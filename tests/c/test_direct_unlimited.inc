/* Production policy regression: 1952 counts usable console time without a
   synthetic quota. Legacy result parsing remains compatible with old reports. */
static void test_direct_unlimited_lifecycle(void)
{
    Fixture f;
    PtcRules rules;
    PtcRuntimeState state;
    PtcResultState result;
    uint64_t initial = 100 * PTC_USAGE_MINUTE_NS;
    seed_direct_grant(&f, initial, 180);
    expect(load_rules(&f.core, &rules), "load direct unlimited rules");
    for (unsigned i = 0; i < 7; ++i) { rules.week[i].mode = PTC_RULE_MODE_UNLIMITED; rules.week[i].minutes = 120; }
    rules.eye_care.enabled = true; rules.eye_care.play_minutes = 2; rules.eye_care.rest_minutes = 1;
    expect(save_rules(&f.core, &rules), "enable direct unlimited eye policy");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(f.pctl.status.unrestricted_today && f.pctl.last_target.minutes == 0,
        "health-enabled unlimited has no synthetic counting quota");
    f.pctl.status.play_timer_enabled = false;
    direct_sample(&f, initial + 121 * UINT64_C(1000000000), 121);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.eye_care_resting && f.pctl.status.blocked_today,
        "direct use triggers rest even when unlimited official timer flag is false");
    unsigned calls = f.pctl.start_timer_calls;
    uint64_t blocked_total = f.pctl.status.usage_ns;
    direct_sample(&f, blocked_total, 61);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && !state.eye_care_resting && f.pctl.status.unrestricted_today &&
        f.pctl.last_target.minutes == 0 && f.pctl.start_timer_calls == calls,
        "natural rest expiry restores true unlimited without autonomous 1451");
    PtcUsageCounter baseline = state.eye_usage_counter;
    f.pctl.status.usage_available = false;
    f.clock.snapshot.unix_seconds += 60;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.eye_usage_counter.total_ns == baseline.total_ns &&
        f.pctl.status.unrestricted_today && f.pctl.start_timer_calls == calls,
        "failed direct read keeps trusted use and never installs a fallback quota");
    result_state_from_pctl(&result, f.clock.snapshot.day_index, &f.pctl.status);
    fill_extended_result_state(&f.core, &result, &rules, &state, &f.pctl.status, f.clock.snapshot);
    expect(!result.eye_care_unlimited_capped && !result.dock_unlimited_capped &&
        strcmp(result.eye_care_usage_quality, "unknown") == 0, "unknown direct use is independent of cap-free status projection");
    direct_sample(&f, blocked_total + UINT64_C(1000000000), 1);
    request(&f, "direct-limit", "set_today_limit", "{\"minutes\":180}");
    expect(result_has(&f, "direct-limit", "\"status\":\"ok\"") && f.pctl.status.limited_today &&
        f.pctl.configured_minutes == 180, "real limited rule retains its real allowance");
    request(&f, "direct-unlimited", "disable_today_limit", "{}");
    expect(result_has(&f, "direct-unlimited", "\"status\":\"ok\"") && f.pctl.status.unrestricted_today &&
        f.pctl.last_target.minutes == 0, "interactive unlimited retains health counting without a quota");
    free(f.mem);
}

