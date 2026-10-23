#include "nro_app_internal.h"

void handle_config_backup_input(UiState *ui, u64 down)
{
    int sel = ui->model.overlay_selection;
    if (down & HidNpadButton_B) {
        ptc_audio_play(PTC_SE_CANCEL);
        config_backup_action(ui, 15);
    } else if (down & HidNpadButton_Y) {
        ptc_audio_play(PTC_SE_CONFIRM);
        open_config_backup(ui);
    } else if (down & (HidNpadButton_L | HidNpadButton_ZL)) {
        ptc_audio_play(PTC_SE_FOCUS);
        ui->model.config_backup_tab = 0;
        if (sel != 15 && sel != 16 && sel != 17) ui->model.overlay_selection = 13;
    } else if (down & (HidNpadButton_R | HidNpadButton_ZR)) {
        ptc_audio_play(PTC_SE_FOCUS);
        ui->model.config_backup_tab = 1;
        if (sel != 15 && sel != 16 && sel != 17) ui->model.overlay_selection = 14;
    } else if (down & HidNpadButton_Up) {
        ptc_audio_play(PTC_SE_FOCUS);
        if (ui->model.config_backup_tab == 0) {
            if (sel == 16 || sel == 17) ui->model.overlay_selection = 15;
            else if (sel == 13) ui->model.overlay_selection = 16;
            else if (sel == 15) ui->model.overlay_selection = 13;
            else ui->model.overlay_selection = 13;
        } else {
            if (sel == 16 || sel == 17) ui->model.overlay_selection = 15;
            else if (sel == 0) ui->model.overlay_selection = 16;
            else if (sel == 1) ui->model.overlay_selection = 17;
            else if (sel == 2) ui->model.overlay_selection = 0;
            else if (sel == 3) ui->model.overlay_selection = 1;
            else if (sel == 4) ui->model.overlay_selection = 2;
            else if (sel == 5) ui->model.overlay_selection = 3;
            else if (sel == 6) ui->model.overlay_selection = 4;
            else if (sel == 7) ui->model.overlay_selection = 5;
            else if (sel == 8) ui->model.overlay_selection = 6;
            else if (sel == 9) ui->model.overlay_selection = 8;
            else if (sel == 10) ui->model.overlay_selection = 7;
            else if (sel == 11) ui->model.overlay_selection = 9;
            else if (sel == 12) ui->model.overlay_selection = 10;
            else if (sel == 14) ui->model.overlay_selection = 11;
            else if (sel == 15) ui->model.overlay_selection = 14;
            else ui->model.overlay_selection = 14;
        }
    } else if (down & HidNpadButton_Down) {
        ptc_audio_play(PTC_SE_FOCUS);
        if (ui->model.config_backup_tab == 0) {
            if (sel == 16 || sel == 17) ui->model.overlay_selection = 13;
            else if (sel == 13) ui->model.overlay_selection = 15;
            else if (sel == 15) ui->model.overlay_selection = 16;
            else ui->model.overlay_selection = 13;
        } else {
            if (sel == 16) ui->model.overlay_selection = 0;
            else if (sel == 17) ui->model.overlay_selection = 1;
            else if (sel == 0) ui->model.overlay_selection = 2;
            else if (sel == 1) ui->model.overlay_selection = 3;
            else if (sel == 2) ui->model.overlay_selection = 4;
            else if (sel == 3) ui->model.overlay_selection = 5;
            else if (sel == 4) ui->model.overlay_selection = 6;
            else if (sel == 5) ui->model.overlay_selection = 7;
            else if (sel == 6) ui->model.overlay_selection = 8;
            else if (sel == 7) ui->model.overlay_selection = 10;
            else if (sel == 8) ui->model.overlay_selection = 9;
            else if (sel == 9) ui->model.overlay_selection = 11;
            else if (sel == 10) ui->model.overlay_selection = 12;
            else if (sel == 11 || sel == 12) ui->model.overlay_selection = 14;
            else if (sel == 14) ui->model.overlay_selection = 15;
            else if (sel == 15) ui->model.overlay_selection = 16;
            else ui->model.overlay_selection = 14;
        }
    } else if (down & HidNpadButton_Left) {
        ptc_audio_play(PTC_SE_FOCUS);
        if (sel == 17) ui->model.overlay_selection = 16;
        else if (ui->model.config_backup_tab == 1) {
            if (sel == 1) ui->model.overlay_selection = 0;
            else if (sel == 3) ui->model.overlay_selection = 2;
            else if (sel == 5) ui->model.overlay_selection = 4;
            else if (sel == 7) ui->model.overlay_selection = 6;
            else if (sel == 10) ui->model.overlay_selection = 9;
            else if (sel == 12) ui->model.overlay_selection = 11;
            else if (sel == 14) ui->model.overlay_selection = 10;
        }
    } else if (down & HidNpadButton_Right) {
        ptc_audio_play(PTC_SE_FOCUS);
        if (sel == 16) ui->model.overlay_selection = 17;
        else if (ui->model.config_backup_tab == 1) {
            if (sel == 0) ui->model.overlay_selection = 1;
            else if (sel == 2) ui->model.overlay_selection = 3;
            else if (sel == 4) ui->model.overlay_selection = 5;
            else if (sel == 6) ui->model.overlay_selection = 7;
            else if (sel == 9) ui->model.overlay_selection = 10;
            else if (sel == 11) ui->model.overlay_selection = 12;
            else if (sel == 1 || sel == 3 || sel == 5 || sel == 7 || sel == 8 || sel == 10 || sel == 12) ui->model.overlay_selection = 14;
        }
    } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
        ptc_audio_play(PTC_SE_CONFIRM);
        if (sel == 16) {
            ui->model.config_backup_tab = 0;
            ui->model.overlay_selection = 13;
        } else if (sel == 17) {
            ui->model.config_backup_tab = 1;
            ui->model.overlay_selection = 14;
        } else {
            config_backup_action(ui, sel);
        }
    }
    return;
}

