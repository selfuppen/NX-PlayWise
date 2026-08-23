#ifndef PTC_DAILY_SUMMARY_H
#define PTC_DAILY_SUMMARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PTC_DAILY_SUMMARY_MAX_RECORDS 30u
#define PTC_DAILY_SUMMARY_LINE_SIZE 256u
#define PTC_DAILY_SUMMARY_FILE_SIZE \
    (PTC_DAILY_SUMMARY_MAX_RECORDS * PTC_DAILY_SUMMARY_LINE_SIZE + 1u)

typedef struct {
    uint16_t day_index;
    int64_t captured_at;
    char rule_source[32];
    bool limited;
    uint16_t configured_minutes;
    bool remaining_available;
    uint16_t remaining_minutes;
    bool consumed_available;
    uint16_t consumed_minutes;
    uint16_t granted_minutes;
} PtcDailySummaryRecord;

typedef struct {
    uint16_t known_days_7;
    uint32_t consumed_minutes_7;
    uint16_t known_days_30;
    uint32_t consumed_minutes_30;
} PtcDailySummaryAggregate;

bool ptc_daily_summary_parse_line(const char *line, PtcDailySummaryRecord *out);
bool ptc_daily_summary_format_line(
    char *out, size_t out_size, const PtcDailySummaryRecord *record);
void ptc_daily_summary_aggregate(const PtcDailySummaryRecord *records, size_t count,
    uint16_t today_day_index, PtcDailySummaryAggregate *out);

#endif
