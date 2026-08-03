#include "self_check.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/protocol/result_builder.h"
#include "../common/protocol/capability_backend.h"
#include "../common/time/ptc_time.h"
#include "../third_party/cjson/cJSON.h"

#define SELF_CHECK_TEXT_SIZE 8192

typedef struct {
    PtcStorage *storage;
    const char *app_root;
    const char *request_id;
    const PtcSelfCheckOptions *options;
    char *report;
    size_t report_size;
    size_t used;
    int pass_count;
    int warn_count;
    int fail_count;
} CheckContext;

typedef struct {
    cJSON *root;
    char text[4096];
    const char *status;
    const char *type;
    const char *mode;
    const char *reason;
    int dry_run;
} ResultInfo;

static void join_path(char *out, size_t out_size, const char *a, const char *b)
{
    snprintf(out, out_size, "%s/%s", a ? a : "", b ? b : "");
}

static int appendf(CheckContext *ctx, const char *fmt, ...)
{
    int written;
    va_list args;
    if (!ctx || !ctx->report || ctx->used >= ctx->report_size) {
        return 0;
    }
    va_start(args, fmt);
    written = vsnprintf(ctx->report + ctx->used, ctx->report_size - ctx->used, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= ctx->report_size - ctx->used) {
        if (ctx->report_size > 0) {
            ctx->report[ctx->report_size - 1] = '\0';
        }
        ctx->used = ctx->report_size;
        return 0;
    }
    ctx->used += (size_t)written;
    return 1;
}

static void add_line(CheckContext *ctx, PtcSelfCheckStatus status, const char *label)
{
    switch (status) {
    case PTC_SELF_CHECK_PASS:
        ++ctx->pass_count;
        appendf(ctx, "PASS %s\n", label);
        break;
    case PTC_SELF_CHECK_WARN:
        ++ctx->warn_count;
        appendf(ctx, "WARN %s\n", label);
        break;
    case PTC_SELF_CHECK_FAIL:
        ++ctx->fail_count;
        appendf(ctx, "FAIL %s\n", label);
        break;
    }
}

static const char *json_string_or(const cJSON *object, const char *key, const char *fallback)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : fallback;
}

static int json_bool_as_int(const cJSON *object, const char *key)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    if (cJSON_IsTrue(item)) {
        return 1;
    }
    if (cJSON_IsFalse(item)) {
        return 0;
    }
    return -1;
}

static long long json_number_or(const cJSON *object, const char *key, long long fallback)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsNumber(item) ? (long long)item->valuedouble : fallback;
}

static bool text_contains(const char *text, const char *needle)
{
    return text && needle && strstr(text, needle) != NULL;
}

static bool read_app_text(CheckContext *ctx, const char *relative_path, char *out, size_t out_size)
{
    char path[320];
    join_path(path, sizeof(path), ctx->app_root, relative_path);
    return ctx->storage && ctx->storage->vtable->read_text(ctx->storage, path, out, out_size);
}

static bool scan_app_lines(
    CheckContext *ctx,
    const char *relative_path,
    const char *request_needle,
    const char *event_needle)
{
    char path[320];
    char line[1024];
    FILE *file;
    join_path(path, sizeof(path), ctx->app_root, relative_path);
    file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    while (fgets(line, sizeof(line), file)) {
        if (text_contains(line, request_needle) && (!event_needle || text_contains(line, event_needle))) {
            fclose(file);
            return true;
        }
    }
    fclose(file);
    return false;
}

static bool exists_app_path(CheckContext *ctx, const char *relative_path)
{
    char path[320];
    join_path(path, sizeof(path), ctx->app_root, relative_path);
    return ctx->storage && ctx->storage->vtable->exists(ctx->storage, path);
}

