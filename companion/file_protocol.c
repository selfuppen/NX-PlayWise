#include "file_protocol.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../common/protocol/result_builder.h"
#include "../third_party/cjson/cJSON.h"
#include "request_client.h"

static void join_path(char *out, size_t out_size, const char *a, const char *b)
{
    snprintf(out, out_size, "%s/%s", a, b);
}

static int appendf(char *out, size_t out_size, size_t *used, const char *fmt, ...)
{
    int written;
    va_list args;

    if (*used >= out_size) {
        return 0;
    }
    va_start(args, fmt);
    written = vsnprintf(out + *used, out_size - *used, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= out_size - *used) {
        if (out_size > 0) {
            out[out_size - 1] = '\0';
        }
        return 0;
    }
    *used += (size_t)written;
    return 1;
}

static const char *json_string_or(const cJSON *object, const char *key, const char *fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : fallback;
}

static long long json_number_or(const cJSON *object, const char *key, long long fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? (long long)item->valuedouble : fallback;
}

static const char *json_bool_text_or(const cJSON *object, const char *key, const char *fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsTrue(item)) {
        return "true";
    }
    if (cJSON_IsFalse(item)) {
        return "false";
    }
    return fallback;
}

static PtcCompanionStatus submit_json(PtcCompanionFileClient *client, const char *request_id, const char *json)
{
    char pending_dir[160];
    char tmp_name[80];
    char final_name[80];
    char tmp_path[240];
    char final_path[240];

    if (!client || !client->storage || !request_id || !json || request_id[0] == '\0') {
        return PTC_COMPANION_BAD_ARGUMENT;
    }

    join_path(pending_dir, sizeof(pending_dir), client->app_root, "inbox/pending");
    snprintf(tmp_name, sizeof(tmp_name), "%s.json.tmp", request_id);
    snprintf(final_name, sizeof(final_name), "%s.json", request_id);
    join_path(tmp_path, sizeof(tmp_path), pending_dir, tmp_name);
    join_path(final_path, sizeof(final_path), pending_dir, final_name);

    if (!client->storage->vtable->write_text_atomic(client->storage, tmp_path, json)) {
        return PTC_COMPANION_WRITE_FAILED;
    }
    if (!client->storage->vtable->rename_path(client->storage, tmp_path, final_path)) {
        return PTC_COMPANION_RENAME_FAILED;
    }
    return PTC_COMPANION_OK;
}

void ptc_companion_file_client_init(PtcCompanionFileClient *client, const char *app_root, PtcStorage *storage)
{
    if (!client) {
        return;
    }
    snprintf(client->app_root, sizeof(client->app_root), "%s", app_root ? app_root : "");
    client->storage = storage;
}

PtcCompanionStatus ptc_companion_make_request_id(char *out, size_t out_size, int64_t unix_ms, uint16_t random16)
{
    int written;
    if (!out || out_size == 0 || unix_ms < 0) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    written = snprintf(out, out_size, "%lld-%04x", (long long)unix_ms, (unsigned int)random16);
    if (written < 0 || (size_t)written >= out_size) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    return PTC_COMPANION_OK;
}

