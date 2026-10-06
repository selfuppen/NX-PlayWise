#include "transport_client.h"
#include "ui_language.h"

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
        client->route = PTC_TRANSPORT_ROUTE_IPC;
        status = client->ipc->submit(client->ipc_ctx, request_id, json, &client->wait_token);
        if (status == PTC_COMPANION_OK) {
            client->active = PTC_TRANSPORT_IPC;
            return status;
        }
        if (status == PTC_COMPANION_PENDING) {
            /* Submit may have reached the server even if its reply was lost. Poll only
               this request's durable result so an in-flight request is never duplicated. */
            client->active = PTC_TRANSPORT_FILE;
            client->route = PTC_TRANSPORT_ROUTE_IPC_SD_RESULT;
            client->file_poll_delay_ms = 100;
            client->next_file_poll_ms = 100;
            return PTC_COMPANION_OK;
        }
        return status;
    }
    client->route = PTC_TRANSPORT_ROUTE_SD_QUEUE;
    status = file_submit_json(client, request_id, json);
    if (status == PTC_COMPANION_OK) {
        client->active = PTC_TRANSPORT_FILE;
        client->file_poll_delay_ms = 100;
        client->next_file_poll_ms = 100;
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
        client->route = PTC_TRANSPORT_ROUTE_IPC_SD_RESULT;
        client->file_poll_delay_ms = 100;
        client->next_file_poll_ms = client->elapsed_ms;
    }
    if (client->elapsed_ms < client->next_file_poll_ms) return PTC_COMPANION_PENDING;
    status = ptc_companion_read_result(&client->file, client->active_request_id, client->elapsed_ms, timeout_ms, out, out_size);
    if (status == PTC_COMPANION_PENDING) {
        /* SDMC result checks are cheap; keep the fallback responsive instead
         * of adding up to seconds of exponential backoff to every refresh. */
        client->file_poll_delay_ms = 100;
        client->next_file_poll_ms = client->elapsed_ms + client->file_poll_delay_ms;
    }
    return status;
}

void ptc_companion_transport_cancel(PtcCompanionTransportClient *client)
{
    if (!client) return;
    close_wait_token(client);
    client->active = PTC_TRANSPORT_NONE;
    client->active_request_id[0] = '\0';
    client->elapsed_ms = 0;
}

bool ptc_companion_transport_notify_storage_changed(PtcCompanionTransportClient *client)
{
    return client && client->ipc && client->ipc->connect && client->ipc->connect(client->ipc_ctx) &&
        client->ipc->notify_storage_changed && client->ipc->notify_storage_changed(client->ipc_ctx);
}

PtcCompanionTransportRoute ptc_companion_transport_route(const PtcCompanionTransportClient *client)
{
    return client ? client->route : PTC_TRANSPORT_ROUTE_NONE;
}

const char *ptc_companion_transport_route_label_zh(PtcCompanionTransportRoute route)
{
    switch (route) {
    case PTC_TRANSPORT_ROUTE_IPC:
        return ptc_ui_text(PTC_UI_T_TRANSPORT_IPC);
    case PTC_TRANSPORT_ROUTE_SD_QUEUE:
        return ptc_ui_text(PTC_UI_T_TRANSFER_SD_FILE_QUEUE);
    case PTC_TRANSPORT_ROUTE_IPC_SD_RESULT:
        return ptc_ui_text(PTC_UI_T_TRANSMISSION_IPC_SD_RESULT_READBACK);
    case PTC_TRANSPORT_ROUTE_LOCAL_SD_FLAG:
        return ptc_ui_text(PTC_UI_T_EXECUTION_MODE_LOCAL_SD_FLAG_FILE);
    case PTC_TRANSPORT_ROUTE_NONE:
    default:
        return ptc_ui_text(PTC_UI_T_TRANSFER_NOT_STARTED);
    }
}

