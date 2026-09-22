#include "ui_render_internal.h"

static float child_remaining_fraction(const PtcUiModel *model, bool *available)
{
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    *available = false;
    if (!fresh) return 0.0f;
    if (model->bedtime_active && !model->bedtime_skipped) {
        *available = true;
        return 0.0f;
    }
    if (model->unrestricted_today == 1) {
        *available = true;
        return 1.0f;
    }
    if (model->remaining_available && model->played_minutes_available &&
        model->remaining_minutes >= 0 && model->played_minutes >= 0 &&
        model->remaining_minutes + model->played_minutes > 0) {
        *available = true;
        return (float)model->displayed_remaining_minutes /
               (float)(model->remaining_minutes + model->played_minutes);
    }
    if (model->remaining_available && model->remaining_minutes > 0) {
        int shown = model->displayed_remaining_minutes > 120 ? 120 : model->displayed_remaining_minutes;
        if (shown < 0) shown = 0;
        *available = true;
        return (float)shown / 120.0f;
    }
    return 0.0f;
}

static void draw_child_calendar_icon(uint32_t *pixels, uint32_t stride, int cx, int cy, uint32_t color)
{
    draw_rect_outline(pixels, stride, (UiRect){cx - 9, cy - 8, 18, 16}, 3, 2, color);
    draw_line(pixels, stride, cx - 9, cy - 2, cx + 9, cy - 2, 2, color);
    draw_line(pixels, stride, cx - 4, cy - 10, cx - 4, cy - 7, 2, color);
    draw_line(pixels, stride, cx + 4, cy - 10, cx + 4, cy - 7, 2, color);
}

static void draw_child_day_card(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                UiRect card, const char *title, int forecast_index)
{
    char value[48];
    char source[64];
    char fitted[64];
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    bool available = fresh && model->forecast_available && forecast_index >= 0 && forecast_index < 7 &&
        model->forecast[forecast_index].day_index == model->day_index + forecast_index;
    fill_round_rect(pixels, stride, card, 12, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, card, 12, 1, UI_RGB(UI_BLENDED(border_control)));
    draw_child_calendar_icon(pixels, stride, card.x + 20, card.y + 22, UI_ACCENT);
    draw_text(pixels, stride, card.x + 38, card.y + 28, title, 16, UI_MUTED);
    if (available && forecast_index == 0 && model->unrestricted_today == 1) {
        snprintf(value, sizeof(value), "不限时");
        snprintf(source, sizeof(source), "%s", ui_rule_source_label(model->rule_source));
    } else if (available) {
        const PtcResultForecastDay *day = &model->forecast[forecast_index];
        if (day->mode == PTC_RULE_MODE_UNLIMITED) snprintf(value, sizeof(value), "不限时");
        else snprintf(value, sizeof(value), "%u 分钟", (unsigned int)day->minutes);
        snprintf(source, sizeof(source), "%s", ui_rule_source_label(day->rule_source));
    } else {
        snprintf(value, sizeof(value), "%s", fresh ? "暂不可用" : "待确认");
        snprintf(source, sizeof(source), "%s", fresh ? "尚未取得安排" : "状态需要刷新");
    }
    draw_text(pixels, stride, card.x + 16, card.y + 68, value, 25,
              available ? UI_INK : UI_MUTED);
    fit_text(fitted, sizeof(fitted), source, 13, card.width - 32);
    draw_text(pixels, stride, card.x + 16, card.y + 98, fitted, 13,
              available ? UI_ACCENT : UI_MUTED);
}

