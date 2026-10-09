#include "ui_state.h"
#include "ptc_audio.h"

#include <stdio.h>
#include <string.h>

#include "../file_protocol.h"
#include "../../third_party/cjson/cJSON.h"

static int json_int(const cJSON *object, const char *name, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static int64_t json_int64(const cJSON *object, const char *name, int64_t fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? (int64_t)item->valuedouble : fallback;
}

static const char *event_label(const char *event)
{
    if (strcmp(event, "result_ok") == 0) return ptc_ui_text(PTC_UI_T_OPERATION_COMPLETED);
    if (strcmp(event, "result_error") == 0) return ptc_ui_text(PTC_UI_T_OPERATION_INCOMPLETE);
    if (strcmp(event, "pctl_apply_failed") == 0) return ptc_ui_text(PTC_UI_T_THE_SYSTEM_SETTING_DOES_NOT_TAKE_EFFECT);
    if (strcmp(event, "effect_restore") == 0) return ptc_ui_text(PTC_UI_T_SETTINGS_RESTORED);
    if (strcmp(event, "effect_restore_failed") == 0) return ptc_ui_text(PTC_UI_T_SETTING_RECOVERY_FAILED);
    if (strcmp(event, "handover_preserved") == 0) return ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_HAS_BEEN_RESERVED);
    if (strcmp(event, "handover_restore") == 0) return ptc_ui_text(PTC_UI_T_THE_PRE_ACTIVATION_QUOTA_HAS_BEEN_RESTORED);
    return event && event[0] ? event : ptc_ui_text(PTC_UI_T_UNKNOWN_EVENT);
}

void ptc_ui_refresh_recent_event_labels(PtcUiModel *model)
{
    if (!model) return;
    for (int i = 0; i < model->recent_event_count && i < 3; ++i)
        snprintf(model->recent_events[i], sizeof(model->recent_events[i]), "%s  |  %s",
                 event_label(model->recent_event_names[i]),
                 model->recent_event_errors[i][0] ? model->recent_event_errors[i] : ptc_ui_text(PTC_UI_T_SUCCESS));
}

