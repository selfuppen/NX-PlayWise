#include "transport_client.h"

#include <stdio.h>
#include <string.h>

#include "../common/protocol/request_schema.h"
#include "../common/protocol/result_builder.h"
#include "../third_party/cjson/cJSON.h"
#include "request_client.h"

static PtcCompanionStatus file_submit_json(PtcCompanionTransportClient *client, const char *request_id, const char *json)
{
    char path[240];
    if (!client || !ptc_request_id_is_valid(request_id) || !json) return PTC_COMPANION_BAD_ARGUMENT;
    snprintf(path, sizeof(path), "%s/inbox/pending/%s.json", client->file.app_root, request_id);
    return client->file.storage->vtable->write_text_atomic(client->file.storage, path, json)
        ? PTC_COMPANION_OK : PTC_COMPANION_WRITE_FAILED;
}

static PtcCompanionStatus validate_result(const char *request_id, const char *json)
{
    cJSON *root;
    const cJSON *id;
    PtcCompanionStatus status = PTC_COMPANION_OK;
    if (ptc_result_validate(json) != PTC_ERR_OK) return PTC_COMPANION_RESULT_INVALID;
    root = cJSON_Parse(json);
    if (!root) return PTC_COMPANION_RESULT_INVALID;
    id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    if (!cJSON_IsString(id) || !id->valuestring) status = PTC_COMPANION_RESULT_INVALID;
    else if (strcmp(id->valuestring, request_id) != 0) status = PTC_COMPANION_RESULT_MISMATCH;
    cJSON_Delete(root);
    return status;
}

static void close_wait_token(PtcCompanionTransportClient *client)
{
    if (client->wait_token && client->ipc && client->ipc->close_wait) {
        client->ipc->close_wait(client->ipc_ctx, client->wait_token);
    }
    client->wait_token = NULL;
}

void ptc_companion_transport_init(PtcCompanionTransportClient *client, const char *app_root, PtcStorage *storage,
    const PtcCompanionIpcBackend *ipc, void *ipc_ctx)
{
    if (!client) return;
    memset(client, 0, sizeof(*client));
    ptc_companion_file_client_init(&client->file, app_root, storage);
    client->ipc = ipc;
    client->ipc_ctx = ipc_ctx;
}

PtcCompanionStatus ptc_companion_transport_submit_json(PtcCompanionTransportClient *client,
    const char *request_id, const char *json)
{
    PtcCompanionStatus status;
    bool ipc_connected;
    if (!client || !ptc_request_id_is_valid(request_id) || !json || strlen(json) > 4096) return PTC_COMPANION_BAD_ARGUMENT;
    ptc_companion_transport_cancel(client);
    snprintf(client->active_request_id, sizeof(client->active_request_id), "%s", request_id);
    ipc_connected = client->ipc && client->ipc->connect && client->ipc->connect(client->ipc_ctx);
    if (ipc_connected) {
        status = client->ipc->submit(client->ipc_ctx, request_id, json, &client->wait_token);
        if (status == PTC_COMPANION_OK) {
            client->active = PTC_TRANSPORT_IPC;
            client->accepted_by_ipc = true;
            return status;
        }
        if (status == PTC_COMPANION_PENDING) {
            /* Submit may have reached the server even if its reply was lost. Poll only
               this request's durable result so an in-flight request is never duplicated. */
            client->active = PTC_TRANSPORT_FILE;
            client->accepted_by_ipc = true;
            client->file_poll_delay_ms = 250;
            client->next_file_poll_ms = 250;
            return PTC_COMPANION_OK;
        }
        return status;
    }
    status = file_submit_json(client, request_id, json);
    if (status == PTC_COMPANION_OK) {
        client->active = PTC_TRANSPORT_FILE;
        client->file_poll_delay_ms = 250;
        client->next_file_poll_ms = 250;
    }
    return status;
}

