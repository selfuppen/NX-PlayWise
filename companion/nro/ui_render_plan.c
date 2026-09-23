#include "ui_render_internal.h"

void draw_plan_impact(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                      PtcUiPlanKind kind, bool dirty, UiRect panel)
{
    char impact[256], age[80], title[64], left_value[40], right_value[48];
    char left_note[64], right_note[64], source_text[80];
    PtcUiPlanImpactProjection projection;
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    int played = fresh && model->played_minutes_available ? model->played_minutes : -1;
    bool requires_hold = dirty && ptc_ui_plan_save_requires_hold(model, kind, ptc_ui_render_now());
    bool error = strcmp(model->result_status, "error") == 0;
    const char *state_message = NULL;
    uint32_t state_color = UI_MUTED;
    uint32_t state_background = UI_RGB(UI_BLENDED(surface_raised));
    int changed = 0;
    ptc_ui_project_plan_impact(model, kind, dirty, ptc_ui_render_now(), &projection);

    if (kind == PTC_UI_PLAN_WEEKLY) {
        for (int d = 0; d < 7; ++d)
            if (ptc_ui_day_rule_effectively_changed(model->current_week[d], model->draft_week[d])) ++changed;
    } else if (kind == PTC_UI_PLAN_HOLIDAY) {
        if (model->holiday_enabled != model->draft_holiday_enabled) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->holiday_rule, model->draft_holiday_rule)) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->makeup_workday_rule, model->draft_makeup_workday_rule)) ++changed;
    }

    draw_plan_card(pixels, stride, panel, false);
    if (dirty) {
        if (changed > 0)
            snprintf(title, sizeof(title), kind == PTC_UI_PLAN_WEEKLY ? "修改草稿 | 已调整 %d 天" : "修改草稿 | 已调整 %d 项", changed);
        else snprintf(title, sizeof(title), "修改草稿 | 待保存");
    } else snprintf(title, sizeof(title), "计划已保存 | 正常生效");
    draw_text(pixels, stride, panel.x + 20, panel.y + 34, title, 19,
              dirty ? (requires_hold ? UI_DANGER : UI_WARNING) : UI_SUCCESS);
    const char *sub = model->disable_flag_present ? "控制已停用，计划只读" :
        (model->waiting ? "正在保存中，请稍候..." :
         (model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR ? "按 + 完成输入，再保存计划" :
          (requires_hold ? (fresh ? "保存后今日额度将耗尽；需长按 A 确认" : "今日状态待确认；保存需长按 A 确认") :
           (dirty ? "按 + 保存后应用到主机" : "已与主机策略同步"))));
    draw_text(pixels, stride, panel.x + 20, panel.y + 58, sub, 13,
              requires_hold ? UI_DANGER : UI_RGB(UI_BLENDED(text_secondary)));

    UiRect result = {panel.x + 20, panel.y + 74, panel.width - 40, 96};
    UiRect left = {result.x + 10, result.y + 8, 138, result.height - 16};
    UiRect right = {result.x + result.width - 148, result.y + 8, 138, result.height - 16};
    fill_round_rect(pixels, stride, result, 12, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, result, 12, 1, UI_RGB(UI_BLENDED(border_control)));

    if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
        draw_status_symbol(pixels, stride, result.x + 24, result.y + 48, UI_MUTED, 1);
        draw_text(pixels, stride, result.x + 44, result.y + 30, "今天额度不变", 16, UI_INK);
        draw_wrapped_text(pixels, stride, result.x + 44, result.y + 54, impact,
                          12, result.width - 58, 17, 2, UI_MUTED);
    } else {
        bool current_state = projection.state == PTC_UI_PLAN_IMPACT_CURRENT;
        PtcDayRule left_rule = current_state ? projection.after.rule : projection.before.rule;
        PtcDayRule right_rule = projection.after.rule;
        draw_text_center(pixels, stride, (UiRect){left.x, left.y, left.width, 22},
                         current_state ? "今日额度" : "当前额度", 13, UI_MUTED);
        if (left_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(left_value, sizeof(left_value), "不限时");
        else snprintf(left_value, sizeof(left_value), "%u 分钟", (unsigned int)left_rule.minutes);
        snprintf(left_note, sizeof(left_note), "%s",
                 ptc_ui_effective_rule_label(current_state ? projection.after.source : projection.before.source));
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 24, left.width, 30}, left_value, 19,
                         left_rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT);
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 55, left.width, 20}, left_note, 11, UI_MUTED);

        if (!current_state) {
            int arrow_x = result.x + result.width / 2;
            int arrow_y = result.y + 40;
            draw_line(pixels, stride, arrow_x - 13, arrow_y, arrow_x + 10, arrow_y, 2, UI_MUTED);
            draw_line(pixels, stride, arrow_x + 5, arrow_y - 5, arrow_x + 10, arrow_y, 2, UI_MUTED);
            draw_line(pixels, stride, arrow_x + 5, arrow_y + 5, arrow_x + 10, arrow_y, 2, UI_MUTED);
        }

        if (current_state) {
            draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 22}, "今天还可玩", 13, UI_MUTED);
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "不限时");
            else if (projection.remaining_available) snprintf(right_value, sizeof(right_value), "%d 分钟", projection.remaining_minutes);
            else snprintf(right_value, sizeof(right_value), "待确认");
            if (played >= 0) snprintf(right_note, sizeof(right_note), "已用约 %d 分钟", played);
            else snprintf(right_note, sizeof(right_note), "刷新后显示");
        } else {
            draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 22}, "保存后额度", 13, UI_MUTED);
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "不限时");
            else snprintf(right_value, sizeof(right_value), "%u 分钟", (unsigned int)right_rule.minutes);
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_note, sizeof(right_note), "预计还可玩：不限时");
            else if (projection.remaining_available) snprintf(right_note, sizeof(right_note), "预计还可玩 %d 分钟", projection.remaining_minutes);
            else snprintf(right_note, sizeof(right_note), "剩余待刷新确认");
        }
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 24, right.width, 30}, right_value, 19,
                         right_rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS :
                         (projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER :
                          (projection.state == PTC_UI_PLAN_IMPACT_UNKNOWN ? UI_MUTED : UI_SUCCESS)));
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 55, right.width, 20}, right_note, 11,
                         projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_MUTED);
    }

    snprintf(source_text, sizeof(source_text), "%s  %s", dirty ? "保存后来源" : "今天来源",
             ptc_ui_effective_rule_label(projection.after.source));
    {
        int source_width = measure_text(source_text, 12) + 24;
        if (source_width > panel.width - 40) source_width = panel.width - 40;
        UiRect source = {panel.x + 20, panel.y + 184, source_width, 26};
        fill_round_rect(pixels, stride, source, 13, UI_ACCENT_SOFT);
        draw_text_center(pixels, stride, source, source_text, 12, UI_ACCENT);
    }

    if (error) {
        state_message = "保存失败，草稿已保留；检查后可重新保存。";
        state_color = UI_DANGER; state_background = UI_DANGER_SOFT;
    } else if (model->disable_flag_present) {
        state_message = "紧急停用中，计划保持只读。";
        state_color = UI_DANGER; state_background = UI_DANGER_SOFT;
    } else if (model->waiting) {
        state_message = "正在保存草稿，请稍候。";
        state_color = UI_WARNING; state_background = UI_WARNING_SOFT;
    } else if (model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
        state_message = "先完成额度输入，再返回保存计划。";
        state_color = UI_WARNING; state_background = UI_WARNING_SOFT;
    } else if (!fresh && projection.quota_changes_today) {
        state_message = requires_hold ? "状态待确认；保存时需长按 A 确认今天的影响。" :
            "状态待确认，保存时会再次核对今天的影响。";
        state_color = UI_WARNING; state_background = UI_WARNING_SOFT;
    } else if (requires_hold) {
        state_message = "保存后今天的时间将立即用尽，需长按 A 确认。";
        state_color = UI_DANGER; state_background = UI_DANGER_SOFT;
    }

    UiRect conclusion = {panel.x + 20, panel.y + 222, panel.width - 40, 58};
    if (state_message) {
        fill_round_rect(pixels, stride, conclusion, 10, state_background);
        draw_wrapped_text(pixels, stride, conclusion.x + 12, conclusion.y + 24, state_message,
                          13, conclusion.width - 24, 18, 2, state_color);
    } else if (projection.state == PTC_UI_PLAN_IMPACT_CURRENT) {
        draw_text(pixels, stride, conclusion.x, conclusion.y + 16, "当前状态", 13, UI_MUTED);
        draw_text(pixels, stride, conclusion.x, conclusion.y + 39,
                  "计划已保存，没有等待应用的额度草稿。", 13, UI_RGB(UI_BLENDED(text_secondary)));
    } else if (projection.state != PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        draw_text(pixels, stride, conclusion.x, conclusion.y + 16, "保存后的今天", 13, UI_MUTED);
        ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
        draw_wrapped_text(pixels, stride, conclusion.x, conclusion.y + 39, impact,
                          13, conclusion.width, 18, 2, UI_RGB(UI_BLENDED(text_secondary)));
    }

    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, panel.x + 20, panel.y + panel.height - 20,
              age, 13, status_age_color(model));
}

