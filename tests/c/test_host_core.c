#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#endif

#include "../../common/policy/control_policy.h"
#include "../../common/protocol/error_code.h"
#include "../../common/protocol/result_builder.h"
#include "../../common/protocol/request_schema.h"
#include "../../common/time/ptc_time.h"
#include "../../common/token/token_v1.h"
#include "../../common/token/token_v2.h"
#include "../../companion/auth.h"
#include "../../companion/file_protocol.h"
#include "../../companion/overlay/bridge.h"
#include "../../companion/overlay/input_model.h"
#include "../../companion/request_client.h"
#include "../../companion/result_summary.h"
#include "../../companion/self_check.h"
#include "../../companion/transport_client.h"
#include "../../platform/host/fake_time.h"
#include "../../platform/host/mem_storage.h"
#include "../../platform/host/pctl_stub.h"
#include "../../platform/switch/play_timer_settings_layout.h"
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

static void test_token_v2(void)
{
    PtcTokenV2Payload decoded;
    char code[PTC_TOKEN_V2_TEXT_SIZE];
    uint8_t tier;
    unsigned int index;

    for (index = 0; index < PTC_TOKEN_V2_TIER_COUNT; ++index) {
        uint16_t minutes = (uint16_t)((index + 1u) * PTC_TOKEN_V2_TIER_MINUTES);
        check_int(ptc_token_v2_tier_for_minutes(minutes, &tier), PTC_ERR_OK, "v2 minutes map to tier");
        check_int(tier, index, "v2 tier index mapping");
        check_int(ptc_token_v2_encode(tier, (uint16_t)index, "test-device", "test-secret", 2380, code), PTC_ERR_OK, "v2 encode tier");
        check_int(strlen(code), PTC_TOKEN_V2_TEXT_LEN, "v2 code is eight digits");
        check_int(ptc_token_v2_verify(code, "test-device", "test-secret", 2380, 120, NULL, NULL, &decoded), PTC_ERR_OK, "v2 verify tier");
        check_int(decoded.tier_index, index, "v2 decoded tier");
        check_int(decoded.minutes, minutes, "v2 decoded minutes");
        check_int(decoded.nonce, index, "v2 decoded nonce");
    }
    check_int(ptc_token_v2_encode(5, 7, "test-device", "test-secret", 2380, code), PTC_ERR_OK, "v2 fixture encode");
    check_str(code, "10514680", "v2 Python/C fixture parity");
    check_int(ptc_token_v2_encode(0, 0, "test-device", "test-secret", 2380, code), PTC_ERR_OK, "v2 leading zero encode");
    check_str(code, "00002848", "v2 leading zero preserved");
    check_int(ptc_token_v2_decode(code, "other-device", "test-secret", 2380, &decoded), PTC_ERR_BAD_SIGNATURE, "v2 wrong device");
    check_int(ptc_token_v2_decode(code, "test-device", "wrong-secret", 2380, &decoded), PTC_ERR_BAD_SIGNATURE, "v2 wrong secret");
    check_int(ptc_token_v2_decode(code, "test-device", "test-secret", 2381, &decoded), PTC_ERR_BAD_SIGNATURE, "v2 wrong day");
    check_int(ptc_token_v2_decode("1234567", "test-device", "test-secret", 2380, &decoded), PTC_ERR_BAD_CODE, "v2 wrong length");
    check_int(ptc_token_v2_decode("67108864", "test-device", "test-secret", 2380, &decoded), PTC_ERR_BAD_CODE, "v2 over 26 bit range");
    check_int(ptc_token_v2_decode("50331648", "test-device", "test-secret", 2380, &decoded), PTC_ERR_BAD_CODE, "v2 invalid tier bits");
    check_int(ptc_token_v2_tier_for_minutes(6, &tier), PTC_ERR_BAD_CODE, "v2 rejects non-tier minutes");
}

static void test_overlay_input_and_shared_result_summary(void)
{
    PtcOverlayInput input;
    PtcOverlayBridge bridge;
    PtcMemStorage mem;
    PtcCompanionResultSummary summary;
    char formatted[32];
    char result[2048];
    PtcResultState state;
    unsigned int i;
    ptc_overlay_input_init(&input);
    check_int(input.cursor, 1, "overlay cursor starts at one");
    check_true(ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_LEFT, PTC_OVERLAY_BUTTON_LEFT, 0), "overlay left consumed");
    check_int(input.cursor, 3, "overlay left wraps within top row");
    check_true(ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_UP, PTC_OVERLAY_BUTTON_UP, 0), "overlay up consumed");
    check_int(input.cursor, 9, "overlay up skips blank bottom-right cell");

    ptc_overlay_input_init(&input);
    check_true(ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_RIGHT, PTC_OVERLAY_BUTTON_RIGHT, 0), "overlay repeat starts with immediate move");
    check_int(input.cursor, 2, "overlay repeat immediate cursor");
    check_true(ptc_overlay_input_handle(&input, 0, PTC_OVERLAY_BUTTON_RIGHT, PTC_OVERLAY_REPEAT_DELAY_MS - 1), "overlay held direction consumed before delay");
    check_int(input.cursor, 2, "overlay repeat waits for initial delay");
    check_true(ptc_overlay_input_handle(&input, 0, PTC_OVERLAY_BUTTON_RIGHT, 1), "overlay held direction reaches delay");
    check_int(input.cursor, 3, "overlay repeat moves at initial delay");
    check_true(ptc_overlay_input_handle(&input, 0, PTC_OVERLAY_BUTTON_RIGHT, PTC_OVERLAY_REPEAT_INTERVAL_MS - 1), "overlay held direction waits for interval");
    check_int(input.cursor, 3, "overlay repeat waits between moves");
    check_true(ptc_overlay_input_handle(&input, 0, PTC_OVERLAY_BUTTON_RIGHT, 1), "overlay held direction reaches interval");
    check_int(input.cursor, 1, "overlay repeat moves at interval");
    check_true(!ptc_overlay_input_handle(&input, 0, 0, PTC_OVERLAY_REPEAT_INTERVAL_MS * 2), "overlay release is not consumed");
    check_int(input.cursor, 1, "overlay release stops repeat");
    check_true(ptc_overlay_input_handle(
        &input,
        PTC_OVERLAY_BUTTON_LEFT | PTC_OVERLAY_BUTTON_RIGHT,
        PTC_OVERLAY_BUTTON_LEFT | PTC_OVERLAY_BUTTON_RIGHT,
        0), "overlay multiple directions consumed");
    check_int(input.cursor, 1, "overlay multiple directions do not move");
    check_true(!ptc_overlay_input_handle(&input, 0, PTC_OVERLAY_BUTTON_A, 1000), "overlay held action is not repeated");
    check_int(input.length, 0, "overlay held action does not enter character");

    ptc_overlay_input_init(&input);
    (void)ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_RIGHT, PTC_OVERLAY_BUTTON_RIGHT, 0);
    (void)ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_DOWN, PTC_OVERLAY_BUTTON_DOWN, 0);
    (void)ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_DOWN, PTC_OVERLAY_BUTTON_DOWN, 0);
    (void)ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_DOWN, PTC_OVERLAY_BUTTON_DOWN, 0);
    check_int(input.cursor, 0, "overlay reaches centered zero key");
    (void)ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_LEFT, PTC_OVERLAY_BUTTON_LEFT, 0);
    check_int(input.cursor, 0, "overlay skips blank cell left of zero");
    (void)ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_RIGHT, PTC_OVERLAY_BUTTON_RIGHT, 0);
    check_int(input.cursor, 0, "overlay skips blank cell right of zero");

    ptc_overlay_input_init(&input);
    check_true(!ptc_overlay_input_can_submit(&input), "overlay incomplete code cannot submit");
    for (i = 0; i < PTC_OVERLAY_CODE_SYMBOLS; ++i) {
        check_true(ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_A, PTC_OVERLAY_BUTTON_A, 0), "overlay A enters character");
        check_int(input.length, i + 1u, "overlay advances to next input slot after digit");
    }
    check_true(ptc_overlay_input_can_submit(&input), "overlay accepts exactly eight digits");
    check_true(ptc_overlay_input_format(&input, formatted, sizeof(formatted)), "overlay code formats");
    check_str(formatted, "11111111", "overlay formats numeric short code");
    check_int(input.cursor, 1, "overlay keypad focus remains on repeated digit");
    (void)ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_X, PTC_OVERLAY_BUTTON_X, 0);
    check_true(!ptc_overlay_input_can_submit(&input), "overlay delete disables submit");
    check_int(input.cursor, 1, "overlay delete keeps keyboard focus");
    (void)ptc_overlay_input_handle(&input, PTC_OVERLAY_BUTTON_Y, PTC_OVERLAY_BUTTON_Y, 0);
    check_int(input.length, 0, "overlay clear empties code");
    check_int(input.cursor, 1, "overlay clear keeps keyboard focus");
    ptc_overlay_input_tick(&input, PTC_OVERLAY_REQUEST_TIMEOUT_MS, PTC_OVERLAY_REQUEST_TIMEOUT_MS);
    check_true(input.timed_out, "overlay input timeout fires");

    ptc_result_state_default(&state, 2380);
    state.remaining_available = true;
    state.remaining_minutes = 30;
    state.played_minutes_available = true;
    state.played_minutes = 60;
    state.play_timer_enabled = 1;
    state.restricted_now = 0;
    (void)ptc_result_ok_json(result, sizeof(result), "sum-1", "offline_code", "grant", false, &state, 1);
    check_true(ptc_companion_result_summary_parse(result, &summary), "shared result summary parses");
    check_true(summary.unlock_observed, "shared result summary recognizes unlock");
    check_int(summary.remaining_minutes, 30, "shared result summary remaining minutes");
    check_int(summary.played_minutes, 60, "shared result summary played minutes");
    check_true(summary.played_minutes_available, "shared result summary played availability");
    (void)ptc_result_ok_json(result, sizeof(result), "sum-2", "offline_code", "observe", true, &state, 1);
    check_true(ptc_companion_result_summary_parse(result, &summary), "observe summary parses");
    check_true(!summary.unlock_observed, "observe summary never claims unlock");

    ptc_mem_storage_init(&mem);
    ptc_overlay_bridge_init(&bridge, "app", &mem.storage);
    check_int(ptc_overlay_bridge_submit(&bridge, "01234567", 1000, 0x12), PTC_COMPANION_OK, "overlay bridge submits queue request");
    check_int(ptc_overlay_bridge_transport_state(&bridge), PTC_TRANSPORT_ROUTE_SD_QUEUE, "overlay bridge reports SD queue transport");
    check_true(ptc_overlay_bridge_waiting(&bridge), "overlay bridge enters waiting state");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/inbox/pending/1000000-0012.json"), "overlay bridge uses pending atomic protocol");
    check_int(ptc_overlay_bridge_poll(&bridge, PTC_OVERLAY_REQUEST_TIMEOUT_MS, PTC_OVERLAY_REQUEST_TIMEOUT_MS), PTC_COMPANION_TIMEOUT, "overlay bridge times out at sixty seconds");
    check_int(ptc_overlay_bridge_last_status(&bridge), PTC_COMPANION_TIMEOUT, "overlay bridge preserves transport failure status");
    check_str(ptc_overlay_bridge_error_message_zh(&bridge), "后台响应超时，请重试",
        "overlay bridge maps timeout to Chinese feedback");
    check_true(!ptc_overlay_bridge_waiting(&bridge), "overlay bridge exits waiting after timeout");

    bridge.last_status = PTC_COMPANION_WRITE_FAILED;
    check_str(ptc_overlay_bridge_error_message_zh(&bridge), "请求写入失败，请检查 SD 卡",
        "overlay bridge maps write failure to Chinese feedback");
    bridge.last_status = PTC_COMPANION_RESULT_INVALID;
    check_str(ptc_overlay_bridge_error_message_zh(&bridge), "后台返回的结果无效",
        "overlay bridge maps invalid result to Chinese feedback");
    bridge.last_status = PTC_COMPANION_BAD_ARGUMENT;
    check_str(ptc_overlay_bridge_error_message_zh(&bridge), "请求被后台拒绝",
        "overlay bridge maps rejected request to Chinese feedback");

    memset(&bridge.summary, 0, sizeof(bridge.summary));
    bridge.summary.valid = true;
    bridge.summary.ok = true;
    bridge.summary.dry_run = true;
    check_str(ptc_overlay_bridge_error_message_zh(&bridge), "当前为演练模式，未实际解锁",
        "overlay bridge maps successful dry run to Chinese feedback");
    (void)ptc_result_error_json(result, sizeof(result), "sum-3", "offline_code", "grant", true,
        PTC_ERR_USED_TOKEN, &state, 1);
    check_true(ptc_companion_result_summary_parse(result, &bridge.summary),
        "shared result summary parses dry-run structured error");
    check_str(ptc_overlay_bridge_error_message_zh(&bridge), "授权码已经使用过",
        "overlay bridge prioritizes structured error over dry run feedback");
}

