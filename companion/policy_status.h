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

/* Callers supply freshness and local civil time, including in deterministic tests. */
void ptc_policy_status_project(const PtcCompanionResultSummary *summary,
    bool fresh, int local_day_index, int second_of_day, PtcPolicyStatus *out);
bool ptc_policy_local_clock(long long now, int *day_index, int *second_of_day);

#endif
