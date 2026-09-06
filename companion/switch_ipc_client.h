#ifndef PTC_COMPANION_SWITCH_IPC_CLIENT_H
#define PTC_COMPANION_SWITCH_IPC_CLIENT_H

#ifdef __SWITCH__
#include <switch.h>

#include "transport_client.h"

typedef struct {
    Service service;
    bool initialized;
    bool sm_initialized;
} PtcSwitchIpcClient;

void ptc_switch_ipc_client_init(PtcSwitchIpcClient *client);
void ptc_switch_ipc_client_exit(PtcSwitchIpcClient *client);
bool ptc_switch_ipc_client_probe(PtcSwitchIpcClient *client);
const PtcCompanionIpcBackend *ptc_switch_ipc_backend(void);
#endif

#endif
