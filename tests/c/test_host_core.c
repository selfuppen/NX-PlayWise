#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../common/policy/control_policy.h"
#include "../../common/protocol/error_code.h"
#include "../../common/protocol/result_builder.h"
#include "../../common/time/ptc_time.h"
#include "../../common/token/token_v1.h"
#include "../../companion/auth.h"
#include "../../companion/file_protocol.h"
#include "../../companion/request_client.h"
#include "../../companion/self_check.h"
#include "../../platform/host/fake_time.h"
#include "../../platform/host/mem_storage.h"
#include "../../platform/host/pctl_stub.h"
#include "../../sysmodule/sysmodule_core.h"

static int failures = 0;

static void check_true(bool value, const char *label)
{
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", label);
        ++failures;
    }
}

static void check_int(long actual, long expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s expected %ld got %ld\n", label, expected, actual);
        ++failures;
    }
}

static void check_str(const char *actual, const char *expected, const char *label)
{
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s expected %s got %s\n", label, expected, actual);
        ++failures;
    }
}

static bool used_nonce_callback(uint16_t day_index, uint32_t nonce, void *ctx)
{
    (void)ctx;
    return day_index == 2380 && nonce == 4660;
}

static bool fixed_random(uint8_t *out, size_t out_size, void *ctx)
{
    size_t i;
    uint8_t seed = ctx ? *(uint8_t *)ctx : 0x10u;
    for (i = 0; i < out_size; ++i) {
        out[i] = (uint8_t)(seed + i);
    }
    return true;
}

static void test_token_v1(void)
{
    PtcTokenPayload payload;
    PtcTokenPayload decoded;
    char code[PTC_TOKEN_TEXT_SIZE];

    payload.version = 1;
    payload.action = PTC_TOKEN_ACTION_ADD_TODAY_MINUTES;
    payload.minutes = 30;
    payload.day_index_since_2020 = 2380;
    payload.nonce = 4660;

    check_int(ptc_token_encode(&payload, "test-device", "test-secret", code), PTC_ERR_OK, "token encode ok");
    check_str(code, "241W-2AC0-04HM-7YW5", "token fixture parity");
    check_int(ptc_token_verify(code, "test-device", "test-secret", 2380, 120, NULL, NULL, &decoded), PTC_ERR_OK, "token verify ok");
    check_int(decoded.minutes, 30, "decoded minutes");
    check_int(decoded.nonce, 4660, "decoded nonce");
    check_int(ptc_token_decode("241W2-AC004-HM7YW-51R84", "test-device", "test-secret", &decoded), PTC_ERR_BAD_CODE, "legacy 20-symbol code rejected");
    check_int(ptc_token_decode(code, "test-device", "wrong-secret", &decoded), PTC_ERR_BAD_SIGNATURE, "bad signature");
    check_int(ptc_token_verify(code, "test-device", "test-secret", 2381, 120, NULL, NULL, &decoded), PTC_ERR_WRONG_DATE, "wrong date");
    check_int(ptc_token_verify(code, "test-device", "test-secret", 2380, 120, used_nonce_callback, NULL, &decoded), PTC_ERR_USED_TOKEN, "used nonce");

    payload.minutes = 180;
    payload.nonce = 4661;
    check_int(ptc_token_encode(&payload, "test-device", "test-secret", code), PTC_ERR_OK, "over-limit token encode");
    check_str(code, "24B8-2AC0-04HN-MGYD", "over-limit fixture parity");
    check_int(ptc_token_verify(code, "test-device", "test-secret", 2380, 120, NULL, NULL, &decoded), PTC_ERR_MINUTES_EXCEED_LIMIT, "minutes exceed");
}

static void test_time_and_policy(void)
{
    PtcCapabilities caps;
    PtcPolicyDecision decision;
    caps.play_timer_write_verified = false;
    caps.raw_block_verified = false;
    caps.suspend_verified = false;

    check_int(ptc_day_index_from_unix(1577836800), 0, "2020 epoch day index");
    check_int(ptc_weekday_from_day_index(0), 3, "2020-01-01 weekday");
    check_true(ptc_bedtime_active(30, 1260, 480), "cross-midnight bedtime active");
    check_true(!ptc_bedtime_active(720, 1260, 480), "midday bedtime inactive");
    check_str(ptc_error_reason(PTC_ERR_BAD_SIGNATURE), "bad_signature", "error reason map");

    decision = ptc_policy_decide(PTC_CONTROL_OBSERVE, false, PTC_OPERATION_GRANT_MINUTES, &caps, false, false);
    check_true(decision.dry_run && !decision.may_write_pctl && !decision.consume_nonce_after_success, "observe dry run");

    decision = ptc_policy_decide(PTC_CONTROL_OBSERVE, false, PTC_OPERATION_GRANT_MINUTES, &caps, true, false);
    check_int(decision.error, PTC_ERR_OK, "observe ignores unlimited guard");

    decision = ptc_policy_decide(PTC_CONTROL_GRANT, false, PTC_OPERATION_GRANT_MINUTES, &caps, false, true);
    check_true(!decision.dry_run && decision.may_write_pctl && decision.requires_backup && decision.consume_nonce_after_success, "grant write decision");

    decision = ptc_policy_decide(PTC_CONTROL_GRANT, false, PTC_OPERATION_GRANT_MINUTES, &caps, true, false);
    check_int(decision.error, PTC_ERR_UNLIMITED_NOT_ALLOWED, "grant unlimited guard");

    decision = ptc_policy_decide(PTC_CONTROL_GRANT, false, PTC_OPERATION_BLOCK_TODAY, &caps, false, true);
    check_int(decision.error, PTC_ERR_RAW_BLOCK_NOT_VERIFIED, "raw block gated");

    decision = ptc_policy_decide(PTC_CONTROL_GRANT, true, PTC_OPERATION_STATUS, &caps, false, true);
    check_int(decision.error, PTC_ERR_DISABLED, "disable flag wins");
}

