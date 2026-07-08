#include "file_protocol.h"

#include <stdio.h>
#include <string.h>

#include "request_client.h"

static void join_path(char *out, size_t out_size, const char *a, const char *b)
{
    snprintf(out, out_size, "%s/%s", a, b);
}

static int has_value(const char *text, const char *key, const char *expected)
{
    char pattern[64];
    const char *pos;
    const char *colon;
    const char *start;
    const char *end;
    size_t expected_len = strlen(expected);

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(text, pattern);
    if (!pos) {
        return 0;
    }
    colon = strchr(pos + strlen(pattern), ':');
    if (!colon) {
        return 0;
    }
    start = strchr(colon, '"');
    if (!start) {
        return 0;
    }
    ++start;
    end = strchr(start, '"');
    if (!end) {
        return 0;
    }
    return (size_t)(end - start) == expected_len && strncmp(start, expected, expected_len) == 0;
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

    if (!has_value(out, "request_id", request_id)) {
        return strstr(out, "\"request_id\"") ? PTC_COMPANION_RESULT_MISMATCH : PTC_COMPANION_RESULT_INVALID;
    }
    return PTC_COMPANION_OK;
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
