#include "sha256.h"

#include <string.h>

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

static void sha256_transform(PtcSha256Ctx *ctx, const uint8_t data[])
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

void ptc_sha256_init(PtcSha256Ctx *ctx)
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

void ptc_sha256_update(PtcSha256Ctx *ctx, const uint8_t data[], size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == PTC_SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

void ptc_sha256_final(PtcSha256Ctx *ctx, uint8_t hash[PTC_SHA256_DIGEST_SIZE])
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

void ptc_hmac_sha256(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *msg,
    size_t msg_len,
    uint8_t out[PTC_SHA256_DIGEST_SIZE])
{
    uint8_t kopad[PTC_SHA256_BLOCK_SIZE];
    uint8_t kipad[PTC_SHA256_BLOCK_SIZE];
    uint8_t key_hash[PTC_SHA256_DIGEST_SIZE];
    PtcSha256Ctx ctx;
    size_t i;

    if (key_len > PTC_SHA256_BLOCK_SIZE) {
        ptc_sha256_init(&ctx);
        ptc_sha256_update(&ctx, key, key_len);
        ptc_sha256_final(&ctx, key_hash);
        key = key_hash;
        key_len = PTC_SHA256_DIGEST_SIZE;
    }

    memset(kopad, 0x5c, sizeof(kopad));
    memset(kipad, 0x36, sizeof(kipad));
    for (i = 0; i < key_len; ++i) {
        kopad[i] ^= key[i];
        kipad[i] ^= key[i];
    }

    ptc_sha256_init(&ctx);
    ptc_sha256_update(&ctx, kipad, sizeof(kipad));
    ptc_sha256_update(&ctx, msg, msg_len);
    ptc_sha256_final(&ctx, key_hash);

    ptc_sha256_init(&ctx);
    ptc_sha256_update(&ctx, kopad, sizeof(kopad));
    ptc_sha256_update(&ctx, key_hash, sizeof(key_hash));
    ptc_sha256_final(&ctx, out);
}
