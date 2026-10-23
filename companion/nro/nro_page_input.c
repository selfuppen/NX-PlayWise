#include "nro_app_internal.h"

static void handle_weekly_page_input(UiState *ui, u64 down)
{
    PtcDayRule *day = &ui->model.draft_week[ui->model.editor_index];
    if (ui->waiting) {
        if (down) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_WEEKLY_PLAN_IS));
        }
    } else if (down & HidNpadButton_B) {
        ptc_audio_play(PTC_SE_CANCEL);
        request_parent_navigation(ui, -1, true);
    } else if (down & (HidNpadButton_L | HidNpadButton_R)) {
        ptc_audio_play(PTC_SE_TAB);
        request_parent_navigation(ui, -1, true);
    } else if (down & HidNpadButton_Left) {
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_move_weekly_focus(&ui->model, -1, 0);
    } else if (down & HidNpadButton_Right) {
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_move_weekly_focus(&ui->model, 1, 0);
    } else if (down & HidNpadButton_X) {
        if (!weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_TOGGLE);
            day->mode = ptc_ui_next_rule_mode(day->mode);
            update_weekly_dirty(ui);
        } else {
            ptc_audio_play(PTC_SE_ERROR);
        }
        if (!ui->model.disable_flag_present && day->mode == PTC_RULE_MODE_LIMIT) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_PREVIOUS_DAILY_LIMIT_RESTORED_U_MIN), (unsigned int)day->minutes);
        }
    } else if (down & HidNpadButton_Up) {
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_move_weekly_focus(&ui->model, 0, -1);
    } else if (down & HidNpadButton_Down) {
        ptc_audio_play(PTC_SE_FOCUS);
        if (ui->model.selected_index != 0) {
            ui->model.parent_content_selection = ui->model.selected_index;
            ui->model.parent_footer_focused = true;
            ui->model.parent_footer_selection =
                ptc_ui_parent_status_alert_visible(&ui->model) ? 1 : 0;
        } else {
            ptc_ui_move_weekly_focus(&ui->model, 0, 1);
        }
    } else if (down & HidNpadButton_Y) {
        ptc_audio_play(PTC_SE_CONFIRM);
        refresh_disable_flag(ui);
        submit_status(ui);
    } else if (down & HidNpadButton_A) {
        if (ui->model.selected_index == 2) {
            if (weekly_editing_blocked(ui)) {
                ptc_audio_play(PTC_SE_ERROR);
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_IS_IN_EMERGENCY_DEACTIVATION_BATCH_OPERATIONS_ARE));
            } else {
                ptc_audio_play(PTC_SE_CONFIRM);
                ui->model.overlay = PTC_UI_OVERLAY_WEEKLY_BULK;
                ui->model.overlay_selection = 0;
                snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_BATCH_QUICK_OPERATIONS));
                snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), ptc_ui_text(PTC_UI_T_COPIES_THE_COMPLETE_DRAFT_RULE_FOR_THE));
            }
        } else if (ui->model.selected_index == 3) {
            ptc_audio_play(PTC_SE_CANCEL);
            if (ui->model.weekly_dirty) {
                memcpy(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week));
                ui->model.weekly_dirty = false;
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_UNSAVED_WEEKLY_SCHEDULE_MODIFICATIONS_ABANDONED));
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_WEEKLY_PLAN_HAS_NOT_BEEN_MODIFIED));
            }
        } else if (weekly_editing_blocked(ui)) {
            /* Focus remains movable while emergency stop makes the editor read-only. */
            ptc_audio_play(PTC_SE_ERROR);
        } else if (ui->model.selected_index == 1) {
            ptc_audio_play(PTC_SE_TOGGLE);
            day->mode = ptc_ui_next_rule_mode(day->mode);
            update_weekly_dirty(ui);
            if (day->mode == PTC_RULE_MODE_LIMIT) {
                snprintf(ui->model.message, sizeof(ui->model.message),
                         ptc_ui_text(PTC_UI_T_PREVIOUS_DAILY_LIMIT_RESTORED_U_MIN), (unsigned int)day->minutes);
            }
        } else if (ui->model.selected_index == 4) {
            ptc_audio_play(PTC_SE_CONFIRM);
            save_weekly_from_page(ui);
        } else if (day->mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_CONFIRM);
            edit_weekly_minutes(ui);
        } else {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_THIS_DAY_IS_UNLIMITED_SELECT_CHANGE_MODE));
        }
    } else if (down & HidNpadButton_Plus) {
        ui->model.selected_index = 4;
        if (!weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_CONFIRM);
            save_weekly_from_page(ui);
        } else {
            ptc_audio_play(PTC_SE_ERROR);
        }
    } else if (down & HidNpadButton_ZL) {
        ptc_audio_play(PTC_SE_CANCEL);
        ui->model.selected_index = 3;
        if (ui->model.weekly_dirty) {
            memcpy(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week));
            ui->model.weekly_dirty = false;
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_UNSAVED_WEEKLY_SCHEDULE_MODIFICATIONS_ABANDONED));
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_WEEKLY_PLAN_HAS_NOT_BEEN_MODIFIED));
        }
    }
}

