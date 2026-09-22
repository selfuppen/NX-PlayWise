#include "ui_render_internal.h"

static const char *rule_mode_label(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_UNLIMITED:
        return "不限时";
    case PTC_RULE_MODE_LIMIT:
    default:
        return "限时";
    }
}

void draw_minutes_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect value_box = to_uirect(ptc_ui_minutes_value_rect());
    char value[32];
    char duration[64];
    char after_value[64];
    char played_line[64];
    char current_value[64];
    char date_line[64];
    char freshness[64];
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    int preview_min = ptc_ui_preview_remaining_minutes(model);
    int played_min = model->played_minutes_available ? model->played_minutes : -1;
    bool quota_unchanged = false;

    draw_dialog_shell(pixels, stride, model, &dialog, 720, 560);
    snprintf(value, sizeof(value), "%u 分钟", (unsigned int)model->draft_minutes);
    format_duration(model->draft_minutes, duration, sizeof(duration));
    fill_round_rect(pixels, stride, value_box, 16, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, value_box, 16, 2, UI_ACCENT);
    draw_text_center(pixels, stride, value_box, value, 39, UI_ACCENT);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_dec_rect(), "-5", UI_RAISED, UI_ACCENT, true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_inc_rect(), "+5", UI_RAISED, UI_ACCENT, true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_inc_large_rect(), "+15", UI_RAISED, UI_ACCENT, true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_dec_large_rect(), "-15", UI_RAISED, UI_ACCENT, true);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 242, 640, 26}, duration, 19,
                     UI_ACCENT);

    if (model->status_loaded && ptc_date_from_day_index(model->day_index, &year, &month, &day)) {
        snprintf(date_line, sizeof(date_line), "影响日期：%u 年 %u 月 %u 日（今天）", year, month, day);
    } else {
        snprintf(date_line, sizeof(date_line), "影响日期：今天");
    }
    if (played_min >= 0) {
        snprintf(played_line, sizeof(played_line), "额度已耗（估算）：约 %d 分钟", played_min);
    } else {
        snprintf(played_line, sizeof(played_line), "额度已耗（估算）：暂不可用");
    }
    if (model->unrestricted_today == 1) {
        snprintf(current_value, sizeof(current_value), "不限时");
    } else if (model->remaining_available && model->remaining_minutes >= 0) {
        format_duration(model->remaining_minutes, current_value, sizeof(current_value));
    } else {
        snprintf(current_value, sizeof(current_value), "暂不可用");
    }
    if (preview_min >= 0) {
        format_duration(preview_min, after_value, sizeof(after_value));
    } else if (model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        snprintf(after_value, sizeof(after_value), "暂不可用");
    } else {
        snprintf(after_value, sizeof(after_value), "保存后刷新确认");
    }
    format_status_age(model, freshness, sizeof(freshness));
    if (model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT) {
        PtcEffectiveRule current_rule = ptc_ui_plan_rule(model, PTC_UI_PLAN_SAVED);
        PtcDayRule requested_rule = {PTC_RULE_MODE_LIMIT, model->draft_minutes};
        quota_unchanged = !ptc_ui_day_rule_effectively_changed(current_rule.rule, requested_rule);
    } else if (model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        quota_unchanged = model->remaining_available && preview_min == model->remaining_minutes;
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 316, 640, 24}, date_line, 17, UI_MUTED);
    if (quota_unchanged) {
        draw_unchanged_quota_card(pixels, stride,
            (UiRect){dialog.x + 44, dialog.y + 344, 632, 74},
            model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES
                ? "已达到每日额度上限，本次加时不会增加今天额度。"
                : "输入值与当前今日额度相同。");
    } else {
        draw_remaining_transition(pixels, stride,
            (UiRect){dialog.x + 44, dialog.y + 344, 632, 74},
            "当前剩余", current_value,
            time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                              model->unrestricted_today == 1, model->remaining_minutes),
            "操作后剩余", after_value,
            time_state_accent(preview_min >= 0, false, preview_min));
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 424, 640, 22}, played_line, 16, UI_MUTED);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 448, 640, 22}, freshness, 16,
                     status_age_color(model));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 50, dialog.y + 472, 620, 22},
                     "本次修改只影响今天  |  Y 或点击数值手动输入", 16, UI_MUTED);
    draw_overlay_actions(pixels, stride, model, "A / +  提交并刷新");
}

