#include "nro_app_internal.h"

void update_weekly_dirty(UiState *ui)
{
    ui->model.weekly_dirty = memcmp(
        ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week)) != 0;
}

void update_holiday_dirty(UiState *ui)
{
    ui->model.holiday_dirty = ui->model.draft_holiday_enabled != ui->model.holiday_enabled ||
        memcmp(&ui->model.draft_holiday_rule, &ui->model.holiday_rule, sizeof(PtcDayRule)) != 0 ||
        memcmp(&ui->model.draft_makeup_workday_rule, &ui->model.makeup_workday_rule, sizeof(PtcDayRule)) != 0;
}


void save_weekly_from_page(UiState *ui)
{
    PtcEffectiveRule before;
    PtcEffectiveRule after;
    char body[192];
    bool hold;
    if (weekly_editing_blocked(ui)) {
        return;
    }
    if (!ui->model.weekly_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), "周计划没有修改。");
        return;
    }
    hold = ptc_ui_plan_save_requires_hold(
        &ui->model, PTC_UI_PLAN_WEEKLY, (int64_t)time(NULL));
    if (!hold) {
        submit_weekly(ui);
        return;
    }
    before = ptc_ui_plan_rule(&ui->model, PTC_UI_PLAN_SAVED);
    after = ptc_ui_plan_rule(&ui->model, PTC_UI_PLAN_WEEKLY);
    snprintf(body, sizeof(body), "保存前请核对今天的最终规则、预计剩余时间和覆盖原因。");
    open_danger_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_WEEKLY,
        (before.source != after.source || ptc_ui_day_rule_effectively_changed(before.rule, after.rule))
            ? "周计划将影响今天" : "确认保存每周计划", body);
}

void save_holiday_from_page(UiState *ui)
{
    PtcEffectiveRule before;
    PtcEffectiveRule after;
    bool hold;
    if (!ui || ui->model.disable_flag_present) {
        if (ui) snprintf(ui->model.message, sizeof(ui->model.message),
                         "紧急停用中，国家节假日设置暂时只读。");
        return;
    }
    if (!ui->model.holiday_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), "国家节假日设置没有修改。");
        return;
    }
    hold = ptc_ui_plan_save_requires_hold(
        &ui->model, PTC_UI_PLAN_HOLIDAY, (int64_t)time(NULL));
    if (!hold) {
        submit_holiday_policy(ui);
        return;
    }
    before = ptc_ui_plan_rule(&ui->model, PTC_UI_PLAN_SAVED);
    after = ptc_ui_plan_rule(&ui->model, PTC_UI_PLAN_HOLIDAY);
    open_danger_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_HOLIDAY,
        (before.source != after.source || ptc_ui_day_rule_effectively_changed(before.rule, after.rule))
            ? "国家节假日设置将影响今天" : "确认保存国家节假日设置",
        "保存前请核对今天的最终规则、预计剩余时间和覆盖原因。");
}

void apply_pending_navigation(UiState *ui)
{
    if (ui->pending_leave_parent && ui->model.parent_page == PTC_UI_PARENT_PLAN &&
        ui->model.plan_page != PTC_UI_PLAN_PAGE_ROOT) {
        PtcUiPlanPage previous = ui->model.plan_page;
        ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
            ui->model.selected_index = previous == PTC_UI_PLAN_PAGE_WEEKLY ? 2 :
            (previous == PTC_UI_PLAN_PAGE_HOLIDAY ? 1 : 3);
        ui->model.parent_footer_focused = false;
        submit_status(ui);
    } else if (ui->pending_leave_parent) {
        ui->model.view = ui->model.setup_phase[0] && strcmp(ui->model.setup_phase, "active") != 0
            ? PTC_UI_SETUP : PTC_UI_CHILD;
        snprintf(ui->model.message, sizeof(ui->model.message), "已返回主页面。");
        if (ui->model.view == PTC_UI_CHILD) {
            submit_status(ui);
        }
    } else if (ui->pending_parent_page >= 0) {
        ui->model.parent_page = (PtcUiParentPage)ui->pending_parent_page;
        ui->model.selected_index = 0;
        if (ui->model.parent_page == PTC_UI_PARENT_PLAN) {
            ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
        }
        if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
            submit_status(ui);
        } else if (ui->model.parent_page == PTC_UI_PARENT_SETTINGS) {
            refresh_album_restriction(ui);
        }
    }
    ui->pending_parent_page = -1;
    ui->pending_leave_parent = false;
}

void refresh_recovery_state(UiState *ui)
{
    char path[192];
    if (!ui || !ui->client.storage) {
        return;
    }
    snprintf(path, sizeof(path), "%s/recovery/active/meta.json", APP_ROOT);
    ui->model.recovery_active = ui->client.storage->vtable->exists(ui->client.storage, path);
}

