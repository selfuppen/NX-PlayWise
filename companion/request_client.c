#include "request_client.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *rule_mode_name(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_UNLIMITED:
        return "unlimited";
    case PTC_RULE_MODE_LIMIT:
    default:
        return "limit";
    }
}

int ptc_companion_status_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"status\",\"created_at\":%lld,\"payload\":{}}\n", request_id, (long long)created_at);
}

int ptc_companion_offline_code_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *code)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"offline_code\",\"created_at\":%lld,\"payload\":{\"code\":\"%s\"}}\n", request_id, (long long)created_at, code);
}

int ptc_companion_preview_offline_code_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *code)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"preview_offline_code\",\"created_at\":%lld,\"payload\":{\"code\":\"%s\"}}\n", request_id, (long long)created_at, code);
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

int ptc_companion_set_holiday_policy_request_json(char *out, size_t out_size, const char *request_id,
    int64_t created_at, bool enabled, PtcDayRule holiday_rule, PtcDayRule makeup_workday_rule)
{
    return snprintf(out, out_size,
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"set_holiday_policy\",\"created_at\":%lld,"
        "\"payload\":{\"enabled\":%s,\"holiday_rule\":{\"mode\":\"%s\",\"minutes\":%u},"
        "\"makeup_workday_rule\":{\"mode\":\"%s\",\"minutes\":%u}}}\n",
        request_id, (long long)created_at, enabled ? "true" : "false",
        rule_mode_name(holiday_rule.mode), holiday_rule.minutes,
        rule_mode_name(makeup_workday_rule.mode), makeup_workday_rule.minutes);
}
