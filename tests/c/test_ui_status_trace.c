#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../companion/ui_status_trace.h"
#include "../../platform/host/mem_storage.h"
#include "../../third_party/cjson/cJSON.h"

static PtcMemStorage mem;
static PtcUiStatusTrace trace;
static int failures;
static const char *COMMIT = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char *RELEASE = "playwise-test+aaaaaaaaaaaa.1234567890abcdef";
static const char *CONFIG = "app/support/ui-status-trace.json";

static void check(bool ok, const char *label)
{
    if (!ok) { fprintf(stderr, "FAIL status trace: %s\n", label); ++failures; }
}

static void put(const char *path, const char *text)
{
    check(mem.storage.vtable->write_text_atomic(&mem.storage, path, text), "fixture write");
}

static void fixture(unsigned max)
{
    char text[768];
    ptc_mem_storage_init(&mem);
    snprintf(text, sizeof(text), "{\"schema_version\":1,\"run_id\":\"0123456789abcdef0123456789abcdef\","
        "\"commit\":\"%s\",\"release_id\":\"%s\",\"backend_boot_id\":\"0123456789abcdef\","
        "\"expires_mono_ns\":\"120000000000\",\"max_records\":%u}", COMMIT, RELEASE, max);
    put(CONFIG, text);
    snprintf(text, sizeof(text), "{\"profile\":\"release\",\"release_id\":\"%s\",\"boot_id\":\"0123456789abcdef\"}", RELEASE);
    put("app/handover/runtime-ready.json", text);
    put("app/state.json", "business-state-must-not-change");
}

static void init(uint64_t now)
{
    ptc_ui_status_trace_init(&trace, &mem.storage, "app", COMMIT, RELEASE, "nro", now);
}

static void change(const char *key, cJSON *value)
{
    char text[768];
    cJSON *root;
    char *updated;
    check(mem.storage.vtable->read_text(&mem.storage, CONFIG, text, sizeof(text)), "read fixture");
    root = cJSON_Parse(text);
    cJSON_ReplaceItemInObjectCaseSensitive(root, key, value);
    updated = cJSON_PrintUnformatted(root);
    put(CONFIG, updated);
    free(updated);
    cJSON_Delete(root);
}

static bool contains(const char *needle)
{
    char text[8192];
    return mem.storage.vtable->read_text(&mem.storage, trace.output_path, text, sizeof(text)) && strstr(text, needle);
}

