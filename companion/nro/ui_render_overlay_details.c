#include "ui_render_internal.h"
#include "../../common/rules/rules.h"

static void format_decision_rule(const PtcUiDecisionStep *step, char *out, size_t out_size)
{
    if (!step || step->state == PTC_UI_DECISION_UNKNOWN ||
        step->state == PTC_UI_DECISION_NOT_CONFIGURED || step->state == PTC_UI_DECISION_NOT_MATCHED ||
        step->state == PTC_UI_DECISION_DISABLED || step->state == PTC_UI_DECISION_CALENDAR_UNCOVERED) {
        snprintf(out, out_size, "%s", step ? ptc_ui_decision_state_label(step->state)
            : ptc_ui_text(PTC_UI_T_DECISION_UNKNOWN));
    } else if (step->rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
    } else {
        PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("minutes", step->rule.minutes)};
        (void)ptc_ui_text_format(PTC_UI_T_SHORT_MINUTES, out, out_size, args, 1);
    }
}


static const PtcUiTextId DETAIL_CONDITION_TITLES[] = {
    PTC_UI_T_TOTAL_DAILY_ALLOWANCE, PTC_UI_T_BEDTIME_SCHEDULE,
    PTC_UI_T_DETAIL_REST, PTC_UI_T_DETAIL_RULES_GATE_DOCK_TITLE
};

static void detail_text(uint32_t *pixels, uint32_t stride, UiRect r,
                        const char *text, int size, uint32_t color, int lines)
{
    draw_wrapped_text(pixels, stride, r.x, r.y, text, size, r.width, size + 4, lines, color);
}

static void detail_panel(uint32_t *pixels, uint32_t stride, UiRect r)
{
    fill_round_rect(pixels, stride, r, 12, UI_SURFACE);
    draw_rect_outline(pixels, stride, r, 12, 1, UI_BORDER);
}

static const char *detail_condition_label(PtcUiCondition condition)
{
    return ptc_ui_text(condition == PTC_UI_CONDITION_BLOCK ? PTC_UI_T_DETAIL_BLOCKED :
        condition == PTC_UI_CONDITION_UNKNOWN ? PTC_UI_T_STATUS_TO_CONFIRM :
        condition == PTC_UI_CONDITION_PASS ? PTC_UI_T_DETAIL_PASS :
        condition == PTC_UI_CONDITION_WAIVED ? PTC_UI_T_DETAIL_WAIVED : PTC_UI_T_DETAIL_OFF);
}

static void detail_reasons(unsigned reasons, char *out, size_t size)
{
    static const unsigned bits[] = {PTC_UI_STOP_DAILY, PTC_UI_STOP_BEDTIME,
        PTC_UI_STOP_EYE, PTC_UI_STOP_DOCK, PTC_UI_STOP_DAY_END};
    static const PtcUiTextId labels[] = {PTC_UI_T_DETAIL_STOP_DAILY, PTC_UI_T_DETAIL_STOP_BEDTIME,
        PTC_UI_T_DETAIL_REST, PTC_UI_T_DETAIL_STOP_DOCK, PTC_UI_T_DETAIL_DAY_END};
    out[0] = '\0';
    for (int i = 0; i < 5; ++i) if (reasons & bits[i]) {
        size_t used = strlen(out);
        if (used < size) snprintf(out + used, size - used, "%s%s", used ? " / " : "", ptc_ui_text(labels[i]));
    }
}

static void detail_balance(bool known, bool unlimited, int minutes, char *out, size_t size)
{
    if (!known) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
    else if (unlimited) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    else snprintf(out, size, ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)minutes);
}

static const char *detail_pending_reason(PtcUiEstimatePending reason)
{
    static const PtcUiTextId ids[] = {
        PTC_UI_T_DETAIL_NO_PREDICTION, PTC_UI_T_DETAIL_ESTIMATE_STALE,
        PTC_UI_T_DETAIL_ESTIMATE_DATE, PTC_UI_T_DETAIL_ESTIMATE_REFRESH,
        PTC_UI_T_DETAIL_ESTIMATE_RECOVERY, PTC_UI_T_DETAIL_ESTIMATE_READBACK,
        PTC_UI_T_DETAIL_ESTIMATE_RUNTIME, PTC_UI_T_DETAIL_ESTIMATE_TEMP_UNLOCK
    };
    if ((unsigned)reason >= (unsigned)(sizeof(ids) / sizeof(ids[0]))) {
        return ptc_ui_text(PTC_UI_T_DETAIL_ESTIMATE_RUNTIME);
    }
    return ptc_ui_text(ids[reason]);
}

static void detail_time(int seconds, char *out, size_t size)
{
    if (seconds % 60)
        snprintf(out, size, "%02u:%02u:%02u", (unsigned)(seconds / 3600),
            (unsigned)((seconds / 60) % 60), (unsigned)(seconds % 60));
    else snprintf(out, size, "%02u:%02u", (unsigned)(seconds / 3600), (unsigned)((seconds / 60) % 60));
}

static bool detail_runtime_available(const PtcUiModel *model)
{
    return !model->disable_flag_present && !model->recovery_active &&
        !model->apply_pending_confirmation && !model->temporary_unlocked &&
        strcmp(model->setup_phase, "active") == 0 && model->restricted_now != 1 && model->blocked_today != 1;
}

static bool detail_available(const PtcUiModel *model, const PtcUiTodayProjection *p)
{
    if (!p->fresh || p->active_reasons || !detail_runtime_available(model)) return false;
    for (int i = 0; i < 4; ++i) if (p->conditions[i] == PTC_UI_CONDITION_UNKNOWN) return false;
    return true;
}

static void detail_current(const PtcUiModel *model, const PtcUiTodayProjection *p, char *out, size_t size)
{
    if (p->active_reasons) detail_reasons(p->active_reasons, out, size);
    else if (detail_available(model, p)) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_DETAIL_AVAILABLE));
    else if (ptc_ui_runtime_fingerprint_reconfirmation_needed(model)) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_RECONFIRM_BADGE));
    else if (model->disable_flag_present) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_CONTROL_IS_DISABLED));
    else if (model->recovery_active) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_RECOVERING));
    else if (model->apply_pending_confirmation) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_WAITING_FOR_EFFECT));
    else if (strcmp(model->setup_phase, "active") == 0 && model->temporary_unlocked)
        snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_OVERVIEW_TEMP_UNLOCK_TITLE));
    else snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_DETAIL_PENDING));
}


/* ================== 今日调度详情重构辅助图元 ================== */

static void support_fill_circle_mini(uint32_t *pixels, uint32_t stride, float cx, float cy, float radius, uint32_t color)
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

static void support_draw_checkmark(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t bg_color, uint32_t fg_color)
{
    support_fill_circle_mini(pixels, stride, (float)cx, (float)cy, (float)radius, bg_color);
    draw_line(pixels, stride, cx - radius * 4 / 9, cy, cx - radius / 7, cy + radius * 3 / 8, 2, fg_color);
    draw_line(pixels, stride, cx - radius / 7, cy + radius * 3 / 8, cx + radius * 5 / 9, cy - radius * 3 / 8, 2, fg_color);
}

static void support_draw_calendar(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t color)
{
    int w = radius * 8 / 10;
    int h = radius * 8 / 10;
    draw_rect_outline(pixels, stride, (UiRect){cx - w, cy - h + 2, w * 2, h * 2 - 2}, 3, 2, color);
    draw_line(pixels, stride, cx - w, cy - h / 3, cx + w, cy - h / 3, 2, color);
    draw_line(pixels, stride, cx - w / 2, cy - h, cx - w / 2, cy - h + 4, 2, color);
    draw_line(pixels, stride, cx + w / 2, cy - h, cx + w / 2, cy - h + 4, 2, color);
}

static void support_draw_info(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t color)
{
    draw_circle_outline(pixels, stride, cx, cy, radius, 2, color);
    support_fill_circle_mini(pixels, stride, (float)cx, (float)(cy - radius * 4 / 10), 1.3f, color);
    draw_line(pixels, stride, cx, cy - radius / 10, cx, cy + radius * 5 / 10, 2, color);
}


static void support_draw_arrow_down(uint32_t *pixels, uint32_t stride, int tip_x, int tip_y, int width, int height, uint32_t color)
{
    uint32_t resolved = resolve_color(color);
    for (int dy = 0; dy <= height; ++dy) {
        int y = tip_y - dy;
        if (y < 0 || y >= SCREEN_HEIGHT) continue;
        int half_w = (width * dy + height / 2) / (height * 2);
        int x1 = tip_x - half_w;
        int x2 = tip_x + half_w;
        if (x1 < 0) x1 = 0;
        if (x2 >= SCREEN_WIDTH) x2 = SCREEN_WIDTH - 1;
        for (int x = x1; x <= x2; ++x) {
            pixels[y * stride + x] = resolved;
        }
    }
}

static void support_draw_chevron_right(uint32_t *pixels, uint32_t stride, int cx, int cy, int size, uint32_t color)
{
    draw_line(pixels, stride, cx - size / 2, cy - size, cx + size / 2, cy, 2, color);
    draw_line(pixels, stride, cx + size / 2, cy, cx - size / 2, cy + size, 2, color);
}

static void support_draw_play(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t bg_color, uint32_t fg_color)
{
    support_fill_circle_mini(pixels, stride, (float)cx, (float)cy, (float)radius, bg_color);
    int pw = radius * 5 / 10;
    int ph = radius * 5 / 10;
    draw_line(pixels, stride, cx - pw / 2, cy - ph, cx - pw / 2, cy + ph, 2, fg_color);
    draw_line(pixels, stride, cx - pw / 2, cy - ph, cx + pw, cy, 2, fg_color);
    draw_line(pixels, stride, cx - pw / 2, cy + ph, cx + pw, cy, 2, fg_color);
}

static void support_draw_heart(uint32_t *pixels, uint32_t stride, int cx, int cy, int size, uint32_t color)
{
    float r = (float)size * 0.38f;
    float d = r * 0.85f;
    support_fill_circle_mini(pixels, stride, (float)cx - d, (float)cy - d * 0.5f, r, color);
    support_fill_circle_mini(pixels, stride, (float)cx + d, (float)cy - d * 0.5f, r, color);
    int top_y = cy - (int)(d * 0.5f);
    int bot_y = cy + size;
    for (int y = top_y; y <= bot_y; ++y) {
        float t = (float)(y - top_y) / (float)(bot_y - top_y);
        float half_w = (1.0f - t) * (d + r);
        if (half_w > 0.0f) {
            draw_line(pixels, stride, (int)((float)cx - half_w), y, (int)((float)cx + half_w), y, 1, color);
        }
    }
}

static void detail_event(uint32_t *pixels, uint32_t stride, UiRect r, int second,
                         const char *label, uint32_t color, uint32_t bg)
{
    char time[16];
    detail_time(second, time, sizeof(time));
    fill_round_rect(pixels, stride, r, 7, bg);
    draw_text_bold(pixels, stride, r.x + 12, r.y + 22, time, 15, color);
    detail_text(pixels, stride, (UiRect){r.x + 12, r.y + 42, r.width - 24, 36},
                label, 13, color, 2);
}

