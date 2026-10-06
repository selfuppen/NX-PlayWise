#include "ui_render_internal.h"
#include "../../common/time/ptc_time.h"

static float child_remaining_fraction(const PtcUiModel *model, bool *available)
{
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    *available = false;
    if (!fresh) return 0.0f;
    if (model->bedtime_active && !model->bedtime_skipped) {
        *available = true;
        return 0.0f;
    }
    if (model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped)) {
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

static void draw_child_forecast_columns(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, UiRect container)
{
    fill_round_rect(pixels, stride, container, 16, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, container, 16, 1, UI_RGB(UI_BLENDED(border_control)));

    /* 顶部标题栏 */
    draw_child_calendar_icon(pixels, stride, container.x + 28, container.y + 20, UI_ACCENT);
    draw_text(pixels, stride, container.x + 46, container.y + 26, ptc_ui_text(PTC_UI_T_7_DAY_PLAN_BEDTIME_FORECAST), 16, UI_RGB(UI_BLENDED(text_primary)));
    draw_text(pixels, stride, container.x + 280, container.y + 26, ptc_ui_text(PTC_UI_T_REST_AS_PLANNED), 13, UI_MUTED);

    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    const int start_x = container.x + 16;
    const int card_w = 114;
    const int gap = 11;
    const int card_y = container.y + 40;
    const int card_h = 168;

    for (int i = 0; i < 7; ++i) {
        UiRect card = {start_x + i * (card_w + gap), card_y, card_w, card_h};
        uint16_t target_day = model->day_index + (uint16_t)i;
        uint16_t yr = 0;
        uint8_t mo = 0, da = 0;
        bool date_ok = ptc_date_from_day_index(target_day, &yr, &mo, &da);
        uint8_t w = ptc_weekday_from_day_index(target_day);
        bool is_weekend = (w == 0 || w == 6);
        bool is_tomorrow = (i == 1);
        bool is_today = (i == 0);

        uint32_t card_bg = is_tomorrow ? UI_RGB(UI_BLENDED(surface_raised)) : UI_RGB(UI_BLENDED(surface_raised));
        uint32_t border_col = is_tomorrow ? UI_ACCENT : (is_weekend ? UI_SUCCESS : UI_RGB(UI_BLENDED(border_control)));
        fill_round_rect(pixels, stride, card, 10, card_bg);
        draw_rect_outline(pixels, stride, card, 10, is_tomorrow ? 2 : 1, border_col);

        /* 星期与特殊标签 */
        char day_title[32];
        uint32_t title_color = is_tomorrow ? UI_ACCENT : (is_weekend ? UI_SUCCESS : UI_RGB(UI_BLENDED(text_primary)));
        if (is_today) {
            snprintf(day_title, sizeof(day_title), "%s", ptc_ui_text(PTC_UI_T_TODAY));
        } else if (is_tomorrow) {
            snprintf(day_title, sizeof(day_title), "%s", ptc_ui_text(PTC_UI_T_TOMORROW));
        } else {
            snprintf(day_title, sizeof(day_title), "%s", ptc_ui_weekday_label(w));
        }
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 8, card.width, 22}, day_title, 14, title_color);

        /* 日期 MM/DD */
        char date_str[32];
        if (date_ok) snprintf(date_str, sizeof(date_str), "%02u/%02u", mo, da);
        else snprintf(date_str, sizeof(date_str), "--/--");
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 32, card.width, 18}, date_str, 12, UI_MUTED);

        /* 额度大数值 */
        char val_str[32];
        char unit_str[16] = "";
        uint32_t val_color = UI_INK;
        bool available = fresh && model->forecast_available && i < 7 &&
            model->forecast[i].day_index == target_day;

        if (available && i == 0 && (model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped))) {
            snprintf(val_str, sizeof(val_str), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
            val_color = UI_SUCCESS;
        } else if (available) {
            const PtcResultForecastDay *day = &model->forecast[i];
            if (day->mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(val_str, sizeof(val_str), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
                val_color = UI_SUCCESS;
            } else if (day->mode == PTC_RULE_MODE_LIMIT) {
                snprintf(val_str, sizeof(val_str), "%u", (unsigned int)day->minutes);
                snprintf(unit_str, sizeof(unit_str), "%s", ptc_ui_text(PTC_UI_T_MIN));
            } else {
                snprintf(val_str, sizeof(val_str), "%s", ptc_ui_text(PTC_UI_T_DISABLED));
                val_color = UI_MUTED;
            }
        } else {
            snprintf(val_str, sizeof(val_str), "%s", fresh ? ptc_ui_text(PTC_UI_T_UNAVAILABLE) : ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED));
            val_color = UI_MUTED;
        }

        if (unit_str[0]) {
            int vw = measure_text(val_str, 24);
            int uw = measure_text(unit_str, 13);
            int tx = card.x + (card.width - (vw + uw + 4)) / 2;
            draw_text_bold(pixels, stride, tx, card.y + 86, val_str, 24, val_color);
            draw_text(pixels, stride, tx + vw + 4, card.y + 84, unit_str, 13, UI_MUTED);
        } else {
            draw_text_center(pixels, stride, (UiRect){card.x, card.y + 66, card.width, 30}, val_str, 20, val_color);
        }

        /* 规则来源标签 */
        char source[64];
        char fitted_src[64];
        if (available && i == 0 && (model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped))) {
            snprintf(source, sizeof(source), "%s", ui_rule_source_label(model->rule_source));
        } else if (available) {
            snprintf(source, sizeof(source), "%s", ui_rule_source_label(model->forecast[i].rule_source));
        } else {
            snprintf(source, sizeof(source), "%s", fresh ? ptc_ui_text(PTC_UI_T_UNAVAILABLE) : ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM));
        }
        fit_text(fitted_src, sizeof(fitted_src), source, 11, card.width - 12);
        draw_text_center(pixels, stride, (UiRect){card.x + 6, card.y + 116, card.width - 12, 20}, fitted_src, 11,
                         available ? (is_tomorrow ? UI_ACCENT : UI_MUTED) : UI_MUTED);

        /* 底部装饰指示条 */
        UiRect pill = {card.x + 14, card.y + card.height - 12, card.width - 28, 4};
        uint32_t pill_col = is_tomorrow ? UI_ACCENT : (is_weekend ? UI_SUCCESS : UI_MUTED);
        fill_round_rect(pixels, stride, pill, 2, pill_col);
    }
}