static void test_time_and_policy(void)
{
    PtcCapabilities caps;
    PtcPolicyDecision decision;
    uint16_t local_day = 0;
    caps.play_timer_write_verified = false;
    caps.raw_block_verified = false;
    caps.suspend_verified = false;

    check_int(ptc_day_index_from_unix(1577836800), 0, "2020 epoch day index");
    check_int(ptc_day_index_from_unix(1783785600), 2383, "UTC day index before UTC midnight");
    check_int(ptc_day_index_from_unix_utc8(1783785599), 2383, "UTC+8 day index before local midnight");
    check_int(ptc_day_index_from_unix_utc8(1783785600), 2384, "UTC+8 day index after local midnight");
    check_int(ptc_minute_of_day_from_unix_utc8(1783786320), 12, "UTC+8 minute of day");
    check_true(ptc_day_index_from_date(2026, 7, 12, &local_day), "2026-07-12 local date accepted");
    check_int(local_day, 2384, "2026-07-12 local day index");
    check_true(!ptc_day_index_from_date(2026, 2, 29, &local_day), "invalid local date rejected");
    check_int(ptc_weekday_from_day_index(0), 3, "2020-01-01 weekday");
    check_true(ptc_bedtime_active(30, 1260, 480), "cross-midnight bedtime active");
    check_true(!ptc_bedtime_active(720, 1260, 480), "midday bedtime inactive");
    check_int(
        ptc_nonnegative_minutes_from_nanoseconds(INT64_C(1366) * PTC_NANOSECONDS_PER_MINUTE),
        1366,
        "positive PCTL duration converts to minutes");
    check_int(
        ptc_nonnegative_minutes_from_nanoseconds(-INT64_C(29) * PTC_NANOSECONDS_PER_MINUTE),
        0,
        "expired PCTL duration clamps to zero");
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
    char path[240];
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
    check_int(ptc_companion_submit_probe_play_timer_effect(&client, "1000-0115", 115, false), PTC_COMPANION_OK, "submit play effect probe");
    check_int(ptc_companion_submit_probe_apply_today_limit(&client, "1000-0112", 112), PTC_COMPANION_OK, "submit probe apply today limit");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/inbox/pending/1000-0112.json", result, sizeof(result)), "probe apply request readable");
    check_true(strstr(result, "\"type\":\"probe_apply_today_limit\"") != NULL && strstr(result, "\"minutes\":1") != NULL && strstr(result, "\"start_timer\":true") != NULL, "probe apply request content");
    check_int(ptc_companion_submit_probe_raw_block(&client, "1000-0113", 113), PTC_COMPANION_OK, "submit raw probe");
    check_int(ptc_companion_submit_probe_suspend(&client, "1000-0114", 114), PTC_COMPANION_OK, "submit suspend probe");
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
    snprintf(path, sizeof(path), "app/inbox/done/%s.json", request_id);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, path, "{}"), "write done without result");
    check_int(ptc_companion_read_result(&client, request_id, 0, 8000, result, sizeof(result)), PTC_COMPANION_WRITE_FAILED,
        "done without result reports persistence failure");
    check_true(mem.storage.vtable->remove_path(&mem.storage, path), "remove done without result");

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
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/capabilities.json", "{\"version\":1,\"play_timer_write_verified\":true,\"play_timer_write_backend\":\"pctl-s-v2\",\"raw_block_verified\":false,\"suspend_verified\":false}\n"), "write self-check caps");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/backups/last_pctl_backup.txt", "play_timer_settings_hex=001122\n"), "write self-check backup");

    result = run_self_check_for_test(&mem, "sc-probe", PTC_SELF_CHECK_PLAY_WRITE_PROBE, report, sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_PASS, "self-check play write probe passes");
    check_true(strstr(report, "PASS play write capability persisted") != NULL, "self-check probe capability evidence");

    ptc_mem_storage_init(&mem);
    write_self_check_result(&mem, "sc-probe-enforce", "probe_play_timer_write", "enforce", false, PTC_ERR_OK);
    write_self_check_done(&mem, "sc-probe-enforce");
    write_self_check_event(&mem, "sc-probe-enforce", "pctl_backup");
    write_self_check_event(&mem, "sc-probe-enforce", "probe_ok");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/capabilities.json", "{\"version\":1,\"play_timer_write_verified\":true,\"play_timer_write_backend\":\"pctl-s-v2\",\"raw_block_verified\":false,\"suspend_verified\":false}\n"), "write self-check enforce caps");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/backups/last_pctl_backup.txt", "play_timer_settings_hex=001122\n"), "write self-check enforce backup");

    result = run_self_check_for_test(&mem, "sc-probe-enforce", PTC_SELF_CHECK_PLAY_WRITE_PROBE, report, sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_PASS, "self-check play write probe accepts enforce mode");
    check_true(strstr(report, "PASS result mode is write-capable") != NULL, "self-check probe write-capable mode evidence");
}

