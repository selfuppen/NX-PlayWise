#include "fake_time.h"

static PtcClockSnapshot fake_now(PtcTimeProvider *time_provider)
{
    PtcFakeTime *fake = (PtcFakeTime *)time_provider->ctx;
    return fake->snapshot;
}

static void fake_sleep_ms(PtcTimeProvider *time_provider, uint32_t milliseconds)
{
    PtcFakeTime *fake = (PtcFakeTime *)time_provider->ctx;
    fake->slept_ms += milliseconds;
    fake->snapshot.unix_seconds += milliseconds / 1000U;
}

static const PtcTimeProviderVTable FAKE_TIME_VTABLE = {
    fake_now,
    fake_sleep_ms,
};

void ptc_fake_time_init(PtcFakeTime *fake, int64_t unix_seconds, uint16_t day_index, uint16_t minute_of_day)
{
    fake->provider.vtable = &FAKE_TIME_VTABLE;
    fake->provider.ctx = fake;
    fake->snapshot.unix_seconds = unix_seconds;
    fake->snapshot.day_index = day_index;
    fake->snapshot.minute_of_day = minute_of_day;
    fake->slept_ms = 0;
}

PtcTimeProvider *ptc_fake_time_as_provider(PtcFakeTime *fake)
{
    return &fake->provider;
}