void discard_holiday_draft(UiState *ui)
{
    ui->model.draft_holiday_enabled = ui->model.holiday_enabled;
    ui->model.draft_holiday_rule = ui->model.holiday_rule;
    ui->model.draft_makeup_workday_rule = ui->model.makeup_workday_rule;
    update_holiday_dirty(ui);
}

void update_bedtime_dirty(UiState *ui)
{
    if (!ui) return;
    ui->model.bedtime_dirty = memcmp(&ui->model.draft_bedtime_policy,
        &ui->model.bedtime_policy, sizeof(PtcBedtimePolicy)) != 0;
}

void discard_bedtime_draft(UiState *ui)
{
    if (!ui) return;
    ui->model.draft_bedtime_policy = ui->model.bedtime_policy;
    update_bedtime_dirty(ui);
    snprintf(ui->model.message, sizeof(ui->model.message), "已放弃未保存的就寝时间修改。");
}

void save_bedtime_from_page(UiState *ui)
{
    PtcBedtimePolicy *draft;
    if (!ui || ui->waiting) return;
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，就寝时间设置暂时只读。");
        return;
    }
    if (!ui->model.bedtime_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), "就寝时间没有修改。");
        return;
    }
    draft = &ui->model.draft_bedtime_policy;
    if (!ptc_bedtime_policy_is_valid(draft)) {
        snprintf(ui->model.message, sizeof(ui->model.message),
            "无法保存：请检查跨夜窗口、相邻日期重叠、特殊规则冲突和日期长度。");
        return;
    }
    if (draft->enabled && !ui->model.bedtime_official_setting_confirmed) {
        submit_bedtime_confirmation(ui);
        return;
    }
    {
        time_t raw_now = time(NULL);
        struct tm *tm_now = localtime(&raw_now);
        uint16_t minute_of_day = tm_now ? (uint16_t)(tm_now->tm_hour * 60 + tm_now->tm_min) : 0;
        if (ptc_ui_bedtime_save_will_restrict(&ui->model, minute_of_day)) {
            open_danger_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_BEDTIME,
                "立即进入就寝限制？",
                "当前时间处于设定的就寝时段内。保存后将立即暂停游戏并限制游玩（立断）。\n请长按 A 或持续按住确认按钮 1 秒。");
            return;
        }
    }
    submit_bedtime_policy(ui);
}

void select_bedtime_section(UiState *ui, int section)
{
    if (!ui) return;
    if (section < PTC_UI_BEDTIME_WEEKLY) section = PTC_UI_BEDTIME_SCHEDULED;
    if (section > PTC_UI_BEDTIME_SCHEDULED) section = PTC_UI_BEDTIME_WEEKLY;
    ui->model.bedtime_section = (PtcUiBedtimeSection)section;
    ui->model.selected_index = 0;
    ui->model.bedtime_section_focused = false;
    ui->model.parent_footer_focused = false;
}

void open_bedtime_window_editor(UiState *ui, int weekday)
{
    if (!ui || weekday < 0 || weekday >= 7) return;
    static const char *WEEKDAY_NAMES[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    ui->model.bedtime_editor_day = weekday;
    ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_WINDOW;
    ui->model.overlay_selection = 0;
    ui->model.confirm_hold_required = false;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s就寝时间窗口", WEEKDAY_NAMES[weekday]);
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
        "设定当晚至次日清晨的禁玩时段（必须跨越午夜）；到达开始时间立即强制暂停软件。");
}

void open_bedtime_special_editor(UiState *ui, int kind)
{
    if (!ui || kind < 0 || kind > 2) return;
    ui->model.bedtime_special_kind = kind;
    ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_SPECIAL;
    ui->model.overlay_selection = 0;
    ui->model.confirm_hold_required = false;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s",
        kind == 0 ? "法定休假就寝规则" :
        (kind == 1 ? "调休工作日就寝规则" : "指定日期就寝规则"));
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
        "选择继承、关闭或自定义；自定义窗口必须跨越午夜且不能与相邻窗口重叠。");
}

