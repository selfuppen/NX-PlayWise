#include "ui_render_internal.h"

void format_duration(int minutes, char *out, size_t out_size)
{
    if (minutes < 0) {
        snprintf(out, out_size, "暂不可用");
        return;
    }
    if (minutes < 60) {
        snprintf(out, out_size, "%d 分钟", minutes);
    } else if (minutes % 60 == 0) {
        snprintf(out, out_size, "%d 小时", minutes / 60);
    } else {
        snprintf(out, out_size, "%d 小时 %d 分钟", minutes / 60, minutes % 60);
    }
}

uint32_t time_state_accent(bool available, bool unlimited, int minutes)
{
    if (!available) return UI_WARNING;
    if (unlimited) return UI_SUCCESS;
    if (minutes <= 0) return UI_DANGER;
    if (minutes <= 15) return UI_WARNING;
    return UI_SUCCESS;
}

void draw_time_state_card(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    const char *label,
    const char *value,
    uint32_t accent)
{
    int label_size = 18;
    int value_size = 23;
    while (label_size > 16 && measure_text(label, label_size) > rect.width - 16) --label_size;
    while (value_size > 17 && measure_text(value, value_size) > rect.width - 16) --value_size;
    fill_round_rect(pixels, stride, rect, 16, UI_RGB(UI_BLENDED(surface_raised)));
    draw_text_center(pixels, stride, (UiRect){rect.x + 8, rect.y + 10, rect.width - 16, 26}, label, label_size, UI_MUTED);
    draw_text_center(pixels, stride, (UiRect){rect.x + 8, rect.y + 38, rect.width - 16, 38}, value, value_size, accent);
}

void format_status_age(const PtcUiModel *model, char *out, size_t out_size)
{
    ptc_ui_format_status_age(model, ptc_ui_render_now(), out, out_size);
}

uint32_t status_age_color(const PtcUiModel *model)
{
    if (model->waiting) return UI_RGB(UI_BLENDED(warning));
    return UI_RGB(ptc_ui_status_is_fresh(model, ptc_ui_render_now())
        ? UI_BLENDED(text_secondary) : UI_BLENDED(warning));
}

const char *ui_rule_source_label(const char *source)
{
    if (!source || !source[0]) return "尚未刷新";
    if (strcmp(source, "today_override") == 0) return "今日额度调整";
    if (strcmp(source, "scheduled_override") == 0) return "临时额度计划";
    if (strcmp(source, "statutory_holiday") == 0) return "国家法定休假日";
    if (strcmp(source, "makeup_workday") == 0) return "国家调休工作日";
    return "周计划";
}

void draw_plan_card(uint32_t *pixels, uint32_t stride, UiRect card, bool focused)
{
    draw_card_shadow(pixels, stride, card, 16);
    fill_round_rect(pixels, stride, card, 16, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, card, 16, focused ? 3 : 1, UI_RGB(focused ? UI_BLENDED(focus) : UI_BLENDED(border_control)));
}

void draw_toggle_switch(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    bool is_on,
    bool selected,
    bool disabled,
    const char *on_label,
    const char *off_label)
{
    int radius = rect.height / 2;
    uint32_t bg_color = disabled ? UI_BORDER :
                       (is_on ? UI_SUCCESS : UI_BORDER);
    uint32_t knob_color = disabled ? UI_RAISED : UI_SURFACE;
    int knob_size = rect.height - 6;
    int off_x = rect.x + 3;
    int on_x = rect.x + rect.width - 3 - knob_size;
    /* 滑块按切换时刻缓动；首帧或预览直接落位（单调用点，矩形即身份）。 */
    static UiRect memo_rect = {0, 0, 0, 0};
    static bool memo_is_on = false;
    static int64_t memo_changed_ms = 0;
    float slide_t = 1.0f;
    int knob_x;
    if (memo_rect.x != rect.x || memo_rect.y != rect.y || memo_rect.width != rect.width) {
        memo_rect = rect;
        memo_is_on = is_on;
        memo_changed_ms = 0;
    } else if (memo_is_on != is_on) {
        memo_is_on = is_on;
        memo_changed_ms = ptc_ui_anim_now_ms();
    }
    if (memo_changed_ms != 0) {
        slide_t = ui_ease_out((float)(ptc_ui_anim_now_ms() - memo_changed_ms) / 120.0f);
        if (slide_t >= 1.0f) memo_changed_ms = 0;
    }
    knob_x = off_x + (int)((float)(on_x - off_x) * (is_on ? slide_t : 1.0f - slide_t));
    int knob_y = rect.y + 3;

    if (selected && !disabled) {
        fill_round_rect(pixels, stride, (UiRect){rect.x - 3, rect.y - 3, rect.width + 6, rect.height + 6},
                        radius + 3, UI_ACCENT);
    }
    fill_round_rect(pixels, stride, rect, radius, bg_color);
    fill_round_rect(pixels, stride, (UiRect){knob_x, knob_y, knob_size, knob_size}, knob_size / 2, knob_color);

    if (on_label && off_label) {
        const char *label = is_on ? on_label : off_label;
        uint32_t text_color = is_on ? UI_ON_ACCENT : UI_MUTED;
        int label_x = is_on ? (rect.x + 12) : (rect.x + knob_size + 8);
        draw_text(pixels, stride, label_x, rect.y + rect.height / 2 + 5, label, 15, text_color);
    }
}

void draw_transition_arrow(
    uint32_t *pixels, uint32_t stride, int cx, int cy, uint32_t color)
{
    draw_line(pixels, stride, cx - 9, cy, cx + 8, cy, 2, color);
    draw_line(pixels, stride, cx + 8, cy, cx + 3, cy - 5, 2, color);
    draw_line(pixels, stride, cx + 8, cy, cx + 3, cy + 5, 2, color);
}

void draw_remaining_transition(
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

void draw_unchanged_quota_card(
    uint32_t *pixels, uint32_t stride, UiRect rect, const char *reason)
{
    char fitted[192];
    fill_round_rect(pixels, stride, rect, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, rect, 16, 1, UI_BORDER);
    draw_text(pixels, stride, rect.x + 18, rect.y + 30, "今天额度不变", 17, UI_INK);
    fit_text(fitted, sizeof(fitted), reason, 14, rect.width - 36);
    draw_text(pixels, stride, rect.x + 18, rect.y + 58, fitted, 14, UI_MUTED);
}
