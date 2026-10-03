#include "ui_render_internal.h"

void draw_numpad_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *KEY_LABELS[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", ptc_ui_text(PTC_UI_T_X_BACKSPACE), "0", ptc_ui_text(PTC_UI_T_Y_CLEAR)
    };
    UiRect dialog;
    UiRect display = to_uirect(ptc_ui_numpad_display_rect());
    char shown[32];
    char current[64];
    char duration[64];
    uint16_t entered_minutes = 0;
    bool weekly_today_preview = false;
    bool weekly_today_no_change = false;
    char weekly_no_change_reason[128] = {0};
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 620, 700);
    if ((model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
         model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES ||
         model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
         model->numpad_purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) && model->numpad_text[0]) {
        snprintf(shown, sizeof(shown), ptc_ui_text(PTC_UI_T_S_MIN), model->numpad_text);
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE && model->numpad_text[0]) {
        ptc_ui_format_code(model->numpad_text, shown, sizeof(shown));
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        snprintf(shown, sizeof(shown), ptc_ui_text(PTC_UI_T_ENTER_CODE));
    } else if (model->numpad_text[0]) {
        snprintf(shown, sizeof(shown), "%s", model->numpad_text);
    } else {
        snprintf(shown, sizeof(shown), "%.*s", model->numpad_max_digits, "________");
    }
    if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        size_t entered = strlen(model->numpad_text);
        fill_round_rect(pixels, stride, display, 16, UI_RAISED);
        draw_rect_outline(pixels, stride, display, 16, 1, UI_BORDER);
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
            draw_text_center(pixels, stride, cell, filled ? digit : "_", 27,
                             filled ? UI_ACCENT : (current_slot ? UI_ACCENT : UI_DISABLED));
        }
    } else {
        fill_round_rect(pixels, stride, display, 16, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, display, 16, 2, UI_ACCENT);
        draw_text_center(pixels, stride, display, shown, 30, UI_ACCENT);
    }

    if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES) {
        snprintf(current, sizeof(current), ptc_ui_text(PTC_UI_T_CURRENT_U_MIN_RANGE_U_TO_U),
                 (unsigned int)model->numpad_current, (unsigned int)model->numpad_minimum,
                 (unsigned int)model->numpad_maximum);
        if (!ptc_ui_parse_minutes(model->numpad_text, model->numpad_minimum, model->numpad_maximum, &entered_minutes)) {
            entered_minutes = model->numpad_current;
        }
        format_duration(entered_minutes, duration, sizeof(duration));
    } else {
        if (model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) {
            uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
            PtcDayRule entered_rule;
            snprintf(current, sizeof(current), ptc_ui_text(PTC_UI_T_CURRENT_U_MIN_RANGE_U_TO_U),
                     (unsigned int)model->numpad_current, (unsigned int)model->numpad_minimum,
                     (unsigned int)model->numpad_maximum);
            if (!ptc_ui_parse_minutes(model->numpad_text, model->numpad_minimum, model->numpad_maximum, &entered_minutes)) {
                entered_minutes = model->numpad_current;
            }
            format_duration(entered_minutes, duration, sizeof(duration));
            entered_rule = model->draft_week[model->editor_index];
            entered_rule.minutes = entered_minutes;
            if (model->editor_index == weekday &&
                ptc_ui_day_rule_effectively_changed(model->current_week[weekday], entered_rule)) {
                PtcUiModel preview = *model;
                PtcUiPlanImpactProjection projection;
                preview.draft_week[weekday] = entered_rule;
                ptc_ui_project_plan_impact(&preview, PTC_UI_PLAN_WEEKLY, true,
                                           ptc_ui_render_now(), &projection);
                weekly_today_preview = projection.quota_changes_today;
                weekly_today_no_change = !projection.quota_changes_today;
                if (weekly_today_no_change)
                    ptc_ui_format_plan_impact(&preview, PTC_UI_PLAN_WEEKLY, ptc_ui_render_now(),
                                              weekly_no_change_reason, sizeof(weekly_no_change_reason));
            }
        } else if (model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
                   model->numpad_purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) {
            snprintf(current, sizeof(current), ptc_ui_text(PTC_UI_T_CURRENT_U_MIN_RANGE_U_TO_U_2),
                     (unsigned int)model->numpad_current, (unsigned int)model->numpad_minimum,
                     (unsigned int)model->numpad_maximum);
            if (!ptc_ui_parse_minutes(model->numpad_text, model->numpad_minimum, model->numpad_maximum, &entered_minutes)) {
                entered_minutes = model->numpad_current;
            }
            format_duration(entered_minutes, duration, sizeof(duration));
        } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
            unsigned int len = (unsigned int)strlen(model->numpad_text);
            snprintf(current, sizeof(current), ptc_ui_text(PTC_UI_T_ENTER_8_DIGIT_CODE_ENTERED_U_8), len);
            duration[0] = '\0';
        } else {
            snprintf(current, sizeof(current), ptc_ui_text(PTC_UI_T_PLEASE_ENTER_COMPLETE_8_DIGIT_CODE));
            duration[0] = '\0';
        }
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 218, dialog.width - 80, 22}, current, 16, UI_MUTED);
    if (weekly_today_preview) {
        char current_value[48];
        char after_value[48];
        char left[96];
        char right[112];
        int after_minutes = model->played_minutes_available
            ? (int)entered_minutes - model->played_minutes : -1;
        if (after_minutes < 0 && model->played_minutes_available) after_minutes = 0;
        if (model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else format_duration(model->remaining_available ? model->remaining_minutes : -1,
                             current_value, sizeof(current_value));
        format_duration(after_minutes, after_value, sizeof(after_value));
        snprintf(left, sizeof(left), "%s: %s",
                 ptc_ui_text(PTC_UI_T_PLAYABLE_TODAY), current_value);
        snprintf(right, sizeof(right), "%s: %s",
                 model->today_override_present ? (ptc_ui_text(PTC_UI_T_EST_AFTER_RESTORE)) : (ptc_ui_text(PTC_UI_T_EST_AFTER_SAVE)), after_value);
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 32, dialog.y + 242, 250, 32}, 6, UI_RAISED);
        draw_rect_outline(pixels, stride, (UiRect){dialog.x + 32, dialog.y + 242, 250, 32}, 6, 1, time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                            model->unrestricted_today == 1, model->remaining_minutes));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 244, 242, 28}, left, 14, UI_INK);
        draw_transition_arrow(pixels, stride, dialog.x + 298, dialog.y + 258, UI_MUTED);
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 314, dialog.y + 242, 274, 32}, 6, UI_RAISED);
        draw_rect_outline(pixels, stride, (UiRect){dialog.x + 314, dialog.y + 242, 274, 32}, 6, 1, time_state_accent(model->played_minutes_available, false, after_minutes));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 318, dialog.y + 244, 266, 28}, right,
                         model->today_override_present ? 12 : 14, UI_INK);
    } else if (weekly_today_no_change) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 32, dialog.y + 242, 556, 36}, 8, UI_RAISED);
        draw_rect_outline(pixels, stride, (UiRect){dialog.x + 32, dialog.y + 242, 556, 36}, 8, 1, UI_BORDER);
        char fitted[160];
        fit_text(fitted, sizeof(fitted), weekly_no_change_reason, 14, 528);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 244, 540, 30}, fitted, 14, UI_MUTED);
    } else if (duration[0]) {
        char duration_line[80];
        snprintf(duration_line, sizeof(duration_line), ptc_ui_text(PTC_UI_T_DURATION_S), duration);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 244, dialog.width - 80, 22},
                         duration_line, 17, UI_ACCENT);
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        char console_date[64];
        char date_line[128];
        ptc_ui_format_console_date(model, console_date, sizeof(console_date));
        snprintf(date_line, sizeof(date_line), ptc_ui_text(PTC_UI_T_S_SELECT_THIS_DATE_WHEN_GENERATING_CODE), console_date);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 244, dialog.width - 80, 22},
                         date_line, 16, model->status_loaded ? UI_ACCENT : UI_WARNING);
    }
    for (index = 0; index < 12; ++index) {
        UiRect key = to_uirect(ptc_ui_numpad_key_rect(index));
        bool selected = index == model->numpad_cursor;
        fill_round_rect(pixels, stride, key, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, key, 12, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
        draw_text_center(pixels, stride, key, KEY_LABELS[index], index == 9 || index == 11 ? 18 : 30,
                         selected ? UI_ACCENT : UI_INK);
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 35, dialog.y + 548, dialog.width - 70, 24},
                     ptc_ui_text(PTC_UI_T_DIRECTIONAL_KEYS_JOYSTICK_SELECTION_A_ENTER_X), 17, UI_MUTED);
    if (model->numpad_error[0]) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 35, dialog.y + 576, dialog.width - 70, 24},
                         model->numpad_error, 17, UI_DANGER);
    }
    draw_overlay_actions(pixels, stride, model, ptc_ui_text(PTC_UI_T_DONE));
}

