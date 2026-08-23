#include "sysmodule_core.h"
#include "lab_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/crypto/sha256.h"
#include "../common/policy/control_policy.h"
#include "../common/protocol/request_schema.h"
#include "../common/protocol/result_builder.h"
#include "../common/rules/rules.h"
#include "../common/rules/holiday_calendar.h"
#include "../common/time/ptc_time.h"
#include "../common/token/token_v1.h"
#include "../common/token/token_v2.h"
#include "../common/version.h"

#ifdef PLAYWISE_DEVICE_LAB
#include "../common/protocol/capability_backend.h"
#define PTC_REQUEST_PROBE_RAW_BLOCK PTC_REQUEST_REMOVED_13
#define PTC_REQUEST_PROBE_SUSPEND PTC_REQUEST_REMOVED_14
#define PTC_REQUEST_PROBE_PLAY_TIMER_WRITE PTC_REQUEST_REMOVED_15
#define PTC_REQUEST_PROBE_APPLY_TODAY_LIMIT PTC_REQUEST_REMOVED_16
#define PTC_REQUEST_PROBE_PLAY_TIMER_EFFECT PTC_REQUEST_REMOVED_17
#define PTC_REQUEST_PREPARE_DEVICE_TEST PTC_REQUEST_REMOVED_18
#endif

typedef struct {
    char device_id[80];
    char grant_secret[128];
    uint16_t max_add_minutes;
    PtcControlMode mode;
    bool allow_unlimited_to_limited;
} PtcRuntimeConfig;

typedef struct {
    uint16_t last_enforced_day_index;
    PtcPctlTargetMode last_enforced_mode;
    uint16_t last_enforced_minutes;
    bool apply_pending_confirmation;
    int64_t apply_confirmation_deadline;
    PtcPctlTargetMode pending_mode;
    uint16_t pending_minutes;
    uint16_t v2_failed_attempts;
    int64_t v2_cooldown_until;
} PtcRuntimeState;

typedef struct {
    char phase[32];
    char compatibility_status[24];
    bool restriction_cleared;
    bool snapshot_available;
    int64_t activate_after;
    bool handover_today_pending;
    uint16_t handover_day_index;
    bool handover_unlimited;
    uint16_t handover_minutes;
    bool handover_remaining_available;
    uint16_t handover_remaining_minutes;
    char last_error[64];
} PtcSetupState;

#define PTC_V2_FAILURE_LIMIT 5u
#define PTC_V2_COOLDOWN_SECONDS 600

/* Retention shares a single PtcStorageEntry array across the log scan and both
   cleanup_timestamped_json calls. One array is ~39 KiB; nesting two of them
   (cleanup -> cleanup_timestamped_json) overflowed the 128 KiB main thread stack.
   Capacity stays at 256 because list_entries has no offset/paging parameter, so a
   smaller value would silently truncate retention. */
#define PTC_CLEANUP_MAX_ENTRIES 256u

static void effect_wait(PtcSysmodule *sysmodule, uint32_t milliseconds);
static PtcErrorCode restore_snapshot_exact(
    PtcSysmodule *sysmodule,
    const PtcPctlSettingsSnapshot *original,
    PtcPctlSettingsSnapshot *restored_snapshot,
    PtcPctlStatus *restored_status,
    uint8_t weekday,
    bool *raw_restored,
    bool *timer_restored);
static PtcErrorCode start_timer_and_wait_target(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now,
    const char *mode_name,
    PtcPctlTargetMode target_mode,
    uint16_t minutes,
    const char *event_detail,
    PtcPctlStatus *observed,
    bool *timer_started);
static PtcErrorCode release_setup_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now);
static PtcErrorCode direct_handover_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now);
static PtcErrorCode restore_install_snapshot_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now);
static void write_disable_flag(PtcSysmodule *sysmodule, const char *reason);
static void recovery_clear(PtcSysmodule *sysmodule);
static bool save_rules(PtcSysmodule *sysmodule, const PtcRules *rules);

static void join_path(char *out, size_t out_size, const char *a, const char *b)
{
    snprintf(out, out_size, "%s/%s", a, b);
}

static bool daily_log_path(PtcSysmodule *sysmodule, const char *name, char *out, size_t out_size)
{
    char date[11];
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    if (!ptc_format_date_utc8(now.unix_seconds, date)) {
        return snprintf(out, out_size, "%s/logs/undated/%s/%s", sysmodule->app_root, sysmodule->boot_id, name) > 0;
    }
    return snprintf(out, out_size, "%s/logs/%s/%s", sysmodule->app_root, date, name) > 0;
}

static bool metadata_equal(const PtcStorageMetadata *a, const PtcStorageMetadata *b)
{
    return a->type == b->type && a->modified_time_valid == b->modified_time_valid &&
        (!a->modified_time_valid || a->modified_unix_seconds == b->modified_unix_seconds);
}

static bool read_cached_text(PtcSysmodule *sysmodule, const char *relative, char *cache, size_t cache_size,
    PtcStorageMetadata *cached_meta, bool *cache_valid, char *out, size_t out_size, bool missing_is_empty)
{
    char path[320];
    PtcStorageMetadata current;
    join_path(path, sizeof(path), sysmodule->app_root, relative);
    if (!sysmodule->storage->vtable->metadata || !sysmodule->storage->vtable->metadata(sysmodule->storage, path, &current)) {
        if (missing_is_empty) { cache[0] = '\0'; memset(cached_meta, 0, sizeof(*cached_meta)); *cache_valid = true; out[0] = '\0'; return true; }
        *cache_valid = false;
        return false;
    }
    if (*cache_valid && metadata_equal(cached_meta, &current)) {
        snprintf(out, out_size, "%s", cache);
        return true;
    }
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, cache, cache_size)) { *cache_valid = false; return false; }
    *cached_meta = current;
    *cache_valid = true;
    snprintf(out, out_size, "%s", cache);
    return true;
}

static void invalidate_all_caches(PtcSysmodule *sysmodule)
{
    sysmodule->config_cache_valid = false;
    sysmodule->rules_cache_valid = false;
    sysmodule->state_cache_valid = false;
    sysmodule->capabilities_cache_valid = false;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        ++p;
    }
    return p;
}

static const char *find_key(const char *text, const char *key)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(text, pattern);
}

static bool json_string(const char *text, const char *key, char *out, size_t out_size)
{
    const char *pos = find_key(text, key);
    const char *start;
    const char *end;
    if (!pos) {
        return false;
    }
    pos = strchr(pos + strlen(key) + 2, ':');
    if (!pos) {
        return false;
    }
    pos = skip_ws(pos + 1);
    if (*pos != '"') {
        return false;
    }
    start = pos + 1;
    end = strchr(start, '"');
    if (!end || (size_t)(end - start) >= out_size) {
        return false;
    }
    memcpy(out, start, (size_t)(end - start));
    out[end - start] = '\0';
    return true;
}

static bool json_i64(const char *text, const char *key, int64_t *out)
{
    const char *pos = find_key(text, key);
    char *endptr;
    long long value;
    if (!pos) {
        return false;
    }
    pos = strchr(pos + strlen(key) + 2, ':');
    if (!pos) {
        return false;
    }
    pos = skip_ws(pos + 1);
    value = strtoll(pos, &endptr, 10);
    if (endptr == pos) {
        return false;
    }
    *out = (int64_t)value;
    return true;
}

static bool json_u16(const char *text, const char *key, uint16_t *out)
{
    int64_t value;
    if (!json_i64(text, key, &value) || value < 0 || value > 65535) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

static bool json_bool_value(const char *text, const char *key, bool *out)
{
    const char *pos = find_key(text, key);
    if (!pos) {
        return false;
    }
    pos = strchr(pos + strlen(key) + 2, ':');
    if (!pos) {
        return false;
    }
    pos = skip_ws(pos + 1);
    if (strncmp(pos, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static void setup_state_default(PtcSetupState *setup)
{
    memset(setup, 0, sizeof(*setup));
    snprintf(setup->phase, sizeof(setup->phase), "unconfigured");
    snprintf(setup->compatibility_status, sizeof(setup->compatibility_status), "pending");
}

static bool load_setup_state(PtcSysmodule *sysmodule, PtcSetupState *setup)
{
    char path[320];
    char text[1024];
    int64_t version;
    setup_state_default(setup);
    join_path(path, sizeof(path), sysmodule->app_root, "setup.json");
    if (!sysmodule->storage->vtable->exists(sysmodule->storage, path)) {
        return true;
    }
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text)) ||
        !json_i64(text, "version", &version) || version != 1 ||
        !json_string(text, "phase", setup->phase, sizeof(setup->phase))) {
        return false;
    }
    (void)json_bool_value(text, "restriction_cleared", &setup->restriction_cleared);
    if (!json_string(text, "compatibility_status", setup->compatibility_status, sizeof(setup->compatibility_status))) {
        snprintf(setup->compatibility_status, sizeof(setup->compatibility_status),
            strcmp(setup->phase, "active") == 0 ? "accepted" : "pending");
    }
    (void)json_bool_value(text, "snapshot_available", &setup->snapshot_available);
    (void)json_i64(text, "activate_after", &setup->activate_after);
    (void)json_bool_value(text, "handover_today_pending", &setup->handover_today_pending);
    (void)json_u16(text, "handover_day_index", &setup->handover_day_index);
    (void)json_bool_value(text, "handover_unlimited", &setup->handover_unlimited);
    (void)json_u16(text, "handover_minutes", &setup->handover_minutes);
    (void)json_bool_value(text, "handover_remaining_available", &setup->handover_remaining_available);
    (void)json_u16(text, "handover_remaining_minutes", &setup->handover_remaining_minutes);
    (void)json_string(text, "last_error", setup->last_error, sizeof(setup->last_error));
    return strcmp(setup->phase, "unconfigured") == 0 || strcmp(setup->phase, "compatibility_pending") == 0 ||
        strcmp(setup->phase, "protection") == 0 || strcmp(setup->phase, "pending") == 0 || strcmp(setup->phase, "released") == 0 ||
        strcmp(setup->phase, "active") == 0 || strcmp(setup->phase, "failed") == 0 ||
        strcmp(setup->phase, "restored") == 0;
}

static bool save_setup_state(PtcSysmodule *sysmodule, const PtcSetupState *setup)
{
    char path[320];
    char text[512];
    join_path(path, sizeof(path), sysmodule->app_root, "setup.json");
    snprintf(text, sizeof(text),
        "{\"version\":1,\"phase\":\"%s\",\"compatibility_status\":\"%s\",\"restriction_cleared\":%s,"
        "\"snapshot_available\":%s,\"activate_after\":%lld,\"handover_today_pending\":%s,"
        "\"handover_day_index\":%u,\"handover_unlimited\":%s,\"handover_minutes\":%u,"
        "\"handover_remaining_available\":%s,\"handover_remaining_minutes\":%u,\"last_error\":\"%s\"}\n",
        setup->phase,
        setup->compatibility_status,
        setup->restriction_cleared ? "true" : "false",
        setup->snapshot_available ? "true" : "false",
        (long long)setup->activate_after,
        setup->handover_today_pending ? "true" : "false",
        (unsigned int)setup->handover_day_index,
        setup->handover_unlimited ? "true" : "false",
        (unsigned int)setup->handover_minutes,
        setup->handover_remaining_available ? "true" : "false",
        (unsigned int)setup->handover_remaining_minutes,
        setup->last_error);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text);
}

static void bytes_hex(char *out, size_t out_size, const uint8_t *data, size_t size)
{
    static const char HEX[] = "0123456789abcdef";
    size_t i;
    if (!out || out_size < size * 2U + 1U) return;
    for (i = 0; i < size; ++i) {
        out[i * 2U] = HEX[(data[i] >> 4) & 0x0fU];
        out[i * 2U + 1U] = HEX[data[i] & 0x0fU];
    }
    out[size * 2U] = '\0';
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool hex_bytes(const char *text, uint8_t *out, size_t size)
{
    size_t i;
    if (!text || strlen(text) != size * 2U) return false;
    for (i = 0; i < size; ++i) {
        int hi = hex_digit(text[i * 2U]);
        int lo = hex_digit(text[i * 2U + 1U]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static void snapshot_sha256(char out[65], const PtcPctlSettingsSnapshot *snapshot)
{
    PtcSha256Ctx ctx;
    uint8_t digest[PTC_SHA256_DIGEST_SIZE];
    uint8_t timer = snapshot->timer_enabled ? 1U : 0U;
    ptc_sha256_init(&ctx);
    ptc_sha256_update(&ctx, snapshot->data, snapshot->size);
    ptc_sha256_update(&ctx, &timer, 1U);
    ptc_sha256_final(&ctx, digest);
    bytes_hex(out, 65U, digest, sizeof(digest));
}

static bool save_install_snapshot(PtcSysmodule *sysmodule, const PtcPctlSettingsSnapshot *snapshot, int64_t captured_at)
{
    char path[320];
    char settings_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];
    char digest[65];
    char text[512];
    join_path(path, sizeof(path), sysmodule->app_root, "backups/install_pctl_snapshot.json");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, path)) return true;
    if (!snapshot || snapshot->size != PTC_PCTL_OPAQUE_SETTINGS_SIZE) return false;
    bytes_hex(settings_hex, sizeof(settings_hex), snapshot->data, snapshot->size);
    snapshot_sha256(digest, snapshot);
    snprintf(text, sizeof(text),
        "{\"version\":1,\"captured_at\":%lld,\"size\":%u,\"timer_enabled\":%s,"
        "\"settings_hex\":\"%s\",\"sha256\":\"%s\"}\n",
        (long long)captured_at, (unsigned int)snapshot->size,
        snapshot->timer_enabled ? "true" : "false", settings_hex, digest);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text);
}

static bool load_install_snapshot(PtcSysmodule *sysmodule, PtcPctlSettingsSnapshot *snapshot)
{
    char path[320];
    char text[1024];
    char settings_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];
    char expected_digest[65];
    char actual_digest[65];
    int64_t version;
    int64_t size;
    bool timer_enabled;
    join_path(path, sizeof(path), sysmodule->app_root, "backups/install_pctl_snapshot.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text)) ||
        !json_i64(text, "version", &version) || version != 1 ||
        !json_i64(text, "size", &size) || size != PTC_PCTL_OPAQUE_SETTINGS_SIZE ||
        !json_bool_value(text, "timer_enabled", &timer_enabled) ||
        !json_string(text, "settings_hex", settings_hex, sizeof(settings_hex)) ||
        !json_string(text, "sha256", expected_digest, sizeof(expected_digest))) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->size = (uint32_t)size;
    snapshot->timer_enabled = timer_enabled;
    if (!hex_bytes(settings_hex, snapshot->data, snapshot->size)) return false;
    snapshot_sha256(actual_digest, snapshot);
    return strcmp(actual_digest, expected_digest) == 0;
}

static bool save_snapshot_file(PtcSysmodule *sysmodule, const char *relative,
    const PtcPctlSettingsSnapshot *snapshot, int64_t captured_at)
{
    char path[320];
    char settings_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];
    char digest[65];
    char text[512];
    if (!snapshot || snapshot->size != PTC_PCTL_OPAQUE_SETTINGS_SIZE) return false;
    join_path(path, sizeof(path), sysmodule->app_root, relative);
    bytes_hex(settings_hex, sizeof(settings_hex), snapshot->data, snapshot->size);
    snapshot_sha256(digest, snapshot);
    snprintf(text, sizeof(text),
        "{\"version\":1,\"captured_at\":%lld,\"size\":%u,\"timer_enabled\":%s,"
        "\"settings_hex\":\"%s\",\"sha256\":\"%s\"}\n",
        (long long)captured_at, (unsigned int)snapshot->size,
        snapshot->timer_enabled ? "true" : "false", settings_hex, digest);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text);
}

static bool load_snapshot_file(PtcSysmodule *sysmodule, const char *relative, PtcPctlSettingsSnapshot *snapshot)
{
    char path[320];
    char text[1024];
    char settings_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];
    char expected_digest[65];
    char actual_digest[65];
    int64_t version;
    int64_t size;
    bool timer_enabled;
    join_path(path, sizeof(path), sysmodule->app_root, relative);
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text)) ||
        !json_i64(text, "version", &version) || version != 1 ||
        !json_i64(text, "size", &size) || size != PTC_PCTL_OPAQUE_SETTINGS_SIZE ||
        !json_bool_value(text, "timer_enabled", &timer_enabled) ||
        !json_string(text, "settings_hex", settings_hex, sizeof(settings_hex)) ||
        !json_string(text, "sha256", expected_digest, sizeof(expected_digest))) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->size = (uint32_t)size;
    snapshot->timer_enabled = timer_enabled;
    if (!hex_bytes(settings_hex, snapshot->data, snapshot->size)) return false;
    snapshot_sha256(actual_digest, snapshot);
    return strcmp(actual_digest, expected_digest) == 0;
}

static bool recovery_path_exists(PtcSysmodule *sysmodule)
{
    char path[320];
    join_path(path, sizeof(path), sysmodule->app_root, "recovery/active/meta.json");
    return sysmodule->storage->vtable->exists(sysmodule->storage, path);
}

static bool backup_text_file(PtcSysmodule *sysmodule, const char *relative, const char *backup_relative, bool *existed)
{
    char source[320];
    char backup[320];
    char text[16384];
    join_path(source, sizeof(source), sysmodule->app_root, relative);
    join_path(backup, sizeof(backup), sysmodule->app_root, backup_relative);
    *existed = sysmodule->storage->vtable->exists(sysmodule->storage, source);
    if (!*existed) text[0] = '\0';
    else if (!sysmodule->storage->vtable->read_text(sysmodule->storage, source, text, sizeof(text))) return false;
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, backup, text);
}

static bool restore_text_file(PtcSysmodule *sysmodule, const char *relative, const char *backup_relative, bool existed)
{
    char target[320];
    char backup[320];
    char text[16384];
    join_path(target, sizeof(target), sysmodule->app_root, relative);
    if (!existed) {
        return !sysmodule->storage->vtable->exists(sysmodule->storage, target) ||
            sysmodule->storage->vtable->remove_path(sysmodule->storage, target);
    }
    join_path(backup, sizeof(backup), sysmodule->app_root, backup_relative);
    return sysmodule->storage->vtable->read_text(sysmodule->storage, backup, text, sizeof(text)) &&
        sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, target, text);
}

static bool recovery_begin(PtcSysmodule *sysmodule, const PtcRequest *request, PtcClockSnapshot now)
{
    PtcPctlSettingsSnapshot snapshot;
    char meta_path[320];
    char meta[512];
    bool rules_existed;
    bool state_existed;
    bool ledger_existed;
    if (recovery_path_exists(sysmodule)) return true;
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &snapshot) != PTC_ERR_OK ||
        !save_snapshot_file(sysmodule, "recovery/active/pctl_snapshot.json", &snapshot, now.unix_seconds) ||
        !backup_text_file(sysmodule, "rules.json", "recovery/active/rules.before", &rules_existed) ||
        !backup_text_file(sysmodule, "state.json", "recovery/active/state.before", &state_existed) ||
        !backup_text_file(sysmodule, "ledger/used_nonces.jsonl", "recovery/active/ledger.before", &ledger_existed)) {
        recovery_clear(sysmodule);
        return false;
    }
    join_path(meta_path, sizeof(meta_path), sysmodule->app_root, "recovery/active/meta.json");
    snprintf(meta, sizeof(meta),
        "{\"version\":1,\"request_id\":\"%s\",\"created_at\":%lld,"
        "\"rules_existed\":%s,\"state_existed\":%s,\"ledger_existed\":%s}\n",
        request && ptc_request_id_is_valid(request->request_id) ? request->request_id : "enforce",
        (long long)now.unix_seconds,
        rules_existed ? "true" : "false",
        state_existed ? "true" : "false",
        ledger_existed ? "true" : "false");
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, meta_path, meta)) {
        recovery_clear(sysmodule);
        return false;
    }
    return true;
}

static void recovery_clear(PtcSysmodule *sysmodule)
{
    char path[320];
    join_path(path, sizeof(path), sysmodule->app_root, "recovery/active");
    if (sysmodule->storage->vtable->remove_tree) {
        (void)sysmodule->storage->vtable->remove_tree(sysmodule->storage, path);
    }
}

static bool recovery_rollback(PtcSysmodule *sysmodule)
{
    char meta_path[320];
    char meta[1024];
    PtcPctlSettingsSnapshot original;
    PtcPctlSettingsSnapshot restored;
    PtcPctlStatus status;
    bool rules_existed;
    bool state_existed;
    bool ledger_existed;
    bool raw_restored = false;
    bool timer_restored = false;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    bool ok;
    if (!recovery_path_exists(sysmodule)) return true;
    join_path(meta_path, sizeof(meta_path), sysmodule->app_root, "recovery/active/meta.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, meta_path, meta, sizeof(meta)) ||
        !json_bool_value(meta, "rules_existed", &rules_existed) ||
        !json_bool_value(meta, "state_existed", &state_existed) ||
        !json_bool_value(meta, "ledger_existed", &ledger_existed) ||
        !load_snapshot_file(sysmodule, "recovery/active/pctl_snapshot.json", &original)) return false;
    ok = restore_snapshot_exact(sysmodule, &original, &restored, &status,
        ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored) == PTC_ERR_OK;
    ok = restore_text_file(sysmodule, "rules.json", "recovery/active/rules.before", rules_existed) && ok;
    ok = restore_text_file(sysmodule, "state.json", "recovery/active/state.before", state_existed) && ok;
    ok = restore_text_file(sysmodule, "ledger/used_nonces.jsonl", "recovery/active/ledger.before", ledger_existed) && ok;
    invalidate_all_caches(sysmodule);
    if (ok) recovery_clear(sysmodule);
    return ok;
}

static const char *rule_mode_name(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_UNLIMITED:
        return "unlimited";
    case PTC_RULE_MODE_LIMIT:
    default:
        return "limit";
    }
}

static bool parse_rule_mode(const char *value, PtcRuleMode *out)
{
    if (strcmp(value, "limit") == 0) {
        *out = PTC_RULE_MODE_LIMIT;
        return true;
    }
    if (strcmp(value, "unlimited") == 0) {
        *out = PTC_RULE_MODE_UNLIMITED;
        return true;
    }
    return false;
}

static const char *pctl_target_mode_name(PtcPctlTargetMode mode)
{
    switch (mode) {
    case PTC_PCTL_TARGET_UNLIMITED:
        return "unlimited";
    case PTC_PCTL_TARGET_BLOCKED:
        return "blocked";
    case PTC_PCTL_TARGET_LIMIT:
    default:
        return "limit";
    }
}

static bool parse_rule_array(const char *text, const char *key, PtcDayRule week[7])
{
    const char *pos = find_key(text, key);
    unsigned int i;
    if (!pos) {
        return false;
    }
    pos = strchr(pos, '[');
    if (!pos) {
        return false;
    }
    for (i = 0; i < 7; ++i) {
        const char *obj_start = strchr(pos, '{');
        const char *obj_end;
        char item[192];
        char mode[24];
        size_t len;
        if (!obj_start) {
            return false;
        }
        obj_end = strchr(obj_start, '}');
        if (!obj_end) {
            return false;
        }
        len = (size_t)(obj_end - obj_start + 1);
        if (len >= sizeof(item)) {
            return false;
        }
        memcpy(item, obj_start, len);
        item[len] = '\0';
        if (!json_string(item, "mode", mode, sizeof(mode)) ||
            !parse_rule_mode(mode, &week[i].mode)) {
            return false;
        }
        if (!json_u16(item, "minutes", &week[i].minutes)) {
            week[i].minutes = 0;
        }
        pos = obj_end + 1;
    }
    return true;
}

static void append_event(PtcSysmodule *sysmodule, const PtcRequest *request, const char *event, PtcErrorCode error, const char *detail)
{
    char path[320];
    char line[512];
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    if (!daily_log_path(sysmodule, "events.jsonl", path, sizeof(path))) return;
    snprintf(
        line,
        sizeof(line),
        "{\"ts\":%lld,\"request_id\":\"%s\",\"type\":\"%s\",\"event\":\"%s\",\"error\":\"%s\",\"detail\":\"%s\"}",
        (long long)now.unix_seconds,
        request ? request->request_id : "unknown",
        request ? request->type_text : "unknown",
        event,
        ptc_error_reason(error),
        detail ? detail : "");
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, path, line);
#ifndef __SWITCH__
    snprintf(path, sizeof(path), "%s/logs/events.jsonl", sysmodule->app_root);
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, path, line);
#endif
    /* A small, independently disposable summary feeds Support without parsing arbitrary logs. */
    if (strcmp(event, "result_error") == 0 || strcmp(event, "pctl_apply_failed") == 0 ||
        strcmp(event, "effect_restore") == 0 || strcmp(event, "effect_restore_failed") == 0 ||
        strcmp(event, "handover_preserved") == 0 || strcmp(event, "handover_restore") == 0 ||
        (strcmp(event, "result_ok") == 0 && request && strcmp(request->type_text, "status") != 0)) {
        char old[4096] = "";
        char summary_path[320];
        char output[4096] = "";
        const char *lines[20];
        size_t count = 0;
        char *cursor;
        join_path(summary_path, sizeof(summary_path), sysmodule->app_root, "support/recent-events.jsonl");
        (void)sysmodule->storage->vtable->read_text(sysmodule->storage, summary_path, old, sizeof(old));
        cursor = old;
        while (*cursor) {
            char *newline = strchr(cursor, '\n');
            if (count == 20) { memmove(lines, lines + 1, 19 * sizeof(lines[0])); count = 19; }
            lines[count++] = cursor;
            if (!newline) break;
            *newline = '\0';
            cursor = newline + 1;
        }
        if (count == 20) { memmove(lines, lines + 1, 19 * sizeof(lines[0])); count = 19; }
        for (size_t i = 0; i < count; ++i) {
            if (lines[i][0]) { strncat(output, lines[i], sizeof(output) - strlen(output) - 1); strncat(output, "\n", sizeof(output) - strlen(output) - 1); }
        }
        strncat(output, line, sizeof(output) - strlen(output) - 1);
        strncat(output, "\n", sizeof(output) - strlen(output) - 1);
        (void)sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, summary_path, output);
    }
    /* Keep the legacy root log readable for older desktop/self-check tooling during migration. */
}

