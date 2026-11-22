#include "ui_render_internal.h"

static const char *rule_mode_label(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_UNLIMITED:
        return ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED);
    case PTC_RULE_MODE_LIMIT:
    default:
        return ptc_ui_text(PTC_UI_T_LIMITED);
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
    char bedtime_notice[128];
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    int preview_min = ptc_ui_status_is_fresh(model, ptc_ui_render_now())
        ? ptc_ui_preview_remaining_minutes(model) : -1;
    int played_min = model->played_minutes_available ? model->played_minutes : -1;
    bool quota_unchanged = false;

    draw_dialog_shell(pixels, stride, model, &dialog, 720, 560);
    snprintf(value, sizeof(value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)model->draft_minutes);
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
        snprintf(date_line, sizeof(date_line), ptc_ui_text(PTC_UI_T_AFFECTED_DATE_U_02U_02U_TODAY), year, month, day);
    } else {
        snprintf(date_line, sizeof(date_line), "%s", ptc_ui_text(PTC_UI_T_DATE_IMPACTED_TODAY));
    }
    if (played_min >= 0) {
        snprintf(played_line, sizeof(played_line),
            ptc_ui_text(model->usage_present ? PTC_UI_T_CONSOLE_USAGE_D_MIN : PTC_UI_T_ESTIMATED_USED_ABOUT_D_MIN), played_min);
    } else {
        snprintf(played_line, sizeof(played_line), "%s", ptc_ui_text(PTC_UI_T_THE_QUOTA_HAS_BEEN_CONSUMED_ESTIMATED_TEMPORARILY));
    }
    if (model->unrestricted_today == 1) {
        snprintf(current_value, sizeof(current_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    } else if (model->remaining_available && model->remaining_minutes >= 0) {
        format_duration(model->remaining_minutes, current_value, sizeof(current_value));
    } else {
        snprintf(current_value, sizeof(current_value), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    }
    if (preview_min >= 0) {
        format_duration(preview_min, after_value, sizeof(after_value));
    } else if (model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        snprintf(after_value, sizeof(after_value), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    } else {
        snprintf(after_value, sizeof(after_value), "%s", ptc_ui_text(PTC_UI_T_REFRESH_AND_CONFIRM_AFTER_SAVING));
    }
    format_status_age(model, freshness, sizeof(freshness));
    ptc_ui_format_bedtime_quota_notice(model, ptc_ui_render_now(), bedtime_notice, sizeof(bedtime_notice));
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
                ? ptc_ui_text(PTC_UI_T_THE_DAILY_QUOTA_LIMIT_HAS_BEEN_REACHED)
                : ptc_ui_text(PTC_UI_T_THE_INPUT_VALUE_IS_THE_SAME_AS));
    } else {
        draw_remaining_transition(pixels, stride,
            (UiRect){dialog.x + 44, dialog.y + 344, 632, 74},
            ptc_ui_text(PTC_UI_T_CURRENT_REMAINING), current_value,
            time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                              model->unrestricted_today == 1, model->remaining_minutes),
            ptc_ui_text(PTC_UI_T_NEW_REMAINING), after_value,
            time_state_accent(preview_min >= 0, false, preview_min));
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 424, 640, 22}, played_line, 16, UI_MUTED);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 448, 640, 22}, freshness, 16,
                     status_age_color(model));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 50, dialog.y + 472, 620, 22},
                     bedtime_notice[0] ? bedtime_notice : ptc_ui_text(PTC_UI_T_THIS_MODIFICATION_ONLY_AFFECTS_TODAY_Y_OR),
                     16, bedtime_notice[0] ? UI_DANGER : UI_MUTED);
    draw_overlay_actions(pixels, stride, model, ptc_ui_text(PTC_UI_T_A_SUBMIT_AND_REFRESH));
}

void draw_weekly_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *DAYS[] = {ptc_ui_text(PTC_UI_T_SUNDAY), ptc_ui_text(PTC_UI_T_MONDAY), ptc_ui_text(PTC_UI_T_TUESDAY), ptc_ui_text(PTC_UI_T_WEDNESDAY), ptc_ui_text(PTC_UI_T_THURSDAY), ptc_ui_text(PTC_UI_T_FRIDAY), ptc_ui_text(PTC_UI_T_SATURDAY)};
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
            snprintf(minutes, sizeof(minutes), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)model->draft_week[day].minutes);
        } else {
            snprintf(minutes, sizeof(minutes), "--");
        }
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 119, card.width, 34}, minutes, 19, UI_MUTED);
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 80, dialog.y + 320, dialog.width - 160, 30},
                     ptc_ui_text(PTC_UI_T_AFTER_SELECTING_THE_DATE_X_SWITCHES_MODE), 19, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_weekly_mode_rect(), ptc_ui_text(PTC_UI_T_X_SWITCH_MODE), UI_RAISED, UI_ACCENT, true);
    if (model->draft_week[model->editor_index].mode == PTC_RULE_MODE_LIMIT) {
        snprintf(selected_minutes, sizeof(selected_minutes), ptc_ui_text(PTC_UI_T_U_MIN),
                 (unsigned int)model->draft_week[model->editor_index].minutes);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_up_rect(), "+15", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_down_rect(), "-15", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_dec_rect(), "-5", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_inc_rect(), "+5", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_input_rect(), selected_minutes,
                           UI_ACCENT_SOFT, UI_ACCENT, true);
    }
    draw_overlay_actions(pixels, stride, model, ptc_ui_text(PTC_UI_T_A_SAVE_AND_REFRESH));
}


