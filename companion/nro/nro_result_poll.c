#include "nro_app_internal.h"

static void submit_offline_code(UiState *ui, const char *code)
{
    PtcCompanionStatus status;
    PtcPendingRedemption pending;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    memset(&pending, 0, sizeof(pending));
    snprintf(pending.request_id, sizeof(pending.request_id), "%s", ui->active_request_id);
    pending.confirmed_at = (int64_t)time(NULL);
    pending.grant_minutes = ui->model.code_grant_minutes;
    pending.before_remaining_available = ui->model.code_before_remaining_available;
    pending.before_remaining_minutes = ui->model.code_before_remaining_minutes;
    pending.before_unlimited = ui->model.code_before_unlimited;
    pending.after_remaining_available = ui->model.code_preview_after_available;
    pending.after_remaining_minutes = ui->model.code_preview_after_minutes;
    pending.effective_add_minutes = ui->model.code_effective_add_minutes;
    pending.capped = ui->model.code_preview_capped;
    pending.converts_unlimited_to_limited = ui->model.code_preview_converts_unlimited;
    status = ptc_companion_pending_redemption_save(&ui->client, &pending);
    if (status != PTC_COMPANION_OK) {
        ui->waiting = false;
        ui->model.pending_code[0] = '\0';
        set_message(ui, ptc_ui_text(PTC_UI_T_THE_REDEMPTION_RECOVERY_INFORMATION_CANNOT_BE_SAVED), status);
        return;
    }
    status = ptc_companion_transport_submit_offline_code(&ui->transport, ui->active_request_id, time(NULL), code);
    set_command_name(ui, "offline_code");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        pending.submitted = true;
        (void)ptc_companion_pending_redemption_save(&ui->client, &pending);
        ui->pending_redemption = pending;
        begin_wait(ui, "offline_code", ptc_ui_text(PTC_UI_T_THE_GRANT_CODE_HAS_BEEN_SUBMITTED_AND));
        return;
    }
    (void)ptc_companion_pending_redemption_clear(&ui->client);
    ui->model.pending_code[0] = '\0';
    ui->waiting = false;
    set_message(ui, ptc_ui_text(PTC_UI_T_THE_GRANT_CODE_SUBMISSION_FAILED_THE_CODE), status);
}

static bool code_error_stays_in_input(int error_code)
{
    return error_code >= PTC_ERR_BAD_CODE && error_code <= PTC_ERR_CODE_COOLDOWN;
}

static void open_code_preview_confirm(UiState *ui, bool refreshed)
{
    const char *body = refreshed
        ? ptc_ui_text(PTC_UI_T_THE_LIVE_STATE_CHANGED_THE_PREVIEW_WAS)
        : ptc_ui_text(PTC_UI_T_CHECK_CURRENT_STATUS_AND_THE_ESTIMATED_RESULT);
    bool requires_hold = !ui->model.code_preview_after_available ||
        ui->model.code_preview_after_minutes == 0 ||
        ui->model.code_preview_converts_unlimited;
    if (requires_hold)
        open_danger_confirm_overlay(ui, PTC_UI_OPERATION_REDEEM_OFFLINE_CODE,
                                    refreshed ? ptc_ui_text(PTC_UI_T_STATUS_CHANGED_PLEASE_CONFIRM_AGAIN) : ptc_ui_text(PTC_UI_T_CONFIRM_PLAYTIME_GRANT), body);
    else
        open_confirm_overlay(ui, PTC_UI_OPERATION_REDEEM_OFFLINE_CODE,
                             refreshed ? ptc_ui_text(PTC_UI_T_STATUS_CHANGED_PLEASE_CONFIRM_AGAIN) : ptc_ui_text(PTC_UI_T_CONFIRM_PLAYTIME_GRANT), body);
}

static void sync_setup_wizard(UiState *ui)
{
    if (!ui) return;
    ptc_ui_setup_sync(&ui->model);
}

