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

const char *ptc_ui_shortcut_common_label(int index)
{
    static const char *labels[] = {
        "L + R", "L + R + 上", "L + R + 下", "L + R + 左", "L + R + 右", "L + R + Plus +", "L + R + Minus -",
        "ZL + ZR", "ZL + ZR + 上", "ZL + ZR + 下", "ZL + ZR + 左", "ZL + ZR + 右", "ZL + ZR + Plus +", "ZL + ZR + Minus -"
    };
    if (index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        return "未选择";
    }
    return labels[index];
}

int ptc_ui_weekday_for_display_slot(int slot)
{
    static const int ORDER[] = {1, 2, 3, 4, 5, 6, 0};
    return slot >= 0 && slot < 7 ? ORDER[slot] : 0;
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
        return "加时成功，今天的游玩时间已更新。";
    }
    if (strcmp(type, "complete_setup") == 0) {
        return "首次设置已完成，自动控制将在宽限结束后启用。";
    }
    if (strcmp(type, "retry_setup_release") == 0) {
        return "当前限制已解除，可以继续完成首次设置。";
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
        return "今日临时设置已清除，已恢复周计划。";
    }
    if (strcmp(type, "set_weekly_template") == 0) {
        return "周计划已保存；今天如无临时设置，将由后台同步。";
    }
    return "设置已生效。";
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
        return "任我玩 已停用。解除停用后选择【启用自动控制】即可重新完成设置。";
    }
    return "";
}

static void fill_error_guidance(char *out, size_t out_size, const char *type, int error_code, const char *reason)
{
    if (!type || !out || out_size == 0) {
        return;
    }
    if (error_code == 306) {
        snprintf(out, out_size,
                 "反馈码：306。可能未手动开启主机家长控制；系统设置→家长控制→开启，返回后选择“重新检测”。");
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
    }
    /* Other types: leave existing feedback_detail as-is (filled by caller). */
}

