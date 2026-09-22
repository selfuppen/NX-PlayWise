#include "sysmodule_internal.h"

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
