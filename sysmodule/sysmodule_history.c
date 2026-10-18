#include "sysmodule_internal.h"
#include "../third_party/cjson/cJSON.h"

typedef struct {
    uint16_t day_index;
    uint32_t nonce;
    unsigned int version;
    bool used;
    char *output;
    size_t length;
    bool changed;
} PtcLedgerScan;

static bool ledger_number(const cJSON *root, const char *key, uint32_t max, uint32_t *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > max) return false;
    *value = (uint32_t)item->valuedouble;
    return item->valuedouble == *value;
}

static bool visit_ledger_line(const char *line, void *ctx)
{
    PtcLedgerScan *scan = (PtcLedgerScan *)ctx;
    cJSON *root;
    uint32_t day, nonce, version = 1;
    bool valid;
    if (*line == '\0' || strcmp(line, "\r") == 0) return true;
    root = cJSON_ParseWithOpts(line, NULL, true);
    /* Reject ambiguous duplicate keys before interpreting replay records. */
    if (cJSON_IsObject(root)) {
        const cJSON *item, *other;
        for (item = root->child; item; item = item->next) {
            for (other = item->next; other; other = other->next) {
                if (strcmp(item->string, other->string) == 0) {
                    cJSON_Delete(root);
                    return false;
                }
            }
        }
    }
    valid = cJSON_IsObject(root) && ledger_number(root, "day_index", UINT16_MAX, &day) &&
        ledger_number(root, "nonce", PTC_TOKEN_MAX_NONCE, &nonce) &&
        (!cJSON_HasObjectItem(root, "token_version") || ledger_number(root, "token_version", 2, &version)) &&
        (version == 1 || version == 2) && (version != 2 || nonce <= PTC_TOKEN_V2_MAX_NONCE);
    cJSON_Delete(root);
    if (!valid) return false;
    if (day == scan->day_index && nonce == scan->nonce && version == scan->version) scan->used = true;
    if (scan->output) {
        char normalized[128];
        int length;
        /* Only expired dates are discarded. Preserve future records if the
           clock has moved backwards; never forget a still-redeemable nonce. */
        if (day < scan->day_index) { scan->changed = true; return true; }
        length = version == 2
            ? snprintf(normalized, sizeof(normalized), "{\"day_index\":%u,\"nonce\":%lu,\"token_version\":2}\n",
                (unsigned)day, (unsigned long)nonce)
            : snprintf(normalized, sizeof(normalized), "{\"day_index\":%u,\"nonce\":%lu}\n",
                (unsigned)day, (unsigned long)nonce);
        if (length < 0 || (size_t)length >= sizeof(normalized)) return false;
        if (strstr(scan->output, normalized)) { scan->changed = true; return true; }
        if (strlen(line) != (size_t)length - 1 ||
            strncmp(line, normalized, (size_t)length - 1) != 0) scan->changed = true;
        /* Reserve room for the next append so a successful grant remains
           recoverable even at the bounded same-day legacy ledger limit. */
        if (length < 0 || scan->length + (size_t)length + 128 >= PTC_ACTIVITY_HISTORY_FILE_SIZE) return false;
        memcpy(scan->output + scan->length, normalized, (size_t)length);
        scan->length += (size_t)length;
        scan->output[scan->length] = '\0';
    }
    return true;
}

static PtcErrorCode scan_ledger(PtcSysmodule *sysmodule, PtcLedgerScan *scan)
{
    char path[320], line[256];
    PtcStorageMetadata meta;
    join_path(path, sizeof(path), sysmodule->app_root, "ledger/used_nonces.jsonl");
    if (!sysmodule->storage->vtable->metadata ||
        !sysmodule->storage->vtable->metadata(sysmodule->storage, path, &meta)) return PTC_ERR_STORAGE_READ_FAILED;
    if (meta.type == PTC_STORAGE_ENTRY_MISSING) return PTC_ERR_OK;
    if (meta.type != PTC_STORAGE_ENTRY_FILE || !sysmodule->storage->vtable->read_lines ||
        !sysmodule->storage->vtable->read_lines(sysmodule->storage, path, line, sizeof(line),
            visit_ledger_line, scan)) return PTC_ERR_STORAGE_READ_FAILED;
    return PTC_ERR_OK;
}

