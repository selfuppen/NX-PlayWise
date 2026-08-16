#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../../common/version.h"
#include "../../common/policy/control_policy.h"
#include "../../common/protocol/request_schema.h"
#include "../../common/protocol/result_builder.h"
#include "../../common/support/support_export.h"
#include "../../common/security/credential_policy.h"
#include "../../third_party/qrcodegen/qrcodegen.h"
#include "../../common/time/ptc_time.h"
#include "../../common/rules/holiday_calendar.h"
#include "../../common/token/token_v1.h"
#include "../../common/token/token_v2.h"
#include "../../companion/auth.h"
#include "../../companion/album_restriction.h"
#include "../../companion/file_protocol.h"
#include "../../companion/result_summary.h"
#include "../../companion/overlay/bridge.h"
#include "../../companion/overlay/input_model.h"
#include "../../companion/overlay/layout.h"
#include "../../platform/host/fake_time.h"
#include "../../platform/host/mem_storage.h"
#include "../../platform/host/pctl_stub.h"
#include "../../platform/install_defaults.h"
#include "../../platform/switch/play_timer_settings_layout.h"
#include "../../sysmodule/sysmodule_core.h"

static int failures;

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

static bool fixed_random(uint8_t *out, size_t out_size, void *ctx)
{
    size_t index;
    uint8_t seed = *(uint8_t *)ctx;
    for (index = 0; index < out_size; ++index) out[index] = (uint8_t)(seed + index);
    return true;
}

static void test_tokens(void)
{
    PtcTokenPayload v1 = {1, PTC_TOKEN_ACTION_ADD_TODAY_MINUTES, 30, 2380, 4660};
    PtcTokenPayload decoded_v1;
    PtcTokenV2Payload decoded_v2;
    char long_code[PTC_TOKEN_TEXT_SIZE];
    char short_code[PTC_TOKEN_V2_TEXT_SIZE];
    bool consumed[PTC_TOKEN_V2_MAX_NONCE + 1U] = {false};
    bool issued[PTC_TOKEN_V2_MAX_NONCE + 1U] = {false};
    uint16_t selected_nonce = 0;

    check_int(ptc_token_encode(&v1, "test-device", "test-secret", long_code), PTC_ERR_OK, "v1 token encode");
    check_true(strcmp(long_code, "241W-2AC0-04HM-7YW5") == 0, "v1 fixture parity");
    check_int(ptc_token_verify(long_code, "test-device", "test-secret", 2380, 240, NULL, NULL, &decoded_v1),
        PTC_ERR_OK, "v1 token verify");
    check_int(decoded_v1.minutes, 30, "v1 token minutes");
    check_int(ptc_token_v2_encode(5, 7, "test-device", "test-secret", 2380, short_code),
        PTC_ERR_OK, "v2 token encode");
    check_true(strcmp(short_code, "10514680") == 0, "v2 eight-digit fixture parity");
    check_int(ptc_token_v2_verify(short_code, "test-device", "test-secret", 2380, 240, NULL, NULL, &decoded_v2),
        PTC_ERR_OK, "v2 token verify");
    check_int(decoded_v2.minutes, 30, "v2 token minutes");

    uint8_t t31;
    check_int(ptc_token_v2_tier_for_minutes(240, &t31), PTC_ERR_OK, "tier for 240 minutes");
    check_int(t31, 31, "tier 31 index");
    check_int(ptc_token_v2_encode(31, 7, "test-device", "test-secret", 2380, short_code), PTC_ERR_OK, "v2 tier 31 encode");
    check_int(ptc_token_v2_verify(short_code, "test-device", "test-secret", 2380, 240, NULL, NULL, &decoded_v2), PTC_ERR_OK, "v2 tier 31 verify");
    check_int(decoded_v2.minutes, 240, "v2 tier 31 decoded minutes");

    consumed[7] = true;
    issued[8] = true;
    check_true(ptc_token_v2_find_available_nonce(consumed, issued, 7, &selected_nonce),
               "nonce selection finds an unused issued slot");
    check_int(selected_nonce, 9, "nonce selection skips consumed and already-issued values");
    memset(consumed, 1, sizeof(consumed));
    check_true(!ptc_token_v2_find_available_nonce(consumed, issued, 0, &selected_nonce),
               "nonce selection refuses generation after all 512 values are unavailable");
}

static void test_release_request_contract(void)
{
    static const char *const removed_types[] = {
        "block_today", "set_bedtime", "set_limit_action", "parent_unlock_start", "parent_unlock_end",
        "probe_raw_block", "probe_suspend", "probe_play_timer_write", "probe_apply_today_limit",
        "probe_play_timer_effect", "prepare_device_test"
    };
    static const char *const release_requests[] = {
        "{\"version\":1,\"request_id\":\"status-1\",\"type\":\"status\",\"created_at\":1,\"payload\":{}}",
        "{\"version\":1,\"request_id\":\"preview-1\",\"type\":\"preview_offline_code\",\"created_at\":1,\"payload\":{\"code\":\"10514680\"}}",
        "{\"version\":1,\"request_id\":\"code-1\",\"type\":\"offline_code\",\"created_at\":1,\"payload\":{\"code\":\"10514680\"}}",
        "{\"version\":1,\"request_id\":\"limit-1\",\"type\":\"set_today_limit\",\"created_at\":1,\"payload\":{\"minutes\":60}}",
        "{\"version\":1,\"request_id\":\"add-1\",\"type\":\"add_today_minutes\",\"created_at\":1,\"payload\":{\"minutes\":15}}",
        "{\"version\":1,\"request_id\":\"unlimited-1\",\"type\":\"disable_today_limit\",\"created_at\":1,\"payload\":{}}",
        "{\"version\":1,\"request_id\":\"restore-1\",\"type\":\"restore_today_policy\",\"created_at\":1,\"payload\":{}}",
        "{\"version\":1,\"request_id\":\"setup-1\",\"type\":\"complete_setup\",\"created_at\":1,\"payload\":{}}",
        "{\"version\":1,\"request_id\":\"retry-1\",\"type\":\"retry_setup_release\",\"created_at\":1,\"payload\":{}}",
        "{\"version\":1,\"request_id\":\"snapshot-1\",\"type\":\"restore_install_snapshot\",\"created_at\":1,\"payload\":{}}"
    };
    PtcRequest request;
    char json[320];
    size_t index;

    for (index = 0; index < sizeof(release_requests) / sizeof(release_requests[0]); ++index) {
        check_int(ptc_request_parse(release_requests[index], &request), PTC_ERR_OK, "release request parses");
    }
    for (index = 0; index < sizeof(removed_types) / sizeof(removed_types[0]); ++index) {
        snprintf(json, sizeof(json),
            "{\"version\":1,\"request_id\":\"removed-%u\",\"type\":\"%s\",\"created_at\":1,\"payload\":{}}",
            (unsigned int)index, removed_types[index]);
        check_int(ptc_request_parse(json, &request), PTC_ERR_UNKNOWN_REQUEST_TYPE, "removed request is unreachable");
    }
    check_int(ptc_request_parse(
        "{\"version\":1,\"request_id\":\"weekly-blocked\",\"type\":\"set_weekly_template\",\"created_at\":1,"
        "\"payload\":{\"week\":[{\"mode\":\"blocked\",\"minutes\":0},{\"mode\":\"limit\",\"minutes\":60},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"unlimited\",\"minutes\":0}]}}", &request),
        PTC_ERR_BAD_REQUEST, "blocked weekly rule is rejected");
}

