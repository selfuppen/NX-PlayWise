#include "../../sysmodule/sysmodule_internal.h"
#include "../../platform/host/mem_storage.h"
#include "../../platform/host/pctl_stub.h"
#include "../../platform/host/operation_mode_file.h"
#include "../../platform/host/fake_time.h"
#include "../../platform/switch/fs_storage.h"
#include "../../companion/result_summary.h"
#include "../../companion/overlay/bridge.h"
#include "../../companion/nro/eden_timer.h"
#include "../../third_party/cjson/cJSON.h"

static int failures;
static const char *SECRET = "control-safety-fixture-secret";

static void expect(bool ok, const char *label)
{
    if (!ok) { fprintf(stderr, "FAIL: control safety: %s\n", label); ++failures; }
}

typedef struct {
    PtcMemStorage *mem;
    PtcPctlStub pctl;
    PtcFakeTime clock;
    PtcSysmodule core;
} Fixture;

static void init_fixture(Fixture *f)
{
    PtcRules rules;
    PtcRuntimeState state;
    f->mem = (PtcMemStorage *)calloc(1, sizeof(*f->mem));
    if (!f->mem) { fputs("fixture allocation failed\n", stderr); exit(1); }
    ptc_mem_storage_init(f->mem);
    ptc_pctl_stub_init(&f->pctl);
    f->pctl.model_elapsed_time = true;
    f->pctl.configured_minutes = 120;
    f->pctl.played_minutes_today = 20;
    f->pctl.status.limited_today = true;
    f->pctl.status.unrestricted_today = false;
    f->pctl.status.remaining_available = true;
    f->pctl.status.remaining_minutes = 100;
    f->pctl.status.configured_minutes_available = true;
    f->pctl.status.configured_minutes = 120;
    f->pctl.status.play_timer_enabled = true;
    ptc_fake_time_init(&f->clock, 1783526401, 2380, 720);
    ptc_sysmodule_init(&f->core, "app", &f->mem->storage, &f->pctl.pctl, &f->clock.provider);
    expect(f->mem->storage.vtable->write_text_atomic(&f->mem->storage, "app/config.json",
        "{\"version\":1,\"device_id\":\"kid-switch\",\"max_add_minutes\":240}"), "seed config");
    expect(f->mem->storage.vtable->write_text_atomic(&f->mem->storage, "app/credentials.json",
        "{\"version\":1,\"grant_secret\":\"control-safety-fixture-secret\"}"), "seed credentials");
    expect(f->mem->storage.vtable->write_text_atomic(&f->mem->storage, "app/setup.json",
        "{\"version\":1,\"phase\":\"active\",\"compatibility_status\":\"verified\","
        "\"restriction_cleared\":true,\"snapshot_available\":true,\"activate_after\":0,\"last_error\":\"\"}"), "seed setup");
    ptc_rules_default(&rules);
    for (unsigned i = 0; i < 7; ++i) { rules.week[i].mode = PTC_RULE_MODE_LIMIT; rules.week[i].minutes = 120; }
    rules.autonomy_policy.daily_buffer_minutes = 5;
    expect(save_rules(&f->core, &rules), "seed rules");
    expect(load_state(&f->core, &state) && save_state(&f->core, &state, f->clock.snapshot.unix_seconds), "seed state");
}

static void request(Fixture *f, const char *id, const char *type, const char *payload)
{
    char json[512];
    snprintf(json, sizeof(json), "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\","
        "\"created_at\":1,\"payload\":%s}", id, type, payload);
    process_request_text(&f->core, json, id);
}

static bool result_has(Fixture *f, const char *id, const char *needle)
{
    char path[320], text[8192];
    snprintf(path, sizeof(path), "app/results/%s.json", id);
    return f->mem->storage.vtable->read_text(&f->mem->storage, path, text, sizeof(text)) && strstr(text, needle);
}

static void test_direct_usage_projection(void)
{
    Fixture f;
    PtcResultState result;
    PtcCompanionResultSummary summary;
    char json[6144];
    init_fixture(&f);
    PtcPctlStatus status = f.pctl.status;
    status.played_minutes_available = false;
    status.configured_minutes = 60;
    status.remaining_minutes = 0;
    status.usage_supported = status.usage_available = true;
    status.usage_day_index = f.clock.snapshot.day_index;
    status.usage_captured_at = f.clock.snapshot.unix_seconds;
    status.usage_ns = UINT64_C(4191000000000);
    status.signed_remaining_ns_available = true;
    status.signed_remaining_ns = -INT64_C(591000000000);
    result_state_from_pctl(&result, f.clock.snapshot.day_index, &status);
    expect(result.played_minutes == 69 && result.played_minutes_available,
        "display retains excess usage instead of truncating at quota");
    PtcRules rules;
    PtcGrantPlan plan;
    expect(load_rules(&f.core, &rules) &&
        plan_grant(&rules, f.clock.snapshot, &status, 15, &plan) == PTC_ERR_OK &&
        plan.played_ceiling_minutes == 70,
        "grant rounds true usage up independently from display floor");
    ptc_result_ok_json(json, sizeof(json), "direct-display", "status", "release", true,
        &result, f.clock.snapshot.unix_seconds);
    expect(ptc_companion_result_summary_parse(json, &summary) && summary.usage_present &&
        summary.played_minutes == 69 && summary.usage_captured_at == status.usage_captured_at,
        "direct observation survives result serialization and parsing");
    status.usage_ns = UINT64_C(30133000000000);
    for (unsigned i = 0; i < 3; ++i) {
        status.configured_minutes = (uint16_t)(323 + 120 * i);
        status.remaining_minutes = i == 2 ? 60 : 0;
        result_state_from_pctl(&result, f.clock.snapshot.day_index, &status);
        expect(result.played_minutes == 502, "quota changes never change direct displayed use");
    }
    status.usage_available = false;
    result_state_from_pctl(&result, f.clock.snapshot.day_index, &status);
    expect(!result.played_minutes_available && result.played_minutes == -1,
        "1952 failure does not fall back to clamped estimate");
    status.usage_available = true; ++status.usage_day_index;
    result_state_from_pctl(&result, f.clock.snapshot.day_index, &status);
    expect(!result.played_minutes_available, "next-day observation cannot populate old-day result");
    free(f.mem);
}

static void test_snapshot_failure_stops_rule_writes(void)
{
    for (unsigned scenario = 0; scenario < 2; ++scenario) {
        Fixture f;
        char rules_before[8192], state_before[8192], after[8192];
        init_fixture(&f);
        expect(f.mem->storage.vtable->read_text(&f.mem->storage, "app/rules.json",
            rules_before, sizeof(rules_before)) &&
            f.mem->storage.vtable->read_text(&f.mem->storage, "app/state.json",
                state_before, sizeof(state_before)), "save snapshot-failure file preimages");
        f.pctl.snapshot_error = PTC_ERR_PCTL_READ_FAILED;
        request(&f, "snapshot-read-failed", scenario ? "disable_today_limit" : "set_scheduled_override",
            scenario ? "{}" : "{\"enabled\":true,\"start_day_index\":2380,\"end_day_index\":2380,"
                "\"rule\":{\"mode\":\"limit\",\"minutes\":180}}");
        expect(result_has(&f, "snapshot-read-failed", "\"reason\":\"pctl_backup_failed\""),
            "incomplete snapshot rejects the rule request with the existing backup error");
        expect(f.pctl.apply_target_calls == 0 && f.pctl.start_timer_calls == 0 &&
            !f.pctl.timer_stopped && !f.pctl.restore_called,
            "snapshot read failure performs no target, timer or guessed restoration write");
        expect(f.mem->storage.vtable->read_text(&f.mem->storage, "app/rules.json", after, sizeof(after)) &&
            strcmp(rules_before, after) == 0, "snapshot failure preserves rules bytes");
        expect(f.mem->storage.vtable->read_text(&f.mem->storage, "app/state.json", after, sizeof(after)) &&
            strcmp(state_before, after) == 0 && !recovery_path_exists(&f.core),
            "snapshot failure preserves business state without publishing a recovery transaction");
        free(f.mem);
    }
}

static void test_positive_subminute_runtime(void)
{
    Fixture f;
    PtcPctlStatus observed;
    init_fixture(&f);
    f.pctl.model_elapsed_time = false;
    f.pctl.status.remaining_minutes = 0;
    f.pctl.status.signed_remaining_ns_available = true;
    f.pctl.status.signed_remaining_ns = INT64_C(40000000000);
    f.pctl.status.restricted_now = false;
    PtcErrorCode error = observe_target_with_optional_activation(&f.core, &(PtcRequest){0},
        f.clock.snapshot, "release", PTC_PCTL_TARGET_LIMIT, 120, "subminute-test", &observed);
    expect(error == PTC_ERR_OK && f.pctl.start_timer_calls == 0,
        "positive 40 seconds is ready without redundant 1451 fallback");
    expect(!ptc_pctl_remaining_exhausted(&observed), "positive subminute is not exhausted");
    observed.signed_remaining_ns = 0;
    expect(ptc_pctl_remaining_exhausted(&observed), "zero signed remainder is exhausted");
    observed.signed_remaining_ns = -1;
    expect(ptc_pctl_remaining_exhausted(&observed), "negative signed remainder is exhausted");
    free(f.mem);
}

static void seed_direct_grant(Fixture *f, uint64_t used_ns, uint16_t quota)
{
    PtcRules rules;
    init_fixture(f);
    expect(load_rules(&f->core, &rules), "load direct grant rules");
    for (unsigned i = 0; i < 7; ++i) rules.week[i].minutes = quota;
    rules.autonomy_policy.daily_buffer_minutes = 15;
    expect(save_rules(&f->core, &rules), "seed direct grant quota");
    f->pctl.configured_minutes = f->pctl.status.configured_minutes = quota;
    f->pctl.played_minutes_today = (uint16_t)(used_ns / UINT64_C(60000000000));
    f->pctl.usage_fraction_ns = used_ns % UINT64_C(60000000000);
    f->pctl.status.remaining_minutes = quota > f->pctl.played_minutes_today
        ? quota - f->pctl.played_minutes_today : 0;
    f->pctl.status.usage_supported = f->pctl.status.usage_available = true;
    f->pctl.status.usage_ns = used_ns;
    f->pctl.status.usage_day_index = f->clock.snapshot.day_index;
    f->pctl.status.usage_captured_at = f->clock.snapshot.unix_seconds;
}

static void grant_payload(char *payload, size_t size)
{
    char code[16];
    uint8_t tier;
    expect(ptc_token_v2_tier_for_minutes(15, &tier) == PTC_ERR_OK &&
        ptc_token_v2_encode(tier, 42, "kid-switch", SECRET, 2380, code) == PTC_ERR_OK,
        "encode direct grant code");
    snprintf(payload, size, "{\"code\":\"%s\"}", code);
}

static const char *grant_types[] = {
    "preview_offline_code", "offline_code", "add_today_minutes", "claim_daily_buffer"
};

static void direct_grant_request(Fixture *f, unsigned surface, const char *id)
{
    char payload[64];
    if (surface < 2) grant_payload(payload, sizeof(payload));
    else snprintf(payload, sizeof(payload), "%s", surface == 2 ? "{\"minutes\":15}" : "{}");
    request(f, id, grant_types[surface], payload);
}

