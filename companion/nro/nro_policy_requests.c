#include "nro_app_internal.h"

void submit_scheduled_override(UiState *ui)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_scheduled_override(&ui->transport,
        ui->active_request_id, time(NULL), &ui->model.draft_scheduled_override);
    set_command_name(ui, "set_scheduled_override");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "set_scheduled_override", ptc_ui_text(PTC_UI_T_SAVING_THE_QUOTA_FOR_THE_SPECIFIED_DATE));
    else set_message(ui, ptc_ui_text(PTC_UI_T_FAILED_TO_SUBMIT_THE_QUOTA_ON_THE), status);
}
void submit_autonomy_policy(UiState *ui)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_autonomy_policy(&ui->transport,
        ui->active_request_id, time(NULL), &ui->model.draft_autonomy_policy);
    set_command_name(ui, "set_autonomy_policy");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "set_autonomy_policy", ptc_ui_text(PTC_UI_T_SAVING_AUTONOMOUS_BUFFER_SETTINGS));
    else set_message(ui, ptc_ui_text(PTC_UI_T_AUTONOMOUS_BUFFER_SETTING_SUBMISSION_FAILED), status);
}
void submit_bedtime_confirmation(UiState *ui)
{
    char fingerprint[65];
    PtcCompanionStatus status;
    current_environment_fingerprint(ui, fingerprint);
    if (strcmp(fingerprint, "environment-unavailable") == 0) {
        snprintf(ui->model.message, sizeof(ui->model.message),
            ptc_ui_text(PTC_UI_T_THE_CURRENT_ENVIRONMENT_FINGERPRINT_CANNOT_BE_READ));
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_confirm_bedtime_requirements(&ui->transport,
        ui->active_request_id, time(NULL), true, true, 1, fingerprint);
    set_command_name(ui, "confirm_bedtime_requirements");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "confirm_bedtime_requirements", ptc_ui_text(PTC_UI_T_CONFIRMING_NINTENDO_PARENTAL_CONTROLS_AND_CURRENT_ENVIRONMENT));
    } else {
        set_message(ui, ptc_ui_text(PTC_UI_T_BEDTIME_RESTRICTION_ENVIRONMENT_CONFIRMATION_FAILED), status);
    }
}
void submit_bedtime_policy(UiState *ui)
{
    PtcCompanionStatus status;
    PtcBedtimePolicy policy = ptc_ui_bedtime_section_policy(&ui->model,
        ui->model.bedtime_section);
    ui->bedtime_saved_section = (int)ui->model.bedtime_section;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_bedtime_policy(&ui->transport,
        ui->active_request_id, time(NULL), &policy, true);
    set_command_name(ui, "set_bedtime_policy");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "set_bedtime_policy", ptc_ui_text(PTC_UI_T_SAVING_THE_CURRENT_BEDTIME_SUBPAGE));
    else set_message(ui, ptc_ui_text(PTC_UI_T_BEDTIME_PLAN_SUBMISSION_FAILED), status);
}
void submit_bedtime_skip(UiState *ui)
{
    PtcCompanionStatus status;
    uint64_t instance_id = ui->model.pending_bedtime_skip_instance_id;
    if (instance_id == 0) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THERE_IS_NO_BEDTIME_WINDOW_TO_SUBMIT));
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_skip_bedtime(&ui->transport,
        ui->active_request_id, time(NULL), instance_id);
    set_command_name(ui, "skip_bedtime");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "skip_bedtime", ptc_ui_text(PTC_UI_T_SKIPPING_BEDTIME_THIS_TIME));
    else set_message(ui, ptc_ui_text(PTC_UI_T_SKIP_BEDTIME_COMMIT_FAILED), status);
}
void submit_clear_bedtime_skip(UiState *ui)
{
    PtcCompanionStatus status;
    uint64_t instance_id = ui->model.pending_bedtime_skip_instance_id;
    if (instance_id == 0) return;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_clear_bedtime_skip(&ui->transport,
        ui->active_request_id, time(NULL), instance_id);
    set_command_name(ui, "clear_bedtime_skip");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "clear_bedtime_skip", ptc_ui_text(PTC_UI_T_RESTORING_THIS_BEDTIME_LIMIT));
    else set_message(ui, ptc_ui_text(PTC_UI_T_RESTORE_BEDTIME_LIMIT_SUBMISSION_FAILED), status);
}
