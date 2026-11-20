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
    if (model->remaining_available && model->remaining_minutes >= 0 &&
        model->forecast[0].day_index == model->day_index &&
        model->forecast[0].mode == PTC_RULE_MODE_LIMIT && model->forecast[0].minutes > 0) {
        *available = true;
        float ratio = (float)model->displayed_remaining_minutes / (float)model->forecast[0].minutes;
        return ratio < 0.0f ? 0.0f : ratio > 1.0f ? 1.0f : ratio;
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

static const char *child_forecast_label(const PtcUiModel *model, int offset, PtcUiChildForecastSource source)
{
    if (source == PTC_UI_CHILD_FORECAST_WEEKLY) return ptc_ui_text(PTC_UI_T_WEEKLY_BASELINE);
    return ui_rule_source_label(child_forecast_unlimited_today(model, offset) ?
        model->rule_source : model->forecast[offset].rule_source);
}

static void draw_child_forecast_columns(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, UiRect container)
{
    fill_round_rect(pixels, stride, container, 16, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, container, 16, 1, UI_BORDER);

    /* 顶部标题栏 */
    draw_child_calendar_icon(pixels, stride, container.x + 28, container.y + 20, UI_ACCENT);
    int title_x = container.x + 46;
    int heading_width = container.width - 62;
    char title[192], summary[128], detail[128];
    int64_t now = ptc_ui_render_now();
    int focused_offset = model->child_forecast_focus - 1;
    PtcUiChildForecastSource focused_source = ptc_ui_child_forecast_source(model, focused_offset, now);
    const char *summary_text = ptc_ui_text(PTC_UI_T_REST_AS_PLANNED);
    if (focused_source != PTC_UI_CHILD_FORECAST_UNKNOWN) {
        PtcDayRule rule = child_forecast_rule(model, focused_offset, focused_source);
        char date[32], quota[40];
        ptc_format_date(model->day_index + focused_offset, date);
        if (rule.mode == PTC_RULE_MODE_LIMIT)
            snprintf(quota, sizeof(quota), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)rule.minutes);
        else
            snprintf(quota, sizeof(quota), "%s", ptc_ui_text(rule.mode == PTC_RULE_MODE_UNLIMITED ?
                PTC_UI_T_ADJUST_BADGE_UNLIMITED : PTC_UI_T_DISABLED));
        snprintf(detail, sizeof(detail), "%s  |  %s  |  %s", date, quota,
            child_forecast_label(model, focused_offset, focused_source));
        summary_text = detail;
    }
    fit_text(summary, sizeof(summary), summary_text, 13, heading_width / 2);
    int summary_width = measure_text(summary, 13);
    int summary_x = container.x + container.width - 16 - summary_width;
    fit_text(title, sizeof(title), ptc_ui_text(PTC_UI_T_7_DAY_PLAN_BEDTIME_FORECAST), 16,
             summary_x - title_x - 16);
    draw_text(pixels, stride, title_x, container.y + 26, title, 16, UI_RGB(UI_BLENDED(text_primary)));
    draw_text(pixels, stride, summary_x, container.y + 26, summary, 13, UI_MUTED);

    for (int i = 0; i < (int)PTC_RESULT_FORECAST_DAYS; ++i) {
        UiRect card = to_uirect(ptc_ui_child_forecast_day_rect(i, 1));
        uint16_t target_day = model->day_index + (uint16_t)i;
        PtcUiChildForecastSource source = ptc_ui_child_forecast_source(model, i, now);
        if (source == PTC_UI_CHILD_FORECAST_UNKNOWN) {
            int end = i + 1;
            while (end < (int)PTC_RESULT_FORECAST_DAYS &&
                ptc_ui_child_forecast_source(model, end, now) == PTC_UI_CHILD_FORECAST_UNKNOWN) ++end;
            card = to_uirect(ptc_ui_child_forecast_day_rect(i, end - i));
            draw_plan_card(pixels, stride, card, false);
            char fitted[160];
            fit_text(fitted, sizeof(fitted), ptc_ui_text(end - i == 1 ? PTC_UI_T_TO_BE_CONFIRMED :
                PTC_UI_T_REFRESH_STATUS_TO_VIEW_FORECAST_DATA), 16, card.width - 24);
            draw_text_center(pixels, stride, card, fitted, 16, UI_MUTED);
            i = end - 1;
            continue;
        }
        uint16_t yr = 0;
        uint8_t mo = 0, da = 0;
        bool date_ok = ptc_date_from_day_index(target_day, &yr, &mo, &da);
        uint8_t w = ptc_weekday_from_day_index(target_day);
        bool is_weekend = (w == 0 || w == 6);
        bool is_tomorrow = (i == 1);
        bool is_today = (i == 0);

        bool focused = focused_offset == i;
        uint32_t card_bg = focused ? UI_ACCENT_SOFT :
            (is_weekend ? UI_SUCCESS_SOFT : UI_RGB(UI_BLENDED(surface_raised)));
        uint32_t border_col = is_today && focused_offset < 0 ? UI_ACCENT :
            (is_weekend ? UI_SUCCESS_BORDER : UI_BORDER);
        fill_round_rect(pixels, stride, card, 10, card_bg);
        if (focused) draw_focus_ring(pixels, stride, card, 10);
        else draw_rect_outline(pixels, stride, card, 10, is_today && focused_offset < 0 ? 2 : 1, border_col);

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
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 10, card.width, 20}, day_title, 14, title_color);

        /* 日期 MM/DD */
        char date_str[32];
        if (date_ok) snprintf(date_str, sizeof(date_str), "%02u/%02u", mo, da);
        else snprintf(date_str, sizeof(date_str), "--/--");
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 30, card.width, 18}, date_str, 12, UI_MUTED);

        /* 额度大数值 */
        char val_str[32];
        char unit_str[16] = "";
        uint32_t val_color = UI_INK;
        PtcDayRule rule = child_forecast_rule(model, i, source);
        if (rule.mode == PTC_RULE_MODE_UNLIMITED) {
            snprintf(val_str, sizeof(val_str), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
            val_color = UI_SUCCESS;
        } else if (rule.mode == PTC_RULE_MODE_LIMIT) {
            snprintf(val_str, sizeof(val_str), "%u", (unsigned)rule.minutes);
            snprintf(unit_str, sizeof(unit_str), "%s", ptc_ui_text(PTC_UI_T_MIN));
        } else {
            snprintf(val_str, sizeof(val_str), "%s", ptc_ui_text(PTC_UI_T_DISABLED));
            val_color = UI_MUTED;
        }

        if (unit_str[0]) {
            int vw = measure_text(val_str, 24);
            int uw = measure_text(unit_str, 13);
            int tx = card.x + (card.width - (vw + uw + 4)) / 2;
            draw_text_bold(pixels, stride, tx, card.y + 82, val_str, 24, val_color);
            draw_text(pixels, stride, tx + vw + 4, card.y + 80, unit_str, 13, UI_MUTED);
        } else {
            char fitted[64];
            fit_text(fitted, sizeof(fitted), val_str, 20, card.width - 12);
            draw_text_center(pixels, stride, (UiRect){card.x, card.y + 64, card.width, 30}, fitted, 20, val_color);
        }

        /* 规则来源标签 */
        char fitted_src[64];
        fit_text(fitted_src, sizeof(fitted_src), child_forecast_label(model, i, source), 11, card.width - 12);
        draw_text_center(pixels, stride, (UiRect){card.x + 6, card.y + 114, card.width - 12, 20}, fitted_src, 11,
                         is_tomorrow ? UI_ACCENT : UI_MUTED);

        /* 底部装饰指示条 */
        UiRect pill = {card.x + 14, card.y + card.height - 8, card.width - 28, 4};
        uint32_t pill_col = is_tomorrow ? UI_ACCENT : (is_weekend ? UI_SUCCESS : UI_MUTED);
        fill_round_rect(pixels, stride, pill, 2, pill_col);
    }
}

