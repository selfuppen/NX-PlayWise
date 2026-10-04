#include "nro_app_internal.h"

void refresh_setup_activation(UiState *ui)
{
    int64_t now;
    if (!ui || ui->waiting || (ui->model.view != PTC_UI_SETUP &&
        !(ui->model.setup_wizard_completed && strcmp(ui->model.setup_phase, "released") == 0))) {
        return;
    }
    now = (int64_t)time(NULL);
    if (ptc_ui_setup_grace_remaining(&ui->model, now) != 0 ||
        ui->last_setup_refresh_second == now) {
        return;
    }
    ui->last_setup_refresh_second = now;
    submit_status(ui);
}

static void enter_parent_area_unlocked(UiState *ui)
{
    if (!ui) {
        return;
    }
    ui->model.parent_support_only = false;
    refresh_disable_flag(ui);
    refresh_security_state(ui);
    ui->model.view = PTC_UI_PARENT;
    ui->model.parent_page = ui->model.setup_phase[0] && strcmp(ui->model.setup_phase, "active") != 0
        ? PTC_UI_PARENT_SUPPORT : PTC_UI_PARENT_TODAY;
    ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
    ui->model.selected_index = 0;
    snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_PARENTAL_AREA_UNLOCKED_TO_ENTER_THE_KIDS));
#ifndef PLAYWISE_EDEN
    ptc_hot_reload_inspect(&ui->hot_reload);
    sync_hot_reload_model(ui);
    if (ui->hot_reload.status == PTC_HOT_RELOAD_PENDING && !ui->hot_reload_prompted) {
        ui->hot_reload_prompted = true;
        open_hot_reload_confirmation(ui);
        return;
    }
#endif
    if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        submit_status(ui);
    }
}

void enter_parent_area(UiState *ui)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    char pin_confirm[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus state = ptc_companion_auth_state(&ui->auth);
    ui->auth_retry_action = AUTH_RETRY_ENTER_PARENT;
    if (state == PTC_AUTH_EMPTY) {
        if (!pin_input(ui, ptc_ui_text(PTC_UI_T_SET_PLAYWISE_PIN), ptc_ui_text(PTC_UI_T_JOYSTICK_DIRECTION_INPUT_X_0_Y_9_2), pin, sizeof(pin)) ||
            !pin_input(ui, ptc_ui_text(PTC_UI_T_CONFIRM_PLAYWISE_PIN), ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THE_SAME_PIN_AGAIN_YOUR), pin_confirm, sizeof(pin_confirm))) {
            ui->auth_retry_action = AUTH_RETRY_NONE;
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_PIN_SETUP_CANCELED));
            return;
        }
        if (strcmp(pin, pin_confirm) != 0) {
            show_auth_error(ui, ptc_ui_text(PTC_UI_T_TWO_PINS_ARE_INCONSISTENT), ptc_ui_text(PTC_UI_T_THE_PINS_ENTERED_TWICE_ARE_INCONSISTENT_AND), 0);
            return;
        }
        state = ptc_companion_auth_set_pin(&ui->auth, pin, time(NULL), switch_random, NULL);
        if (state != PTC_AUTH_OK) {
            show_auth_error(ui, ptc_ui_text(PTC_UI_T_PIN_SETTING_FAILED), auth_status_zh(state), 0);
            return;
        }
    } else if (state != PTC_AUTH_OK) {
        show_auth_error(ui, ptc_ui_text(PTC_UI_T_UNABLE_TO_ENTER_PARENT_AREA), auth_status_zh(state), 0);
        return;
    }
    if (!pin_input(ui, ptc_ui_text(PTC_UI_T_PLAYWISE_PIN), ptc_ui_text(PTC_UI_T_JOYSTICK_DIRECTION_INPUT_X_0_Y_9_2), pin, sizeof(pin))) {
        ui->auth_retry_action = AUTH_RETRY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_ACCESS_TO_THE_PARENT_AREA_HAS_BEEN));
        return;
    }
    {
        int64_t retry_after = 0;
        state = ptc_companion_auth_verify_pin(&ui->auth, pin, (int64_t)time(NULL), &retry_after);
        if (state == PTC_AUTH_COOLDOWN && retry_after > 0) {
            show_auth_error(ui, ptc_ui_text(PTC_UI_T_PIN_TEMPORARILY_LOCKED), ptc_ui_text(PTC_UI_T_TOO_MANY_PIN_ERRORS_PLEASE_WAIT_FOR), retry_after);
            return;
        }
    }
    if (state != PTC_AUTH_OK) {
        show_auth_error(ui, ptc_ui_text(PTC_UI_T_PIN_VERIFICATION_FAILED),
                        state == PTC_AUTH_DENIED ? ptc_ui_text(PTC_UI_T_PIN_IS_INCORRECT_PLEASE_TRY_AGAIN) : auth_status_zh(state), 0);
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_NONE;
    ui->setup_parent_authorized = true;
    enter_parent_area_unlocked(ui);
    if (strlen(pin) < 4U) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 ptc_ui_text(PTC_UI_T_THE_PARENT_AREA_HAS_BEEN_UNLOCKED_THE));
    }
}

