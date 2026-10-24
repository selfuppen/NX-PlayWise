#include "eden_timer.h"
#include "../../sysmodule/sysmodule_internal.h"

#include <stdio.h>
#include <string.h>

bool ptc_eden_refresh_usage(PtcPctlStub *pctl, PtcClockSnapshot now,
    uint64_t elapsed_ns, uint64_t *carry_ns)
{
    bool changed;
    if (now.day_index != pctl->status.usage_day_index) {
        ptc_pctl_stub_reset_daily_usage(pctl);
        *carry_ns = 0;
        changed = true;
    } else {
        changed = ptc_pctl_stub_advance_usage_ns(pctl, elapsed_ns, carry_ns);
    }
    pctl->status.usage_day_index = now.day_index;
    pctl->status.usage_captured_at = now.unix_seconds;
    return changed;
}

PtcErrorCode ptc_eden_read_grant_sample(PtcPctlStub *pctl,
    const PtcPctlVTable *base, uint8_t weekday, PtcClockSnapshot now,
    uint64_t elapsed_ns, uint64_t *carry_ns, PtcPctlStatus *out)
{
    (void)ptc_eden_refresh_usage(pctl, now, elapsed_ns, carry_ns);
    return base->read_grant_status(&pctl->pctl, weekday, out);
}

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
    if (state.eye_usage_counter.known && state.eye_usage_counter.day_index == now.day_index &&
        state.eye_usage_counter.total_ns > used) used = state.eye_usage_counter.total_ns;
    if (state.dock_usage_counter.known && state.dock_usage_counter.day_index == now.day_index &&
        state.dock_usage_counter.total_ns > used) used = state.dock_usage_counter.total_ns;
    pctl->model_elapsed_time = true;
    pctl->direct_usage_model = true;
    pctl->status.usage_supported = pctl->status.usage_available = true;
    pctl->status.usage_day_index = now.day_index;
    pctl->status.usage_captured_at = now.unix_seconds;
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
        target.mode = ptc_compose_base_target(base, &target.minutes);
    }
    pctl->status.play_timer_enabled = true;
    {
        char path[320];
        PtcSetupState setup;
        snprintf(path, sizeof(path), "%s/flags/disable.flag", core->app_root);
        if (core->storage->vtable->exists(core->storage, path)) {
            if (!load_setup_state(core, &setup)) return false;
            if (strcmp(setup.phase, "restored") == 0) {
                PtcPctlSettingsSnapshot original, restored;
                PtcPctlStatus status;
                bool raw_ok = false, timer_ok = false;
                if (!load_install_snapshot(core, &original)) return false;
                /* The NRO owns this in-memory stub. Reconstruct its restored
                   installation image, not the stale pre-restore policy target.
                   The stub selects the day through last_target metadata. */
                pctl->last_target.weekday = target.weekday;
                pctl->applied = true;
                return restore_snapshot_exact(core, &original, &restored, &status,
                    target.weekday, &raw_ok, &timer_ok) == PTC_ERR_OK && raw_ok && timer_ok;
            }
        }
    }
    return pctl->pctl.vtable->apply_target(&pctl->pctl, &target) == PTC_ERR_OK;
}
