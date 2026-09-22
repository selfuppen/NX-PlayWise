#include "sysmodule_internal.h"

static bool process_disable_today_limit(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    bool disable_flag,
    PtcClockSnapshot now)
{
    PtcPctlSettingsSnapshot original_snapshot;
    PtcPctlSettingsSnapshot restored_snapshot;
    PtcPctlStatus before_status;
    PtcPctlStatus observed_status;
    PtcPctlStatus restored_status;
    PtcRules original_rules;
    PtcRules updated_rules;
    PtcRuntimeState runtime_state;
    PtcErrorCode err;
    PtcErrorCode restore_error = PTC_ERR_OK;
    bool rules_existed;
    bool rules_persisted = false;
    bool pctl_changed = false;
    bool raw_restored = false;
    bool timer_restored = false;
    char rules_path[320];

    memset(&original_snapshot, 0, sizeof(original_snapshot));
    memset(&restored_snapshot, 0, sizeof(restored_snapshot));
    memset(&before_status, 0, sizeof(before_status));
    memset(&observed_status, 0, sizeof(observed_status));
    memset(&restored_status, 0, sizeof(restored_status));
    if (disable_flag) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_DISABLED, now.day_index);
    }
    if (!load_rules(sysmodule, &original_rules)) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_RULES_INVALID, now.day_index);
    }
    if (!load_state(sysmodule, &runtime_state)) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_BAD_REQUEST, now.day_index);
    }
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &before_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", true, err, now.day_index);
    }
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &original_snapshot) != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", false, PTC_ERR_PCTL_BACKUP_FAILED, now.day_index);
    }

    updated_rules = original_rules;
    updated_rules.today_override.present = true;
    updated_rules.today_override.day_index = now.day_index;
    updated_rules.today_override.rule.mode = PTC_RULE_MODE_UNLIMITED;
    updated_rules.today_override.rule.minutes = 0;
    join_path(rules_path, sizeof(rules_path), sysmodule->app_root, "rules.json");
    rules_existed = sysmodule->storage->vtable->exists(sysmodule->storage, rules_path);

    pctl_changed = true;
    err = apply_target(sysmodule, request, now, "release", PTC_PCTL_TARGET_UNLIMITED, 0);
    if (err == PTC_ERR_OK) {
        err = observe_target_with_optional_activation(sysmodule, request, now,
            "release", PTC_PCTL_TARGET_UNLIMITED, 0,
            "disable_today_limit", &observed_status);
    }
    if (err != PTC_ERR_OK) {
        goto disable_today_rollback;
    }
    if (!save_rules(sysmodule, &updated_rules)) {
        err = PTC_ERR_STORAGE_WRITE_FAILED;
        goto disable_today_rollback;
    }
    rules_persisted = true;
    append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "disable_today_limit");
    if (write_current_status_result(sysmodule, request, "release", false, now, true)) {
        recovery_clear(sysmodule);
        return true;
    }
    err = PTC_ERR_STORAGE_WRITE_FAILED;
    append_event(sysmodule, request, "result_write_failed", err, "disable_today_limit");

disable_today_rollback:
    if (pctl_changed) {
        restore_error = restore_snapshot_exact(sysmodule, &original_snapshot, &restored_snapshot, &restored_status,
            ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
        append_event(sysmodule, request,
            restore_error == PTC_ERR_OK ? "effect_restore" : "effect_restore_failed",
            restore_error, "disable_today_limit");
    }
    if (rules_persisted) {
        if (!restore_rules(sysmodule, &original_rules, rules_existed)) {
            restore_error = PTC_ERR_STORAGE_WRITE_FAILED;
        }
    }
    if (restore_error != PTC_ERR_OK) {
        err = PTC_ERR_PCTL_RESTORE_FAILED;
        write_disable_flag(sysmodule, "transaction_restore_failed\n");
    } else {
        recovery_clear(sysmodule);
    }
    return finish_with_error(sysmodule, request, "release", false, err, now.day_index);
}