static void test_companion_request_builder_and_file_protocol(void)
{
    PtcMemStorage mem;
    PtcCompanionFileClient client;
    char request_id[PTC_COMPANION_REQUEST_ID_SIZE];
    char json[1024];
    char result[4096];
    char summary[2048];
    PtcResultState result_state;
    PtcBedtimeRule bedtime;
    PtcDayRule week[7];

    ptc_mem_storage_init(&mem);
    ptc_companion_file_client_init(&client, "app", &mem.storage);

    check_int(ptc_companion_make_request_id(request_id, sizeof(request_id), 1783526400123LL, 0xa4f2), PTC_COMPANION_OK, "request id made");
    check_str(request_id, "1783526400123-a4f2", "request id format");

    (void)ptc_companion_status_request_json(json, sizeof(json), request_id, 1783526400);
    check_str(json, "{\"version\":1,\"request_id\":\"1783526400123-a4f2\",\"type\":\"status\",\"created_at\":1783526400,\"payload\":{}}\n", "status request json");

    (void)ptc_companion_offline_code_request_json(json, sizeof(json), request_id, 1783526400, "241W-2AC0-04HM-7YW5");
    check_str(json, "{\"version\":1,\"request_id\":\"1783526400123-a4f2\",\"type\":\"offline_code\",\"created_at\":1783526400,\"payload\":{\"code\":\"241W-2AC0-04HM-7YW5\"}}\n", "offline code request json");

    (void)ptc_companion_parent_minutes_request_json(json, sizeof(json), request_id, 1783526400, "add_today_minutes", 30);
    check_str(json, "{\"version\":1,\"request_id\":\"1783526400123-a4f2\",\"type\":\"add_today_minutes\",\"created_at\":1783526400,\"payload\":{\"minutes\":30}}\n", "parent minutes request json");

    (void)ptc_companion_empty_payload_request_json(json, sizeof(json), request_id, 1783526400, "block_today");
    check_str(json, "{\"version\":1,\"request_id\":\"1783526400123-a4f2\",\"type\":\"block_today\",\"created_at\":1783526400,\"payload\":{}}\n", "empty payload request json");

    bedtime.enabled = true;
    bedtime.start_min = 1260;
    bedtime.end_min = 480;
    (void)ptc_companion_set_bedtime_request_json(json, sizeof(json), request_id, 1783526400, &bedtime);
    check_str(json, "{\"version\":1,\"request_id\":\"1783526400123-a4f2\",\"type\":\"set_bedtime\",\"created_at\":1783526400,\"payload\":{\"enabled\":true,\"start_min\":1260,\"end_min\":480}}\n", "bedtime request json");

    (void)ptc_companion_set_limit_action_request_json(json, sizeof(json), request_id, 1783526400, PTC_LIMIT_ACTION_SUSPEND);
    check_str(json, "{\"version\":1,\"request_id\":\"1783526400123-a4f2\",\"type\":\"set_limit_action\",\"created_at\":1783526400,\"payload\":{\"action\":\"suspend\"}}\n", "limit action request json");

    (void)ptc_companion_parent_unlock_start_request_json(json, sizeof(json), request_id, 1783526400, 15);
    check_str(json, "{\"version\":1,\"request_id\":\"1783526400123-a4f2\",\"type\":\"parent_unlock_start\",\"created_at\":1783526400,\"payload\":{\"duration_minutes\":15}}\n", "unlock start request json");

    check_int(ptc_companion_submit_set_today_limit(&client, "1000-0101", 101, 45), PTC_COMPANION_OK, "submit set today");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/inbox/pending/1000-0101.json", result, sizeof(result)), "set today request readable");
    check_true(strstr(result, "\"type\":\"set_today_limit\"") != NULL && strstr(result, "\"minutes\":45") != NULL, "set today request content");

    check_int(ptc_companion_submit_add_today_minutes(&client, "1000-0102", 102, 15), PTC_COMPANION_OK, "submit add today");
    check_int(ptc_companion_submit_disable_today_limit(&client, "1000-0103", 103), PTC_COMPANION_OK, "submit disable today");
    check_int(ptc_companion_submit_block_today(&client, "1000-0104", 104), PTC_COMPANION_OK, "submit block today");
    check_int(ptc_companion_submit_restore_today_policy(&client, "1000-0105", 105), PTC_COMPANION_OK, "submit restore today");

    for (int i = 0; i < 7; ++i) {
        week[i].mode = i == 6 ? PTC_RULE_MODE_UNLIMITED : PTC_RULE_MODE_LIMIT;
        week[i].minutes = (uint16_t)((i + 1) * 10);
    }
    check_int(ptc_companion_submit_set_weekly_template(&client, "1000-0106", 106, week), PTC_COMPANION_OK, "submit weekly");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/inbox/pending/1000-0106.json", result, sizeof(result)), "weekly request readable");
    check_true(strstr(result, "\"type\":\"set_weekly_template\"") != NULL && strstr(result, "\"mode\":\"unlimited\"") != NULL, "weekly request content");

    check_int(ptc_companion_submit_set_bedtime(&client, "1000-0107", 107, &bedtime), PTC_COMPANION_OK, "submit bedtime");
    check_int(ptc_companion_submit_set_limit_action(&client, "1000-0108", 108, PTC_LIMIT_ACTION_RAW_BLOCK), PTC_COMPANION_OK, "submit limit action");
    check_int(ptc_companion_submit_parent_unlock_start(&client, "1000-0109", 109, 20), PTC_COMPANION_OK, "submit unlock start");
    check_int(ptc_companion_submit_parent_unlock_end(&client, "1000-0110", 110), PTC_COMPANION_OK, "submit unlock end");
    check_int(ptc_companion_submit_probe_play_timer_write(&client, "1000-0111", 111), PTC_COMPANION_OK, "submit play write probe");
    check_int(ptc_companion_submit_probe_raw_block(&client, "1000-0112", 112), PTC_COMPANION_OK, "submit raw probe");
    check_int(ptc_companion_submit_probe_suspend(&client, "1000-0113", 113), PTC_COMPANION_OK, "submit suspend probe");
    check_int(ptc_companion_set_disable_flag(&client, true), PTC_COMPANION_OK, "set disable flag");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"), "disable flag exists");
    check_int(ptc_companion_set_disable_flag(&client, false), PTC_COMPANION_OK, "clear disable flag");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"), "disable flag removed");

    check_int(ptc_companion_submit_status(&client, request_id, 1783526400), PTC_COMPANION_OK, "submit status");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/inbox/pending/1783526400123-a4f2.json.tmp"), "tmp request renamed");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/inbox/pending/1783526400123-a4f2.json"), "pending request visible");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/inbox/pending/1783526400123-a4f2.json", result, sizeof(result)), "pending request readable");
    check_true(strstr(result, "\"type\":\"status\"") != NULL, "pending request content");

    check_int(ptc_companion_read_result(&client, request_id, 7999, 8000, result, sizeof(result)), PTC_COMPANION_PENDING, "missing result pending");
    check_int(ptc_companion_read_result(&client, request_id, 8000, 8000, result, sizeof(result)), PTC_COMPANION_TIMEOUT, "missing result timeout");

    ptc_result_state_default(&result_state, 2380);
    (void)ptc_result_ok_json(result, sizeof(result), "other", "status", "observe", true, &result_state, 1783526401);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/results/1783526400123-a4f2.json", result), "write mismatched result");
    check_int(ptc_companion_read_result(&client, request_id, 0, 8000, result, sizeof(result)), PTC_COMPANION_RESULT_MISMATCH, "mismatched result rejected");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/results/1783526400123-a4f2.json", "{\"version\":1,\"status\":\"ok\"}\n"), "write invalid result");
    check_int(ptc_companion_read_result(&client, request_id, 0, 8000, result, sizeof(result)), PTC_COMPANION_RESULT_INVALID, "invalid result rejected");

    (void)ptc_result_ok_json(result, sizeof(result), request_id, "status", "observe", true, &result_state, 1783526401);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/results/1783526400123-a4f2.json", result), "write matching result");
    check_int(ptc_companion_read_result(&client, request_id, 0, 8000, result, sizeof(result)), PTC_COMPANION_OK, "matching result accepted");
    check_int(ptc_companion_format_result_summary(result, summary, sizeof(summary)), PTC_COMPANION_OK, "ok result summary");
    check_true(strstr(summary, "status: ok") != NULL, "ok summary status");
    check_true(strstr(summary, "mode: observe  dry_run: true") != NULL, "ok summary mode");

    (void)ptc_result_error_json(result, sizeof(result), request_id, "status", "disabled", true, PTC_ERR_DISABLED, &result_state, 1783526402);
    check_int(ptc_companion_format_result_summary(result, summary, sizeof(summary)), PTC_COMPANION_OK, "error result summary");
    check_true(strstr(summary, "status: error") != NULL, "error summary status");
    check_true(strstr(summary, "error: disabled (300)") != NULL, "error summary reason");
    check_true(strstr(summary, "message") == NULL, "summary avoids raw localized message");

    mem.fail_renames = true;
    check_int(ptc_companion_submit_offline_code(&client, "1783526400124-0001", 1783526401, "241W-2AC0-04HM-7YW5"), PTC_COMPANION_RENAME_FAILED, "rename failure surfaced");
}