static void test_holiday_calendar_and_priority(void)
{
    PtcRules rules;
    PtcEffectiveRule effective;
    PtcRequest request;
    uint16_t holiday = 0;
    uint16_t makeup = 0;
    uint16_t next_makeup = 0;
    uint16_t ordinary = 0;
    uint16_t uncovered = 0;
    bool covered = false;
    PtcHolidayCalendarMatch match;
    check_true(ptc_day_index_from_date(2026, 10, 1, &holiday), "2026 National Day index converts");
    check_true(ptc_day_index_from_date(2026, 10, 10, &makeup), "2026 makeup day index converts");
    check_true(ptc_day_index_from_date(2026, 9, 20, &next_makeup), "2026 next makeup day index converts");
    check_true(ptc_day_index_from_date(2026, 8, 11, &ordinary), "2026 ordinary day index converts");
    check_true(ptc_day_index_from_date(2025, 12, 31, &uncovered), "2025 uncovered day index converts");
    check_int(ptc_holiday_calendar_classify(holiday, &covered), PTC_CALENDAR_DAY_STATUTORY_HOLIDAY,
        "National Day is classified as a statutory holiday");
    check_true(covered, "2026 is covered by the embedded calendar");
    check_int(ptc_holiday_calendar_classify(makeup, &covered), PTC_CALENDAR_DAY_MAKEUP_WORKDAY,
        "National Day makeup Saturday is classified as a workday");
    check_int(ptc_holiday_calendar_classify(ordinary, &covered), PTC_CALENDAR_DAY_ORDINARY,
        "ordinary covered date stays ordinary");
    check_int((long)ptc_holiday_calendar_arrangement_count(2026), 7,
        "calendar exposes seven authoritative 2026 holiday groups");
    check_true(ptc_holiday_calendar_arrangement(2026, 1) != NULL &&
        strcmp(ptc_holiday_calendar_arrangement(2026, 1)->display_name, "春节") == 0 &&
        strcmp(ptc_holiday_calendar_arrangement(2026, 1)->makeup_workdays, "2月14日、2月28日") == 0,
        "calendar arrangement carries display name and related makeup days");
    check_true(ptc_holiday_calendar_arrangement_count(2027) == 0 &&
        ptc_holiday_calendar_arrangement(2027, 0) == NULL,
        "uncovered year does not invent official arrangements");
    check_true(ptc_holiday_calendar_find(PTC_CALENDAR_DAY_STATUTORY_HOLIDAY, holiday, &match) &&
        match.day_index == holiday && match.arrangement != NULL &&
        strcmp(match.arrangement->display_name, "国庆节") == 0,
        "calendar query returns today's statutory holiday and authoritative name");
    check_true(ptc_holiday_calendar_find(PTC_CALENDAR_DAY_MAKEUP_WORKDAY, ordinary, &match) &&
        match.day_index == next_makeup && match.arrangement != NULL &&
        strcmp(match.arrangement->display_name, "中秋节") == 0,
        "calendar query returns the next makeup workday and related holiday name");
    check_true(!ptc_holiday_calendar_find(PTC_CALENDAR_DAY_STATUTORY_HOLIDAY, (uint16_t)(makeup + 1u), &match),
        "calendar query does not invent a holiday after the last embedded arrangement");
    check_true(!ptc_holiday_calendar_find(PTC_CALENDAR_DAY_STATUTORY_HOLIDAY, uncovered, &match),
        "calendar query does not cross from an uncovered year into embedded arrangements");

    ptc_rules_default(&rules);
    rules.holiday_enabled = true;
    rules.holiday_rule.mode = PTC_RULE_MODE_LIMIT;
    rules.holiday_rule.minutes = 180;
    rules.makeup_workday_rule.mode = PTC_RULE_MODE_LIMIT;
    rules.makeup_workday_rule.minutes = 45;
    effective = ptc_rules_resolve(&rules, holiday, ptc_weekday_from_day_index(holiday));
    check_int(effective.source, PTC_RULE_SOURCE_STATUTORY_HOLIDAY, "holiday rule overrides weekly plan");
    check_int(effective.rule.minutes, 180, "holiday quota is selected");
    effective = ptc_rules_resolve(&rules, makeup, ptc_weekday_from_day_index(makeup));
    check_int(effective.source, PTC_RULE_SOURCE_MAKEUP_WORKDAY, "makeup workday overrides weekend plan");
    check_int(effective.rule.minutes, 45, "makeup workday quota is selected");
    rules.today_override.present = true;
    rules.today_override.day_index = holiday;
    rules.today_override.rule.mode = PTC_RULE_MODE_LIMIT;
    rules.today_override.rule.minutes = 90;
    effective = ptc_rules_resolve(&rules, holiday, ptc_weekday_from_day_index(holiday));
    check_int(effective.source, PTC_RULE_SOURCE_TODAY_OVERRIDE, "today override remains highest priority");
    check_int(effective.rule.minutes, 90, "today override quota is preserved");

    check_int(ptc_request_parse(
        "{\"version\":1,\"request_id\":\"holiday-1\",\"type\":\"set_holiday_policy\",\"created_at\":1,"
        "\"payload\":{\"enabled\":true,\"holiday_rule\":{\"mode\":\"unlimited\",\"minutes\":0},"
        "\"makeup_workday_rule\":{\"mode\":\"limit\",\"minutes\":60}}}", &request),
        PTC_ERR_OK, "holiday policy request parses");
    check_true(request.holiday_enabled && request.makeup_workday_rule.minutes == 60,
        "holiday request fields map into the request model");
    check_int(ptc_request_parse(
        "{\"version\":1,\"request_id\":\"holiday-bad\",\"type\":\"set_holiday_policy\",\"created_at\":1,"
        "\"payload\":{\"enabled\":true,\"holiday_rule\":{\"mode\":\"limit\",\"minutes\":0},"
        "\"makeup_workday_rule\":{\"mode\":\"limit\",\"minutes\":60}}}", &request),
        PTC_ERR_BAD_REQUEST, "zero-minute limited holiday rule is rejected");
}

static void test_policy_and_disable_flag(void)
{
    PtcCapabilities caps = {0};
    PtcPolicyDecision decision;
    decision = ptc_policy_decide(PTC_CONTROL_ENFORCE, true, PTC_OPERATION_STATUS, &caps, false, true);
    check_int(decision.error, PTC_ERR_OK, "disable flag preserves status");
    check_true(decision.may_read_pctl && !decision.may_write_pctl, "disabled status remains read-only");
    decision = ptc_policy_decide(PTC_CONTROL_ENFORCE, true, PTC_OPERATION_SET_TODAY_LIMIT, &caps, false, true);
    check_int(decision.error, PTC_ERR_DISABLED, "disable flag blocks writes");
    decision = ptc_policy_decide(PTC_CONTROL_ENFORCE, false, PTC_OPERATION_GRANT_MINUTES, &caps, false, true);
    check_true(decision.may_write_pctl && decision.requires_backup && decision.consume_nonce_after_success,
        "offline grant is transactional");
}

static void test_support_redaction(void)
{
    size_t index;
    check_int((long)ptc_support_export_file_count(), 7, "support export uses a fixed allowlist");
    for (index = 0; index < ptc_support_export_file_count(); ++index) {
        const char *name = ptc_support_export_file(index);
        check_true(name && strcmp(name, "credentials.json") != 0 && strcmp(name, "auth.json") != 0,
            "support allowlist excludes credential files");
    }
    check_true(ptc_support_export_text_safe("{\"version\":1,\"device_id\":\"kid-switch\"}"),
        "ordinary support data is exportable");
    check_true(!ptc_support_export_text_safe("{\"grant_secret\":\"secret\"}"), "support rejects secret fields");
    check_true(!ptc_support_export_text_safe("{\"pin_hash\":\"hash\"}"), "support rejects PIN material");
    check_true(!ptc_support_export_text_safe("{\"nonce\":4660}"), "support rejects complete nonce fields");
    check_true(!ptc_support_export_text_safe("{\"offline_code\":\"10514680\"}"), "support rejects offline codes");
}

static void test_result_summary_unlimited_state(void)
{
    const char *result =
        "{\"version\":1,\"request_id\":\"status-unlimited\",\"type\":\"status\",\"status\":\"ok\","
        "\"state\":{\"day_index\":2380,\"limited_today\":0,\"blocked_today\":0,\"unrestricted_today\":1,"
        "\"remaining_available\":false,\"remaining_minutes\":-1,\"played_minutes_available\":false,"
        "\"played_minutes\":-1,\"play_timer_enabled\":0,\"restricted_now\":0},\"completed_at\":1}";
    PtcCompanionResultSummary summary;
    char formatted[256];

    check_true(ptc_companion_result_summary_parse(result, &summary), "unlimited result summary parses");
    check_int(summary.unrestricted_today, 1, "unlimited state reaches compact result summary");
    check_true(ptc_companion_result_summary_format(&summary, formatted, sizeof(formatted)) &&
        strstr(formatted, "今天还可玩：不限时"), "unlimited result summary uses the user-facing state");
}