static void draw_today_plan(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                            const PtcUiTodayProjection *p, const PtcUiTodayDecision *decision,
                            const PtcUiHomeDetailsStatus *status)
{
    char text[384], value[128], fitted[256];
    bool available = detail_available(model, p);
    if (model->view == PTC_UI_CHILD) {
        UiRect guide = to_uirect(ptc_ui_home_details_guide_rect());
        fill_round_rect(pixels, stride, guide, 6, UI_ACCENT_SOFT);
        support_draw_info(pixels, stride, guide.x + 16, guide.y + guide.height / 2, 7, UI_ACCENT);
        fit_text(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_CHILD_GUIDE), 13, guide.width - 46);
        draw_text(pixels, stride, guide.x + 32, guide.y + guide.height / 2 + 5, text, 13, UI_INK);
    }

    UiRect current = to_uirect(ptc_ui_today_details_panel_rect(model, 0));
    uint32_t current_color = available ? UI_SUCCESS : p->active_reasons ? UI_DANGER : UI_WARNING;
    fill_round_rect(pixels, stride, current, 10,
                    available ? UI_SUCCESS_SOFT : p->active_reasons ? UI_DANGER_SOFT : UI_WARNING_SOFT);
    draw_status_symbol(pixels, stride, current.x + 28, current.y + current.height / 2,
                       current_color, available ? 1 : 2);
    detail_current(model, p, text, sizeof(text));
    detail_text(pixels, stride, (UiRect){current.x + 52, current.y + 28, current.width - 72, 44},
                text, 18, current_color, 2);

    UiRect balance = to_uirect(ptc_ui_today_details_panel_rect(model, 3));
    fill_round_rect(pixels, stride, balance, 12, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, balance, 12, 1, UI_BORDER);
    draw_text_bold(pixels, stride, balance.x + 20, balance.y + 28,
                   ptc_ui_text(PTC_UI_T_OVERVIEW_BALANCE), 16, UI_INK);
    detail_balance(p->total_known, p->total_unlimited, p->total_remaining, value, sizeof(value));
    if (p->total_known && !p->total_unlimited)
        snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_LEFT_LABEL), value);
    else snprintf(text, sizeof(text), "%s", value);
    fit_text(text, sizeof(text), text, 26, balance.width - 40);
    draw_text_bold(pixels, stride, balance.x + 20, balance.y + 62, text, 26, UI_ACCENT);
    if (status->fresh) snprintf(text, sizeof(text), "%s / %s", ptc_ui_effective_rule_label(decision->effective.source), status->quota);
    else snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
    fit_text(fitted, sizeof(fitted), text, 13, balance.width - 40);
    draw_text(pixels, stride, balance.x + 20, balance.y + 86, fitted, 13, UI_MUTED);
    UiRect progress = {balance.x + 20, balance.y + 98, balance.width - 40, 8};
    fill_round_rect(pixels, stride, progress, 4, UI_RAISED);
    int fill = status->usage_progress_percent * progress.width / 100;
    if (fill > 0) fill_round_rect(pixels, stride, (UiRect){progress.x, progress.y, fill, progress.height}, 4, UI_ACCENT);
    fit_text(fitted, sizeof(fitted), status->usage, 12, balance.width - 40);
    draw_text(pixels, stride, balance.x + 20, balance.y + 126, fitted, 12, UI_MUTED);

    UiRect sleep = to_uirect(ptc_ui_today_details_panel_rect(model, 1));
    detail_panel(pixels, stride, sleep);
    support_draw_calendar(pixels, stride, sleep.x + 24, sleep.y + 25, 9, UI_ACCENT);
    draw_text_bold(pixels, stride, sleep.x + 42, sleep.y + 30, ptc_ui_text(PTC_UI_T_DETAIL_SLEEP), 17, UI_INK);
    const char *scale = ptc_ui_text(PTC_UI_T_DETAIL_SCHEDULE_SUB);
    fit_text(fitted, sizeof(fitted), scale, 13,
             sleep.width - 62 - measure_text(ptc_ui_text(PTC_UI_T_DETAIL_SLEEP), 17));
    draw_text(pixels, stride, sleep.x + sleep.width - 20 - measure_text(fitted, 13), sleep.y + 30, fitted, 13, UI_MUTED);
    UiRect track = {sleep.x + 24, sleep.y + 64, sleep.width - 48, 24};
    fill_round_rect(pixels, stride, track, 6, UI_RAISED);
    if (!p->fresh) {
        detail_text(pixels, stride, (UiRect){track.x, track.y + 17, track.width, 36},
                    ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM), 15, UI_WARNING, 1);
    } else {
        for (int i = 0; i < p->sleep_count; ++i) {
            const PtcUiSleepInterval *interval = &p->sleep[i];
            int sx = track.x + interval->start_second * track.width / 86400;
            int ex = track.x + interval->end_second * track.width / 86400;
            if (ex <= sx) continue;
            fill_round_rect(pixels, stride, (UiRect){sx, track.y, ex - sx, track.height},
                            ex - sx >= 8 ? 4 : (ex - sx) / 2, interval->skipped ? UI_SUCCESS_SOFT : UI_NIGHT);
            char from[16], to[16];
            detail_time(interval->start_second, from, sizeof(from));
            detail_time(interval->end_second, to, sizeof(to));
            snprintf(text, sizeof(text), "%s - %s", from, to);
            const char *label = measure_text(text, 11) <= ex - sx - 8 ? text : from;
            if (measure_text(label, 11) <= ex - sx - 8)
                draw_text_center(pixels, stride, (UiRect){sx + 4, track.y, ex - sx - 8, track.height},
                                 label, 11, interval->skipped ? UI_SUCCESS : UI_ON_ACCENT);
        }
        static const int ticks[] = {0, 6 * 3600, 12 * 3600, 18 * 3600, 24 * 3600};
        for (int i = 0; i < 5; ++i) {
            detail_time(ticks[i], text, sizeof(text));
            int x = track.x + ticks[i] * track.width / 86400;
            int width = measure_text(text, 12);
            int left = x - width / 2;
            if (left < track.x) left = track.x;
            if (left + width > track.x + track.width) left = track.x + track.width - width;
            draw_text(pixels, stride, left, track.y + 44, text, 12, UI_MUTED);
        }
        int now_x = track.x + p->now_second * track.width / 86400;
        detail_time(p->now_second, text, sizeof(text));
        int bubble_width = measure_text(text, 12) + 14;
        int bubble_x = now_x - bubble_width / 2;
        if (bubble_x < track.x) bubble_x = track.x;
        if (bubble_x + bubble_width > track.x + track.width) bubble_x = track.x + track.width - bubble_width;
        fill_round_rect(pixels, stride, (UiRect){bubble_x, track.y - 22, bubble_width, 20}, 4, UI_ACCENT);
        draw_text_center(pixels, stride, (UiRect){bubble_x, track.y - 22, bubble_width, 20}, text, 12, UI_ON_ACCENT);
        draw_line(pixels, stride, now_x, track.y - 2, now_x, track.y + track.height, 2, UI_ACCENT);
    }
    detail_text(pixels, stride, (UiRect){sleep.x + 24, sleep.y + sleep.height - 30, sleep.width - 48, 20},
                status->policies.bedtime.detail, 13, UI_MUTED, 1);

    UiRect prediction = to_uirect(ptc_ui_today_details_panel_rect(model, 2));
    detail_panel(pixels, stride, prediction);
    support_draw_play(pixels, stride, prediction.x + 24, prediction.y + 25, 9, UI_ACCENT, UI_ON_ACCENT);
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_DETAIL_PREDICTION), 17, prediction.width - 64);
    draw_text_bold(pixels, stride, prediction.x + 42, prediction.y + 30, fitted, 17, UI_INK);
    const char *mode = !status->fresh ? ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM) :
        ptc_ui_text(strcmp(model->operation_mode, "docked") == 0 ? PTC_UI_T_DOCK_TV_BADGE : PTC_UI_T_DOCK_HANDHELD_BADGE);
    fit_text(fitted, sizeof(fitted), mode, 13, prediction.width - 40);
    draw_text(pixels, stride, prediction.x + 20, prediction.y + 52, fitted, 13, UI_MUTED);
    if (!p->simulation_available || !p->segment_count) {
        if (!p->simulation_available) snprintf(text, sizeof(text), "%s", detail_pending_reason(p->pending_reason));
        else detail_reasons(p->stop_reasons, text, sizeof(text));
        detail_text(pixels, stride, (UiRect){prediction.x + 20, prediction.y + 92, prediction.width - 40, 64},
                    text, 16, p->active_reasons ? UI_DANGER : UI_WARNING, 3);
    } else {
        UiRect rail = {prediction.x + 20, prediction.y + 72, prediction.width - 40, 18};
        fill_round_rect(pixels, stride, rail, 4, UI_RAISED);
        int range = p->stop_second - p->now_second;
        if (range <= 0) range = 1;
        int first_rest = -1;
        for (int i = 0; i < p->segment_count; ++i) {
            const PtcUiUsageSegment *seg = &p->segments[i];
            int x = rail.x + (seg->start_second - p->now_second) * rail.width / range;
            int end = rail.x + (seg->end_second - p->now_second) * rail.width / range;
            if (end > x) fill_rect(pixels, stride, (UiRect){x, rail.y, end - x, rail.height}, seg->resting ? UI_WARNING : UI_ACCENT);
            if (seg->resting && first_rest < 0) first_rest = i;
        }
        detail_time(p->now_second, text, sizeof(text));
        draw_text(pixels, stride, rail.x, rail.y + 36, text, 12, UI_MUTED);
        detail_time(p->stop_second, text, sizeof(text));
        draw_text(pixels, stride, rail.x + rail.width - measure_text(text, 12), rail.y + 36, text, 12, UI_MUTED);
        /* Every segment stays on the rail. Fixed event cells avoid label collisions
           even when a short eye-care cycle has many near-adjacent boundaries. */
        if (first_rest >= 0) {
            const PtcUiUsageSegment *rest = &p->segments[first_rest];
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_SIM_REST_FMT), (unsigned)((rest->end_second - rest->start_second) / 60));
            detail_event(pixels, stride, to_uirect(ptc_ui_today_details_event_rect(model, 0)), rest->start_second, text, UI_WARNING_TEXT, UI_WARNING_SOFT);
            if (rest->end_second < p->stop_second)
                detail_event(pixels, stride, to_uirect(ptc_ui_today_details_event_rect(model, 1)), rest->end_second,
                             ptc_ui_text(PTC_UI_T_DETAIL_SIM_RESUME_LABEL), UI_ACCENT, UI_ACCENT_SOFT);
        } else {
            detail_event(pixels, stride, to_uirect(ptc_ui_today_details_event_rect(model, 0)), p->now_second,
                         ptc_ui_text(PTC_UI_T_DETAIL_AVAILABLE), UI_ACCENT, UI_ACCENT_SOFT);
        }
        detail_reasons(p->stop_reasons, text, sizeof(text));
        detail_event(pixels, stride, to_uirect(ptc_ui_today_details_event_rect(model, 2)), p->stop_second, text, UI_INK, UI_RAISED);
    }
    UiRect note = {prediction.x + 20, prediction.y + prediction.height - 48, prediction.width - 40, 36};
    if (p->simulation_available && (p->stop_reasons & PTC_UI_STOP_DOCK) && !p->total_unlimited && p->total_at_stop >= 0)
        snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_SIM_FOOTER_FMT), (unsigned)p->total_at_stop);
    else snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_DETAIL_ESTIMATE));
    detail_text(pixels, stride, (UiRect){note.x, note.y + 12, note.width, note.height}, text, 12, UI_MUTED, 2);

    UiRect guards = to_uirect(ptc_ui_today_details_panel_rect(model, 4));
    detail_panel(pixels, stride, guards);
    support_draw_heart(pixels, stride, guards.x + 22, guards.y + 22, 6, UI_ACCENT);
    draw_text_bold(pixels, stride, guards.x + 40, guards.y + 29, ptc_ui_text(PTC_UI_T_DETAIL_HEALTH), 17, UI_INK);
    const PtcUiTextId titles[] = {PTC_UI_T_DETAIL_HEALTH_EYE, PTC_UI_T_DETAIL_HEALTH_BEDTIME, PTC_UI_T_DETAIL_HEALTH_DOCK};
    const PtcUiTextId badges[] = {status->eye_badge, status->policies.bedtime.badge, status->policies.dock.badge};
    const PtcPolicyDisplayState states[] = {status->eye_state, status->policies.bedtime.state, status->policies.dock.state};
    const char *details[] = {status->eye, status->policies.bedtime.detail, status->policies.dock.compact};
    int row_height = (guards.height - 58) / 3;
    for (int i = 0; i < 3; ++i) {
        UiRect row = {guards.x + 14, guards.y + 42 + i * row_height, guards.width - 28, row_height - 8};
        uint32_t color = states[i] == PTC_POLICY_UNKNOWN ? UI_WARNING : policy_display_color(states[i]);
        fill_round_rect(pixels, stride, row, 7, UI_PAGE);
        const char *badge = ptc_ui_text(badges[i]);
        int badge_width = measure_text(badge, 12);
        int title_width = row.width - 24 - badge_width - 12;
        fit_text(fitted, sizeof(fitted), ptc_ui_text(titles[i]), 15, title_width);
        draw_text_bold(pixels, stride, row.x + 12, row.y + 21, fitted, 15, UI_INK);
        draw_text(pixels, stride, row.x + row.width - 12 - badge_width, row.y + 21, badge, 12, color);
        detail_text(pixels, stride, (UiRect){row.x + 12, row.y + 42, row.width - 24, 34}, details[i], 13, color, row.height >= 68 ? 2 : 1);
    }

    UiRect button = to_uirect(ptc_ui_home_details_action_rect(model, 0));
    bool focused = model->home_details_focus == 0;
    fill_round_rect(pixels, stride, button, 10, UI_ACCENT);
    if (focused) draw_focus_ring(pixels, stride, button, 10);
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_DETAIL_VIEW_RULES_BTN), 18, button.width - 88);
    draw_text_bold(pixels, stride, button.x + 48, button.y + 30, fitted, 18, UI_ON_ACCENT);
    draw_text(pixels, stride, button.x + 18, button.y + 30, "X", 17, UI_ON_ACCENT);
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_DETAIL_RULES_PRIORITY_NOTE), 12, button.width - 68);
    draw_text(pixels, stride, button.x + 48, button.y + 54, fitted, 12, UI_ON_ACCENT);
    support_draw_chevron_right(pixels, stride, button.x + button.width - 20, button.y + button.height / 2, 6, UI_ON_ACCENT);
}


