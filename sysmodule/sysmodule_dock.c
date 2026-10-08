#include "sysmodule_internal.h"

#define DOCK_MINUTE_NS 60000000000ULL
#define DOCK_DAY_NS (1440ULL * DOCK_MINUTE_NS)

PtcOperationModeStatus ptc_read_operation_mode(PtcSysmodule *sysmodule)
{
    PtcOperationModeStatus status = { PTC_OPERATION_MODE_UNKNOWN, false, false };
    if (sysmodule->operation_mode_provider && sysmodule->operation_mode_provider->read)
        status = sysmodule->operation_mode_provider->read(sysmodule->operation_mode_provider->ctx);
    if (status.mode != PTC_OPERATION_MODE_DOCKED && status.mode != PTC_OPERATION_MODE_UNDOCKED)
        status.mode = PTC_OPERATION_MODE_UNKNOWN;
    return status;
}

bool dock_policy_enabled(const PtcRules *rules)
{
    return rules->dock_policy.force_docked || rules->dock_policy.undocked_limit_enabled;
}

bool dock_policy_blocks(PtcSysmodule *sysmodule, const PtcRules *rules,
    const PtcRuntimeState *state, PtcClockSnapshot now)
{
    PtcOperationModeStatus mode;
    if (!dock_policy_enabled(rules) ||
        (state->dock_waived && state->dock_waived_day_index == now.day_index)) return false;
    mode = ptc_read_operation_mode(sysmodule);
    if (mode.mode == PTC_OPERATION_MODE_DOCKED) return false;
    if (mode.mode == PTC_OPERATION_MODE_UNKNOWN || rules->dock_policy.force_docked) return true;
    return rules->dock_policy.undocked_limit_enabled &&
        (state->dock_day_index != now.day_index || !state->dock_usage_known ||
         state->undocked_used_ns >= (uint64_t)rules->dock_policy.undocked_daily_minutes * DOCK_MINUTE_NS);
}

bool dock_blocks_grants(PtcSysmodule *sysmodule, PtcClockSnapshot now)
{
    PtcRules rules;
    PtcRuntimeState state;
    PtcRuntimeState before;
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &state)) return true;
    if (!dock_policy_enabled(&rules)) return false;
    before = state;
    dock_sample_usage(sysmodule, &rules, &state, now);
    if (memcmp(&before, &state, sizeof(state)) != 0 &&
        !save_state(sysmodule, &state, now.unix_seconds)) return true;
    return dock_policy_blocks(sysmodule, &rules, &state, now);
}

static bool read_used_ns(PtcSysmodule *sysmodule, uint8_t weekday, uint16_t expected_minutes, uint64_t *used)
{
    PtcPctlStatus status;
    uint64_t configured;
    if (sysmodule->pctl->vtable->read_status(sysmodule->pctl, weekday, &status) != PTC_ERR_OK ||
        !status.limited_today || !status.configured_minutes_available ||
        !status.remaining_ns_available || status.remaining_ns < 0 ||
        status.configured_minutes != expected_minutes) return false;
    configured = (uint64_t)status.configured_minutes * DOCK_MINUTE_NS;
    if ((uint64_t)status.remaining_ns > configured) return false;
    *used = configured - (uint64_t)status.remaining_ns;
    return true;
}

