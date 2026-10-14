#include "result_summary.h"
#include "ui_language.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#include "../common/protocol/result_builder.h"
#include "../third_party/cjson/cJSON.h"

static const char *string_value(const cJSON *object, const char *key)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static int number_value(const cJSON *object, const char *key, int fallback)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static bool bool_value(const cJSON *object, const char *key, bool fallback)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

static unsigned long long u64_value(const cJSON *object, const char *key)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsNumber(item) && item->valuedouble >= 0
        ? (unsigned long long)item->valuedouble : 0ULL;
}

bool ptc_companion_result_summary_parse(const char *result_json, PtcCompanionResultSummary *out)
{
    cJSON *root;
    const cJSON *state;
    const cJSON *error;
    const cJSON *preview;
    const cJSON *bedtime;
    const cJSON *eye_care;
    const cJSON *restriction_reasons;
    const char *status;
    if (!out || !result_json || ptc_result_validate(result_json) != PTC_ERR_OK) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    root = cJSON_Parse(result_json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    status = string_value(root, "status");
    snprintf(out->status, sizeof(out->status), "%s", status);
    snprintf(out->type, sizeof(out->type), "%s", string_value(root, "type"));
    out->ok = strcmp(status, "ok") == 0;
    {
        const cJSON *completed = cJSON_GetObjectItemCaseSensitive(root, "completed_at");
        if (cJSON_IsNumber(completed) && completed->valuedouble > 0 &&
            completed->valuedouble < (double)INT64_MAX &&
            completed->valuedouble == (double)(int64_t)completed->valuedouble) {
            out->completed_at = (int64_t)completed->valuedouble;
        }
    }
    state = cJSON_GetObjectItemCaseSensitive(root, "state");
    out->disable_flag_present = bool_value(cJSON_GetObjectItemCaseSensitive(root, "setup"),
        "disable_flag_present", false);
    out->day_index = number_value(state, "day_index", -1);
    {
        const cJSON *clock = cJSON_GetObjectItemCaseSensitive(state, "clock");
        const cJSON *day = cJSON_GetObjectItemCaseSensitive(clock, "day_index");
        const cJSON *second = cJSON_GetObjectItemCaseSensitive(clock, "second_of_day");
        const cJSON *captured = cJSON_GetObjectItemCaseSensitive(clock, "captured_at");
        /* Bind civil time to this result; an unrelated timestamp cannot renew it. */
        if (cJSON_IsNumber(day) && out->day_index >= 0 && out->day_index <= 65535 &&
            day->valuedouble == out->day_index && cJSON_IsNumber(second) &&
            second->valuedouble >= 0 && second->valuedouble < 86400 &&
            second->valuedouble == (double)second->valueint &&
            cJSON_IsNumber(captured) && out->completed_at > 0 &&
            captured->valuedouble == (double)out->completed_at) {
            out->local_clock_available = true;
            out->local_second_of_day = second->valueint;
        }
    }
    out->remaining_available = bool_value(state, "remaining_available", false);
    out->remaining_minutes = number_value(state, "remaining_minutes", -1);
    out->played_minutes_available = bool_value(state, "played_minutes_available", false);
    out->played_minutes = number_value(state, "played_minutes", -1);
    {
        const cJSON *usage = cJSON_GetObjectItemCaseSensitive(state, "usage");
        out->usage_present = usage != NULL;
        if (out->usage_present) {
            const cJSON *captured = cJSON_GetObjectItemCaseSensitive(usage, "captured_at");
            const char *total = string_value(usage, "total_ns");
            char *end;
            unsigned long long ns;
            out->played_minutes_available = false;
            out->played_minutes = -1;
            errno = 0;
            ns = strtoull(total, &end, 10);
            if (cJSON_IsObject(usage) && bool_value(usage, "available", false) &&
                strcmp(string_value(usage, "source"), "pctl_1952") == 0 &&
                strcmp(string_value(usage, "scope"), "console_usable_time") == 0 &&
                *total && strspn(total, "0123456789") == strlen(total) && *end == '\0' &&
                errno != ERANGE && ns <= INT64_MAX && cJSON_IsNumber(captured) &&
                captured->valuedouble > 0 && captured->valuedouble < (double)INT64_MAX &&
                captured->valuedouble == (double)(int64_t)captured->valuedouble) {
                out->played_minutes_available = true;
                out->played_minutes = (int)(ns / UINT64_C(60000000000));
                out->usage_captured_at = (int64_t)captured->valuedouble;
                out->usage_ns = (uint64_t)ns;
            }
        }
    }
    {
        const cJSON *forecast = cJSON_GetObjectItemCaseSensitive(state, "forecast");
        const cJSON *today = cJSON_GetArrayItem(forecast, 0);
        const cJSON *day = cJSON_GetObjectItemCaseSensitive(today, "day_index");
        const cJSON *mode = cJSON_GetObjectItemCaseSensitive(today, "mode");
        const cJSON *minutes = cJSON_GetObjectItemCaseSensitive(today, "minutes");
        if (cJSON_IsArray(forecast) && cJSON_IsObject(today) && cJSON_IsNumber(day) &&
            day->valuedouble == out->day_index && out->day_index >= 0 &&
            cJSON_IsNumber(mode) && (mode->valuedouble == 1 || mode->valuedouble == 2) &&
            cJSON_IsNumber(minutes) && minutes->valuedouble >= 0 && minutes->valuedouble <= 1440 &&
            minutes->valuedouble == minutes->valueint) {
            out->daily_rule_available = true;
            out->daily_rule_unlimited = mode->valueint == 2;
            out->daily_rule_minutes = minutes->valueint;
        }
    }
    out->play_timer_enabled = number_value(state, "play_timer_enabled", -1);
    out->restricted_now = number_value(state, "restricted_now", -1);
    out->unrestricted_today = number_value(state, "unrestricted_today", -1);
    out->temporary_unlocked_available = bool_value(state, "temporary_unlocked_available", false);
    out->temporary_unlocked = bool_value(state, "temporary_unlocked", false);
    out->calendar_covered = bool_value(state, "calendar_covered", false);
    out->calendar_update_warning = bool_value(state, "calendar_update_warning", false);
    snprintf(out->rule_source, sizeof(out->rule_source), "%s", string_value(state, "rule_source"));
    bedtime = cJSON_GetObjectItemCaseSensitive(state, "bedtime");
    out->bedtime_available = cJSON_IsObject(bedtime) &&
        cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(bedtime, "enabled")) &&
        cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(bedtime, "active")) &&
        cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(bedtime, "skipped")) &&
        cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(bedtime, "next_available"));
    out->bedtime_enabled = bool_value(bedtime, "enabled", false);
    out->bedtime_active = bool_value(bedtime, "active", false);
    out->bedtime_skipped = bool_value(bedtime, "skipped", false);
    out->bedtime_window_instance_id = u64_value(bedtime, "window_instance_id");
    out->bedtime_start_day_index = number_value(bedtime, "start_day_index", -1);
    out->bedtime_start_minute = number_value(bedtime, "start_minute", -1);
    out->bedtime_end_minute = number_value(bedtime, "end_minute", -1);
    out->bedtime_next_available = bool_value(bedtime, "next_available", false);
    out->bedtime_next_start_day_index = number_value(bedtime, "next_start_day_index", -1);
    out->bedtime_next_start_minute = number_value(bedtime, "next_start_minute", -1);
    out->bedtime_next_end_minute = number_value(bedtime, "next_end_minute", -1);
    out->bedtime_next_window_instance_id = u64_value(bedtime, "next_window_instance_id");
    {
        const cJSON *skipped = cJSON_GetObjectItemCaseSensitive(bedtime, "skipped_window");
        out->bedtime_skipped_window_available = bool_value(skipped, "available", false);
        out->bedtime_skipped_window_instance_id = u64_value(skipped, "window_instance_id");
        out->bedtime_skipped_start_day_index = number_value(skipped, "start_day_index", 0);
        out->bedtime_skipped_start_minute = number_value(skipped, "start_minute", 0);
        out->bedtime_skipped_end_minute = number_value(skipped, "end_minute", 0);
        snprintf(out->bedtime_skipped_source, sizeof(out->bedtime_skipped_source), "%s",
            string_value(skipped, "source"));
    }
    out->bedtime_official_setting_confirmed = bool_value(bedtime, "official_setting_confirmed", false);
    out->bedtime_overlay_verified = bool_value(bedtime, "overlay_verified", false);
    snprintf(out->bedtime_source, sizeof(out->bedtime_source), "%s", string_value(bedtime, "source"));
    snprintf(out->bedtime_recovery_phase, sizeof(out->bedtime_recovery_phase), "%s",
        string_value(bedtime, "recovery_phase"));
    restriction_reasons = cJSON_GetObjectItemCaseSensitive(state, "restriction_reasons");
    out->daily_restriction_active = bool_value(restriction_reasons, "daily_allowance", false);
    eye_care = cJSON_GetObjectItemCaseSensitive(state, "eye_care");
    out->eye_care_available = cJSON_IsObject(eye_care) &&
        cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(eye_care, "enabled")) &&
        cJSON_IsString(cJSON_GetObjectItemCaseSensitive(eye_care, "phase")) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(eye_care, "play_minutes")) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(eye_care, "rest_minutes")) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(eye_care, "used_minutes")) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(eye_care, "rest_remaining_seconds")) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(eye_care, "break_id"));
    out->eye_care_enabled = bool_value(eye_care, "enabled", false);
    out->eye_care_play_minutes = number_value(eye_care, "play_minutes", 40);
    out->eye_care_rest_minutes = number_value(eye_care, "rest_minutes", 10);
    out->eye_care_used_minutes = number_value(eye_care, "used_minutes", 0);
    out->eye_care_rest_remaining_seconds = number_value(eye_care, "rest_remaining_seconds", 0);
    out->eye_care_break_id = u64_value(eye_care, "break_id");
    out->eye_care_unlimited_capped = bool_value(eye_care, "unlimited_capped", false);
    snprintf(out->eye_care_phase, sizeof(out->eye_care_phase), "%s", string_value(eye_care, "phase"));
    out->access_recovery_required =
        (out->daily_restriction_active || (out->bedtime_active && !out->bedtime_skipped) ||
         strcmp(out->eye_care_phase, "resting") == 0) &&
        !(out->temporary_unlocked_available && out->temporary_unlocked);
    {
        const cJSON *dock = cJSON_GetObjectItemCaseSensitive(state, "dock");
        out->dock_policy_available = cJSON_IsObject(dock) &&
            cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(dock, "force_docked")) &&
            cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(dock, "undocked_limit_enabled")) &&
            cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(dock, "undocked_daily_minutes")) &&
            cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(dock, "waived_today"));
        out->dock_available = bool_value(dock, "available", false);
        out->force_docked = bool_value(dock, "force_docked", false);
        out->undocked_limit_enabled = bool_value(dock, "undocked_limit_enabled", false);
        out->undocked_daily_minutes = number_value(dock, "undocked_daily_minutes", 30);
        snprintf(out->operation_mode, sizeof(out->operation_mode), "%s", string_value(dock, "operation_mode"));
        out->dock_supported_available = bool_value(dock, "dock_supported_available", false);
        out->dock_supported = bool_value(dock, "dock_supported", false);
        out->undocked_usage_available = bool_value(dock, "usage_available", false);
        out->undocked_used_minutes = number_value(dock, "used_minutes", -1);
        out->undocked_remaining_minutes = number_value(dock, "remaining_minutes", -1);
        out->dock_waived_today = bool_value(dock, "waived_today", false);
        out->dock_restriction_active = bool_value(restriction_reasons, "dock", false);
        out->dock_unlimited_capped = bool_value(dock, "unlimited_capped", false);
        if (out->dock_restriction_active && !(out->temporary_unlocked_available && out->temporary_unlocked))
            out->access_recovery_required = true;
    }
    {
        const cJSON *autonomy = cJSON_GetObjectItemCaseSensitive(state, "autonomy");
        out->daily_buffer_minutes = number_value(autonomy, "daily_buffer_minutes", 0);
        out->daily_buffer_claimed = bool_value(autonomy, "claimed_today", false);
        out->daily_buffer_available = bool_value(autonomy, "available", false);
        snprintf(out->daily_buffer_reason, sizeof(out->daily_buffer_reason), "%s",
            string_value(autonomy, "reason"));
    }
    preview = cJSON_GetObjectItemCaseSensitive(root, "preview");
    out->preview_available = cJSON_IsObject(preview);
    out->grant_minutes = number_value(preview, "grant_minutes", 0);
    out->remaining_after_available = bool_value(preview, "remaining_after_available", false);
    out->remaining_after_minutes = number_value(preview, "remaining_after_minutes", -1);
    out->effective_add_minutes = number_value(preview, "effective_add_minutes", 0);
    out->preview_capped = bool_value(preview, "capped", false);
    out->converts_unlimited_to_limited = bool_value(preview, "converts_unlimited_to_limited", false);
    error = cJSON_GetObjectItemCaseSensitive(root, "error");
    out->error_code = number_value(error, "code", 0);
    snprintf(out->reason, sizeof(out->reason), "%s", string_value(error, "reason"));
    snprintf(out->message, sizeof(out->message), "%s", string_value(error, "message"));
    out->unlock_observed = out->ok && strcmp(out->type, "offline_code") == 0 &&
        out->play_timer_enabled == 1 && out->restricted_now == 0;
    out->valid = true;
    cJSON_Delete(root);
    return true;
}

