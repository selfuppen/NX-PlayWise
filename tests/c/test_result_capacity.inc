static void test_result_capacity_and_complete_recent_events(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    PtcSysmodule *sysmodule = malloc(sizeof(*sysmodule));
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    char recent[4096] = "", row[512], result[8192];
    cJSON *root = NULL;
    if (!mem || !sysmodule) { check_true(false, "allocate receipt fixtures"); goto done; }
    seed_active_buffer_fixture(mem, &pctl, &fake_time, sysmodule, 60, 0, true);
    pctl.status.usage_supported = true;
    pctl.status.usage_available = true;
    pctl.status.usage_day_index = 2380;
    pctl.status.usage_ns = UINT64_C(1200000000000);
    pctl.status.usage_captured_at = fake_time.snapshot.unix_seconds;
    for (int i = 0; i < 20; ++i) {
        snprintf(row, sizeof(row),
            "{\"ts\":1791977408,\"request_id\":\"%032d\",\"type\":\"set_eye_care_policy\","
            "\"event\":\"result_ok\",\"error\":\"ok\",\"detail\":\"accepted_current_candidate_verified_with_full_payload\"}\n", i);
        check_true(strlen(recent) + strlen(row) < sizeof(recent), "twenty whole events fit summary");
        strcat(recent, row);
    }
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage,
        "app/support/recent-events.jsonl", recent), "seed full recent-event summary");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage,
        "app/inbox/pending/capacity-status.json",
        "{\"version\":1,\"request_id\":\"capacity-status\",\"type\":\"status\",\"created_at\":1,\"payload\":{}}"),
        "queue status with a full recent-event summary");
    check_int(ptc_sysmodule_process_all(sysmodule), 1, "full-summary status processed");
    check_true(mem->storage.vtable->read_text(&mem->storage,
        "app/results/capacity-status.json", result, sizeof(result)), "read complete large receipt");
    root = cJSON_Parse(result);
    check_true(root != NULL && strlen(result) > 6143 && strlen(result) < sizeof(result),
        "receipt exceeding the former capacity remains complete JSON within transport limit");
    if (root) {
        check_int(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root, "recent_events")), 20,
            "large receipt preserves every whole recent event");
        check_true(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "completed_at")),
            "large receipt retains its commit timestamp");
    }
    cJSON_Delete(root); root = NULL;

    /* More than 4096 bytes across writes must evict whole old rows, and an
       interrupted summary tail must not poison a later durable receipt. */
    char detail[301];
    memset(detail, 'x', sizeof(detail) - 1); detail[sizeof(detail) - 1] = '\0';
    recent[0] = '\0';
    for (int i = 0; i < 9; ++i) {
        snprintf(row, sizeof(row),
            "{\"ts\":1791977408,\"request_id\":\"%032d\",\"type\":\"set_eye_care_policy\","
            "\"event\":\"result_ok\",\"error\":\"ok\",\"detail\":\"%s\"}\n", i, detail);
        check_true(strlen(recent) + strlen(row) + 8 < sizeof(recent), "long rows leave room for a cut tail");
        strcat(recent, row);
    }
    strcat(recent, "{\"cut\":");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage,
        "app/support/recent-events.jsonl", recent), "seed interrupted optional event tail");
    for (int i = 0; i < 12; ++i) {
        char request[256], path[128];
        snprintf(request, sizeof(request),
            "{\"version\":1,\"request_id\":\"capacity-grant-%d\",\"type\":\"add_today_minutes\","
            "\"created_at\":2,\"payload\":{\"minutes\":1}}", i);
        snprintf(path, sizeof(path), "app/inbox/pending/capacity-grant-%d.json", i);
        check_true(mem->storage.vtable->write_text_atomic(&mem->storage, path, request), "queue summary rotation grant");
        check_int(ptc_sysmodule_process_all(sysmodule), 1, "summary rotation grant processed");
        snprintf(path, sizeof(path), "app/results/capacity-grant-%d.json", i);
        check_true(mem->storage.vtable->read_text(&mem->storage, path, result, sizeof(result)), "read rotation receipt");
        root = cJSON_Parse(result);
        check_true(root != NULL && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "completed_at")),
            "summary rotation and incomplete tail never truncate receipts");
        cJSON_Delete(root); root = NULL;
    }
    check_true(mem->storage.vtable->read_text(&mem->storage,
        "app/support/recent-events.jsonl", recent, sizeof(recent)), "rotated summary stays bounded");
    char *cursor = recent;
    while (*cursor) {
        char *newline = strchr(cursor, '\n');
        check_true(newline != NULL, "rotated summary has no partial final row");
        if (!newline) break;
        *newline = '\0';
        root = cJSON_Parse(cursor);
        check_true(root != NULL, "every rotated summary row is complete JSON");
        cJSON_Delete(root); root = NULL;
        cursor = newline + 1;
    }
done:
    cJSON_Delete(root);
    free(sysmodule);
    free(mem);
}

static void test_result_builder_refuses_small_receipts(void)
{
    PtcResultState state;
    char small[256], full[6144];
    ptc_result_state_default(&state, 2380);
    check_int(ptc_result_ok_json(small, sizeof(small), "small", "status", "release", false, &state, 1), -1,
        "success builder rejects a truncated state or commit timestamp");
    check_int(ptc_result_error_json(small, sizeof(small), "small", "status", "release", false,
        PTC_ERR_BAD_REQUEST, &state, 1), -1, "error builder rejects a truncated receipt");
    check_int(ptc_result_ok_json(full, sizeof(full), "full", "status", "release", false, &state, 1), 0,
        "success builder still accepts a complete receipt");
    cJSON *root = cJSON_Parse(full);
    check_true(root != NULL, "complete receipt parses");
    cJSON_Delete(root);
}
