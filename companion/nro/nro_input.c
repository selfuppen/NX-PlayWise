#include "nro_app_internal.h"

void handle_overlay_input(UiState *ui, u64 down)
{
    if (handle_parent_auth_error_input(ui, down)) return;
    if (ui->model.overlay == PTC_UI_OVERLAY_ONLINE_UPDATE || ui->model.overlay == PTC_UI_OVERLAY_UPDATE_BACKUPS) {
        handle_online_update_input(ui, down);
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CONFIG_BACKUP) {
        handle_config_backup_input(ui, down);
        return;
    }
    if (down & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right)) {
        ptc_audio_play(PTC_SE_FOCUS);
    } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
        ptc_audio_play(PTC_SE_CONFIRM);
    } else if (down & HidNpadButton_B) {
        ptc_audio_play(PTC_SE_CANCEL);
    }

    if (handle_plan_overlay_input(ui, down) || handle_account_overlay_input(ui, down)) return;

    if (ui->model.overlay == PTC_UI_OVERLAY_SUPPORT_GUIDE) {
        if (down & (HidNpadButton_B | HidNpadButton_A)) {
            ptc_audio_play(PTC_SE_CANCEL);
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & (HidNpadButton_L | HidNpadButton_Left)) {
            ptc_audio_play(PTC_SE_STEP);
            ptc_ui_support_guide_change_page(&ui->model, -1);
        } else if (down & (HidNpadButton_R | HidNpadButton_Right)) {
            ptc_audio_play(PTC_SE_STEP);
            ptc_ui_support_guide_change_page(&ui->model, 1);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_NOTICE_DETAILS ||
        ui->model.overlay == PTC_UI_OVERLAY_SETUP_PCTL_HELP) {
        if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Minus)) {
            ptc_audio_play(PTC_SE_CANCEL);
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            ui->model.overlay_title[0] = '\0';
            ui->model.overlay_body[0] = '\0';
        }
        return;
    }

    if (ui->model.overlay == PTC_UI_OVERLAY_HOME_DETAILS) {
        if (ui->model.rules_dialog_open) {
            if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_X | HidNpadButton_Plus)) {
                ptc_audio_play(PTC_SE_CANCEL);
                ui->model.rules_dialog_open = false;
            }
            return;
        }
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
            ptc_audio_play(PTC_SE_CANCEL);
            ptc_ui_home_details_back(&ui->model);
        } else if (down & HidNpadButton_X) {
            if (ui->model.home_details_page == 0) {
                ptc_audio_play(PTC_SE_POPUP);
                ptc_ui_home_details_activate(&ui->model, 0);
            } else if (ui->model.home_details_page == 1) {
                ptc_audio_play(PTC_SE_POPUP);
                ui->model.rules_dialog_open = true;
            }
        } else if (down & HidNpadButton_Y) {
            if (!ui->waiting) {
                ptc_audio_play(PTC_SE_CONFIRM);
                submit_status(ui);
            }
        } else if (down & (HidNpadButton_ZL | HidNpadButton_ZR)) {
            ptc_audio_play(PTC_SE_STEP);
            ptc_ui_home_details_scroll(&ui->model, down & HidNpadButton_ZR ? 200 : -200);
        } else if (down & (HidNpadButton_Up | HidNpadButton_Left)) {
            ptc_audio_play(PTC_SE_FOCUS);
            ptc_ui_home_details_move(&ui->model, -1);
        } else if (down & (HidNpadButton_Down | HidNpadButton_Right)) {
            ptc_audio_play(PTC_SE_FOCUS);
            ptc_ui_home_details_move(&ui->model, 1);
        } else if (down & HidNpadButton_A) {
            int focus = ui->model.home_details_focus;
            if (focus == 2) {
                ptc_audio_play(PTC_SE_CANCEL);
                ptc_ui_home_details_back(&ui->model);
            } else if (focus == 1) {
                if (!ui->waiting) {
                    ptc_audio_play(PTC_SE_CONFIRM);
                    submit_status(ui);
                }
            } else if (focus == 0) {
                ptc_audio_play(PTC_SE_POPUP);
                ptc_ui_home_details_activate(&ui->model, 0);
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_DAY_DECISION) {
        if (ui->model.rules_dialog_open) {
            if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_X | HidNpadButton_Plus)) {
                ptc_audio_play(PTC_SE_CANCEL);
                ui->model.rules_dialog_open = false;
            }
            return;
        }
        if (down & HidNpadButton_X) {
            ptc_audio_play(PTC_SE_POPUP);
            ui->model.rules_dialog_open = true;
            return;
        }
        if (down & (HidNpadButton_Left | HidNpadButton_L | HidNpadButton_ZL)) {
            if (ui->model.forecast_detail_day_offset > 0) {
                ui->model.forecast_detail_day_offset--;
                ui->model.overlay_selection = ui->model.forecast_detail_day_offset;
                ptc_audio_play(PTC_SE_POPUP);
            }
        } else if (down & (HidNpadButton_Right | HidNpadButton_R | HidNpadButton_ZR)) {
            if (ui->model.forecast_detail_day_offset < (int)PTC_RESULT_FORECAST_DAYS - 1) {
                ui->model.forecast_detail_day_offset++;
                ui->model.overlay_selection = ui->model.forecast_detail_day_offset;
                ptc_audio_play(PTC_SE_POPUP);
            }
        } else if (down & HidNpadButton_Y) {
            if (!ui->waiting) {
                ptc_audio_play(PTC_SE_CONFIRM);
                submit_status(ui);
            }
        } else if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus)) {
            ptc_audio_play(PTC_SE_CANCEL);
            ptc_ui_cancel_overlay(&ui->model);
        }
        return;
    }

    if (ui->model.overlay == PTC_UI_OVERLAY_SUPPORT_EVENT) {
        if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus)) {
            ptc_ui_cancel_overlay(&ui->model);
        }
        return;
    }

    if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
        if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus)) {
            close_code_result(ui);
        }
        return;
    }
    if (down & HidNpadButton_B) {
        ui->today_limit_refresh_pending = false;
        ui->today_limit_save_pending = false;
        ptc_ui_cancel_overlay(&ui->model);
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_MODIFICATION_CANCELED));
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_NUMPAD ||
        ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
        bool today_mode = ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR &&
            ui->model.numpad_purpose == PTC_UI_NUMPAD_MINUTES &&
            ui->model.operation == PTC_UI_OPERATION_SET_TODAY_LIMIT;
        if (today_mode && ui->today_limit_refresh_pending) return;
        if (today_mode && (down & HidNpadButton_StickR)) {
            ptc_audio_play(PTC_SE_CONFIRM);
            refresh_today_limit_editor(ui, false);
        } else if (today_mode && (down & (HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_L | HidNpadButton_R))) {
            ui->model.today_limit_unlimited_draft = (down & (HidNpadButton_ZR | HidNpadButton_R)) != 0;
            ptc_audio_play(PTC_SE_TOGGLE);
        } else if (today_mode && ui->model.today_limit_unlimited_draft &&
                   (down & (HidNpadButton_X | HidNpadButton_Left))) {
            ui->model.today_limit_unlimited_draft = false;
            ptc_audio_play(PTC_SE_TOGGLE);
        } else if (today_mode && ui->model.today_limit_unlimited_draft &&
                   (down & (HidNpadButton_A | HidNpadButton_Plus))) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_DISABLE_TODAY_LIMIT,
                ptc_ui_text(PTC_UI_T_MAKE_TODAY_UNLIMITED), ptc_ui_text(PTC_UI_T_THERE_IS_NO_DAILY_QUOTA_LIMIT_TODAY));
        } else if (today_mode && ui->model.today_limit_unlimited_draft) {
            /* Keep the previously entered limited value for a mode switch back. */
        } else if (ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR && (down & HidNpadButton_Minus)) {
            ptc_ui_duration_toggle_field(&ui->model);
            ptc_audio_play(PTC_SE_TOGGLE);
        } else if (down & HidNpadButton_Left) {
            ptc_ui_numpad_move(&ui->model, -1, 0);
        } else if (down & HidNpadButton_Right) {
            ptc_ui_numpad_move(&ui->model, 1, 0);
        } else if (down & HidNpadButton_Up) {
            ptc_ui_numpad_move(&ui->model, 0, -1);
        } else if (down & HidNpadButton_Down) {
            ptc_ui_numpad_move(&ui->model, 0, 1);
        } else if (down & HidNpadButton_X) {
            ptc_audio_play(PTC_SE_KEYSTROKE);
            ptc_ui_numpad_backspace(&ui->model);
        } else if (down & HidNpadButton_Y) {
            ptc_audio_play(PTC_SE_KEYSTROKE);
            ptc_ui_numpad_clear(&ui->model);
        } else if (down & HidNpadButton_A) {
            ptc_audio_play(PTC_SE_KEYSTROKE);
            ptc_ui_numpad_activate(&ui->model);
        } else if (down & HidNpadButton_Plus) {
            PtcUiNumpadPurpose purpose = ui->model.numpad_purpose;
            PtcUiOperation operation = ui->model.operation;
            uint16_t value = 0;
            if (!ptc_ui_numpad_validate(&ui->model, &value)) return;
            if (today_mode) {
                ui->model.draft_minutes = value;
                refresh_today_limit_editor(ui, true);
                return;
            }
            accept_numpad(ui);
            if (purpose == PTC_UI_NUMPAD_MINUTES) {
                if (operation == PTC_UI_OPERATION_SET_TODAY_LIMIT) {
                    if (ptc_ui_limit_minutes_would_restrict(&ui->model, value)) {
                        char body[192];
                        snprintf(body, sizeof(body),
                                 ptc_ui_text(PTC_UI_T_ESTIMATED_USAGE_ABOUT_D_MIN_SET_TOTAL),
                                 ui->model.played_minutes, (unsigned int)value);
                        open_danger_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_THE_NEW_QUOTA_IS_NOT_HIGHER_THAN), body);
                    } else if (!ui->model.played_minutes_available || ui->model.played_minutes < 0) {
                        char body[192];
                        snprintf(body, sizeof(body),
                                 ptc_ui_text(PTC_UI_T_USAGE_ESTIMATE_UNAVAILABLE_SET_TOTAL_QUOTA_U),
                                 (unsigned int)value);
                        open_danger_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_UNABLE_TO_CONFIRM_WHETHER_TO_LIMIT_IMMEDIATELY), body);
                    } else if (ui->model.unrestricted_today == 1) {
                        char body[192];
                        int preview = (int)value - ui->model.played_minutes;
                        if (preview < 0) preview = 0;
                        snprintf(body, sizeof(body),
                                 ptc_ui_text(PTC_UI_T_TODAY_IS_UNLIMITED_A_U_MIN_TOTAL_2),
                                 (unsigned int)value, preview);
                        open_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_UNLIMITED_TIME_WILL_BE_CHANGED_TO_LIMITED_2), body);
                    } else {
                        ui->model.operation = PTC_UI_OPERATION_NONE;
                        submit_minutes(ui, operation, value);
                    }
                } else if (operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
                    char body[192];
                    snprintf(body, sizeof(body), ptc_ui_text(PTC_UI_T_ADD_U_MIN_TO_TODAY_S_CURRENT),
                        (unsigned int)value);
                    open_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_CONFIRM_RAPID_GRANT), body);
                } else {
                    ui->model.operation = PTC_UI_OPERATION_NONE;
                    submit_minutes(ui, operation, value);
                }
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_MINUTES) {
        if (down & HidNpadButton_Up) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Down) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Right) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Left) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Y) {
            ptc_audio_play(PTC_SE_CONFIRM);
            edit_overlay_minutes(ui);
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            PtcUiOperation operation = ui->model.operation;
            if (operation == PTC_UI_OPERATION_SET_TODAY_LIMIT) {
                if (ptc_ui_limit_minutes_would_restrict(&ui->model, ui->model.draft_minutes)) {
                    char body[192];
                    snprintf(body, sizeof(body),
                             ptc_ui_text(PTC_UI_T_ESTIMATED_USAGE_ABOUT_D_MIN_SET_TOTAL_2),
                             ui->model.played_minutes, (unsigned int)ui->model.draft_minutes);
                    open_danger_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_THE_NEW_QUOTA_IS_NOT_HIGHER_THAN), body);
                } else if (!ui->model.played_minutes_available || ui->model.played_minutes < 0) {
                    char body[192];
                    snprintf(body, sizeof(body),
                             ptc_ui_text(PTC_UI_T_USAGE_ESTIMATE_UNAVAILABLE_SET_TOTAL_QUOTA_U),
                             (unsigned int)ui->model.draft_minutes);
                    open_danger_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_UNABLE_TO_CONFIRM_WHETHER_TO_LIMIT_IMMEDIATELY), body);
                } else if (ui->model.unrestricted_today == 1) {
                    char body[192];
                    int preview = (int)ui->model.draft_minutes - ui->model.played_minutes;
                    if (preview < 0) preview = 0;
                    snprintf(body, sizeof(body),
                             ptc_ui_text(PTC_UI_T_ESTIMATED_USAGE_D_MIN_NEW_QUOTA_U),
                             ui->model.played_minutes, (unsigned int)ui->model.draft_minutes, preview);
                    open_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_UNLIMITED_TIME_WILL_BE_CHANGED_TO_LIMITED_2), body);
                } else {
                    ui->model.overlay = PTC_UI_OVERLAY_NONE;
                    ui->model.operation = PTC_UI_OPERATION_NONE;
                    submit_minutes(ui, operation, ui->model.draft_minutes);
                }
            } else {
                ui->model.overlay = PTC_UI_OVERLAY_NONE;
                ui->model.operation = PTC_UI_OPERATION_NONE;
                submit_minutes(ui, operation, ui->model.draft_minutes);
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_WEEKLY) {
        PtcDayRule *day = &ui->model.draft_week[ui->model.editor_index];
        if (down & HidNpadButton_Left) {
            ui->model.editor_index = ui->model.editor_index <= 0 ? 6 : ui->model.editor_index - 1;
        } else if (down & HidNpadButton_Right) {
            ui->model.editor_index = ui->model.editor_index >= 6 ? 0 : ui->model.editor_index + 1;
        } else if ((down & (HidNpadButton_X | HidNpadButton_Up | HidNpadButton_Down |
                           HidNpadButton_Y | HidNpadButton_A | HidNpadButton_Plus)) &&
                    weekly_editing_blocked(ui)) {
            return;
        } else if (down & HidNpadButton_X) {
            ptc_audio_play(PTC_SE_TOGGLE);
            day->mode = ptc_ui_next_rule_mode(day->mode);
        } else if ((down & HidNpadButton_Up) && day->mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            day->minutes = ptc_ui_adjust_minutes(day->minutes, 15, 1, 1440);
        } else if ((down & HidNpadButton_Down) && day->mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            day->minutes = ptc_ui_adjust_minutes(day->minutes, -15, 1, 1440);
        } else if ((down & HidNpadButton_Y) && day->mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_CONFIRM);
            edit_weekly_minutes(ui);
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            save_weekly_from_page(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM) {
        bool album_change = ui->model.operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
                            ui->model.operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
                            ui->model.operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY;
        if ((down & HidNpadButton_Y) &&
            quota_operation_needs_recheck(ui->model.operation)) {
            ptc_audio_play(PTC_SE_CONFIRM);
            start_quota_recheck(ui, true);
        } else if (down & HidNpadButton_B) {
            ui->quota_recheck_ready = false;
            if (ui->model.operation == PTC_UI_OPERATION_SAVE_BEDTIME)
                cancel_bedtime_navigation(ui);
            if (ui->model.operation == PTC_UI_OPERATION_SAVE_WEEKLY) {
                ui->pending_parent_page = -1;
                ui->pending_leave_parent = false;
            }
            ptc_ui_cancel_overlay(&ui->model);
        } else if (album_change && (down & (HidNpadButton_Left | HidNpadButton_Right))) {
            ui->model.overlay_selection = 1 - ui->model.overlay_selection;
        } else if (album_change && (down & HidNpadButton_A) && ui->model.overlay_selection == 0) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if ((down & (HidNpadButton_A | HidNpadButton_Plus)) &&
                   (!album_change || ui->model.overlay_selection == 1)) {
            confirm_operation(ui);
        }
    }
}
