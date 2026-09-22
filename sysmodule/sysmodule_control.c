#include "sysmodule_internal.h"


bool request_file_path(char *out, size_t out_size, const PtcSysmodule *sysmodule, const char *queue, const char *name)
{
    int written = snprintf(out, out_size, "%s/inbox/%s/%.127s", sysmodule->app_root, queue, name);
    return written >= 0 && (size_t)written < out_size;
}
PtcErrorCode ptc_backup_before_write(PtcSysmodule *sysmodule, const PtcRequest *request, const char *mode)
{
    char path[320];
    PtcPctlBackup backup;
    PtcPctlDebugSnapshot snapshot;
    uint32_t ipc_result;
    PtcErrorCode err = sysmodule->pctl->vtable->backup(sysmodule->pctl, &backup);
    ipc_result = last_pctl_ipc_result(sysmodule);
    if (err != PTC_ERR_OK) {
        append_event(sysmodule, request, "pctl_backup_failed", err, "");
        take_pctl_debug_snapshot(sysmodule, &snapshot);
        append_pctl_debug(sysmodule, request, "backup", mode, NULL, err, ipc_result, NULL, &snapshot);
        return err;
    }
    snprintf(path, sizeof(path), "%s/backups/last_pctl_backup.txt", sysmodule->app_root);
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, backup.text)) {
        append_event(sysmodule, request, "pctl_backup_failed", PTC_ERR_PCTL_BACKUP_FAILED, "storage");
        take_pctl_debug_snapshot(sysmodule, &snapshot);
        append_pctl_debug(sysmodule, request, "backup", mode, NULL, PTC_ERR_PCTL_BACKUP_FAILED, ipc_result, NULL, &snapshot);
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    append_event(sysmodule, request, "pctl_backup", PTC_ERR_OK, "");
    take_pctl_debug_snapshot(sysmodule, &snapshot);
    append_pctl_debug(sysmodule, request, "backup", mode, NULL, PTC_ERR_OK, ipc_result, NULL, &snapshot);
    return PTC_ERR_OK;
}

PtcErrorCode apply_target(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now,
    const char *mode_name,
    PtcPctlTargetMode mode,
    uint16_t minutes)
{
    PtcPctlTarget target;
    PtcPctlDebugSnapshot before;
    PtcPctlDebugSnapshot after;
    PtcErrorCode err;
    if (!recovery_begin(sysmodule, request, now)) {
        append_event(sysmodule, request, "pctl_backup_failed", PTC_ERR_PCTL_BACKUP_FAILED, "recovery_transaction");
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    err = ptc_backup_before_write(sysmodule, request, mode_name);
    if (err != PTC_ERR_OK) {
        recovery_clear(sysmodule);
        return err;
    }
    target.mode = mode;
    target.minutes = minutes;
    target.weekday = ptc_weekday_from_day_index(now.day_index);
    take_pctl_debug_snapshot(sysmodule, &before);
    err = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &target);
    {
        uint32_t ipc_result = last_pctl_ipc_result(sysmodule);
        take_pctl_debug_snapshot(sysmodule, &after);
        append_pctl_debug(sysmodule, request, "apply_target", mode_name, &target, err, ipc_result, &before, &after);
    }
    append_event(sysmodule, request, err == PTC_ERR_OK ? "pctl_apply" : "pctl_apply_failed", err, pctl_target_mode_name(mode));
    if (err != PTC_ERR_OK && !recovery_rollback(sysmodule)) {
        write_disable_flag(sysmodule, "transaction_restore_failed\n");
        return PTC_ERR_RECOVERY_FAILED;
    }
    return err;
}

bool request_file_stem(const char *name, char *out, size_t out_size)
{
    size_t len;
    if (!name || !out) return false;
    len = strlen(name);
    if (len <= 5 || strcmp(name + len - 5, ".json") != 0 || len - 5 >= out_size) return false;
    memcpy(out, name, len - 5);
    out[len - 5] = '\0';
    return ptc_request_id_is_valid(out);
}


