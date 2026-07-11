#include "sysmodule_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/policy/control_policy.h"
#include "../common/protocol/request_schema.h"
#include "../common/protocol/result_builder.h"
#include "../common/rules/rules.h"
#include "../common/time/ptc_time.h"
#include "../common/token/token_v1.h"

#define PTC_PLAY_TIMER_WRITE_BACKEND "pctl-s-v1"

typedef struct {
    char device_id[80];
    char grant_secret[128];
    uint16_t max_add_minutes;
    PtcControlMode mode;
    bool allow_unlimited_to_limited;
} PtcRuntimeConfig;

typedef struct {
    int64_t parent_unlock_until;
    uint16_t last_enforced_day_index;
    PtcPctlTargetMode last_enforced_mode;
    uint16_t last_enforced_minutes;
} PtcRuntimeState;

static void join_path(char *out, size_t out_size, const char *a, const char *b)
{
    snprintf(out, out_size, "%s/%s", a, b);
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

static const char *rule_mode_name(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_UNLIMITED:
        return "unlimited";
    case PTC_RULE_MODE_BLOCKED:
        return "blocked";
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
    if (strcmp(value, "blocked") == 0) {
        *out = PTC_RULE_MODE_BLOCKED;
        return true;
    }
    return false;
}

static const char *limit_action_name(PtcLimitAction action)
{
    switch (action) {
    case PTC_LIMIT_ACTION_RAW_BLOCK:
        return "raw_block";
    case PTC_LIMIT_ACTION_SUSPEND:
        return "suspend";
    case PTC_LIMIT_ACTION_REMIND:
    default:
        return "remind";
    }
}

static bool parse_limit_action(const char *value, PtcLimitAction *out)
{
    if (strcmp(value, "remind") == 0) {
        *out = PTC_LIMIT_ACTION_REMIND;
        return true;
    }
    if (strcmp(value, "raw_block") == 0) {
        *out = PTC_LIMIT_ACTION_RAW_BLOCK;
        return true;
    }
    if (strcmp(value, "suspend") == 0) {
        *out = PTC_LIMIT_ACTION_SUSPEND;
        return true;
    }
    return false;
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
    snprintf(path, sizeof(path), "%s/logs/events.jsonl", sysmodule->app_root);
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
}

static bool load_config(PtcSysmodule *sysmodule, PtcRuntimeConfig *config)
{
    char path[320];
    char text[4096];
    char mode[24];
    int64_t version;
    join_path(path, sizeof(path), sysmodule->app_root, "config.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        return false;
    }
    if (!json_i64(text, "version", &version) || version != 1 ||
        !json_string(text, "device_id", config->device_id, sizeof(config->device_id)) ||
        !json_string(text, "grant_secret", config->grant_secret, sizeof(config->grant_secret))) {
        return false;
    }
    if (!json_u16(text, "max_add_minutes", &config->max_add_minutes)) {
        config->max_add_minutes = 120;
    }
    if (!json_string(text, "control_mode", mode, sizeof(mode))) {
        snprintf(mode, sizeof(mode), "observe");
    }
    config->mode = ptc_control_mode_from_string(mode);
    if (!json_bool_value(text, "allow_unlimited_to_limited", &config->allow_unlimited_to_limited)) {
        config->allow_unlimited_to_limited = false;
    }
    return true;
}

static PtcCapabilities load_capabilities(PtcSysmodule *sysmodule)
{
    PtcCapabilities caps;
    char path[320];
    char text[1024];
    char backend[32];
    caps.play_timer_write_verified = false;
    caps.raw_block_verified = false;
    caps.suspend_verified = false;
    join_path(path, sizeof(path), sysmodule->app_root, "capabilities.json");
    if (sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        (void)json_bool_value(text, "play_timer_write_verified", &caps.play_timer_write_verified);
        if (!json_string(text, "play_timer_write_backend", backend, sizeof(backend)) ||
            strcmp(backend, PTC_PLAY_TIMER_WRITE_BACKEND) != 0) {
            caps.play_timer_write_verified = false;
        }
        (void)json_bool_value(text, "raw_block_verified", &caps.raw_block_verified);
        (void)json_bool_value(text, "suspend_verified", &caps.suspend_verified);
    }
    return caps;
}

static bool save_capabilities(PtcSysmodule *sysmodule, const PtcCapabilities *caps, int64_t updated_at)
{
    char path[320];
    char text[512];
    snprintf(path, sizeof(path), "%s/capabilities.json", sysmodule->app_root);
    snprintf(
        text,
        sizeof(text),
        "{\"version\":1,\"play_timer_write_verified\":%s,"
        "\"play_timer_write_backend\":\"%s\",\"raw_block_verified\":%s,"
        "\"suspend_verified\":%s,\"verified_at\":{\"play_timer_write\":%lld,"
        "\"raw_block\":%lld,\"suspend\":%lld}}\n",
        caps->play_timer_write_verified ? "true" : "false",
        PTC_PLAY_TIMER_WRITE_BACKEND,
        caps->raw_block_verified ? "true" : "false",
        caps->suspend_verified ? "true" : "false",
        caps->play_timer_write_verified ? (long long)updated_at : 0LL,
        caps->raw_block_verified ? (long long)updated_at : 0LL,
        caps->suspend_verified ? (long long)updated_at : 0LL);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text);
}