static bool load_result(CheckContext *ctx, ResultInfo *info)
{
    char relative[128];
    const cJSON *error;
    const char *actual_request_id;

    memset(info, 0, sizeof(*info));
    info->dry_run = -1;
    if (!ctx->request_id || ctx->request_id[0] == '\0') {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "request id is required");
        return false;
    }

    snprintf(relative, sizeof(relative), "results/%s.json", ctx->request_id);
    if (!read_app_text(ctx, relative, info->text, sizeof(info->text))) {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "result file exists");
        return false;
    }
    add_line(ctx, PTC_SELF_CHECK_PASS, "result file exists");

    if (strlen(info->text) >= sizeof(info->text) - 1) {
        add_line(ctx, PTC_SELF_CHECK_WARN, "result may be truncated for self-check");
    }
    if (ptc_result_validate(info->text) != PTC_ERR_OK) {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "result schema is valid");
        return false;
    }
    add_line(ctx, PTC_SELF_CHECK_PASS, "result schema is valid");

    info->root = cJSON_Parse(info->text);
    if (!info->root) {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "result JSON parses");
        return false;
    }
    add_line(ctx, PTC_SELF_CHECK_PASS, "result JSON parses");

    actual_request_id = json_string_or(info->root, "request_id", "");
    if (strcmp(actual_request_id, ctx->request_id) == 0) {
        add_line(ctx, PTC_SELF_CHECK_PASS, "result request_id matches");
    } else {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "result request_id matches");
    }

    error = cJSON_GetObjectItemCaseSensitive(info->root, "error");
    info->status = json_string_or(info->root, "status", "");
    info->type = json_string_or(info->root, "type", "");
    info->mode = json_string_or(info->root, "mode", "");
    info->dry_run = json_bool_as_int(info->root, "dry_run");
    info->reason = json_string_or(error, "reason", "");
    appendf(ctx, "EVID result status=%s type=%s mode=%s dry_run=%d reason=%s\n", info->status, info->type, info->mode, info->dry_run, info->reason);
    return true;
}

static void check_queue_state(CheckContext *ctx)
{
    char relative[128];
    snprintf(relative, sizeof(relative), "inbox/done/%s.json", ctx->request_id);
    add_line(ctx, exists_app_path(ctx, relative) ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "request archived in done");
    snprintf(relative, sizeof(relative), "inbox/pending/%s.json", ctx->request_id);
    add_line(ctx, !exists_app_path(ctx, relative) ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "no same request left in pending");
    snprintf(relative, sizeof(relative), "inbox/processing/%s.json", ctx->request_id);
    add_line(ctx, !exists_app_path(ctx, relative) ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "no same request left in processing");
}

static bool read_events(CheckContext *ctx, char *events, size_t events_size)
{
    char relative[64];
    char date[11];
    snprintf(relative, sizeof(relative), "logs/events.jsonl");
    if (ctx->storage && ctx->storage->vtable->list_entries) {
        PtcStorageEntry entries[64];
        size_t count = 0;
        size_t i;
        char logs_path[320];
        char newest[11] = "";
        join_path(logs_path, sizeof(logs_path), ctx->app_root, "logs");
        if (ctx->storage->vtable->list_entries(ctx->storage, logs_path, entries, 64, &count)) {
            for (i = 0; i < count; ++i) {
                uint16_t day_index;
                if (entries[i].type == PTC_STORAGE_ENTRY_DIRECTORY && strlen(entries[i].name) == 10 &&
                    ptc_day_index_from_date((uint16_t)atoi(entries[i].name), (uint8_t)atoi(entries[i].name + 5),
                        (uint8_t)atoi(entries[i].name + 8), &day_index) && strcmp(entries[i].name, newest) > 0) {
                    memcpy(newest, entries[i].name, 11);
                }
            }
        }
        if (newest[0]) { snprintf(date, sizeof(date), "%s", newest); snprintf(relative, sizeof(relative), "logs/%s/events.jsonl", date); }
    }
    if (!read_app_text(ctx, relative, events, events_size)) {
        events[0] = '\0';
        if (exists_app_path(ctx, relative)) {
            add_line(ctx, PTC_SELF_CHECK_PASS, "events log readable");
            return false;
        }
        add_line(ctx, PTC_SELF_CHECK_WARN, "events log readable");
        return false;
    }
    add_line(ctx, PTC_SELF_CHECK_PASS, "events log readable");
    if (strlen(events) >= events_size - 1) {
        add_line(ctx, PTC_SELF_CHECK_WARN, "events log may be truncated");
    }
    return true;
}

