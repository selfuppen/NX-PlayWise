#include "reason_event.h"
#include <stdio.h>
#include <string.h>

static bool same(const PtcReasonObservation *a, const PtcReasonObservation *b)
{
    return a->restricted == b->restricted && a->day == b->day &&
        a->reason == b->reason && a->instance == b->instance;
}

static uint64_t repeat_deadline(uint64_t ms)
{
    return ms > UINT64_MAX - 60000 ? UINT64_MAX : ms + 60000;
}

void ptc_reason_event_init(PtcReasonEvent *e, const char *release, const char *boot)
{
    memset(e, 0, sizeof(*e));
    snprintf(e->release, sizeof(e->release), "%s", release);
    snprintf(e->boot, sizeof(e->boot), "%s", boot);
}

bool ptc_reason_event_update(PtcReasonEvent *e, const char *release, const char *boot,
    const PtcReasonObservation *s, uint64_t ms, int64_t wall, bool enabled, bool suppressed)
{
    e->repeat = false;
    bool clock_ok = ms >= e->last_ms && (!e->last_wall || wall >= e->last_wall);
    bool identity_ok = !strcmp(release, e->release) && !strcmp(boot, e->boot);
    bool fresh = s->reliable && s->captured_at > 0 && wall >= s->captured_at &&
        wall - s->captured_at < 2;
    /* A one-second-old observation can already belong to yesterday. */
    if (s->local_clock_available && (s->local_second_of_day >= 86400 ||
        (wall >= s->captured_at && wall - s->captured_at >= 86400 - s->local_second_of_day)))
        fresh = false;
    bool became_enabled = enabled && !e->enabled;
    if (became_enabled) e->handled_valid = false;
    e->last_ms = ms;
    e->last_wall = wall;
    e->enabled = enabled;
    if (!enabled || suppressed) {
        e->next_repeat_ms = repeat_deadline(ms);
        e->dirty = false;
        /* Details/disable can begin after the last policy sample aged. Consume
           the known instance now so leaving details cannot replay its intent. */
        if (e->latest_accepted) {
            e->handled = e->latest;
            e->handled_valid = true;
        }
    }
    if (!clock_ok || !identity_ok || !s->reliable || s->captured_at <= 0 ||
        wall < s->captured_at || (s->restricted &&
        (s->reason <= PTC_REASON_NONE || s->reason > PTC_REASON_DOCK || !s->instance))) {
        e->next_repeat_ms = repeat_deadline(ms);
        e->dirty = false;
        e->latest_accepted = false;
        return false;
    }
    /* An aged sample cannot publish or replace history. Retain only the hint
       that a handoff occurred; publication still needs a new fresh observation. */
    if (!fresh) return false;
    bool new_observation = s->revision != e->latest.revision || became_enabled || !e->latest_accepted;
    if (new_observation) {
        bool changed = e->latest_accepted && !same(s, &e->latest);
        /* A -> B -> A remains a handoff when B was rate-limited. Comparing only
           with the last attempt would silently discard the fresh return to A. */
        e->dirty = e->dirty || !e->handled_valid || !same(s, &e->handled) || changed;
        e->latest = *s;
        e->latest_accepted = true;
        e->observed_ms = ms;
    }
    if (!enabled || suppressed || !s->restricted) {
        e->handled = *s;
        e->handled_valid = true;
        e->dirty = false;
        return false;
    }
    if (e->dirty)
        return ms >= e->observed_ms && ms - e->observed_ms < 2000 && ms >= e->next_publish_ms;
    /* Repeats consume only a new policy observation, not an aged cached sample.
       Do not mark dirty while waiting: this minute timer must not speed up idle polling. */
    uint32_t max_seconds = s->reason == PTC_REASON_EYE ? 3600 : 86400;
    e->repeat = new_observation && e->handled_valid && same(s, &e->handled) &&
        (s->reason == PTC_REASON_EYE || s->reason == PTC_REASON_BEDTIME) &&
        s->time_kind == PTC_REASON_TIME_REMAINING &&
        s->remaining_seconds > (uint32_t)(wall - s->captured_at) &&
        s->remaining_seconds <= max_seconds && e->next_repeat_ms != 0 &&
        ms >= e->next_repeat_ms && ms >= e->next_publish_ms;
    return e->repeat;
}

void ptc_reason_event_attempted(PtcReasonEvent *e, uint64_t ms)
{
    e->handled = e->latest;
    e->handled_valid = true;
    e->dirty = false;
    e->next_publish_ms = ms > UINT64_MAX - 10000 ? UINT64_MAX : ms + 10000;
    e->next_repeat_ms = repeat_deadline(ms);
}
