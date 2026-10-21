#include "ui_render_internal.h"

static void setup_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect,
    const char *label, bool selected, bool primary)
{
    UiRect r = to_uirect(rect);
    uint32_t fill = primary ? UI_ACCENT : (selected ? UI_ACCENT_SOFT : UI_SURFACE);
    uint32_t border = selected ? UI_ACCENT : (primary ? UI_ACCENT : UI_BORDER);
    uint32_t text_color = primary ? UI_ON_ACCENT : (selected ? UI_ACCENT : UI_INK);
    fill_round_rect(pixels, stride, r, 10, fill);
    draw_rect_outline(pixels, stride, r, 10, selected ? 2 : 1, border);
    if (selected) draw_focus_ring(pixels, stride, r, 10);
    draw_button_label(pixels, stride, r, label, 18, text_color);
}

/* Wizard cards stack rows from measured wrapped line counts instead of fixed
   baselines: English strings wrap where Chinese ones do not, and a fixed grid
   left one row crowded while the next floated in empty space. */
static int setup_line_height(int size)
{
    return size * 3 / 2;
}

static int setup_block_height(const char *text, int size, int width, int max_lines)
{
    int lines = count_wrapped_lines(text, size, width, max_lines);
    if (lines < 1) lines = 1;
    return lines * setup_line_height(size);
}

/* Draws one block whose ink starts at `top` and returns the next block's top. */
static int setup_block(uint32_t *pixels, uint32_t stride, int x, int top,
    const char *text, int size, int width, int max_lines, uint32_t color)
{
    int lines = count_wrapped_lines(text, size, width, max_lines);
    if (lines < 1) lines = 1;
    draw_wrapped_text(pixels, stride, x, top + size, text, size, width,
        setup_line_height(size), max_lines, color);
    return top + lines * setup_line_height(size);
}