void ptc_companion_result_summary_expire_usage(PtcCompanionResultSummary *summary, int64_t now)
{
    if (summary && summary->usage_present && (summary->usage_captured_at <= 0 ||
        now < summary->usage_captured_at || now - summary->usage_captured_at > 120)) {
        summary->played_minutes_available = false;
        summary->played_minutes = -1;
    }
}

void ptc_companion_result_summary_expire_usage_elapsed(PtcCompanionResultSummary *summary,
    uint64_t request_elapsed_seconds)
{
    if (!summary || !summary->usage_present) return;
    /* Count the whole request interval, including response transit, rather than
       granting another 120 seconds when a cached observation arrives. */
    if (!summary->valid || summary->usage_captured_at <= 0 || summary->completed_at <= 0 ||
        summary->completed_at < summary->usage_captured_at || request_elapsed_seconds > 120 ||
        (uint64_t)(summary->completed_at - summary->usage_captured_at) > 120 - request_elapsed_seconds) {
        summary->played_minutes_available = false;
        summary->played_minutes = -1;
    }
}

bool ptc_companion_result_summary_format(const PtcCompanionResultSummary *summary, char *out, size_t out_size)
{
    int written;
    char remaining[32];
    const char *timer;
    const char *restriction;
    if (!summary || !out || out_size == 0 || !summary->valid) {
        return false;
    }
    if (summary->unrestricted_today == 1) {
        snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    } else if (summary->remaining_available && summary->remaining_minutes >= 0) {
        snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_D_MIN), summary->remaining_minutes);
    } else {
        snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    }
    timer = summary->play_timer_enabled == 1 ? ptc_ui_text(PTC_UI_T_STARTED) :
        (summary->play_timer_enabled == 0 ? ptc_ui_text(PTC_UI_T_NOT_STARTED_2) : ptc_ui_text(PTC_UI_T_UNCONFIRMED));
    restriction = summary->restricted_now == 1 ? ptc_ui_text(PTC_UI_T_REPORTED) :
        (summary->restricted_now == 0 ? ptc_ui_text(PTC_UI_T_NOT_REPORTED) : ptc_ui_text(PTC_UI_T_UNCONFIRMED));
    written = snprintf(out, out_size, ptc_ui_text(PTC_UI_T_S_S_REMAINING_S_ESTIMATED_USED_S),
        summary->ok ? ptc_ui_text(PTC_UI_T_SUCCESS) : ptc_ui_text(PTC_UI_T_FAILED),
        summary->ok ? "" : (summary->reason[0] ? summary->reason : ptc_ui_text(PTC_UI_T_BACKGROUND_REJECTION)),
        remaining,
        summary->played_minutes_available ? ptc_ui_text(PTC_UI_T_APPROX) : "",
        summary->played_minutes_available ? summary->played_minutes : -1,
        summary->played_minutes_available ? ptc_ui_text(PTC_UI_T_MINUTES) : ptc_ui_text(PTC_UI_T_NOT_AVAILABLE),
        timer,
        restriction);
    return written >= 0 && (size_t)written < out_size;
}