static void json_safe_copy(char *out, size_t out_size, const char *value)
{
    size_t used = 0;
    if (out_size == 0) {
        return;
    }
    if (!value) {
        out[0] = '\0';
        return;
    }
    while (*value && used + 1 < out_size) {
        unsigned char ch = (unsigned char)*value++;
        out[used++] = (ch >= 32U && ch != '"' && ch != '\\') ? (char)ch : '_';
    }
    out[used] = '\0';
}

static void empty_pctl_debug_snapshot(PtcPctlDebugSnapshot *out)
{
    memset(out, 0, sizeof(*out));
    out->available = false;
    out->error = PTC_ERR_PCTL_READ_FAILED;
}

static void take_pctl_debug_snapshot(PtcSysmodule *sysmodule, PtcPctlDebugSnapshot *out)
{
    empty_pctl_debug_snapshot(out);
    if (!sysmodule->pctl || !sysmodule->pctl->vtable || !sysmodule->pctl->vtable->debug_snapshot) {
        return;
    }
    (void)sysmodule->pctl->vtable->debug_snapshot(sysmodule->pctl, out);
}

static uint32_t last_pctl_ipc_result(PtcSysmodule *sysmodule)
{
    if (!sysmodule->pctl || !sysmodule->pctl->vtable || !sysmodule->pctl->vtable->last_ipc_result) {
        return 0;
    }
    return sysmodule->pctl->vtable->last_ipc_result(sysmodule->pctl);
}

static void append_pctl_debug(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *stage,
    const char *mode,
    const PtcPctlTarget *target,
    PtcErrorCode error,
    uint32_t ipc_result,
    const PtcPctlDebugSnapshot *before,
    const PtcPctlDebugSnapshot *after)
{
    char path[320];
    char line[2048];
    char request_id[80];
    char type[80];
    char stage_safe[80];
    char mode_safe[32];
    char before_raw[160];
    char before_slots[320];
    char after_raw[160];
    char after_slots[320];
    const PtcPctlDebugSnapshot *before_snapshot = before;
    const PtcPctlDebugSnapshot *after_snapshot = after;
    PtcPctlDebugSnapshot empty_before;
    PtcPctlDebugSnapshot empty_after;

    if (!before_snapshot) {
        empty_pctl_debug_snapshot(&empty_before);
        before_snapshot = &empty_before;
    }
    if (!after_snapshot) {
        empty_pctl_debug_snapshot(&empty_after);
        after_snapshot = &empty_after;
    }
    json_safe_copy(request_id, sizeof(request_id), request ? request->request_id : "unknown");
    json_safe_copy(type, sizeof(type), request ? request->type_text : "unknown");
    json_safe_copy(stage_safe, sizeof(stage_safe), stage);
    json_safe_copy(mode_safe, sizeof(mode_safe), mode ? mode : "unknown");
    json_safe_copy(before_raw, sizeof(before_raw), before_snapshot->raw_hex);
    json_safe_copy(before_slots, sizeof(before_slots), before_snapshot->decoded_slots);
    json_safe_copy(after_raw, sizeof(after_raw), after_snapshot->raw_hex);
    json_safe_copy(after_slots, sizeof(after_slots), after_snapshot->decoded_slots);
    if (!daily_log_path(sysmodule, "pctl_debug.jsonl", path, sizeof(path))) return;
    snprintf(
        line,
        sizeof(line),
        "{\"ts\":%lld,\"request_id\":\"%s\",\"type\":\"%s\",\"stage\":\"%s\","
        "\"mode\":\"%s\",\"target_mode\":\"%s\",\"target_minutes\":%u,\"weekday\":%u,"
        "\"error\":\"%s\",\"ipc_result\":\"0x%08x\","
        "\"before_available\":%s,\"before_error\":\"%s\",\"before_ipc_result\":\"0x%08x\","
        "\"before_raw_hex\":\"%s\",\"before_slots\":\"%s\","
        "\"after_available\":%s,\"after_error\":\"%s\",\"after_ipc_result\":\"0x%08x\","
        "\"after_raw_hex\":\"%s\",\"after_slots\":\"%s\"}",
        (long long)sysmodule->time_provider->vtable->now(sysmodule->time_provider).unix_seconds,
        request_id,
        type,
        stage_safe,
        mode_safe,
        target ? pctl_target_mode_name(target->mode) : "none",
        target ? (unsigned int)target->minutes : 0U,
        target ? (unsigned int)target->weekday : 0U,
        ptc_error_reason(error),
        (unsigned int)ipc_result,
        before_snapshot->available ? "true" : "false",
        ptc_error_reason(before_snapshot->error),
        (unsigned int)before_snapshot->ipc_result,
        before_raw,
        before_slots,
        after_snapshot->available ? "true" : "false",
        ptc_error_reason(after_snapshot->error),
        (unsigned int)after_snapshot->ipc_result,
        after_raw,
        after_slots);
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, path, line);
#ifndef __SWITCH__
    snprintf(path, sizeof(path), "%s/logs/pctl_debug.jsonl", sysmodule->app_root);
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, path, line);
#endif
}

static bool load_config(PtcSysmodule *sysmodule, PtcRuntimeConfig *config)
{
    char path[320];
    char text[4096];
    char credentials[512];
    int64_t version;
    join_path(path, sizeof(path), sysmodule->app_root, "config.json");
    if (!read_cached_text(sysmodule, "config.json", sysmodule->config_cache_text, sizeof(sysmodule->config_cache_text),
            &sysmodule->config_meta, &sysmodule->config_cache_valid, text, sizeof(text), false)) {
        return false;
    }
    if (!json_i64(text, "version", &version) || version != 1 ||
        !json_string(text, "device_id", config->device_id, sizeof(config->device_id))) {
        return false;
    }
    config->grant_secret[0] = '\0';
    join_path(path, sizeof(path), sysmodule->app_root, "credentials.json");
    if (sysmodule->storage->vtable->read_text(sysmodule->storage, path, credentials, sizeof(credentials))) {
        int64_t credentials_version;
        if (!json_i64(credentials, "version", &credentials_version) || credentials_version != 1 ||
            !json_string(credentials, "grant_secret", config->grant_secret, sizeof(config->grant_secret))) {
            return false;
        }
    }
    if (!json_u16(text, "max_add_minutes", &config->max_add_minutes)) {
        config->max_add_minutes = 240;
    }
    config->mode = PTC_CONTROL_ENFORCE;
    config->allow_unlimited_to_limited = true;
    return true;
}

static PtcCapabilities load_capabilities(PtcSysmodule *sysmodule)
{
    PtcCapabilities caps;
    (void)sysmodule;
    caps.play_timer_write_verified = true;
    caps.play_timer_effect_verified = true;
    caps.play_timer_effect_backend[0] = '\0';
#ifdef PLAYWISE_DEVICE_LAB
    caps.raw_block_verified = false;
    caps.suspend_verified = false;
#endif
    return caps;
}

#ifdef PLAYWISE_DEVICE_LAB
static bool save_capabilities(PtcSysmodule *sysmodule, const PtcCapabilities *caps, int64_t updated_at)
{
    char path[320];
    char text[512];
    snprintf(path, sizeof(path), "%s/capabilities.json", sysmodule->app_root);
    snprintf(
        text,
        sizeof(text),
        "{\"version\":1,\"raw_block_verified\":%s,"
        "\"raw_block_backend\":\"%s\",\"suspend_verified\":%s,"
        "\"suspend_backend\":\"%s\",\"verified_at\":{\"raw_block\":%lld,\"suspend\":%lld}}\n",
        caps->raw_block_verified ? "true" : "false",
        PTC_RAW_BLOCK_BACKEND,
        caps->suspend_verified ? "true" : "false",
        PTC_SUSPEND_BACKEND,
        caps->raw_block_verified ? (long long)updated_at : 0LL,
        caps->suspend_verified ? (long long)updated_at : 0LL);
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text)) return false;
    snprintf(sysmodule->capabilities_cache_text, sizeof(sysmodule->capabilities_cache_text), "%s", text);
    sysmodule->capabilities_cache_valid = sysmodule->storage->vtable->metadata &&
        sysmodule->storage->vtable->metadata(sysmodule->storage, path, &sysmodule->capabilities_meta);
    return true;
}

static bool restore_capabilities(PtcSysmodule *sysmodule, const PtcCapabilities *caps, bool existed, int64_t updated_at)
{
    char path[320];
    if (existed) {
        return save_capabilities(sysmodule, caps, updated_at);
    }
    join_path(path, sizeof(path), sysmodule->app_root, "capabilities.json");
    sysmodule->capabilities_cache_valid = false;
    return !sysmodule->storage->vtable->exists(sysmodule->storage, path) ||
        sysmodule->storage->vtable->remove_path(sysmodule->storage, path);
}
#endif

static bool load_rules(PtcSysmodule *sysmodule, PtcRules *rules)
{
    char path[320];
    char text[4096];
    char mode[24];
    int64_t version;
    ptc_rules_default(rules);
    join_path(path, sizeof(path), sysmodule->app_root, "rules.json");
    if (!read_cached_text(sysmodule, "rules.json", sysmodule->rules_cache_text, sizeof(sysmodule->rules_cache_text),
            &sysmodule->rules_meta, &sysmodule->rules_cache_valid, text, sizeof(text), true) || text[0] == '\0') {
        return true;
    }
    if (!json_i64(text, "version", &version) || version != 1) {
        return false;
    }
    (void)parse_rule_array(text, "week", rules->week);
    if (json_bool_value(text, "today_override_present", &rules->today_override.present)) {
        (void)json_u16(text, "today_override_day_index", &rules->today_override.day_index);
        if (json_string(text, "today_override_mode", mode, sizeof(mode))) {
            (void)parse_rule_mode(mode, &rules->today_override.rule.mode);
        }
        (void)json_u16(text, "today_override_minutes", &rules->today_override.rule.minutes);
    }
    (void)json_bool_value(text, "holiday_enabled", &rules->holiday_enabled);
    if (json_string(text, "holiday_mode", mode, sizeof(mode))) {
        (void)parse_rule_mode(mode, &rules->holiday_rule.mode);
    }
    (void)json_u16(text, "holiday_minutes", &rules->holiday_rule.minutes);
    if (json_string(text, "makeup_workday_mode", mode, sizeof(mode))) {
        (void)parse_rule_mode(mode, &rules->makeup_workday_rule.mode);
    }
    (void)json_u16(text, "makeup_workday_minutes", &rules->makeup_workday_rule.minutes);
    return true;
}

static bool save_rules(PtcSysmodule *sysmodule, const PtcRules *rules)
{
    char path[320];
    char text[4096];
    size_t used;
    unsigned int i;
    snprintf(path, sizeof(path), "%s/rules.json", sysmodule->app_root);
    snprintf(text, sizeof(text), "{\"version\":1,\"week\":[");
    for (i = 0; i < 7; ++i) {
        used = strlen(text);
        snprintf(
            text + used,
            sizeof(text) - used,
            "%s{\"mode\":\"%s\",\"minutes\":%u}",
            i == 0 ? "" : ",",
            rule_mode_name(rules->week[i].mode),
            rules->week[i].minutes);
    }
    used = strlen(text);
    snprintf(
        text + used,
        sizeof(text) - used,
        "],\"today_override_present\":%s,\"today_override_day_index\":%u,"
        "\"today_override_mode\":\"%s\",\"today_override_minutes\":%u,"
        "\"holiday_enabled\":%s,\"holiday_mode\":\"%s\",\"holiday_minutes\":%u,"
        "\"makeup_workday_mode\":\"%s\",\"makeup_workday_minutes\":%u}\n",
        rules->today_override.present ? "true" : "false",
        rules->today_override.day_index,
        rule_mode_name(rules->today_override.rule.mode),
        rules->today_override.rule.minutes,
        rules->holiday_enabled ? "true" : "false",
        rule_mode_name(rules->holiday_rule.mode),
        rules->holiday_rule.minutes,
        rule_mode_name(rules->makeup_workday_rule.mode),
        rules->makeup_workday_rule.minutes);
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text)) return false;
    snprintf(sysmodule->rules_cache_text, sizeof(sysmodule->rules_cache_text), "%s", text);
    sysmodule->rules_cache_valid = sysmodule->storage->vtable->metadata &&
        sysmodule->storage->vtable->metadata(sysmodule->storage, path, &sysmodule->rules_meta);
    return true;
}

static bool restore_rules(PtcSysmodule *sysmodule, const PtcRules *rules, bool existed)
{
    char path[320];
    if (existed) {
        return save_rules(sysmodule, rules);
    }
    join_path(path, sizeof(path), sysmodule->app_root, "rules.json");
    return !sysmodule->storage->vtable->exists(sysmodule->storage, path) ||
        sysmodule->storage->vtable->remove_path(sysmodule->storage, path);
}

static bool load_state(PtcSysmodule *sysmodule, PtcRuntimeState *state)
{
    char path[320];
    char text[1024];
    int64_t version;
    state->last_enforced_day_index = 0;
    state->last_enforced_mode = 0;
    state->last_enforced_minutes = 0;
    state->apply_pending_confirmation = false;
    state->apply_confirmation_deadline = 0;
    state->pending_mode = 0;
    state->pending_minutes = 0;
    state->v2_failed_attempts = 0;
    state->v2_cooldown_until = 0;
    join_path(path, sizeof(path), sysmodule->app_root, "state.json");
    if (!read_cached_text(sysmodule, "state.json", sysmodule->state_cache_text, sizeof(sysmodule->state_cache_text),
            &sysmodule->state_meta, &sysmodule->state_cache_valid, text, sizeof(text), true) || text[0] == '\0') {
        return true;
    }
    if (!json_i64(text, "version", &version) || version != 1) {
        return false;
    }
    (void)json_u16(text, "last_enforced_day_index", &state->last_enforced_day_index);
    (void)json_u16(text, "last_enforced_minutes", &state->last_enforced_minutes);
    (void)json_bool_value(text, "apply_pending_confirmation", &state->apply_pending_confirmation);
    (void)json_i64(text, "apply_confirmation_deadline", &state->apply_confirmation_deadline);
    (void)json_u16(text, "pending_minutes", &state->pending_minutes);
    {
        uint16_t pending_mode = 0;
        if (json_u16(text, "pending_mode", &pending_mode)) state->pending_mode = (PtcPctlTargetMode)pending_mode;
    }
    (void)json_u16(text, "v2_failed_attempts", &state->v2_failed_attempts);
    (void)json_i64(text, "v2_cooldown_until", &state->v2_cooldown_until);
    {
        uint16_t mode = 0;
        if (json_u16(text, "last_enforced_mode", &mode)) {
            state->last_enforced_mode = (PtcPctlTargetMode)mode;
        }
    }
    return true;
}

static bool save_state(PtcSysmodule *sysmodule, const PtcRuntimeState *state, int64_t updated_at)
{
    char path[320];
    char text[768];
    snprintf(path, sizeof(path), "%s/state.json", sysmodule->app_root);
    snprintf(
        text,
        sizeof(text),
        "{\"version\":1,\"last_enforced_day_index\":%u,"
        "\"last_enforced_mode\":%u,\"last_enforced_minutes\":%u,"
        "\"apply_status\":\"%s\",\"apply_pending_confirmation\":%s,"
        "\"apply_confirmation_deadline\":%lld,\"pending_mode\":%u,\"pending_minutes\":%u,"
        "\"v2_failed_attempts\":%u,\"v2_cooldown_until\":%lld,\"updated_at\":%lld}\n",
        state->last_enforced_day_index,
        (unsigned int)state->last_enforced_mode,
        state->last_enforced_minutes,
        state->apply_pending_confirmation ? "applied_pending_confirmation" : "idle",
        state->apply_pending_confirmation ? "true" : "false",
        (long long)state->apply_confirmation_deadline,
        (unsigned int)state->pending_mode,
        state->pending_minutes,
        state->v2_failed_attempts,
        (long long)state->v2_cooldown_until,
        (long long)updated_at);
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text)) return false;
    snprintf(sysmodule->state_cache_text, sizeof(sysmodule->state_cache_text), "%s", text);
    sysmodule->state_cache_valid = sysmodule->storage->vtable->metadata &&
        sysmodule->storage->vtable->metadata(sysmodule->storage, path, &sysmodule->state_meta);
    return true;
}

bool ptc_sysmodule_refresh_caches(PtcSysmodule *sysmodule)
{
    PtcRuntimeConfig config;
    PtcRules rules;
    PtcRuntimeState state;
    bool config_ok;
    bool rules_ok;
    bool state_ok;
    if (!sysmodule) return false;
    config_ok = load_config(sysmodule, &config);
    (void)load_capabilities(sysmodule);
    rules_ok = load_rules(sysmodule, &rules);
    state_ok = load_state(sysmodule, &state);
    return config_ok && rules_ok && state_ok;
}

static bool ledger_nonce_used(PtcSysmodule *sysmodule, uint16_t day_index, uint32_t nonce, unsigned int token_version)
{
    char path[320];
    char text[4096];
    char needle[96];
    const char *line;
    join_path(path, sizeof(path), sysmodule->app_root, "ledger/used_nonces.jsonl");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        return false;
    }
    snprintf(needle, sizeof(needle), "\"day_index\":%u,\"nonce\":%lu", day_index, (unsigned long)nonce);
    line = text;
    while (line && *line) {
        const char *end = strchr(line, '\n');
        const char *match = strstr(line, needle);
        if (match && (!end || match < end)) {
            const char *version = strstr(line, "\"token_version\":");
            if ((!version || (end && version >= end)) && token_version == 1u) return true;
            if (version && (!end || version < end) && strtoul(version + strlen("\"token_version\":"), NULL, 10) == token_version) return true;
        }
        line = end ? end + 1 : NULL;
    }
    return false;
}

static bool nonce_used_v1(uint16_t day_index, uint32_t nonce, void *ctx)
{
    return ledger_nonce_used((PtcSysmodule *)ctx, day_index, nonce, 1u);
}

static bool nonce_used_v2(uint16_t day_index, uint32_t nonce, void *ctx)
{
    return ledger_nonce_used((PtcSysmodule *)ctx, day_index, nonce, 2u);
}

static bool consume_nonce(PtcSysmodule *sysmodule, const PtcRequest *request, uint16_t day_index, uint32_t nonce, unsigned int token_version)
{
    char path[320];
    char line[128];
    bool ok;
    join_path(path, sizeof(path), sysmodule->app_root, "ledger/used_nonces.jsonl");
    if (token_version == 2u) {
        snprintf(line, sizeof(line), "{\"day_index\":%u,\"nonce\":%lu,\"token_version\":2}", day_index, (unsigned long)nonce);
    } else {
        snprintf(line, sizeof(line), "{\"day_index\":%u,\"nonce\":%lu}", day_index, (unsigned long)nonce);
    }
    ok = sysmodule->storage->vtable->append_line(sysmodule->storage, path, line);
    append_event(sysmodule, request, ok ? "nonce_consumed" : "nonce_failed", ok ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED, "");
    return ok;
}

static int64_t result_remaining_minutes(const PtcPctlStatus *status)
{
    return status->remaining_available ? (int64_t)status->remaining_minutes : -1;
}

static int64_t result_played_minutes(const PtcPctlStatus *status)
{
    if (status->played_minutes_available) {
        return (int64_t)status->played_minutes;
    }
    if (!status->limited_today || !status->configured_minutes_available || !status->remaining_available) {
        return -1;
    }
    return status->configured_minutes > status->remaining_minutes
        ? (int64_t)(status->configured_minutes - status->remaining_minutes)
        : 0;
}

static int result_observed_bool(bool available, bool value)
{
    return available ? (value ? 1 : 0) : -1;
}

static void result_state_from_pctl(
    PtcResultState *state,
    uint16_t day_index,
    const PtcPctlStatus *status,
    const PtcCapabilities *caps)
{
    ptc_result_state_default(state, day_index);
    state->restriction_enabled_available = status->restriction_enabled_available;
    state->restriction_enabled = status->restriction_enabled;
    state->temporary_unlocked_available = status->temporary_unlocked_available;
    state->temporary_unlocked = status->temporary_unlocked;
    state->limited_today = status->limited_today ? 1 : 0;
    state->blocked_today = status->blocked_today ? 1 : 0;
    state->unrestricted_today = status->unrestricted_today ? 1 : 0;
    state->remaining_available = status->remaining_available;
    state->remaining_minutes = result_remaining_minutes(status);
    state->played_minutes = result_played_minutes(status);
    state->played_minutes_available = state->played_minutes >= 0;
    state->play_timer_enabled = result_observed_bool(
        status->play_timer_enabled_available, status->play_timer_enabled);
    state->restricted_now = result_observed_bool(
        status->restricted_now_available, status->restricted_now);
    (void)caps;
}

static void result_state_default_with_caps(PtcResultState *state, uint16_t day_index, const PtcCapabilities *caps)
{
    ptc_result_state_default(state, day_index);
    (void)caps;
}

static bool write_result(PtcSysmodule *sysmodule, const char *request_id, const char *json)
{
    char path[320];
    if (!ptc_request_id_is_valid(request_id)) return false;
    snprintf(path, sizeof(path), "%s/results/%s.json", sysmodule->app_root, request_id);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, json);
}

static bool write_result_with_setup(
    PtcSysmodule *sysmodule,
    const char *request_id,
    const char *base,
    bool commits_recovery)
{
    PtcSetupState setup;
    PtcRuntimeState runtime_state;
    char json[6144];
    char path[320];
    char text[1024];
    char disable_reason[48] = "";
    char hos[32] = "";
    char model[32] = "";
    bool atmosphere = false;
    bool environment_available = false;
    bool recovery_active;
    char recent[4096] = "";
    char recent_json[4096] = "";
    const char *completed_at;
    if (!load_setup_state(sysmodule, &setup) || !load_state(sysmodule, &runtime_state)) return false;
    join_path(path, sizeof(path), sysmodule->app_root, "flags/disable.flag");
    if (sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        size_t length = strcspn(text, "\r\n");
        if (length >= sizeof(disable_reason)) length = sizeof(disable_reason) - 1;
        memcpy(disable_reason, text, length);
        disable_reason[length] = '\0';
    }
    join_path(path, sizeof(path), sysmodule->app_root, "environment.json");
    if (sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        (void)json_bool_value(text, "read_ok", &environment_available);
        (void)json_string(text, "hos", hos, sizeof(hos));
        (void)json_string(text, "model", model, sizeof(model));
        (void)json_bool_value(text, "atmosphere", &atmosphere);
    }
    join_path(path, sizeof(path), sysmodule->app_root, "recovery/active/meta.json");
    recovery_active = sysmodule->storage->vtable->exists(sysmodule->storage, path);
    if (recovery_active && commits_recovery) {
        /* The result is the commit record for this transaction. Keep the
           recovery files until that record is durable, but do not expose the
           about-to-be-cleared transaction as pending work to the client. */
        recovery_active = false;
    }
    join_path(path, sizeof(path), sysmodule->app_root, "support/recent-events.jsonl");
    if (sysmodule->storage->vtable->read_text(sysmodule->storage, path, recent, sizeof(recent))) {
        char *cursor = recent;
        bool first = true;
        snprintf(recent_json, sizeof(recent_json), "[");
        while (*cursor) {
            char *newline = strchr(cursor, '\n');
            if (newline) *newline = '\0';
            if (*cursor) {
                strncat(recent_json, first ? "" : ",", sizeof(recent_json) - strlen(recent_json) - 1);
                strncat(recent_json, cursor, sizeof(recent_json) - strlen(recent_json) - 1);
                first = false;
            }
            if (!newline) break;
            cursor = newline + 1;
        }
        strncat(recent_json, "]", sizeof(recent_json) - strlen(recent_json) - 1);
    } else snprintf(recent_json, sizeof(recent_json), "[]");
    completed_at = strstr(base, "\"completed_at\"");
    if (!completed_at) return false;
    snprintf(json, sizeof(json),
        "%.*s\"setup\":{\"phase\":\"%s\",\"compatibility_status\":\"%s\",\"restriction_cleared\":%s,"
        "\"snapshot_available\":%s,\"activate_after\":%lld,\"last_error\":\"%s\","
        "\"apply_status\":\"%s\",\"apply_pending_confirmation\":%s,\"recovery_active\":%s,"
        "\"disable_reason\":\"%s\"},"
        "\"environment\":{\"available\":%s,\"hos\":\"%s\",\"model\":\"%s\",\"atmosphere\":%s},"
        "\"recent_events\":%s,%s",
        (int)(completed_at - base), base,
        setup.phase,
        setup.compatibility_status,
        setup.restriction_cleared ? "true" : "false",
        setup.snapshot_available ? "true" : "false",
        (long long)setup.activate_after,
        setup.last_error,
        runtime_state.apply_pending_confirmation ? "applied_pending_confirmation" : "idle",
        runtime_state.apply_pending_confirmation ? "true" : "false",
        recovery_active ? "true" : "false",
        disable_reason,
        environment_available ? "true" : "false",
        hos,
        model,
        atmosphere ? "true" : "false",
        recent_json,
        completed_at);
    return write_result(sysmodule, request_id, json);
}

