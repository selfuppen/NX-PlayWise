#include "ui_render_internal.h"

UiRuntime g_ui;
const PtcUiPalette *g_palette;
PtcUiThemeView g_theme = {
    PTC_UI_THEME_SYSTEM,
    PTC_UI_RESOLVED_LIGHT,
    NULL,
    false
};

int64_t ptc_ui_render_now(void)
{
#ifdef PTC_UI_PREVIEW_WALL_TIME
    return PTC_UI_PREVIEW_WALL_TIME;
#else
    return (int64_t)time(NULL);
#endif
}

uint16_t ptc_ui_render_minute_of_day(int64_t now)
{
#ifdef PTC_UI_PREVIEW_MINUTE_OF_DAY
    (void)now;
    /* Match the fixed 08:16 clock shown in host preview headers. */
    return PTC_UI_PREVIEW_MINUTE_OF_DAY;
#else
    time_t clock_value = (time_t)now;
    struct tm *local = localtime(&clock_value);
    return local ? (uint16_t)(local->tm_hour * 60 + local->tm_min) : 0;
#endif
}


bool is_docked_mode(void)
{
#if defined(__SWITCH__) && !defined(PLAYWISE_EDEN)
    return appletGetOperationMode() == AppletOperationMode_Console;
#else
    return false;
#endif
}

void draw_header(uint32_t *pixels, uint32_t stride, const char *title, const char *subtitle)
{
    fill_round_rect(pixels, stride, (UiRect){54, 25, 48, 48}, 16, UI_ACCENT);
    fill_round_rect(pixels, stride, (UiRect){87, 24, 12, 12}, 6, UI_CORAL);
    draw_line(pixels, stride, 65, 49, 79, 49, 3, UI_ON_ACCENT);
    draw_line(pixels, stride, 72, 42, 72, 56, 3, UI_ON_ACCENT);
    draw_circle_outline(pixels, stride, 90, 44, 3, 3, UI_ON_ACCENT);
    draw_circle_outline(pixels, stride, 86, 55, 3, 3, UI_ON_ACCENT);
    draw_text(pixels, stride, 124, 49, title, 30, UI_INK);
    char fitted_sub[256];
    fit_text(fitted_sub, sizeof(fitted_sub), subtitle, 18, 615);
    draw_text(pixels, stride, 124, 77, fitted_sub, 18, UI_MUTED);
}

uint32_t time_projection_color(PtcUiTimeState state)
{
    switch (state) {
    case PTC_UI_TIME_NORMAL: return UI_SUCCESS;
    case PTC_UI_TIME_REMINDER: return UI_WARNING;
    case PTC_UI_TIME_DANGER:
    case PTC_UI_TIME_EXHAUSTED:
    case PTC_UI_TIME_DISABLED:
    case PTC_UI_TIME_PROTECTION: return UI_DANGER;
    case PTC_UI_TIME_UNLIMITED: return UI_ACCENT;
    case PTC_UI_TIME_RECOVERY:
    case PTC_UI_TIME_TEMPORARY_UNLOCK:
    case PTC_UI_TIME_WAITING: return UI_WARNING;
    default: return UI_MUTED;
    }
}

typedef struct {
    const char *label;
    uint32_t color;
    uint32_t bg_color;
} UiActiveRuleBadge;

static UiActiveRuleBadge get_active_rule_badge(const PtcUiModel *model)
{
    UiActiveRuleBadge badge;
    if (model->bedtime_active && !model->bedtime_skipped) {
        badge.label = ptc_ui_text(PTC_UI_T_BEDTIME_RESTRICTIONS);
        badge.color = UI_DANGER;
        badge.bg_color = UI_DANGER_SOFT;
        return badge;
    }
    if (!model->status_loaded) {
        badge.label = ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED);
        badge.color = UI_MUTED;
        badge.bg_color = UI_PAGE;
        return badge;
    }
    if (strcmp(model->rule_source, "today_override") == 0) {
        badge.label = ptc_ui_text(PTC_UI_T_TODAY_S_ADJUSTMENT);
        badge.color = UI_ACCENT;
        badge.bg_color = UI_ACCENT_SOFT;
    } else if (strcmp(model->rule_source, "scheduled_override") == 0) {
        badge.label = ptc_ui_text(PTC_UI_T_SPECIFIED_DATE_QUOTA);
        badge.color = UI_ACCENT;
        badge.bg_color = UI_ACCENT_SOFT;
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0) {
        badge.label = ptc_ui_text(PTC_UI_T_LEGAL_HOLIDAYS);
        badge.color = UI_SUCCESS;
        badge.bg_color = UI_SUCCESS_SOFT;
    } else if (strcmp(model->rule_source, "makeup_workday") == 0) {
        badge.label = ptc_ui_text(PTC_UI_T_COMPENSATION_WORK);
        badge.color = UI_SUCCESS;
        badge.bg_color = UI_SUCCESS_SOFT;
    } else {
        badge.label = ptc_ui_text(PTC_UI_T_RULE_WEEKLY);
        badge.color = UI_ACCENT;
        badge.bg_color = UI_ACCENT_SOFT;
    }
    return badge;
}

