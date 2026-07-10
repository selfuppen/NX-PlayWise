#ifndef PTC_TOKEN_V1_H
#define PTC_TOKEN_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "token_payload.h"
#include "../protocol/error_code.h"

#define PTC_TOKEN_TEXT_LEN 19
#define PTC_TOKEN_TEXT_SIZE 20
#define PTC_TOKEN_MAX_DAY_INDEX 0xffffu

typedef bool (*PtcNonceUsedFn)(uint16_t day_index, uint32_t nonce, void *ctx);

uint64_t ptc_token_pack_payload(const PtcTokenPayload *payload);
PtcErrorCode ptc_token_unpack_payload(uint64_t value, PtcTokenPayload *out);
PtcErrorCode ptc_token_encode(
    const PtcTokenPayload *payload,
    const char *device_id,
    const char *secret,
    char out[PTC_TOKEN_TEXT_SIZE]);
PtcErrorCode ptc_token_decode(
    const char *code,
    const char *device_id,
    const char *secret,
    PtcTokenPayload *out);
PtcErrorCode ptc_token_verify(
    const char *code,
    const char *device_id,
    const char *secret,
    uint16_t current_day_index,
    uint16_t max_add_minutes,
    PtcNonceUsedFn nonce_used,
    void *nonce_ctx,
    PtcTokenPayload *out);

#endif
