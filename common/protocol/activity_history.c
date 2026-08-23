#include "activity_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_value(const char *text, const char *key)
{
    char needle[64];
    const char *position;
    if (!text || !key || snprintf(needle, sizeof(needle), "\"%s\"", key) <= 0) return NULL;
    position = strstr(text, needle);
    if (!position || !(position = strchr(position + strlen(needle), ':'))) return NULL;
    return position + 1;
}

static bool parse_i64(const char *text, const char *key, int64_t *out)
{
    const char *value = find_value(text, key);
    char *end;
    long long parsed;
    if (!value || !out) return false;
    parsed = strtoll(value, &end, 10);
    if (end == value) return false;
    *out = (int64_t)parsed;
    return true;
}

static bool parse_string(const char *text, const char *key, char *out, size_t out_size)
{
    const char *value = find_value(text, key);
    const char *end;
    size_t length;
    if (!value || !out || out_size == 0) return false;
    while (*value == ' ' || *value == '\t') ++value;
    if (*value++ != '"' || !(end = strchr(value, '"'))) return false;
    length = (size_t)(end - value);
    if (length == 0 || length >= out_size) return false;
    memcpy(out, value, length);
    out[length] = '\0';
    return true;
}

static bool action_is_valid(const char *action)
{
    static const char *const ACTIONS[] = {
        "today_limit", "today_add", "today_unlimited", "today_restore",
        "weekly_update", "holiday_update", "scheduled_update", "autonomy_update",
        "offline_grant", "daily_buffer", "protection", "activity_cleared"
    };
    size_t i;
    for (i = 0; i < sizeof(ACTIONS) / sizeof(ACTIONS[0]); ++i) {
        if (strcmp(action, ACTIONS[i]) == 0) return true;
    }
    return false;
}

bool ptc_activity_history_parse_line(const char *line, PtcActivityHistoryRecord *out)
{
    PtcActivityHistoryRecord parsed;
    int64_t version;
    int64_t day_index;
    int64_t minutes;
    int64_t effective_minutes;
    if (!line || !out || !parse_i64(line, "version", &version) || version != 1 ||
        !parse_i64(line, "occurred_at", &parsed.occurred_at) || parsed.occurred_at <= 0 ||
        !parse_i64(line, "day_index", &day_index) || day_index < 0 || day_index > UINT16_MAX ||
        !parse_string(line, "action", parsed.action, sizeof(parsed.action)) ||
        !action_is_valid(parsed.action) ||
        !parse_i64(line, "minutes", &minutes) || minutes < 0 || minutes > 1440 ||
        !parse_i64(line, "effective_minutes", &effective_minutes) ||
        effective_minutes < 0 || effective_minutes > minutes) return false;
    parsed.day_index = (uint16_t)day_index;
    parsed.minutes = (uint16_t)minutes;
    parsed.effective_minutes = (uint16_t)effective_minutes;
    *out = parsed;
    return true;
}

bool ptc_activity_history_format_line(
    char *out, size_t out_size, const PtcActivityHistoryRecord *record)
{
    int written;
    PtcActivityHistoryRecord checked;
    if (!out || out_size == 0 || !record) return false;
    written = snprintf(out, out_size,
        "{\"version\":1,\"occurred_at\":%lld,\"day_index\":%u,\"action\":\"%s\","
        "\"minutes\":%u,\"effective_minutes\":%u}",
        (long long)record->occurred_at, (unsigned int)record->day_index, record->action,
        (unsigned int)record->minutes, (unsigned int)record->effective_minutes);
    return written > 0 && (size_t)written < out_size &&
        ptc_activity_history_parse_line(out, &checked);
}
