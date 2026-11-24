#include "ui_render_internal.h"
#include "../../common/update/update.h"

/* 24 小时可视化时间轴使用的家庭常用就寝窗口。 */
static const struct {
    PtcUiTextId name;
    uint16_t start;
    uint16_t end;
} PTC_BEDTIME_PRESETS[] = {
    {PTC_UI_T_EARLY_TO_BED_TYPE, 21 * 60, 7 * 60},
    {PTC_UI_T_STANDARD_TYPE, 21 * 60 + 30, 7 * 60},
    {PTC_UI_T_BALANCED_TYPE, 22 * 60, 7 * 60},
    {PTC_UI_T_WEEKEND_TYPE, 22 * 60 + 30, 7 * 60 + 30},
    {PTC_UI_T_LOOSE_TYPE, 23 * 60, 8 * 60}
};

static void draw_scheduled_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const PtcScheduledOverride *draft = &model->draft_scheduled_override;
    uint32_t duration = draft->end_day_index >= draft->start_day_index
        ? (uint32_t)draft->end_day_index - draft->start_day_index + 1u : 1u;
    uint16_t s_year = 0, e_year = 0;
    uint8_t s_month = 0, s_day = 0, e_month = 0, e_day = 0;
    bool s_ok = ptc_date_from_day_index(draft->start_day_index, &s_year, &s_month, &s_day);
    bool e_ok = ptc_date_from_day_index(draft->end_day_index, &e_year, &e_month, &e_day);
    bool dirty = ptc_ui_scheduled_dirty(model);
    bool disabled = model->disable_flag_present || model->waiting;
    bool limited = draft->rule.mode == PTC_RULE_MODE_LIMIT;
    bool save_danger = dirty &&
        ptc_ui_plan_save_requires_hold(model, PTC_UI_PLAN_SCHEDULED, ptc_ui_render_now());
    char path[128], buf[160], fitted[160];

    snprintf(path, sizeof(path), ptc_ui_text(PTC_UI_T_TIME_PLANS_S),
             ptc_ui_text(PTC_UI_T_SPECIFIED_DATE_QUOTA));
    draw_subpage_shell(pixels, stride, model,
                       ptc_ui_text(PTC_UI_T_SPECIFIED_DATE_QUOTA),
                       ptc_ui_text(PTC_UI_T_SCHEDULE_DAILY_LIMITS_FOR_A_DATE_RANGE),
                       ptc_ui_text(PTC_UI_T_B_BACK_TO_PLANS), path);
    {
        int64_t now = ptc_ui_render_now();
        draw_refresh_footer_button(pixels, stride, ptc_ui_scheduled_refresh_rect(),
            ptc_ui_text(PTC_UI_T_Y_REFRESH), ptc_ui_status_needs_refresh(model, now),
            ptc_ui_status_refresh_failed(model), false, model->waiting);
    }

    /* ================= Left Column: Step 1 Card (Active Dates) ================= */
    UiRect step1_card = {54, 174, 748, 250};
    draw_plan_card(pixels, stride, step1_card, false);

    /* Field 0: Master toggle row inside Step 1 */
    UiRect row0 = to_uirect(ptc_ui_scheduled_field_rect(0));
    bool f0 = model->overlay_selection == 0;
    fill_round_rect(pixels, stride, row0, 10, f0 ? UI_ACCENT_SOFT : UI_RAISED);
    if (f0) draw_focus_ring(pixels, stride, row0, 10);
    else draw_rect_outline(pixels, stride, row0, 10, 1, UI_BORDER);

    char status_badge[48];
    uint32_t badge_bg = UI_RAISED, badge_fg = UI_MUTED;
    if (!draft->enabled) {
        snprintf(status_badge, sizeof(status_badge), "%s", ptc_ui_text(PTC_UI_T_OFF_2));
    } else if (s_ok && e_ok && model->status_loaded) {
        if (model->day_index >= draft->start_day_index && model->day_index <= draft->end_day_index) {
            snprintf(status_badge, sizeof(status_badge), "%s", ptc_ui_text(PTC_UI_T_SCHEDULED_ACTIVE_TODAY));
            badge_bg = UI_SUCCESS_SOFT; badge_fg = UI_SUCCESS;
        } else if (model->day_index < draft->start_day_index) {
            snprintf(status_badge, sizeof(status_badge), ptc_ui_text(PTC_UI_T_SCHEDULED_IN_DAYS),
                     (unsigned)(draft->start_day_index - model->day_index));
            badge_bg = UI_ACCENT_SOFT; badge_fg = UI_ACCENT;
        } else {
            snprintf(status_badge, sizeof(status_badge), "%s", ptc_ui_text(PTC_UI_T_SCHEDULED_EXPIRED_SHORT));
            badge_bg = UI_WARNING_SOFT; badge_fg = UI_WARNING_TEXT;
        }
    } else {
        snprintf(status_badge, sizeof(status_badge), "%s", ptc_ui_text(PTC_UI_T_ON));
        badge_bg = UI_SUCCESS_SOFT; badge_fg = UI_SUCCESS;
    }

    UiRect toggle_rect = {row0.x + row0.width - 74, row0.y + (row0.height - 28) / 2, 58, 28};
    int badge_w = measure_text(status_badge, 12) + 20;
    if (badge_w > 180) badge_w = 180;
    UiRect badge_rect = {toggle_rect.x - badge_w - 14, row0.y + (row0.height - 24) / 2, badge_w, 24};
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SCHEDULED_STEP1_TITLE), 18,
             badge_rect.x - (row0.x + 16) - 12);
    draw_text_bold(pixels, stride, row0.x + 16, row0.y + 32, fitted, 18, UI_INK);
    fill_round_rect(pixels, stride, badge_rect, 12, badge_bg);
    fit_text(fitted, sizeof(fitted), status_badge, 12, badge_rect.width - 10);
    draw_text_center(pixels, stride, badge_rect, fitted, 12, badge_fg);
    draw_toggle_switch(pixels, stride, toggle_rect, draft->enabled, f0, disabled, NULL, NULL);

    /* Visual Date Range Bar */
    UiRect range_bar = {68, 246, 720, 44};
    fill_round_rect(pixels, stride, range_bar, 10, draft->enabled ? UI_ACCENT_SOFT : UI_PAGE);
    draw_rect_outline(pixels, stride, range_bar, 10, 1, draft->enabled ? UI_ACCENT : UI_BORDER);
    if (s_ok && e_ok) {
        char start_str[32], end_str[32], span_str[32];
        snprintf(start_str, sizeof(start_str), "%04u-%02u-%02u", s_year, s_month, s_day);
        snprintf(end_str, sizeof(end_str), "%04u-%02u-%02u", e_year, e_month, e_day);
        snprintf(span_str, sizeof(span_str), ptc_ui_text(PTC_UI_T_SCHEDULED_DAYS_TOTAL), (unsigned)duration);
        int start_w = measure_text(start_str, 16);
        int end_w = measure_text(end_str, 16);
        int span_w = measure_text(span_str, 13) + 24;
        if (span_w < 78) span_w = 78;
        if (span_w > 160) span_w = 160;
        draw_text_bold(pixels, stride, range_bar.x + 18, range_bar.y + 28, start_str, 16,
                       draft->enabled ? UI_INK : UI_MUTED);
        draw_text_bold(pixels, stride, range_bar.x + range_bar.width - 18 - end_w, range_bar.y + 28,
                       end_str, 16, draft->enabled ? UI_INK : UI_MUTED);
        int line_l = range_bar.x + 18 + start_w + 16;
        int line_r = range_bar.x + range_bar.width - 18 - end_w - 16;
        int mid_x = (line_l + line_r) / 2;
        int mid_y = range_bar.y + range_bar.height / 2;
        if (line_r > line_l + span_w + 24) {
            draw_line(pixels, stride, line_l, mid_y, mid_x - span_w / 2 - 8, mid_y, 2,
                      draft->enabled ? UI_ACCENT : UI_BORDER);
            draw_line(pixels, stride, mid_x + span_w / 2 + 8, mid_y, line_r, mid_y, 2,
                      draft->enabled ? UI_ACCENT : UI_BORDER);
        }
        UiRect span_pill = {mid_x - span_w / 2, range_bar.y + 9, span_w, 26};
        fill_round_rect(pixels, stride, span_pill, 13, draft->enabled ? UI_ACCENT : UI_RAISED);
        fit_text(fitted, sizeof(fitted), span_str, 13, span_pill.width - 10);
        draw_text_center(pixels, stride, span_pill, fitted, 13, draft->enabled ? UI_ON_ACCENT : UI_MUTED);
    } else {
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THE_PLANNING_INTERVAL_IS_BEING_CALCULATED_PLEASE),
                 14, range_bar.width - 28);
        draw_text_center(pixels, stride, range_bar, fitted, 14, UI_MUTED);
    }

    /* Field 1: Start Date sub-box */
    UiRect row1 = to_uirect(ptc_ui_scheduled_field_rect(1));
    bool f1 = model->overlay_selection == 1;
    fill_round_rect(pixels, stride, row1, 10, f1 ? UI_ACCENT_SOFT : UI_RAISED);
    if (f1) draw_focus_ring(pixels, stride, row1, 10);
    else draw_rect_outline(pixels, stride, row1, 10, 1, UI_BORDER);
    {
        const char *hint = "ZL/ZR ±7d";
        int hw = measure_text(hint, 11);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_START_DATE), 14, row1.width - hw - 36);
        draw_text(pixels, stride, row1.x + 14, row1.y + 24, fitted, 14, UI_MUTED);
        draw_text(pixels, stride, row1.x + row1.width - 14 - hw, row1.y + 23, hint, 11, UI_MUTED);
        if (s_ok) {
            snprintf(buf, sizeof(buf), "%04u-%02u-%02u%s", s_year, s_month, s_day,
                     model->status_loaded && draft->start_day_index == model->day_index ? ptc_ui_text(PTC_UI_T_TODAY_2) : "");
        } else {
            snprintf(buf, sizeof(buf), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        }
        fit_text(fitted, sizeof(fitted), buf, 20, row1.width - 28);
        draw_text_bold(pixels, stride, row1.x + 14, row1.y + 52, fitted, 20, UI_INK);
        const char *q_labels[3] = {"-1", ptc_ui_text(PTC_UI_T_SCHEDULED_CHANGE_DATE), "+1"};
        for (int q = 0; q < 3; ++q) {
            UiRect chip = to_uirect(ptc_ui_scheduled_quick_rect(q));
            fill_round_rect(pixels, stride, chip, 7, q == 1 ? UI_SURFACE : UI_PAGE);
            draw_rect_outline(pixels, stride, chip, 7, 1, q == 1 && f1 ? UI_ACCENT : UI_BORDER);
            fit_text(fitted, sizeof(fitted), q_labels[q], 12, chip.width - 8);
            draw_text_center(pixels, stride, chip, fitted, 12, disabled ? UI_DISABLED : (q == 1 ? UI_ACCENT : UI_INK));
        }
    }

    /* Field 2: Duration sub-box */
    UiRect row2 = to_uirect(ptc_ui_scheduled_field_rect(2));
    bool f2 = model->overlay_selection == 2;
    fill_round_rect(pixels, stride, row2, 10, f2 ? UI_ACCENT_SOFT : UI_RAISED);
    if (f2) draw_focus_ring(pixels, stride, row2, 10);
    else draw_rect_outline(pixels, stride, row2, 10, 1, UI_BORDER);
    {
        const char *hint = "1~366";
        int hw = measure_text(hint, 11);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NUMBER_OF_DAYS_TO_LAST), 14, row2.width - hw - 36);
        draw_text(pixels, stride, row2.x + 14, row2.y + 24, fitted, 14, UI_MUTED);
        draw_text(pixels, stride, row2.x + row2.width - 14 - hw, row2.y + 23, hint, 11, UI_MUTED);
        snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_U_DAYS), (unsigned)duration);
        fit_text(fitted, sizeof(fitted), buf, 20, row2.width - 28);
        draw_text_bold(pixels, stride, row2.x + 14, row2.y + 52, fitted, 20, UI_INK);
        static const uint32_t DAY_PRESETS[3] = {3, 7, 14};
        for (int q = 0; q < 4; ++q) {
            UiRect chip = to_uirect(ptc_ui_scheduled_quick_rect(3 + q));
            bool active = q < 3 ? (duration == DAY_PRESETS[q]) :
                          (duration != 3 && duration != 7 && duration != 14);
            if (q < 3) snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_U_DAYS), (unsigned)DAY_PRESETS[q]);
            else snprintf(buf, sizeof(buf), "%s", ptc_ui_text(PTC_UI_T_CUSTOM));
            fill_round_rect(pixels, stride, chip, 7, active ? UI_ACCENT : UI_PAGE);
            draw_rect_outline(pixels, stride, chip, 7, 1, active ? UI_ACCENT : UI_BORDER);
            fit_text(fitted, sizeof(fitted), buf, 12, chip.width - 8);
            draw_text_center(pixels, stride, chip, fitted, 12,
                             disabled ? UI_DISABLED : (active ? UI_ON_ACCENT : UI_INK));
        }
    }

    /* ================= Left Column: Step 2 Card (Daily Quota) ================= */
    UiRect step2_card = {54, 436, 748, 200};
    draw_plan_card(pixels, stride, step2_card, false);

    UiRect row3 = to_uirect(ptc_ui_scheduled_field_rect(3));
    bool f3 = model->overlay_selection == 3;
    fill_round_rect(pixels, stride, row3, 10, f3 ? UI_ACCENT_SOFT : UI_SURFACE);
    if (f3) draw_focus_ring(pixels, stride, row3, 10);
    else draw_rect_outline(pixels, stride, row3, 10, 1, UI_BORDER);

    /* Top row inside Step 2: Title + Mode Segmented Control */
    UiRect lim_btn = to_uirect(ptc_ui_scheduled_quick_rect(7));
    UiRect unlim_btn = to_uirect(ptc_ui_scheduled_quick_rect(8));
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SCHEDULED_STEP2_TITLE), 18,
             lim_btn.x - (row3.x + 14) - 12);
    draw_text_bold(pixels, stride, row3.x + 14, row3.y + 32, fitted, 18, UI_INK);

    fill_round_rect(pixels, stride, lim_btn, 8, limited ? UI_ACCENT : UI_RAISED);
    draw_rect_outline(pixels, stride, lim_btn, 8, 1, limited ? UI_ACCENT : UI_BORDER);
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_LIMIT), 13, lim_btn.width - 10);
    draw_text_center(pixels, stride, lim_btn, fitted, 13, disabled ? UI_DISABLED : (limited ? UI_ON_ACCENT : UI_INK));

    fill_round_rect(pixels, stride, unlim_btn, 8, !limited ? UI_SUCCESS : UI_RAISED);
    draw_rect_outline(pixels, stride, unlim_btn, 8, 1, !limited ? UI_SUCCESS : UI_BORDER);
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED), 13, unlim_btn.width - 10);
    draw_text_center(pixels, stride, unlim_btn, fitted, 13, disabled ? UI_DISABLED : (!limited ? UI_ON_ACCENT : UI_INK));

    /* Middle quota readout inside Step 2 (centered vertically around Y=536) */
    if (limited) {
        snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_U_MIN_U_HR_U_MIN),
                 (unsigned)draft->rule.minutes,
                 (unsigned)(draft->rule.minutes / 60),
                 (unsigned)(draft->rule.minutes % 60));
        fit_text(fitted, sizeof(fitted), buf, 24, row3.width - 28);
        draw_text_bold(pixels, stride, row3.x + 14, row3.y + 88, fitted, 24,
                       disabled ? UI_DISABLED : UI_ACCENT);
    } else {
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NO_LIMIT), 24, row3.width - 28);
        draw_text_bold(pixels, stride, row3.x + 14, row3.y + 84, fitted, 24,
                       disabled ? UI_DISABLED : UI_SUCCESS);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ALL_DAY_ACCESS), 13, row3.width - 28);
        draw_text(pixels, stride, row3.x + 14, row3.y + 106, fitted, 13, UI_MUTED);
    }

    /* Bottom quick quota preset chips (60 / 90 / 120 / 180 / Custom) */
    static const uint16_t QUOTA_PRESETS[4] = {60, 90, 120, 180};
    for (int q = 0; q < 5; ++q) {
        UiRect chip = to_uirect(ptc_ui_scheduled_quick_rect(9 + q));
        bool active = limited && (q < 4 ? (draft->rule.minutes == QUOTA_PRESETS[q]) :
                                  (draft->rule.minutes != 60 && draft->rule.minutes != 90 &&
                                   draft->rule.minutes != 120 && draft->rule.minutes != 180));
        if (q < 4) snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)QUOTA_PRESETS[q]);
        else snprintf(buf, sizeof(buf), "A %s", ptc_ui_text(PTC_UI_T_CUSTOM));
        fill_round_rect(pixels, stride, chip, 9, active ? UI_ACCENT : UI_RAISED);
        draw_rect_outline(pixels, stride, chip, 9, 1, active ? UI_ACCENT : UI_BORDER);
        fit_text(fitted, sizeof(fitted), buf, 14, chip.width - 10);
        draw_text_center(pixels, stride, chip, fitted, 14,
                         disabled ? UI_DISABLED : (active ? UI_ON_ACCENT : (limited ? UI_INK : UI_MUTED)));
    }

    /* ================= Right Column: Summary & Impact + Discard/Save ================= */
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_SCHEDULED, dirty,
                     (UiRect){820, 174, 406, 396});

    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay),
                          ptc_ui_text(PTC_UI_T_ZL_DISCARD), UI_PAGE, UI_INK, false,
                          !dirty || model->waiting);
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                          model->disable_flag_present ? ptc_ui_text(PTC_UI_T_READ_ONLY) :
                          (model->waiting ? ptc_ui_text(PTC_UI_T_SAVING_2) :
                           (dirty ? (save_danger ? ptc_ui_text(PTC_UI_T_SAVE_CONFIRM)
                                                 : ptc_ui_text(PTC_UI_T_SAVE_DRAFT_2))
                                  : ptc_ui_text(PTC_UI_T_SAVED))),
                          save_danger ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT, false,
                          disabled || !dirty);
}

