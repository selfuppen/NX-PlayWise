#include "sysmodule_internal.h"

void append_event(PtcSysmodule *sysmodule, const PtcRequest *request, const char *event, PtcErrorCode error, const char *detail)
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

void take_pctl_debug_snapshot(PtcSysmodule *sysmodule, PtcPctlDebugSnapshot *out)
{
    empty_pctl_debug_snapshot(out);
    if (!sysmodule->pctl || !sysmodule->pctl->vtable || !sysmodule->pctl->vtable->debug_snapshot) {
        return;
    }
    (void)sysmodule->pctl->vtable->debug_snapshot(sysmodule->pctl, out);
}

uint32_t last_pctl_ipc_result(PtcSysmodule *sysmodule)
{
    if (!sysmodule->pctl || !sysmodule->pctl->vtable || !sysmodule->pctl->vtable->last_ipc_result) {
        return 0;
    }
    return sysmodule->pctl->vtable->last_ipc_result(sysmodule->pctl);
}

void append_pctl_debug(
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

bool nonce_used_v1(uint16_t day_index, uint32_t nonce, void *ctx)
{
    return ledger_nonce_used((PtcSysmodule *)ctx, day_index, nonce, 1u);
}

bool nonce_used_v2(uint16_t day_index, uint32_t nonce, void *ctx)
{
    return ledger_nonce_used((PtcSysmodule *)ctx, day_index, nonce, 2u);
}

bool consume_nonce(PtcSysmodule *sysmodule, const PtcRequest *request, uint16_t day_index, uint32_t nonce, unsigned int token_version)
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

bool save_redemption_history(
    PtcSysmodule *sysmodule,
    const PtcRedemptionHistoryRecord *record)
{
    char path[320];
    char existing[PTC_REDEMPTION_HISTORY_FILE_SIZE];
    char output[PTC_REDEMPTION_HISTORY_FILE_SIZE];
    char new_line[PTC_REDEMPTION_HISTORY_LINE_SIZE];
    char *lines[PTC_REDEMPTION_HISTORY_MAX_RECORDS];
    char *cursor;
    size_t count = 0;
    size_t used = 0;
    size_t index;
    join_path(path, sizeof(path), sysmodule->app_root, "ledger/redemption-history.jsonl");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, path)) {
        if (!sysmodule->storage->vtable->read_text(
                sysmodule->storage, path, existing, sizeof(existing))) return false;
    } else {
        existing[0] = '\0';
    }
    cursor = existing;
    while (*cursor) {
        char *newline = strchr(cursor, '\n');
        size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
        PtcRedemptionHistoryRecord parsed;
        if (length == 0) {
            cursor = newline ? newline + 1 : cursor + length;
            continue;
        }
        if (length >= PTC_REDEMPTION_HISTORY_LINE_SIZE) return false;
        if (newline) *newline = '\0';
        if (!ptc_redemption_history_parse_line(cursor, &parsed)) return false;
        if (count == PTC_REDEMPTION_HISTORY_MAX_RECORDS) {
            memmove(lines, lines + 1, (PTC_REDEMPTION_HISTORY_MAX_RECORDS - 1u) * sizeof(lines[0]));
            count = PTC_REDEMPTION_HISTORY_MAX_RECORDS - 1u;
        }
        lines[count++] = cursor;
        if (!newline) break;
        cursor = newline + 1;
    }
    if (!ptc_redemption_history_format_line(new_line, sizeof(new_line), record)) return false;
    if (count == PTC_REDEMPTION_HISTORY_MAX_RECORDS) {
        memmove(lines, lines + 1, (PTC_REDEMPTION_HISTORY_MAX_RECORDS - 1u) * sizeof(lines[0]));
        count = PTC_REDEMPTION_HISTORY_MAX_RECORDS - 1u;
    }
    for (index = 0; index < count; ++index) {
        size_t length = strlen(lines[index]);
        if (used + length + 1u >= sizeof(output)) return false;
        memcpy(output + used, lines[index], length);
        used += length;
        output[used++] = '\n';
    }
    {
        size_t length = strlen(new_line);
        if (used + length + 2u > sizeof(output)) return false;
        memcpy(output + used, new_line, length);
        used += length;
        output[used++] = '\n';
        output[used] = '\0';
    }
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, output);
}

