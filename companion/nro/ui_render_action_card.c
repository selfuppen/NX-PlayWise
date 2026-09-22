#include "ui_render_internal.h"

static void fill_circle_mini(uint32_t *pixels, uint32_t stride, float cx, float cy, float radius, uint32_t color)
{
    if (radius <= 0.0f) return;
    uint32_t resolved = resolve_color(color);
    int ir = (int)(radius + 1.5f);
    int icx = (int)(cx + 0.5f);
    int icy = (int)(cy + 0.5f);
    for (int dy = -ir; dy <= ir; ++dy) {
        for (int dx = -ir; dx <= ir; ++dx) {
            float px = (float)(icx + dx) + 0.5f;
            float py = (float)(icy + dy) + 0.5f;
            float dist = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
            float delta = radius + 0.5f - dist;
            if (delta > 0.0f) {
                float cov = delta > 1.0f ? 1.0f : delta;
                blend_pixel(pixels, stride, icx + dx, icy + dy, resolved, (uint8_t)(cov * 255.0f + 0.5f));
            }
        }
    }
}

static void draw_crescent_moon(uint32_t *pixels, uint32_t stride, int cx, int cy, float r_out, float r_in,
                               float offset_x, float offset_y, uint32_t color)
{
    uint32_t resolved = resolve_color(color);
    int ir = (int)(r_out + 2.0f);
    for (int dy = -ir; dy <= ir; ++dy) {
        for (int dx = -ir; dx <= ir; ++dx) {
            float dist_out = sqrtf((float)(dx * dx + dy * dy));
            float delta_out = r_out + 0.5f - dist_out;
            if (delta_out <= 0.0f) continue;
            float in_dx = (float)dx - offset_x;
            float in_dy = (float)dy - offset_y;
            float dist_in = sqrtf(in_dx * in_dx + in_dy * in_dy);
            float delta_in = dist_in - (r_in - 0.5f);
            if (delta_in <= 0.0f) continue;
            float cov = delta_out < delta_in ? delta_out : delta_in;
            if (cov > 1.0f) cov = 1.0f;
            blend_pixel(pixels, stride, cx + dx, cy + dy, resolved, (uint8_t)(cov * 255.0f + 0.5f));
        }
    }
}

static void draw_infinity_smooth(uint32_t *pixels, uint32_t stride, int cx, int cy, float a, int stroke, uint32_t color)
{
    const int N = 36;
    const float two_pi = 6.2831853f;
    float prev_x = (float)cx + a;
    float prev_y = (float)cy;
    for (int i = 1; i <= N; ++i) {
        float t = (float)i * two_pi / (float)N;
        float s = sinf(t);
        float c = cosf(t);
        float denom = 1.0f + s * s;
        float x = (float)cx + (a * c) / denom;
        float y = (float)cy + (a * s * c) / denom;
        draw_line(pixels, stride, (int)(prev_x + 0.5f), (int)(prev_y + 0.5f),
                  (int)(x + 0.5f), (int)(y + 0.5f), stroke, color);
        prev_x = x;
        prev_y = y;
    }
}

static void draw_restore_arc(uint32_t *pixels, uint32_t stride, int cx, int cy, float radius, int stroke, uint32_t color)
{
    const int N = 24;
    const float start_angle = 0.6f;
    const float end_angle = 4.65f;
    float prev_x = (float)cx + radius * cosf(start_angle);
    float prev_y = (float)cy + radius * sinf(start_angle);
    for (int i = 1; i <= N; ++i) {
        float t = start_angle + (end_angle - start_angle) * (float)i / (float)N;
        float x = (float)cx + radius * cosf(t);
        float y = (float)cy + radius * sinf(t);
        draw_line(pixels, stride, (int)(prev_x + 0.5f), (int)(prev_y + 0.5f),
                  (int)(x + 0.5f), (int)(y + 0.5f), stroke, color);
        prev_x = x;
        prev_y = y;
    }
    /* Solid, crisp counter-clockwise arrowhead at the top pointing left */
    int tip_x = cx - 3;
    int tip_y = cy - (int)radius;
    int base_x = cx + 2;
    draw_line(pixels, stride, tip_x, tip_y, base_x, tip_y - 5, stroke, color);
    draw_line(pixels, stride, tip_x, tip_y, base_x, tip_y + 5, stroke, color);
    draw_line(pixels, stride, base_x, tip_y - 5, base_x, tip_y + 5, stroke, color);
    draw_line(pixels, stride, tip_x + 2, tip_y - 2, tip_x + 2, tip_y + 2, stroke, color);
    draw_line(pixels, stride, tip_x + 4, tip_y - 3, tip_x + 4, tip_y + 3, stroke, color);
}

