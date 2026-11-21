#include "ui_render_internal.h"
#include "../../common/time/ptc_time.h"

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
            draw_text_bold(pixels, stride, tx, card.y + 72, val_str, 24, val_color);
            draw_text(pixels, stride, tx + vw + 4, card.y + 70, unit_str, 13, UI_MUTED);
        } else {
            char fitted[64];
            fit_text(fitted, sizeof(fitted), val_str, 20, card.width - 12);
            draw_text_center(pixels, stride, (UiRect){card.x, card.y + 54, card.width, 30}, fitted, 20, val_color);
        }

        /* 规则来源标签 */
        char fitted_src[64];
        fit_text(fitted_src, sizeof(fitted_src), child_forecast_label(model, i, source), 11, card.width - 12);
        draw_text_center(pixels, stride, (UiRect){card.x + 6, card.y + 96, card.width - 12, 20}, fitted_src, 11,
                         is_tomorrow ? UI_ACCENT : UI_MUTED);

        /* 底部装饰指示条 */
        UiRect pill = {card.x + 14, card.y + card.height - 8, card.width - 28, 4};
        uint32_t pill_col = is_tomorrow ? UI_ACCENT : (is_weekend ? UI_SUCCESS : UI_MUTED);
        fill_round_rect(pixels, stride, pill, 2, pill_col);
    }
}

