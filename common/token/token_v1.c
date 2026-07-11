#include "token_v1.h"

#include "../crypto/sha256.h"

#include <string.h>

static PtcErrorCode validate_payload(const PtcTokenPayload *payload)
{
    if (payload->version != 1) {
        return PTC_ERR_BAD_TOKEN_VERSION;
    }
    if (payload->action != PTC_TOKEN_ACTION_ADD_TODAY_MINUTES) {
        return PTC_ERR_UNSUPPORTED_TOKEN_ACTION;
    }
    if (payload->minutes == 0 || payload->minutes > PTC_TOKEN_MAX_MINUTES) {
        return PTC_ERR_BAD_CODE;
    }
    if (payload->nonce > PTC_TOKEN_MAX_NONCE) {
        return PTC_ERR_BAD_CODE;
    }
    return PTC_ERR_OK;
}

uint64_t ptc_token_pack_payload(const PtcTokenPayload *payload)
{
    uint64_t value = payload->version & 0xfu;
    value = (value << 4) | (payload->action & 0xfu);
    value = (value << 11) | (payload->minutes & 0x7ffu);
    value = (value << 16) | (payload->day_index_since_2020 & 0xffffu);
    value = (value << 25) | (payload->nonce & PTC_TOKEN_MAX_NONCE);
    return value;
}

PtcErrorCode ptc_token_unpack_payload(uint64_t value, PtcTokenPayload *out)
{
    out->nonce = (uint32_t)(value & PTC_TOKEN_MAX_NONCE);
    value >>= 25;
    out->day_index_since_2020 = (uint16_t)(value & 0xffffu);
    value >>= 16;
    out->minutes = (uint16_t)(value & 0x7ffu);
    value >>= 11;
    out->action = (uint8_t)(value & 0xfu);
    value >>= 4;
    out->version = (uint8_t)(value & 0xfu);
    return validate_payload(out);
}

static uint64_t calculate_mac(uint64_t payload_value, const char *device_id, const char *secret)
{
    uint8_t msg[4 + 128 + 1 + 8];
    uint8_t digest[PTC_SHA256_DIGEST_SIZE];
    size_t pos = 0;
    size_t device_len = strlen(device_id);
    size_t i;
    if (device_len > 127) {
        device_len = 127;
    }

    memcpy(msg + pos, PTC_TOKEN_DOMAIN, 4);
    pos += 4;
    memcpy(msg + pos, device_id, device_len);
    pos += device_len;
    msg[pos++] = 0;
    for (i = 0; i < 8; ++i) {
        msg[pos + i] = (uint8_t)(payload_value >> (56 - i * 8));
    }
    pos += 8;

    ptc_hmac_sha256((const uint8_t *)secret, strlen(secret), msg, pos, digest);
    return (((uint64_t)digest[0] << 32) | ((uint64_t)digest[1] << 24) | ((uint64_t)digest[2] << 16) | ((uint64_t)digest[3] << 8) | digest[4]) >> (40u - PTC_TOKEN_MAC_BITS);
}

static const char CROCKFORD[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static uint8_t token_bit(uint64_t high, uint64_t low, unsigned int bit)
{
    if (bit >= 64) {
        return (uint8_t)((high >> (bit - 64)) & 1u);
    }
    return (uint8_t)((low >> bit) & 1u);
}

static void set_token_bit(uint64_t *high, uint64_t *low, unsigned int bit)
{
    if (bit >= 64) {
        *high |= 1ull << (bit - 64);
    } else {
        *low |= 1ull << bit;
    }
}

static int crockford_value(char ch)
{
    unsigned int i;
    if (ch == 'O' || ch == 'o') {
        return 0;
    }
    if (ch == 'I' || ch == 'i' || ch == 'L' || ch == 'l') {
        return 1;
    }
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }
    for (i = 0; i < 32; ++i) {
        if (CROCKFORD[i] == ch) {
            return (int)i;
        }
    }
    return -1;
}

