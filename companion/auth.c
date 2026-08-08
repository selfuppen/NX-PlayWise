#include "auth.h"

#include <stdio.h>
#include <string.h>

#include "../common/crypto/sha256.h"
#include "../third_party/cjson/cJSON.h"

#define AUTH_JSON_SIZE 512

typedef struct {
    char hash[PTC_AUTH_HASH_HEX_LEN + 1];
    char salt[PTC_AUTH_SALT_HEX_LEN + 1];
    int failed_attempts;
    int64_t cooldown_until;
    int64_t updated_at;
} AuthRecord;

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
    size_t i;
    if (len < PTC_AUTH_PIN_MIN_LEN || len > PTC_AUTH_PIN_MAX_LEN) return false;
    for (i = 0; i < len; ++i) {
        if (pin[i] < '0' || pin[i] > '9') return false;
    }
    return true;
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

static bool valid_hex(const char *text, size_t expected_length)
{
    size_t index;
    if (!text || strlen(text) != expected_length) return false;
    for (index = 0; index < expected_length; ++index) {
        if (hex_value(text[index]) < 0) return false;
    }
    return true;
}

static PtcAuthStatus parse_auth_json(const char *text, AuthRecord *record)
{
    cJSON *root;
    const cJSON *version;
    const cJSON *pin_hash;
    const cJSON *pin_salt;
    const cJSON *hash_name;
    const cJSON *failed_attempts;
    const cJSON *cooldown_until;
    const cJSON *updated_at;
    PtcAuthStatus status = PTC_AUTH_OK;

    if (!record) return PTC_AUTH_BAD_ARGUMENT;
    memset(record, 0, sizeof(*record));
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return PTC_AUTH_INVALID_FILE;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    pin_hash = cJSON_GetObjectItemCaseSensitive(root, "pin_hash");
    pin_salt = cJSON_GetObjectItemCaseSensitive(root, "pin_salt");
    hash_name = cJSON_GetObjectItemCaseSensitive(root, "hash");
    failed_attempts = cJSON_GetObjectItemCaseSensitive(root, "failed_attempts");
    cooldown_until = cJSON_GetObjectItemCaseSensitive(root, "cooldown_until");
    updated_at = cJSON_GetObjectItemCaseSensitive(root, "updated_at");
    if (!cJSON_IsNumber(version) || (int)version->valuedouble != 1 ||
        !cJSON_IsString(pin_hash) || !pin_hash->valuestring ||
        !cJSON_IsString(pin_salt) || !pin_salt->valuestring ||
        !cJSON_IsString(hash_name) || !hash_name->valuestring ||
        strcmp(hash_name->valuestring, "hmac-sha256") != 0) {
        status = PTC_AUTH_INVALID_FILE;
        goto done;
    }
    if (pin_hash->valuestring[0] == '\0' && pin_salt->valuestring[0] == '\0') {
        status = PTC_AUTH_EMPTY;
        goto done;
    }
    if (!valid_hex(pin_hash->valuestring, PTC_AUTH_HASH_HEX_LEN) ||
        !valid_hex(pin_salt->valuestring, PTC_AUTH_SALT_HEX_LEN) ||
        (failed_attempts && (!cJSON_IsNumber(failed_attempts) || failed_attempts->valuedouble < 0 || failed_attempts->valuedouble > 1000)) ||
        (cooldown_until && (!cJSON_IsNumber(cooldown_until) || cooldown_until->valuedouble < 0)) ||
        (updated_at && (!cJSON_IsNumber(updated_at) || updated_at->valuedouble < 0))) {
        status = PTC_AUTH_INVALID_FILE;
        goto done;
    }
    memcpy(record->hash, pin_hash->valuestring, PTC_AUTH_HASH_HEX_LEN + 1);
    memcpy(record->salt, pin_salt->valuestring, PTC_AUTH_SALT_HEX_LEN + 1);
    record->failed_attempts = failed_attempts ? failed_attempts->valueint : 0;
    record->cooldown_until = cooldown_until ? (int64_t)cooldown_until->valuedouble : 0;
    record->updated_at = updated_at ? (int64_t)updated_at->valuedouble : 0;

done:
    cJSON_Delete(root);
    return status;
}

