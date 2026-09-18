#include "sysmodule_internal.h"

int ptc_sysmodule_enforce_tick(PtcSysmodule *sysmodule)
{
    PtcRuntimeConfig config;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    PtcDayRule active_rule;
    PtcBedtimeEvaluation bedtime;
    PtcPctlStatus observed_status;
    PtcPctlTargetMode target_mode;
    uint16_t target_minutes;
    char disable_path[320];
    PtcSetupState setup;
    PtcErrorCode err;
    bool bedtime_should_enforce;

    if (!load_config(sysmodule, &config)) {
        return 0;
    }
    if (!load_setup_state(sysmodule, &setup) || strcmp(setup.phase, "active") != 0) {
        return 0;
    }
    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    if (sysmodule->storage->vtable->exists(sysmodule->storage, disable_path)) {
        return 0;
    }
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &runtime_state)) {
        append_event(sysmodule, NULL, "result_error", PTC_ERR_RULES_INVALID, "enforce");
        return 0;
    }
    if (runtime_state.apply_pending_confirmation) {
        PtcPctlStatus pending_status;
        PtcErrorCode pending_err = sysmodule->pctl->vtable->read_status(
            sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pending_status);
        if (pending_err == PTC_ERR_OK && target_settings_observed(
                runtime_state.pending_mode, runtime_state.pending_minutes, &pending_status)) {
            runtime_state.apply_pending_confirmation = false;
            runtime_state.apply_confirmation_deadline = 0;
            runtime_state.pending_mode = 0;
            runtime_state.pending_minutes = 0;
            if (!save_state(sysmodule, &runtime_state, now.unix_seconds)) {
                write_disable_flag(sysmodule, "pending_confirmation_state_failed\n");
                return 0;
            }
            append_event(sysmodule, NULL, "effect_observed", PTC_ERR_OK, "enforce_pending_confirmation");
            recovery_clear(sysmodule);
        } else if (now.unix_seconds < runtime_state.apply_confirmation_deadline) {
            return 0;
        } else {
            append_event(sysmodule, NULL, "pctl_apply_failed", PTC_ERR_PCTL_EFFECT_NOT_OBSERVED,
                "enforce_pending_confirmation_timeout");
            if (!recovery_rollback(sysmodule)) {
                write_disable_flag(sysmodule, "pending_confirmation_restore_failed\n");
            } else if (runtime_state.bedtime_enforced) {
                clear_bedtime_snapshot(sysmodule);
            }
            return 0;
        }
    }
    active_rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    bedtime = ptc_bedtime_evaluate(
        &rules, now.day_index, ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
    bedtime_should_enforce = bedtime.active &&
        runtime_state.bedtime_skipped_instance_id != bedtime.window_instance_id;
    if (runtime_state.bedtime_enforced &&
        (!bedtime_should_enforce || runtime_state.bedtime_window_instance_id != bedtime.window_instance_id)) {
        err = restore_bedtime_base(sysmodule, NULL, &rules, &runtime_state, now);
        if (err != PTC_ERR_OK) {
            write_disable_flag(sysmodule, "bedtime_restore_failed\n");
            append_event(sysmodule, NULL, "pctl_apply_failed", PTC_ERR_BEDTIME_RECOVERY_FAILED,
                "bedtime_exit");
            return 0;
        }
        recovery_clear(sysmodule);
    }
    target_mode = bedtime_should_enforce ? PTC_PCTL_TARGET_BLOCKED : target_from_day_rule(active_rule);
    target_minutes = bedtime_should_enforce ? 0u : active_rule.minutes;
    if (runtime_state.last_enforced_day_index == now.day_index &&
        runtime_state.last_enforced_mode == target_mode &&
        runtime_state.last_enforced_minutes == target_minutes &&
        runtime_state.bedtime_enforced == bedtime_should_enforce &&
        (!bedtime_should_enforce || runtime_state.bedtime_window_instance_id == bedtime.window_instance_id)) {
        if (sysmodule->pctl->vtable->read_status(sysmodule->pctl,
                ptc_weekday_from_day_index(now.day_index), &observed_status) != PTC_ERR_OK ||
            target_settings_observed(target_mode, target_minutes, &observed_status)) {
            return 0;
        }
    }
    if (bedtime_should_enforce && !runtime_state.bedtime_enforced) {
        PtcPctlSettingsSnapshot bedtime_snapshot;
        if (!sysmodule->pctl->vtable->snapshot_settings ||
            sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &bedtime_snapshot) != PTC_ERR_OK ||
            !save_bedtime_snapshot(sysmodule, &bedtime_snapshot, &bedtime, now.unix_seconds)) {
            append_event(sysmodule, NULL, "pctl_backup_failed", PTC_ERR_PCTL_BACKUP_FAILED,
                "bedtime_entry");
            return 0;
        }
    }
    err = apply_target(sysmodule, NULL, now, "release", target_mode, target_minutes);
    if (err != PTC_ERR_OK) {
        if (bedtime_should_enforce && !runtime_state.bedtime_enforced) clear_bedtime_snapshot(sysmodule);
        return 0;
    }
    {
        unsigned int i;
        for (i = 0; i < 20U; ++i) {
            err = sysmodule->pctl->vtable->read_status(
                sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &observed_status);
            if (err == PTC_ERR_OK && target_settings_observed(target_mode, target_minutes, &observed_status)) {
                append_event(sysmodule, NULL, "effect_observed", PTC_ERR_OK, "enforce_settings");
                break;
            }
            if (i + 1U < 20U) {
                effect_wait(sysmodule, 250U);
            }
        }
        if (err == PTC_ERR_OK && !target_settings_observed(target_mode, target_minutes, &observed_status)) {
            err = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
            append_event(sysmodule, NULL, "pctl_apply_failed", err, "enforce_settings");
        }
    }
    if (err != PTC_ERR_OK) {
        if (err != PTC_ERR_PCTL_EFFECT_NOT_OBSERVED) {
            if (!recovery_rollback(sysmodule)) write_disable_flag(sysmodule, "enforce_restore_failed\n");
            return 0;
        }
        runtime_state.apply_pending_confirmation = true;
        runtime_state.apply_confirmation_deadline = now.unix_seconds + 30;
        runtime_state.pending_mode = target_mode;
        runtime_state.pending_minutes = target_minutes;
        append_event(sysmodule, NULL, "enforce_effect_pending", PTC_ERR_OK, "applied_pending_confirmation");
    } else {
        runtime_state.apply_pending_confirmation = false;
        runtime_state.apply_confirmation_deadline = 0;
        runtime_state.pending_mode = 0;
        runtime_state.pending_minutes = 0;
    }
    runtime_state.last_enforced_day_index = now.day_index;
    runtime_state.last_enforced_mode = target_mode;
    runtime_state.last_enforced_minutes = target_minutes;
    runtime_state.bedtime_enforced = bedtime_should_enforce;
    runtime_state.bedtime_window_instance_id = bedtime_should_enforce ? bedtime.window_instance_id : 0;
    runtime_state.bedtime_start_day_index = bedtime_should_enforce ? bedtime.start_day_index : 0;
    if (!save_state(sysmodule, &runtime_state, now.unix_seconds)) {
        append_event(sysmodule, NULL, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "enforce_state");
        if (!recovery_rollback(sysmodule)) write_disable_flag(sysmodule, "enforce_restore_failed\n");
        return 0;
    }
    append_event(sysmodule, NULL, "state_persisted", PTC_ERR_OK, "enforce");
    if (!runtime_state.apply_pending_confirmation) recovery_clear(sysmodule);
    return 1;
}

