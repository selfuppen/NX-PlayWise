#ifdef __SWITCH__
#include "ipc_server.h"

#include <stdio.h>
#include <string.h>

#include "../common/protocol/ipc_protocol.h"
#include "../common/protocol/request_schema.h"

static void make_response(Result result, const void *data, size_t data_size)
{
    HipcRequest response = hipcMakeRequest(armGetTls(), (HipcMetadata){
        .type = 0,
        .num_data_words = (u32)((16 + sizeof(CmifOutHeader) + data_size + 3) / 4),
    });
    CmifOutHeader *header = (CmifOutHeader *)cmifGetAlignedDataStart(response.data_words, armGetTls());
    *header = (CmifOutHeader){ CMIF_OUT_HEADER_MAGIC, 0, result, 0 };
    if (data && data_size) memcpy(header + 1, data, data_size);
}

static void make_response_handle(Result result, const void *data, size_t data_size, Handle handle)
{
    HipcRequest response = hipcMakeRequest(armGetTls(), (HipcMetadata){
        .type = 0,
        .num_data_words = (u32)((16 + sizeof(CmifOutHeader) + data_size + 3) / 4),
        .num_copy_handles = 1,
    });
    CmifOutHeader *header;
    *response.copy_handles = handle;
    header = (CmifOutHeader *)cmifGetAlignedDataStart(response.data_words, armGetTls());
    *header = (CmifOutHeader){ CMIF_OUT_HEADER_MAGIC, 0, result, 0 };
    if (data && data_size) memcpy(header + 1, data, data_size);
}

static bool parse_request(HipcParsedRequest *hipc, CmifInHeader **header, void **data, size_t *data_size)
{
    const uint8_t *end;
    void *start;
    *hipc = hipcParseRequest(armGetTls());
    if (hipc->meta.type == CmifCommandType_Close) return false;
    start = cmifGetAlignedDataStart(hipc->data.data_words, armGetTls());
    end = (const uint8_t *)(hipc->data.data_words + hipc->meta.num_data_words);
    if ((const uint8_t *)start > end || (size_t)(end - (const uint8_t *)start) < sizeof(**header)) return false;
    *header = (CmifInHeader *)start;
    if ((*header)->magic != CMIF_IN_HEADER_MAGIC) return false;
    *data = *header + 1;
    *data_size = (size_t)(end - (const uint8_t *)*data);
    return true;
}

static void handle_get_version(void)
{
    u32 version = PTC_IPC_INTERFACE_VERSION;
    make_response(0, &version, sizeof(version));
}

static bool read_existing_request(PtcIpcServer *server, const char *request_id, char *out, size_t out_size)
{
    static const char *QUEUES[] = { "pending", "processing", "done" };
    size_t i;
    for (i = 0; i < sizeof(QUEUES) / sizeof(QUEUES[0]); ++i) {
        char path[320];
        snprintf(path, sizeof(path), "%s/inbox/%s/%s.json", server->sysmodule->app_root, QUEUES[i], request_id);
        if (server->sysmodule->storage->vtable->read_text(server->sysmodule->storage, path, out, out_size)) return true;
    }
    return false;
}

static void reclaim_signaled_waiters(PtcIpcServer *server)
{
    unsigned int i;
    /* A new request can only be received after the previous reply copied its
       Event handle, so completed waiter handles are now safe to release. */
    for (i = 0; i < PTC_IPC_MAX_WAITERS; ++i) {
        if (!server->waiters[i].active || !server->waiters[i].signaled) continue;
        eventClose(&server->waiters[i].completion);
        memset(&server->waiters[i], 0, sizeof(server->waiters[i]));
    }
}