static void test_play_timer_settings_layout(void)
{
    uint16_t words[PTC_PLAY_TIMER_SETTINGS_WORDS] = {
        0x0101U, 0x0001U, 0U, 0U, 0U, 0U, 0U,
        0x0600U, 0x0100U, 0x0578U, 0U,
        0x0600U, 0x0100U, 0x0578U, 0U,
        0x0600U, 0x0100U, 0x0578U, 0U,
        0x0600U, 0x0100U, 0x0578U, 0U,
        0x0600U, 0x0100U, 0x0578U, 0U,
        0x0600U, 0x0100U, 0x0578U, 0U,
        0x0600U, 0x0100U, 0x0578U,
    };
    uint16_t before[PTC_PLAY_TIMER_SETTINGS_WORDS];
    uint16_t minutes = 0;
    char summary[320];
    unsigned int i;

    memcpy(before, words, sizeof(words));
    check_true(ptc_play_timer_settings_valid(words, PTC_PLAY_TIMER_SETTINGS_WORDS), "real-device play timer layout accepted");
    check_true(ptc_play_timer_settings_get_minutes(words, PTC_PLAY_TIMER_SETTINGS_WORDS, 4U, &minutes), "Thursday minutes readable");
    check_int(minutes, 1400, "Thursday fixture minutes");
    check_true(ptc_play_timer_settings_set_day(words, PTC_PLAY_TIMER_SETTINGS_WORDS, 4U, true, 5U), "Thursday limit writable");
    check_int(words[25], 5, "Thursday minutes use word 25");
    for (i = 0; i < PTC_PLAY_TIMER_SETTINGS_WORDS; ++i) {
        if (i != 25U) {
            check_int(words[i], before[i], "non-target play timer word preserved");
        }
    }
    ptc_play_timer_settings_summary(summary, sizeof(summary), words, PTC_PLAY_TIMER_SETTINGS_WORDS);
    check_true(strstr(summary, "d4:flag=1536,enabled=256,m=5") != NULL, "layout summary reports Thursday target");
    memcpy(words, before, sizeof(words));
    check_true(ptc_play_timer_settings_set_day(words, PTC_PLAY_TIMER_SETTINGS_WORDS, 4U, false, PTC_PLAY_TIMER_UNLIMITED), "Thursday unlimited writable");
    check_int(words[23], 0, "Thursday unlimited clears configured flag");
    check_int(words[24], 0, "Thursday unlimited clears restricted flag");
    check_int(words[25], PTC_PLAY_TIMER_UNLIMITED, "Thursday unlimited sentinel");
    memcpy(words, before, sizeof(words));
    check_true(ptc_play_timer_settings_set_day(words, PTC_PLAY_TIMER_SETTINGS_WORDS, 4U, true, 0U), "Thursday block writable");
    check_int(words[23], PTC_PLAY_TIMER_DAY_CONFIGURED, "Thursday block keeps configured flag");
    check_int(words[24], PTC_PLAY_TIMER_DAY_RESTRICTED, "Thursday block keeps restricted flag");
    check_int(words[25], 0, "Thursday block uses zero minutes");
    memcpy(words, before, sizeof(words));
    check_true(ptc_play_timer_settings_set_day(words, PTC_PLAY_TIMER_SETTINGS_WORDS, 6U, true, 30U), "Saturday limit writable");
    check_int(words[33], 30, "Saturday minutes use final word 33");
    check_true(!ptc_play_timer_settings_set_day(words, PTC_PLAY_TIMER_SETTINGS_WORDS, 7U, true, 30U), "out-of-range weekday rejected");
    check_true(!ptc_play_timer_settings_get_minutes(words, PTC_PLAY_TIMER_SETTINGS_WORDS, 7U, &minutes), "out-of-range weekday unreadable");
    memcpy(words, before, sizeof(words));
    words[7] = 1U;
    check_true(!ptc_play_timer_settings_valid(words, PTC_PLAY_TIMER_SETTINGS_WORDS), "unexpected layout flag rejected");
    memcpy(words, before, sizeof(words));
    words[8] = 1U;
    check_true(!ptc_play_timer_settings_valid(words, PTC_PLAY_TIMER_SETTINGS_WORDS), "unexpected layout enable value rejected");
    memcpy(words, before, sizeof(words));
    words[25] = 1500U;
    check_true(!ptc_play_timer_settings_valid(words, PTC_PLAY_TIMER_SETTINGS_WORDS), "unexpected layout minutes rejected");
}

static void test_companion_self_check_play_timer_effect_probe(void)
{
    PtcMemStorage mem;
    PtcSelfCheckResult result;
    char report[4096];

    ptc_mem_storage_init(&mem);
    check_true(
        mem.storage.vtable->write_text_atomic(
            &mem.storage,
            "app/results/sc-effect.json",
            "{\"version\":1,\"request_id\":\"sc-effect\",\"type\":\"probe_play_timer_effect\","
            "\"status\":\"ok\",\"mode\":\"grant\",\"dry_run\":false,"
            "\"state\":{\"day_index\":2380,\"limited_today\":1,\"blocked_today\":0,\"unrestricted_today\":0,"
            "\"remaining_available\":true,\"remaining_minutes\":1,\"play_timer_enabled\":1,\"restricted_now\":0,"
            "\"bedtime_active\":false,\"parent_unlock_active\":false},"
            "\"capabilities\":{\"play_timer_effect_verified\":true,\"raw_block_verified\":false,\"suspend_verified\":false},"
            "\"pctl_effect_probe\":{\"verdict\":\"pass\",\"checks\":{\"raw_restored\":true,\"timer_restored\":true}},"
            "\"completed_at\":1783526401}\n"),
        "write effect self-check result");
    write_self_check_done(&mem, "sc-effect");
    write_self_check_event(&mem, "sc-effect", "pctl_backup");
    write_self_check_event(&mem, "sc-effect", "effect_before");
    write_self_check_event(&mem, "sc-effect", "effect_restore");
    check_true(
        mem.storage.vtable->write_text_atomic(
            &mem.storage,
            "app/capabilities.json",
            "{\"version\":1,\"play_timer_write_verified\":true,\"play_timer_effect_verified\":true,"
            "\"play_timer_effect_backend\":\"pctl-s-runtime-v2\"}\n"),
        "write effect self-check capabilities");
    check_true(
        mem.storage.vtable->write_text_atomic(
            &mem.storage,
            "app/backups/last_pctl_backup.txt",
            "play_timer_settings_hex=001122\n"),
        "write effect self-check backup");

    result = run_self_check_for_test(
        &mem,
        "sc-effect",
        PTC_SELF_CHECK_PLAY_TIMER_EFFECT_PROBE,
        report,
        sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_PASS, "effect self-check passes");
    check_true(strstr(report, "PASS effect raw restored") != NULL, "effect self-check reports raw restore");
    check_true(strstr(report, "SUMMARY PASS") != NULL, "effect self-check reports pass summary");
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

static void test_companion_self_check_scans_large_event_log(void)
{
    PtcMemStorage mem;
    PtcSelfCheckResult result;
    FILE *file;
    char report[8192];
    int i;
    const char *app_root = "build/host/self_check_large_events_app";

    (void)mkdir("build", 0777);
    (void)mkdir("build/host", 0777);
    (void)mkdir(app_root, 0777);
    (void)mkdir("build/host/self_check_large_events_app/logs", 0777);
    file = fopen("build/host/self_check_large_events_app/logs/events.jsonl", "wb");
    check_true(file != NULL, "open large self-check events file");
    if (!file) {
        return;
    }
    for (i = 0; i < 220; ++i) {
        fprintf(file, "{\"ts\":1783526401,\"request_id\":\"noise-%04d\",\"type\":\"status\",\"event\":\"result_ok\",\"error\":\"ok\",\"detail\":\"padding-padding-padding-padding-padding-padding\"}\n", i);
    }
    fprintf(file, "{\"ts\":1783526401,\"request_id\":\"unknown\",\"type\":\"unknown\",\"event\":\"pctl_backup\",\"error\":\"ok\",\"detail\":\"\"}\n");
    fprintf(file, "{\"ts\":1783526401,\"request_id\":\"unknown\",\"type\":\"unknown\",\"event\":\"pctl_apply\",\"error\":\"ok\",\"detail\":\"\"}\n");
    fprintf(file, "{\"ts\":1783526401,\"request_id\":\"unknown\",\"type\":\"unknown\",\"event\":\"pctl_start_timer\",\"error\":\"ok\",\"detail\":\"\"}\n");
    fprintf(file, "{\"ts\":1783526401,\"request_id\":\"unknown\",\"type\":\"unknown\",\"event\":\"state_persisted\",\"error\":\"ok\",\"detail\":\"\"}\n");
    fclose(file);

    ptc_mem_storage_init(&mem);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "build/host/self_check_large_events_app/logs/events.jsonl", ""), "write dummy large events marker");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "build/host/self_check_large_events_app/state.json", "{\"version\":1,\"parent_unlock_until\":0,\"last_enforced_day_index\":2380,\"last_enforced_mode\":1,\"last_enforced_minutes\":60,\"updated_at\":1783526401}\n"), "write large events enforce state");

    result = ptc_self_check_run(&mem.storage, app_root, "", PTC_SELF_CHECK_ENFORCE_SNAPSHOT, NULL, report, sizeof(report));
    check_int(result.status, PTC_SELF_CHECK_PASS, "self-check scans large events file");
    check_true(strstr(report, "PASS enforce state_persisted event present") != NULL, "self-check large events evidence");
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
    char caps[384];
    snprintf(
        caps,
        sizeof(caps),
        "{\"version\":1,\"play_timer_write_verified\":%s,\"play_timer_write_backend\":\"pctl-s-v2\",\"play_timer_effect_verified\":%s,\"play_timer_effect_backend\":\"pctl-s-runtime-v2\",\"raw_block_verified\":%s,\"raw_block_backend\":\"pctl-s-rawblock-v1\",\"suspend_verified\":%s,\"suspend_backend\":\"pctl-s-suspend-v1\"}\n",
        play_timer_write_verified ? "true" : "false",
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

static void make_valid_v2_code(char out[PTC_TOKEN_V2_TEXT_SIZE], uint16_t nonce)
{
    check_int(ptc_token_v2_encode(5, nonce, "test-device", "test-secret", 2380, out), PTC_ERR_OK, "make v2 test token");
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

static void test_status_played_minutes(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char request[512];
    char result[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 30;
    pctl.model_elapsed_time = true;
    pctl.configured_minutes = 90;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "observe", false);
    (void)ptc_companion_status_request_json(request, sizeof(request), "status-played", 1000);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/status-played.json", request), "write played status request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process played status");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/status-played.json", result, sizeof(result)), "played status result written");
    check_true(strstr(result, "\"played_minutes_available\":true,\"played_minutes\":60") != NULL, "played status computes configured minus remaining");

    pctl.status.remaining_minutes = 100;
    (void)ptc_companion_status_request_json(request, sizeof(request), "status-played-clamped", 1001);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/status-played-clamped.json", request), "write clamped played status request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process clamped played status");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/status-played-clamped.json", result, sizeof(result)), "clamped played status result written");
    check_true(strstr(result, "\"played_minutes_available\":true,\"played_minutes\":0") != NULL, "played status clamps negative elapsed time");

    pctl.status.limited_today = false;
    pctl.status.unrestricted_today = true;
    (void)ptc_companion_status_request_json(request, sizeof(request), "status-played-unavailable", 1002);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/status-played-unavailable.json", request), "write unavailable played status request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process unavailable played status");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/status-played-unavailable.json", result, sizeof(result)), "unavailable played status result written");
    check_true(strstr(result, "\"played_minutes_available\":false,\"played_minutes\":-1") != NULL, "unlimited status leaves played time unavailable");
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
    check_true(strstr(result, "\"reason\":\"pctl_effect_not_verified\"") != NULL, "unprobed grant reason");
}

