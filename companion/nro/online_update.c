#include "online_update.h"
#include "../hot_reload_guard.h"
#include "../../common/version.h"
#include "../../common/crypto/sha256.h"
#include "release_manifest.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

typedef struct { PtcOnlineUpdater *u; FILE *file; char *text; size_t used, limit, headers, skipped; bool redirect; } Download;

static size_t header(void *data, size_t size, size_t count, void *ctx)
{
    Download *d = ctx;
    if (count && size > SIZE_MAX / count) return 0;
    size_t bytes = size * count;
    if (bytes > 65536u - d->headers) return 0;
    d->headers += bytes;
    if (bytes > 5 && !memcmp(data, "HTTP/", 5)) {
        char line[128]; size_t n = bytes < sizeof(line) - 1 ? bytes : sizeof(line) - 1;
        memcpy(line, data, n); line[n] = '\0'; long status;
        if (sscanf(line, "HTTP/%*s %ld", &status) != 1) return 0;
        d->redirect = status >= 300 && status < 400;
    }
    return bytes;
}

static size_t receive(void *data, size_t size, size_t count, void *ctx)
{
    Download *d = ctx;
    if (count && size > SIZE_MAX / count) return 0;
    size_t bytes = size * count;
    if (d->redirect) {
        if (bytes > 65536u - d->skipped || atomic_load(&d->u->cancel)) return 0;
        d->skipped += bytes; return bytes;
    }
    if (atomic_load(&d->u->cancel) || bytes > d->limit - d->used) return 0;
    if (d->file) {
        if (((d->used + bytes) >> 20) != (d->used >> 20) && !ptc_update_fs_space(&d->u->fs, bytes + 8u * 1024u * 1024u)) { d->u->error = PTC_UPDATE_ERROR_STORAGE; return 0; }
        if (fwrite(data, 1, bytes, d->file) != bytes) { d->u->error = PTC_UPDATE_ERROR_STORAGE; return 0; }
    }
    else { memcpy(d->text + d->used, data, bytes); d->text[d->used + bytes] = '\0'; }
    d->used += bytes; atomic_store(&d->u->progress, d->used);
    return bytes;
}

static int progress(void *ctx, curl_off_t total, curl_off_t now, curl_off_t upload_total, curl_off_t uploaded)
{
    PtcOnlineUpdater *u = ctx;
    (void)total; (void)now; (void)upload_total; (void)uploaded;
    return atomic_load(&u->cancel) ? 1 : 0;
}

static bool fetch(PtcOnlineUpdater *u, const char *initial, Download *d)
{
    char url[1024]; snprintf(url, sizeof(url), "%s", initial);
    CURL *curl = curl_easy_init(); if (!curl) return false;
    bool ok = false;
    u64 started = armGetSystemTick(); unsigned timeout = d->file ? 180 : 20;
    for (int redirects = 0; redirects <= 3 && ptc_update_url_allowed(url); ++redirects) {
        if (d->file && (d->used || fseek(d->file, 0, SEEK_SET))) break;
        d->used = d->headers = d->skipped = 0; d->redirect = false;
        uint64_t elapsed = armTicksToNs(armGetSystemTick() - started) / UINT64_C(1000000000);
        if (elapsed >= timeout) break;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)(timeout - elapsed));
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 15L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "NX-PlayWise/" PLAYWISE_VERSION);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, d);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, d);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, u);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        u->network_result = (unsigned long)curl_easy_perform(curl);
        long response = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
        if (u->network_result != CURLE_OK) break;
        if (response == 200) { ok = true; break; }
        char *redirect = NULL;
        if (response < 300 || response >= 400 || curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirect) != CURLE_OK ||
            !redirect || !ptc_update_url_allowed(redirect) || strlen(redirect) >= sizeof(url)) break;
        /* Never append a redirect response body to the package. */
        if (d->file && d->used) break;
        strcpy(url, redirect);
    }
    curl_easy_cleanup(curl); return ok;
}

