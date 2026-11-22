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
    } else snprintf(title, sizeof(title), "%s", ptc_ui_text(PTC_UI_T_PLAN_SAVED_ACTIVE));
    const char *sub = model->disable_flag_present ? (ptc_ui_text(PTC_UI_T_CONTROL_DISABLED_PLAN_READ_ONLY)) :
        (model->waiting ? (ptc_ui_text(PTC_UI_T_SAVING_PLEASE_WAIT)) :
         (model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR ? (ptc_ui_text(PTC_UI_T_PRESS_TO_FINISH_THEN_SAVE)) :
          (requires_hold ? (fresh ? (ptc_ui_text(PTC_UI_T_QUOTA_WILL_EXHAUST_HOLD_A_TO_CONFIRM))
                                  : (ptc_ui_text(PTC_UI_T_STATUS_PENDING_HOLD_A_TO_CONFIRM))) :
           (dirty ? (ptc_ui_text(PTC_UI_T_PRESS_TO_SAVE_AND_APPLY_TO_CONSOLE))
                  : (ptc_ui_text(PTC_UI_T_SYNCHRONIZED_WITH_CONSOLE_POLICY))))));

    snprintf(source_text, sizeof(source_text), "%s  %s", dirty ? (ptc_ui_text(PTC_UI_T_SOURCE_AFTER_SAVE))
                                                               : (ptc_ui_text(PTC_UI_T_TODAY_SOURCE)),
             ptc_ui_effective_rule_label(projection.after.source));
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
    ptc_ui_format_bedtime_quota_notice(model, ptc_ui_render_now(), bedtime_notice, sizeof(bedtime_notice));
    format_status_age(model, age, sizeof(age));

    /* Wide horizontal layout (used by Holiday B right-side impact card, W>=600, H~176). */
    if (panel.width >= 600) {
        char fitted[160];
        int source_w = measure_text(source_text, 12) + 24;
        if (source_w > 260) source_w = 260;
        UiRect source = {panel.x + panel.width - 18 - source_w, panel.y + 12, source_w, 24};
        fill_round_rect(pixels, stride, source, 12, UI_ACCENT_SOFT);
        fit_text(fitted, sizeof(fitted), source_text, 12, source.width - 12);
        draw_text_center(pixels, stride, source, fitted, 12, UI_ACCENT);

        fit_text(fitted, sizeof(fitted), title, 18, source.x - (panel.x + 18) - 12);
        draw_text_bold(pixels, stride, panel.x + 18, panel.y + 28, fitted, 18,
                       dirty ? (requires_hold ? UI_DANGER : UI_WARNING) : UI_SUCCESS);
        fit_text(fitted, sizeof(fitted), sub, 12, panel.width - 36);
        draw_text(pixels, stride, panel.x + 18, panel.y + 47, fitted, 12,
                  requires_hold ? UI_DANGER : UI_RGB(UI_BLENDED(text_secondary)));

        bool single_unchanged = (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE && !state_message);
        int half_w = (panel.width - 48) / 2;
        UiRect result = {panel.x + 18, panel.y + 56, single_unchanged ? (panel.width - 36) : half_w, 86};
        UiRect conclusion = {result.x + half_w + 12, panel.y + 56, panel.width - 36 - half_w - 12, 86};
        fill_round_rect(pixels, stride, result, 10, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, result, 10, 1, UI_BORDER);

        if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
            ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
            draw_status_symbol(pixels, stride, result.x + 22, result.y + 43, UI_MUTED, 1);
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_UNCHANGED), 15, result.width - 52);
            draw_text(pixels, stride, result.x + 40, result.y + 28, fitted, 15, UI_INK);
            draw_wrapped_text(pixels, stride, result.x + 40, result.y + 48, impact,
                              12, result.width - 52, 16, 2, UI_MUTED);
        } else {
            bool current_state = projection.state == PTC_UI_PLAN_IMPACT_CURRENT;
            PtcDayRule left_rule = current_state ? projection.after.rule : projection.before.rule;
            PtcDayRule right_rule = projection.after.rule;
            int col_w = (result.width - 44) / 2;
            UiRect left = {result.x + 8, result.y + 6, col_w, result.height - 12};
            UiRect right = {result.x + result.width - 8 - col_w, result.y + 6, col_w, result.height - 12};
            fit_text(fitted, sizeof(fitted),
                     current_state ? ptc_ui_text(PTC_UI_T_TODAY_QUOTA) : ptc_ui_text(PTC_UI_T_CURRENT_QUOTA_2),
                     12, left.width - 4);
            draw_text_center(pixels, stride, (UiRect){left.x, left.y, left.width, 18}, fitted, 12, UI_MUTED);
            if (left_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(left_value, sizeof(left_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
            else snprintf(left_value, sizeof(left_value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)left_rule.minutes);
            fit_text(fitted, sizeof(fitted), left_value, 18, left.width - 4);
            draw_text_center(pixels, stride, (UiRect){left.x, left.y + 20, left.width, 28}, fitted, 18,
                             left_rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT);
            snprintf(left_note, sizeof(left_note), "%s",
                     ptc_ui_effective_rule_label(current_state ? projection.after.source : projection.before.source));
            fit_text(fitted, sizeof(fitted), left_note, 11, left.width - 4);
            draw_text_center(pixels, stride, (UiRect){left.x, left.y + 50, left.width, 18}, fitted, 11, UI_MUTED);

            if (!current_state) {
                int arrow_x = result.x + result.width / 2;
                int arrow_y = result.y + 38;
                draw_line(pixels, stride, arrow_x - 10, arrow_y, arrow_x + 8, arrow_y, 2, UI_MUTED);
                draw_line(pixels, stride, arrow_x + 3, arrow_y - 4, arrow_x + 8, arrow_y, 2, UI_MUTED);
                draw_line(pixels, stride, arrow_x + 3, arrow_y + 4, arrow_x + 8, arrow_y, 2, UI_MUTED);
            }

            if (current_state) {
                fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_REMAINING_TODAY), 12, right.width - 4);
                draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 18}, fitted, 12, UI_MUTED);
                if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
                else if (projection.remaining_available) snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_D_MIN), projection.remaining_minutes);
                else snprintf(right_value, sizeof(right_value), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING));
                if (played >= 0) snprintf(right_note, sizeof(right_note), ptc_ui_text(PTC_UI_T_USED_D_MIN), played);
                else snprintf(right_note, sizeof(right_note), "%s", ptc_ui_text(PTC_UI_T_SHOWN_AFTER_REFRESH));
            } else {
                fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_QUOTA_AFTER_SAVE), 12, right.width - 4);
                draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 18}, fitted, 12, UI_MUTED);
                if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
                else snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)right_rule.minutes);
                if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_note, sizeof(right_note), "%s", ptc_ui_text(PTC_UI_T_EST_REMAINING_NO_LIMIT));
                else if (projection.remaining_available) snprintf(right_note, sizeof(right_note), ptc_ui_text(PTC_UI_T_EST_REMAINING_D_MIN), projection.remaining_minutes);
                else snprintf(right_note, sizeof(right_note), "%s", ptc_ui_text(PTC_UI_T_REMAINING_PENDING_REFRESH));
            }
            fit_text(fitted, sizeof(fitted), right_value, 18, right.width - 4);
            draw_text_center(pixels, stride, (UiRect){right.x, right.y + 20, right.width, 28}, fitted, 18,
                             right_rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS :
                             (projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER :
                              (projection.state == PTC_UI_PLAN_IMPACT_UNKNOWN ? UI_MUTED : UI_SUCCESS)));
            fit_text(fitted, sizeof(fitted), right_note, 11, right.width - 4);
            draw_text_center(pixels, stride, (UiRect){right.x, right.y + 50, right.width, 18}, fitted, 11,
                             projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_MUTED);
        }

        if (!single_unchanged) {
            fill_round_rect(pixels, stride, conclusion, 10, state_message ? state_background : UI_RGB(UI_BLENDED(surface_raised)));
            draw_rect_outline(pixels, stride, conclusion, 10, 1, state_message ? state_color : UI_BORDER);
            if (state_message) {
                draw_wrapped_text(pixels, stride, conclusion.x + 12, conclusion.y + 24, state_message,
                                  12, conclusion.width - 24, 17, 3, state_color);
            } else if (projection.state == PTC_UI_PLAN_IMPACT_CURRENT) {
                draw_text(pixels, stride, conclusion.x + 12, conclusion.y + 22, ptc_ui_text(PTC_UI_T_CURRENT_STATUS_2), 12, UI_MUTED);
                draw_wrapped_text(pixels, stride, conclusion.x + 12, conclusion.y + 42,
                                  ptc_ui_text(PTC_UI_T_PLAN_IS_SAVED_NO_PENDING_QUOTA_DRAFTS),
                                  12, conclusion.width - 24, 17, 2, UI_RGB(UI_BLENDED(text_secondary)));
            } else {
                draw_text(pixels, stride, conclusion.x + 12, conclusion.y + 22, ptc_ui_text(PTC_UI_T_TODAY_AFTER_SAVE), 12, UI_MUTED);
                ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
                draw_wrapped_text(pixels, stride, conclusion.x + 12, conclusion.y + 42, impact,
                                  12, conclusion.width - 24, 17, 2, UI_RGB(UI_BLENDED(text_secondary)));
            }
        }

        fit_text(fitted, sizeof(fitted), bedtime_notice[0] ? bedtime_notice : age, 11, panel.width - 36);
        draw_text(pixels, stride, panel.x + 18, panel.y + panel.height - 12,
                  fitted, 11, bedtime_notice[0] ? UI_DANGER : status_age_color(model));
        return;
    }

    draw_text(pixels, stride, panel.x + 20, panel.y + 34, title, 19,
              dirty ? (requires_hold ? UI_DANGER : UI_WARNING) : UI_SUCCESS);
    draw_text(pixels, stride, panel.x + 20, panel.y + 58, sub, 13,
              requires_hold ? UI_DANGER : UI_RGB(UI_BLENDED(text_secondary)));

    UiRect result = {panel.x + 20, panel.y + 74, panel.width - 40, 96};
    UiRect left = {result.x + 10, result.y + 8, 138, result.height - 16};
    UiRect right = {result.x + result.width - 148, result.y + 8, 138, result.height - 16};
    fill_round_rect(pixels, stride, result, 12, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, result, 12, 1, UI_BORDER);

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
        if (left_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(left_value, sizeof(left_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
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
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
            else if (projection.remaining_available) snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_D_MIN), projection.remaining_minutes);
            else snprintf(right_value, sizeof(right_value), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING));
            if (played >= 0) snprintf(right_note, sizeof(right_note), ptc_ui_text(PTC_UI_T_USED_D_MIN), played);
            else snprintf(right_note, sizeof(right_note), "%s", ptc_ui_text(PTC_UI_T_SHOWN_AFTER_REFRESH));
        } else {
            draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 22}, ptc_ui_text(PTC_UI_T_QUOTA_AFTER_SAVE), 13, UI_MUTED);
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
            else snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)right_rule.minutes);
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_note, sizeof(right_note), "%s", ptc_ui_text(PTC_UI_T_EST_REMAINING_NO_LIMIT));
            else if (projection.remaining_available) snprintf(right_note, sizeof(right_note), ptc_ui_text(PTC_UI_T_EST_REMAINING_D_MIN), projection.remaining_minutes);
            else snprintf(right_note, sizeof(right_note), "%s", ptc_ui_text(PTC_UI_T_REMAINING_PENDING_REFRESH));
        }
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 24, right.width, 30}, right_value, 19,
                         right_rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS :
                         (projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER :
                          (projection.state == PTC_UI_PLAN_IMPACT_UNKNOWN ? UI_MUTED : UI_SUCCESS)));
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 55, right.width, 20}, right_note, 11,
                         projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_MUTED);
    }

    {
        int source_width = measure_text(source_text, 12) + 24;
        if (source_width > panel.width - 40) source_width = panel.width - 40;
        UiRect source = {panel.x + 20, panel.y + 184, source_width, 26};
        fill_round_rect(pixels, stride, source, 13, UI_ACCENT_SOFT);
        draw_text_center(pixels, stride, source, source_text, 12, UI_ACCENT);
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

    if (bedtime_notice[0]) {
        draw_wrapped_text(pixels, stride, panel.x + 20, panel.y + panel.height - 34,
                          bedtime_notice, 12, panel.width - 40, 16, 2, UI_DANGER);
    } else {
        draw_text(pixels, stride, panel.x + 20, panel.y + panel.height - 14,
                  age, 12, status_age_color(model));
    }
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
        else snprintf(title, sizeof(title), "%s", ptc_ui_text(PTC_UI_T_DRAFT_UNSAVED));
    } else if (kind == PTC_UI_PLAN_SCHEDULED) {
        if (ptc_ui_scheduled_dirty(model)) ++changed;
        if (changed > 0) snprintf(title, sizeof(title), "%s", ptc_ui_text(PTC_UI_T_DRAFT_SPECIFIC_DATE_QUOTA_CHANGED));
        else snprintf(title, sizeof(title), "%s", ptc_ui_text(PTC_UI_T_DRAFT_UNSAVED));
    } else {
        if (model->holiday_enabled != model->draft_holiday_enabled) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->holiday_rule, model->draft_holiday_rule)) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->makeup_workday_rule, model->draft_makeup_workday_rule)) ++changed;
        if (changed == 1) snprintf(title, sizeof(title), "%s", ptc_ui_text(PTC_UI_T_DRAFT_ONE_ITEM_CHANGED));
        else if (changed > 1) {
            PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("count", changed)};
            (void)ptc_ui_text_format(PTC_UI_T_DRAFT_ITEMS_CHANGED_NAMED, title, sizeof(title), args, 1);
        }
        else snprintf(title, sizeof(title), "%s", ptc_ui_text(PTC_UI_T_DRAFT_UNSAVED));
    }

    draw_plan_card(pixels, stride, panel, false);
    draw_text(pixels, stride, panel.x + 20, panel.y + 32, title, 18, UI_WARNING);

    UiRect result = {panel.x + 20, panel.y + 50, panel.width - 40, 104};
    int col_w = panel.width >= 440 ? 168 : 132;
    UiRect left = {result.x + 10, result.y + 8, col_w, result.height - 16};
    UiRect right = {result.x + result.width - 10 - col_w, result.y + 8, col_w, result.height - 16};
    fill_round_rect(pixels, stride, result, 12, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, result, 12, 1, UI_BORDER);
    if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
        draw_status_symbol(pixels, stride, result.x + 24, result.y + 52, UI_MUTED, 1);
        draw_text(pixels, stride, result.x + 44, result.y + 34, ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_UNCHANGED), 15, UI_INK);
        draw_wrapped_text(pixels, stride, result.x + 44, result.y + 58, impact,
                          12, result.width - 56, 17, 2, UI_MUTED);
    } else {
        draw_text_center(pixels, stride, (UiRect){left.x, left.y, left.width, 22}, ptc_ui_text(PTC_UI_T_CURRENT_QUOTA_2), 12, UI_MUTED);
        if (projection.before.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(left_value, sizeof(left_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
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
        if (projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)projection.after.rule.minutes);
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 24, right.width, 34}, right_value, 19,
                         projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS :
                         (projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_ACCENT));
        if (projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "%s", ptc_ui_text(PTC_UI_T_EST_REMAINING_NO_LIMIT));
        else if (projection.remaining_available) snprintf(right_value, sizeof(right_value), ptc_ui_text(PTC_UI_T_EST_REMAINING_D_MIN_2), projection.remaining_minutes);
        else snprintf(right_value, sizeof(right_value), "%s", ptc_ui_text(PTC_UI_T_REMAINING_PENDING_REFRESH));
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
    char text[192], fitted[192], saved[96];
    uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
    bool disabled = model->disable_flag_present || model->waiting;
    int selected_day = model->editor_index >= 0 && model->editor_index < 7 ? model->editor_index : 0;
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_WEEKLY_BASE_PLAN_CLICK_MODE_OR_QUOTA), 16,
             ptc_ui_weekly_bulk_rect().x - 54 - 16);
    draw_text(pixels, stride, 54, 194, fitted, 16, UI_MUTED);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_bulk_rect(), ptc_ui_text(PTC_UI_T_BATCH_SETUP),
                          UI_PAGE, UI_ACCENT, model->selected_index == 2, disabled);
    for (int slot = 0; slot < 7; ++slot) {
        int day = ptc_ui_weekday_for_display_slot(slot);
        bool selected = slot == model->weekly_grid_slot;
        bool changed = ptc_ui_day_rule_effectively_changed(model->current_week[day], model->draft_week[day]);
        UiRect card = to_uirect(ptc_ui_weekly_day_rect(slot));
        bool limited = model->draft_week[day].mode == PTC_RULE_MODE_LIMIT;
        draw_plan_card(pixels, stride, card, selected && model->selected_index == 0 && !model->parent_footer_focused);
        if (selected && model->selected_index != 0) draw_rect_outline(pixels, stride, card, 10, 2, UI_ACCENT);
        draw_text_center(pixels, stride, (UiRect){card.x + 6, card.y + 10, card.width - 12, 28},
                         ptc_ui_weekday_label((unsigned)day), 19, selected ? UI_ACCENT : UI_INK);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(limited ? PTC_UI_T_LIMIT : PTC_UI_T_BASIS_UNLIMITED), 12, card.width - 16);
        draw_text_center(pixels, stride, (UiRect){card.x + 8, card.y + 42, card.width - 16, 22}, fitted, 12,
                         limited ? UI_MUTED : UI_SUCCESS);
        if (limited) snprintf(text, sizeof(text), "%u", (unsigned)model->draft_week[day].minutes);
        else snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_NO_LIMIT));
        fit_text(fitted, sizeof(fitted), text, limited ? 30 : 18, card.width - 12);
        draw_text_center(pixels, stride, (UiRect){card.x + 6, card.y + 66, card.width - 12, 40},
                         fitted, limited ? 30 : 18, disabled ? UI_DISABLED : limited ? UI_ACCENT : UI_SUCCESS);
        draw_text_center(pixels, stride, (UiRect){card.x + 6, card.y + 108, card.width - 12, 20},
                         limited ? ptc_ui_text(PTC_UI_T_MIN) : "", 12, UI_MUTED);
        draw_text_center(pixels, stride, (UiRect){card.x + 6, card.y + 132, card.width - 12, 18},
                         model->status_loaded && day == weekday ? ptc_ui_text(PTC_UI_T_TODAY_3) : "", 12, UI_SUCCESS);
        if (changed) fill_round_rect(pixels, stride, (UiRect){card.x + card.width - 12, card.y + 10, 5, 5}, 2, UI_WARNING);
        int fill = limited ? (int)((int64_t)(card.width - 24) * (model->draft_week[day].minutes > 180 ? 180 : model->draft_week[day].minutes) / 180)
                           : card.width - 24;
        fill_round_rect(pixels, stride, (UiRect){card.x + 12, card.y + card.height - 8, card.width - 24, 4}, 2, UI_BORDER);
        if (fill > 0) fill_round_rect(pixels, stride, (UiRect){card.x + 12, card.y + card.height - 8, fill, 4}, 2,
                                    disabled ? UI_DISABLED : limited ? UI_ACCENT : UI_SUCCESS);
    }
    UiRect editor = to_uirect(ptc_ui_weekly_editor_rect());
    draw_plan_card(pixels, stride, editor, false);
    snprintf(text, sizeof(text), "%s %s", ptc_ui_text(PTC_UI_T_DRAFT_PREVIEW), ptc_ui_weekday_label((unsigned)selected_day));
    fit_text(fitted, sizeof(fitted), text, 19, editor.width - 40);
    draw_text_bold(pixels, stride, editor.x + 20, editor.y + 30, fitted, 19, UI_INK);
    bool limited = model->draft_week[selected_day].mode == PTC_RULE_MODE_LIMIT;
    PtcDayRule current = model->current_week[selected_day];
    if (current.mode == PTC_RULE_MODE_LIMIT) snprintf(saved, sizeof(saved), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)current.minutes);
    else snprintf(saved, sizeof(saved), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    snprintf(text, sizeof(text), "%s: %s", ptc_ui_text(PTC_UI_T_SAVED), saved);
    fit_text(fitted, sizeof(fitted), text, 13, editor.width - 40);
    draw_text(pixels, stride, editor.x + 20, editor.y + 54, fitted, 13, UI_MUTED);
    UiRect mode = to_uirect(ptc_ui_weekly_page_mode_rect());
    UiRect minutes = to_uirect(ptc_ui_weekly_page_minutes_rect());
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_X_TOGGLE_MODE), 16, mode.x - editor.x - 40);
    draw_text(pixels, stride, editor.x + 20, mode.y + 34, fitted, 16, UI_INK);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_page_mode_rect(),
                          ptc_ui_text(limited ? PTC_UI_T_LIMIT : PTC_UI_T_BASIS_UNLIMITED), UI_PAGE, UI_ACCENT,
                          model->selected_index == 1, disabled);
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_WEEKLY_DAY_ALLOWANCE), 16, minutes.x - editor.x - 40);
    draw_text(pixels, stride, editor.x + 20, minutes.y + 34, fitted, 16, UI_INK);
    if (limited) snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)model->draft_week[selected_day].minutes);
    else snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_NOT_EDITABLE));
    draw_candidate_button(pixels, stride, ptc_ui_weekly_page_minutes_rect(), text, UI_PAGE, UI_ACCENT,
                          model->selected_index == 5, disabled || !limited);
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_UPDATES_DRAFT_ON_INPUT_APPLIED_ONLY_AFTER), 13, editor.width - 40);
    draw_text(pixels, stride, editor.x + 20, editor.y + editor.height - 18, fitted, 13, UI_MUTED);
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_WEEKLY, model->weekly_dirty, (UiRect){838, 176, 388, 372});
    draw_candidate_button(pixels, stride, ptc_ui_weekly_discard_rect(), ptc_ui_text(PTC_UI_T_ZL_DISCARD),
                          UI_PAGE, UI_INK, model->selected_index == 3, !model->weekly_dirty || model->waiting);
    bool hold = model->weekly_dirty && ptc_ui_plan_save_requires_hold(model, PTC_UI_PLAN_WEEKLY, ptc_ui_render_now());
    draw_candidate_button(pixels, stride, ptc_ui_weekly_save_rect(), ptc_ui_text(model->disable_flag_present ? PTC_UI_T_READ_ONLY :
                          model->waiting ? PTC_UI_T_SAVING_2 : model->weekly_dirty ? (hold ? PTC_UI_T_SAVE_CONFIRM : PTC_UI_T_SAVE_DRAFT_2) : PTC_UI_T_SAVED),
                          hold ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT, model->selected_index == 4, disabled || !model->weekly_dirty);
}


