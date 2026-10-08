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
void submit_eye_care_policy(UiState *ui)
{
    PtcCompanionStatus status;
    if (!ui || ui->waiting) return;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_eye_care_policy(&ui->transport,
        ui->active_request_id, time(NULL), &ui->model.draft_eye_care_policy);
    set_command_name(ui, "set_eye_care_policy");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "set_eye_care_policy", ptc_ui_text(PTC_UI_T_EYE_CARE_SAVE));
    else set_message(ui, ptc_ui_text(PTC_UI_T_EYE_CARE_SAVE), status);
}
void submit_eye_care_skip(UiState *ui)
{
    PtcCompanionStatus status;
    uint64_t break_id;
    const char *unavailable;
    if (!ui || ui->waiting) return;
    break_id = ui->model.pending_eye_care_break_id;
    unavailable = ptc_ui_today_action_unavailable_reason(&ui->model, 6, (int64_t)time(NULL));
    if (break_id == 0 || break_id != ui->model.eye_care_break_id || unavailable) {
        ui->model.pending_eye_care_break_id = 0;
        if (!ui->waiting) submit_status(ui);
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
            unavailable ? unavailable : ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_CHANGED));
        return;
    }
    ui->model.pending_eye_care_break_id = 0;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_skip_eye_care_break(&ui->transport,
        ui->active_request_id, time(NULL), break_id);
    set_command_name(ui, "skip_eye_care_break");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK)
        begin_wait(ui, "skip_eye_care_break", ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_SUBMITTING));
    else set_message(ui, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_SUBMIT_FAILED), status);
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

void submit_dock_policy(UiState *ui)
{
    PtcCompanionStatus status;
    if (!ui || ui->waiting) return;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_dock_policy(&ui->transport,
        ui->active_request_id, time(NULL), &ui->model.draft_dock_policy);
    set_command_name(ui, "set_dock_policy");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "set_dock_policy", ptc_ui_text(PTC_UI_T_DOCK_SAVE));
    else set_message(ui, ptc_ui_text(PTC_UI_T_DOCK_SAVE), status);
}

void save_dock_from_page(UiState *ui)
{
    if (!ui || ui->waiting || ui->model.disable_flag_present || !ui->model.dock_dirty) return;
    if (ui->model.draft_dock_policy.force_docked && ui->model.dock_supported_available && !ui->model.dock_supported) {
        snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(PTC_UI_T_DOCK_LITE));
        return;
    }
    if ((ui->model.draft_dock_policy.force_docked || ui->model.draft_dock_policy.undocked_limit_enabled) &&
        !ui->model.bedtime_official_setting_confirmed) {
        open_confirm_overlay(ui, PTC_UI_OPERATION_CONFIRM_DOCK, ptc_ui_text(PTC_UI_T_DOCK_CONFIRM),
            ptc_ui_text(PTC_UI_T_DOCK_REQUIREMENTS));
        return;
    }
    if (ptc_ui_dock_save_requires_hold(&ui->model, (int64_t)time(NULL)))
        open_danger_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_DOCK, ptc_ui_text(PTC_UI_T_DOCK_IMMEDIATE),
            ptc_ui_text(PTC_UI_T_DOCK_CONFIRM_BODY));
    else open_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_DOCK, ptc_ui_text(PTC_UI_T_DOCK_CONFIRM),
            ptc_ui_text(PTC_UI_T_DOCK_CONFIRM_BODY));
}

void request_dock_waiver(UiState *ui)
{
    if (!ui || ui->waiting) return;
    if (!ui->model.dock_available || !ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL))) {
        submit_status(ui);
        return;
    }
    if (ui->model.dock_waived_today || !(ui->model.dock_policy.force_docked || ui->model.dock_policy.undocked_limit_enabled)) return;
    ui->model.pending_dock_waiver_day = ui->model.day_index;
    ui->auth_retry_action = AUTH_RETRY_WAIVE_DOCK;
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_DOCK_PIN))) return;
    open_confirm_overlay(ui, PTC_UI_OPERATION_WAIVE_DOCK, ptc_ui_text(PTC_UI_T_DOCK_WAIVE),
        ptc_ui_text(PTC_UI_T_DOCK_WAIVE_BODY));
}