static bool json_bool(const cJSON *object, const char *name, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static const char *localized_mode(const char *mode)
{
    (void)mode;
    return ptc_ui_text(PTC_UI_T_QUOTA_MANAGEMENT);
}

static const char *request_success_message(const char *type)
{
    if (!type) {
        return ptc_ui_text(PTC_UI_T_THE_BACKGROUND_OPERATION_HAS_BEEN_COMPLETED);
    }
    if (strcmp(type, "status") == 0) {
        return ptc_ui_text(PTC_UI_T_TODAY_S_PLAY_STATUS_HAS_BEEN_REFRESHED);
    }
    if (strcmp(type, "offline_code") == 0) {
        return ptc_ui_text(PTC_UI_T_THE_GRANT_WAS_SUCCESSFUL_AND_TODAY_S);
    }
    if (strcmp(type, "clear_redemption_history") == 0) {
        return ptc_ui_text(PTC_UI_T_ALL_GRANT_CODE_USAGE_RECORDS_HAVE_BEEN);
    }
    if (strcmp(type, "claim_daily_buffer") == 0) {
        return ptc_ui_text(PTC_UI_T_THE_INDEPENDENT_BUFFER_HAS_BEEN_RECEIVED_TODAY);
    }
    if (strcmp(type, "set_scheduled_override") == 0) {
        return ptc_ui_text(PTC_UI_T_THE_QUOTA_FOR_THE_SPECIFIED_DATE_HAS);
    }
    if (strcmp(type, "set_autonomy_policy") == 0) {
        return ptc_ui_text(PTC_UI_T_TODAY_S_AUTONOMOUS_BUFFER_SETTINGS_HAVE_BEEN);
    }
    if (strcmp(type, "confirm_bedtime_requirements") == 0) {
        return ptc_ui_text(PTC_UI_T_BEDTIME_RESTRICTED_ENVIRONMENT_CONFIRMED_SAVING_FULL_PLAN);
    }
    if (strcmp(type, "set_bedtime_policy") == 0) {
        return ptc_ui_text(PTC_UI_T_BEDTIME_SCHEDULE_SAVED_USE_OVERLAY_TO_RESTORE);
    }
    if (strcmp(type, "clear_bedtime_skip") == 0) {
        return ptc_ui_text(PTC_UI_T_THIS_BEDTIME_SKIP_HAS_BEEN_CLEARED_AND);
    }
    if (strcmp(type, "clear_activity_history") == 0) {
        return ptc_ui_text(PTC_UI_T_FAMILY_ACTIVITY_RECORDS_HAVE_BEEN_CLEARED);
    }
    if (strcmp(type, "complete_setup") == 0) {
        return ptc_ui_text(PTC_UI_T_THE_FIRST_SETUP_HAS_BEEN_COMPLETED_THE);
    }
    if (strcmp(type, "retry_setup_release") == 0) {
        return ptc_ui_text(PTC_UI_T_THE_QUOTA_MANAGEMENT_STATUS_HAS_BEEN_RECONFIRMED);
    }
    if (strcmp(type, "restore_install_snapshot") == 0) {
        return ptc_ui_text(PTC_UI_T_THE_PARENTAL_CONTROL_STATUS_BEFORE_INSTALLATION_HAS);
    }
    if (strcmp(type, "disable_today_limit") == 0) {
        return ptc_ui_text(PTC_UI_T_THE_CURRENT_RESTRICTION_HAS_BEEN_LIFTED_AND);
    }
    if (strcmp(type, "set_today_limit") == 0) {
        return ptc_ui_text(PTC_UI_T_TODAY_S_TOTAL_QUOTA_HAS_BEEN_UPDATED);
    }
    if (strcmp(type, "add_today_minutes") == 0) {
        return ptc_ui_text(PTC_UI_T_THE_TEMPORARY_GRANT_HAS_TAKEN_EFFECT_AND);
    }
    if (strcmp(type, "restore_today_policy") == 0) {
        return ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_ADJUSTMENT_HAS_BEEN_CLEARED);
    }
    if (strcmp(type, "set_weekly_template") == 0) {
        return ptc_ui_text(PTC_UI_T_WEEKLY_PLAN_SAVED_IF_THERE_IS_NO);
    }
    if (strcmp(type, "set_holiday_policy") == 0) {
        return ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAY_SETTINGS_SAVED);
    }
    return ptc_ui_text(PTC_UI_T_SETTING_HAS_TAKEN_EFFECT);
}

static const char *request_success_guidance(const char *type)
{
    if (!type) {
        return "";
    }
    if (strcmp(type, "complete_setup") == 0) {
        return ptc_ui_text(PTC_UI_T_SETUP_CHECK_NOTICE);
    }
    if (strcmp(type, "retry_setup_release") == 0) {
        return ptc_ui_text(PTC_UI_T_NEXT_REFRESH_STATUS_DISPLAYING_NORMAL_OPERATION_MEANS);
    }
    if (strcmp(type, "restore_install_snapshot") == 0) {
        return ptc_ui_text(PTC_UI_T_LET_ME_PLAY_HAS_BEEN_DEACTIVATED_AFTER);
    }
    return "";
}

static void fill_error_guidance(char *out, size_t out_size, const char *type, int error_code, const char *reason)
{
    if (!type || !out || out_size == 0) {
        return;
    }
    if (error_code == 504) {
        snprintf(out, out_size,
                 ptc_ui_text(PTC_UI_T_FEEDBACK_CODE_504_PLEASE_RESTART_THE_HOST));
        return;
    }
    if (error_code == 306) {
        if (strcmp(type, "status") == 0) {
            snprintf(out, out_size,
                     ptc_ui_text(PTC_UI_T_FEEDBACK_CODE_306));
            return;
        }
        snprintf(out, out_size,
                 ptc_ui_text(PTC_UI_T_RESULT_CODE_306_NINTENDO_PARENTAL_CONTROLS_MAY));
        return;
    }
    if (error_code == 313) {
        snprintf(out, out_size,
                 ptc_ui_text(PTC_UI_T_FEEDBACK_CODE_313));
        return;
    }
    if (strcmp(type, "complete_setup") == 0) {
        snprintf(out, out_size,
                 ptc_ui_text(PTC_UI_T_RESULT_CODE_D_S_KEEP_CURRENT_SYSTEM),
                 error_code, reason[0] ? reason : "unknown");
    } else if (strcmp(type, "retry_setup_release") == 0) {
        snprintf(out, out_size,
                 ptc_ui_text(PTC_UI_T_RESULT_CODE_D_S_QUOTA_CONTROL_CANNOT),
                 error_code, reason[0] ? reason : "unknown");
    } else if (strcmp(type, "restore_install_snapshot") == 0) {
        snprintf(out, out_size,
                 ptc_ui_text(PTC_UI_T_RESULT_CODE_D_S_KEEP_LOGS_AND),
                 error_code, reason[0] ? reason : "unknown");
    }
    /* Other types: leave existing feedback_detail as-is (filled by caller). */
}

