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

static const char *bedtime_mode_name(PtcBedtimeOverrideMode mode)
{
    switch (mode) {
    case PTC_BEDTIME_OVERRIDE_DISABLED: return "disabled";
    case PTC_BEDTIME_OVERRIDE_CUSTOM: return "custom";
    case PTC_BEDTIME_OVERRIDE_INHERIT:
    default: return "inherit";
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

int ptc_companion_set_scheduled_override_request_json(char *out, size_t out_size,
    const char *request_id, int64_t created_at, const PtcScheduledOverride *scheduled_override)
{
    if (!scheduled_override) return -1;
    if (!scheduled_override->enabled) {
        return snprintf(out, out_size,
            "{\"version\":1,\"request_id\":\"%s\",\"type\":\"set_scheduled_override\","
            "\"created_at\":%lld,\"payload\":{\"enabled\":false}}\n",
            request_id, (long long)created_at);
    }
    return snprintf(out, out_size,
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"set_scheduled_override\","
        "\"created_at\":%lld,\"payload\":{\"enabled\":true,\"start_day_index\":%u,"
        "\"end_day_index\":%u,\"rule\":{\"mode\":\"%s\",\"minutes\":%u}}}\n",
        request_id, (long long)created_at, scheduled_override->start_day_index,
        scheduled_override->end_day_index, rule_mode_name(scheduled_override->rule.mode),
        scheduled_override->rule.minutes);
}

int ptc_companion_set_autonomy_policy_request_json(char *out, size_t out_size,
    const char *request_id, int64_t created_at, const PtcAutonomyPolicy *policy)
{
    if (!policy) return -1;
    return snprintf(out, out_size,
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"set_autonomy_policy\","
        "\"created_at\":%lld,\"payload\":{\"daily_buffer_minutes\":%u}}\n",
        request_id, (long long)created_at, policy->daily_buffer_minutes);
}

int ptc_companion_set_bedtime_policy_request_json(char *out, size_t out_size,
    const char *request_id, int64_t created_at, const PtcBedtimePolicy *policy, bool apply_immediately)
{
    size_t used;
    int written;
    unsigned int i;
    if (!out || !request_id || !policy || !ptc_bedtime_policy_is_valid(policy)) return -1;
    written = snprintf(out, out_size,
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"set_bedtime_policy\","
        "\"created_at\":%lld,\"payload\":{\"bedtime_enabled\":%s,\"bedtime_days\":[",
        request_id, (long long)created_at, policy->enabled ? "true" : "false");
    if (written < 0 || (size_t)written >= out_size) return written;
    for (i = 0; i < 7; ++i) {
        used = strlen(out);
        written = snprintf(out + used, out_size - used,
            "%s{\"enabled\":%s,\"start_minute\":%u,\"end_minute\":%u}",
            i ? "," : "", policy->week[i].enabled ? "true" : "false",
            policy->week[i].start_minute, policy->week[i].end_minute);
        if (written < 0 || (size_t)written >= out_size - used) return (int)(used + (written > 0 ? written : 0));
    }
    used = strlen(out);
    written = snprintf(out + used, out_size - used,
        "],\"bedtime_calendar_enabled\":%s,"
        "\"bedtime_holiday_mode\":\"%s\",\"bedtime_holiday_enabled\":%s,"
        "\"bedtime_holiday_start_minute\":%u,\"bedtime_holiday_end_minute\":%u,"
        "\"bedtime_makeup_mode\":\"%s\",\"bedtime_makeup_enabled\":%s,"
        "\"bedtime_makeup_start_minute\":%u,\"bedtime_makeup_end_minute\":%u,"
        "\"bedtime_scheduled_present\":%s,\"bedtime_scheduled_start_day_index\":%u,"
        "\"bedtime_scheduled_end_day_index\":%u,\"bedtime_scheduled_mode\":\"%s\","
        "\"bedtime_scheduled_enabled\":%s,\"bedtime_scheduled_start_minute\":%u,"
        "\"bedtime_scheduled_end_minute\":%u,\"activation\":\"%s\"}}\n",
        policy->calendar_enabled ? "true" : "false",
        bedtime_mode_name(policy->holiday_rule.mode), policy->holiday_rule.window.enabled ? "true" : "false",
        policy->holiday_rule.window.start_minute, policy->holiday_rule.window.end_minute,
        bedtime_mode_name(policy->makeup_workday_rule.mode), policy->makeup_workday_rule.window.enabled ? "true" : "false",
        policy->makeup_workday_rule.window.start_minute, policy->makeup_workday_rule.window.end_minute,
        policy->scheduled_override.present ? "true" : "false",
        policy->scheduled_override.start_day_index, policy->scheduled_override.end_day_index,
        bedtime_mode_name(policy->scheduled_override.rule.mode),
        policy->scheduled_override.rule.window.enabled ? "true" : "false",
        policy->scheduled_override.rule.window.start_minute,
        policy->scheduled_override.rule.window.end_minute,
        apply_immediately ? "immediate" : "next_window");
    return written < 0 ? written : (int)(used + written);
}

int ptc_companion_skip_bedtime_request_json(char *out, size_t out_size,
    const char *request_id, int64_t created_at, uint64_t window_instance_id)
{
    return snprintf(out, out_size,
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"skip_bedtime\","
        "\"created_at\":%lld,\"payload\":{\"window_instance_id\":%llu}}\n",
        request_id, (long long)created_at, (unsigned long long)window_instance_id);
}

int ptc_companion_confirm_bedtime_requirements_request_json(char *out, size_t out_size,
    const char *request_id, int64_t created_at, bool official_setting_confirmed,
    bool overlay_risk_accepted, uint16_t confirmation_version, const char *environment_fingerprint)
{
    return snprintf(out, out_size,
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"confirm_bedtime_requirements\","
        "\"created_at\":%lld,\"payload\":{\"official_setting_confirmed\":%s,"
        "\"overlay_risk_accepted\":%s,\"confirmation_version\":%u,"
        "\"environment_fingerprint\":\"%s\"}}\n",
        request_id, (long long)created_at, official_setting_confirmed ? "true" : "false",
        overlay_risk_accepted ? "true" : "false", confirmation_version,
        environment_fingerprint ? environment_fingerprint : "");
}

int ptc_companion_overlay_ready_request_json(char *out, size_t out_size,
    const char *request_id, int64_t created_at, const char *release_id,
    const char *boot_id, const char *environment_fingerprint)
{
    return snprintf(out, out_size,
        "{\"version\":1,\"request_id\":\"%s\",\"type\":\"overlay_ready\","
        "\"created_at\":%lld,\"payload\":{\"release_id\":\"%s\",\"boot_id\":\"%s\","
        "\"environment_fingerprint\":\"%s\"}}\n",
        request_id, (long long)created_at, release_id ? release_id : "",
        boot_id ? boot_id : "", environment_fingerprint ? environment_fingerprint : "");
}