void select_setup_shortcut(UiState *ui, int index)
{
    if (!ui || index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        return;
    }
    ui->model.setup_shortcut_index = index;
    ui->model.shortcut_draft_mask = shortcut_preset_mask(index);
    ui->model.shortcut_draft_enabled = true;
    refresh_shortcut_draft_label(ui);
    snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_SHORTCUT_TO_CONFIRM_S_PRESS_TO_APPLY),
             ui->model.shortcut_draft_label);
}

bool commit_shortcut_preferences(UiState *ui)
{
    u64 old_mask;
    bool old_enabled;
    bool old_hint;
    if (!ui || !shortcut_mask_valid(ui->model.shortcut_draft_mask)) return false;
    old_mask = ui->model.custom_shortcut_mask;
    old_enabled = ui->model.custom_shortcut_enabled;
    old_hint = ui->model.show_parent_shortcut_hint;
    ui->model.custom_shortcut_mask = ui->model.shortcut_draft_mask;
    ui->model.custom_shortcut_enabled = ui->model.shortcut_draft_enabled;
    ui->model.show_parent_shortcut_hint = ui->model.shortcut_draft_show_hint;
    refresh_custom_shortcut_label(ui);
    if (save_ui_preferences(ui)) return true;
    if (ui->model.view == PTC_UI_SETUP) {
        ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_SHORTCUT, PTC_ERR_STORAGE_WRITE_FAILED);
        ui->model.overlay = PTC_UI_OVERLAY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(PTC_UI_T_SETUP_SAVE_SESSION_ONLY));
        return false;
    }
    ui->model.custom_shortcut_mask = old_mask;
    ui->model.custom_shortcut_enabled = old_enabled;
    ui->model.show_parent_shortcut_hint = old_hint;
    refresh_custom_shortcut_label(ui);
    return false;
}

void open_shortcut_manager(UiState *ui)
{
    if (!ui) return;
    ui->model.shortcut_draft_mask = ui->model.custom_shortcut_mask;
    ui->model.shortcut_draft_enabled = ui->model.custom_shortcut_enabled;
    ui->model.shortcut_draft_show_hint = ui->model.show_parent_shortcut_hint;
    refresh_shortcut_draft_label(ui);
    ui->model.overlay = PTC_UI_OVERLAY_SHORTCUT_MANAGER;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_PARENT_AREA_SHORTCUT_KEY_MANAGEMENT));
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             ptc_ui_text(PTC_UI_T_THE_CUSTOM_COMBINATION_REQUIRES_LONG_PRESSING_FOR));
}

