#include "nro_app_internal.h"

static void format_today_label(uint16_t day_index, char *out, size_t out_size)
{
    const char *WEEKDAYS[] = {ptc_ui_text(PTC_UI_T_SUNDAY), ptc_ui_text(PTC_UI_T_MONDAY), ptc_ui_text(PTC_UI_T_TUESDAY), ptc_ui_text(PTC_UI_T_WEDNESDAY), ptc_ui_text(PTC_UI_T_THURSDAY), ptc_ui_text(PTC_UI_T_FRIDAY), ptc_ui_text(PTC_UI_T_SATURDAY)};
    uint16_t year;
    uint8_t month;
    uint8_t day;
    if (ptc_date_from_day_index(day_index, &year, &month, &day)) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_U_U_S_TODAY), month, day, WEEKDAYS[ptc_weekday_from_day_index(day_index)]);
    } else {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_TODAY_6));
    }
}

static PtcDayRule effective_today_rule(const UiState *ui)
{
    uint8_t weekday = ptc_weekday_from_day_index(ui->model.day_index);
    if (ui->model.today_override_present) {
        return ui->model.today_override_rule;
    }
    if (strcmp(ui->model.rule_source, "statutory_holiday") == 0) {
        return ui->model.holiday_rule;
    }
    if (strcmp(ui->model.rule_source, "makeup_workday") == 0) {
        return ui->model.makeup_workday_rule;
    }
    return ui->model.current_week[weekday];
}

static uint16_t current_today_limit_value(const UiState *ui)
{
    PtcDayRule rule = effective_today_rule(ui);
    uint16_t fallback = 0;
    if (ui->model.unrestricted_today != 1 && rule.mode == PTC_RULE_MODE_LIMIT &&
        rule.minutes >= 1 && rule.minutes <= 1440) {
        fallback = rule.minutes;
    }
    return ptc_ui_today_limit_start_value(&ui->model, fallback);
}

void request_clear_bedtime_skip(UiState *ui)
{
    char body[256];
    bool current;
    if (ui->model.disable_flag_present || ui->waiting) return;
    if (!ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL)) ||
        !ptc_ui_bedtime_skip_matches_policy(&ui->model, &ui->model.bedtime_policy)) {
        snprintf(ui->model.message, sizeof(ui->model.message),
            ptc_ui_text(PTC_UI_T_THERE_IS_NO_RECOVERABLE_SKIP_FOR_THIS));
        return;
    }
    current = ui->model.bedtime_active && ui->model.bedtime_skipped &&
        ui->model.bedtime_window_instance_id == ui->model.bedtime_skipped_window_instance_id;
    ui->model.pending_bedtime_skip_instance_id = ui->model.bedtime_skipped_window_instance_id;
    ui->auth_retry_action = AUTH_RETRY_CLEAR_BEDTIME_SKIP;
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THIS_APPLICATION_PIN_AGAIN_BEFORE))) return;
    snprintf(body, sizeof(body),
        current ? ptc_ui_text(PTC_UI_T_IS_STILL_WITHIN_THE_SKIPPED_BEDTIME_WINDOW) :
                  ptc_ui_text(PTC_UI_T_WILL_CLEAR_THIS_BEDTIME_SKIP_LIMIT_USAGE));
    if (current)
        open_danger_confirm_overlay(ui, PTC_UI_OPERATION_CLEAR_BEDTIME_SKIP,
            ptc_ui_text(PTC_UI_T_RESTORE_THIS_BEDTIME_RESTRICTION_NOW), body);
    else
        open_confirm_overlay(ui, PTC_UI_OPERATION_CLEAR_BEDTIME_SKIP,
            ptc_ui_text(PTC_UI_T_RESTORE_BEDTIME_LIMIT), body);
}