bool ptc_ui_apply_redemption_history_text(PtcUiModel *model, const char *text)
{
    const char *cursor;
    if (!model || !text || strlen(text) >= PTC_REDEMPTION_HISTORY_FILE_SIZE) return false;
    model->redemption_history_available = false;
    model->redemption_history_count = 0;
    model->redemption_history_page = 0;
    cursor = text;
    while (*cursor) {
        const char *newline = strchr(cursor, '\n');
        size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
        char line[PTC_REDEMPTION_HISTORY_LINE_SIZE];
        PtcRedemptionHistoryRecord parsed;
        if (length == 0) {
            cursor = newline ? newline + 1 : cursor + length;
            continue;
        }
        if (length >= PTC_REDEMPTION_HISTORY_LINE_SIZE) return false;
        memcpy(line, cursor, length);
        line[length] = '\0';
        if (!ptc_redemption_history_parse_line(line, &parsed)) return false;
        if (model->redemption_history_count == (int)PTC_REDEMPTION_HISTORY_MAX_RECORDS) {
            memmove(model->redemption_history, model->redemption_history + 1,
                (PTC_REDEMPTION_HISTORY_MAX_RECORDS - 1u) * sizeof(model->redemption_history[0]));
            model->redemption_history_count = (int)PTC_REDEMPTION_HISTORY_MAX_RECORDS - 1;
        }
        model->redemption_history[model->redemption_history_count++] = parsed;
        if (!newline) break;
        cursor = newline + 1;
    }
    model->redemption_history_available = true;
    return true;
}

bool ptc_ui_apply_activity_history_text(PtcUiModel *model, const char *text)
{
    const char *cursor;
    if (!model || !text || strlen(text) >= PTC_ACTIVITY_HISTORY_FILE_SIZE) return false;
    model->activity_history_available = false;
    model->activity_history_count = 0;
    model->activity_history_page = 0;
    cursor = text;
    while (*cursor) {
        const char *newline = strchr(cursor, '\n');
        size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
        char line[PTC_ACTIVITY_HISTORY_LINE_SIZE];
        PtcActivityHistoryRecord parsed;
        if (length == 0) { cursor = newline ? newline + 1 : cursor + length; continue; }
        if (length >= sizeof(line)) return false;
        memcpy(line, cursor, length);
        line[length] = '\0';
        if (!ptc_activity_history_parse_line(line, &parsed)) return false;
        if (model->activity_history_count == (int)PTC_ACTIVITY_HISTORY_MAX_RECORDS) {
            memmove(model->activity_history, model->activity_history + 1,
                (PTC_ACTIVITY_HISTORY_MAX_RECORDS - 1u) * sizeof(model->activity_history[0]));
            model->activity_history_count = (int)PTC_ACTIVITY_HISTORY_MAX_RECORDS - 1;
        }
        model->activity_history[model->activity_history_count++] = parsed;
        if (!newline) break;
        cursor = newline + 1;
    }
    model->activity_history_available = true;
    return true;
}

int ptc_ui_redemption_history_page_count(const PtcUiModel *model)
{
    if (!model || model->redemption_history_count <= 0) return 1;
    return (model->redemption_history_count + 5) / 6;
}

