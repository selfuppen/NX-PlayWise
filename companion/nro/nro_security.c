#include "nro_app_internal.h"

bool verify_sensitive_pin(UiState *ui, const char *action)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus status;
    int64_t retry_after = 0;
    const PtcUiOverlay return_overlay = ui ? ui->model.overlay : PTC_UI_OVERLAY_NONE;
    if (!pin_input(ui, ptc_ui_text(PTC_UI_T_VERIFY_PLAYWISE_PIN), action, pin, sizeof(pin))) {
        if (ui && return_overlay != PTC_UI_OVERLAY_NONE && ui->model.overlay == PTC_UI_OVERLAY_NONE) {
            ui->model.overlay = return_overlay;
        }
        ui->auth_retry_action = AUTH_RETRY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_SENSITIVE_OPERATION_CANCELED));
        return false;
    }
    /* pin_input owns the PIN dialog and closes it on return. Sensitive actions
     * must continue in the dialog that initiated authentication. */
    if (return_overlay != PTC_UI_OVERLAY_NONE && ui->model.overlay == PTC_UI_OVERLAY_NONE) {
        ui->model.overlay = return_overlay;
    }
    status = ptc_companion_auth_verify_pin(&ui->auth, pin, (int64_t)time(NULL), &retry_after);
    if (status != PTC_AUTH_OK) {
        ptc_audio_play(PTC_SE_ERROR);
        if (status == PTC_AUTH_COOLDOWN && retry_after > 0) {
            show_auth_error(ui, ptc_ui_text(PTC_UI_T_PIN_TEMPORARILY_LOCKED), ptc_ui_text(PTC_UI_T_TOO_MANY_PIN_ERRORS_PLEASE_WAIT_FOR), retry_after);
            return false;
        }
        show_auth_error(ui, ptc_ui_text(PTC_UI_T_PIN_VERIFICATION_FAILED),
                        status == PTC_AUTH_DENIED ? ptc_ui_text(PTC_UI_T_PIN_IS_INCORRECT_PLEASE_TRY_AGAIN) : auth_status_zh(status), 0);
        return false;
    }
    ui->auth_retry_action = AUTH_RETRY_NONE;
    return true;
}
void change_parent_pin(UiState *ui)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    char confirm[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus status;
    ui->auth_retry_action = AUTH_RETRY_CHANGE_PIN;
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_BEFORE_CHANGING_YOUR_PIN_PLEASE_ENTER_YOUR))) return;
    if (!pin_input(ui, ptc_ui_text(PTC_UI_T_CHANGE_PLAYWISE_PIN), ptc_ui_text(PTC_UI_T_PLEASE_ENTER_A_NEW_1_TO_64), pin, sizeof(pin)) ||
        !pin_input(ui, ptc_ui_text(PTC_UI_T_CONFIRM_NEW_PIN), ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THE_SAME_PIN_AGAIN_YOUR), confirm, sizeof(confirm))) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_PIN_CHANGE_CANCELED));
        return;
    }
    if (strcmp(pin, confirm) != 0) {
        ptc_audio_play(PTC_SE_ERROR);
        ui->auth_retry_action = AUTH_RETRY_CHANGE_PIN;
        show_auth_error(ui, ptc_ui_text(PTC_UI_T_TWO_PINS_ARE_INCONSISTENT), ptc_ui_text(PTC_UI_T_THE_NEW_PINS_ENTERED_TWICE_ARE_INCONSISTENT), 0);
        return;
    }
    status = ptc_companion_auth_set_pin(&ui->auth, pin, time(NULL), switch_random, NULL);
    if (status == PTC_AUTH_OK) {
        ptc_audio_play(PTC_SE_SUCCESS);
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
            strlen(pin) < 4U ? ptc_ui_text(PTC_UI_T_PLAYWISE_PIN_HAS_BEEN_UPDATED_THE_CURRENT) : ptc_ui_text(PTC_UI_T_PLAYWISE_PIN_HAS_BEEN_UPDATED));
    } else {
        ptc_audio_play(PTC_SE_ERROR);
        ui->auth_retry_action = AUTH_RETRY_CHANGE_PIN;
        show_auth_error(ui, ptc_ui_text(PTC_UI_T_PIN_MODIFICATION_FAILED), auth_status_zh(status), 0);
    }
}
void dispatch_auth_retry(UiState *ui, AuthRetryAction action)
{
    if (!ui) return;
    switch (action) {
    case AUTH_RETRY_ENTER_PARENT: enter_parent_area(ui); break;
    case AUTH_RETRY_DEFAULT_SETUP_PIN: (void)ensure_default_setup_pin(ui); break;
    case AUTH_RETRY_SETUP_PIN: setup_pin(ui); break;
    case AUTH_RETRY_SAVE_CREDENTIAL: request_save_credential(ui); break;
    case AUTH_RETRY_CHANGE_PIN: change_parent_pin(ui); break;
    case AUTH_RETRY_EDIT_URL: edit_pairing_base_url(ui); break;
    case AUTH_RETRY_RESET_URL: request_reset_pairing_base_url(ui); break;
    case AUTH_RETRY_GENERATE_CODE: generate_local_grant_code(ui); break;
    case AUTH_RETRY_SHOW_QR: show_pairing_qr(ui); break;
    case AUTH_RETRY_EXPORT_CONFIG: export_parent_import(ui); break;
    case AUTH_RETRY_REVEAL_CREDENTIAL: reveal_current_credential(ui); break;
    case AUTH_RETRY_CLEAR_REDEMPTION_HISTORY: request_clear_redemption_history(ui); break;
    case AUTH_RETRY_CLEAR_ACTIVITY_HISTORY: request_clear_activity_history(ui); break;
    case AUTH_RETRY_SKIP_BEDTIME:
        handle_today_action_ready(ui, PTC_UI_OPERATION_SKIP_BEDTIME);
        break;
    case AUTH_RETRY_CLEAR_BEDTIME_SKIP:
        request_clear_bedtime_skip(ui);
        break;
    case AUTH_RETRY_NONE:
    default:
        break;
    }
}