static void handle_bedtime_page_input(UiState *ui, u64 down)
{
    PtcBedtimePolicy *draft = &ui->model.draft_bedtime_policy;
    if (ui->waiting) {
        if (down) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message),
                ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_BEDTIME_SETTINGS_ARE));
        }
    } else if (down & HidNpadButton_B) {
        ptc_audio_play(PTC_SE_CANCEL);
        request_bedtime_leave(ui, -1, true);
    } else if (down & HidNpadButton_Y) {
        ptc_audio_play(PTC_SE_CONFIRM);
        handle_today_action_ready(ui, PTC_UI_OPERATION_SKIP_BEDTIME);
    } else if (down & HidNpadButton_X) {
        ptc_audio_play(PTC_SE_CONFIRM);
        request_clear_bedtime_skip(ui);
    } else if (down & HidNpadButton_L) {
        ptc_audio_play(PTC_SE_TAB);
        select_bedtime_section(ui, ui->model.bedtime_section - 1);
    } else if (down & HidNpadButton_R) {
        ptc_audio_play(PTC_SE_TAB);
        select_bedtime_section(ui, ui->model.bedtime_section + 1);
    } else if (down & HidNpadButton_Up) {
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_move_bedtime_focus(&ui->model, 0, -1);
    } else if (down & HidNpadButton_Down) {
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_move_bedtime_focus(&ui->model, 0, 1);
    } else if (down & HidNpadButton_Left) {
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_move_bedtime_focus(&ui->model, -1, 0);
    } else if (down & HidNpadButton_Right) {
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_move_bedtime_focus(&ui->model, 1, 0);
    } else if (down & HidNpadButton_Minus) {
        if (!ui->model.disable_flag_present) {
            ptc_audio_play(PTC_SE_TOGGLE);
            draft->enabled = !draft->enabled;
            update_bedtime_dirty(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_BEDTIME_MASTER_SWITCH_DRAFT_IS_S_SAVE),
                draft->enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF));
        } else {
            ptc_audio_play(PTC_SE_ERROR);
        }
    } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED &&
               !ui->model.bedtime_section_focused &&
               (down & (HidNpadButton_ZL | HidNpadButton_ZR))) {
        int direction = down & HidNpadButton_ZR ? 1 : -1;
        int date_step = 7;
        ptc_audio_play(PTC_SE_STEP);
        if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED &&
            ui->model.selected_index == 1) {
            uint32_t duration = draft->scheduled_override.end_day_index >=
                draft->scheduled_override.start_day_index
                ? (uint32_t)draft->scheduled_override.end_day_index -
                  draft->scheduled_override.start_day_index + 1u : 1u;
            int next = (int)draft->scheduled_override.start_day_index + direction * date_step;
            if (next < (int)ui->model.day_index) next = ui->model.day_index;
            if ((uint32_t)next + duration - 1u > UINT16_MAX) next = UINT16_MAX - (int)duration + 1;
            draft->scheduled_override.start_day_index = (uint16_t)next;
            draft->scheduled_override.end_day_index = (uint16_t)(next + (int)duration - 1);
            update_bedtime_dirty(ui);
        } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED &&
                   ui->model.selected_index == 2) {
            uint32_t duration = draft->scheduled_override.end_day_index >=
                draft->scheduled_override.start_day_index
                ? (uint32_t)draft->scheduled_override.end_day_index -
                  draft->scheduled_override.start_day_index + 1u : 1u;
            int next = (int)duration + direction * date_step;
            if (next < 1) next = 1;
            if (next > 366) next = 366;
            if ((uint32_t)draft->scheduled_override.start_day_index + (uint32_t)next - 1u > UINT16_MAX)
                next = UINT16_MAX - draft->scheduled_override.start_day_index + 1;
            draft->scheduled_override.end_day_index =
                (uint16_t)(draft->scheduled_override.start_day_index + next - 1);
            update_bedtime_dirty(ui);
        }
    } else if ((down & HidNpadButton_A) && ui->model.bedtime_master_focused) {
        if (!ui->model.disable_flag_present) {
            ptc_audio_play(PTC_SE_TOGGLE);
            draft->enabled = !draft->enabled;
            update_bedtime_dirty(ui);
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_BEDTIME_MASTER_SWITCH_DRAFT_IS_S_SAVE), draft->enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF));
        } else {
            ptc_audio_play(PTC_SE_ERROR);
        }
    } else if ((down & HidNpadButton_A) && !ui->model.bedtime_section_focused) {
        if (ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY) {
            if (ui->model.selected_index < 7) {
                ptc_audio_play(PTC_SE_CONFIRM);
                int day = ptc_ui_weekday_for_display_slot(ui->model.selected_index);
                ui->model.bedtime_editor_day = day;
                open_bedtime_window_editor(ui, day);
            } else if (ui->model.selected_index == 7 || ui->model.selected_index == 8) {
                ptc_audio_play(PTC_SE_CONFIRM);
                ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_BULK;
                ui->model.overlay_selection = ui->model.selected_index - 7;
                snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_COPY_WEEKLY_BEDTIME_WINDOW));
                snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                    ptc_ui_text(PTC_UI_T_COPIES_THE_COMPLETE_SWITCHES_AND_TIME_OF));
            } else if (ui->model.selected_index == 9) {
                ptc_audio_play(PTC_SE_CANCEL);
                discard_bedtime_draft(ui);
            } else {
                ptc_audio_play(PTC_SE_CONFIRM);
                save_bedtime_from_page(ui);
            }
        } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR) {
            if (ui->model.selected_index == 0) {
                ptc_audio_play(PTC_SE_TOGGLE);
                draft->calendar_enabled = !draft->calendar_enabled;
                update_bedtime_dirty(ui);
            } else if (ui->model.selected_index <= 2) {
                ptc_audio_play(PTC_SE_CONFIRM);
                open_bedtime_special_editor(ui, ui->model.selected_index - 1);
            } else if (ui->model.selected_index == 3) {
                ptc_audio_play(PTC_SE_CANCEL);
                discard_bedtime_draft(ui);
            } else {
                ptc_audio_play(PTC_SE_CONFIRM);
                save_bedtime_from_page(ui);
            }
        } else {
            if (ui->model.selected_index == 0) {
                ptc_audio_play(PTC_SE_TOGGLE);
                draft->scheduled_override.present = !draft->scheduled_override.present;
                if (draft->scheduled_override.start_day_index < ui->model.day_index) {
                    draft->scheduled_override.start_day_index = ui->model.day_index;
                    draft->scheduled_override.end_day_index = ui->model.day_index;
                }
                update_bedtime_dirty(ui);
            } else if (ui->model.selected_index == 1) {
                ptc_audio_play(PTC_SE_CONFIRM);
                if (edit_date_range_start(ui, &draft->scheduled_override.start_day_index,
                                          &draft->scheduled_override.end_day_index))
                    update_bedtime_dirty(ui);
            } else if (ui->model.selected_index == 2) {
                ptc_audio_play(PTC_SE_CONFIRM);
                if (edit_date_range_span(ui, draft->scheduled_override.start_day_index,
                                         &draft->scheduled_override.end_day_index))
                    update_bedtime_dirty(ui);
            } else if (ui->model.selected_index == 3) {
                ptc_audio_play(PTC_SE_CONFIRM);
                open_bedtime_special_editor(ui, 2);
            } else if (ui->model.selected_index == 4) {
                ptc_audio_play(PTC_SE_CANCEL);
                discard_bedtime_draft(ui);
            } else if (ui->model.selected_index == 5) {
                ptc_audio_play(PTC_SE_CONFIRM);
                save_bedtime_from_page(ui);
            }
        }
    } else if (down & HidNpadButton_Plus) {
        ptc_audio_play(PTC_SE_CONFIRM);
        save_bedtime_from_page(ui);
    } else if (down & HidNpadButton_ZL) {
        ptc_audio_play(PTC_SE_CANCEL);
        discard_bedtime_draft(ui);
    }
}