static void test_install_defaults_preserve_runtime_data(void)
{
    static const char *const names[] = {
        "config.json", "auth.json", "rules.json", "state.json", "compatibility.json", "setup.json"
    };
    static const char *const defaults[] = {
        "{\"version\":1,\"device_id\":\"kid-switch\"}",
        "{\"version\":1,\"pin_hash\":\"\"}",
        "{\"version\":1,\"week\":[]}",
        "{\"version\":1,\"apply_status\":\"idle\"}",
        "{\"version\":1,\"status\":\"pending\"}",
        "{\"version\":1,\"phase\":\"unconfigured\"}",
    };
    PtcMemStorage mem;
    char path[160];
    char text[1024];
    size_t index;

    ptc_mem_storage_init(&mem);
    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        snprintf(path, sizeof(path), "app/defaults/%s", names[index]);
        check_true(mem.storage.vtable->write_text_atomic(&mem.storage, path, defaults[index]),
            "seed immutable install default");
    }
    check_true(ptc_install_materialize_defaults(&mem.storage, "app"),
        "fresh install materializes all runtime defaults");
    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        snprintf(path, sizeof(path), "app/%s", names[index]);
        check_true(mem.storage.vtable->read_text(&mem.storage, path, text, sizeof(text)) &&
            strcmp(text, defaults[index]) == 0, "materialized default matches immutable template");
    }

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/config.json", "not-json"),
        "seed malformed existing config");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/defaults/config.json", "replacement"),
        "change immutable config template");
    check_true(ptc_install_materialize_defaults(&mem.storage, "app"),
        "repeat initialization succeeds with existing runtime data");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/config.json", text, sizeof(text)) &&
        strcmp(text, "not-json") == 0, "existing malformed config is never overwritten");

    check_true(mem.storage.vtable->remove_path(&mem.storage, "app/auth.json"),
        "remove one runtime file for partial initialization");
    mem.fail_write_path_contains = "app/auth.json";
    check_true(!ptc_install_materialize_defaults(&mem.storage, "app"),
        "partial initialization reports an atomic write failure");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/auth.json"),
        "failed materialization does not create the missing runtime file");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/config.json", text, sizeof(text)) &&
        strcmp(text, "not-json") == 0, "failed materialization preserves existing runtime files");
    mem.fail_write_path_contains = NULL;
    check_true(ptc_install_materialize_defaults(&mem.storage, "app") &&
        mem.storage.vtable->exists(&mem.storage, "app/auth.json"),
        "partial initialization can be retried safely");
}

static void test_auth_and_queue(void)
{
    PtcMemStorage mem;
    PtcCompanionAuth auth;
    PtcCompanionFileClient client;
    uint8_t seed = 0x21;
    char request_id[PTC_COMPANION_REQUEST_ID_SIZE];
    char text[1024];
    char long_pin[PTC_AUTH_PIN_MAX_LEN + 2];
    int64_t retry_after = 0;
    int attempt;

    ptc_mem_storage_init(&mem);
    ptc_companion_auth_init(&auth, "app", &mem.storage);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/auth.json",
        "{\"version\":1,\"pin_hash\":\"\",\"pin_salt\":\"\",\"hash\":\"hmac-sha256\",\"updated_at\":0}"),
        "seed empty auth");
    check_int(ptc_companion_auth_state(&auth), PTC_AUTH_EMPTY, "fresh install starts with empty PIN state");
    check_int(ptc_companion_auth_set_pin(&auth, "110", 1, fixed_random, &seed), PTC_AUTH_OK,
              "onboarding default PIN can be created through the hashed auth path");
    check_int(ptc_companion_auth_verify_pin(&auth, "110", 2, &retry_after), PTC_AUTH_OK,
              "onboarding default PIN verifies without storing plaintext");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/auth.json", text, sizeof(text)) &&
               strstr(text, "\"110\"") == NULL && strstr(text, "pin_hash") != NULL,
               "default PIN plaintext is absent from auth storage");
    mem.fail_write_path_contains = "auth.json";
    check_int(ptc_companion_auth_set_pin(&auth, "110", 3, fixed_random, &seed), PTC_AUTH_WRITE_FAILED,
              "default PIN creation reports storage failure");
    mem.fail_write_path_contains = NULL;
    check_int(ptc_companion_auth_set_pin(&auth, "7", 1, fixed_random, &seed), PTC_AUTH_OK, "one-digit PIN accepted");
    check_int(ptc_companion_auth_verify_pin(&auth, "7", 100, &retry_after), PTC_AUTH_OK, "variable-length PIN verifies");
    check_int(ptc_companion_auth_set_pin(&auth, "12a", 1, fixed_random, &seed), PTC_AUTH_BAD_ARGUMENT, "non-digit PIN rejected");
    memset(long_pin, '8', PTC_AUTH_PIN_MAX_LEN);
    long_pin[PTC_AUTH_PIN_MAX_LEN] = '\0';
    check_int(ptc_companion_auth_set_pin(&auth, long_pin, 2, fixed_random, &seed), PTC_AUTH_OK, "64-digit PIN accepted");
    long_pin[PTC_AUTH_PIN_MAX_LEN] = '8';
    long_pin[PTC_AUTH_PIN_MAX_LEN + 1] = '\0';
    check_int(ptc_companion_auth_set_pin(&auth, long_pin, 2, fixed_random, &seed), PTC_AUTH_BAD_ARGUMENT, "65-digit PIN rejected");
    check_int(ptc_companion_auth_set_pin(&auth, "123456", 3, fixed_random, &seed), PTC_AUTH_OK, "six-digit PIN remains compatible");
    for (attempt = 0; attempt < PTC_AUTH_FAILURE_LIMIT - 1; ++attempt) {
        check_int(ptc_companion_auth_verify_pin(&auth, "000000", 100, &retry_after), PTC_AUTH_DENIED, "wrong PIN counted");
    }
    check_int(ptc_companion_auth_verify_pin(&auth, "000000", 100, &retry_after), PTC_AUTH_COOLDOWN, "fifth failure starts cooldown");
    check_int(retry_after, 30, "initial PIN cooldown is 30 seconds");
    check_int(ptc_companion_auth_verify_pin(&auth, "123456", 110, &retry_after), PTC_AUTH_COOLDOWN, "correct PIN waits for cooldown");
    check_int(ptc_companion_auth_verify_pin(&auth, "123456", 130, &retry_after), PTC_AUTH_OK, "successful PIN clears cooldown");

    ptc_companion_file_client_init(&client, "app", &mem.storage);
    check_int(ptc_companion_make_request_id(request_id, sizeof(request_id), 1000, 0x12), PTC_COMPANION_OK, "request id created");
    check_int(ptc_companion_submit_status(&client, request_id, 1), PTC_COMPANION_OK, "status queued atomically");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/inbox/pending/1000-0012.json"), "pending request visible");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/inbox/pending/1000-0012.json.tmp"), "temporary request hidden");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/inbox/pending/1000-0012.json", text, sizeof(text)) &&
        strstr(text, "\"type\":\"status\"") != NULL, "queued request has release type");
    check_int(ptc_companion_set_disable_flag(&client, true), PTC_COMPANION_OK, "emergency disable enabled");
    check_int(ptc_companion_set_disable_flag(&client, false), PTC_COMPANION_OK, "emergency disable cleared");
}

static void test_overlay_result_classification(void)
{
    PtcOverlayBridge bridge;
    memset(&bridge, 0, sizeof(bridge));
    bridge.summary.valid = true;
    bridge.summary.ok = true;
    snprintf(bridge.summary.type, sizeof(bridge.summary.type), "status");
    check_true(ptc_overlay_bridge_status_succeeded(&bridge), "overlay accepts a successful status refresh");
    check_true(!ptc_overlay_bridge_offline_code_succeeded(&bridge), "status refresh is not treated as code redemption");

    snprintf(bridge.summary.type, sizeof(bridge.summary.type), "offline_code");
    check_true(ptc_overlay_bridge_offline_code_succeeded(&bridge), "successful code result is accepted even at zero remaining");
    check_true(!ptc_overlay_bridge_status_succeeded(&bridge), "code redemption is not treated as status refresh");

    snprintf(bridge.summary.type, sizeof(bridge.summary.type), "preview_offline_code");
    bridge.summary.preview_available = true;
    check_true(ptc_overlay_bridge_preview_succeeded(&bridge), "preview result is classified separately");
    check_true(!ptc_overlay_bridge_offline_code_succeeded(&bridge), "preview never counts as redemption");
}

static void test_overlay_request_action_gate(void)
{
    check_true(ptc_overlay_request_action_enabled(false),
               "overlay enables request actions while idle");
    check_true(!ptc_overlay_request_action_enabled(true),
               "overlay blocks overlapping requests while preserving local input");
}

