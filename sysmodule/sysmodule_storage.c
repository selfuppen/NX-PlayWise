#include "sysmodule_internal.h"

void join_path(char *out, size_t out_size, const char *a, const char *b)
{
    snprintf(out, out_size, "%s/%s", a, b);
}

bool daily_log_path(PtcSysmodule *sysmodule, const char *name, char *out, size_t out_size)
{
    char date[11];
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    if (now.unix_seconds < PTC_DAY_INDEX_EPOCH_UNIX || !ptc_format_date(now.day_index, date)) {
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

void invalidate_all_caches(PtcSysmodule *sysmodule)
{
    sysmodule->config_cache_valid = false;
    sysmodule->rules_cache_valid = false;
    sysmodule->state_cache_valid = false;
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

bool json_string(const char *text, const char *key, char *out, size_t out_size)
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

static bool json_u64(const char *text, const char *key, uint64_t *out)
{
    const char *pos = find_key(text, key);
    char *endptr;
    unsigned long long value;
    if (!pos) return false;
    pos = strchr(pos + strlen(key) + 2, ':');
    if (!pos) return false;
    pos = skip_ws(pos + 1);
    if (*pos == '-') return false;
    value = strtoull(pos, &endptr, 10);
    if (endptr == pos) return false;
    *out = (uint64_t)value;
    return true;
}

bool json_bool_value(const char *text, const char *key, bool *out)
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

bool load_setup_state(PtcSysmodule *sysmodule, PtcSetupState *setup)
{
    char path[320];
    char text[2048];
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

bool save_setup_state(PtcSysmodule *sysmodule, const PtcSetupState *setup)
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

bool save_install_snapshot(PtcSysmodule *sysmodule, const PtcPctlSettingsSnapshot *snapshot, int64_t captured_at)
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

bool load_install_snapshot(PtcSysmodule *sysmodule, PtcPctlSettingsSnapshot *snapshot)
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

bool save_bedtime_snapshot(PtcSysmodule *sysmodule, const PtcPctlSettingsSnapshot *snapshot,
    const PtcBedtimeEvaluation *evaluation, int64_t captured_at)
{
    char path[320];
    char meta[320];
    if (!snapshot || !evaluation || !evaluation->active ||
        !save_snapshot_file(sysmodule, "backups/bedtime_pctl_snapshot.json", snapshot, captured_at)) return false;
    join_path(path, sizeof(path), sysmodule->app_root, "backups/bedtime_active.json");
    snprintf(meta, sizeof(meta),
        "{\"version\":1,\"window_instance_id\":%llu,\"start_day_index\":%u,"
        "\"captured_at\":%lld}\n",
        (unsigned long long)evaluation->window_instance_id, evaluation->start_day_index,
        (long long)captured_at);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, meta);
}

bool load_bedtime_snapshot(PtcSysmodule *sysmodule, PtcPctlSettingsSnapshot *snapshot,
    uint64_t *window_instance_id, uint16_t *start_day_index)
{
    char path[320];
    char meta[512];
    int64_t version;
    join_path(path, sizeof(path), sysmodule->app_root, "backups/bedtime_active.json");
    return sysmodule->storage->vtable->read_text(sysmodule->storage, path, meta, sizeof(meta)) &&
        json_i64(meta, "version", &version) && version == 1 &&
        json_u64(meta, "window_instance_id", window_instance_id) &&
        json_u16(meta, "start_day_index", start_day_index) &&
        load_snapshot_file(sysmodule, "backups/bedtime_pctl_snapshot.json", snapshot);
}

void clear_bedtime_snapshot(PtcSysmodule *sysmodule)
{
    char path[320];
    join_path(path, sizeof(path), sysmodule->app_root, "backups/bedtime_active.json");
    (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, path);
    join_path(path, sizeof(path), sysmodule->app_root, "backups/bedtime_pctl_snapshot.json");
    (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, path);
}

bool recovery_path_exists(PtcSysmodule *sysmodule)
{
    char path[320];
    join_path(path, sizeof(path), sysmodule->app_root, "recovery/active/meta.json");
    return sysmodule->storage->vtable->exists(sysmodule->storage, path);
}

static bool backup_text_file(PtcSysmodule *sysmodule, const char *relative, const char *backup_relative, bool *existed)
{
    char source[320];
    char backup[320];
    char text[PTC_ACTIVITY_HISTORY_FILE_SIZE];
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
    char text[PTC_ACTIVITY_HISTORY_FILE_SIZE];
    join_path(target, sizeof(target), sysmodule->app_root, relative);
    if (!existed) {
        return !sysmodule->storage->vtable->exists(sysmodule->storage, target) ||
            sysmodule->storage->vtable->remove_path(sysmodule->storage, target);
    }
    join_path(backup, sizeof(backup), sysmodule->app_root, backup_relative);
    return sysmodule->storage->vtable->read_text(sysmodule->storage, backup, text, sizeof(text)) &&
        sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, target, text);
}

bool recovery_begin(PtcSysmodule *sysmodule, const PtcRequest *request, PtcClockSnapshot now)
{
    PtcPctlSettingsSnapshot snapshot;
    char meta_path[320];
    char meta[512];
    bool rules_existed;
    bool state_existed;
    bool ledger_existed;
    bool redemption_history_existed;
    bool activity_history_existed;
    if (recovery_path_exists(sysmodule)) return true;
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &snapshot) != PTC_ERR_OK ||
        !save_snapshot_file(sysmodule, "recovery/active/pctl_snapshot.json", &snapshot, now.unix_seconds) ||
        !backup_text_file(sysmodule, "rules.json", "recovery/active/rules.before", &rules_existed) ||
        !backup_text_file(sysmodule, "state.json", "recovery/active/state.before", &state_existed) ||
        !backup_text_file(sysmodule, "ledger/used_nonces.jsonl", "recovery/active/ledger.before", &ledger_existed) ||
        !backup_text_file(sysmodule, "ledger/redemption-history.jsonl",
            "recovery/active/redemption-history.before", &redemption_history_existed) ||
        !backup_text_file(sysmodule, "activity/history.jsonl",
            "recovery/active/activity-history.before", &activity_history_existed)) {
        recovery_clear(sysmodule);
        return false;
    }
    join_path(meta_path, sizeof(meta_path), sysmodule->app_root, "recovery/active/meta.json");
    snprintf(meta, sizeof(meta),
        "{\"version\":1,\"request_id\":\"%s\",\"created_at\":%lld,"
        "\"rules_existed\":%s,\"state_existed\":%s,\"ledger_existed\":%s,"
        "\"redemption_history_existed\":%s,\"activity_history_existed\":%s}\n",
        request && ptc_request_id_is_valid(request->request_id) ? request->request_id : "enforce",
        (long long)now.unix_seconds,
        rules_existed ? "true" : "false",
        state_existed ? "true" : "false",
        ledger_existed ? "true" : "false",
        redemption_history_existed ? "true" : "false",
        activity_history_existed ? "true" : "false");
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, meta_path, meta)) {
        recovery_clear(sysmodule);
        return false;
    }
    return true;
}

