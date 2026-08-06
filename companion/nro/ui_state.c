#include "ui_graphics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../file_protocol.h"
#include "../../third_party/cjson/cJSON.h"

static int json_int(const cJSON *object, const char *name, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
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

static const char *effect_failure_hint(const cJSON *probe)
{
    const cJSON *checks;
    const char *stage = json_string(probe, "failure_stage");
    if (strcmp(stage, "runtime_status") != 0) {
        return "";
    }
    checks = probe ? cJSON_GetObjectItemCaseSensitive(probe, "checks") : NULL;
    if (json_bool(checks, "raw_target_correct", false) &&
        !json_bool(checks, "timer_enabled", false)) {
        return "；检查官方临时解锁/计时器设置";
    }
    return "；检查官方家长控制状态";
}

static const char *localized_mode(const char *mode)
{
    if (!mode) {
        return "--";
    }
    if (strcmp(mode, "observe") == 0) {
        return "观察";
    }
    if (strcmp(mode, "grant") == 0) {
        return "授权";
    }
    if (strcmp(mode, "enforce") == 0) {
        return "强制执行";
    }
    if (strcmp(mode, "disabled") == 0) {
        return "已停用";
    }
    return "未知模式";
}

static const char *request_success_message(const char *type, const char *mode, bool dry_run)
{
    if (!type) {
        return "后台已完成本次操作。";
    }
    if (strcmp(type, "status") == 0) {
        return "今天的游玩状态已刷新。";
    }
    if (strcmp(type, "offline_code") == 0) {
        return dry_run ? "加时码验证通过；当前为观察模式，未修改系统设置。" : "加时成功，今天的游玩时间已更新。";
    }
    if (strcmp(type, "complete_setup") == 0) {
        return "首次设置已完成，自动控制将在宽限结束后启用。";
    }
    if (strcmp(type, "retry_setup_release") == 0) {
        return "当前限制已解除，可以继续完成首次设置。";
    }
    if (strcmp(type, "restore_install_snapshot") == 0) {
        return "安装前家长控制状态已恢复，PlayWise 已停用。";
    }
    if (strcmp(type, "disable_today_limit") == 0) {
        return dry_run ? "解除操作验证通过；观察模式未修改系统设置。" : "当前限制已解除，今天保持不限时。";
    }
    if (strcmp(type, "set_bedtime") == 0 && mode && strcmp(mode, "grant") == 0) {
        return "就寝规则已保存；Grant 模式不会自动执行限制。";
    }
    return dry_run ? "设置验证通过；观察模式不会修改系统设置。" : "设置已生效。";
}

static const char *request_success_guidance(const char *type)
{
    if (!type) {
        return "";
    }
    if (strcmp(type, "complete_setup") == 0) {
        return "接下来：5 秒后自动启用规则控制，无需操作。";
    }
    if (strcmp(type, "retry_setup_release") == 0) {
        return "接下来：确认限制已解除后，在安全工具选择【启用自动控制】。";
    }
    if (strcmp(type, "restore_install_snapshot") == 0) {
        return "PlayWise 已停用。若要重新启用，清理 setup.json 并重启。";
    }
    if (strcmp(type, "probe_raw_block") == 0) {
        return "强制阻止已验证。可在时间计划 → 限制方式中选择【强制阻止】。";
    }
    if (strcmp(type, "probe_suspend") == 0) {
        return "暂停软件已验证。可在时间计划 → 限制方式中选择【暂停软件】。";
    }
    return "";
}

static void fill_error_guidance(char *out, size_t out_size, const char *type, int error_code, const char *reason)
{
    if (!type || !out || out_size == 0) {
        return;
    }
    if (strcmp(type, "complete_setup") == 0) {
        snprintf(out, out_size,
                 "反馈码：%d %s。建议：确认限制已解除（phase=released），或尝试【重试前置解限】。",
                 error_code, reason[0] ? reason : "unknown");
    } else if (strcmp(type, "retry_setup_release") == 0) {
        snprintf(out, out_size,
                 "反馈码：%d %s。多次失败建议执行【恢复安装前状态】并重新安装。",
                 error_code, reason[0] ? reason : "unknown");
    } else if (strcmp(type, "restore_install_snapshot") == 0) {
        snprintf(out, out_size,
                 "反馈码：%d %s。恢复失败请保留日志，联系作者排查。",
                 error_code, reason[0] ? reason : "unknown");
    } else if (strcmp(type, "probe_raw_block") == 0 || strcmp(type, "probe_suspend") == 0) {
        snprintf(out, out_size,
                 "反馈码：%d %s。能力验证失败不影响普通控制。若持续失败，反馈此码给作者。",
                 error_code, reason[0] ? reason : "unknown");
    }
    /* Other types: leave existing feedback_detail as-is (filled by caller). */
}

int ptc_ui_parent_action_count(PtcUiParentPage page)
{
    switch (page) {
    case PTC_UI_PARENT_PLAN:
        return 5;
    case PTC_UI_PARENT_SAFETY:
        return 7;
    case PTC_UI_PARENT_TODAY:
    default:
        return 6;
    }
}

void ptc_ui_change_parent_page(PtcUiModel *model, int direction)
{
    int page;
    if (!model || direction == 0) {
        return;
    }
    page = (int)model->parent_page + (direction > 0 ? 1 : -1);
    if (page < 0) {
        page = PTC_UI_PARENT_PAGE_COUNT - 1;
    } else if (page >= PTC_UI_PARENT_PAGE_COUNT) {
        page = 0;
    }
    model->parent_page = (PtcUiParentPage)page;
    model->selected_index = 0;
}

void ptc_ui_move_parent_selection(PtcUiModel *model, int horizontal, int vertical)
{
    int count;
    int index;
    int row;
    int row_count;
    int column;
    int target;
    if (!model) {
        return;
    }
    count = ptc_ui_parent_action_count(model->parent_page);
    if (count <= 0) {
        model->selected_index = 0;
        return;
    }
    index = model->selected_index;
    if (index < 0 || index >= count) {
        index = 0;
    }
    column = index % 2;
    if (horizontal < 0 && column > 0) {
        --index;
    } else if (horizontal > 0 && column == 0 && index + 1 < count) {
        ++index;
    }
    if (vertical != 0) {
        row = index / 2;
        column = index % 2;
        row_count = (count + 1) / 2;
        row = (row + (vertical > 0 ? 1 : row_count - 1)) % row_count;
        target = row * 2 + column;
        if (target >= count) {
            target = row * 2;
        }
        index = target;
    }
    model->selected_index = index;
}

uint16_t ptc_ui_adjust_minutes(uint16_t value, int delta, uint16_t minimum, uint16_t maximum)
{
    int adjusted = (int)value + delta;
    if (adjusted < (int)minimum) {
        adjusted = minimum;
    }
    if (adjusted > (int)maximum) {
        adjusted = maximum;
    }
    if (adjusted < 0) {
        adjusted += 1440;
    }
    return (uint16_t)adjusted;
}

uint16_t ptc_ui_adjust_minute_of_day(uint16_t value, int delta)
{
    int adjusted = (int)value + delta;
    adjusted = adjusted % 1440;
    if (adjusted < 0) {
        adjusted += 1440;
    }
    return (uint16_t)adjusted;
}

bool ptc_ui_parse_minutes(const char *text, uint16_t minimum, uint16_t maximum, uint16_t *out)
{
    unsigned long value;
    const char *p;
    if (!text || !out || !text[0]) {
        return false;
    }
    for (p = text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    value = strtoul(text, NULL, 10);
    if (value < minimum || value > maximum) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

bool ptc_ui_parse_bedtime_time(const char *text, uint16_t *out_min)
{
    unsigned int h = 0;
    unsigned int m = 0;
    size_t len;
    if (!text || !out_min) {
        return false;
    }
    len = strlen(text);
    /* Accept "HH:MM" or "HHMM" formats. */
    if (len == 5 && text[2] == ':') {
        if (sscanf(text, "%u:%u", &h, &m) != 2) {
            return false;
        }
    } else if (len == 4) {
        if (sscanf(text, "%2u%2u", &h, &m) != 2) {
            return false;
        }
    } else {
        return false;
    }
    if (h >= 24 || m >= 60) {
        return false;
    }
    *out_min = (uint16_t)(h * 60 + m);
    return true;
}

void ptc_ui_numpad_open(
    PtcUiModel *model,
    PtcUiNumpadPurpose purpose,
    PtcUiOverlay return_overlay,
    const char *title,
    const char *guide,
    uint8_t max_digits,
    uint16_t minimum,
    uint16_t maximum,
    uint16_t current)
{
    if (!model || purpose == PTC_UI_NUMPAD_NONE || max_digits == 0 || max_digits > 8) {
        return;
    }
    model->overlay = PTC_UI_OVERLAY_NUMPAD;
    model->numpad_purpose = purpose;
    model->numpad_return_overlay = return_overlay;
    model->numpad_text[0] = '\0';
    model->numpad_cursor = 0;
    model->numpad_max_digits = max_digits;
    model->numpad_minimum = minimum;
    model->numpad_maximum = maximum;
    model->numpad_current = current;
    model->numpad_error[0] = '\0';
    snprintf(model->numpad_title, sizeof(model->numpad_title), "%s", title ? title : "数字输入");
    snprintf(model->numpad_guide, sizeof(model->numpad_guide), "%s", guide ? guide : "使用方向键或摇杆选择数字");
}

void ptc_ui_numpad_move(PtcUiModel *model, int horizontal, int vertical)
{
    int row;
    int column;
    if (!model || model->overlay != PTC_UI_OVERLAY_NUMPAD) {
        return;
    }
    row = model->numpad_cursor / 3;
    column = model->numpad_cursor % 3;
    if (horizontal != 0) {
        column = (column + (horizontal > 0 ? 1 : 2)) % 3;
    }
    if (vertical != 0) {
        row = (row + (vertical > 0 ? 1 : 3)) % 4;
    }
    model->numpad_cursor = row * 3 + column;
}

void ptc_ui_numpad_backspace(PtcUiModel *model)
{
    size_t length;
    if (!model || model->overlay != PTC_UI_OVERLAY_NUMPAD) {
        return;
    }
    length = strlen(model->numpad_text);
    if (length > 0) {
        model->numpad_text[length - 1] = '\0';
    }
    model->numpad_error[0] = '\0';
}

void ptc_ui_numpad_clear(PtcUiModel *model)
{
    if (!model || model->overlay != PTC_UI_OVERLAY_NUMPAD) {
        return;
    }
    model->numpad_text[0] = '\0';
    model->numpad_error[0] = '\0';
}

void ptc_ui_numpad_activate(PtcUiModel *model)
{
    size_t length;
    int digit;
    if (!model || model->overlay != PTC_UI_OVERLAY_NUMPAD) {
        return;
    }
    if (model->numpad_cursor == 9) {
        ptc_ui_numpad_backspace(model);
        return;
    }
    if (model->numpad_cursor == 11) {
        ptc_ui_numpad_clear(model);
        return;
    }
    length = strlen(model->numpad_text);
    if (length >= model->numpad_max_digits || length + 1 >= sizeof(model->numpad_text)) {
        snprintf(model->numpad_error, sizeof(model->numpad_error), "最多输入 %u 位数字", (unsigned int)model->numpad_max_digits);
        return;
    }
    digit = model->numpad_cursor == 10 ? 0 : model->numpad_cursor + 1;
    model->numpad_text[length] = (char)('0' + digit);
    model->numpad_text[length + 1] = '\0';
    model->numpad_error[0] = '\0';
}

bool ptc_ui_numpad_validate(PtcUiModel *model, uint16_t *out_value)
{
    uint16_t value = 0;
    size_t length;
    if (!model || model->overlay != PTC_UI_OVERLAY_NUMPAD) {
        return false;
    }
    length = strlen(model->numpad_text);
    if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        if (length != 8) {
            snprintf(model->numpad_error, sizeof(model->numpad_error), "加时码必须为 8 位数字");
            return false;
        }
        return true;
    }
    if (model->numpad_purpose == PTC_UI_NUMPAD_BEDTIME) {
        if (length != 4 || !ptc_ui_parse_bedtime_time(model->numpad_text, &value)) {
            snprintf(model->numpad_error, sizeof(model->numpad_error), "请输入有效的 4 位时间 HHMM");
            return false;
        }
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES) {
        if (!ptc_ui_parse_minutes(model->numpad_text, model->numpad_minimum, model->numpad_maximum, &value)) {
            snprintf(model->numpad_error, sizeof(model->numpad_error), "请输入 %u 到 %u 分钟",
                     (unsigned int)model->numpad_minimum, (unsigned int)model->numpad_maximum);
            return false;
        }
    } else {
        return false;
    }
    if (out_value) {
        *out_value = value;
    }
    model->numpad_error[0] = '\0';
    return true;
}

void ptc_ui_numpad_finish(PtcUiModel *model)
{
    if (!model || model->overlay != PTC_UI_OVERLAY_NUMPAD) {
        return;
    }
    model->overlay = model->numpad_return_overlay;
    model->numpad_purpose = PTC_UI_NUMPAD_NONE;
    model->numpad_return_overlay = PTC_UI_OVERLAY_NONE;
    model->numpad_error[0] = '\0';
}

int ptc_ui_preview_remaining_minutes(const PtcUiModel *model)
{
    int remaining;
    if (!model) {
        return 0;
    }

    if (model->operation == PTC_UI_OPERATION_PARENT_UNLOCK) {
        return (int)model->draft_minutes;
    }

    if (model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        if (model->remaining_available && model->remaining_minutes >= 0) {
            return model->remaining_minutes + (int)model->draft_minutes;
        }
        return (int)model->draft_minutes;
    }

    remaining = (int)model->draft_minutes;
    if (model->played_minutes_available && model->played_minutes >= 0) {
        remaining = (int)model->draft_minutes - model->played_minutes;
        if (remaining < 0) {
            remaining = 0;
        }
    }
    return remaining;
}

PtcRuleMode ptc_ui_next_rule_mode(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_LIMIT:
        return PTC_RULE_MODE_UNLIMITED;
    case PTC_RULE_MODE_UNLIMITED:
        return PTC_RULE_MODE_BLOCKED;
    case PTC_RULE_MODE_BLOCKED:
    default:
        return PTC_RULE_MODE_LIMIT;
    }
}

PtcLimitAction ptc_ui_shift_limit_action(PtcLimitAction action, int direction)
{
    int value = (int)action;
    if (value < PTC_LIMIT_ACTION_REMIND || value > PTC_LIMIT_ACTION_SUSPEND) {
        value = PTC_LIMIT_ACTION_REMIND;
    }
    value += direction >= 0 ? 1 : -1;
    if (value > PTC_LIMIT_ACTION_SUSPEND) {
        value = PTC_LIMIT_ACTION_REMIND;
    } else if (value < PTC_LIMIT_ACTION_REMIND) {
        value = PTC_LIMIT_ACTION_SUSPEND;
    }
    return (PtcLimitAction)value;
}

bool ptc_ui_limit_minutes_would_restrict(const PtcUiModel *model, uint16_t minutes)
{
    return model && model->played_minutes_available && model->played_minutes >= 0 &&
        minutes <= (uint16_t)model->played_minutes;
}

bool ptc_ui_day_rule_would_restrict(const PtcUiModel *model, PtcDayRule rule)
{
    if (rule.mode == PTC_RULE_MODE_BLOCKED) {
        return true;
    }
    return rule.mode == PTC_RULE_MODE_LIMIT && ptc_ui_limit_minutes_would_restrict(model, rule.minutes);
}

bool ptc_ui_bedtime_active_at(const PtcBedtimeRule *bedtime, uint16_t minute_of_day)
{
    if (!bedtime || !bedtime->enabled || bedtime->start_min == bedtime->end_min) {
        return false;
    }
    if (bedtime->start_min < bedtime->end_min) {
        return minute_of_day >= bedtime->start_min && minute_of_day < bedtime->end_min;
    }
    return minute_of_day >= bedtime->start_min || minute_of_day < bedtime->end_min;
}

uint16_t ptc_ui_minutes_to_bedtime(uint16_t now_minute, const PtcBedtimeRule *bedtime)
{
    if (!bedtime || !bedtime->enabled || bedtime->start_min == bedtime->end_min) {
        return 1440U;
    }
    if (ptc_ui_bedtime_active_at(bedtime, now_minute)) {
        return 0U;
    }
    if (bedtime->start_min > now_minute) {
        return (uint16_t)(bedtime->start_min - now_minute);
    }
    return (uint16_t)((1440U - now_minute) + bedtime->start_min);
}

bool ptc_ui_check_bedtime_conflict(const PtcUiModel *model, uint16_t target_minutes, PtcUiBedtimeConflict *out)
{
    uint16_t now_min;
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!model) {
        return false;
    }
    now_min = model->played_minutes_available && model->played_minutes >= 0
        ? (uint16_t)(model->played_minutes % 1440)
        : 720U;
    out->minutes_to_bedtime = ptc_ui_minutes_to_bedtime(now_min, &model->draft_bedtime);

    if (model->draft_bedtime.enabled) {
        if (target_minutes > out->minutes_to_bedtime && out->minutes_to_bedtime < 1440U) {
            out->conflict_detected = true;
            out->quota_exceeds_bedtime = true;
            snprintf(out->warning_text, sizeof(out->warning_text),
                "⚠️ 冲突提醒：目标配额(%u分) 超过就寝倒计时(%u分)。到就寝时间(%02u:%02u)将优先截止限制！",
                (unsigned int)target_minutes,
                (unsigned int)out->minutes_to_bedtime,
                (unsigned int)(model->draft_bedtime.start_min / 60U),
                (unsigned int)(model->draft_bedtime.start_min % 60U));
        }
        if (model->current_limit_action_loaded && model->current_limit_action == PTC_LIMIT_ACTION_REMIND) {
            out->conflict_detected = true;
            out->remind_bedtime_conflict = true;
            if (!out->quota_exceeds_bedtime) {
                snprintf(out->warning_text, sizeof(out->warning_text),
                    "⚠️ 提醒：目前动作为【提醒】，到就寝时间(%02u:%02u)不会强行关停；如需强制关停请更改为【强行禁玩】。",
                    (unsigned int)(model->draft_bedtime.start_min / 60U),
                    (unsigned int)(model->draft_bedtime.start_min % 60U));
            }
        }
    }
    return out->conflict_detected;
}

