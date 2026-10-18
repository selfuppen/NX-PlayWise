#ifndef PTC_EDEN_TIMER_H
#define PTC_EDEN_TIMER_H

#include "../../sysmodule/sysmodule_core.h"
#include "../../platform/host/pctl_stub.h"

/* Emulator-only restoration; never link this into production. */
bool ptc_eden_restore_timer(PtcSysmodule *core, PtcPctlStub *pctl,
    PtcClockSnapshot now, uint64_t *carry_ns);

#endif
