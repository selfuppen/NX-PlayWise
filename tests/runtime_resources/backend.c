#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sysmodule/sysmodule_internal.h"
#include "platform/host/mem_storage.h"
#include "platform/host/fake_time.h"
#include "third_party/cjson/cJSON.h"

extern unsigned ptc_resource_rules_parses, ptc_resource_state_parses;
static int failures;
static unsigned reads, library_reads;
static bool fail_metadata;
static const PtcStorageVTable *original;
static PtcStorageVTable counted;
static size_t json_allocations, json_allocated_bytes;
static void *json_allocate(size_t size)
{
    ++json_allocations;
    json_allocated_bytes += size;
    return malloc(size);
}

static void check(bool value, const char *label)
{
    if (!value) { fprintf(stderr, "FAIL resource regression: %s\n", label); ++failures; }
}

static bool read_text(PtcStorage *s, const char *path, char *out, size_t size)
{
    ++reads;
    if (strstr(path, "/calendars/library/")) ++library_reads;
    return original->read_text(s, path, out, size);
}

static bool metadata(PtcStorage *s, const char *path, PtcStorageMetadata *out)
{
    return !fail_metadata && original->metadata(s, path, out);
}

static void put(PtcMemStorage *mem, const char *path, const char *text)
{
    check(mem->storage.vtable->write_text_atomic(&mem->storage, path, text), "write fixture");
}

