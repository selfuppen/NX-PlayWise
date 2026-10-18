#ifndef PLAYWISE_POLICY_STATUS_H
#define PLAYWISE_POLICY_STATUS_H

#include "result_summary.h"
#include "ui_language.h"

typedef enum {
    PTC_POLICY_UNKNOWN, PTC_POLICY_OFF, PTC_POLICY_NONE_TODAY,
    PTC_POLICY_SKIPPED, PTC_POLICY_PENDING, PTC_POLICY_ACTIVE,
    PTC_POLICY_WAITING, PTC_POLICY_AVAILABLE, PTC_POLICY_TV_ONLY,
    PTC_POLICY_EXHAUSTED, PTC_POLICY_WAIVED
} PtcPolicyDisplayState;

typedef struct {
    PtcPolicyDisplayState state;
    int minutes; /* -1 means unavailable; use minutes, never a wall-clock TV estimate. */
    int time_minute;
    PtcUiTextId badge;
    char compact[160];
    char detail[256];
} PtcPolicyDisplay;

typedef struct {
    PtcPolicyDisplay bedtime;
    PtcPolicyDisplay dock;
    PtcUiTextId mode;
    char dock_rule[160];
} PtcPolicyStatus;

/* Freshness gates usage counters; known bedtime windows keep their countdown. */
void ptc_policy_status_project(const PtcCompanionResultSummary *summary,
    bool fresh, int local_day_index, int second_of_day, PtcPolicyStatus *out);
bool ptc_policy_local_clock(long long now, int *day_index, int *second_of_day);
/* Overlay has no initialized libc wall clock. Request age is a conservative bound.
   Same-day clock projection remains usable after status freshness expires. */
bool ptc_policy_snapshot_clock(const PtcCompanionResultSummary *summary,
    uint64_t request_elapsed_seconds, int *day_index, int *second_of_day);

#endif
