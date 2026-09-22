#include "sysmodule_internal.h"

static bool process_clear_redemption_history(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now)
{
    char path[320];
    char previous[PTC_REDEMPTION_HISTORY_FILE_SIZE];
    bool existed;
    bool restored;
    join_path(path, sizeof(path), sysmodule->app_root, "ledger/redemption-history.jsonl");
    existed = sysmodule->storage->vtable->exists(sysmodule->storage, path);
    if (existed && !sysmodule->storage->vtable->read_text(
            sysmodule->storage, path, previous, sizeof(previous))) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    if (!clear_redemption_history(sysmodule)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "redemption_history_cleared");
    if (write_current_status_result(
            sysmodule, request, "release", false, now, false)) {
        return true;
    }
    restored = existed
        ? sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, previous)
        : (!sysmodule->storage->vtable->exists(sysmodule->storage, path) ||
           sysmodule->storage->vtable->remove_path(sysmodule->storage, path));
    append_event(sysmodule, request,
        restored ? "state_rollback_ok" : "state_rollback_failed",
        restored ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED,
        "redemption_history_clear");
    return false;
}

static bool process_clear_activity_history(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now)
{
    char path[320];
    char previous[PTC_ACTIVITY_HISTORY_FILE_SIZE];
    bool existed;
    bool restored;
    join_path(path, sizeof(path), sysmodule->app_root, "activity/history.jsonl");
    existed = sysmodule->storage->vtable->exists(sysmodule->storage, path);
    if (existed && !sysmodule->storage->vtable->read_text(
            sysmodule->storage, path, previous, sizeof(previous))) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_READ_FAILED, now.day_index);
    }
    if (!clear_activity_history(sysmodule)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "activity_history_cleared");
    if (write_current_status_result(
            sysmodule, request, "release", false, now, false)) return true;
    restored = existed
        ? sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, previous)
        : (!sysmodule->storage->vtable->exists(sysmodule->storage, path) ||
           sysmodule->storage->vtable->remove_path(sysmodule->storage, path));
    append_event(sysmodule, request, restored ? "state_rollback_ok" : "state_rollback_failed",
        restored ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED, "activity_history_clear");
    return false;
}


