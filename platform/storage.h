#ifndef PTC_PLATFORM_STORAGE_H
#define PTC_PLATFORM_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct PtcStorage PtcStorage;

typedef struct {
    bool (*read_text)(PtcStorage *storage, const char *path, char *out, size_t out_size);
    bool (*write_text_atomic)(PtcStorage *storage, const char *path, const char *text);
    bool (*append_line)(PtcStorage *storage, const char *path, const char *line);
    bool (*rename_path)(PtcStorage *storage, const char *from, const char *to);
    bool (*remove_path)(PtcStorage *storage, const char *path);
    bool (*exists)(PtcStorage *storage, const char *path);
    bool (*list_json)(PtcStorage *storage, const char *dir, char names[][128], size_t max, size_t *count);
} PtcStorageVTable;

struct PtcStorage {
    const PtcStorageVTable *vtable;
    void *ctx;
};

#endif
