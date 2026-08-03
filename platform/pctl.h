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
    uint32_t remaining_minutes;
    bool configured_minutes_available;
    uint16_t configured_minutes;
    bool play_timer_enabled;
    bool restricted_now;
} PtcPctlStatus;

typedef struct {
    PtcPctlTargetMode mode;
    uint16_t minutes;
    uint8_t weekday;
} PtcPctlTarget;

typedef struct {
    char text[1024];
} PtcPctlBackup;

typedef struct {
    bool verified;
    char detail[128];
} PtcProbeResult;

typedef struct {
    bool available;
    PtcErrorCode error;
    uint32_t ipc_result;
    char raw_hex[160];
    char decoded_slots[320];
} PtcPctlDebugSnapshot;

/* Opaque platform snapshot: callers may restore it but must not interpret layout. */
#define PTC_PCTL_OPAQUE_SETTINGS_SIZE 0x44
typedef struct {
    uint8_t data[PTC_PCTL_OPAQUE_SETTINGS_SIZE];
    uint32_t size;
    bool timer_enabled;
} PtcPctlSettingsSnapshot;

typedef struct {
    PtcErrorCode (*read_status)(PtcPctl *pctl, uint8_t weekday, PtcPctlStatus *out);
    PtcErrorCode (*backup)(PtcPctl *pctl, PtcPctlBackup *out);
    PtcErrorCode (*apply_target)(PtcPctl *pctl, const PtcPctlTarget *target);
    PtcErrorCode (*start_timer)(PtcPctl *pctl);
    PtcErrorCode (*stop_timer)(PtcPctl *pctl);
    /* Raw block has no probe entry: it is orchestrated by the sysmodule through
       snapshot_settings/apply_target/restore_settings so it produces A/B evidence. */
    PtcErrorCode (*probe_suspend)(PtcPctl *pctl, PtcProbeResult *out);
    PtcErrorCode (*probe_play_timer_write)(PtcPctl *pctl, PtcProbeResult *out);
    PtcErrorCode (*snapshot_settings)(PtcPctl *pctl, PtcPctlSettingsSnapshot *out);
    PtcErrorCode (*restore_settings)(PtcPctl *pctl, const PtcPctlSettingsSnapshot *snapshot);
    PtcErrorCode (*debug_snapshot)(PtcPctl *pctl, PtcPctlDebugSnapshot *out);
    uint32_t (*last_ipc_result)(PtcPctl *pctl);
} PtcPctlVTable;

struct PtcPctl {
    const PtcPctlVTable *vtable;
    void *ctx;
};

#endif