static void test_companion_auth(void)
{
    PtcMemStorage mem;
    PtcCompanionAuth auth;
    char auth_json[512];
    uint8_t seed = 0x21u;

    ptc_mem_storage_init(&mem);
    ptc_companion_auth_init(&auth, "app", &mem.storage);
    check_int(ptc_companion_auth_state(&auth), PTC_AUTH_READ_FAILED, "missing auth read failed");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/auth.json", "{\"version\":1,\"pin_hash\":\"\",\"pin_salt\":\"\",\"hash\":\"hmac-sha256\",\"updated_at\":0}\n"), "write empty auth");
    check_int(ptc_companion_auth_state(&auth), PTC_AUTH_EMPTY, "empty auth state");
    check_int(ptc_companion_auth_verify_pin(&auth, "1234"), PTC_AUTH_EMPTY, "empty auth verify");
    check_int(ptc_companion_auth_set_pin(&auth, "2468", 1783526400, fixed_random, &seed), PTC_AUTH_OK, "set pin");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/auth.json", auth_json, sizeof(auth_json)), "auth readable");
    check_true(strstr(auth_json, "\"pin_hash\":\"\"") == NULL, "auth hash written");
    check_true(strstr(auth_json, "\"pin_salt\":\"2122232425262728292a2b2c2d2e2f30\"") != NULL, "auth salt deterministic");
    check_int(ptc_companion_auth_state(&auth), PTC_AUTH_OK, "auth configured");
    check_int(ptc_companion_auth_verify_pin(&auth, "0000"), PTC_AUTH_DENIED, "wrong pin denied");
    check_int(ptc_companion_auth_verify_pin(&auth, "2468"), PTC_AUTH_OK, "pin verified");
    check_str(ptc_auth_status_name(PTC_AUTH_DENIED), "denied", "auth status name");
}