static bool request_file_path(char *out, size_t out_size, const PtcSysmodule *sysmodule, const char *queue, const char *name)
{
    int written = snprintf(out, out_size, "%s/inbox/%s/%.127s", sysmodule->app_root, queue, name);
    return written >= 0 && (size_t)written < out_size;
}

static PtcErrorCode backup_before_write(PtcSysmodule *sysmodule, const PtcRequest *request, const char *mode)
{
    char path[320];
    PtcPctlBackup backup;
    PtcPctlDebugSnapshot snapshot;
    uint32_t ipc_result;
    PtcErrorCode err = sysmodule->pctl->vtable->backup(sysmodule->pctl, &backup);
    ipc_result = last_pctl_ipc_result(sysmodule);
    if (err != PTC_ERR_OK) {
        append_event(sysmodule, request, "pctl_backup_failed", err, "");
        take_pctl_debug_snapshot(sysmodule, &snapshot);
        append_pctl_debug(sysmodule, request, "backup", mode, NULL, err, ipc_result, NULL, &snapshot);
        return err;
    }
    snprintf(path, sizeof(path), "%s/backups/last_pctl_backup.txt", sysmodule->app_root);
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, backup.text)) {
        append_event(sysmodule, request, "pctl_backup_failed", PTC_ERR_PCTL_BACKUP_FAILED, "storage");
        take_pctl_debug_snapshot(sysmodule, &snapshot);
        append_pctl_debug(sysmodule, request, "backup", mode, NULL, PTC_ERR_PCTL_BACKUP_FAILED, ipc_result, NULL, &snapshot);
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    append_event(sysmodule, request, "pctl_backup", PTC_ERR_OK, "");
    take_pctl_debug_snapshot(sysmodule, &snapshot);
    append_pctl_debug(sysmodule, request, "backup", mode, NULL, PTC_ERR_OK, ipc_result, NULL, &snapshot);
    return PTC_ERR_OK;
}

static PtcErrorCode apply_target(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const PtcCapabilities *caps,
    PtcClockSnapshot now,
    const char *mode_name,
    PtcPctlTargetMode mode,
    uint16_t minutes)
{
    PtcPctlTarget target;
    PtcPctlDebugSnapshot before;
    PtcPctlDebugSnapshot after;
    PtcErrorCode err;
    (void)caps;
    if (!recovery_begin(sysmodule, request, now)) {
        append_event(sysmodule, request, "pctl_backup_failed", PTC_ERR_PCTL_BACKUP_FAILED, "recovery_transaction");
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    err = backup_before_write(sysmodule, request, mode_name);
    if (err != PTC_ERR_OK) {
        recovery_clear(sysmodule);
        return err;
    }
    target.mode = mode;
    target.minutes = minutes;
    target.weekday = ptc_weekday_from_day_index(now.day_index);
    take_pctl_debug_snapshot(sysmodule, &before);
    err = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &target);
    {
        uint32_t ipc_result = last_pctl_ipc_result(sysmodule);
        take_pctl_debug_snapshot(sysmodule, &after);
        append_pctl_debug(sysmodule, request, "apply_target", mode_name, &target, err, ipc_result, &before, &after);
    }
    append_event(sysmodule, request, err == PTC_ERR_OK ? "pctl_apply" : "pctl_apply_failed", err, pctl_target_mode_name(mode));
    if (err != PTC_ERR_OK && !recovery_rollback(sysmodule)) {
        write_disable_flag(sysmodule, "transaction_restore_failed\n");
        return PTC_ERR_RECOVERY_FAILED;
    }
    return err;
}

static bool request_file_stem(const char *name, char *out, size_t out_size)
{
    size_t len;
    if (!name || !out) return false;
    len = strlen(name);
    if (len <= 5 || strcmp(name + len - 5, ".json") != 0 || len - 5 >= out_size) return false;
    memcpy(out, name, len - 5);
    out[len - 5] = '\0';
    return ptc_request_id_is_valid(out);
}

/* An offline grant is successful only after Horizon reports that the timer is
   running and the active restriction is cleared. The target is absolute, so a
   retry after any failure safely re-applies the same requested total. */
static PtcErrorCode start_timer_and_wait_unrestricted(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now,
    const char *mode_name,
    uint16_t minutes,
    PtcPctlStatus *observed)
{
    PtcPctlTarget target;
    PtcPctlDebugSnapshot before;
    PtcPctlDebugSnapshot after;
    PtcErrorCode err;
    unsigned int i;
    memset(observed, 0, sizeof(*observed));
    target.mode = PTC_PCTL_TARGET_LIMIT;
    target.minutes = minutes;
    target.weekday = ptc_weekday_from_day_index(now.day_index);
    take_pctl_debug_snapshot(sysmodule, &before);
    err = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
    take_pctl_debug_snapshot(sysmodule, &after);
    append_pctl_debug(sysmodule, request, "start_timer", mode_name, &target, err,
        last_pctl_ipc_result(sysmodule), &before, &after);
    append_event(sysmodule, request, err == PTC_ERR_OK ? "pctl_start_timer" : "pctl_apply_failed", err, "start_timer");
    if (err != PTC_ERR_OK) {
        return err;
    }
    for (i = 0; i < 20U; ++i) {
        err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, target.weekday, observed);
        if (err == PTC_ERR_OK && observed->play_timer_enabled && !observed->restricted_now) {
            append_event(sysmodule, request, "effect_observed", PTC_ERR_OK, "offline_code");
            return PTC_ERR_OK;
        }
        if (i + 1U < 20U) {
            effect_wait(sysmodule, 250U);
        }
    }
    append_event(sysmodule, request, "pctl_apply_failed", PTC_ERR_PCTL_EFFECT_NOT_OBSERVED, "offline_code");
    return PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
}

#ifdef PLAYWISE_DEVICE_LAB
static void status_json(char *out, size_t out_size, const PtcPctlStatus *status, PtcErrorCode error)
{
    snprintf(
        out,
        out_size,
        "{\"available\":%s,\"error\":\"%s\",\"limited_today\":%d,\"blocked_today\":%d,"
        "\"unrestricted_today\":%d,\"remaining_available\":%s,\"remaining_minutes\":%lld,"
        "\"play_timer_enabled\":%d,\"restricted_now\":%d}",
        error == PTC_ERR_OK ? "true" : "false",
        ptc_error_reason(error),
        error == PTC_ERR_OK && status->limited_today ? 1 : 0,
        error == PTC_ERR_OK && status->blocked_today ? 1 : 0,
        error == PTC_ERR_OK && status->unrestricted_today ? 1 : 0,
        error == PTC_ERR_OK && status->remaining_available ? "true" : "false",
        error == PTC_ERR_OK && status->remaining_available ? (long long)status->remaining_minutes : -1LL,
        error == PTC_ERR_OK
            ? result_observed_bool(status->play_timer_enabled_available, status->play_timer_enabled)
            : -1,
        error == PTC_ERR_OK
            ? result_observed_bool(status->restricted_now_available, status->restricted_now)
            : -1);
}

static const char *probe_apply_hint(PtcErrorCode error, bool raw_changed, bool start_timer_requested)
{
    if (error == PTC_ERR_PCTL_INIT_FAILED) {
        return "suspect_pctl_s_session_init_permission";
    }
    if (error == PTC_ERR_PCTL_WRITE_FAILED) {
        return start_timer_requested
            ? "suspect_command_id_write_permission_parameter_shape_or_start_timer"
            : "suspect_command_id_write_permission_or_parameter_shape";
    }
    if (error == PTC_ERR_PCTL_READ_FAILED) {
        return "suspect_readback_or_status_session";
    }
    if (error == PTC_ERR_OK && raw_changed) {
        return "manual_confirm_official_page_if_unchanged_suspect_raw_layout_or_missing_commit_apply_start";
    }
    if (error == PTC_ERR_OK) {
        return "manual_confirm_official_page_or_play_timer_counting";
    }
    return "check_pctl_debug_stage_error";
}

static bool write_probe_apply_result(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *mode,
    bool dry_run,
    PtcErrorCode error,
    const PtcCapabilities *caps,
    PtcClockSnapshot now,
    const PtcPctlTarget *target,
    const PtcPctlStatus *before_status,
    PtcErrorCode before_status_error,
    const PtcPctlStatus *after_status,
    PtcErrorCode after_status_error,
    const PtcPctlDebugSnapshot *before_snapshot,
    const PtcPctlDebugSnapshot *after_snapshot,
    bool start_timer_requested,
    bool start_timer_called,
    PtcErrorCode start_timer_error,
    uint32_t write_ipc_result,
    uint32_t start_timer_ipc_result)
{
    PtcResultState state;
    char base[2048];
    char json[6144];
    char extra[4096];
    char before_status_text[384];
    char after_status_text[384];
    char before_raw[160];
    char before_slots[320];
    char after_raw[160];
    char after_slots[320];
    char *completed_at;
    bool raw_changed = before_snapshot && after_snapshot &&
        before_snapshot->available && after_snapshot->available &&
        strcmp(before_snapshot->raw_hex, after_snapshot->raw_hex) != 0;

    result_state_default_with_caps(&state, now.day_index, caps);
    if (after_status_error == PTC_ERR_OK) {
        state.limited_today = after_status->limited_today ? 1 : 0;
        state.blocked_today = after_status->blocked_today ? 1 : 0;
        state.unrestricted_today = after_status->unrestricted_today ? 1 : 0;
        state.remaining_available = after_status->remaining_available;
        state.remaining_minutes = result_remaining_minutes(after_status);
        state.play_timer_enabled = result_observed_bool(
            after_status->play_timer_enabled_available, after_status->play_timer_enabled);
        state.restricted_now = result_observed_bool(
            after_status->restricted_now_available, after_status->restricted_now);
    }

    if (error == PTC_ERR_OK) {
        (void)ptc_result_ok_json(base, sizeof(base), request->request_id, request->type_text, mode, dry_run, &state, now.unix_seconds);
    } else {
        (void)ptc_result_error_json(base, sizeof(base), request->request_id, request->type_text, mode, dry_run, error, &state, now.unix_seconds);
    }

    status_json(before_status_text, sizeof(before_status_text), before_status, before_status_error);
    status_json(after_status_text, sizeof(after_status_text), after_status, after_status_error);
    json_safe_copy(before_raw, sizeof(before_raw), before_snapshot ? before_snapshot->raw_hex : "");
    json_safe_copy(before_slots, sizeof(before_slots), before_snapshot ? before_snapshot->decoded_slots : "");
    json_safe_copy(after_raw, sizeof(after_raw), after_snapshot ? after_snapshot->raw_hex : "");
    json_safe_copy(after_slots, sizeof(after_slots), after_snapshot ? after_snapshot->decoded_slots : "");
    snprintf(
        extra,
        sizeof(extra),
        "{\"target_weekday\":%u,\"target_minutes\":%u,\"target_mode\":\"%s\","
        "\"start_timer_requested\":%s,\"start_timer_called\":%s,"
        "\"pctl_error\":\"%s\",\"pctl_error_code\":%d,"
        "\"write_ipc_result\":\"0x%08x\",\"start_timer_error\":\"%s\","
        "\"start_timer_ipc_result\":\"0x%08x\",\"before_status\":%s,\"after_status\":%s,"
        "\"debug_evidence\":{\"raw_changed\":%s,\"before_raw_hex\":\"%s\","
        "\"after_raw_hex\":\"%s\",\"before_slots\":\"%s\",\"after_slots\":\"%s\","
        "\"hint\":\"%s\"}}",
        (unsigned int)target->weekday,
        (unsigned int)target->minutes,
        pctl_target_mode_name(target->mode),
        start_timer_requested ? "true" : "false",
        start_timer_called ? "true" : "false",
        ptc_error_reason(error),
        (int)error,
        (unsigned int)write_ipc_result,
        ptc_error_reason(start_timer_error),
        (unsigned int)start_timer_ipc_result,
        before_status_text,
        after_status_text,
        raw_changed ? "true" : "false",
        before_raw,
        after_raw,
        before_slots,
        after_slots,
        probe_apply_hint(error, raw_changed, start_timer_requested));
    completed_at = strstr(base, ",\"completed_at\"");
    if (!completed_at) {
        return false;
    }
    snprintf(json, sizeof(json), "%.*s,\"probe_apply\":%s%s", (int)(completed_at - base), base, extra, completed_at);
    return write_result(sysmodule, request->request_id, json);
}

#endif

static bool finish_with_error(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *mode,
    bool dry_run,
    PtcErrorCode error,
    uint16_t day_index,
    const PtcCapabilities *caps)
{
    char json[2048];
    PtcResultState state;
    if (recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule)) {
        write_disable_flag(sysmodule, "transaction_restore_failed\n");
        error = PTC_ERR_RECOVERY_FAILED;
    }
    result_state_default_with_caps(&state, day_index, caps);
    (void)ptc_result_error_json(
        json,
        sizeof(json),
        request ? request->request_id : "unknown",
        request ? request->type_text : "unknown",
        mode,
        dry_run,
        error,
        &state,
        sysmodule->time_provider->vtable->now(sysmodule->time_provider).unix_seconds);
    append_event(sysmodule, request, "result_error", error, "");
    return write_result(sysmodule, request ? request->request_id : "unknown", json);
}

static PtcOperation request_operation(PtcRequestType type)
{
    switch (type) {
    case PTC_REQUEST_SET_TODAY_LIMIT:
    case PTC_REQUEST_ADD_TODAY_MINUTES:
        return PTC_OPERATION_SET_TODAY_LIMIT;
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
        return PTC_OPERATION_SET_TODAY_LIMIT;
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
        return PTC_OPERATION_DISABLE_TODAY_LIMIT;
    case PTC_REQUEST_OFFLINE_CODE:
    case PTC_REQUEST_PREVIEW_OFFLINE_CODE:
        return PTC_OPERATION_GRANT_MINUTES;
    case PTC_REQUEST_STATUS:
        return PTC_OPERATION_STATUS;
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
    case PTC_REQUEST_SET_HOLIDAY_POLICY:
        return PTC_OPERATION_RULE_UPDATE;
    default:
        return PTC_OPERATION_STATUS;
    }
}

static PtcPctlTargetMode target_from_day_rule(PtcDayRule rule)
{
    if (rule.mode == PTC_RULE_MODE_UNLIMITED) {
        return PTC_PCTL_TARGET_UNLIMITED;
    }
    return PTC_PCTL_TARGET_LIMIT;
}

static uint16_t ptc_pctl_played_minutes(const PtcPctlStatus *status)
{
    if (!status) {
        return 0U;
    }
    if (status->played_minutes_available) {
        return status->played_minutes > UINT16_MAX ? UINT16_MAX : (uint16_t)status->played_minutes;
    }
    if (!status->configured_minutes_available || !status->limited_today) {
        return 0U;
    }
    if (status->remaining_available && status->configured_minutes >= status->remaining_minutes) {
        uint32_t played = (uint32_t)status->configured_minutes - status->remaining_minutes;
        if (status->restricted_now && played < status->configured_minutes) {
            return status->configured_minutes;
        }
        return (uint16_t)played;
    }
    if (status->restricted_now || status->remaining_minutes == 0U) {
        return status->configured_minutes;
    }
    return 0U;
}

/* Offline codes and add_today_minutes both stack onto the existing daily limit
   instead of overwriting it. Base is max(today's effective LIMIT minutes, played_minutes);
   unlimited or blocked days start from played_minutes (or 0 if played is 0). The total is
   clamped to the single-day maximum so even a large accumulation still writes successfully to PCTL.
   Sets today_override in place and returns the resulting minutes. */
static uint16_t accumulate_today_limit(PtcRules *rules, uint16_t day_index, uint8_t weekday, uint16_t add_minutes, uint16_t played_minutes)
{
    PtcDayRule active = ptc_rules_today_rule(rules, day_index, weekday);
    uint32_t base = (active.mode == PTC_RULE_MODE_LIMIT) ? active.minutes : 0u;
    if (played_minutes > base) {
        base = played_minutes;
    }
    uint32_t total = base + add_minutes;
    if (total > PTC_TOKEN_MAX_MINUTES) {
        total = PTC_TOKEN_MAX_MINUTES;
    }
    rules->today_override.present = true;
    rules->today_override.day_index = day_index;
    rules->today_override.rule.mode = PTC_RULE_MODE_LIMIT;
    rules->today_override.rule.minutes = (uint16_t)total;
    return (uint16_t)total;
}

static PtcErrorCode update_rules_for_request(PtcSysmodule *sysmodule, const PtcRequest *request, PtcRules *rules, PtcRuntimeState *runtime_state, PtcClockSnapshot now, uint16_t played_minutes)
{
    (void)runtime_state;
    switch (request->type) {
    case PTC_REQUEST_SET_TODAY_LIMIT:
        rules->today_override.present = true;
        rules->today_override.day_index = now.day_index;
        rules->today_override.rule.mode = PTC_RULE_MODE_LIMIT;
        rules->today_override.rule.minutes = request->minutes;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_ADD_TODAY_MINUTES:
        (void)accumulate_today_limit(rules, now.day_index, ptc_weekday_from_day_index(now.day_index), request->minutes, played_minutes);
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
        rules->today_override.present = true;
        rules->today_override.day_index = now.day_index;
        rules->today_override.rule.mode = PTC_RULE_MODE_UNLIMITED;
        rules->today_override.rule.minutes = 0;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
        rules->today_override.present = false;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
        memcpy(rules->week, request->week, sizeof(rules->week));
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_SET_HOLIDAY_POLICY:
        rules->holiday_enabled = request->holiday_enabled;
        rules->holiday_rule = request->holiday_rule;
        rules->makeup_workday_rule = request->makeup_workday_rule;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    default:
        return PTC_ERR_OK;
    }
}

static bool write_current_status_result(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *mode,
    bool dry_run,
    const PtcCapabilities *caps,
    PtcClockSnapshot now,
    bool commits_recovery)
{
    PtcPctlStatus pctl_status;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcResultState state;
    char json[2048];
    PtcErrorCode err;
    PtcEffectiveRule effective;
    const PtcHolidayCalendarInfo *calendar_info;
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    if (!load_rules(sysmodule, &rules)) {
        return finish_with_error(sysmodule, request, mode, dry_run, PTC_ERR_RULES_INVALID, now.day_index, caps);
    }
    if (!load_state(sysmodule, &runtime_state)) {
        return finish_with_error(sysmodule, request, mode, dry_run, PTC_ERR_BAD_REQUEST, now.day_index, caps);
    }
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, mode, dry_run, err, now.day_index, caps);
    }
    result_state_from_pctl(&state, now.day_index, &pctl_status, caps);
    effective = ptc_rules_resolve(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    state.rule_source = ptc_rule_source_name(effective.source);
    state.calendar_covered = effective.calendar_covered;
    calendar_info = ptc_holiday_calendar_info();
    if (ptc_date_from_day_index(now.day_index, &year, &month, &day)) {
        state.calendar_update_warning = rules.holiday_enabled &&
            (year > calendar_info->last_year ||
                (year == calendar_info->last_year && month == 12 && day >= 2));
    }
    (void)ptc_result_ok_json(json, sizeof(json), request->request_id, request->type_text, mode, dry_run, &state, now.unix_seconds);
    append_event(sysmodule, request, "result_ok", PTC_ERR_OK, "");
    return write_result_with_setup(sysmodule, request->request_id, json, commits_recovery);
}

static bool process_status(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, const PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcPolicyDecision decision = ptc_policy_decide(config->mode, disable_flag, PTC_OPERATION_STATUS, caps, false, config->allow_unlimited_to_limited);
    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, decision.error, now.day_index, caps);
    }
    return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, caps, now, false);
}

static bool code_is_v2_candidate(const char *code)
{
    size_t length;
    size_t index;
    bool all_digits = true;
    if (!code) return false;
    length = strlen(code);
    if (strchr(code, '-') != NULL) return false;
    for (index = 0; index < length; ++index) {
        if (code[index] < '0' || code[index] > '9') all_digits = false;
    }
    /* Exactly eight characters is an intended short-code entry even when a
       non-digit typo makes it malformed. Other all-digit lengths are also
       treated as malformed v2, except 16 symbols which is a valid v1 shape. */
    return length == PTC_TOKEN_V2_TEXT_LEN || (length != 0u && length != PTC_TOKEN_SYMBOLS && all_digits);
}

typedef struct {
    uint16_t day_index;
    uint32_t nonce;
    uint16_t minutes;
    unsigned int version;
    bool is_v2;
} PtcVerifiedOfflineCode;

static PtcErrorCode verify_offline_code(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcRuntimeConfig *config,
    PtcClockSnapshot now,
    PtcRuntimeState *runtime_state,
    PtcVerifiedOfflineCode *verified)
{
    PtcTokenPayload token_v1;
    PtcTokenV2Payload token_v2;
    PtcErrorCode err;
    bool is_v2;
    if (!sysmodule || !request || !config || !runtime_state || !verified) return PTC_ERR_BAD_REQUEST;
    memset(verified, 0, sizeof(*verified));
    verified->day_index = now.day_index;
    verified->version = 1u;
    is_v2 = code_is_v2_candidate(request->code);
    verified->is_v2 = is_v2;
    if (is_v2) {
        if (!load_state(sysmodule, runtime_state)) return PTC_ERR_STORAGE_READ_FAILED;
        if (runtime_state->v2_cooldown_until > now.unix_seconds) return PTC_ERR_CODE_COOLDOWN;
        if (runtime_state->v2_cooldown_until != 0) {
            runtime_state->v2_failed_attempts = 0;
            runtime_state->v2_cooldown_until = 0;
        }
        err = ptc_token_v2_verify(request->code, config->device_id, config->grant_secret, now.day_index,
            config->max_add_minutes, nonce_used_v2, sysmodule, &token_v2);
        if (err == PTC_ERR_BAD_SIGNATURE) {
            sysmodule->config_cache_valid = false;
            if (load_config(sysmodule, config)) {
                err = ptc_token_v2_verify(request->code, config->device_id, config->grant_secret, now.day_index,
                    config->max_add_minutes, nonce_used_v2, sysmodule, &token_v2);
            }
        }
        if (err == PTC_ERR_BAD_CODE || err == PTC_ERR_BAD_SIGNATURE) {
            if (runtime_state->v2_failed_attempts < PTC_V2_FAILURE_LIMIT) ++runtime_state->v2_failed_attempts;
            if (runtime_state->v2_failed_attempts >= PTC_V2_FAILURE_LIMIT) {
                runtime_state->v2_cooldown_until = now.unix_seconds + PTC_V2_COOLDOWN_SECONDS;
            }
            if (!save_state(sysmodule, runtime_state, now.unix_seconds)) return PTC_ERR_STORAGE_WRITE_FAILED;
        }
        if (err == PTC_ERR_OK) {
            verified->minutes = token_v2.minutes;
            verified->nonce = token_v2.nonce;
            verified->version = 2u;
        }
        return err;
    }
    err = ptc_token_verify(request->code, config->device_id, config->grant_secret, now.day_index,
        config->max_add_minutes, nonce_used_v1, sysmodule, &token_v1);
    if (err == PTC_ERR_BAD_SIGNATURE) {
        sysmodule->config_cache_valid = false;
        if (load_config(sysmodule, config)) {
            err = ptc_token_verify(request->code, config->device_id, config->grant_secret, now.day_index,
                config->max_add_minutes, nonce_used_v1, sysmodule, &token_v1);
        }
    }
    if (err == PTC_ERR_OK) {
        verified->day_index = token_v1.day_index_since_2020;
        verified->minutes = token_v1.minutes;
        verified->nonce = token_v1.nonce;
    }
    return err;
}

static bool process_preview_offline_code(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const PtcRuntimeConfig *config,
    bool disable_flag,
    const PtcCapabilities *caps,
    PtcClockSnapshot now)
{
    PtcRuntimeConfig active_config = *config;
    PtcRuntimeState runtime_state;
    PtcVerifiedOfflineCode verified;
    PtcPolicyDecision decision;
    PtcPctlStatus pctl_status;
    PtcRules rules;
    PtcRules preview_rules;
    PtcResultState state;
    PtcOfflineCodePreview preview;
    PtcErrorCode err;
    int64_t played_minutes;
    uint16_t played_for_apply;
    uint16_t base_minutes;
    uint16_t target_minutes;
    char json[2304];

    decision = ptc_policy_decide(active_config.mode, disable_flag, PTC_OPERATION_GRANT_MINUTES,
        caps, false, active_config.allow_unlimited_to_limited);
    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), true,
            decision.error, now.day_index, caps);
    }
    err = verify_offline_code(sysmodule, request, &active_config, now, &runtime_state, &verified);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), true,
            err, now.day_index, caps);
    }
    err = sysmodule->pctl->vtable->read_status(
        sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), true,
            err, now.day_index, caps);
    }
    decision = ptc_policy_decide(active_config.mode, disable_flag, PTC_OPERATION_GRANT_MINUTES,
        caps, pctl_status.unrestricted_today, active_config.allow_unlimited_to_limited);
    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), true,
            decision.error, now.day_index, caps);
    }
    if (!load_rules(sysmodule, &rules)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), true,
            PTC_ERR_RULES_INVALID, now.day_index, caps);
    }
    preview_rules = rules;
    played_for_apply = ptc_pctl_played_minutes(&pctl_status);
    {
        PtcDayRule active = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
        base_minutes = active.mode == PTC_RULE_MODE_LIMIT ? active.minutes : 0u;
        if (played_for_apply > base_minutes) base_minutes = played_for_apply;
    }
    target_minutes = accumulate_today_limit(&preview_rules, now.day_index,
        ptc_weekday_from_day_index(now.day_index), verified.minutes, played_for_apply);
    memset(&preview, 0, sizeof(preview));
    preview.grant_minutes = verified.minutes;
    preview.effective_add_minutes = target_minutes >= base_minutes ? (uint16_t)(target_minutes - base_minutes) : 0u;
    preview.capped = preview.effective_add_minutes < verified.minutes;
    preview.converts_unlimited_to_limited = pctl_status.unrestricted_today;
    played_minutes = result_played_minutes(&pctl_status);
    preview.remaining_after_available = played_minutes >= 0;
    preview.remaining_after_minutes = preview.remaining_after_available
        ? ((int64_t)target_minutes > played_minutes ? (int64_t)target_minutes - played_minutes : 0)
        : -1;
    result_state_from_pctl(&state, now.day_index, &pctl_status, caps);
    if (ptc_result_preview_ok_json(json, sizeof(json), request->request_id, request->type_text,
            &state, &preview, now.unix_seconds) != 0) {
        append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "preview_json");
        return false;
    }
    if (!write_result(sysmodule, request->request_id, json)) {
        append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "preview");
        return false;
    }
    append_event(sysmodule, request, "result_ok", PTC_ERR_OK, "preview_offline_code");
    return true;
}