static void draw_child_task_summary(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect box = to_uirect(ptc_ui_home_summary_rect(false)); /* {48, 120, 896, 496} */
    UiRect top_box = {box.x, box.y, box.width, 260};
    UiRect bottom_box = {box.x, box.y + 274, box.width, 222};

    char remaining[64];
    char today[64];
    char line[160];
    char age[64];
    bool ring_available;
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    bool eye_resting = model->eye_care_policy.enabled && strcmp(model->eye_care_phase, "resting") == 0;
    float fraction = child_remaining_fraction(model, &ring_available);
    uint32_t ring_color = UI_SUCCESS;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    if (bedtime_enforcing || eye_resting || (model->remaining_available && model->remaining_minutes <= 10)) ring_color = UI_DANGER;
    else if (model->remaining_available && model->remaining_minutes <= 30) ring_color = UI_WARNING;

    ptc_ui_format_home_remaining(model, ptc_ui_render_now(), remaining, sizeof(remaining));
    ptc_ui_format_today_mode(model, today, sizeof(today));

    /* --- 上半部：今日调度态势看板 (896x260) --- */
    draw_card_shadow(pixels, stride, top_box, 16);
    fill_round_rect(pixels, stride, top_box, 16, UI_RGB(UI_BLENDED(hero)));

    /* 呼吸脉冲提醒边框 */
    if ((model->remaining_available && model->unrestricted_today != 1 &&
         model->remaining_minutes >= 0 && model->remaining_minutes <= 10) ||
        bedtime_enforcing || eye_resting) {
        int phase = get_breathing_phase();
        draw_rect_outline(pixels, stride, top_box, 16, 2,
                          UI_RGB(ui_mix_rgb(UI_BLENDED(danger), 0xFF9A8A, phase * 4)));
    }

    /* 左列 (宽度 330)：剩余时间、主数值、额度进度槽、状态说明 */
    draw_text(pixels, stride, top_box.x + 28, top_box.y + 36,
              bedtime_enforcing ? ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY_BEDTIME_ACTIVE) :
              (eye_resting ? ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING) : ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY)),
              20, UI_RGB(UI_BLENDED(hero_secondary)));

    int numeric_minutes;
    if (ptc_ui_home_remaining_minutes(model, ptc_ui_render_now(), &numeric_minutes)) {
        snprintf(remaining, sizeof(remaining), "%d", numeric_minutes);
        int number_width = measure_text(remaining, 64);
        draw_text_bold(pixels, stride, top_box.x + 28, top_box.y + 104, remaining, 64,
                       UI_RGB(UI_BLENDED(on_hero)));
        draw_text(pixels, stride, top_box.x + 36 + number_width, top_box.y + 100, ptc_ui_text(PTC_UI_T_MIN), 20,
                  UI_RGB(UI_BLENDED(hero_secondary)));
    } else {
        if (eye_resting) {
            char cycle[128], fitted_cycle[128];
            ptc_ui_format_eye_care_cycle(model, ptc_ui_render_now(), cycle, sizeof(cycle));
            fit_text(fitted_cycle, sizeof(fitted_cycle), cycle, 22, 280);
            draw_text_bold(pixels, stride, top_box.x + 28, top_box.y + 100, fitted_cycle, 22,
                           UI_RGB(UI_BLENDED(on_hero)));
        } else {
            draw_wrapped_text(pixels, stride, top_box.x + 28, top_box.y + 98, remaining, 28,
                              280, 36, 2, UI_RGB(UI_BLENDED(on_hero)));
        }
    }

    /* 今日额度进度槽 (Time Progress Gauge) */
    UiRect slot = {top_box.x + 28, top_box.y + 128, 290, 8};
    fill_round_rect(pixels, stride, slot, 4, UI_GAUGE_SLOT);
    draw_rect_outline(pixels, stride, slot, 4, 1, UI_GAUGE_SLOT_BORDER);
    char gauge_left[64] = "";
    char gauge_right[64] = "";
    if (bedtime_enforcing) {
        snprintf(gauge_left, sizeof(gauge_left), "%s", ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_2));
        snprintf(gauge_right, sizeof(gauge_right), "%s", ptc_ui_text(PTC_UI_T_PLAY_PAUSED_TODAY));
    } else if (eye_resting) {
        snprintf(gauge_left, sizeof(gauge_left), "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING));
        snprintf(gauge_right, sizeof(gauge_right), "%s", ptc_ui_text(PTC_UI_T_REST_AS_PLANNED));
    } else if (model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped)) {
        fill_round_rect(pixels, stride, slot, 4, UI_SUCCESS);
        snprintf(gauge_left, sizeof(gauge_left), "%s", ptc_ui_text(PTC_UI_T_UNLIMITED_TODAY));
        snprintf(gauge_right, sizeof(gauge_right), "%s", ptc_ui_text(PTC_UI_T_REST_AS_PLANNED));
    } else if (model->remaining_available && model->played_minutes_available &&
               model->remaining_minutes >= 0 && model->played_minutes >= 0 &&
               model->remaining_minutes + model->played_minutes > 0) {
        int total = model->remaining_minutes + model->played_minutes;
        int fill_w = (int)((int64_t)slot.width * model->displayed_remaining_minutes / total);
        if (fill_w < 4 && model->remaining_minutes > 0) fill_w = 4;
        if (fill_w > slot.width) fill_w = slot.width;
        if (fill_w > 0) fill_round_rect(pixels, stride, (UiRect){slot.x, slot.y, fill_w, slot.height}, 4, ring_color);
        snprintf(gauge_left, sizeof(gauge_left), ptc_ui_text(PTC_UI_T_PLAYED_D_MIN), model->played_minutes);
        snprintf(gauge_right, sizeof(gauge_right), ptc_ui_text(PTC_UI_T_D_MIN_LEFT), model->remaining_minutes);
    }
    if (gauge_left[0]) {
        draw_text(pixels, stride, slot.x, slot.y + 20, gauge_left, 12, bedtime_enforcing ? UI_DANGER : UI_RGB(UI_BLENDED(hero_secondary)));
        draw_text(pixels, stride, slot.x + slot.width - measure_text(gauge_right, 12), slot.y + 20, gauge_right, 12, bedtime_enforcing ? UI_DANGER : UI_RGB(UI_BLENDED(hero_secondary)));
    }

    if (bedtime_enforcing) {
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_S),
                 model->status_loaded ? ui_rule_source_label(model->rule_source) : (ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM)));
    } else {
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_TODAY_S_S), today,
                 model->status_loaded ? ui_rule_source_label(model->rule_source) : (ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM)));
    }
    draw_text(pixels, stride, top_box.x + 28, top_box.y + 196, line, 14, UI_RGB(UI_BLENDED(hero_secondary)));

    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, top_box.x + 28, top_box.y + (model->dock_available ? 250 : 232), age, 11, UI_RGB(UI_BLENDED(hero_secondary)));

    /* 右列 (x: 374, w: 494)：三周期态势卡片（额度流、护眼周期、就寝计划） */
    const int c_x = top_box.x + 360;
    const int c_w = top_box.width - 388;
    const int c_h = 68;

    /* 周期卡 1：今日额度流 */
    UiRect card1 = {c_x, top_box.y + 20, c_w, c_h};
    fill_round_rect(pixels, stride, card1, 10, UI_RGB(ui_mix_rgb(UI_BLENDED(hero), 0x000000, 24)));
    draw_rect_outline(pixels, stride, card1, 10, 1, UI_RGB(ui_mix_rgb(UI_BLENDED(hero), 0xFFFFFF, 18)));
    draw_text(pixels, stride, card1.x + 14, card1.y + 24, ptc_ui_text(PTC_UI_T_TODAY_QUOTA), 14, UI_RGB(UI_BLENDED(on_hero)));
    char quota_badge[64];
    if (model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped)) snprintf(quota_badge, sizeof(quota_badge), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
    else snprintf(quota_badge, sizeof(quota_badge), "%s", model->status_loaded ? ui_rule_source_label(model->rule_source) : ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM));
    int qb_w = measure_text(quota_badge, 12) + 14;
    UiRect qb_rect = {card1.x + card1.width - qb_w - 12, card1.y + 8, qb_w, 20};
    fill_round_rect(pixels, stride, qb_rect, 4, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, qb_rect, 4, 1, UI_ACCENT);
    draw_text_center(pixels, stride, qb_rect, quota_badge, 12, UI_ACCENT);

    char q_detail[128];
    char q_total[48];
    ptc_ui_format_home_total_value(model, q_total, sizeof(q_total));
    if (!eye_resting && model->played_minutes_available && model->played_minutes >= 0) {
        snprintf(q_detail, sizeof(q_detail), "%s: %s / %s: %d %s",
            ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE), q_total,
            ptc_ui_text(PTC_UI_T_USED_QUOTA_EST), model->played_minutes, ptc_ui_text(PTC_UI_T_MIN));
    } else {
        snprintf(q_detail, sizeof(q_detail), "%s: %s", ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE), q_total);
    }
    draw_text(pixels, stride, card1.x + 14, card1.y + 50, q_detail, 13, UI_RGB(UI_BLENDED(hero_secondary)));

    /* 周期卡 2：护眼休息周期 */
    UiRect card2 = {c_x, top_box.y + 98, c_w, c_h};
    fill_round_rect(pixels, stride, card2, 10, UI_RGB(ui_mix_rgb(UI_BLENDED(hero), 0x000000, 24)));
    draw_rect_outline(pixels, stride, card2, 10, 1, eye_resting ? UI_DANGER : UI_RGB(ui_mix_rgb(UI_BLENDED(hero), 0xFFFFFF, 18)));
    draw_text(pixels, stride, card2.x + 14, card2.y + 24, ptc_ui_text(PTC_UI_T_EYE_CARE), 14, UI_RGB(UI_BLENDED(on_hero)));

    const char *eye_b_label = !model->eye_care_policy.enabled ? ptc_ui_text(PTC_UI_T_DISABLED_2) :
        (eye_resting ? ptc_ui_text(PTC_UI_T_EYE_CARE_BADGE_RESTING) : ptc_ui_text(PTC_UI_T_ENABLED));
    uint32_t eye_b_col = !model->eye_care_policy.enabled ? UI_MUTED : (eye_resting ? UI_DANGER : UI_SUCCESS);
    int eb_w = measure_text(eye_b_label, 12) + 14;
    UiRect eb_rect = {card2.x + card2.width - eb_w - 12, card2.y + 8, eb_w, 20};
    fill_round_rect(pixels, stride, eb_rect, 4, eye_b_col == UI_DANGER ? UI_DANGER_SOFT : (eye_b_col == UI_SUCCESS ? UI_SUCCESS_SOFT : UI_PAGE));
    draw_rect_outline(pixels, stride, eb_rect, 4, 1, eye_b_col);
    draw_text_center(pixels, stride, eb_rect, eye_b_label, 12, eye_b_col);

    char eye_str[128], fitted_eye[128];
    if (model->eye_care_policy.enabled) {
        ptc_ui_format_eye_care_cycle(model, ptc_ui_render_now(), eye_str, sizeof(eye_str));
    } else {
        snprintf(eye_str, sizeof(eye_str), "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_OFF));
    }
    fit_text(fitted_eye, sizeof(fitted_eye), eye_str, 13, card2.width - 28);
    draw_text(pixels, stride, card2.x + 14, card2.y + 50, fitted_eye, 13, eye_resting ? UI_DANGER : UI_RGB(UI_BLENDED(hero_secondary)));

    /* 周期卡 3：就寝计划周期 */
    UiRect card3 = {c_x, top_box.y + 176, c_w, c_h};
    fill_round_rect(pixels, stride, card3, 10, UI_RGB(ui_mix_rgb(UI_BLENDED(hero), 0x000000, 24)));
    draw_rect_outline(pixels, stride, card3, 10, 1, bedtime_enforcing ? UI_DANGER : UI_RGB(ui_mix_rgb(UI_BLENDED(hero), 0xFFFFFF, 18)));
    draw_text(pixels, stride, card3.x + 14, card3.y + 24, ptc_ui_text(PTC_UI_T_BEDTIME_SCHEDULE), 14, UI_RGB(UI_BLENDED(on_hero)));

    bool bedtime_skip_matches = ptc_ui_bedtime_skip_matches_policy(model, &model->bedtime_policy);
    const char *bed_b_label = !model->bedtime_policy.enabled ? ptc_ui_text(PTC_UI_T_DISABLED_2) :
        (bedtime_enforcing ? ptc_ui_text(PTC_UI_T_RESTRICTED) :
         (bedtime_skip_matches ? ptc_ui_text(PTC_UI_T_SKIPPED) : ptc_ui_text(PTC_UI_T_ENABLED)));
    uint32_t bed_b_col = !model->bedtime_policy.enabled ? UI_MUTED :
        (bedtime_enforcing ? UI_DANGER : (bedtime_skip_matches ? UI_SUCCESS : UI_ACCENT));
    int bb_w = measure_text(bed_b_label, 12) + 14;
    UiRect bb_rect = {card3.x + card3.width - bb_w - 12, card3.y + 8, bb_w, 20};
    fill_round_rect(pixels, stride, bb_rect, 4, bed_b_col == UI_DANGER ? UI_DANGER_SOFT : (bed_b_col == UI_SUCCESS ? UI_SUCCESS_SOFT : UI_PAGE));
    draw_rect_outline(pixels, stride, bb_rect, 4, 1, bed_b_col);
    draw_text_center(pixels, stride, bb_rect, bed_b_label, 12, bed_b_col);

    char bed_str[128], fitted_bed[128];
    if (!model->bedtime_policy.enabled) {
        snprintf(bed_str, sizeof(bed_str), "%s", ptc_ui_text(PTC_UI_T_CURRENTLY_OFF));
    } else if (bedtime_enforcing) {
        snprintf(bed_str, sizeof(bed_str), "%s (%02u:%02u ~ %02u:%02u)",
            ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_2),
            (unsigned int)(model->bedtime_start_minute / 60), (unsigned int)(model->bedtime_start_minute % 60),
            (unsigned int)(model->bedtime_end_minute / 60), (unsigned int)(model->bedtime_end_minute % 60));
    } else if (bedtime_skip_matches) {
        snprintf(bed_str, sizeof(bed_str), "%s", ptc_ui_text(PTC_UI_T_THE_BEDTIME_LIMIT_HAS_BEEN_SKIPPED_TONIGHT));
    } else if (model->bedtime_next_available) {
        uint16_t yr; uint8_t mo, da;
        if (ptc_date_from_day_index(model->bedtime_next_start_day_index, &yr, &mo, &da)) {
            snprintf(bed_str, sizeof(bed_str), "%02u/%02u  %02u:%02u ~ %02u:%02u", mo, da,
                (unsigned int)(model->bedtime_next_start_minute / 60), (unsigned int)(model->bedtime_next_start_minute % 60),
                (unsigned int)(model->bedtime_next_end_minute / 60), (unsigned int)(model->bedtime_next_end_minute % 60));
        } else {
            snprintf(bed_str, sizeof(bed_str), "%02u:%02u ~ %02u:%02u",
                (unsigned int)(model->bedtime_next_start_minute / 60), (unsigned int)(model->bedtime_next_start_minute % 60),
                (unsigned int)(model->bedtime_next_end_minute / 60), (unsigned int)(model->bedtime_next_end_minute % 60));
        }
    } else {
        snprintf(bed_str, sizeof(bed_str), "%s", ptc_ui_text(PTC_UI_T_ENABLED_WAITING_FOR_WINDOW));
    }
    fit_text(fitted_bed, sizeof(fitted_bed), bed_str, 13, card3.width - 28);
    draw_text(pixels, stride, card3.x + 14, card3.y + 50, fitted_bed, 13, bedtime_enforcing ? UI_DANGER : UI_RGB(UI_BLENDED(hero_secondary)));

    if (model->dock_available) {
        char dock_text[192];
        ptc_ui_format_dock_usage(model, ptc_ui_render_now(), dock_text, sizeof(dock_text));
        draw_wrapped_text(pixels, stride, top_box.x + 28, top_box.y + 214,
            dock_text, 12, 304, 15, 2, UI_RGB(UI_BLENDED(on_hero)));
    }
    /* --- 下半部：未来本周每天额度周历预报 (896x222) --- */
    draw_child_forecast_columns(pixels, stride, model, bottom_box);
}