static void test_pending_redemption_recovery_marker(void)
{
    PtcMemStorage mem;
    PtcCompanionFileClient client;
    PtcPendingRedemption pending;
    PtcPendingRedemption loaded;
    bool found = false;
    char text[1024];
    ptc_mem_storage_init(&mem);
    ptc_companion_file_client_init(&client, "app", &mem.storage);
    memset(&pending, 0, sizeof(pending));
    snprintf(pending.request_id, sizeof(pending.request_id), "redeem-recovery-1");
    pending.confirmed_at = 100;
    pending.grant_minutes = 30;
    pending.before_remaining_available = true;
    pending.before_remaining_minutes = 20;
    pending.after_remaining_available = true;
    pending.after_remaining_minutes = 50;
    pending.effective_add_minutes = 30;
    check_int(ptc_companion_pending_redemption_save(&client, &pending), PTC_COMPANION_OK,
              "prepared redemption marker persisted");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/pending-redemption.json", text, sizeof(text)) &&
               strstr(text, "10514680") == NULL && strstr(text, "\"request_id\":\"redeem-recovery-1\"") != NULL,
               "redemption marker stores request identity without the code");
    check_int(ptc_companion_pending_redemption_load(&client, &loaded, &found), PTC_COMPANION_OK,
              "redemption marker reloads");
    check_true(found && loaded.grant_minutes == 30 && loaded.after_remaining_minutes == 50,
               "redemption preview snapshot survives restart");
    check_true(!ptc_companion_pending_redemption_has_submission(&client, &loaded),
               "prepared marker alone is not mistaken for submission");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/processing/redeem-recovery-1.json", "{}"),
               "seed durable request evidence");
    check_true(ptc_companion_pending_redemption_has_submission(&client, &loaded),
               "durable queue evidence recovers an interrupted acceptance reply");
    pending.submitted = true;
    check_int(ptc_companion_pending_redemption_save(&client, &pending), PTC_COMPANION_OK,
              "accepted redemption marker persisted");
    check_int(ptc_companion_pending_redemption_clear(&client), PTC_COMPANION_OK,
              "handled redemption marker cleared");
    found = true;
    check_int(ptc_companion_pending_redemption_load(&client, &loaded, &found), PTC_COMPANION_OK,
              "missing marker is a normal state");
    check_true(!found, "handled redemption is not recovered again");
}

static void test_overlay_layout_geometry(void)
{
    PtcOverlayRect submit = ptc_overlay_submit_rect(
        PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y, PTC_OVERLAY_CONTENT_W);
    PtcOverlayRect collapsed = ptc_overlay_status_rect(
        PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y, PTC_OVERLAY_CONTENT_W, false, false);
    PtcOverlayRect normal = ptc_overlay_status_rect(
        PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y, PTC_OVERLAY_CONTENT_W, true, false);
    PtcOverlayRect detail = ptc_overlay_status_rect(
        PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y, PTC_OVERLAY_CONTENT_W, true, true);
    int slot_group_width = PTC_OVERLAY_CODE_SYMBOLS * PTC_OVERLAY_SLOT_W +
        (PTC_OVERLAY_CODE_SYMBOLS - 1) * PTC_OVERLAY_SLOT_GAP;
    int slot_left = PTC_OVERLAY_CONTENT_X + (PTC_OVERLAY_CONTENT_W - slot_group_width) / 2;

    check_int(PTC_OVERLAY_CONTENT_Y, 90, "overlay content reclaims title whitespace");
    check_int(slot_group_width, 348, "overlay enlarged code slots fit as one group");
    check_int(slot_left - PTC_OVERLAY_CONTENT_X, 7, "overlay code slots are centered on the left");
    check_int(PTC_OVERLAY_CONTENT_X + PTC_OVERLAY_CONTENT_W - (slot_left + slot_group_width), 8,
        "overlay code slots are centered on the right");
    check_int(PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_SLOT_Y + PTC_OVERLAY_SLOT_H, 242,
        "overlay enlarged code slots end above input count");
    check_int(PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_KEYPAD_Y, 282,
        "overlay keypad leaves room below input count");
    check_int(ptc_overlay_key_rect(0, 0, 1).w, 105, "overlay keypad keys are wider");
    check_int(ptc_overlay_key_rect(0, 0, 1).h, 44, "overlay keypad rows have no dead vertical gap");
    check_int(ptc_overlay_refresh_rect(0, 0).h, 36, "overlay refresh target is easier to touch");
    check_true(ptc_overlay_rect_contains(submit, submit.x + submit.w - 1, submit.y + submit.h - 1),
        "overlay submit touch covers its visible right and bottom edges");
    check_true(!ptc_overlay_rect_contains(submit, submit.x + submit.w, submit.y),
        "overlay submit touch stops at its visible edge");
    check_true(!ptc_overlay_rect_contains(collapsed, collapsed.x + 1, collapsed.y + collapsed.h),
        "collapsed status has no invisible touch area");
    check_int(normal.h, PTC_OVERLAY_STATUS_NORMAL_H, "normal status drops duplicate detail rows");
    check_int(detail.y + detail.h, 646, "expanded error and success details fill the content area");
    check_true(detail.y + detail.h <= PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_CONTENT_H,
        "expanded overlay status remains inside content bounds");
    check_true(ptc_overlay_remaining_refresh_pending(true, true),
        "submitted overlay code uses a pending remaining-time presentation");
    check_true(!ptc_overlay_remaining_refresh_pending(true, false),
        "ordinary status refresh preserves the last remaining-time snapshot");
    check_true(!ptc_overlay_remaining_refresh_pending(false, true),
        "completed code result leaves the pending remaining-time presentation");
    check_int(ptc_overlay_preview_visual_level(true, 30, false, false),
        PTC_OVERLAY_PREVIEW_NEUTRAL, "ordinary overlay preview uses neutral styling");
    check_int(ptc_overlay_preview_visual_level(true, 30, true, false),
        PTC_OVERLAY_PREVIEW_WARNING, "capped overlay preview uses warning styling");
    check_int(ptc_overlay_preview_visual_level(false, 0, false, false),
        PTC_OVERLAY_PREVIEW_DANGER, "unknown overlay preview result uses danger styling");
    check_int(ptc_overlay_preview_visual_level(true, 0, false, false),
        PTC_OVERLAY_PREVIEW_DANGER, "zero overlay preview result uses danger styling");
    check_int(ptc_overlay_preview_visual_level(true, 30, false, true),
        PTC_OVERLAY_PREVIEW_DANGER, "unlimited conversion uses danger styling");
    check_int(ptc_overlay_preview_visual_level(true, 0, true, false),
        PTC_OVERLAY_PREVIEW_DANGER, "danger styling takes priority over a capped warning");
}

static void seed_release_setup(PtcMemStorage *mem)
{
    const char *rules =
        "{\"version\":1,\"week\":[{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"unlimited\",\"minutes\":0}],\"today_override_present\":false}";
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/config.json",
        "{\"version\":1,\"device_id\":\"kid-switch\",\"max_add_minutes\":240}"), "seed release config");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/credentials.json",
        "{\"version\":1,\"grant_secret\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}"),
        "seed generated credentials");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/auth.json",
        "{\"version\":1,\"pin_hash\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"pin_salt\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}"),
        "seed configured PIN");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/rules.json", rules), "seed weekly plan");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/setup.json",
        "{\"version\":1,\"phase\":\"unconfigured\",\"compatibility_status\":\"pending\",\"restriction_cleared\":false,"
        "\"snapshot_available\":false,\"activate_after\":0,\"last_error\":\"\"}"), "seed setup");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/build.json",
        "{\"playwise_version\":\"" PLAYWISE_VERSION "\",\"profile\":\"release\","
        "\"release_id\":\"playwise-" PLAYWISE_VERSION "+test\"}"), "seed build manifest");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/environment.json",
        "{\"read_ok\":true,\"hos\":\"22.5.0\",\"firmware_hash\":\"test-hash\",\"model\":\"mariko-oled\",\"atmosphere\":true}"),
        "seed verified environment");
}

