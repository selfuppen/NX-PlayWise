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
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_WEEKLY_PLAN_HAS_NOT_BEEN_MODIFIED));
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
    snprintf(body, sizeof(body), ptc_ui_text(PTC_UI_T_PLEASE_CHECK_TODAY_S_FINAL_RULES_ESTIMATED));
    open_danger_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_WEEKLY,
        (before.source != after.source || ptc_ui_day_rule_effectively_changed(before.rule, after.rule))
            ? ptc_ui_text(PTC_UI_T_WEEKLY_PLANS_WILL_AFFECT_TODAY) : ptc_ui_text(PTC_UI_T_CONFIRM_TO_SAVE_WEEKLY_PLAN), body);
}

void save_holiday_from_page(UiState *ui)
{
    PtcEffectiveRule before;
    PtcEffectiveRule after;
    bool hold;
    if (!ui || ui->model.disable_flag_present) {
        if (ui) snprintf(ui->model.message, sizeof(ui->model.message),
                         ptc_ui_text(PTC_UI_T_EMERGENCY_DEACTIVATION_NATIONAL_HOLIDAY_SETTINGS_ARE_TEMPORARI));
        return;
    }
    if (!ui->model.holiday_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAY_SETTINGS_HAVE_NOT_BEEN_MODIFIED));
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
            ? ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAY_SETTING_WILL_AFFECT_TODAY) : ptc_ui_text(PTC_UI_T_CONFIRM_TO_SAVE_NATIONAL_HOLIDAY_SETTINGS),
        ptc_ui_text(PTC_UI_T_PLEASE_CHECK_TODAY_S_FINAL_RULES_ESTIMATED));
}

void save_scheduled_from_overlay(UiState *ui)
{
    if (!ui || ui->waiting || ui->model.disable_flag_present ||
        !ptc_ui_scheduled_dirty(&ui->model)) return;
    if (!ptc_scheduled_override_is_valid(&ui->model.draft_scheduled_override)) {
        snprintf(ui->model.message, sizeof(ui->model.message),
            ptc_ui_text(PTC_UI_T_THE_SPECIFIED_DATE_QUOTA_IS_INVALID_PLEASE));
        return;
    }
    if (ptc_ui_plan_save_requires_hold(&ui->model, PTC_UI_PLAN_SCHEDULED, (int64_t)time(NULL))) {
        open_danger_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_SCHEDULED,
            ptc_ui_text(PTC_UI_T_MAY_RESTRICT_USE_IMMEDIATELY_AFTER_SAVING),
            ptc_ui_text(PTC_UI_T_PLEASE_CHECK_TODAY_S_FINAL_AMOUNT_ESTIMATED));
        return;
    }
    submit_scheduled_override(ui);
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
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_HAS_RETURNED_TO_THE_MAIN_PAGE));
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
    ui->model.bedtime_dirty = ptc_ui_bedtime_section_dirty(&ui->model,
        ui->model.bedtime_section);
}

void discard_bedtime_draft(UiState *ui)
{
    if (!ui) return;
    ptc_ui_bedtime_discard_section(&ui->model, ui->model.bedtime_section);
    update_bedtime_dirty(ui);
    snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_MODIFICATION_OF_THE_CURRENT_BEDTIME_SUBPAGE));
}

static const char *bedtime_section_name(PtcUiBedtimeSection section)
{
    switch (section) {
    case PTC_UI_BEDTIME_WEEKLY: return ptc_ui_text(PTC_UI_T_WEEKLY_BEDTIME);
    case PTC_UI_BEDTIME_CALENDAR: return ptc_ui_text(PTC_UI_T_GOING_TO_BED_ON_HOLIDAYS);
    case PTC_UI_BEDTIME_SCHEDULED: return ptc_ui_text(PTC_UI_T_GO_TO_BED_ON_THE_SPECIFIED_DATE);
    default: return ptc_ui_text(PTC_UI_T_BEDTIME);
    }
}

void cancel_bedtime_navigation(UiState *ui)
{
    if (!ui) return;
    ui->pending_bedtime_section = -1;
    ui->model.bedtime_switch_pending = false;
    ui->pending_parent_page = -1;
    ui->pending_leave_parent = false;
}