void recovery_clear(PtcSysmodule *sysmodule)
{
    char path[320];
    join_path(path, sizeof(path), sysmodule->app_root, "recovery/active");
    if (sysmodule->storage->vtable->remove_tree) {
        (void)sysmodule->storage->vtable->remove_tree(sysmodule->storage, path);
    }
}

bool recovery_rollback(PtcSysmodule *sysmodule)
{
    char meta_path[320];
    char meta[1024];
    PtcPctlSettingsSnapshot original;
    PtcPctlSettingsSnapshot restored;
    PtcPctlStatus status;
    bool rules_existed;
    bool state_existed;
    bool ledger_existed;
    bool redemption_history_existed = false;
    bool redemption_history_tracked;
    bool activity_history_existed = false;
    bool activity_history_tracked;
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
    redemption_history_tracked = json_bool_value(
        meta, "redemption_history_existed", &redemption_history_existed);
    activity_history_tracked = json_bool_value(
        meta, "activity_history_existed", &activity_history_existed);
    ok = restore_snapshot_exact(sysmodule, &original, &restored, &status,
        ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored) == PTC_ERR_OK;
    ok = restore_text_file(sysmodule, "rules.json", "recovery/active/rules.before", rules_existed) && ok;
    ok = restore_text_file(sysmodule, "state.json", "recovery/active/state.before", state_existed) && ok;
    ok = restore_text_file(sysmodule, "ledger/used_nonces.jsonl", "recovery/active/ledger.before", ledger_existed) && ok;
    if (redemption_history_tracked) {
        ok = restore_text_file(sysmodule, "ledger/redemption-history.jsonl",
            "recovery/active/redemption-history.before", redemption_history_existed) && ok;
    }
    if (activity_history_tracked) {
        ok = restore_text_file(sysmodule, "activity/history.jsonl",
            "recovery/active/activity-history.before", activity_history_existed) && ok;
    }
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

const char *pctl_target_mode_name(PtcPctlTargetMode mode)
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

static bool parse_bedtime_mode(const char *value, PtcBedtimeOverrideMode *out)
{
    if (strcmp(value, "inherit") == 0) *out = PTC_BEDTIME_OVERRIDE_INHERIT;
    else if (strcmp(value, "disabled") == 0) *out = PTC_BEDTIME_OVERRIDE_DISABLED;
    else if (strcmp(value, "custom") == 0) *out = PTC_BEDTIME_OVERRIDE_CUSTOM;
    else return false;
    return true;
}

static const char *bedtime_mode_name(PtcBedtimeOverrideMode mode)
{
    switch (mode) {
    case PTC_BEDTIME_OVERRIDE_DISABLED: return "disabled";
    case PTC_BEDTIME_OVERRIDE_CUSTOM: return "custom";
    case PTC_BEDTIME_OVERRIDE_INHERIT:
    default: return "inherit";
    }
}

static bool parse_bedtime_array(const char *text, PtcBedtimeWindow week[7])
{
    const char *pos = find_key(text, "bedtime_week");
    unsigned int i;
    if (!pos || !(pos = strchr(pos, '['))) return false;
    for (i = 0; i < 7; ++i) {
        const char *start = strchr(pos, '{');
        const char *end;
        char item[192];
        size_t length;
        if (!start || !(end = strchr(start, '}'))) return false;
        length = (size_t)(end - start + 1);
        if (length >= sizeof(item)) return false;
        memcpy(item, start, length);
        item[length] = '\0';
        if (!json_bool_value(item, "enabled", &week[i].enabled) ||
            !json_u16(item, "start_minute", &week[i].start_minute) ||
            !json_u16(item, "end_minute", &week[i].end_minute)) return false;
        pos = end + 1;
    }
    return true;
}

bool sysmodule_environment_fingerprint(PtcSysmodule *sysmodule, char out[65])
{
    char path[320];
    char environment[1024];
    uint8_t digest[PTC_SHA256_DIGEST_SIZE];
    static const char HEX[] = "0123456789abcdef";
    size_t i;
    if (!sysmodule || !out) return false;
    join_path(path, sizeof(path), sysmodule->app_root, "environment.json");
    if (!sysmodule->storage->vtable->read_text(
            sysmodule->storage, path, environment, sizeof(environment))) return false;
    {
        PtcSha256Ctx hash;
        ptc_sha256_init(&hash);
        ptc_sha256_update(&hash, (const uint8_t *)environment, strlen(environment));
        ptc_sha256_final(&hash, digest);
    }
    for (i = 0; i < sizeof(digest); ++i) {
        out[i * 2] = HEX[digest[i] >> 4];
        out[i * 2 + 1] = HEX[digest[i] & 0x0fu];
    }
    out[64] = '\0';
    return true;
}


bool load_config(PtcSysmodule *sysmodule, PtcRuntimeConfig *config)
{
    char path[320];
    char text[6144];
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
    return true;
}

bool load_rules(PtcSysmodule *sysmodule, PtcRules *rules)
{
    char path[320];
    char text[6144];
    char mode[24];
    int64_t version;
    ptc_rules_default(rules);
    join_path(path, sizeof(path), sysmodule->app_root, "rules.json");
    if (!read_cached_text(sysmodule, "rules.json", sysmodule->rules_cache_text, sizeof(sysmodule->rules_cache_text),
            &sysmodule->rules_meta, &sysmodule->rules_cache_valid, text, sizeof(text), true) || text[0] == '\0') {
        return true;
    }
    if (!json_i64(text, "version", &version) || (version != 1 && version != 2)) {
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
    if (json_bool_value(text, "scheduled_override_enabled", &rules->scheduled_override.enabled)) {
        (void)json_u16(text, "scheduled_override_start_day_index", &rules->scheduled_override.start_day_index);
        (void)json_u16(text, "scheduled_override_end_day_index", &rules->scheduled_override.end_day_index);
        if (json_string(text, "scheduled_override_mode", mode, sizeof(mode))) {
            (void)parse_rule_mode(mode, &rules->scheduled_override.rule.mode);
        }
        (void)json_u16(text, "scheduled_override_minutes", &rules->scheduled_override.rule.minutes);
        if (!ptc_scheduled_override_is_valid(&rules->scheduled_override)) return false;
    }
    if (json_u16(text, "daily_buffer_minutes", &rules->autonomy_policy.daily_buffer_minutes) &&
        !ptc_autonomy_policy_is_valid(&rules->autonomy_policy)) return false;
    (void)json_bool_value(text, "holiday_enabled", &rules->holiday_enabled);
    if (json_string(text, "holiday_mode", mode, sizeof(mode))) {
        (void)parse_rule_mode(mode, &rules->holiday_rule.mode);
    }
    (void)json_u16(text, "holiday_minutes", &rules->holiday_rule.minutes);
    if (json_string(text, "makeup_workday_mode", mode, sizeof(mode))) {
        (void)parse_rule_mode(mode, &rules->makeup_workday_rule.mode);
    }
    (void)json_u16(text, "makeup_workday_minutes", &rules->makeup_workday_rule.minutes);
    if (version >= 2 && find_key(text, "bedtime_enabled")) {
        char bedtime_mode[24];
        if (!json_bool_value(text, "bedtime_enabled", &rules->bedtime.enabled) ||
            !parse_bedtime_array(text, rules->bedtime.week) ||
            !json_bool_value(text, "bedtime_calendar_enabled", &rules->bedtime.calendar_enabled) ||
            !json_string(text, "bedtime_holiday_mode", bedtime_mode, sizeof(bedtime_mode)) ||
            !parse_bedtime_mode(bedtime_mode, &rules->bedtime.holiday_rule.mode) ||
            !json_string(text, "bedtime_makeup_mode", bedtime_mode, sizeof(bedtime_mode)) ||
            !parse_bedtime_mode(bedtime_mode, &rules->bedtime.makeup_workday_rule.mode) ||
            !json_bool_value(text, "bedtime_scheduled_present", &rules->bedtime.scheduled_override.present)) return false;
        (void)json_bool_value(text, "bedtime_holiday_enabled", &rules->bedtime.holiday_rule.window.enabled);
        (void)json_u16(text, "bedtime_holiday_start_minute", &rules->bedtime.holiday_rule.window.start_minute);
        (void)json_u16(text, "bedtime_holiday_end_minute", &rules->bedtime.holiday_rule.window.end_minute);
        (void)json_bool_value(text, "bedtime_makeup_enabled", &rules->bedtime.makeup_workday_rule.window.enabled);
        (void)json_u16(text, "bedtime_makeup_start_minute", &rules->bedtime.makeup_workday_rule.window.start_minute);
        (void)json_u16(text, "bedtime_makeup_end_minute", &rules->bedtime.makeup_workday_rule.window.end_minute);
        (void)json_u16(text, "bedtime_scheduled_start_day_index", &rules->bedtime.scheduled_override.start_day_index);
        (void)json_u16(text, "bedtime_scheduled_end_day_index", &rules->bedtime.scheduled_override.end_day_index);
        if (json_string(text, "bedtime_scheduled_mode", bedtime_mode, sizeof(bedtime_mode)))
            (void)parse_bedtime_mode(bedtime_mode, &rules->bedtime.scheduled_override.rule.mode);
        (void)json_bool_value(text, "bedtime_scheduled_enabled", &rules->bedtime.scheduled_override.rule.window.enabled);
        (void)json_u16(text, "bedtime_scheduled_start_minute", &rules->bedtime.scheduled_override.rule.window.start_minute);
        (void)json_u16(text, "bedtime_scheduled_end_minute", &rules->bedtime.scheduled_override.rule.window.end_minute);
        (void)json_u16(text, "bedtime_confirmation_version", &rules->bedtime.confirmation_version);
        (void)json_i64(text, "bedtime_official_setting_confirmed_at", &rules->bedtime.official_setting_confirmed_at);
        (void)json_string(text, "bedtime_confirmed_environment", rules->bedtime.confirmed_environment,
            sizeof(rules->bedtime.confirmed_environment));
        (void)json_bool_value(text, "bedtime_overlay_risk_accepted", &rules->bedtime.unverified_overlay_risk_accepted);
        if (!ptc_bedtime_policy_is_valid(&rules->bedtime)) return false;
    }
    return true;
}

bool save_rules(PtcSysmodule *sysmodule, const PtcRules *rules)
{
    char path[320];
    char text[6144];
    size_t used;
    unsigned int i;
    snprintf(path, sizeof(path), "%s/rules.json", sysmodule->app_root);
    snprintf(text, sizeof(text), "{\"version\":2,\"week\":[");
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
        "\"scheduled_override_enabled\":%s,\"scheduled_override_start_day_index\":%u,"
        "\"scheduled_override_end_day_index\":%u,\"scheduled_override_mode\":\"%s\","
        "\"scheduled_override_minutes\":%u,\"daily_buffer_minutes\":%u,"
        "\"holiday_enabled\":%s,\"holiday_mode\":\"%s\",\"holiday_minutes\":%u,"
        "\"makeup_workday_mode\":\"%s\",\"makeup_workday_minutes\":%u,"
        "\"bedtime_enabled\":%s,\"bedtime_week\":[",
        rules->today_override.present ? "true" : "false",
        rules->today_override.day_index,
        rule_mode_name(rules->today_override.rule.mode),
        rules->today_override.rule.minutes,
        rules->scheduled_override.enabled ? "true" : "false",
        rules->scheduled_override.start_day_index,
        rules->scheduled_override.end_day_index,
        rule_mode_name(rules->scheduled_override.rule.mode),
        rules->scheduled_override.rule.minutes,
        rules->autonomy_policy.daily_buffer_minutes,
        rules->holiday_enabled ? "true" : "false",
        rule_mode_name(rules->holiday_rule.mode),
        rules->holiday_rule.minutes,
        rule_mode_name(rules->makeup_workday_rule.mode),
        rules->makeup_workday_rule.minutes,
        rules->bedtime.enabled ? "true" : "false");
    for (i = 0; i < 7; ++i) {
        used = strlen(text);
        snprintf(text + used, sizeof(text) - used,
            "%s{\"enabled\":%s,\"start_minute\":%u,\"end_minute\":%u}",
            i ? "," : "", rules->bedtime.week[i].enabled ? "true" : "false",
            rules->bedtime.week[i].start_minute, rules->bedtime.week[i].end_minute);
    }
    used = strlen(text);
    snprintf(text + used, sizeof(text) - used,
        "],\"bedtime_calendar_enabled\":%s,"
        "\"bedtime_holiday_mode\":\"%s\",\"bedtime_holiday_enabled\":%s,"
        "\"bedtime_holiday_start_minute\":%u,\"bedtime_holiday_end_minute\":%u,"
        "\"bedtime_makeup_mode\":\"%s\",\"bedtime_makeup_enabled\":%s,"
        "\"bedtime_makeup_start_minute\":%u,\"bedtime_makeup_end_minute\":%u,"
        "\"bedtime_scheduled_present\":%s,\"bedtime_scheduled_start_day_index\":%u,"
        "\"bedtime_scheduled_end_day_index\":%u,\"bedtime_scheduled_mode\":\"%s\","
        "\"bedtime_scheduled_enabled\":%s,\"bedtime_scheduled_start_minute\":%u,"
        "\"bedtime_scheduled_end_minute\":%u,\"bedtime_confirmation_version\":%u,"
        "\"bedtime_official_setting_confirmed_at\":%lld,\"bedtime_confirmed_environment\":\"%s\","
        "\"bedtime_overlay_risk_accepted\":%s}\n",
        rules->bedtime.calendar_enabled ? "true" : "false",
        bedtime_mode_name(rules->bedtime.holiday_rule.mode),
        rules->bedtime.holiday_rule.window.enabled ? "true" : "false",
        rules->bedtime.holiday_rule.window.start_minute, rules->bedtime.holiday_rule.window.end_minute,
        bedtime_mode_name(rules->bedtime.makeup_workday_rule.mode),
        rules->bedtime.makeup_workday_rule.window.enabled ? "true" : "false",
        rules->bedtime.makeup_workday_rule.window.start_minute, rules->bedtime.makeup_workday_rule.window.end_minute,
        rules->bedtime.scheduled_override.present ? "true" : "false",
        rules->bedtime.scheduled_override.start_day_index, rules->bedtime.scheduled_override.end_day_index,
        bedtime_mode_name(rules->bedtime.scheduled_override.rule.mode),
        rules->bedtime.scheduled_override.rule.window.enabled ? "true" : "false",
        rules->bedtime.scheduled_override.rule.window.start_minute,
        rules->bedtime.scheduled_override.rule.window.end_minute,
        rules->bedtime.confirmation_version, (long long)rules->bedtime.official_setting_confirmed_at,
        rules->bedtime.confirmed_environment,
        rules->bedtime.unverified_overlay_risk_accepted ? "true" : "false");
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text)) return false;
    snprintf(sysmodule->rules_cache_text, sizeof(sysmodule->rules_cache_text), "%s", text);
    sysmodule->rules_cache_valid = sysmodule->storage->vtable->metadata &&
        sysmodule->storage->vtable->metadata(sysmodule->storage, path, &sysmodule->rules_meta);
    return true;
}