static void draw_child_budget_bar(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, UiRect box)
{
    char left[64];
    char right[64];
    UiRect slot = {box.x + 28, box.y + 384, box.width - 56, 10};
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    fill_round_rect(pixels, stride, slot, 5, UI_GAUGE_SLOT);
    draw_rect_outline(pixels, stride, slot, 5, 1, UI_GAUGE_SLOT_BORDER);
    if (bedtime_enforcing) {
        snprintf(left, sizeof(left), "就寝限制生效中");
        snprintf(right, sizeof(right), "今天暂停游玩");
    } else if (!fresh) {
        snprintf(left, sizeof(left), "今天的用时待确认");
        snprintf(right, sizeof(right), "刷新后显示");
    } else if (model->unrestricted_today == 1) {
        fill_round_rect(pixels, stride, slot, 5, UI_SUCCESS);
        snprintf(left, sizeof(left), "今天不限时");
        snprintf(right, sizeof(right), "合理安排休息");
    } else if (model->remaining_available && model->played_minutes_available &&
               model->remaining_minutes >= 0 && model->played_minutes >= 0 &&
               model->remaining_minutes + model->played_minutes > 0) {
        int total = model->remaining_minutes + model->played_minutes;
        int fill_w = (int)((int64_t)slot.width * model->displayed_remaining_minutes / total);
        uint32_t color = model->remaining_minutes <= 10 ? UI_DANGER :
            (model->remaining_minutes <= 30 ? UI_WARNING : UI_SUCCESS);
        if (fill_w < 4 && model->remaining_minutes > 0) fill_w = 4;
        if (fill_w > slot.width) fill_w = slot.width;
        if (fill_w > 0) fill_round_rect(pixels, stride, (UiRect){slot.x, slot.y, fill_w, slot.height}, 5, color);
        snprintf(left, sizeof(left), "已玩约 %d 分钟", model->played_minutes);
        snprintf(right, sizeof(right), "还剩 %d 分钟", model->remaining_minutes);
    } else {
        snprintf(left, sizeof(left), "今天的用时暂不可用");
        snprintf(right, sizeof(right), "稍后再刷新");
    }
    draw_text(pixels, stride, slot.x, slot.y - 14, "今天的时间", 15, UI_MUTED);
    draw_text(pixels, stride, slot.x, slot.y + 34, left, 14,
              bedtime_enforcing ? UI_DANGER : UI_MUTED);
    draw_text(pixels, stride, slot.x + slot.width - measure_text(right, 14), slot.y + 34, right, 14,
              bedtime_enforcing ? UI_DANGER : UI_MUTED);
}

static void draw_child_task_summary(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect box = to_uirect(ptc_ui_home_summary_rect(false));
    UiRect today_card = {box.x + 28, box.y + 218, 250, 122};
    UiRect tomorrow_card = {box.x + 290, box.y + 218, 262, 122};
    char remaining[64];
    char today[64];
    char line[160];
    char age[64];
    bool ring_available;
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    float fraction = child_remaining_fraction(model, &ring_available);
    uint32_t ring_color = UI_SUCCESS;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    if (bedtime_enforcing || (model->remaining_available && model->remaining_minutes <= 10)) ring_color = UI_DANGER;
    else if (model->remaining_available && model->remaining_minutes <= 30) ring_color = UI_WARNING;

    ptc_ui_format_home_remaining(model, ptc_ui_render_now(), remaining, sizeof(remaining));
    ptc_ui_format_today_mode(model, today, sizeof(today));
    draw_card_shadow(pixels, stride, box, 16);
    fill_round_rect(pixels, stride, box, 16, UI_RGB(UI_BLENDED(hero)));
    draw_text(pixels, stride, box.x + 28, box.y + 42,
              bedtime_enforcing ? "今天还可玩（就寝立断）" : "今天还可玩",
              22, UI_RGB(UI_BLENDED(hero_secondary)));
    {
        char *unit = strstr(remaining, " 分钟");
        if (unit) {
            *unit = '\0';
            int number_width = measure_text(remaining, 72);
            draw_text_bold(pixels, stride, box.x + 28, box.y + 132, remaining, 72,
                           UI_RGB(UI_BLENDED(on_hero)));
            draw_text(pixels, stride, box.x + 40 + number_width, box.y + 130, "分钟", 22,
                      UI_RGB(UI_BLENDED(hero_secondary)));
        } else {
            draw_wrapped_text(pixels, stride, box.x + 28, box.y + 120, remaining, 38,
                              300, 44, 2, UI_RGB(UI_BLENDED(on_hero)));
        }
    }
    draw_ring_progress(pixels, stride, box.x + box.width - 78, box.y + 92, 48, 9,
                       ring_available ? fraction : 0.0f,
                       UI_RGB(ui_mix_rgb(UI_BLENDED(hero), 0xFFFFFF, 20)), ring_color);
    draw_circle_outline(pixels, stride, box.x + box.width - 78, box.y + 92, 17, 2,
                        UI_RGB(UI_BLENDED(hero_secondary)));
    draw_line(pixels, stride, box.x + box.width - 78, box.y + 92,
              box.x + box.width - 78, box.y + 82, 2, UI_RGB(UI_BLENDED(hero_secondary)));
    draw_line(pixels, stride, box.x + box.width - 78, box.y + 92,
              box.x + box.width - 70, box.y + 96, 2, UI_RGB(UI_BLENDED(hero_secondary)));
    if (bedtime_enforcing) {
        snprintf(line, sizeof(line), "就寝时间生效中  /  %s",
                 model->status_loaded ? ui_rule_source_label(model->rule_source) : "待确认规则");
    } else {
        snprintf(line, sizeof(line), "今日%s  /  %s", today,
                 model->status_loaded ? ui_rule_source_label(model->rule_source) : "待确认规则");
    }
    draw_text(pixels, stride, box.x + 28, box.y + 176, line, 17,
              UI_RGB(UI_BLENDED(hero_secondary)));

    fill_round_rect(pixels, stride, (UiRect){box.x + 12, box.y + 200, box.width - 24, box.height - 212},
                    16, UI_SURFACE);
    draw_child_day_card(pixels, stride, model, today_card, "今天安排", 0);
    draw_child_day_card(pixels, stride, model, tomorrow_card, "明天安排", 1);
    draw_child_budget_bar(pixels, stride, model, box);
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, box.x + 28, box.y + box.height - 24, age, 13, UI_MUTED);
}