static void test_direct_grants(void)
{
    {
        Fixture f;
        PtcRules rules;
        PtcGrantPlan plan;
        seed_direct_grant(&f, 0, 60);
        expect(load_rules(&f.core, &rules), "load rounding table rules");
        const struct { uint64_t ns; uint16_t target, effective; int64_t remaining; } cases[] = {
            {0, 75, 15, 75},
            {UINT64_C(6059999999999), 116, 15, 15},
            {UINT64_C(6060000000000), 116, 15, 15},
            {UINT64_C(6060000000001), 117, 15, 15},
            {UINT64_C(14395000000000), 255, 15, 15},
            {UINT64_C(1435) * UINT64_C(60000000000), 1440, 5, 5},
            {INT64_MAX, 1440, 0, 0},
        };
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            f.pctl.status.usage_ns = cases[i].ns;
            expect(plan_grant(&rules, f.clock.snapshot, &f.pctl.status, 15, &plan) == PTC_ERR_OK &&
                plan.target_minutes == cases[i].target && plan.effective_minutes == cases[i].effective &&
                plan.remaining_after_minutes == cases[i].remaining,
                "nanosecond boundaries, partial cap and maximum s64 retain safe rounding");
        }
        f.pctl.status.usage_supported = false;
        f.pctl.status.played_minutes_available = true;
        f.pctl.status.played_minutes = 307445735u;
        expect(plan_grant(&rules, f.clock.snapshot, &f.pctl.status, 15, &plan) == PTC_ERR_PCTL_READ_FAILED,
            "legacy minute conversion rejects overflow before multiplication");
        free(f.mem);
    }
    for (unsigned surface = 0; surface < 4; ++surface) {
        Fixture f;
        PtcRuntimeState state;
        PtcGrantPlan plan;
        PtcRules rules;
        bool used;
        seed_direct_grant(&f, UINT64_C(6059000000000), 60); /* 100m59s */
        expect(load_rules(&f.core, &rules) && plan_grant(&rules, f.clock.snapshot,
            &f.pctl.status, 15, &plan) == PTC_ERR_OK && plan.target_minutes == 116 &&
            plan.effective_minutes == 15 && plan.remaining_after_minutes == 15,
            "overrun adds the full award with ceiling base and floor balance");
        direct_grant_request(&f, surface, "direct-grant");
        expect(result_has(&f, "direct-grant", "\"status\":\"ok\""), "all grant surfaces accept true cumulative usage");
        if (surface == 0) {
            expect(f.pctl.apply_target_calls == 0 &&
                result_has(&f, "direct-grant", "\"effective_add_minutes\":15") &&
                result_has(&f, "direct-grant", "\"remaining_after_minutes\":15"),
                "preview is non-consuming and uses the same fractional plan");
        } else {
            expect(f.pctl.last_target.minutes == 116 && load_rules(&f.core, &rules) &&
                rules.today_override.rule.minutes == 116,
                "actual grant matches preview instead of under-awarding from clamped remainder");
            expect(f.mem->storage.vtable->exists(&f.mem->storage, "app/activity/history.jsonl"),
                "successful grants write family activity");
        }
        expect(check_nonce_used(&f.core, 2380, 42, 2, &used) == PTC_ERR_OK && used == (surface == 1),
            "only confirmed offline redemption consumes nonce");
        expect(load_state(&f.core, &state) && state.buffer_claimed == (surface == 3),
            "only confirmed buffer consumes eligibility");
        free(f.mem);
    }
    for (unsigned surface = 0; surface < 4; ++surface) {
        for (unsigned invalid = 0; invalid < 7; ++invalid) {
            Fixture f;
            PtcRules rules;
            PtcRuntimeState state;
            PtcPctlVTable vtable;
            bool used;
            seed_direct_grant(&f, UINT64_C(6059000000000), 60);
            vtable = *f.pctl.pctl.vtable;
            if (invalid == 0) f.pctl.status.usage_available = false;
            if (invalid == 1) ++f.pctl.status.usage_day_index;
            if (invalid == 2) --f.pctl.status.usage_captured_at;
            if (invalid == 3) ++f.pctl.status.usage_captured_at;
            if (invalid == 4) f.pctl.status.usage_ns = UINT64_MAX;
            if (invalid == 5) { vtable.read_grant_status = NULL; f.pctl.pctl.vtable = &vtable; }
            if (invalid == 6) f.pctl.status.usage_result = 777;
            direct_grant_request(&f, surface, "unknown-grant");
            expect(result_has(&f, "unknown-grant", "pctl_read_failed") && f.pctl.apply_target_calls == 0 &&
                !recovery_path_exists(&f.core), "unknown, stale, invalid or mismatched usage rejects before transaction");
            expect(load_rules(&f.core, &rules) && !rules.today_override.present &&
                load_state(&f.core, &state) && !state.buffer_claimed &&
                check_nonce_used(&f.core, 2380, 42, 2, &used) == PTC_ERR_OK && !used,
                "rejected grants preserve rules, buffer and nonce");
            free(f.mem);
        }
    }
    for (unsigned surface = 0; surface < 4; ++surface) {
        Fixture f;
        PtcRuntimeState state;
        bool used;
        seed_direct_grant(&f, UINT64_C(1440) * UINT64_C(60000000000), 1440);
        direct_grant_request(&f, surface, "zero-grant");
        expect(f.pctl.apply_target_calls == 0 &&
            result_has(&f, "zero-grant", surface == 0 ? "\"effective_add_minutes\":0" : "minutes_exceed_limit"),
            "zero-effective preview explains cap; actual grants reject without writes");
        expect(check_nonce_used(&f.core, 2380, 42, 2, &used) == PTC_ERR_OK && !used &&
            load_state(&f.core, &state) && !state.buffer_claimed,
            "1440 cap does not consume an unusable award");
        free(f.mem);
    }
    for (unsigned surface = 0; surface < 4; ++surface) {
        Fixture f;
        PtcRuntimeState state;
        char activity[4096];
        seed_direct_grant(&f, UINT64_C(6059000000000), 1435);
        direct_grant_request(&f, surface, "partial-grant");
        expect(result_has(&f, "partial-grant", "\"status\":\"ok\""), "partially capped award succeeds");
        if (surface == 0) expect(result_has(&f, "partial-grant", "\"effective_add_minutes\":5"),
            "preview reports partial award rather than face value");
        else expect(f.pctl.last_target.minutes == 1440 && load_state(&f.core, &state) &&
            state.summary_grant_minutes == 5 &&
            f.mem->storage.vtable->read_text(&f.mem->storage, "app/activity/history.jsonl", activity, sizeof(activity)) &&
            strstr(activity, "\"minutes\":15") && strstr(activity, "\"effective_minutes\":5"),
            "all actual surfaces record face and effective award separately");
        free(f.mem);
    }
}

static void test_direct_grant_rollbacks(void)
{
    const char *failures_at[] = {"rules.json", "state.json", "activity/history.jsonl", "results/direct-fail.json"};
    for (unsigned surface = 1; surface < 4; ++surface) {
        for (unsigned stage = 0; stage < 4; ++stage) {
            Fixture f;
            PtcRules rules;
            PtcRuntimeState state;
            bool used;
            seed_direct_grant(&f, UINT64_C(6059000000000), 60);
            f.mem->fail_write_path_contains_once = failures_at[stage];
            direct_grant_request(&f, surface, "direct-fail");
            expect(!result_has(&f, "direct-fail", "\"status\":\"ok\""), "failed persistence never reports a committed grant");
            f.mem->fail_write_path_contains = NULL;
            expect(load_rules(&f.core, &rules) && !rules.today_override.present &&
                load_state(&f.core, &state) && !state.buffer_claimed &&
                f.pctl.configured_minutes == 60 && f.pctl.played_minutes_today == 100 &&
                check_nonce_used(&f.core, 2380, 42, 2, &used) == PTC_ERR_OK && !used,
                "rollback restores quota and eligibility without restoring real consumption");
            direct_grant_request(&f, surface, "direct-retry");
            expect(result_has(&f, "direct-retry", "\"status\":\"ok\"") &&
                f.pctl.last_target.minutes == 116, "retry awards once from original quota and true used time");
            free(f.mem);
        }
    }
}

static Fixture *grant_fault_fixture;
static unsigned grant_fault_kind;
static PtcErrorCode (*grant_original_apply)(PtcPctl *, const PtcPctlTarget *);

static PtcErrorCode grant_fault_apply(PtcPctl *pctl, const PtcPctlTarget *target)
{
    PtcErrorCode err = grant_original_apply(pctl, target);
    if (grant_fault_kind == 0) {
        ++grant_fault_fixture->clock.snapshot.day_index;
        ++grant_fault_fixture->clock.snapshot.unix_seconds;
    } else if (grant_fault_kind == 1) --grant_fault_fixture->clock.snapshot.unix_seconds;
    else grant_fault_fixture->pctl.status.usage_available = false;
    return err;
}

static void test_direct_grant_clock_and_field_failures(void)
{
    for (unsigned surface = 1; surface < 4; ++surface) {
        for (unsigned fault = 0; fault < 3; ++fault) {
            Fixture f;
            PtcRules rules;
            PtcRuntimeState state;
            PtcPctlVTable vtable;
            bool used;
            seed_direct_grant(&f, UINT64_C(6059000000000), 60);
            vtable = *f.pctl.pctl.vtable;
            grant_original_apply = vtable.apply_target;
            grant_fault_fixture = &f;
            grant_fault_kind = fault;
            vtable.apply_target = grant_fault_apply;
            f.pctl.pctl.vtable = &vtable;
            direct_grant_request(&f, surface, "clock-grant");
            if (fault < 2) {
                expect(result_has(&f, "clock-grant", "bad_clock") &&
                    f.pctl.configured_minutes == 60 && load_rules(&f.core, &rules) &&
                    !rules.today_override.present && load_state(&f.core, &state) && !state.buffer_claimed &&
                    check_nonce_used(&f.core, 2380, 42, 2, &used) == PTC_ERR_OK && !used,
                    "day switch or clock reversal during write rolls back and preserves award");
            } else expect(result_has(&f, "clock-grant", "\"status\":\"ok\"") &&
                result_has(&f, "clock-grant", "\"played_minutes_available\":false") &&
                f.pctl.configured_minutes == 116,
                "display-field failure after a validated grant does not undo a confirmed target");
            free(f.mem);
        }
    }
}

static void test_ledger(void)
{
    Fixture f;
    bool used;
    char code[16], payload[64], text[32768];
    init_fixture(&f);
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage, "app/ledger/used_nonces.jsonl",
        "{\"day_index\":2380,\"nonce\":123,\"token_version\":2}\n"), "seed prefix collision");
    expect(check_nonce_used(&f.core, 2380, 12, 2, &used) == PTC_ERR_OK && !used, "nonce 12 differs from 123");
    expect(check_nonce_used(&f.core, 2380, 123, 2, &used) == PTC_ERR_OK && used, "exact nonce is used");
    expect(ptc_token_v2_encode(3, 12, "kid-switch", SECRET, 2380, code) == PTC_ERR_OK, "encode new code");
    snprintf(payload, sizeof(payload), "{\"code\":\"%s\"}", code);
    request(&f, "prefix-ok", "offline_code", payload);
    expect(result_has(&f, "prefix-ok", "\"status\":\"ok\""), "legitimate prefix code redeems");
    request(&f, "prefix-replay", "offline_code", payload);
    expect(result_has(&f, "prefix-replay", "used_token"), "redeemed code remains single use");
    text[0] = '\0';
    for (unsigned i = 0; i < 512; ++i) {
        char line[128];
        snprintf(line, sizeof(line), "{\"day_index\":2380,\"nonce\":%u,\"token_version\":2}\n", i);
        strcat(text, line);
    }
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage, "app/ledger/used_nonces.jsonl", text), "seed all 512 nonces");
    expect(check_nonce_used(&f.core, 2380, 511, 2, &used) == PTC_ERR_OK && used, "read beyond old 4096-byte limit");
    f.mem->fail_read_path_contains = "used_nonces";
    request(&f, "ledger-read-error", "offline_code", payload);
    expect(result_has(&f, "ledger-read-error", "storage_read_failed"), "read failure does not authorize replay");
    f.mem->fail_read_path_contains = NULL;
    expect(f.mem->storage.vtable->append_line(&f.mem->storage, "app/ledger/used_nonces.jsonl", "{broken}"), "append damaged tail");
    expect(check_nonce_used(&f.core, 2380, 12, 2, &used) == PTC_ERR_STORAGE_READ_FAILED, "validate tail even after a match");
    free(f.mem);
}

static void test_real_storage_ledger(void)
{
    PtcFsStorage fs;
    PtcPctlStub pctl;
    PtcFakeTime clock;
    PtcSysmodule core;
    PtcStorageMetadata meta;
    char path[320], text[256];
    bool used;
    FILE *file;
    ptc_fs_storage_init(&fs);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&clock, 1783526401, 2380, 720);
    ptc_sysmodule_init(&core, "build/host/control-safety", &fs.storage, &pctl.pctl, &clock.provider);
    snprintf(path, sizeof(path), "%s/ledger/used_nonces.jsonl", core.app_root);
    expect(fs.storage.vtable->write_text_atomic(&fs.storage, path, ""), "create isolated real ledger");
    file = fopen(path, "wb");
    if (!file) { expect(false, "open real ledger"); return; }
    for (unsigned i = 0; i < 2000; ++i)
        fprintf(file, "{\"day_index\":2379,\"nonce\":%u,\"token_version\":2}\n", i % 512);
    fputs("{\"day_index\":2380,\"nonce\":123,\"token_version\":2}\n", file);
    fclose(file);
    expect(!fs.storage.vtable->read_text(&fs.storage, path, text, sizeof(text)), "real whole-file reader rejects truncation");
    expect(check_nonce_used(&core, 2380, 123, 2, &used) == PTC_ERR_OK && used, "stream a legacy ledger above backup capacity");
    expect(compact_nonce_ledger(&core, 2380) == PTC_ERR_OK, "migrate oversized expired ledger");
    expect(fs.storage.vtable->read_text(&fs.storage, path, text, sizeof(text)) &&
        strstr(text, "2380") && !strstr(text, "2379"), "compaction retains today's replay protection");
    expect(fs.storage.vtable->write_text_atomic(&fs.storage, path,
        "{\"day_index\":2380,\"nonce\":123,\"token_version\":2}\n"
        "{\"day_index\":2381,\"nonce\":511,\"token_version\":2}\n"), "seed future record");
    expect(compact_nonce_ledger(&core, 2380) == PTC_ERR_OK &&
        check_nonce_used(&core, 2381, 511, 2, &used) == PTC_ERR_OK && used, "never prune future records");
    file = fopen(path, "wb");
    if (!file) { expect(false, "open duplicate ledger"); return; }
    for (unsigned i = 0; i < 2000; ++i)
        fputs("{ \"nonce\": 123, \"day_index\": 2380, \"token_version\": 2 }\n", file);
    fclose(file);
    expect(compact_nonce_ledger(&core, 2380) == PTC_ERR_OK &&
        fs.storage.vtable->read_text(&fs.storage, path, text, sizeof(text)) &&
        check_nonce_used(&core, 2380, 123, 2, &used) == PTC_ERR_OK && used,
        "normalize and deduplicate oversized current-day replay records");
    memset(text, 'x', sizeof(text) - 1); text[sizeof(text) - 1] = '\0';
    expect(fs.storage.vtable->append_line(&fs.storage, path, text), "append maximum line");
    expect(check_nonce_used(&core, 2380, 123, 2, &used) == PTC_ERR_STORAGE_READ_FAILED, "malformed real tail blocks verification");
    expect(fs.storage.vtable->write_text_atomic(&fs.storage, path,
        "{\"day_index\":2380,\"nonce\":12,\"nonce\":123}\n"), "seed ambiguous record");
    expect(check_nonce_used(&core, 2380, 123, 1, &used) == PTC_ERR_STORAGE_READ_FAILED,
        "duplicate JSON keys cannot hide consumed nonces");
    expect(fs.storage.vtable->remove_tree(&fs.storage, core.app_root), "remove isolated real fixture");
    expect(fs.storage.vtable->metadata(&fs.storage, path, &meta) && meta.type == PTC_STORAGE_ENTRY_MISSING, "missing differs from I/O failure");
}

static void test_transaction_and_read_failures(void)
{
    Fixture f;
    PtcRuntimeState state;
    PtcRules rules;
    unsigned writes;
    init_fixture(&f);
    f.pctl.configured_minutes = 90;
    f.pctl.status.configured_minutes = 90;
    f.pctl.runtime_effect_succeeds = false;
    expect(ptc_sysmodule_enforce_tick(&f.core) == 1 && recovery_path_exists(&f.core), "enter pending transaction");
    writes = f.pctl.apply_target_calls;
    request(&f, "pending-add", "add_today_minutes", "{\"minutes\":15}");
    expect(result_has(&f, "pending-add", "control_busy") && f.pctl.apply_target_calls == writes &&
        recovery_path_exists(&f.core), "new writes cannot take over pending transaction");
    process_request_text(&f.core, "not-json", "malformed-request");
    expect(recovery_path_exists(&f.core), "parse errors cannot roll back another transaction");
    f.clock.snapshot.unix_seconds += 31;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && !state.apply_pending_confirmation && !recovery_path_exists(&f.core), "timeout resolves original transaction");
    f.pctl.runtime_effect_succeeds = true;
    expect(ptc_sysmodule_enforce_tick(&f.core) == 1, "control resumes after timeout");
    writes = f.pctl.apply_target_calls;
    f.mem->fail_read_path_contains = "rules.json";
    expect(!load_rules(&f.core, &rules), "failed rules read rejects defaults");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(f.pctl.apply_target_calls == writes, "failed rules read cannot relax restrictions");
    f.mem->fail_read_path_contains = "state.json";
    expect(!load_state(&f.core, &state), "failed state read rejects defaults");
    request(&f, "buffer-read-error", "claim_daily_buffer", "{}");
    expect(result_has(&f, "buffer-read-error", "\"status\":\"error\"") && f.pctl.apply_target_calls == writes,
        "failed state read cannot consume buffer or write PCTL");
    f.mem->fail_read_path_contains = NULL;
    expect(load_state(&f.core, &state), "read state recovers");
    state.apply_pending_confirmation = true;
    expect(save_state(&f.core, &state, f.clock.snapshot.unix_seconds), "seed orphan pending marker");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(f.mem->storage.vtable->exists(&f.mem->storage, "app/flags/disable.flag"), "orphan pending marker enters explicit protection");
    free(f.mem);
}

