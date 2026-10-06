#include "sysmodule_internal.h"
#include "../platform/config_backup.h"
#include "../common/security/credential_policy.h"
#ifdef __SWITCH__
#include <switch.h>
#endif

static bool copy_file(PtcStorage *s, const char *from, const char *to, const char *relative, bool exists)
{
    char source[512], target[512], text[PTC_CONFIG_BACKUP_FILE_SIZE];
    snprintf(source, sizeof(source), "%s/%s", from, relative);
    snprintf(target, sizeof(target), "%s/%s", to, relative);
    if (!exists) return !s->vtable->exists(s, target) || s->vtable->remove_path(s, target);
    return s->vtable->read_text(s, source, text, sizeof(text)) && s->vtable->write_text_atomic(s, target, text);
}

bool config_backup_rollback_files(PtcSysmodule *sysmodule)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/recovery/active/config-added.json", sysmodule->app_root);
    if (!sysmodule->storage->vtable->exists(sysmodule->storage, path)) return true;
    cJSON *list = ptc_config_read_json(sysmodule->storage, sysmodule->app_root,
        "recovery/active/config-added.json", PTC_CONFIG_BACKUP_MANIFEST_SIZE);
    bool ok = cJSON_IsArray(list);
    for (const cJSON *v = ok ? list->child : NULL; v; v = v->next) {
        if (!cJSON_IsString(v) || strncmp(v->valuestring, "calendars/library/", 18) ||
            !ptc_calendar_file_name_valid(v->valuestring + 18)) { ok = false; break; }
        snprintf(path, sizeof(path), "%s/%s", sysmodule->app_root, v->valuestring);
        if (sysmodule->storage->vtable->exists(sysmodule->storage, path))
            ok = sysmodule->storage->vtable->remove_path(sysmodule->storage, path) && ok;
    }
    cJSON_Delete(list); return ok;
}

static bool calendar_files(PtcSysmodule *sysmodule, const char *stage, const cJSON *manifest)
{
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(manifest, "files");
    cJSON *added = cJSON_CreateArray();
    char path[512], text[PTC_CONFIG_BACKUP_FILE_SIZE], hash[65];
    bool ok = added != NULL;
    /* Journal new content-addressed files before publishing any of them. Existing
       files are never overwritten, so rollback can safely remove only additions. */
    for (const cJSON *f = ok ? files->child : NULL; ok && f; f = f->next) {
        const char *relative = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "path"));
        if (strncmp(relative, "calendars/library/", 18)) continue;
        snprintf(path, sizeof(path), "%s/%s", sysmodule->app_root, relative);
        if (sysmodule->storage->vtable->exists(sysmodule->storage, path)) {
            ok = sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text));
            if (ok) {
                ptc_calendar_sha256_hex(text, strlen(text), hash);
                ok = !strcmp(hash, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "sha256")));
            }
        } else ok = cJSON_AddItemToArray(added, cJSON_CreateString(relative));
    }
    if (ok) ok = ptc_config_write_json(sysmodule->storage, sysmodule->app_root, "recovery/active/config-added.json", added);
    for (const cJSON *v = ok ? added->child : NULL; ok && v; v = v->next)
        ok = copy_file(sysmodule->storage, stage, sysmodule->app_root, v->valuestring, true);
    for (const cJSON *f = ok ? files->child : NULL; ok && f; f = f->next) {
        const char *relative = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "path"));
        if (!strcmp(relative, "calendars/catalog.json") || !strcmp(relative, "calendars/active.json"))
            ok = copy_file(sysmodule->storage, stage, sysmodule->app_root, relative,
                cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(f, "exists")));
    }
    cJSON_Delete(added); return ok;
}

