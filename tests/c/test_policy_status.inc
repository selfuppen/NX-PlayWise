static PtcCompanionResultSummary policy_summary(void)
{
    PtcCompanionResultSummary s = {0};
    s.valid = s.bedtime_available = s.bedtime_enabled = true;
    s.day_index = 2380;
    s.bedtime_next_available = true;
    s.bedtime_next_start_day_index = 2380;
    s.bedtime_next_start_minute = 1260;
    s.bedtime_next_end_minute = 420;
    s.bedtime_next_window_instance_id = ptc_bedtime_window_instance_id(2380, 1260);
    s.dock_available = s.dock_policy_available = s.undocked_usage_available = true;
    s.undocked_limit_enabled = true;
    s.undocked_daily_minutes = 30;
    s.undocked_used_minutes = 12;
    s.undocked_remaining_minutes = 18;
    snprintf(s.operation_mode, sizeof(s.operation_mode), "undocked");
    return s;
}

static void test_policy_status(void)
{
    PtcCompanionResultSummary s = policy_summary();
    PtcPolicyStatus p;
    const PtcUiLanguagePreference languages[] = {PTC_UI_LANGUAGE_SIMPLIFIED,
        PTC_UI_LANGUAGE_TRADITIONAL, PTC_UI_LANGUAGE_ENGLISH};
    for (unsigned i = 0; i < 3; ++i) {
        ptc_ui_language_set_resolved(languages[i]);
        ptc_policy_status_project(&s, true, 2380, 19 * 3600 + 40 * 60, &p);
        check_int(p.bedtime.state, PTC_POLICY_PENDING, "today bedtime pending");
        check_int(p.bedtime.minutes, 80, "bedtime countdown uses local civil time");
        check_true(strstr(p.bedtime.detail, "21:00") && !strchr(p.bedtime.detail, '{'),
            "all languages format bedtime clock and duration");
        check_int(p.dock.minutes, 18, "TV allowance comes from snapshot");
        check_true(strstr(p.dock_rule, "30") && strstr(p.dock_rule, "12"),
            "TV rule preserves limit and estimated use");
        ptc_policy_status_project(&s, true, 2380, 20 * 3600, &p);
        check_int(p.dock.minutes, 18, "TV allowance never decreases with wall time");
    }
    ptc_ui_language_set_resolved(PTC_UI_LANGUAGE_SIMPLIFIED);
    ptc_policy_status_project(&s, true, 2380, 1260 * 60 - 1, &p);
    check_int(p.bedtime.minutes, 1, "positive sub-minute countdown rounds up");
    ptc_policy_status_project(&s, true, 2380, 1260 * 60, &p);
    check_int(p.bedtime.state, PTC_POLICY_WAITING, "boundary awaits backend confirmation");
    s.bedtime_active = true;
    s.bedtime_start_day_index = 2380;
    s.bedtime_start_minute = 1260;
    s.bedtime_end_minute = 420;
    s.bedtime_window_instance_id = s.bedtime_next_window_instance_id;
    ptc_policy_status_project(&s, true, 2380, 22 * 3600, &p);
    check_int(p.bedtime.minutes, 540, "active bedtime ends tomorrow");
    check_true(strstr(p.bedtime.detail, "07:00") != NULL, "active bedtime names end clock");
    s.day_index = 2381;
    ptc_policy_status_project(&s, true, 2381, 420 * 60 - 1, &p);
    check_int(p.bedtime.state, PTC_POLICY_ACTIVE, "previous day bedtime remains active");
    check_int(p.bedtime.minutes, 1, "cross-night release rounds up");
    ptc_policy_status_project(&s, true, 2381, 420 * 60, &p);
    check_int(p.bedtime.state, PTC_POLICY_WAITING, "release boundary is not manufactured");
    s.bedtime_skipped = true;
    ptc_policy_status_project(&s, true, 2381, 3600, &p);
    check_int(p.bedtime.state, PTC_POLICY_SKIPPED, "active instance skip suppresses countdown");
    s = policy_summary();
    s.bedtime_next_start_day_index = 2382;
    ptc_policy_status_project(&s, true, 2380, 496 * 60, &p);
    check_int(p.bedtime.state, PTC_POLICY_NONE_TODAY, "future bedtime is not counted down");
    s.bedtime_skipped_window_available = true;
    s.bedtime_skipped_start_day_index = 2380;
    s.bedtime_skipped_start_minute = 1260;
    s.bedtime_skipped_end_minute = 420;
    s.bedtime_skipped_window_instance_id = ptc_bedtime_window_instance_id(2380, 1260);
    ptc_policy_status_project(&s, true, 2380, 496 * 60, &p);
    check_int(p.bedtime.state, PTC_POLICY_SKIPPED, "today skip survives future next window");
    s.bedtime_skipped_start_day_index = 2378;
    ptc_policy_status_project(&s, true, 2380, 496 * 60, &p);
    check_int(p.bedtime.state, PTC_POLICY_NONE_TODAY, "unrelated old skip is not today's skip");
    s.bedtime_enabled = false;
    ptc_policy_status_project(&s, true, 2380, 0, &p);
    check_int(p.bedtime.state, PTC_POLICY_OFF, "disabled bedtime is explicit");
    s = policy_summary();
    s.undocked_remaining_minutes = 0;
    s.undocked_used_minutes = 30;
    snprintf(s.operation_mode, sizeof(s.operation_mode), "docked");
    ptc_policy_status_project(&s, true, 2380, 496 * 60, &p);
    check_int(p.dock.state, PTC_POLICY_EXHAUSTED, "empty non-TV allowance is not active on TV");
    check_int(p.mode, PTC_UI_T_DOCK_TV_BADGE, "TV mode remains visible");
    snprintf(s.operation_mode, sizeof(s.operation_mode), "undocked");
    s.dock_restriction_active = true;
    ptc_policy_status_project(&s, true, 2380, 496 * 60, &p);
    check_int(p.dock.state, PTC_POLICY_ACTIVE, "backend restriction is shown");
    s.dock_waived_today = true;
    ptc_policy_status_project(&s, true, 2380, 496 * 60, &p);
    check_int(p.dock.state, PTC_POLICY_WAIVED, "waived TV policy suppresses balance countdown");
    check_int(p.dock.minutes, -1, "waived policy has no manufactured balance");
    s.dock_waived_today = s.dock_restriction_active = false;
    s.force_docked = true;
    s.undocked_usage_available = false;
    ptc_policy_status_project(&s, true, 2380, 496 * 60, &p);
    check_int(p.dock.state, PTC_POLICY_TV_ONLY, "TV-only requires no usage estimate");
    check_int(p.dock.minutes, 0, "TV-only permits zero non-TV minutes");
    s.force_docked = false;
    ptc_policy_status_project(&s, true, 2380, 496 * 60, &p);
    check_int(p.dock.state, PTC_POLICY_UNKNOWN, "missing TV usage stays unknown");
    s = policy_summary();
    s.bedtime_next_window_instance_id = 0;
    ptc_policy_status_project(&s, true, 2380, 496 * 60, &p);
    check_int(p.bedtime.state, PTC_POLICY_UNKNOWN, "missing instance cannot manufacture countdown");
    ptc_policy_status_project(&s, false, 2380, 496 * 60, &p);
    check_int(p.dock.minutes, -1, "stale TV balance is unavailable");
    ptc_policy_status_project(&s, true, 2381, 0, &p);
    check_int(p.dock.state, PTC_POLICY_UNKNOWN, "cross-day snapshot is unavailable");
    s.bedtime_available = s.dock_policy_available = false;
    ptc_policy_status_project(&s, true, 2380, 496 * 60, &p);
    check_int(p.bedtime.state, PTC_POLICY_UNKNOWN, "legacy missing bedtime fields stay unknown");
    check_int(p.dock.state, PTC_POLICY_UNKNOWN, "legacy missing TV policy stays unknown");

    PtcResultState state;
    char json[8192];
    ptc_result_state_default(&state, 2380);
    state.day_index = 2380;
    state.bedtime_enabled = state.bedtime_next_available = true;
    state.bedtime_next_start_day_index = 2380;
    state.bedtime_next_start_minute = 1260;
    state.bedtime_next_end_minute = 420;
    state.bedtime_next_window_instance_id = ptc_bedtime_window_instance_id(2380, 1260);
    check_true(ptc_result_ok_json(json, sizeof(json), "policy-test", "status", "release",
        false, &state, 1000) == 0, "policy snapshot serializes");
    check_true(ptc_companion_result_summary_parse(json, &s) && s.bedtime_available && s.dock_policy_available,
        "wire parser records policy field presence");
    cJSON *root = cJSON_Parse(json);
    cJSON *wire_state = cJSON_GetObjectItemCaseSensitive(root, "state");
    cJSON *bedtime = cJSON_GetObjectItemCaseSensitive(wire_state, "bedtime");
    cJSON_DeleteItemFromObjectCaseSensitive(bedtime, "next_end_minute");
    check_true(cJSON_PrintPreallocated(root, json, sizeof(json), false) &&
        ptc_companion_result_summary_parse(json, &s), "legacy partial state still parses");
    ptc_policy_status_project(&s, true, 2380, 496 * 60, &p);
    check_int(p.bedtime.state, PTC_POLICY_UNKNOWN, "missing end time cannot look like midnight");
    cJSON_DeleteItemFromObjectCaseSensitive(wire_state, "bedtime");
    cJSON_DeleteItemFromObjectCaseSensitive(wire_state, "dock");
    check_true(cJSON_PrintPreallocated(root, json, sizeof(json), false) &&
        ptc_companion_result_summary_parse(json, &s) && !s.bedtime_available && !s.dock_policy_available,
        "absent policy objects are not presented as disabled");
    cJSON_Delete(root);

    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    model.status_loaded = model.bedtime_status_available = model.bedtime_status_enabled = true;
    model.bedtime_policy.enabled = false; /* Saved settings may lag a status refresh. */
    model.day_index = 2380;
    model.status_updated_at = 1000;
    model.bedtime_next_available = true;
    model.bedtime_next_start_day_index = 2380;
    model.bedtime_next_start_minute = 1260;
    model.bedtime_next_end_minute = 420;
    model.bedtime_next_window_instance_id = ptc_bedtime_window_instance_id(2380, 1260);
    model.draft_bedtime_policy.enabled = false;
    ptc_ui_project_policy_status(&model, 1000, 2380, 496 * 60, &p);
    check_int(p.bedtime.state, PTC_POLICY_PENDING, "NRO uses backend policy, not draft");
    ptc_ui_project_policy_status(&model, 999, 2380, 496 * 60, &p);
    check_int(p.bedtime.state, PTC_POLICY_UNKNOWN, "clock rollback invalidates snapshot");
    model.eye_care_policy.enabled = model.dock_policy.undocked_limit_enabled = true;
    PtcUiRect rect = ptc_ui_time_status_bar_for_model(&model);
    check_true(rect.y >= 0 && rect.y + rect.h <= 104,
        "all policy rows fit above navigation without overlap");
}
