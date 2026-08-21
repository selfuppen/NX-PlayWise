#ifndef PTC_HOST_PCTL_STUB_H
#define PTC_HOST_PCTL_STUB_H

#include "../pctl.h"

typedef struct {
    PtcPctl pctl;
    PtcPctlStatus status;
    PtcPctlTarget last_target;
    bool applied;
    bool timer_started;
    bool timer_stopped;
    bool play_timer_write_probe_succeeds;
    bool suspend_probe_succeeds;
    PtcErrorCode read_error;
    bool read_fails_after_apply;
    PtcErrorCode backup_error;
    PtcErrorCode write_error;
    unsigned int apply_target_calls;
    unsigned int apply_target_fail_on_call;
    PtcErrorCode start_timer_error;
    unsigned int start_timer_calls;
    unsigned int start_timer_fail_on_call;
    PtcErrorCode snapshot_error;
    PtcErrorCode restore_error;
    bool runtime_effect_succeeds;
    bool model_elapsed_time;
    bool hide_restricted_now;
    uint16_t configured_minutes;
    uint32_t played_minutes_today;
    bool expiry_observed;
    bool restore_called;
    int64_t forensic_remaining_ns;
    int64_t forensic_spent_ns;
    uint64_t forensic_monotonic_ns;
    bool suspend_event_armed;
    bool suspend_event_signaled;
} PtcPctlStub;

void ptc_pctl_stub_init(PtcPctlStub *stub);
PtcPctl *ptc_pctl_stub_as_pctl(PtcPctlStub *stub);

#endif
