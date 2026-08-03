#include "result_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capability_backend.h"

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    return p;
}

static const char *find_key(const char *text, const char *key)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(text, pattern);
}

static bool has_key(const char *text, const char *key)
{
    return find_key(text, key) != NULL;
}

static bool json_string_equals(const char *text, const char *key, const char *expected)
{
    const char *pos = find_key(text, key);
    const char *start;
    const char *end;
    size_t expected_len = strlen(expected);
    if (!pos) {
        return false;
    }
    pos = strchr(pos + strlen(key) + 2, ':');
    if (!pos) {
        return false;
    }
    pos = skip_ws(pos + 1);
    if (*pos != '"') {
        return false;
    }
    start = pos + 1;
    end = strchr(start, '"');
    return end && (size_t)(end - start) == expected_len && strncmp(start, expected, expected_len) == 0;
}

static bool json_i64_present(const char *text, const char *key)
{
    const char *pos = find_key(text, key);
    char *endptr;
    if (!pos) {
        return false;
    }
    pos = strchr(pos + strlen(key) + 2, ':');
    if (!pos) {
        return false;
    }
    pos = skip_ws(pos + 1);
    (void)strtoll(pos, &endptr, 10);
    return endptr != pos;
}

static bool json_bool_present(const char *text, const char *key)
{
    const char *pos = find_key(text, key);
    if (!pos) {
        return false;
    }
    pos = strchr(pos + strlen(key) + 2, ':');
    if (!pos) {
        return false;
    }
    pos = skip_ws(pos + 1);
    return strncmp(pos, "true", 4) == 0 || strncmp(pos, "false", 5) == 0;
}

static const char *json_bool(bool value)
{
    return value ? "true" : "false";
}

static void append_state(char *out, size_t out_size, const PtcResultState *state)
{
    size_t used = 0;
    while (out[used] != '\0' && used < out_size) {
        ++used;
    }
    (void)snprintf(
        out + used,
        out_size > used ? out_size - used : 0,
        "\"state\":{\"day_index\":%u,\"limited_today\":%d,\"blocked_today\":%d,"
        "\"unrestricted_today\":%d,\"remaining_available\":%s,\"remaining_minutes\":%lld,"
        "\"played_minutes_available\":%s,\"played_minutes\":%lld,"
        "\"play_timer_enabled\":%d,\"restricted_now\":%d,\"bedtime_active\":%s,"
        "\"parent_unlock_active\":%s},\"capabilities\":{\"play_timer_write_verified\":%s,"
        "\"play_timer_effect_verified\":%s,\"play_timer_effect_backend\":\"%s\","
        "\"raw_block_verified\":%s,\"suspend_verified\":%s}",
        state->day_index,
        state->limited_today,
        state->blocked_today,
        state->unrestricted_today,
        json_bool(state->remaining_available),
        (long long)state->remaining_minutes,
        json_bool(state->played_minutes_available),
        (long long)state->played_minutes,
        state->play_timer_enabled,
        state->restricted_now,
        json_bool(state->bedtime_active),
        json_bool(state->parent_unlock_active),
        json_bool(state->play_timer_write_verified),
        json_bool(state->play_timer_effect_verified),
        PTC_PLAY_TIMER_EFFECT_BACKEND,
        json_bool(state->raw_block_verified),
        json_bool(state->suspend_verified));
}

void ptc_result_state_default(PtcResultState *state, uint16_t day_index)
{
    state->day_index = day_index;
    state->limited_today = -1;
    state->blocked_today = -1;
    state->unrestricted_today = -1;
    state->remaining_available = false;
    state->remaining_minutes = -1;
    state->played_minutes_available = false;
    state->played_minutes = -1;
    state->play_timer_enabled = -1;
    state->restricted_now = -1;
    state->bedtime_active = false;
    state->parent_unlock_active = false;
    state->play_timer_write_verified = false;
    state->play_timer_effect_verified = false;
    state->raw_block_verified = false;
    state->suspend_verified = false;
}

PtcErrorCode ptc_result_validate(const char *text)
{
    bool ok_status;
    bool error_status;
    if (!text || strchr(text, '{') == NULL || strchr(text, '}') == NULL) {
        return PTC_ERR_BAD_REQUEST;
    }
    if (!json_i64_present(text, "version") ||
        !has_key(text, "request_id") ||
        !has_key(text, "type") ||
        !has_key(text, "status") ||
        !has_key(text, "mode") ||
        !json_bool_present(text, "dry_run") ||
        !json_i64_present(text, "completed_at")) {
        return PTC_ERR_BAD_REQUEST;
    }
    if (!has_key(text, "state") ||
        !json_i64_present(text, "day_index") ||
        !json_i64_present(text, "limited_today") ||
        !json_i64_present(text, "blocked_today") ||
        !json_i64_present(text, "unrestricted_today") ||
        !json_bool_present(text, "remaining_available") ||
        !json_i64_present(text, "remaining_minutes") ||
        !json_i64_present(text, "play_timer_enabled") ||
        !json_i64_present(text, "restricted_now") ||
        !json_bool_present(text, "bedtime_active") ||
        !json_bool_present(text, "parent_unlock_active")) {
        return PTC_ERR_BAD_REQUEST;
    }
    if (!has_key(text, "capabilities") ||
        !json_bool_present(text, "play_timer_effect_verified") ||
        !json_bool_present(text, "raw_block_verified") ||
        !json_bool_present(text, "suspend_verified")) {
        return PTC_ERR_BAD_REQUEST;
    }
    ok_status = json_string_equals(text, "status", "ok");
    error_status = json_string_equals(text, "status", "error");
    if (!ok_status && !error_status) {
        return PTC_ERR_BAD_REQUEST;
    }
    if (error_status &&
        (!has_key(text, "error") ||
            !json_i64_present(text, "code") ||
            !has_key(text, "reason") ||
            !has_key(text, "message"))) {
        return PTC_ERR_BAD_REQUEST;
    }
    return PTC_ERR_OK;
}

int ptc_result_ok_json(
    char *out,
    size_t out_size,
    const char *request_id,
    const char *request_type,
    const char *mode,
    bool dry_run,
    const PtcResultState *state,
    int64_t completed_at)
{
    int written = snprintf(
        out,
        out_size,
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"status\":\"ok\","
        "\"mode\":\"%s\",\"dry_run\":%s,",
        request_id,
        request_type,
        mode,
        json_bool(dry_run));
    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }
    append_state(out, out_size, state);
    written = (int)snprintf(out + strlen(out), out_size - strlen(out), ",\"completed_at\":%lld}\n", (long long)completed_at);
    return written < 0 ? -1 : 0;
}

int ptc_result_error_json(
    char *out,
    size_t out_size,
    const char *request_id,
    const char *request_type,
    const char *mode,
    bool dry_run,
    PtcErrorCode error,
    const PtcResultState *state,
    int64_t completed_at)
{
    int written = snprintf(
        out,
        out_size,
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"status\":\"error\","
        "\"mode\":\"%s\",\"dry_run\":%s,\"error\":{\"code\":%d,\"reason\":\"%s\","
        "\"message\":\"%s\"},",
        request_id,
        request_type,
        mode,
        json_bool(dry_run),
        (int)error,
        ptc_error_reason(error),
        ptc_error_message_zh(error));
    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }
    append_state(out, out_size, state);
    written = (int)snprintf(out + strlen(out), out_size - strlen(out), ",\"completed_at\":%lld}\n", (long long)completed_at);
    return written < 0 ? -1 : 0;
}