static void draw_scheduled_leave(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel copy = *model;
    snprintf(copy.overlay_title, sizeof(copy.overlay_title), "%s", ptc_ui_text(PTC_UI_T_GIVE_UP_THE_DRAFT_QUOTA_FOR_A));
    snprintf(copy.overlay_body, sizeof(copy.overlay_body), "%s", ptc_ui_text(PTC_UI_T_UNSAVED_MODIFICATIONS_WILL_BE_LOST_AND_SAVED));
    draw_dialog_shell(pixels, stride, &copy, &dialog, 720, 300);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_CONTINUE_EDITING),
                       UI_RAISED, UI_ACCENT, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_ABANDON_DRAFT),
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_autonomy_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const uint16_t OPTIONS[4] = {0, 5, 10, 15};
    const char *SUBTITLES[4] = {
        ptc_ui_text(PTC_UI_T_DOES_NOT_OPEN_BUFFERING),
        ptc_ui_text(PTC_UI_T_QUICK_SAVE),
        ptc_ui_text(PTC_UI_T_CLOSING_RECOMMENDATIONS),
        ptc_ui_text(PTC_UI_T_ABUNDANT_EXIT)
    };
    const char *DESCS[4] = {
        ptc_ui_text(PTC_UI_T_AUTONOMY_DESC_OFF),
        ptc_ui_text(PTC_UI_T_AUTONOMY_DESC_5),
        ptc_ui_text(PTC_UI_T_AUTONOMY_DESC_10),
        ptc_ui_text(PTC_UI_T_AUTONOMY_DESC_15)
    };
    uint16_t saved_min = model->autonomy_policy.daily_buffer_minutes;
    uint16_t draft_min = model->draft_autonomy_policy.daily_buffer_minutes;
    bool dirty = (draft_min != saved_min);
    bool disabled = model->disable_flag_present || model->waiting;
    int64_t now = ptc_ui_render_now();
    bool fresh = ptc_ui_status_is_fresh(model, now);
    char path[128], buf[192], fitted[192];

    snprintf(path, sizeof(path), ptc_ui_text(PTC_UI_T_TIME_PLANS_S),
             ptc_ui_text(PTC_UI_T_AUTONOMY_BUFFER_2));
    draw_subpage_shell(pixels, stride, model,
                       ptc_ui_text(PTC_UI_T_AUTONOMY_BUFFER_2),
                       ptc_ui_text(PTC_UI_T_TODAY_S_INDEPENDENT_BUFFERING),
                       ptc_ui_text(PTC_UI_T_B_BACK_TO_PLANS), path);
    draw_refresh_footer_button(pixels, stride, ptc_ui_scheduled_refresh_rect(),
        ptc_ui_text(PTC_UI_T_Y_REFRESH), ptc_ui_status_needs_refresh(model, now),
        ptc_ui_status_refresh_failed(model), false, model->waiting);

    /* ================= Left Column: Intro Card + 2x2 Options + Boundary Pills ================= */
    UiRect intro_card = {54, 174, 748, 72};
    draw_plan_card(pixels, stride, intro_card, false);
    {
        char saved_lbl[64];
        if (saved_min > 0u) snprintf(saved_lbl, sizeof(saved_lbl), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)saved_min);
        else snprintf(saved_lbl, sizeof(saved_lbl), "%s", ptc_ui_text(PTC_UI_T_OFF));
        int sw = measure_text(saved_lbl, 12) + 22;
        if (sw > 140) sw = 140;
        UiRect s_pill = {intro_card.x + intro_card.width - 18 - sw, intro_card.y + 12, sw, 24};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_TODAY_S_INDEPENDENT_BUFFERING), 18,
                 s_pill.x - (intro_card.x + 18) - 10);
        draw_text_bold(pixels, stride, intro_card.x + 18, intro_card.y + 30, fitted, 18, UI_INK);
        fill_round_rect(pixels, stride, s_pill, 6, saved_min > 0u ? UI_SUCCESS_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, s_pill, 6, 1, saved_min > 0u ? UI_SUCCESS_BORDER : UI_BORDER);
        fit_text(fitted, sizeof(fitted), saved_lbl, 12, s_pill.width - 10);
        draw_text_center(pixels, stride, s_pill, fitted, 12, saved_min > 0u ? UI_SUCCESS : UI_MUTED);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_PURPOSE_WHEN_THE_QUOTA_IS_EXHAUSTED_CHILDREN),
                 13, intro_card.width - 36);
        draw_text(pixels, stride, intro_card.x + 18, intro_card.y + 56, fitted, 13, UI_MUTED);
    }

    for (int index = 0; index < 4; ++index) {
        UiRect option = to_uirect(ptc_ui_autonomy_option_rect(index));
        bool selected = (draft_min == OPTIONS[index]);
        bool is_saved = (saved_min == OPTIONS[index]);
        bool focused = (model->overlay_selection == index);
        bool is_off = (OPTIONS[index] == 0u);
        uint32_t bg = selected ? (is_off ? UI_ACCENT_SOFT : UI_SUCCESS_SOFT) : UI_RGB(UI_BLENDED(surface));
        uint32_t bd = selected ? (is_off ? UI_ACCENT : UI_SUCCESS) : UI_BORDER;
        uint32_t fg = selected ? (is_off ? UI_ACCENT : UI_SUCCESS) : UI_INK;
        if (!focused) draw_card_shadow(pixels, stride, option, 14);
        fill_round_rect(pixels, stride, option, 14, bg);
        if (focused) draw_focus_ring(pixels, stride, option, 14);
        else draw_rect_outline(pixels, stride, option, 14, selected ? 2 : 1, bd);

        if (OPTIONS[index] > 0u) snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)OPTIONS[index]);
        else snprintf(buf, sizeof(buf), "%s", ptc_ui_text(PTC_UI_T_OFF));

        /* Right badges: Active & Selected (measured dynamically from right edge) */
        int right_edge = option.x + option.width - 14;
        if (selected) {
            const char *st = ptc_ui_text(PTC_UI_T_AUTONOMY_SELECTED_BADGE);
            int bw = measure_text(st, 11) + 16;
            UiRect sb = {right_edge - bw, option.y + 14, bw, 22};
            fill_round_rect(pixels, stride, sb, 6, is_off ? UI_ACCENT : UI_SUCCESS);
            draw_text_center(pixels, stride, sb, st, 11, UI_ON_ACCENT);
            right_edge = sb.x - 6;
        }
        if (is_saved) {
            const char *at = ptc_ui_text(PTC_UI_T_AUTONOMY_ACTIVE_BADGE);
            int bw = measure_text(at, 11) + 16;
            UiRect ab = {right_edge - bw, option.y + 14, bw, 22};
            fill_round_rect(pixels, stride, ab, 6, UI_RAISED);
            draw_rect_outline(pixels, stride, ab, 6, 1, UI_BORDER);
            draw_text_center(pixels, stride, ab, at, 11, UI_MUTED);
            right_edge = ab.x - 8;
        }

        fit_text(fitted, sizeof(fitted), buf, 24, right_edge - (option.x + 16));
        draw_text_bold(pixels, stride, option.x + 16, option.y + 40, fitted, 24, fg);
        fit_text(fitted, sizeof(fitted), SUBTITLES[index], 14, option.width - 32);
        draw_text_bold(pixels, stride, option.x + 16, option.y + 72, fitted, 14,
                       selected ? fg : UI_INK);
        draw_wrapped_text(pixels, stride, option.x + 16, option.y + 98, DESCS[index],
                          12, option.width - 32, 18, 2, UI_MUTED);
    }

    /* 3 compact boundary boxes along bottom-left */
    {
        static const PtcUiTextId BOUNDS[3] = {
            PTC_UI_T_AUTONOMY_BOUNDARY_1,
            PTC_UI_T_AUTONOMY_BOUNDARY_2,
            PTC_UI_T_AUTONOMY_BOUNDARY_3
        };
        int bw = (748 - 16) / 3;
        for (int b = 0; b < 3; ++b) {
            UiRect box = {54 + b * (bw + 8), 582, bw, 54};
            draw_plan_card(pixels, stride, box, false);
            fit_text(fitted, sizeof(fitted), ptc_ui_text(BOUNDS[b]), 12, box.width - 16);
            draw_text_center(pixels, stride, box, fitted, 12, UI_MUTED);
        }
    }

    /* ================= Right Column: Today Claim Status, Impact & Child Preview ================= */
    UiRect right_card = {818, 174, 408, 396};
    draw_plan_card(pixels, stride, right_card, false);
    {
        const char *st_badge;
        const char *st_head;
        const char *st_sub;
        uint32_t st_bg, st_bd, st_fg;
        if (!fresh) {
            st_badge = ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM);
            st_head = ptc_ui_text(PTC_UI_T_AUTONOMY_STATUS_PENDING);
            st_sub = ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED_WILL_RECHECK_IMPACT_UPON_SAVING);
            st_bg = UI_WARNING_SOFT; st_bd = UI_WARNING_BORDER; st_fg = UI_WARNING_TEXT;
        } else if (saved_min == 0u) {
            st_badge = ptc_ui_text(PTC_UI_T_OFF_2);
            st_head = ptc_ui_text(PTC_UI_T_AUTONOMY_STATUS_OFF);
            st_sub = ptc_ui_text(PTC_UI_T_RECOMMENDED_SETTINGS_5_TO_15_MINUTES_ARE);
            st_bg = UI_RAISED; st_bd = UI_BORDER; st_fg = UI_MUTED;
        } else if (model->daily_buffer_claimed) {
            st_badge = ptc_ui_text(PTC_UI_T_CLAIMED);
            st_head = ptc_ui_text(PTC_UI_T_AUTONOMY_CLAIMED_NEXT_DAY);
            st_sub = ptc_ui_text(PTC_UI_T_THE_BUFFER_ONLY_INCREASES_TODAY_S_QUOTA);
            st_bg = UI_ACCENT_SOFT; st_bd = UI_ACCENT; st_fg = UI_ACCENT;
        } else if (model->unrestricted_today == 1) {
            st_badge = ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED);
            st_head = ptc_ui_text(PTC_UI_T_AUTONOMY_STATUS_UNAVAILABLE);
            st_sub = ptc_ui_text(PTC_UI_T_CAN_ONLY_BE_COLLECTED_ONCE_A_DAY);
            st_bg = UI_SUCCESS_SOFT; st_bd = UI_SUCCESS_BORDER; st_fg = UI_SUCCESS;
        } else {
            st_badge = ptc_ui_text(PTC_UI_T_AVAILABLE);
            st_head = ptc_ui_text(PTC_UI_T_CAN_ONLY_BE_COLLECTED_ONCE_A_DAY);
            st_sub = ptc_ui_text(PTC_UI_T_THE_BUFFER_ONLY_INCREASES_TODAY_S_QUOTA);
            st_bg = UI_SUCCESS_SOFT; st_bd = UI_SUCCESS_BORDER; st_fg = UI_SUCCESS;
        }
        int bw = measure_text(st_badge, 12) + 18;
        if (bw > 140) bw = 140;
        UiRect bp = {right_card.x + right_card.width - 16 - bw, right_card.y + 14, bw, 24};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_AUTONOMY_TODAY_STATUS), 17,
                 bp.x - (right_card.x + 16) - 10);
        draw_text_bold(pixels, stride, right_card.x + 16, right_card.y + 32, fitted, 17, UI_INK);
        fill_round_rect(pixels, stride, bp, 6, st_bg);
        draw_rect_outline(pixels, stride, bp, 6, 1, st_bd);
        fit_text(fitted, sizeof(fitted), st_badge, 12, bp.width - 8);
        draw_text_center(pixels, stride, bp, fitted, 12, st_fg);

        UiRect status_strip = {right_card.x + 16, right_card.y + 48, right_card.width - 32, 82};
        fill_round_rect(pixels, stride, status_strip, 9, st_bg);
        draw_rect_outline(pixels, stride, status_strip, 9, 1, st_bd);
        fit_text(fitted, sizeof(fitted), st_head, 14, status_strip.width - 20);
        draw_text_bold(pixels, stride, status_strip.x + 10, status_strip.y + 26, fitted, 14, st_fg);
        draw_wrapped_text(pixels, stride, status_strip.x + 10, status_strip.y + 48, st_sub,
                          12, status_strip.width - 20, 16, 2, UI_MUTED);
    }

    /* Before -> After comparison box */
    {
        UiRect cmp_box = {right_card.x + 16, right_card.y + 142, right_card.width - 32, 76};
        fill_round_rect(pixels, stride, cmp_box, 10, UI_RAISED);
        draw_rect_outline(pixels, stride, cmp_box, 10, 1, UI_BORDER);
        int half_w = (cmp_box.width - 34) / 2;
        UiRect lh = {cmp_box.x + 6, cmp_box.y + 6, half_w, cmp_box.height - 12};
        UiRect rh = {cmp_box.x + cmp_box.width - 6 - half_w, cmp_box.y + 6, half_w, cmp_box.height - 12};
        char cur_s[48], next_s[48];
        if (saved_min > 0u) snprintf(cur_s, sizeof(cur_s), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)saved_min);
        else snprintf(cur_s, sizeof(cur_s), "%s", ptc_ui_text(PTC_UI_T_OFF));
        if (draft_min > 0u) snprintf(next_s, sizeof(next_s), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)draft_min);
        else snprintf(next_s, sizeof(next_s), "%s", ptc_ui_text(PTC_UI_T_OFF));
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_CURRENT_STATE), 11, lh.width - 6);
        draw_text_center(pixels, stride, (UiRect){lh.x, lh.y + 4, lh.width, 18}, fitted, 11, UI_MUTED);
        fit_text(fitted, sizeof(fitted), cur_s, 18, lh.width - 6);
        draw_text_center(pixels, stride, (UiRect){lh.x, lh.y + 26, lh.width, 30}, fitted, 18, UI_INK);
        draw_transition_arrow(pixels, stride, cmp_box.x + cmp_box.width / 2, cmp_box.y + cmp_box.height / 2, UI_MUTED);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_FORECAST_AFTER_SAVE), 11, rh.width - 6);
        draw_text_center(pixels, stride, (UiRect){rh.x, rh.y + 4, rh.width, 18}, fitted, 11, UI_MUTED);
        fit_text(fitted, sizeof(fitted), next_s, 18, rh.width - 6);
        draw_text_center(pixels, stride, (UiRect){rh.x, rh.y + 26, rh.width, 30}, fitted, 18,
                         dirty ? UI_SUCCESS : UI_MUTED);
    }

    /* Child Entry Preview Card */
    {
        UiRect preview = {right_card.x + 16, right_card.y + 230, right_card.width - 32, 150};
        fill_round_rect(pixels, stride, preview, 10, UI_RAISED);
        draw_rect_outline(pixels, stride, preview, 10, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_AUTONOMY_CHILD_PREVIEW), 14, preview.width - 24);
        draw_text_bold(pixels, stride, preview.x + 12, preview.y + 26, fitted, 14, UI_INK);

        UiRect mock_btn = {preview.x + 12, preview.y + 42, preview.width - 24, 46};
        uint32_t mb_bg = UI_SURFACE, mb_bd = UI_BORDER, mb_fg = UI_MUTED;
        if (draft_min == 0u) {
            snprintf(buf, sizeof(buf), "%s", ptc_ui_text(PTC_UI_T_DOES_NOT_OPEN_BUFFERING));
        } else if (!fresh) {
            snprintf(buf, sizeof(buf), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
            mb_bg = UI_WARNING_SOFT; mb_bd = UI_WARNING_BORDER; mb_fg = UI_WARNING_TEXT;
        } else if (model->daily_buffer_claimed) {
            snprintf(buf, sizeof(buf), "%s", ptc_ui_text(PTC_UI_T_RECEIVED_TODAY));
        } else if (model->unrestricted_today == 1) {
            snprintf(buf, sizeof(buf), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
        } else {
            snprintf(buf, sizeof(buf), "%s (+%u %s)",
                     ptc_ui_text(PTC_UI_T_AUTONOMY_BUFFER_2), (unsigned)draft_min, ptc_ui_text(PTC_UI_T_MIN));
            mb_bg = UI_SUCCESS_SOFT; mb_bd = UI_SUCCESS_BORDER; mb_fg = UI_SUCCESS;
        }
        fill_round_rect(pixels, stride, mock_btn, 9, mb_bg);
        draw_rect_outline(pixels, stride, mock_btn, 9, 1, mb_bd);
        fit_text(fitted, sizeof(fitted), buf, 14, mock_btn.width - 20);
        draw_text_center(pixels, stride, mock_btn, fitted, 14, mb_fg);

        draw_wrapped_text(pixels, stride, preview.x + 12, preview.y + 110,
                          ptc_ui_text(PTC_UI_T_BEDTIME_TIME_REMAINS_AS_SCHEDULED),
                          12, preview.width - 24, 17, 2, UI_WARNING_TEXT);
    }

    /* Discard & Save buttons */
    {
        UiRect discard_btn = to_uirect(ptc_ui_discard_rect(model->overlay));
        UiRect save_btn = to_uirect(ptc_ui_confirm_rect(model->overlay));
        bool save_active = dirty && !disabled;
        fill_round_rect(pixels, stride, discard_btn, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, discard_btn, 12, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ZL_DISCARD), 15, discard_btn.width - 16);
        draw_text_center(pixels, stride, discard_btn, fitted, 15, save_active ? UI_INK : UI_DISABLED);

        if (save_active) draw_card_shadow(pixels, stride, save_btn, 12);
        fill_round_rect(pixels, stride, save_btn, 12, save_active ? UI_ACCENT : UI_RAISED);
        draw_rect_outline(pixels, stride, save_btn, 12, 1, save_active ? UI_ACCENT : UI_BORDER);
        fit_text(fitted, sizeof(fitted),
                 dirty ? ptc_ui_text(PTC_UI_T_SAVE_BUFFER_SETTINGS) : ptc_ui_text(PTC_UI_T_RULE_SAVED),
                 14, save_btn.width - 16);
        draw_text_center(pixels, stride, save_btn, fitted, 14, save_active ? UI_ON_ACCENT : UI_DISABLED);
    }
}

