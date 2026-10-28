#include "nro_app_internal.h"

#ifndef PLAYWISE_EDEN
static bool ui_busy(const UiState *ui)
{
    return ui->waiting || ui->recovering_redemption || ui->model.recovery_active ||
        ui->model.weekly_dirty || ui->model.holiday_dirty || ui->model.bedtime_dirty || ui->model.dock_dirty ||
        ui->hot_reload.status == PTC_HOT_RELOAD_RUNNING;
}

static void sync_model(UiState *ui)
{
    PtcOnlineUpdater *u = &ui->updater;
    ui->model.update_busy = u->worker_live || ui->update_backup_pending;
    ui->model.update_received_kib = (unsigned)(atomic_load(&u->progress) / 1024u);
    /* Worker-owned results are consumed only after acquire + thread join. */
    if (u->worker_live) {
        ui->model.update_status = u->job == 1 ? PTC_UPDATE_CHECKING : (u->job == 2 ? PTC_UPDATE_DOWNLOADING : PTC_UPDATE_INSTALLING);
        return;
    }
    ui->model.update_status = u->status;
    ui->model.update_error = u->error;
    snprintf(ui->model.update_version, sizeof(ui->model.update_version), "%s", u->offer.version);
    snprintf(ui->model.update_installed_version, sizeof(ui->model.update_installed_version), "%s", u->installed_version);
    snprintf(ui->model.update_notes, sizeof(ui->model.update_notes), "%s", u->offer.notes);
    ui->model.update_size_kib = u->offer.size_kib;
}

static bool history(PtcOnlineUpdater *u, const char *id, PtcUpdateJournal *j)
{
    char path[384], *text = malloc(PTC_UPDATE_JOURNAL_LIMIT), hash[65]; uint64_t size;
    if (!ptc_update_hex(id, 32) || !text) { free(text); return false; }
    snprintf(path, sizeof(path), "switch/playwise/updates/%s/journal.json", id);
    bool ok = u->fs.io.read(&u->fs, path, text, PTC_UPDATE_JOURNAL_LIMIT) && ptc_update_journal_parse(text, j) &&
        !strcmp(id, j->transaction_id) && ptc_update_path(path, sizeof(path), j, "config-backup.json", -1) &&
        u->fs.io.hash(&u->fs, path, &size, hash) && size == j->config_size && !strcmp(hash, j->config_sha256);
    free(text); return ok;
}

static bool download_history(PtcOnlineUpdater *u, const char *id, PtcUpdateJournal *j)
{
    char path[384], text[2048];
    if (!ptc_update_hex(id, 32)) return false;
    snprintf(path, sizeof(path), "switch/playwise/updates/%s/journal.json", id);
    if (u->fs.io.exists(&u->fs, path)) return false; /* Unknown installation records are never reclassified as cache. */
    snprintf(path, sizeof(path), "switch/playwise/updates/%s/download.json", id);
    if (!u->fs.io.read(&u->fs, path, text, sizeof(text))) return false;
    cJSON *o = ptc_update_parse_json(text);
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, "version"), *date = cJSON_GetObjectItemCaseSensitive(o, "created_at");
    const char *transaction = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "transaction_id"));
    const char *state = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "state"));
    const char *source = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "source_release_id"));
    bool ok = cJSON_IsNumber(v) && v->valuedouble == 1 && transaction && !strcmp(transaction, id) && state &&
        (!strcmp(state, "failed") || !strcmp(state, "canceled") || !strcmp(state, "backup_unknown")) &&
        source && *source && strlen(source) < sizeof(j->source_release_id) && cJSON_IsNumber(date) &&
        date->valuedouble >= 0 && date->valuedouble <= 9007199254740991.0 && (double)(int64_t)date->valuedouble == date->valuedouble;
    if (ok) {
        memset(j, 0, sizeof(*j)); strcpy(j->transaction_id, id); strcpy(j->source_release_id, source);
        strcpy(j->phase, state); j->created_at = (int64_t)date->valuedouble;
    }
    cJSON_Delete(o); return ok;
}

