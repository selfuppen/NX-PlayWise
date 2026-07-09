#include "request_client.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *rule_mode_name(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_UNLIMITED:
        return "unlimited";
    case PTC_RULE_MODE_BLOCKED:
        return "blocked";
    case PTC_RULE_MODE_LIMIT:
    default:
        return "limit";
    }
}

static const char *limit_action_name(PtcLimitAction action)
{
    switch (action) {
    case PTC_LIMIT_ACTION_RAW_BLOCK:
        return "raw_block";
    case PTC_LIMIT_ACTION_SUSPEND:
        return "suspend";
    case PTC_LIMIT_ACTION_REMIND:
    default:
        return "remind";
    }
}

static const char *json_bool(bool value)
{
    return value ? "true" : "false";
}

int ptc_companion_status_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"status\",\"created_at\":%lld,\"payload\":{}}\n", request_id, (long long)created_at);
}

int ptc_companion_offline_code_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *code)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"offline_code\",\"created_at\":%lld,\"payload\":{\"code\":\"%s\"}}\n", request_id, (long long)created_at, code);
}

int ptc_companion_parent_minutes_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *type, uint16_t minutes)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"created_at\":%lld,\"payload\":{\"minutes\":%u}}\n", request_id, type, (long long)created_at, minutes);
}

int ptc_companion_empty_payload_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *type)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"created_at\":%lld,\"payload\":{}}\n", request_id, type, (long long)created_at);
}

int ptc_companion_set_weekly_template_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const PtcDayRule week[7])
{
    int written;
    size_t used;
    unsigned int i;
    written = snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"set_weekly_template\",\"created_at\":%lld,\"payload\":{\"days\":[", request_id, (long long)created_at);
    if (written < 0 || (size_t)written >= out_size) {
        return written;
    }
    for (i = 0; i < 7; ++i) {
        used = strlen(out);
        written = snprintf(out + used, out_size - used, "%s{\"mode\":\"%s\",\"minutes\":%u}", i == 0 ? "" : ",", rule_mode_name(week[i].mode), week[i].minutes);
        if (written < 0 || (size_t)written >= out_size - used) {
            return (int)(used + (written > 0 ? written : 0));
        }
    }
    used = strlen(out);
    written = snprintf(out + used, out_size - used, "]}}\n");
    return written < 0 ? written : (int)(used + written);
}

int ptc_companion_set_bedtime_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const PtcBedtimeRule *bedtime)
{
    return snprintf(
        out,
        out_size,
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"set_bedtime\",\"created_at\":%lld,\"payload\":{\"enabled\":%s,\"start_min\":%u,\"end_min\":%u}}\n",
        request_id,
        (long long)created_at,
        json_bool(bedtime->enabled),
        bedtime->start_min,
        bedtime->end_min);
}

int ptc_companion_set_limit_action_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, PtcLimitAction action)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"set_limit_action\",\"created_at\":%lld,\"payload\":{\"action\":\"%s\"}}\n", request_id, (long long)created_at, limit_action_name(action));
}

int ptc_companion_parent_unlock_start_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, uint16_t duration_minutes)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"parent_unlock_start\",\"created_at\":%lld,\"payload\":{\"duration_minutes\":%u}}\n", request_id, (long long)created_at, duration_minutes);
}