static void draw_eye_care_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char value[64];
    int row;
    PtcUiModel shell_model = *model;
    shell_model.overlay_body[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 880, 480);
    for (row = 0; row < 3; ++row) {
        UiRect rect = {dialog.x + 48, dialog.y + 120 + row * 74, dialog.width - 96, 64};
        bool focused = model->overlay_selection == row;
        fill_round_rect(pixels, stride, rect, 10, focused ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, rect, 10, focused ? 2 : 1, focused ? UI_ACCENT : UI_BORDER);
        draw_text(pixels, stride, rect.x + 16, rect.y + 38,
            row == 0 ? ptc_ui_text(PTC_UI_T_EYE_CARE) :
            (row == 1 ? ptc_ui_text(PTC_UI_T_EYE_CARE_PLAY) : ptc_ui_text(PTC_UI_T_EYE_CARE_REST)),
            18, UI_INK);
        if (row == 0) snprintf(value, sizeof(value), "%s",
            model->draft_eye_care_policy.enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF));
        else snprintf(value, sizeof(value), "%u %s",
            (unsigned)(row == 1 ? model->draft_eye_care_policy.play_minutes :
                model->draft_eye_care_policy.rest_minutes), ptc_ui_text(PTC_UI_T_MIN));
        if (row == 0) draw_text(pixels, stride, rect.x + 480, rect.y + 38, value, 18, UI_ACCENT);
        else {
            static const char *steps[] = {"-10", "-1", "+1", "+10"};
            int first = row == 1 ? 1 : 5;
            draw_text_center(pixels, stride,
                (UiRect){rect.x + 464, rect.y + 14, 96, 36}, value, 18, UI_ACCENT);
            for (int step = 0; step < 4; ++step)
                draw_candidate_button(pixels, stride,
                    ptc_ui_eye_care_field_rect(first + step), steps[step],
                    UI_PAGE, UI_INK, focused, false);
        }
    }
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 370,
        ptc_ui_text(PTC_UI_T_EYE_CARE_GUIDE), 14, UI_MUTED);
    if (model->draft_eye_care_policy.enabled && !model->bedtime_official_setting_confirmed)
        draw_text(pixels, stride, dialog.x + 48, dialog.y + 397,
            ptc_ui_text(PTC_UI_T_EYE_CARE_ENABLE_CONFIRM), 12, UI_WARNING);
    draw_overlay_actions(pixels, stride, model, ptc_ui_text(PTC_UI_T_EYE_CARE_SAVE));
}