static void draw_child_action_icon(uint32_t *pixels, uint32_t stride, PtcUiRect target,
                                   int kind, bool primary, bool disabled)
{
    int cx = target.x + 28;
    int cy = target.y + target.h / 2;
    bool completed = kind == 3;
    uint32_t color = completed ? UI_SUCCESS :
        (disabled ? UI_DISABLED : (primary ? UI_ON_ACCENT : UI_ACCENT));
    uint32_t background = completed ? UI_SUCCESS_SOFT :
        (disabled ? UI_PAGE : (primary ? UI_RGB(ui_mix_rgb(UI_BLENDED(accent), 0xFFFFFF, 28)) : UI_SURFACE));
    fill_round_rect(pixels, stride, (UiRect){cx - 16, cy - 16, 32, 32}, 10, background);
    if (kind == 0) {
        draw_rect_outline(pixels, stride, (UiRect){cx - 9, cy - 7, 18, 14}, 3, 2, color);
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 3; ++column)
                fill_round_rect(pixels, stride, (UiRect){cx - 6 + column * 5, cy - 4 + row * 6, 2, 2}, 1, color);
    } else if (kind == 1) {
        draw_circle_outline(pixels, stride, cx - 2, cy - 1, 8, 2, color);
        draw_line(pixels, stride, cx - 6, cy + 6, cx + 7, cy - 7, 2, color);
        draw_line(pixels, stride, cx - 2, cy + 2, cx - 7, cy - 2, 2, color);
    } else if (kind == 3) {
        draw_circle_outline(pixels, stride, cx, cy, 9, 2, color);
        draw_line(pixels, stride, cx - 4, cy, cx - 1, cy + 3, 2, color);
        draw_line(pixels, stride, cx - 1, cy + 3, cx + 5, cy - 4, 2, color);
    } else {
        draw_child_calendar_icon(pixels, stride, cx, cy, color);
    }
}

