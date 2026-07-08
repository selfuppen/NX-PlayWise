#ifndef PTC_PLATFORM_PCTL_H
#define PTC_PLATFORM_PCTL_H

#include <stdbool.h>
#include <stdint.h>

#include "../common/protocol/error_code.h"

typedef struct PtcPctl PtcPctl;

typedef enum {
    PTC_PCTL_TARGET_LIMIT = 1,
    PTC_PCTL_TARGET_UNLIMITED = 2,
    PTC_PCTL_TARGET_BLOCKED = 3
} PtcPctlTargetMode;

typedef struct {
    bool limited_today;
    bool blocked_today;
    bool unrestricted_today;
    bool remaining_available;
    uint16_t remaining_minutes;
    bool play_timer_enabled;
    bool restricted_now;
} PtcPctlStatus;

typedef struct {
    PtcPctlTargetMode mode;
    uint16_t minutes;
} PtcPctlTarget;

typedef struct {
    char text[512];
} PtcPctlBackup;

typedef struct {
    bool verified;
    char detail[128];
} PtcProbeResult;

typedef struct {
    PtcErrorCode (*read_status)(PtcPctl *pctl, PtcPctlStatus *out);
    PtcErrorCode (*backup)(PtcPctl *pctl, PtcPctlBackup *out);
    PtcErrorCode (*apply_target)(PtcPctl *pctl, const PtcPctlTarget *target);
    PtcErrorCode (*start_timer)(PtcPctl *pctl);
    PtcErrorCode (*stop_timer)(PtcPctl *pctl);
    PtcErrorCode (*probe_raw_block)(PtcPctl *pctl, PtcProbeResult *out);
    PtcErrorCode (*probe_suspend)(PtcPctl *pctl, PtcProbeResult *out);
} PtcPctlVTable;

struct PtcPctl {
    const PtcPctlVTable *vtable;
    void *ctx;
};

#endif