static void draw_quick_add_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const char *LABELS[] = {ptc_ui_text(PTC_UI_T_15_MINUTES), ptc_ui_text(PTC_UI_T_30_MINUTES), ptc_ui_text(PTC_UI_T_60_MINUTES), ptc_ui_text(PTC_UI_T_CUSTOM)};
    draw_dialog_shell(pixels, stride, model, &dialog, 760, 420);
    for (int index = 0; index < 4; ++index) {
        UiRect option = to_uirect(ptc_ui_quick_add_option_rect(index));
        bool selected = model->overlay_selection == index;
        fill_round_rect(pixels, stride, option, 12, selected ? UI_SUCCESS_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 12, selected ? 2 : 1,
            selected ? UI_SUCCESS : UI_BORDER);
        draw_text_center(pixels, stride, option, LABELS[index], 20,
            selected ? UI_SUCCESS : UI_INK);
    }
    unsigned minutes = ptc_ui_quick_add_option_minutes(model->overlay_selection);
    int remaining = minutes && !model->waiting && !model->disable_flag_present &&
        !model->recovery_active && ptc_ui_status_is_fresh(model, ptc_ui_render_now())
        ? ptc_ui_preview_added_remaining_minutes(model, (uint16_t)minutes) : -1;
    char preview[160];
    if (remaining >= 0)
        snprintf(preview, sizeof(preview), ptc_ui_text(PTC_UI_T_EST_REMAINING_D_MIN), remaining);
    else
        snprintf(preview, sizeof(preview), "%s", ptc_ui_text(minutes ?
            PTC_UI_T_REMAINING_PENDING_REFRESH : PTC_UI_T_ALSO_DISPLAYS_A_CONFIRMATION_PAGE_AFTER_SELECTION));
    draw_button_label(pixels, stride, (UiRect){dialog.x + 48, dialog.y + 266, dialog.width - 96, 34},
        preview, 16, remaining >= 0 ? UI_ACCENT : UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_CANCEL),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_CONTINUE),
        UI_SUCCESS, UI_ON_ACCENT, false);
}

static void draw_bedtime_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    char values[4][96];
    draw_dialog_shell(pixels, stride, model, &dialog, 860, 540);
    snprintf(values[0], sizeof(values[0]), ptc_ui_text(PTC_UI_T_BEDTIME_PLAN_S), draft->enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF));
    snprintf(values[1], sizeof(values[1]), ptc_ui_text(PTC_UI_T_STARTS_DAILY_02U_02U),
        (unsigned int)(draft->week[0].start_minute / 60),
        (unsigned int)(draft->week[0].start_minute % 60));
    snprintf(values[2], sizeof(values[2]), ptc_ui_text(PTC_UI_T_ENDS_NEXT_DAY_02U_02U),
        (unsigned int)(draft->week[0].end_minute / 60),
        (unsigned int)(draft->week[0].end_minute % 60));
    snprintf(values[3], sizeof(values[3]), "%s",
        model->bedtime_active && !model->bedtime_skipped
            ? ptc_ui_text(PTC_UI_T_IN_EFFECT_CAN_ONLY_BE_RESTORED_FROM)
            : ptc_ui_text(PTC_UI_T_AFTER_THE_RESTRICTION_TAKES_EFFECT_PLAYWISE_NRO));
    for (int index = 0; index < 4; ++index) {
        UiRect row = {dialog.x + 42, dialog.y + 118 + index * 70, dialog.width - 84, 56};
        draw_plan_card(pixels, stride, row, model->overlay_selection == index);
        draw_text(pixels, stride, row.x + 18, row.y + 35, values[index], 20,
            index == 3 && model->bedtime_active && !model->bedtime_skipped ? UI_WARNING : UI_INK);
    }
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 402,
        ptc_ui_text(PTC_UI_T_DIRECTION_KEY_SELECTION_A_OPENS_THE_HOUR), 16, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 434,
        model->bedtime_official_setting_confirmed
            ? (model->bedtime_overlay_verified ? ptc_ui_text(PTC_UI_T_ENVIRONMENT_CONFIRMED_OVERLAY_VERIFIED) : ptc_ui_text(PTC_UI_T_THE_ENVIRONMENT_HAS_BEEN_CONFIRMED_BY_CONTINUING))
            : ptc_ui_text(PTC_UI_T_BEFORE_ENABLING_CONFIRM_SUSPEND_SOFTWARE_IN_NINTENDO),
        14, model->bedtime_official_setting_confirmed ? UI_MUTED : UI_WARNING);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_SAVE_AND_TAKE_EFFECT),
        UI_WARNING, UI_ON_ACCENT, false);
}

void draw_bedtime_timeline_strip(
    uint32_t *pixels, uint32_t stride,
    UiRect bar, uint16_t start_m, uint16_t end_m, bool enabled)
{
    fill_round_rect(pixels, stride, bar, 6, UI_PAGE);
    draw_rect_outline(pixels, stride, bar, 6, 1, UI_BORDER);

    if (!enabled) {
        char fitted[128];
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_TODAY_S_BEDTIME_RESTRICTION_IS_NOT_ENABLED),
                 13, bar.width - 16);
        draw_text_center(pixels, stride, bar, fitted, 13, UI_MUTED);
    } else {
        int x_morn_end = bar.x + (int)((float)end_m / 1440.0f * (float)bar.width + 0.5f);
        int x_eve_start = bar.x + (int)((float)start_m / 1440.0f * (float)bar.width + 0.5f);
        if (x_morn_end < bar.x) x_morn_end = bar.x;
        if (x_morn_end > bar.x + bar.width) x_morn_end = bar.x + bar.width;
        if (x_eve_start < bar.x) x_eve_start = bar.x;
        if (x_eve_start > bar.x + bar.width) x_eve_start = bar.x + bar.width;

        uint32_t bed_block_bg = UI_NIGHT_SOFT;
        uint32_t bed_block_fg = UI_NIGHT;

        /* 清晨就寝区间 [bar.x, x_morn_end] */
        if (x_morn_end > bar.x) {
            UiRect morn_rect = {bar.x, bar.y, x_morn_end - bar.x, bar.height};
            fill_round_rect(pixels, stride, morn_rect, 6, bed_block_bg);
            if (morn_rect.width > 58 && bar.height >= 24) {
                char fitted[64];
                fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_GO_TO_BED_EARLY_IN_THE_MORNING),
                         11, morn_rect.width - 8);
                draw_text_center(pixels, stride, morn_rect, fitted, 11, bed_block_fg);
            }
        }

        /* 晚间就寝区间 [x_eve_start, bar.x + bar.width] */
        if (x_eve_start < bar.x + bar.width) {
            UiRect eve_rect = {x_eve_start, bar.y, bar.x + bar.width - x_eve_start, bar.height};
            fill_round_rect(pixels, stride, eve_rect, 6, bed_block_bg);
            if (eve_rect.width > 58 && bar.height >= 24) {
                char fitted[64];
                fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NIGHTTIME_BEDTIME),
                         11, eve_rect.width - 8);
                draw_text_center(pixels, stride, eve_rect, fitted, 11, bed_block_fg);
            }
        }

        /* 白昼允许游玩区间 [x_morn_end, x_eve_start] */
        if (x_eve_start > x_morn_end && (x_eve_start - x_morn_end) > 72 && bar.height >= 24) {
            UiRect day_rect = {x_morn_end, bar.y, x_eve_start - x_morn_end, bar.height};
            char fitted[64];
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ALLOWED_PLAY_TIME_DURING_THE_DAY),
                     12, day_rect.width - 12);
            draw_text_center(pixels, stride, day_rect, fitted, 12, UI_RGB(UI_BLENDED(text_primary)));
        }

        /* 立断与解禁指示线 */
        if (x_eve_start >= bar.x && x_eve_start <= bar.x + bar.width) {
            fill_rect(pixels, stride, (UiRect){x_eve_start - 1, bar.y, 2, bar.height}, UI_NIGHT);
            fill_round_rect(pixels, stride, (UiRect){x_eve_start - 3, bar.y, 6, 4}, 1, UI_NIGHT);
        }
        if (x_morn_end >= bar.x && x_morn_end <= bar.x + bar.width) {
            fill_rect(pixels, stride, (UiRect){x_morn_end - 1, bar.y, 2, bar.height}, UI_SUCCESS);
            fill_round_rect(pixels, stride, (UiRect){x_morn_end - 3, bar.y, 6, 4}, 1, UI_SUCCESS);
        }
    }
}

int bedtime_matching_preset_window(const PtcBedtimeWindow *window)
{
    if (!window || !window->enabled) return -1;
    for (int preset = 0; preset < 5; ++preset) {
        if (window->start_minute == PTC_BEDTIME_PRESETS[preset].start &&
            window->end_minute == PTC_BEDTIME_PRESETS[preset].end)
            return preset;
    }
    return -1;
}

void draw_bedtime_preset_chip_row(
    uint32_t *pixels, uint32_t stride,
    const PtcBedtimeWindow *window, bool custom,
    const PtcUiRect *rects, int focused_index)
{
    int matched = custom ? bedtime_matching_preset_window(window) : -1;
    for (int preset = 0; preset < 5; ++preset) {
        UiRect chip = to_uirect(rects[preset]);
        bool selected = custom && matched == preset;
        bool focused = (focused_index == preset);
        char times[32], fitted[48];
        fill_round_rect(pixels, stride, chip, 9,
            selected ? UI_ACCENT_SOFT : (custom ? UI_RAISED : UI_PAGE));
        if (focused) draw_focus_ring(pixels, stride, chip, 9);
        else draw_rect_outline(pixels, stride, chip, 9, selected ? 2 : 1,
                               selected ? UI_ACCENT : UI_BORDER);
        int top_h = chip.height >= 54 ? 26 : 18;
        int bot_y = chip.height >= 54 ? chip.y + 30 : chip.y + 21;
        int bot_h = chip.height >= 54 ? 22 : 18;
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_BEDTIME_PRESETS[preset].name),
                 chip.height >= 54 ? 13 : 12, chip.width - 10);
        draw_text_center(pixels, stride, (UiRect){chip.x, chip.y + (chip.height >= 54 ? 6 : 3), chip.width, top_h},
            fitted, chip.height >= 54 ? 13 : 12,
            custom ? (selected ? UI_ACCENT : UI_INK) : UI_DISABLED);
        snprintf(times, sizeof(times), "%02u:%02u-%02u:%02u",
            PTC_BEDTIME_PRESETS[preset].start / 60, PTC_BEDTIME_PRESETS[preset].start % 60,
            PTC_BEDTIME_PRESETS[preset].end / 60, PTC_BEDTIME_PRESETS[preset].end % 60);
        fit_text(fitted, sizeof(fitted), times, chip.height >= 54 ? 11 : 10, chip.width - 8);
        draw_text_center(pixels, stride, (UiRect){chip.x, bot_y, chip.width, bot_h},
            fitted, chip.height >= 54 ? 11 : 10, custom ? UI_MUTED : UI_DISABLED);
    }
}

static int bedtime_matching_preset(const PtcBedtimeWindow *window)
{
    if (!window) return -1;
    for (int preset = 0; preset < 5; ++preset) {
        if (window->start_minute == PTC_BEDTIME_PRESETS[preset].start &&
            window->end_minute == PTC_BEDTIME_PRESETS[preset].end)
            return preset;
    }
    return -1;
}

static void draw_bedtime_editor_fields(
    uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
    const PtcBedtimeWindow *window, const char *mode_label, uint32_t mode_color,
    bool custom, bool show_toggle)
{
    UiRect mode = to_uirect(ptc_ui_bedtime_overlay_field_rect(model->overlay, 0));
    draw_plan_card(pixels, stride, mode, model->overlay_selection == 0);
    const char *title = show_toggle ? ptc_ui_text(PTC_UI_T_TODAY_S_BEDTIME_LIMIT) : ptc_ui_text(PTC_UI_T_RULE_PATTERN);
    const char *hint = show_toggle ? ptc_ui_text(PTC_UI_T_A_X_SWITCH) : ptc_ui_text(PTC_UI_T_A_X_SWITCH_MODE);
    int right = mode.x + mode.width - 18;
    if (show_toggle) right -= 76;
    int hint_x = right - measure_text(hint, 13);
    int value_x = hint_x - 20 - measure_text(mode_label, 18);
    char fitted_title[192];
    fit_text(fitted_title, sizeof(fitted_title), title, 18, value_x - (mode.x + 18) - 20);
    draw_text(pixels, stride, mode.x + 18, mode.y + 36, fitted_title, 18, UI_INK);
    draw_text(pixels, stride, value_x, mode.y + 36, mode_label, 18, mode_color);
    draw_text(pixels, stride, hint_x, mode.y + 36, hint, 13, UI_MUTED);
    if (show_toggle) {
        UiRect toggle = {mode.x + mode.width - 76, mode.y + 15, 58, 28};
        draw_toggle_switch(pixels, stride, toggle, window->enabled,
            model->overlay_selection == 0, false, NULL, NULL);
    }

    for (int field = 1; field < 3; ++field) {
        UiRect row = to_uirect(ptc_ui_bedtime_overlay_field_rect(model->overlay, field));
        unsigned int minute = field == 1 ? window->start_minute : window->end_minute;
        char value[32];
        draw_plan_card(pixels, stride, row, model->overlay_selection == field);
        draw_text(pixels, stride, row.x + 18, row.y + 24,
            field == 1 ? ptc_ui_text(PTC_UI_T_BEGINS_TO_GO_TO_BED_USE_IS) : ptc_ui_text(PTC_UI_T_ENDS_THE_NEXT_DAY_AND_RESUMES_USE),
            13, custom ? UI_MUTED : UI_DISABLED);
        snprintf(value, sizeof(value), "%02u:%02u", minute / 60, minute % 60);
        draw_text(pixels, stride, row.x + 18, row.y + 52, value, 22,
            custom ? UI_ACCENT : UI_DISABLED);
        const char *hint = custom ? ptc_ui_text(PTC_UI_T_A_FINE_ADJUSTMENT) : ptc_ui_text(PTC_UI_T_NOT_EDITABLE);
        int hint_x = row.x + row.width - 18 - measure_text(hint, 13);
        draw_text(pixels, stride, hint_x, row.y + 50, hint, 13,
            custom ? UI_MUTED : UI_DISABLED);
    }
}

