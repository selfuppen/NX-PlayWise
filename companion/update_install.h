#ifndef PTC_UPDATE_INSTALL_H
#define PTC_UPDATE_INSTALL_H

#include "../common/update/update.h"

typedef struct {
    void *ctx;
    bool (*hash)(void *ctx, const char *path, uint64_t *size, char sha256[65]);
    bool (*exists)(void *ctx, const char *path);
    bool (*copy)(void *ctx, const char *source, const char *target);
    bool (*publish)(void *ctx, const char *source, const char *target, const char *expected_hash, bool allow_missing);
    bool (*remove)(void *ctx, const char *path);
    bool (*read)(void *ctx, const char *path, char *out, size_t size);
    bool (*write)(void *ctx, const char *path, const char *text);
    bool (*commit)(void *ctx);
    bool (*discard_temporary)(void *ctx, const char *path, const char *before_hash, const char *after_hash);
    bool (*claim)(void *ctx, const char *path, const char *text);
} PtcUpdateIo;

typedef struct {
    bool existed;
    uint64_t before_size, after_size;
    char before_sha256[65], after_sha256[65];
} PtcUpdateFile;

typedef struct {
    char transaction_id[33];
    char source_release_id[96], target_release_id[96], target_version[64];
    char source_boot_id[32], request_id[80], md5[33], zip_sha256[65];
    char config_sha256[65];
    uint64_t config_size;
    uint64_t source_pid;
    uint64_t target_pid;
    char target_boot_id[32], status_request_id[80], status_sha256[65];
    int64_t created_at;
    char phase[32];
    unsigned next_file;
    PtcUpdateFile files[PTC_UPDATE_FILE_COUNT];
} PtcUpdateJournal;

bool ptc_update_journal_parse(const char *text, PtcUpdateJournal *journal);
bool ptc_update_journal_save(const PtcUpdateIo *io, const PtcUpdateJournal *journal);
bool ptc_update_journal_load(const PtcUpdateIo *io, PtcUpdateJournal *journal);
bool ptc_update_install_busy_phase(const char *phase);
bool ptc_update_path(char *out, size_t size, const PtcUpdateJournal *journal, const char *part, int file);
/* Caller freezes target after-digests during package validation. They are
   checked, never refreshed from conflicting stage bytes. Backups precede publish. */
bool ptc_update_prepare(const PtcUpdateIo *io, PtcUpdateJournal *journal);
bool ptc_update_install(const PtcUpdateIo *io, PtcUpdateJournal *journal);
/* Caller must first prove no target process is running. Never restores live data. */
bool ptc_update_rollback(const PtcUpdateIo *io, PtcUpdateJournal *journal);
bool ptc_update_files_match(const PtcUpdateIo *io, const PtcUpdateJournal *journal, bool target);

#endif