static PtcErrorCode restore_config(PtcSysmodule *sysmodule, const PtcRequest *request, bool disabled, PtcClockSnapshot now)
{
    char stage[256], working[256];
    PtcRules previous, restored;
    PtcRuntimeState state;
    PtcPctlStatus status;
    PtcErrorCode error = PTC_ERR_CONFIG_BACKUP_INVALID;
    unsigned groups = request->config_groups;
    bool control = (groups & 255u) != 0;
    PtcSetupState setup;
    if (control && disabled) return PTC_ERR_DISABLED;
    if (control && (!load_setup_state(sysmodule, &setup) || strcmp(setup.phase, "active"))) return PTC_ERR_SETUP_PENDING;
    if (!ptc_config_stage_path(stage, sizeof(stage), sysmodule->app_root, request->config_stage_id) ||
        !ptc_config_stage_validate(sysmodule->storage, stage, NULL)) return error;
    if (!ptc_config_stage_validate(sysmodule->storage, stage, request->config_sha256)) return PTC_ERR_CONFIG_BACKUP_CHANGED;
    if (!load_rules(sysmodule, &previous) || !load_state(sysmodule, &state)) return PTC_ERR_RULES_INVALID;
    if (state.apply_pending_confirmation) return PTC_ERR_CONTROL_BUSY;
    if (control && sysmodule->pctl->vtable->read_status(sysmodule->pctl,
        ptc_weekday_from_day_index(now.day_index), &status) != PTC_ERR_OK) return PTC_ERR_PCTL_READ_FAILED;
    /* Resolve reliable usage before changing the rules; never refund non-TV use. */
    if (control) {
        dock_sample_usage(sysmodule, &previous, &state, now);
        if (!save_state(sysmodule, &state, now.unix_seconds)) return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    if (!recovery_begin(sysmodule, request, now)) return PTC_ERR_PCTL_BACKUP_FAILED;
    snprintf(working, sizeof(working), "%s/recovery/active/config-input", sysmodule->app_root);
    cJSON *manifest = ptc_config_read_json(sysmodule->storage, stage, "manifest.json", PTC_CONFIG_BACKUP_MANIFEST_SIZE);
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(manifest, "files");
    bool ok = manifest != NULL;
    for (const cJSON *f = ok ? files->child : NULL; ok && f; f = f->next)
        ok = copy_file(sysmodule->storage, stage, working,
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "path")),
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(f, "exists")));
    /* Recheck the private copy, closing the preview/staging race. */
    if (ok) ok = ptc_config_write_json(sysmodule->storage, working, "manifest.json", manifest) &&
        ptc_config_stage_validate(sysmodule->storage, working, request->config_sha256);
    cJSON *current_rules = ok ? ptc_config_read_json(sysmodule->storage, sysmodule->app_root, "rules.json", 6144) : NULL;
    cJSON *source_rules = ok ? ptc_config_read_json(sysmodule->storage, working, "rules.json", 6144) : NULL;
    cJSON *merged_rules = ok ? ptc_config_merge_rules(current_rules, source_rules, groups, now.day_index) : NULL;
    cJSON *current_config = ok ? ptc_config_read_json(sysmodule->storage, sysmodule->app_root, "config.json", 4096) : NULL;
    cJSON *source_config = ok ? ptc_config_read_json(sysmodule->storage, working, "config.json", 4096) : NULL;
    cJSON *merged_config = ok ? ptc_config_merge_preferences(current_config, source_config, groups) : NULL;
    ok = ok && merged_rules && merged_config;
    if (ok && control) {
        ok = ptc_config_rules_decode(merged_rules, &restored);
        restored.calendar = previous.calendar;
        if (ok && restored.dock_policy.force_docked) {
            PtcOperationModeStatus mode = ptc_read_operation_mode(sysmodule);
            if (!mode.dock_supported_available || !mode.dock_supported) { ok = false; error = PTC_ERR_DOCK_UNSUPPORTED; }
        }
#ifndef PLAYWISE_EDEN
        if (ok && (restored.bedtime.enabled || restored.eye_care.enabled || dock_policy_enabled(&restored))) {
            char fingerprint[65];
            if (!previous.bedtime.confirmation_version || previous.bedtime.official_setting_confirmed_at <= 0 ||
                !sysmodule_environment_fingerprint(sysmodule, fingerprint) || strcmp(previous.bedtime.confirmed_environment, fingerprint) ||
                (!bedtime_overlay_verified(sysmodule) && !previous.bedtime.unverified_overlay_risk_accepted)) {
                ok = false; error = PTC_ERR_BEDTIME_CONFIRMATION_REQUIRED;
            }
        }
#endif
    }
    if (!ok) {
        cJSON_Delete(manifest); cJSON_Delete(current_rules); cJSON_Delete(source_rules); cJSON_Delete(merged_rules);
        cJSON_Delete(current_config); cJSON_Delete(source_config); cJSON_Delete(merged_config);
        return error;
    }
    error = PTC_ERR_STORAGE_WRITE_FAILED;
    if (ok && (groups & PTC_CONFIG_HOLIDAY)) ok = calendar_files(sysmodule, working, manifest);
    if (ok && control) ok = ptc_config_write_json(sysmodule->storage, sysmodule->app_root, "rules.json", merged_rules);
    if (ok && (groups & (PTC_CONFIG_PREFS | PTC_CONFIG_PAIRING)))
        ok = ptc_config_write_json(sysmodule->storage, sysmodule->app_root, "config.json", merged_config);
    if (ok && (groups & PTC_CONFIG_PIN)) {
        cJSON *pin = ptc_config_read_json(sysmodule->storage, working, "auth.json", 1024);
        const char *hash = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(pin, "pin_hash"));
        ok = hash && hash[0] && ptc_config_pin_valid(pin);
        if (ok) {
            cJSON_DeleteItemFromObjectCaseSensitive(pin, "failed_attempts"); cJSON_AddNumberToObject(pin, "failed_attempts", 0);
            cJSON_DeleteItemFromObjectCaseSensitive(pin, "cooldown_until"); cJSON_AddNumberToObject(pin, "cooldown_until", 0);
            cJSON_DeleteItemFromObjectCaseSensitive(pin, "updated_at"); cJSON_AddNumberToObject(pin, "updated_at", (double)now.unix_seconds);
            ok = ptc_config_write_json(sysmodule->storage, sysmodule->app_root, "auth.json", pin);
        }
        cJSON_Delete(pin);
    }
    if (ok && (groups & PTC_CONFIG_PAIRING)) {
        uint8_t bytes[32]; char secret[65];
#ifdef __SWITCH__
        randomGet(bytes, sizeof(bytes));
#else
        ok = sysmodule->config_random && sysmodule->config_random(sysmodule->config_random_ctx, bytes, sizeof(bytes));
#endif
        cJSON *credentials = cJSON_CreateObject();
        if (ok) ok = ptc_hex_from_random(bytes, sizeof(bytes), secret, sizeof(secret));
        if (ok) {
            cJSON_AddNumberToObject(credentials, "version", 1);
            cJSON_AddStringToObject(credentials, "grant_secret", secret);
            ok = ptc_config_write_json(sysmodule->storage, sysmodule->app_root, "credentials.json", credentials);
        }
        memset(bytes, 0, sizeof(bytes)); memset(secret, 0, sizeof(secret)); cJSON_Delete(credentials);
    }
    cJSON_Delete(manifest); cJSON_Delete(current_rules); cJSON_Delete(source_rules); cJSON_Delete(merged_rules);
    cJSON_Delete(current_config); cJSON_Delete(source_config); cJSON_Delete(merged_config);
    if (!ok) return error;
    invalidate_all_caches(sysmodule);
    if (control) {
        state.last_enforced_mode = 0; /* Force validation of the newly composed target. */
        if (!save_state(sysmodule, &state, now.unix_seconds)) return PTC_ERR_STORAGE_WRITE_FAILED;
        if (!ptc_sysmodule_enforce_request(sysmodule, request) || !recovery_owned_by(sysmodule, request) ||
            !load_state(sysmodule, &state) || state.apply_pending_confirmation) return PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
    }
    return PTC_ERR_OK;
}