static void handle_submit(PtcIpcServer *server, HipcParsedRequest *hipc, const void *data, size_t data_size)
{
    struct { u32 request_length; } in;
    struct { u32 status; } out = { PTC_IPC_SUBMIT_INVALID };
    const char *json;
    size_t buffer_size;
    char request_text[PTC_IPC_MAX_REQUEST_SIZE + 1];
    PtcRequest request;
    char pending_path[320];
    char existing[PTC_IPC_MAX_REQUEST_SIZE + 1];
    PtcIpcWaiter *waiter = NULL;
    Handle completion_handle = INVALID_HANDLE;
    bool result_exists = false;
    bool waiter_created = false;
    unsigned int i;
    if (data_size < sizeof(in)) {
        make_response_handle(0, &out, sizeof(out), server->error_event.revent);
        return;
    }
    memcpy(&in, data, sizeof(in));
    if (in.request_length == 0 || in.request_length > PTC_IPC_MAX_REQUEST_SIZE || hipc->meta.num_send_buffers < 1) {
        make_response_handle(0, &out, sizeof(out), server->error_event.revent);
        return;
    }
    json = (const char *)hipcGetBufferAddress(&hipc->data.send_buffers[0]);
    buffer_size = hipcGetBufferSize(&hipc->data.send_buffers[0]);
    if (!json || buffer_size < in.request_length) { make_response_handle(0, &out, sizeof(out), server->error_event.revent); return; }
    memcpy(request_text, json, in.request_length);
    request_text[in.request_length] = '\0';
    if (ptc_request_parse(request_text, &request) != PTC_ERR_OK) { make_response_handle(0, &out, sizeof(out), server->error_event.revent); return; }
    mutexLock(&server->storage_mutex);
    reclaim_signaled_waiters(server);
    if (!server->accepting) {
        mutexUnlock(&server->storage_mutex);
        out.status = PTC_IPC_SUBMIT_QUIESCING;
        make_response_handle(0, &out, sizeof(out), server->error_event.revent);
        return;
    }
    for (i = 0; i < PTC_IPC_MAX_WAITERS; ++i) {
        if (server->waiters[i].active && strcmp(server->waiters[i].request_id, request.request_id) == 0) { waiter = &server->waiters[i]; break; }
    }
    if (!waiter) {
        for (i = 0; i < PTC_IPC_MAX_WAITERS; ++i) if (!server->waiters[i].active) { waiter = &server->waiters[i]; break; }
        if (waiter && R_SUCCEEDED(eventCreate(&waiter->completion, false))) {
            waiter->active = true;
            waiter->signaled = false;
            snprintf(waiter->request_id, sizeof(waiter->request_id), "%s", request.request_id);
            waiter_created = true;
        } else {
            waiter = NULL;
        }
    }
    if (!waiter) {
        mutexUnlock(&server->storage_mutex);
        out.status = PTC_IPC_SUBMIT_TOO_MANY_WAITERS;
        make_response_handle(0, &out, sizeof(out), server->error_event.revent);
        return;
    }
    snprintf(pending_path, sizeof(pending_path), "%s/inbox/pending/%s.json", server->sysmodule->app_root, request.request_id);
    if (read_existing_request(server, request.request_id, existing, sizeof(existing))) {
        out.status = strcmp(existing, request_text) == 0 ? PTC_IPC_SUBMIT_DUPLICATE : PTC_IPC_SUBMIT_CONFLICT;
    } else {
        char result_path[320];
        snprintf(result_path, sizeof(result_path), "%s/results/%s.json", server->sysmodule->app_root, request.request_id);
        if (server->sysmodule->storage->vtable->exists(server->sysmodule->storage, result_path)) {
            out.status = PTC_IPC_SUBMIT_DUPLICATE;
        } else if (server->sysmodule->storage->vtable->write_text_atomic(server->sysmodule->storage, pending_path, request_text)) {
            out.status = PTC_IPC_SUBMIT_ACCEPTED;
        } else {
            out.status = PTC_IPC_SUBMIT_STORAGE_FAILED;
        }
    }
    {
        char result_path[320];
        snprintf(result_path, sizeof(result_path), "%s/results/%s.json", server->sysmodule->app_root, request.request_id);
        result_exists = server->sysmodule->storage->vtable->exists(server->sysmodule->storage, result_path);
        if (result_exists) { eventFire(&waiter->completion); waiter->signaled = true; }
        if (result_exists && (out.status == PTC_IPC_SUBMIT_ACCEPTED || out.status == PTC_IPC_SUBMIT_DUPLICATE)) {
            eventClose(&waiter->completion);
            memset(waiter, 0, sizeof(*waiter));
            waiter = NULL;
            completion_handle = server->completed_event.revent;
        } else {
            completion_handle = waiter->completion.revent;
        }
    }
    mutexUnlock(&server->storage_mutex);
    if (!result_exists && (out.status == PTC_IPC_SUBMIT_ACCEPTED || out.status == PTC_IPC_SUBMIT_DUPLICATE)) eventFire(&server->wake_event);
    if (out.status == PTC_IPC_SUBMIT_CONFLICT || out.status == PTC_IPC_SUBMIT_STORAGE_FAILED) {
        if (waiter_created) {
            mutexLock(&server->storage_mutex);
            eventClose(&waiter->completion);
            memset(waiter, 0, sizeof(*waiter));
            mutexUnlock(&server->storage_mutex);
        }
        make_response_handle(0, &out, sizeof(out), server->error_event.revent);
        return;
    }
    make_response_handle(0, &out, sizeof(out), completion_handle);
}