int main(void)
{
    char text[8192], output[320];
    fixture(3);
    mem.storage.vtable->remove_path(&mem.storage, CONFIG);
    init(1000000000);
    check(!trace.active && !trace.output_path[0], "default off creates no output");
    ptc_ui_status_trace_record(&trace, "ignored", PTC_UI_STATUS_BUTTON_Y, 2000000000, 1, 0);
    fixture(3); init(1000000000);
    check(trace.active, "valid bound opt-in starts observation");
    ptc_ui_status_trace_record(&trace, "1234001-abcd", PTC_UI_STATUS_BUTTON_Y, 2000000000, 1, 0);
    ptc_ui_status_trace_record(&trace, "1234002-abcd", PTC_UI_STATUS_RESUME_AND_FOCUS, 3000000000, 1, 4);
    check(contains("\"origin\":\"button_y\"") && contains("\"origin\":\"resume_and_focus\"") &&
        contains("\"transport_status\":4"), "original IDs, trigger and failed transport remain distinct");
    ptc_ui_status_trace_close(&trace, 4000000000);
    check(!trace.active && contains("client_exit"), "normal close has terminal evidence");
    check(mem.storage.vtable->read_text(&mem.storage, "app/state.json", text, sizeof(text)) &&
        !strcmp(text, "business-state-must-not-change"), "observation never alters business state");
    fixture(1); init(1000000000);
    ptc_ui_status_trace_record(&trace, "one", PTC_UI_STATUS_FOCUS, 2000000000, 1, 0);
    check(!trace.active && trace.count == 1 && contains("record_limit"), "record cap terminates");
    mem.storage.vtable->read_text(&mem.storage, trace.output_path, text, sizeof(text));
    ptc_ui_status_trace_record(&trace, "two", PTC_UI_STATUS_OTHER, 3000000000, 1, 0);
    { char after[8192]; mem.storage.vtable->read_text(&mem.storage, trace.output_path, after, sizeof(after));
      check(!strcmp(text, after), "terminated trace never appends again"); }
    fixture(3); init(1000000000);
    ptc_ui_status_trace_record(&trace, "late", PTC_UI_STATUS_OTHER, 120000000000, 1, 0);
    check(!trace.active && contains("lease_expired") && trace.count == 0, "absolute monotonic deadline");
    fixture(3); init(1000000000);
    ptc_ui_status_trace_record(&trace, "back", PTC_UI_STATUS_OTHER, 999999999, 1, 0);
    check(!trace.active && contains("clock_invalid"), "clock rollback stops");
    fixture(3); init(1000000000); mem.storage.vtable->remove_path(&mem.storage, CONFIG);
    ptc_ui_status_trace_record(&trace, "removed", PTC_UI_STATUS_OTHER, 2000000000, 1, 0);
    check(!trace.active && contains("config_removed_or_changed"), "removing owned config stops");
    fixture(3); init(1000000000); change("max_records", cJSON_CreateNumber(4));
    ptc_ui_status_trace_record(&trace, "changed", PTC_UI_STATUS_OTHER, 2000000000, 1, 0);
    check(!trace.active && contains("config_or_runtime_changed"), "configuration cannot extend live lease");
    fixture(3); init(1000000000); put("app/handover/runtime-ready.json", "{}");
    ptc_ui_status_trace_record(&trace, "runtime", PTC_UI_STATUS_OTHER, 2000000000, 1, 0);
    check(!trace.active && contains("config_or_runtime_changed"), "backend identity change stops");
    fixture(3); init(1000000000); mem.fail_appends = true;
    ptc_ui_status_trace_record(&trace, "io-failure", PTC_UI_STATUS_OTHER, 2000000000, 1, 0);
    mem.fail_appends = false;
    ptc_ui_status_trace_record(&trace, "no-retry", PTC_UI_STATUS_OTHER, 3000000000, 1, 0);
    check(!trace.active && trace.count == 0 && !contains("io-failure") && !contains("no-retry"), "append failure is latched, no retry");
    fixture(3); mem.fail_appends = true; init(1000000000); check(!trace.active, "header failure disables");
    fixture(3); init(1000000000); snprintf(output, sizeof(output), "%s", trace.output_path); init(1000000000);
    check(!trace.active && mem.storage.vtable->exists(&mem.storage, output), "existing instance output never overwritten");
    fixture(3); mem.fail_read_path_contains = "ui-status-trace-012345"; init(1000000000);
    check(!trace.active && !mem.storage.vtable->exists(&mem.storage, trace.output_path),
        "unknown output metadata never permits appending a new header");
    fixture(3); init(1000000000);
    ptc_ui_status_trace_record(&trace, "id\"private", PTC_UI_STATUS_OTHER, 2000000000, 1, 0);
    check(!trace.active && !contains("private") && contains("invalid_record"), "unsafe record content never logged");
    fixture(3); change("commit", cJSON_CreateString("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")); init(1000000000);
    check(!trace.active, "different candidate rejected");
    fixture(3); change("backend_boot_id", cJSON_CreateString("ffffffffffffffff")); init(1000000000);
    check(!trace.active, "different backend boot rejected");
    fixture(3); change("max_records", cJSON_CreateNumber(1.5)); init(1000000000); check(!trace.active, "fractional cap rejected");
    fixture(129); init(1000000000); check(!trace.active, "oversized cap rejected");
    fixture(3); change("expires_mono_ns", cJSON_CreateString("18446744073709551616")); init(1000000000);
    check(!trace.active, "deadline overflow rejected");
    fixture(3); change("expires_mono_ns", cJSON_CreateString("0120000000000")); init(1000000000);
    check(!trace.active, "noncanonical deadline rejected");
    fixture(3); init(120000000000); check(!trace.active, "expired configuration cannot start");
    fixture(3); change("expires_mono_ns", cJSON_CreateString("999000000000")); init(1000000000);
    check(!trace.active, "lease over 900 seconds rejected");
    fixture(3); mem.storage.vtable->read_text(&mem.storage, CONFIG, text, sizeof(text));
    text[strlen(text)-1] = 0; strcat(text, ",\"max_records\":3}"); put(CONFIG, text); init(1000000000);
    check(!trace.active, "duplicate keys rejected");
    fixture(3); mem.storage.vtable->read_text(&mem.storage, CONFIG, text, sizeof(text));
    text[strlen(text)-1] = 0; strcat(text, ",\"extra\":true}"); put(CONFIG, text); init(1000000000);
    check(!trace.active, "unknown keys rejected");
    fixture(3);
    ptc_ui_status_trace_init(&trace, &mem.storage, "app", COMMIT, RELEASE, "overlay", 1000000000);
    ptc_ui_status_trace_record(&trace, "12000-0001", PTC_UI_STATUS_OVERLAY_READY, 2000000000, -1, 0);
    check(contains("\"client\":\"overlay\"") && contains("\"focus_state\":-1"), "overlay does not invent applet focus");
    if (failures) return 1;
    puts("ui status trace tests passed");
    return 0;
}
