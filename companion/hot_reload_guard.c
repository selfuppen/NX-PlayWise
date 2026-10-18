#include "hot_reload_guard.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../common/crypto/sha256.h"
#include "../third_party/cjson/cJSON.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

static bool path_exists(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0;
}

static bool empty_file(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0 && info.st_size == 0;
}

static void io_error(const PtcHotReloadPaths *paths, const char *operation, int err, uint32_t result)
{
    if (!paths || !paths->error) return;
    snprintf(paths->error->operation, sizeof(paths->error->operation), "%s", operation);
    paths->error->system_errno = err;
    paths->error->result = result;
}

static bool commit_storage(const PtcHotReloadPaths *paths)
{
    uint32_t result = 0;
    if (paths->io && paths->io->commit) result = paths->io->commit(paths->io->ctx);
#ifdef __SWITCH__
    else result = fsdevCommitDevice("sdmc");
#endif
    if (result) io_error(paths, "commit", 0, result);
    return result == 0;
}

static int mutate_path(const PtcHotReloadPaths *paths, const char *from, const char *to)
{
    unsigned int attempt;
    for (attempt = 0; attempt < 51U; ++attempt) {
        int rc;
        if (to) rc = paths->io && paths->io->rename_path
            ? paths->io->rename_path(paths->io->ctx, from, to) : rename(from, to);
        else rc = paths->io && paths->io->remove_path
            ? paths->io->remove_path(paths->io->ctx, from) : remove(from);
        if (rc == 0) return 0;
        int err = errno;
        io_error(paths, to ? "rename" : "remove", err, 0);
        /* FAT readers may briefly hold the old generation. Keep both journal
           generations on failure; never treat a sharing error as absence. */
        if ((err != EBUSY && err != EACCES && err != EAGAIN) || attempt == 50U) return -1;
        if (paths->error) ++paths->error->retries;
        if (paths->io && paths->io->wait_ms) paths->io->wait_ms(paths->io->ctx, 10);
#ifdef __SWITCH__
        else svcSleepThread(10000000LL);
#endif
    }
    return -1;
}

static bool json_string(const char *json, const char *key, char *out, size_t out_size)
{
    char pattern[64];
    const char *value;
    const char *end;
    size_t length;
    if (!json || !key || !out || out_size == 0) return false;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    value = strstr(json, pattern);
    if (!value || !(value = strchr(value, ':'))) return false;
    do { ++value; } while (*value == ' ' || *value == '\t');
    if (*value++ != '"' || !(end = strchr(value, '"'))) return false;
    length = (size_t)(end - value);
    if (length == 0 || length >= out_size) return false;
    memcpy(out, value, length);
    out[length] = '\0';
    return true;
}

static bool json_u64(const char *json, const char *key, uint64_t *out)
{
    char pattern[64];
    const char *value;
    char *end;
    unsigned long long parsed;
    if (!json || !key || !out) return false;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    value = strstr(json, pattern);
    if (!value || !(value = strchr(value, ':'))) return false;
    do { ++value; } while (*value == ' ' || *value == '\t');
    if (*value < '0' || *value > '9') return false;
    parsed = strtoull(value, &end, 10);
    if (end == value || (*end != ',' && *end != '}' && *end != ' ' && *end != '\t' &&
        *end != '\r' && *end != '\n')) return false;
    *out = (uint64_t)parsed;
    return true;
}

static bool unique_object(const cJSON *root, int size)
{
    if (!cJSON_IsObject(root) || (size >= 0 && cJSON_GetArraySize(root) != size)) return false;
    for (const cJSON *item = root->child; item; item = item->next) {
        if (!item->string) return false;
        for (const cJSON *prior = root->child; prior != item; prior = prior->next)
            if (strcmp(prior->string, item->string) == 0) return false;
    }
    return true;
}

