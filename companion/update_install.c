#include "update_install.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ACTIVE "switch/playwise/updates/active.json"
#define LOCK "switch/playwise/updates/install.lock"

static bool unlock(const PtcUpdateIo *io, const PtcUpdateJournal *j)
{
    char text[256];
    if (!io->exists(io->ctx, LOCK)) return true;
    if (!io->read(io->ctx, LOCK, text, sizeof(text)) || strcmp(text, j->transaction_id)) return false;
    return io->remove(io->ctx, LOCK) && io->commit(io->ctx);
}

static const char *str(const cJSON *o, const char *key) { return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key)); }

bool ptc_update_install_busy_phase(const char *phase)
{
    return phase && (!strcmp(phase, "prepared") || !strcmp(phase, "installing") ||
        !strcmp(phase, "rolling_back") || !strcmp(phase, "recovery_required"));
}

static bool copy_string(char *out, size_t size, const char *text)
{
    if (!text || strlen(text) >= size) return false;
    memcpy(out, text, strlen(text) + 1); return true;
}

static bool number(const cJSON *o, const char *key, uint64_t *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > 9007199254740991.0 ||
        (double)(uint64_t)v->valuedouble != v->valuedouble) return false;
    *out = (uint64_t)v->valuedouble; return true;
}

bool ptc_update_journal_parse(const char *text, PtcUpdateJournal *j)
{
    cJSON *o = ptc_update_parse_json(text);
    uint64_t version, next, timestamp;
    bool ok = j && number(o, "version", &version) && version == 1;
    if (j) memset(j, 0, sizeof(*j));
    ok = ok && copy_string(j->transaction_id, sizeof(j->transaction_id), str(o, "transaction_id")) &&
        ptc_update_hex(j->transaction_id, 32) &&
        copy_string(j->source_release_id, sizeof(j->source_release_id), str(o, "source_release_id")) &&
        copy_string(j->target_release_id, sizeof(j->target_release_id), str(o, "target_release_id")) &&
        copy_string(j->target_version, sizeof(j->target_version), str(o, "target_version")) &&
        copy_string(j->source_boot_id, sizeof(j->source_boot_id), str(o, "source_boot_id")) &&
        copy_string(j->request_id, sizeof(j->request_id), str(o, "request_id")) &&
        copy_string(j->md5, sizeof(j->md5), str(o, "md5")) && ptc_update_hex(j->md5, 32) &&
        copy_string(j->zip_sha256, sizeof(j->zip_sha256), str(o, "zip_sha256")) && ptc_update_hex(j->zip_sha256, 64) &&
        copy_string(j->config_sha256, sizeof(j->config_sha256), str(o, "config_sha256")) && ptc_update_hex(j->config_sha256, 64) &&
        number(o, "config_size", &j->config_size) && j->config_size && j->config_size < 4u * 1024u * 1024u &&
        copy_string(j->phase, sizeof(j->phase), str(o, "phase")) &&
        number(o, "source_pid", &j->source_pid) && number(o, "created_at", &timestamp) &&
        number(o, "target_pid", &j->target_pid) &&
        copy_string(j->target_boot_id, sizeof(j->target_boot_id), str(o, "target_boot_id")) &&
        copy_string(j->status_request_id, sizeof(j->status_request_id), str(o, "status_request_id")) &&
        copy_string(j->status_sha256, sizeof(j->status_sha256), str(o, "status_sha256")) &&
        number(o, "next_file", &next) && next <= PTC_UPDATE_FILE_COUNT;
    int order;
    ok = ok && j->source_release_id[0] && j->target_release_id[0] &&
        ptc_update_hex(j->source_boot_id, 16) && j->source_pid && ptc_update_request_id(j->request_id) &&
        ptc_update_version_compare(j->target_version, j->target_version, &order) &&
        (ptc_update_install_busy_phase(j->phase) || !strcmp(j->phase, "files_installed") ||
         !strcmp(j->phase, "completed") || !strcmp(j->phase, "rolled_back"));
    if (ok && !strcmp(j->phase, "completed")) ok = j->target_pid && ptc_update_hex(j->target_boot_id, 16) &&
        strcmp(j->source_boot_id, j->target_boot_id) && ptc_update_request_id(j->status_request_id) && ptc_update_hex(j->status_sha256, 64);
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(o, "files");
    ok = ok && cJSON_IsArray(files) && cJSON_GetArraySize(files) == PTC_UPDATE_FILE_COUNT;
    for (int i = 0; ok && i < PTC_UPDATE_FILE_COUNT; ++i) {
        const cJSON *f = cJSON_GetArrayItem(files, i);
        const cJSON *exists = cJSON_GetObjectItemCaseSensitive(f, "existed");
        PtcUpdateFile *entry = &j->files[i];
        const char *path = str(f, "path");
        ok = path && !strcmp(path, ptc_update_files[i]) && cJSON_IsBool(exists) &&
            number(f, "before_size", &entry->before_size) && number(f, "after_size", &entry->after_size) &&
            copy_string(entry->before_sha256, 65, str(f, "before_sha256")) &&
            copy_string(entry->after_sha256, 65, str(f, "after_sha256")) &&
            ptc_update_hex(entry->after_sha256, 64);
        entry->existed = cJSON_IsTrue(exists);
        ok = ok && (entry->existed ? ptc_update_hex(entry->before_sha256, 64) : (!entry->before_sha256[0] && !entry->before_size));
        if (i == 1) ok = ok && entry->existed && !entry->before_size && !entry->after_size &&
            !strcmp(entry->before_sha256, entry->after_sha256);
    }
    if (ok) { j->next_file = (unsigned)next; j->created_at = (int64_t)timestamp; }
    cJSON_Delete(o); return ok;
}

