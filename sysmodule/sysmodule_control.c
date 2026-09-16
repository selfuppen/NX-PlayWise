#include "sysmodule_internal.h"

static PtcErrorCode restore_install_snapshot_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now);
static PtcErrorCode release_setup_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now);
static PtcErrorCode direct_handover_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now);

bool request_file_path(char *out, size_t out_size, const PtcSysmodule *sysmodule, const char *queue, const char *name)
{
    int written = snprintf(out, out_size, "%s/inbox/%s/%.127s", sysmodule->app_root, queue, name);
    return written >= 0 && (size_t)written < out_size;
}
static PtcErrorCode backup_before_write(PtcSysmodule *sysmodule, const PtcRequest *request, const char *mode)
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
    err = backup_before_write(sysmodule, request, mode_name);
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


static bool effect_snapshot_equal(const PtcPctlSettingsSnapshot *a, const PtcPctlSettingsSnapshot *b)
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
        *raw_restored = effect_snapshot_equal(original, restored_snapshot);
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

/* A fresh takeover must not silently replace an in-progress Nintendo allowance
   with PlayWise's weekly defaults.  PCTL targets are daily totals, so retain the
   observed configured total when possible; only derive it from spent + remaining
   when both values are available. */
static PtcErrorCode capture_handover_today(PtcSetupState *setup, const PtcPctlStatus *status, PtcClockSnapshot now)
{
    uint32_t total;
    if (setup->handover_today_pending) {
        return PTC_ERR_OK;
    }
    setup->handover_day_index = now.day_index;
    setup->handover_unlimited = false;
    setup->handover_minutes = 0;
    setup->handover_remaining_available = false;
    setup->handover_remaining_minutes = 0;
    if (status->unrestricted_today && status->restricted_now_available && !status->restricted_now) {
        setup->handover_today_pending = true;
        setup->handover_unlimited = true;
        return PTC_ERR_OK;
    }
    if (!status->limited_today && !status->blocked_today) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }
    if (status->blocked_today) {
        setup->handover_today_pending = true;
        setup->handover_remaining_available = status->remaining_available;
        setup->handover_remaining_minutes = 0;
        return PTC_ERR_OK;
    }
    if (!status->remaining_available) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }
    if (status->configured_minutes_available && status->configured_minutes <= PTC_TOKEN_MAX_MINUTES) {
        total = status->configured_minutes;
    } else if (status->played_minutes_available &&
               status->played_minutes <= PTC_TOKEN_MAX_MINUTES &&
               status->remaining_minutes <= PTC_TOKEN_MAX_MINUTES &&
               status->played_minutes + status->remaining_minutes <= PTC_TOKEN_MAX_MINUTES) {
        total = status->played_minutes + status->remaining_minutes;
    } else {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }
    setup->handover_today_pending = true;
    setup->handover_minutes = (uint16_t)total;
    setup->handover_remaining_available = true;
    setup->handover_remaining_minutes = status->remaining_minutes > UINT16_MAX
        ? UINT16_MAX : (uint16_t)status->remaining_minutes;
    return PTC_ERR_OK;
}

static PtcErrorCode persist_handover_today_rule(
    PtcSysmodule *sysmodule,
    const PtcSetupState *setup,
    PtcClockSnapshot now)
{
    PtcRules rules;
    if (!setup->handover_today_pending || setup->handover_day_index != now.day_index) {
        return PTC_ERR_OK;
    }
    if (!load_rules(sysmodule, &rules)) {
        return PTC_ERR_RULES_INVALID;
    }
    rules.today_override.present = true;
    rules.today_override.day_index = now.day_index;
    rules.today_override.rule.mode = setup->handover_unlimited
        ? PTC_RULE_MODE_UNLIMITED : PTC_RULE_MODE_LIMIT;
    rules.today_override.rule.minutes = setup->handover_unlimited ? 0U : setup->handover_minutes;
    return save_rules(sysmodule, &rules) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
}

