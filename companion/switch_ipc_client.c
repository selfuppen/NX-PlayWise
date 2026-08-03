#ifdef __SWITCH__
#include <assert.h>
#ifndef static_assert
#define static_assert _Static_assert
#endif
#include "switch_ipc_client.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../common/protocol/ipc_protocol.h"

static bool backend_connect(void *ctx)
{
    PtcSwitchIpcClient *client = (PtcSwitchIpcClient *)ctx;
    u32 version = 0;
    if (!client) return false;
    if (!client->initialized) {
        if (R_FAILED(smGetService(&client->service, PTC_IPC_SERVICE_NAME))) return false;
        client->initialized = true;
    }
    if (R_FAILED(serviceDispatchOut(&client->service, PTC_IPC_CMD_GET_VERSION, version)) || version != PTC_IPC_INTERFACE_VERSION) {
        serviceClose(&client->service);
        client->initialized = false;
        return false;
    }
    return true;
}

static PtcCompanionStatus backend_submit(void *ctx, const char *request_id, const char *json, void **wait_token)
{
    PtcSwitchIpcClient *client = (PtcSwitchIpcClient *)ctx;
    struct { u32 request_length; } in;
    struct { u32 status; } out;
    Handle event_handle = INVALID_HANDLE;
    Event *event;
    Result rc;
    (void)request_id;
    in.request_length = (u32)strlen(json);
    rc = serviceDispatchInOut(&client->service, PTC_IPC_CMD_SUBMIT_REQUEST, in, out,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
        .buffers = { { json, in.request_length } },
        .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
        .out_handles = &event_handle);
    if (R_FAILED(rc)) {
        client->initialized = false;
        serviceClose(&client->service);
        return PTC_COMPANION_PENDING;
    }
    if (out.status == PTC_IPC_SUBMIT_CONFLICT || out.status == PTC_IPC_SUBMIT_INVALID) {
        if (event_handle != INVALID_HANDLE) svcCloseHandle(event_handle);
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    if (out.status == PTC_IPC_SUBMIT_STORAGE_FAILED || out.status == PTC_IPC_SUBMIT_TOO_MANY_WAITERS) {
        if (event_handle != INVALID_HANDLE) svcCloseHandle(event_handle);
        return PTC_COMPANION_WRITE_FAILED;
    }
    if (event_handle == INVALID_HANDLE) return PTC_COMPANION_PENDING;
    event = (Event *)malloc(sizeof(*event));
    if (!event) { svcCloseHandle(event_handle); return PTC_COMPANION_PENDING; }
    eventLoadRemote(event, event_handle, false);
    *wait_token = event;
    return PTC_COMPANION_OK;
}

static int backend_event_status(void *ctx, void *wait_token)
{
    Event *event = (Event *)wait_token;
    Result rc;
    (void)ctx;
    if (!event) return -1;
    rc = eventWait(event, 0);
    if (R_SUCCEEDED(rc)) return 1;
    return R_DESCRIPTION(rc) == KernelError_TimedOut ? 0 : -1;
}

static PtcCompanionStatus backend_get_result(void *ctx, const char *request_id, char *out_text, size_t out_size)
{
    PtcSwitchIpcClient *client = (PtcSwitchIpcClient *)ctx;
    struct { char request_id[80]; u32 buffer_size; } in = {{0}, 0};
    struct { u32 status; u32 actual_length; } out = {0, 0};
    Result rc;
    snprintf(in.request_id, sizeof(in.request_id), "%s", request_id);
    in.buffer_size = (u32)out_size;
    rc = serviceDispatchInOut(&client->service, PTC_IPC_CMD_GET_RESULT, in, out,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out_text, out_size } });
    if (R_FAILED(rc)) { client->initialized = false; serviceClose(&client->service); return PTC_COMPANION_PENDING; }
    if (out.status == PTC_IPC_RESULT_READY) {
        if (out.actual_length >= out_size) return PTC_COMPANION_RESULT_INVALID;
        out_text[out.actual_length] = '\0';
        return PTC_COMPANION_OK;
    }
    if (out.status == PTC_IPC_RESULT_BUFFER_TOO_SMALL) return PTC_COMPANION_RESULT_INVALID;
    if (out.status == PTC_IPC_RESULT_NOT_PERSISTED) return PTC_COMPANION_WRITE_FAILED;
    return PTC_COMPANION_PENDING;
}

static void backend_close_wait(void *ctx, void *wait_token)
{
    Event *event = (Event *)wait_token;
    (void)ctx;
    if (event) { eventClose(event); free(event); }
}

static bool backend_notify(void *ctx)
{
    PtcSwitchIpcClient *client = (PtcSwitchIpcClient *)ctx;
    return client && client->initialized && R_SUCCEEDED(serviceDispatch(&client->service, PTC_IPC_CMD_NOTIFY_STORAGE_CHANGED));
}

static const PtcCompanionIpcBackend BACKEND = {
    backend_connect, backend_submit, backend_event_status, backend_get_result, backend_close_wait, backend_notify,
};

void ptc_switch_ipc_client_init(PtcSwitchIpcClient *client) { if (client) memset(client, 0, sizeof(*client)); }
void ptc_switch_ipc_client_exit(PtcSwitchIpcClient *client) { if (client && client->initialized) serviceClose(&client->service); }
const PtcCompanionIpcBackend *ptc_switch_ipc_backend(void) { return &BACKEND; }
#endif