void finish_bedtime_navigation(UiState *ui)
{
    int section;
    if (!ui) return;
    section = ui->pending_bedtime_section;
    ui->pending_bedtime_section = -1;
    ui->model.bedtime_switch_pending = false;
    if (section >= PTC_UI_BEDTIME_WEEKLY && section <= PTC_UI_BEDTIME_SCHEDULED) {
        ui->model.bedtime_section = (PtcUiBedtimeSection)section;
        ui->model.selected_index = 0;
        ui->model.bedtime_section_focused = false;
        ui->model.parent_footer_focused = false;
        update_bedtime_dirty(ui);
    } else if (ui->pending_parent_page >= 0 || ui->pending_leave_parent) {
        apply_pending_navigation(ui);
    }
}

void save_bedtime_from_page(UiState *ui)
{
    PtcBedtimePolicy draft;
    PtcUiBedtimeImpact impact;
    if (!ui || ui->waiting) return;
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_IN_EMERGENCY_DEACTIVATION_THE_BEDTIME_SETTING_IS));
        return;
    }
    if (!ui->model.bedtime_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_BEDTIME_HAS_NOT_BEEN_MODIFIED));
        return;
    }
    draft = ptc_ui_bedtime_section_policy(&ui->model, ui->model.bedtime_section);
    if (!ptc_bedtime_policy_is_valid(&draft)) {
        snprintf(ui->model.message, sizeof(ui->model.message),
            ptc_ui_text(PTC_UI_T_UNABLE_TO_SAVE_PLEASE_CHECK_FOR_OVERNIGHT));
        return;
    }
    if (draft.enabled && !ui->model.bedtime_official_setting_confirmed) {
        submit_bedtime_confirmation(ui);
        return;
    }
    {
        time_t raw_now = time(NULL);
        struct tm *tm_now = localtime(&raw_now);
        uint16_t minute_of_day = tm_now ? (uint16_t)(tm_now->tm_hour * 60 + tm_now->tm_min) : 0;
        impact = ptc_ui_bedtime_save_impact(&ui->model, minute_of_day, (int64_t)raw_now);
        if (impact == PTC_UI_BEDTIME_IMPACT_RESTRICT || impact == PTC_UI_BEDTIME_IMPACT_UNKNOWN) {
            open_danger_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_BEDTIME,
                impact == PTC_UI_BEDTIME_IMPACT_UNKNOWN ? ptc_ui_text(PTC_UI_T_POSSIBLY_ENTER_BEDTIME_RESTRICTION_IMMEDIATELY) : ptc_ui_text(PTC_UI_T_ENTER_BEDTIME_RESTRICTION_NOW),
                impact == PTC_UI_BEDTIME_IMPACT_UNKNOWN
                    ? ptc_ui_text(PTC_UI_T_THE_CURRENT_STATUS_NEEDS_TO_BE_CONFIRMED)
                    : ptc_ui_text(PTC_UI_T_SAVE_WILL_IMMEDIATELY_PAUSE_THE_GAME_AND));
            return;
        }
    }
    submit_bedtime_policy(ui);
}

void select_bedtime_section(UiState *ui, int section)
{
    if (!ui) return;
    ui->model.bedtime_master_focused = false;
    if (section < PTC_UI_BEDTIME_WEEKLY) section = PTC_UI_BEDTIME_SCHEDULED;
    if (section > PTC_UI_BEDTIME_SCHEDULED) section = PTC_UI_BEDTIME_WEEKLY;
    if (section == (int)ui->model.bedtime_section) return;
    update_bedtime_dirty(ui);
    if (ui->model.bedtime_dirty) {
        ui->pending_bedtime_section = section;
        ui->model.bedtime_switch_pending = true;
        ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_LEAVE;
        ui->model.overlay_selection = ui->model.disable_flag_present ? 2 : 0;
        snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_SAVE_S_DRAFT_BEFORE_SWITCHING),
            bedtime_section_name(ui->model.bedtime_section));
        snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
            ptc_ui_text(PTC_UI_T_SAVE_AND_SWITCH_ABANDON_AND_SWITCH_OR));
        return;
    }
    ui->model.bedtime_section = (PtcUiBedtimeSection)section;
    ui->model.selected_index = 0;
    ui->model.bedtime_section_focused = false;
    ui->model.parent_footer_focused = false;
    update_bedtime_dirty(ui);
}