static PtcErrorCode run_release_preflight(
    PtcSysmodule *sysmodule,
    const PtcRuntimeConfig *config,
    PtcSetupState *setup,
    PtcClockSnapshot now,
    bool capture_handover)
{
    char path[320];
    char text[4096];
    char profile[24];
    char version[24];
    char release_id[96];
    char pin_hash[80];
    char hos[32] = "unknown";
    char firmware_hash[96] = "unknown";
    char model[32] = "unknown";
    char atmosphere_version[24] = "unknown";
    bool atmosphere = false;
    bool environment_read_ok = false;
    const char *compatibility_status;
    char compatibility[768];
    PtcPctlSettingsSnapshot snapshot;
    PtcPctlStatus status;
    PtcRules rules;
    PtcErrorCode err;

    join_path(path, sizeof(path), sysmodule->app_root, "build.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text)) ||
        !json_string(text, "profile", profile, sizeof(profile)) || strcmp(profile, PLAYWISE_PROFILE_NAME) != 0 ||
        !json_string(text, "playwise_version", version, sizeof(version)) || strcmp(version, PLAYWISE_VERSION) != 0 ||
        !json_string(text, "release_id", release_id, sizeof(release_id))) {
        return PTC_ERR_RELEASE_MANIFEST_INVALID;
    }
    join_path(path, sizeof(path), sysmodule->app_root, "auth.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text)) ||
        !json_string(text, "pin_hash", pin_hash, sizeof(pin_hash)) || strlen(pin_hash) != 64U ||
        strlen(config->grant_secret) < 32U || !load_rules(sysmodule, &rules)) {
        return PTC_ERR_SETUP_PENDING;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    err = sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &snapshot);
    if (err != PTC_ERR_OK) return PTC_ERR_PCTL_BACKUP_FAILED;
    if (snapshot.size != PTC_PCTL_OPAQUE_SETTINGS_SIZE) return PTC_ERR_PCTL_LAYOUT_MISMATCH;
    err = sysmodule->pctl->vtable->read_status(
        sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &status);
    if (err != PTC_ERR_OK) return err;
    if (capture_handover) {
        err = capture_handover_today(setup, &status, now);
        if (err != PTC_ERR_OK) return err;
    }

    join_path(path, sizeof(path), sysmodule->app_root, "environment.json");
    if (sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        (void)json_bool_value(text, "read_ok", &environment_read_ok);
        (void)json_string(text, "hos", hos, sizeof(hos));
        (void)json_string(text, "firmware_hash", firmware_hash, sizeof(firmware_hash));
        (void)json_string(text, "model", model, sizeof(model));
        (void)json_bool_value(text, "atmosphere", &atmosphere);
        (void)json_string(text, "atmosphere_version", atmosphere_version, sizeof(atmosphere_version));
    }
    compatibility_status = environment_read_ok && strcmp(hos, "22.5.0") == 0 &&
        strcmp(model, "mariko-oled") == 0 && atmosphere &&
        strcmp(atmosphere_version, "1.11.2") == 0 ? "verified" : "accepted_unknown";

    snprintf(
        compatibility,
        sizeof(compatibility),
        "{\"version\":1,\"status\":\"%s\",\"environment\":{"
        "\"hos\":\"%s\",\"firmware_hash\":\"%s\",\"model\":\"%s\","
        "\"atmosphere\":%s,\"atmosphere_version\":\"%s\",\"pctl_profile\":\"layout-v1\"},"
        "\"release_id\":\"%s\",\"accepted_at\":%lld}\n",
        compatibility_status,
        hos,
        firmware_hash,
        model,
        atmosphere ? "true" : "false",
        atmosphere_version,
        release_id,
        (long long)now.unix_seconds);
    join_path(path, sizeof(path), sysmodule->app_root, "compatibility.json");
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, compatibility)) {
        return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    snprintf(setup->compatibility_status, sizeof(setup->compatibility_status), "%s", compatibility_status);
    return PTC_ERR_OK;
}