static void test_probe_play_timer_write_updates_capability(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char caps[1024];
    char backup[1024];
    char debug[4096];
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
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/backups/last_pctl_backup.txt", backup, sizeof(backup)), "probe backup readable");
    check_true(strstr(backup, "play_timer_settings_hex=") != NULL, "probe backup raw hex");
    check_true(strstr(backup, "play_timer_slots=") != NULL, "probe backup decoded slots");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/capabilities.json", caps, sizeof(caps)), "play write capabilities persisted");
    check_true(strstr(caps, "\"play_timer_write_verified\":true") != NULL, "play write capability true");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0028.json", result, sizeof(result)), "play write probe result");
    check_true(strstr(result, "\"play_timer_write_verified\":true") != NULL, "play write result capability");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/logs/pctl_debug.jsonl", debug, sizeof(debug)), "probe pctl debug readable");
    check_true(strstr(debug, "\"stage\":\"backup\"") != NULL, "probe debug backup stage");
    check_true(strstr(debug, "\"stage\":\"probe_play_timer_write\"") != NULL, "probe debug stage");
    check_true(strstr(debug, "\"before_raw_hex\"") != NULL, "probe debug before raw");
    check_true(strstr(debug, "\"after_slots\"") != NULL, "probe debug after slots");
}

static void test_probe_play_timer_effect_paths(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char result[8192];
    char caps[2048];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 30;
    pctl.status.play_timer_enabled = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/effect-fast.json", "{\"version\":1,\"request_id\":\"effect-fast\",\"type\":\"probe_play_timer_effect\",\"created_at\":1200,\"payload\":{\"wait_for_expiry\":false}}\n"), "write fast effect probe");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process fast effect probe");
    check_true(pctl.restore_called, "effect probe restores settings");
    check_true(!pctl.status.play_timer_enabled, "effect probe restores stopped timer");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/effect-fast.json", result, sizeof(result)), "effect result readable");
    check_true(strstr(result, "\"verdict\":\"pass\"") != NULL, "effect result passes");
    check_true(strstr(result, "\"opaque_snapshots\":{\"before_hex\":\"0101") != NULL, "effect result includes opaque snapshots");
    check_true(strstr(result, "1e00\",\"active_hex\"") != NULL, "effect result includes complete 68-byte snapshot");
    check_true(strstr(result, "\"raw_target_correct\":true") != NULL, "effect target raw changed");
    check_true(strstr(result, "\"raw_restored\":true") != NULL, "effect raw restored evidence");
    check_true(strstr(result, "\"timer_restored\":true") != NULL, "effect timer restored evidence");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/capabilities.json", caps, sizeof(caps)), "effect capabilities readable");
    check_true(strstr(caps, "\"play_timer_effect_verified\":true") != NULL, "effect capability persisted");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "effect probe consumes no nonce");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 1366;
    pctl.status.play_timer_enabled = true;
    pctl.model_elapsed_time = true;
    pctl.configured_minutes = 1400;
    pctl.played_minutes_today = 34;
    ptc_fake_time_init(&fake_time, 1785429261, 2403, 34);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/effect-elapsed.json", "{\"version\":1,\"request_id\":\"effect-elapsed\",\"type\":\"probe_play_timer_effect\",\"created_at\":1785429261,\"payload\":{\"wait_for_expiry\":false}}\n"), "write elapsed-time effect probe");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process elapsed-time effect probe");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/effect-elapsed.json", result, sizeof(result)), "elapsed-time effect result readable");
    check_true(strstr(result, "\"verdict\":\"pass\"") != NULL, "elapsed-time effect probe passes");
    check_true(strstr(result, "\"target_minutes\":1440") != NULL, "elapsed-time effect probe uses safe maximum target");
    check_true(strstr(result, "\"remaining_minutes\":1406") != NULL, "elapsed-time effect observes adjusted remaining time");
    check_true(!pctl.status.restricted_now, "elapsed-time effect probe avoids expiry");
    check_int(pctl.status.remaining_minutes, 1366, "elapsed-time effect restores remaining time");
    check_int(pctl.configured_minutes, 1400, "elapsed-time effect restores configured limit");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 1406;
    pctl.status.play_timer_enabled = true;
    pctl.model_elapsed_time = true;
    pctl.configured_minutes = 1440;
    pctl.played_minutes_today = 34;
    ptc_fake_time_init(&fake_time, 1785429261, 2403, 34);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/effect-alternate.json", "{\"version\":1,\"request_id\":\"effect-alternate\",\"type\":\"probe_play_timer_effect\",\"created_at\":1785429261,\"payload\":{\"wait_for_expiry\":false}}\n"), "write alternate-target effect probe");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process alternate-target effect probe");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/effect-alternate.json", result, sizeof(result)), "alternate-target effect result readable");
    check_true(strstr(result, "\"verdict\":\"pass\"") != NULL, "alternate-target effect probe passes");
    check_true(strstr(result, "\"target_minutes\":1430") != NULL, "unchanged maximum uses safe alternate target");
    check_int(pctl.configured_minutes, 1440, "alternate-target effect restores configured limit");
    check_int(pctl.status.remaining_minutes, 1406, "alternate-target effect restores remaining time");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 30;
    pctl.restore_error = PTC_ERR_PCTL_WRITE_FAILED;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/effect-restore-fail.json", "{\"version\":1,\"request_id\":\"effect-restore-fail\",\"type\":\"probe_play_timer_effect\",\"created_at\":1201,\"payload\":{}}\n"), "write restore failure effect probe");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process restore failure effect probe");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/effect-restore-fail.json", result, sizeof(result)), "restore failure result readable");
    check_true(strstr(result, "\"reason\":\"pctl_restore_failed\"") != NULL, "restore failure reason");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"), "restore failure creates disable flag");
}