int ptc_ui_parent_action_count(PtcUiParentPage page)
{
    switch (page) {
    case PTC_UI_PARENT_PLAN:
        return 0;
    case PTC_UI_PARENT_SECURITY:
        return 5;
    case PTC_UI_PARENT_SUPPORT:
        return 5;
    case PTC_UI_PARENT_TODAY:
    default:
        return 5;
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

void ptc_ui_numpad_adjust(PtcUiModel *model, int delta)
{
    uint16_t value;
    if (!model || (model->numpad_purpose != PTC_UI_NUMPAD_MINUTES &&
                   model->numpad_purpose != PTC_UI_NUMPAD_WEEKLY_MINUTES)) {
        return;
    }
    value = model->numpad_current;
    if (model->numpad_text[0] && ptc_ui_parse_minutes(
            model->numpad_text, model->numpad_minimum, model->numpad_maximum, &value)) {
        /* A quick adjustment commits any complete value already typed. */
    }
    value = ptc_ui_adjust_minutes(
        value, delta, model->numpad_minimum, model->numpad_maximum);
    model->numpad_current = value;
    snprintf(model->numpad_text, sizeof(model->numpad_text), "%u", (unsigned int)value);
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
    if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
        model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) {
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

    if (model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        if (model->remaining_available && model->remaining_minutes >= 0) {
            return model->remaining_minutes + (int)model->draft_minutes;
        }
        return -1;
    }

    if (!model->played_minutes_available || model->played_minutes < 0) {
        return -1;
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

void ptc_ui_mark_status_updated(PtcUiModel *model, int64_t now)
{
    if (!model) {
        return;
    }
    model->status_updated_at = now > 0 ? now : 0;
}

int64_t ptc_ui_status_age_seconds(const PtcUiModel *model, int64_t now)
{
    if (!model || !model->status_loaded || model->status_updated_at <= 0) {
        return -1;
    }
    if (now <= model->status_updated_at) {
        return 0;
    }
    return now - model->status_updated_at;
}

PtcRuleMode ptc_ui_next_rule_mode(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_LIMIT:
        return PTC_RULE_MODE_UNLIMITED;
    case PTC_RULE_MODE_UNLIMITED:
    default:
        return PTC_RULE_MODE_LIMIT;
    }
}

bool ptc_ui_limit_minutes_would_restrict(const PtcUiModel *model, uint16_t minutes)
{
    return model && model->played_minutes_available && model->played_minutes >= 0 &&
        minutes <= (uint16_t)model->played_minutes;
}

bool ptc_ui_day_rule_would_restrict(const PtcUiModel *model, PtcDayRule rule)
{
    return rule.mode == PTC_RULE_MODE_LIMIT && ptc_ui_limit_minutes_would_restrict(model, rule.minutes);
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
    const cJSON *setup;
    const char *status;
    const char *type;
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
    if (!status || (strcmp(status, "ok") != 0 && strcmp(status, "error") != 0)) {
        cJSON_Delete(root);
        return false;
    }
    snprintf(model->result_status, sizeof(model->result_status), "%s", status ? status : "error");
    snprintf(model->result_type, sizeof(model->result_type), "%s", type ? type : "");
    snprintf(model->mode, sizeof(model->mode), "%s", localized_mode("release"));
    model->feedback_detail[0] = '\0';
    model->error_code = 0;

    state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (strcmp(status, "ok") == 0 && cJSON_IsObject(state)) {
        bool preserve_played_minutes = model->played_minutes_available &&
            !summary.played_minutes_available;

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
        } else if (strcmp(model->setup_phase, "active") == 0 && model->view == PTC_UI_SETUP &&
                   model->setup_step == 0) {
            model->view = PTC_UI_CHILD;
        }
        setup_activated = setup_was_waiting && strcmp(model->setup_phase, "active") == 0;
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
        snprintf(model->message, sizeof(model->message), "%s", request_success_message(type));
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
    /* Large code field in the child reward card. */
    PtcUiRect rect = {86, 292, 696, 84};
    return rect;
}

PtcUiRect ptc_ui_child_refresh_rect(void)
{
    /* Bottom action in the taller game-time-statistics card. */
    PtcUiRect rect = {866, 454, 330, 42};
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

PtcUiRect ptc_ui_setup_shortcut_card_rect(int index)
{
    int column = index / 7;
    int row = index % 7;
    PtcUiRect rect = {204 + column * 438, 278 + row * 34, 410, 29};
    if (index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

PtcUiRect ptc_ui_setup_shortcut_capture_rect(void)
{
    PtcUiRect rect = {692, 526, 384, 42};
    return rect;
}

PtcUiRect ptc_ui_setup_primary_rect(void)
{
    PtcUiRect rect = {896, 570, 330, 62};
    return rect;
}

PtcUiRect ptc_ui_setup_back_rect(void)
{
    PtcUiRect rect = {54, 570, 230, 62};
    return rect;
}

PtcUiRect ptc_ui_setup_pin_rect(void)
{
    PtcUiRect rect = {204, 300, 520, 78};
    return rect;
}

PtcUiRect ptc_ui_setup_zone_rect(int index)
{
    PtcUiRect rect = {204 + index * 448, 286, 400, 190};
    if (index < 0 || index > 1) {
        rect.w = 0;
        rect.h = 0;
    }
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

PtcUiRect ptc_ui_parent_refresh_rect(void)
{
    PtcUiRect rect = {1010, 108, 216, 48};
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
        *height = 560;
        break;
    case PTC_UI_OVERLAY_WEEKLY:
        *width = 1172;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_NUMPAD:
        *width = 620;
        *height = 700;
        break;
    case PTC_UI_OVERLAY_CREDENTIAL:
        *width = 900;
        *height = 500;
        break;
    case PTC_UI_OVERLAY_GRANT_SETUP:
        *width = 900;
        *height = 570;
        break;
    case PTC_UI_OVERLAY_QR:
        *width = 900;
        *height = 610;
        break;
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
        *width = 1120;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_GRANT_LOCAL:
        *width = 820;
        *height = 570;
        break;
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
        *width = 860;
        *height = 350;
        break;
    case PTC_UI_OVERLAY_CONFIRM:
    default:
        *width = 760;
        *height = 420;
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
    PtcUiRect rect = {dialog.x + 174, dialog.y + 140, 372, 96};
    return rect;
}

PtcUiRect ptc_ui_minutes_dec_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 36, dialog.y + 140, 112, 96};
    return rect;
}

PtcUiRect ptc_ui_minutes_inc_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 572, dialog.y + 140, 112, 96};
    return rect;
}

PtcUiRect ptc_ui_minutes_inc_large_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 378, dialog.y + 260, 156, 42};
    return rect;
}

PtcUiRect ptc_ui_minutes_dec_large_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 186, dialog.y + 260, 156, 42};
    return rect;
}