static void test_setup_preflight_and_recovery(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char text[4096];
    char install_snapshot[4096];
    unsigned int apply_calls_after_activation;

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.model_elapsed_time = true;
    pctl.configured_minutes = 150;
    pctl.played_minutes_today = 30;
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 120;
    pctl.status.play_timer_enabled = true;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    seed_release_setup(&mem);

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/setup-1.json",
        "{\"version\":1,\"request_id\":\"setup-1\",\"type\":\"complete_setup\",\"created_at\":1,\"payload\":{}}"),
        "queue setup completion");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "complete setup processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/setup.json", text, sizeof(text)) &&
        strstr(text, "\"phase\":\"released\"") && strstr(text, "\"compatibility_status\":\"verified\"") &&
        strstr(text, "\"activate_after\":1783526406"), "preflight records verified environment and grace");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/backups/install_pctl_snapshot.json"), "installation snapshot persisted");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/backups/install_pctl_snapshot.json",
        install_snapshot, sizeof(install_snapshot)), "installation snapshot readable");
    check_true(pctl.status.unrestricted_today, "setup releases current restriction");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/rules.json", text, sizeof(text)) &&
        strstr(text, "\"today_override_present\":true") &&
        strstr(text, "\"today_override_day_index\":2380") &&
        strstr(text, "\"today_override_minutes\":150"),
        "fresh takeover persists the existing daily total instead of the default rule");

    fake_time.snapshot.unix_seconds = 1783526406;
    check_int(ptc_sysmodule_bootstrap_setup(&sysmodule), 1, "setup grace activates control");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/setup.json", text, sizeof(text)) &&
        strstr(text, "\"phase\":\"active\""), "setup becomes active");
    check_true(pctl.status.limited_today && pctl.status.remaining_minutes == 120 && !pctl.status.restricted_now,
        "handover keeps the original two-hour remaining allowance after activation");
    check_int(pctl.last_target.minutes, 150, "handover writes the daily total rather than remaining minutes");

    apply_calls_after_activation = pctl.apply_target_calls;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/setup-active-repeat.json",
        "{\"version\":1,\"request_id\":\"setup-active-repeat\",\"type\":\"complete_setup\",\"created_at\":2,\"payload\":{}}"),
        "queue repeated setup completion after activation");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "active setup completion is idempotent");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/setup-active-repeat.json", text, sizeof(text)) &&
        strstr(text, "\"status\":\"ok\"") && strstr(text, "\"phase\":\"active\"") &&
        strstr(text, "\"activate_after\":0"), "active setup retry returns current state without restarting grace");
    check_int((int)pctl.apply_target_calls, (int)apply_calls_after_activation,
        "active setup retry performs no PCTL target write");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/backups/install_pctl_snapshot.json", text, sizeof(text)) &&
        strcmp(text, install_snapshot) == 0, "active setup retry preserves installation snapshot");

    apply_calls_after_activation = pctl.apply_target_calls;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/holiday-policy.json",
        "{\"version\":1,\"request_id\":\"holiday-policy\",\"type\":\"set_holiday_policy\",\"created_at\":2,"
        "\"payload\":{\"enabled\":true,\"holiday_rule\":{\"mode\":\"unlimited\",\"minutes\":0},"
        "\"makeup_workday_rule\":{\"mode\":\"limit\",\"minutes\":45}}}"),
        "queue holiday policy update");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "holiday policy update is processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/rules.json", text, sizeof(text)) &&
        strstr(text, "\"holiday_enabled\":true") && strstr(text, "\"makeup_workday_minutes\":45"),
        "holiday policy persists without clearing today's override");
    check_true(pctl.apply_target_calls > apply_calls_after_activation,
        "holiday policy update immediately reapplies today's effective rule");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/holiday-policy.json", text, sizeof(text)) &&
        strstr(text, "\"rule_source\":\"today_override\"") &&
        strstr(text, "\"recovery_active\":false"),
        "holiday update reports the effective source without a committed recovery transaction");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/recovery/active/meta.json"),
        "holiday update clears its committed recovery transaction");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/flags/disable.flag", "startup_transaction_restored\n"),
        "write automatic disable flag while active");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/setup-active-disabled.json",
        "{\"version\":1,\"request_id\":\"setup-active-disabled\",\"type\":\"complete_setup\",\"created_at\":2,\"payload\":{}}"),
        "queue safe takeover while active and disabled");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "active disabled takeover is processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/setup-active-disabled.json", text, sizeof(text)) &&
        strstr(text, "\"status\":\"ok\"") && strstr(text, "\"phase\":\"released\""),
        "active disabled takeover reruns preflight and release");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"),
        "active disabled takeover clears flag only after preflight");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/backups/install_pctl_snapshot.json", text, sizeof(text)) &&
        strcmp(text, install_snapshot) == 0, "active disabled takeover preserves installation snapshot");
    fake_time.snapshot.unix_seconds += 5;
    check_int(ptc_sysmodule_bootstrap_setup(&sysmodule), 1, "active disabled takeover returns to active");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/flags/disable.flag", "emergency\n"), "write disable flag");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/holiday-disabled.json",
        "{\"version\":1,\"request_id\":\"holiday-disabled\",\"type\":\"set_holiday_policy\",\"created_at\":2,"
        "\"payload\":{\"enabled\":false,\"holiday_rule\":{\"mode\":\"limit\",\"minutes\":120},"
        "\"makeup_workday_rule\":{\"mode\":\"limit\",\"minutes\":60}}}"),
        "queue holiday policy while disabled");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "disabled holiday policy request is processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/holiday-disabled.json", text, sizeof(text)) &&
        strstr(text, "\"reason\":\"disabled\""), "disable flag rejects holiday policy writes");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/status-disabled.json",
        "{\"version\":1,\"request_id\":\"status-disabled\",\"type\":\"status\",\"created_at\":2,\"payload\":{}}"),
        "queue status under disable");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "status remains available under disable");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/status-disabled.json", text, sizeof(text)) &&
        strstr(text, "\"status\":\"ok\""), "disabled status query succeeds");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/restore-snapshot.json",
        "{\"version\":1,\"request_id\":\"restore-snapshot\",\"type\":\"restore_install_snapshot\",\"created_at\":3,\"payload\":{}}"),
        "queue restore under disable");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "restore remains available under disable");
    check_true(pctl.status.limited_today && pctl.status.remaining_minutes == 120, "installation snapshot restored exactly");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/setup.json", text, sizeof(text)) &&
        strstr(text, "\"phase\":\"restored\""), "snapshot restore records restored phase");

    pctl.read_error = PTC_ERR_PCTL_READ_FAILED;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/setup-preflight-failed.json",
        "{\"version\":1,\"request_id\":\"setup-preflight-failed\",\"type\":\"complete_setup\",\"created_at\":4,\"payload\":{}}"),
        "queue restored setup with failed preflight");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "restored setup preflight failure processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/setup-preflight-failed.json", text, sizeof(text)) &&
        strstr(text, "\"reason\":\"pctl_backup_failed\""), "restored setup reports preflight failure");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"),
        "failed restored preflight keeps emergency disable enabled");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/setup.json", text, sizeof(text)) &&
        strstr(text, "\"phase\":\"restored\""), "failed restored preflight keeps the resumable phase");

    pctl.read_error = PTC_ERR_OK;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/setup-restored.json",
        "{\"version\":1,\"request_id\":\"setup-restored\",\"type\":\"complete_setup\",\"created_at\":5,\"payload\":{}}"),
        "queue setup after snapshot restore");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "setup restarts after snapshot restore");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/setup-restored.json", text, sizeof(text)) &&
        strstr(text, "\"status\":\"ok\"") && strstr(text, "\"phase\":\"released\""),
        "restored setup returns to release grace");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"),
        "successful restored preflight clears emergency disable before takeover");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/backups/install_pctl_snapshot.json", text, sizeof(text)) &&
        strcmp(text, install_snapshot) == 0, "re-enable preserves the original installation snapshot");
    check_true(pctl.status.unrestricted_today, "re-enabled setup releases the restored restriction");

    fake_time.snapshot.unix_seconds += 5;
    check_int(ptc_sysmodule_bootstrap_setup(&sysmodule), 1, "re-enabled setup grace activates control");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/setup.json", text, sizeof(text)) &&
        strstr(text, "\"phase\":\"active\""), "re-enabled setup becomes active");
}

static void test_setup_refuses_unknown_handover_total(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char text[2048];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 120;
    pctl.status.play_timer_enabled = true;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    seed_release_setup(&mem);

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/setup-unknown-total.json",
        "{\"version\":1,\"request_id\":\"setup-unknown-total\",\"type\":\"complete_setup\",\"created_at\":1,\"payload\":{}}"),
        "queue setup with an unconvertible current allowance");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "unknown handover setup is processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/setup-unknown-total.json", text, sizeof(text)) &&
        strstr(text, "\"reason\":\"handover_state_unavailable\""),
        "unknown total reports a dedicated no-takeover error");
    check_int((int)pctl.apply_target_calls, 0, "unknown total leaves the current PCTL allowance untouched");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/setup.json", text, sizeof(text)) &&
        strstr(text, "\"phase\":\"protection\""), "unknown total enters retryable protection mode");
}

