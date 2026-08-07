#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../../common/policy/control_policy.h"
#include "../../common/protocol/request_schema.h"
#include "../../common/protocol/result_builder.h"
#include "../../common/support/support_export.h"
#include "../../common/time/ptc_time.h"
#include "../../common/token/token_v1.h"
#include "../../common/token/token_v2.h"
#include "../../companion/auth.h"
#include "../../companion/file_protocol.h"
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

    check_int(ptc_token_encode(&v1, "test-device", "test-secret", long_code), PTC_ERR_OK, "v1 token encode");
    check_true(strcmp(long_code, "241W-2AC0-04HM-7YW5") == 0, "v1 fixture parity");
    check_int(ptc_token_verify(long_code, "test-device", "test-secret", 2380, 120, NULL, NULL, &decoded_v1),
        PTC_ERR_OK, "v1 token verify");
    check_int(decoded_v1.minutes, 30, "v1 token minutes");
    check_int(ptc_token_v2_encode(5, 7, "test-device", "test-secret", 2380, short_code),
        PTC_ERR_OK, "v2 token encode");
    check_true(strcmp(short_code, "10514680") == 0, "v2 eight-digit fixture parity");
    check_int(ptc_token_v2_verify(short_code, "test-device", "test-secret", 2380, 120, NULL, NULL, &decoded_v2),
        PTC_ERR_OK, "v2 token verify");
    check_int(decoded_v2.minutes, 30, "v2 token minutes");
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

    ptc_mem_storage_init(&mem);
    ptc_companion_auth_init(&auth, "app", &mem.storage);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/auth.json",
        "{\"version\":1,\"pin_hash\":\"\",\"pin_salt\":\"\",\"hash\":\"hmac-sha256\",\"updated_at\":0}"),
        "seed empty auth");
    check_int(ptc_companion_auth_set_pin(&auth, "12345", 1, fixed_random, &seed), PTC_AUTH_BAD_ARGUMENT, "five-digit PIN rejected");
    check_int(ptc_companion_auth_set_pin(&auth, "123456", 1, fixed_random, &seed), PTC_AUTH_OK, "six-digit PIN accepted");
    check_int(ptc_companion_auth_verify_pin(&auth, "123456"), PTC_AUTH_OK, "PIN verifies");

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

static void seed_release_setup(PtcMemStorage *mem)
{
    const char *rules =
        "{\"version\":1,\"week\":[{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"unlimited\",\"minutes\":0}],\"today_override_present\":false}";
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/config.json",
        "{\"version\":1,\"device_id\":\"kid-switch\",\"max_add_minutes\":120}"), "seed release config");
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
        "{\"playwise_version\":\"0.1.0\",\"profile\":\"release\",\"release_id\":\"playwise-0.1.0+test\"}"), "seed build manifest");
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
    check_true(pctl.status.unrestricted_today, "setup releases current restriction");

    fake_time.snapshot.unix_seconds = 1783526406;
    check_int(ptc_sysmodule_bootstrap_setup(&sysmodule), 1, "setup grace activates control");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/setup.json", text, sizeof(text)) &&
        strstr(text, "\"phase\":\"active\""), "setup becomes active");

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

int main(void)
{
    test_tokens();
    test_release_request_contract();
    test_policy_and_disable_flag();
    test_support_redaction();
    test_auth_and_queue();
    test_setup_preflight_and_recovery();
    test_play_timer_layout();
    if (failures) {
        fprintf(stderr, "%d C host tests failed\n", failures);
        return 1;
    }
    puts("C host release tests passed");
    return 0;
}
