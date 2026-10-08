#define _POSIX_C_SOURCE 200809L
#include "nro_app_internal.h"

#define CONFIG_BACKUP_PATH APP_ROOT "/backups/config-backup.json"

static unsigned available_groups(const PtcUiModel *model)
{
    unsigned groups = PTC_CONFIG_BACKUP_ALL;
    if (!model->config_today_available) groups &= ~PTC_CONFIG_TODAY;
    if (!model->config_pin_available) groups &= ~PTC_CONFIG_PIN;
    return groups;
}

void config_backup_preview(UiState *ui)
{
    char stage[256];
    PtcRules rules;
    ui->model.config_needs_confirmation = false;
    if (!ui->model.config_backup_ready || !ptc_config_stage_path(stage, sizeof(stage), APP_ROOT, ui->config_stage_id)) return;
    cJSON *current = ptc_config_read_json(ui->client.storage, APP_ROOT, "rules.json", 6144);
    cJSON *source = ptc_config_read_json(ui->client.storage, stage, "rules.json", 6144);
    cJSON *merged = ptc_config_merge_rules(current, source, ui->model.config_groups, ui->model.day_index);
    bool ok = merged && ptc_config_rules_decode(merged, &rules);
    PtcCalendarRuntime *calendar = calloc(1, sizeof(*calendar));
    if (ok) ok = calendar && ptc_calendar_runtime_load(ui->client.storage,
        (ui->model.config_groups & PTC_CONFIG_HOLIDAY) ? stage : APP_ROOT, ui->model.day_index, calendar);
    if (ok) {
        rules.calendar = calendar->builtin ? NULL : &calendar->set;
        PtcDayRule day = ptc_rules_today_rule(&rules, ui->model.day_index, ptc_weekday_from_day_index(ui->model.day_index));
        time_t now = time(NULL); struct tm local = {0}; localtime_r(&now, &local);
        PtcBedtimeEvaluation bedtime = ptc_bedtime_evaluate(&rules, ui->model.day_index,
            ptc_weekday_from_day_index(ui->model.day_index), (uint16_t)(local.tm_hour * 60 + local.tm_min));
        bool blocked = (bedtime.active && ui->model.bedtime_skipped_window_instance_id != bedtime.window_instance_id) ||
            (rules.eye_care.enabled && !strcmp(ui->model.eye_care_phase, "resting")) ||
            (!ui->model.dock_waived_today && (rules.dock_policy.force_docked || rules.dock_policy.undocked_limit_enabled) &&
             (strcmp(ui->model.operation_mode, "docked") && (rules.dock_policy.force_docked ||
              !ui->model.undocked_usage_available || ui->model.undocked_used_minutes >= rules.dock_policy.undocked_daily_minutes)));
        bool usage_known = ui->model.played_minutes_available && ui->model.played_minutes >= 0 &&
            ptc_ui_status_is_fresh(&ui->model, (int64_t)now);
        if (usage_known && day.mode == PTC_RULE_MODE_LIMIT && ui->model.played_minutes >= day.minutes) blocked = true;
        const char *effect = blocked ? ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_RESTRICT) : ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_APPLY);
        if (day.mode == PTC_RULE_MODE_UNLIMITED)
            snprintf(ui->model.config_preview, sizeof(ui->model.config_preview), "%s\n%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED), effect);
        else snprintf(ui->model.config_preview, sizeof(ui->model.config_preview), ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_PREVIEW),
            (unsigned)day.minutes, effect);
        if (usage_known && day.mode == PTC_RULE_MODE_LIMIT) {
            size_t n = strlen(ui->model.config_preview);
            unsigned remaining = blocked || ui->model.played_minutes >= day.minutes ? 0u : (unsigned)(day.minutes - ui->model.played_minutes);
            snprintf(ui->model.config_preview + n, sizeof(ui->model.config_preview) - n, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_USAGE),
                (unsigned)ui->model.played_minutes, remaining);
        }
        ui->model.config_needs_confirmation = (rules.bedtime.enabled || rules.eye_care.enabled ||
            rules.dock_policy.force_docked || rules.dock_policy.undocked_limit_enabled) && !ui->model.bedtime_official_setting_confirmed;
    } else snprintf(ui->model.config_preview, sizeof(ui->model.config_preview), "%s", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_INVALID));
    if (ui->model.config_groups & PTC_CONFIG_PAIRING) {
        size_t n = strlen(ui->model.config_preview);
        snprintf(ui->model.config_preview + n, sizeof(ui->model.config_preview) - n, "\n%s", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NEW_KEY));
    }
    cJSON_Delete(current); cJSON_Delete(source); cJSON_Delete(merged); free(calendar);
}

