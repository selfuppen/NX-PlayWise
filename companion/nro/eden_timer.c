#include "eden_timer.h"
#include "../../sysmodule/sysmodule_internal.h"

bool ptc_eden_restore_timer(PtcSysmodule *core, PtcPctlStub *pctl,
    PtcClockSnapshot now, uint64_t *carry_ns)
{
    PtcRuntimeState state;
    PtcRules rules;
    PtcPctlTarget target = {0};
    if (!core || !pctl || !carry_ns || !load_state(core, &state) || !load_rules(core, &rules))
        return false;
    /* Closing the NRO stops the emulator timer. Resume its last sampled
       total, including fractions, so it cannot fall below the saved non-TV
       baseline on restart. No wall-time gap is charged. */
    uint64_t used = (uint64_t)(now.minute_of_day < 30 ? now.minute_of_day : 30) * 60000000000ULL;
    if (state.dock_tracking_started && state.dock_day_index == now.day_index && state.dock_baseline_known)
        used = state.dock_last_used_ns;
    pctl->model_elapsed_time = true;
    pctl->played_minutes_today = (uint32_t)(used / 60000000000ULL);
    pctl->usage_fraction_ns = *carry_ns = used % 60000000000ULL;
    target.weekday = ptc_weekday_from_day_index(now.day_index);
    if (state.last_enforced_day_index == now.day_index &&
        (state.last_enforced_mode == PTC_PCTL_TARGET_LIMIT ||
         state.last_enforced_mode == PTC_PCTL_TARGET_BLOCKED ||
         state.last_enforced_mode == PTC_PCTL_TARGET_UNLIMITED)) {
        target.mode = state.last_enforced_mode;
        target.minutes = state.last_enforced_minutes;
    } else {
        PtcDayRule base = ptc_rules_today_rule(&rules, now.day_index, target.weekday);
        target.mode = target_from_day_rule(base);
        target.minutes = base.minutes;
        if (base.mode == PTC_RULE_MODE_UNLIMITED && (rules.eye_care.enabled || dock_policy_enabled(&rules))) {
            target.mode = PTC_PCTL_TARGET_LIMIT;
            target.minutes = 1440;
        }
    }
    pctl->status.play_timer_enabled = true;
    return pctl->pctl.vtable->apply_target(&pctl->pctl, &target) == PTC_ERR_OK;
}