static void test_result_validator(void)
{
    PtcResultState state;
    char result[2048];
    ptc_result_state_default(&state, 2380);
    state.raw_block_verified = true;

    check_int(ptc_result_ok_json(result, sizeof(result), "1000-0010", "status", "observe", true, &state, 1783526401), 0, "build ok result");
    check_int(ptc_result_validate(result), PTC_ERR_OK, "validate ok result");

    check_int(ptc_result_error_json(result, sizeof(result), "1000-0011", "offline_code", "grant", false, PTC_ERR_BAD_SIGNATURE, &state, 1783526402), 0, "build error result");
    check_int(ptc_result_validate(result), PTC_ERR_OK, "validate error result");
    check_int(ptc_result_validate("{\"version\":1,\"request_id\":\"x\",\"status\":\"ok\"}\n"), PTC_ERR_BAD_REQUEST, "reject incomplete result");
}

static void write_self_check_done(PtcMemStorage *mem, const char *request_id)
{
    char path[128];
    snprintf(path, sizeof(path), "app/inbox/done/%s.json", request_id);
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, path, "{}\n"), "write self-check done request");
}

static void write_self_check_result(PtcMemStorage *mem, const char *request_id, const char *type, const char *mode, bool dry_run, PtcErrorCode error)
{
    PtcResultState state;
    char path[128];
    char result[4096];
    ptc_result_state_default(&state, 2380);
    state.play_timer_write_verified = true;
    snprintf(path, sizeof(path), "app/results/%s.json", request_id);
    if (error == PTC_ERR_OK) {
        check_int(ptc_result_ok_json(result, sizeof(result), request_id, type, mode, dry_run, &state, 1783526401), 0, "build self-check ok result");
    } else {
        check_int(ptc_result_error_json(result, sizeof(result), request_id, type, mode, dry_run, error, &state, 1783526401), 0, "build self-check error result");
    }
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, path, result), "write self-check result");
}

static void write_self_check_event(PtcMemStorage *mem, const char *request_id, const char *event)
{
    char line[256];
    snprintf(line, sizeof(line), "{\"ts\":1783526401,\"request_id\":\"%s\",\"type\":\"status\",\"event\":\"%s\",\"error\":\"ok\",\"detail\":\"\"}", request_id, event);
    check_true(mem->storage.vtable->append_line(&mem->storage, "app/logs/events.jsonl", line), "write self-check event");
}

static PtcSelfCheckResult run_self_check_for_test(PtcMemStorage *mem, const char *request_id, PtcSelfCheckProfile profile, char *report, size_t report_size)
{
    return ptc_self_check_run(&mem->storage, "app", request_id, profile, NULL, report, report_size);
}

static void test_companion_self_check_observe_success(void)
{
    PtcMemStorage mem;
    PtcSelfCheckResult result;
    char report[8192];
    ptc_mem_storage_init(&mem);
    write_self_check_result(&mem, "sc-observe-ok", "offline_code", "observe", true, PTC_ERR_OK);
    write_self_check_done(&mem, "sc-observe-ok");
    write_self_check_event(&mem, "sc-observe-ok", "request_received");
    write_self_check_event(&mem, "sc-observe-ok", "result_ok");

    result = run_self_check_for_test(&mem, "sc-observe-ok", PTC_SELF_CHECK_OBSERVE_SUCCESS, report, sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_PASS, "self-check observe success passes");
    check_true(strstr(report, "SUMMARY PASS") != NULL, "self-check observe report pass");
}

static void test_companion_self_check_forbidden_event_fails(void)
{
    PtcMemStorage mem;
    PtcSelfCheckResult result;
    char report[8192];
    ptc_mem_storage_init(&mem);
    write_self_check_result(&mem, "sc-observe-bad", "offline_code", "observe", true, PTC_ERR_OK);
    write_self_check_done(&mem, "sc-observe-bad");
    write_self_check_event(&mem, "sc-observe-bad", "request_received");
    write_self_check_event(&mem, "sc-observe-bad", "pctl_apply");

    result = run_self_check_for_test(&mem, "sc-observe-bad", PTC_SELF_CHECK_OBSERVE_SUCCESS, report, sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_FAIL, "self-check forbidden event fails");
    check_true(strstr(report, "FAIL event pctl_apply absent") != NULL, "self-check reports forbidden pctl apply");
}

static void test_companion_self_check_disabled_status(void)
{
    PtcMemStorage mem;
    PtcSelfCheckResult result;
    char report[8192];
    ptc_mem_storage_init(&mem);
    write_self_check_result(&mem, "sc-disabled", "status", "disabled", true, PTC_ERR_DISABLED);
    write_self_check_done(&mem, "sc-disabled");
    write_self_check_event(&mem, "sc-disabled", "result_error");

    result = run_self_check_for_test(&mem, "sc-disabled", PTC_SELF_CHECK_DISABLED_STATUS, report, sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_PASS, "self-check disabled passes");
}

static void test_companion_self_check_play_write_probe(void)
{
    PtcMemStorage mem;
    PtcSelfCheckResult result;
    char report[8192];
    ptc_mem_storage_init(&mem);
    write_self_check_result(&mem, "sc-probe", "probe_play_timer_write", "grant", false, PTC_ERR_OK);
    write_self_check_done(&mem, "sc-probe");
    write_self_check_event(&mem, "sc-probe", "pctl_backup");
    write_self_check_event(&mem, "sc-probe", "probe_ok");
    write_self_check_event(&mem, "sc-probe", "result_ok");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/capabilities.json", "{\"version\":1,\"play_timer_write_verified\":true,\"play_timer_write_backend\":\"pctl-s-v1\",\"raw_block_verified\":false,\"suspend_verified\":false}\n"), "write self-check caps");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/backups/last_pctl_backup.txt", "play_timer_settings_hex=001122\n"), "write self-check backup");

    result = run_self_check_for_test(&mem, "sc-probe", PTC_SELF_CHECK_PLAY_WRITE_PROBE, report, sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_PASS, "self-check play write probe passes");
    check_true(strstr(report, "PASS play write capability persisted") != NULL, "self-check probe capability evidence");
}

