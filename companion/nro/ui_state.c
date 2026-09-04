#include "ui_graphics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../file_protocol.h"
#include "../../common/time/ptc_time.h"
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

const char *ptc_ui_shortcut_common_label(int index)
{
    static const char *labels[] = {
        "L + R", "L + R + 上", "L + R + 下", "L + R + 左", "L + R + 右", "L + R + Plus(+)", "L + R + Minus(-)",
        "ZL + ZR", "ZL + ZR + 上", "ZL + ZR + 下", "ZL + ZR + 左", "ZL + ZR + 右", "ZL + ZR + Plus(+)", "ZL + ZR + Minus(-)"
    };
    if (index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        return "未选择";
    }
    return labels[index];
}

bool ptc_ui_shortcut_mask_held(uint64_t configured_mask, uint64_t buttons)
{
    return configured_mask != 0 && (buttons & configured_mask) == configured_mask;
}

bool ptc_ui_shortcut_hold_update(PtcUiShortcutHoldState *state, bool combo_held, int required_samples)
{
    if (!state || required_samples <= 0) return false;
    if (!combo_held) {
        state->held_samples = 0;
        state->latched = false;
        return false;
    }
    if (state->latched) return false;
    if (state->held_samples < required_samples) ++state->held_samples;
    if (state->held_samples < required_samples) return false;
    state->latched = true;
    return true;
}

bool ptc_ui_confirm_hold_update(PtcUiConfirmHoldState *state, bool held, int required_samples)
{
    if (!state || required_samples <= 0) return false;
    if (!held) {
        state->held_samples = 0;
        state->completed = false;
        return false;
    }
    if (state->completed) return false;
    if (state->held_samples < required_samples) ++state->held_samples;
    if (state->held_samples < required_samples) return false;
    state->completed = true;
    return true;
}

bool ptc_ui_touch_after_entry_allowed(bool *ignore_until_release, bool touch_active)
{
    if (!ignore_until_release) return false;
    if (*ignore_until_release) {
        if (!touch_active) *ignore_until_release = false;
        return false;
    }
    return true;
}

uint16_t ptc_ui_confirm_hold_progress(const PtcUiConfirmHoldState *state, int required_samples)
{
    int progress;
    if (!state || required_samples <= 0 || state->held_samples <= 0) return 0;
    progress = state->held_samples * 1000 / required_samples;
    return (uint16_t)(progress > 1000 ? 1000 : progress);
}

int ptc_ui_migrate_setup_step(int step, int wizard_version)
{
    if (step <= 0) return 0;
    if (wizard_version >= 4) return step <= PTC_UI_SETUP_ZONE ? step : PTC_UI_SETUP_SHORTCUT;
    return PTC_UI_SETUP_SHORTCUT;
}

static const char *effective_rule_label(PtcRuleSource source)
{
    switch (source) {
    case PTC_RULE_SOURCE_STATUTORY_HOLIDAY: return "国家法定休假日";
    case PTC_RULE_SOURCE_MAKEUP_WORKDAY: return "国家调休工作日";
    case PTC_RULE_SOURCE_TODAY_OVERRIDE: return "今日临时设置";
    case PTC_RULE_SOURCE_WEEKLY:
    default: return "周计划";
    }
}

static void format_rule_basis(PtcDayRule rule, int played_minutes, bool played_available,
                              char *out, size_t out_size)
{
    int remaining;
    if (!out || out_size == 0) return;
    if (rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(out, out_size, "不限时");
    } else if (!played_available || played_minutes < 0) {
        snprintf(out, out_size, "额度 %u 分钟；额度消耗估算不可用，暂不能估算剩余",
                 (unsigned int)rule.minutes);
    } else {
        remaining = (int)rule.minutes - played_minutes;
        if (remaining < 0) remaining = 0;
        snprintf(out, out_size, "额度 %u 分钟 - 已耗 %d 分钟 = 预计剩余 %d 分钟",
                 (unsigned int)rule.minutes, played_minutes, remaining);
    }
}

static PtcDayRule active_rule_for_source(const PtcUiModel *model)
{
    uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
    if (strcmp(model->rule_source, "today_override") == 0) return model->today_override_rule;
    if (strcmp(model->rule_source, "statutory_holiday") == 0) return model->holiday_rule;
    if (strcmp(model->rule_source, "makeup_workday") == 0) return model->makeup_workday_rule;
    return model->draft_week[weekday];
}

PtcEffectiveRule ptc_ui_rule_after_today_restore(const PtcUiModel *model)
{
    PtcRules rules;
    PtcEffectiveRule empty;
    memset(&empty, 0, sizeof(empty));
    empty.source = PTC_RULE_SOURCE_WEEKLY;
    empty.rule.mode = PTC_RULE_MODE_LIMIT;
    empty.rule.minutes = 60;
    if (!model) return empty;
    ptc_rules_default(&rules);
    memcpy(rules.week, model->current_week, sizeof(rules.week));
    rules.today_override.present = false;
    rules.holiday_enabled = model->holiday_enabled;
    rules.holiday_rule = model->holiday_rule;
    rules.makeup_workday_rule = model->makeup_workday_rule;
    return ptc_rules_resolve(&rules, model->day_index, ptc_weekday_from_day_index(model->day_index));
}

void ptc_ui_format_restore_today_basis(const PtcUiModel *model, char *out, size_t out_size)
{
    PtcEffectiveRule after;
    char current[112];
    char restored[112];
    if (!out || out_size == 0) return;
    if (!model) {
        snprintf(out, out_size, "当前状态不可用，恢复后请刷新确认。");
        return;
    }
    after = ptc_ui_rule_after_today_restore(model);
    format_rule_basis(model->today_override_rule, model->played_minutes, model->played_minutes_available,
                      current, sizeof(current));
    format_rule_basis(after.rule, model->played_minutes, model->played_minutes_available,
                      restored, sizeof(restored));
    snprintf(out, out_size, "当前临时设置：%s\n清除后按%s：%s",
             current, effective_rule_label(after.source), restored);
}

void ptc_ui_format_weekly_save_result(const PtcUiModel *model, char *message, size_t message_size,
                                      char *detail, size_t detail_size)
{
    uint8_t weekday;
    char basis[112];
    char current_basis[112];
    const char *current_source;
    bool today_changed;
    if (!model || !message || message_size == 0 || !detail || detail_size == 0) return;
    weekday = ptc_weekday_from_day_index(model->day_index);
    today_changed = ptc_ui_day_rule_effectively_changed(model->current_week[weekday], model->draft_week[weekday]);
    format_rule_basis(model->draft_week[weekday], model->played_minutes, model->played_minutes_available,
                      basis, sizeof(basis));
    current_source = effective_rule_label(
        strcmp(model->rule_source, "today_override") == 0 ? PTC_RULE_SOURCE_TODAY_OVERRIDE :
        strcmp(model->rule_source, "statutory_holiday") == 0 ? PTC_RULE_SOURCE_STATUTORY_HOLIDAY :
        strcmp(model->rule_source, "makeup_workday") == 0 ? PTC_RULE_SOURCE_MAKEUP_WORKDAY : PTC_RULE_SOURCE_WEEKLY);
    format_rule_basis(active_rule_for_source(model), model->played_minutes, model->played_minutes_available,
                      current_basis, sizeof(current_basis));
    if (!today_changed) {
        snprintf(message, message_size, "周计划已保存；本次只修改其他日期，今天不受影响。");
        snprintf(detail, detail_size, "今天继续按%s执行：%s。", current_source, current_basis);
    } else if (strcmp(model->rule_source, "today_override") == 0) {
        snprintf(message, message_size, "周计划已保存；今天仍按临时设置执行，当前不变。");
        snprintf(detail, detail_size, "当前按今日临时设置：%s；恢复周计划生效后：%s。",
                 current_basis, basis);
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0 ||
               strcmp(model->rule_source, "makeup_workday") == 0) {
        snprintf(message, message_size, "周计划已保存；今天由%s覆盖，当前不变。",
                 strcmp(model->rule_source, "statutory_holiday") == 0 ? "国家法定休假日" : "国家调休工作日");
        snprintf(detail, detail_size, "当前按%s：%s；今天对应的周计划已更新为：%s。",
                 current_source, current_basis, basis);
    } else {
        snprintf(message, message_size, "周计划已保存并影响今天。");
        snprintf(detail, detail_size, "今天按新周计划执行：%s。", basis);
    }
}

void ptc_ui_format_holiday_save_result(const PtcUiModel *model, char *message, size_t message_size,
                                       char *detail, size_t detail_size)
{
    const char *source;
    PtcDayRule active;
    char basis[112];
    uint8_t weekday;
    if (!model || !message || message_size == 0 || !detail || detail_size == 0) return;
    detail[0] = '\0';
    weekday = ptc_weekday_from_day_index(model->day_index);
    if (strcmp(model->rule_source, "today_override") == 0) {
        format_rule_basis(model->today_override_rule, model->played_minutes,
                          model->played_minutes_available, basis, sizeof(basis));
        snprintf(message, message_size, "国家节假日设置已保存；今天仍按临时设置执行，当前不变。");
        snprintf(detail, detail_size, "原因：今日临时设置优先于国家节假日规则。");
        return;
    }
    if (strcmp(model->rule_source, "statutory_holiday") == 0) {
        source = "国家法定休假日";
        active = model->draft_holiday_rule;
    } else if (strcmp(model->rule_source, "makeup_workday") == 0) {
        source = "国家调休工作日";
        active = model->draft_makeup_workday_rule;
    } else {
        source = "周计划";
        active = model->draft_week[weekday];
    }
    format_rule_basis(active, model->played_minutes, model->played_minutes_available, basis, sizeof(basis));
    if (!model->draft_holiday_enabled) {
        snprintf(message, message_size, "国家节假日预设已保存但未启用；今天不受影响。");
        snprintf(detail, detail_size, "原因：国家节假日总开关未开启。");
    } else if (!model->calendar_covered) {
        snprintf(message, message_size, "国家节假日设置已保存；内置日历未覆盖今天，今天不受影响。");
        snprintf(detail, detail_size, "原因：今天不在内置日历覆盖范围内，继续使用周计划。");
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0 ||
               strcmp(model->rule_source, "makeup_workday") == 0) {
        snprintf(message, message_size, "国家节假日设置已保存并影响今天。");
        snprintf(detail, detail_size, "今天按%s执行：%s。", source, basis);
    } else {
        snprintf(message, message_size, "国家节假日设置已保存；今天是普通日期，不受影响。");
        snprintf(detail, detail_size, "原因：今天未命中国家节假日安排，继续使用周计划。");
    }
}

