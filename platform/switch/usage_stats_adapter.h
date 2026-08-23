#ifndef PTC_SWITCH_USAGE_STATS_ADAPTER_H
#define PTC_SWITCH_USAGE_STATS_ADAPTER_H

#include "../usage_stats.h"

typedef struct {
    PtcUsageStats stats;
} PtcSwitchUsageStats;

/* Release builds deliberately remain unavailable until the read-only pdm:qry
   Device Lab matrix has established reliable event semantics. */
void ptc_switch_usage_stats_init(PtcSwitchUsageStats *adapter);
PtcUsageStats *ptc_switch_usage_stats_as_stats(PtcSwitchUsageStats *adapter);

#endif