bool clear_redemption_history(PtcSysmodule *sysmodule)
{
    char path[320];
    join_path(path, sizeof(path), sysmodule->app_root, "ledger/redemption-history.jsonl");
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, "");
}

bool save_activity_history(PtcSysmodule *sysmodule, const PtcActivityHistoryRecord *record)
{
    char path[320];
    char existing[PTC_ACTIVITY_HISTORY_FILE_SIZE];
    char output[PTC_ACTIVITY_HISTORY_FILE_SIZE];
    char new_line[PTC_ACTIVITY_HISTORY_LINE_SIZE];
    char *lines[PTC_ACTIVITY_HISTORY_MAX_RECORDS];
    char *cursor;
    size_t count = 0;
    size_t used = 0;
    size_t index;
    join_path(path, sizeof(path), sysmodule->app_root, "activity/history.jsonl");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, path)) {
        if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, existing, sizeof(existing))) return false;
    } else existing[0] = '\0';
    cursor = existing;
    while (*cursor) {
        char *newline = strchr(cursor, '\n');
        size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
        PtcActivityHistoryRecord parsed;
        if (length == 0) { cursor = newline ? newline + 1 : cursor + length; continue; }
        if (length >= PTC_ACTIVITY_HISTORY_LINE_SIZE) return false;
        if (newline) *newline = '\0';
        if (!ptc_activity_history_parse_line(cursor, &parsed)) return false;
        if (count == PTC_ACTIVITY_HISTORY_MAX_RECORDS) {
            memmove(lines, lines + 1, (PTC_ACTIVITY_HISTORY_MAX_RECORDS - 1u) * sizeof(lines[0]));
            count = PTC_ACTIVITY_HISTORY_MAX_RECORDS - 1u;
        }
        lines[count++] = cursor;
        if (!newline) break;
        cursor = newline + 1;
    }
    if (!ptc_activity_history_format_line(new_line, sizeof(new_line), record)) return false;
    if (count == PTC_ACTIVITY_HISTORY_MAX_RECORDS) {
        memmove(lines, lines + 1, (PTC_ACTIVITY_HISTORY_MAX_RECORDS - 1u) * sizeof(lines[0]));
        count = PTC_ACTIVITY_HISTORY_MAX_RECORDS - 1u;
    }
    for (index = 0; index < count; ++index) {
        size_t length = strlen(lines[index]);
        if (used + length + 1u >= sizeof(output)) return false;
        memcpy(output + used, lines[index], length);
        used += length;
        output[used++] = '\n';
    }
    {
        size_t length = strlen(new_line);
        if (used + length + 2u > sizeof(output)) return false;
        memcpy(output + used, new_line, length);
        used += length;
        output[used++] = '\n';
        output[used] = '\0';
    }
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, output);
}

bool clear_activity_history(PtcSysmodule *sysmodule)
{
    char path[320];
    join_path(path, sizeof(path), sysmodule->app_root, "activity/history.jsonl");
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, "");
}

