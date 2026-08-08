#include "token_v2.h"

#include "../crypto/sha256.h"

#include <stdio.h>
#include <string.h>

static uint16_t calculate_mac(
    const char *device_id,
    const char *secret,
    uint16_t day_index,
    uint8_t tier_index,
    uint16_t nonce)
{
    uint8_t msg[4 + 128 + 1 + 2 + 1 + 2];
    uint8_t digest[PTC_SHA256_DIGEST_SIZE];
    size_t device_len = strlen(device_id);
    size_t pos = 0;
    if (device_len > 127) device_len = 127;
    memcpy(msg + pos, "PTC2", 4);
    pos += 4;
    memcpy(msg + pos, device_id, device_len);
    pos += device_len;
    msg[pos++] = 0;
    /* Integers in the v2 MAC transcript use fixed-width big-endian bytes. */
    msg[pos++] = (uint8_t)(day_index >> 8);
    msg[pos++] = (uint8_t)day_index;
    msg[pos++] = tier_index;
    msg[pos++] = (uint8_t)(nonce >> 8);
    msg[pos++] = (uint8_t)nonce;
    ptc_hmac_sha256((const uint8_t *)secret, strlen(secret), msg, pos, digest);
    return (uint16_t)((((uint16_t)digest[0] << 8) | digest[1]) >> 4);
}

PtcErrorCode ptc_token_v2_tier_for_minutes(uint16_t minutes, uint8_t *out_tier_index)
{
    if (!out_tier_index) {
        return PTC_ERR_BAD_CODE;
    }
    if (minutes >= 1 && minutes <= 4) {
        *out_tier_index = (uint8_t)(23u + minutes);
        return PTC_ERR_OK;
    }
    if (minutes >= PTC_TOKEN_V2_TIER_MINUTES && minutes <= 120 &&
        minutes % PTC_TOKEN_V2_TIER_MINUTES == 0) {
        *out_tier_index = (uint8_t)(minutes / PTC_TOKEN_V2_TIER_MINUTES - 1u);
        return PTC_ERR_OK;
    }
    if (minutes == 150) { *out_tier_index = 28; return PTC_ERR_OK; }
    if (minutes == 180) { *out_tier_index = 29; return PTC_ERR_OK; }
    if (minutes == 210) { *out_tier_index = 30; return PTC_ERR_OK; }
    if (minutes == 240) { *out_tier_index = 31; return PTC_ERR_OK; }
    return PTC_ERR_BAD_CODE;
}

PtcErrorCode ptc_token_v2_encode(
    uint8_t tier_index,
    uint16_t nonce,
    const char *device_id,
    const char *secret,
    uint16_t day_index,
    char out[PTC_TOKEN_V2_TEXT_SIZE])
{
    uint32_t value;
    uint16_t mac;
    if (!device_id || !secret || !out || tier_index >= PTC_TOKEN_V2_TIER_COUNT ||
        nonce > PTC_TOKEN_V2_MAX_NONCE) {
        return PTC_ERR_BAD_CODE;
    }
    mac = calculate_mac(device_id, secret, day_index, tier_index, nonce);
    value = ((uint32_t)tier_index << 21) | ((uint32_t)nonce << 12) | mac;
    snprintf(out, PTC_TOKEN_V2_TEXT_SIZE, "%08lu", (unsigned long)value);
    return PTC_ERR_OK;
}

static PtcErrorCode parse_value(const char *code, uint32_t *out)
{
    uint32_t value = 0;
    unsigned int index;
    if (!code || !out || strlen(code) != PTC_TOKEN_V2_TEXT_LEN) return PTC_ERR_BAD_CODE;
    for (index = 0; index < PTC_TOKEN_V2_TEXT_LEN; ++index) {
        if (code[index] < '0' || code[index] > '9') return PTC_ERR_BAD_CODE;
        value = value * 10u + (uint32_t)(code[index] - '0');
    }
    if (value > PTC_TOKEN_V2_MAX_VALUE) return PTC_ERR_BAD_CODE;
    *out = value;
    return PTC_ERR_OK;
}

PtcErrorCode ptc_token_v2_decode(
    const char *code,
    const char *device_id,
    const char *secret,
    uint16_t current_day_index,
    PtcTokenV2Payload *out)
{
    uint32_t value;
    uint16_t actual_mac;
    uint16_t expected_mac;
    PtcErrorCode err;
    if (!device_id || !secret || !out) return PTC_ERR_BAD_CODE;
    err = parse_value(code, &value);
    if (err != PTC_ERR_OK) return err;
    out->tier_index = (uint8_t)(value >> 21);
    out->nonce = (uint16_t)((value >> 12) & PTC_TOKEN_V2_MAX_NONCE);
    actual_mac = (uint16_t)(value & ((1u << PTC_TOKEN_V2_MAC_BITS) - 1u));
    if (out->tier_index >= PTC_TOKEN_V2_TIER_COUNT) return PTC_ERR_BAD_CODE;
    expected_mac = calculate_mac(device_id, secret, current_day_index, out->tier_index, out->nonce);
    if (actual_mac != expected_mac) return PTC_ERR_BAD_SIGNATURE;
    if (out->tier_index < 24) {
        out->minutes = (uint16_t)((out->tier_index + 1u) * PTC_TOKEN_V2_TIER_MINUTES);
    } else if (out->tier_index <= 27) {
        out->minutes = (uint16_t)(out->tier_index - 23u);
    } else if (out->tier_index == 28) {
        out->minutes = 150;
    } else if (out->tier_index == 29) {
        out->minutes = 180;
    } else if (out->tier_index == 30) {
        out->minutes = 210;
    } else if (out->tier_index == 31) {
        out->minutes = 240;
    } else {
        return PTC_ERR_BAD_CODE;
    }
    return PTC_ERR_OK;
}

PtcErrorCode ptc_token_v2_verify(
    const char *code,
    const char *device_id,
    const char *secret,
    uint16_t current_day_index,
    uint16_t max_add_minutes,
    PtcNonceUsedFn nonce_used,
    void *nonce_ctx,
    PtcTokenV2Payload *out)
{
    PtcErrorCode err = ptc_token_v2_decode(code, device_id, secret, current_day_index, out);
    if (err != PTC_ERR_OK) return err;
    if (out->minutes > max_add_minutes) return PTC_ERR_MINUTES_EXCEED_LIMIT;
    if (nonce_used && nonce_used(current_day_index, out->nonce, nonce_ctx)) return PTC_ERR_USED_TOKEN;
    return PTC_ERR_OK;
}
