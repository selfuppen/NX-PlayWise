#include "redemption_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_value(const char *text, const char *key)
{
    char needle[64];
    const char *position;
    if (!text || !key || snprintf(needle, sizeof(needle), "\"%s\"", key) <= 0) return NULL;
    position = strstr(text, needle);
    if (!position) return NULL;
    position = strchr(position + strlen(needle), ':');
    if (!position) return NULL;
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

static bool parse_bool(const char *text, const char *key, bool *out)
{
    const char *value = find_value(text, key);
    if (!value || !out) return false;
    if (strncmp(value, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(value, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

bool ptc_redemption_history_parse_line(const char *line, PtcRedemptionHistoryRecord *out)
{
    int64_t version;
    int64_t day_index;
    int64_t token_version;
    int64_t grant_minutes;
    int64_t effective_add_minutes;
    PtcRedemptionHistoryRecord parsed;
    if (!line || !out || !parse_i64(line, "version", &version) || version != 1 ||
        !parse_i64(line, "redeemed_at", &parsed.redeemed_at) || parsed.redeemed_at <= 0 ||
        !parse_i64(line, "day_index", &day_index) || day_index < 0 || day_index > UINT16_MAX ||
        !parse_i64(line, "token_version", &token_version) || (token_version != 1 && token_version != 2) ||
        !parse_i64(line, "grant_minutes", &grant_minutes) || grant_minutes < 1 || grant_minutes > 1440 ||
        !parse_i64(line, "effective_add_minutes", &effective_add_minutes) ||
        effective_add_minutes < 0 || effective_add_minutes > grant_minutes ||
        !parse_bool(line, "remaining_after_available", &parsed.remaining_after_available) ||
        !parse_i64(line, "remaining_after_minutes", &parsed.remaining_after_minutes)) {
        return false;
    }
    if ((parsed.remaining_after_available &&
         (parsed.remaining_after_minutes < 0 || parsed.remaining_after_minutes > 1440)) ||
        (!parsed.remaining_after_available && parsed.remaining_after_minutes != -1)) {
        return false;
    }
    parsed.day_index = (uint16_t)day_index;
    parsed.token_version = (unsigned int)token_version;
    parsed.grant_minutes = (uint16_t)grant_minutes;
    parsed.effective_add_minutes = (uint16_t)effective_add_minutes;
    *out = parsed;
    return true;
}

bool ptc_redemption_history_format_line(
    char *out,
    size_t out_size,
    const PtcRedemptionHistoryRecord *record)
{
    int written;
    PtcRedemptionHistoryRecord checked;
    if (!out || out_size == 0 || !record) return false;
    written = snprintf(
        out,
        out_size,
        "{\"version\":1,\"redeemed_at\":%lld,\"day_index\":%u,\"token_version\":%u,"
        "\"grant_minutes\":%u,\"effective_add_minutes\":%u,"
        "\"remaining_after_available\":%s,\"remaining_after_minutes\":%lld}",
        (long long)record->redeemed_at,
        (unsigned int)record->day_index,
        record->token_version,
        (unsigned int)record->grant_minutes,
        (unsigned int)record->effective_add_minutes,
        record->remaining_after_available ? "true" : "false",
        (long long)record->remaining_after_minutes);
    return written > 0 && (size_t)written < out_size &&
        ptc_redemption_history_parse_line(out, &checked);
}