static void usage(Fixture *f, uint16_t played)
{
    f->pctl.played_minutes_today = played;
    f->pctl.status.remaining_minutes = 120u - played;
    (void)ptc_sysmodule_enforce_tick(&f->core);
}

static void test_eye_care_continuity_and_recovery(void)
{
    Fixture f;
    PtcRules rules;
    PtcRuntimeState state;
    int64_t deadline;
    uint64_t instance;
    char payload[128];
    init_fixture(&f);
    expect(load_rules(&f.core, &rules), "read eye-care fixture rules");
    rules.eye_care.enabled = true;
    expect(save_rules(&f.core, &rules), "enable eye care fixture");
    usage(&f, 20);
    f.clock.snapshot.unix_seconds += 1800;
    usage(&f, 50);
    expect(load_state(&f.core, &state) && state.eye_care_accumulated_minutes == 30, "accumulate 30 minutes");
    f.clock.snapshot.unix_seconds += 60;
    usage(&f, 50);
    f.clock.snapshot.unix_seconds += 539;
    usage(&f, 50);
    expect(load_state(&f.core, &state) && state.eye_care_accumulated_minutes == 30, "short rest retains cycle");
    f.clock.snapshot.unix_seconds += 1;
    usage(&f, 50);
    usage(&f, 60);
    expect(load_state(&f.core, &state) && state.eye_care_accumulated_minutes == 10 && !state.eye_care_resting,
        "sufficient natural rest begins a fresh cycle");
    f.pctl.read_error = PTC_ERR_PCTL_READ_FAILED;
    usage(&f, 65);
    f.pctl.read_error = PTC_ERR_OK;
    usage(&f, 65);
    expect(load_state(&f.core, &state) && state.eye_care_accumulated_minutes == 15, "recovered read includes missed usage delta");
    ++f.clock.snapshot.day_index;
    f.pctl.played_minutes_today = 5;
    f.pctl.status.remaining_minutes = 115;
    request(&f, "eye-midnight-preview", "status", "{}");
    expect(result_has(&f, "eye-midnight-preview", "\"used_minutes\":20"),
        "status preserves cycle before midnight enforcement");
    expect(load_state(&f.core, &state) && state.eye_care_accumulated_minutes == 15,
        "status projection never persists usage");
    usage(&f, 5);
    expect(load_state(&f.core, &state) && state.eye_care_accumulated_minutes == 20, "playing cycle crosses midnight");
    f.clock.snapshot.minute_of_day = 1438;
    usage(&f, 25);
    expect(load_state(&f.core, &state) && state.eye_care_resting, "reach play threshold");
    deadline = state.eye_care_rest_deadline; instance = state.eye_care_break_id;
    ++f.clock.snapshot.day_index;
    f.clock.snapshot.minute_of_day = 0;
    f.clock.snapshot.unix_seconds += 120;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.eye_care_resting && state.eye_care_rest_deadline == deadline &&
        state.eye_care_break_id == instance && f.pctl.status.blocked_today, "midnight does not shorten a rest");
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage, "app/flags/disable.flag", "test-protection\n"), "seed protection");
    f.clock.snapshot.unix_seconds = deadline + 1;
    ++f.clock.snapshot.day_index;
    expect(load_rules(&f.core, &rules), "read next-day rules");
    for (unsigned i = 0; i < 7; ++i) rules.week[i].minutes = 10;
    expect(save_rules(&f.core, &rules), "lower next-day quota");
    state.eye_care_last_used_minutes = 25;
    state.eye_care_usage_known = true;
    expect(save_state(&f.core, &state, f.clock.snapshot.unix_seconds), "retain prior-day usage baseline");
    f.pctl.played_minutes_today = 0;
    snprintf(payload, sizeof(payload), "{\"break_id\":%llu}", (unsigned long long)instance);
    f.pctl.write_error = PTC_ERR_PCTL_WRITE_FAILED;
    request(&f, "eye-recovery-fail", "skip_eye_care_break", payload);
    expect(result_has(&f, "eye-recovery-fail", "\"status\":\"error\"") && load_state(&f.core, &state) &&
        state.eye_care_resting, "failed protected recovery retains rest");
    f.pctl.write_error = PTC_ERR_OK;
    request(&f, "eye-recovery-ok", "skip_eye_care_break", payload);
    expect(result_has(&f, "eye-recovery-ok", "\"status\":\"ok\"") && load_state(&f.core, &state) &&
        !state.eye_care_resting && f.mem->storage.vtable->exists(&f.mem->storage, "app/flags/disable.flag"),
        "protected recovery releases only this rest and retains disable flag");
    free(f.mem);
}

static void test_eye_care_rest_deadline_survives_backward_clock(void)
{
    Fixture f;
    PtcRules rules;
    PtcRuntimeState state;
    int64_t t0, original_deadline, capped_deadline;
    init_fixture(&f);
    expect(load_rules(&f.core, &rules), "read backward-clock fixture rules");
    rules.eye_care.enabled = true;
    rules.eye_care.play_minutes = 40;
    rules.eye_care.rest_minutes = 10;
    expect(save_rules(&f.core, &rules), "enable eye care for backward-clock test");
    t0 = f.clock.snapshot.unix_seconds;
    original_deadline = t0 + 10 * 60;
    expect(load_state(&f.core, &state), "load state for backward-clock test");
    state.eye_care_resting = true;
    state.eye_care_rest_deadline = original_deadline;
    state.eye_care_break_id = 4242;
    state.eye_care_day_index = f.clock.snapshot.day_index;
    expect(save_state(&f.core, &state, t0), "seed active eye-care rest");
    /* Wall clock steps back an hour; the forced rest must not stretch with it. */
    f.clock.snapshot.unix_seconds = t0 - 3600;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    capped_deadline = (t0 - 3600) + 10 * 60;
    expect(load_state(&f.core, &state) && state.eye_care_resting &&
        state.eye_care_rest_deadline == capped_deadline &&
        state.eye_care_rest_deadline < original_deadline,
        "backward clock caps the rest deadline to the configured window");
    free(f.mem);
}

static void test_eye_care_grants_unblock_after_deadline(void)
{
    Fixture f;
    PtcRules rules;
    PtcRuntimeState state;
    int64_t t0;
    init_fixture(&f);
    expect(load_rules(&f.core, &rules), "read grant-deadline fixture rules");
    rules.eye_care.enabled = true;
    rules.eye_care.play_minutes = 40;
    rules.eye_care.rest_minutes = 10;
    expect(save_rules(&f.core, &rules), "enable eye care for grant-deadline test");
    t0 = f.clock.snapshot.unix_seconds;
    expect(load_state(&f.core, &state), "load state for grant-deadline test");
    state.eye_care_resting = true;
    state.eye_care_rest_deadline = t0 + 10 * 60;
    state.eye_care_break_id = 7777;
    expect(save_state(&f.core, &state, t0), "seed active eye-care rest for grant test");
    request(&f, "grant-during-rest", "add_today_minutes", "{\"minutes\":15}");
    expect(result_has(&f, "grant-during-rest", "\"status\":\"error\""),
        "add-time is rejected while the eye-care rest is active");
    /* Deadline passes with no enforce tick to clear the stale resting flag. */
    f.clock.snapshot.unix_seconds = t0 + 10 * 60 + 1;
    request(&f, "grant-after-rest", "add_today_minutes", "{\"minutes\":15}");
    expect(result_has(&f, "grant-after-rest", "\"status\":\"ok\""),
        "add-time is accepted once the rest deadline has passed");
    free(f.mem);
}

static void test_sleep_without_scheduler_ticks(void)
{
    Fixture f;
    PtcRules rules;
    PtcRuntimeState state;
    init_fixture(&f);
    expect(load_rules(&f.core, &rules), "read sleep fixture rules");
    rules.eye_care.enabled = true;
    expect(save_rules(&f.core, &rules), "enable sleep fixture");
    usage(&f, 20);
    f.clock.snapshot.unix_seconds += 1800;
    usage(&f, 50);
    f.clock.snapshot.unix_seconds += 1200;
    usage(&f, 50);
    expect(load_state(&f.core, &state) && state.eye_care_accumulated_minutes == 0,
        "unchanged reliable reading after sleep resets without intermediate ticks");
    usage(&f, 60);
    f.pctl.read_error = PTC_ERR_PCTL_READ_FAILED;
    usage(&f, 60);
    f.pctl.read_error = PTC_ERR_OK;
    f.clock.snapshot.unix_seconds += 1200;
    usage(&f, 60);
    expect(load_state(&f.core, &state) && state.eye_care_accumulated_minutes == 10,
        "an unknown interval cannot prove natural rest");
    free(f.mem);
}

static void test_bedtime_result_rollback(void)
{
    Fixture f;
    PtcRules rules;
    PtcRuntimeState state;
    PtcPctlSettingsSnapshot snapshot;
    PtcBedtimeEvaluation evaluation;
    init_fixture(&f);
    expect(load_rules(&f.core, &rules), "read bedtime rules");
    rules.bedtime.enabled = true;
    for (unsigned i = 0; i < 7; ++i) {
        rules.bedtime.week[i].enabled = true;
        rules.bedtime.week[i].start_minute = 1260;
        rules.bedtime.week[i].end_minute = 420;
    }
    f.clock.snapshot.minute_of_day = 1300;
    expect(save_rules(&f.core, &rules) && ptc_sysmodule_enforce_tick(&f.core) == 1,
        "enter bedtime with recovery snapshot");
    expect(load_state(&f.core, &state) && state.bedtime_enforced, "bedtime state is active");
    expect(load_eye_care_snapshot(&f.core, &snapshot) == false, "eye snapshot initially absent");
    f.mem->fail_write_path_contains_once = "/results/";
    request(&f, "bedtime-result-failure", "disable_bedtime", "{}");
    expect(load_rules(&f.core, &rules) && rules.bedtime.enabled &&
        load_state(&f.core, &state) && state.bedtime_enforced && f.pctl.status.blocked_today,
        "failed bedtime result restores policy and restriction");
    expect(load_bedtime_snapshot(&f.core, &snapshot, &evaluation.window_instance_id,
        &evaluation.start_day_index), "rollback restores both bedtime snapshot and instance metadata");
    request(&f, "bedtime-retry", "disable_bedtime", "{}");
    expect(result_has(&f, "bedtime-retry", "\"status\":\"ok\"") &&
        load_state(&f.core, &state) && !state.bedtime_enforced, "recovery can retry after result rollback");
    free(f.mem);
}

static void enter_bedtime_with_stopped_timer(Fixture *f)
{
    PtcRules rules;
    PtcRuntimeState state;
    PtcPctlSettingsSnapshot snapshot;
    uint64_t instance;
    uint16_t start_day;
    expect(load_rules(&f->core, &rules), "read timer recovery rules");
    rules.bedtime.enabled = true;
    for (unsigned i = 0; i < 7; ++i) {
        rules.bedtime.week[i].enabled = true;
        rules.bedtime.week[i].start_minute = 1350;
        rules.bedtime.week[i].end_minute = 450;
    }
    /* Match the affected SD card's October 6 -> 7 scheduled 120-minute day. */
    rules.scheduled_override.enabled = true;
    rules.scheduled_override.start_day_index = 2465;
    rules.scheduled_override.end_day_index = 2474;
    rules.scheduled_override.rule.mode = PTC_RULE_MODE_LIMIT;
    rules.scheduled_override.rule.minutes = 120;
    f->clock.snapshot.day_index = 2470;
    f->clock.snapshot.minute_of_day = 1350;
    f->clock.snapshot.unix_seconds = 1791297000;
    expect(save_rules(&f->core, &rules) && ptc_sysmodule_enforce_tick(&f->core) == 1,
        "enter bedtime before midnight");
    expect(load_state(&f->core, &state) && state.bedtime_enforced &&
        load_bedtime_snapshot(&f->core, &snapshot, &instance, &start_day) && snapshot.timer_enabled,
        "bedtime records the previously running timer");
    expect(f->pctl.pctl.vtable->stop_timer(&f->pctl.pctl) == PTC_ERR_OK,
        "simulate system stopping timer during bedtime");
}