PtcCompanionStatus ptc_companion_submit_status(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    char json[512];
    if (!request_id || request_id[0] == '\0') {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    if (ptc_companion_status_request_json(json, sizeof(json), request_id, created_at) >= (int)sizeof(json)) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    return submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_submit_offline_code(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, const char *code)
{
    char json[640];
    if (!request_id || request_id[0] == '\0' || !code) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    if (ptc_companion_offline_code_request_json(json, sizeof(json), request_id, created_at, code) >= (int)sizeof(json)) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    return submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_read_result(
    PtcCompanionFileClient *client,
    const char *request_id,
    int elapsed_ms,
    int timeout_ms,
    char *out,
    size_t out_size)
{
    char result_name[80];
    char result_path[240];
    cJSON *root;
    const cJSON *json_request_id;

    if (!client || !client->storage || !request_id || !out || out_size == 0) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }

    snprintf(result_name, sizeof(result_name), "results/%s.json", request_id);
    join_path(result_path, sizeof(result_path), client->app_root, result_name);
    if (!client->storage->vtable->read_text(client->storage, result_path, out, out_size)) {
        out[0] = '\0';
        if (timeout_ms >= 0 && elapsed_ms >= timeout_ms) {
            return PTC_COMPANION_TIMEOUT;
        }
        return PTC_COMPANION_PENDING;
    }

    if (ptc_result_validate(out) != PTC_ERR_OK) {
        return PTC_COMPANION_RESULT_INVALID;
    }
    root = cJSON_Parse(out);
    if (!root) {
        return PTC_COMPANION_RESULT_INVALID;
    }
    json_request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    if (!cJSON_IsString(json_request_id) || !json_request_id->valuestring) {
        cJSON_Delete(root);
        return PTC_COMPANION_RESULT_INVALID;
    }
    if (strcmp(json_request_id->valuestring, request_id) != 0) {
        cJSON_Delete(root);
        return PTC_COMPANION_RESULT_MISMATCH;
    }
    cJSON_Delete(root);
    return PTC_COMPANION_OK;
}

PtcCompanionStatus ptc_companion_format_result_summary(const char *result_json, char *out, size_t out_size)
{
    cJSON *root;
    const cJSON *error;
    const cJSON *state;
    const cJSON *capabilities;
    const char *request_id;
    const char *type;
    const char *status;
    const char *mode;
    const char *dry_run;
    const char *reason;
    size_t used = 0;
    PtcCompanionStatus result = PTC_COMPANION_OK;

    if (!result_json || !out || out_size == 0) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    out[0] = '\0';
    if (ptc_result_validate(result_json) != PTC_ERR_OK) {
        return PTC_COMPANION_RESULT_INVALID;
    }

    root = cJSON_Parse(result_json);
    if (!root) {
        return PTC_COMPANION_RESULT_INVALID;
    }
    error = cJSON_GetObjectItemCaseSensitive(root, "error");
    state = cJSON_GetObjectItemCaseSensitive(root, "state");
    capabilities = cJSON_GetObjectItemCaseSensitive(root, "capabilities");
    request_id = json_string_or(root, "request_id", "unknown");
    type = json_string_or(root, "type", "unknown");
    status = json_string_or(root, "status", "unknown");
    mode = json_string_or(root, "mode", "unknown");
    dry_run = json_bool_text_or(root, "dry_run", "unknown");
    reason = json_string_or(error, "reason", "");

    if (!appendf(out, out_size, &used, "Result summary\n")) {
        result = PTC_COMPANION_BAD_ARGUMENT;
        goto done;
    }
    if (!appendf(out, out_size, &used, "status: %s\n", status) ||
        !appendf(out, out_size, &used, "request: %s\n", request_id) ||
        !appendf(out, out_size, &used, "type: %s\n", type) ||
        !appendf(out, out_size, &used, "mode: %s  dry_run: %s\n", mode, dry_run)) {
        result = PTC_COMPANION_BAD_ARGUMENT;
        goto done;
    }
    if (reason[0] != '\0') {
        if (!appendf(out, out_size, &used, "error: %s (%lld)\n", reason, json_number_or(error, "code", -1))) {
            result = PTC_COMPANION_BAD_ARGUMENT;
            goto done;
        }
    }
    if (!appendf(out, out_size, &used, "day_index: %lld\n", json_number_or(state, "day_index", -1)) ||
        !appendf(
            out,
            out_size,
            &used,
            "remaining: %lld  available: %s\n",
            json_number_or(state, "remaining_minutes", -1),
            json_bool_text_or(state, "remaining_available", "unknown")) ||
        !appendf(
            out,
            out_size,
            &used,
            "pctl: limited=%lld blocked=%lld unrestricted=%lld\n",
            json_number_or(state, "limited_today", -1),
            json_number_or(state, "blocked_today", -1),
            json_number_or(state, "unrestricted_today", -1)) ||
        !appendf(
            out,
            out_size,
            &used,
            "timer: enabled=%lld restricted_now=%lld\n",
            json_number_or(state, "play_timer_enabled", -1),
            json_number_or(state, "restricted_now", -1)) ||
        !appendf(
            out,
            out_size,
            &used,
            "state: bedtime=%s unlock=%s\n",
            json_bool_text_or(state, "bedtime_active", "unknown"),
            json_bool_text_or(state, "parent_unlock_active", "unknown")) ||
        !appendf(
            out,
            out_size,
            &used,
            "cap: raw_block=%s suspend=%s\n",
            json_bool_text_or(capabilities, "raw_block_verified", "unknown"),
            json_bool_text_or(capabilities, "suspend_verified", "unknown")) ||
        !appendf(out, out_size, &used, "completed_at: %lld\n", json_number_or(root, "completed_at", -1))) {
        result = PTC_COMPANION_BAD_ARGUMENT;
        goto done;
    }

done:
    cJSON_Delete(root);
    return result;
}

const char *ptc_companion_status_name(PtcCompanionStatus status)
{
    switch (status) {
    case PTC_COMPANION_OK:
        return "ok";
    case PTC_COMPANION_PENDING:
        return "pending";
    case PTC_COMPANION_TIMEOUT:
        return "timeout";
    case PTC_COMPANION_BAD_ARGUMENT:
        return "bad_argument";
    case PTC_COMPANION_WRITE_FAILED:
        return "write_failed";
    case PTC_COMPANION_RENAME_FAILED:
        return "rename_failed";
    case PTC_COMPANION_RESULT_INVALID:
        return "result_invalid";
    case PTC_COMPANION_RESULT_MISMATCH:
        return "result_mismatch";
    default:
        return "unknown";
    }
}