static bool load_rules(PtcSysmodule *sysmodule, PtcRules *rules)
{
    char path[320];
    char text[4096];
    char mode[24];
    char action[24];
    int64_t version;
    ptc_rules_default(rules);
    join_path(path, sizeof(path), sysmodule->app_root, "rules.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
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
    (void)json_bool_value(text, "bedtime_enabled", &rules->bedtime.enabled);
    (void)json_u16(text, "bedtime_start_min", &rules->bedtime.start_min);
    (void)json_u16(text, "bedtime_end_min", &rules->bedtime.end_min);
    if (json_string(text, "limit_action", action, sizeof(action))) {
        (void)parse_limit_action(action, &rules->limit_action);
    }
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
        "\"bedtime_enabled\":%s,\"bedtime_start_min\":%u,\"bedtime_end_min\":%u,"
        "\"limit_action\":\"%s\"}\n",
        rules->today_override.present ? "true" : "false",
        rules->today_override.day_index,
        rule_mode_name(rules->today_override.rule.mode),
        rules->today_override.rule.minutes,
        rules->bedtime.enabled ? "true" : "false",
        rules->bedtime.start_min,
        rules->bedtime.end_min,
        limit_action_name(rules->limit_action));
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text);
}

static bool load_state(PtcSysmodule *sysmodule, PtcRuntimeState *state)
{
    char path[320];
    char text[1024];
    int64_t version;
    state->parent_unlock_until = 0;
    state->last_enforced_day_index = 0;
    state->last_enforced_mode = 0;
    state->last_enforced_minutes = 0;
    join_path(path, sizeof(path), sysmodule->app_root, "state.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        return true;
    }
    if (!json_i64(text, "version", &version) || version != 1) {
        return false;
    }
    (void)json_i64(text, "parent_unlock_until", &state->parent_unlock_until);
    (void)json_u16(text, "last_enforced_day_index", &state->last_enforced_day_index);
    (void)json_u16(text, "last_enforced_minutes", &state->last_enforced_minutes);
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
    char text[512];
    snprintf(path, sizeof(path), "%s/state.json", sysmodule->app_root);
    snprintf(
        text,
        sizeof(text),
        "{\"version\":1,\"parent_unlock_until\":%lld,\"last_enforced_day_index\":%u,"
        "\"last_enforced_mode\":%u,\"last_enforced_minutes\":%u,\"updated_at\":%lld}\n",
        (long long)state->parent_unlock_until,
        state->last_enforced_day_index,
        (unsigned int)state->last_enforced_mode,
        state->last_enforced_minutes,
        (long long)updated_at);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text);
}

static bool nonce_used(uint16_t day_index, uint32_t nonce, void *ctx)
{
    PtcSysmodule *sysmodule = (PtcSysmodule *)ctx;
    char path[320];
    char text[4096];
    char needle[96];
    join_path(path, sizeof(path), sysmodule->app_root, "ledger/used_nonces.jsonl");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        return false;
    }
    snprintf(needle, sizeof(needle), "\"day_index\":%u,\"nonce\":%lu", day_index, (unsigned long)nonce);
    return strstr(text, needle) != NULL;
}