void handle_today_action_ready(UiState *ui, int index)
{
    char date[64];
    char body[320];
    char played[32];
    char remaining[32];
    const char *unavailable = ptc_ui_today_action_unavailable_reason(
        &ui->model, index == PTC_UI_OPERATION_ADD_TODAY_MINUTES ? 1 :
        (index == PTC_UI_OPERATION_SKIP_BEDTIME ? 4 :
         (index == PTC_UI_OPERATION_SKIP_EYE_CARE ? 6 :
          (index == PTC_UI_OPERATION_WAIVE_DOCK ? 7 : -1))), (int64_t)time(NULL));
    if (unavailable) {
        snprintf(ui->model.message, sizeof(ui->model.message), "%s", unavailable);
        return;
    }
    format_today_label(ui->model.day_index, date, sizeof(date));
    if (ui->model.played_minutes_available) snprintf(played, sizeof(played), ptc_ui_text(PTC_UI_T_ABOUT_D_MIN), ui->model.played_minutes);
    else snprintf(played, sizeof(played), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    if (ui->model.unrestricted_today == 1) snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    else if (ui->model.remaining_available) snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_D_MIN), ui->model.remaining_minutes);
    else snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    switch (index) {
    case PTC_UI_OPERATION_SET_TODAY_LIMIT:
        ui->model.today_limit_unlimited_draft = ui->model.unrestricted_today == 1;
        ui->model.operation = PTC_UI_OPERATION_SET_TODAY_LIMIT;
        ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_MINUTES, PTC_UI_OVERLAY_NONE,
            ptc_ui_text(PTC_UI_T_SET_TODAY_S_TOTAL_QUOTA),
            !ui->model.played_minutes_available || ui->model.played_minutes < 0
                ? ptc_ui_text(PTC_UI_T_THE_TOTAL_QUOTA_FOR_THE_WHOLE_DAY)
                : (ui->model.unrestricted_today == 1
                    ? ptc_ui_text(PTC_UI_T_THE_TOTAL_QUOTA_FOR_THE_WHOLE_DAY_2)
                    : ptc_ui_text(PTC_UI_T_THE_TOTAL_QUOTA_FOR_THE_WHOLE_DAY_3)),
            4, 1, 1440, current_today_limit_value(ui));
        if (!ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL)) ||
            !ui->model.played_minutes_available || ui->model.played_minutes < 0 ||
            (!ui->model.remaining_available && ui->model.unrestricted_today != 1))
            refresh_today_limit_editor(ui, false);
        break;
    case PTC_UI_OPERATION_ADD_TODAY_MINUTES:
        if (ui->model.unrestricted_today == 1) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_TODAY_IS_ALREADY_UNLIMITED_USE_SET_TODAY));
        } else {
            ui->model.operation = PTC_UI_OPERATION_ADD_TODAY_MINUTES;
            ui->model.draft_minutes = 15;
            ui->model.overlay = PTC_UI_OVERLAY_QUICK_ADD;
            ui->model.overlay_selection = 0;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_QUICK_GRANT));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                ptc_ui_text(PTC_UI_T_SELECT_A_COMMONLY_USED_DURATION_OR_CUSTOMIZE));
        }
        break;
    case PTC_UI_OPERATION_DISABLE_TODAY_LIMIT:
        snprintf(body, sizeof(body), ptc_ui_text(PTC_UI_T_S_ESTIMATED_USAGE_S_S_LEFT_TODAY),
                 date, played, remaining);
        open_confirm_overlay(ui, PTC_UI_OPERATION_DISABLE_TODAY_LIMIT, ptc_ui_text(PTC_UI_T_MAKE_TODAY_UNLIMITED), body);
        break;
    case PTC_UI_OPERATION_RESTORE_TODAY_POLICY: {
        PtcEffectiveRule restored = ptc_ui_rule_after_today_restore(&ui->model);
        char basis[256];
        ptc_ui_format_restore_today_basis(&ui->model, basis, sizeof(basis));
        snprintf(body, sizeof(body), "%s\n%s", date, basis);
        bool requires_hold = restored.rule.mode == PTC_RULE_MODE_LIMIT &&
            (!ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL)) ||
             !ui->model.played_minutes_available || ui->model.played_minutes < 0 ||
             (int)restored.rule.minutes - ui->model.played_minutes <= 0);
        if (requires_hold)
            open_danger_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_TODAY_POLICY, ptc_ui_text(PTC_UI_T_CLEAR_TODAY_S_QUOTA_ADJUSTMENT), body);
        else
            open_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_TODAY_POLICY, ptc_ui_text(PTC_UI_T_CLEAR_TODAY_S_QUOTA_ADJUSTMENT), body);
        break;
    }
    case PTC_UI_OPERATION_SKIP_BEDTIME: {
        uint64_t instance_id = 0;
        uint16_t start_day = 0, start_minute = 0, end_minute = 0;
        char start_date[64];
        if (ui->model.disable_flag_present || ui->waiting) break;
        if (!ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL))) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                ptc_ui_text(PTC_UI_T_THE_BEDTIME_STATUS_IS_STILL_TO_BE));
            break;
        }
        if (ui->model.bedtime_active) {
            if (!ui->model.bedtime_skipped) {
                instance_id = ui->model.bedtime_window_instance_id;
                start_day = ui->model.bedtime_start_day_index;
                start_minute = ui->model.bedtime_start_minute;
                end_minute = ui->model.bedtime_end_minute;
            }
        } else if (ui->model.bedtime_next_available) {
            instance_id = ui->model.bedtime_next_window_instance_id;
            start_day = ui->model.bedtime_next_start_day_index;
            start_minute = ui->model.bedtime_next_start_minute;
            end_minute = ui->model.bedtime_next_end_minute;
        }
        if (instance_id == 0) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                ui->model.bedtime_skipped_window_available
                    ? ptc_ui_text(PTC_UI_T_THE_MOST_RECENT_BEDTIME_WINDOW_HAS_BEEN) : ptc_ui_text(PTC_UI_T_THERE_ARE_CURRENTLY_NO_SKIPPABLE_BEDTIME_WINDOWS));
            break;
        }
        ui->model.pending_bedtime_skip_instance_id = instance_id;
        ui->model.pending_bedtime_skip_start_day_index = start_day;
        ui->model.pending_bedtime_skip_start_minute = start_minute;
        ui->model.pending_bedtime_skip_end_minute = end_minute;
        ui->auth_retry_action = AUTH_RETRY_SKIP_BEDTIME;
        if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_PLEASE_ENTER_YOUR_APP_PIN_AGAIN_BEFORE))) break;
        format_today_label(start_day, start_date, sizeof(start_date));
        snprintf(body, sizeof(body), ptc_ui_text(PTC_UI_T_S_02U_02U_TO_NEXT_DAY_02U),
            start_date, (unsigned int)(start_minute / 60), (unsigned int)(start_minute % 60),
            (unsigned int)(end_minute / 60), (unsigned int)(end_minute % 60));
        open_confirm_overlay(ui, PTC_UI_OPERATION_SKIP_BEDTIME, ptc_ui_text(PTC_UI_T_SKIP_BEDTIME_THIS_TIME), body);
        break;
    }
    case PTC_UI_OPERATION_WAIVE_DOCK:
        request_dock_waiver(ui);
        break;
    case PTC_UI_OPERATION_SKIP_EYE_CARE:
        ui->model.pending_eye_care_break_id = ui->model.eye_care_break_id;
        ui->auth_retry_action = AUTH_RETRY_SKIP_EYE_CARE;
        if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_VERIFY_PIN))) break;
        open_confirm_overlay(ui, PTC_UI_OPERATION_SKIP_EYE_CARE,
            ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP),
            ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_CONFIRM_BODY));
        break;
    default:
        break;
    }
}