const char *ptc_companion_request_command_label_zh(const char *type)
{
    if (!type) return ptc_ui_text(PTC_UI_T_BACKGROUND_OPERATION);
    if (strcmp(type, "status") == 0) return ptc_ui_text(PTC_UI_T_REFRESH_STATUS);
    if (strcmp(type, "preview_offline_code") == 0) return ptc_ui_text(PTC_UI_T_PREVIEW_TODAY_S_GRANT);
    if (strcmp(type, "offline_code") == 0) return ptc_ui_text(PTC_UI_T_SUBMIT_TODAY_S_GRANT);
    if (strcmp(type, "clear_redemption_history") == 0) return ptc_ui_text(PTC_UI_T_CLEAR_GRANT_CODE_USAGE_RECORDS);
    if (strcmp(type, "set_scheduled_override") == 0) return ptc_ui_text(PTC_UI_T_SET_THE_QUOTA_FOR_THE_SPECIFIED_DATE);
    if (strcmp(type, "set_autonomy_policy") == 0) return ptc_ui_text(PTC_UI_T_SET_TODAY_S_INDEPENDENT_BUFFERING);
    if (strcmp(type, "claim_daily_buffer") == 0) return ptc_ui_text(PTC_UI_T_GET_TODAY_S_INDEPENDENT_BUFFER);
    if (strcmp(type, "clear_activity_history") == 0) return ptc_ui_text(PTC_UI_T_CLEAR_FAMILY_ACTIVITY_RECORDS);
    if (strcmp(type, "set_today_limit") == 0) return ptc_ui_text(PTC_UI_T_SET_TODAY_S_TOTAL_QUOTA);
    if (strcmp(type, "add_today_minutes") == 0) return ptc_ui_text(PTC_UI_T_TEMPORARY_GRANT);
    if (strcmp(type, "disable_today_limit") == 0) return ptc_ui_text(PTC_UI_T_LIFT_CURRENT_RESTRICTIONS);
    if (strcmp(type, "restore_today_policy") == 0) return ptc_ui_text(PTC_UI_T_CLEAR_TODAY_S_QUOTA_ADJUSTMENT);
    if (strcmp(type, "set_weekly_template") == 0) return ptc_ui_text(PTC_UI_T_WEEKLY_PLAN);
    if (strcmp(type, "set_holiday_policy") == 0) return ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAY_SETTINGS);
    if (strcmp(type, "complete_setup") == 0) return ptc_ui_text(PTC_UI_T_ENABLE_AUTOMATIC_CONTROL);
    if (strcmp(type, "retry_setup_release") == 0) return ptc_ui_text(PTC_UI_T_RETRY_PRE_LIMITING);
    if (strcmp(type, "restore_install_snapshot") == 0) return ptc_ui_text(PTC_UI_T_RESTORE_ORIGINAL_STATE);
    if (strcmp(type, "set_bedtime_policy") == 0) return ptc_ui_text(PTC_UI_T_SAVE_BEDTIME_PLAN);
    if (strcmp(type, "skip_bedtime") == 0) return ptc_ui_text(PTC_UI_T_SKIP_THIS_BEDTIME_RESTRICTION);
    if (strcmp(type, "clear_bedtime_skip") == 0) return ptc_ui_text(PTC_UI_T_RESTORE_THIS_BEDTIME_LIMIT);
    if (strcmp(type, "disable_bedtime") == 0) return ptc_ui_text(PTC_UI_T_TURN_OFF_BEDTIME_PLAN);
    if (strcmp(type, "confirm_bedtime_requirements") == 0) return ptc_ui_text(PTC_UI_T_IDENTIFY_BEDTIME_RESTRICTION_RISKS);
    if (strcmp(type, "overlay_ready") == 0) return ptc_ui_text(PTC_UI_T_VERIFY_FLOATING_WINDOW_RECOVERY_CHANNEL);
    return ptc_ui_text(PTC_UI_T_BACKGROUND_OPERATION);
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

