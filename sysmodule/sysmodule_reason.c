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
    out->time_kind = PTC_REASON_TIME_UNKNOWN;
    out->remaining_seconds = 0;
    out->local_clock_available = now.minute_of_day < 1440 && now.second_of_minute < 60;
    out->local_second_of_day = (uint32_t)now.minute_of_day * 60 + now.second_of_minute;
    if (out->restricted) {
        /* Match the existing status projection: daily is masked during rest
           and bedtime; show one major reason, never invent a concurrent list. */
        if (bed.active && state->bedtime_enforced &&
            state->bedtime_window_instance_id == bed.window_instance_id &&
            state->bedtime_skipped_instance_id != bed.window_instance_id) {
            out->reason = PTC_REASON_BEDTIME;
            out->instance = bed.window_instance_id;
            /* The evaluated instance owns the end date, including after midnight.
               Do not infer an end from today's independently resolved plan. */
            int64_t end_day = (int64_t)bed.start_day_index + (bed.end_minute <= bed.start_minute);
            int64_t seconds = (end_day - now.day_index) * 86400 +
                (int64_t)bed.end_minute * 60 - out->local_second_of_day;
            if (out->local_clock_available && seconds > 0 && seconds <= 86400) {
                out->time_kind = PTC_REASON_TIME_REMAINING;
                out->remaining_seconds = (uint32_t)seconds;
            }
        } else if (state->eye_care_resting && rules->eye_care.enabled &&
            state->eye_care_rest_deadline > now.unix_seconds) {
            out->reason = PTC_REASON_EYE;
            out->instance = state->eye_care_break_id;
            int64_t seconds = state->eye_care_rest_deadline - now.unix_seconds;
            if (seconds > 0 && seconds <= 3600) {
                out->time_kind = PTC_REASON_TIME_REMAINING;
                out->remaining_seconds = (uint32_t)seconds;
            }
        } else if (state->dock_enforced) {
            out->reason = PTC_REASON_DOCK;
            out->instance = (uint64_t)now.day_index + 1;
            out->time_kind = PTC_REASON_TIME_CONNECT_TV;
            if (rules->dock_policy.undocked_limit_enabled &&
                rules->dock_policy.undocked_daily_minutes > 0 &&
                state->dock_last_mode == PTC_OPERATION_MODE_UNDOCKED &&
                state->dock_usage_known && state->dock_day_index == now.day_index &&
                state->undocked_used_ns >= (uint64_t)rules->dock_policy.undocked_daily_minutes * UINT64_C(60000000000))
                out->time_kind = PTC_REASON_TIME_DOCK_ZERO;
        } else if (status->limited_today && ptc_pctl_remaining_exhausted(status)) {
            out->reason = PTC_REASON_DAILY;
            out->instance = (uint64_t)now.day_index + 1;
            out->time_kind = PTC_REASON_TIME_DAILY_ZERO;
        } else return;
    }
    out->reliable = true;
}