void dock_rebaseline(PtcSysmodule *sysmodule, PtcRuntimeState *state, uint8_t weekday)
{
    uint64_t used;
    PtcRules rules;
    PtcDayRule base;
    bool continuity = state->dock_baseline_known;
    /* No new baseline can prove the size of an already lost interval. */
    if (!state->dock_usage_known && !continuity) return;
    if (!load_rules(sysmodule, &rules)) return;
    base = ptc_rules_today_rule(&rules, state->dock_day_index, weekday);
    bool read_ok = read_used_ns(sysmodule, weekday,
        base.mode == PTC_RULE_MODE_UNLIMITED ? 1440u : base.minutes, &used);
    if (!read_ok) {
        PtcPctlStatus status;
        if (sysmodule->pctl->vtable->read_status(sysmodule->pctl, weekday, &status) == PTC_ERR_OK &&
            status.blocked_today) return;
        state->dock_interval_unknown = true;
        state->dock_usage_known = false;
        return;
    }
    if (continuity && (!state->dock_usage_known || state->dock_interval_unknown)) {
        if (used < state->dock_last_used_ns) {
            state->dock_baseline_known = false;
            return;
        }
        uint64_t delta = used - state->dock_last_used_ns;
        state->undocked_used_ns = delta > DOCK_DAY_NS - state->undocked_used_ns
            ? DOCK_DAY_NS : state->undocked_used_ns + delta;
        state->dock_usage_known = true;
    }
    state->dock_baseline_known = true;
    state->dock_last_used_ns = used;
    state->dock_interval_unknown = false;
    state->dock_last_mode = ptc_read_operation_mode(sysmodule).mode;
}

