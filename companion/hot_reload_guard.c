#include "hot_reload_guard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static void commit_storage(void)
{
#ifdef __SWITCH__
    (void)fsdevCommitDevice("sdmc");
#endif
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
    char temporary[384];
    FILE *file;
    bool wrote;
    if (!paths || !journal || !paths->journal) return false;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", paths->journal) <= 0 || path_exists(temporary)) return false;
    file = fopen(temporary, "wb");
    if (!file) return false;
    wrote = fprintf(file,
            "{\"version\":1,\"transaction_id\":\"%s\",\"phase\":\"%s\","
            "\"source_release_id\":\"%s\",\"target_release_id\":\"%s\","
            "\"source_pid\":%llu,\"target_pid\":%llu}\n",
            journal->transaction_id, journal->phase, journal->source_release_id,
            journal->target_release_id, (unsigned long long)journal->source_pid,
            (unsigned long long)journal->target_pid) >= 0;
    if (fclose(file) != 0) wrote = false;
    if (!wrote) {
        (void)remove(temporary);
        return false;
    }
    commit_storage();
    if (path_exists(paths->journal) && remove(paths->journal) != 0) return false;
    commit_storage();
    if (rename(temporary, paths->journal) != 0) return false;
    commit_storage();
    return true;
}

static bool parse_journal_path(const char *path, PtcHotReloadJournal *journal)
{
    char text[1024];
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
    return true;
}

bool ptc_hot_reload_read_journal(const PtcHotReloadPaths *paths, PtcHotReloadJournal *journal)
{
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
        if (!current_valid || remove(temporary) != 0) return false;
        commit_storage();
        return true;
    }
    if (current_valid && strcmp(journal->transaction_id, pending.transaction_id) != 0) return false;
    if (path_exists(paths->journal) && remove(paths->journal) != 0) return false;
    commit_storage();
    if (rename(temporary, paths->journal) != 0) return false;
    commit_storage();
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
            existing.source_pid != journal->source_pid) return PTC_HOT_RELOAD_FLAG_CONFLICT;
    } else if (!ptc_hot_reload_write_journal(paths, journal)) {
        return PTC_HOT_RELOAD_FLAG_IO_ERROR;
    }
    if (rename(paths->boot_flag, paths->boot_flag_backup) != 0) return PTC_HOT_RELOAD_FLAG_IO_ERROR;
    commit_storage();
    snprintf(journal->phase, sizeof(journal->phase), "boot_disabled");
    if (ptc_hot_reload_write_journal(paths, journal)) return PTC_HOT_RELOAD_FLAG_OK;
    if (rename(paths->boot_flag_backup, paths->boot_flag) == 0) commit_storage();
    return PTC_HOT_RELOAD_FLAG_IO_ERROR;
}

PtcHotReloadFlagResult ptc_hot_reload_restore_boot(
    const PtcHotReloadPaths *paths, PtcHotReloadJournal *journal)
{
    if (!paths || !journal || !paths->boot_flag || !paths->boot_flag_backup || !paths->journal)
        return PTC_HOT_RELOAD_FLAG_IO_ERROR;
    if (path_exists(paths->boot_flag) && path_exists(paths->boot_flag_backup)) return PTC_HOT_RELOAD_FLAG_CONFLICT;
    if (!path_exists(paths->boot_flag)) {
        if (!empty_file(paths->boot_flag_backup) || rename(paths->boot_flag_backup, paths->boot_flag) != 0)
            return PTC_HOT_RELOAD_FLAG_CONFLICT;
        commit_storage();
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
    if (path_exists(paths->journal) && remove(paths->journal) != 0) ok = false;
    if (path_exists(temporary) && remove(temporary) != 0) ok = false;
    commit_storage();
    return ok;
}
