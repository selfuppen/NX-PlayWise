#include "ui_render_internal.h"
#include "../../common/rules/rules.h"
#include "../../common/time/ptc_time.h"

/* ---------- Pure CPU Vector Icons (no emoji dependencies) ---------- */

static void draw_child_calendar_icon(uint32_t *pixels, uint32_t stride, int cx, int cy, uint32_t color)
{
    draw_rect_outline(pixels, stride, (UiRect){cx - 8, cy - 7, 16, 14}, 3, 2, color);
    draw_line(pixels, stride, cx - 8, cy - 2, cx + 8, cy - 2, 2, color);
    draw_line(pixels, stride, cx - 4, cy - 9, cx - 4, cy - 6, 2, color);
    draw_line(pixels, stride, cx + 4, cy - 9, cx + 4, cy - 6, 2, color);
}

static void draw_child_clock_icon(uint32_t *pixels, uint32_t stride, int cx, int cy, uint32_t color)
{
    draw_circle_outline(pixels, stride, cx, cy, 8, 2, color);
    draw_line(pixels, stride, cx, cy - 4, cx, cy, 2, color);
    draw_line(pixels, stride, cx, cy, cx + 3, cy + 2, 2, color);
}

static void draw_child_eye_icon(uint32_t *pixels, uint32_t stride, int cx, int cy, uint32_t color)
{
    draw_rect_outline(pixels, stride, (UiRect){cx - 7, cy - 4, 14, 8}, 4, 1, color);
    draw_circle_outline(pixels, stride, cx, cy, 2, 1, color);
}

static void draw_child_moon_icon(uint32_t *pixels, uint32_t stride, int cx, int cy, int r, uint32_t color, uint32_t bg)
{
    fill_round_rect(pixels, stride, (UiRect){cx - r, cy - r, r * 2, r * 2}, r, color);
    fill_round_rect(pixels, stride, (UiRect){cx - r + r * 3 / 5, cy - r - 1, r * 2 - 1, r * 2 - 1}, r, bg);
}

static void draw_child_tv_icon(uint32_t *pixels, uint32_t stride, int cx, int cy, uint32_t color)
{
    draw_rect_outline(pixels, stride, (UiRect){cx - 7, cy - 5, 14, 9}, 2, 1, color);
    draw_line(pixels, stride, cx, cy + 4, cx, cy + 6, 1, color);
    draw_line(pixels, stride, cx - 3, cy + 6, cx + 3, cy + 6, 1, color);
}

static void draw_child_action_icon_mini(uint32_t *pixels, uint32_t stride, int cx, int cy,
                                        int kind, uint32_t color)
{
    if (kind == 0) {
        draw_rect_outline(pixels, stride, (UiRect){cx - 8, cy - 6, 16, 12}, 3, 2, color);
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 3; ++col)
                fill_round_rect(pixels, stride, (UiRect){cx - 5 + col * 4, cy - 3 + row * 5, 2, 2}, 1, color);
    } else if (kind == 1) {
        draw_circle_outline(pixels, stride, cx, cy, 7, 2, color);
        draw_line(pixels, stride, cx - 3, cy, cx + 3, cy, 2, color);
        draw_line(pixels, stride, cx, cy - 3, cx, cy + 3, 2, color);
    } else if (kind == 3) {
        draw_circle_outline(pixels, stride, cx, cy, 7, 2, color);
        draw_line(pixels, stride, cx - 3, cy, cx - 1, cy + 3, 2, color);
        draw_line(pixels, stride, cx - 1, cy + 3, cx + 4, cy - 3, 2, color);
    } else if (kind == 4) {
        draw_line(pixels, stride, cx - 7, cy - 5, cx, cy - 8, 2, color);
        draw_line(pixels, stride, cx, cy - 8, cx + 7, cy - 5, 2, color);
        draw_line(pixels, stride, cx - 7, cy - 5, cx - 5, cy + 3, 2, color);
        draw_line(pixels, stride, cx + 7, cy - 5, cx + 5, cy + 3, 2, color);
        draw_line(pixels, stride, cx - 5, cy + 3, cx, cy + 8, 2, color);
        draw_line(pixels, stride, cx + 5, cy + 3, cx, cy + 8, 2, color);
    } else {
        draw_child_calendar_icon(pixels, stride, cx, cy, color);
    }
}

static void draw_child_keycap(uint32_t *pixels, uint32_t stride, int x, int y, int size,
                              const char *key, bool inverted, bool disabled)
{
    uint32_t bg = disabled ? UI_RAISED : (inverted ? UI_RGB(ui_mix_rgb(UI_BLENDED(accent), 0xFFFFFF, 26)) : UI_KEY_GLYPH_BG);
    uint32_t bd = disabled ? UI_BORDER : (inverted ? UI_ON_ACCENT : UI_KEY_GLYPH_BORDER);
    uint32_t fg = disabled ? UI_DISABLED : (inverted ? UI_ON_ACCENT : UI_INK);
    int r = size / 2;
    fill_round_rect(pixels, stride, (UiRect){x, y, size, size}, r, bg);
    draw_rect_outline(pixels, stride, (UiRect){x, y, size, size}, r, 1, bd);
    if (strcmp(key, "+") == 0) {
        int cx = x + r, cy = y + r;
        draw_line(pixels, stride, cx - 4, cy, cx + 4, cy, 2, fg);
        draw_line(pixels, stride, cx, cy - 4, cx, cy + 4, 2, fg);
    } else if (strcmp(key, "-") == 0) {
        int cx = x + r, cy = y + r;
        draw_line(pixels, stride, cx - 4, cy, cx + 4, cy, 2, fg);
    } else {
        draw_text_center(pixels, stride, (UiRect){x, y, size, size}, key, 12, fg);
    }
}

/* ---------- Shared Clock & Projection Helpers ---------- */

static void child_local_clock(const PtcUiModel *model, int64_t *out_now, uint16_t *out_day, int *out_second)
{
    int64_t now = ptc_ui_render_now();
    uint16_t day = model ? model->day_index : 0;
    int second = ptc_ui_render_minute_of_day(now) * 60;
#ifdef PTC_UI_PREVIEW_ANIM_CLOCK_MS
    if (model && model->home_details_preview_second > 0) second = model->home_details_preview_second;
#endif
#ifndef PTC_UI_PREVIEW_WALL_TIME
    time_t clock_val = (time_t)now;
    struct tm *local = localtime(&clock_val);
    if (local) {
        second += local->tm_sec;
        if (!ptc_day_index_from_date(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, &day))
            day = UINT16_MAX;
    } else {
        day = UINT16_MAX;
    }
#endif
    *out_now = now;
    *out_day = day;
    *out_second = second;
}

void ui_render_overview(const PtcUiModel *model, PtcUiOverview *out)
{
    int64_t now = 0;
    uint16_t day = 0;
    int second = 0;
    child_local_clock(model, &now, &day, &second);
    ptc_ui_project_overview(model, now, day, second, out);
}

void draw_home_policy_row(uint32_t *pixels, uint32_t stride, UiRect box,
    const char *title, const char *detail, const char *badge, PtcPolicyDisplayState state)
{
    uint32_t fg = policy_display_color(state);
    uint32_t bg = state == PTC_POLICY_ACTIVE ? UI_DANGER_SOFT :
        state == PTC_POLICY_WAITING || state == PTC_POLICY_UNKNOWN || state == PTC_POLICY_EXHAUSTED ? UI_WARNING_SOFT : UI_RAISED;
    fill_round_rect(pixels, stride, box, 10, bg);
    draw_rect_outline(pixels, stride, box, 10, 1, UI_BORDER);
    char fitted_title[128], fitted_badge[128], fitted_detail[256];
    int width = box.width - 24;
    fit_text(fitted_badge, sizeof(fitted_badge), badge, 13, width / 2);
    int badge_w = measure_text(fitted_badge, 13);
    fit_text(fitted_title, sizeof(fitted_title), title, 16, width - badge_w - 10);
    draw_text(pixels, stride, box.x + 12, box.y + 22, fitted_title, 16, UI_INK);
    draw_text(pixels, stride, box.x + box.width - 12 - badge_w, box.y + 22, fitted_badge, 13, fg);
    fit_text(fitted_detail, sizeof(fitted_detail), detail, 13, width);
    draw_text(pixels, stride, box.x + 12, box.y + 43, fitted_detail, 13, state == PTC_POLICY_ACTIVE ? fg : UI_MUTED);
}

static void child_format_hhmm(int seconds, char *out, size_t size)
{
    if (seconds < 0) seconds = 0;
    if (seconds >= 86400) {
        snprintf(out, size, "24:00");
        return;
    }
    int mins = (seconds + 30) / 60;
    if (mins >= 1440) {
        snprintf(out, size, "24:00");
        return;
    }
    snprintf(out, size, "%02d:%02d", mins / 60, mins % 60);
}

static void child_format_stop_reasons(unsigned reasons, char *out, size_t size)
{
    static const unsigned bits[] = {
        PTC_UI_STOP_DAILY, PTC_UI_STOP_BEDTIME, PTC_UI_STOP_EYE, PTC_UI_STOP_DOCK, PTC_UI_STOP_DAY_END
    };
    static const PtcUiTextId labels[] = {
        PTC_UI_T_DETAIL_STOP_DAILY, PTC_UI_T_DETAIL_STOP_BEDTIME,
        PTC_UI_T_DETAIL_REST, PTC_UI_T_DETAIL_STOP_DOCK, PTC_UI_T_DETAIL_DAY_END
    };
    out[0] = '\0';
    for (int i = 0; i < 5; ++i) {
        if (reasons & bits[i]) {
            size_t used = strlen(out);
            if (used < size) {
                snprintf(out + used, size - used, "%s%s", used ? " / " : "", ptc_ui_text(labels[i]));
            }
        }
    }
    if (!out[0]) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_DETAIL_DAY_END));
}

static const char *child_pending_reason_text(PtcUiEstimatePending reason)
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

static bool child_forecast_unlimited_today(const PtcUiModel *model, int offset)
{
    return offset == 0 && (model->unrestricted_today == 1 ||
        model->eye_care_unlimited_capped || model->dock_unlimited_capped);
}