bool process_complete_setup(PtcSysmodule *sysmodule, const PtcRequest *request,
    const PtcRuntimeConfig *config, bool disable_flag, PtcClockSnapshot now)
{
    PtcSetupState setup;
    bool resuming_disabled_setup;
    bool reconfirming_runtime_fingerprint = false;
    bool retrying_failed_release = false;
    char disable_path[320];
    if (!load_setup_state(sysmodule, &setup)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_SETUP_PENDING, now.day_index);
    }
    /* A completed takeover is idempotent: stale clients may resubmit after
       navigating back, but must never repeat the snapshot or PCTL writes. */
    if (strcmp(setup.phase, "active") == 0 && !disable_flag) {
        return write_current_status_result(
            sysmodule, request, "release", false, now, false);
    }
    if (disable_flag) {
        char reason[64];
        join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
        if (sysmodule->storage->vtable->read_text(sysmodule->storage, disable_path, reason, sizeof(reason))) {
            size_t length = strcspn(reason, "\r\n");
            reason[length] = '\0';
            reconfirming_runtime_fingerprint = strcmp(setup.phase, "protection") == 0 &&
                strcmp(reason, "runtime_fingerprint_changed") == 0;
            retrying_failed_release = setup.handover_today_pending &&
                strcmp(reason, "setup_release_failed") == 0;
        }
    }
    resuming_disabled_setup = disable_flag &&
        (strcmp(setup.phase, "restored") == 0 || strcmp(setup.phase, "active") == 0 ||
         reconfirming_runtime_fingerprint || retrying_failed_release);
    if (disable_flag && !resuming_disabled_setup) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_DISABLED, now.day_index);
    }
    if (strcmp(setup.phase, "unconfigured") == 0 || strcmp(setup.phase, "compatibility_pending") == 0 ||
        strcmp(setup.phase, "protection") == 0 || strcmp(setup.phase, "restored") == 0 ||
        (strcmp(setup.phase, "pending") == 0 && setup.handover_today_pending) ||
        resuming_disabled_setup) {
        bool capture_handover = !setup.handover_today_pending &&
            (strcmp(setup.phase, "unconfigured") == 0 || strcmp(setup.phase, "compatibility_pending") == 0 ||
             (strcmp(setup.phase, "protection") == 0 && !reconfirming_runtime_fingerprint));
        PtcErrorCode preflight_err = run_release_preflight(sysmodule, config, &setup, now, capture_handover);
        if (preflight_err != PTC_ERR_OK) {
            /* A disabled active/restored installation remains retryable until a
               later parent-confirmed attempt passes the read-only preflight. */
            if (!resuming_disabled_setup) {
                snprintf(setup.phase, sizeof(setup.phase), "protection");
                snprintf(setup.compatibility_status, sizeof(setup.compatibility_status), "protection");
            }
            snprintf(setup.last_error, sizeof(setup.last_error), "%s", ptc_error_reason(preflight_err));
            (void)save_setup_state(sysmodule, &setup);
            return finish_with_error(sysmodule, request, "release", false,
                preflight_err, now.day_index);
        }
        preflight_err = persist_handover_today_rule(sysmodule, &setup, now);
        if (preflight_err != PTC_ERR_OK) {
            snprintf(setup.phase, sizeof(setup.phase), "protection");
            snprintf(setup.compatibility_status, sizeof(setup.compatibility_status), "protection");
            snprintf(setup.last_error, sizeof(setup.last_error), "%s", ptc_error_reason(preflight_err));
            (void)save_setup_state(sysmodule, &setup);
            return finish_with_error(sysmodule, request, "release", false,
                preflight_err, now.day_index);
        }
        snprintf(setup.phase, sizeof(setup.phase), "pending");
        if (!save_setup_state(sysmodule, &setup)) {
            return finish_with_error(sysmodule, request, "release", false,
                PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
        }
        if (setup.handover_today_pending) {
            PtcErrorCode handover_err = direct_handover_now(sysmodule, &setup, now);
            if (handover_err != PTC_ERR_OK) {
                return finish_with_error(sysmodule, request, "release", false,
                    handover_err, now.day_index);
            }
            if (disable_flag) {
                join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
                if (!sysmodule->storage->vtable->remove_path(sysmodule->storage, disable_path)) {
                    return finish_with_error(sysmodule, request, "release", false,
                        PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
                }
            }
            return write_current_status_result(
                sysmodule, request, "release", false, now, false);
        }
        {
            PtcErrorCode release_err = release_setup_now(sysmodule, &setup, now);
            if (release_err != PTC_ERR_OK) {
                return finish_with_error(sysmodule, request, "release", false,
                    release_err, now.day_index);
            }
        }
    }
    if (strcmp(setup.phase, "released") != 0 || !setup.restriction_cleared) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_SETUP_PENDING, now.day_index);
    }
    setup.activate_after = now.unix_seconds + 5;
    setup.last_error[0] = '\0';
    if (!save_setup_state(sysmodule, &setup)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    if (disable_flag) {
        join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
        if (!sysmodule->storage->vtable->remove_path(sysmodule->storage, disable_path)) {
            return finish_with_error(sysmodule, request, "release", false,
                PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
        }
    }
    return write_current_status_result(sysmodule, request, "release", false, now, false);
}

bool process_retry_setup_release(PtcSysmodule *sysmodule, const PtcRequest *request,
    const PtcRuntimeConfig *config, PtcClockSnapshot now)
{
    PtcSetupState setup;
    char disable_path[320];
    PtcErrorCode err;
    if (!load_setup_state(sysmodule, &setup) ||
        (strcmp(setup.phase, "pending") != 0 && strcmp(setup.phase, "failed") != 0 &&
         strcmp(setup.phase, "protection") != 0)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_BAD_REQUEST, now.day_index);
    }
    err = run_release_preflight(sysmodule, config, &setup, now, !setup.handover_today_pending);
    if (err != PTC_ERR_OK) {
        snprintf(setup.last_error, sizeof(setup.last_error), "%s", ptc_error_reason(err));
        (void)save_setup_state(sysmodule, &setup);
        return finish_with_error(sysmodule, request, "release", false,
            err, now.day_index);
    }
    err = persist_handover_today_rule(sysmodule, &setup, now);
    if (err != PTC_ERR_OK) {
        snprintf(setup.last_error, sizeof(setup.last_error), "%s", ptc_error_reason(err));
        (void)save_setup_state(sysmodule, &setup);
        return finish_with_error(sysmodule, request, "release", false,
            err, now.day_index);
    }
    snprintf(setup.phase, sizeof(setup.phase), "pending");
    setup.restriction_cleared = false;
    setup.activate_after = 0;
    setup.last_error[0] = '\0';
    if (!save_setup_state(sysmodule, &setup)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    err = setup.handover_today_pending
        ? direct_handover_now(sysmodule, &setup, now)
        : release_setup_now(sysmodule, &setup, now);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", false,
            err, now.day_index);
    }
    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, disable_path) &&
        !sysmodule->storage->vtable->remove_path(sysmodule->storage, disable_path)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    return write_current_status_result(sysmodule, request, "release", false, now, false);
}