void draw_plan_impact_compact(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                              PtcUiPlanKind kind, UiRect panel)
{
    char title[64], impact[256], left_value[40], right_value[48], source_text[80];
    PtcUiPlanImpactProjection projection;
    int changed = 0;
    ptc_ui_project_plan_impact(model, kind, true, ptc_ui_render_now(), &projection);

    if (kind == PTC_UI_PLAN_WEEKLY) {
        for (int day = 0; day < 7; ++day) {
            if (ptc_ui_day_rule_effectively_changed(model->current_week[day], model->draft_week[day])) ++changed;
        }
        if (changed > 0) snprintf(title, sizeof(title), "修改草稿 | 已调整 %d 天", changed);
        else snprintf(title, sizeof(title), "修改草稿 | 待保存");
    } else if (kind == PTC_UI_PLAN_SCHEDULED) {
        if (ptc_ui_scheduled_dirty(model)) ++changed;
        if (changed > 0) snprintf(title, sizeof(title), "修改草稿 | 临时计划已调整");
        else snprintf(title, sizeof(title), "修改草稿 | 待保存");
    } else {
        if (model->holiday_enabled != model->draft_holiday_enabled) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->holiday_rule, model->draft_holiday_rule)) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->makeup_workday_rule, model->draft_makeup_workday_rule)) ++changed;
        if (changed > 0) snprintf(title, sizeof(title), "修改草稿 | 已调整 %d 项", changed);
        else snprintf(title, sizeof(title), "修改草稿 | 待保存");
    }

    draw_plan_card(pixels, stride, panel, false);
    draw_text(pixels, stride, panel.x + 20, panel.y + 32, title, 18, UI_WARNING);

    UiRect result = {panel.x + 20, panel.y + 50, panel.width - 40, 104};
    int col_w = panel.width >= 440 ? 168 : 132;
    UiRect left = {result.x + 10, result.y + 8, col_w, result.height - 16};
    UiRect right = {result.x + result.width - 10 - col_w, result.y + 8, col_w, result.height - 16};
    fill_round_rect(pixels, stride, result, 12, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, result, 12, 1, UI_RGB(UI_BLENDED(border_control)));
    if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
        draw_status_symbol(pixels, stride, result.x + 24, result.y + 52, UI_MUTED, 1);
        draw_text(pixels, stride, result.x + 44, result.y + 34, "今天额度不变", 15, UI_INK);
        draw_wrapped_text(pixels, stride, result.x + 44, result.y + 58, impact,
                          12, result.width - 56, 17, 2, UI_MUTED);
    } else {
        draw_text_center(pixels, stride, (UiRect){left.x, left.y, left.width, 22}, "当前额度", 12, UI_MUTED);
        if (projection.before.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(left_value, sizeof(left_value), "不限时");
        else snprintf(left_value, sizeof(left_value), "%u 分钟", (unsigned int)projection.before.rule.minutes);
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 24, left.width, 34}, left_value, 19,
                         projection.before.rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT);
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 61, left.width, 18},
                         ptc_ui_effective_rule_label(projection.before.source), 11, UI_MUTED);

        int arrow_x = result.x + result.width / 2;
        int arrow_y = result.y + 42;
        draw_line(pixels, stride, arrow_x - 11, arrow_y, arrow_x + 9, arrow_y, 2, UI_MUTED);
        draw_line(pixels, stride, arrow_x + 4, arrow_y - 5, arrow_x + 9, arrow_y, 2, UI_MUTED);
        draw_line(pixels, stride, arrow_x + 4, arrow_y + 5, arrow_x + 9, arrow_y, 2, UI_MUTED);

        draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 22}, "保存后额度", 12, UI_MUTED);
        if (projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "不限时");
        else snprintf(right_value, sizeof(right_value), "%u 分钟", (unsigned int)projection.after.rule.minutes);
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 24, right.width, 34}, right_value, 19,
                         projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS :
                         (projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_ACCENT));
        if (projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "预计还可玩：不限时");
        else if (projection.remaining_available) snprintf(right_value, sizeof(right_value), "预计还可玩 %d 分钟", projection.remaining_minutes);
        else snprintf(right_value, sizeof(right_value), "剩余待刷新确认");
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 61, right.width, 18}, right_value, 11,
                         projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_MUTED);
    }

    snprintf(source_text, sizeof(source_text), "保存后来源  %s", ptc_ui_effective_rule_label(projection.after.source));
    {
        int source_width = measure_text(source_text, 12) + 24;
        if (source_width > panel.width - 40) source_width = panel.width - 40;
        UiRect source = {panel.x + 20, panel.y + 174, source_width, 26};
        fill_round_rect(pixels, stride, source, 13, UI_ACCENT_SOFT);
        draw_text_center(pixels, stride, source, source_text, 12, UI_ACCENT);
    }
}