static PtcDayRule child_forecast_rule(const PtcUiModel *model, int offset, PtcUiChildForecastSource source)
{
    if (source == PTC_UI_CHILD_FORECAST_WEEKLY)
        return model->current_week[ptc_weekday_from_day_index(model->day_index + offset)];
    const PtcResultForecastDay *day = &model->forecast[offset];
    return (PtcDayRule){child_forecast_unlimited_today(model, offset) ?
        PTC_RULE_MODE_UNLIMITED : (PtcRuleMode)day->mode, day->minutes};
}

static const char *child_forecast_source_key(const PtcUiModel *model, int offset, PtcUiChildForecastSource source)
{
    if (source == PTC_UI_CHILD_FORECAST_WEEKLY) return "weekly";
    return child_forecast_unlimited_today(model, offset) ?
        model->rule_source : model->forecast[offset].rule_source;
}

static const char *child_forecast_special_tag(const PtcUiModel *model, int offset,
                                              PtcUiChildForecastSource source, bool is_weekend)
{
    const char *src = child_forecast_source_key(model, offset, source);
    if (src && strcmp(src, "today_override") == 0) return ptc_ui_text(PTC_UI_T_CHILD_DAY_TAG_TODAY);
    if (src && strcmp(src, "scheduled_override") == 0) return ptc_ui_text(PTC_UI_T_CHILD_DAY_TAG_SPECIAL);
    if (src && strcmp(src, "statutory_holiday") == 0) return ptc_ui_text(PTC_UI_T_CHILD_DAY_TAG_HOLIDAY);
    if (src && strcmp(src, "makeup_workday") == 0) return ptc_ui_text(PTC_UI_T_CHILD_DAY_TAG_MAKEUP);
    if (is_weekend) return ptc_ui_text(PTC_UI_T_CHILD_DAY_TAG_WKND);
    return "";
}

static bool child_day_bedtime_start(const PtcUiModel *model, uint16_t target_day, uint16_t *start_minute)
{
    if (!model || !model->bedtime_policy.enabled) return false;
    PtcRules rules;
    ptc_ui_build_plan_rules(model, PTC_UI_PLAN_SAVED, &rules);
    if (!rules.bedtime.enabled) return false;
    PtcEffectiveBedtime eff = ptc_bedtime_resolve_start_day(&rules, target_day,
        ptc_weekday_from_day_index(target_day));
    if (!eff.window.enabled) return false;
    *start_minute = eff.window.start_minute;
    return true;
}

/* ---------- Tier 1: Hero Left Semantic Panel + Right Projection Stage ---------- */

typedef enum {
    CHILD_STEP_PLAY = 0,
    CHILD_STEP_REST,
    CHILD_STEP_NIGHT,
    CHILD_STEP_STOP
} ChildStepTone;

typedef struct {
    char time[24];
    char title[96];
    char sub[96];
    ChildStepTone tone;
} ChildMilestoneStep;

static int child_build_milestones(const PtcUiTodayProjection *p, ChildMilestoneStep out[4])
{
    int count = 0;
    if (!p || !p->simulation_available || p->segment_count <= 0) return 0;

    int total_play_sec = 0;
    int rest_count = 0;
    int first_rest = -1;
    for (int i = 0; i < p->segment_count; ++i) {
        int dur = p->segments[i].end_second - p->segments[i].start_second;
        if (dur <= 0) continue;
        if (p->segments[i].resting) {
            ++rest_count;
            if (first_rest < 0) first_rest = i;
        } else {
            total_play_sec += dur;
        }
    }
    unsigned total_play_min = (unsigned)((total_play_sec + 30) / 60);
    if (total_play_sec > 0 && total_play_min == 0) total_play_min = 1;

    char t0[12], t1[12], stop_t[12], stop_reason[96];
    child_format_hhmm(p->stop_second, stop_t, sizeof(stop_t));
    child_format_stop_reasons(p->stop_reasons, stop_reason, sizeof(stop_reason));
    bool stop_night = (p->stop_reasons & PTC_UI_STOP_BEDTIME) != 0;
    bool stop_dock = (p->stop_reasons & PTC_UI_STOP_DOCK) != 0;

    if (p->segments[0].resting) {
        const PtcUiUsageSegment *r0 = &p->segments[0];
        child_format_hhmm(r0->start_second, t0, sizeof(t0));
        child_format_hhmm(r0->end_second, t1, sizeof(t1));
        snprintf(out[count].time, sizeof(out[count].time), "%s-%s", t0, t1);
        snprintf(out[count].title, sizeof(out[count].title), "%s", ptc_ui_text(PTC_UI_T_CHILD_STEP_REST_NOW));
        snprintf(out[count].sub, sizeof(out[count].sub), "%s", ptc_ui_text(PTC_UI_T_CHILD_STEP_REST_AFTER));
        out[count].tone = CHILD_STEP_REST;
        ++count;

        if (p->segment_count > 1) {
            const PtcUiUsageSegment *p1 = &p->segments[1];
            unsigned p1_min = (unsigned)((p1->end_second - p1->start_second + 30) / 60);
            if (p1_min == 0) p1_min = 1;
            child_format_hhmm(p1->start_second, t0, sizeof(t0));
            child_format_hhmm(p1->end_second, t1, sizeof(t1));
            snprintf(out[count].time, sizeof(out[count].time), "%s-%s", t0, t1);
            snprintf(out[count].title, sizeof(out[count].title),
                     ptc_ui_text(PTC_UI_T_CHILD_STEP_PLAY_NEXT_FMT), p1_min);
            if (p->segment_count > 2 && p->segments[2].resting) {
                snprintf(out[count].sub, sizeof(out[count].sub),
                         ptc_ui_text(PTC_UI_T_CHILD_STEP_TO_REST_FMT), t1);
            } else {
                snprintf(out[count].sub, sizeof(out[count].sub), "%s",
                         ptc_ui_text(PTC_UI_T_CHILD_STEP_USE_BALANCE));
            }
            out[count].tone = CHILD_STEP_PLAY;
            ++count;
        }
        if (p->segment_count > 3) {
            int more_sec = 0;
            unsigned more_rests = 0;
            for (int i = 2; i < p->segment_count; ++i) {
                if (p->segments[i].resting) ++more_rests;
                else more_sec += p->segments[i].end_second - p->segments[i].start_second;
            }
            if (more_sec > 0) {
                unsigned more_min = (unsigned)((more_sec + 30) / 60);
                if (more_min == 0) more_min = 1;
                child_format_hhmm(p->segments[2].start_second, t0, sizeof(t0));
                snprintf(out[count].time, sizeof(out[count].time), "%s-%s", t0, stop_t);
                snprintf(out[count].title, sizeof(out[count].title),
                         ptc_ui_text(PTC_UI_T_CHILD_STEP_PLAY_MORE_FMT), more_min);
                snprintf(out[count].sub, sizeof(out[count].sub),
                         ptc_ui_text(PTC_UI_T_CHILD_STEP_WITH_RESTS_FMT), more_rests);
                out[count].tone = CHILD_STEP_PLAY;
                ++count;
            }
        }
    } else if (first_rest >= 0) {
        const PtcUiUsageSegment *p0 = &p->segments[0];
        const PtcUiUsageSegment *r0 = &p->segments[first_rest];
        unsigned p0_min = (unsigned)((p0->end_second - p0->start_second + 30) / 60);
        unsigned r0_min = (unsigned)((r0->end_second - r0->start_second + 30) / 60);
        if (p0_min == 0) p0_min = 1;
        if (r0_min == 0) r0_min = 1;

        child_format_hhmm(p0->start_second, t0, sizeof(t0));
        child_format_hhmm(p0->end_second, t1, sizeof(t1));
        snprintf(out[count].time, sizeof(out[count].time), "%s-%s", t0, t1);
        snprintf(out[count].title, sizeof(out[count].title),
                 ptc_ui_text(PTC_UI_T_CHILD_STEP_PLAY_NOW_FMT), p0_min);
        snprintf(out[count].sub, sizeof(out[count].sub),
                 ptc_ui_text(PTC_UI_T_CHILD_STEP_TO_REST_FMT), t1);
        out[count].tone = CHILD_STEP_PLAY;
        ++count;

        child_format_hhmm(r0->start_second, t0, sizeof(t0));
        child_format_hhmm(r0->end_second, t1, sizeof(t1));
        snprintf(out[count].time, sizeof(out[count].time), "%s-%s", t0, t1);
        snprintf(out[count].title, sizeof(out[count].title),
                 ptc_ui_text(PTC_UI_T_CHILD_STEP_REST_MIN_FMT), r0_min);
        snprintf(out[count].sub, sizeof(out[count].sub),
                 ptc_ui_text(PTC_UI_T_CHILD_STEP_RESUME_FMT), t1);
        out[count].tone = CHILD_STEP_REST;
        ++count;

        if (first_rest + 1 < p->segment_count) {
            int rem_sec = 0;
            unsigned rem_rests = 0;
            for (int i = first_rest + 1; i < p->segment_count; ++i) {
                if (p->segments[i].resting) ++rem_rests;
                else rem_sec += p->segments[i].end_second - p->segments[i].start_second;
            }
            if (rem_sec > 0) {
                unsigned rem_min = (unsigned)((rem_sec + 30) / 60);
                if (rem_min == 0) rem_min = 1;
                child_format_hhmm(p->segments[first_rest + 1].start_second, t0, sizeof(t0));
                snprintf(out[count].time, sizeof(out[count].time), "%s-%s", t0, stop_t);
                snprintf(out[count].title, sizeof(out[count].title),
                         ptc_ui_text(PTC_UI_T_CHILD_STEP_PLAY_MORE_FMT), rem_min);
                if (rem_rests > 0) {
                    snprintf(out[count].sub, sizeof(out[count].sub),
                             ptc_ui_text(PTC_UI_T_CHILD_STEP_WITH_RESTS_FMT), rem_rests);
                } else {
                    snprintf(out[count].sub, sizeof(out[count].sub), "%s",
                             ptc_ui_text(PTC_UI_T_CHILD_STEP_USE_BALANCE));
                }
                out[count].tone = CHILD_STEP_PLAY;
                ++count;
            }
        }
    } else {
        child_format_hhmm(p->now_second, t0, sizeof(t0));
        snprintf(out[count].time, sizeof(out[count].time), "%s-%s", t0, stop_t);
        snprintf(out[count].title, sizeof(out[count].title),
                 ptc_ui_text(PTC_UI_T_CHILD_PROJ_CONTINUOUS_FMT), total_play_min);
        snprintf(out[count].sub, sizeof(out[count].sub), "%s", ptc_ui_text(PTC_UI_T_CHILD_STEP_NO_REST));
        out[count].tone = CHILD_STEP_PLAY;
        ++count;
    }

    /* Final stop milestone */
    if (count < 4) {
        snprintf(out[count].time, sizeof(out[count].time), "%s", stop_t);
        snprintf(out[count].title, sizeof(out[count].title), "%s", stop_reason);
        if (stop_dock && !p->total_unlimited && p->total_at_stop > 0) {
            snprintf(out[count].sub, sizeof(out[count].sub),
                     ptc_ui_text(PTC_UI_T_CHILD_STEP_TV_LEFT_FMT), (unsigned)p->total_at_stop);
        } else if (total_play_min > 0) {
            snprintf(out[count].sub, sizeof(out[count].sub),
                     ptc_ui_text(PTC_UI_T_CHILD_STEP_TOTAL_PLAY_FMT), total_play_min);
        } else {
            snprintf(out[count].sub, sizeof(out[count].sub), "%s",
                     ptc_ui_text(PTC_UI_T_CHILD_STEP_USE_BALANCE));
        }
        out[count].tone = stop_night ? CHILD_STEP_NIGHT : CHILD_STEP_STOP;
        ++count;
    }
    return count;
}