PtcCompanionStatus ptc_companion_transport_poll(PtcCompanionTransportClient *client, int elapsed_ms,
    int timeout_ms, char *out, size_t out_size)
{
    PtcCompanionStatus status;
    if (!client || client->active == PTC_TRANSPORT_NONE || !out || out_size == 0) return PTC_COMPANION_BAD_ARGUMENT;
    if (elapsed_ms > 0) client->elapsed_ms += elapsed_ms;
    if (timeout_ms >= 0 && client->elapsed_ms >= timeout_ms) return PTC_COMPANION_TIMEOUT;
    if (client->active == PTC_TRANSPORT_IPC && client->ipc) {
        int event_status = client->ipc->event_status ? client->ipc->event_status(client->ipc_ctx, client->wait_token) : -1;
        if (event_status > 0) {
            status = client->ipc->get_result(client->ipc_ctx, client->active_request_id, out, out_size);
            if (status == PTC_COMPANION_OK) {
                status = validate_result(client->active_request_id, out);
                close_wait_token(client);
                return status;
            }
            if (status != PTC_COMPANION_PENDING) {
                close_wait_token(client);
                return status;
            }
        } else if (event_status == 0) {
            return PTC_COMPANION_PENDING;
        }
        close_wait_token(client);
        client->active = PTC_TRANSPORT_FILE;
        client->file_poll_delay_ms = 250;
        client->next_file_poll_ms = client->elapsed_ms;
    }
    if (client->elapsed_ms < client->next_file_poll_ms) return PTC_COMPANION_PENDING;
    status = ptc_companion_read_result(&client->file, client->active_request_id, client->elapsed_ms, timeout_ms, out, out_size);
    if (status == PTC_COMPANION_PENDING) {
        client->next_file_poll_ms = client->elapsed_ms + client->file_poll_delay_ms;
        if (client->file_poll_delay_ms < 500) client->file_poll_delay_ms = 500;
        else client->file_poll_delay_ms = 1000;
    }
    return status;
}

void ptc_companion_transport_cancel(PtcCompanionTransportClient *client)
{
    if (!client) return;
    close_wait_token(client);
    client->active = PTC_TRANSPORT_NONE;
    client->accepted_by_ipc = false;
    client->active_request_id[0] = '\0';
    client->elapsed_ms = 0;
}

bool ptc_companion_transport_notify_storage_changed(PtcCompanionTransportClient *client)
{
    return client && client->ipc && client->ipc->connect && client->ipc->connect(client->ipc_ctx) &&
        client->ipc->notify_storage_changed && client->ipc->notify_storage_changed(client->ipc_ctx);
}

PtcCompanionTransportKind ptc_companion_transport_active(const PtcCompanionTransportClient *client)
{
    return client ? client->active : PTC_TRANSPORT_NONE;
}

bool ptc_companion_transport_accepted_by_ipc(const PtcCompanionTransportClient *client)
{
    return client && client->accepted_by_ipc;
}

PtcCompanionStatus ptc_companion_transport_submit_status(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at)
{
    char json[512];
    if (ptc_companion_status_request_json(json, sizeof(json), request_id, created_at) >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_offline_code(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, const char *code)
{
    char json[640];
    if (ptc_companion_offline_code_request_json(json, sizeof(json), request_id, created_at, code) >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

static PtcCompanionStatus transport_submit_minutes(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, const char *type, uint16_t minutes)
{
    char json[512];
    if (ptc_companion_parent_minutes_request_json(json, sizeof(json), request_id, created_at, type, minutes) >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_set_today_limit(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, uint16_t minutes)
{ return transport_submit_minutes(client, request_id, created_at, "set_today_limit", minutes); }
PtcCompanionStatus ptc_companion_transport_submit_add_today_minutes(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, uint16_t minutes)
{ return transport_submit_minutes(client, request_id, created_at, "add_today_minutes", minutes); }
PtcCompanionStatus ptc_companion_transport_submit_parent_unlock_start(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, uint16_t minutes)
{ return transport_submit_minutes(client, request_id, created_at, "parent_unlock_start", minutes); }

PtcCompanionStatus ptc_companion_transport_submit_empty(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, const char *type)
{
    char json[512];
    if (ptc_companion_empty_payload_request_json(json, sizeof(json), request_id, created_at, type) >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_set_weekly_template(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, const PtcDayRule week[7])
{
    char json[1024];
    if (ptc_companion_set_weekly_template_request_json(json, sizeof(json), request_id, created_at, week) >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_set_bedtime(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, const PtcBedtimeRule *bedtime)
{
    char json[512];
    if (ptc_companion_set_bedtime_request_json(json, sizeof(json), request_id, created_at, bedtime) >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_set_limit_action(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, PtcLimitAction action)
{
    char json[512];
    if (ptc_companion_set_limit_action_request_json(json, sizeof(json), request_id, created_at, action) >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_probe_play_timer_effect(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, bool wait_for_expiry)
{
    char json[512];
    if (ptc_companion_probe_play_timer_effect_request_json(json, sizeof(json), request_id, created_at, wait_for_expiry) >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}