static void draw_child_action_icon(uint32_t *pixels, uint32_t stride, PtcUiRect target,
                                   int kind, bool primary, bool disabled)
{
    int cx = target.x + 34;
    int cy = target.y + target.h / 2;
    uint32_t color = disabled ? UI_DISABLED : (primary ? UI_ON_ACCENT : UI_ACCENT);
    uint32_t background = disabled ? UI_PAGE : (primary ? UI_RGB(ui_mix_rgb(UI_BLENDED(accent), 0xFFFFFF, 28)) : UI_SURFACE);
    fill_round_rect(pixels, stride, (UiRect){cx - 18, cy - 18, 36, 36}, 12, background);
    if (kind == 0) {
        draw_rect_outline(pixels, stride, (UiRect){cx - 10, cy - 8, 20, 16}, 4, 2, color);
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 4; ++column)
                fill_round_rect(pixels, stride, (UiRect){cx - 7 + column * 5, cy - 5 + row * 7, 3, 3}, 1, color);
    } else if (kind == 1) {
        draw_circle_outline(pixels, stride, cx - 2, cy - 1, 9, 2, color);
        draw_line(pixels, stride, cx - 7, cy + 7, cx + 8, cy - 8, 2, color);
        draw_line(pixels, stride, cx - 2, cy + 2, cx - 8, cy - 2, 2, color);
    } else {
        draw_child_calendar_icon(pixels, stride, cx, cy, color);
    }
}