void submit_dock_waiver(UiState *ui)
{
    PtcCompanionStatus status;
    if (!ui || ui->waiting) return;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_waive_dock_policy(&ui->transport,
        ui->active_request_id, time(NULL), ui->model.pending_dock_waiver_day);
    set_command_name(ui, "waive_dock_policy_today");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "waive_dock_policy_today", ptc_ui_text(PTC_UI_T_DOCK_WAIVE));
    else set_message(ui, ptc_ui_text(PTC_UI_T_DOCK_WAIVE), status);
}

void dock_page_action(UiState *ui, int action, int delta)
{
    PtcDockPolicy *draft;
    if (!ui || ui->waiting) return;
    draft = &ui->model.draft_dock_policy;
#ifdef PLAYWISE_EDEN
    if (action == 7 || action == 8) {
        ui->model.dock_field_focus = action;
        if (ui->client.storage->vtable->write_text_atomic(ui->client.storage, APP_ROOT "/operation-mode.txt",
                action == 7 ? "docked" : "undocked")) {
            ptc_companion_transport_notify_storage_changed(&ui->transport);
            submit_status(ui);
        } else snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_SAVE_FAILED));
        return;
    }
#endif
    if (action == 5) {
        if (ui->model.dock_dirty)
            open_confirm_overlay(ui, PTC_UI_OPERATION_LEAVE_DOCK, ptc_ui_text(PTC_UI_T_DOCK_LEAVE),
                ptc_ui_text(PTC_UI_T_DOCK_CONFIRM_BODY));
        else { ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT; ui->model.selected_index = 5; }
        return;
    }
    if (action == 6) { submit_status(ui); return; }
    ui->model.dock_field_focus = action;
    if (action == 4) { request_dock_waiver(ui); return; }
    if (ui->model.disable_flag_present) return;
    if (action == 0) {
        bool currently_on = draft->force_docked || draft->undocked_limit_enabled;
        if (!currently_on) {
            bool lite = ui->model.dock_supported_available && !ui->model.dock_supported;
            if (lite) {
                draft->force_docked = false;
                draft->undocked_limit_enabled = true;
                if (draft->undocked_daily_minutes == 0) draft->undocked_daily_minutes = 30;
            } else if (draft->undocked_daily_minutes == 0) {
                draft->force_docked = true;
                draft->undocked_limit_enabled = true;
            } else {
                draft->force_docked = false;
                draft->undocked_limit_enabled = true;
            }
        } else {
            draft->force_docked = false;
            draft->undocked_limit_enabled = false;
        }
    } else if (action == 1) {
        if (!draft->force_docked && ui->model.dock_supported_available && !ui->model.dock_supported) {
            snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(PTC_UI_T_DOCK_LITE));
            return;
        }
        draft->force_docked = !draft->force_docked;
        if (draft->force_docked) {
            draft->undocked_limit_enabled = true;
        } else {
            draft->undocked_limit_enabled = true;
            if (draft->undocked_daily_minutes == 0) draft->undocked_daily_minutes = 30;
        }
    } else if (action == 2 && delta) {
        draft->undocked_daily_minutes = ptc_ui_adjust_minutes(draft->undocked_daily_minutes, delta, 0, 1440);
        if (draft->undocked_daily_minutes == 0) {
            draft->force_docked = true;
            draft->undocked_limit_enabled = true;
        } else {
            draft->force_docked = false;
            draft->undocked_limit_enabled = true;
        }
    } else if (action == 2) {
        ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_DOCK_MINUTES, PTC_UI_OVERLAY_NONE,
            ptc_ui_text(PTC_UI_T_DOCK_LIMIT), ptc_ui_text(PTC_UI_T_DOCK_ALLOWANCE), 4, 0, 1440, draft->undocked_daily_minutes);
    } else if (action == 3) { save_dock_from_page(ui); return; }
    ui->model.dock_dirty = ptc_ui_dock_dirty(&ui->model);
}