void draw_time_status_bar(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    PtcUiTimeProjection status;
    UiRect box = to_uirect(ptc_ui_time_status_bar_rect());
    char fitted_fresh[64];
    bool show_eye_cycle = model && model->eye_care_policy.enabled;
    uint32_t color;
    UiActiveRuleBadge badge;

    ptc_ui_project_time_status(model, ptc_ui_render_now(), &status);
    if (show_eye_cycle) box.height = 78;
    color = time_projection_color(status.state);
    badge = get_active_rule_badge(model);
    if (!ptc_ui_status_is_fresh(model, ptc_ui_render_now())) {
        badge.label = ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED);
        badge.color = UI_MUTED;
        badge.bg_color = UI_PAGE;
    }

    fill_round_rect(pixels, stride, box, 14, UI_SURFACE);
    draw_rect_outline(pixels, stride, box, 14, 1, UI_BORDER);

    /* --- 第一层：时钟、日期星期、生效规则胶囊徽章、更新时效 --- */
    /* 1. 时钟显示 (突出粗体) */
    draw_text_bold(pixels, stride, box.x + 14, box.y + 21, status.clock_text, 16, UI_INK);
    int clock_w = measure_text(status.clock_text, 16);

    /* 2. 微细竖向分隔线 */
    int div_x = box.x + 14 + clock_w + 9;
    draw_line(pixels, stride, div_x, box.y + 10, div_x, box.y + 22, 1, UI_BORDER);

    /* 3. 日期与星期显示 */
    int date_x = div_x + 9;
    draw_text(pixels, stride, date_x, box.y + 21, status.date_text, 13, UI_MUTED);
    int date_w = measure_text(status.date_text, 13);

    /* 4. 当前生效规则胶囊徽章 (随日期动态自适应排版) */
    int badge_text_w = measure_text(badge.label, 11);
    int pill_w = badge_text_w + 22;
    if (pill_w < 64) pill_w = 64;
    UiRect pill = {date_x + date_w + 10, box.y + 7, pill_w, 19};
    int right_limit = box.x + box.width - 14 - 110;
    if (pill.x + pill.width > right_limit && pill.width > 50) {
        pill.width = right_limit - pill.x;
    }
    fill_round_rect(pixels, stride, pill, 5, badge.bg_color);
    draw_rect_outline(pixels, stride, pill, 5, 1, badge.color);
    fill_round_rect(pixels, stride, (UiRect){pill.x + 6, pill.y + 6, 5, 5}, 2, badge.color);
    char fitted_badge[64];
    int max_badge_w = pill.width - 20;
    if (max_badge_w < 10) max_badge_w = 10;
    fit_text(fitted_badge, sizeof(fitted_badge), badge.label, 11, max_badge_w);
    draw_text(pixels, stride, pill.x + 15, box.y + 21, fitted_badge, 11, badge.color);

    /* 4b. 屏幕形态胶囊徽章 (电视模式 / 掌机桌面) */
    if (model && model->dock_available && ptc_ui_status_is_fresh(model, ptc_ui_render_now())) {
        bool is_tv = (strcmp(model->operation_mode, "docked") == 0);
        bool is_undocked = (strcmp(model->operation_mode, "undocked") == 0);
        const char *m_label = is_tv ? ptc_ui_text(PTC_UI_T_DOCK_TV_BADGE) :
            (is_undocked ? ptc_ui_text(PTC_UI_T_DOCK_HANDHELD_BADGE) : NULL);
        if (m_label) {
            uint32_t m_color = is_tv ? UI_SUCCESS : (model->dock_restriction_active ? UI_DANGER : UI_ACCENT);
            uint32_t m_bg = is_tv ? UI_SUCCESS_SOFT : (model->dock_restriction_active ? UI_DANGER_SOFT : UI_ACCENT_SOFT);
            int m_tw = measure_text(m_label, 11);
            int m_pw = m_tw + 16;
            if (m_pw < 48) m_pw = 48;
            UiRect m_rect = {pill.x + pill.width + 8, box.y + 7, m_pw, 19};
            if (m_rect.x + m_rect.width <= right_limit) {
                fill_round_rect(pixels, stride, m_rect, 5, m_bg);
                draw_rect_outline(pixels, stride, m_rect, 5, 1, m_color);
                draw_text_center(pixels, stride, m_rect, m_label, 11, m_color);
            }
        }
    }

    /* 5. 数据更新时效 (右对齐) */
    fit_text(fitted_fresh, sizeof(fitted_fresh), status.freshness_text, 12, 110);
    int fresh_w = measure_text(fitted_fresh, 12);
    draw_text(pixels, stride, box.x + box.width - 14 - fresh_w, box.y + 21, fitted_fresh, 12, UI_MUTED);

    /* --- 第二层：核心游玩额度状态 (前缀次级色与数值高亮加粗) 与 演示模式徽章 --- */
    const char *pfx_zh = ptc_ui_text(PTC_UI_T_ALSO_AVAILABLE_TO_PLAY_TODAY);
    const char *pfx_en = "Available today: ";
    bool is_pfx_zh = strncmp(status.remaining_text, pfx_zh, strlen(pfx_zh)) == 0;
    bool is_pfx_en = strncmp(status.remaining_text, pfx_en, strlen(pfx_en)) == 0;
    int rem_max_w = box.width - 28 - (model->demo_secret_enabled ? 48 : 0);
    if (is_pfx_zh || is_pfx_en) {
        const char *pfx = is_pfx_zh ? pfx_zh : pfx_en;
        size_t pfx_len = strlen(pfx);
        char fitted_rem[64];
        const char *val = status.remaining_text + pfx_len;
        int pfx_w = measure_text(pfx, 15);
        draw_text(pixels, stride, box.x + 14, box.y + 43, pfx, 15, UI_MUTED);
        fit_text(fitted_rem, sizeof(fitted_rem), val, 15, rem_max_w - pfx_w);
        draw_text_bold(pixels, stride, box.x + 14 + pfx_w, box.y + 43, fitted_rem, 15, color);
    } else {
        char fitted_rem[64];
        fit_text(fitted_rem, sizeof(fitted_rem), status.remaining_text, 15, rem_max_w);
        draw_text_bold(pixels, stride, box.x + 14, box.y + 43, fitted_rem, 15, color);
    }

    if (model->demo_secret_enabled) {
        UiRect dbadge = {box.x + box.width - 14 - 38, box.y + 30, 38, 18};
        fill_round_rect(pixels, stride, dbadge, 5, UI_DANGER_SOFT);
        draw_text_center(pixels, stride, dbadge, ptc_ui_text(PTC_UI_T_DEMO), 11, UI_DANGER);
    }

    /* --- 第三层：全宽精致圆角额度进度槽 (置于卡片下沿基线) --- */
    if (show_eye_cycle) {
        char cycle[128], fitted_cycle[128];
        ptc_ui_format_eye_care_cycle(model, ptc_ui_render_now(), cycle, sizeof(cycle));
        fit_text(fitted_cycle, sizeof(fitted_cycle), cycle, 11, box.width - 28);
        draw_text(pixels, stride, box.x + 14, box.y + 61, fitted_cycle, 11,
                  strcmp(model->eye_care_phase, "resting") == 0 ? UI_DANGER : UI_ACCENT);
    }
    UiRect track = {box.x + 14, box.y + (show_eye_cycle ? 69 : 53), box.width - 28, 5};
    fill_round_rect(pixels, stride, track, 2, UI_RAISED);
    if (status.progress_available && status.progress_per_mille > 0) {
        int width = track.width * status.progress_per_mille / 1000;
        if (width < 4) width = 4;
        if (width > track.width) width = track.width;
        fill_round_rect(pixels, stride, (UiRect){track.x, track.y, width, track.height}, 2, color);
    }
}

