#ifndef PLAYWISE_HOT_RELOAD_GUARD_H
#define PLAYWISE_HOT_RELOAD_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *boot_flag;
    const char *boot_flag_backup;
    const char *journal;
} PtcHotReloadPaths;

typedef struct {
    char transaction_id[48];
    char phase[32];
    char source_release_id[96];
    char target_release_id[96];
    uint64_t source_pid;
    uint64_t target_pid;
} PtcHotReloadJournal;

typedef struct {
    char profile[32];
    char release_id[96];
    char boot_id[32];
    uint64_t pid;
    unsigned int ipc_version;
} PtcHotReloadIdentity;

typedef enum {
    PTC_HOT_RELOAD_FLAG_OK = 0,
    PTC_HOT_RELOAD_FLAG_CONFLICT = 1,
    PTC_HOT_RELOAD_FLAG_IO_ERROR = 2
} PtcHotReloadFlagResult;

bool ptc_hot_reload_parse_identity(const char *json, PtcHotReloadIdentity *identity);
bool ptc_hot_reload_identity_matches(const PtcHotReloadIdentity *identity,
    uint64_t pid, const char *profile, const char *release_id);
bool ptc_hot_reload_read_journal(const PtcHotReloadPaths *paths, PtcHotReloadJournal *journal);
bool ptc_hot_reload_write_journal(const PtcHotReloadPaths *paths, const PtcHotReloadJournal *journal);
PtcHotReloadFlagResult ptc_hot_reload_disable_boot(
    const PtcHotReloadPaths *paths, PtcHotReloadJournal *journal);
PtcHotReloadFlagResult ptc_hot_reload_restore_boot(
    const PtcHotReloadPaths *paths, PtcHotReloadJournal *journal);
bool ptc_hot_reload_finish_journal(const PtcHotReloadPaths *paths);

#endif
