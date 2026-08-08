#ifndef PTC_COMMON_SECURITY_CREDENTIAL_POLICY_H
#define PTC_COMMON_SECURITY_CREDENTIAL_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PTC_DEVICE_ID_MAX_LEN 32
#define PTC_GRANT_SECRET_MIN_LEN 32
#define PTC_GRANT_SECRET_MAX_LEN 64
#define PTC_DEMO_GRANT_SECRET "playwise-public-demo-secret-0001"
#define PTC_PAIRING_BASE_URL "https://selfuppen.github.io/playwise/"

bool ptc_device_id_valid(const char *value);
bool ptc_grant_secret_valid(const char *value);
bool ptc_grant_secret_is_demo(const char *value);
bool ptc_hex_from_random(const uint8_t *bytes, size_t byte_count, char *out, size_t out_size);
bool ptc_random_device_id(const uint8_t bytes[3], char *out, size_t out_size);
bool ptc_build_pairing_url(
    const char *device_id,
    const char *grant_secret,
    char *out,
    size_t out_size);

#endif
