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

static void draw_transition_arrow(
    uint32_t *pixels, uint32_t stride, int cx, int cy, uint32_t color)
{
    draw_line(pixels, stride, cx - 9, cy, cx + 8, cy, 2, color);
    draw_line(pixels, stride, cx + 8, cy, cx + 3, cy - 5, 2, color);
    draw_line(pixels, stride, cx + 8, cy, cx + 3, cy + 5, 2, color);
}

static void draw_remaining_transition(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    const char *before_label,
    const char *before_value,
    uint32_t before_accent,
    const char *after_label,
    const char *after_value,
    uint32_t after_accent)
{
    int arrow_width = rect.width >= 500 ? 52 : 34;
    int card_width = (rect.width - arrow_width) / 2;
    UiRect before = {rect.x, rect.y, card_width, rect.height};
    UiRect after = {rect.x + card_width + arrow_width, rect.y,
                    rect.width - card_width - arrow_width, rect.height};
    draw_time_state_card(pixels, stride, before, before_label, before_value, before_accent);
    draw_transition_arrow(pixels, stride,
        before.x + before.width + arrow_width / 2,
        rect.y + rect.height / 2, UI_MUTED);
    draw_time_state_card(pixels, stride, after, after_label, after_value, after_accent);
}

static void draw_unchanged_quota_card(
    uint32_t *pixels, uint32_t stride, UiRect rect, const char *reason)
{
    char fitted[192];
    fill_round_rect(pixels, stride, rect, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, rect, 16, 1, UI_BORDER);
    draw_text(pixels, stride, rect.x + 18, rect.y + 30, "今天额度不变", 17, UI_INK);
    fit_text(fitted, sizeof(fitted), reason, 14, rect.width - 36);
    draw_text(pixels, stride, rect.x + 18, rect.y + 58, fitted, 14, UI_MUTED);
}