static bool process_offline_code(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, const PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcPctlStatus pctl_status;
    PtcPolicyDecision decision;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcResultState state;
    char json[2048];
    PtcErrorCode err;
    PtcRuntimeConfig active_config;
    PtcVerifiedOfflineCode verified;

    if (config) {
        active_config = *config;
    } else {
        memset(&active_config, 0, sizeof(active_config));
    }

    decision = ptc_policy_decide(active_config.mode, disable_flag, PTC_OPERATION_GRANT_MINUTES, caps, false, active_config.allow_unlimited_to_limited);
    if (decision.error == PTC_ERR_DISABLED) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    err = verify_offline_code(sysmodule, request, &active_config, now, &runtime_state, &verified);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), true, err, now.day_index, caps);
    }
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), true, err, now.day_index, caps);
    }
    decision = ptc_policy_decide(active_config.mode, disable_flag, PTC_OPERATION_GRANT_MINUTES, caps, pctl_status.unrestricted_today, active_config.allow_unlimited_to_limited);
    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    if (decision.may_write_pctl) {
        uint16_t new_minutes;
        uint16_t played_minutes = ptc_pctl_played_minutes(&pctl_status);
        (void)load_rules(sysmodule, &rules);
        /* Stack the granted minutes onto today's existing limit or played time rather than
           overwriting it, matching the add_today_minutes token action. */
        new_minutes = accumulate_today_limit(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index), verified.minutes, played_minutes);
        /* Apply to PCTL first (idempotent absolute write); persist the override
           only after it succeeds. On failure the nonce is not consumed and the
           same code may be re-entered, so persisting first would double-count. */
        err = apply_target(sysmodule, request, caps, now, ptc_control_mode_name(active_config.mode), PTC_PCTL_TARGET_LIMIT, new_minutes);
        if (err != PTC_ERR_OK) {
            return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), false, err, now.day_index, caps);
        }
        err = start_timer_and_wait_unrestricted(sysmodule, request, now, ptc_control_mode_name(active_config.mode), new_minutes, &pctl_status);
        if (err != PTC_ERR_OK) {
            if (!recovery_rollback(sysmodule)) {
                write_disable_flag(sysmodule, "transaction_restore_failed\n");
                err = PTC_ERR_RECOVERY_FAILED;
            }
            return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), false, err, now.day_index, caps);
        }
        if (!save_rules(sysmodule, &rules)) {
            append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "rules");
            err = PTC_ERR_STORAGE_WRITE_FAILED;
            if (!recovery_rollback(sysmodule)) {
                write_disable_flag(sysmodule, "transaction_restore_failed\n");
                err = PTC_ERR_RECOVERY_FAILED;
            }
            return finish_with_error(sysmodule, request, ptc_control_mode_name(active_config.mode), false, err, now.day_index, caps);
        }
        append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "offline_code");
    }
    (void)load_rules(sysmodule, &rules);
    (void)load_state(sysmodule, &runtime_state);
    (void)sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    result_state_from_pctl(&state, now.day_index, &pctl_status, caps);
    (void)ptc_result_ok_json(json, sizeof(json), request->request_id, request->type_text, ptc_control_mode_name(active_config.mode), decision.dry_run, &state, now.unix_seconds);
    if (write_result(sysmodule, request->request_id, json)) {
        append_event(sysmodule, request, "result_ok", PTC_ERR_OK, "");
        if (verified.is_v2 && !decision.dry_run &&
            (runtime_state.v2_failed_attempts != 0 || runtime_state.v2_cooldown_until != 0)) {
            bool cleared;
            runtime_state.v2_failed_attempts = 0;
            runtime_state.v2_cooldown_until = 0;
            cleared = save_state(sysmodule, &runtime_state, now.unix_seconds);
            append_event(sysmodule, request,
                cleared ? "v2_failures_cleared" : "result_write_failed",
                cleared ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED,
                "v2_cooldown");
        }
        if (decision.consume_nonce_after_success &&
            !consume_nonce(sysmodule, request, verified.day_index, verified.nonce, verified.version)) {
            char result_path[320];
            snprintf(result_path, sizeof(result_path), "%s/results/%s.json", sysmodule->app_root, request->request_id);
            (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, result_path);
            err = PTC_ERR_STORAGE_WRITE_FAILED;
            if (!recovery_rollback(sysmodule)) {
                write_disable_flag(sysmodule, "transaction_restore_failed\n");
                err = PTC_ERR_RECOVERY_FAILED;
            }
            return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
                err, now.day_index, caps);
        }
        recovery_clear(sysmodule);
        return true;
    }
    append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "");
    if (recovery_path_exists(sysmodule)) {
        bool restored = recovery_rollback(sysmodule);
        append_event(sysmodule, request,
            restored ? "state_rollback_ok" : "state_rollback_failed",
            restored ? PTC_ERR_OK : PTC_ERR_RECOVERY_FAILED, "offline_code");
        if (!restored) write_disable_flag(sysmodule, "transaction_restore_failed\n");
    }
    return false;
}

static bool effect_snapshot_equal(const PtcPctlSettingsSnapshot *a, const PtcPctlSettingsSnapshot *b)
{
    return a && b && a->size == b->size && a->size <= PTC_PCTL_OPAQUE_SETTINGS_SIZE &&
        memcmp(a->data, b->data, a->size) == 0;
}

#ifdef PLAYWISE_DEVICE_LAB
static void effect_snapshot_hex(char *out, size_t out_size, const PtcPctlSettingsSnapshot *snapshot)
{
    size_t used = 0;
    size_t i;
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!snapshot || snapshot->size > PTC_PCTL_OPAQUE_SETTINGS_SIZE) {
        return;
    }
    for (i = 0; i < snapshot->size && used + 3 <= out_size; ++i) {
        int written = snprintf(out + used, out_size - used, "%02x", (unsigned int)snapshot->data[i]);
        if (written < 0 || (size_t)written >= out_size - used) {
            break;
        }
        used += (size_t)written;
    }
}
#endif

static void effect_wait(PtcSysmodule *sysmodule, uint32_t milliseconds)
{
    if (sysmodule->time_provider && sysmodule->time_provider->vtable && sysmodule->time_provider->vtable->sleep_ms) {
        sysmodule->time_provider->vtable->sleep_ms(sysmodule->time_provider, milliseconds);
    }
}

#ifdef PLAYWISE_DEVICE_LAB
static void effect_status_json(char *out, size_t out_size, const PtcPctlStatus *status, PtcErrorCode error)
{
    status_json(out, out_size, status, error);
}
#endif

static bool target_status_observed(
    PtcPctlTargetMode mode,
    uint16_t minutes,
    const PtcPctlStatus *status)
{
    if (mode == PTC_PCTL_TARGET_UNLIMITED) {
        return status->unrestricted_today && status->restricted_now_available && !status->restricted_now;
    }
    if (mode == PTC_PCTL_TARGET_BLOCKED) {
        return status->blocked_today;
    }
    if (!status->limited_today || !status->configured_minutes_available ||
        status->configured_minutes != minutes || !status->remaining_available) {
        return false;
    }
    /* Command 1455 is an instantaneous "is a title restricted now" query. On
       hardware it can return false again after Horizon has shown the limit and
       suspended or exited the affected title. Exact settings readback plus an
       exhausted 1454 value therefore proves the requested target without
       requiring restricted_now to remain latched. */
    return status->restricted_now || status->remaining_minutes == 0U ||
        (status->remaining_minutes > 0U && status->play_timer_enabled_available && status->play_timer_enabled);
}

/* Daily rule synchronization only proves that the configured target was
   written. Starting private command 1451 here is unsafe: device evidence shows
   that a timer started by the sysmodule can consume 1454 while the console is
   on HOME or asleep. Horizon remains responsible for the actual play-timer
   lifecycle when an application starts, suspends, resumes, or exits. */
static bool target_settings_observed(
    PtcPctlTargetMode mode,
    uint16_t minutes,
    const PtcPctlStatus *status)
{
    if (mode == PTC_PCTL_TARGET_UNLIMITED) {
        return status->unrestricted_today;
    }
    if (mode == PTC_PCTL_TARGET_BLOCKED) {
        return status->blocked_today;
    }
    return status->limited_today && status->configured_minutes_available &&
        status->configured_minutes == minutes;
}

static PtcErrorCode start_timer_and_wait_target(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now,
    const char *mode_name,
    PtcPctlTargetMode target_mode,
    uint16_t minutes,
    const char *event_detail,
    PtcPctlStatus *observed,
    bool *timer_started)
{
    PtcPctlTarget target;
    PtcPctlDebugSnapshot before;
    PtcPctlDebugSnapshot after;
    PtcErrorCode err;
    unsigned int i;
    memset(observed, 0, sizeof(*observed));
    if (timer_started) {
        *timer_started = false;
    }
    target.mode = target_mode;
    target.minutes = minutes;
    target.weekday = ptc_weekday_from_day_index(now.day_index);
    take_pctl_debug_snapshot(sysmodule, &before);
    err = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
    take_pctl_debug_snapshot(sysmodule, &after);
    append_pctl_debug(sysmodule, request, "start_timer", mode_name, &target, err,
        last_pctl_ipc_result(sysmodule), &before, &after);
    append_event(sysmodule, request, err == PTC_ERR_OK ? "pctl_start_timer" : "pctl_apply_failed", err, event_detail);
    if (err != PTC_ERR_OK) {
        return err;
    }
    if (timer_started) {
        *timer_started = true;
    }
    for (i = 0; i < 20U; ++i) {
        err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, target.weekday, observed);
        if (err == PTC_ERR_OK && target_status_observed(target_mode, minutes, observed)) {
            append_event(sysmodule, request, "effect_observed", PTC_ERR_OK, event_detail);
            return PTC_ERR_OK;
        }
        if (i + 1U < 20U) {
            effect_wait(sysmodule, 250U);
        }
    }
    append_event(sysmodule, request, "pctl_apply_failed", PTC_ERR_PCTL_EFFECT_NOT_OBSERVED, event_detail);
    return err == PTC_ERR_OK ? PTC_ERR_PCTL_EFFECT_NOT_OBSERVED : err;
}

static PtcErrorCode restore_snapshot_exact(
    PtcSysmodule *sysmodule,
    const PtcPctlSettingsSnapshot *original,
    PtcPctlSettingsSnapshot *restored_snapshot,
    PtcPctlStatus *restored_status,
    uint8_t weekday,
    bool *raw_restored,
    bool *timer_restored)
{
    PtcErrorCode err;
    *raw_restored = false;
    *timer_restored = false;
    memset(restored_snapshot, 0, sizeof(*restored_snapshot));
    memset(restored_status, 0, sizeof(*restored_status));
    if (!sysmodule->pctl->vtable->restore_settings || !sysmodule->pctl->vtable->snapshot_settings) {
        return PTC_ERR_PCTL_RESTORE_FAILED;
    }
    err = sysmodule->pctl->vtable->restore_settings(sysmodule->pctl, original);
    if (err == PTC_ERR_OK) {
        err = original->timer_enabled
            ? sysmodule->pctl->vtable->start_timer(sysmodule->pctl)
            : sysmodule->pctl->vtable->stop_timer(sysmodule->pctl);
    }
    if (err == PTC_ERR_OK) {
        err = sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, restored_snapshot);
    }
    if (err == PTC_ERR_OK) {
        *raw_restored = effect_snapshot_equal(original, restored_snapshot);
        *timer_restored = restored_snapshot->timer_enabled == original->timer_enabled;
        if (!*raw_restored || !*timer_restored) {
            err = PTC_ERR_PCTL_RESTORE_FAILED;
        }
    }
    if (err == PTC_ERR_OK) {
        err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, weekday, restored_status);
    }
    return err;
}

/* A fresh takeover must not silently replace an in-progress Nintendo allowance
   with PlayWise's weekly defaults.  PCTL targets are daily totals, so retain the
   observed configured total when possible; only derive it from spent + remaining
   when both values are available. */
static PtcErrorCode capture_handover_today(PtcSetupState *setup, const PtcPctlStatus *status, PtcClockSnapshot now)
{
    uint32_t total;
    if (setup->handover_today_pending) {
        return PTC_ERR_OK;
    }
    setup->handover_day_index = now.day_index;
    setup->handover_unlimited = false;
    setup->handover_minutes = 0;
    setup->handover_remaining_available = false;
    setup->handover_remaining_minutes = 0;
    if (status->unrestricted_today && status->restricted_now_available && !status->restricted_now) {
        setup->handover_today_pending = true;
        setup->handover_unlimited = true;
        return PTC_ERR_OK;
    }
    if (!status->limited_today && !status->blocked_today) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }
    if (status->blocked_today) {
        setup->handover_today_pending = true;
        setup->handover_remaining_available = status->remaining_available;
        setup->handover_remaining_minutes = 0;
        return PTC_ERR_OK;
    }
    if (!status->remaining_available) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }
    if (status->configured_minutes_available && status->configured_minutes <= PTC_TOKEN_MAX_MINUTES) {
        total = status->configured_minutes;
    } else if (status->played_minutes_available &&
               status->played_minutes <= PTC_TOKEN_MAX_MINUTES &&
               status->remaining_minutes <= PTC_TOKEN_MAX_MINUTES &&
               status->played_minutes + status->remaining_minutes <= PTC_TOKEN_MAX_MINUTES) {
        total = status->played_minutes + status->remaining_minutes;
    } else {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }
    setup->handover_today_pending = true;
    setup->handover_minutes = (uint16_t)total;
    setup->handover_remaining_available = true;
    setup->handover_remaining_minutes = status->remaining_minutes > UINT16_MAX
        ? UINT16_MAX : (uint16_t)status->remaining_minutes;
    return PTC_ERR_OK;
}

static PtcErrorCode persist_handover_today_rule(
    PtcSysmodule *sysmodule,
    const PtcSetupState *setup,
    PtcClockSnapshot now)
{
    PtcRules rules;
    if (!setup->handover_today_pending || setup->handover_day_index != now.day_index) {
        return PTC_ERR_OK;
    }
    if (!load_rules(sysmodule, &rules)) {
        return PTC_ERR_RULES_INVALID;
    }
    rules.today_override.present = true;
    rules.today_override.day_index = now.day_index;
    rules.today_override.rule.mode = setup->handover_unlimited
        ? PTC_RULE_MODE_UNLIMITED : PTC_RULE_MODE_LIMIT;
    rules.today_override.rule.minutes = setup->handover_unlimited ? 0U : setup->handover_minutes;
    return save_rules(sysmodule, &rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
}

static PtcErrorCode run_release_preflight(
    PtcSysmodule *sysmodule,
    const PtcRuntimeConfig *config,
    PtcSetupState *setup,
    PtcClockSnapshot now,
    bool capture_handover)
{
    char path[320];
    char text[4096];
    char profile[24];
    char version[24];
    char release_id[96];
    char pin_hash[80];
    char hos[32] = "unknown";
    char firmware_hash[96] = "unknown";
    char model[32] = "unknown";
    bool atmosphere = false;
    bool environment_read_ok = false;
    const char *compatibility_status;
    char compatibility[768];
    PtcPctlSettingsSnapshot snapshot;
    PtcPctlStatus status;
    PtcRules rules;
    PtcErrorCode err;

    join_path(path, sizeof(path), sysmodule->app_root, "build.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text)) ||
        !json_string(text, "profile", profile, sizeof(profile)) || strcmp(profile, PLAYWISE_PROFILE_NAME) != 0 ||
        !json_string(text, "playwise_version", version, sizeof(version)) || strcmp(version, PLAYWISE_VERSION) != 0 ||
        !json_string(text, "release_id", release_id, sizeof(release_id))) {
        return PTC_ERR_RELEASE_MANIFEST_INVALID;
    }
    join_path(path, sizeof(path), sysmodule->app_root, "auth.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text)) ||
        !json_string(text, "pin_hash", pin_hash, sizeof(pin_hash)) || strlen(pin_hash) != 64U ||
        strlen(config->grant_secret) < 32U || !load_rules(sysmodule, &rules)) {
        return PTC_ERR_SETUP_PENDING;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    err = sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &snapshot);
    if (err != PTC_ERR_OK) return PTC_ERR_PCTL_BACKUP_FAILED;
    if (snapshot.size != PTC_PCTL_OPAQUE_SETTINGS_SIZE) return PTC_ERR_PCTL_LAYOUT_MISMATCH;
    err = sysmodule->pctl->vtable->read_status(
        sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &status);
    if (err != PTC_ERR_OK) return err;
    if (capture_handover) {
        err = capture_handover_today(setup, &status, now);
        if (err != PTC_ERR_OK) return err;
    }

    join_path(path, sizeof(path), sysmodule->app_root, "environment.json");
    if (sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        (void)json_bool_value(text, "read_ok", &environment_read_ok);
        (void)json_string(text, "hos", hos, sizeof(hos));
        (void)json_string(text, "firmware_hash", firmware_hash, sizeof(firmware_hash));
        (void)json_string(text, "model", model, sizeof(model));
        (void)json_bool_value(text, "atmosphere", &atmosphere);
    }
    compatibility_status = environment_read_ok && strcmp(hos, "22.5.0") == 0 &&
        strcmp(model, "mariko-oled") == 0 && atmosphere ? "verified" : "accepted_unknown";

    snprintf(
        compatibility,
        sizeof(compatibility),
        "{\"version\":1,\"status\":\"%s\",\"environment\":{"
        "\"hos\":\"%s\",\"firmware_hash\":\"%s\",\"model\":\"%s\","
        "\"atmosphere\":%s,\"pctl_profile\":\"layout-v1\"},"
        "\"release_id\":\"%s\",\"accepted_at\":%lld}\n",
        compatibility_status,
        hos,
        firmware_hash,
        model,
        atmosphere ? "true" : "false",
        release_id,
        (long long)now.unix_seconds);
    join_path(path, sizeof(path), sysmodule->app_root, "compatibility.json");
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, compatibility)) {
        return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    snprintf(setup->compatibility_status, sizeof(setup->compatibility_status), "%s", compatibility_status);
    return PTC_ERR_OK;
}

static bool process_complete_setup(PtcSysmodule *sysmodule, const PtcRequest *request,
    const PtcRuntimeConfig *config, bool disable_flag, const PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcSetupState setup;
    bool resuming_disabled_setup;
    bool reconfirming_runtime_fingerprint = false;
    bool retrying_failed_release = false;
    char disable_path[320];
    if (!load_setup_state(sysmodule, &setup)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            PTC_ERR_SETUP_PENDING, now.day_index, caps);
    }
    /* A completed takeover is idempotent: stale clients may resubmit after
       navigating back, but must never repeat the snapshot or PCTL writes. */
    if (strcmp(setup.phase, "active") == 0 && !disable_flag) {
        return write_current_status_result(
            sysmodule, request, ptc_control_mode_name(config->mode), false, caps, now, false);
    }
    if (disable_flag) {
        char reason[64];
        join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
        if (sysmodule->storage->vtable->read_text(sysmodule->storage, disable_path, reason, sizeof(reason))) {
            size_t length = strcspn(reason, "\r\n");
            reason[length] = '\0';
            reconfirming_runtime_fingerprint = strcmp(setup.phase, "protection") == 0 &&
                strcmp(reason, "runtime_fingerprint_changed") == 0;
            retrying_failed_release = setup.handover_today_pending &&
                strcmp(reason, "setup_release_failed") == 0;
        }
    }
    resuming_disabled_setup = disable_flag &&
        (strcmp(setup.phase, "restored") == 0 || strcmp(setup.phase, "active") == 0 ||
         reconfirming_runtime_fingerprint || retrying_failed_release);
    if (disable_flag && !resuming_disabled_setup) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            PTC_ERR_DISABLED, now.day_index, caps);
    }
    if (strcmp(setup.phase, "unconfigured") == 0 || strcmp(setup.phase, "compatibility_pending") == 0 ||
        strcmp(setup.phase, "protection") == 0 || strcmp(setup.phase, "restored") == 0 ||
        (strcmp(setup.phase, "pending") == 0 && setup.handover_today_pending) ||
        resuming_disabled_setup) {
        bool capture_handover = !setup.handover_today_pending &&
            (strcmp(setup.phase, "unconfigured") == 0 || strcmp(setup.phase, "compatibility_pending") == 0 ||
             (strcmp(setup.phase, "protection") == 0 && !reconfirming_runtime_fingerprint));
        PtcErrorCode preflight_err = run_release_preflight(sysmodule, config, &setup, now, capture_handover);
        if (preflight_err != PTC_ERR_OK) {
            /* A disabled active/restored installation remains retryable until a
               later parent-confirmed attempt passes the read-only preflight. */
            if (!resuming_disabled_setup) {
                snprintf(setup.phase, sizeof(setup.phase), "protection");
                snprintf(setup.compatibility_status, sizeof(setup.compatibility_status), "protection");
            }
            snprintf(setup.last_error, sizeof(setup.last_error), "%s", ptc_error_reason(preflight_err));
            (void)save_setup_state(sysmodule, &setup);
            return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
                preflight_err, now.day_index, caps);
        }
        preflight_err = persist_handover_today_rule(sysmodule, &setup, now);
        if (preflight_err != PTC_ERR_OK) {
            snprintf(setup.phase, sizeof(setup.phase), "protection");
            snprintf(setup.compatibility_status, sizeof(setup.compatibility_status), "protection");
            snprintf(setup.last_error, sizeof(setup.last_error), "%s", ptc_error_reason(preflight_err));
            (void)save_setup_state(sysmodule, &setup);
            return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
                preflight_err, now.day_index, caps);
        }
        snprintf(setup.phase, sizeof(setup.phase), "pending");
        if (!save_setup_state(sysmodule, &setup)) {
            return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
                PTC_ERR_STORAGE_WRITE_FAILED, now.day_index, caps);
        }
        if (setup.handover_today_pending) {
            PtcErrorCode handover_err = direct_handover_now(sysmodule, &setup, now);
            if (handover_err != PTC_ERR_OK) {
                return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
                    handover_err, now.day_index, caps);
            }
            if (disable_flag) {
                join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
                if (!sysmodule->storage->vtable->remove_path(sysmodule->storage, disable_path)) {
                    return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
                        PTC_ERR_STORAGE_WRITE_FAILED, now.day_index, caps);
                }
            }
            return write_current_status_result(
                sysmodule, request, ptc_control_mode_name(config->mode), false, caps, now, false);
        }
        {
            PtcErrorCode release_err = release_setup_now(sysmodule, &setup, now);
            if (release_err != PTC_ERR_OK) {
                return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
                    release_err, now.day_index, caps);
            }
        }
    }
    if (strcmp(setup.phase, "released") != 0 || !setup.restriction_cleared) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            PTC_ERR_SETUP_PENDING, now.day_index, caps);
    }
    setup.activate_after = now.unix_seconds + 5;
    setup.last_error[0] = '\0';
    if (!save_setup_state(sysmodule, &setup)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index, caps);
    }
    if (disable_flag) {
        join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
        if (!sysmodule->storage->vtable->remove_path(sysmodule->storage, disable_path)) {
            return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
                PTC_ERR_STORAGE_WRITE_FAILED, now.day_index, caps);
        }
    }
    return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), false, caps, now, false);
}

static bool process_retry_setup_release(PtcSysmodule *sysmodule, const PtcRequest *request,
    const PtcRuntimeConfig *config, const PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcSetupState setup;
    char disable_path[320];
    PtcErrorCode err;
    if (!load_setup_state(sysmodule, &setup) ||
        (strcmp(setup.phase, "pending") != 0 && strcmp(setup.phase, "failed") != 0 &&
         strcmp(setup.phase, "protection") != 0)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            PTC_ERR_BAD_REQUEST, now.day_index, caps);
    }
    err = run_release_preflight(sysmodule, config, &setup, now, !setup.handover_today_pending);
    if (err != PTC_ERR_OK) {
        snprintf(setup.last_error, sizeof(setup.last_error), "%s", ptc_error_reason(err));
        (void)save_setup_state(sysmodule, &setup);
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            err, now.day_index, caps);
    }
    err = persist_handover_today_rule(sysmodule, &setup, now);
    if (err != PTC_ERR_OK) {
        snprintf(setup.last_error, sizeof(setup.last_error), "%s", ptc_error_reason(err));
        (void)save_setup_state(sysmodule, &setup);
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            err, now.day_index, caps);
    }
    snprintf(setup.phase, sizeof(setup.phase), "pending");
    setup.restriction_cleared = false;
    setup.activate_after = 0;
    setup.last_error[0] = '\0';
    if (!save_setup_state(sysmodule, &setup)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index, caps);
    }
    err = setup.handover_today_pending
        ? direct_handover_now(sysmodule, &setup, now)
        : release_setup_now(sysmodule, &setup, now);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            err, now.day_index, caps);
    }
    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, disable_path) &&
        !sysmodule->storage->vtable->remove_path(sysmodule->storage, disable_path)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index, caps);
    }
    return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), false, caps, now, false);
}

