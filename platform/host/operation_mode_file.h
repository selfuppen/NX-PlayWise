#ifndef PTC_HOST_OPERATION_MODE_FILE_H
#define PTC_HOST_OPERATION_MODE_FILE_H

#include "../operation_mode.h"
#include "../storage.h"

typedef struct {
    PtcStorage *storage;
    const char *app_root;
} PtcFileOperationMode;

/* Eden and deterministic host tests only; never linked into production. */
PtcOperationModeStatus ptc_file_operation_mode_read(void *ctx);

#endif