static void draw_single_key_glyph(uint32_t *pixels, uint32_t stride, int x, int y, int size, const char *key_str, bool disabled)
{
    int d = size;
    int r = d / 2;
    uint32_t bg_col;
    uint32_t border_col;
    uint32_t fg_col;

    if (disabled) {
        bg_col = UI_RAISED;
        border_col = UI_BORDER;
        fg_col = UI_DISABLED;
    } else {
        bg_col = UI_KEY_GLYPH_BG;
        border_col = UI_KEY_GLYPH_BORDER;
        fg_col = UI_INK;
    }
    fill_round_rect(pixels, stride, (UiRect){x, y, d, d}, r, bg_col);
    draw_rect_outline(pixels, stride, (UiRect){x, y, d, d}, r, 1, border_col);

    if (strcmp(key_str, "+") == 0) {
        int cx = x + r, cy = y + r;
        draw_line(pixels, stride, cx - 4, cy, cx + 4, cy, 2, fg_col);
        draw_line(pixels, stride, cx, cy - 4, cx, cy + 4, 2, fg_col);
    } else if (strcmp(key_str, "-") == 0) {
        int cx = x + r, cy = y + r;
        draw_line(pixels, stride, cx - 4, cy, cx + 4, cy, 2, fg_col);
    } else {
        draw_text_center(pixels, stride, (UiRect){x, y, d, d}, key_str, 14, fg_col);
    }
}