void draw_dialog_shell(
    uint32_t *pixels,
    uint32_t stride,
    const PtcUiModel *model,
    UiRect *dialog,
    int width,
    int height)
{
    bool numeric = model->overlay == PTC_UI_OVERLAY_NUMPAD || model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR;
    bool pin = model->overlay == PTC_UI_OVERLAY_PIN;
    bool custom_header = model->overlay == PTC_UI_OVERLAY_NOTICE_DETAILS ||
                         model->overlay == PTC_UI_OVERLAY_DAY_DECISION;
    const char *title = custom_header ? "" : (numeric ? model->numpad_title : (pin ? model->pin_title : model->overlay_title));
    const char *description = custom_header ? "" : (numeric ? model->numpad_guide : (pin ? model->pin_guide : model->overlay_body));
    *dialog = to_uirect(ptc_ui_dialog_rect(width, height));
    float open_progress = ui_ease_out((float)model->overlay_open_frames / (float)PTC_UI_OVERLAY_OPEN_FRAMES);
    int y_offset = (int)(((float)PTC_UI_OVERLAY_OPEN_FRAMES - open_progress * (float)PTC_UI_OVERLAY_OPEN_FRAMES) * 2.0f + 0.5f);
    dialog->y += y_offset;

    uint32_t raw_scrim = UI_BLENDED(scrim);
    uint32_t a = (raw_scrim >> 24) & 0xff;
    a = (uint32_t)(a * open_progress + 0.5f);
    uint32_t fade_scrim = (a << 24) | (raw_scrim & 0xffffff);

    fill_rect_packed(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, fade_scrim);
    draw_round_rect_shadow(pixels, stride, *dialog, 16, 16, 76, 5);
    fill_round_rect_gradient(pixels, stride, *dialog, 16, UI_SURFACE,
                             UI_RGB(ui_darken(UI_BLENDED(surface), 4)));
    draw_rect_outline(pixels, stride, *dialog, 16, 1, UI_BORDER);
    fill_round_rect(pixels, stride, (UiRect){dialog->x + 34, dialog->y + 15, 36, 5}, 2, UI_CORAL);
    if (title && title[0]) {
        draw_text(pixels, stride, dialog->x + 34, dialog->y + 54, title, 29, UI_INK);
    }
    if (description && description[0]) {
        draw_wrapped_text(pixels, stride, dialog->x + 34, dialog->y + 88, description,
                          18, dialog->width - 68, 26, 6, UI_MUTED);
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

void draw_numpad_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *KEY_LABELS[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "X 退格", "0", "Y 清空"
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
        snprintf(shown, sizeof(shown), "%s 分钟", model->numpad_text);
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE && model->numpad_text[0]) {
        ptc_ui_format_code(model->numpad_text, shown, sizeof(shown));
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        snprintf(shown, sizeof(shown), "输入加时码");
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
        snprintf(current, sizeof(current), "当前值：%u 分钟   |   范围 %u到%u",
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
            snprintf(current, sizeof(current), "当前值：%u 分钟   |   范围 %u到%u",
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
            snprintf(current, sizeof(current), "当前值：%u 分钟  |  范围 %u到%u",
                     (unsigned int)model->numpad_current, (unsigned int)model->numpad_minimum,
                     (unsigned int)model->numpad_maximum);
            if (!ptc_ui_parse_minutes(model->numpad_text, model->numpad_minimum, model->numpad_maximum, &entered_minutes)) {
                entered_minutes = model->numpad_current;
            }
            format_duration(entered_minutes, duration, sizeof(duration));
        } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
            unsigned int len = (unsigned int)strlen(model->numpad_text);
            snprintf(current, sizeof(current), "请输入 8 位加时码   |   当前已输入 %u/8 位", len);
            duration[0] = '\0';
        } else {
            snprintf(current, sizeof(current), "请输入完整的 8 位加时码");
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
        if (model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "不限时");
        else format_duration(model->remaining_available ? model->remaining_minutes : -1,
                             current_value, sizeof(current_value));
        format_duration(after_minutes, after_value, sizeof(after_value));
        snprintf(left, sizeof(left), "%s：%s",
                 "今天还可玩", current_value);
        snprintf(right, sizeof(right), "%s：%s",
                 model->today_override_present ? "恢复后预计还可玩" : "保存后预计还可玩", after_value);
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
        snprintf(duration_line, sizeof(duration_line), "换算：%s", duration);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 244, dialog.width - 80, 22},
                         duration_line, 17, UI_ACCENT);
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        char console_date[64];
        char date_line[128];
        ptc_ui_format_console_date(model, console_date, sizeof(console_date));
        snprintf(date_line, sizeof(date_line), "%s  |  生成加时码请选这一天", console_date);
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
                     "方向键/摇杆选择  A 输入  X 退格  Y 清空  + 完成", 17, UI_MUTED);
    if (model->numpad_error[0]) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 35, dialog.y + 576, dialog.width - 70, 24},
                         model->numpad_error, 17, UI_DANGER);
    }
    draw_overlay_actions(pixels, stride, model, "+  完成输入");
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
    draw_text_center(pixels, stride, display, mask[0] ? mask : "输入内容只显示为圆点", mask[0] ? 26 : 18,
                     mask[0] ? UI_ACCENT : UI_MUTED);
    snprintf(count, sizeof(count), "已输入 %u 位", (unsigned int)strlen(model->pin_text));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 188, 480, 24}, count, 17, UI_MUTED);

    draw_text(pixels, stride, dialog.x + 40, dialog.y + 222, "手柄输入示意", 20, UI_INK);
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
                         "十字键同正方向", 11, UI_MUTED);

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
                         "右摇杆映射相同", 11, UI_MUTED);

        /* Home 键 */
        draw_circle_outline(pixels, stride, right_jc_x + 18, right_jc_y + jc_h - 18, 7, 2, UI_CONTROL);

        /* 中央说明卡片（提供官方风格图例） */
        UiRect center_guide = {dialog.x + 196, dialog.y + 248, 116, 222};
        fill_round_rect(pixels, stride, center_guide, 14, UI_PAGE);
        draw_rect_outline(pixels, stride, center_guide, 14, 1, UI_BORDER);

        UiRect gbadge = {dialog.x + 204, dialog.y + 258, 100, 22};
        fill_round_rect(pixels, stride, gbadge, 6, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, gbadge, 6, 1, UI_ACCENT);
        draw_text_center(pixels, stride, gbadge, "Joy-Con 输入", 12, UI_ACCENT);

        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 288, 112, 16}, "摇杆: 1 到 8", 13, UI_INK);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 306, 112, 14}, "(顺时针方向)", 11, UI_MUTED);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 328, 112, 16}, "X 键: 0", 13, right_key_active_border);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 350, 112, 16}, "Y 键: 9", 13, right_key_active_border);
        draw_line(pixels, stride, dialog.x + 208, dialog.y + 374, dialog.x + 300, dialog.y + 374, 1, UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 382, 112, 16}, "ZL 键 退格", 12, UI_MUTED);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 402, 112, 16}, "+ 键 确认", 12, UI_MUTED);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 198, dialog.y + 422, 112, 14}, "长按+切换键盘", 11, UI_MUTED);
    }
    draw_text(pixels, stride, dialog.x + 52, dialog.y + 490,
              "左右摇杆映射相同：方向 1-8；X=0，Y=9；十字键同正方向", 13, UI_MUTED);

    draw_text(pixels, stride, dialog.x + 590, dialog.y + 212, "触摸数字键盘", 20, UI_INK);
    for (row = 0; row < 10; ++row) {
        UiRect key = to_uirect(ptc_ui_pin_key_rect(row));
        bool selected = model->pin_focus == row;
        char label[4];
        snprintf(label, sizeof(label), "%d", row);
        fill_round_rect(pixels, stride, key, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, key, 12, selected ? 2 : 1, selected ? UI_ACCENT : UI_CONTROL);
        draw_text_center(pixels, stride, key, label, 24, selected ? UI_ACCENT : UI_INK);
    }
    draw_dialog_button(pixels, stride, ptc_ui_pin_backspace_rect(), "ZL  退格",
                       UI_WARNING_SOFT, UI_WARNING, true);
    draw_dialog_button(pixels, stride, ptc_ui_pin_confirm_rect(), "+  确认",
                       UI_ACCENT, UI_ON_ACCENT, false);
    draw_dialog_button(pixels, stride, ptc_ui_pin_cancel_rect(), "B  取消",
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_pin_keyboard_rect(), "长按 + 传统键盘",
                       UI_RAISED, UI_INK, true);
    draw_text(pixels, stride, dialog.x + 590, dialog.y + 594,
              model->pin_error[0] ? model->pin_error : "短按 + 确认；长按 + 约 1 秒切换传统键盘",
              16, model->pin_error[0] ? UI_DANGER : UI_MUTED);
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