bool finish_with_error(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *mode,
    bool dry_run,
    PtcErrorCode error,
    uint16_t day_index)
{
    char json[4096];
    PtcResultState state;
    if (recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule)) {
        write_disable_flag(sysmodule, "transaction_restore_failed\n");
        error = PTC_ERR_RECOVERY_FAILED;
    }
    ptc_result_state_default(&state, day_index);
    (void)ptc_result_error_json(
        json,
        sizeof(json),
        request ? request->request_id : "unknown",
        request ? request->type_text : "unknown",
        mode,
        dry_run,
        error,
        &state,
        sysmodule->time_provider->vtable->now(sysmodule->time_provider).unix_seconds);
    append_event(sysmodule, request, "result_error", error, "");
    return write_result(sysmodule, request ? request->request_id : "unknown", json);
}

PtcPctlTargetMode target_from_day_rule(PtcDayRule rule)
{
    if (rule.mode == PTC_RULE_MODE_UNLIMITED) {
        return PTC_PCTL_TARGET_UNLIMITED;
    }
    return PTC_PCTL_TARGET_LIMIT;
}

uint16_t ptc_pctl_played_minutes(const PtcPctlStatus *status)
{
    if (!status) {
        return 0U;
    }
    if (status->played_minutes_available) {
        return status->played_minutes > UINT16_MAX ? UINT16_MAX : (uint16_t)status->played_minutes;
    }
    if (!status->configured_minutes_available || !status->limited_today) {
        return 0U;
    }
    if (status->remaining_available && status->configured_minutes >= status->remaining_minutes) {
        uint32_t played = (uint32_t)status->configured_minutes - status->remaining_minutes;
        if (status->restricted_now && played < status->configured_minutes) {
            return status->configured_minutes;
        }
        return (uint16_t)played;
    }
    if (status->restricted_now || status->remaining_minutes == 0U) {
        return status->configured_minutes;
    }
    return 0U;
}

/* Offline codes and add_today_minutes both stack onto the existing daily limit
   instead of overwriting it. Base is max(today's effective LIMIT minutes, played_minutes);
   unlimited or blocked days start from played_minutes (or 0 if played is 0). The total is
   clamped to the single-day maximum so even a large accumulation still writes successfully to PCTL.
   Sets today_override in place and returns the resulting minutes. */
uint16_t accumulate_today_limit(PtcRules *rules, uint16_t day_index, uint8_t weekday, uint16_t add_minutes, uint16_t played_minutes)
{
    PtcDayRule active = ptc_rules_today_rule(rules, day_index, weekday);
    uint32_t base = (active.mode == PTC_RULE_MODE_LIMIT) ? active.minutes : 0u;
    if (played_minutes > base) {
        base = played_minutes;
    }
    uint32_t total = base + add_minutes;
    if (total > PTC_TOKEN_MAX_MINUTES) {
        total = PTC_TOKEN_MAX_MINUTES;
    }
    rules->today_override.present = true;
    rules->today_override.day_index = day_index;
    rules->today_override.rule.mode = PTC_RULE_MODE_LIMIT;
    rules->today_override.rule.minutes = (uint16_t)total;
    return (uint16_t)total;
}