void draw_shoulder_key_glyph(uint32_t *pixels, uint32_t stride, int x, int y, int width, int height, const char *key_str, bool disabled)
{
    uint32_t bg_col;
    uint32_t border_col;
    uint32_t fg_col;

    if (disabled) {
        bg_col = UI_RAISED;
        border_col = UI_BORDER;
        fg_col = UI_DISABLED;
    } else {
        bg_col = UI_KEY_GLYPH_BG;
        border_col = UI_KEY_GLYPH_BORDER;
        fg_col = UI_INK;
    }
    fill_round_rect(pixels, stride, (UiRect){x, y, width, height}, 5, bg_col);
    draw_rect_outline(pixels, stride, (UiRect){x, y, width, height}, 5, 1, border_col);
    draw_text_center(pixels, stride, (UiRect){x, y, width, height}, key_str, 13, fg_col);
}

void draw_arrow_glyph(uint32_t *pixels, uint32_t stride, int cx, int cy, bool up, uint32_t color)
{
    int h = 5;
    for (int dy = 0; dy <= h; ++dy) {
        int span = up ? dy : (h - dy);
        int y = up ? (cy - h / 2 + dy) : (cy - h / 2 + dy);
        draw_line(pixels, stride, cx - span, y, cx + span, y, 1, color);
    }
}

void draw_r_stick_axis_glyph(uint32_t *pixels, uint32_t stride, int x, int y, int size, bool vertical, int dir)
{
    int r = size / 2;
    int cx = x + r;
    int cy = y + r;
    uint32_t bg_col = UI_KEY_GLYPH_BG;
    uint32_t border_col = UI_KEY_GLYPH_BORDER;
    uint32_t fg_col = UI_INK;

    fill_round_rect(pixels, stride, (UiRect){x, y, size, size}, r, bg_col);
    draw_rect_outline(pixels, stride, (UiRect){x, y, size, size}, r, 1, border_col);

    if (vertical) {
        int offset_y = dir > 0 ? -1 : (dir < 0 ? 1 : 0);
        draw_text_center(pixels, stride, (UiRect){x, y + offset_y, size, size}, "R", 12, fg_col);
        draw_line(pixels, stride, cx, y - 4, cx, y - 2, 1, dir > 0 ? UI_ACCENT : UI_MUTED);
        draw_line(pixels, stride, cx, y + size + 2, cx, y + size + 4, 1, dir < 0 ? UI_ACCENT : UI_MUTED);
    } else {
        int offset_x = dir > 0 ? 1 : (dir < 0 ? -1 : 0);
        draw_text_center(pixels, stride, (UiRect){x + offset_x, y, size, size}, "R", 12, fg_col);
        draw_line(pixels, stride, x - 4, cy, x - 2, cy, 1, dir < 0 ? UI_ACCENT : UI_MUTED);
        draw_line(pixels, stride, x + size + 2, cy, x + size + 4, cy, 1, dir > 0 ? UI_ACCENT : UI_MUTED);
    }
}

void draw_r_stick_glyph(uint32_t *pixels, uint32_t stride, int x, int y, int size, int dir)
{
    draw_r_stick_axis_glyph(pixels, stride, x, y, size, true, dir);
}