static void list_backups(UiState *ui)
{
    PtcStorageEntry *entries = calloc(128, sizeof(*entries)); size_t count = 0;
    PtcOnlineUpdater *u = &ui->updater;
    int64_t dates[16] = {0};
    ui->model.update_backup_count = 0; ui->model.update_backup_index = 0;
    if (!entries || !u->fs.storage->vtable->list_entries(u->fs.storage, APP_ROOT "/updates", entries, 128, &count)) { free(entries); return; }
    for (size_t i = 0; i < count; ++i) {
        PtcUpdateJournal j;
        if (entries[i].type != PTC_STORAGE_ENTRY_DIRECTORY || (!history(u, entries[i].name, &j) && !download_history(u, entries[i].name, &j))) continue;
        int n = ui->model.update_backup_count;
        if (n == 16 && j.created_at <= dates[15]) continue;
        if (n < 16) ++ui->model.update_backup_count;
        else n = 15;
        while (n > 0 && dates[n - 1] < j.created_at) {
            dates[n] = dates[n - 1];
            memcpy(ui->update_backup_ids[n], ui->update_backup_ids[n - 1], 33);
            memcpy(ui->model.update_backup_labels[n], ui->model.update_backup_labels[n - 1], sizeof(ui->model.update_backup_labels[n])); --n;
        }
        dates[n] = j.created_at; strcpy(ui->update_backup_ids[n], j.transaction_id);
        time_t timestamp = (time_t)j.created_at; struct tm local; char date[32] = "?";
        if (localtime_r(&timestamp, &local)) strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &local);
        bool archived = j.config_sha256[0] != '\0';
        snprintf(ui->model.update_backup_labels[n], sizeof(ui->model.update_backup_labels[n]), "%.31s / %.48s\n%.95s", date,
            ptc_ui_text(archived ? PTC_UI_T_UPDATE_BACKUP_VERIFIED : PTC_UI_T_UPDATE_FAILED), j.source_release_id);
    }
    free(entries);
}

static bool owned_tree(UiState *ui, const char *base, const char *part, unsigned depth)
{
    char path[768]; PtcStorageEntry *entries = calloc(64, sizeof(*entries)); size_t count; bool ok = false;
    if (!entries || depth > 10 || snprintf(path, sizeof(path), "%s%s", base, part) >= (int)sizeof(path) ||
        !ui->client.storage->vtable->list_entries(ui->client.storage, path, entries, 64, &count)) goto done;
    for (size_t i = 0; i < count; ++i) {
        const char *name = entries[i].name;
        if (!*name || strchr(name, '/') || strchr(name, '\\') || strchr(name, ':') || !strcmp(name, ".") || !strcmp(name, "..")) goto done;
        char relative[640];
        if (snprintf(relative, sizeof(relative), "%s/%s", part, name) >= (int)sizeof(relative)) goto done;
        if (entries[i].type == PTC_STORAGE_ENTRY_DIRECTORY) {
            bool owned = !strcmp(relative, "/stage") || !strcmp(relative, "/before") || !strcmp(relative, "/receipts");
            const char *asset = !strncmp(relative, "/stage/", 7) ? relative + 7 : (!strncmp(relative, "/before/", 8) ? relative + 8 : NULL);
            if (asset) for (int k = 0; k < PTC_UPDATE_FILE_COUNT; ++k)
                if (!strncmp(asset, ptc_update_files[k], strlen(asset)) && ptc_update_files[k][strlen(asset)] == '/') owned = true;
            if (!owned || !owned_tree(ui, base, relative, depth + 1)) goto done;
        } else if (entries[i].type == PTC_STORAGE_ENTRY_FILE) {
            const char *asset = !strncmp(relative, "/stage/", 7) ? relative + 7 : (!strncmp(relative, "/before/", 8) ? relative + 8 : NULL);
            if (asset) {
                if (!ptc_update_archive_path(asset, false)) goto done;
            } else if (!strncmp(relative, "/receipts/", 10)) {
                char id[80]; size_t n = strlen(relative + 10);
                if (n <= 5 || n - 5 >= sizeof(id) || strcmp(relative + strlen(relative) - 5, ".json")) goto done;
                memcpy(id, relative + 10, n - 5); id[n - 5] = '\0'; if (!ptc_update_request_id(id)) goto done;
            } else if (strcmp(relative, "/journal.json") && strcmp(relative, "/config-backup.json") && strcmp(relative, "/download.zip") && strcmp(relative, "/download.json")) goto done;
        } else goto done;
    }
    ok = true;
done:
    free(entries); return ok;
}

