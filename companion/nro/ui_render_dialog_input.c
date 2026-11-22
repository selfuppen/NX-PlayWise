#include "ui_render_internal.h"

void draw_numpad_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *KEY_LABELS[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", ptc_ui_text(PTC_UI_T_X_BACKSPACE), "0", ptc_ui_text(PTC_UI_T_Y_CLEAR)
    };
    UiRect dialog;
    UiRect display = to_uirect(ptc_ui_numpad_display_rect());
    char shown[32];
    char current[80];
    char duration[64];
    char fitted[160];
    uint16_t entered_minutes = 0;
    bool offline_code = model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE;
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 610);

    if (offline_code) {
        draw_redeem_step_bar(pixels, stride,
                             (UiRect){dialog.x + 34, dialog.y + 74, dialog.width - 68, 34}, 0);
    }

    if ((model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
         model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES ||
         model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
         model->numpad_purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) && model->numpad_text[0]) {
        snprintf(shown, sizeof(shown), ptc_ui_text(PTC_UI_T_S_MIN), model->numpad_text);
    } else if (offline_code && model->numpad_text[0]) {
        ptc_ui_format_code(model->numpad_text, shown, sizeof(shown));
    } else if (offline_code) {
        snprintf(shown, sizeof(shown), "%s", ptc_ui_text(PTC_UI_T_ENTER_CODE));
    } else if (model->numpad_text[0]) {
        snprintf(shown, sizeof(shown), "%s", model->numpad_text);
    } else {
        snprintf(shown, sizeof(shown), "%.*s", model->numpad_max_digits, "________");
    }

    if (offline_code) {
        size_t entered = strlen(model->numpad_text);
        fill_round_rect(pixels, stride, display, 14, UI_RAISED);
        draw_rect_outline(pixels, stride, display, 14, 1, UI_BORDER);
        for (int slot = 0; slot < 8; ++slot) {
            UiRect cell = to_uirect(ptc_ui_code_slot_rect(slot));
            bool filled = (size_t)slot < entered;
            bool current_slot = (size_t)slot == entered && entered < 8U;
            char digit[2] = {'\0', '\0'};
            if (filled) digit[0] = model->numpad_text[slot];
            fill_round_rect(pixels, stride, cell, 10,
                            current_slot ? UI_ACCENT_SOFT : (filled ? UI_SURFACE : UI_PAGE));
            draw_rect_outline(pixels, stride, cell, 10, current_slot ? 3 : 1,
                              current_slot ? UI_ACCENT : (filled ? UI_CONTROL : UI_BORDER));
            draw_text_center(pixels, stride, cell, filled ? digit : "_", 28,
                             filled ? UI_ACCENT : (current_slot ? UI_ACCENT : UI_DISABLED));
        }
        /* Visual dash divider between the two 4-digit groups */
        {
            UiRect s3 = to_uirect(ptc_ui_code_slot_rect(3));
            UiRect s4 = to_uirect(ptc_ui_code_slot_rect(4));
            int mid_x = (s3.x + s3.width + s4.x) / 2;
            int mid_y = display.y + display.height / 2;
            draw_line(pixels, stride, mid_x - 5, mid_y, mid_x + 5, mid_y, 2, UI_MUTED);
        }
        snprintf(current, sizeof(current), ptc_ui_text(PTC_UI_T_ENTER_8_DIGIT_CODE_ENTERED_U_8),
                 (unsigned int)entered);
        duration[0] = '\0';
    } else {
        fill_round_rect(pixels, stride, display, 14, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, display, 14, 2, UI_ACCENT);
        draw_text_center(pixels, stride, display, shown, 30, UI_ACCENT);
        snprintf(current, sizeof(current), ptc_ui_text(PTC_UI_T_CURRENT_U_MIN_RANGE_U_TO_U),
                 (unsigned int)model->numpad_current, (unsigned int)model->numpad_minimum,
                 (unsigned int)model->numpad_maximum);
        if (!ptc_ui_parse_minutes(model->numpad_text, model->numpad_minimum, model->numpad_maximum, &entered_minutes)) {
            entered_minutes = model->numpad_current;
        }
        format_duration(entered_minutes, duration, sizeof(duration));
    }

    fit_text(fitted, sizeof(fitted), current, 15, display.width);
    draw_text(pixels, stride, display.x + 2, display.y + display.height + 22, fitted, 15, UI_MUTED);

    for (index = 0; index < 12; ++index) {
        UiRect key = to_uirect(ptc_ui_numpad_key_rect(index));
        bool selected = index == model->numpad_cursor;
        fill_round_rect(pixels, stride, key, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, key, 12, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
        draw_text_center(pixels, stride, key, KEY_LABELS[index], index == 9 || index == 11 ? 17 : 28,
                         selected ? UI_ACCENT : UI_INK);
    }

    if (model->numpad_error[0]) {
        fit_text(fitted, sizeof(fitted), model->numpad_error, 15, display.width);
        draw_text(pixels, stride, display.x + 2, dialog.y + 506, fitted, 15, UI_DANGER);
    } else {
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_DIRECTIONAL_KEYS_JOYSTICK_SELECTION_A_ENTER_X), 14, display.width);
        draw_text(pixels, stride, display.x + 2, dialog.y + 506, fitted, 14, UI_MUTED);
    }

    /* Right Column: Live Today Balance + Console Date + Safe Redemption Reference */
    {
        UiRect right_col = {dialog.x + 624, dialog.y + 132, dialog.width - 658, 372};
        char remaining[48], played[64], age[64], console_date[64];

        UiRect balance_card = {right_col.x, right_col.y, right_col.width, 138};
        fill_round_rect(pixels, stride, balance_card, 14, UI_RAISED);
        draw_rect_outline(pixels, stride, balance_card, 14, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_CURRENT_REMAINING), 14, balance_card.width - 32);
        draw_text(pixels, stride, balance_card.x + 16, balance_card.y + 26, fitted, 14, UI_MUTED);
        if (fresh && model->unrestricted_today == 1) {
            snprintf(remaining, sizeof(remaining), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        } else {
            format_duration(fresh && model->remaining_available ? model->remaining_minutes : -1,
                            remaining, sizeof(remaining));
        }
        uint32_t rem_accent = time_state_accent(
            fresh && (model->unrestricted_today == 1 || model->remaining_available),
            model->unrestricted_today == 1, model->remaining_minutes);
        fit_text(fitted, sizeof(fitted), remaining, 30, balance_card.width - 32);
        draw_text_bold(pixels, stride, balance_card.x + 16, balance_card.y + 68, fitted, 30, rem_accent);
        if (fresh && model->played_minutes_available && model->played_minutes >= 0) {
            snprintf(played, sizeof(played), ptc_ui_text(PTC_UI_T_PLAYED_D_MIN_TODAY), model->played_minutes);
        } else {
            snprintf(played, sizeof(played), "%s", ptc_ui_text(PTC_UI_T_USED_QUOTA_ESTIMATE_UNAVAILABLE));
        }
        fit_text(fitted, sizeof(fitted), played, 13, balance_card.width - 32);
        draw_text(pixels, stride, balance_card.x + 16, balance_card.y + 96, fitted, 13, UI_MUTED);
        format_status_age(model, age, sizeof(age));
        fit_text(fitted, sizeof(fitted), age, 12, balance_card.width - 32);
        draw_text(pixels, stride, balance_card.x + 16, balance_card.y + 118, fitted, 12, status_age_color(model));

        if (offline_code) {
            UiRect date_card = {right_col.x, right_col.y + 150, right_col.width, 110};
            fill_round_rect(pixels, stride, date_card, 14,
                            model->status_loaded ? UI_ACCENT_SOFT : UI_WARNING_SOFT);
            draw_rect_outline(pixels, stride, date_card, 14, 1,
                              model->status_loaded ? UI_ACCENT : UI_WARNING_BORDER);
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_REDEEM_DATE_CARD_TITLE), 13, date_card.width - 32);
            draw_text(pixels, stride, date_card.x + 16, date_card.y + 24, fitted, 13,
                      model->status_loaded ? UI_ACCENT : UI_WARNING_TEXT);
            ptc_ui_format_console_date(model, console_date, sizeof(console_date));
            fit_text(fitted, sizeof(fitted), console_date, 22, date_card.width - 32);
            draw_text_bold(pixels, stride, date_card.x + 16, date_card.y + 58, fitted, 22, UI_INK);
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_REDEEM_DATE_CARD_HINT), 13, date_card.width - 32);
            draw_text(pixels, stride, date_card.x + 16, date_card.y + 88, fitted, 13, UI_MUTED);

            UiRect safe_card = {right_col.x, right_col.y + 272, right_col.width, 100};
            fill_round_rect(pixels, stride, safe_card, 14, UI_PAGE);
            draw_rect_outline(pixels, stride, safe_card, 14, 1, UI_BORDER);
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_REDEEM_SAFE_CARD_TITLE), 14, safe_card.width - 32);
            draw_text_bold(pixels, stride, safe_card.x + 16, safe_card.y + 26, fitted, 14, UI_INK);
            draw_wrapped_text(pixels, stride, safe_card.x + 16, safe_card.y + 50,
                              ptc_ui_text(PTC_UI_T_REDEEM_SAFE_CARD_BODY), 13,
                              safe_card.width - 32, 19, 3, UI_MUTED);
        } else if (duration[0]) {
            UiRect dur_card = {right_col.x, right_col.y + 150, right_col.width, 110};
            fill_round_rect(pixels, stride, dur_card, 14, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, dur_card, 14, 1, UI_ACCENT);
            draw_text_center(pixels, stride, dur_card, duration, 26, UI_ACCENT);
        }
    }

    draw_overlay_actions(pixels, stride, model,
                         offline_code ? ptc_ui_text(PTC_UI_T_PREVIEW_TODAY_S_GRANT) : ptc_ui_text(PTC_UI_T_DONE));
}