static void draw_child_status_card(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect card = {684, 492, 516, 108};
    const char *runtime = ptc_ui_runtime_notice_summary(model);
    bool feedback = ptc_ui_operation_feedback_visible(model);
    bool error = strcmp(model->result_status, "error") == 0;
    bool alert = runtime[0] || feedback || ptc_ui_home_notice_expanded(model);
    uint32_t accent = (error || model->disable_flag_present) ? UI_DANGER :
        (model->waiting ? UI_WARNING : (alert ? UI_WARNING : UI_SUCCESS));
    uint32_t background = (error || model->disable_flag_present) ? UI_DANGER_SOFT :
        (model->waiting ? UI_WARNING_SOFT : UI_RGB(UI_BLENDED(surface_raised)));
    char age[64];
    const char *title = alert ? (runtime[0] ? runtime : (error ? "操作未完成" :
        (model->waiting ? "正在同步" : "状态提示"))) : "状态自动同步";
    const char *detail = "今天的安排会在后台保持更新";
    if (alert) {
        if (model->message[0] && strcmp(title, model->message) != 0) detail = model->message;
        else if (model->feedback_detail[0]) detail = model->feedback_detail;
        else if (model->waiting) detail = "请稍候，完成后即可继续操作";
        else detail = "需要帮助时，请家长查看支持与恢复";
    }
    fill_round_rect(pixels, stride, card, 12, background);
    draw_rect_outline(pixels, stride, card, 12, 1, alert ? accent : UI_BORDER);
    draw_status_symbol(pixels, stride, card.x + 18, card.y + 20, accent,
                       error ? 3 : (model->waiting ? 2 : 1));
    draw_wrapped_text(pixels, stride, card.x + 48, card.y + 28, title, 15,
                      card.width - 70, 19, 2, accent);
    draw_wrapped_text(pixels, stride, card.x + 48, card.y + 63, detail, 13,
                      card.width - 70, 18, 2, UI_MUTED);
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, card.x + card.width - 18 - measure_text(age, 12),
              card.y + card.height - 10, age, 12, UI_MUTED);
}

void draw_child(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char buffer[128], hint[160], fitted_hint[160];
    bool disabled = model->disable_flag_present || model->waiting;
    draw_header(pixels, stride, "自律即自由", "合理安排时间，做自己时间的主人");
    draw_time_status_bar(pixels, stride, model);
    draw_child_task_summary(pixels, stride, model);
    draw_card_shadow(pixels, stride, (UiRect){652, 120, 580, 496}, 16);
    fill_round_rect(pixels, stride, (UiRect){652, 120, 580, 496}, 16, UI_RGB(UI_BLENDED(surface)));
    draw_text(pixels, stride, 684, 164, "今天可以做什么？", 28, UI_RGB(UI_BLENDED(text_primary)));
    draw_text(pixels, stride, 684, 195, "需要更多时间时，从这里开始", 18, UI_RGB(UI_BLENDED(text_secondary)));
    home_button(pixels, stride, ptc_ui_child_submit_rect(),
        model->disable_flag_present ? "兑换暂不可用" : "A  输入加时码", true, false, disabled);
    draw_child_action_icon(pixels, stride, ptc_ui_child_submit_rect(), 0, true, disabled);
    if (model->daily_buffer_available)
        snprintf(buffer, sizeof(buffer), "X  领取自主缓冲  +%u 分钟", (unsigned int)model->daily_buffer_minutes);
    else snprintf(buffer, sizeof(buffer), "%s", model->daily_buffer_claimed ? "今日已使用缓冲" :
        (model->daily_buffer_minutes == 0 ? "今日自主缓冲未开启" : "自主缓冲仅可在限时日领取"));
    home_button(pixels, stride, ptc_ui_child_buffer_rect(), buffer, false, false,
        disabled || !model->daily_buffer_available);
    draw_child_action_icon(pixels, stride, ptc_ui_child_buffer_rect(), 1, false,
                           disabled || !model->daily_buffer_available);
    home_button(pixels, stride, ptc_ui_home_details_rect(false), "+  使用详情", false, false, model->waiting);
    draw_child_action_icon(pixels, stride, ptc_ui_home_details_rect(false), 2, false, model->waiting);
    draw_child_status_card(pixels, stride, model);
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_footer_rect(0)), "A  输入加时码", 18, disabled ? UI_DISABLED : UI_MUTED);
    if (model->show_parent_shortcut_hint && model->custom_shortcut_enabled)
        ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label, hint, sizeof(hint));
    else snprintf(hint, sizeof(hint), "状态会在后台自动同步");
    fit_text(fitted_hint, sizeof(fitted_hint), hint, 18, ptc_ui_child_footer_rect(1).w - 24);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), fitted_hint, 18, UI_RGB(UI_BLENDED(text_secondary)));
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), "B  退出");
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_refresh_rect()), "Y  刷新", 18, model->waiting ? UI_DISABLED : UI_MUTED);
}