static bool event_for_request_exists(CheckContext *ctx, const char *events, const char *request_id)
{
    char needle[96];
    snprintf(needle, sizeof(needle), "\"request_id\":\"%s\"", request_id ? request_id : "");
    return text_contains(events, needle) || scan_app_lines(ctx, "logs/events.jsonl", needle, NULL);
}

static bool event_has(CheckContext *ctx, const char *events, const char *request_id, const char *event_name)
{
    char request_needle[96];
    char event_needle[96];
    const char *cursor = events;
    snprintf(request_needle, sizeof(request_needle), "\"request_id\":\"%s\"", request_id ? request_id : "");
    snprintf(event_needle, sizeof(event_needle), "\"event\":\"%s\"", event_name ? event_name : "");
    while (cursor && *cursor) {
        const char *line_end = strchr(cursor, '\n');
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
        if (strstr(cursor, request_needle) && strstr(cursor, event_needle)) {
            const char *req_pos = strstr(cursor, request_needle);
            const char *event_pos = strstr(cursor, event_needle);
            if (req_pos && event_pos && (size_t)(req_pos - cursor) < line_len && (size_t)(event_pos - cursor) < line_len) {
                return true;
            }
        }
        cursor = line_end ? line_end + 1 : NULL;
    }
    return scan_app_lines(ctx, "logs/events.jsonl", request_needle, event_needle);
}

static void expect_event(CheckContext *ctx, const char *events, const char *request_id, const char *event_name)
{
    char label[96];
    snprintf(label, sizeof(label), "event %s present", event_name);
    add_line(ctx, event_has(ctx, events, request_id, event_name) ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, label);
}

static void forbid_event(CheckContext *ctx, const char *events, const char *request_id, const char *event_name)
{
    char label[96];
    snprintf(label, sizeof(label), "event %s absent", event_name);
    add_line(ctx, !event_has(ctx, events, request_id, event_name) ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, label);
}

static void expect_result_string(CheckContext *ctx, const char *actual, const char *expected, const char *label)
{
    add_line(ctx, strcmp(actual ? actual : "", expected) == 0 ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, label);
}

static void expect_result_bool(CheckContext *ctx, int actual, int expected, const char *label)
{
    add_line(ctx, actual == expected ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, label);
}

static void expect_write_mode(CheckContext *ctx, const char *actual)
{
    add_line(
        ctx,
        strcmp(actual ? actual : "", "grant") == 0 || strcmp(actual ? actual : "", "enforce") == 0
            ? PTC_SELF_CHECK_PASS
            : PTC_SELF_CHECK_FAIL,
        "result mode is write-capable");
}

static void forbid_pctl_and_nonce(CheckContext *ctx, const char *events, const char *request_id)
{
    forbid_event(ctx, events, request_id, "pctl_backup");
    forbid_event(ctx, events, request_id, "pctl_apply");
    forbid_event(ctx, events, request_id, "pctl_apply_failed");
    forbid_event(ctx, events, request_id, "nonce_consumed");
}

static void check_optional_ledger(CheckContext *ctx)
{
    char ledger[SELF_CHECK_TEXT_SIZE];
    char needle[96];
    if (!ctx->options || !ctx->options->has_nonce) {
        return;
    }
    if (!read_app_text(ctx, "ledger/used_nonces.jsonl", ledger, sizeof(ledger))) {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "ledger readable for expected nonce");
        return;
    }
    snprintf(needle, sizeof(needle), "\"day_index\":%u,\"nonce\":%lu", ctx->options->day_index, (unsigned long)ctx->options->nonce);
    add_line(ctx, text_contains(ledger, needle) ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "expected nonce present in ledger");
}

static void check_capabilities_play_write(CheckContext *ctx)
{
    char text[2048];
    cJSON *root;
    if (!read_app_text(ctx, "capabilities.json", text, sizeof(text))) {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "capabilities.json readable");
        return;
    }
    root = cJSON_Parse(text);
    if (!root) {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "capabilities.json parses");
        return;
    }
    add_line(ctx, json_bool_as_int(root, "play_timer_write_verified") == 1 ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "play write capability persisted");
    expect_result_string(ctx, json_string_or(root, "play_timer_write_backend", ""), PTC_PLAY_TIMER_WRITE_BACKEND, "play write backend persisted");
    cJSON_Delete(root);
}

