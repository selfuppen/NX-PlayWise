#include "sysmodule_internal.h"

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