static void handle_dock_page_input(UiState *ui, u64 down)
{
    if (!ui->waiting) {
        if (down & HidNpadButton_B) dock_page_action(ui, 10, 0);
        else if (down & HidNpadButton_Y) dock_page_action(ui, 6, 0);
        else if (down & HidNpadButton_X) dock_page_action(ui, 4, 0);
        else if (down & HidNpadButton_Plus) dock_page_action(ui, 3, 0);
        else if (down & HidNpadButton_Up) {
            int old_focus = ui->model.dock_field_focus;
            int focus = ui->model.dock_field_focus;
            if (focus == 7 || focus == 8) ui->model.dock_field_focus = focus == 7 ? 3 : 4;
            else if (focus == 5) ui->model.dock_field_focus = 4;
            else if (focus == 3 || focus == 4) ui->model.dock_field_focus = 2;
            else if (focus == 2) ui->model.dock_field_focus = 1;
            else if (focus == 1) ui->model.dock_field_focus = 0;
            else if (focus == 0) ui->model.dock_field_focus = ui->model.eden_mode_controls ? 7 : 5;
            if (ui->model.dock_field_focus != old_focus) ptc_audio_play(PTC_SE_FOCUS);
        }
        else if (down & HidNpadButton_Down) {
            int old_focus = ui->model.dock_field_focus;
            int focus = ui->model.dock_field_focus;
            if (focus == 0) ui->model.dock_field_focus = 1;
            else if (focus == 1) ui->model.dock_field_focus = 2;
            else if (focus == 2) ui->model.dock_field_focus = 3;
            else if (focus == 3 || focus == 4) ui->model.dock_field_focus = 5;
            else if (focus == 5) {
                if (ui->model.eden_mode_controls) ui->model.dock_field_focus = 7;
                else ui->model.dock_field_focus = 0;
            }
            else if (focus == 7 || focus == 8) ui->model.dock_field_focus = 0;
            if (ui->model.dock_field_focus != old_focus) ptc_audio_play(PTC_SE_FOCUS);
        }
        else if (down & HidNpadButton_Left) {
            int old_focus = ui->model.dock_field_focus;
            int focus = ui->model.dock_field_focus;
            if (focus == 2) dock_page_action(ui, 2, -1);
            else if (focus == 4) ui->model.dock_field_focus = 3;
            else if (focus == 5) ui->model.dock_field_focus = 4;
            else if (focus == 8) ui->model.dock_field_focus = 7;
            if (ui->model.dock_field_focus != old_focus) ptc_audio_play(PTC_SE_FOCUS);
        }
        else if (down & HidNpadButton_Right) {
            int old_focus = ui->model.dock_field_focus;
            int focus = ui->model.dock_field_focus;
            if (focus == 2) dock_page_action(ui, 2, 1);
            else if (focus == 3) ui->model.dock_field_focus = 4;
            else if (focus == 4) ui->model.dock_field_focus = 5;
            else if (focus == 7) ui->model.dock_field_focus = 8;
            if (ui->model.dock_field_focus != old_focus) ptc_audio_play(PTC_SE_FOCUS);
        }
        else if (down & HidNpadButton_A) dock_page_action(ui, ui->model.dock_field_focus, 0);
    }
}