static void draw_rules_conditions_gate(uint32_t *pixels, uint32_t stride,
                                      const PtcUiModel *model,
                                      const PtcUiTodayDecision *decision,
                                      int x, int y, int full_w, int full_h,
                                      int target_minutes, int played_minutes, int remaining_minutes,
                                      uint8_t target_weekday, bool is_today,
                                      bool total_known, bool total_unlimited,
                                      const PtcUiCondition *conditions)
{
    char text[512], value[64], fitted_sub[256];
    UiRect sec1 = {x, y, full_w, full_h};
    detail_panel(pixels, stride, sec1);

    /* 标题行: 第二步：实时准入与四大健康关卡 */
    bool any_block = (conditions[0] == PTC_UI_CONDITION_BLOCK || conditions[1] == PTC_UI_CONDITION_BLOCK ||
                      conditions[2] == PTC_UI_CONDITION_BLOCK || conditions[3] == PTC_UI_CONDITION_BLOCK);
    bool any_unknown = is_today && !detail_runtime_available(model);
    for (int i = 0; i < 4; ++i)
        if (conditions[i] == PTC_UI_CONDITION_UNKNOWN) any_unknown = true;
    uint32_t badge_color = any_block ? UI_DANGER : any_unknown ? UI_WARNING : UI_SUCCESS;
    support_fill_circle_mini(pixels, stride, (float)(sec1.x + 24), (float)(sec1.y + 18), 9.0f, badge_color);
    draw_text_center(pixels, stride, (UiRect){sec1.x + 15, sec1.y + 9, 18, 18}, "2", 12, UI_ON_ACCENT);
    const char *sec1_title = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC1_TITLE);
    int t1_w = measure_text(sec1_title, 14);
    draw_text_bold(pixels, stride, sec1.x + 40, sec1.y + 22, sec1_title, 14, UI_INK);

    const char *sec1_sub = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC1_SUB);
    int sub1_x = sec1.x + 40 + t1_w + 14;
    int max_sub1_w = (sec1.x + sec1.width - 20) - sub1_x;
    if (max_sub1_w > 60) {
        fit_text(fitted_sub, sizeof(fitted_sub), sec1_sub, 12, max_sub1_w);
        draw_text(pixels, stride, sub1_x, sec1.y + 22, fitted_sub, 12, UI_MUTED);
    }

    /* 独立守护互不干扰交互行 (可点击弹出规则说明) */
    UiRect tip_rect = to_uirect(ptc_ui_rules_independent_tip_rect(model));
    fill_round_rect(pixels, stride, tip_rect, 6, UI_PAGE);
    draw_rect_outline(pixels, stride, tip_rect, 6, 1, UI_BORDER);
    draw_text(pixels, stride, tip_rect.x + 10, tip_rect.y + 18, "🛡️", 14, UI_INK);

    /* 右侧查看说明药丸按钮 */
    const char *btn_txt = ptc_ui_text(PTC_UI_T_RULES_EXPLAIN_BTN);
    int btn_w = measure_text(btn_txt, 11) + 20;
    UiRect exp_btn = {tip_rect.x + tip_rect.width - btn_w - 6, tip_rect.y + 3, btn_w, 20};
    fill_round_rect(pixels, stride, exp_btn, 4, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, exp_btn, 4, 1, UI_ACCENT);
    draw_text_center(pixels, stride, exp_btn, btn_txt, 11, UI_ACCENT);

    /* 左侧提示文案 */
    int tip_text_x = tip_rect.x + 30;
    int max_tip_w = exp_btn.x - tip_text_x - 10;
    char fitted_tip[256];
    fit_text(fitted_tip, sizeof(fitted_tip), ptc_ui_text(PTC_UI_T_DETAIL_RULES_TIP_INDEPENDENT), 11, max_tip_w);
    draw_text(pixels, stride, tip_text_x, tip_rect.y + 18, fitted_tip, 11, UI_MUTED);

    /* 4 张健康关卡卡片 (高度压缩至 76px) */
    int card_y1 = tip_rect.y + tip_rect.height + 6;
    int gap1 = (full_w > 1100) ? 20 : 12;
    int card_w1 = (full_w - 32 - 3 * gap1) / 4;
    int card_h1 = 76;

    for (int i = 0; i < 4; ++i) {
        int cx = sec1.x + 16 + i * (card_w1 + gap1);
        UiRect c = {cx, card_y1, card_w1, card_h1};
        PtcUiCondition cond = conditions[i];
        bool is_pass = (cond == PTC_UI_CONDITION_PASS || cond == PTC_UI_CONDITION_WAIVED);
        bool is_block = (cond == PTC_UI_CONDITION_BLOCK);

        if (is_block) {
            fill_round_rect(pixels, stride, c, 8, UI_DANGER_SOFT);
            draw_rect_outline(pixels, stride, c, 8, 2, UI_DANGER);
        } else {
            fill_round_rect(pixels, stride, c, 8, UI_PAGE);
            draw_rect_outline(pixels, stride, c, 8, 1, UI_BORDER);
        }

        /* 4 个关卡的专属领域色 */
        uint32_t domain_color = (i == 0) ? UI_ACCENT : ((i == 1) ? UI_HERO : ((i == 2) ? UI_SUCCESS : UI_WARNING));

        /* 左侧领域竖色条 */
        UiRect accent_bar = {c.x + 4, c.y + 8, 4, c.height - 16};
        fill_round_rect(pixels, stride, accent_bar, 2, is_block ? UI_DANGER : domain_color);

        /* 顶部行：Emoji + 关卡名称 + 右上角紧凑状态胶囊 */
        const char *gate_emojis[] = {"⏰", "🌙", "🌿", "📺"};
        draw_text(pixels, stride, c.x + 12, c.y + 20, gate_emojis[i], 14, UI_INK);
        draw_text_bold(pixels, stride, c.x + 32, c.y + 19, ptc_ui_text(DETAIL_CONDITION_TITLES[i]), 13, UI_INK);

        /* 右上角紧凑状态胶囊 */
        const char *cond_label = detail_condition_label(cond);
        int pill_w = measure_text(cond_label, 11) + (is_pass ? 26 : 14);
        int pill_h = 18;
        UiRect pass_pill = {c.x + c.width - pill_w - 8, c.y + 6, pill_w, pill_h};
        uint32_t p_bg = is_block ? UI_DANGER : (is_pass ? UI_SUCCESS_SOFT : UI_RAISED);
        uint32_t p_bd = is_block ? UI_DANGER : (is_pass ? UI_SUCCESS : UI_BORDER);
        uint32_t p_fg = is_block ? UI_ON_ACCENT : (is_pass ? UI_SUCCESS : UI_MUTED);

        fill_round_rect(pixels, stride, pass_pill, 4, p_bg);
        if (!is_block) draw_rect_outline(pixels, stride, pass_pill, 4, 1, p_bd);
        if (is_pass) {
            support_draw_checkmark(pixels, stride, pass_pill.x + 9, pass_pill.y + pill_h / 2, 4, UI_SUCCESS, UI_ON_ACCENT);
        }
        draw_text_center(pixels, stride, pass_pill, cond_label, 11, p_fg);

        /* 中部核心指标 */
        int metric_y = c.y + 42;
        /* 底部副文 */
        int sub_y = c.y + 62;

        if (cond == PTC_UI_CONDITION_UNKNOWN || (i > 0 &&
            (cond == PTC_UI_CONDITION_OFF || cond == PTC_UI_CONDITION_WAIVED))) {
            fit_text(text, sizeof(text), detail_condition_label(cond), 13, c.width - 28);
            draw_text_bold(pixels, stride, c.x + 14, metric_y, text, 13,
                cond == PTC_UI_CONDITION_UNKNOWN ? UI_WARNING : UI_MUTED);
        } else if (i == 0) {
            if (is_today) {
                detail_balance(total_known, total_unlimited, remaining_minutes, value, sizeof(value));
                char left_msg[64];
                snprintf(left_msg, sizeof(left_msg), ptc_ui_text(PTC_UI_T_DETAIL_LEFT_LABEL), value);
                draw_text_bold(pixels, stride, c.x + 14, metric_y, left_msg, 14, is_block ? UI_DANGER : domain_color);
                if (total_known && !total_unlimited && model->played_minutes_available && played_minutes >= 0)
                    snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_USED_TOTAL_SUB),
                        (unsigned)played_minutes, (unsigned)target_minutes);
                else snprintf(text, sizeof(text), "%s", ptc_ui_effective_rule_label(decision->effective.source));
                draw_text(pixels, stride, c.x + 14, sub_y, text, 10, UI_MUTED);
            } else {
                detail_balance(total_known, total_unlimited, target_minutes, value, sizeof(value));
                draw_text_bold(pixels, stride, c.x + 14, metric_y, value, 14, is_block ? UI_DANGER : domain_color);
                draw_text(pixels, stride, c.x + 14, sub_y, ptc_ui_effective_rule_label(decision->effective.source), 10, UI_MUTED);
            }
        } else if (i == 1) {
            unsigned b_st = model->bedtime_policy.week[target_weekday].start_minute;
            unsigned b_en = model->bedtime_policy.week[target_weekday].end_minute;
            char bt_msg[64];
            snprintf(bt_msg, sizeof(bt_msg), ptc_ui_text(PTC_UI_T_DETAIL_RULE_BEDTIME_START), b_st / 60, b_st % 60);
            draw_text_bold(pixels, stride, c.x + 14, metric_y, bt_msg, 14, is_block ? UI_DANGER : domain_color);
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_BEDTIME_NEXT), b_en / 60, b_en % 60);
            draw_text(pixels, stride, c.x + 14, sub_y, text, 10, UI_MUTED);
        } else if (i == 2) {
            char eye_msg[64];
            snprintf(eye_msg, sizeof(eye_msg), ptc_ui_text(PTC_UI_T_DETAIL_RULE_EYE_PLAYED),
                     (unsigned)(is_today ? model->eye_care_used_minutes : 0), (unsigned)model->eye_care_policy.play_minutes);
            draw_text_bold(pixels, stride, c.x + 14, metric_y, eye_msg, 14, is_block ? UI_DANGER : domain_color);
            unsigned left_p = is_today && model->eye_care_policy.play_minutes > model->eye_care_used_minutes ?
                (unsigned)(model->eye_care_policy.play_minutes - model->eye_care_used_minutes) : (unsigned)model->eye_care_policy.play_minutes;
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_EYE_LEFT), left_p, (unsigned)model->eye_care_policy.rest_minutes);
            draw_text(pixels, stride, c.x + 14, sub_y, text, 10, UI_MUTED);
        } else {
            const char *dmode = is_today ?
                ptc_ui_text(strcmp(model->operation_mode, "docked") == 0 ? PTC_UI_T_DETAIL_TAG_DOCKED : PTC_UI_T_DETAIL_TAG_HANDHELD) :
                ptc_ui_text(model->dock_policy.force_docked ? PTC_UI_T_DETAIL_TAG_DOCKED : PTC_UI_T_DETAIL_TAG_HANDHELD);
            draw_text_bold(pixels, stride, c.x + 14, metric_y, dmode, 14, is_block ? UI_DANGER : domain_color);
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_DOCK_REMAIN),
                     (unsigned)(is_today ? model->undocked_remaining_minutes : model->dock_policy.undocked_daily_minutes));
            draw_text(pixels, stride, c.x + 14, sub_y, text, 10, UI_MUTED);
        }
    }

    int mid_x = sec1.x + sec1.width / 2;
    int gate_w = (full_w > 1000) ? 960 : (full_w - 40);
    int gate_h = (full_h >= 240) ? 36 : 30;
    int gate_y = sec1.y + full_h - gate_h - 10;
    UiRect gate = {mid_x - gate_w / 2, gate_y, gate_w, gate_h};

    int by1 = card_y1 + card_h1;
    int bus_y = by1 + (gate_y - by1) * 38 / 100;
    int lx1 = sec1.x + 16 + card_w1 / 2;
    int lx2 = sec1.x + 16 + 3 * (card_w1 + gap1) + card_w1 / 2;

    /* 绘制各卡片向下的动态引线与道闸 (垂直长度达 36px 以上) */
    for (int i = 0; i < 4; ++i) {
        int cx = sec1.x + 16 + i * (card_w1 + gap1);
        int bx = cx + card_w1 / 2;
        PtcUiCondition cond = conditions[i];
        bool is_pass = (cond == PTC_UI_CONDITION_PASS || cond == PTC_UI_CONDITION_WAIVED);
        bool is_block = (cond == PTC_UI_CONDITION_BLOCK);

        if (is_block) {
            /* 红色阻断引线 */
            draw_line(pixels, stride, bx, by1, bx, bus_y, 2, UI_DANGER);
            int sym_y = (by1 + bus_y) / 2;
            support_fill_circle_mini(pixels, stride, (float)bx, (float)sym_y, 5.0f, UI_DANGER);
            draw_line(pixels, stride, bx - 2, sym_y - 2, bx + 2, sym_y + 2, 2, UI_ON_ACCENT);
            draw_line(pixels, stride, bx - 2, sym_y + 2, bx + 2, sym_y - 2, 2, UI_ON_ACCENT);
        } else if (is_pass) {
            /* 绿色放行引线 */
            draw_line(pixels, stride, bx, by1, bx, bus_y, 2, UI_SUCCESS);
            int sym_y = (by1 + bus_y) / 2;
            support_fill_circle_mini(pixels, stride, (float)bx, (float)sym_y, 3.5f, UI_SUCCESS);
        } else {
            /* 未开启 / 离线淡灰色弱化线 */
            draw_line(pixels, stride, bx, by1, bx, bus_y, 1, UI_BORDER);
        }
    }

    /* 汇聚横线及连接节点 */
    uint32_t bus_color = any_block ? UI_DANGER : any_unknown ? UI_WARNING : UI_SUCCESS;
    draw_line(pixels, stride, lx1, bus_y, lx2, bus_y, 2, bus_color);
    for (int i = 0; i < 4; ++i) {
        int bx = sec1.x + 16 + i * (card_w1 + gap1) + card_w1 / 2;
        support_fill_circle_mini(pixels, stride, (float)bx, (float)bus_y, 3.0f, bus_color);
    }

    /* 粗壮导向干线与实心大三角向下箭头强力注入总闸门 (通道长度达 50px 以上) */
    int shaft_end_y = gate.y - 14;
    draw_line(pixels, stride, mid_x, bus_y, mid_x, shaft_end_y, 3, bus_color);
    support_draw_arrow_down(pixels, stride, mid_x, gate.y, 22, 14, bus_color);

    /* 居中横条 (总闸门) */
    fill_round_rect(pixels, stride, gate, 8, any_block ? UI_DANGER_SOFT : any_unknown ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
    draw_rect_outline(pixels, stride, gate, 8, 2, bus_color);
    if (!any_block && any_unknown) {
        draw_text_center(pixels, stride, gate, ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM), 13, UI_WARNING);
    } else if (!any_block) {
        const char *pass_txt = ptc_ui_text(PTC_UI_T_DETAIL_RULES_GATE_PASS);
        support_draw_checkmark(pixels, stride, gate.x + 24, gate.y + gate_h / 2, 7, UI_SUCCESS, UI_ON_ACCENT);
        draw_text_center(pixels, stride, gate, pass_txt, 13, UI_INK);
        const char *note = ptc_ui_text(PTC_UI_T_DETAIL_RULES_CONVERGE_SUB);
        int note_w = measure_text(note, 10);
        int pass_w = measure_text(pass_txt, 13);
        int note_x = gate.x + gate.width - note_w - 18;
        if (note_x > gate.x + gate.width / 2 + pass_w / 2 + 16) {
            draw_text(pixels, stride, note_x, gate.y + gate_h / 2 + 4, note, 10, UI_MUTED);
        }
    } else {
        const char *block_txt = ptc_ui_text(PTC_UI_T_DETAIL_RULES_GATE_BLOCKED_TITLE);
        int bx = gate.x + 24;
        int by = gate.y + gate_h / 2;
        support_fill_circle_mini(pixels, stride, (float)bx, (float)by, 7.0f, UI_DANGER);
        draw_line(pixels, stride, bx - 3, by - 3, bx + 3, by + 3, 2, UI_ON_ACCENT);
        draw_line(pixels, stride, bx - 3, by + 3, bx + 3, by - 3, 2, UI_ON_ACCENT);
        draw_text_center(pixels, stride, gate, block_txt, 13, UI_DANGER);
    }
}

