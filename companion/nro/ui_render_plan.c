#include "ui_render_internal.h"

void draw_plan_impact(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                      PtcUiPlanKind kind, bool dirty, UiRect panel)
{
    char impact[256], age[80], bedtime_notice[128], title[64], left_value[40], right_value[48];
    char left_note[64], right_note[64], source_text[80];
    PtcUiPlanImpactProjection projection;
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    int played = fresh && model->played_minutes_available ? model->played_minutes : -1;
    bool requires_hold = dirty && ptc_ui_plan_save_requires_hold(model, kind, ptc_ui_render_now());
    bool error = strcmp(model->result_status, "error") == 0;
    const char *state_message = NULL;
    uint32_t state_color = UI_MUTED;
    uint32_t state_background = UI_RGB(UI_BLENDED(surface_raised));
    int changed = 0;
    ptc_ui_project_plan_impact(model, kind, dirty, ptc_ui_render_now(), &projection);

    if (kind == PTC_UI_PLAN_WEEKLY) {
        for (int d = 0; d < 7; ++d)
            if (ptc_ui_day_rule_effectively_changed(model->current_week[d], model->draft_week[d])) ++changed;
    } else if (kind == PTC_UI_PLAN_HOLIDAY) {
        if (model->holiday_enabled != model->draft_holiday_enabled) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->holiday_rule, model->draft_holiday_rule)) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->makeup_workday_rule, model->draft_makeup_workday_rule)) ++changed;
    }

    draw_plan_card(pixels, stride, panel, false);
    if (dirty) {
        if (changed == 1)
            snprintf(title, sizeof(title), "%s", ptc_ui_text(kind == PTC_UI_PLAN_WEEKLY ?
                PTC_UI_T_DRAFT_ONE_DAY_CHANGED : PTC_UI_T_DRAFT_ONE_ITEM_CHANGED));
        else if (changed > 1) {
            PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("count", changed)};
            (void)ptc_ui_text_format(kind == PTC_UI_PLAN_WEEKLY ? PTC_UI_T_DRAFT_DAYS_CHANGED_NAMED :
                PTC_UI_T_DRAFT_ITEMS_CHANGED_NAMED, title, sizeof(title), args, 1);
        } else snprintf(title, sizeof(title), "%s", ptc_ui_text(PTC_UI_T_DRAFT_UNSAVED));
    } else snprintf(title, sizeof(title), ptc_ui_text(PTC_UI_T_PLAN_SAVED_ACTIVE));
    draw_text(pixels, stride, panel.x + 20, panel.y + 34, title, 19,
              dirty ? (requires_hold ? UI_DANGER : UI_WARNING) : UI_SUCCESS);
    const char *sub = model->disable_flag_present ? (ptc_ui_text(PTC_UI_T_CONTROL_DISABLED_PLAN_READ_ONLY)) :
        (model->waiting ? (ptc_ui_text(PTC_UI_T_SAVING_PLEASE_WAIT)) :
         (model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR ? (ptc_ui_text(PTC_UI_T_PRESS_TO_FINISH_THEN_SAVE)) :
          (requires_hold ? (fresh ? (ptc_ui_text(PTC_UI_T_QUOTA_WILL_EXHAUST_HOLD_A_TO_CONFIRM))
                                  : (ptc_ui_text(PTC_UI_T_STATUS_PENDING_HOLD_A_TO_CONFIRM))) :
           (dirty ? (ptc_ui_text(PTC_UI_T_PRESS_TO_SAVE_AND_APPLY_TO_CONSOLE))
                  : (ptc_ui_text(PTC_UI_T_SYNCHRONIZED_WITH_CONSOLE_POLICY))))));
    draw_text(pixels, stride, panel.x + 20, panel.y + 58, sub, 13,
              requires_hold ? UI_DANGER : UI_RGB(UI_BLENDED(text_secondary)));

    UiRect result = {panel.x + 20, panel.y + 74, panel.width - 40, 96};
    UiRect left = {result.x + 10, result.y + 8, 138, result.height - 16};
    UiRect right = {result.x + result.width - 148, result.y + 8, 138, result.height - 16};
    fill_round_rect(pixels, stride, result, 12, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, result, 12, 1, UI_RGB(UI_BLENDED(border_control)));

    if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
        draw_status_symbol(pixels, stride, result.x + 24, result.y + 48, UI_MUTED, 1);
        draw_text(pixels, stride, result.x + 44, result.y + 30, ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_UNCHANGED), 16, UI_INK);
        draw_wrapped_text(pixels, stride, result.x + 44, result.y + 54, impact,
                          12, result.width - 58, 17, 2, UI_MUTED);
    } else {
        bool current_state = projection.state == PTC_UI_PLAN_IMPACT_CURRENT;
        PtcDayRule left_rule = current_state ? projection.after.rule : projection.before.rule;
        PtcDayRule right_rule = projection.after.rule;
        draw_text_center(pixels, stride, (UiRect){left.x, left.y, left.width, 22},
                         current_state ? (ptc_ui_text(PTC_UI_T_TODAY_QUOTA)) : (ptc_ui_text(PTC_UI_T_CURRENT_QUOTA_2)), 13, UI_MUTED);
        if (left_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(left_value, sizeof(left_value), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else snprintf(left_value, sizeof(left_value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)left_rule.minutes);
        snprintf(left_note, sizeof(left_note), "%s",
                 ptc_ui_effective_rule_label(current_state ? projection.after.source : projection.before.source));
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 24, left.width, 30}, left_value, 19,
                         left_rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT);
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 55, left.width, 20}, left_note, 11, UI_MUTED);

        if (!current_state) {
            int arrow_x = result.x + result.width / 2;
            int arrow_y = result.y + 40;
            draw_line(pixels, stride, arrow_x - 13, arrow_y, arrow_x + 10, arrow_y, 2, UI_MUTED);
            draw_line(pixels, stride, arrow_x + 5, arrow_y - 5, arrow_x + 10, arrow_y, 2, UI_MUTED);
            draw_line(pixels, stride, arrow_x + 5, arrow_y + 5, arrow_x + 10, arrow_y, 2, UI_MUTED);
        }

        if (current_state) {
            draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 22}, ptc_ui_text(PTC_UI_T_REMAINING_TODAY), 13, UI_MUTED);
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
            else if (projection.remaining_available) snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_D_MIN), projection.remaining_minutes);
            else snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING));
            if (played >= 0) snprintf(right_note, sizeof(right_note), ptc_ui_text(PTC_UI_T_USED_D_MIN), played);
            else snprintf(right_note, sizeof(right_note), ptc_ui_text(PTC_UI_T_SHOWN_AFTER_REFRESH));
        } else {
            draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 22}, ptc_ui_text(PTC_UI_T_QUOTA_AFTER_SAVE), 13, UI_MUTED);
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
            else snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)right_rule.minutes);
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_note, sizeof(right_note), ptc_ui_text(PTC_UI_T_EST_REMAINING_NO_LIMIT));
            else if (projection.remaining_available) snprintf(right_note, sizeof(right_note), ptc_ui_text(PTC_UI_T_EST_REMAINING_D_MIN), projection.remaining_minutes);
            else snprintf(right_note, sizeof(right_note), ptc_ui_text(PTC_UI_T_REMAINING_PENDING_REFRESH));
        }
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 24, right.width, 30}, right_value, 19,
                         right_rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS :
                         (projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER :
                          (projection.state == PTC_UI_PLAN_IMPACT_UNKNOWN ? UI_MUTED : UI_SUCCESS)));
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 55, right.width, 20}, right_note, 11,
                         projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_MUTED);
    }

    snprintf(source_text, sizeof(source_text), "%s  %s", dirty ? (ptc_ui_text(PTC_UI_T_SOURCE_AFTER_SAVE))
                                                               : (ptc_ui_text(PTC_UI_T_TODAY_SOURCE)),
             ptc_ui_effective_rule_label(projection.after.source));
    {
        int source_width = measure_text(source_text, 12) + 24;
        if (source_width > panel.width - 40) source_width = panel.width - 40;
        UiRect source = {panel.x + 20, panel.y + 184, source_width, 26};
        fill_round_rect(pixels, stride, source, 13, UI_ACCENT_SOFT);
        draw_text_center(pixels, stride, source, source_text, 12, UI_ACCENT);
    }

    if (error) {
        state_message = ptc_ui_text(PTC_UI_T_SAVE_FAILED_DRAFT_KEPT_VERIFY_AND_RETRY);
        state_color = UI_DANGER; state_background = UI_DANGER_SOFT;
    } else if (model->disable_flag_present) {
        state_message = ptc_ui_text(PTC_UI_T_EMERGENCY_STOP_ACTIVE_PLAN_READ_ONLY);
        state_color = UI_DANGER; state_background = UI_DANGER_SOFT;
    } else if (model->waiting) {
        state_message = ptc_ui_text(PTC_UI_T_SAVING_DRAFT_PLEASE_WAIT);
        state_color = UI_WARNING; state_background = UI_WARNING_SOFT;
    } else if (model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
        state_message = ptc_ui_text(PTC_UI_T_COMPLETE_MINUTE_INPUT_BEFORE_SAVING);
        state_color = UI_WARNING; state_background = UI_WARNING_SOFT;
    } else if (!fresh && projection.quota_changes_today) {
        state_message = requires_hold ? (ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED_HOLD_A_TO_CONFIRM_TODAY)) :
            (ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED_WILL_RECHECK_IMPACT_UPON_SAVING));
        state_color = UI_WARNING; state_background = UI_WARNING_SOFT;
    } else if (requires_hold) {
        state_message = ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_WILL_EXPIRE_IMMEDIATELY_HOLD);
        state_color = UI_DANGER; state_background = UI_DANGER_SOFT;
    }

    UiRect conclusion = {panel.x + 20, panel.y + 216, panel.width - 40, 58};
    if (state_message) {
        fill_round_rect(pixels, stride, conclusion, 10, state_background);
        draw_wrapped_text(pixels, stride, conclusion.x + 12, conclusion.y + 22, state_message,
                          12, conclusion.width - 24, 16, 2, state_color);
    } else if (projection.state == PTC_UI_PLAN_IMPACT_CURRENT) {
        draw_text(pixels, stride, conclusion.x, conclusion.y + 14, ptc_ui_text(PTC_UI_T_CURRENT_STATUS_2), 12, UI_MUTED);
        draw_text(pixels, stride, conclusion.x, conclusion.y + 34,
                  ptc_ui_text(PTC_UI_T_PLAN_IS_SAVED_NO_PENDING_QUOTA_DRAFTS), 12, UI_RGB(UI_BLENDED(text_secondary)));
    } else if (projection.state != PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        draw_text(pixels, stride, conclusion.x, conclusion.y + 14, ptc_ui_text(PTC_UI_T_TODAY_AFTER_SAVE), 12, UI_MUTED);
        ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
        draw_wrapped_text(pixels, stride, conclusion.x, conclusion.y + 34, impact,
                          12, conclusion.width, 16, 2, UI_RGB(UI_BLENDED(text_secondary)));
    }

    ptc_ui_format_bedtime_quota_notice(model, ptc_ui_render_now(), bedtime_notice, sizeof(bedtime_notice));
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, panel.x + 20, panel.y + panel.height - 14,
              bedtime_notice[0] ? bedtime_notice : age, 12,
              bedtime_notice[0] ? UI_DANGER : status_age_color(model));
}