bool ptc_update_path(char *out, size_t size, const PtcUpdateJournal *j, const char *part, int file)
{
    if (!j || !ptc_update_hex(j->transaction_id, 32) ||
        (strcmp(part, "stage") && strcmp(part, "before") && strcmp(part, "config-backup.json") && strcmp(part, "journal.json"))) return false;
    int n = file >= 0 && file < PTC_UPDATE_FILE_COUNT
        ? snprintf(out, size, "switch/playwise/updates/%s/%s/%s", j->transaction_id, part, ptc_update_files[file])
        : snprintf(out, size, "switch/playwise/updates/%s/%s", j->transaction_id, part);
    return n > 0 && (size_t)n < size;
}

static cJSON *journal_json(const PtcUpdateJournal *j)
{
    cJSON *o = cJSON_CreateObject(), *files = cJSON_CreateArray();
    if (!o || !files) { cJSON_Delete(o); cJSON_Delete(files); return NULL; }
    cJSON_AddNumberToObject(o, "version", 1);
    cJSON_AddStringToObject(o, "transaction_id", j->transaction_id);
    cJSON_AddStringToObject(o, "source_release_id", j->source_release_id);
    cJSON_AddStringToObject(o, "target_release_id", j->target_release_id);
    cJSON_AddStringToObject(o, "target_version", j->target_version);
    cJSON_AddStringToObject(o, "source_boot_id", j->source_boot_id);
    cJSON_AddStringToObject(o, "request_id", j->request_id);
    cJSON_AddStringToObject(o, "md5", j->md5);
    cJSON_AddStringToObject(o, "zip_sha256", j->zip_sha256);
    cJSON_AddStringToObject(o, "config_sha256", j->config_sha256);
    cJSON_AddNumberToObject(o, "config_size", (double)j->config_size);
    cJSON_AddStringToObject(o, "phase", j->phase);
    cJSON_AddNumberToObject(o, "source_pid", (double)j->source_pid);
    cJSON_AddNumberToObject(o, "target_pid", (double)j->target_pid);
    cJSON_AddStringToObject(o, "target_boot_id", j->target_boot_id);
    cJSON_AddStringToObject(o, "status_request_id", j->status_request_id);
    cJSON_AddStringToObject(o, "status_sha256", j->status_sha256);
    cJSON_AddNumberToObject(o, "created_at", (double)j->created_at);
    cJSON_AddNumberToObject(o, "next_file", j->next_file);
    cJSON_AddItemToObject(o, "files", files);
    for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) {
        cJSON *f = cJSON_CreateObject();
        if (!f) { cJSON_Delete(o); return NULL; }
        cJSON_AddStringToObject(f, "path", ptc_update_files[i]);
        cJSON_AddBoolToObject(f, "existed", j->files[i].existed);
        cJSON_AddNumberToObject(f, "before_size", (double)j->files[i].before_size);
        cJSON_AddNumberToObject(f, "after_size", (double)j->files[i].after_size);
        cJSON_AddStringToObject(f, "before_sha256", j->files[i].before_sha256);
        cJSON_AddStringToObject(f, "after_sha256", j->files[i].after_sha256);
        cJSON_AddItemToArray(files, f);
    }
    return o;
}