static void test_companion_self_check_missing_mismatch_and_pending_fail(void)
{
    PtcMemStorage mem;
    PtcSelfCheckResult result;
    PtcResultState state;
    char report[8192];
    char bad_result[4096];
    ptc_mem_storage_init(&mem);

    result = run_self_check_for_test(&mem, "sc-missing", PTC_SELF_CHECK_GENERIC, report, sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_FAIL, "self-check missing result fails");

    ptc_result_state_default(&state, 2380);
    check_int(ptc_result_ok_json(bad_result, sizeof(bad_result), "different", "status", "observe", true, &state, 1783526401), 0, "build mismatched self-check result");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/results/sc-mismatch.json", bad_result), "write mismatched self-check result");
    write_self_check_done(&mem, "sc-mismatch");
    result = run_self_check_for_test(&mem, "sc-mismatch", PTC_SELF_CHECK_GENERIC, report, sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_FAIL, "self-check mismatched request fails");

    ptc_mem_storage_init(&mem);
    write_self_check_result(&mem, "sc-pending", "status", "observe", true, PTC_ERR_OK);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/sc-pending.json", "{}\n"), "write pending self-check residue");
    result = run_self_check_for_test(&mem, "sc-pending", PTC_SELF_CHECK_GENERIC, report, sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_FAIL, "self-check pending residue fails");
}

static void test_companion_self_check_enforce_snapshot(void)
{
    PtcMemStorage mem;
    PtcSelfCheckResult result;
    char report[8192];
    ptc_mem_storage_init(&mem);
    write_self_check_event(&mem, "unknown", "pctl_backup");
    write_self_check_event(&mem, "unknown", "pctl_apply");
    write_self_check_event(&mem, "unknown", "pctl_start_timer");
    write_self_check_event(&mem, "unknown", "state_persisted");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/state.json", "{\"version\":1,\"parent_unlock_until\":0,\"last_enforced_day_index\":2380,\"last_enforced_mode\":1,\"last_enforced_minutes\":60,\"updated_at\":1783526401}\n"), "write enforce state");

    result = run_self_check_for_test(&mem, "", PTC_SELF_CHECK_ENFORCE_SNAPSHOT, report, sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_PASS, "self-check enforce snapshot passes");
    check_true(strstr(report, "PASS enforce pctl_apply event present") != NULL, "self-check enforce event evidence");
}

static void write_default_files(PtcMemStorage *mem, const char *mode, bool allow_unlimited)
{
    char config[512];
    snprintf(
        config,
        sizeof(config),
        "{\"version\":1,\"device_id\":\"test-device\",\"grant_secret\":\"test-secret\","
        "\"max_add_minutes\":120,\"control_mode\":\"%s\",\"allow_unlimited_to_limited\":%s}\n",
        mode,
        allow_unlimited ? "true" : "false");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/config.json", config), "write config");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/capabilities.json", "{\"version\":1,\"play_timer_write_verified\":false,\"raw_block_verified\":false,\"suspend_verified\":false}\n"), "write capabilities");
}

static void write_capabilities(PtcMemStorage *mem, bool play_timer_write_verified, bool raw_block_verified, bool suspend_verified)
{
    char caps[256];
    snprintf(
        caps,
        sizeof(caps),
        "{\"version\":1,\"play_timer_write_verified\":%s,\"play_timer_write_backend\":\"pctl-s-v1\",\"raw_block_verified\":%s,\"suspend_verified\":%s}\n",
        play_timer_write_verified ? "true" : "false",
        raw_block_verified ? "true" : "false",
        suspend_verified ? "true" : "false");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/capabilities.json", caps), "write custom capabilities");
}

static void make_valid_code(char *out)
{
    PtcTokenPayload payload;
    payload.version = 1;
    payload.action = PTC_TOKEN_ACTION_ADD_TODAY_MINUTES;
    payload.minutes = 30;
    payload.day_index_since_2020 = 2380;
    payload.nonce = 4660;
    check_int(ptc_token_encode(&payload, "test-device", "test-secret", out), PTC_ERR_OK, "make test token");
}

static void test_observe_status_flow(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char request[512];
    char result[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "observe", false);
    (void)ptc_companion_status_request_json(request, sizeof(request), "1000-0001", 1000);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0001.json", request), "write status request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process one observe status");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/inbox/done/1000-0001.json"), "request archived");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0001.json", result, sizeof(result)), "status result written");
    check_true(strstr(result, "\"status\":\"ok\"") != NULL, "status ok result");
    check_true(strstr(result, "\"dry_run\":true") != NULL, "status dry run");
}

static void test_observe_offline_code_allows_unrestricted_dry_run(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_TEXT_SIZE];
    char request[512];
    char result[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "observe", false);
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0025", 1025, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0025.json", request), "write observe unrestricted grant");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process observe unrestricted grant");
    check_true(!pctl.applied, "observe unrestricted grant avoids pctl");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "observe unrestricted grant avoids nonce");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0025.json", result, sizeof(result)), "observe unrestricted grant result");
    check_true(strstr(result, "\"status\":\"ok\"") != NULL, "observe unrestricted grant ok");
    check_true(strstr(result, "\"dry_run\":true") != NULL, "observe unrestricted grant dry run");
    check_true(strstr(result, "\"unrestricted_today\":1") != NULL, "observe unrestricted state reported");
}

