#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../common/protocol/request_schema.h"
#include "../../common/protocol/atmosphere_version.h"
#include "../../device_lab/boot_flags.h"
#include "../../device_lab/ui_model.h"
#include "../../platform/host/fake_time.h"
#include "../../platform/host/mem_storage.h"
#include "../../platform/host/pctl_stub.h"
#include "../../platform/switch/play_timer_settings_layout.h"
#include "../../sysmodule/lab_session.h"

static int failures;

static void check(bool condition, const char *label)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", label); ++failures; }
}

static PtcRequest request(PtcRequestType type, const char *id, const char *phase, const char *observation)
{
    PtcRequest value;
    memset(&value, 0, sizeof(value));
    value.type = type;
    snprintf(value.request_id, sizeof(value.request_id), "%s", id);
    snprintf(value.type_text, sizeof(value.type_text), "%s", ptc_request_type_name(type));
    if (phase) snprintf(value.phase, sizeof(value.phase), "%s", phase);
    if (observation) {
        snprintf(value.observation, sizeof(value.observation), "%s", observation);
        snprintf(value.runtime_effect, sizeof(value.runtime_effect), "unsure");
    }
    return value;
}

static void init_lab(PtcSysmodule *sysmodule, PtcMemStorage *storage, PtcPctlStub *pctl, PtcFakeTime *time)
{
    memset(sysmodule, 0, sizeof(*sysmodule));
    snprintf(sysmodule->app_root, sizeof(sysmodule->app_root), "/lab");
    snprintf(sysmodule->boot_id, sizeof(sysmodule->boot_id), "test-boot");
    sysmodule->storage = ptc_mem_storage_as_storage(storage);
    sysmodule->pctl = ptc_pctl_stub_as_pctl(pctl);
    sysmodule->time_provider = ptc_fake_time_as_provider(time);
}

static void test_protocol(void)
{
    PtcRequest parsed;
    static const char *const observations[] = {
        "restriction_visible", "no_visible_restriction", "unsure"
    };
    size_t i;
    check(ptc_request_parse("{\"version\":1,\"request_id\":\"s0\",\"type\":\"lab_session_start\",\"created_at\":1,\"payload\":{}}", &parsed) == PTC_ERR_OK &&
            strcmp(parsed.lab_mode, "full") == 0,
        "legacy Lab session start defaults to full mode");
    check(ptc_request_parse("{\"version\":1,\"request_id\":\"s1\",\"type\":\"lab_session_start\",\"created_at\":1,\"payload\":{\"mode\":\"restriction_quick\"}}", &parsed) == PTC_ERR_OK &&
            strcmp(parsed.lab_mode, "restriction_quick") == 0,
        "focused restriction mode parses explicitly");
    check(ptc_request_parse("{\"version\":1,\"request_id\":\"sa\",\"type\":\"lab_session_start\",\"created_at\":1,\"payload\":{\"mode\":\"timer_activation_ab\"}}", &parsed) == PTC_ERR_OK &&
            strcmp(parsed.lab_mode, "timer_activation_ab") == 0,
        "timer activation A/B mode parses explicitly");
    check(ptc_request_parse("{\"version\":1,\"request_id\":\"s2\",\"type\":\"lab_session_start\",\"created_at\":1,\"payload\":{\"mode\":\"skip\"}}", &parsed) == PTC_ERR_BAD_REQUEST,
        "unknown Lab mode is rejected");
    check(ptc_request_parse("{\"version\":1,\"request_id\":\"p1\",\"type\":\"lab_phase_start\",\"created_at\":1,\"payload\":{\"phase\":\"sleep_wake\"}}", &parsed) == PTC_ERR_OK,
        "valid Lab phase parses");
    check(strcmp(parsed.phase, "sleep_wake") == 0, "Lab phase is retained");
    check(ptc_request_parse("{\"version\":1,\"request_id\":\"pa\",\"type\":\"lab_phase_start\",\"created_at\":1,\"payload\":{\"phase\":\"ab_unlimited_settings_only\"}}", &parsed) == PTC_ERR_OK &&
            strcmp(parsed.phase, "ab_unlimited_settings_only") == 0,
        "timer activation A/B target phase parses");
    check(ptc_request_parse("{\"version\":1,\"request_id\":\"p2\",\"type\":\"lab_phase_start\",\"created_at\":1,\"payload\":{\"phase\":\"skip\"}}", &parsed) == PTC_ERR_BAD_REQUEST,
        "unknown Lab phase is rejected");
    for (i = 0; i < sizeof(observations) / sizeof(observations[0]); ++i) {
        char json[320];
        snprintf(json, sizeof(json),
            "{\"version\":1,\"request_id\":\"o%u\",\"type\":\"lab_observation\",\"created_at\":1,"
            "\"payload\":{\"observation\":\"%s\"}}", (unsigned int)i, observations[i]);
        check(ptc_request_parse(json, &parsed) == PTC_ERR_OK, "each tri-state observation parses");
        check(strcmp(parsed.runtime_effect, "unsure") == 0, "legacy observation defaults runtime effect to unsure");
    }
    check(ptc_request_parse("{\"version\":1,\"request_id\":\"oe\",\"type\":\"lab_observation\",\"created_at\":1,\"payload\":{\"observation\":\"restriction_visible\",\"runtime_effect\":\"exited\"}}", &parsed) == PTC_ERR_OK &&
            strcmp(parsed.runtime_effect, "exited") == 0,
        "two-layer manual observation retains actual runtime effect");
    check(ptc_request_parse("{\"version\":1,\"request_id\":\"ob\",\"type\":\"lab_observation\",\"created_at\":1,\"payload\":{\"observation\":\"restriction_visible\",\"runtime_effect\":\"prompt_only\"}}", &parsed) == PTC_ERR_BAD_REQUEST,
        "unknown runtime effect is rejected");
}

