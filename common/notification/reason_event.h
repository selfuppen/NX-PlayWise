#ifndef PTC_REASON_EVENT_H
#define PTC_REASON_EVENT_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PTC_REASON_NONE, PTC_REASON_DAILY, PTC_REASON_BEDTIME,
    PTC_REASON_EYE, PTC_REASON_DOCK
} PtcRestrictionReason;

typedef enum {
    PTC_REASON_TIME_UNKNOWN, PTC_REASON_TIME_DAILY_ZERO,
    PTC_REASON_TIME_REMAINING, PTC_REASON_TIME_DOCK_ZERO,
    PTC_REASON_TIME_CONNECT_TV
} PtcReasonTime;

/* Produced only after the original policy pass has observed and committed. */
typedef struct {
    uint64_t revision;
    bool reliable;
    bool restricted;
    uint16_t day;
    int64_t captured_at;
    PtcRestrictionReason reason;
    uint64_t instance;
    bool local_clock_available;
    uint32_t local_second_of_day;
    PtcReasonTime time_kind;
    uint32_t remaining_seconds; /* same successful observation, never a UI cache */
} PtcReasonObservation;

typedef struct {
    char release[128];
    char boot[24];
    PtcReasonObservation latest;
    PtcReasonObservation handled;
    uint64_t observed_ms;
    uint64_t last_ms;
    uint64_t next_publish_ms;
    uint64_t next_repeat_ms;
    bool repeat; /* this attempt is a brief countdown reminder, never a detail offer */
    int64_t last_wall;
    bool handled_valid;
    bool dirty;
    bool enabled;
    bool latest_accepted; /* latest was accepted while reliable and fresh */
} PtcReasonEvent;

void ptc_reason_event_init(PtcReasonEvent *event, const char *release, const char *boot);
/* Identity, time and suppression are supplied by the caller, never read here. */
bool ptc_reason_event_update(PtcReasonEvent *event, const char *release, const char *boot,
    const PtcReasonObservation *sample, uint64_t now_ms, int64_t wall,
    bool enabled, bool suppressed);
void ptc_reason_event_attempted(PtcReasonEvent *event, uint64_t now_ms);
#endif
