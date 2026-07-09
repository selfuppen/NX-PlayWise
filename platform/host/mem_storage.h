#ifndef PTC_HOST_MEM_STORAGE_H
#define PTC_HOST_MEM_STORAGE_H

#include "../storage.h"

#define PTC_MEM_STORAGE_MAX_FILES 128
#define PTC_MEM_STORAGE_PATH_SIZE 160
#define PTC_MEM_STORAGE_TEXT_SIZE 4096

typedef struct {
    char path[PTC_MEM_STORAGE_PATH_SIZE];
    char text[PTC_MEM_STORAGE_TEXT_SIZE];
    bool present;
} PtcMemStorageFile;

typedef struct {
    PtcStorage storage;
    PtcMemStorageFile files[PTC_MEM_STORAGE_MAX_FILES];
    bool fail_reads;
    bool fail_writes;
    bool fail_appends;
    bool fail_renames;
    const char *fail_write_path_contains;
} PtcMemStorage;

void ptc_mem_storage_init(PtcMemStorage *mem);
PtcStorage *ptc_mem_storage_as_storage(PtcMemStorage *mem);

#endif
