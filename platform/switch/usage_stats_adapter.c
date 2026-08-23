#include "usage_stats_adapter.h"

#include <string.h>

static PtcUsageStatsAvailability unavailable_read_day(
    PtcUsageStats *stats, uint16_t day_index, PtcUsageStatsSnapshot *out)
{
    (void)stats;
    if (out) {
        memset(out, 0, sizeof(*out));
        out->local_device_scope = true;
        out->day_index = day_index;
    }
    return PTC_USAGE_STATS_UNAVAILABLE;
}

static const PtcUsageStatsVTable UNAVAILABLE_VTABLE = {
    unavailable_read_day,
};

void ptc_switch_usage_stats_init(PtcSwitchUsageStats *adapter)
{
    if (!adapter) return;
    memset(adapter, 0, sizeof(*adapter));
    adapter->stats.vtable = &UNAVAILABLE_VTABLE;
    adapter->stats.ctx = adapter;
}

PtcUsageStats *ptc_switch_usage_stats_as_stats(PtcSwitchUsageStats *adapter)
{
    return adapter ? &adapter->stats : NULL;
}
