#ifndef PTC_COMPANION_NRO_EDEN_RUNTIME_H
#define PTC_COMPANION_NRO_EDEN_RUNTIME_H

#ifdef PLAYWISE_EDEN

#include <stdbool.h>

#include "../../platform/host/pctl_stub.h"
#include "../../platform/storage.h"
#include "../../platform/time_provider.h"
#include "../../sysmodule/sysmodule_core.h"

/* Fixed test material so tools/grant_code.py can mint redeemable codes without
   reading the emulator SD card. Never valid on real hardware: the package gate
   rejects both strings in every public artifact. */
#define PTC_EDEN_TEST_DEVICE_ID "eden-switch"
#define PTC_EDEN_TEST_GRANT_SECRET "playwise-eden-test-secret-00000001"

typedef struct {
    PtcPctlStub pctl;
    PtcTimeProvider time_provider;
    PtcSysmodule sysmodule;
    bool initialized;
} PtcEdenRuntime;

/* Seeds the isolated Eden app root and starts the in-process control core.
   Must run before ptc_install_materialize_defaults so the seeded live files
   make that call succeed without a packaged defaults/ directory. */
bool ptc_eden_runtime_init(PtcEdenRuntime *runtime, PtcStorage *storage);
void ptc_eden_runtime_tick(PtcEdenRuntime *runtime);

#endif

#endif
