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
    PtcErrorCode read_error;
    unsigned int read_status_calls;
    unsigned int read_status_fail_on_call;
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
    uint64_t usage_fraction_ns;
    bool expiry_observed;
    bool restore_called;
    int64_t forensic_remaining_ns;
    int64_t forensic_spent_ns;
    uint64_t forensic_monotonic_ns;
    bool settings_header_initialized;
    bool raw_settings_override_enabled;
    uint8_t raw_settings[PTC_PCTL_OPAQUE_SETTINGS_SIZE];
    bool suspend_event_armed;
    bool suspend_event_signaled;
    bool suspend_event_signaled_latched;
    uint32_t suspend_event_check_count;
    uint64_t suspend_event_first_signaled_monotonic_ns;
    bool public_parity_override_enabled;
    PtcPctlPublicParity public_parity_override;
} PtcPctlStub;

void ptc_pctl_stub_init(PtcPctlStub *stub);
PtcPctl *ptc_pctl_stub_as_pctl(PtcPctlStub *stub);
/* Advances the Eden-only simulated timer; carry_ns holds a partial minute. */
bool ptc_pctl_stub_advance_usage_ns(PtcPctlStub *stub, uint64_t elapsed_ns, uint64_t *carry_ns);
void ptc_pctl_stub_reset_daily_usage(PtcPctlStub *stub);

#endif