void draw_plan_impact_compact(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                              PtcUiPlanKind kind, UiRect panel)
{
    char title[64], impact[256], left_value[40], right_value[48], source_text[80];
    PtcUiPlanImpactProjection projection;
    int changed = 0;
    ptc_ui_project_plan_impact(model, kind, true, ptc_ui_render_now(), &projection);

    if (kind == PTC_UI_PLAN_WEEKLY) {
        for (int day = 0; day < 7; ++day) {
            if (ptc_ui_day_rule_effectively_changed(model->current_week[day], model->draft_week[day])) ++changed;
        }
        if (changed == 1) snprintf(title, sizeof(title), "%s", ptc_ui_text(PTC_UI_T_DRAFT_ONE_DAY_CHANGED));
        else if (changed > 1) {
            PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("count", changed)};
            (void)ptc_ui_text_format(PTC_UI_T_DRAFT_DAYS_CHANGED_NAMED, title, sizeof(title), args, 1);
        }
        else snprintf(title, sizeof(title), ptc_ui_text(PTC_UI_T_DRAFT_UNSAVED));
    } else if (kind == PTC_UI_PLAN_SCHEDULED) {
        if (ptc_ui_scheduled_dirty(model)) ++changed;
        if (changed > 0) snprintf(title, sizeof(title), ptc_ui_text(PTC_UI_T_DRAFT_SPECIFIC_DATE_QUOTA_CHANGED));
        else snprintf(title, sizeof(title), ptc_ui_text(PTC_UI_T_DRAFT_UNSAVED));
    } else {
        if (model->holiday_enabled != model->draft_holiday_enabled) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->holiday_rule, model->draft_holiday_rule)) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->makeup_workday_rule, model->draft_makeup_workday_rule)) ++changed;
        if (changed == 1) snprintf(title, sizeof(title), "%s", ptc_ui_text(PTC_UI_T_DRAFT_ONE_ITEM_CHANGED));
        else if (changed > 1) {
            PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("count", changed)};
            (void)ptc_ui_text_format(PTC_UI_T_DRAFT_ITEMS_CHANGED_NAMED, title, sizeof(title), args, 1);
        }
        else snprintf(title, sizeof(title), ptc_ui_text(PTC_UI_T_DRAFT_UNSAVED));
    }

    draw_plan_card(pixels, stride, panel, false);
    draw_text(pixels, stride, panel.x + 20, panel.y + 32, title, 18, UI_WARNING);

    UiRect result = {panel.x + 20, panel.y + 50, panel.width - 40, 104};
    int col_w = panel.width >= 440 ? 168 : 132;
    UiRect left = {result.x + 10, result.y + 8, col_w, result.height - 16};
    UiRect right = {result.x + result.width - 10 - col_w, result.y + 8, col_w, result.height - 16};
    fill_round_rect(pixels, stride, result, 12, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, result, 12, 1, UI_RGB(UI_BLENDED(border_control)));
    if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
        draw_status_symbol(pixels, stride, result.x + 24, result.y + 52, UI_MUTED, 1);
        draw_text(pixels, stride, result.x + 44, result.y + 34, ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_UNCHANGED), 15, UI_INK);
        draw_wrapped_text(pixels, stride, result.x + 44, result.y + 58, impact,
                          12, result.width - 56, 17, 2, UI_MUTED);
    } else {
        draw_text_center(pixels, stride, (UiRect){left.x, left.y, left.width, 22}, ptc_ui_text(PTC_UI_T_CURRENT_QUOTA_2), 12, UI_MUTED);
        if (projection.before.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(left_value, sizeof(left_value), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else snprintf(left_value, sizeof(left_value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)projection.before.rule.minutes);
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 24, left.width, 34}, left_value, 19,
                         projection.before.rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT);
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 61, left.width, 18},
                         ptc_ui_effective_rule_label(projection.before.source), 11, UI_MUTED);

        int arrow_x = result.x + result.width / 2;
        int arrow_y = result.y + 42;
        draw_line(pixels, stride, arrow_x - 11, arrow_y, arrow_x + 9, arrow_y, 2, UI_MUTED);
        draw_line(pixels, stride, arrow_x + 4, arrow_y - 5, arrow_x + 9, arrow_y, 2, UI_MUTED);
        draw_line(pixels, stride, arrow_x + 4, arrow_y + 5, arrow_x + 9, arrow_y, 2, UI_MUTED);

        draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 22}, ptc_ui_text(PTC_UI_T_QUOTA_AFTER_SAVE), 12, UI_MUTED);
        if (projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)projection.after.rule.minutes);
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 24, right.width, 34}, right_value, 19,
                         projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS :
                         (projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_ACCENT));
        if (projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_EST_REMAINING_NO_LIMIT));
        else if (projection.remaining_available) snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_EST_REMAINING_D_MIN_2), projection.remaining_minutes);
        else snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_REMAINING_PENDING_REFRESH));
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 61, right.width, 18}, right_value, 11,
                         projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_MUTED);
    }

    snprintf(source_text, sizeof(source_text), "%s  %s", ptc_ui_text(PTC_UI_T_SOURCE_AFTER_SAVE), ptc_ui_effective_rule_label(projection.after.source));
    {
        int source_width = measure_text(source_text, 12) + 24;
        if (source_width > panel.width - 40) source_width = panel.width - 40;
        UiRect source = {panel.x + 20, panel.y + 174, source_width, 26};
        fill_round_rect(pixels, stride, source, 13, UI_ACCENT_SOFT);
        draw_text_center(pixels, stride, source, source_text, 12, UI_ACCENT);
    }
}

static void draw_weekly_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    int slot;
    char detail[64];
    char freshness[64];
    uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
    format_status_age(model, freshness, sizeof(freshness));
    draw_rect_outline(pixels, stride, (UiRect){54, 178, 26, 24}, 4, 2, UI_ACCENT);
    draw_line(pixels, stride, 54, 186, 80, 186, 2, UI_ACCENT);
    draw_line(pixels, stride, 61, 174, 61, 181, 3, UI_ACCENT);
    draw_line(pixels, stride, 73, 174, 73, 181, 3, UI_ACCENT);
    draw_text(pixels, stride, 92, 198, ptc_ui_text(PTC_UI_T_WEEKLY_BASE_PLAN_CLICK_MODE_OR_QUOTA), 17, UI_MUTED);
    for (slot = 0; slot < 7; ++slot) {
        int day = ptc_ui_weekday_for_display_slot(slot);
        bool selected = slot == model->weekly_grid_slot && model->selected_index == 0;
        bool today = model->status_loaded && day == weekday;
        UiRect card = to_uirect(ptc_ui_weekly_day_rect(slot));
        UiRect header = to_uirect(ptc_ui_weekly_day_header_rect(slot));
        UiRect mode = to_uirect(ptc_ui_weekly_day_mode_rect(slot));
        UiRect minutes = to_uirect(ptc_ui_weekly_day_minutes_rect(slot));
        bool limited = model->draft_week[day].mode == PTC_RULE_MODE_LIMIT;
        draw_plan_card(pixels, stride, card, selected && !model->parent_footer_focused);
        draw_text_center(pixels, stride, header, ptc_ui_weekday_label((unsigned)day), 19, UI_INK);
        fill_round_rect(pixels, stride, (UiRect){mode.x + 6, mode.y + 6, mode.width - 12, 32}, 14,
                        model->disable_flag_present ? UI_BORDER :
                        (limited ? UI_ACCENT : UI_SUCCESS));
        draw_text_center(pixels, stride, (UiRect){mode.x + 6, mode.y + 6, mode.width - 12, 32},
                         limited ? (ptc_ui_text(PTC_UI_T_LIMIT)) : (ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED)), 15,
                         model->disable_flag_present ? UI_DISABLED : UI_ON_ACCENT);
        UiRect today_rect = {card.x, minutes.y + 8, card.width, 22};
        draw_text_center(pixels, stride, today_rect,
                         today ? (ptc_ui_text(PTC_UI_T_TODAY_3)) : " ", 13, today ? UI_SUCCESS : UI_MUTED);
        if (limited) {
            snprintf(detail, sizeof(detail), "%u", (unsigned int)model->draft_week[day].minutes);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 36, minutes.width, 46}, detail, 30,
                             model->disable_flag_present ? UI_DISABLED : UI_ACCENT);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 84, minutes.width, 24}, ptc_ui_text(PTC_UI_T_MIN), 14, UI_MUTED);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 124, minutes.width, 22}, ptc_ui_text(PTC_UI_T_A_CLICK_EDIT), 12,
                             model->disable_flag_present ? UI_DISABLED : UI_MUTED);
        } else {
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 50, minutes.width, 36}, ptc_ui_text(PTC_UI_T_NO_LIMIT), 18,
                             model->disable_flag_present ? UI_DISABLED : UI_SUCCESS);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 94, minutes.width, 24}, ptc_ui_text(PTC_UI_T_ALL_DAY_ACCESS), 13, UI_MUTED);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 124, minutes.width, 22}, ptc_ui_text(PTC_UI_T_CLICK_FOR_TIP), 11, UI_DISABLED);
        }
        /* 每日容量柱状直方图 (Weekly Capacity Histogram Bar) */
        int bar_w = card.width - 24;
        int bar_x = card.x + 12;
        int bar_y = card.y + card.height - 14;
        UiRect bar_bg = {bar_x, bar_y, bar_w, 5};
        fill_round_rect(pixels, stride, bar_bg, 2, UI_BORDER);
        if (limited) {
            int fill_w = (int)((int64_t)bar_w * (model->draft_week[day].minutes > 180 ? 180 : model->draft_week[day].minutes) / 180);
            if (fill_w < 4 && model->draft_week[day].minutes > 0) fill_w = 4;
            uint32_t bar_col = model->disable_flag_present ? UI_DISABLED :
                (model->draft_week[day].minutes <= 30 ? UI_WARNING : UI_ACCENT);
            if (fill_w > 0) {
                fill_round_rect(pixels, stride, (UiRect){bar_x, bar_y, fill_w, 5}, 2, bar_col);
            }
        } else {
            fill_round_rect(pixels, stride, bar_bg, 2, model->disable_flag_present ? UI_DISABLED : UI_SUCCESS);
        }
    }
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_WEEKLY, model->weekly_dirty,
                     (UiRect){838, 176, 388, 452});
    draw_candidate_button(pixels, stride, ptc_ui_weekly_page_mode_rect(), ptc_ui_text(PTC_UI_T_X_TOGGLE_MODE),
                           UI_PAGE, UI_ACCENT, model->selected_index == 1,
                           model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_bulk_rect(), ptc_ui_text(PTC_UI_T_BATCH_SETUP),
                          UI_PAGE, UI_ACCENT, model->selected_index == 2,
                          model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_discard_rect(), ptc_ui_text(PTC_UI_T_ZL_DISCARD),
                           UI_PAGE, UI_INK, model->selected_index == 3,
                           !model->weekly_dirty);
    bool weekly_hold = model->weekly_dirty && ptc_ui_plan_save_requires_hold(model, PTC_UI_PLAN_WEEKLY, ptc_ui_render_now());
    draw_candidate_button(pixels, stride, ptc_ui_weekly_save_rect(),
                           model->disable_flag_present ? (ptc_ui_text(PTC_UI_T_READ_ONLY)) : (model->waiting ? (ptc_ui_text(PTC_UI_T_SAVING_2)) : (model->weekly_dirty ? (weekly_hold ? (ptc_ui_text(PTC_UI_T_SAVE_CONFIRM)) : (ptc_ui_text(PTC_UI_T_SAVE_DRAFT_2))) : (ptc_ui_text(PTC_UI_T_SAVED)))),
                           weekly_hold ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT, model->selected_index == 4,
                           !model->weekly_dirty || model->disable_flag_present || model->waiting);
}

static void draw_holiday_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {838, 176, 388, 340};
    UiRect top_card = to_uirect(ptc_ui_holiday_card_rect(0));
    bool is_en = (ptc_ui_language_get_resolved() == PTC_UI_LANGUAGE_ENGLISH);
    const char *titles[] = {ptc_ui_text(PTC_UI_T_STATUTORY_VACATION), ptc_ui_text(PTC_UI_T_ADJUSTED_WORKING_DAYS)};
    const char *descriptions[] = {ptc_ui_text(PTC_UI_T_VACATION_DATES_FOR_MAJOR_STATUTORY_HOLIDAYS), ptc_ui_text(PTC_UI_T_THE_MAKE_UP_DATE_DUE_TO_HOLIDAYS)};
    char line[160];
    char minutes_str[64];
    bool disabled = model->disable_flag_present;
    bool top_selected = model->selected_index == 0;
    fill_round_rect(pixels, stride, top_card, 16, disabled ? UI_PAGE : (top_selected ? UI_ACCENT_SOFT : UI_SURFACE));
    draw_rect_outline(pixels, stride, top_card, 16, top_selected ? 3 : 1, top_selected ? UI_ACCENT : UI_BORDER);
    draw_text(pixels, stride, top_card.x + 20, top_card.y + 32, ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAY_RULES), 22, UI_INK);
    draw_text(pixels, stride, top_card.x + 20, top_card.y + 60,
              ptc_ui_text(PTC_UI_T_AUTOMATICALLY_APPLY_STATUTORY_HOLIDAY_MAKEUP_WORKDAY_RULES), 14, UI_MUTED);
    {
        const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_BUILT_IN_U_V_U),
                 (unsigned int)info->last_year, (unsigned int)info->version);
        draw_text(pixels, stride, top_card.x + (is_en ? 450 : 498), top_card.y + 34, line, 14,
                  model->calendar_update_warning ? UI_DANGER : UI_SUCCESS);
    }
    draw_toggle_switch(pixels, stride, to_uirect(ptc_ui_holiday_enable_rect()), model->draft_holiday_enabled,
                       top_selected, disabled, NULL, NULL);

    for (int index = 0; index < 2; ++index) {
        PtcDayRule rule = index == 0 ? model->draft_holiday_rule : model->draft_makeup_workday_rule;
        UiRect card = to_uirect(ptc_ui_holiday_card_rect(index + 1));
        UiRect mode = to_uirect(ptc_ui_holiday_mode_rect(index));
        UiRect minutes = to_uirect(ptc_ui_holiday_minutes_rect(index));
        bool selected = model->selected_index == index + 1;
        bool limited = rule.mode == PTC_RULE_MODE_LIMIT;
        draw_plan_card(pixels, stride, card, selected && !model->parent_footer_focused);
        draw_text(pixels, stride, card.x + 18, card.y + 34, titles[index], 21, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, card.x + 18, card.y + 62, descriptions[index], 13, UI_MUTED);
        fill_round_rect(pixels, stride, mode, 18,
                        disabled ? UI_BORDER : (limited ? UI_ACCENT : UI_SUCCESS));
        draw_text_center(pixels, stride, mode, limited ? (ptc_ui_text(PTC_UI_T_LIMIT)) : (ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED)), 14,
                         disabled ? UI_DISABLED : UI_ON_ACCENT);
        fill_round_rect(pixels, stride, minutes, 12, disabled || !limited ? UI_RAISED : UI_RAISED);
        if (limited) {
            snprintf(minutes_str, sizeof(minutes_str), ptc_ui_text(PTC_UI_T_U_MIN_U_HR_U_MIN), (unsigned int)rule.minutes,
                     (unsigned int)rule.minutes / 60, (unsigned int)rule.minutes % 60);
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 46, minutes_str, 22,
                      disabled ? UI_DISABLED : UI_RGB(UI_BLENDED(accent)));
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 88, ptc_ui_text(PTC_UI_T_A_CLICK_TO_EDIT_QUOTA), 14,
                      disabled ? UI_DISABLED : UI_MUTED);
        } else {
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 46, ptc_ui_text(PTC_UI_T_NO_LIMIT), 22, UI_DISABLED);
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 88, ptc_ui_text(PTC_UI_T_CLICK_TIP_SWITCH_TO_LIMIT_FIRST), 14, UI_DISABLED);
        }
    }
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(3), ptc_ui_text(PTC_UI_T_X_TOGGLE_MODE),
                           UI_PAGE, UI_ACCENT, model->selected_index == 3, disabled);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(4), ptc_ui_text(PTC_UI_T_ZL_DISCARD),
                           UI_PAGE, UI_INK, model->selected_index == 4,
                           !model->holiday_dirty);
    bool holiday_hold = model->holiday_dirty && ptc_ui_plan_save_requires_hold(model, PTC_UI_PLAN_HOLIDAY, ptc_ui_render_now());
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(5),
                           disabled ? (ptc_ui_text(PTC_UI_T_DISABLED_READ_ONLY)) : (model->waiting ? (ptc_ui_text(PTC_UI_T_SAVING)) :
                           (model->holiday_dirty ? (holiday_hold ? (ptc_ui_text(PTC_UI_T_SAVE_CONFIRM)) : (ptc_ui_text(PTC_UI_T_SAVE_DRAFT_2))) : (ptc_ui_text(PTC_UI_T_SAVED)))),
                           holiday_hold ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT, model->selected_index == 5,
                           disabled || model->waiting || !model->holiday_dirty);
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_HOLIDAY, model->holiday_dirty, panel);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_calendar_rect(), ptc_ui_text(PTC_UI_T_VIEW_HOLIDAY_CALENDAR),
                           UI_ACCENT_SOFT, UI_ACCENT, model->selected_index == 6, false);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_manage_rect(),
                           ptc_ui_text(PTC_UI_T_REGION_CALENDAR_MANAGER),
                           UI_ACCENT_SOFT, UI_ACCENT, model->selected_index == 7, false);
}

