#ifndef PTC_TEST_UPDATE_MD5_H
#define PTC_TEST_UPDATE_MD5_H
#include <stddef.h>
#include <string.h>
/* Isolate the external crypto library, not the production comparison or parser.
   These tests exercise package handling; upstream MD5 correctness is not claimed. */
typedef struct { int unused; } mbedtls_md5_context;
static inline void mbedtls_md5_init(mbedtls_md5_context *ctx) { (void)ctx; }
static inline void mbedtls_md5_free(mbedtls_md5_context *ctx) { (void)ctx; }
static inline int mbedtls_md5_starts_ret(mbedtls_md5_context *ctx) { (void)ctx; return 0; }
static inline int mbedtls_md5_update_ret(mbedtls_md5_context *ctx, const unsigned char *data, size_t length)
{ (void)ctx; (void)data; (void)length; return 0; }
static inline int mbedtls_md5_finish_ret(mbedtls_md5_context *ctx, unsigned char out[16])
{ (void)ctx; memset(out, 0, 16); return 0; }
#endif