static void handle_eye_care_page_input(UiState *ui, u64 down)
{
    PtcEyeCarePolicy *draft = &ui->model.draft_eye_care_policy;
    bool resting = (strcmp(ui->model.eye_care_phase, "resting") == 0);
    int focus_count = resting ? 5 : 4;
    if (ui->waiting) {
        if (down) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_CURRENT_OPERATION_IS));
        }
    } else if (down & HidNpadButton_B) {
        ptc_audio_play(PTC_SE_CANCEL);
        ui->model.draft_eye_care_policy = ui->model.eye_care_policy;
        ui->model.eye_care_dirty = false;
        if (!ptc_ui_return_today_settings(&ui->model)) {
            ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
            ui->model.selected_index = 4;
        }
    } else if (down & (HidNpadButton_L | HidNpadButton_R)) {
        ptc_audio_play(PTC_SE_CANCEL);
        ui->model.draft_eye_care_policy = ui->model.eye_care_policy;
        ui->model.eye_care_dirty = false;
        if (!ptc_ui_return_today_settings(&ui->model)) {
            ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
            ui->model.selected_index = 4;
        }
    } else if (down & HidNpadButton_Up) {
        ptc_audio_play(PTC_SE_FOCUS);
        ui->model.eye_care_field_focus = (ui->model.eye_care_field_focus + focus_count - 1) % focus_count;
    } else if (down & HidNpadButton_Down) {
        ptc_audio_play(PTC_SE_FOCUS);
        ui->model.eye_care_field_focus = (ui->model.eye_care_field_focus + 1) % focus_count;
    } else if (down & (HidNpadButton_Left | HidNpadButton_Right | HidNpadButton_ZL | HidNpadButton_ZR)) {
        int delta = 0;
        if (down & HidNpadButton_Left) delta = -1;
        else if (down & HidNpadButton_Right) delta = 1;
        else if (down & HidNpadButton_ZL) delta = -10;
        else if (down & HidNpadButton_ZR) delta = 10;

        if (ui->model.eye_care_field_focus == 0) {
            ptc_audio_play(PTC_SE_TOGGLE);
            draft->enabled = !draft->enabled;
            ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
        } else if (ui->model.eye_care_field_focus == 1) {
            ptc_audio_play(PTC_SE_STEP);
            draft->play_minutes = ptc_ui_adjust_minutes(draft->play_minutes, delta, 1, 240);
            ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
        } else if (ui->model.eye_care_field_focus == 2) {
            int rest_delta = delta;
            if (down & HidNpadButton_ZL) rest_delta = -5;
            else if (down & HidNpadButton_ZR) rest_delta = 5;
            ptc_audio_play(PTC_SE_STEP);
            draft->rest_minutes = ptc_ui_adjust_minutes(draft->rest_minutes, rest_delta, 1, 60);
            ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
        }
    } else if (down & HidNpadButton_A) {
        if (ui->model.eye_care_field_focus == 0) {
            ptc_audio_play(PTC_SE_TOGGLE);
            draft->enabled = !draft->enabled;
            ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
        } else if (ui->model.eye_care_field_focus == 1) {
            ptc_audio_play(PTC_SE_POPUP);
            ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_EYE_CARE_PLAY, PTC_UI_OVERLAY_NONE,
                               ptc_ui_text(PTC_UI_T_EYE_CARE_PLAY),
                               ptc_ui_text(PTC_UI_T_EYE_CARE_PLAY_SUBTITLE),
                               3, 1, 240, draft->play_minutes);
        } else if (ui->model.eye_care_field_focus == 2) {
            ptc_audio_play(PTC_SE_POPUP);
            ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_EYE_CARE_REST, PTC_UI_OVERLAY_NONE,
                               ptc_ui_text(PTC_UI_T_EYE_CARE_REST),
                               ptc_ui_text(PTC_UI_T_EYE_CARE_REST_SUBTITLE),
                               2, 1, 60, draft->rest_minutes);
        } else if (ui->model.eye_care_field_focus == 3) {
            ptc_audio_play(PTC_SE_CONFIRM);
            submit_eye_care_policy(ui);
        } else if (ui->model.eye_care_field_focus == 4) {
            ptc_audio_play(PTC_SE_CONFIRM);
            submit_eye_care_skip(ui);
        }
    } else if (down & HidNpadButton_Minus) {
        ptc_audio_play(PTC_SE_TOGGLE);
        draft->enabled = !draft->enabled;
        ui->model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui->model);
    } else if (down & HidNpadButton_Plus) {
        ptc_audio_play(PTC_SE_CONFIRM);
        submit_eye_care_policy(ui);
    } else if (down & HidNpadButton_X) {
        if (resting) {
            ptc_audio_play(PTC_SE_CONFIRM);
            submit_eye_care_skip(ui);
        } else {
            ptc_audio_play(PTC_SE_ERROR);
        }
    } else if (down & HidNpadButton_Y) {
        ptc_audio_play(PTC_SE_CONFIRM);
        refresh_disable_flag(ui);
        submit_status(ui);
    }
}