static void draw_holiday_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect top_card = to_uirect(ptc_ui_holiday_card_rect(0));
    const char *titles[] = {ptc_ui_text(PTC_UI_T_STATUTORY_VACATION), ptc_ui_text(PTC_UI_T_ADJUSTED_WORKING_DAYS)};
    const char *descriptions[] = {ptc_ui_text(PTC_UI_T_VACATION_DATES_FOR_MAJOR_STATUTORY_HOLIDAYS),
                                  ptc_ui_text(PTC_UI_T_THE_MAKE_UP_DATE_DUE_TO_HOLIDAYS)};
    char line[160], fitted[160], minutes_str[64];
    bool disabled = model->disable_flag_present;
    bool enabled = model->draft_holiday_enabled;
    bool top_selected = model->selected_index == 0 && !model->parent_footer_focused;

    /* ================= Left Column: Master Switch + Calendar Preview + Priority ================= */
    if (!top_selected) draw_card_shadow(pixels, stride, top_card, 14);
    fill_round_rect(pixels, stride, top_card, 14, disabled ? UI_RAISED : (top_selected ? UI_ACCENT_SOFT : UI_SURFACE));
    if (top_selected) draw_focus_ring(pixels, stride, top_card, 14);
    else draw_rect_outline(pixels, stride, top_card, 14, 1, UI_BORDER);

    UiRect enable_rect = to_uirect(ptc_ui_holiday_enable_rect());
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAY_RULES), 19,
             enable_rect.x - (top_card.x + 18) - 12);
    draw_text_bold(pixels, stride, top_card.x + 18, top_card.y + 32, fitted, 19, UI_INK);
    draw_toggle_switch(pixels, stride, enable_rect, enabled, top_selected, disabled, NULL, NULL);

    draw_wrapped_text(pixels, stride, top_card.x + 18, top_card.y + 58,
                      ptc_ui_text(PTC_UI_T_AUTOMATICALLY_APPLY_STATUTORY_HOLIDAY_MAKEUP_WORKDAY_RULES),
                      12, top_card.width - 36, 16, 2, UI_MUTED);
    {
        const char *st_label = enabled ? ptc_ui_text(PTC_UI_T_ACTIVE_ON)
                                       : ptc_ui_text(PTC_UI_T_HOLIDAY_PRESET_RETAINED);
        int st_w = measure_text(st_label, 12) + 20;
        if (st_w > top_card.width - 36) st_w = top_card.width - 36;
        UiRect st_pill = {top_card.x + 18, top_card.y + top_card.height - 32, st_w, 22};
        fill_round_rect(pixels, stride, st_pill, 11, enabled ? UI_SUCCESS_SOFT : UI_RAISED);
        fit_text(fitted, sizeof(fitted), st_label, 12, st_pill.width - 10);
        draw_text_center(pixels, stride, st_pill, fitted, 12, enabled ? UI_SUCCESS : UI_MUTED);
    }

    /* Left Middle: Current Region Calendar & Upcoming Preview Card */
    UiRect cal_card = {54, 314, 396, 260};
    draw_plan_card(pixels, stride, cal_card, false);
    {
        const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
        const char *reg_name = model->calendar_region_name[0] ? model->calendar_region_name :
            (model->calendar_builtin || !model->calendar_option_id[0]
                ? ptc_ui_text(PTC_UI_T_CALENDAR_BUILTIN_CHINA)
                : ptc_ui_text(PTC_UI_T_CALENDAR_INVALID_FILE));
        uint16_t show_year = model->holiday_calendar_year ? model->holiday_calendar_year : info->last_year;
        char ver_badge[48];
        snprintf(ver_badge, sizeof(ver_badge), "v%u | %u", (unsigned)info->version, (unsigned)show_year);
        int vb_w = measure_text(ver_badge, 11) + 16;
        if (vb_w > 110) vb_w = 110;
        UiRect vb_rect = {cal_card.x + cal_card.width - 16 - vb_w, cal_card.y + 14, vb_w, 22};
        fill_round_rect(pixels, stride, vb_rect, 11,
                        model->calendar_update_warning ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
        draw_text_center(pixels, stride, vb_rect, ver_badge, 11,
                         model->calendar_update_warning ? UI_DANGER : UI_SUCCESS);

        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAY_CALENDAR), 16,
                 vb_rect.x - (cal_card.x + 16) - 10);
        draw_text_bold(pixels, stride, cal_card.x + 16, cal_card.y + 30, fitted, 16, UI_INK);

        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_CALENDAR_CURRENT_ACTIVE_PREFIX),
                 reg_name, (unsigned)show_year);
        fit_text(fitted, sizeof(fitted), line, 12, cal_card.width - 32);
        draw_text(pixels, stride, cal_card.x + 16, cal_card.y + 52, fitted, 12, UI_MUTED);

        /* Two upcoming preview rows: next statutory holiday & next makeup workday */
        for (int r = 0; r < 2; ++r) {
            UiRect row = {cal_card.x + 16, cal_card.y + 66 + r * 62, cal_card.width - 32, 54};
            fill_round_rect(pixels, stride, row, 9, UI_RAISED);
            draw_rect_outline(pixels, stride, row, 9, 1, UI_BORDER);
            draw_text(pixels, stride, row.x + 12, row.y + 20,
                      ptc_ui_text(r == 0 ? PTC_UI_T_HOLIDAY_UPCOMING_HOLIDAY : PTC_UI_T_HOLIDAY_UPCOMING_MAKEUP),
                      11, r == 0 ? UI_WARNING_TEXT : UI_ACCENT);
            PtcHolidayCalendarMatch match;
            uint16_t from_day = model->day_index ? model->day_index : 2380;
            bool found = ptc_holiday_calendar_find(
                r == 0 ? PTC_CALENDAR_DAY_STATUTORY_HOLIDAY : PTC_CALENDAR_DAY_MAKEUP_WORKDAY,
                from_day, &match);
            if (found) {
                uint16_t y = 0; uint8_t m = 0, d = 0;
                ptc_date_from_day_index(match.day_index, &y, &m, &d);
                if (r == 0 && match.arrangement && match.arrangement->display_name) {
                    snprintf(line, sizeof(line), "%s  (%02u/%02u-%02u/%02u)",
                             match.arrangement->display_name,
                             (unsigned)match.arrangement->start_month, (unsigned)match.arrangement->start_day,
                             (unsigned)match.arrangement->end_month, (unsigned)match.arrangement->end_day);
                } else if (match.arrangement && match.arrangement->display_name) {
                    snprintf(line, sizeof(line), "%04u-%02u-%02u  (%s)",
                             (unsigned)y, (unsigned)m, (unsigned)d, match.arrangement->display_name);
                } else {
                    snprintf(line, sizeof(line), "%04u-%02u-%02u", (unsigned)y, (unsigned)m, (unsigned)d);
                }
            } else {
                snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_HOLIDAY_NO_UPCOMING));
            }
            fit_text(fitted, sizeof(fitted), line, 14, row.width - 24);
            draw_text_bold(pixels, stride, row.x + 12, row.y + 42, fitted, 14,
                           found ? UI_INK : UI_MUTED);
        }
    }

    /* Calendar Manager CTA Button inside bottom of Left Calendar Card */
    UiRect manage_btn = to_uirect(ptc_ui_holiday_manage_rect());
    bool manage_focused = (model->selected_index == 6 && !model->parent_footer_focused);
    fill_round_rect(pixels, stride, manage_btn, 10, manage_focused ? UI_ACCENT_SOFT : UI_PAGE);
    if (manage_focused) draw_focus_ring(pixels, stride, manage_btn, 10);
    else draw_rect_outline(pixels, stride, manage_btn, 10, 1, UI_BORDER);
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_HOLIDAY_SCHEDULE_AND_CALENDAR), 15,
             manage_btn.width - 54);
    draw_text_bold(pixels, stride, manage_btn.x + 14, manage_btn.y + 30, fitted, 15,
                   manage_focused ? UI_ACCENT : UI_INK);
    UiRect arrow_badge = {manage_btn.x + manage_btn.width - 36, manage_btn.y + 10, 26, 28};
    fill_round_rect(pixels, stride, arrow_badge, 7, manage_focused ? UI_ACCENT : UI_RAISED);
    draw_text_center(pixels, stride, arrow_badge, ">", 17, manage_focused ? UI_ON_ACCENT : UI_ACCENT);

    /* Left Bottom: Priority Note Strip */
    UiRect prio_strip = {54, 586, 396, 48};
    fill_round_rect(pixels, stride, prio_strip, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, prio_strip, 10, 1, UI_BORDER);
    ptc_ui_format_holiday_priority_summary(model, line, sizeof(line));
    draw_wrapped_text(pixels, stride, prio_strip.x + 12, prio_strip.y + 20, line,
                      11, prio_strip.width - 24, 15, 2, UI_MUTED);

    /* ================= Right Column: Two Rule Rows + Wide Impact + Actions ================= */
    UiRect rules_box = {466, 176, 760, 210};
    draw_plan_card(pixels, stride, rules_box, false);
    {
        const char *hint = ptc_ui_text(PTC_UI_T_X_TOGGLE_MODE);
        int hw = measure_text(hint, 12);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_HOLIDAY_QUOTA), 15,
                 rules_box.width - hw - 44);
        draw_text_bold(pixels, stride, rules_box.x + 16, rules_box.y + 26, fitted, 15, UI_INK);
        draw_text(pixels, stride, rules_box.x + rules_box.width - 16 - hw, rules_box.y + 26,
                  hint, 12, UI_ACCENT);
    }

    for (int index = 0; index < 2; ++index) {
        PtcDayRule rule = index == 0 ? model->draft_holiday_rule : model->draft_makeup_workday_rule;
        UiRect row = to_uirect(ptc_ui_holiday_card_rect(index + 1));
        UiRect mode = to_uirect(ptc_ui_holiday_mode_rect(index));
        UiRect minutes = to_uirect(ptc_ui_holiday_minutes_rect(index));
        bool selected = model->selected_index == index + 1 && !model->parent_footer_focused;
        bool limited = rule.mode == PTC_RULE_MODE_LIMIT;

        fill_round_rect(pixels, stride, row, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        if (selected) draw_focus_ring(pixels, stride, row, 12);
        else draw_rect_outline(pixels, stride, row, 12, 1, UI_BORDER);

        int text_max = mode.x - (row.x + 16) - 12;
        fit_text(fitted, sizeof(fitted), titles[index], 18, text_max);
        draw_text_bold(pixels, stride, row.x + 16, row.y + 32, fitted, 18,
                       disabled || !enabled ? UI_MUTED : UI_INK);
        fit_text(fitted, sizeof(fitted), descriptions[index], 12, text_max);
        draw_text(pixels, stride, row.x + 16, row.y + 56, fitted, 12, UI_MUTED);

        /* Inline segmented Mode Toggle (Limit | Unlimited) inside ptc_ui_holiday_mode_rect */
        fill_round_rect(pixels, stride, mode, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, mode, 10, 1, UI_BORDER);
        int seg_w = (mode.width - 6) / 2;
        UiRect seg_lim = {mode.x + 3, mode.y + 3, seg_w, mode.height - 6};
        UiRect seg_unlim = {mode.x + 3 + seg_w, mode.y + 3, mode.width - 6 - seg_w, mode.height - 6};
        if (limited) {
            fill_round_rect(pixels, stride, seg_lim, 8, disabled ? UI_BORDER : (enabled ? UI_ACCENT : UI_MUTED));
        } else {
            fill_round_rect(pixels, stride, seg_unlim, 8, disabled ? UI_BORDER : (enabled ? UI_SUCCESS : UI_MUTED));
        }
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_LIMIT), 12, seg_lim.width - 6);
        draw_text_center(pixels, stride, seg_lim, fitted, 12,
                         disabled ? UI_DISABLED : (limited ? UI_ON_ACCENT : UI_MUTED));
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED), 12, seg_unlim.width - 6);
        draw_text_center(pixels, stride, seg_unlim, fitted, 12,
                         disabled ? UI_DISABLED : (!limited ? UI_ON_ACCENT : UI_MUTED));

        /* Right Quota Button inside ptc_ui_holiday_minutes_rect */
        fill_round_rect(pixels, stride, minutes, 10, limited && !disabled ? UI_SURFACE : UI_PAGE);
        draw_rect_outline(pixels, stride, minutes, 10, 1,
                          limited && !disabled && enabled ? UI_ACCENT : UI_BORDER);
        if (limited) {
            snprintf(minutes_str, sizeof(minutes_str), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)rule.minutes);
            fit_text(fitted, sizeof(fitted), minutes_str, 17, minutes.width - 14);
            draw_text_center(pixels, stride, minutes, fitted, 17,
                             disabled ? UI_DISABLED : (enabled ? UI_ACCENT : UI_MUTED));
        } else {
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NO_LIMIT), 14, minutes.width - 14);
            draw_text_center(pixels, stride, minutes, fitted, 14, UI_DISABLED);
        }
    }

    /* Right Middle: Wide Today Impact Card */
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_HOLIDAY, model->holiday_dirty,
                     (UiRect){466, 398, 760, 176});

    /* Right Bottom: Shortcut hint + Discard + Save */
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_A_CLICK_TO_EDIT_QUOTA), 13, 250);
    draw_text(pixels, stride, 470, 615, fitted, 13, UI_MUTED);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(4), ptc_ui_text(PTC_UI_T_ZL_DISCARD),
                          UI_PAGE, UI_INK, model->selected_index == 4 && !model->parent_footer_focused,
                          !model->holiday_dirty || model->waiting);
    bool holiday_hold = model->holiday_dirty && ptc_ui_plan_save_requires_hold(model, PTC_UI_PLAN_HOLIDAY, ptc_ui_render_now());
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(5),
                          disabled ? ptc_ui_text(PTC_UI_T_READ_ONLY) : (model->waiting ? ptc_ui_text(PTC_UI_T_SAVING_2) :
                          (model->holiday_dirty ? (holiday_hold ? ptc_ui_text(PTC_UI_T_SAVE_CONFIRM) : ptc_ui_text(PTC_UI_T_SAVE_DRAFT_2)) : ptc_ui_text(PTC_UI_T_SAVED))),
                          holiday_hold ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT,
                          model->selected_index == 5 && !model->parent_footer_focused,
                          disabled || model->waiting || !model->holiday_dirty);
}

