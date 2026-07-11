#ifndef PTC_SHA256_H
#define PTC_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define PTC_SHA256_BLOCK_SIZE 64
#define PTC_SHA256_DIGEST_SIZE 32

typedef struct {
    uint8_t data[PTC_SHA256_BLOCK_SIZE];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} PtcSha256Ctx;

void ptc_sha256_init(PtcSha256Ctx *ctx);
void ptc_sha256_update(PtcSha256Ctx *ctx, const uint8_t data[], size_t len);
void ptc_sha256_final(PtcSha256Ctx *ctx, uint8_t hash[PTC_SHA256_DIGEST_SIZE]);
void ptc_hmac_sha256(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *msg,
    size_t msg_len,
    uint8_t out[PTC_SHA256_DIGEST_SIZE]);

#endif
