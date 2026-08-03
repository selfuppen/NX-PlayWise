#include "bridge.h"

#include <stdio.h>
#include <string.h>

void ptc_overlay_bridge_init(PtcOverlayBridge *bridge, const char *app_root, PtcStorage *storage)
{
    if (!bridge) return;
    memset(bridge, 0, sizeof(*bridge));
    ptc_companion_file_client_init(&bridge->client, app_root, storage);
}

PtcCompanionStatus ptc_overlay_bridge_submit(PtcOverlayBridge *bridge, const char *code, int64_t created_at, uint16_t random16)
{
    PtcCompanionStatus status;
    if (!bridge || !code || code[0] == '\0') return PTC_COMPANION_BAD_ARGUMENT;
    if (ptc_companion_make_request_id(bridge->request_id, sizeof(bridge->request_id), created_at * 1000, random16) != PTC_COMPANION_OK)
        return PTC_COMPANION_BAD_ARGUMENT;
    status = ptc_companion_submit_offline_code(&bridge->client, bridge->request_id, created_at, code);
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
    status = ptc_companion_read_result(&bridge->client, bridge->request_id, bridge->elapsed_ms, timeout_ms,
        bridge->result_json, sizeof(bridge->result_json));
    if (status == PTC_COMPANION_OK) {
        if (ptc_companion_parse_result_summary(bridge->result_json, &bridge->summary) != PTC_COMPANION_OK) {
            bridge->waiting = false;
            return PTC_COMPANION_RESULT_INVALID;
        }
        bridge->waiting = false;
    } else if (status == PTC_COMPANION_TIMEOUT || status == PTC_COMPANION_RESULT_INVALID || status == PTC_COMPANION_RESULT_MISMATCH) {
        bridge->waiting = false;
    }
    return status;
}

bool ptc_overlay_bridge_waiting(const PtcOverlayBridge *bridge) { return bridge && bridge->waiting; }
const PtcCompanionResultSummary *ptc_overlay_bridge_summary(const PtcOverlayBridge *bridge) { return bridge ? &bridge->summary : NULL; }
