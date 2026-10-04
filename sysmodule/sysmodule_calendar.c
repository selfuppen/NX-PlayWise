#include "sysmodule_internal.h"

static bool process_calendar_import(PtcSysmodule *sysmodule, const PtcRequest *request,
                                    PtcClockSnapshot now)
{
    PtcCalendarIndex *catalog = NULL;
    char catalog_path[320];
    char *before = NULL;
    bool existed, ok = false;
    PtcErrorCode error = PTC_ERR_CALENDAR_INVALID;
    catalog = (PtcCalendarIndex *)malloc(sizeof(*catalog));
    before = (char *)malloc(PTC_CALENDAR_INDEX_TEXT_SIZE);
    if (!catalog || !before) { error = PTC_ERR_STORAGE_READ_FAILED; goto done; }
    join_path(catalog_path, sizeof(catalog_path), sysmodule->app_root, "calendars/catalog.json");
    existed = sysmodule->storage->vtable->exists(sysmodule->storage, catalog_path);
    if (existed && !sysmodule->storage->vtable->read_text(sysmodule->storage,
            catalog_path, before, PTC_CALENDAR_INDEX_TEXT_SIZE)) {
        error = PTC_ERR_STORAGE_READ_FAILED; goto done;
    }
    if (!ptc_calendar_index_load(sysmodule->storage, sysmodule->app_root, false,
            catalog, NULL) ||
        !ptc_calendar_import_file(sysmodule->storage, sysmodule->app_root,
            request->calendar_file_name, request->calendar_sha256, catalog)) goto done;
    if (!ptc_calendar_index_save(sysmodule->storage, sysmodule->app_root, false, catalog)) {
        if (!(existed
            ? sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, catalog_path, before)
            : (!sysmodule->storage->vtable->exists(sysmodule->storage, catalog_path) ||
               sysmodule->storage->vtable->remove_path(sysmodule->storage, catalog_path))))
            write_disable_flag(sysmodule, "calendar_catalog_restore_failed\n");
        error = PTC_ERR_STORAGE_WRITE_FAILED; goto done;
    }
    append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "calendar_import");
    if (write_current_status_result(sysmodule, request, "release", false, now, false)) {
        ok = true;
        goto done;
    }
    if (!(existed
        ? sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, catalog_path, before)
        : (!sysmodule->storage->vtable->exists(sysmodule->storage, catalog_path) ||
           sysmodule->storage->vtable->remove_path(sysmodule->storage, catalog_path)))) {
        write_disable_flag(sysmodule, "calendar_catalog_restore_failed\n");
    }
    error = PTC_ERR_STORAGE_WRITE_FAILED;
done:
    free(before);
    free(catalog);
    if (!ok) return finish_with_error(sysmodule, request, "release", false, error, now.day_index);
    return true;
}

