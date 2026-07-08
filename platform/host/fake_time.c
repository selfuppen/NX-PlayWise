#include "fake_time.h"

static PtcClockSnapshot fake_now(PtcTimeProvider *time_provider)
{
    PtcFakeTime *fake = (PtcFakeTime *)time_provider->ctx;
    return fake->snapshot;
}

static const PtcTimeProviderVTable FAKE_TIME_VTABLE = {
    fake_now,
};

void ptc_fake_time_init(PtcFakeTime *fake, int64_t unix_seconds, uint16_t day_index, uint16_t minute_of_day)
{
    fake->provider.vtable = &FAKE_TIME_VTABLE;
    fake->provider.ctx = fake;
    fake->snapshot.unix_seconds = unix_seconds;
    fake->snapshot.day_index = day_index;
    fake->snapshot.minute_of_day = minute_of_day;
}

PtcTimeProvider *ptc_fake_time_as_provider(PtcFakeTime *fake)
{
    return &fake->provider;
}