static void test_session_timing_order_restart_and_restore_failure(void)
{
    PtcMemStorage storage;
    PtcPctlStub pctl;
    PtcFakeTime time;
    PtcSysmodule first;
    PtcSysmodule restarted;
    PtcRequest item;
    char text[8192];
    int phase;
    static const char *const phases[] = {
        "home_stopped", "home_started", "game_foreground", "game_suspended", "sleep_wake", "restriction_effect"
    };
    ptc_mem_storage_init(&storage);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&time, 2000000000LL, 3000, 600);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 120;
    pctl.status.play_timer_enabled = true;
    init_lab(&first, &storage, &pctl, &time);

    item = request(PTC_REQUEST_LAB_SESSION_START, "start", NULL, NULL);
    check(ptc_lab_process_request(&first, &item), "session start handled");
    check(storage.storage.vtable->exists(&storage.storage, "/lab/lab/report-2000000000-test-boot.draft.json") &&
            !storage.storage.vtable->exists(&storage.storage, "/lab/reports/2000000000-test-boot.json"),
        "session start creates only a clearly named draft report");

    item = request(PTC_REQUEST_LAB_PHASE_START, "wrong", "home_started", NULL);
    (void)ptc_lab_process_request(&first, &item);
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/results/wrong.json", text, sizeof(text)) &&
        strstr(text, "\"status\":\"error\"") != NULL, "out-of-order phase is rejected explicitly");

    item = request(PTC_REQUEST_LAB_PHASE_START, "phase0", phases[0], NULL);
    (void)ptc_lab_process_request(&first, &item);
    time.snapshot.unix_seconds += 74;
    check(ptc_lab_next_wait_ms(&first, 5000) == 1000, "scheduler wake is clamped to the persisted phase deadline");
    check(ptc_lab_scheduler_tick(&first) == 0, "75-second phase does not finish early");
    init_lab(&restarted, &storage, &pctl, &time);
    time.snapshot.unix_seconds += 1;
    check(ptc_lab_scheduler_tick(&restarted) == 1, "phase resumes and finishes after sysmodule restart");

    for (phase = 1; phase < 5; ++phase) {
        char id[24];
        snprintf(id, sizeof(id), "phase%d", phase);
        item = request(PTC_REQUEST_LAB_PHASE_START, id, phases[phase], NULL);
        (void)ptc_lab_process_request(&restarted, &item);
        time.snapshot.unix_seconds += 75;
        check(ptc_lab_scheduler_tick(&restarted) == 1, "ordered lifecycle phase completes");
    }
    item = request(PTC_REQUEST_LAB_PHASE_START, "restriction", phases[5], NULL);
    (void)ptc_lab_process_request(&restarted, &item);
    time.snapshot.unix_seconds += 14;
    check(ptc_lab_scheduler_tick(&restarted) == 0, "restriction is not restored before 15 seconds");
    pctl.restore_error = PTC_ERR_PCTL_RESTORE_FAILED;
    time.snapshot.unix_seconds += 1;
    check(ptc_lab_scheduler_tick(&restarted) == 1, "restriction deadline triggers independent recovery");
    check(storage.storage.vtable->exists(&storage.storage, "/lab/flags/disable.flag"),
        "unproved restoration disables all later writes");
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/lab/session.json", text, sizeof(text)) &&
        strstr(text, "restore_required") != NULL, "restore-required state survives restart");
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/lab/report-2000000000-test-boot.draft.json", text, sizeof(text)) &&
        strstr(text, "\"1952\"") != NULL, "private 1952 evidence appears only in Lab report data");
}

static void test_atmosphere_version(void)
{
    PtcAtmosphereVersion version;
    uint64_t raw = (1ULL << 56) | (11ULL << 48) | (2ULL << 40) | 0x16050000ULL;
    check(ptc_atmosphere_version_decode(raw, &version) && version.major == 1U &&
            version.minor == 11U && version.micro == 2U,
        "Exosphere ApiInfo exposes Atmosphere 1.11.2");
    check(!ptc_atmosphere_version_decode(0, &version),
        "zero Exosphere evidence is not accepted as Atmosphere");
    check(!ptc_atmosphere_version_decode(raw, NULL),
        "Atmosphere version decoder rejects a missing output");
}