bool restore_rules(PtcSysmodule *sysmodule, const PtcRules *rules, bool existed)
{
    char path[320];
    if (existed) {
        return save_rules(sysmodule, rules);
    }
    join_path(path, sizeof(path), sysmodule->app_root, "rules.json");
    return !sysmodule->storage->vtable->exists(sysmodule->storage, path) ||
        sysmodule->storage->vtable->remove_path(sysmodule->storage, path);
}

bool load_state(PtcSysmodule *sysmodule, PtcRuntimeState *state)
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
    state->buffer_claimed = false;
    state->buffer_claim_day_index = 0;
    state->buffer_claimed_minutes = 0;
    state->summary_day_index = 0;
    state->summary_grant_minutes = 0;
    state->bedtime_enforced = false;
    state->bedtime_window_instance_id = 0;
    state->bedtime_start_day_index = 0;
    state->bedtime_skipped_instance_id = 0;
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
    (void)json_bool_value(text, "buffer_claimed", &state->buffer_claimed);
    (void)json_u16(text, "buffer_claim_day_index", &state->buffer_claim_day_index);
    (void)json_u16(text, "buffer_claimed_minutes", &state->buffer_claimed_minutes);
    (void)json_u16(text, "summary_day_index", &state->summary_day_index);
    (void)json_u16(text, "summary_grant_minutes", &state->summary_grant_minutes);
    (void)json_bool_value(text, "bedtime_enforced", &state->bedtime_enforced);
    (void)json_u64(text, "bedtime_window_instance_id", &state->bedtime_window_instance_id);
    (void)json_u16(text, "bedtime_start_day_index", &state->bedtime_start_day_index);
    (void)json_u64(text, "bedtime_skipped_instance_id", &state->bedtime_skipped_instance_id);
    {
        uint16_t mode = 0;
        if (json_u16(text, "last_enforced_mode", &mode)) {
            state->last_enforced_mode = (PtcPctlTargetMode)mode;
        }
    }
    return true;
}

