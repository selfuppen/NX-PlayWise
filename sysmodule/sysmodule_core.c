#include "sysmodule_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/policy/control_policy.h"
#include "../common/protocol/capability_backend.h"
#include "../common/protocol/request_schema.h"
#include "../common/protocol/result_builder.h"
#include "../common/rules/rules.h"
#include "../common/time/ptc_time.h"
#include "../common/token/token_v1.h"

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
    snprintf(path, sizeof(path), "%s/logs/pctl_debug.jsonl", sysmodule->app_root);
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
    caps.play_timer_effect_verified = false;
    caps.play_timer_effect_backend[0] = '\0';
    caps.raw_block_verified = false;
    caps.suspend_verified = false;
    join_path(path, sizeof(path), sysmodule->app_root, "capabilities.json");
    if (sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        (void)json_bool_value(text, "play_timer_write_verified", &caps.play_timer_write_verified);
        if (!json_string(text, "play_timer_write_backend", backend, sizeof(backend)) ||
            strcmp(backend, PTC_PLAY_TIMER_WRITE_BACKEND) != 0) {
            caps.play_timer_write_verified = false;
        }
        if (json_string(text, "play_timer_effect_backend", backend, sizeof(backend)) &&
            strcmp(backend, PTC_PLAY_TIMER_EFFECT_BACKEND) == 0) {
            (void)json_bool_value(text, "play_timer_effect_verified", &caps.play_timer_effect_verified);
            snprintf(caps.play_timer_effect_backend, sizeof(caps.play_timer_effect_backend), "%s", backend);
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
        "\"play_timer_write_backend\":\"%s\",\"play_timer_effect_verified\":%s,"
        "\"play_timer_effect_backend\":\"%s\",\"raw_block_verified\":%s,"
        "\"suspend_verified\":%s,\"verified_at\":{\"play_timer_write\":%lld,"
        "\"play_timer_effect\":%lld,\"raw_block\":%lld,\"suspend\":%lld}}\n",
        caps->play_timer_write_verified ? "true" : "false",
        PTC_PLAY_TIMER_WRITE_BACKEND,
        caps->play_timer_effect_verified ? "true" : "false",
        PTC_PLAY_TIMER_EFFECT_BACKEND,
        caps->raw_block_verified ? "true" : "false",
        caps->suspend_verified ? "true" : "false",
        caps->play_timer_write_verified ? (long long)updated_at : 0LL,
        caps->play_timer_effect_verified ? (long long)updated_at : 0LL,
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
    state->play_timer_effect_verified = caps->play_timer_effect_verified;
    state->raw_block_verified = caps->raw_block_verified;
    state->suspend_verified = caps->suspend_verified;
}

static void result_state_default_with_caps(PtcResultState *state, uint16_t day_index, const PtcCapabilities *caps)
{
    ptc_result_state_default(state, day_index);
    if (caps) {
        state->play_timer_write_verified = caps->play_timer_write_verified;
        state->play_timer_effect_verified = caps->play_timer_effect_verified;
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
    if (!caps || !caps->play_timer_write_verified) {
        append_event(sysmodule, request, "pctl_apply_failed", PTC_ERR_PCTL_WRITE_NOT_VERIFIED, "play_timer_write");
        return PTC_ERR_PCTL_WRITE_NOT_VERIFIED;
    }
    if (!caps->play_timer_effect_verified) {
        append_event(sysmodule, request, "pctl_apply_failed", PTC_ERR_PCTL_EFFECT_NOT_VERIFIED, "play_timer_effect");
        return PTC_ERR_PCTL_EFFECT_NOT_VERIFIED;
    }
    err = backup_before_write(sysmodule, request, mode_name);
    if (err != PTC_ERR_OK) {
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
    return err;
}

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
        error == PTC_ERR_OK && status->play_timer_enabled ? 1 : 0,
        error == PTC_ERR_OK && status->restricted_now ? 1 : 0);
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
        state.remaining_minutes = after_status->remaining_available ? after_status->remaining_minutes : -1;
        state.play_timer_enabled = after_status->play_timer_enabled ? 1 : 0;
        state.restricted_now = after_status->restricted_now ? 1 : 0;
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
    case PTC_REQUEST_PROBE_APPLY_TODAY_LIMIT:
        return PTC_OPERATION_PROBE_APPLY_TODAY_LIMIT;
    case PTC_REQUEST_PROBE_PLAY_TIMER_EFFECT:
        return PTC_OPERATION_PROBE_PLAY_TIMER_EFFECT;
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
    if (!decision.dry_run && !caps->play_timer_effect_verified) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, PTC_ERR_PCTL_EFFECT_NOT_VERIFIED, now.day_index, caps);
    }
    if (decision.may_write_pctl) {
        err = apply_target(sysmodule, request, caps, now, ptc_control_mode_name(config->mode), PTC_PCTL_TARGET_LIMIT, token.minutes);
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

static bool effect_snapshot_equal(const PtcPctlSettingsSnapshot *a, const PtcPctlSettingsSnapshot *b)
{
    return a && b && a->size == b->size && a->size <= PTC_PCTL_OPAQUE_SETTINGS_SIZE &&
        memcmp(a->data, b->data, a->size) == 0;
}

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
    for (i = 0; i < snapshot->size && used + 3 < out_size; ++i) {
        int written = snprintf(out + used, out_size - used, "%02x", (unsigned int)snapshot->data[i]);
        if (written < 0 || (size_t)written >= out_size - used) {
            break;
        }
        used += (size_t)written;
    }
}