void draw_button_label(uint32_t *pixels, uint32_t stride, UiRect box, const char *label, int size, uint32_t color)
{
    char localized[512];
    if (!label || !*label) return;
    label = ptc_ui_localize(label, localized, sizeof(localized));

    bool disabled = (color == UI_DISABLED);

    /* 匹配复合肩键 "L/R  " 或 "L/R " */
    if (strncmp(label, "L/R  ", 5) == 0 || strncmp(label, "L/R ", 4) == 0) {
        const char *rest = strncmp(label, "L/R  ", 5) == 0 ? label + 5 : label + 4;
        int slash_w = measure_text("/", 14);
        int key_w = 24 + 4 + slash_w + 4 + 24 + 8;
        int cur_size = size;
        int rest_w = measure_text(rest, cur_size);
        int total_w = key_w + rest_w;
        while (total_w > box.width - 8 && cur_size > 11) {
            cur_size--;
            rest_w = measure_text(rest, cur_size);
            total_w = key_w + rest_w;
        }
        char fitted_rest[128];
        if (total_w > box.width - 6) {
            fit_text(fitted_rest, sizeof(fitted_rest), rest, cur_size, box.width - key_w - 6);
            rest = fitted_rest;
            rest_w = measure_text(rest, cur_size);
            total_w = key_w + rest_w;
        }
        int start_x = box.x + (box.width - total_w) / 2;
        if (start_x < box.x + 2) start_x = box.x + 2;
        int gly_y = box.y + (box.height - 20) / 2;
        int baseline = box.y + (box.height + cur_size - 4) / 2;

        draw_shoulder_key_glyph(pixels, stride, start_x, gly_y, 24, 20, "L", disabled);
        draw_text(pixels, stride, start_x + 28, baseline, "/", 14, color);
        draw_shoulder_key_glyph(pixels, stride, start_x + 28 + slash_w + 4, gly_y, 24, 20, "R", disabled);
        draw_text(pixels, stride, start_x + key_w, baseline, rest, cur_size, color);
        return;
    }

    /* 匹配单肩键 "ZL  ", "ZR  " 或 "ZL ", "ZR " */
    if ((strncmp(label, "ZL", 2) == 0 || strncmp(label, "ZR", 2) == 0) && label[2] == ' ') {
        char key_buf[4];
        memcpy(key_buf, label, 2);
        key_buf[2] = '\0';
        int pfx_len = (label[3] == ' ') ? 4 : 3;
        const char *rest = label + pfx_len;
        int key_w = 32 + 8;
        int cur_size = size;
        int rest_w = measure_text(rest, cur_size);
        int total_w = key_w + rest_w;
        while (total_w > box.width - 8 && cur_size > 11) {
            cur_size--;
            rest_w = measure_text(rest, cur_size);
            total_w = key_w + rest_w;
        }
        char fitted_rest[128];
        if (total_w > box.width - 6) {
            fit_text(fitted_rest, sizeof(fitted_rest), rest, cur_size, box.width - key_w - 6);
            rest = fitted_rest;
            rest_w = measure_text(rest, cur_size);
            total_w = key_w + rest_w;
        }
        int start_x = box.x + (box.width - total_w) / 2;
        if (start_x < box.x + 2) start_x = box.x + 2;
        int gly_y = box.y + (box.height - 20) / 2;
        int baseline = box.y + (box.height + cur_size - 4) / 2;

        draw_shoulder_key_glyph(pixels, stride, start_x, gly_y, 32, 20, key_buf, disabled);
        draw_text(pixels, stride, start_x + key_w, baseline, rest, cur_size, color);
        return;
    }

    /* 匹配单字符圆键 "A  ", "B  ", "X  ", "Y  ", "+  ", "-  " 或单空格 "A ", "B ", ... */
    if ((label[0] == 'A' || label[0] == 'B' || label[0] == 'X' || label[0] == 'Y' ||
         label[0] == '+' || label[0] == '-') && label[1] == ' ') {
        char key_buf[2] = {label[0], '\0'};
        int pfx_len = (label[2] == ' ') ? 3 : 2;
        const char *rest = label + pfx_len;
        int key_w = 22 + 8;
        int cur_size = size;
        int rest_w = measure_text(rest, cur_size);
        int total_w = key_w + rest_w;
        while (total_w > box.width - 8 && cur_size > 11) {
            cur_size--;
            rest_w = measure_text(rest, cur_size);
            total_w = key_w + rest_w;
        }
        char fitted_rest[128];
        if (total_w > box.width - 6) {
            fit_text(fitted_rest, sizeof(fitted_rest), rest, cur_size, box.width - key_w - 6);
            rest = fitted_rest;
            rest_w = measure_text(rest, cur_size);
            total_w = key_w + rest_w;
        }
        int start_x = box.x + (box.width - total_w) / 2;
        if (start_x < box.x + 2) start_x = box.x + 2;
        int gly_y = box.y + (box.height - 22) / 2;
        int baseline = box.y + (box.height + cur_size - 4) / 2;

        draw_single_key_glyph(pixels, stride, start_x, gly_y, 22, key_buf, disabled);
        draw_text(pixels, stride, start_x + key_w, baseline, rest, cur_size, color);
        return;
    }

    /* 普通文本居中展示 */
    draw_text_center(pixels, stride, box, label, size, color);
}

void draw_footer_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect, const char *label)
{
    draw_button_label(pixels, stride, to_uirect(rect), label, 18, UI_MUTED);
}