static void draw_child_rule_strip(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                  const PtcUiOverview *overview, const PtcUiTodayProjection *p,
                                  int x, int y, int max_right)
{
    typedef struct {
        int icon; /* 0=eye, 1=moon, 2=tv */
        uint32_t bg, bd, fg;
        char text[64];
    } RuleChip;
    RuleChip chips[3];
    int chip_count = 0;

    if (p->fresh) {
        if (model->eye_care_policy.enabled) {
            bool resting = strcmp(model->eye_care_phase, "resting") == 0;
            chips[chip_count].icon = 0;
            chips[chip_count].bg = resting ? UI_WARNING_SOFT : UI_RAISED;
            chips[chip_count].bd = resting ? UI_WARNING_BORDER : UI_BORDER;
            chips[chip_count].fg = resting ? UI_WARNING_TEXT : UI_MUTED;
            if (resting) {
                snprintf(chips[chip_count].text, sizeof(chips[chip_count].text), "%s",
                         ptc_ui_text(PTC_UI_T_CHILD_STEP_REST_NOW));
            } else {
                snprintf(chips[chip_count].text, sizeof(chips[chip_count].text),
                         ptc_ui_text(PTC_UI_T_CHILD_RULE_EYE_FMT),
                         (unsigned)model->eye_care_policy.play_minutes,
                         (unsigned)model->eye_care_policy.rest_minutes);
            }
            ++chip_count;
        }
        if (model->bedtime_policy.enabled) {
            uint16_t start_min = 0;
            bool has_bed = child_day_bedtime_start(model, model->day_index, &start_min);
            if (has_bed || model->bedtime_active || model->bedtime_skipped) {
                chips[chip_count].icon = 1;
                if (model->bedtime_skipped) {
                    chips[chip_count].bg = UI_SUCCESS_SOFT;
                    chips[chip_count].bd = UI_SUCCESS_BORDER;
                    chips[chip_count].fg = UI_SUCCESS;
                    snprintf(chips[chip_count].text, sizeof(chips[chip_count].text), "%s",
                             ptc_ui_text(PTC_UI_T_CHILD_RULE_BED_SKIPPED));
                } else {
                    chips[chip_count].bg = UI_NIGHT_SOFT;
                    chips[chip_count].bd = UI_NIGHT_BORDER;
                    chips[chip_count].fg = UI_NIGHT;
                    if (has_bed) {
                        snprintf(chips[chip_count].text, sizeof(chips[chip_count].text),
                                 ptc_ui_text(PTC_UI_T_CHILD_RULE_BED_FMT),
                                 (unsigned)(start_min / 60), (unsigned)(start_min % 60));
                    } else {
                        snprintf(chips[chip_count].text, sizeof(chips[chip_count].text), "%s",
                                 ptc_ui_text(PTC_UI_T_BEDTIME_SCHEDULE));
                    }
                }
                ++chip_count;
            }
        }
        if (model->dock_policy.force_docked || model->dock_policy.undocked_limit_enabled) {
            bool is_tv = strcmp(model->operation_mode, "docked") == 0;
            chips[chip_count].icon = 2;
            if (is_tv || model->dock_waived_today) {
                chips[chip_count].bg = UI_SUCCESS_SOFT;
                chips[chip_count].bd = UI_SUCCESS_BORDER;
                chips[chip_count].fg = UI_SUCCESS;
                snprintf(chips[chip_count].text, sizeof(chips[chip_count].text), "%s",
                         ptc_ui_text(PTC_UI_T_CHILD_RULE_DOCK_TV));
            } else if (model->dock_policy.force_docked || model->dock_restriction_active) {
                chips[chip_count].bg = UI_DANGER_SOFT;
                chips[chip_count].bd = UI_DANGER_BORDER;
                chips[chip_count].fg = UI_DANGER;
                snprintf(chips[chip_count].text, sizeof(chips[chip_count].text), "%s",
                         ptc_ui_text(PTC_UI_T_CHILD_RULE_DOCK_ONLY));
            } else if (model->undocked_usage_available) {
                unsigned rem = model->undocked_remaining_minutes > 0 ? (unsigned)model->undocked_remaining_minutes : 0;
                chips[chip_count].bg = UI_WARNING_SOFT;
                chips[chip_count].bd = UI_WARNING_BORDER;
                chips[chip_count].fg = UI_WARNING_TEXT;
                snprintf(chips[chip_count].text, sizeof(chips[chip_count].text),
                         ptc_ui_text(PTC_UI_T_CHILD_RULE_DOCK_REM_FMT), rem);
            } else {
                chips[chip_count].bg = UI_RAISED;
                chips[chip_count].bd = UI_BORDER;
                chips[chip_count].fg = UI_MUTED;
                snprintf(chips[chip_count].text, sizeof(chips[chip_count].text), "%s",
                         ptc_ui_text(PTC_UI_T_DOCK_TITLE));
            }
            ++chip_count;
        }
    }

    if (chip_count == 0) {
        char fitted[160];
        const char *msg = !p->fresh ? ptc_ui_text(PTC_UI_T_OVERVIEW_REFRESH) :
            (overview->available ? ptc_ui_text(PTC_UI_T_CHILD_RULE_NONE) : ptc_ui_text(PTC_UI_T_CHILD_ACT_NEED_PARENT));
        fit_text(fitted, sizeof(fitted), msg, 12, max_right - x);
        draw_text(pixels, stride, x, y + 17, fitted, 12, UI_MUTED);
        return;
    }

    int cursor_x = x;
    for (int i = 0; i < chip_count; ++i) {
        int tw = measure_text(chips[i].text, 12);
        int pw = tw + 34;
        if (cursor_x + pw > max_right) {
            int avail = max_right - cursor_x - 34;
            if (avail < 36) break;
            char shrunk[64];
            fit_text(shrunk, sizeof(shrunk), chips[i].text, 12, avail);
            snprintf(chips[i].text, sizeof(chips[i].text), "%s", shrunk);
            tw = measure_text(chips[i].text, 12);
            pw = tw + 34;
        }
        UiRect pill = {cursor_x, y, pw, 24};
        fill_round_rect(pixels, stride, pill, 12, chips[i].bg);
        draw_rect_outline(pixels, stride, pill, 12, 1, chips[i].bd);
        int icx = pill.x + 13;
        int icy = pill.y + 12;
        if (chips[i].icon == 0) draw_child_eye_icon(pixels, stride, icx, icy, chips[i].fg);
        else if (chips[i].icon == 1) draw_child_moon_icon(pixels, stride, icx, icy, 5, chips[i].fg, chips[i].bg);
        else draw_child_tv_icon(pixels, stride, icx, icy, chips[i].fg);
        draw_text_bold(pixels, stride, pill.x + 24, pill.y + 16, chips[i].text, 12, chips[i].fg);
        cursor_x += pw + 8;
    }
}