static bool process_rule_request(PtcSysmodule *sysmodule, const PtcRequest *request,
    bool disable_flag, PtcClockSnapshot now)
{
    PtcPctlStatus pctl_status;
    PtcPctlStatus observed_status;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcErrorCode err;
    PtcDayRule active_rule;
    bool pctl_request = request->type == PTC_REQUEST_SET_TODAY_LIMIT ||
        request->type == PTC_REQUEST_ADD_TODAY_MINUTES ||
        request->type == PTC_REQUEST_RESTORE_TODAY_POLICY ||
        request->type == PTC_REQUEST_SET_WEEKLY_TEMPLATE ||
        request->type == PTC_REQUEST_SET_HOLIDAY_POLICY ||
        request->type == PTC_REQUEST_SET_SCHEDULED_OVERRIDE ||
        request->type == PTC_REQUEST_SET_AUTONOMY_POLICY ||
        request->type == PTC_REQUEST_SET_BEDTIME_POLICY;
    PtcDayRule before_active_rule;
    if (disable_flag) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_DISABLED, now.day_index);
    }
    if (!load_rules(sysmodule, &rules)) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_RULES_INVALID, now.day_index);
    }
    if (!load_state(sysmodule, &runtime_state)) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_BAD_REQUEST, now.day_index);
    }
    before_active_rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", true, err, now.day_index);
    }
    {
        if (pctl_request && !recovery_begin(sysmodule, request, now)) {
            return finish_with_error(sysmodule, request, "release", false,
                PTC_ERR_PCTL_BACKUP_FAILED, now.day_index);
        }
        uint16_t played_minutes = ptc_pctl_played_minutes(&pctl_status);
        err = update_rules_for_request(sysmodule, request, &rules, &runtime_state, now, played_minutes);
        if (err != PTC_ERR_OK) {
            if (pctl_request && recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule)) {
                write_disable_flag(sysmodule, "transaction_restore_failed\n");
                err = PTC_ERR_RECOVERY_FAILED;
            }
            return finish_with_error(sysmodule, request, "release", false, err, now.day_index);
        }
        if (request->type == PTC_REQUEST_SET_BEDTIME_POLICY && runtime_state.bedtime_enforced) {
            PtcBedtimeEvaluation bedtime_now = ptc_bedtime_evaluate(
                &rules, now.day_index, ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
            if (!bedtime_now.active || runtime_state.bedtime_skipped_instance_id == bedtime_now.window_instance_id) {
                err = restore_bedtime_base(sysmodule, request, &rules, &runtime_state, now);
                if (err != PTC_ERR_OK) {
                    write_disable_flag(sysmodule, "bedtime_restore_failed\n");
                    return finish_with_error(sysmodule, request, "release", false,
                        PTC_ERR_BEDTIME_RECOVERY_FAILED, now.day_index);
                }
            }
        }
        append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "");
        if (request->type == PTC_REQUEST_SET_TODAY_LIMIT ||
            request->type == PTC_REQUEST_ADD_TODAY_MINUTES ||
            request->type == PTC_REQUEST_DISABLE_TODAY_LIMIT ||
            request->type == PTC_REQUEST_RESTORE_TODAY_POLICY ||
            request->type == PTC_REQUEST_SET_HOLIDAY_POLICY ||
            ((request->type == PTC_REQUEST_SET_SCHEDULED_OVERRIDE ||
              request->type == PTC_REQUEST_SET_WEEKLY_TEMPLATE) &&
             (before_active_rule.mode != ptc_rules_today_rule(&rules, now.day_index,
                 ptc_weekday_from_day_index(now.day_index)).mode ||
              before_active_rule.minutes != ptc_rules_today_rule(&rules, now.day_index,
                 ptc_weekday_from_day_index(now.day_index)).minutes))) {
            active_rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
            err = apply_target(sysmodule, request, now, "release", target_from_day_rule(active_rule), active_rule.minutes);
            if (err != PTC_ERR_OK) {
                return finish_with_error(sysmodule, request, "release", false, err, now.day_index);
            }
            err = observe_target_with_optional_activation(sysmodule, request, now,
                "release", target_from_day_rule(active_rule),
                active_rule.minutes, "rule_request", &observed_status);
            if (err != PTC_ERR_OK) {
                if (!recovery_rollback(sysmodule)) {
                    write_disable_flag(sysmodule, "transaction_restore_failed\n");
                    err = PTC_ERR_RECOVERY_FAILED;
                }
                return finish_with_error(sysmodule, request, "release", false, err, now.day_index);
            }
        }
        {
            uint16_t activity_minutes = request->minutes;
            if (request->type == PTC_REQUEST_SET_SCHEDULED_OVERRIDE) {
                activity_minutes = request->scheduled_override.enabled
                    ? request->scheduled_override.rule.minutes : 0u;
            } else if (request->type == PTC_REQUEST_SET_AUTONOMY_POLICY) {
                activity_minutes = request->autonomy_policy.daily_buffer_minutes;
            } else if (request->type == PTC_REQUEST_SET_WEEKLY_TEMPLATE) {
                PtcDayRule today_r = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
                activity_minutes = today_r.mode == PTC_RULE_MODE_LIMIT ? today_r.minutes : 0u;
            }
            if (!record_activity(sysmodule, request, now, activity_minutes, activity_minutes)) {
                append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED,
                    "activity_history");
                if (recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule)) {
                    write_disable_flag(sysmodule, "transaction_restore_failed\n");
                    err = PTC_ERR_RECOVERY_FAILED;
                } else err = PTC_ERR_STORAGE_WRITE_FAILED;
                return finish_with_error(sysmodule, request, "release", false,
                    err, now.day_index);
            }
        }
    }
    {
        bool ok = write_current_status_result(sysmodule, request, "release",
            false, now, pctl_request && recovery_path_exists(sysmodule));
        if (ok) recovery_clear(sysmodule);
        else if (recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule))
            write_disable_flag(sysmodule, "transaction_restore_failed\n");
        return ok;
    }
}