bool process_restore_install_snapshot(PtcSysmodule *sysmodule, const PtcRequest *request,
    PtcClockSnapshot now)
{
    PtcSetupState setup;
    PtcErrorCode err;
    if (!load_setup_state(sysmodule, &setup)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_RECOVERY_UNAVAILABLE, now.day_index);
    }
    err = restore_install_snapshot_now(sysmodule, &setup, now);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", false,
            err, now.day_index);
    }
    return write_current_status_result(sysmodule, request, "release", false, now, false);
}

void write_disable_flag(PtcSysmodule *sysmodule, const char *reason)
{
    char path[320];
    PtcActivityHistoryRecord record;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    join_path(path, sizeof(path), sysmodule->app_root, "flags/disable.flag");
    if (!sysmodule->storage->vtable->write_text_atomic(
            sysmodule->storage, path, reason ? reason : "disabled\n")) return;
    memset(&record, 0, sizeof(record));
    record.occurred_at = now.unix_seconds;
    record.day_index = now.day_index;
    snprintf(record.action, sizeof(record.action), "protection");
    (void)save_activity_history(sysmodule, &record);
}

static PtcErrorCode restore_install_snapshot_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now)
{
    PtcPctlSettingsSnapshot original;
    PtcPctlSettingsSnapshot restored;
    PtcPctlStatus status;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    bool raw_restored = false;
    bool timer_restored = false;
    PtcErrorCode err;
    if (!load_install_snapshot(sysmodule, &original)) {
        return PTC_ERR_RECOVERY_UNAVAILABLE;
    }
    err = restore_snapshot_exact(sysmodule, &original, &restored, &status,
        ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
    if (err != PTC_ERR_OK || !raw_restored || !timer_restored) {
        snprintf(setup->phase, sizeof(setup->phase), "failed");
        setup->restriction_cleared = false;
        setup->snapshot_available = true;
        setup->activate_after = 0;
        snprintf(setup->last_error, sizeof(setup->last_error), "recovery_failed");
        (void)save_setup_state(sysmodule, setup);
        write_disable_flag(sysmodule, "recovery_failed\n");
        return PTC_ERR_RECOVERY_FAILED;
    }
    /* A full installation restore is also the terminal PlayWise escape hatch.
       Persist that bedtime is no longer effective so the refreshed Overlay
       result cannot claim that a successfully restored PCTL snapshot remains
       restricted by the old window. */
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &runtime_state)) {
        write_disable_flag(sysmodule, "recovery_state_unavailable\n");
        return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    rules.bedtime.enabled = false;
    runtime_state.bedtime_enforced = false;
    runtime_state.bedtime_window_instance_id = 0;
    runtime_state.bedtime_start_day_index = 0;
    runtime_state.bedtime_skipped_instance_id = 0;
    runtime_state.apply_pending_confirmation = false;
    runtime_state.apply_confirmation_deadline = 0;
    runtime_state.pending_mode = 0;
    runtime_state.pending_minutes = 0;
    if (!save_rules(sysmodule, &rules) ||
        !save_state(sysmodule, &runtime_state, now.unix_seconds)) {
        write_disable_flag(sysmodule, "recovery_state_failed\n");
        return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    clear_bedtime_snapshot(sysmodule);
    snprintf(setup->phase, sizeof(setup->phase), "restored");
    setup->restriction_cleared = false;
    setup->snapshot_available = true;
    setup->activate_after = 0;
    setup->last_error[0] = '\0';
    if (!save_setup_state(sysmodule, setup)) {
        write_disable_flag(sysmodule, "recovery_state_failed\n");
        return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    write_disable_flag(sysmodule, "install_snapshot_restored\n");
    return PTC_ERR_OK;
}

static PtcErrorCode release_setup_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now)
{
    char snapshot_path[320];
    PtcPctlSettingsSnapshot original;
    PtcPctlSettingsSnapshot restored;
    PtcPctlStatus before;
    PtcPctlStatus released;
    PtcPctlTarget target;
    bool raw_restored = false;
    bool timer_restored = false;
    PtcErrorCode err;
    memset(&original, 0, sizeof(original));
    memset(&restored, 0, sizeof(restored));
    memset(&before, 0, sizeof(before));
    memset(&released, 0, sizeof(released));
    join_path(snapshot_path, sizeof(snapshot_path), sysmodule->app_root, "backups/install_pctl_snapshot.json");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, snapshot_path)) {
        if (!load_install_snapshot(sysmodule, &original)) {
            snprintf(setup->phase, sizeof(setup->phase), "failed");
            setup->restriction_cleared = false;
            setup->snapshot_available = false;
            setup->activate_after = 0;
            snprintf(setup->last_error, sizeof(setup->last_error), "recovery_unavailable");
            (void)save_setup_state(sysmodule, setup);
            write_disable_flag(sysmodule, "setup_snapshot_invalid\n");
            return PTC_ERR_RECOVERY_UNAVAILABLE;
        }
    } else {
        if (!sysmodule->pctl->vtable->snapshot_settings ||
            sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &original) != PTC_ERR_OK ||
            !save_install_snapshot(sysmodule, &original, now.unix_seconds)) {
            snprintf(setup->phase, sizeof(setup->phase), "failed");
            setup->restriction_cleared = false;
            setup->snapshot_available = false;
            setup->activate_after = 0;
            snprintf(setup->last_error, sizeof(setup->last_error), "pctl_backup_failed");
            (void)save_setup_state(sysmodule, setup);
            write_disable_flag(sysmodule, "setup_snapshot_failed\n");
            return PTC_ERR_PCTL_BACKUP_FAILED;
        }
    }
    setup->snapshot_available = true;
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &before);
    if (err == PTC_ERR_OK && before.unrestricted_today &&
        before.restricted_now_available && !before.restricted_now) {
        snprintf(setup->phase, sizeof(setup->phase), "released");
        setup->restriction_cleared = true;
        setup->activate_after = 0;
        setup->last_error[0] = '\0';
        return save_setup_state(sysmodule, setup) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    }
    err = backup_before_write(sysmodule, NULL, "setup");
    if (err == PTC_ERR_OK) {
        target.mode = PTC_PCTL_TARGET_UNLIMITED;
        target.minutes = 0;
        target.weekday = ptc_weekday_from_day_index(now.day_index);
        err = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &target);
    }
    if (err == PTC_ERR_OK) {
        err = observe_target_with_optional_activation(sysmodule, NULL, now, "setup",
            PTC_PCTL_TARGET_UNLIMITED, 0, "setup_release", &released);
    }
    if (err != PTC_ERR_OK) {
        PtcErrorCode restore_error = restore_snapshot_exact(sysmodule, &original, &restored, &released,
            ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
        snprintf(setup->phase, sizeof(setup->phase), "failed");
        setup->restriction_cleared = false;
        setup->activate_after = 0;
        snprintf(setup->last_error, sizeof(setup->last_error), "%s",
            restore_error == PTC_ERR_OK ? ptc_error_reason(err) : "recovery_failed");
        (void)save_setup_state(sysmodule, setup);
        write_disable_flag(sysmodule, restore_error == PTC_ERR_OK ? "setup_release_failed\n" : "setup_restore_failed\n");
        return restore_error == PTC_ERR_OK ? err : PTC_ERR_RECOVERY_FAILED;
    }
    snprintf(setup->phase, sizeof(setup->phase), "released");
    setup->restriction_cleared = true;
    setup->activate_after = 0;
    setup->last_error[0] = '\0';
    return save_setup_state(sysmodule, setup) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
}