static void test_live_enforce_recovery_is_not_startup_recovery(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    PtcSysmodule restarted;
    char text[2048];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    seed_release_setup(&mem);
    check_int(ptc_sysmodule_bootstrap_setup(&sysmodule), 0, "initial bootstrap completes startup recovery scan");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/setup.json",
        "{\"version\":1,\"phase\":\"active\",\"compatibility_status\":\"verified\",\"restriction_cleared\":true,"
        "\"snapshot_available\":true,\"activate_after\":0,\"last_error\":\"\"}"), "seed active setup");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/compatibility.json",
        "{\"version\":1,\"status\":\"verified\",\"environment\":{\"hos\":\"22.5.0\","
        "\"firmware_hash\":\"test-hash\",\"model\":\"mariko-oled\",\"atmosphere\":true},"
        "\"release_id\":\"playwise-" PLAYWISE_VERSION "+test\",\"accepted_at\":1783526401}"),
        "seed accepted runtime fingerprint");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/state.json",
        "{\"version\":1,\"last_enforced_day_index\":0,\"last_enforced_mode\":0,\"last_enforced_minutes\":0,"
        "\"apply_status\":\"idle\",\"apply_pending_confirmation\":false,\"apply_confirmation_deadline\":0,"
        "\"pending_mode\":0,\"pending_minutes\":0,\"updated_at\":0}"), "seed enforce state");
    pctl.runtime_effect_succeeds = false;

    check_int(ptc_sysmodule_enforce_tick(&sysmodule), 1, "unobserved enforce enters pending confirmation");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/recovery/active/meta.json"),
        "pending enforce keeps live recovery transaction");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/state.json", text, sizeof(text)) &&
        strstr(text, "\"apply_pending_confirmation\":true"), "pending enforce state is persisted");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/status-live-recovery.json",
        "{\"version\":1,\"request_id\":\"status-live-recovery\",\"type\":\"status\",\"created_at\":1,\"payload\":{}}"),
        "queue status while enforce recovery is live");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "status reads the live enforce recovery");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/status-live-recovery.json", text, sizeof(text)) &&
        strstr(text, "\"recovery_active\":true"),
        "unrelated live recovery remains visible in status results");

    (void)ptc_sysmodule_scheduler_tick(&sysmodule, false);
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"),
        "scheduler does not classify live recovery as startup recovery");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/recovery/active/meta.json"),
        "scheduler preserves live recovery through confirmation window");

    ptc_sysmodule_init(&restarted, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    check_int(ptc_sysmodule_bootstrap_setup(&restarted), 1, "new sysmodule instance restores abandoned transaction");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"),
        "real restart creates startup transaction disable flag");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/recovery/active/meta.json"),
        "real restart clears restored transaction");
}

static void test_played_time_status(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char text[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.model_elapsed_time = false;
    pctl.status.unrestricted_today = true;
    pctl.status.play_timer_enabled = true;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    seed_release_setup(&mem);

    pctl.status.played_minutes_available = false;
    pctl.status.unrestricted_today = true;
    pctl.status.limited_today = false;
    pctl.status.remaining_available = false;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/status-unlimited-unknown.json",
        "{\"version\":1,\"request_id\":\"status-unlimited-unknown\",\"type\":\"status\",\"created_at\":2,\"payload\":{}}"),
        "queue unlimited status without direct spent time");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "unlimited status without direct spent time processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/status-unlimited-unknown.json", text, sizeof(text)) &&
        strstr(text, "\"played_minutes_available\":false") &&
        strstr(text, "\"played_minutes\":-1"),
        "unlimited status degrades when direct spent time is unavailable");

    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 30;
    pctl.status.configured_minutes_available = true;
    pctl.status.configured_minutes = 60;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/status-limited-fallback.json",
        "{\"version\":1,\"request_id\":\"status-limited-fallback\",\"type\":\"status\",\"created_at\":3,\"payload\":{}}"),
        "queue limited status for remaining-time fallback");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "limited status fallback processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/status-limited-fallback.json", text, sizeof(text)) &&
        strstr(text, "\"played_minutes_available\":true") &&
        strstr(text, "\"played_minutes\":30"),
        "limited status retains configured-minus-remaining fallback");
}

static void test_offline_code_preview_is_non_consuming(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char code[PTC_TOKEN_V2_TEXT_SIZE];
    char request[512];
    char result[4096];
    uint8_t tier = 0;
    unsigned int apply_calls;

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.model_elapsed_time = true;
    pctl.played_minutes_today = 20;
    pctl.status.limited_today = true;
    pctl.status.unrestricted_today = false;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 40;
    pctl.status.configured_minutes_available = true;
    pctl.status.configured_minutes = 60;
    pctl.status.play_timer_enabled = true;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    seed_release_setup(&mem);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/setup.json",
        "{\"version\":1,\"phase\":\"active\",\"compatibility_status\":\"verified\",\"restriction_cleared\":true,"
        "\"snapshot_available\":true,\"activate_after\":0,\"last_error\":\"\"}"), "seed active setup for preview");
    check_int(ptc_token_v2_tier_for_minutes(30, &tier), PTC_ERR_OK, "preview token tier selected");
    check_int(ptc_token_v2_encode(tier, 7, "kid-switch",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", 2380, code),
        PTC_ERR_OK, "preview token encoded");

    snprintf(request, sizeof(request),
        "{\"version\":1,\"request_id\":\"preview-code\",\"type\":\"preview_offline_code\","
        "\"created_at\":1,\"payload\":{\"code\":\"%s\"}}", code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage,
        "app/inbox/pending/preview-code.json", request), "queue code preview");
    apply_calls = pctl.apply_target_calls;
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "code preview processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/preview-code.json", result, sizeof(result)) &&
        strstr(result, "\"type\":\"preview_offline_code\"") &&
        strstr(result, "\"grant_minutes\":30") &&
        strstr(result, "\"remaining_after_minutes\":70"), "preview exposes current and estimated state");
    check_int((int)pctl.apply_target_calls, (int)apply_calls, "preview performs no PCTL write");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"),
        "preview does not consume nonce");

    snprintf(request, sizeof(request),
        "{\"version\":1,\"request_id\":\"redeem-code\",\"type\":\"offline_code\","
        "\"created_at\":2,\"payload\":{\"code\":\"%s\"}}", code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage,
        "app/inbox/pending/redeem-code.json", request), "queue confirmed redemption");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "confirmed code processed");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"),
        "confirmed successful redemption consumes nonce");
    check_true(pctl.apply_target_calls > apply_calls, "confirmed redemption writes PCTL");

    snprintf(request, sizeof(request),
        "{\"version\":1,\"request_id\":\"preview-used\",\"type\":\"preview_offline_code\","
        "\"created_at\":3,\"payload\":{\"code\":\"%s\"}}", code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage,
        "app/inbox/pending/preview-used.json", request), "queue used-code preview");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "used code preview processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/preview-used.json", result, sizeof(result)) &&
        strstr(result, "\"reason\":\"used_token\""), "used code is rejected before confirmation");
}

static void test_play_timer_layout(void)
{
    uint16_t words[PTC_PLAY_TIMER_SETTINGS_WORDS] = {
        0x0101U, 0x0001U, 0U, 0U, 0U, 0U, 0U,
        0x0600U, 0x0100U, 60U, 0U,
        0x0600U, 0x0100U, 60U, 0U,
        0x0600U, 0x0100U, 60U, 0U,
        0x0600U, 0x0100U, 60U, 0U,
        0x0600U, 0x0100U, 60U, 0U,
        0x0600U, 0x0100U, 60U, 0U,
        0x0600U, 0x0100U, 60U
    };
    uint16_t minutes = 0;
    check_true(ptc_play_timer_settings_valid(words, PTC_PLAY_TIMER_SETTINGS_WORDS), "0x44 PCTL layout accepted");
    check_true(ptc_play_timer_settings_get_minutes(words, PTC_PLAY_TIMER_SETTINGS_WORDS, 4, &minutes), "weekday minutes readable");
    check_int(minutes, 60, "weekday minutes preserve units");
    words[25] = 1500;
    check_true(!ptc_play_timer_settings_valid(words, PTC_PLAY_TIMER_SETTINGS_WORDS), "invalid PCTL layout rejected");
}

