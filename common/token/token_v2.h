#ifndef PTC_TOKEN_V2_H
#define PTC_TOKEN_V2_H

#include <stddef.h>
#include <stdint.h>

#include "token_v1.h"

#define PTC_TOKEN_V2_VERSION 2
#define PTC_TOKEN_V2_TEXT_LEN 8
#define PTC_TOKEN_V2_TEXT_SIZE 9
#define PTC_TOKEN_V2_TIER_COUNT 24
#define PTC_TOKEN_V2_TIER_MINUTES 5
#define PTC_TOKEN_V2_MAX_MINUTES 120
#define PTC_TOKEN_V2_MAX_NONCE 0x1ffu
#define PTC_TOKEN_V2_MAC_BITS 12
#define PTC_TOKEN_V2_MAX_VALUE 0x3ffffffu

typedef struct {
    uint8_t tier_index;
    uint16_t minutes;
    uint16_t nonce;
} PtcTokenV2Payload;

PtcErrorCode ptc_token_v2_tier_for_minutes(uint16_t minutes, uint8_t *out_tier_index);
PtcErrorCode ptc_token_v2_encode(
    uint8_t tier_index,
    uint16_t nonce,
    const char *device_id,
    const char *secret,
    uint16_t day_index,
    char out[PTC_TOKEN_V2_TEXT_SIZE]);
PtcErrorCode ptc_token_v2_decode(
    const char *code,
    const char *device_id,
    const char *secret,
    uint16_t current_day_index,
    PtcTokenV2Payload *out);
PtcErrorCode ptc_token_v2_verify(
    const char *code,
    const char *device_id,
    const char *secret,
    uint16_t current_day_index,
    uint16_t max_add_minutes,
    PtcNonceUsedFn nonce_used,
    void *nonce_ctx,
    PtcTokenV2Payload *out);

#endif