const char *bedtime_override_label(PtcBedtimeOverrideMode mode)
{
    if (mode == PTC_BEDTIME_OVERRIDE_DISABLED) return ptc_ui_text(PTC_UI_T_OFF);
    if (mode == PTC_BEDTIME_OVERRIDE_CUSTOM) return ptc_ui_text(PTC_UI_T_CUSTOM);
    return ptc_ui_text(PTC_UI_T_INHERIT);
}

static void draw_bedtime_window_value(char *out, size_t out_size, const PtcBedtimeWindow *window)
{
    if (!window->enabled) snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_OFF));
    else snprintf(out, out_size, ptc_ui_text(PTC_UI_T_02U_02U_NEXT_DAY_02U_02U),
        (unsigned int)(window->start_minute / 60), (unsigned int)(window->start_minute % 60),
        (unsigned int)(window->end_minute / 60), (unsigned int)(window->end_minute % 60));
}

static void draw_bedtime_sections(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *SECTIONS[] = {ptc_ui_text(PTC_UI_T_WEEKLY_2), ptc_ui_text(PTC_UI_T_HOLIDAYS), ptc_ui_text(PTC_UI_T_GO_TO_BED_ON_THE_SPECIFIED_DATE)};
    UiRect track = {630, 168, 596, 48};
    fill_round_rect(pixels, stride, track, 14, UI_RAISED);
    draw_rect_outline(pixels, stride, track, 14, 1, UI_BORDER);
    for (int i = 0; i < 3; ++i) {
        UiRect rect = to_uirect(ptc_ui_bedtime_section_rect(i));
        bool selected = model->bedtime_section == (PtcUiBedtimeSection)i;
        bool sec_focused = selected && model->bedtime_section_focused && !model->parent_footer_focused;
        char fitted[64];
        if (selected) fill_round_rect(pixels, stride, rect, 10, UI_ACCENT);
        if (sec_focused) draw_focus_ring(pixels, stride, rect, 10);
        fit_text(fitted, sizeof(fitted), SECTIONS[i], 15, rect.width - 14);
        draw_text_center(pixels, stride, rect, fitted, 15, selected ? UI_ON_ACCENT : UI_INK);
    }
}

static void draw_bedtime_master_card(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                     bool is_en, bool bedtime_enforcing)
{
    (void)is_en;
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    UiRect master_card = to_uirect(ptc_ui_bedtime_master_switch_rect());
    bool master_focused = model->bedtime_master_focused && !model->parent_footer_focused;
    bool switch_dirty = draft->enabled != model->bedtime_policy.enabled;
    char fitted[96];

    if (!master_focused) draw_card_shadow(pixels, stride, master_card, 14);
    fill_round_rect(pixels, stride, master_card, 14, master_focused ? UI_ACCENT_SOFT : UI_RGB(UI_BLENDED(surface)));
    if (master_focused) {
        draw_focus_ring(pixels, stride, master_card, 14);
    } else if (bedtime_enforcing) {
        int phase = get_breathing_phase();
        draw_rect_outline(pixels, stride, master_card, 14, 2,
                          UI_RGB(ui_mix_rgb(UI_BLENDED(danger), 0xFF9A8A, phase * 4)));
    } else {
        draw_rect_outline(pixels, stride, master_card, 14, 1, UI_BORDER);
    }

    UiRect toggle_rect = {master_card.x + master_card.width - 70, master_card.y + (master_card.height - 28) / 2, 56, 28};
    const char *pill_text = bedtime_enforcing ? ptc_ui_text(PTC_UI_T_RESTRICTING) :
        (switch_dirty ? (draft->enabled ? ptc_ui_text(PTC_UI_T_DRAFT_ON_PRESS) : ptc_ui_text(PTC_UI_T_DRAFT_OFF_PRESS)) :
         (draft->enabled ? ptc_ui_text(PTC_UI_T_ACTIVE_ON) : ptc_ui_text(PTC_UI_T_ACTIVE_OFF)));
    uint32_t pill_bg = bedtime_enforcing ? UI_DANGER_SOFT :
        (switch_dirty ? UI_WARNING_SOFT : (draft->enabled ? UI_SUCCESS_SOFT : UI_RAISED));
    uint32_t pill_fg = bedtime_enforcing ? UI_DANGER :
        (switch_dirty ? UI_WARNING_TEXT : (draft->enabled ? UI_SUCCESS : UI_MUTED));
    int pill_w = measure_text(pill_text, 12) + 20;
    if (pill_w > 190) pill_w = 190;
    UiRect status_pill = {toggle_rect.x - pill_w - 12, master_card.y + (master_card.height - 24) / 2, pill_w, 24};
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_BEDTIME_SWITCH), 17,
             status_pill.x - (master_card.x + 16) - 10);
    draw_text_bold(pixels, stride, master_card.x + 16, master_card.y + 30, fitted, 17, UI_INK);
    fill_round_rect(pixels, stride, status_pill, 12, pill_bg);
    fit_text(fitted, sizeof(fitted), pill_text, 12, status_pill.width - 10);
    draw_text_center(pixels, stride, status_pill, fitted, 12, pill_fg);
    draw_toggle_switch(pixels, stride, toggle_rect, draft->enabled, master_focused,
                       model->disable_flag_present, NULL, NULL);
}