void draw_minute_editor_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *KEY_LABELS[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "X 退格", "0", "Y 清空"
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
    bool quota_unchanged = false;
    draw_dialog_shell(pixels, stride, model, &dialog, 920, 620);
    if (entered_valid) {
        if (clock) {
            snprintf(value, sizeof(value), "%02u:%02u", (unsigned int)(entered / 60u),
                     (unsigned int)(entered % 60u));
            snprintf(total_value, sizeof(total_value), "设定时间  %02u:%02u",
                     (unsigned int)(entered / 60u), (unsigned int)(entered % 60u));
        } else {
            snprintf(value, sizeof(value), "%u 分钟", (unsigned int)entered);
            snprintf(total_value, sizeof(total_value), "总计 %u 分钟", (unsigned int)entered);
        }
    } else {
        snprintf(value, sizeof(value), "暂不可用");
        snprintf(total_value, sizeof(total_value), "总计 -- 分钟");
    }
    snprintf(hours_value, sizeof(hours_value), "%s 小时",
             model->duration_hours_text[0] ? model->duration_hours_text : "--");
    snprintf(minutes_value, sizeof(minutes_value), "%s 分钟",
             model->duration_minutes_text[0] ? model->duration_minutes_text : "--");
    format_duration(model->played_minutes_available ? model->played_minutes : -1, played, sizeof(played));
    if (model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), "不限时");
    else format_duration(model->remaining_available ? model->remaining_minutes : -1, remaining, sizeof(remaining));
    if (weekly) {
        uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
        if (entered_valid && model->editor_index == weekday && model->played_minutes_available) {
            after_minutes = (int)entered - model->played_minutes;
        }
    } else if (entered_valid && model->numpad_purpose == PTC_UI_NUMPAD_MINUTES && model->played_minutes_available) {
        after_minutes = model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES && model->remaining_available
            ? model->remaining_minutes + entered : (int)entered - model->played_minutes;
    }
    if (after_minutes < 0 && model->played_minutes_available &&
        (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
         (model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES &&
          model->editor_index == ptc_weekday_from_day_index(model->day_index)))) after_minutes = 0;
    format_duration(after_minutes, after, sizeof(after));
    format_status_age(model, freshness, sizeof(freshness));
    if (entered_valid && model->numpad_purpose == PTC_UI_NUMPAD_MINUTES &&
        model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT) {
        PtcEffectiveRule current_rule = ptc_ui_plan_rule(model, PTC_UI_PLAN_SAVED);
        PtcDayRule requested_rule = {PTC_RULE_MODE_LIMIT, entered};
        quota_unchanged = !ptc_ui_day_rule_effectively_changed(current_rule.rule, requested_rule);
    }

    /* The compact editor keeps the keypad on the left and puts the two-part value on the right. */
    int guide_y = dialog.y + 116;
    draw_r_stick_glyph(pixels, stride, dialog.x + 548, guide_y - 11, 20, model->duration_scroll_dir);
    draw_text(pixels, stride, dialog.x + 576, guide_y + 4, "上下调整", 15, UI_MUTED);
    {
        char step_hint[32];
        if (model->duration_field == PTC_UI_DURATION_HOURS)
            snprintf(step_hint, sizeof(step_hint), "每步 1 小时");
        else snprintf(step_hint, sizeof(step_hint), "当前 ±%u", (unsigned)model->duration_step_feedback);
        draw_text(pixels, stride, dialog.x + 676, guide_y + 4, step_hint, 14, UI_ACCENT);
        draw_text(pixels, stride, dialog.x + 790, guide_y + 4, "L / R 选栏", 14, UI_MUTED);
    }

    for (int field = 0; field < 2; ++field) {
        UiRect rect = to_uirect(ptc_ui_minute_editor_field_rect((PtcUiDurationField)field));
        bool selected = model->duration_field == (PtcUiDurationField)field;
        fill_round_rect(pixels, stride, rect, 16, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, rect, 16, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);

        const char *key_name = field == PTC_UI_DURATION_HOURS ? "L" : "R";
        draw_shoulder_key_glyph(pixels, stride, rect.x + 14, rect.y + (rect.height - 22) / 2, 28, 22, key_name, !selected);

        UiRect text_rect = {rect.x + 44, rect.y, rect.width - 48, rect.height};
        draw_text_center(pixels, stride, text_rect,
                         field == PTC_UI_DURATION_HOURS ? hours_value : minutes_value,
                         25, selected ? UI_ACCENT : UI_INK);

        if (selected) {
            int arrow_cx = rect.x + rect.width / 2;
            bool up_active = model->duration_scroll_anim_ticks > 0 && model->duration_scroll_dir > 0;
            bool down_active = model->duration_scroll_anim_ticks > 0 && model->duration_scroll_dir < 0;
            draw_arrow_glyph(pixels, stride, arrow_cx, rect.y - 8 + (up_active ? -2 : 0), true,
                             up_active ? UI_ACCENT : UI_MUTED);
            draw_arrow_glyph(pixels, stride, arrow_cx, rect.y + rect.height + 8 + (down_active ? 2 : 0), false,
                             down_active ? UI_ACCENT : UI_MUTED);
        }
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 218, 350, 28},
                     total_value, 19, entered_valid ? UI_ACCENT : UI_DANGER);
    if (holiday) {
        PtcUiModel preview = *model;
        if (entered_valid) {
            if (model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES) {
                preview.draft_holiday_rule.minutes = entered;
            } else {
                preview.draft_makeup_workday_rule.minutes = entered;
            }
            draw_plan_impact_compact(pixels, stride, &preview, PTC_UI_PLAN_HOLIDAY,
                                     to_uirect(ptc_ui_minute_editor_summary_rect()));
        } else {
            draw_text(pixels, stride, dialog.x + 558, dialog.y + 310, "请先输入有效额度", 18, UI_RGB(UI_BLENDED(danger)));
        }
    } else if (weekly) {
        PtcUiModel preview = *model;
        if (entered_valid) {
            preview.draft_week[model->editor_index].minutes = entered;
            draw_plan_impact_compact(pixels, stride, &preview, PTC_UI_PLAN_WEEKLY,
                                     to_uirect(ptc_ui_minute_editor_summary_rect()));
        } else {
            draw_text(pixels, stride, dialog.x + 558, dialog.y + 310, "请先输入有效额度", 18, UI_RGB(UI_BLENDED(danger)));
        }
    } else if (scheduled) {
        PtcUiModel preview = *model;
        if (entered_valid) {
            preview.draft_scheduled_override.rule.mode = PTC_RULE_MODE_LIMIT;
            preview.draft_scheduled_override.rule.minutes = entered;
            draw_plan_impact_compact(pixels, stride, &preview, PTC_UI_PLAN_SCHEDULED,
                                     to_uirect(ptc_ui_minute_editor_summary_rect()));
        } else {
            draw_text(pixels, stride, dialog.x + 558, dialog.y + 310, "请先输入有效额度", 18, UI_RGB(UI_BLENDED(danger)));
        }
    } else if (grant) {
        PtcUiTimeProjection current_status;
        ptc_ui_project_time_status(model, ptc_ui_render_now(), &current_status);
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 264, 350, 74},
                             "当前状态", current_status.remaining_text,
                             time_projection_color(current_status.state));
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 350, 350, 74},
                             "下一枚代码时长", value,
                             entered_valid ? UI_ACCENT : UI_DANGER);
        draw_wrapped_text(pixels, stride, dialog.x + 548, dialog.y + 458,
            "只影响下一枚新代码；已生成代码保留签发时长。非法协议面额不会生成。",
            15, 326, 21, 3, UI_MUTED);
    } else if (clock) {
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 278, 350, 74},
                             "时刻范围", "00:00 - 23:59", UI_ACCENT);
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 364, 350, 74},
                             "快速调整", "ZL -15 分钟   ZR +15 分钟", UI_ACCENT);
        draw_wrapped_text(pixels, stride, dialog.x + 548, dialog.y + 472,
            "时间可跨过 00:00 循环；完成后仍会检查就寝窗口是否跨越午夜及是否与相邻日期冲突。",
            15, 326, 21, 3, UI_MUTED);
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES) {
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 264, 350, 74}, "额度已耗（估算）", played,
                             model->played_minutes_available ? UI_ACCENT : UI_WARNING);
        if (quota_unchanged) {
            draw_unchanged_quota_card(pixels, stride,
                (UiRect){dialog.x + 536, dialog.y + 350, 350, 74},
                "输入值与当前今日额度相同。");
        } else {
            draw_remaining_transition(pixels, stride,
                (UiRect){dialog.x + 536, dialog.y + 350, 350, 74},
                "当前剩余", remaining,
                time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                  model->unrestricted_today == 1, model->remaining_minutes),
                "操作后剩余", after,
                time_state_accent(after_minutes >= 0, false, after_minutes));
        }
        draw_text_center(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 438, 350, 28}, freshness, 16,
                         status_age_color(model));
    }

    for (int index = 0; index < 2; ++index) {
        const char *quick_label;
        if (grant) quick_label = index == 0 ? "ZL  上一档" : "ZR  下一档";
        else quick_label = index == 0 ? "ZL  -15 分钟" : "ZR  +15 分钟";
        draw_dialog_button(pixels, stride, ptc_ui_minute_editor_quick_rect(index), quick_label,
                           UI_RAISED, UI_ACCENT, true);
    }
    for (int index = 0; index < 12; ++index) {
        UiRect key = to_uirect(ptc_ui_minute_editor_key_rect(index));
        bool selected = index == model->numpad_cursor;
        fill_round_rect(pixels, stride, key, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, key, 12, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
        draw_text_center(pixels, stride, key, KEY_LABELS[index], index == 9 || index == 11 ? 18 : 28,
                         selected ? UI_ACCENT : UI_INK);
    }
    if (model->numpad_error[0]) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 488, 450, 28},
                         model->numpad_error, 18, UI_DANGER);
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 518, 450, 24},
                     "使用方向键与 A 键输入数字，或直接触摸", 16, UI_MUTED);
    draw_overlay_actions(pixels, stride, model, "+  完成输入");
}
