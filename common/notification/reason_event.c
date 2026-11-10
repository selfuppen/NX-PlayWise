#include "reason_event.h"
#include <stdio.h>
#include <string.h>

static bool same(const PtcReasonObservation *a, const PtcReasonObservation *b)
{
    return a->restricted == b->restricted && a->day == b->day &&
        a->reason == b->reason && a->instance == b->instance;
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
    bool clock_ok = ms >= e->last_ms && (!e->last_wall || wall >= e->last_wall);
    bool identity_ok = !strcmp(release, e->release) && !strcmp(boot, e->boot);
    bool fresh = s->reliable && s->captured_at > 0 && wall >= s->captured_at &&
        wall - s->captured_at < 2;
    bool became_enabled = enabled && !e->enabled;
    if (became_enabled) e->handled_valid = false;
    if (s->revision != e->latest.revision || became_enabled) {
        e->dirty = !e->handled_valid || !same(s, &e->handled);
        e->latest = *s;
        e->observed_ms = ms;
    }
    e->last_ms = ms;
    e->last_wall = wall;
    e->enabled = enabled;
    if (!clock_ok || !identity_ok || !fresh) {
        e->dirty = false;
        return false;
    }
    if (!enabled || suppressed || !s->restricted) {
        e->handled = *s;
        e->handled_valid = true;
        e->dirty = false;
        return false;
    }
    return e->dirty && s->reason > PTC_REASON_NONE && s->reason <= PTC_REASON_DOCK &&
        s->instance && ms >= e->observed_ms && ms - e->observed_ms < 2000 &&
        ms >= e->next_publish_ms;
}

void ptc_reason_event_attempted(PtcReasonEvent *e, uint64_t ms)
{
    e->handled = e->latest;
    e->handled_valid = true;
    e->dirty = false;
    e->next_publish_ms = ms > UINT64_MAX - 10000 ? UINT64_MAX : ms + 10000;
}
