#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../../common/policy/control_policy.h"
#include "../../common/protocol/request_schema.h"
#include "../../common/protocol/result_builder.h"
#include "../../common/support/support_export.h"
#include "../../common/security/credential_policy.h"
#include "../../third_party/qrcodegen/qrcodegen.h"
#include "../../common/time/ptc_time.h"
#include "../../common/token/token_v1.h"
#include "../../common/token/token_v2.h"
#include "../../companion/auth.h"
#include "../../companion/file_protocol.h"
#include "../../companion/overlay/bridge.h"
#include "../../companion/overlay/input_model.h"
#include "../../companion/overlay/layout.h"
#include "../../platform/host/fake_time.h"
#include "../../platform/host/mem_storage.h"
#include "../../platform/host/pctl_stub.h"
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
    check_true(ptc_overlay_rect_contains(submit, submit.x + submit.w - 1, submit.y + submit.h - 1),
        "overlay submit touch covers its visible right and bottom edges");
    check_true(!ptc_overlay_rect_contains(submit, submit.x + submit.w, submit.y),
        "overlay submit touch stops at its visible edge");
    check_true(!ptc_overlay_rect_contains(collapsed, collapsed.x + 1, collapsed.y + collapsed.h),
        "collapsed status has no invisible touch area");
    check_int(normal.h, PTC_OVERLAY_STATUS_NORMAL_H, "normal status drops duplicate detail rows");
    check_int(detail.y + detail.h, 626, "expanded error and success details stay above the footer");
    check_true(detail.y + detail.h <= PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_CONTENT_H,
        "expanded overlay status remains inside content bounds");
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
        "{\"playwise_version\":\"0.1.3\",\"profile\":\"release\",\"release_id\":\"playwise-0.1.3+test\"}"), "seed build manifest");
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
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 30;
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

    fake_time.snapshot.unix_seconds = 1783526406;
    check_int(ptc_sysmodule_bootstrap_setup(&sysmodule), 1, "setup grace activates control");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/setup.json", text, sizeof(text)) &&
        strstr(text, "\"phase\":\"active\""), "setup becomes active");

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
    check_true(pctl.status.limited_today && pctl.status.remaining_minutes == 30, "installation snapshot restored exactly");
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
        "\"release_id\":\"playwise-0.1.3+test\",\"accepted_at\":1783526401}"),
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
    pctl.model_elapsed_time = true;
    pctl.played_minutes_today = 37;
    pctl.status.unrestricted_today = true;
    pctl.status.play_timer_enabled = true;
    ptc_fake_time_init(&fake_time, 1783526401, 2380, 720);
    ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &fake_time.provider);
    seed_release_setup(&mem);

    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/status-unlimited-played.json",
        "{\"version\":1,\"request_id\":\"status-unlimited-played\",\"type\":\"status\",\"created_at\":1,\"payload\":{}}"),
        "queue unlimited status with direct spent time");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "unlimited status with direct spent time processed");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/status-unlimited-played.json", text, sizeof(text)) &&
        strstr(text, "\"unrestricted_today\":1") &&
        strstr(text, "\"remaining_available\":false") &&
        strstr(text, "\"played_minutes_available\":true") &&
        strstr(text, "\"played_minutes\":37"),
        "unlimited status exposes direct spent time");

    pctl.model_elapsed_time = false;
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

int main(void)
{
    test_tokens();
    test_release_request_contract();
    test_policy_and_disable_flag();
    test_support_redaction();
    test_auth_and_queue();
    test_overlay_result_classification();
    test_pending_redemption_recovery_marker();
    test_overlay_layout_geometry();
    test_setup_preflight_and_recovery();
    test_live_enforce_recovery_is_not_startup_recovery();
    test_played_time_status();
    test_offline_code_preview_is_non_consuming();
    test_play_timer_layout();
    test_credential_policy();
    if (failures) {
        fprintf(stderr, "%d C host tests failed\n", failures);
        return 1;
    }
    puts("C host release tests passed");
    return 0;
}