static bool network_job(PtcOnlineUpdater *u)
{
    bool sockets = R_SUCCEEDED(socketInitializeDefault());
    bool nifm = sockets && R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
    bool curl = nifm && curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    bool ok = false;
    if (curl && u->job == 1) {
        char *text = calloc(1, PTC_UPDATE_METADATA_LIMIT + 1);
        Download d = {u, NULL, text, 0, PTC_UPDATE_METADATA_LIMIT, 0, 0, false};
        ok = text && fetch(u, PTC_UPDATE_REPO_URL, &d);
        if (ok) { ok = !memchr(text, '\0', d.used) && ptc_update_offer_parse(text, &u->offer); if (!ok) u->error = PTC_UPDATE_ERROR_PACKAGE; }
        if (ok) u->status = ptc_update_offer_status(&u->offer, PLAYWISE_VERSION, u->stable_only);
        free(text);
    } else if (curl && u->job == 2) {
        char relative[160], path[768];
        snprintf(relative, sizeof(relative), "switch/playwise/updates/%s/download.zip", u->journal.transaction_id);
        ok = ptc_update_fs_path(&u->fs, relative, path, sizeof(path)) && !u->fs.io.exists(&u->fs, relative) &&
            ptc_update_fs_space(&u->fs, 8u * 1024u * 1024u) && ptc_update_fs_mkdirs(path);
        FILE *file = ok ? fopen(path, "wb") : NULL;
        if (!file) u->error = PTC_UPDATE_ERROR_STORAGE;
        Download d = {u, file, NULL, 0, PTC_UPDATE_ZIP_LIMIT, 0, 0, false};
        ok = file && fetch(u, PTC_UPDATE_ZIP_URL, &d);
        if (file && fclose(file)) ok = false;
        if (ok) { ok = u->fs.io.commit(&u->fs) && ptc_online_update_package(u); if (!ok && !u->error) u->error = PTC_UPDATE_ERROR_PACKAGE; }
        if (ok) u->status = PTC_UPDATE_BACKUP_PENDING;
    }
    if (curl) curl_global_cleanup();
    if (nifm) nifmExit();
    if (sockets) socketExit();
    return ok;
}

static void worker(void *ctx)
{
    PtcOnlineUpdater *u = ctx;
    bool ok;
    if (u->job <= 2) ok = network_job(u);
    else if (u->job == 3) {
        ok = ptc_update_prepare(&u->fs.io, &u->journal) && ptc_update_install(&u->fs.io, &u->journal);
        if (ok) u->status = PTC_UPDATE_AWAITING_REOPEN;
        else {
            PtcUpdateJournal persisted;
            if (ptc_update_journal_load(&u->fs.io, &persisted) &&
                !strcmp(persisted.transaction_id, u->journal.transaction_id) && ptc_online_update_can_recover(u)) {
                u->journal = persisted;
                if (ptc_update_rollback(&u->fs.io, &u->journal)) {
                    u->error = PTC_UPDATE_ERROR_STORAGE; u->status = PTC_UPDATE_FAILED;
                    (void)ptc_online_update_record(u, "failed"); atomic_store(&u->done, true); return;
                }
            }
        }
    } else {
        ok = ptc_update_rollback(&u->fs.io, &u->journal);
        if (ok) u->status = PTC_UPDATE_CANCELED;
    }
    if (u->job <= 2 && atomic_load(&u->cancel)) ok = false;
    if (!ok) {
        if (!u->error) u->error = u->job <= 2 ? PTC_UPDATE_ERROR_NETWORK : PTC_UPDATE_ERROR_STORAGE;
        u->status = atomic_load(&u->cancel) ? PTC_UPDATE_CANCELED :
            (u->error == 2 ? PTC_UPDATE_MANUAL_REQUIRED : (u->job >= 3 ? PTC_UPDATE_RECOVERY_REQUIRED : PTC_UPDATE_FAILED));
    }
    if (u->status == PTC_UPDATE_CANCELED) u->error = PTC_UPDATE_ERROR_NONE;
    if (u->job >= 2) (void)ptc_online_update_record(u, u->status == PTC_UPDATE_BACKUP_PENDING ? "verified" :
        (u->status == PTC_UPDATE_AWAITING_REOPEN ? "installed" : (u->status == PTC_UPDATE_CANCELED ? "canceled" : "failed")));
    atomic_store_explicit(&u->done, true, memory_order_release);
}

