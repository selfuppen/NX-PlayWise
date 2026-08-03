#ifndef PTC_COMPANION_TRANSPORT_CLIENT_H
#define PTC_COMPANION_TRANSPORT_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "file_protocol.h"

typedef enum {
    PTC_TRANSPORT_NONE = 0,
    PTC_TRANSPORT_IPC = 1,
    PTC_TRANSPORT_FILE = 2,
} PtcCompanionTransportKind;

typedef struct {
    bool (*connect)(void *ctx);
    PtcCompanionStatus (*submit)(void *ctx, const char *request_id, const char *json, void **wait_token);
    int (*event_status)(void *ctx, void *wait_token);
    PtcCompanionStatus (*get_result)(void *ctx, const char *request_id, char *out, size_t out_size);
    void (*close_wait)(void *ctx, void *wait_token);
    bool (*notify_storage_changed)(void *ctx);
} PtcCompanionIpcBackend;

typedef struct {
    PtcCompanionFileClient file;
    const PtcCompanionIpcBackend *ipc;
    void *ipc_ctx;
    PtcCompanionTransportKind active;
    char active_request_id[80];
    void *wait_token;
    int elapsed_ms;
    int next_file_poll_ms;
    int file_poll_delay_ms;
    bool accepted_by_ipc;
} PtcCompanionTransportClient;

void ptc_companion_transport_init(PtcCompanionTransportClient *client, const char *app_root, PtcStorage *storage,
    const PtcCompanionIpcBackend *ipc, void *ipc_ctx);
PtcCompanionStatus ptc_companion_transport_submit_json(PtcCompanionTransportClient *client,
    const char *request_id, const char *json);
PtcCompanionStatus ptc_companion_transport_poll(PtcCompanionTransportClient *client, int elapsed_ms,
    int timeout_ms, char *out, size_t out_size);
void ptc_companion_transport_cancel(PtcCompanionTransportClient *client);
bool ptc_companion_transport_notify_storage_changed(PtcCompanionTransportClient *client);
PtcCompanionStatus ptc_companion_transport_submit_status(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_transport_submit_offline_code(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, const char *code);
PtcCompanionStatus ptc_companion_transport_submit_set_today_limit(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, uint16_t minutes);
PtcCompanionStatus ptc_companion_transport_submit_add_today_minutes(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, uint16_t minutes);
PtcCompanionStatus ptc_companion_transport_submit_parent_unlock_start(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, uint16_t minutes);
PtcCompanionStatus ptc_companion_transport_submit_empty(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, const char *type);
PtcCompanionStatus ptc_companion_transport_submit_set_weekly_template(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, const PtcDayRule week[7]);
PtcCompanionStatus ptc_companion_transport_submit_set_bedtime(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, const PtcBedtimeRule *bedtime);
PtcCompanionStatus ptc_companion_transport_submit_set_limit_action(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, PtcLimitAction action);
PtcCompanionStatus ptc_companion_transport_submit_probe_play_timer_effect(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, bool wait_for_expiry);

#endif