static void test_cross_night_bedtime_does_not_start_timer(void)
{
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        Fixture f;
        PtcRules rules;
        PtcRuntimeState state;
        PtcPctlStatus status;
        uint64_t carry = 0;
        init_fixture(&f);
        enter_bedtime_with_stopped_timer(&f);
        f.clock.snapshot.day_index = 2471;
        f.clock.snapshot.minute_of_day = 0;
        f.clock.snapshot.unix_seconds = 1791302403;
        ptc_pctl_stub_reset_daily_usage(&f.pctl);
        expect(ptc_sysmodule_enforce_tick(&f.core) == 1 && f.pctl.status.blocked_today,
            "midnight keeps the cross-night restriction");
        /* Persistent bedtime state must survive a new backend instance. */
        ptc_sysmodule_init(&f.core, "app", &f.mem->storage, &f.pctl.pctl, &f.clock.provider);
        if (scenario == 1) {
            expect(load_rules(&f.core, &rules), "read unlimited morning rules");
            rules.scheduled_override.rule.mode = PTC_RULE_MODE_UNLIMITED;
            expect(save_rules(&f.core, &rules), "set unlimited morning");
        } else if (scenario == 2) {
            f.pctl.write_error = PTC_ERR_PCTL_WRITE_FAILED;
            f.pctl.apply_target_fail_on_call = f.pctl.apply_target_calls + 1;
        }
        f.clock.snapshot.minute_of_day = 501;
        f.clock.snapshot.unix_seconds = 1791332512;
        int applied = ptc_sysmodule_enforce_tick(&f.core);
        expect(f.pctl.start_timer_calls == 0 && !f.pctl.status.play_timer_enabled,
            "autonomous morning recovery never starts 1451");
        expect(load_state(&f.core, &state), "read morning recovery state");
        if (scenario == 2) {
            expect(applied == 0 && state.bedtime_enforced && f.pctl.status.blocked_today &&
                f.mem->storage.vtable->exists(&f.mem->storage, "app/flags/disable.flag"),
                "failed morning write retains bedtime and enters protection");
        } else {
            expect(applied == 1 && !state.bedtime_enforced && !state.apply_pending_confirmation &&
                !recovery_path_exists(&f.core), "morning recovery commits without runtime activation");
            expect(f.pctl.pctl.vtable->read_status(&f.pctl.pctl, 3, &status) == PTC_ERR_OK &&
                (scenario == 1 ? status.unrestricted_today :
                 status.limited_today && status.configured_minutes == 120 && status.remaining_minutes == 120),
                "morning restores the new day's effective rule");
            expect(!ptc_pctl_stub_advance_usage_ns(&f.pctl, 6ULL * 3600 * 1000000000ULL, &carry),
                "simulated stopped timer does not charge elapsed morning time");
            expect(ptc_sysmodule_enforce_tick(&f.core) == 0 && f.pctl.start_timer_calls == 0,
                "repeated autonomous enforcement cannot reactivate the timer");
        }
        free(f.mem);
    }
}

static PtcErrorCode restore_with_mismatched_settings(PtcPctl *pctl, const PtcPctlSettingsSnapshot *snapshot)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    PtcPctlStub reference;
    ptc_pctl_stub_init(&reference);
    PtcErrorCode err = reference.pctl.vtable->restore_settings(pctl, snapshot);
    if (err == PTC_ERR_OK) ++stub->configured_minutes;
    return err;
}

static void test_same_day_bedtime_timer_recovery(void)
{
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        Fixture f;
        PtcRuntimeState state;
        PtcPctlVTable faulty;
        init_fixture(&f);
        enter_bedtime_with_stopped_timer(&f);
        if (scenario == 3) {
            request(&f, "parent-bedtime-recovery", "disable_bedtime", "{}");
            expect(result_has(&f, "parent-bedtime-recovery", "\"status\":\"ok\"") &&
                f.pctl.start_timer_calls == 1 && f.pctl.status.play_timer_enabled,
                "explicit parent recovery retains exact timer restoration");
        } else {
            /* A same-day clock correction can leave the active night window. */
            f.clock.snapshot.minute_of_day = 1200;
            f.clock.snapshot.unix_seconds -= 150 * 60;
            if (scenario == 1) f.pctl.restore_error = PTC_ERR_PCTL_RESTORE_FAILED;
            if (scenario == 2) {
                faulty = *f.pctl.pctl.vtable;
                faulty.restore_settings = restore_with_mismatched_settings;
                f.pctl.pctl.vtable = &faulty;
            }
            int applied = ptc_sysmodule_enforce_tick(&f.core);
            expect(f.pctl.start_timer_calls == 0 && !f.pctl.status.play_timer_enabled,
                "same-day autonomous recovery cannot replay a stale enabled timer");
            expect(load_state(&f.core, &state), "read same-day recovery state");
            if (scenario == 0) {
                expect(applied == 1 && f.pctl.restore_called && !state.bedtime_enforced &&
                    f.pctl.configured_minutes == 120 && f.pctl.played_minutes_today == 20 &&
                    f.pctl.status.remaining_minutes == 100 && !recovery_path_exists(&f.core),
                    "same-day recovery restores settings and preserves existing usage");
            } else {
                expect(applied == 0 && state.bedtime_enforced && recovery_path_exists(&f.core) &&
                    f.mem->storage.vtable->exists(&f.mem->storage, "app/flags/disable.flag"),
                    "failed restore or mismatched readback retains evidence and enters protection");
            }
        }
        free(f.mem);
    }
}

static void test_no_blocked_activation_and_recovery_ui(void)
{
    Fixture f;
    PtcPctlStatus observed;
    PtcRequest req = {0};
    PtcCompanionResultSummary summary = {0};
    unsigned calls;
    init_fixture(&f);
    snprintf(req.request_id, sizeof(req.request_id), "blocked-check");
    f.pctl.status.limited_today = false;
    f.pctl.status.blocked_today = true;
    f.pctl.status.restricted_now = false;
    calls = f.pctl.start_timer_calls;
    expect(observe_target_with_optional_activation(&f.core, &req, f.clock.snapshot, "release",
        PTC_PCTL_TARGET_BLOCKED, 0, "test", &observed) == PTC_ERR_OK && f.pctl.start_timer_calls == calls,
        "blocked settings never activate 1451 even with transient restricted=false");
    f.pctl.status.blocked_today = false;
    expect(observe_target_with_optional_activation(&f.core, &req, f.clock.snapshot, "release",
        PTC_PCTL_TARGET_BLOCKED, 0, "test", &observed) != PTC_ERR_OK && f.pctl.start_timer_calls == calls,
        "unobserved blocked settings fail without activation");
    summary.valid = true; summary.eye_care_enabled = true; summary.eye_care_break_id = 42;
    snprintf(summary.eye_care_phase, sizeof(summary.eye_care_phase), "resting");
    expect(ptc_overlay_parent_action_unavailable_reason(&summary, PTC_OVERLAY_PARENT_SKIP_EYE_CARE) != NULL,
        "expired normal rest requires refresh");
    summary.disable_flag_present = true;
    expect(ptc_overlay_parent_action_unavailable_reason(&summary, PTC_OVERLAY_PARENT_SKIP_EYE_CARE) == NULL,
        "expired protected rest retains recovery entrance");
    free(f.mem);
}


static PtcOperationModeStatus test_operation_mode(void *ctx)
{
    return *(PtcOperationModeStatus *)ctx;
}

static void enable_dock_fixture(Fixture *f, bool force, uint16_t minutes)
{
    PtcRules rules;
    PtcRuntimeState state;
    expect(load_rules(&f->core, &rules) && load_state(&f->core, &state), "dock fixture read");
    rules.dock_policy.force_docked = force;
    rules.dock_policy.undocked_limit_enabled = true;
    rules.dock_policy.undocked_daily_minutes = minutes;
    state.dock_tracking_started = true;
    state.dock_day_index = f->clock.snapshot.day_index;
    state.dock_usage_known = true;
    dock_rebaseline(&f->core, &state, ptc_weekday_from_day_index(state.dock_day_index));
    expect(save_rules(&f->core, &rules) && save_state(&f->core, &state, f->clock.snapshot.unix_seconds), "dock fixture save");
}

static void test_persisted_eden_mode(void)
{
    Fixture f;
    PtcRuntimeState state;
    PtcRules rules;
    uint64_t carry = 0;
    init_fixture(&f);
    PtcFileOperationMode file_mode = {&f.mem->storage, "eden-app"};
    PtcOperationModeProvider provider = {ptc_file_operation_mode_read, &file_mode};
    f.core.operation_mode_provider = &provider;
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage,"app/operation-mode.txt","docked"), "separate production mode fixture");
    expect(ptc_file_operation_mode_read(&file_mode).mode == PTC_OPERATION_MODE_UNDOCKED, "Eden provider reads isolated root and defaults to non-TV");
    enable_dock_fixture(&f, false, 1);
    f.core.dock_boot_sampled = true;
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl,60000000000ULL,&carry);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(f.pctl.status.blocked_today && load_state(&f.core,&state) &&
        state.undocked_used_ns == 60000000000ULL, "persisted provider enforces exhausted non-TV quota");
    for (int i = 0; i < 6; ++i) {
        bool tv = !(i % 2);
        expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage,"eden-app/operation-mode.txt",tv ? "docked" : "undocked"), "persist simulated mode change");
        (void)ptc_sysmodule_enforce_tick(&f.core);
        expect(f.pctl.status.blocked_today == !tv && f.pctl.played_minutes_today == 21 &&
            load_state(&f.core,&state) && state.undocked_used_ns == 60000000000ULL, "repeated persisted switches recompute limits without refunding usage");
    }
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage,"eden-app/operation-mode.txt","docked"), "persist TV before restart");
    ptc_sysmodule_init(&f.core,"app",&f.mem->storage,&f.pctl.pctl,&f.clock.provider);
    PtcFileOperationMode reopened = {&f.mem->storage,"eden-app"};
    provider.ctx = &reopened;
    f.core.operation_mode_provider = &provider;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(ptc_file_operation_mode_read(&reopened).mode == PTC_OPERATION_MODE_DOCKED &&
        !f.pctl.status.blocked_today && f.pctl.played_minutes_today == 21 &&
        load_state(&f.core,&state) && state.undocked_used_ns == 60000000000ULL, "restart retains mode and both usage counters");
    enable_dock_fixture(&f,true,30);
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage,"eden-app/operation-mode.txt","undocked"), "persist non-TV for forced TV rule");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(f.pctl.status.blocked_today,"persisted non-TV cannot bypass forced TV");
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage,"eden-app/operation-mode.txt","docked") &&
        load_rules(&f.core,&rules), "persist TV with concurrent bedtime");
    rules.bedtime.enabled = true;
    for (unsigned i = 0; i < 7; ++i) {
        rules.bedtime.week[i].enabled = true;
        rules.bedtime.week[i].start_minute = 600;
        rules.bedtime.week[i].end_minute = 500;
    }
    expect(save_rules(&f.core,&rules),"save concurrent restriction for file provider");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(f.pctl.status.blocked_today,"simulating TV preserves bedtime restriction");
    free(f.mem);
}

static void test_dock_policy(void)
{
    Fixture f;
    PtcRuntimeState state;
    PtcRules rules;
    PtcOperationModeStatus mode = {PTC_OPERATION_MODE_UNDOCKED, true, true};
    PtcOperationModeProvider provider = {test_operation_mode, &mode};
    uint64_t carry = 0;
    init_fixture(&f);
    f.core.operation_mode_provider = &provider;
    enable_dock_fixture(&f, false, 1);
    f.core.dock_boot_sampled = true;
    expect(ptc_pctl_stub_advance_usage_ns(&f.pctl, 20000000000ULL, &carry) == false, "fraction does not spend a whole minute");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.undocked_used_ns == 20000000000ULL, "preserve 20 second usage");
    mode.mode = PTC_OPERATION_MODE_DOCKED;
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 20000000000ULL, &carry);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.undocked_used_ns == 40000000000ULL, "cross-mode interval is charged conservatively");
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 20000000000ULL, &carry);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.undocked_used_ns == 40000000000ULL, "stable TV usage does not charge handheld");
    mode.mode = PTC_OPERATION_MODE_UNDOCKED;
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 20000000000ULL, &carry);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.dock_enforced && f.pctl.status.blocked_today &&
        state.undocked_used_ns == 60000000000ULL, "handheld exhausted blocks without rounding refund");
    request(&f, "dock-buffer", "claim_daily_buffer", "{}");
    expect(result_has(&f, "dock-buffer", "dock_active") && load_state(&f.core, &state) && !state.buffer_claimed,
        "dock restriction does not consume buffer");
    request(&f, "dock-unlimited", "disable_today_limit", "{}");
    expect(result_has(&f, "dock-unlimited", "dock_active"), "unlimited cannot bypass dock");
    request(&f, "dock-stale", "waive_dock_policy_today", "{\"expected_day_index\":2379}");
    expect(result_has(&f, "dock-stale", "dock_date_mismatch"), "stale waiver rejected");
    mode.mode = PTC_OPERATION_MODE_DOCKED;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && !state.dock_enforced && f.pctl.status.limited_today &&
        f.pctl.configured_minutes == 120 && f.pctl.played_minutes_today == 21, "TV restores daily total with existing usage");
    mode.mode = PTC_OPERATION_MODE_UNKNOWN;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(f.pctl.status.blocked_today, "unknown mode restricts");
    mode.mode = PTC_OPERATION_MODE_DOCKED;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(!f.pctl.status.blocked_today, "mode recovery restores TV");
    request(&f, "dock-waive", "waive_dock_policy_today", "{\"expected_day_index\":2380}");
    expect(result_has(&f, "dock-waive", "\"status\":\"ok\"") && load_state(&f.core, &state) &&
        state.dock_waived && state.undocked_used_ns == 60000000000ULL, "waiver retains use");
    mode.mode = PTC_OPERATION_MODE_UNDOCKED;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(!f.pctl.status.blocked_today, "today waiver permits handheld");
    f.clock.snapshot.day_index++;
    f.clock.snapshot.unix_seconds += 86400;
    ptc_pctl_stub_reset_daily_usage(&f.pctl);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.undocked_used_ns == 0 &&
        !state.dock_enforced, "new day resets use");
    free(f.mem);

    init_fixture(&f);
    mode.mode = PTC_OPERATION_MODE_UNDOCKED;
    f.core.operation_mode_provider = &provider;
    enable_dock_fixture(&f, true, 30);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(f.pctl.status.blocked_today, "force dock immediately blocks handheld");
    f.pctl.write_error = PTC_ERR_PCTL_WRITE_FAILED;
    request(&f, "dock-failed-waive", "waive_dock_policy_today", "{\"expected_day_index\":2380}");
    expect(result_has(&f, "dock-failed-waive", "\"status\":\"error\"") && load_state(&f.core, &state) &&
        !state.dock_waived && state.dock_enforced, "failed waiver rolls back state");
    f.pctl.write_error = PTC_ERR_OK;
    expect(load_rules(&f.core, &rules), "load concurrent policy");
    rules.bedtime.enabled = true;
    for (unsigned i = 0; i < 7; ++i) { rules.bedtime.week[i].enabled = true; rules.bedtime.week[i].start_minute = 600; rules.bedtime.week[i].end_minute = 500; }
    expect(save_rules(&f.core, &rules), "save concurrent bedtime");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    request(&f, "dock-bed-waive", "waive_dock_policy_today", "{\"expected_day_index\":2380}");
    expect(result_has(&f, "dock-bed-waive", "\"status\":\"ok\"") && f.pctl.status.blocked_today,
        "waiving dock preserves bedtime");
    free(f.mem);

    init_fixture(&f);
    mode.mode = PTC_OPERATION_MODE_UNDOCKED;
    mode.dock_supported = false;
    f.core.operation_mode_provider = &provider;
    request(&f, "dock-lite", "set_dock_policy", "{\"force_docked\":true,\"undocked_limit_enabled\":false,\"undocked_daily_minutes\":30}");
    expect(result_has(&f, "dock-lite", "dock_unsupported"), "Lite rejects force dock");
    request(&f, "dock-unconfirmed", "set_dock_policy", "{\"force_docked\":false,\"undocked_limit_enabled\":true,\"undocked_daily_minutes\":30}");
    expect(result_has(&f, "dock-unconfirmed", "dock_confirmation_required"), "first enable needs confirmation");
    request(&f, "dock-invalid", "set_dock_policy", "{\"force_docked\":false,\"undocked_limit_enabled\":true,\"undocked_daily_minutes\":1441}");
    expect(result_has(&f, "dock-invalid", "bad_request"), "out of range policy rejected");
    free(f.mem);
}