#ifndef PLAYWISE_DEVICE_LAB
static bool save_daily_summary(PtcSysmodule *sysmodule, const PtcDailySummaryRecord *record)
{
    char path[320];
    char existing[PTC_DAILY_SUMMARY_FILE_SIZE];
    char output[PTC_DAILY_SUMMARY_FILE_SIZE];
    char formatted[PTC_DAILY_SUMMARY_LINE_SIZE];
    PtcDailySummaryRecord records[PTC_DAILY_SUMMARY_MAX_RECORDS];
    char *cursor;
    size_t count = 0;
    size_t used = 0;
    size_t index;
    bool replaced = false;
    join_path(path, sizeof(path), sysmodule->app_root, "stats/daily-summaries.jsonl");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, path)) {
        if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, existing, sizeof(existing))) return false;
    } else existing[0] = '\0';
    cursor = existing;
    while (*cursor) {
        char *newline = strchr(cursor, '\n');
        size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
        char line[PTC_DAILY_SUMMARY_LINE_SIZE];
        if (length == 0) { cursor = newline ? newline + 1 : cursor + length; continue; }
        if (length >= sizeof(line)) return false;
        memcpy(line, cursor, length);
        line[length] = '\0';
        if (!ptc_daily_summary_parse_line(line, &records[count])) return false;
        if (records[count].day_index == record->day_index) {
            if (record->captured_at >= records[count].captured_at) records[count] = *record;
            replaced = true;
        }
        if (++count == PTC_DAILY_SUMMARY_MAX_RECORDS) break;
        if (!newline) break;
        cursor = newline + 1;
    }
    if (!replaced) {
        if (count == PTC_DAILY_SUMMARY_MAX_RECORDS) {
            size_t oldest = 0;
            for (index = 1; index < count; ++index) {
                if (records[index].day_index < records[oldest].day_index) oldest = index;
            }
            if (record->day_index <= records[oldest].day_index) return true;
            if (oldest + 1u < count) {
                memmove(records + oldest, records + oldest + 1u,
                    (count - oldest - 1u) * sizeof(records[0]));
            }
            count = PTC_DAILY_SUMMARY_MAX_RECORDS - 1u;
        }
        records[count++] = *record;
    }
    for (index = 0; index < count; ++index) {
        size_t length;
        if (!ptc_daily_summary_format_line(formatted, sizeof(formatted), &records[index])) return false;
        length = strlen(formatted);
        if (used + length + 2u > sizeof(output)) return false;
        memcpy(output + used, formatted, length);
        used += length;
        output[used++] = '\n';
    }
    output[used] = '\0';
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, output);
}
#endif

static bool load_daily_summary_aggregate(PtcSysmodule *sysmodule, uint16_t today,
    PtcDailySummaryAggregate *aggregate)
{
    char path[320];
    char text[PTC_DAILY_SUMMARY_FILE_SIZE];
    PtcDailySummaryRecord records[PTC_DAILY_SUMMARY_MAX_RECORDS];
    char *cursor;
    size_t count = 0;
    join_path(path, sizeof(path), sysmodule->app_root, "stats/daily-summaries.jsonl");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) return false;
    cursor = text;
    while (*cursor && count < PTC_DAILY_SUMMARY_MAX_RECORDS) {
        char *newline = strchr(cursor, '\n');
        if (newline) *newline = '\0';
        if (*cursor && !ptc_daily_summary_parse_line(cursor, &records[count++])) return false;
        if (!newline) break;
        cursor = newline + 1;
    }
    ptc_daily_summary_aggregate(records, count, today, aggregate);
    return true;
}

static int64_t result_remaining_minutes(const PtcPctlStatus *status)
{
    return status->remaining_available ? (int64_t)status->remaining_minutes : -1;
}

int64_t result_played_minutes(const PtcPctlStatus *status)
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

void result_state_from_pctl(
    PtcResultState *state,
    uint16_t day_index,
    const PtcPctlStatus *status)
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
}

bool write_result(PtcSysmodule *sysmodule, const char *request_id, const char *json)
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


static const char *activity_action_for_request(PtcRequestType type)
{
    switch (type) {
    case PTC_REQUEST_SET_TODAY_LIMIT: return "today_limit";
    case PTC_REQUEST_ADD_TODAY_MINUTES: return "today_add";
    case PTC_REQUEST_DISABLE_TODAY_LIMIT: return "today_unlimited";
    case PTC_REQUEST_RESTORE_TODAY_POLICY: return "today_restore";
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE: return "weekly_update";
    case PTC_REQUEST_SET_HOLIDAY_POLICY: return "holiday_update";
    case PTC_REQUEST_SET_SCHEDULED_OVERRIDE: return "scheduled_update";
    case PTC_REQUEST_SET_AUTONOMY_POLICY: return "autonomy_update";
    case PTC_REQUEST_SET_BEDTIME_POLICY: return "bedtime_update";
    case PTC_REQUEST_CONFIRM_BEDTIME_REQUIREMENTS: return "bedtime_confirm";
    case PTC_REQUEST_SKIP_BEDTIME: return "bedtime_skip";
    case PTC_REQUEST_DISABLE_BEDTIME: return "bedtime_disable";
    case PTC_REQUEST_OFFLINE_CODE: return "offline_grant";
    case PTC_REQUEST_CLAIM_DAILY_BUFFER: return "daily_buffer";
    default: return NULL;
    }
}