void ptc_sysmodule_init(
    PtcSysmodule *sysmodule,
    const char *app_root,
    PtcStorage *storage,
    PtcPctl *pctl,
    PtcTimeProvider *time_provider)
{
    snprintf(sysmodule->app_root, sizeof(sysmodule->app_root), "%s", app_root);
    sysmodule->storage = storage;
    sysmodule->pctl = pctl;
    sysmodule->time_provider = time_provider;
    sysmodule->scan_backoff_ms = 500;
    sysmodule->minute_initialized = false;
    sysmodule->cleanup_initialized = false;
    sysmodule->startup_recovery_checked = false;
    sysmodule->disable_initialized = false;
    sysmodule->disable_present = false;
    snprintf(sysmodule->boot_id, sizeof(sysmodule->boot_id), "host-boot");
    invalidate_all_caches(sysmodule);
    (void)ptc_sysmodule_refresh_caches(sysmodule);
}

void ptc_sysmodule_set_boot_id(PtcSysmodule *sysmodule, const char *boot_id)
{
    size_t i;
    if (!sysmodule || !boot_id || !boot_id[0]) return;
    for (i = 0; boot_id[i] && i + 1 < sizeof(sysmodule->boot_id); ++i) {
        char ch = boot_id[i];
        sysmodule->boot_id[i] = ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-') ? ch : '-';
    }
    sysmodule->boot_id[i] = '\0';
}