static void draw_card_action_icon(uint32_t *pixels, uint32_t stride, int cx, int cy,
                                  UiActionIcon icon, uint32_t color)
{
    switch (icon) {
    case UI_ACTION_ICON_CLOCK:
        draw_circle_outline(pixels, stride, cx, cy, 11, 2, color);
        fill_circle_mini(pixels, stride, (float)cx, (float)cy, 2.0f, color);
        draw_line(pixels, stride, cx, cy, cx, cy - 6, 2, color);
        draw_line(pixels, stride, cx, cy, cx + 5, cy + 1, 2, color);
        draw_line(pixels, stride, cx, cy - 11, cx, cy - 9, 2, color);
        break;
    case UI_ACTION_ICON_ADD_TIME:
        draw_circle_outline(pixels, stride, cx - 2, cy - 2, 9, 2, color);
        fill_circle_mini(pixels, stride, (float)(cx - 2), (float)(cy - 2), 1.5f, color);
        draw_line(pixels, stride, cx - 2, cy - 2, cx - 2, cy - 6, 2, color);
        draw_line(pixels, stride, cx - 2, cy - 2, cx + 2, cy - 2, 2, color);
        fill_circle_mini(pixels, stride, (float)(cx + 6), (float)(cy + 5), 5.0f, color);
        draw_line(pixels, stride, cx + 3, cy + 5, cx + 9, cy + 5, 2, UI_ON_ACCENT);
        draw_line(pixels, stride, cx + 6, cy + 2, cx + 6, cy + 8, 2, UI_ON_ACCENT);
        break;
    case UI_ACTION_ICON_INFINITY:
        draw_infinity_smooth(pixels, stride, cx, cy, 10.5f, 2, color);
        break;
    case UI_ACTION_ICON_RESTORE:
        draw_restore_arc(pixels, stride, cx, cy, 9.5f, 2, color);
        break;
    case UI_ACTION_ICON_MOON:
        draw_crescent_moon(pixels, stride, cx - 1, cy, 10.5f, 9.0f, 4.0f, -2.5f, color);
        draw_line(pixels, stride, cx + 5, cy - 5, cx + 5, cy - 1, 1, color);
        draw_line(pixels, stride, cx + 3, cy - 3, cx + 7, cy - 3, 1, color);
        draw_line(pixels, stride, cx + 8, cy + 1, cx + 8, cy + 5, 1, color);
        draw_line(pixels, stride, cx + 6, cy + 3, cx + 10, cy + 3, 1, color);
        break;
    case UI_ACTION_ICON_BUFFER:
        draw_rect_outline(pixels, stride, (UiRect){cx - 10, cy - 6, 17, 12}, 4, 2, color);
        draw_line(pixels, stride, cx + 8, cy - 2, cx + 8, cy + 2, 2, color);
        draw_line(pixels, stride, cx - 4, cy, cx + 1, cy, 2, color);
        draw_line(pixels, stride, cx - 1, cy - 3, cx - 1, cy + 3, 2, color);
        break;
    case UI_ACTION_ICON_CALENDAR_RANGE:
    case UI_ACTION_ICON_HOLIDAY:
    case UI_ACTION_ICON_WEEKLY:
        draw_rect_outline(pixels, stride, (UiRect){cx - 10, cy - 8, 20, 18}, 3, 2, color);
        draw_line(pixels, stride, cx - 10, cy - 2, cx + 10, cy - 2, 2, color);
        draw_line(pixels, stride, cx - 5, cy - 10, cx - 5, cy - 7, 2, color);
        draw_line(pixels, stride, cx + 5, cy - 10, cx + 5, cy - 7, 2, color);
        if (icon == UI_ACTION_ICON_CALENDAR_RANGE) {
            draw_line(pixels, stride, cx - 6, cy + 4, cx + 6, cy + 4, 2, color);
            draw_line(pixels, stride, cx - 6, cy + 4, cx - 3, cy + 1, 2, color);
            draw_line(pixels, stride, cx + 6, cy + 4, cx + 3, cy + 7, 2, color);
        } else if (icon == UI_ACTION_ICON_HOLIDAY) {
            draw_line(pixels, stride, cx, cy + 1, cx, cy + 7, 2, color);
            draw_line(pixels, stride, cx - 3, cy + 4, cx + 3, cy + 4, 2, color);
            draw_line(pixels, stride, cx - 2, cy + 2, cx + 2, cy + 6, 1, color);
            draw_line(pixels, stride, cx + 2, cy + 2, cx - 2, cy + 6, 1, color);
        } else {
            for (int r = 0; r < 2; ++r) {
                for (int c = 0; c < 3; ++c) {
                    fill_circle_mini(pixels, stride, (float)(cx - 5 + c * 5), (float)(cy + 1 + r * 5), 1.3f, color);
                }
            }
        }
        break;
    case UI_ACTION_ICON_KEY:
        draw_circle_outline(pixels, stride, cx - 4, cy - 4, 6, 2, color);
        fill_circle_mini(pixels, stride, (float)(cx - 4), (float)(cy - 4), 2.0f, UI_PAGE);
        draw_line(pixels, stride, cx + 1, cy + 1, cx + 9, cy + 9, 2, color);
        draw_line(pixels, stride, cx + 5, cy + 5, cx + 8, cy + 2, 2, color);
        draw_line(pixels, stride, cx + 7, cy + 7, cx + 10, cy + 4, 2, color);
        break;
    case UI_ACTION_ICON_DEVICE:
        draw_rect_outline(pixels, stride, (UiRect){cx - 7, cy - 10, 14, 20}, 4, 2, color);
        draw_line(pixels, stride, cx - 3, cy - 7, cx + 3, cy - 7, 1, color);
        draw_line(pixels, stride, cx - 3, cy + 6, cx + 3, cy + 6, 2, color);
        break;
    case UI_ACTION_ICON_CONSOLE:
        draw_rect_outline(pixels, stride, (UiRect){cx - 6, cy - 7, 12, 14}, 2, 2, color);
        draw_line(pixels, stride, cx - 8, cy - 7, cx - 11, cy - 5, 2, color);
        draw_line(pixels, stride, cx - 11, cy - 5, cx - 11, cy + 5, 2, color);
        draw_line(pixels, stride, cx - 11, cy + 5, cx - 8, cy + 7, 2, color);
        draw_line(pixels, stride, cx - 8, cy + 7, cx - 8, cy - 7, 2, color);
        fill_circle_mini(pixels, stride, (float)(cx - 9.5f), (float)(cy - 2.0f), 1.3f, color);
        draw_line(pixels, stride, cx + 8, cy - 7, cx + 11, cy - 5, 2, color);
        draw_line(pixels, stride, cx + 11, cy - 5, cx + 11, cy + 5, 2, color);
        draw_line(pixels, stride, cx + 11, cy + 5, cx + 8, cy + 7, 2, color);
        draw_line(pixels, stride, cx + 8, cy + 7, cx + 8, cy - 7, 2, color);
        fill_circle_mini(pixels, stride, (float)(cx + 9.5f), (float)(cy + 2.0f), 1.3f, color);
        break;
    case UI_ACTION_ICON_SLIDERS:
        draw_line(pixels, stride, cx - 10, cy - 6, cx + 10, cy - 6, 2, color);
        draw_line(pixels, stride, cx - 10, cy, cx + 10, cy, 2, color);
        draw_line(pixels, stride, cx - 10, cy + 6, cx + 10, cy + 6, 2, color);
        fill_circle_mini(pixels, stride, (float)cx - 4.0f, (float)cy - 6.0f, 3.2f, color);
        fill_circle_mini(pixels, stride, (float)cx + 4.0f, (float)cy, 3.2f, color);
        fill_circle_mini(pixels, stride, (float)cx - 1.0f, (float)cy + 6.0f, 3.2f, color);
        break;
    case UI_ACTION_ICON_HISTORY:
        draw_circle_outline(pixels, stride, cx, cy, 9, 2, color);
        fill_circle_mini(pixels, stride, (float)cx, (float)cy, 1.5f, color);
        draw_line(pixels, stride, cx, cy, cx, cy - 5, 2, color);
        draw_line(pixels, stride, cx, cy, cx + 4, cy, 2, color);
        draw_line(pixels, stride, cx - 9, cy - 6, cx - 5, cy - 9, 2, color);
        draw_line(pixels, stride, cx - 9, cy - 6, cx - 9, cy - 1, 2, color);
        break;
    case UI_ACTION_ICON_THEME:
        draw_circle_outline(pixels, stride, cx, cy, 10, 2, color);
        fill_circle_mini(pixels, stride, (float)cx - 4.0f, (float)cy - 3.5f, 1.8f, color);
        fill_circle_mini(pixels, stride, (float)cx + 2.5f, (float)cy - 4.0f, 1.8f, color);
        fill_circle_mini(pixels, stride, (float)cx + 4.5f, (float)cy + 1.5f, 1.8f, color);
        fill_circle_mini(pixels, stride, (float)cx - 2.0f, (float)cy + 4.0f, 2.2f, UI_PAGE);
        draw_circle_outline(pixels, stride, cx - 2, cy + 4, 2, 1, color);
        break;
    case UI_ACTION_ICON_CONTROLLER:
        draw_rect_outline(pixels, stride, (UiRect){cx - 10, cy - 5, 20, 12}, 4, 2, color);
        draw_line(pixels, stride, cx - 8, cy - 7, cx - 4, cy - 7, 2, color);
        draw_line(pixels, stride, cx + 4, cy - 7, cx + 8, cy - 7, 2, color);
        draw_line(pixels, stride, cx - 6, cy + 1, cx - 2, cy + 1, 2, color);
        draw_line(pixels, stride, cx - 4, cy - 1, cx - 4, cy + 3, 2, color);
        fill_circle_mini(pixels, stride, (float)cx + 3.5f, (float)cy, 1.4f, color);
        fill_circle_mini(pixels, stride, (float)cx + 6.5f, (float)cy + 2.0f, 1.4f, color);
        break;
    case UI_ACTION_ICON_HOMEBREW:
        draw_rect_outline(pixels, stride, (UiRect){cx - 10, cy - 8, 20, 16}, 3, 2, color);
        draw_line(pixels, stride, cx - 6, cy - 3, cx - 3, cy, 2, color);
        draw_line(pixels, stride, cx - 3, cy, cx - 6, cy + 3, 2, color);
        draw_line(pixels, stride, cx - 1, cy + 3, cx + 5, cy + 3, 2, color);
        break;
    case UI_ACTION_ICON_ACTIVITY:
        draw_line(pixels, stride, cx - 9, cy + 8, cx + 9, cy + 8, 2, color);
        fill_round_rect(pixels, stride, (UiRect){cx - 8, cy + 2, 3, 6}, 1, color);
        fill_round_rect(pixels, stride, (UiRect){cx - 3, cy - 5, 3, 13}, 1, color);
        fill_round_rect(pixels, stride, (UiRect){cx + 2, cy - 1, 3, 9}, 1, color);
        fill_round_rect(pixels, stride, (UiRect){cx + 7, cy - 7, 3, 15}, 1, color);
        break;
    case UI_ACTION_ICON_SHIELD:
        draw_line(pixels, stride, cx - 9, cy - 8, cx, cy - 6, 2, color);
        draw_line(pixels, stride, cx, cy - 6, cx + 9, cy - 8, 2, color);
        draw_line(pixels, stride, cx - 9, cy - 8, cx - 9, cy - 1, 2, color);
        draw_line(pixels, stride, cx + 9, cy - 8, cx + 9, cy - 1, 2, color);
        draw_line(pixels, stride, cx - 9, cy - 1, cx, cy + 9, 2, color);
        draw_line(pixels, stride, cx + 9, cy - 1, cx, cy + 9, 2, color);
        draw_line(pixels, stride, cx - 4, cy, cx - 1, cy + 3, 2, color);
        draw_line(pixels, stride, cx - 1, cy + 3, cx + 4, cy - 3, 2, color);
        break;
    case UI_ACTION_ICON_REPAIR:
        draw_circle_outline(pixels, stride, cx - 4, cy - 4, 6, 2, color);
        fill_circle_mini(pixels, stride, (float)(cx - 6), (float)(cy - 6), 2.8f, UI_PAGE);
        draw_line(pixels, stride, cx - 1, cy - 1, cx + 8, cy + 8, 3, color);
        fill_circle_mini(pixels, stride, (float)(cx + 8), (float)(cy + 8), 1.8f, color);
        break;
    case UI_ACTION_ICON_STOP:
        draw_line(pixels, stride, cx - 4, cy - 9, cx + 4, cy - 9, 2, color);
        draw_line(pixels, stride, cx + 4, cy - 9, cx + 9, cy - 4, 2, color);
        draw_line(pixels, stride, cx + 9, cy - 4, cx + 9, cy + 4, 2, color);
        draw_line(pixels, stride, cx + 9, cy + 4, cx + 4, cy + 9, 2, color);
        draw_line(pixels, stride, cx + 4, cy + 9, cx - 4, cy + 9, 2, color);
        draw_line(pixels, stride, cx - 4, cy + 9, cx - 9, cy + 4, 2, color);
        draw_line(pixels, stride, cx - 9, cy + 4, cx - 9, cy - 4, 2, color);
        draw_line(pixels, stride, cx - 9, cy - 4, cx - 4, cy - 9, 2, color);
        draw_line(pixels, stride, cx - 5, cy, cx + 5, cy, 3, color);
        break;
    case UI_ACTION_ICON_EXPORT:
        draw_line(pixels, stride, cx, cy + 4, cx, cy - 8, 2, color);
        draw_line(pixels, stride, cx, cy - 8, cx - 4, cy - 4, 2, color);
        draw_line(pixels, stride, cx, cy - 8, cx + 4, cy - 4, 2, color);
        draw_line(pixels, stride, cx - 8, cy - 1, cx - 8, cy + 7, 2, color);
        draw_line(pixels, stride, cx - 8, cy + 7, cx + 8, cy + 7, 2, color);
        draw_line(pixels, stride, cx + 8, cy + 7, cx + 8, cy - 1, 2, color);
        break;
    case UI_ACTION_ICON_INFO:
        draw_circle_outline(pixels, stride, cx, cy, 11, 2, color);
        fill_circle_mini(pixels, stride, (float)cx, (float)cy - 5.0f, 1.7f, color);
        draw_line(pixels, stride, cx, cy - 1, cx, cy + 6, 2, color);
        draw_line(pixels, stride, cx - 2, cy - 1, cx, cy - 1, 2, color);
        draw_line(pixels, stride, cx - 3, cy + 6, cx + 3, cy + 6, 2, color);
        break;
    default:
        draw_circle_outline(pixels, stride, cx, cy, 8, 2, color);
        break;
    }
}