PtcCompanionStatus ptc_companion_transport_submit_set_holiday_policy(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, bool enabled, PtcDayRule holiday_rule, PtcDayRule makeup_workday_rule)
{
    char json[768];
    if (ptc_companion_set_holiday_policy_request_json(json, sizeof(json), request_id, created_at,
            enabled, holiday_rule, makeup_workday_rule) >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_preview_offline_code(PtcCompanionTransportClient *client, const char *request_id, int64_t created_at, const char *code)
{
    char json[640];
    if (ptc_companion_preview_offline_code_request_json(json, sizeof(json), request_id, created_at, code) >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_set_scheduled_override(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, const PtcScheduledOverride *scheduled_override)
{
    char json[768];
    int written = ptc_companion_set_scheduled_override_request_json(
        json, sizeof(json), request_id, created_at, scheduled_override);
    if (written < 0 || written >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_set_autonomy_policy(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, const PtcAutonomyPolicy *policy)
{
    char json[512];
    int written = ptc_companion_set_autonomy_policy_request_json(
        json, sizeof(json), request_id, created_at, policy);
    if (written < 0 || written >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_set_eye_care_policy(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, const PtcEyeCarePolicy *policy)
{
    char json[512];
    int written = ptc_companion_set_eye_care_policy_request_json(
        json, sizeof(json), request_id, created_at, policy);
    if (written < 0 || written >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_skip_eye_care_break(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, uint64_t break_id)
{
    char json[512];
    int written = ptc_companion_skip_eye_care_break_request_json(
        json, sizeof(json), request_id, created_at, break_id);
    if (written < 0 || written >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_set_bedtime_policy(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, const PtcBedtimePolicy *policy, bool apply_immediately)
{
    char json[4096];
    int written = ptc_companion_set_bedtime_policy_request_json(
        json, sizeof(json), request_id, created_at, policy, apply_immediately);
    if (written < 0 || written >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_skip_bedtime(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, uint64_t window_instance_id)
{
    char json[512];
    int written = ptc_companion_skip_bedtime_request_json(
        json, sizeof(json), request_id, created_at, window_instance_id);
    if (written < 0 || written >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_clear_bedtime_skip(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, uint64_t window_instance_id)
{
    char json[512];
    int written = ptc_companion_clear_bedtime_skip_request_json(
        json, sizeof(json), request_id, created_at, window_instance_id);
    if (written < 0 || written >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_confirm_bedtime_requirements(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, bool official_setting_confirmed,
    bool overlay_risk_accepted, uint16_t confirmation_version, const char *environment_fingerprint)
{
    char json[768];
    int written = ptc_companion_confirm_bedtime_requirements_request_json(
        json, sizeof(json), request_id, created_at, official_setting_confirmed,
        overlay_risk_accepted, confirmation_version, environment_fingerprint);
    if (written < 0 || written >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_overlay_ready(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, const char *release_id,
    const char *boot_id, const char *environment_fingerprint)
{
    char json[768];
    int written = ptc_companion_overlay_ready_request_json(
        json, sizeof(json), request_id, created_at, release_id, boot_id, environment_fingerprint);
    if (written < 0 || written >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_set_dock_policy(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, const PtcDockPolicy *policy)
{
    char json[512];
    int written = ptc_companion_set_dock_policy_request_json(json, sizeof(json), request_id, created_at, policy);
    if (written < 0 || written >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_transport_submit_waive_dock_policy(PtcCompanionTransportClient *client,
    const char *request_id, int64_t created_at, uint16_t day_index)
{
    char json[512];
    int written = ptc_companion_waive_dock_policy_request_json(json, sizeof(json), request_id, created_at, day_index);
    if (written < 0 || written >= (int)sizeof(json)) return PTC_COMPANION_BAD_ARGUMENT;
    return ptc_companion_transport_submit_json(client, request_id, json);
}