static void draw_weekly_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    int slot;
    char detail[64];
    char freshness[64];
    uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
    format_status_age(model, freshness, sizeof(freshness));
    draw_rect_outline(pixels, stride, (UiRect){54, 178, 26, 24}, 4, 2, UI_ACCENT);
    draw_line(pixels, stride, 54, 186, 80, 186, 2, UI_ACCENT);
    draw_line(pixels, stride, 61, 174, 61, 181, 3, UI_ACCENT);
    draw_line(pixels, stride, 73, 174, 73, 181, 3, UI_ACCENT);
    draw_text(pixels, stride, 92, 198, "周一到周日基础规划  |  点按模式或额度区直接修改", 17, UI_MUTED);
    for (slot = 0; slot < 7; ++slot) {
        int day = ptc_ui_weekday_for_display_slot(slot);
        bool selected = slot == model->weekly_grid_slot && model->selected_index == 0;
        bool today = model->status_loaded && day == weekday;
        UiRect card = to_uirect(ptc_ui_weekly_day_rect(slot));
        UiRect header = to_uirect(ptc_ui_weekly_day_header_rect(slot));
        UiRect mode = to_uirect(ptc_ui_weekly_day_mode_rect(slot));
        UiRect minutes = to_uirect(ptc_ui_weekly_day_minutes_rect(slot));
        bool limited = model->draft_week[day].mode == PTC_RULE_MODE_LIMIT;
        draw_plan_card(pixels, stride, card, selected && !model->parent_footer_focused);
        draw_text_center(pixels, stride, header, DAYS[day], 19, UI_INK);
        fill_round_rect(pixels, stride, (UiRect){mode.x + 6, mode.y + 6, mode.width - 12, 32}, 14,
                        model->disable_flag_present ? UI_BORDER :
                        (limited ? UI_ACCENT : UI_SUCCESS));
        draw_text_center(pixels, stride, (UiRect){mode.x + 6, mode.y + 6, mode.width - 12, 32},
                         limited ? "限时" : "不限时", 15,
                         model->disable_flag_present ? UI_DISABLED : UI_ON_ACCENT);
        UiRect today_rect = {card.x, minutes.y + 8, card.width, 22};
        draw_text_center(pixels, stride, today_rect,
                         today ? "● 今天" : " ", 13, today ? UI_SUCCESS : UI_MUTED);
        if (limited) {
            snprintf(detail, sizeof(detail), "%u", (unsigned int)model->draft_week[day].minutes);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 36, minutes.width, 46}, detail, 30,
                             model->disable_flag_present ? UI_DISABLED : UI_ACCENT);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 84, minutes.width, 24}, "分钟", 14, UI_MUTED);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 124, minutes.width, 22}, "A / 点按修改", 12,
                             model->disable_flag_present ? UI_DISABLED : UI_MUTED);
        } else {
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 50, minutes.width, 36}, "不限时间", 18,
                             model->disable_flag_present ? UI_DISABLED : UI_SUCCESS);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 94, minutes.width, 24}, "全天无限制", 13, UI_MUTED);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 124, minutes.width, 22}, "点按看提示", 11, UI_DISABLED);
        }
        /* 每日容量柱状直方图 (Weekly Capacity Histogram Bar) */
        int bar_w = card.width - 24;
        int bar_x = card.x + 12;
        int bar_y = card.y + card.height - 14;
        UiRect bar_bg = {bar_x, bar_y, bar_w, 5};
        fill_round_rect(pixels, stride, bar_bg, 2, UI_BORDER);
        if (limited) {
            int fill_w = (int)((int64_t)bar_w * (model->draft_week[day].minutes > 180 ? 180 : model->draft_week[day].minutes) / 180);
            if (fill_w < 4 && model->draft_week[day].minutes > 0) fill_w = 4;
            uint32_t bar_col = model->disable_flag_present ? UI_DISABLED :
                (model->draft_week[day].minutes <= 30 ? UI_WARNING : UI_ACCENT);
            if (fill_w > 0) {
                fill_round_rect(pixels, stride, (UiRect){bar_x, bar_y, fill_w, 5}, 2, bar_col);
            }
        } else {
            fill_round_rect(pixels, stride, bar_bg, 2, model->disable_flag_present ? UI_DISABLED : UI_SUCCESS);
        }
    }
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_WEEKLY, model->weekly_dirty,
                     (UiRect){838, 176, 388, 452});
    draw_candidate_button(pixels, stride, ptc_ui_weekly_page_mode_rect(), "X  切换模式",
                           UI_PAGE, UI_ACCENT, model->selected_index == 1,
                           model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_bulk_rect(), "批量设置",
                          UI_PAGE, UI_ACCENT, model->selected_index == 2,
                          model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_discard_rect(), "ZL  放弃",
                           UI_PAGE, UI_INK, model->selected_index == 3,
                           !model->weekly_dirty);
    bool weekly_hold = model->weekly_dirty && ptc_ui_plan_save_requires_hold(model, PTC_UI_PLAN_WEEKLY, ptc_ui_render_now());
    draw_candidate_button(pixels, stride, ptc_ui_weekly_save_rect(),
                           model->disable_flag_present ? "只读" : (model->waiting ? "保存中" : (model->weekly_dirty ? (weekly_hold ? "+  保存（需确认）" : "+  保存草稿") : "已保存")),
                           weekly_hold ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT, model->selected_index == 4,
                           !model->weekly_dirty || model->disable_flag_present || model->waiting);
}