bool record_activity(PtcSysmodule *sysmodule, const PtcRequest *request,
    PtcClockSnapshot now, uint16_t minutes, uint16_t effective_minutes)
{
    PtcActivityHistoryRecord record;
    const char *action = activity_action_for_request(request ? request->type : PTC_REQUEST_UNKNOWN);
    if (!action) return true;
    memset(&record, 0, sizeof(record));
    record.occurred_at = now.unix_seconds;
    record.day_index = now.day_index;
    snprintf(record.action, sizeof(record.action), "%s", action);
    record.minutes = minutes;
    record.effective_minutes = effective_minutes;
    return save_activity_history(sysmodule, &record);
}

bool bedtime_overlay_verified(PtcSysmodule *sysmodule)
{
    char path[320];
    char text[512];
    char build[1024];
    char boot_id[65];
    char ready_release_id[96];
    char build_release_id[96];
    char ready_fingerprint[65];
    char current_fingerprint[65];
    join_path(path, sizeof(path), sysmodule->app_root, "overlay/ready.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text)) ||
        !json_string(text, "boot_id", boot_id, sizeof(boot_id)) ||
        strcmp(boot_id, sysmodule->boot_id) != 0 ||
        !json_string(text, "release_id", ready_release_id, sizeof(ready_release_id)) ||
        !json_string(text, "environment_fingerprint", ready_fingerprint, sizeof(ready_fingerprint)) ||
        !sysmodule_environment_fingerprint(sysmodule, current_fingerprint) ||
        strcmp(ready_fingerprint, current_fingerprint) != 0) {
        return false;
    }
    join_path(path, sizeof(path), sysmodule->app_root, "build.json");
    return sysmodule->storage->vtable->read_text(sysmodule->storage, path, build, sizeof(build)) &&
        json_string(build, "release_id", build_release_id, sizeof(build_release_id)) &&
        strcmp(ready_release_id, build_release_id) == 0;
}