static void handle_activity_history_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
    else if (down & (HidNpadButton_L | HidNpadButton_Left)) {
        ptc_audio_play(PTC_SE_TAB);
        ptc_ui_change_activity_history_page(&ui->model, -1);
    } else if (down & (HidNpadButton_R | HidNpadButton_Right)) {
        ptc_audio_play(PTC_SE_TAB);
        ptc_ui_change_activity_history_page(&ui->model, 1);
    } else if (down & HidNpadButton_X) {
        request_clear_activity_history(ui);
    }
    return;
}

static void handle_redemption_history_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & (HidNpadButton_L | HidNpadButton_Left)) {
        ptc_audio_play(PTC_SE_TAB);
        ptc_ui_change_redemption_history_page(&ui->model, -1);
    } else if (down & (HidNpadButton_R | HidNpadButton_Right)) {
        ptc_audio_play(PTC_SE_TAB);
        ptc_ui_change_redemption_history_page(&ui->model, 1);
    } else if (down & HidNpadButton_X) {
        request_clear_redemption_history(ui);
    }
    return;
}

static void handle_theme_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & HidNpadButton_Left) {
        if (ui->model.overlay_selection < 3) {
            ui->model.overlay_selection = ui->model.overlay_selection <= 0 ? 2 : ui->model.overlay_selection - 1;
        } else {
            int style_idx = ui->model.overlay_selection - 3;
            style_idx = style_idx <= 0 ? 3 : style_idx - 1;
            ui->model.overlay_selection = 3 + style_idx;
        }
    } else if (down & HidNpadButton_Right) {
        if (ui->model.overlay_selection < 3) {
            ui->model.overlay_selection = (ui->model.overlay_selection + 1) % 3;
        } else {
            int style_idx = ui->model.overlay_selection - 3;
            style_idx = (style_idx + 1) % 4;
            ui->model.overlay_selection = 3 + style_idx;
        }
    } else if (down & HidNpadButton_Up) {
        if (ui->model.overlay_selection >= 3) {
            int style_idx = ui->model.overlay_selection - 3;
            ui->model.overlay_selection = style_idx < 3 ? style_idx : 2;
        }
    } else if (down & HidNpadButton_Down) {
        if (ui->model.overlay_selection < 3) {
            int mode_idx = ui->model.overlay_selection;
            ui->model.overlay_selection = 3 + mode_idx;
        }
    } else if (down & HidNpadButton_X) {
        PtcUiThemeStyle next_style = (ui->theme_style + 1) % PTC_UI_THEME_STYLE_COUNT;
        if (apply_theme_style(ui, next_style)) {
            snprintf(ui->model.message, sizeof(ui->model.message), "%s: %s",
                     ptc_ui_text(PTC_UI_T_APPEARANCE_THEME), ptc_ui_theme_style_label(next_style));
        }
    } else if (down & HidNpadButton_A) {
        if (ui->model.overlay_selection < 3) {
            PtcUiThemePreference preference = (PtcUiThemePreference)ui->model.overlay_selection;
            if (apply_theme_preference(ui, preference)) {
                ptc_ui_cancel_overlay(&ui->model);
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THEME_CHANGED_TO_S),
                         ptc_ui_theme_preference_label(preference));
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_THEME_SETTINGS_ARE_NOT_SAVED_AND));
            }
        } else {
            PtcUiThemeStyle style = (PtcUiThemeStyle)(ui->model.overlay_selection - 3);
            if (apply_theme_style(ui, style)) {
                ptc_ui_cancel_overlay(&ui->model);
                snprintf(ui->model.message, sizeof(ui->model.message), "%s: %s",
                         ptc_ui_text(PTC_UI_T_APPEARANCE_THEME), ptc_ui_theme_style_label(style));
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_THEME_SETTINGS_ARE_NOT_SAVED_AND));
            }
        }
    }
    return;
}

