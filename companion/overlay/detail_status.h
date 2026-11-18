#ifndef PTC_OVERLAY_DETAIL_STATUS_H
#define PTC_OVERLAY_DETAIL_STATUS_H
#include "../../platform/reason_detail.h"
#include "../result_summary.h"
#include <string.h>

/* Same primary-reason priority as the committed backend observation. */
static inline PtcRestrictionReason ptc_overlay_detail_reason(const PtcCompanionResultSummary *s)
{
    /* access_recovery_required also marks the normal restricted child page.
       Sensitive parent/PIN pages are excluded by the GUI's frame-level gate. */
    if (!s->valid || !s->ok || s->restricted_now != 1 || s->disable_flag_present ||
        !s->temporary_unlocked_available || s->temporary_unlocked) return PTC_REASON_NONE;
    if (s->bedtime_available && s->bedtime_active && !s->bedtime_skipped)
        return PTC_REASON_BEDTIME;
    if (s->eye_care_available && !strcmp(s->eye_care_phase, "resting"))
        return PTC_REASON_EYE;
    if (s->dock_available && s->dock_restriction_active)
        return PTC_REASON_DOCK;
    return s->daily_restriction_active ? PTC_REASON_DAILY : PTC_REASON_NONE;
}

static inline bool ptc_overlay_detail_matches(const PtcReasonDetail *d, const PtcCompanionResultSummary *s)
{
    PtcRestrictionReason reason = ptc_overlay_detail_reason(s);
    if (reason == PTC_REASON_NONE || s->day_index != d->day || d->reason != reason) return false;
    if (reason == PTC_REASON_BEDTIME) return d->instance == s->bedtime_window_instance_id;
    if (reason == PTC_REASON_EYE) return d->instance == s->eye_care_break_id;
    return d->instance == (uint64_t)d->day + 1;
}
#endif