void open_config_backup(UiState *ui)
{
    if (!ui || ui->waiting || ui->model.recovery_active || ui->model.hot_reload_status == PTC_UI_HOT_RELOAD_RUNNING) return;
    char stage[256];
    if (ptc_config_stage_path(stage, sizeof(stage), APP_ROOT, ui->config_stage_id))
        (void)ui->client.storage->vtable->remove_tree(ui->client.storage, stage);
    ui->model.overlay = PTC_UI_OVERLAY_CONFIG_BACKUP;
    ui->model.overlay_selection = 13;
    ui->model.config_backup_ready = false;
    ui->model.config_today_available = false;
    ui->model.config_pin_available = false;
    ui->model.config_groups = PTC_CONFIG_BACKUP_DEFAULT;
    ui->model.config_metadata[0] = '\0'; ui->model.config_preview[0] = '\0';
    make_next_request_id(ui->config_stage_id, sizeof(ui->config_stage_id));
    if (!ptc_config_stage_path(stage, sizeof(stage), APP_ROOT, ui->config_stage_id)) return;
    if (!ui->client.storage->vtable->exists(ui->client.storage, CONFIG_BACKUP_PATH)) {
        snprintf(ui->model.config_preview, sizeof(ui->model.config_preview), "%s", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_MISSING)); return;
    }
    if (!ptc_config_archive_load(ui->client.storage, CONFIG_BACKUP_PATH, stage, ui->config_digest)) {
        (void)ui->client.storage->vtable->remove_tree(ui->client.storage, stage);
        snprintf(ui->model.config_preview, sizeof(ui->model.config_preview), "%s", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_INVALID)); return;
    }
    cJSON *manifest = ptc_config_read_json(ui->client.storage, stage, "manifest.json", PTC_CONFIG_BACKUP_MANIFEST_SIZE);
    cJSON *rules = ptc_config_read_json(ui->client.storage, stage, "rules.json", 6144);
    cJSON *pin = ptc_config_read_json(ui->client.storage, stage, "auth.json", 1024);
    const cJSON *date = cJSON_GetObjectItemCaseSensitive(rules, "today_override_day_index");
    const char *hash = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(pin, "pin_hash"));
    ui->model.config_today_available = cJSON_IsNumber(date) && date->valueint == ui->model.day_index;
    ui->model.config_pin_available = (hash && hash[0]) || (pin != NULL);
    const cJSON *created = cJSON_GetObjectItemCaseSensitive(manifest, "created_at");
    time_t timestamp = cJSON_IsNumber(created) ? (time_t)created->valuedouble : 0;
    struct tm local;
    char date_text[32] = "";
    if (localtime_r(&timestamp, &local)) strftime(date_text, sizeof(date_text), "%Y-%m-%d %H:%M", &local);
    const char *device = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(manifest, "source_device"));
    snprintf(ui->model.config_metadata, sizeof(ui->model.config_metadata), "%s / %s", device ? device : "", date_text);
    ui->model.config_backup_ready = true;
    ui->model.config_groups = PTC_CONFIG_BACKUP_DEFAULT & available_groups(&ui->model);
    cJSON_Delete(manifest); cJSON_Delete(rules); cJSON_Delete(pin);
    config_backup_preview(ui);
}

void request_config_restore(UiState *ui)
{
    if (!ui || ui->waiting || !ui->model.config_backup_ready || !ui->model.config_groups ||
        ui->model.recovery_active || ui->model.hot_reload_status == PTC_UI_HOT_RELOAD_RUNNING) return;
    if (!ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL))) { submit_status(ui); return; }
    config_backup_preview(ui);
#ifndef PLAYWISE_EDEN
    if (ui->model.config_needs_confirmation) {
        open_confirm_overlay(ui, PTC_UI_OPERATION_CONFIG_REQUIREMENTS, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_IMPORT),
            ptc_ui_text(PTC_UI_T_DOCK_REQUIREMENTS)); return;
    }
#endif
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_IMPORT))) return;
    if (ui->model.config_groups & PTC_CONFIG_PIN) {
        char stage[256], path[320], text[1024], pin[PTC_AUTH_PIN_MAX_LEN + 1];
        if (!ptc_config_stage_path(stage, sizeof(stage), APP_ROOT, ui->config_stage_id)) return;
        snprintf(path, sizeof(path), "%s/auth.json", stage);
        bool matches = ui->client.storage->vtable->read_text(ui->client.storage, path, text, sizeof(text)) &&
            pin_input(ui, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_PIN), ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_PIN_NOTE), pin, sizeof(pin)) &&
            ptc_companion_auth_backup_pin_matches(text, pin);
        memset(pin, 0, sizeof(pin)); memset(text, 0, sizeof(text));
        ui->model.overlay = PTC_UI_OVERLAY_CONFIG_BACKUP;
        if (!matches) {
            ptc_audio_play(PTC_SE_ERROR);
            show_auth_error(ui, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_PIN), ptc_ui_text(PTC_UI_T_PIN_IS_INCORRECT_PLEASE_TRY_AGAIN), 0);
            return;
        }
    }
    open_danger_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_CONFIG_BACKUP,
        ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_IMPORT), ui->model.config_preview);
}