void handle_parent_action(UiState *ui)
{
    int index = ui->model.selected_index;
    if (ui->model.parent_support_only && !ptc_ui_parent_read_only_action(&ui->model, index)) {
        enter_parent_area(ui);
        return;
    }
    if (ptc_ui_open_today_settings(&ui->model, index)) return;
    if (ui->model.parent_page == PTC_UI_PARENT_TODAY && index == 11) {
        if (ui->waiting) return;
        ptc_audio_play(PTC_SE_POPUP);
        ptc_ui_open_home_details_page(&ui->model, 1);
        return;
    }
    if (ui->model.disable_flag_present && ui->model.parent_page == PTC_UI_PARENT_TODAY && index != 6 && index != 7) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 ptc_ui_text(PTC_UI_T_EMERGENCY_DEACTIVATION_IS_ENABLED_THIS_CONTROL_WRITE));
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        if (ui->waiting) return;
        const char *unavailable = ptc_ui_today_action_unavailable_reason(
            &ui->model, index, (int64_t)time(NULL));
        if (unavailable) {
            snprintf(ui->model.message, sizeof(ui->model.message), "%s", unavailable);
            return;
        }
        if (index == 3 && ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL)) &&
            !ui->model.today_override_present) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_THERE_ARE_NO_CREDIT_ADJUSTMENTS_TO_CLEAR));
            return;
        }
        if (index == 0) {
            handle_today_action_ready(ui, PTC_UI_OPERATION_SET_TODAY_LIMIT);
        } else if (ptc_ui_today_operation(index) != PTC_UI_OPERATION_NONE) {
            submit_status(ui);
            /* A failed submit must not leave an action for a later auto-refresh. */
            ui->pending_today_action = ui->waiting ? (int)ptc_ui_today_operation(index) : -1;
            if (ui->waiting)
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_IS_REFRESHING_THE_CREDIT_CONSUMPTION_ESTIMATE_AND));
        } else if (index == 5) {
            if (ui->model.daily_buffer_minutes == 0) {
                snprintf(ui->model.message, sizeof(ui->model.message),
                    ptc_ui_text(PTC_UI_T_AUTONOMOUS_BUFFERING_IS_CURRENTLY_CLOSED_IT_CAN));
            } else if (ui->model.daily_buffer_claimed) {
                snprintf(ui->model.message, sizeof(ui->model.message),
                    ptc_ui_text(PTC_UI_T_THE_CHILD_HAS_RECEIVED_INDEPENDENT_BUFFER_TODAY));
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message),
                    ptc_ui_text(PTC_UI_T_THE_AUTONOMOUS_BUFFER_IS_READ_ONLY_THE));
            }
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.plan_page == PTC_UI_PLAN_PAGE_ROOT) {
        switch (index) {
        case 0:
            if (!ptc_ui_scheduled_dirty(&ui->model))
                ui->model.draft_scheduled_override = ui->model.scheduled_override;
            if (!ptc_ui_scheduled_dirty(&ui->model) && !ui->model.draft_scheduled_override.enabled) {
                ui->model.draft_scheduled_override.start_day_index = ui->model.day_index;
                ui->model.draft_scheduled_override.end_day_index = ui->model.day_index;
                ui->model.draft_scheduled_override.rule.mode = PTC_RULE_MODE_LIMIT;
                ui->model.draft_scheduled_override.rule.minutes = 60;
            }
            ui->model.overlay = PTC_UI_OVERLAY_SCHEDULED;
            ui->model.overlay_selection = 0;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_SPECIFIED_DATE_QUOTA));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                ptc_ui_text(PTC_UI_T_REPLACES_THE_TOTAL_DAILY_QUOTA_WITHIN_THE));
            break;
        case 1:
            ui->model.plan_page = PTC_UI_PLAN_PAGE_HOLIDAY;
            ui->model.selected_index = 0;
            break;
        case 2:
            open_weekly_page(ui);
            break;
        case 3:
            ui->model.draft_bedtime_policy = ui->model.bedtime_policy;
            ui->model.bedtime_dirty = false;
            ui->model.bedtime_switch_pending = false;
            ui->model.bedtime_section = PTC_UI_BEDTIME_WEEKLY;
            ui->model.plan_page = PTC_UI_PLAN_PAGE_BEDTIME;
            ui->model.selected_index = 0;
            break;
        case 4:
            ui->model.draft_eye_care_policy = ui->model.eye_care_policy;
            ui->model.eye_care_dirty = false;
            ui->model.eye_care_field_focus = 0;
            ui->model.plan_page = PTC_UI_PLAN_PAGE_EYE_CARE;
            ui->model.selected_index = 0;
            break;
        case 6:
            ui->model.draft_autonomy_policy = ui->model.autonomy_policy;
            ui->model.overlay = PTC_UI_OVERLAY_AUTONOMY;
            ui->model.overlay_selection = ui->model.draft_autonomy_policy.daily_buffer_minutes / 5;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_TODAY_S_INDEPENDENT_BUFFERING));
            ui->model.overlay_body[0] = '\0';
            break;
        case 5:
            if (!ui->model.dock_dirty) ui->model.draft_dock_policy = ui->model.dock_policy;
            ui->model.dock_field_focus = 0;
            ui->model.plan_page = PTC_UI_PLAN_PAGE_DOCK;
            ui->model.parent_footer_focused = false;
            break;
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
            if (ui->model.forecast_available) {
                ui->model.forecast_detail_day_offset = index - 7;
                ui->model.overlay = PTC_UI_OVERLAY_DAY_DECISION;
                ui->model.overlay_selection = index - 7;
            }
            break;
        default:
            break;
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
        if (ui->model.disable_flag_present && index != 4 && index != 6 && index != 7) {
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_EMERGENCY_DEACTIVATION_NATIONAL_HOLIDAY_SETTINGS_ARE_TEMPORARI));
            return;
        }
        switch (index) {
        case 0:
            ui->model.draft_holiday_enabled = !ui->model.draft_holiday_enabled;
            update_holiday_dirty(ui);
            break;
        case 1:
            ui->model.holiday_last_rule = 0;
            if (ui->model.draft_holiday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_CURRENT_MODE_IS_UNLIMITED_PLEASE_SWITCH));
            } else {
                ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_HOLIDAY_MINUTES, PTC_UI_OVERLAY_NONE,
                    ptc_ui_text(PTC_UI_T_SET_LEGAL_HOLIDAY_QUOTA), ptc_ui_text(PTC_UI_T_ENTER_HOURS_AND_MINUTES_SEPARATELY_TOTALING_1), 4, 1, 1440, ui->model.draft_holiday_rule.minutes);
            }
            break;
        case 2:
            ui->model.holiday_last_rule = 1;
            if (ui->model.draft_makeup_workday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_CURRENT_MODE_IS_UNLIMITED_PLEASE_SWITCH));
            } else {
                ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_MAKEUP_MINUTES, PTC_UI_OVERLAY_NONE,
                    ptc_ui_text(PTC_UI_T_SET_THE_AMOUNT_OF_REST_WORK_DAYS), ptc_ui_text(PTC_UI_T_ENTER_HOURS_AND_MINUTES_SEPARATELY_TOTALING_1), 4, 1, 1440,
                    ui->model.draft_makeup_workday_rule.minutes);
            }
            break;
        case 3: {
            PtcDayRule *rule = ui->model.holiday_last_rule == 1
                ? &ui->model.draft_makeup_workday_rule : &ui->model.draft_holiday_rule;
            rule->mode = ptc_ui_next_rule_mode(rule->mode);
            update_holiday_dirty(ui);
            break;
        }
        case 4:
            discard_holiday_draft(ui);
            break;
        case 6: {
            char holiday_rule[48];
            char makeup_rule[48];
            if (ui->model.draft_holiday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(holiday_rule, sizeof(holiday_rule), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
            } else {
                snprintf(holiday_rule, sizeof(holiday_rule), ptc_ui_text(PTC_UI_T_U_MIN),
                         (unsigned int)ui->model.draft_holiday_rule.minutes);
            }
            if (ui->model.draft_makeup_workday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(makeup_rule, sizeof(makeup_rule), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
            } else {
                snprintf(makeup_rule, sizeof(makeup_rule), ptc_ui_text(PTC_UI_T_U_MIN),
                         (unsigned int)ui->model.draft_makeup_workday_rule.minutes);
            }
            open_holiday_calendar_view(ui);
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                     ptc_ui_text(PTC_UI_T_CURRENT_S_STATUTORY_HOLIDAY_S_MAKEUP_WORKDAY),
                     ui->model.draft_holiday_enabled ? ptc_ui_text(PTC_UI_T_ENABLED) : ptc_ui_text(PTC_UI_T_NOT_ENABLED),
                     holiday_rule, makeup_rule);
            break;
        }
        case 7:
            open_calendar_manager(ui);
            break;
        case 5:
            save_holiday_from_page(ui);
            break;
        default:
            break;
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_GRANT) {
        switch (index) {
        case 0: open_local_grant(ui); break;
        case 1: show_pairing_qr(ui); break;
        case 2: open_grant_manager(ui); break;
        case 3: open_redemption_history(ui); break;
        default: break;
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_SETTINGS) {
        switch (index) {
        case 0: change_parent_pin(ui); break;
        case 1:
            ui->model.overlay = PTC_UI_OVERLAY_THEME;
            ui->model.overlay_selection = (int)ui->theme_preference;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_APPEARANCE_THEME));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                     ptc_ui_text(PTC_UI_T_ONLY_CHANGES_THE_DRAWING_APPEARANCE_OF_THE));
            break;
        case 2: open_shortcut_manager(ui); break;
        case 3:
            ui->model.overlay = PTC_UI_OVERLAY_LANGUAGE;
            ui->model.overlay_selection = (int)ui->language_preference;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_UI_LANGUAGE));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                ptc_ui_text(PTC_UI_T_THE_HOST_APPLICATION_SHARES_THIS_SETTING_WITH));
            break;
        case 4:
            refresh_album_restriction(ui);
            ui->model.overlay = PTC_UI_OVERLAY_ALBUM_MANAGER;
            ui->model.overlay_selection = ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_OFF ? 0 : 1;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_ADVANCED_ENTRY_TO_HOMEBREW_MENU));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                     ptc_ui_text(PTC_UI_T_THIS_FUNCTION_ONLY_CHANGES_THE_HBMENU_STARTUP));
            break;
        case 5: {
            bool new_state = !ptc_audio_is_enabled();
            ptc_audio_set_enabled(new_state);
            if (new_state) {
                ptc_audio_play(PTC_SE_CONFIRM);
            }
            save_ui_preferences(ui);
            snprintf(ui->model.message, sizeof(ui->model.message),
                     new_state ? ptc_ui_text(PTC_UI_T_BUTTON_AND_INTERACTIVE_SOUND_EFFECTS_HAVE_BEEN) : ptc_ui_text(PTC_UI_T_KEYSTROKES_AND_INTERACTIVE_SOUND_EFFECTS_HAVE_BEEN));
            break;
        }
        case 7: open_config_backup(ui); break;
        case 6:
            open_activity_history(ui);
            break;
        default: break;
        }
        return;
    }
    if (ui->model.parent_page != PTC_UI_PARENT_SUPPORT) return;
    if (ptc_ui_safety_action_available(&ui->model, index) == PTC_UI_ACTION_DISABLED) {
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 ptc_ui_safety_action_hint(&ui->model, index));
        return;
    }
    switch (index) {
    case 0:
        if (ptc_ui_runtime_fingerprint_reconfirmation_needed(&ui->model)) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, ptc_ui_text(PTC_UI_T_THE_SYSTEM_ENVIRONMENT_HAS_CHANGED_PLEASE_RECHECK),
                                 ptc_ui_text(PTC_UI_T_THE_SYSTEM_VERSION_OR_OPERATING_ENVIRONMENT_IS));
        } else if (ui->model.disable_flag_present) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, ptc_ui_text(PTC_UI_T_RE_ENABLE_CONTROLS),
                                 ptc_ui_text(PTC_UI_T_RECHECK_SYSTEM_COMPATIBILITY_ONLY_AFTER_PASSING_IT));
        } else {
            open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, ptc_ui_text(PTC_UI_T_CONFIRM_ENABLE_CONTROLS),
                                 ptc_ui_text(PTC_UI_T_FIRST_CHECK_SYSTEM_COMPATIBILITY_AFTER_PASSING_SAVE));
        }
        break;
    case 1:
        open_confirm_overlay(ui, PTC_UI_OPERATION_RETRY_SETUP_RELEASE, ptc_ui_text(PTC_UI_T_RETRY_REPAIR),
                             ptc_ui_text(PTC_UI_T_RERUN_THE_SECURITY_CHECK_AND_CONTINUE_FIRST));
        break;
    case 2:
        refresh_disable_flag(ui);
        if (ui->model.disable_flag_present) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, ptc_ui_text(PTC_UI_T_RE_ENABLE_CONTROLS),
                                 ptc_ui_text(PTC_UI_T_RERUNS_SAFETY_CHECKS_THEN_RESUMES_QUOTA_CONTROL));
        } else {
            open_confirm_overlay(ui, PTC_UI_OPERATION_EMERGENCY_DISABLE, ptc_ui_text(PTC_UI_T_EMERGENCY_DEACTIVATION_CONTROL),
                                 ptc_ui_text(PTC_UI_T_CREATES_DISABLE_FLAG_AND_STOPS_NORMAL_CONTROL));
        }
        break;
    case 3:
        open_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT, ptc_ui_text(PTC_UI_T_RESTORE_ORIGINAL_STATE),
                             ptc_ui_text(PTC_UI_T_RESTORES_ORIGINAL_PARENTAL_CONTROL_SETTINGS_AND_TIMERS));
        break;
    case 4:
        open_confirm_overlay(ui, PTC_UI_OPERATION_EXPORT_DIAGNOSTICS, ptc_ui_text(PTC_UI_T_EXPORT_DIAGNOSTIC_PACKAGE),
            ptc_ui_text(PTC_UI_T_DIAGNOSTICS_MAY_CONTAIN_DEVICE_IDS_AND_FILE));
        break;
    case 5:
#ifndef PLAYWISE_EDEN
        ptc_hot_reload_inspect(&ui->hot_reload);
        sync_hot_reload_model(ui);
#endif
        ui->model.overlay = PTC_UI_OVERLAY_SOFTWARE_INFO;
        snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_SOFTWARE_INFO));
        ui->model.overlay_body[0] = '\0';
        snprintf(ui->model.software_version, sizeof(ui->model.software_version), "%s", PLAYWISE_VERSION);
        snprintf(ui->model.repository_url, sizeof(ui->model.repository_url), "%s", PLAYWISE_REPOSITORY_URL);
        snprintf(ui->model.pwa_url, sizeof(ui->model.pwa_url), "%s", PTC_PAIRING_BASE_URL);
        break;
    default:
        break;
    }
}

bool quota_operation_needs_recheck(PtcUiOperation operation)
{
    return operation == PTC_UI_OPERATION_SET_TODAY_LIMIT ||
        operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES ||
        operation == PTC_UI_OPERATION_DISABLE_TODAY_LIMIT ||
        operation == PTC_UI_OPERATION_RESTORE_TODAY_POLICY;
}

void refresh_today_limit_editor(UiState *ui, bool save_after_refresh)
{
    if (!ui || ui->waiting || ui->model.overlay != PTC_UI_OVERLAY_MINUTE_EDITOR ||
        ui->model.operation != PTC_UI_OPERATION_SET_TODAY_LIMIT) return;
    ui->today_limit_refresh_pending = true;
    ui->today_limit_save_pending = save_after_refresh;
    ui->model.quota_refresh_failed = false;
    submit_status(ui);
    if (!ui->waiting) finish_today_limit_refresh(ui, false);
}