static void test_probe_apply_today_limit_paths(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char result[8192];
    char backup[2048];
    char debug[8192];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "observe", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/probe-apply-observe.json", "{\"version\":1,\"request_id\":\"probe-apply-observe\",\"type\":\"probe_apply_today_limit\",\"created_at\":1101,\"payload\":{\"minutes\":1,\"start_timer\":true}}\n"), "write observe probe apply");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process observe probe apply");
    check_true(!pctl.applied, "observe probe apply avoids pctl write");
    check_true(!pctl.timer_started, "observe probe apply avoids start timer");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/backups/last_pctl_backup.txt"), "observe probe apply avoids backup");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "observe probe apply avoids nonce");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/probe-apply-observe.json", result, sizeof(result)), "observe probe apply result");
    check_true(strstr(result, "\"dry_run\":true") != NULL, "observe probe apply dry run");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/probe-apply-ok.json", "{\"version\":1,\"request_id\":\"probe-apply-ok\",\"type\":\"probe_apply_today_limit\",\"created_at\":1102,\"payload\":{\"minutes\":1,\"start_timer\":true}}\n"), "write grant probe apply");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process grant probe apply");
    check_true(pctl.applied, "grant probe apply writes pctl");
    check_true(pctl.timer_started, "grant probe apply starts timer");
    check_int(pctl.last_target.mode, PTC_PCTL_TARGET_LIMIT, "probe apply target mode");
    check_int(pctl.last_target.minutes, 1, "probe apply target minutes");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "probe apply never consumes nonce");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/backups/last_pctl_backup.txt", backup, sizeof(backup)), "probe apply backup readable");
    check_true(strstr(backup, "play_timer_settings_hex=") != NULL, "probe apply backup raw hex");
    check_true(strstr(backup, "play_timer_slots=") != NULL, "probe apply backup slots");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/probe-apply-ok.json", result, sizeof(result)), "probe apply result readable");
    check_true(strstr(result, "\"status\":\"ok\"") != NULL, "probe apply result ok");
    check_true(strstr(result, "\"probe_apply\"") != NULL, "probe apply result evidence object");
    check_true(strstr(result, "\"target_minutes\":1") != NULL, "probe apply result target minutes");
    check_true(strstr(result, "\"start_timer_called\":true") != NULL, "probe apply result start timer");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/logs/pctl_debug.jsonl", debug, sizeof(debug)), "probe apply debug readable");
    check_true(strstr(debug, "\"stage\":\"probe_apply_before\"") != NULL, "probe apply before debug");
    check_true(strstr(debug, "\"stage\":\"probe_apply_write\"") != NULL, "probe apply write debug");
    check_true(strstr(debug, "\"stage\":\"probe_apply_after\"") != NULL, "probe apply after debug");
    check_true(strstr(debug, "\"stage\":\"probe_apply_start_timer\"") != NULL, "probe apply start timer debug");
    check_true(strstr(debug, "\"target_minutes\":1") != NULL, "probe apply debug target minutes");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.write_error = PTC_ERR_PCTL_WRITE_FAILED;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/probe-apply-write-fail.json", "{\"version\":1,\"request_id\":\"probe-apply-write-fail\",\"type\":\"probe_apply_today_limit\",\"created_at\":1103,\"payload\":{\"minutes\":1,\"start_timer\":true}}\n"), "write failing probe apply");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process failing probe apply");
    check_true(!pctl.timer_started, "probe apply write failure avoids start timer");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "probe apply write failure avoids nonce");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/probe-apply-write-fail.json", result, sizeof(result)), "probe apply failure result");
    check_true(strstr(result, "\"reason\":\"pctl_write_failed\"") != NULL, "probe apply failure reason");
    check_true(strstr(result, "\"write_ipc_result\"") != NULL, "probe apply failure ipc result");
    check_true(strstr(result, "suspect_command_id_write_permission") != NULL, "probe apply failure hint");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/logs/pctl_debug.jsonl", debug, sizeof(debug)), "probe apply failure debug readable");
    check_true(strstr(debug, "\"stage\":\"probe_apply_write\"") != NULL, "probe apply failure write stage");
    check_true(strstr(debug, "\"error\":\"pctl_write_failed\"") != NULL, "probe apply failure debug error");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/flags/disable.flag", ""), "write disable flag for probe apply");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/probe-apply-disabled.json", "{\"version\":1,\"request_id\":\"probe-apply-disabled\",\"type\":\"probe_apply_today_limit\",\"created_at\":1104,\"payload\":{\"minutes\":1,\"start_timer\":true}}\n"), "write disabled probe apply");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process disabled probe apply");
    check_true(!pctl.applied, "disable flag blocks probe apply write");
    check_true(!pctl.timer_started, "disable flag blocks probe apply start timer");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/backups/last_pctl_backup.txt"), "disabled probe apply avoids backup");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/probe-apply-disabled.json", result, sizeof(result)), "disabled probe apply result");
    check_true(strstr(result, "\"reason\":\"disabled\"") != NULL, "disabled probe apply reason");
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
            "{\"version\":1,\"play_timer_write_verified\":true,\"play_timer_write_backend\":\"pctl-s-v1\","
            "\"play_timer_effect_verified\":true,\"play_timer_effect_backend\":\"pctl-s-runtime-v1\","
            "\"raw_block_verified\":false,\"suspend_verified\":false}\n"),
        "write legacy capability");
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0029", 1029, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0029.json", request), "write legacy capability grant");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process legacy capability grant");
    check_true(!pctl.applied, "legacy capability avoids pctl write");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0029.json", result, sizeof(result)), "legacy capability result");
    check_true(strstr(result, "\"reason\":\"pctl_effect_not_verified\"") != NULL, "legacy capability reason");
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
    char debug[4096];
    char rules[4096];

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
    check_true(pctl.timer_started, "offline grant starts timer");
    check_true(!pctl.status.restricted_now, "offline grant observes unrestricted runtime");
    check_int(pctl.last_target.mode, PTC_PCTL_TARGET_LIMIT, "grant target mode");
    /* Offline code stacks 30 minutes onto today's default weekday limit (60). */
    check_int(pctl.last_target.minutes, 90, "grant target minutes stack onto today limit");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/backups/last_pctl_backup.txt"), "backup persisted");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/ledger/used_nonces.jsonl", ledger, sizeof(ledger)), "ledger persisted");
    check_true(strstr(ledger, "\"day_index\":2380,\"nonce\":4660") != NULL, "nonce consumed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/rules.json", rules, sizeof(rules)), "rules persisted");
    check_true(strstr(rules, "\"today_override_present\":true") != NULL, "offline grant persists today override");
    check_true(strstr(rules, "\"today_override_minutes\":90") != NULL, "offline grant persists stacked minutes");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/logs/pctl_debug.jsonl", debug, sizeof(debug)), "grant pctl debug readable");
    check_true(strstr(debug, "\"stage\":\"apply_target\"") != NULL, "grant debug apply stage");
    check_true(strstr(debug, "\"target_mode\":\"limit\"") != NULL, "grant debug target mode");
    check_true(strstr(debug, "\"target_minutes\":90") != NULL, "grant debug target minutes");
    check_true(strstr(debug, "\"before_raw_hex\"") != NULL, "grant debug before raw");
    check_true(strstr(debug, "\"after_raw_hex\"") != NULL, "grant debug after raw");
}

static void test_v2_grant_replay_and_ledger_version(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    PtcTokenPayload v1_payload;
    char code[PTC_TOKEN_V2_TEXT_SIZE];
    char v1_code[PTC_TOKEN_TEXT_SIZE];
    char request[512];
    char ledger[4096];
    char result[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    make_valid_v2_code(code, 7);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "v2-grant", 1001, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/v2-grant.json", request), "write v2 grant request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process v2 grant");
    check_true(pctl.applied, "v2 grant applies pctl");
    check_int(pctl.last_target.minutes, 90, "v2 tier adds thirty minutes");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/ledger/used_nonces.jsonl", ledger, sizeof(ledger)), "v2 ledger readable");
    check_true(strstr(ledger, "\"day_index\":2380,\"nonce\":7,\"token_version\":2") != NULL, "v2 ledger records token version");

    v1_payload.version = 1;
    v1_payload.action = PTC_TOKEN_ACTION_ADD_TODAY_MINUTES;
    v1_payload.minutes = 5;
    v1_payload.day_index_since_2020 = 2380;
    v1_payload.nonce = 7;
    check_int(ptc_token_encode(&v1_payload, "test-device", "test-secret", v1_code), PTC_ERR_OK, "make v1 token sharing v2 nonce");
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "v1-same-nonce", 1002, v1_code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/v1-same-nonce.json", request), "write v1 with same nonce");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "v1 nonce namespace stays separate");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/v1-same-nonce.json", result, sizeof(result)), "v1 same nonce result");
    check_true(strstr(result, "\"status\":\"ok\"") != NULL, "v1 same numeric nonce is accepted");

    pctl.applied = false;
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "v2-replay", 1003, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/v2-replay.json", request), "write v2 replay request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process v2 replay");
    check_true(!pctl.applied, "v2 replay avoids pctl");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/v2-replay.json", result, sizeof(result)), "v2 replay result");
    check_true(strstr(result, "\"reason\":\"used_token\"") != NULL, "v2 replay has stable reason");
}

static void test_v2_failure_paths_do_not_consume_nonce(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_V2_TEXT_SIZE];
    char request[512];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.backup_error = PTC_ERR_PCTL_BACKUP_FAILED;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    make_valid_v2_code(code, 20);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "v2-backup-fail", 1, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/v2-backup-fail.json", request), "write v2 backup failure");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process v2 backup failure");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "v2 backup failure avoids nonce");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.write_error = PTC_ERR_PCTL_WRITE_FAILED;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    make_valid_v2_code(code, 21);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "v2-pctl-fail", 2, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/v2-pctl-fail.json", request), "write v2 pctl failure");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process v2 pctl failure");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "v2 pctl failure avoids nonce");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    make_valid_v2_code(code, 22);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "v2-result-fail", 3, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/v2-result-fail.json", request), "write v2 result failure");
    mem.fail_write_path_contains = "results/";
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process v2 result failure");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "v2 result failure avoids nonce");
}

static void test_v2_cooldown_persists_and_resets(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_V2_TEXT_SIZE];
    char bad_signature_code[PTC_TOKEN_V2_TEXT_SIZE];
    char v1_code[PTC_TOKEN_TEXT_SIZE];
    char request[512];
    char path[160];
    char state[1024];
    char result[4096];
    unsigned int index;

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    check_int(ptc_token_v2_encode(5, 40, "test-device", "wrong-secret", 2380, bad_signature_code), PTC_ERR_OK, "make bad signature v2 attempt");
    for (index = 0; index < 5; ++index) {
        char request_id[32];
        snprintf(request_id, sizeof(request_id), "v2-bad-%u", index);
        (void)ptc_companion_offline_code_request_json(request, sizeof(request), request_id, (int64_t)index,
            index == 4u ? bad_signature_code : "67108864");
        snprintf(path, sizeof(path), "app/inbox/pending/%s.json", request_id);
        check_true(mem.storage.vtable->write_text_atomic(&mem.storage, path, request), "write bad v2 attempt");
        check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process bad v2 attempt");
    }
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/state.json", state, sizeof(state)), "v2 cooldown state readable");
    check_true(strstr(state, "\"v2_failed_attempts\":5") != NULL, "v2 fifth failure persisted");
    check_true(strstr(state, "\"v2_cooldown_until\":1783527001") != NULL, "v2 cooldown deadline persisted");

    (void)ptc_companion_status_request_json(request, sizeof(request), "v2-cooldown-status", 10);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/v2-cooldown-status.json", request), "write status during cooldown");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "status unaffected by v2 cooldown");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/v2-cooldown-status.json", result, sizeof(result)), "cooldown status result");
    check_true(strstr(result, "\"status\":\"ok\"") != NULL, "status succeeds during v2 cooldown");

    make_valid_code(v1_code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "v1-during-v2-cooldown", 10, v1_code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/v1-during-v2-cooldown.json", request), "write v1 during v2 cooldown");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "v1 unaffected by v2 cooldown");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/v1-during-v2-cooldown.json", result, sizeof(result)), "v1 cooldown result");
    check_true(strstr(result, "\"status\":\"ok\"") != NULL, "v1 succeeds during v2 cooldown");

    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    make_valid_v2_code(code, 30);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "v2-still-cooling", 11, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/v2-still-cooling.json", request), "write v2 after restart");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "restart preserves v2 cooldown");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/v2-still-cooling.json", result, sizeof(result)), "cooldown rejection result");
    check_true(strstr(result, "\"reason\":\"code_cooldown\"") != NULL, "v2 cooldown stable reason");

    fake_time.snapshot.unix_seconds += 601;
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "v2-after-cooldown", 12, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/v2-after-cooldown.json", request), "write v2 after cooldown");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "v2 succeeds after cooldown");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/state.json", state, sizeof(state)), "v2 reset state readable");
    check_true(strstr(state, "\"v2_failed_attempts\":0") != NULL, "successful v2 clears failure count");
    check_true(strstr(state, "\"v2_cooldown_until\":0") != NULL, "successful v2 clears cooldown");
}