static void test_complete_report_requires_observation_and_latches_event(void)
{
    PtcMemStorage storage;
    PtcPctlStub pctl;
    PtcFakeTime time;
    PtcSysmodule sysmodule;
    PtcRequest item;
    char text[20000];
    int phase;
    static const char *const phases[] = {
        "home_stopped", "home_started", "game_foreground", "game_suspended", "sleep_wake", "restriction_effect"
    };
    ptc_mem_storage_init(&storage);
    ptc_pctl_stub_init(&pctl);
    /* day_index 2999 maps to weekday 6, whose final day slot ends exactly at
       the 0x44 settings boundary. */
    ptc_fake_time_init(&time, 2100000000LL, 2999, 600);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 60;
    pctl.status.play_timer_enabled = false;
    pctl.model_elapsed_time = true;
    pctl.configured_minutes = 60;
    pctl.public_parity_override_enabled = true;
    pctl.public_parity_override.current_settings_equal = true;
    pctl.public_parity_override.raw_suspend_event_valid = true;
    pctl.public_parity_override.libnx_suspend_event_valid = true;
    pctl.public_parity_override.libnx_alarm_disabled_result = 19289U;
    init_lab(&sysmodule, &storage, &pctl, &time);
    check(storage.storage.vtable->write_text_atomic(&storage.storage, "/lab/environment.json",
            "{\"version\":1,\"read_ok\":true,\"hos\":\"22.5.0\",\"model\":\"mariko-oled\"}"),
        "Lab runtime environment fixture is stored");
    check(storage.storage.vtable->write_text_atomic(&storage.storage, "/lab/build.json",
            "{\"profile\":\"device-lab\",\"release_id\":\"test-lab\"}"),
        "Lab build identity fixture is stored");

    item = request(PTC_REQUEST_LAB_SESSION_START, "complete-start", NULL, NULL);
    (void)ptc_lab_process_request(&sysmodule, &item);
    for (phase = 0; phase < 5; ++phase) {
        char id[24];
        if (phase == 1) pctl.status.remaining_minutes = 0;
        snprintf(id, sizeof(id), "complete-phase-%d", phase);
        item = request(PTC_REQUEST_LAB_PHASE_START, id, phases[phase], NULL);
        (void)ptc_lab_process_request(&sysmodule, &item);
        if (phase == 1) pctl.forensic_spent_ns = 75000000000LL;
        time.snapshot.unix_seconds += 75;
        check(ptc_lab_scheduler_tick(&sysmodule) == 1, "successful Lab phase completes");
    }
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/lab/phase-1.json", text, sizeof(text)) &&
            strstr(text, "\"deltas\":{\"remaining_ns\":0,\"spent_ns\":75000000000}") != NULL &&
            strstr(text, "\"product_semantics\":\"home_usage_counted\"") != NULL,
        "HOME spent-time growth is recorded as factual console-use evidence");

    item = request(PTC_REQUEST_LAB_PHASE_START, "complete-restriction", phases[5], NULL);
    (void)ptc_lab_process_request(&sysmodule, &item);
    check(ptc_lab_next_wait_ms(&sysmodule, 5000) == 100,
        "restriction phase polls the suspension event every 100 ms");
    pctl.forensic_monotonic_ns = 123456789ULL;
    pctl.suspend_event_signaled = true;
    time.snapshot.unix_seconds += 1;
    check(ptc_lab_scheduler_tick(&sysmodule) == 0, "restriction event can be sampled before the deadline");
    pctl.suspend_event_signaled = false;
    time.snapshot.unix_seconds += 14;
    check(ptc_lab_scheduler_tick(&sysmodule) == 1, "restriction phase completes at its deadline");
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/lab/phase-5.json", text, sizeof(text)) &&
            strstr(text, "\"check_count\":2") != NULL &&
            strstr(text, "\"signaled\":true") != NULL &&
            strstr(text, "\"first_signaled_monotonic_ns\":123456789") != NULL,
        "a transient suspension event remains latched in the final phase evidence");
    check(strstr(text, "\"settings_hex\":") != NULL &&
            strstr(text, "\"target_weekday\":6") != NULL &&
            strstr(text, "\"expected_byte_start\":62") != NULL &&
            strstr(text, "\"expected_byte_end_exclusive\":68") != NULL &&
            strstr(text, "\"changed_offsets\":[") != NULL &&
            strstr(text, "\"outside_today_changed_offsets\":[") != NULL &&
            strstr(text, "\"expected_header_changed_offsets\":[") != NULL &&
            strstr(text, "\"expected_today_changed_offsets\":[") != NULL &&
            strstr(text, "\"unexpected_changed_offsets\":[") != NULL &&
            strstr(text, "\"unrelated_bytes_unchanged\":false") != NULL,
        "restriction evidence includes raw settings and bounded byte-offset differences");

    check(storage.storage.vtable->read_text(&storage.storage,
            "/lab/lab/report-2100000000-test-boot.draft.json", text, sizeof(text)) &&
            strstr(text, "\"report_status\":\"draft\"") != NULL &&
            strstr(text, "\"automated_phases_completed\":6") != NULL &&
            strstr(text, "\"required_automated_phases\":6") != NULL &&
            strstr(text, "\"manual_observation_recorded\":false") != NULL &&
            strstr(text, "\"complete\":false") != NULL &&
            strstr(text, "\"runtime\":{\"version\":1") != NULL &&
            strstr(text, "\"build\":{\"profile\":\"device-lab\"") != NULL,
        "six automatic phases remain incomplete until the observation is recorded and carry environment identity");
    check(strstr(text, "\"1458\":{\"raw_result\":0,\"libnx_result\":19289,\"comparable\":false,\"value_equal\":false}") != NULL,
        "failed libnx parity is explicitly non-comparable instead of accidentally equal");

    item = request(PTC_REQUEST_LAB_SESSION_RESTORE, "redundant-restore", NULL, NULL);
    (void)ptc_lab_process_request(&sysmodule, &item);
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/lab/session.json", text, sizeof(text)) &&
            strstr(text, "\"state\":\"awaiting_observation\"") != NULL,
        "redundant restore preserves the pending manual observation");

    item = request(PTC_REQUEST_LAB_OBSERVATION, "visible", NULL, "restriction_visible");
    (void)ptc_lab_process_request(&sysmodule, &item);
    check(storage.storage.vtable->read_text(&storage.storage,
            "/lab/reports/2100000000-test-boot.json", text, sizeof(text)) &&
            strstr(text, "\"manual_observation\":\"restriction_visible\"") != NULL &&
            strstr(text, "\"manual_runtime_effect\":\"unsure\"") != NULL &&
            strstr(text, "\"manual_observation_recorded\":true") != NULL &&
            strstr(text, "\"manual_runtime_effect_recorded\":true") != NULL &&
            strstr(text, "\"report_status\":\"final\"") != NULL &&
            strstr(text, "\"complete\":true") != NULL,
        "confirmed visible restriction finalizes the report");
    check(!storage.storage.vtable->exists(&storage.storage,
            "/lab/lab/report-2100000000-test-boot.draft.json"),
        "publishing the final report removes its draft");
}