void ptc_ui_change_redemption_history_page(PtcUiModel *model, int direction)
{
    int pages;
    if (!model || direction == 0) return;
    pages = ptc_ui_redemption_history_page_count(model);
    if (direction < 0 && model->redemption_history_page > 0) --model->redemption_history_page;
    else if (direction > 0 && model->redemption_history_page + 1 < pages) ++model->redemption_history_page;
}

int ptc_ui_activity_history_page_count(const PtcUiModel *model)
{
    if (!model || model->activity_history_count <= 0) return 1;
    return (model->activity_history_count + 7) / 8;
}

void ptc_ui_change_activity_history_page(PtcUiModel *model, int direction)
{
    int pages;
    if (!model || direction == 0) return;
    pages = ptc_ui_activity_history_page_count(model);
    if (direction < 0 && model->activity_history_page > 0) --model->activity_history_page;
    else if (direction > 0 && model->activity_history_page + 1 < pages) ++model->activity_history_page;
}

void ptc_ui_match_redemption_result(PtcUiModel *model)
{
    int matches = 0;
    if (!model) return;
    model->code_actual_add_available = false;
    /* History has no request ID. Require a unique completion timestamp and
       matching result values; never substitute the persisted preview. */
    if (model->code_result_pending || model->code_result_failed ||
        model->code_completed_at <= 0 || !model->redemption_history_available) return;
    for (int i = 0; i < model->redemption_history_count; ++i) {
        const PtcRedemptionHistoryRecord *record = &model->redemption_history[i];
        if (record->redeemed_at != model->code_completed_at) continue;
        ++matches;
        if (record->day_index == model->day_index && record->grant_minutes == model->code_grant_minutes &&
            record->remaining_after_available == model->remaining_available &&
            (!model->remaining_available || record->remaining_after_minutes == model->remaining_minutes)) {
            model->code_actual_add_available = true;
            model->code_actual_add_minutes = record->effective_add_minutes;
        }
    }
    if (matches != 1) model->code_actual_add_available = false;
}

