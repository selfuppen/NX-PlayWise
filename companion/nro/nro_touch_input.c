#include "nro_app_internal.h"

/* A tap resolves to the renderer's control geometry, then drives the same
   shared action as its button shortcut so pointer and pad stay in lock-step. */
void handle_touch(UiState *ui, int x, int y)
{
    PtcUiHit hit = ptc_ui_hit_test(&ui->model, x, y);
    if (ui->waiting && ui->model.view == PTC_UI_PARENT &&
        ui->model.overlay == PTC_UI_OVERLAY_NONE &&
        hit.kind != PTC_UI_HIT_SUPPORT_GUIDE &&
        !(hit.kind == PTC_UI_HIT_PARENT_CARD && ptc_ui_parent_read_only_action(&ui->model, hit.index))) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_CURRENT_OPERATION_IS_2));
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_SUPPORT_GUIDE) {
        if (hit.kind == PTC_UI_HIT_HISTORY_PREV) handle_overlay_input(ui, HidNpadButton_L);
        else if (hit.kind == PTC_UI_HIT_HISTORY_NEXT) handle_overlay_input(ui, HidNpadButton_R);
        else if (hit.kind == PTC_UI_HIT_OVERLAY_CANCEL) handle_overlay_input(ui, HidNpadButton_B);
        return;
    }
    switch (hit.kind) {
    case PTC_UI_HIT_CONFIG_BACKUP_FIELD:
        ui->model.overlay_selection = hit.index; config_backup_action(ui, hit.index); break;
    case PTC_UI_HIT_CHILD_SUBMIT_CODE:
        if (ui->waiting) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_PLEASE_WAIT_FOR_THE_CURRENT_OPERATION_TO));
        } else {
            ptc_audio_play(PTC_SE_CONFIRM);
            open_offline_code_input(ui);
        }
        break;
    case PTC_UI_HIT_CHILD_REFRESH:
        ptc_audio_play(PTC_SE_CONFIRM);
        submit_status(ui);
        break;
    case PTC_UI_HIT_CHILD_BUFFER:
        if (!ui->waiting && ui->model.daily_buffer_available) {
            ptc_audio_play(PTC_SE_CLAIM_BUFFER);
            submit_transport_empty(ui, "claim_daily_buffer",
                ptc_ui_text(PTC_UI_T_IS_RECEIVING_TODAY_S_INDEPENDENT_BUFFER), ptc_ui_text(PTC_UI_T_FAILED_TO_RECEIVE_TODAY_S_INDEPENDENT_BUFFERING));
        } else {
            ptc_audio_play(PTC_SE_ERROR);
        }
        break;
    case PTC_UI_HIT_CHILD_PARENT:
        ptc_audio_play(PTC_SE_CONFIRM);
        enter_parent_area(ui);
        break;
    case PTC_UI_HIT_CHILD_EXIT:
        ptc_audio_play(PTC_SE_CANCEL);
        ui->exit_requested = true;
        break;
    case PTC_UI_HIT_ERROR_RETRY:
        ptc_audio_play(PTC_SE_CONFIRM);
        retry_error(ui);
        break;
    case PTC_UI_HIT_ERROR_BACK:
        ptc_audio_play(PTC_SE_CANCEL);
        enter_child_area(ui);
        break;
    case PTC_UI_HIT_SETUP_LANGUAGE:
    case PTC_UI_HIT_SETUP_TIME_HELP:
    case PTC_UI_HIT_SETUP_PCTL_HELP:
    case PTC_UI_HIT_SETUP_MORE:
    case PTC_UI_HIT_SETUP_SHORTCUT:
    case PTC_UI_HIT_SETUP_EYE_CARE:
    case PTC_UI_HIT_SETUP_BEDTIME:
    case PTC_UI_HIT_SETUP_SKIP:
        ptc_audio_play(PTC_SE_CONFIRM);
        setup_action(ui, hit.kind, hit.index);
        break;
    case PTC_UI_HIT_SETUP_PRIMARY:
        ptc_audio_play(PTC_SE_CONFIRM);
        setup_primary(ui);
        break;
    case PTC_UI_HIT_SETUP_BACK:
        ptc_audio_play(PTC_SE_CANCEL);
        setup_previous(ui);
        break;
    case PTC_UI_HIT_SETUP_PIN:
        ptc_audio_play(PTC_SE_CONFIRM);
        setup_pin(ui);
        break;
    case PTC_UI_HIT_PARENT_PREV_PAGE:
        ptc_audio_play(PTC_SE_TAB);
        request_parent_navigation(ui,
            (ui->model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
        break;
    case PTC_UI_HIT_PARENT_NEXT_PAGE:
        ptc_audio_play(PTC_SE_TAB);
        request_parent_navigation(ui, (ui->model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
        break;
    case PTC_UI_HIT_PARENT_REFRESH:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.parent_footer_focused = true;
        ui->model.parent_footer_selection = 0;
        refresh_disable_flag(ui);
        submit_status(ui);
        break;
    case PTC_UI_HIT_PARENT_STATUS:
        if (!ptc_ui_parent_status_alert_visible(&ui->model)) break;
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.parent_footer_focused = true;
        ui->model.parent_footer_selection = 1;
        activate_parent_status(ui);
        break;
    case PTC_UI_HIT_PARENT_BACK:
        ptc_audio_play(PTC_SE_CANCEL);
        request_parent_navigation(ui, -1, true);
        break;
    case PTC_UI_HIT_PARENT_TAB:
        ptc_audio_play(PTC_SE_TAB);
        request_parent_navigation(ui, hit.index, false);
        break;
    case PTC_UI_HIT_PARENT_CARD:
        if (ui->waiting && !ptc_ui_parent_read_only_action(&ui->model, hit.index)) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_CURRENT_OPERATION_IS));
        } else {
            ptc_audio_play(PTC_SE_CONFIRM);
            ui->model.selected_index = hit.index;
            if (!(ui->model.parent_page == PTC_UI_PARENT_PLAN &&
                  ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) || hit.index >= 3) {
                handle_parent_action(ui);
            }
        }
        break;
    case PTC_UI_HIT_HOLIDAY_CALENDAR:
        ptc_audio_play(PTC_SE_POPUP);
        ui->model.selected_index = 6;
        handle_parent_action(ui);
        break;
    case PTC_UI_HIT_CALENDAR_MANAGER:
        ui->model.selected_index = 7;
        open_calendar_manager(ui);
        break;
    case PTC_UI_HIT_CALENDAR_MANAGER_TAB:
        calendar_manager_select_tab(ui, hit.index);
        break;
    case PTC_UI_HIT_CALENDAR_MANAGER_ROW:
        calendar_manager_select_row(ui, hit.index);
        break;
    case PTC_UI_HIT_CALENDAR_MANAGER_NAV:
        calendar_manager_nav(ui, hit.index);
        break;
    case PTC_UI_HIT_HOLIDAY_PAGE_ACTION:
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_SUPPORT_GUIDE:
        ptc_ui_open_support_guide(&ui->model);
        return;
    case PTC_UI_HIT_SUPPORT_EVENT:
        if (hit.index >= 0 && hit.index < ui->model.recent_event_count) {
            ptc_audio_play(PTC_SE_POPUP);
            ui->model.selected_index = 6 + (ui->model.recent_event_count - 1 - hit.index);
            ui->model.overlay = PTC_UI_OVERLAY_SUPPORT_EVENT;
            ui->model.overlay_selection = hit.index;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_RECENT_EVENT_DETAILS));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                     ptc_ui_text(PTC_UI_T_THE_PARENT_AREA_IS_PIN_AUTHENTICATED_THE));
        }
        break;
    case PTC_UI_HIT_FORECAST_DAY:
        if (hit.index >= 0 && hit.index < 7 && ui->model.forecast_available) {
            ptc_audio_play(PTC_SE_POPUP);
            ui->model.selected_index = 7 + hit.index;
            ui->model.forecast_detail_day_offset = hit.index;
            ui->model.overlay = PTC_UI_OVERLAY_DAY_DECISION;
            ui->model.overlay_selection = hit.index;
        }
        break;
    case PTC_UI_HIT_HOME_DETAILS:
        ptc_audio_play(PTC_SE_POPUP);
        ptc_ui_open_home_details(&ui->model);
        break;
    case PTC_UI_HIT_HOME_DETAILS_ACTION:
        ui->model.home_details_focus = hit.index;
        if (hit.index == 2) ptc_ui_home_details_back(&ui->model);
        else if (hit.index == 1) { if (!ui->waiting) submit_status(ui); }
        else { ptc_audio_play(PTC_SE_POPUP); ptc_ui_home_details_activate(&ui->model, 0); }
        break;
    case PTC_UI_HIT_OVERLAY_CANCEL:
        ptc_audio_play(PTC_SE_CANCEL);
        if (ui->model.overlay == PTC_UI_OVERLAY_HOME_DETAILS ||
            ui->model.overlay == PTC_UI_OVERLAY_NOTICE_DETAILS ||
            ui->model.overlay == PTC_UI_OVERLAY_SETUP_PCTL_HELP ||
            ui->model.overlay == PTC_UI_OVERLAY_DAY_DECISION ||
            ui->model.overlay == PTC_UI_OVERLAY_PARENT_EXPORT_RESULT) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_REDEMPTION_HISTORY) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_AUTH_ERROR) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL ||
            ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL ||
            ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
                ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_BACK;
            }
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CALENDAR_FORMAT) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
            close_code_result(ui);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM &&
                   (ui->model.operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
                    ui->model.operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
                    ui->model.operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY)) {
            ui->model.overlay_selection = 0;
            handle_overlay_input(ui, HidNpadButton_A);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE ||
                   ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM &&
                   (ui->model.operation == PTC_UI_OPERATION_SAVE_BEDTIME ||
                    ui->model.operation == PTC_UI_OPERATION_SAVE_WEEKLY)) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else {
            ptc_ui_cancel_overlay(&ui->model);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_MODIFICATION_CANCELED));
        }
        break;
    case PTC_UI_HIT_OVERLAY_CONFIRM:
        if (ui->model.overlay == PTC_UI_OVERLAY_REDEMPTION_HISTORY ||
            ui->model.overlay == PTC_UI_OVERLAY_ACTIVITY_HISTORY) {
            handle_overlay_input(ui, HidNpadButton_X);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
            close_code_result(ui);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE ||
            ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE) {
            handle_overlay_input(ui, HidNpadButton_A);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_SUPPORT_EVENT) {
            handle_overlay_input(ui, HidNpadButton_A);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_CALENDAR) {
            handle_overlay_input(ui, HidNpadButton_A);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM &&
            (ui->model.operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
             ui->model.operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
             ui->model.operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY)) {
            ui->model.overlay_selection = 1;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_SAVE;
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            ui->model.overlay_selection = 1;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM && ui->model.confirm_hold_required) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_TO_AVOID_MISOPERATION_PLEASE_PRESS_AND_HOLD));
        } else {
            handle_overlay_input(ui, ptc_ui_overlay_primary_uses_plus(ui->model.overlay)
                ? HidNpadButton_Plus : HidNpadButton_A);
        }
        break;
    case PTC_UI_HIT_OVERLAY_DISCARD:
        ptc_audio_play(PTC_SE_CANCEL);
        if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE ||
            ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE) {
            handle_overlay_input(ui, HidNpadButton_X);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            ui->model.overlay_selection = 0;
        }
        handle_overlay_input(ui, HidNpadButton_X);
        break;
    case PTC_UI_HIT_HISTORY_PREV:
        ptc_audio_play(PTC_SE_TAB);
        handle_overlay_input(ui, HidNpadButton_Left);
        break;
    case PTC_UI_HIT_HISTORY_NEXT:
        ptc_audio_play(PTC_SE_TAB);
        handle_overlay_input(ui, HidNpadButton_Right);
        break;
    case PTC_UI_HIT_SCHEDULED_FIELD:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_AUTONOMY_OPTION:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.overlay_selection = hit.index;
        ui->model.draft_autonomy_policy.daily_buffer_minutes = (uint16_t)(hit.index * 5);
        break;
    case PTC_UI_HIT_EYE_CARE_FIELD:
        ptc_audio_play(PTC_SE_STEP);
        if (hit.index == 0) {
            ui->model.overlay_selection = 0;
            ui->model.draft_eye_care_policy.enabled = !ui->model.draft_eye_care_policy.enabled;
        } else {
            static const int deltas[] = {-10, -1, 1, 10};
            uint16_t *value = hit.index <= 4 ? &ui->model.draft_eye_care_policy.play_minutes
                : &ui->model.draft_eye_care_policy.rest_minutes;
            ui->model.overlay_selection = hit.index <= 4 ? 1 : 2;
            *value = ptc_ui_adjust_minutes(*value, deltas[(hit.index - 1) % 4],
                1u, hit.index <= 4 ? 240u : 60u);
        }
        break;
    case PTC_UI_HIT_DOCK_FIELD:
        dock_page_action(ui, hit.index, 0);
        break;
    case PTC_UI_HIT_EYE_CARE_PAGE_MASTER:
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.draft_eye_care_policy.enabled = !ui->model.draft_eye_care_policy.enabled;
        ui->model.eye_care_field_focus = 0;
        ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
        break;
    case PTC_UI_HIT_EYE_CARE_PAGE_PLAY_PRESET:
        {
            static const uint16_t PLAY_PRESETS[] = {20, 30, 40, 60};
            if (hit.index >= 0 && hit.index < 4) {
                ptc_audio_play(PTC_SE_STEP);
                ui->model.draft_eye_care_policy.play_minutes = PLAY_PRESETS[hit.index];
                ui->model.eye_care_field_focus = 1;
                ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
            }
        }
        break;
    case PTC_UI_HIT_EYE_CARE_PAGE_PLAY_DEC:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_eye_care_policy.play_minutes = ptc_ui_adjust_minutes(
            ui->model.draft_eye_care_policy.play_minutes, -1, 1, 240);
        ui->model.eye_care_field_focus = 1;
        ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
        break;
    case PTC_UI_HIT_EYE_CARE_PAGE_PLAY_INC:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_eye_care_policy.play_minutes = ptc_ui_adjust_minutes(
            ui->model.draft_eye_care_policy.play_minutes, 1, 1, 240);
        ui->model.eye_care_field_focus = 1;
        ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
        break;
    case PTC_UI_HIT_EYE_CARE_PAGE_PLAY_VAL:
        ptc_audio_play(PTC_SE_POPUP);
        ui->model.eye_care_field_focus = 1;
        ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_EYE_CARE_PLAY, PTC_UI_OVERLAY_NONE,
                           ptc_ui_text(PTC_UI_T_EYE_CARE_PLAY),
                           ptc_ui_text(PTC_UI_T_EYE_CARE_PLAY_SUBTITLE),
                           3, 1, 240, ui->model.draft_eye_care_policy.play_minutes);
        break;
    case PTC_UI_HIT_EYE_CARE_PAGE_REST_PRESET:
        {
            static const uint16_t REST_PRESETS[] = {5, 10, 15, 20};
            if (hit.index >= 0 && hit.index < 4) {
                ptc_audio_play(PTC_SE_STEP);
                ui->model.draft_eye_care_policy.rest_minutes = REST_PRESETS[hit.index];
                ui->model.eye_care_field_focus = 2;
                ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
            }
        }
        break;
    case PTC_UI_HIT_EYE_CARE_PAGE_REST_DEC:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_eye_care_policy.rest_minutes = ptc_ui_adjust_minutes(
            ui->model.draft_eye_care_policy.rest_minutes, -1, 1, 60);
        ui->model.eye_care_field_focus = 2;
        ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
        break;
    case PTC_UI_HIT_EYE_CARE_PAGE_REST_INC:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_eye_care_policy.rest_minutes = ptc_ui_adjust_minutes(
            ui->model.draft_eye_care_policy.rest_minutes, 1, 1, 60);
        ui->model.eye_care_field_focus = 2;
        ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
        break;
    case PTC_UI_HIT_EYE_CARE_PAGE_REST_VAL:
        ptc_audio_play(PTC_SE_POPUP);
        ui->model.eye_care_field_focus = 2;
        ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_EYE_CARE_REST, PTC_UI_OVERLAY_NONE,
                           ptc_ui_text(PTC_UI_T_EYE_CARE_REST),
                           ptc_ui_text(PTC_UI_T_EYE_CARE_REST_SUBTITLE),
                           2, 1, 60, ui->model.draft_eye_care_policy.rest_minutes);
        break;
    case PTC_UI_HIT_EYE_CARE_PAGE_SAVE:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.eye_care_field_focus = 3;
        submit_eye_care_policy(ui);
        break;
    case PTC_UI_HIT_EYE_CARE_PAGE_SKIP:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.eye_care_field_focus = 4;
        submit_eye_care_skip(ui);
        break;
    case PTC_UI_HIT_QUICK_ADD_OPTION:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_BEDTIME_SECTION:
        ptc_audio_play(PTC_SE_TAB);
        ui->model.bedtime_master_focused = false;
        select_bedtime_section(ui, hit.index);
        break;
    case PTC_UI_HIT_BEDTIME_MANAGE:
        ptc_audio_play(PTC_SE_CONFIRM);
        if (hit.index == 0) handle_today_action_ready(ui, PTC_UI_OPERATION_SKIP_BEDTIME);
        else request_clear_bedtime_skip(ui);
        break;
    case PTC_UI_HIT_BEDTIME_MASTER_SWITCH:
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.bedtime_master_focused = true;
        ui->model.bedtime_section_focused = false;
        if (!ui->model.disable_flag_present) {
            ui->model.draft_bedtime_policy.enabled = !ui->model.draft_bedtime_policy.enabled;
            update_bedtime_dirty(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_BEDTIME_MASTER_SWITCH_DRAFT_IS_S_SAVE),
                     ui->model.draft_bedtime_policy.enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF));
        }
        break;
    case PTC_UI_HIT_BEDTIME_FIELD:
        ui->model.bedtime_master_focused = false;
        ui->model.bedtime_section_focused = false;
        ui->model.selected_index = hit.index;
        if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
            ui->model.plan_page == PTC_UI_PLAN_PAGE_BEDTIME) {
            if (ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY && hit.index < 7) {
                int day = ptc_ui_weekday_for_display_slot(hit.index);
                ui->model.bedtime_editor_day = day;
                ptc_audio_play(PTC_SE_POPUP);
                open_bedtime_window_editor(ui, day);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY &&
                       (hit.index == 7 || hit.index == 8)) {
                ptc_audio_play(PTC_SE_POPUP);
                ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_BULK;
                ui->model.overlay_selection = hit.index - 7;
                snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_COPY_WEEKLY_BEDTIME_WINDOW));
                snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                    ptc_ui_text(PTC_UI_T_COPIES_THE_COMPLETE_SWITCHES_AND_TIME_OF));
            } else if ((ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY && hit.index == 9) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index == 3) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 4)) {
                ptc_audio_play(PTC_SE_CANCEL);
                discard_bedtime_draft(ui);
            } else if ((ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY && hit.index == 10) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index == 4) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 5)) {
                ptc_audio_play(PTC_SE_CONFIRM);
                save_bedtime_from_page(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index == 0) {
                ptc_audio_play(PTC_SE_TOGGLE);
                ui->model.draft_bedtime_policy.calendar_enabled =
                    !ui->model.draft_bedtime_policy.calendar_enabled;
                update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index <= 2) {
                ptc_audio_play(PTC_SE_POPUP);
                open_bedtime_special_editor(ui, hit.index - 1);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 0) {
                ptc_audio_play(PTC_SE_TOGGLE);
                ui->model.draft_bedtime_policy.scheduled_override.present =
                    !ui->model.draft_bedtime_policy.scheduled_override.present;
                update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 1) {
                ptc_audio_play(PTC_SE_POPUP);
                if (edit_date_range_start(ui,
                        &ui->model.draft_bedtime_policy.scheduled_override.start_day_index,
                        &ui->model.draft_bedtime_policy.scheduled_override.end_day_index))
                    update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 2) {
                ptc_audio_play(PTC_SE_POPUP);
                if (edit_date_range_span(ui,
                        ui->model.draft_bedtime_policy.scheduled_override.start_day_index,
                        &ui->model.draft_bedtime_policy.scheduled_override.end_day_index))
                    update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 3) {
                ptc_audio_play(PTC_SE_POPUP);
                open_bedtime_special_editor(ui, 2);
            }
        }
        break;
    case PTC_UI_HIT_BEDTIME_OVERLAY_FIELD:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_BEDTIME_PRESET:
        ptc_audio_play(PTC_SE_STEP);
        apply_bedtime_preset(ui, hit.index);
        break;
    case PTC_UI_HIT_MINUTES_INC:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_DEC:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_INC_LARGE:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_DEC_LARGE:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_VALUE:
        ptc_audio_play(PTC_SE_POPUP);
        edit_overlay_minutes(ui);
        break;
    case PTC_UI_HIT_WEEKLY_DAY:
        ptc_audio_play(PTC_SE_FOCUS);
        ui->model.editor_index = hit.index;
        for (int slot = 0; slot < 7; ++slot) {
            if (ptc_ui_weekday_for_display_slot(slot) == hit.index) {
                ui->model.weekly_grid_slot = slot;
                ui->model.weekly_last_day_slot = slot;
                break;
            }
        }
        ui->model.selected_index = 0;
        break;
    case PTC_UI_HIT_WEEKLY_BULK:
        ui->model.selected_index = 2;
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_IS_IN_EMERGENCY_DEACTIVATION_BATCH_OPERATIONS_ARE));
        } else {
            ptc_audio_play(PTC_SE_POPUP);
            ui->model.overlay = PTC_UI_OVERLAY_WEEKLY_BULK;
            ui->model.overlay_selection = 0;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_BATCH_QUICK_OPERATIONS));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), ptc_ui_text(PTC_UI_T_COPIES_THE_COMPLETE_DRAFT_RULE_FOR_THE));
        }
        break;
    case PTC_UI_HIT_WEEKLY_BULK_TARGET:
        ptc_audio_play(PTC_SE_FOCUS);
        ui->model.overlay_selection = hit.index;
        break;
    case PTC_UI_HIT_ALBUM_ACTION:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_ALBUM_REFRESH:
        ptc_audio_play(PTC_SE_CONFIRM);
        handle_overlay_input(ui, HidNpadButton_Y);
        break;
    case PTC_UI_HIT_WEEKLY_MODE:
        if (hit.index >= 0 && hit.index < 7) {
            ui->model.editor_index = hit.index;
            for (int slot = 0; slot < 7; ++slot) {
                if (ptc_ui_weekday_for_display_slot(slot) == hit.index) {
                    ui->model.weekly_grid_slot = slot;
                    ui->model.weekly_last_day_slot = slot;
                    break;
                }
            }
        }
        ui->model.selected_index = 0;
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.draft_week[ui->model.editor_index].mode =
            ptc_ui_next_rule_mode(ui->model.draft_week[ui->model.editor_index].mode);
        update_weekly_dirty(ui);
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_PREVIOUS_DAILY_LIMIT_RESTORED_U_MIN),
                     (unsigned int)ui->model.draft_week[ui->model.editor_index].minutes);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_UP:
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 15, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DOWN:
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -15, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DEC:
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -5, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_INC:
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 5, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_INPUT:
        if (hit.index >= 0 && hit.index < 7) {
            ui->model.editor_index = hit.index;
            for (int slot = 0; slot < 7; ++slot) {
                if (ptc_ui_weekday_for_display_slot(slot) == hit.index) {
                    ui->model.weekly_grid_slot = slot;
                    ui->model.weekly_last_day_slot = slot;
                    break;
                }
            }
        }
        ui->model.selected_index = 0;
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_POPUP);
            edit_weekly_minutes(ui);
        } else {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_THE_CURRENT_MODE_IS_UNLIMITED_PLEASE_SWITCH));
        }
        break;
    case PTC_UI_HIT_HOLIDAY_ENABLE:
        ui->model.selected_index = 0;
        if (ui->model.disable_flag_present) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_IN_EMERGENCY_DEACTIVATION_THE_RULES_ARE_TEMPORARILY));
        } else {
            ptc_audio_play(PTC_SE_TOGGLE);
            ui->model.draft_holiday_enabled = !ui->model.draft_holiday_enabled;
            update_holiday_dirty(ui);
        }
        break;
    case PTC_UI_HIT_HOLIDAY_MODE:
        ui->model.selected_index = hit.index + 1;
        ui->model.holiday_last_rule = hit.index;
        if (ui->model.disable_flag_present) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_IN_EMERGENCY_DEACTIVATION_THE_RULES_ARE_TEMPORARILY));
        } else if (hit.index == 0) {
            ptc_audio_play(PTC_SE_TOGGLE);
            ui->model.draft_holiday_rule.mode = ptc_ui_next_rule_mode(ui->model.draft_holiday_rule.mode);
            update_holiday_dirty(ui);
        } else if (hit.index == 1) {
            ptc_audio_play(PTC_SE_TOGGLE);
            ui->model.draft_makeup_workday_rule.mode = ptc_ui_next_rule_mode(ui->model.draft_makeup_workday_rule.mode);
            update_holiday_dirty(ui);
        }
        break;
    case PTC_UI_HIT_HOLIDAY_MINUTES:
        ui->model.selected_index = hit.index + 1;
        ui->model.holiday_last_rule = hit.index;
        if (ui->model.disable_flag_present) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_IN_EMERGENCY_DEACTIVATION_THE_RULES_ARE_TEMPORARILY));
        } else {
            ptc_audio_play(PTC_SE_POPUP);
            handle_parent_action(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_SAVE:
        ui->model.selected_index = 4;
        ptc_audio_play(PTC_SE_CONFIRM);
        save_weekly_from_page(ui);
        break;
    case PTC_UI_HIT_WEEKLY_DISCARD:
        ui->model.selected_index = 3;
        ptc_audio_play(PTC_SE_CANCEL);
        if (ui->model.weekly_dirty) {
            memcpy(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week));
            ui->model.weekly_dirty = false;
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_UNSAVED_WEEKLY_SCHEDULE_MODIFICATIONS_ABANDONED));
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_WEEKLY_PLAN_HAS_NOT_BEEN_MODIFIED));
        }
        break;
    case PTC_UI_HIT_CREDENTIAL_INPUT:
        ptc_audio_play(PTC_SE_KEYSTROKE);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
        handle_overlay_input(ui, HidNpadButton_X);
        break;
    case PTC_UI_HIT_CREDENTIAL_RANDOM:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_RANDOM;
        handle_overlay_input(ui, HidNpadButton_Y);
        break;
    case PTC_UI_HIT_CREDENTIAL_REVEAL:
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_REVEAL;
        handle_overlay_input(ui, HidNpadButton_ZR);
        break;
    case PTC_UI_HIT_CREDENTIAL_DEMO:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_DEMO;
        handle_overlay_input(ui, HidNpadButton_R);
        break;
    case PTC_UI_HIT_GRANT_MANAGER_CARD:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_GRANT_GENERATE:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
        generate_local_grant_code(ui);
        break;
    case PTC_UI_HIT_SHORTCUT_OPTION:
        ptc_audio_play(PTC_SE_FOCUS);
        select_setup_shortcut(ui, hit.index);
        break;
    case PTC_UI_HIT_SHORTCUT_DISABLE:
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.shortcut_draft_enabled = false;
        break;
    case PTC_UI_HIT_SHORTCUT_HINT:
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.shortcut_draft_show_hint = !ui->model.shortcut_draft_show_hint;
        break;
    case PTC_UI_HIT_THEME_OPTION:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_LANGUAGE_OPTION:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_QR_EXPORT:
        ptc_audio_play(PTC_SE_CONFIRM);
        export_parent_import(ui);
        break;
    case PTC_UI_HIT_SETUP_THEME_OPTION:
        ptc_audio_play(PTC_SE_CONFIRM);
        setup_action(ui, hit.kind, hit.index);
        break;
    case PTC_UI_HIT_GRANT_ADJUST:
        ptc_audio_play(PTC_SE_POPUP);
        ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_ADJUST_FIRST;
        edit_grant_minutes(ui);
        break;
    case PTC_UI_HIT_NUMPAD_KEY:
        ptc_audio_play(PTC_SE_KEYSTROKE);
        ui->model.numpad_cursor = hit.index;
        ptc_ui_numpad_activate(&ui->model);
        break;
    case PTC_UI_HIT_NUMPAD_QUICK:
        if (hit.index >= 0 && hit.index < 2) {
            static const int DELTAS[] = {-15, 15};
            ptc_audio_play(PTC_SE_STEP);
            ptc_ui_numpad_adjust(&ui->model, DELTAS[hit.index]);
        }
        break;
    case PTC_UI_HIT_DURATION_FIELD:
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_duration_select_field(&ui->model, (PtcUiDurationField)hit.index);
        break;
    case PTC_UI_HIT_TODAY_MODE:
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.today_limit_unlimited_draft = hit.index == 1;
        break;
    case PTC_UI_HIT_QUOTA_REFRESH:
        ptc_audio_play(PTC_SE_CONFIRM);
        start_quota_recheck(ui, true);
        break;
    case PTC_UI_HIT_TODAY_LIMIT_REFRESH:
        ptc_audio_play(PTC_SE_CONFIRM);
        refresh_today_limit_editor(ui, false);
        break;
    case PTC_UI_HIT_NOTICE_DETAILS:
        ptc_audio_play(PTC_SE_POPUP);
        ptc_ui_open_notice_details(&ui->model);
        break;
    case PTC_UI_HIT_NONE:
    default:
        break;
    }
}