static void handle_language_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
        int col = ui->model.overlay_selection % 2;
        int row = ui->model.overlay_selection / 2;
        col = (col + 1) % 2;
        ui->model.overlay_selection = row * 2 + col;
    } else if (down & (HidNpadButton_Up | HidNpadButton_Down)) {
        int col = ui->model.overlay_selection % 2;
        int row = ui->model.overlay_selection / 2;
        row = (row + 1) % 2;
        ui->model.overlay_selection = row * 2 + col;
    } else if (down & HidNpadButton_A) {
        PtcUiLanguagePreference preference = (PtcUiLanguagePreference)ui->model.overlay_selection;
        if (apply_language_preference(ui, preference)) {
            ptc_ui_cancel_overlay(&ui->model);
            char localized_label[64];
            const char *label = ptc_ui_localize(ptc_ui_language_preference_label(preference),
                localized_label, sizeof(localized_label));
            PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("language", label)};
            (void)ptc_ui_text_format(PTC_UI_T_LANGUAGE_CHANGED, ui->model.message,
                sizeof(ui->model.message), args, 1);
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                ptc_ui_text(PTC_UI_T_LANGUAGE_SAVE_FAILED));
        }
    }
    return;
}

static void handle_album_manager_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & HidNpadButton_Y) {
        ptc_audio_play(PTC_SE_CONFIRM);
        refresh_album_restriction(ui);
        refresh_recovery_state(ui);
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_HOMEBREW_MENU_ADVANCED_ENTRY_STATUS_HAS_BEEN));
    } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
        ui->model.overlay_selection = 1 - ui->model.overlay_selection;
    } else if (down & HidNpadButton_A) {
        if (ui->model.overlay_selection == 0 && ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_OFF) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION, ptc_ui_text(PTC_UI_T_CONFIGURE_ADVANCED_ENTRY_TO_HOMEBREW_MENU),
                ptc_ui_text(PTC_UI_T_CURRENT_NOT_CONFIGURED_TARGET_ADVANCED_ENTRY_AVAILABLE));
        } else if (ui->model.overlay_selection == 1 && ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY, ptc_ui_text(PTC_UI_T_RESTORE_THE_ORIGINAL_STARTUP_MODE_2),
                ptc_ui_text(PTC_UI_T_WILL_RESTORE_THE_ORIGINAL_CONFIGURATION_ACCORDING_TO));
        } else if (ui->model.overlay_selection == 1 && ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_ANOMALY && ui->model.album_backup_valid) {
            char body[320];
            snprintf(body, sizeof(body), ptc_ui_text(PTC_UI_T_EXTERNAL_CHANGES_DETECTED_120S_CONTINUING_DISCARDS_THEM),
                     ui->model.album_restriction_detail[0] ? ui->model.album_restriction_detail : ptc_ui_text(PTC_UI_T_CONFIGURATION_IS_INCONSISTENT_WITH_RECORDS));
            open_danger_confirm_overlay(ui, PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY, ptc_ui_text(PTC_UI_T_FORCE_RESTORE_OF_TRUSTED_BACKUP), body);
        } else if (ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     ptc_ui_text(PTC_UI_T_THE_PORTAL_HAS_BEEN_CONFIGURED_EXTERNALLY_AND));
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_CURRENT_STATUS_DOES_NOT_ALLOW_THIS));
        }
    }
    return;
}

static void handle_software_info_overlay_input(UiState *ui, u64 down)
{
            if ((down & (HidNpadButton_A | HidNpadButton_Plus)) &&
                ui->model.hot_reload_status == PTC_UI_HOT_RELOAD_PENDING && !ui->model.parent_support_only) {
    #ifndef PLAYWISE_EDEN
                open_hot_reload_confirmation(ui);
    #endif
            } else if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus)) {
                ptc_ui_cancel_overlay(&ui->model);
            }
            return;
}

