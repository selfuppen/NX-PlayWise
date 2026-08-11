#include "request_schema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "schema_version.h"

bool ptc_request_id_is_valid(const char *request_id)
{
    size_t len;
    size_t i;
    if (!request_id) return false;
    len = strlen(request_id);
    if (len == 0 || len >= 80) return false;
    for (i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)request_id[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) {
            return false;
        }
    }
    return true;
}

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
    const char *match;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    match = text;
    while ((match = strstr(match, pattern)) != NULL) {
        const char *after = skip_ws(match + strlen(pattern));
        if (*after == ':') return match;
        match += strlen(pattern);
    }
    return NULL;
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

static bool json_bool_required(const char *text, const char *key, bool *out)
{
    const char *pos = find_key(text, key);
    if (!pos) return false;
    pos = strchr(pos + strlen(key) + 2, ':');
    if (!pos) return false;
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

#ifdef PLAYWISE_DEVICE_LAB
static bool json_bool_optional(const char *text, const char *key, bool default_value, bool *out)
{
    const char *pos = find_key(text, key);
    if (!pos) {
        *out = default_value;
        return true;
    }
    pos = strchr(pos + strlen(key) + 2, ':');
    if (!pos) return false;
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
#endif

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

static bool parse_named_rule(const char *text, const char *key, PtcDayRule *rule)
{
    const char *pos = find_key(text, key);
    const char *start;
    const char *end;
    char item[192];
    char mode[24];
    size_t len;
    if (!pos || !rule) return false;
    start = strchr(pos, '{');
    if (!start || !(end = strchr(start, '}'))) return false;
    len = (size_t)(end - start + 1);
    if (len >= sizeof(item)) return false;
    memcpy(item, start, len);
    item[len] = '\0';
    if (!json_string(item, "mode", mode, sizeof(mode)) || !parse_rule_mode(mode, &rule->mode)) return false;
    if (!json_u16(item, "minutes", &rule->minutes)) rule->minutes = 0;
    return rule->mode == PTC_RULE_MODE_UNLIMITED || (rule->minutes >= 1 && rule->minutes <= 1440);
}

PtcRequestType ptc_request_type_from_string(const char *value)
{
    if (!value) {
        return PTC_REQUEST_UNKNOWN;
    }
    if (strcmp(value, "offline_code") == 0) {
        return PTC_REQUEST_OFFLINE_CODE;
    }
    if (strcmp(value, "preview_offline_code") == 0) {
        return PTC_REQUEST_PREVIEW_OFFLINE_CODE;
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
    if (strcmp(value, "restore_today_policy") == 0) {
        return PTC_REQUEST_RESTORE_TODAY_POLICY;
    }
    if (strcmp(value, "set_weekly_template") == 0) {
        return PTC_REQUEST_SET_WEEKLY_TEMPLATE;
    }
    if (strcmp(value, "set_holiday_policy") == 0) {
        return PTC_REQUEST_SET_HOLIDAY_POLICY;
    }
    if (strcmp(value, "complete_setup") == 0) {
        return PTC_REQUEST_COMPLETE_SETUP;
    }
    if (strcmp(value, "retry_setup_release") == 0) {
        return PTC_REQUEST_RETRY_SETUP_RELEASE;
    }
    if (strcmp(value, "restore_install_snapshot") == 0) {
        return PTC_REQUEST_RESTORE_INSTALL_SNAPSHOT;
    }
#ifdef PLAYWISE_DEVICE_LAB
    if (strcmp(value, "probe_raw_block") == 0) return PTC_REQUEST_REMOVED_13;
    if (strcmp(value, "probe_suspend") == 0) return PTC_REQUEST_REMOVED_14;
    if (strcmp(value, "probe_play_timer_write") == 0) return PTC_REQUEST_REMOVED_15;
    if (strcmp(value, "probe_apply_today_limit") == 0) return PTC_REQUEST_REMOVED_16;
    if (strcmp(value, "probe_play_timer_effect") == 0) return PTC_REQUEST_REMOVED_17;
    if (strcmp(value, "prepare_device_test") == 0) return PTC_REQUEST_REMOVED_18;
#endif
    return PTC_REQUEST_UNKNOWN;
}

const char *ptc_request_type_name(PtcRequestType type)
{
    switch (type) {
    case PTC_REQUEST_OFFLINE_CODE:
        return "offline_code";
    case PTC_REQUEST_PREVIEW_OFFLINE_CODE:
        return "preview_offline_code";
    case PTC_REQUEST_STATUS:
        return "status";
    case PTC_REQUEST_SET_TODAY_LIMIT:
        return "set_today_limit";
    case PTC_REQUEST_ADD_TODAY_MINUTES:
        return "add_today_minutes";
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
        return "disable_today_limit";
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
        return "restore_today_policy";
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
        return "set_weekly_template";
    case PTC_REQUEST_SET_HOLIDAY_POLICY:
        return "set_holiday_policy";
    case PTC_REQUEST_COMPLETE_SETUP:
        return "complete_setup";
    case PTC_REQUEST_RETRY_SETUP_RELEASE:
        return "retry_setup_release";
    case PTC_REQUEST_RESTORE_INSTALL_SNAPSHOT:
        return "restore_install_snapshot";
#ifdef PLAYWISE_DEVICE_LAB
    case PTC_REQUEST_REMOVED_13:
        return "probe_raw_block";
    case PTC_REQUEST_REMOVED_14:
        return "probe_suspend";
    case PTC_REQUEST_REMOVED_15:
        return "probe_play_timer_write";
    case PTC_REQUEST_REMOVED_16:
        return "probe_apply_today_limit";
    case PTC_REQUEST_REMOVED_17:
        return "probe_play_timer_effect";
    case PTC_REQUEST_REMOVED_18:
        return "prepare_device_test";
#endif
    case PTC_REQUEST_UNKNOWN:
    default:
        return "unknown";
    }
}

PtcErrorCode ptc_request_parse(const char *text, PtcRequest *out)
{
    int64_t version;
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
        !ptc_request_id_is_valid(out->request_id) ||
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
    case PTC_REQUEST_PREVIEW_OFFLINE_CODE:
        return json_string(text, "code", out->code, sizeof(out->code)) ? PTC_ERR_OK : PTC_ERR_BAD_REQUEST;
    case PTC_REQUEST_SET_TODAY_LIMIT:
    case PTC_REQUEST_ADD_TODAY_MINUTES:
        return json_u16(text, "minutes", &out->minutes) ? PTC_ERR_OK : PTC_ERR_BAD_REQUEST;
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
        return parse_week_template(text, out->week) ? PTC_ERR_OK : PTC_ERR_BAD_REQUEST;
    case PTC_REQUEST_SET_HOLIDAY_POLICY:
        return json_bool_required(text, "enabled", &out->holiday_enabled) &&
            parse_named_rule(text, "holiday_rule", &out->holiday_rule) &&
            parse_named_rule(text, "makeup_workday_rule", &out->makeup_workday_rule)
            ? PTC_ERR_OK : PTC_ERR_BAD_REQUEST;
    case PTC_REQUEST_COMPLETE_SETUP:
    case PTC_REQUEST_RETRY_SETUP_RELEASE:
    case PTC_REQUEST_RESTORE_INSTALL_SNAPSHOT:
    case PTC_REQUEST_STATUS:
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
        return PTC_ERR_OK;
#ifdef PLAYWISE_DEVICE_LAB
    case PTC_REQUEST_REMOVED_13:
    case PTC_REQUEST_REMOVED_14:
    case PTC_REQUEST_REMOVED_15:
    case PTC_REQUEST_REMOVED_18:
        return PTC_ERR_OK;
    case PTC_REQUEST_REMOVED_16:
        out->minutes = 1;
        (void)json_u16(text, "minutes", &out->minutes);
        return json_bool_optional(text, "start_timer", false, &out->start_timer)
            ? PTC_ERR_OK : PTC_ERR_BAD_REQUEST;
    case PTC_REQUEST_REMOVED_17:
        return json_bool_optional(text, "wait_for_expiry", false, &out->wait_for_expiry)
            ? PTC_ERR_OK : PTC_ERR_BAD_REQUEST;
#endif
    case PTC_REQUEST_UNKNOWN:
    default:
        return PTC_ERR_UNKNOWN_REQUEST_TYPE;
    }
}