static void fill_bedtime_result_state(PtcSysmodule *sysmodule, PtcResultState *state,
    const PtcRules *rules, const PtcRuntimeState *runtime_state,
    const PtcPctlStatus *pctl_status, PtcClockSnapshot now)
{
    PtcBedtimeEvaluation evaluation = ptc_bedtime_evaluate(
        rules, now.day_index, ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
    char fingerprint[65];
    unsigned int offset;
    state->bedtime_enabled = rules->bedtime.enabled;
    state->bedtime_active = evaluation.active;
    state->bedtime_skipped = evaluation.active &&
        runtime_state->bedtime_skipped_instance_id == evaluation.window_instance_id;
    state->bedtime_window_instance_id = evaluation.window_instance_id;
    state->bedtime_start_day_index = evaluation.start_day_index;
    state->bedtime_start_minute = evaluation.start_minute;
    state->bedtime_end_minute = evaluation.end_minute;
    state->bedtime_source = ptc_bedtime_source_name(evaluation.source);
    state->bedtime_official_setting_confirmed = rules->bedtime.confirmation_version > 0 &&
        rules->bedtime.official_setting_confirmed_at > 0 && rules->bedtime.confirmed_environment[0] != '\0' &&
        sysmodule_environment_fingerprint(sysmodule, fingerprint) &&
        strcmp(fingerprint, rules->bedtime.confirmed_environment) == 0;
    state->bedtime_overlay_verified = bedtime_overlay_verified(sysmodule);
    state->bedtime_recovery_phase = runtime_state->bedtime_enforced ? "restricted" : "idle";
    state->daily_restriction_active = (!evaluation.active || state->bedtime_skipped) &&
        pctl_status->limited_today &&
        pctl_status->remaining_available && pctl_status->remaining_minutes == 0u;
    if (!rules->bedtime.enabled) return;
    if (runtime_state->bedtime_skipped_instance_id != 0) {
        if (evaluation.active &&
            evaluation.window_instance_id == runtime_state->bedtime_skipped_instance_id) {
            state->bedtime_skipped_window_available = true;
            state->bedtime_skipped_window_instance_id = evaluation.window_instance_id;
            state->bedtime_skipped_start_day_index = evaluation.start_day_index;
            state->bedtime_skipped_start_minute = evaluation.start_minute;
            state->bedtime_skipped_end_minute = evaluation.end_minute;
            state->bedtime_skipped_source = ptc_bedtime_source_name(evaluation.source);
        } else {
            for (offset = 0; offset < 8u; ++offset) {
                uint16_t start_day = (uint16_t)(now.day_index + offset);
                PtcEffectiveBedtime skipped = ptc_bedtime_resolve_start_day(
                    rules, start_day, ptc_weekday_from_day_index(start_day));
                uint64_t instance_id;
                if (!skipped.window.enabled ||
                    (offset == 0u && now.minute_of_day >= skipped.window.start_minute)) continue;
                instance_id = ptc_bedtime_window_instance_id(start_day, skipped.window.start_minute);
                if (instance_id != runtime_state->bedtime_skipped_instance_id) continue;
                state->bedtime_skipped_window_available = true;
                state->bedtime_skipped_window_instance_id = instance_id;
                state->bedtime_skipped_start_day_index = start_day;
                state->bedtime_skipped_start_minute = skipped.window.start_minute;
                state->bedtime_skipped_end_minute = skipped.window.end_minute;
                state->bedtime_skipped_source = ptc_bedtime_source_name(skipped.source);
                break;
            }
        }
    }
    for (offset = 0; offset < 8u; ++offset) {
        uint16_t start_day = (uint16_t)(now.day_index + offset);
        PtcEffectiveBedtime next = ptc_bedtime_resolve_start_day(
            rules, start_day, ptc_weekday_from_day_index(start_day));
        uint64_t instance_id;
        if (!next.window.enabled) continue;
        if (offset == 0u && now.minute_of_day >= next.window.start_minute) continue;
        instance_id = ptc_bedtime_window_instance_id(start_day, next.window.start_minute);
        if (instance_id == runtime_state->bedtime_skipped_instance_id) continue;
        state->bedtime_next_available = true;
        state->bedtime_next_start_day_index = start_day;
        state->bedtime_next_start_minute = next.window.start_minute;
        state->bedtime_next_end_minute = next.window.end_minute;
        state->bedtime_next_window_instance_id = instance_id;
        break;
    }
}

void fill_extended_result_state(PtcSysmodule *sysmodule, PtcResultState *state,
    const PtcRules *rules, const PtcRuntimeState *runtime_state,
    const PtcPctlStatus *pctl_status, PtcClockSnapshot now)
{
    unsigned int i;
    PtcDailySummaryAggregate aggregate;
    for (i = 0; i < PTC_RESULT_FORECAST_DAYS; ++i) {
        uint16_t forecast_day = (uint16_t)(now.day_index + i);
        PtcEffectiveRule forecast = ptc_rules_resolve(
            rules, forecast_day, ptc_weekday_from_day_index(forecast_day));
        state->forecast[i].day_index = forecast_day;
        state->forecast[i].mode = forecast.rule.mode == PTC_RULE_MODE_UNLIMITED ? 2 : 1;
        state->forecast[i].minutes = forecast.rule.minutes;
        state->forecast[i].rule_source = ptc_rule_source_name(forecast.source);
        state->forecast[i].calendar_covered = forecast.calendar_covered;
    }
    state->daily_buffer_minutes = rules->autonomy_policy.daily_buffer_minutes;
    state->daily_buffer_claimed = runtime_state->buffer_claimed &&
        runtime_state->buffer_claim_day_index == now.day_index;
    state->daily_buffer_available = false;
    if (state->daily_buffer_minutes == 0) state->daily_buffer_reason = "disabled";
    else if (state->daily_buffer_claimed) state->daily_buffer_reason = "already_claimed";
    else if (!pctl_status->limited_today || pctl_status->unrestricted_today) {
        state->daily_buffer_reason = "today_unlimited";
    } else {
        state->daily_buffer_available = true;
        state->daily_buffer_reason = "available";
    }
    if (load_daily_summary_aggregate(sysmodule, now.day_index, &aggregate)) {
        state->usage_summary_available = aggregate.known_days_30 > 0;
        state->usage_known_days_7 = aggregate.known_days_7;
        state->usage_consumed_minutes_7 = aggregate.consumed_minutes_7;
        state->usage_known_days_30 = aggregate.known_days_30;
        state->usage_consumed_minutes_30 = aggregate.consumed_minutes_30;
    }
    fill_bedtime_result_state(sysmodule, state, rules, runtime_state, pctl_status, now);
}

bool write_current_status_result(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *mode,
    bool dry_run,
    PtcClockSnapshot now,
    bool commits_recovery)
{
    PtcPctlStatus pctl_status;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcResultState state;
    char json[6144];
    PtcErrorCode err;
    PtcEffectiveRule effective;
    const PtcHolidayCalendarInfo *calendar_info;
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    if (!load_rules(sysmodule, &rules)) {
        return finish_with_error(sysmodule, request, mode, dry_run, PTC_ERR_RULES_INVALID, now.day_index);
    }
    if (!load_state(sysmodule, &runtime_state)) {
        return finish_with_error(sysmodule, request, mode, dry_run, PTC_ERR_BAD_REQUEST, now.day_index);
    }
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, mode, dry_run, err, now.day_index);
    }
    result_state_from_pctl(&state, now.day_index, &pctl_status);
    effective = ptc_rules_resolve(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    state.rule_source = ptc_rule_source_name(effective.source);
    state.calendar_covered = effective.calendar_covered;
    calendar_info = ptc_holiday_calendar_info();
    if (ptc_date_from_day_index(now.day_index, &year, &month, &day)) {
        state.calendar_update_warning = rules.holiday_enabled &&
            (year > calendar_info->last_year ||
                (year == calendar_info->last_year && month == 12 && day >= 2));
    }
    fill_extended_result_state(sysmodule, &state, &rules, &runtime_state, &pctl_status, now);
    (void)ptc_result_ok_json(json, sizeof(json), request->request_id, request->type_text, mode, dry_run, &state, now.unix_seconds);
    append_event(sysmodule, request, "result_ok", PTC_ERR_OK, "");
    return write_result_with_setup(sysmodule, request->request_id, json, commits_recovery);
}