static void draw_action_visual(uint32_t *pixels, uint32_t stride, UiRect area,
                               const UiAction *action, bool disabled)
{
    uint32_t ink = disabled ? UI_DISABLED : action->accent;
    if (action->visual == UI_ACTION_VISUAL_QUICK_ADD) {
        static const char *LABELS[] = {"+15", "+30", "+60", "自定义"};
        int gap = 4;
        int width = (area.width - gap * 3) / 4;
        for (int index = 0; index < 4; ++index) {
            UiRect chip = {area.x + index * (width + gap), area.y, width, area.height};
            fill_round_rect(pixels, stride, chip, 6, disabled ? UI_PAGE : UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, chip, 6, 1, disabled ? UI_DISABLED : UI_SUCCESS);
            draw_text_center(pixels, stride, chip, LABELS[index], index == 3 ? 11 : 13, ink);
        }
    } else if (action->visual == UI_ACTION_VISUAL_THEME) {
        static const char *LABELS[] = {"系统", "浅色", "暗色"};
        const char *selected = action->subtitle ? action->subtitle : "";
        int gap = 5;
        int width = (area.width - gap * 2) / 3;
        for (int index = 0; index < 3; ++index) {
            bool active = strstr(selected, LABELS[index]) != NULL;
            UiRect chip = {area.x + index * (width + gap), area.y, width, area.height};
            fill_round_rect(pixels, stride, chip, 6, active ? UI_ACCENT_SOFT : UI_RAISED);
            draw_rect_outline(pixels, stride, chip, 6, active ? 2 : 1,
                              disabled ? UI_DISABLED : (active ? UI_ACCENT : UI_BORDER));
            draw_text_center(pixels, stride, chip, LABELS[index], 12,
                             disabled ? UI_DISABLED : (active ? UI_ACCENT : UI_MUTED));
        }
    }
}

