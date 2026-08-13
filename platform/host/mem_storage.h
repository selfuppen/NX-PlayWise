#ifndef PTC_HOST_MEM_STORAGE_H
#define PTC_HOST_MEM_STORAGE_H

#include "../storage.h"

#define PTC_MEM_STORAGE_MAX_FILES 256
#define PTC_MEM_STORAGE_PATH_SIZE 320
#define PTC_MEM_STORAGE_TEXT_SIZE 8192

typedef struct {
    char path[PTC_MEM_STORAGE_PATH_SIZE];
    char text[PTC_MEM_STORAGE_TEXT_SIZE];
    int64_t modified_unix_seconds;
    bool modified_time_valid;
    bool present;
} PtcMemStorageFile;

typedef struct {
    PtcStorage storage;
    PtcMemStorageFile files[PTC_MEM_STORAGE_MAX_FILES];
    bool fail_reads;
    bool fail_writes;
    bool fail_appends;
    bool fail_renames;
    bool fail_removes;
    const char *fail_write_path_contains;
    const char *fail_rename_path_contains;
    const char *fail_remove_path_contains;
    int64_t now_unix_seconds;
} PtcMemStorage;

void ptc_mem_storage_init(PtcMemStorage *mem);
PtcStorage *ptc_mem_storage_as_storage(PtcMemStorage *mem);
void ptc_mem_storage_set_now(PtcMemStorage *mem, int64_t unix_seconds);
bool ptc_mem_storage_set_mtime(PtcMemStorage *mem, const char *path, int64_t unix_seconds, bool valid);

#endif
