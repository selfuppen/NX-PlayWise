#include "file_protocol.h"

#include <stdarg.h>
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

static PtcCompanionStatus submit_minutes(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, const char *type, uint16_t minutes)
{
    char json[512];
    if (!request_id || request_id[0] == '\0' || !type) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    if (ptc_companion_parent_minutes_request_json(json, sizeof(json), request_id, created_at, type, minutes) >= (int)sizeof(json)) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    return submit_json(client, request_id, json);
}

static PtcCompanionStatus submit_empty(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, const char *type)
{
    char json[512];
    if (!request_id || request_id[0] == '\0' || !type) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    if (ptc_companion_empty_payload_request_json(json, sizeof(json), request_id, created_at, type) >= (int)sizeof(json)) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    return submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_submit_set_today_limit(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, uint16_t minutes)
{
    return submit_minutes(client, request_id, created_at, "set_today_limit", minutes);
}

PtcCompanionStatus ptc_companion_submit_add_today_minutes(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, uint16_t minutes)
{
    return submit_minutes(client, request_id, created_at, "add_today_minutes", minutes);
}

PtcCompanionStatus ptc_companion_submit_disable_today_limit(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    return submit_empty(client, request_id, created_at, "disable_today_limit");
}

PtcCompanionStatus ptc_companion_submit_block_today(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    return submit_empty(client, request_id, created_at, "block_today");
}

PtcCompanionStatus ptc_companion_submit_restore_today_policy(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    return submit_empty(client, request_id, created_at, "restore_today_policy");
}

PtcCompanionStatus ptc_companion_submit_set_weekly_template(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, const PtcDayRule week[7])
{
    char json[1024];
    if (!request_id || request_id[0] == '\0' || !week) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    if (ptc_companion_set_weekly_template_request_json(json, sizeof(json), request_id, created_at, week) >= (int)sizeof(json)) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    return submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_submit_set_bedtime(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, const PtcBedtimeRule *bedtime)
{
    char json[512];
    if (!request_id || request_id[0] == '\0' || !bedtime) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    if (ptc_companion_set_bedtime_request_json(json, sizeof(json), request_id, created_at, bedtime) >= (int)sizeof(json)) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    return submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_submit_set_limit_action(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, PtcLimitAction action)
{
    char json[512];
    if (!request_id || request_id[0] == '\0') {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    if (ptc_companion_set_limit_action_request_json(json, sizeof(json), request_id, created_at, action) >= (int)sizeof(json)) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    return submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_submit_parent_unlock_start(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, uint16_t duration_minutes)
{
    char json[512];
    if (!request_id || request_id[0] == '\0') {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    if (ptc_companion_parent_unlock_start_request_json(json, sizeof(json), request_id, created_at, duration_minutes) >= (int)sizeof(json)) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    return submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_submit_parent_unlock_end(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    return submit_empty(client, request_id, created_at, "parent_unlock_end");
}

PtcCompanionStatus ptc_companion_submit_probe_play_timer_write(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    return submit_empty(client, request_id, created_at, "probe_play_timer_write");
}

PtcCompanionStatus ptc_companion_submit_probe_play_timer_effect(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, bool wait_for_expiry)
{
    char json[512];
    if (!request_id || request_id[0] == '\0') {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    if (ptc_companion_probe_play_timer_effect_request_json(json, sizeof(json), request_id, created_at, wait_for_expiry) >= (int)sizeof(json)) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    return submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_submit_prepare_device_test(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    return submit_empty(client, request_id, created_at, "prepare_device_test");
}

PtcCompanionStatus ptc_companion_submit_probe_apply_today_limit(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    char json[512];
    if (!request_id || request_id[0] == '\0') {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    snprintf(
        json,
        sizeof(json),
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"probe_apply_today_limit\","
        "\"created_at\":%lld,\"payload\":{\"minutes\":1,\"start_timer\":true}}\n",
        request_id,
        (long long)created_at);
    return submit_json(client, request_id, json);
}

PtcCompanionStatus ptc_companion_submit_probe_raw_block(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    return submit_empty(client, request_id, created_at, "probe_raw_block");
}

PtcCompanionStatus ptc_companion_submit_probe_suspend(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    return submit_empty(client, request_id, created_at, "probe_suspend");
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
            "cap: play_write=%s effect=%s raw_block=%s suspend=%s\n",
            json_bool_text_or(capabilities, "play_timer_write_verified", "false"),
            json_bool_text_or(capabilities, "play_timer_effect_verified", "false"),
            json_bool_text_or(capabilities, "raw_block_verified", "unknown"),
            json_bool_text_or(capabilities, "suspend_verified", "unknown")) ||
        !appendf(out, out_size, &used, "completed_at: %lld\n", json_number_or(root, "completed_at", -1))) {
        result = PTC_COMPANION_BAD_ARGUMENT;
        goto done;
    }
    {
        const cJSON *effect = cJSON_GetObjectItemCaseSensitive(root, "pctl_effect_probe");
        if (cJSON_IsObject(effect)) {
            const char *verdict = json_string_or(effect, "verdict", "unknown");
            if (!appendf(out, out_size, &used, "%s SYSTEM EFFECT\n", strcmp(verdict, "pass") == 0 ? "PASS" : "FAIL") ||
                !appendf(out, out_size, &used, "effect verdict: %s stage: %s expiry: %s\n",
                    verdict,
                    json_string_or(effect, "failure_stage", "unknown"),
                    json_bool_text_or(effect, "expiry_observed", "false"))) {
                result = PTC_COMPANION_BAD_ARGUMENT;
                goto done;
            }
        }
    }
    {
        const cJSON *raw_block = cJSON_GetObjectItemCaseSensitive(root, "pctl_raw_block_probe");
        if (cJSON_IsObject(raw_block)) {
            const cJSON *checks = cJSON_GetObjectItemCaseSensitive(raw_block, "checks");
            const char *verdict = json_string_or(raw_block, "verdict", "unknown");
            if (!appendf(out, out_size, &used, "%s RAW BLOCK\n", strcmp(verdict, "pass") == 0 ? "PASS" : "FAIL") ||
                !appendf(out, out_size, &used, "raw block verdict: %s stage: %s blocked: %s restored: %s\n",
                    verdict,
                    json_string_or(raw_block, "failure_stage", "unknown"),
                    json_bool_text_or(checks, "blocked_observed", "false"),
                    json_bool_text_or(checks, "raw_restored", "false"))) {
                result = PTC_COMPANION_BAD_ARGUMENT;
                goto done;
            }
        }
    }

done:
    cJSON_Delete(root);
    return result;
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
    default:
        return "unknown";
    }
}