static void test_credential_policy(void)
{
    const uint8_t random_bytes[3] = {0xabU, 0xcdU, 0xefU};
    char device_id[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    char url[768];
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    memset(secret, 'a', 32U);
    secret[32] = '\0';
    check_true(ptc_device_id_valid("kid-switch"), "device ID accepts safe characters");
    check_true(!ptc_device_id_valid("kid switch"), "device ID rejects whitespace");
    check_true(ptc_grant_secret_valid(secret), "32-character secret accepted");
    check_true(ptc_grant_secret_is_demo(PTC_DEMO_GRANT_SECRET), "public demo secret recognized");
    check_true(ptc_random_device_id(random_bytes, device_id, sizeof(device_id)) &&
        strcmp(device_id, "playwise-abcdef") == 0, "random device ID is readable");
    check_true(ptc_build_pairing_url("kid-switch", secret, url, sizeof(url)) &&
        strstr(url, PTC_PAIRING_BASE_URL "#device_id=kid-switch&grant_secret=") == url,
        "pairing URL uses fragment fields");
    check_true(ptc_pairing_base_url_valid("https://parent.example/playwise?source=switch"), "custom HTTPS pairing base accepted");
    check_true(ptc_pairing_base_url_valid("http://192.168.1.8:8080/playwise"), "private HTTP pairing base accepted");
    check_true(!ptc_pairing_base_url_valid("http://example.com/playwise"), "public HTTP pairing base rejected");
    check_true(!ptc_pairing_base_url_valid("https://user:pass@example.com/playwise"), "pairing URL userinfo rejected");
    check_true(!ptc_pairing_base_url_valid("https://example.com/playwise#old"), "existing fragment rejected");
    check_true(!ptc_pairing_base_url_valid("https://:443/playwise"), "empty pairing host rejected");
    check_true(!ptc_pairing_base_url_valid("https://example.com:bad/playwise"), "invalid pairing port rejected");
    check_true(ptc_build_pairing_url_with_base("https://parent.example/app?source=switch", "kid-switch", secret, url, sizeof(url)) &&
        strstr(url, "https://parent.example/app?source=switch#device_id=kid-switch&grant_secret=") == url,
        "custom pairing URL preserves path and query before fragment");
    check_true(qrcodegen_encodeText(url, temp, qr, qrcodegen_Ecc_MEDIUM,
        qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true) &&
        qrcodegen_getSize(qr) > 0, "pairing URL encodes as QR");
}

static void test_album_restriction_transaction(void)
{
    PtcMemStorage mem;
    PtcAlbumRestrictionStatus status;
    char transformed[1024];
    char restored[1024];
    char error[160];
    const char *original = "[default_config]\r\nvalue=true\r\n[hbl_config]\r\noverride_any_app=true\r\n[other]\r\nkeep=yes\r\n";
    ptc_mem_storage_init(&mem);
    check_true(ptc_album_restriction_transform_ini(original, transformed, sizeof(transformed)),
               "album INI transforms safely");
    check_true(strstr(transformed, "[other]\r\nkeep=yes") != NULL &&
               strstr(transformed, "program_id_0=0100000000001003") != NULL &&
               strstr(transformed, "override_key_0=X") != NULL &&
               strstr(transformed, "override_key_0=!R+X") == NULL &&
               strstr(transformed, "override_any_app=true") == NULL,
               "controller INI preserves unrelated sections and replaces hbl config with a supported key");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, original),
               "seed Atmosphere override config");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_PACKAGE_PATH, "[package]\nname=Album\n"),
               "seed More Menu package");
    check_true(ptc_album_restriction_enable(&mem.storage, error, sizeof(error)), "album restriction enables transactionally");
    check_true(!mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_PACKAGE_PATH) &&
               mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_PACKAGE_BACKUP_PATH) &&
               mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_OVERRIDE_BACKUP_PATH) &&
               mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_STATE_PATH),
               "album files are backed up beside their originals");
    check_true(ptc_album_restriction_get_status(&mem.storage, &status) &&
               status.state == PTC_ALBUM_RESTRICTION_CONFIGURED && status.backup_valid,
               "configured album restriction is detected with valid backup");
    check_true(ptc_album_restriction_restore(&mem.storage, false, error, sizeof(error)), "album restriction restores backup");
    check_true(mem.storage.vtable->read_text(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, restored, sizeof(restored)) &&
               strcmp(restored, original) == 0 && mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_PACKAGE_PATH),
               "album restore is byte exact and restores removed package");
    check_true(ptc_album_restriction_get_status(&mem.storage, &status) &&
               status.state == PTC_ALBUM_RESTRICTION_OFF &&
               !mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_STATE_PATH) &&
               !mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_OVERRIDE_BACKUP_PATH) &&
               !mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_PACKAGE_BACKUP_PATH),
               "completed restore consumes adjacent recovery files and reports off");

    ptc_mem_storage_init(&mem);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, transformed),
               "seed externally configured album entry");
    check_true(ptc_album_restriction_get_status(&mem.storage, &status) &&
               status.state == PTC_ALBUM_RESTRICTION_EXTERNAL && !status.backup_valid,
               "matching external config without a PlayWise transaction is not an anomaly");
    check_true(!ptc_album_restriction_enable(&mem.storage, error, sizeof(error)) &&
               !mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_STATE_PATH),
               "external config is not adopted using a false pre-change backup");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage,
                   PTC_ALBUM_STATE_PATH, "invalid\n") &&
               ptc_album_restriction_get_status(&mem.storage, &status) &&
               status.state == PTC_ALBUM_RESTRICTION_ANOMALY,
               "configured PlayWise transaction with a missing backup remains anomalous");

    ptc_mem_storage_init(&mem);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, original),
               "seed rollback config");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_PACKAGE_PATH, "package"),
               "seed rollback package");
    mem.fail_rename_path_contains = "Photo Album/package.ini";
    check_true(!ptc_album_restriction_enable(&mem.storage, error, sizeof(error)), "package backup failure rejects album restriction");
    mem.fail_rename_path_contains = NULL;
    check_true(mem.storage.vtable->read_text(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, restored, sizeof(restored)) &&
               strcmp(restored, original) == 0 && mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_PACKAGE_PATH) &&
               !mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_STATE_PATH),
               "package backup failure rolls both files back");

    ptc_mem_storage_init(&mem);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, original) &&
               ptc_album_restriction_enable(&mem.storage, error, sizeof(error)), "seed configured conflict case");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, "[external]\nkeep=me\n"),
               "simulate external override edit");
    check_true(!ptc_album_restriction_restore(&mem.storage, false, error, sizeof(error)),
               "ordinary restore refuses external edits");
    check_true(ptc_album_restriction_restore(&mem.storage, true, error, sizeof(error)) &&
               mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_OVERRIDE_CONFLICT_PATH),
               "forced restore first preserves an external-edit rescue copy");

    ptc_mem_storage_init(&mem);
    check_true(ptc_album_restriction_enable(&mem.storage, error, sizeof(error)),
               "album entry supports both original files being absent");
    check_true(!mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_OVERRIDE_BACKUP_PATH) &&
               !mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_PACKAGE_BACKUP_PATH) &&
               ptc_album_restriction_restore(&mem.storage, false, error, sizeof(error)) &&
               !mem.storage.vtable->exists(&mem.storage, PTC_ALBUM_OVERRIDE_PATH),
               "restore removes a PlayWise-created override when no original existed");

    ptc_mem_storage_init(&mem);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, original) &&
               mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_BACKUP_PATH, "occupied") &&
               !ptc_album_restriction_enable(&mem.storage, error, sizeof(error)),
               "enable never overwrites an existing adjacent backup");

    ptc_mem_storage_init(&mem);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, original),
               "seed first-rename rollback case");
    mem.fail_rename_path_contains = "override_config.ini";
    check_true(!ptc_album_restriction_enable(&mem.storage, error, sizeof(error)),
               "first backup rename failure rejects enable");
    mem.fail_rename_path_contains = NULL;
    check_true(mem.storage.vtable->read_text(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, restored, sizeof(restored)) &&
               strcmp(restored, original) == 0,
               "first backup rename failure leaves the original in place");

    ptc_mem_storage_init(&mem);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, original) &&
               ptc_album_restriction_enable(&mem.storage, error, sizeof(error)),
               "seed corrupt backup case");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_BACKUP_PATH, "corrupt") &&
               ptc_album_restriction_get_status(&mem.storage, &status) &&
               status.state == PTC_ALBUM_RESTRICTION_ANOMALY &&
               !ptc_album_restriction_restore(&mem.storage, true, error, sizeof(error)),
               "corrupt adjacent backup blocks all restoration");

    ptc_mem_storage_init(&mem);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, original) &&
               ptc_album_restriction_enable(&mem.storage, error, sizeof(error)) &&
               mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_PATH, "[external]\n") &&
               mem.storage.vtable->write_text_atomic(&mem.storage, PTC_ALBUM_OVERRIDE_CONFLICT_PATH, "occupied") &&
               !ptc_album_restriction_restore(&mem.storage, true, error, sizeof(error)),
               "forced restore never overwrites an existing conflict rescue file");
}

