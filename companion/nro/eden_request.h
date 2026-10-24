#ifndef PTC_EDEN_REQUEST_H
#define PTC_EDEN_REQUEST_H

#ifdef PLAYWISE_EDEN
#include <stdbool.h>
#include <string.h>
#include "../../common/protocol/ipc_protocol.h"
#include "../../common/protocol/request_schema.h"
#include "../file_protocol.h"

typedef struct {
    enum { PTC_EDEN_REQUEST_IDLE, PTC_EDEN_REQUEST_QUEUED, PTC_EDEN_REQUEST_RUNNING } state;
    char id[PTC_COMPANION_REQUEST_ID_SIZE];
    char json[PTC_IPC_MAX_REQUEST_SIZE + 1];
} PtcEdenRequest;

static inline bool ptc_eden_request_submit(PtcEdenRequest *slot, const char *id, const char *json)
{
    if (!slot || slot->state != PTC_EDEN_REQUEST_IDLE || !ptc_request_id_is_valid(id) || !json ||
        strlen(id) >= sizeof(slot->id) || !json[0] || strlen(json) > PTC_IPC_MAX_REQUEST_SIZE) return false;
    memcpy(slot->id, id, strlen(id) + 1);
    memcpy(slot->json, json, strlen(json) + 1);
    slot->state = PTC_EDEN_REQUEST_QUEUED;
    return true;
}

static inline bool ptc_eden_request_dispatch(PtcEdenRequest *slot, bool input_active, void *ctx,
    void (*process)(void *, const char *, const char *))
{
    if (!slot || input_active || slot->state != PTC_EDEN_REQUEST_QUEUED || !process) return false;
    /* One owned request, processed only after HID release. Mark running before
       entering the core; reentrancy or an unknown outcome must never replay it. */
    slot->state = PTC_EDEN_REQUEST_RUNNING;
    process(ctx, slot->id, slot->json);
    memset(slot, 0, sizeof(*slot));
    return true;
}
#endif
#endif
