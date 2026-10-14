#ifndef PTC_USAGE_COUNTER_H
#define PTC_USAGE_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

#define PTC_USAGE_SOURCE_PCTL_1952 2u
#define PTC_USAGE_MINUTE_NS UINT64_C(60000000000)

typedef struct {
    uint16_t source;
    bool known;
    uint16_t day_index;
    uint64_t total_ns;
    int64_t captured_at;
} PtcUsageCounter;

typedef enum {
    PTC_USAGE_UNKNOWN = 0,
    PTC_USAGE_BASELINE = 1,
    PTC_USAGE_DELTA = 2,
    PTC_USAGE_NEXT_DAY = 3
} PtcUsageStep;

/* Pure cumulative-counter contract. Unknown samples retain the trusted
   baseline; source changes establish a new epoch without inventing a delta.
   Only an adjacent forward date with a bounded capture gap is a normal rollover. */
static inline PtcUsageStep ptc_usage_counter_step(PtcUsageCounter *baseline,
    const PtcUsageCounter *sample, uint64_t *delta)
{
    *delta = 0;
    if (!sample->known || sample->source != PTC_USAGE_SOURCE_PCTL_1952 ||
        sample->total_ns > INT64_MAX || sample->captured_at <= 0) return PTC_USAGE_UNKNOWN;
    if (!baseline->known || baseline->source != sample->source) {
        *baseline = *sample;
        return PTC_USAGE_BASELINE;
    }
    if (sample->captured_at < baseline->captured_at) return PTC_USAGE_UNKNOWN;
    if (sample->day_index == baseline->day_index) {
        if (sample->total_ns < baseline->total_ns) return PTC_USAGE_UNKNOWN;
        *delta = sample->total_ns - baseline->total_ns;
        *baseline = *sample;
        return PTC_USAGE_DELTA;
    }
    if ((uint32_t)baseline->day_index + 1 != sample->day_index ||
        sample->captured_at == baseline->captured_at ||
        sample->captured_at - baseline->captured_at > INT64_C(129600)) return PTC_USAGE_UNKNOWN;
    *delta = sample->total_ns;
    *baseline = *sample;
    return PTC_USAGE_NEXT_DAY;
}

#endif