static void test_restriction_quick_mode_and_zero_baseline_classification(void)
{
    PtcMemStorage storage;
    PtcPctlStub pctl;
    PtcFakeTime time;
    PtcSysmodule sysmodule;
    PtcRequest item;
    char text[20000];
    ptc_mem_storage_init(&storage);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&time, 2200000000LL, 3003, 600);
    pctl.raw_settings_override_enabled = true;
    pctl.model_elapsed_time = true;
    memset(pctl.raw_settings, 0, sizeof(pctl.raw_settings));
    pctl.settings_header_initialized = false;
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = false;
    pctl.status.blocked_today = false;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 0;
    init_lab(&sysmodule, &storage, &pctl, &time);

    item = request(PTC_REQUEST_LAB_SESSION_START, "quick-start", NULL, NULL);
    snprintf(item.lab_mode, sizeof(item.lab_mode), "restriction_quick");
    (void)ptc_lab_process_request(&sysmodule, &item);
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/lab/session.json", text, sizeof(text)) &&
            strstr(text, "\"mode\":\"restriction_quick\"") != NULL &&
            strstr(text, "\"baseline_all_zero\":true") != NULL,
        "quick session persists its mode and all-zero baseline warning");

    item = request(PTC_REQUEST_LAB_PHASE_START, "quick-wrong", "home_stopped", NULL);
    (void)ptc_lab_process_request(&sysmodule, &item);
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/results/quick-wrong.json", text, sizeof(text)) &&
            strstr(text, "\"status\":\"error\"") != NULL,
        "quick mode rejects every phase except restriction_effect");

    item = request(PTC_REQUEST_LAB_PHASE_START, "quick-restriction", "restriction_effect", NULL);
    (void)ptc_lab_process_request(&sysmodule, &item);
    time.snapshot.unix_seconds += 15;
    check(ptc_lab_scheduler_tick(&sysmodule) == 1, "quick restriction phase completes and restores independently");
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/lab/phase-5.json", text, sizeof(text)) &&
            strstr(text, "\"expected_header_changed_offsets\":[0,1,2]") != NULL &&
            strstr(text, "\"expected_today_changed_offsets\":[39,41]") != NULL &&
            strstr(text, "\"unexpected_changed_offsets\":[]") != NULL &&
            strstr(text, "\"unexpected_bytes_unchanged\":true") != NULL,
        "all-zero baseline changes classify header, current day, and unexpected offsets separately");
    check(storage.storage.vtable->read_text(&storage.storage,
            "/lab/lab/report-2200000000-test-boot.draft.json", text, sizeof(text)) &&
            strstr(text, "\"automated_phases_completed\":1") != NULL &&
            strstr(text, "\"required_automated_phases\":1") != NULL &&
            strstr(text, "\"complete\":false") != NULL,
        "quick mode remains a draft after its single automated phase until both observations arrive");

    item = request(PTC_REQUEST_LAB_OBSERVATION, "quick-visible", NULL, "restriction_visible");
    snprintf(item.runtime_effect, sizeof(item.runtime_effect), "paused_or_suspended");
    (void)ptc_lab_process_request(&sysmodule, &item);
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/results/quick-visible.json", text, sizeof(text)) &&
            strstr(text, "\"status\":\"ok\"") != NULL,
        "quick two-layer observation request succeeds");
    check(storage.storage.vtable->read_text(&storage.storage,
            "/lab/reports/2200000000-test-boot.json", text, sizeof(text)),
        "two-layer quick observation publishes a final report path");
    check(strstr(text, "\"manual_runtime_effect\":\"paused_or_suspended\"") != NULL,
        "quick final report retains the actual runtime effect");
    check(strstr(text, "\"report_status\":\"final\"") != NULL &&
            strstr(text, "\"complete\":true") != NULL,
        "two-layer quick observation promotes the draft to final status");
}