static bool consume_nonce(PtcSysmodule *sysmodule, const PtcRequest *request, uint16_t day_index, uint32_t nonce)
{
    char path[320];
    char line[128];
    bool ok;
    join_path(path, sizeof(path), sysmodule->app_root, "ledger/used_nonces.jsonl");
    snprintf(line, sizeof(line), "{\"day_index\":%u,\"nonce\":%lu}", day_index, (unsigned long)nonce);
    ok = sysmodule->storage->vtable->append_line(sysmodule->storage, path, line);
    append_event(sysmodule, request, ok ? "nonce_consumed" : "nonce_failed", ok ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED, "");
    return ok;
}

static void result_state_from_pctl(
    PtcResultState *state,
    uint16_t day_index,
    const PtcPctlStatus *status,
    const PtcCapabilities *caps,
    const PtcRules *rules,
    bool parent_unlock_active,
    uint16_t minute_of_day)
{
    PtcRuleEvaluation eval;
    ptc_result_state_default(state, day_index);
    state->limited_today = status->limited_today ? 1 : 0;
    state->blocked_today = status->blocked_today ? 1 : 0;
    state->unrestricted_today = status->unrestricted_today ? 1 : 0;
    state->remaining_available = status->remaining_available;
    state->remaining_minutes = status->remaining_available ? status->remaining_minutes : -1;
    state->play_timer_enabled = status->play_timer_enabled ? 1 : 0;
    eval = ptc_rules_evaluate(rules, day_index, ptc_weekday_from_day_index(day_index), minute_of_day, parent_unlock_active);
    state->bedtime_active = eval.bedtime_active;
    state->parent_unlock_active = eval.parent_unlock_active;
    state->restricted_now = (status->restricted_now || eval.restricted_now) ? 1 : 0;
    state->play_timer_write_verified = caps->play_timer_write_verified;
    state->raw_block_verified = caps->raw_block_verified;
    state->suspend_verified = caps->suspend_verified;
}

static void result_state_default_with_caps(PtcResultState *state, uint16_t day_index, const PtcCapabilities *caps)
{
    ptc_result_state_default(state, day_index);
    if (caps) {
        state->play_timer_write_verified = caps->play_timer_write_verified;
        state->raw_block_verified = caps->raw_block_verified;
        state->suspend_verified = caps->suspend_verified;
    }
}

static bool write_result(PtcSysmodule *sysmodule, const char *request_id, const char *json)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/results/%s.json", sysmodule->app_root, request_id);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, json);
}

static bool request_file_path(char *out, size_t out_size, const PtcSysmodule *sysmodule, const char *queue, const char *name)
{
    int written = snprintf(out, out_size, "%s/inbox/%s/%.127s", sysmodule->app_root, queue, name);
    return written >= 0 && (size_t)written < out_size;
}

static PtcErrorCode backup_before_write(PtcSysmodule *sysmodule, const PtcRequest *request)
{
    char path[320];
    PtcPctlBackup backup;
    PtcErrorCode err = sysmodule->pctl->vtable->backup(sysmodule->pctl, &backup);
    if (err != PTC_ERR_OK) {
        append_event(sysmodule, request, "pctl_backup_failed", err, "");
        return err;
    }
    snprintf(path, sizeof(path), "%s/backups/last_pctl_backup.txt", sysmodule->app_root);
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, backup.text)) {
        append_event(sysmodule, request, "pctl_backup_failed", PTC_ERR_PCTL_BACKUP_FAILED, "storage");
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    append_event(sysmodule, request, "pctl_backup", PTC_ERR_OK, "");
    return PTC_ERR_OK;
}