static void check_capabilities_play_effect(CheckContext *ctx)
{
    char text[2048];
    cJSON *root;
    if (!read_app_text(ctx, "capabilities.json", text, sizeof(text))) {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "capabilities.json readable");
        return;
    }
    root = cJSON_Parse(text);
    if (!root) {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "capabilities.json parses");
        return;
    }
    add_line(ctx, json_bool_as_int(root, "play_timer_write_verified") == 1 ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "play write capability persisted");
    add_line(ctx, json_bool_as_int(root, "play_timer_effect_verified") == 1 ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "play effect capability persisted");
    expect_result_string(ctx, json_string_or(root, "play_timer_effect_backend", ""), PTC_PLAY_TIMER_EFFECT_BACKEND, "play effect backend persisted");
    cJSON_Delete(root);
}

static void check_backup(CheckContext *ctx, bool require_hex)
{
    char text[SELF_CHECK_TEXT_SIZE];
    if (!read_app_text(ctx, "backups/last_pctl_backup.txt", text, sizeof(text))) {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "PCTL backup readable");
        return;
    }
    add_line(ctx, PTC_SELF_CHECK_PASS, "PCTL backup readable");
    if (require_hex) {
        add_line(ctx, text_contains(text, "play_timer_settings_hex=") ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "backup contains play timer hex");
    }
}