PtcErrorCode update_rules_for_request(PtcSysmodule *sysmodule, const PtcRequest *request, PtcRules *rules, PtcRuntimeState *runtime_state, PtcClockSnapshot now, uint16_t played_minutes)
{
    switch (request->type) {
    case PTC_REQUEST_SET_TODAY_LIMIT:
        rules->today_override.present = true;
        rules->today_override.day_index = now.day_index;
        rules->today_override.rule.mode = PTC_RULE_MODE_LIMIT;
        rules->today_override.rule.minutes = request->minutes;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_ADD_TODAY_MINUTES:
        (void)accumulate_today_limit(rules, now.day_index, ptc_weekday_from_day_index(now.day_index), request->minutes, played_minutes);
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
        rules->today_override.present = true;
        rules->today_override.day_index = now.day_index;
        rules->today_override.rule.mode = PTC_RULE_MODE_UNLIMITED;
        rules->today_override.rule.minutes = 0;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
        rules->today_override.present = false;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
        memcpy(rules->week, request->week, sizeof(rules->week));
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_SET_HOLIDAY_POLICY:
        rules->holiday_enabled = request->holiday_enabled;
        rules->holiday_rule = request->holiday_rule;
        rules->makeup_workday_rule = request->makeup_workday_rule;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_SET_SCHEDULED_OVERRIDE:
        rules->scheduled_override = request->scheduled_override;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_SET_AUTONOMY_POLICY:
        rules->autonomy_policy = request->autonomy_policy;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    case PTC_REQUEST_CONFIRM_BEDTIME_REQUIREMENTS:
    {
        char fingerprint[65];
        if (!request->bedtime_official_setting_confirmed ||
            request->bedtime_confirmation_version == 0 || request->environment_fingerprint[0] == '\0' ||
            !sysmodule_environment_fingerprint(sysmodule, fingerprint) ||
            strcmp(fingerprint, request->environment_fingerprint) != 0) {
            return PTC_ERR_BEDTIME_CONFIRMATION_REQUIRED;
        }
        rules->bedtime.confirmation_version = request->bedtime_confirmation_version;
        rules->bedtime.official_setting_confirmed_at = now.unix_seconds;
        snprintf(rules->bedtime.confirmed_environment,
            sizeof(rules->bedtime.confirmed_environment), "%s", request->environment_fingerprint);
        rules->bedtime.unverified_overlay_risk_accepted = request->bedtime_overlay_risk_accepted;
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    }
    case PTC_REQUEST_SET_BEDTIME_POLICY: {
        PtcBedtimeEvaluation before = ptc_bedtime_evaluate(
            rules, now.day_index, ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
        PtcBedtimeEvaluation after;
        PtcBedtimePolicy next = request->bedtime_policy;
        next.confirmation_version = rules->bedtime.confirmation_version;
        next.official_setting_confirmed_at = rules->bedtime.official_setting_confirmed_at;
        snprintf(next.confirmed_environment, sizeof(next.confirmed_environment), "%s",
            rules->bedtime.confirmed_environment);
        next.unverified_overlay_risk_accepted = rules->bedtime.unverified_overlay_risk_accepted;
        if (next.enabled) {
#ifdef PLAYWISE_EDEN
            /* Eden owns a simulated PCTL adapter and cannot load Tesla. Its purpose
               is to exercise the full rule/enforcement/recovery flow in one NRO. */
            next.confirmation_version = 1;
            next.official_setting_confirmed_at = now.unix_seconds;
            snprintf(next.confirmed_environment, sizeof(next.confirmed_environment), "eden-simulated");
            next.unverified_overlay_risk_accepted = true;
#else
            char fingerprint[65];
            if (next.confirmation_version == 0 || next.official_setting_confirmed_at <= 0 ||
                next.confirmed_environment[0] == '\0' ||
                !sysmodule_environment_fingerprint(sysmodule, fingerprint) ||
                strcmp(fingerprint, next.confirmed_environment) != 0) {
                return PTC_ERR_BEDTIME_CONFIRMATION_REQUIRED;
            }
            /* The risk acknowledgement is needed only for the disabled -> enabled
               transition. Once enabled, a missing Overlay handshake is surfaced as
               a warning but cannot become a way to bypass the active schedule. */
            if (!rules->bedtime.enabled && !bedtime_overlay_verified(sysmodule) &&
                !next.unverified_overlay_risk_accepted) {
                return PTC_ERR_OVERLAY_UNVERIFIED;
            }
#endif
        }
        rules->bedtime = next;
        after = ptc_bedtime_evaluate(
            rules, now.day_index, ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
        if (!request->bedtime_apply_immediately && after.active && !before.active) {
            runtime_state->bedtime_skipped_instance_id = after.window_instance_id;
            if (!save_state(sysmodule, runtime_state, now.unix_seconds)) return PTC_ERR_STORAGE_WRITE_FAILED;
        }
        return save_rules(sysmodule, rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    }
    default:
        return PTC_ERR_OK;
    }
}


bool ptc_pctl_settings_snapshot_equal(const PtcPctlSettingsSnapshot *a, const PtcPctlSettingsSnapshot *b)
{
    return a && b && a->size == b->size && a->size <= PTC_PCTL_OPAQUE_SETTINGS_SIZE &&
        memcmp(a->data, b->data, a->size) == 0;
}


void effect_wait(PtcSysmodule *sysmodule, uint32_t milliseconds)
{
    if (sysmodule->time_provider && sysmodule->time_provider->vtable && sysmodule->time_provider->vtable->sleep_ms) {
        sysmodule->time_provider->vtable->sleep_ms(sysmodule->time_provider, milliseconds);
    }
}


static bool target_status_observed(
    PtcPctlTargetMode mode,
    uint16_t minutes,
    const PtcPctlStatus *status)
{
    if (mode == PTC_PCTL_TARGET_UNLIMITED) {
        return status->unrestricted_today && status->restricted_now_available && !status->restricted_now;
    }
    if (mode == PTC_PCTL_TARGET_BLOCKED) {
        return status->blocked_today;
    }
    if (!status->limited_today || !status->configured_minutes_available ||
        status->configured_minutes != minutes || !status->remaining_available) {
        return false;
    }
    /* Command 1455 is an instantaneous "is a title restricted now" query. On
       hardware it can return false again after Horizon has shown the limit and
       suspended or exited the affected title. Exact settings readback plus an
       exhausted 1454 value therefore proves the requested target without
       requiring restricted_now to remain latched. */
    return status->restricted_now || status->remaining_minutes == 0U ||
        (status->remaining_minutes > 0U && status->play_timer_enabled_available && status->play_timer_enabled);
}

bool bedtime_blocks_grants(PtcSysmodule *sysmodule, PtcClockSnapshot now)
{
    PtcRules rules;
    PtcRuntimeState state;
    PtcBedtimeEvaluation evaluation;
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &state)) return false;
    evaluation = ptc_bedtime_evaluate(
        &rules, now.day_index, ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
    return evaluation.active && state.bedtime_skipped_instance_id != evaluation.window_instance_id;
}

bool target_settings_observed(
    PtcPctlTargetMode mode,
    uint16_t minutes,
    const PtcPctlStatus *status);

static bool target_runtime_ready(
    PtcPctlTargetMode mode,
    uint16_t minutes,
    const PtcPctlStatus *status)
{
    if (!target_settings_observed(mode, minutes, status)) {
        return false;
    }
    /* Nintendo pauses its play timer while the parent PIN temporarily unlocks
       restrictions. Preserve that behavior: settings may change, but an
       interactive request must not restart command 1451 before Sleep Mode. */
    if (status->temporary_unlocked_available && status->temporary_unlocked) {
        return true;
    }
    if (mode == PTC_PCTL_TARGET_UNLIMITED) {
        return status->restricted_now_available && !status->restricted_now;
    }
    if (mode == PTC_PCTL_TARGET_BLOCKED || minutes == 0U ||
        (status->remaining_available && status->remaining_minutes == 0U)) {
        return status->restricted_now_available && status->restricted_now;
    }
    return status->remaining_available && status->remaining_minutes > 0U &&
        status->play_timer_enabled_available && status->play_timer_enabled &&
        status->restricted_now_available && !status->restricted_now;
}

static bool target_runtime_confirmed_after_activation(
    PtcPctlTargetMode mode,
    uint16_t minutes,
    const PtcPctlStatus *status)
{
    if (target_runtime_ready(mode, minutes, status)) return true;
    /* 1455 is instantaneous. After the one allowed activation attempt, an
       exhausted or blocked target may already have shown the system prompt and
       returned to restricted_now=false. Exact settings plus exhausted 1454 is
       sufficient; prompt visibility and software outcome remain Lab observations. */
    if (mode == PTC_PCTL_TARGET_BLOCKED || minutes == 0U ||
        (status->remaining_available && status->remaining_minutes == 0U)) {
        return target_status_observed(mode, minutes, status);
    }
    return false;
}

/* Daily rule synchronization only proves that the configured target was
   written. It must not activate private command 1451 in the background: HOME
   and other awake console use legitimately consume Nintendo's allowance, so an
   autonomous activation could charge time when no parent action requested it. */
bool target_settings_observed(
    PtcPctlTargetMode mode,
    uint16_t minutes,
    const PtcPctlStatus *status)
{
    if (mode == PTC_PCTL_TARGET_UNLIMITED) {
        return status->unrestricted_today;
    }
    if (mode == PTC_PCTL_TARGET_BLOCKED) {
        return status->blocked_today;
    }
    return status->limited_today && status->configured_minutes_available &&
        status->configured_minutes == minutes;
}

/* Interactive writes first prove the 0x44 target without touching command 1451.
   A single activation fallback is allowed only after the settings are exact but
   the current console-use state is not ready yet. Background Enforce never calls
   this helper and therefore cannot start a timer while nobody is using the console. */
PtcErrorCode observe_target_with_optional_activation(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now,
    const char *mode_name,
    PtcPctlTargetMode target_mode,
    uint16_t minutes,
    const char *event_detail,
    PtcPctlStatus *observed)
{
    PtcPctlTarget target;
    PtcPctlDebugSnapshot before;
    PtcPctlDebugSnapshot after;
    PtcErrorCode err = PTC_ERR_OK;
    bool settings_seen = false;
    unsigned int i;
    memset(observed, 0, sizeof(*observed));
    target.mode = target_mode;
    target.minutes = minutes;
    target.weekday = ptc_weekday_from_day_index(now.day_index);

    for (i = 0; i < 20U; ++i) {
        err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, target.weekday, observed);
        if (err == PTC_ERR_OK && target_settings_observed(target_mode, minutes, observed)) {
            settings_seen = true;
            if (target_runtime_ready(target_mode, minutes, observed)) {
                append_event(sysmodule, request, "effect_observed", PTC_ERR_OK, event_detail);
                return PTC_ERR_OK;
            }
            break;
        }
        if (i + 1U < 20U) effect_wait(sysmodule, 250U);
    }
    if (!settings_seen) {
        append_event(sysmodule, request, "pctl_apply_failed",
            err == PTC_ERR_OK ? PTC_ERR_PCTL_EFFECT_NOT_OBSERVED : err,
            "settings_not_observed_before_activation");
        return err == PTC_ERR_OK ? PTC_ERR_PCTL_EFFECT_NOT_OBSERVED : err;
    }

    take_pctl_debug_snapshot(sysmodule, &before);
    err = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
    take_pctl_debug_snapshot(sysmodule, &after);
    append_pctl_debug(sysmodule, request, "start_timer_fallback", mode_name, &target, err,
        last_pctl_ipc_result(sysmodule), &before, &after);
    append_event(sysmodule, request,
        err == PTC_ERR_OK ? "pctl_start_timer_fallback" : "pctl_apply_failed",
        err, event_detail);
    if (err != PTC_ERR_OK) return err;

    for (i = 0; i < 20U; ++i) {
        err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, target.weekday, observed);
        if (err == PTC_ERR_OK &&
            target_runtime_confirmed_after_activation(target_mode, minutes, observed)) {
            append_event(sysmodule, request, "effect_observed", PTC_ERR_OK, event_detail);
            return PTC_ERR_OK;
        }
        if (i + 1U < 20U) effect_wait(sysmodule, 250U);
    }
    append_event(sysmodule, request, "pctl_apply_failed",
        err == PTC_ERR_OK ? PTC_ERR_PCTL_EFFECT_NOT_OBSERVED : err,
        "activation_fallback_not_observed");
    return err == PTC_ERR_OK ? PTC_ERR_PCTL_EFFECT_NOT_OBSERVED : err;
}


