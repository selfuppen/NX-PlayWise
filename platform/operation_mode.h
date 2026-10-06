#ifndef PTC_OPERATION_MODE_H
#define PTC_OPERATION_MODE_H

#include <stdbool.h>

typedef enum {
    PTC_OPERATION_MODE_UNKNOWN = 0,
    PTC_OPERATION_MODE_UNDOCKED = 1,
    PTC_OPERATION_MODE_DOCKED = 2
} PtcOperationMode;

typedef struct {
    PtcOperationMode mode;
    bool dock_supported_available;
    bool dock_supported;
} PtcOperationModeStatus;

typedef struct {
    PtcOperationModeStatus (*read)(void *ctx);
    void *ctx;
} PtcOperationModeProvider;

#endif