static void test_dock_daily_total_priority(void)
{
    for (int today_override = 0; today_override < 2; ++today_override) {
        Fixture f;
        PtcRules rules;
        PtcRuntimeState state;
        PtcOperationModeStatus mode = {PTC_OPERATION_MODE_UNDOCKED, true, true};
        PtcOperationModeProvider provider = {test_operation_mode, &mode};
        uint64_t carry = 0;
        init_fixture(&f);
        f.core.operation_mode_provider = &provider;
        expect(load_rules(&f.core, &rules), "read daily priority rules");
        if (today_override) {
            rules.today_override.present = true;
            rules.today_override.day_index = f.clock.snapshot.day_index;
            rules.today_override.rule = (PtcDayRule){PTC_RULE_MODE_LIMIT, 30};
        } else {
            for (unsigned i = 0; i < 7; ++i) rules.week[i].minutes = 30;
        }
        expect(save_rules(&f.core, &rules), "save daily total below non-TV quota");
        (void)ptc_sysmodule_enforce_tick(&f.core);
        enable_dock_fixture(&f, false, 180);
        f.core.dock_boot_sampled = true;
        (void)ptc_sysmodule_enforce_tick(&f.core);
        (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 9ULL * 60000000000ULL, &carry);
        (void)ptc_sysmodule_enforce_tick(&f.core);
        expect(f.pctl.configured_minutes == 30 && f.pctl.status.remaining_minutes == 1 &&
            !f.pctl.status.restricted_now, "non-TV use is allowed only within the smaller daily total");
        (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 60000000000ULL, &carry);
        (void)ptc_sysmodule_enforce_tick(&f.core);
        expect(f.pctl.status.restricted_now && f.pctl.status.remaining_minutes == 0 &&
            load_state(&f.core, &state) && !state.dock_enforced &&
            state.undocked_used_ns == 10ULL * 60000000000ULL,
            "daily total exhausts while non-TV quota still has 170 minutes");
        request(&f, "total-exhausted", "status", "{}");
        expect(result_has(&f, "total-exhausted", "\"daily_allowance\":true") &&
            result_has(&f, "total-exhausted", "\"dock\":false"),
            "status attributes exhaustion to the daily total");
        mode.mode = PTC_OPERATION_MODE_DOCKED;
        (void)ptc_sysmodule_enforce_tick(&f.core);
        expect(f.pctl.status.restricted_now && f.pctl.status.remaining_minutes == 0,
            "connecting the dock does not refund exhausted daily total");
        request(&f, "total-waive", "waive_dock_policy_today", "{\"expected_day_index\":2380}");
        expect(result_has(&f, "total-waive", "\"status\":\"ok\"") &&
            load_state(&f.core, &state) && state.dock_waived &&
            f.pctl.configured_minutes == 30 && f.pctl.status.restricted_now &&
            f.pctl.status.remaining_minutes == 0,
            "today TV waiver succeeds without bypassing exhausted daily total");
        mode.mode = PTC_OPERATION_MODE_UNDOCKED;
        (void)ptc_sysmodule_enforce_tick(&f.core);
        expect(f.pctl.status.restricted_now && f.pctl.played_minutes_today == 30,
            "returning to non-TV after waiver preserves total consumption");
        free(f.mem);
    }
}

static void test_dock_quota_preserves_tv_total(void)
{
    for (int today_override = 0; today_override < 2; ++today_override) {
        for (unsigned quota = 0; quota <= 30; quota += 30) {
            Fixture f;
            PtcRules rules;
            PtcRuntimeState state;
            PtcOperationModeStatus mode = {PTC_OPERATION_MODE_UNDOCKED, true, true};
            PtcOperationModeProvider provider = {test_operation_mode, &mode};
            uint64_t carry = 0;
            init_fixture(&f);
            f.core.operation_mode_provider = &provider;
            ptc_pctl_stub_reset_daily_usage(&f.pctl);
            if (today_override) {
                expect(load_rules(&f.core, &rules), "read TV total override rules");
                for (unsigned i = 0; i < 7; ++i) rules.week[i].minutes = 60;
                rules.today_override.present = true;
                rules.today_override.day_index = f.clock.snapshot.day_index;
                rules.today_override.rule = (PtcDayRule){PTC_RULE_MODE_LIMIT, 120};
                expect(save_rules(&f.core, &rules), "save larger TV daily override");
            }
            (void)ptc_sysmodule_enforce_tick(&f.core);
            enable_dock_fixture(&f, false, (uint16_t)quota);
            f.core.dock_boot_sampled = true;
            (void)ptc_sysmodule_enforce_tick(&f.core);
            if (quota > 0) {
                (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, quota * 60000000000ULL, &carry);
                (void)ptc_sysmodule_enforce_tick(&f.core);
            }
            expect(f.pctl.status.blocked_today && load_state(&f.core, &state) && state.dock_enforced &&
                state.undocked_used_ns == quota * 60000000000ULL,
                "zero or exhausted handheld quota restricts handheld");
            for (unsigned i = 0; i < 2; ++i) {
                mode.mode = PTC_OPERATION_MODE_DOCKED;
                (void)ptc_sysmodule_enforce_tick(&f.core);
                expect(f.pctl.configured_minutes == 120 && f.pctl.status.remaining_minutes == 120 - quota &&
                    !f.pctl.status.restricted_now && load_state(&f.core, &state) && !state.dock_enforced &&
                    state.undocked_used_ns == quota * 60000000000ULL,
                    "docking restores full daily target with only actual use deducted");
                request(&f, "tv-total-available", "status", "{}");
                expect(result_has(&f, "tv-total-available", "\"daily_allowance\":false") &&
                    result_has(&f, "tv-total-available", "\"dock\":false"),
                    "TV status does not inherit exhausted handheld restriction");
                mode.mode = PTC_OPERATION_MODE_UNDOCKED;
                (void)ptc_sysmodule_enforce_tick(&f.core);
                expect(f.pctl.status.blocked_today && f.pctl.played_minutes_today == quota,
                    "undocking restores handheld restriction without refunding daily use");
            }
            mode.mode = PTC_OPERATION_MODE_DOCKED;
            (void)ptc_sysmodule_enforce_tick(&f.core);
            (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, (119 - quota) * 60000000000ULL, &carry);
            (void)ptc_sysmodule_enforce_tick(&f.core);
            expect(f.pctl.status.remaining_minutes == 1 && !f.pctl.status.restricted_now &&
                load_state(&f.core, &state) && state.undocked_used_ns == quota * 60000000000ULL,
                "TV can use daily total beyond handheld quota without spending handheld time");
            (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 60000000000ULL, &carry);
            (void)ptc_sysmodule_enforce_tick(&f.core);
            request(&f, "tv-total-exhausted", "status", "{}");
            expect(f.pctl.configured_minutes == 120 && f.pctl.played_minutes_today == 120 &&
                f.pctl.status.restricted_now && f.pctl.status.remaining_minutes == 0 &&
                result_has(&f, "tv-total-exhausted", "\"daily_allowance\":true") &&
                result_has(&f, "tv-total-exhausted", "\"dock\":false"),
                "TV stops at daily total and reports only daily exhaustion");
            free(f.mem);
        }
    }
}

static void confirm_dock_fixture(Fixture *f)
{
    PtcRules rules;
    char fingerprint[65];
    expect(f->mem->storage.vtable->write_text_atomic(&f->mem->storage, "app/environment.json",
        "{\"read_ok\":true,\"hos\":\"22.5.0\",\"model\":\"mariko-oled\",\"atmosphere\":true}"), "dock environment");
    expect(sysmodule_environment_fingerprint(&f->core, fingerprint) && load_rules(&f->core, &rules), "dock fingerprint");
    rules.bedtime.confirmation_version = 1;
    rules.bedtime.official_setting_confirmed_at = 1;
    rules.bedtime.unverified_overlay_risk_accepted = true;
    snprintf(rules.bedtime.confirmed_environment, sizeof(rules.bedtime.confirmed_environment), "%s", fingerprint);
    expect(save_rules(&f->core, &rules), "dock enable confirmation");
}

static void test_dock_accounting_failures(void)
{
    Fixture f;
    PtcRules rules;
    PtcRuntimeState state;
    PtcOperationModeStatus mode = {PTC_OPERATION_MODE_UNDOCKED, true, true};
    PtcOperationModeProvider provider = {test_operation_mode, &mode};
    uint64_t carry = 0;
    char code[16], payload[80];
    bool used;
    init_fixture(&f);
    f.core.operation_mode_provider = &provider;
    confirm_dock_fixture(&f);
    request(&f, "dock-first", "set_dock_policy", "{\"force_docked\":false,\"undocked_limit_enabled\":true,\"undocked_daily_minutes\":30}");
    expect(result_has(&f, "dock-first", "\"status\":\"ok\"") && load_state(&f.core, &state) &&
        state.undocked_used_ns == 0 && state.dock_baseline_known, "first enable excludes earlier console use");
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 10000000000ULL, &carry);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    f.clock.snapshot.unix_seconds += 3600;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.undocked_used_ns == 10000000000ULL, "sleep wall time is not consumption");
    ptc_sysmodule_init(&f.core, "app", &f.mem->storage, &f.pctl.pctl, &f.clock.provider);
    f.core.operation_mode_provider = &provider;
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 20000000000ULL, &carry);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.undocked_used_ns == 30000000000ULL, "restart preserves and charges reliable gap");
    mode.mode = PTC_OPERATION_MODE_DOCKED;
    f.pctl.read_error = PTC_ERR_PCTL_READ_FAILED;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && !state.dock_usage_known, "read failure marks quota unknown even on TV");
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 10000000000ULL, &carry);
    f.pctl.read_error = PTC_ERR_OK;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.dock_usage_known && state.undocked_used_ns == 40000000000ULL,
        "recovered reliable TV gap charges undocked conservatively");
    mode.mode = PTC_OPERATION_MODE_UNDOCKED;
    request(&f, "dock-zero", "set_dock_policy", "{\"force_docked\":false,\"undocked_limit_enabled\":true,\"undocked_daily_minutes\":0}");
    expect(result_has(&f, "dock-zero", "\"status\":\"ok\"") && f.pctl.status.blocked_today,
        "zero allowance restricts immediately");
    expect(ptc_token_v2_encode(3, 12, "kid-switch", SECRET, 2380, code) == PTC_ERR_OK, "encode dock test token");
    snprintf(payload, sizeof(payload), "{\"code\":\"%s\"}", code);
    request(&f, "dock-token", "offline_code", payload);
    expect(result_has(&f, "dock-token", "dock_active") &&
        check_nonce_used(&f.core, 2380, 12, 2, &used) == PTC_ERR_OK && !used, "dock rejection preserves nonce");
    f.mem->fail_write_path_contains_once = "results/dock-result-fail.json";
    request(&f, "dock-result-fail", "waive_dock_policy_today", "{\"expected_day_index\":2380}");
    expect(load_state(&f.core, &state) && !state.dock_waived && state.undocked_used_ns == 40000000000ULL,
        "result failure restores waiver and retains all usage");
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage, "app/flags/disable.flag", "test\n"), "dock protection flag");
    request(&f, "dock-protected-waive", "waive_dock_policy_today", "{\"expected_day_index\":2380}");
    expect(result_has(&f, "dock-protected-waive", "\"status\":\"ok\"") && !f.pctl.status.blocked_today &&
        f.mem->storage.vtable->exists(&f.mem->storage, "app/flags/disable.flag"), "protected waiver restores only dock target");
    free(f.mem);

    seed_direct_grant(&f, 20 * PTC_USAGE_MINUTE_NS, 120);
    mode.mode = PTC_OPERATION_MODE_UNDOCKED;
    f.core.operation_mode_provider = &provider;
    confirm_dock_fixture(&f);
    expect(load_rules(&f.core, &rules), "unlimited dock rules");
    for (unsigned i = 0; i < 7; ++i) rules.week[i].mode = PTC_RULE_MODE_UNLIMITED;
    expect(save_rules(&f.core, &rules), "seed unlimited dock");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    request(&f, "dock-unlimited-enable", "set_dock_policy", "{\"force_docked\":false,\"undocked_limit_enabled\":true,\"undocked_daily_minutes\":30}");
    expect(result_has(&f, "dock-unlimited-enable", "\"status\":\"ok\"") && f.pctl.status.unrestricted_today &&
        load_state(&f.core, &state) && state.dock_usage_known, "unlimited first enable uses direct counter without cap");
    request(&f, "dock-off", "set_dock_policy", "{\"force_docked\":false,\"undocked_limit_enabled\":false,\"undocked_daily_minutes\":0}");
    expect(result_has(&f, "dock-off", "\"status\":\"ok\"") && f.pctl.status.unrestricted_today,
        "turning off last timer feature restores unlimited");
    free(f.mem);
}