static void draw_child_projection_stage(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                        const PtcUiOverview *overview, const PtcUiTodayProjection *p, UiRect box)
{
    const bool dark = (g_theme.resolved == PTC_UI_RESOLVED_DARK);
    const int pad_x = 20;
    const int rx = box.x + pad_x;
    const int rw = box.width - pad_x * 2;
    const bool has_segments = p->fresh && p->simulation_available && p->segment_count > 0;

    /* 1. Stage Header: Clock Icon + Title + Projected Stop Time Pill */
    bool is_tv = p->fresh && model->dock_available && strcmp(model->operation_mode, "docked") == 0;
    bool is_handheld = p->fresh && model->dock_available && strcmp(model->operation_mode, "undocked") == 0;
    const char *proj_title = is_tv ? ptc_ui_text(PTC_UI_T_CHILD_PROJ_TITLE_TV) :
        (is_handheld ? ptc_ui_text(PTC_UI_T_CHILD_PROJ_TITLE_HANDHELD) : ptc_ui_text(PTC_UI_T_DETAIL_PREDICTION));

    draw_child_clock_icon(pixels, stride, rx + 9, box.y + 27, UI_ACCENT);
    int title_max_w = rw - 28;
    if (has_segments) {
        char stop_t[12], stop_reason[96], until_str[128], fitted_until[128];
        child_format_hhmm(p->stop_second, stop_t, sizeof(stop_t));
        child_format_stop_reasons(p->stop_reasons, stop_reason, sizeof(stop_reason));
        snprintf(until_str, sizeof(until_str), ptc_ui_text(PTC_UI_T_CHILD_PROJ_UNTIL_FMT), stop_t, stop_reason);
        fit_text(fitted_until, sizeof(fitted_until), until_str, 13, rw / 2);
        int uw = measure_text(fitted_until, 13);
        int pill_w = uw + 22;
        UiRect upill = {box.x + box.width - pad_x - pill_w, box.y + 14, pill_w, 26};
        bool stop_night = (p->stop_reasons & PTC_UI_STOP_BEDTIME) != 0;
        bool stop_day_end = (p->stop_reasons == PTC_UI_STOP_DAY_END);
        uint32_t pbg = stop_night ? UI_NIGHT_SOFT : (stop_day_end ? UI_ACCENT_SOFT : UI_DANGER_SOFT);
        uint32_t pbd = stop_night ? UI_NIGHT_BORDER : (stop_day_end ? UI_ACCENT : UI_DANGER_BORDER);
        uint32_t pfg = stop_night ? UI_NIGHT : (stop_day_end ? UI_ACCENT : UI_DANGER);
        fill_round_rect(pixels, stride, upill, 13, pbg);
        draw_rect_outline(pixels, stride, upill, 13, 1, pbd);
        draw_text_center(pixels, stride, upill, fitted_until, 13, pfg);
        title_max_w = upill.x - (rx + 26) - 12;
    }
    char fitted_title[128];
    fit_text(fitted_title, sizeof(fitted_title), proj_title, 17, title_max_w);
    draw_text_bold(pixels, stride, rx + 26, box.y + 33, fitted_title, 17, UI_INK);

    /* 2. Body: Either Full Segmented Track + Step Cards, or Honest Unavailable Guidance */
    int footer_y = box.y + box.height - 46;
    if (has_segments) {
        int total_play_sec = 0;
        unsigned rest_count = 0;
        int first_rest = -1;
        for (int i = 0; i < p->segment_count; ++i) {
            int dur = p->segments[i].end_second - p->segments[i].start_second;
            if (dur <= 0) continue;
            if (p->segments[i].resting) {
                ++rest_count;
                if (first_rest < 0) first_rest = i;
            } else {
                total_play_sec += dur;
            }
        }
        unsigned total_play_min = (unsigned)((total_play_sec + 30) / 60);
        if (total_play_sec > 0 && total_play_min == 0) total_play_min = 1;

        /* 2a. Time Axis Header */
        char start_t[12], stop_t[12], start_lbl[48], stop_lbl[96], mid_lbl[96];
        char stop_reason[64];
        child_format_hhmm(p->now_second, start_t, sizeof(start_t));
        child_format_hhmm(p->stop_second, stop_t, sizeof(stop_t));
        child_format_stop_reasons(p->stop_reasons, stop_reason, sizeof(stop_reason));
        snprintf(start_lbl, sizeof(start_lbl), ptc_ui_text(PTC_UI_T_CHILD_PROJ_START_FMT), start_t);
        snprintf(stop_lbl, sizeof(stop_lbl), "%s %s", stop_t, stop_reason);
        if (first_rest >= 0) {
            char r0s[12], r0e[12];
            child_format_hhmm(p->segments[first_rest].start_second, r0s, sizeof(r0s));
            child_format_hhmm(p->segments[first_rest].end_second, r0e, sizeof(r0e));
            if (first_rest == 0) {
                snprintf(mid_lbl, sizeof(mid_lbl), ptc_ui_text(PTC_UI_T_CHILD_PROJ_RESUME_AT_FMT), r0e);
            } else {
                snprintf(mid_lbl, sizeof(mid_lbl), ptc_ui_text(PTC_UI_T_CHILD_PROJ_REST_WIN_FMT), r0s, r0e);
            }
        } else {
            snprintf(mid_lbl, sizeof(mid_lbl), ptc_ui_text(PTC_UI_T_CHILD_PROJ_CONTINUOUS_FMT), total_play_min);
        }

        int axis_y = box.y + 62;
        bool stop_night = (p->stop_reasons & PTC_UI_STOP_BEDTIME) != 0;
        uint32_t stop_col = stop_night ? UI_NIGHT : (p->stop_reasons == PTC_UI_STOP_DAY_END ? UI_ACCENT : UI_DANGER);
        char fitted_stop_lbl[96];
        fit_text(fitted_stop_lbl, sizeof(fitted_stop_lbl), stop_lbl, 12, rw / 2);
        int sw = measure_text(start_lbl, 12);
        int ew = measure_text(fitted_stop_lbl, 12);
        draw_text_bold(pixels, stride, rx, axis_y, start_lbl, 12, UI_INK);
        draw_text_bold(pixels, stride, rx + rw - ew, axis_y, fitted_stop_lbl, 12, stop_col);
        int mid_space = rw - sw - ew - 28;
        if (mid_space > 80) {
            char fitted_mid[96];
            fit_text(fitted_mid, sizeof(fitted_mid), mid_lbl, 12, mid_space);
            int mw = measure_text(fitted_mid, 12);
            int mx = rx + (rw - mw) / 2;
            if (mx > rx + sw + 10 && mx + mw < rx + rw - ew - 10) {
                draw_text(pixels, stride, mx, axis_y, fitted_mid, 12, UI_MUTED);
            }
        }

        /* 2b. 48px Segmented Track */
        UiRect track = {rx, box.y + 70, rw, 48};
        fill_round_rect(pixels, stride, track, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, track, 12, 1, UI_BORDER);

        const int max_blocks = 7;
        int vis_count = p->segment_count < max_blocks ? p->segment_count : max_blocks;
        int seg_durs[7];
        bool seg_rest[7];
        int total_dur = 0;
        for (int i = 0; i < vis_count; ++i) {
            if (i == max_blocks - 1 && p->segment_count > max_blocks) {
                int merged = 0;
                for (int j = i; j < p->segment_count; ++j)
                    merged += p->segments[j].end_second - p->segments[j].start_second;
                seg_durs[i] = merged > 0 ? merged : 1;
                seg_rest[i] = p->segments[i].resting;
            } else {
                int d = p->segments[i].end_second - p->segments[i].start_second;
                seg_durs[i] = d > 0 ? d : 1;
                seg_rest[i] = p->segments[i].resting;
            }
            total_dur += seg_durs[i];
        }
        if (total_dur <= 0) total_dur = 1;

        const int seg_gap = 4;
        const int inner_x = track.x + 4;
        const int inner_y = track.y + 4;
        const int inner_h = track.height - 8;
        const int inner_w = track.width - 8;
        const int avail_w = inner_w - seg_gap * (vis_count - 1);
        int min_w = vis_count > 1 ? 34 : avail_w;
        if (min_w * vis_count > avail_w) min_w = avail_w / vis_count;
        int extra_w = avail_w - min_w * vis_count;
        int widths[7];
        int used_w = 0;
        for (int i = 0; i < vis_count; ++i) {
            int add = (int)((int64_t)extra_w * seg_durs[i] / total_dur);
            widths[i] = min_w + add;
            used_w += widths[i];
        }
        if (vis_count > 0) widths[vis_count - 1] += (avail_w - used_w);

        uint32_t rest_bg = dark ? UI_WARNING : UI_RGB(0xD98218);
        uint32_t rest_fg = dark ? UI_RGB(0x1E1505) : UI_RGB(0xFFFFFF);
        int bx = inner_x;
        for (int i = 0; i < vis_count; ++i) {
            UiRect blk = {bx, inner_y, widths[i], inner_h};
            uint32_t bbg = seg_rest[i] ? rest_bg : UI_ACCENT;
            uint32_t bfg = seg_rest[i] ? rest_fg : UI_ON_ACCENT;
            fill_round_rect(pixels, stride, blk, 8, bbg);

            unsigned mins = (unsigned)((seg_durs[i] + 30) / 60);
            if (mins == 0) mins = 1;
            char full_t[48], mid_t[32], num_t[16];
            snprintf(full_t, sizeof(full_t),
                     ptc_ui_text(seg_rest[i] ? PTC_UI_T_CHILD_SEG_REST_FULL : PTC_UI_T_CHILD_SEG_PLAY_FULL), mins);
            snprintf(mid_t, sizeof(mid_t),
                     ptc_ui_text(seg_rest[i] ? PTC_UI_T_CHILD_SEG_REST_MID : PTC_UI_T_CHILD_SEG_PLAY_MID), mins);
            snprintf(num_t, sizeof(num_t), "%u", mins);

            if (measure_text(full_t, 13) <= blk.width - 10) {
                draw_text_center(pixels, stride, blk, full_t, 13, bfg);
            } else if (measure_text(mid_t, 12) <= blk.width - 8) {
                draw_text_center(pixels, stride, blk, mid_t, 12, bfg);
            } else if (measure_text(num_t, 11) <= blk.width - 4) {
                draw_text_center(pixels, stride, blk, num_t, 11, bfg);
            }
            bx += widths[i] + seg_gap;
        }

        /* 2c. 2-4 Milestone Step Cards */
        ChildMilestoneStep steps[4];
        int step_count = child_build_milestones(p, steps);
        if (step_count > 0) {
            const int step_gap = 10;
            const int step_y = box.y + 128;
            const int step_h = 88;
            int step_w = (rw - step_gap * (step_count - 1)) / step_count;
            for (int i = 0; i < step_count; ++i) {
                int sx = rx + i * (step_w + step_gap);
                int cw = (i == step_count - 1) ? (rx + rw - sx) : step_w;
                UiRect scard = {sx, step_y, cw, step_h};
                uint32_t sbg = UI_ACCENT_SOFT, sbd = UI_BORDER, sfg = UI_ACCENT;
                const char *badge = ptc_ui_text(PTC_UI_T_CHILD_STEP_BADGE_PLAY);
                if (steps[i].tone == CHILD_STEP_REST) {
                    sbg = UI_WARNING_SOFT; sbd = UI_WARNING_BORDER; sfg = UI_WARNING_TEXT;
                    badge = ptc_ui_text(PTC_UI_T_CHILD_STEP_BADGE_REST);
                } else if (steps[i].tone == CHILD_STEP_NIGHT) {
                    sbg = UI_NIGHT_SOFT; sbd = UI_NIGHT_BORDER; sfg = UI_NIGHT;
                    badge = ptc_ui_text(PTC_UI_T_CHILD_STEP_BADGE_NIGHT);
                } else if (steps[i].tone == CHILD_STEP_STOP) {
                    sbg = UI_DANGER_SOFT; sbd = UI_DANGER_BORDER; sfg = UI_DANGER;
                    badge = ptc_ui_text(PTC_UI_T_CHILD_STEP_BADGE_STOP);
                }
                fill_round_rect(pixels, stride, scard, 12, sbg);
                draw_rect_outline(pixels, stride, scard, 12, 1, sbd);

                int bw = measure_text(badge, 11) + 14;
                UiRect bpill = {scard.x + scard.width - 10 - bw, scard.y + 10, bw, 20};
                fill_round_rect(pixels, stride, bpill, 10, UI_SURFACE);
                draw_text_center(pixels, stride, bpill, badge, 11, sfg);

                char f_time[24], f_title[96], f_sub[96];
                fit_text(f_time, sizeof(f_time), steps[i].time, 14, bpill.x - (scard.x + 12) - 6);
                draw_text_bold(pixels, stride, scard.x + 12, scard.y + 25, f_time, 14, sfg);

                fit_text(f_title, sizeof(f_title), steps[i].title, 14, scard.width - 24);
                draw_text_bold(pixels, stride, scard.x + 12, scard.y + 53, f_title, 14, UI_INK);

                fit_text(f_sub, sizeof(f_sub), steps[i].sub, 12, scard.width - 24);
                draw_text(pixels, stride, scard.x + 12, scard.y + 74, f_sub, 12, UI_MUTED);
            }
        }

        /* 2d. Footer Divider + Active-only Rules + Concise Totals */
        draw_line(pixels, stride, rx, footer_y, rx + rw, footer_y, 1, UI_BORDER);
        char right_sum[128], fitted_sum[128];
        if ((p->stop_reasons & PTC_UI_STOP_DOCK) && !p->total_unlimited && p->total_at_stop > 0) {
            snprintf(right_sum, sizeof(right_sum),
                     ptc_ui_text(PTC_UI_T_CHILD_STEP_TV_FOOTER_FMT), (unsigned)p->total_at_stop);
        } else if (rest_count > 0) {
            snprintf(right_sum, sizeof(right_sum),
                     ptc_ui_text(PTC_UI_T_CHILD_STEP_TOTAL_RESTS_FMT), total_play_min, rest_count);
        } else {
            snprintf(right_sum, sizeof(right_sum),
                     ptc_ui_text(PTC_UI_T_CHILD_STEP_TOTAL_PLAY_FMT), total_play_min);
        }
        fit_text(fitted_sum, sizeof(fitted_sum), right_sum, 12, rw / 2 - 12);
        int sum_w = measure_text(fitted_sum, 12);
        draw_text(pixels, stride, rx + rw - sum_w, footer_y + 27, fitted_sum, 12, UI_MUTED);
        draw_child_rule_strip(pixels, stride, model, overview, p, rx, footer_y + 11, rx + rw - sum_w - 14);
    } else {
        /* Unavailable / Blocked / Pending Honest Explanation Card */
        UiRect card = {rx, box.y + 52, rw, footer_y - (box.y + 52) - 12};
        bool hot_reload_pending = model->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING &&
            !model->hot_reload_capture_restart;
        bool hot_reload_recovery = model->hot_reload_status == PTC_UI_HOT_RELOAD_RECOVERY_REQUIRED;
        bool reconfirm = ptc_ui_runtime_fingerprint_reconfirmation_needed(model);
        bool protection_or_failed = !reconfirm &&
            (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0);
        bool manual_disabled = !reconfirm && model->disable_flag_present;
        bool is_pending = !p->fresh || !overview->blocked;
        bool is_night = overview->blocked && overview->reasons == PTC_UI_STOP_BEDTIME;
        bool is_dock = overview->blocked && (overview->reasons & PTC_UI_STOP_DOCK);
        bool is_urgent_recovery = hot_reload_recovery || (!is_pending && !is_night && !is_dock);
        uint32_t cbg = is_urgent_recovery ? UI_DANGER_SOFT :
            (is_pending ? UI_WARNING_SOFT : (is_night ? UI_NIGHT_SOFT : (is_dock ? UI_ACCENT_SOFT : UI_DANGER_SOFT)));
        uint32_t cbd = is_urgent_recovery ? UI_DANGER_BORDER :
            (is_pending ? UI_WARNING_BORDER : (is_night ? UI_NIGHT_BORDER : (is_dock ? UI_ACCENT : UI_DANGER_BORDER)));
        uint32_t cfg = is_urgent_recovery ? UI_DANGER :
            (is_pending ? UI_WARNING_TEXT : (is_night ? UI_NIGHT : (is_dock ? UI_ACCENT : UI_DANGER)));
        fill_round_rect(pixels, stride, card, 14, cbg);
        draw_rect_outline(pixels, stride, card, 14, 1, cbd);

        const char *headline = overview->title;
        const char *subtext = overview->hint;
        if (hot_reload_recovery) {
            headline = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_HOT_RELOAD_RECOVERY_TITLE);
            subtext = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_HOT_RELOAD_RECOVERY_SUB);
        } else if (hot_reload_pending) {
            headline = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_HOT_RELOAD_TITLE);
            subtext = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_HOT_RELOAD_SUB);
        } else if (reconfirm) {
            headline = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_RECONFIRM_TITLE);
            subtext = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_RECONFIRM_SUB);
        } else if (model->recovery_active) {
            headline = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_RECOVERY_TITLE);
            subtext = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_RECOVERY_SUB);
        } else if (manual_disabled) {
            headline = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_DISABLED_TITLE);
            subtext = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_DISABLED_SUB);
        } else if (protection_or_failed) {
            headline = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_PROTECTION_TITLE);
            subtext = ptc_ui_text(PTC_UI_T_CHILD_GUIDE_PROTECTION_SUB);
        } else if (!p->fresh) {
            subtext = ptc_ui_text(PTC_UI_T_CHILD_UNAVAIL_PENDING_SUB);
        } else if (overview->blocked) {
            if (overview->reasons == PTC_UI_STOP_DAILY) subtext = ptc_ui_text(PTC_UI_T_CHILD_UNAVAIL_DAILY_SUB);
            else if (overview->reasons == PTC_UI_STOP_DOCK) subtext = ptc_ui_text(PTC_UI_T_CHILD_UNAVAIL_DOCK_SUB);
            else if (overview->reasons == PTC_UI_STOP_BEDTIME) subtext = ptc_ui_text(PTC_UI_T_CHILD_UNAVAIL_BED_SUB);
        } else if (p->pending_reason == PTC_UI_ESTIMATE_TEMPORARY_UNLOCK) {
            headline = ptc_ui_text(PTC_UI_T_OVERVIEW_TEMP_UNLOCK_TITLE);
            subtext = ptc_ui_text(PTC_UI_T_CHILD_UNAVAIL_TEMP_UNLOCK_SUB);
        } else {
            headline = child_pending_reason_text(p->pending_reason);
        }

        char fitted_head[160];
        fit_text(fitted_head, sizeof(fitted_head), headline, 20, card.width - 44);
        draw_text_bold(pixels, stride, card.x + 22, card.y + 46, fitted_head, 20, cfg);
        draw_wrapped_text(pixels, stride, card.x + 22, card.y + 82, subtext, 15, card.width - 44, 22, 3, UI_INK);

        draw_line(pixels, stride, rx, footer_y, rx + rw, footer_y, 1, UI_BORDER);
        draw_child_rule_strip(pixels, stride, model, overview, p, rx, footer_y + 11, rx + rw);
    }
}