static bool save_journal(const PtcUpdateIo *io, const PtcUpdateJournal *j, bool active)
{
    char path[384];
    cJSON *o = journal_json(j);
    char *text = o ? cJSON_PrintUnformatted(o) : NULL;
    char *readback = malloc(PTC_UPDATE_JOURNAL_LIMIT);
    PtcUpdateJournal parsed;
    bool ok = text && readback && strlen(text) < PTC_UPDATE_JOURNAL_LIMIT && ptc_update_journal_parse(text, &parsed) &&
        ptc_update_path(path, sizeof(path), j, "journal.json", -1) &&
        io->write(io->ctx, path, text) && io->commit(io->ctx) &&
        io->read(io->ctx, path, readback, PTC_UPDATE_JOURNAL_LIMIT) && !strcmp(text, readback);
    if (ok && active) ok = io->write(io->ctx, ACTIVE, text) && io->commit(io->ctx) &&
        io->read(io->ctx, ACTIVE, readback, PTC_UPDATE_JOURNAL_LIMIT) && !strcmp(text, readback);
    free(readback); free(text); cJSON_Delete(o); return ok;
}

bool ptc_update_journal_save(const PtcUpdateIo *io, const PtcUpdateJournal *j) { return save_journal(io, j, true); }

static bool load_at(const PtcUpdateIo *io, const char *path, PtcUpdateJournal *j)
{
    char *text = malloc(PTC_UPDATE_JOURNAL_LIMIT);
    bool ok = text && io->read(io->ctx, path, text, PTC_UPDATE_JOURNAL_LIMIT) && ptc_update_journal_parse(text, j);
    free(text); return ok;
}

bool ptc_update_journal_load(const PtcUpdateIo *io, PtcUpdateJournal *j)
{
    char lock[256];
    char fallback[256];
    bool active = io->exists(io->ctx, ACTIVE);
    if (active && !load_at(io, ACTIVE, j)) return false;
    if (io->exists(io->ctx, LOCK)) {
        if (!io->read(io->ctx, LOCK, lock, sizeof(lock)) || !ptc_update_hex(lock, 32)) return false;
        if (!active || strcmp(j->transaction_id, lock)) {
            if (active && strcmp(j->phase, "completed") && strcmp(j->phase, "rolled_back")) return false;
            snprintf(fallback, sizeof(fallback), "switch/playwise/updates/%.32s/journal.json", lock);
            return load_at(io, fallback, j) && !strcmp(j->transaction_id, lock);
        }
    }
    return active;
}

static bool match(const PtcUpdateIo *io, const char *path, uint64_t size, const char *hash)
{
    uint64_t actual_size; char actual_hash[65];
    return io->hash(io->ctx, path, &actual_size, actual_hash) && size == actual_size && !strcmp(actual_hash, hash);
}

bool ptc_update_files_match(const PtcUpdateIo *io, const PtcUpdateJournal *j, bool target)
{
    for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) {
        const PtcUpdateFile *f = &j->files[i];
        if (!target && !f->existed) { if (io->exists(io->ctx, ptc_update_files[i])) return false; }
        else if (!match(io, ptc_update_files[i], target ? f->after_size : f->before_size,
                       target ? f->after_sha256 : f->before_sha256)) return false;
    }
    return true;
}

bool ptc_update_prepare(const PtcUpdateIo *io, PtcUpdateJournal *j)
{
    char stage[384], before[384], config[384];
    if (io->exists(io->ctx, LOCK)) return false;
    if (io->exists(io->ctx, ACTIVE)) {
        PtcUpdateJournal old;
        if (!ptc_update_journal_load(io, &old) || (strcmp(old.phase, "completed") && strcmp(old.phase, "rolled_back"))) return false;
    }
    if (!ptc_update_path(config, sizeof(config), j, "config-backup.json", -1) || !match(io, config, j->config_size, j->config_sha256)) return false;
    for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) {
        PtcUpdateFile *f = &j->files[i];
        PtcUpdateFile frozen = *f;
        char digest[65]; uint64_t size;
        if (!ptc_update_hex(frozen.after_sha256, 64) || frozen.after_size > PTC_UPDATE_ZIP_LIMIT) return false;
        if (!ptc_update_path(stage, sizeof(stage), j, "stage", i) ||
            !ptc_update_path(before, sizeof(before), j, "before", i) ||
            !io->hash(io->ctx, stage, &size, digest)) return false;
        if (frozen.after_size != size || strcmp(frozen.after_sha256, digest)) return false;
        memset(f, 0, sizeof(*f)); f->after_size = size; memcpy(f->after_sha256, digest, 65);
        f->existed = io->exists(io->ctx, ptc_update_files[i]);
        if (f->existed && (!io->hash(io->ctx, ptc_update_files[i], &f->before_size, f->before_sha256) ||
            !io->copy(io->ctx, ptc_update_files[i], before) ||
            !match(io, before, f->before_size, f->before_sha256))) return false;
        if (i == 1 && (!f->existed || f->before_size || f->after_size)) return false;
    }
    j->target_pid = 0;
    memset(j->target_boot_id, 0, sizeof(j->target_boot_id)); memset(j->status_request_id, 0, sizeof(j->status_request_id)); memset(j->status_sha256, 0, sizeof(j->status_sha256));
    j->next_file = 0; strcpy(j->phase, "prepared");
    if (!save_journal(io, j, false)) return false;
    if (!io->claim || !io->claim(io->ctx, LOCK, j->transaction_id) || !io->commit(io->ctx)) return false;
    if (io->exists(io->ctx, ACTIVE)) {
        PtcUpdateJournal previous;
        if (!load_at(io, ACTIVE, &previous) || (strcmp(previous.phase, "completed") && strcmp(previous.phase, "rolled_back"))) {
            (void)unlock(io, j); return false;
        }
    }
    if (!ptc_update_files_match(io, j, false)) { (void)unlock(io, j); return false; }
    return ptc_update_journal_save(io, j);
}