static void format_plan_rule_value(PtcEffectiveRule rule, char *out, size_t out_size)
{
    if (rule.rule.mode == PTC_RULE_MODE_UNLIMITED)
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    else
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)rule.rule.minutes);
}

static void draw_plan_save_confirmation(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiPlanKind kind = model->operation == PTC_UI_OPERATION_SAVE_HOLIDAY
        ? PTC_UI_PLAN_HOLIDAY : (model->operation == PTC_UI_OPERATION_SAVE_SCHEDULED
            ? PTC_UI_PLAN_SCHEDULED : PTC_UI_PLAN_WEEKLY);
    PtcUiPlanImpactProjection projection;
    PtcEffectiveRule before;
    PtcEffectiveRule after;
    PtcUiTodayDecision decision;
    char before_value[48], after_value[48], impact[256], change[96], remaining[48], bedtime_notice[128];
    int changed = 0;
    ptc_ui_project_plan_impact(model, kind, true, ptc_ui_render_now(), &projection);
    before = projection.before;
    after = projection.after;
    ptc_ui_build_today_decision(model, kind, ptc_ui_render_now(), &decision);
    format_plan_rule_value(before, before_value, sizeof(before_value));
    format_plan_rule_value(after, after_value, sizeof(after_value));
    ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
    ptc_ui_format_bedtime_quota_notice(model, ptc_ui_render_now(), bedtime_notice, sizeof(bedtime_notice));
    if (kind == PTC_UI_PLAN_WEEKLY) {
        for (int day = 0; day < 7; ++day)
            if (ptc_ui_day_rule_effectively_changed(model->current_week[day], model->draft_week[day])) ++changed;
        snprintf(change, sizeof(change), ptc_ui_text(PTC_UI_T_CHANGES_D_WEEKLY_DAYS), changed);
    } else if (kind == PTC_UI_PLAN_HOLIDAY) {
        if (model->holiday_enabled != model->draft_holiday_enabled) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->holiday_rule, model->draft_holiday_rule)) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->makeup_workday_rule, model->draft_makeup_workday_rule)) ++changed;
        snprintf(change, sizeof(change), ptc_ui_text(PTC_UI_T_CHANGES_D_HOLIDAY_SETTINGS), changed);
    } else {
        snprintf(change, sizeof(change), "%s", ptc_ui_text(PTC_UI_T_THIS_MODIFICATION_SPECIFIED_DATE_QUOTA));
    }
    if (after.rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(remaining, sizeof(remaining), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    } else if (ptc_ui_status_is_fresh(model, ptc_ui_render_now()) &&
               model->played_minutes_available && model->played_minutes >= 0) {
        int value = (int)after.rule.minutes - model->played_minutes;
        snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_D_MIN), value > 0 ? value : 0);
    } else {
        snprintf(remaining, sizeof(remaining), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    }
    draw_dialog_shell(pixels, stride, model, &dialog, 860, 476);
    {
        const char *pill_txt = projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE
            ? ptc_ui_text(PTC_UI_T_CONFIRM_STATUS_UNCHANGED)
            : (model->confirm_hold_required
                ? (projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED
                    ? ptc_ui_text(PTC_UI_T_CONFIRM_STATUS_PAUSE)
                    : ptc_ui_text(PTC_UI_T_CONFIRM_STATUS_PENDING))
                : ptc_ui_text(PTC_UI_T_CONFIRM_STATUS_DIRECT));
        uint32_t pbg = projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE ? UI_RAISED
            : (model->confirm_hold_required ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
        uint32_t pbd = projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE ? UI_BORDER
            : (model->confirm_hold_required ? UI_DANGER_BORDER : UI_SUCCESS_BORDER);
        uint32_t pfg = projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE ? UI_MUTED
            : (model->confirm_hold_required ? UI_DANGER : UI_SUCCESS);
        int pw = measure_text(pill_txt, 13) + 24;
        if (pw > 300) pw = 300;
        UiRect pill = {dialog.x + dialog.width - 34 - pw, dialog.y + 26, pw, 28};
        char fitted[96];
        fill_round_rect(pixels, stride, pill, 14, pbg);
        draw_rect_outline(pixels, stride, pill, 14, 1, pbd);
        fit_text(fitted, sizeof(fitted), pill_txt, 13, pill.width - 14);
        draw_text_center(pixels, stride, pill, fitted, 13, pfg);
    }
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 126, change, 15, UI_MUTED);
    UiRect current = {dialog.x + 34, dialog.y + 148, 376, 102};
    UiRect saved = {dialog.x + 450, dialog.y + 148, 376, 102};
    if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        UiRect unchanged = {dialog.x + 34, dialog.y + 148, dialog.width - 68, 102};
        fill_round_rect(pixels, stride, unchanged, 14, UI_RAISED);
        draw_rect_outline(pixels, stride, unchanged, 14, 1, UI_BORDER);
        draw_status_symbol(pixels, stride, unchanged.x + 28, unchanged.y + 51, UI_MUTED, 1);
        draw_text(pixels, stride, unchanged.x + 52, unchanged.y + 34, ptc_ui_text(PTC_UI_T_THE_QUOTA_REMAINS_UNCHANGED_TODAY), 18, UI_INK);
        draw_wrapped_text(pixels, stride, unchanged.x + 52, unchanged.y + 62, impact,
                          14, unchanged.width - 70, 20, 2, UI_MUTED);
    } else {
        fill_round_rect(pixels, stride, current, 14, UI_RAISED);
        draw_rect_outline(pixels, stride, current, 14, 1, UI_BORDER);
        fill_round_rect(pixels, stride, saved, 14,
                        model->confirm_hold_required ? UI_DANGER_SOFT : UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, saved, 14, 2,
                          model->confirm_hold_required ? UI_DANGER : UI_ACCENT);
        draw_text(pixels, stride, current.x + 18, current.y + 26, ptc_ui_text(PTC_UI_T_CURRENT_QUOTA), 14, UI_MUTED);
        draw_text_bold(pixels, stride, current.x + 18, current.y + 60, before_value, 26, UI_INK);
        draw_text(pixels, stride, current.x + 18, current.y + 86,
                  ptc_ui_effective_rule_label(before.source), 13, UI_MUTED);
        draw_text(pixels, stride, saved.x + 18, saved.y + 26, ptc_ui_text(PTC_UI_T_AMOUNT_AFTER_SAVING), 14,
                  model->confirm_hold_required ? UI_DANGER : UI_ACCENT);
        draw_text_bold(pixels, stride, saved.x + 18, saved.y + 60, after_value, 26,
                       model->confirm_hold_required ? UI_DANGER :
                       (after.rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT));
        draw_text(pixels, stride, saved.x + 18, saved.y + 86,
                  ptc_ui_effective_rule_label(after.source), 13, UI_MUTED);
        draw_transition_arrow(pixels, stride, current.x + current.width + 20, current.y + 51, UI_MUTED);
    }
    if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        draw_text(pixels, stride, dialog.x + 34, dialog.y + 276, ptc_ui_text(PTC_UI_T_SAVING_INSTRUCTIONS), 15, UI_INK);
    } else {
        char estimate[96];
        snprintf(estimate, sizeof(estimate), ptc_ui_text(PTC_UI_T_EST_TIME_AFTER_SAVE_S), remaining);
        draw_text(pixels, stride, dialog.x + 34, dialog.y + 276, estimate, 15,
                  projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_INK);
    }
    draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 300,
                      projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE
                          ? decision.final_reason : impact,
                      15, dialog.width - 68, 20, 2, UI_MUTED);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 332, dialog.width - 68, 48}, 10,
                    model->confirm_hold_required ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
    draw_rect_outline(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 332, dialog.width - 68, 48}, 10, 1,
                      model->confirm_hold_required ? UI_DANGER_BORDER : UI_SUCCESS_BORDER);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 46, dialog.y + 332, dialog.width - 92, bedtime_notice[0] ? 24 : 48},
                     model->confirm_hold_required
                        ? ptc_ui_text(PTC_UI_T_THE_USE_MAY_BE_RESTRICTED_IMMEDIATELY_AFTER) : decision.final_reason,
                     bedtime_notice[0] ? 13 : 15, model->confirm_hold_required ? UI_DANGER : UI_SUCCESS);
    if (bedtime_notice[0])
        draw_text_center(pixels, stride, (UiRect){dialog.x + 46, dialog.y + 356, dialog.width - 92, 20},
            bedtime_notice, 12, UI_DANGER);
    draw_overlay_actions(pixels, stride, model,
                         model->confirm_hold_required ? ptc_ui_text(PTC_UI_T_LONG_PRESS_A_TOUCH_AND_HOLD) : ptc_ui_text(PTC_UI_T_A_CONFIRM_SAVE));
}