void setup_pin(UiState *ui)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    char pin_confirm[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus state;
    if (!ui) {
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_SETUP_PIN;
    state = ptc_companion_auth_state(&ui->auth);
    ui->model.setup_pin_ready = state == PTC_AUTH_OK;
    if (state == PTC_AUTH_OK) {
        if (!ui->setup_parent_authorized &&
            !verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_BEFORE_CHANGING_YOUR_PIN_PLEASE_ENTER_YOUR))) return;
        ui->setup_parent_authorized = true;
        if (!pin_input(ui, ptc_ui_text(PTC_UI_T_MODIFY_DEFAULT_PIN), ptc_ui_text(PTC_UI_T_ENTER_NEW_1_TO_64_DIGIT_NUMBER),
                       pin, sizeof(pin)) ||
            !pin_input(ui, ptc_ui_text(PTC_UI_T_CONFIRM_NEW_PIN), ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THE_SAME_PIN_AGAIN_YOUR),
                       pin_confirm, sizeof(pin_confirm))) {
            ui->auth_retry_action = AUTH_RETRY_NONE;
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_CURRENT_PIN_RESERVED));
            return;
        }
        if (strcmp(pin, pin_confirm) != 0) {
            show_auth_error(ui, ptc_ui_text(PTC_UI_T_TWO_PINS_ARE_INCONSISTENT), ptc_ui_text(PTC_UI_T_THE_NEW_PINS_ENTERED_TWICE_ARE_INCONSISTENT_2), 0);
            return;
        }
        state = ptc_companion_auth_set_pin(&ui->auth, pin, time(NULL), switch_random, NULL);
        if (state != PTC_AUTH_OK) {
            show_auth_error(ui, ptc_ui_text(PTC_UI_T_PIN_MODIFICATION_FAILED), auth_status_zh(state), 0);
            return;
        }
        ui->model.setup_pin_ready = true;
        ui->auth_retry_action = AUTH_RETRY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 strlen(pin) < 4U ? ptc_ui_text(PTC_UI_T_PIN_HAS_BEEN_MODIFIED_CURRENTLY_STILL_WEAK) : ptc_ui_text(PTC_UI_T_PIN_HAS_BEEN_MODIFIED));
        return;
    }
    if (state != PTC_AUTH_EMPTY) {
        show_auth_error(ui, ptc_ui_text(PTC_UI_T_UNABLE_TO_SET_PLAYWISE_PIN), auth_status_zh(state), 0);
        return;
    }
    if (!pin_input(ui, ptc_ui_text(PTC_UI_T_SET_PLAYWISE_PIN), ptc_ui_text(PTC_UI_T_ENTER_A_NUMBER_FROM_1_TO_64),
                   pin, sizeof(pin)) ||
        !pin_input(ui, ptc_ui_text(PTC_UI_T_CONFIRM_PLAYWISE_PIN), ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THE_SAME_PIN_AGAIN_YOUR),
                   pin_confirm, sizeof(pin_confirm))) {
        ui->auth_retry_action = AUTH_RETRY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_PIN_SETUP_CANCELED));
        return;
    }
    if (strcmp(pin, pin_confirm) != 0) {
        show_auth_error(ui, ptc_ui_text(PTC_UI_T_TWO_PINS_ARE_INCONSISTENT), ptc_ui_text(PTC_UI_T_THE_PINS_ENTERED_TWICE_ARE_INCONSISTENT_AND), 0);
        return;
    }
    state = ptc_companion_auth_set_pin(&ui->auth, pin, time(NULL), switch_random, NULL);
    if (state != PTC_AUTH_OK) {
        show_auth_error(ui, ptc_ui_text(PTC_UI_T_PIN_SETTING_FAILED), auth_status_zh(state), 0);
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_NONE;
    ui->model.setup_pin_ready = true;
    ui->setup_parent_authorized = true;
    snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(PTC_UI_T_PIN_HAS_BEEN_MODIFIED));
}

bool ensure_default_setup_pin(UiState *ui)
{
    PtcAuthStatus state;
    if (!ui) return false;
    state = ptc_companion_auth_state(&ui->auth);
    ui->model.setup_pin_ready = state == PTC_AUTH_OK;
    if (state == PTC_AUTH_OK) {
        ui->auth_retry_action = AUTH_RETRY_NONE;
        return true;
    }
    if (state != PTC_AUTH_EMPTY) {
        ui->auth_retry_action = AUTH_RETRY_DEFAULT_SETUP_PIN;
        show_auth_error(ui, ptc_ui_text(PTC_UI_T_UNABLE_TO_CREATE_DEFAULT_PIN), auth_status_zh(state), 0);
        return false;
    }
    state = ptc_companion_auth_set_pin(&ui->auth, "110", time(NULL), switch_random, NULL);
    if (state != PTC_AUTH_OK) {
        ui->auth_retry_action = AUTH_RETRY_DEFAULT_SETUP_PIN;
        show_auth_error(ui, ptc_ui_text(PTC_UI_T_DEFAULT_PIN_SETUP_FAILED), auth_status_zh(state), 0);
        return false;
    }
    ui->auth_retry_action = AUTH_RETRY_NONE;
    ui->model.setup_pin_ready = true;
    ui->setup_parent_authorized = true;
    snprintf(ui->model.message, sizeof(ui->model.message),
             ptc_ui_text(PTC_UI_T_DEFAULT_PIN_110_HAS_BEEN_CREATED_THIS));
    return true;
}

void enter_support_area(UiState *ui)
{
    if (!ui) return;
    ui->model.view = PTC_UI_PARENT;
    ui->model.parent_page = PTC_UI_PARENT_SUPPORT;
    ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
    ui->model.selected_index = 4;
    ui->model.parent_footer_focused = false;
    ui->model.overlay = PTC_UI_OVERLAY_NONE;
    snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(PTC_UI_T_SETUP_DIAGNOSTIC_GUIDE));
}

