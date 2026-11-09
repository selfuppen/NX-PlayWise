#ifndef PTC_RUNTIME_STATE_H
#define PTC_RUNTIME_STATE_H

#include "../platform/pctl.h"
#include "../platform/operation_mode.h"
#include "../common/usage/usage_counter.h"

typedef struct {
    uint16_t last_enforced_day_index;
    PtcPctlTargetMode last_enforced_mode;
    uint16_t last_enforced_minutes;
    bool apply_pending_confirmation;
    int64_t apply_confirmation_deadline;
    PtcPctlTargetMode pending_mode;
    uint16_t pending_minutes;
    uint16_t v2_failed_attempts;
    int64_t v2_cooldown_until;
    bool buffer_claimed;
    uint16_t buffer_claim_day_index;
    uint16_t buffer_claimed_minutes;
    uint16_t summary_day_index;
    uint16_t summary_grant_minutes;
    bool bedtime_enforced;
    uint64_t bedtime_window_instance_id;
    uint16_t bedtime_start_day_index;
    uint64_t bedtime_skipped_instance_id;
    uint16_t eye_care_day_index;
    uint16_t eye_care_accumulated_minutes;
    uint16_t eye_care_last_used_minutes;
    bool eye_care_usage_known;
    bool eye_care_resting;
    int64_t eye_care_rest_deadline;
    uint64_t eye_care_break_id;
    int64_t eye_care_idle_since;
    PtcUsageCounter eye_usage_counter;
    uint64_t eye_cycle_used_ns;
    bool eye_usage_migrated;
    bool eye_usage_boundary_gap;
    uint16_t dock_day_index;
    uint64_t undocked_used_ns;
    uint64_t dock_last_used_ns;
    bool dock_baseline_known;
    bool dock_usage_known;
    bool dock_tracking_started;
    bool dock_interval_unknown;
    PtcOperationMode dock_last_mode;
    uint16_t dock_waived_day_index;
    bool dock_waived;
    bool dock_enforced;
    PtcUsageCounter dock_usage_counter;
    bool dock_usage_incomplete;
} PtcRuntimeState;

#endif
