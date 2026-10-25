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

/* A countdown the child can see running down: the eye-care rest timer or the
   bedtime window end. Expiry is observed locally; the backend still decides
   whether the restriction actually lifted, so the caller re-reads status once. */
typedef enum {
    PTC_POLICY_COUNTDOWN_NONE = 0,
    PTC_POLICY_COUNTDOWN_EYE_REST = 1,
    PTC_POLICY_COUNTDOWN_BEDTIME_END = 2
} PtcPolicyCountdownKind;

typedef struct {
    PtcPolicyCountdownKind kind;
    int seconds;               /* seconds until the visible countdown reaches zero */
    uint64_t instance_id;      /* break/window instance this countdown belongs to */
} PtcPolicyCountdown;

/* Only countdowns already visible to the child are tracked. An unknown,
   skipped, disabled or expired window reports NONE rather than a guess; a
   recognized instance that passed reports seconds == 0, never a negative.
   Eye-care rest ages from the status receipt, while the bedtime clock ages from
   the request start that produced `local_second_of_day`, so each takes its own
   elapsed value instead of guessing one shared origin. A negative age only
   disables the branch that needs it. */
void ptc_policy_status_countdown(const PtcCompanionResultSummary *summary,
    bool snapshot_usable, int64_t rest_age_seconds, int64_t clock_age_seconds,
    PtcPolicyCountdown *out);

#endif