void ptc_ui_format_custom_shortcut_hint(
    const char *shortcut_label,
    char *out,
    size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "长按约 400ms：%s 进入家长区",
             shortcut_label && shortcut_label[0] ? shortcut_label : "自定义组合");
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
        return "加时成功，今天的主机使用额度已更新。";
    }
    if (strcmp(type, "clear_redemption_history") == 0) {
        return "加时码使用记录已全部清空。";
    }
    if (strcmp(type, "claim_daily_buffer") == 0) {
        return "今日自主缓冲已领取，完成今天的约定后记得休息。";
    }
    if (strcmp(type, "set_scheduled_override") == 0) {
        return "日期计划已保存，未来规则预览已更新。";
    }
    if (strcmp(type, "set_autonomy_policy") == 0) {
        return "今日自主缓冲设置已保存。";
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
        return "今日临时设置已清除，已恢复周计划。";
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

int ptc_ui_parent_action_count(PtcUiParentPage page)
{
    switch (page) {
    case PTC_UI_PARENT_PLAN:
        return 0;
    case PTC_UI_PARENT_HOLIDAY:
        return 7;
    case PTC_UI_PARENT_GRANT:
        return 4;
    case PTC_UI_PARENT_SETTINGS:
        return 5;
    case PTC_UI_PARENT_TODAY:
        return 4;
    default:
        return 5;
    }
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

const char *ptc_ui_settings_status_label(const PtcUiModel *model)
{
    if (!model) return NULL;
    if (model->disable_flag_present || model->recovery_active ||
        strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) {
        return "需处理";
    }
    if (model->setup_phase[0] && strcmp(model->setup_phase, "active") != 0) return "待完成";
    return NULL;
}

PtcUiActionState ptc_ui_settings_support_state(const PtcUiModel *model)
{
    return ptc_ui_settings_status_label(model) ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_AVAILABLE;
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
    if (model->parent_page == PTC_UI_PARENT_SETTINGS) model->settings_page = PTC_UI_SETTINGS_ROOT;
    if (!model->parent_footer_focused) model->selected_index = 0;
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
    if (model->parent_footer_focused) {
        if (horizontal < 0 && model->parent_footer_selection > 0) {
            --model->parent_footer_selection;
        } else if (horizontal > 0 && model->parent_footer_selection < 1) {
            ++model->parent_footer_selection;
        }
        if (vertical < 0) {
            model->parent_footer_focused = false;
            model->selected_index = model->parent_content_selection;
        }
        return;
    }
    count = model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED
        ? 4 : ptc_ui_parent_action_count(model->parent_page);
    if (count <= 0) {
        model->selected_index = 0;
        return;
    }
    index = model->selected_index;
    if (index < 0 ||
        (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT
            ? index >= count + model->recent_event_count
            : index >= count)) {
        index = 0;
    }
    if (model->parent_page == PTC_UI_PARENT_HOLIDAY) {
        static const int left[7]  = {0, 1, 1, 3, 3, 4, 2};
        static const int right[7] = {0, 2, 2, 4, 5, 5, 6};
        static const int up[7]    = {0, 0, 0, 1, 1, 2, 0};
        static const int down[7]  = {1, 3, 6, 3, 4, 5, 6};
        int previous = index;
        if (horizontal < 0) index = left[index];
        else if (horizontal > 0) index = right[index];
        else if (vertical < 0) index = up[index];
        else if (vertical > 0) index = down[index];
        model->selected_index = index;
        if (index == 1 || index == 2) model->holiday_last_rule = index - 1;
        if (vertical > 0 && previous == down[previous]) {
            model->parent_content_selection = previous;
            model->parent_footer_focused = true;
            model->parent_footer_selection = 1;
        }
        return;
    }
    if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT) {
        int event_count = model->recent_event_count;
        int max_index = 5 + event_count;
        if (index > max_index) index = 0;
        if (index >= 6) {
            if (vertical < 0) index = index == 6 ? 4 : index - 1;
            else if (vertical > 0 && index < max_index) ++index;
            else if (vertical > 0) {
                model->parent_content_selection = index;
                model->parent_footer_focused = true;
                model->parent_footer_selection = 1;
            }
        } else {
            int previous = index;
            if (horizontal < 0 && index % 2 == 1) --index;
            else if (horizontal > 0 && index % 2 == 0) ++index;
            else if (vertical < 0 && index >= 2) index -= 2;
            else if (vertical > 0 && index < 4) index += 2;
            else if (vertical > 0 && event_count > 0) index = 6;
            else if (vertical > 0) {
                model->parent_content_selection = previous;
                model->parent_footer_focused = true;
                model->parent_footer_selection = 1;
            }
        }
        model->selected_index = index;
        return;
    }
    if (model->parent_page == PTC_UI_PARENT_TODAY) {
        if (horizontal < 0 && index % 2 == 1) --index;
        else if (horizontal > 0 && index % 2 == 0) ++index;
        else if (vertical < 0) index = (index + 2) % 4;
        else if (vertical > 0 && index < 2) index += 2;
        else if (vertical > 0) {
            model->parent_content_selection = index;
            model->parent_footer_focused = true;
            model->parent_footer_selection = 1;
        }
        model->selected_index = index;
        return;
    }
    column = index % 2;
    if (horizontal < 0 && column > 0) {
        --index;
    } else if (horizontal > 0 && column == 0 && index + 1 < count) {
        ++index;
    }
    {
        int previous_row = index / 2;
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
        if (vertical > 0 && previous_row == row_count - 1) {
            model->parent_content_selection = index;
            model->parent_footer_focused = true;
            model->parent_footer_selection = 1;
        }
    }
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

static bool duration_purpose(PtcUiNumpadPurpose purpose)
{
    return purpose == PTC_UI_NUMPAD_MINUTES ||
        purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES ||
        purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
        purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES;
}

static bool parse_duration_component(const char *text, unsigned int maximum, unsigned int *out)
{
    const char *p;
    unsigned long value;
    if (!text || !text[0] || !out) return false;
    for (p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    value = strtoul(text, NULL, 10);
    if (value > maximum) return false;
    *out = (unsigned int)value;
    return true;
}

bool ptc_ui_duration_value(const PtcUiModel *model, uint16_t *out_value)
{
    unsigned int hours;
    unsigned int minutes;
    unsigned int total;
    if (!model || !out_value || !duration_purpose(model->numpad_purpose) ||
        !parse_duration_component(model->duration_hours_text, 24, &hours) ||
        !parse_duration_component(model->duration_minutes_text, 59, &minutes)) {
        return false;
    }
    total = hours * 60U + minutes;
    if (total < model->numpad_minimum || total > model->numpad_maximum) return false;
    *out_value = (uint16_t)total;
    return true;
}

static void set_duration_value(PtcUiModel *model, uint16_t value)
{
    if (!model) return;
    snprintf(model->duration_hours_text, sizeof(model->duration_hours_text), "%u",
             (unsigned int)(value / 60U));
    snprintf(model->duration_minutes_text, sizeof(model->duration_minutes_text), "%u",
             (unsigned int)(value % 60U));
    model->numpad_current = value;
}

void ptc_ui_duration_select_field(PtcUiModel *model, PtcUiDurationField field)
{
    if (!model || !duration_purpose(model->numpad_purpose) ||
        (field != PTC_UI_DURATION_HOURS && field != PTC_UI_DURATION_MINUTES)) return;
    model->duration_field = field;
    model->numpad_error[0] = '\0';
}

void ptc_ui_duration_toggle_field(PtcUiModel *model)
{
    if (!model || !duration_purpose(model->numpad_purpose)) return;
    ptc_ui_duration_select_field(model, model->duration_field == PTC_UI_DURATION_HOURS
        ? PTC_UI_DURATION_MINUTES : PTC_UI_DURATION_HOURS);
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
    model->numpad_replace_on_input = false;
    if (duration_purpose(purpose)) {
        set_duration_value(model, current);
        model->duration_field = PTC_UI_DURATION_MINUTES;
        model->duration_hours_replace_on_input = true;
        model->duration_minutes_replace_on_input = true;
        model->overlay = PTC_UI_OVERLAY_MINUTE_EDITOR;
    }
    model->numpad_error[0] = '\0';
    snprintf(model->numpad_title, sizeof(model->numpad_title), "%s", title ? title : "数字输入");
    snprintf(model->numpad_guide, sizeof(model->numpad_guide), "%s", guide ? guide : "使用方向键或摇杆选择数字");
}

void ptc_ui_numpad_move(PtcUiModel *model, int horizontal, int vertical)
{
    int row;
    int column;
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
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
    char *text;
    bool *replace;
    size_t length;
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
        return;
    }
    if (duration_purpose(model->numpad_purpose)) {
        text = model->duration_field == PTC_UI_DURATION_HOURS
            ? model->duration_hours_text : model->duration_minutes_text;
        replace = model->duration_field == PTC_UI_DURATION_HOURS
            ? &model->duration_hours_replace_on_input : &model->duration_minutes_replace_on_input;
    } else {
        text = model->numpad_text;
        replace = &model->numpad_replace_on_input;
    }
    if (*replace) {
        text[0] = '\0';
        *replace = false;
        return;
    }
    length = strlen(text);
    if (length > 0) {
        text[length - 1] = '\0';
    }
    model->numpad_error[0] = '\0';
}

void ptc_ui_numpad_clear(PtcUiModel *model)
{
    char *text;
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
        return;
    }
    if (duration_purpose(model->numpad_purpose)) {
        text = model->duration_field == PTC_UI_DURATION_HOURS
            ? model->duration_hours_text : model->duration_minutes_text;
        text[0] = '\0';
        if (model->duration_field == PTC_UI_DURATION_HOURS) model->duration_hours_replace_on_input = false;
        else model->duration_minutes_replace_on_input = false;
    } else {
        model->numpad_text[0] = '\0';
        model->numpad_replace_on_input = false;
    }
    model->numpad_error[0] = '\0';
}

void ptc_ui_numpad_adjust(PtcUiModel *model, int delta)
{
    uint16_t value;
    if (!model || !duration_purpose(model->numpad_purpose)) {
        return;
    }
    value = model->numpad_current;
    if (ptc_ui_duration_value(model, &value)) {
        /* A quick adjustment commits any complete value already typed. */
    }
    value = ptc_ui_adjust_minutes(
        value, delta, model->numpad_minimum, model->numpad_maximum);
    set_duration_value(model, value);
    model->duration_hours_replace_on_input = false;
    model->duration_minutes_replace_on_input = false;
    model->numpad_error[0] = '\0';
}

void ptc_ui_numpad_activate(PtcUiModel *model)
{
    char *text;
    bool *replace;
    size_t capacity;
    size_t length;
    int digit;
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
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
    if (duration_purpose(model->numpad_purpose)) {
        text = model->duration_field == PTC_UI_DURATION_HOURS
            ? model->duration_hours_text : model->duration_minutes_text;
        replace = model->duration_field == PTC_UI_DURATION_HOURS
            ? &model->duration_hours_replace_on_input : &model->duration_minutes_replace_on_input;
        capacity = model->duration_field == PTC_UI_DURATION_HOURS
            ? sizeof(model->duration_hours_text) : sizeof(model->duration_minutes_text);
    } else {
        text = model->numpad_text;
        replace = &model->numpad_replace_on_input;
        capacity = sizeof(model->numpad_text);
    }
    if (*replace) {
        text[0] = '\0';
        *replace = false;
    }
    length = strlen(text);
    if ((duration_purpose(model->numpad_purpose) && length >= 2U) ||
        length + 1 >= capacity ||
        (!duration_purpose(model->numpad_purpose) && length >= model->numpad_max_digits)) {
        snprintf(model->numpad_error, sizeof(model->numpad_error), "当前输入项最多输入 %u 位数字",
                 duration_purpose(model->numpad_purpose) ? 2U : (unsigned int)model->numpad_max_digits);
        return;
    }
    digit = model->numpad_cursor == 10 ? 0 : model->numpad_cursor + 1;
    text[length] = (char)('0' + digit);
    text[length + 1] = '\0';
    model->numpad_error[0] = '\0';
}

bool ptc_ui_numpad_validate(PtcUiModel *model, uint16_t *out_value)
{
    uint16_t value = 0;
    size_t length;
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
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
    if (duration_purpose(model->numpad_purpose)) {
        if (!ptc_ui_duration_value(model, &value)) {
            snprintf(model->numpad_error, sizeof(model->numpad_error), "请输入完整时长，总计范围为 %u 到 %u 分钟",
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
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
        return;
    }
    model->overlay = model->numpad_return_overlay;
    model->numpad_purpose = PTC_UI_NUMPAD_NONE;
    model->numpad_replace_on_input = false;
    model->duration_hours_text[0] = '\0';
    model->duration_minutes_text[0] = '\0';
    model->duration_hours_replace_on_input = false;
    model->duration_minutes_replace_on_input = false;
    model->numpad_return_overlay = PTC_UI_OVERLAY_NONE;
    model->numpad_error[0] = '\0';
}

void ptc_ui_pin_open(PtcUiModel *model, const char *title, const char *guide)
{
    if (!model) return;
    model->overlay = PTC_UI_OVERLAY_PIN;
    model->pin_text[0] = '\0';
    model->pin_error[0] = '\0';
    model->pin_keyboard_mode = false;
    model->pin_focus = 1;
    snprintf(model->pin_title, sizeof(model->pin_title), "%s", title ? title : "任我玩 PIN");
    snprintf(model->pin_guide, sizeof(model->pin_guide), "%s", guide ? guide : "摇杆方向输入；X=0，Y=9");
}

bool ptc_ui_pin_append(PtcUiModel *model, int digit)
{
    size_t length;
    if (!model || model->overlay != PTC_UI_OVERLAY_PIN || digit < 0 || digit > 9) return false;
    length = strlen(model->pin_text);
    if (length >= PTC_UI_PIN_MAX_DIGITS) {
        snprintf(model->pin_error, sizeof(model->pin_error), "最多输入 %u 位数字", PTC_UI_PIN_MAX_DIGITS);
        return false;
    }
    model->pin_text[length] = (char)('0' + digit);
    model->pin_text[length + 1] = '\0';
    model->pin_error[0] = '\0';
    return true;
}

bool ptc_ui_pin_backspace(PtcUiModel *model)
{
    size_t length;
    if (!model || model->overlay != PTC_UI_OVERLAY_PIN) return false;
    length = strlen(model->pin_text);
    if (length == 0) return false;
    model->pin_text[length - 1] = '\0';
    model->pin_error[0] = '\0';
    return true;
}

bool ptc_ui_pin_validate(PtcUiModel *model)
{
    size_t length;
    if (!model || model->overlay != PTC_UI_OVERLAY_PIN) return false;
    length = strlen(model->pin_text);
    if (length == 0 || length > PTC_UI_PIN_MAX_DIGITS) {
        snprintf(model->pin_error, sizeof(model->pin_error), "请输入 1 到 %u 位数字", PTC_UI_PIN_MAX_DIGITS);
        return false;
    }
    model->pin_error[0] = '\0';
    return true;
}

void ptc_ui_pin_finish(PtcUiModel *model)
{
    if (!model || model->overlay != PTC_UI_OVERLAY_PIN) return;
    model->overlay = PTC_UI_OVERLAY_NONE;
    model->pin_text[0] = '\0';
    model->pin_error[0] = '\0';
    model->pin_keyboard_mode = false;
    model->pin_focus = 0;
}

int ptc_ui_pin_digit_from_vector(int x, int y, int deadzone)
{
    int ax = x < 0 ? -x : x;
    int ay = y < 0 ? -y : y;
    long long radius;
    if (deadzone < 0) deadzone = 0;
    radius = (long long)x * x + (long long)y * y;
    if (radius < (long long)deadzone * deadzone) return -1;
    if ((long long)ax * 1000 < (long long)ay * 414) return y > 0 ? 1 : 5;
    if ((long long)ay * 1000 < (long long)ax * 414) return x > 0 ? 3 : 7;
    if (x > 0 && y > 0) return 2;
    if (x > 0 && y < 0) return 4;
    if (x < 0 && y < 0) return 6;
    if (x < 0 && y > 0) return 8;
    return y > 0 ? 1 : 5;
}

int ptc_ui_pin_digit_from_button(int direction)
{
    switch (direction) {
    case 0: return 1;
    case 1: return 3;
    case 2: return 5;
    case 3: return 7;
    default: return -1;
    }
}

void ptc_ui_pin_format_mask(const PtcUiModel *model, char *out, size_t out_size)
{
    size_t length;
    size_t i;
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!model) return;
    length = strlen(model->pin_text);
    if (length > PTC_UI_PIN_MAX_DIGITS) length = PTC_UI_PIN_MAX_DIGITS;
    for (i = 0; i < length && i + 1 < out_size; ++i) out[i] = '*';
    out[i < out_size ? i : out_size - 1] = '\0';
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
    if (now < model->status_updated_at) return -1;
    if (now == model->status_updated_at) {
        return 0;
    }
    return now - model->status_updated_at;
}

bool ptc_ui_status_is_fresh(const PtcUiModel *model, int64_t now)
{
    int64_t age = ptc_ui_status_age_seconds(model, now);
    return model && age >= 0 && age <= 120 && model->error_code == 0 &&
        strcmp(model->result_status, "error") != 0;
}

void ptc_ui_format_status_age(const PtcUiModel *model, int64_t now, char *out, size_t out_size)
{
    int64_t age = ptc_ui_status_age_seconds(model, now);
    if (!out || !out_size) return;
    if (!model || !model->status_loaded) snprintf(out, out_size, "等待刷新");
    else if (model->waiting) snprintf(out, out_size, "正在刷新状态...");
    else if (!ptc_ui_status_is_fresh(model, now)) snprintf(out, out_size, "状态待确认，请刷新");
    else if (age == 0) snprintf(out, out_size, "刚刚刷新");
    else if (age < 60) snprintf(out, out_size, "上次刷新：%lld 秒前", (long long)age);
    else snprintf(out, out_size, "上次刷新：%lld 分钟前", (long long)(age / 60));
}

void ptc_ui_format_code(const char *code, char *out, size_t out_size)
{
    char grouped[10] = "____ ____";
    size_t length = code ? strlen(code) : 0;
    if (!out || !out_size) return;
    for (size_t i = 0; i < 8 && i < length; ++i) grouped[i + (i >= 4)] = code[i];
    snprintf(out, out_size, "%s", grouped);
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

void ptc_ui_format_today_mode(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || !model->status_loaded) {
        snprintf(out, out_size, "等待刷新");
    } else if (model->blocked_today == 1) {
        snprintf(out, out_size, "禁止游玩");
    } else if (model->unrestricted_today == 1) {
        snprintf(out, out_size, "不限时");
    } else if (model->limited_today == 1) {
        snprintf(out, out_size,
                 model->remaining_available && model->remaining_minutes <= 0
                     ? "限时 | 额度用完"
                     : "限时");
    } else {
        snprintf(out, out_size, "状态未知");
    }
}

void ptc_ui_format_quota_remaining(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || !model->status_loaded) {
        snprintf(out, out_size, "--");
    } else if (model->unrestricted_today == 1) {
        snprintf(out, out_size, "不限时");
    } else if (model->remaining_available && model->remaining_minutes >= 0) {
        snprintf(out, out_size, "%d 分钟", model->remaining_minutes);
    } else {
        snprintf(out, out_size, "暂不可用");
    }
}

void ptc_ui_format_timer_status(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || model->play_timer_enabled < 0) {
        snprintf(out, out_size, "未确认");
    } else if (model->play_timer_enabled == 1) {
        snprintf(out, out_size, "已计时");
    } else if (model->unrestricted_today == 1) {
        snprintf(out, out_size, "无需计时");
    } else {
        snprintf(out, out_size, "未计时");
    }
}