static void handle_auth_error_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        close_auth_error(ui, true);
    } else if ((down & (HidNpadButton_A | HidNpadButton_Plus)) &&
               ui->model.auth_cooldown_seconds <= 0) {
        AuthRetryAction action = ui->auth_retry_action;
        close_auth_error(ui, false);
        ui->auth_retry_action = AUTH_RETRY_NONE;
        dispatch_auth_retry(ui, action);
    } else if (down & HidNpadButton_X) {
        close_auth_error(ui, true);
        ui->model.overlay = PTC_UI_OVERLAY_SUPPORT_GUIDE;
        ui->model.support_guide_page = 0;
    }
    return;
}

static void handle_shortcut_manager_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
        refresh_custom_shortcut_label(ui);
    } else if (down & HidNpadButton_Up) {
        ui->model.setup_shortcut_index = ui->model.setup_shortcut_index <= 0
            ? PTC_UI_SHORTCUT_PRESET_COUNT - 1 : ui->model.setup_shortcut_index - 1;
    } else if (down & HidNpadButton_Down) {
        ui->model.setup_shortcut_index = (ui->model.setup_shortcut_index + 1) % PTC_UI_SHORTCUT_PRESET_COUNT;
    } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
        ui->model.setup_shortcut_index = (ui->model.setup_shortcut_index + 7) % PTC_UI_SHORTCUT_PRESET_COUNT;
    } else if (down & HidNpadButton_A) {
        select_setup_shortcut(ui, ui->model.setup_shortcut_index);
    } else if (down & HidNpadButton_Y) {
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.shortcut_draft_show_hint = !ui->model.shortcut_draft_show_hint;
    } else if (down & HidNpadButton_ZL) {
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.shortcut_draft_enabled = false;
    } else if (down & HidNpadButton_Plus) {
        if (commit_shortcut_preferences(ui)) {
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                     ui->model.custom_shortcut_enabled
                        ? ptc_ui_text(PTC_UI_T_THE_PARENT_AREA_SHORTCUT_KEYS_HAVE_BEEN)
                        : ptc_ui_text(PTC_UI_T_CUSTOM_SHORTCUT_KEYS_TURNED_OFF_FIXED_MINUS));
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_SHORTCUT_KEY_SETTINGS_ARE_NOT_SAVED));
        }
    }
    return;
}

static void handle_credential_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        if (strcmp(ui->model.credential_current, ui->model.credential_new) != 0) {
            ui->model.overlay = PTC_UI_OVERLAY_CREDENTIAL_LEAVE;
            ui->model.overlay_selection = 1;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_GIVE_UP_MODIFYING_PAIRING_INFORMATION));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                     ptc_ui_text(PTC_UI_T_MANUAL_INPUT_AND_RANDOM_GENERATION_ONLY_MODIFY));
        } else {
            show_grant_manager(ui, ui->model.credential_kind == 1
                ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
        }
    } else if (down & (HidNpadButton_Up | HidNpadButton_Left)) {
        ptc_ui_move_overlay_selection(&ui->model, -1, 0);
    } else if (down & (HidNpadButton_Down | HidNpadButton_Right)) {
        ptc_ui_move_overlay_selection(&ui->model, 1, 0);
    } else if (down & HidNpadButton_X) {
        ptc_audio_play(PTC_SE_KEYSTROKE);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
        edit_credential_input(ui);
    } else if (down & HidNpadButton_ZR) {
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_REVEAL;
        reveal_current_credential(ui);
    } else if (down & HidNpadButton_Y) {
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_RANDOM;
        randomize_credential(ui);
    } else if ((down & HidNpadButton_R) && ui->model.credential_kind == 2) {
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_DEMO;
        if (ptc_grant_secret_is_demo(ui->model.credential_current)) randomize_credential(ui);
        else snprintf(ui->model.credential_new, sizeof(ui->model.credential_new), "%s", PTC_DEMO_GRANT_SECRET);
        ui->model.credential_new_revealed = true;
    } else if (down & HidNpadButton_A) {
        if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_RANDOM) randomize_credential(ui);
        else if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_REVEAL && ui->model.credential_kind == 2) {
            handle_overlay_input(ui, HidNpadButton_ZR);
        } else if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_DEMO && ui->model.credential_kind == 2) {
            handle_overlay_input(ui, HidNpadButton_R);
        } else if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_SAVE) request_save_credential(ui);
        else edit_credential_input(ui);
    } else if (down & HidNpadButton_Plus) {
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_SAVE;
        request_save_credential(ui);
    }
    return;
}