static void effect_wait(PtcSysmodule *sysmodule, uint32_t milliseconds)
{
    if (sysmodule->time_provider && sysmodule->time_provider->vtable && sysmodule->time_provider->vtable->sleep_ms) {
        sysmodule->time_provider->vtable->sleep_ms(sysmodule->time_provider, milliseconds);
    }
}

static void effect_status_json(char *out, size_t out_size, const PtcPctlStatus *status, PtcErrorCode error)
{
    status_json(out, out_size, status, error);
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
        state.remaining_minutes = active_status->remaining_available ? active_status->remaining_minutes : -1;
        state.play_timer_enabled = active_status->play_timer_enabled ? 1 : 0;
        state.restricted_now = active_status->restricted_now ? 1 : 0;
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

static bool process_probe_play_timer_effect(
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
    uint16_t target_minutes = 5;
    bool captured = false;
    bool raw_target_correct = false;
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
            "not_run", "observe", 0, &before_status, before_error, &active_status, active_error,
            &restored_status, restored_error, false, false, false, false, false, false, "", "", "");
    }

    before_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, &before_status);
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
    if (before_status.remaining_available && before_status.remaining_minutes == 5U) {
        target_minutes = 10;
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
        final_error = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
        failure_stage = "raw_target";
        verdict = "fail";
        goto effect_done;
    }
    for (i = 0; i < 20U; ++i) {
        active_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, &active_status);
        if (active_error == PTC_ERR_OK) {
            timer_enabled_seen = active_status.play_timer_enabled;
            remaining_seen = active_status.remaining_available &&
                active_status.remaining_minutes <= target_minutes + 1U &&
                active_status.remaining_minutes + 1U >= target_minutes;
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
            active_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, &active_status);
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
            restored_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, &restored_status);
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

static bool process_probe(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, PtcCapabilities *caps, PtcClockSnapshot now)
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
        return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), true, caps, now);
    }
    err = backup_before_write(sysmodule, request, ptc_control_mode_name(config->mode));
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, err, now.day_index, caps);
    }
    take_pctl_debug_snapshot(sysmodule, &before);
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
    return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), false, caps, now);
}

static bool process_probe_apply_today_limit(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, const PtcCapabilities *caps, PtcClockSnapshot now)
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
        return write_current_status_result(sysmodule, request, ptc_control_mode_name(config->mode), true, caps, now);
    }

    before_status_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, &before_status);
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
        after_status_error = sysmodule->pctl->vtable->read_status(sysmodule->pctl, &after_status);
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
    if (!decision.dry_run &&
        (request->type == PTC_REQUEST_SET_TODAY_LIMIT ||
            request->type == PTC_REQUEST_ADD_TODAY_MINUTES ||
            request->type == PTC_REQUEST_DISABLE_TODAY_LIMIT ||
            request->type == PTC_REQUEST_BLOCK_TODAY ||
            request->type == PTC_REQUEST_RESTORE_TODAY_POLICY) &&
        !caps->play_timer_effect_verified) {
        append_event(sysmodule, request, "pctl_apply_failed", PTC_ERR_PCTL_EFFECT_NOT_VERIFIED, "play_timer_effect");
        return finish_with_error(sysmodule, request, ptc_control_mode_name(config->mode), false, PTC_ERR_PCTL_EFFECT_NOT_VERIFIED, now.day_index, caps);
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
            err = apply_target(sysmodule, request, caps, now, ptc_control_mode_name(config->mode), target_from_day_rule(active_rule), active_rule.minutes);
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
    case PTC_REQUEST_PROBE_APPLY_TODAY_LIMIT:
        (void)process_probe_apply_today_limit(sysmodule, &request, &config, disable_flag, &caps, now);
        break;
    case PTC_REQUEST_PROBE_PLAY_TIMER_EFFECT:
        (void)process_probe_play_timer_effect(sysmodule, &request, &config, disable_flag, &caps, now);
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
    err = apply_target(sysmodule, NULL, &caps, now, ptc_control_mode_name(config.mode), target_mode, active_rule.minutes);
    if (err != PTC_ERR_OK) {
        return 0;
    }
    {
        PtcPctlTarget target;
        PtcPctlDebugSnapshot before;
        PtcPctlDebugSnapshot after;
        uint32_t ipc_result;
        target.mode = target_mode;
        target.minutes = active_rule.minutes;
        target.weekday = ptc_weekday_from_day_index(now.day_index);
        take_pctl_debug_snapshot(sysmodule, &before);
        err = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
        ipc_result = last_pctl_ipc_result(sysmodule);
        take_pctl_debug_snapshot(sysmodule, &after);
        append_pctl_debug(sysmodule, NULL, "start_timer", ptc_control_mode_name(config.mode), &target, err, ipc_result, &before, &after);
    }
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