void ptc_ui_format_console_date(const PtcUiModel *model, char *out, size_t out_size)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    if (!out || out_size == 0) return;
    /* The offline code MAC binds the day index reported by status. Never fall back
       to the NRO local clock here: a guessed date would advertise a day the
       sysmodule does not accept. */
    if (!model || !model->status_loaded ||
        !ptc_date_from_day_index(model->day_index, &year, &month, &day)) {
        snprintf(out, out_size, "主机日期待刷新");
        return;
    }
    snprintf(out, out_size, "主机今天 %04u-%02u-%02u",
             (unsigned int)year, (unsigned int)month, (unsigned int)day);
}

void ptc_ui_format_parent_status_summary(
    const PtcUiModel *model,
    int64_t now,
    char *out,
    size_t out_size)
{
    int64_t age;
    char remaining[64];
    char freshness[40];
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!model) {
        snprintf(out, out_size, "? 状态待确认  |  尚无可靠读数");
        return;
    }
    age = ptc_ui_status_age_seconds(model, now);
    if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) {
        snprintf(out, out_size, "! 保护模式  |  需要处理");
        return;
    }
    if (model->recovery_active) {
        snprintf(out, out_size, "! 恢复事务待处理  |  查看详情");
        return;
    }
    if (model->disable_flag_present) {
        snprintf(out, out_size, "! 紧急停用  |  控制写入已停止");
        return;
    }
    if (model->restriction_enabled_available && !model->restriction_enabled) {
        snprintf(out, out_size, "! Nintendo 家长控制未启用");
        return;
    }
    if (model->temporary_unlocked_available && model->temporary_unlocked) {
        snprintf(out, out_size, "! 系统限制临时解除  |  期间不计时，进入睡眠后恢复今日限制");
        return;
    }
    if (model->apply_pending_confirmation) {
        snprintf(out, out_size, "... 设置等待确认生效");
        return;
    }
    if (model->waiting) {
        snprintf(out, out_size, "... 正在检测当前状态");
        return;
    }
    if (!ptc_ui_status_is_fresh(model, now)) {
        if (age < 0) snprintf(out, out_size, "? 状态待确认  |  尚无可靠读数");
        else if (age < 3600) snprintf(out, out_size, "? 状态待确认  |  上次成功于 %lld 分钟前", (long long)(age / 60));
        else if (age < 86400) snprintf(out, out_size, "? 状态待确认  |  上次成功于 %lld 小时前", (long long)(age / 3600));
        else snprintf(out, out_size, "? 状态待确认  |  上次成功超过一天");
        return;
    }
    if (model->restricted_now == 1 || model->blocked_today == 1 ||
        (model->remaining_available && model->remaining_minutes <= 0)) {
        snprintf(out, out_size, "! 已到限制  |  今日时间已用完  |  刚刚同步");
        return;
    }
    if (model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), "今天还可玩：不限时");
    else if (model->remaining_available) snprintf(remaining, sizeof(remaining), "今天还可玩 %d 分钟", model->remaining_minutes);
    else snprintf(remaining, sizeof(remaining), "今天还可玩：暂不可用");
    if (age <= 30) snprintf(freshness, sizeof(freshness), "刚刚同步");
    else if (age < 60) snprintf(freshness, sizeof(freshness), "%lld 秒前", (long long)age);
    else snprintf(freshness, sizeof(freshness), "%lld 分钟前", (long long)(age / 60));
    snprintf(out, out_size, "控制正常  |  %s  |  %s", remaining, freshness);
}