void draw_action_card(uint32_t *pixels, uint32_t stride, UiRect rect,
    const UiAction *action, bool selected, PtcUiActionState state, int reserved_right)
{
    bool disabled = state == PTC_UI_ACTION_DISABLED;
    bool recommended = state == PTC_UI_ACTION_RECOMMENDED;
    bool compact = rect.height < 90;
    int badge_size = compact ? 34 : 44;
    int title_size = compact ? 20 : 22;
    int sub_size = compact ? 14 : 15;
    int text_x = rect.x + 18 + badge_size + 16;
    int title_width = rect.width - (text_x - rect.x) - (recommended ? 64 : 16) - reserved_right;
    int sub_width = rect.width - (text_x - rect.x) - 16;
    if (title_width < 100) title_width = 100;
    if (sub_width < 100) sub_width = 100;

    uint32_t background = disabled ? UI_RAISED : (selected ? UI_ACCENT_SOFT : UI_SURFACE);
    draw_card_shadow(pixels, stride, rect, 16);
    fill_round_rect(pixels, stride, rect, 16, background);
    if (selected) {
        draw_focus_ring(pixels, stride, rect, 16);
    } else {
        draw_rect_outline(pixels, stride, rect, 16, 1, UI_BORDER);
    }

    bool has_sub = action->subtitle && action->subtitle[0];
    bool has_visual = action->visual != UI_ACTION_VISUAL_NONE;
    int text_block_h = title_size + ((has_sub || has_visual) ? (compact ? 31 : 36) : 0);
    int text_top = rect.y + (rect.height - text_block_h) / 2;
    if (text_top < rect.y + 12) text_top = rect.y + 12;
    int icon_cx = rect.x + 18 + badge_size / 2;
    int icon_cy = text_top + text_block_h / 2;
    UiRect badge_rect = {icon_cx - badge_size / 2, icon_cy - badge_size / 2, badge_size, badge_size};
    uint32_t badge_bg = disabled ? UI_PAGE :
        (action->accent == UI_SUCCESS ? UI_SUCCESS_SOFT :
        (action->accent == UI_DANGER ? UI_DANGER_SOFT :
        (action->accent == UI_WARNING ? UI_WARNING_SOFT : UI_ACCENT_SOFT)));
    fill_round_rect(pixels, stride, badge_rect, 12, badge_bg);
    draw_rect_outline(pixels, stride, badge_rect, 12, 1, UI_BORDER);
    draw_card_action_icon(pixels, stride, icon_cx, icon_cy, action->icon,
                          disabled ? UI_DISABLED : action->accent);

    char fitted[128];
    fit_text(fitted, sizeof(fitted), action->title, title_size, title_width);
    draw_text(pixels, stride, text_x, text_top + title_size, fitted, title_size,
              disabled ? UI_DISABLED : UI_INK);

    if (has_visual) {
        draw_action_visual(pixels, stride,
            (UiRect){text_x, text_top + title_size + 8, sub_width, compact ? 24 : 28},
            action, disabled);
    } else if (has_sub) {
        fit_text(fitted, sizeof(fitted), action->subtitle, sub_size, sub_width);
        draw_text(pixels, stride, text_x, text_top + title_size + sub_size + 7, fitted, sub_size,
                  disabled ? UI_DISABLED : UI_MUTED);
    }

    if (recommended && !disabled) {
        fill_round_rect(pixels, stride, (UiRect){rect.x + rect.width - 66, rect.y + 8, 56, 24}, 6, UI_SUCCESS);
        draw_text_center(pixels, stride, (UiRect){rect.x + rect.width - 66, rect.y + 8, 56, 24}, "建议", 16, UI_ON_ACCENT);
    }
}