static void draw_child_today_hero(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                  const PtcUiOverview *overview, const PtcUiTodayProjection *p)
{
    UiRect top = to_uirect(ptc_ui_child_hero_rect());
    const bool dark = (g_theme.resolved == PTC_UI_RESOLVED_DARK);
    const int left_w = 316;
    UiRect left = {top.x, top.y, left_w, top.height};
    UiRect right = {top.x + left_w, top.y, top.width - left_w, top.height};

    /* Semantic left hero colors derived from active theme palette */
    uint32_t bg_rgb = UI_BLENDED(hero);
    uint32_t border_col = UI_BORDER;
    uint32_t dot_col = UI_SUCCESS;
    if (overview->available) {
        bg_rgb = UI_BLENDED(hero);
        border_col = UI_BORDER;
        dot_col = UI_RGB(0x4ADE80);
    } else if (overview->blocked && overview->reasons == PTC_UI_STOP_BEDTIME) {
        bg_rgb = dark ? ui_mix_rgb(UI_BLENDED(night_soft), UI_BLENDED(night), 22)
                      : ui_mix_rgb(UI_BLENDED(night), 0x16102B, 34);
        border_col = UI_NIGHT_BORDER;
        dot_col = UI_NIGHT;
    } else if (overview->state == PTC_POLICY_WAITING || !overview->fresh || !overview->blocked) {
        bg_rgb = dark ? ui_mix_rgb(UI_BLENDED(warning_soft), UI_BLENDED(warning), 22)
                      : ui_mix_rgb(UI_BLENDED(warning), 0x241404, 28);
        border_col = UI_WARNING_BORDER;
        dot_col = UI_RGB(0xF6C453);
    } else {
        bg_rgb = dark ? ui_mix_rgb(UI_BLENDED(danger_soft), UI_BLENDED(danger), 22)
                      : ui_mix_rgb(UI_BLENDED(danger), 0x24080C, 28);
        border_col = UI_DANGER_BORDER;
        dot_col = UI_RGB(0xFF9EA8);
    }
    uint32_t hero_bg = UI_RGB(bg_rgb);
    uint32_t pill_bg = UI_RGB(ui_mix_rgb(bg_rgb, 0xFFFFFF, 16));
    uint32_t div_col = UI_RGB(ui_mix_rgb(bg_rgb, 0xFFFFFF, 22));
    uint32_t track_col = UI_RGB(ui_mix_rgb(bg_rgb, 0xFFFFFF, 20));

    /* Outer unified rounded card */
    draw_card_shadow(pixels, stride, top, 16);
    fill_round_rect(pixels, stride, top, 16, UI_SURFACE);
    /* Left semantic half with flat right edge */
    fill_round_rect(pixels, stride, left, 16, hero_bg);
    fill_rect(pixels, stride, (UiRect){left.x + left.width - 16, left.y, 16, left.height}, hero_bg);

    /* Left Top: Status Pill */
    bool hot_reload_pending = model->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING &&
        !model->hot_reload_capture_restart;
    bool hot_reload_recovery = model->hot_reload_status == PTC_UI_HOT_RELOAD_RECOVERY_REQUIRED;
    bool reconfirm = ptc_ui_runtime_fingerprint_reconfirmation_needed(model);
    bool parent_step_needed = hot_reload_pending || hot_reload_recovery || reconfirm ||
        model->disable_flag_present ||
        strcmp(model->setup_phase, "protection") == 0 ||
        strcmp(model->setup_phase, "failed") == 0;
    const char *pill_title = overview->title;
    if (!overview->available) {
        if (hot_reload_recovery) pill_title = ptc_ui_text(PTC_UI_T_REQUIRES_RECOVERY);
        else if (hot_reload_pending) pill_title = ptc_ui_text(PTC_UI_T_TO_BE_LOADED);
        else if (reconfirm) pill_title = ptc_ui_text(PTC_UI_T_RECONFIRM_BADGE);
    }
    char fitted_pill[96];
    fit_text(fitted_pill, sizeof(fitted_pill), pill_title, 13, left.width - 68);
    int pill_w = measure_text(fitted_pill, 13) + 34;
    UiRect spill = {left.x + 20, left.y + 18, pill_w, 26};
    fill_round_rect(pixels, stride, spill, 13, pill_bg);
    fill_round_rect(pixels, stride, (UiRect){spill.x + 11, spill.y + 9, 8, 8}, 4, dot_col);
    draw_text_bold(pixels, stride, spill.x + 25, spill.y + 18, fitted_pill, 13, UI_ON_HERO);

    /* Left Mid: Hero Label + Big Value */
    bool temp_unlocked = !parent_step_needed && p->pending_reason == PTC_UI_ESTIMATE_TEMPORARY_UNLOCK;
    const char *hero_lbl = overview->available ? ptc_ui_text(PTC_UI_T_CHILD_HERO_PLAYABLE) :
        (parent_step_needed ? ptc_ui_text(PTC_UI_T_CHILD_HERO_NEXT_STEP) :
        (overview->reasons == PTC_UI_STOP_EYE ? ptc_ui_text(PTC_UI_T_CHILD_HERO_REST_LEFT) :
        (!overview->fresh ? ptc_ui_text(PTC_UI_T_CHILD_HERO_PENDING) :
        (temp_unlocked ? ptc_ui_text(PTC_UI_T_CHILD_HERO_TEMP_UNLOCK) :
        (overview->reasons == PTC_UI_STOP_DAILY ? ptc_ui_text(PTC_UI_T_CHILD_HERO_ENDED) :
        ptc_ui_text(PTC_UI_T_CHILD_HERO_BLOCKED))))));
    char fitted_lbl[96];
    fit_text(fitted_lbl, sizeof(fitted_lbl), hero_lbl, 14, left.width - 40);
    draw_text(pixels, stride, left.x + 22, left.y + 72, fitted_lbl, 14, UI_HERO_SECONDARY);

    if (overview->available && p->simulation_available && p->continuous_seconds >= 0) {
        char num[24];
        snprintf(num, sizeof(num), "%u", (unsigned)(p->continuous_seconds / 60));
        const char *unit = ptc_ui_text(PTC_UI_T_MIN);
        int num_sz = strlen(num) > 3 ? 38 : 48;
        int nw = measure_text(num, num_sz);
        draw_text_bold(pixels, stride, left.x + 22, left.y + 126, num, num_sz, UI_ON_HERO);
        draw_text_bold(pixels, stride, left.x + 22 + nw + 8, left.y + 124, unit, 20, UI_HERO_SECONDARY);
    } else {
        const char *hero_val = overview->value;
        if (hot_reload_recovery) hero_val = ptc_ui_text(PTC_UI_T_CHILD_HERO_REBOOT_HOST);
        else if (parent_step_needed) hero_val = ptc_ui_text(PTC_UI_T_CHILD_HERO_GO_PARENT);
        char fitted_val[64];
        int val_sz = 40;
        while (val_sz > 24 && measure_text(hero_val, val_sz) > left.width - 44) val_sz -= 4;
        fit_text(fitted_val, sizeof(fitted_val), hero_val, val_sz, left.width - 44);
        draw_text_bold(pixels, stride, left.x + 22, left.y + 124, fitted_val, val_sz, UI_ON_HERO);
    }

    const char *notice = ptc_ui_runtime_notice_summary(model);
    const char *feedback = hot_reload_pending ? ptc_ui_text(PTC_UI_T_A_NEW_VERSION_HAS_BEEN_INSTALLED_PLEASE) :
        (notice[0] ? notice :
        (ptc_ui_operation_feedback_visible(model) && model->message[0] ? model->message : ""));
    if (feedback[0]) {
        char fitted_fb[128];
        fit_text(fitted_fb, sizeof(fitted_fb), feedback, 12, left.width - 44);
        draw_text(pixels, stride, left.x + 22, left.y + 154, fitted_fb, 12,
                  hot_reload_pending ? UI_ON_HERO : UI_HERO_SECONDARY);
    }

    /* Left Bottom: Integrated Daily Balance + Progress Bar + Played/Source */
    int bal_y = left.y + 174;
    draw_line(pixels, stride, left.x + 20, bal_y, left.x + left.width - 20, bal_y, 1, div_col);

    char bal_val[64], fitted_bal[64];
    int total_min = 0;
    if (!overview->fresh || !overview->total_known) {
        snprintf(bal_val, sizeof(bal_val), "%s", ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED));
    } else if (overview->total_unlimited) {
        snprintf(bal_val, sizeof(bal_val), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    } else if (model->forecast_available && model->forecast[0].day_index == model->day_index &&
               model->forecast[0].mode == PTC_RULE_MODE_LIMIT && model->forecast[0].minutes > 0) {
        total_min = model->forecast[0].minutes;
        snprintf(bal_val, sizeof(bal_val), ptc_ui_text(PTC_UI_T_CHILD_BAL_FRAC),
                 (unsigned)overview->total_remaining, (unsigned)total_min);
    } else {
        snprintf(bal_val, sizeof(bal_val), "%s", overview->balance);
    }
    fit_text(fitted_bal, sizeof(fitted_bal), bal_val, 15, left.width - 110);
    int bw = measure_text(fitted_bal, 15);
    draw_text(pixels, stride, left.x + 20, bal_y + 28, ptc_ui_text(PTC_UI_T_CHILD_BAL_SHORT), 13, UI_HERO_SECONDARY);
    draw_text_bold(pixels, stride, left.x + left.width - 20 - bw, bal_y + 28, fitted_bal, 15, UI_ON_HERO);

    UiRect bar = {left.x + 20, bal_y + 42, left.width - 40, 8};
    fill_round_rect(pixels, stride, bar, 4, track_col);
    if (overview->fresh && overview->total_known) {
        if (overview->total_unlimited) {
            fill_round_rect(pixels, stride, bar, 4, dot_col);
        } else if (overview->total_remaining <= 0) {
            fill_round_rect(pixels, stride, bar, 4, UI_RGB(0xFFAAB4));
        } else {
            int denom = total_min > 0 ? total_min : (overview->total_remaining + (model->played_minutes > 0 ? model->played_minutes : 0));
            if (denom <= 0) denom = overview->total_remaining;
            int fw = bar.width * overview->total_remaining / denom;
            if (fw < 6) fw = 6;
            if (fw > bar.width) fw = bar.width;
            fill_round_rect(pixels, stride, (UiRect){bar.x, bar.y, fw, bar.height}, 4, dot_col);
        }
    }

    char played_str[64], src_str[64], f_played[64], f_src[64];
    if (overview->fresh && model->played_minutes_available && model->played_minutes >= 0) {
        snprintf(played_str, sizeof(played_str), ptc_ui_text(PTC_UI_T_CHILD_PLAYED_SHORT), model->played_minutes);
    } else {
        snprintf(played_str, sizeof(played_str), "%s", ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED));
    }
    snprintf(src_str, sizeof(src_str), "%s", overview->fresh ? ui_rule_source_label(model->rule_source) : ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM));
    fit_text(f_src, sizeof(f_src), src_str, 12, left.width / 2 - 22);
    int src_w = measure_text(f_src, 12);
    fit_text(f_played, sizeof(f_played), played_str, 12, left.width - 44 - src_w - 8);
    draw_text(pixels, stride, left.x + 20, bal_y + 74, f_played, 12, UI_HERO_SECONDARY);
    draw_text(pixels, stride, left.x + left.width - 20 - src_w, bal_y + 74, f_src, 12, UI_HERO_SECONDARY);

    /* Right Half: Continuous-Use Projection Stage */
    draw_child_projection_stage(pixels, stride, model, overview, p, right);

    /* Outer Border on top for crisp unified edge */
    draw_rect_outline(pixels, stride, top, 16, 1, border_col);
}

