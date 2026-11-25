#ifndef PTC_PLATFORM_STORAGE_H
#define PTC_PLATFORM_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct PtcStorage PtcStorage;

typedef enum {
    PTC_STORAGE_APPEND_NONE = 0,
    PTC_STORAGE_APPEND_OPEN,
    PTC_STORAGE_APPEND_WRITE,
    PTC_STORAGE_APPEND_CLOSE,
} PtcStorageAppendStage;

/* Optional diagnostics; errno/result never determine storage success. */
typedef struct {
    PtcStorageAppendStage stage;
    int system_errno;
    uint32_t platform_result;
    unsigned open_attempts;
} PtcStorageAppendDiagnostics;
typedef PtcStorageAppendDiagnostics (*PtcStorageAppendDiagnosticsReader)(void *ctx);

typedef enum {
    PTC_STORAGE_ENTRY_UNKNOWN = 0,
    PTC_STORAGE_ENTRY_FILE = 1,
    PTC_STORAGE_ENTRY_DIRECTORY = 2,
    PTC_STORAGE_ENTRY_MISSING = 3,
} PtcStorageEntryType;

typedef enum {
    PTC_STORAGE_READ_FAILED = 0,
    PTC_STORAGE_READ_OK,
    PTC_STORAGE_READ_MISSING,
} PtcStorageReadResult;

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

typedef bool (*PtcStorageLineVisitor)(const char *line, void *ctx);

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
    /* Reject oversized lines and read errors; visit the entire file. */
    bool (*read_lines)(PtcStorage *storage, const char *path, char *line, size_t line_size,
                       PtcStorageLineVisitor visit, void *ctx);
    /* Optional replace for expiring hints that can be lost on interruption. */
    bool (*write_text_transient)(PtcStorage *storage, const char *path, const char *text);
    /* Optional bounded read that distinguishes confirmed absence from I/O errors. */
    PtcStorageReadResult (*read_text_status)(PtcStorage *storage, const char *path, char *out, size_t out_size);
    /* Optional bounded binary read at offset (e.g. NRO/NACP header inspection). */
    bool (*read_range)(PtcStorage *storage, const char *path, uint64_t offset,
                       void *out, size_t size, size_t *bytes_read);
} PtcStorageVTable;

struct PtcStorage {
    const PtcStorageVTable *vtable;
    void *ctx;
};

#endif