static bool handover_remaining_matches(const PtcSetupState *setup, const PtcPctlStatus *status)
{
    uint16_t observed;
    uint16_t expected;
    if (setup->handover_unlimited) {
        return status->unrestricted_today && status->restricted_now_available && !status->restricted_now;
    }
    if (!setup->handover_remaining_available) {
        return setup->handover_minutes == 0U && status->restricted_now_available && status->restricted_now;
    }
    if (!status->remaining_available) {
        return false;
    }
    observed = status->remaining_minutes > UINT16_MAX ? UINT16_MAX : (uint16_t)status->remaining_minutes;
    expected = setup->handover_remaining_minutes;
    /* 1454 is rounded down to minutes, so the five-second setup grace may make
       a successful handover differ by one displayed minute. */
    return observed >= expected ? observed - expected <= 1 : expected - observed <= 1;
}

/* A fresh install already has the policy and remaining allowance that must be
   preserved.  Taking it over in place avoids a transient unlimited write and,
   importantly, avoids starting private timer command 1451 while on HOME. */
static PtcErrorCode direct_handover_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now)
{
    char snapshot_path[320];
    PtcPctlSettingsSnapshot installed;
    PtcPctlSettingsSnapshot current;
    PtcPctlStatus status;
    PtcRuntimeState runtime_state;
    PtcRules rules;
    PtcDayRule rule;
    PtcPctlTargetMode target_mode;
    bool snapshot_exists;
    bool state_matches;
    PtcErrorCode err;

    if (!setup->handover_today_pending || setup->handover_day_index != now.day_index) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &runtime_state)) {
        return PTC_ERR_RULES_INVALID;
    }
    rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    if (!rules.today_override.present || rules.today_override.day_index != now.day_index ||
        (setup->handover_unlimited ? rule.mode != PTC_RULE_MODE_UNLIMITED :
         rule.mode != PTC_RULE_MODE_LIMIT || rule.minutes != setup->handover_minutes)) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }

    memset(&installed, 0, sizeof(installed));
    memset(&current, 0, sizeof(current));
    join_path(snapshot_path, sizeof(snapshot_path), sysmodule->app_root, "backups/install_pctl_snapshot.json");
    snapshot_exists = sysmodule->storage->vtable->exists(sysmodule->storage, snapshot_path);
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &current) != PTC_ERR_OK ||
        current.size != PTC_PCTL_OPAQUE_SETTINGS_SIZE) {
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    if (snapshot_exists) {
        if (!load_install_snapshot(sysmodule, &installed)) {
            return PTC_ERR_RECOVERY_UNAVAILABLE;
        }
        if (!effect_snapshot_equal(&installed, &current) || installed.timer_enabled != current.timer_enabled) {
            return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
        }
    } else {
        if (!save_install_snapshot(sysmodule, &current, now.unix_seconds)) {
            return PTC_ERR_STORAGE_WRITE_FAILED;
        }
        append_event(sysmodule, NULL, "pctl_backup", PTC_ERR_OK, "direct_handover");
    }
    setup->snapshot_available = true;

    err = sysmodule->pctl->vtable->read_status(
        sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &status);
    if (err != PTC_ERR_OK) {
        return err;
    }
    if (!status.restriction_enabled_available || !status.restriction_enabled) {
        return PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
    }
    target_mode = target_from_day_rule(rule);
    if (!target_settings_observed(target_mode, rule.minutes, &status)) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }
    if (setup->handover_unlimited) {
        state_matches = status.unrestricted_today;
    } else if (setup->handover_minutes == 0U) {
        state_matches = status.blocked_today &&
            (!status.remaining_available || status.remaining_minutes == 0U);
    } else {
        state_matches = handover_remaining_matches(setup, &status);
    }
    if (!state_matches) {
        return PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
    }

    runtime_state.last_enforced_day_index = now.day_index;
    runtime_state.last_enforced_mode = target_mode;
    runtime_state.last_enforced_minutes = rule.minutes;
    if (!save_state(sysmodule, &runtime_state, now.unix_seconds)) {
        return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    snprintf(setup->phase, sizeof(setup->phase), "active");
    setup->restriction_cleared = false;
    setup->activate_after = 0;
    setup->handover_today_pending = false;
    setup->last_error[0] = '\0';
    if (!save_setup_state(sysmodule, setup)) {
        return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    append_event(sysmodule, NULL, "handover_preserved", PTC_ERR_OK, "direct_takeover");
    return PTC_ERR_OK;
}

static PtcErrorCode activate_handover_today(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now)
{
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcPctlStatus observed;
    PtcDayRule rule;
    PtcErrorCode err;
    if (!setup->handover_today_pending) {
        return PTC_ERR_OK;
    }
    if (setup->handover_day_index != now.day_index) {
        /* The captured allowance belongs to yesterday. Never carry it into a
           new day; the ordinary weekly plan takes over instead. */
        setup->handover_today_pending = false;
        return save_setup_state(sysmodule, setup) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
    }
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &runtime_state)) {
        err = PTC_ERR_RULES_INVALID;
        goto restore_original;
    }
    rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    if (!rules.today_override.present || rules.today_override.day_index != now.day_index ||
        (setup->handover_unlimited ? rule.mode != PTC_RULE_MODE_UNLIMITED :
         rule.mode != PTC_RULE_MODE_LIMIT || rule.minutes != setup->handover_minutes)) {
        err = PTC_ERR_HANDOVER_STATE_UNAVAILABLE;
        goto restore_original;
    }
    err = apply_target(sysmodule, NULL, now, "setup_handover",
        target_from_day_rule(rule), rule.minutes);
    if (err != PTC_ERR_OK) {
        goto restore_original;
    }
    err = observe_target_with_optional_activation(sysmodule, NULL, now, "setup_handover",
        target_from_day_rule(rule), rule.minutes, "setup_handover", &observed);
    if (err != PTC_ERR_OK || !handover_remaining_matches(setup, &observed)) {
        if (err == PTC_ERR_OK) err = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
        goto restore_original;
    }
    runtime_state.last_enforced_day_index = now.day_index;
    runtime_state.last_enforced_mode = target_from_day_rule(rule);
    runtime_state.last_enforced_minutes = rule.minutes;
    if (!save_state(sysmodule, &runtime_state, now.unix_seconds)) {
        err = PTC_ERR_STORAGE_WRITE_FAILED;
        goto restore_original;
    }
    setup->handover_today_pending = false;
    if (!save_setup_state(sysmodule, setup)) {
        err = PTC_ERR_STORAGE_WRITE_FAILED;
        goto restore_original;
    }
    append_event(sysmodule, NULL, "handover_preserved", PTC_ERR_OK, "today_allowance");
    recovery_clear(sysmodule);
    return PTC_ERR_OK;