static void draw_bedtime_editor_timeline(
    uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
    const PtcBedtimeWindow *window, bool custom, const char *neutral_text)
{
    UiRect bar = to_uirect(ptc_ui_bedtime_timeline_rect(model->overlay));
    int matched = custom ? bedtime_matching_preset(window) : -1;
    int tick_y = bar.y + bar.height + 16;
    char summary[112];

    if (custom) {
        draw_bedtime_timeline_strip(pixels, stride, bar,
            window->start_minute, window->end_minute, window->enabled);
        draw_text(pixels, stride, bar.x, tick_y, "00:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width / 4 - 14, tick_y, "06:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width / 2 - 14, tick_y, "12:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width * 3 / 4 - 14, tick_y, "18:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width - 32, tick_y, "24:00", 10, UI_MUTED);
        if (window->enabled) {
            int duration = (int)(window->end_minute + 1440 - window->start_minute) % 1440;
            snprintf(summary, sizeof(summary), ptc_ui_text(PTC_UI_T_S_OVERNIGHT_CONTROL_U_HR_U_MIN),
                matched >= 0 ? ptc_ui_text(PTC_BEDTIME_PRESETS[matched].name) : ptc_ui_text(PTC_UI_T_CUSTOM_TIME_PERIOD),
                (unsigned int)(duration / 60), (unsigned int)(duration % 60));
        } else {
            snprintf(summary, sizeof(summary), "%s", ptc_ui_text(PTC_UI_T_CLOSED_TODAY_USE_WILL_NOT_BE_RESTRICTED));
        }
        draw_text_center(pixels, stride,
            (UiRect){bar.x, tick_y + 4, bar.width, 24},
            summary, 12, window->enabled ? UI_ACCENT : UI_MUTED);
    } else {
        fill_round_rect(pixels, stride, bar, 6, UI_PAGE);
        draw_rect_outline(pixels, stride, bar, 6, 1, UI_BORDER);
        draw_text_center(pixels, stride, bar, neutral_text, 13, UI_MUTED);
    }

    for (int preset = 0; preset < 5; ++preset) {
        UiRect chip = to_uirect(ptc_ui_bedtime_preset_rect(model->overlay, preset));
        bool selected = custom && matched == preset;
        char times[32];
        fill_round_rect(pixels, stride, chip, 9,
            selected ? UI_ACCENT_SOFT : (custom ? UI_RAISED : UI_PAGE));
        draw_rect_outline(pixels, stride, chip, 9, selected ? 2 : 1,
            selected ? UI_ACCENT : UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){chip.x, chip.y + 3, chip.width, 18},
            ptc_ui_text(PTC_BEDTIME_PRESETS[preset].name), 12,
            custom ? (selected ? UI_ACCENT : UI_INK) : UI_DISABLED);
        snprintf(times, sizeof(times), ptc_ui_text(PTC_UI_T_02U_02U_TO_02U_02U),
            PTC_BEDTIME_PRESETS[preset].start / 60, PTC_BEDTIME_PRESETS[preset].start % 60,
            PTC_BEDTIME_PRESETS[preset].end / 60, PTC_BEDTIME_PRESETS[preset].end % 60);
        draw_text_center(pixels, stride, (UiRect){chip.x, chip.y + 21, chip.width, 18},
            times, 10, custom ? UI_MUTED : UI_DISABLED);
    }
}

static void draw_bedtime_window_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcBedtimeWindow *window = &model->draft_bedtime_policy.week[model->bedtime_editor_day];
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 560);
    draw_bedtime_editor_fields(pixels, stride, model, window,
        window->enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF), window->enabled ? UI_SUCCESS : UI_MUTED,
        true, true);
    draw_bedtime_editor_timeline(pixels, stride, model, window, true, "");
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 450,
        ptc_ui_text(PTC_UI_T_DIRECTION_KEY_SELECTION_FIELD_A_FINE_ADJUSTMENT), 13, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 476,
        ptc_ui_text(PTC_UI_T_BEDTIME_PERIOD_MUST_SPAN_MIDNIGHT_USE_IS), 13, UI_WARNING);

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_COMPLETE),
        UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_bedtime_special_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcBedtimeSpecialRule *rule = model->bedtime_special_kind == 0
        ? &model->draft_bedtime_policy.holiday_rule
        : (model->bedtime_special_kind == 1
            ? &model->draft_bedtime_policy.makeup_workday_rule
            : &model->draft_bedtime_policy.scheduled_override.rule);
    bool custom = rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM;
    uint32_t mode_color = custom ? UI_ACCENT :
        (rule->mode == PTC_BEDTIME_OVERRIDE_DISABLED ? UI_MUTED : UI_SUCCESS);
    const char *neutral = rule->mode == PTC_BEDTIME_OVERRIDE_DISABLED
        ? ptc_ui_text(PTC_UI_T_THERE_IS_NO_SLEEPING_LIMIT_ON_THAT)
        : ptc_ui_text(PTC_UI_T_FOLLOW_THE_WEEKLY_SCHEDULE_USE_THE_BEDTIME);
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 560);
    draw_bedtime_editor_fields(pixels, stride, model, &rule->window,
        bedtime_override_label(rule->mode), mode_color, custom, false);
    draw_bedtime_editor_timeline(pixels, stride, model, &rule->window,
        custom, neutral);
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 450,
        custom
            ? ptc_ui_text(PTC_UI_T_DIRECTION_KEY_SELECTION_FIELD_A_FINE_ADJUSTMENT_2)
            : ptc_ui_text(PTC_UI_T_A_X_CHANGES_MODE_SELECT_CUSTOM_WINDOW),
        13, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 476,
        custom ? ptc_ui_text(PTC_UI_T_THE_CUSTOM_WINDOW_MUST_SPAN_MIDNIGHT_THE)
               : ptc_ui_text(PTC_UI_T_FOLLOW_AND_FREE_MODES_WILL_NOT_USE),
        13, custom ? UI_WARNING : UI_MUTED);

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_COMPLETE),
        UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_bedtime_bulk_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const char *LABELS[] = {ptc_ui_text(PTC_UI_T_MONDAY_TO_FRIDAY), ptc_ui_text(PTC_UI_T_SATURDAY_SUNDAY)};
    draw_dialog_shell(pixels, stride, model, &dialog, 700, 370);
    for (int i = 0; i < 2; ++i) {
        UiRect option = {dialog.x + 46 + i * 304, dialog.y + 138, 284, 82};
        draw_candidate_button(pixels, stride, (PtcUiRect){option.x, option.y, option.width, option.height},
            LABELS[i], UI_PAGE, UI_ACCENT, model->overlay_selection == i, false);
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_CANCEL),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_COPY),
        UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_bedtime_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 780, 390);
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 164,
        ptc_ui_text(PTC_UI_T_THE_DRAFT_REMAINS_UNTIL_SAVED_OR_EXPLICITLY), 17, UI_MUTED);
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay),
        model->bedtime_switch_pending ? ptc_ui_text(PTC_UI_T_X_GIVE_UP_AND_SWITCH) : ptc_ui_text(PTC_UI_T_X_GIVE_UP_AND_LEAVE),
        UI_DANGER_SOFT, UI_DANGER, model->overlay_selection == 1, false);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_CONTINUE_EDITING),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
        model->disable_flag_present ? ptc_ui_text(PTC_UI_T_CANNOT_BE_SAVED_DURING_EMERGENCY_DEACTIVATION) :
            (model->bedtime_switch_pending ? ptc_ui_text(PTC_UI_T_A_SAVE_AND_SWITCH) : ptc_ui_text(PTC_UI_T_A_SAVE_AND_EXIT)),
        UI_ACCENT, UI_ON_ACCENT, model->disable_flag_present);
}

static void draw_weekly_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    bool refreshing = strcmp(model->overlay_title, ptc_ui_text(PTC_UI_T_REFRESH_WEEKLY_PLAN)) == 0;
    bool disabled = model->disable_flag_present;
    draw_dialog_shell(pixels, stride, model, &dialog, 860, 350);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 150, dialog.width - 80, 34},
                     ptc_ui_text(PTC_UI_T_THERE_ARE_UNSAVED_MODIFICATIONS_TO_THE_WEEKLY), 22, UI_WARNING);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 190, dialog.width - 80, 24},
                     ptc_ui_text(PTC_UI_T_SELECT_LEFT_AND_RIGHT_A_CONFIRM_YOU), 17, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_discard_rect(model->overlay),
                       refreshing ? ptc_ui_text(PTC_UI_T_X_GIVE_UP_AND_REFRESH) : ptc_ui_text(PTC_UI_T_X_GIVE_UP_AND_LEAVE),
                       UI_DANGER_SOFT, UI_DANGER, true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), disabled ? ptc_ui_text(PTC_UI_T_B_BACK) : ptc_ui_text(PTC_UI_T_B_CONTINUE_EDITING),
                        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       disabled
                         ? (refreshing ? ptc_ui_text(PTC_UI_T_KEEP_DRAFT_AND_REFRESH) : ptc_ui_text(PTC_UI_T_KEEP_DRAFT_AND_LEAVE))
                         : (refreshing ? ptc_ui_text(PTC_UI_T_SAVE_AND_REFRESH) : ptc_ui_text(PTC_UI_T_SAVE_AND_EXIT)),
                       UI_ACCENT, UI_ON_ACCENT, false);
    if (model->weekly_leave_selection == 0) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_discard_rect(model->overlay)), 12, 3, UI_DANGER);
    } else if (model->weekly_leave_selection == 1) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_cancel_rect(model->overlay)), 12, 3, UI_ACCENT);
    } else {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_confirm_rect(model->overlay)), 12, 3, UI_SURFACE);
    }
}

static bool holiday_is_past_or_today(
    const PtcHolidayArrangement *entry,
    uint16_t cur_year, uint8_t cur_month, uint8_t cur_day,
    bool *is_past_out, bool *is_today_out)
{
    *is_past_out = false;
    *is_today_out = false;
    if (!entry) return false;
    uint8_t last_month = entry->end_month;
    uint8_t last_day = entry->end_day;
    /* Calendar fixture fields have a fixed Chinese data format, independent of UI language. */
    if (entry->makeup_workdays && strcmp(entry->makeup_workdays, "无") != 0) {
        const char *p = entry->makeup_workdays;
        while (*p) {
            if (*p >= '0' && *p <= '9') {
                unsigned int m = 0, d = 0;
                if (sscanf(p, "%u月%u日", &m, &d) == 2) {
                    if (m > last_month || (m == last_month && d > last_day)) {
                        last_month = (uint8_t)m;
                        last_day = (uint8_t)d;
                    }
                }
                const char *next = strstr(p, "日");
                if (next) {
                    p = next + strlen("日");
                    continue;
                }
            }
            p++;
        }
    }
    if (cur_year > entry->year ||
        (cur_year == entry->year && (cur_month > last_month || (cur_month == last_month && cur_day > last_day)))) {
        *is_past_out = true;
        return true;
    }
    if (cur_year == entry->year) {
        bool in_holiday = (cur_month > entry->start_month || (cur_month == entry->start_month && cur_day >= entry->start_day)) &&
                          (cur_month < entry->end_month || (cur_month == entry->end_month && cur_day <= entry->end_day));
        if (in_holiday) {
            *is_today_out = true;
        } else if (entry->makeup_workdays && strcmp(entry->makeup_workdays, "无") != 0) {
            char today_str[32];
            snprintf(today_str, sizeof(today_str), "%u月%u日", cur_month, cur_day);
            if (strstr(entry->makeup_workdays, today_str)) {
                *is_today_out = true;
            }
        }
    }
    return true;
}