static void handle_parent_page_input(UiState *ui, u64 down)
{
    if ((down & HidNpadButton_X) && ptc_ui_open_notice_details(&ui->model)) {
    } else if ((down & HidNpadButton_Plus) && ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        ptc_ui_open_home_details(&ui->model);
    } else if ((down & (HidNpadButton_ZL | HidNpadButton_ZR)) && ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        ptc_audio_play(PTC_SE_POPUP);
        open_language_overlay(ui);
    } else if (ui->model.parent_footer_focused) {
        if (down & HidNpadButton_B) {
            ptc_audio_play(PTC_SE_CANCEL);
            request_parent_navigation(ui, -1, true);
        } else if (down & HidNpadButton_L &&
                   !(ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
            ptc_audio_play(PTC_SE_TAB);
            request_parent_navigation(ui,
                (ui->model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
        } else if (down & HidNpadButton_R &&
                   !(ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
            ptc_audio_play(PTC_SE_TAB);
            request_parent_navigation(ui,
                (ui->model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
        } else if (down & HidNpadButton_Up) {
            ptc_audio_play(PTC_SE_FOCUS);
            ui->model.parent_footer_focused = false;
            ui->model.selected_index = ui->model.parent_content_selection;
        } else if (down & HidNpadButton_Left) {
            ptc_audio_play(PTC_SE_FOCUS);
            ui->model.parent_footer_selection = 0;
        } else if (down & HidNpadButton_Right) {
            ptc_audio_play(PTC_SE_FOCUS);
            ui->model.parent_footer_selection =
                ptc_ui_parent_status_alert_visible(&ui->model) ? 1 : 0;
        } else if (down & HidNpadButton_Y) {
            ptc_audio_play(PTC_SE_CONFIRM);
            refresh_disable_flag(ui);
            submit_status(ui);
        } else if (down & HidNpadButton_A) {
            ptc_audio_play(PTC_SE_CONFIRM);
            if (ui->model.parent_footer_selection == 0) submit_status(ui);
            else activate_parent_status(ui);
        }
    } else if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
               ui->model.plan_page == PTC_UI_PLAN_PAGE_WEEKLY) {
        handle_weekly_page_input(ui, down);
    } else if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
               ui->model.plan_page == PTC_UI_PLAN_PAGE_BEDTIME) {
        handle_bedtime_page_input(ui, down);
    } else if (ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.plan_page == PTC_UI_PLAN_PAGE_DOCK) {
        handle_dock_page_input(ui, down);
    } else if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
               ui->model.plan_page == PTC_UI_PLAN_PAGE_EYE_CARE) {
        handle_eye_care_page_input(ui, down);
    } else if (ui->waiting && ui->model.parent_page == PTC_UI_PARENT_PLAN &&
               ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
        if (down) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_NATIONAL_HOLIDAY_SETTINGS));
        }
    } else if (down & HidNpadButton_B) {
        ptc_audio_play(PTC_SE_CANCEL);
        request_parent_navigation(ui, -1, true);
    } else if (down & HidNpadButton_L &&
               !(ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
        ptc_audio_play(PTC_SE_TAB);
        request_parent_navigation(ui,
            (ui->model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
    } else if (down & HidNpadButton_R &&
               !(ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
        ptc_audio_play(PTC_SE_TAB);
        request_parent_navigation(ui,
            (ui->model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
    } else if (down & HidNpadButton_Left) {
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_move_parent_selection(&ui->model, -1, 0);
    } else if (down & HidNpadButton_Right) {
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_move_parent_selection(&ui->model, 1, 0);
    } else if (down & HidNpadButton_Up) {
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_move_parent_selection(&ui->model, 0, -1);
    } else if (down & HidNpadButton_Down) {
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_move_parent_selection(&ui->model, 0, 1);
    } else if (down & HidNpadButton_Y) {
        ptc_audio_play(PTC_SE_CONFIRM);
        refresh_disable_flag(ui);
        submit_status(ui);
    } else if (down & HidNpadButton_X && ui->model.parent_page == PTC_UI_PARENT_PLAN &&
               ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
        if (ui->model.disable_flag_present) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_IN_EMERGENCY_DEACTIVATION_THE_RULES_ARE_TEMPORARILY));
        } else if (ui->model.selected_index == 1 ||
                   (ui->model.selected_index != 2 && ui->model.holiday_last_rule == 0)) {
            ptc_audio_play(PTC_SE_TOGGLE);
            ui->model.holiday_last_rule = 0;
            ui->model.draft_holiday_rule.mode = ptc_ui_next_rule_mode(ui->model.draft_holiday_rule.mode);
            update_holiday_dirty(ui);
        } else {
            ptc_audio_play(PTC_SE_TOGGLE);
            ui->model.holiday_last_rule = 1;
            ui->model.draft_makeup_workday_rule.mode = ptc_ui_next_rule_mode(ui->model.draft_makeup_workday_rule.mode);
            update_holiday_dirty(ui);
        }
    } else if (down & HidNpadButton_Plus && ui->model.parent_page == PTC_UI_PARENT_PLAN &&
               ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.selected_index = 5;
        save_holiday_from_page(ui);
    } else if (down & HidNpadButton_ZL && ui->model.parent_page == PTC_UI_PARENT_PLAN &&
               ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
        ptc_audio_play(PTC_SE_CANCEL);
        ui->model.selected_index = 4;
        discard_holiday_draft(ui);
    } else if (down & HidNpadButton_A) {
        if (ui->waiting && !ptc_ui_parent_read_only_action(&ui->model, ui->model.selected_index)) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_CURRENT_OPERATION_IS));
        } else if (ui->model.parent_footer_focused) {
            ptc_audio_play(PTC_SE_CONFIRM);
            activate_parent_status(ui);
        } else if (ui->model.parent_page == PTC_UI_PARENT_SUPPORT &&
                   ui->model.selected_index == 6 + ui->model.recent_event_count) {
            ptc_audio_play(PTC_SE_POPUP);
            ptc_ui_open_support_guide(&ui->model);
        } else if (ui->model.parent_page == PTC_UI_PARENT_SUPPORT && ui->model.selected_index >= 6) {
            int visible_index = ui->model.selected_index - 6;
            int event_index = ui->model.recent_event_count - 1 - visible_index;
            if (event_index >= 0 && event_index < ui->model.recent_event_count) {
                ptc_audio_play(PTC_SE_POPUP);
                open_support_event(ui, event_index);
            }
        } else {
            ptc_audio_play(PTC_SE_CONFIRM);
            handle_parent_action(ui);
        }
    }
}