int64_t ptc_ui_setup_grace_remaining(const PtcUiModel *model, int64_t now)
{
    if (!model || strcmp(model->setup_phase, "released") != 0 || model->setup_activate_after <= 0) {
        return -1;
    }
    if (now >= model->setup_activate_after) {
        return 0;
    }
    return model->setup_activate_after - now;
}

bool ptc_ui_cancel_overlay(PtcUiModel *model)
{
    if (!model || model->overlay == PTC_UI_OVERLAY_NONE) {
        return false;
    }
    if (model->overlay == PTC_UI_OVERLAY_NUMPAD) {
        ptc_ui_numpad_finish(model);
    } else {
        model->overlay = PTC_UI_OVERLAY_NONE;
        model->operation = PTC_UI_OPERATION_NONE;
    }
    return true;
}

PtcUiOperation ptc_ui_take_confirmed_operation(PtcUiModel *model)
{
    PtcUiOperation operation;
    if (!model || model->overlay != PTC_UI_OVERLAY_CONFIRM) {
        return PTC_UI_OPERATION_NONE;
    }
    operation = model->operation;
    model->overlay = PTC_UI_OVERLAY_NONE;
    model->operation = PTC_UI_OPERATION_NONE;
    return operation;
}

bool ptc_ui_apply_result_json(PtcUiModel *model, const char *text)
{
    PtcCompanionResultSummary summary;
    cJSON *root;
    const cJSON *state;
    const cJSON *capabilities;
    const cJSON *setup;
    const cJSON *probe;
    const char *status;
    const char *type;
    const char *mode;
    bool dry_run;
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
    mode = summary.mode;
    if (!status || (strcmp(status, "ok") != 0 && strcmp(status, "error") != 0)) {
        cJSON_Delete(root);
        return false;
    }
    dry_run = summary.dry_run;
    snprintf(model->result_status, sizeof(model->result_status), "%s", status ? status : "error");
    snprintf(model->result_type, sizeof(model->result_type), "%s", type ? type : "");
    snprintf(model->mode, sizeof(model->mode), "%s", localized_mode(mode));
    model->feedback_detail[0] = '\0';

    state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (strcmp(status, "ok") == 0 && cJSON_IsObject(state)) {
        bool preserve_played_minutes = model->played_minutes_available &&
            !summary.played_minutes_available;
        bool is_parent_unlock_request = type &&
            (strcmp(type, "parent_unlock_start") == 0 ||
             strcmp(type, "parent_unlock_end") == 0);

        model->status_loaded = true;
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
        model->bedtime_active = json_bool(state, "bedtime_active", false);
        /* Non-unlock operations can return a default false value when they do
         * not reload the local unlock state. Do not clear a known active window. */
        if (is_parent_unlock_request || !model->parent_unlock_active ||
            json_bool(state, "parent_unlock_active", false)) {
            model->parent_unlock_active = json_bool(state, "parent_unlock_active", false);
        }
    }
    capabilities = cJSON_GetObjectItemCaseSensitive(root, "capabilities");
    if (strcmp(status, "ok") == 0 && cJSON_IsObject(capabilities)) {
        model->raw_block_verified = json_bool(capabilities, "raw_block_verified", false);
        model->suspend_verified = json_bool(capabilities, "suspend_verified", false);
    }
    setup = cJSON_GetObjectItemCaseSensitive(root, "setup");
    if (strcmp(status, "ok") == 0 && cJSON_IsObject(setup)) {
        bool setup_was_waiting = strcmp(model->setup_phase, "released") == 0 &&
            model->setup_activate_after > 0;
        snprintf(model->setup_phase, sizeof(model->setup_phase), "%s", json_string(setup, "phase"));
        model->setup_restriction_cleared = json_bool(setup, "restriction_cleared", false);
        model->setup_snapshot_available = json_bool(setup, "snapshot_available", false);
        model->setup_activate_after = json_int(setup, "activate_after", 0);
        if (type && strcmp(type, "complete_setup") == 0 &&
            strcmp(model->setup_phase, "released") == 0 && model->setup_activate_after > 0) {
            model->view = PTC_UI_SETUP;
        } else if (strcmp(model->setup_phase, "active") != 0 && model->view != PTC_UI_PARENT) {
            model->view = PTC_UI_SETUP;
        } else if (strcmp(model->setup_phase, "active") == 0 && model->view == PTC_UI_SETUP) {
            model->view = PTC_UI_CHILD;
        }
        setup_activated = setup_was_waiting && strcmp(model->setup_phase, "active") == 0;
    }
    if (status && strcmp(status, "error") == 0) {
        const char *message = summary.message[0] ? summary.message : NULL;
        const char *failure_stage = "";
        const char *hint = "";
        probe = cJSON_GetObjectItemCaseSensitive(root, "pctl_raw_block_probe");
        if (cJSON_IsObject(probe)) {
            failure_stage = json_string(probe, "failure_stage");
            hint = effect_failure_hint(probe);
        }
        snprintf(model->message, sizeof(model->message), "%s", message ? message : "后台拒绝了本次操作。");
        if (summary.error_code > 0) {
            /* Try type-specific guidance first; fall back to generic detail. */
            model->feedback_detail[0] = '\0';
            fill_error_guidance(model->feedback_detail, sizeof(model->feedback_detail),
                                type, summary.error_code, summary.reason);
            if (!model->feedback_detail[0]) {
                snprintf(
                    model->feedback_detail,
                    sizeof(model->feedback_detail),
                    "反馈码：%d %s%s%s%s",
                    summary.error_code,
                    summary.reason[0] ? summary.reason : "unknown",
                    failure_stage[0] ? "/" : "",
                    failure_stage,
                    hint);
            }
        }
    } else if (setup_activated) {
        snprintf(model->message, sizeof(model->message), "自动控制已启用，首次设置完成。");
    } else {
        const char *guidance = request_success_guidance(type);
        snprintf(model->message, sizeof(model->message), "%s", request_success_message(type, mode, dry_run));
        if (guidance[0]) {
            snprintf(model->feedback_detail, sizeof(model->feedback_detail), "%s", guidance);
        }
    }
    cJSON_Delete(root);
    return true;
}