const char *bedtime_override_label(PtcBedtimeOverrideMode mode)
{
    if (mode == PTC_BEDTIME_OVERRIDE_DISABLED) return ptc_ui_text(PTC_UI_T_OFF);
    if (mode == PTC_BEDTIME_OVERRIDE_CUSTOM) return ptc_ui_text(PTC_UI_T_CUSTOM);
    return ptc_ui_text(PTC_UI_T_INHERIT);
}

static void draw_bedtime_window_value(char *out, size_t out_size, const PtcBedtimeWindow *window)
{
    if (!window->enabled) snprintf(out, out_size, ptc_ui_text(PTC_UI_T_OFF));
    else snprintf(out, out_size, ptc_ui_text(PTC_UI_T_02U_02U_NEXT_DAY_02U_02U),
        (unsigned int)(window->start_minute / 60), (unsigned int)(window->start_minute % 60),
        (unsigned int)(window->end_minute / 60), (unsigned int)(window->end_minute % 60));
}

static void draw_bedtime_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    bool is_en = (ptc_ui_language_get_resolved() == PTC_UI_LANGUAGE_ENGLISH);
    const char *SECTIONS[] = {ptc_ui_text(PTC_UI_T_WEEKLY_2), ptc_ui_text(PTC_UI_T_HOLIDAYS), ptc_ui_text(PTC_UI_T_GO_TO_BED_ON_THE_SPECIFIED_DATE)};
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    char line[128];
    int64_t raw_now = ptc_ui_render_now();
    uint16_t minute_of_day = ptc_ui_render_minute_of_day(raw_now);
    for (int i = 0; i < 3; ++i) {
        UiRect rect = to_uirect(ptc_ui_bedtime_section_rect(i));
        bool selected = model->bedtime_section == (PtcUiBedtimeSection)i;
        fill_round_rect(pixels, stride, rect, 12, selected ? UI_ACCENT : UI_RAISED);
        draw_text_center(pixels, stride, rect, SECTIONS[i], 18, selected ? UI_ON_ACCENT : UI_INK);
        if (selected && model->bedtime_section_focused) draw_focus_ring(pixels, stride, rect, 12);
    }

    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    PtcUiBedtimeImpact bedtime_impact = ptc_ui_bedtime_save_impact(model,
        minute_of_day, (int64_t)raw_now);
    bool bedtime_save_danger = model->bedtime_dirty &&
        (bedtime_impact == PTC_UI_BEDTIME_IMPACT_RESTRICT ||
         bedtime_impact == PTC_UI_BEDTIME_IMPACT_UNKNOWN);
    const char *section_name = model->bedtime_section == PTC_UI_BEDTIME_WEEKLY
        ? (ptc_ui_text(PTC_UI_T_WEEKLY_BEDTIME)) : (model->bedtime_section == PTC_UI_BEDTIME_CALENDAR
            ? (ptc_ui_text(PTC_UI_T_HOLIDAY_BEDTIME)) : (ptc_ui_text(PTC_UI_T_SPECIFIC_DATE_BEDTIME)));

    /* 独立醒目的就寝管控总闸卡片 (Bedtime Master Circuit Breaker Card) */
    UiRect master_card = to_uirect(ptc_ui_bedtime_master_switch_rect());
    draw_card_shadow(pixels, stride, master_card, 16);
    fill_round_rect(pixels, stride, master_card, 14, UI_RGB(UI_BLENDED(surface)));
    if (bedtime_enforcing) {
        int phase = get_breathing_phase();
        draw_rect_outline(pixels, stride, master_card, 14, 2,
                          UI_RGB(ui_mix_rgb(UI_BLENDED(danger), 0xFF9A8A, phase * 4)));
    } else {
        draw_rect_outline(pixels, stride, master_card, 14, 1, UI_RGB(UI_BLENDED(border_control)));
    }
    if (model->bedtime_master_focused) draw_focus_ring(pixels, stride, master_card, 14);
    draw_text(pixels, stride, master_card.x + 18, master_card.y + 24, ptc_ui_text(PTC_UI_T_BEDTIME_SWITCH), 20, UI_INK);

    bool is_skipped_active = ptc_ui_status_is_fresh(model, raw_now) &&
        (bedtime_impact == PTC_UI_BEDTIME_IMPACT_SKIPPED ||
         ptc_ui_bedtime_skip_matches_policy(model, draft));

    {
        bool switch_dirty = draft->enabled != model->bedtime_policy.enabled;
        int saved_w = is_en ? 134 : 126;
        int draft_w = is_en ? 208 : 185;
        UiRect saved_pill = {master_card.x + 205, master_card.y + 8, saved_w, 22};
        UiRect draft_pill = {master_card.x + 205 + saved_w + 10, master_card.y + 8, draft_w, 22};
        fill_round_rect(pixels, stride, saved_pill, 6, UI_RAISED);
        draw_text_center(pixels, stride, saved_pill,
                         model->bedtime_policy.enabled ? (ptc_ui_text(PTC_UI_T_ACTIVE_ON))
                                                       : (ptc_ui_text(PTC_UI_T_ACTIVE_OFF)),
                         12, UI_INK);
        if (switch_dirty) {
            fill_round_rect(pixels, stride, draft_pill, 6, UI_WARNING_SOFT);
            draw_text_center(pixels, stride, draft_pill,
                             draft->enabled ? (ptc_ui_text(PTC_UI_T_DRAFT_ON_PRESS))
                                            : (ptc_ui_text(PTC_UI_T_DRAFT_OFF_PRESS)),
                             12, UI_WARNING);
        }
        if (bedtime_enforcing) {
            int active_x = switch_dirty ? (draft_pill.x + draft_pill.width + 10) : (saved_pill.x + saved_pill.width + 10);
            int active_w = is_en ? 130 : 110;
            UiRect active_pill = {active_x, master_card.y + 8, active_w, 22};
            fill_round_rect(pixels, stride, active_pill, 6, UI_DANGER_SOFT);
            draw_text_center(pixels, stride, active_pill, ptc_ui_text(PTC_UI_T_RESTRICTING), 12, UI_DANGER);
        }

        draw_text(pixels, stride, master_card.x + 18, master_card.y + 45,
                  bedtime_enforcing && !draft->enabled
                      ? (ptc_ui_text(PTC_UI_T_SET_TO_OFF_DRAFT_SAVE_WITH_TO))
                      : (draft->enabled
                          ? (switch_dirty
                              ? (ptc_ui_text(PTC_UI_T_SET_TO_ON_DRAFT_SAVE_WITH_TO))
                              : (ptc_ui_text(PTC_UI_T_MASTER_ON_WEEKLY_HOLIDAY_AND_SPECIFIC_DATE)))
                          : (switch_dirty
                              ? (ptc_ui_text(PTC_UI_T_SET_TO_OFF_DRAFT_SAVE_WITH_TO_2))
                              : (ptc_ui_text(PTC_UI_T_MASTER_OFF_ALL_BEDTIME_RULES_PAUSED_SAVED)))),
                  13, bedtime_enforcing ? UI_DANGER : UI_MUTED);
    }
    UiRect toggle_rect = {master_card.x + master_card.width - 76, master_card.y + (master_card.height - 30) / 2, 60, 30};
    draw_toggle_switch(pixels, stride, toggle_rect, draft->enabled, false, model->disable_flag_present, NULL, NULL);
    draw_text(pixels, stride, master_card.x + master_card.width - (is_en ? 206 : 196), master_card.y + 24,
               ptc_ui_text(PTC_UI_T_A_TOGGLE), 13, UI_ACCENT);

    {
        bool fresh = ptc_ui_status_is_fresh(model, raw_now);
        bool can_skip = fresh && !model->bedtime_skipped &&
            ((model->bedtime_active && model->bedtime_window_instance_id != 0) ||
             model->bedtime_next_available);
        bool can_restore = fresh && ptc_ui_bedtime_skip_matches_policy(model, &model->bedtime_policy);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_manage_rect(0),
            ptc_ui_text(PTC_UI_T_Y_SKIP_ONCE), UI_PAGE, UI_ACCENT, false,
            !can_skip || model->waiting || model->disable_flag_present);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_manage_rect(1),
            ptc_ui_text(PTC_UI_T_X_RESTORE_ONCE), UI_PAGE, can_restore ? UI_DANGER : UI_INK, false,
            !can_restore || model->waiting || model->disable_flag_present);
    }

    /* 页面状态、预测与风险统一在右侧卡片中 */
    {
        UiRect eval_card = {838, 276, 388, 350};
        uint32_t state_color = strcmp(model->result_status, "error") == 0 ? UI_DANGER :
            (model->waiting || model->bedtime_dirty ? UI_WARNING :
             (bedtime_enforcing ? UI_DANGER : UI_SUCCESS));
        uint32_t state_bg = state_color == UI_DANGER ? UI_DANGER_SOFT :
            (state_color == UI_WARNING ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
        char state_text[96];
        snprintf(state_text, sizeof(state_text), ptc_ui_text(PTC_UI_T_CURRENTLY_ACTIVE_S_S),
            model->bedtime_policy.enabled ? ptc_ui_text(PTC_UI_T_ON_2) : ptc_ui_text(PTC_UI_T_CLOSE),
            strcmp(model->result_status, "error") == 0 ? ptc_ui_text(PTC_UI_T_SAVE_FAILED_DRAFT_RETAINED) :
            (model->waiting ? ptc_ui_text(PTC_UI_T_SAVING_3) :
             (model->bedtime_dirty ? ptc_ui_text(PTC_UI_T_DRAFT_TO_BE_SAVED) : ptc_ui_text(PTC_UI_T_RULE_SAVED))));
        fill_round_rect(pixels, stride, eval_card, 12, UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, eval_card, 12, 1, UI_RGB(UI_BLENDED(border_control)));
        char preview_title[64];
        snprintf(preview_title, sizeof(preview_title), "%s%s",
                 model->bedtime_dirty ? (ptc_ui_text(PTC_UI_T_DRAFT_PREVIEW)) : "", section_name);
        draw_text(pixels, stride, eval_card.x + 16, eval_card.y + 24,
                  preview_title, 17, UI_RGB(UI_BLENDED(text_primary)));
        UiRect state_pill = {eval_card.x + 16, eval_card.y + 36, eval_card.width - 32, 32};
        fill_round_rect(pixels, stride, state_pill, 9, state_bg);
        draw_rect_outline(pixels, stride, state_pill, 9, 1, state_color);
        draw_text_center(pixels, stride, state_pill, state_text, 14, state_color);

        PtcRules eval_rules;
        memset(&eval_rules, 0, sizeof(eval_rules));
        eval_rules.bedtime = *draft;
        PtcBedtimeEvaluation eval = ptc_bedtime_evaluate(
            &eval_rules, model->day_index, ptc_weekday_from_day_index(model->day_index), minute_of_day);

        char forecast_line1[96];
        char forecast_line2[96];
        uint32_t f1_color = UI_RGB(UI_BLENDED(text_primary));

        if (!draft->enabled) {
            snprintf(forecast_line1, sizeof(forecast_line1), ptc_ui_text(PTC_UI_T_S_BEDTIME_DISABLED_NO_NIGHTTIME_LIMITS),
                     model->bedtime_dirty ? (ptc_ui_text(PTC_UI_T_FORECAST_AFTER_SAVE))
                                          : (ptc_ui_text(PTC_UI_T_CURRENT_STATE)));
            f1_color = UI_MUTED;
        } else if (bedtime_impact == PTC_UI_BEDTIME_IMPACT_RESTRICT) {
            snprintf(forecast_line1, sizeof(forecast_line1), ptc_ui_text(PTC_UI_T_IN_BEDTIME_HOURS_SAVING_WILL_RESTRICT_IMMEDIATELY));
            f1_color = UI_DANGER;
        } else if (bedtime_impact == PTC_UI_BEDTIME_IMPACT_UNKNOWN) {
            snprintf(forecast_line1, sizeof(forecast_line1), ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED_MAY_RESTRICT_IMMEDIATELY_HOLD_TO));
            f1_color = UI_DANGER;
        } else if (is_skipped_active) {
            uint16_t skipped_start = model->bedtime_skipped_window_available
                ? model->bedtime_skipped_start_minute : eval.start_minute;
            uint16_t skipped_end = model->bedtime_skipped_window_available
                ? model->bedtime_skipped_end_minute : eval.end_minute;
            snprintf(forecast_line1, sizeof(forecast_line1), ptc_ui_text(PTC_UI_T_SKIPPED_THIS_TIME_02U_02U_TO_NEXT),
                (unsigned int)(skipped_start / 60), (unsigned int)(skipped_start % 60),
                (unsigned int)(skipped_end / 60), (unsigned int)(skipped_end % 60));
            f1_color = UI_SUCCESS;
        } else if (eval.active) {
            snprintf(forecast_line1, sizeof(forecast_line1), ptc_ui_text(PTC_UI_T_CURRENT_BEDTIME_RESTRICTION_ACTIVE));
            f1_color = UI_DANGER;
        } else {
            PtcEffectiveBedtime eff_today = ptc_bedtime_resolve_start_day(
                &eval_rules, model->day_index, ptc_weekday_from_day_index(model->day_index));
            if (eff_today.window.enabled) {
                int diff_m = (int)eff_today.window.start_minute - (int)minute_of_day;
                if (diff_m < 0) diff_m += 1440;
                snprintf(forecast_line1, sizeof(forecast_line1), ptc_ui_text(PTC_UI_T_BEDTIME_STARTS_AT_02U_02U_TONIGHT_ABOUT),
                         (unsigned int)(eff_today.window.start_minute / 60),
                         (unsigned int)(eff_today.window.start_minute % 60),
                         (unsigned int)(diff_m / 60), (unsigned int)(diff_m % 60));
                f1_color = UI_SUCCESS;
            } else {
                snprintf(forecast_line1, sizeof(forecast_line1), ptc_ui_text(PTC_UI_T_TONIGHT_WINDOW_CLOSED_NO_LIMITS));
                f1_color = UI_MUTED;
            }
        }
        draw_wrapped_text(pixels, stride, eval_card.x + 16, eval_card.y + 92,
                          forecast_line1, 13, eval_card.width - 32, 19, 2, f1_color);

        int enabled_count = 0;
        int total_span_m = 0;
        for (int d = 0; d < 7; ++d) {
            if (draft->week[d].enabled) {
                enabled_count++;
                total_span_m += (int)(draft->week[d].end_minute + 1440 - draft->week[d].start_minute) % 1440;
            }
        }
        if (draft->enabled) {
            snprintf(forecast_line2, sizeof(forecast_line2), ptc_ui_text(PTC_UI_T_WEEKLY_STATS_D_7_DAYS_ON_U),
                     enabled_count, (unsigned int)(total_span_m / 60));
        } else {
            snprintf(forecast_line2, sizeof(forecast_line2), ptc_ui_text(PTC_UI_T_S_D_7_DAYS_PRESET),
                     model->bedtime_dirty ? (ptc_ui_text(PTC_UI_T_PAUSED_WEEKLY_AFTER_SAVE))
                                          : (ptc_ui_text(PTC_UI_T_WEEKLY_DISABLED_INACTIVE)),
                     enabled_count);
        }
        draw_text(pixels, stride, eval_card.x + 16, eval_card.y + 138,
                  forecast_line2, 13, UI_MUTED);

        const char *cal_str = draft->calendar_enabled
            ? (draft->enabled ? (ptc_ui_text(PTC_UI_T_HOLIDAY_CALENDAR_ON))
                              : (ptc_ui_text(PTC_UI_T_HOLIDAY_CALENDAR_INACTIVE)))
            : (ptc_ui_text(PTC_UI_T_HOLIDAYS_FOLLOW_WEEKLY_PLAN));
        const char *sch_str = draft->scheduled_override.present
            ? (draft->enabled ? (ptc_ui_text(PTC_UI_T_SPECIFIC_DATE_ACTIVE))
                              : (ptc_ui_text(PTC_UI_T_SPECIFIC_DATE_INACTIVE)))
            : (ptc_ui_text(PTC_UI_T_SPECIFIC_DATE_NOT_SET));
        char special_line[128];
        snprintf(special_line, sizeof(special_line), ptc_ui_text(PTC_UI_T_SPECIAL_RULES_S_S), cal_str, sch_str);
        draw_wrapped_text(pixels, stride, eval_card.x + 16, eval_card.y + 166,
                          special_line, 12, eval_card.width - 32, 18, 2, UI_MUTED);

        UiRect risk = {eval_card.x + 16, eval_card.y + 206, eval_card.width - 32, 60};
        fill_round_rect(pixels, stride, risk, 8,
            model->bedtime_official_setting_confirmed && model->bedtime_overlay_verified
                ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
        draw_wrapped_text(pixels, stride, risk.x + 12, risk.y + 20,
            model->bedtime_official_setting_confirmed
                ? (model->bedtime_overlay_verified
                    ? (ptc_ui_text(PTC_UI_T_OFFICIAL_SUSPEND_SETTING_CONFIRMED_OVERLAY_VERIFIED))
                    : (ptc_ui_text(PTC_UI_T_OFFICIAL_SUSPEND_CONFIRMED_SAVE_ACCEPTS_UNVERIFIED_OVERLAY)))
                : (ptc_ui_text(PTC_UI_T_BEFORE_ENABLING_CONFIRM_NINTENDO_PARENTAL_CONTROLS_HAS)),
            12, risk.width - 24, 18, 2,
            model->bedtime_official_setting_confirmed && model->bedtime_overlay_verified
                ? UI_SUCCESS : UI_WARNING);

        draw_text(pixels, stride, eval_card.x + 16,
                  eval_card.y + eval_card.height - 18,
                  ptc_ui_text(PTC_UI_T_TOGGLE_SAVE_ZL_DISCARD),
                  14, UI_ACCENT);
    }

    if (model->bedtime_section == PTC_UI_BEDTIME_WEEKLY) {
        int today_weekday = ptc_weekday_from_day_index(model->day_index);
        for (int slot = 0; slot < 7; ++slot) {
            int day = ptc_ui_weekday_for_display_slot(slot);
            UiRect card = to_uirect(ptc_ui_bedtime_field_rect(PTC_UI_BEDTIME_WEEKLY, slot));
            char value[64];
            int diff = day - today_weekday;
            uint16_t slot_day_idx = (uint16_t)(model->day_index + diff);
            uint16_t c_y; uint8_t c_m, c_d;
            bool has_date = ptc_date_from_day_index(slot_day_idx, &c_y, &c_m, &c_d);
            bool is_today = (day == today_weekday);
            bool is_active_day = (bedtime_enforcing && is_today);

            draw_plan_card(pixels, stride, card,
                model->selected_index == slot && !model->bedtime_section_focused &&
                !model->parent_footer_focused && !model->bedtime_master_focused);

            if (is_today) {
                uint32_t today_bg = is_active_day ? UI_DANGER_SOFT :
                    (is_skipped_active ? UI_SUCCESS_SOFT : UI_ACCENT_SOFT);
                uint32_t today_color = is_active_day ? UI_DANGER :
                    (is_skipped_active ? UI_SUCCESS : UI_ACCENT);
                fill_round_rect(pixels, stride, card, 16, today_bg);
                draw_rect_outline(pixels, stride, card, 16, 2, today_color);
                if (model->selected_index == slot && !model->bedtime_section_focused &&
                    !model->parent_footer_focused && !model->bedtime_master_focused) {
                    draw_focus_ring(pixels, stride, card, 16);
                }
                int pill_w = is_en ? (is_skipped_active ? 88 : (is_active_day ? 58 : 50)) : (is_skipped_active ? 74 : 50);
                UiRect today_pill = {card.x + (card.width - pill_w) / 2, card.y + 6, pill_w, 18};
                fill_round_rect(pixels, stride, today_pill, 5, today_color);
                draw_text_center(pixels, stride, today_pill,
                                 is_active_day ? (ptc_ui_text(PTC_UI_T_ACTIVE_3)) : (is_skipped_active ? (ptc_ui_text(PTC_UI_T_SKIP)) : (ptc_ui_text(PTC_UI_T_TODAY_4))),
                                 11, UI_ON_ACCENT);
            }

            draw_text_center(pixels, stride, (UiRect){card.x, card.y + (is_today ? 25 : 13), card.width, 20},
                             ptc_ui_weekday_label((unsigned)day), is_today ? 17 : 16,
                             is_today ? (is_active_day ? UI_DANGER : (is_skipped_active ? UI_SUCCESS : UI_ACCENT)) : UI_INK);

            if (has_date) {
                char date_str[16];
                snprintf(date_str, sizeof(date_str), "%02u/%02u", c_m, c_d);
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + (is_today ? 44 : 33), card.width, 18},
                                 date_str, 12,
                                 is_today ? (is_active_day ? UI_DANGER : (is_skipped_active ? UI_SUCCESS : UI_ACCENT)) : UI_MUTED);
            }

            draw_bedtime_window_value(value, sizeof(value), &draft->week[day]);
            if (draft->week[day].enabled) {
                snprintf(line, sizeof(line), "%02u:%02u",
                    (unsigned int)(draft->week[day].start_minute / 60),
                    (unsigned int)(draft->week[day].start_minute % 60));
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 64, card.width, 22}, line, 18,
                                 draft->enabled ? (is_active_day ? UI_DANGER : (is_skipped_active && is_today ? UI_SUCCESS : UI_ACCENT)) : UI_MUTED);
                snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_TO_02U_02U),
                    (unsigned int)(draft->week[day].end_minute / 60),
                    (unsigned int)(draft->week[day].end_minute % 60));
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 88, card.width, 18}, line, 13, UI_MUTED);

                /* 开启状态微标 */
                UiRect pill = {card.x + (card.width - 48) / 2, card.y + 114, 48, 20};
                fill_round_rect(pixels, stride, pill, 6,
                                is_active_day ? UI_DANGER_SOFT : (draft->enabled ? (is_skipped_active && is_today ? UI_SUCCESS_SOFT : UI_ACCENT_SOFT) : UI_RAISED));
                draw_text_center(pixels, stride, pill,
                                 is_active_day ? (ptc_ui_text(PTC_UI_T_ADJUST_BADGE_ACTIVE)) : (is_skipped_active && is_today ? (ptc_ui_text(PTC_UI_T_SKIPPED_2)) : (draft->enabled ? (ptc_ui_text(PTC_UI_T_ON)) : (ptc_ui_text(PTC_UI_T_PAUSED)))), 12,
                                 is_active_day ? UI_DANGER : (is_skipped_active && is_today ? UI_SUCCESS : (draft->enabled ? UI_ACCENT : UI_MUTED)));
            } else {
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 74, card.width, 24}, ptc_ui_text(PTC_UI_T_OFF), 18, UI_MUTED);
                UiRect pill = {card.x + (card.width - 48) / 2, card.y + 114, 48, 20};
                fill_round_rect(pixels, stride, pill, 6, UI_RAISED);
                draw_text_center(pixels, stride, pill, ptc_ui_text(PTC_UI_T_OFF), 12, UI_MUTED);
            }
            draw_text_center(pixels, stride, (UiRect){card.x, card.y + 148, card.width, 18}, ptc_ui_text(PTC_UI_T_A_SET), 11, UI_MUTED);
        }
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 7), ptc_ui_text(PTC_UI_T_COPY_TO_WEEKDAYS),
            UI_PAGE, UI_ACCENT, model->selected_index == 7 && !model->bedtime_section_focused && !model->bedtime_master_focused, model->disable_flag_present);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 8), ptc_ui_text(PTC_UI_T_COPY_TO_WEEKENDS),
            UI_PAGE, UI_ACCENT, model->selected_index == 8 && !model->bedtime_section_focused && !model->bedtime_master_focused, model->disable_flag_present);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 9), ptc_ui_text(PTC_UI_T_ZL_DISCARD),
            UI_PAGE, UI_INK, model->selected_index == 9 && !model->bedtime_section_focused && !model->bedtime_master_focused, !model->bedtime_dirty);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 10),
            bedtime_save_danger ? (ptc_ui_text(PTC_UI_T_SAVE_CONFIRM)) : (ptc_ui_text(PTC_UI_T_SAVE_PAGE)),
            bedtime_save_danger ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT,
            model->selected_index == 10 && !model->bedtime_section_focused && !model->bedtime_master_focused,
            !model->bedtime_dirty || model->disable_flag_present || model->waiting);

        /* 左下角就寝规则说明面板（与右侧预测卡片底部平齐对齐至 y=626） */
        UiRect bedtime_guide = {54, 526, 752, 100};
        fill_round_rect(pixels, stride, bedtime_guide, 12, UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, bedtime_guide, 12, 1, UI_RGB(UI_BLENDED(border_control)));
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 22,
                   ptc_ui_text(PTC_UI_T_HOW_BEDTIME_WORKS), 15, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 44,
                   ptc_ui_text(PTC_UI_T_RESTRICTS_IMMEDIATELY_AT_BEDTIME_EVEN_IF_QUOTA), 13, UI_MUTED);
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 66,
                   ptc_ui_text(PTC_UI_T_FOR_EXCEPTIONS_SELECT_SKIP_TONIGHT_BEDTIME_IN), 13, UI_MUTED);
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 88,
                   ptc_ui_text(PTC_UI_T_CAN_SPAN_OVERNIGHT_E_G_22_00), 13, UI_MUTED);
    } else if (model->bedtime_section == PTC_UI_BEDTIME_CALENDAR) {
        UiRect master = to_uirect(ptc_ui_bedtime_field_rect(1, 0));
        draw_plan_card(pixels, stride, master, model->selected_index == 0 && !model->bedtime_section_focused && !model->bedtime_master_focused);
        draw_text(pixels, stride, master.x + 20, master.y + 40, ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAY_CALENDAR), 20, UI_INK);
        if (!draft->enabled) {
            draw_text(pixels, stride, master.x + (is_en ? 260 : 190), master.y + 40,
                draft->calendar_enabled ? (ptc_ui_text(PTC_UI_T_ON_INACTIVE)) : (ptc_ui_text(PTC_UI_T_OFF_2)), 18, UI_MUTED);
        } else {
            draw_text(pixels, stride, master.x + (is_en ? 260 : 190), master.y + 40,
                draft->calendar_enabled ? (ptc_ui_text(PTC_UI_T_ON)) : (ptc_ui_text(PTC_UI_T_OFF)), 18,
                draft->calendar_enabled ? UI_SUCCESS : UI_MUTED);
        }
        UiRect cal_toggle = {master.x + master.width - 76, master.y + (master.height - 28) / 2, 58, 28};
        draw_toggle_switch(pixels, stride, cal_toggle, draft->calendar_enabled,
                           model->selected_index == 0 && !model->bedtime_section_focused && !model->bedtime_master_focused,
                           !draft->enabled || model->disable_flag_present, NULL, NULL);
        draw_text(pixels, stride, master.x + master.width - (is_en ? 180 : 165), master.y + 39, ptc_ui_text(PTC_UI_T_A_TOGGLE_2), 13, UI_MUTED);

        for (int i = 0; i < 2; ++i) {
            const PtcBedtimeSpecialRule *rule = i == 0 ? &draft->holiday_rule : &draft->makeup_workday_rule;
            UiRect card = to_uirect(ptc_ui_bedtime_field_rect(1, i + 1));
            char value[64];
            draw_plan_card(pixels, stride, card, model->selected_index == i + 1 && !model->bedtime_section_focused && !model->bedtime_master_focused);
            draw_text(pixels, stride, card.x + 18, card.y + 30,
                i == 0 ? (ptc_ui_text(PTC_UI_T_STATUTORY_HOLIDAY)) : (ptc_ui_text(PTC_UI_T_MAKEUP_WORKDAY)), 19, UI_INK);
            draw_bedtime_window_value(value, sizeof(value), &rule->window);
            if (!draft->enabled) {
                snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_S_S_INACTIVE),
                    bedtime_override_label(rule->mode),
                    rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM ? value : (ptc_ui_text(PTC_UI_T_USE_MATCHING_MODE)));
            } else {
                snprintf(line, sizeof(line), "%s  |  %s", bedtime_override_label(rule->mode),
                    rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM ? value : (ptc_ui_text(PTC_UI_T_USE_MATCHING_MODE)));
            }
            draw_text(pixels, stride, card.x + 18, card.y + 72, line, 15, UI_MUTED);
        }
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(1, 3), ptc_ui_text(PTC_UI_T_ZL_DISCARD),
            UI_PAGE, UI_INK, model->selected_index == 3 && !model->bedtime_section_focused && !model->bedtime_master_focused, !model->bedtime_dirty);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(1, 4),
            bedtime_save_danger ? (ptc_ui_text(PTC_UI_T_SAVE_CONFIRM)) : (ptc_ui_text(PTC_UI_T_SAVE_PAGE)),
            bedtime_save_danger ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT,
            model->selected_index == 4 && !model->bedtime_section_focused && !model->bedtime_master_focused,
            !model->bedtime_dirty || model->disable_flag_present || model->waiting);

        /* 节假日日历规则与总闸状态指引面板（底部与右侧预测卡平齐至 y=626） */
        UiRect cal_guide = {54, 526, 752, 100};
        fill_round_rect(pixels, stride, cal_guide, 12,
                        !draft->enabled ? UI_WARNING_SOFT : UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, cal_guide, 12, 1,
                          !draft->enabled ? UI_WARNING : UI_RGB(UI_BLENDED(border_control)));
        if (!draft->enabled) {
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 24,
                      ptc_ui_text(PTC_UI_T_BEDTIME_SWITCH_IS_OFF), 15, UI_WARNING);
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 48,
                      ptc_ui_text(PTC_UI_T_WHEN_DISABLED_HOLIDAY_AND_MAKEUP_BEDTIME_RULES), 13, UI_MUTED);
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 70,
                      ptc_ui_text(PTC_UI_T_SETTINGS_ARE_SAVED_PRESS_OR_TURN_ON), 13, UI_MUTED);
        } else {
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 24,
                      ptc_ui_text(PTC_UI_T_HOLIDAY_CALENDAR_RULE_PRIORITIES), 15, UI_RGB(UI_BLENDED(text_primary)));
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 48,
                      ptc_ui_text(PTC_UI_T_WHEN_ENABLED_HOLIDAYS_AND_WORKDAYS_TAKE_PRIORITY), 13, UI_MUTED);
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 70,
                      ptc_ui_text(PTC_UI_T_SUGGEST_STRICT_BEDTIME_ON_MAKEUP_WORKDAYS_RELAX), 13, UI_MUTED);
        }
    } else {
        const PtcBedtimeScheduledOverride *scheduled = &draft->scheduled_override;
        uint32_t duration = scheduled->end_day_index >= scheduled->start_day_index
            ? (uint32_t)scheduled->end_day_index - scheduled->start_day_index + 1u : 1u;
        const char *labels[] = {ptc_ui_text(PTC_UI_T_GO_TO_BED_ON_THE_SPECIFIED_DATE), ptc_ui_text(PTC_UI_T_START_DATE), ptc_ui_text(PTC_UI_T_NUMBER_OF_DAYS_TO_LAST), ptc_ui_text(PTC_UI_T_BEDTIME_RULES)};
        char values[4][96];
        if (!draft->enabled) {
            snprintf(values[0], sizeof(values[0]), "%s", scheduled->present ? (ptc_ui_text(PTC_UI_T_ON_INACTIVE_2)) : (ptc_ui_text(PTC_UI_T_OFF)));
        } else {
            snprintf(values[0], sizeof(values[0]), "%s", scheduled->present ? (ptc_ui_text(PTC_UI_T_ON)) : (ptc_ui_text(PTC_UI_T_OFF)));
        }
        {
            uint16_t year;
            uint8_t month, day;
            if (ptc_date_from_day_index(scheduled->start_day_index, &year, &month, &day))
                snprintf(values[1], sizeof(values[1]), "%04u-%02u-%02u", year, month, day);
            else snprintf(values[1], sizeof(values[1]), ptc_ui_text(PTC_UI_T_WAITING_FOR_DATE));
        }
        snprintf(values[2], sizeof(values[2]), ptc_ui_text(PTC_UI_T_U_DAYS), (unsigned int)duration);
        if (scheduled->rule.mode == PTC_BEDTIME_OVERRIDE_CUSTOM) {
            char value[64];
            draw_bedtime_window_value(value, sizeof(value), &scheduled->rule.window);
            snprintf(values[3], sizeof(values[3]), "%s  %s%s",
                bedtime_override_label(scheduled->rule.mode), value,
                draft->enabled ? "" : (ptc_ui_text(PTC_UI_T_INACTIVE)));
        } else {
            snprintf(values[3], sizeof(values[3]), "%s%s",
                bedtime_override_label(scheduled->rule.mode),
                draft->enabled ? "" : (ptc_ui_text(PTC_UI_T_INACTIVE)));
        }
        for (int i = 0; i < 4; ++i) {
            UiRect row = to_uirect(ptc_ui_bedtime_field_rect(2, i));
            draw_plan_card(pixels, stride, row, model->selected_index == i && !model->bedtime_section_focused && !model->bedtime_master_focused);
            if (i == 0) {
                draw_text(pixels, stride, row.x + 18, row.y + 31, labels[0], 17, UI_MUTED);
                if (!draft->enabled) {
                    draw_text(pixels, stride, row.x + (is_en ? 200 : 150), row.y + 31,
                              scheduled->present ? (ptc_ui_text(PTC_UI_T_ON_INACTIVE_2)) : (ptc_ui_text(PTC_UI_T_OFF)), 18, UI_MUTED);
                } else {
                    draw_text(pixels, stride, row.x + (is_en ? 200 : 150), row.y + 31,
                              scheduled->present ? (ptc_ui_text(PTC_UI_T_ON)) : (ptc_ui_text(PTC_UI_T_OFF)), 18,
                              scheduled->present ? UI_SUCCESS : UI_MUTED);
                }
                UiRect sched_toggle = {row.x + row.width - 76, row.y + (row.height - 28) / 2, 58, 28};
                draw_toggle_switch(pixels, stride, sched_toggle, scheduled->present,
                                   model->selected_index == 0 && !model->bedtime_section_focused && !model->bedtime_master_focused,
                                   !draft->enabled || model->disable_flag_present, NULL, NULL);
                draw_text(pixels, stride, row.x + row.width - (is_en ? 180 : 165), row.y + 31, ptc_ui_text(PTC_UI_T_A_TOGGLE_2), 13, UI_MUTED);
            } else {
                draw_text(pixels, stride, row.x + 18, row.y + 31, labels[i], 17, UI_MUTED);
                draw_text(pixels, stride, row.x + (is_en ? 200 : 240), row.y + 31, values[i], 18, UI_INK);
                if (i == 1 || i == 2)
                    draw_text(pixels, stride, row.x + (is_en ? 450 : 484), row.y + 31,
                              ptc_ui_text(PTC_UI_T_A_EDIT_ZL_ZR_7_DAYS), 11, UI_MUTED);
            }
        }

        /* 指定日期规则与总闸状态指引面板（与保存/放弃按钮同高对齐） */
        UiRect sched_guide = {54, 526, 360, 50};
        fill_round_rect(pixels, stride, sched_guide, 12,
                        !draft->enabled ? UI_WARNING_SOFT : UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, sched_guide, 12, 1,
                          !draft->enabled ? UI_WARNING : UI_RGB(UI_BLENDED(border_control)));
        if (!draft->enabled) {
            draw_text(pixels, stride, sched_guide.x + 14, sched_guide.y + 20,
                      ptc_ui_text(PTC_UI_T_BEDTIME_SWITCH_IS_OFF), 13, UI_WARNING);
            draw_text(pixels, stride, sched_guide.x + 14, sched_guide.y + 38,
                      ptc_ui_text(PTC_UI_T_CURRENTLY_INACTIVE_ACTIVATES_WHEN_BEDTIME_SWITCH_IS), 12, UI_MUTED);
        } else {
            draw_text(pixels, stride, sched_guide.x + 14, sched_guide.y + 20,
                      ptc_ui_text(PTC_UI_T_SPECIFIC_DATE_BEDTIME_PRIORITY), 13, UI_RGB(UI_BLENDED(text_primary)));
            draw_text(pixels, stride, sched_guide.x + 14, sched_guide.y + 38,
                      ptc_ui_text(PTC_UI_T_TAKES_PRIORITY_OVER_HOLIDAYS_AND_WEEKLY_PLANS), 12, UI_MUTED);
        }

        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(2, 4), ptc_ui_text(PTC_UI_T_ZL_DISCARD),
            UI_PAGE, UI_INK, model->selected_index == 4 && !model->bedtime_section_focused && !model->bedtime_master_focused, !model->bedtime_dirty);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(2, 5),
            bedtime_save_danger ? (ptc_ui_text(PTC_UI_T_SAVE_CONFIRM)) : (ptc_ui_text(PTC_UI_T_SAVE_PAGE)),
            bedtime_save_danger ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT,
            model->selected_index == 5 && !model->bedtime_section_focused && !model->bedtime_master_focused,
            !model->bedtime_dirty || model->disable_flag_present || model->waiting);
    }
}