static void draw_rules_priority_pipeline(uint32_t *pixels, uint32_t stride,
                                        const PtcUiModel *model,
                                        const PtcUiTodayDecision *decision,
                                        int x, int y, int full_w, int full_h,
                                        int target_minutes, int played_minutes, int remaining_minutes,
                                        bool is_today, bool total_known, bool total_unlimited)
{
    (void)model;
    char text[512], value[64], val[64], fitted_sub[256];
    UiRect sec2 = {x, y, full_w, full_h};
    detail_panel(pixels, stride, sec2);

    /* 标题行: 第一步：今日可用额度从何而来 */
    support_fill_circle_mini(pixels, stride, (float)(sec2.x + 24), (float)(sec2.y + 18), 9.0f, UI_ACCENT);
    draw_text_center(pixels, stride, (UiRect){sec2.x + 15, sec2.y + 9, 18, 18}, "1", 12, UI_ON_ACCENT);
    const char *sec2_title = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC2_TITLE);
    int t2_w = measure_text(sec2_title, 14);
    draw_text_bold(pixels, stride, sec2.x + 40, sec2.y + 22, sec2_title, 14, UI_INK);

    const char *sec2_sub = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC2_SUB);
    int sub2_x = sec2.x + 40 + t2_w + 14;
    int max_sub2_w = (sec2.x + sec2.width - 20) - sub2_x;
    if (max_sub2_w > 60) {
        fit_text(fitted_sub, sizeof(fitted_sub), sec2_sub, 12, max_sub2_w);
        draw_text(pixels, stride, sub2_x, sec2.y + 22, fitted_sub, 12, UI_MUTED);
    }

    /* 4 张优先级规则候选卡片 */
    const PtcUiDecisionStep *steps[] = {
        &decision->today_override, &decision->scheduled_override,
        &decision->holiday, &decision->weekly
    };
    const PtcUiTextId card_titles[] = {
        PTC_UI_T_TODAY_S_ADJUSTMENT,
        PTC_UI_T_SPECIFIED_DATE_QUOTA,
        PTC_UI_T_HOLIDAY,
        PTC_UI_T_WEEKLY_PLAN
    };
    const PtcUiTextId card_descs[] = {
        PTC_UI_T_DETAIL_RULES_TEMP_DESC,
        PTC_UI_T_DETAIL_RULES_SPEC_DESC,
        PTC_UI_T_DETAIL_RULES_HOLIDAY_DESC,
        PTC_UI_T_DETAIL_RULES_WEEKLY_PLAN_DESC
    };

    int selected_idx = -1;
    for (int i = 0; i < 4; ++i) {
        if (steps[i]->state == PTC_UI_DECISION_SELECTED) {
            selected_idx = i;
            break;
        }
    }

    int card_y2 = sec2.y + 28;
    int gap2 = (full_w > 1100) ? 20 : 12;
    int card_w2 = (full_w - 32 - 3 * gap2) / 4;
    int card_h2 = (full_h >= 180) ? 84 : 72;

    for (int i = 0; i < 4; ++i) {
        int cx = sec2.x + 16 + i * (card_w2 + gap2);
        UiRect c = {cx, card_y2, card_w2, card_h2};
        bool is_selected = (i == selected_idx);
        bool is_overridden = (selected_idx >= 0 && i > selected_idx);

        uint32_t domain = (i == 0) ? UI_CORAL : ((i == 1) ? UI_ACCENT : ((i == 2) ? UI_WARNING : UI_HERO));

        if (is_selected) {
            fill_round_rect(pixels, stride, c, 8, UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, c, 8, 2, UI_SUCCESS);
        } else if (is_overridden) {
            fill_round_rect(pixels, stride, c, 8, UI_RAISED);
            draw_rect_outline(pixels, stride, c, 8, 1, UI_BORDER);
        } else {
            fill_round_rect(pixels, stride, c, 8, UI_PAGE);
            draw_rect_outline(pixels, stride, c, 8, 1, UI_BORDER);
        }

        /* 顶部行：Emoji + P1~P4芯片 + 规则名称 + 右侧状态标签 */
        int top_y = c.y + 12;
        const char *prio_emojis[] = {"📅", "📅", "🎁", "🕒"};
        draw_text(pixels, stride, c.x + 12, top_y + 9, prio_emojis[i], 14, UI_INK);

        const char *prio_tags[] = {"P1", "P2", "P3", "P4"};
        char tag_buf[8];
        snprintf(tag_buf, sizeof(tag_buf), "%s", prio_tags[i]);
        int tag_w = measure_text(tag_buf, 11) + 8;
        UiRect tag_chip = {c.x + 32, top_y - 4, tag_w, 16};
        fill_round_rect(pixels, stride, tag_chip, 3, is_selected ? UI_SUCCESS : (is_overridden ? UI_RAISED : UI_ACCENT_SOFT));
        draw_text_center(pixels, stride, tag_chip, tag_buf, 11,
                         is_selected ? UI_ON_ACCENT : (is_overridden ? UI_DISABLED : domain));

        int name_x = tag_chip.x + tag_w + 6;
        int max_name_w = c.x + c.width - name_x - (is_selected ? 64 : 10);
        char fitted_title[64];
        fit_text(fitted_title, sizeof(fitted_title), ptc_ui_text(card_titles[i]), 12, max_name_w);
        draw_text_bold(pixels, stride, name_x, top_y + 8, fitted_title, 12, is_overridden ? UI_DISABLED : UI_INK);

        /* 右上角高亮标签：Hero 命中卡突出显示 */
        if (is_selected) {
            const char *act_lbl = ptc_ui_text(PTC_UI_T_DETAIL_RULES_ACTIVE_BADGE);
            int ab_w = measure_text(act_lbl, 10) + 12;
            UiRect act_pill = {c.x + c.width - ab_w - 8, top_y - 4, ab_w, 16};
            fill_round_rect(pixels, stride, act_pill, 4, UI_SUCCESS);
            draw_text_center(pixels, stride, act_pill, act_lbl, 10, UI_ON_ACCENT);
        }

        /* 中部核心指标/状态 */
        int mid_y = c.y + 40;
        if (is_selected) {
            format_decision_rule(steps[i], val, sizeof(val));
            char bold_val[64];
            snprintf(bold_val, sizeof(bold_val), "%s", val);
            draw_text_bold(pixels, stride, c.x + 14, mid_y + 8, bold_val, 16, UI_SUCCESS);
        } else if (is_overridden) {
            if (steps[i]->rule.minutes > 0 || steps[i]->rule.mode == PTC_RULE_MODE_UNLIMITED) {
                format_decision_rule(steps[i], val, sizeof(val));
                char ov_val[128];
                snprintf(ov_val, sizeof(ov_val), "%s (%s)", val, ptc_ui_text(PTC_UI_T_DETAIL_RULES_OVERRIDDEN_BADGE));
                draw_text(pixels, stride, c.x + 14, mid_y + 6, ov_val, 12, UI_DISABLED);
            } else {
                draw_text(pixels, stride, c.x + 14, mid_y + 6, ptc_ui_text(PTC_UI_T_DETAIL_RULES_OVERRIDDEN_BADGE), 12, UI_DISABLED);
            }
        } else {
            const char *st_lbl;
            if (i == 0 && !is_today) st_lbl = ptc_ui_text(PTC_UI_T_REASON_TODAY_ONLY);
            else st_lbl = ptc_ui_text(PTC_UI_T_DETAIL_RULES_NOT_SET_BADGE);
            draw_text(pixels, stride, c.x + 14, mid_y + 6, st_lbl, 12, UI_MUTED);
        }

        /* 底部解释说明行 */
        int desc_y = c.y + card_h2 - 10;
        char fitted_desc[128];
        fit_text(fitted_desc, sizeof(fitted_desc), ptc_ui_text(card_descs[i]), 10, c.width - 24);
        draw_text(pixels, stride, c.x + 14, desc_y, fitted_desc, 10,
                  is_selected ? domain : (is_overridden ? UI_DISABLED : UI_MUTED));

        /* 串行优先级流向箭头与阻断屏障 (节点间横向连接) */
        if (i < 3) {
            int x_start = c.x + c.width + 2;
            int x_end = c.x + c.width + gap2 - 2;
            int ay = c.y + card_h2 / 2;
            int ax_mid = (x_start + x_end) / 2;

            if (selected_idx >= 0 && i == selected_idx) {
                /* 关键设计：命中规则处切断后续流向，绘制阻断屏障 */
                draw_line(pixels, stride, x_start, ay, ax_mid - 2, ay, 2, UI_SUCCESS);
                draw_line(pixels, stride, ax_mid - 2, ay - 10, ax_mid - 2, ay + 10, 2, UI_DANGER);
                draw_line(pixels, stride, ax_mid + 2, ay - 10, ax_mid + 2, ay + 10, 2, UI_BORDER);
            } else if (selected_idx < 0 || i < selected_idx) {
                /* 前序规则未命中，顺畅流向下一级优先级 */
                draw_line(pixels, stride, x_start, ay, x_end, ay, 2, UI_ACCENT);
                draw_line(pixels, stride, x_end - 4, ay - 4, x_end, ay, 2, UI_ACCENT);
                draw_line(pixels, stride, x_end - 4, ay + 4, x_end, ay, 2, UI_ACCENT);
            } else {
                /* 已被高优先级覆盖的后续卡片，弱化浅线表示未激活 */
                draw_line(pixels, stride, x_start, ay, x_end, ay, 1, UI_BORDER);
            }
        }
    }

    /* 居中额度结果判定横条 */
    int mid_x = sec2.x + sec2.width / 2;
    int res_w = (full_w > 1000) ? 1040 : full_w - 40;
    int res_h = (full_h >= 180) ? 34 : 30;
    int res_y = sec2.y + full_h - res_h - 6;
    UiRect r_bar = {mid_x - res_w / 2, res_y, res_w, res_h};
    fill_round_rect(pixels, stride, r_bar, 8, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, r_bar, 8, 1, UI_ACCENT);

    detail_balance(total_known, total_unlimited, target_minutes, value, sizeof(value));
    char used_left[64];
    if (is_today && total_known && !total_unlimited && model->played_minutes_available && played_minutes >= 0) {
        snprintf(used_left, sizeof(used_left), ptc_ui_text(PTC_UI_T_DETAIL_RULES_USED_LEFT_FMT),
                 (unsigned)played_minutes, (unsigned)remaining_minutes);
        snprintf(text, sizeof(text), "%s: %s (%s / %s)",
                 ptc_ui_text(PTC_UI_T_DETAIL_RULES_CONFIRM_TOTAL), value,
                 ptc_ui_effective_rule_label(decision->effective.source), used_left);
    } else {
        snprintf(text, sizeof(text), "%s: %s (%s)",
                 ptc_ui_text(PTC_UI_T_DETAIL_RULES_CONFIRM_TOTAL), value,
                 ptc_ui_effective_rule_label(decision->effective.source));
    }
    /* 结果条左侧时钟 Emoji，文字整体居中 */
    draw_text(pixels, stride, r_bar.x + 16, r_bar.y + r_bar.height / 2 + 5, "⏰", 14, UI_INK);
    draw_text_center(pixels, stride, r_bar, text, 12, UI_ACCENT);

    /* 纵向导通线条：从生效卡片引出醒目纵线与实心箭头注入 r_bar */
    if (selected_idx >= 0 && selected_idx < 4) {
        int sel_x = sec2.x + 16 + selected_idx * (card_w2 + gap2) + card_w2 / 2;
        int by1 = card_y2 + card_h2;
        int by2 = r_bar.y;
        draw_line(pixels, stride, sel_x, by1, sel_x, by2 - 6, 2, UI_SUCCESS);
        support_fill_circle_mini(pixels, stride, (float)sel_x, (float)(by1 + 2), 2.5f, UI_SUCCESS);
        support_draw_arrow_down(pixels, stride, sel_x, by2, 10, 6, UI_SUCCESS);
    }
}

