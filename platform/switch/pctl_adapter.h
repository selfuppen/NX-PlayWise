#ifndef PTC_SWITCH_PCTL_ADAPTER_H
#define PTC_SWITCH_PCTL_ADAPTER_H

#include <switch.h>

#include "../pctl.h"

typedef struct {
    PtcPctl pctl;
    Result last_result;
#ifdef PLAYWISE_DEVICE_LAB
    Handle suspend_event;
#endif
} PtcSwitchPctl;

void ptc_switch_pctl_init(PtcSwitchPctl *adapter);
void ptc_switch_pctl_exit(PtcSwitchPctl *adapter);
PtcPctl *ptc_switch_pctl_as_pctl(PtcSwitchPctl *adapter);

#endif