void ptc_ui_format_holiday_priority_summary(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model) {
        snprintf(out, out_size, "当前原因：状态尚未刷新");
    } else if (model->today_override_present) {
        snprintf(out, out_size, "当前原因：今日临时设置覆盖其他规则");
    } else if (!model->holiday_enabled) {
        snprintf(out, out_size, "当前原因：节假日预设未开启，回退周计划");
    } else if (!model->calendar_covered) {
        snprintf(out, out_size, "当前原因：内置日历未覆盖，回退周计划");
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0) {
        snprintf(out, out_size, "当前原因：法定休假日命中节假日规则");
    } else if (strcmp(model->rule_source, "makeup_workday") == 0) {
        snprintf(out, out_size, "当前原因：调休工作日命中节假日规则");
    } else {
        snprintf(out, out_size, "当前原因：普通日期，回退周计划");
    }
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

bool ptc_ui_day_rule_effectively_changed(PtcDayRule before, PtcDayRule after)
{
    if (before.mode != after.mode) {
        return true;
    }
    return before.mode == PTC_RULE_MODE_LIMIT && before.minutes != after.minutes;
}

bool ptc_ui_weekly_today_changed(const PtcUiModel *model)
{
    uint8_t weekday;
    if (!model) {
        return false;
    }
    weekday = ptc_weekday_from_day_index(model->day_index);
    return ptc_ui_day_rule_effectively_changed(
        model->current_week[weekday], model->draft_week[weekday]);
}

bool ptc_ui_limit_minutes_would_restrict(const PtcUiModel *model, uint16_t minutes)
{
    return model && model->played_minutes_available && model->played_minutes >= 0 &&
        minutes <= (uint16_t)model->played_minutes;
}

bool ptc_ui_today_limit_requires_hold(const PtcUiModel *model, uint16_t minutes)
{
    return model && (model->unrestricted_today == 1 ||
        !model->played_minutes_available || model->played_minutes < 0 ||
        ptc_ui_limit_minutes_would_restrict(model, minutes));
}

void ptc_ui_format_today_limit_confirmation(
    const PtcUiModel *model,
    char *risk,
    size_t risk_size,
    char *recovery,
    size_t recovery_size)
{
    if (risk && risk_size > 0) {
        if (!model || !model->played_minutes_available || model->played_minutes < 0) {
            snprintf(risk, risk_size,
                     "风险：无法取得额度消耗估算，设置后可能立即进入时间限制");
        } else if (ptc_ui_limit_minutes_would_restrict(model, model->draft_minutes)) {
            snprintf(risk, risk_size,
                     "风险：新额度不高于额度消耗估算，设置后会立即进入时间限制");
        } else if (model->unrestricted_today == 1) {
            snprintf(risk, risk_size,
                     "提示：今天将从不限时改为限时，请确认修改后剩余时间");
        } else {
            snprintf(risk, risk_size, "提示：请确认今天的实时状态和修改结果");
        }
    }
    if (recovery && recovery_size > 0) {
        snprintf(recovery, recovery_size,
                 "解除：选择“今日不限时”“临时加时”，或兑换加时码");
    }
}

bool ptc_ui_day_rule_would_restrict(const PtcUiModel *model, PtcDayRule rule)
{
    return rule.mode == PTC_RULE_MODE_LIMIT && ptc_ui_limit_minutes_would_restrict(model, rule.minutes);
}

bool ptc_ui_setup_takeover_complete(const PtcUiModel *model)
{
    return model &&
        (strcmp(model->setup_phase, "released") == 0 ||
         (strcmp(model->setup_phase, "active") == 0 && !model->disable_flag_present));
}

bool ptc_ui_runtime_fingerprint_reconfirmation_needed(const PtcUiModel *model)
{
    return model && model->disable_flag_present &&
        strcmp(model->setup_phase, "protection") == 0 &&
        strcmp(model->disable_reason, "runtime_fingerprint_changed") == 0;
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
    bool clear_pending_code;
    if (!model || model->overlay == PTC_UI_OVERLAY_NONE) {
        return false;
    }
    clear_pending_code = model->operation == PTC_UI_OPERATION_REDEEM_OFFLINE_CODE ||
        model->overlay == PTC_UI_OVERLAY_CODE_RESULT;
    if (model->overlay == PTC_UI_OVERLAY_PIN) {
        ptc_ui_pin_finish(model);
    } else if (model->overlay == PTC_UI_OVERLAY_NUMPAD ||
        model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
        ptc_ui_numpad_finish(model);
    } else if (model->overlay == PTC_UI_OVERLAY_CONFIRM &&
               model->confirm_return_overlay != PTC_UI_OVERLAY_NONE) {
        model->overlay = model->confirm_return_overlay;
        model->confirm_return_overlay = PTC_UI_OVERLAY_NONE;
        snprintf(model->overlay_title, sizeof(model->overlay_title), "%s", model->confirm_return_title);
        snprintf(model->overlay_body, sizeof(model->overlay_body), "%s", model->confirm_return_body);
        model->confirm_return_title[0] = '\0';
        model->confirm_return_body[0] = '\0';
        model->operation = PTC_UI_OPERATION_NONE;
    } else if (model->overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
        model->overlay = PTC_UI_OVERLAY_CREDENTIAL;
        snprintf(model->overlay_title, sizeof(model->overlay_title), "%s",
                 model->credential_kind == 1 ? "管理加时码设备名" : "管理加时码密钥");
        snprintf(model->overlay_body, sizeof(model->overlay_body), "%s",
                 model->credential_kind == 1
                    ? "当前值只读；可手工输入或随机生成新设备名。"
                    : "当前密钥默认遮挡；建议使用随机生成的 64 位十六进制密钥。");
    } else {
        model->overlay = PTC_UI_OVERLAY_NONE;
        model->confirm_return_overlay = PTC_UI_OVERLAY_NONE;
        model->confirm_return_title[0] = '\0';
        model->confirm_return_body[0] = '\0';
        model->operation = PTC_UI_OPERATION_NONE;
    }
    if (clear_pending_code) model->pending_code[0] = '\0';
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
    model->confirm_return_overlay = PTC_UI_OVERLAY_NONE;
    model->confirm_return_title[0] = '\0';
    model->confirm_return_body[0] = '\0';
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
    PtcUiRect rect = {736, 216, 464, 88};
    return rect;
}