bool ptc_ui_apply_result_json(PtcUiModel *model, const char *text)
{
    PtcCompanionResultSummary summary;
    cJSON *root;
    const cJSON *state;
    const cJSON *setup;
    const char *status;
    const char *type;
    bool status_context;
    bool setup_activated = false;
    if (!model || !text || ptc_companion_parse_result_summary(text, &summary) != PTC_COMPANION_OK) {
        return false;
    }
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    status = summary.status;
    type = summary.type;
    if (strcmp(type, "offline_code") == 0) {
        model->code_completed_at = json_int64(root, "completed_at", 0);
        model->code_actual_add_available = false;
    }
    if (!status || (strcmp(status, "ok") != 0 && strcmp(status, "error") != 0)) {
        cJSON_Delete(root);
        return false;
    }
    snprintf(model->result_status, sizeof(model->result_status), "%s", status ? status : "error");
    snprintf(model->result_type, sizeof(model->result_type), "%s", type ? type : "");
    snprintf(model->mode, sizeof(model->mode), "%s", localized_mode("release"));
    model->feedback_detail[0] = '\0';
    model->error_code = 0;
    status_context = strcmp(status, "ok") == 0 ||
        (strcmp(status, "error") == 0 && summary.error_code == 306 &&
            type && strcmp(type, "status") == 0);

    state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (status_context && cJSON_IsObject(state)) {
        bool preserve_played_minutes = model->played_minutes_available &&
            !summary.played_minutes_available && type && strcmp(type, "status") != 0 &&
            model->day_index == (uint16_t)json_int(state, "day_index", 0);

        model->status_loaded = true;
        model->restriction_enabled_available = json_bool(state, "restriction_enabled_available", false);
        model->restriction_enabled = json_bool(state, "restriction_enabled", false);
        model->temporary_unlocked_available = json_bool(state, "temporary_unlocked_available", false);
        model->temporary_unlocked = json_bool(state, "temporary_unlocked", false);
        model->day_index = (uint16_t)json_int(state, "day_index", 0);
        model->limited_today = json_int(state, "limited_today", -1);
        model->blocked_today = json_int(state, "blocked_today", -1);
        model->unrestricted_today = json_int(state, "unrestricted_today", -1);
        model->remaining_available = summary.remaining_available;
        model->remaining_minutes = summary.remaining_minutes;
        /* Same-day management replies may omit compatibility readings. A status
         * read must replace them, including unknown values, so a refreshed or
         * next-day estimate cannot inherit consumption from an older snapshot. */
        if (!preserve_played_minutes) {
            model->played_minutes_available = summary.played_minutes_available;
            model->played_minutes = summary.played_minutes;
        }
        model->play_timer_enabled = summary.play_timer_enabled;
        model->restricted_now = summary.restricted_now;
        model->bedtime_active = summary.bedtime_active;
        model->bedtime_skipped = summary.bedtime_skipped;
        model->bedtime_window_instance_id = summary.bedtime_window_instance_id;
        model->bedtime_start_day_index = (uint16_t)summary.bedtime_start_day_index;
        model->bedtime_start_minute = (uint16_t)summary.bedtime_start_minute;
        model->bedtime_end_minute = (uint16_t)summary.bedtime_end_minute;
        snprintf(model->bedtime_source, sizeof(model->bedtime_source), "%s", summary.bedtime_source);
        model->bedtime_next_available = summary.bedtime_next_available;
        model->bedtime_next_start_day_index = (uint16_t)summary.bedtime_next_start_day_index;
        model->bedtime_next_start_minute = (uint16_t)summary.bedtime_next_start_minute;
        model->bedtime_next_end_minute = (uint16_t)summary.bedtime_next_end_minute;
        model->bedtime_next_window_instance_id = summary.bedtime_next_window_instance_id;
        model->bedtime_skipped_window_available = summary.bedtime_skipped_window_available;
        model->bedtime_skipped_window_instance_id = summary.bedtime_skipped_window_instance_id;
        model->bedtime_skipped_start_day_index = (uint16_t)summary.bedtime_skipped_start_day_index;
        model->bedtime_skipped_start_minute = (uint16_t)summary.bedtime_skipped_start_minute;
        model->bedtime_skipped_end_minute = (uint16_t)summary.bedtime_skipped_end_minute;
        snprintf(model->bedtime_skipped_source, sizeof(model->bedtime_skipped_source), "%s",
            summary.bedtime_skipped_source);
        model->bedtime_official_setting_confirmed = summary.bedtime_official_setting_confirmed;
        model->bedtime_overlay_verified = summary.bedtime_overlay_verified;
        model->dock_available = summary.dock_available;
        model->dock_policy.force_docked = summary.force_docked;
        model->dock_policy.undocked_limit_enabled = summary.undocked_limit_enabled;
        model->dock_policy.undocked_daily_minutes = (uint16_t)summary.undocked_daily_minutes;
        model->dock_supported_available = summary.dock_supported_available;
        model->dock_supported = summary.dock_supported;
        snprintf(model->operation_mode, sizeof(model->operation_mode), "%s", summary.operation_mode);
        model->undocked_usage_available = summary.undocked_usage_available;
        model->undocked_used_minutes = summary.undocked_used_minutes;
        model->undocked_remaining_minutes = summary.undocked_remaining_minutes;
        model->dock_waived_today = summary.dock_waived_today;
        model->dock_restriction_active = summary.dock_restriction_active;
        model->dock_unlimited_capped = summary.dock_unlimited_capped;
        if (strcmp(summary.type, "set_dock_policy") == 0 && summary.ok) model->dock_dirty = false;
        if (!model->dock_dirty) model->draft_dock_policy = model->dock_policy;
        model->eye_care_policy.enabled = summary.eye_care_enabled;
        model->eye_care_policy.play_minutes = (uint16_t)summary.eye_care_play_minutes;
        model->eye_care_policy.rest_minutes = (uint16_t)summary.eye_care_rest_minutes;
        model->eye_care_used_minutes = (uint16_t)summary.eye_care_used_minutes;
        model->eye_care_rest_remaining_seconds = summary.eye_care_rest_remaining_seconds;
        model->eye_care_break_id = summary.eye_care_break_id;
        model->eye_care_unlimited_capped = summary.eye_care_unlimited_capped;
        model->daily_restriction_active = summary.daily_restriction_active;
        snprintf(model->eye_care_phase, sizeof(model->eye_care_phase), "%s", summary.eye_care_phase);
        model->calendar_covered = summary.calendar_covered;
        model->calendar_update_warning = summary.calendar_update_warning;
        snprintf(model->rule_source, sizeof(model->rule_source), "%s", summary.rule_source);
        {
            const cJSON *forecast = cJSON_GetObjectItemCaseSensitive(state, "forecast");
            model->forecast_available = cJSON_IsArray(forecast) &&
                cJSON_GetArraySize(forecast) == (int)PTC_RESULT_FORECAST_DAYS;
            if (model->forecast_available) {
                int forecast_index;
                for (forecast_index = 0; forecast_index < (int)PTC_RESULT_FORECAST_DAYS; ++forecast_index) {
                    const cJSON *item = cJSON_GetArrayItem(forecast, forecast_index);
                    model->forecast[forecast_index].day_index = (uint16_t)json_int(item, "day_index", 0);
                    model->forecast[forecast_index].mode = json_int(item, "mode", 1);
                    model->forecast[forecast_index].minutes = (uint16_t)json_int(item, "minutes", 0);
                    snprintf(model->forecast_rule_sources[forecast_index],
                        sizeof(model->forecast_rule_sources[forecast_index]), "%s",
                        json_string(item, "rule_source"));
                    model->forecast[forecast_index].rule_source = model->forecast_rule_sources[forecast_index];
                    model->forecast[forecast_index].calendar_covered =
                        json_bool(item, "calendar_covered", false);
                }
            }
        }
        {
            const cJSON *autonomy = cJSON_GetObjectItemCaseSensitive(state, "autonomy");
            model->daily_buffer_minutes = cJSON_IsObject(autonomy)
                ? (uint16_t)json_int(autonomy, "daily_buffer_minutes", 0) : 0;
            model->daily_buffer_claimed = cJSON_IsObject(autonomy) &&
                json_bool(autonomy, "claimed_today", false);
            model->daily_buffer_available = cJSON_IsObject(autonomy) &&
                json_bool(autonomy, "available", false);
            snprintf(model->daily_buffer_reason, sizeof(model->daily_buffer_reason), "%s",
                cJSON_IsObject(autonomy) ? json_string(autonomy, "reason") : "unavailable");
        }
        {
            const cJSON *usage = cJSON_GetObjectItemCaseSensitive(state, "usage_summary");
            model->usage_summary_available = cJSON_IsObject(usage) &&
                json_bool(usage, "available", false);
            model->usage_known_days_7 = cJSON_IsObject(usage)
                ? (uint16_t)json_int(usage, "known_days_7", 0) : 0;
            model->usage_consumed_minutes_7 = cJSON_IsObject(usage)
                ? (uint32_t)json_int64(usage, "consumed_minutes_7", 0) : 0;
            model->usage_known_days_30 = cJSON_IsObject(usage)
                ? (uint16_t)json_int(usage, "known_days_30", 0) : 0;
            model->usage_consumed_minutes_30 = cJSON_IsObject(usage)
                ? (uint32_t)json_int64(usage, "consumed_minutes_30", 0) : 0;
        }
    }
    if (strcmp(status, "ok") == 0 && type && strcmp(type, "preview_offline_code") == 0 &&
        summary.preview_available) {
        model->code_grant_minutes = summary.grant_minutes;
        model->code_preview_after_available = summary.remaining_after_available;
        model->code_preview_after_minutes = summary.remaining_after_minutes;
        model->code_effective_add_minutes = summary.effective_add_minutes;
        model->code_preview_capped = summary.preview_capped;
        model->code_preview_converts_unlimited = summary.converts_unlimited_to_limited;
    }
    setup = cJSON_GetObjectItemCaseSensitive(root, "setup");
    if (status_context && type && strcmp(type, "complete_setup") == 0 && !cJSON_IsObject(setup)) {
        /* A success envelope without setup evidence cannot confirm activation. */
        model->setup_phase[0] = '\0';
    }
    if (status_context && cJSON_IsObject(setup)) {
        bool setup_was_waiting = strcmp(model->setup_phase, "released") == 0 &&
            model->setup_activate_after > 0;
        snprintf(model->setup_phase, sizeof(model->setup_phase), "%s", json_string(setup, "phase"));
        snprintf(model->compatibility_status, sizeof(model->compatibility_status), "%s",
                 json_string(setup, "compatibility_status"));
        snprintf(model->apply_status, sizeof(model->apply_status), "%s", json_string(setup, "apply_status"));
        model->apply_pending_confirmation = json_bool(setup, "apply_pending_confirmation", false);
        model->recovery_active = json_bool(setup, "recovery_active", false);
        snprintf(model->disable_reason, sizeof(model->disable_reason), "%s", json_string(setup, "disable_reason"));
        if (cJSON_HasObjectItem(setup, "disable_flag_present"))
            model->disable_flag_present = json_bool(setup, "disable_flag_present", false);
        model->setup_restriction_cleared = json_bool(setup, "restriction_cleared", false);
        model->setup_snapshot_available = json_bool(setup, "snapshot_available", false);
        model->setup_activate_after = json_int(setup, "activate_after", 0);
        if (!model->setup_wizard_completed && type && strcmp(type, "complete_setup") == 0 &&
            strcmp(model->setup_phase, "released") == 0 && model->setup_activate_after > 0) {
            model->view = PTC_UI_SETUP;
        } else if (!model->setup_wizard_completed && strcmp(model->setup_phase, "active") != 0 && model->view != PTC_UI_PARENT) {
            model->view = PTC_UI_SETUP;
        } else if (strcmp(model->setup_phase, "active") == 0 && model->view == PTC_UI_SETUP &&
                   model->setup_step == 0) {
            model->view = PTC_UI_CHILD;
        }
        setup_activated = setup_was_waiting && strcmp(model->setup_phase, "active") == 0;
    }
    {
        cJSON *environment = cJSON_GetObjectItemCaseSensitive(root, "environment");
        if (status_context && cJSON_IsObject(environment)) {
            model->environment_available = json_bool(environment, "available", false);
            snprintf(model->environment_hos, sizeof(model->environment_hos), "%s", json_string(environment, "hos"));
            snprintf(model->environment_model, sizeof(model->environment_model), "%s", json_string(environment, "model"));
            model->environment_atmosphere = json_bool(environment, "atmosphere", false);
            snprintf(model->environment_atmosphere_version, sizeof(model->environment_atmosphere_version), "%s",
                     json_string(environment, "atmosphere_version"));
        }
    }
    {
        cJSON *events = cJSON_GetObjectItemCaseSensitive(root, "recent_events");
        model->recent_events_available = status_context && cJSON_IsArray(events);
        if (model->recent_events_available) {
            int total = cJSON_GetArraySize(events);
            int start = total > 3 ? total - 3 : 0;
            model->recent_event_count = 0;
            for (int event_index = start; event_index < total; ++event_index) {
                cJSON *item = cJSON_GetArrayItem(events, event_index);
                const char *event_name = cJSON_IsObject(item) ? json_string(item, "event") : "";
                const char *error_name = cJSON_IsObject(item) ? json_string(item, "error") : "";
                const char *event_type = cJSON_IsObject(item) ? json_string(item, "type") : "";
                const char *event_detail = cJSON_IsObject(item) ? json_string(item, "detail") : "";
                const char *request_id = cJSON_IsObject(item) ? json_string(item, "request_id") : "";
                int64_t timestamp = cJSON_IsObject(item) ? json_int64(item, "ts", 0) : 0;
                if (!event_name[0]) continue;
                int target = model->recent_event_count;
                snprintf(model->recent_event_names[target], sizeof(model->recent_event_names[target]), "%s", event_name);
                snprintf(model->recent_event_types[target], sizeof(model->recent_event_types[target]), "%s", event_type);
                snprintf(model->recent_event_errors[target], sizeof(model->recent_event_errors[target]), "%s", error_name);
                snprintf(model->recent_event_details[target], sizeof(model->recent_event_details[target]), "%s", event_detail);
                snprintf(model->recent_event_request_ids[target], sizeof(model->recent_event_request_ids[target]), "%s", request_id);
                model->recent_event_timestamps[target] = timestamp;
                snprintf(model->recent_events[model->recent_event_count],
                         sizeof(model->recent_events[model->recent_event_count]),
                         "%s  |  %s", event_label(event_name), error_name[0] ? error_name : ptc_ui_text(PTC_UI_T_SUCCESS));
                ++model->recent_event_count;
            }
        }
    }
    if (status && strcmp(status, "error") == 0) {
        ptc_audio_play(PTC_SE_ERROR);
        const char *message = summary.error_code == 325 ? ptc_ui_text(PTC_UI_T_CONTROL_BUSY) :
            (summary.message[0] ? summary.message : NULL);
        snprintf(model->message, sizeof(model->message), "%s", message ? message : ptc_ui_text(PTC_UI_T_THE_BACKGROUND_REJECTED_THIS_OPERATION));
        if (summary.error_code > 0) {
            model->error_code = summary.error_code;
            /* Try type-specific guidance first; fall back to generic detail. */
            model->feedback_detail[0] = '\0';
            fill_error_guidance(model->feedback_detail, sizeof(model->feedback_detail),
                                type, summary.error_code, summary.reason);
            if (!model->feedback_detail[0]) {
                snprintf(
                    model->feedback_detail,
                    sizeof(model->feedback_detail),
                    ptc_ui_text(PTC_UI_T_RESULT_CODE_D_S),
                    summary.error_code,
                    summary.reason[0] ? summary.reason : "unknown");
            }
        }
    } else if (setup_activated) {
        ptc_audio_play(PTC_SE_SUCCESS);
        snprintf(model->message, sizeof(model->message), ptc_ui_text(PTC_UI_T_AUTOMATIC_CONTROL_IS_ENABLED_AND_FIRST_TIME));
    } else {
        if (type && strcmp(type, "claim_daily_buffer") == 0) {
            ptc_audio_play(PTC_SE_CLAIM_BUFFER);
        } else if (type && strcmp(type, "status") != 0) {
            ptc_audio_play(PTC_SE_SUCCESS);
        }
        const char *guidance = request_success_guidance(type);
        if (type && strcmp(type, "set_weekly_template") == 0) {
            ptc_ui_format_weekly_save_result(model, model->message, sizeof(model->message),
                                             model->feedback_detail, sizeof(model->feedback_detail));
        } else if (type && strcmp(type, "set_holiday_policy") == 0) {
            ptc_ui_format_holiday_save_result(model, model->message, sizeof(model->message),
                                              model->feedback_detail, sizeof(model->feedback_detail));
        } else if (type && strcmp(type, "set_today_limit") == 0 &&
                   (model->restricted_now == 1 || model->blocked_today == 1)) {
            snprintf(model->message, sizeof(model->message),
                     ptc_ui_text(PTC_UI_T_TODAY_S_TOTAL_QUOTA_HAS_BEEN_UPDATED_2));
            snprintf(model->feedback_detail, sizeof(model->feedback_detail),
                     ptc_ui_text(PTC_UI_T_TO_LIFT_CHOOSE_NO_LIMIT_TODAY_QUICK));
        } else if (type && strcmp(type, "set_today_limit") == 0 &&
                   model->remaining_available && model->remaining_minutes <= 0) {
            snprintf(model->message, sizeof(model->message),
                     ptc_ui_text(PTC_UI_T_TODAY_S_TOTAL_QUOTA_HAS_BEEN_UPDATED_3));
            snprintf(model->feedback_detail, sizeof(model->feedback_detail),
                     ptc_ui_text(PTC_UI_T_TO_LIFT_CHOOSE_NO_LIMIT_TODAY_QUICK));
        } else if (type && strcmp(type, "complete_setup") == 0 &&
                   strcmp(model->setup_phase, "active") != 0) {
            snprintf(model->message, sizeof(model->message), "%s", ptc_ui_text(PTC_UI_T_SETUP_CONTROLS_INACTIVE));
        } else {
            snprintf(model->message, sizeof(model->message), "%s", request_success_message(type));
        }
        if (guidance[0] && !model->feedback_detail[0]) {
            snprintf(model->feedback_detail, sizeof(model->feedback_detail), "%s", guidance);
        }
    }
    cJSON_Delete(root);
    return true;
}
