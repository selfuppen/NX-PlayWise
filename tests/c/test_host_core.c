#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../common/policy/control_policy.h"
#include "../../common/protocol/error_code.h"
#include "../../common/time/ptc_time.h"
#include "../../common/token/token_v1.h"
#include "../../companion/request_client.h"
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
    check_str(code, "241W2-AC004-HM7YW-51R84", "token fixture parity");
    check_int(ptc_token_verify(code, "test-device", "test-secret", 2380, 120, NULL, NULL, &decoded), PTC_ERR_OK, "token verify ok");
    check_int(decoded.minutes, 30, "decoded minutes");
    check_int(decoded.nonce, 4660, "decoded nonce");
    check_int(ptc_token_decode(code, "test-device", "wrong-secret", &decoded), PTC_ERR_BAD_SIGNATURE, "bad signature");
    check_int(ptc_token_verify(code, "test-device", "test-secret", 2381, 120, NULL, NULL, &decoded), PTC_ERR_WRONG_DATE, "wrong date");
    check_int(ptc_token_verify(code, "test-device", "test-secret", 2380, 120, used_nonce_callback, NULL, &decoded), PTC_ERR_USED_TOKEN, "used nonce");

    payload.minutes = 180;
    payload.nonce = 4661;
    check_int(ptc_token_encode(&payload, "test-device", "test-secret", code), PTC_ERR_OK, "over-limit token encode");
    check_str(code, "24B82-AC004-HNMGY-D0FAS", "over-limit fixture parity");
    check_int(ptc_token_verify(code, "test-device", "test-secret", 2380, 120, NULL, NULL, &decoded), PTC_ERR_MINUTES_EXCEED_LIMIT, "minutes exceed");
}

static void test_time_and_policy(void)
{
    PtcCapabilities caps;
    PtcPolicyDecision decision;
    caps.raw_block_verified = false;
    caps.suspend_verified = false;

    check_int(ptc_day_index_from_unix(1577836800), 0, "2020 epoch day index");
    check_int(ptc_weekday_from_day_index(0), 3, "2020-01-01 weekday");
    check_true(ptc_bedtime_active(30, 1260, 480), "cross-midnight bedtime active");
    check_true(!ptc_bedtime_active(720, 1260, 480), "midday bedtime inactive");
    check_str(ptc_error_reason(PTC_ERR_BAD_SIGNATURE), "bad_signature", "error reason map");

    decision = ptc_policy_decide(PTC_CONTROL_OBSERVE, false, PTC_OPERATION_GRANT_MINUTES, &caps, false, false);
    check_true(decision.dry_run && !decision.may_write_pctl && !decision.consume_nonce_after_success, "observe dry run");

    decision = ptc_policy_decide(PTC_CONTROL_GRANT, false, PTC_OPERATION_GRANT_MINUTES, &caps, false, true);
    check_true(!decision.dry_run && decision.may_write_pctl && decision.requires_backup && decision.consume_nonce_after_success, "grant write decision");

    decision = ptc_policy_decide(PTC_CONTROL_GRANT, false, PTC_OPERATION_BLOCK_TODAY, &caps, false, true);
    check_int(decision.error, PTC_ERR_RAW_BLOCK_NOT_VERIFIED, "raw block gated");

    decision = ptc_policy_decide(PTC_CONTROL_GRANT, true, PTC_OPERATION_STATUS, &caps, false, true);
    check_int(decision.error, PTC_ERR_DISABLED, "disable flag wins");
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
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/capabilities.json", "{\"version\":1,\"raw_block_verified\":false,\"suspend_verified\":false}\n"), "write capabilities");
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
    make_valid_code(code);
    (void)ptc_companion_offline_code_request_json(request, sizeof(request), "1000-0003", 1002, code);
    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/1000-0003.json", request), "write backup fail request");
    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process backup failure");
    check_true(!pctl.applied, "backup failure blocks apply");
    check_true(!mem.storage.vtable->exists(&mem.storage, "app/ledger/used_nonces.jsonl"), "backup failure does not consume nonce");
    check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/1000-0003.json", result, sizeof(result)), "backup failure result");
    check_true(strstr(result, "\"reason\":\"pctl_backup_failed\"") != NULL, "backup failure reason");
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
    test_observe_status_flow();
    test_grant_flow_consumes_nonce_after_write();
    test_backup_failure_blocks_write();
    test_recover_processing();

    if (failures != 0) {
        fprintf(stderr, "%d C host tests failed\n", failures);
        return 1;
    }
    printf("C host core tests passed\n");
    return 0;
}