static PtcErrorCode apply_target(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const PtcCapabilities *caps,
    PtcClockSnapshot now,
    PtcPctlTargetMode mode,
    uint16_t minutes)
{
    PtcPctlTarget target;
    PtcErrorCode err;
    if (!caps || !caps->play_timer_write_verified) {
        append_event(sysmodule, request, "pctl_apply_failed", PTC_ERR_PCTL_WRITE_NOT_VERIFIED, "play_timer_write");
        return PTC_ERR_PCTL_WRITE_NOT_VERIFIED;
    }
    err = backup_before_write(sysmodule, request);
    if (err != PTC_ERR_OK) {
        return err;
    }
    target.mode = mode;
    target.minutes = minutes;
    target.weekday = ptc_weekday_from_day_index(now.day_index);
    err = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &target);
    append_event(sysmodule, request, err == PTC_ERR_OK ? "pctl_apply" : "pctl_apply_failed", err, rule_mode_name((PtcRuleMode)mode));
    return err;
}

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
    case PTC_REQUEST_BLOCK_TODAY:
        return PTC_OPERATION_BLOCK_TODAY;
    case PTC_REQUEST_PROBE_RAW_BLOCK:
        return PTC_OPERATION_PROBE_RAW_BLOCK;
    case PTC_REQUEST_PROBE_SUSPEND:
        return PTC_OPERATION_PROBE_SUSPEND;
    case PTC_REQUEST_PROBE_PLAY_TIMER_WRITE:
        return PTC_OPERATION_PROBE_PLAY_TIMER_WRITE;
    case PTC_REQUEST_OFFLINE_CODE:
        return PTC_OPERATION_GRANT_MINUTES;
    case PTC_REQUEST_STATUS:
        return PTC_OPERATION_STATUS;
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
    case PTC_REQUEST_SET_BEDTIME:
    case PTC_REQUEST_SET_LIMIT_ACTION:
    case PTC_REQUEST_PARENT_UNLOCK_START:
    case PTC_REQUEST_PARENT_UNLOCK_END:
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
    if (rule.mode == PTC_RULE_MODE_BLOCKED) {
        return PTC_PCTL_TARGET_BLOCKED;
    }
    return PTC_PCTL_TARGET_LIMIT;
}

static PtcErrorCode update_rules_for_request(PtcSysmodule *sysmodule, const PtcRequest *request, PtcRules *rules, PtcRuntimeState *runtime_state, PtcClockSnapshot now)
{
    PtcDayRule active;
    switch (request->type) {
    case PTC_REQUEST_SET_TODAY_LIMIT:
        rules->today_override.present = true;
        rules->today_override.day_index = now.day_index;
        rules->today_override.rule.mode = PTC_RULE_MODE_LIMIT;
        rules->today_override.rule.minutes = request->minutes;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_ADD_TODAY_MINUTES:
        active = ptc_rules_today_rule(rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
        if (active.mode != PTC_RULE_MODE_LIMIT) {
            active.mode = PTC_RULE_MODE_LIMIT;
            active.minutes = 0;
        }
        rules->today_override.present = true;
        rules->today_override.day_index = now.day_index;
        rules->today_override.rule.mode = PTC_RULE_MODE_LIMIT;
        rules->today_override.rule.minutes = (uint16_t)(active.minutes + request->minutes);
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
        rules->today_override.present = true;
        rules->today_override.day_index = now.day_index;
        rules->today_override.rule.mode = PTC_RULE_MODE_UNLIMITED;
        rules->today_override.rule.minutes = 0;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_BLOCK_TODAY:
        rules->today_override.present = true;
        rules->today_override.day_index = now.day_index;
        rules->today_override.rule.mode = PTC_RULE_MODE_BLOCKED;
        rules->today_override.rule.minutes = 0;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
        rules->today_override.present = false;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
        memcpy(rules->week, request->week, sizeof(rules->week));
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_SET_BEDTIME:
        rules->bedtime = request->bedtime;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_SET_LIMIT_ACTION:
        rules->limit_action = request->limit_action;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_PARENT_UNLOCK_START:
        runtime_state->parent_unlock_until = now.unix_seconds + ((int64_t)request->duration_minutes * 60);
        return save_state(sysmodule, runtime_state, now.unix_seconds) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_PARENT_UNLOCK_END:
        runtime_state->parent_unlock_until = 0;
        return save_state(sysmodule, runtime_state, now.unix_seconds) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
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
    PtcClockSnapshot now)
{
    PtcPctlStatus pctl_status;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcResultState state;
    char json[2048];
    PtcErrorCode err;
    if (!load_rules(sysmodule, &rules)) {
        return finish_with_error(sysmodule, request, mode, dry_run, PTC_ERR_RULES_INVALID, now.day_index, caps);
    }
    if (!load_state(sysmodule, &runtime_state)) {
        return finish_with_error(sysmodule, request, mode, dry_run, PTC_ERR_BAD_REQUEST, now.day_index, caps);
    }
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, mode, dry_run, err, now.day_index, caps);
    }
    result_state_from_pctl(&state, now.day_index, &pctl_status, caps, &rules, runtime_state.parent_unlock_until > now.unix_seconds, now.minute_of_day);
    (void)ptc_result_ok_json(json, sizeof(json), request->request_id, request->type_text, mode, dry_run, &state, now.unix_seconds);
    append_event(sysmodule, request, "result_ok", PTC_ERR_OK, "");
    return write_result(sysmodule, request->request_id, json);
}

