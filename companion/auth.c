#include "auth.h"

#include <stdio.h>
#include <string.h>

#include "../common/crypto/sha256.h"
#include "../third_party/cjson/cJSON.h"

#define AUTH_JSON_SIZE 512

static void join_path(char *out, size_t out_size, const char *a, const char *b)
{
    snprintf(out, out_size, "%s/%s", a, b);
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static bool hex_to_bytes(const char *text, uint8_t *out, size_t out_size)
{
    size_t i;
    if (!text || strlen(text) != out_size * 2) {
        return false;
    }
    for (i = 0; i < out_size; ++i) {
        int high = hex_value(text[i * 2]);
        int low = hex_value(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static void bytes_to_hex(const uint8_t *bytes, size_t bytes_len, char *out, size_t out_size)
{
    static const char HEX[] = "0123456789abcdef";
    size_t i;
    if (out_size == 0) {
        return;
    }
    for (i = 0; i < bytes_len && i * 2 + 1 < out_size; ++i) {
        out[i * 2] = HEX[bytes[i] >> 4];
        out[i * 2 + 1] = HEX[bytes[i] & 0xfu];
    }
    out[i * 2] = '\0';
}

static bool fixed_time_equal(const char *a, const char *b)
{
    size_t len_a = a ? strlen(a) : 0;
    size_t len_b = b ? strlen(b) : 0;
    size_t len = len_a > len_b ? len_a : len_b;
    unsigned char diff = (unsigned char)(len_a ^ len_b);
    size_t i;
    for (i = 0; i < len; ++i) {
        unsigned char ca = i < len_a ? (unsigned char)a[i] : 0;
        unsigned char cb = i < len_b ? (unsigned char)b[i] : 0;
        diff |= (unsigned char)(ca ^ cb);
    }
    return diff == 0;
}

static bool valid_pin(const char *pin)
{
    size_t len = pin ? strlen(pin) : 0;
    return len > 0 && len <= PTC_AUTH_PIN_MAX_LEN;
}

static void pin_hash_hex(const char *pin, const uint8_t salt[PTC_AUTH_SALT_LEN], char out[PTC_AUTH_HASH_HEX_LEN + 1])
{
    static const uint8_t DOMAIN[] = {'P', 'T', 'C', '-', 'P', 'I', 'N', '1'};
    uint8_t msg[sizeof(DOMAIN) + PTC_AUTH_SALT_LEN];
    uint8_t digest[PTC_SHA256_DIGEST_SIZE];
    memcpy(msg, DOMAIN, sizeof(DOMAIN));
    memcpy(msg + sizeof(DOMAIN), salt, PTC_AUTH_SALT_LEN);
    ptc_hmac_sha256((const uint8_t *)pin, strlen(pin), msg, sizeof(msg), digest);
    bytes_to_hex(digest, sizeof(digest), out, PTC_AUTH_HASH_HEX_LEN + 1);
}

static PtcAuthStatus read_auth_json(PtcCompanionAuth *auth, char *text, size_t text_size)
{
    char path[160];
    if (!auth || !auth->storage || !text || text_size == 0) {
        return PTC_AUTH_BAD_ARGUMENT;
    }
    join_path(path, sizeof(path), auth->app_root, "auth.json");
    if (!auth->storage->vtable->read_text(auth->storage, path, text, text_size)) {
        return PTC_AUTH_READ_FAILED;
    }
    return PTC_AUTH_OK;
}

static PtcAuthStatus parse_auth_json(const char *text, char *hash, size_t hash_size, char *salt, size_t salt_size)
{
    cJSON *root;
    const cJSON *version;
    const cJSON *pin_hash;
    const cJSON *pin_salt;
    const cJSON *hash_name;
    PtcAuthStatus status = PTC_AUTH_OK;

    root = cJSON_Parse(text);
    if (!root) {
        return PTC_AUTH_INVALID_FILE;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    pin_hash = cJSON_GetObjectItemCaseSensitive(root, "pin_hash");
    pin_salt = cJSON_GetObjectItemCaseSensitive(root, "pin_salt");
    hash_name = cJSON_GetObjectItemCaseSensitive(root, "hash");
    if (!cJSON_IsNumber(version) || (int)version->valuedouble != 1 ||
        !cJSON_IsString(pin_hash) || !pin_hash->valuestring ||
        !cJSON_IsString(pin_salt) || !pin_salt->valuestring ||
        !cJSON_IsString(hash_name) || !hash_name->valuestring ||
        strcmp(hash_name->valuestring, "hmac-sha256") != 0) {
        status = PTC_AUTH_INVALID_FILE;
        goto done;
    }
    snprintf(hash, hash_size, "%s", pin_hash->valuestring);
    snprintf(salt, salt_size, "%s", pin_salt->valuestring);
    if (hash[0] == '\0' && salt[0] == '\0') {
        status = PTC_AUTH_EMPTY;
        goto done;
    }
    if (strlen(hash) != PTC_AUTH_HASH_HEX_LEN || strlen(salt) != PTC_AUTH_SALT_HEX_LEN) {
        status = PTC_AUTH_INVALID_FILE;
        goto done;
    }

done:
    cJSON_Delete(root);
    return status;
}

void ptc_companion_auth_init(PtcCompanionAuth *auth, const char *app_root, PtcStorage *storage)
{
    if (!auth) {
        return;
    }
    snprintf(auth->app_root, sizeof(auth->app_root), "%s", app_root ? app_root : "");
    auth->storage = storage;
}

PtcAuthStatus ptc_companion_auth_state(PtcCompanionAuth *auth)
{
    char text[AUTH_JSON_SIZE];
    char hash[PTC_AUTH_HASH_HEX_LEN + 1];
    char salt[PTC_AUTH_SALT_HEX_LEN + 1];
    PtcAuthStatus status = read_auth_json(auth, text, sizeof(text));
    if (status != PTC_AUTH_OK) {
        return status;
    }
    return parse_auth_json(text, hash, sizeof(hash), salt, sizeof(salt));
}

PtcAuthStatus ptc_companion_auth_set_pin(
    PtcCompanionAuth *auth,
    const char *pin,
    int64_t updated_at,
    PtcAuthRandomFn random_fn,
    void *random_ctx)
{
    uint8_t salt[PTC_AUTH_SALT_LEN];
    char salt_hex[PTC_AUTH_SALT_HEX_LEN + 1];
    char hash_hex[PTC_AUTH_HASH_HEX_LEN + 1];
    char path[160];
    char json[AUTH_JSON_SIZE];
    int written;

    if (!auth || !auth->storage || !valid_pin(pin) || !random_fn) {
        return PTC_AUTH_BAD_ARGUMENT;
    }
    if (!random_fn(salt, sizeof(salt), random_ctx)) {
        return PTC_AUTH_BAD_ARGUMENT;
    }
    bytes_to_hex(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
    pin_hash_hex(pin, salt, hash_hex);
    written = snprintf(
        json,
        sizeof(json),
        "{\"version\":1,\"pin_hash\":\"%s\",\"pin_salt\":\"%s\",\"hash\":\"hmac-sha256\",\"updated_at\":%lld}\n",
        hash_hex,
        salt_hex,
        (long long)updated_at);
    if (written < 0 || (size_t)written >= sizeof(json)) {
        return PTC_AUTH_BAD_ARGUMENT;
    }
    join_path(path, sizeof(path), auth->app_root, "auth.json");
    return auth->storage->vtable->write_text_atomic(auth->storage, path, json) ? PTC_AUTH_OK : PTC_AUTH_WRITE_FAILED;
}

PtcAuthStatus ptc_companion_auth_verify_pin(PtcCompanionAuth *auth, const char *pin)
{
    char text[AUTH_JSON_SIZE];
    char expected_hash[PTC_AUTH_HASH_HEX_LEN + 1];
    char salt_hex[PTC_AUTH_SALT_HEX_LEN + 1];
    char actual_hash[PTC_AUTH_HASH_HEX_LEN + 1];
    uint8_t salt[PTC_AUTH_SALT_LEN];
    PtcAuthStatus status;

    if (!valid_pin(pin)) {
        return PTC_AUTH_BAD_ARGUMENT;
    }
    status = read_auth_json(auth, text, sizeof(text));
    if (status != PTC_AUTH_OK) {
        return status;
    }
    status = parse_auth_json(text, expected_hash, sizeof(expected_hash), salt_hex, sizeof(salt_hex));
    if (status != PTC_AUTH_OK) {
        return status;
    }
    if (!hex_to_bytes(salt_hex, salt, sizeof(salt))) {
        return PTC_AUTH_INVALID_FILE;
    }
    pin_hash_hex(pin, salt, actual_hash);
    return fixed_time_equal(expected_hash, actual_hash) ? PTC_AUTH_OK : PTC_AUTH_DENIED;
}

const char *ptc_auth_status_name(PtcAuthStatus status)
{
    switch (status) {
    case PTC_AUTH_OK:
        return "ok";
    case PTC_AUTH_EMPTY:
        return "empty";
    case PTC_AUTH_BAD_ARGUMENT:
        return "bad_argument";
    case PTC_AUTH_READ_FAILED:
        return "read_failed";
    case PTC_AUTH_WRITE_FAILED:
        return "write_failed";
    case PTC_AUTH_INVALID_FILE:
        return "invalid_file";
    case PTC_AUTH_DENIED:
        return "denied";
    default:
        return "unknown";
    }
}