static void draw_holiday_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {838, 176, 388, 340};
    UiRect top_card = to_uirect(ptc_ui_holiday_card_rect(0));
    const char *titles[] = {"法定休假", "调休工作日"};
    const char *descriptions[] = {"主要法定节假日的休假日期", "节假日调休产生的补班日期"};
    char line[160];
    char minutes_str[64];
    bool disabled = model->disable_flag_present;
    bool top_selected = model->selected_index == 0;
    fill_round_rect(pixels, stride, top_card, 16, disabled ? UI_PAGE : (top_selected ? UI_ACCENT_SOFT : UI_SURFACE));
    draw_rect_outline(pixels, stride, top_card, 16, top_selected ? 3 : 1, top_selected ? UI_ACCENT : UI_BORDER);
    draw_text(pixels, stride, top_card.x + 20, top_card.y + 32, "国家节假日规则", 22, UI_INK);
    draw_text(pixels, stride, top_card.x + 20, top_card.y + 60, "开启后自动应用法定休假与调休工作日规则", 14, UI_MUTED);
    {
        const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
        snprintf(line, sizeof(line), "内置日历：%u  |  v%u", (unsigned int)info->last_year, (unsigned int)info->version);
        draw_text(pixels, stride, top_card.x + 498, top_card.y + 34, line, 14,
                  model->calendar_update_warning ? UI_DANGER : UI_SUCCESS);
    }
    draw_toggle_switch(pixels, stride, to_uirect(ptc_ui_holiday_enable_rect()), model->draft_holiday_enabled,
                       top_selected, disabled, NULL, NULL);

    for (int index = 0; index < 2; ++index) {
        PtcDayRule rule = index == 0 ? model->draft_holiday_rule : model->draft_makeup_workday_rule;
        UiRect card = to_uirect(ptc_ui_holiday_card_rect(index + 1));
        UiRect mode = to_uirect(ptc_ui_holiday_mode_rect(index));
        UiRect minutes = to_uirect(ptc_ui_holiday_minutes_rect(index));
        bool selected = model->selected_index == index + 1;
        bool limited = rule.mode == PTC_RULE_MODE_LIMIT;
        draw_plan_card(pixels, stride, card, selected && !model->parent_footer_focused);
        draw_text(pixels, stride, card.x + 18, card.y + 34, titles[index], 21, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, card.x + 18, card.y + 62, descriptions[index], 13, UI_MUTED);
        fill_round_rect(pixels, stride, mode, 18,
                        disabled ? UI_BORDER : (limited ? UI_ACCENT : UI_SUCCESS));
        draw_text_center(pixels, stride, mode, limited ? "限时" : "不限时", 14,
                         disabled ? UI_DISABLED : UI_ON_ACCENT);
        fill_round_rect(pixels, stride, minutes, 12, disabled || !limited ? UI_RAISED : UI_RAISED);
        if (limited) {
            snprintf(minutes_str, sizeof(minutes_str), "%u 分钟（%u小时%u分）", (unsigned int)rule.minutes,
                     (unsigned int)rule.minutes / 60, (unsigned int)rule.minutes % 60);
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 46, minutes_str, 22,
                      disabled ? UI_DISABLED : UI_RGB(UI_BLENDED(accent)));
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 88, "A / 点按修改额度", 14,
                      disabled ? UI_DISABLED : UI_MUTED);
        } else {
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 46, "不限时间", 22, UI_DISABLED);
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 88, "点按后提示先切换为限时", 14, UI_DISABLED);
        }
    }
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(3), "X  切换模式",
                           UI_PAGE, UI_ACCENT, model->selected_index == 3, disabled);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(4), "ZL  放弃",
                           UI_PAGE, UI_INK, model->selected_index == 4,
                           !model->holiday_dirty);
    bool holiday_hold = model->holiday_dirty && ptc_ui_plan_save_requires_hold(model, PTC_UI_PLAN_HOLIDAY, ptc_ui_render_now());
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(5),
                           disabled ? "紧急停用中，设置只读" : (model->waiting ? "正在保存..." :
                           (model->holiday_dirty ? (holiday_hold ? "+  保存（需确认）" : "+  保存草稿") : "已保存")),
                           holiday_hold ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT, model->selected_index == 5,
                           disabled || model->waiting || !model->holiday_dirty);
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_HOLIDAY, model->holiday_dirty, panel);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_calendar_rect(), "查看节假日安排",
                           UI_ACCENT_SOFT, UI_ACCENT, model->selected_index == 6, false);
}

const char *bedtime_override_label(PtcBedtimeOverrideMode mode)
{
    if (mode == PTC_BEDTIME_OVERRIDE_DISABLED) return "关闭";
    if (mode == PTC_BEDTIME_OVERRIDE_CUSTOM) return "自定义";
    return "继承";
}

static void draw_bedtime_window_value(char *out, size_t out_size, const PtcBedtimeWindow *window)
{
    if (!window->enabled) snprintf(out, out_size, "关闭");
    else snprintf(out, out_size, "%02u:%02u - 次日 %02u:%02u",
        (unsigned int)(window->start_minute / 60), (unsigned int)(window->start_minute % 60),
        (unsigned int)(window->end_minute / 60), (unsigned int)(window->end_minute % 60));
}

