#ifndef PTC_SYSMODULE_IDLE_WAIT_H
#define PTC_SYSMODULE_IDLE_WAIT_H
#include <stdbool.h>
#include <stdint.h>

/* IPC wakes immediately. File-only controls retain a bounded one-second poll;
   handoff and diagnostic/notification leases keep their existing cadence. */
static inline uint32_t ptc_sysmodule_idle_wait_ms(uint32_t scheduled_ms, bool fast)
{
    uint32_t ceiling = fast ? 250U : 1000U;
    return scheduled_ms < ceiling ? scheduled_ms : ceiling;
}
#endif