static bool delete_history(UiState *ui, const char *id, bool automatic)
{
    PtcUpdateJournal j, active;
    PtcOnlineUpdater *u = &ui->updater;
    if ((!history(u, id, &j) && !download_history(u, id, &j)) || (automatic && strcmp(j.phase, "completed"))) return false;
    if (u->fs.io.exists(&u->fs, "switch/playwise/updates/pending-backup.json")) return false;
    if (u->fs.io.exists(&u->fs, "switch/playwise/updates/active.json") &&
        (!ptc_update_journal_load(&u->fs.io, &active) || !strcmp(active.transaction_id, id))) return false;
    char base[512]; snprintf(base, sizeof(base), APP_ROOT "/updates/%s", id);
    return owned_tree(ui, base, "", 0) && ui->client.storage->vtable->remove_tree(ui->client.storage, base);
}

static void retain_backups(UiState *ui)
{
    list_backups(ui); int successful = 0;
    for (int i = 0; i < ui->model.update_backup_count; ++i) {
        PtcUpdateJournal j;
        if (!history(&ui->updater, ui->update_backup_ids[i], &j) || strcmp(j.phase, "completed")) continue;
        if (++successful > 3) (void)delete_history(ui, j.transaction_id, true);
    }
}
#endif

void open_online_update(UiState *ui)
{
    if (!ui || ui->model.view != PTC_UI_PARENT) return;
    ui->model.update_notes_open = false; ui->model.update_notes_page = 0;
    ui->model.overlay = PTC_UI_OVERLAY_ONLINE_UPDATE; ui->model.overlay_selection = 0;
#ifndef PLAYWISE_EDEN
    sync_model(ui);
#endif
}

void online_update_action(UiState *ui, int action)
{
    if (!ui || ui->model.view != PTC_UI_PARENT) return;
    if (action == 8) {
        if (!ui->model.update_busy && ui->model.update_notes[0]) {
            ui->model.update_notes_open = true; ui->model.update_notes_page = 0;
        }
        return;
    }
#ifndef PLAYWISE_EDEN
    if (!ptc_ui_update_action_enabled(&ui->model, action)) return;
    PtcOnlineUpdater *u = &ui->updater;
    if (action == 4 && !u->worker_live) {
        ui->model.update_cleanup_failed = false;
        list_backups(ui); ui->model.overlay = PTC_UI_OVERLAY_UPDATE_BACKUPS; ui->model.overlay_selection = 0; return;
    }
    if (action == 6 && u->worker_live && u->job <= 2) { atomic_store(&u->cancel, true); return; }
    if (u->worker_live || ui_busy(ui)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(PTC_UI_T_UPDATE_BUSY)); return;
    }
    if (action == 0) {
        if (u->backup_submitted) u->status = PTC_UPDATE_BACKUP_PENDING;
        else { u->stable_only = ui->model.update_stable_only; (void)ptc_online_update_start(u, 1); }
    }
    else if (action == 1 && !ui->model.parent_support_only && u->status == PTC_UPDATE_AVAILABLE) {
        if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_UPDATE_DOWNLOAD))) return;
        open_confirm_overlay(ui, PTC_UI_OPERATION_ONLINE_UPDATE, ptc_ui_text(PTC_UI_T_UPDATE_DOWNLOAD), ptc_ui_text(PTC_UI_T_UPDATE_CONFIRM));
        ui->model.confirm_hold_required = true;
    } else if ((action == 2 || action == 3) && !ui->model.parent_support_only) {
        bool old = action == 2 ? ui->model.update_check_on_startup : ui->model.update_stable_only;
        if (action == 2) ui->model.update_check_on_startup = !old; else ui->model.update_stable_only = !old;
        if (!save_ui_preferences(ui)) { if (action == 2) ui->model.update_check_on_startup = old; else ui->model.update_stable_only = old; }
        if (action == 3 && u->offer.version[0]) u->status = ptc_update_offer_status(&u->offer, PLAYWISE_VERSION, ui->model.update_stable_only);
    } else if (action == 5 && !ui->model.parent_support_only) {
        if (u->status == PTC_UPDATE_RECONFIRM) { ui->model.overlay = PTC_UI_OVERLAY_NONE; ui->model.parent_page = PTC_UI_PARENT_SUPPORT; }
        else open_hot_reload_confirmation(ui);
    }
    else if (action == 6 && !ui->model.parent_support_only && u->status == PTC_UPDATE_RECOVERY_REQUIRED) {
        if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_UPDATE_RECOVER))) return;
        open_confirm_overlay(ui, PTC_UI_OPERATION_UPDATE_RECOVER, ptc_ui_text(PTC_UI_T_UPDATE_RECOVER), ptc_ui_text(PTC_UI_T_UPDATE_RECOVER_CONFIRM));
        ui->model.confirm_hold_required = true;
    } else if (action == 7) ui->exit_requested = true;
    sync_model(ui);