void draw_weekly_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    UiRect dialog;
    int day;
    char selected_minutes[32];
    draw_dialog_shell(pixels, stride, model, &dialog, 1172, 560);
    for (day = 0; day < 7; ++day) {
        UiRect card = to_uirect(ptc_ui_weekly_day_rect(day));
        uint32_t border = day == model->editor_index ? UI_ACCENT : UI_BORDER;
        char minutes[32];
        fill_round_rect(pixels, stride, card, 16, day == model->editor_index ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, card, 16, day == model->editor_index ? 3 : 1, border);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 14, card.width, 34}, DAYS[day], 21, UI_INK);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 69, card.width, 34}, rule_mode_label(model->draft_week[day].mode), 22,
                         UI_ACCENT);
        if (model->draft_week[day].mode == PTC_RULE_MODE_LIMIT) {
            snprintf(minutes, sizeof(minutes), "%u 分钟", (unsigned int)model->draft_week[day].minutes);
        } else {
            snprintf(minutes, sizeof(minutes), "--");
        }
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 119, card.width, 34}, minutes, 19, UI_MUTED);
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 80, dialog.y + 320, dialog.width - 160, 30},
                     "选择日期后：X 切换模式，Y 或点数值手动输入", 19, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_weekly_mode_rect(), "X 切换模式", UI_RAISED, UI_ACCENT, true);
    if (model->draft_week[model->editor_index].mode == PTC_RULE_MODE_LIMIT) {
        snprintf(selected_minutes, sizeof(selected_minutes), "%u 分钟",
                 (unsigned int)model->draft_week[model->editor_index].minutes);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_up_rect(), "+15", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_down_rect(), "-15", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_dec_rect(), "-5", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_inc_rect(), "+5", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_input_rect(), selected_minutes,
                           UI_ACCENT_SOFT, UI_ACCENT, true);
    }
    draw_overlay_actions(pixels, stride, model, "A / +  保存并刷新");
}


static void format_plan_rule_value(PtcEffectiveRule rule, char *out, size_t out_size)
{
    if (rule.rule.mode == PTC_RULE_MODE_UNLIMITED)
        snprintf(out, out_size, "不限时");
    else
        snprintf(out, out_size, "%u 分钟", (unsigned int)rule.rule.minutes);
}