void finish_today_limit_refresh(UiState *ui, bool success)
{
    PtcUiModel *model = &ui->model;
    bool save;
    if (!ui->today_limit_refresh_pending) return;
    save = ui->today_limit_save_pending;
    ui->today_limit_refresh_pending = false;
    ui->today_limit_save_pending = false;
    if (model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR ||
        model->operation != PTC_UI_OPERATION_SET_TODAY_LIMIT) return;
    if (!success || !ptc_ui_status_is_fresh(model, (int64_t)time(NULL)) ||
        !model->played_minutes_available || model->played_minutes < 0) {
        model->quota_refresh_failed = true;
        snprintf(model->message, sizeof(model->message),
                 ptc_ui_text(PTC_UI_T_STATUS_REFRESH_FAILED_OR_THE_PLAYED_TIME));
        return;
    }
    model->quota_refresh_failed = false;
    if (!save) return;
    if (ptc_ui_limit_minutes_would_restrict(model, model->draft_minutes)) {
        char body[192];
        snprintf(body, sizeof(body),
                 ptc_ui_text(PTC_UI_T_ESTIMATED_USAGE_ABOUT_D_MIN_NEW_QUOTA),
                 model->played_minutes, (unsigned int)model->draft_minutes);
        open_danger_confirm_overlay(ui, PTC_UI_OPERATION_SET_TODAY_LIMIT,
                                    ptc_ui_text(PTC_UI_T_MAY_BE_RESTRICTED_IMMEDIATELY_AFTER_SETTING), body);
        return;
    }
    if (model->unrestricted_today == 1) {
        char body[192];
        snprintf(body, sizeof(body),
                 ptc_ui_text(PTC_UI_T_TODAY_IS_UNLIMITED_A_U_MIN_TOTAL),
                 (unsigned int)model->draft_minutes,
                 (int)model->draft_minutes - model->played_minutes);
        open_confirm_overlay(ui, PTC_UI_OPERATION_SET_TODAY_LIMIT,
                             ptc_ui_text(PTC_UI_T_UNLIMITED_TIME_WILL_BE_CHANGED_TO_LIMITED_2), body);
        return;
    }
    ptc_ui_numpad_finish(model);
    model->operation = PTC_UI_OPERATION_NONE;
    submit_minutes(ui, PTC_UI_OPERATION_SET_TODAY_LIMIT, model->draft_minutes);
}

void start_quota_recheck(UiState *ui, bool manual)
{
    const PtcUiModel *model = &ui->model;
    if (ui->waiting || model->overlay != PTC_UI_OVERLAY_CONFIRM ||
        !quota_operation_needs_recheck(model->operation)) return;
    ui->quota_recheck_pending = true;
    ui->quota_recheck_manual = manual;
    ui->quota_recheck_ready = false;
    ui->model.quota_refresh_failed = false;
    ptc_ui_quota_recheck_snapshot(model, &ui->quota_before);
    ptc_ui_confirm_hold_update(&ui->confirm_hold, false, ptc_ui_anim_now_ms(), DANGER_CONFIRM_HOLD_MS);
    ui->model.confirm_hold_progress = 0;
    submit_status(ui);
    if (!ui->waiting) finish_quota_recheck(ui, false);
}

void finish_quota_recheck(UiState *ui, bool success)
{
    PtcUiModel *model = &ui->model;
    PtcUiQuotaRecheckDecision decision;
    bool hold;
    if (!ui->quota_recheck_pending) return;
    ui->quota_recheck_pending = false;
    if (model->overlay != PTC_UI_OVERLAY_CONFIRM ||
        model->operation != ui->quota_before.operation) return;
    decision = ptc_ui_quota_recheck_decide(&ui->quota_before, model, success,
        ui->quota_recheck_manual, (int64_t)time(NULL), &hold);
    if (decision == PTC_UI_QUOTA_RECHECK_BLOCK) {
        ui->quota_recheck_ready = false;
        model->quota_refresh_failed = true;
        snprintf(model->message, sizeof(model->message), ptc_ui_text(PTC_UI_T_REFRESH_FAILED_AND_WAS_NOT_SUBMITTED_PRESS));
        return;
    }
    model->quota_refresh_failed = false;
    model->confirm_hold_required = hold;
    if (decision == PTC_UI_QUOTA_RECHECK_CONFIRM_AGAIN) {
        ui->quota_recheck_ready = false;
        snprintf(model->overlay_title, sizeof(model->overlay_title),
                 ui->quota_recheck_manual ? ptc_ui_text(PTC_UI_T_THE_ESTIMATE_HAS_BEEN_REFRESHED_PLEASE_CONFIRM) : ptc_ui_text(PTC_UI_T_THE_STATUS_HAS_CHANGED_PLEASE_RECONFIRM));
        snprintf(model->message, sizeof(model->message),
                 ptc_ui_text(PTC_UI_T_THE_REMAINING_VALUE_HAS_BEEN_RECALCULATED_USING));
        return;
    }
    ui->quota_recheck_ready = true;
    confirm_operation(ui);
}