bool ptc_online_update_record(PtcOnlineUpdater *u, const char *state)
{
    if (!ptc_update_hex(u->journal.transaction_id, 32)) return false;
    cJSON *o = cJSON_CreateObject(); if (!o) return false;
    cJSON_AddNumberToObject(o, "version", 1);
    cJSON_AddStringToObject(o, "transaction_id", u->journal.transaction_id);
    cJSON_AddStringToObject(o, "source_release_id", u->journal.source_release_id);
    cJSON_AddStringToObject(o, "request_id", u->journal.request_id);
    cJSON_AddStringToObject(o, "target_version", u->journal.target_version[0] ? u->journal.target_version : u->offer.version);
    cJSON_AddStringToObject(o, "state", state);
    cJSON_AddNumberToObject(o, "created_at", (double)u->journal.created_at);
    cJSON_AddNumberToObject(o, "error", u->error);
    cJSON_AddNumberToObject(o, "curl_result", (double)u->network_result);
    cJSON_AddNumberToObject(o, "io_errno", u->fs.last_errno);
    cJSON_AddNumberToObject(o, "io_result", u->fs.last_result);
    char path[160]; snprintf(path, sizeof(path), "switch/playwise/updates/%s/download.json", u->journal.transaction_id);
    char *text = cJSON_PrintUnformatted(o);
    bool ok = text && u->fs.io.write(&u->fs, path, text) && u->fs.io.commit(&u->fs);
    free(text); cJSON_Delete(o); return ok;
}

void ptc_online_update_init(PtcOnlineUpdater *u, PtcStorage *storage) { ptc_online_update_init_at(u, storage, "sdmc:"); }

void ptc_online_update_init_at(PtcOnlineUpdater *u, PtcStorage *storage, const char *root)
{
    memset(u, 0, sizeof(*u));
    atomic_init(&u->cancel, false); atomic_init(&u->done, false); atomic_init(&u->progress, 0);
    ptc_update_fs_init(&u->fs, root, storage);
    if (u->fs.io.exists(&u->fs, "switch/playwise/updates/active.json")) {
        if (!ptc_update_journal_load(&u->fs.io, &u->journal)) u->status = PTC_UPDATE_RECOVERY_REQUIRED;
        else if (!strcmp(u->journal.phase, "files_installed")) {
            u->status = ptc_update_files_match(&u->fs.io, &u->journal, true)
                ? (!strcmp(u->journal.target_release_id, PLAYWISE_BUILD_RELEASE_ID) ? PTC_UPDATE_BACKEND_PENDING : PTC_UPDATE_AWAITING_REOPEN)
                : PTC_UPDATE_RECOVERY_REQUIRED;
        } else if (ptc_update_install_busy_phase(u->journal.phase)) u->status = PTC_UPDATE_RECOVERY_REQUIRED;
    }
    if (u->fs.io.exists(&u->fs, "switch/playwise/updates/pending-backup.json")) {
        if (u->status == PTC_UPDATE_IDLE) u->status = ptc_online_update_pending_backup(u, false) ? PTC_UPDATE_BACKUP_PENDING : PTC_UPDATE_RECOVERY_REQUIRED;
        else u->status = PTC_UPDATE_RECOVERY_REQUIRED;
    }
    if (u->fs.io.exists(&u->fs, "switch/playwise/updates/install.lock")) {
        (void)ptc_update_journal_load(&u->fs.io, &u->journal);
        u->status = PTC_UPDATE_RECOVERY_REQUIRED;
    }
    char text[16384];
    if (u->fs.io.read(&u->fs, "switch/playwise/build.json", text, sizeof(text))) {
        cJSON *o = ptc_update_parse_json(text);
        const char *version = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "playwise_version"));
        if (version) snprintf(u->installed_version, sizeof(u->installed_version), "%s", version);
        cJSON_Delete(o);
    }
}

