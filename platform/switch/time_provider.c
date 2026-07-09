#include "time_provider.h"

#include <string.h>
#include <switch.h>

#include "../../common/time/ptc_time.h"

static PtcClockSnapshot switch_now(PtcTimeProvider *provider)
{
    PtcClockSnapshot snapshot;
    TimeCalendarTime calendar;
    TimeCalendarAdditionalInfo info;
    u64 timestamp = 0;
    Result rc;
    (void)provider;

    memset(&snapshot, 0, sizeof(snapshot));
    rc = timeGetCurrentTime(TimeType_Default, &timestamp);
    if (R_FAILED(rc)) {
        return snapshot;
    }
    snapshot.unix_seconds = (int64_t)timestamp;
    snapshot.day_index = ptc_day_index_from_unix((int64_t)timestamp);
    snapshot.minute_of_day = (uint16_t)(((timestamp % 86400) / 60) % 1440);
    if (R_SUCCEEDED(timeToCalendarTimeWithMyRule(timestamp, &calendar, &info))) {
        snapshot.minute_of_day = (uint16_t)(calendar.hour * 60 + calendar.minute);
    }
    return snapshot;
}

static const PtcTimeProviderVTable SWITCH_TIME_PROVIDER_VTABLE = {
    switch_now,
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