static void test_dock_composition_and_migration(void)
{
    Fixture f;
    PtcRules rules;
    PtcRuntimeState state;
    PtcPctlSettingsSnapshot snapshot;
    PtcOperationModeStatus mode = {PTC_OPERATION_MODE_DOCKED, true, true};
    PtcOperationModeProvider provider = {test_operation_mode, &mode};
    uint64_t carry = 0;
    init_fixture(&f);
    f.core.operation_mode_provider = &provider;
    enable_dock_fixture(&f, true, 30);
    f.core.dock_boot_sampled = true;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 20000000000ULL, &carry);
    mode.mode = PTC_OPERATION_MODE_UNDOCKED;
    f.pctl.write_error = PTC_ERR_PCTL_WRITE_FAILED;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.undocked_used_ns == 20000000000ULL && !state.dock_enforced,
        "failed mode restriction cannot refund observed usage");
    f.pctl.write_error = PTC_ERR_OK;
    mode.mode = PTC_OPERATION_MODE_DOCKED;
    expect(load_rules(&f.core, &rules) && load_state(&f.core, &state), "composed eye policy");
    rules.eye_care.enabled = true;
    state.eye_care_resting = true;
    state.eye_care_rest_deadline = f.clock.snapshot.unix_seconds + 600;
    state.eye_care_break_id = 123;
    expect(f.pctl.pctl.vtable->snapshot_settings(&f.pctl.pctl, &snapshot) == PTC_ERR_OK &&
        save_eye_care_snapshot(&f.core, &snapshot, f.clock.snapshot.unix_seconds), "composed eye base snapshot");
    expect(save_rules(&f.core, &rules) && save_state(&f.core, &state, f.clock.snapshot.unix_seconds), "persist eye rest");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    unsigned int blocked_writes = f.pctl.apply_target_calls;
    mode.mode = PTC_OPERATION_MODE_UNDOCKED;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(f.pctl.apply_target_calls == blocked_writes, "adding dock reason does not rewrite blocked target");
    request(&f, "dock-eye-waive", "waive_dock_policy_today", "{\"expected_day_index\":2380}");
    expect(result_has(&f, "dock-eye-waive", "\"status\":\"ok\"") && f.pctl.status.blocked_today &&
        load_state(&f.core, &state) && state.eye_care_resting, "dock waiver never ends an eye-care rest");
    expect(f.pctl.apply_target_calls == blocked_writes, "waiving dock under eye rest does not rewrite blocked target");
    free(f.mem);

    init_fixture(&f);
    f.core.operation_mode_provider = &provider;
    mode.mode = PTC_OPERATION_MODE_UNDOCKED;
    enable_dock_fixture(&f, false, 1);
    f.core.dock_boot_sampled = true;
    carry = 0;
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 60000000000ULL, &carry);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(f.pctl.status.blocked_today, "prior day quota exhausted without waiver");
    ++f.clock.snapshot.day_index;
    f.clock.snapshot.unix_seconds += 86400;
    ptc_pctl_stub_reset_daily_usage(&f.pctl);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.dock_usage_known && state.undocked_used_ns == 0 &&
        !state.dock_enforced && f.pctl.status.limited_today, "new day restores quota from prior blocked target");
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 10000000000ULL, &carry);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.undocked_used_ns == 10000000000ULL,
        "new day resumes precise undocked counting");
    free(f.mem);

    init_fixture(&f);
    f.core.operation_mode_provider = &provider;
    mode.mode = PTC_OPERATION_MODE_UNDOCKED;
    enable_dock_fixture(&f, false, 30);
    f.pctl.played_minutes_today = 120;
    f.pctl.status.remaining_minutes = 0;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    mode.mode = PTC_OPERATION_MODE_DOCKED;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(f.pctl.status.restricted_now && f.pctl.status.remaining_minutes == 0,
        "docking cannot replenish exhausted daily total");
    expect(load_state(&f.core, &state), "unknown interval state");
    state.dock_usage_known = state.dock_baseline_known = false;
    expect(save_state(&f.core, &state, f.clock.snapshot.unix_seconds), "seed unprovable interval");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && !state.dock_usage_known, "new TV baseline cannot prove lost consumption");
    free(f.mem);

    init_fixture(&f);
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage, "app/rules.json", "{\"version\":2}"), "seed old rules");
    expect(load_rules(&f.core, &rules) && !rules.dock_policy.force_docked && !rules.dock_policy.undocked_limit_enabled,
        "old rules default both policies off");
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage, "app/rules.json",
        "{\"version\":2,\"undocked_limit_enabled\":true}"), "seed partial rules");
    expect(load_rules(&f.core, &rules) && !rules.dock_policy.force_docked &&
        rules.dock_policy.undocked_daily_minutes == 30, "missing new rule fields retain individual defaults");
    expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage, "app/state.json",
        "{\"version\":1,\"dock_day_index\":2380,\"undocked_used_ns\":20000000000}"), "seed partial dock state");
    expect(load_state(&f.core, &state) && !state.dock_usage_known && state.undocked_used_ns == 20000000000ULL,
        "missing state preserves known counter without claiming reliable allowance");
    free(f.mem);
}

static void test_dock_midnight_allowance_change(void)
{
    for (int unreadable = 0; unreadable < 2; ++unreadable) {
        Fixture f;
        PtcRules rules;
        PtcRuntimeState state;
        PtcOperationModeStatus mode = {PTC_OPERATION_MODE_UNDOCKED, true, true};
        PtcOperationModeProvider provider = {test_operation_mode, &mode};
        uint64_t carry = 0;
        init_fixture(&f);
        f.core.operation_mode_provider = &provider;
        enable_dock_fixture(&f, false, 1410);
        (void)ptc_sysmodule_enforce_tick(&f.core);
        expect(load_rules(&f.core, &rules), "midnight load rules");
        rules.week[ptc_weekday_from_day_index(2381)].minutes = 60;
        expect(save_rules(&f.core, &rules), "midnight different daily total");
        f.clock.snapshot.day_index++;
        f.clock.snapshot.unix_seconds += 86400;
        f.clock.snapshot.minute_of_day = 10;
        ptc_pctl_stub_reset_daily_usage(&f.pctl);
        (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 10ULL * 60000000000ULL, &carry);
        if (unreadable) f.pctl.read_error = PTC_ERR_PCTL_READ_FAILED;
        (void)ptc_sysmodule_enforce_tick(&f.core);
        expect(load_state(&f.core, &state), "midnight load state");
        if (unreadable) {
            expect(!state.dock_usage_known, "midnight read failure must not invent usage");
        } else {
            expect(state.dock_usage_known && state.undocked_used_ns == 10ULL * 60000000000ULL &&
                !state.dock_enforced && f.pctl.configured_minutes == 60 &&
                f.pctl.status.remaining_minutes == 50, "00:10 large handheld quota survives daily target change");
            (void)ptc_sysmodule_enforce_tick(&f.core);
            expect(load_state(&f.core, &state) && state.undocked_used_ns == 10ULL * 60000000000ULL,
                "midnight rebaseline does not double count");
        }
        free(f.mem);
    }
}

static void test_eden_grant_sampling(void)
{
    Fixture f;
    PtcPctlStatus status;
    uint64_t carry = 0;
    init_fixture(&f);
    expect(ptc_eden_restore_timer(&f.core, &f.pctl, f.clock.snapshot, &carry),
        "Eden grant sampling starts from its simulated timer");
    f.clock.snapshot.unix_seconds++;
    expect(read_grant_status(&f.core, f.clock.snapshot, &status) == PTC_ERR_PCTL_READ_FAILED,
        "cached UI poll timestamp cannot authorize a later-second grant");
    expect(ptc_eden_read_grant_sample(&f.pctl, f.pctl.pctl.vtable, 0,
        f.clock.snapshot, 1000000000ULL, &carry, &status) == PTC_ERR_OK &&
        status.usage_captured_at == f.clock.snapshot.unix_seconds &&
        status.usage_ns == 30ULL * 60000000000ULL + 1000000000ULL,
        "Eden grant read advances and timestamps the actual cumulative sample");
    expect(read_grant_status(&f.core, f.clock.snapshot, &status) == PTC_ERR_OK,
        "fresh Eden sample passes unchanged production freshness guard");
    f.pctl.read_error = PTC_ERR_PCTL_READ_FAILED;
    expect(ptc_eden_read_grant_sample(&f.pctl, f.pctl.pctl.vtable, 0,
        f.clock.snapshot, 0, &carry, &status) == PTC_ERR_PCTL_READ_FAILED,
        "Eden read sampling never masks injected PCTL failure");
    f.clock.snapshot.day_index++;
    f.clock.snapshot.unix_seconds += 86400;
    expect(ptc_eden_refresh_usage(&f.pctl, f.clock.snapshot, 1000000000ULL, &carry) &&
        carry == 0 && f.pctl.played_minutes_today == 0 &&
        f.pctl.status.usage_day_index == f.clock.snapshot.day_index,
        "new-day grant sample resets the old-day counter and fraction");
    free(f.mem);
}

static void test_eden_timer_restart(void)
{
    Fixture f;
    PtcRuntimeState state;
    PtcOperationModeStatus mode = {PTC_OPERATION_MODE_UNDOCKED, true, true};
    PtcOperationModeProvider provider = {test_operation_mode, &mode};
    uint64_t carry = 0;
    init_fixture(&f);
    f.core.operation_mode_provider = &provider;
    enable_dock_fixture(&f, false, 1410);
    f.core.dock_boot_sampled = true;
    (void)ptc_pctl_stub_advance_usage_ns(&f.pctl, 41ULL * 60000000000ULL + 20000000000ULL, &carry);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.dock_usage_known, "restart sampled baseline");
    uint64_t used = state.undocked_used_ns;
    ptc_pctl_stub_init(&f.pctl);
    expect(ptc_eden_restore_timer(&f.core, &f.pctl, f.clock.snapshot, &carry) &&
        f.pctl.played_minutes_today == 61 && carry == 20000000000ULL,
        "Eden resumes sampled total and fractional minutes instead of fixed 30");
    f.core.dock_boot_sampled = false;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.dock_usage_known && !state.dock_enforced &&
        state.undocked_used_ns == used, "Eden restart retains handheld allowance");
    f.mem->fail_read_path_contains = "state.json";
    expect(!ptc_eden_restore_timer(&f.core, &f.pctl, f.clock.snapshot, &carry),
        "Eden unreadable state rejects guessed restart");
    f.mem->fail_read_path_contains = NULL;
    f.clock.snapshot.day_index++;
    f.clock.snapshot.unix_seconds += 86400;
    f.clock.snapshot.minute_of_day = 10;
    expect(ptc_eden_restore_timer(&f.core, &f.pctl, f.clock.snapshot, &carry) &&
        f.pctl.played_minutes_today == 10 && carry == 0,
        "Eden new-day seed cannot exceed minutes since midnight");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.dock_usage_known && !state.dock_enforced,
        "Eden 00:10 restart with 1410-minute quota permits play");
    free(f.mem);
}

static void test_eden_restored_restart(void)
{
    for (unsigned timer = 0; timer < 2; ++timer) {
        Fixture f;
        PtcPctlSettingsSnapshot original, observed;
        uint64_t carry = 0;
        init_fixture(&f);
        f.pctl.status.play_timer_enabled = timer != 0;
        expect(f.pctl.pctl.vtable->snapshot_settings(&f.pctl.pctl, &original) == PTC_ERR_OK &&
            save_install_snapshot(&f.core, &original, f.clock.snapshot.unix_seconds),
            "Eden restored restart retains an installation snapshot");
        request(&f, "eden-set-150", "set_today_limit", "{\"minutes\":150}");
        expect(result_has(&f, "eden-set-150", "\"status\":\"ok\""), "Eden restart normal policy seed succeeds");
        request(&f, "eden-install-restore", "restore_install_snapshot", "{}");
        expect(result_has(&f, "eden-install-restore", "\"status\":\"ok\""), "Eden restart installation restore succeeds");
        ptc_pctl_stub_init(&f.pctl);
        /* A policy apply must not be used to reconstruct a restored stub. */
        f.pctl.write_error = PTC_ERR_PCTL_WRITE_FAILED;
        expect(ptc_eden_restore_timer(&f.core, &f.pctl, f.clock.snapshot, &carry) &&
            f.pctl.configured_minutes == 120 && f.pctl.status.play_timer_enabled == (timer != 0),
            "Eden restored restart ignores the stale 150-minute policy and restores timer lifecycle");
        expect(f.pctl.pctl.vtable->snapshot_settings(&f.pctl.pctl, &observed) == PTC_ERR_OK &&
            ptc_pctl_settings_snapshot_equal(&original, &observed),
            "Eden restored restart reconstructs the original stub settings image");
        f.mem->fail_read_path_contains = "install_pctl_snapshot.json";
        expect(!ptc_eden_restore_timer(&f.core, &f.pctl, f.clock.snapshot, &carry),
            "Eden restored restart rejects unreadable snapshot without policy fallback");
        f.mem->fail_read_path_contains = NULL;
        f.pctl.restore_error = PTC_ERR_PCTL_RESTORE_FAILED;
        expect(!ptc_eden_restore_timer(&f.core, &f.pctl, f.clock.snapshot, &carry),
            "Eden restored restart rejects a failed restore without policy fallback");
        expect(f.mem->storage.vtable->exists(&f.mem->storage, "app/flags/disable.flag"),
            "Eden failed initialization never clears the disabled state");
        free(f.mem);
    }
}

static void direct_sample(Fixture *f, uint64_t ns, uint32_t seconds)
{
    f->clock.snapshot.unix_seconds += seconds;
    f->pctl.status.usage_supported = f->pctl.status.usage_available = true;
    f->pctl.status.usage_result = 0;
    f->pctl.status.usage_ns = ns;
    f->pctl.status.usage_day_index = f->clock.snapshot.day_index;
    f->pctl.status.usage_captured_at = f->clock.snapshot.unix_seconds;
    f->pctl.played_minutes_today = (uint16_t)(ns / PTC_USAGE_MINUTE_NS);
    f->pctl.usage_fraction_ns = ns % PTC_USAGE_MINUTE_NS;
    f->pctl.status.remaining_minutes = f->pctl.configured_minutes > f->pctl.played_minutes_today
        ? f->pctl.configured_minutes - f->pctl.played_minutes_today : 0;
}