bool process_config_backup_request(PtcSysmodule *sysmodule, const PtcRequest *request, bool disabled, PtcClockSnapshot now)
{
    if (request->type != PTC_REQUEST_CREATE_CONFIG_BACKUP && request->type != PTC_REQUEST_RESTORE_CONFIG_BACKUP) return false;
    PtcErrorCode error = PTC_ERR_OK;
    if (request->type == PTC_REQUEST_CREATE_CONFIG_BACKUP) {
        char stage[256];
        if (!ptc_config_stage_path(stage, sizeof(stage), sysmodule->app_root, request->request_id) ||
            !ptc_config_stage_create(sysmodule->storage, sysmodule->app_root, stage, now.unix_seconds)) error = PTC_ERR_CONFIG_BACKUP_INVALID;
    } else error = restore_config(sysmodule, request, disabled, now);
    if (request->type == PTC_REQUEST_RESTORE_CONFIG_BACKUP || error != PTC_ERR_OK) {
        char stage[256];
        const char *owner = request->type == PTC_REQUEST_RESTORE_CONFIG_BACKUP ? request->config_stage_id : request->request_id;
        if (ptc_config_stage_path(stage, sizeof(stage), sysmodule->app_root, owner))
            (void)sysmodule->storage->vtable->remove_tree(sysmodule->storage, stage);
    }
    if (error != PTC_ERR_OK) (void)finish_with_error(sysmodule, request, "release", false, error, now.day_index);
    else {
        bool ok;
        if (request->type == PTC_REQUEST_CREATE_CONFIG_BACKUP) {
            char json[4096]; PtcResultState result;
            ptc_result_state_default(&result, now.day_index);
            ok = ptc_result_ok_json(json, sizeof(json), request->request_id, request->type_text,
                "release", false, &result, now.unix_seconds) == 0 && write_result(sysmodule, request->request_id, json);
            if (!ok) {
                char stage[256];
                if (ptc_config_stage_path(stage, sizeof(stage), sysmodule->app_root, request->request_id))
                    (void)sysmodule->storage->vtable->remove_tree(sysmodule->storage, stage);
            }
        } else ok = write_current_status_result(sysmodule, request, "release", false, now, true);
        if (ok && request->type == PTC_REQUEST_RESTORE_CONFIG_BACKUP) recovery_clear(sysmodule);
        if (!ok && recovery_owned_by(sysmodule, request) && !recovery_rollback(sysmodule))
            write_disable_flag(sysmodule, "config_restore_failed\n");
    }
    return true;
}
