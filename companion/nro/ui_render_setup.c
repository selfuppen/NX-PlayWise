#include "ui_render_internal.h"

static void setup_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect,
    const char *label, bool selected, bool primary)
{
    UiRect r = to_uirect(rect);
    fill_round_rect(pixels, stride, r, 10, primary ? UI_ACCENT : UI_RAISED);
    draw_rect_outline(pixels, stride, r, 10, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
    draw_text_center(pixels, stride, r, label, 18, primary ? UI_ON_ACCENT : UI_INK);
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
        /* Card 1: Interface Language */
        UiRect lang_card = {74, 185, 1132, 106};
        fill_round_rect(pixels, stride, lang_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, lang_card, 12, 1, UI_BORDER);
        draw_text(pixels, stride, 94, 203, ptc_ui_text(PTC_UI_T_SETUP_CARD_LANG), 17, UI_INK);
        draw_text(pixels, stride, 200, 204, ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HINT_ZH), 13, UI_MUTED);
        draw_text(pixels, stride, 480, 204, ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HINT_EN), 13, UI_MUTED);
        for (int i = 0; i < 4; ++i) {
            PtcUiRect r = ptc_ui_setup_language_rect(i);
            bool chosen = model->language_preference == (PtcUiLanguagePreference)i;
            fill_round_rect(pixels, stride, to_uirect(r), 10, chosen ? UI_ACCENT_SOFT : UI_SURFACE);
            draw_rect_outline(pixels, stride, to_uirect(r), 10, chosen ? 2 : 1, chosen ? UI_ACCENT : UI_BORDER);
            if (chosen && model->setup_focus == 1) draw_focus_ring(pixels, stride, to_uirect(r), 10);
            draw_text_center(pixels, stride, to_uirect(r), i == 0 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_SYSTEM) :
                i == 1 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HANS) :
                i == 2 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HANT) : "English", 16, chosen ? UI_ACCENT : UI_INK);
        }

        /* Card 2: Environment and Console Time */
        UiRect env_card = {74, 305, 1132, 310};
        fill_round_rect(pixels, stride, env_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, env_card, 12, 1, UI_BORDER);
        draw_text(pixels, stride, 94, 323, ptc_ui_text(PTC_UI_T_SETUP_CARD_ENV), 17, UI_INK);
        snprintf(text, sizeof(text), "HOS %s  |  Atmosphère %s  |  %s",
            model->environment_available && model->environment_hos[0] ? model->environment_hos : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
            model->environment_available && model->environment_atmosphere_version[0] ? model->environment_atmosphere_version : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
            model->environment_available && model->environment_model[0] ?
                (strcmp(model->environment_model, "mariko-oled") == 0 ? "OLED" : model->environment_model) : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
        draw_text(pixels, stride, 94, 349, text, 17, UI_INK);
        draw_text(pixels, stride, 94, 372, ptc_ui_text(PTC_UI_T_SETUP_REFERENCE), 14, UI_MUTED);
        draw_text(pixels, stride, 460, 372, ptc_ui_text(ptc_ui_setup_reference_matches(model)
            ? PTC_UI_T_SETUP_REFERENCE_MATCH : PTC_UI_T_SETUP_REFERENCE_UNKNOWN), 14, UI_MUTED);

        /* Separator line */
        fill_round_rect(pixels, stride, (UiRect){94, 396, 1092, 1}, 0, UI_BORDER);

        /* Parental Control row */
        draw_text(pixels, stride, 94, 417, ptc_ui_text(!model->restriction_enabled_available ? PTC_UI_T_SETUP_PCTL_UNKNOWN :
            model->restriction_enabled ? PTC_UI_T_SETUP_PCTL_ON : PTC_UI_T_SETUP_PCTL_OFF), 18,
            model->restriction_enabled_available && model->restriction_enabled ? UI_SUCCESS : UI_WARNING);
        setup_button(pixels, stride, ptc_ui_setup_pctl_help_rect(), ptc_ui_text(PTC_UI_T_SETUP_PCTL_HELP_BUTTON),
            model->setup_focus == 2, false);
        draw_text(pixels, stride, 94, 442, ptc_ui_text(PTC_UI_T_SETUP_PAUSE_OVERLAY), 14, UI_MUTED);

        /* Separator line */
        fill_round_rect(pixels, stride, (UiRect){94, 468, 1092, 1}, 0, UI_BORDER);

        /* Console Time & Direct Time Guide */
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
        draw_text(pixels, stride, 94, 506, text, 24, UI_ACCENT);
        draw_wrapped_text(pixels, stride, 340, 488, ptc_ui_text(PTC_UI_T_SETUP_TIME_HINT), 14, 840, 20, 1, UI_INK);
        draw_wrapped_text(pixels, stride, 340, 514, ptc_ui_text(PTC_UI_T_SETUP_TIME_INSTRUCTIONS), 13, 840, 20, 2, UI_MUTED);
        if (model->setup_focus == 3) {
            draw_focus_ring(pixels, stride, (UiRect){90, 480, 1100, 68}, 8);
        }
    } else if (step == PTC_UI_SETUP_PARENT) {
        /* Left Card: Security & PIN */
        UiRect sec_card = {74, 185, 550, 432};
        fill_round_rect(pixels, stride, sec_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, sec_card, 12, 1, UI_BORDER);
        draw_text(pixels, stride, 94, 212, ptc_ui_text(PTC_UI_T_SETUP_CARD_SECURITY), 20, UI_INK);
        draw_wrapped_text(pixels, stride, 94, 240, ptc_ui_text(PTC_UI_T_SETUP_PIN_DESC), 14, 510, 20, 2, UI_MUTED);

        /* Status badge */
        UiRect pin_badge = {94, 286, 510, 36};
        bool pin_ok = model->setup_pin_ready;
        fill_round_rect(pixels, stride, pin_badge, 8, pin_ok ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
        draw_rect_outline(pixels, stride, pin_badge, 8, 1, pin_ok ? UI_SUCCESS : UI_WARNING);
        draw_text_center(pixels, stride, pin_badge, ptc_ui_text(pin_ok ? PTC_UI_T_SETUP_PIN_STATUS_CUSTOM : PTC_UI_T_SETUP_PIN_STATUS_DEFAULT), 15, pin_ok ? UI_SUCCESS : UI_WARNING);

        /* PIN modification button */
        setup_button(pixels, stride, ptc_ui_setup_pin_rect(), ptc_ui_text(PTC_UI_T_X_CLICK_CHANGE_PIN),
            model->setup_focus == 1, model->setup_focus == 1);

        /* Hint note */
        draw_wrapped_text(pixels, stride, 94, 400, ptc_ui_text(PTC_UI_T_ENTER_A_NUMBER_FROM_1_TO_64), 14, 510, 22, 2, UI_MUTED);
        draw_wrapped_text(pixels, stride, 94, 448, ptc_ui_text(PTC_UI_T_ENTER_NEW_1_TO_64_DIGIT_NUMBER), 13, 510, 20, 2, UI_MUTED);

        /* Right Card: Preferences & Feature Toggles */
        UiRect pref_card = {656, 185, 550, 432};
        fill_round_rect(pixels, stride, pref_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, pref_card, 12, 1, UI_BORDER);
        draw_text(pixels, stride, 676, 212, ptc_ui_text(PTC_UI_T_SETUP_CARD_PREFS), 20, UI_INK);

        /* 1. Theme */
        draw_text(pixels, stride, 676, 238, ptc_ui_text(PTC_UI_T_SETUP_THEME_LABEL), 14, UI_MUTED);
        for (int i = 0; i < 3; ++i) {
            bool selected = model->setup_theme_index == i;
            setup_button(pixels, stride, ptc_ui_setup_theme_rect(i), ptc_ui_theme_preference_label((PtcUiThemePreference)i),
                selected, false);
            if (selected && model->setup_focus == 2)
                draw_focus_ring(pixels, stride, to_uirect(ptc_ui_setup_theme_rect(i)), 10);
        }

        /* 2. Shortcut */
        draw_text(pixels, stride, 676, 298, ptc_ui_text(PTC_UI_T_SETUP_SHORTCUT_LABEL), 14, UI_MUTED);
        snprintf(text, sizeof(text), "%s: %s  [%s]", ptc_ui_text(PTC_UI_T_SETUP_SHORTCUT_LABEL),
            model->custom_shortcut_label[0] ? model->custom_shortcut_label : "(-)",
            ptc_ui_text(PTC_UI_T_SETUP_SHORTCUT_CHANGE));
        setup_button(pixels, stride, ptc_ui_setup_shortcut_rect(), text, model->setup_focus == 3, false);

        /* 3. Eye Care Toggle */
        draw_text(pixels, stride, 676, 374, ptc_ui_text(PTC_UI_T_SETUP_EYE_CARE_LABEL), 14, UI_MUTED);
        bool eye_on = model->setup_eye_care_enabled || model->draft_eye_care_policy.enabled;
        setup_button(pixels, stride, ptc_ui_setup_eye_care_rect(),
            ptc_ui_text(eye_on ? PTC_UI_T_SETUP_EYE_CARE_ON : PTC_UI_T_SETUP_EYE_CARE_OFF),
            model->setup_focus == 4, eye_on);
        draw_text(pixels, stride, 676, 440, ptc_ui_text(PTC_UI_T_SETUP_EYE_CARE_HINT), 13, UI_MUTED);

        /* 4. Bedtime Toggle */
        draw_text(pixels, stride, 676, 468, ptc_ui_text(PTC_UI_T_SETUP_BEDTIME_LABEL), 14, UI_MUTED);
        bool bed_on = model->setup_bedtime_enabled || model->draft_bedtime_policy.enabled;
        setup_button(pixels, stride, ptc_ui_setup_bedtime_rect(),
            ptc_ui_text(bed_on ? PTC_UI_T_SETUP_BEDTIME_ON : PTC_UI_T_SETUP_BEDTIME_OFF),
            model->setup_focus == 5, bed_on);
        draw_text(pixels, stride, 676, 520, ptc_ui_text(PTC_UI_T_SETUP_BEDTIME_HINT), 13, UI_MUTED);
    } else {
        /* Card 1: Configuration Summary Board */
        UiRect sum_card = {74, 185, 1132, 230};
        fill_round_rect(pixels, stride, sum_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, sum_card, 12, 1, UI_BORDER);
        draw_text(pixels, stride, 94, 212, ptc_ui_text(PTC_UI_T_SETUP_CARD_SUMMARY), 20, UI_INK);

        /* Row 1: Environment & Today's Quota */
        snprintf(text, sizeof(text), "HOS %s | AMS %s | %s",
            model->environment_available && model->environment_hos[0] ? model->environment_hos : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
            model->environment_available && model->environment_atmosphere_version[0] ? model->environment_atmosphere_version : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
            model->environment_available && model->environment_model[0] ?
                (strcmp(model->environment_model, "mariko-oled") == 0 ? "OLED" : model->environment_model) : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
        draw_text(pixels, stride, 94, 246, text, 16, UI_INK);
        draw_text(pixels, stride, 94, 268, ptc_ui_text(ptc_ui_setup_reference_matches(model)
            ? PTC_UI_T_SETUP_REFERENCE_MATCH : PTC_UI_T_SETUP_REFERENCE_UNKNOWN), 13, UI_MUTED);

        if (model->status_loaded) {
            ptc_ui_format_today_mode(model, text, sizeof(text));
            draw_text(pixels, stride, 676, 246, text, 16, UI_INK);
            ptc_ui_format_quota_remaining(model, text, sizeof(text));
            draw_text(pixels, stride, 920, 246, text, 16, UI_ACCENT);
        } else {
            draw_text(pixels, stride, 676, 246, ptc_ui_text(PTC_UI_T_SETUP_STATUS_UNKNOWN), 16, UI_WARNING);
        }

        /* Row 2: Parental Control & PlayWise PIN */
        draw_text(pixels, stride, 94, 304, ptc_ui_text(!model->restriction_enabled_available ? PTC_UI_T_SETUP_PCTL_UNKNOWN :
            model->restriction_enabled ? PTC_UI_T_SETUP_PCTL_ON : PTC_UI_T_SETUP_PCTL_OFF), 16,
            model->restriction_enabled_available && model->restriction_enabled ? UI_SUCCESS : UI_WARNING);
        draw_text(pixels, stride, 676, 304, ptc_ui_text(model->setup_pin_ready ?
            PTC_UI_T_SETUP_PIN_READY : PTC_UI_T_SETUP_PIN_UNCONFIRMED), 16, model->setup_pin_ready ? UI_SUCCESS : UI_WARNING);

        /* Row 3: Eye Care & Bedtime toggles status */
        bool eye_on = model->setup_eye_care_enabled || model->draft_eye_care_policy.enabled;
        draw_text(pixels, stride, 94, 348, ptc_ui_text(eye_on ? PTC_UI_T_SETUP_EYE_CARE_ON : PTC_UI_T_SETUP_EYE_CARE_SUMMARY_OFF),
            16, eye_on ? UI_SUCCESS : UI_MUTED);
        bool bed_on = model->setup_bedtime_enabled || model->draft_bedtime_policy.enabled;
        draw_text(pixels, stride, 676, 348, ptc_ui_text(bed_on ? PTC_UI_T_SETUP_BEDTIME_ON : PTC_UI_T_SETUP_BEDTIME_SUMMARY_OFF),
            16, bed_on ? UI_SUCCESS : UI_MUTED);

        /* Card 2: Protection and Safety Notes */
        UiRect note_card = {74, 428, 1132, 185};
        fill_round_rect(pixels, stride, note_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, note_card, 12, 1, UI_BORDER);
        draw_text(pixels, stride, 94, 452, ptc_ui_text(PTC_UI_T_SETUP_CARD_TAKEOVER), 18, UI_INK);
        draw_wrapped_text(pixels, stride, 94, 478, ptc_ui_text(PTC_UI_T_SETUP_BACKUP_NOTE), 15, 1092, 22, 2, UI_INK);
        draw_wrapped_text(pixels, stride, 94, 528, ptc_ui_text(PTC_UI_T_SETUP_CHECK_NOTICE), 14, 1092, 20, 1, UI_MUTED);
        if (ptc_ui_setup_has_issues(model)) {
            int count = 0;
            for (int i = 0; i < PTC_UI_SETUP_ISSUE_COUNT; ++i) if (model->setup_issue_mask & (1U << i)) ++count;
            if (count) {
                unsigned int save_issues = (1U << PTC_UI_SETUP_ISSUE_LANGUAGE) | (1U << PTC_UI_SETUP_ISSUE_THEME) |
                    (1U << PTC_UI_SETUP_ISSUE_SHORTCUT) | (1U << PTC_UI_SETUP_ISSUE_PROGRESS) | (1U << PTC_UI_SETUP_ISSUE_DEFAULTS);
                if (model->setup_issue_mask & save_issues)
                    snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_SETUP_SAVE_PROBLEM));
                else snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_SETUP_ISSUE_COUNT), count);
                draw_text(pixels, stride, 94, 552, text, 15, UI_WARNING);
            }
        } else {
            draw_wrapped_text(pixels, stride, 94, 552, ptc_ui_text(model->setup_activation_pending
                ? PTC_UI_T_SETUP_ACTIVATING : PTC_UI_T_SETUP_CONTROLS_INACTIVE), 14, 1092, 20, 1, UI_MUTED);
        }
        setup_button(pixels, stride, ptc_ui_setup_skip_rect(), ptc_ui_text(PTC_UI_T_SETUP_SKIP), model->setup_focus == 1, false);
    }
    if (model->message[0]) draw_wrapped_text(pixels, stride, 94, 589, model->message, 16, 1092, 21, 2, UI_WARNING);
    setup_button(pixels, stride, ptc_ui_setup_back_rect(), ptc_ui_text(PTC_UI_T_B_PREVIOUS_STEP), false, false);
    setup_button(pixels, stride, ptc_ui_setup_primary_rect(), ptc_ui_text(step == PTC_UI_SETUP_PREPARE
        ? PTC_UI_T_SETUP_NEXT_PREPARE : step == PTC_UI_SETUP_PARENT ? PTC_UI_T_SETUP_NEXT : PTC_UI_T_SETUP_ACTIVATE),
        model->setup_focus == 0, true);
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