static bool process_status(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, const PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcPolicyDecision decision = ptc_policy_decide(config->mode, disable_flag, PTC_OPERATION_STATUS, caps, false, config->allow_unlimited_to_limited);
    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, decision.error, now.day_index, caps);
    }
    return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, caps, now);
}

static bool process_offline_code(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, const PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcTokenPayload token;
    PtcPctlStatus pctl_status;
    PtcPolicyDecision decision;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcResultState state;
    char json[2048];
    PtcErrorCode err;
    decision = ptc_policy_decide(config->mode, disable_flag, PTC_OPERATION_GRANT_MINUTES, caps, false, config->allow_unlimited_to_limited);
    if (decision.error == PTC_ERR_DISABLED) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    err = ptc_token_verify(request->code, config->device_id, config->grant_secret, now.day_index, config->max_add_minutes, nonce_used, sysmodule, &token);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, err, now.day_index, caps);
    }
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, err, now.day_index, caps);
    }
    decision = ptc_policy_decide(config->mode, disable_flag, PTC_OPERATION_GRANT_MINUTES, caps, pctl_status.unrestricted_today, config->allow_unlimited_to_limited);
    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    if (decision.may_write_pctl) {
        err = apply_target(sysmodule, request, caps, now, PTC_PCTL_TARGET_LIMIT, token.minutes);
        if (err != PTC_ERR_OK) {
            return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
        }
    }
    (void)load_rules(sysmodule, &rules);
    (void)load_state(sysmodule, &runtime_state);
    (void)sysmodule->pctl->vtable->read_status(sysmodule->pctl, &pctl_status);
    result_state_from_pctl(&state, now.day_index, &pctl_status, caps, &rules, runtime_state.parent_unlock_until > now.unix_seconds, now.minute_of_day);
    (void)ptc_result_ok_json(json, sizeof(json), request->request_id, request->type_text, ptc_control_mode_name(config->mode), decision.dry_run, &state, now.unix_seconds);
    if (write_result(sysmodule, request->request_id, json)) {
        append_event(sysmodule, request, "result_ok", PTC_ERR_OK, "");
        if (decision.consume_nonce_after_success) {
            (void)consume_nonce(sysmodule, request, token.day_index_since_2020, token.nonce);
        }
        return true;
    }
    append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "");
    return false;
}

static bool process_probe(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcPolicyDecision decision = ptc_policy_decide(config->mode, disable_flag, request_operation(request->type), caps, false, config->allow_unlimited_to_limited);
    PtcProbeResult probe;
    PtcErrorCode err;
    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    if (decision.dry_run) {
        return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), true, caps, now);
    }
    err = backup_before_write(sysmodule, request);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
    }
    if (request->type == PTC_REQUEST_PROBE_PLAY_TIMER_WRITE) {
        err = sysmodule->pctl->vtable->probe_play_timer_write(sysmodule->pctl, &probe);
        caps->play_timer_write_verified = err == PTC_ERR_OK && probe.verified;
    } else if (request->type == PTC_REQUEST_PROBE_RAW_BLOCK) {
        err = sysmodule->pctl->vtable->probe_raw_block(sysmodule->pctl, &probe);
        caps->raw_block_verified = err == PTC_ERR_OK && probe.verified;
    } else {
        err = sysmodule->pctl->vtable->probe_suspend(sysmodule->pctl, &probe);
        caps->suspend_verified = err == PTC_ERR_OK && probe.verified;
    }
    append_event(sysmodule, request, err == PTC_ERR_OK ? "probe_ok" : "probe_failed", err, probe.detail);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
    }
    if (!save_capabilities(sysmodule, caps, now.unix_seconds)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, PTC_ERR_STORAGE_WRITE_FAILED, now.day_index, caps);
    }
    return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), false, caps, now);
}