static void test_timer_activation_ab_report(void)
{
    PtcMemStorage storage;
    PtcPctlStub pctl;
    PtcFakeTime time;
    PtcSysmodule sysmodule;
    PtcRequest item;
    char text[24000];
    uint16_t initial_words[PTC_PLAY_TIMER_SETTINGS_WORDS] = {0};
    int phase;
    unsigned int weekday;
    size_t fallback_start_calls = 0U;
    static const char *const phases[] = {
        "ab_home_awake", "ab_sleep_wake", "ab_limited_settings_only",
        "ab_restriction_settings_only", "ab_grant_settings_only",
        "ab_restriction_before_unlimited", "ab_unlimited_settings_only"
    };
    ptc_mem_storage_init(&storage);
    ptc_pctl_stub_init(&pctl);
    ptc_fake_time_init(&time, 2250000000LL, 3004, 600);
    pctl.status.unrestricted_today = false;
    pctl.status.limited_today = true;
    pctl.status.remaining_available = true;
    pctl.status.remaining_minutes = 60;
    pctl.status.play_timer_enabled = true;
    pctl.model_elapsed_time = true;
    pctl.configured_minutes = 60;
    pctl.raw_settings_override_enabled = true;
    for (weekday = 0; weekday < PTC_PLAY_TIMER_DAY_COUNT; ++weekday) {
        check(ptc_play_timer_settings_set_day(initial_words, PTC_PLAY_TIMER_SETTINGS_WORDS,
                (uint8_t)weekday, true, 60U),
            "A/B fixture builds a non-empty official weekly schedule");
    }
    memcpy(pctl.raw_settings, initial_words, sizeof(initial_words));
    init_lab(&sysmodule, &storage, &pctl, &time);

    item = request(PTC_REQUEST_LAB_SESSION_START, "ab-start", NULL, NULL);
    snprintf(item.lab_mode, sizeof(item.lab_mode), "timer_activation_ab");
    (void)ptc_lab_process_request(&sysmodule, &item);
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/lab/session.json", text, sizeof(text)) &&
            strstr(text, "\"mode\":\"timer_activation_ab\"") != NULL,
        "timer activation A/B session starts from a non-empty Nintendo schedule");

    for (phase = 0; phase < 7; ++phase) {
        char id[32];
        snprintf(id, sizeof(id), "ab-phase-%d", phase);
        if (phase == 2) {
            pctl.runtime_effect_succeeds = false;
            pctl.status.play_timer_enabled = false;
        }
        item = request(PTC_REQUEST_LAB_PHASE_START, id, phases[phase], NULL);
        (void)ptc_lab_process_request(&sysmodule, &item);
        if (phase == 0) pctl.forensic_spent_ns = 90000000000LL;
        time.snapshot.unix_seconds += phase < 2 ? 90 : 15;
        check(ptc_lab_scheduler_tick(&sysmodule) == 1,
            "each timer activation A/B phase completes in order");
        if (phase == 2) {
            fallback_start_calls = pctl.start_timer_calls;
            pctl.runtime_effect_succeeds = true;
        }
    }
    check(storage.storage.vtable->read_text(&storage.storage,
            "/lab/reports/2250000000-test-boot.json", text, sizeof(text)) &&
            strstr(text, "\"report_status\":\"final\"") != NULL &&
            strstr(text, "\"schema_version\":2") != NULL &&
            strstr(text, "\"required_automated_phases\":7") != NULL &&
            strstr(text, "\"manual_observation_recorded\":false") != NULL &&
            strstr(text, "\"complete\":true") != NULL,
        "A/B report finalizes from seven automated phases without inventing a manual observation");
    check(strstr(text, "\"timer_activation_ab\":{\"home_awake_counted\":true,") != NULL &&
            strstr(text, "\"sleep_excluded\":true") != NULL &&
            strstr(text, "\"target\":\"limited\",\"settings_only_runtime_ready\":false,\"fallback_called\":true,\"fallback_succeeded\":true") != NULL &&
            strstr(text, "\"target\":\"grant\",\"settings_only_runtime_ready\":true,\"fallback_called\":false") != NULL &&
            strstr(text, "\"target\":\"unlimited\",\"settings_only_runtime_ready\":true,\"fallback_called\":false") != NULL,
        "A/B summary records factual target-bound fallback decisions");
    check(fallback_start_calls == 1U,
        "A/B invokes 1451 once only for the target that failed its settings-only runtime condition");
    check(strstr(text, "\"manual_observation\":null") != NULL &&
            strstr(text, "\"manual_runtime_effect\":null") != NULL,
        "A/B report keeps unrelated manual observation fields explicitly null");
    check(strstr(text, "\"comparison\":\"phase_prewrite_to_after\"") != NULL &&
            strstr(text, "\"prewrite_settings_hex\":") != NULL &&
            strstr(text, "\"unexpected_bytes_unchanged\":true") != NULL,
        "A/B settings experiments retain their full pre-write image and unexpected-offset verdict");
}