/*
 * Shared control geometry. These rects are the single source of truth for both
 * the renderer (ui_graphics.c) and touch hit-testing (ptc_ui_hit_test), so a
 * tap always lands on exactly the control that was drawn.
 */

#define PTC_UI_SCREEN_W 1280
#define PTC_UI_SCREEN_H 720
#define PTC_UI_DIALOG_BTN_W 210
#define PTC_UI_DIALOG_BTN_H 52

PtcUiRect ptc_ui_child_submit_rect(void)
{
    PtcUiRect rect = {86, 354, 696, 92};
    return rect;
}

PtcUiRect ptc_ui_child_refresh_rect(void)
{
    /* "Y 刷新状态" line inside the status-detail card. */
    PtcUiRect rect = {836, 430, 390, 54};
    return rect;
}

PtcUiRect ptc_ui_child_footer_rect(int index)
{
    static const int widths[] = {250, 180, 180};
    static const int xs[] = {54, 322, 520};
    PtcUiRect rect = {0, 660, 0, 48};
    if (index >= 0 && index < 3) {
        rect.x = xs[index];
        rect.w = widths[index];
    }
    return rect;
}

PtcUiRect ptc_ui_error_retry_rect(void)
{
    PtcUiRect rect = {294, 478, 320, 76};
    return rect;
}