void dock_sample_usage(PtcSysmodule *sysmodule, const PtcRules *rules,
    PtcRuntimeState *state, PtcClockSnapshot now)
{
    PtcOperationModeStatus mode = ptc_read_operation_mode(sysmodule);
    uint64_t used;
    bool boot_gap = !sysmodule->dock_boot_sampled;
    bool rollover = false;
    sysmodule->dock_boot_sampled = true;
    if (!dock_policy_enabled(rules)) {
        /* Disabling a policy never refunds today's accumulated usage. */
        state->dock_baseline_known = false;
        return;
    }
    if (state->dock_tracking_started && state->dock_day_index != now.day_index) {
        rollover = true;
        state->dock_day_index = now.day_index;
        state->undocked_used_ns = 0;
        state->dock_usage_known = true;
        state->dock_baseline_known = false;
        state->dock_interval_unknown = false;
    }
    if (!state->dock_tracking_started) {
        state->dock_tracking_started = true;
        state->dock_day_index = now.day_index;
        state->dock_usage_known = false;
        return;
    }
    if (rollover && state->last_enforced_mode == PTC_PCTL_TARGET_BLOCKED) {
        /* Yesterday's temporary zero target is not a failed new-day timer.
           Resume the base first, then conservatively charge its reliable use. */
        state->dock_last_used_ns = 0;
        state->dock_baseline_known = true;
        state->dock_interval_unknown = true;
        return;
    }
    /* A temporary BLOCKED target hides the base timer. Do not interpret its
       zero allowance as usage or refund usage when the base target is restored. */
    if (state->last_enforced_mode == PTC_PCTL_TARGET_BLOCKED &&
        state->last_enforced_day_index == now.day_index) return;
    PtcDayRule base = ptc_rules_today_rule(rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    bool read_ok = read_used_ns(sysmodule, ptc_weekday_from_day_index(now.day_index),
        base.mode == PTC_RULE_MODE_UNLIMITED ? 1440u : base.minutes, &used);
    /* Midnight may still expose the previous confirmed allowance until
       Enforce installs today's target. Never accept an arbitrary mismatch. */
    if (!read_ok && rollover && state->last_enforced_day_index != now.day_index &&
        state->last_enforced_mode == PTC_PCTL_TARGET_LIMIT)
        read_ok = read_used_ns(sysmodule, ptc_weekday_from_day_index(now.day_index),
            state->last_enforced_minutes, &used);
    if (!read_ok) {
        PtcPctlStatus status;
        /* A newly enabled unlimited day deliberately has no timer until the
           composed target installs the 1440-minute cap. All other failures
           make the allowance unknown, including failure to read that status. */
        bool installing_cap = !state->dock_baseline_known && state->dock_usage_known &&
            !state->dock_interval_unknown && base.mode == PTC_RULE_MODE_UNLIMITED &&
            sysmodule->pctl->vtable->read_status(sysmodule->pctl,
                ptc_weekday_from_day_index(now.day_index), &status) == PTC_ERR_OK && status.unrestricted_today;
        if (installing_cap) return;
        state->dock_interval_unknown = true;
        state->dock_usage_known = false;
        return;
    }
    if (rollover) {
        state->dock_last_used_ns = 0;
        state->dock_baseline_known = true;
    }
    if (!state->dock_usage_known && !state->dock_baseline_known) return;
    if (state->dock_baseline_known) {
        if (used < state->dock_last_used_ns) {
            /* An unexplained timer reset cannot prove remaining allowance. */
            state->dock_usage_known = false;
            state->dock_baseline_known = false;
            return;
        }
        if (boot_gap || state->dock_interval_unknown ||
            state->dock_last_mode != PTC_OPERATION_MODE_DOCKED ||
            mode.mode != PTC_OPERATION_MODE_DOCKED) {
            uint64_t delta = used - state->dock_last_used_ns;
            state->undocked_used_ns = delta > DOCK_DAY_NS - state->undocked_used_ns
                ? DOCK_DAY_NS : state->undocked_used_ns + delta;
        }
        state->dock_usage_known = true;
    }
    state->dock_last_used_ns = used;
    state->dock_baseline_known = true;
    state->dock_last_mode = mode.mode;
    state->dock_interval_unknown = false;
}

static PtcErrorCode validate_dock_enable(PtcSysmodule *sysmodule, const PtcRules *rules,
    const PtcDockPolicy *next)
{
    PtcOperationModeStatus mode = ptc_read_operation_mode(sysmodule);
    char fingerprint[65];
    if (next->force_docked && mode.dock_supported_available && !mode.dock_supported)
        return PTC_ERR_DOCK_UNSUPPORTED;
    if (!(next->force_docked || next->undocked_limit_enabled)) return PTC_ERR_OK;
    if (mode.mode == PTC_OPERATION_MODE_UNKNOWN ||
        (next->force_docked && !mode.dock_supported_available)) return PTC_ERR_DOCK_MODE_UNAVAILABLE;
#ifndef PLAYWISE_EDEN
    if (!dock_policy_enabled(rules) &&
        (rules->bedtime.confirmation_version == 0 || rules->bedtime.official_setting_confirmed_at <= 0 ||
         !sysmodule_environment_fingerprint(sysmodule, fingerprint) ||
         strcmp(fingerprint, rules->bedtime.confirmed_environment) != 0 ||
         (!bedtime_overlay_verified(sysmodule) && !rules->bedtime.unverified_overlay_risk_accepted)))
        return PTC_ERR_DOCK_CONFIRMATION_REQUIRED;
#else
    (void)rules;
    (void)fingerprint;
#endif
    return PTC_ERR_OK;
}

bool process_dock_request(PtcSysmodule *sysmodule, const PtcRequest *request,
    bool disabled, PtcClockSnapshot now)
{
    PtcRules rules;
    PtcRuntimeState state;
    PtcErrorCode err = PTC_ERR_OK;
    bool waive = request->type == PTC_REQUEST_WAIVE_DOCK_POLICY_TODAY;
    if (request->type != PTC_REQUEST_SET_DOCK_POLICY && !waive) return false;
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &state))
        err = PTC_ERR_RULES_INVALID;
    else if (waive && request->dock_expected_day_index != now.day_index)
        err = PTC_ERR_DOCK_DATE_MISMATCH;
    else if (disabled && !waive) err = PTC_ERR_DISABLED;
    else if (!waive) err = validate_dock_enable(sysmodule, &rules, &request->dock_policy);
    if (err != PTC_ERR_OK) {
        (void)finish_with_error(sysmodule, request, "release", true, err, now.day_index);
        return true;
    }
    dock_sample_usage(sysmodule, &rules, &state, now);
    if (!save_state(sysmodule, &state, now.unix_seconds) || !recovery_begin(sysmodule, request, now)) {
        (void)finish_with_error(sysmodule, request, "release", false, PTC_ERR_PCTL_BACKUP_FAILED, now.day_index);
        return true;
    }
    if (waive) {
        state.dock_waived = true;
        state.dock_waived_day_index = now.day_index;
    } else {
        rules.dock_policy = request->dock_policy;
        if (dock_policy_enabled(&rules) && !state.dock_tracking_started) {
            state.dock_tracking_started = true;
            state.dock_day_index = now.day_index;
            state.dock_usage_known = true;
        }
    }
    if (!save_rules(sysmodule, &rules) || !save_state(sysmodule, &state, now.unix_seconds))
        err = PTC_ERR_STORAGE_WRITE_FAILED;
    if (err == PTC_ERR_OK && !disabled) {
        (void)ptc_sysmodule_enforce_request(sysmodule, request);
        if (!load_state(sysmodule, &state) || state.apply_pending_confirmation ||
            (waive && (!state.dock_waived || state.dock_waived_day_index != now.day_index)) ||
            state.dock_enforced != dock_policy_blocks(sysmodule, &rules, &state, now))
            err = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
        if (!waive) {
            PtcRules saved;
            if (!load_rules(sysmodule, &saved) || saved.dock_policy.force_docked != request->dock_policy.force_docked ||
                saved.dock_policy.undocked_limit_enabled != request->dock_policy.undocked_limit_enabled ||
                saved.dock_policy.undocked_daily_minutes != request->dock_policy.undocked_daily_minutes)
                err = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
        }
        if (err == PTC_ERR_OK) {
            PtcPctlStatus status;
            PtcDayRule base = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
            bool blocked = dock_policy_blocks(sysmodule, &rules, &state, now) ||
                bedtime_blocks_grants(sysmodule, now) || state.eye_care_resting;
            bool capped = (rules.eye_care.enabled || dock_policy_enabled(&rules)) && base.mode == PTC_RULE_MODE_UNLIMITED;
            PtcPctlTargetMode target = blocked ? PTC_PCTL_TARGET_BLOCKED : capped ? PTC_PCTL_TARGET_LIMIT : target_from_day_rule(base);
            uint16_t minutes = blocked ? 0 : capped ? 1440u : base.minutes;
            if (sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &status) != PTC_ERR_OK ||
                !target_settings_observed(target, minutes, &status)) err = PTC_ERR_PCTL_EFFECT_NOT_OBSERVED;
        }
    }
    /* A recovery request may run under disable.flag, but must preserve every
       other reason. Restore only the composed target without re-enabling control. */
    if (err == PTC_ERR_OK && disabled && waive && state.dock_enforced) {
        PtcDayRule base = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
        PtcPctlStatus observed;
        PtcPctlTargetMode target = target_from_day_rule(base);
        uint16_t minutes = base.minutes;
        if ((rules.eye_care.enabled || dock_policy_enabled(&rules)) && base.mode == PTC_RULE_MODE_UNLIMITED) {
            target = PTC_PCTL_TARGET_LIMIT; minutes = 1440u;
        }
        if (bedtime_blocks_grants(sysmodule, now) || state.eye_care_resting) {
            target = PTC_PCTL_TARGET_BLOCKED; minutes = 0;
        }
        err = sysmodule->pctl->vtable->read_status(sysmodule->pctl,
            ptc_weekday_from_day_index(now.day_index), &observed);
        if (err != PTC_ERR_OK || !target_settings_observed(target, minutes, &observed))
            err = apply_target(sysmodule, request, now, "release", target, minutes);
        if (err == PTC_ERR_OK) err = observe_target_with_optional_activation(sysmodule, request, now,
            "release", target, minutes, "dock_waive", &observed);
        if (err == PTC_ERR_OK) {
            state.dock_enforced = false;
            state.last_enforced_mode = target;
            state.last_enforced_minutes = minutes;
            state.last_enforced_day_index = now.day_index;
            if (!save_state(sysmodule, &state, now.unix_seconds)) err = PTC_ERR_STORAGE_WRITE_FAILED;
        }
    }
    if (err != PTC_ERR_OK) {
        (void)finish_with_error(sysmodule, request, "release", false, err, now.day_index);
    } else if (write_current_status_result(sysmodule, request, "release", false, now, true)) {
        recovery_clear(sysmodule);
    } else if (!recovery_rollback(sysmodule)) write_disable_flag(sysmodule, "dock_result_restore_failed\n");
    return true;
}