static void draw_child_task_summary(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect top_box = to_uirect(ptc_ui_child_hero_rect());
    UiRect bottom_box = to_uirect(ptc_ui_child_forecast_rect());

    char remaining[64];
    char today[64];
    char line[160];
    bool ring_available;
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    bool eye_resting = model->eye_care_policy.enabled && strcmp(model->eye_care_phase, "resting") == 0;
    float fraction = child_remaining_fraction(model, &ring_available);
    uint32_t ring_color = UI_SUCCESS;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    if (bedtime_enforcing) ring_color = UI_NIGHT;
    else if (eye_resting) ring_color = UI_WARNING;
    else if (model->remaining_available && model->remaining_minutes <= 10) ring_color = UI_DANGER;
    else if (model->remaining_available && model->remaining_minutes <= 30) ring_color = UI_WARNING;

    ptc_ui_format_home_remaining(model, ptc_ui_render_now(), remaining, sizeof(remaining));
    ptc_ui_format_today_mode(model, today, sizeof(today));

    /* --- 上半部：Bento 清透分舱今日调度态势看板 (896x260) --- */
    draw_plan_card(pixels, stride, top_box, false);
    UiRect quota_box = {top_box.x + 12, top_box.y + 12, 336, top_box.height - 24};
    bool is_light_theme = (g_theme.resolved == PTC_UI_RESOLVED_LIGHT);
    uint32_t pane_bg_rgb = is_light_theme ? UI_BLENDED(accent) : UI_BLENDED(hero);
    uint32_t pane_fg_rgb = is_light_theme ? UI_BLENDED(on_accent) : UI_BLENDED(on_hero);
    uint32_t pane_sub_rgb = is_light_theme
        ? ui_mix_rgb(pane_bg_rgb, pane_fg_rgb, 78)
        : UI_BLENDED(hero_secondary);
    uint32_t pane_pill_rgb = ui_mix_rgb(pane_bg_rgb, pane_fg_rgb, 18);
    uint32_t pane_track_rgb = ui_mix_rgb(pane_bg_rgb, pane_fg_rgb, 24);
    fill_round_rect(pixels, stride, quota_box, 12, UI_RGB(pane_bg_rgb));

    /* 呼吸脉冲提醒边框 */
    if ((model->remaining_available && model->unrestricted_today != 1 &&
         model->remaining_minutes >= 0 && model->remaining_minutes <= 10) ||
        bedtime_enforcing || eye_resting) {
        int phase = get_breathing_phase();
        uint32_t breath_c = bedtime_enforcing ? UI_BLENDED(night) : (eye_resting ? UI_BLENDED(warning) : UI_BLENDED(danger));
        draw_rect_outline(pixels, stride, quota_box, 12, 2,
                          UI_RGB(ui_mix_rgb(breath_c, 0xFFFFFF, phase * 3)));
    }

    /* 左列顶部：标题 + 右上规则与额度胶囊 */
    const char *quota_heading = bedtime_enforcing ? ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY_BEDTIME_ACTIVE) :
        (eye_resting ? ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING) : ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY));
    char rule_chip[64], fitted_chip[64], total_quota[48];
    ptc_ui_format_home_total_value(model, total_quota, sizeof(total_quota));
    if (model->status_loaded && ptc_ui_status_is_fresh(model, ptc_ui_render_now())) {
        snprintf(rule_chip, sizeof(rule_chip), "%s / %s",
            ui_rule_source_label(model->rule_source), total_quota);
    } else {
        snprintf(rule_chip, sizeof(rule_chip), "%s", ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM));
    }
    int heading_w = measure_text(quota_heading, 18);
    int max_chip_w = quota_box.width - 36 - heading_w - 10;
    if (max_chip_w < 72) max_chip_w = 72;
    fit_text(fitted_chip, sizeof(fitted_chip), rule_chip, 12, max_chip_w - 16);
    int chip_w = measure_text(fitted_chip, 12) + 16;
    if (chip_w > max_chip_w) chip_w = max_chip_w;
    UiRect chip_rect = {quota_box.x + quota_box.width - 16 - chip_w, quota_box.y + 14, chip_w, 22};
    fill_round_rect(pixels, stride, chip_rect, 6, UI_RGB(pane_pill_rgb));
    draw_text_center(pixels, stride, chip_rect, fitted_chip, 12, UI_RGB(pane_fg_rgb));

    char fitted_heading[96];
    fit_text(fitted_heading, sizeof(fitted_heading), quota_heading, 18, chip_rect.x - (quota_box.x + 18) - 8);
    draw_text(pixels, stride, quota_box.x + 18, quota_box.y + 32, fitted_heading, 18, UI_RGB(pane_sub_rgb));

    int numeric_minutes;
    if (ptc_ui_home_remaining_minutes(model, ptc_ui_render_now(), &numeric_minutes)) {
        snprintf(remaining, sizeof(remaining), "%d", numeric_minutes);
        int number_width = measure_text(remaining, 64);
        draw_text_bold(pixels, stride, quota_box.x + 18, quota_box.y + 112, remaining, 64,
                       UI_RGB(pane_fg_rgb));
        draw_text(pixels, stride, quota_box.x + 26 + number_width, quota_box.y + 108, ptc_ui_text(PTC_UI_T_MIN), 20,
                  UI_RGB(pane_sub_rgb));
    } else {
        if (eye_resting) {
            char cycle[128], fitted_cycle[128];
            ptc_ui_format_eye_care_cycle(model, ptc_ui_render_now(), cycle, sizeof(cycle));
            fit_text(fitted_cycle, sizeof(fitted_cycle), cycle, 22, quota_box.width - 36);
            draw_text_bold(pixels, stride, quota_box.x + 18, quota_box.y + 108, fitted_cycle, 22,
                           UI_RGB(pane_fg_rgb));
        } else {
            draw_wrapped_text(pixels, stride, quota_box.x + 18, quota_box.y + 106, remaining, 28,
                              quota_box.width - 36, 36, 2, UI_RGB(pane_fg_rgb));
        }
    }

    /* 今日额度进度槽 (Time Progress Gauge)
     * “已玩/还剩”是孩子最先要读懂的两个数字，因此加高进度槽、把标签提到 16px，
     * 并让右侧剩余时间加粗。 */
    const int gauge_label_size = 16;
    UiRect slot = {quota_box.x + 18, quota_box.y + 138, quota_box.width - 36, 10};
    fill_round_rect(pixels, stride, slot, 5, UI_RGB(pane_track_rgb));
    uint32_t bar_fill = (is_light_theme && ring_color == UI_SUCCESS) ? UI_RGB(pane_fg_rgb) : ring_color;
    char gauge_left[64] = "";
    char gauge_right[64] = "";
    if (!ptc_ui_status_is_fresh(model, ptc_ui_render_now())) {
        snprintf(gauge_left, sizeof(gauge_left), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
    } else if (bedtime_enforcing) {
        snprintf(gauge_left, sizeof(gauge_left), "%s", ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_2));
        snprintf(gauge_right, sizeof(gauge_right), "%s", ptc_ui_text(PTC_UI_T_PLAY_PAUSED_TODAY));
    } else if (eye_resting) {
        snprintf(gauge_left, sizeof(gauge_left), "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING));
        snprintf(gauge_right, sizeof(gauge_right), "%s", ptc_ui_text(PTC_UI_T_REST_AS_PLANNED));
    } else if (model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped)) {
        fill_round_rect(pixels, stride, slot, 5, bar_fill);
        snprintf(gauge_left, sizeof(gauge_left), "%s", ptc_ui_text(PTC_UI_T_UNLIMITED_TODAY));
        snprintf(gauge_right, sizeof(gauge_right), "%s", ptc_ui_text(PTC_UI_T_REST_AS_PLANNED));
    } else if (model->remaining_available && model->remaining_minutes >= 0 &&
               model->forecast[0].day_index == model->day_index &&
               model->forecast[0].mode == PTC_RULE_MODE_LIMIT && model->forecast[0].minutes > 0) {
        int total = model->forecast[0].minutes;
        int fill_w = (int)((int64_t)slot.width * model->displayed_remaining_minutes / total);
        if (fill_w < 5 && model->remaining_minutes > 0) fill_w = 5;
        if (fill_w > slot.width) fill_w = slot.width;
        if (fill_w > 0) fill_round_rect(pixels, stride, (UiRect){slot.x, slot.y, fill_w, slot.height}, 5, bar_fill);
        if (model->played_minutes_available && model->played_minutes >= 0)
            snprintf(gauge_left, sizeof(gauge_left),
                ptc_ui_text(model->usage_present ? PTC_UI_T_CONSOLE_USAGE_D_MIN : PTC_UI_T_PLAYED_D_MIN), model->played_minutes);
        snprintf(gauge_right, sizeof(gauge_right), ptc_ui_text(PTC_UI_T_D_MIN_LEFT), model->remaining_minutes);
    }
    if (gauge_left[0]) {
        uint32_t gauge_color = UI_RGB(pane_sub_rgb);
        char fitted_left[64];
        /* 剩余分钟是这一屏最该被读到的数字：右侧加粗，并在空间不足时先裁剪左侧「已玩」标签。 */
        int right_width = measure_text(gauge_right, gauge_label_size);
        fit_text(fitted_left, sizeof(fitted_left), gauge_left, gauge_label_size, slot.width - right_width - 8);
        draw_text(pixels, stride, slot.x, slot.y + 28, fitted_left, gauge_label_size, gauge_color);
        draw_text_bold(pixels, stride, slot.x + slot.width - right_width, slot.y + 28,
                       gauge_right, gauge_label_size, UI_RGB(pane_fg_rgb));
    }

    if (bedtime_enforcing) {
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_S),
                 model->status_loaded ? ui_rule_source_label(model->rule_source) : (ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM)));
    } else {
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_TODAY_S_S), today,
                 model->status_loaded ? ui_rule_source_label(model->rule_source) : (ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM)));
    }
    draw_wrapped_text(pixels, stride, quota_box.x + 18, quota_box.y + 206, line,
        13, quota_box.width - 36, 18, 2, UI_RGB(pane_sub_rgb));

    /* 右列 (x: 408, w: 508)：三大守护策略卡片（护眼周期、就寝计划、电视限制）
     * 使用清透次级底色 UI_PAGE 嵌于 UI_SURFACE 主容器内，形成明确 Bento 分层。 */
    const int c_x = top_box.x + 362;
    const int c_w = top_box.width - 376;
    const int c_h = 70;
    const int dock_card_h = 92;
    const int card_title_size = 16;
    const int card_detail_size = 15;
    const int card_badge_size = 13;

    /* 守护卡 1：护眼休息周期 */
    UiRect card1 = {c_x, top_box.y + 12, c_w, c_h};
    fill_round_rect(pixels, stride, card1, 10, eye_resting ? UI_DANGER_SOFT : UI_PAGE);
    draw_rect_outline(pixels, stride, card1, 10, 1, eye_resting ? UI_DANGER : UI_BORDER);
    draw_text(pixels, stride, card1.x + 16, card1.y + 25, ptc_ui_text(PTC_UI_T_EYE_CARE), card_title_size, UI_INK);

    const char *eye_b_label = !model->eye_care_policy.enabled ? ptc_ui_text(PTC_UI_T_DISABLED_2) :
        (eye_resting ? ptc_ui_text(PTC_UI_T_EYE_CARE_BADGE_RESTING) : ptc_ui_text(PTC_UI_T_ENABLED));
    uint32_t eye_b_col = !model->eye_care_policy.enabled ? UI_MUTED : (eye_resting ? UI_DANGER : UI_SUCCESS);
    int eb_w = measure_text(eye_b_label, card_badge_size) + 16;
    UiRect eb_rect = {card1.x + card1.width - eb_w - 14, card1.y + 8, eb_w, 22};
    uint32_t eb_bd = eye_b_col == UI_DANGER ? UI_DANGER_BORDER : (eye_b_col == UI_SUCCESS ? UI_SUCCESS_BORDER : UI_BORDER);
    fill_round_rect(pixels, stride, eb_rect, 5, eye_b_col == UI_DANGER ? UI_DANGER_SOFT : (eye_b_col == UI_SUCCESS ? UI_SUCCESS_SOFT : UI_RAISED));
    draw_rect_outline(pixels, stride, eb_rect, 5, 1, eb_bd);
    draw_text_center(pixels, stride, eb_rect, eye_b_label, card_badge_size, eye_b_col);

    char eye_str[128], fitted_eye[128];
    if (model->eye_care_policy.enabled) {
        ptc_ui_format_eye_care_cycle(model, ptc_ui_render_now(), eye_str, sizeof(eye_str));
    } else {
        snprintf(eye_str, sizeof(eye_str), "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_OFF));
    }
    fit_text(fitted_eye, sizeof(fitted_eye), eye_str, card_detail_size, card1.width - 32);
    draw_text(pixels, stride, card1.x + 16, card1.y + 56, fitted_eye, card_detail_size,
              eye_resting ? UI_DANGER : UI_MUTED);

    PtcPolicyStatus policies;
    ptc_ui_render_policy_status(model, &policies);

    /* 守护卡 2：就寝计划周期 */
    const PtcPolicyDisplay *bed_policy = &policies.bedtime;
    UiRect card2 = {c_x, card1.y + c_h + 7, c_w, c_h};
    uint32_t bed_fg = bed_policy->state == PTC_POLICY_ACTIVE ? UI_NIGHT : policy_display_color(bed_policy->state);
    uint32_t bed_bg = bed_policy->state == PTC_POLICY_ACTIVE ? UI_NIGHT_SOFT :
        bed_policy->state == PTC_POLICY_PENDING || bed_policy->state == PTC_POLICY_WAITING ||
        bed_policy->state == PTC_POLICY_EXHAUSTED ? UI_WARNING_SOFT :
        (bed_policy->state == PTC_POLICY_OFF ? UI_RAISED : UI_ACCENT_SOFT);
    uint32_t bed_bd = bed_policy->state == PTC_POLICY_ACTIVE ? UI_NIGHT_BORDER :
        bed_policy->state == PTC_POLICY_PENDING || bed_policy->state == PTC_POLICY_WAITING ||
        bed_policy->state == PTC_POLICY_EXHAUSTED ? UI_WARNING_BORDER : UI_BORDER;
    fill_round_rect(pixels, stride, card2, 10, bed_policy->state == PTC_POLICY_ACTIVE ? UI_NIGHT_SOFT : UI_PAGE);
    draw_rect_outline(pixels, stride, card2, 10, 1, bed_policy->state == PTC_POLICY_ACTIVE ? bed_fg : UI_BORDER);
    draw_text(pixels, stride, card2.x + 16, card2.y + 25, ptc_ui_text(PTC_UI_T_BEDTIME_SCHEDULE), card_title_size, UI_INK);

    const char *bed_badge = ptc_ui_text(bed_policy->badge);
    int bed_badge_w = measure_text(bed_badge, card_badge_size) + 16;
    UiRect bed_pill = {card2.x + card2.width - bed_badge_w - 14, card2.y + 8, bed_badge_w, 22};
    fill_round_rect(pixels, stride, bed_pill, 5, bed_bg);
    draw_rect_outline(pixels, stride, bed_pill, 5, 1, bed_bd);
    draw_text_center(pixels, stride, bed_pill, bed_badge, card_badge_size, bed_fg);

    char fitted_bed[256];
    fit_text(fitted_bed, sizeof(fitted_bed), bed_policy->detail, card_detail_size, card2.width - 32);
    draw_text(pixels, stride, card2.x + 16, card2.y + 56, fitted_bed, card_detail_size,
        bed_policy->state == PTC_POLICY_ACTIVE ? UI_NIGHT : UI_MUTED);

    /* 守护卡 3：电视/底座限制（更高，末行单独承载规则摘要说明） */
    const PtcPolicyDisplay *dock_policy = &policies.dock;
    UiRect card3 = {c_x, card2.y + c_h + 7, c_w, dock_card_h};
    uint32_t dock_fg = policy_display_color(dock_policy->state);
    uint32_t dock_bg = dock_policy->state == PTC_POLICY_ACTIVE ? UI_DANGER_SOFT :
        dock_policy->state == PTC_POLICY_PENDING || dock_policy->state == PTC_POLICY_WAITING ||
        dock_policy->state == PTC_POLICY_EXHAUSTED ? UI_WARNING_SOFT :
        (dock_policy->state == PTC_POLICY_OFF ? UI_RAISED : UI_ACCENT_SOFT);
    uint32_t dock_bd = dock_policy->state == PTC_POLICY_ACTIVE ? UI_DANGER_BORDER :
        dock_policy->state == PTC_POLICY_PENDING || dock_policy->state == PTC_POLICY_WAITING ||
        dock_policy->state == PTC_POLICY_EXHAUSTED ? UI_WARNING_BORDER : UI_BORDER;
    fill_round_rect(pixels, stride, card3, 10, dock_policy->state == PTC_POLICY_ACTIVE ? UI_DANGER_SOFT : UI_PAGE);
    draw_rect_outline(pixels, stride, card3, 10, 1, dock_policy->state == PTC_POLICY_ACTIVE ? dock_fg : UI_BORDER);

    const char *dock_title = ptc_ui_text(PTC_UI_T_DOCK_TITLE);
    draw_text(pixels, stride, card3.x + 16, card3.y + 25, dock_title, card_title_size, UI_INK);

    int mode_x = card3.x + 16 + measure_text(dock_title, card_title_size) + 10;
    char mode[96];
    const char *dock_badge = ptc_ui_text(dock_policy->badge);
    int dock_badge_w = measure_text(dock_badge, card_badge_size) + 16;
    UiRect dock_pill = {card3.x + card3.width - dock_badge_w - 14, card3.y + 8, dock_badge_w, 22};
    fill_round_rect(pixels, stride, dock_pill, 5, dock_bg);
    draw_rect_outline(pixels, stride, dock_pill, 5, 1, dock_bd);
    draw_text_center(pixels, stride, dock_pill, dock_badge, card_badge_size, dock_fg);

    fit_text(mode, sizeof(mode), ptc_ui_text(policies.mode), 13, dock_pill.x - mode_x - 8);
    draw_text(pixels, stride, mode_x, card3.y + 25, mode, 13, UI_MUTED);

    /* 电视规则是孩子最常看的一行，说明与规则摘要都使用和护眼、就寝一致的 15px。 */
    char fitted_dock[256];
    fit_text(fitted_dock, sizeof(fitted_dock), dock_policy->detail, card_detail_size, card3.width - 32);
    draw_text(pixels, stride, card3.x + 16, card3.y + 55, fitted_dock, card_detail_size,
        dock_policy->state == PTC_POLICY_ACTIVE ? UI_DANGER : UI_INK);

    fit_text(fitted_dock, sizeof(fitted_dock), policies.dock_rule, card_detail_size, card3.width - 32);
    draw_text(pixels, stride, card3.x + 16, card3.y + 79, fitted_dock, card_detail_size,
              UI_MUTED);
    /* --- 下半部：未来本周每天额度周历预报 --- */
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
                                    const char *label, const char *hint, int kind, bool primary,
                                    bool selected, bool disabled)
{
    UiRect box = to_uirect(target);
    uint32_t fill = disabled ? UI_RAISED : (primary ? UI_ACCENT : UI_ACCENT_SOFT);
    fill_round_rect(pixels, stride, box, 12, fill);
    if (selected) draw_focus_ring(pixels, stride, box, 12);
    draw_child_action_icon(pixels, stride, target, kind, primary, disabled);

    /* Text content area to the right of the action icon */
    UiRect label_box = {box.x + 48, box.y, box.width - 54, box.height};
    uint32_t title_color = disabled ? UI_DISABLED : (primary ? UI_ON_ACCENT : UI_ACCENT);
    if (!hint || !hint[0]) {
        draw_button_label(pixels, stride, label_box, label, target.h <= 48 ? 18 : 20, title_color);
        return;
    }
    /* “使用详情”用一行标题加说明：让孩子在点进去之前就知道里面能看什么。
     * 行数按当前语言实测，整块垂直居中，避免中文两行、英文三行时上重下空。 */
    const int hint_size = 13;
    const int line_height = 17;
    int hint_width = label_box.width - 6;
    int lines = count_wrapped_lines(hint, hint_size, hint_width, 3);
    if (lines < 1) lines = 1;
    int block = 20 + 12 + (lines - 1) * line_height + 8;
    int title_baseline = label_box.y + (label_box.height - block) / 2 + 20;
    draw_text_center(pixels, stride, (UiRect){label_box.x, title_baseline - 20, label_box.width, 26},
                     label, 20, title_color);
    draw_wrapped_text_centered(pixels, stride, label_box.x + label_box.width / 2,
                               title_baseline + 24, hint, hint_size, hint_width,
                               line_height, 3, disabled ? UI_DISABLED : UI_MUTED);
}

