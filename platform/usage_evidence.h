#ifndef PTC_USAGE_EVIDENCE_H
#define PTC_USAGE_EVIDENCE_H

#include <stdbool.h>
#include <stdint.h>

/* Raw read group, separate from cached UI observations and policy state. */
typedef struct {
    uint64_t begin_ns, end_ns;
    int64_t unix_before, unix_after;
    uint16_t day_before, day_after;
    bool clock_before_valid, clock_after_valid;
    uint64_t start_timer_calls;
    uint32_t session_result, timer_result, remaining_result, restricted_result;
    uint32_t spent_result, settings_result, unlocked_result;
    bool timer_enabled, restricted, temporarily_unlocked;
    int64_t remaining_ns, spent_ns;
    uint8_t settings[0x44];
} PtcUsageEvidenceSample;

typedef bool (*PtcUsageEvidenceReader)(void *ctx, PtcUsageEvidenceSample *out);

#endif