PtcUiRect ptc_ui_weekly_day_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 26 + index * 160, dialog.y + 122, 142, 190};
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
    PtcUiRect rect = {dialog.x + 70, dialog.y + 140, dialog.w - 140, 70};
    return rect;
}

PtcUiRect ptc_ui_numpad_key_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_NUMPAD);
    int row = index / 3;
    int column = index % 3;
    PtcUiRect rect = {dialog.x + 116 + column * 134, dialog.y + 324 + row * 54, 120, 46};
    if (index < 0 || index >= 12) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

PtcUiRect ptc_ui_numpad_quick_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_NUMPAD);
    PtcUiRect rect = {dialog.x + 32 + index * 140, dialog.y + 274, 124, 40};
    if (index < 0 || index >= 4) {
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

PtcUiRect ptc_ui_discard_rect(PtcUiOverlay overlay)
{
    PtcUiRect dialog = dialog_for(overlay);
    PtcUiRect rect = {dialog.x + 24, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    return rect;
}

PtcUiRect ptc_ui_weekly_save_rect(void)
{
    PtcUiRect rect = {932, 540, 260, 58};
    return rect;
}

PtcUiRect ptc_ui_weekly_discard_rect(void)
{
    PtcUiRect rect = {654, 540, 250, 58};
    return rect;
}

PtcUiRect ptc_ui_credential_input_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_CREDENTIAL);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 224, 600, 64};
    return rect;
}

PtcUiRect ptc_ui_credential_random_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_CREDENTIAL);
    PtcUiRect rect = {dialog.x + 662, dialog.y + 224, 196, 64};
    return rect;
}

PtcUiRect ptc_ui_credential_reveal_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_CREDENTIAL);
    PtcUiRect rect = {dialog.x + 662, dialog.y + 132, 196, 56};
    return rect;
}

PtcUiRect ptc_ui_credential_demo_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_CREDENTIAL);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 318, 270, 52};
    return rect;
}

PtcUiRect ptc_ui_grant_qr_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_GRANT_SETUP);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 128, 816, 92};
    return rect;
}

PtcUiRect ptc_ui_grant_local_toggle_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_GRANT_SETUP);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 234, 816, 56};
    return rect;
}

PtcUiRect ptc_ui_grant_more_toggle_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_GRANT_SETUP);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 304, 816, 50};
    return rect;
}

PtcUiRect ptc_ui_grant_export_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_GRANT_SETUP);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 374, 250, 54};
    return rect;
}

PtcUiRect ptc_ui_grant_generate_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_GRANT_LOCAL);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 382, dialog.w - 84, 60};
    return rect;
}

PtcUiRect ptc_ui_grant_edit_url_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_GRANT_SETUP);
    PtcUiRect rect = {dialog.x + 310, dialog.y + 374, 250, 54};
    return rect;
}

PtcUiRect ptc_ui_grant_reset_url_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_GRANT_SETUP);
    PtcUiRect rect = {dialog.x + 578, dialog.y + 374, 280, 54};
    return rect;
}

void ptc_ui_weekly_leave_move(PtcUiModel *model, int direction)
{
    if (!model || direction == 0) {
        return;
    }
    model->weekly_leave_selection =
        (model->weekly_leave_selection + (direction > 0 ? 1 : 2)) % 3;
}

PtcUiRect ptc_ui_shortcut_option_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_SHORTCUT_MANAGER);
    int column = index / 7;
    int row = index % 7;
    PtcUiRect rect = {dialog.x + 36 + column * 524, dialog.y + 128 + row * 40, 500, 34};
    if (index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

PtcUiRect ptc_ui_shortcut_capture_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_SHORTCUT_MANAGER);
    PtcUiRect rect = {dialog.x + 36, dialog.y + 430, 318, 46};
    return rect;
}

PtcUiRect ptc_ui_shortcut_disable_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_SHORTCUT_MANAGER);
    PtcUiRect rect = {dialog.x + 370, dialog.y + 430, 318, 46};
    return rect;
}

PtcUiRect ptc_ui_shortcut_hint_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_SHORTCUT_MANAGER);
    PtcUiRect rect = {dialog.x + 704, dialog.y + 430, 380, 46};
    return rect;
}