void finish_setup(UiState *ui, bool activated)
{
    if (!ui) return;
    ui->model.setup_wizard_completed = true;
    ui->model.setup_completion_known = true;
    (void)save_setup_step(ui, 0);
    /* An unreadable/missing PIN opens diagnostics only, never privileged pages. */
    ui->model.setup_pin_ready = ptc_companion_auth_state(&ui->auth) == PTC_AUTH_OK;
    ui->model.parent_support_only = !ui->setup_parent_authorized || !ui->model.setup_pin_ready;
    if (activated && !ui->model.parent_support_only) enter_parent_area_unlocked(ui);
    else enter_support_area(ui);
}

void setup_previous(UiState *ui)
{
    if (!ui) return;
    if (ui->model.setup_step <= PTC_UI_SETUP_PREPARE) ui->exit_requested = true;
    else (void)save_setup_step(ui, ui->model.setup_step - 1);
    ui->model.setup_focus = 0;
}

void setup_primary(UiState *ui)
{
    if (!ui) return;
    if (ui->model.setup_step == PTC_UI_SETUP_PREPARE) {
        (void)save_setup_step(ui, PTC_UI_SETUP_PARENT);
        (void)ensure_default_setup_pin(ui);
    } else if (ui->model.setup_step == PTC_UI_SETUP_PARENT) {
        (void)save_setup_step(ui, PTC_UI_SETUP_CONFIRM);
    } else if (ui->model.setup_step == PTC_UI_SETUP_CONFIRM) {
        if (ui->model.setup_activation_pending) return;
        if (!ui->waiting && ptc_companion_auth_state(&ui->auth) != PTC_AUTH_OK) {
            ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_PIN, PTC_ERR_SETUP_PENDING);
            finish_setup(ui, false);
            return;
        }
        if (!ui->waiting && !ui->setup_parent_authorized) {
            if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_SETUP_ACTIVATE))) {
                ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_PIN, PTC_ERR_SETUP_PENDING);
                finish_setup(ui, false);
                return;
            }
            ui->setup_parent_authorized = true;
        }
        if (strcmp(ui->model.setup_phase, "active") == 0 && !ui->model.disable_flag_present) {
            finish_setup(ui, true);
        } else if (!ui->waiting) {
            ui->model.setup_activation_pending = true;
            submit_transport_empty(ui, "complete_setup", ptc_ui_text(PTC_UI_T_SETUP_ACTIVATING),
                ptc_ui_text(PTC_UI_T_FAILED_TO_ENABLE_AUTOMATIC_CONTROL));
            if (!ui->waiting) {
                ui->model.setup_activation_pending = false;
                ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_ACTIVATION, PTC_ERR_SETUP_PENDING);
                finish_setup(ui, false);
            }
        }
    }
    ui->model.setup_focus = 0;
}

void setup_action(UiState *ui, int action, int index)
{
    if (!ui) return;
    switch (action) {
    case PTC_UI_HIT_SETUP_LANGUAGE:
        if (index >= 0 && index <= PTC_UI_LANGUAGE_ENGLISH)
            (void)apply_language_preference(ui, (PtcUiLanguagePreference)index);
        break;
    case PTC_UI_HIT_SETUP_TIME_HELP:
        ui->model.setup_time_help = !ui->model.setup_time_help;
        break;
    case PTC_UI_HIT_SETUP_MORE:
        ui->model.setup_more = !ui->model.setup_more;
        break;
    case PTC_UI_HIT_SETUP_SHORTCUT:
        open_shortcut_manager(ui);
        break;
    case PTC_UI_HIT_SETUP_THEME_OPTION:
        ui->model.setup_theme_index = index;
        (void)apply_theme_preference(ui, (PtcUiThemePreference)index);
        break;
    case PTC_UI_HIT_SETUP_SKIP:
        finish_setup(ui, false);
        break;
    default:
        break;
    }
}