restore_original:
    append_event(sysmodule, NULL, "handover_restore", err, "today_allowance");
    setup->handover_today_pending = false;
    if (restore_install_snapshot_now(sysmodule, setup, now) == PTC_ERR_OK) {
        recovery_clear(sysmodule);
        return err;
    }
    write_disable_flag(sysmodule, "handover_restore_failed\n");
    return PTC_ERR_RECOVERY_FAILED;
}

#if !defined(PLAYWISE_DEVICE_LAB) && !defined(PLAYWISE_EDEN)
static PtcErrorCode validate_runtime_fingerprint(PtcSysmodule *sysmodule)
{
    char path[320];
    char build[4096];
    char compatibility[2048];
    char environment[1024];
    char build_profile[24];
    char build_version[24];
    char build_release_id[96];
    char accepted_release_id[96];
    char accepted_hos[32];
    char current_hos[32];
    char accepted_model[32];
    char current_model[32];
    char accepted_hash[96];
    char current_hash[96];
    char accepted_atmosphere_version[24];
    char current_atmosphere_version[24];
    bool accepted_atmosphere = false;
    bool current_atmosphere = false;
    bool current_read_ok = false;

    join_path(path, sizeof(path), sysmodule->app_root, "build.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, build, sizeof(build)) ||
        !json_string(build, "profile", build_profile, sizeof(build_profile)) || strcmp(build_profile, PLAYWISE_PROFILE_NAME) != 0 ||
        !json_string(build, "playwise_version", build_version, sizeof(build_version)) || strcmp(build_version, PLAYWISE_VERSION) != 0 ||
        !json_string(build, "release_id", build_release_id, sizeof(build_release_id))) {
        return PTC_ERR_RELEASE_MANIFEST_INVALID;
    }
    join_path(path, sizeof(path), sysmodule->app_root, "compatibility.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, compatibility, sizeof(compatibility)) ||
        !json_string(compatibility, "release_id", accepted_release_id, sizeof(accepted_release_id)) ||
        strcmp(build_release_id, accepted_release_id) != 0) {
        return PTC_ERR_RELEASE_MANIFEST_INVALID;
    }
    join_path(path, sizeof(path), sysmodule->app_root, "environment.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, environment, sizeof(environment))) {
        return PTC_ERR_COMPATIBILITY_CONFIRMATION_REQUIRED;
    }
    (void)json_bool_value(environment, "read_ok", &current_read_ok);
    if (!current_read_ok) return PTC_ERR_COMPATIBILITY_CONFIRMATION_REQUIRED;
    if (!json_string(compatibility, "hos", accepted_hos, sizeof(accepted_hos)) ||
        !json_string(environment, "hos", current_hos, sizeof(current_hos)) ||
        !json_string(compatibility, "model", accepted_model, sizeof(accepted_model)) ||
        !json_string(environment, "model", current_model, sizeof(current_model)) ||
        !json_string(compatibility, "firmware_hash", accepted_hash, sizeof(accepted_hash)) ||
        !json_string(environment, "firmware_hash", current_hash, sizeof(current_hash)) ||
        !json_string(compatibility, "atmosphere_version", accepted_atmosphere_version, sizeof(accepted_atmosphere_version)) ||
        !json_string(environment, "atmosphere_version", current_atmosphere_version, sizeof(current_atmosphere_version)) ||
        !json_bool_value(compatibility, "atmosphere", &accepted_atmosphere) ||
        !json_bool_value(environment, "atmosphere", &current_atmosphere)) {
        return PTC_ERR_COMPATIBILITY_CONFIRMATION_REQUIRED;
    }
    if (strcmp(accepted_hos, current_hos) != 0 || strcmp(accepted_model, current_model) != 0 ||
        strcmp(accepted_hash, current_hash) != 0 || accepted_atmosphere != current_atmosphere ||
        strcmp(accepted_atmosphere_version, current_atmosphere_version) != 0) {
        return PTC_ERR_COMPATIBILITY_CONFIRMATION_REQUIRED;
    }
    return PTC_ERR_OK;
}
#endif

