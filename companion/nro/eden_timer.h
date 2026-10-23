#ifndef PTC_EDEN_TIMER_H
#define PTC_EDEN_TIMER_H

#include "../../sysmodule/sysmodule_core.h"
#include "../../platform/host/pctl_stub.h"

/* Emulator-only restoration; never link this into production. */
bool ptc_eden_restore_timer(PtcSysmodule *core, PtcPctlStub *pctl,
    PtcClockSnapshot now, uint64_t *carry_ns);
/* Sample at the actual read boundary, never reuse a UI poll timestamp. */
bool ptc_eden_refresh_usage(PtcPctlStub *pctl, PtcClockSnapshot now,
    uint64_t elapsed_ns, uint64_t *carry_ns);
PtcErrorCode ptc_eden_read_grant_sample(PtcPctlStub *pctl,
    const PtcPctlVTable *base, uint8_t weekday, PtcClockSnapshot now,
    uint64_t elapsed_ns, uint64_t *carry_ns, PtcPctlStatus *out);

#endif