static void draw_bedtime_manage_buttons(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                        int64_t raw_now)
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

static void draw_bedtime_preview(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                 uint16_t minute_of_day, PtcUiBedtimeImpact bedtime_impact,
                                 bool is_skipped_active)
{
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    const char *section_name = model->bedtime_section == PTC_UI_BEDTIME_WEEKLY
        ? ptc_ui_text(PTC_UI_T_WEEKLY_BEDTIME) : (model->bedtime_section == PTC_UI_BEDTIME_CALENDAR
            ? ptc_ui_text(PTC_UI_T_HOLIDAY_BEDTIME) : ptc_ui_text(PTC_UI_T_SPECIFIC_DATE_BEDTIME));
    UiRect eval_card = {838, 228, 388, 338};
    uint32_t state_color = strcmp(model->result_status, "error") == 0 ? UI_DANGER :
        (model->waiting || model->bedtime_dirty ? UI_WARNING :
         (bedtime_enforcing ? UI_DANGER : UI_SUCCESS));
    uint32_t state_bg = state_color == UI_DANGER ? UI_DANGER_SOFT :
        (state_color == UI_WARNING ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
    char state_text[96], fitted[160], preview_title[64];

    snprintf(state_text, sizeof(state_text), "%s",
        strcmp(model->result_status, "error") == 0 ? ptc_ui_text(PTC_UI_T_SAVE_FAILED_DRAFT_RETAINED) :
        (model->waiting ? ptc_ui_text(PTC_UI_T_SAVING_3) :
         (model->bedtime_dirty ? ptc_ui_text(PTC_UI_T_DRAFT_TO_BE_SAVED) : ptc_ui_text(PTC_UI_T_RULE_SAVED))));

    draw_plan_card(pixels, stride, eval_card, false);
    int st_w = measure_text(state_text, 11) + 18;
    if (st_w > 160) st_w = 160;
    UiRect state_pill = {eval_card.x + eval_card.width - 16 - st_w, eval_card.y + 12, st_w, 22};
    fill_round_rect(pixels, stride, state_pill, 11, state_bg);
    fit_text(fitted, sizeof(fitted), state_text, 11, state_pill.width - 10);
    draw_text_center(pixels, stride, state_pill, fitted, 11, state_color);

    snprintf(preview_title, sizeof(preview_title), "%s%s",
             model->bedtime_dirty ? ptc_ui_text(PTC_UI_T_DRAFT_PREVIEW) : "", section_name);
    fit_text(fitted, sizeof(fitted), preview_title, 16, state_pill.x - (eval_card.x + 16) - 10);
    draw_text_bold(pixels, stride, eval_card.x + 16, eval_card.y + 28, fitted, 16, UI_INK);

    PtcRules eval_rules;
    memset(&eval_rules, 0, sizeof(eval_rules));
    eval_rules.bedtime = *draft;
    PtcBedtimeEvaluation eval = ptc_bedtime_evaluate(
        &eval_rules, model->day_index, ptc_weekday_from_day_index(model->day_index), minute_of_day);

    char forecast_line1[128], forecast_line2[128];
    uint32_t f1_color = UI_INK, f1_bg = UI_RAISED;
    if (!draft->enabled) {
        snprintf(forecast_line1, sizeof(forecast_line1), ptc_ui_text(PTC_UI_T_S_BEDTIME_DISABLED_NO_NIGHTTIME_LIMITS),
                 model->bedtime_dirty ? ptc_ui_text(PTC_UI_T_FORECAST_AFTER_SAVE)
                                      : ptc_ui_text(PTC_UI_T_CURRENT_STATE));
        f1_color = UI_MUTED; f1_bg = UI_RAISED;
    } else if (bedtime_impact == PTC_UI_BEDTIME_IMPACT_RESTRICT) {
        snprintf(forecast_line1, sizeof(forecast_line1), "%s", ptc_ui_text(PTC_UI_T_IN_BEDTIME_HOURS_SAVING_WILL_RESTRICT_IMMEDIATELY));
        f1_color = UI_DANGER; f1_bg = UI_DANGER_SOFT;
    } else if (bedtime_impact == PTC_UI_BEDTIME_IMPACT_UNKNOWN) {
        snprintf(forecast_line1, sizeof(forecast_line1), "%s", ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED_MAY_RESTRICT_IMMEDIATELY_HOLD_TO));
        f1_color = UI_DANGER; f1_bg = UI_DANGER_SOFT;
    } else if (is_skipped_active) {
        uint16_t skipped_start = model->bedtime_skipped_window_available
            ? model->bedtime_skipped_start_minute : eval.start_minute;
        uint16_t skipped_end = model->bedtime_skipped_window_available
            ? model->bedtime_skipped_end_minute : eval.end_minute;
        snprintf(forecast_line1, sizeof(forecast_line1), ptc_ui_text(PTC_UI_T_SKIPPED_THIS_TIME_02U_02U_TO_NEXT),
            (unsigned)(skipped_start / 60), (unsigned)(skipped_start % 60),
            (unsigned)(skipped_end / 60), (unsigned)(skipped_end % 60));
        f1_color = UI_SUCCESS; f1_bg = UI_SUCCESS_SOFT;
    } else if (eval.active) {
        snprintf(forecast_line1, sizeof(forecast_line1), "%s", ptc_ui_text(PTC_UI_T_CURRENT_BEDTIME_RESTRICTION_ACTIVE));
        f1_color = UI_DANGER; f1_bg = UI_DANGER_SOFT;
    } else {
        PtcEffectiveBedtime eff_today = ptc_bedtime_resolve_start_day(
            &eval_rules, model->day_index, ptc_weekday_from_day_index(model->day_index));
        if (eff_today.window.enabled) {
            int diff_m = (int)eff_today.window.start_minute - (int)minute_of_day;
            if (diff_m < 0) diff_m += 1440;
            snprintf(forecast_line1, sizeof(forecast_line1), ptc_ui_text(PTC_UI_T_BEDTIME_STARTS_AT_02U_02U_TONIGHT_ABOUT),
                     (unsigned)(eff_today.window.start_minute / 60),
                     (unsigned)(eff_today.window.start_minute % 60),
                     (unsigned)(diff_m / 60), (unsigned)(diff_m % 60));
            f1_color = UI_SUCCESS; f1_bg = UI_SUCCESS_SOFT;
        } else {
            snprintf(forecast_line1, sizeof(forecast_line1), "%s", ptc_ui_text(PTC_UI_T_TONIGHT_WINDOW_CLOSED_NO_LIMITS));
            f1_color = UI_MUTED; f1_bg = UI_RAISED;
        }
    }

    UiRect status_box = {eval_card.x + 16, eval_card.y + 42, eval_card.width - 32, 58};
    fill_round_rect(pixels, stride, status_box, 10, f1_bg);
    draw_rect_outline(pixels, stride, status_box, 10, 1, f1_color == UI_MUTED ? UI_BORDER : f1_color);
    draw_wrapped_text(pixels, stride, status_box.x + 12, status_box.y + 22,
                      forecast_line1, 13, status_box.width - 24, 18, 2, f1_color);

    int enabled_count = 0, total_span_m = 0;
    for (int d = 0; d < 7; ++d) {
        if (draft->week[d].enabled) {
            enabled_count++;
            total_span_m += (int)(draft->week[d].end_minute + 1440 - draft->week[d].start_minute) % 1440;
        }
    }
    if (draft->enabled) {
        snprintf(forecast_line2, sizeof(forecast_line2), ptc_ui_text(PTC_UI_T_WEEKLY_STATS_D_7_DAYS_ON_U),
                 enabled_count, (unsigned)(total_span_m / 60));
    } else {
        snprintf(forecast_line2, sizeof(forecast_line2), ptc_ui_text(PTC_UI_T_S_D_7_DAYS_PRESET),
                 model->bedtime_dirty ? ptc_ui_text(PTC_UI_T_PAUSED_WEEKLY_AFTER_SAVE)
                                      : ptc_ui_text(PTC_UI_T_WEEKLY_DISABLED_INACTIVE),
                 enabled_count);
    }
    fit_text(fitted, sizeof(fitted), forecast_line2, 12, eval_card.width - 32);
    draw_text(pixels, stride, eval_card.x + 16, eval_card.y + 122, fitted, 12, UI_INK);

    const char *cal_str = draft->calendar_enabled
        ? (draft->enabled ? ptc_ui_text(PTC_UI_T_HOLIDAY_CALENDAR_ON) : ptc_ui_text(PTC_UI_T_HOLIDAY_CALENDAR_INACTIVE))
        : ptc_ui_text(PTC_UI_T_HOLIDAYS_FOLLOW_WEEKLY_PLAN);
    const char *sch_str = draft->scheduled_override.present
        ? (draft->enabled ? ptc_ui_text(PTC_UI_T_SPECIFIC_DATE_ACTIVE) : ptc_ui_text(PTC_UI_T_SPECIFIC_DATE_INACTIVE))
        : ptc_ui_text(PTC_UI_T_SPECIFIC_DATE_NOT_SET);
    char special_line[128];
    snprintf(special_line, sizeof(special_line), ptc_ui_text(PTC_UI_T_SPECIAL_RULES_S_S), cal_str, sch_str);
    draw_wrapped_text(pixels, stride, eval_card.x + 16, eval_card.y + 144,
                      special_line, 11, eval_card.width - 32, 16, 2, UI_MUTED);

    /* Tonight Exception Section (manage buttons drawn inside at Y=434..478) */
    UiRect exc_box = {eval_card.x + 12, eval_card.y + 180, eval_card.width - 24, 78};
    fill_round_rect(pixels, stride, exc_box, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, exc_box, 10, 1, UI_BORDER);
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_BEDTIME_TONIGHT_EXCEPTION), 12, exc_box.width - 20);
    draw_text_bold(pixels, stride, exc_box.x + 10, exc_box.y + 18, fitted, 12, UI_MUTED);

    /* Compact Official Suspend Confirmation Strip */
    UiRect risk = {eval_card.x + 16, eval_card.y + 266, eval_card.width - 32, 58};
    fill_round_rect(pixels, stride, risk, 8,
        model->bedtime_official_setting_confirmed && model->bedtime_overlay_verified
            ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
    draw_wrapped_text(pixels, stride, risk.x + 10, risk.y + 20,
        model->bedtime_official_setting_confirmed
            ? (model->bedtime_overlay_verified
                ? ptc_ui_text(PTC_UI_T_OFFICIAL_SUSPEND_SETTING_CONFIRMED_OVERLAY_VERIFIED)
                : ptc_ui_text(PTC_UI_T_OFFICIAL_SUSPEND_CONFIRMED_SAVE_ACCEPTS_UNVERIFIED_OVERLAY))
            : ptc_ui_text(PTC_UI_T_BEFORE_ENABLING_CONFIRM_NINTENDO_PARENTAL_CONTROLS_HAS),
        11, risk.width - 20, 16, 2,
        model->bedtime_official_setting_confirmed && model->bedtime_overlay_verified
            ? UI_SUCCESS : UI_WARNING_TEXT);
}

