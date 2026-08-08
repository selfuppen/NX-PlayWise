#ifndef PTC_COMPANION_AUTH_H
#define PTC_COMPANION_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../platform/storage.h"
#include "file_protocol.h"

#define PTC_AUTH_PIN_MIN_LEN 1
#define PTC_AUTH_PIN_MAX_LEN 64
#define PTC_AUTH_FAILURE_LIMIT 5
#define PTC_AUTH_INITIAL_COOLDOWN_SECONDS 30
#define PTC_AUTH_MAX_COOLDOWN_SECONDS 600
#define PTC_AUTH_SALT_LEN 16
#define PTC_AUTH_SALT_HEX_LEN 32
#define PTC_AUTH_HASH_HEX_LEN 64

typedef bool (*PtcAuthRandomFn)(uint8_t *out, size_t out_size, void *ctx);

typedef enum {
    PTC_AUTH_OK = 0,
    PTC_AUTH_EMPTY = 1,
    PTC_AUTH_BAD_ARGUMENT = 2,
    PTC_AUTH_READ_FAILED = 3,
    PTC_AUTH_WRITE_FAILED = 4,
    PTC_AUTH_INVALID_FILE = 5,
    PTC_AUTH_DENIED = 6,
    PTC_AUTH_COOLDOWN = 7,
} PtcAuthStatus;

typedef struct {
    char app_root[96];
    PtcStorage *storage;
} PtcCompanionAuth;

void ptc_companion_auth_init(PtcCompanionAuth *auth, const char *app_root, PtcStorage *storage);
PtcAuthStatus ptc_companion_auth_state(PtcCompanionAuth *auth);
PtcAuthStatus ptc_companion_auth_set_pin(
    PtcCompanionAuth *auth,
    const char *pin,
    int64_t updated_at,
    PtcAuthRandomFn random_fn,
    void *random_ctx);
PtcAuthStatus ptc_companion_auth_verify_pin(
    PtcCompanionAuth *auth,
    const char *pin,
    int64_t now,
    int64_t *retry_after_seconds);
const char *ptc_auth_status_name(PtcAuthStatus status);

#endif
