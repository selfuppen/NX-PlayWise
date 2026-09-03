#ifndef PTC_SYSMODULE_IPC_SERVER_H
#define PTC_SYSMODULE_IPC_SERVER_H

#ifdef __SWITCH__
#include <switch.h>

#include "sysmodule_core.h"

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

typedef struct {
    PtcSysmodule *sysmodule;
    Handle port;
    Handle sessions[8];
    unsigned int session_count;
    Mutex storage_mutex;
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
bool ptc_ipc_server_take_wake(PtcIpcServer *server);
bool ptc_ipc_server_wait(PtcIpcServer *server, uint32_t timeout_ms);
void ptc_ipc_server_signal_completed(PtcIpcServer *server);
void ptc_ipc_server_lock_storage(PtcIpcServer *server);
void ptc_ipc_server_unlock_storage(PtcIpcServer *server);
void ptc_ipc_server_set_accepting(PtcIpcServer *server, bool accepting);

#endif
#endif