static const char *string_field(const cJSON *root, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool hex_field(const char *text, size_t length)
{
    return text && strlen(text) == length && strspn(text, "0123456789abcdef") == length;
}

static bool equals_field(const cJSON *root, const char *key, const char *expected)
{
    const char *text = string_field(root, key);
    return text && expected && strcmp(text, expected) == 0;
}

bool ptc_hot_reload_capture_binding_equal(const PtcCaptureRestartBinding *a,
    const PtcCaptureRestartBinding *b)
{
    return a && b && strcmp(a->request_id, b->request_id) == 0 &&
        strcmp(a->run_id, b->run_id) == 0 && strcmp(a->config_sha256, b->config_sha256) == 0;
}

bool ptc_hot_reload_capture_request(const char *request_json, const char *config_json,
    const char *commit, const char *release_id, const char *source_boot_id,
    PtcCaptureRestartBinding *binding)
{
    if (!request_json || !config_json || !binding || !hex_field(commit, 40) ||
        !hex_field(source_boot_id, 16)) return false;
    memset(binding, 0, sizeof(*binding));
    cJSON *request = cJSON_ParseWithOpts(request_json, NULL, true);
    cJSON *config = cJSON_ParseWithOpts(config_json, NULL, true);
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(request, "schema_version");
    const cJSON *cv = cJSON_GetObjectItemCaseSensitive(config, "schema_version");
    const cJSON *duration = cJSON_GetObjectItemCaseSensitive(config, "duration_s");
    const char *id = string_field(request, "request_id");
    const char *run = string_field(request, "run_id");
    const char *hash = string_field(request, "config_sha256");
    bool valid = unique_object(request, 7) && unique_object(config, 5) &&
        cJSON_IsNumber(version) && version->valuedouble == 1 &&
        cJSON_IsNumber(cv) && cv->valuedouble == 1 &&
        equals_field(request, "purpose", "usage_evidence") &&
        equals_field(request, "release_id", release_id) &&
        equals_field(request, "source_boot_id", source_boot_id) &&
        hex_field(id, 32) && hex_field(run, 32) && hex_field(hash, 64) &&
        equals_field(config, "commit", commit) && equals_field(config, "release_id", release_id) &&
        equals_field(config, "run_id", run) && cJSON_IsNumber(duration) &&
        duration->valuedouble == duration->valueint && duration->valueint >= 300 && duration->valueint <= 7200;
    if (valid) {
        PtcSha256Ctx ctx;
        uint8_t digest[PTC_SHA256_DIGEST_SIZE];
        char actual[65];
        ptc_sha256_init(&ctx);
        ptc_sha256_update(&ctx, (const uint8_t *)config_json, strlen(config_json));
        ptc_sha256_final(&ctx, digest);
        for (size_t i = 0; i < sizeof(digest); ++i) snprintf(actual + i * 2, 3, "%02x", digest[i]);
        valid = strcmp(actual, hash) == 0;
        if (valid) {
            snprintf(binding->request_id, sizeof(binding->request_id), "%s", id);
            snprintf(binding->run_id, sizeof(binding->run_id), "%s", run);
            snprintf(binding->config_sha256, sizeof(binding->config_sha256), "%s", hash);
        }
    }
    cJSON_Delete(request);
    cJSON_Delete(config);
    return valid;
}

bool ptc_hot_reload_parse_identity(const char *json, PtcHotReloadIdentity *identity)
{
    uint64_t ipc_version;
    if (!identity) return false;
    memset(identity, 0, sizeof(*identity));
    if (!json_string(json, "profile", identity->profile, sizeof(identity->profile)) ||
        !json_string(json, "release_id", identity->release_id, sizeof(identity->release_id)) ||
        !json_string(json, "boot_id", identity->boot_id, sizeof(identity->boot_id)) ||
        !json_u64(json, "pid", &identity->pid) ||
        !json_u64(json, "ipc_version", &ipc_version) || ipc_version > UINT32_MAX) return false;
    identity->ipc_version = (unsigned int)ipc_version;
    return true;
}

bool ptc_hot_reload_identity_matches(const PtcHotReloadIdentity *identity,
    uint64_t pid, const char *profile, const char *release_id)
{
    return identity && profile && release_id && identity->pid == pid &&
        strcmp(identity->profile, profile) == 0 && strcmp(identity->release_id, release_id) == 0 &&
        identity->boot_id[0] != '\0';
}

bool ptc_hot_reload_write_journal(const PtcHotReloadPaths *paths, const PtcHotReloadJournal *journal)
{
    if (paths && paths->error) memset(paths->error, 0, sizeof(*paths->error));
    char temporary[384];
    FILE *file;
    bool wrote;
    if (!paths || !journal || !paths->journal) return false;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", paths->journal) <= 0 || path_exists(temporary)) return false;
    file = fopen(temporary, "wb");
    if (!file) { io_error(paths, "open_journal", errno, 0); return false; }
    wrote = fprintf(file,
            "{\"version\":1,\"transaction_id\":\"%s\",\"phase\":\"%s\","
            "\"source_release_id\":\"%s\",\"target_release_id\":\"%s\","
            "\"source_boot_id\":\"%s\","
            "\"source_pid\":%llu,\"target_pid\":%llu",
            journal->transaction_id, journal->phase, journal->source_release_id,
            journal->target_release_id, journal->source_boot_id, (unsigned long long)journal->source_pid,
            (unsigned long long)journal->target_pid) >= 0;
    if (wrote && journal->capture.request_id[0])
        wrote = fprintf(file, ",\"capture_request_id\":\"%s\",\"capture_run_id\":\"%s\",\"capture_config_sha256\":\"%s\"",
            journal->capture.request_id, journal->capture.run_id, journal->capture.config_sha256) >= 0;
    if (wrote) wrote = fputs("}\n", file) >= 0;
    if (fclose(file) != 0) wrote = false;
    if (!wrote) {
        io_error(paths, "write_journal", errno, 0);
        (void)mutate_path(paths, temporary, NULL);
        return false;
    }
    if (!commit_storage(paths)) return false;
    if (path_exists(paths->journal) && mutate_path(paths, paths->journal, NULL) != 0) return false;
    if (!commit_storage(paths)) return false;
    if (mutate_path(paths, temporary, paths->journal) != 0) return false;
    if (!commit_storage(paths)) return false;
    return true;
}

static bool parse_journal_path(const char *path, PtcHotReloadJournal *journal)
{
    char text[1536];
    uint64_t version;
    uint64_t source_pid;
    uint64_t target_pid;
    FILE *file = fopen(path, "rb");
    size_t got;
    if (!file || !journal) return false;
    got = fread(text, 1, sizeof(text) - 1U, file);
    if (fclose(file) != 0 || got == 0 || got >= sizeof(text) - 1U) return false;
    text[got] = '\0';
    memset(journal, 0, sizeof(*journal));
    if (!json_u64(text, "version", &version) || version != 1 ||
        !json_string(text, "transaction_id", journal->transaction_id, sizeof(journal->transaction_id)) ||
        !json_string(text, "phase", journal->phase, sizeof(journal->phase)) ||
        !json_string(text, "source_release_id", journal->source_release_id, sizeof(journal->source_release_id)) ||
        !json_string(text, "target_release_id", journal->target_release_id, sizeof(journal->target_release_id)) ||
        !json_u64(text, "source_pid", &source_pid) || !json_u64(text, "target_pid", &target_pid)) return false;
    if (strcmp(journal->phase, "prepared") != 0 && strcmp(journal->phase, "boot_disabled") != 0 &&
        strcmp(journal->phase, "boot_restored") != 0 && strcmp(journal->phase, "target_launched") != 0) return false;
    journal->source_pid = source_pid;
    journal->target_pid = target_pid;
    /* Optional in existing v1 journals; new writes bind the source instance. */
    (void)json_string(text, "source_boot_id", journal->source_boot_id, sizeof(journal->source_boot_id));
    cJSON *root = cJSON_ParseWithOpts(text, NULL, true);
    const char *id = string_field(root, "capture_request_id");
    bool valid = unique_object(root, -1);
    if (cJSON_HasObjectItem(root, "capture_request_id") ||
        cJSON_HasObjectItem(root, "capture_run_id") || cJSON_HasObjectItem(root, "capture_config_sha256")) {
        const char *run = string_field(root, "capture_run_id");
        const char *hash = string_field(root, "capture_config_sha256");
        valid = valid && hex_field(id, 32) && hex_field(run, 32) && hex_field(hash, 64) &&
            hex_field(journal->source_boot_id, 16) &&
            strcmp(journal->source_release_id, journal->target_release_id) == 0;
        if (valid) {
            snprintf(journal->capture.request_id, sizeof(journal->capture.request_id), "%s", id);
            snprintf(journal->capture.run_id, sizeof(journal->capture.run_id), "%s", run);
            snprintf(journal->capture.config_sha256, sizeof(journal->capture.config_sha256), "%s", hash);
        }
    } else if (strcmp(journal->source_release_id, journal->target_release_id) == 0) valid = false;
    cJSON_Delete(root);
    if (!valid) return false;
    return true;
}

bool ptc_hot_reload_read_journal(const PtcHotReloadPaths *paths, PtcHotReloadJournal *journal)
{
    if (paths && paths->error) memset(paths->error, 0, sizeof(*paths->error));
    char temporary[384];
    PtcHotReloadJournal pending;
    bool current_valid;
    bool pending_valid;
    if (!paths || !paths->journal || !journal ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", paths->journal) <= 0) return false;
    current_valid = path_exists(paths->journal) && parse_journal_path(paths->journal, journal);
    if (!path_exists(temporary)) return current_valid;
    pending_valid = parse_journal_path(temporary, &pending);
    if (!pending_valid) {
        if (!current_valid || mutate_path(paths, temporary, NULL) != 0) return false;
        if (!commit_storage(paths)) return false;
        return true;
    }
    if (current_valid && (strcmp(journal->transaction_id, pending.transaction_id) != 0 ||
        strcmp(journal->source_release_id, pending.source_release_id) != 0 ||
        strcmp(journal->target_release_id, pending.target_release_id) != 0 ||
        strcmp(journal->source_boot_id, pending.source_boot_id) != 0 ||
        !ptc_hot_reload_capture_binding_equal(&journal->capture, &pending.capture) ||
        journal->source_pid != pending.source_pid)) return false;
    if (path_exists(paths->journal) && mutate_path(paths, paths->journal, NULL) != 0) return false;
    if (!commit_storage(paths)) return false;
    if (mutate_path(paths, temporary, paths->journal) != 0) return false;
    if (!commit_storage(paths)) return false;
    *journal = pending;
    return true;
}

PtcHotReloadFlagResult ptc_hot_reload_disable_boot(
    const PtcHotReloadPaths *paths, PtcHotReloadJournal *journal)
{
    PtcHotReloadJournal existing;
    if (!paths || !journal || !paths->boot_flag || !paths->boot_flag_backup || !paths->journal ||
        !empty_file(paths->boot_flag) || path_exists(paths->boot_flag_backup))
        return PTC_HOT_RELOAD_FLAG_CONFLICT;
    snprintf(journal->phase, sizeof(journal->phase), "prepared");
    if (path_exists(paths->journal)) {
        if (!ptc_hot_reload_read_journal(paths, &existing) || strcmp(existing.phase, "prepared") != 0 ||
            strcmp(existing.transaction_id, journal->transaction_id) != 0 ||
            strcmp(existing.source_release_id, journal->source_release_id) != 0 ||
            strcmp(existing.target_release_id, journal->target_release_id) != 0 ||
            strcmp(existing.source_boot_id, journal->source_boot_id) != 0 ||
            !ptc_hot_reload_capture_binding_equal(&existing.capture, &journal->capture) ||
            existing.source_pid != journal->source_pid) return PTC_HOT_RELOAD_FLAG_CONFLICT;
    } else if (!ptc_hot_reload_write_journal(paths, journal)) {
        return PTC_HOT_RELOAD_FLAG_IO_ERROR;
    }
    if (mutate_path(paths, paths->boot_flag, paths->boot_flag_backup) != 0) return PTC_HOT_RELOAD_FLAG_IO_ERROR;
    if (!commit_storage(paths)) return PTC_HOT_RELOAD_FLAG_IO_ERROR;
    snprintf(journal->phase, sizeof(journal->phase), "boot_disabled");
    if (ptc_hot_reload_write_journal(paths, journal)) return PTC_HOT_RELOAD_FLAG_OK;
    if (mutate_path(paths, paths->boot_flag_backup, paths->boot_flag) == 0)
        (void)commit_storage(paths);
    return PTC_HOT_RELOAD_FLAG_IO_ERROR;
}

PtcHotReloadFlagResult ptc_hot_reload_restore_boot(
    const PtcHotReloadPaths *paths, PtcHotReloadJournal *journal)
{
    if (!paths || !journal || !paths->boot_flag || !paths->boot_flag_backup || !paths->journal)
        return PTC_HOT_RELOAD_FLAG_IO_ERROR;
    if (path_exists(paths->boot_flag) && path_exists(paths->boot_flag_backup)) return PTC_HOT_RELOAD_FLAG_CONFLICT;
    if (!path_exists(paths->boot_flag)) {
        if (!empty_file(paths->boot_flag_backup) || mutate_path(paths, paths->boot_flag_backup, paths->boot_flag) != 0)
            return PTC_HOT_RELOAD_FLAG_CONFLICT;
        if (!commit_storage(paths)) return PTC_HOT_RELOAD_FLAG_IO_ERROR;
    } else if (!empty_file(paths->boot_flag) || path_exists(paths->boot_flag_backup)) {
        return PTC_HOT_RELOAD_FLAG_CONFLICT;
    }
    snprintf(journal->phase, sizeof(journal->phase), "boot_restored");
    return ptc_hot_reload_write_journal(paths, journal) ? PTC_HOT_RELOAD_FLAG_OK : PTC_HOT_RELOAD_FLAG_IO_ERROR;
}

bool ptc_hot_reload_finish_journal(const PtcHotReloadPaths *paths)
{
    char temporary[384];
    bool ok = true;
    if (!paths || !paths->journal || !paths->boot_flag_backup ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", paths->journal) <= 0 ||
        path_exists(paths->boot_flag_backup)) return false;
    if (path_exists(paths->journal) && mutate_path(paths, paths->journal, NULL) != 0) ok = false;
    if (path_exists(temporary) && mutate_path(paths, temporary, NULL) != 0) ok = false;
    if (!commit_storage(paths)) return false;
    return ok;
}
