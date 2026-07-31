#include "ui_graphics.h"

#include <stdio.h>
#include <string.h>

#include "../../third_party/cjson/cJSON.h"

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : NULL;
}

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

static const char *request_success_message(const char *type, bool dry_run)
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
    if (strcmp(type, "probe_play_timer_effect") == 0) {
        return "设备快速测试已完成，正在检查恢复证据。";
    }
    return dry_run ? "设置验证通过；观察模式不会修改系统设置。" : "设置已生效。";
}

int ptc_ui_parent_action_count(PtcUiParentPage page)
{
    switch (page) {
    case PTC_UI_PARENT_PLAN:
        return 5;
    case PTC_UI_PARENT_SAFETY:
        return 3;
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
    return (uint16_t)adjusted;
}

uint16_t ptc_ui_adjust_minute_of_day(uint16_t value, int delta)
{
    int adjusted = ((int)value + delta) % 1440;
    if (adjusted < 0) {
        adjusted += 1440;
    }
    return (uint16_t)adjusted;
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

bool ptc_ui_cancel_overlay(PtcUiModel *model)
{
    if (!model || model->overlay == PTC_UI_OVERLAY_NONE) {
        return false;
    }
    model->overlay = PTC_UI_OVERLAY_NONE;
    model->operation = PTC_UI_OPERATION_NONE;
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
    cJSON *root;
    const cJSON *state;
    const cJSON *capabilities;
    const cJSON *error;
    const char *status;
    const char *type;
    const char *mode;
    bool dry_run;
    if (!model || !text) {
        return false;
    }
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    status = json_string(root, "status");
    type = json_string(root, "type");
    mode = json_string(root, "mode");
    if (!status || (strcmp(status, "ok") != 0 && strcmp(status, "error") != 0)) {
        cJSON_Delete(root);
        return false;
    }
    dry_run = json_bool(root, "dry_run", false);
    snprintf(model->result_status, sizeof(model->result_status), "%s", status ? status : "error");
    snprintf(model->result_type, sizeof(model->result_type), "%s", type ? type : "");
    snprintf(model->mode, sizeof(model->mode), "%s", localized_mode(mode));

    state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (cJSON_IsObject(state)) {
        model->status_loaded = true;
        model->limited_today = json_int(state, "limited_today", -1);
        model->blocked_today = json_int(state, "blocked_today", -1);
        model->unrestricted_today = json_int(state, "unrestricted_today", -1);
        model->remaining_available = json_bool(state, "remaining_available", false);
        model->remaining_minutes = json_int(state, "remaining_minutes", -1);
        model->play_timer_enabled = json_int(state, "play_timer_enabled", -1);
        model->restricted_now = json_int(state, "restricted_now", -1);
        model->bedtime_active = json_bool(state, "bedtime_active", false);
        model->parent_unlock_active = json_bool(state, "parent_unlock_active", false);
    }
    capabilities = cJSON_GetObjectItemCaseSensitive(root, "capabilities");
    if (cJSON_IsObject(capabilities)) {
        model->play_timer_write_verified = json_bool(capabilities, "play_timer_write_verified", false);
        model->play_timer_effect_verified = json_bool(capabilities, "play_timer_effect_verified", false);
        model->raw_block_verified = json_bool(capabilities, "raw_block_verified", false);
        model->suspend_verified = json_bool(capabilities, "suspend_verified", false);
    }
    if (status && strcmp(status, "error") == 0) {
        const char *message = NULL;
        error = cJSON_GetObjectItemCaseSensitive(root, "error");
        if (cJSON_IsObject(error)) {
            message = json_string(error, "message");
        }
        snprintf(model->message, sizeof(model->message), "%s", message ? message : "后台拒绝了本次操作。");
    } else {
        snprintf(model->message, sizeof(model->message), "%s", request_success_message(type, dry_run));
    }
    cJSON_Delete(root);
    return true;
}
