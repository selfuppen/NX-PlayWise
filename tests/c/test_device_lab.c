#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../common/protocol/request_schema.h"
#include "../../device_lab/boot_flags.h"
#include "../../device_lab/ui_model.h"
#include "../../platform/host/fake_time.h"
#include "../../platform/host/mem_storage.h"
#include "../../platform/host/pctl_stub.h"
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
    if (observation) snprintf(value.observation, sizeof(value.observation), "%s", observation);
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
    check(ptc_request_parse("{\"version\":1,\"request_id\":\"p1\",\"type\":\"lab_phase_start\",\"created_at\":1,\"payload\":{\"phase\":\"sleep_wake\"}}", &parsed) == PTC_ERR_OK,
        "valid Lab phase parses");
    check(strcmp(parsed.phase, "sleep_wake") == 0, "Lab phase is retained");
    check(ptc_request_parse("{\"version\":1,\"request_id\":\"p2\",\"type\":\"lab_phase_start\",\"created_at\":1,\"payload\":{\"phase\":\"skip\"}}", &parsed) == PTC_ERR_BAD_REQUEST,
        "unknown Lab phase is rejected");
    for (i = 0; i < sizeof(observations) / sizeof(observations[0]); ++i) {
        char json[320];
        snprintf(json, sizeof(json),
            "{\"version\":1,\"request_id\":\"o%u\",\"type\":\"lab_observation\",\"created_at\":1,"
            "\"payload\":{\"observation\":\"%s\"}}", (unsigned int)i, observations[i]);
        check(ptc_request_parse(json, &parsed) == PTC_ERR_OK, "each tri-state observation parses");
    }
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
    check(storage.storage.vtable->exists(&storage.storage, "/lab/reports/2000000000-test-boot.json"),
        "single report is created at start");

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
    check(storage.storage.vtable->read_text(&storage.storage, "/lab/reports/2000000000-test-boot.json", text, sizeof(text)) &&
        strstr(text, "\"1952\"") != NULL, "private 1952 evidence appears only in Lab report data");
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
            strstr(text, "\"product_semantics\":\"unsafe_for_home_start\"") != NULL,
        "HOME spent-time growth marks active timer start unsafe even without a remaining-time delta");

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
            strstr(text, "\"unrelated_bytes_unchanged\":false") != NULL,
        "restriction evidence includes raw settings and bounded byte-offset differences");

    check(storage.storage.vtable->read_text(&storage.storage,
            "/lab/reports/2100000000-test-boot.json", text, sizeof(text)) &&
            strstr(text, "\"automated_phases_completed\":6") != NULL &&
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
            strstr(text, "\"manual_observation_recorded\":true") != NULL &&
            strstr(text, "\"complete\":true") != NULL,
        "confirmed visible restriction finalizes the report");
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
            strstr(ptc_lab_transport_error_zh(2), "超时") != NULL,
        "phase and transport guidance are localized in Chinese");
    check(ptc_lab_nro_hit_test(72, 402) == 0 && ptc_lab_nro_hit_test(831, 473) == 0 &&
            ptc_lab_nro_hit_test(832, 473) == -1,
        "NRO touch targets match their visible action cards");
}

int main(void)
{
    test_protocol();
    test_session_timing_order_restart_and_restore_failure();
    test_complete_report_requires_observation_and_latches_event();
    test_boot_flags();
    test_ui_model();
    if (failures) { fprintf(stderr, "%d Device Lab test(s) failed\n", failures); return 1; }
    printf("PASS: Device Lab protocol, state machine, recovery, and boot flags\n");
    return 0;
}