/* ---------- Tier 2: Spacious Full-Width 7-Day Plan Calendar ---------- */

static void draw_child_forecast_columns(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                        const PtcUiOverview *overview, UiRect container)
{
    draw_card_shadow(pixels, stride, container, 16);
    fill_round_rect(pixels, stride, container, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, container, 16, 1, UI_BORDER);

    /* Header row */
    draw_child_calendar_icon(pixels, stride, container.x + 26, container.y + 19, UI_ACCENT);
    int title_x = container.x + 44;
    int heading_width = container.width - 60;
    char title[192], summary[160], detail[160];
    int64_t now = ptc_ui_render_now();
    int focused_offset = model->child_forecast_focus - 1;
    int active_offset = focused_offset >= 0 ? focused_offset : 0;
    PtcUiChildForecastSource active_source = ptc_ui_child_forecast_source(model, active_offset, now);
    const char *summary_text = ptc_ui_text(PTC_UI_T_REST_AS_PLANNED);
    uint32_t summary_col = UI_MUTED;

    if (focused_offset >= 0 && active_source != PTC_UI_CHILD_FORECAST_UNKNOWN) {
        PtcDayRule rule = child_forecast_rule(model, focused_offset, active_source);
        uint16_t target_day = model->day_index + (uint16_t)focused_offset;
        char date[32], quota[40], bed_part[40] = "";
        uint16_t bed_min = 0;
        ptc_format_date(target_day, date);
        if (rule.mode == PTC_RULE_MODE_LIMIT)
            snprintf(quota, sizeof(quota), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)rule.minutes);
        else
            snprintf(quota, sizeof(quota), "%s", ptc_ui_text(rule.mode == PTC_RULE_MODE_UNLIMITED ?
                PTC_UI_T_ADJUST_BADGE_UNLIMITED : PTC_UI_T_DISABLED));
        if (child_day_bedtime_start(model, target_day, &bed_min)) {
            char bed_lbl[32];
            snprintf(bed_lbl, sizeof(bed_lbl), ptc_ui_text(PTC_UI_T_CHILD_DAY_BED_FMT),
                     (unsigned)(bed_min / 60), (unsigned)(bed_min % 60));
            snprintf(bed_part, sizeof(bed_part), " / %s", bed_lbl);
        }
        snprintf(detail, sizeof(detail), "%s%s / %s%s",
                 ptc_ui_text(PTC_UI_T_CHILD_VIEWING_PREFIX), date, quota, bed_part);
        summary_text = detail;
        summary_col = UI_ACCENT;
    }

    fit_text(summary, sizeof(summary), summary_text, 13, heading_width / 2);
    int summary_width = measure_text(summary, 13);
    int summary_x = container.x + container.width - 16 - summary_width;
    fit_text(title, sizeof(title), ptc_ui_text(PTC_UI_T_7_DAY_PLAN_BEDTIME_FORECAST), 16,
             summary_x - title_x - 16);
    draw_text_bold(pixels, stride, title_x, container.y + 25, title, 16, UI_INK);
    draw_text(pixels, stride, summary_x, container.y + 25, summary, 13, summary_col);

    /* Compute max quota across known limited days for proportional bars */
    int max_min = 120;
    for (int i = 0; i < (int)PTC_RESULT_FORECAST_DAYS; ++i) {
        PtcUiChildForecastSource src = ptc_ui_child_forecast_source(model, i, now);
        if (src != PTC_UI_CHILD_FORECAST_UNKNOWN) {
            PtcDayRule r = child_forecast_rule(model, i, src);
            if (r.mode == PTC_RULE_MODE_LIMIT && (int)r.minutes > max_min) max_min = r.minutes;
        }
    }

    for (int i = 0; i < (int)PTC_RESULT_FORECAST_DAYS; ++i) {
        UiRect card = to_uirect(ptc_ui_child_forecast_day_rect(i, 1));
        uint16_t target_day = model->day_index + (uint16_t)i;
        PtcUiChildForecastSource source = ptc_ui_child_forecast_source(model, i, now);
        if (source == PTC_UI_CHILD_FORECAST_UNKNOWN) {
            int end = i + 1;
            while (end < (int)PTC_RESULT_FORECAST_DAYS &&
                ptc_ui_child_forecast_source(model, end, now) == PTC_UI_CHILD_FORECAST_UNKNOWN) ++end;
            card = to_uirect(ptc_ui_child_forecast_day_rect(i, end - i));
            fill_round_rect(pixels, stride, card, 12, UI_RAISED);
            draw_rect_outline(pixels, stride, card, 12, 1, UI_BORDER);
            char fitted[160];
            fit_text(fitted, sizeof(fitted), ptc_ui_text(end - i == 1 ? PTC_UI_T_TO_BE_CONFIRMED :
                PTC_UI_T_REFRESH_STATUS_TO_VIEW_FORECAST_DATA), 15, card.width - 24);
            draw_text_center(pixels, stride, card, fitted, 15, UI_MUTED);
            i = end - 1;
            continue;
        }

        uint16_t yr = 0;
        uint8_t mo = 0, da = 0;
        bool date_ok = ptc_date_from_day_index(target_day, &yr, &mo, &da);
        uint8_t w = ptc_weekday_from_day_index(target_day);
        bool is_weekend = (w == 0 || w == 6);
        bool is_today = (i == 0);
        bool is_tomorrow = (i == 1);
        bool focused = (focused_offset == i);

        /* Today is the primary accent highlight; weekends soft green; normal days neutral. */
        uint32_t card_bg = is_today ? UI_ACCENT_SOFT :
            (is_weekend ? UI_SUCCESS_SOFT : UI_RAISED);
        uint32_t border_col = is_today ? UI_ACCENT :
            (is_weekend ? UI_SUCCESS_BORDER : UI_BORDER);
        fill_round_rect(pixels, stride, card, 12, card_bg);
        if (focused) draw_focus_ring(pixels, stride, card, 12);
        else draw_rect_outline(pixels, stride, card, 12, is_today ? 2 : 1, border_col);

        /* Day label + Date */
        char day_title[32];
        uint32_t title_color = is_today ? UI_ACCENT : (is_weekend ? UI_SUCCESS : UI_INK);
        if (is_today) snprintf(day_title, sizeof(day_title), "%s", ptc_ui_text(PTC_UI_T_CHILD_TODAY_SHORT));
        else if (is_tomorrow) snprintf(day_title, sizeof(day_title), "%s", ptc_ui_text(PTC_UI_T_CHILD_TOMORROW_SHORT));
        else snprintf(day_title, sizeof(day_title), "%s", ptc_ui_weekday_label(w));
        draw_text_center(pixels, stride, (UiRect){card.x + 4, card.y + 8, card.width - 8, 22}, day_title, 15, title_color);

        char date_str[32];
        if (date_ok) snprintf(date_str, sizeof(date_str), "%02u/%02u", mo, da);
        else snprintf(date_str, sizeof(date_str), "--/--");
        draw_text_center(pixels, stride, (UiRect){card.x + 4, card.y + 28, card.width - 8, 16}, date_str, 12, UI_MUTED);

        /* Quota Value */
        PtcDayRule rule = child_forecast_rule(model, i, source);
        uint32_t val_color = rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS :
            (is_today ? UI_ACCENT : (rule.mode == PTC_RULE_MODE_LIMIT ? UI_INK : UI_MUTED));
        if (rule.mode == PTC_RULE_MODE_LIMIT) {
            char num[16];
            const char *unit = ptc_ui_text(PTC_UI_T_MIN);
            snprintf(num, sizeof(num), "%u", (unsigned)rule.minutes);
            int nsz = strlen(num) >= 4 ? 22 : 28;
            int vw = measure_text(num, nsz);
            int uw = measure_text(unit, 13);
            int tx = card.x + (card.width - (vw + uw + 3)) / 2;
            draw_text_bold(pixels, stride, tx, card.y + 78, num, nsz, val_color);
            draw_text(pixels, stride, tx + vw + 3, card.y + 76, unit, 13, UI_MUTED);
        } else {
            char fitted[64];
            const char *lbl = ptc_ui_text(rule.mode == PTC_RULE_MODE_UNLIMITED ?
                PTC_UI_T_ADJUST_BADGE_UNLIMITED : PTC_UI_T_DISABLED);
            fit_text(fitted, sizeof(fitted), lbl, 21, card.width - 14);
            draw_text_center(pixels, stride, (UiRect){card.x + 6, card.y + 54, card.width - 12, 30}, fitted, 21, val_color);
        }

        /* Bottom Bar Area: Today dual-segment played/left vs Other days bedtime + comparison bar */
        UiRect bar = {card.x + 12, card.y + card.height - 18, card.width - 24, 8};
        if (is_today && overview->fresh && rule.mode == PTC_RULE_MODE_LIMIT && rule.minutes > 0 &&
            overview->total_known && model->played_minutes_available && model->played_minutes >= 0) {
            char rem_s[32], used_s[32], f_rem[32], f_used[32];
            snprintf(rem_s, sizeof(rem_s), ptc_ui_text(PTC_UI_T_CHILD_DAY_REM_FMT), (unsigned)overview->total_remaining);
            snprintf(used_s, sizeof(used_s), ptc_ui_text(PTC_UI_T_CHILD_DAY_PLAYED_FMT), model->played_minutes);
            fit_text(f_rem, sizeof(f_rem), rem_s, 11, bar.width / 2 + 6);
            fit_text(f_used, sizeof(f_used), used_s, 11, bar.width / 2 + 6);
            int rw_rem = measure_text(f_rem, 11);
            int rw_used = measure_text(f_used, 11);
            draw_text_bold(pixels, stride, bar.x, bar.y - 6, f_rem, 11,
                           overview->total_remaining == 0 ? UI_DANGER : UI_ACCENT);
            if (bar.x + rw_rem + 4 <= bar.x + bar.width - rw_used) {
                draw_text(pixels, stride, bar.x + bar.width - rw_used, bar.y - 6, f_used, 11, UI_MUTED);
            }
            fill_round_rect(pixels, stride, bar, 4, UI_BORDER);
            if (overview->total_remaining > 0) {
                int fw = bar.width * overview->total_remaining / (int)rule.minutes;
                if (fw < 6) fw = 6;
                if (fw > bar.width) fw = bar.width;
                fill_round_rect(pixels, stride, (UiRect){bar.x, bar.y, fw, bar.height}, 4, UI_ACCENT);
            } else {
                fill_round_rect(pixels, stride, bar, 4, UI_DANGER);
            }
        } else {
            uint16_t bed_min = 0;
            bool has_bed = child_day_bedtime_start(model, target_day, &bed_min);
            const char *tag = child_forecast_special_tag(model, i, source, is_weekend);
            int right_edge = bar.x + bar.width;
            if (tag && tag[0]) {
                char f_tag[32];
                fit_text(f_tag, sizeof(f_tag), tag, 11, bar.width / 2);
                int tw = measure_text(f_tag, 11);
                draw_text_bold(pixels, stride, right_edge - tw, bar.y - 6, f_tag, 11,
                               is_weekend ? UI_SUCCESS : UI_ACCENT);
                right_edge -= (tw + 6);
            }
            if (has_bed && right_edge - bar.x >= 44) {
                char bed_t[16];
                snprintf(bed_t, sizeof(bed_t), "%02u:%02u", (unsigned)(bed_min / 60), (unsigned)(bed_min % 60));
                draw_child_moon_icon(pixels, stride, bar.x + 5, bar.y - 10, 4, UI_NIGHT, card_bg);
                draw_text(pixels, stride, bar.x + 13, bar.y - 6, bed_t, 11, UI_MUTED);
            }

            fill_round_rect(pixels, stride, bar, 4, is_today || is_weekend ? UI_SURFACE : UI_BORDER);
            int pct = 0;
            if (rule.mode == PTC_RULE_MODE_UNLIMITED) pct = 100;
            else if (rule.mode == PTC_RULE_MODE_LIMIT && rule.minutes > 0) {
                pct = (int)rule.minutes * 100 / max_min;
                if (pct < 16) pct = 16;
                if (pct > 100) pct = 100;
            }
            if (pct > 0) {
                int fw = bar.width * pct / 100;
                uint32_t bar_col = is_today ? UI_ACCENT :
                    ((is_weekend || rule.mode == PTC_RULE_MODE_UNLIMITED) ? UI_SUCCESS : UI_CONTROL);
                fill_round_rect(pixels, stride, (UiRect){bar.x, bar.y, fw, bar.height}, 4, bar_col);
            }
        }
    }
}

