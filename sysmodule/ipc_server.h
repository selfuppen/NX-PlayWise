#ifndef PTC_SYSMODULE_IPC_SERVER_H
#define PTC_SYSMODULE_IPC_SERVER_H

#ifdef __SWITCH__
#include <switch.h>

#include "sysmodule_core.h"
#ifndef PLAYWISE_DEVICE_LAB
#include "../platform/reason_detail.h"
#endif

typedef struct {
    char request_id[80];
    Event completion;
    bool active;
    bool signaled;
} PtcIpcWaiter;

typedef struct {
    char request_id[80];
    char json[8193];
    uint32_t length;
    bool active;
} PtcIpcResultCacheEntry;

#ifndef PLAYWISE_DEVICE_LAB
typedef struct {
    Handle session;
    Event event;
} PtcIpcReasonSubscriber;
#endif

typedef struct {
    PtcSysmodule *sysmodule;
    Handle port;
    Handle sessions[8];
    unsigned int session_count;
    Mutex storage_mutex;
#ifndef PLAYWISE_DEVICE_LAB
    Mutex reason_mutex;
    PtcIpcReasonSubscriber reason_subscribers[8];
    Handle reason_ready_session;
    PtcReasonDetail reason_ready;
    bool reason_ready_valid;
    bool reason_stopping;
#endif
    Event wake_event;
    Event error_event;
    Event completed_event;
    Thread thread;
    PtcIpcWaiter waiters[16];
    PtcIpcResultCacheEntry result_cache[8];
    unsigned int result_cache_next;
    bool running;
    bool accepting;
    bool registered;
} PtcIpcServer;

bool ptc_ipc_server_start(PtcIpcServer *server, PtcSysmodule *sysmodule);
void ptc_ipc_server_stop(PtcIpcServer *server);
bool ptc_ipc_server_wait(PtcIpcServer *server, uint32_t timeout_ms);
void ptc_ipc_server_signal_completed(PtcIpcServer *server);
#ifndef PLAYWISE_DEVICE_LAB
void ptc_ipc_server_signal_reason(void *ctx);
bool ptc_ipc_server_read_reason_ready(void *ctx, PtcReasonDetail *out);
#endif
void ptc_ipc_server_lock_storage(PtcIpcServer *server);
void ptc_ipc_server_unlock_storage(PtcIpcServer *server);

#endif
#endif
