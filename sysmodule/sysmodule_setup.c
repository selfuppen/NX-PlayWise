#include "sysmodule_internal.h"

static PtcErrorCode restore_install_snapshot_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now);
static PtcErrorCode release_setup_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now);
static PtcErrorCode direct_handover_now(PtcSysmodule *sysmodule, PtcSetupState *setup, PtcClockSnapshot now);

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
    err = ptc_backup_before_write(sysmodule, NULL, "setup");
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
        if (!ptc_pctl_settings_snapshot_equal(&installed, &current) ||
            installed.timer_enabled != current.timer_enabled) {
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