int ptc_sysmodule_recover_processing(PtcSysmodule *sysmodule)
{
    char dir[320];
    char names[32][128];
    size_t count = 0;
    size_t i;
    int recovered = 0;
    snprintf(dir, sizeof(dir), "%s/inbox/processing", sysmodule->app_root);
    if (!sysmodule->storage->vtable->list_json(sysmodule->storage, dir, names, 32, &count)) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        char from[320];
        char to[320];
        if (!request_file_path(from, sizeof(from), sysmodule, "processing", names[i]) ||
            !request_file_path(to, sizeof(to), sysmodule, "pending", names[i])) {
            continue;
        }
        if (sysmodule->storage->vtable->rename_path(sysmodule->storage, from, to)) {
            ++recovered;
        }
    }
    return recovered;
}

int ptc_sysmodule_process_all(PtcSysmodule *sysmodule)
{
    char dir[320];
    char names[32][128];
    size_t count = 0;
    size_t i;
    int processed = 0;
    snprintf(dir, sizeof(dir), "%s/inbox/pending", sysmodule->app_root);
    if (!sysmodule->storage->vtable->list_json(sysmodule->storage, dir, names, 32, &count)) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        char pending[320];
        char processing[320];
        char done[320];
        char text[4096];
        char request_id[80];
        if (!request_file_stem(names[i], request_id, sizeof(request_id))) continue;
        if (!request_file_path(pending, sizeof(pending), sysmodule, "pending", names[i]) ||
            !request_file_path(processing, sizeof(processing), sysmodule, "processing", names[i]) ||
            !request_file_path(done, sizeof(done), sysmodule, "done", names[i])) {
            continue;
        }
        if (!sysmodule->storage->vtable->rename_path(sysmodule->storage, pending, processing)) {
            continue;
        }
        if (sysmodule->storage->vtable->read_text(sysmodule->storage, processing, text, sizeof(text))) {
            process_request_text(sysmodule, text, request_id);
        }
        (void)sysmodule->storage->vtable->rename_path(sysmodule->storage, processing, done);
        ++processed;
    }
    return processed;
}

uint32_t ptc_sysmodule_note_scan_result(PtcSysmodule *sysmodule, bool found_work)
{
    if (!sysmodule) return 5000;
    if (found_work) {
        sysmodule->scan_backoff_ms = 500;
    } else if (sysmodule->scan_backoff_ms < 1000) {
        sysmodule->scan_backoff_ms = 1000;
    } else if (sysmodule->scan_backoff_ms < 2000) {
        sysmodule->scan_backoff_ms = 2000;
    } else {
        sysmodule->scan_backoff_ms = 5000;
    }
    return sysmodule->scan_backoff_ms;
}

uint32_t ptc_sysmodule_current_scan_interval(const PtcSysmodule *sysmodule)
{
    return sysmodule && sysmodule->scan_backoff_ms ? sysmodule->scan_backoff_ms : 500;
}

uint32_t ptc_sysmodule_next_wait_ms(PtcSysmodule *sysmodule)
{
    PtcClockSnapshot now;
    int64_t second_of_day;
    uint32_t minute_ms;
    uint64_t date_ms;
    uint32_t wait_ms;
    if (!sysmodule) return 500;
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    second_of_day = (int64_t)now.minute_of_day * 60 + now.unix_seconds % 60;
    if (second_of_day < 0) second_of_day += 60;
    minute_ms = (uint32_t)(60 - (second_of_day % 60)) * 1000u;
    date_ms = (uint64_t)(PTC_SECONDS_PER_DAY - second_of_day) * 1000u;
    wait_ms = ptc_sysmodule_current_scan_interval(sysmodule);
    if (minute_ms < wait_ms) wait_ms = minute_ms;
    if (date_ms < wait_ms) wait_ms = date_ms;
#ifdef PLAYWISE_DEVICE_LAB
    wait_ms = ptc_lab_next_wait_ms(sysmodule, wait_ms);
#endif
    return wait_ms ? wait_ms : 1u;
}

