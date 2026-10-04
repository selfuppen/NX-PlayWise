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
        draw_text(pixels, stride, 94, 200, ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_BILINGUAL), 22, UI_INK);
        draw_text(pixels, stride, 94, 224, ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HINT_ZH), 16, UI_MUTED);
        draw_text(pixels, stride, 94, 246, ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HINT_EN), 16, UI_MUTED);
        for (int i = 0; i < 4; ++i) {
            PtcUiRect r = ptc_ui_setup_language_rect(i);
            bool chosen = model->language_preference == (PtcUiLanguagePreference)i;
            fill_round_rect(pixels, stride, to_uirect(r), 10, chosen ? UI_ACCENT_SOFT : UI_RAISED);
            draw_rect_outline(pixels, stride, to_uirect(r), 10, chosen ? 2 : 1, chosen ? UI_ACCENT : UI_BORDER);
            if (chosen && model->setup_focus == 1) draw_focus_ring(pixels, stride, to_uirect(r), 10);
            draw_text_center(pixels, stride, to_uirect(r), i == 0 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_SYSTEM) :
                i == 1 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HANS) :
                i == 2 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HANT) : "English", 16, chosen ? UI_ACCENT : UI_INK);
        }
        snprintf(text, sizeof(text), "HOS %s  |  Atmosphère %s  |  %s",
            model->environment_available && model->environment_hos[0] ? model->environment_hos : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
            model->environment_available && model->environment_atmosphere_version[0] ? model->environment_atmosphere_version : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
            model->environment_available && model->environment_model[0] ?
                (strcmp(model->environment_model, "mariko-oled") == 0 ? "OLED" : model->environment_model) : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
        draw_text(pixels, stride, 94, 332, text, 18, UI_INK);
        draw_text(pixels, stride, 94, 358, ptc_ui_text(PTC_UI_T_SETUP_REFERENCE), 18, UI_MUTED);
        draw_text(pixels, stride, 94, 382, ptc_ui_text(ptc_ui_setup_reference_matches(model)
            ? PTC_UI_T_SETUP_REFERENCE_MATCH : PTC_UI_T_SETUP_REFERENCE_UNKNOWN), 16, UI_MUTED);
        draw_text(pixels, stride, 94, 416, ptc_ui_text(!model->restriction_enabled_available ? PTC_UI_T_SETUP_PCTL_UNKNOWN :
            model->restriction_enabled ? PTC_UI_T_SETUP_PCTL_ON : PTC_UI_T_SETUP_PCTL_OFF), 19,
            model->restriction_enabled_available && model->restriction_enabled ? UI_SUCCESS : UI_WARNING);
        draw_text(pixels, stride, 94, 444, ptc_ui_text(PTC_UI_T_SETUP_PAUSE_OVERLAY), 18, UI_INK);
        if (!model->restriction_enabled_available || !model->restriction_enabled)
            draw_text(pixels, stride, 94, 469, ptc_ui_text(PTC_UI_T_SETUP_PCTL_PATH), 16, UI_MUTED);
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
        draw_text(pixels, stride, 94, 508, text, 22, UI_ACCENT);
        setup_button(pixels, stride, ptc_ui_setup_time_help_rect(), ptc_ui_text(PTC_UI_T_SETUP_TIME_HELP),
            model->setup_focus == 2, false);
        draw_wrapped_text(pixels, stride, 94, 534, ptc_ui_text(model->setup_time_help
            ? PTC_UI_T_SETUP_TIME_INSTRUCTIONS : PTC_UI_T_SETUP_TIME_HINT), 16, 1092, 22, 2, UI_MUTED);
    } else if (step == PTC_UI_SETUP_PARENT) {
        draw_text(pixels, stride, 94, 218, ptc_ui_text(PTC_UI_T_SETUP_PARENT), 28, UI_INK);
        draw_wrapped_text(pixels, stride, 94, 248, ptc_ui_text(PTC_UI_T_SETUP_PASSWORD_NOTE), 19, 1092, 24, 1, UI_WARNING);
        setup_button(pixels, stride, ptc_ui_setup_pin_rect(), ptc_ui_text(PTC_UI_T_X_CLICK_CHANGE_PIN),
            model->setup_focus == 1, false);
        draw_text(pixels, stride, 94, 360, ptc_ui_text(PTC_UI_T_USED_FOR_PARENT_ZONE_SEPARATE_FROM_NINTENDO), 16, UI_MUTED);
        setup_button(pixels, stride, ptc_ui_setup_more_rect(), ptc_ui_text(PTC_UI_T_SETUP_MORE), model->setup_focus == 2, false);
        if (model->setup_more) {
            for (int i = 0; i < 3; ++i)
                setup_button(pixels, stride, ptc_ui_setup_theme_rect(i), ptc_ui_theme_preference_label((PtcUiThemePreference)i),
                    model->setup_theme_index == i, false);
            if (model->setup_focus == 3)
                draw_focus_ring(pixels, stride, to_uirect(ptc_ui_setup_theme_rect(model->setup_theme_index)), 10);
            setup_button(pixels, stride, ptc_ui_setup_shortcut_rect(), ptc_ui_text(PTC_UI_T_PARENT_AREA_SHORTCUT_KEY_MANAGEMENT),
                model->setup_focus == 4, false);
        } else draw_wrapped_text(pixels, stride, 94, 452, ptc_ui_text(PTC_UI_T_SETUP_DEFAULT_PREFS), 20, 1092, 30, 2, UI_MUTED);
    } else {
        draw_text(pixels, stride, 94, 215, ptc_ui_text(PTC_UI_T_SETUP_CONFIRM), 28, UI_INK);
        if (model->status_loaded) {
            ptc_ui_format_today_mode(model, text, sizeof(text));
            draw_text(pixels, stride, 94, 252, text, 21, UI_INK);
            ptc_ui_format_quota_remaining(model, text, sizeof(text));
            draw_text(pixels, stride, 750, 252, text, 21, UI_ACCENT);
        } else draw_text(pixels, stride, 94, 252, ptc_ui_text(PTC_UI_T_SETUP_STATUS_UNKNOWN), 21, UI_WARNING);
        draw_text(pixels, stride, 94, 287, ptc_ui_text(ptc_ui_setup_reference_matches(model)
            ? PTC_UI_T_SETUP_REFERENCE_MATCH : PTC_UI_T_SETUP_REFERENCE_UNKNOWN), 16, UI_MUTED);
        draw_text(pixels, stride, 94, 313, ptc_ui_text(!model->restriction_enabled_available ? PTC_UI_T_SETUP_PCTL_UNKNOWN :
            model->restriction_enabled ? PTC_UI_T_SETUP_PCTL_ON : PTC_UI_T_SETUP_PCTL_OFF), 16, UI_WARNING);
        draw_text(pixels, stride, 750, 313, ptc_ui_text(model->setup_pin_ready ?
            PTC_UI_T_SETUP_PIN_READY : PTC_UI_T_SETUP_PIN_UNCONFIRMED), 16, model->setup_pin_ready ? UI_MUTED : UI_WARNING);
        draw_wrapped_text(pixels, stride, 94, 345, ptc_ui_text(PTC_UI_T_SETUP_BACKUP_NOTE), 18, 1092, 24, 2, UI_INK);
        draw_wrapped_text(pixels, stride, 94, 394, ptc_ui_text(PTC_UI_T_SETUP_CHECK_NOTICE), 16, 1092, 22, 1, UI_MUTED);
        if (ptc_ui_setup_has_issues(model)) {
            int count = 0;
            for (int i = 0; i < PTC_UI_SETUP_ISSUE_COUNT; ++i) if (model->setup_issue_mask & (1U << i)) ++count;
            if (count) {
                unsigned int save_issues = (1U << PTC_UI_SETUP_ISSUE_LANGUAGE) | (1U << PTC_UI_SETUP_ISSUE_THEME) |
                    (1U << PTC_UI_SETUP_ISSUE_SHORTCUT) | (1U << PTC_UI_SETUP_ISSUE_PROGRESS) | (1U << PTC_UI_SETUP_ISSUE_DEFAULTS);
                if (model->setup_issue_mask & save_issues)
                    snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_SETUP_SAVE_PROBLEM));
                else snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_SETUP_ISSUE_COUNT), count);
                draw_text(pixels, stride, 94, 435, text, 18, UI_WARNING);
            }
            draw_wrapped_text(pixels, stride, 94, 466, ptc_ui_text(PTC_UI_T_SETUP_DIAGNOSTIC_GUIDE), 18, 1092, 24, 3, UI_WARNING);
        }
        draw_wrapped_text(pixels, stride, 94, 550, ptc_ui_text(model->setup_activation_pending
            ? PTC_UI_T_SETUP_ACTIVATING : PTC_UI_T_SETUP_CONTROLS_INACTIVE), 16, 1092, 22, 1, UI_MUTED);
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