static void draw_bedtime_weekly_section(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                        bool is_en, bool bedtime_enforcing, bool is_skipped_active,
                                        bool bedtime_save_danger)
{
    (void)is_en;
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    char line[128], fitted[128];
    int today_weekday = ptc_weekday_from_day_index(model->day_index);
    int ed_day = (model->bedtime_editor_day >= 0 && model->bedtime_editor_day < 7)
        ? model->bedtime_editor_day : ptc_ui_weekday_for_display_slot(0);
    bool focus_active = !model->bedtime_section_focused && !model->parent_footer_focused && !model->bedtime_master_focused;

    /* 1. Top 7 Compact Day Cards (Y=228..342) */
    for (int slot = 0; slot < 7; ++slot) {
        int day = ptc_ui_weekday_for_display_slot(slot);
        UiRect card = to_uirect(ptc_ui_bedtime_field_rect(PTC_UI_BEDTIME_WEEKLY, slot));
        bool is_today = (day == today_weekday);
        bool is_selected_day = (day == ed_day);
        bool is_focused = focus_active && model->selected_index == slot;
        bool is_active_day = (bedtime_enforcing && is_today);
        bool changed = draft->week[day].enabled != model->bedtime_policy.week[day].enabled ||
            draft->week[day].start_minute != model->bedtime_policy.week[day].start_minute ||
            draft->week[day].end_minute != model->bedtime_policy.week[day].end_minute;

        uint32_t bg = is_active_day ? UI_DANGER_SOFT :
            (is_selected_day ? UI_ACCENT_SOFT : UI_SURFACE);
        if (!is_focused) draw_card_shadow(pixels, stride, card, 12);
        fill_round_rect(pixels, stride, card, 12, bg);
        if (is_focused) {
            draw_focus_ring(pixels, stride, card, 12);
        } else if (is_selected_day) {
            draw_rect_outline(pixels, stride, card, 12, 2, is_active_day ? UI_DANGER : UI_ACCENT);
        } else {
            draw_rect_outline(pixels, stride, card, 12, 1, is_active_day ? UI_DANGER : UI_BORDER);
        }
        if (changed) fill_round_rect(pixels, stride, (UiRect){card.x + card.width - 12, card.y + 8, 5, 5}, 2, UI_WARNING);

        fit_text(fitted, sizeof(fitted), ptc_ui_weekday_label((unsigned)day), 15, card.width - 14);
        draw_text_center(pixels, stride, (UiRect){card.x + 4, card.y + 10, card.width - 8, 22}, fitted, 15,
                         is_active_day ? UI_DANGER : (is_selected_day ? UI_ACCENT : UI_INK));

        if (draft->week[day].enabled) {
            snprintf(line, sizeof(line), "%02u:%02u",
                (unsigned)(draft->week[day].start_minute / 60), (unsigned)(draft->week[day].start_minute % 60));
            draw_text_center(pixels, stride, (UiRect){card.x + 4, card.y + 38, card.width - 8, 24}, line, 18,
                             draft->enabled ? (is_active_day ? UI_DANGER : (is_skipped_active && is_today ? UI_SUCCESS : UI_ACCENT)) : UI_MUTED);
            snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_TO_02U_02U),
                (unsigned)(draft->week[day].end_minute / 60), (unsigned)(draft->week[day].end_minute % 60));
            fit_text(fitted, sizeof(fitted), line, 11, card.width - 10);
            draw_text_center(pixels, stride, (UiRect){card.x + 4, card.y + 66, card.width - 8, 16}, fitted, 11, UI_MUTED);
        } else {
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_OFF), 16, card.width - 12);
            draw_text_center(pixels, stride, (UiRect){card.x + 4, card.y + 44, card.width - 8, 24}, fitted, 16, UI_MUTED);
        }

        if (is_today) {
            const char *tag = is_active_day ? ptc_ui_text(PTC_UI_T_ACTIVE_3) :
                (is_skipped_active ? ptc_ui_text(PTC_UI_T_SKIPPED_2) : ptc_ui_text(PTC_UI_T_TODAY_4));
            uint32_t tag_bg = is_active_day ? UI_DANGER : (is_skipped_active ? UI_SUCCESS : UI_ACCENT);
            UiRect pill = {card.x + 12, card.y + card.height - 24, card.width - 24, 18};
            fill_round_rect(pixels, stride, pill, 5, tag_bg);
            fit_text(fitted, sizeof(fitted), tag, 10, pill.width - 6);
            draw_text_center(pixels, stride, pill, fitted, 10, UI_ON_ACCENT);
        } else {
            UiRect bar_ind = {card.x + 16, card.y + card.height - 10, card.width - 32, 4};
            fill_round_rect(pixels, stride, bar_ind, 2,
                            draft->week[day].enabled ? (draft->enabled ? UI_NIGHT : UI_MUTED) : UI_BORDER);
        }
    }

    /* 2. Inline Selected-Day Editor Card (Y=354..632) */
    const PtcBedtimeWindow *win = &draft->week[ed_day];
    UiRect editor = to_uirect(ptc_ui_bedtime_inline_editor_rect());
    draw_plan_card(pixels, stride, editor, false);

    /* Editor Header Row */
    snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_BEDTIME_EDITING_DAY), ptc_ui_weekday_label((unsigned)ed_day));
    fit_text(fitted, sizeof(fitted), line, 18, 412 - (editor.x + 16) - 8);
    draw_text_bold(pixels, stride, editor.x + 16, editor.y + 30, fitted, 18, UI_INK);
    if (win->enabled) {
        int dur = (int)(win->end_minute + 1440 - win->start_minute) % 1440;
        snprintf(line, sizeof(line), "%02u:%02u - %02u:%02u (%uh%02um)",
                 (unsigned)(win->start_minute / 60), (unsigned)(win->start_minute % 60),
                 (unsigned)(win->end_minute / 60), (unsigned)(win->end_minute % 60),
                 (unsigned)(dur / 60), (unsigned)(dur % 60));
    } else {
        snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_TODAY_S_BEDTIME_RESTRICTION_IS_NOT_ENABLED));
    }
    fit_text(fitted, sizeof(fitted), line, 12, 412 - (editor.x + 16) - 8);
    draw_text(pixels, stride, editor.x + 16, editor.y + 49, fitted, 12, win->enabled ? UI_NIGHT : UI_MUTED);

    /* Field 11: Day On/Off Toggle + Field 7/8: Copy to Weekdays / Weekends */
    draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 11),
        win->enabled ? ptc_ui_text(PTC_UI_T_BEDTIME_DAY_ON) : ptc_ui_text(PTC_UI_T_BEDTIME_DAY_OFF),
        win->enabled ? UI_SUCCESS_SOFT : UI_RAISED, win->enabled ? UI_SUCCESS : UI_MUTED,
        focus_active && model->selected_index == 11, model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 7), ptc_ui_text(PTC_UI_T_COPY_TO_WEEKDAYS),
        UI_PAGE, UI_ACCENT, focus_active && model->selected_index == 7, model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 8), ptc_ui_text(PTC_UI_T_COPY_TO_WEEKENDS),
        UI_PAGE, UI_ACCENT, focus_active && model->selected_index == 8, model->disable_flag_present);

    /* 24-Hour Overnight Timeline Bar + Ticks */
    UiRect tbar = to_uirect(ptc_ui_bedtime_inline_timeline_rect());
    draw_bedtime_timeline_strip(pixels, stride, tbar, win->start_minute, win->end_minute, win->enabled);
    {
        int ty = tbar.y + tbar.height + 14;
        draw_text(pixels, stride, tbar.x, ty, "00:00", 10, UI_MUTED);
        draw_text(pixels, stride, tbar.x + tbar.width / 4 - 14, ty, "06:00", 10, UI_MUTED);
        draw_text(pixels, stride, tbar.x + tbar.width / 2 - 14, ty, "12:00", 10, UI_MUTED);
        draw_text(pixels, stride, tbar.x + tbar.width * 3 / 4 - 14, ty, "18:00", 10, UI_MUTED);
        draw_text(pixels, stride, tbar.x + tbar.width - 32, ty, "24:00", 10, UI_MUTED);
    }

    /* Fields 12 & 13: Start Time & Next-Day End Time Cards */
    for (int f = 0; f < 2; ++f) {
        int fid = 12 + f;
        UiRect tbox = to_uirect(ptc_ui_bedtime_field_rect(0, fid));
        bool focused = focus_active && model->selected_index == fid;
        unsigned mins = f == 0 ? win->start_minute : win->end_minute;
        fill_round_rect(pixels, stride, tbox, 10, focused ? UI_ACCENT_SOFT : UI_RAISED);
        if (focused) draw_focus_ring(pixels, stride, tbox, 10);
        else draw_rect_outline(pixels, stride, tbox, 10, 1, UI_BORDER);
        const char *lbl = ptc_ui_text(f == 0 ? PTC_UI_T_BEDTIME_START_SHORT : PTC_UI_T_BEDTIME_END_SHORT);
        const char *hint = win->enabled ? ptc_ui_text(PTC_UI_T_A_FINE_ADJUSTMENT) : ptc_ui_text(PTC_UI_T_NOT_EDITABLE);
        int hw = measure_text(hint, 12);
        fit_text(fitted, sizeof(fitted), lbl, 13, tbox.width - hw - 36);
        draw_text(pixels, stride, tbox.x + 14, tbox.y + 22, fitted, 13, win->enabled ? UI_MUTED : UI_DISABLED);
        snprintf(line, sizeof(line), "%02u:%02u", mins / 60, mins % 60);
        draw_text_bold(pixels, stride, tbox.x + 14, tbox.y + 49, line, 22,
                       win->enabled && !model->disable_flag_present ? UI_ACCENT : UI_DISABLED);
        draw_text(pixels, stride, tbox.x + tbox.width - 14 - hw, tbox.y + 46, hint, 12,
                  win->enabled ? UI_MUTED : UI_DISABLED);
    }

    /* Fields 14..18: 5 Clickable Family Preset Chips */
    PtcUiRect preset_rects[5];
    for (int p = 0; p < 5; ++p) preset_rects[p] = ptc_ui_bedtime_field_rect(0, 14 + p);
    int preset_focus = (focus_active && model->selected_index >= 14 && model->selected_index <= 18)
        ? (model->selected_index - 14) : -1;
    draw_bedtime_preset_chip_row(pixels, stride, win, !model->disable_flag_present, preset_rects, preset_focus);

    /* Right-column Bottom Actions: Discard (9) & Save (10) */
    draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 9), ptc_ui_text(PTC_UI_T_ZL_DISCARD),
        UI_PAGE, UI_INK, focus_active && model->selected_index == 9, !model->bedtime_dirty || model->waiting);
    draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 10),
        model->disable_flag_present ? ptc_ui_text(PTC_UI_T_READ_ONLY) :
        (model->waiting ? ptc_ui_text(PTC_UI_T_SAVING_2) :
         (model->bedtime_dirty ? (bedtime_save_danger ? ptc_ui_text(PTC_UI_T_SAVE_CONFIRM) : ptc_ui_text(PTC_UI_T_SAVE_PAGE))
                               : ptc_ui_text(PTC_UI_T_SAVED))),
        bedtime_save_danger ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT,
        focus_active && model->selected_index == 10,
        !model->bedtime_dirty || model->disable_flag_present || model->waiting);
}

static void draw_bedtime_mode_bar(uint32_t *pixels, uint32_t stride, UiRect bar, PtcBedtimeOverrideMode mode, bool active)
{
    static const PtcBedtimeOverrideMode MODES[3] = {
        PTC_BEDTIME_OVERRIDE_INHERIT, PTC_BEDTIME_OVERRIDE_CUSTOM, PTC_BEDTIME_OVERRIDE_DISABLED
    };
    fill_round_rect(pixels, stride, bar, 9, UI_PAGE);
    draw_rect_outline(pixels, stride, bar, 9, 1, UI_BORDER);
    int seg_w = (bar.width - 8) / 3;
    for (int m = 0; m < 3; ++m) {
        UiRect seg = {bar.x + 4 + m * seg_w, bar.y + 4, (m == 2) ? (bar.width - 8 - 2 * seg_w) : seg_w, bar.height - 8};
        bool sel = (mode == MODES[m]);
        char fitted[48];
        if (sel) {
            uint32_t bg = !active ? UI_MUTED :
                (MODES[m] == PTC_BEDTIME_OVERRIDE_CUSTOM ? UI_ACCENT :
                 (MODES[m] == PTC_BEDTIME_OVERRIDE_INHERIT ? UI_SUCCESS : UI_WARNING));
            fill_round_rect(pixels, stride, seg, 7, bg);
        }
        fit_text(fitted, sizeof(fitted), bedtime_override_label(MODES[m]), 12, seg.width - 6);
        draw_text_center(pixels, stride, seg, fitted, 12, sel ? UI_ON_ACCENT : UI_MUTED);
    }
}

static void draw_bedtime_calendar_section(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                          bool is_en, bool bedtime_save_danger)
{
    (void)is_en;
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    char fitted[128];
    bool focus_active = !model->bedtime_section_focused && !model->parent_footer_focused && !model->bedtime_master_focused;

    /* Field 0: Holiday Calendar Switch Card */
    UiRect master = to_uirect(ptc_ui_bedtime_field_rect(1, 0));
    bool f0 = focus_active && model->selected_index == 0;
    draw_plan_card(pixels, stride, master, f0);
    UiRect cal_toggle = {master.x + master.width - 74, master.y + (master.height - 28) / 2, 58, 28};
    const char *st = !draft->enabled
        ? (draft->calendar_enabled ? ptc_ui_text(PTC_UI_T_ON_INACTIVE) : ptc_ui_text(PTC_UI_T_OFF_2))
        : (draft->calendar_enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF));
    int st_w = measure_text(st, 13) + 20;
    if (st_w > 190) st_w = 190;
    UiRect st_pill = {cal_toggle.x - st_w - 14, master.y + (master.height - 26) / 2, st_w, 26};
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAY_CALENDAR), 18,
             st_pill.x - (master.x + 18) - 12);
    draw_text_bold(pixels, stride, master.x + 18, master.y + 40, fitted, 18, UI_INK);
    fill_round_rect(pixels, stride, st_pill, 13,
                    draft->enabled && draft->calendar_enabled ? UI_SUCCESS_SOFT : UI_RAISED);
    fit_text(fitted, sizeof(fitted), st, 13, st_pill.width - 10);
    draw_text_center(pixels, stride, st_pill, fitted, 13,
                     draft->enabled && draft->calendar_enabled ? UI_SUCCESS : UI_MUTED);
    draw_toggle_switch(pixels, stride, cal_toggle, draft->calendar_enabled, f0,
                       !draft->enabled || model->disable_flag_present, NULL, NULL);

    /* Fields 1 & 2: Equal-Height Side-by-Side Statutory / Makeup Cards */
    for (int i = 0; i < 2; ++i) {
        const PtcBedtimeSpecialRule *rule = i == 0 ? &draft->holiday_rule : &draft->makeup_workday_rule;
        UiRect card = to_uirect(ptc_ui_bedtime_field_rect(1, i + 1));
        bool focused = focus_active && model->selected_index == i + 1;
        bool custom = rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM;
        bool active = draft->enabled && draft->calendar_enabled;
        char value[64];

        draw_plan_card(pixels, stride, card, focused);
        fit_text(fitted, sizeof(fitted),
                 ptc_ui_text(i == 0 ? PTC_UI_T_STATUTORY_HOLIDAY : PTC_UI_T_MAKEUP_WORKDAY),
                 18, card.width - 36);
        draw_text_bold(pixels, stride, card.x + 18, card.y + 32, fitted, 18, active ? UI_INK : UI_MUTED);

        draw_bedtime_mode_bar(pixels, stride, (UiRect){card.x + 16, card.y + 48, card.width - 32, 34},
                              rule->mode, active);

        if (custom) {
            draw_bedtime_window_value(value, sizeof(value), &rule->window);
            fit_text(fitted, sizeof(fitted), value, 19, card.width - 36);
            draw_text_bold(pixels, stride, card.x + 18, card.y + 116, fitted, 19, active ? UI_ACCENT : UI_MUTED);
            draw_bedtime_timeline_strip(pixels, stride, (UiRect){card.x + 16, card.y + 132, card.width - 32, 26},
                                        rule->window.start_minute, rule->window.end_minute, rule->window.enabled);
        } else {
            const char *desc = rule->mode == PTC_BEDTIME_OVERRIDE_DISABLED
                ? ptc_ui_text(PTC_UI_T_THERE_IS_NO_SLEEPING_LIMIT_ON_THAT)
                : ptc_ui_text(PTC_UI_T_FOLLOW_THE_WEEKLY_SCHEDULE_USE_THE_BEDTIME);
            draw_wrapped_text(pixels, stride, card.x + 18, card.y + 112, desc,
                              13, card.width - 36, 19, 3, UI_MUTED);
        }

        UiRect edit_bar = {card.x + 16, card.y + card.height - 46, card.width - 32, 32};
        fill_round_rect(pixels, stride, edit_bar, 8, focused ? UI_ACCENT : UI_RAISED);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_BEDTIME_EDIT_RULE_HINT), 13, edit_bar.width - 12);
        draw_text_center(pixels, stride, edit_bar, fitted, 13, focused ? UI_ON_ACCENT : UI_ACCENT);
    }

    /* Bottom Priority / Master-Off Strip */
    UiRect cal_guide = {54, 560, 768, 72};
    fill_round_rect(pixels, stride, cal_guide, 12, !draft->enabled ? UI_WARNING_SOFT : UI_SURFACE);
    draw_rect_outline(pixels, stride, cal_guide, 12, 1, !draft->enabled ? UI_WARNING : UI_BORDER);
    if (!draft->enabled) {
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_BEDTIME_SWITCH_IS_OFF), 14, cal_guide.width - 32);
        draw_text_bold(pixels, stride, cal_guide.x + 16, cal_guide.y + 24, fitted, 14, UI_WARNING_TEXT);
        draw_wrapped_text(pixels, stride, cal_guide.x + 16, cal_guide.y + 44,
                          ptc_ui_text(PTC_UI_T_SETTINGS_ARE_SAVED_PRESS_OR_TURN_ON),
                          12, cal_guide.width - 32, 16, 2, UI_MUTED);
    } else {
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_HOLIDAY_CALENDAR_RULE_PRIORITIES), 14, cal_guide.width - 32);
        draw_text_bold(pixels, stride, cal_guide.x + 16, cal_guide.y + 24, fitted, 14, UI_INK);
        draw_wrapped_text(pixels, stride, cal_guide.x + 16, cal_guide.y + 44,
                          ptc_ui_text(PTC_UI_T_WHEN_ENABLED_HOLIDAYS_AND_WORKDAYS_TAKE_PRIORITY),
                          12, cal_guide.width - 32, 16, 2, UI_MUTED);
    }

    /* Right-column Bottom Actions: Discard (3) & Save (4) */
    draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(1, 3), ptc_ui_text(PTC_UI_T_ZL_DISCARD),
        UI_PAGE, UI_INK, focus_active && model->selected_index == 3, !model->bedtime_dirty || model->waiting);
    draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(1, 4),
        model->disable_flag_present ? ptc_ui_text(PTC_UI_T_READ_ONLY) :
        (model->waiting ? ptc_ui_text(PTC_UI_T_SAVING_2) :
         (model->bedtime_dirty ? (bedtime_save_danger ? ptc_ui_text(PTC_UI_T_SAVE_CONFIRM) : ptc_ui_text(PTC_UI_T_SAVE_PAGE))
                               : ptc_ui_text(PTC_UI_T_SAVED))),
        bedtime_save_danger ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT,
        focus_active && model->selected_index == 4,
        !model->bedtime_dirty || model->disable_flag_present || model->waiting);
}