static void draw_child_action_button(uint32_t *pixels, uint32_t stride, PtcUiRect target,
                                    const char *label, int kind, bool primary,
                                    bool selected, bool disabled)
{
    UiRect box = to_uirect(target);
    uint32_t fill = disabled ? UI_RAISED : (primary ? UI_ACCENT : UI_ACCENT_SOFT);
    fill_round_rect(pixels, stride, box, 12, fill);
    if (selected) draw_focus_ring(pixels, stride, box, 12);
    draw_child_action_icon(pixels, stride, target, kind, primary, disabled);

    /* Text content area to the right of the action icon */
    UiRect label_box = {box.x + 48, box.y, box.width - 54, box.height};
    draw_button_label(pixels, stride, label_box, label, target.h <= 48 ? 18 : 20,
                      disabled ? UI_DISABLED : (primary ? UI_ON_ACCENT : UI_ACCENT));
}

static void draw_child_status_card(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect card = {964, 486, 268, 130};
    const char *runtime = ptc_ui_runtime_notice_summary(model);
    bool feedback = ptc_ui_operation_feedback_visible(model);
    bool error = strcmp(model->result_status, "error") == 0;
    bool alert = runtime[0] || feedback || ptc_ui_home_notice_expanded(model);
    uint32_t accent = (error || model->disable_flag_present) ? UI_DANGER :
        (model->waiting ? UI_WARNING : (alert ? UI_WARNING : UI_SUCCESS));
    uint32_t background = (error || model->disable_flag_present) ? UI_DANGER_SOFT :
        (model->waiting ? UI_WARNING_SOFT : UI_RGB(UI_BLENDED(surface_raised)));
    char age[64];
    const char *title = alert ? (runtime[0] ? runtime : (error ? ptc_ui_text(PTC_UI_T_OPERATION_INCOMPLETE) :
        (model->waiting ? ptc_ui_text(PTC_UI_T_SYNCHRONIZING) : ptc_ui_text(PTC_UI_T_STATUS_PROMPT)))) : ptc_ui_text(PTC_UI_T_STATUS_AUTOMATIC_SYNCHRONIZATION);
    const char *detail = ptc_ui_text(PTC_UI_T_TODAY_S_SCHEDULE_SYNCS_IN_BACKGROUND);
    if (alert) {
        if (model->message[0] && strcmp(title, model->message) != 0) detail = model->message;
        else if (model->feedback_detail[0]) detail = model->feedback_detail;
        else if (model->waiting) detail = ptc_ui_text(PTC_UI_T_PLEASE_WAIT_AND_CONTINUE_AFTER_COMPLETION);
        else detail = ptc_ui_text(PTC_UI_T_IF_YOU_NEED_HELP_PLEASE_CHECK_SUPPORT);
    }
    if (model->dock_restriction_active && ptc_ui_status_is_fresh(model, ptc_ui_render_now()))
        detail = ptc_ui_text(PTC_UI_T_DOCK_CONNECT);
    fill_round_rect(pixels, stride, card, 12, background);
    draw_rect_outline(pixels, stride, card, 12, 1, alert ? accent : UI_BORDER);
    draw_status_symbol(pixels, stride, card.x + 14, card.y + 18, accent,
                       error ? 3 : (model->waiting ? 2 : 1));
    draw_wrapped_text(pixels, stride, card.x + 40, card.y + 24, title, 14,
                      card.width - 52, 18, 1, accent);
    draw_wrapped_text(pixels, stride, card.x + 14, card.y + 54, detail, 12,
                      card.width - 28, 17, 3, UI_MUTED);
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, card.x + card.width - 14 - measure_text(age, 11),
              card.y + card.height - 10, age, 11, UI_MUTED);
}