bool process_status(PtcSysmodule *sysmodule, const PtcRequest *request,
    PtcClockSnapshot now)
{
    return write_current_status_result(sysmodule, request, "release", true, now, false);
}

#ifndef PLAYWISE_DEVICE_LAB
int usage_summary_tick(PtcSysmodule *sysmodule, PtcClockSnapshot now)
{
    PtcSetupState setup;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcPctlStatus pctl_status;
    PtcEffectiveRule effective;
    PtcDailySummaryRecord record;
    int64_t consumed;
    if (!load_setup_state(sysmodule, &setup) || strcmp(setup.phase, "active") != 0 ||
        !load_rules(sysmodule, &rules) || !load_state(sysmodule, &runtime_state) ||
        sysmodule->pctl->vtable->read_status(sysmodule->pctl,
            ptc_weekday_from_day_index(now.day_index), &pctl_status) != PTC_ERR_OK) return 0;
    memset(&record, 0, sizeof(record));
    effective = ptc_rules_resolve(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    record.day_index = now.day_index;
    record.captured_at = now.unix_seconds;
    snprintf(record.rule_source, sizeof(record.rule_source), "%s", ptc_rule_source_name(effective.source));
    record.limited = pctl_status.limited_today;
    record.configured_minutes = pctl_status.configured_minutes_available
        ? pctl_status.configured_minutes : (effective.rule.mode == PTC_RULE_MODE_LIMIT ? effective.rule.minutes : 0u);
    record.remaining_available = pctl_status.remaining_available &&
        pctl_status.remaining_minutes <= 1440u;
    record.remaining_minutes = record.remaining_available
        ? (uint16_t)pctl_status.remaining_minutes : 0u;
    consumed = result_played_minutes(&pctl_status);
    record.consumed_available = consumed >= 0;
    record.consumed_minutes = consumed >= 0 && consumed <= 1440 ? (uint16_t)consumed : 0u;
    record.granted_minutes = runtime_state.summary_day_index == now.day_index
        ? runtime_state.summary_grant_minutes : 0u;
    return save_daily_summary(sysmodule, &record) ? 1 : 0;
}
#endif