#else
    (void)ui; (void)action;
#endif
}

void open_online_update_badge(UiState *ui)
{
    if (!ui || !ptc_ui_update_badge_visible(&ui->model)) return;
    if (ui->model.view == PTC_UI_PARENT) { open_online_update(ui); return; }
    ui->update_open_after_parent_auth = true;
    enter_parent_area(ui);
    if (ui->auth_retry_action == AUTH_RETRY_NONE) ui->update_open_after_parent_auth = false;
}

void handle_online_update_input(UiState *ui, u64 down)
{
    if (ui->model.view != PTC_UI_PARENT) { ui->model.overlay = PTC_UI_OVERLAY_NONE; return; }
    if (ui->model.overlay == PTC_UI_OVERLAY_ONLINE_UPDATE && ui->model.update_notes_open) {
        if (down & (HidNpadButton_B | HidNpadButton_Y)) ui->model.update_notes_open = false;
        else if (down & (HidNpadButton_L | HidNpadButton_Left | HidNpadButton_Up))
            ui->model.update_notes_page = ui->model.update_notes_page > -32768 ? ui->model.update_notes_page - 1 : 0;
        else if (down & (HidNpadButton_R | HidNpadButton_Right | HidNpadButton_Down | HidNpadButton_A))
            ui->model.update_notes_page = ui->model.update_notes_page < 32768 ? ui->model.update_notes_page + 1 : 0;
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_UPDATE_BACKUPS) {
#ifndef PLAYWISE_EDEN
        int selected = ui->model.update_backup_index + ui->model.overlay_selection;
        if (down & HidNpadButton_B) { open_online_update(ui); return; }
        if (down & (HidNpadButton_L | HidNpadButton_R)) {
            int first = ui->model.update_backup_index;
            if (down & HidNpadButton_L) first = first >= 4 ? first - 4 : 0;
            else if (first + 4 < ui->model.update_backup_count) first += 4;
            ui->model.update_backup_index = first; selected = first;
        }
        if ((down & HidNpadButton_Up) && selected > 0) --selected;
        if ((down & HidNpadButton_Down) && selected + 1 < ui->model.update_backup_count) ++selected;
        if (selected < ui->model.update_backup_index) ui->model.update_backup_index = selected;
        if (selected >= ui->model.update_backup_index + 4) ui->model.update_backup_index = selected - 3;
        ui->model.overlay_selection = selected - ui->model.update_backup_index;
        if (selected < 0 || selected >= ui->model.update_backup_count || ui->model.parent_support_only) return;
        if (down & HidNpadButton_A) {
            PtcUpdateJournal j;
            if (!history(&ui->updater, ui->update_backup_ids[selected], &j)) return;
            snprintf(ui->config_backup_selected_path, sizeof(ui->config_backup_selected_path), APP_ROOT "/updates/%s/config-backup.json", j.transaction_id);
            open_config_backup(ui); ui->model.config_backup_tab = 1; ui->model.overlay_selection = 14;
        } else if (down & HidNpadButton_X) {
            if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_UPDATE_CLEANUP))) return;
            strcpy(ui->update_cleanup_id, ui->update_backup_ids[selected]);
            open_confirm_overlay(ui, PTC_UI_OPERATION_UPDATE_CLEANUP, ptc_ui_text(PTC_UI_T_UPDATE_CLEANUP), ptc_ui_text(PTC_UI_T_UPDATE_DELETE_CONFIRM));
            ui->model.confirm_hold_required = true;
        }
#else
        if (down & HidNpadButton_B) open_online_update(ui);
#endif
        return;
    }
    if (down & HidNpadButton_Y) { online_update_action(ui, 8); return; }
    if (down & HidNpadButton_B) { if (!ui->model.update_busy) ptc_ui_cancel_overlay(&ui->model); return; }
    int selection = ui->model.overlay_selection;
    if (down & HidNpadButton_Up) selection = (selection + 6) % 8;
    if (down & HidNpadButton_Down) selection = (selection + 2) % 8;
    if (down & (HidNpadButton_Left | HidNpadButton_Right)) selection ^= 1;
    ui->model.overlay_selection = selection;
    if (down & HidNpadButton_A) online_update_action(ui, selection);
}