static bool process_rule_request(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, const PtcCapabilities *caps, PtcClockSnapshot now)
{
    PtcPctlStatus pctl_status;
    PtcPolicyDecision decision;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcErrorCode err;
    PtcDayRule active_rule;
    if (disable_flag || config->mode == PTC_CONTROL_DISABLED) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_DISABLED, now.day_index, caps);
    }
    if (!load_rules(sysmodule, &rules)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_RULES_INVALID, now.day_index, caps);
    }
    if (!load_state(sysmodule, &runtime_state)) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_BAD_REQUEST, now.day_index, caps);
    }
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, err, now.day_index, caps);
    }
    if (request->type == PTC_REQUEST_SET_LIMIT_ACTION) {
        if (request->limit_action == PTC_LIMIT_ACTION_RAW_BLOCK && !caps->raw_block_verified) {
            return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_RAW_BLOCK_NOT_VERIFIED, now.day_index, caps);
        }
        if (request->limit_action == PTC_LIMIT_ACTION_SUSPEND && !caps->suspend_verified) {
            return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), true, PTC_ERR_SUSPEND_NOT_VERIFIED, now.day_index, caps);
        }
    }
    decision = ptc_policy_decide(config->mode, disable_flag, request_operation(request->type), caps, pctl_status.unrestricted_today, config->allow_unlimited_to_limited);
    if (decision.error != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, decision.error, now.day_index, caps);
    }
    if (!decision.dry_run &&
        (request->type == PTC_REQUEST_SET_TODAY_LIMIT ||
            request->type == PTC_REQUEST_ADD_TODAY_MINUTES ||
            request->type == PTC_REQUEST_DISABLE_TODAY_LIMIT ||
            request->type == PTC_REQUEST_BLOCK_TODAY ||
            request->type == PTC_REQUEST_RESTORE_TODAY_POLICY) &&
        !caps->play_timer_write_verified) {
        append_event(sysmodule, request, "pctl_apply_failed", PTC_ERR_PCTL_WRITE_NOT_VERIFIED, "play_timer_write");
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, PTC_ERR_PCTL_WRITE_NOT_VERIFIED, now.day_index, caps);
    }
    if (!decision.dry_run) {
        err = update_rules_for_request(sysmodule, request, &rules, &runtime_state, now);
        if (err != PTC_ERR_OK) {
            return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
        }
        append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "");
        if (request->type == PTC_REQUEST_SET_TODAY_LIMIT ||
            request->type == PTC_REQUEST_ADD_TODAY_MINUTES ||
            request->type == PTC_REQUEST_DISABLE_TODAY_LIMIT ||
            request->type == PTC_REQUEST_BLOCK_TODAY ||
            request->type == PTC_REQUEST_RESTORE_TODAY_POLICY) {
            active_rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
            err = apply_target(sysmodule, request, caps, now, target_from_day_rule(active_rule), active_rule.minutes);
            if (err != PTC_ERR_OK) {
                return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
            }
        }
    }
    return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), decision.dry_run, caps, now);
}

