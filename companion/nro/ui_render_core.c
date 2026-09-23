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
    draw_text(pixels, stride, 124, 77, subtitle, 18, UI_MUTED);
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
        badge.label = "就寝限制";
        badge.color = UI_DANGER;
        badge.bg_color = UI_DANGER_SOFT;
        return badge;
    }
    if (!model->status_loaded) {
        badge.label = "待确认";
        badge.color = UI_MUTED;
        badge.bg_color = UI_PAGE;
        return badge;
    }
    if (strcmp(model->rule_source, "today_override") == 0) {
        badge.label = "今日调整";
        badge.color = UI_ACCENT;
        badge.bg_color = UI_ACCENT_SOFT;
    } else if (strcmp(model->rule_source, "scheduled_override") == 0) {
        badge.label = "临时计划";
        badge.color = UI_ACCENT;
        badge.bg_color = UI_ACCENT_SOFT;
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0) {
        badge.label = "法定假日";
        badge.color = UI_SUCCESS;
        badge.bg_color = UI_SUCCESS_SOFT;
    } else if (strcmp(model->rule_source, "makeup_workday") == 0) {
        badge.label = "调休工作";
        badge.color = UI_SUCCESS;
        badge.bg_color = UI_SUCCESS_SOFT;
    } else {
        badge.label = "周计划";
        badge.color = UI_ACCENT;
        badge.bg_color = UI_ACCENT_SOFT;
    }
    return badge;
}

