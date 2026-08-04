#ifndef PTC_OVERLAY_BRIDGE_H
#define PTC_OVERLAY_BRIDGE_H

#include <stdbool.h>

#include "../transport_client.h"
#include "../switch_ipc_client.h"
#include "../result_summary.h"

typedef enum {
    PTC_OVERLAY_TRANSPORT_NONE = 0,
    PTC_OVERLAY_TRANSPORT_IPC,
    PTC_OVERLAY_TRANSPORT_SD_QUEUE,
    PTC_OVERLAY_TRANSPORT_SD_RESULT_AFTER_IPC
} PtcOverlayTransportState;

typedef struct {
    PtcCompanionTransportClient transport;
#ifdef __SWITCH__
    PtcSwitchIpcClient ipc;
#endif
    char request_id[PTC_COMPANION_REQUEST_ID_SIZE];
    char result_json[8192];
    PtcCompanionResultSummary summary;
    int elapsed_ms;
    bool waiting;
    PtcOverlayTransportState transport_state;
    PtcCompanionStatus last_status;
} PtcOverlayBridge;

void ptc_overlay_bridge_init(PtcOverlayBridge *bridge, const char *app_root, PtcStorage *storage);
PtcCompanionStatus ptc_overlay_bridge_submit(PtcOverlayBridge *bridge, const char *code, int64_t created_at, uint16_t random16);
PtcCompanionStatus ptc_overlay_bridge_poll(PtcOverlayBridge *bridge, int elapsed_ms, int timeout_ms);
bool ptc_overlay_bridge_waiting(const PtcOverlayBridge *bridge);
const PtcCompanionResultSummary *ptc_overlay_bridge_summary(const PtcOverlayBridge *bridge);
PtcOverlayTransportState ptc_overlay_bridge_transport_state(const PtcOverlayBridge *bridge);
const char *ptc_overlay_bridge_transport_label(const PtcOverlayBridge *bridge);
PtcCompanionStatus ptc_overlay_bridge_last_status(const PtcOverlayBridge *bridge);

#endif