void poll_result(UiState *ui, bool force)
{
    PtcCompanionStatus status;
    PtcDayRule saved_draft[7];
    bool preserve_weekly_draft;
    bool preserve_holiday_draft;
    bool saved_holiday_enabled;
    PtcDayRule saved_holiday_rule;
    PtcDayRule saved_makeup_rule;
    PtcScheduledOverride saved_scheduled_draft;
    bool preserve_scheduled_draft;
    PtcBedtimePolicy saved_bedtime_draft;
    bool preserve_bedtime_draft;
    PtcAutonomyPolicy saved_autonomy_draft;
    bool preserve_autonomy_draft;
    if (!ui->waiting) {
        if (force) {
            submit_status(ui);
        }
        return;
    }
    if (ui->active_request_id[0] == '\0') {
        if (force) {
            submit_status(ui);
        }
        return;
    }
    if (ui->waiting) {
        ui->elapsed_ms += BACKGROUND_POLL_INTERVAL_MS;
    }
    status = ptc_companion_transport_poll(
        &ui->transport,
        BACKGROUND_POLL_INTERVAL_MS,
        ui->model.setup_activation_pending ? -1 : REQUEST_TIMEOUT_MS,
        ui->last_result,
        sizeof(ui->last_result));
    sync_transport_label(ui);
    if (status == PTC_COMPANION_PENDING) {
        if (ui->model.setup_activation_pending && ui->elapsed_ms >= REQUEST_TIMEOUT_MS) {
            if (!(ui->model.setup_issue_mask & (1U << PTC_UI_SETUP_ISSUE_ACTIVATION)))
                ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_ACTIVATION, PTC_ERR_SETUP_PENDING);
            if (!ui->model.setup_wizard_completed) finish_setup(ui, false);
            return;
        }
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_BACKGROUND_IS_BEING_PROCESSED_PLEASE_WAIT));
        return;
    }
    ui->waiting = false;
    if (status == PTC_COMPANION_OK) {
        if (ui->model.setup_activation_pending) {
            cJSON *result = cJSON_Parse(ui->last_result);
            const cJSON *type = cJSON_GetObjectItemCaseSensitive(result, "type");
            bool activation_result = cJSON_IsString(type) && strcmp(type->valuestring, "complete_setup") == 0;
            cJSON_Delete(result);
            if (!activation_result) {
                ui->waiting = true;
                ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_ACTIVATION, PTC_ERR_BAD_REQUEST);
                if (!ui->model.setup_wizard_completed) finish_setup(ui, false);
                return;
            }
        }
        saved_scheduled_draft = ui->model.draft_scheduled_override;
        saved_autonomy_draft = ui->model.draft_autonomy_policy;
        preserve_autonomy_draft = ui->model.overlay == PTC_UI_OVERLAY_HOME_DETAILS ||
            saved_autonomy_draft.daily_buffer_minutes != ui->model.autonomy_policy.daily_buffer_minutes;
        preserve_scheduled_draft = ptc_ui_scheduled_dirty(&ui->model);
        saved_bedtime_draft = ui->model.draft_bedtime_policy;
        preserve_bedtime_draft = ui->model.bedtime_dirty ||
            (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
             ui->model.plan_page == PTC_UI_PLAN_PAGE_BEDTIME);
        preserve_weekly_draft = ui->model.weekly_dirty;
        if (preserve_weekly_draft) {
            memcpy(saved_draft, ui->model.draft_week, sizeof(saved_draft));
        }
        preserve_holiday_draft = ui->model.holiday_dirty;
        saved_holiday_enabled = ui->model.draft_holiday_enabled;
        saved_holiday_rule = ui->model.draft_holiday_rule;
        saved_makeup_rule = ui->model.draft_makeup_workday_rule;
        if (!ptc_ui_apply_result_json(&ui->model, ui->last_result)) {
            if (ui->model.setup_activation_pending) {
                ui->waiting = true;
                ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_ACTIVATION, PTC_ERR_BAD_REQUEST);
                if (!ui->model.setup_wizard_completed) finish_setup(ui, false);
                return;
            }
            ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_STATUS, PTC_ERR_BAD_REQUEST);
            cancel_bedtime_navigation(ui);
            set_message(ui, ptc_ui_text(PTC_UI_T_FAILED_TO_READ_THE_RESULT), PTC_COMPANION_RESULT_INVALID);
            if (ui->quota_recheck_pending) finish_quota_recheck(ui, false);
            if (ui->today_limit_refresh_pending) finish_today_limit_refresh(ui, false);
            if (ui->request_view == PTC_UI_CHILD && ui->model.overlay != PTC_UI_OVERLAY_HOME_DETAILS)
                ui->model.view = PTC_UI_ERROR;
            return;
        }
        if (strcmp(ui->model.result_type, "status") == 0 &&
            ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
            ui->model.grant_status_refresh_failed = strcmp(ui->model.result_status, "ok") != 0;
        }
        if (ui->model.setup_activation_pending && strcmp(ui->model.result_type, "complete_setup") == 0) {
            ui->model.setup_activation_pending = false;
            cJSON *result = cJSON_Parse(ui->last_result);
            const cJSON *setup = cJSON_GetObjectItemCaseSensitive(result, "setup");
            const cJSON *phase = cJSON_GetObjectItemCaseSensitive(setup, "phase");
            bool activated = strcmp(ui->model.result_status, "ok") == 0 && cJSON_IsString(phase) &&
                strcmp(phase->valuestring, "active") == 0 && !ui->model.disable_flag_present;
            cJSON_Delete(result);
            if (!activated) ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_ACTIVATION,
                ui->model.error_code ? ui->model.error_code : PTC_ERR_SETUP_PENDING);
            if (!ui->model.setup_wizard_completed) finish_setup(ui, activated);
            /* If already in Support, keep its current focus and diagnostic result. */
        }
        if (strcmp(ui->model.result_type, "status") == 0 && strcmp(ui->model.result_status, "ok") != 0)
            ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_STATUS, ui->model.error_code);
        sync_setup_wizard(ui);
        load_rule_drafts(ui);
        if (preserve_autonomy_draft && strcmp(ui->model.result_type, "status") == 0)
            ui->model.draft_autonomy_policy = saved_autonomy_draft;
        if (strcmp(ui->model.result_status, "ok") == 0 &&
            strcmp(ui->model.result_type, "restore_today_policy") == 0) {
            ui->model.today_override_cleared_in_session = true;
        } else if (ui->model.today_override_present) {
            ui->model.today_override_cleared_in_session = false;
        }
        ptc_ui_reconcile_scheduled_result(&ui->model, &saved_scheduled_draft, preserve_scheduled_draft);
        if (preserve_bedtime_draft &&
            !(strcmp(ui->model.result_type, "set_bedtime_policy") == 0 &&
              strcmp(ui->model.result_status, "ok") == 0)) {
            ui->model.draft_bedtime_policy = saved_bedtime_draft;
            update_bedtime_dirty(ui);
        } else if (strcmp(ui->model.result_type, "set_bedtime_policy") == 0 &&
                   strcmp(ui->model.result_status, "ok") == 0) {
            ui->model.draft_bedtime_policy = ui->model.bedtime_policy;
            ui->model.bedtime_dirty = false;
        }
        if (ui->model.status_loaded && strcmp(ui->model.result_status, "ok") == 0) {
            ptc_ui_mark_status_updated(&ui->model, (int64_t)time(NULL));
            ui->model.home_details_refresh_failed = false;
        }
        if (preserve_weekly_draft &&
            !(strcmp(ui->model.result_type, "set_weekly_template") == 0 &&
              strcmp(ui->model.result_status, "ok") == 0)) {
            memcpy(ui->model.draft_week, saved_draft, sizeof(saved_draft));
            update_weekly_dirty(ui);
        }
        if (preserve_holiday_draft &&
            !(strcmp(ui->model.result_type, "set_holiday_policy") == 0 &&
              strcmp(ui->model.result_status, "ok") == 0)) {
            ui->model.draft_holiday_enabled = saved_holiday_enabled;
            ui->model.draft_holiday_rule = saved_holiday_rule;
            ui->model.draft_makeup_workday_rule = saved_makeup_rule;
            update_holiday_dirty(ui);
        }
        config_backup_result(ui);
        refresh_disable_flag(ui);
        if (ui->model.overlay == PTC_UI_OVERLAY_CALENDAR_MANAGER)
            calendar_manager_refresh(ui);
        if (strcmp(ui->model.result_type, "clear_redemption_history") == 0) {
            bool cleared = strcmp(ui->model.result_status, "ok") == 0;
            open_redemption_history(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                     cleared ? ptc_ui_text(PTC_UI_T_THE_GRANT_CODE_USAGE_RECORD_HAS_BEEN)
                             : ptc_ui_text(PTC_UI_T_FAILED_TO_CLEAR_THE_GRANT_CODE_USAGE));
        }
        if (strcmp(ui->model.result_type, "clear_activity_history") == 0) {
            bool cleared = strcmp(ui->model.result_status, "ok") == 0;
            open_activity_history(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                cleared ? ptc_ui_text(PTC_UI_T_FAMILY_ACTIVITY_RECORDS_HAVE_BEEN_CLEARED_RULES)
                        : ptc_ui_text(PTC_UI_T_FAILED_TO_CLEAR_FAMILY_ACTIVITY_RECORDS_THE));
        }
        if (strcmp(ui->model.result_type, "preview_offline_code") == 0) {
            if (strcmp(ui->model.result_status, "ok") == 0) {
                bool after_zero = ui->model.code_preview_after_available &&
                    ui->model.code_preview_after_minutes == 0;
                bool material_change = ui->code_preview_recheck &&
                    (ui->code_previous_after_available != ui->model.code_preview_after_available ||
                     ui->code_previous_after_zero != after_zero ||
                     ui->code_previous_capped != ui->model.code_preview_capped ||
                     ui->code_previous_converts_unlimited != ui->model.code_preview_converts_unlimited);
                if (ui->code_preview_recheck && !material_change) {
                    ui->code_preview_recheck = false;
                    ui->model.code_before_remaining_available = ui->model.remaining_available;
                    ui->model.code_before_remaining_minutes = ui->model.remaining_minutes;
                    ui->model.code_before_unlimited = ui->model.unrestricted_today == 1;
                    submit_offline_code(ui, ui->model.pending_code);
                } else {
                    ui->code_preview_recheck = false;
                    open_code_preview_confirm(ui, material_change);
                }
            } else {
                ui->code_preview_recheck = false;
                ui->model.pending_code[0] = '\0';
                if (ui->model.error_code == PTC_ERR_UNLIMITED_NOT_ALLOWED) {
                    ui->model.view = PTC_UI_CHILD;
                    ui->model.overlay = PTC_UI_OVERLAY_NONE;
                    snprintf(ui->model.message, sizeof(ui->model.message),
                        ptc_ui_text(PTC_UI_T_THERE_IS_NO_TIME_LIMIT_TODAY_AND_3));
                } else if (code_error_stays_in_input(ui->model.error_code)) {
                    char error[96];
                    snprintf(error, sizeof(error), "%.95s", ui->model.message);
                    open_offline_code_input(ui);
                    snprintf(ui->model.numpad_error, sizeof(ui->model.numpad_error), "%s", error);
                } else {
                    ui->model.view = PTC_UI_ERROR;
                }
            }
        } else if (strcmp(ui->model.result_type, "offline_code") == 0) {
            ui->model.pending_code[0] = '\0';
            ui->model.code_result_pending = false;
            ui->model.code_result_failed = strcmp(ui->model.result_status, "ok") != 0;
            (void)load_redemption_history(ui);
            ptc_ui_match_redemption_result(&ui->model);
            ptc_ui_mark_status_updated(&ui->model, ui->model.code_completed_at);
            if (strcmp(ui->model.result_status, "ok") == 0) {
                ui->model.overlay = PTC_UI_OVERLAY_CODE_RESULT;
                ui->model.operation = PTC_UI_OPERATION_NONE;
            } else if (ui->model.error_code == PTC_ERR_UNLIMITED_NOT_ALLOWED) {
                ui->model.view = PTC_UI_CHILD;
                ui->model.overlay = PTC_UI_OVERLAY_NONE;
                snprintf(ui->model.message, sizeof(ui->model.message),
                    ptc_ui_text(PTC_UI_T_THERE_IS_NO_TIME_LIMIT_TODAY_AND_3));
                (void)ptc_companion_pending_redemption_clear(&ui->client);
            } else if (code_error_stays_in_input(ui->model.error_code)) {
                char error[96];
                snprintf(error, sizeof(error), "%.95s", ui->model.message);
                open_offline_code_input(ui);
                snprintf(ui->model.numpad_error, sizeof(ui->model.numpad_error), "%s", error);
                (void)ptc_companion_pending_redemption_clear(&ui->client);
            } else {
                char original[192];
                snprintf(original, sizeof(original), "%s", ui->model.message);
                snprintf(ui->model.message, sizeof(ui->model.message),
                         ptc_ui_text(PTC_UI_T_THE_REDEMPTION_WAS_UNSUCCESSFUL_AND_THE_GRANT));
                snprintf(ui->model.feedback_detail, sizeof(ui->model.feedback_detail), "%s", original);
                ui->model.view = PTC_UI_ERROR;
                (void)ptc_companion_pending_redemption_clear(&ui->client);
            }
        }
        if (ui->pending_today_action >= 0 && strcmp(ui->model.result_type, "status") == 0) {
            int action = ui->pending_today_action;
            ui->pending_today_action = -1;
            if (strcmp(ui->model.result_status, "ok") == 0) {
                handle_today_action_ready(ui, action);
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message),
                         ptc_ui_text(PTC_UI_T_THE_CURRENT_STATUS_CANNOT_BE_REFRESHED_AND));
            }
        }
        if (ui->request_view == PTC_UI_CHILD && strcmp(ui->model.result_status, "error") == 0 &&
            ui->model.overlay != PTC_UI_OVERLAY_HOME_DETAILS &&
            strcmp(ui->model.result_type, "preview_offline_code") != 0 &&
            strcmp(ui->model.result_type, "offline_code") != 0) {
            ui->model.view = PTC_UI_ERROR;
        }
        if (ui->pending_parent_page >= 0 || ui->pending_leave_parent ||
            ui->pending_bedtime_section >= 0) {
            if (strcmp(ui->model.result_type, "set_weekly_template") == 0 &&
                strcmp(ui->model.result_status, "ok") == 0) {
                apply_pending_navigation(ui);
            } else if (strcmp(ui->model.result_type, "set_weekly_template") == 0) {
                ui->pending_parent_page = -1;
                ui->pending_leave_parent = false;
                snprintf(ui->model.message, sizeof(ui->model.message),
                         ptc_ui_text(PTC_UI_T_WEEKLY_PLAN_SAVING_IS_NOT_COMPLETED_MODIFICATIONS));
            } else if (strcmp(ui->model.result_type, "set_bedtime_policy") == 0 &&
                       strcmp(ui->model.result_status, "ok") == 0) {
                ui->model.bedtime_dirty = false;
                finish_bedtime_navigation(ui);
            } else if (strcmp(ui->model.result_type, "set_bedtime_policy") == 0) {
                cancel_bedtime_navigation(ui);
                snprintf(ui->model.message, sizeof(ui->model.message),
                         ptc_ui_text(PTC_UI_T_THE_CURRENT_BEDTIME_SUBPAGE_HAS_NOT_BEEN));
            }
        }
        if (strcmp(ui->model.result_type, "set_bedtime_policy") == 0 &&
            strcmp(ui->model.result_status, "ok") == 0) {
            const char *NAMES[] = {ptc_ui_text(PTC_UI_T_WEEKLY_BEDTIME), ptc_ui_text(PTC_UI_T_GOING_TO_BED_ON_HOLIDAYS), ptc_ui_text(PTC_UI_T_GO_TO_BED_ON_THE_SPECIFIED_DATE)};
            int section = ui->bedtime_saved_section;
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_S_SAVED_AND_APPLIED),
                section >= 0 && section < 3 ? NAMES[section] : ptc_ui_text(PTC_UI_T_BEDTIME));
        }
        if (strcmp(ui->model.result_type, "confirm_bedtime_requirements") == 0) {
            if (ui->pending_config_restore) {
                ui->pending_config_restore = false;
                ui->model.overlay = PTC_UI_OVERLAY_CONFIG_BACKUP;
                if (strcmp(ui->model.result_status, "ok") == 0) request_config_restore(ui);
                return;
            }
            if (ui->pending_dock_save) {
                ui->pending_dock_save = false;
                if (strcmp(ui->model.result_status, "ok") == 0) {
                    ui->model.overlay = PTC_UI_OVERLAY_NONE;
                    save_dock_from_page(ui);
                }
                return;
            }
            if (ui->pending_eye_care_save) {
                ui->pending_eye_care_save = false;
                if (strcmp(ui->model.result_status, "ok") == 0) {
                    ui->model.overlay = PTC_UI_OVERLAY_NONE;
                    submit_eye_care_policy(ui);
                }
                return;
            }
            if (strcmp(ui->model.result_status, "ok") == 0 && ui->model.bedtime_dirty) {
                /* Re-enter the common save gate after the environment
                 * acknowledgement. The current minute may now be inside the
                 * draft window, so first enablement must still show the
                 * immediate-restriction confirmation before it submits. */
                save_bedtime_from_page(ui);
                if (!ui->waiting && ui->model.overlay != PTC_UI_OVERLAY_CONFIRM)
                    cancel_bedtime_navigation(ui);
                return;
            }
            if (strcmp(ui->model.result_status, "ok") != 0) {
                cancel_bedtime_navigation(ui);
                snprintf(ui->model.message, sizeof(ui->model.message),
                    ptc_ui_text(PTC_UI_T_THE_BEDTIME_RESTRICTION_ENVIRONMENT_CONFIRMATION_HAS_NOT));
            } else if (!ui->model.bedtime_dirty) {
                cancel_bedtime_navigation(ui);
            }
        }
        if ((strcmp(ui->model.result_type, "skip_bedtime") == 0 ||
             strcmp(ui->model.result_type, "clear_bedtime_skip") == 0) &&
            strcmp(ui->model.result_status, "error") == 0 && ui->model.error_code == 318) {
            ui->model.pending_bedtime_skip_instance_id = 0;
            submit_status(ui);
            snprintf(ui->model.message, sizeof(ui->model.message),
                ptc_ui_text(PTC_UI_T_THE_BEDTIME_WINDOW_HAS_CHANGED_AND_IS));
        }
        if (strcmp(ui->model.result_type, "skip_eye_care_break") == 0 &&
            strcmp(ui->model.result_status, "error") == 0 && ui->model.error_code == 322) {
            ui->model.pending_eye_care_break_id = 0;
            submit_status(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_CHANGED));
        }
        if (ui->quota_recheck_pending && strcmp(ui->model.result_type, "status") == 0) {
            finish_quota_recheck(ui, strcmp(ui->model.result_status, "ok") == 0);
        }
        if (ui->today_limit_refresh_pending && strcmp(ui->model.result_type, "status") == 0)
            finish_today_limit_refresh(ui, strcmp(ui->model.result_status, "ok") == 0);
        return;
    }
    if (ui->pending_parent_page >= 0 || ui->pending_leave_parent ||
        ui->pending_bedtime_section >= 0) cancel_bedtime_navigation(ui);
    if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
        ui->model.grant_status_refresh_failed = true;
    }
    if (ui->pending_redemption.request_id[0] != '\0' &&
        strcmp(ui->pending_redemption.request_id, ui->active_request_id) == 0) {
        ptc_companion_transport_cancel(&ui->transport);
        ui->recovering_redemption = true;
        show_pending_redemption(ui);
        return;
    }
    if (ui->model.setup_activation_pending) {
        ui->waiting = true;
        ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_ACTIVATION, PTC_ERR_SETUP_PENDING);
        if (!ui->model.setup_wizard_completed) finish_setup(ui, false);
        return;
    }
    ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_STATUS, PTC_ERR_PCTL_READ_FAILED);
    set_message(ui, ptc_ui_text(PTC_UI_T_FAILED_TO_READ_THE_RESULT), status);
    if (ui->quota_recheck_pending) finish_quota_recheck(ui, false);
    if (ui->today_limit_refresh_pending) finish_today_limit_refresh(ui, false);
    if (ui->request_view == PTC_UI_CHILD && ui->model.overlay != PTC_UI_OVERLAY_HOME_DETAILS)
        ui->model.view = PTC_UI_ERROR;
}
