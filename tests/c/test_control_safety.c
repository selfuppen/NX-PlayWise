#include "../../sysmodule/sysmodule_internal.h"
#include "../../platform/host/mem_storage.h"
#include "../../platform/host/pctl_stub.h"
#include "../../platform/host/operation_mode_file.h"
#include "../../platform/host/fake_time.h"
#include "../../platform/switch/fs_storage.h"
#include "../../companion/result_summary.h"
#include "../../companion/overlay/bridge.h"

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

    init_fixture(&f);
    mode.mode = PTC_OPERATION_MODE_UNDOCKED;
    f.core.operation_mode_provider = &provider;
    confirm_dock_fixture(&f);
    expect(load_rules(&f.core, &rules), "unlimited dock rules");
    for (unsigned i = 0; i < 7; ++i) rules.week[i].mode = PTC_RULE_MODE_UNLIMITED;
    expect(save_rules(&f.core, &rules), "seed unlimited dock");
    (void)ptc_sysmodule_enforce_tick(&f.core);
    request(&f, "dock-unlimited-enable", "set_dock_policy", "{\"force_docked\":false,\"undocked_limit_enabled\":true,\"undocked_daily_minutes\":30}");
    expect(result_has(&f, "dock-unlimited-enable", "\"status\":\"ok\"") && f.pctl.configured_minutes == 1440 &&
        load_state(&f.core, &state) && state.dock_usage_known, "unlimited first enable installs reliable timer cap");
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

int ptc_test_control_safety(void)
{
    failures = 0;
    test_persisted_eden_mode();
    test_dock_policy();
    test_dock_accounting_failures();
    test_dock_composition_and_migration();
    test_ledger();
    test_real_storage_ledger();
    test_transaction_and_read_failures();
    test_eye_care_continuity_and_recovery();
    test_sleep_without_scheduler_ticks();
    test_bedtime_result_rollback();
    test_no_blocked_activation_and_recovery_ui();
    return failures;
}
