#ifdef __SWITCH__
#include <assert.h>
#ifndef static_assert
#define static_assert _Static_assert
#endif
#include "switch_ipc_client.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../common/protocol/ipc_protocol.h"
#include "../common/time/ptc_time.h"

#if defined(PTC_IPC_CLIENT_OVERLAY)
#define PTC_IPC_CLIENT_NAME "overlay"
#elif defined(PTC_IPC_CLIENT_NRO)
#define PTC_IPC_CLIENT_NAME "nro"
#else
#define PTC_IPC_CLIENT_NAME "unknown"
#endif

static bool g_ipc_failure_logged;
static char g_ipc_failure_stage[32];
static Result g_ipc_failure_result;
static u32 g_ipc_failure_version;
static u64 g_ipc_log_boot_id;

static bool ipc_diagnostic_path(char *out, size_t out_size, int64_t now)
{
    char date[11];
    char directory[192];
    struct tm local;
    time_t local_now = (time_t)now;
    int written;
    if (!out || out_size == 0) return false;
    (void)mkdir(PLAYWISE_SD_ROOT "/logs", 0777);
    if (localtime_r(&local_now, &local) != NULL &&
        strftime(date, sizeof(date), "%Y-%m-%d", &local) == 10) {
        written = snprintf(directory, sizeof(directory), PLAYWISE_SD_ROOT "/logs/%s", date);
    } else {
        (void)mkdir(PLAYWISE_SD_ROOT "/logs/undated", 0777);
        if (g_ipc_log_boot_id == 0) g_ipc_log_boot_id = randomGet64();
        written = snprintf(directory, sizeof(directory), PLAYWISE_SD_ROOT "/logs/undated/%016llx",
            (unsigned long long)g_ipc_log_boot_id);
    }
    if (written <= 0 || (size_t)written >= sizeof(directory)) return false;
    (void)mkdir(directory, 0777);
    written = snprintf(out, out_size, "%s/ipc-client.log", directory);
    return written > 0 && (size_t)written < out_size;
}

static bool append_ipc_diagnostic(const char *stage, Result rc, u32 version)
{
    char path[224];
    int64_t now = (int64_t)time(NULL);
    FILE *file;
    int written;
    if (!ipc_diagnostic_path(path, sizeof(path), now)) return false;
    file = fopen(path, "ab");
    if (!file) return false;
    written = fprintf(file,
        "ts=%lld client=%s stage=%s rc=0x%08X module=%u description=%u version=%u expected=%u\n",
        (long long)now,
        PTC_IPC_CLIENT_NAME,
        stage,
        (unsigned int)rc,
        (unsigned int)R_MODULE(rc),
        (unsigned int)R_DESCRIPTION(rc),
        (unsigned int)version,
        (unsigned int)PTC_IPC_INTERFACE_VERSION);
    return fclose(file) == 0 && written > 0;
}

static void log_ipc_failure(const char *stage, Result rc, u32 version)
{
    if (g_ipc_failure_logged && g_ipc_failure_result == rc &&
        g_ipc_failure_version == version && strcmp(g_ipc_failure_stage, stage) == 0) {
        return;
    }
    if (!append_ipc_diagnostic(stage, rc, version)) return;
    snprintf(g_ipc_failure_stage, sizeof(g_ipc_failure_stage), "%s", stage);
    g_ipc_failure_result = rc;
    g_ipc_failure_version = version;
    g_ipc_failure_logged = true;
}

static void log_ipc_recovered(u32 version)
{
    if (!g_ipc_failure_logged) return;
    if (append_ipc_diagnostic("connected", 0, version)) {
        g_ipc_failure_logged = false;
        g_ipc_failure_stage[0] = '\0';
        g_ipc_failure_result = 0;
        g_ipc_failure_version = 0;
    }
}

static bool backend_connect(void *ctx)
{
    PtcSwitchIpcClient *client = (PtcSwitchIpcClient *)ctx;
    Result rc;
    u32 version = 0;
    if (!client || !client->sm_initialized) return false;
    if (!client->initialized) {
        rc = smGetService(&client->service, PTC_IPC_SERVICE_NAME);
        if (R_FAILED(rc)) {
            log_ipc_failure("smGetService", rc, 0);
            return false;
        }
        client->initialized = true;
    }
    rc = serviceDispatchOut(&client->service, PTC_IPC_CMD_GET_VERSION, version);
    if (R_FAILED(rc)) {
        log_ipc_failure("get_version_dispatch", rc, version);
        serviceClose(&client->service);
        client->initialized = false;
        return false;
    }
    if (version != PTC_IPC_INTERFACE_VERSION) {
        log_ipc_failure("version_mismatch", 0, version);
        serviceClose(&client->service);
        client->initialized = false;
        return false;
    }
    log_ipc_recovered(version);
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

void ptc_switch_ipc_client_init(PtcSwitchIpcClient *client)
{
    Result rc;
    if (!client) return;
    memset(client, 0, sizeof(*client));
    /* libtesla only lends initServices() an SM session. Retain our own
       reference because the pctc:u connection is opened later by the UI. */
    rc = smInitialize();
    if (R_FAILED(rc)) {
        log_ipc_failure("smInitialize", rc, 0);
        return;
    }
    client->sm_initialized = true;
}

void ptc_switch_ipc_client_exit(PtcSwitchIpcClient *client)
{
    if (!client) return;
    if (client->initialized) {
        serviceClose(&client->service);
        client->initialized = false;
    }
    if (client->sm_initialized) {
        smExit();
        client->sm_initialized = false;
    }
}
const PtcCompanionIpcBackend *ptc_switch_ipc_backend(void) { return &BACKEND; }
#endif