static const char *bedtime_source_short(PtcBedtimeSource source)
{
    switch (source) {
    case PTC_BEDTIME_SOURCE_SCHEDULED_OVERRIDE: return ptc_ui_text(PTC_UI_T_DATE);
    case PTC_BEDTIME_SOURCE_STATUTORY_HOLIDAY: return ptc_ui_text(PTC_UI_T_HOL);
    case PTC_BEDTIME_SOURCE_MAKEUP_WORKDAY: return ptc_ui_text(PTC_UI_T_WORK);
    case PTC_BEDTIME_SOURCE_WEEKLY:
    default: return ptc_ui_text(PTC_UI_T_WKLY);
    }
}

static void draw_time_plan_preview(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    bool is_en = (ptc_ui_language_get_resolved() == PTC_UI_LANGUAGE_ENGLISH);
    UiRect panel = {824, 172, 402, 452};
    int index;
    fill_round_rect(pixels, stride, panel, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    draw_text(pixels, stride, panel.x + 16, panel.y + 26, ptc_ui_text(PTC_UI_T_7_DAY_PLAN_BEDTIME_FORECAST), 17, UI_INK);

    if (!model->forecast_available) {
        draw_text(pixels, stride, panel.x + 16, panel.y + 110, ptc_ui_text(PTC_UI_T_REFRESH_STATUS_TO_VIEW_FORECAST_DATA), 15, UI_MUTED);
        return;
    }

    for (index = 0; index < (int)PTC_RESULT_FORECAST_DAYS; ++index) {
        const PtcResultForecastDay *day = &model->forecast[index];
        PtcRules rules;
        ptc_ui_build_plan_rules(model, PTC_UI_PLAN_WEEKLY, &rules);
        PtcEffectiveBedtime bedtime = ptc_bedtime_resolve_start_day(
            &rules,
            day->day_index, ptc_weekday_from_day_index(day->day_index));
        uint8_t weekday = ptc_weekday_from_day_index(day->day_index);
        UiRect row = to_uirect(ptc_ui_forecast_day_row_rect(index));
        bool is_today = (index == 0);

        bool is_focused = (!model->parent_footer_focused && model->selected_index == index + 7);

        if (is_focused) {
            fill_round_rect(pixels, stride, row, 8, UI_ACCENT_SOFT);
            draw_focus_ring(pixels, stride, row, 8);
        } else if (is_today) {
            fill_round_rect(pixels, stride, row, 8, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, row, 8, 1, UI_ACCENT);
        } else {
            fill_round_rect(pixels, stride, row, 8, UI_RGB(UI_BLENDED(surface)));
            draw_rect_outline(pixels, stride, row, 8, 1, UI_RGB(UI_BLENDED(border_control)));
        }

        /* 1. 日期与星期 */
        char date_label[32];
        if (is_today) {
            snprintf(date_label, sizeof(date_label), ptc_ui_text(PTC_UI_T_TODAY_S), ptc_ui_weekday_label(weekday));
        } else if (index == 1) {
            snprintf(date_label, sizeof(date_label), ptc_ui_text(PTC_UI_T_TMRW_S), ptc_ui_weekday_label(weekday));
        } else {
            snprintf(date_label, sizeof(date_label), "%s (D+%d)", ptc_ui_weekday_label(weekday), index);
        }
        draw_text(pixels, stride, row.x + 10, row.y + 27, date_label, 14,
                  is_today ? UI_ACCENT : UI_INK);

        /* 2. 额度与微进度条 */
        int quota_x = row.x + (is_en ? 104 : 100);
        if (day->mode == PTC_RULE_MODE_UNLIMITED) {
            draw_text(pixels, stride, quota_x, row.y + 20, ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED), 14, UI_SUCCESS);
            fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 28, 64, 4}, 2, UI_SUCCESS);
        } else {
            char q_str[24];
            snprintf(q_str, sizeof(q_str), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)day->minutes);
            draw_text(pixels, stride, quota_x, row.y + 20, q_str, 14, UI_INK);
            int bar_w = 64;
            int fill_w = (int)((int64_t)bar_w * (day->minutes > 180 ? 180 : day->minutes) / 180);
            if (fill_w < 3 && day->minutes > 0) fill_w = 3;
            uint32_t bar_col = day->minutes <= 30 ? UI_WARNING : UI_ACCENT;
            fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 28, bar_w, 4}, 2, UI_BORDER);
            if (fill_w > 0) {
                fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 28, fill_w, 4}, 2, bar_col);
            }
        }

        /* 3. 规则来源胶囊徽标 */
        UiRect badge = {row.x + 178, row.y + 11, 56, 22};
        const char *badge_label = ptc_ui_text(PTC_UI_T_WEEKLY);
        uint32_t badge_bg = UI_RAISED;
        uint32_t badge_color = UI_MUTED;
        if (day->rule_source && strcmp(day->rule_source, "statutory_holiday") == 0) {
            badge_label = ptc_ui_text(PTC_UI_T_HOLIDAY);
            badge_bg = UI_WARNING_SOFT;
            badge_color = UI_WARNING;
        } else if (day->rule_source && strcmp(day->rule_source, "makeup_workday") == 0) {
            badge_label = ptc_ui_text(PTC_UI_T_MAKEUP);
            badge_bg = UI_ACCENT_SOFT;
            badge_color = UI_ACCENT;
        } else if (day->rule_source && strcmp(day->rule_source, "scheduled_override") == 0) {
            badge_label = ptc_ui_text(PTC_UI_T_CUSTOM_2);
            badge_bg = UI_SUCCESS_SOFT;
            badge_color = UI_SUCCESS;
        } else if (day->rule_source && strcmp(day->rule_source, "today_override") == 0) {
            badge_label = ptc_ui_text(PTC_UI_T_TODAY_5);
            badge_bg = UI_WARNING_SOFT;
            badge_color = UI_WARNING;
        }
        fill_round_rect(pixels, stride, badge, 4, badge_bg);
        draw_text_center(pixels, stride, badge, badge_label, 11, badge_color);

        /* 4. 就寝窗口 */
        int bt_x = row.x + (is_en ? 240 : 246);
        if (!model->bedtime_policy.enabled || !bedtime.window.enabled) {
            draw_text(pixels, stride, bt_x, row.y + 27, ptc_ui_text(PTC_UI_T_NO_BEDTIME), 13, UI_MUTED);
        } else {
            char bt_buf[32];
            snprintf(bt_buf, sizeof(bt_buf), "%02u:%02u (%s)",
                     (unsigned int)(bedtime.window.start_minute / 60),
                     (unsigned int)(bedtime.window.start_minute % 60),
                     bedtime_source_short(bedtime.source));
            draw_text(pixels, stride, bt_x, row.y + 27, bt_buf, 13, UI_MUTED);
        }

        /* 5. 右侧微指示符 */
        draw_text(pixels, stride, row.x + row.width - 16, row.y + 26, ">", 13,
                  is_focused ? UI_ACCENT : UI_MUTED);
    }

    /* 6. 底部并行策略胶囊条 */
    UiRect eye_capsule = {panel.x + 14, panel.y + panel.height - 46, (panel.width - 34) / 2, 22};
    bool eye_on = model->eye_care_policy.enabled;
    fill_round_rect(pixels, stride, eye_capsule, 4, eye_on ? UI_SUCCESS_SOFT : UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, eye_capsule, 4, 1, eye_on ? UI_SUCCESS : UI_BORDER);
    char eye_text[48];
    if (eye_on) {
        snprintf(eye_text, sizeof(eye_text), ptc_ui_text(PTC_UI_T_EYE_CAPSULE_ACTIVE),
                 model->eye_care_policy.play_minutes, model->eye_care_policy.rest_minutes);
    } else {
        snprintf(eye_text, sizeof(eye_text), "%s", ptc_ui_text(PTC_UI_T_EYE_CAPSULE_OFF));
    }
    draw_text_center(pixels, stride, eye_capsule, eye_text, 11, eye_on ? UI_SUCCESS : UI_MUTED);

    UiRect dock_capsule = {panel.x + 14 + (panel.width - 34) / 2 + 6, panel.y + panel.height - 46, (panel.width - 34) / 2, 22};
    bool dock_on = model->dock_policy.force_docked || model->dock_policy.undocked_limit_enabled;
    fill_round_rect(pixels, stride, dock_capsule, 4, dock_on ? UI_ACCENT_SOFT : UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, dock_capsule, 4, 1, dock_on ? UI_ACCENT : UI_BORDER);
    char dock_text[48];
    if (model->dock_policy.force_docked) {
        snprintf(dock_text, sizeof(dock_text), "%s", ptc_ui_text(PTC_UI_T_DOCK_CAPSULE_FORCE));
    } else if (model->dock_policy.undocked_limit_enabled) {
        snprintf(dock_text, sizeof(dock_text), ptc_ui_text(PTC_UI_T_DOCK_CAPSULE_LIMIT),
                 model->dock_policy.undocked_daily_minutes);
    } else {
        snprintf(dock_text, sizeof(dock_text), "%s", ptc_ui_text(PTC_UI_T_DOCK_CAPSULE_OFF));
    }
    draw_text_center(pixels, stride, dock_capsule, dock_text, 11, dock_on ? UI_ACCENT : UI_MUTED);

    draw_text(pixels, stride, panel.x + 14, panel.y + panel.height - 12,
              ptc_ui_text(PTC_UI_T_PRESS_A_OR_CLICK_TO_VIEW_DAILY), 11, UI_MUTED);
}