static bool date_directory_day_index(const char *name, uint16_t *out)
{
    unsigned int year;
    unsigned int month;
    unsigned int day;
    char tail;
    if (!name || strlen(name) != 10 || sscanf(name, "%4u-%2u-%2u%c", &year, &month, &day, &tail) != 3) return false;
    return ptc_day_index_from_date((uint16_t)year, (uint8_t)month, (uint8_t)day, out);
}

/* entries is supplied by the caller: a PtcStorageEntry[256] costs ~38 KiB, so a
   nested copy here plus the caller's own array overflows the main thread stack. */
static bool cleanup_timestamped_json(
    PtcSysmodule *sysmodule,
    const char *relative_dir,
    uint16_t today,
    PtcStorageEntry *entries,
    size_t entry_capacity)
{
    char dir[320];
    size_t count = 0;
    size_t i;
    bool ok = true;
    snprintf(dir, sizeof(dir), "%s/%s", sysmodule->app_root, relative_dir);
    if (!sysmodule->storage->vtable->list_entries(sysmodule->storage, dir, entries, entry_capacity, &count)) return true;
    for (i = 0; i < count; ++i) {
        char stem[80];
        char path[512];
        uint16_t file_day;
        if (entries[i].type != PTC_STORAGE_ENTRY_FILE || !request_file_stem(entries[i].name, stem, sizeof(stem)) ||
            !entries[i].modified_time_valid || entries[i].modified_unix_seconds < PTC_DAY_INDEX_EPOCH_UNIX) continue;
        file_day = ptc_day_index_from_unix(entries[i].modified_unix_seconds);
        if (file_day > today || (uint16_t)(today - file_day) < 30u) continue;
        if (strlen(dir) + 1u + strlen(entries[i].name) >= sizeof(path)) continue;
        memcpy(path, dir, strlen(dir));
        path[strlen(dir)] = '/';
        memcpy(path + strlen(dir) + 1u, entries[i].name, strlen(entries[i].name) + 1u);
        if (!sysmodule->storage->vtable->remove_path(sysmodule->storage, path)) ok = false;
    }
    return ok;
}

int ptc_sysmodule_cleanup(PtcSysmodule *sysmodule)
{
    char logs_dir[320];
    /* Reused across the log scan and both cleanup_timestamped_json calls so the
       ~38 KiB entry array is only reserved once on the stack. */
    PtcStorageEntry entries[PTC_CLEANUP_MAX_ENTRIES];
    size_t count = 0;
    size_t i;
    bool ok = true;
    PtcClockSnapshot now;
    if (!sysmodule || !sysmodule->storage->vtable->list_entries || !sysmodule->storage->vtable->remove_tree) return 0;
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    snprintf(logs_dir, sizeof(logs_dir), "%s/logs", sysmodule->app_root);
    if (sysmodule->storage->vtable->list_entries(sysmodule->storage, logs_dir, entries, PTC_CLEANUP_MAX_ENTRIES, &count)) {
        for (i = 0; i < count; ++i) {
            uint16_t day_index;
            char path[512];
            if (entries[i].type != PTC_STORAGE_ENTRY_DIRECTORY || !date_directory_day_index(entries[i].name, &day_index) ||
                day_index > now.day_index || (uint16_t)(now.day_index - day_index) < 30u) continue;
            if (strlen(logs_dir) + 1u + strlen(entries[i].name) >= sizeof(path)) continue;
            memcpy(path, logs_dir, strlen(logs_dir));
            path[strlen(logs_dir)] = '/';
            memcpy(path + strlen(logs_dir) + 1u, entries[i].name, strlen(entries[i].name) + 1u);
            if (!sysmodule->storage->vtable->remove_tree(sysmodule->storage, path)) ok = false;
        }
    }
    if (!cleanup_timestamped_json(sysmodule, "results", now.day_index, entries, PTC_CLEANUP_MAX_ENTRIES)) ok = false;
    if (!cleanup_timestamped_json(sysmodule, "inbox/done", now.day_index, entries, PTC_CLEANUP_MAX_ENTRIES)) ok = false;
    if (!ok) append_event(sysmodule, NULL, "cleanup_failed", PTC_ERR_STORAGE_WRITE_FAILED, "retention");
    sysmodule->last_cleanup_day_index = now.day_index;
    sysmodule->cleanup_initialized = true;
    return ok ? 1 : 0;
}