static void draw_plan_save_confirmation(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiPlanKind kind = model->operation == PTC_UI_OPERATION_SAVE_HOLIDAY
        ? PTC_UI_PLAN_HOLIDAY : PTC_UI_PLAN_WEEKLY;
    PtcUiPlanImpactProjection projection;
    PtcEffectiveRule before;
    PtcEffectiveRule after;
    PtcUiTodayDecision decision;
    char before_value[48], after_value[48], impact[256], change[96], remaining[48];
    int changed = 0;
    ptc_ui_project_plan_impact(model, kind, true, ptc_ui_render_now(), &projection);
    before = projection.before;
    after = projection.after;
    ptc_ui_build_today_decision(model, kind, ptc_ui_render_now(), &decision);
    format_plan_rule_value(before, before_value, sizeof(before_value));
    format_plan_rule_value(after, after_value, sizeof(after_value));
    ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
    if (kind == PTC_UI_PLAN_WEEKLY) {
        for (int day = 0; day < 7; ++day)
            if (ptc_ui_day_rule_effectively_changed(model->current_week[day], model->draft_week[day])) ++changed;
        snprintf(change, sizeof(change), "本次修改：调整了 %d 天的周计划", changed);
    } else {
        if (model->holiday_enabled != model->draft_holiday_enabled) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->holiday_rule, model->draft_holiday_rule)) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->makeup_workday_rule, model->draft_makeup_workday_rule)) ++changed;
        snprintf(change, sizeof(change), "本次修改：%d 项节假日设置", changed);
    }
    if (after.rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(remaining, sizeof(remaining), "不限时");
    } else if (ptc_ui_status_is_fresh(model, ptc_ui_render_now()) &&
               model->played_minutes_available && model->played_minutes >= 0) {
        int value = (int)after.rule.minutes - model->played_minutes;
        snprintf(remaining, sizeof(remaining), "%d 分钟", value > 0 ? value : 0);
    } else {
        snprintf(remaining, sizeof(remaining), "暂不可用");
    }
    draw_dialog_shell(pixels, stride, model, &dialog, 760, 420);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 126, change, 15, UI_MUTED);
    UiRect current = {dialog.x + 34, dialog.y + 146, 326, 94};
    UiRect saved = {dialog.x + 400, dialog.y + 146, 326, 94};
    if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        UiRect unchanged = {dialog.x + 34, dialog.y + 146, dialog.width - 68, 94};
        fill_round_rect(pixels, stride, unchanged, 14, UI_RAISED);
        draw_rect_outline(pixels, stride, unchanged, 14, 1, UI_BORDER);
        draw_status_symbol(pixels, stride, unchanged.x + 28, unchanged.y + 47, UI_MUTED, 1);
        draw_text(pixels, stride, unchanged.x + 52, unchanged.y + 30, "今天额度不变", 17, UI_INK);
        draw_wrapped_text(pixels, stride, unchanged.x + 52, unchanged.y + 56, impact,
                          13, unchanged.width - 70, 18, 2, UI_MUTED);
    } else {
        fill_round_rect(pixels, stride, current, 14, UI_RAISED);
        draw_rect_outline(pixels, stride, current, 14, 1, UI_BORDER);
        fill_round_rect(pixels, stride, saved, 14,
                        model->confirm_hold_required ? UI_DANGER_SOFT : UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, saved, 14, 2,
                          model->confirm_hold_required ? UI_DANGER : UI_ACCENT);
        draw_text(pixels, stride, current.x + 16, current.y + 24, "当前额度", 14, UI_MUTED);
        draw_text(pixels, stride, current.x + 16, current.y + 57, before_value, 26, UI_INK);
        draw_text(pixels, stride, current.x + 16, current.y + 80,
                  ptc_ui_effective_rule_label(before.source), 13, UI_MUTED);
        draw_text(pixels, stride, saved.x + 16, saved.y + 24, "保存后额度", 14,
                  model->confirm_hold_required ? UI_DANGER : UI_ACCENT);
        draw_text(pixels, stride, saved.x + 16, saved.y + 57, after_value, 26,
                  model->confirm_hold_required ? UI_DANGER :
                  (after.rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT));
        draw_text(pixels, stride, saved.x + 16, saved.y + 80,
                  ptc_ui_effective_rule_label(after.source), 13, UI_MUTED);
        draw_line(pixels, stride, current.x + current.width + 5, current.y + 47,
                  saved.x - 8, saved.y + 47, 2, UI_MUTED);
        draw_line(pixels, stride, saved.x - 13, saved.y + 42, saved.x - 8, saved.y + 47, 2, UI_MUTED);
        draw_line(pixels, stride, saved.x - 13, saved.y + 52, saved.x - 8, saved.y + 47, 2, UI_MUTED);
    }
    if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        draw_text(pixels, stride, dialog.x + 34, dialog.y + 258, "保存说明", 15, UI_INK);
    } else {
        char estimate[96];
        snprintf(estimate, sizeof(estimate), "保存后预计还可玩：%s", remaining);
        draw_text(pixels, stride, dialog.x + 34, dialog.y + 258, estimate, 15,
                  projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_INK);
    }
    draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 280,
                      projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE
                          ? decision.final_reason : impact,
                      15, dialog.width - 68, 20, 2, UI_MUTED);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 306, dialog.width - 68, 38}, 10,
                    model->confirm_hold_required ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 46, dialog.y + 306, dialog.width - 92, 38},
                     model->confirm_hold_required
                        ? "保存后可能立即耗尽；请长按确认" : decision.final_reason,
                     15, model->confirm_hold_required ? UI_DANGER : UI_SUCCESS);
    draw_overlay_actions(pixels, stride, model,
                         model->confirm_hold_required ? "长按 A / 触摸按住" : "A  确认保存");
}