PtcUiRect ptc_ui_child_refresh_rect(void)
{
    PtcUiRect rect = {1044, 660, 188, 48};
    return rect;
}

PtcUiRect ptc_ui_child_buffer_rect(void)
{
    PtcUiRect rect = {736, 328, 464, 64};
    return rect;
}

PtcUiRect ptc_ui_child_footer_rect(int index)
{
    static const int widths[] = {250, 500, 180};
    static const int xs[] = {54, 322, 840};
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

PtcUiRect ptc_ui_setup_theme_rect(int index)
{
    PtcUiRect rect = {204 + index * 292, 270, 268, 132};
    if (index < 0 || index >= 3) return (PtcUiRect){0, 0, 0, 0};
    return rect;
}

PtcUiRect ptc_ui_notice_status_icon_rect(int y)
{
    return (PtcUiRect){74, y + 9, 20, 20};
}

PtcUiRect ptc_ui_notice_command_text_rect(int y, int height)
{
    bool compact = height < 128;
    return (PtcUiRect){78, y + (compact ? 38 : 43), 1124, compact ? 20 : 22};
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
    static const int widths[] = {130, 130, 170, 130, 564};
    static const int xs[] = {54, 196, 338, 520, 662};
    PtcUiRect rect = {0, 660, 0, 48};
    if (index >= 0 && index < 5) {
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
    PtcUiRect rect = {54 + index * 174, 108, 158, 48};
    return rect;
}

PtcUiRect ptc_ui_parent_card_rect(int index)
{
    if (index == 6) return (PtcUiRect){842, 408, 384, 94};
    int column = index % 2;
    int row = index / 2;
    PtcUiRect rect = {54 + column * 385, 176 + row * 110, 365, 94};
    return rect;
}

void ptc_ui_format_home_remaining(const PtcUiModel *model, int64_t now, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || !model->status_loaded) snprintf(out, out_size, "等待刷新");
    else if (!ptc_ui_status_is_fresh(model, now))
        snprintf(out, out_size, "状态待确认");
    else ptc_ui_format_quota_remaining(model, out, out_size);
}

void ptc_ui_format_home_total(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    /* Only the backend's current-day forecast supplies the displayed total;
       do not reconstruct it from remaining time or consumption estimates. */
    if (!model || !model->status_loaded) snprintf(out, out_size, "今日总额度  待刷新");
    else if (model->unrestricted_today == 1) snprintf(out, out_size, "今日总额度  不限时");
    else if (model->forecast_available && model->forecast[0].day_index == model->day_index &&
             model->forecast[0].mode == PTC_RULE_MODE_LIMIT)
        snprintf(out, out_size, "今日总额度  %u 分钟", (unsigned int)model->forecast[0].minutes);
    else snprintf(out, out_size, "今日总额度  暂不可用");
}

PtcUiRect ptc_ui_home_summary_rect(bool parent)
{
    return parent ? (PtcUiRect){48, 176, 488, 328} : (PtcUiRect){48, 120, 632, 384};
}

PtcUiRect ptc_ui_today_card_rect(int index)
{
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){560 + (index % 2) * 348, 176 + (index / 2) * 136, 324, 112};
}

PtcUiRect ptc_ui_home_details_rect(bool parent)
{
    return parent ? (PtcUiRect){1008, 456, 224, 48} : (PtcUiRect){736, 424, 464, 48};
}

PtcUiOperation ptc_ui_today_operation(int index)
{
    /* UI order is independent of the operation enum and request dispatch. */
    static const PtcUiOperation actions[] = {
        PTC_UI_OPERATION_SET_TODAY_LIMIT, PTC_UI_OPERATION_ADD_TODAY_MINUTES,
        PTC_UI_OPERATION_DISABLE_TODAY_LIMIT, PTC_UI_OPERATION_RESTORE_TODAY_POLICY
    };
    return index >= 0 && index < 4 ? actions[index] : PTC_UI_OPERATION_NONE;
}

bool ptc_ui_open_home_details(PtcUiModel *model)
{
    if (!model || model->waiting || model->overlay != PTC_UI_OVERLAY_NONE ||
        (model->view != PTC_UI_CHILD &&
         !(model->view == PTC_UI_PARENT && model->parent_page == PTC_UI_PARENT_TODAY))) return false;
    /* Keep the underlying focus and execution message intact on open/close. */
    model->overlay = PTC_UI_OVERLAY_HOME_DETAILS;
    snprintf(model->overlay_title, sizeof(model->overlay_title), "%s",
        model->view == PTC_UI_CHILD ? "使用详情" : "今日详情");
    model->overlay_body[0] = '\0';
    return true;
}

bool ptc_ui_home_notice_expanded(const PtcUiModel *model)
{
    return model && (strcmp(model->result_status, "error") == 0 ||
        model->disable_flag_present || model->recovery_active ||
        model->apply_pending_confirmation || model->restricted_now == 1 ||
        (model->remaining_available && model->remaining_minutes == 0 && model->unrestricted_today != 1) ||
        (model->temporary_unlocked_available && model->temporary_unlocked) ||
        (model->restriction_enabled_available && !model->restriction_enabled) ||
        strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0);
}

PtcUiRect ptc_ui_advanced_hierarchy_rect(void)
{
    return (PtcUiRect){54, 172, 1172, 56};
}

PtcUiRect ptc_ui_advanced_back_rect(void)
{
    return (PtcUiRect){1038, 180, 170, 40};
}

PtcUiRect ptc_ui_advanced_card_rect(void)
{
    return (PtcUiRect){54, 246, 365, 94};
}

PtcUiRect ptc_ui_advanced_feature_rect(int index)
{
    int column = index % 2;
    int row = index / 2;
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){54 + column * 385, 246 + row * 110, 365, 94};
}

PtcUiRect ptc_ui_support_hierarchy_rect(void)
{
    return (PtcUiRect){54, 172, 760, 56};
}

PtcUiRect ptc_ui_support_back_rect(void)
{
    return (PtcUiRect){644, 180, 152, 40};
}

PtcUiRect ptc_ui_support_card_rect(int index)
{
    int column;
    int row;
    if (index < 0 || index >= 6) return (PtcUiRect){0, 0, 0, 0};
    column = index % 2;
    row = index / 2;
    return (PtcUiRect){54 + column * 385, 240 + row * 88, 365, 82};
}

PtcUiRect ptc_ui_holiday_card_rect(int index)
{
    switch (index) {
    case 0: return (PtcUiRect){54, 176, 760, 72};
    case 1: return (PtcUiRect){54, 260, 368, 168};
    case 2: return (PtcUiRect){446, 260, 368, 168};
    case 3: return (PtcUiRect){54, 440, 240, 60};
    case 4: return (PtcUiRect){310, 440, 240, 60};
    case 5: return (PtcUiRect){566, 440, 248, 60};
    default: return (PtcUiRect){0, 0, 0, 0};
    }
}

PtcUiRect ptc_ui_holiday_calendar_rect(void)
{
    return (PtcUiRect){862, 346, 336, 54};
}

PtcUiRect ptc_ui_holiday_page_action_rect(int index)
{
    PtcUiRect rect = {280 + index * 250, 576, 230, 50};
    if (index < 0 || index >= 3) return (PtcUiRect){0, 0, 0, 0};
    return rect;
}

PtcUiRect ptc_ui_holiday_enable_rect(void)
{
    return (PtcUiRect){722, 194, 76, 36};
}

PtcUiRect ptc_ui_holiday_mode_rect(int index)
{
    PtcUiRect card = ptc_ui_holiday_card_rect(index + 1);
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){card.x + card.w - 92, card.y + 12, 80, 36};
}

PtcUiRect ptc_ui_holiday_minutes_rect(int index)
{
    PtcUiRect card = ptc_ui_holiday_card_rect(index + 1);
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){card.x + 12, card.y + 80, card.w - 24, 76};
}

uint16_t ptc_ui_today_limit_start_value(const PtcUiModel *model, uint16_t fallback)
{
    int played;
    if (model && model->played_minutes_available && model->played_minutes >= 0) {
        played = model->played_minutes;
        if (played < 1) played = 1;
        if (played > 1440) played = 1440;
        if (fallback >= 1 && fallback <= 1440 && fallback > played) return fallback;
        return (uint16_t)played;
    }
    if (fallback >= 1 && fallback <= 1440) return fallback;
    return 60;
}

PtcUiRect ptc_ui_support_event_rect(int index)
{
    if (index < 0 || index >= 3) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){868, 427 + index * 22, 332, 20};
}

PtcUiRect ptc_ui_dialog_rect(int width, int height)
{
    PtcUiRect rect = {(PTC_UI_SCREEN_W - width) / 2, (PTC_UI_SCREEN_H - height) / 2 - 10, width, height};
    return rect;
}