static void test_grant_unrestricted_guard_rejects_without_nonce(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_TEXT_SIZE];
    char request[512];
    char result[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", false);
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0026", 1026, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0026.json", request), "write grant unrestricted guard");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process grant unrestricted guard");
    check_true(!pctl.applied, "grant unrestricted guard avoids pctl");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "grant unrestricted guard avoids nonce");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0026.json", result, sizeof(result)), "grant unrestricted guard result");
    check_true(strstr(result, "\"reason\":\"unlimited_not_allowed\"") != NULL, "grant unrestricted guard reason");
}

static void test_grant_requires_play_timer_write_probe(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_TEXT_SIZE];
    char request[512];
    char result[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0027", 1027, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0027.json", request), "write unprobed grant");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process unprobed grant");
    check_true(!pctl.applied, "unprobed grant avoids pctl");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/backups/last_pctl_backup.txt"), "unprobed grant avoids backup");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "unprobed grant avoids nonce");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0027.json", result, sizeof(result)), "unprobed grant result");
    check_true(strstr(result, "\"reason\":\"pctl_write_not_verified\"") != NULL, "unprobed grant reason");
}

static void test_probe_play_timer_write_updates_capability(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char caps[1024];
    char result[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.play_timer_write_probe_succeeds = true;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0028.json", "{\"version\":1,\"request_id\":\"1000-0028\",\"type\":\"probe_play_timer_write\",\"created_at\":1028,\"payload\":{}}\n"), "write play write probe");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process play write probe");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/backups/last_pctl_backup.txt"), "probe backup persisted");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/capabilities.json", caps, sizeof(caps)), "play write capabilities persisted");
    check_true(strstr(caps, "\"play_timer_write_verified\":true") != NULL, "play write capability true");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0028.json", result, sizeof(result)), "play write probe result");
    check_true(strstr(result, "\"play_timer_write_verified\":true") != NULL, "play write result capability");
}

static void test_legacy_play_timer_capability_is_invalidated(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_TEXT_SIZE];
    char request[512];
    char result[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(
        mem.storage.vtable->write_text_atomic(
            &mem.storage,
            "app/capabilities.json",
            "{\"version\":1,\"play_timer_write_verified\":true,\"raw_block_verified\":false,\"suspend_verified\":false}\n"),
        "write legacy capability");
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0029", 1029, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0029.json", request), "write legacy capability grant");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process legacy capability grant");
    check_true(!pctl.applied, "legacy capability avoids pctl write");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0029.json", result, sizeof(result)), "legacy capability result");
    check_true(strstr(result, "\"reason\":\"pctl_write_not_verified\"") != NULL, "legacy capability reason");
}

static void test_grant_flow_consumes_nonce_after_write(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_TEXT_SIZE];
    char request[512];
    char ledger[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0002", 1001, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0002.json", request), "write grant request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process one grant");
    check_true(pctl.applied, "pctl apply called");
    check_int(pctl.last_target.mode, PTC_PCTL_TARGET_LIMIT, "grant target mode");
    check_int(pctl.last_target.minutes, 30, "grant target minutes");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/backups/last_pctl_backup.txt"), "backup persisted");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/ledger/used_nonces.jsonl", ledger, sizeof(ledger)), "ledger persisted");
    check_true(strstr(ledger, "\"day_index\":2380,\"nonce\":4660") != NULL, "nonce consumed");
}

static void test_enforce_tick_applies_once_and_respects_disable_flag(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char state[1024];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "enforce", true);
    write_capabilities(&mem, true, false, false);
    check_int(ptc_sysmodule_enforce_tick(&sysmodule), 1, "enforce applies first target");
    check_true(pctl.applied, "enforce applies pctl");
    check_true(pctl.timer_started, "enforce starts timer");
    check_int(pctl.last_target.mode, PTC_PCTL_TARGET_LIMIT, "enforce target mode");
    check_int(pctl.last_target.minutes, 60, "enforce target minutes");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/state.json", state, sizeof(state)), "enforce state persisted");
    check_true(strstr(state, "\"last_enforced_day_index\":2380") != NULL, "enforce day persisted");

    pctl.applied = false;
    pctl.timer_started = false;
    check_int(ptc_sysmodule_enforce_tick(&sysmodule), 0, "enforce skips unchanged target");
    check_true(!pctl.applied, "unchanged enforce avoids pctl");
    check_true(!pctl.timer_started, "unchanged enforce avoids start timer");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "enforce", true);
    write_capabilities(&mem, true, false, false);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/flags/disable.flag", ""), "write disable flag");
    check_int(ptc_sysmodule_enforce_tick(&sysmodule), 0, "disabled enforce skipped");
    check_true(!pctl.applied, "disabled enforce avoids pctl");
    check_true(!pctl.timer_started, "disabled enforce avoids timer");
}

static void test_backup_failure_blocks_write(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_TEXT_SIZE];
    char request[512];
    char result[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.backup_error = PTC_ERR_PCTL_BACKUP_FAILED;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0003", 1002, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0003.json", request), "write backup fail request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process backup failure");
    check_true(!pctl.applied, "backup failure blocks apply");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "backup failure does not consume nonce");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0003.json", result, sizeof(result)), "backup failure result");
    check_true(strstr(result, "\"reason\":\"pctl_backup_failed\"") != NULL, "backup failure reason");
}