void draw_time_status_bar(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    PtcUiTimeProjection status;
    UiRect box = {754, 24, 472, 66};
    char fitted[64];
    char fitted_fresh[64];
    uint32_t color;
    UiActiveRuleBadge badge;

    ptc_ui_project_time_status(model, ptc_ui_render_now(), &status);
    color = time_projection_color(status.state);
    badge = get_active_rule_badge(model);

    fill_round_rect(pixels, stride, box, 14, UI_SURFACE);
    draw_rect_outline(pixels, stride, box, 14, 1, UI_BORDER);

    /* 时钟显示 */
    draw_text(pixels, stride, box.x + 14, box.y + 26, status.clock_text, 17, UI_INK);

    /* 当前生效规则胶囊徽章 */
    UiRect pill = {box.x + 68, box.y + 11, 74, 22};
    fill_round_rect(pixels, stride, pill, 6, badge.bg_color);
    draw_rect_outline(pixels, stride, pill, 6, 1, badge.color);
    fill_round_rect(pixels, stride, (UiRect){pill.x + 6, pill.y + 8, 6, 6}, 3, badge.color);
    draw_text(pixels, stride, pill.x + 16, box.y + 26, badge.label, 12, badge.color);

    /* 剩余/状态文本 */
    fit_text(fitted, sizeof(fitted), status.remaining_text, 16, 185);
    draw_text(pixels, stride, box.x + 150, box.y + 26, fitted, 16, color);

    /* 更新时效 */
    fit_text(fitted_fresh, sizeof(fitted_fresh), status.freshness_text, 12, 110);
    int fresh_w = measure_text(fitted_fresh, 12);
    draw_text(pixels, stride, box.x + box.width - 14 - fresh_w, box.y + 26, fitted_fresh, 12, UI_MUTED);

    /* 额度进度槽 */
    int demo_w = model->demo_secret_enabled ? 46 : 0;
    UiRect track = {box.x + 14, box.y + 46, box.width - 28 - demo_w, 6};
    fill_round_rect(pixels, stride, track, 3, UI_RAISED);
    if (status.progress_available && status.progress_per_mille > 0) {
        int width = track.width * status.progress_per_mille / 1000;
        if (width < 4) width = 4;
        if (width > track.width) width = track.width;
        fill_round_rect(pixels, stride, (UiRect){track.x, track.y, width, track.height}, 3, color);
    }
    if (model->demo_secret_enabled) {
        UiRect dbadge = {box.x + box.width - 14 - 38, box.y + 40, 38, 18};
        fill_round_rect(pixels, stride, dbadge, 5, UI_DANGER_SOFT);
        draw_text_center(pixels, stride, dbadge, "演示", 11, UI_DANGER);
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
    if (!label || !*label) return;

    bool disabled = (color == UI_DISABLED);

    /* 匹配复合肩键 "L/R  " 或 "L/R " */
    if (strncmp(label, "L/R  ", 5) == 0 || strncmp(label, "L/R ", 4) == 0) {
        const char *rest = strncmp(label, "L/R  ", 5) == 0 ? label + 5 : label + 4;
        int rest_w = measure_text(rest, size);
        int slash_w = measure_text("/", 14);
        int total_w = 24 + 4 + slash_w + 4 + 24 + 8 + rest_w;
        int start_x = box.x + (box.width - total_w) / 2;
        if (start_x < box.x + 2) start_x = box.x + 2;
        int gly_y = box.y + (box.height - 20) / 2;
        int baseline = box.y + (box.height + size - 4) / 2;

        draw_shoulder_key_glyph(pixels, stride, start_x, gly_y, 24, 20, "L", disabled);
        draw_text(pixels, stride, start_x + 28, baseline, "/", 14, color);
        draw_shoulder_key_glyph(pixels, stride, start_x + 28 + slash_w + 4, gly_y, 24, 20, "R", disabled);
        draw_text(pixels, stride, start_x + 28 + slash_w + 4 + 24 + 8, baseline, rest, size, color);
        return;
    }

    /* 匹配单肩键 "ZL  " 或 "ZR  " */
    if (strncmp(label, "ZL  ", 4) == 0 || strncmp(label, "ZR  ", 4) == 0) {
        char key_buf[4];
        memcpy(key_buf, label, 2);
        key_buf[2] = '\0';
        const char *rest = label + 4;
        int rest_w = measure_text(rest, size);
        int total_w = 32 + 8 + rest_w;
        int start_x = box.x + (box.width - total_w) / 2;
        if (start_x < box.x + 2) start_x = box.x + 2;
        int gly_y = box.y + (box.height - 20) / 2;
        int baseline = box.y + (box.height + size - 4) / 2;

        draw_shoulder_key_glyph(pixels, stride, start_x, gly_y, 32, 20, key_buf, disabled);
        draw_text(pixels, stride, start_x + 32 + 8, baseline, rest, size, color);
        return;
    }

    /* 匹配单字符圆键 "A  ", "B  ", "X  ", "Y  ", "+  ", "-  " */
    if ((label[0] == 'A' || label[0] == 'B' || label[0] == 'X' || label[0] == 'Y' ||
         label[0] == '+' || label[0] == '-') && (label[1] == ' ' && label[2] == ' ')) {
        char key_buf[2] = {label[0], '\0'};
        const char *rest = label + 3;
        int rest_w = measure_text(rest, size);
        int total_w = 22 + 8 + rest_w;
        int start_x = box.x + (box.width - total_w) / 2;
        if (start_x < box.x + 2) start_x = box.x + 2;
        int gly_y = box.y + (box.height - 22) / 2;
        int baseline = box.y + (box.height + size - 4) / 2;

        draw_single_key_glyph(pixels, stride, start_x, gly_y, 22, key_buf, disabled);
        draw_text(pixels, stride, start_x + 22 + 8, baseline, rest, size, color);
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
        snprintf(summary, sizeof(summary), "▲ 控制已停用  |  按 A 查看恢复");
    } else if (model->recovery_active) {
        snprintf(summary, sizeof(summary), "▲ 存在待恢复事务  |  按 A 进入排障");
    } else if (strcmp(model->setup_phase, "protection") == 0) {
        snprintf(summary, sizeof(summary), "▲ 系统防护已激活  |  按 A 查看详情");
    } else if (model->temporary_unlocked_available && model->temporary_unlocked) {
        snprintf(summary, sizeof(summary), "● 临时解除中  |  按 A 管理设置");
        color = UI_WARNING;
    } else {
        snprintf(summary, sizeof(summary), "▲ 系统异常需处理  |  按 A 进入支持");
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
    draw_button_label(pixels, stride, box, label, 20, disabled ? UI_DISABLED : foreground);
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
            snprintf(progress_label, sizeof(progress_label), "A  继续按住  %u%%",
                     (unsigned int)(model->confirm_hold_progress / 10));
        } else {
            snprintf(progress_label, sizeof(progress_label), "A  按住 1 秒确认");
        }
        draw_text_center(pixels, stride, button,
                         model->confirm_hold_progress >= 1000 ? "A  确认完成" : progress_label,
                         20, UI_ON_ACCENT);
        draw_rect_outline(pixels, stride, button, 12, 2,
                          model->confirm_hold_progress > 0 ? UI_SUCCESS : UI_ACCENT);
    } else {
        draw_dialog_button(pixels, stride, confirm, confirm_label, UI_ACCENT, UI_ON_ACCENT, false);
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  取消", UI_RAISED, UI_INK, true);
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
    snprintf(out, out_size, "时间未知");
    if (timestamp <= 0) {
        snprintf(out, out_size, "时间未知");
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
        snprintf(out, out_size, "今天 %02u:%02u", minute / 60, minute % 60);
    } else if ((uint16_t)(event_day + 1u) == today) {
        snprintf(out, out_size, "昨天 %02u:%02u", minute / 60, minute % 60);
    } else if (ptc_date_from_day_index(event_day, &year, &month, &day)) {
        snprintf(out, out_size, "%u-%02u-%02u %02u:%02u", year, month, day, minute / 60, minute % 60);
    } else {
        snprintf(out, out_size, "时间未知");
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
        draw_text_center(pixels, stride, detail_btn, "X 详情", 13, (danger || warning) ? UI_ON_ACCENT : UI_ACCENT);
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

    const char *msg = notice.summary[0] ? notice.summary : "操作状态与反馈";
    char fitted_msg[192];
    fit_text(fitted_msg, sizeof(fitted_msg), msg, 20, dialog.width - 108);
    draw_text(pixels, stride, dialog.x + 72, dialog.y + 76, fitted_msg, 20, danger ? UI_DANGER : UI_INK);

    UiRect card = {dialog.x + 36, dialog.y + 104, dialog.width - 72, 230};
    fill_round_rect(pixels, stride, card, 12, UI_PAGE);
    draw_rect_outline(pixels, stride, card, 12, 1, UI_BORDER);

    int card_base = card.y + 36;
    if (notice.details[0]) {
        draw_text(pixels, stride, card.x + 20, card_base, "详细信息与排查指引：", 16, UI_ACCENT);
        card_base = draw_wrapped_text(pixels, stride, card.x + 20, card_base + 32,
            notice.details, 16, card.width - 40, 24, 4, UI_INK);
    } else {
        draw_text(pixels, stride, card.x + 20, card_base, "操作已完成，当前没有更多详细日志。", 16, UI_MUTED);
        card_base += 32;
    }

    if (model->command_name[0]) {
        char execution[256];
        snprintf(execution, sizeof(execution), "执行命令：%s    传输模式：%s",
                 model->command_name[0] ? model->command_name : "无",
                 model->transport_label[0] ? model->transport_label : "默认");
        draw_text(pixels, stride, card.x + 20, card.y + card.height - 24, execution, 14, UI_MUTED);
    }

    PtcUiRect btn_rect = ptc_ui_cancel_rect(model->overlay);
    draw_dialog_button(pixels, stride, btn_rect, "A / B  关闭", UI_ACCENT, UI_ON_ACCENT, false);
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