static void draw_today_rules(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                             const PtcUiTodayProjection *p, const PtcUiTodayDecision *decision)
{
    PtcUiRect body = ptc_ui_home_details_body_rect();
    ui_set_vertical_clip(body.y, body.y + body.h);

    int target_minutes = (int)model->forecast[0].minutes;
    int played_minutes = (int)model->played_minutes;
    uint8_t wd = ptc_weekday_from_day_index(model->day_index);
    bool available = detail_available(model, p);
    bool blocked = p->fresh && p->active_reasons != 0;

    /* 1. Top Conclusion-First Hero Banner (y = 72, h = 70) */
    {
        uint32_t fg = available ? UI_SUCCESS : (blocked ? UI_DANGER : UI_WARNING_TEXT);
        uint32_t icon_fg = available ? UI_SUCCESS : (blocked ? UI_DANGER : UI_WARNING);
        uint32_t bg = available ? UI_SUCCESS_SOFT : (blocked ? UI_DANGER_SOFT : UI_WARNING_SOFT);
        uint32_t bd = available ? UI_SUCCESS_BORDER : (blocked ? UI_DANGER_BORDER : UI_WARNING_BORDER);
        UiRect banner = {40, 72, 1200, 70};
        fill_round_rect(pixels, stride, banner, 12, bg);
        draw_rect_outline(pixels, stride, banner, 12, 2, bd);

        UiRect icon_tile = {banner.x + 16, banner.y + 16, 38, 38};
        fill_round_rect(pixels, stride, icon_tile, 10, UI_SURFACE);
        draw_rect_outline(pixels, stride, icon_tile, 10, 1, bd);
        if (available) {
            support_draw_checkmark(pixels, stride, icon_tile.x + 19, icon_tile.y + 19, 11, icon_fg, UI_SURFACE);
        } else {
            support_draw_info(pixels, stride, icon_tile.x + 19, icon_tile.y + 19, 11, icon_fg);
        }

        /* Right action button: X Guard Principles (shared hit rect) */
        UiRect explain_btn = to_uirect(ptc_ui_rules_independent_tip_rect(model));
        bool exp_focus = (model->home_details_focus == 0);
        fill_round_rect(pixels, stride, explain_btn, 10, exp_focus ? UI_ACCENT_SOFT : UI_SURFACE);
        draw_rect_outline(pixels, stride, explain_btn, 10, exp_focus ? 2 : 1, exp_focus ? UI_ACCENT : bd);
        char fitted_btn[64];
        fit_text(fitted_btn, sizeof(fitted_btn), ptc_ui_text(PTC_UI_T_RULES_EXPLAIN_KEY_BTN), 14, explain_btn.width - 16);
        draw_text_center(pixels, stride, explain_btn, fitted_btn, 14, exp_focus ? UI_ACCENT : UI_INK);

        int text_x = banner.x + 68;
        int text_max_w = explain_btn.x - text_x - 16;
        char headline[256], sub[256], fitted[256], val[64];
        if (available) {
            snprintf(headline, sizeof(headline), "%s", ptc_ui_text(PTC_UI_T_RULES_HERO_OK));
        } else if (blocked) {
            char reasons[160];
            detail_reasons(p->active_reasons, reasons, sizeof(reasons));
            snprintf(headline, sizeof(headline), ptc_ui_text(PTC_UI_T_RULES_HERO_BLOCKED_FMT), reasons);
        } else {
            detail_current(model, p, headline, sizeof(headline));
        }
        fit_text(fitted, sizeof(fitted), headline, 19, text_max_w);
        draw_text_bold(pixels, stride, text_x, banner.y + 32, fitted, 19, fg);

        if (!p->fresh) {
            snprintf(sub, sizeof(sub), "%s", ptc_ui_text(PTC_UI_T_RULES_HERO_STALE_SUB));
        } else if (p->total_known && !p->total_unlimited && model->played_minutes_available && played_minutes >= 0) {
            detail_balance(p->total_known, p->total_unlimited, target_minutes, val, sizeof(val));
            snprintf(sub, sizeof(sub), ptc_ui_text(PTC_UI_T_RULES_HERO_QUOTA_SUB_FMT),
                     val, ptc_ui_effective_rule_label(decision->effective.source),
                     (unsigned)played_minutes, (unsigned)(p->total_remaining > 0 ? p->total_remaining : 0));
        } else {
            detail_balance(p->total_known, p->total_unlimited, target_minutes, val, sizeof(val));
            snprintf(sub, sizeof(sub), "%s: %s (%s)",
                     ptc_ui_text(PTC_UI_T_DETAIL_RULES_CONFIRM_TOTAL), val,
                     ptc_ui_effective_rule_label(decision->effective.source));
        }
        fit_text(fitted, sizeof(fitted), sub, 14, text_max_w);
        draw_text(pixels, stride, text_x, banner.y + 56, fitted, 14, UI_INK);
    }

    /* 2. Left Column (56%): 4 Independent Real-Time Guards in spacious 2x2 grid */
    {
        UiRect left_card = {40, 154, 668, 462};
        detail_panel(pixels, stride, left_card);

        const char *g_sub = ptc_ui_text(PTC_UI_T_RULES_GUARDS_SUB);
        int g_sub_w = measure_text(g_sub, 13);
        draw_text(pixels, stride, left_card.x + left_card.width - 18 - g_sub_w, left_card.y + 28, g_sub, 13, UI_MUTED);
        char fitted_hdr[128];
        fit_text(fitted_hdr, sizeof(fitted_hdr), ptc_ui_text(PTC_UI_T_RULES_GUARDS_TITLE), 16,
                 left_card.width - 36 - g_sub_w - 14);
        draw_text_bold(pixels, stride, left_card.x + 18, left_card.y + 28, fitted_hdr, 16, UI_INK);

        const int grid_x = left_card.x + 16;
        const int grid_y = left_card.y + 44;
        const int gap = 12;
        const int cw = (left_card.width - 32 - gap) / 2;
        const int ch = (left_card.height - 60 - gap) / 2;

        for (int i = 0; i < 4; ++i) {
            int col = i % 2;
            int row = i / 2;
            UiRect c = {grid_x + col * (cw + gap), grid_y + row * (ch + gap), cw, ch};
            PtcUiCondition cond = p->conditions[i];
            bool is_block = (cond == PTC_UI_CONDITION_BLOCK);
            bool is_unknown = (cond == PTC_UI_CONDITION_UNKNOWN);
            bool is_off = (cond == PTC_UI_CONDITION_OFF);
            uint32_t domain = (i == 0) ? UI_ACCENT : ((i == 1) ? UI_HERO : ((i == 2) ? UI_SUCCESS : UI_WARNING));

            uint32_t c_bg = is_block ? UI_DANGER_SOFT : (is_unknown ? UI_WARNING_SOFT : UI_PAGE);
            uint32_t c_bd = is_block ? UI_DANGER : (is_unknown ? UI_WARNING_BORDER : UI_BORDER);
            uint32_t val_fg = is_block ? UI_DANGER : (is_unknown ? UI_WARNING_TEXT : (is_off ? UI_MUTED : domain));
            uint32_t bar_fg = is_block ? UI_DANGER : (is_unknown ? UI_WARNING : (is_off ? UI_BORDER : domain));
            fill_round_rect(pixels, stride, c, 12, c_bg);
            draw_rect_outline(pixels, stride, c, 12, (is_block || is_unknown) ? 2 : 1, c_bd);
            fill_round_rect(pixels, stride, (UiRect){c.x + 6, c.y + 14, 4, c.height - 28}, 2, bar_fg);

            /* Top row: Guard Title + Status Pill */
            const char *st_lbl = detail_condition_label(cond);
            int pw = measure_text(st_lbl, 12) + 18;
            if (pw > 116) pw = 116;
            UiRect pill = {c.x + c.width - 12 - pw, c.y + 12, pw, 24};
            uint32_t p_bg = is_block ? UI_DANGER : (is_unknown ? UI_WARNING : (is_off ? UI_RAISED : UI_SUCCESS_SOFT));
            uint32_t p_fg = (is_block || is_unknown) ? UI_ON_ACCENT : (is_off ? UI_MUTED : UI_SUCCESS);
            fill_round_rect(pixels, stride, pill, 12, p_bg);
            if (!is_block && !is_unknown && !is_off) {
                draw_rect_outline(pixels, stride, pill, 12, 1, UI_SUCCESS_BORDER);
            }
            char fitted_pill[48];
            fit_text(fitted_pill, sizeof(fitted_pill), st_lbl, 12, pill.width - 10);
            draw_text_center(pixels, stride, pill, fitted_pill, 12, p_fg);

            char fitted_t[64];
            fit_text(fitted_t, sizeof(fitted_t), ptc_ui_text(DETAIL_CONDITION_TITLES[i]), 16,
                     pill.x - (c.x + 20) - 8);
            draw_text_bold(pixels, stride, c.x + 20, c.y + 30, fitted_t, 16, UI_INK);

            /* Primary Metric + Multi-line Explanation */
            char big[96], desc[192], fitted_big[96];
            big[0] = '\0';
            desc[0] = '\0';
            if (i == 0) {
                detail_balance(p->total_known, p->total_unlimited, p->total_remaining, big, sizeof(big));
                if (is_unknown) {
                    snprintf(desc, sizeof(desc), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_STALE_DESC));
                } else if (p->total_known && !p->total_unlimited && model->played_minutes_available && played_minutes >= 0) {
                    snprintf(desc, sizeof(desc), ptc_ui_text(PTC_UI_T_DETAIL_RULE_USED_TOTAL_SUB),
                             (unsigned)played_minutes, (unsigned)target_minutes);
                } else {
                    snprintf(desc, sizeof(desc), "%s", ptc_ui_effective_rule_label(decision->effective.source));
                }
            } else if (i == 1) {
                if (is_off) {
                    snprintf(big, sizeof(big), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_OFF_VALUE));
                    snprintf(desc, sizeof(desc), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_BEDTIME_OFF_DESC));
                } else if (is_unknown) {
                    snprintf(big, sizeof(big), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
                    snprintf(desc, sizeof(desc), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_STALE_DESC));
                } else {
                    unsigned b_st = model->bedtime_policy.week[wd].start_minute;
                    unsigned b_en = model->bedtime_policy.week[wd].end_minute;
                    char s1[64], s2[64];
                    snprintf(big, sizeof(big), "%02u:%02u ~ %02u:%02u", b_st / 60, b_st % 60, b_en / 60, b_en % 60);
                    snprintf(s1, sizeof(s1), ptc_ui_text(PTC_UI_T_DETAIL_RULE_BEDTIME_START), b_st / 60, b_st % 60);
                    snprintf(s2, sizeof(s2), ptc_ui_text(PTC_UI_T_DETAIL_RULE_BEDTIME_NEXT), b_en / 60, b_en % 60);
                    snprintf(desc, sizeof(desc), "%s / %s", s1, s2);
                }
            } else if (i == 2) {
                if (is_off) {
                    snprintf(big, sizeof(big), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_OFF_VALUE));
                    snprintf(desc, sizeof(desc), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_EYE_OFF_DESC));
                } else if (is_unknown) {
                    snprintf(big, sizeof(big), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
                    snprintf(desc, sizeof(desc), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_STALE_DESC));
                } else if (is_block && model->eye_care_rest_remaining_seconds > 0) {
                    snprintf(big, sizeof(big), ptc_ui_text(PTC_UI_T_RULES_GUARD_EYE_RESTING_FMT),
                             (unsigned)(model->eye_care_rest_remaining_seconds / 60),
                             (unsigned)(model->eye_care_rest_remaining_seconds % 60));
                    snprintf(desc, sizeof(desc), ptc_ui_text(PTC_UI_T_DETAIL_RULE_EYE_PLAYED),
                             (unsigned)model->eye_care_used_minutes, (unsigned)model->eye_care_policy.play_minutes);
                } else {
                    unsigned left_p = model->eye_care_policy.play_minutes > model->eye_care_used_minutes
                        ? (unsigned)(model->eye_care_policy.play_minutes - model->eye_care_used_minutes)
                        : (unsigned)model->eye_care_policy.play_minutes;
                    snprintf(big, sizeof(big), ptc_ui_text(PTC_UI_T_DETAIL_RULE_EYE_PLAYED),
                             (unsigned)model->eye_care_used_minutes, (unsigned)model->eye_care_policy.play_minutes);
                    snprintf(desc, sizeof(desc), ptc_ui_text(PTC_UI_T_DETAIL_RULE_EYE_LEFT),
                             left_p, (unsigned)model->eye_care_policy.rest_minutes);
                }
            } else {
                const char *dmode = ptc_ui_text(strcmp(model->operation_mode, "docked") == 0
                    ? PTC_UI_T_DETAIL_TAG_DOCKED : PTC_UI_T_DETAIL_TAG_HANDHELD);
                if (is_off) {
                    snprintf(big, sizeof(big), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_OFF_VALUE));
                    snprintf(desc, sizeof(desc), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_DOCK_OFF_DESC));
                } else if (is_unknown) {
                    snprintf(big, sizeof(big), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
                    snprintf(desc, sizeof(desc), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_STALE_DESC));
                } else if (model->dock_policy.force_docked) {
                    snprintf(big, sizeof(big), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_DOCK_TV_ONLY));
                    snprintf(desc, sizeof(desc), "%s / %s", dmode, ptc_ui_text(PTC_UI_T_RULES_GUARD_DOCK_TV_OK_DESC));
                } else if (model->undocked_remaining_minutes <= 0 && model->dock_policy.undocked_limit_enabled) {
                    snprintf(big, sizeof(big), "%s", ptc_ui_text(PTC_UI_T_RULES_GUARD_DOCK_EMPTY));
                    snprintf(desc, sizeof(desc), "%s / %s", dmode, ptc_ui_text(PTC_UI_T_RULES_GUARD_DOCK_TV_OK_DESC));
                } else {
                    snprintf(big, sizeof(big), ptc_ui_text(PTC_UI_T_DETAIL_RULE_DOCK_REMAIN),
                             (unsigned)(model->undocked_remaining_minutes > 0 ? model->undocked_remaining_minutes : 0));
                    snprintf(desc, sizeof(desc), "%s / %s", dmode, ptc_ui_text(PTC_UI_T_RULES_GUARD_DOCK_TV_OK_DESC));
                }
            }

            fit_text(fitted_big, sizeof(fitted_big), big, 21, c.width - 32);
            draw_text_bold(pixels, stride, c.x + 20, c.y + 88, fitted_big, 21, val_fg);
            draw_wrapped_text(pixels, stride, c.x + 20, c.y + 124, desc, 14, c.width - 32, 20, 3, UI_MUTED);
        }
    }

    /* 3. Right Column (44%): Top-Down Quota Priority Ladder (P1 -> P4) */
    {
        UiRect right_card = {724, 154, 516, 462};
        detail_panel(pixels, stride, right_card);

        const PtcUiDecisionStep *steps[] = {
            &decision->today_override, &decision->scheduled_override,
            &decision->holiday, &decision->weekly
        };
        const PtcUiTextId tier_titles[] = {
            PTC_UI_T_TODAY_S_ADJUSTMENT,
            PTC_UI_T_SPECIFIED_DATE_QUOTA,
            PTC_UI_T_HOLIDAY,
            PTC_UI_T_WEEKLY_PLAN
        };
        const PtcUiTextId tier_descs[] = {
            PTC_UI_T_DETAIL_RULES_TEMP_DESC,
            PTC_UI_T_DETAIL_RULES_SPEC_DESC,
            PTC_UI_T_DETAIL_RULES_HOLIDAY_DESC,
            PTC_UI_T_DETAIL_RULES_WEEKLY_PLAN_DESC
        };

        int selected_idx = -1;
        for (int i = 0; i < 4; ++i) {
            if (steps[i]->state == PTC_UI_DECISION_SELECTED) {
                selected_idx = i;
                break;
            }
        }

        int badge_w = 0;
        if (selected_idx >= 0) {
            char hit_buf[48];
            snprintf(hit_buf, sizeof(hit_buf), ptc_ui_text(PTC_UI_T_RULES_HIT_TIER_FMT), selected_idx + 1);
            badge_w = measure_text(hit_buf, 12) + 20;
            if (badge_w > 110) badge_w = 110;
            UiRect hit_pill = {right_card.x + right_card.width - 16 - badge_w, right_card.y + 12, badge_w, 24};
            fill_round_rect(pixels, stride, hit_pill, 12, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, hit_pill, 12, 1, UI_ACCENT);
            draw_text_center(pixels, stride, hit_pill, hit_buf, 12, UI_ACCENT);
        }
        char fitted_hdr[128];
        fit_text(fitted_hdr, sizeof(fitted_hdr), ptc_ui_text(PTC_UI_T_RULES_QUOTA_TITLE), 16,
                 right_card.width - 36 - (badge_w ? badge_w + 12 : 0));
        draw_text_bold(pixels, stride, right_card.x + 18, right_card.y + 28, fitted_hdr, 16, UI_INK);

        int y = right_card.y + 46;
        const int row_x = right_card.x + 16;
        const int row_w = right_card.width - 32;

        for (int i = 0; i < 4; ++i) {
            bool is_hit = (i == selected_idx);
            bool is_overridden = (selected_idx >= 0 && i > selected_idx);
            int row_h = is_hit ? 142 : 66;
            UiRect r = {row_x, y, row_w, row_h};
            char ptag[8];
            snprintf(ptag, sizeof(ptag), "P%d", i + 1);

            if (is_hit) {
                fill_round_rect(pixels, stride, r, 11, UI_SUCCESS_SOFT);
                draw_rect_outline(pixels, stride, r, 11, 2, UI_SUCCESS);

                UiRect p_chip = {r.x + 14, r.y + 12, 34, 22};
                fill_round_rect(pixels, stride, p_chip, 6, UI_SUCCESS);
                draw_text_center(pixels, stride, p_chip, ptag, 12, UI_ON_ACCENT);

                char val_str[64];
                format_decision_rule(steps[i], val_str, sizeof(val_str));
                int val_w = measure_text(val_str, 18);
                draw_text_bold(pixels, stride, r.x + r.width - 14 - val_w, r.y + 29, val_str, 18, UI_SUCCESS);

                const char *act_txt = ptc_ui_text(PTC_UI_T_DETAIL_RULES_ACTIVE_BADGE);
                int act_w = measure_text(act_txt, 11) + 14;
                int name_x = p_chip.x + p_chip.width + 8;
                int max_name_w = (r.x + r.width - 14 - val_w - 10) - name_x - act_w - 8;
                if (max_name_w < 60) max_name_w = 60;
                char fitted_name[64];
                fit_text(fitted_name, sizeof(fitted_name), ptc_ui_text(tier_titles[i]), 16, max_name_w);
                draw_text_bold(pixels, stride, name_x, r.y + 29, fitted_name, 16, UI_INK);
                int nw = measure_text(fitted_name, 16);
                if (name_x + nw + 8 + act_w < r.x + r.width - 14 - val_w - 6) {
                    UiRect act_pill = {name_x + nw + 8, r.y + 13, act_w, 20};
                    fill_round_rect(pixels, stride, act_pill, 10, UI_SURFACE);
                    draw_rect_outline(pixels, stride, act_pill, 10, 1, UI_SUCCESS_BORDER);
                    draw_text_center(pixels, stride, act_pill, act_txt, 11, UI_SUCCESS);
                }

                /* Progress bar showing remaining vs total */
                UiRect bar_track = {r.x + 14, r.y + 48, r.width - 28, 10};
                fill_round_rect(pixels, stride, bar_track, 5, UI_SURFACE);
                draw_rect_outline(pixels, stride, bar_track, 5, 1, UI_SUCCESS_BORDER);
                int fill_pct = 100;
                if (p->total_known && !p->total_unlimited && target_minutes > 0) {
                    int rem_clamped = p->total_remaining > 0 ? p->total_remaining : 0;
                    fill_pct = rem_clamped * 100 / target_minutes;
                    if (fill_pct > 100) fill_pct = 100;
                    if (fill_pct < 0) fill_pct = 0;
                }
                int fill_w = bar_track.width * fill_pct / 100;
                if (fill_w >= 6) {
                    fill_round_rect(pixels, stride, (UiRect){bar_track.x, bar_track.y, fill_w, bar_track.height},
                                    5, UI_SUCCESS);
                }

                /* Used / Remaining summary row */
                char left_s[64], right_s[96], rem_val[48];
                detail_balance(p->total_known, p->total_unlimited, p->total_remaining, rem_val, sizeof(rem_val));
                snprintf(right_s, sizeof(right_s), ptc_ui_text(PTC_UI_T_RULES_LEFT_SHORT_FMT), rem_val);
                int rw = measure_text(right_s, 14);
                draw_text_bold(pixels, stride, r.x + r.width - 14 - rw, r.y + 84, right_s, 14, UI_INK);
                if (p->total_known && !p->total_unlimited && model->played_minutes_available && played_minutes >= 0) {
                    snprintf(left_s, sizeof(left_s), ptc_ui_text(PTC_UI_T_RULES_USED_SHORT_FMT), (unsigned)played_minutes);
                    draw_text(pixels, stride, r.x + 14, r.y + 84, left_s, 14, UI_INK);
                }

                /* Tier description */
                char fitted_d[128];
                fit_text(fitted_d, sizeof(fitted_d), ptc_ui_text(tier_descs[i]), 13, r.width - 28);
                draw_text(pixels, stride, r.x + 14, r.y + 116, fitted_d, 13, UI_MUTED);
            } else {
                fill_round_rect(pixels, stride, r, 10, is_overridden ? UI_RAISED : UI_PAGE);
                draw_rect_outline(pixels, stride, r, 10, 1, UI_BORDER);

                UiRect p_chip = {r.x + 12, r.y + 11, 32, 20};
                fill_round_rect(pixels, stride, p_chip, 5, UI_RAISED);
                draw_rect_outline(pixels, stride, p_chip, 5, 1, UI_BORDER);
                draw_text_center(pixels, stride, p_chip, ptag, 11, is_overridden ? UI_DISABLED : UI_MUTED);

                const char *st_txt = is_overridden
                    ? ptc_ui_text(PTC_UI_T_DETAIL_RULES_OVERRIDDEN_BADGE)
                    : ptc_ui_text(PTC_UI_T_DETAIL_RULES_NOT_SET_BADGE);
                int st_w = measure_text(st_txt, 13);
                draw_text(pixels, stride, r.x + r.width - 14 - st_w, r.y + 26, st_txt, 13,
                          is_overridden ? UI_DISABLED : UI_MUTED);

                int name_x = p_chip.x + p_chip.width + 8;
                int max_name_w = (r.x + r.width - 14 - st_w - 10) - name_x;
                char fitted_name[64];
                fit_text(fitted_name, sizeof(fitted_name), ptc_ui_text(tier_titles[i]), 15, max_name_w);
                draw_text_bold(pixels, stride, name_x, r.y + 26, fitted_name, 15,
                               is_overridden ? UI_DISABLED : UI_INK);

                char fitted_d[128];
                fit_text(fitted_d, sizeof(fitted_d), ptc_ui_text(tier_descs[i]), 13, r.width - 26);
                draw_text(pixels, stride, r.x + 12, r.y + 52, fitted_d, 13,
                          is_overridden ? UI_DISABLED : UI_MUTED);
            }

            y += row_h + 10;
        }

        /* Bottom integrated reminder bar inside right panel */
        UiRect note_box = {row_x, right_card.y + right_card.height - 46, row_w, 34};
        fill_round_rect(pixels, stride, note_box, 8, UI_PAGE);
        draw_rect_outline(pixels, stride, note_box, 8, 1, UI_BORDER);
        char fitted_note[160];
        fit_text(fitted_note, sizeof(fitted_note), ptc_ui_text(PTC_UI_T_DETAIL_RULES_PRIORITY_NOTE),
                 13, note_box.width - 24);
        draw_text(pixels, stride, note_box.x + 12, note_box.y + 22, fitted_note, 13, UI_MUTED);
    }

    ui_set_vertical_clip(0, SCREEN_HEIGHT);
}