static bool process_calendar_activation(PtcSysmodule *sysmodule, const PtcRequest *request,
                                        bool disable_flag, PtcClockSnapshot now)
{
    PtcCalendarIndex *catalog = NULL, *selected = NULL;
    PtcRules before_rules, after_rules;
    PtcRuntimeState state;
    PtcBedtimeEvaluation bedtime;
    PtcPctlSettingsSnapshot bedtime_snapshot;
    PtcPctlStatus observed;
    PtcDayRule day_rule;
    PtcPctlTargetMode target;
    PtcErrorCode error = PTC_ERR_CALENDAR_INVALID;
    uint16_t minutes;
    char digest[65];
    bool bedtime_active, ok = false;
    if (disable_flag) return finish_with_error(sysmodule, request, "release", true,
        PTC_ERR_DISABLED, now.day_index);
    catalog = (PtcCalendarIndex *)malloc(sizeof(*catalog));
    selected = (PtcCalendarIndex *)malloc(sizeof(*selected));
    if (!catalog || !selected) { error = PTC_ERR_STORAGE_READ_FAILED; goto done; }
    if (!ptc_calendar_index_load(sysmodule->storage, sysmodule->app_root, false,
            catalog, digest)) goto done;
    if (strcmp(digest, request->calendar_catalog_sha256) != 0) {
        error = PTC_ERR_CALENDAR_CATALOG_CHANGED; goto done;
    }
    if (!ptc_calendar_build_selection(catalog, request->calendar_option_id, selected) ||
        !ptc_calendar_selection_validate(sysmodule->storage, sysmodule->app_root, selected)) goto done;
    if (!load_rules(sysmodule, &before_rules) || !load_state(sysmodule, &state)) {
        error = PTC_ERR_RULES_INVALID; goto done;
    }
    if (!recovery_begin(sysmodule, request, now)) {
        error = PTC_ERR_PCTL_BACKUP_FAILED; goto done;
    }
    if (!ptc_calendar_index_save(sysmodule->storage, sysmodule->app_root, true, selected) ||
        !load_rules(sysmodule, &after_rules)) {
        error = PTC_ERR_STORAGE_WRITE_FAILED; goto done;
    }
    bedtime = ptc_bedtime_evaluate(&after_rules, now.day_index,
        ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
    bedtime_active = bedtime.active && after_rules.bedtime.enabled &&
        state.bedtime_skipped_instance_id != bedtime.window_instance_id;
    day_rule = ptc_rules_today_rule(&after_rules, now.day_index,
        ptc_weekday_from_day_index(now.day_index));
    target = target_from_day_rule(day_rule);
    minutes = day_rule.minutes;
    if (after_rules.eye_care.enabled && day_rule.mode == PTC_RULE_MODE_UNLIMITED) {
        target = PTC_PCTL_TARGET_LIMIT;
        minutes = 1440u;
    }
    if (bedtime_active || state.eye_care_resting) {
        target = PTC_PCTL_TARGET_BLOCKED;
        minutes = 0u;
    }
    if (bedtime_active && !state.bedtime_enforced) {
        if (!sysmodule->pctl->vtable->snapshot_settings ||
            sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &bedtime_snapshot) != PTC_ERR_OK ||
            !save_bedtime_snapshot(sysmodule, &bedtime_snapshot, &bedtime, now.unix_seconds)) {
            error = PTC_ERR_PCTL_BACKUP_FAILED; goto done;
        }
    }
    error = apply_target(sysmodule, request, now, "release", target, minutes);
    if (error != PTC_ERR_OK) goto done;
    error = observe_target_with_optional_activation(sysmodule, request, now,
        "release", target, minutes, "calendar_activation", &observed);
    if (error != PTC_ERR_OK) goto done;
    state.bedtime_enforced = bedtime_active;
    state.bedtime_window_instance_id = bedtime_active ? bedtime.window_instance_id : 0u;
    state.bedtime_start_day_index = bedtime_active ? bedtime.start_day_index : 0u;
    state.last_enforced_day_index = now.day_index;
    state.last_enforced_mode = target;
    state.last_enforced_minutes = minutes;
    if (!save_state(sysmodule, &state, now.unix_seconds) ||
        !record_activity(sysmodule, request, now, 0u, 0u)) {
        error = PTC_ERR_STORAGE_WRITE_FAILED; goto done;
    }
    if (!write_current_status_result(sysmodule, request, "release", false, now, true)) {
        error = PTC_ERR_STORAGE_WRITE_FAILED;
        goto done;
    }
    if (!bedtime_active) clear_bedtime_snapshot(sysmodule);
    recovery_clear(sysmodule);
    ok = true;
done:
    if (!ok && recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule)) {
        write_disable_flag(sysmodule, "calendar_activation_restore_failed\n");
        error = PTC_ERR_RECOVERY_FAILED;
    }
    free(selected);
    free(catalog);
    if (!ok) return finish_with_error(sysmodule, request, "release", false, error, now.day_index);
    return true;
}

bool process_calendar_request(PtcSysmodule *sysmodule, const PtcRequest *request,
                              bool disable_flag, PtcClockSnapshot now)
{
    if (request->type == PTC_REQUEST_IMPORT_HOLIDAY_CALENDAR)
        return process_calendar_import(sysmodule, request, now);
    if (request->type == PTC_REQUEST_ACTIVATE_HOLIDAY_CALENDAR)
        return process_calendar_activation(sysmodule, request, disable_flag, now);
    return false;
}
