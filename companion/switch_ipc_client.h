#ifndef PTC_COMPANION_SWITCH_IPC_CLIENT_H
#define PTC_COMPANION_SWITCH_IPC_CLIENT_H

#ifdef __SWITCH__
#include <switch.h>

#include "transport_client.h"
#include "../platform/reason_detail.h"

typedef struct {
    Service service;
    bool initialized;
    bool sm_initialized;
} PtcSwitchIpcClient;

void ptc_switch_ipc_client_init(PtcSwitchIpcClient *client);
void ptc_switch_ipc_client_exit(PtcSwitchIpcClient *client);
bool ptc_switch_ipc_client_probe(PtcSwitchIpcClient *client);
#ifdef PTC_IPC_CLIENT_OVERLAY
bool ptc_switch_ipc_subscribe_reason(PtcSwitchIpcClient *client, const char *release, const char *boot, Event *event);
bool ptc_switch_ipc_update_reason_ready(PtcSwitchIpcClient *client, const PtcReasonDetail *ready);
void ptc_switch_ipc_clear_reason_ready(PtcSwitchIpcClient *client);
bool ptc_switch_ipc_request_wake_reason_recheck(PtcSwitchIpcClient *client, const char *release, const char *boot);
#endif
bool ptc_switch_ipc_query_reason_ready(PtcSwitchIpcClient *client, const char *release, const char *boot,
    PtcReasonDetail *out);
const PtcCompanionIpcBackend *ptc_switch_ipc_backend(void);
#endif

#endif
