#ifndef PTC_OVERLAY_BRIDGE_H
#define PTC_OVERLAY_BRIDGE_H

#include <stdbool.h>

#include "../transport_client.h"
#include "../switch_ipc_client.h"
#include "../result_summary.h"

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
    PtcCompanionStatus last_status;
} PtcOverlayBridge;

void ptc_overlay_bridge_init(PtcOverlayBridge *bridge, const char *app_root, PtcStorage *storage);
void ptc_overlay_bridge_exit(PtcOverlayBridge *bridge);
PtcCompanionStatus ptc_overlay_bridge_submit(
    PtcOverlayBridge *bridge,
    const char *code,
    int64_t created_at,
    uint16_t random16,
    const PtcCompanionResultSummary *preview);
PtcCompanionStatus ptc_overlay_bridge_preview(PtcOverlayBridge *bridge, const char *code, int64_t created_at, uint16_t random16);
PtcCompanionStatus ptc_overlay_bridge_submit_status(PtcOverlayBridge *bridge, int64_t created_at, uint16_t random16);
PtcCompanionStatus ptc_overlay_bridge_submit_overlay_ready(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16, const char *release_id,
    const char *boot_id, const char *environment_fingerprint);
PtcCompanionStatus ptc_overlay_bridge_skip_bedtime(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16, uint64_t window_instance_id);
PtcCompanionStatus ptc_overlay_bridge_disable_bedtime(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16);
PtcCompanionStatus ptc_overlay_bridge_add_today_minutes(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16, uint16_t minutes);
PtcCompanionStatus ptc_overlay_bridge_disable_today_limit(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16);
PtcCompanionStatus ptc_overlay_bridge_restore_install_snapshot(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16);
PtcCompanionStatus ptc_overlay_bridge_poll(PtcOverlayBridge *bridge, int elapsed_ms, int timeout_ms);
bool ptc_overlay_bridge_waiting(const PtcOverlayBridge *bridge);
const PtcCompanionResultSummary *ptc_overlay_bridge_summary(const PtcOverlayBridge *bridge);
PtcCompanionTransportRoute ptc_overlay_bridge_transport_state(const PtcOverlayBridge *bridge);
const char *ptc_overlay_bridge_transport_label(const PtcOverlayBridge *bridge);
PtcCompanionStatus ptc_overlay_bridge_last_status(const PtcOverlayBridge *bridge);
const char *ptc_overlay_bridge_error_message_zh(const PtcOverlayBridge *bridge);
bool ptc_overlay_bridge_status_succeeded(const PtcOverlayBridge *bridge);
bool ptc_overlay_bridge_offline_code_succeeded(const PtcOverlayBridge *bridge);
bool ptc_overlay_bridge_preview_succeeded(const PtcOverlayBridge *bridge);

#endif
