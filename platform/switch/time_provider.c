#include "time_provider.h"

#include <string.h>
#include <switch.h>

#include "../../common/time/ptc_time.h"

static PtcClockSnapshot switch_now(PtcTimeProvider *provider)
{
    PtcClockSnapshot snapshot;
    TimeCalendarTime calendar;
    TimeCalendarAdditionalInfo additional;
    u64 timestamp = 0;
    Result rc;
    (void)provider;

    memset(&snapshot, 0, sizeof(snapshot));
    rc = timeGetCurrentTime(TimeType_Default, &timestamp);
    if (R_FAILED(rc)) {
        return snapshot;
    }
    snapshot.unix_seconds = (int64_t)timestamp;
    memset(&calendar, 0, sizeof(calendar));
    memset(&additional, 0, sizeof(additional));
    rc = timeToCalendarTimeWithMyRule(timestamp, &calendar, &additional);
    if (R_FAILED(rc) || !ptc_day_index_from_date(
            calendar.year, calendar.month, calendar.day, &snapshot.day_index)) {
        memset(&snapshot, 0, sizeof(snapshot));
        return snapshot;
    }
    snapshot.minute_of_day = (uint16_t)((uint16_t)calendar.hour * 60u + calendar.minute);
    return snapshot;
}

static void switch_sleep_ms(PtcTimeProvider *provider, uint32_t milliseconds)
{
    (void)provider;
    svcSleepThread((int64_t)milliseconds * 1000000LL);
}

static const PtcTimeProviderVTable SWITCH_TIME_PROVIDER_VTABLE = {
    switch_now,
    switch_sleep_ms,
};

void ptc_switch_time_provider_init(PtcSwitchTimeProvider *provider)
{
    provider->provider.vtable = &SWITCH_TIME_PROVIDER_VTABLE;
    provider->provider.ctx = provider;
}

PtcTimeProvider *ptc_switch_time_provider_as_provider(PtcSwitchTimeProvider *provider)
{
    return &provider->provider;
}