static void test_grant_requires_runtime_unlock_before_persisting(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_TEXT_SIZE];
    char request[512];
    char result[4096];
    char rules[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.restricted_now = true;
    pctl.runtime_effect_succeeds = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "grant-effect-missing", 1001, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/grant-effect-missing.json", request), "write missing effect grant");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process missing effect grant");
    check_true(pctl.timer_started, "missing effect path still starts timer");
    check_int(fake_time.slept_ms, 4750, "missing effect path polls for five seconds");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/grant-effect-missing.json", result, sizeof(result)), "missing effect result");
    check_true(strstr(result, "\"reason\":\"pctl_effect_not_observed\"") != NULL, "missing effect stable reason");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "missing effect avoids nonce");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/rules.json"), "missing effect avoids override persistence");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.start_timer_error = PTC_ERR_PCTL_WRITE_FAILED;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "grant-start-fail", 1001, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/grant-start-fail.json", request), "write start failure grant");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process start failure grant");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/grant-start-fail.json", result, sizeof(result)), "start failure result");
    check_true(strstr(result, "\"reason\":\"pctl_write_failed\"") != NULL, "start failure propagated");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "start failure avoids nonce");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/rules.json"), "start failure avoids override persistence");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.read_fails_after_apply = true;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "grant-read-fail", 1001, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/grant-read-fail.json", request), "write runtime read failure grant");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process runtime read failure grant");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/grant-read-fail.json", result, sizeof(result)), "runtime read failure result");
    check_true(strstr(result, "\"reason\":\"pctl_effect_not_observed\"") != NULL, "runtime read failure maps to effect not observed");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "runtime read failure avoids nonce");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/rules.json"), "runtime read failure avoids override persistence");

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.read_error = PTC_ERR_OK;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    check_true(
        mem.storage.vtable->write_text_atomic(
            &mem.storage,
            "app/rules.json",
            "{\"version\":1,\"week\":[{\"mode\":\"limit\",\"minutes\":75},{\"mode\":\"limit\",\"minutes\":75},"
            "{\"mode\":\"limit\",\"minutes\":75},{\"mode\":\"limit\",\"minutes\":75},{\"mode\":\"limit\",\"minutes\":75},"
            "{\"mode\":\"limit\",\"minutes\":75},{\"mode\":\"limit\",\"minutes\":75}],\"today_override_present\":false,"
            "\"today_override_day_index\":0,\"today_override_mode\":\"limit\",\"today_override_minutes\":60,\"bedtime_enabled\":false,"
            "\"bedtime_start_min\":1260,\"bedtime_end_min\":480,\"limit_action\":\"remind\"}\n"),
        "write result failure baseline rules");
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "grant-result-fail", 1001, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/grant-result-fail.json", request), "write result failure grant");
    mem.fail_write_path_contains = "results/";
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process result failure after observed unlock");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "result failure after unlock avoids nonce");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/rules.json", rules, sizeof(rules)), "result failure rules remain readable");
    check_true(strstr(rules, "\"today_override_present\":false") != NULL, "result failure restores original override state");
    check_true(strstr(rules, "\"minutes\":75") != NULL, "result failure restores original weekly target");
    mem.fail_write_path_contains = NULL;
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "grant-result-retry", 1002, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/grant-result-retry.json", request), "write retry after result failure");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "retry result failure grant");
    check_int(pctl.last_target.minutes, 105, "retry reuses original absolute target");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/rules.json", rules, sizeof(rules)), "retry rules readable");
    check_true(strstr(rules, "\"today_override_minutes\":105") != NULL, "retry persists grant exactly once");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "retry consumes nonce after result succeeds");
}

static void test_grant_offline_code_stacks_on_existing_limit(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_TEXT_SIZE];
    char request[512];
    char rules[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);

    /* Establish today's limit at 100 via a parent set_today_limit request. */
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0040.json", "{\"version\":1,\"request_id\":\"1000-0040\",\"type\":\"set_today_limit\",\"created_at\":1040,\"payload\":{\"minutes\":100}}\n"), "write set today 100");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process set today 100");
    check_int(pctl.last_target.minutes, 100, "today limit set to 100");

    /* Offline code adds 30 on top of the existing 100 -> 130, not overwrite to 30. */
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0041", 1041, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0041.json", request), "write offline stack request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process offline stack");
    check_int(pctl.last_target.mode, PTC_PCTL_TARGET_LIMIT, "offline stack target mode");
    check_int(pctl.last_target.minutes, 130, "offline stack adds onto existing 100");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/rules.json", rules, sizeof(rules)), "rules persisted after stack");
    check_true(strstr(rules, "\"today_override_minutes\":130") != NULL, "stacked minutes persisted");
}

static void test_grant_offline_code_clamps_to_daily_maximum(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_TEXT_SIZE];
    char request[512];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);

    /* Push today's limit near the single-day maximum. */
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0042.json", "{\"version\":1,\"request_id\":\"1000-0042\",\"type\":\"set_today_limit\",\"created_at\":1042,\"payload\":{\"minutes\":1430}}\n"), "write set today 1430");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process set today 1430");
    check_int(pctl.last_target.minutes, 1430, "today limit set to 1430");

    /* 1430 + 30 = 1460 clamps to 1440 so the PCTL write still succeeds. */
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0043", 1043, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0043.json", request), "write offline clamp request");
    pctl.applied = false;
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process offline clamp");
    check_true(pctl.applied, "offline clamp still applies pctl");
    check_int(pctl.last_target.minutes, 1440, "offline stack clamps to daily maximum");
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
    (void)ptc_sysmodule_scheduler_tick(&sysmodule, false);
    check_true(mem.storage.vtable->remove_path(&mem.storage, "app/flags/disable.flag"), "remove disable flag");
    (void)ptc_sysmodule_scheduler_tick(&sysmodule, false);
    check_true(pctl.applied, "file scan detects disable removal without waiting for minute boundary");
    check_true(pctl.timer_started, "disable removal triggers enforce reconciliation");
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
    char result[8192];
    char backup[1024];
    char events[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 60;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0007.json", "{\"version\":1,\"request_id\":\"1000-0007\",\"type\":\"probe_raw_block\",\"created_at\":1007,\"payload\":{}}\n"), "write probe request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process raw probe");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/capabilities.json", caps, sizeof(caps)), "capabilities persisted");
    check_true(strstr(caps, "\"raw_block_verified\":true") != NULL, "raw capability true");
    check_true(strstr(caps, "\"raw_block_backend\":\"pctl-s-rawblock-v1\"") != NULL, "raw capability backend");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0007.json", result, sizeof(result)), "probe result");
    check_true(strstr(result, "\"raw_block_verified\":true") != NULL, "probe result capability");
    check_true(strstr(result, "\"pctl_raw_block_probe\"") != NULL, "probe result evidence block");
    check_true(strstr(result, "\"verdict\":\"pass\"") != NULL, "probe verdict pass");
    check_true(strstr(result, "\"raw_target_written\":true") != NULL, "probe wrote raw target");
    check_true(strstr(result, "\"blocked_observed\":true") != NULL, "probe observed block");
    check_true(strstr(result, "\"raw_restored\":true") != NULL, "probe restored raw");
    check_true(strstr(result, "\"timer_restored\":true") != NULL, "probe restored timer");
    check_true(pctl.restore_called, "probe restored settings");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/backups/last_pctl_backup.txt", backup, sizeof(backup)), "probe wrote backup");
    check_true(strstr(backup, "play_timer_settings_hex=") != NULL, "backup has raw hex");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/logs/events.jsonl", events, sizeof(events)), "probe events");
    check_true(strstr(events, "raw_block_apply") != NULL, "probe apply event");
    check_true(strstr(events, "raw_block_restore") != NULL, "probe restore event");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"), "passing probe leaves no disable flag");
}

static void test_probe_raw_block_requires_effect_capability(void)
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
    write_default_files(&mem, "grant", true);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/probe-raw-gate.json", "{\"version\":1,\"request_id\":\"probe-raw-gate\",\"type\":\"probe_raw_block\",\"created_at\":1041,\"payload\":{}}\n"), "write gated raw probe");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process gated raw probe");
    check_true(!pctl.applied, "gated raw probe avoids pctl write");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/probe-raw-gate.json", result, sizeof(result)), "gated raw probe result");
    check_true(strstr(result, "\"reason\":\"pctl_write_not_verified\"") != NULL, "gated raw probe reason");
}

static void test_probe_raw_block_restore_failure_disables(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char caps[1024];
    char result[8192];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 60;
    pctl.restore_error = PTC_ERR_PCTL_WRITE_FAILED;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "grant", true);
    write_capabilities(&mem, true, false, false);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/probe-raw-restore.json", "{\"version\":1,\"request_id\":\"probe-raw-restore\",\"type\":\"probe_raw_block\",\"created_at\":1042,\"payload\":{}}\n"), "write restore-fail raw probe");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process restore-fail raw probe");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/probe-raw-restore.json", result, sizeof(result)), "restore-fail raw probe result");
    check_true(strstr(result, "\"reason\":\"pctl_restore_failed\"") != NULL, "restore-fail reason");
    check_true(strstr(result, "\"failure_stage\":\"restore\"") != NULL, "restore-fail stage");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"), "restore failure creates disable flag");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/capabilities.json", caps, sizeof(caps)), "restore-fail capabilities");
    check_true(strstr(caps, "\"raw_block_verified\":true") == NULL, "restore failure leaves raw capability unverified");
}