void draw_setup(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    int step = model->setup_step > 0 ? model->setup_step : PTC_UI_SETUP_PREPARE;
    const char *labels[] = {ptc_ui_text(PTC_UI_T_SETUP_PREPARE), ptc_ui_text(PTC_UI_T_SETUP_PARENT),
        ptc_ui_text(PTC_UI_T_SETUP_CONFIRM)};
    char text[320];
    snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_SETUP_TITLE), step);
    draw_header(pixels, stride, text, labels[step - 1]);
    fill_round_rect(pixels, stride, (UiRect){54, 120, 1172, 510}, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, (UiRect){54, 120, 1172, 510}, 16, 1, UI_BORDER);
    for (int i = 0; i < 3; ++i) {
        draw_text_center(pixels, stride, (UiRect){94 + i * 372, 133, 340, 26}, labels[i], 17,
            step == i + 1 ? UI_ACCENT : UI_MUTED);
        fill_round_rect(pixels, stride, (UiRect){94 + i * 372, 165, 340, 4}, 2, i < step ? UI_ACCENT : UI_RAISED);
    }
    if (step == PTC_UI_SETUP_PREPARE) {
        /* Card 1: Interface Language (Y=185..298). Content block (title, then the
           44px option row) keeps equal 13/14px padding top and bottom. */
        UiRect lang_card = {74, 185, 1132, 113};
        fill_round_rect(pixels, stride, lang_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, lang_card, 12, 1, UI_BORDER);
        const char *lang_title = ptc_ui_text(PTC_UI_T_SETUP_CARD_LANG);
        draw_text(pixels, stride, 94, 214, lang_title, 17, UI_INK);
        /* Hints follow the measured title so the English card name cannot run
           into the Chinese hint (it used to start at a fixed X=210). */
        int hint_x = 94 + measure_text(lang_title, 17) + 28;
        const char *hint_zh = ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HINT_ZH);
        const char *hint_en = ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HINT_EN);
        char fitted_hint[256];
        fit_text(fitted_hint, sizeof(fitted_hint), hint_zh, 13, 380);
        draw_text(pixels, stride, hint_x, 215, fitted_hint, 13, UI_MUTED);
        hint_x += measure_text(fitted_hint, 13) + 32;
        if (hint_x < 1060) {
            fit_text(fitted_hint, sizeof(fitted_hint), hint_en, 13, 1190 - hint_x);
            draw_text(pixels, stride, hint_x, 215, fitted_hint, 13, UI_MUTED);
        }
        for (int i = 0; i < 4; ++i) {
            PtcUiRect r = ptc_ui_setup_language_rect(i);
            bool chosen = model->language_preference == (PtcUiLanguagePreference)i;
            bool focused = (model->setup_focus == 1) && chosen;
            UiRect ur = to_uirect(r);
            fill_round_rect(pixels, stride, ur, 10, chosen ? UI_ACCENT_SOFT : UI_SURFACE);
            draw_rect_outline(pixels, stride, ur, 10, (focused || chosen) ? 2 : 1, chosen ? UI_ACCENT : UI_BORDER);
            if (focused) draw_focus_ring(pixels, stride, ur, 10);
            draw_text_center(pixels, stride, ur, i == 0 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_SYSTEM) :
                i == 1 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HANS) :
                i == 2 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HANT) : "English", 16, chosen ? UI_ACCENT : UI_INK);
        }

        /* Card 2: Environment and Console Time (Y=312..618). The three bands are
           sized from their measured content plus one shared 24px padding, so no
           band ends up tight while another floats in empty space. */
        UiRect env_card = {74, 312, 1132, 306};
        fill_round_rect(pixels, stride, env_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, env_card, 12, 1, UI_BORDER);

        /* Section 1: Running Environment (Y=312..432) */
        draw_text(pixels, stride, 94, 352, ptc_ui_text(PTC_UI_T_SETUP_CARD_ENV), 17, UI_INK);
        snprintf(text, sizeof(text), "HOS %s  |  Atmosphère %s  |  %s",
            model->environment_available && model->environment_hos[0] ? model->environment_hos : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
            model->environment_available && model->environment_atmosphere_version[0] ? model->environment_atmosphere_version : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
            model->environment_available && model->environment_model[0] ?
                (strcmp(model->environment_model, "mariko-oled") == 0 ? "OLED" : model->environment_model) : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
        draw_text(pixels, stride, 94, 379, text, 17, UI_INK);
        draw_text(pixels, stride, 94, 403, ptc_ui_text(PTC_UI_T_SETUP_REFERENCE), 14, UI_MUTED);
        draw_text(pixels, stride, 460, 403, ptc_ui_text(ptc_ui_setup_reference_matches(model)
            ? PTC_UI_T_SETUP_REFERENCE_MATCH : PTC_UI_T_SETUP_REFERENCE_UNKNOWN), 14, UI_MUTED);

        /* Separator line 1 */
        fill_round_rect(pixels, stride, (UiRect){94, 432, 1092, 1}, 0, UI_BORDER);

        /* Section 2: Parental Control row (Y=432..524) */
        draw_text(pixels, stride, 94, 471, ptc_ui_text(!model->restriction_enabled_available ? PTC_UI_T_SETUP_PCTL_UNKNOWN :
            model->restriction_enabled ? PTC_UI_T_SETUP_PCTL_ON : PTC_UI_T_SETUP_PCTL_OFF), 18,
            model->restriction_enabled_available && model->restriction_enabled ? UI_SUCCESS : UI_WARNING_TEXT);
        setup_button(pixels, stride, ptc_ui_setup_pctl_help_rect(), ptc_ui_text(PTC_UI_T_SETUP_PCTL_HELP_BUTTON),
            model->setup_focus == 2, false);
        draw_text(pixels, stride, 94, 494, ptc_ui_text(PTC_UI_T_SETUP_PAUSE_OVERLAY), 14, UI_MUTED);

        /* Separator line 2 */
        fill_round_rect(pixels, stride, (UiRect){94, 524, 1092, 1}, 0, UI_BORDER);

        /* Section 3: Console Time & Direct Time Guide (Y=524..618) */
#ifdef PTC_UI_PREVIEW_WALL_TIME
        uint16_t year, minute = ptc_ui_render_minute_of_day(ptc_ui_render_now());
        uint8_t month, day;
        if (ptc_date_from_day_index(model->day_index, &year, &month, &day))
            snprintf(text, sizeof(text), "%u-%02u-%02u  %02u:%02u", year, month, day, minute / 60, minute % 60);
        else snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
#else
        time_t now = (time_t)ptc_ui_render_now();
        struct tm *local = now > 0 ? localtime(&now) : NULL;
        if (local) strftime(text, sizeof(text), "%Y-%m-%d  %H:%M", local);
        else snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
#endif
        draw_text(pixels, stride, 94, 563, ptc_ui_text(PTC_UI_T_SETUP_TIME_HELP), 15, UI_MUTED);
        draw_text(pixels, stride, 94, 592, text, 24, UI_ACCENT);

        draw_wrapped_text(pixels, stride, 360, 562, ptc_ui_text(PTC_UI_T_SETUP_TIME_HINT), 14, 820, 20, 1, UI_INK);
        draw_wrapped_text(pixels, stride, 360, 586, ptc_ui_text(PTC_UI_T_SETUP_TIME_INSTRUCTIONS), 13, 820, 20, 2, UI_MUTED);
        if (model->setup_focus == 3) {
            draw_focus_ring(pixels, stride, (UiRect){90, 536, 1100, 76}, 8);
        }
    } else if (step == PTC_UI_SETUP_PARENT) {
        /* Left Card: Security & PIN */
        UiRect sec_card = {74, 185, 550, 432};
        fill_round_rect(pixels, stride, sec_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, sec_card, 12, 1, UI_BORDER);
        draw_text(pixels, stride, 94, 212, ptc_ui_text(PTC_UI_T_SETUP_CARD_SECURITY), 20, UI_INK);
        draw_wrapped_text(pixels, stride, 94, 238, ptc_ui_text(PTC_UI_T_SETUP_PIN_DESC), 14, 510, 20, 2, UI_MUTED);

        /* Status badge */
        UiRect pin_badge = {94, 268, 510, 36};
        bool pin_ok = model->setup_pin_ready;
        fill_round_rect(pixels, stride, pin_badge, 8, pin_ok ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
        draw_rect_outline(pixels, stride, pin_badge, 8, 1, pin_ok ? UI_SUCCESS_BORDER : UI_WARNING_BORDER);
        draw_text_center(pixels, stride, pin_badge, ptc_ui_text(pin_ok ? PTC_UI_T_SETUP_PIN_STATUS_CUSTOM : PTC_UI_T_SETUP_PIN_STATUS_DEFAULT), 15, pin_ok ? UI_SUCCESS : UI_WARNING_TEXT);

        /* PIN modification button */
        setup_button(pixels, stride, ptc_ui_setup_pin_rect(), ptc_ui_text(PTC_UI_T_X_CLICK_CHANGE_PIN),
            model->setup_focus == 1, false);

        /* Security Guide Panel - perfectly balances the card height */
        UiRect sec_guide = {94, 372, 510, 230};
        fill_round_rect(pixels, stride, sec_guide, 10, UI_SURFACE);
        draw_rect_outline(pixels, stride, sec_guide, 10, 1, UI_BORDER);
        draw_text(pixels, stride, 108, 396, ptc_ui_text(PTC_UI_T_SETUP_SEC_GUIDE_TITLE), 15, UI_INK);
        fill_round_rect(pixels, stride, (UiRect){108, 407, 482, 1}, 0, UI_BORDER);

        draw_text(pixels, stride, 108, 432, ptc_ui_text(PTC_UI_T_SETUP_SEC_GUIDE_1_TITLE), 13, UI_ACCENT);
        draw_text(pixels, stride, 188, 432, ptc_ui_text(PTC_UI_T_SETUP_SEC_GUIDE_1_DESC), 13, UI_MUTED);

        draw_text(pixels, stride, 108, 474, ptc_ui_text(PTC_UI_T_SETUP_SEC_GUIDE_2_TITLE), 13, UI_ACCENT);
        draw_text(pixels, stride, 188, 474, ptc_ui_text(PTC_UI_T_SETUP_SEC_GUIDE_2_DESC), 13, UI_MUTED);

        draw_text(pixels, stride, 108, 516, ptc_ui_text(PTC_UI_T_SETUP_SEC_GUIDE_3_TITLE), 13, UI_ACCENT);
        draw_text(pixels, stride, 188, 516, ptc_ui_text(PTC_UI_T_SETUP_SEC_GUIDE_3_DESC), 13, UI_MUTED);

        draw_text(pixels, stride, 108, 558, ptc_ui_text(PTC_UI_T_SETUP_SEC_GUIDE_4_TITLE), 13, UI_ACCENT);
        draw_text(pixels, stride, 188, 558, ptc_ui_text(PTC_UI_T_SETUP_SEC_GUIDE_4_DESC), 13, UI_MUTED);

        /* Right Card: Preferences & Feature Toggles */
        UiRect pref_card = {656, 185, 550, 432};
        fill_round_rect(pixels, stride, pref_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, pref_card, 12, 1, UI_BORDER);
        draw_text(pixels, stride, 676, 212, ptc_ui_text(PTC_UI_T_SETUP_CARD_PREFS), 20, UI_INK);

        /* 1. Theme */
        draw_text(pixels, stride, 676, 244, ptc_ui_text(PTC_UI_T_SETUP_THEME_LABEL), 14, UI_MUTED);
        for (int i = 0; i < 3; ++i) {
            bool selected = model->setup_theme_index == i;
            bool focused = model->setup_focus == 2 && selected;
            PtcUiRect tr = ptc_ui_setup_theme_rect(i);
            UiRect utr = to_uirect(tr);
            fill_round_rect(pixels, stride, utr, 10, selected ? UI_ACCENT_SOFT : UI_SURFACE);
            draw_rect_outline(pixels, stride, utr, 10, (focused || selected) ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
            if (focused) draw_focus_ring(pixels, stride, utr, 10);
            draw_text_center(pixels, stride, utr, ptc_ui_theme_preference_label((PtcUiThemePreference)i),
                16, selected ? UI_ACCENT : UI_INK);
        }

        /* 2. Shortcut */
        draw_text(pixels, stride, 676, 320, ptc_ui_text(PTC_UI_T_SETUP_SHORTCUT_LABEL), 14, UI_MUTED);
        UiRect s_rect = to_uirect(ptc_ui_setup_shortcut_rect());
        bool s_focused = model->setup_focus == 3;
        fill_round_rect(pixels, stride, s_rect, 10, s_focused ? UI_ACCENT_SOFT : UI_SURFACE);
        draw_rect_outline(pixels, stride, s_rect, 10, s_focused ? 2 : 1, s_focused ? UI_ACCENT : UI_BORDER);
        if (s_focused) draw_focus_ring(pixels, stride, s_rect, 10);
        char sc_buf[192];
        snprintf(sc_buf, sizeof(sc_buf), "%s  [%s]",
            model->custom_shortcut_label[0] ? model->custom_shortcut_label : "(-)",
            ptc_ui_text(PTC_UI_T_SETUP_SHORTCUT_CHANGE));
        int sc_w = measure_text(sc_buf, 14);
        draw_text(pixels, stride, s_rect.x + 16, s_rect.y + 27, model->custom_shortcut_label[0] ? model->custom_shortcut_label : "(-)", 16, UI_INK);
        draw_text(pixels, stride, s_rect.x + s_rect.width - sc_w - 16, s_rect.y + 27, sc_buf, 14, s_focused ? UI_ACCENT : UI_MUTED);

        /* 3. Eye Care Toggle */
        draw_text(pixels, stride, 676, 398, ptc_ui_text(PTC_UI_T_SETUP_EYE_CARE_LABEL), 14, UI_MUTED);
        bool eye_on = model->setup_eye_care_enabled || model->draft_eye_care_policy.enabled;
        UiRect eye_rect = to_uirect(ptc_ui_setup_eye_care_rect());
        bool eye_focused = model->setup_focus == 4;
        fill_round_rect(pixels, stride, eye_rect, 10, eye_focused ? UI_ACCENT_SOFT : UI_SURFACE);
        draw_rect_outline(pixels, stride, eye_rect, 10, (eye_focused || eye_on) ? 2 : 1, eye_focused ? UI_ACCENT : (eye_on ? UI_ACCENT : UI_BORDER));
        if (eye_focused) draw_focus_ring(pixels, stride, eye_rect, 10);
        draw_text(pixels, stride, eye_rect.x + 16, eye_rect.y + 29, ptc_ui_text(PTC_UI_T_SETUP_EYE_CARE_DEFAULT_PARAMS), 15, UI_INK);
        UiRect eye_sw = {eye_rect.x + eye_rect.width - 66, eye_rect.y + (eye_rect.height - 24) / 2, 50, 24};
        draw_toggle_switch(pixels, stride, eye_sw, eye_on, false, false, NULL, NULL);
        const char *eye_st = eye_on ? ptc_ui_text(PTC_UI_T_ENABLED_2) : ptc_ui_text(PTC_UI_T_DISABLED);
        int eye_st_w = measure_text(eye_st, 14);
        draw_text(pixels, stride, eye_sw.x - eye_st_w - 10, eye_rect.y + 29, eye_st, 14, eye_on ? UI_SUCCESS : UI_MUTED);
        draw_text(pixels, stride, 676, 478, ptc_ui_text(PTC_UI_T_SETUP_EYE_CARE_HINT), 12, UI_MUTED);

        /* 4. Bedtime Toggle */
        draw_text(pixels, stride, 676, 500, ptc_ui_text(PTC_UI_T_SETUP_BEDTIME_LABEL), 14, UI_MUTED);
        bool bed_on = model->setup_bedtime_enabled || model->draft_bedtime_policy.enabled;
        UiRect bed_rect = to_uirect(ptc_ui_setup_bedtime_rect());
        bool bed_focused = model->setup_focus == 5;
        fill_round_rect(pixels, stride, bed_rect, 10, bed_focused ? UI_ACCENT_SOFT : UI_SURFACE);
        draw_rect_outline(pixels, stride, bed_rect, 10, (bed_focused || bed_on) ? 2 : 1, bed_focused ? UI_ACCENT : (bed_on ? UI_ACCENT : UI_BORDER));
        if (bed_focused) draw_focus_ring(pixels, stride, bed_rect, 10);
        draw_text(pixels, stride, bed_rect.x + 16, bed_rect.y + 29, ptc_ui_text(PTC_UI_T_SETUP_BEDTIME_DEFAULT_PARAMS), 15, UI_INK);
        UiRect bed_sw = {bed_rect.x + bed_rect.width - 66, bed_rect.y + (bed_rect.height - 24) / 2, 50, 24};
        draw_toggle_switch(pixels, stride, bed_sw, bed_on, false, false, NULL, NULL);
        const char *bed_st = bed_on ? ptc_ui_text(PTC_UI_T_ENABLED_2) : ptc_ui_text(PTC_UI_T_DISABLED);
        int bed_st_w = measure_text(bed_st, 14);
        draw_text(pixels, stride, bed_sw.x - bed_st_w - 10, bed_rect.y + 29, bed_st, 14, bed_on ? UI_SUCCESS : UI_MUTED);
        draw_text(pixels, stride, 676, 580, ptc_ui_text(PTC_UI_T_SETUP_BEDTIME_HINT), 12, UI_MUTED);
    } else {
        /* Step 3 stacks two cards whose rows are measured, so a two-line English
           notice does not collide with the row above or leave a dead band below.
           Card 1: Configuration Summary (Y=185..), Card 2: Management & Protection. */
        const int card_x = 74, card_w = 1132;
        const int inner_x = 94, inner_w = 1092;
        const int column_x = 656, column_w = 530;
        const int left_w = column_x - inner_x - 36;

        snprintf(text, sizeof(text), "HOS %s  |  AMS %s  |  %s",
            model->environment_available && model->environment_hos[0] ? model->environment_hos : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
            model->environment_available && model->environment_atmosphere_version[0] ? model->environment_atmosphere_version : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
            model->environment_available && model->environment_model[0] ?
                (strcmp(model->environment_model, "mariko-oled") == 0 ? "OLED" : model->environment_model) : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
        const char *reference = ptc_ui_text(ptc_ui_setup_reference_matches(model)
            ? PTC_UI_T_SETUP_REFERENCE_MATCH : PTC_UI_T_SETUP_REFERENCE_UNKNOWN);
        const char *pctl = ptc_ui_text(!model->restriction_enabled_available ? PTC_UI_T_SETUP_PCTL_UNKNOWN :
            model->restriction_enabled ? PTC_UI_T_SETUP_PCTL_ON : PTC_UI_T_SETUP_PCTL_OFF);
        const char *pin = ptc_ui_text(model->setup_pin_ready ?
            PTC_UI_T_SETUP_PIN_READY : PTC_UI_T_SETUP_PIN_UNCONFIRMED);
        bool eye_on = model->setup_eye_care_enabled || model->draft_eye_care_policy.enabled;
        bool bed_on = model->setup_bedtime_enabled || model->draft_bedtime_policy.enabled;
        const char *eye_head = ptc_ui_text(eye_on ? PTC_UI_T_SETUP_EYE_CARE_ON : PTC_UI_T_SETUP_EYE_CARE_SUMMARY_OFF);
        const char *eye_note = ptc_ui_text(eye_on ? PTC_UI_T_SETUP_EYE_CARE_DEFAULT_PARAMS : PTC_UI_T_SETUP_EYE_CARE_HINT);
        const char *bed_head = ptc_ui_text(bed_on ? PTC_UI_T_SETUP_BEDTIME_ON : PTC_UI_T_SETUP_BEDTIME_SUMMARY_OFF);
        const char *bed_note = ptc_ui_text(bed_on ? PTC_UI_T_SETUP_BEDTIME_DEFAULT_PARAMS : PTC_UI_T_SETUP_BEDTIME_HINT);

        /* Row 1's right column shares its first line between mode and quota, so
           the strings are prepared before measuring the row. */
        char mode_text[64], quota_text[64];
        if (model->status_loaded) {
            ptc_ui_format_today_mode(model, mode_text, sizeof(mode_text));
            ptc_ui_format_quota_remaining(model, quota_text, sizeof(quota_text));
        } else {
            snprintf(mode_text, sizeof(mode_text), "%s", ptc_ui_text(PTC_UI_T_SETUP_STATUS_UNKNOWN));
        }

        /* Each of the three rows is as tall as its taller column; the left column
           is narrower than the right one, so measure with its real width. */
        int heights[3];
        heights[0] = setup_block_height(text, 16, left_w, 2) +
            setup_block_height(reference, 13, left_w, 2);
        {
            const char *right1_note = model->status_loaded ? ptc_ui_text(PTC_UI_T_QUOTA_RULES)
                : ptc_ui_text(PTC_UI_T_THE_STATUS_NEEDS_TO_BE_CONFIRMED_PLEASE);
            int right1 = setup_block_height(mode_text, 16, column_w, 2) +
                setup_block_height(right1_note, 13, column_w, 2);
            if (right1 > heights[0]) heights[0] = right1;
        }
        heights[1] = setup_block_height(pctl, 16, left_w, 2) +
            setup_block_height(ptc_ui_text(PTC_UI_T_SETUP_PCTL_PATH), 13, left_w, 2);
        {
            int right2 = setup_block_height(pin, 16, column_w, 2) +
                setup_block_height(ptc_ui_text(PTC_UI_T_SETUP_PIN_DESC), 13, column_w, 2);
            if (right2 > heights[1]) heights[1] = right2;
        }
        heights[2] = setup_block_height(eye_head, 16, left_w, 2) +
            setup_block_height(eye_note, 13, left_w, 2);
        {
            int right3 = setup_block_height(bed_head, 16, column_w, 2) +
                setup_block_height(bed_note, 13, column_w, 2);
            if (right3 > heights[2]) heights[2] = right3;
        }

        /* Rows are separated by a shared gap. Both cards are measured first, then
           the pair is centred in the 185..630 panel so leftover space splits
           evenly above, between and below instead of pooling at the bottom. */
        const int row_gap = 12;
        const int rows_h = heights[0] + heights[1] + heights[2] + row_gap * 2;
        /* 18px card padding, a title on a shared 40px band, then the rows. The
           band keeps the title clear of the first row in every language. */
        const int card_pad = 18;
        const int title_band = 40;
        const int card1_h = card_pad + title_band + rows_h + card_pad;

        const char *backup_note = ptc_ui_text(PTC_UI_T_SETUP_BACKUP_NOTE);
        const char *check_notice = ptc_ui_text(PTC_UI_T_SETUP_CHECK_NOTICE);
        const char *status_note;
        char status_buf[160];
        bool status_warning = false;
        if (ptc_ui_setup_has_issues(model)) {
            int count = 0;
            for (int i = 0; i < PTC_UI_SETUP_ISSUE_COUNT; ++i) if (model->setup_issue_mask & (1U << i)) ++count;
            unsigned int save_issues = (1U << PTC_UI_SETUP_ISSUE_LANGUAGE) | (1U << PTC_UI_SETUP_ISSUE_THEME) |
                (1U << PTC_UI_SETUP_ISSUE_SHORTCUT) | (1U << PTC_UI_SETUP_ISSUE_PROGRESS) | (1U << PTC_UI_SETUP_ISSUE_DEFAULTS);
            if (count && (model->setup_issue_mask & save_issues))
                snprintf(status_buf, sizeof(status_buf), "%s", ptc_ui_text(PTC_UI_T_SETUP_SAVE_PROBLEM));
            else if (count) snprintf(status_buf, sizeof(status_buf), ptc_ui_text(PTC_UI_T_SETUP_ISSUE_COUNT), count);
            else status_buf[0] = '\0';
            status_note = status_buf;
            status_warning = count > 0;
        } else {
            status_note = ptc_ui_text(model->setup_activation_pending
                ? PTC_UI_T_SETUP_ACTIVATING : PTC_UI_T_SETUP_CONTROLS_INACTIVE);
        }
        /* A page-level save message is the same failure the generic status note
           describes, so it replaces it instead of stacking a second line on top. */
        if (model->message[0]) {
            status_note = model->message;
            status_warning = true;
        }
        /* Only count the 8px separator when another block follows it, so an
           empty status note does not leave a dead band at the card's bottom. */
        int note_blocks = setup_block_height(backup_note, 15, inner_w, 2) + 8 +
            setup_block_height(check_notice, 14, inner_w, 2);
        if (status_note[0]) note_blocks += 8 + setup_block_height(status_note, 14, inner_w, 2);
        const int card2_h = card_pad + title_band + note_blocks + card_pad;

        const int card_gap = 16;
        const int panel_top = 185, panel_bottom = 630;
        int stack = panel_top + (panel_bottom - panel_top - (card1_h + card_gap + card2_h)) / 2;
        if (stack < panel_top) stack = panel_top;

        UiRect sum_card = {card_x, stack, card_w, card1_h};
        fill_round_rect(pixels, stride, sum_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, sum_card, 12, 1, UI_BORDER);
        draw_text(pixels, stride, inner_x, stack + card_pad + 20, ptc_ui_text(PTC_UI_T_SETUP_CARD_SUMMARY), 20, UI_INK);

        int y = stack + card_pad + title_band;
        /* Row 1: Environment & Today's Quota */
        setup_block(pixels, stride, inner_x, y, text, 16, left_w, 2, UI_INK);
        setup_block(pixels, stride, inner_x, y + setup_block_height(text, 16, left_w, 2),
            reference, 13, left_w, 2, UI_MUTED);
        /* Draw the mode through the same helper used for measuring, so a wrapped
           mode string can never overflow the measured row. The quota shares the
           first line and the rule note sits on the second. */
        const int mode_top = y;
        (void)setup_block(pixels, stride, column_x, mode_top, mode_text, 16, column_w, 2,
            model->status_loaded ? UI_INK : UI_WARNING_TEXT);
        if (model->status_loaded) {
            const int quota_w = measure_text(quota_text, 16);
            draw_text(pixels, stride, column_x + column_w - quota_w, mode_top + 16, quota_text, 16, UI_ACCENT);
            draw_text(pixels, stride, column_x, mode_top + setup_line_height(16) + 13, ptc_ui_text(PTC_UI_T_QUOTA_RULES), 13, UI_MUTED);
        } else {
            setup_block(pixels, stride, column_x, mode_top + setup_block_height(mode_text, 16, column_w, 2),
                ptc_ui_text(PTC_UI_T_THE_STATUS_NEEDS_TO_BE_CONFIRMED_PLEASE), 13, column_w, 2, UI_MUTED);
        }
        y += heights[0] + row_gap;
        fill_round_rect(pixels, stride, (UiRect){inner_x, y - row_gap / 2, inner_w, 1}, 0, UI_BORDER);

        /* Row 2: Parental Control & PlayWise PIN */
        setup_block(pixels, stride, inner_x, y, pctl, 16, left_w, 2,
            model->restriction_enabled_available && model->restriction_enabled ? UI_SUCCESS : UI_WARNING_TEXT);
        setup_block(pixels, stride, inner_x, y + setup_block_height(pctl, 16, left_w, 2),
            ptc_ui_text(PTC_UI_T_SETUP_PCTL_PATH), 13, left_w, 2, UI_MUTED);
        setup_block(pixels, stride, column_x, y, pin, 16, column_w, 2,
            model->setup_pin_ready ? UI_SUCCESS : UI_WARNING_TEXT);
        setup_block(pixels, stride, column_x, y + setup_block_height(pin, 16, column_w, 2),
            ptc_ui_text(PTC_UI_T_SETUP_PIN_DESC), 13, column_w, 2, UI_MUTED);
        y += heights[1] + row_gap;
        fill_round_rect(pixels, stride, (UiRect){inner_x, y - row_gap / 2, inner_w, 1}, 0, UI_BORDER);

        /* Row 3: Eye Care & Bedtime status */
        setup_block(pixels, stride, inner_x, y, eye_head, 16, left_w, 2, eye_on ? UI_SUCCESS : UI_MUTED);
        setup_block(pixels, stride, inner_x, y + setup_block_height(eye_head, 16, left_w, 2),
            eye_note, 13, left_w, 2, UI_MUTED);
        setup_block(pixels, stride, column_x, y, bed_head, 16, column_w, 2, bed_on ? UI_SUCCESS : UI_MUTED);
        setup_block(pixels, stride, column_x, y + setup_block_height(bed_head, 16, column_w, 2),
            bed_note, 13, column_w, 2, UI_MUTED);
        y += heights[2];

        /* Card 2: Protection and Safety Notes, also measured top-down. */
        const int note_top = stack + card1_h + card_gap;
        const int note_inner_top = note_top + card_pad;
        UiRect note_card = {card_x, note_top, card_w, card2_h};
        fill_round_rect(pixels, stride, note_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, note_card, 12, 1, UI_BORDER);
        draw_text(pixels, stride, inner_x, note_inner_top + 20, ptc_ui_text(PTC_UI_T_SETUP_CARD_TAKEOVER), 18, UI_INK);
        int draw_y = note_inner_top + title_band;
        draw_y = setup_block(pixels, stride, inner_x, draw_y, backup_note, 15, inner_w, 2, UI_INK) + 8;
        draw_y = setup_block(pixels, stride, inner_x, draw_y, check_notice, 14, inner_w, 2, UI_MUTED) + 8;
        if (status_note[0])
            (void)setup_block(pixels, stride, inner_x, draw_y, status_note, 14, inner_w, 2,
                status_warning ? UI_WARNING_TEXT : UI_MUTED);

        char skip_label[96];
        snprintf(skip_label, sizeof(skip_label), "X  %s", ptc_ui_text(PTC_UI_T_SETUP_SKIP));
        draw_footer_button(pixels, stride, ptc_ui_setup_skip_rect(), skip_label);
        if (model->setup_focus == 1) {
            draw_rect_outline(pixels, stride, to_uirect(ptc_ui_setup_skip_rect()), 12, 3, UI_ACCENT);
        }
    }
    /* Save failures are reported in-page. Step 3 carries them inside its notes
       card; steps 1 and 2 use the free strip between their cards (bottom 618)
       and the stage divider (Y=652) instead of overprinting card content. */
    if (model->message[0] && step != PTC_UI_SETUP_CONFIRM) {
        char fitted[256];
        fit_text(fitted, sizeof(fitted), model->message, 14, 1092);
        draw_text(pixels, stride, 94, 642, fitted, 14, UI_WARNING_TEXT);
    }
    draw_footer_button(pixels, stride, ptc_ui_setup_back_rect(), ptc_ui_text(PTC_UI_T_B_PREVIOUS_STEP));
    char primary_label[96];
    snprintf(primary_label, sizeof(primary_label), "A  %s", ptc_ui_text(step == PTC_UI_SETUP_PREPARE
        ? PTC_UI_T_SETUP_NEXT_PREPARE : step == PTC_UI_SETUP_PARENT ? PTC_UI_T_SETUP_NEXT : PTC_UI_T_SETUP_ACTIVATE));
    draw_footer_button(pixels, stride, ptc_ui_setup_primary_rect(), primary_label);
    if (model->setup_focus == 0) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_setup_primary_rect()), 12, 3, UI_ACCENT);
    }
}

