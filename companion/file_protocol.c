#include "file_protocol.h"

#include <stdio.h>
#include <string.h>

#include "../common/protocol/result_builder.h"
#include "../common/protocol/request_schema.h"
#include "../third_party/cjson/cJSON.h"
#include "request_client.h"

static void join_path(char *out, size_t out_size, const char *a, const char *b)
{
    snprintf(out, out_size, "%s/%s", a, b);
}

static PtcCompanionStatus submit_json(PtcCompanionFileClient *client, const char *request_id, const char *json)
{
    char pending_dir[160];
    char tmp_name[80];
    char final_name[80];
    char tmp_path[240];
    char final_path[240];

    if (!client || !client->storage || !request_id || !json || !ptc_request_id_is_valid(request_id)) {
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

static void pending_redemption_path(const PtcCompanionFileClient *client, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/pending-redemption.json", client->app_root);
}

static bool json_bool_required(const cJSON *object, const char *key, bool *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsBool(item) || !out) return false;
    *out = cJSON_IsTrue(item);
    return true;
}

static bool json_int_required(const cJSON *object, const char *key, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !out) return false;
    *out = item->valueint;
    return true;
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

PtcCompanionStatus ptc_companion_set_disable_flag(PtcCompanionFileClient *client, bool enabled)
{
    char flag_path[160];
    if (!client || !client->storage) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    join_path(flag_path, sizeof(flag_path), client->app_root, "flags/disable.flag");
    if (enabled) {
        return client->storage->vtable->write_text_atomic(client->storage, flag_path, "") ? PTC_COMPANION_OK : PTC_COMPANION_WRITE_FAILED;
    }
    if (!client->storage->vtable->exists(client->storage, flag_path)) {
        return PTC_COMPANION_OK;
    }
    return client->storage->vtable->remove_path(client->storage, flag_path) ? PTC_COMPANION_OK : PTC_COMPANION_WRITE_FAILED;
}

PtcCompanionStatus ptc_companion_pending_redemption_save(
    PtcCompanionFileClient *client,
    const PtcPendingRedemption *pending)
{
    char path[192];
    char text[1024];
    int written;
    if (!client || !client->storage || !pending ||
        !ptc_request_id_is_valid(pending->request_id) || pending->confirmed_at < 0 ||
        pending->grant_minutes < 0 || pending->grant_minutes > 1440 ||
        pending->before_remaining_minutes < -1 || pending->after_remaining_minutes < -1 ||
        pending->effective_add_minutes < 0 || pending->effective_add_minutes > 1440) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    written = snprintf(
        text, sizeof(text),
        "{\"version\":1,\"request_id\":\"%s\",\"confirmed_at\":%lld,\"submitted\":%s,"
        "\"preview\":{\"grant_minutes\":%d,\"before_remaining_available\":%s,"
        "\"before_remaining_minutes\":%d,\"before_unlimited\":%s,"
        "\"after_remaining_available\":%s,\"after_remaining_minutes\":%d,"
        "\"effective_add_minutes\":%d,\"capped\":%s,"
        "\"converts_unlimited_to_limited\":%s}}\n",
        pending->request_id,
        (long long)pending->confirmed_at,
        pending->submitted ? "true" : "false",
        pending->grant_minutes,
        pending->before_remaining_available ? "true" : "false",
        pending->before_remaining_minutes,
        pending->before_unlimited ? "true" : "false",
        pending->after_remaining_available ? "true" : "false",
        pending->after_remaining_minutes,
        pending->effective_add_minutes,
        pending->capped ? "true" : "false",
        pending->converts_unlimited_to_limited ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(text)) return PTC_COMPANION_BAD_ARGUMENT;
    pending_redemption_path(client, path, sizeof(path));
    return client->storage->vtable->write_text_atomic(client->storage, path, text)
        ? PTC_COMPANION_OK : PTC_COMPANION_WRITE_FAILED;
}

PtcCompanionStatus ptc_companion_pending_redemption_load(
    PtcCompanionFileClient *client,
    PtcPendingRedemption *out,
    bool *found)
{
    char path[192];
    char text[1024];
    cJSON *root;
    const cJSON *version;
    const cJSON *request_id;
    const cJSON *confirmed_at;
    const cJSON *preview;
    if (!client || !client->storage || !out || !found) return PTC_COMPANION_BAD_ARGUMENT;
    memset(out, 0, sizeof(*out));
    *found = false;
    pending_redemption_path(client, path, sizeof(path));
    if (!client->storage->vtable->exists(client->storage, path)) return PTC_COMPANION_OK;
    *found = true;
    if (!client->storage->vtable->read_text(client->storage, path, text, sizeof(text))) {
        return PTC_COMPANION_WRITE_FAILED;
    }
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return PTC_COMPANION_RESULT_INVALID;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    confirmed_at = cJSON_GetObjectItemCaseSensitive(root, "confirmed_at");
    preview = cJSON_GetObjectItemCaseSensitive(root, "preview");
    if (!cJSON_IsNumber(version) || version->valueint != 1 ||
        !cJSON_IsString(request_id) || !request_id->valuestring ||
        !ptc_request_id_is_valid(request_id->valuestring) ||
        !cJSON_IsNumber(confirmed_at) || confirmed_at->valuedouble < 0 ||
        !cJSON_IsObject(preview) ||
        !json_bool_required(root, "submitted", &out->submitted) ||
        !json_int_required(preview, "grant_minutes", &out->grant_minutes) ||
        !json_bool_required(preview, "before_remaining_available", &out->before_remaining_available) ||
        !json_int_required(preview, "before_remaining_minutes", &out->before_remaining_minutes) ||
        !json_bool_required(preview, "before_unlimited", &out->before_unlimited) ||
        !json_bool_required(preview, "after_remaining_available", &out->after_remaining_available) ||
        !json_int_required(preview, "after_remaining_minutes", &out->after_remaining_minutes) ||
        !json_int_required(preview, "effective_add_minutes", &out->effective_add_minutes) ||
        !json_bool_required(preview, "capped", &out->capped) ||
        !json_bool_required(preview, "converts_unlimited_to_limited", &out->converts_unlimited_to_limited) ||
        out->grant_minutes < 0 || out->grant_minutes > 1440 ||
        out->before_remaining_minutes < -1 || out->after_remaining_minutes < -1 ||
        out->effective_add_minutes < 0 || out->effective_add_minutes > 1440) {
        cJSON_Delete(root);
        return PTC_COMPANION_RESULT_INVALID;
    }
    snprintf(out->request_id, sizeof(out->request_id), "%s", request_id->valuestring);
    out->confirmed_at = (int64_t)confirmed_at->valuedouble;
    cJSON_Delete(root);
    return PTC_COMPANION_OK;
}

PtcCompanionStatus ptc_companion_pending_redemption_clear(PtcCompanionFileClient *client)
{
    char path[192];
    if (!client || !client->storage) return PTC_COMPANION_BAD_ARGUMENT;
    pending_redemption_path(client, path, sizeof(path));
    if (!client->storage->vtable->exists(client->storage, path)) return PTC_COMPANION_OK;
    return client->storage->vtable->remove_path(client->storage, path)
        ? PTC_COMPANION_OK : PTC_COMPANION_WRITE_FAILED;
}

bool ptc_companion_pending_redemption_has_submission(
    PtcCompanionFileClient *client,
    const PtcPendingRedemption *pending)
{
    static const char *DIRS[] = {"results", "inbox/pending", "inbox/processing", "inbox/done"};
    char path[256];
    size_t index;
    if (!client || !client->storage || !pending || !ptc_request_id_is_valid(pending->request_id)) return false;
    if (pending->submitted) return true;
    for (index = 0; index < sizeof(DIRS) / sizeof(DIRS[0]); ++index) {
        snprintf(path, sizeof(path), "%s/%s/%s.json", client->app_root, DIRS[index], pending->request_id);
        if (client->storage->vtable->exists(client->storage, path)) return true;
    }
    return false;
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

    if (!ptc_request_id_is_valid(request_id)) return PTC_COMPANION_BAD_ARGUMENT;
    snprintf(result_name, sizeof(result_name), "results/%s.json", request_id);
    join_path(result_path, sizeof(result_path), client->app_root, result_name);
    if (!client->storage->vtable->read_text(client->storage, result_path, out, out_size)) {
        char done_name[96];
        char done_path[240];
        out[0] = '\0';
        snprintf(done_name, sizeof(done_name), "inbox/done/%s.json", request_id);
        join_path(done_path, sizeof(done_path), client->app_root, done_name);
        if (client->storage->vtable->exists(client->storage, done_path)) {
            return PTC_COMPANION_WRITE_FAILED;
        }
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

PtcCompanionStatus ptc_companion_parse_result_summary(const char *result_json, PtcCompanionResultSummary *out)
{
    return ptc_companion_result_summary_parse(result_json, out)
        ? PTC_COMPANION_OK
        : PTC_COMPANION_RESULT_INVALID;
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
    case PTC_COMPANION_QUIESCING:
        return "quiescing";
    default:
        return "unknown";
    }
}