bool ptc_online_update_pending_backup(PtcOnlineUpdater *u, bool save)
{
    static const char *const path = "switch/playwise/updates/pending-backup.json";
    char text[8192];
    if (save) {
        if (u->fs.io.exists(&u->fs, path)) return false;
        cJSON *o = cJSON_CreateObject(); if (!o) return false;
        cJSON_AddNumberToObject(o, "version", 1);
        cJSON_AddStringToObject(o, "transaction_id", u->journal.transaction_id);
        cJSON_AddStringToObject(o, "request_id", u->journal.request_id);
        cJSON_AddStringToObject(o, "source_release_id", u->journal.source_release_id);
        cJSON_AddStringToObject(o, "target_release_id", u->journal.target_release_id);
        cJSON_AddStringToObject(o, "source_boot_id", u->journal.source_boot_id);
        cJSON_AddStringToObject(o, "target_version", u->journal.target_version);
        cJSON_AddStringToObject(o, "md5", u->journal.md5);
        cJSON_AddStringToObject(o, "zip_sha256", u->journal.zip_sha256);
        cJSON_AddNumberToObject(o, "source_pid", (double)u->journal.source_pid);
        cJSON_AddNumberToObject(o, "created_at", (double)u->journal.created_at);
        cJSON *files = cJSON_AddArrayToObject(o, "target_files");
        bool frozen = files != NULL;
        for (int i = 0; frozen && i < PTC_UPDATE_FILE_COUNT; ++i) {
            const PtcUpdateFile *f = &u->journal.files[i];
            cJSON *entry = cJSON_CreateObject();
            frozen = entry && ptc_update_hex(f->after_sha256, 64) && f->after_size <= PTC_UPDATE_ZIP_LIMIT &&
                cJSON_AddNumberToObject(entry, "size", (double)f->after_size) && cJSON_AddStringToObject(entry, "sha256", f->after_sha256);
            if (frozen) cJSON_AddItemToArray(files, entry); else cJSON_Delete(entry);
        }
        char *body = frozen ? cJSON_PrintUnformatted(o) : NULL;
        bool ok = body && u->fs.io.claim(&u->fs, path, body) && u->fs.io.commit(&u->fs) &&
            u->fs.io.read(&u->fs, path, text, sizeof(text)) && !strcmp(body, text);
        free(body); cJSON_Delete(o); return ok;
    }
    if (!u->fs.io.read(&u->fs, path, text, sizeof(text))) return false;
    cJSON *o = ptc_update_parse_json(text);
    struct { const char *name; char *out; size_t size; } fields[] = {
        {"transaction_id", u->journal.transaction_id, sizeof(u->journal.transaction_id)},
        {"request_id", u->journal.request_id, sizeof(u->journal.request_id)},
        {"source_release_id", u->journal.source_release_id, sizeof(u->journal.source_release_id)},
        {"target_release_id", u->journal.target_release_id, sizeof(u->journal.target_release_id)},
        {"source_boot_id", u->journal.source_boot_id, sizeof(u->journal.source_boot_id)},
        {"target_version", u->journal.target_version, sizeof(u->journal.target_version)},
        {"md5", u->journal.md5, sizeof(u->journal.md5)},
        {"zip_sha256", u->journal.zip_sha256, sizeof(u->journal.zip_sha256)}
    };
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, "version");
    bool ok = cJSON_IsNumber(v) && v->valuedouble == 1;
    for (size_t i = 0; ok && i < sizeof(fields) / sizeof(fields[0]); ++i) {
        const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, fields[i].name));
        ok = value && *value && strlen(value) < fields[i].size;
        if (ok) strcpy(fields[i].out, value);
    }
    const cJSON *pid = cJSON_GetObjectItemCaseSensitive(o, "source_pid"), *created = cJSON_GetObjectItemCaseSensitive(o, "created_at");
    int order;
    ok = ok && cJSON_IsNumber(pid) && pid->valuedouble > 0 && pid->valuedouble <= 9007199254740991.0 &&
        (double)(uint64_t)pid->valuedouble == pid->valuedouble && cJSON_IsNumber(created) && created->valuedouble >= 0 && created->valuedouble <= 9007199254740991.0 &&
        (double)(int64_t)created->valuedouble == created->valuedouble &&
        ptc_update_hex(u->journal.transaction_id, 32) && ptc_update_hex(u->journal.source_boot_id, 16) &&
        ptc_update_hex(u->journal.md5, 32) && ptc_update_hex(u->journal.zip_sha256, 64) &&
        ptc_update_version_compare(u->journal.target_version, u->journal.target_version, &order) && ptc_update_request_id(u->journal.request_id);
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(o, "target_files");
    ok = ok && cJSON_IsArray(files) && cJSON_GetArraySize(files) == PTC_UPDATE_FILE_COUNT;
    for (int i = 0; ok && i < PTC_UPDATE_FILE_COUNT; ++i) {
        const cJSON *entry = cJSON_GetArrayItem(files, i), *size = cJSON_GetObjectItemCaseSensitive(entry, "size");
        const char *digest = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "sha256"));
        ok = cJSON_IsNumber(size) && size->valuedouble >= 0 && size->valuedouble <= PTC_UPDATE_ZIP_LIMIT &&
            (double)(uint64_t)size->valuedouble == size->valuedouble && ptc_update_hex(digest, 64);
        if (ok) {
            memset(&u->journal.files[i], 0, sizeof(u->journal.files[i]));
            u->journal.files[i].after_size = (uint64_t)size->valuedouble;
            memcpy(u->journal.files[i].after_sha256, digest, 65);
        }
    }
    if (ok) {
        u->journal.source_pid = (uint64_t)pid->valuedouble; u->journal.created_at = (int64_t)created->valuedouble; u->backup_submitted = true;
        strcpy(u->offer.version, u->journal.target_version); strcpy(u->offer.md5, u->journal.md5);
    }
    cJSON_Delete(o); return ok;
}