static void draw_eye_care_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const PtcEyeCarePolicy *draft = &model->draft_eye_care_policy;
    char line[128];
    int64_t raw_now = ptc_ui_render_now();
    uint16_t minute_of_day = ptc_ui_render_minute_of_day(raw_now);
    bool resting = (strcmp(model->eye_care_phase, "resting") == 0);
    bool playing = (strcmp(model->eye_care_phase, "playing") == 0);
    bool paused = (strcmp(model->eye_care_phase, "paused") == 0);

    /* 1. 独立醒目的护眼管控总开关卡片 (Eye Care Master Circuit Breaker Card) */
    UiRect master_card = to_uirect(ptc_ui_eye_care_page_master_rect());
    draw_card_shadow(pixels, stride, master_card, 16);
    fill_round_rect(pixels, stride, master_card, 14, UI_RGB(UI_BLENDED(surface)));
    if (resting) {
        int phase = get_breathing_phase();
        draw_rect_outline(pixels, stride, master_card, 14, 2,
                          UI_RGB(ui_mix_rgb(UI_BLENDED(danger), 0xFF9A8A, phase * 4)));
    } else {
        draw_rect_outline(pixels, stride, master_card, 14, 1, UI_RGB(UI_BLENDED(border_control)));
    }
    if (model->eye_care_field_focus == 0) draw_focus_ring(pixels, stride, master_card, 14);
    draw_text(pixels, stride, master_card.x + 18, master_card.y + 26, ptc_ui_text(PTC_UI_T_EYE_CARE), 20, UI_INK);

    /* Switch pills */
    {
        bool switch_dirty = draft->enabled != model->eye_care_policy.enabled;
        int saved_w = 134;
        int draft_w = 208;
        UiRect saved_pill = {master_card.x + 180, master_card.y + 12, saved_w, 24};
        UiRect draft_pill = {master_card.x + 180 + saved_w + 10, master_card.y + 12, draft_w, 24};
        fill_round_rect(pixels, stride, saved_pill, 6, UI_RAISED);
        draw_text_center(pixels, stride, saved_pill,
                         model->eye_care_policy.enabled ? (ptc_ui_text(PTC_UI_T_ACTIVE_ON))
                                                        : (ptc_ui_text(PTC_UI_T_ACTIVE_OFF)),
                         12, UI_INK);
        if (switch_dirty) {
            fill_round_rect(pixels, stride, draft_pill, 6, UI_WARNING_SOFT);
            draw_text_center(pixels, stride, draft_pill,
                             draft->enabled ? (ptc_ui_text(PTC_UI_T_DRAFT_ON_PRESS))
                                            : (ptc_ui_text(PTC_UI_T_DRAFT_OFF_PRESS)),
                             12, UI_WARNING);
        }
        int phase_x = switch_dirty ? (draft_pill.x + draft_pill.width + 12) : (saved_pill.x + saved_pill.width + 12);
        UiRect phase_pill = {phase_x, master_card.y + 12, master_card.width - (phase_x - master_card.x) - 200, 24};
        char phase_text[128];
        ptc_ui_format_eye_care_cycle(model, raw_now, phase_text, sizeof(phase_text));
        uint32_t phase_bg = resting ? UI_DANGER_SOFT : (playing ? UI_SUCCESS_SOFT : (paused ? UI_WARNING_SOFT : UI_RAISED));
        uint32_t phase_fg = resting ? UI_DANGER : (playing ? UI_SUCCESS : (paused ? UI_WARNING : UI_MUTED));
        fill_round_rect(pixels, stride, phase_pill, 6, phase_bg);
        char fitted_phase[128];
        fit_text(fitted_phase, sizeof(fitted_phase), phase_text, 12, phase_pill.width - 12);
        draw_text_center(pixels, stride, phase_pill, fitted_phase, 12, phase_fg);
    }
    UiRect toggle_rect = {master_card.x + master_card.width - 76,
                          master_card.y + (master_card.height - 30) / 2, 60, 30};
    draw_toggle_switch(pixels, stride, toggle_rect, draft->enabled, false,
                       model->disable_flag_present, NULL, NULL);
    draw_text(pixels, stride, toggle_rect.x - 92, master_card.y + 34,
              ptc_ui_text(PTC_UI_T_A_TOGGLE), 13, UI_ACCENT);

    /* 2. 时长配置卡片 (Time Configuration Card) */
    UiRect time_card = {54, 238, 1172, 160};
    draw_card_shadow(pixels, stride, time_card, 16);
    fill_round_rect(pixels, stride, time_card, 14, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, time_card, 14, 1, UI_RGB(UI_BLENDED(border_control)));

    /* Row 1: 连续游玩时长 */
    UiRect play_row = {time_card.x + 12, time_card.y + 10, time_card.width - 24, 66};
    if (model->eye_care_field_focus == 1) {
        fill_round_rect(pixels, stride, play_row, 10, UI_RGB(UI_BLENDED(surface_raised)));
        draw_focus_ring(pixels, stride, play_row, 10);
    }
    draw_text(pixels, stride, play_row.x + 14, play_row.y + 24, ptc_ui_text(PTC_UI_T_EYE_CARE_PLAY), 18, UI_INK);
    draw_text(pixels, stride, play_row.x + 14, play_row.y + 48,
              ptc_ui_text(PTC_UI_T_EYE_CARE_PLAY_SUBTITLE), 11, UI_MUTED);

    /* Quick Presets for Play */
    static const uint16_t PLAY_PRESETS[] = {20, 30, 40, 60};
    for (int p = 0; p < 4; ++p) {
        UiRect chip = to_uirect(ptc_ui_eye_care_play_preset_rect(p));
        bool active = (draft->play_minutes == PLAY_PRESETS[p]);
        snprintf(line, sizeof(line), "%u %s", PLAY_PRESETS[p], ptc_ui_text(PTC_UI_T_MIN));
        fill_round_rect(pixels, stride, chip, 8, active ? UI_ACCENT : UI_RAISED);
        draw_text_center(pixels, stride, chip, line, 13, active ? UI_ON_ACCENT : UI_INK);
        if (active) draw_rect_outline(pixels, stride, chip, 8, 1, UI_ACCENT);
    }

    /* Stepper & Value for Play */
    UiRect play_dec = to_uirect(ptc_ui_eye_care_play_dec_rect());
    UiRect play_val = to_uirect(ptc_ui_eye_care_play_value_rect());
    UiRect play_inc = to_uirect(ptc_ui_eye_care_play_inc_rect());
    fill_round_rect(pixels, stride, play_dec, 8, UI_RAISED);
    draw_text_center(pixels, stride, play_dec, "-", 18, UI_INK);
    fill_round_rect(pixels, stride, play_val, 8, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, play_val, 8, 1, UI_ACCENT);
    snprintf(line, sizeof(line), "%u %s", draft->play_minutes, ptc_ui_text(PTC_UI_T_MIN));
    draw_text_center(pixels, stride, play_val, line, 16, UI_ACCENT);
    fill_round_rect(pixels, stride, play_inc, 8, UI_RAISED);
    draw_text_center(pixels, stride, play_inc, "+", 18, UI_INK);

    /* Row 2: 单次休息时长 */
    UiRect rest_row = {time_card.x + 12, time_card.y + 82, time_card.width - 24, 66};
    if (model->eye_care_field_focus == 2) {
        fill_round_rect(pixels, stride, rest_row, 10, UI_RGB(UI_BLENDED(surface_raised)));
        draw_focus_ring(pixels, stride, rest_row, 10);
    }
    draw_text(pixels, stride, rest_row.x + 14, rest_row.y + 24, ptc_ui_text(PTC_UI_T_EYE_CARE_REST), 18, UI_INK);
    draw_text(pixels, stride, rest_row.x + 14, rest_row.y + 48,
              ptc_ui_text(PTC_UI_T_EYE_CARE_REST_SUBTITLE), 11, UI_MUTED);

    /* Quick Presets for Rest */
    static const uint16_t REST_PRESETS[] = {5, 10, 15, 20};
    for (int p = 0; p < 4; ++p) {
        UiRect chip = to_uirect(ptc_ui_eye_care_rest_preset_rect(p));
        bool active = (draft->rest_minutes == REST_PRESETS[p]);
        snprintf(line, sizeof(line), "%u %s", REST_PRESETS[p], ptc_ui_text(PTC_UI_T_MIN));
        fill_round_rect(pixels, stride, chip, 8, active ? UI_ACCENT : UI_RAISED);
        draw_text_center(pixels, stride, chip, line, 13, active ? UI_ON_ACCENT : UI_INK);
        if (active) draw_rect_outline(pixels, stride, chip, 8, 1, UI_ACCENT);
    }

    /* Stepper & Value for Rest */
    UiRect rest_dec = to_uirect(ptc_ui_eye_care_rest_dec_rect());
    UiRect rest_val = to_uirect(ptc_ui_eye_care_rest_value_rect());
    UiRect rest_inc = to_uirect(ptc_ui_eye_care_rest_inc_rect());
    fill_round_rect(pixels, stride, rest_dec, 8, UI_RAISED);
    draw_text_center(pixels, stride, rest_dec, "-", 18, UI_INK);
    fill_round_rect(pixels, stride, rest_val, 8, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, rest_val, 8, 1, UI_ACCENT);
    snprintf(line, sizeof(line), "%u %s", draft->rest_minutes, ptc_ui_text(PTC_UI_T_MIN));
    draw_text_center(pixels, stride, rest_val, line, 16, UI_ACCENT);
    fill_round_rect(pixels, stride, rest_inc, 8, UI_RAISED);
    draw_text_center(pixels, stride, rest_inc, "+", 18, UI_INK);

    /* 3. 预计休息周期样例 (Dynamic Cycle Schedule Preview) */
    UiRect preview_card = {54, 408, 1172, 108};
    draw_card_shadow(pixels, stride, preview_card, 16);
    fill_round_rect(pixels, stride, preview_card, 14, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, preview_card, 14, 1, UI_RGB(UI_BLENDED(border_control)));
    draw_text(pixels, stride, preview_card.x + 18, preview_card.y + 24,
              ptc_ui_text(PTC_UI_T_EYE_CARE_SCHEDULE_PREVIEW), 16, UI_INK);

    /* 3 dynamic cycle sample blocks */
    uint16_t cur_min = minute_of_day;
    int cycle_w = (preview_card.width - 36 - 24) / 3;
    for (int c = 0; c < 3; ++c) {
        uint16_t play_start = (uint16_t)((cur_min + c * (draft->play_minutes + draft->rest_minutes)) % 1440);
        uint16_t rest_start = (uint16_t)((play_start + draft->play_minutes) % 1440);
        uint16_t rest_end = (uint16_t)((rest_start + draft->rest_minutes) % 1440);

        UiRect block = {preview_card.x + 18 + c * (cycle_w + 12), preview_card.y + 36, cycle_w, 56};
        fill_round_rect(pixels, stride, block, 8, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, block, 8, 1, UI_BORDER);

        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_PLAY_FMT),
                 c + 1, play_start / 60, play_start % 60, rest_start / 60, rest_start % 60, draft->play_minutes);
        draw_text(pixels, stride, block.x + 10, block.y + 18, line, 12, UI_ACCENT);

        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_REST_FMT),
                 rest_start / 60, rest_start % 60, rest_end / 60, rest_end % 60, draft->rest_minutes);
        draw_text(pixels, stride, block.x + 10, block.y + 38, line, 12, UI_WARNING);
    }
    draw_text(pixels, stride, preview_card.x + 18, preview_card.y + 98,
              ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_FOOTNOTE), 11, UI_MUTED);

    /* 4. 详细逻辑说明卡片 (Operating Logic & Rules Guide) */
    UiRect guide_card = {54, 526, 1172, 112};
    draw_card_shadow(pixels, stride, guide_card, 16);
    fill_round_rect(pixels, stride, guide_card, 14, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, guide_card, 14, 1, UI_RGB(UI_BLENDED(border_control)));

    int col_w = (guide_card.width - 36 - 36) / 4;
    static const PtcUiTextId guide_titles[] = {
        PTC_UI_T_EYE_CARE_LOGIC_TITLE_1,
        PTC_UI_T_EYE_CARE_LOGIC_TITLE_2,
        PTC_UI_T_EYE_CARE_LOGIC_TITLE_3,
        PTC_UI_T_EYE_CARE_LOGIC_TITLE_4
    };
    static const PtcUiTextId guide_descs[] = {
        PTC_UI_T_EYE_CARE_LOGIC_DESC_1,
        PTC_UI_T_EYE_CARE_LOGIC_DESC_2,
        PTC_UI_T_EYE_CARE_LOGIC_DESC_3,
        PTC_UI_T_EYE_CARE_LOGIC_DESC_4
    };

    for (int g = 0; g < 4; ++g) {
        UiRect col = {guide_card.x + 18 + g * (col_w + 12), guide_card.y + 12, col_w, guide_card.height - 24};
        fill_round_rect(pixels, stride, col, 8, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, col, 8, 1, UI_BORDER);
        draw_text(pixels, stride, col.x + 10, col.y + 20, ptc_ui_text(guide_titles[g]), 12, UI_INK);
        draw_wrapped_text(pixels, stride, col.x + 10, col.y + 40, ptc_ui_text(guide_descs[g]), 11, col.width - 20, 15, 3, UI_MUTED);
    }

    /* 5. 底部操作按钮 */
    UiRect save_btn = to_uirect(ptc_ui_eye_care_page_save_rect());
    bool dirty = model->eye_care_dirty;
    fill_round_rect(pixels, stride, save_btn, 10, dirty ? UI_SUCCESS : UI_RAISED);
    draw_button_label(pixels, stride, save_btn,
                      dirty ? (ptc_ui_text(PTC_UI_T_EYE_CARE_SAVE)) : (ptc_ui_text(PTC_UI_T_RULE_SAVED)),
                      16, dirty ? UI_ON_ACCENT : UI_MUTED);
    if (model->eye_care_field_focus == 3) draw_focus_ring(pixels, stride, save_btn, 10);

    if (resting) {
        UiRect skip_btn = to_uirect(ptc_ui_eye_care_page_skip_rect());
        char label[96];
        snprintf(label, sizeof(label), "X %s", ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP));
        fill_round_rect(pixels, stride, skip_btn, 10, UI_DANGER_SOFT);
        draw_rect_outline(pixels, stride, skip_btn, 10, 1, UI_DANGER);
        draw_button_label(pixels, stride, skip_btn, label, 16, UI_DANGER);
        if (model->eye_care_field_focus == 4) draw_focus_ring(pixels, stride, skip_btn, 10);
    }
}