PtcUiRect ptc_ui_error_back_rect(void)
{
    PtcUiRect rect = {666, 478, 320, 76};
    return rect;
}

PtcUiRect ptc_ui_parent_footer_rect(int index)
{
    static const int widths[] = {170, 170, 220, 250};
    static const int xs[] = {54, 242, 430, 668};
    PtcUiRect rect = {0, 660, 0, 48};
    if (index >= 0 && index < 4) {
        rect.x = xs[index];
        rect.w = widths[index];
    }
    return rect;
}

PtcUiRect ptc_ui_parent_tab_rect(int index)
{
    PtcUiRect rect = {54 + index * 214, 108, 194, 48};
    return rect;
}

PtcUiRect ptc_ui_parent_card_rect(int index)
{
    int column = index % 2;
    int row = index / 2;
    PtcUiRect rect = {54 + column * 385, 176 + row * 110, 365, 94};
    return rect;
}

PtcUiRect ptc_ui_dialog_rect(int width, int height)
{
    PtcUiRect rect = {(PTC_UI_SCREEN_W - width) / 2, (PTC_UI_SCREEN_H - height) / 2 - 10, width, height};
    return rect;
}

static void dialog_dims(PtcUiOverlay overlay, int *width, int *height)
{
    switch (overlay) {
    case PTC_UI_OVERLAY_MINUTES:
        *width = 720;
        *height = 460;
        break;
    case PTC_UI_OVERLAY_WEEKLY:
        *width = 1172;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_BEDTIME:
        *width = 820;
        *height = 500;
        break;
    case PTC_UI_OVERLAY_LIMIT_ACTION:
        *width = 850;
        *height = 360;
        break;
    case PTC_UI_OVERLAY_NUMPAD:
        *width = 620;
        *height = 610;
        break;
    case PTC_UI_OVERLAY_CONFIRM:
    default:
        *width = 760;
        *height = 330;
        break;
    }
}