static void dialog_dims(PtcUiOverlay overlay, int *width, int *height)
{
    switch (overlay) {
    case PTC_UI_OVERLAY_HOME_DETAILS:
        *width = 1120;
        *height = 640;
        break;
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
    case PTC_UI_OVERLAY_GRANT_MANAGER:
        *width = 1120;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_REDEMPTION_HISTORY:
        *width = 1120;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_ACTIVITY_HISTORY:
        *width = 1120;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_SCHEDULED:
        *width = 960;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_AUTONOMY:
        *width = 760;
        *height = 420;
        break;
    case PTC_UI_OVERLAY_QR:
        *width = 1120;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
        *width = 1120;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_GRANT_LOCAL:
        *width = 920;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
        *width = 860;
        *height = 350;
        break;
    case PTC_UI_OVERLAY_CREDENTIAL_LEAVE:
        *width = 720;
        *height = 300;
        break;
    case PTC_UI_OVERLAY_CODE_RESULT:
        *width = 760;
        *height = 420;
        break;
    case PTC_UI_OVERLAY_AUTH_ERROR:
        *width = 720;
        *height = 340;
        break;
    case PTC_UI_OVERLAY_SOFTWARE_INFO:
        *width = 960;
        *height = 480;
        break;
    case PTC_UI_OVERLAY_HOLIDAY_CALENDAR:
        *width = 1040;
        *height = 600;
        break;
    case PTC_UI_OVERLAY_MINUTE_EDITOR:
        *width = 920;
        *height = 620;
        break;
    case PTC_UI_OVERLAY_HOLIDAY_LEAVE:
        *width = 720;
        *height = 320;
        break;
    case PTC_UI_OVERLAY_SUPPORT_EVENT:
        *width = 960;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_WEEKLY_BULK:
        *width = 1040;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_ALBUM_MANAGER:
        *width = 980;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_THEME:
        *width = 820;
        *height = 360;
        break;
    case PTC_UI_OVERLAY_PIN:
        *width = 1040;
        *height = 620;
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

PtcUiRect ptc_ui_redemption_history_prev_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_REDEMPTION_HISTORY);
    return (PtcUiRect){dialog.x + 24, dialog.y + dialog.h - PTC_UI_DIALOG_BTN_H - 24,
        160, PTC_UI_DIALOG_BTN_H};
}

PtcUiRect ptc_ui_redemption_history_next_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_REDEMPTION_HISTORY);
    return (PtcUiRect){dialog.x + 200, dialog.y + dialog.h - PTC_UI_DIALOG_BTN_H - 24,
        160, PTC_UI_DIALOG_BTN_H};
}

PtcUiRect ptc_ui_scheduled_field_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_SCHEDULED);
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 48, dialog.y + 104 + index * 72, dialog.w - 96, 58};
}

PtcUiRect ptc_ui_autonomy_option_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_AUTONOMY);
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 48 + index * 164, dialog.y + 166, 148, 92};
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
    PtcUiRect rect = {54 + index * 108, 218, 96, 200};
    if (index < 0 || index >= 7) rect = (PtcUiRect){0, 0, 0, 0};
    return rect;
}

PtcUiRect ptc_ui_weekly_day_header_rect(int index)
{
    PtcUiRect card = ptc_ui_weekly_day_rect(index);
    return (PtcUiRect){card.x, card.y, card.w, 42};
}

PtcUiRect ptc_ui_weekly_day_mode_rect(int index)
{
    PtcUiRect card = ptc_ui_weekly_day_rect(index);
    return (PtcUiRect){card.x + 4, card.y + 42, card.w - 8, 50};
}

PtcUiRect ptc_ui_weekly_day_minutes_rect(int index)
{
    PtcUiRect card = ptc_ui_weekly_day_rect(index);
    PtcUiRect rect = {card.x + 4, card.y + 92, card.w - 8, 108};
    return rect;
}

PtcUiRect ptc_ui_weekly_bulk_rect(void)
{
    return (PtcUiRect){242, 438, 176, 62};
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

PtcUiRect ptc_ui_pin_dialog_rect(void)
{
    return dialog_for(PTC_UI_OVERLAY_PIN);
}

PtcUiRect ptc_ui_pin_key_rect(int digit)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_PIN);
    int row;
    int column;
    PtcUiRect rect;
    if (digit < 0 || digit > 9) return (PtcUiRect){0, 0, 0, 0};
    row = digit == 0 ? 3 : (digit - 1) / 3;
    column = digit == 0 ? 1 : (digit - 1) % 3;
    rect = (PtcUiRect){dialog.x + 590 + column * 112, dialog.y + 220 + row * 58, 100, 52};
    return rect;
}

PtcUiRect ptc_ui_pin_backspace_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_PIN);
    return (PtcUiRect){dialog.x + 590, dialog.y + 458, 210, 48};
}

PtcUiRect ptc_ui_pin_confirm_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_PIN);
    return (PtcUiRect){dialog.x + 814, dialog.y + 458, 190, 48};
}

PtcUiRect ptc_ui_pin_cancel_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_PIN);
    return (PtcUiRect){dialog.x + 590, dialog.y + 516, 190, 48};
}

PtcUiRect ptc_ui_pin_keyboard_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_PIN);
    return (PtcUiRect){dialog.x + 794, dialog.y + 516, 210, 48};
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
    if (overlay == PTC_UI_OVERLAY_GRANT_LOCAL)
        return (PtcUiRect){dialog.x + 42, dialog.y + 588, 210, 44};
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
    PtcUiRect rect = {618, 438, 196, 62};
    return rect;
}

PtcUiRect ptc_ui_weekly_discard_rect(void)
{
    PtcUiRect rect = {430, 438, 176, 62};
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

PtcUiRect ptc_ui_grant_manager_card_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_GRANT_MANAGER);
    int column = index % 2;
    int row = index / 2;
    PtcUiRect rect = {dialog.x + 34 + column * 530, dialog.y + 126 + row * 116, 510, 100};
    if (index < 0 || index >= PTC_UI_GRANT_MANAGER_COUNT) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

PtcUiRect ptc_ui_grant_generate_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_GRANT_LOCAL);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 530, dialog.w - 84, 46};
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

void ptc_ui_move_weekly_focus(PtcUiModel *model, int horizontal, int vertical)
{
    int slot;
    if (!model) return;
    if (model->selected_index < 0 || model->selected_index > 4) model->selected_index = 0;
    if (model->selected_index != 0) {
        if (vertical < 0) {
            model->selected_index = 0;
        } else if (horizontal < 0 && model->selected_index > 1) {
            --model->selected_index;
        } else if (horizontal > 0 && model->selected_index < 4) {
            ++model->selected_index;
        }
        return;
    }
    slot = model->weekly_grid_slot;
    if (slot < 0 || slot > 6) slot = 0;
    if (horizontal < 0 && slot > 0) --slot;
    else if (horizontal > 0 && slot < 6) ++slot;
    else if (vertical > 0) {
        model->selected_index = 1;
        return;
    }
    model->weekly_grid_slot = slot;
    model->weekly_last_day_slot = slot;
    model->editor_index = ptc_ui_weekday_for_display_slot(slot);
}

PtcUiRect ptc_ui_minute_editor_key_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTE_EDITOR);
    int row = index / 3;
    int column = index % 3;
    PtcUiRect rect = {dialog.x + 34 + column * 138, dialog.y + 214 + row * 66, 126, 54};
    if (index < 0 || index >= 12) return (PtcUiRect){0, 0, 0, 0};
    return rect;
}

PtcUiRect ptc_ui_minute_editor_quick_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTE_EDITOR);
    PtcUiRect rect = {dialog.x + 34 + index * 108, dialog.y + 154, 100, 48};
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    return rect;
}

PtcUiRect ptc_ui_minute_editor_field_rect(PtcUiDurationField field)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_MINUTE_EDITOR);
    if (field == PTC_UI_DURATION_HOURS) return (PtcUiRect){dialog.x + 536, dialog.y + 146, 166, 68};
    if (field == PTC_UI_DURATION_MINUTES) return (PtcUiRect){dialog.x + 720, dialog.y + 146, 166, 68};
    return (PtcUiRect){0, 0, 0, 0};
}

bool ptc_ui_apply_weekly_bulk(PtcUiModel *model, bool weekend)
{
    PtcDayRule source;
    PtcDayRule before[7];
    int slot;
    if (!model || model->disable_flag_present) return false;
    slot = model->weekly_last_day_slot;
    if (slot < 0 || slot >= 7) slot = 0;
    source = model->draft_week[ptc_ui_weekday_for_display_slot(slot)];
    memcpy(before, model->draft_week, sizeof(before));
    if (weekend) {
        model->draft_week[6] = source;
        model->draft_week[0] = source;
    } else {
        for (int day = 1; day <= 5; ++day) model->draft_week[day] = source;
    }
    model->weekly_dirty = memcmp(model->draft_week, model->current_week, sizeof(model->draft_week)) != 0;
    return memcmp(before, model->draft_week, sizeof(before)) != 0;
}

void ptc_ui_weekly_bulk_stats(const PtcUiModel *model, bool weekend, PtcUiWeeklyBulkStats *stats)
{
    int slot;
    PtcDayRule source;
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    if (!model) return;
    slot = model->weekly_last_day_slot;
    if (slot < 0 || slot >= 7) slot = 0;
    source = model->draft_week[ptc_ui_weekday_for_display_slot(slot)];
    for (int day = 0; day < 7; ++day) {
        bool included = weekend ? (day == 0 || day == 6) : (day >= 1 && day <= 5);
        int group = -1;
        if (!included) continue;
        ++stats->target_count;
        if (ptc_ui_day_rule_effectively_changed(model->draft_week[day], source)) ++stats->changed_count;
        else ++stats->unchanged_count;
        for (int index = 0; index < stats->rule_group_count; ++index) {
            if (!ptc_ui_day_rule_effectively_changed(stats->rule_groups[index].rule, model->draft_week[day])) {
                group = index;
                break;
            }
        }
        if (group < 0 && stats->rule_group_count < 5) {
            group = stats->rule_group_count++;
            stats->rule_groups[group].rule = model->draft_week[day];
        }
        if (group >= 0) ++stats->rule_groups[group].count;
    }
}