static void draw_dock_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const PtcDockPolicy *draft = &model->draft_dock_policy;
    int current_mode = (draft->force_docked) ? 1 : (draft->undocked_limit_enabled ? 2 : 0);

    /* --- 左侧配置卡片 --- */
    for (int i = 0; i < 3; ++i) {
        UiRect row = to_uirect(ptc_ui_dock_field_rect(i));
        bool focus = model->dock_field_focus == i;
        bool selected = (current_mode == i);
        bool lite_unsupported = (i == 1) && model->dock_supported_available && !model->dock_supported;
        bool row_disabled = model->disable_flag_present || (i == 1 && lite_unsupported);
        draw_plan_card(pixels, stride, row, focus);

        /* 单选圆圈指示器 (Radio Indicator) */
        UiRect radio = {row.x + row.width - 48, row.y + (row.height - 24) / 2, 24, 24};
        fill_round_rect(pixels, stride, radio, 12, selected ? UI_ACCENT : UI_RAISED);
        draw_rect_outline(pixels, stride, radio, 12, 2, selected ? UI_ACCENT : UI_BORDER);
        if (selected) {
            fill_round_rect(pixels, stride, (UiRect){radio.x + 6, radio.y + 6, 12, 12}, 6, UI_ON_ACCENT);
        }

        if (i == 0) {
            /* 选项 0：不限制屏幕形态 */
            draw_text(pixels, stride, row.x + 18, row.y + 30,
                ptc_ui_text(PTC_UI_T_DOCK_MODE_UNRESTRICTED), 20, row_disabled ? UI_DISABLED : (selected ? UI_ACCENT : UI_INK));
            const char *note = ptc_ui_text(PTC_UI_T_DOCK_MODE_UNRESTRICTED_HINT);
            char fitted[192];
            fit_text(fitted, sizeof(fitted), note, 13, row.width - 80);
            draw_text(pixels, stride, row.x + 18, row.y + 64, fitted, 13, UI_MUTED);
        } else if (i == 1) {
            /* 选项 1：仅允许电视大屏 */
            draw_text(pixels, stride, row.x + 18, row.y + 30,
                ptc_ui_text(PTC_UI_T_DOCK_MODE_FORCE), 20, row_disabled ? UI_DISABLED : (selected ? UI_ACCENT : UI_INK));
            const char *note = lite_unsupported ? ptc_ui_text(PTC_UI_T_DOCK_LITE) : ptc_ui_text(PTC_UI_T_DOCK_MODE_FORCE_HINT);
            char fitted[192];
            fit_text(fitted, sizeof(fitted), note, 13, row.width - 80);
            draw_text(pixels, stride, row.x + 18, row.y + 64, fitted, 13, row_disabled ? UI_DISABLED : UI_MUTED);
        } else {
            /* 选项 2：允许少量掌机应急 */
            draw_text(pixels, stride, row.x + 18, row.y + 30,
                ptc_ui_text(PTC_UI_T_DOCK_MODE_LIMIT), 20, row_disabled ? UI_DISABLED : (selected ? UI_ACCENT : UI_INK));
            
            uint16_t mins = (draft->undocked_daily_minutes == 0) ? 30 : draft->undocked_daily_minutes;
            snprintf(text, sizeof(text), "%u %s", mins, ptc_ui_text(PTC_UI_T_MINUTES));
            int tw = measure_text(text, 18);
            if (selected) {
                draw_text(pixels, stride, row.x + row.width - 70 - tw - 20, row.y + 32, "◀", 14, focus ? UI_ACCENT : UI_MUTED);
                draw_text(pixels, stride, row.x + row.width - 70 - tw, row.y + 32, text, 18, UI_ACCENT);
                draw_text(pixels, stride, row.x + row.width - 64, row.y + 32, "▶", 14, focus ? UI_ACCENT : UI_MUTED);
            } else {
                draw_text(pixels, stride, row.x + row.width - 70 - tw, row.y + 32, text, 18, UI_MUTED);
            }

            char hint_buf[192];
            snprintf(hint_buf, sizeof(hint_buf), ptc_ui_text(PTC_UI_T_DOCK_MODE_LIMIT_HINT), mins);
            char fitted[192];
            fit_text(fitted, sizeof(fitted), hint_buf, 13, row.width - 80);
            draw_text(pixels, stride, row.x + 18, row.y + 64, fitted, 13, selected ? UI_ACCENT : UI_MUTED);
        }
    }

    /* --- 右侧 Bento 仪表盘 (方案 1) --- */
    UiRect info = {824, 230, 402, 378};

    /* 1. 顶部 Bento 卡片：屏幕形态与视力健康守护 (824, 230, 402, 74) */
    UiRect bento_top = {info.x, info.y, info.width, 74};
    fill_round_rect(pixels, stride, bento_top, 12, UI_SURFACE);
    draw_rect_outline(pixels, stride, bento_top, 12, 1, UI_BORDER);

    bool is_docked = fresh && strcmp(model->operation_mode, "docked") == 0;
    bool is_undocked = fresh && strcmp(model->operation_mode, "undocked") == 0;
    const char *mode_title = !fresh ? ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM) :
        (is_docked ? ptc_ui_text(PTC_UI_T_DOCK_TV) : (is_undocked ? ptc_ui_text(PTC_UI_T_DOCK_HANDHELD) : ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM)));
    uint32_t mode_color = !fresh ? UI_MUTED : (is_docked ? UI_SUCCESS : UI_ACCENT);

    draw_text_bold(pixels, stride, bento_top.x + 16, bento_top.y + 26, mode_title, 17, mode_color);

    /* 视力守护胶囊徽章 */
    const char *eye_badge_text = ptc_ui_text(PTC_UI_T_DOCK_EYE_CARE_BADGE);
    int eb_w = measure_text(eye_badge_text, 11) + 16;
    if (eb_w < 70) eb_w = 70;
    UiRect eb_rect = {bento_top.x + bento_top.width - eb_w - 14, bento_top.y + 12, eb_w, 20};
    fill_round_rect(pixels, stride, eb_rect, 5, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, eb_rect, 5, 1, UI_ACCENT);
    draw_text_center(pixels, stride, eb_rect, eye_badge_text, 11, UI_ACCENT);

    /* 屏幕使用态势小字 */
    const char *sub_note = !fresh ? ptc_ui_text(PTC_UI_T_PRESS_Y_TO_REFRESH_STATUS) :
        (is_docked ? ptc_ui_text(PTC_UI_T_DOCK_TV_ACTIVE_NOTE) : ptc_ui_text(PTC_UI_T_DOCK_HANDHELD_ACTIVE_NOTE));
    draw_text(pixels, stride, bento_top.x + 16, bento_top.y + 54, sub_note, 12, is_undocked && model->dock_policy.undocked_limit_enabled ? UI_WARNING : UI_MUTED);

    /* 2. 中部 Bento 卡片：掌机小屏额度态势与倒计时 (824, 314, 402, 106) */
    UiRect bento_mid = {info.x, info.y + 84, info.width, 106};
    fill_round_rect(pixels, stride, bento_mid, 12, UI_SURFACE);
    draw_rect_outline(pixels, stride, bento_mid, 12, 1, model->dock_restriction_active ? UI_DANGER : UI_BORDER);

    draw_text(pixels, stride, bento_mid.x + 16, bento_mid.y + 20,
        ptc_ui_text(PTC_UI_T_DOCK_HANDHELD_REMAINING_NAMED), 13, UI_MUTED);

    if (model->dock_waived_today) {
        draw_text_bold(pixels, stride, bento_mid.x + 16, bento_mid.y + 50,
            ptc_ui_text(PTC_UI_T_DOCK_WAIVED), 16, UI_SUCCESS);
        draw_text(pixels, stride, bento_mid.x + 16, bento_mid.y + 76,
            ptc_ui_text(PTC_UI_T_DOCK_CARD_WAIVED), 12, UI_MUTED);
    } else if (model->dock_restriction_active) {
        draw_text_bold(pixels, stride, bento_mid.x + 16, bento_mid.y + 50,
            ptc_ui_text(PTC_UI_T_DOCK_BLOCKED_BANNER), 15, UI_DANGER);
        draw_text(pixels, stride, bento_mid.x + 16, bento_mid.y + 76,
            ptc_ui_text(PTC_UI_T_DOCK_CONNECT), 11, UI_DANGER);
    } else if (!model->dock_policy.force_docked && !model->dock_policy.undocked_limit_enabled) {
        draw_text(pixels, stride, bento_mid.x + 16, bento_mid.y + 50,
            ptc_ui_text(PTC_UI_T_DOCK_OFF), 15, UI_MUTED);
        draw_text(pixels, stride, bento_mid.x + 16, bento_mid.y + 76,
            ptc_ui_text(PTC_UI_T_DOCK_CARD_OFF), 11, UI_MUTED);
    } else if (model->dock_policy.force_docked) {
        draw_text_bold(pixels, stride, bento_mid.x + 16, bento_mid.y + 50,
            ptc_ui_text(PTC_UI_T_DOCK_CARD_FORCE), 14, UI_DANGER);
        draw_text(pixels, stride, bento_mid.x + 16, bento_mid.y + 76,
            ptc_ui_text(PTC_UI_T_DOCK_FORCE_HINT), 11, UI_MUTED);
    } else if (model->undocked_usage_available) {
        char rem_str[64];
        snprintf(rem_str, sizeof(rem_str), "%d %s", model->undocked_remaining_minutes, ptc_ui_text(PTC_UI_T_MINUTES));
        draw_text_bold(pixels, stride, bento_mid.x + 16, bento_mid.y + 52, rem_str, 22,
            model->undocked_remaining_minutes <= 5 ? UI_DANGER : UI_ACCENT);
        int rem_w = measure_text(rem_str, 22);
        char quota_str[64];
        snprintf(quota_str, sizeof(quota_str), "/ %d %s (%s %d %s)",
            model->dock_policy.undocked_daily_minutes, ptc_ui_text(PTC_UI_T_MINUTES),
            ptc_ui_text(PTC_UI_T_PLAYED), model->undocked_used_minutes, ptc_ui_text(PTC_UI_T_MIN));
        draw_text(pixels, stride, bento_mid.x + 16 + rem_w + 10, bento_mid.y + 50, quota_str, 12, UI_MUTED);

        /* 进度条 */
        UiRect track = {bento_mid.x + 16, bento_mid.y + 78, bento_mid.width - 32, 6};
        fill_round_rect(pixels, stride, track, 3, UI_RAISED);
        if (model->dock_policy.undocked_daily_minutes > 0) {
            int fill_w = (int)((int64_t)track.width * model->undocked_remaining_minutes / model->dock_policy.undocked_daily_minutes);
            if (fill_w < 4 && model->undocked_remaining_minutes > 0) fill_w = 4;
            if (fill_w > track.width) fill_w = track.width;
            if (fill_w > 0) {
                fill_round_rect(pixels, stride, (UiRect){track.x, track.y, fill_w, track.height}, 3,
                    model->undocked_remaining_minutes <= 5 ? UI_DANGER : UI_ACCENT);
            }
        }
    } else {
        draw_text(pixels, stride, bento_mid.x + 16, bento_mid.y + 50,
            ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM), 15, UI_MUTED);
    }

    /* 3. 底部 Bento 卡片：功能初衷与规则说明 (824, 430, 402, 178) */
    UiRect bento_bot = {info.x, info.y + 200, info.width, 178};
    fill_round_rect(pixels, stride, bento_bot, 12, UI_SURFACE);
    draw_rect_outline(pixels, stride, bento_bot, 12, 1, UI_BORDER);

    /* 💡 功能初衷 */
    draw_text_bold(pixels, stride, bento_bot.x + 16, bento_bot.y + 20,
        ptc_ui_text(PTC_UI_T_DOCK_INTENT_TITLE), 13, UI_ACCENT);
    draw_wrapped_text(pixels, stride, bento_bot.x + 16, bento_bot.y + 40,
        ptc_ui_text(PTC_UI_T_DOCK_EYE_PROTECTION_INTENT), 12,
        bento_bot.width - 32, 16, 3, UI_MUTED);

    /* ℹ️ 规则说明 */
    draw_text_bold(pixels, stride, bento_bot.x + 16, bento_bot.y + 98,
        ptc_ui_text(PTC_UI_T_DOCK_RULE_TITLE), 13, UI_INK);
    draw_wrapped_text(pixels, stride, bento_bot.x + 16, bento_bot.y + 118,
        ptc_ui_text(PTC_UI_T_DOCK_RULE_EXPLANATION), 12,
        bento_bot.width - 32, 16, 3, UI_MUTED);