static void test_usage_counter_epochs(void)
{
    PtcUsageCounter baseline = {1, true, 2380, 60 * PTC_USAGE_MINUTE_NS, 1000};
    PtcUsageCounter sample = {PTC_USAGE_SOURCE_PCTL_1952, true, 2380, 100 * PTC_USAGE_MINUTE_NS, 1001};
    uint64_t delta;
    expect(ptc_usage_counter_step(&baseline, &sample, &delta) == PTC_USAGE_BASELINE && delta == 0,
        "new source establishes epoch without subtracting legacy quota baseline");
    sample.total_ns += 1; ++sample.captured_at;
    expect(ptc_usage_counter_step(&baseline, &sample, &delta) == PTC_USAGE_DELTA && delta == 1,
        "counter preserves a one-nanosecond delta");
    PtcUsageCounter trusted = baseline;
    --sample.total_ns; ++sample.captured_at;
    expect(ptc_usage_counter_step(&baseline, &sample, &delta) == PTC_USAGE_UNKNOWN &&
        baseline.total_ns == trusted.total_ns && baseline.captured_at == trusted.captured_at,
        "decrease retains trusted counter rather than refunding usage");
    sample = trusted; sample.known = false;
    expect(ptc_usage_counter_step(&baseline, &sample, &delta) == PTC_USAGE_UNKNOWN,
        "failed sample retains epoch");
    sample = trusted; ++sample.day_index; ++sample.captured_at; sample.total_ns = 5;
    expect(ptc_usage_counter_step(&baseline, &sample, &delta) == PTC_USAGE_NEXT_DAY && delta == 5,
        "ordinary next day charges new-day total independently");
    --sample.day_index;
    expect(ptc_usage_counter_step(&baseline, &sample, &delta) == PTC_USAGE_UNKNOWN,
        "date rewind cannot reset allowance");
    sample = baseline; sample.day_index += 2; sample.captured_at += 2;
    expect(ptc_usage_counter_step(&baseline, &sample, &delta) == PTC_USAGE_UNKNOWN,
        "unexplained multi-day jump cannot create a new epoch");
    sample = baseline; ++sample.day_index; sample.captured_at += 129601;
    expect(ptc_usage_counter_step(&baseline, &sample, &delta) == PTC_USAGE_UNKNOWN,
        "long gap is not an observed natural midnight");
}

static void test_direct_eye_usage(void)
{
    Fixture f;
    PtcRules rules;
    PtcRuntimeState state, projected;
    PtcResultState result;
    seed_direct_grant(&f, 100 * PTC_USAGE_MINUTE_NS, 180);
    expect(load_rules(&f.core, &rules) && load_state(&f.core, &state), "load eye migration fixture");
    rules.eye_care.enabled = true;
    rules.eye_care.play_minutes = 40;
    state.eye_care_day_index = f.clock.snapshot.day_index;
    state.eye_care_accumulated_minutes = 30;
    state.eye_care_last_used_minutes = 60;
    state.eye_care_usage_known = true;
    expect(eye_care_sample_direct(&rules, &state, &f.pctl.status, f.clock.snapshot, false) &&
        state.eye_care_accumulated_minutes == 30 && state.eye_usage_migrated,
        "eye migration preserves cycle without inventing forty minutes");
    direct_sample(&f, 109 * PTC_USAGE_MINUTE_NS + UINT64_C(59000000000), 599);
    expect(eye_care_sample_direct(&rules, &state, &f.pctl.status, f.clock.snapshot, false) &&
        state.eye_cycle_used_ns == 39 * PTC_USAGE_MINUTE_NS + UINT64_C(59000000000),
        "eye cycle retains seconds below its threshold");
    expect(save_rules(&f.core, &rules) && save_state(&f.core, &state, f.clock.snapshot.unix_seconds),
        "persist nanosecond cycle before restart");
    PtcSysmodule restarted;
    ptc_sysmodule_init(&restarted, "app", &f.mem->storage, &f.pctl.pctl, &f.clock.provider);
    expect(load_state(&restarted, &projected) && projected.eye_cycle_used_ns == state.eye_cycle_used_ns &&
        projected.eye_usage_counter.total_ns == state.eye_usage_counter.total_ns,
        "same-day restart restores counter epoch and fractional cycle");
    direct_sample(&f, 110 * PTC_USAGE_MINUTE_NS, 1);
    f.pctl.write_error = PTC_ERR_PCTL_WRITE_FAILED;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && !state.eye_care_resting &&
        state.eye_cycle_used_ns == 40 * PTC_USAGE_MINUTE_NS &&
        state.eye_usage_counter.total_ns == 110 * PTC_USAGE_MINUTE_NS,
        "failed rest write rolls back rest but never observed consumption");
    f.pctl.write_error = PTC_ERR_OK;
    unsigned starts_before_retry = f.pctl.start_timer_calls;
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.eye_care_resting && f.pctl.status.blocked_today &&
        f.pctl.start_timer_calls == starts_before_retry, "retry enters rest at exact threshold without timer activation");
    uint64_t break_id = state.eye_care_break_id;
    ++f.clock.snapshot.day_index;
    direct_sample(&f, 0, 1);
    (void)ptc_sysmodule_enforce_tick(&f.core);
    expect(load_state(&f.core, &state) && state.eye_care_resting && state.eye_care_break_id == break_id,
        "midnight preserves existing rest instance and deadline");
    free(f.mem);

    seed_direct_grant(&f, 20 * PTC_USAGE_MINUTE_NS, 120);
    expect(load_rules(&f.core, &rules) && load_state(&f.core, &state), "load eye idle fixture");
    rules.eye_care.enabled = true;
    rules.eye_care.rest_minutes = 1;
    (void)eye_care_sample_direct(&rules, &state, &f.pctl.status, f.clock.snapshot, false);
    direct_sample(&f, 20 * PTC_USAGE_MINUTE_NS + UINT64_C(1000000000), 1);
    (void)eye_care_sample_direct(&rules, &state, &f.pctl.status, f.clock.snapshot, false);
    direct_sample(&f, f.pctl.status.usage_ns, 60);
    projected = state;
    result_state_from_pctl(&result, f.clock.snapshot.day_index, &f.pctl.status);
    fill_extended_result_state(&f.core, &result, &rules, &state, &f.pctl.status, f.clock.snapshot);
    expect(result.eye_care_used_ns == 0 && state.eye_cycle_used_ns == UINT64_C(1000000000),
        "status projects natural rest without persisting a baseline");
    (void)eye_care_sample_direct(&rules, &projected, &f.pctl.status, f.clock.snapshot, false);
    expect(projected.eye_cycle_used_ns == result.eye_care_used_ns, "status and scheduler use identical idle-reset evaluation");
    state = projected;
    direct_sample(&f, f.pctl.status.usage_ns + UINT64_C(1000000000), 1);
    (void)eye_care_sample_direct(&rules, &state, &f.pctl.status, f.clock.snapshot, false);
    f.pctl.status.usage_available = false;
    expect(!eye_care_sample_direct(&rules, &state, &f.pctl.status, f.clock.snapshot, false) &&
        state.eye_care_idle_since == 0, "unknown interval cannot prove natural rest");
    direct_sample(&f, f.pctl.status.usage_ns + UINT64_C(1000000000), 60);
    (void)eye_care_sample_direct(&rules, &state, &f.pctl.status, f.clock.snapshot, false);
    expect(state.eye_cycle_used_ns == UINT64_C(2000000000), "recovered counter adds missed delta without claiming rest");
    f.pctl.status.temporary_unlocked = true;
    direct_sample(&f, f.pctl.status.usage_ns, 60);
    expect(!eye_care_sample_direct(&rules, &state, &f.pctl.status, f.clock.snapshot, false) &&
        state.eye_cycle_used_ns == UINT64_C(2000000000), "PIN unlock does not count as eye rest");
    f.pctl.status.temporary_unlocked = false;
    ++f.clock.snapshot.day_index;
    direct_sample(&f, UINT64_C(3000000000), 1);
    expect(eye_care_sample_direct(&rules, &state, &f.pctl.status, f.clock.snapshot, false) &&
        state.eye_cycle_used_ns == UINT64_C(5000000000) && state.eye_usage_boundary_gap,
        "natural midnight retains cycle, adds new use and flags unobserved boundary tail");
    expect(save_state(&f.core, &state, f.clock.snapshot.unix_seconds) && load_state(&f.core, &projected) &&
        projected.eye_usage_boundary_gap, "boundary quality survives restart");
    ++f.clock.snapshot.day_index;
    direct_sample(&f, 0, 120);
    expect(eye_care_sample_direct(&rules, &state, &f.pctl.status, f.clock.snapshot, false) &&
        state.eye_cycle_used_ns == UINT64_C(5000000000) && state.eye_usage_boundary_gap,
        "midnight zero cannot certify idle time across an unobserved old-day tail");
    f.pctl.status.usage_supported = false;
    expect(!eye_care_sample_direct(&rules, &state, &f.pctl.status, f.clock.snapshot, false),
        "direct epoch never falls back to legacy quota estimate");
    free(f.mem);
}

static void test_direct_dock_usage(void)
{
    Fixture f;
    PtcRuntimeState state, restored;
    seed_direct_grant(&f, 100 * PTC_USAGE_MINUTE_NS, 180);
    expect(load_state(&f.core, &state), "load direct dock fixture");
    state.dock_tracking_started = state.dock_usage_known = state.dock_baseline_known = true;
    state.dock_day_index = f.clock.snapshot.day_index;
    state.undocked_used_ns = 30 * PTC_USAGE_MINUTE_NS;
    state.dock_last_used_ns = 60 * PTC_USAGE_MINUTE_NS;
    dock_sample_direct(&state, &f.pctl.status, f.clock.snapshot, PTC_OPERATION_MODE_UNDOCKED, false);
    expect(state.undocked_used_ns == 30 * PTC_USAGE_MINUTE_NS && state.dock_usage_known,
        "dock migration preserves existing amount without subtracting across sources");
    f.pctl.status.configured_minutes = 0;
    f.pctl.status.remaining_minutes = 0;
    direct_sample(&f, 100 * PTC_USAGE_MINUTE_NS + UINT64_C(20000000000), 20);
    dock_sample_direct(&state, &f.pctl.status, f.clock.snapshot, PTC_OPERATION_MODE_DOCKED, false);
    expect(state.undocked_used_ns == 30 * PTC_USAGE_MINUTE_NS + UINT64_C(20000000000),
        "BLOCKED zero quota cannot change the independent counter or refund usage");
    direct_sample(&f, f.pctl.status.usage_ns + UINT64_C(20000000000), 20);
    dock_sample_direct(&state, &f.pctl.status, f.clock.snapshot, PTC_OPERATION_MODE_DOCKED, false);
    expect(state.undocked_used_ns == 30 * PTC_USAGE_MINUTE_NS + UINT64_C(20000000000),
        "stable TV endpoints exclude new TV delta");
    expect(save_state(&f.core, &state, f.clock.snapshot.unix_seconds) && load_state(&f.core, &restored) &&
        restored.dock_usage_counter.total_ns == state.dock_usage_counter.total_ns,
        "dock counter persists independently from clamped legacy shadow");
    direct_sample(&f, f.pctl.status.usage_ns + UINT64_C(5000000000), 5);
    dock_sample_direct(&restored, &f.pctl.status, f.clock.snapshot, PTC_OPERATION_MODE_DOCKED, true);
    expect(restored.undocked_used_ns == state.undocked_used_ns + UINT64_C(5000000000),
        "restart gap is conservatively charged even between TV endpoints");
    state = restored;
    f.pctl.status.usage_available = false;
    dock_sample_direct(&state, &f.pctl.status, f.clock.snapshot, PTC_OPERATION_MODE_DOCKED, false);
    expect(!state.dock_usage_known && state.dock_usage_counter.known && state.dock_interval_unknown,
        "failed read retains trusted counter and blocks uncertain small-screen allowance");
    direct_sample(&f, f.pctl.status.usage_ns + UINT64_C(5000000000), 5);
    dock_sample_direct(&state, &f.pctl.status, f.clock.snapshot, PTC_OPERATION_MODE_DOCKED, false);
    expect(state.dock_usage_known && state.undocked_used_ns == restored.undocked_used_ns + UINT64_C(5000000000),
        "recovery conservatively charges missed delta");
    PtcUsageCounter trusted = state.dock_usage_counter;
    direct_sample(&f, f.pctl.status.usage_ns - UINT64_C(1000000000), 1);
    dock_sample_direct(&state, &f.pctl.status, f.clock.snapshot, PTC_OPERATION_MODE_UNDOCKED, false);
    expect(!state.dock_usage_known && state.dock_usage_counter.total_ns == trusted.total_ns,
        "counter rollback cannot clear accumulated small-screen consumption");
    ++f.clock.snapshot.day_index;
    direct_sample(&f, UINT64_C(3000000000), 1);
    dock_sample_direct(&state, &f.pctl.status, f.clock.snapshot, PTC_OPERATION_MODE_DOCKED, false);
    expect(state.dock_usage_known && state.undocked_used_ns == UINT64_C(3000000000),
        "next-day total is conservatively charged without old-day quota alias");
    f.clock.snapshot.day_index -= 2;
    direct_sample(&f, 0, 1);
    dock_sample_direct(&state, &f.pctl.status, f.clock.snapshot, PTC_OPERATION_MODE_UNDOCKED, false);
    expect(!state.dock_usage_known && state.undocked_used_ns == UINT64_C(3000000000),
        "date rewind does not refund daily consumption");
    free(f.mem);
}

static size_t read_usage_rows(Fixture *f, PtcDailySummaryRecord *records)
{
    char *text = calloc(1, PTC_DAILY_SUMMARY_FILE_SIZE);
    size_t count = 0;
    expect(text && f->mem->storage.vtable->read_text(&f->mem->storage,
        "app/stats/daily-summaries.jsonl", text, PTC_DAILY_SUMMARY_FILE_SIZE), "read usage rows");
    if (text) {
        char *line = text;
        while (*line && count < PTC_DAILY_SUMMARY_MAX_RECORDS) {
            char *end = strchr(line, '\n');
            if (end) *end = '\0';
            expect(ptc_daily_summary_parse_line(line, &records[count]), "parse stored usage row");
            ++count;
            if (!end) break;
            line = end + 1;
        }
    }
    free(text);
    return count;
}