/* ---------- Tier 3: Subtle Single-Row Action Bar ---------- */

static void draw_child_action_pill(uint32_t *pixels, uint32_t stride, PtcUiRect target,
                                   const char *key, const char *label, int icon_kind,
                                   bool ghost, bool highlighted, bool completed,
                                   bool selected, bool disabled)
{
    UiRect box = to_uirect(target);
    uint32_t bg = disabled ? UI_RAISED :
        (completed ? UI_SUCCESS_SOFT :
        (highlighted ? UI_ACCENT :
        (ghost ? UI_PAGE : UI_SURFACE)));
    uint32_t bd = disabled ? UI_BORDER :
        (completed ? UI_SUCCESS_BORDER :
        (highlighted ? UI_ACCENT : UI_BORDER));
    uint32_t fg = disabled ? UI_DISABLED :
        (completed ? UI_SUCCESS :
        (highlighted ? UI_ON_ACCENT :
        (ghost ? UI_MUTED : UI_INK)));
    uint32_t ic = disabled ? UI_DISABLED :
        (completed ? UI_SUCCESS :
        (highlighted ? UI_ON_ACCENT :
        (ghost ? UI_MUTED : UI_ACCENT)));

    fill_round_rect(pixels, stride, box, 12, bg);
    if (selected) draw_focus_ring(pixels, stride, box, 12);
    else draw_rect_outline(pixels, stride, box, 12, 1, bd);

    int cy = box.y + box.height / 2;
    int key_size = 22;
    draw_child_keycap(pixels, stride, box.x + 12, cy - key_size / 2, key_size, key, highlighted && !disabled, disabled);
    draw_child_action_icon_mini(pixels, stride, box.x + 48, cy, icon_kind, ic);

    char fitted[96];
    int text_x = box.x + 64;
    int text_max = box.x + box.width - 12 - text_x;
    fit_text(fitted, sizeof(fitted), label, 15, text_max);
    draw_text_bold(pixels, stride, text_x, cy + 5, fitted, 15, fg);
}