static void draw_bedtime_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *SECTIONS[] = {"每周", "节假日", "指定日期就寝"};
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    char line[128];
    time_t raw_now = time(NULL);
    struct tm *tm_now = localtime(&raw_now);
    uint16_t minute_of_day = tm_now ? (uint16_t)(tm_now->tm_hour * 60 + tm_now->tm_min) : 0;
    for (int i = 0; i < 3; ++i) {
        UiRect rect = to_uirect(ptc_ui_bedtime_section_rect(i));
        bool selected = model->bedtime_section == (PtcUiBedtimeSection)i;
        fill_round_rect(pixels, stride, rect, 12, selected ? UI_ACCENT : UI_RAISED);
        draw_text_center(pixels, stride, rect, SECTIONS[i], 18, selected ? UI_ON_ACCENT : UI_INK);
        if (selected && model->bedtime_section_focused) draw_focus_ring(pixels, stride, rect, 12);
    }

    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    bool bedtime_will_restrict = ptc_ui_bedtime_save_will_restrict(model, minute_of_day);
    bool bedtime_save_danger = model->bedtime_dirty && bedtime_will_restrict;

    /* 独立醒目的就寝管控总闸卡片 (Bedtime Master Circuit Breaker Card) */
    UiRect master_card = to_uirect(ptc_ui_bedtime_master_switch_rect());
    draw_card_shadow(pixels, stride, master_card, 16);
    fill_round_rect(pixels, stride, master_card, 16, UI_RGB(UI_BLENDED(surface)));
    if (bedtime_enforcing) {
        int phase = get_breathing_phase();
        draw_rect_outline(pixels, stride, master_card, 16, 2,
                          UI_RGB(ui_mix_rgb(UI_BLENDED(danger), 0xFF9A8A, phase * 4)));
    } else {
        draw_rect_outline(pixels, stride, master_card, 16, 1, UI_RGB(UI_BLENDED(border_control)));
    }
    draw_text(pixels, stride, master_card.x + 18, master_card.y + 28, "就寝管控总闸", 20, UI_INK);
    if (bedtime_enforcing) {
        draw_text(pixels, stride, master_card.x + 18, master_card.y + 54,
                  "立断执行中 (夜间就寝时段)", 13, UI_DANGER);
        UiRect active_pill = {master_card.x + 175, master_card.y + 12, 96, 20};
        fill_round_rect(pixels, stride, active_pill, 6, UI_DANGER_SOFT);
        draw_text_center(pixels, stride, active_pill, "● 立断生效中", 12, UI_DANGER);
    } else {
        draw_text(pixels, stride, master_card.x + 18, master_card.y + 54,
                  draft->enabled ? "已接通 (按每周/节假日/指定日期执行)" : "已切断 (停用全部就寝限制，含特殊日期)", 13,
                  draft->enabled ? UI_SUCCESS : UI_MUTED);
    }
    UiRect toggle_rect = {master_card.x + master_card.width - 76, master_card.y + (master_card.height - 30) / 2, 60, 30};
    draw_toggle_switch(pixels, stride, toggle_rect, draft->enabled, false, model->disable_flag_present, NULL, NULL);
    draw_text(pixels, stride, master_card.x + master_card.width - 92, master_card.y + master_card.height - 8,
              "- / 点按切换总闸", 11, UI_MUTED);

    /* 页面状态、预测与风险统一在一张卡片中，避免与全局状态和底部反馈重复。 */
    {
        UiRect eval_card = {838, 266, 388, 364};
        uint32_t state_color = strcmp(model->result_status, "error") == 0 ? UI_DANGER :
            (model->waiting || model->bedtime_dirty ? UI_WARNING :
             (bedtime_enforcing ? UI_DANGER : UI_SUCCESS));
        uint32_t state_bg = state_color == UI_DANGER ? UI_DANGER_SOFT :
            (state_color == UI_WARNING ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
        const char *state_text = strcmp(model->result_status, "error") == 0 ? "保存失败，草稿仍保留" :
            (model->waiting ? "正在保存并等待后台确认" :
             (model->bedtime_dirty ? "草稿尚未保存" :
              (bedtime_enforcing ? "就寝限制正在生效" : "计划已保存")));
        fill_round_rect(pixels, stride, eval_card, 12, UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, eval_card, 12, 1, UI_RGB(UI_BLENDED(border_control)));
        draw_text(pixels, stride, eval_card.x + 16, eval_card.y + 28,
                  "计划状态与今晚预测", 17, UI_RGB(UI_BLENDED(text_primary)));
        UiRect state_pill = {eval_card.x + 16, eval_card.y + 42, eval_card.width - 32, 34};
        fill_round_rect(pixels, stride, state_pill, 9, state_bg);
        draw_rect_outline(pixels, stride, state_pill, 9, 1, state_color);
        draw_text_center(pixels, stride, state_pill, state_text, 14, state_color);

        PtcRules eval_rules;
        memset(&eval_rules, 0, sizeof(eval_rules));
        eval_rules.bedtime = *draft;
        PtcBedtimeEvaluation eval = ptc_bedtime_evaluate(
            &eval_rules, model->day_index, ptc_weekday_from_day_index(model->day_index), minute_of_day);

        char forecast_line1[96];
        char forecast_line2[96];
        uint32_t f1_color = UI_RGB(UI_BLENDED(text_primary));

        if (!draft->enabled) {
            snprintf(forecast_line1, sizeof(forecast_line1), "当前预测：总闸已切断，全部就寝规则休眠，夜间不限制");
            f1_color = UI_MUTED;
        } else if (bedtime_will_restrict) {
            snprintf(forecast_line1, sizeof(forecast_line1), "! 立即生效：当前处于就寝时段，保存后将立断！");
            f1_color = UI_DANGER;
        } else if (eval.active) {
            snprintf(forecast_line1, sizeof(forecast_line1), "当前状态：就寝限制正在生效");
            f1_color = UI_DANGER;
        } else {
            PtcEffectiveBedtime eff_today = ptc_bedtime_resolve_start_day(
                &eval_rules, model->day_index, ptc_weekday_from_day_index(model->day_index));
            if (eff_today.window.enabled) {
                int diff_m = (int)eff_today.window.start_minute - (int)minute_of_day;
                if (diff_m < 0) diff_m += 1440;
                snprintf(forecast_line1, sizeof(forecast_line1), "今晚 %02u:%02u 开始就寝（距现在约 %u 小时 %u 分）",
                         (unsigned int)(eff_today.window.start_minute / 60),
                         (unsigned int)(eff_today.window.start_minute % 60),
                         (unsigned int)(diff_m / 60), (unsigned int)(diff_m % 60));
                f1_color = UI_SUCCESS;
            } else {
                snprintf(forecast_line1, sizeof(forecast_line1), "今晚安排：本日就寝窗口已关闭，不限制");
                f1_color = UI_MUTED;
            }
        }
        draw_wrapped_text(pixels, stride, eval_card.x + 16, eval_card.y + 104,
                          forecast_line1, 13, eval_card.width - 32, 19, 2, f1_color);

        int enabled_count = 0;
        int total_span_m = 0;
        for (int d = 0; d < 7; ++d) {
            if (draft->week[d].enabled) {
                enabled_count++;
                total_span_m += (int)(draft->week[d].end_minute + 1440 - draft->week[d].start_minute) % 1440;
            }
        }
        if (draft->enabled) {
            snprintf(forecast_line2, sizeof(forecast_line2), "每周统计：开启 %d/7 天  |  周总就寝管控 %u 小时",
                     enabled_count, (unsigned int)(total_span_m / 60));
        } else {
            snprintf(forecast_line2, sizeof(forecast_line2), "每周统计：休眠中 (预设 %d/7 天，总闸切断不生效)",
                     enabled_count);
        }
        draw_text(pixels, stride, eval_card.x + 16, eval_card.y + 150,
                  forecast_line2, 13, UI_MUTED);

        const char *cal_str = draft->calendar_enabled
            ? (draft->enabled ? "节假日日历开启" : "节假日日历(休眠)")
            : "节假日跟随周计划";
        const char *sch_str = draft->scheduled_override.present
            ? (draft->enabled ? "指定日期生效中" : "指定日期生效(休眠)")
            : "指定日期未设置";
        char special_line[128];
        snprintf(special_line, sizeof(special_line), "特殊规则：%s  |  %s", cal_str, sch_str);
        draw_wrapped_text(pixels, stride, eval_card.x + 16, eval_card.y + 178,
                          special_line, 12, eval_card.width - 32, 18, 2, UI_MUTED);

        UiRect risk = {eval_card.x + 16, eval_card.y + 218, eval_card.width - 32, 64};
        fill_round_rect(pixels, stride, risk, 8,
            model->bedtime_official_setting_confirmed && model->bedtime_overlay_verified
                ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
        draw_wrapped_text(pixels, stride, risk.x + 12, risk.y + 22,
            model->bedtime_official_setting_confirmed
                ? (model->bedtime_overlay_verified
                    ? "官方暂停设置已确认，Overlay 已验证。"
                    : "官方暂停设置已确认；保存即接受 Overlay 尚未验证的恢复风险。")
                : "首次启用前请确认 Nintendo 家长控制已开启“时间到了暂停软件”，并接受 Overlay 恢复风险。",
            12, risk.width - 24, 18, 2,
            model->bedtime_official_setting_confirmed && model->bedtime_overlay_verified
                ? UI_SUCCESS : UI_WARNING);

        draw_text(pixels, stride, eval_card.x + 16,
                  eval_card.y + eval_card.height - 18,
                  "L/R 切换区段，- 总闸，+ 保存，ZL 放弃",
                  12, UI_ACCENT);
    }

    if (model->bedtime_section == PTC_UI_BEDTIME_WEEKLY) {
        int today_weekday = ptc_weekday_from_day_index(model->day_index);
        for (int slot = 0; slot < 7; ++slot) {
            int day = ptc_ui_weekday_for_display_slot(slot);
            UiRect card = to_uirect(ptc_ui_bedtime_field_rect(PTC_UI_BEDTIME_WEEKLY, slot));
            char value[64];
            int diff = day - today_weekday;
            uint16_t slot_day_idx = (uint16_t)(model->day_index + diff);
            uint16_t c_y; uint8_t c_m, c_d;
            bool has_date = ptc_date_from_day_index(slot_day_idx, &c_y, &c_m, &c_d);
            bool is_today = (day == today_weekday);
            bool is_active_day = (bedtime_enforcing && is_today);

            draw_plan_card(pixels, stride, card,
                model->selected_index == slot && !model->bedtime_section_focused && !model->parent_footer_focused);

            if (is_today) {
                fill_round_rect(pixels, stride, card, 16, is_active_day ? UI_DANGER_SOFT : UI_ACCENT_SOFT);
                draw_rect_outline(pixels, stride, card, 16, 2, is_active_day ? UI_DANGER : UI_ACCENT);
                if (model->selected_index == slot && !model->bedtime_section_focused && !model->parent_footer_focused) {
                    draw_focus_ring(pixels, stride, card, 16);
                }
                UiRect today_pill = {card.x + (card.width - 50) / 2, card.y + 6, 50, 18};
                fill_round_rect(pixels, stride, today_pill, 5, is_active_day ? UI_DANGER : UI_ACCENT);
                draw_text_center(pixels, stride, today_pill, is_active_day ? "● 立断" : "★ 今日", 12, UI_ON_ACCENT);
            }

            draw_text_center(pixels, stride, (UiRect){card.x, card.y + (is_today ? 25 : 13), card.width, 20},
                             DAYS[day], is_today ? 17 : 16, is_today ? (is_active_day ? UI_DANGER : UI_ACCENT) : UI_INK);

            if (has_date) {
                char date_str[16];
                snprintf(date_str, sizeof(date_str), "%02u/%02u", c_m, c_d);
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + (is_today ? 44 : 33), card.width, 18},
                                 date_str, 12, is_today ? (is_active_day ? UI_DANGER : UI_ACCENT) : UI_MUTED);
            }

            draw_bedtime_window_value(value, sizeof(value), &draft->week[day]);
            if (draft->week[day].enabled) {
                snprintf(line, sizeof(line), "%02u:%02u",
                    (unsigned int)(draft->week[day].start_minute / 60),
                    (unsigned int)(draft->week[day].start_minute % 60));
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 64, card.width, 22}, line, 18,
                                 draft->enabled ? (is_active_day ? UI_DANGER : UI_ACCENT) : UI_MUTED);
                snprintf(line, sizeof(line), "到 %02u:%02u",
                    (unsigned int)(draft->week[day].end_minute / 60),
                    (unsigned int)(draft->week[day].end_minute % 60));
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 88, card.width, 18}, line, 13, UI_MUTED);

                /* 开启状态微标 */
                UiRect pill = {card.x + (card.width - 48) / 2, card.y + 114, 48, 20};
                fill_round_rect(pixels, stride, pill, 6,
                                is_active_day ? UI_DANGER_SOFT : (draft->enabled ? UI_ACCENT_SOFT : UI_RAISED));
                draw_text_center(pixels, stride, pill,
                                 is_active_day ? "生效中" : (draft->enabled ? "开启" : "暂停"), 12,
                                 is_active_day ? UI_DANGER : (draft->enabled ? UI_ACCENT : UI_MUTED));
            } else {
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 74, card.width, 24}, "关闭", 18, UI_MUTED);
                UiRect pill = {card.x + (card.width - 48) / 2, card.y + 114, 48, 20};
                fill_round_rect(pixels, stride, pill, 6, UI_RAISED);
                draw_text_center(pixels, stride, pill, "关闭", 12, UI_MUTED);
            }
            draw_text_center(pixels, stride, (UiRect){card.x, card.y + 148, card.width, 18}, "A 设置", 11, UI_MUTED);
        }
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 7), "复制到工作日",
            UI_PAGE, UI_ACCENT, model->selected_index == 7 && !model->bedtime_section_focused, model->disable_flag_present);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 8), "复制到周末",
            UI_PAGE, UI_ACCENT, model->selected_index == 8 && !model->bedtime_section_focused, model->disable_flag_present);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 9), "ZL  放弃",
            UI_PAGE, UI_INK, model->selected_index == 9 && !model->bedtime_section_focused, !model->bedtime_dirty);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 10),
            bedtime_save_danger ? "+  保存（将立断）" : "+  保存",
            bedtime_save_danger ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT,
            model->selected_index == 10 && !model->bedtime_section_focused,
            !model->bedtime_dirty || model->disable_flag_present || model->waiting);

        /* 左下角就寝规则说明面板（与右侧预测卡片底部平齐对齐至 y=630） */
        UiRect bedtime_guide = {54, 500, 752, 130};
        fill_round_rect(pixels, stride, bedtime_guide, 12, UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, bedtime_guide, 12, 1, UI_RGB(UI_BLENDED(border_control)));
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 26,
                  "就寝时间并行规则与执行机制", 16, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 54,
                  "• 到点立断：进入就寝窗口后立即强制限制，不受今日剩余游玩额度影响", 14, UI_MUTED);
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 80,
                  "• 单次跳过：若今晚有特殊需要，可在今日调度页面选择“跳过今晚就寝”放行一次", 14, UI_MUTED);
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 106,
                  "• 跨日计算：就寝窗口支持跨日（如 22:00 至次日 07:00），到结束时间自动恢复", 14, UI_MUTED);
    } else if (model->bedtime_section == PTC_UI_BEDTIME_CALENDAR) {
        UiRect master = to_uirect(ptc_ui_bedtime_field_rect(1, 0));
        draw_plan_card(pixels, stride, master, model->selected_index == 0 && !model->bedtime_section_focused);
        draw_text(pixels, stride, master.x + 20, master.y + 40, "国家节假日日历", 20, UI_INK);
        if (!draft->enabled) {
            draw_text(pixels, stride, master.x + 190, master.y + 40,
                draft->calendar_enabled ? "已开启 (休眠)" : "已关闭", 18, UI_MUTED);
        } else {
            draw_text(pixels, stride, master.x + 190, master.y + 40,
                draft->calendar_enabled ? "开启" : "关闭", 18,
                draft->calendar_enabled ? UI_SUCCESS : UI_MUTED);
        }
        UiRect cal_toggle = {master.x + master.width - 76, master.y + (master.height - 28) / 2, 58, 28};
        draw_toggle_switch(pixels, stride, cal_toggle, draft->calendar_enabled,
                           model->selected_index == 0 && !model->bedtime_section_focused,
                           !draft->enabled || model->disable_flag_present, NULL, NULL);
        draw_text(pixels, stride, master.x + master.width - 165, master.y + 39, "A 切换", 13, UI_MUTED);

        for (int i = 0; i < 2; ++i) {
            const PtcBedtimeSpecialRule *rule = i == 0 ? &draft->holiday_rule : &draft->makeup_workday_rule;
            UiRect card = to_uirect(ptc_ui_bedtime_field_rect(1, i + 1));
            char value[64];
            draw_plan_card(pixels, stride, card, model->selected_index == i + 1 && !model->bedtime_section_focused);
            draw_text(pixels, stride, card.x + 18, card.y + 30,
                i == 0 ? "法定休假" : "调休工作日", 19, UI_INK);
            draw_bedtime_window_value(value, sizeof(value), &rule->window);
            if (!draft->enabled) {
                snprintf(line, sizeof(line), "%s  |  %s (总闸休眠)", bedtime_override_label(rule->mode),
                    rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM ? value : "使用对应模式");
            } else {
                snprintf(line, sizeof(line), "%s  |  %s", bedtime_override_label(rule->mode),
                    rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM ? value : "使用对应模式");
            }
            draw_text(pixels, stride, card.x + 18, card.y + 72, line, 15, UI_MUTED);
        }
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(1, 3), "ZL  放弃",
            UI_PAGE, UI_INK, model->selected_index == 3 && !model->bedtime_section_focused, !model->bedtime_dirty);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(1, 4),
            bedtime_save_danger ? "+  保存（将立断）" : "+  保存",
            bedtime_save_danger ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT,
            model->selected_index == 4 && !model->bedtime_section_focused,
            !model->bedtime_dirty || model->disable_flag_present || model->waiting);

        /* 节假日日历规则与总闸状态指引面板（底部与右侧预测卡平齐至 y=630） */
        UiRect cal_guide = {54, 536, 752, 94};
        fill_round_rect(pixels, stride, cal_guide, 12,
                        !draft->enabled ? UI_WARNING_SOFT : UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, cal_guide, 12, 1,
                          !draft->enabled ? UI_WARNING : UI_RGB(UI_BLENDED(border_control)));
        if (!draft->enabled) {
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 24,
                      "⚠️ 就寝管控总闸已切断 (休眠状态)", 15, UI_WARNING);
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 48,
                      "• 总闸切断时，所有节假日日历和调休就寝均暂停执行，夜间不设任何就寝限制。", 13, UI_MUTED);
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 70,
                      "• 您可在此配置规则，按 [-] 键或触摸右侧“就寝管控总闸”接通后即按计划生效。", 13, UI_MUTED);
        } else {
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 24,
                      "国家节假日日历与规则优先级", 15, UI_RGB(UI_BLENDED(text_primary)));
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 48,
                      "• 开启日历后，国家法定休假与调休工作日将自动优先于每周基础计划生效。", 13, UI_MUTED);
            draw_text(pixels, stride, cal_guide.x + 18, cal_guide.y + 70,
                      "• 调休工作日建议设为严格就寝；法定休假可根据需要放宽或免除就寝限制。", 13, UI_MUTED);
        }
    } else {
        const PtcBedtimeScheduledOverride *scheduled = &draft->scheduled_override;
        uint32_t duration = scheduled->end_day_index >= scheduled->start_day_index
            ? (uint32_t)scheduled->end_day_index - scheduled->start_day_index + 1u : 1u;
        const char *labels[] = {"指定日期就寝", "开始日期", "持续天数", "就寝规则"};
        char values[4][96];
        if (!draft->enabled) {
            snprintf(values[0], sizeof(values[0]), "%s", scheduled->present ? "开启 (休眠)" : "关闭");
        } else {
            snprintf(values[0], sizeof(values[0]), "%s", scheduled->present ? "开启" : "关闭");
        }
        {
            uint16_t year;
            uint8_t month, day;
            if (ptc_date_from_day_index(scheduled->start_day_index, &year, &month, &day))
                snprintf(values[1], sizeof(values[1]), "%04u-%02u-%02u", year, month, day);
            else snprintf(values[1], sizeof(values[1]), "等待有效日期");
        }
        snprintf(values[2], sizeof(values[2]), "%u 天", (unsigned int)duration);
        if (scheduled->rule.mode == PTC_BEDTIME_OVERRIDE_CUSTOM) {
            char value[64];
            draw_bedtime_window_value(value, sizeof(value), &scheduled->rule.window);
            snprintf(values[3], sizeof(values[3]), "%s  %s%s",
                bedtime_override_label(scheduled->rule.mode), value,
                draft->enabled ? "" : " (总闸休眠)");
        } else {
            snprintf(values[3], sizeof(values[3]), "%s%s",
                bedtime_override_label(scheduled->rule.mode),
                draft->enabled ? "" : " (总闸休眠)");
        }
        for (int i = 0; i < 4; ++i) {
            UiRect row = to_uirect(ptc_ui_bedtime_field_rect(2, i));
            draw_plan_card(pixels, stride, row, model->selected_index == i && !model->bedtime_section_focused);
            if (i == 0) {
                draw_text(pixels, stride, row.x + 18, row.y + 31, labels[0], 17, UI_MUTED);
                if (!draft->enabled) {
                    draw_text(pixels, stride, row.x + 150, row.y + 31,
                              scheduled->present ? "开启 (休眠)" : "关闭", 18, UI_MUTED);
                } else {
                    draw_text(pixels, stride, row.x + 150, row.y + 31,
                              scheduled->present ? "开启" : "关闭", 18,
                              scheduled->present ? UI_SUCCESS : UI_MUTED);
                }
                UiRect sched_toggle = {row.x + row.width - 76, row.y + (row.height - 28) / 2, 58, 28};
                draw_toggle_switch(pixels, stride, sched_toggle, scheduled->present,
                                   model->selected_index == 0 && !model->bedtime_section_focused,
                                   !draft->enabled || model->disable_flag_present, NULL, NULL);
                draw_text(pixels, stride, row.x + row.width - 165, row.y + 31, "A 切换", 13, UI_MUTED);
            } else {
                draw_text(pixels, stride, row.x + 18, row.y + 31, labels[i], 17, UI_MUTED);
                draw_text(pixels, stride, row.x + 240, row.y + 31, values[i], 18, UI_INK);
                if (i == 1 || i == 2)
                    draw_text(pixels, stride, row.x + 484, row.y + 31,
                              "A 输入 | ZL/ZR ±7 天", 11, UI_MUTED);
            }
        }

        /* 指定日期规则与总闸状态指引面板（与保存/放弃按钮同高对齐） */
        UiRect sched_guide = {54, 520, 360, 56};
        fill_round_rect(pixels, stride, sched_guide, 12,
                        !draft->enabled ? UI_WARNING_SOFT : UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, sched_guide, 12, 1,
                          !draft->enabled ? UI_WARNING : UI_RGB(UI_BLENDED(border_control)));
        if (!draft->enabled) {
            draw_text(pixels, stride, sched_guide.x + 14, sched_guide.y + 22,
                      "⚠️ 管控总闸已切断 (休眠)", 13, UI_WARNING);
            draw_text(pixels, stride, sched_guide.x + 14, sched_guide.y + 42,
                      "规则当前休眠；按 [-] 键接通总闸生效", 12, UI_MUTED);
        } else {
            draw_text(pixels, stride, sched_guide.x + 14, sched_guide.y + 22,
                      "指定日期就寝独立优先级", 13, UI_RGB(UI_BLENDED(text_primary)));
            draw_text(pixels, stride, sched_guide.x + 14, sched_guide.y + 42,
                      "设定区间内优先于节假日与周计划执行", 12, UI_MUTED);
        }

        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(2, 4), "ZL  放弃",
            UI_PAGE, UI_INK, model->selected_index == 4 && !model->bedtime_section_focused, !model->bedtime_dirty);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(2, 5),
            bedtime_save_danger ? "+  保存（将立断）" : "+  保存",
            bedtime_save_danger ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT,
            model->selected_index == 5 && !model->bedtime_section_focused,
            !model->bedtime_dirty || model->disable_flag_present || model->waiting);
    }
}

