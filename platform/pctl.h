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
    bool restriction_enabled_available;
    bool restriction_enabled;
    bool temporary_unlocked_available;
    bool temporary_unlocked;
    bool limited_today;
    bool blocked_today;
    bool unrestricted_today;
    bool remaining_available;
    uint32_t remaining_minutes;
    bool played_minutes_available;
    uint32_t played_minutes;
    bool configured_minutes_available;
    uint16_t configured_minutes;
    bool play_timer_enabled_available;
    bool play_timer_enabled;
    bool restricted_now_available;
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

/* Device Lab keeps the private command wire values separate from release
   status. Release adapters leave the corresponding vtable entries NULL. */
typedef struct {
    uint64_t monotonic_ns;
    uint32_t timer_enabled_result;
    bool timer_enabled;
    uint32_t remaining_result;
    int64_t remaining_ns;
    uint32_t restricted_result;
    bool restricted;
    uint32_t spent_result;
    int64_t spent_ns;
    uint32_t settings_result;
    uint8_t settings[PTC_PCTL_OPAQUE_SETTINGS_SIZE];
} PtcPctlForensicSample;

typedef struct {
    uint32_t raw_temporary_unlocked_result;
    uint32_t libnx_temporary_unlocked_result;
    bool raw_temporary_unlocked;
    bool libnx_temporary_unlocked;
    uint32_t raw_restriction_enabled_result;
    uint32_t libnx_restriction_enabled_result;
    bool raw_restriction_enabled;
    bool libnx_restriction_enabled;
    uint32_t raw_current_settings_result;
    uint32_t libnx_current_settings_result;
    bool current_settings_equal;
    uint32_t raw_suspend_event_result;
    uint32_t libnx_suspend_event_result;
    bool raw_suspend_event_valid;
    bool libnx_suspend_event_valid;
    uint32_t raw_alarm_disabled_result;
    uint32_t libnx_alarm_disabled_result;
    bool raw_alarm_disabled;
    bool libnx_alarm_disabled;
} PtcPctlPublicParity;

typedef struct {
    bool known;
    bool signaled;
    uint32_t check_count;
    uint64_t first_signaled_monotonic_ns;
} PtcPctlSuspendEventEvidence;

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
    PtcErrorCode (*forensic_sample)(PtcPctl *pctl, PtcPctlForensicSample *out);
    PtcErrorCode (*public_parity)(PtcPctl *pctl, PtcPctlPublicParity *out);
    PtcErrorCode (*arm_suspend_event)(PtcPctl *pctl);
    PtcErrorCode (*poll_suspend_event)(PtcPctl *pctl, PtcPctlSuspendEventEvidence *out);
} PtcPctlVTable;

struct PtcPctl {
    const PtcPctlVTable *vtable;
    void *ctx;
};

#endif
