#ifndef PTC_SWITCH_PCTL_ADAPTER_H
#define PTC_SWITCH_PCTL_ADAPTER_H

#include "../pctl.h"

typedef struct {
    PtcPctl pctl;
    bool initialized;
} PtcSwitchPctl;

void ptc_switch_pctl_init(PtcSwitchPctl *adapter);
void ptc_switch_pctl_exit(PtcSwitchPctl *adapter);
PtcPctl *ptc_switch_pctl_as_pctl(PtcSwitchPctl *adapter);

#endif
