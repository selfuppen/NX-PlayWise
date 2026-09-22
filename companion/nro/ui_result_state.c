#include "ui_state.h"

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
    if (strcmp(event, "result_ok") == 0) return "操作已完成";
    if (strcmp(event, "result_error") == 0) return "操作未完成";
    if (strcmp(event, "pctl_apply_failed") == 0) return "系统设置未生效";
    if (strcmp(event, "effect_restore") == 0) return "设置已恢复";
    if (strcmp(event, "effect_restore_failed") == 0) return "设置恢复失败";
    if (strcmp(event, "handover_preserved") == 0) return "已保留今天的额度";
    if (strcmp(event, "handover_restore") == 0) return "已恢复接管前额度";
    return event && event[0] ? event : "未知事件";
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
    return "额度管理";
}

static const char *request_success_message(const char *type)
{
    if (!type) {
        return "后台已完成本次操作。";
    }
    if (strcmp(type, "status") == 0) {
        return "今天的游玩状态已刷新。";
    }
    if (strcmp(type, "offline_code") == 0) {
        return "加时成功，今天的主机使用额度已更新。";
    }
    if (strcmp(type, "clear_redemption_history") == 0) {
        return "加时码使用记录已全部清空。";
    }
    if (strcmp(type, "claim_daily_buffer") == 0) {
        return "今日自主缓冲已领取，记得休息眼睛。";
    }
    if (strcmp(type, "set_scheduled_override") == 0) {
        return "临时额度计划已保存，未来规则预览已更新。";
    }
    if (strcmp(type, "set_autonomy_policy") == 0) {
        return "今日自主缓冲设置已保存。";
    }
    if (strcmp(type, "confirm_bedtime_requirements") == 0) {
        return "就寝限制环境已确认，正在保存完整计划。";
    }
    if (strcmp(type, "set_bedtime_policy") == 0) {
        return "就寝计划已保存；限制生效后请使用 Overlay 恢复。";
    }
    if (strcmp(type, "clear_activity_history") == 0) {
        return "家庭活动记录已清空。";
    }
    if (strcmp(type, "complete_setup") == 0) {
        return "首次设置已完成，已保留当前额度并接管控制。";
    }
    if (strcmp(type, "retry_setup_release") == 0) {
        return "接管状态已重新验证。";
    }
    if (strcmp(type, "restore_install_snapshot") == 0) {
        return "安装前家长控制状态已恢复，任我玩 已停用。";
    }
    if (strcmp(type, "disable_today_limit") == 0) {
        return "当前限制已解除，今天保持不限时。";
    }
    if (strcmp(type, "set_today_limit") == 0) {
        return "今日总额度已更新，当前状态已刷新。";
    }
    if (strcmp(type, "add_today_minutes") == 0) {
        return "临时加时已生效，当前状态已刷新。";
    }
    if (strcmp(type, "restore_today_policy") == 0) {
        return "今日额度调整已清除，已恢复下级规则。";
    }
    if (strcmp(type, "set_weekly_template") == 0) {
        return "周计划已保存。如果今天没有单独设置，今天也会按新计划执行。";
    }
    if (strcmp(type, "set_holiday_policy") == 0) {
        return "国家节假日设置已保存。";
    }
    return "设置已生效。";
}

static const char *request_success_guidance(const char *type)
{
    if (!type) {
        return "";
    }
    if (strcmp(type, "complete_setup") == 0) {
        return "接下来：进入第 5 步选择家长区或孩子区。";
    }
    if (strcmp(type, "retry_setup_release") == 0) {
        return "接下来：刷新状态；显示正常运行即已完成接管。";
    }
    if (strcmp(type, "restore_install_snapshot") == 0) {
        return "任我玩 已停用。解除停用后选择【启用自动控制】即可重新完成设置。";
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
                 "反馈码：504。请完整重启主机后再试；若仍有问题，请到 GitHub 项目 Issue 页反馈。感谢反馈。");
        return;
    }
    if (error_code == 306) {
        if (strcmp(type, "status") == 0) {
            snprintf(out, out_size,
                     "反馈码：306。今日时间已用完，但系统没有执行限制；请进入支持与恢复导出诊断信息。");
            return;
        }
        snprintf(out, out_size,
                 "反馈码：306。可能未手动开启主机家长控制；系统设置到家长控制到开启，返回后选择“重新检测”。");
        return;
    }
    if (error_code == 313) {
        snprintf(out, out_size,
                 "反馈码：313。任我玩未改写今天的系统额度；请保留当前设置，稍后重新检测或明天再接管。");
        return;
    }
    if (strcmp(type, "complete_setup") == 0) {
        snprintf(out, out_size,
                 "反馈码：%d %s。建议：保留当前系统设置并重新检测；不要手工删除停用标记。",
                 error_code, reason[0] ? reason : "unknown");
    } else if (strcmp(type, "retry_setup_release") == 0) {
        snprintf(out, out_size,
                 "反馈码：%d %s。当前状态无法安全接管；可稍后重试或恢复安装前状态。",
                 error_code, reason[0] ? reason : "unknown");
    } else if (strcmp(type, "restore_install_snapshot") == 0) {
        snprintf(out, out_size,
                 "反馈码：%d %s。恢复失败请保留日志，联系作者排查。",
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
            !summary.played_minutes_available;

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
        /* Management results may carry the compatibility fields as unavailable;
         * retain the last status snapshot instead of displaying a false reset. */
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
        model->setup_restriction_cleared = json_bool(setup, "restriction_cleared", false);
        model->setup_snapshot_available = json_bool(setup, "snapshot_available", false);
        model->setup_activate_after = json_int(setup, "activate_after", 0);
        if (type && strcmp(type, "complete_setup") == 0 &&
            strcmp(model->setup_phase, "released") == 0 && model->setup_activate_after > 0) {
            model->view = PTC_UI_SETUP;
        } else if (strcmp(model->setup_phase, "active") != 0 && model->view != PTC_UI_PARENT) {
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
                         "%s  |  %s", event_label(event_name), error_name[0] ? error_name : "成功");
                ++model->recent_event_count;
            }
        }
    }
    if (status && strcmp(status, "error") == 0) {
        const char *message = summary.message[0] ? summary.message : NULL;
        snprintf(model->message, sizeof(model->message), "%s", message ? message : "后台拒绝了本次操作。");
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
                    "反馈码：%d %s",
                    summary.error_code,
                    summary.reason[0] ? summary.reason : "unknown");
            }
        }
    } else if (setup_activated) {
        snprintf(model->message, sizeof(model->message), "自动控制已启用，首次设置完成。");
    } else {
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
                     "今日总额度已更新，当前已进入时间限制。");
            snprintf(model->feedback_detail, sizeof(model->feedback_detail),
                     "解除：选择“今日不限时”“临时加时”，或兑换加时码。");
        } else if (type && strcmp(type, "set_today_limit") == 0 &&
                   model->remaining_available && model->remaining_minutes <= 0) {
            snprintf(model->message, sizeof(model->message),
                     "今日总额度已更新，额度已用完，系统限制可能即将生效。");
            snprintf(model->feedback_detail, sizeof(model->feedback_detail),
                     "解除：选择“今日不限时”“临时加时”，或兑换加时码。");
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
