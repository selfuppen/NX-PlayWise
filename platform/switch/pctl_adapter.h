#ifndef PTC_SWITCH_PCTL_ADAPTER_H
#define PTC_SWITCH_PCTL_ADAPTER_H

#include <switch.h>

#include "../pctl.h"

typedef struct {
    PtcPctl pctl;
    Result last_result;
    bool usage_sampled;
    bool usage_available;
    uint64_t usage_sample_tick;
    uint64_t usage_ns;
    uint16_t usage_day_index;
    int64_t usage_captured_at;
    Result usage_result;
    bool usage_high_water_available;
    uint16_t usage_high_water_day;
    uint64_t usage_high_water_ns;
#ifdef PLAYWISE_DEVICE_LAB
    Handle suspend_event;
    bool suspend_event_signaled;
    uint32_t suspend_event_check_count;
    uint64_t suspend_event_first_signaled_monotonic_ns;
#endif
} PtcSwitchPctl;

void ptc_switch_pctl_init(PtcSwitchPctl *adapter);
void ptc_switch_pctl_exit(PtcSwitchPctl *adapter);
PtcPctl *ptc_switch_pctl_as_pctl(PtcSwitchPctl *adapter);

#endif
