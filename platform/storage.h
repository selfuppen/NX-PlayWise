#ifndef PTC_PLATFORM_STORAGE_H
#define PTC_PLATFORM_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct PtcStorage PtcStorage;

typedef enum {
    PTC_STORAGE_ENTRY_UNKNOWN = 0,
    PTC_STORAGE_ENTRY_FILE = 1,
    PTC_STORAGE_ENTRY_DIRECTORY = 2,
} PtcStorageEntryType;

typedef struct {
    char name[128];
    PtcStorageEntryType type;
    int64_t modified_unix_seconds;
    bool modified_time_valid;
} PtcStorageEntry;

typedef struct {
    PtcStorageEntryType type;
    int64_t modified_unix_seconds;
    bool modified_time_valid;
} PtcStorageMetadata;

typedef struct {
    bool (*read_text)(PtcStorage *storage, const char *path, char *out, size_t out_size);
    bool (*write_text_atomic)(PtcStorage *storage, const char *path, const char *text);
    bool (*append_line)(PtcStorage *storage, const char *path, const char *line);
    bool (*rename_path)(PtcStorage *storage, const char *from, const char *to);
    bool (*remove_path)(PtcStorage *storage, const char *path);
    bool (*exists)(PtcStorage *storage, const char *path);
    bool (*list_json)(PtcStorage *storage, const char *dir, char names[][128], size_t max, size_t *count);
    bool (*metadata)(PtcStorage *storage, const char *path, PtcStorageMetadata *out);
    bool (*list_entries)(PtcStorage *storage, const char *dir, PtcStorageEntry *entries, size_t max, size_t *count);
    bool (*remove_tree)(PtcStorage *storage, const char *path);
} PtcStorageVTable;

struct PtcStorage {
    const PtcStorageVTable *vtable;
    void *ctx;
};

#endif