void confirm_operation(UiState *ui)
{
    PtcCompanionStatus status;
    if (ui->model.parent_support_only && ui->model.operation != PTC_UI_OPERATION_EXPORT_DIAGNOSTICS) {
        ptc_ui_cancel_overlay(&ui->model);
        snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(PTC_UI_T_SETUP_READ_ONLY_SUPPORT));
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM &&
        quota_operation_needs_recheck(ui->model.operation)) {
        if (ui->quota_recheck_pending || ui->waiting) return;
        if (!ui->quota_recheck_ready) {
            start_quota_recheck(ui, false);
            return;
        }
        ui->quota_recheck_ready = false;
    }
    PtcUiOverlay return_overlay = ui->model.confirm_return_overlay;
    bool held_danger_confirmation = ui->model.confirm_hold_required;
    PtcUiOperation operation = ptc_ui_take_confirmed_operation(&ui->model);
    switch (operation) {
    case PTC_UI_OPERATION_IMPORT_CALENDAR:
    case PTC_UI_OPERATION_ACTIVATE_CALENDAR:
        submit_calendar_operation(ui, operation);
        break;
#ifndef PLAYWISE_EDEN
    case PTC_UI_OPERATION_HOT_RELOAD:
        if (ui->waiting || ui->model.recovery_active) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                ptc_ui_text(PTC_UI_T_THE_CURRENT_OPERATION_OR_RECOVERY_HAS_NOT));
        } else if (ptc_hot_reload_begin(&ui->hot_reload)) {
            ui->hot_reload_terminal_handled = false;
            ui->waiting = true;
            ui->model.waiting = true;
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            sync_hot_reload_model(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "%s", ui->hot_reload.detail);
        } else {
            sync_hot_reload_model(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                ui->hot_reload.detail[0] ? ui->hot_reload.detail : ptc_ui_text(PTC_UI_T_THE_CHECK_BEFORE_LOADING_THE_NEW_VERSION));
        }
        break;