static void encode_base32(uint64_t high, uint64_t low, char out[PTC_TOKEN_TEXT_SIZE])
{
    unsigned int symbol;
    unsigned int pos = 0;
    for (symbol = 0; symbol < PTC_TOKEN_SYMBOLS; ++symbol) {
        unsigned int shift = PTC_TOKEN_TOTAL_BITS - 5 - symbol * 5;
        unsigned int j;
        uint8_t idx = 0;
        for (j = 0; j < 5; ++j) {
            idx = (uint8_t)((idx << 1) | token_bit(high, low, shift + 4 - j));
        }
        if (symbol > 0 && symbol % PTC_TOKEN_GROUP_SYMBOLS == 0) {
            out[pos++] = '-';
        }
        out[pos++] = CROCKFORD[idx];
    }
    out[pos] = '\0';
}

static PtcErrorCode decode_base32(const char *code, uint64_t *high, uint64_t *low)
{
    unsigned int symbols = 0;
    *high = 0;
    *low = 0;
    while (*code) {
        int value;
        unsigned int shift;
        unsigned int j;
        char ch = *code++;
        if (ch == '-' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            continue;
        }
        value = crockford_value(ch);
        if (value < 0 || symbols >= PTC_TOKEN_SYMBOLS) {
            return PTC_ERR_BAD_CODE;
        }
        shift = PTC_TOKEN_TOTAL_BITS - 5 - symbols * 5;
        for (j = 0; j < 5; ++j) {
            if ((value >> (4 - j)) & 1) {
                set_token_bit(high, low, shift + 4 - j);
            }
        }
        ++symbols;
    }
    if (symbols != PTC_TOKEN_SYMBOLS) {
        return PTC_ERR_BAD_CODE;
    }
    return PTC_ERR_OK;
}

PtcErrorCode ptc_token_encode(
    const PtcTokenPayload *payload,
    const char *device_id,
    const char *secret,
    char out[PTC_TOKEN_TEXT_SIZE])
{
    PtcErrorCode err = validate_payload(payload);
    uint64_t payload_value;
    uint64_t mac;
    uint64_t high;
    uint64_t low;
    if (err != PTC_ERR_OK) {
        return err;
    }
    payload_value = ptc_token_pack_payload(payload);
    mac = calculate_mac(payload_value, device_id, secret);
    high = payload_value >> 44;
    low = ((payload_value & 0xfffffffffffull) << PTC_TOKEN_MAC_BITS) | mac;
    encode_base32(high, low, out);
    return PTC_ERR_OK;
}

PtcErrorCode ptc_token_decode(
    const char *code,
    const char *device_id,
    const char *secret,
    PtcTokenPayload *out)
{
    uint64_t high;
    uint64_t low;
    uint64_t payload_value;
    uint64_t actual_mac;
    uint64_t expected_mac;
    PtcErrorCode err = decode_base32(code, &high, &low);
    if (err != PTC_ERR_OK) {
        return err;
    }
    payload_value = (high << 44) | (low >> PTC_TOKEN_MAC_BITS);
    actual_mac = low & ((1ull << PTC_TOKEN_MAC_BITS) - 1ull);
    expected_mac = calculate_mac(payload_value, device_id, secret);
    if (actual_mac != expected_mac) {
        return PTC_ERR_BAD_SIGNATURE;
    }
    return ptc_token_unpack_payload(payload_value, out);
}

PtcErrorCode ptc_token_verify(
    const char *code,
    const char *device_id,
    const char *secret,
    uint16_t current_day_index,
    uint16_t max_add_minutes,
    PtcNonceUsedFn nonce_used,
    void *nonce_ctx,
    PtcTokenPayload *out)
{
    PtcErrorCode err = ptc_token_decode(code, device_id, secret, out);
    if (err != PTC_ERR_OK) {
        return err;
    }
    if (out->day_index_since_2020 != current_day_index) {
        return PTC_ERR_WRONG_DATE;
    }
    if (out->minutes > max_add_minutes) {
        return PTC_ERR_MINUTES_EXCEED_LIMIT;
    }
    if (nonce_used && nonce_used(out->day_index_since_2020, out->nonce, nonce_ctx)) {
        return PTC_ERR_USED_TOKEN;
    }
    return PTC_ERR_OK;
}