static void test_direct_unlimited_reason_recovery(void)
{
    Fixture f;
    PtcRules rules;
    PtcRuntimeState state;
    PtcOperationModeStatus mode = {PTC_OPERATION_MODE_UNDOCKED, true, true};
    PtcOperationModeProvider provider = {test_operation_mode, &mode};
    seed_direct_grant(&f, 20 * PTC_USAGE_MINUTE_NS, 120);
    f.core.operation_mode_provider = &provider;
    confirm_dock_fixture(&f);
    expect(load_rules(&f.core, &rules), "load unlimited reason fixture");
    for (unsigned i = 0; i < 7; ++i) { rules.week[i].mode = PTC_RULE_MODE_UNLIMITED; rules.week[i].minutes = 0; }
    rules.eye_care.enabled = true;
    expect(save_rules(&f.core, &rules), "seed unlimited reason policies");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    request(&f, "direct-force-dock", "set_dock_policy",
        "{\"force_docked\":true,\"undocked_limit_enabled\":false,\"undocked_daily_minutes\":30}");
    expect(result_has(&f, "direct-force-dock", "\"status\":\"ok\"") && f.pctl.status.blocked_today,
        "unlimited never bypasses a remaining dock reason");
    expect(load_state(&f.core, &state), "load composed direct rest state");
    state.eye_care_resting = true; state.eye_care_break_id = 42;
    state.eye_care_rest_deadline = f.clock.snapshot.unix_seconds + 60;
    expect(save_state(&f.core, &state, f.clock.snapshot.unix_seconds), "seed simultaneous rest metadata");
    unsigned calls = f.pctl.start_timer_calls;
    request(&f, "direct-eye-skip", "skip_eye_care_break", "{\"break_id\":42}");
    expect(result_has(&f, "direct-eye-skip", "\"status\":\"ok\"") && f.pctl.status.blocked_today &&
        f.pctl.start_timer_calls == calls, "removing eye reason preserves dock and does not activate timer");
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage,"app/flags/disable.flag","fixture\n"),
        "seed protection for recovery-only waiver");
    request(&f, "direct-protected-waive", "waive_dock_policy_today", "{\"expected_day_index\":2380}");
    expect(result_has(&f, "direct-protected-waive", "\"status\":\"ok\"") && f.pctl.status.unrestricted_today &&
        f.pctl.last_target.minutes == 0 && f.pctl.start_timer_calls == calls &&
        f.mem->storage.vtable->exists(&f.mem->storage,"app/flags/disable.flag"),
        "last-reason protected recovery restores true unlimited while retaining protection");
    free(f.mem);
}

static void test_eden_direct_unlimited_counter(void)
{
    Fixture f;
    PtcRules rules;
    PtcPctlStatus status;
    uint64_t carry = 0;
    init_fixture(&f);
    expect(load_rules(&f.core,&rules), "load Eden unlimited rules");
    for (unsigned i = 0; i < 7; ++i) rules.week[i].mode = PTC_RULE_MODE_UNLIMITED;
    expect(save_rules(&f.core,&rules) && ptc_eden_restore_timer(&f.core,&f.pctl,f.clock.snapshot,&carry),
        "Eden restores direct unlimited without proxy");
    f.pctl.status.play_timer_enabled = false;
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl,PTC_USAGE_MINUTE_NS+1,&carry);
    expect(f.pctl.pctl.vtable->read_status(&f.pctl.pctl,0,&status)==PTC_ERR_OK &&
        status.usage_supported && status.usage_available && status.usage_ns == 31*PTC_USAGE_MINUTE_NS+1 &&
        status.unrestricted_today, "Eden direct counter includes unlimited subminute use without enabled timer");
    PtcPctlTarget blocked = {PTC_PCTL_TARGET_BLOCKED,0,0};
    expect(f.pctl.pctl.vtable->apply_target(&f.pctl.pctl,&blocked)==PTC_ERR_OK,"block Eden direct timer");
    uint64_t before = status.usage_ns;
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl,PTC_USAGE_MINUTE_NS,&carry);
    expect(f.pctl.pctl.vtable->read_status(&f.pctl.pctl,0,&status)==PTC_ERR_OK && status.usage_ns==before,
        "Eden blocked time does not add direct consumption");
    PtcPctlTarget unlimited = {PTC_PCTL_TARGET_UNLIMITED,0,0};
    expect(f.pctl.pctl.vtable->apply_target(&f.pctl.pctl,&unlimited)==PTC_ERR_OK,"resume Eden direct timer");
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl,1,&carry);
    expect(f.pctl.pctl.vtable->read_status(&f.pctl.pctl,0,&status)==PTC_ERR_OK && status.usage_ns==before+1,
        "Eden restriction and resume retain fractional direct consumption");
    f.pctl.played_minutes_today = 1440;
    f.pctl.usage_fraction_ns = carry = 0;
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl,1,&carry);
    expect(f.pctl.pctl.vtable->read_status(&f.pctl.pctl,0,&status)==PTC_ERR_OK && status.usage_ns==1440*PTC_USAGE_MINUTE_NS,
        "Eden day bound cannot acquire a fractional overflow");
    free(f.mem);
}
