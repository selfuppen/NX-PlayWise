#ifndef PTC_SWITCH_FS_STORAGE_H
#define PTC_SWITCH_FS_STORAGE_H

#include "../storage.h"

typedef struct {
    PtcStorage storage;
    PtcStorageAppendDiagnostics last_append;
} PtcFsStorage;

void ptc_fs_storage_init(PtcFsStorage *fs);
PtcStorage *ptc_fs_storage_as_storage(PtcFsStorage *fs);
PtcStorageAppendDiagnostics ptc_fs_storage_append_diagnostics(void *ctx);

#endif
