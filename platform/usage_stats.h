#ifndef PTC_PLATFORM_USAGE_STATS_H
#define PTC_PLATFORM_USAGE_STATS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PTC_USAGE_STATS_MAX_TITLES 3u

typedef struct PtcUsageStats PtcUsageStats;

typedef enum {
    PTC_USAGE_STATS_UNAVAILABLE = 0,
    PTC_USAGE_STATS_AVAILABLE = 1
} PtcUsageStatsAvailability;

typedef struct {
    uint64_t application_id;
    uint32_t minutes;
    char display_name[96];
} PtcUsageStatsTitle;

typedef struct {
    /* Statistics describe this console, never a child or Nintendo account. */
    bool local_device_scope;
    uint16_t day_index;
    size_t title_count;
    PtcUsageStatsTitle titles[PTC_USAGE_STATS_MAX_TITLES];
} PtcUsageStatsSnapshot;

typedef struct {
    PtcUsageStatsAvailability (*read_day)(
        PtcUsageStats *stats, uint16_t day_index, PtcUsageStatsSnapshot *out);
} PtcUsageStatsVTable;

struct PtcUsageStats {
    const PtcUsageStatsVTable *vtable;
    void *ctx;
};

#endif