static const char *bedtime_source_short(PtcBedtimeSource source)
{
    switch (source) {
    case PTC_BEDTIME_SOURCE_SCHEDULED_OVERRIDE: return "日期";
    case PTC_BEDTIME_SOURCE_STATUTORY_HOLIDAY: return "休假";
    case PTC_BEDTIME_SOURCE_MAKEUP_WORKDAY: return "调休";
    case PTC_BEDTIME_SOURCE_WEEKLY:
    default: return "每周";
    }
}

static void draw_time_plan_preview(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *WEEKDAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    UiRect panel = {824, 172, 402, 452};
    int index;
    fill_round_rect(pixels, stride, panel, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    draw_text(pixels, stride, panel.x + 16, panel.y + 26, "未来 7 天计划与就寝预测", 17, UI_INK);

    if (!model->forecast_available) {
        draw_text(pixels, stride, panel.x + 16, panel.y + 110, "刷新状态后显示预测数据", 15, UI_MUTED);
        return;
    }

    for (index = 0; index < (int)PTC_RESULT_FORECAST_DAYS; ++index) {
        const PtcResultForecastDay *day = &model->forecast[index];
        PtcEffectiveBedtime bedtime = ptc_bedtime_resolve_start_day(
            (const PtcRules *)&(PtcRules){.bedtime = model->bedtime_policy},
            day->day_index, ptc_weekday_from_day_index(day->day_index));
        uint8_t weekday = ptc_weekday_from_day_index(day->day_index);
        UiRect row = {panel.x + 10, panel.y + 38 + index * 51, panel.width - 20, 44};
        bool is_today = (index == 0);

        bool is_focused = (!model->parent_footer_focused && model->selected_index == index + 5);

        if (is_focused) {
            fill_round_rect(pixels, stride, row, 8, UI_ACCENT_SOFT);
            draw_focus_ring(pixels, stride, row, 8);
        } else if (is_today) {
            fill_round_rect(pixels, stride, row, 8, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, row, 8, 1, UI_ACCENT);
        } else {
            fill_round_rect(pixels, stride, row, 8, UI_RGB(UI_BLENDED(surface)));
            draw_rect_outline(pixels, stride, row, 8, 1, UI_RGB(UI_BLENDED(border_control)));
        }

        /* 1. 日期与星期 */
        char date_label[32];
        if (is_today) {
            snprintf(date_label, sizeof(date_label), "今天 %s", WEEKDAYS[weekday]);
        } else if (index == 1) {
            snprintf(date_label, sizeof(date_label), "明天 %s", WEEKDAYS[weekday]);
        } else {
            snprintf(date_label, sizeof(date_label), "%s (D+%d)", WEEKDAYS[weekday], index);
        }
        draw_text(pixels, stride, row.x + 10, row.y + 27, date_label, 14,
                  is_today ? UI_ACCENT : UI_INK);

        /* 2. 额度与微进度条 */
        int quota_x = row.x + 100;
        if (day->mode == PTC_RULE_MODE_UNLIMITED) {
            draw_text(pixels, stride, quota_x, row.y + 20, "不限时", 14, UI_SUCCESS);
            fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 28, 64, 4}, 2, UI_SUCCESS);
        } else {
            char q_str[24];
            snprintf(q_str, sizeof(q_str), "%u 分钟", (unsigned int)day->minutes);
            draw_text(pixels, stride, quota_x, row.y + 20, q_str, 14, UI_INK);
            int bar_w = 64;
            int fill_w = (int)((int64_t)bar_w * (day->minutes > 180 ? 180 : day->minutes) / 180);
            if (fill_w < 3 && day->minutes > 0) fill_w = 3;
            uint32_t bar_col = day->minutes <= 30 ? UI_WARNING : UI_ACCENT;
            fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 28, bar_w, 4}, 2, UI_BORDER);
            if (fill_w > 0) {
                fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 28, fill_w, 4}, 2, bar_col);
            }
        }

        /* 3. 规则来源胶囊徽标 */
        UiRect badge = {row.x + 178, row.y + 11, 56, 22};
        const char *badge_label = "周常规";
        uint32_t badge_bg = UI_RAISED;
        uint32_t badge_color = UI_MUTED;
        if (day->rule_source && strcmp(day->rule_source, "statutory_holiday") == 0) {
            badge_label = "节假日";
            badge_bg = UI_WARNING_SOFT;
            badge_color = UI_WARNING;
        } else if (day->rule_source && strcmp(day->rule_source, "makeup_workday") == 0) {
            badge_label = "调休";
            badge_bg = UI_ACCENT_SOFT;
            badge_color = UI_ACCENT;
        } else if (day->rule_source && strcmp(day->rule_source, "scheduled_override") == 0) {
            badge_label = "临时";
            badge_bg = UI_SUCCESS_SOFT;
            badge_color = UI_SUCCESS;
        } else if (day->rule_source && strcmp(day->rule_source, "today_override") == 0) {
            badge_label = "今日调";
            badge_bg = UI_WARNING_SOFT;
            badge_color = UI_WARNING;
        }
        fill_round_rect(pixels, stride, badge, 4, badge_bg);
        draw_text_center(pixels, stride, badge, badge_label, 11, badge_color);

        /* 4. 就寝窗口 */
        int bt_x = row.x + 244;
        if (!model->bedtime_policy.enabled || !bedtime.window.enabled) {
            draw_text(pixels, stride, bt_x + 12, row.y + 27, "无就寝", 13, UI_MUTED);
        } else {
            char bt_buf[48];
            snprintf(bt_buf, sizeof(bt_buf), "🌙 %02u:%02u %s",
                     (unsigned int)(bedtime.window.start_minute / 60),
                     (unsigned int)(bedtime.window.start_minute % 60),
                     bedtime_source_short(bedtime.source));
            draw_text(pixels, stride, bt_x, row.y + 27, bt_buf, 13, UI_MUTED);
        }

        /* 5. 右侧微指示符 */
        draw_text(pixels, stride, row.x + row.width - 16, row.y + 26, ">", 13,
                  is_focused ? UI_ACCENT : UI_MUTED);
    }

    draw_text(pixels, stride, panel.x + 14, panel.y + panel.height - 16,
              "按 A 键或点击查看单日规则生效逻辑", 11, UI_MUTED);
}

bool draw_parent_plan_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    if (model->parent_page != PTC_UI_PARENT_PLAN) return false;

    switch (model->plan_page) {
    case PTC_UI_PLAN_PAGE_WEEKLY:
        draw_weekly_page(pixels, stride, model);
        break;
    case PTC_UI_PLAN_PAGE_HOLIDAY:
        draw_holiday_page(pixels, stride, model);
        break;
    case PTC_UI_PLAN_PAGE_BEDTIME:
        draw_bedtime_page(pixels, stride, model);
        break;
    case PTC_UI_PLAN_PAGE_ROOT:
    default:
        draw_time_plan_preview(pixels, stride, model);
        break;
    }
    return true;
}
