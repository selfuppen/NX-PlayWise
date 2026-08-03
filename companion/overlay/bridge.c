#include "bridge.h"

#include <stdio.h>
#include <string.h>

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
    if (ptc_companion_make_request_id(bridge->request_id, sizeof(bridge->request_id), created_at * 1000, random16) != PTC_COMPANION_OK)
        return PTC_COMPANION_BAD_ARGUMENT;
    status = ptc_companion_transport_submit_offline_code(&bridge->transport, bridge->request_id, created_at, code);
    if (status == PTC_COMPANION_OK) {
        bridge->elapsed_ms = 0;
        bridge->waiting = true;
        memset(&bridge->summary, 0, sizeof(bridge->summary));
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
    if (status == PTC_COMPANION_OK) {
        if (ptc_companion_parse_result_summary(bridge->result_json, &bridge->summary) != PTC_COMPANION_OK) {
            bridge->waiting = false;
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