void confirm_online_update(UiState *ui, PtcUiOperation operation)
{
#ifndef PLAYWISE_EDEN
    PtcOnlineUpdater *u = &ui->updater;
    if (!ptc_ui_update_can_resume_backup(&ui->model) || ui_busy(ui) || u->worker_live) return;
    if (operation == PTC_UI_OPERATION_UPDATE_CLEANUP) {
        bool removed = delete_history(ui, ui->update_cleanup_id, false);
        ui->model.update_cleanup_failed = !removed;
        ui->update_cleanup_id[0] = '\0';
        list_backups(ui); ui->model.overlay = PTC_UI_OVERLAY_UPDATE_BACKUPS; ui->model.overlay_selection = 0;
    } else {
        if (operation == PTC_UI_OPERATION_UPDATE_RECOVER) {
            if (ptc_update_journal_load(&u->fs.io, &u->journal) && ptc_online_update_can_recover(u)) (void)ptc_online_update_start(u, 4);
            else u->error = PTC_UPDATE_ERROR_CONFLICT;
        } else (void)ptc_online_update_start(u, 2);
        open_online_update(ui);
    }
#else
    (void)ui; (void)operation;
#endif
}

void poll_online_update(UiState *ui)
{
#ifndef PLAYWISE_EDEN
    PtcOnlineUpdater *u = &ui->updater;
    bool done = ptc_online_update_poll(u);
    if (!u->started && !u->worker_live && ui->model.setup_wizard_completed && !ui_busy(ui)) {
        u->started = true;
        if (ui->model.update_check_on_startup && u->status == PTC_UPDATE_IDLE && !u->backup_submitted) {
            u->stable_only = ui->model.update_stable_only; (void)ptc_online_update_start(u, 1);
        }
    }
    if (!u->worker_live && u->status == PTC_UPDATE_BACKUP_PENDING && !ui->update_backup_pending &&
        ptc_ui_update_can_resume_backup(&ui->model) && !ui_busy(ui)) {
        /* Never truncate an original request ID or poll a different request. */
        if (u->backup_submitted && strlen(u->journal.request_id) >= sizeof(ui->active_request_id)) {
            u->status = PTC_UPDATE_RECOVERY_REQUIRED; u->error = PTC_UPDATE_ERROR_CONFLICT;
            sync_model(ui); return;
        }
        ui->update_backup_pending = true;
        if (u->backup_submitted) {
            memcpy(ui->active_request_id, u->journal.request_id, strlen(u->journal.request_id) + 1);
            set_command_name(ui, "create_config_backup");
            begin_wait(ui, "create_config_backup", ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_PENDING));
        } else submit_config_backup(ui, false);
        ui->model.overlay = PTC_UI_OVERLAY_ONLINE_UPDATE;
        if (!ui->waiting) { ui->update_backup_pending = false; u->status = PTC_UPDATE_FAILED; }
    }
    if (ui->update_backup_pending && !ui->waiting) {
        ui->update_backup_pending = false; u->status = PTC_UPDATE_BACKUP_UNKNOWN;
        u->error = PTC_UPDATE_ERROR_BACKUP;
        (void)ptc_online_update_record(u, "backup_unknown");
    }
    (void)done;
    sync_model(ui);
    bool parent_needed = u->status == PTC_UPDATE_BACKUP_PENDING || u->status == PTC_UPDATE_BACKUP_UNKNOWN ||
        u->status == PTC_UPDATE_RECOVERY_REQUIRED || u->status == PTC_UPDATE_AWAITING_REOPEN ||
        u->status == PTC_UPDATE_BACKEND_PENDING || u->status == PTC_UPDATE_RECONFIRM;
    if (!u->worker_live && (u->status == PTC_UPDATE_AVAILABLE || parent_needed) &&
        ui->model.view == PTC_UI_CHILD && !ui->update_child_notice_shown) {
        ui->update_child_notice_shown = true;
        snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(PTC_UI_T_UPDATE_PARENT_REQUIRED));
    }
#else
    (void)ui;
#endif
}