PtcErrorCode restore_snapshot_exact(
    PtcSysmodule *sysmodule,
    const PtcPctlSettingsSnapshot *original,
    PtcPctlSettingsSnapshot *restored_snapshot,
    PtcPctlStatus *restored_status,
    uint8_t weekday,
    bool *raw_restored,
    bool *timer_restored)
{
    PtcErrorCode err;
    *raw_restored = false;
    *timer_restored = false;
    memset(restored_snapshot, 0, sizeof(*restored_snapshot));
    memset(restored_status, 0, sizeof(*restored_status));
    if (!sysmodule->pctl->vtable->restore_settings || !sysmodule->pctl->vtable->snapshot_settings) {
        return PTC_ERR_PCTL_RESTORE_FAILED;
    }
    err = sysmodule->pctl->vtable->restore_settings(sysmodule->pctl, original);
    if (err == PTC_ERR_OK) {
        err = original->timer_enabled
            ? sysmodule->pctl->vtable->start_timer(sysmodule->pctl)
            : sysmodule->pctl->vtable->stop_timer(sysmodule->pctl);
    }
    if (err == PTC_ERR_OK) {
        err = sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, restored_snapshot);
    }
    if (err == PTC_ERR_OK) {
        *raw_restored = ptc_pctl_settings_snapshot_equal(original, restored_snapshot);
        *timer_restored = restored_snapshot->timer_enabled == original->timer_enabled;
        if (!*raw_restored || !*timer_restored) {
            err = PTC_ERR_PCTL_RESTORE_FAILED;
        }
    }
    if (err == PTC_ERR_OK) {
        err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, weekday, restored_status);
    }
    return err;
}