void ui_render_overview(const PtcUiModel *model, PtcUiOverview *out)
{
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

static void draw_child_task_summary(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    PtcUiOverview overview;
    PtcPolicyStatus policies;
    ui_render_overview(model, &overview);
    ptc_ui_render_policy_status(model, &policies);
    UiRect top = to_uirect(ptc_ui_child_hero_rect());
    UiRect state = {top.x, top.y, 512, top.height};
    UiRect balance = to_uirect(ptc_ui_child_balance_rect());
    fill_round_rect(pixels, stride, state, 16, UI_HERO);
    draw_rect_outline(pixels, stride, state, 16, 1, UI_ACCENT);
    draw_text(pixels, stride, state.x + 24, state.y + 40, ptc_ui_text(PTC_UI_T_OVERVIEW_STATE), 18, UI_HERO_SECONDARY);
    draw_wrapped_text(pixels, stride, state.x + 24, state.y + 90, overview.title, 30, state.width - 48, 38, 2, UI_ON_HERO);
    char fitted[256];
    fit_text(fitted, sizeof(fitted), overview.value, 48, state.width - 48);
    draw_text_bold(pixels, stride, state.x + 24, state.y + 204, fitted, 48, UI_ON_HERO);
    draw_line(pixels, stride, state.x + 24, state.y + 224, state.x + state.width - 24, state.y + 224, 1, UI_ACCENT);
    const char *notice = ptc_ui_runtime_notice_summary(model);
    const char *hint = notice[0] ? notice : ptc_ui_operation_feedback_visible(model) && model->message[0] ? model->message : overview.hint;
    draw_wrapped_text(pixels, stride, state.x + 24, state.y + 252, hint, 16, state.width - 48, 22, 3, UI_HERO_SECONDARY);

    draw_plan_card(pixels, stride, balance, false);
    char source[96], title[96], total[64], used[96];
    snprintf(source, sizeof(source), "%s", overview.fresh ? ui_rule_source_label(model->rule_source) : ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM));
    /* Source stays right anchored; a long translation never covers the title. */
    fit_text(fitted, sizeof(fitted), source, 12, balance.width / 2 - 18);
    int source_w = measure_text(fitted, 12);
    fit_text(title, sizeof(title), ptc_ui_text(PTC_UI_T_OVERVIEW_BALANCE), 17, balance.width - source_w - 42);
    draw_text(pixels, stride, balance.x + 16, balance.y + 34, title, 17, UI_INK);
    draw_text(pixels, stride, balance.x + balance.width - 16 - source_w, balance.y + 34, fitted, 12, UI_ACCENT);
    fit_text(fitted, sizeof(fitted), overview.balance, 30, balance.width / 2 - 16);
    draw_text_bold(pixels, stride, balance.x + 16, balance.y + 90, fitted, 30, UI_ACCENT);
    ptc_ui_format_home_total_value(model, total, sizeof(total));
    if (!overview.fresh) snprintf(total, sizeof(total), "%s", ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED));
    snprintf(used, sizeof(used), "%s", ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED));
    if (overview.fresh && model->played_minutes_available && model->played_minutes >= 0)
        snprintf(used, sizeof(used), ptc_ui_text(model->usage_present ? PTC_UI_T_CONSOLE_USAGE_D_MIN : PTC_UI_T_PLAYED_D_MIN), model->played_minutes);
    const char *values[] = {used, total};
    for (int i = 0; i < 2; ++i) {
        fit_text(fitted, sizeof(fitted), values[i], 12, balance.width / 2 - 20);
        draw_text(pixels, stride, balance.x + balance.width - 16 - measure_text(fitted, 12), balance.y + 69 + i * 20, fitted, 12, UI_MUTED);
    }
    draw_line(pixels, stride, balance.x + 16, balance.y + 114, balance.x + balance.width - 16, balance.y + 114, 1, UI_BORDER);
    char eye[160];
    ptc_ui_format_eye_care_cycle(model, ptc_ui_render_now(), eye, sizeof(eye));
    PtcPolicyDisplayState eye_state = !overview.fresh ? PTC_POLICY_UNKNOWN :
        !model->eye_care_policy.enabled ? PTC_POLICY_OFF :
        strcmp(model->eye_care_phase, "resting") == 0 ? PTC_POLICY_WAITING : PTC_POLICY_AVAILABLE;
    draw_home_policy_row(pixels, stride, to_uirect(ptc_ui_child_guard_rect(0)), ptc_ui_text(PTC_UI_T_EYE_CARE), eye,
        ptc_ui_text(!overview.fresh ? PTC_UI_T_TO_BE_CONFIRMED : !model->eye_care_policy.enabled ? PTC_UI_T_DISABLED_2 : PTC_UI_T_ENABLED), eye_state);
    draw_home_policy_row(pixels, stride, to_uirect(ptc_ui_child_guard_rect(1)), ptc_ui_text(PTC_UI_T_BEDTIME_SCHEDULE), policies.bedtime.detail,
        ptc_ui_text(policies.bedtime.badge), policies.bedtime.state);
    draw_home_policy_row(pixels, stride, to_uirect(ptc_ui_child_guard_rect(2)), ptc_ui_text(PTC_UI_T_DOCK_TITLE), policies.dock.detail,
        ptc_ui_text(policies.dock.badge), policies.dock.state);
    draw_child_forecast_columns(pixels, stride, model, to_uirect(ptc_ui_child_forecast_rect()));
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
    } else if (kind == 4) {
        draw_line(pixels, stride, cx - 9, cy - 6, cx, cy - 10, 2, color);
        draw_line(pixels, stride, cx, cy - 10, cx + 9, cy - 6, 2, color);
        draw_line(pixels, stride, cx - 9, cy - 6, cx - 7, cy + 4, 2, color);
        draw_line(pixels, stride, cx + 9, cy - 6, cx + 7, cy + 4, 2, color);
        draw_line(pixels, stride, cx - 7, cy + 4, cx, cy + 10, 2, color);
        draw_line(pixels, stride, cx + 7, cy + 4, cx, cy + 10, 2, color);
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
                               line_height, 3, disabled ? UI_DISABLED : primary ? UI_ON_ACCENT : UI_MUTED);
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

    for (int i = 0; i < 4; ++i) {
        PtcUiHitKind kind = ptc_ui_child_action_kind(model, i);
        const char *label = ptc_ui_text(PTC_UI_T_USAGE_DETAILS_2);
        const char *detail = ptc_ui_text(PTC_UI_T_USAGE_DETAILS_HINT);
        int icon = 2;
        bool unavailable = model->waiting;
        if (kind == PTC_UI_HIT_CHILD_SUBMIT_CODE) {
            label = code_unavailable ? ptc_ui_text(PTC_UI_T_NO_TIME_LIMIT_TODAY_GRANT_CODES_ARE) : ptc_ui_text(PTC_UI_T_A_ENTER_CODE);
            detail = ptc_ui_text(PTC_UI_T_OVERVIEW_CODE_HINT);
            icon = 0; unavailable = disabled || code_unavailable;
        } else if (kind == PTC_UI_HIT_CHILD_BUFFER) {
            if (model->daily_buffer_available) snprintf(buffer, sizeof(buffer), ptc_ui_text(PTC_UI_T_X_CLAIM_SELF_BUFFER_U_MIN), (unsigned)model->daily_buffer_minutes);
            else snprintf(buffer, sizeof(buffer), "%s", ptc_ui_text(model->daily_buffer_claimed ? PTC_UI_T_BUFFER_USED_TODAY : PTC_UI_T_THERE_ARE_NO_AUTONOMOUS_BUFFERS_TO_CLAIM));
            label = buffer; detail = ptc_ui_text(PTC_UI_T_OVERVIEW_CODE_HINT);
            icon = model->daily_buffer_claimed ? 3 : 1; unavailable = disabled || !model->daily_buffer_available;
        } else if (kind == PTC_UI_HIT_CHILD_PARENT) {
            label = ptc_ui_text(PTC_UI_T_OVERVIEW_PARENT); detail = ptc_ui_text(PTC_UI_T_OVERVIEW_PARENT_HINT);
            icon = 4; unavailable = model->waiting;
        }
        draw_child_action_button(pixels, stride, ptc_ui_child_action_rect(i), label, detail, icon, i == 0,
            model->child_actions_focused && model->child_action_focus == i, unavailable);
    }

    /* 底部按钮栏 */
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_footer_rect(0)), ptc_ui_text(model->child_actions_focused ? PTC_UI_T_OVERVIEW_FOCUS : PTC_UI_T_A_ENTER_CODE), 18, disabled ? UI_DISABLED : UI_MUTED);
    if (model->show_parent_shortcut_hint && model->custom_shortcut_enabled)
        ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label, hint, sizeof(hint));
    else snprintf(hint, sizeof(hint), "%s", ptc_ui_text(PTC_UI_T_STATUS_AUTO_SYNCS_IN_BACKGROUND));
    fit_text(fitted_hint, sizeof(fitted_hint), hint, 18, ptc_ui_child_footer_rect(1).w - 24);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), fitted_hint, 18, UI_RGB(UI_BLENDED(text_secondary)));
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), ptc_ui_text(PTC_UI_T_B_EXIT));
    {
        int64_t now = ptc_ui_render_now();
        draw_refresh_footer_button(pixels, stride, ptc_ui_child_refresh_rect(),
            ptc_ui_text(PTC_UI_T_Y_REFRESH), ptc_ui_status_needs_refresh(model, now),
            ptc_ui_status_refresh_failed(model), false, model->waiting);
    }
}