static void draw_rules_help_dialog(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell = *model;
    snprintf(shell.overlay_title, sizeof(shell.overlay_title), "%s", ptc_ui_text(PTC_UI_T_RULES_EXPLAIN_TITLE));
    shell.overlay_body[0] = '\0';
    shell.overlay_open_frames = PTC_UI_OVERLAY_OPEN_FRAMES;
    draw_dialog_shell(pixels, stride, &shell, &dialog, 1000, 560);

    const PtcUiTextId titles[] = {
        PTC_UI_T_RULES_EXPLAIN_CARD1_TITLE,
        PTC_UI_T_RULES_EXPLAIN_CARD2_TITLE,
        PTC_UI_T_RULES_EXPLAIN_CARD3_TITLE,
        PTC_UI_T_RULES_EXPLAIN_CARD4_TITLE
    };
    const PtcUiTextId items[] = {
        PTC_UI_T_RULES_EXPLAIN_CARD1_BODY,
        PTC_UI_T_RULES_EXPLAIN_CARD2_BODY,
        PTC_UI_T_RULES_EXPLAIN_CARD3_BODY,
        PTC_UI_T_RULES_EXPLAIN_CARD4_BODY
    };
    /* All four symbols are verified built-in vector emojis in ui_emoji.h. */
    const char *emojis[] = {"🛡️", "📅", "🎯", "🌿"};
    uint32_t accent_colors[] = {UI_CORAL, UI_ACCENT, UI_SUCCESS, UI_HERO};

    int card_w = (dialog.width - 68 - 18) / 2;
    int card_h = 166;
    for (int i = 0; i < 4; ++i) {
        int col = i % 2;
        int row = i / 2;
        int cx = dialog.x + 34 + col * (card_w + 18);
        int cy = dialog.y + 100 + row * (card_h + 16);
        UiRect c = {cx, cy, card_w, card_h};
        char num[8];
        char fitted_title[128];
        snprintf(num, sizeof(num), "0%d", i + 1);

        fill_round_rect(pixels, stride, c, 12, UI_PAGE);

        /* Tinted header band clipped to the top 44px so top corners stay smoothly rounded. */
        ui_set_vertical_clip(c.y, c.y + 44);
        fill_round_rect(pixels, stride, c, 12, UI_RAISED);
        ui_set_vertical_clip(0, SCREEN_HEIGHT);
        draw_line(pixels, stride, c.x + 1, c.y + 44, c.x + c.width - 1, c.y + 44, 1, UI_BORDER);

        /* Left domain accent strip and outer border */
        fill_round_rect(pixels, stride, (UiRect){c.x + 4, c.y + 10, 4, c.height - 20}, 2, accent_colors[i]);
        draw_rect_outline(pixels, stride, c, 12, 1, UI_BORDER);

        /* Header contents: built-in emoji, fitted bold title, and right-aligned 01..04 badge */
        UiRect badge = {c.x + c.width - 14 - 34, c.y + 11, 34, 22};
        fill_round_rect(pixels, stride, badge, 11, UI_SURFACE);
        draw_rect_outline(pixels, stride, badge, 11, 1, UI_BORDER);
        draw_text_center(pixels, stride, badge, num, 11, UI_MUTED);

        draw_text(pixels, stride, c.x + 18, c.y + 29, emojis[i], 16, UI_INK);
        fit_text(fitted_title, sizeof(fitted_title), ptc_ui_text(titles[i]), 15, badge.x - (c.x + 44) - 10);
        draw_text_bold(pixels, stride, c.x + 44, c.y + 29, fitted_title, 15, UI_INK);

        /* Body text starts at baseline c.y + 70 (13px glyph top at c.y + 57, clear of c.y + 44 divider) */
        draw_wrapped_text(pixels, stride, c.x + 18, c.y + 70,
                          ptc_ui_text(items[i]), 13, c.width - 34, 20, 5, UI_INK);
    }

    /* 底部返回/关闭按钮 */
    home_button(pixels, stride, ptc_ui_rules_dialog_close_rect(), ptc_ui_text(PTC_UI_T_B_BACK), true, false, false);
}