static bool process_restore_install_snapshot(PtcSysmodule *sysmodule, const PtcRequest *request,
    const PtcRuntimeConfig *config, const PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcSetupState setup;
    PtcErrorCode err;
    if (!load_setup_state(sysmodule, &setup)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            PTC_ERR_RECOVERY_UNAVAILABLE, now.day_index, caps);
    }
    err = restore_install_snapshot_now(sysmodule, &setup, now);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
            err, now.day_index, caps);
    }
    return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), false, caps, now, false);
}

static void write_disable_flag(PtcSysmodule *sysmodule, const char *reason)
{
    char path[320];
    join_path(path, sizeof(path), sysmodule->app_root, "flags/disable.flag");
    (void)sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, reason ? reason : "disabled\n");
}

static PtcErrorCode restore_install_snapshot_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now)
{
    PtcPctlSettingsSnapshot original;
    PtcPctlSettingsSnapshot restored;
    PtcPctlStatus status;
    bool raw_restored = false;
    bool timer_restored = false;
    PtcErrorCode err;
    if (!load_install_snapshot(sysmodule, &original)) {
        return PTC_ERR_RECOVERY_UNAVAILABLE;
    }
    err = restore_snapshot_exact(sysmodule, &original, &restored, &status,
        ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
    if (err != PTC_ERR_OK || !raw_restored || !timer_restored) {
        snprintf(setup->phase, sizeof(setup->phase), "failed");
        setup->restriction_cleared = false;
        setup->snapshot_available = true;
        setup->activate_after = 0;
        snprintf(setup->last_error, sizeof(setup->last_error), "recovery_failed");
        (void)save_setup_state(sysmodule, setup);
        write_disable_flag(sysmodule, "recovery_failed\n");
        return PTC_ERR_RECOVERY_FAILED;
    }
    snprintf(setup->phase, sizeof(setup->phase), "restored");
    setup->restriction_cleared = false;
    setup->snapshot_available = true;
    setup->activate_after = 0;
    setup->last_error[0] = '\0';
    if (!save_setup_state(sysmodule, setup)) {
        write_disable_flag(sysmodule, "recovery_state_failed\n");
        return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    write_disable_flag(sysmodule, "install_snapshot_restored\n");
    return PTC_ERR_OK;
}

static PtcErrorCode release_setup_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now)
{
    char snapshot_path[320];
    PtcPctlSettingsSnapshot original;
    PtcPctlSettingsSnapshot restored;
    PtcPctlStatus before;
    PtcPctlStatus released;
    PtcPctlTarget target;
    bool raw_restored = false;
    bool timer_restored = false;
    bool timer_started = false;
    PtcErrorCode err;
    memset(&original, 0, sizeof(original));
    memset(&restored, 0, sizeof(restored));
    memset(&before, 0, sizeof(before));
    memset(&released, 0, sizeof(released));
    join_path(snapshot_path, sizeof(snapshot_path), sysmodule->app_root, "backups/install_pctl_snapshot.json");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, snapshot_path)) {
        if (!load_install_snapshot(sysmodule, &original)) {
            snprintf(setup->phase, sizeof(setup->phase), "failed");
            setup->restriction_cleared = false;
            setup->snapshot_available = false;
            setup->activate_after = 0;
            snprintf(setup->last_error, sizeof(setup->last_error), "recovery_unavailable");
            (void)save_setup_state(sysmodule, setup);
            write_disable_flag(sysmodule, "setup_snapshot_invalid\n");
            return PTC_ERR_RECOVERY_UNAVAILABLE;
        }
    } else {
        if (!sysmodule->pctl->vtable->snapshot_settings ||
            sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &original) != PTC_ERR_OK ||
            !save_install_snapshot(sysmodule, &original, now.unix_seconds)) {
            snprintf(setup->phase, sizeof(setup->phase), "failed");
            setup->restriction_cleared = false;
            setup->snapshot_available = false;
            setup->activate_after = 0;
            snprintf(setup->last_error, sizeof(setup->last_error), "pctl_backup_failed");
            (void)save_setup_state(sysmodule, setup);
            write_disable_flag(sysmodule, "setup_snapshot_failed\n");
            return PTC_ERR_PCTL_BACKUP_FAILED;
        }
    }
    setup->snapshot_available = true;
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &before);
    if (err == PTC_ERR_OK && before.unrestricted_today &&
        before.restricted_now_available && !before.restricted_now) {
        snprintf(setup->phase, sizeof(setup->phase), "released");
        setup->restriction_cleared = true;
        setup->activate_after = 0;
        setup->last_error[0] = '\0';
        return save_setup_state(sysmodule, setup) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    }
    err = backup_before_write(sysmodule, NULL, "setup");
    if (err == PTC_ERR_OK) {
        target.mode = PTC_PCTL_TARGET_UNLIMITED;
        target.minutes = 0;
        target.weekday = ptc_weekday_from_day_index(now.day_index);
        err = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &target);
    }
    if (err == PTC_ERR_OK) {
        err = start_timer_and_wait_target(sysmodule, NULL, now, "setup", PTC_PCTL_TARGET_UNLIMITED,
            0, "setup_release", &released, &timer_started);
    }
    if (err != PTC_ERR_OK) {
        PtcErrorCode restore_error = restore_snapshot_exact(sysmodule, &original, &restored, &released,
            ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
        snprintf(setup->phase, sizeof(setup->phase), "failed");
        setup->restriction_cleared = false;
        setup->activate_after = 0;
        snprintf(setup->last_error, sizeof(setup->last_error), "%s",
            restore_error == PTC_ERR_OK ? ptc_error_reason(err) : "recovery_failed");
        (void)save_setup_state(sysmodule, setup);
        write_disable_flag(sysmodule, restore_error == PTC_ERR_OK ? "setup_release_failed\n" : "setup_restore_failed\n");
        return restore_error == PTC_ERR_OK ? err : PTC_ERR_RECOVERY_FAILED;
    }
    snprintf(setup->phase, sizeof(setup->phase), "released");
    setup->restriction_cleared = true;
    setup->activate_after = 0;
    setup->last_error[0] = '\0';
    return save_setup_state(sysmodule, setup) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
}

static bool handover_remaining_matches(const PtcSetupState *setup, const PtcPctlStatus *status)
{
    uint16_t observed;
    uint16_t expected;
    if (setup->handover_unlimited) {
        return status->unrestricted_today && status->restricted_now_available && !status->restricted_now;
    }
    if (!setup->handover_remaining_available) {
        return setup->handover_minutes == 0U && status->restricted_now_available && status->restricted_now;
    }
    if (!status->remaining_available) {
        return false;
    }
    observed = status->remaining_minutes > UINT16_MAX ? UINT16_MAX : (uint16_t)status->remaining_minutes;
    expected = setup->handover_remaining_minutes;
    /* 1454 is rounded down to minutes, so the five-second setup grace may make
       a successful handover differ by one displayed minute. */
    return observed >= expected ? observed - expected <= 1 : expected - observed <= 1;
}

/* A fresh install already has the policy and remaining allowance that must be
   preserved.  Taking it over in place avoids a transient unlimited write and,
   importantly, avoids starting private timer command 1451 while on HOME. */
static PtcErrorCode direct_handover_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now)
{
    char snapshot_path[320];
    PtcPctlSettingsSnapshot installed;
    PtcPctlSettingsSnapshot current;
    PtcPctlStatus status;
    PtcRuntimeState runtime_state;
    PtcRules rules;
    PtcDayRule rule;
    PtcPctlTargetMode target_mode;
    bool snapshot_exists;
    bool state_matches;
    PtcErrorCode err;

    if (!setup->handover_today_pending || setup->handover_day_index != now.day_index) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &runtime_state)) {
        return PTC_ERR_RULES_INVALID;
    }
    rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    if (!rules.today_override.present || rules.today_override.day_index != now.day_index ||
        (setup->handover_unlimited ? rule.mode != PTC_RULE_MODE_UNLIMITED :
         rule.mode != PTC_RULE_MODE_LIMIT || rule.minutes != setup->handover_minutes)) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }

    memset(&installed, 0, sizeof(installed));
    memset(&current, 0, sizeof(current));
    join_path(snapshot_path, sizeof(snapshot_path), sysmodule->app_root, "backups/install_pctl_snapshot.json");
    snapshot_exists = sysmodule->storage->vtable->exists(sysmodule->storage, snapshot_path);
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &current) != PTC_ERR_OK ||
        current.size != PTC_PCTL_OPAQUE_SETTINGS_SIZE) {
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    if (snapshot_exists) {
        if (!load_install_snapshot(sysmodule, &installed)) {
            return PTC_ERR_RECOVERY_UNAVAILABLE;
        }
        if (!effect_snapshot_equal(&installed, &current) || installed.timer_enabled != current.timer_enabled) {
            return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
        }
    } else {
        if (!save_install_snapshot(sysmodule, &current, now.unix_seconds)) {
            return PTC_ERR_STORAGE_WRITE_FAILED;
        }
        append_event(sysmodule, NULL, "pctl_backup", PTC_ERR_OK, "direct_handover");
    }
    setup->snapshot_available = true;

    err = sysmodule->pctl->vtable->read_status(
        sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &status);
    if (err != PTC_ERR_OK) {
        return err;
    }
    if (!status.restriction_enabled_available || !status.restriction_enabled) {
        return PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
    }
    target_mode = target_from_day_rule(rule);
    if (!target_settings_observed(target_mode, rule.minutes, &status)) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }
    if (setup->handover_unlimited) {
        state_matches = status.unrestricted_today;
    } else if (setup->handover_minutes == 0U) {
        state_matches = status.blocked_today &&
            (!status.remaining_available || status.remaining_minutes == 0U);
    } else {
        state_matches = handover_remaining_matches(setup, &status);
    }
    if (!state_matches) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }

    runtime_state.last_enforced_day_index = now.day_index;
    runtime_state.last_enforced_mode = target_mode;
    runtime_state.last_enforced_minutes = rule.minutes;
    if (!save_state(sysmodule, &runtime_state, now.unix_seconds)) {
        return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    snprintf(setup->phase, sizeof(setup->phase), "active");
    setup->restriction_cleared = false;
    setup->activate_after = 0;
    setup->handover_today_pending = false;
    setup->last_error[0] = '\0';
    if (!save_setup_state(sysmodule, setup)) {
        return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    append_event(sysmodule, NULL, "handover_preserved", PTC_ERR_OK, "direct_takeover");
    return PTC_ERR_OK;
}

static PtcErrorCode activate_handover_today(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now)
{
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcPctlStatus observed;
    PtcDayRule rule;
    PtcCapabilities caps;
    PtcErrorCode err;
    if (!setup->handover_today_pending) {
        return PTC_ERR_OK;
    }
    if (setup->handover_day_index != now.day_index) {
        /* The captured allowance belongs to yesterday. Never carry it into a
           new day; the ordinary weekly plan takes over instead. */
        setup->handover_today_pending = false;
        return save_setup_state(sysmodule, setup) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    }
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &runtime_state)) {
        err = PTC_ERR_RULES_INVALID;
        goto restore_original;
    }
    rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    if (!rules.today_override.present || rules.today_override.day_index != now.day_index ||
        (setup->handover_unlimited ? rule.mode != PTC_RULE_MODE_UNLIMITED :
         rule.mode != PTC_RULE_MODE_LIMIT || rule.minutes != setup->handover_minutes)) {
        err = PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
        goto restore_original;
    }
    caps = load_capabilities(sysmodule);
    err = apply_target(sysmodule, NULL, &caps, now, "setup_handover",
        target_from_day_rule(rule), rule.minutes);
    if (err != PTC_ERR_OK) {
        goto restore_original;
    }
    err = start_timer_and_wait_target(sysmodule, NULL, now, "setup_handover",
        target_from_day_rule(rule), rule.minutes, "setup_handover", &observed, NULL);
    if (err != PTC_ERR_OK || !handover_remaining_matches(setup, &observed)) {
        if (err == PTC_ERR_OK) err = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
        goto restore_original;
    }
    runtime_state.last_enforced_day_index = now.day_index;
    runtime_state.last_enforced_mode = target_from_day_rule(rule);
    runtime_state.last_enforced_minutes = rule.minutes;
    if (!save_state(sysmodule, &runtime_state, now.unix_seconds)) {
        err = PTC_ERR_STORAGE_WRITE_FAILED;
        goto restore_original;
    }
    setup->handover_today_pending = false;
    if (!save_setup_state(sysmodule, setup)) {
        err = PTC_ERR_STORAGE_WRITE_FAILED;
        goto restore_original;
    }
    append_event(sysmodule, NULL, "handover_preserved", PTC_ERR_OK, "today_allowance");
    recovery_clear(sysmodule);
    return PTC_ERR_OK;

restore_original:
    append_event(sysmodule, NULL, "handover_restore", err, "today_allowance");
    setup->handover_today_pending = false;
    if (restore_install_snapshot_now(sysmodule, setup, now) == PTC_ERR_OK) {
        recovery_clear(sysmodule);
        return err;
    }
    write_disable_flag(sysmodule, "handover_restore_failed\n");
    return PTC_ERR_RECOVERY_FAILED;
}

static PtcErrorCode __attribute__((unused)) validate_runtime_fingerprint(PtcSysmodule *sysmodule)
{
    char path[320];
    char build[4096];
    char compatibility[2048];
    char environment[1024];
    char build_profile[24];
    char build_version[24];
    char build_release_id[96];
    char accepted_release_id[96];
    char accepted_hos[32];
    char current_hos[32];
    char accepted_model[32];
    char current_model[32];
    char accepted_hash[96];
    char current_hash[96];
    bool accepted_atmosphere = false;
    bool current_atmosphere = false;
    bool current_read_ok = false;

    join_path(path, sizeof(path), sysmodule->app_root, "build.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, build, sizeof(build)) ||
        !json_string(build, "profile", build_profile, sizeof(build_profile)) || strcmp(build_profile, PLAYWISE_PROFILE_NAME) != 0 ||
        !json_string(build, "playwise_version", build_version, sizeof(build_version)) || strcmp(build_version, PLAYWISE_VERSION) != 0 ||
        !json_string(build, "release_id", build_release_id, sizeof(build_release_id))) {
        return PTC_ERR_RELEASE_MANIFEST_INVALID;
    }
    join_path(path, sizeof(path), sysmodule->app_root, "compatibility.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, compatibility, sizeof(compatibility)) ||
        !json_string(compatibility, "release_id", accepted_release_id, sizeof(accepted_release_id)) ||
        strcmp(build_release_id, accepted_release_id) != 0) {
        return PTC_ERR_RELEASE_MANIFEST_INVALID;
    }
    join_path(path, sizeof(path), sysmodule->app_root, "environment.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, environment, sizeof(environment))) {
        return PTC_ERR_COMPATIBILITY_CONFIRMATION_REQUIRED;
    }
    (void)json_bool_value(environment, "read_ok", &current_read_ok);
    if (!current_read_ok) return PTC_ERR_COMPATIBILITY_CONFIRMATION_REQUIRED;
    if (!json_string(compatibility, "hos", accepted_hos, sizeof(accepted_hos)) ||
        !json_string(environment, "hos", current_hos, sizeof(current_hos)) ||
        !json_string(compatibility, "model", accepted_model, sizeof(accepted_model)) ||
        !json_string(environment, "model", current_model, sizeof(current_model)) ||
        !json_string(compatibility, "firmware_hash", accepted_hash, sizeof(accepted_hash)) ||
        !json_string(environment, "firmware_hash", current_hash, sizeof(current_hash)) ||
        !json_bool_value(compatibility, "atmosphere", &accepted_atmosphere) ||
        !json_bool_value(environment, "atmosphere", &current_atmosphere)) {
        return PTC_ERR_COMPATIBILITY_CONFIRMATION_REQUIRED;
    }
    if (strcmp(accepted_hos, current_hos) != 0 || strcmp(accepted_model, current_model) != 0 ||
        strcmp(accepted_hash, current_hash) != 0 || accepted_atmosphere != current_atmosphere) {
        return PTC_ERR_COMPATIBILITY_CONFIRMATION_REQUIRED;
    }
    return PTC_ERR_OK;
}

int ptc_sysmodule_bootstrap_setup(PtcSysmodule *sysmodule)
{
    char restore_flag[320];
    char retry_flag[320];
    char disable_path[320];
    PtcSetupState setup;
    PtcClockSnapshot now;
    PtcErrorCode err = PTC_ERR_OK;
    bool check_startup_recovery;
    if (!sysmodule || !load_setup_state(sysmodule, &setup)) return -1;
    check_startup_recovery = !sysmodule->startup_recovery_checked;
    sysmodule->startup_recovery_checked = true;
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    join_path(restore_flag, sizeof(restore_flag), sysmodule->app_root, "flags/restore_install_snapshot.flag");
    join_path(retry_flag, sizeof(retry_flag), sysmodule->app_root, "flags/retry_setup_release.flag");
    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, restore_flag)) {
        err = restore_install_snapshot_now(sysmodule, &setup, now);
        (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, restore_flag);
        if (err == PTC_ERR_OK) recovery_clear(sysmodule);
        return err == PTC_ERR_OK ? 1 : -1;
    }
    if (sysmodule->storage->vtable->exists(sysmodule->storage, retry_flag)) {
        if (recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule)) {
            write_disable_flag(sysmodule, "startup_recovery_failed\n");
            (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, retry_flag);
            return -1;
        }
        snprintf(setup.phase, sizeof(setup.phase), "pending");
        setup.restriction_cleared = false;
        setup.activate_after = 0;
        setup.last_error[0] = '\0';
        (void)save_setup_state(sysmodule, &setup);
        (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, retry_flag);
        (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, disable_path);
    }
    /* recovery/active is treated as abandoned only once per sysmodule boot.
       Later scheduler ticks may see a live Enforce confirmation transaction. */
    if (check_startup_recovery && recovery_path_exists(sysmodule)) {
        if (!recovery_rollback(sysmodule)) {
            write_disable_flag(sysmodule, "startup_recovery_failed\n");
            return -1;
        }
        write_disable_flag(sysmodule, "startup_transaction_restored\n");
        return 1;
    }
    if (strcmp(setup.phase, "active") == 0 || strcmp(setup.phase, "released") == 0) {
        /* The emulator has no real set:sys fingerprint to re-confirm, so keep the
           gate out of the simulated build instead of letting it park every boot
           in protection with disable.flag. */
#if !defined(PLAYWISE_DEVICE_LAB) && !defined(PLAYWISE_EDEN)
        PtcErrorCode gate_err = validate_runtime_fingerprint(sysmodule);
        if (gate_err != PTC_ERR_OK) {
            snprintf(setup.phase, sizeof(setup.phase), "protection");
            snprintf(setup.compatibility_status, sizeof(setup.compatibility_status), "pending");
            snprintf(setup.last_error, sizeof(setup.last_error), "%s", ptc_error_reason(gate_err));
            (void)save_setup_state(sysmodule, &setup);
            write_disable_flag(sysmodule, "runtime_fingerprint_changed\n");
            return -1;
        }
#endif
    }
    if (strcmp(setup.phase, "released") == 0 && setup.activate_after > 0 && now.unix_seconds >= setup.activate_after) {
        err = activate_handover_today(sysmodule, &setup, now);
        if (err != PTC_ERR_OK) {
            return -1;
        }
        snprintf(setup.phase, sizeof(setup.phase), "active");
        setup.activate_after = 0;
        setup.last_error[0] = '\0';
        return save_setup_state(sysmodule, &setup) ? 1 : -1;
    }
    return 0;
}

#ifdef PLAYWISE_DEVICE_LAB
static PtcErrorCode apply_probe_target(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now,
    const char *mode_name,
    PtcPctlTargetMode target_mode,
    uint16_t minutes,
    const char *stage)
{
    PtcPctlTarget target;
    PtcPctlDebugSnapshot before;
    PtcPctlDebugSnapshot after;
    PtcErrorCode err;
    target.mode = target_mode;
    target.minutes = minutes;
    target.weekday = ptc_weekday_from_day_index(now.day_index);
    take_pctl_debug_snapshot(sysmodule, &before);
    err = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &target);
    take_pctl_debug_snapshot(sysmodule, &after);
    append_pctl_debug(sysmodule, request, stage, mode_name, &target, err,
        last_pctl_ipc_result(sysmodule), &before, &after);
    append_event(sysmodule, request, err == PTC_ERR_OK ? stage : "pctl_apply_failed", err, pctl_target_mode_name(target_mode));
    return err;
}

static bool write_effect_probe_result(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *mode,
    bool dry_run,
    PtcErrorCode error,
    const PtcCapabilities *caps,
    PtcClockSnapshot now,
    const char *verdict,
    const char *failure_stage,
    uint16_t target_minutes,
    const PtcPctlStatus *before_status,
    PtcErrorCode before_error,
    const PtcPctlStatus *active_status,
    PtcErrorCode active_error,
    const PtcPctlStatus *restored_status,
    PtcErrorCode restored_error,
    bool raw_target_correct,
    bool timer_enabled_seen,
    bool remaining_seen,
    bool expiry_observed,
    bool raw_restored,
    bool timer_restored,
    const char *before_opaque_hex,
    const char *active_opaque_hex,
    const char *restored_opaque_hex)
{
    PtcResultState state;
    char base[3072];
    char json[8192];
    char extra[5200];
    char before_text[384];
    char active_text[384];
    char restored_text[384];
    char *completed_at;
    result_state_default_with_caps(&state, now.day_index, caps);
    if (active_error == PTC_ERR_OK) {
        state.limited_today = active_status->limited_today ? 1 : 0;
        state.blocked_today = active_status->blocked_today ? 1 : 0;
        state.unrestricted_today = active_status->unrestricted_today ? 1 : 0;
        state.remaining_available = active_status->remaining_available;
        state.remaining_minutes = result_remaining_minutes(active_status);
        state.play_timer_enabled = result_observed_bool(
            active_status->play_timer_enabled_available, active_status->play_timer_enabled);
        state.restricted_now = result_observed_bool(
            active_status->restricted_now_available, active_status->restricted_now);
    }
    if (error == PTC_ERR_OK) {
        (void)ptc_result_ok_json(base, sizeof(base), request->request_id, request->type_text, mode, dry_run, &state, now.unix_seconds);
    } else {
        (void)ptc_result_error_json(base, sizeof(base), request->request_id, request->type_text, mode, dry_run, error, &state, now.unix_seconds);
    }
    effect_status_json(before_text, sizeof(before_text), before_status, before_error);
    effect_status_json(active_text, sizeof(active_text), active_status, active_error);
    effect_status_json(restored_text, sizeof(restored_text), restored_status, restored_error);
    snprintf(
        extra, sizeof(extra),
        "{\"verdict\":\"%s\",\"failure_stage\":\"%s\",\"target_minutes\":%u,"
        "\"before\":%s,\"active\":%s,\"restored\":%s,"
        "\"opaque_snapshots\":{\"before_hex\":\"%s\",\"active_hex\":\"%s\",\"restored_hex\":\"%s\"},"
        "\"checks\":{\"raw_target_correct\":%s,\"timer_enabled\":%s,"
        "\"remaining_available\":%s,\"raw_restored\":%s,\"timer_restored\":%s},"
        "\"ipc_result\":\"0x%08x\",\"expiry_observed\":%s}",
        verdict ? verdict : "inconclusive",
        failure_stage ? failure_stage : "none",
        (unsigned int)target_minutes,
        before_text,
        active_text,
        restored_text,
        before_opaque_hex ? before_opaque_hex : "",
        active_opaque_hex ? active_opaque_hex : "",
        restored_opaque_hex ? restored_opaque_hex : "",
        raw_target_correct ? "true" : "false",
        timer_enabled_seen ? "true" : "false",
        remaining_seen ? "true" : "false",
        raw_restored ? "true" : "false",
        timer_restored ? "true" : "false",
        (unsigned int)last_pctl_ipc_result(sysmodule),
        expiry_observed ? "true" : "false");
    completed_at = strstr(base, ",\"completed_at\"");
    if (!completed_at) {
        return false;
    }
    snprintf(json, sizeof(json), "%.*s,\"pctl_effect_probe\":%s%s", (int)(completed_at - base), base, extra, completed_at);
    return write_result(sysmodule, request->request_id, json);
}