bool ptc_update_install(const PtcUpdateIo *io, PtcUpdateJournal *j)
{
    char stage[384];
    if (strcmp(j->phase, "prepared") || !ptc_update_files_match(io, j, false)) return false;
    strcpy(j->phase, "installing");
    if (!ptc_update_journal_save(io, j)) return false;
    for (unsigned i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) {
        const PtcUpdateFile *f = &j->files[i];
        j->next_file = i; /* Durable intent precedes every possible live-path mutation. */
        if (!ptc_update_journal_save(io, j) || !ptc_update_path(stage, sizeof(stage), j, "stage", (int)i) ||
            !match(io, stage, f->after_size, f->after_sha256)) return false;
        if (f->existed ? !match(io, ptc_update_files[i], f->before_size, f->before_sha256) : io->exists(io->ctx, ptc_update_files[i])) return false;
        if (i != 1 && (!io->publish(io->ctx, stage, ptc_update_files[i], f->before_sha256, !f->existed) || !io->commit(io->ctx))) return false;
        if (!match(io, ptc_update_files[i], f->after_size, f->after_sha256)) return false;
        j->next_file = i + 1;
        if (!ptc_update_journal_save(io, j)) return false;
    }
    if (!ptc_update_files_match(io, j, true)) return false;
    strcpy(j->phase, "files_installed");
    return ptc_update_journal_save(io, j) && unlock(io, j);
}

bool ptc_update_rollback(const PtcUpdateIo *io, PtcUpdateJournal *j)
{
    char before[384];
    if (!strcmp(j->phase, "rolled_back")) return ptc_update_files_match(io, j, false) && unlock(io, j);
    if (!ptc_update_install_busy_phase(j->phase) && strcmp(j->phase, "files_installed")) return false;
    /* Validate every preimage and every live path before restoring anything.
       A missing path is explainable only at the durable interrupted file. */
    for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) {
        const PtcUpdateFile *f = &j->files[i];
        if (f->existed && (!ptc_update_path(before, sizeof(before), j, "before", i) ||
            !match(io, before, f->before_size, f->before_sha256))) return false;
        bool exists = io->exists(io->ctx, ptc_update_files[i]);
        if (exists && !match(io, ptc_update_files[i], f->after_size, f->after_sha256) &&
            !(f->existed && match(io, ptc_update_files[i], f->before_size, f->before_sha256))) return false;
        if (!exists && f->existed && (unsigned)i != j->next_file) return false;
    }
    strcpy(j->phase, "rolling_back");
    if (!ptc_update_journal_save(io, j)) return false;
    for (int i = PTC_UPDATE_FILE_COUNT - 1; i >= 0; --i) {
        const PtcUpdateFile *f = &j->files[i];
        if (i == 1) continue;
        j->next_file = (unsigned)i;
        if (!ptc_update_journal_save(io, j)) return false;
        if (io->discard_temporary && !io->discard_temporary(io->ctx, ptc_update_files[i], f->before_sha256, f->after_sha256)) return false;
        if (f->existed) {
            if (match(io, ptc_update_files[i], f->before_size, f->before_sha256)) continue;
            if (!ptc_update_path(before, sizeof(before), j, "before", i) || !io->publish(io->ctx, before, ptc_update_files[i], f->after_sha256, true)) return false;
        } else if (io->exists(io->ctx, ptc_update_files[i]) &&
            (!match(io, ptc_update_files[i], f->after_size, f->after_sha256) || !io->remove(io->ctx, ptc_update_files[i]))) return false;
        if (!io->commit(io->ctx)) return false;
    }
    if (!ptc_update_files_match(io, j, false)) return false;
    strcpy(j->phase, "rolled_back");
    return ptc_update_journal_save(io, j) && unlock(io, j);
}