static void handle_credential_leave_overlay_input(UiState *ui, u64 down)
{
    if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
        ui->model.overlay_selection = ui->model.overlay_selection == 0 ? 1 : 0;
    } else if (down & (HidNpadButton_B | HidNpadButton_A)) {
        if ((down & HidNpadButton_A) && ui->model.overlay_selection == 0) {
            show_grant_manager(ui, ui->model.credential_kind == 1
                ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_UNSAVED_PAIRING_INFORMATION_MODIFICATIONS_WERE_ABANDONED));
        } else {
            ptc_ui_cancel_overlay(&ui->model);
        }
    } else if (down & HidNpadButton_X) {
        ptc_audio_play(PTC_SE_CANCEL);
        show_grant_manager(ui, ui->model.credential_kind == 1
            ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_UNSAVED_PAIRING_INFORMATION_MODIFICATIONS_WERE_ABANDONED));
    }
    return;
}

static void handle_grant_manager_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
    else if (down & HidNpadButton_Left) ptc_ui_move_overlay_selection(&ui->model, -1, 0);
    else if (down & HidNpadButton_Right) ptc_ui_move_overlay_selection(&ui->model, 1, 0);
    else if (down & HidNpadButton_Up) ptc_ui_move_overlay_selection(&ui->model, 0, -1);
    else if (down & HidNpadButton_Down) ptc_ui_move_overlay_selection(&ui->model, 0, 1);
    else if (down & HidNpadButton_A) {
        if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_DEVICE) open_credential_manager(ui, 1);
        else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_SECRET) open_credential_manager(ui, 2);
        else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_EXPORT) export_parent_import(ui);
        else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_EDIT_URL) edit_pairing_base_url(ui);
        else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_RESET_URL) request_reset_pairing_base_url(ui);
    }
    return;
}

static void handle_grant_local_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & HidNpadButton_Up) {
        ui->model.overlay_selection = ui->model.overlay_selection == PTC_UI_GRANT_LOCAL_BACK
            ? PTC_UI_GRANT_LOCAL_GENERATE : (ui->model.overlay_selection == PTC_UI_GRANT_LOCAL_GENERATE
                ? PTC_UI_GRANT_LOCAL_ADJUST_FIRST : PTC_UI_GRANT_LOCAL_BACK);
    } else if (down & HidNpadButton_Down) {
        ui->model.overlay_selection = ui->model.overlay_selection == PTC_UI_GRANT_LOCAL_ADJUST_FIRST
            ? PTC_UI_GRANT_LOCAL_GENERATE : (ui->model.overlay_selection == PTC_UI_GRANT_LOCAL_GENERATE
                ? PTC_UI_GRANT_LOCAL_BACK : PTC_UI_GRANT_LOCAL_ADJUST_FIRST);
    }
    else if (down & HidNpadButton_Plus) {
        ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
        generate_local_grant_code(ui);
    } else if (down & HidNpadButton_A) {
        int selection = ui->model.overlay_selection;
        if (selection == PTC_UI_GRANT_LOCAL_ADJUST_FIRST) edit_grant_minutes(ui);
        else if (selection == PTC_UI_GRANT_LOCAL_BACK) handle_overlay_input(ui, HidNpadButton_B);
        else generate_local_grant_code(ui);
    }
    return;
}

static void handle_qr_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
    else if (down & HidNpadButton_A) export_parent_import(ui);
    return;
}

static void handle_parent_export_result_overlay_input(UiState *ui, u64 down)
{
    if (down & (HidNpadButton_A | HidNpadButton_B)) {
        ui->model.overlay = ui->export_return_overlay;
        ui->model.overlay_selection = PTC_UI_GRANT_MANAGER_EXPORT;
    }
    return;
}

bool handle_account_overlay_input(UiState *ui, u64 down)
{
    switch (ui->model.overlay) {
    case PTC_UI_OVERLAY_ACTIVITY_HISTORY:
        handle_activity_history_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_REDEMPTION_HISTORY:
        handle_redemption_history_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_THEME:
        handle_theme_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_LANGUAGE:
        handle_language_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_ALBUM_MANAGER:
        handle_album_manager_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_SOFTWARE_INFO:
        handle_software_info_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_AUTH_ERROR:
        handle_auth_error_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
        handle_shortcut_manager_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_CREDENTIAL:
        handle_credential_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_CREDENTIAL_LEAVE:
        handle_credential_leave_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_GRANT_MANAGER:
        handle_grant_manager_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_GRANT_LOCAL:
        handle_grant_local_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_QR:
        handle_qr_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_PARENT_EXPORT_RESULT:
        handle_parent_export_result_overlay_input(ui, down);
        return true;
    default:
        return false;
    }
}