#if defined(PLAYWISE_EDEN) || defined(PTC_UI_PREVIEW_ANIM_CLOCK_MS)
    if (model->eden_mode_controls) {
        home_button(pixels, stride, ptc_ui_dock_field_rect(7), ptc_ui_text(PTC_UI_T_EDEN_SIMULATE_TV),
            model->dock_field_focus == 7, false, model->waiting);
        home_button(pixels, stride, ptc_ui_dock_field_rect(8), ptc_ui_text(PTC_UI_T_EDEN_SIMULATE_NON_TV),
            model->dock_field_focus == 8, false, model->waiting);
    }
#endif
    bool hold = ptc_ui_dock_save_requires_hold(model, ptc_ui_render_now());
    home_button(pixels, stride, ptc_ui_dock_field_rect(3),
        ptc_ui_text(model->dock_dirty ? PTC_UI_T_DOCK_SAVE : PTC_UI_T_RULE_SAVED),
        model->dock_field_focus == 3, hold, model->waiting || model->disable_flag_present || !model->dock_dirty);
    home_button(pixels, stride, ptc_ui_dock_field_rect(4), ptc_ui_text(PTC_UI_T_DOCK_WAIVE),
        model->dock_field_focus == 4, false, model->waiting || model->dock_waived_today ||
        !(model->dock_policy.force_docked || model->dock_policy.undocked_limit_enabled));
}

bool draw_parent_plan_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    if (model->parent_page != PTC_UI_PARENT_PLAN) return false;

    switch (model->plan_page) {
    case PTC_UI_PLAN_PAGE_WEEKLY:
        draw_weekly_page(pixels, stride, model);
        break;
    case PTC_UI_PLAN_PAGE_HOLIDAY:
        draw_holiday_page(pixels, stride, model);
        break;
    case PTC_UI_PLAN_PAGE_BEDTIME:
        draw_bedtime_page(pixels, stride, model);
        break;
    case PTC_UI_PLAN_PAGE_DOCK:
        draw_dock_page(pixels, stride, model);
        break;
    case PTC_UI_PLAN_PAGE_EYE_CARE:
        draw_eye_care_page(pixels, stride, model);
        break;
    case PTC_UI_PLAN_PAGE_ROOT:
    default:
        draw_time_plan_preview(pixels, stride, model);
        break;
    }
    return true;
}