void open_bedtime_time_editor(UiState *ui, PtcUiBedtimeTimeTarget target)
{
    PtcUiOverlay return_overlay;
    const PtcBedtimeWindow *window;
    const char *title;
    if (!ui || (target != PTC_UI_BEDTIME_TIME_START && target != PTC_UI_BEDTIME_TIME_END)) return;
    return_overlay = ui->model.overlay;
    if (return_overlay == PTC_UI_OVERLAY_BEDTIME_WINDOW) {
        window = &ui->model.draft_bedtime_policy.week[ui->model.bedtime_editor_day];
    } else if (return_overlay == PTC_UI_OVERLAY_BEDTIME) {
        window = &ui->model.draft_bedtime_policy.week[0];
    } else if (return_overlay == PTC_UI_OVERLAY_BEDTIME_SPECIAL) {
        const PtcBedtimeSpecialRule *rule = ui->model.bedtime_special_kind == 0
            ? &ui->model.draft_bedtime_policy.holiday_rule
            : (ui->model.bedtime_special_kind == 1
                ? &ui->model.draft_bedtime_policy.makeup_workday_rule
                : &ui->model.draft_bedtime_policy.scheduled_override.rule);
        window = &rule->window;
    } else {
        return;
    }
    ui->model.bedtime_editor_time_target = target;
    title = target == PTC_UI_BEDTIME_TIME_START ? "设置就寝开始时间" : "设置次日结束时间";
    ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_BEDTIME_TIME, return_overlay,
        title, "选择小时或分钟；右摇杆上下调整，长推加速", 4, 0, 1439,
        target == PTC_UI_BEDTIME_TIME_START ? window->start_minute : window->end_minute);
}

void request_bedtime_leave(UiState *ui, int target_page, bool leave_parent)
{
    if (!ui) return;
    if (!ui->model.bedtime_dirty) {
        request_parent_navigation(ui, target_page, leave_parent);
        return;
    }
    ui->pending_parent_page = target_page;
    ui->pending_leave_parent = leave_parent;
    ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_LEAVE;
    ui->model.overlay_selection = ui->model.disable_flag_present ? 2 : 0;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "离开就寝时间编辑？");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
        ui->model.disable_flag_present
            ? "紧急停用期间不能保存；可继续编辑或放弃草稿后离开。"
            : "请选择保存并离开、放弃修改，或继续编辑。");
}

void request_parent_navigation(UiState *ui, int target_page, bool leave_parent)
{
    if (ui->waiting) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "请等待当前设置保存完成后再离开页面。");
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_SUPPORT &&
        (leave_parent || target_page != PTC_UI_PARENT_SUPPORT)) {
        ui->model.diagnostic_status = PTC_UI_DIAGNOSTIC_IDLE;
        ui->model.diagnostic_path[0] = '\0';
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
        ui->model.plan_page == PTC_UI_PLAN_PAGE_WEEKLY && ui->model.weekly_dirty) {
        ui->pending_parent_page = target_page;
        ui->pending_leave_parent = leave_parent;
        ui->model.overlay = PTC_UI_OVERLAY_WEEKLY_LEAVE;
        refresh_disable_flag(ui);
        ui->model.weekly_leave_selection = ui->model.disable_flag_present ? 2 : 1;
        if (!leave_parent && target_page == PTC_UI_PARENT_PLAN) {
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "刷新周计划？");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
                     ui->model.disable_flag_present
                       ? "紧急停用期间不能保存；请选择保留草稿并刷新、放弃草稿并刷新，或返回。"
                       : "刷新会读取已保存的周计划；请选择先保存、放弃草稿并刷新，或返回编辑。");
        } else {
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "离开周计划？");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
                     ui->model.disable_flag_present
                       ? "紧急停用期间不能保存；请选择保留草稿并离开、放弃草稿并离开，或返回。"
                       : "请选择保存修改、放弃修改，或返回继续编辑。");
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
        ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY && ui->model.holiday_dirty) {
        ui->pending_parent_page = target_page;
        ui->pending_leave_parent = leave_parent;
        ui->model.overlay = PTC_UI_OVERLAY_HOLIDAY_LEAVE;
        ui->model.holiday_leave_selection = 1;
        snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "离开国家节假日设置？");
        snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                 "这里还有尚未保存的更改。继续编辑可以保留内容；放弃更改后再离开。");
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
        ui->model.plan_page == PTC_UI_PLAN_PAGE_BEDTIME && ui->model.bedtime_dirty) {
        request_bedtime_leave(ui, target_page, leave_parent);
        return;
    }
    if (leave_parent && ui->model.parent_page == PTC_UI_PARENT_PLAN &&
        ui->model.plan_page != PTC_UI_PLAN_PAGE_ROOT) {
        PtcUiPlanPage previous = ui->model.plan_page;
        ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
        ui->model.selected_index = previous == PTC_UI_PLAN_PAGE_WEEKLY ? 2 :
            (previous == PTC_UI_PLAN_PAGE_HOLIDAY ? 1 : 3);
        ui->model.parent_footer_focused = false;
        return;
    }
    ui->pending_parent_page = target_page;
    ui->pending_leave_parent = leave_parent;
    apply_pending_navigation(ui);
}