static PtcAuthStatus write_auth_record(PtcCompanionAuth *auth, const AuthRecord *record)
{
    char path[160];
    char json[AUTH_JSON_SIZE];
    int written;
    if (!auth || !auth->storage || !record) return PTC_AUTH_BAD_ARGUMENT;
    written = snprintf(
        json,
        sizeof(json),
        "{\"version\":1,\"pin_hash\":\"%s\",\"pin_salt\":\"%s\",\"hash\":\"hmac-sha256\",\"updated_at\":%lld,\"failed_attempts\":%d,\"cooldown_until\":%lld}\n",
        record->hash,
        record->salt,
        (long long)record->updated_at,
        record->failed_attempts,
        (long long)record->cooldown_until);
    if (written < 0 || (size_t)written >= sizeof(json)) return PTC_AUTH_BAD_ARGUMENT;
    join_path(path, sizeof(path), auth->app_root, "auth.json");
    return auth->storage->vtable->write_text_atomic(auth->storage, path, json) ? PTC_AUTH_OK : PTC_AUTH_WRITE_FAILED;
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
    AuthRecord record;
    PtcAuthStatus status = read_auth_json(auth, text, sizeof(text));
    if (status != PTC_AUTH_OK) {
        return status;
    }
    return parse_auth_json(text, &record);
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
    AuthRecord record;

    if (!auth || !auth->storage || !valid_pin(pin) || !random_fn) {
        return PTC_AUTH_BAD_ARGUMENT;
    }
    if (!random_fn(salt, sizeof(salt), random_ctx)) {
        return PTC_AUTH_BAD_ARGUMENT;
    }
    bytes_to_hex(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
    pin_hash_hex(pin, salt, hash_hex);
    memset(&record, 0, sizeof(record));
    memcpy(record.hash, hash_hex, sizeof(record.hash));
    memcpy(record.salt, salt_hex, sizeof(record.salt));
    record.updated_at = updated_at;
    return write_auth_record(auth, &record);
}

PtcAuthStatus ptc_companion_auth_verify_pin(
    PtcCompanionAuth *auth,
    const char *pin,
    int64_t now,
    int64_t *retry_after_seconds)
{
    char text[AUTH_JSON_SIZE];
    char actual_hash[PTC_AUTH_HASH_HEX_LEN + 1];
    uint8_t salt[PTC_AUTH_SALT_LEN];
    AuthRecord record;
    PtcAuthStatus status;
    bool matches;

    if (retry_after_seconds) *retry_after_seconds = 0;

    if (!valid_pin(pin)) {
        return PTC_AUTH_BAD_ARGUMENT;
    }
    status = read_auth_json(auth, text, sizeof(text));
    if (status != PTC_AUTH_OK) {
        return status;
    }
    status = parse_auth_json(text, &record);
    if (status != PTC_AUTH_OK) {
        return status;
    }
    if (record.cooldown_until > now) {
        if (retry_after_seconds) *retry_after_seconds = record.cooldown_until - now;
        return PTC_AUTH_COOLDOWN;
    }
    if (!hex_to_bytes(record.salt, salt, sizeof(salt))) {
        return PTC_AUTH_INVALID_FILE;
    }
    pin_hash_hex(pin, salt, actual_hash);
    matches = fixed_time_equal(record.hash, actual_hash);
    if (matches) {
        if (record.failed_attempts == 0 && record.cooldown_until == 0) return PTC_AUTH_OK;
        record.failed_attempts = 0;
        record.cooldown_until = 0;
        return write_auth_record(auth, &record);
    }
    if (record.failed_attempts < 1000) ++record.failed_attempts;
    record.cooldown_until = 0;
    if (record.failed_attempts >= PTC_AUTH_FAILURE_LIMIT) {
        int shift = record.failed_attempts - PTC_AUTH_FAILURE_LIMIT;
        int64_t delay = PTC_AUTH_INITIAL_COOLDOWN_SECONDS;
        while (shift-- > 0 && delay < PTC_AUTH_MAX_COOLDOWN_SECONDS) delay *= 2;
        if (delay > PTC_AUTH_MAX_COOLDOWN_SECONDS) delay = PTC_AUTH_MAX_COOLDOWN_SECONDS;
        record.cooldown_until = now + delay;
        if (retry_after_seconds) *retry_after_seconds = delay;
    }
    status = write_auth_record(auth, &record);
    if (status != PTC_AUTH_OK) return status;
    return record.cooldown_until ? PTC_AUTH_COOLDOWN : PTC_AUTH_DENIED;
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
    case PTC_AUTH_COOLDOWN:
        return "cooldown";
    default:
        return "unknown";
    }
}
