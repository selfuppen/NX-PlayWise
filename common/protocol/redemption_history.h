#ifndef PTC_REDEMPTION_HISTORY_H
#define PTC_REDEMPTION_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PTC_REDEMPTION_HISTORY_MAX_RECORDS 100u
#define PTC_REDEMPTION_HISTORY_LINE_SIZE 256u
#define PTC_REDEMPTION_HISTORY_FILE_SIZE \
    (PTC_REDEMPTION_HISTORY_MAX_RECORDS * PTC_REDEMPTION_HISTORY_LINE_SIZE + 1u)

typedef struct {
    int64_t redeemed_at;
    uint16_t day_index;
    unsigned int token_version;
    uint16_t grant_minutes;
    uint16_t effective_add_minutes;
    bool remaining_after_available;
    int64_t remaining_after_minutes;
} PtcRedemptionHistoryRecord;

bool ptc_redemption_history_parse_line(const char *line, PtcRedemptionHistoryRecord *out);
bool ptc_redemption_history_format_line(
    char *out,
    size_t out_size,
    const PtcRedemptionHistoryRecord *record);

#endif