void draw_parent_status_footer(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect box = to_uirect(ptc_ui_parent_footer_rect(4));
    char summary[96];
    uint32_t color = UI_DANGER;

    if (!ptc_ui_parent_status_alert_visible(model)) return;
    if (ptc_ui_operation_feedback_visible(model)) return;
    if (model->disable_flag_present) {
        snprintf(summary, sizeof(summary), ptc_ui_text(PTC_UI_T_CONTROLS_DISABLED_PRESS_A_FOR_RECOVERY));
    } else if (model->recovery_active) {
        snprintf(summary, sizeof(summary), ptc_ui_text(PTC_UI_T_RECOVERY_INCOMPLETE_PRESS_A_FOR_ACTIONS));
    } else if (strcmp(model->setup_phase, "protection") == 0) {
        snprintf(summary, sizeof(summary), ptc_ui_text(PTC_UI_T_PROTECTION_ACTIVE_PRESS_A_FOR_DETAILS));
    } else if (model->temporary_unlocked_available && model->temporary_unlocked) {
        snprintf(summary, sizeof(summary), ptc_ui_text(PTC_UI_T_TEMPORARILY_UNLOCKED_PRESS_A_TO_MANAGE));
        color = UI_WARNING;
    } else {
        snprintf(summary, sizeof(summary), ptc_ui_text(PTC_UI_T_SYSTEM_ALERT_PRESS_A_FOR_SUPPORT));
    }

    fill_round_rect(pixels, stride, box, 12,
        color == UI_WARNING ? UI_WARNING_SOFT : UI_DANGER_SOFT);
    draw_rect_outline(pixels, stride, box, 12, 1, color);
    if (model->parent_footer_focused && model->parent_footer_selection == 1) {
        fill_round_rect(pixels, stride, box, 12, UI_RGB(UI_BLENDED(focus)));
        fill_round_rect(pixels, stride, (UiRect){box.x + 3, box.y + 3, box.width - 6, box.height - 6},
            9, UI_RGB(UI_BLENDED(surface_raised)));
    }
    draw_text_center(pixels, stride, box, summary, 17, color);
}

UiRect to_uirect(PtcUiRect rect)
{
    UiRect out = {rect.x, rect.y, rect.w, rect.h};
    return out;
}

void draw_dialog_button(
    uint32_t *pixels,
    uint32_t stride,
    PtcUiRect rect,
    const char *label,
    uint32_t background,
    uint32_t foreground,
    bool outline)
{
    UiRect box = to_uirect(rect);
    fill_round_rect(pixels, stride, box, 12, outline ? UI_RGB(UI_BLENDED(surface_raised)) : background);
    draw_button_label(pixels, stride, box, label, 21, foreground);
}

void draw_candidate_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect,
    const char *label, uint32_t background, uint32_t foreground, bool selected, bool disabled)
{
    UiRect box = to_uirect(rect);
    bool primary = foreground == UI_ON_ACCENT;
    uint32_t fill = disabled ? UI_RAISED : (selected && !primary ? UI_ACCENT_SOFT : background);
    fill_round_rect(pixels, stride, box, 12, fill);
    if (selected) draw_focus_ring(pixels, stride, box, 12);
    else draw_rect_outline(pixels, stride, box, 12, 1, UI_CONTROL);

    int text_size = 20;
    int tw = measure_text(label, text_size);
    while (tw > box.width - 16 && text_size > 14) {
        text_size--;
        tw = measure_text(label, text_size);
    }
    draw_button_label(pixels, stride, box, label, text_size, disabled ? UI_DISABLED : foreground);
}

void draw_overlay_actions(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, const char *confirm_label)
{
    PtcUiRect confirm = ptc_ui_confirm_rect(model->overlay);
    bool hold = model->confirm_hold_required && model->overlay == PTC_UI_OVERLAY_CONFIRM;
    if (hold) {
        UiRect button = to_uirect(confirm);
        char progress_label[48];
        fill_round_rect(pixels, stride, button, 12, UI_ACCENT);
        if (model->confirm_hold_progress > 0) {
            UiRect progress = button;
            progress.width = progress.width * model->confirm_hold_progress / 1000;
            fill_round_rect(pixels, stride, progress, 12, UI_SUCCESS);
            snprintf(progress_label, sizeof(progress_label), ptc_ui_text(PTC_UI_T_A_KEEP_HOLDING_U),
                     (unsigned int)(model->confirm_hold_progress / 10));
        } else {
            snprintf(progress_label, sizeof(progress_label), ptc_ui_text(PTC_UI_T_A_PRESS_AND_HOLD_FOR_1_SECOND));
        }
        draw_text_center(pixels, stride, button,
                         model->confirm_hold_progress >= 1000 ? ptc_ui_text(PTC_UI_T_A_CONFIRMATION_COMPLETED) : progress_label,
                         20, UI_ON_ACCENT);
        draw_rect_outline(pixels, stride, button, 12, 2,
                          model->confirm_hold_progress > 0 ? UI_SUCCESS : UI_ACCENT);
    } else {
        draw_dialog_button(pixels, stride, confirm, confirm_label, UI_ACCENT, UI_ON_ACCENT, false);
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_CANCEL), UI_RAISED, UI_INK, true);
    if (model->overlay == PTC_UI_OVERLAY_CONFIRM &&
        (model->operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
         model->operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
         model->operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY)) {
        PtcUiRect selected = model->overlay_selection == 0
            ? ptc_ui_cancel_rect(model->overlay) : ptc_ui_confirm_rect(model->overlay);
        draw_rect_outline(pixels, stride, to_uirect(selected), 12, 3, UI_ACCENT);
    }
}