bool save_state(PtcSysmodule *sysmodule, const PtcRuntimeState *state, int64_t updated_at)
{
    char path[320];
    char text[2048];
    snprintf(path, sizeof(path), "%s/state.json", sysmodule->app_root);
    snprintf(
        text,
        sizeof(text),
        "{\"version\":1,\"last_enforced_day_index\":%u,"
        "\"last_enforced_mode\":%u,\"last_enforced_minutes\":%u,"
        "\"apply_status\":\"%s\",\"apply_pending_confirmation\":%s,"
        "\"apply_confirmation_deadline\":%lld,\"pending_mode\":%u,\"pending_minutes\":%u,"
        "\"v2_failed_attempts\":%u,\"v2_cooldown_until\":%lld,"
        "\"buffer_claimed\":%s,\"buffer_claim_day_index\":%u,\"buffer_claimed_minutes\":%u,"
        "\"summary_day_index\":%u,\"summary_grant_minutes\":%u,"
        "\"bedtime_enforced\":%s,\"bedtime_window_instance_id\":%llu,"
        "\"bedtime_start_day_index\":%u,\"bedtime_skipped_instance_id\":%llu,"
        "\"updated_at\":%lld}\n",
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
        state->buffer_claimed ? "true" : "false",
        state->buffer_claim_day_index,
        state->buffer_claimed_minutes,
        state->summary_day_index,
        state->summary_grant_minutes,
        state->bedtime_enforced ? "true" : "false",
        (unsigned long long)state->bedtime_window_instance_id,
        state->bedtime_start_day_index,
        (unsigned long long)state->bedtime_skipped_instance_id,
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
    rules_ok = load_rules(sysmodule, &rules);
    state_ok = load_state(sysmodule, &state);
    return config_ok && rules_ok && state_ok;
}
