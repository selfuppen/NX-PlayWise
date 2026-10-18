#include "daily_summary.h"
#include "usage_counter.h"

#include <stdio.h>
#include <string.h>

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    return p;
}

/* Flat JSON with ASCII enum strings. Match keys structurally and validate
   delimiters so malformed precision/source fields cannot become known zero. */
static bool field(const char *text, const char *key, const char **value, size_t *length)
{
    const char *p = skip_ws(text);
    bool found = false;
    if (*p++ != '{') return false;
    p = skip_ws(p);
    while (*p && *p != '}') {
        const char *name, *start;
        size_t name_length;
        if (*p++ != '"') return false;
        name = p;
        while (*p && *p != '"' && *p != '\\' && (unsigned char)*p >= 32) ++p;
        if (*p != '"') return false;
        name_length = (size_t)(p++ - name);
        p = skip_ws(p);
        if (*p++ != ':') return false;
        p = skip_ws(p);
        start = p;
        if (*p == '"') {
            ++p;
            while (*p && *p != '"' && *p != '\\' && (unsigned char)*p >= 32) ++p;
            if (*p != '"') return false;
            ++p;
        } else {
            while (*p && *p != ',' && *p != '}' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ++p;
            if (p == start) return false;
        }
        if (strlen(key) == name_length && memcmp(name, key, name_length) == 0) {
            if (found) return false;
            found = true; *value = start; *length = (size_t)(p - start);
        }
        p = skip_ws(p);
        if (*p == '}') break;
        if (*p++ != ',') return false;
        p = skip_ws(p);
        if (*p == '}') return false;
    }
    return *p == '}' && !*skip_ws(p + 1) && found;
}

static bool digits(const char *p, size_t length, uint64_t max, uint64_t *out)
{
    uint64_t n = 0;
    size_t i;
    if (!length || (length > 1 && *p == '0')) return false;
    for (i = 0; i < length; ++i) {
        unsigned d = (unsigned)(p[i] - '0');
        if (d > 9 || n > max / 10 || (n == max / 10 && d > max % 10)) return false;
        n = n * 10 + d;
    }
    *out = n;
    return true;
}

static bool number(const char *text, const char *key, uint64_t max, uint64_t *out)
{
    const char *p; size_t n;
    return field(text, key, &p, &n) && digits(p, n, max, out);
}

static bool boolean(const char *text, const char *key, bool *out)
{
    const char *p; size_t n;
    if (!field(text, key, &p, &n)) return false;
    if (n == 4 && memcmp(p, "true", 4) == 0) { *out = true; return true; }
    if (n == 5 && memcmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static bool string(const char *text, const char *key, char *out, size_t size)
{
    const char *p; size_t n;
    if (!field(text, key, &p, &n) || n < 3 || *p != '"' || p[n - 1] != '"' || n - 2 >= size) return false;
    memcpy(out, p + 1, n - 2); out[n - 2] = '\0';
    return true;
}

bool ptc_daily_summary_parse_line(const char *line, PtcDailySummaryRecord *out)
{
    PtcDailySummaryRecord r = {0};
    uint64_t version, day, capture, configured, remaining, consumed, granted, observed;
    char source[32], scope[32], coverage[16], total[24];
    if (!line || !out || !number(line, "version", 2, &version) || !version ||
        !number(line, "day_index", UINT16_MAX, &day) ||
        !number(line, "captured_at", INT64_MAX, &capture) ||
        !string(line, "rule_source", r.rule_source, sizeof(r.rule_source)) ||
        !boolean(line, "limited", &r.limited) ||
        !number(line, "configured_minutes", 1440, &configured) ||
        !boolean(line, "remaining_available", &r.remaining_available) ||
        !number(line, "remaining_minutes", 1440, &remaining) ||
        !boolean(line, "consumed_available", &r.consumed_available) ||
        !number(line, "consumed_minutes", 1440, &consumed) ||
        !number(line, "granted_minutes", 1440, &granted)) return false;
    if ((!r.remaining_available && remaining) || (!r.consumed_available && consumed)) return false;
    r.day_index = (uint16_t)day; r.captured_at = (int64_t)capture;
    r.configured_minutes = (uint16_t)configured; r.remaining_minutes = (uint16_t)remaining;
    r.consumed_minutes = (uint16_t)consumed; r.granted_minutes = (uint16_t)granted;
    if (version == 1) {
        const char *typed; size_t length;
        if (!capture) return false;
        if (field(line, "consumption_source", &typed, &length) ||
            field(line, "total_ns", &typed, &length)) return false;
    } else {
        if (!string(line, "consumption_source", source, sizeof(source)) || strcmp(source, "pctl_1952") ||
            !string(line, "scope", scope, sizeof(scope)) || strcmp(scope, "console_usable_time") ||
            !string(line, "coverage", coverage, sizeof(coverage)) || strcmp(coverage, "partial") ||
            !string(line, "total_ns", total, sizeof(total)) ||
            !digits(total, strlen(total), PTC_DAILY_SUMMARY_DAY_NS, &r.total_ns) ||
            !number(line, "observed_at", INT64_MAX, &observed) || !observed ||
            !boolean(line, "latest_available", &r.latest_available)) return false;
        if ((r.consumed_available && (!capture || observed < capture || consumed != r.total_ns / PTC_USAGE_MINUTE_NS)) ||
            (!r.consumed_available && (capture || r.total_ns || r.latest_available))) return false;
        r.direct_source = true; r.observed_at = (int64_t)observed;
    }
    *out = r;
    return true;
}

bool ptc_daily_summary_format_line(char *out, size_t size, const PtcDailySummaryRecord *r)
{
    int written;
    PtcDailySummaryRecord checked;
    char direct[240] = "";
    if (!out || !r) return false;
    if (r->direct_source) {
        written = snprintf(direct, sizeof(direct),
            ",\"consumption_source\":\"pctl_1952\",\"scope\":\"console_usable_time\",\"coverage\":\"partial\","
            "\"total_ns\":\"%llu\",\"observed_at\":%lld,\"latest_available\":%s",
            (unsigned long long)r->total_ns, (long long)r->observed_at, r->latest_available ? "true" : "false");
        if (written < 0 || (size_t)written >= sizeof(direct)) return false;
    }
    written = snprintf(out, size,
        "{\"version\":%u,\"day_index\":%u,\"captured_at\":%lld,\"rule_source\":\"%s\","
        "\"limited\":%s,\"configured_minutes\":%u,\"remaining_available\":%s,"
        "\"remaining_minutes\":%u,\"consumed_available\":%s,\"consumed_minutes\":%u,\"granted_minutes\":%u%s}",
        r->direct_source ? 2u : 1u, (unsigned)r->day_index, (long long)r->captured_at, r->rule_source,
        r->limited ? "true" : "false", (unsigned)r->configured_minutes,
        r->remaining_available ? "true" : "false", (unsigned)r->remaining_minutes,
        r->consumed_available ? "true" : "false", (unsigned)r->consumed_minutes,
        (unsigned)r->granted_minutes, direct);
    return written > 0 && (size_t)written < size && ptc_daily_summary_parse_line(out, &checked);
}

void ptc_daily_summary_merge(PtcDailySummaryRecord *stored, const PtcDailySummaryRecord *sample)
{
    if (stored->day_index != sample->day_index || stored->direct_source != sample->direct_source) return;
    if (!stored->direct_source) {
        if (sample->captured_at >= stored->captured_at && (sample->consumed_available || !stored->consumed_available)) *stored = *sample;
        return;
    }
    if (sample->observed_at < stored->observed_at) {
        /* Out-of-order duplicates can contain an older reliable lower bound
           followed by a failed or decreasing observation. Keep both facts. */
        if (sample->consumed_available && (!stored->consumed_available || sample->total_ns > stored->total_ns)) {
            int64_t observed = stored->observed_at;
            *stored = *sample;
            stored->observed_at = observed;
            stored->latest_available = false;
        }
        return;
    }
    if (sample->consumed_available && (!stored->consumed_available ||
        (sample->captured_at >= stored->captured_at && sample->total_ns >= stored->total_ns))) *stored = *sample;
    else {
        stored->observed_at = sample->observed_at;
        stored->latest_available = false;
    }
}

void ptc_daily_summary_aggregate(const PtcDailySummaryRecord *records, size_t count,
    uint16_t today, PtcDailySummaryAggregate *out)
{
    size_t i, j;
    memset(out, 0, sizeof(*out));
    if (!records) return;
    for (i = 0; i < count; ++i) {
        PtcDailySummaryRecord day = records[i];
        uint16_t age;
        if (day.day_index > today || today - day.day_index >= 30) continue;
        for (j = 0; j < i; ++j)
            if (records[j].day_index == day.day_index && records[j].direct_source == day.direct_source) break;
        if (j < i) continue;
        for (j = i + 1; j < count; ++j) ptc_daily_summary_merge(&day, &records[j]);
        age = (uint16_t)(today - day.day_index);
        if (day.direct_source) {
            out->direct_present = true;
            if (day.observed_at >= out->last_observed_at) {
                out->last_observed_at = day.observed_at; out->latest_available = day.latest_available;
            }
            if (!day.consumed_available) continue;
            if (day.captured_at > out->last_captured_at) out->last_captured_at = day.captured_at;
            ++out->direct_days_30; out->direct_ns_30 += day.total_ns;
            if (age < 7) { ++out->direct_days_7; out->direct_ns_7 += day.total_ns; }
        } else if (day.consumed_available) {
            ++out->known_days_30; out->consumed_minutes_30 += day.consumed_minutes;
            if (age < 7) { ++out->known_days_7; out->consumed_minutes_7 += day.consumed_minutes; }
        }
    }
}