void draw_pin_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog = to_uirect(ptc_ui_pin_dialog_rect());
    char mask[PTC_UI_PIN_MAX_DIGITS + 1];
    char count[64];
    char fitted[160];
    int row;
    size_t len = strlen(model->pin_text);
    bool has_error = model->pin_error[0] != '\0';
    ptc_ui_pin_format_mask(model, mask, sizeof(mask));
    draw_dialog_shell(pixels, stride, model, &dialog, 1080, 620);

    /* Top-right rule badge */
    {
        const char *rule_badge = ptc_ui_text(PTC_UI_T_PIN_RULE_BADGE);
        int bw = measure_text(rule_badge, 12) + 24;
        if (bw > 280) bw = 280;
        UiRect badge = {dialog.x + dialog.width - 34 - bw, dialog.y + 26, bw, 28};
        fill_round_rect(pixels, stride, badge, 14, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, badge, 14, 1, UI_ACCENT);
        fit_text(fitted, sizeof(fitted), rule_badge, 12, badge.width - 14);
        draw_text_center(pixels, stride, badge, fitted, 12, UI_ACCENT);
    }

    /* Single-line subtitle under dialog title */
    if (model->pin_guide[0]) {
        fit_text(fitted, sizeof(fitted), model->pin_guide, 15, dialog.width - 68);
        draw_text(pixels, stride, dialog.x + 34, dialog.y + 84, fitted, 15, UI_MUTED);
    }

    /* Full-width Top Mask Bar */
    {
        UiRect display = {dialog.x + 34, dialog.y + 104, dialog.width - 68, 66};
        snprintf(count, sizeof(count), ptc_ui_text(PTC_UI_T_U_DIGITS_ENTERED), (unsigned int)len);
        int cw = measure_text(count, 13) + 24;
        if (cw > 180) cw = 180;
        UiRect count_pill = {display.x + display.width - 16 - cw, display.y + 18, cw, 30};
        UiRect mask_area = {display.x + 20, display.y, count_pill.x - display.x - 32, display.height};

        fill_round_rect(pixels, stride, display, 14, has_error ? UI_DANGER_SOFT : UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, display, 14, 2, has_error ? UI_DANGER : UI_ACCENT);

        if (len > 0) {
            fit_text(fitted, sizeof(fitted), mask, 28, mask_area.width);
            draw_text_center(pixels, stride, mask_area, fitted, 28, has_error ? UI_DANGER : UI_ACCENT);
        } else {
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THE_INPUT_CONTENT_IS_ONLY_DISPLAYED_AS), 17, mask_area.width);
            draw_text_center(pixels, stride, mask_area, fitted, 17, UI_MUTED);
        }

        fill_round_rect(pixels, stride, count_pill, 15, UI_SURFACE);
        draw_rect_outline(pixels, stride, count_pill, 15, 1, has_error ? UI_DANGER_BORDER : UI_BORDER);
        fit_text(fitted, sizeof(fitted), count, 13, count_pill.width - 12);
        draw_text_center(pixels, stride, count_pill, fitted, 13, has_error ? UI_DANGER : UI_INK);
    }

    /* Inline error or security tip strip */
    {
        const char *tip = has_error ? model->pin_error : ptc_ui_text(PTC_UI_T_PIN_SECURITY_RULE_LINE);
        fit_text(fitted, sizeof(fitted), tip, 14, dialog.width - 68);
        draw_text(pixels, stride, dialog.x + 38, dialog.y + 192, fitted, 14, has_error ? UI_DANGER : UI_MUTED);
    }

    /* Left Panel: Joy-Con Blind Input Diagram + Wider Legend */
    {
        UiRect left_card = {dialog.x + 34, dialog.y + 206, 504, 316};
        int i;
        bool is_dark = (g_theme.resolved == PTC_UI_RESOLVED_DARK);

        fill_round_rect(pixels, stride, left_card, 14, UI_RAISED);
        draw_rect_outline(pixels, stride, left_card, 14, 1, UI_BORDER);

        const char *blind_badge = ptc_ui_text(PTC_UI_T_PIN_BLIND_BADGE);
        int bw = measure_text(blind_badge, 12) + 20;
        if (bw > 150) bw = 150;
        UiRect bpill = {left_card.x + left_card.width - 16 - bw, left_card.y + 12, bw, 24};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_CONTROLLER_INPUT_SIGNAL), 16,
                 bpill.x - (left_card.x + 16) - 10);
        draw_text_bold(pixels, stride, left_card.x + 16, left_card.y + 30, fitted, 16, UI_INK);
        fill_round_rect(pixels, stride, bpill, 12, UI_ACCENT_SOFT);
        fit_text(fitted, sizeof(fitted), blind_badge, 12, bpill.width - 10);
        draw_text_center(pixels, stride, bpill, fitted, 12, UI_ACCENT);

        uint32_t left_jc_bg = is_dark ? UI_RGB(0x0C3048) : UI_RGB(0xD4EEFA);
        uint32_t left_jc_border = is_dark ? UI_RGB(0x00AEE6) : UI_RGB(0x009BD0);
        uint32_t left_jc_shoulder = is_dark ? UI_RGB(0x0094C4) : UI_RGB(0x0088B8);
        uint32_t left_stick_accent = is_dark ? UI_RGB(0x00C4FA) : UI_RGB(0x009BD0);

        uint32_t right_jc_bg = is_dark ? UI_RGB(0x441820) : UI_RGB(0xFCE4E6);
        uint32_t right_jc_border = is_dark ? UI_RGB(0xFF4554) : UI_RGB(0xEB3646);
        uint32_t right_jc_shoulder = is_dark ? UI_RGB(0xDE3646) : UI_RGB(0xD42838);
        uint32_t right_key_active_bg = is_dark ? UI_RGB(0x601C26) : UI_RGB(0xFAD0D4);
        uint32_t right_key_active_border = is_dark ? UI_RGB(0xFF4554) : UI_RGB(0xEB3646);
        uint32_t right_key_active_text = is_dark ? UI_RGB(0xFF707E) : UI_RGB(0xD42838);

        int jc_w = 122;
        int jc_h = 224;
        int left_jc_x = left_card.x + 18;
        int left_jc_y = left_card.y + 56;
        int right_jc_x = left_card.x + left_card.width - 18 - jc_w;
        int right_jc_y = left_card.y + 56;

        int stick_x = left_jc_x + jc_w / 2;
        int stick_y = left_jc_y + 66;
        int dpad_cx = left_jc_x + jc_w / 2;
        int dpad_cy = left_jc_y + 156;
        int buttons_x = right_jc_x + jc_w / 2;
        int buttons_y = right_jc_y + 66;
        int rstick_x = right_jc_x + jc_w / 2;
        int rstick_y = right_jc_y + 156;

        /* Left Joy-Con (L) */
        fill_round_rect(pixels, stride, (UiRect){left_jc_x, left_jc_y, jc_w, jc_h}, 24, left_jc_bg);
        draw_rect_outline(pixels, stride, (UiRect){left_jc_x, left_jc_y, jc_w, jc_h}, 24, 2, left_jc_border);
        fill_round_rect(pixels, stride, (UiRect){left_jc_x + 10, left_jc_y - 9, 64, 9}, 4, left_jc_shoulder);
        draw_text_center(pixels, stride, (UiRect){left_jc_x + 10, left_jc_y - 9, 64, 9}, "L", 9, UI_PAGE);
        fill_round_rect(pixels, stride, (UiRect){left_jc_x + jc_w - 22, left_jc_y + 12, 12, 4}, 2, UI_CONTROL);

        draw_circle_outline(pixels, stride, stick_x, stick_y, 35, 1, UI_BORDER);
        draw_circle_outline(pixels, stride, stick_x, stick_y, 20, 2, left_stick_accent);
        fill_round_rect(pixels, stride, (UiRect){stick_x - 14, stick_y - 14, 28, 28}, 14, UI_SURFACE);
        draw_circle_outline(pixels, stride, stick_x, stick_y, 14, 1, UI_CONTROL);

        for (i = 1; i <= 8; ++i) {
            int dx = 0, dy = 0;
            char label[4];
            switch (i) {
            case 1: dy = -39; break;
            case 2: dx = 28; dy = -28; break;
            case 3: dx = 39; break;
            case 4: dx = 28; dy = 28; break;
            case 5: dy = 39; break;
            case 6: dx = -28; dy = 28; break;
            case 7: dx = -39; break;
            case 8: dx = -28; dy = -28; break;
            }
            snprintf(label, sizeof(label), "%d", i);
            draw_text_center(pixels, stride, (UiRect){stick_x + dx - 9, stick_y + dy - 9, 18, 18},
                             label, 14, left_stick_accent);
        }

        for (i = 0; i < 4; ++i) {
            int d = 15;
            int dx = i == 1 ? d : (i == 3 ? -d : 0);
            int dy = i == 0 ? -d : (i == 2 ? d : 0);
            const char *arrow = i == 0 ? "^" : (i == 1 ? ">" : (i == 2 ? "v" : "<"));
            UiRect btn = {dpad_cx + dx - 8, dpad_cy + dy - 8, 16, 16};
            fill_round_rect(pixels, stride, btn, 8, UI_SURFACE);
            draw_circle_outline(pixels, stride, dpad_cx + dx, dpad_cy + dy, 8, 1, UI_CONTROL);
            draw_text_center(pixels, stride, btn, arrow, 10, UI_MUTED);
        }
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THE_CROSS_KEY_IS_THE_SAME_AS), 11, jc_w - 10);
        draw_text_center(pixels, stride, (UiRect){left_jc_x + 5, left_jc_y + 186, jc_w - 10, 16},
                         fitted, 11, UI_MUTED);

        /* Right Joy-Con (R) */
        fill_round_rect(pixels, stride, (UiRect){right_jc_x, right_jc_y, jc_w, jc_h}, 24, right_jc_bg);
        draw_rect_outline(pixels, stride, (UiRect){right_jc_x, right_jc_y, jc_w, jc_h}, 24, 2, right_jc_border);
        fill_round_rect(pixels, stride, (UiRect){right_jc_x + jc_w - 74, right_jc_y - 9, 64, 9}, 4, right_jc_shoulder);
        draw_text_center(pixels, stride, (UiRect){right_jc_x + jc_w - 74, right_jc_y - 9, 64, 9}, "R", 9, UI_PAGE);
        draw_line(pixels, stride, right_jc_x + 12, right_jc_y + 14, right_jc_x + 22, right_jc_y + 14, 2, UI_CONTROL);
        draw_line(pixels, stride, right_jc_x + 17, right_jc_y + 9, right_jc_x + 17, right_jc_y + 19, 2, UI_CONTROL);

        for (i = 0; i < 4; ++i) {
            int d = 25;
            int dx = i == 1 ? d : (i == 3 ? -d : 0);
            int dy = i == 0 ? -d : (i == 2 ? d : 0);
            const char *letter = i == 0 ? "X" : (i == 1 ? "A" : (i == 2 ? "B" : "Y"));
            char label[12];
            UiRect key = {buttons_x + dx - 14, buttons_y + dy - 14, 28, 28};
            bool is_digit = (i == 0 || i == 3);
            fill_round_rect(pixels, stride, key, 14, is_digit ? right_key_active_bg : UI_SURFACE);
            draw_circle_outline(pixels, stride, buttons_x + dx, buttons_y + dy, 14, 2,
                                is_digit ? right_key_active_border : UI_CONTROL);
            if (i == 0) snprintf(label, sizeof(label), "X=0");
            else if (i == 3) snprintf(label, sizeof(label), "Y=9");
            else snprintf(label, sizeof(label), "%s", letter);
            draw_text_center(pixels, stride, key, label, is_digit ? 10 : 13,
                             is_digit ? right_key_active_text : UI_MUTED);
        }

        draw_circle_outline(pixels, stride, rstick_x, rstick_y, 18, 2, UI_CONTROL);
        fill_round_rect(pixels, stride, (UiRect){rstick_x - 13, rstick_y - 13, 26, 26}, 13, UI_SURFACE);
        draw_circle_outline(pixels, stride, rstick_x, rstick_y, 13, 1, UI_CONTROL);
        draw_text_center(pixels, stride, (UiRect){rstick_x - 13, rstick_y - 13, 26, 26}, "R", 12, UI_MUTED);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_RIGHT_STICK_MAPPING_IS_THE_SAME), 11, jc_w - 10);
        draw_text_center(pixels, stride, (UiRect){right_jc_x + 5, right_jc_y + 186, jc_w - 10, 16},
                         fitted, 11, UI_MUTED);

        /* Spacious Center Legend Card (196px wide, legible 13px rows) */
        UiRect center_guide = {left_jc_x + jc_w + 14, left_card.y + 56,
                               right_jc_x - (left_jc_x + jc_w + 28), 224};
        fill_round_rect(pixels, stride, center_guide, 12, UI_SURFACE);
        draw_rect_outline(pixels, stride, center_guide, 12, 1, UI_BORDER);

        UiRect gbadge = {center_guide.x + 12, center_guide.y + 12, center_guide.width - 24, 24};
        fill_round_rect(pixels, stride, gbadge, 8, UI_ACCENT_SOFT);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_JOY_CON_INPUT), 12, gbadge.width - 10);
        draw_text_center(pixels, stride, gbadge, fitted, 12, UI_ACCENT);

        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_JOYSTICK_1_TO_8), 13, center_guide.width - 20);
        draw_text_center(pixels, stride, (UiRect){center_guide.x + 10, center_guide.y + 44, center_guide.width - 20, 18}, fitted, 13, UI_INK);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_CLOCKWISE), 12, center_guide.width - 20);
        draw_text_center(pixels, stride, (UiRect){center_guide.x + 10, center_guide.y + 64, center_guide.width - 20, 16}, fitted, 12, UI_MUTED);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_X_KEY_0), 14, center_guide.width - 20);
        draw_text_center(pixels, stride, (UiRect){center_guide.x + 10, center_guide.y + 88, center_guide.width - 20, 20}, fitted, 14, right_key_active_border);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_Y_KEY_9), 14, center_guide.width - 20);
        draw_text_center(pixels, stride, (UiRect){center_guide.x + 10, center_guide.y + 110, center_guide.width - 20, 20}, fitted, 14, right_key_active_border);
        draw_line(pixels, stride, center_guide.x + 16, center_guide.y + 138, center_guide.x + center_guide.width - 16, center_guide.y + 138, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ZL_KEY_BACKSPACE), 13, center_guide.width - 20);
        draw_text_center(pixels, stride, (UiRect){center_guide.x + 10, center_guide.y + 148, center_guide.width - 20, 18}, fitted, 13, UI_MUTED);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_KEY_CONFIRM), 13, center_guide.width - 20);
        draw_text_center(pixels, stride, (UiRect){center_guide.x + 10, center_guide.y + 170, center_guide.width - 20, 18}, fitted, 13, UI_INK);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_LONG_PRESS_SWITCH_KEYBOARD), 12, center_guide.width - 20);
        draw_text_center(pixels, stride, (UiRect){center_guide.x + 10, center_guide.y + 194, center_guide.width - 20, 16}, fitted, 12, UI_MUTED);

        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THE_LEFT_AND_RIGHT_JOYSTICKS_HAVE_THE), 12, left_card.width - 32);
        draw_text(pixels, stride, left_card.x + 16, left_card.y + 302, fitted, 12, UI_MUTED);
    }

    /* Right Panel: 3x4 Touch Numeric Keypad */
    {
        UiRect right_card = {dialog.x + 558, dialog.y + 206, 488, 316};
        fill_round_rect(pixels, stride, right_card, 14, UI_RAISED);
        draw_rect_outline(pixels, stride, right_card, 14, 1, UI_BORDER);

        const char *touch_hint = ptc_ui_text(PTC_UI_T_PIN_TOUCH_HINT);
        int thw = measure_text(touch_hint, 12);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_TOUCH_NUMERIC_KEYPAD), 16,
                 right_card.width - thw - 44);
        draw_text_bold(pixels, stride, right_card.x + 18, right_card.y + 30, fitted, 16, UI_INK);
        draw_text(pixels, stride, right_card.x + right_card.width - 18 - thw, right_card.y + 29, touch_hint, 12, UI_MUTED);

        for (row = 0; row < 10; ++row) {
            UiRect key = to_uirect(ptc_ui_pin_key_rect(row));
            bool selected = model->pin_focus == row;
            char label[4];
            snprintf(label, sizeof(label), "%d", row);
            fill_round_rect(pixels, stride, key, 12, selected ? UI_ACCENT_SOFT : UI_SURFACE);
            draw_rect_outline(pixels, stride, key, 12, selected ? 2 : 1, selected ? UI_ACCENT : UI_CONTROL);
            draw_text_center(pixels, stride, key, label, 24, selected ? UI_ACCENT : UI_INK);
        }

        draw_dialog_button(pixels, stride, ptc_ui_pin_backspace_rect(), ptc_ui_text(PTC_UI_T_ZL_BACKSPACE),
                           UI_WARNING_SOFT, UI_WARNING_TEXT, false);
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_pin_backspace_rect()), 12, 1, UI_WARNING_BORDER);

        draw_dialog_button(pixels, stride, ptc_ui_pin_keyboard_rect(), ptc_ui_text(PTC_UI_T_PIN_SYSTEM_KEYBOARD_SHORT),
                           UI_SURFACE, UI_INK, true);
    }

    /* Bottom Action Bar */
    draw_dialog_button(pixels, stride, ptc_ui_pin_cancel_rect(), ptc_ui_text(PTC_UI_T_B_CANCEL),
                       UI_RAISED, UI_INK, true);
    {
        PtcUiRect cancel = ptc_ui_pin_cancel_rect();
        PtcUiRect confirm = ptc_ui_pin_confirm_rect();
        UiRect mid_hint = {cancel.x + cancel.w + 16, cancel.y, confirm.x - (cancel.x + cancel.w) - 32, cancel.h};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SHORT_PRESS_CONFIRM_LONG_PRESS_ABOUT_1), 14, mid_hint.width);
        draw_text_center(pixels, stride, mid_hint, fitted, 14, UI_MUTED);
    }
    draw_dialog_button(pixels, stride, ptc_ui_pin_confirm_rect(), ptc_ui_text(PTC_UI_T_PIN_CONFIRM_SHORT),
                       UI_ACCENT, UI_ON_ACCENT, false);
}