void format_event_time(int64_t timestamp, bool full, char *out, size_t out_size)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint16_t event_day;
    uint16_t today;
    uint16_t minute;
    if (!out || out_size == 0) return;
    snprintf(out, out_size, ptc_ui_text(PTC_UI_T_TIME_UNKNOWN));
    if (timestamp <= 0) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_TIME_UNKNOWN));
        return;
    }
    {
        time_t event_time = (time_t)timestamp;
        time_t now_time = (time_t)ptc_ui_render_now();
        struct tm event_local;
        struct tm today_local;
        if (localtime_r(&event_time, &event_local) == NULL ||
            localtime_r(&now_time, &today_local) == NULL ||
            !ptc_day_index_from_date((uint16_t)(event_local.tm_year + 1900),
                (uint8_t)(event_local.tm_mon + 1), (uint8_t)event_local.tm_mday, &event_day) ||
            !ptc_day_index_from_date((uint16_t)(today_local.tm_year + 1900),
                (uint8_t)(today_local.tm_mon + 1), (uint8_t)today_local.tm_mday, &today)) return;
        minute = (uint16_t)(event_local.tm_hour * 60 + event_local.tm_min);
    }
    if (full && ptc_date_from_day_index(event_day, &year, &month, &day)) {
        snprintf(out, out_size, "%u-%02u-%02u %02u:%02u", year, month, day, minute / 60, minute % 60);
    } else if (event_day == today) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_TODAY_02U_02U), minute / 60, minute % 60);
    } else if ((uint16_t)(event_day + 1u) == today) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_YESTERDAY_02U_02U), minute / 60, minute % 60);
    } else if (ptc_date_from_day_index(event_day, &year, &month, &day)) {
        snprintf(out, out_size, "%u-%02u-%02u %02u:%02u", year, month, day, minute / 60, minute % 60);
    } else {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_TIME_UNKNOWN));
    }
}

void draw_notice(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    PtcUiNoticeProjection notice;
    ptc_ui_project_notice(model, &notice);
    if (!notice.visible) return;
    bool danger = notice.level == PTC_UI_NOTICE_DANGER;
    bool warning = notice.level == PTC_UI_NOTICE_WARNING;
    uint32_t accent = danger ? UI_DANGER : (warning ? UI_WARNING : UI_SUCCESS);

    /* 共享的右下角状态胶囊只显示摘要，详情由家长主动打开。 */
    PtcUiRect n_rect = ptc_ui_notice_rect();
    UiRect box = to_uirect(n_rect);
    int radius = box.height / 2;
    draw_round_rect_shadow(pixels, stride, box, radius, 12, 40, 3);
    fill_round_rect(pixels, stride, box, radius, danger ? UI_DANGER_SOFT : (warning ? UI_WARNING_SOFT : UI_SURFACE));
    draw_rect_outline(pixels, stride, box, radius, 1, danger ? UI_DANGER : (warning ? UI_WARNING : UI_BORDER));

    PtcUiRect icon_rect = ptc_ui_notice_status_icon_rect(n_rect.y);
    int icon_cx = icon_rect.x + icon_rect.w / 2;
    int icon_cy = icon_rect.y + icon_rect.h / 2;
    draw_status_symbol(pixels, stride, icon_cx, icon_cy, accent, danger ? 3 : (warning ? 2 : 1));

    if (notice.has_details) {
        UiRect detail_btn = to_uirect(ptc_ui_notice_details_rect());
        fill_round_rect(pixels, stride, detail_btn, 8, danger ? UI_DANGER : (warning ? UI_WARNING : UI_ACCENT_SOFT));
        draw_rect_outline(pixels, stride, detail_btn, 8, 1, danger ? UI_DANGER : (warning ? UI_WARNING : UI_ACCENT));
        draw_text_center(pixels, stride, detail_btn, ptc_ui_text(PTC_UI_T_X_DETAILS), 13, (danger || warning) ? UI_ON_ACCENT : UI_ACCENT);
    }

    int max_text_w = box.width - 50 - (notice.has_details ? 88 : 16);
    char fitted_msg[128];
    fit_text(fitted_msg, sizeof(fitted_msg), notice.summary, 15, max_text_w);
    int baseline = box.y + (box.height + 15) / 2 - 2;
    draw_text(pixels, stride, box.x + 44, baseline, fitted_msg, 15, danger ? UI_DANGER : UI_INK);
}