static void handle_get_result(PtcIpcServer *server, HipcParsedRequest *hipc, const void *data, size_t data_size)
{
    struct { char request_id[80]; u32 buffer_size; } in;
    struct { u32 status; u32 actual_length; } out = { PTC_IPC_RESULT_NOT_FOUND, 0 };
    char path[320];
    char result[PTC_IPC_MAX_RESULT_SIZE + 1];
    char *client_buffer;
    size_t client_size;
    unsigned int i;
    if (data_size < sizeof(in)) { make_response(0, &out, sizeof(out)); return; }
    memcpy(&in, data, sizeof(in));
    if (!memchr(in.request_id, '\0', sizeof(in.request_id)) || !ptc_request_id_is_valid(in.request_id) ||
        hipc->meta.num_recv_buffers < 1) { make_response(0, &out, sizeof(out)); return; }
    client_buffer = (char *)hipcGetBufferAddress(&hipc->data.recv_buffers[0]);
    client_size = hipcGetBufferSize(&hipc->data.recv_buffers[0]);
    if (!client_buffer || client_size == 0) { make_response(0, &out, sizeof(out)); return; }
    snprintf(path, sizeof(path), "%s/results/%s.json", server->sysmodule->app_root, in.request_id);
    mutexLock(&server->storage_mutex);
    for (i = 0; i < PTC_IPC_RESULT_CACHE_SIZE; ++i) {
        if (server->result_cache[i].active && strcmp(server->result_cache[i].request_id, in.request_id) == 0) {
            out.actual_length = server->result_cache[i].length;
            if (out.actual_length > in.buffer_size || out.actual_length > client_size) out.status = PTC_IPC_RESULT_BUFFER_TOO_SMALL;
            else { memcpy(client_buffer, server->result_cache[i].json, out.actual_length); out.status = PTC_IPC_RESULT_READY; }
            break;
        }
    }
    if (out.status == PTC_IPC_RESULT_READY || out.status == PTC_IPC_RESULT_BUFFER_TOO_SMALL) {
        /* cache hit: no SD read */
    } else if (server->sysmodule->storage->vtable->read_text(server->sysmodule->storage, path, result, sizeof(result))) {
        out.actual_length = (u32)strlen(result);
        if (out.actual_length > in.buffer_size || out.actual_length > client_size) out.status = PTC_IPC_RESULT_BUFFER_TOO_SMALL;
        else {
            PtcIpcResultCacheEntry *entry = &server->result_cache[server->result_cache_next++ % PTC_IPC_RESULT_CACHE_SIZE];
            memcpy(client_buffer, result, out.actual_length);
            snprintf(entry->request_id, sizeof(entry->request_id), "%s", in.request_id);
            memcpy(entry->json, result, out.actual_length + 1u);
            entry->length = out.actual_length;
            entry->active = true;
            out.status = PTC_IPC_RESULT_READY;
        }
    } else {
        snprintf(path, sizeof(path), "%s/inbox/pending/%s.json", server->sysmodule->app_root, in.request_id);
        if (server->sysmodule->storage->vtable->exists(server->sysmodule->storage, path)) out.status = PTC_IPC_RESULT_PENDING;
        else {
            snprintf(path, sizeof(path), "%s/inbox/processing/%s.json", server->sysmodule->app_root, in.request_id);
            if (server->sysmodule->storage->vtable->exists(server->sysmodule->storage, path)) out.status = PTC_IPC_RESULT_PENDING;
            else {
                snprintf(path, sizeof(path), "%s/inbox/done/%s.json", server->sysmodule->app_root, in.request_id);
                out.status = server->sysmodule->storage->vtable->exists(server->sysmodule->storage, path)
                    ? PTC_IPC_RESULT_NOT_PERSISTED : PTC_IPC_RESULT_NOT_FOUND;
            }
        }
    }
    mutexUnlock(&server->storage_mutex);
    make_response(0, &out, sizeof(out));
    if (out.status == PTC_IPC_RESULT_READY || out.status == PTC_IPC_RESULT_NOT_PERSISTED) {
        mutexLock(&server->storage_mutex);
        for (i = 0; i < PTC_IPC_MAX_WAITERS; ++i) {
            if (server->waiters[i].active && strcmp(server->waiters[i].request_id, in.request_id) == 0) {
                eventClose(&server->waiters[i].completion);
                memset(&server->waiters[i], 0, sizeof(server->waiters[i]));
                break;
            }
        }
        mutexUnlock(&server->storage_mutex);
    }
}