PtcErrorCode restore_bedtime_base(PtcSysmodule *sysmodule, const PtcRequest *request,
    const PtcRules *rules,
    PtcRuntimeState *runtime_state, PtcClockSnapshot now)
{
    PtcPctlSettingsSnapshot snapshot;
    PtcPctlSettingsSnapshot restored;
    PtcPctlStatus status;
    PtcPctlStatus observed;
    uint64_t snapshot_instance = 0;
    uint16_t snapshot_start_day = 0;
    bool raw_restored = false;
    bool timer_restored = false;
    PtcErrorCode err;
    if (!runtime_state->bedtime_enforced) return PTC_ERR_OK;
    if (!recovery_begin(sysmodule, request, now)) return PTC_ERR_PCTL_BACKUP_FAILED;
    if (now.day_index == runtime_state->bedtime_start_day_index &&
        load_bedtime_snapshot(sysmodule, &snapshot, &snapshot_instance, &snapshot_start_day) &&
        snapshot_instance == runtime_state->bedtime_window_instance_id &&
        snapshot_start_day == runtime_state->bedtime_start_day_index) {
        err = restore_snapshot_exact(sysmodule, &snapshot, &restored, &status,
            ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
        if (err != PTC_ERR_OK || !raw_restored || !timer_restored) return PTC_ERR_BEDTIME_RECOVERY_FAILED;
    } else {
        PtcDayRule base = ptc_rules_today_rule(
            rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
        err = apply_target(sysmodule, request, now, "release",
            target_from_day_rule(base), base.minutes);
        if (err != PTC_ERR_OK) return PTC_ERR_BEDTIME_RECOVERY_FAILED;
        err = observe_target_with_optional_activation(sysmodule, request, now,
            "release", target_from_day_rule(base), base.minutes,
            "bedtime_restore_base", &observed);
        if (err != PTC_ERR_OK) return PTC_ERR_BEDTIME_RECOVERY_FAILED;
    }
    runtime_state->bedtime_enforced = false;
    runtime_state->bedtime_window_instance_id = 0;
    runtime_state->bedtime_start_day_index = 0;
    runtime_state->last_enforced_day_index = 0;
    runtime_state->last_enforced_mode = 0;
    runtime_state->last_enforced_minutes = 0;
    if (!save_state(sysmodule, runtime_state, now.unix_seconds)) return PTC_ERR_STORAGE_WRITE_FAILED;
    clear_bedtime_snapshot(sysmodule);
    append_event(sysmodule, request, "bedtime_recovered", PTC_ERR_OK,
        now.day_index == snapshot_start_day ? "same_day_snapshot" : "current_day_rule");
    return PTC_ERR_OK;
}

static bool bedtime_instance_is_upcoming(const PtcRules *rules, PtcClockSnapshot now, uint64_t instance_id)
{
    unsigned int offset;
    PtcBedtimeEvaluation current = ptc_bedtime_evaluate(
        rules, now.day_index, ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
    if (current.active && current.window_instance_id == instance_id) return true;
    for (offset = 0; offset < 8u; ++offset) {
        uint16_t day = (uint16_t)(now.day_index + offset);
        PtcEffectiveBedtime next = ptc_bedtime_resolve_start_day(
            rules, day, ptc_weekday_from_day_index(day));
        if (!next.window.enabled || (offset == 0u && now.minute_of_day >= next.window.start_minute)) continue;
        return ptc_bedtime_window_instance_id(day, next.window.start_minute) == instance_id;
    }
    return false;
}

static bool process_bedtime_recovery_request(PtcSysmodule *sysmodule, const PtcRequest *request,
    PtcClockSnapshot now)
{
    PtcRules rules;
    PtcRuntimeState state;
    PtcBedtimeEvaluation current;
    PtcErrorCode err;
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &state)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_RULES_INVALID, now.day_index);
    }
    current = ptc_bedtime_evaluate(
        &rules, now.day_index, ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
    if (request->type == PTC_REQUEST_SKIP_BEDTIME) {
        if (!bedtime_instance_is_upcoming(&rules, now, request->bedtime_window_instance_id)) {
            return finish_with_error(sysmodule, request, "release", false,
                PTC_ERR_BEDTIME_INSTANCE_NOT_ACTIVE, now.day_index);
        }
        state.bedtime_skipped_instance_id = request->bedtime_window_instance_id;
    } else {
        rules.bedtime.enabled = false;
    }
    if (current.active && state.bedtime_enforced &&
        (request->type == PTC_REQUEST_DISABLE_BEDTIME ||
         request->bedtime_window_instance_id == current.window_instance_id)) {
        err = restore_bedtime_base(sysmodule, request, &rules, &state, now);
        if (err != PTC_ERR_OK) {
            write_disable_flag(sysmodule, "bedtime_restore_failed\n");
            return finish_with_error(sysmodule, request, "release", false,
                PTC_ERR_BEDTIME_RECOVERY_FAILED, now.day_index);
        }
    }
    if (!save_rules(sysmodule, &rules) || !save_state(sysmodule, &state, now.unix_seconds)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    (void)record_activity(sysmodule, request, now, 0, 0);
    return write_current_status_result(sysmodule, request, "release",
        false, now, recovery_path_exists(sysmodule));
}

static bool process_overlay_ready(PtcSysmodule *sysmodule, const PtcRequest *request,
    PtcClockSnapshot now)
{
    char path[320];
    char text[512];
    char build[1024];
    char build_release_id[96];
    char fingerprint[65];
    join_path(path, sizeof(path), sysmodule->app_root, "build.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, build, sizeof(build)) ||
        !json_string(build, "release_id", build_release_id, sizeof(build_release_id)) ||
        strcmp(build_release_id, request->overlay_release_id) != 0 ||
        !sysmodule_environment_fingerprint(sysmodule, fingerprint) ||
        strcmp(fingerprint, request->environment_fingerprint) != 0) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_OVERLAY_UNVERIFIED, now.day_index);
    }
    join_path(path, sizeof(path), sysmodule->app_root, "overlay/ready.json");
    snprintf(text, sizeof(text),
        "{\"version\":1,\"release_id\":\"%s\",\"boot_id\":\"%s\","
        "\"environment_fingerprint\":\"%s\",\"confirmed_at\":%lld}\n",
        build_release_id, sysmodule->boot_id, request->environment_fingerprint,
        (long long)now.unix_seconds);
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    return write_current_status_result(sysmodule, request, "release",
        false, now, false);
}

bool process_recovery_request_surface(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    bool disable_flag,
    PtcClockSnapshot now)
{
    switch (request->type) {
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
        (void)process_disable_today_limit(sysmodule, request, disable_flag, now);
        return true;
    case PTC_REQUEST_SKIP_BEDTIME:
    case PTC_REQUEST_DISABLE_BEDTIME:
        (void)process_bedtime_recovery_request(sysmodule, request, now);
        return true;
    case PTC_REQUEST_OVERLAY_READY:
        (void)process_overlay_ready(sysmodule, request, now);
        return true;
    default:
        return false;
    }
}