static void test_probe_raw_block_observe_is_dry_run(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char caps[1024];
    char result[8192];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "observe", true);
    write_capabilities(&mem, true, false, false);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/probe-raw-observe.json", "{\"version\":1,\"request_id\":\"probe-raw-observe\",\"type\":\"probe_raw_block\",\"created_at\":1043,\"payload\":{}}\n"), "write observe raw probe");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process observe raw probe");
    check_true(!pctl.applied, "observe raw probe avoids pctl write");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/probe-raw-observe.json", result, sizeof(result)), "observe raw probe result");
    check_true(strstr(result, "\"dry_run\":true") != NULL, "observe raw probe dry run");
    check_true(strstr(result, "\"verdict\":\"not_run\"") != NULL, "observe raw probe verdict");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/capabilities.json", caps, sizeof(caps)), "observe raw probe capabilities");
    check_true(strstr(caps, "\"raw_block_verified\":true") == NULL, "observe raw probe does not verify");
}

static void test_legacy_raw_block_capability_is_invalidated(void)
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
    write_default_files(&mem, "grant", true);
    /* A hand-edited or pre-backend capabilities file must not unlock block_today. */
    check_true(
        mem.storage.vtable->write_text_atomic(
            &mem.storage,
            "app/capabilities.json",
            "{\"version\":1,\"play_timer_write_verified\":true,\"play_timer_write_backend\":\"pctl-s-v2\","
            "\"play_timer_effect_verified\":true,\"play_timer_effect_backend\":\"pctl-s-runtime-v2\","
            "\"raw_block_verified\":true,\"suspend_verified\":true}\n"),
        "write backendless raw capability");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/legacy-raw.json", "{\"version\":1,\"request_id\":\"legacy-raw\",\"type\":\"block_today\",\"created_at\":1044,\"payload\":{}}\n"), "write legacy block today");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process legacy block today");
    check_true(!pctl.applied, "backendless raw capability avoids pctl write");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/legacy-raw.json", result, sizeof(result)), "legacy raw result");
    check_true(strstr(result, "\"reason\":\"raw_block_not_verified\"") != NULL, "legacy raw reason");
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
    check_true(strstr(rules, "\"suspend_backend\":\"pctl-s-suspend-v1\"") != NULL, "suspend capability backend");
}

static void test_legacy_suspend_capability_is_invalidated(void)
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
    write_default_files(&mem, "grant", true);
    /* Backendless suspend_verified must not unlock the suspend limit action. */
    check_true(
        mem.storage.vtable->write_text_atomic(
            &mem.storage,
            "app/capabilities.json",
            "{\"version\":1,\"play_timer_write_verified\":true,\"play_timer_write_backend\":\"pctl-s-v2\","
            "\"play_timer_effect_verified\":true,\"play_timer_effect_backend\":\"pctl-s-runtime-v2\","
            "\"raw_block_verified\":false,\"suspend_verified\":true}\n"),
        "write backendless suspend capability");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/legacy-suspend.json", "{\"version\":1,\"request_id\":\"legacy-suspend\",\"type\":\"set_limit_action\",\"created_at\":1045,\"payload\":{\"action\":\"suspend\"}}\n"), "write legacy suspend action");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process legacy suspend action");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/legacy-suspend.json", result, sizeof(result)), "legacy suspend result");
    check_true(strstr(result, "\"reason\":\"suspend_not_verified\"") != NULL, "legacy suspend reason");
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
    char debug[4096];

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
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/logs/pctl_debug.jsonl", debug, sizeof(debug)), "pctl failure debug readable");
    check_true(strstr(debug, "\"stage\":\"apply_target\"") != NULL, "pctl failure debug apply stage");
    check_true(strstr(debug, "\"error\":\"pctl_write_failed\"") != NULL, "pctl failure debug error");

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
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/rules.json"), "result failure removes newly created rules");

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

static void test_request_id_security_and_stem_match(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char result[4096];
    check_true(ptc_request_id_is_valid("Abc_09-x"), "request id allowed characters");
    check_true(!ptc_request_id_is_valid("../escape"), "request id traversal rejected");
    check_true(!ptc_request_id_is_valid("bad.id"), "request id dot rejected");
    check_true(!ptc_request_id_is_valid(""), "empty request id rejected");
    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    write_default_files(&mem, "observe", false);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/stem-good.json",
        "{\"version\":1,\"request_id\":\"embedded-other\",\"type\":\"status\",\"created_at\":1,\"payload\":{}}\n"), "write stem mismatch request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "stem mismatch archived");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/stem-good.json", result, sizeof(result)), "stem mismatch result uses safe stem");
    check_true(strstr(result, "bad_request") != NULL, "stem mismatch bad request");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/results/embedded-other.json"), "stem mismatch cannot select result path");
}

static void test_backoff_daily_logs_and_retention(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    const int64_t now = 1783526401;
    ptc_mem_storage_init(&mem);
    ptc_mem_storage_set_now(&mem, now);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&fake_time, now, 2380, 0);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    check_int(ptc_sysmodule_note_scan_result(&sysmodule, false), 1000, "backoff 1s");
    check_int(ptc_sysmodule_note_scan_result(&sysmodule, false), 2000, "backoff 2s");
    check_int(ptc_sysmodule_note_scan_result(&sysmodule, false), 5000, "backoff 5s");
    check_int(ptc_sysmodule_note_scan_result(&sysmodule, true), 500, "backoff resets");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/logs/2026-06-08/events.jsonl", "old"), "old dated log");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/logs/2026-06-09/events.jsonl", "keep"), "D-29 dated log");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/logs/unknown/file", "keep"), "unknown log dir");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/results/old.json", "{}"), "old result");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/done/future.json", "{}"), "future done");
    /* Retention reuses one entry array across the log scan and both dirs, so cover an
       actual deletion in inbox/done: it reads the array after two prior populations. */
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/done/old.json", "{}"), "old done");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/done/keep.json", "{}"), "D-29 done");
    ptc_mem_storage_set_mtime(&mem, "app/results/old.json", now - 31LL * 86400LL, true);
    ptc_mem_storage_set_mtime(&mem, "app/inbox/done/future.json", now + 86400LL, true);
    ptc_mem_storage_set_mtime(&mem, "app/inbox/done/old.json", now - 31LL * 86400LL, true);
    ptc_mem_storage_set_mtime(&mem, "app/inbox/done/keep.json", now - 29LL * 86400LL, true);
    check_int(ptc_sysmodule_cleanup(&sysmodule), 1, "retention cleanup succeeds");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/logs/2026-06-08/events.jsonl"), "D-30 log deleted");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/logs/2026-06-09/events.jsonl"), "D-29 log retained");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/results/old.json"), "D-30 result deleted");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/inbox/done/future.json"), "future done retained");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/inbox/done/old.json"), "D-30 done deleted");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/inbox/done/keep.json"), "D-29 done retained");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/logs/unknown/file"), "unknown log retained");
}

typedef struct { bool available; int submit_count; int event_state; PtcCompanionStatus submit_status; } FakeIpc;
static bool fake_ipc_connect(void *ctx) { return ((FakeIpc *)ctx)->available; }
static PtcCompanionStatus fake_ipc_submit(void *ctx, const char *request_id, const char *json, void **token)
{ (void)request_id; (void)json; ++((FakeIpc *)ctx)->submit_count; *token = ctx; return ((FakeIpc *)ctx)->submit_status; }
static int fake_ipc_event(void *ctx, void *token) { (void)token; return ((FakeIpc *)ctx)->event_state; }
static PtcCompanionStatus fake_ipc_result(void *ctx, const char *request_id, char *out, size_t size)
{ (void)ctx; (void)request_id; (void)out; (void)size; return PTC_COMPANION_PENDING; }
static void fake_ipc_close(void *ctx, void *token) { (void)ctx; (void)token; }
static bool fake_ipc_notify(void *ctx) { return ((FakeIpc *)ctx)->available; }

static void test_transport_fallback_does_not_resubmit(void)
{
    static const PtcCompanionIpcBackend BACKEND = {
        fake_ipc_connect, fake_ipc_submit, fake_ipc_event, fake_ipc_result, fake_ipc_close, fake_ipc_notify,
    };
    PtcMemStorage mem;
    PtcCompanionTransportClient client;
    FakeIpc ipc = { true, 0, -1, PTC_COMPANION_OK };
    char result[8192];
    const char *json = "{\"version\":1,\"request_id\":\"ipc-fallback\",\"type\":\"status\",\"created_at\":1,\"payload\":{}}\n";
    ptc_mem_storage_init(&mem);
    ptc_companion_transport_init(&client, "app", &mem.storage, &BACKEND, &ipc);
    check_int(ptc_companion_transport_submit_json(&client, "ipc-fallback", json), PTC_COMPANION_OK, "IPC submit accepted");
    check_int(ipc.submit_count, 1, "IPC submitted once");
    check_int(ptc_companion_transport_poll(&client, 100, 5000, result, sizeof(result)), PTC_COMPANION_PENDING, "invalid Event falls back to SD");
    check_int(ipc.submit_count, 1, "Event failure never resubmits");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/inbox/pending/ipc-fallback.json"), "accepted IPC fallback does not create another request");
    check_int(client.file_poll_delay_ms, 500, "file result polling backs off from 250ms");
}