static bool touch_empty(const char *path)
{
    FILE *file = fopen(path, "wb");
    return file && fclose(file) == 0;
}

static bool write_journal_fixture(const char *path, const char *phase, bool standard_was_enabled)
{
    FILE *file = fopen(path, "wb");
    bool ok;
    if (!file) return false;
    ok = fprintf(file, "{\"version\":1,\"phase\":\"%s\",\"standard_was_enabled\":%s}\n",
        phase, standard_was_enabled ? "true" : "false") > 0;
    return fclose(file) == 0 && ok;
}

static void test_boot_flags(void)
{
    char root[] = "/tmp/playwise-lab-flags-XXXXXX";
    char standard[512], backup[512], lab[512], journal[512], journal_tmp[520], message[256];
    PtcLabBootFlagPaths paths;
    PtcLabBootStatus status;
    check(mkdtemp(root) != NULL, "temporary boot flag root created");
    snprintf(standard, sizeof(standard), "%s/standard.flag", root);
    snprintf(backup, sizeof(backup), "%s/standard.backup", root);
    snprintf(lab, sizeof(lab), "%s/lab.flag", root);
    snprintf(journal, sizeof(journal), "%s/journal.json", root);
    snprintf(journal_tmp, sizeof(journal_tmp), "%s.tmp", journal);
    paths.standard_flag = standard; paths.standard_backup = backup; paths.lab_flag = lab; paths.journal = journal;
    check(touch_empty(standard), "standard flag fixture created");
    check(ptc_lab_boot_flags_inspect(&paths, &status) && status.state == PTC_LAB_BOOT_NORMAL,
        "untouched flags are presented as ready for Lab preparation");
    check(ptc_lab_boot_flags_enable(&paths, message, sizeof(message)) == PTC_LAB_FLAG_OK,
        "standard-enabled state switches transactionally");
    check(strstr(message, "实验后台") != NULL, "boot transaction success message is Chinese");
    check(ptc_lab_boot_flags_inspect(&paths, &status) && status.state == PTC_LAB_BOOT_ENABLED &&
            status.standard_was_enabled,
        "enabled transaction has a read-only UI status");
    check(access(standard, F_OK) != 0 && access(backup, F_OK) == 0 && access(lab, F_OK) == 0,
        "enable preserves standard flag in unique backup");
    check(ptc_lab_boot_flags_enable(&paths, message, sizeof(message)) == PTC_LAB_FLAG_ALREADY_DONE,
        "repeated enable is idempotent");
    check(ptc_lab_boot_flags_restore(&paths, message, sizeof(message)) == PTC_LAB_FLAG_OK,
        "restore returns exact original flag state");
    check(ptc_lab_boot_flags_inspect(&paths, &status) && status.state == PTC_LAB_BOOT_RESTORED,
        "completed restore has a read-only UI status");
    check(access(standard, F_OK) == 0 && access(backup, F_OK) != 0 && access(lab, F_OK) != 0,
        "normal package flag is restored exactly");
    check(ptc_lab_boot_flags_restore(&paths, message, sizeof(message)) == PTC_LAB_FLAG_ALREADY_DONE,
        "repeated restore is idempotent");
    check(ptc_lab_boot_flags_enable(&paths, message, sizeof(message)) == PTC_LAB_FLAG_OK,
        "a completed transaction can begin another exact Lab cycle");
    check(ptc_lab_boot_flags_restore(&paths, message, sizeof(message)) == PTC_LAB_FLAG_OK,
        "second Lab cycle restores normally");
    (void)remove(standard); (void)remove(journal);

    check(ptc_lab_boot_flags_enable(&paths, message, sizeof(message)) == PTC_LAB_FLAG_OK,
        "standard-not-installed state can enable Lab");
    check(ptc_lab_boot_flags_restore(&paths, message, sizeof(message)) == PTC_LAB_FLAG_OK && access(standard, F_OK) != 0,
        "restore preserves originally absent standard flag");
    (void)remove(journal);
    check(touch_empty(backup), "conflict backup fixture created");
    check(ptc_lab_boot_flags_enable(&paths, message, sizeof(message)) == PTC_LAB_FLAG_CONFLICT,
        "unknown backup conflict is never overwritten");
    check(ptc_lab_boot_flags_inspect(&paths, &status) && status.state == PTC_LAB_BOOT_CONFLICT,
        "unknown backup is presented as a blocking conflict");
    (void)remove(backup);

    check(touch_empty(backup), "interrupted transaction backup fixture created");
    {
        FILE *file = fopen(journal, "wb");
        check(file != NULL && fprintf(file,
            "{\"version\":1,\"phase\":\"standard_disabled\",\"standard_was_enabled\":true}\n") > 0 &&
            fclose(file) == 0, "interrupted transaction journal created");
    }
    check(ptc_lab_boot_flags_enable(&paths, message, sizeof(message)) == PTC_LAB_FLAG_RECOVERY_REQUIRED,
        "interrupted enable routes to recovery without overwriting flags");
    check(ptc_lab_boot_flags_restore(&paths, message, sizeof(message)) == PTC_LAB_FLAG_OK && access(standard, F_OK) == 0,
        "interrupted transaction restores the preserved standard flag");
    (void)remove(standard); (void)remove(journal);

    check(write_journal_fixture(journal, "prepared", false),
        "field-reported prepared journal without a standard flag is reproducible");
    check(ptc_lab_boot_flags_restore(&paths, message, sizeof(message)) == PTC_LAB_FLAG_OK &&
            access(standard, F_OK) != 0 && access(lab, F_OK) != 0,
        "field-reported prepared journal restores without changing boot flags");
    (void)remove(journal);

    check(write_journal_fixture(journal, "prepared", false) &&
            write_journal_fixture(journal_tmp, "standard_disabled", false),
        "Switch no-overwrite interruption keeps old and complete pending journals");
    check(ptc_lab_boot_flags_restore(&paths, message, sizeof(message)) == PTC_LAB_FLAG_OK &&
            access(journal_tmp, F_OK) != 0,
        "restore promotes a complete pending journal before continuing");
    (void)remove(journal);

    check(touch_empty(backup) && write_journal_fixture(journal_tmp, "standard_disabled", true),
        "power loss after old journal removal leaves a recoverable pending generation");
    check(ptc_lab_boot_flags_restore(&paths, message, sizeof(message)) == PTC_LAB_FLAG_OK &&
            access(standard, F_OK) == 0 && access(backup, F_OK) != 0 && access(journal_tmp, F_OK) != 0,
        "pending-only journal restores the preserved standard flag exactly");
    (void)remove(standard); (void)remove(journal);

    check(touch_empty(backup) && write_journal_fixture(journal, "prepared", true),
        "prior journal remains available beside a torn pending generation");
    {
        FILE *file = fopen(journal_tmp, "wb");
        check(file != NULL && fprintf(file, "{\"version\":1") > 0 && fclose(file) == 0,
            "torn pending journal fixture created");
    }
    check(ptc_lab_boot_flags_restore(&paths, message, sizeof(message)) == PTC_LAB_FLAG_OK &&
            access(standard, F_OK) == 0 && access(journal_tmp, F_OK) != 0,
        "torn pending journal falls back to the prior trustworthy generation");
    (void)remove(standard); (void)remove(journal);
    (void)rmdir(root);
}