void draw_confirm_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char comparison[128];
    PtcUiModel shell_model;
    bool restore = model->operation == PTC_UI_OPERATION_RESTORE_TODAY_POLICY;
    bool limit_change = model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT;
    bool add_change = model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES;
    bool unlimited_change = model->operation == PTC_UI_OPERATION_DISABLE_TODAY_LIMIT;
    bool direct_quota_change = add_change || unlimited_change;
    bool code_preview = model->operation == PTC_UI_OPERATION_REDEEM_OFFLINE_CODE;
    bool bedtime_save = model->operation == PTC_UI_OPERATION_SAVE_BEDTIME;
    bool restore_keeps_quota = false;
    bool limit_keeps_quota = false;
    bool code_keeps_quota = false;
    bool direct_keeps_quota = false;
    bool album_change = model->operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
                        model->operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
                        model->operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY;
    bool danger = model->operation == PTC_UI_OPERATION_DISABLE_TODAY_LIMIT ||
                  model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT ||
                  model->operation == PTC_UI_OPERATION_SAVE_WEEKLY ||
                  model->operation == PTC_UI_OPERATION_SAVE_HOLIDAY ||
                  model->operation == PTC_UI_OPERATION_EMERGENCY_DISABLE ||
                  model->operation == PTC_UI_OPERATION_RESUME_CONTROL ||
                   model->operation == PTC_UI_OPERATION_COMPLETE_SETUP ||
                   model->operation == PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT ||
                   code_preview || bedtime_save;
    if (model->operation == PTC_UI_OPERATION_SAVE_WEEKLY ||
        model->operation == PTC_UI_OPERATION_SAVE_HOLIDAY) {
        draw_plan_save_confirmation(pixels, stride, model);
        return;
    }
    shell_model = *model;
    if (code_preview) {
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "代码时长 %d 分钟，本次预计增加 %d 分钟。成功兑换后仅可使用一次。",
                 model->code_grant_minutes, model->code_effective_add_minutes);
    } else if (restore) {
        ptc_ui_format_restore_today_basis(model, shell_model.overlay_body, sizeof(shell_model.overlay_body));
    } else if (limit_change) {
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "请核对今天的实时状态和修改结果。");
    }
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 760, 420);
    if (code_preview) {
        char current_value[48];
        char after_value[48];
        if (model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "不限时");
        else format_duration(model->remaining_available ? model->remaining_minutes : -1,
                             current_value, sizeof(current_value));
        format_duration(model->code_preview_after_available ? model->code_preview_after_minutes : -1,
                        after_value, sizeof(after_value));
        code_keeps_quota = model->code_preview_capped &&
            !model->code_preview_converts_unlimited && model->code_effective_add_minutes == 0;
        if (code_keeps_quota) {
            draw_unchanged_quota_card(pixels, stride,
                (UiRect){dialog.x + 54, dialog.y + 142, 652, 92},
                "已达到每日额度上限，本次兑换不会增加今天额度。");
        } else {
            draw_remaining_transition(pixels, stride,
                (UiRect){dialog.x + 54, dialog.y + 142, 652, 92},
                "当前剩余", current_value,
                time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                  model->unrestricted_today == 1, model->remaining_minutes),
                "操作后剩余", after_value,
                time_state_accent(model->code_preview_after_available, false,
                                  model->code_preview_after_minutes));
        }
    } else if (restore) {
        PtcEffectiveRule restored = ptc_ui_rule_after_today_restore(model);
        PtcDayRule current = model->today_override_present ? model->today_override_rule : restored.rule;
        PtcDayRule after = restored.rule;
        char current_value[48];
        char after_value[48];
        int current_minutes = current.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available
            ? (int)current.minutes - model->played_minutes : -1;
        int after_minutes = after.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available
            ? (int)after.minutes - model->played_minutes : -1;
        if (current_minutes < 0 && current.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available) current_minutes = 0;
        if (after_minutes < 0 && after.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available) after_minutes = 0;
        if (current.mode == PTC_RULE_MODE_UNLIMITED) snprintf(current_value, sizeof(current_value), "不限时");
        else format_duration(current_minutes, current_value, sizeof(current_value));
        if (after.mode == PTC_RULE_MODE_UNLIMITED) snprintf(after_value, sizeof(after_value), "不限时");
        else format_duration(after_minutes, after_value, sizeof(after_value));
        restore_keeps_quota = !ptc_ui_day_rule_effectively_changed(current, after);
        if (restore_keeps_quota) {
            draw_unchanged_quota_card(pixels, stride,
                (UiRect){dialog.x + 54, dialog.y + 142, 652, 92},
                "清除后继续采用相同额度的下级规则，仅规则来源变化。");
        } else {
            draw_remaining_transition(pixels, stride,
                (UiRect){dialog.x + 54, dialog.y + 142, 652, 92},
                "当前剩余", current_value,
                time_state_accent(current.mode == PTC_RULE_MODE_UNLIMITED || current_minutes >= 0,
                                  current.mode == PTC_RULE_MODE_UNLIMITED, current_minutes),
                "操作后剩余", after_value,
                time_state_accent(after.mode == PTC_RULE_MODE_UNLIMITED || after_minutes >= 0,
                                  after.mode == PTC_RULE_MODE_UNLIMITED, after_minutes));
        }
    } else if (limit_change) {
        PtcEffectiveRule current_rule = ptc_ui_plan_rule(model, PTC_UI_PLAN_SAVED);
        PtcDayRule requested_rule = {PTC_RULE_MODE_LIMIT, model->draft_minutes};
        char played_value[48];
        char played_line[96];
        char current_value[48];
        char after_value[48];
        int after_minutes = model->played_minutes_available ? (int)model->draft_minutes - model->played_minutes : -1;
        if (after_minutes < 0 && model->played_minutes_available) after_minutes = 0;
        format_duration(model->played_minutes_available ? model->played_minutes : -1, played_value, sizeof(played_value));
        snprintf(played_line, sizeof(played_line), "额度已耗（估算）：%s", played_value);
        if (model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "不限时");
        else format_duration(model->remaining_available ? model->remaining_minutes : -1, current_value, sizeof(current_value));
        format_duration(after_minutes, after_value, sizeof(after_value));
        limit_keeps_quota = !ptc_ui_day_rule_effectively_changed(current_rule.rule, requested_rule);
        if (limit_keeps_quota) {
            draw_unchanged_quota_card(pixels, stride,
                (UiRect){dialog.x + 54, dialog.y + 142, 652, 92},
                "输入值与当前额度相同，仅建立相同额度的今日调整。");
        } else {
            draw_remaining_transition(pixels, stride,
                (UiRect){dialog.x + 54, dialog.y + 142, 652, 92},
                "当前剩余", current_value,
                time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                  model->unrestricted_today == 1, model->remaining_minutes),
                "操作后剩余", after_value,
                time_state_accent(after_minutes >= 0, false, after_minutes));
        }
        draw_text_center(pixels, stride,
            (UiRect){dialog.x + 54, dialog.y + 234, 652, 18},
            model->played_minutes_available ? played_line : "额度已耗（估算）：暂不可用",
            14, model->played_minutes_available ? UI_MUTED : UI_WARNING);
    } else if (direct_quota_change) {
        char current_value[48];
        char after_value[48];
        int after_minutes = add_change ? ptc_ui_preview_remaining_minutes(model) : -1;
        if (model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "不限时");
        else format_duration(model->remaining_available ? model->remaining_minutes : -1,
                             current_value, sizeof(current_value));
        if (unlimited_change) snprintf(after_value, sizeof(after_value), "不限时");
        else format_duration(after_minutes, after_value, sizeof(after_value));
        direct_keeps_quota = add_change && model->unrestricted_today != 1 &&
            model->remaining_available && after_minutes == model->remaining_minutes;
        if (direct_keeps_quota) {
            draw_unchanged_quota_card(pixels, stride,
                (UiRect){dialog.x + 54, dialog.y + 142, 652, 92},
                "已达到每日额度上限，本次加时不会增加今天额度。");
        } else {
            draw_remaining_transition(pixels, stride,
                (UiRect){dialog.x + 54, dialog.y + 142, 652, 92},
                "当前剩余", current_value,
                time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                  model->unrestricted_today == 1, model->remaining_minutes),
                "操作后剩余", after_value,
                time_state_accent(unlimited_change || after_minutes >= 0,
                                  unlimited_change, after_minutes));
        }
    } else if (bedtime_save) {
        UiRect bedtime_risk = {dialog.x + 54, dialog.y + 218, 652, 92};
        fill_round_rect(pixels, stride, bedtime_risk, 16, UI_DANGER_SOFT);
        draw_rect_outline(pixels, stride, bedtime_risk, 16, 2, UI_DANGER);
        draw_text_center(pixels, stride, (UiRect){bedtime_risk.x, bedtime_risk.y + 10, bedtime_risk.width, 34},
                         "保存后立即进入就寝限制", 24, UI_DANGER);
        draw_text_center(pixels, stride, (UiRect){bedtime_risk.x, bedtime_risk.y + 48, bedtime_risk.width, 28},
                         "游戏会暂停；之后仅可通过 Overlay 恢复", 17, UI_DANGER);
    } else if (model->confirm_hold_required && model->played_minutes_available) {
        snprintf(comparison, sizeof(comparison), "额度已耗 %d 分钟       还剩 0 分钟",
                 model->played_minutes);
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 218, 652, 92}, 16, UI_DANGER_SOFT);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 226, 652, 34}, comparison, 25, UI_DANGER);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 264, 652, 34},
                         "新额度不高于额度消耗估算，保存后会马上限制儿童使用", 20, UI_DANGER);
    }
    if (restore || limit_change || code_preview || direct_quota_change) {
        uint32_t impact_background = (limit_keeps_quota || restore_keeps_quota)
            ? UI_RAISED : (model->confirm_hold_required ? UI_DANGER_SOFT : UI_WARNING_SOFT);
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54}, 16,
                        impact_background);
        if (code_preview) {
            char warning[160];
            if (model->code_preview_converts_unlimited) {
                snprintf(warning, sizeof(warning), "当前不限时，兑换后将改为限时%s",
                         model->confirm_hold_required ? "；请长按 A 确认" : "");
            } else if (!model->code_preview_after_available) {
                snprintf(warning, sizeof(warning), "实时状态未知；请长按 A 确认");
            } else if (model->code_preview_after_minutes == 0) {
                snprintf(warning, sizeof(warning), "兑换后预计没有可玩时间；请长按 A 确认");
            } else if (model->code_preview_capped) {
                snprintf(warning, sizeof(warning), "受每日 1440 分钟上限影响，预计增加 %d 分钟",
                         model->code_effective_add_minutes);
            } else {
                snprintf(warning, sizeof(warning), "确认后才会生效并消费这枚一次性加时码");
            }
            draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                             warning, 18, model->confirm_hold_required ? UI_DANGER : UI_WARNING);
        } else if (limit_change && limit_keeps_quota) {
            draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                             "今天额度不变；确认后保存相同额度的今日调整", 18, UI_MUTED);
        } else if (limit_change) {
            char risk[160];
            char recovery[128];
            ptc_ui_format_today_limit_confirmation(model, risk, sizeof(risk), recovery, sizeof(recovery));
            draw_text_center(pixels, stride, (UiRect){dialog.x + 64, dialog.y + 254, 632, 24},
                             risk, 15, model->confirm_hold_required ? UI_DANGER : UI_WARNING);
            draw_text_center(pixels, stride, (UiRect){dialog.x + 64, dialog.y + 280, 632, 22},
                             recovery, 15, UI_MUTED);
        } else if (direct_quota_change && direct_keeps_quota) {
            draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                             "今天额度不变；已达到每日额度上限", 18, UI_WARNING);
        } else if (direct_quota_change) {
            char impact[144];
            if (model->confirm_hold_required) {
                snprintf(impact, sizeof(impact), "实时状态待确认；请长按确认本次额度调整");
            } else if (unlimited_change) {
                snprintf(impact, sizeof(impact), "保存后今天不再受每日额度限制");
            } else {
                snprintf(impact, sizeof(impact), "本次增加 %u 分钟；保存后刷新实际剩余",
                         (unsigned int)model->draft_minutes);
            }
            draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                             impact, 18, model->confirm_hold_required ? UI_DANGER : UI_WARNING);
        } else if (restore_keeps_quota) {
            draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                             "今天额度不变；确认后只清除今日调整来源", 18, UI_MUTED);
        } else {
            draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                             model->confirm_hold_required
                                ? (model->played_minutes_available ? "操作后可能立即限制游玩，请长按 A 确认" : "无法取得额度消耗估算，不能判断是否立即限制")
                                : (limit_change && model->unrestricted_today == 1 ? "不限时将改为限时，请确认状态变化" : "请确认状态变化"),
                             19, model->confirm_hold_required ? UI_DANGER : UI_WARNING);
        }
    } else if (!album_change && !bedtime_save &&
               (!model->confirm_hold_required || !model->played_minutes_available)) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 230, 620, 72}, 16, danger ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 230, 620, 72},
                         danger ? "请确认已了解这项操作的影响" : "确认执行这项操作", 22,
                         danger ? UI_DANGER : UI_SUCCESS);
    }
    draw_overlay_actions(pixels, stride, model,
                         model->confirm_hold_required ? "长按 A / 触摸按住" : "A  确认执行");
}