static PtcUiRect dialog_for(PtcUiOverlay overlay)
{
    int width = 0;
    int height = 0;
    dialog_dims(overlay, &width, &height);
    return ptc_ui_dialog_rect(width, height);
}

PtcUiRect ptc_ui_minutes_value_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 170, dialog.y + 160, 380, 104};
    return rect;
}

PtcUiRect ptc_ui_minutes_dec_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 46, dialog.y + 160, 104, 104};
    return rect;
}

PtcUiRect ptc_ui_minutes_inc_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 570, dialog.y + 160, 104, 104};
    return rect;
}

PtcUiRect ptc_ui_minutes_inc_large_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 288, dialog.y + 118, 144, 34};
    return rect;
}

PtcUiRect ptc_ui_minutes_dec_large_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 288, dialog.y + 272, 144, 34};
    return rect;
}

PtcUiRect ptc_ui_weekly_day_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 26 + index * 160, dialog.y + 122, 142, 190};
    return rect;
}

PtcUiRect ptc_ui_bedtime_field_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_BEDTIME);
    PtcUiRect rect = {dialog.x + 46 + index * 236, dialog.y + 154, 210, 92};
    return rect;
}

PtcUiRect ptc_ui_limit_option_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_LIMIT_ACTION);
    PtcUiRect rect = {dialog.x + 46 + index * 254, dialog.y + 142, 228, 84};
    return rect;
}