static void draw_holiday_calendar_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    uint16_t year = model->holiday_calendar_year;
    if (!year) year = ptc_holiday_calendar_info()->first_year;
    const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info_for_year(year);
    size_t count = ptc_holiday_calendar_arrangement_count(year);
    const int per_page = 4;
    int pages = (int)((count + per_page - 1) / per_page);
    int page = model->holiday_calendar_page;
    char line[192];
    uint16_t cur_year = 0;
    uint8_t cur_month = 0, cur_day = 0;
    bool has_date = model->status_loaded && ptc_date_from_day_index(model->day_index, &cur_year, &cur_month, &cur_day);
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 600);
    if (model->holiday_dirty) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + dialog.width - 330, dialog.y + 24, 290, 34}, 16, UI_WARNING_SOFT);
        draw_text_center(pixels, stride, (UiRect){dialog.x + dialog.width - 330, dialog.y + 24, 290, 34},
                         ptc_ui_text(PTC_UI_T_PREVIEW_UNSAVED_SETTINGS), 16, UI_WARNING);
    }
    if ((!model->calendar_builtin || !info) && !model->holiday_calendar_data) {
        draw_text_center(pixels, stride,
            (UiRect){dialog.x + 40, dialog.y + 240, dialog.width - 80, 60},
            ptc_ui_text(PTC_UI_T_CALENDAR_INVALID_FILE), 20, UI_WARNING);
        draw_candidate_button(pixels, stride, ptc_ui_holiday_page_action_rect(2),
            ptc_ui_text(PTC_UI_T_A_B_CLOSE), UI_ACCENT, UI_ON_ACCENT, true, false);
        return;
    }
    if (model->holiday_calendar_data) {
        const PtcImportedCalendarYear *imported = model->holiday_calendar_data;
        size_t total = imported->group_count + imported->workday_count;
        uint16_t first = 0;
        pages = total ? (int)((total + 3u) / 4u) : 1;
        if (page < 0 || page >= pages) page = 0;
        (void)ptc_day_index_from_date(imported->year, 1, 1, &first);
        for (int row = 0; row < 4; ++row) {
            size_t index = (size_t)(page * 4 + row);
            UiRect card = {dialog.x + 34, dialog.y + 146 + row * 82, dialog.width - 68, 70};
            const char *name;
            if (index >= total) break;
            if (index < imported->group_count) {
                const PtcCalendarHolidayGroup *group = &imported->groups[index];
                uint16_t y;
                uint8_t sm = 0, sd = 0, em = 0, ed = 0;
                (void)ptc_date_from_day_index(group->first_day_index, &y, &sm, &sd);
                (void)ptc_date_from_day_index(group->last_day_index, &y, &em, &ed);
                name = group->name;
                snprintf(line, sizeof(line), "%04u-%02u-%02u - %04u-%02u-%02u",
                    imported->year, sm, sd, imported->year, em, ed);
            } else {
                size_t ordinal = index - imported->group_count, seen = 0;
                uint16_t y = 0;
                uint8_t m = 0, d = 0;
                name = ptc_ui_text(PTC_UI_T_MAKEUP_WORKDAY);
                for (uint16_t offset = 0; offset < 366; ++offset) {
                    if (imported->days[offset] != PTC_CALENDAR_DAY_MAKEUP_WORKDAY) continue;
                    if (seen++ == ordinal) {
                        (void)ptc_date_from_day_index((uint16_t)(first + offset), &y, &m, &d);
                        break;
                    }
                }
                snprintf(line, sizeof(line), "%04u-%02u-%02u", y, m, d);
            }
            fill_round_rect(pixels, stride, card, 16, UI_RAISED);
            draw_rect_outline(pixels, stride, card, 16, 1, UI_BORDER);
            draw_text(pixels, stride, card.x + 20, card.y + 27, name, 21, UI_INK);
            draw_text(pixels, stride, card.x + 20, card.y + 53, line, 16, UI_MUTED);
        }
        if (!total)
            draw_text_center(pixels, stride,
                (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 60},
                ptc_ui_text(PTC_UI_T_CALENDAR_EMPTY), 19, UI_MUTED);
        snprintf(line, sizeof(line), "%u  |  %s  |  %d / %d",
            imported->year, ptc_ui_text(model->calendar_builtin ?
                PTC_UI_T_CALENDAR_PUBLISHED : PTC_UI_T_CALENDAR_USER_IMPORT), page + 1, pages);
        draw_text_center(pixels, stride,
            (UiRect){dialog.x + 30, dialog.y + 480, dialog.width - 60, 30},
            line, 16, UI_MUTED);
        for (int index = 0; index < 3; ++index) {
            bool disabled = (index == 0 && page == 0 && !model->holiday_calendar_has_previous_year) ||
                (index == 1 && page + 1 >= pages && !model->holiday_calendar_has_next_year);
            const char *label = index == 0 ? ptc_ui_text(PTC_UI_T_L_PREVIOUS_PAGE) :
                index == 1 ? ptc_ui_text(PTC_UI_T_R_NEXT_PAGE) :
                ptc_ui_text(PTC_UI_T_A_B_CLOSE);
            draw_candidate_button(pixels, stride, ptc_ui_holiday_page_action_rect(index), label,
                index == 2 ? UI_ACCENT : UI_PAGE,
                index == 2 ? UI_ON_ACCENT : UI_ACCENT,
                model->overlay_selection == index, disabled);
        }
        return;
    }
    if (page < 0 || page >= pages) page = 0;
    for (int row = 0; row < per_page; ++row) {
        size_t index = (size_t)(page * per_page + row);
        const PtcHolidayArrangement *entry = ptc_holiday_calendar_arrangement(year, index);
        UiRect card = {dialog.x + 34, dialog.y + 146 + row * 82, dialog.width - 68, 70};
        if (!entry) break;
        bool is_past = false, is_today = false;
        if (has_date) {
            holiday_is_past_or_today(entry, cur_year, cur_month, cur_day, &is_past, &is_today);
        }
        uint32_t bg_color = is_today ? UI_ACCENT_SOFT : (is_past ? UI_SURFACE : UI_RAISED);
        uint32_t border_color = is_today ? UI_ACCENT : UI_BORDER;
        uint32_t title_color = is_today ? UI_ACCENT : (is_past ? UI_MUTED : UI_INK);
        uint32_t line_color = is_today ? UI_INK : (is_past ? UI_DISABLED : UI_MUTED);
        fill_round_rect(pixels, stride, card, 16, bg_color);
        draw_rect_outline(pixels, stride, card, 16, is_today ? 2 : 1, border_color);
        draw_text(pixels, stride, card.x + 20, card.y + 28, entry->display_name, 21, title_color);
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_HOLIDAY_U_U_U_U_MAKEUP_WORKDAY),
                 entry->start_month, entry->start_day, entry->end_month, entry->end_day, entry->makeup_workdays);
        draw_text(pixels, stride, card.x + 150, card.y + 28, line, 18, line_color);
        UiRect badge = {card.x + card.width - 96, card.y + 21, 76, 28};
        if (is_today) {
            fill_round_rect(pixels, stride, badge, 6, UI_ACCENT);
            draw_text_center(pixels, stride, badge, ptc_ui_text(PTC_UI_T_IN_PROGRESS), 14, UI_ON_ACCENT);
        } else if (is_past) {
            fill_round_rect(pixels, stride, badge, 6, UI_PAGE);
            draw_text_center(pixels, stride, badge, ptc_ui_text(PTC_UI_T_ENDED), 14, UI_MUTED);
        }
    }
    snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_V_U_PUBLISHED_S),
             year, info->version, info->published_date, page + 1, pages);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 30, dialog.y + 480, dialog.width - 60, 30}, line, 16, UI_MUTED);
    for (int index = 0; index < 3; ++index) {
        bool disabled = (index == 0 && page == 0 && !model->holiday_calendar_has_previous_year) ||
            (index == 1 && page + 1 >= pages && !model->holiday_calendar_has_next_year);
        const char *label = index == 0 ? ptc_ui_text(PTC_UI_T_L_PREVIOUS_PAGE) : (index == 1 ? ptc_ui_text(PTC_UI_T_R_NEXT_PAGE) : ptc_ui_text(PTC_UI_T_A_B_CLOSE));
        draw_candidate_button(pixels, stride, ptc_ui_holiday_page_action_rect(index), label,
                              index == 2 ? UI_ACCENT : UI_PAGE,
                              index == 2 ? UI_ON_ACCENT : UI_ACCENT,
                              model->overlay_selection == index, disabled);
    }
}

static void draw_calendar_schedule_tab(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    uint16_t year = model->holiday_calendar_year;
    if (!year) year = ptc_holiday_calendar_info()->first_year;
    const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info_for_year(year);
    size_t count = ptc_holiday_calendar_arrangement_count(year);
    const int per_page = 4;
    int pages = (int)((count + per_page - 1) / per_page);
    int page = model->holiday_calendar_page;
    char line[192];
    uint16_t cur_year = 0;
    uint8_t cur_month = 0, cur_day = 0;
    bool has_date = model->status_loaded && ptc_date_from_day_index(model->day_index, &cur_year, &cur_month, &cur_day);

    UiRect top_bar = {110, 166, 1060, 36};
    const char *reg_name = model->calendar_region_name[0] ? model->calendar_region_name :
        (model->calendar_builtin ? ptc_ui_text(PTC_UI_T_CALENDAR_BUILTIN_CHINA) : ptc_ui_text(PTC_UI_T_CALENDAR_INVALID_FILE));
    char region_hdr[128];
    snprintf(region_hdr, sizeof(region_hdr), ptc_ui_text(PTC_UI_T_CALENDAR_CURRENT_ACTIVE_PREFIX), reg_name, (unsigned int)year);
    draw_text_bold(pixels, stride, top_bar.x, top_bar.y + 24, region_hdr, 19, UI_INK);

    draw_text(pixels, stride, top_bar.x + 480, top_bar.y + 24,
              ptc_ui_text(PTC_UI_T_CALENDAR_YEAR_NAV_HINT), 14, UI_MUTED);

    if (model->holiday_calendar_data) {
        const PtcImportedCalendarYear *imported = model->holiday_calendar_data;
        size_t total = imported->group_count + imported->workday_count;
        uint16_t first = 0;
        pages = total ? (int)((total + 3u) / 4u) : 1;
        if (page < 0 || page >= pages) page = 0;
        (void)ptc_day_index_from_date(imported->year, 1, 1, &first);
        for (int row = 0; row < 4; ++row) {
            size_t index = (size_t)(page * 4 + row);
            UiRect card = {110, 208 + row * 88, 1060, 76};
            const char *name;
            if (index >= total) break;
            if (index < imported->group_count) {
                const PtcCalendarHolidayGroup *group = &imported->groups[index];
                uint16_t y;
                uint8_t sm = 0, sd = 0, em = 0, ed = 0;
                (void)ptc_date_from_day_index(group->first_day_index, &y, &sm, &sd);
                (void)ptc_date_from_day_index(group->last_day_index, &y, &em, &ed);
                name = group->name;
                snprintf(line, sizeof(line), "%04u-%02u-%02u - %04u-%02u-%02u",
                    imported->year, sm, sd, imported->year, em, ed);
            } else {
                size_t ordinal = index - imported->group_count, seen = 0;
                uint16_t y = 0;
                uint8_t m = 0, d = 0;
                name = ptc_ui_text(PTC_UI_T_MAKEUP_WORKDAY);
                for (uint16_t offset = 0; offset < 366; ++offset) {
                    if (imported->days[offset] != PTC_CALENDAR_DAY_MAKEUP_WORKDAY) continue;
                    if (seen++ == ordinal) {
                        (void)ptc_date_from_day_index((uint16_t)(first + offset), &y, &m, &d);
                        break;
                    }
                }
                snprintf(line, sizeof(line), "%04u-%02u-%02u", y, m, d);
            }
            fill_round_rect(pixels, stride, card, 14, UI_RAISED);
            draw_rect_outline(pixels, stride, card, 14, 1, UI_BORDER);
            draw_text_bold(pixels, stride, card.x + 22, card.y + 30, name, 21, UI_INK);
            draw_text(pixels, stride, card.x + 22, card.y + 57, line, 16, UI_MUTED);
        }
        if (!total)
            draw_text_center(pixels, stride, (UiRect){110, 280, 1060, 60},
                ptc_ui_text(PTC_UI_T_CALENDAR_EMPTY), 19, UI_MUTED);
    } else if (info) {
        if (page < 0 || page >= pages) page = 0;
        for (int row = 0; row < per_page; ++row) {
            size_t index = (size_t)(page * per_page + row);
            const PtcHolidayArrangement *entry = ptc_holiday_calendar_arrangement(year, index);
            UiRect card = {110, 208 + row * 88, 1060, 76};
            if (!entry) break;
            bool is_past = false, is_today = false;
            if (has_date) {
                holiday_is_past_or_today(entry, cur_year, cur_month, cur_day, &is_past, &is_today);
            }
            uint32_t bg_color = is_today ? UI_ACCENT_SOFT : (is_past ? UI_SURFACE : UI_RAISED);
            uint32_t border_color = is_today ? UI_ACCENT : UI_BORDER;
            uint32_t title_color = is_today ? UI_ACCENT : (is_past ? UI_MUTED : UI_INK);
            uint32_t line_color = is_today ? UI_INK : (is_past ? UI_DISABLED : UI_MUTED);
            fill_round_rect(pixels, stride, card, 14, bg_color);
            draw_rect_outline(pixels, stride, card, 14, is_today ? 2 : 1, border_color);
            draw_text_bold(pixels, stride, card.x + 22, card.y + 30, entry->display_name, 21, title_color);
            snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_HOLIDAY_U_U_U_U_MAKEUP_WORKDAY),
                     entry->start_month, entry->start_day, entry->end_month, entry->end_day, entry->makeup_workdays);
            draw_text(pixels, stride, card.x + 180, card.y + 30, line, 18, line_color);
            UiRect badge = {card.x + card.width - 110, card.y + 24, 88, 28};
            if (is_today) {
                fill_round_rect(pixels, stride, badge, 6, UI_ACCENT);
                draw_text_center(pixels, stride, badge, ptc_ui_text(PTC_UI_T_IN_PROGRESS), 14, UI_ON_ACCENT);
            } else if (is_past) {
                fill_round_rect(pixels, stride, badge, 6, UI_PAGE);
                draw_text_center(pixels, stride, badge, ptc_ui_text(PTC_UI_T_ENDED), 14, UI_MUTED);
            }
        }
    } else {
        draw_text_center(pixels, stride, (UiRect){110, 280, 1060, 60},
            ptc_ui_text(PTC_UI_T_CALENDAR_INVALID_FILE), 19, UI_WARNING);
    }

    char page_str[32];
    snprintf(page_str, sizeof(page_str), "%d / %d", page + 1, pages > 0 ? pages : 1);
    draw_candidate_button(pixels, stride, ptc_ui_calendar_manager_nav_rect(0),
        ptc_ui_text(PTC_UI_T_L_PREVIOUS_PAGE), UI_RAISED, UI_INK, false, page == 0 && !model->holiday_calendar_has_previous_year);
    draw_candidate_button(pixels, stride, ptc_ui_calendar_manager_nav_rect(1),
        ptc_ui_text(PTC_UI_T_R_NEXT_PAGE), UI_RAISED, UI_INK, false, page + 1 >= pages && !model->holiday_calendar_has_next_year);
    draw_candidate_button(pixels, stride, ptc_ui_calendar_manager_nav_rect(5),
        ptc_ui_text(PTC_UI_T_B_BACK), UI_RAISED, UI_INK, false, false);
}

