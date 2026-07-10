#include "request_schema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "schema_version.h"

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

static bool json_string(const char *text, const char *key, char *out, size_t out_size)
{
    const char *pos = find_key(text, key);
    const char *start;
    const char *end;
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
    if (!end || (size_t)(end - start) >= out_size) {
        return false;
    }
    memcpy(out, start, (size_t)(end - start));
    out[end - start] = '\0';
    return true;
}

static bool json_i64(const char *text, const char *key, int64_t *out)
{
    const char *pos = find_key(text, key);
    char *endptr;
    long long value;
    if (!pos) {
        return false;
    }
    pos = strchr(pos + strlen(key) + 2, ':');
    if (!pos) {
        return false;
    }
    pos = skip_ws(pos + 1);
    value = strtoll(pos, &endptr, 10);
    if (endptr == pos) {
        return false;
    }
    *out = (int64_t)value;
    return true;
}

static bool json_u16(const char *text, const char *key, uint16_t *out)
{
    int64_t value;
    if (!json_i64(text, key, &value) || value < 0 || value > 65535) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

static bool json_bool_value(const char *text, const char *key, bool *out)
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
    if (strncmp(pos, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_rule_mode(const char *value, PtcRuleMode *out)
{
    if (strcmp(value, "limit") == 0) {
        *out = PTC_RULE_MODE_LIMIT;
        return true;
    }
    if (strcmp(value, "unlimited") == 0) {
        *out = PTC_RULE_MODE_UNLIMITED;
        return true;
    }
    if (strcmp(value, "blocked") == 0) {
        *out = PTC_RULE_MODE_BLOCKED;
        return true;
    }
    return false;
}

static bool parse_limit_action(const char *value, PtcLimitAction *out)
{
    if (strcmp(value, "remind") == 0) {
        *out = PTC_LIMIT_ACTION_REMIND;
        return true;
    }
    if (strcmp(value, "raw_block") == 0) {
        *out = PTC_LIMIT_ACTION_RAW_BLOCK;
        return true;
    }
    if (strcmp(value, "suspend") == 0) {
        *out = PTC_LIMIT_ACTION_SUSPEND;
        return true;
    }
    return false;
}

static bool parse_week_template(const char *text, PtcDayRule week[7])
{
    const char *days = find_key(text, "days");
    unsigned int i;
    if (!days) {
        return false;
    }
    days = strchr(days, '[');
    if (!days) {
        return false;
    }
    for (i = 0; i < 7; ++i) {
        const char *obj_start = strchr(days, '{');
        const char *obj_end;
        char item[192];
        char mode[24];
        size_t len;
        if (!obj_start) {
            return false;
        }
        obj_end = strchr(obj_start, '}');
        if (!obj_end) {
            return false;
        }
        len = (size_t)(obj_end - obj_start + 1);
        if (len >= sizeof(item)) {
            return false;
        }
        memcpy(item, obj_start, len);
        item[len] = '\0';
        if (!json_string(item, "mode", mode, sizeof(mode)) ||
            !parse_rule_mode(mode, &week[i].mode)) {
            return false;
        }
        if (!json_u16(item, "minutes", &week[i].minutes)) {
            week[i].minutes = 0;
        }
        days = obj_end + 1;
    }
    return true;
}

PtcRequestType ptc_request_type_from_string(const char *value)
{
    if (!value) {
        return PTC_REQUEST_UNKNOWN;
    }
    if (strcmp(value, "offline_code") == 0) {
        return PTC_REQUEST_OFFLINE_CODE;
    }
    if (strcmp(value, "status") == 0) {
        return PTC_REQUEST_STATUS;
    }
    if (strcmp(value, "set_today_limit") == 0) {
        return PTC_REQUEST_SET_TODAY_LIMIT;
    }
    if (strcmp(value, "add_today_minutes") == 0) {
        return PTC_REQUEST_ADD_TODAY_MINUTES;
    }
    if (strcmp(value, "disable_today_limit") == 0) {
        return PTC_REQUEST_DISABLE_TODAY_LIMIT;
    }
    if (strcmp(value, "block_today") == 0) {
        return PTC_REQUEST_BLOCK_TODAY;
    }
    if (strcmp(value, "restore_today_policy") == 0) {
        return PTC_REQUEST_RESTORE_TODAY_POLICY;
    }
    if (strcmp(value, "set_weekly_template") == 0) {
        return PTC_REQUEST_SET_WEEKLY_TEMPLATE;
    }
    if (strcmp(value, "set_bedtime") == 0) {
        return PTC_REQUEST_SET_BEDTIME;
    }
    if (strcmp(value, "set_limit_action") == 0) {
        return PTC_REQUEST_SET_LIMIT_ACTION;
    }
    if (strcmp(value, "parent_unlock_start") == 0) {
        return PTC_REQUEST_PARENT_UNLOCK_START;
    }
    if (strcmp(value, "parent_unlock_end") == 0) {
        return PTC_REQUEST_PARENT_UNLOCK_END;
    }
    if (strcmp(value, "probe_raw_block") == 0) {
        return PTC_REQUEST_PROBE_RAW_BLOCK;
    }
    if (strcmp(value, "probe_suspend") == 0) {
        return PTC_REQUEST_PROBE_SUSPEND;
    }
    if (strcmp(value, "probe_play_timer_write") == 0) {
        return PTC_REQUEST_PROBE_PLAY_TIMER_WRITE;
    }
    return PTC_REQUEST_UNKNOWN;
}

const char *ptc_request_type_name(PtcRequestType type)
{
    switch (type) {
    case PTC_REQUEST_OFFLINE_CODE:
        return "offline_code";
    case PTC_REQUEST_STATUS:
        return "status";
    case PTC_REQUEST_SET_TODAY_LIMIT:
        return "set_today_limit";
    case PTC_REQUEST_ADD_TODAY_MINUTES:
        return "add_today_minutes";
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
        return "disable_today_limit";
    case PTC_REQUEST_BLOCK_TODAY:
        return "block_today";
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
        return "restore_today_policy";
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
        return "set_weekly_template";
    case PTC_REQUEST_SET_BEDTIME:
        return "set_bedtime";
    case PTC_REQUEST_SET_LIMIT_ACTION:
        return "set_limit_action";
    case PTC_REQUEST_PARENT_UNLOCK_START:
        return "parent_unlock_start";
    case PTC_REQUEST_PARENT_UNLOCK_END:
        return "parent_unlock_end";
    case PTC_REQUEST_PROBE_RAW_BLOCK:
        return "probe_raw_block";
    case PTC_REQUEST_PROBE_SUSPEND:
        return "probe_suspend";
    case PTC_REQUEST_PROBE_PLAY_TIMER_WRITE:
        return "probe_play_timer_write";
    case PTC_REQUEST_UNKNOWN:
    default:
        return "unknown";
    }
}

PtcErrorCode ptc_request_parse(const char *text, PtcRequest *out)
{
    int64_t version;
    char action[24];
    if (!text || !out || strchr(text, '{') == NULL || strchr(text, '}') == NULL) {
        return PTC_ERR_BAD_REQUEST;
    }
    memset(out, 0, sizeof(*out));
    if (!json_i64(text, "version", &version)) {
        return PTC_ERR_BAD_REQUEST;
    }
    if (version != PTC_SCHEMA_VERSION) {
        return PTC_ERR_UNSUPPORTED_VERSION;
    }
    if (!json_string(text, "request_id", out->request_id, sizeof(out->request_id)) ||
        !json_string(text, "type", out->type_text, sizeof(out->type_text)) ||
        !json_i64(text, "created_at", &out->created_at) ||
        !find_key(text, "payload")) {
        return PTC_ERR_BAD_REQUEST;
    }
    out->type = ptc_request_type_from_string(out->type_text);
    if (out->type == PTC_REQUEST_UNKNOWN) {
        return PTC_ERR_UNKNOWN_REQUEST_TYPE;
    }

    switch (out->type) {
    case PTC_REQUEST_OFFLINE_CODE:
        return json_string(text, "code", out->code, sizeof(out->code)) ? PTC_ERR_OK : PTC_ERR_BAD_REQUEST;
    case PTC_REQUEST_SET_TODAY_LIMIT:
    case PTC_REQUEST_ADD_TODAY_MINUTES:
        return json_u16(text, "minutes", &out->minutes) ? PTC_ERR_OK : PTC_ERR_BAD_REQUEST;
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
        return parse_week_template(text, out->week) ? PTC_ERR_OK : PTC_ERR_BAD_REQUEST;
    case PTC_REQUEST_SET_BEDTIME:
        return json_bool_value(text, "enabled", &out->bedtime.enabled) &&
            json_u16(text, "start_min", &out->bedtime.start_min) &&
            json_u16(text, "end_min", &out->bedtime.end_min) &&
            out->bedtime.start_min < 1440 && out->bedtime.end_min < 1440
            ? PTC_ERR_OK
            : PTC_ERR_BAD_REQUEST;
    case PTC_REQUEST_SET_LIMIT_ACTION:
        return json_string(text, "action", action, sizeof(action)) &&
            parse_limit_action(action, &out->limit_action)
            ? PTC_ERR_OK
            : PTC_ERR_BAD_REQUEST;
    case PTC_REQUEST_PARENT_UNLOCK_START:
        return json_u16(text, "duration_minutes", &out->duration_minutes) &&
            out->duration_minutes > 0
            ? PTC_ERR_OK
            : PTC_ERR_BAD_REQUEST;
    case PTC_REQUEST_STATUS:
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
    case PTC_REQUEST_BLOCK_TODAY:
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
    case PTC_REQUEST_PARENT_UNLOCK_END:
    case PTC_REQUEST_PROBE_RAW_BLOCK:
    case PTC_REQUEST_PROBE_SUSPEND:
    case PTC_REQUEST_PROBE_PLAY_TIMER_WRITE:
        return PTC_ERR_OK;
    case PTC_REQUEST_UNKNOWN:
    default:
        return PTC_ERR_UNKNOWN_REQUEST_TYPE;
    }
}