static void test_bad_request_schema_writes_error_result(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char result[4096];
    char events[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0004.json", "{\"version\":1,\"request_id\":\"1000-0004\",\"type\":\"set_today_limit\",\"created_at\":1004}\n"), "write malformed request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process malformed request");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0004.json", result, sizeof(result)), "bad request result");
    check_true(strstr(result, "\"reason\":\"bad_request\"") != NULL, "bad request reason");
    check_true(!pctl.applied, "bad request avoids pctl");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/logs/events.jsonl", events, sizeof(events)), "bad request events");
    check_true(strstr(events, "\"event\":\"result_error\"") != NULL, "bad request event");
}

static void test_observe_rule_request_is_dry_run(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char result[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "observe", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0005.json", "{\"version\":1,\"request_id\":\"1000-0005\",\"type\":\"set_today_limit\",\"created_at\":1005,\"payload\":{\"minutes\":45}}\n"), "write observe rule request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process observe rule request");
    check_true(!pctl.applied, "observe rule request avoids pctl");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/rules.json"), "observe rule request avoids rules write");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0005.json", result, sizeof(result)), "observe rule result");
    check_true(strstr(result, "\"dry_run\":true") != NULL, "observe rule dry run");
}

static void test_grant_set_today_limit_persists_applies_and_logs(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char result[4096];
    char rules[4096];
    char events[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0006.json", "{\"version\":1,\"request_id\":\"1000-0006\",\"type\":\"set_today_limit\",\"created_at\":1006,\"payload\":{\"minutes\":45}}\n"), "write grant set today request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process grant set today");
    check_true(pctl.applied, "grant set today applies pctl");
    check_int(pctl.last_target.mode, PTC_PCTL_TARGET_LIMIT, "grant set target mode");
    check_int(pctl.last_target.minutes, 45, "grant set target minutes");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/rules.json", rules, sizeof(rules)), "rules persisted");
    check_true(strstr(rules, "\"today_override_minutes\":45") != NULL, "today override minutes persisted");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0006.json", result, sizeof(result)), "grant set result");
    check_true(strstr(result, "\"dry_run\":false") != NULL, "grant set not dry run");
    check_true(strstr(result, "\"remaining_minutes\":45") != NULL, "grant set result state minutes");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/logs/events.jsonl", events, sizeof(events)), "grant set events");
    check_true(strstr(events, "\"event\":\"state_persisted\"") != NULL, "state persisted event");
    check_true(strstr(events, "\"event\":\"pctl_apply\"") != NULL, "pctl apply event");
}

static void test_probe_raw_block_updates_capability(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char caps[1024];
    char result[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.raw_probe_succeeds = true;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0007.json", "{\"version\":1,\"request_id\":\"1000-0007\",\"type\":\"probe_raw_block\",\"created_at\":1007,\"payload\":{}}\n"), "write probe request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process raw probe");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/capabilities.json", caps, sizeof(caps)), "capabilities persisted");
    check_true(strstr(caps, "\"raw_block_verified\":true") != NULL, "raw capability true");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0007.json", result, sizeof(result)), "probe result");
    check_true(strstr(result, "\"raw_block_verified\":true") != NULL, "probe result capability");
}

static void test_parent_unlock_state_and_expiry(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char result[4096];
    char state[1024];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0008.json", "{\"version\":1,\"request_id\":\"1000-0008\",\"type\":\"parent_unlock_start\",\"created_at\":1008,\"payload\":{\"duration_minutes\":15}}\n"), "write unlock start");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process unlock start");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/state.json", state, sizeof(state)), "state persisted");
    check_true(strstr(state, "\"parent_unlock_until\":1783527301") != NULL, "unlock until persisted");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0008.json", result, sizeof(result)), "unlock start result");
    check_true(strstr(result, "\"parent_unlock_active\":true") != NULL, "unlock active result");

    fake_time.snapshot.unix_seconds = 1783527302;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0009.json", "{\"version\":1,\"request_id\":\"1000-0009\",\"type\":\"status\",\"created_at\":1009,\"payload\":{}}\n"), "write expiry status");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process expiry status");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0009.json", result, sizeof(result)), "expiry status result");
    check_true(strstr(result, "\"parent_unlock_active\":false") != NULL, "unlock expired result");
}