bool ptc_online_update_start(PtcOnlineUpdater *u, int job)
{
    if (!u || u->worker_live || job < 1 || job > 4 ||
        (job <= 2 && (u->status == PTC_UPDATE_RECOVERY_REQUIRED || u->status == PTC_UPDATE_AWAITING_REOPEN ||
        u->status == PTC_UPDATE_BACKEND_PENDING || u->status == PTC_UPDATE_RECONFIRM))) return false;
    if (job == 2) {
        if (u->status != PTC_UPDATE_AVAILABLE) return false;
        memset(&u->journal, 0, sizeof(u->journal));
        if (!ptc_online_update_source(u)) { u->error = PTC_UPDATE_ERROR_CONFLICT; u->status = PTC_UPDATE_FAILED; return false; }
        char id[33]; snprintf(id, sizeof(id), "%016llx%016llx", (unsigned long long)randomGet64(), (unsigned long long)randomGet64());
        strcpy(u->journal.transaction_id, id);
        u->journal.created_at = (int64_t)time(NULL);
        char relative[128], directory[768];
        snprintf(relative, sizeof(relative), "switch/playwise/updates/%s", id);
        if (!ptc_update_fs_path(&u->fs, relative, directory, sizeof(directory)) || !ptc_update_fs_mkdirs(directory) ||
            mkdir(directory, 0777) || !u->fs.io.commit(&u->fs) || !ptc_online_update_record(u, "downloading")) {
            u->error = PTC_UPDATE_ERROR_STORAGE; u->status = PTC_UPDATE_FAILED; return false;
        }
    }
    u->job = job; u->error = 0; u->network_result = 0;
    if (job == 1) u->started = true;
    atomic_store(&u->cancel, false); atomic_store(&u->done, false); atomic_store(&u->progress, 0);
    u->status = job == 1 ? PTC_UPDATE_CHECKING : (job == 2 ? PTC_UPDATE_DOWNLOADING : PTC_UPDATE_INSTALLING);
    Result rc = threadCreate(&u->worker, worker, u, NULL, 128u * 1024u, 0x2c, -2);
    if (R_FAILED(rc)) { u->error = PTC_UPDATE_ERROR_STORAGE; u->status = PTC_UPDATE_FAILED; if (job >= 2) (void)ptc_online_update_record(u, "failed"); return false; }
    u->worker_live = true;
    rc = threadStart(&u->worker);
    if (R_FAILED(rc)) { threadClose(&u->worker); u->worker_live = false; u->status = PTC_UPDATE_FAILED; if (job >= 2) (void)ptc_online_update_record(u, "failed"); return false; }
    return true;
}

