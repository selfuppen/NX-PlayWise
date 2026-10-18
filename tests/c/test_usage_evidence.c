#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../sysmodule/usage_evidence.h"
#include "../../third_party/cjson/cJSON.h"

static const char *commit = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char *release = "playwise-test+aaaaaaaaaaaa";
static char config[512], last_line[2048];
static bool missing, existing, save_failure, read_failure;
static int calls, lines;
static PtcUsageEvidence capture;

static bool read_text(PtcStorage *s, const char *path, char *out, size_t size)
{
    (void)s; assert(strstr(path, "/support/usage-evidence.json"));
    if (missing) return false;
    snprintf(out, size, "%s", config); return true;
}
static bool exists(PtcStorage *s, const char *path) { (void)s; (void)path; return existing; }
static bool append(PtcStorage *s, const char *path, const char *line)
{
    (void)s; assert(strstr(path, "0123456789abcdef0123456789abcdef-0123456789abcdef.jsonl"));
    ++lines; snprintf(last_line, sizeof(last_line), "%s", line);
    cJSON *j = cJSON_ParseWithOpts(line, NULL, true); assert(j); cJSON_Delete(j);
    return !save_failure;
}
static const PtcStorageVTable vtable = {.read_text = read_text, .exists = exists, .append_line = append};
static PtcStorage storage = {&vtable, NULL};
static PtcStorageAppendDiagnostics append_diagnostics(void *ctx)
{
    assert(ctx == &calls);
    PtcStorageAppendDiagnostics io = {0};
    io.open_attempts = 3;
    if (save_failure) {
        io.stage = PTC_STORAGE_APPEND_CLOSE;
        io.system_errno = 5;
        io.platform_result = 1234;
    }
    return io;
}
static bool sample(void *ctx, PtcUsageEvidenceSample *out)
{
    assert(ctx == &calls); ++calls;
    out->begin_ns = 10000000000ULL; out->end_ns = 10100000000ULL;
    out->unix_before = out->unix_after = 1791914860;
    out->day_before = out->day_after = 2478;
    out->clock_before_valid = out->clock_after_valid = true;
    out->spent_ns = INT64_C(4191000000000); out->remaining_ns = -INT64_C(591000000000);
    out->start_timer_calls = 2; out->timer_enabled = true;
    out->spent_result = read_failure ? 777 : 0;
    memset(out->settings, 0xaa, sizeof(out->settings));
    return !read_failure;
}
static bool init(void)
{
    return ptc_usage_evidence_init(&capture, &storage, "root", "0123456789abcdef", commit, release, 1, sample, &calls);
}
static void reset(void)
{
    snprintf(config, sizeof(config),
        "{\"schema_version\":1,\"run_id\":\"0123456789abcdef0123456789abcdef\",\"commit\":\"%s\","
        "\"release_id\":\"%s\",\"duration_s\":300}", commit, release);
    missing = existing = save_failure = read_failure = false;
    calls = lines = 0;
}
int main(void)
{
    reset(); missing = true; assert(!init() && calls == 0 && lines == 0);
    reset(); existing = true; assert(!init() && calls == 0 && lines == 0);
    reset(); save_failure = true; assert(!init() && !capture.active && calls == 0);
    reset(); strcat(config, " garbage"); assert(!init());
    reset(); config[strlen(config)-1] = ','; strcat(config, "\"duration_s\":300}"); assert(!init());
    reset(); config[0] = '['; assert(!init());
    reset(); config[80] = 'z'; assert(!init()); /* Identity/run binding cannot be bypassed. */
    reset(); assert(init());
    ptc_usage_evidence_tick(&capture, 1); assert(calls == 1);
    cJSON *j = cJSON_Parse(last_line); assert(j);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "valid")));
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(j, "signed_remaining_ns")->valuestring, "-591000000000") == 0);
    assert(strlen(cJSON_GetObjectItemCaseSensitive(j, "settings_hex")->valuestring) == 136);
    cJSON_Delete(j);
    for (unsigned i = 1; i <= 19; ++i) ptc_usage_evidence_tick(&capture, 1 + i * 250000000ULL);
    assert(calls == 1);
    ptc_usage_evidence_tick(&capture, 5000000001ULL); assert(calls == 2);
    read_failure = true;
    ptc_usage_evidence_tick(&capture, 10000000001ULL);
    ptc_usage_evidence_tick(&capture, 15000000001ULL);
    assert(capture.active && capture.failures == 2);
    read_failure = false; ptc_usage_evidence_tick(&capture, 20000000001ULL);
    assert(capture.failures == 0);
    save_failure = true;
    for (unsigned i = 5; i <= 7; ++i) ptc_usage_evidence_tick(&capture, 1 + i * 5000000000ULL);
    assert(!capture.active && capture.failures == 3);
    int before = calls; save_failure = false;
    ptc_usage_evidence_tick(&capture, 45000000001ULL); assert(calls == before);
    reset(); assert(init()); read_failure = true;
    for (unsigned i = 0; i < 3; ++i) ptc_usage_evidence_tick(&capture, 1 + i * 5000000000ULL);
    assert(!capture.active && calls == 3 && strstr(last_line, "failure_limit"));
    reset(); assert(init());
    ptc_usage_evidence_tick(&capture, 300000000001ULL);
    assert(!capture.active && calls == 0 && strstr(last_line, "lease_expired"));
    reset(); assert(init()); missing = true;
    ptc_usage_evidence_tick(&capture, 1); assert(!capture.active && calls == 0);
    missing = false; ptc_usage_evidence_tick(&capture, 5000000001ULL); assert(calls == 0);
    reset(); assert(init()); strcat(config, " ");
    ptc_usage_evidence_tick(&capture, 1); assert(!capture.active && calls == 0);
    reset(); assert(init()); ptc_usage_evidence_tick(&capture, 0);
    assert(!capture.active && strstr(last_line, "monotonic_regression"));
    reset(); assert(init()); ptc_usage_evidence_stop(&capture, "process_handoff");
    before = lines; ptc_usage_evidence_stop(&capture, "process_handoff"); assert(lines == before);
    reset(); assert(init());
    ptc_usage_evidence_set_append_diagnostics(&capture, append_diagnostics, &calls);
    save_failure = true; ptc_usage_evidence_tick(&capture, 1);
    save_failure = false; ptc_usage_evidence_tick(&capture, 5000000001ULL);
    assert(capture.active && capture.failures == 0 && capture.save_failures == 1);
    assert(capture.last_failed_sample == 1 && capture.sample_open_retries == 4);
    j = cJSON_Parse(last_line); assert(j);
    assert(cJSON_GetObjectItemCaseSensitive(j, "sample_index")->valueint == 2);
    cJSON_Delete(j);
    ptc_usage_evidence_stop(&capture, "config_removed_or_changed");
    j = cJSON_Parse(last_line); assert(j);
    assert(cJSON_GetObjectItemCaseSensitive(j, "save_failures")->valueint == 1);
    assert(cJSON_GetObjectItemCaseSensitive(j, "last_save_errno")->valueint == 5);
    assert(cJSON_GetObjectItemCaseSensitive(j, "last_save_result")->valueint == 1234);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(j, "last_save_operation")->valuestring, "close") == 0);
    cJSON_Delete(j);
    puts("PASS standard evidence identity, raw precision, frequency, leases and latched failure stop");
    return 0;
}