void open_bedtime_window_editor(UiState *ui, int weekday)
{
    if (!ui || weekday < 0 || weekday >= 7) return;
    const char *WEEKDAY_NAMES[] = {ptc_ui_text(PTC_UI_T_MONDAY), ptc_ui_text(PTC_UI_T_TUESDAY), ptc_ui_text(PTC_UI_T_WEDNESDAY), ptc_ui_text(PTC_UI_T_THURSDAY), ptc_ui_text(PTC_UI_T_FRIDAY), ptc_ui_text(PTC_UI_T_SATURDAY), ptc_ui_text(PTC_UI_T_SUNDAY)};
    ui->model.bedtime_editor_day = weekday;
    ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_WINDOW;
    ui->model.overlay_selection = 0;
    ui->model.confirm_hold_required = false;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_S_BEDTIME_WINDOW), WEEKDAY_NAMES[weekday]);
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
        ptc_ui_text(PTC_UI_T_SET_THE_NO_PLAY_PERIOD_FROM_THAT));
}

void open_bedtime_special_editor(UiState *ui, int kind)
{
    if (!ui || kind < 0 || kind > 2) return;
    ui->model.bedtime_special_kind = kind;
    ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_SPECIAL;
    ui->model.overlay_selection = 0;
    ui->model.confirm_hold_required = false;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s",
        kind == 0 ? ptc_ui_text(PTC_UI_T_LEGAL_VACATION_BEDTIME_RULES) :
        (kind == 1 ? ptc_ui_text(PTC_UI_T_BEDTIME_RULES_FOR_REST_DAYS) : ptc_ui_text(PTC_UI_T_BEDTIME_RULES_ON_SPECIFIED_DAYS)));
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
        ptc_ui_text(PTC_UI_T_SELECT_INHERIT_CLOSE_OR_CUSTOM_CUSTOM_WINDOWS));
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
    title = target == PTC_UI_BEDTIME_TIME_START ? ptc_ui_text(PTC_UI_T_SET_BEDTIME_START_TIME) : ptc_ui_text(PTC_UI_T_SET_THE_END_TIME_OF_THE_NEXT);
    ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_BEDTIME_TIME, return_overlay,
        title, ptc_ui_text(PTC_UI_T_SELECT_HOURS_OR_MINUTES_ADJUST_THE_RIGHT), 4, 0, 1439,
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
    ui->model.bedtime_switch_pending = false;
    ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_LEAVE;
    ui->model.overlay_selection = ui->model.disable_flag_present ? 2 : 0;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_LEAVE_S_EDITING),
        bedtime_section_name(ui->model.bedtime_section));
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
        ui->model.disable_flag_present
            ? ptc_ui_text(PTC_UI_T_CANNOT_SAVE_DURING_EMERGENCY_SHUTDOWN_YOU_CAN)
            : ptc_ui_text(PTC_UI_T_PLEASE_CHOOSE_TO_SAVE_THE_CURRENT_SUBPAGE));
}

void request_parent_navigation(UiState *ui, int target_page, bool leave_parent)
{
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.plan_page == PTC_UI_PLAN_PAGE_DOCK) {
        dock_page_action(ui, 5, 0);
        return;
    }
    if (ui->model.parent_support_only && !leave_parent && target_page != PTC_UI_PARENT_SUPPORT) {
        enter_parent_area(ui);
        return;
    }
    if (ui->waiting) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_CURRENT_SETTINGS_ARE));
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
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_REFRESH_WEEKLY_PLAN));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
                     ui->model.disable_flag_present
                       ? ptc_ui_text(PTC_UI_T_CANNOT_SAVE_DURING_EMERGENCY_SHUTDOWN_CHOOSE_TO)
                       : ptc_ui_text(PTC_UI_T_REFRESH_WILL_READ_THE_SAVED_WEEKLY_PLAN));
        } else {
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_AWAY_WEEK_PLANS));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
                     ui->model.disable_flag_present
                       ? ptc_ui_text(PTC_UI_T_CANNOT_SAVE_DURING_EMERGENCY_SHUTDOWN_CHOOSE_TO_2)
                       : ptc_ui_text(PTC_UI_T_PLEASE_CHOOSE_TO_SAVE_CHANGES_ABANDON_CHANGES));
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
        ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY && ui->model.holiday_dirty) {
        ui->pending_parent_page = target_page;
        ui->pending_leave_parent = leave_parent;
        ui->model.overlay = PTC_UI_OVERLAY_HOLIDAY_LEAVE;
        ui->model.holiday_leave_selection = 1;
        snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_LEAVE_COUNTRY_HOLIDAY_SETTINGS));
        snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                 ptc_ui_text(PTC_UI_T_THERE_ARE_UNSAVED_CHANGES_HERE_CONTINUE_EDITING));
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
