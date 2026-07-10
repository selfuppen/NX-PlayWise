#include "token_v1.h"

#include <string.h>

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint8_t data[SHA256_BLOCK_SIZE];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} Sha256Ctx;

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t rotr(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void sha256_transform(Sha256Ctx *ctx, const uint8_t data[])
{
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];
    for (i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) | ((uint32_t)data[j + 2] << 8) | data[j + 3];
    }
    for (; i < 64; ++i) {
        uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + s1 + ch + K[i] + m[i];
        t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(Sha256Ctx *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
}

static void sha256_update(Sha256Ctx *ctx, const uint8_t data[], size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(Sha256Ctx *ctx, uint8_t hash[SHA256_DIGEST_SIZE])
{
    uint32_t i = ctx->datalen;
    uint32_t j;

    ctx->data[i++] = 0x80;
    if (ctx->datalen < 56) {
        while (i < 56) {
            ctx->data[i++] = 0x00;
        }
    } else {
        while (i < 64) {
            ctx->data[i++] = 0x00;
        }
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += (uint64_t)ctx->datalen * 8u;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 8; ++j) {
            hash[i + (j * 4)] = (uint8_t)((ctx->state[j] >> (24 - i * 8)) & 0xff);
        }
    }
}

static void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len, uint8_t out[SHA256_DIGEST_SIZE])
{
    uint8_t kopad[SHA256_BLOCK_SIZE];
    uint8_t kipad[SHA256_BLOCK_SIZE];
    uint8_t key_hash[SHA256_DIGEST_SIZE];
    Sha256Ctx ctx;
    size_t i;

    if (key_len > SHA256_BLOCK_SIZE) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, key_hash);
        key = key_hash;
        key_len = SHA256_DIGEST_SIZE;
    }

    memset(kopad, 0x5c, sizeof(kopad));
    memset(kipad, 0x36, sizeof(kipad));
    for (i = 0; i < key_len; ++i) {
        kopad[i] ^= key[i];
        kipad[i] ^= key[i];
    }

    sha256_init(&ctx);
    sha256_update(&ctx, kipad, sizeof(kipad));
    sha256_update(&ctx, msg, msg_len);
    sha256_final(&ctx, key_hash);

    sha256_init(&ctx);
    sha256_update(&ctx, kopad, sizeof(kopad));
    sha256_update(&ctx, key_hash, sizeof(key_hash));
    sha256_final(&ctx, out);
}

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
    uint8_t digest[SHA256_DIGEST_SIZE];
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

    hmac_sha256((const uint8_t *)secret, strlen(secret), msg, pos, digest);
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
        if (symbol > 0 && symbol % 5 == 0) {
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