static void draw_bedtime_scheduled_section(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                           bool is_en, bool bedtime_save_danger)
{
    (void)is_en;
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    const PtcBedtimeScheduledOverride *scheduled = &draft->scheduled_override;
    uint32_t duration = scheduled->end_day_index >= scheduled->start_day_index
        ? (uint32_t)scheduled->end_day_index - scheduled->start_day_index + 1u : 1u;
    uint16_t sy = 0, ey = 0; uint8_t sm = 0, sd = 0, em = 0, ed = 0;
    bool s_ok = ptc_date_from_day_index(scheduled->start_day_index, &sy, &sm, &sd);
    bool e_ok = ptc_date_from_day_index(scheduled->end_day_index, &ey, &em, &ed);
    bool focus_active = !model->bedtime_section_focused && !model->parent_footer_focused && !model->bedtime_master_focused;
    bool active = draft->enabled && scheduled->present;
    char line[128], fitted[128];

    /* Top Card: Date Range Container */
    UiRect range_card = {54, 228, 768, 178};
    draw_plan_card(pixels, stride, range_card, false);

    /* Field 0: Switch row */
    UiRect row0 = to_uirect(ptc_ui_bedtime_field_rect(2, 0));
    bool f0 = focus_active && model->selected_index == 0;
    fill_round_rect(pixels, stride, row0, 10, f0 ? UI_ACCENT_SOFT : UI_RAISED);
    if (f0) draw_focus_ring(pixels, stride, row0, 10);
    else draw_rect_outline(pixels, stride, row0, 10, 1, UI_BORDER);
    UiRect sched_toggle = {row0.x + row0.width - 70, row0.y + (row0.height - 28) / 2, 56, 28};
    const char *st = !draft->enabled
        ? (scheduled->present ? ptc_ui_text(PTC_UI_T_ON_INACTIVE_2) : ptc_ui_text(PTC_UI_T_OFF))
        : (scheduled->present ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF));
    int st_w = measure_text(st, 12) + 20;
    if (st_w > 180) st_w = 180;
    UiRect st_pill = {sched_toggle.x - st_w - 12, row0.y + (row0.height - 24) / 2, st_w, 24};
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_GO_TO_BED_ON_THE_SPECIFIED_DATE), 16,
             st_pill.x - (row0.x + 14) - 10);
    draw_text_bold(pixels, stride, row0.x + 14, row0.y + 30, fitted, 16, UI_INK);
    fill_round_rect(pixels, stride, st_pill, 12, active ? UI_SUCCESS_SOFT : UI_PAGE);
    fit_text(fitted, sizeof(fitted), st, 12, st_pill.width - 10);
    draw_text_center(pixels, stride, st_pill, fitted, 12, active ? UI_SUCCESS : UI_MUTED);
    draw_toggle_switch(pixels, stride, sched_toggle, scheduled->present, f0,
                       !draft->enabled || model->disable_flag_present, NULL, NULL);

    /* Visual Date Range Bar */
    UiRect rbar = {68, 296, 740, 34};
    fill_round_rect(pixels, stride, rbar, 8, active ? UI_ACCENT_SOFT : UI_PAGE);
    draw_rect_outline(pixels, stride, rbar, 8, 1, active ? UI_ACCENT : UI_BORDER);
    if (s_ok && e_ok) {
        char sbuf[24], ebuf[24], spanbuf[32];
        snprintf(sbuf, sizeof(sbuf), "%04u-%02u-%02u", (unsigned)sy, (unsigned)sm, (unsigned)sd);
        snprintf(ebuf, sizeof(ebuf), "%04u-%02u-%02u", (unsigned)ey, (unsigned)em, (unsigned)ed);
        snprintf(spanbuf, sizeof(spanbuf), ptc_ui_text(PTC_UI_T_SCHEDULED_DAYS_TOTAL), (unsigned)duration);
        snprintf(line, sizeof(line), "%s  |  %s  |  %s", sbuf, spanbuf, ebuf);
    } else {
        snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_WAITING_FOR_DATE));
    }
    fit_text(fitted, sizeof(fitted), line, 14, rbar.width - 24);
    draw_text_center(pixels, stride, rbar, fitted, 14, active ? UI_ACCENT : UI_MUTED);

    /* Fields 1 & 2: Start Date & Duration side-by-side */
    for (int f = 0; f < 2; ++f) {
        UiRect cell = to_uirect(ptc_ui_bedtime_field_rect(2, 1 + f));
        bool focused = focus_active && model->selected_index == 1 + f;
        fill_round_rect(pixels, stride, cell, 9, focused ? UI_ACCENT_SOFT : UI_RAISED);
        if (focused) draw_focus_ring(pixels, stride, cell, 9);
        else draw_rect_outline(pixels, stride, cell, 9, 1, UI_BORDER);
        const char *lbl = ptc_ui_text(f == 0 ? PTC_UI_T_START_DATE : PTC_UI_T_NUMBER_OF_DAYS_TO_LAST);
        const char *hint = "A | ZL/ZR +-7d";
        int hw = measure_text(hint, 11);
        fit_text(fitted, sizeof(fitted), lbl, 12, cell.width - hw - 32);
        draw_text(pixels, stride, cell.x + 14, cell.y + 20, fitted, 12, UI_MUTED);
        draw_text(pixels, stride, cell.x + cell.width - 14 - hw, cell.y + 20, hint, 11, UI_MUTED);
        if (f == 0) {
            if (s_ok) snprintf(line, sizeof(line), "%04u-%02u-%02u", (unsigned)sy, (unsigned)sm, (unsigned)sd);
            else snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_WAITING_FOR_DATE));
        } else {
            snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_DAYS), (unsigned)duration);
        }
        fit_text(fitted, sizeof(fitted), line, 18, cell.width - 28);
        draw_text_bold(pixels, stride, cell.x + 14, cell.y + 45, fitted, 18, UI_INK);
    }

    /* Field 3: Bedtime Rule Card with 3-state mode + timeline */
    UiRect rule_card = to_uirect(ptc_ui_bedtime_field_rect(2, 3));
    bool f3 = focus_active && model->selected_index == 3;
    bool custom = scheduled->rule.mode == PTC_BEDTIME_OVERRIDE_CUSTOM;
    draw_plan_card(pixels, stride, rule_card, f3);
    {
        const char *ehint = ptc_ui_text(PTC_UI_T_BEDTIME_EDIT_RULE_HINT);
        int ew = measure_text(ehint, 12) + 20;
        if (ew > 210) ew = 210;
        UiRect epill = {rule_card.x + rule_card.width - 16 - ew, rule_card.y + 12, ew, 26};
        fill_round_rect(pixels, stride, epill, 8, f3 ? UI_ACCENT : UI_RAISED);
        fit_text(fitted, sizeof(fitted), ehint, 12, epill.width - 10);
        draw_text_center(pixels, stride, epill, fitted, 12, f3 ? UI_ON_ACCENT : UI_ACCENT);

        UiRect mbar = {rule_card.x + 210, rule_card.y + 10, epill.x - (rule_card.x + 224), 30};
        if (mbar.width >= 180) {
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_BEDTIME_RULES), 16, 184);
            draw_text_bold(pixels, stride, rule_card.x + 16, rule_card.y + 30, fitted, 16, UI_INK);
            draw_bedtime_mode_bar(pixels, stride, mbar, scheduled->rule.mode, active);
        } else {
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_BEDTIME_RULES), 16,
                     epill.x - (rule_card.x + 16) - 12);
            draw_text_bold(pixels, stride, rule_card.x + 16, rule_card.y + 30, fitted, 16, UI_INK);
        }
    }
    if (custom) {
        char value[64];
        draw_bedtime_window_value(value, sizeof(value), &scheduled->rule.window);
        fit_text(fitted, sizeof(fitted), value, 18, rule_card.width - 32);
        draw_text_bold(pixels, stride, rule_card.x + 16, rule_card.y + 68, fitted, 18, active ? UI_ACCENT : UI_MUTED);
        draw_bedtime_timeline_strip(pixels, stride, (UiRect){rule_card.x + 16, rule_card.y + 84, rule_card.width - 32, 30},
                                    scheduled->rule.window.start_minute, scheduled->rule.window.end_minute,
                                    scheduled->rule.window.enabled);
    } else {
        const char *desc = scheduled->rule.mode == PTC_BEDTIME_OVERRIDE_DISABLED
            ? ptc_ui_text(PTC_UI_T_THERE_IS_NO_SLEEPING_LIMIT_ON_THAT)
            : ptc_ui_text(PTC_UI_T_FOLLOW_THE_WEEKLY_SCHEDULE_USE_THE_BEDTIME);
        fit_text(fitted, sizeof(fitted), bedtime_override_label(scheduled->rule.mode), 18, rule_card.width - 32);
        draw_text_bold(pixels, stride, rule_card.x + 16, rule_card.y + 70, fitted, 18, UI_MUTED);
        draw_wrapped_text(pixels, stride, rule_card.x + 16, rule_card.y + 96, desc,
                          12, rule_card.width - 32, 17, 2, UI_MUTED);
    }

    /* Bottom Priority / Master-Off Strip */
    UiRect sched_guide = {54, 564, 768, 68};
    fill_round_rect(pixels, stride, sched_guide, 12, !draft->enabled ? UI_WARNING_SOFT : UI_SURFACE);
    draw_rect_outline(pixels, stride, sched_guide, 12, 1, !draft->enabled ? UI_WARNING : UI_BORDER);
    if (!draft->enabled) {
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_BEDTIME_SWITCH_IS_OFF), 14, sched_guide.width - 32);
        draw_text_bold(pixels, stride, sched_guide.x + 16, sched_guide.y + 24, fitted, 14, UI_WARNING_TEXT);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_CURRENTLY_INACTIVE_ACTIVATES_WHEN_BEDTIME_SWITCH_IS),
                 12, sched_guide.width - 32);
        draw_text(pixels, stride, sched_guide.x + 16, sched_guide.y + 48, fitted, 12, UI_MUTED);
    } else {
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SPECIFIC_DATE_BEDTIME_PRIORITY), 14, sched_guide.width - 32);
        draw_text_bold(pixels, stride, sched_guide.x + 16, sched_guide.y + 24, fitted, 14, UI_INK);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_TAKES_PRIORITY_OVER_HOLIDAYS_AND_WEEKLY_PLANS),
                 12, sched_guide.width - 32);
        draw_text(pixels, stride, sched_guide.x + 16, sched_guide.y + 48, fitted, 12, UI_MUTED);
    }

    /* Right-column Bottom Actions: Discard (4) & Save (5) */
    draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(2, 4), ptc_ui_text(PTC_UI_T_ZL_DISCARD),
        UI_PAGE, UI_INK, focus_active && model->selected_index == 4, !model->bedtime_dirty || model->waiting);
    draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(2, 5),
        model->disable_flag_present ? ptc_ui_text(PTC_UI_T_READ_ONLY) :
        (model->waiting ? ptc_ui_text(PTC_UI_T_SAVING_2) :
         (model->bedtime_dirty ? (bedtime_save_danger ? ptc_ui_text(PTC_UI_T_SAVE_CONFIRM) : ptc_ui_text(PTC_UI_T_SAVE_PAGE))
                               : ptc_ui_text(PTC_UI_T_SAVED))),
        bedtime_save_danger ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT,
        focus_active && model->selected_index == 5,
        !model->bedtime_dirty || model->disable_flag_present || model->waiting);
}