static void test_direct_usage_history(void)
{
    Fixture f;
    PtcDailySummaryRecord rows[PTC_DAILY_SUMMARY_MAX_RECORDS], saved, parsed, duplicate[3];
    PtcDailySummaryAggregate aggregate;
    char line[PTC_DAILY_SUMMARY_LINE_SIZE], json[8192];
    init_fixture(&f);
    expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1, "save pre-migration quota estimate");
    direct_sample(&f, UINT64_C(4191000000001), 1);
    expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1 && read_usage_rows(&f, rows) == 2,
        "same-day v1 estimate and v2 direct record coexist");
    saved = rows[1];
    expect(!rows[0].direct_source && saved.direct_source && saved.consumed_available &&
        saved.total_ns == UINT64_C(4191000000001) && saved.consumed_minutes == 69 &&
        saved.captured_at == f.pctl.status.usage_captured_at, "direct history exceeds quota with exact nanoseconds");
    f.clock.snapshot.unix_seconds += 5;
    expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1, "cached direct sample stores receipt separately");
    read_usage_rows(&f, rows);
    expect(rows[1].captured_at == saved.captured_at && rows[1].observed_at == f.clock.snapshot.unix_seconds,
        "cached sample never re-ages its original capture");
    f.pctl.status.blocked_today = true; f.pctl.configured_minutes = 0;
    direct_sample(&f, saved.total_ns + UINT64_C(1000000000), 1);
    expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1, "BLOCKED zero configuration still records console usage");
    f.pctl.status.unrestricted_today = true; f.pctl.status.limited_today = false;
    direct_sample(&f, saved.total_ns + UINT64_C(2000000000), 1);
    expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1, "unlimited history is independent of allowance estimate");
    read_usage_rows(&f, rows); saved = rows[1];
    f.pctl.status.usage_available = false;
    f.clock.snapshot.unix_seconds += 60;
    expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1, "failed 1952 sample records read quality");
    read_usage_rows(&f, rows);
    expect(rows[1].total_ns == saved.total_ns && rows[1].captured_at == saved.captured_at &&
        rows[1].consumed_available && !rows[1].latest_available, "read failure preserves reliable lower bound and capture");
    f.pctl.read_error = PTC_ERR_PCTL_READ_FAILED;
    ++f.clock.snapshot.unix_seconds;
    expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1, "total service failure marks existing direct history");
    f.pctl.read_error = PTC_ERR_OK;
    direct_sample(&f, saved.total_ns - 1, 1);
    expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1, "decreasing total is recorded as failed observation");
    read_usage_rows(&f, rows);
    expect(rows[1].total_ns == saved.total_ns && !rows[1].latest_available,
        "decrease cannot overwrite the historical lower bound");
    direct_sample(&f, PTC_DAILY_SUMMARY_DAY_NS + 1, 1);
    expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1, "out-of-range day total produces unknown sample");
    read_usage_rows(&f, rows);
    expect(rows[1].total_ns == saved.total_ns && rows[1].consumed_minutes != 0,
        "out-of-range consumption cannot become known zero");
    direct_sample(&f, saved.total_ns + 1, 1);
    expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1, "valid monotonic recovery replaces the lower bound");
    read_usage_rows(&f, rows); saved = rows[1];
    expect(saved.latest_available, "successful recovery clears only the latest read failure");
    ptc_daily_summary_aggregate(rows, 2, f.clock.snapshot.day_index, &aggregate);
    expect(aggregate.consumed_minutes_7 == 20 && aggregate.direct_days_7 == 1 &&
        aggregate.direct_ns_7 == saved.total_ns, "v1 estimates and direct nanoseconds aggregate separately");
    request(&f, "history-status", "status", "{}");
    expect(f.mem->storage.vtable->read_text(&f.mem->storage, "app/results/history-status.json", json, sizeof(json)) &&
        strstr(json, "\"direct\":{\"source\":\"pctl_1952\"") && strstr(json, "\"coverage\":\"partial\""),
        "result reports typed partial console history");
    expect(ptc_daily_summary_format_line(line, sizeof(line), &saved) &&
        ptc_daily_summary_parse_line(line, &parsed) && parsed.total_ns == saved.total_ns,
        "v2 decimal string roundtrip preserves subminute nanoseconds");
    for (unsigned bad = 0; bad < 7; ++bad) {
        cJSON *root = cJSON_Parse(line);
        if (bad == 0) cJSON_SetNumberValue(cJSON_GetObjectItem(root, "version"), 2.5);
        if (bad == 1) cJSON_SetNumberValue(cJSON_GetObjectItem(root, "version"), 1);
        if (bad == 2) cJSON_ReplaceItemInObject(root, "total_ns", cJSON_CreateString("86400000000001"));
        if (bad == 3) cJSON_ReplaceItemInObject(root, "total_ns", cJSON_CreateString("01"));
        if (bad == 4) cJSON_DeleteItemFromObject(root, "scope");
        if (bad == 5) cJSON_SetNumberValue(cJSON_GetObjectItem(root, "consumed_minutes"), 0);
        if (bad == 6) cJSON_AddNumberToObject(root, "version", 2);
        char *invalid = cJSON_PrintUnformatted(root);
        expect(invalid && !ptc_daily_summary_parse_line(invalid, &parsed), "malformed typed history fails closed");
        free(invalid); cJSON_Delete(root);
    }
    duplicate[0] = saved;
    duplicate[0].consumed_available = duplicate[0].latest_available = false;
    duplicate[0].total_ns = duplicate[0].captured_at = duplicate[0].consumed_minutes = 0;
    duplicate[0].observed_at += 20;
    duplicate[1] = saved; duplicate[2] = saved;
    --duplicate[2].day_index; duplicate[2].total_ns = UINT64_C(59000000000);
    duplicate[2].consumed_minutes = 0;
    ptc_daily_summary_aggregate(duplicate, 3, saved.day_index, &aggregate);
    expect(aggregate.direct_days_7 == 2 && aggregate.direct_ns_7 == saved.total_ns + UINT64_C(59000000000) &&
        !aggregate.latest_available && aggregate.last_captured_at == saved.captured_at,
        "out-of-order unknown duplicates retain old captures and sum before rounding");
    f.mem->fail_write_path_contains = "daily-summaries";
    direct_sample(&f, saved.total_ns + 2, 1);
    expect(usage_summary_tick(&f.core, f.clock.snapshot) == 0, "summary storage failure is best effort");
    read_usage_rows(&f, rows);
    expect(rows[1].total_ns == saved.total_ns, "failed write preserves original rows");
    f.mem->fail_write_path_contains = NULL;
    for (unsigned day = 1; day <= 30; ++day) {
        ++f.clock.snapshot.day_index;
        f.pctl.status.usage_supported = false;
        f.pctl.status.unrestricted_today = false; f.pctl.status.limited_today = true;
        f.pctl.played_minutes_today = 1;
        expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1, "retain old-source date before native migration");
        direct_sample(&f, UINT64_C(59000000000), 86400);
        expect(usage_summary_tick(&f.core, f.clock.snapshot) == 1, "bounded daily history retention writes");
    }
    size_t count = read_usage_rows(&f, rows);
    expect(count == 60 && rows[0].day_index == saved.day_index + 1,
        "31st date evicts both sources of the oldest day together");
    ptc_daily_summary_aggregate(rows, count, f.clock.snapshot.day_index, &aggregate);
    expect(aggregate.direct_days_30 == 30 && aggregate.direct_days_7 == 7 &&
        aggregate.direct_ns_30 / PTC_USAGE_MINUTE_NS == 29 && aggregate.known_days_30 == 30,
        "fractional days sum without per-day minute loss or combining old estimates");
    free(f.mem);
}

static void test_usage_state_validation(void)
{
    Fixture f;
    PtcRuntimeState state, restored;
    char json[4096];
    init_fixture(&f);
    expect(load_state(&f.core, &state), "load source shape fixture");
    state.eye_usage_counter = (PtcUsageCounter){PTC_USAGE_SOURCE_PCTL_1952, true, 2380,
        100 * PTC_USAGE_MINUTE_NS, f.clock.snapshot.unix_seconds};
    state.eye_cycle_used_ns = 30 * PTC_USAGE_MINUTE_NS;
    state.eye_care_accumulated_minutes = 30;
    state.dock_usage_counter = state.eye_usage_counter;
    state.dock_usage_known = false;
    state.dock_baseline_known = true;
    state.dock_usage_incomplete = true;
    expect(save_state(&f.core, &state, f.clock.snapshot.unix_seconds) &&
        load_state(&f.core, &restored) && restored.dock_usage_counter.known && !restored.dock_usage_known,
        "counter continuity and policy availability are distinct persisted booleans");
    expect(f.mem->storage.vtable->read_text(&f.mem->storage, "app/state.json", json, sizeof(json)),
        "read serialized source groups");
    cJSON *root = cJSON_Parse(json);
    bool unique = root != NULL;
    for (cJSON *item = root ? root->child : NULL; item; item = item->next)
        for (cJSON *other = item->next; other; other = other->next)
            if (strcmp(item->string, other->string) == 0) unique = false;
    expect(unique, "serialized state has no duplicate keys");
    cJSON_Delete(root);
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        root = cJSON_Parse(json);
        if (invalid == 0) cJSON_SetNumberValue(cJSON_GetObjectItem(root, "eye_usage_source"), 2.5);
        if (invalid == 1) cJSON_DeleteItemFromObject(root, "dock_counter_source");
        if (invalid == 2) cJSON_DeleteItemFromObject(root, "eye_usage_captured_at");
        if (invalid == 3) cJSON_SetNumberValue(cJSON_GetObjectItem(root, "eye_cycle_used_ns"), 1);
        char *bad = cJSON_PrintUnformatted(root);
        expect(bad && f.mem->storage.vtable->write_text_atomic(&f.mem->storage, "app/state.json", bad) &&
            !load_state(&f.core, &restored), "partial, fractional or inconsistent epoch state rejects defaults");
        free(bad);
        cJSON_Delete(root);
    }
    free(f.mem);
}

#include "test_direct_unlimited.inc"

static void test_eye_rest_scheduler_deadline(void)
{
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        Fixture f;
        PtcRules rules;
        PtcRuntimeState state;
        char build[256];
        const char *environment = "{\"read_ok\":true,\"hos\":\"22.5.0\","
            "\"model\":\"mariko-oled\",\"firmware_hash\":\"fixture\","
            "\"atmosphere\":true,\"atmosphere_version\":\"1.11.2\",\"release_id\":\"fixture\"}";
        seed_direct_grant(&f, 20 * PTC_USAGE_MINUTE_NS, 120);
        snprintf(build, sizeof(build), "{\"profile\":\"%s\",\"playwise_version\":\"%s\","
            "\"release_id\":\"fixture\"}", PLAYWISE_PROFILE_NAME, PLAYWISE_VERSION);
        expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage, "app/build.json", build) &&
            f.mem->storage.vtable->write_text_atomic(&f.mem->storage, "app/environment.json", environment) &&
            f.mem->storage.vtable->write_text_atomic(&f.mem->storage, "app/compatibility.json", environment),
            "seed accepted scheduler environment");
        expect(load_rules(&f.core, &rules), "load scheduler rest rules");
        rules.eye_care.enabled = true;
        rules.eye_care.play_minutes = 40;
        rules.eye_care.rest_minutes = 2;
        expect(save_rules(&f.core, &rules) && load_state(&f.core, &state), "seed scheduler rest policy");
        int64_t deadline = f.clock.snapshot.unix_seconds + 17;
        state.eye_care_resting = true;
        state.eye_care_rest_deadline = deadline;
        state.eye_care_break_id = 4242;
        expect(save_state(&f.core, &state, f.clock.snapshot.unix_seconds), "persist non-minute rest deadline");
        (void)ptc_sysmodule_scheduler_tick(&f.core, false);
        expect(f.pctl.status.blocked_today, "scheduler applies initial rest");
        f.clock.snapshot.unix_seconds = deadline - 1;
        f.core.scan_backoff_ms = 5000;
        expect(ptc_sysmodule_next_wait_ms(&f.core) == 1000, "scheduler wakes at rest deadline before next minute");
        (void)ptc_sysmodule_scheduler_tick(&f.core, false);
        expect(load_state(&f.core, &state) && state.eye_care_resting && f.pctl.status.blocked_today,
            "rest never clears one second early");
        unsigned calls = f.pctl.start_timer_calls;
        unsigned writes = f.pctl.apply_target_calls;
        f.clock.snapshot.unix_seconds = deadline;
        if (scenario == 1) f.pctl.write_error = PTC_ERR_PCTL_WRITE_FAILED;
        if (scenario == 2)
            expect(f.mem->storage.vtable->write_text_atomic(&f.mem->storage,
                "app/flags/disable.flag", "fixture\n"), "disable controls before deadline");
        (void)ptc_sysmodule_scheduler_tick(&f.core, false);
        if (scenario) {
            expect(load_state(&f.core, &state) && state.eye_care_resting && f.pctl.status.blocked_today,
                "failed or disabled deadline recovery retains rest");
            if (scenario == 1) expect(f.pctl.apply_target_calls > writes, "deadline recovery attempts its failed write");
            expect(ptc_sysmodule_next_wait_ms(&f.core) >= 500,
                "failed or disabled overdue rest retains bounded scan backoff");
            /* Exact rollback may restore the saved timer. The subsequent
               successful deadline retry must not introduce another start. */
            calls = f.pctl.start_timer_calls;
            f.pctl.write_error = PTC_ERR_OK;
            if (scenario == 2) (void)f.mem->storage.vtable->remove_path(&f.mem->storage, "app/flags/disable.flag");
            (void)ptc_sysmodule_scheduler_tick(&f.core, false);
        }
        expect(load_state(&f.core, &state) && !state.eye_care_resting && !f.pctl.status.blocked_today &&
            f.pctl.last_target.mode == PTC_PCTL_TARGET_LIMIT && f.pctl.last_target.minutes == 120,
            "rest expires on its deadline without request, notification or minute change");
        expect(f.pctl.start_timer_calls == calls, "direct deadline recovery never starts 1451");
        free(f.mem);
    }
}

int ptc_test_control_safety(void)
{
    failures = 0;
    test_snapshot_failure_stops_rule_writes();
    test_direct_usage_projection();
    test_eye_rest_scheduler_deadline();
    test_direct_unlimited_lifecycle();
    test_direct_unlimited_reason_recovery();
    test_eden_direct_unlimited_counter();
    test_positive_subminute_runtime();
    test_direct_grants();
    test_direct_grant_rollbacks();
    test_direct_grant_clock_and_field_failures();
    test_usage_counter_epochs();
    test_direct_eye_usage();
    test_direct_dock_usage();
    test_usage_state_validation();
    test_direct_usage_history();
    test_dock_midnight_allowance_change();
    test_eden_timer_restart();
    test_eden_restored_restart();
    test_eden_grant_sampling();
    test_persisted_eden_mode();
    test_dock_policy();
    test_dock_daily_total_priority();
    test_dock_quota_preserves_tv_total();
    test_dock_accounting_failures();
    test_dock_composition_and_migration();
    test_ledger();
    test_real_storage_ledger();
    test_transaction_and_read_failures();
    test_eye_care_continuity_and_recovery();
    test_eye_care_rest_deadline_survives_backward_clock();
    test_eye_care_grants_unblock_after_deadline();
    test_sleep_without_scheduler_ticks();
    test_bedtime_result_rollback();
    test_cross_night_bedtime_does_not_start_timer();
    test_same_day_bedtime_timer_recovery();
    test_no_blocked_activation_and_recovery_ui();
    return failures;
}