static void test_expiry_without_restriction_is_reported(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char text[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.model_elapsed_time = true;
    pctl.configured_minutes = 60;
    pctl.played_minutes_today = 61;
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 0;
    pctl.status.play_timer_enabled = true;
    pctl.status.restricted_now = false;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    seed_release_setup(&mem);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/setup.json",
        "{\"version\":1,\"phase\":\"active\",\"compatibility_status\":\"verified\",\"restriction_cleared\":true,"
        "\"snapshot_available\":true,\"activate_after\":0,\"last_error\":\"\"}"),
        "seed active setup for expiry observation");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/rules.json",
        "{\"version\":1,\"week\":[{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60}],"
        "\"today_override_present\":true,\"today_override_day_index\":2380,"
        "\"today_override_mode\":\"limit\",\"today_override_minutes\":60}"),
        "seed expired effective rule");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/state.json",
        "{\"version\":1,\"last_enforced_day_index\":2380,\"last_enforced_mode\":1,\"last_enforced_minutes\":60,"
        "\"apply_status\":\"idle\",\"apply_pending_confirmation\":false,\"apply_confirmation_deadline\":0,"
        "\"pending_mode\":0,\"pending_minutes\":0,\"updated_at\":0}"),
        "seed enforced state before expiry anomaly");

    check_int(ptc_sysmodule_enforce_tick(&sysmodule), 1,
        "played-time overrun immediately confirms a missing expiry effect");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/state.json", text, sizeof(text)) &&
        strstr(text, "\"expiry_effect_failed\":true"),
        "missing expiry effect is persisted");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/support/recent-events.jsonl", text, sizeof(text)) &&
        strstr(text, "\"error\":\"pctl_effect_not_observed\"") &&
        strstr(text, "\"detail\":\"expiry_effect_not_observed\""),
        "missing expiry effect is recorded for support");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/status-expiry-failed.json",
        "{\"version\":1,\"request_id\":\"status-expiry-failed\",\"type\":\"status\",\"created_at\":1,\"payload\":{}}"),
        "queue status for confirmed expiry failure");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "confirmed expiry failure status is processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/status-expiry-failed.json", text, sizeof(text)) &&
        strstr(text, "\"status\":\"error\"") && strstr(text, "\"code\":306") &&
        strstr(text, "\"remaining_minutes\":0") && strstr(text, "\"restricted_now\":0"),
        "status reports 306 with the real exhausted but unrestricted state");

    pctl.status.restricted_now = true;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/status-expiry-recovered.json",
        "{\"version\":1,\"request_id\":\"status-expiry-recovered\",\"type\":\"status\",\"created_at\":2,\"payload\":{}}"),
        "queue status after the restriction effect appears");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "recovered expiry status is processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/status-expiry-recovered.json", text, sizeof(text)) &&
        strstr(text, "\"status\":\"ok\"") && strstr(text, "\"restricted_now\":1"),
        "observed restriction clears the expiry failure");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/state.json", text, sizeof(text)) &&
        strstr(text, "\"expiry_effect_failed\":false"),
        "recovered restriction clears persisted expiry markers");

    pctl.status.restricted_now = false;
    pctl.played_minutes_today = 60;
    check_int(ptc_sysmodule_enforce_tick(&sysmodule), 0,
        "zero remaining at the exact minute starts a grace window");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/state.json", text, sizeof(text)) &&
        strstr(text, "\"expiry_effect_pending_since\":1783526401") &&
        strstr(text, "\"expiry_effect_failed\":false"),
        "rounding grace is persisted without an early failure");
    fake_time.snapshot.unix_seconds += 89;
    check_int(ptc_sysmodule_enforce_tick(&sysmodule), 0, "expiry grace remains quiet before 90 seconds");
    fake_time.snapshot.unix_seconds += 1;
    check_int(ptc_sysmodule_enforce_tick(&sysmodule), 1, "expiry grace confirms failure at 90 seconds");
}

static void test_runtime_fingerprint_change_can_be_reconfirmed(void)
{
    PtcMemStorage mem;
    PtcPctlStub pctl;
    PtcFakeTime fake_time;
    PtcSysmodule sysmodule;
    char text[4096];

    ptc_mem_storage_init(&mem);
    ptc_pctl_stub_init(&pctl);
    pctl.status.unrestricted_today = true;
    pctl.status.play_timer_enabled = true;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    seed_release_setup(&mem);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/setup.json",
        "{\"version\":1,\"phase\":\"active\",\"compatibility_status\":\"verified\",\"restriction_cleared\":true,"
        "\"snapshot_available\":true,\"activate_after\":0,\"last_error\":\"\"}"),
        "seed active setup before runtime change");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/compatibility.json",
        "{\"version\":1,\"status\":\"accepted_unknown\",\"environment\":{\"hos\":\"20.5.0\","
        "\"firmware_hash\":\"old-hash\",\"model\":\"mariko-oled\",\"atmosphere\":true},"
        "\"release_id\":\"playwise-" PLAYWISE_VERSION "+test\",\"accepted_at\":1}"),
        "seed old accepted runtime fingerprint");

    check_int(ptc_sysmodule_bootstrap_setup(&sysmodule), -1, "runtime change enters protection");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/flags/disable.flag", text, sizeof(text)) &&
        strstr(text, "runtime_fingerprint_changed"), "runtime change records a specific disable reason");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/setup.json", text, sizeof(text)) &&
        strstr(text, "\"phase\":\"protection\""), "runtime change records protection phase");

    pctl.read_error = PTC_ERR_PCTL_READ_FAILED;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/reconfirm-runtime-failed.json",
        "{\"version\":1,\"request_id\":\"reconfirm-runtime-failed\",\"type\":\"complete_setup\","
        "\"created_at\":2,\"payload\":{}}"), "queue failed runtime recheck");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "failed runtime recheck request is processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/reconfirm-runtime-failed.json", text, sizeof(text)) &&
        strstr(text, "\"reason\":\"pctl_backup_failed\""), "failed runtime recheck reports preflight error");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"),
        "failed runtime recheck keeps control disabled");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/setup.json", text, sizeof(text)) &&
        strstr(text, "\"phase\":\"protection\""), "failed runtime recheck remains retryable");

    pctl.read_error = PTC_ERR_OK;
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/reconfirm-runtime.json",
        "{\"version\":1,\"request_id\":\"reconfirm-runtime\",\"type\":\"complete_setup\","
        "\"created_at\":2,\"payload\":{}}"), "queue parent-confirmed runtime recheck");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "runtime recheck request is processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/reconfirm-runtime.json", text, sizeof(text)) &&
        strstr(text, "\"status\":\"ok\"") && strstr(text, "\"phase\":\"released\""),
        "successful runtime recheck resumes takeover grace");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"),
        "successful runtime recheck clears its disable flag");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/compatibility.json", text, sizeof(text)) &&
        strstr(text, "\"hos\":\"22.5.0\"") && strstr(text, "\"status\":\"verified\""),
        "successful runtime recheck records the current fingerprint");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/rules.json", text, sizeof(text)) &&
        strstr(text, "\"today_override_present\":false"),
        "runtime reconfirmation preserves the configured rules without recapturing today");

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/setup.json",
        "{\"version\":1,\"phase\":\"protection\",\"compatibility_status\":\"protection\","
        "\"restriction_cleared\":false,\"snapshot_available\":true,\"activate_after\":0,"
        "\"last_error\":\"recovery_failed\"}"), "seed unrelated protection state");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/flags/disable.flag",
        "transaction_restore_failed\n"), "seed unrelated disable reason");
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/reconfirm-refused.json",
        "{\"version\":1,\"request_id\":\"reconfirm-refused\",\"type\":\"complete_setup\","
        "\"created_at\":3,\"payload\":{}}"), "queue unsafe protection takeover");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "unsafe protection takeover is processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/reconfirm-refused.json", text, sizeof(text)) &&
        strstr(text, "\"reason\":\"disabled\""), "unrelated protection reason remains blocked");
    check_true(mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"),
        "unrelated protection reason remains disabled");
}

int main(void)
{
    test_result_summary_unlimited_state();
    test_tokens();
    test_release_request_contract();
    test_holiday_calendar_and_priority();
    test_policy_and_disable_flag();
    test_support_redaction();
    test_install_defaults_preserve_runtime_data();
    test_auth_and_queue();
    test_overlay_result_classification();
    test_overlay_request_action_gate();
    test_pending_redemption_recovery_marker();
    test_overlay_layout_geometry();
    test_setup_preflight_and_recovery();
    test_setup_refuses_unknown_handover_total();
    test_runtime_fingerprint_change_can_be_reconfirmed();
    test_live_enforce_recovery_is_not_startup_recovery();
    test_played_time_status();
    test_expiry_without_restriction_is_reported();
    test_offline_code_preview_is_non_consuming();
    test_play_timer_layout();
    test_credential_policy();
    test_album_restriction_transaction();
    if (failures) {
        fprintf(stderr, "%d C host tests failed\n", failures);
        return 1;
    }
    puts("C host release tests passed");
    return 0;
}