static void test_ui_model(void)
{
    static const char *const valid =
        "{\"version\":1,\"run_id\":\"run-1\",\"state\":\"ready\",\"next_phase\":3,"
        "\"active_phase\":\"\",\"deadline\":0,\"restored\":false,"
        "\"restore_verdict\":\"pending\"}";
    static const char *const error =
        "{\"version\":1,\"status\":\"error\",\"error\":{\"code\":307,"
        "\"reason\":\"pctl_restore_failed\"}}";
    static const char *const quick =
        "{\"version\":1,\"run_id\":\"quick-1\",\"mode\":\"restriction_quick\","
        "\"state\":\"ready\",\"next_phase\":0,\"active_phase\":\"\",\"deadline\":0,"
        "\"restored\":false,\"baseline_all_zero\":true,\"restore_verdict\":\"pending\"}";
    static const char *const activation_ab =
        "{\"version\":2,\"run_id\":\"ab-1\",\"mode\":\"timer_activation_ab\","
        "\"state\":\"ready\",\"next_phase\":6,\"active_phase\":\"\",\"deadline\":0,"
        "\"restored\":false,\"baseline_all_zero\":false,\"restore_verdict\":\"pending\"}";
    PtcLabSessionView session;
    PtcLabBootStatus boot;
    int code = 0;
    char reason[64];
    memset(&boot, 0, sizeof(boot));
    check(ptc_lab_session_parse(valid, &session) && session.next_phase == 3 &&
            strcmp(session.state, "ready") == 0,
        "UI parser accepts the persisted Lab session schema");
    check(!ptc_lab_session_parse("{\"state\":\"ready\"}", &session),
        "UI parser rejects a damaged session instead of treating it as new");
    check(ptc_lab_session_parse(quick, &session) && session.required_phases == 1 &&
            session.baseline_all_zero && strcmp(session.mode, "restriction_quick") == 0,
        "UI parser exposes focused mode progress and its all-zero baseline warning");
    check(ptc_lab_session_parse(activation_ab, &session) && session.required_phases == 7 &&
            session.next_phase == 6 && strcmp(session.mode, "timer_activation_ab") == 0,
        "UI parser exposes seven-phase timer activation progress");
    check(ptc_lab_result_error(error, &code, reason, sizeof(reason)) && code == 307 &&
            strcmp(reason, "pctl_restore_failed") == 0,
        "UI extracts a durable backend error code and reason");
    boot.state = PTC_LAB_BOOT_ENABLED;
    check(ptc_lab_nro_stage(&boot, false, PTC_LAB_SESSION_MISSING, NULL) == PTC_LAB_NRO_REBOOT_TO_LAB,
        "enabled Lab without service recommends reboot");
    check(ptc_lab_nro_stage(&boot, true, PTC_LAB_SESSION_MISSING, NULL) == PTC_LAB_NRO_START_OVERLAY,
        "running Lab without session recommends opening the Overlay");
    check(ptc_lab_nro_stage(&boot, true, PTC_LAB_SESSION_INVALID, NULL) == PTC_LAB_NRO_SESSION_INVALID,
        "damaged session blocks all new evidence actions");
    memset(&session, 0, sizeof(session));
    snprintf(session.state, sizeof(session.state), "awaiting_observation");
    snprintf(session.restore_verdict, sizeof(session.restore_verdict), "exact_restore_proved");
    session.next_phase = 6;
    session.restored = true;
    check(ptc_lab_nro_stage(&boot, true, PTC_LAB_SESSION_VALID, &session) == PTC_LAB_NRO_RESUME_OVERLAY,
        "restored six-phase evidence still returns to the Overlay until observation is submitted");
    snprintf(session.state, sizeof(session.state), "complete");
    check(ptc_lab_nro_stage(&boot, true, PTC_LAB_SESSION_VALID, &session) == PTC_LAB_NRO_RESTORE_NORMAL,
        "exact restore proof recommends switching back to the normal package");
    boot.state = PTC_LAB_BOOT_RESTORED;
    check(ptc_lab_nro_stage(&boot, true, PTC_LAB_SESSION_VALID, &session) == PTC_LAB_NRO_REBOOT_TO_NORMAL &&
            ptc_lab_nro_stage(&boot, false, PTC_LAB_SESSION_VALID, &session) == PTC_LAB_NRO_PREPARE,
        "restored flags require one reboot before another Lab cycle is offered");
    check(strstr(ptc_lab_phase_title_zh(4), "待机") != NULL &&
            strstr(ptc_lab_phase_title_for_mode_zh("timer_activation_ab", 0), "HOME") != NULL &&
            strstr(ptc_lab_phase_instruction_for_mode_zh("timer_activation_ab", 6), "1451") != NULL &&
            strstr(ptc_lab_transport_error_zh(2), "超时") != NULL,
        "phase and transport guidance are localized in Chinese");
    check(ptc_lab_nro_hit_test(72, 402) == 0 && ptc_lab_nro_hit_test(831, 473) == 0 &&
            ptc_lab_nro_hit_test(832, 473) == -1,
        "NRO touch targets match their visible action cards");
}

int main(void)
{
    test_atmosphere_version();
    test_protocol();
    test_session_timing_order_restart_and_restore_failure();
    test_complete_report_requires_observation_and_latches_event();
    test_restriction_quick_mode_and_zero_baseline_classification();
    test_timer_activation_ab_report();
    test_boot_flags();
    test_ui_model();
    if (failures) { fprintf(stderr, "%d Device Lab test(s) failed\n", failures); return 1; }
    printf("PASS: Device Lab protocol, state machine, recovery, and boot flags\n");
    return 0;
}