bool ptc_online_update_poll(PtcOnlineUpdater *u)
{
    if (!u->worker_live || !atomic_load_explicit(&u->done, memory_order_acquire)) return false;
    threadWaitForExit(&u->worker); threadClose(&u->worker); u->worker_live = false;
    if (u->job <= 2 && atomic_load(&u->cancel)) { u->status = PTC_UPDATE_CANCELED; u->error = PTC_UPDATE_ERROR_NONE; }
    return true;
}

void ptc_online_update_exit(PtcOnlineUpdater *u)
{
    if (u->worker_live) {
        if (u->job <= 2) atomic_store(&u->cancel, true);
        threadWaitForExit(&u->worker); threadClose(&u->worker); u->worker_live = false;
    }
}

static bool runtime(PtcOnlineUpdater *u, u64 pid, PtcHotReloadIdentity *identity)
{
    char text[2048];
    bool ok = u->fs.io.read(&u->fs, "switch/playwise/handover/runtime-ready.json", text, sizeof(text)) &&
        ptc_hot_reload_parse_identity(text, identity) && identity->pid == pid && !strcmp(identity->profile, "release") && identity->ipc_version == 2;
    cJSON *o = ok ? ptc_update_parse_json(text) : NULL;
    ok = ok && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "ipc_available"));
    cJSON_Delete(o); return ok;
}

bool ptc_online_update_source(PtcOnlineUpdater *u)
{
    static const char *const conflicts[] = {"switch/playwise/recovery/active/meta.json", "switch/playwise/handover/reload.json",
        "switch/playwise/handover/reload.json.tmp", "switch/playwise/handover/intent.json", "switch/playwise/handover/ready.json",
        "atmosphere/contents/4200000000BD2300/flags/boot2.flag.playwise-hot-reload-backup"};
    if (!ptc_online_update_verify_current(u)) return false;
    for (size_t i = 0; i < sizeof(conflicts) / sizeof(conflicts[0]); ++i) if (u->fs.io.exists(&u->fs, conflicts[i])) return false;
    char names[1][128]; size_t count;
    const char *queues[] = {PLAYWISE_RELEASE_SD_ROOT "/inbox/pending", PLAYWISE_RELEASE_SD_ROOT "/inbox/processing"};
    for (size_t i = 0; i < 2; ++i) if (!u->fs.storage->vtable->list_json(u->fs.storage, queues[i], names, 1, &count) || count) return false;
    if (R_FAILED(pmshellInitialize())) return false;
    u64 pid; PtcHotReloadIdentity identity;
    bool ok = R_SUCCEEDED(pmshellGetProcessId(&pid, UINT64_C(0x4200000000BD2300))) && runtime(u, pid, &identity) &&
        !strcmp(identity.release_id, PLAYWISE_BUILD_RELEASE_ID);
    pmshellExit();
    if (ok) {
        u->journal.source_pid = pid;
        snprintf(u->journal.source_release_id, sizeof(u->journal.source_release_id), "%s", identity.release_id);
        snprintf(u->journal.source_boot_id, sizeof(u->journal.source_boot_id), "%s", identity.boot_id);
    }
    return ok;
}