PtcUiRect ptc_ui_weekly_day_minutes_rect(int index)
{
    PtcUiRect card = ptc_ui_weekly_day_rect(index);
    PtcUiRect rect = {card.x + 8, card.y + 112, card.w - 16, 56};
    return rect;
}

PtcUiRect ptc_ui_numpad_display_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_NUMPAD);
    PtcUiRect rect = {dialog.x + 70, dialog.y + 112, dialog.w - 140, 64};
    return rect;
}

PtcUiRect ptc_ui_numpad_key_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_NUMPAD);
    int row = index / 3;
    int column = index % 3;
    PtcUiRect rect = {dialog.x + 116 + column * 134, dialog.y + 214 + row * 66, 120, 54};
    if (index < 0 || index >= 12) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

static int dialog_button_top(PtcUiRect dialog)
{
    return dialog.y + dialog.h - PTC_UI_DIALOG_BTN_H - 24;
}

PtcUiRect ptc_ui_confirm_rect(PtcUiOverlay overlay)
{
    PtcUiRect dialog = dialog_for(overlay);
    PtcUiRect rect = {dialog.x + dialog.w - 24 - PTC_UI_DIALOG_BTN_W, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    return rect;
}

PtcUiRect ptc_ui_cancel_rect(PtcUiOverlay overlay)
{
    PtcUiRect dialog = dialog_for(overlay);
    PtcUiRect rect = {dialog.x + dialog.w - 24 - PTC_UI_DIALOG_BTN_W * 2 - 16, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    return rect;
}

/* Left-aligned editor helper buttons for the weekly overlay. */
PtcUiRect ptc_ui_weekly_mode_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 34, dialog_button_top(dialog), 150, PTC_UI_DIALOG_BTN_H};
    return rect;
}

PtcUiRect ptc_ui_weekly_min_down_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 426, dialog.y + 464, 140, 32};
    return rect;
}

PtcUiRect ptc_ui_weekly_min_up_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 426, dialog.y + 350, 140, 32};
    return rect;
}

PtcUiRect ptc_ui_weekly_min_input_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 406, dialog.y + 390, 180, 66};
    return rect;
}

PtcUiRect ptc_ui_weekly_min_dec_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 310, dialog.y + 390, 80, 66};
    return rect;
}