void draw_child(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char buffer[128], hint[160], fitted_hint[160];
    bool dock_blocked = ptc_ui_status_is_fresh(model, ptc_ui_render_now()) && model->dock_restriction_active;
    bool disabled = model->disable_flag_present || model->waiting || dock_blocked;
    bool code_unavailable = ptc_ui_status_is_fresh(model, ptc_ui_render_now()) &&
        (model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped));
    draw_header(pixels, stride, ptc_ui_text(PTC_UI_T_SELF_DISCIPLINE_IS_FREEDOM), ptc_ui_text(PTC_UI_T_ARRANGE_TIME_REASONABLY_AND_BE_THE_MASTER));
    draw_time_status_bar(pixels, stride, model);
    draw_child_task_summary(pixels, stride, model);

    /* 右侧动作栏 (4 个紧凑功能项目) */
    draw_child_action_button(pixels, stride, ptc_ui_child_submit_rect(),
        dock_blocked ? ptc_ui_text(PTC_UI_T_DOCK_BLOCKED) : model->disable_flag_present ? ptc_ui_text(PTC_UI_T_REDEMPTION_IS_CURRENTLY_UNAVAILABLE) :
        (code_unavailable ? ptc_ui_text(PTC_UI_T_NO_TIME_LIMIT_TODAY_GRANT_CODES_ARE) : ptc_ui_text(PTC_UI_T_A_ENTER_CODE)),
        0, true, false, disabled || code_unavailable);

    if (model->daily_buffer_available) {
        snprintf(buffer, sizeof(buffer), ptc_ui_text(PTC_UI_T_X_CLAIM_SELF_BUFFER_U_MIN), (unsigned int)model->daily_buffer_minutes);
    } else {
        snprintf(buffer, sizeof(buffer), "%s", model->daily_buffer_claimed ? ptc_ui_text(PTC_UI_T_BUFFER_USED_TODAY) :
            (model->daily_buffer_minutes == 0 ? ptc_ui_text(PTC_UI_T_AUTONOMOUS_BUFFERING_IS_NOT_ENABLED_TODAY) : ptc_ui_text(PTC_UI_T_AUTONOMOUS_BUFFERING_CAN_ONLY_BE_COLLECTED_ON)));
    }

    draw_child_action_button(pixels, stride, ptc_ui_child_buffer_rect(), buffer,
        model->daily_buffer_claimed ? 3 : 1, false, false,
        disabled || !model->daily_buffer_available);

    draw_child_action_button(pixels, stride, ptc_ui_home_details_rect(false),
        ptc_ui_text(PTC_UI_T_USAGE_DETAILS_2), 2, false, false, model->waiting);

    draw_child_status_card(pixels, stride, model);

    /* 底部按钮栏 */
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_footer_rect(0)), ptc_ui_text(PTC_UI_T_A_ENTER_CODE), 18, disabled ? UI_DISABLED : UI_MUTED);
    if (model->show_parent_shortcut_hint && model->custom_shortcut_enabled)
        ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label, hint, sizeof(hint));
    else snprintf(hint, sizeof(hint), ptc_ui_text(PTC_UI_T_STATUS_AUTO_SYNCS_IN_BACKGROUND));
    fit_text(fitted_hint, sizeof(fitted_hint), hint, 18, ptc_ui_child_footer_rect(1).w - 24);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), fitted_hint, 18, UI_RGB(UI_BLENDED(text_secondary)));
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), ptc_ui_text(PTC_UI_T_B_EXIT));
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_refresh_rect()), ptc_ui_text(PTC_UI_T_Y_REFRESH), 18, model->waiting ? UI_DISABLED : UI_MUTED);
}