static void check_request_profile(CheckContext *ctx, PtcSelfCheckProfile profile)
{
    ResultInfo info;
    char events[SELF_CHECK_TEXT_SIZE];
    bool events_read;
    bool result_loaded;

    result_loaded = load_result(ctx, &info);
    if (!result_loaded) {
        return;
    }
    check_queue_state(ctx);
    events_read = read_events(ctx, events, sizeof(events));
    if (events_read) {
        add_line(ctx, event_for_request_exists(ctx, events, ctx->request_id) ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_WARN, "event for request exists");
    }

    switch (profile) {
    case PTC_SELF_CHECK_GENERIC:
        break;
    case PTC_SELF_CHECK_DISABLED_STATUS:
        expect_result_string(ctx, info.status, "error", "result status is error");
        expect_result_string(ctx, info.reason, "disabled", "result reason is disabled");
        forbid_pctl_and_nonce(ctx, events, ctx->request_id);
        break;
    case PTC_SELF_CHECK_OBSERVE_SUCCESS:
        expect_result_string(ctx, info.status, "ok", "result status is ok");
        expect_result_string(ctx, info.mode, "observe", "result mode is observe");
        expect_result_bool(ctx, info.dry_run, 1, "result dry_run is true");
        forbid_pctl_and_nonce(ctx, events, ctx->request_id);
        break;
    case PTC_SELF_CHECK_OBSERVE_REJECTION:
        expect_result_string(ctx, info.status, "error", "result status is error");
        expect_result_string(ctx, info.mode, "observe", "result mode is observe");
        expect_result_bool(ctx, info.dry_run, 1, "result dry_run is true");
        add_line(ctx, info.reason && info.reason[0] ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "error reason is present");
        forbid_pctl_and_nonce(ctx, events, ctx->request_id);
        break;
    case PTC_SELF_CHECK_GRANT_BEFORE_PROBE_REJECT:
        expect_result_string(ctx, info.status, "error", "result status is error");
        expect_result_string(ctx, info.mode, "grant", "result mode is grant");
        expect_result_bool(ctx, info.dry_run, 0, "result dry_run is false");
        add_line(
            ctx,
            strcmp(info.reason, "pctl_write_not_verified") == 0 || strcmp(info.reason, "unlimited_not_allowed") == 0
                ? PTC_SELF_CHECK_PASS
                : PTC_SELF_CHECK_FAIL,
            "reason is pre-probe guard");
        forbid_event(ctx, events, ctx->request_id, "pctl_apply");
        forbid_event(ctx, events, ctx->request_id, "nonce_consumed");
        break;
    case PTC_SELF_CHECK_PLAY_WRITE_PROBE:
        expect_result_string(ctx, info.status, "ok", "result status is ok");
        expect_result_string(ctx, info.type, "probe_play_timer_write", "result type is play write probe");
        expect_write_mode(ctx, info.mode);
        expect_result_bool(ctx, info.dry_run, 0, "result dry_run is false");
        check_capabilities_play_write(ctx);
        check_backup(ctx, true);
        expect_event(ctx, events, ctx->request_id, "pctl_backup");
        expect_event(ctx, events, ctx->request_id, "probe_ok");
        forbid_event(ctx, events, ctx->request_id, "nonce_consumed");
        break;
    case PTC_SELF_CHECK_PLAY_TIMER_EFFECT_PROBE:
        expect_result_string(ctx, info.status, "ok", "result status is ok");
        expect_result_string(ctx, info.type, "probe_play_timer_effect", "result type is effect probe");
        expect_write_mode(ctx, info.mode);
        expect_result_bool(ctx, info.dry_run, 0, "result dry_run is false");
        check_capabilities_play_effect(ctx);
        check_backup(ctx, true);
        expect_event(ctx, events, ctx->request_id, "pctl_backup");
        expect_event(ctx, events, ctx->request_id, "effect_before");
        expect_event(ctx, events, ctx->request_id, "effect_restore");
        {
            cJSON *probe = cJSON_GetObjectItemCaseSensitive(info.root, "pctl_effect_probe");
            add_line(ctx, cJSON_IsObject(probe) ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "effect probe evidence present");
            expect_result_string(ctx, json_string_or(probe, "verdict", ""), "pass", "effect probe verdict is pass");
            {
                cJSON *checks = cJSON_GetObjectItemCaseSensitive(probe, "checks");
                add_line(ctx, json_bool_as_int(checks, "raw_restored") == 1 ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "effect raw restored");
                add_line(ctx, json_bool_as_int(checks, "timer_restored") == 1 ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "effect timer restored");
            }
        }
        forbid_event(ctx, events, ctx->request_id, "nonce_consumed");
        break;
    case PTC_SELF_CHECK_GRANT_SUCCESS:
        expect_result_string(ctx, info.status, "ok", "result status is ok");
        expect_result_string(ctx, info.type, "offline_code", "result type is offline_code");
        expect_result_string(ctx, info.mode, "grant", "result mode is grant");
        expect_result_bool(ctx, info.dry_run, 0, "result dry_run is false");
        check_backup(ctx, false);
        expect_event(ctx, events, ctx->request_id, "pctl_backup");
        expect_event(ctx, events, ctx->request_id, "pctl_apply");
        expect_event(ctx, events, ctx->request_id, "result_ok");
        expect_event(ctx, events, ctx->request_id, "nonce_consumed");
        check_optional_ledger(ctx);
        break;
    case PTC_SELF_CHECK_GRANT_REJECTION:
        expect_result_string(ctx, info.status, "error", "result status is error");
        expect_result_string(ctx, info.mode, "grant", "result mode is grant");
        expect_result_bool(ctx, info.dry_run, 0, "result dry_run is false");
        add_line(ctx, info.reason && info.reason[0] ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "error reason is present");
        forbid_event(ctx, events, ctx->request_id, "pctl_apply");
        if (strcmp(info.reason, "used_token") == 0) {
            add_line(ctx, PTC_SELF_CHECK_WARN, "used_token may have consumed nonce in prior request");
        } else {
            forbid_event(ctx, events, ctx->request_id, "nonce_consumed");
        }
        break;
    case PTC_SELF_CHECK_ENFORCE_SNAPSHOT:
        break;
    }

    if (info.root) {
        cJSON_Delete(info.root);
    }
}