static bool __attribute__((unused)) process_probe_play_timer_effect(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const PtcRuntimeConfig *config,
    bool disable_flag,
    PtcCapabilities *caps,
    PtcClockSnapshot now)
{
    PtcPolicyDecision decision = ptc_policy_decide(config->mode, disable_flag, PTC_OPERATION_PROBE_PLAY_TIMER_EFFECT, caps, false, config->allow_unlimited_to_limited);
    PtcPctlSettingsSnapshot original;
    PtcPctlSettingsSnapshot active_snapshot;
    PtcPctlSettingsSnapshot restored_snapshot;
    PtcPctlStatus before_status;
    PtcPctlStatus active_status;
    PtcPctlStatus restored_status;
    PtcErrorCode before_error = PTC_ERR_PCTL_READ_FAILED;
    PtcErrorCode active_error = PTC_ERR_PCTL_READ_FAILED;
    PtcErrorCode restored_error = PTC_ERR_PCTL_READ_FAILED;
    PtcErrorCode final_error = PTC_ERR_OK;
    const char *failure_stage = "none";
    const char *verdict = "pass";
    uint16_t target_minutes = 1440;
    bool captured = false;
    bool raw_target_correct = false;
    bool target_decreases_remaining = false;
    uint32_t expected_remaining_delta = 0;
    bool timer_enabled_seen = false;
    bool remaining_seen = false;
    bool expiry_observed = false;
    bool raw_restored = false;
    bool timer_restored = false;
    unsigned int i;
    char disable_path[320];
    char before_opaque_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];
    char active_opaque_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];
    char restored_opaque_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];

    memset(&original, 0, sizeof(original));
    memset(&active_snapshot, 0, sizeof(active_snapshot));
    memset(&restored_snapshot, 0, sizeof(restored_snapshot));
    memset(&before_status, 0, sizeof(before_status));
    memset(&active_status, 0, sizeof(active_status));
    memset(&restored_status, 0, sizeof(restored_status));
    before_opaque_hex[0] = '\0';
    active_opaque_hex[0] = '\0';
    restored_opaque_hex[0] = '\0';

    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    if (decision.dry_run) {
        return write_effect_probe_result(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_OK, caps, now,
            "not_run", "not_run", 0, &before_status, before_error, &active_status, active_error,
            &restored_status, restored_error, false, false, false, false, false, false, "", "", "");
    }

    before_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &before_status);
    append_event(sysmodule, request, "effect_before", before_error, "read_status");
    if (before_error != PTC_ERR_OK) {
        final_error = before_error;
        failure_stage = "before_read";
        verdict = "fail";
        goto effect_done;
    }
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &original) != PTC_ERR_OK) {
        final_error = PTC_ERR_PCTL_BACKUP_FAILED;
        failure_stage = "snapshot";
        verdict = "fail";
        goto effect_done;
    }
    captured = true;
    effect_snapshot_hex(before_opaque_hex, sizeof(before_opaque_hex), &original);
    final_error = backup_before_write(sysmodule, request, ptc_control_mode_name(config->mode));
    if (final_error != PTC_ERR_OK) {
        failure_stage = "backup";
        verdict = "fail";
        goto effect_done;
    }
    {
        PtcPctlTarget target;
        target.mode = PTC_PCTL_TARGET_LIMIT;
        target.minutes = target_minutes;
        target.weekday = ptc_weekday_from_day_index(now.day_index);
        final_error = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &target);
        append_event(sysmodule, request, final_error == PTC_ERR_OK ? "effect_verify" : "pctl_apply_failed", final_error, "apply_target");
        if (final_error != PTC_ERR_OK) {
            failure_stage = "write";
            verdict = "fail";
            goto effect_done;
        }
    }
    final_error = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
    append_event(sysmodule, request, final_error == PTC_ERR_OK ? "effect_verify" : "pctl_apply_failed", final_error, "start_timer");
    if (final_error != PTC_ERR_OK) {
        failure_stage = "start_timer";
        verdict = "fail";
        goto effect_done;
    }
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &active_snapshot) != PTC_ERR_OK) {
        final_error = PTC_ERR_PCTL_READ_FAILED;
        failure_stage = "raw_target";
        verdict = "fail";
        goto effect_done;
    }
    effect_snapshot_hex(active_opaque_hex, sizeof(active_opaque_hex), &active_snapshot);
    raw_target_correct = !effect_snapshot_equal(&original, &active_snapshot);
    if (!raw_target_correct) {
        PtcPctlTarget alternate_target = { PTC_PCTL_TARGET_LIMIT, 1430, ptc_weekday_from_day_index(now.day_index) };
        if (before_status.remaining_available) {
            if (before_status.remaining_minutes <= 1U) {
                final_error = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
                failure_stage = "safe_target";
                verdict = "inconclusive";
                goto effect_done;
            }
            expected_remaining_delta = before_status.remaining_minutes > 10U
                ? 10U
                : before_status.remaining_minutes - 1U;
            alternate_target.minutes = (uint16_t)(1440U - expected_remaining_delta);
        } else {
            expected_remaining_delta = 10U;
        }
        target_decreases_remaining = true;
        target_minutes = alternate_target.minutes;
        final_error = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &alternate_target);
        append_event(sysmodule, request, final_error == PTC_ERR_OK ? "effect_verify" : "pctl_apply_failed", final_error, "apply_alternate_target");
        if (final_error == PTC_ERR_OK) {
            final_error = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
            append_event(sysmodule, request, final_error == PTC_ERR_OK ? "effect_verify" : "pctl_apply_failed", final_error, "start_alternate_timer");
        }
        if (final_error != PTC_ERR_OK ||
            sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &active_snapshot) != PTC_ERR_OK) {
            final_error = final_error == PTC_ERR_OK ? PTC_ERR_PCTL_READ_FAILED : final_error;
            failure_stage = "raw_target";
            verdict = "fail";
            goto effect_done;
        }
        effect_snapshot_hex(active_opaque_hex, sizeof(active_opaque_hex), &active_snapshot);
        raw_target_correct = !effect_snapshot_equal(&original, &active_snapshot);
    }
    if (!raw_target_correct) {
        final_error = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
        failure_stage = "raw_target";
        verdict = "fail";
        goto effect_done;
    }
    for (i = 0; i < 20U; ++i) {
        active_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &active_status);
        if (active_error == PTC_ERR_OK) {
            timer_enabled_seen = active_status.play_timer_enabled;
            remaining_seen = active_status.remaining_available &&
                active_status.remaining_minutes <= target_minutes &&
                (!before_status.remaining_available ||
                    (target_decreases_remaining
                        ? active_status.remaining_minutes + expected_remaining_delta <= before_status.remaining_minutes
                        : active_status.remaining_minutes > before_status.remaining_minutes));
            if (remaining_seen && timer_enabled_seen) {
                break;
            }
        }
        effect_wait(sysmodule, 250);
    }
    if (active_error != PTC_ERR_OK || !remaining_seen || !timer_enabled_seen) {
        final_error = active_error == PTC_ERR_OK ? PTC_ERR_PCTL_EFFECT_NOT_OBSERVED : active_error;
        failure_stage = "runtime_status";
        verdict = "fail";
        goto effect_done;
    }
    if (request->wait_for_expiry) {
        PtcPctlTarget expiry_target = { PTC_PCTL_TARGET_LIMIT, 1, ptc_weekday_from_day_index(now.day_index) };
        final_error = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &expiry_target);
        if (final_error == PTC_ERR_OK) {
            final_error = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
        }
        for (i = 0; final_error == PTC_ERR_OK && i < 45U; ++i) {
            active_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &active_status);
            if (active_error != PTC_ERR_OK) {
                final_error = active_error;
                break;
            }
            if (active_status.restricted_now || (active_status.remaining_available && active_status.remaining_minutes == 0U)) {
                expiry_observed = true;
                break;
            }
            effect_wait(sysmodule, 2000);
        }
        if (!expiry_observed) {
            final_error = final_error == PTC_ERR_OK ? PTC_ERR_PCTL_EFFECT_NOT_OBSERVED : final_error;
            failure_stage = "expiry";
            verdict = "inconclusive";
        }
    }
effect_done:
    if (captured && sysmodule->pctl->vtable->restore_settings) {
        restored_error = sysmodule->pctl->vtable->restore_settings(sysmodule->pctl, &original);
        if (restored_error == PTC_ERR_OK) {
            PtcErrorCode timer_error = original.timer_enabled
                ? sysmodule->pctl->vtable->start_timer(sysmodule->pctl)
                : sysmodule->pctl->vtable->stop_timer(sysmodule->pctl);
            if (timer_error != PTC_ERR_OK) {
                restored_error = timer_error;
            }
        }
        if (restored_error == PTC_ERR_OK && sysmodule->pctl->vtable->snapshot_settings) {
            restored_error = sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &restored_snapshot);
            raw_restored = effect_snapshot_equal(&original, &restored_snapshot);
            timer_restored = restored_snapshot.timer_enabled == original.timer_enabled;
            effect_snapshot_hex(restored_opaque_hex, sizeof(restored_opaque_hex), &restored_snapshot);
        }
        if (restored_error == PTC_ERR_OK) {
            restored_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &restored_status);
        }
        append_event(sysmodule, request, restored_error == PTC_ERR_OK && raw_restored && timer_restored ? "effect_restore" : "effect_restore_failed", restored_error, "restore");
        if (restored_error != PTC_ERR_OK || !raw_restored || !timer_restored) {
            final_error = PTC_ERR_PCTL_RESTORE_FAILED;
            failure_stage = "restore";
            verdict = "fail";
            caps->play_timer_write_verified = false;
            caps->play_timer_effect_verified = false;
            join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
            (void)sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, disable_path, "restore_failed\n");
        }
    }
    if (final_error == PTC_ERR_OK && strcmp(verdict, "pass") == 0) {
        caps->play_timer_write_verified = true;
        caps->play_timer_effect_verified = true;
        snprintf(caps->play_timer_effect_backend, sizeof(caps->play_timer_effect_backend), "%s", PTC_PLAY_TIMER_EFFECT_BACKEND);
        if (!save_capabilities(sysmodule, caps, now.unix_seconds)) {
            final_error = PTC_ERR_STORAGE_WRITE_FAILED;
            failure_stage = "capability_persist";
            verdict = "fail";
        }
    }
    if (final_error == PTC_ERR_OK && strcmp(verdict, "pass") != 0) {
        if (strcmp(verdict, "inconclusive") == 0) {
            final_error = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
        }
    }
    append_event(sysmodule, request, final_error == PTC_ERR_OK ? "probe_ok" : "probe_failed", final_error, failure_stage);
    return write_effect_probe_result(sysmodule, request, ptc_control_mode_name(config->mode), false, final_error, caps, now,
        verdict, failure_stage, target_minutes, &before_status, before_error, &active_status, active_error,
        &restored_status, restored_error, raw_target_correct, timer_enabled_seen, remaining_seen,
        expiry_observed, raw_restored, timer_restored, before_opaque_hex, active_opaque_hex, restored_opaque_hex);
}

static void disable_after_restore_failure(PtcSysmodule *sysmodule, PtcCapabilities *caps, PtcClockSnapshot now)
{
    char disable_path[320];
    caps->play_timer_write_verified = false;
    caps->play_timer_effect_verified = false;
    caps->play_timer_effect_backend[0] = '\0';
    caps->raw_block_verified = false;
    (void)save_capabilities(sysmodule, caps, now.unix_seconds);
    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    (void)sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, disable_path, "restore_failed\n");
}

static uint16_t device_test_ready_minutes(const PtcPctlStatus *before)
{
    int64_t played = result_played_minutes(before);
    uint32_t target;
    if (played < 0) {
        return 1425U;
    }
    target = (uint32_t)played + 30U;
    if (target < 60U) {
        target = 60U;
    }
    if (target > 1425U) {
        target = 1425U;
    }
    return (uint16_t)target;
}

static bool write_device_test_result(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *mode,
    bool dry_run,
    PtcErrorCode error,
    const PtcCapabilities *caps,
    PtcClockSnapshot now,
    const char *verdict,
    const char *failure_stage,
    uint16_t ready_target_minutes,
    const PtcPctlStatus *release_status,
    PtcErrorCode release_error,
    const PtcPctlStatus *restored_status,
    PtcErrorCode restored_error,
    const PtcPctlStatus *ready_status,
    PtcErrorCode ready_error,
    bool restriction_cleared,
    bool raw_restored,
    bool timer_restored,
    bool rules_persisted,
    const char *before_opaque_hex,
    const char *restored_opaque_hex)
{
    PtcResultState state;
    const PtcPctlStatus *reported = ready_error == PTC_ERR_OK ? ready_status :
        (restored_error == PTC_ERR_OK ? restored_status : NULL);
    char base[3072];
    char json[8192];
    char extra[4600];
    char release_text[384];
    char restored_text[384];
    char ready_text[384];
    char *completed_at;
    result_state_default_with_caps(&state, now.day_index, caps);
    if (reported) {
        state.limited_today = reported->limited_today ? 1 : 0;
        state.blocked_today = reported->blocked_today ? 1 : 0;
        state.unrestricted_today = reported->unrestricted_today ? 1 : 0;
        state.remaining_available = reported->remaining_available;
        state.remaining_minutes = result_remaining_minutes(reported);
        state.played_minutes = result_played_minutes(reported);
        state.played_minutes_available = state.played_minutes >= 0;
        state.play_timer_enabled = result_observed_bool(
            reported->play_timer_enabled_available, reported->play_timer_enabled);
        state.restricted_now = result_observed_bool(
            reported->restricted_now_available, reported->restricted_now);
    }
    if (error == PTC_ERR_OK) {
        (void)ptc_result_ok_json(base, sizeof(base), request->request_id, request->type_text, mode, dry_run, &state, now.unix_seconds);
    } else {
        (void)ptc_result_error_json(base, sizeof(base), request->request_id, request->type_text, mode, dry_run, error, &state, now.unix_seconds);
    }
    effect_status_json(release_text, sizeof(release_text), release_status, release_error);
    effect_status_json(restored_text, sizeof(restored_text), restored_status, restored_error);
    effect_status_json(ready_text, sizeof(ready_text), ready_status, ready_error);
    snprintf(
        extra,
        sizeof(extra),
        "{\"verdict\":\"%s\",\"failure_stage\":\"%s\",\"ready_target_minutes\":%u,"
        "\"release\":%s,\"restored\":%s,\"ready\":%s,"
        "\"opaque_snapshots\":{\"before_hex\":\"%s\",\"restored_hex\":\"%s\"},"
        "\"checks\":{\"restriction_cleared\":%s,\"raw_restored\":%s,\"timer_restored\":%s,"
        "\"ready_limited\":%s,\"ready_timer_enabled\":%s,\"ready_remaining\":%s,"
        "\"ready_restricted_cleared\":%s,\"rules_persisted\":%s},\"ipc_result\":\"0x%08x\"}",
        verdict ? verdict : "fail",
        failure_stage ? failure_stage : "none",
        (unsigned int)ready_target_minutes,
        release_text,
        restored_text,
        ready_text,
        before_opaque_hex ? before_opaque_hex : "",
        restored_opaque_hex ? restored_opaque_hex : "",
        restriction_cleared ? "true" : "false",
        raw_restored ? "true" : "false",
        timer_restored ? "true" : "false",
        ready_error == PTC_ERR_OK && ready_status->limited_today ? "true" : "false",
        ready_error == PTC_ERR_OK && ready_status->play_timer_enabled_available &&
            ready_status->play_timer_enabled ? "true" : "false",
        ready_error == PTC_ERR_OK && ready_status->remaining_available && ready_status->remaining_minutes > 0U ? "true" : "false",
        ready_error == PTC_ERR_OK && ready_status->restricted_now_available &&
            !ready_status->restricted_now ? "true" : "false",
        rules_persisted ? "true" : "false",
        (unsigned int)last_pctl_ipc_result(sysmodule));
    completed_at = strstr(base, ",\"completed_at\"");
    if (!completed_at) {
        return false;
    }
    snprintf(json, sizeof(json), "%.*s,\"device_test\":%s%s", (int)(completed_at - base), base, extra, completed_at);
    return write_result(sysmodule, request->request_id, json);
}

static bool __attribute__((unused)) process_prepare_device_test(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const PtcRuntimeConfig *config,
    bool disable_flag,
    PtcCapabilities *caps,
    PtcClockSnapshot now)
{
    PtcPolicyDecision decision = ptc_policy_decide(config->mode, disable_flag, PTC_OPERATION_PREPARE_DEVICE_TEST, caps, false, config->allow_unlimited_to_limited);
    PtcCapabilities original_caps = *caps;
    PtcPctlSettingsSnapshot original;
    PtcPctlSettingsSnapshot restored_snapshot;
    PtcPctlStatus before_status;
    PtcPctlStatus release_status;
    PtcPctlStatus restored_status;
    PtcPctlStatus ready_status;
    PtcRules original_rules;
    PtcRules ready_rules;
    PtcErrorCode before_error = PTC_ERR_PCTL_READ_FAILED;
    PtcErrorCode release_error = PTC_ERR_PCTL_READ_FAILED;
    PtcErrorCode restored_error = PTC_ERR_PCTL_READ_FAILED;
    PtcErrorCode ready_error = PTC_ERR_PCTL_READ_FAILED;
    PtcErrorCode final_error = PTC_ERR_OK;
    const char *failure_stage = "none";
    const char *verdict = "pass";
    bool captured = false;
    bool restriction_cleared = false;
    bool raw_restored = false;
    bool timer_restored = false;
    bool rules_existed = false;
    bool rules_persisted = false;
    bool capabilities_existed = false;
    bool capabilities_persisted = false;
    bool timer_started = false;
    bool ready_changed = false;
    bool result_written;
    uint16_t ready_target_minutes = 0;
    char rules_path[320];
    char capabilities_path[320];
    char before_opaque_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];
    char restored_opaque_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];

    memset(&original, 0, sizeof(original));
    memset(&restored_snapshot, 0, sizeof(restored_snapshot));
    memset(&before_status, 0, sizeof(before_status));
    memset(&release_status, 0, sizeof(release_status));
    memset(&restored_status, 0, sizeof(restored_status));
    memset(&ready_status, 0, sizeof(ready_status));
    before_opaque_hex[0] = '\0';
    restored_opaque_hex[0] = '\0';

    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    if (decision.dry_run) {
        return write_device_test_result(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_OK, caps, now,
            "not_run", "not_run", 0, &release_status, release_error, &restored_status, restored_error,
            &ready_status, ready_error, false, false, false, false, "", "");
    }
    if (!load_rules(sysmodule, &original_rules)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, PTC_ERR_RULES_INVALID, now.day_index, caps);
    }
    ready_rules = original_rules;
    join_path(rules_path, sizeof(rules_path), sysmodule->app_root, "rules.json");
    join_path(capabilities_path, sizeof(capabilities_path), sysmodule->app_root, "capabilities.json");
    rules_existed = sysmodule->storage->vtable->exists(sysmodule->storage, rules_path);
    capabilities_existed = sysmodule->storage->vtable->exists(sysmodule->storage, capabilities_path);

    before_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &before_status);
    append_event(sysmodule, request, "device_test_before", before_error, "read_status");
    if (before_error != PTC_ERR_OK) {
        final_error = before_error;
        failure_stage = "before_read";
        verdict = "fail";
        goto device_test_done;
    }
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &original) != PTC_ERR_OK) {
        final_error = PTC_ERR_PCTL_BACKUP_FAILED;
        failure_stage = "snapshot";
        verdict = "fail";
        goto device_test_done;
    }
    captured = true;
    effect_snapshot_hex(before_opaque_hex, sizeof(before_opaque_hex), &original);
    final_error = backup_before_write(sysmodule, request, ptc_control_mode_name(config->mode));
    if (final_error != PTC_ERR_OK) {
        failure_stage = "backup";
        verdict = "fail";
        goto device_test_done;
    }

    final_error = apply_probe_target(sysmodule, request, now, ptc_control_mode_name(config->mode),
        PTC_PCTL_TARGET_UNLIMITED, 0, "device_test_release");
    if (final_error != PTC_ERR_OK) {
        failure_stage = "release_write";
        verdict = "fail";
    } else {
        release_error = start_timer_and_wait_target(sysmodule, request, now, ptc_control_mode_name(config->mode),
            PTC_PCTL_TARGET_UNLIMITED, 0, "device_test_release", &release_status, &timer_started);
        restriction_cleared = release_error == PTC_ERR_OK;
        if (release_error != PTC_ERR_OK) {
            final_error = release_error;
            failure_stage = timer_started ? "release_status" : "release_start";
            verdict = "fail";
        }
    }

    restored_error = restore_snapshot_exact(sysmodule, &original, &restored_snapshot, &restored_status,
        ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
    effect_snapshot_hex(restored_opaque_hex, sizeof(restored_opaque_hex), &restored_snapshot);
    append_event(sysmodule, request,
        restored_error == PTC_ERR_OK ? "device_test_restore" : "device_test_restore_failed",
        restored_error, "restore");
    if (restored_error != PTC_ERR_OK) {
        final_error = PTC_ERR_PCTL_RESTORE_FAILED;
        failure_stage = "restore";
        verdict = "fail";
        write_disable_flag(sysmodule, "transaction_restore_failed\n");
        goto device_test_done;
    }
    if (final_error != PTC_ERR_OK) {
        goto device_test_done;
    }

    ready_target_minutes = device_test_ready_minutes(&before_status);
    final_error = apply_probe_target(sysmodule, request, now, ptc_control_mode_name(config->mode),
        PTC_PCTL_TARGET_LIMIT, ready_target_minutes, "device_test_ready");
    if (final_error != PTC_ERR_OK) {
        failure_stage = "ready_write";
        verdict = "fail";
        goto device_test_rollback;
    }
    ready_changed = true;
    ready_error = start_timer_and_wait_target(sysmodule, request, now, ptc_control_mode_name(config->mode),
        PTC_PCTL_TARGET_LIMIT, ready_target_minutes, "device_test_ready", &ready_status, &timer_started);
    if (ready_error == PTC_ERR_PCTL_EFFECT_NOT_OBSERVED && ready_target_minutes < 1440U &&
        ready_status.remaining_available && (ready_status.restricted_now || ready_status.remaining_minutes == 0U)) {
        ready_target_minutes = 1440U;
        final_error = apply_probe_target(sysmodule, request, now, ptc_control_mode_name(config->mode),
            PTC_PCTL_TARGET_LIMIT, ready_target_minutes, "device_test_ready");
        if (final_error == PTC_ERR_OK) {
            ready_error = start_timer_and_wait_target(sysmodule, request, now, ptc_control_mode_name(config->mode),
                PTC_PCTL_TARGET_LIMIT, ready_target_minutes, "device_test_ready", &ready_status, &timer_started);
        }
    }
    if (final_error != PTC_ERR_OK) {
        failure_stage = "ready_write";
        verdict = "fail";
        goto device_test_rollback;
    }
    if (ready_error != PTC_ERR_OK) {
        final_error = ready_error;
        failure_stage = timer_started ? "ready_status" : "ready_start";
        verdict = "fail";
        goto device_test_rollback;
    }

    ready_rules.today_override.present = true;
    ready_rules.today_override.day_index = now.day_index;
    ready_rules.today_override.rule.mode = PTC_RULE_MODE_LIMIT;
    ready_rules.today_override.rule.minutes = ready_target_minutes;
    if (!save_rules(sysmodule, &ready_rules)) {
        final_error = PTC_ERR_STORAGE_WRITE_FAILED;
        failure_stage = "rules_persist";
        verdict = "fail";
        goto device_test_rollback;
    }
    rules_persisted = true;
    append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "device_test_ready");

    caps->play_timer_write_verified = true;
    caps->play_timer_effect_verified = true;
    snprintf(caps->play_timer_effect_backend, sizeof(caps->play_timer_effect_backend), "%s", PTC_PLAY_TIMER_EFFECT_BACKEND);
    if (!save_capabilities(sysmodule, caps, now.unix_seconds)) {
        final_error = PTC_ERR_STORAGE_WRITE_FAILED;
        failure_stage = "capability_persist";
        verdict = "fail";
        goto device_test_rollback;
    }
    capabilities_persisted = true;

device_test_done:
    append_event(sysmodule, request, final_error == PTC_ERR_OK ? "probe_ok" : "probe_failed", final_error, failure_stage);
    result_written = write_device_test_result(sysmodule, request, ptc_control_mode_name(config->mode), false,
        final_error, caps, now, verdict, failure_stage, ready_target_minutes,
        &release_status, release_error, &restored_status, restored_error, &ready_status, ready_error,
        restriction_cleared, raw_restored, timer_restored, rules_persisted,
        before_opaque_hex, restored_opaque_hex);
    if (result_written) {
        append_event(sysmodule, request, final_error == PTC_ERR_OK ? "result_ok" : "result_error", final_error, "");
        return true;
    }
    if (final_error == PTC_ERR_OK) {
        final_error = PTC_ERR_STORAGE_WRITE_FAILED;
        failure_stage = "result_persist";
        verdict = "fail";
        goto device_test_rollback_no_result;
    }
    return false;

device_test_rollback:
    if (captured && ready_changed) {
        restored_error = restore_snapshot_exact(sysmodule, &original, &restored_snapshot, &restored_status,
            ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
        effect_snapshot_hex(restored_opaque_hex, sizeof(restored_opaque_hex), &restored_snapshot);
        append_event(sysmodule, request,
            restored_error == PTC_ERR_OK ? "device_test_restore" : "device_test_restore_failed",
            restored_error, "rollback");
        if (restored_error != PTC_ERR_OK) {
            final_error = PTC_ERR_PCTL_RESTORE_FAILED;
            failure_stage = "restore";
            disable_after_restore_failure(sysmodule, caps, now);
        }
    }
    if (rules_persisted) {
        if (!restore_rules(sysmodule, &original_rules, rules_existed)) {
            final_error = PTC_ERR_STORAGE_WRITE_FAILED;
            failure_stage = "rules_persist";
            disable_after_restore_failure(sysmodule, caps, now);
        }
        rules_persisted = false;
    }
    if (capabilities_persisted) {
        if (!restore_capabilities(sysmodule, &original_caps, capabilities_existed, now.unix_seconds)) {
            final_error = PTC_ERR_STORAGE_WRITE_FAILED;
            failure_stage = "capability_persist";
            disable_after_restore_failure(sysmodule, caps, now);
        }
        capabilities_persisted = false;
    }
    *caps = original_caps;
    goto device_test_done;

device_test_rollback_no_result:
    if (captured) {
        restored_error = restore_snapshot_exact(sysmodule, &original, &restored_snapshot, &restored_status,
            ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
        if (restored_error != PTC_ERR_OK) {
            disable_after_restore_failure(sysmodule, caps, now);
        }
    }
    if (rules_persisted) {
        if (!restore_rules(sysmodule, &original_rules, rules_existed)) {
            disable_after_restore_failure(sysmodule, caps, now);
        }
    }
    if (capabilities_persisted) {
        if (!restore_capabilities(sysmodule, &original_caps, capabilities_existed, now.unix_seconds)) {
            disable_after_restore_failure(sysmodule, caps, now);
        }
        *caps = original_caps;
    }
    append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "device_test");
    return false;
}