void draw_confirm_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
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
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    bool album_change = model->operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
                        model->operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
                        model->operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY;
    bool danger = model->confirm_hold_required ||
                  model->operation == PTC_UI_OPERATION_DISABLE_TODAY_LIMIT ||
                  model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT ||
                  model->operation == PTC_UI_OPERATION_SAVE_WEEKLY ||
                  model->operation == PTC_UI_OPERATION_SAVE_HOLIDAY ||
                  model->operation == PTC_UI_OPERATION_SAVE_SCHEDULED ||
                  model->operation == PTC_UI_OPERATION_EMERGENCY_DISABLE ||
                  model->operation == PTC_UI_OPERATION_RESUME_CONTROL ||
                   model->operation == PTC_UI_OPERATION_COMPLETE_SETUP ||
                   model->operation == PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT ||
                   code_preview || bedtime_save;
    if (model->operation == PTC_UI_OPERATION_SAVE_WEEKLY ||
        model->operation == PTC_UI_OPERATION_SAVE_HOLIDAY ||
        model->operation == PTC_UI_OPERATION_SAVE_SCHEDULED) {
        draw_plan_save_confirmation(pixels, stride, model);
        return;
    }
    shell_model = *model;
    if (code_preview) {
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 ptc_ui_text(PTC_UI_T_CODE_DURATION_D_MIN_ESTIMATED_D_MIN),
                 model->code_grant_minutes, model->code_effective_add_minutes);
    } else if (restore) {
        ptc_ui_format_restore_today_basis(model, shell_model.overlay_body, sizeof(shell_model.overlay_body));
    } else if (limit_change) {
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "%s", ptc_ui_text(PTC_UI_T_PLEASE_CHECK_TODAY_S_REAL_TIME_STATUS));
    }
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 860, 476);
    UiRect card_box = {dialog.x + 54, dialog.y + 144, dialog.width - 108, 104};
    if (code_preview) {
        draw_redeem_step_bar(pixels, stride,
                             (UiRect){dialog.x + 54, dialog.y + 72, dialog.width - 108, 30}, 1);
        {
            char formatted_code[16], code_tag[96], fitted[128];
            ptc_ui_format_code(model->pending_code[0] ? model->pending_code : "--------",
                               formatted_code, sizeof(formatted_code));
            snprintf(code_tag, sizeof(code_tag), ptc_ui_text(PTC_UI_T_REDEEM_CODE_TAG_FMT),
                     formatted_code, model->code_grant_minutes);
            fit_text(fitted, sizeof(fitted), code_tag, 14, dialog.width - 108);
            draw_text(pixels, stride, dialog.x + 54, dialog.y + 128, fitted, 14, UI_ACCENT);
        }
    }
    if (code_preview) {
        char current_value[48];
        char after_value[48];
        if (fresh && model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else format_duration(fresh && model->remaining_available ? model->remaining_minutes : -1,
                             current_value, sizeof(current_value));
        format_duration(model->code_preview_after_available ? model->code_preview_after_minutes : -1,
                        after_value, sizeof(after_value));
        code_keeps_quota = model->code_preview_capped &&
            !model->code_preview_converts_unlimited && model->code_effective_add_minutes == 0;
        if (code_keeps_quota) {
            draw_unchanged_quota_card(pixels, stride, card_box,
                ptc_ui_text(PTC_UI_T_THE_DAILY_QUOTA_LIMIT_HAS_BEEN_REACHED_2));
        } else {
            char before_formula[64] = "";
            char after_formula[64] = "";
            if (fresh && model->played_minutes_available) {
                snprintf(before_formula, sizeof(before_formula), ptc_ui_text(model->usage_present ? PTC_UI_T_CONSOLE_USAGE_D_MIN : PTC_UI_T_PLAYED_D_MIN_TODAY), model->played_minutes);
            }
            snprintf(after_formula, sizeof(after_formula), ptc_ui_text(PTC_UI_T_CURRENT_D_MIN), model->code_grant_minutes);
            draw_quota_transition_detailed(pixels, stride, card_box,
                ptc_ui_text(PTC_UI_T_CURRENT_REMAINING), fresh ? ui_rule_source_label(model->rule_source) : (ptc_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING)),
                current_value, before_formula,
                time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                  model->unrestricted_today == 1, model->remaining_minutes),
                ptc_ui_text(PTC_UI_T_NEW_REMAINING), ptc_ui_text(PTC_UI_T_CODE_REDEMPTION),
                after_value, after_formula,
                time_state_accent(model->code_preview_after_available, false,
                                  model->code_preview_after_minutes));
        }
    } else if (restore) {
        PtcEffectiveRule restored = ptc_ui_rule_after_today_restore(model);
        PtcDayRule current = model->today_override_present ? model->today_override_rule : restored.rule;
        PtcDayRule after = restored.rule;
        char current_value[48];
        char after_value[48];
        int current_minutes = fresh && current.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available
            ? (int)current.minutes - model->played_minutes : -1;
        int after_minutes = fresh && after.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available
            ? (int)after.minutes - model->played_minutes : -1;
        if (current_minutes < 0 && fresh && current.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available) current_minutes = 0;
        if (after_minutes < 0 && fresh && after.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available) after_minutes = 0;
        if (!fresh) snprintf(current_value, sizeof(current_value), "%s", ptc_ui_text(PTC_UI_T_PENDING_2));
        else if (current.mode == PTC_RULE_MODE_UNLIMITED) snprintf(current_value, sizeof(current_value), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
        else format_duration(current_minutes, current_value, sizeof(current_value));
        if (!fresh) snprintf(after_value, sizeof(after_value), "%s", ptc_ui_text(PTC_UI_T_RECALCULATE_ON_REFRESH));
        else if (after.mode == PTC_RULE_MODE_UNLIMITED) snprintf(after_value, sizeof(after_value), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
        else format_duration(after_minutes, after_value, sizeof(after_value));
        restore_keeps_quota = !ptc_ui_day_rule_effectively_changed(current, after);
        if (restore_keeps_quota) {
            draw_unchanged_quota_card(pixels, stride, card_box,
                ptc_ui_text(PTC_UI_T_BASE_RULE_WITH_SAME_QUOTA_WILL_CONTINUE));
        } else {
            char before_formula[64] = "";
            char after_formula[64] = "";
            if (fresh && current.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available) {
                snprintf(before_formula, sizeof(before_formula),
                         ptc_ui_text(PTC_UI_T_TOTAL_U_MIN_PLAYED_D_MIN),
                         (unsigned int)current.minutes, model->played_minutes);
            }
            if (fresh && after.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available) {
                snprintf(after_formula, sizeof(after_formula),
                         ptc_ui_text(PTC_UI_T_BASE_U_MIN_PLAYED_D_MIN),
                         (unsigned int)after.minutes, model->played_minutes);
            } else if (after.mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(after_formula, sizeof(after_formula), "%s", ptc_ui_text(PTC_UI_T_NO_DAILY_LIMIT_CAP));
            }
            draw_quota_transition_detailed(pixels, stride, card_box,
                ptc_ui_text(PTC_UI_T_CURRENT_REMAINING), fresh ? ui_rule_source_label(model->rule_source) : (ptc_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING)),
                current_value, before_formula,
                time_state_accent(current.mode == PTC_RULE_MODE_UNLIMITED || current_minutes >= 0,
                                  current.mode == PTC_RULE_MODE_UNLIMITED, current_minutes),
                ptc_ui_text(PTC_UI_T_REMAINING_AFTER_CLEARING), fresh ? ptc_ui_effective_rule_label(restored.source) : (ptc_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING)),
                after_value, after_formula,
                time_state_accent(after.mode == PTC_RULE_MODE_UNLIMITED || after_minutes >= 0,
                                  after.mode == PTC_RULE_MODE_UNLIMITED, after_minutes));
        }
    } else if (limit_change) {
        PtcEffectiveRule current_rule = ptc_ui_plan_rule(model, PTC_UI_PLAN_SAVED);
        PtcDayRule requested_rule = {PTC_RULE_MODE_LIMIT, model->draft_minutes};
        char current_value[48];
        char after_value[48];
        int after_minutes = fresh && model->played_minutes_available ? (int)model->draft_minutes - model->played_minutes : -1;
        if (after_minutes < 0 && fresh && model->played_minutes_available) after_minutes = 0;
        if (fresh && model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
        else format_duration(fresh && model->remaining_available ? model->remaining_minutes : -1, current_value, sizeof(current_value));
        format_duration(after_minutes, after_value, sizeof(after_value));
        limit_keeps_quota = !ptc_ui_day_rule_effectively_changed(current_rule.rule, requested_rule);
        if (limit_keeps_quota) {
            draw_unchanged_quota_card(pixels, stride, card_box,
                ptc_ui_text(PTC_UI_T_SAME_AS_CURRENT_QUOTA_SETS_TODAY_S));
        } else {
            char before_formula[64] = "";
            char after_formula[64] = "";
            if (fresh && model->played_minutes_available) {
                snprintf(before_formula, sizeof(before_formula),
                         ptc_ui_text(model->usage_present ? PTC_UI_T_CONSOLE_USAGE_D_MIN : PTC_UI_T_PLAYED_D_MIN_TODAY),
                         model->played_minutes);
                snprintf(after_formula, sizeof(after_formula),
                         ptc_ui_text(PTC_UI_T_NEW_U_MIN_PLAYED_D_MIN),
                         (unsigned int)model->draft_minutes, model->played_minutes);
            }
            draw_quota_transition_detailed(pixels, stride, card_box,
                ptc_ui_text(PTC_UI_T_CURRENT_REMAINING), fresh ? ui_rule_source_label(model->rule_source) : (ptc_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING)),
                current_value, before_formula,
                time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                  model->unrestricted_today == 1, model->remaining_minutes),
                ptc_ui_text(PTC_UI_T_NEW_REMAINING), ptc_ui_text(PTC_UI_T_RULE_TODAY),
                after_value, after_formula,
                time_state_accent(after_minutes >= 0, false, after_minutes));
        }
    } else if (direct_quota_change) {
        char current_value[48];
        char after_value[48];
        int after_minutes = add_change && fresh ? ptc_ui_preview_remaining_minutes(model) : -1;
        if (fresh && model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
        else format_duration(fresh && model->remaining_available ? model->remaining_minutes : -1,
                             current_value, sizeof(current_value));
        if (unlimited_change) snprintf(after_value, sizeof(after_value), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
        else format_duration(after_minutes, after_value, sizeof(after_value));
        direct_keeps_quota = fresh && add_change && model->unrestricted_today != 1 &&
            model->remaining_available && after_minutes == model->remaining_minutes;
        if (direct_keeps_quota) {
            draw_unchanged_quota_card(pixels, stride, card_box,
                ptc_ui_text(PTC_UI_T_DAILY_LIMIT_CAP_REACHED_THIS_GRANT_WILL));
        } else {
            char before_formula[64] = "";
            char after_formula[64] = "";
            if (fresh && model->played_minutes_available) {
                snprintf(before_formula, sizeof(before_formula),
                         ptc_ui_text(model->usage_present ? PTC_UI_T_CONSOLE_USAGE_D_MIN : PTC_UI_T_PLAYED_D_MIN_TODAY), model->played_minutes);
            }
            if (unlimited_change) {
                snprintf(after_formula, sizeof(after_formula), "%s", ptc_ui_text(PTC_UI_T_NO_DAILY_LIMIT_CAP));
            } else if (add_change) {
                snprintf(after_formula, sizeof(after_formula), "%s", ptc_ui_text(PTC_UI_T_CURRENT_QUICK_ADD));
            }
            draw_quota_transition_detailed(pixels, stride, card_box,
                ptc_ui_text(PTC_UI_T_CURRENT_REMAINING), fresh ? ui_rule_source_label(model->rule_source) : (ptc_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING)),
                current_value, before_formula,
                time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                  model->unrestricted_today == 1, model->remaining_minutes),
                ptc_ui_text(PTC_UI_T_NEW_REMAINING), unlimited_change ? (ptc_ui_text(PTC_UI_T_UNLIMITED_ADJUSTMENT)) : (ptc_ui_text(PTC_UI_T_QUICK_GRANT)),
                after_value, after_formula,
                time_state_accent(unlimited_change || after_minutes >= 0,
                                  unlimited_change, after_minutes));
        }
    } else if (bedtime_save) {
        UiRect bedtime_risk = {dialog.x + 54, dialog.y + 224, dialog.width - 108, 96};
        int64_t raw_now = ptc_ui_render_now();
        uint16_t minute_of_day = ptc_ui_render_minute_of_day(raw_now);
        PtcUiBedtimeImpact impact = ptc_ui_bedtime_save_impact(model,
            minute_of_day, (int64_t)raw_now);
        fill_round_rect(pixels, stride, bedtime_risk, 14, UI_DANGER_SOFT);
        draw_rect_outline(pixels, stride, bedtime_risk, 14, 2, UI_DANGER);
        draw_text_center(pixels, stride, (UiRect){bedtime_risk.x, bedtime_risk.y + 12, bedtime_risk.width, 34},
                         impact == PTC_UI_BEDTIME_IMPACT_RESTRICT
                             ? ptc_ui_text(PTC_UI_T_ENTER_BEDTIME_RESTRICTIONS_IMMEDIATELY_AFTER_SAVING) : ptc_ui_text(PTC_UI_T_MAY_ENTER_BEDTIME_RESTRICTIONS_IMMEDIATELY_AFTER_SAVING), 22, UI_DANGER);
        draw_text_center(pixels, stride, (UiRect){bedtime_risk.x, bedtime_risk.y + 52, bedtime_risk.width, 28},
                         ptc_ui_text(PTC_UI_T_THE_GAME_WILL_PAUSE_IT_CAN_ONLY), 16, UI_DANGER);
    }

    /* Plain-language top-right status header pill (no engineering jargon) */
    {
        bool unchanged = limit_keeps_quota || restore_keeps_quota || code_keeps_quota || direct_keeps_quota;
        const char *pill_txt = NULL;
        uint32_t pbg = UI_SUCCESS_SOFT, pbd = UI_SUCCESS_BORDER, pfg = UI_SUCCESS;
        if (unchanged) {
            pill_txt = ptc_ui_text(PTC_UI_T_CONFIRM_STATUS_UNCHANGED);
            pbg = UI_RAISED; pbd = UI_BORDER; pfg = UI_MUTED;
        } else if (model->confirm_hold_required) {
            bool policy_confirm = restore || limit_change || direct_quota_change || code_preview || bedtime_save;
            if (!policy_confirm) {
                pill_txt = ptc_ui_text(PTC_UI_T_CONFIRM_STATUS_HOLD_GENERIC);
            } else if (!fresh || (!bedtime_save && !model->played_minutes_available)) {
                pill_txt = ptc_ui_text(PTC_UI_T_CONFIRM_STATUS_PENDING);
            } else {
                pill_txt = ptc_ui_text(PTC_UI_T_CONFIRM_STATUS_PAUSE);
            }
            pbg = UI_DANGER_SOFT; pbd = UI_DANGER_BORDER; pfg = UI_DANGER;
        } else if (restore || limit_change || direct_quota_change || code_preview) {
            pill_txt = ptc_ui_text(PTC_UI_T_CONFIRM_STATUS_DIRECT);
        }
        if (pill_txt) {
            char fitted[96];
            int pw = measure_text(pill_txt, 13) + 24;
            if (pw > 300) pw = 300;
            UiRect pill = {dialog.x + dialog.width - 34 - pw, dialog.y + 26, pw, 28};
            fill_round_rect(pixels, stride, pill, 14, pbg);
            draw_rect_outline(pixels, stride, pill, 14, 1, pbd);
            fit_text(fitted, sizeof(fitted), pill_txt, 13, pill.width - 14);
            draw_text_center(pixels, stride, pill, fitted, 13, pfg);
        }
    }

    if (restore || limit_change || code_preview || direct_quota_change) {
        UiRect impact_box = {dialog.x + 54, dialog.y + 260, dialog.width - 108, 60};
        bool unchanged = limit_keeps_quota || restore_keeps_quota || code_keeps_quota || direct_keeps_quota;
        bool safe_direct = !unchanged && !model->confirm_hold_required &&
            !(code_preview && (model->code_preview_capped || model->code_preview_converts_unlimited)) &&
            !(limit_change && model->unrestricted_today == 1);
        uint32_t impact_bg = unchanged ? UI_RAISED
            : (model->confirm_hold_required ? UI_DANGER_SOFT
                : (safe_direct ? UI_SUCCESS_SOFT : UI_WARNING_SOFT));
        uint32_t impact_bd = unchanged ? UI_BORDER
            : (model->confirm_hold_required ? UI_DANGER_BORDER
                : (safe_direct ? UI_SUCCESS_BORDER : UI_WARNING_BORDER));
        uint32_t impact_fg = unchanged ? UI_MUTED
            : (model->confirm_hold_required ? UI_DANGER
                : (safe_direct ? UI_SUCCESS : UI_WARNING_TEXT));
        fill_round_rect(pixels, stride, impact_box, 12, impact_bg);
        draw_rect_outline(pixels, stride, impact_box, 12, 1, impact_bd);
        if (code_preview) {
            char warning[160];
            if (model->code_preview_converts_unlimited) {
                snprintf(warning, sizeof(warning), ptc_ui_text(PTC_UI_T_CURRENTLY_UNLIMITED_REDEMPTION_WILL_SET_A_S),
                         model->confirm_hold_required ? ptc_ui_text(PTC_UI_T_PLEASE_PRESS_AND_HOLD_A_TO_CONFIRM) : "");
            } else if (!model->code_preview_after_available) {
                snprintf(warning, sizeof(warning), "%s", ptc_ui_text(PTC_UI_T_THE_REAL_TIME_STATUS_IS_UNKNOWN_PLEASE));
            } else if (model->code_preview_after_minutes == 0) {
                snprintf(warning, sizeof(warning), "%s", ptc_ui_text(PTC_UI_T_THERE_IS_NO_EXPECTED_PLAY_TIME_AFTER));
            } else if (model->code_preview_capped) {
                snprintf(warning, sizeof(warning),
                         ptc_ui_text(PTC_UI_T_DAILY_1440_MIN_CAP_APPLIES_ESTIMATED_D),
                         model->code_effective_add_minutes);
            } else {
                snprintf(warning, sizeof(warning), "%s", ptc_ui_text(PTC_UI_T_THIS_ONE_TIME_GRANT_CODE_WILL_TAKE));
            }
            draw_text_center(pixels, stride, impact_box, warning, 16, impact_fg);
        } else if (limit_change && limit_keeps_quota) {
            draw_text_center(pixels, stride, impact_box,
                             ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_REMAINS_UNCHANGED_TODAY_S), 16, UI_MUTED);
        } else if (limit_change) {
            char risk[160];
            char recovery[128];
            ptc_ui_format_today_limit_confirmation(model, risk, sizeof(risk), recovery, sizeof(recovery));
            draw_text_center(pixels, stride, (UiRect){impact_box.x + 12, impact_box.y + 6, impact_box.width - 24, 24},
                             risk, 15, impact_fg);
            draw_text_center(pixels, stride, (UiRect){impact_box.x + 12, impact_box.y + 32, impact_box.width - 24, 22},
                             recovery, 13, UI_MUTED);
        } else if (direct_quota_change && direct_keeps_quota) {
            draw_text_center(pixels, stride, impact_box,
                             ptc_ui_text(PTC_UI_T_THE_QUOTA_REMAINS_UNCHANGED_TODAY_THE_DAILY), 16, UI_MUTED);
        } else if (direct_quota_change) {
            char impact[144];
            int effective = fresh ? ptc_ui_preview_effective_add_minutes(model) : -1;
            if (model->confirm_hold_required || (add_change && effective < 0)) {
                snprintf(impact, sizeof(impact), "%s", ptc_ui_text(PTC_UI_T_THE_REAL_TIME_STATUS_NEEDS_TO_BE));
            } else if (unlimited_change) {
                snprintf(impact, sizeof(impact), "%s", ptc_ui_text(PTC_UI_T_AFTER_SAVING_TODAY_WILL_NO_LONGER_BE));
            } else {
                snprintf(impact, sizeof(impact),
                         ptc_ui_text(PTC_UI_T_ADD_U_MIN_ACTUAL_REMAINING_REFRESHES_ON),
                         (unsigned int)effective);
            }
            draw_text_center(pixels, stride, impact_box, impact, 16, impact_fg);
        } else if (restore_keeps_quota) {
            draw_text_center(pixels, stride, impact_box,
                             ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_REMAINS_UNCHANGED_ONLY_TODAY), 16, UI_MUTED);
        } else {
            draw_text_center(pixels, stride, impact_box,
                             model->confirm_hold_required
                                ? (model->played_minutes_available ? ptc_ui_text(PTC_UI_T_PLAY_MAY_BE_RESTRICTED_IMMEDIATELY_AFTER_OPERATION) : ptc_ui_text(PTC_UI_T_UNABLE_TO_OBTAIN_QUOTA_CONSUMPTION_ESTIMATE_UNABLE))
                                : (limit_change && model->unrestricted_today == 1 ? ptc_ui_text(PTC_UI_T_UNLIMITED_TIME_WILL_BE_CHANGED_TO_LIMITED) : ptc_ui_text(PTC_UI_T_PLEASE_CONFIRM_THE_STATUS_CHANGE)),
                             17, impact_fg);
        }
    } else if (!album_change && !bedtime_save) {
        /* A hold protects many operations; only policy previews above can
         * establish an immediate restriction, never the hold flag itself. */
        UiRect generic_box = {dialog.x + 54, dialog.y + 244, dialog.width - 108, 78};
        fill_round_rect(pixels, stride, generic_box, 14, danger ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
        draw_rect_outline(pixels, stride, generic_box, 14, 1, danger ? UI_DANGER_BORDER : UI_SUCCESS_BORDER);
        draw_text_center(pixels, stride, generic_box,
                         danger ? ptc_ui_text(PTC_UI_T_PLEASE_MAKE_SURE_YOU_UNDERSTAND_THE_IMPACT) : ptc_ui_text(PTC_UI_T_CONFIRM_TO_PERFORM_THIS_OPERATION), 20,
                         danger ? UI_DANGER : UI_SUCCESS);
    }
    if (restore || limit_change || direct_quota_change) {
        UiRect refresh = to_uirect(ptc_ui_quota_refresh_rect());
        bool failed = model->quota_refresh_failed;
        bool stale = !model->waiting && !ptc_ui_status_is_fresh(model, ptc_ui_render_now());
        bool warn = !model->waiting && (failed || stale);
        uint32_t bg = failed ? UI_DANGER_SOFT : (warn ? UI_WARNING_SOFT : UI_ACCENT_SOFT);
        uint32_t bd = failed ? UI_DANGER : (warn ? UI_WARNING : UI_ACCENT);
        uint32_t fg = failed ? UI_DANGER : (warn ? UI_WARNING_TEXT : UI_ACCENT);
        if (warn) draw_semantic_pulse_glow(pixels, stride, refresh, 10, bd);
        fill_round_rect(pixels, stride, refresh, 10, bg);
        draw_rect_outline(pixels, stride, refresh, 10, warn ? 2 : 1, bd);
        draw_text_center(pixels, stride, refresh,
                         failed ? ptc_ui_text(PTC_UI_T_REFRESH_FAILED_NOT_SUBMITTED_PRESS_Y_OR) :
                         ptc_ui_text(PTC_UI_T_Y_CLICK_TO_REFRESH_AND_RECALCULATE_AUTOMATICALLY), 14,
                         fg);
    }
    draw_overlay_actions(pixels, stride, model,
                         model->confirm_hold_required ? ptc_ui_text(PTC_UI_T_LONG_PRESS_A_TOUCH_AND_HOLD) : ptc_ui_text(PTC_UI_T_A_CONFIRM));
}
