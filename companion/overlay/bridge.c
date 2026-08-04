#include "bridge.h"

#include <stdio.h>
#include <string.h>

static void update_transport_state(PtcOverlayBridge *bridge)
{
    PtcCompanionTransportKind active;
    if (!bridge) return;
    active = ptc_companion_transport_active(&bridge->transport);
    if (active == PTC_TRANSPORT_FILE) {
        bridge->transport_state = ptc_companion_transport_accepted_by_ipc(&bridge->transport)
            ? PTC_OVERLAY_TRANSPORT_SD_RESULT_AFTER_IPC : PTC_OVERLAY_TRANSPORT_SD_QUEUE;
    } else if (active == PTC_TRANSPORT_IPC) {
        bridge->transport_state = PTC_OVERLAY_TRANSPORT_IPC;
    } else {
        bridge->transport_state = PTC_OVERLAY_TRANSPORT_NONE;
    }
}

void ptc_overlay_bridge_init(PtcOverlayBridge *bridge, const char *app_root, PtcStorage *storage)
{
    if (!bridge) return;
    memset(bridge, 0, sizeof(*bridge));
#ifdef __SWITCH__
    ptc_switch_ipc_client_init(&bridge->ipc);
    ptc_companion_transport_init(&bridge->transport, app_root, storage, ptc_switch_ipc_backend(), &bridge->ipc);
#else
    ptc_companion_transport_init(&bridge->transport, app_root, storage, NULL, NULL);
#endif
}

PtcCompanionStatus ptc_overlay_bridge_submit(PtcOverlayBridge *bridge, const char *code, int64_t created_at, uint16_t random16)
{
    PtcCompanionStatus status;
    if (!bridge || !code || code[0] == '\0') return PTC_COMPANION_BAD_ARGUMENT;
    memset(&bridge->summary, 0, sizeof(bridge->summary));
    bridge->last_status = PTC_COMPANION_PENDING;
    if (ptc_companion_make_request_id(bridge->request_id, sizeof(bridge->request_id), created_at * 1000, random16) != PTC_COMPANION_OK)
        return PTC_COMPANION_BAD_ARGUMENT;
    status = ptc_companion_transport_submit_offline_code(&bridge->transport, bridge->request_id, created_at, code);
    bridge->last_status = status;
    update_transport_state(bridge);
    if (status == PTC_COMPANION_OK) {
        bridge->elapsed_ms = 0;
        bridge->waiting = true;
    }
    return status;
}

PtcCompanionStatus ptc_overlay_bridge_poll(PtcOverlayBridge *bridge, int elapsed_ms, int timeout_ms)
{
    PtcCompanionStatus status;
    if (!bridge || !bridge->waiting) return PTC_COMPANION_BAD_ARGUMENT;
    if (elapsed_ms > 0) bridge->elapsed_ms += elapsed_ms;
    status = ptc_companion_transport_poll(&bridge->transport, elapsed_ms, timeout_ms,
        bridge->result_json, sizeof(bridge->result_json));
    bridge->last_status = status;
    update_transport_state(bridge);
    if (status == PTC_COMPANION_OK) {
        if (ptc_companion_parse_result_summary(bridge->result_json, &bridge->summary) != PTC_COMPANION_OK) {
            bridge->waiting = false;
            bridge->last_status = PTC_COMPANION_RESULT_INVALID;
            return PTC_COMPANION_RESULT_INVALID;
        }
        bridge->waiting = false;
    } else if (status != PTC_COMPANION_PENDING) {
        bridge->waiting = false;
    }
    return status;
}

bool ptc_overlay_bridge_waiting(const PtcOverlayBridge *bridge) { return bridge && bridge->waiting; }
const PtcCompanionResultSummary *ptc_overlay_bridge_summary(const PtcOverlayBridge *bridge) { return bridge ? &bridge->summary : NULL; }
PtcOverlayTransportState ptc_overlay_bridge_transport_state(const PtcOverlayBridge *bridge)
{
    return bridge ? bridge->transport_state : PTC_OVERLAY_TRANSPORT_NONE;
}

const char *ptc_overlay_bridge_transport_label(const PtcOverlayBridge *bridge)
{
    switch (ptc_overlay_bridge_transport_state(bridge)) {
    case PTC_OVERLAY_TRANSPORT_IPC: return "传输：IPC";
    case PTC_OVERLAY_TRANSPORT_SD_QUEUE: return "传输：SD 文件队列";
    case PTC_OVERLAY_TRANSPORT_SD_RESULT_AFTER_IPC: return "传输：IPC → SD 结果回读";
    default: return "传输：未开始";
    }
}

PtcCompanionStatus ptc_overlay_bridge_last_status(const PtcOverlayBridge *bridge)
{
    return bridge ? bridge->last_status : PTC_COMPANION_BAD_ARGUMENT;
}
