#ifndef PTC_PLATFORM_TIME_PROVIDER_H
#define PTC_PLATFORM_TIME_PROVIDER_H

#include <stdint.h>

typedef struct {
    int64_t unix_seconds;
    uint16_t day_index;
    uint16_t minute_of_day;
} PtcClockSnapshot;

typedef struct PtcTimeProvider PtcTimeProvider;

typedef struct {
    PtcClockSnapshot (*now)(PtcTimeProvider *time_provider);
} PtcTimeProviderVTable;

struct PtcTimeProvider {
    const PtcTimeProviderVTable *vtable;
    void *ctx;
};

#endif