static void draw_home_details(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiTodayProjection projection;
    PtcUiTodayDecision decision;
    PtcUiHomeDetailsStatus status;
    char age[64];
    PtcUiModel shell = *model;
    shell.overlay_title[0] = shell.overlay_body[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell, &dialog, 1240, 684);
    int64_t now = ptc_ui_render_now();
    uint16_t day = model->day_index;
    int second = ptc_ui_render_minute_of_day(now) * 60;
#ifdef PTC_UI_PREVIEW_ANIM_CLOCK_MS
    if (model->home_details_preview_second > 0) second = model->home_details_preview_second;
#endif
#ifndef PTC_UI_PREVIEW_WALL_TIME
    time_t clock = (time_t)now;
    struct tm *local = localtime(&clock);
    if (local) {
        second += local->tm_sec;
        if (!ptc_day_index_from_date(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, &day)) day = UINT16_MAX;
    } else day = UINT16_MAX;
#endif
    ptc_ui_project_today(model, now, day, second, &projection);
    ptc_ui_project_home_details_status(model, now, day, second, &status);
    ptc_ui_build_today_decision(model, PTC_UI_PLAN_SAVED, now, &decision);
    if (!projection.fresh) {
        decision.today_override.state = decision.scheduled_override.state =
            decision.holiday.state = decision.weekly.state = PTC_UI_DECISION_UNKNOWN;
        snprintf(decision.autonomy, sizeof(decision.autonomy), "%s", ptc_ui_text(PTC_UI_T_AUTONOMY_STATUS_PENDING));
    }

    /* 顶部标题与面包屑 */
    const char *details_title = ptc_ui_text(model->home_details_page == 0
        ? PTC_UI_T_TODAY_S_SCHEDULE_DETAILS : PTC_UI_T_DETAIL_RULES);
    const int details_sub_x = 44 + measure_text(details_title, 24) + 18;
    const int details_sub_width = 980 - details_sub_x;
    char fitted_sub[128];
    draw_text_bold(pixels, stride, 44, 52, details_title, 24, UI_INK);
    if (model->home_details_page == 0) {
        char sub[128];
        snprintf(sub, sizeof(sub), "%s", ptc_ui_text(PTC_UI_T_DETAIL_PLANNING));
        fit_text(fitted_sub, sizeof(fitted_sub), sub, 13, details_sub_width);
        draw_text(pixels, stride, details_sub_x, 52, fitted_sub, 13, UI_MUTED);
    } else {
        char sub[128];
        snprintf(sub, sizeof(sub), "%s / %s",
                 ptc_ui_text(PTC_UI_T_TODAY_S_SCHEDULE_DETAILS), ptc_ui_text(PTC_UI_T_DETAIL_RULES));
        fit_text(fitted_sub, sizeof(fitted_sub), sub, 12, details_sub_width);
        draw_text(pixels, stride, details_sub_x, 44, fitted_sub, 12, UI_MUTED);
        fit_text(fitted_sub, sizeof(fitted_sub), ptc_ui_text(PTC_UI_T_DETAIL_PREVIEW_SAMPLE), 11, details_sub_width);
        draw_text(pixels, stride, details_sub_x, 60, fitted_sub, 11, UI_MUTED);
    }

    format_status_age(model, age, sizeof(age));
    char age_label[128];
    snprintf(age_label, sizeof(age_label), ptc_ui_text(PTC_UI_T_DETAIL_UPDATED_AGE), age);
    bool needs_refresh = !model->waiting && (!projection.fresh || ptc_ui_status_needs_refresh(model, now));
    bool refresh_failed = ptc_ui_status_refresh_failed(model);
    if (needs_refresh) {
        const char *pending_txt = ptc_ui_text(PTC_UI_T_DETAIL_PENDING);
        int pw = measure_text(pending_txt, 13) + 34;
        if (pw > 260) pw = 260;
        UiRect fresh_pill = {dialog.x + dialog.width - 24 - pw, dialog.y + 28, pw, 28};
        char fitted_pending[96];
        fit_text(fitted_pending, sizeof(fitted_pending), pending_txt, 13, pw - 30);
        fill_round_rect(pixels, stride, fresh_pill, 14, refresh_failed ? UI_DANGER_SOFT : UI_WARNING_SOFT);
        draw_rect_outline(pixels, stride, fresh_pill, 14, 1, refresh_failed ? UI_DANGER_BORDER : UI_WARNING_BORDER);
        fill_round_rect(pixels, stride, (UiRect){fresh_pill.x + 10, fresh_pill.y + 11, 6, 6}, 3,
            refresh_failed ? UI_DANGER : UI_WARNING);
        draw_text_bold(pixels, stride, fresh_pill.x + 22, fresh_pill.y + 19, fitted_pending, 13,
            refresh_failed ? UI_DANGER : UI_WARNING_TEXT);
    } else {
        draw_text_center(pixels, stride, (UiRect){dialog.x + dialog.width - 280, dialog.y + 24, 260, 36},
            projection.fresh ? age_label : ptc_ui_text(PTC_UI_T_DETAIL_PENDING), 13,
            projection.fresh ? UI_MUTED : UI_WARNING);
    }

    if (model->home_details_page == 0) draw_today_plan(pixels, stride, model, &projection, &decision, &status);
    else draw_today_rules(pixels, stride, model, &projection, &decision);

    /* 底部按钮 */
    const char *back_label = ptc_ui_text(
        model->home_details_page == 1
            ? (model->home_details_entry_page == 1
                ? (model->parent_page == PTC_UI_PARENT_PLAN ? PTC_UI_T_B_BACK_TO_PLANS : PTC_UI_T_DETAIL_BACK_TODAY)
                : PTC_UI_T_DETAIL_BACK)
            : (model->view == PTC_UI_CHILD ? PTC_UI_T_DETAIL_BACK_CHILD : PTC_UI_T_DETAIL_BACK_TODAY));
    home_button(pixels, stride, ptc_ui_home_details_action_rect(model, 2), back_label,
        !needs_refresh, model->home_details_focus == 2, false);
    draw_refresh_action_button(pixels, stride, ptc_ui_home_details_action_rect(model, 1),
        ptc_ui_text(PTC_UI_T_Y_REFRESH), needs_refresh, refresh_failed,
        model->home_details_focus == 1, model->waiting);

    /* 底部右侧文字 */
    char footer[384];
    if (model->usage_direct_present || model->usage_summary_available) {
        char direct[192], legacy[192];
        ptc_ui_format_usage_history(model, true, direct, sizeof(direct));
        ptc_ui_format_usage_history(model, false, legacy, sizeof(legacy));
        snprintf(footer, sizeof(footer), "%.191s\n%.191s", direct, legacy);
    } else snprintf(footer, sizeof(footer), "%s%s%s", model->home_details_page == 0 ? ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_NOTE) : "",
        model->home_details_page == 0 ? "\n" : "", ptc_ui_text(PTC_UI_T_DETAIL_ESTIMATE));
    detail_text(pixels, stride, (UiRect){470, 630, 750, 42}, footer, 11, UI_MUTED, 2);

    if (model->rules_dialog_open) {
        draw_rules_help_dialog(pixels, stride, model);
    }
}