bool online_update_backup_result(UiState *ui)
{
#ifndef PLAYWISE_EDEN
    if (!ui->update_backup_pending || strcmp(ui->model.result_type, "create_config_backup")) return false;
    PtcOnlineUpdater *u = &ui->updater;
    if (strcmp(ui->active_request_id, u->journal.request_id)) { u->status = PTC_UPDATE_RECOVERY_REQUIRED; return true; }
    if (!ptc_ui_update_can_resume_backup(&ui->model)) {
        ui->update_backup_pending = false; u->status = PTC_UPDATE_BACKUP_PENDING;
        return true; /* Keep the original request and its stage until a parent returns. */
    }
    ui->update_backup_pending = false;
    char stage[256], relative[384], archive[768], verify[256], digest[65];
    bool ok = !strcmp(ui->model.result_status, "ok") &&
        ptc_config_stage_path(stage, sizeof(stage), APP_ROOT, ui->active_request_id) &&
        ptc_update_path(relative, sizeof(relative), &u->journal, "config-backup.json", -1) &&
        ptc_update_fs_path(&u->fs, relative, archive, sizeof(archive)) &&
        ptc_config_archive_save(ui->client.storage, stage, archive);
    make_next_request_id(verify, sizeof(verify));
    char verify_path[256];
    ok = ok && ptc_config_stage_path(verify_path, sizeof(verify_path), APP_ROOT, verify) &&
        ptc_config_archive_load(ui->client.storage, archive, verify_path, digest) &&
        u->fs.io.hash(&u->fs, relative, &u->journal.config_size, u->journal.config_sha256);
    if (ok) (void)ui->client.storage->vtable->remove_tree(ui->client.storage, verify_path);
    if (ptc_config_stage_path(stage, sizeof(stage), APP_ROOT, ui->active_request_id))
        (void)ui->client.storage->vtable->remove_tree(ui->client.storage, stage);
    uint64_t source_pid = u->journal.source_pid;
    char source_boot[32]; strcpy(source_boot, u->journal.source_boot_id);
    ok = ok && ptc_online_update_source(u) && source_pid == u->journal.source_pid && !strcmp(source_boot, u->journal.source_boot_id);
    bool receipt_closed = u->fs.io.remove(&u->fs, "switch/playwise/updates/pending-backup.json") && u->fs.io.commit(&u->fs);
    if (receipt_closed) u->backup_submitted = false;
    ok = ok && receipt_closed;
    if (ok) ok = ptc_online_update_start(u, 3);
    if (!ok) { u->error = PTC_UPDATE_ERROR_BACKUP; u->status = PTC_UPDATE_FAILED; (void)ptc_online_update_record(u, "failed"); }
    ui->model.overlay = PTC_UI_OVERLAY_ONLINE_UPDATE; sync_model(ui); return true;
#else
    (void)ui; return false;
#endif
}

bool online_update_backup_before_submit(UiState *ui)
{
#ifndef PLAYWISE_EDEN
    if (!ui->update_backup_pending) return true;
    if (!ptc_ui_update_can_resume_backup(&ui->model)) return false;
    snprintf(ui->updater.journal.request_id, sizeof(ui->updater.journal.request_id), "%s", ui->active_request_id);
    if (!ptc_online_update_pending_backup(&ui->updater, true)) return false;
    ui->updater.backup_submitted = true;
#else
    (void)ui;
#endif
    return true;
}

void online_update_status_result(UiState *ui)
{
#ifndef PLAYWISE_EDEN
    PtcOnlineUpdater *u = &ui->updater;
    if (u->worker_live || strcmp(ui->model.result_type, "status") || strcmp(ui->model.result_status, "ok") ||
        (ui->hot_reload.status != PTC_HOT_RELOAD_CURRENT && ui->hot_reload.status != PTC_HOT_RELOAD_SUCCESS)) return;
    if (!strcmp(u->journal.phase, "files_installed") && !strcmp(u->journal.target_release_id, PLAYWISE_BUILD_RELEASE_ID) &&
        (strcmp(ui->model.setup_phase, "active") || ui->model.disable_flag_present || ui->model.recovery_active)) {
        u->status = PTC_UPDATE_RECONFIRM; sync_model(ui); return;
    }
    if (ptc_online_update_complete(u, ui->active_request_id, ui->last_result)) { retain_backups(ui); sync_model(ui); }
#else
    (void)ui;
#endif
}