int main(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    PtcSysmodule *core = calloc(1, sizeof(*core));
    PtcCalendarRuntime *runtime = calloc(1, sizeof(*runtime));
    PtcCalendarIndex *selection = calloc(1, sizeof(*selection));
    PtcFakeTime clock;
    PtcRules rules, roundtrip;
    PtcRuntimeState state, state_roundtrip;
    char saved_rules[6144], saved_state[4096], path[320], text[320];
    unsigned cold_rules, cold_state, warm_reads, warm_rules, warm_state;
    unsigned legacy_calendar_reads, single_calendar_reads;
    size_t legacy_allocations, single_allocations, legacy_bytes, single_bytes;
    size_t cache_bytes = sizeof(core->rules_cache) + sizeof(core->state_cache);
    if (!mem || !core || !runtime || !selection) return 1;
    check(10240u - cache_bytes >= 8192u, "replace 10KiB text caches with at least 8KiB net savings");
    ptc_mem_storage_init(mem);
    original = mem->storage.vtable;
    counted = *original;
    counted.read_text = read_text;
    counted.metadata = metadata;
    mem->storage.vtable = &counted;
    ptc_fake_time_init(&clock, 1000, 2380, 496);
    core->storage = &mem->storage;
    core->time_provider = &clock.provider;
    snprintf(core->app_root, sizeof(core->app_root), "app");
    put(mem, "app/rules.json", "{\"version\":1,\"daily_buffer_minutes\":10}");
    put(mem, "app/state.json", "{\"version\":1,\"summary_grant_minutes\":17}");
    check(load_rules(core, &rules) && load_state(core, &state), "cold valid load");
    cold_rules = ptc_resource_rules_parses;
    cold_state = ptc_resource_state_parses;
    reads = 0;
    for (int i = 0; i < 1000; ++i) {
        check(load_rules(core, &rules) && load_state(core, &state), "warm valid load");
        check(rules.autonomy_policy.daily_buffer_minutes == 10 && state.summary_grant_minutes == 17,
            "warm values match disk");
        check(rules.calendar == &core->calendar_runtime.set && core->rules_cache.calendar == NULL,
            "rule cache cannot own stale calendar pointers");
    }
    warm_reads = reads;
    warm_rules = ptc_resource_rules_parses - cold_rules;
    warm_state = ptc_resource_state_parses - cold_state;
    check(!warm_reads && !warm_rules && !warm_state, "warm cache avoids text reads and reparsing");
    rules.autonomy_policy.daily_buffer_minutes = 15;
    state.summary_grant_minutes = 19;
    check(save_rules(core, &rules) && save_state(core, &state, 1000), "save both caches");
    check(load_rules(core, &rules) && load_state(core, &state), "read saved values from cache");
    check(original->read_text(&mem->storage, "app/rules.json", saved_rules, sizeof(saved_rules)) &&
        original->read_text(&mem->storage, "app/state.json", saved_state, sizeof(saved_state)), "saved JSON exists");
    invalidate_all_caches(core);
    check(load_rules(core, &roundtrip) && load_state(core, &state_roundtrip), "disk roundtrip");
    /* C structure padding is not semantic state. Compare the complete persisted
       representation of the cached value with a fresh parse of the same file. */
    check(save_rules(core, &rules) && save_state(core, &state, 1000), "serialize cached values");
    check(original->read_text(&mem->storage, "app/rules.json", saved_rules, sizeof(saved_rules)) &&
        original->read_text(&mem->storage, "app/state.json", saved_state, sizeof(saved_state)), "read cached serialization");
    check(save_rules(core, &roundtrip) && save_state(core, &state_roundtrip, 1000), "serialize disk roundtrip");
    {
        char disk_rules[6144], disk_state[4096];
        check(original->read_text(&mem->storage, "app/rules.json", disk_rules, sizeof(disk_rules)) &&
            original->read_text(&mem->storage, "app/state.json", disk_state, sizeof(disk_state)) &&
            !strcmp(saved_rules, disk_rules) && !strcmp(saved_state, disk_state),
            "cache equals serialized/parsed values");
    }
    fail_metadata = true;
    check(!load_rules(core, &rules) && !load_state(core, &state), "unknown metadata rejects cached success");
    fail_metadata = false;
    mem->fail_reads = true;
    check(!load_state(core, &state), "read failure cannot return stale state");
    mem->fail_reads = false;
    check(load_rules(core, &rules) && load_state(core, &state), "recover after failed metadata/read");
    put(mem, "app/rules.json", "{\"version\":99}");
    put(mem, "app/state.json", "{\"version\":99}");
    for (int i = 0; i < 2; ++i)
        check(!load_rules(core, &rules) && !load_state(core, &state), "bad JSON never publishes success cache");
    put(mem, "app/rules.json", saved_rules);
    put(mem, "app/state.json", saved_state);
    check(load_rules(core, &rules) && load_state(core, &state), "repair loads current data");
    mem->fail_writes = true;
    rules.autonomy_policy.daily_buffer_minutes = 5;
    state.summary_grant_minutes = 999;
    check(!save_rules(core, &rules) && !save_state(core, &state, 1000), "failed saves rejected");
    mem->fail_writes = false;
    check(load_rules(core, &rules) && load_state(core, &state) &&
        rules.autonomy_policy.daily_buffer_minutes == 15 && state.summary_grant_minutes == 19,
        "failed saves do not publish candidate values");
    check(original->remove_path(&mem->storage, "app/rules.json") &&
        original->remove_path(&mem->storage, "app/state.json"), "delete cached files");
    check(load_rules(core, &rules) && load_state(core, &state) &&
        rules.autonomy_policy.daily_buffer_minutes == 0 && state.summary_grant_minutes == 0,
        "missing files restore defaults");
    put(mem, "app/rules.json", ""); put(mem, "app/state.json", "");
    check(!load_rules(core, &rules) && !load_state(core, &state), "empty files are not missing defaults");

    snprintf(selection->option_id, sizeof(selection->option_id), "JP");
    selection->count = 4;
    for (unsigned i = 0; i < 4; ++i) {
        PtcCalendarIndexEntry *entry = &selection->entries[i];
        entry->year = (uint16_t)(2026 + i);
        snprintf(entry->region_id, sizeof(entry->region_id), "JP");
        snprintf(entry->region_name, sizeof(entry->region_name), "Japan");
        snprintf(text, sizeof(text), "{\"format_version\":1,\"region_id\":\"JP\",\"region_name\":\"Japan\","
            "\"year\":%u,\"holidays\":[],\"workdays\":[]}", entry->year);
        ptc_calendar_sha256_hex(text, strlen(text), entry->sha256);
        snprintf(path, sizeof(path), "app/calendars/library/JP-%u-%s.json", entry->year, entry->sha256);
        put(mem, path, text);
    }
    check(ptc_calendar_index_save(&mem->storage, "app", true, selection), "save four-year calendar");
    uint16_t day;
    check(ptc_day_index_from_date(2027, 12, 31, &day), "fixed calendar day");
    cJSON_Hooks hooks = {json_allocate, free};
    cJSON_InitHooks(&hooks);
    json_allocations = json_allocated_bytes = 0;
    library_reads = 0;
    check(ptc_calendar_index_load(&mem->storage, "app", true, selection, NULL) &&
        ptc_calendar_selection_validate(&mem->storage, "app", selection) &&
        ptc_calendar_runtime_from_selection(&mem->storage, "app", selection, day, runtime), "legacy two-pass composition");
    legacy_calendar_reads = library_reads;
    legacy_allocations = json_allocations; legacy_bytes = json_allocated_bytes;
    json_allocations = json_allocated_bytes = 0;
    library_reads = 0;
    check(ptc_calendar_runtime_load(&mem->storage, "app", day, runtime) && runtime->set.count == 3,
        "single pass retains nearby three years");
    single_calendar_reads = library_reads;
    single_allocations = json_allocations; single_bytes = json_allocated_bytes;
    cJSON_InitHooks(NULL);
    check(single_allocations < legacy_allocations && single_bytes < legacy_bytes,
        "single-pass calendar reduces JSON allocation count and cumulative allocated bytes");
    check(legacy_calendar_reads == 7 && single_calendar_reads == 4, "each selected file read once");
    check(runtime->set.years[0] == &runtime->years[0] && runtime->years[0].year == 2026,
        "runtime year pointers bind to destination");
    check(ptc_calendar_runtime_load(&mem->storage, "app", (uint16_t)(day + 1), runtime) &&
        runtime->set.count == 3 && runtime->years[0].year == 2027, "new year shifts retained window");
    put(mem, path, "{}"); /* 2029 is outside the first runtime's nearby window. */
    check(!ptc_calendar_runtime_load(&mem->storage, "app", day, runtime) &&
        runtime->set.count == 0 && runtime->option_id[0] == '\0', "far-year corruption clears prior runtime");
    put(mem, path, text);
    mem->fail_read_path_contains = "JP-2029-";
    check(!ptc_calendar_runtime_load(&mem->storage, "app", day, runtime), "far-year read failure is not skipped");
    mem->fail_read_path_contains = NULL;
    put(mem, "app/calendars/active.json", "{}");
    check(!ptc_calendar_runtime_load(&mem->storage, "app", day, runtime) && !runtime->set.count,
        "invalid index leaves no stale calendar");
    printf("{\"schema_version\":1,\"cache_before_bytes\":10240,\"cache_after_bytes\":%zu,"
        "\"warm_loads\":1000,\"warm_rules_parses\":%u,\"warm_state_parses\":%u,\"warm_text_reads\":%u,"
        "\"calendar_reads_before\":%u,\"calendar_reads_after\":%u,"
        "\"calendar_json_allocations_before\":%zu,\"calendar_json_allocations_after\":%zu,"
        "\"calendar_json_allocated_bytes_before\":%zu,\"calendar_json_allocated_bytes_after\":%zu,\"failures\":%d}\n",
        cache_bytes, warm_rules, warm_state, warm_reads, legacy_calendar_reads, single_calendar_reads,
        legacy_allocations, single_allocations, legacy_bytes, single_bytes, failures);
    free(selection); free(runtime); free(core); free(mem);
    return failures ? 1 : 0;
}