static void draw_forecast_day_details(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiTodayDecision decision;
    int offset = model->forecast_detail_day_offset;
    if (offset < 0 || offset >= (int)PTC_RESULT_FORECAST_DAYS) offset = 0;
    uint16_t target_day_index = model->day_index + offset;
    uint8_t weekday = ptc_weekday_from_day_index(target_day_index);
    bool is_today = (offset == 0);
    int64_t now = ptc_ui_render_now();
    bool fresh = ptc_ui_status_is_fresh(model, now);
    char age_buf[64], date_buf[32], sub[128], date_sub[128], cal_desc[256];

    PtcUiModel shell = *model;
    shell.overlay_title[0] = shell.overlay_body[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell, &dialog, 1120, 640);

    ptc_ui_build_day_decision(model, PTC_UI_PLAN_SAVED, target_day_index, now, &decision);

    /* 1. 顶部标题与面包屑 */
    draw_text_bold(pixels, stride, 114, 82, ptc_ui_text(PTC_UI_T_DETAIL_RULES), 24, UI_INK);

    const char *day_prefix = is_today ? ptc_ui_text(PTC_UI_T_TODAY) :
        (offset == 1 ? ptc_ui_text(PTC_UI_T_TOMORROW) : "");
    if (day_prefix[0]) {
        snprintf(sub, sizeof(sub), "%s / %s %s", ptc_ui_text(PTC_UI_T_TIME_PLANS),
                 day_prefix, ptc_ui_weekday_label(weekday));
    } else {
        snprintf(sub, sizeof(sub), "%s / %s (D+%d)", ptc_ui_text(PTC_UI_T_TIME_PLANS),
                 ptc_ui_weekday_label(weekday), offset);
    }
    draw_text(pixels, stride, 240, 72, sub, 12, UI_MUTED);

    bool covered = false;
    PtcCalendarDayType day_type = ptc_holiday_calendar_classify_in(model->calendar, target_day_index, &covered);
    const char *type_label = (covered && day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY) ? ptc_ui_text(PTC_UI_T_STATUTORY_HOLIDAYS) :
        ((covered && day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY) ? ptc_ui_text(PTC_UI_T_ADJUSTED_WORKING_DAYS) :
         ((weekday == 0 || weekday == 6) ? ptc_ui_text(PTC_UI_T_ORDINARY_WEEKEND) : ptc_ui_text(PTC_UI_T_ORDINARY_WORKING_DAYS)));

    ptc_format_date(target_day_index, date_buf);
    snprintf(date_sub, sizeof(date_sub), "%s  |  %s", date_buf, type_label);
    draw_text(pixels, stride, 240, 88, date_sub, 11,
              (covered && day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY) ? UI_WARNING :
              ((covered && day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY) ? UI_ACCENT : UI_MUTED));

    /* 顶部 7 天分页胶囊: < D+%d / 7天预测 > */
    UiRect prev_rect = to_uirect(ptc_ui_day_decision_prev_rect());
    UiRect next_rect = to_uirect(ptc_ui_day_decision_next_rect());
    UiRect pag_pill = {prev_rect.x, prev_rect.y, next_rect.x + next_rect.width - prev_rect.x, prev_rect.height};
    fill_round_rect(pixels, stride, pag_pill, 6, UI_RAISED);
    draw_rect_outline(pixels, stride, pag_pill, 6, 1, UI_BORDER);
    draw_text_center(pixels, stride, prev_rect, "<", 14, offset > 0 ? UI_ACCENT : UI_MUTED);
    char pag_txt[64];
    snprintf(pag_txt, sizeof(pag_txt), ptc_ui_text(PTC_UI_T_DAY_DECISION_PAGINATION), offset);
    draw_text_center(pixels, stride, (UiRect){prev_rect.x + prev_rect.width, prev_rect.y,
                     next_rect.x - (prev_rect.x + prev_rect.width), prev_rect.height}, pag_txt, 11, UI_INK);
    draw_text_center(pixels, stride, next_rect, ">", 14, offset < (int)PTC_RESULT_FORECAST_DAYS - 1 ? UI_ACCENT : UI_MUTED);

    /* 状态新鲜度 */
    format_status_age(model, age_buf, sizeof(age_buf));
    char age_label[128];
    snprintf(age_label, sizeof(age_label), ptc_ui_text(PTC_UI_T_DETAIL_UPDATED_AGE), age_buf);
    bool needs_refresh = !model->waiting && (!fresh || ptc_ui_status_needs_refresh(model, now));
    bool refresh_failed = ptc_ui_status_refresh_failed(model);
    if (needs_refresh) {
        const char *pending_txt = ptc_ui_text(PTC_UI_T_DETAIL_PENDING);
        int pw = measure_text(pending_txt, 13) + 34;
        if (pw > 256) pw = 256;
        UiRect fresh_pill = {1172 - pw, 62, pw, 26};
        char fitted_pending[96];
        fit_text(fitted_pending, sizeof(fitted_pending), pending_txt, 13, pw - 30);
        fill_round_rect(pixels, stride, fresh_pill, 13, refresh_failed ? UI_DANGER_SOFT : UI_WARNING_SOFT);
        draw_rect_outline(pixels, stride, fresh_pill, 13, 1, refresh_failed ? UI_DANGER_BORDER : UI_WARNING_BORDER);
        fill_round_rect(pixels, stride, (UiRect){fresh_pill.x + 10, fresh_pill.y + 10, 6, 6}, 3,
            refresh_failed ? UI_DANGER : UI_WARNING);
        draw_text_bold(pixels, stride, fresh_pill.x + 22, fresh_pill.y + 18, fitted_pending, 13,
            refresh_failed ? UI_DANGER : UI_WARNING_TEXT);
    } else {
        draw_text_center(pixels, stride, (UiRect){906, 56, 260, 36}, fresh ? age_label :
            ptc_ui_text(PTC_UI_T_DETAIL_PENDING), 13, fresh ? UI_MUTED : UI_WARNING);
    }

    /* 2. 顶部全局提示横幅 (y = 98, h = 24) */
    UiRect banner = {108, 98, 1064, 24};
    fill_round_rect(pixels, stride, banner, 6, fresh ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
    draw_rect_outline(pixels, stride, banner, 6, 1, fresh ? UI_SUCCESS : UI_WARNING);
    if (fresh) {
        support_draw_checkmark(pixels, stride, banner.x + 20, banner.y + 12, 5, UI_SUCCESS, UI_ON_ACCENT);
        draw_text_bold(pixels, stride, banner.x + 34, banner.y + 16,
                       ptc_ui_text(is_today ? PTC_UI_T_DETAIL_RULES_GLOBAL_BANNER : PTC_UI_T_DETAIL_RULES_FORECAST_BANNER),
                       12, UI_INK);
    } else {
        support_draw_info(pixels, stride, banner.x + 20, banner.y + 12, 5, UI_WARNING);
        draw_text_bold(pixels, stride, banner.x + 34, banner.y + 16,
                       ptc_ui_text(PTC_UI_T_RULES_PENDING_CONFIRMATION), 12, UI_WARNING);
    }

    /* 3. 准备额度参数与条件 */
    int target_minutes = decision.effective.rule.minutes;
    bool total_unlimited = (decision.effective.rule.mode == PTC_RULE_MODE_UNLIMITED);
    bool total_known = fresh;
    int played_minutes = is_today ? model->played_minutes : 0;
    int remaining_minutes = is_today ? model->remaining_minutes : target_minutes;

    PtcUiCondition conditions[4];
    if (is_today) {
        PtcUiTodayProjection p;
        int second = ptc_ui_render_minute_of_day(now) * 60;
        ptc_ui_project_today(model, now, model->day_index, second, &p);
        memcpy(conditions, p.conditions, sizeof(conditions));
    } else {
        conditions[0] = (!fresh) ? PTC_UI_CONDITION_UNKNOWN :
            (total_unlimited || target_minutes > 0 ? PTC_UI_CONDITION_PASS : PTC_UI_CONDITION_BLOCK);
        conditions[1] = !model->bedtime_policy.enabled ? PTC_UI_CONDITION_OFF : PTC_UI_CONDITION_PASS;
        conditions[2] = !model->eye_care_policy.enabled ? PTC_UI_CONDITION_OFF : PTC_UI_CONDITION_PASS;
        conditions[3] = (!model->dock_policy.undocked_limit_enabled && !model->dock_policy.force_docked)
            ? PTC_UI_CONDITION_OFF : PTC_UI_CONDITION_PASS;
    }

    /* 4. 区块 ① 先算时间：今天总共能玩多久 (y = 122, h = 150) */
    draw_rules_priority_pipeline(pixels, stride, model, &decision, 108, 122, 1064, 150,
                                 target_minutes, played_minutes, remaining_minutes,
                                 is_today, total_known, total_unlimited);

    /* 5. 中间因果流向提示：从步骤①输出注入步骤②门禁 (y = 272 ~ 330) */
    {
        int mid_x = 108 + 1064 / 2;
        int flow_y1 = 272;
        int flow_y2 = 330;
        const char *flow_txt = ptc_ui_text(PTC_UI_T_DETAIL_RULES_FLOW_INTO_GATE);
        int flow_w = measure_text(flow_txt, 11);
        int pill_w = flow_w + 24;
        int pill_h = 20;
        int pill_y = 280;
        UiRect flow_pill = {mid_x - pill_w / 2, pill_y, pill_w, pill_h};

        /* 上方导流线 */
        draw_line(pixels, stride, mid_x, flow_y1, mid_x, pill_y, 2, UI_ACCENT);

        /* 流程流向胶囊 */
        fill_round_rect(pixels, stride, flow_pill, 6, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, flow_pill, 6, 1, UI_ACCENT);
        draw_text_center(pixels, stride, flow_pill, flow_txt, 11, UI_ACCENT);

        /* 下方导流长轴与实心向下大三角箭头直达步骤②顶部 (30px 舒展流道，完全不被文字条框遮盖) */
        int shaft_start_y = pill_y + pill_h;
        int arrow_tip_y = flow_y2;
        int arrow_h = 14;
        draw_line(pixels, stride, mid_x, shaft_start_y, mid_x, arrow_tip_y - arrow_h, 3, UI_ACCENT);
        support_draw_arrow_down(pixels, stride, mid_x, arrow_tip_y, 20, arrow_h, UI_ACCENT);
    }

    /* 6. 区块 ② 综合把关：现在能不能开机玩 (y = 330, h = 244) */
    draw_rules_conditions_gate(pixels, stride, model, &decision, 108, 330, 1064, 244,
                               target_minutes, played_minutes, remaining_minutes,
                               weekday, is_today, total_known, total_unlimited, conditions);

    /* 7. 底部说明卡片: 日历属性解读 (y = 578, h = 32) */
    UiRect cal_card = {108, 578, 1064, 32};
    fill_round_rect(pixels, stride, cal_card, 6, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, cal_card, 6, 1, UI_BORDER);
    draw_text(pixels, stride, cal_card.x + 14, cal_card.y + 21, "📅", 14, UI_INK);
    draw_text(pixels, stride, cal_card.x + 36, cal_card.y + 20, ptc_ui_text(PTC_UI_T_CALENDAR_EXPLAIN_TITLE), 11, UI_INK);
    draw_line(pixels, stride, cal_card.x + 130, cal_card.y + 5, cal_card.x + 130, cal_card.y + 27, 1, UI_BORDER);
    snprintf(cal_desc, sizeof(cal_desc), ptc_ui_text(PTC_UI_T_CALENDAR_EXPLAIN_DESC), date_buf, type_label);
    draw_text(pixels, stride, cal_card.x + 144, cal_card.y + 20, cal_desc, 11, UI_INK);

    /* 8. 底部导航与按钮 (y = 618) */
    draw_text(pixels, stride, 114, 630, ptc_ui_text(PTC_UI_T_DAY_DECISION_SWITCH_HINT), 11, UI_MUTED);

    draw_refresh_action_button(pixels, stride, ptc_ui_day_decision_refresh_rect(),
                ptc_ui_text(PTC_UI_T_Y_REFRESH), needs_refresh, refresh_failed, false, model->waiting);
    home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_B_RETURN),
                false, !needs_refresh, false);

    if (model->rules_dialog_open) {
        draw_rules_help_dialog(pixels, stride, model);
    }
}

bool draw_details_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
    case PTC_UI_OVERLAY_HOME_DETAILS:
        draw_home_details(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_DAY_DECISION:
        draw_forecast_day_details(pixels, stride, model);
        return true;
    default:
        return false;
    }
}