static void draw_child_status_card(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect card = to_uirect(ptc_ui_child_status_rect());
    const char *runtime = ptc_ui_runtime_notice_summary(model);
    bool feedback = ptc_ui_operation_feedback_visible(model);
    bool error = strcmp(model->result_status, "error") == 0;
    bool alert = runtime[0] || feedback || ptc_ui_home_notice_expanded(model);
    uint32_t accent = (error || model->disable_flag_present) ? UI_DANGER :
        (model->waiting ? UI_WARNING : (alert ? UI_WARNING : UI_SUCCESS));
    uint32_t background = (error || model->disable_flag_present) ? UI_DANGER_SOFT :
        (model->waiting ? UI_WARNING_SOFT : UI_RGB(UI_BLENDED(surface_raised)));
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
    draw_language_switcher_badge(pixels, stride, to_uirect(ptc_ui_header_language_rect()), false);
    draw_child_task_summary(pixels, stride, model);

    /* 右侧动作栏 (4 个紧凑功能项目) */
    draw_child_action_button(pixels, stride, ptc_ui_child_submit_rect(),
        dock_blocked ? ptc_ui_text(PTC_UI_T_DOCK_BLOCKED) : model->disable_flag_present ? ptc_ui_text(PTC_UI_T_REDEMPTION_IS_CURRENTLY_UNAVAILABLE) :
        (code_unavailable ? ptc_ui_text(PTC_UI_T_NO_TIME_LIMIT_TODAY_GRANT_CODES_ARE) : ptc_ui_text(PTC_UI_T_A_ENTER_CODE)),
        NULL, 0, true, false, disabled || code_unavailable);

    if (model->daily_buffer_available) {
        snprintf(buffer, sizeof(buffer), ptc_ui_text(PTC_UI_T_X_CLAIM_SELF_BUFFER_U_MIN), (unsigned int)model->daily_buffer_minutes);
    } else {
        snprintf(buffer, sizeof(buffer), "%s", model->daily_buffer_claimed ? ptc_ui_text(PTC_UI_T_BUFFER_USED_TODAY) :
            (model->daily_buffer_minutes == 0 ? ptc_ui_text(PTC_UI_T_AUTONOMOUS_BUFFERING_IS_NOT_ENABLED_TODAY) : ptc_ui_text(PTC_UI_T_AUTONOMOUS_BUFFERING_CAN_ONLY_BE_COLLECTED_ON)));
    }

    draw_child_action_button(pixels, stride, ptc_ui_child_buffer_rect(), buffer,
        NULL, model->daily_buffer_claimed ? 3 : 1, false, false,
        disabled || !model->daily_buffer_available);

    draw_child_action_button(pixels, stride, ptc_ui_home_details_rect(false),
        ptc_ui_text(PTC_UI_T_USAGE_DETAILS_2), ptc_ui_text(PTC_UI_T_USAGE_DETAILS_HINT),
        2, false, false, model->waiting);

    draw_child_status_card(pixels, stride, model);

    /* 底部按钮栏 */
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_footer_rect(0)), ptc_ui_text(PTC_UI_T_A_ENTER_CODE), 18, disabled ? UI_DISABLED : UI_MUTED);
    if (model->show_parent_shortcut_hint && model->custom_shortcut_enabled)
        ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label, hint, sizeof(hint));
    else snprintf(hint, sizeof(hint), "%s", ptc_ui_text(PTC_UI_T_STATUS_AUTO_SYNCS_IN_BACKGROUND));
    fit_text(fitted_hint, sizeof(fitted_hint), hint, 18, ptc_ui_child_footer_rect(1).w - 24);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), fitted_hint, 18, UI_RGB(UI_BLENDED(text_secondary)));
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), ptc_ui_text(PTC_UI_T_B_EXIT));
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_refresh_rect()), ptc_ui_text(PTC_UI_T_Y_REFRESH), 18, model->waiting ? UI_DISABLED : UI_MUTED);
}