static PtcUiTextId dock_preview_label(PtcUiDockPreviewState state)
{
    switch (state) {
    case PTC_UI_DOCK_PREVIEW_OFF: return PTC_UI_T_DOCK_OFF;
    case PTC_UI_DOCK_PREVIEW_WAIVED: return PTC_UI_T_DOCK_WAIVED;
    case PTC_UI_DOCK_PREVIEW_ALLOWED: return PTC_UI_T_DOCK_WITHIN_ALLOWANCE;
    case PTC_UI_DOCK_PREVIEW_REQUIRED: return PTC_UI_T_DOCK_BLOCKED;
    case PTC_UI_DOCK_PREVIEW_USAGE_UNKNOWN: return PTC_UI_T_DOCK_PREVIEW_UNKNOWN;
    default: return PTC_UI_T_STATUS_TO_CONFIRM;
    }
}

static uint32_t dock_preview_color(PtcUiDockPreviewState state)
{
    if (state == PTC_UI_DOCK_PREVIEW_REQUIRED) return UI_DANGER;
    if (state == PTC_UI_DOCK_PREVIEW_UNKNOWN || state == PTC_UI_DOCK_PREVIEW_USAGE_UNKNOWN) return UI_WARNING;
    return UI_SUCCESS;
}

static void draw_dock_editor_preview(uint32_t *pixels, uint32_t stride,
    const PtcUiModel *model, UiRect summary, bool valid, uint16_t entered)
{
    PtcDockPolicy draft = model->draft_dock_policy;
    PtcUiDockPreview current, after;
    char used[48], left[48];
    int64_t now = ptc_ui_render_now();
    draft.force_docked = false;
    draft.undocked_limit_enabled = true;
    draft.undocked_daily_minutes = entered;
    ptc_ui_project_dock_policy(model, &model->dock_policy, now, &current);
    ptc_ui_project_dock_policy(model, &draft, now, &after);
    format_duration(after.usage_known ? model->undocked_used_minutes : -1, used, sizeof(used));
    if (valid && after.state == PTC_UI_DOCK_PREVIEW_WAIVED)
        snprintf(left, sizeof(left), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
    else format_duration(valid && after.remaining_known ? after.remaining_minutes : -1, left, sizeof(left));
    fill_round_rect(pixels, stride, summary, 14, UI_RAISED);
    draw_text(pixels, stride, summary.x + 18, summary.y + 26,
        ptc_ui_text(PTC_UI_T_DOCK_CURRENT_POLICY), 14, UI_MUTED);
    draw_wrapped_text(pixels, stride, summary.x + 18, summary.y + 51,
        ptc_ui_text(dock_preview_label(current.state)), 16, summary.width - 36, 22, 2,
        dock_preview_color(current.state));
    int half = (summary.width - 12) / 2;
    draw_time_state_card(pixels, stride, (UiRect){summary.x, summary.y + 88, half, 80},
        ptc_ui_text(PTC_UI_T_DOCK_USED_SHORT), used, after.usage_known ? UI_ACCENT : UI_WARNING);
    draw_time_state_card(pixels, stride, (UiRect){summary.x + half + 12, summary.y + 88, half, 80},
        ptc_ui_text(PTC_UI_T_DOCK_AFTER_LEFT_SHORT), left, valid ? dock_preview_color(after.state) : UI_WARNING);
    draw_text(pixels, stride, summary.x + 18, summary.y + 202,
        ptc_ui_text(PTC_UI_T_DOCK_AFTER_STATUS), 14, UI_MUTED);
    draw_wrapped_text(pixels, stride, summary.x + 18, summary.y + 230,
        ptc_ui_text(valid ? dock_preview_label(after.state) : PTC_UI_T_PLEASE_ENTER_THE_VALID_AMOUNT_FIRST),
        18, summary.width - 36, 24, 2, valid ? dock_preview_color(after.state) : UI_WARNING);
    draw_wrapped_text(pixels, stride, summary.x + 18, summary.y + 291,
        ptc_ui_text(PTC_UI_T_DOCK_PREVIEW_SCOPE), 14, summary.width - 36, 20, 3, UI_MUTED);
}

void draw_minute_editor_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *KEY_LABELS[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", ptc_ui_text(PTC_UI_T_X_BACKSPACE), "0", ptc_ui_text(PTC_UI_T_Y_CLEAR)
    };
    UiRect dialog;
    uint16_t entered = model->numpad_current;
    char value[48];
    char hours_value[32];
    char minutes_value[32];
    char total_value[48];
    char played[48];
    char remaining[48];
    char after[48];
    char freshness[64];
    int after_minutes = -1;
    bool weekly = model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES;
    bool holiday = model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
                   model->numpad_purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES;
    bool scheduled = model->numpad_purpose == PTC_UI_NUMPAD_SCHEDULED_MINUTES;
    bool grant = model->numpad_purpose == PTC_UI_NUMPAD_GRANT_MINUTES;
    bool clock = model->numpad_purpose == PTC_UI_NUMPAD_BEDTIME_TIME;
    bool entered_valid = ptc_ui_duration_value(model, &entered);
    bool today_mode = model->numpad_purpose == PTC_UI_NUMPAD_MINUTES &&
        model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT;
    bool unlimited_draft = today_mode && model->today_limit_unlimited_draft;
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    bool quota_unchanged = false;
    char fitted[160];
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 630);
    if (today_mode) {
        UiRect tab_bg = {dialog.x + 34, dialog.y + 72, dialog.width - 68, 44};
        fill_round_rect(pixels, stride, tab_bg, 12, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, tab_bg, 12, 1, UI_RGB(UI_BLENDED(border_control)));
        for (int mode = 0; mode < 2; ++mode) {
            UiRect mode_rect = to_uirect(ptc_ui_today_mode_rect(mode));
            bool selected = (mode == 1) == unlimited_draft;
            const char *lbl = mode == 0 ? ptc_ui_text(PTC_UI_T_LIMITED_TIME_MODE)
                                        : ptc_ui_text(PTC_UI_T_NO_LIMIT_TODAY);
            if (selected) {
                fill_round_rect(pixels, stride, mode_rect, 9, mode == 1 ? UI_SUCCESS : UI_ACCENT);
                fit_text(fitted, sizeof(fitted), lbl, 16, mode_rect.width - 16);
                draw_text_center(pixels, stride, mode_rect, fitted, 16, UI_ON_ACCENT);
            } else {
                fit_text(fitted, sizeof(fitted), lbl, 15, mode_rect.width - 16);
                draw_text_center(pixels, stride, mode_rect, fitted, 15, UI_MUTED);
            }
        }
    }
    if (entered_valid) {
        if (clock) {
            snprintf(value, sizeof(value), "%02u:%02u", (unsigned int)(entered / 60u),
                     (unsigned int)(entered % 60u));
            snprintf(total_value, sizeof(total_value), ptc_ui_text(PTC_UI_T_SET_TIME_02U_02U_02U_02U),
                     (unsigned int)(entered / 60u), (unsigned int)(entered % 60u),
                     (unsigned int)(entered / 60u), (unsigned int)(entered % 60u));
        } else {
            snprintf(value, sizeof(value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)entered);
            snprintf(total_value, sizeof(total_value), ptc_ui_text(PTC_UI_T_TOTAL_U_MIN), (unsigned int)entered);
        }
    } else {
        snprintf(value, sizeof(value), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        snprintf(total_value, sizeof(total_value), "%s", clock ? ptc_ui_text(PTC_UI_T_SET_TIME_HOURS_MINUTES) : ptc_ui_text(PTC_UI_T_TOTAL_MINUTES));
    }
    snprintf(hours_value, sizeof(hours_value), clock ? ptc_ui_text(PTC_UI_T_S_HR) : ptc_ui_text(PTC_UI_T_S_HR_2),
             model->duration_hours_text[0] ? model->duration_hours_text : "--");
    snprintf(minutes_value, sizeof(minutes_value), clock ? ptc_ui_text(PTC_UI_T_S_MIN_2) : ptc_ui_text(PTC_UI_T_S_MIN),
             model->duration_minutes_text[0] ? model->duration_minutes_text : "--");
    format_duration(fresh && model->played_minutes_available ? model->played_minutes : -1, played, sizeof(played));
    if (fresh && model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    else format_duration(fresh && model->remaining_available ? model->remaining_minutes : -1, remaining, sizeof(remaining));
    if (weekly) {
        uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
        if (entered_valid && model->editor_index == weekday && model->played_minutes_available) {
            after_minutes = (int)entered - model->played_minutes;
        }
    } else if (entered_valid && model->numpad_purpose == PTC_UI_NUMPAD_MINUTES &&
               fresh && model->played_minutes_available) {
        after_minutes = model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES && model->remaining_available
            ? model->remaining_minutes + entered : (int)entered - model->played_minutes;
    }
    if (after_minutes < 0 && fresh && model->played_minutes_available &&
        (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
         (model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES &&
          model->editor_index == ptc_weekday_from_day_index(model->day_index)))) after_minutes = 0;
    if (unlimited_draft) snprintf(after, sizeof(after), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    else format_duration(after_minutes, after, sizeof(after));
    format_status_age(model, freshness, sizeof(freshness));
    if (fresh && entered_valid && model->numpad_purpose == PTC_UI_NUMPAD_MINUTES &&
        model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT) {
        PtcEffectiveRule current_rule = ptc_ui_plan_rule(model, PTC_UI_PLAN_SAVED);
        PtcDayRule requested_rule = {unlimited_draft ? PTC_RULE_MODE_UNLIMITED : PTC_RULE_MODE_LIMIT, entered};
        quota_unchanged = !ptc_ui_day_rule_effectively_changed(current_rule.rule, requested_rule);
    }

    /* When in unlimited mode for today, replace keypad and adjuster with explanation panel */
    if (today_mode && unlimited_draft) {
        UiRect panel = {dialog.x + 34, dialog.y + 148, 382, 376};
        fill_round_rect(pixels, stride, panel, 16, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, panel, 16, 1, UI_SUCCESS_BORDER);
        draw_text_center(pixels, stride, (UiRect){panel.x + 12, panel.y + 34, panel.width - 24, 32},
                         ptc_ui_text(PTC_UI_T_TODAY_S_UNLIMITED_TIME_MODE), 22, UI_SUCCESS);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_FREE_TO_PLAY_ALL_DAY_LONG_NO), 14, panel.width - 28);
        draw_text_center(pixels, stride, (UiRect){panel.x + 14, panel.y + 74, panel.width - 28, 22},
                         fitted, 14, UI_MUTED);

        UiRect tip = {panel.x + 18, panel.y + 114, panel.width - 36, 140};
        fill_round_rect(pixels, stride, tip, 12, UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, tip, 12, 1, UI_RGB(UI_BLENDED(border_control)));
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_EFFECTIVENESS_AND_PROTECTION_MECHANISM), 15, tip.width - 28);
        draw_text_bold(pixels, stride, tip.x + 14, tip.y + 26, fitted, 15, UI_INK);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ONLY_AFFECTS_TODAY_AND_WILL_AUTOMATICALLY_RESUME), 13, tip.width - 28);
        draw_text(pixels, stride, tip.x + 14, tip.y + 56, fitted, 13, UI_MUTED);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_BEDTIME_STILL_TAKES_EFFECT_INDEPENDENTLY_AND_THE), 13, tip.width - 28);
        draw_text(pixels, stride, tip.x + 14, tip.y + 84, fitted, 13, UI_MUTED);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_YOU_CAN_RESET_THE_TIME_LIMIT_AT), 13, tip.width - 28);
        draw_text(pixels, stride, tip.x + 14, tip.y + 112, fitted, 13, UI_MUTED);

        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_PRESS_A_OR_TO_CONFIRM_TO_MAKE), 15, panel.width - 28);
        draw_text_center(pixels, stride, (UiRect){panel.x + 14, panel.y + 286, panel.width - 28, 24},
                         fitted, 15, UI_ACCENT);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_PRESS_X_OR_TOUCH_THE_TOP_OF), 13, panel.width - 28);
        draw_text_center(pixels, stride, (UiRect){panel.x + 14, panel.y + 318, panel.width - 28, 20},
                         fitted, 13, UI_MUTED);
    } else {
        /* Left column (382px): quick presets (today_mode) + dual highlight fields + 3x4 keypad. */
        if (today_mode) {
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_TODAY_QUOTA_QUICK_LABEL), 13, 382);
            draw_text(pixels, stride, dialog.x + 34, dialog.y + 140, fitted, 13, UI_MUTED);
            for (int p = 0; p < 5; ++p) {
                uint16_t preset = ptc_ui_today_quota_preset_minutes(p);
                UiRect chip = to_uirect(ptc_ui_today_quota_preset_rect(p));
                bool active = entered_valid && entered == preset;
                char plabel[24];
                snprintf(plabel, sizeof(plabel), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)preset);
                fill_round_rect(pixels, stride, chip, 9, active ? UI_ACCENT : UI_RAISED);
                draw_rect_outline(pixels, stride, chip, 9, 1, active ? UI_ACCENT : UI_CONTROL);
                fit_text(fitted, sizeof(fitted), plabel, 13, chip.width - 8);
                draw_text_center(pixels, stride, chip, fitted, 13, active ? UI_ON_ACCENT : UI_INK);
            }
        } else {
            char step_hint[32];
            if (model->duration_field == PTC_UI_DURATION_HOURS)
                snprintf(step_hint, sizeof(step_hint), "%s", clock ? ptc_ui_text(PTC_UI_T_1_POINT_PER_STEP) : ptc_ui_text(PTC_UI_T_1_HOUR_PER_STEP));
            else snprintf(step_hint, sizeof(step_hint), clock ? ptc_ui_text(PTC_UI_T_CURRENT_U_MIN) : ptc_ui_text(PTC_UI_T_CURRENT_U), (unsigned)model->duration_step_feedback);

            const char *col_txt = ptc_ui_text(PTC_UI_T_LEFT_AND_RIGHT_SELECT_COLUMN);
            const char *adj_txt = ptc_ui_text(PTC_UI_T_UP_AND_DOWN_ADJUSTMENT);
            int cur_x = dialog.x + 34;
            draw_r_stick_axis_glyph(pixels, stride, cur_x, dialog.y + 150, 18, false, 0);
            cur_x += 22;
            draw_text(pixels, stride, cur_x, dialog.y + 164, col_txt, 13, UI_MUTED);
            cur_x += measure_text(col_txt, 13) + 8;
            draw_text(pixels, stride, cur_x, dialog.y + 164, "|", 13, UI_CONTROL);
            cur_x += measure_text("|", 13) + 8;
            draw_r_stick_axis_glyph(pixels, stride, cur_x, dialog.y + 150, 18, true, model->duration_scroll_dir);
            cur_x += 22;
            draw_text(pixels, stride, cur_x, dialog.y + 164, adj_txt, 13, UI_MUTED);
            cur_x += measure_text(adj_txt, 13) + 6;
            draw_text(pixels, stride, cur_x, dialog.y + 164, step_hint, 13, UI_ACCENT);
        }

        for (int field = 0; field < 2; ++field) {
            UiRect rect = to_uirect(ptc_ui_minute_editor_field_rect((PtcUiDurationField)field));
            bool selected = model->duration_field == (PtcUiDurationField)field;
            const char *flbl = ptc_ui_text(field == PTC_UI_DURATION_HOURS
                ? PTC_UI_T_TODAY_QUOTA_HOURS_LABEL : PTC_UI_T_TODAY_QUOTA_MINS_LABEL);
            draw_text(pixels, stride, rect.x + 4, rect.y - 6, flbl, 12, selected ? UI_ACCENT : UI_MUTED);
            fill_round_rect(pixels, stride, rect, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
            draw_rect_outline(pixels, stride, rect, 12, selected ? 2 : 1, selected ? UI_ACCENT : UI_CONTROL);

            draw_text_center(pixels, stride, rect,
                             field == PTC_UI_DURATION_HOURS ? hours_value : minutes_value,
                             24, selected ? UI_ACCENT : UI_INK);
        }

        {
            UiRect total_box = {dialog.x + 34, dialog.y + 266, 382, 30};
            fill_round_rect(pixels, stride, total_box, 8, UI_RGB(UI_BLENDED(surface_raised)));
            draw_rect_outline(pixels, stride, total_box, 8, 1, UI_RGB(UI_BLENDED(border_control)));
            fit_text(fitted, sizeof(fitted), total_value, 15, total_box.width - 16);
            draw_text_center(pixels, stride, total_box, fitted, 15,
                             entered_valid ? UI_ACCENT : UI_DANGER);
        }

        for (int index = 0; index < 12; ++index) {
            UiRect key = to_uirect(ptc_ui_minute_editor_key_rect(index));
            bool selected = index == model->numpad_cursor;
            fill_round_rect(pixels, stride, key, 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
            draw_rect_outline(pixels, stride, key, 10, selected ? 2 : 1, selected ? UI_ACCENT : UI_CONTROL);
            draw_text_center(pixels, stride, key, KEY_LABELS[index], index == 9 || index == 11 ? 15 : 23,
                             selected ? UI_ACCENT : UI_INK);
        }
        const char *hint_text = model->numpad_error[0] ? model->numpad_error : ptc_ui_text(PTC_UI_T_USE_THE_DIRECTION_KEYS_AND_A_TO);
        int hint_size = 13;
        while (hint_size > 11 && measure_text(hint_text, hint_size) > 382) --hint_size;
        fit_text(fitted, sizeof(fitted), hint_text, hint_size, 382);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 506, 382, 20},
                         fitted, hint_size, model->numpad_error[0] ? UI_DANGER : UI_MUTED);
    }

    UiRect summary = to_uirect(ptc_ui_minute_editor_summary_rect());
    if (model->numpad_purpose == PTC_UI_NUMPAD_DOCK_MINUTES) {
        draw_dock_editor_preview(pixels, stride, model, summary, entered_valid, entered);
    } else if (holiday) {
        PtcUiModel preview = *model;
        if (entered_valid) {
            if (model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES) {
                preview.draft_holiday_rule.minutes = entered;
            } else {
                preview.draft_makeup_workday_rule.minutes = entered;
            }
            draw_plan_impact_compact(pixels, stride, &preview, PTC_UI_PLAN_HOLIDAY, summary);
        } else {
            draw_text(pixels, stride, summary.x + 20, summary.y + 100, ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THE_VALID_AMOUNT_FIRST), 18, UI_RGB(UI_BLENDED(danger)));
        }
    } else if (weekly) {
        PtcUiModel preview = *model;
        if (entered_valid) {
            preview.draft_week[model->editor_index].minutes = entered;
            draw_plan_impact_compact(pixels, stride, &preview, PTC_UI_PLAN_WEEKLY, summary);
        } else {
            draw_text(pixels, stride, summary.x + 20, summary.y + 100, ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THE_VALID_AMOUNT_FIRST), 18, UI_RGB(UI_BLENDED(danger)));
        }
    } else if (scheduled) {
        PtcUiModel preview = *model;
        if (entered_valid) {
            preview.draft_scheduled_override.rule.mode = PTC_RULE_MODE_LIMIT;
            preview.draft_scheduled_override.rule.minutes = entered;
            draw_plan_impact_compact(pixels, stride, &preview, PTC_UI_PLAN_SCHEDULED, summary);
        } else {
            draw_text(pixels, stride, summary.x + 20, summary.y + 100, ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THE_VALID_AMOUNT_FIRST), 18, UI_RGB(UI_BLENDED(danger)));
        }
    } else if (grant) {
        PtcUiTimeProjection current_status;
        ptc_ui_project_time_status(model, ptc_ui_render_now(), &current_status);
        draw_time_state_card(pixels, stride, (UiRect){summary.x, summary.y, summary.width, 76},
                             ptc_ui_text(PTC_UI_T_CURRENT_STATUS), current_status.remaining_text,
                             time_projection_color(current_status.state));
        draw_time_state_card(pixels, stride, (UiRect){summary.x, summary.y + 90, summary.width, 76},
                             ptc_ui_text(PTC_UI_T_NEXT_CODE_DURATION), value,
                             entered_valid ? UI_ACCENT : UI_DANGER);
        fill_round_rect(pixels, stride, (UiRect){summary.x, summary.y + 180, summary.width, 86},
                        12, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, (UiRect){summary.x, summary.y + 180, summary.width, 86},
                          12, 1, UI_RGB(UI_BLENDED(border_control)));
        draw_wrapped_text(pixels, stride, summary.x + 18, summary.y + 204,
            ptc_ui_text(PTC_UI_T_ONLY_AFFECTS_THE_NEXT_NEW_CODE_THE),
            14, summary.width - 36, 22, 3, UI_MUTED);
    } else if (clock) {
        draw_time_state_card(pixels, stride, (UiRect){summary.x, summary.y, summary.width, 76},
                             ptc_ui_text(PTC_UI_T_TIME_RANGE), "00:00 - 23:59", UI_ACCENT);
        fill_round_rect(pixels, stride, (UiRect){summary.x, summary.y + 90, summary.width, 160},
                        14, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, (UiRect){summary.x, summary.y + 90, summary.width, 160},
                          14, 1, UI_RGB(UI_BLENDED(border_control)));
        draw_text(pixels, stride, summary.x + 20, summary.y + 120,
                  ptc_ui_text(PTC_UI_T_CHECK_AFTER_COMPLETION_OF_INPUT), 18, UI_INK);
        draw_wrapped_text(pixels, stride, summary.x + 20, summary.y + 152,
            ptc_ui_text(PTC_UI_T_THE_TIME_CAN_LOOP_PAST_00_00),
            15, summary.width - 40, 24, 4, UI_MUTED);
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES) {
        /* Visual Proportional Bar Card: Used + Estimated Remaining */
        UiRect bar_card = {summary.x, summary.y, summary.width, 114};
        fill_round_rect(pixels, stride, bar_card, 14, UI_RAISED);
        draw_rect_outline(pixels, stride, bar_card, 14, 1, UI_BORDER);
        {
            char used_lbl[64], left_lbl[64];
            snprintf(used_lbl, sizeof(used_lbl), ptc_ui_text(PTC_UI_T_TODAY_QUOTA_BAR_USED_FMT), played);
            snprintf(left_lbl, sizeof(left_lbl), ptc_ui_text(PTC_UI_T_TODAY_QUOTA_BAR_LEFT_FMT), after);

            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_TODAY_QUOTA_BAR_TITLE), 14, bar_card.width - 140);
            draw_text_bold(pixels, stride, bar_card.x + 16, bar_card.y + 26, fitted, 14, UI_INK);
            const char *top_right = unlimited_draft ? ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED) : value;
            int trw = measure_text(top_right, 13);
            draw_text_bold(pixels, stride, bar_card.x + bar_card.width - 16 - trw, bar_card.y + 26,
                           top_right, 13, unlimited_draft ? UI_SUCCESS : UI_ACCENT);

            UiRect track = {bar_card.x + 16, bar_card.y + 44, bar_card.width - 32, 18};
            fill_round_rect(pixels, stride, track, 9, UI_GAUGE_SLOT);
            draw_rect_outline(pixels, stride, track, 9, 1, UI_GAUGE_SLOT_BORDER);
            if (unlimited_draft) {
                fill_round_rect(pixels, stride, track, 9, UI_SUCCESS);
            } else if (fresh && model->played_minutes_available && entered_valid && entered > 0) {
                int used_m = model->played_minutes > 0 ? model->played_minutes : 0;
                int total_m = (int)entered;
                if (used_m >= total_m) {
                    fill_round_rect(pixels, stride, track, 9, UI_DANGER);
                } else {
                    int used_w = (track.width * used_m) / total_m;
                    if (used_m > 0 && used_w < 16) used_w = 16;
                    fill_round_rect(pixels, stride, track, 9, UI_SUCCESS);
                    if (used_w > 0) {
                        fill_round_rect(pixels, stride,
                                        (UiRect){track.x, track.y, used_w, track.height},
                                        used_w < 18 ? used_w / 2 : 9, UI_ACCENT);
                    }
                }
            } else if (!fresh) {
                fill_round_rect(pixels, stride, track, 9, UI_WARNING_SOFT);
            }

            int lw = measure_text(left_lbl, 13);
            fit_text(fitted, sizeof(fitted), used_lbl, 13, bar_card.width - lw - 44);
            draw_text(pixels, stride, bar_card.x + 16, bar_card.y + 84, fitted, 13,
                      fresh && model->played_minutes_available ? UI_ACCENT : UI_WARNING_TEXT);
            draw_text(pixels, stride, bar_card.x + bar_card.width - 16 - lw, bar_card.y + 84,
                      left_lbl, 13, time_state_accent(unlimited_draft || after_minutes >= 0, unlimited_draft, after_minutes));

            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_TODAY_QUOTA_NEXT_HINT), 12, bar_card.width - 32);
            draw_text(pixels, stride, bar_card.x + 16, bar_card.y + 104, fitted, 12, UI_MUTED);
        }

        if (quota_unchanged) {
            draw_unchanged_quota_card(pixels, stride,
                (UiRect){summary.x, summary.y + 126, summary.width, 108},
                unlimited_draft ? ptc_ui_text(PTC_UI_T_THERE_IS_NO_TIME_LIMIT_TODAY_AND) : ptc_ui_text(PTC_UI_T_THE_INPUT_VALUE_IS_THE_SAME_AS));
        } else {
            char before_formula[64] = "";
            char after_formula[64] = "";
            if (fresh && model->played_minutes_available && model->played_minutes >= 0) {
                snprintf(before_formula, sizeof(before_formula), ptc_ui_text(PTC_UI_T_PLAYED_ABOUT_D_MIN_TODAY), model->played_minutes);
                if (unlimited_draft) {
                    snprintf(after_formula, sizeof(after_formula), "%s", ptc_ui_text(PTC_UI_T_NO_DAILY_QUOTA_LIMIT));
                } else if (entered_valid) {
                    snprintf(after_formula, sizeof(after_formula), ptc_ui_text(PTC_UI_T_ENTERED_U_MIN_PLAYED_D_MIN),
                             (unsigned int)entered, model->played_minutes);
                }
            }
            draw_quota_transition_detailed(pixels, stride,
                (UiRect){summary.x, summary.y + 126, summary.width, 108},
                ptc_ui_text(PTC_UI_T_CURRENT_REMAINING), fresh ? ui_rule_source_label(model->rule_source) : ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED),
                remaining, before_formula,
                time_state_accent(fresh && (model->unrestricted_today == 1 || model->remaining_available),
                                  model->unrestricted_today == 1, model->remaining_minutes),
                ptc_ui_text(PTC_UI_T_NEW_REMAINING), unlimited_draft ? ptc_ui_text(PTC_UI_T_UNLIMITED_TIME_MODE_2) : ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_ADJUSTMENT),
                after, after_formula,
                time_state_accent(unlimited_draft || after_minutes >= 0, unlimited_draft, after_minutes));
        }

        if (today_mode) {
            UiRect refresh = to_uirect(ptc_ui_today_limit_refresh_rect());
            bool failed = model->quota_refresh_failed;
            bool warn = !model->waiting && (!fresh || failed);
            uint32_t bg = failed ? UI_DANGER_SOFT : (warn ? UI_WARNING_SOFT : UI_ACCENT_SOFT);
            uint32_t bd = failed ? UI_DANGER : (warn ? UI_WARNING : UI_ACCENT);
            uint32_t fg = failed ? UI_DANGER : (warn ? UI_WARNING_TEXT : UI_ACCENT);
            if (warn) draw_semantic_pulse_glow(pixels, stride, refresh, 10, bd);
            fill_round_rect(pixels, stride, refresh, 10, bg);
            draw_rect_outline(pixels, stride, refresh, 10, warn ? 2 : 1, bd);
            fit_text(fitted, sizeof(fitted),
                     model->waiting ? ptc_ui_text(PTC_UI_T_REFRESHING_HOST_STATUS)
                                    : ptc_ui_text(PTC_UI_T_CLICK_TO_REFRESH_THE_LATEST_STATUS_R3),
                     14, refresh.width - 20);
            draw_text_center(pixels, stride, refresh, fitted, 14, fg);
        }
        fit_text(fitted, sizeof(fitted),
                 model->quota_refresh_failed ? ptc_ui_text(PTC_UI_T_REFRESH_FAILED_PLEASE_CLICK_TO_TRY_AGAIN) :
                 (fresh ? freshness : ptc_ui_text(PTC_UI_T_THE_STATUS_NEEDS_TO_BE_CONFIRMED_PLEASE)),
                 14, summary.width);
        draw_text_center(pixels, stride, (UiRect){summary.x, summary.y + 342, summary.width, 24},
                         fitted, 14,
                         model->quota_refresh_failed ? UI_DANGER : (fresh ? status_age_color(model) : UI_WARNING_TEXT));
    }

    draw_overlay_actions(pixels, stride, model,
        today_mode && unlimited_draft ? ptc_ui_text(PTC_UI_T_A_CONFIRM_THAT_THERE_IS_NO_TIME) :
        (today_mode ? ptc_ui_text(PTC_UI_T_SAVE_TODAY_S_TOTAL_QUOTA) : ptc_ui_text(PTC_UI_T_DONE)));
}