PtcUiRect ptc_ui_weekly_min_inc_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 602, dialog.y + 390, 80, 66};
    return rect;
}

/* Left-aligned time steppers for the bedtime overlay. */
PtcUiRect ptc_ui_bedtime_adj_down_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_BEDTIME);
    PtcUiRect field = ptc_ui_bedtime_field_rect(index == 2 ? 2 : 1);
    PtcUiRect rect = {field.x + 35, dialog.y + 280, 140, 34};
    return rect;
}

PtcUiRect ptc_ui_bedtime_adj_up_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_BEDTIME);
    PtcUiRect field = ptc_ui_bedtime_field_rect(index == 2 ? 2 : 1);
    PtcUiRect rect = {field.x + 35, dialog.y + 116, 140, 34};
    return rect;
}

bool ptc_ui_rect_contains(PtcUiRect rect, int x, int y)
{
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

static PtcUiHit make_hit(PtcUiHitKind kind, int index)
{
    PtcUiHit hit;
    hit.kind = kind;
    hit.index = index;
    return hit;
}

static PtcUiHit hit_test_overlay(const PtcUiModel *model, int x, int y)
{
    int i;
    if (ptc_ui_rect_contains(ptc_ui_confirm_rect(model->overlay), x, y)) {
        return make_hit(PTC_UI_HIT_OVERLAY_CONFIRM, 0);
    }
    if (ptc_ui_rect_contains(ptc_ui_cancel_rect(model->overlay), x, y)) {
        return make_hit(PTC_UI_HIT_OVERLAY_CANCEL, 0);
    }
    switch (model->overlay) {
    case PTC_UI_OVERLAY_MINUTES:
        if (ptc_ui_rect_contains(ptc_ui_minutes_value_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_MINUTES_VALUE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_minutes_dec_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_MINUTES_DEC, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_minutes_inc_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_MINUTES_INC, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_minutes_dec_large_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_MINUTES_DEC_LARGE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_minutes_inc_large_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_MINUTES_INC_LARGE, 0);
        }
        break;
    case PTC_UI_OVERLAY_WEEKLY:
        for (i = 0; i < 7; ++i) {
            if (model->draft_week[i].mode == PTC_RULE_MODE_LIMIT &&
                ptc_ui_rect_contains(ptc_ui_weekly_day_minutes_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_MIN_INPUT, i);
            }
            if (ptc_ui_rect_contains(ptc_ui_weekly_day_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_DAY, i);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_mode_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MODE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_min_up_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MIN_UP, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_min_down_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MIN_DOWN, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_min_dec_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MIN_DEC, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_min_inc_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MIN_INC, 0);
        }
        if (model->draft_week[model->editor_index].mode == PTC_RULE_MODE_LIMIT &&
            ptc_ui_rect_contains(ptc_ui_weekly_min_input_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MIN_INPUT, model->editor_index);
        }
        break;
    case PTC_UI_OVERLAY_BEDTIME:
        for (i = 0; i < 3; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_bedtime_field_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_BEDTIME_FIELD, i);
            }
        }
        for (i = 1; model->draft_bedtime.enabled && i <= 2; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_bedtime_adj_up_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_BEDTIME_ADJ_UP, i);
            }
            if (ptc_ui_rect_contains(ptc_ui_bedtime_adj_down_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_BEDTIME_ADJ_DOWN, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_LIMIT_ACTION:
        for (i = 0; i < 3; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_limit_option_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_LIMIT_ACTION_OPTION, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_NUMPAD:
        for (i = 0; i < 12; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_numpad_key_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_NUMPAD_KEY, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_CONFIRM:
    case PTC_UI_OVERLAY_NONE:
    default:
        break;
    }
    /* Overlay is modal: taps on empty space do nothing, never fall through. */
    return make_hit(PTC_UI_HIT_NONE, 0);
}

PtcUiHit ptc_ui_hit_test(const PtcUiModel *model, int x, int y)
{
    int i;
    int count;
    if (!model) {
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->overlay != PTC_UI_OVERLAY_NONE) {
        return hit_test_overlay(model, x, y);
    }
    if (model->view == PTC_UI_CHILD) {
        if (ptc_ui_rect_contains(ptc_ui_child_submit_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_SUBMIT_CODE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_child_refresh_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_REFRESH, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_child_footer_rect(0), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_SUBMIT_CODE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_child_footer_rect(1), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_REFRESH, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_child_footer_rect(2), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_EXIT, 0);
        }
        /* The parent area stays hidden; touch never exposes it. */
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->view == PTC_UI_SETUP) {
        if (ptc_ui_rect_contains(ptc_ui_child_footer_rect(1), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_REFRESH, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_child_footer_rect(2), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_EXIT, 0);
        }
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->view == PTC_UI_ERROR) {
        if (ptc_ui_rect_contains(ptc_ui_error_retry_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_ERROR_RETRY, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_error_back_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_ERROR_BACK, 0);
        }
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    for (i = 0; i < PTC_UI_PARENT_PAGE_COUNT; ++i) {
        if (ptc_ui_rect_contains(ptc_ui_parent_tab_rect(i), x, y)) {
            return make_hit(PTC_UI_HIT_PARENT_TAB, i);
        }
    }
    if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(0), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_PREV_PAGE, 0);
    }
    if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(1), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_NEXT_PAGE, 0);
    }
    if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(2), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_REFRESH, 0);
    }
    if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(3), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_BACK, 0);
    }
    count = ptc_ui_parent_action_count(model->parent_page);
    for (i = 0; i < count; ++i) {
        if (ptc_ui_rect_contains(ptc_ui_parent_card_rect(i), x, y)) {
            return make_hit(PTC_UI_HIT_PARENT_CARD, i);
        }
    }
    return make_hit(PTC_UI_HIT_NONE, 0);
}
void ptc_ui_set_execution(PtcUiModel *model, const char *command_name, const char *transport_label)
{
    char command_copy[sizeof(model->command_name)];
    char transport_copy[sizeof(model->transport_label)];
    if (!model) {
        return;
    }
    snprintf(
        command_copy,
        sizeof(command_copy),
        "%s",
        command_name && command_name[0] ? command_name : "未开始");
    snprintf(
        transport_copy,
        sizeof(transport_copy),
        "%s",
        transport_label && transport_label[0] ? transport_label : "传输：未开始");
    snprintf(
        model->command_name,
        sizeof(model->command_name),
        "%s",
        command_copy);
    snprintf(
        model->transport_label,
        sizeof(model->transport_label),
        "%s",
        transport_copy);
}