#endif
    case PTC_UI_OPERATION_EXPORT_DIAGNOSTICS:
        export_diagnostics(ui);
        break;
    case PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION:
    case PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY:
    case PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY: {
        char error[160] = {0};
        bool ok = operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION
            ? ptc_album_restriction_enable(ui->client.storage, error, sizeof(error))
            : ptc_album_restriction_restore(ui->client.storage,
                operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY, error, sizeof(error));
        refresh_album_restriction(ui);
        refresh_recovery_state(ui);
        if (return_overlay == PTC_UI_OVERLAY_ALBUM_MANAGER) {
            ui->model.overlay = PTC_UI_OVERLAY_ALBUM_MANAGER;
            ui->model.overlay_selection = ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_OFF ? 0 : 1;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_ADVANCED_ENTRY_TO_HOMEBREW_MENU));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), ptc_ui_text(PTC_UI_T_THE_STATUS_HAS_BEEN_RECHECKED_CONFIGURATION_CHANGES));
        }
        if (ok) {
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                     operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION
                       ? ptc_ui_text(PTC_UI_T_PROTECTION_HAS_BEEN_TURNED_ON_AFTER_RESTARTING)
                       : ptc_ui_text(PTC_UI_T_THE_ORIGINAL_HOMEBREW_MENU_ENTRY_METHOD_HAS));
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_NO_SETTING_CHANGED_S), error[0] ? error : ptc_ui_text(PTC_UI_T_PLEASE_TRY_AGAIN_LATER));
        }
        break;
    }
    case PTC_UI_OPERATION_SET_TODAY_LIMIT:
        if (!held_danger_confirmation &&
            ptc_ui_limit_minutes_would_restrict(&ui->model, ui->model.draft_minutes)) {
            open_danger_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_WILL_BE_RESTRICTED_IMMEDIATELY_AFTER_SETTING),
                ptc_ui_text(PTC_UI_T_THE_NEW_AMOUNT_IS_NOT_HIGHER_THAN));
            break;
        }
        submit_minutes(ui, operation, ui->model.draft_minutes);
        break;
    case PTC_UI_OPERATION_ADD_TODAY_MINUTES:
        submit_minutes(ui, operation, ui->model.draft_minutes);
        break;
    case PTC_UI_OPERATION_SAVE_WEEKLY:
        submit_weekly(ui);
        break;
    case PTC_UI_OPERATION_SAVE_HOLIDAY:
        submit_holiday_policy(ui);
        break;
    case PTC_UI_OPERATION_SAVE_SCHEDULED:
        if (ptc_scheduled_override_is_valid(&ui->model.draft_scheduled_override))
            submit_scheduled_override(ui);
        break;
    case PTC_UI_OPERATION_SAVE_BEDTIME:
        {
        PtcBedtimePolicy policy = ptc_ui_bedtime_section_policy(&ui->model,
            ui->model.bedtime_section);
        time_t raw_now = time(NULL);
        struct tm *tm_now = localtime(&raw_now);
        uint16_t minute_of_day = tm_now ? (uint16_t)(tm_now->tm_hour * 60 + tm_now->tm_min) : 0;
        /* Re-evaluate the current window after the hold and environment check. */
        PtcUiBedtimeImpact impact = ptc_ui_bedtime_save_impact(&ui->model,
            minute_of_day, (int64_t)raw_now);
        if (!held_danger_confirmation &&
            (impact == PTC_UI_BEDTIME_IMPACT_RESTRICT ||
             impact == PTC_UI_BEDTIME_IMPACT_UNKNOWN)) {
            open_danger_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_POSSIBLY_ENTER_BEDTIME_RESTRICTION_IMMEDIATELY),
                ptc_ui_text(PTC_UI_T_THE_CURRENT_BEDTIME_WINDOW_OR_STATUS_HAS));
            break;
        }
        if (ptc_bedtime_policy_is_valid(&policy))
            submit_bedtime_policy(ui);
        else {
            cancel_bedtime_navigation(ui);
            snprintf(ui->model.message, sizeof(ui->model.message),
                ptc_ui_text(PTC_UI_T_THE_BEDTIME_DRAFT_HAS_CHANGED_PLEASE_CHECK));
        }
        }
        break;
    case PTC_UI_OPERATION_DISABLE_TODAY_LIMIT:
        submit_transport_empty(ui, "disable_today_limit", ptc_ui_text(PTC_UI_T_LIFTING_CURRENT_RESTRICTIONS), ptc_ui_text(PTC_UI_T_FAILED_TO_LIFT_CURRENT_RESTRICTION));
        break;
    case PTC_UI_OPERATION_RESTORE_TODAY_POLICY:
        {
        PtcEffectiveRule restored = ptc_ui_rule_after_today_restore(&ui->model);
        bool restricts = restored.rule.mode == PTC_RULE_MODE_LIMIT &&
            (!ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL)) ||
             !ui->model.played_minutes_available || ui->model.played_minutes < 0 ||
             (int)restored.rule.minutes <= ui->model.played_minutes);
        if (restricts && !held_danger_confirmation) {
            open_danger_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_MAY_BE_RESTRICTED_IMMEDIATELY_AFTER_CLEARING),
                ptc_ui_text(PTC_UI_T_THE_CURRENT_STATUS_HAS_CHANGED_PLEASE_CHECK));
            break;
        }
        submit_transport_empty(ui, "restore_today_policy", ptc_ui_text(PTC_UI_T_CLEARING_TODAY_S_QUOTA_ADJUSTMENT), ptc_ui_text(PTC_UI_T_FAILED_TO_CLEAR_TODAY_S_QUOTA_ADJUSTMENT));
        }
        break;
    case PTC_UI_OPERATION_SKIP_BEDTIME:
        submit_bedtime_skip(ui);
        break;
    case PTC_UI_OPERATION_CREATE_CONFIG_BACKUP: submit_config_backup(ui, false); break;
    case PTC_UI_OPERATION_RESTORE_CONFIG_BACKUP: submit_config_backup(ui, true); break;
    case PTC_UI_OPERATION_CONFIG_REQUIREMENTS:
        ui->pending_config_restore = true; submit_bedtime_confirmation(ui); break;
    case PTC_UI_OPERATION_SAVE_DOCK:
        submit_dock_policy(ui);
        break;
    case PTC_UI_OPERATION_WAIVE_DOCK:
        submit_dock_waiver(ui);
        break;
    case PTC_UI_OPERATION_CONFIRM_DOCK:
        ui->pending_dock_save = true;
        submit_bedtime_confirmation(ui);
        break;
    case PTC_UI_OPERATION_LEAVE_DOCK:
        ui->model.draft_dock_policy = ui->model.dock_policy;
        ui->model.dock_dirty = false;
        if (!ptc_ui_return_today_settings(&ui->model)) {
            ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
            ui->model.selected_index = 5;
        }
        break;
    case PTC_UI_OPERATION_SKIP_EYE_CARE:
        submit_eye_care_skip(ui);
        break;
    case PTC_UI_OPERATION_CLEAR_BEDTIME_SKIP:
        submit_clear_bedtime_skip(ui);
        break;
    case PTC_UI_OPERATION_CLEAR_REDEMPTION_HISTORY:
        submit_transport_empty(ui, "clear_redemption_history", ptc_ui_text(PTC_UI_T_CLEARING_GRANT_CODE_USAGE_RECORDS), ptc_ui_text(PTC_UI_T_FAILED_TO_CLEAR_USAGE_RECORDS));
        break;
    case PTC_UI_OPERATION_CLEAR_ACTIVITY_HISTORY:
        submit_transport_empty(ui, "clear_activity_history", ptc_ui_text(PTC_UI_T_CLEARING_FAMILY_ACTIVITY_RECORDS), ptc_ui_text(PTC_UI_T_FAILED_TO_CLEAR_FAMILY_ACTIVITY_RECORDS));
        break;
    case PTC_UI_OPERATION_REDEEM_OFFLINE_CODE:
        ui->code_previous_after_available = ui->model.code_preview_after_available;
        ui->code_previous_after_zero = ui->model.code_preview_after_available &&
            ui->model.code_preview_after_minutes == 0;
        ui->code_previous_capped = ui->model.code_preview_capped;
        ui->code_previous_converts_unlimited = ui->model.code_preview_converts_unlimited;
        ui->code_preview_recheck = true;
        submit_preview_offline_code(ui, ui->model.pending_code);
        break;
    case PTC_UI_OPERATION_SAVE_CREDENTIAL:
        if (!commit_credential(ui)) snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_FAILED_TO_SAVE_EXTRA_TIME_CODE_KEY));
        break;
    case PTC_UI_OPERATION_RESET_PAIRING_URL:
        apply_default_pairing_base_url(ui);
        break;
    case PTC_UI_OPERATION_COMPLETE_SETUP:
        submit_transport_empty(ui, "complete_setup", ptc_ui_text(PTC_UI_T_COMPLETING_FIRST_TIME_SETUP), ptc_ui_text(PTC_UI_T_FAILED_TO_ENABLE_AUTOMATIC_CONTROL));
        break;
    case PTC_UI_OPERATION_RETRY_SETUP_RELEASE:
        submit_transport_empty(ui, "retry_setup_release", ptc_ui_text(PTC_UI_T_RETRYING_TO_LIFT_CURRENT_RESTRICTIONS), ptc_ui_text(PTC_UI_T_RETRY_PRE_DELIMITATION_FAILED));
        break;
    case PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT:
        submit_transport_empty(ui, "restore_install_snapshot", ptc_ui_text(PTC_UI_T_RESTORING_TO_PRE_INSTALLATION_STATE), ptc_ui_text(PTC_UI_T_FAILED_TO_RESTORE_PRE_INSTALLATION_STATE));
        break;
    case PTC_UI_OPERATION_EMERGENCY_DISABLE:
        set_local_sd_command(ui, ptc_ui_text(PTC_UI_T_EMERGENCY_DEACTIVATION_CONTROL));
        status = ptc_companion_set_disable_flag(&ui->client, true);
        (void)ptc_companion_transport_notify_storage_changed(&ui->transport);
        ui->model.feedback_detail[0] = '\0';
        if (status == PTC_COMPANION_OK) {
            ui->model.disable_flag_present = true;
            snprintf(ui->model.result_status, sizeof(ui->model.result_status), "ok");
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_BACKGROUND_CONTROL_HAS_BEEN_EMERGENCY_DEACTIVATED));
        } else {
            set_message(ui, ptc_ui_text(PTC_UI_T_EMERGENCY_DEACTIVATION_FAILED), status);
        }
        break;
    case PTC_UI_OPERATION_RESUME_CONTROL:
        set_local_sd_command(ui, ptc_ui_text(PTC_UI_T_RELEASE_EMERGENCY_DISABLE));
        status = ptc_companion_set_disable_flag(&ui->client, false);
        (void)ptc_companion_transport_notify_storage_changed(&ui->transport);
        ui->model.feedback_detail[0] = '\0';
        if (status == PTC_COMPANION_OK) {
            ui->model.disable_flag_present = false;
            snprintf(ui->model.result_status, sizeof(ui->model.result_status), "ok");
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_EMERGENCY_DISABLEMENT_HAS_BEEN_LIFTED_AND_BACKGROUND));
        } else {
            set_message(ui, ptc_ui_text(PTC_UI_T_FAILED_TO_RELEASE_EMERGENCY_DEACTIVATION), status);
        }
        break;
    default:
        break;
    }
}

