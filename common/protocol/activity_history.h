#ifndef PTC_ACTIVITY_HISTORY_H
#define PTC_ACTIVITY_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PTC_ACTIVITY_HISTORY_MAX_RECORDS 200u
#define PTC_ACTIVITY_HISTORY_LINE_SIZE 256u
#define PTC_ACTIVITY_HISTORY_FILE_SIZE \
    (PTC_ACTIVITY_HISTORY_MAX_RECORDS * PTC_ACTIVITY_HISTORY_LINE_SIZE + 1u)

typedef struct {
    int64_t occurred_at;
    uint16_t day_index;
    char action[40];
    uint16_t minutes;
    uint16_t effective_minutes;
} PtcActivityHistoryRecord;

bool ptc_activity_history_parse_line(const char *line, PtcActivityHistoryRecord *out);
bool ptc_activity_history_format_line(
    char *out, size_t out_size, const PtcActivityHistoryRecord *record);

#endif
