#include "daily_summary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_value(const char *text, const char *key)
{
    char needle[64];
    const char *position;
    if (!text || snprintf(needle, sizeof(needle), "\"%s\"", key) <= 0) return NULL;
    position = strstr(text, needle);
    if (!position || !(position = strchr(position + strlen(needle), ':'))) return NULL;
    return position + 1;
}

static bool parse_i64(const char *text, const char *key, int64_t *out)
{
    const char *value = find_value(text, key);
    char *end;
    long long parsed;
    if (!value) return false;
    parsed = strtoll(value, &end, 10);
    if (end == value) return false;
    *out = (int64_t)parsed;
    return true;
}

static bool parse_bool(const char *text, const char *key, bool *out)
{
    const char *value = find_value(text, key);
    if (!value) return false;
    if (strncmp(value, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(value, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static bool parse_string(const char *text, const char *key, char *out, size_t out_size)
{
    const char *value = find_value(text, key);
    const char *end;
    size_t length;
    if (!value) return false;
    while (*value == ' ' || *value == '\t') ++value;
    if (*value++ != '"' || !(end = strchr(value, '"'))) return false;
    length = (size_t)(end - value);
    if (length == 0 || length >= out_size) return false;
    memcpy(out, value, length);
    out[length] = '\0';
    return true;
}

bool ptc_daily_summary_parse_line(const char *line, PtcDailySummaryRecord *out)
{
    PtcDailySummaryRecord parsed;
    int64_t version, day_index, configured, remaining, consumed, granted;
    if (!line || !out || !parse_i64(line, "version", &version) || version != 1 ||
        !parse_i64(line, "day_index", &day_index) || day_index < 0 || day_index > UINT16_MAX ||
        !parse_i64(line, "captured_at", &parsed.captured_at) || parsed.captured_at <= 0 ||
        !parse_string(line, "rule_source", parsed.rule_source, sizeof(parsed.rule_source)) ||
        !parse_bool(line, "limited", &parsed.limited) ||
        !parse_i64(line, "configured_minutes", &configured) || configured < 0 || configured > 1440 ||
        !parse_bool(line, "remaining_available", &parsed.remaining_available) ||
        !parse_i64(line, "remaining_minutes", &remaining) || remaining < 0 || remaining > 1440 ||
        !parse_bool(line, "consumed_available", &parsed.consumed_available) ||
        !parse_i64(line, "consumed_minutes", &consumed) || consumed < 0 || consumed > 1440 ||
        !parse_i64(line, "granted_minutes", &granted) || granted < 0 || granted > 1440) return false;
    if ((!parsed.remaining_available && remaining != 0) ||
        (!parsed.consumed_available && consumed != 0)) return false;
    parsed.day_index = (uint16_t)day_index;
    parsed.configured_minutes = (uint16_t)configured;
    parsed.remaining_minutes = (uint16_t)remaining;
    parsed.consumed_minutes = (uint16_t)consumed;
    parsed.granted_minutes = (uint16_t)granted;
    *out = parsed;
    return true;
}

bool ptc_daily_summary_format_line(
    char *out, size_t out_size, const PtcDailySummaryRecord *record)
{
    int written;
    PtcDailySummaryRecord checked;
    if (!out || !record) return false;
    written = snprintf(out, out_size,
        "{\"version\":1,\"day_index\":%u,\"captured_at\":%lld,\"rule_source\":\"%s\","
        "\"limited\":%s,\"configured_minutes\":%u,\"remaining_available\":%s,"
        "\"remaining_minutes\":%u,\"consumed_available\":%s,"
        "\"consumed_minutes\":%u,\"granted_minutes\":%u}",
        (unsigned int)record->day_index, (long long)record->captured_at, record->rule_source,
        record->limited ? "true" : "false", (unsigned int)record->configured_minutes,
        record->remaining_available ? "true" : "false", (unsigned int)record->remaining_minutes,
        record->consumed_available ? "true" : "false", (unsigned int)record->consumed_minutes,
        (unsigned int)record->granted_minutes);
    return written > 0 && (size_t)written < out_size && ptc_daily_summary_parse_line(out, &checked);
}

void ptc_daily_summary_aggregate(const PtcDailySummaryRecord *records, size_t count,
    uint16_t today_day_index, PtcDailySummaryAggregate *out)
{
    size_t i;
    memset(out, 0, sizeof(*out));
    if (!records) return;
    for (i = 0; i < count; ++i) {
        uint16_t age;
        size_t duplicate;
        if (!records[i].consumed_available || records[i].day_index > today_day_index) continue;
        /* A day contributes at most once. If storage contains duplicate or
           out-of-order rows, the newest trustworthy capture wins. */
        for (duplicate = 0; duplicate < count; ++duplicate) {
            if (duplicate != i && records[duplicate].day_index == records[i].day_index &&
                records[duplicate].consumed_available &&
                (records[duplicate].captured_at > records[i].captured_at ||
                 (records[duplicate].captured_at == records[i].captured_at && duplicate > i))) break;
        }
        if (duplicate < count) continue;
        age = (uint16_t)(today_day_index - records[i].day_index);
        if (age < 30u) {
            ++out->known_days_30;
            out->consumed_minutes_30 += records[i].consumed_minutes;
        }
        if (age < 7u) {
            ++out->known_days_7;
            out->consumed_minutes_7 += records[i].consumed_minutes;
        }
    }
}