static void process_request_text(PtcSysmodule *sysmodule, const char *request_text)
{
    PtcRequest request;
    PtcRuntimeConfig config;
    PtcCapabilities caps;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    PtcErrorCode parse_err;
    bool disable_flag;
    char disable_path[320];

    parse_err = ptc_request_parse(request_text, &request);
    if (parse_err != PTC_ERR_OK) {
        memset(&request, 0, sizeof(request));
        snprintf(request.request_id, sizeof(request.request_id), "unknown");
        snprintf(request.type_text, sizeof(request.type_text), "unknown");
        (void)json_string(request_text, "request_id", request.request_id, sizeof(request.request_id));
        (void)json_string(request_text, "type", request.type_text, sizeof(request.type_text));
        (void)finish_with_error(sysmodule, &request, "observe", true, parse_err, now.day_index, NULL);
        return;
    }
    append_event(sysmodule, &request, "request_received", PTC_ERR_OK, "");
    if (!load_config(sysmodule, &config)) {
        (void)finish_with_error(sysmodule, &request, "observe", true, PTC_ERR_CONFIG_INVALID, now.day_index, NULL);
        return;
    }
    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    disable_flag = sysmodule->storage->vtable->exists(sysmodule->storage, disable_path);
    caps = load_capabilities(sysmodule);

    switch (request.type) {
    case PTC_REQUEST_STATUS:
        (void)process_status(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_OFFLINE_CODE:
        (void)process_offline_code(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_PROBE_RAW_BLOCK:
    case PTC_REQUEST_PROBE_SUSPEND:
    case PTC_REQUEST_PROBE_PLAY_TIMER_WRITE:
        (void)process_probe(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_SET_TODAY_LIMIT:
    case PTC_REQUEST_ADD_TODAY_MINUTES:
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
    case PTC_REQUEST_BLOCK_TODAY:
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
    case PTC_REQUEST_SET_BEDTIME:
    case PTC_REQUEST_SET_LIMIT_ACTION:
    case PTC_REQUEST_PARENT_UNLOCK_START:
    case PTC_REQUEST_PARENT_UNLOCK_END:
        (void)process_rule_request(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_UNKNOWN:
    default:
        (void)finish_with_error(sysmodule, &request, ptc_control_mode_name(config.mode), true, PTC_ERR_UNKNOWN_REQUEST_TYPE, now.day_index, &caps);
        break;
    }
}

int ptc_sysmodule_enforce_tick(PtcSysmodule *sysmodule)
{
    PtcRuntimeConfig config;
    PtcCapabilities caps;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    PtcDayRule active_rule;
    PtcPctlTargetMode target_mode;
    char disable_path[320];
    PtcErrorCode err;

    if (!load_config(sysmodule, &config) || config.mode != PTC_CONTROL_ENFORCE) {
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
    active_rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    target_mode = target_from_day_rule(active_rule);
    if (runtime_state.last_enforced_day_index == now.day_index &&
        runtime_state.last_enforced_mode == target_mode &&
        runtime_state.last_enforced_minutes == active_rule.minutes) {
        return 0;
    }
    err = apply_target(sysmodule, NULL, &caps, now, target_mode, active_rule.minutes);
    if (err != PTC_ERR_OK) {
        return 0;
    }
    err = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
    append_event(sysmodule, NULL, err == PTC_ERR_OK ? "pctl_start_timer" : "pctl_apply_failed", err, "start_timer");
    if (err != PTC_ERR_OK) {
        return 0;
    }
    runtime_state.last_enforced_day_index = now.day_index;
    runtime_state.last_enforced_mode = target_mode;
    runtime_state.last_enforced_minutes = active_rule.minutes;
    if (!save_state(sysmodule, &runtime_state, now.unix_seconds)) {
        append_event(sysmodule, NULL, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "enforce_state");
        return 0;
    }
    append_event(sysmodule, NULL, "state_persisted", PTC_ERR_OK, "enforce");
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
        if (!request_file_path(pending, sizeof(pending), sysmodule, "pending", names[i]) ||
            !request_file_path(processing, sizeof(processing), sysmodule, "processing", names[i]) ||
            !request_file_path(done, sizeof(done), sysmodule, "done", names[i])) {
            continue;
        }
        if (!sysmodule->storage->vtable->rename_path(sysmodule->storage, pending, processing)) {
            continue;
        }
        if (sysmodule->storage->vtable->read_text(sysmodule->storage, processing, text, sizeof(text))) {
            process_request_text(sysmodule, text);
        }
        (void)sysmodule->storage->vtable->rename_path(sysmodule->storage, processing, done);
        ++processed;
    }
    return processed;
}