void process_request_text(PtcSysmodule *sysmodule, const char *request_text, const char *expected_request_id)
{
    PtcRequest request;
    PtcRuntimeConfig config;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    PtcErrorCode parse_err;
    bool disable_flag;
    char disable_path[320];

    parse_err = ptc_request_parse(request_text, &request);
    if (parse_err == PTC_ERR_OK && expected_request_id && strcmp(request.request_id, expected_request_id) != 0) {
        parse_err = PTC_ERR_BAD_REQUEST;
    }
    if (parse_err != PTC_ERR_OK) {
        memset(&request, 0, sizeof(request));
        snprintf(request.request_id, sizeof(request.request_id), "unknown");
        snprintf(request.type_text, sizeof(request.type_text), "unknown");
        if (expected_request_id && ptc_request_id_is_valid(expected_request_id)) {
            snprintf(request.request_id, sizeof(request.request_id), "%s", expected_request_id);
        } else {
            (void)json_string(request_text, "request_id", request.request_id, sizeof(request.request_id));
            if (!ptc_request_id_is_valid(request.request_id)) snprintf(request.request_id, sizeof(request.request_id), "unknown");
        }
        (void)json_string(request_text, "type", request.type_text, sizeof(request.type_text));
        (void)finish_with_error(sysmodule, &request, "release", true, parse_err, now.day_index);
        return;
    }
    append_event(sysmodule, &request, "request_received", PTC_ERR_OK, "");
    if (!load_config(sysmodule, &config)) {
        (void)finish_with_error(sysmodule, &request, "release", true, PTC_ERR_CONFIG_INVALID, now.day_index);
        return;
    }
    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    disable_flag = sysmodule->storage->vtable->exists(sysmodule->storage, disable_path);

    if (request.type != PTC_REQUEST_STATUS &&
        request.type != PTC_REQUEST_COMPLETE_SETUP &&
        request.type != PTC_REQUEST_RETRY_SETUP_RELEASE &&
        request.type != PTC_REQUEST_RESTORE_INSTALL_SNAPSHOT &&
        request.type != PTC_REQUEST_SKIP_BEDTIME &&
        request.type != PTC_REQUEST_DISABLE_BEDTIME &&
        request.type != PTC_REQUEST_OVERLAY_READY) {
        PtcSetupState setup;
        if (!load_setup_state(sysmodule, &setup) || strcmp(setup.phase, "active") != 0) {
            (void)finish_with_error(sysmodule, &request, "release", true,
                PTC_ERR_SETUP_PENDING, now.day_index);
            return;
        }
    }

    if (process_grant_request_surface(
            sysmodule, &request, &config, disable_flag, now) ||
        process_recovery_request_surface(
            sysmodule, &request, disable_flag, now)) {
        return;
    }

    switch (request.type) {
#ifdef PLAYWISE_DEVICE_LAB
    case PTC_REQUEST_LAB_SESSION_START:
    case PTC_REQUEST_LAB_PHASE_START:
    case PTC_REQUEST_LAB_SESSION_STATUS:
    case PTC_REQUEST_LAB_OBSERVATION:
    case PTC_REQUEST_LAB_SESSION_RESTORE:
        (void)ptc_lab_process_request(sysmodule, &request);
        break;
#endif
    case PTC_REQUEST_STATUS:
        (void)process_status(sysmodule, &request, now);
        break;
    case PTC_REQUEST_CLEAR_REDEMPTION_HISTORY:
        (void)process_clear_redemption_history(sysmodule, &request, now);
        break;
    case PTC_REQUEST_CLEAR_ACTIVITY_HISTORY:
        (void)process_clear_activity_history(sysmodule, &request, now);
        break;
    case PTC_REQUEST_COMPLETE_SETUP:
        (void)process_complete_setup(sysmodule, &request, &config, disable_flag, now);
        break;
    case PTC_REQUEST_RETRY_SETUP_RELEASE:
        (void)process_retry_setup_release(sysmodule, &request, &config, now);
        break;
    case PTC_REQUEST_RESTORE_INSTALL_SNAPSHOT:
        (void)process_restore_install_snapshot(sysmodule, &request, now);
        break;
    case PTC_REQUEST_SET_TODAY_LIMIT:
    case PTC_REQUEST_ADD_TODAY_MINUTES:
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
    case PTC_REQUEST_SET_HOLIDAY_POLICY:
    case PTC_REQUEST_SET_SCHEDULED_OVERRIDE:
    case PTC_REQUEST_SET_AUTONOMY_POLICY:
    case PTC_REQUEST_SET_BEDTIME_POLICY:
    case PTC_REQUEST_CONFIRM_BEDTIME_REQUIREMENTS:
        (void)process_rule_request(sysmodule, &request, disable_flag, now);
        break;
    case PTC_REQUEST_UNKNOWN:
    default:
        (void)finish_with_error(sysmodule, &request, "release", true, PTC_ERR_UNKNOWN_REQUEST_TYPE, now.day_index);
        break;
    }
}

#ifdef PLAYWISE_EDEN
void ptc_sysmodule_process_request_direct(PtcSysmodule *sysmodule,
    const char *request_text, const char *expected_request_id)
{
    if (!sysmodule || !request_text) return;
    process_request_text(sysmodule, request_text, expected_request_id);
}
#endif
