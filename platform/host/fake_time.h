#ifndef PTC_HOST_FAKE_TIME_H
#define PTC_HOST_FAKE_TIME_H

#include "../time_provider.h"

typedef struct {
    PtcTimeProvider provider;
    PtcClockSnapshot snapshot;
} PtcFakeTime;

void ptc_fake_time_init(PtcFakeTime *fake, int64_t unix_seconds, uint16_t day_index, uint16_t minute_of_day);
PtcTimeProvider *ptc_fake_time_as_provider(PtcFakeTime *fake);

#endif
