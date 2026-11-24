#include "time_provider.h"

#include <string.h>
#include <switch.h>

#include "time_client.h"
#include "../../common/time/ptc_time.h"

static PtcClockSnapshot switch_now(PtcTimeProvider *provider)
{
    PtcClockSnapshot snapshot;
    PtcSwitchLocalTime local;
    (void)provider;

    memset(&snapshot, 0, sizeof(snapshot));
    if (!ptc_switch_time_read_local(&local) ||
        !ptc_day_index_from_date(local.year, local.month, local.day, &snapshot.day_index)) {
        return snapshot;
    }
    snapshot.unix_seconds = (int64_t)local.unix_seconds;
    snapshot.minute_of_day = (uint16_t)((uint16_t)local.hour * 60u + local.minute);
    snapshot.second_of_minute = local.second;
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
