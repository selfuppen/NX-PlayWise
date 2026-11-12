#ifndef PTC_OVERLAY_DETAIL_STATUS_H
#define PTC_OVERLAY_DETAIL_STATUS_H
#include "../../platform/reason_detail.h"
#include "../result_summary.h"
#include <string.h>

/* Same primary-reason priority as the committed backend observation. A fresh
   status request must also have completed after accepting this detail offer. */
static inline bool ptc_overlay_detail_matches(const PtcReasonDetail *d, const PtcCompanionResultSummary *s)
{
    /* access_recovery_required also marks the normal restricted child page.
       Sensitive parent/PIN pages are excluded by the GUI's frame-level gate. */
    if (!s->valid || !s->ok || s->day_index != d->day || s->restricted_now != 1 ||
        s->disable_flag_present ||
        !s->temporary_unlocked_available || s->temporary_unlocked) return false;
    if (s->bedtime_available && s->bedtime_active && !s->bedtime_skipped)
        return d->reason == PTC_REASON_BEDTIME && d->instance == s->bedtime_window_instance_id;
    if (s->eye_care_available && !strcmp(s->eye_care_phase, "resting"))
        return d->reason == PTC_REASON_EYE && d->instance == s->eye_care_break_id;
    if (s->dock_available && s->dock_restriction_active)
        return d->reason == PTC_REASON_DOCK && d->instance == (uint64_t)d->day + 1;
    return s->daily_restriction_active && d->reason == PTC_REASON_DAILY && d->instance == (uint64_t)d->day + 1;
}
#endif