static int credential_selection_step(const PtcUiModel *model, int current, int direction)
{
    static const int DEVICE_ITEMS[] = {
        PTC_UI_CREDENTIAL_INPUT, PTC_UI_CREDENTIAL_RANDOM, PTC_UI_CREDENTIAL_SAVE
    };
    static const int SECRET_ITEMS[] = {
        PTC_UI_CREDENTIAL_INPUT, PTC_UI_CREDENTIAL_RANDOM, PTC_UI_CREDENTIAL_REVEAL,
        PTC_UI_CREDENTIAL_DEMO, PTC_UI_CREDENTIAL_SAVE
    };
    const int *items = model->credential_kind == 2 ? SECRET_ITEMS : DEVICE_ITEMS;
    int count = model->credential_kind == 2 ? 5 : 3;
    int index;
    for (index = 0; index < count; ++index) {
        if (items[index] == current) break;
    }
    if (index >= count) index = 0;
    if (direction < 0 && index > 0) --index;
    if (direction > 0 && index + 1 < count) ++index;
    return items[index];
}

void ptc_ui_move_overlay_selection(PtcUiModel *model, int horizontal, int vertical)
{
    int direction;
    if (!model) return;
    direction = horizontal != 0 ? horizontal : vertical;
    if (direction == 0) return;
    if (model->overlay == PTC_UI_OVERLAY_CREDENTIAL) {
        model->overlay_selection = credential_selection_step(model, model->overlay_selection, direction);
    } else if (model->overlay == PTC_UI_OVERLAY_GRANT_MANAGER) {
        int index = model->overlay_selection;
        int column;
        int row;
        if (index < 0 || index >= PTC_UI_GRANT_MANAGER_COUNT) index = 0;
        column = index % 2;
        row = index / 2;
        if (horizontal < 0 && column > 0) --index;
        else if (horizontal > 0 && column == 0 && index + 1 < PTC_UI_GRANT_MANAGER_COUNT) ++index;
        if (vertical != 0) {
            row = index / 2;
            column = index % 2;
            row = (row + (vertical > 0 ? 1 : 2)) % 3;
            index = row * 2 + column;
            if (index >= PTC_UI_GRANT_MANAGER_COUNT) index = row * 2;
        }
        model->overlay_selection = index;
    } else if (model->overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
        int selection = model->overlay_selection;
        if (selection < PTC_UI_GRANT_LOCAL_ADJUST_FIRST || selection > PTC_UI_GRANT_LOCAL_BACK) {
            selection = PTC_UI_GRANT_LOCAL_GENERATE;
        }
        if (selection <= PTC_UI_GRANT_LOCAL_ADJUST_LAST) {
            if (horizontal < 0 && selection > PTC_UI_GRANT_LOCAL_ADJUST_FIRST) --selection;
            else if (horizontal > 0 && selection < PTC_UI_GRANT_LOCAL_ADJUST_LAST) ++selection;
            else if (vertical > 0) selection = PTC_UI_GRANT_LOCAL_GENERATE;
        } else if (selection == PTC_UI_GRANT_LOCAL_GENERATE) {
            if (vertical < 0) selection = PTC_UI_GRANT_LOCAL_ADJUST_FIRST;
            else if (vertical > 0) selection = PTC_UI_GRANT_LOCAL_BACK;
        } else if (vertical < 0) {
            selection = PTC_UI_GRANT_LOCAL_GENERATE;
        }
        model->overlay_selection = selection;
    }
}

int ptc_ui_grant_estimate_remaining(const PtcUiModel *model, uint16_t grant_minutes, bool *capped)
{
    int maximum_remaining;
    int estimate;
    if (capped) *capped = false;
    if (!model || model->grant_status_refresh_failed || !model->played_minutes_available ||
        model->played_minutes < 0) {
        return -1;
    }
    if (model->unrestricted_today == 1) return (int)grant_minutes;
    if (!model->remaining_available || model->remaining_minutes < 0) return -1;
    maximum_remaining = 1440 - model->played_minutes;
    if (maximum_remaining < 0) maximum_remaining = 0;
    estimate = model->remaining_minutes + (int)grant_minutes;
    if (estimate > maximum_remaining) {
        estimate = maximum_remaining;
        if (capped) *capped = true;
    }
    return estimate;
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

PtcUiRect ptc_ui_shortcut_disable_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_SHORTCUT_MANAGER);
    PtcUiRect rect = {dialog.x + 120, dialog.y + 430, 400, 46};
    return rect;
}

PtcUiRect ptc_ui_shortcut_hint_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_SHORTCUT_MANAGER);
    PtcUiRect rect = {dialog.x + 600, dialog.y + 430, 400, 46};
    return rect;
}

PtcUiRect ptc_ui_grant_adjust_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_GRANT_LOCAL);
    PtcUiRect rect = {dialog.x + 42 + index * 139, dialog.y + 238, 126, 50};
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

static bool rect_contains_row_cell(PtcUiRect rect, int cell_height, int x, int y)
{
    if (cell_height > rect.h) rect.h = cell_height;
    return ptc_ui_rect_contains(rect, x, y);
}