bool ptc_online_update_can_recover(PtcOnlineUpdater *u)
{
    if (!ptc_update_hex(u->journal.transaction_id, 32) || R_FAILED(pmshellInitialize())) return false;
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
        u64 pid; Result rc = pmshellGetProcessId(&pid, UINT64_C(0x4200000000BD2300));
        if (R_SUCCEEDED(rc)) {
            PtcHotReloadIdentity identity;
            ok = runtime(u, pid, &identity) && pid == u->journal.source_pid &&
                !strcmp(identity.boot_id, u->journal.source_boot_id) && !strcmp(identity.release_id, u->journal.source_release_id);
            break;
        }
        if (!(R_MODULE(rc) == 15 && R_DESCRIPTION(rc) == 1)) { ok = false; break; }
        if (i < 2) svcSleepThread(100000000LL);
    }
    pmshellExit(); return ok;
}

bool ptc_online_update_complete(PtcOnlineUpdater *u, const char *request_id, const char *receipt)
{
    if (!ptc_update_request_id(request_id) || !receipt || strlen(receipt) >= 8192) return false;
    cJSON *o = ptc_update_parse_json(receipt);
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "request_id"));
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "type"));
    const char *status = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "status"));
    bool valid = id && type && status && !strcmp(id, request_id) && !strcmp(type, "status") && !strcmp(status, "ok");
    cJSON_Delete(o); if (!valid) return false;
    if (strcmp(u->journal.phase, "files_installed") || strcmp(u->journal.target_release_id, PLAYWISE_BUILD_RELEASE_ID) ||
        !ptc_update_files_match(&u->fs.io, &u->journal, true)) return false;
    if (R_FAILED(pmshellInitialize())) return false;
    u64 pid; PtcHotReloadIdentity identity;
    bool ready = R_SUCCEEDED(pmshellGetProcessId(&pid, UINT64_C(0x4200000000BD2300))) && runtime(u, pid, &identity) &&
        !strcmp(identity.release_id, u->journal.target_release_id) && strcmp(identity.boot_id, u->journal.source_boot_id);
    pmshellExit(); if (!ready) return false;
    u->journal.target_pid = pid;
    snprintf(u->journal.target_boot_id, sizeof(u->journal.target_boot_id), "%s", identity.boot_id);
    snprintf(u->journal.status_request_id, sizeof(u->journal.status_request_id), "%s", request_id);
    PtcSha256Ctx sha; uint8_t digest[32];
    ptc_sha256_init(&sha); ptc_sha256_update(&sha, (const uint8_t *)receipt, strlen(receipt)); ptc_sha256_final(&sha, digest);
    for (unsigned i = 0; i < 32; ++i) snprintf(u->journal.status_sha256 + i * 2, 3, "%02x", digest[i]);
    char path[256], actual[65]; uint64_t size;
    snprintf(path, sizeof(path), "switch/playwise/updates/%s/receipts/%s.json", u->journal.transaction_id, request_id);
    if (u->fs.io.exists(&u->fs, path)) {
        if (!u->fs.io.hash(&u->fs, path, &size, actual) || size != strlen(receipt) || strcmp(actual, u->journal.status_sha256)) return false;
    } else if (!u->fs.io.write(&u->fs, path, receipt) || !u->fs.io.commit(&u->fs)) return false;
    if (!u->fs.io.hash(&u->fs, path, &size, actual) || size != strlen(receipt) || strcmp(actual, u->journal.status_sha256)) return false;
    strcpy(u->journal.phase, "completed");
    if (!ptc_update_journal_save(&u->fs.io, &u->journal)) { strcpy(u->journal.phase, "files_installed"); return false; }
    u->error = PTC_UPDATE_ERROR_NONE; u->status = PTC_UPDATE_COMPLETED; return true;
}