static void check_enforce_snapshot(CheckContext *ctx)
{
    char events[SELF_CHECK_TEXT_SIZE];
    char state_text[2048];
    cJSON *state;
    read_events(ctx, events, sizeof(events));
    add_line(ctx, event_has(ctx, events, "unknown", "pctl_backup") ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "enforce pctl_backup event present");
    add_line(ctx, event_has(ctx, events, "unknown", "pctl_apply") ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "enforce pctl_apply event present");
    add_line(ctx, event_has(ctx, events, "unknown", "pctl_start_timer") ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "enforce pctl_start_timer event present");
    add_line(ctx, event_has(ctx, events, "unknown", "state_persisted") ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "enforce state_persisted event present");

    if (!read_app_text(ctx, "state.json", state_text, sizeof(state_text))) {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "state.json readable");
        return;
    }
    state = cJSON_Parse(state_text);
    if (!state) {
        add_line(ctx, PTC_SELF_CHECK_FAIL, "state.json parses");
        return;
    }
    add_line(ctx, json_number_or(state, "last_enforced_day_index", 0) > 0 ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "last enforced day is set");
    add_line(ctx, json_number_or(state, "last_enforced_minutes", -1) >= 0 ? PTC_SELF_CHECK_PASS : PTC_SELF_CHECK_FAIL, "last enforced minutes is set");
    cJSON_Delete(state);
}

const char *ptc_self_check_profile_name(PtcSelfCheckProfile profile)
{
    switch (profile) {
    case PTC_SELF_CHECK_GENERIC:
        return "generic";
    case PTC_SELF_CHECK_DISABLED_STATUS:
        return "disabled_status";
    case PTC_SELF_CHECK_OBSERVE_SUCCESS:
        return "observe_success";
    case PTC_SELF_CHECK_OBSERVE_REJECTION:
        return "observe_rejection";
    case PTC_SELF_CHECK_GRANT_BEFORE_PROBE_REJECT:
        return "grant_before_probe_reject";
    case PTC_SELF_CHECK_PLAY_WRITE_PROBE:
        return "play_write_probe";
    case PTC_SELF_CHECK_GRANT_SUCCESS:
        return "grant_success";
    case PTC_SELF_CHECK_GRANT_REJECTION:
        return "grant_rejection";
    case PTC_SELF_CHECK_ENFORCE_SNAPSHOT:
        return "enforce_snapshot";
    case PTC_SELF_CHECK_PLAY_TIMER_EFFECT_PROBE:
        return "play_timer_effect_probe";
    default:
        return "unknown";
    }
}

const char *ptc_self_check_status_name(PtcSelfCheckStatus status)
{
    switch (status) {
    case PTC_SELF_CHECK_PASS:
        return "PASS";
    case PTC_SELF_CHECK_WARN:
        return "WARN";
    case PTC_SELF_CHECK_FAIL:
        return "FAIL";
    default:
        return "UNKNOWN";
    }
}

PtcSelfCheckResult ptc_self_check_run(
    PtcStorage *storage,
    const char *app_root,
    const char *request_id,
    PtcSelfCheckProfile profile,
    const PtcSelfCheckOptions *options,
    char *report,
    size_t report_size)
{
    CheckContext ctx;
    PtcSelfCheckResult result;
    memset(&ctx, 0, sizeof(ctx));
    ctx.storage = storage;
    ctx.app_root = app_root ? app_root : "";
    ctx.request_id = request_id ? request_id : "";
    ctx.options = options;
    ctx.report = report;
    ctx.report_size = report_size;

    if (report && report_size > 0) {
        report[0] = '\0';
    }

    appendf(&ctx, "Self-check profile=%s request=%s\n", ptc_self_check_profile_name(profile), ctx.request_id[0] ? ctx.request_id : "(none)");
    if (!storage || !storage->vtable || !report || report_size == 0) {
        add_line(&ctx, PTC_SELF_CHECK_FAIL, "self-check arguments are valid");
    } else if (profile == PTC_SELF_CHECK_ENFORCE_SNAPSHOT) {
        check_enforce_snapshot(&ctx);
    } else {
        check_request_profile(&ctx, profile);
    }

    result.pass_count = ctx.pass_count;
    result.warn_count = ctx.warn_count;
    result.fail_count = ctx.fail_count;
    result.status = ctx.fail_count > 0 ? PTC_SELF_CHECK_FAIL : (ctx.warn_count > 0 ? PTC_SELF_CHECK_WARN : PTC_SELF_CHECK_PASS);
    appendf(&ctx, "SUMMARY %s pass=%d warn=%d fail=%d\n", ptc_self_check_status_name(result.status), result.pass_count, result.warn_count, result.fail_count);
    return result;
}