PtcUiRect ptc_ui_grant_adjust_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_GRANT_LOCAL);
    PtcUiRect rect = {dialog.x + 42 + index * 122, dialog.y + 250, 108, 54};
    if (index < 0 || index >= 6) {
        rect.w = 0;
        rect.h = 0;
    }
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
    if (model->overlay == PTC_UI_OVERLAY_WEEKLY_LEAVE &&
        ptc_ui_rect_contains(ptc_ui_discard_rect(model->overlay), x, y)) {
        return make_hit(PTC_UI_HIT_OVERLAY_DISCARD, 0);
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
            int weekday = ptc_ui_weekday_for_display_slot(i);
            if (model->draft_week[weekday].mode == PTC_RULE_MODE_LIMIT &&
                ptc_ui_rect_contains(ptc_ui_weekly_day_minutes_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_MIN_INPUT, weekday);
            }
            if (ptc_ui_rect_contains(ptc_ui_weekly_day_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_DAY, weekday);
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
    case PTC_UI_OVERLAY_NUMPAD:
        if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
            model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) {
            for (i = 0; i < 4; ++i) {
                if (ptc_ui_rect_contains(ptc_ui_numpad_quick_rect(i), x, y)) {
                    return make_hit(PTC_UI_HIT_NUMPAD_QUICK, i);
                }
            }
        }
        for (i = 0; i < 12; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_numpad_key_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_NUMPAD_KEY, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_CREDENTIAL:
        if (ptc_ui_rect_contains(ptc_ui_credential_input_rect(), x, y)) return make_hit(PTC_UI_HIT_CREDENTIAL_INPUT, 0);
        if (ptc_ui_rect_contains(ptc_ui_credential_random_rect(), x, y)) return make_hit(PTC_UI_HIT_CREDENTIAL_RANDOM, 0);
        if (ptc_ui_rect_contains(ptc_ui_credential_reveal_rect(), x, y)) return make_hit(PTC_UI_HIT_CREDENTIAL_REVEAL, 0);
        if (ptc_ui_rect_contains(ptc_ui_credential_demo_rect(), x, y)) return make_hit(PTC_UI_HIT_CREDENTIAL_DEMO, 0);
        break;
    case PTC_UI_OVERLAY_GRANT_SETUP:
        if (ptc_ui_rect_contains(ptc_ui_grant_qr_rect(), x, y)) return make_hit(PTC_UI_HIT_GRANT_QR, 0);
        if (ptc_ui_rect_contains(ptc_ui_grant_local_toggle_rect(), x, y)) return make_hit(PTC_UI_HIT_GRANT_LOCAL_TOGGLE, 0);
        if (ptc_ui_rect_contains(ptc_ui_grant_more_toggle_rect(), x, y)) return make_hit(PTC_UI_HIT_GRANT_MORE_TOGGLE, 0);
        if (model->grant_local_expanded &&
            ptc_ui_rect_contains(ptc_ui_grant_generate_rect(), x, y)) return make_hit(PTC_UI_HIT_GRANT_GENERATE, 0);
        if (model->grant_more_expanded) {
            if (ptc_ui_rect_contains(ptc_ui_grant_export_rect(), x, y)) return make_hit(PTC_UI_HIT_GRANT_EXPORT, 0);
            if (ptc_ui_rect_contains(ptc_ui_grant_edit_url_rect(), x, y)) return make_hit(PTC_UI_HIT_GRANT_EDIT_URL, 0);
            if (ptc_ui_rect_contains(ptc_ui_grant_reset_url_rect(), x, y)) return make_hit(PTC_UI_HIT_GRANT_RESET_URL, 0);
        }
        break;
    case PTC_UI_OVERLAY_GRANT_LOCAL:
        for (i = 0; i < 6; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_grant_adjust_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_GRANT_ADJUST, i);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_grant_generate_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_GRANT_GENERATE, 0);
        }
        break;
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
        for (i = 0; i < PTC_UI_SHORTCUT_PRESET_COUNT; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_shortcut_option_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_SHORTCUT_OPTION, i);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_shortcut_capture_rect(), x, y)) return make_hit(PTC_UI_HIT_SHORTCUT_CAPTURE, 0);
        if (ptc_ui_rect_contains(ptc_ui_shortcut_disable_rect(), x, y)) return make_hit(PTC_UI_HIT_SHORTCUT_DISABLE, 0);
        if (ptc_ui_rect_contains(ptc_ui_shortcut_hint_rect(), x, y)) return make_hit(PTC_UI_HIT_SHORTCUT_HINT, 0);
        break;
    case PTC_UI_OVERLAY_QR:
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
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
        if (!model->disable_flag_present && ptc_ui_rect_contains(ptc_ui_child_submit_rect(), x, y)) {
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
        int step = model->setup_step > 0 ? model->setup_step : PTC_UI_SETUP_SHORTCUT;
        if (ptc_ui_rect_contains(ptc_ui_setup_back_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_SETUP_BACK, 0);
        }
        if (step == PTC_UI_SETUP_SHORTCUT) {
            for (i = 0; i < PTC_UI_SHORTCUT_PRESET_COUNT; ++i) {
                if (ptc_ui_rect_contains(ptc_ui_setup_shortcut_card_rect(i), x, y)) {
                    return make_hit(PTC_UI_HIT_SETUP_SHORTCUT_CARD, i);
                }
            }
            if (ptc_ui_rect_contains(ptc_ui_setup_shortcut_capture_rect(), x, y)) {
                return make_hit(PTC_UI_HIT_SETUP_SHORTCUT_CAPTURE, 0);
            }
        } else if (step == PTC_UI_SETUP_PIN &&
                   ptc_ui_rect_contains(ptc_ui_setup_pin_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_SETUP_PIN, 0);
        } else if (step == PTC_UI_SETUP_ZONE) {
            if (ptc_ui_rect_contains(ptc_ui_setup_zone_rect(0), x, y)) {
                return make_hit(PTC_UI_HIT_SETUP_CHILD_ZONE, 0);
            }
            if (ptc_ui_rect_contains(ptc_ui_setup_zone_rect(1), x, y)) {
                return make_hit(PTC_UI_HIT_SETUP_PARENT_ZONE, 1);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_setup_primary_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_SETUP_PRIMARY, 0);
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
    if (ptc_ui_rect_contains(ptc_ui_parent_refresh_rect(), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_REFRESH, 0);
    }
    if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(0), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_PREV_PAGE, 0);
    }
    if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(1), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_NEXT_PAGE, 0);
    }
    if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(2), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_BACK, 0);
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN) {
        for (i = 0; i < 7; ++i) {
            int weekday = ptc_ui_weekday_for_display_slot(i);
            if (model->draft_week[weekday].mode == PTC_RULE_MODE_LIMIT &&
                ptc_ui_rect_contains(ptc_ui_weekly_day_minutes_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_MIN_INPUT, weekday);
            }
            if (ptc_ui_rect_contains(ptc_ui_weekly_day_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_DAY, weekday);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_mode_rect(), x, y)) return make_hit(PTC_UI_HIT_WEEKLY_MODE, 0);
        if (ptc_ui_rect_contains(ptc_ui_weekly_save_rect(), x, y)) return make_hit(PTC_UI_HIT_WEEKLY_SAVE, 0);
        if (ptc_ui_rect_contains(ptc_ui_weekly_discard_rect(), x, y)) return make_hit(PTC_UI_HIT_WEEKLY_DISCARD, 0);
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
    if (!model) {
        return PTC_UI_ACTION_DISABLED;
    }
    switch (index) {
    case 0:
        return strcmp(model->setup_phase, "active") == 0 ? PTC_UI_ACTION_DISABLED : PTC_UI_ACTION_RECOMMENDED;
    case 1:
        return strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0 ||
            strcmp(model->setup_phase, "pending") == 0 ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_DISABLED;
    case 2:
        return PTC_UI_ACTION_AVAILABLE;
    case 3:
        return model->setup_snapshot_available ? PTC_UI_ACTION_AVAILABLE : PTC_UI_ACTION_DISABLED;
    case 4:
        return PTC_UI_ACTION_AVAILABLE;
    case 5:
        return PTC_UI_ACTION_AVAILABLE;
    default:
        return PTC_UI_ACTION_DISABLED;
    }
}

const char *ptc_ui_safety_action_hint(const PtcUiModel *model, int index)
{
    if (!model) {
        return "";
    }
    switch (index) {
    case 0:
        return strcmp(model->setup_phase, "active") == 0 ? "额度管理已启用。" : "预检通过后保存快照并接管系统控制。";
    case 1:
        return "重新执行兼容、快照和恢复前置检查。";
    case 2:
        return model->disable_flag_present
            ? "解除停用后才允许新的控制写入；状态和恢复始终可用。"
            : "只停止新的控制写入；状态、诊断和恢复仍可使用。";
    case 3:
        return model->setup_snapshot_available ? "精确恢复安装前状态。" : "安装前快照不可用。";
    case 4:
        return "导出时自动排除 secret、PIN、离线码和完整 nonce。";
    case 5:
        return "控制孩子区是否显示进入家长区的快捷键说明。";
    default:
        return "";
    }
}