static bool rect_contains_with_padding(PtcUiRect rect, int padding, int x, int y)
{
    rect.x -= padding;
    rect.y -= padding;
    rect.w += padding * 2;
    rect.h += padding * 2;
    return ptc_ui_rect_contains(rect, x, y);
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
    if (model->overlay == PTC_UI_OVERLAY_HOLIDAY_CALENDAR) {
        for (i = 0; i < 3; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_holiday_page_action_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_HOLIDAY_PAGE_ACTION, i);
            }
        }
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->overlay == PTC_UI_OVERLAY_HOME_DETAILS) {
        return ptc_ui_rect_contains(ptc_ui_cancel_rect(model->overlay), x, y)
            ? make_hit(PTC_UI_HIT_OVERLAY_CANCEL, 0) : make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->overlay == PTC_UI_OVERLAY_SOFTWARE_INFO) {
        return ptc_ui_rect_contains(ptc_ui_confirm_rect(model->overlay), x, y)
            ? make_hit(PTC_UI_HIT_OVERLAY_CONFIRM, 0)
            : make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->overlay != PTC_UI_OVERLAY_ALBUM_MANAGER &&
        ptc_ui_rect_contains(ptc_ui_confirm_rect(model->overlay), x, y)) {
        return make_hit(PTC_UI_HIT_OVERLAY_CONFIRM, 0);
    }
    if (model->overlay != PTC_UI_OVERLAY_CREDENTIAL_LEAVE &&
        ptc_ui_rect_contains(ptc_ui_cancel_rect(model->overlay), x, y)) {
        return make_hit(PTC_UI_HIT_OVERLAY_CANCEL, 0);
    }
    if ((model->overlay == PTC_UI_OVERLAY_WEEKLY_LEAVE ||
         model->overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE ||
         model->overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) &&
        ptc_ui_rect_contains(ptc_ui_discard_rect(model->overlay), x, y)) {
        return make_hit(PTC_UI_HIT_OVERLAY_DISCARD, 0);
    }
    switch (model->overlay) {
    case PTC_UI_OVERLAY_REDEMPTION_HISTORY:
    case PTC_UI_OVERLAY_ACTIVITY_HISTORY:
        if (ptc_ui_rect_contains(ptc_ui_redemption_history_prev_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_HISTORY_PREV, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_redemption_history_next_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_HISTORY_NEXT, 0);
        }
        break;
    case PTC_UI_OVERLAY_SCHEDULED:
        for (i = 0; i < 4; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_scheduled_field_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_SCHEDULED_FIELD, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_AUTONOMY:
        for (i = 0; i < 4; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_autonomy_option_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_AUTONOMY_OPTION, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_MINUTE_EDITOR:
        for (i = 0; i < 2; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_minute_editor_field_rect((PtcUiDurationField)i), x, y)) {
                return make_hit(PTC_UI_HIT_DURATION_FIELD, i);
            }
        }
        for (i = 0; i < 4; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_minute_editor_quick_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_NUMPAD_QUICK, i);
            }
        }
        for (i = 0; i < 12; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_minute_editor_key_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_NUMPAD_KEY, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_PIN:
        for (i = 0; i < 10; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_pin_key_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_PIN_KEY, i);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_pin_backspace_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_PIN_BACKSPACE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_pin_confirm_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_PIN_CONFIRM, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_pin_cancel_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_PIN_CANCEL, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_pin_keyboard_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_PIN_KEYBOARD, 0);
        }
        break;
    case PTC_UI_OVERLAY_WEEKLY_BULK:
        for (i = 0; i < 2; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_weekly_bulk_target_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_BULK_TARGET, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_ALBUM_MANAGER:
        if (ptc_ui_rect_contains(ptc_ui_album_refresh_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_ALBUM_REFRESH, 0);
        }
        for (i = 0; i < 2; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_album_action_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_ALBUM_ACTION, i);
            }
        }
        break;
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
            model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES ||
            model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
            model->numpad_purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) {
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
        if (model->credential_kind == 2 && ptc_ui_rect_contains(ptc_ui_credential_reveal_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CREDENTIAL_REVEAL, 0);
        }
        if (model->credential_kind == 2 && ptc_ui_rect_contains(ptc_ui_credential_demo_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CREDENTIAL_DEMO, 0);
        }
        break;
    case PTC_UI_OVERLAY_GRANT_MANAGER:
        for (i = 0; i < PTC_UI_GRANT_MANAGER_COUNT; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_grant_manager_card_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_GRANT_MANAGER_CARD, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_GRANT_LOCAL:
        for (i = 0; i < 6; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_grant_adjust_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_GRANT_ADJUST, i);
            }
        }
        if (!model->waiting && ptc_ui_rect_contains(ptc_ui_grant_generate_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_GRANT_GENERATE, 0);
        }
        break;
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
        for (i = 0; i < PTC_UI_SHORTCUT_PRESET_COUNT; ++i) {
            if (rect_contains_row_cell(ptc_ui_shortcut_option_rect(i), 40, x, y)) {
                return make_hit(PTC_UI_HIT_SHORTCUT_OPTION, i);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_shortcut_disable_rect(), x, y)) return make_hit(PTC_UI_HIT_SHORTCUT_DISABLE, 0);
        if (ptc_ui_rect_contains(ptc_ui_shortcut_hint_rect(), x, y)) return make_hit(PTC_UI_HIT_SHORTCUT_HINT, 0);
        break;
    case PTC_UI_OVERLAY_THEME:
        for (i = 0; i < 3; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_theme_option_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_THEME_OPTION, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_QR:
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
    case PTC_UI_OVERLAY_CREDENTIAL_LEAVE:
    case PTC_UI_OVERLAY_CODE_RESULT:
    case PTC_UI_OVERLAY_AUTH_ERROR:
    case PTC_UI_OVERLAY_SOFTWARE_INFO:
    case PTC_UI_OVERLAY_HOLIDAY_LEAVE:
    case PTC_UI_OVERLAY_SUPPORT_EVENT:
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
    if ((model->view == PTC_UI_CHILD ||
         (model->view == PTC_UI_PARENT && model->parent_page == PTC_UI_PARENT_TODAY)) &&
        ptc_ui_rect_contains(ptc_ui_home_details_rect(model->view == PTC_UI_PARENT), x, y)) {
        return model->waiting ? make_hit(PTC_UI_HIT_NONE, 0) : make_hit(PTC_UI_HIT_HOME_DETAILS, 0);
    }
    if (model->view == PTC_UI_CHILD) {
        if (!model->disable_flag_present && !model->waiting && ptc_ui_rect_contains(ptc_ui_child_submit_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_SUBMIT_CODE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_child_refresh_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_REFRESH, 0);
        }
        if (model->daily_buffer_available && !model->disable_flag_present && !model->waiting &&
            ptc_ui_rect_contains(ptc_ui_child_buffer_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_BUFFER, 0);
        }
        if (!model->disable_flag_present && !model->waiting && ptc_ui_rect_contains(ptc_ui_child_footer_rect(0), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_SUBMIT_CODE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_child_footer_rect(1), x, y)) {
            if (model->show_parent_shortcut_hint && model->custom_shortcut_enabled) {
                return make_hit(PTC_UI_HIT_CHILD_PARENT, 0);
            }
            return make_hit(PTC_UI_HIT_NONE, 0);
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
                if (rect_contains_row_cell(ptc_ui_setup_shortcut_card_rect(i), 34, x, y)) {
                    return make_hit(PTC_UI_HIT_SETUP_SHORTCUT_CARD, i);
                }
            }
        } else if (step == PTC_UI_SETUP_PIN &&
                   ptc_ui_rect_contains(ptc_ui_setup_pin_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_SETUP_PIN, 0);
        } else if (step == PTC_UI_SETUP_THEME) {
            for (i = 0; i < 3; ++i) {
                if (ptc_ui_rect_contains(ptc_ui_setup_theme_rect(i), x, y)) {
                    return make_hit(PTC_UI_HIT_SETUP_THEME_OPTION, i);
                }
            }
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
    if (!(model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page != PTC_UI_SETTINGS_ROOT)) {
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
    }
    if (model->parent_page == PTC_UI_PARENT_SETTINGS &&
        ((model->settings_page == PTC_UI_SETTINGS_ADVANCED &&
          ptc_ui_rect_contains(ptc_ui_advanced_back_rect(), x, y)) ||
         (model->settings_page == PTC_UI_SETTINGS_SUPPORT &&
          ptc_ui_rect_contains(ptc_ui_support_back_rect(), x, y)))) {
        return make_hit(PTC_UI_HIT_PARENT_BACK, 0);
    }
    if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(2), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_BACK, 0);
    }
    if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(3), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_REFRESH, 0);
    }
    if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(4), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_STATUS, 0);
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN) {
        for (i = 0; i < 7; ++i) {
            int weekday = ptc_ui_weekday_for_display_slot(i);
            if (ptc_ui_rect_contains(ptc_ui_weekly_day_mode_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_MODE, weekday);
            }
            if (ptc_ui_rect_contains(ptc_ui_weekly_day_minutes_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_MIN_INPUT, weekday);
            }
            if (ptc_ui_rect_contains(ptc_ui_weekly_day_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_DAY, weekday);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_page_mode_rect(), x, y)) return make_hit(PTC_UI_HIT_WEEKLY_MODE, model->editor_index);
        if (ptc_ui_rect_contains(ptc_ui_weekly_save_rect(), x, y)) return make_hit(PTC_UI_HIT_WEEKLY_SAVE, 0);
        if (ptc_ui_rect_contains(ptc_ui_weekly_discard_rect(), x, y)) return make_hit(PTC_UI_HIT_WEEKLY_DISCARD, 0);
        if (ptc_ui_rect_contains(ptc_ui_weekly_bulk_rect(), x, y)) return make_hit(PTC_UI_HIT_WEEKLY_BULK, 0);
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->parent_page == PTC_UI_PARENT_HOLIDAY) {
        if (rect_contains_with_padding(ptc_ui_holiday_enable_rect(), 4, x, y)) {
            return make_hit(PTC_UI_HIT_HOLIDAY_ENABLE, 0);
        }
        for (i = 0; i < 2; ++i) {
            if (rect_contains_with_padding(ptc_ui_holiday_mode_rect(i), 4, x, y)) {
                return make_hit(PTC_UI_HIT_HOLIDAY_MODE, i);
            }
            if (ptc_ui_rect_contains(ptc_ui_holiday_minutes_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_HOLIDAY_MINUTES, i);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_holiday_calendar_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_HOLIDAY_CALENDAR, 0);
        }
        for (i = 0; i < 6; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_holiday_card_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_PARENT_CARD, i);
            }
        }
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT) {
        for (i = 0; i < model->recent_event_count; ++i) {
            if (rect_contains_row_cell(ptc_ui_support_event_rect(i), 22, x, y)) {
                return make_hit(PTC_UI_HIT_SUPPORT_EVENT, model->recent_event_count - 1 - i);
            }
        }
    }
    count = model->parent_page == PTC_UI_PARENT_SETTINGS
        ? (model->settings_page == PTC_UI_SETTINGS_SUPPORT ? 6 :
           model->settings_page == PTC_UI_SETTINGS_ADVANCED ? 4 : ptc_ui_parent_action_count(model->parent_page))
        : ptc_ui_parent_action_count(model->parent_page);
    for (i = 0; i < count; ++i) {
        PtcUiRect card_rect = model->parent_page == PTC_UI_PARENT_SETTINGS
            ? (model->settings_page == PTC_UI_SETTINGS_ADVANCED
                ? ptc_ui_advanced_feature_rect(i)
                : (model->settings_page == PTC_UI_SETTINGS_SUPPORT
                    ? ptc_ui_support_card_rect(i) : ptc_ui_parent_card_rect(i)))
            : (model->parent_page == PTC_UI_PARENT_TODAY ? ptc_ui_today_card_rect(i) : ptc_ui_parent_card_rect(i));
        if (model->parent_page == PTC_UI_PARENT_TODAY && (model->disable_flag_present || model->waiting)) continue;
        if ((model->parent_page != PTC_UI_PARENT_SETTINGS || model->settings_page != PTC_UI_SETTINGS_SUPPORT ||
             (ptc_ui_safety_action_visible(model, i) &&
              ptc_ui_safety_action_available(model, i) != PTC_UI_ACTION_DISABLED)) &&
            ptc_ui_rect_contains(card_rect, x, y)) {
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
        return ptc_ui_runtime_fingerprint_reconfirmation_needed(model) || model->disable_flag_present ||
            (strcmp(model->setup_phase, "active") != 0 && strcmp(model->setup_phase, "protection") != 0 &&
             strcmp(model->setup_phase, "failed") != 0)
            ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_DISABLED;
    case 1:
        return strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0 ||
            strcmp(model->setup_phase, "pending") == 0 ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_DISABLED;
    case 2:
        return !model->disable_flag_present && strcmp(model->setup_phase, "protection") != 0 &&
            strcmp(model->setup_phase, "restored") != 0
            ? PTC_UI_ACTION_AVAILABLE : PTC_UI_ACTION_DISABLED;
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

PtcUiRect ptc_ui_weekly_page_mode_rect(void)
{
    PtcUiRect rect = {54, 438, 176, 62};
    return rect;
}

PtcUiRect ptc_ui_weekly_bulk_target_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_WEEKLY_BULK);
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 40, dialog.y + 184 + index * 102, 410, 88};
}

PtcUiRect ptc_ui_album_action_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_ALBUM_MANAGER);
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 38 + index * 450, dialog.y + 178, 420, 224};
}

PtcUiRect ptc_ui_album_refresh_rect(void)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_ALBUM_MANAGER);
    return (PtcUiRect){dialog.x + dialog.w - 226, dialog.y + 92, 188, 46};
}

PtcUiRect ptc_ui_theme_option_rect(int index)
{
    PtcUiRect dialog = dialog_for(PTC_UI_OVERLAY_THEME);
    if (index < 0 || index >= 3) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 40 + index * 250, dialog.y + 154, 230, 100};
}

bool ptc_ui_safety_action_visible(const PtcUiModel *model, int index)
{
    if (!model) return false;
    return index >= 0 && index < 6;
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
        return strcmp(model->setup_phase, "active") == 0
            ? "当前运行正常，无需执行修复。"
            : "重新执行兼容、快照和恢复前置检查。";
    case 2:
        return model->disable_flag_present
            ? "解除停用后才允许新的控制写入；状态和恢复始终可用。"
            : "只停止新的控制写入；状态、诊断和恢复仍可使用。";
    case 3:
        return model->setup_snapshot_available ? "精确恢复安装前状态。" : "安装前快照不可用。";
    case 4:
        return "导出时自动排除 secret、PIN、离线码和完整 nonce。";
    case 5:
        return "查看 PlayWise 版本、项目仓库和家长网页地址。";
    default:
        return "";
    }
}