void handle_setup_input(UiState *ui, u64 down, u64 held)
{
    int max_focus;
    (void)held;
    if (!ui || ui->model.view != PTC_UI_SETUP) return;
    if (down & HidNpadButton_B) { setup_previous(ui); return; }
    max_focus = ui->model.setup_step == PTC_UI_SETUP_CONFIRM ? 1 :
        (ui->model.setup_step == PTC_UI_SETUP_PARENT && ui->model.setup_more ? 4 : 2);
    if (down & HidNpadButton_Down) ui->model.setup_focus = (ui->model.setup_focus + 1) % (max_focus + 1);
    else if (down & HidNpadButton_Up) ui->model.setup_focus = (ui->model.setup_focus + max_focus) % (max_focus + 1);
    else if (down & HidNpadButton_X) {
        if (ui->model.setup_step == PTC_UI_SETUP_PARENT) setup_pin(ui);
        else if (ui->model.setup_step == PTC_UI_SETUP_CONFIRM) finish_setup(ui, false);
        else setup_action(ui, PTC_UI_HIT_SETUP_TIME_HELP, 0);
    } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
        int direction = down & HidNpadButton_Right ? 1 : -1;
        if (ui->model.setup_step == PTC_UI_SETUP_PREPARE) {
            ui->model.setup_focus = 1;
            setup_action(ui, PTC_UI_HIT_SETUP_LANGUAGE, ((int)ui->language_preference + direction + 4) % 4);
        } else if (ui->model.setup_step == PTC_UI_SETUP_PARENT && ui->model.setup_more) {
            ui->model.setup_focus = 3;
            setup_action(ui, PTC_UI_HIT_SETUP_THEME_OPTION, ((int)ui->theme_preference + direction + 3) % 3);
        } else if (ui->model.setup_step == PTC_UI_SETUP_CONFIRM) ui->model.setup_focus ^= 1;
    } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
        int focus = down & HidNpadButton_Plus ? 0 : ui->model.setup_focus;
        if (focus == 0) setup_primary(ui);
        else if (ui->model.setup_step == PTC_UI_SETUP_PREPARE) {
            if (focus == 2) setup_action(ui, PTC_UI_HIT_SETUP_TIME_HELP, 0);
        } else if (ui->model.setup_step == PTC_UI_SETUP_PARENT) {
            if (focus == 1) setup_pin(ui);
            else if (focus == 2) setup_action(ui, PTC_UI_HIT_SETUP_MORE, 0);
            else if (focus == 4) setup_action(ui, PTC_UI_HIT_SETUP_SHORTCUT, 0);
        } else finish_setup(ui, false);
    }
}

static void open_confirm_overlay_internal(UiState *ui, PtcUiOperation operation,
                                         const char *title, const char *body, bool is_danger)
{
    ui->quota_recheck_ready = false;
    ui->model.confirm_return_overlay = ui->model.overlay;
    snprintf(ui->model.confirm_return_title, sizeof(ui->model.confirm_return_title), "%s", ui->model.overlay_title);
    snprintf(ui->model.confirm_return_body, sizeof(ui->model.confirm_return_body), "%s", ui->model.overlay_body);
    ui->model.overlay = PTC_UI_OVERLAY_CONFIRM;
    ui->model.operation = operation;
    ui->model.confirm_hold_required = is_danger;
    ui->model.quota_refresh_failed = false;
    ui->model.overlay_selection = 1;
    if (operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
        operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
        operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY) {
        ui->model.overlay_selection = 0;
    }
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s", title);
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s", body);
    ptc_audio_play(is_danger ? PTC_SE_DANGER : PTC_SE_POPUP);
}

void open_confirm_overlay(UiState *ui, PtcUiOperation operation, const char *title, const char *body)
{
    open_confirm_overlay_internal(ui, operation, title, body, false);
}

void open_danger_confirm_overlay(UiState *ui, PtcUiOperation operation, const char *title, const char *body)
{
    open_confirm_overlay_internal(ui, operation, title, body, true);
}

void open_weekly_page(UiState *ui)
{
    PtcDayRule saved_draft[7];
    int saved_editor = ui->model.editor_index;
    bool preserve_draft = ui->model.weekly_dirty;
    if (preserve_draft) {
        memcpy(saved_draft, ui->model.draft_week, sizeof(saved_draft));
    }
    load_rule_drafts(ui);
    if (preserve_draft) {
        memcpy(ui->model.draft_week, saved_draft, sizeof(saved_draft));
        update_weekly_dirty(ui);
        ui->model.editor_index = saved_editor;
    } else {
        ui->model.editor_index = ptc_weekday_from_day_index(ui->model.day_index);
    }
    ui->model.parent_page = PTC_UI_PARENT_PLAN;
    ui->model.plan_page = PTC_UI_PLAN_PAGE_WEEKLY;
    ui->model.selected_index = 0;
    for (int slot = 0; slot < 7; ++slot) {
        if (ptc_ui_weekday_for_display_slot(slot) == ui->model.editor_index) {
            ui->model.weekly_grid_slot = slot;
            ui->model.weekly_last_day_slot = slot;
            break;
        }
    }
    submit_status(ui);
    snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_REFRESHING_WEEKLY_PLANNER_PRESS_A_AFTER_SELECTING));
}