void config_backup_action(UiState *ui, int index)
{
    if (!ui || ui->waiting) return;
    if (index == 15) {
        char stage[256];
        if (ptc_config_stage_path(stage, sizeof(stage), APP_ROOT, ui->config_stage_id))
            (void)ui->client.storage->vtable->remove_tree(ui->client.storage, stage);
        ui->config_stage_id[0] = '\0';
        ptc_ui_cancel_overlay(&ui->model); return;
    }
    if (index < 11 && index >= 0 && ui->model.config_backup_ready && (available_groups(&ui->model) & (1u << index)))
        ui->model.config_groups ^= (1u << index);
    else if (index == 11 && ui->model.config_backup_ready) ui->model.config_groups = available_groups(&ui->model);
    else if (index == 12) ui->model.config_groups = 0;
    else if (index == 14) { request_config_restore(ui); return; }
    else if (index == 13) {
        if (ui->model.recovery_active || ui->model.hot_reload_status == PTC_UI_HOT_RELOAD_RUNNING) return;
        if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE))) return;
        open_confirm_overlay(ui, PTC_UI_OPERATION_CREATE_CONFIG_BACKUP, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE),
            ui->client.storage->vtable->exists(ui->client.storage, CONFIG_BACKUP_PATH)
                ? ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_OVERWRITE) : ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NOTE)); return;
    }
    config_backup_preview(ui);
}

void submit_config_backup(UiState *ui, bool restore)
{
    if (!ui || ui->waiting) return;
    char json[512];
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    const char *type = restore ? "restore_config_backup" : "create_config_backup";
    if (restore) snprintf(json, sizeof(json),
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"created_at\":%lld,\"payload\":{\"groups\":%u,\"stage_id\":\"%s\",\"sha256\":\"%s\"}}",
        ui->active_request_id, type, (long long)time(NULL), ui->model.config_groups, ui->config_stage_id, ui->config_digest);
    else snprintf(json, sizeof(json), "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"created_at\":%lld,\"payload\":{}}",
        ui->active_request_id, type, (long long)time(NULL));
    ui->config_submitted_groups = restore ? ui->model.config_groups : 0;
    PtcCompanionStatus status = ptc_companion_transport_submit_json(&ui->transport, ui->active_request_id, json);
    ui->model.overlay = PTC_UI_OVERLAY_CONFIG_BACKUP;
    set_command_name(ui, type); sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, type, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_TITLE));
    else set_message(ui, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_TITLE), status);
}

void config_backup_result(UiState *ui)
{
    bool create = !strcmp(ui->model.result_type, "create_config_backup");
    bool restore = !strcmp(ui->model.result_type, "restore_config_backup");
    if (!create && !restore) return;
    ui->model.overlay = PTC_UI_OVERLAY_CONFIG_BACKUP;
    if (strcmp(ui->model.result_status, "ok")) {
        if (restore) { char message[sizeof(ui->model.message)];
            memcpy(message, ui->model.message, sizeof(message)); open_config_backup(ui);
            memcpy(ui->model.message, message, sizeof(message)); }
        return;
    }
    if (create) {
        char stage[256];
        bool stage_valid = ptc_config_stage_path(stage, sizeof(stage), APP_ROOT, ui->active_request_id);
        bool ok = stage_valid && ptc_config_archive_save(ui->client.storage, stage, CONFIG_BACKUP_PATH);
        if (stage_valid)
            ui->client.storage->vtable->remove_tree(ui->client.storage, stage);
        if (ok) open_config_backup(ui);
        snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(ok ? PTC_UI_T_CONFIG_BACKUP_SAVED : PTC_UI_T_CONFIG_BACKUP_SAVE_FAILED));
    } else {
        ui->model.weekly_dirty = ui->model.holiday_dirty = ui->model.bedtime_dirty = ui->model.dock_dirty = false;
        load_rule_drafts(ui); load_ui_preferences(ui); refresh_theme(ui); refresh_language(ui); refresh_security_state(ui);
        memset(ui->model.credential_current, 0, sizeof(ui->model.credential_current));
        memset(ui->model.credential_new, 0, sizeof(ui->model.credential_new));
        ui->model.credential_revealed = ui->model.credential_new_revealed = false;
        ui->model.pairing_url[0] = ui->model.pairing_base_url[0] = '\0';
        if (ui->config_submitted_groups & PTC_CONFIG_PIN) config_backup_action(ui, 15);
        else open_config_backup(ui);
        snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(
            (ui->config_submitted_groups & PTC_CONFIG_PAIRING) ? PTC_UI_T_CONFIG_BACKUP_NEW_KEY : PTC_UI_T_CONFIG_BACKUP_RESTORED));
        if (ui->config_submitted_groups & PTC_CONFIG_PIN) {
            ui->setup_parent_authorized = false;
            ui->model.view = PTC_UI_CHILD; ui->model.overlay = PTC_UI_OVERLAY_NONE;
        }
    }
}