static void draw_bedtime_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    bool is_en = (ptc_ui_language_get_resolved() == PTC_UI_LANGUAGE_ENGLISH);
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    int64_t raw_now = ptc_ui_render_now();
    uint16_t minute_of_day = ptc_ui_render_minute_of_day(raw_now);
    draw_bedtime_sections(pixels, stride, model);

    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    PtcUiBedtimeImpact bedtime_impact = ptc_ui_bedtime_save_impact(model,
        minute_of_day, (int64_t)raw_now);
    bool bedtime_save_danger = model->bedtime_dirty &&
        (bedtime_impact == PTC_UI_BEDTIME_IMPACT_RESTRICT ||
         bedtime_impact == PTC_UI_BEDTIME_IMPACT_UNKNOWN);
    bool is_skipped_active = ptc_ui_status_is_fresh(model, raw_now) &&
        (bedtime_impact == PTC_UI_BEDTIME_IMPACT_SKIPPED ||
         ptc_ui_bedtime_skip_matches_policy(model, draft));

    draw_bedtime_master_card(pixels, stride, model, is_en, bedtime_enforcing);
    draw_bedtime_preview(pixels, stride, model, minute_of_day, bedtime_impact, is_skipped_active);
    /* Manage buttons sit inside the right Tonight Exception card, so draw after preview card! */
    draw_bedtime_manage_buttons(pixels, stride, model, raw_now);
    if (model->bedtime_section == PTC_UI_BEDTIME_WEEKLY) {
        draw_bedtime_weekly_section(pixels, stride, model, is_en, bedtime_enforcing,
                                    is_skipped_active, bedtime_save_danger);
    } else if (model->bedtime_section == PTC_UI_BEDTIME_CALENDAR) {
        draw_bedtime_calendar_section(pixels, stride, model, is_en, bedtime_save_danger);
    } else {
        draw_bedtime_scheduled_section(pixels, stride, model, is_en, bedtime_save_danger);
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

    /* 右上角单日规则提示说明（原重复的规则按钮已由底栏常驻呈现） */
    const char *daily_hint = ptc_ui_text(PTC_UI_T_PRESS_A_OR_CLICK_TO_VIEW_DAILY);
    int hint_size = is_en ? 12 : 13;
    int hint_w = measure_text(daily_hint, hint_size);
    int hint_x = panel.x + panel.width - 16 - hint_w;
    draw_text(pixels, stride, hint_x, panel.y + 26, daily_hint, hint_size, UI_MUTED);

    if (!model->forecast_available) {
        draw_text(pixels, stride, panel.x + 16, panel.y + 110, ptc_ui_text(PTC_UI_T_REFRESH_STATUS_TO_VIEW_FORECAST_DATA), 15, UI_MUTED);
        return;
    }

    PtcRules rules;
    ptc_ui_build_plan_rules(model, PTC_UI_PLAN_WEEKLY, &rules);
    for (index = 0; index < (int)PTC_RESULT_FORECAST_DAYS; ++index) {
        const PtcResultForecastDay *day = &model->forecast[index];
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
            draw_rect_outline(pixels, stride, row, 8, 1, UI_BORDER);
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
    bool master_focused = (model->eye_care_field_focus == 0 && !model->parent_footer_focused);
    if (!master_focused) draw_card_shadow(pixels, stride, master_card, 14);
    fill_round_rect(pixels, stride, master_card, 14, master_focused ? UI_ACCENT_SOFT : UI_RGB(UI_BLENDED(surface)));
    if (master_focused) {
        draw_focus_ring(pixels, stride, master_card, 14);
    } else if (resting) {
        int phase = get_breathing_phase();
        draw_rect_outline(pixels, stride, master_card, 14, 2,
                          UI_RGB(ui_mix_rgb(UI_BLENDED(danger), 0xFF9A8A, phase * 4)));
    } else {
        draw_rect_outline(pixels, stride, master_card, 14, 1, UI_BORDER);
    }
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
            draw_rect_outline(pixels, stride, draft_pill, 6, 1, UI_WARNING_BORDER);
            draw_text_center(pixels, stride, draft_pill,
                             draft->enabled ? (ptc_ui_text(PTC_UI_T_DRAFT_ON_PRESS))
                                            : (ptc_ui_text(PTC_UI_T_DRAFT_OFF_PRESS)),
                             12, UI_WARNING_TEXT);
        }
        int phase_x = switch_dirty ? (draft_pill.x + draft_pill.width + 12) : (saved_pill.x + saved_pill.width + 12);
        UiRect phase_pill = {phase_x, master_card.y + 12, master_card.width - (phase_x - master_card.x) - 200, 24};
        char phase_text[128];
        ptc_ui_format_eye_care_cycle(model, raw_now, phase_text, sizeof(phase_text));
        uint32_t phase_bg = resting ? UI_DANGER_SOFT : (playing ? UI_SUCCESS_SOFT : (paused ? UI_WARNING_SOFT : UI_RAISED));
        uint32_t phase_fg = resting ? UI_DANGER : (playing ? UI_SUCCESS : (paused ? UI_WARNING_TEXT : UI_MUTED));
        uint32_t phase_bd = resting ? UI_DANGER_BORDER : (playing ? UI_SUCCESS_BORDER : (paused ? UI_WARNING_BORDER : UI_BORDER));
        fill_round_rect(pixels, stride, phase_pill, 6, phase_bg);
        draw_rect_outline(pixels, stride, phase_pill, 6, 1, phase_bd);
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
    draw_card_shadow(pixels, stride, time_card, 14);
    fill_round_rect(pixels, stride, time_card, 14, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, time_card, 14, 1, UI_BORDER);

    /* Row 1: 连续游玩时长 */
    UiRect play_row = {time_card.x + 12, time_card.y + 10, time_card.width - 24, 66};
    if (model->eye_care_field_focus == 1 && !model->parent_footer_focused) {
        fill_round_rect(pixels, stride, play_row, 10, UI_ACCENT_SOFT);
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
    if (model->eye_care_field_focus == 2 && !model->parent_footer_focused) {
        fill_round_rect(pixels, stride, rest_row, 10, UI_ACCENT_SOFT);
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
    draw_card_shadow(pixels, stride, preview_card, 14);
    fill_round_rect(pixels, stride, preview_card, 14, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, preview_card, 14, 1, UI_BORDER);
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
    draw_card_shadow(pixels, stride, guide_card, 14);
    fill_round_rect(pixels, stride, guide_card, 14, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, guide_card, 14, 1, UI_BORDER);

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
    bool save_focused = (model->eye_care_field_focus == 3 && !model->parent_footer_focused);
    if (dirty && !save_focused) draw_card_shadow(pixels, stride, save_btn, 12);
    fill_round_rect(pixels, stride, save_btn, 12, dirty ? UI_SUCCESS : (save_focused ? UI_ACCENT_SOFT : UI_RAISED));
    if (save_focused) {
        draw_focus_ring(pixels, stride, save_btn, 12);
    } else {
        draw_rect_outline(pixels, stride, save_btn, 12, 1, dirty ? UI_SUCCESS : UI_BORDER);
    }
    draw_button_label(pixels, stride, save_btn,
                      dirty ? (ptc_ui_text(PTC_UI_T_EYE_CARE_SAVE)) : (ptc_ui_text(PTC_UI_T_RULE_SAVED)),
                      16, dirty ? UI_ON_ACCENT : (save_focused ? UI_ACCENT : UI_MUTED));

    if (resting) {
        UiRect skip_btn = to_uirect(ptc_ui_eye_care_page_skip_rect());
        bool skip_focused = (model->eye_care_field_focus == 4 && !model->parent_footer_focused);
        char label[96];
        snprintf(label, sizeof(label), "X %s", ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP));
        if (!skip_focused) draw_card_shadow(pixels, stride, skip_btn, 12);
        fill_round_rect(pixels, stride, skip_btn, 12, UI_DANGER_SOFT);
        if (skip_focused) {
            draw_focus_ring(pixels, stride, skip_btn, 12);
        } else {
            draw_rect_outline(pixels, stride, skip_btn, 12, 1, UI_DANGER);
        }
        draw_button_label(pixels, stride, skip_btn, label, 16, UI_DANGER);
    }
}

void draw_dock_usage_card(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, UiRect card)
{
    bool status_fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    bool fresh = model->dock_available && status_fresh;
    bool known = fresh && model->undocked_usage_available;
    bool active = model->dock_policy.force_docked || model->dock_policy.undocked_limit_enabled;
    bool compact = card.height < 200;
    char value[96], line[192];
    int step = compact ? 22 : 24;
    draw_plan_card(pixels, stride, card, false);
    draw_text(pixels, stride, card.x + 20, card.y + 32, ptc_ui_text(PTC_UI_T_DOCK_TITLE), 17, UI_INK);
    const char *mode = !fresh ? ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM) :
        strcmp(model->operation_mode, "docked") == 0 ? ptc_ui_text(PTC_UI_T_DOCK_TV) :
        strcmp(model->operation_mode, "undocked") == 0 ? ptc_ui_text(PTC_UI_T_DOCK_HANDHELD) : ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM);
    const char *state = !fresh ? ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM) : model->dock_waived_today ?
        ptc_ui_text(PTC_UI_T_DOCK_CARD_WAIVED) : model->dock_restriction_active ?
        ptc_ui_text(PTC_UI_T_DOCK_BLOCKED) : !active ? ptc_ui_text(PTC_UI_T_DOCK_CARD_OFF) :
        model->dock_policy.force_docked ? ptc_ui_text(PTC_UI_T_DOCK_CARD_FORCE) : ptc_ui_text(PTC_UI_T_DOCK_LIMIT);
    if (fresh) snprintf(line, sizeof(line), "%s / %s", mode, state);
    else snprintf(line, sizeof(line), "%s", mode);
    char fitted[192];
    fit_text(fitted, sizeof(fitted), line, 13, card.width - 40);
    draw_text(pixels, stride, card.x + 20, card.y + 54, fitted, 13,
        fresh && model->dock_restriction_active ? UI_DANGER : UI_ACCENT);
    const PtcUiTextId labels[] = {PTC_UI_T_DOCK_USED_VALUE_NAMED, PTC_UI_T_DOCK_LEFT_VALUE_NAMED,
        PTC_UI_T_DOCK_LIMIT_VALUE_NAMED, PTC_UI_T_TOTAL_LEFT_VALUE_NAMED};
    for (int i = 0; i < 4; ++i) {
        snprintf(value, sizeof(value), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
        if (i == 0 && known)
            snprintf(value, sizeof(value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)model->undocked_used_minutes);
        else if (i == 1 && fresh && !active)
            snprintf(value, sizeof(value), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
        else if (i == 1 && fresh && model->dock_policy.force_docked)
            snprintf(value, sizeof(value), ptc_ui_text(PTC_UI_T_U_MIN), 0u);
        else if (i == 1 && known && active)
            snprintf(value, sizeof(value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)(model->undocked_remaining_minutes > 0 ? model->undocked_remaining_minutes : 0));
        else if (i == 2 && fresh) {
            if (!active) snprintf(value, sizeof(value), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
            else snprintf(value, sizeof(value), ptc_ui_text(PTC_UI_T_U_MIN), model->dock_policy.force_docked ? 0u : (unsigned)model->dock_policy.undocked_daily_minutes);
        } else if (i == 3 && status_fresh) {
            /* Estimate from the backend's current-day total, never from a temporary BLOCKED timer. */
            if (model->unrestricted_today == 1 || model->dock_unlimited_capped || model->eye_care_unlimited_capped)
                snprintf(value, sizeof(value), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
            else if (model->forecast_available && model->forecast[0].day_index == model->day_index &&
                     model->forecast[0].mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available && model->played_minutes >= 0) {
                int left = (int)model->forecast[0].minutes - model->played_minutes;
                snprintf(value, sizeof(value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)(left > 0 ? left : 0));
            }
        }
        PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("value", value)};
        (void)ptc_ui_text_format(labels[i], line, sizeof(line), args, 1);
        fit_text(fitted, sizeof(fitted), line, 14, card.width - 40);
        draw_text(pixels, stride, card.x + 20, card.y + 84 + i * step, fitted, 14, UI_INK);
    }
    if (!compact) {
        UiRect bar = {card.x + 20, card.y + 164, card.width - 40, 6};
        fill_round_rect(pixels, stride, bar, 3, UI_RAISED);
        if (known && model->dock_policy.undocked_limit_enabled && !model->dock_policy.force_docked && model->dock_policy.undocked_daily_minutes > 0) {
            int used = model->undocked_used_minutes;
            if (used < 0) used = 0;
            if (used > model->dock_policy.undocked_daily_minutes) used = model->dock_policy.undocked_daily_minutes;
            int width = bar.width * used / model->dock_policy.undocked_daily_minutes;
            if (width > 0) fill_round_rect(pixels, stride, (UiRect){bar.x, bar.y, width, bar.height}, 3, UI_ACCENT);
        }
    }
    draw_wrapped_text(pixels, stride, card.x + 20, card.y + (compact ? 151 : 188),
        ptc_ui_text(compact ? PTC_UI_T_DOCK_TOTAL_RELATION_SHORT : PTC_UI_T_DOCK_TOTAL_RELATION), 13, card.width - 40, 17,
        compact ? 1 : 2, UI_MUTED);
}

static void draw_dock_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const PtcDockPolicy *draft = &model->draft_dock_policy;
    int current_mode = (draft->force_docked) ? 1 : (draft->undocked_limit_enabled ? 2 : 0);

    /* --- 左侧配置卡：标题 + 意图 + 三个模式行 --- */
    UiRect config_card = {54, 176, 744, 420};
    draw_plan_card(pixels, stride, config_card, false);
    draw_text(pixels, stride, config_card.x + 22, config_card.y + 34,
              ptc_ui_text(PTC_UI_T_DOCK_INTENT_TITLE), 19, UI_INK);
    {
        const char *mode_pill = current_mode == 1 ? ptc_ui_text(PTC_UI_T_DOCK_CARD_FORCE) :
                                current_mode == 2 ? ptc_ui_text(PTC_UI_T_DOCK_LIMIT) : ptc_ui_text(PTC_UI_T_DOCK_CARD_OFF);
        int pill_w = measure_text(mode_pill, 12) + 24;
        UiRect pill = {config_card.x + config_card.width - pill_w - 22, config_card.y + 14, pill_w, 24};
        fill_round_rect(pixels, stride, pill, 6, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, pill, 6, 1, UI_ACCENT);
        draw_text_center(pixels, stride, pill, mode_pill, 12, UI_ACCENT);
    }
    draw_wrapped_text(pixels, stride, config_card.x + 22, config_card.y + 62,
                      ptc_ui_text(PTC_UI_T_DOCK_EYE_PROTECTION_INTENT), 13, config_card.width - 44, 18, 2, UI_MUTED);

    for (int i = 0; i < 3; ++i) {
        UiRect row = to_uirect(ptc_ui_dock_field_rect(i));
        bool focus = model->dock_field_focus == i && !model->parent_footer_focused;
        bool selected = (current_mode == i);
        bool lite_unsupported = (i == 1) && model->dock_supported_available && !model->dock_supported;
        bool row_disabled = model->disable_flag_present || (i == 1 && lite_unsupported);
        uint32_t title_color = row_disabled ? UI_DISABLED : (selected ? UI_ACCENT : UI_INK);

        if (focus) {
            fill_round_rect(pixels, stride, row, 10, UI_ACCENT_SOFT);
            draw_focus_ring(pixels, stride, row, 10);
        } else {
            fill_round_rect(pixels, stride, row, 10, selected ? UI_RGB(UI_BLENDED(surface_raised)) : UI_RGB(UI_BLENDED(surface)));
            draw_rect_outline(pixels, stride, row, 10, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
        }

        /* 单选指示器固定在行右端，第三行把分钟步进器排在它左侧。 */
        UiRect radio = {row.x + row.width - 42, row.y + (row.height - 24) / 2, 24, 24};
        fill_round_rect(pixels, stride, radio, 12, selected ? UI_ACCENT : UI_RAISED);
        draw_rect_outline(pixels, stride, radio, 12, 2, selected ? UI_ACCENT : UI_BORDER);
        if (selected) fill_round_rect(pixels, stride, (UiRect){radio.x + 6, radio.y + 6, 12, 12}, 6, UI_ON_ACCENT);

        /* 选项直观微缩图案卡 (Intuitive option pattern illustration tile) */
        UiRect icon_tile = {row.x + 12, row.y + (row.height - 60) / 2, 74, 60};
        fill_round_rect(pixels, stride, icon_tile, 8, selected ? UI_SURFACE : UI_PAGE);
        draw_rect_outline(pixels, stride, icon_tile, 8, 1, selected ? UI_ACCENT : UI_BORDER);

        if (i == 0) {
            /* Mode 0: 掌机与电视均可自由游玩 (Both Handheld and TV illuminated) */
            /* Mini Switch console */
            fill_round_rect(pixels, stride, (UiRect){icon_tile.x + 6, icon_tile.y + 20, 6, 20}, 2, UI_RGB(0x2E72D2));
            fill_rect_packed(pixels, stride, (UiRect){icon_tile.x + 13, icon_tile.y + 20, 13, 20}, pack_rgb(UI_BLENDED(page_bg)));
            fill_round_rect(pixels, stride, (UiRect){icon_tile.x + 27, icon_tile.y + 20, 6, 20}, 2, UI_RGB(0xE64A38));
            /* Plus symbol */
            draw_text(pixels, stride, icon_tile.x + 35, icon_tile.y + 35, "+", 12, UI_MUTED);
            /* Mini TV Screen */
            fill_round_rect(pixels, stride, (UiRect){icon_tile.x + 44, icon_tile.y + 18, 22, 18}, 3, UI_INK);
            fill_rect_packed(pixels, stride, (UiRect){icon_tile.x + 46, icon_tile.y + 20, 18, 14}, pack_rgb(0x5CCB8A));
            fill_rect_packed(pixels, stride, (UiRect){icon_tile.x + 53, icon_tile.y + 36, 4, 3}, pack_rgb(UI_BLENDED(text_primary)));
            fill_rect_packed(pixels, stride, (UiRect){icon_tile.x + 50, icon_tile.y + 39, 10, 2}, pack_rgb(UI_BLENDED(text_primary)));

            int text_x = row.x + 98;
            draw_text(pixels, stride, text_x, row.y + 30,
                ptc_ui_text(PTC_UI_T_DOCK_MODE_UNRESTRICTED), 18, title_color);
            draw_wrapped_text(pixels, stride, text_x, row.y + 54,
                ptc_ui_text(PTC_UI_T_DOCK_MODE_UNRESTRICTED_HINT), 12, row.width - 150, 16, 2, UI_MUTED);
        } else if (i == 1) {
            /* Mode 1: 仅允许电视模式 (TV dock prominent, handheld prohibited) */
            /* Prominent TV dock display */
            fill_round_rect(pixels, stride, (UiRect){icon_tile.x + 12, icon_tile.y + 12, 34, 24}, 3, UI_ACCENT);
            fill_rect_packed(pixels, stride, (UiRect){icon_tile.x + 14, icon_tile.y + 14, 30, 20}, pack_rgb(UI_BLENDED(focus)));
            draw_text_center(pixels, stride, (UiRect){icon_tile.x + 14, icon_tile.y + 16, 30, 16}, "TV", 11, UI_ON_ACCENT);
            /* Dock base */
            fill_round_rect(pixels, stride, (UiRect){icon_tile.x + 18, icon_tile.y + 36, 22, 6}, 2, UI_INK);
            /* Small prohibited handheld indicator */
            fill_round_rect(pixels, stride, (UiRect){icon_tile.x + 50, icon_tile.y + 24, 18, 14}, 2, UI_BORDER);
            fill_round_rect(pixels, stride, (UiRect){icon_tile.x + 48, icon_tile.y + 30, 22, 2}, 1, UI_DANGER);

            int text_x = row.x + 98;
            draw_text(pixels, stride, text_x, row.y + 30,
                ptc_ui_text(PTC_UI_T_DOCK_MODE_FORCE), 18, title_color);
            draw_wrapped_text(pixels, stride, text_x, row.y + 54,
                lite_unsupported ? ptc_ui_text(PTC_UI_T_DOCK_LITE) : ptc_ui_text(PTC_UI_T_DOCK_MODE_FORCE_HINT), 12,
                row.width - 150, 16, 2, row_disabled ? UI_DISABLED : UI_MUTED);
        } else {
            /* Mode 2: 限制非电视模式时长 (Timed handheld countdown leading to TV) */
            /* Handheld console with countdown badge */
            fill_round_rect(pixels, stride, (UiRect){icon_tile.x + 6, icon_tile.y + 18, 6, 24}, 2, UI_RGB(0x2E72D2));
            fill_rect_packed(pixels, stride, (UiRect){icon_tile.x + 13, icon_tile.y + 18, 18, 24}, pack_rgb(UI_BLENDED(page_bg)));
            fill_round_rect(pixels, stride, (UiRect){icon_tile.x + 32, icon_tile.y + 18, 6, 24}, 2, UI_RGB(0xE64A38));
            /* Timer circle in handheld screen */
            fill_round_rect(pixels, stride, (UiRect){icon_tile.x + 16, icon_tile.y + 23, 12, 12}, 6, UI_ACCENT);
            /* Transition arrow to TV */
            fill_rect_packed(pixels, stride, (UiRect){icon_tile.x + 40, icon_tile.y + 29, 8, 2}, pack_rgb(UI_BLENDED(accent)));
            fill_rect_packed(pixels, stride, (UiRect){icon_tile.x + 46, icon_tile.y + 28, 2, 4}, pack_rgb(UI_BLENDED(accent)));
            /* Mini TV dock target */
            fill_round_rect(pixels, stride, (UiRect){icon_tile.x + 52, icon_tile.y + 22, 16, 14}, 2, UI_INK);
            fill_rect_packed(pixels, stride, (UiRect){icon_tile.x + 54, icon_tile.y + 24, 12, 10}, pack_rgb(UI_BLENDED(accent)));

            int text_x = row.x + 98;
            draw_text(pixels, stride, text_x, row.y + 30,
                ptc_ui_text(PTC_UI_T_DOCK_MODE_LIMIT), 18, title_color);

            /* Minus / value / plus capsule stepper, replacing the old text arrows. */
            uint16_t mins = (draft->undocked_daily_minutes == 0) ? 30 : draft->undocked_daily_minutes;
            char value_text[32];
            /* Keep the stepper clear of the radio and vertically centered in
               the row: 8px gap on the right, 24px top/bottom. */
            UiRect dec = {row.x + row.width - 242, row.y + 24, 34, 34};
            UiRect val = {dec.x + 40, row.y + 24, 112, 34};
            UiRect inc = {val.x + val.width + 6, row.y + 24, 34, 34};
            snprintf(value_text, sizeof(value_text), "%u %s", mins, ptc_ui_text(PTC_UI_T_MINUTES));
            fill_round_rect(pixels, stride, dec, 10, selected ? UI_RAISED : UI_SURFACE);
            draw_rect_outline(pixels, stride, dec, 10, 1, selected ? UI_ACCENT : UI_BORDER);
            draw_text_center(pixels, stride, dec, "-", 20, selected ? UI_ACCENT : UI_MUTED);
            fill_round_rect(pixels, stride, val, 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
            draw_rect_outline(pixels, stride, val, 10, 1, selected ? UI_ACCENT : UI_BORDER);
            fit_text(value_text, sizeof(value_text), value_text, 16, val.width - 12);
            draw_text_center(pixels, stride, val, value_text, 16, selected ? UI_ACCENT : UI_MUTED);
            fill_round_rect(pixels, stride, inc, 10, selected ? UI_RAISED : UI_SURFACE);
            draw_rect_outline(pixels, stride, inc, 10, 1, selected ? UI_ACCENT : UI_BORDER);
            draw_text_center(pixels, stride, inc, "+", 20, selected ? UI_ACCENT : UI_MUTED);

            {
                char hint_buf[192];
                snprintf(hint_buf, sizeof(hint_buf), ptc_ui_text(PTC_UI_T_DOCK_MODE_LIMIT_HINT), mins);
                draw_wrapped_text(pixels, stride, text_x, row.y + 54, hint_buf, 12,
                                  row.width - 360, 16, 2, selected ? UI_ACCENT : UI_MUTED);
            }
        }
    }

    /* 左卡底部固定一行：保存后非电视额度的预计结果 */
    {
        char preview[192];
        ptc_ui_format_dock_preview(model, ptc_ui_render_now(), preview, sizeof(preview));
        draw_wrapped_text(pixels, stride, config_card.x + 22, config_card.y + config_card.height - 22,
                          preview, 13, config_card.width - 44, 17, 1, UI_ACCENT);
    }

    draw_dock_usage_card(pixels, stride, model, (UiRect){824, 176, 402, 220});

    /* --- 右侧规则卡：规则要点 + 与总额度的关系 + 规则详解入口 --- */
    {
        UiRect rules_panel = {824, 410, 402, 186};
        draw_plan_card(pixels, stride, rules_panel, false);
        draw_text(pixels, stride, rules_panel.x + 20, rules_panel.y + 32,
                  ptc_ui_text(PTC_UI_T_DOCK_RULE_TITLE), 16, UI_ACCENT);
        draw_wrapped_text(pixels, stride, rules_panel.x + 20, rules_panel.y + 62,
                          ptc_ui_text(PTC_UI_T_DOCK_RULE_EXPLANATION), 13,
                          rules_panel.width - 40, 17, 2, UI_INK);
        draw_wrapped_text(pixels, stride, rules_panel.x + 20, rules_panel.y + 106,
                          ptc_ui_text(PTC_UI_T_DETAIL_RELATION), 13,
                          rules_panel.width - 40, 17, 2, UI_MUTED);
        home_button(pixels, stride, ptc_ui_dock_field_rect(5), ptc_ui_text(PTC_UI_T_DOCK_RULES_EXPLAIN_BTN),
            false, model->dock_field_focus == 5 && !model->parent_footer_focused, false);
    }

#if defined(PLAYWISE_EDEN) || defined(PTC_UI_PREVIEW_ANIM_CLOCK_MS)
    if (model->eden_mode_controls) {
        home_button(pixels, stride, ptc_ui_dock_field_rect(7), ptc_ui_text(PTC_UI_T_EDEN_SIMULATE_TV),
            false, model->dock_field_focus == 7 && !model->parent_footer_focused, model->waiting);
        home_button(pixels, stride, ptc_ui_dock_field_rect(8), ptc_ui_text(PTC_UI_T_EDEN_SIMULATE_NON_TV),
            false, model->dock_field_focus == 8 && !model->parent_footer_focused, model->waiting);
    }
#endif
    /* --- 底栏动作：今日解除（次要） + 保存规则（主操作） --- */
    bool hold = ptc_ui_dock_save_requires_hold(model, ptc_ui_render_now());
    home_button(pixels, stride, ptc_ui_dock_field_rect(3),
        ptc_ui_text(model->dock_dirty ? PTC_UI_T_DOCK_SAVE : PTC_UI_T_RULE_SAVED),
        model->dock_dirty, (model->dock_field_focus == 3 && !model->parent_footer_focused) || hold,
        model->waiting || model->disable_flag_present || !model->dock_dirty);
    home_button(pixels, stride, ptc_ui_dock_field_rect(4), ptc_ui_text(PTC_UI_T_DOCK_WAIVE),
        false, model->dock_field_focus == 4 && !model->parent_footer_focused,
        model->waiting || model->dock_waived_today ||
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