static bool write_raw_block_probe_result(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *mode,
    bool dry_run,
    PtcErrorCode error,
    const PtcCapabilities *caps,
    PtcClockSnapshot now,
    const char *verdict,
    const char *failure_stage,
    const PtcPctlStatus *before_status,
    PtcErrorCode before_error,
    const PtcPctlStatus *active_status,
    PtcErrorCode active_error,
    const PtcPctlStatus *restored_status,
    PtcErrorCode restored_error,
    bool raw_target_written,
    bool blocked_observed,
    bool raw_restored,
    bool timer_restored,
    const char *before_opaque_hex,
    const char *active_opaque_hex,
    const char *restored_opaque_hex)
{
    PtcResultState state;
    char base[3072];
    char json[8192];
    char extra[3072];
    char before_text[384];
    char active_text[384];
    char restored_text[384];
    char *completed_at;
    /* The probe restores the device before returning, so result.state must describe the
       restored device, not the momentary blocked state the probe wrote. */
    const PtcPctlStatus *reported = NULL;
    result_state_default_with_caps(&state, now.day_index, caps);
    if (restored_error == PTC_ERR_OK) {
        reported = restored_status;
    } else if (before_error == PTC_ERR_OK) {
        reported = before_status;
    }
    if (reported) {
        state.limited_today = reported->limited_today ? 1 : 0;
        state.blocked_today = reported->blocked_today ? 1 : 0;
        state.unrestricted_today = reported->unrestricted_today ? 1 : 0;
        state.remaining_available = reported->remaining_available;
        state.remaining_minutes = result_remaining_minutes(reported);
        state.play_timer_enabled = result_observed_bool(
            reported->play_timer_enabled_available, reported->play_timer_enabled);
        state.restricted_now = result_observed_bool(
            reported->restricted_now_available, reported->restricted_now);
    }
    if (error == PTC_ERR_OK) {
        (void)ptc_result_ok_json(base, sizeof(base), request->request_id, request->type_text, mode, dry_run, &state, now.unix_seconds);
    } else {
        (void)ptc_result_error_json(base, sizeof(base), request->request_id, request->type_text, mode, dry_run, error, &state, now.unix_seconds);
    }
    effect_status_json(before_text, sizeof(before_text), before_status, before_error);
    effect_status_json(active_text, sizeof(active_text), active_status, active_error);
    effect_status_json(restored_text, sizeof(restored_text), restored_status, restored_error);
    snprintf(
        extra, sizeof(extra),
        "{\"verdict\":\"%s\",\"failure_stage\":\"%s\",\"target_minutes\":0,"
        "\"before\":%s,\"active\":%s,\"restored\":%s,"
        "\"opaque_snapshots\":{\"before_hex\":\"%s\",\"active_hex\":\"%s\",\"restored_hex\":\"%s\"},"
        "\"checks\":{\"raw_target_written\":%s,\"blocked_observed\":%s,"
        "\"raw_restored\":%s,\"timer_restored\":%s},\"ipc_result\":\"0x%08x\"}",
        verdict ? verdict : "inconclusive",
        failure_stage ? failure_stage : "none",
        before_text,
        active_text,
        restored_text,
        before_opaque_hex ? before_opaque_hex : "",
        active_opaque_hex ? active_opaque_hex : "",
        restored_opaque_hex ? restored_opaque_hex : "",
        raw_target_written ? "true" : "false",
        blocked_observed ? "true" : "false",
        raw_restored ? "true" : "false",
        timer_restored ? "true" : "false",
        (unsigned int)last_pctl_ipc_result(sysmodule));
    completed_at = strstr(base, ",\"completed_at\"");
    if (!completed_at) {
        return false;
    }
    snprintf(json, sizeof(json), "%.*s,\"pctl_raw_block_probe\":%s%s", (int)(completed_at - base), base, extra, completed_at);
    return write_result(sysmodule, request->request_id, json);
}

/*
 * Raw block probe. Rehearses exactly what block_today performs: write minutes=0 with the
 * restricted flags for today's weekday through the same play timer settings write path the
 * write/effect probes already verified on hardware, confirm the device reports the
 * restriction at runtime, then restore the captured settings and verify the restore.
 * No new PCTL command is involved; the capability question is whether raw 0 is accepted
 * and actually restricts, which only an A/B write can answer.
 */
static bool __attribute__((unused)) process_probe_raw_block(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const PtcRuntimeConfig *config,
    bool disable_flag,
    PtcCapabilities *caps,
    PtcClockSnapshot now)
{
    PtcPolicyDecision decision = ptc_policy_decide(config->mode, disable_flag, PTC_OPERATION_PROBE_RAW_BLOCK, caps, false, config->allow_unlimited_to_limited);
    PtcPctlSettingsSnapshot original;
    PtcPctlSettingsSnapshot active_snapshot;
    PtcPctlSettingsSnapshot restored_snapshot;
    PtcPctlStatus before_status;
    PtcPctlStatus active_status;
    PtcPctlStatus restored_status;
    PtcErrorCode before_error = PTC_ERR_PCTL_READ_FAILED;
    PtcErrorCode active_error = PTC_ERR_PCTL_READ_FAILED;
    PtcErrorCode restored_error = PTC_ERR_PCTL_READ_FAILED;
    PtcErrorCode final_error = PTC_ERR_OK;
    const char *failure_stage = "none";
    const char *verdict = "pass";
    bool captured = false;
    bool raw_target_written = false;
    bool blocked_observed = false;
    bool raw_restored = false;
    bool timer_restored = false;
    unsigned int i;
    char disable_path[320];
    char before_opaque_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];
    char active_opaque_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];
    char restored_opaque_hex[(PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U) + 1U];

    memset(&original, 0, sizeof(original));
    memset(&active_snapshot, 0, sizeof(active_snapshot));
    memset(&restored_snapshot, 0, sizeof(restored_snapshot));
    memset(&before_status, 0, sizeof(before_status));
    memset(&active_status, 0, sizeof(active_status));
    memset(&restored_status, 0, sizeof(restored_status));
    before_opaque_hex[0] = '\0';
    active_opaque_hex[0] = '\0';
    restored_opaque_hex[0] = '\0';

    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    if (decision.dry_run) {
        return write_raw_block_probe_result(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_OK, caps, now,
            "not_run", "not_run", &before_status, before_error, &active_status, active_error,
            &restored_status, restored_error, false, false, false, false, "", "", "");
    }
    /* Plain play-timer writes are now treated as a recoverable baseline; this
       probe only establishes the separate raw-block capability. */

    before_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &before_status);
    append_event(sysmodule, request, "raw_block_before", before_error, "read_status");
    if (before_error != PTC_ERR_OK) {
        final_error = before_error;
        failure_stage = "before_read";
        verdict = "fail";
        goto raw_block_done;
    }
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &original) != PTC_ERR_OK) {
        final_error = PTC_ERR_PCTL_BACKUP_FAILED;
        failure_stage = "snapshot";
        verdict = "fail";
        goto raw_block_done;
    }
    captured = true;
    effect_snapshot_hex(before_opaque_hex, sizeof(before_opaque_hex), &original);
    final_error = backup_before_write(sysmodule, request, ptc_control_mode_name(config->mode));
    if (final_error != PTC_ERR_OK) {
        failure_stage = "backup";
        verdict = "fail";
        goto raw_block_done;
    }
    {
        PtcPctlTarget target;
        target.mode = PTC_PCTL_TARGET_BLOCKED;
        target.minutes = 0;
        target.weekday = ptc_weekday_from_day_index(now.day_index);
        final_error = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &target);
        append_event(sysmodule, request, final_error == PTC_ERR_OK ? "raw_block_apply" : "pctl_apply_failed", final_error, "apply_target");
        if (final_error != PTC_ERR_OK) {
            failure_stage = "write";
            verdict = "fail";
            goto raw_block_done;
        }
    }
    final_error = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
    append_event(sysmodule, request, final_error == PTC_ERR_OK ? "raw_block_apply" : "pctl_apply_failed", final_error, "start_timer");
    if (final_error != PTC_ERR_OK) {
        failure_stage = "start_timer";
        verdict = "fail";
        goto raw_block_done;
    }
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &active_snapshot) != PTC_ERR_OK) {
        final_error = PTC_ERR_PCTL_READ_FAILED;
        failure_stage = "raw_target";
        verdict = "fail";
        goto raw_block_done;
    }
    effect_snapshot_hex(active_opaque_hex, sizeof(active_opaque_hex), &active_snapshot);
    raw_target_written = !effect_snapshot_equal(&original, &active_snapshot);
    if (!raw_target_written) {
        final_error = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
        failure_stage = "raw_target";
        verdict = "fail";
        goto raw_block_done;
    }
    for (i = 0; i < 20U; ++i) {
        active_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &active_status);
        if (active_error == PTC_ERR_OK) {
            blocked_observed = active_status.restricted_now ||
                (active_status.remaining_available && active_status.remaining_minutes == 0U);
            if (blocked_observed) {
                break;
            }
        }
        effect_wait(sysmodule, 250);
    }
    if (active_error != PTC_ERR_OK || !blocked_observed) {
        final_error = active_error == PTC_ERR_OK ? PTC_ERR_PCTL_EFFECT_NOT_OBSERVED : active_error;
        failure_stage = "runtime_status";
        verdict = "fail";
    }
raw_block_done:
    if (captured && sysmodule->pctl->vtable->restore_settings) {
        restored_error = sysmodule->pctl->vtable->restore_settings(sysmodule->pctl, &original);
        if (restored_error == PTC_ERR_OK) {
            PtcErrorCode timer_error = original.timer_enabled
                ? sysmodule->pctl->vtable->start_timer(sysmodule->pctl)
                : sysmodule->pctl->vtable->stop_timer(sysmodule->pctl);
            if (timer_error != PTC_ERR_OK) {
                restored_error = timer_error;
            }
        }
        if (restored_error == PTC_ERR_OK && sysmodule->pctl->vtable->snapshot_settings) {
            restored_error = sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &restored_snapshot);
            raw_restored = effect_snapshot_equal(&original, &restored_snapshot);
            timer_restored = restored_snapshot.timer_enabled == original.timer_enabled;
            effect_snapshot_hex(restored_opaque_hex, sizeof(restored_opaque_hex), &restored_snapshot);
        }
        if (restored_error == PTC_ERR_OK) {
            restored_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &restored_status);
        }
        append_event(sysmodule, request, restored_error == PTC_ERR_OK && raw_restored && timer_restored ? "raw_block_restore" : "raw_block_restore_failed", restored_error, "restore");
        if (restored_error != PTC_ERR_OK || !raw_restored || !timer_restored) {
            /* Unlike a failed write, a failed restore can leave the console blocked.
               Fail open so the child is not locked out by a verification run. */
            final_error = PTC_ERR_PCTL_RESTORE_FAILED;
            failure_stage = "restore";
            verdict = "fail";
            caps->raw_block_verified = false;
            join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
            (void)sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, disable_path, "raw_block_restore_failed\n");
        }
    }
    if (final_error == PTC_ERR_OK && strcmp(verdict, "pass") == 0) {
        caps->raw_block_verified = true;
        if (!save_capabilities(sysmodule, caps, now.unix_seconds)) {
            final_error = PTC_ERR_STORAGE_WRITE_FAILED;
            failure_stage = "capability_persist";
            verdict = "fail";
            caps->raw_block_verified = false;
        }
    }
    append_event(sysmodule, request, final_error == PTC_ERR_OK ? "probe_ok" : "probe_failed", final_error, failure_stage);
    return write_raw_block_probe_result(sysmodule, request, ptc_control_mode_name(config->mode), false, final_error, caps, now,
        verdict, failure_stage, &before_status, before_error, &active_status, active_error,
        &restored_status, restored_error, raw_target_written, blocked_observed, raw_restored, timer_restored,
        before_opaque_hex, active_opaque_hex, restored_opaque_hex);
}

static bool __attribute__((unused)) process_probe(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcPolicyDecision decision = ptc_policy_decide(config->mode, disable_flag, request_operation(request->type), caps, false, config->allow_unlimited_to_limited);
    PtcProbeResult probe;
    PtcPctlDebugSnapshot before;
    PtcPctlDebugSnapshot after;
    uint32_t ipc_result;
    PtcErrorCode err;
    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    if (decision.dry_run) {
        return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), true, caps, now, false);
    }
    err = backup_before_write(sysmodule, request, ptc_control_mode_name(config->mode));
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
    }
    take_pctl_debug_snapshot(sysmodule, &before);
    if (request->type == PTC_REQUEST_PROBE_PLAY_TIMER_WRITE) {
        err = sysmodule->pctl->vtable->probe_play_timer_write
            ? sysmodule->pctl->vtable->probe_play_timer_write(sysmodule->pctl, &probe)
            : PTC_ERR_PCTL_WRITE_FAILED;
        caps->play_timer_write_verified = err == PTC_ERR_OK && probe.verified;
    } else {
        err = sysmodule->pctl->vtable->probe_suspend
            ? sysmodule->pctl->vtable->probe_suspend(sysmodule->pctl, &probe)
            : PTC_ERR_PCTL_READ_FAILED;
        caps->suspend_verified = err == PTC_ERR_OK && probe.verified;
    }
    ipc_result = last_pctl_ipc_result(sysmodule);
    take_pctl_debug_snapshot(sysmodule, &after);
    append_pctl_debug(sysmodule, request, request->type_text, ptc_control_mode_name(config->mode), NULL, err, ipc_result, &before, &after);
    append_event(sysmodule, request, err == PTC_ERR_OK ? "probe_ok" : "probe_failed", err, probe.detail);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
    }
    if (!save_capabilities(sysmodule, caps, now.unix_seconds)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, PTC_ERR_STORAGE_WRITE_FAILED, now.day_index, caps);
    }
    return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), false, caps, now, false);
}

static bool __attribute__((unused)) process_probe_apply_today_limit(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, const PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcPolicyDecision decision = ptc_policy_decide(config->mode, disable_flag, PTC_OPERATION_PROBE_APPLY_TODAY_LIMIT, caps, false, config->allow_unlimited_to_limited);
    PtcPctlTarget target;
    PtcPctlStatus before_status;
    PtcPctlStatus after_status;
    PtcPctlDebugSnapshot before_snapshot;
    PtcPctlDebugSnapshot after_write_snapshot;
    PtcPctlDebugSnapshot after_status_snapshot;
    PtcPctlDebugSnapshot start_before_snapshot;
    PtcPctlDebugSnapshot start_after_snapshot;
    PtcErrorCode before_status_error;
    PtcErrorCode after_status_error = PTC_ERR_PCTL_READ_FAILED;
    PtcErrorCode write_error = PTC_ERR_OK;
    PtcErrorCode start_timer_error = PTC_ERR_OK;
    PtcErrorCode final_error;
    uint32_t write_ipc_result = 0;
    uint32_t start_timer_ipc_result = 0;
    bool write_attempted = false;
    bool start_timer_called = false;
    bool ok;

    memset(&before_status, 0, sizeof(before_status));
    memset(&after_status, 0, sizeof(after_status));
    empty_pctl_debug_snapshot(&before_snapshot);
    empty_pctl_debug_snapshot(&after_write_snapshot);
    empty_pctl_debug_snapshot(&after_status_snapshot);
    empty_pctl_debug_snapshot(&start_before_snapshot);
    empty_pctl_debug_snapshot(&start_after_snapshot);

    target.mode = PTC_PCTL_TARGET_LIMIT;
    target.minutes = request->minutes == 0 ? 1 : request->minutes;
    target.weekday = ptc_weekday_from_day_index(now.day_index);

    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    if (decision.dry_run) {
        return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), true, caps, now, false);
    }

    before_status_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, target.weekday, &before_status);
    take_pctl_debug_snapshot(sysmodule, &before_snapshot);
    append_pctl_debug(
        sysmodule,
        request,
        "probe_apply_before",
        ptc_control_mode_name(config->mode),
        &target,
        before_status_error,
        last_pctl_ipc_result(sysmodule),
        &before_snapshot,
        NULL);
    if (before_status_error != PTC_ERR_OK) {
        ok = write_probe_apply_result(
            sysmodule,
            request,
            ptc_control_mode_name(config->mode),
            false,
            before_status_error,
            caps,
            now,
            &target,
            &before_status,
            before_status_error,
            &after_status,
            after_status_error,
            &before_snapshot,
            &after_write_snapshot,
            request->start_timer,
            false,
            start_timer_error,
            write_ipc_result,
            start_timer_ipc_result);
        append_event(sysmodule, request, "probe_failed", before_status_error, "probe_apply_before");
        append_event(sysmodule, request, "result_error", before_status_error, "");
        return ok;
    }

    final_error = backup_before_write(sysmodule, request, ptc_control_mode_name(config->mode));
    if (final_error == PTC_ERR_OK) {
        write_attempted = true;
        write_error = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &target);
        write_ipc_result = last_pctl_ipc_result(sysmodule);
        take_pctl_debug_snapshot(sysmodule, &after_write_snapshot);
        append_pctl_debug(
            sysmodule,
            request,
            "probe_apply_write",
            ptc_control_mode_name(config->mode),
            &target,
            write_error,
            write_ipc_result,
            &before_snapshot,
            &after_write_snapshot);
        append_event(sysmodule, request, write_error == PTC_ERR_OK ? "pctl_apply" : "pctl_apply_failed", write_error, "probe_apply_today_limit");
        final_error = write_error;
        if (write_error != PTC_ERR_OK) {
            append_pctl_debug(
                sysmodule,
                request,
                "probe_apply_after",
                ptc_control_mode_name(config->mode),
                &target,
                write_error,
                write_ipc_result,
                &before_snapshot,
                &after_write_snapshot);
            append_pctl_debug(
                sysmodule,
                request,
                "probe_apply_start_timer",
                ptc_control_mode_name(config->mode),
                &target,
                write_error,
                write_ipc_result,
                &after_write_snapshot,
                &after_write_snapshot);
        }
    }

    if (final_error == PTC_ERR_OK && request->start_timer) {
        start_timer_called = true;
        take_pctl_debug_snapshot(sysmodule, &start_before_snapshot);
        start_timer_error = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
        start_timer_ipc_result = last_pctl_ipc_result(sysmodule);
        take_pctl_debug_snapshot(sysmodule, &start_after_snapshot);
        append_pctl_debug(
            sysmodule,
            request,
            "probe_apply_start_timer",
            ptc_control_mode_name(config->mode),
            &target,
            start_timer_error,
            start_timer_ipc_result,
            &start_before_snapshot,
            &start_after_snapshot);
        append_event(sysmodule, request, start_timer_error == PTC_ERR_OK ? "pctl_start_timer" : "pctl_apply_failed", start_timer_error, "probe_apply_start_timer");
        if (start_timer_error != PTC_ERR_OK) {
            final_error = start_timer_error;
        }
    } else if (final_error == PTC_ERR_OK) {
        append_pctl_debug(
            sysmodule,
            request,
            "probe_apply_start_timer",
            ptc_control_mode_name(config->mode),
            &target,
            PTC_ERR_OK,
            0,
            &after_write_snapshot,
            &after_write_snapshot);
    }

    if (write_attempted && write_error == PTC_ERR_OK) {
        after_status_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, target.weekday, &after_status);
        take_pctl_debug_snapshot(sysmodule, &after_status_snapshot);
        append_pctl_debug(
            sysmodule,
            request,
            "probe_apply_after",
            ptc_control_mode_name(config->mode),
            &target,
            after_status_error,
            last_pctl_ipc_result(sysmodule),
            &before_snapshot,
            &after_status_snapshot);
        if (final_error == PTC_ERR_OK && after_status_error != PTC_ERR_OK) {
            final_error = after_status_error;
        }
    }

    ok = write_probe_apply_result(
        sysmodule,
        request,
        ptc_control_mode_name(config->mode),
        false,
        final_error,
        caps,
        now,
        &target,
        &before_status,
        before_status_error,
        &after_status,
        after_status_error,
        &before_snapshot,
        &after_write_snapshot,
        request->start_timer,
        start_timer_called,
        start_timer_error,
        write_ipc_result,
        start_timer_ipc_result);
    append_event(sysmodule, request, final_error == PTC_ERR_OK ? "probe_ok" : "probe_failed", final_error, "probe_apply_today_limit");
    append_event(sysmodule, request, final_error == PTC_ERR_OK ? "result_ok" : "result_error", final_error, "");
    return ok;
}

#endif

static bool process_disable_today_limit(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const PtcRuntimeConfig *config,
    bool disable_flag,
    PtcCapabilities *caps,
    PtcClockSnapshot now)
{
    PtcPolicyDecision decision;
    PtcPctlSettingsSnapshot original_snapshot;
    PtcPctlSettingsSnapshot restored_snapshot;
    PtcPctlStatus before_status;
    PtcPctlStatus observed_status;
    PtcPctlStatus restored_status;
    PtcRules original_rules;
    PtcRules updated_rules;
    PtcRuntimeState runtime_state;
    PtcErrorCode err;
    PtcErrorCode restore_error = PTC_ERR_OK;
    bool rules_existed;
    bool rules_persisted = false;
    bool pctl_changed = false;
    bool raw_restored = false;
    bool timer_restored = false;
    char rules_path[320];

    memset(&original_snapshot, 0, sizeof(original_snapshot));
    memset(&restored_snapshot, 0, sizeof(restored_snapshot));
    memset(&before_status, 0, sizeof(before_status));
    memset(&observed_status, 0, sizeof(observed_status));
    memset(&restored_status, 0, sizeof(restored_status));
    if (disable_flag) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_DISABLED, now.day_index, caps);
    }
    if (!load_rules(sysmodule, &original_rules)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_RULES_INVALID, now.day_index, caps);
    }
    if (!load_state(sysmodule, &runtime_state)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_BAD_REQUEST, now.day_index, caps);
    }
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &before_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, err, now.day_index, caps);
    }
    decision = ptc_policy_decide(config->mode, disable_flag, PTC_OPERATION_DISABLE_TODAY_LIMIT,
        caps, before_status.unrestricted_today, config->allow_unlimited_to_limited);
    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    if (decision.dry_run) {
        return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), true, caps, now, false);
    }
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &original_snapshot) != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, PTC_ERR_PCTL_BACKUP_FAILED, now.day_index, caps);
    }

    updated_rules = original_rules;
    updated_rules.today_override.present = true;
    updated_rules.today_override.day_index = now.day_index;
    updated_rules.today_override.rule.mode = PTC_RULE_MODE_UNLIMITED;
    updated_rules.today_override.rule.minutes = 0;
    join_path(rules_path, sizeof(rules_path), sysmodule->app_root, "rules.json");
    rules_existed = sysmodule->storage->vtable->exists(sysmodule->storage, rules_path);

    pctl_changed = true;
    err = apply_target(sysmodule, request, caps, now, ptc_control_mode_name(config->mode), PTC_PCTL_TARGET_UNLIMITED, 0);
    if (err == PTC_ERR_OK) {
        err = start_timer_and_wait_target(sysmodule, request, now, ptc_control_mode_name(config->mode),
            PTC_PCTL_TARGET_UNLIMITED, 0, "disable_today_limit", &observed_status, NULL);
    }
    if (err != PTC_ERR_OK) {
        goto disable_today_rollback;
    }
    if (!save_rules(sysmodule, &updated_rules)) {
        err = PTC_ERR_STORAGE_WRITE_FAILED;
        goto disable_today_rollback;
    }
    rules_persisted = true;
    append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "disable_today_limit");
    if (write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), false, caps, now, true)) {
        recovery_clear(sysmodule);
        return true;
    }
    err = PTC_ERR_STORAGE_WRITE_FAILED;
    append_event(sysmodule, request, "result_write_failed", err, "disable_today_limit");

disable_today_rollback:
    if (pctl_changed) {
        restore_error = restore_snapshot_exact(sysmodule, &original_snapshot, &restored_snapshot, &restored_status,
            ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
        append_event(sysmodule, request,
            restore_error == PTC_ERR_OK ? "effect_restore" : "effect_restore_failed",
            restore_error, "disable_today_limit");
    }
    if (rules_persisted) {
        if (!restore_rules(sysmodule, &original_rules, rules_existed)) {
            restore_error = PTC_ERR_STORAGE_WRITE_FAILED;
        }
    }
    if (restore_error != PTC_ERR_OK) {
        err = PTC_ERR_PCTL_RESTORE_FAILED;
        write_disable_flag(sysmodule, "transaction_restore_failed\n");
    } else {
        recovery_clear(sysmodule);
    }
    return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
}