PtcUiActionState ptc_ui_safety_action_available(const PtcUiModel *model, int index)
{
    bool is_released;
    bool is_pending_or_failed;
    bool is_active;
    if (!model) {
        return PTC_UI_ACTION_DISABLED;
    }
    is_released = strcmp(model->setup_phase, "released") == 0;
    is_pending_or_failed = strcmp(model->setup_phase, "pending") == 0 ||
                           strcmp(model->setup_phase, "failed") == 0;
    is_active = strcmp(model->setup_phase, "active") == 0;
    switch (index) {
    case 0: /* 启用自动控制 */
        if (is_released) {
            return PTC_UI_ACTION_RECOMMENDED;
        }
        return PTC_UI_ACTION_DISABLED;
    case 1: /* 重试前置解限 */
        if (is_pending_or_failed) {
            return is_released ? PTC_UI_ACTION_AVAILABLE : PTC_UI_ACTION_RECOMMENDED;
        }
        return PTC_UI_ACTION_DISABLED;
    case 2: /* 恢复安装前状态 */
        return model->setup_snapshot_available ? PTC_UI_ACTION_AVAILABLE : PTC_UI_ACTION_DISABLED;
    case 3: /* 紧急停用控制 / 解除紧急停用 */
        return PTC_UI_ACTION_AVAILABLE;
    case 4: /* 验证强制阻止 */
        if (!is_active) {
            return PTC_UI_ACTION_DISABLED;
        }
        return model->raw_block_verified ? PTC_UI_ACTION_DISABLED : PTC_UI_ACTION_AVAILABLE;
    case 5: /* 验证暂停软件 */
        if (!is_active) {
            return PTC_UI_ACTION_DISABLED;
        }
        return model->suspend_verified ? PTC_UI_ACTION_DISABLED : PTC_UI_ACTION_AVAILABLE;
    case 6: /* 修改加时码密钥 */
        return PTC_UI_ACTION_AVAILABLE;
    default:
        return PTC_UI_ACTION_DISABLED;
    }
}

const char *ptc_ui_safety_action_hint(const PtcUiModel *model, int index)
{
    bool is_released;
    bool is_pending_or_failed;
    bool is_active;
    if (!model) {
        return "";
    }
    is_released = strcmp(model->setup_phase, "released") == 0;
    is_pending_or_failed = strcmp(model->setup_phase, "pending") == 0 ||
                           strcmp(model->setup_phase, "failed") == 0;
    is_active = strcmp(model->setup_phase, "active") == 0;
    switch (index) {
    case 0:
        if (is_released) {
            return "限制已解除，建议现在启用自动控制。";
        }
        if (is_active) {
            return "自动控制已启用，无需再次操作。";
        }
        if (is_pending_or_failed) {
            return "需要先完成前置解限才能启用。";
        }
        return "当前阶段不支持此操作。";
    case 1:
        if (is_pending_or_failed) {
            return "前置解限未完成或失败，建议重试。";
        }
        return "仅在 pending/failed 阶段可用。";
    case 2:
        if (!model->setup_snapshot_available) {
            return "安装前快照不可用，无法恢复。";
        }
        return "精确恢复安装前的家长控制状态。";
    case 3:
        return model->disable_flag_present
            ? "删除 disable.flag，恢复后台正常控制。"
            : "创建 disable.flag，后台立即停止控制操作。";
    case 4:
        if (!is_active) {
            return "需要先完成首次设置（phase=active）。";
        }
        if (model->raw_block_verified) {
            return "强制阻止能力已验证，无需再次探测。";
        }
        return "探针会尝试真机写入并回滚验证。";
    case 5:
        if (!is_active) {
            return "需要先完成首次设置（phase=active）。";
        }
        if (model->suspend_verified) {
            return "暂停软件能力已验证，无需再次探测。";
        }
        return "探针会尝试真机写入并回滚验证。";
    case 6:
        return "修改离线加时码的 HMAC 计算密钥（默认为 replace-with-long-random-secret）。";
    default:
        return "";
    }
}