PtcErrorCode check_nonce_used(PtcSysmodule *sysmodule, uint16_t day_index, uint32_t nonce,
                            unsigned int version, bool *used)
{
    PtcLedgerScan scan = {0};
    PtcErrorCode error;
    scan.day_index = day_index; scan.nonce = nonce; scan.version = version;
    *used = false;
    error = scan_ledger(sysmodule, &scan);
    if (error == PTC_ERR_OK) *used = scan.used;
    return error;
}

PtcErrorCode compact_nonce_ledger(PtcSysmodule *sysmodule, uint16_t day_index)
{
    PtcLedgerScan scan = {0};
    PtcErrorCode error;
    char path[320];
    scan.day_index = day_index;
    scan.output = (char *)calloc(PTC_ACTIVITY_HISTORY_FILE_SIZE, 1);
    if (!scan.output) return PTC_ERR_STORAGE_READ_FAILED;
    error = scan_ledger(sysmodule, &scan);
    if (error == PTC_ERR_OK && scan.changed) {
        join_path(path, sizeof(path), sysmodule->app_root, "ledger/used_nonces.jsonl");
        if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, scan.output))
            error = PTC_ERR_STORAGE_WRITE_FAILED;
    }
    free(scan.output);
    return error;
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
static bool save_daily_summary(PtcSysmodule *sysmodule, const PtcDailySummaryRecord *record, bool only_existing)
{
    char path[320];
    /* History grows to two source rows per date; keep file buffers off the
       bounded main-thread stack. Failure remains a best-effort stats failure. */
    char *existing = calloc(1, PTC_DAILY_SUMMARY_FILE_SIZE);
    char *output = calloc(1, PTC_DAILY_SUMMARY_FILE_SIZE);
    char formatted[PTC_DAILY_SUMMARY_LINE_SIZE];
    PtcDailySummaryRecord records[PTC_DAILY_SUMMARY_MAX_RECORDS];
    char *cursor;
    size_t count = 0;
    size_t used = 0;
    size_t index;
    bool replaced = false;
    bool ok = false;
    if (!existing || !output) goto done;
    join_path(path, sizeof(path), sysmodule->app_root, "stats/daily-summaries.jsonl");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, path)) {
        if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, existing, PTC_DAILY_SUMMARY_FILE_SIZE)) goto done;
    } else existing[0] = '\0';
    cursor = existing;
    while (*cursor) {
        char *newline = strchr(cursor, '\n');
        size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
        char line[PTC_DAILY_SUMMARY_LINE_SIZE];
        if (length == 0) { cursor = newline ? newline + 1 : cursor + length; continue; }
        PtcDailySummaryRecord parsed;
        if (length >= sizeof(line)) goto done;
        memcpy(line, cursor, length);
        line[length] = '\0';
        if (!ptc_daily_summary_parse_line(line, &parsed)) goto done;
        for (index = 0; index < count; ++index)
            if (records[index].day_index == parsed.day_index && records[index].direct_source == parsed.direct_source) break;
        if (index < count) ptc_daily_summary_merge(&records[index], &parsed);
        else {
            if (count == PTC_DAILY_SUMMARY_MAX_RECORDS) goto done;
            records[count++] = parsed;
        }
        if (!newline) break;
        cursor = newline + 1;
    }
    for (index = 0; index < count; ++index) {
        if (records[index].day_index == record->day_index && records[index].direct_source == record->direct_source) {
            ptc_daily_summary_merge(&records[index], record);
            replaced = true;
        }
    }
    /* Retain 30 distinct dates, including both old estimates and direct rows.
       Removing a date removes both sources; never silently truncate a file. */
    if (!replaced) {
        if (only_existing) goto done;
        size_t dates = 0;
        uint16_t oldest = UINT16_MAX;
        bool date_present = false;
        for (index = 0; index < count; ++index) {
            size_t prior;
            if (records[index].day_index == record->day_index) date_present = true;
            if (records[index].day_index < oldest) oldest = records[index].day_index;
            for (prior = 0; prior < index; ++prior)
                if (records[prior].day_index == records[index].day_index) break;
            if (prior == index) ++dates;
        }
        if (!date_present && dates >= 30u) {
            size_t kept = 0;
            if (record->day_index <= oldest) { ok = true; goto done; }
            for (index = 0; index < count; ++index)
                if (records[index].day_index != oldest) records[kept++] = records[index];
            count = kept;
        }
        if (count == PTC_DAILY_SUMMARY_MAX_RECORDS) goto done;
        records[count++] = *record;
    }
    for (index = 0; index < count; ++index) {
        size_t length;
        if (!ptc_daily_summary_format_line(formatted, sizeof(formatted), &records[index])) goto done;
        length = strlen(formatted);
        if (used + length + 2u > PTC_DAILY_SUMMARY_FILE_SIZE) goto done;
        memcpy(output + used, formatted, length);
        used += length;
        output[used++] = '\n';
    }
    output[used] = '\0';
    ok = sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, output);