int ptc_sysmodule_bootstrap_setup(PtcSysmodule *sysmodule)
{
    char restore_flag[320];
    char retry_flag[320];
    char disable_path[320];
    PtcSetupState setup;
    PtcClockSnapshot now;
    PtcErrorCode err = PTC_ERR_OK;
    bool check_startup_recovery;
    if (!sysmodule || !load_setup_state(sysmodule, &setup)) return -1;
    check_startup_recovery = !sysmodule->startup_recovery_checked;
    sysmodule->startup_recovery_checked = true;
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    join_path(restore_flag, sizeof(restore_flag), sysmodule->app_root, "flags/restore_install_snapshot.flag");
    join_path(retry_flag, sizeof(retry_flag), sysmodule->app_root, "flags/retry_setup_release.flag");
    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, restore_flag)) {
        err = restore_install_snapshot_now(sysmodule, &setup, now);
        (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, restore_flag);
        if (err == PTC_ERR_OK) recovery_clear(sysmodule);
        return err == PTC_ERR_OK ? 1 : -1;
    }
    if (sysmodule->storage->vtable->exists(sysmodule->storage, retry_flag)) {
        if (recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule)) {
            write_disable_flag(sysmodule, "startup_recovery_failed\n");
            (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, retry_flag);
            return -1;
        }
        snprintf(setup.phase, sizeof(setup.phase), "pending");
        setup.restriction_cleared = false;
        setup.activate_after = 0;
        setup.last_error[0] = '\0';
        (void)save_setup_state(sysmodule, &setup);
        (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, retry_flag);
        (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, disable_path);
    }
    /* recovery/active is treated as abandoned only once per sysmodule boot.
       Later scheduler ticks may see a live Enforce confirmation transaction. */
    if (check_startup_recovery && recovery_path_exists(sysmodule)) {
        if (!recovery_rollback(sysmodule)) {
            write_disable_flag(sysmodule, "startup_recovery_failed\n");
            return -1;
        }
        write_disable_flag(sysmodule, "startup_transaction_restored\n");
        return 1;
    }
    if (strcmp(setup.phase, "active") == 0 || strcmp(setup.phase, "released") == 0) {
        /* The emulator has no real set:sys fingerprint to re-confirm, so keep the
           gate out of the simulated build instead of letting it park every boot
           in protection with disable.flag. */
#if !defined(PLAYWISE_DEVICE_LAB) && !defined(PLAYWISE_EDEN)
        PtcErrorCode gate_err = validate_runtime_fingerprint(sysmodule);
        if (gate_err != PTC_ERR_OK) {
            snprintf(setup.phase, sizeof(setup.phase), "protection");
            snprintf(setup.compatibility_status, sizeof(setup.compatibility_status), "pending");
            snprintf(setup.last_error, sizeof(setup.last_error), "%s", ptc_error_reason(gate_err));
            (void)save_setup_state(sysmodule, &setup);
            write_disable_flag(sysmodule, "runtime_fingerprint_changed\n");
            return -1;
        }
#endif
    }
    if (strcmp(setup.phase, "released") == 0 && setup.activate_after > 0 && now.unix_seconds >= setup.activate_after) {
        err = activate_handover_today(sysmodule, &setup, now);
        if (err != PTC_ERR_OK) {
            return -1;
        }
        snprintf(setup.phase, sizeof(setup.phase), "active");
        setup.activate_after = 0;
        setup.last_error[0] = '\0';
        return save_setup_state(sysmodule, &setup) ? 1 : -1;
    }
    return 0;
}