void draw_error(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {214, 148, 852, 444};
    char fitted[192];
    char execution[150];
    draw_header(pixels, stride, ptc_ui_text(PTC_UI_T_OPERATION_INCOMPLETE), ptc_ui_text(PTC_UI_T_PLEASE_CHECK_THE_ERROR_MESSAGE_AND_TRY));
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    fill_round_rect(pixels, stride, (UiRect){254, 194, 64, 64}, 16, UI_DANGER);
    draw_text_center(pixels, stride, (UiRect){254, 194, 64, 64}, "!", 34, UI_ON_ACCENT);
    draw_text(pixels, stride, 342, 214, ptc_ui_text(PTC_UI_T_GRANT_CODE_PROCESSING_FAILED), 28, UI_INK);
    snprintf(
        execution,
        sizeof(execution),
        ptc_ui_text(PTC_UI_T_COMMAND_S_S),
        model->command_name[0] ? model->command_name : (ptc_ui_text(PTC_UI_T_NOT_STARTED)),
        model->transport_label[0] ? model->transport_label : (ptc_ui_text(PTC_UI_T_TRANSPORT_NOT_STARTED)));

    fit_text(fitted, sizeof(fitted), execution, 18, 756);
    draw_text(pixels, stride, 254, 286, fitted, 18, UI_MUTED);
    fit_text(fitted, sizeof(fitted), model->message, 23, 756);
    draw_text(pixels, stride, 254, 342, fitted, 23, UI_MUTED);
    if (model->feedback_detail[0]) {
        fit_text(fitted, sizeof(fitted), model->feedback_detail, 17, 756);
        draw_text(pixels, stride, 254, 390, fitted, 17, UI_DANGER);
    }

    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_retry_rect()), 12, UI_ACCENT);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_retry_rect()),
                    model->error_code == 306 ? ptc_ui_text(PTC_UI_T_A_RETEST) : ptc_ui_text(PTC_UI_T_A_RE_ENTER), 25, UI_ON_ACCENT);
    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 12, UI_RAISED);
    draw_rect_outline(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 12, 1, UI_CONTROL);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_back_rect()), ptc_ui_text(PTC_UI_T_B_RETURN_TO_HOMEPAGE), 25, UI_INK);
}