static void test_transport_state_labels(void)
{
    static const PtcCompanionIpcBackend BACKEND = {
        fake_ipc_connect, fake_ipc_submit, fake_ipc_event, fake_ipc_result, fake_ipc_close, fake_ipc_notify,
    };
    PtcMemStorage mem;
    PtcCompanionTransportClient client;
    PtcOverlayBridge bridge;
    FakeIpc ipc = { true, 0, 0, PTC_COMPANION_OK };
    const char *json = "{\"version\":1,\"request_id\":\"state-ipc\",\"type\":\"status\",\"created_at\":1,\"payload\":{}}\n";
    ptc_mem_storage_init(&mem);
    ptc_companion_transport_init(&client, "app", &mem.storage, &BACKEND, &ipc);
    check_int(ptc_companion_transport_submit_json(&client, "state-ipc", json), PTC_COMPANION_OK,
        "transport state IPC submit succeeds");
    check_int(ptc_companion_transport_active(&client), PTC_TRANSPORT_IPC, "transport state reports IPC");
    check_true(ptc_companion_transport_accepted_by_ipc(&client), "transport state records IPC acceptance");
    check_int(ptc_companion_transport_route(&client), PTC_TRANSPORT_ROUTE_IPC, "transport route reports IPC");
    check_str(ptc_companion_transport_route_label_zh(ptc_companion_transport_route(&client)), "传输：IPC",
        "shared transport route provides IPC label");

    memset(&bridge, 0, sizeof(bridge));
    ptc_companion_transport_init(&bridge.transport, "app", &mem.storage, &BACKEND, &ipc);
    check_int(ptc_overlay_bridge_submit(&bridge, "01234567", 2, 1), PTC_COMPANION_OK,
        "overlay bridge IPC submit succeeds");
    check_int(ptc_overlay_bridge_transport_state(&bridge), PTC_TRANSPORT_ROUTE_IPC,
        "overlay bridge reports IPC transport");
    check_str(ptc_overlay_bridge_transport_label(&bridge), "传输：IPC", "overlay bridge provides IPC label");
    ipc.event_state = -1;
    check_int(ptc_overlay_bridge_poll(&bridge, 100, 5000), PTC_COMPANION_PENDING,
        "overlay bridge falls back to durable result polling");
    check_int(ptc_overlay_bridge_transport_state(&bridge), PTC_TRANSPORT_ROUTE_IPC_SD_RESULT,
        "overlay bridge reports IPC to SD result fallback");
    check_str(ptc_overlay_bridge_transport_label(&bridge), "传输：IPC → SD 结果回读",
        "overlay bridge provides IPC to SD result label");
    check_int(ipc.submit_count, 2, "overlay bridge fallback does not resubmit");
    bridge.waiting = false;
    check_int(ptc_overlay_bridge_transport_state(&bridge), PTC_TRANSPORT_ROUTE_IPC_SD_RESULT,
        "overlay bridge preserves completed transport route");

    ipc.available = false;
    ptc_companion_transport_cancel(&client);
    check_int(ptc_companion_transport_route(&client), PTC_TRANSPORT_ROUTE_IPC,
        "cancel preserves the most recent transport route");
    check_int(ptc_companion_transport_submit_json(&client, "state-sd", json), PTC_COMPANION_OK,
        "transport state SD submit succeeds");
    check_int(ptc_companion_transport_active(&client), PTC_TRANSPORT_FILE, "transport state reports SD queue");
    check_true(!ptc_companion_transport_accepted_by_ipc(&client), "SD queue is not marked IPC accepted");
    check_int(ptc_companion_transport_route(&client), PTC_TRANSPORT_ROUTE_SD_QUEUE, "transport route reports SD queue");
    check_str(ptc_companion_transport_route_label_zh(ptc_companion_transport_route(&client)), "传输：SD 文件队列",
        "shared transport route provides SD queue label");
    check_str(ptc_companion_transport_route_label_zh(PTC_TRANSPORT_ROUTE_LOCAL_SD_FLAG), "执行方式：本地 SD 标志文件",
        "shared transport route provides local SD flag label");
    check_str(ptc_companion_transport_route_label_zh(PTC_TRANSPORT_ROUTE_NONE), "传输：未开始",
        "shared transport route provides idle label");
}

static void test_companion_command_labels(void)
{
    static const struct {
        const char *type;
        const char *label;
    } CASES[] = {
        {"status", "刷新状态"},
        {"offline_code", "提交今日加时"},
        {"set_today_limit", "设置今日额度"},
        {"add_today_minutes", "临时加时"},
        {"disable_today_limit", "今日不限"},
        {"block_today", "今日禁玩"},
        {"restore_today_policy", "恢复周计划"},
        {"set_weekly_template", "每周计划"},
        {"set_bedtime", "就寝时间"},
        {"set_limit_action", "限制方式"},
        {"parent_unlock_start", "临时解锁"},
        {"parent_unlock_end", "结束解锁"},
        {"probe_play_timer_write", "验证计时器写入"},
        {"probe_play_timer_effect", "快速设备测试"},
        {"probe_apply_today_limit", "验证今日额度写入"},
        {"probe_raw_block", "验证强制阻止"},
        {"probe_suspend", "验证暂停软件"},
    };
    size_t index;
    for (index = 0; index < sizeof(CASES) / sizeof(CASES[0]); ++index) {
        check_str(ptc_companion_request_command_label_zh(CASES[index].type), CASES[index].label,
            "request type has stable Chinese command label");
    }
    check_str(ptc_companion_request_command_label_zh("future_request"), "后台操作",
        "unknown request type uses safe command label");
    check_str(ptc_companion_request_command_label_zh(NULL), "后台操作",
        "missing request type uses safe command label");
}

static void test_transport_submit_failure_does_not_write_file(void)
{
    static const PtcCompanionIpcBackend BACKEND = {
        fake_ipc_connect, fake_ipc_submit, fake_ipc_event, fake_ipc_result, fake_ipc_close, fake_ipc_notify,
    };
    PtcMemStorage mem;
    PtcCompanionTransportClient client;
    FakeIpc ipc = { true, 0, 0, PTC_COMPANION_BAD_ARGUMENT };
    const char *json = "{\"version\":1,\"request_id\":\"ipc-conflict\",\"type\":\"status\",\"created_at\":1,\"payload\":{}}\n";
    ptc_mem_storage_init(&mem);
    ptc_companion_transport_init(&client, "app", &mem.storage, &BACKEND, &ipc);
    check_int(ptc_companion_transport_submit_json(&client, "ipc-conflict", json), PTC_COMPANION_BAD_ARGUMENT,
        "IPC conflict is returned to caller");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/inbox/pending/ipc-conflict.json"),
        "IPC conflict never falls back to a file submit");

    ipc.submit_status = PTC_COMPANION_PENDING;
    check_int(ptc_companion_transport_submit_json(&client, "ipc-ambiguous", json), PTC_COMPANION_OK,
        "ambiguous IPC submit switches to result fallback");
    check_true(client.active == PTC_TRANSPORT_FILE && client.accepted_by_ipc,
        "ambiguous IPC submit only polls its durable result");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/inbox/pending/ipc-ambiguous.json"),
        "ambiguous IPC submit is not repeated through the file queue");
}

int main(void)
{
    test_token_v1();
    test_token_v2();
    test_overlay_input_and_shared_result_summary();
    test_time_and_policy();
    test_play_timer_settings_layout();
    test_companion_request_builder_and_file_protocol();
    test_companion_auth();
    test_result_validator();
    test_companion_self_check_observe_success();
    test_companion_self_check_forbidden_event_fails();
    test_companion_self_check_disabled_status();
    test_companion_self_check_play_write_probe();
    test_companion_self_check_play_timer_effect_probe();
    test_companion_self_check_missing_mismatch_and_pending_fail();
    test_companion_self_check_enforce_snapshot();
    test_companion_self_check_scans_large_event_log();
    test_observe_status_flow();
    test_status_played_minutes();
    test_observe_offline_code_allows_unrestricted_dry_run();
    test_grant_unrestricted_guard_rejects_without_nonce();
    test_grant_requires_play_timer_write_probe();
    test_probe_play_timer_write_updates_capability();
    test_probe_play_timer_effect_paths();
    test_probe_apply_today_limit_paths();
    test_legacy_play_timer_capability_is_invalidated();
    test_grant_flow_consumes_nonce_after_write();
    test_v2_grant_replay_and_ledger_version();
    test_v2_failure_paths_do_not_consume_nonce();
    test_v2_cooldown_persists_and_resets();
    test_grant_requires_runtime_unlock_before_persisting();
    test_grant_offline_code_stacks_on_existing_limit();
    test_grant_offline_code_clamps_to_daily_maximum();
    test_backup_failure_blocks_write();
    test_bad_request_schema_writes_error_result();
    test_observe_rule_request_is_dry_run();
    test_grant_set_today_limit_persists_applies_and_logs();
    test_probe_raw_block_updates_capability();
    test_probe_raw_block_requires_effect_capability();
    test_probe_raw_block_restore_failure_disables();
    test_probe_raw_block_observe_is_dry_run();
    test_legacy_raw_block_capability_is_invalidated();
    test_parent_unlock_state_and_expiry();
    test_more_rule_requests_and_probe_suspend();
    test_legacy_suspend_capability_is_invalidated();
    test_enforce_tick_applies_once_and_respects_disable_flag();
    test_failure_paths_do_not_consume_nonce();
    test_recover_processing();
    test_request_id_security_and_stem_match();
    test_backoff_daily_logs_and_retention();
    test_transport_fallback_does_not_resubmit();
    test_transport_state_labels();
    test_companion_command_labels();
    test_transport_submit_failure_does_not_write_file();

    if (failures != 0) {
        fprintf(stderr, "%d C host tests failed\n", failures);
        return 1;
    }
    printf("C host core tests passed\n");
    return 0;
}
