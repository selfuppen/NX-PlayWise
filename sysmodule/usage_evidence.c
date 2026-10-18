#include "usage_evidence.h"

#include <stdio.h>
#include <string.h>
#include "../third_party/cjson/cJSON.h"

static bool hex_id(const char *text, size_t size)
{
    return text && strlen(text) == size && strspn(text, "0123456789abcdef") == size;
}

static bool identity(cJSON *config, const char *key, const char *expected)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(config, key);
    return cJSON_IsString(value) && strcmp(value->valuestring, expected) == 0;
}

bool ptc_usage_evidence_init(PtcUsageEvidence *capture, PtcStorage *storage,
    const char *root, const char *boot_id, const char *commit, const char *release_id,
    uint64_t now_ns, PtcUsageEvidenceReader reader, void *ctx)
{
    memset(capture, 0, sizeof(*capture));
    capture->storage = storage;
    capture->reader = reader;
    capture->reader_ctx = ctx;
    if (!hex_id(commit, 40) || !hex_id(boot_id, 16) || !release_id ||
        strlen(release_id) >= sizeof(capture->release_id) ||
        strspn(release_id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.+-_") != strlen(release_id) ||
        !reader) return false;
    int n = snprintf(capture->config_path, sizeof(capture->config_path),
        "%s/support/usage-evidence.json", root);
    if (n < 0 || (size_t)n >= sizeof(capture->config_path) ||
        !storage->vtable->read_text(storage, capture->config_path, capture->config, sizeof(capture->config))) return false;
    cJSON *config = cJSON_ParseWithOpts(capture->config, NULL, true);
    cJSON *version = cJSON_GetObjectItemCaseSensitive(config, "schema_version");
    cJSON *run = cJSON_GetObjectItemCaseSensitive(config, "run_id");
    cJSON *duration = cJSON_GetObjectItemCaseSensitive(config, "duration_s");
    unsigned keys = 0;
    bool unique = true;
    for (cJSON *item = config ? config->child : NULL; item; item = item->next) {
        ++keys;
        if (!item->string) { unique = false; break; }
        for (cJSON *prior = config->child; prior != item; prior = prior->next)
            if (strcmp(prior->string, item->string) == 0) unique = false;
    }
    bool valid = cJSON_IsObject(config) && keys == 5 && unique &&
        cJSON_IsNumber(version) && version->valuedouble == 1 &&
        cJSON_IsString(run) && hex_id(run->valuestring, 32) &&
        identity(config, "commit", commit) && identity(config, "release_id", release_id) &&
        cJSON_IsNumber(duration) && duration->valuedouble == duration->valueint &&
        duration->valueint >= 300 && duration->valueint <= 7200;
    if (valid) {
        snprintf(capture->run_id, sizeof(capture->run_id), "%s", run->valuestring);
        snprintf(capture->boot_id, sizeof(capture->boot_id), "%s", boot_id);
        snprintf(capture->commit, sizeof(capture->commit), "%s", commit);
        snprintf(capture->release_id, sizeof(capture->release_id), "%s", release_id);
        capture->duration_ns = (uint64_t)duration->valueint * UINT64_C(1000000000);
        n = snprintf(capture->output_path, sizeof(capture->output_path),
            "%s/support/usage-evidence-%s-%s.jsonl", root, capture->run_id, boot_id);
        valid = n > 0 && (size_t)n < sizeof(capture->output_path) &&
            !storage->vtable->exists(storage, capture->output_path);
    }
    cJSON_Delete(config);
    if (!valid) return false;
    char line[512];
    n = snprintf(line, sizeof(line),
        "{\"schema_version\":1,\"kind\":\"usage_evidence_start\",\"commit\":\"%s\","
        "\"release_id\":\"%s\",\"run_id\":\"%s\",\"boot_id\":\"%s\","
        "\"started_ns\":\"%llu\",\"duration_ns\":\"%llu\",\"interval_ns\":\"5000000000\",\"append_diagnostics_version\":1}",
        commit, release_id, capture->run_id, boot_id,
        (unsigned long long)now_ns, (unsigned long long)capture->duration_ns);
    if (n < 0 || (size_t)n >= sizeof(line) || !storage->vtable->append_line(storage, capture->output_path, line)) return false;
    capture->started_ns = now_ns;
    capture->active = true;
    return true;
}

void ptc_usage_evidence_stop(PtcUsageEvidence *capture, const char *reason)
{
    if (!capture->active) return;
    capture->active = false; /* Latch before I/O; a failed stop record never restarts reads. */
    char line[512];
    const char *operation = capture->last_save.stage == PTC_STORAGE_APPEND_OPEN ? "open" :
        capture->last_save.stage == PTC_STORAGE_APPEND_WRITE ? "write" :
        capture->last_save.stage == PTC_STORAGE_APPEND_CLOSE ? "close" : "none";
    snprintf(line, sizeof(line),
        "{\"schema_version\":1,\"kind\":\"usage_evidence_stop\",\"reason\":\"%s\","
        "\"samples\":%u,\"consecutive_failures\":%u,\"save_failures\":%u,\"last_failed_sample\":%u,"
        "\"sample_open_retries\":%u,\"last_save_errno\":%d,\"last_save_result\":%u,\"last_save_operation\":\"%s\"}",
        reason, capture->samples, capture->failures, capture->save_failures,
        capture->last_failed_sample, capture->sample_open_retries, capture->last_save.system_errno,
        capture->last_save.platform_result, operation);
    (void)capture->storage->vtable->append_line(capture->storage, capture->output_path, line);
}

void ptc_usage_evidence_set_append_diagnostics(PtcUsageEvidence *capture,
    PtcStorageAppendDiagnosticsReader reader, void *ctx)
{
    capture->append_diagnostics = reader;
    capture->append_diagnostics_ctx = ctx;
}

void ptc_usage_evidence_tick(PtcUsageEvidence *capture, uint64_t now_ns)
{
    if (!capture->active) return;
    if (now_ns < capture->started_ns || (capture->samples && now_ns < capture->last_ns)) {
        ptc_usage_evidence_stop(capture, "monotonic_regression"); return;
    }
    if (now_ns - capture->started_ns >= capture->duration_ns) {
        ptc_usage_evidence_stop(capture, "lease_expired"); return;
    }
    if (capture->samples && now_ns - capture->last_ns < UINT64_C(5000000000)) return;
    char config[512];
    if (!capture->storage->vtable->read_text(capture->storage, capture->config_path, config, sizeof(config)) ||
        strcmp(config, capture->config) != 0) {
        ptc_usage_evidence_stop(capture, "config_removed_or_changed"); return;
    }
    capture->last_ns = now_ns;
    ++capture->samples;
    PtcUsageEvidenceSample sample;
    memset(&sample, 0, sizeof(sample));
    bool valid = capture->reader(capture->reader_ctx, &sample);
    char hex[137];
    for (size_t i = 0; i < sizeof(sample.settings); ++i) snprintf(hex + i * 2, 3, "%02x", sample.settings[i]);
    char line[2048];
    int n = snprintf(line, sizeof(line),
        "{\"schema_version\":1,\"kind\":\"usage_evidence_sample\",\"source\":\"pctl_1952\","
        "\"scope\":\"console_usable_time\",\"run_id\":\"%s\",\"boot_id\":\"%s\",\"valid\":%s,\"sample_index\":%u,"
        "\"begin_ns\":\"%llu\",\"end_ns\":\"%llu\",\"unix_before\":%lld,\"unix_after\":%lld,"
        "\"day_before\":%u,\"day_after\":%u,\"clock_before_valid\":%s,\"clock_after_valid\":%s,"
        "\"start_timer_calls\":\"%llu\",\"session_result\":%u,\"timer_result\":%u,\"timer_enabled\":%s,"
        "\"remaining_result\":%u,\"signed_remaining_ns\":\"%lld\",\"restricted_result\":%u,\"restricted\":%s,"
        "\"spent_result\":%u,\"total_ns\":\"%lld\",\"settings_result\":%u,\"settings_hex\":\"%s\","
        "\"unlocked_result\":%u,\"temporarily_unlocked\":%s}",
        capture->run_id, capture->boot_id, valid ? "true" : "false", capture->samples,
        (unsigned long long)sample.begin_ns, (unsigned long long)sample.end_ns,
        (long long)sample.unix_before, (long long)sample.unix_after, sample.day_before, sample.day_after,
        sample.clock_before_valid ? "true" : "false", sample.clock_after_valid ? "true" : "false",
        (unsigned long long)sample.start_timer_calls, sample.session_result, sample.timer_result,
        sample.timer_enabled ? "true" : "false", sample.remaining_result, (long long)sample.remaining_ns,
        sample.restricted_result, sample.restricted ? "true" : "false", sample.spent_result,
        (long long)sample.spent_ns, sample.settings_result, hex, sample.unlocked_result,
        sample.temporarily_unlocked ? "true" : "false");
    bool serialized = n > 0 && (size_t)n < sizeof(line);
    bool saved = serialized &&
        capture->storage->vtable->append_line(capture->storage, capture->output_path, line);
    PtcStorageAppendDiagnostics io = {0};
    if (serialized && capture->append_diagnostics)
        io = capture->append_diagnostics(capture->append_diagnostics_ctx);
    if (io.open_attempts > 1U) capture->sample_open_retries += io.open_attempts - 1U;
    if (!saved) {
        ++capture->save_failures;
        capture->last_failed_sample = capture->samples;
        capture->last_save = io;
    }
    if (!valid || !saved) ++capture->failures; else capture->failures = 0;
    if (capture->failures >= 3) ptc_usage_evidence_stop(capture, "failure_limit");
}