static bool dispatch(PtcIpcServer *server)
{
    HipcParsedRequest hipc;
    CmifInHeader *header;
    void *data;
    size_t data_size;
    if (!parse_request(&hipc, &header, &data, &data_size)) return false;
    switch (header->command_id) {
    case PTC_IPC_CMD_GET_VERSION: handle_get_version(); break;
    case PTC_IPC_CMD_SUBMIT_REQUEST: handle_submit(server, &hipc, data, data_size); break;
    case PTC_IPC_CMD_GET_RESULT: handle_get_result(server, &hipc, data, data_size); break;
    case PTC_IPC_CMD_NOTIFY_STORAGE_CHANGED: eventFire(&server->wake_event); make_response(0, NULL, 0); break;
    default: make_response(MAKERESULT(Module_Libnx, LibnxError_BadInput), NULL, 0); break;
    }
    return true;
}

static void ipc_thread(void *arg)
{
    PtcIpcServer *server = (PtcIpcServer *)arg;
    Handle reply = INVALID_HANDLE;
    while (server->running) {
        Handle handles[9];
        s32 index = -1;
        unsigned int i;
        Result rc;
        handles[0] = server->port;
        for (i = 0; i < server->session_count; ++i) handles[i + 1] = server->sessions[i];
        rc = svcReplyAndReceive(&index, handles, (s32)(server->session_count + 1), reply, UINT64_MAX);
        reply = INVALID_HANDLE;
        if (R_FAILED(rc)) {
            if (index > 0 && (unsigned int)index <= server->session_count) {
                svcCloseHandle(server->sessions[index - 1]);
                server->sessions[index - 1] = server->sessions[--server->session_count];
            }
            continue;
        }
        if (index == 0) {
            Handle session;
            if (R_SUCCEEDED(svcAcceptSession(&session, server->port))) {
                if (server->session_count < 8) server->sessions[server->session_count++] = session;
                else svcCloseHandle(session);
            }
        } else if (index > 0 && (unsigned int)index <= server->session_count) {
            Handle session = server->sessions[index - 1];
            if (dispatch(server)) reply = session;
            else {
                svcCloseHandle(session);
                server->sessions[index - 1] = server->sessions[--server->session_count];
            }
        }
    }
}

