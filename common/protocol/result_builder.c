#include "result_builder.h"

#include <stdio.h>
#include <string.h>

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
        "\"unrestricted_today\":%d,\"remaining_available\":%s,\"remaining_minutes\":%d,"
        "\"play_timer_enabled\":%d,\"restricted_now\":%d,\"bedtime_active\":%s,"
        "\"parent_unlock_active\":%s},\"capabilities\":{\"raw_block_verified\":%s,"
        "\"suspend_verified\":%s}",
        state->day_index,
        state->limited_today,
        state->blocked_today,
        state->unrestricted_today,
        json_bool(state->remaining_available),
        state->remaining_minutes,
        state->play_timer_enabled,
        state->restricted_now,
        json_bool(state->bedtime_active),
        json_bool(state->parent_unlock_active),
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
    state->play_timer_enabled = -1;
    state->restricted_now = -1;
    state->bedtime_active = false;
    state->parent_unlock_active = false;
    state->raw_block_verified = false;
    state->suspend_verified = false;
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