void accept_numpad(UiState *ui)
{
    PtcUiNumpadPurpose purpose = ui->model.numpad_purpose;
    uint16_t value = 0;
    char code[9];
    if (purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES && weekly_editing_blocked(ui)) {
        return;
    }
    if (!ptc_ui_numpad_validate(&ui->model, &value)) {
        return;
    }
    if (purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        snprintf(code, sizeof(code), "%s", ui->model.numpad_text);
        snprintf(ui->model.pending_code, sizeof(ui->model.pending_code), "%s", code);
        ptc_ui_numpad_finish(&ui->model);
        ui->code_preview_recheck = false;
        submit_preview_offline_code(ui, code);
        return;
    }
    if (purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) {
        ui->model.draft_week[ui->model.editor_index].minutes = value;
        ui->model.weekly_dirty = memcmp(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week)) != 0;
    } else if (purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES) {
        ui->model.draft_holiday_rule.minutes = value;
        update_holiday_dirty(ui);
    } else if (purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) {
        ui->model.draft_makeup_workday_rule.minutes = value;
        update_holiday_dirty(ui);
    } else if (purpose == PTC_UI_NUMPAD_SCHEDULED_MINUTES) {
        ui->model.draft_scheduled_override.rule.minutes = value;
    } else if (purpose == PTC_UI_NUMPAD_GRANT_MINUTES) {
        ui->model.grant_minutes = value;
    } else if (purpose == PTC_UI_NUMPAD_DOCK_MINUTES) {
        ui->model.draft_dock_policy.undocked_daily_minutes = value;
        ui->model.dock_dirty = ptc_ui_dock_dirty(&ui->model);
    } else if (purpose == PTC_UI_NUMPAD_EYE_CARE_PLAY) {
        ui->model.draft_eye_care_policy.play_minutes = value > 240 ? 240 : (value < 1 ? 1 : value);
        ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
    } else if (purpose == PTC_UI_NUMPAD_EYE_CARE_REST) {
        ui->model.draft_eye_care_policy.rest_minutes = value > 60 ? 60 : (value < 1 ? 1 : value);
        ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
    } else if (purpose == PTC_UI_NUMPAD_BEDTIME_TIME) {
        PtcBedtimeWindow *window;
        if (ui->model.numpad_return_overlay == PTC_UI_OVERLAY_BEDTIME_WINDOW) {
            window = &ui->model.draft_bedtime_policy.week[ui->model.bedtime_editor_day];
        } else if (ui->model.numpad_return_overlay == PTC_UI_OVERLAY_BEDTIME) {
            window = &ui->model.draft_bedtime_policy.week[0];
        } else {
            PtcBedtimeSpecialRule *rule = ui->model.bedtime_special_kind == 0
                ? &ui->model.draft_bedtime_policy.holiday_rule
                : (ui->model.bedtime_special_kind == 1
                    ? &ui->model.draft_bedtime_policy.makeup_workday_rule
                    : &ui->model.draft_bedtime_policy.scheduled_override.rule);
            rule->mode = PTC_BEDTIME_OVERRIDE_CUSTOM;
            rule->window.enabled = true;
            window = &rule->window;
        }
        if (ui->model.bedtime_editor_time_target == PTC_UI_BEDTIME_TIME_START)
            window->start_minute = value;
        else
            window->end_minute = value;
        if (ui->model.numpad_return_overlay == PTC_UI_OVERLAY_BEDTIME) {
            for (int day = 1; day < 7; ++day) {
                if (ui->model.bedtime_editor_time_target == PTC_UI_BEDTIME_TIME_START)
                    ui->model.draft_bedtime_policy.week[day].start_minute = value;
                else
                    ui->model.draft_bedtime_policy.week[day].end_minute = value;
            }
        }
        update_bedtime_dirty(ui);
    } else if (purpose == PTC_UI_NUMPAD_MINUTES) {
        ui->model.draft_minutes = value;
    }
    ptc_ui_numpad_finish(&ui->model);
}


void close_code_result(UiState *ui)
{
    bool terminal;
    if (!ui || ui->model.overlay != PTC_UI_OVERLAY_CODE_RESULT) return;
    terminal = !ui->model.code_result_pending;
    ptc_ui_cancel_overlay(&ui->model);
    if (terminal) {
        if (ptc_companion_pending_redemption_clear(&ui->client) != PTC_COMPANION_OK) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_THE_REDEMPTION_RESULT_HAS_BEEN_DISPLAYED_BUT));
        }
        memset(&ui->pending_redemption, 0, sizeof(ui->pending_redemption));
        ui->model.code_result_failed = false;
    } else {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 ptc_ui_text(PTC_UI_T_THE_GRANT_RESULT_IS_STILL_BEING_CONFIRMED));
    }
}