void draw_notice_details_dialog(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell_model = *model;
    shell_model.overlay_title[0] = '\0';
    shell_model.overlay_body[0] = '\0';
    PtcUiNoticeProjection notice;
    ptc_ui_project_notice(model, &notice);
    bool danger = notice.level == PTC_UI_NOTICE_DANGER;
    bool warning = notice.level == PTC_UI_NOTICE_WARNING;
    uint32_t accent = danger ? UI_DANGER : (warning ? UI_WARNING : UI_SUCCESS);

    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 780, 420);

    int icon_cx = dialog.x + 48;
    int icon_cy = dialog.y + 70;
    draw_status_symbol(pixels, stride, icon_cx, icon_cy, accent, danger ? 3 : (warning ? 2 : 1));

    const char *msg = notice.summary[0] ? notice.summary : ptc_ui_text(PTC_UI_T_OPERATION_STATUS_AND_FEEDBACK);
    char fitted_msg[192];
    fit_text(fitted_msg, sizeof(fitted_msg), msg, 20, dialog.width - 108);
    draw_text(pixels, stride, dialog.x + 72, dialog.y + 76, fitted_msg, 20, danger ? UI_DANGER : UI_INK);

    UiRect card = {dialog.x + 36, dialog.y + 104, dialog.width - 72, 230};
    fill_round_rect(pixels, stride, card, 12, UI_PAGE);
    draw_rect_outline(pixels, stride, card, 12, 1, UI_BORDER);

    int card_base = card.y + 36;
    if (notice.details[0]) {
        draw_text(pixels, stride, card.x + 20, card_base, ptc_ui_text(PTC_UI_T_DETAILED_INFORMATION_AND_TROUBLESHOOTING_GUIDELINES), 16, UI_ACCENT);
        card_base = draw_wrapped_text(pixels, stride, card.x + 20, card_base + 32,
            notice.details, 16, card.width - 40, 24, 4, UI_INK);
    } else {
        draw_text(pixels, stride, card.x + 20, card_base, ptc_ui_text(PTC_UI_T_OPERATION_COMPLETED_NO_FURTHER_DETAILS_AVAILABLE), 16, UI_MUTED);
        card_base += 32;
    }

    if (model->command_name[0]) {
        char execution[256];
        char cmd_buf[128], tr_buf[128];
        const char *cmd = model->command_name[0] ? ptc_ui_localize(model->command_name, cmd_buf, sizeof(cmd_buf)) : ptc_ui_text(PTC_UI_T_NONE);
        const char *tr = model->transport_label[0] ? ptc_ui_localize(model->transport_label, tr_buf, sizeof(tr_buf)) : ptc_ui_text(PTC_UI_T_DEFAULT);
        snprintf(execution, sizeof(execution), ptc_ui_text(PTC_UI_T_COMMAND_S_TRANSPORT_S), cmd, tr);
        draw_text(pixels, stride, card.x + 20, card.y + card.height - 24, execution, 14, UI_MUTED);
    }

    PtcUiRect btn_rect = ptc_ui_cancel_rect(model->overlay);
    draw_dialog_button(pixels, stride, btn_rect, ptc_ui_text(PTC_UI_T_A_B_CLOSE), UI_ACCENT, UI_ON_ACCENT, false);
}

void home_button(uint32_t *pixels, uint32_t stride, PtcUiRect target,
    const char *label, bool primary, bool selected, bool disabled)
{
    UiRect box = to_uirect(target);
    uint32_t fill = disabled ? UI_RAISED : (primary ? UI_ACCENT : UI_ACCENT_SOFT);
    fill_round_rect(pixels, stride, box, 12, fill);
    if (selected) draw_focus_ring(pixels, stride, box, 12);
    draw_button_label(pixels, stride, box, label, target.h <= 48 ? 18 : 22,
        disabled ? UI_DISABLED : (primary ? UI_ON_ACCENT : UI_ACCENT));
}