static bool process_rule_request(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, const PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcPctlStatus pctl_status;
    PtcPctlStatus observed_status;
    PtcPolicyDecision decision;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcErrorCode err;
    PtcDayRule active_rule;
    bool pctl_request = request->type == PTC_REQUEST_SET_TODAY_LIMIT ||
        request->type == PTC_REQUEST_ADD_TODAY_MINUTES ||
        request->type == PTC_REQUEST_RESTORE_TODAY_POLICY ||
        request->type == PTC_REQUEST_SET_HOLIDAY_POLICY;
    if (disable_flag) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_DISABLED, now.day_index, caps);
    }
    if (!load_rules(sysmodule, &rules)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_RULES_INVALID, now.day_index, caps);
    }
    if (!load_state(sysmodule, &runtime_state)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_BAD_REQUEST, now.day_index, caps);
    }
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, err, now.day_index, caps);
    }
    decision = ptc_policy_decide(config->mode, disable_flag, request_operation(request->type), caps, pctl_status.unrestricted_today, config->allow_unlimited_to_limited);
    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    if (!decision.dry_run) {
        if (pctl_request && !recovery_begin(sysmodule, request, now)) {
            return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false,
                PTC_ERR_PCTL_BACKUP_FAILED, now.day_index, caps);
        }
        uint16_t played_minutes = ptc_pctl_played_minutes(&pctl_status);
        err = update_rules_for_request(sysmodule, request, &rules, &runtime_state, now, played_minutes);
        if (err != PTC_ERR_OK) {
            if (pctl_request && recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule)) {
                write_disable_flag(sysmodule, "transaction_restore_failed\n");
                err = PTC_ERR_RECOVERY_FAILED;
            }
            return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
        }
        append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "");
        if (request->type == PTC_REQUEST_SET_TODAY_LIMIT ||
            request->type == PTC_REQUEST_ADD_TODAY_MINUTES ||
            request->type == PTC_REQUEST_DISABLE_TODAY_LIMIT ||
            request->type == PTC_REQUEST_RESTORE_TODAY_POLICY ||
            request->type == PTC_REQUEST_SET_HOLIDAY_POLICY) {
            active_rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
            err = apply_target(sysmodule, request, caps, now, ptc_control_mode_name(config->mode), target_from_day_rule(active_rule), active_rule.minutes);
            if (err != PTC_ERR_OK) {
                return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
            }
            err = start_timer_and_wait_target(sysmodule, request, now, ptc_control_mode_name(config->mode),
                target_from_day_rule(active_rule), active_rule.minutes, "rule_request", &observed_status, NULL);
            if (err != PTC_ERR_OK) {
                if (!recovery_rollback(sysmodule)) {
                    write_disable_flag(sysmodule, "transaction_restore_failed\n");
                    err = PTC_ERR_RECOVERY_FAILED;
                }
                return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
            }
        }
    }
    {
        bool ok = write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode),
            decision.dry_run, caps, now, !decision.dry_run && pctl_request && recovery_path_exists(sysmodule));
        if (ok) recovery_clear(sysmodule);
        else if (recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule))
            write_disable_flag(sysmodule, "transaction_restore_failed\n");
        return ok;
    }
}

static void process_request_text(PtcSysmodule *sysmodule, const char *request_text, const char *expected_request_id)
{
    PtcRequest request;
    PtcRuntimeConfig config;
    PtcCapabilities caps;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    PtcErrorCode parse_err;
    bool disable_flag;
    char disable_path[320];

    parse_err = ptc_request_parse(request_text, &request);
    if (parse_err == PTC_ERR_OK && expected_request_id && strcmp(request.request_id, expected_request_id) != 0) {
        parse_err = PTC_ERR_BAD_REQUEST;
    }
    if (parse_err != PTC_ERR_OK) {
        memset(&request, 0, sizeof(request));
        snprintf(request.request_id, sizeof(request.request_id), "unknown");
        snprintf(request.type_text, sizeof(request.type_text), "unknown");
        if (expected_request_id && ptc_request_id_is_valid(expected_request_id)) {
            snprintf(request.request_id, sizeof(request.request_id), "%s", expected_request_id);
        } else {
            (void)json_string(request_text, "request_id", request.request_id, sizeof(request.request_id));
            if (!ptc_request_id_is_valid(request.request_id)) snprintf(request.request_id, sizeof(request.request_id), "unknown");
        }
        (void)json_string(request_text, "type", request.type_text, sizeof(request.type_text));
        (void)finish_with_error(sysmodule, &request, "release", true, parse_err, now.day_index, NULL);
        return;
    }
    append_event(sysmodule, &request, "request_received", PTC_ERR_OK, "");
    if (!load_config(sysmodule, &config)) {
        (void)finish_with_error(sysmodule, &request, "release", true, PTC_ERR_CONFIG_INVALID, now.day_index, NULL);
        return;
    }
    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    disable_flag = sysmodule->storage->vtable->exists(sysmodule->storage, disable_path);
    caps = load_capabilities(sysmodule);

    if (request.type != PTC_REQUEST_STATUS &&
        request.type != PTC_REQUEST_COMPLETE_SETUP &&
        request.type != PTC_REQUEST_RETRY_SETUP_RELEASE &&
        request.type != PTC_REQUEST_RESTORE_INSTALL_SNAPSHOT) {
        PtcSetupState setup;
        if (!load_setup_state(sysmodule, &setup) || strcmp(setup.phase, "active") != 0) {
            (void)finish_with_error(sysmodule, &request, ptc_control_mode_name(config.mode), true,
                PTC_ERR_SETUP_PENDING, now.day_index, &caps);
            return;
        }
    }

    switch (request.type) {
#ifdef PLAYWISE_DEVICE_LAB
    case PTC_REQUEST_LAB_SESSION_START:
    case PTC_REQUEST_LAB_PHASE_START:
    case PTC_REQUEST_LAB_SESSION_STATUS:
    case PTC_REQUEST_LAB_OBSERVATION:
    case PTC_REQUEST_LAB_SESSION_RESTORE:
        (void)ptc_lab_process_request(sysmodule, &request);
        break;
#endif
    case PTC_REQUEST_STATUS:
        (void)process_status(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_OFFLINE_CODE:
        (void)process_offline_code(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_PREVIEW_OFFLINE_CODE:
        (void)process_preview_offline_code(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_COMPLETE_SETUP:
        (void)process_complete_setup(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_RETRY_SETUP_RELEASE:
        (void)process_retry_setup_release(sysmodule, &request, &config, &caps, now);
        break;
    case PTC_REQUEST_RESTORE_INSTALL_SNAPSHOT:
        (void)process_restore_install_snapshot(sysmodule, &request, &config, &caps, now);
        break;
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
        (void)process_disable_today_limit(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_SET_TODAY_LIMIT:
    case PTC_REQUEST_ADD_TODAY_MINUTES:
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
    case PTC_REQUEST_SET_HOLIDAY_POLICY:
        (void)process_rule_request(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
#ifdef PLAYWISE_DEVICE_LAB
    case PTC_REQUEST_PROBE_RAW_BLOCK:
        (void)process_probe_raw_block(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_PROBE_SUSPEND:
    case PTC_REQUEST_PROBE_PLAY_TIMER_WRITE:
        (void)process_probe(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_PROBE_APPLY_TODAY_LIMIT:
        (void)process_probe_apply_today_limit(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_PROBE_PLAY_TIMER_EFFECT:
        (void)process_probe_play_timer_effect(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_PREPARE_DEVICE_TEST:
        (void)process_prepare_device_test(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
#endif
    case PTC_REQUEST_UNKNOWN:
    default:
        (void)finish_with_error(sysmodule, &request, ptc_control_mode_name(config.mode), true, PTC_ERR_UNKNOWN_REQUEST_TYPE, now.day_index, &caps);
        break;
    }
}

#ifdef PLAYWISE_EDEN
void ptc_sysmodule_process_request_direct(PtcSysmodule *sysmodule,
    const char *request_text, const char *expected_request_id)
{
    if (!sysmodule || !request_text) return;
    process_request_text(sysmodule, request_text, expected_request_id);
}
#endif

int ptc_sysmodule_enforce_tick(PtcSysmodule *sysmodule)
{
    PtcRuntimeConfig config;
    PtcCapabilities caps;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    PtcDayRule active_rule;
    PtcPctlStatus observed_status;
    PtcPctlTargetMode target_mode;
    uint16_t target_minutes;
    char disable_path[320];
    PtcSetupState setup;
    PtcErrorCode err;

    if (!load_config(sysmodule, &config) || config.mode != PTC_CONTROL_ENFORCE) {
        return 0;
    }
    if (!load_setup_state(sysmodule, &setup) || strcmp(setup.phase, "active") != 0) {
        return 0;
    }
    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, disable_path)) {
        return 0;
    }
    caps = load_capabilities(sysmodule);
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &runtime_state)) {
        append_event(sysmodule, NULL, "result_error", PTC_ERR_RULES_INVALID, "enforce");
        return 0;
    }
    if (runtime_state.apply_pending_confirmation) {
        PtcPctlStatus pending_status;
        PtcErrorCode pending_err = sysmodule->pctl->vtable->read_status(
            sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pending_status);
        if (pending_err == PTC_ERR_OK && target_settings_observed(
                runtime_state.pending_mode, runtime_state.pending_minutes, &pending_status)) {
            runtime_state.apply_pending_confirmation = false;
            runtime_state.apply_confirmation_deadline = 0;
            runtime_state.pending_mode = 0;
            runtime_state.pending_minutes = 0;
            if (!save_state(sysmodule, &runtime_state, now.unix_seconds)) {
                write_disable_flag(sysmodule, "pending_confirmation_state_failed\n");
                return 0;
            }
            append_event(sysmodule, NULL, "effect_observed", PTC_ERR_OK, "enforce_pending_confirmation");
            recovery_clear(sysmodule);
        } else if (now.unix_seconds < runtime_state.apply_confirmation_deadline) {
            return 0;
        } else {
            append_event(sysmodule, NULL, "pctl_apply_failed", PTC_ERR_PCTL_EFFECT_NOT_OBSERVED,
                "enforce_pending_confirmation_timeout");
            if (!recovery_rollback(sysmodule)) {
                write_disable_flag(sysmodule, "pending_confirmation_restore_failed\n");
            }
            return 0;
        }
    }
    active_rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    target_mode = target_from_day_rule(active_rule);
    target_minutes = active_rule.minutes;
    if (runtime_state.last_enforced_day_index == now.day_index &&
        runtime_state.last_enforced_mode == target_mode &&
        runtime_state.last_enforced_minutes == target_minutes) {
        return 0;
    }
    err = apply_target(sysmodule, NULL, &caps, now, ptc_control_mode_name(config.mode), target_mode, target_minutes);
    if (err != PTC_ERR_OK) {
        return 0;
    }
    {
        unsigned int i;
        for (i = 0; i < 20U; ++i) {
            err = sysmodule->pctl->vtable->read_status(
                sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &observed_status);
            if (err == PTC_ERR_OK && target_settings_observed(target_mode, target_minutes, &observed_status)) {
                append_event(sysmodule, NULL, "effect_observed", PTC_ERR_OK, "enforce_settings");
                break;
            }
            if (i + 1U < 20U) {
                effect_wait(sysmodule, 250U);
            }
        }
        if (err == PTC_ERR_OK && !target_settings_observed(target_mode, target_minutes, &observed_status)) {
            err = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
            append_event(sysmodule, NULL, "pctl_apply_failed", err, "enforce_settings");
        }
    }
    if (err != PTC_ERR_OK) {
        if (err != PTC_ERR_PCTL_EFFECT_NOT_OBSERVED) {
            if (!recovery_rollback(sysmodule)) write_disable_flag(sysmodule, "enforce_restore_failed\n");
            return 0;
        }
        runtime_state.apply_pending_confirmation = true;
        runtime_state.apply_confirmation_deadline = now.unix_seconds + 30;
        runtime_state.pending_mode = target_mode;
        runtime_state.pending_minutes = target_minutes;
        append_event(sysmodule, NULL, "enforce_effect_pending", PTC_ERR_OK, "applied_pending_confirmation");
    } else {
        runtime_state.apply_pending_confirmation = false;
        runtime_state.apply_confirmation_deadline = 0;
        runtime_state.pending_mode = 0;
        runtime_state.pending_minutes = 0;
    }
    runtime_state.last_enforced_day_index = now.day_index;
    runtime_state.last_enforced_mode = target_mode;
    runtime_state.last_enforced_minutes = target_minutes;
    if (!save_state(sysmodule, &runtime_state, now.unix_seconds)) {
        append_event(sysmodule, NULL, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "enforce_state");
        if (!recovery_rollback(sysmodule)) write_disable_flag(sysmodule, "enforce_restore_failed\n");
        return 0;
    }
    append_event(sysmodule, NULL, "state_persisted", PTC_ERR_OK, "enforce");
    if (!runtime_state.apply_pending_confirmation) recovery_clear(sysmodule);
    return 1;
}

void ptc_sysmodule_init(
    PtcSysmodule *sysmodule,
    const char *app_root,
    PtcStorage *storage,
    PtcPctl *pctl,
    PtcTimeProvider *time_provider)
{
    snprintf(sysmodule->app_root, sizeof(sysmodule->app_root), "%s", app_root);
    sysmodule->storage = storage;
    sysmodule->pctl = pctl;
    sysmodule->time_provider = time_provider;
    sysmodule->scan_backoff_ms = 500;
    sysmodule->minute_initialized = false;
    sysmodule->cleanup_initialized = false;
    sysmodule->startup_recovery_checked = false;
    sysmodule->disable_initialized = false;
    sysmodule->disable_present = false;
    snprintf(sysmodule->boot_id, sizeof(sysmodule->boot_id), "host-boot");
    invalidate_all_caches(sysmodule);
    (void)ptc_sysmodule_refresh_caches(sysmodule);
}

void ptc_sysmodule_set_boot_id(PtcSysmodule *sysmodule, const char *boot_id)
{
    size_t i;
    if (!sysmodule || !boot_id || !boot_id[0]) return;
    for (i = 0; boot_id[i] && i + 1 < sizeof(sysmodule->boot_id); ++i) {
        char ch = boot_id[i];
        sysmodule->boot_id[i] = ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-') ? ch : '-';
    }
    sysmodule->boot_id[i] = '\0';
}

int ptc_sysmodule_recover_processing(PtcSysmodule *sysmodule)
{
    char dir[320];
    char names[32][128];
    size_t count = 0;
    size_t i;
    int recovered = 0;
    snprintf(dir, sizeof(dir), "%s/inbox/processing", sysmodule->app_root);
    if (!sysmodule->storage->vtable->list_json(sysmodule->storage, dir, names, 32, &count)) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        char from[320];
        char to[320];
        if (!request_file_path(from, sizeof(from), sysmodule, "processing", names[i]) ||
            !request_file_path(to, sizeof(to), sysmodule, "pending", names[i])) {
            continue;
        }
        if (sysmodule->storage->vtable->rename_path(sysmodule->storage, from, to)) {
            ++recovered;
        }
    }
    return recovered;
}

int ptc_sysmodule_process_all(PtcSysmodule *sysmodule)
{
    char dir[320];
    char names[32][128];
    size_t count = 0;
    size_t i;
    int processed = 0;
    snprintf(dir, sizeof(dir), "%s/inbox/pending", sysmodule->app_root);
    if (!sysmodule->storage->vtable->list_json(sysmodule->storage, dir, names, 32, &count)) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        char pending[320];
        char processing[320];
        char done[320];
        char text[4096];
        char request_id[80];
        if (!request_file_stem(names[i], request_id, sizeof(request_id))) continue;
        if (!request_file_path(pending, sizeof(pending), sysmodule, "pending", names[i]) ||
            !request_file_path(processing, sizeof(processing), sysmodule, "processing", names[i]) ||
            !request_file_path(done, sizeof(done), sysmodule, "done", names[i])) {
            continue;
        }
        if (!sysmodule->storage->vtable->rename_path(sysmodule->storage, pending, processing)) {
            continue;
        }
        if (sysmodule->storage->vtable->read_text(sysmodule->storage, processing, text, sizeof(text))) {
            process_request_text(sysmodule, text, request_id);
        }
        (void)sysmodule->storage->vtable->rename_path(sysmodule->storage, processing, done);
        ++processed;
    }
    return processed;
}

uint32_t ptc_sysmodule_note_scan_result(PtcSysmodule *sysmodule, bool found_work)
{
    if (!sysmodule) return 5000;
    if (found_work) {
        sysmodule->scan_backoff_ms = 500;
    } else if (sysmodule->scan_backoff_ms < 1000) {
        sysmodule->scan_backoff_ms = 1000;
    } else if (sysmodule->scan_backoff_ms < 2000) {
        sysmodule->scan_backoff_ms = 2000;
    } else {
        sysmodule->scan_backoff_ms = 5000;
    }
    return sysmodule->scan_backoff_ms;
}

uint32_t ptc_sysmodule_current_scan_interval(const PtcSysmodule *sysmodule)
{
    return sysmodule && sysmodule->scan_backoff_ms ? sysmodule->scan_backoff_ms : 500;
}

uint32_t ptc_sysmodule_next_wait_ms(PtcSysmodule *sysmodule)
{
    PtcClockSnapshot now;
    int64_t shifted;
    int64_t second_of_day;
    uint32_t minute_ms;
    uint64_t date_ms;
    uint32_t wait_ms;
    if (!sysmodule) return 500;
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    shifted = now.unix_seconds + PTC_UTC8_OFFSET_SECONDS;
    second_of_day = shifted % PTC_SECONDS_PER_DAY;
    if (second_of_day < 0) second_of_day += PTC_SECONDS_PER_DAY;
    minute_ms = (uint32_t)(60 - (second_of_day % 60)) * 1000u;
    date_ms = (uint64_t)(PTC_SECONDS_PER_DAY - second_of_day) * 1000u;
    wait_ms = ptc_sysmodule_current_scan_interval(sysmodule);
    if (minute_ms < wait_ms) wait_ms = minute_ms;
    if (date_ms < wait_ms) wait_ms = date_ms;
#ifdef PLAYWISE_DEVICE_LAB
    wait_ms = ptc_lab_next_wait_ms(sysmodule, wait_ms);
#endif
    return wait_ms ? wait_ms : 1u;
}

static bool date_directory_day_index(const char *name, uint16_t *out)
{
    unsigned int year;
    unsigned int month;
    unsigned int day;
    char tail;
    if (!name || strlen(name) != 10 || sscanf(name, "%4u-%2u-%2u%c", &year, &month, &day, &tail) != 3) return false;
    return ptc_day_index_from_date((uint16_t)year, (uint8_t)month, (uint8_t)day, out);
}

/* entries is supplied by the caller: a PtcStorageEntry[256] costs ~38 KiB, so a
   nested copy here plus the caller's own array overflows the main thread stack. */
static bool cleanup_timestamped_json(
    PtcSysmodule *sysmodule,
    const char *relative_dir,
    uint16_t today,
    PtcStorageEntry *entries,
    size_t entry_capacity)
{
    char dir[320];
    size_t count = 0;
    size_t i;
    bool ok = true;
    snprintf(dir, sizeof(dir), "%s/%s", sysmodule->app_root, relative_dir);
    if (!sysmodule->storage->vtable->list_entries(sysmodule->storage, dir, entries, entry_capacity, &count)) return true;
    for (i = 0; i < count; ++i) {
        char stem[80];
        char path[512];
        uint16_t file_day;
        if (entries[i].type != PTC_STORAGE_ENTRY_FILE || !request_file_stem(entries[i].name, stem, sizeof(stem)) ||
            !entries[i].modified_time_valid || entries[i].modified_unix_seconds < PTC_DAY_INDEX_EPOCH_UNIX - PTC_UTC8_OFFSET_SECONDS) continue;
        file_day = ptc_day_index_from_unix_utc8(entries[i].modified_unix_seconds);
        if (file_day > today || (uint16_t)(today - file_day) < 30u) continue;
        if (strlen(dir) + 1u + strlen(entries[i].name) >= sizeof(path)) continue;
        memcpy(path, dir, strlen(dir));
        path[strlen(dir)] = '/';
        memcpy(path + strlen(dir) + 1u, entries[i].name, strlen(entries[i].name) + 1u);
        if (!sysmodule->storage->vtable->remove_path(sysmodule->storage, path)) ok = false;
    }
    return ok;
}

int ptc_sysmodule_cleanup(PtcSysmodule *sysmodule)
{
    char logs_dir[320];
    /* Reused across the log scan and both cleanup_timestamped_json calls so the
       ~38 KiB entry array is only reserved once on the stack. */
    PtcStorageEntry entries[PTC_CLEANUP_MAX_ENTRIES];
    size_t count = 0;
    size_t i;
    bool ok = true;
    PtcClockSnapshot now;
    if (!sysmodule || !sysmodule->storage->vtable->list_entries || !sysmodule->storage->vtable->remove_tree) return 0;
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    snprintf(logs_dir, sizeof(logs_dir), "%s/logs", sysmodule->app_root);
    if (sysmodule->storage->vtable->list_entries(sysmodule->storage, logs_dir, entries, PTC_CLEANUP_MAX_ENTRIES, &count)) {
        for (i = 0; i < count; ++i) {
            uint16_t day_index;
            char path[512];
            if (entries[i].type != PTC_STORAGE_ENTRY_DIRECTORY || !date_directory_day_index(entries[i].name, &day_index) ||
                day_index > now.day_index || (uint16_t)(now.day_index - day_index) < 30u) continue;
            if (strlen(logs_dir) + 1u + strlen(entries[i].name) >= sizeof(path)) continue;
            memcpy(path, logs_dir, strlen(logs_dir));
            path[strlen(logs_dir)] = '/';
            memcpy(path + strlen(logs_dir) + 1u, entries[i].name, strlen(entries[i].name) + 1u);
            if (!sysmodule->storage->vtable->remove_tree(sysmodule->storage, path)) ok = false;
        }
    }
    if (!cleanup_timestamped_json(sysmodule, "results", now.day_index, entries, PTC_CLEANUP_MAX_ENTRIES)) ok = false;
    if (!cleanup_timestamped_json(sysmodule, "inbox/done", now.day_index, entries, PTC_CLEANUP_MAX_ENTRIES)) ok = false;
    if (!ok) append_event(sysmodule, NULL, "cleanup_failed", PTC_ERR_STORAGE_WRITE_FAILED, "retention");
    sysmodule->last_cleanup_day_index = now.day_index;
    sysmodule->cleanup_initialized = true;
    return ok ? 1 : 0;
}

int ptc_sysmodule_rollover_legacy_logs(PtcSysmodule *sysmodule)
{
    static const char *NAMES[] = { "events.jsonl", "pctl_debug.jsonl", "sysmodule.log" };
    char date[11];
    PtcClockSnapshot now;
    size_t i;
    int moved = 0;
    if (!sysmodule) return 0;
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    if (!ptc_format_date_utc8(now.unix_seconds, date)) return 0;
    for (i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); ++i) {
        char from[320];
        char to[352];
        snprintf(from, sizeof(from), "%s/logs/%s", sysmodule->app_root, NAMES[i]);
        if (!sysmodule->storage->vtable->exists(sysmodule->storage, from)) continue;
        snprintf(to, sizeof(to), "%s/logs/%s/legacy-%lld-%s", sysmodule->app_root, date,
            (long long)now.unix_seconds, NAMES[i]);
        if (sysmodule->storage->vtable->rename_path(sysmodule->storage, from, to)) ++moved;
    }
    return moved;
}

int ptc_sysmodule_scheduler_tick(PtcSysmodule *sysmodule, bool storage_notified)
{
    char disable_path[320];
    char reload_path[320];
    PtcClockSnapshot now;
    bool disable_changed;
    bool disable_present;
    bool minute_changed;
    bool reload;
    int processed;
    int actions = 0;
    if (!sysmodule) return 0;
#ifdef PLAYWISE_DEVICE_LAB
    actions += ptc_lab_scheduler_tick(sysmodule);
#endif
    {
        int setup_actions = ptc_sysmodule_bootstrap_setup(sysmodule);
        if (setup_actions > 0) actions += setup_actions;
    }
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    snprintf(disable_path, sizeof(disable_path), "%s/flags/disable.flag", sysmodule->app_root);
    snprintf(reload_path, sizeof(reload_path), "%s/flags/reload.flag", sysmodule->app_root);
    disable_present = sysmodule->storage->vtable->exists(sysmodule->storage, disable_path);
    disable_changed = !sysmodule->disable_initialized || sysmodule->disable_present != disable_present;
    reload = sysmodule->storage->vtable->exists(sysmodule->storage, reload_path);
    if (reload) {
        invalidate_all_caches(sysmodule);
        (void)ptc_sysmodule_refresh_caches(sysmodule);
    }
#ifdef PLAYWISE_EDEN
    /* Eden's Windows-backed SD implementation can copy pending files to the
       processing directory while failing to remove the source. Requests use
       the synchronous in-process bridge instead, so never rescan those stale
       queue artifacts in the emulator profile. */
    processed = 0;
#else
    processed = ptc_sysmodule_process_all(sysmodule);
#endif
    (void)ptc_sysmodule_note_scan_result(sysmodule, processed > 0 || storage_notified || reload || disable_changed);
    actions += processed;
    minute_changed = !sysmodule->minute_initialized || sysmodule->last_minute_day_index != now.day_index ||
        sysmodule->last_minute_of_day != now.minute_of_day;
    if (minute_changed || reload || storage_notified || disable_changed) {
#ifndef PLAYWISE_DEVICE_LAB
        actions += ptc_sysmodule_enforce_tick(sysmodule);
#endif
        sysmodule->last_minute_day_index = now.day_index;
        sysmodule->last_minute_of_day = now.minute_of_day;
        sysmodule->minute_initialized = true;
    }
    sysmodule->disable_present = disable_present;
    sysmodule->disable_initialized = true;
    if (reload) (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, reload_path);
    if (!sysmodule->cleanup_initialized || sysmodule->last_cleanup_day_index != now.day_index) {
        actions += ptc_sysmodule_cleanup(sysmodule);
    }
    return actions;
}