bool ptc_ipc_server_start(PtcIpcServer *server, PtcSysmodule *sysmodule)
{
    if (!server || !sysmodule) return false;
    memset(server, 0, sizeof(*server));
    server->sysmodule = sysmodule;
    mutexInit(&server->storage_mutex);
    if (R_FAILED(eventCreate(&server->wake_event, true))) return false;
    if (R_FAILED(eventCreate(&server->error_event, false))) { eventClose(&server->wake_event); return false; }
    if (R_FAILED(eventCreate(&server->completed_event, false))) {
        eventClose(&server->error_event);
        eventClose(&server->wake_event);
        return false;
    }
    eventFire(&server->completed_event);
    if (R_FAILED(smRegisterServiceCmif(&server->port, smEncodeName(PTC_IPC_SERVICE_NAME), false, 8))) {
        eventClose(&server->completed_event);
        eventClose(&server->error_event);
        eventClose(&server->wake_event);
        return false;
    }
    server->registered = true;
    server->running = true;
    server->accepting = true;
    if (R_FAILED(threadCreate(&server->thread, ipc_thread, server, NULL, 0x8000, 45, -2)) || R_FAILED(threadStart(&server->thread))) {
        ptc_ipc_server_stop(server);
        return false;
    }
    return true;
}

void ptc_ipc_server_stop(PtcIpcServer *server)
{
    unsigned int i;
    if (!server) return;
    server->running = false;
    for (i = 0; i < server->session_count; ++i) svcCloseHandle(server->sessions[i]);
    for (i = 0; i < PTC_IPC_MAX_WAITERS; ++i) if (server->waiters[i].active) eventClose(&server->waiters[i].completion);
    if (server->registered) { svcCloseHandle(server->port); smUnregisterServiceCmif(smEncodeName(PTC_IPC_SERVICE_NAME)); }
    eventClose(&server->completed_event);
    eventClose(&server->error_event);
    eventClose(&server->wake_event);
}

bool ptc_ipc_server_take_wake(PtcIpcServer *server)
{
    return server && R_SUCCEEDED(eventWait(&server->wake_event, 0));
}

bool ptc_ipc_server_wait(PtcIpcServer *server, uint32_t timeout_ms)
{
    if (!server) return false;
    return R_SUCCEEDED(eventWait(&server->wake_event, (u64)timeout_ms * 1000000ULL));
}

void ptc_ipc_server_signal_completed(PtcIpcServer *server)
{
    unsigned int i;
    if (!server) return;
    mutexLock(&server->storage_mutex);
    for (i = 0; i < PTC_IPC_MAX_WAITERS; ++i) {
        char path[320];
        char result[PTC_IPC_MAX_RESULT_SIZE + 1];
        if (!server->waiters[i].active || server->waiters[i].signaled) continue;
        snprintf(path, sizeof(path), "%s/results/%s.json", server->sysmodule->app_root, server->waiters[i].request_id);
        if (server->sysmodule->storage->vtable->read_text(server->sysmodule->storage, path, result, sizeof(result))) {
            PtcIpcResultCacheEntry *entry = &server->result_cache[server->result_cache_next++ % PTC_IPC_RESULT_CACHE_SIZE];
            snprintf(entry->request_id, sizeof(entry->request_id), "%s", server->waiters[i].request_id);
            snprintf(entry->json, sizeof(entry->json), "%s", result);
            entry->length = (uint32_t)strlen(result);
            entry->active = true;
            eventFire(&server->waiters[i].completion);
            server->waiters[i].signaled = true;
        } else {
            snprintf(path, sizeof(path), "%s/inbox/done/%s.json", server->sysmodule->app_root, server->waiters[i].request_id);
            if (server->sysmodule->storage->vtable->exists(server->sysmodule->storage, path)) {
                eventFire(&server->waiters[i].completion);
                server->waiters[i].signaled = true;
            }
        }
    }
    mutexUnlock(&server->storage_mutex);
}

void ptc_ipc_server_lock_storage(PtcIpcServer *server) { if (server) mutexLock(&server->storage_mutex); }
void ptc_ipc_server_unlock_storage(PtcIpcServer *server) { if (server) mutexUnlock(&server->storage_mutex); }
void ptc_ipc_server_set_accepting(PtcIpcServer *server, bool accepting)
{
    if (!server) return;
    mutexLock(&server->storage_mutex);
    server->accepting = accepting;
    mutexUnlock(&server->storage_mutex);
}
#endif
