#include "sysmodule_internal.h"

void ptc_sysmodule_note_reason(PtcSysmodule *sysmodule, const PtcRules *rules,
    const PtcRuntimeState *state, const PtcPctlStatus *status, PtcClockSnapshot now)
{
    PtcReasonObservation *out = &sysmodule->reason_observation;
    PtcBedtimeEvaluation bed = ptc_bedtime_evaluate(rules, now.day_index,
        ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
    PtcClockSnapshot after = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    out->reliable = false;
    /* No extra PCTL session/read: these are the successful pass's own fields.
       Unknown official unlock/restriction state must not imply daily exhaustion. */
    if (after.day_index != now.day_index || after.unix_seconds < now.unix_seconds ||
        after.unix_seconds - now.unix_seconds >= 2 || state->apply_pending_confirmation ||
        sysmodule->quiescing || recovery_path_exists(sysmodule) ||
        !status->restriction_enabled_available || !status->restriction_enabled ||
        !status->temporary_unlocked_available || !status->restricted_now_available) return;
    out->day = now.day_index;
    out->captured_at = now.unix_seconds;
    out->restricted = status->restricted_now && !status->temporary_unlocked;
    out->reason = PTC_REASON_NONE;
    out->instance = 0;
    if (out->restricted) {
        /* Match the existing status projection: daily is masked during rest
           and bedtime; show one major reason, never invent a concurrent list. */
        if (bed.active && state->bedtime_enforced &&
            state->bedtime_window_instance_id == bed.window_instance_id &&
            state->bedtime_skipped_instance_id != bed.window_instance_id) {
            out->reason = PTC_REASON_BEDTIME;
            out->instance = bed.window_instance_id;
        } else if (state->eye_care_resting && rules->eye_care.enabled &&
            state->eye_care_rest_deadline > now.unix_seconds) {
            out->reason = PTC_REASON_EYE;
            out->instance = state->eye_care_break_id;
        } else if (state->dock_enforced) {
            out->reason = PTC_REASON_DOCK;
            out->instance = (uint64_t)now.day_index + 1;
        } else if (status->limited_today && ptc_pctl_remaining_exhausted(status)) {
            out->reason = PTC_REASON_DAILY;
            out->instance = (uint64_t)now.day_index + 1;
        } else return;
    }
    out->reliable = true;
}