done:
    free(existing);
    free(output);
    return ok;
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
    case PTC_REQUEST_ACTIVATE_HOLIDAY_CALENDAR: return "calendar_activate";
    case PTC_REQUEST_SET_SCHEDULED_OVERRIDE: return "scheduled_update";
    case PTC_REQUEST_SET_AUTONOMY_POLICY: return "autonomy_update";
    case PTC_REQUEST_SET_BEDTIME_POLICY: return "bedtime_update";
    case PTC_REQUEST_SET_EYE_CARE_POLICY: return "eye_care_update";
    case PTC_REQUEST_SKIP_EYE_CARE_BREAK: return "eye_care_skip";
    case PTC_REQUEST_CONFIRM_BEDTIME_REQUIREMENTS: return "bedtime_confirm";
    case PTC_REQUEST_SKIP_BEDTIME: return "bedtime_skip";
    case PTC_REQUEST_CLEAR_BEDTIME_SKIP: return "bedtime_skip_cleared";
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
    bool read_failed;
    if (!load_setup_state(sysmodule, &setup) || strcmp(setup.phase, "active") != 0 ||
        !load_rules(sysmodule, &rules) || !load_state(sysmodule, &runtime_state)) return 0;
    memset(&pctl_status, 0, sizeof(pctl_status));
    read_failed = sysmodule->pctl->vtable->read_status(sysmodule->pctl,
            ptc_weekday_from_day_index(now.day_index), &pctl_status) != PTC_ERR_OK;
    if (read_failed) memset(&pctl_status, 0, sizeof(pctl_status));
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
    record.consumed_available = consumed >= 0 && consumed <= 1440;
    record.consumed_minutes = consumed >= 0 && consumed <= 1440 ? (uint16_t)consumed : 0u;
    record.granted_minutes = runtime_state.summary_day_index == now.day_index
        ? runtime_state.summary_grant_minutes : 0u;
    if (pctl_status.usage_supported || read_failed) {
        PtcUsageCounter sample;
        PtcClockSnapshot observed = usage_sample_clock(sysmodule, now);
        record.direct_source = true;
        record.observed_at = observed.unix_seconds;
        record.consumed_available = !read_failed && direct_usage_sample(&pctl_status, observed, &sample) &&
            sample.total_ns <= PTC_DAILY_SUMMARY_DAY_NS;
        record.latest_available = record.consumed_available;
        record.total_ns = record.consumed_available ? sample.total_ns : 0;
        record.captured_at = record.consumed_available ? sample.captured_at : 0;
        record.consumed_minutes = (uint16_t)(record.total_ns / PTC_USAGE_MINUTE_NS);
        if (record.observed_at <= 0) return 0;
    }
    return save_daily_summary(sysmodule, &record, read_failed) ? 1 : 0;
}
#endif