int ptc_sysmodule_rollover_legacy_logs(PtcSysmodule *sysmodule)
{
    static const char *NAMES[] = { "events.jsonl", "pctl_debug.jsonl", "sysmodule.log" };
    char date[11];
    PtcClockSnapshot now;
    size_t i;
    int moved = 0;
    if (!sysmodule) return 0;
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    if (now.unix_seconds < PTC_DAY_INDEX_EPOCH_UNIX || !ptc_format_date(now.day_index, date)) return 0;
    for (i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); ++i) {
        char from[320];
        char to[352];
        snprintf(from, sizeof(from), "%s/logs/%s", sysmodule->app_root, NAMES[i]);
        if (!sysmodule->storage->vtable->exists(sysmodule->storage, from)) continue;
        snprintf(to, sizeof(to), "%s/logs/%s/legacy-%lld-%s", sysmodule->app_root, date,
            (long long)now.unix_seconds, NAMES[i]);
        if (sysmodule->storage->vtable->rename_path(sysmodule->storage, from, to)) ++moved;
    }
    return moved;
}

int ptc_sysmodule_scheduler_tick(PtcSysmodule *sysmodule, bool storage_notified)
{
    char disable_path[320];
    char reload_path[320];
    PtcClockSnapshot now;
    bool disable_changed;
    bool disable_present;
    bool minute_changed;
    bool reload;
    int processed;
    int actions = 0;
    if (!sysmodule) return 0;
#ifdef PLAYWISE_DEVICE_LAB
    actions += ptc_lab_scheduler_tick(sysmodule);
#endif
    {
        int setup_actions = ptc_sysmodule_bootstrap_setup(sysmodule);
        if (setup_actions > 0) actions += setup_actions;
    }
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    snprintf(disable_path, sizeof(disable_path), "%s/flags/disable.flag", sysmodule->app_root);
    snprintf(reload_path, sizeof(reload_path), "%s/flags/reload.flag", sysmodule->app_root);
    disable_present = sysmodule->storage->vtable->exists(sysmodule->storage, disable_path);
    disable_changed = !sysmodule->disable_initialized || sysmodule->disable_present != disable_present;
    reload = sysmodule->storage->vtable->exists(sysmodule->storage, reload_path);
    if (reload) {
        invalidate_all_caches(sysmodule);
        (void)ptc_sysmodule_refresh_caches(sysmodule);
    }
#ifdef PLAYWISE_EDEN
    /* Eden's Windows-backed SD implementation can copy pending files to the
       processing directory while failing to remove the source. Requests use
       the synchronous in-process bridge instead, so never rescan those stale
       queue artifacts in the emulator profile. */
    processed = 0;
#else
    processed = ptc_sysmodule_process_all(sysmodule);
#endif
    (void)ptc_sysmodule_note_scan_result(sysmodule, processed > 0 || storage_notified || reload || disable_changed);
    actions += processed;
    minute_changed = !sysmodule->minute_initialized || sysmodule->last_minute_day_index != now.day_index ||
        sysmodule->last_minute_of_day != now.minute_of_day;
    if (minute_changed || reload || storage_notified || disable_changed) {
#ifndef PLAYWISE_DEVICE_LAB
        if (sysmodule->minute_initialized) {
            uint32_t previous = (uint32_t)sysmodule->last_minute_day_index * 1440u +
                sysmodule->last_minute_of_day;
            uint32_t current = (uint32_t)now.day_index * 1440u + now.minute_of_day;
            if (current != previous && current != previous + 1u) {
                append_event(sysmodule, NULL, "bedtime_clock_changed", PTC_ERR_OK,
                    current < previous ? "clock_moved_backward" : "clock_jump_forward");
            }
        }
        actions += ptc_sysmodule_enforce_tick(sysmodule);
        actions += usage_summary_tick(sysmodule, now);
#endif
        sysmodule->last_minute_day_index = now.day_index;
        sysmodule->last_minute_of_day = now.minute_of_day;
        sysmodule->minute_initialized = true;
    }
    sysmodule->disable_present = disable_present;
    sysmodule->disable_initialized = true;
    if (reload) (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, reload_path);
    if (!sysmodule->cleanup_initialized || sysmodule->last_cleanup_day_index != now.day_index) {
        actions += ptc_sysmodule_cleanup(sysmodule);
    }
    return actions;
}
