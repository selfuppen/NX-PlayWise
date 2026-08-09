#include "bridge.h"

#include <stdio.h>
#include <string.h>

#include "../../common/protocol/error_code.h"

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
    if (ptc_companion_make_request_id(bridge->request_id, sizeof(bridge->request_id), created_at * 1000, random16) != PTC_COMPANION_OK) {
        bridge->last_status = PTC_COMPANION_BAD_ARGUMENT;
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    status = ptc_companion_transport_submit_offline_code(&bridge->transport, bridge->request_id, created_at, code);
    bridge->last_status = status;
    if (status == PTC_COMPANION_OK) {
        bridge->elapsed_ms = 0;
        bridge->waiting = true;
    }
    return status;
}

PtcCompanionStatus ptc_overlay_bridge_submit_status(PtcOverlayBridge *bridge, int64_t created_at, uint16_t random16)
{
    PtcCompanionStatus status;
    if (!bridge) return PTC_COMPANION_BAD_ARGUMENT;
    memset(&bridge->summary, 0, sizeof(bridge->summary));
    bridge->last_status = PTC_COMPANION_PENDING;
    if (ptc_companion_make_request_id(bridge->request_id, sizeof(bridge->request_id), created_at * 1000, random16) != PTC_COMPANION_OK) {
        bridge->last_status = PTC_COMPANION_BAD_ARGUMENT;
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    status = ptc_companion_transport_submit_status(&bridge->transport, bridge->request_id, created_at);
    bridge->last_status = status;
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
PtcCompanionTransportRoute ptc_overlay_bridge_transport_state(const PtcOverlayBridge *bridge)
{
    return bridge ? ptc_companion_transport_route(&bridge->transport) : PTC_TRANSPORT_ROUTE_NONE;
}

const char *ptc_overlay_bridge_transport_label(const PtcOverlayBridge *bridge)
{
    return ptc_companion_transport_route_label_zh(ptc_overlay_bridge_transport_state(bridge));
}

PtcCompanionStatus ptc_overlay_bridge_last_status(const PtcOverlayBridge *bridge)
{
    return bridge ? bridge->last_status : PTC_COMPANION_BAD_ARGUMENT;
}

const char *ptc_overlay_bridge_error_message_zh(const PtcOverlayBridge *bridge)
{
    if (!bridge) return "请求参数无效";
    if (bridge->summary.valid) {
        if (!bridge->summary.ok) {
            if (bridge->summary.message[0]) return bridge->summary.message;
            if (bridge->summary.error_code > 0)
                return ptc_error_message_zh((PtcErrorCode)bridge->summary.error_code);
            return "请求失败，请稍后重试";
        }
        return "请求成功";
    }
    switch (bridge->last_status) {
    case PTC_COMPANION_TIMEOUT: return "后台响应超时，请重试";
    case PTC_COMPANION_WRITE_FAILED:
    case PTC_COMPANION_RENAME_FAILED: return "请求写入失败，请检查 SD 卡";
    case PTC_COMPANION_RESULT_INVALID:
    case PTC_COMPANION_RESULT_MISMATCH: return "后台返回的结果无效";
    case PTC_COMPANION_BAD_ARGUMENT: return "请求被后台拒绝";
    default: return "无法连接后台服务，请重试";
    }
}

bool ptc_overlay_bridge_status_succeeded(const PtcOverlayBridge *bridge)
{
    return bridge && bridge->summary.valid && bridge->summary.ok &&
        strcmp(bridge->summary.type, "status") == 0;
}

bool ptc_overlay_bridge_offline_code_succeeded(const PtcOverlayBridge *bridge)
{
    return bridge && bridge->summary.valid && bridge->summary.ok &&
        bridge->summary.unlock_observed && strcmp(bridge->summary.type, "offline_code") == 0;
}