void draw_pin_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog = to_uirect(ptc_ui_pin_dialog_rect());
    UiRect display = {dialog.x + 40, dialog.y + 112, 480, 70};
    char mask[PTC_UI_PIN_MAX_DIGITS + 1];
    char count[64];
    int row;
    ptc_ui_pin_format_mask(model, mask, sizeof(mask));
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 620);
    fill_round_rect(pixels, stride, display, 16, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, display, 16, 2, UI_ACCENT);
    draw_text_center(pixels, stride, display, mask[0] ? mask : ptc_ui_text(PTC_UI_T_THE_INPUT_CONTENT_IS_ONLY_DISPLAYED_AS), mask[0] ? 26 : 18,
                     mask[0] ? UI_ACCENT : UI_MUTED);
    snprintf(count, sizeof(count), ptc_ui_text(PTC_UI_T_U_DIGITS_ENTERED), (unsigned int)strlen(model->pin_text));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 188, 480, 24}, count, 17, UI_MUTED);

    draw_text(pixels, stride, dialog.x + 40, dialog.y + 222, ptc_ui_text(PTC_UI_T_CONTROLLER_INPUT_SIGNAL), 20, UI_INK);
    {
        int i;
        bool is_dark = (g_theme.resolved == PTC_UI_RESOLVED_DARK);

        /* 官方 Joy-Con 配色：左霓虹蓝 (Neon Blue)，右霓虹红 (Neon Red) */
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

        /* 调整 Joy-Con 宽高比：宽度收窄至 126，高度保持 238，造型更修长匀称 */
        int jc_w = 126;
        int jc_h = 238;
        int left_jc_x = dialog.x + 52;
        int left_jc_y = dialog.y + 242;
        int right_jc_x = dialog.x + 330;
        int right_jc_y = dialog.y + 242;

        int stick_x = left_jc_x + 63;
        int stick_y = left_jc_y + 70;
        int dpad_cx = left_jc_x + 63;
        int dpad_cy = left_jc_y + 164;
        int buttons_x = right_jc_x + 63;
        int buttons_y = right_jc_y + 70;
        int rstick_x = right_jc_x + 63;
        int rstick_y = right_jc_y + 164;

        /* Left Joy-Con (L) body - 霓虹蓝 */
        fill_round_rect(pixels, stride, (UiRect){left_jc_x, left_jc_y, jc_w, jc_h}, 26, left_jc_bg);
        draw_rect_outline(pixels, stride, (UiRect){left_jc_x, left_jc_y, jc_w, jc_h}, 26, 2, left_jc_border);

        /* Left shoulder (L button) */
        fill_round_rect(pixels, stride, (UiRect){left_jc_x + 10, left_jc_y - 10, 68, 10}, 4, left_jc_shoulder);
        draw_text_center(pixels, stride, (UiRect){left_jc_x + 10, left_jc_y - 10, 68, 10}, "L", 10, UI_PAGE);

        /* Minus (-) button */
        fill_round_rect(pixels, stride, (UiRect){left_jc_x + jc_w - 24, left_jc_y + 14, 14, 4}, 2, UI_CONTROL);

        /* Left Stick: 官方风格 1-8 指南针刻度表盘 */
        draw_circle_outline(pixels, stride, stick_x, stick_y, 38, 1, UI_BORDER);
        draw_circle_outline(pixels, stride, stick_x, stick_y, 22, 2, left_stick_accent);
        fill_round_rect(pixels, stride, (UiRect){stick_x - 16, stick_y - 16, 32, 32}, 16, UI_SURFACE);
        draw_circle_outline(pixels, stride, stick_x, stick_y, 16, 1, UI_CONTROL);

        /* 摇杆防滑十字刻痕 */
        draw_line(pixels, stride, stick_x, stick_y - 14, stick_x, stick_y - 10, 1, UI_CONTROL);
        draw_line(pixels, stride, stick_x, stick_y + 10, stick_x, stick_y + 14, 1, UI_CONTROL);
        draw_line(pixels, stride, stick_x - 14, stick_y, stick_x - 10, stick_y, 1, UI_CONTROL);
        draw_line(pixels, stride, stick_x + 10, stick_y, stick_x + 14, stick_y, 1, UI_CONTROL);

        /* 顺时针 8 方向数字与向外刻度 */
        for (i = 1; i <= 8; ++i) {
            int dx = 0, dy = 0;
            char label[4];
            switch (i) {
            case 1: dy = -42; break;
            case 2: dx = 30; dy = -30; break;
            case 3: dx = 42; break;
            case 4: dx = 30; dy = 30; break;
            case 5: dy = 42; break;
            case 6: dx = -30; dy = 30; break;
            case 7: dx = -42; break;
            case 8: dx = -30; dy = -30; break;
            }
            snprintf(label, sizeof(label), "%d", i);
            draw_text_center(pixels, stride, (UiRect){stick_x + dx - 10, stick_y + dy - 10, 20, 20},
                             label, 15, left_stick_accent);
        }

        /* 方向键（十字键 4 颗独立圆键） */
        for (i = 0; i < 4; ++i) {
            int d = 16;
            int dx = i == 1 ? d : (i == 3 ? -d : 0);
            int dy = i == 0 ? -d : (i == 2 ? d : 0);
            const char *arrow = i == 0 ? "^" : (i == 1 ? ">" : (i == 2 ? "v" : "<"));
            UiRect btn = {dpad_cx + dx - 9, dpad_cy + dy - 9, 18, 18};
            fill_round_rect(pixels, stride, btn, 9, UI_SURFACE);
            draw_circle_outline(pixels, stride, dpad_cx + dx, dpad_cy + dy, 9, 1, UI_CONTROL);
            draw_text_center(pixels, stride, btn, arrow, 11, UI_MUTED);
        }
        draw_text_center(pixels, stride, (UiRect){left_jc_x + 6, left_jc_y + 192, jc_w - 12, 16},
                         ptc_ui_text(PTC_UI_T_THE_CROSS_KEY_IS_THE_SAME_AS), 11, UI_MUTED);

        /* 截图键 */
        fill_round_rect(pixels, stride, (UiRect){left_jc_x + jc_w - 22, left_jc_y + jc_h - 24, 12, 12}, 3, UI_CONTROL);

        /* Right Joy-Con (R) body - 霓虹红 */
        fill_round_rect(pixels, stride, (UiRect){right_jc_x, right_jc_y, jc_w, jc_h}, 26, right_jc_bg);
        draw_rect_outline(pixels, stride, (UiRect){right_jc_x, right_jc_y, jc_w, jc_h}, 26, 2, right_jc_border);

        /* Right shoulder (R button) */
        fill_round_rect(pixels, stride, (UiRect){right_jc_x + jc_w - 78, right_jc_y - 10, 68, 10}, 4, right_jc_shoulder);
        draw_text_center(pixels, stride, (UiRect){right_jc_x + jc_w - 78, right_jc_y - 10, 68, 10}, "R", 10, UI_PAGE);

        /* Plus (+) button */
        draw_line(pixels, stride, right_jc_x + 14, right_jc_y + 16, right_jc_x + 24, right_jc_y + 16, 2, UI_CONTROL);
        draw_line(pixels, stride, right_jc_x + 19, right_jc_y + 11, right_jc_x + 19, right_jc_y + 21, 2, UI_CONTROL);

        /* ABXY 四键：突出强化 X=0 与 Y=9 */
        for (i = 0; i < 4; ++i) {
            int d = 26;
            int dx = i == 1 ? d : (i == 3 ? -d : 0);
            int dy = i == 0 ? -d : (i == 2 ? d : 0);
            const char *letter = i == 0 ? "X" : (i == 1 ? "A" : (i == 2 ? "B" : "Y"));
            char label[12];
            UiRect key = {buttons_x + dx - 15, buttons_y + dy - 15, 30, 30};
            bool is_digit = (i == 0 || i == 3);
            fill_round_rect(pixels, stride, key, 15, is_digit ? right_key_active_bg : UI_SURFACE);
            draw_circle_outline(pixels, stride, buttons_x + dx, buttons_y + dy, 15, 2,
                                is_digit ? right_key_active_border : UI_CONTROL);
            if (i == 0) snprintf(label, sizeof(label), "X=0");
            else if (i == 3) snprintf(label, sizeof(label), "Y=9");
            else snprintf(label, sizeof(label), "%s", letter);
            draw_text_center(pixels, stride, key, label, is_digit ? 11 : 14,
                             is_digit ? right_key_active_text : UI_MUTED);
        }

        /* 右摇杆（同样映射 1-8） */
        draw_circle_outline(pixels, stride, rstick_x, rstick_y, 20, 2, UI_CONTROL);
        fill_round_rect(pixels, stride, (UiRect){rstick_x - 14, rstick_y - 14, 28, 28}, 14, UI_SURFACE);
        draw_circle_outline(pixels, stride, rstick_x, rstick_y, 14, 1, UI_CONTROL);
        draw_text_center(pixels, stride, (UiRect){rstick_x - 14, rstick_y - 14, 28, 28}, "R", 13, UI_MUTED);
        draw_text_center(pixels, stride, (UiRect){right_jc_x + 6, right_jc_y + 192, jc_w - 12, 16},
                         ptc_ui_text(PTC_UI_T_RIGHT_STICK_MAPPING_IS_THE_SAME), 11, UI_MUTED);

        /* Home 键 */
        draw_circle_outline(pixels, stride, right_jc_x + 18, right_jc_y + jc_h - 18, 7, 2, UI_CONTROL);

        /* 中央说明卡片（提供官方风格图例） */
        UiRect center_guide = {dialog.x + 196, dialog.y + 248, 116, 222};
        fill_round_rect(pixels, stride, center_guide, 14, UI_PAGE);
        draw_rect_outline(pixels, stride, center_guide, 14, 1, UI_BORDER);

        UiRect gbadge = {dialog.x + 204, dialog.y + 258, 100, 22};
        fill_round_rect(pixels, stride, gbadge, 6, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, gbadge, 6, 1, UI_ACCENT);
        draw_text_center(pixels, stride, gbadge, ptc_ui_text(PTC_UI_T_JOY_CON_INPUT), 12, UI_ACCENT);

        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 288, 112, 16}, ptc_ui_text(PTC_UI_T_JOYSTICK_1_TO_8), 13, UI_INK);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 306, 112, 14}, ptc_ui_text(PTC_UI_T_CLOCKWISE), 11, UI_MUTED);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 328, 112, 16}, ptc_ui_text(PTC_UI_T_X_KEY_0), 13, right_key_active_border);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 350, 112, 16}, ptc_ui_text(PTC_UI_T_Y_KEY_9), 13, right_key_active_border);
        draw_line(pixels, stride, dialog.x + 208, dialog.y + 374, dialog.x + 300, dialog.y + 374, 1, UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 382, 112, 16}, ptc_ui_text(PTC_UI_T_ZL_KEY_BACKSPACE), 12, UI_MUTED);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 402, 112, 16}, ptc_ui_text(PTC_UI_T_KEY_CONFIRM), 12, UI_MUTED);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 422, 112, 14}, ptc_ui_text(PTC_UI_T_LONG_PRESS_SWITCH_KEYBOARD), 11, UI_MUTED);
    }
    draw_text(pixels, stride, dialog.x + 52, dialog.y + 490,
              ptc_ui_text(PTC_UI_T_THE_LEFT_AND_RIGHT_JOYSTICKS_HAVE_THE), 13, UI_MUTED);

    draw_text(pixels, stride, dialog.x + 590, dialog.y + 212, ptc_ui_text(PTC_UI_T_TOUCH_NUMERIC_KEYPAD), 20, UI_INK);
    for (row = 0; row < 10; ++row) {
        UiRect key = to_uirect(ptc_ui_pin_key_rect(row));
        bool selected = model->pin_focus == row;
        char label[4];
        snprintf(label, sizeof(label), "%d", row);
        fill_round_rect(pixels, stride, key, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, key, 12, selected ? 2 : 1, selected ? UI_ACCENT : UI_CONTROL);
        draw_text_center(pixels, stride, key, label, 24, selected ? UI_ACCENT : UI_INK);
    }
    draw_dialog_button(pixels, stride, ptc_ui_pin_backspace_rect(), ptc_ui_text(PTC_UI_T_ZL_BACKSPACE),
                       UI_WARNING_SOFT, UI_WARNING, true);
    draw_dialog_button(pixels, stride, ptc_ui_pin_confirm_rect(), ptc_ui_text(PTC_UI_T_CONFIRM),
                       UI_ACCENT, UI_ON_ACCENT, false);
    draw_dialog_button(pixels, stride, ptc_ui_pin_cancel_rect(), ptc_ui_text(PTC_UI_T_B_CANCEL),
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_pin_keyboard_rect(), ptc_ui_text(PTC_UI_T_LONG_PRESS_TRADITIONAL_KEYBOARD),
                       UI_RAISED, UI_INK, true);
    draw_text(pixels, stride, dialog.x + 590, dialog.y + 594,
              model->pin_error[0] ? model->pin_error : ptc_ui_text(PTC_UI_T_SHORT_PRESS_CONFIRM_LONG_PRESS_ABOUT_1),
              16, model->pin_error[0] ? UI_DANGER : UI_MUTED);
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
    draw_dialog_shell(pixels, stride, model, &dialog, 920, 620);
    if (today_mode) {
        UiRect tab_bg = {dialog.x + 36, dialog.y + 76, 848, 44};
        fill_round_rect(pixels, stride, tab_bg, 12, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, tab_bg, 12, 1, UI_RGB(UI_BLENDED(border_control)));
        for (int mode = 0; mode < 2; ++mode) {
            UiRect mode_rect = to_uirect(ptc_ui_today_mode_rect(mode));
            bool selected = (mode == 1) == unlimited_draft;
            if (selected) {
                fill_round_rect(pixels, stride, mode_rect, 9, UI_ACCENT);
                draw_text_center(pixels, stride, mode_rect,
                                 mode == 0 ? ptc_ui_text(PTC_UI_T_LIMITED_TIME_MODE_CUSTOMIZE_TODAY_S_QUOTA) : ptc_ui_text(PTC_UI_T_UNLIMITED_TIME_MODE_FREE_TO_PLAY_ALL),
                                 16, UI_ON_ACCENT);
            } else {
                draw_text_center(pixels, stride, mode_rect,
                                 mode == 0 ? ptc_ui_text(PTC_UI_T_LIMITED_TIME_MODE) : ptc_ui_text(PTC_UI_T_UNLIMITED_TIME_MODE),
                                 15, UI_MUTED);
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
        snprintf(value, sizeof(value), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        snprintf(total_value, sizeof(total_value), clock ? ptc_ui_text(PTC_UI_T_SET_TIME_HOURS_MINUTES) : ptc_ui_text(PTC_UI_T_TOTAL_MINUTES));
    }
    snprintf(hours_value, sizeof(hours_value), clock ? ptc_ui_text(PTC_UI_T_S_HR) : ptc_ui_text(PTC_UI_T_S_HR_2),
             model->duration_hours_text[0] ? model->duration_hours_text : "--");
    snprintf(minutes_value, sizeof(minutes_value), clock ? ptc_ui_text(PTC_UI_T_S_MIN_2) : ptc_ui_text(PTC_UI_T_S_MIN),
             model->duration_minutes_text[0] ? model->duration_minutes_text : "--");
    format_duration(fresh && model->played_minutes_available ? model->played_minutes : -1, played, sizeof(played));
    if (fresh && model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
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
    if (unlimited_draft) snprintf(after, sizeof(after), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
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
        UiRect panel = {dialog.x + 36, dialog.y + 150, 338, 350};
        fill_round_rect(pixels, stride, panel, 16, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, panel, 16, 1, UI_RGB(UI_BLENDED(border_control)));
        draw_text_center(pixels, stride, (UiRect){panel.x, panel.y + 36, panel.width, 32},
                         ptc_ui_text(PTC_UI_T_TODAY_S_UNLIMITED_TIME_MODE), 22, UI_SUCCESS);
        draw_text_center(pixels, stride, (UiRect){panel.x, panel.y + 78, panel.width, 22},
                         ptc_ui_text(PTC_UI_T_FREE_TO_PLAY_ALL_DAY_LONG_NO), 14, UI_MUTED);

        UiRect tip = {panel.x + 18, panel.y + 120, panel.width - 36, 126};
        fill_round_rect(pixels, stride, tip, 12, UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, tip, 12, 1, UI_RGB(UI_BLENDED(border_control)));
        draw_text(pixels, stride, tip.x + 16, tip.y + 24, ptc_ui_text(PTC_UI_T_EFFECTIVENESS_AND_PROTECTION_MECHANISM), 15, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, tip.x + 16, tip.y + 50, ptc_ui_text(PTC_UI_T_ONLY_AFFECTS_TODAY_AND_WILL_AUTOMATICALLY_RESUME), 13, UI_MUTED);
        draw_text(pixels, stride, tip.x + 16, tip.y + 74, ptc_ui_text(PTC_UI_T_BEDTIME_STILL_TAKES_EFFECT_INDEPENDENTLY_AND_THE), 13, UI_MUTED);
        draw_text(pixels, stride, tip.x + 16, tip.y + 98, ptc_ui_text(PTC_UI_T_YOU_CAN_RESET_THE_TIME_LIMIT_AT), 13, UI_MUTED);

        draw_text_center(pixels, stride, (UiRect){panel.x, panel.y + 276, panel.width, 24},
                         ptc_ui_text(PTC_UI_T_PRESS_A_OR_TO_CONFIRM_TO_MAKE), 15, UI_ACCENT);
        draw_text_center(pixels, stride, (UiRect){panel.x, panel.y + 308, panel.width, 20},
                         ptc_ui_text(PTC_UI_T_PRESS_X_OR_TOUCH_THE_TOP_OF), 13, UI_MUTED);
    } else {
        /* Keep the editable value and keypad in one continuous left
         * column (338px). The right column (470px) is reserved for consequences and validation. */
        {
            char step_hint[32];
            if (model->duration_field == PTC_UI_DURATION_HOURS)
                snprintf(step_hint, sizeof(step_hint), clock ? ptc_ui_text(PTC_UI_T_1_POINT_PER_STEP) : ptc_ui_text(PTC_UI_T_1_HOUR_PER_STEP));
            else snprintf(step_hint, sizeof(step_hint), clock ? ptc_ui_text(PTC_UI_T_CURRENT_U_MIN) : ptc_ui_text(PTC_UI_T_CURRENT_U), (unsigned)model->duration_step_feedback);
            draw_r_stick_axis_glyph(pixels, stride, dialog.x + 36, dialog.y + 132, 18,
                                    false, 0);
            draw_text(pixels, stride, dialog.x + 60, dialog.y + 146, ptc_ui_text(PTC_UI_T_LEFT_AND_RIGHT_SELECT_COLUMN), 13, UI_MUTED);
            draw_text(pixels, stride, dialog.x + 128, dialog.y + 146, "|", 13, UI_CONTROL);
            draw_r_stick_axis_glyph(pixels, stride, dialog.x + 142, dialog.y + 132, 18,
                                    true, model->duration_scroll_dir);
            draw_text(pixels, stride, dialog.x + 166, dialog.y + 146, ptc_ui_text(PTC_UI_T_UP_AND_DOWN_ADJUSTMENT), 13, UI_MUTED);
            draw_text(pixels, stride, dialog.x + 236, dialog.y + 146, step_hint, 13, UI_ACCENT);
        }

        for (int field = 0; field < 2; ++field) {
            UiRect rect = to_uirect(ptc_ui_minute_editor_field_rect((PtcUiDurationField)field));
            bool selected = model->duration_field == (PtcUiDurationField)field;
            fill_round_rect(pixels, stride, rect, 14, selected ? UI_ACCENT_SOFT : UI_RAISED);
            draw_rect_outline(pixels, stride, rect, 14, selected ? 2 : 1, selected ? UI_ACCENT : UI_CONTROL);

            draw_text_center(pixels, stride, rect,
                             field == PTC_UI_DURATION_HOURS ? hours_value : minutes_value,
                             24, selected ? UI_ACCENT : UI_INK);

            if (selected) {
                int arrow_cx = rect.x + rect.width / 2;
                bool up_active = model->duration_scroll_anim_ticks > 0 && model->duration_scroll_dir > 0;
                bool down_active = model->duration_scroll_anim_ticks > 0 && model->duration_scroll_dir < 0;
                draw_arrow_glyph(pixels, stride, arrow_cx, rect.y - 7 + (up_active ? -2 : 0), true,
                                 up_active ? UI_ACCENT : UI_MUTED);
                draw_arrow_glyph(pixels, stride, arrow_cx, rect.y + rect.height + 7 + (down_active ? 2 : 0), false,
                                 down_active ? UI_ACCENT : UI_MUTED);
            }
        }

        {
            UiRect total_box = {dialog.x + 36, dialog.y + 236, 338, 32};
            fill_round_rect(pixels, stride, total_box, 8, UI_RGB(UI_BLENDED(surface_raised)));
            draw_rect_outline(pixels, stride, total_box, 8, 1, UI_RGB(UI_BLENDED(border_control)));
            draw_text_center(pixels, stride, total_box, total_value, 16,
                             entered_valid ? UI_ACCENT : UI_DANGER);
        }

        for (int index = 0; index < 12; ++index) {
            UiRect key = to_uirect(ptc_ui_minute_editor_key_rect(index));
            bool selected = index == model->numpad_cursor;
            fill_round_rect(pixels, stride, key, 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
            draw_rect_outline(pixels, stride, key, 10, selected ? 2 : 1, selected ? UI_ACCENT : UI_CONTROL);
            draw_text_center(pixels, stride, key, KEY_LABELS[index], index == 9 || index == 11 ? 15 : 24,
                             selected ? UI_ACCENT : UI_INK);
        }
        draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 492, 338, 22},
                         model->numpad_error[0] ? model->numpad_error : ptc_ui_text(PTC_UI_T_USE_THE_DIRECTION_KEYS_AND_A_TO),
                         14, model->numpad_error[0] ? UI_DANGER : UI_MUTED);
    }

    UiRect summary = to_uirect(ptc_ui_minute_editor_summary_rect());
    if (holiday) {
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
        draw_time_state_card(pixels, stride, (UiRect){summary.x, summary.y, summary.width, 76}, ptc_ui_text(PTC_UI_T_THE_QUOTA_HAS_BEEN_CONSUMED_ESTIMATED), played,
                             fresh && model->played_minutes_available ? UI_ACCENT : UI_WARNING);
        if (quota_unchanged) {
            draw_unchanged_quota_card(pixels, stride,
                (UiRect){summary.x, summary.y + 90, summary.width, 92},
                unlimited_draft ? ptc_ui_text(PTC_UI_T_THERE_IS_NO_TIME_LIMIT_TODAY_AND) : ptc_ui_text(PTC_UI_T_THE_INPUT_VALUE_IS_THE_SAME_AS));
        } else {
            char before_formula[64] = "";
            char after_formula[64] = "";
            if (fresh && model->played_minutes_available && model->played_minutes >= 0) {
                snprintf(before_formula, sizeof(before_formula), ptc_ui_text(PTC_UI_T_PLAYED_ABOUT_D_MIN_TODAY), model->played_minutes);
                if (unlimited_draft) {
                    snprintf(after_formula, sizeof(after_formula), ptc_ui_text(PTC_UI_T_NO_DAILY_QUOTA_LIMIT));
                } else if (entered_valid) {
                    snprintf(after_formula, sizeof(after_formula), ptc_ui_text(PTC_UI_T_ENTERED_U_MIN_PLAYED_D_MIN),
                             (unsigned int)entered, model->played_minutes);
                }
            }
            draw_quota_transition_detailed(pixels, stride,
                (UiRect){summary.x, summary.y + 90, summary.width, 92},
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
            fill_round_rect(pixels, stride, refresh, 10, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, refresh, 10, 1, UI_ACCENT);
            draw_text_center(pixels, stride, refresh,
                model->waiting ? ptc_ui_text(PTC_UI_T_REFRESHING_HOST_STATUS) : ptc_ui_text(PTC_UI_T_CLICK_TO_REFRESH_THE_LATEST_STATUS_R3), 15, UI_ACCENT);
        }
        draw_text_center(pixels, stride, (UiRect){summary.x, summary.y + 248, summary.width, 26},
                         model->quota_refresh_failed ? ptc_ui_text(PTC_UI_T_REFRESH_FAILED_PLEASE_CLICK_TO_TRY_AGAIN) :
                         (fresh ? freshness : ptc_ui_text(PTC_UI_T_THE_STATUS_NEEDS_TO_BE_CONFIRMED_PLEASE)), 15,
                         model->quota_refresh_failed ? UI_DANGER : (fresh ? status_age_color(model) : UI_WARNING));
    }

    draw_overlay_actions(pixels, stride, model,
        today_mode && unlimited_draft ? ptc_ui_text(PTC_UI_T_A_CONFIRM_THAT_THERE_IS_NO_TIME) :
        (today_mode ? ptc_ui_text(PTC_UI_T_SAVE_TODAY_S_TOTAL_QUOTA) : ptc_ui_text(PTC_UI_T_DONE)));
}