static void draw_calendar_sources_tab(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    bool focus_left = (model->calendar_sources_subpanel == 0);
    bool focus_right = (model->calendar_sources_subpanel == 1);

    UiRect left_card = {110, 176, 490, 405};
    fill_round_rect(pixels, stride, left_card, 14, focus_left ? UI_ACCENT_SOFT : UI_RAISED);
    draw_rect_outline(pixels, stride, left_card, 14, focus_left ? 2 : 1, focus_left ? UI_ACCENT : UI_BORDER);
    draw_text_bold(pixels, stride, left_card.x + 20, left_card.y + 32,
                   ptc_ui_text(PTC_UI_T_CALENDAR_ONLINE_UPDATE), 18, UI_INK);

    PtcUiTextId status = PTC_UI_T_CALENDAR_UPDATE_READY;
    uint32_t color = UI_MUTED;
    if (model->calendar_check_unavailable) status = PTC_UI_T_CALENDAR_UPDATE_UNAVAILABLE;
    else if (model->calendar_update_status == PTC_UPDATE_CHECKING) {
        status = PTC_UI_T_UPDATE_CHECKING;
        color = UI_ACCENT;
    } else if (model->calendar_update_status == PTC_UPDATE_CURRENT) {
        status = PTC_UI_T_CALENDAR_UPDATE_CURRENT;
    } else if (model->calendar_update_status == PTC_UPDATE_COMPLETED) {
        status = PTC_UI_T_CALENDAR_UPDATE_IMPORTED;
        color = UI_SUCCESS;
    } else if (model->calendar_update_status == PTC_UPDATE_FAILED) {
        status = PTC_UI_T_CALENDAR_UPDATE_FAILED;
        color = UI_DANGER;
    }
    if (model->update_busy && model->calendar_update_status != PTC_UPDATE_CHECKING) {
        status = PTC_UI_T_UPDATE_BUSY;
        color = UI_MUTED;
    }

    UiRect status_box = {left_card.x + 18, left_card.y + 54, left_card.width - 36, 68};
    fill_round_rect(pixels, stride, status_box, 10, UI_SURFACE);
    draw_rect_outline(pixels, stride, status_box, 10, 1, UI_BORDER);
    draw_wrapped_text(pixels, stride, status_box.x + 14, status_box.y + 26,
                      ptc_ui_text(status), 16, status_box.width - 28, 22, 2, color);

    draw_wrapped_text(pixels, stride, left_card.x + 20, left_card.y + 146,
                      ptc_ui_text(PTC_UI_T_CALENDAR_UPDATE_HELP), 14, left_card.width - 40, 20, 3, UI_MUTED);

    PtcUiRect btn_check = {left_card.x + 20, left_card.y + 230, left_card.width - 40, 46};
    draw_candidate_button(pixels, stride, btn_check,
        ptc_ui_text(PTC_UI_T_CALENDAR_UPDATE_CHECK), UI_ACCENT, UI_ON_ACCENT, focus_left, false);

    PtcUiRect btn_startup = {left_card.x + 20, left_card.y + 290, left_card.width - 40, 46};
    draw_candidate_button(pixels, stride, btn_startup,
        model->calendar_check_on_startup ? ptc_ui_text(PTC_UI_T_CALENDAR_STARTUP_ON) : ptc_ui_text(PTC_UI_T_CALENDAR_STARTUP_OFF),
        UI_SURFACE, UI_INK, false, false);

    UiRect right_card = {620, 176, 550, 405};
    fill_round_rect(pixels, stride, right_card, 14, focus_right ? UI_ACCENT_SOFT : UI_RAISED);
    draw_rect_outline(pixels, stride, right_card, 14, focus_right ? 2 : 1, focus_right ? UI_ACCENT : UI_BORDER);
    draw_text_bold(pixels, stride, right_card.x + 20, right_card.y + 32,
                   ptc_ui_text(PTC_UI_T_CALENDAR_LOCAL_IMPORT), 18, UI_INK);

    UiRect path_chip = {right_card.x + 20, right_card.y + 48, right_card.width - 40, 28};
    fill_round_rect(pixels, stride, path_chip, 6, UI_ACCENT_SOFT);
    draw_text(pixels, stride, path_chip.x + 12, path_chip.y + 19,
              "/switch/playwise/calendar-import/", 13, UI_ACCENT);

    if (model->calendar_manager_count == 0) {
        draw_wrapped_text(pixels, stride, right_card.x + 20, right_card.y + 98,
                          ptc_ui_text(PTC_UI_T_CALENDAR_IMPORT_GUIDE), 14, right_card.width - 40, 20, 3, UI_INK);
        draw_wrapped_text(pixels, stride, right_card.x + 20, right_card.y + 180,
                          ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_FIELDS), 13, right_card.width - 40, 19, 3, UI_MUTED);
        draw_wrapped_text(pixels, stride, right_card.x + 20, right_card.y + 252,
                          ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_DOCUMENT), 12, right_card.width - 40, 17, 2, UI_ACCENT);
        draw_wrapped_text(pixels, stride, right_card.x + 20, right_card.y + 296,
                          ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_DOCUMENT_GITEE), 12, right_card.width - 40, 17, 2, UI_ACCENT);
    } else {
        int show_count = model->calendar_manager_count < 4 ? model->calendar_manager_count : 4;
        for (int i = 0; i < show_count; ++i) {
            UiRect file_row = {right_card.x + 20, right_card.y + 86 + i * 50, right_card.width - 40, 44};
            bool row_sel = (i == model->calendar_manager_selected % 4) && focus_right;
            fill_round_rect(pixels, stride, file_row, 8, row_sel ? UI_ACCENT : UI_SURFACE);
            draw_text(pixels, stride, file_row.x + 12, file_row.y + 27,
                      model->calendar_manager_rows[i].title, 15, row_sel ? UI_ON_ACCENT : UI_INK);
        }
        if (model->calendar_preview_count > 0) {
            draw_text(pixels, stride, right_card.x + 20, right_card.y + 300,
                      model->calendar_preview_lines[0], 15, UI_INK);
            if (model->calendar_preview_count > 1) {
                draw_text(pixels, stride, right_card.x + 20, right_card.y + 324,
                          model->calendar_preview_lines[1], 13, UI_ACCENT);
            }
        }
        PtcUiRect btn_import = {right_card.x + 20, right_card.y + 346, right_card.width - 40, 46};
        draw_candidate_button(pixels, stride, btn_import,
            ptc_ui_text(PTC_UI_T_CALENDAR_IMPORT_ACTION), UI_ACCENT, UI_ON_ACCENT, focus_right,
            !model->calendar_pending_file[0]);
    }

    draw_candidate_button(pixels, stride, ptc_ui_calendar_manager_nav_rect(3),
        ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_ACTION), UI_RAISED, UI_INK, false, false);
    draw_candidate_button(pixels, stride, ptc_ui_calendar_manager_nav_rect(4),
        ptc_ui_text(PTC_UI_T_Y_REFRESH), UI_RAISED, UI_INK, false, false);
    draw_candidate_button(pixels, stride, ptc_ui_calendar_manager_nav_rect(5),
        ptc_ui_text(PTC_UI_T_B_BACK), UI_RAISED, UI_INK, false, false);
}

static void draw_calendar_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char page_text[40];
    int pages = (model->calendar_manager_count + 5) / 6;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 640);
    const PtcUiTextId tabs[] = {PTC_UI_T_CALENDAR_TAB_SCHEDULE, PTC_UI_T_CALENDAR_TAB_REGIONS, PTC_UI_T_CALENDAR_TAB_SOURCES};
    for (int i = 0; i < 3; ++i) {
        draw_candidate_button(pixels, stride, ptc_ui_calendar_manager_tab_rect(i),
            ptc_ui_text(tabs[i]),
            model->calendar_manager_tab == i ? UI_ACCENT : UI_RAISED,
            model->calendar_manager_tab == i ? UI_ON_ACCENT : UI_INK,
            model->calendar_manager_tab == i, false);
    }
    draw_wrapped_text(pixels, stride, 740, 128,
        ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_BRIEF), 13, 430, 16, 2, UI_MUTED);

    if (model->calendar_manager_tab == 0) {
        draw_calendar_schedule_tab(pixels, stride, model);
        return;
    }
    if (model->calendar_manager_tab == 2) {
        draw_calendar_sources_tab(pixels, stride, model);
        return;
    }

    /* Tab 1: 地区日历库 */
    for (int i = 0; i < 6; ++i) {
        int index = model->calendar_manager_page * 6 + i;
        UiRect rect = to_uirect(ptc_ui_calendar_manager_row_rect(i));
        bool selected = index == model->calendar_manager_selected;
        char fitted[128];
        if (index >= model->calendar_manager_count) break;
        fill_round_rect(pixels, stride, rect, 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, rect, 10, selected ? 2 : 1,
            selected ? UI_ACCENT : UI_BORDER);
        fit_text(fitted, sizeof(fitted), model->calendar_manager_rows[index].title,
            17, rect.width - 28);
        draw_text(pixels, stride, rect.x + 14, rect.y + 23,
            fitted, 17, UI_INK);
        draw_text(pixels, stride, rect.x + 14, rect.y + 47,
            model->calendar_manager_rows[index].detail, 13, UI_MUTED);
    }
    if (model->calendar_manager_count == 0)
        draw_text(pixels, stride, 130, 230, ptc_ui_text(PTC_UI_T_CALENDAR_EMPTY), 19, UI_MUTED);

    fill_round_rect(pixels, stride, (UiRect){650, 176, 520, 405}, 12, UI_RAISED);
    draw_rect_outline(pixels, stride, (UiRect){650, 176, 520, 405}, 12, 1, UI_BORDER);

    int preview_y = 206, preview_step = 30;
    int preview_title_size = 18, preview_detail_size = 15;
    for (int i = 0; i < model->calendar_preview_count && i < 12; ++i) {
        char fitted[144];
        int size = i == 0 ? preview_title_size : preview_detail_size;
        fit_text(fitted, sizeof(fitted), model->calendar_preview_lines[i],
            size, 480);
        draw_text(pixels, stride, 670, preview_y + i * preview_step,
            fitted, size, i == 0 ? UI_INK : UI_MUTED);
    }

    snprintf(page_text, sizeof(page_text), "%d / %d", model->calendar_manager_page + 1,
        pages > 0 ? pages : 1);
    draw_text(pixels, stride, 110, 665, page_text, 14, UI_MUTED);
    draw_text(pixels, stride, 250, 665, ptc_ui_text(PTC_UI_T_CALENDAR_MANAGER_HINT), 14, UI_MUTED);

    for (int i = 0; i < 6; ++i) {
        char label[96];
        bool disabled = (i == 0 && model->calendar_manager_page == 0) ||
            (i == 1 && model->calendar_manager_page + 1 >= pages) ||
            (i == 2 && !model->calendar_pending_option_id[0]) ||
            (i == 2 && model->disable_flag_present) ||
            (model->waiting && i < 3) || (model->waiting && i == 4);
        const char *caption = i == 0 ? ptc_ui_text(PTC_UI_T_CALENDAR_PREVIOUS) :
            i == 1 ? ptc_ui_text(PTC_UI_T_CALENDAR_NEXT) :
            i == 3 ? ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_ACTION) :
            i == 4 ? ptc_ui_text(PTC_UI_T_Y_REFRESH) :
            i == 5 ? ptc_ui_text(PTC_UI_T_B_BACK) :
            ptc_ui_text(PTC_UI_T_CALENDAR_APPLY_ACTION);
        snprintf(label, sizeof(label), "%s%s", i == 0 ? "ZL " :
            i == 1 ? "ZR " : i == 2 ? "A " : i == 3 ? "X " : "", caption);
        draw_candidate_button(pixels, stride, ptc_ui_calendar_manager_nav_rect(i),
            label, i == 2 ? UI_ACCENT : UI_RAISED,
            i == 2 ? UI_ON_ACCENT : UI_INK, false, disabled);
    }
}