static void test_more_rule_requests_and_probe_suspend(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char result[4096];
    char rules[4096];
    char state[1024];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0012.json", "{\"version\":1,\"request_id\":\"1000-0012\",\"type\":\"add_today_minutes\",\"created_at\":1012,\"payload\":{\"minutes\":15}}\n"), "write add today request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process add today");
    check_int(pctl.last_target.mode, PTC_PCTL_TARGET_LIMIT, "add target mode");
    check_int(pctl.last_target.minutes, 75, "add target minutes from default weekday");

    pctl.applied = false;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0013.json", "{\"version\":1,\"request_id\":\"1000-0013\",\"type\":\"disable_today_limit\",\"created_at\":1013,\"payload\":{}}\n"), "write disable today request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process disable today");
    check_true(pctl.applied, "disable applies pctl");
    check_int(pctl.last_target.mode, PTC_PCTL_TARGET_UNLIMITED, "disable target unlimited");

    pctl.applied = false;
    write_capabilities(&mem, true, true, false);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0014.json", "{\"version\":1,\"request_id\":\"1000-0014\",\"type\":\"block_today\",\"created_at\":1014,\"payload\":{}}\n"), "write block today request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process block today");
    check_true(pctl.applied, "block applies pctl");
    check_int(pctl.last_target.mode, PTC_PCTL_TARGET_BLOCKED, "block target");

    pctl.applied = false;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0015.json", "{\"version\":1,\"request_id\":\"1000-0015\",\"type\":\"restore_today_policy\",\"created_at\":1015,\"payload\":{}}\n"), "write restore policy request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process restore policy");
    check_true(pctl.applied, "restore applies pctl");
    check_int(pctl.last_target.mode, PTC_PCTL_TARGET_LIMIT, "restore target mode");
    check_int(pctl.last_target.minutes, 60, "restore target minutes");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0016.json", "{\"version\":1,\"request_id\":\"1000-0016\",\"type\":\"set_weekly_template\",\"created_at\":1016,\"payload\":{\"days\":[{\"mode\":\"limit\",\"minutes\":10},{\"mode\":\"limit\",\"minutes\":20},{\"mode\":\"limit\",\"minutes\":30},{\"mode\":\"limit\",\"minutes\":40},{\"mode\":\"limit\",\"minutes\":50},{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"unlimited\",\"minutes\":0}]}}\n"), "write weekly request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process weekly");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/rules.json", rules, sizeof(rules)), "weekly rules persisted");
    check_true(strstr(rules, "\"minutes\":40") != NULL, "weekly minutes persisted");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0017.json", "{\"version\":1,\"request_id\":\"1000-0017\",\"type\":\"set_bedtime\",\"created_at\":1017,\"payload\":{\"enabled\":true,\"start_min\":1260,\"end_min\":480}}\n"), "write bedtime request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process bedtime");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/rules.json", rules, sizeof(rules)), "bedtime rules persisted");
    check_true(strstr(rules, "\"bedtime_enabled\":true") != NULL, "bedtime enabled persisted");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0018.json", "{\"version\":1,\"request_id\":\"1000-0018\",\"type\":\"set_limit_action\",\"created_at\":1018,\"payload\":{\"action\":\"suspend\"}}\n"), "write gated suspend action");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process gated suspend action");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0018.json", result, sizeof(result)), "gated suspend result");
    check_true(strstr(result, "\"reason\":\"suspend_not_verified\"") != NULL, "suspend action gated");

    write_capabilities(&mem, true, true, true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0019.json", "{\"version\":1,\"request_id\":\"1000-0019\",\"type\":\"set_limit_action\",\"created_at\":1019,\"payload\":{\"action\":\"suspend\"}}\n"), "write verified suspend action");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process verified suspend action");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/rules.json", rules, sizeof(rules)), "limit action persisted");
    check_true(strstr(rules, "\"limit_action\":\"suspend\"") != NULL, "suspend action persisted");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0020.json", "{\"version\":1,\"request_id\":\"1000-0020\",\"type\":\"parent_unlock_end\",\"created_at\":1020,\"payload\":{}}\n"), "write unlock end");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process unlock end");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/state.json", state, sizeof(state)), "unlock end state");
    check_true(strstr(state, "\"parent_unlock_until\":0") != NULL, "unlock end persisted");

    pctl.suspend_probe_succeeds = true;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0021.json", "{\"version\":1,\"request_id\":\"1000-0021\",\"type\":\"probe_suspend\",\"created_at\":1021,\"payload\":{}}\n"), "write suspend probe");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process suspend probe");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/capabilities.json", rules, sizeof(rules)), "suspend capability persisted");
    check_true(strstr(rules, "\"suspend_verified\":true") != NULL, "suspend capability true");
}

static void test_failure_paths_do_not_consume_nonce(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_TEXT_SIZE];
    char request[512];
    char result[4096];
    char events[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.write_error = PTC_ERR_PCTL_WRITE_FAILED;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0022", 1022, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0022.json", request), "write pctl fail grant");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process pctl fail grant");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "pctl failure avoids nonce");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0022.json", result, sizeof(result)), "pctl failure result");
    check_true(strstr(result, "\"reason\":\"pctl_write_failed\"") != NULL, "pctl failure reason");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    mem.fail_write_path_contains = "results/";
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0023", 1023, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0023.json", request), "write result fail grant");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process result fail grant");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "result failure avoids nonce");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    mem.fail_write_path_contains = "ledger/";
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0024", 1024, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0024.json", request), "write ledger fail grant");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process ledger fail grant");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "ledger append failed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/logs/events.jsonl", events, sizeof(events)), "ledger failure events");
    check_true(strstr(events, "\"event\":\"nonce_failed\"") != NULL, "ledger failure event");
}

static void test_recover_processing(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/processing/stuck.json", "{}"), "write stuck request");
    check_int(ptc_sysmodule_recover_processing(&sysmodule), 1, "recover stuck processing");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/inbox/pending/stuck.json"), "stuck moved to pending");
}

int main(void)
{
    test_token_v1();
    test_time_and_policy();
    test_companion_request_builder_and_file_protocol();
    test_companion_auth();
    test_result_validator();
    test_companion_self_check_observe_success();
    test_companion_self_check_forbidden_event_fails();
    test_companion_self_check_disabled_status();
    test_companion_self_check_play_write_probe();
    test_companion_self_check_missing_mismatch_and_pending_fail();
    test_companion_self_check_enforce_snapshot();
    test_observe_status_flow();
    test_observe_offline_code_allows_unrestricted_dry_run();
    test_grant_unrestricted_guard_rejects_without_nonce();
    test_grant_requires_play_timer_write_probe();
    test_probe_play_timer_write_updates_capability();
    test_legacy_play_timer_capability_is_invalidated();
    test_grant_flow_consumes_nonce_after_write();
    test_backup_failure_blocks_write();
    test_bad_request_schema_writes_error_result();
    test_observe_rule_request_is_dry_run();
    test_grant_set_today_limit_persists_applies_and_logs();
    test_probe_raw_block_updates_capability();
    test_parent_unlock_state_and_expiry();
    test_more_rule_requests_and_probe_suspend();
    test_enforce_tick_applies_once_and_respects_disable_flag();
    test_failure_paths_do_not_consume_nonce();
    test_recover_processing();

    if (failures != 0) {
        fprintf(stderr, "%d C host tests failed\n", failures);
        return 1;
    }
    printf("C host core tests passed\n");
    return 0;
}
