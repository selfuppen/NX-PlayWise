#include "nro_app_internal.h"

bool load_redemption_history(UiState *ui)
{
    char text[PTC_REDEMPTION_HISTORY_FILE_SIZE];
    if (!ui || !ui->client.storage) return false;
    if (!ui->client.storage->vtable->exists(ui->client.storage, REDEMPTION_HISTORY_PATH)) {
        return ptc_ui_apply_redemption_history_text(&ui->model, "");
    }
    if (!ui->client.storage->vtable->read_text(
            ui->client.storage, REDEMPTION_HISTORY_PATH, text, sizeof(text))) {
        ui->model.redemption_history_available = false;
        ui->model.redemption_history_count = 0;
        ui->model.redemption_history_page = 0;
        return false;
    }
    return ptc_ui_apply_redemption_history_text(&ui->model, text);
}
void open_redemption_history(UiState *ui)
{
    (void)load_redemption_history(ui);
    ui->model.overlay = PTC_UI_OVERLAY_REDEMPTION_HISTORY;
    ui->model.confirm_hold_required = false;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_EXTRA_TIME_CODE_USAGE_RECORD));
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             ptc_ui_text(PTC_UI_T_ONLY_DISPLAYS_THE_LAST_100_SUCCESSFULLY_REDEEMED));
}
void request_clear_redemption_history(UiState *ui)
{
    if (!ui) return;
    if (ui->model.redemption_history_available && ui->model.redemption_history_count == 0) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THERE_ARE_CURRENTLY_NO_GRANT_CODE_USAGE));
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_CLEAR_REDEMPTION_HISTORY;
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_BEFORE_CLEARING_ALL_GRANT_CODE_USAGE_RECORDS))) return;
    open_danger_confirm_overlay(ui, PTC_UI_OPERATION_CLEAR_REDEMPTION_HISTORY,
        ptc_ui_text(PTC_UI_T_CLEAR_ALL_GRANT_CODE_USAGE_RECORDS),
        ptc_ui_text(PTC_UI_T_THIS_OPERATION_IS_IRREVERSIBLE_BUT_IT_WILL));
}
static bool load_activity_history(UiState *ui)
{
    char text[PTC_ACTIVITY_HISTORY_FILE_SIZE];
    if (!ui || !ui->client.storage) return false;
    if (!ui->client.storage->vtable->exists(ui->client.storage, ACTIVITY_HISTORY_PATH)) {
        return ptc_ui_apply_activity_history_text(&ui->model, "");
    }
    if (!ui->client.storage->vtable->read_text(
            ui->client.storage, ACTIVITY_HISTORY_PATH, text, sizeof(text))) {
        ui->model.activity_history_available = false;
        ui->model.activity_history_count = 0;
        ui->model.activity_history_page = 0;
        return false;
    }
    return ptc_ui_apply_activity_history_text(&ui->model, text);
}
void open_activity_history(UiState *ui)
{
    (void)load_activity_history(ui);
    ui->model.overlay = PTC_UI_OVERLAY_ACTIVITY_HISTORY;
    ui->model.confirm_hold_required = false;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_FAMILY_ACTIVITY_RECORD));
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
        ptc_ui_text(PTC_UI_T_RETAINS_UP_TO_200_RULES_GRANT_AUTONOMOUS));
}
void request_clear_activity_history(UiState *ui)
{
    if (!ui) return;
    if (ui->model.activity_history_available && ui->model.activity_history_count == 0) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THERE_ARE_CURRENTLY_NO_FAMILY_ACTIVITY_RECORDS));
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_CLEAR_ACTIVITY_HISTORY;
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THIS_APPLICATION_PIN_AGAIN_BEFORE_2))) return;
    open_danger_confirm_overlay(ui, PTC_UI_OPERATION_CLEAR_ACTIVITY_HISTORY,
        ptc_ui_text(PTC_UI_T_CLEAR_ALL_FAMILY_ACTIVITY_RECORDS),
        ptc_ui_text(PTC_UI_T_THIS_OPERATION_IS_IRREVERSIBLE_THE_GRANT_CODE));
}