static void draw_dock_rules_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell = *model;
    snprintf(shell.overlay_title, sizeof(shell.overlay_title), "%s", ptc_ui_text(PTC_UI_T_DOCK_RULES_EXPLAIN_TITLE));
    shell.overlay_body[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell, &dialog, 1000, 560);

    const PtcUiTextId titles[] = {
        PTC_UI_T_TOTAL_DAILY_ALLOWANCE,
        PTC_UI_T_DOCK_TITLE,
        PTC_UI_T_BEDTIME_OPERATES_INDEPENDENTLY,
        PTC_UI_T_DOCK_RULE_TITLE
    };
    const PtcUiTextId items[] = {
        PTC_UI_T_DOCK_RULES_EXPLAIN_CORE1,
        PTC_UI_T_DOCK_RULES_EXPLAIN_CORE2,
        PTC_UI_T_DOCK_RULES_EXPLAIN_CORE3,
        PTC_UI_T_DOCK_RULES_EXPLAIN_CORE4
    };
    const char *emojis[] = {"⏰", "📺", "🌙", "🛡️"};
    uint32_t accent_colors[] = {UI_ACCENT, UI_WARNING, UI_HERO, UI_SUCCESS};

    int card_w = (dialog.width - 68 - 18) / 2;
    int card_h = 166;
    for (int i = 0; i < 4; ++i) {
        int col = i % 2;
        int row = i / 2;
        int cx = dialog.x + 34 + col * (card_w + 18);
        int cy = dialog.y + 100 + row * (card_h + 16);
        UiRect c = {cx, cy, card_w, card_h};
        char num[8];
        char fitted_title[128];
        snprintf(num, sizeof(num), "0%d", i + 1);

        fill_round_rect(pixels, stride, c, 12, UI_PAGE);

        ui_set_vertical_clip(c.y, c.y + 44);
        fill_round_rect(pixels, stride, c, 12, UI_RAISED);
        ui_set_vertical_clip(0, SCREEN_HEIGHT);
        draw_line(pixels, stride, c.x + 1, c.y + 44, c.x + c.width - 1, c.y + 44, 1, UI_BORDER);

        fill_round_rect(pixels, stride, (UiRect){c.x + 4, c.y + 10, 4, c.height - 20}, 2, accent_colors[i]);
        draw_rect_outline(pixels, stride, c, 12, 1, UI_BORDER);

        UiRect badge = {c.x + c.width - 14 - 34, c.y + 11, 34, 22};
        fill_round_rect(pixels, stride, badge, 11, UI_SURFACE);
        draw_rect_outline(pixels, stride, badge, 11, 1, UI_BORDER);
        draw_text_center(pixels, stride, badge, num, 11, UI_MUTED);

        draw_text(pixels, stride, c.x + 18, c.y + 29, emojis[i], 16, UI_INK);
        fit_text(fitted_title, sizeof(fitted_title), ptc_ui_text(titles[i]), 15, badge.x - (c.x + 44) - 10);
        draw_text_bold(pixels, stride, c.x + 44, c.y + 29, fitted_title, 15, UI_INK);

        draw_wrapped_text(pixels, stride, c.x + 18, c.y + 70,
                          ptc_ui_text(items[i]), 13, c.width - 34, 20, 5, UI_INK);
    }

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay),
        ptc_ui_text(PTC_UI_T_B_BACK), UI_RAISED, UI_INK, true);
}

static void draw_eye_care_rules_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell = *model;
    snprintf(shell.overlay_title, sizeof(shell.overlay_title), "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_RULES_BTN));
    shell.overlay_body[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell, &dialog, 1000, 560);

    static const PtcUiTextId titles[] = {
        PTC_UI_T_EYE_CARE_LOGIC_TITLE_1,
        PTC_UI_T_EYE_CARE_LOGIC_TITLE_2,
        PTC_UI_T_EYE_CARE_LOGIC_TITLE_3,
        PTC_UI_T_EYE_CARE_LOGIC_TITLE_4
    };
    static const PtcUiTextId items[] = {
        PTC_UI_T_EYE_CARE_LOGIC_DESC_1,
        PTC_UI_T_EYE_CARE_LOGIC_DESC_2,
        PTC_UI_T_EYE_CARE_LOGIC_DESC_3,
        PTC_UI_T_EYE_CARE_LOGIC_DESC_4
    };
    const char *emojis[] = {"🌿", "⏰", "✨", "🛡️"};
    uint32_t accent_colors[] = {UI_SUCCESS, UI_ACCENT, UI_WARNING, UI_HERO};

    int card_w = (dialog.width - 68 - 18) / 2;
    int card_h = 166;
    for (int i = 0; i < 4; ++i) {
        int col = i % 2;
        int row = i / 2;
        int cx = dialog.x + 34 + col * (card_w + 18);
        int cy = dialog.y + 100 + row * (card_h + 16);
        UiRect c = {cx, cy, card_w, card_h};
        char num[8];
        char fitted_title[128];
        snprintf(num, sizeof(num), "0%d", i + 1);

        fill_round_rect(pixels, stride, c, 12, UI_PAGE);
        ui_set_vertical_clip(c.y, c.y + 44);
        fill_round_rect(pixels, stride, c, 12, UI_RAISED);
        ui_set_vertical_clip(0, SCREEN_HEIGHT);
        draw_line(pixels, stride, c.x + 1, c.y + 44, c.x + c.width - 1, c.y + 44, 1, UI_BORDER);

        fill_round_rect(pixels, stride, (UiRect){c.x + 4, c.y + 10, 4, c.height - 20}, 2, accent_colors[i]);
        draw_rect_outline(pixels, stride, c, 12, 1, UI_BORDER);

        UiRect badge = {c.x + c.width - 14 - 34, c.y + 11, 34, 22};
        fill_round_rect(pixels, stride, badge, 11, UI_SURFACE);
        draw_rect_outline(pixels, stride, badge, 11, 1, UI_BORDER);
        draw_text_center(pixels, stride, badge, num, 11, UI_MUTED);

        draw_text(pixels, stride, c.x + 18, c.y + 29, emojis[i], 16, UI_INK);
        fit_text(fitted_title, sizeof(fitted_title), ptc_ui_text(titles[i]), 15, badge.x - (c.x + 44) - 10);
        draw_text_bold(pixels, stride, c.x + 44, c.y + 29, fitted_title, 15, UI_INK);

        draw_wrapped_text(pixels, stride, c.x + 18, c.y + 70,
                          ptc_ui_text(items[i]), 13, c.width - 34, 20, 5, UI_INK);
    }

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay),
        ptc_ui_text(PTC_UI_T_B_BACK), UI_RAISED, UI_INK, true);
}

static void draw_calendar_format_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    static const PtcUiTextId lines[] = {
        PTC_UI_T_CALENDAR_FORMAT_FILE, PTC_UI_T_CALENDAR_FORMAT_FIELDS,
        PTC_UI_T_CALENDAR_FORMAT_DATES, PTC_UI_T_CALENDAR_FORMAT_LIMITS,
        PTC_UI_T_CALENDAR_IMPORT_CONFIRM
    };
    draw_dialog_shell(pixels, stride, model, &dialog, 1000, 560);
    for (int i = 0; i < 5; ++i)
        draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 104 + i * 62,
            ptc_ui_text(lines[i]), 18, dialog.width - 68, 24, 2, UI_INK);
    draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 422,
        ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_DOCUMENT), 14, dialog.width - 68, 18, 2, UI_ACCENT);
    draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 448,
        ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_DOCUMENT_GITEE), 14, dialog.width - 68, 18, 2, UI_ACCENT);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay),
        ptc_ui_text(PTC_UI_T_B_BACK), UI_RAISED, UI_INK, true);
}

static void draw_weekly_bulk_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *DAYS[] = {ptc_ui_text(PTC_UI_T_SUNDAY), ptc_ui_text(PTC_UI_T_MONDAY), ptc_ui_text(PTC_UI_T_TUESDAY), ptc_ui_text(PTC_UI_T_WEDNESDAY), ptc_ui_text(PTC_UI_T_THURSDAY), ptc_ui_text(PTC_UI_T_FRIDAY), ptc_ui_text(PTC_UI_T_SATURDAY)};
    UiRect dialog;
    char source[96];
    char line[128];
    PtcUiWeeklyBulkStats stats;
    int slot = model->weekly_last_day_slot >= 0 && model->weekly_last_day_slot < 7 ? model->weekly_last_day_slot : 0;
    int day = ptc_ui_weekday_for_display_slot(slot);
    PtcDayRule rule = model->draft_week[day];
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 560);
    if (rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(source, sizeof(source), ptc_ui_text(PTC_UI_T_SOURCE_S_UNLIMITED), DAYS[day]);
    else snprintf(source, sizeof(source), ptc_ui_text(PTC_UI_T_SOURCE_S_U_MIN_LIMIT), DAYS[day], (unsigned int)rule.minutes);
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 116, source, 19, UI_INK);
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 148, ptc_ui_text(PTC_UI_T_1_SELECT_TARGET), 17, UI_MUTED);
    for (int index = 0; index < 2; ++index) {
        UiRect card = to_uirect(ptc_ui_weekly_bulk_target_rect(index));
        bool selected = model->overlay_selection == index;
        fill_round_rect(pixels, stride, card, 16, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, card, 16, selected ? 3 : 1, selected ? UI_ACCENT : UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 18, card.width, 34},
                         index == 0 ? ptc_ui_text(PTC_UI_T_WORKING_DAY) : ptc_ui_text(PTC_UI_T_WEEKEND), 22, UI_INK);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 54, card.width, 28},
                         index == 0 ? ptc_ui_text(PTC_UI_T_MONDAY_TO_FRIDAY) : ptc_ui_text(PTC_UI_T_SATURDAY_AND_SUNDAY), 17, UI_MUTED);
    }
    ptc_ui_weekly_bulk_stats(model, model->overlay_selection == 1, &stats);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 490, dialog.y + 148, 510, 270}, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, (UiRect){dialog.x + 490, dialog.y + 148, 510, 270}, 16, 1, UI_BORDER);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 180, ptc_ui_text(PTC_UI_T_2_IMPACT_ON_TODAY), 20, UI_INK);
    snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_TARGET_D_DAYS_CHANGE_D_SKIP_D),
             stats.target_count, stats.changed_count, stats.unchanged_count);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 218, line, 17, UI_ACCENT);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 256, ptc_ui_text(PTC_UI_T_TARGET_CURRENT_RULE), 16, UI_MUTED);
    for (int index = 0; index < stats.rule_group_count; ++index) {
        PtcDayRule group = stats.rule_groups[index].rule;
        if (group.mode == PTC_RULE_MODE_UNLIMITED) snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_UNLIMITED_D_DAYS), stats.rule_groups[index].count);
        else snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_MIN_LIMIT_D_DAYS), (unsigned int)group.minutes, stats.rule_groups[index].count);
        draw_text(pixels, stride, dialog.x + 536, dialog.y + 288 + index * 27, line, 16, UI_INK);
    }
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 424,
              ptc_ui_text(PTC_UI_T_AFTER_APPLICATION_ONLY_THE_DRAFT_IN_THIS), 16, UI_WARNING);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       stats.changed_count > 0 ? ptc_ui_text(PTC_UI_T_A_APPLY_TO_DRAFT) : ptc_ui_text(PTC_UI_T_A_NO_MODIFICATION_REQUIRED),
                       stats.changed_count > 0 ? UI_ACCENT : UI_DISABLED,
                       UI_ON_ACCENT, false);
}

static void draw_holiday_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 720, 320);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 148, dialog.width - 72, 28},
                     ptc_ui_text(PTC_UI_T_UNSAVED_HOLIDAY_SETTINGS_WILL_BE_LOST_AFTER), 19, UI_WARNING);
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay), ptc_ui_text(PTC_UI_T_X_DISCARD_CHANGES),
                          UI_DANGER_SOFT, UI_DANGER, model->holiday_leave_selection == 0, false);
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_CONTINUE_EDITING),
                          UI_PAGE, UI_ACCENT, model->holiday_leave_selection == 1, false);
}

bool draw_plan_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
    case PTC_UI_OVERLAY_SCHEDULED_LEAVE:
        draw_scheduled_leave(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SCHEDULED:
        draw_scheduled_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_AUTONOMY:
        draw_autonomy_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_EYE_CARE:
        draw_eye_care_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_QUICK_ADD:
        draw_quick_add_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME:
        draw_bedtime_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_WINDOW:
        draw_bedtime_window_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_SPECIAL:
        draw_bedtime_special_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_LEAVE:
        draw_bedtime_leave_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_BULK:
        draw_bedtime_bulk_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
        draw_weekly_leave_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_HOLIDAY_CALENDAR:
        draw_holiday_calendar_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_CALENDAR_MANAGER:
        draw_calendar_manager_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_CALENDAR_FORMAT:
        draw_calendar_format_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_DOCK_RULES:
        draw_dock_rules_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_EYE_CARE_RULES:
        draw_eye_care_rules_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_HOLIDAY_LEAVE:
        draw_holiday_leave_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_WEEKLY_BULK:
        draw_weekly_bulk_overlay(pixels, stride, model);
        return true;
    default:
        return false;
    }
}