bool handle_page_input(UiState *ui, u64 down, u64 held,
                       bool custom_combo_candidate, bool parent_minus_released)
{
    bool running = true;
    if (ui->model.view == PTC_UI_CHILD) {
        if (custom_combo_candidate) {
            /* Do not let buttons from an in-progress custom chord reach
             * ordinary child-area actions before its hold decision. */
        } else if (down & HidNpadButton_B) {
            ptc_audio_play(PTC_SE_CANCEL);
            running = false;
        } else if (parent_minus_released) {
            ptc_audio_play(PTC_SE_CONFIRM);
            enter_parent_area(ui);
        } else if (down & HidNpadButton_A) {
            if (ui->waiting) {
                ptc_audio_play(PTC_SE_ERROR);
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_PLEASE_WAIT_FOR_THE_CURRENT_OPERATION_TO));
            } else {
                ptc_audio_play(PTC_SE_CONFIRM);
                open_offline_code_input(ui);
            }
        } else if (down & HidNpadButton_Y) {
            ptc_audio_play(PTC_SE_CONFIRM);
            submit_status_with_origin(ui, PTC_UI_STATUS_BUTTON_Y);
        } else if (down & HidNpadButton_Plus) {
            ptc_audio_play(PTC_SE_POPUP);
            ptc_ui_open_home_details(&ui->model);
        } else if (down & (HidNpadButton_ZL | HidNpadButton_ZR)) {
            ptc_audio_play(PTC_SE_POPUP);
            open_language_overlay(ui);
        } else if (down & HidNpadButton_X) {
            if (ui->model.disable_flag_present) {
                ptc_audio_play(PTC_SE_ERROR);
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_CONTROL_HAS_BEEN_DEACTIVATED_AND_AUTONOMOUS_BUFFERING));
            } else if (ui->model.daily_buffer_available && !ui->waiting) {
                ptc_audio_play(PTC_SE_CLAIM_BUFFER);
                submit_transport_empty(ui, "claim_daily_buffer",
                    ptc_ui_text(PTC_UI_T_IS_RECEIVING_TODAY_S_INDEPENDENT_BUFFER), ptc_ui_text(PTC_UI_T_FAILED_TO_RECEIVE_TODAY_S_INDEPENDENT_BUFFERING));
            } else if (ui->model.daily_buffer_claimed) {
                ptc_audio_play(PTC_SE_ERROR);
                snprintf(ui->model.message, sizeof(ui->model.message),
                    ptc_ui_text(PTC_UI_T_THE_BUFFER_HAS_BEEN_USED_TODAY_AND));
            } else {
                ptc_audio_play(PTC_SE_ERROR);
                snprintf(ui->model.message, sizeof(ui->model.message),
                    ptc_ui_text(PTC_UI_T_THERE_ARE_NO_AUTONOMOUS_BUFFERS_TO_CLAIM));
            }
        }
    } else if (ui->model.view == PTC_UI_SETUP) {
        if (down & HidNpadButton_Y) {
            ptc_audio_play(PTC_SE_CONFIRM);
            submit_status_with_origin(ui, PTC_UI_STATUS_BUTTON_Y);
        } else {
            handle_setup_input(ui, down, held);
        }
    } else if (ui->model.view == PTC_UI_ERROR) {
        if (down & HidNpadButton_A) {
            ptc_audio_play(PTC_SE_CONFIRM);
            retry_error(ui);
        } else if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
            ptc_audio_play(PTC_SE_CANCEL);
            enter_child_area(ui);
        }
    } else {
        handle_parent_page_input(ui, down);
    }
    return running;
}