void draw_child(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    int64_t now = 0;
    uint16_t day = 0;
    int second = 0;
    PtcUiTodayProjection projection;
    PtcUiOverview overview;
    char buffer[128], hint[160], fitted_hint[160];

    child_local_clock(model, &now, &day, &second);
    ptc_ui_project_today(model, now, day, second, &projection);
    ptc_ui_project_overview(model, now, day, second, &overview);

    bool fresh = ptc_ui_status_is_fresh(model, now);
    bool dock_blocked = fresh && model->dock_restriction_active;
    bool disabled = model->disable_flag_present || model->waiting || dock_blocked;
    bool code_unavailable = fresh &&
        (model->unrestricted_today == 1 || model->eye_care_unlimited_capped || model->dock_unlimited_capped);

    /* Header */
    draw_header_limited(pixels, stride, ptc_ui_text(PTC_UI_T_SELF_DISCIPLINE_IS_FREEDOM),
                        ptc_ui_text(PTC_UI_T_ARRANGE_TIME_REASONABLY_AND_BE_THE_MASTER),
                        ptc_ui_header_language_rect().x - 124 - 16);
    draw_time_status_bar(pixels, stride, model);
    draw_language_switcher_badge(pixels, stride, to_uirect(ptc_ui_header_language_rect()),
                                 model->header_language_focused);

    /* Tier 1: Full-width Today Planning Hero (Left Status/Balance + Right Continuous Projection) */
    draw_child_today_hero(pixels, stride, model, &overview, &projection);

    /* Tier 2: Full-width Spacious 7-Day Plan Calendar */
    draw_child_forecast_columns(pixels, stride, model, &overview, to_uirect(ptc_ui_child_forecast_rect()));

    /* Tier 3: Subtle Single-Row Action Bar */
    bool hot_reload_pending = model->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING &&
        !model->hot_reload_capture_restart;
    bool reconfirm_needed = ptc_ui_runtime_fingerprint_reconfirmation_needed(model);
    bool parent_action_needed = hot_reload_pending ||
        model->hot_reload_status == PTC_UI_HOT_RELOAD_RECOVERY_REQUIRED ||
        reconfirm_needed || model->disable_flag_present ||
        strcmp(model->setup_phase, "protection") == 0 ||
        strcmp(model->setup_phase, "failed") == 0;
    for (int i = 0; i < 4; ++i) {
        PtcUiHitKind kind = ptc_ui_child_action_kind(model, i);
        const char *key = "+";
        const char *label = ptc_ui_text(PTC_UI_T_CHILD_ACT_DETAILS);
        int icon = 2;
        bool ghost = false;
        bool highlighted = false;
        bool completed = false;
        bool unavailable = model->waiting;

        if (kind == PTC_UI_HIT_CHILD_SUBMIT_CODE) {
            key = "A";
            label = code_unavailable ? ptc_ui_text(PTC_UI_T_CHILD_ACT_CODE_UNLIM) : ptc_ui_text(PTC_UI_T_CHILD_ACT_CODE);
            icon = 0;
            unavailable = disabled || code_unavailable;
            highlighted = false;
        } else if (kind == PTC_UI_HIT_CHILD_BUFFER) {
            key = "X";
            if (model->daily_buffer_available) {
                snprintf(buffer, sizeof(buffer), ptc_ui_text(PTC_UI_T_CHILD_ACT_BUF_FMT), (unsigned)model->daily_buffer_minutes);
            } else {
                snprintf(buffer, sizeof(buffer), "%s",
                         ptc_ui_text(model->daily_buffer_claimed ? PTC_UI_T_CHILD_ACT_BUF_USED : PTC_UI_T_CHILD_ACT_BUF_NONE));
            }
            label = buffer;
            completed = model->daily_buffer_claimed;
            icon = completed ? 3 : 1;
            unavailable = disabled || !model->daily_buffer_available;
        } else if (kind == PTC_UI_HIT_CHILD_PARENT) {
            key = "-";
            label = ptc_ui_text(PTC_UI_T_OVERVIEW_PARENT);
            icon = 4;
            unavailable = model->waiting;
            highlighted = (overview.blocked && (i == 0)) || parent_action_needed;
            ghost = !highlighted;
        }

        draw_child_action_pill(pixels, stride, ptc_ui_child_action_rect(i), key, label, icon,
                               ghost, highlighted, completed,
                               model->child_actions_focused && model->child_action_focus == i,
                               unavailable);
    }

    if ((overview.blocked || parent_action_needed) &&
        ptc_ui_child_action_kind(model, 3) == PTC_UI_HIT_CHILD_PARENT) {
        PtcUiRect prev_rect = ptc_ui_child_action_rect(2);
        PtcUiRect p_rect = ptc_ui_child_action_rect(3);
        int avail_w = p_rect.x - (prev_rect.x + prev_rect.w) - 24;
        if (avail_w > 48) {
            const char *callout = hot_reload_pending ? ptc_ui_text(PTC_UI_T_CHILD_ACT_LOAD_NEW_VERSION) :
                (reconfirm_needed ? ptc_ui_text(PTC_UI_T_CHILD_ACT_RECONFIRM_PARENT) :
                (parent_action_needed ? ptc_ui_text(PTC_UI_T_CHILD_ACT_PARENT_HANDLE) :
                 ptc_ui_text(PTC_UI_T_CHILD_ACT_NEED_PARENT)));
            char need_lbl[96];
            fit_text(need_lbl, sizeof(need_lbl), callout, 13, avail_w);
            int nw = measure_text(need_lbl, 13);
            draw_text(pixels, stride, p_rect.x - nw - 12, p_rect.y + p_rect.h / 2 + 5, need_lbl, 13,
                      parent_action_needed ? UI_WARNING_TEXT : UI_MUTED);
        }
    }

    /* Footer */
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_footer_rect(0)),
                      ptc_ui_text((model->child_actions_focused || model->header_language_focused)
                          ? PTC_UI_T_OVERVIEW_FOCUS : PTC_UI_T_A_ENTER_CODE),
                      18, disabled && !model->header_language_focused ? UI_DISABLED : UI_MUTED);
    if (model->show_parent_shortcut_hint && model->custom_shortcut_enabled)
        ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label, hint, sizeof(hint));
    else snprintf(hint, sizeof(hint), "%s", ptc_ui_text(PTC_UI_T_STATUS_AUTO_SYNCS_IN_BACKGROUND));
    fit_text(fitted_hint, sizeof(fitted_hint), hint, 18, ptc_ui_child_footer_rect(1).w - 24);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), fitted_hint, 18, UI_RGB(UI_BLENDED(text_secondary)));
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), ptc_ui_text(PTC_UI_T_B_EXIT));
    draw_refresh_footer_button(pixels, stride, ptc_ui_child_refresh_rect(),
        ptc_ui_text(PTC_UI_T_Y_REFRESH), ptc_ui_status_needs_refresh(model, now),
        ptc_ui_status_refresh_failed(model), false, model->waiting);
}
