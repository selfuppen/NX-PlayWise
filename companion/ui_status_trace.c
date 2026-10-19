#include "ui_status_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/crypto/sha256.h"
#include "../third_party/cjson/cJSON.h"

#define TRACE_CONFIG_SIZE 768
#define TRACE_MAX_NS UINT64_C(900000000000)

static const char *string(const cJSON *root, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static bool hex(const char *s, size_t size)
{
    size_t i;
    if (!s || strlen(s) != size) return false;
    for (i = 0; i < size; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

static bool atom(const char *s, size_t max)
{
    size_t i, size = s ? strlen(s) : 0;
    if (!size || size >= max) return false;
    for (i = 0; i < size; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'z') ||
              (s[i] >= 'A' && s[i] <= 'Z') || strchr("-_.+", s[i]))) return false;
    return true;
}

static void hash(const char *text, uint8_t digest[32])
{
    PtcSha256Ctx ctx;
    ptc_sha256_init(&ctx);
    ptc_sha256_update(&ctx, (const uint8_t *)text, strlen(text));
    ptc_sha256_final(&ctx, digest);
}

static bool runtime_matches(PtcUiStatusTrace *t)
{
    char text[768];
    cJSON *value;
    bool ok;
    if (!t->storage->vtable->read_text(t->storage, t->runtime_path, text, sizeof(text))) return false;
    value = cJSON_Parse(text);
    ok = cJSON_IsObject(value) && strcmp(string(value, "release_id"), t->release_id) == 0 &&
        strcmp(string(value, "boot_id"), t->backend_boot_id) == 0 &&
        strcmp(string(value, "profile"), "release") == 0;
    cJSON_Delete(value);
    return ok;
}

static void stop(PtcUiStatusTrace *t, const char *reason, uint64_t now_ns)
{
    char line[224];
    if (!t || !t->active) return;
    t->active = false;
    snprintf(line, sizeof(line), "{\"schema_version\":1,\"kind\":\"ui_status_trace_stop\","
        "\"reason\":\"%s\",\"records\":%u,\"monotonic_ns\":\"%llu\"}",
        reason, t->count, (unsigned long long)now_ns);
    (void)t->storage->vtable->append_line(t->storage, t->output_path, line);
}

void ptc_ui_status_trace_init(PtcUiStatusTrace *t, PtcStorage *storage,
    const char *root, const char *commit, const char *release_id,
    const char *client, uint64_t now_ns)
{
    static const char *KEYS[] = { "schema_version", "run_id", "commit", "release_id",
        "backend_boot_id", "expires_mono_ns", "max_records" };
    char text[TRACE_CONFIG_SIZE], line[768];
    cJSON *value;
    const cJSON *item, *version, *max_records;
    const char *expiry, *run_id;
    uint64_t deadline = 0;
    unsigned mask = 0;
    PtcStorageMetadata metadata;
    size_t i;
    int written;
    if (!t) return;
    memset(t, 0, sizeof(*t));
    if (!storage || !storage->vtable || !root || !hex(commit, 40) || !atom(release_id, 128) ||
        !client || (strcmp(client, "nro") && strcmp(client, "overlay")) || !now_ns) return;
    if (!storage->vtable->read_text || !storage->vtable->append_line || !storage->vtable->metadata) return;
    written = snprintf(t->config_path, sizeof(t->config_path), "%s/support/ui-status-trace.json", root);
    if (written < 0 || (size_t)written >= sizeof(t->config_path)) return;
    /* Absence is the production default: no trace file or directory is created. */
    if (!storage->vtable->read_text(storage, t->config_path, text, sizeof(text))) return;
    value = cJSON_ParseWithOpts(text, NULL, true);
    if (!cJSON_IsObject(value)) { cJSON_Delete(value); return; }
    cJSON_ArrayForEach(item, value) {
        for (i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); ++i)
            if (strcmp(item->string, KEYS[i]) == 0) break;
        if (i == sizeof(KEYS) / sizeof(KEYS[0]) || (mask & (1u << i))) goto done;
        mask |= 1u << i;
    }
    version = cJSON_GetObjectItemCaseSensitive(value, "schema_version");
    max_records = cJSON_GetObjectItemCaseSensitive(value, "max_records");
    expiry = string(value, "expires_mono_ns");
    run_id = string(value, "run_id");
    if (mask != 127 || !cJSON_IsNumber(version) || version->valuedouble != 1 ||
        !cJSON_IsNumber(max_records) || max_records->valuedouble < 1 || max_records->valuedouble > 128 ||
        max_records->valuedouble != (int)max_records->valuedouble ||
        !hex(run_id, 32) || !hex(string(value, "backend_boot_id"), 16) ||
        strcmp(string(value, "commit"), commit) || strcmp(string(value, "release_id"), release_id)) goto done;
    if (!expiry[0] || expiry[0] == '0' || strlen(expiry) > 20) goto done;
    for (i = 0; expiry[i]; ++i) {
        unsigned digit = (unsigned)(expiry[i] - '0');
        if (digit > 9 || deadline > (UINT64_MAX - digit) / 10) goto done;
        deadline = deadline * 10 + digit;
    }
    if (deadline <= now_ns || deadline - now_ns > TRACE_MAX_NS) goto done;
    t->storage = storage;
    snprintf(t->release_id, sizeof(t->release_id), "%s", release_id);
    snprintf(t->backend_boot_id, sizeof(t->backend_boot_id), "%s", string(value, "backend_boot_id"));
    written = snprintf(t->runtime_path, sizeof(t->runtime_path), "%s/handover/runtime-ready.json", root);
    if (written < 0 || (size_t)written >= sizeof(t->runtime_path) || !runtime_matches(t)) goto done;
    written = snprintf(t->output_path, sizeof(t->output_path), "%s/support/ui-status-trace-%s-%s-%016llx.jsonl",
        root, run_id, client, (unsigned long long)now_ns);
    /* A failed stat is unknown, not permission to append to an existing stream. */
    if (written < 0 || (size_t)written >= sizeof(t->output_path) ||
        !storage->vtable->metadata(storage, t->output_path, &metadata) ||
        metadata.type != PTC_STORAGE_ENTRY_MISSING) goto done;
    t->expires_ns = deadline;
    t->started_ns = t->last_ns = now_ns;
    t->max_records = (unsigned)max_records->valuedouble;
    hash(text, t->config_hash);
    snprintf(line, sizeof(line), "{\"schema_version\":1,\"kind\":\"ui_status_trace_start\","
        "\"run_id\":\"%s\",\"commit\":\"%s\",\"release_id\":\"%s\",\"backend_boot_id\":\"%s\","
        "\"client\":\"%s\",\"started_mono_ns\":\"%llu\",\"expires_mono_ns\":\"%llu\",\"max_records\":%u}",
        run_id, commit, release_id, t->backend_boot_id, client, (unsigned long long)now_ns,
        (unsigned long long)deadline, t->max_records);
    t->active = storage->vtable->append_line(storage, t->output_path, line);
done:
    cJSON_Delete(value);
}

void ptc_ui_status_trace_record(PtcUiStatusTrace *t, const char *request_id,
    PtcUiStatusOrigin origin, uint64_t now_ns, int focus_state, int transport_status)
{
    char text[TRACE_CONFIG_SIZE], line[384];
    uint8_t digest[32];
    const char *reason;
    if (!t || !t->active) return;
    if (now_ns < t->last_ns) { stop(t, "clock_invalid", now_ns); return; }
    if (now_ns >= t->expires_ns) { stop(t, "lease_expired", now_ns); return; }
    if (!t->storage->vtable->read_text(t->storage, t->config_path, text, sizeof(text))) {
        stop(t, "config_removed_or_changed", now_ns); return;
    }
    hash(text, digest);
    if (memcmp(digest, t->config_hash, sizeof(digest)) || !runtime_matches(t)) {
        stop(t, "config_or_runtime_changed", now_ns); return;
    }
    switch (origin) {
    case PTC_UI_STATUS_OTHER: reason = "other"; break;
    case PTC_UI_STATUS_BUTTON_Y: reason = "button_y"; break;
    case PTC_UI_STATUS_RESUME: reason = "resume"; break;
    case PTC_UI_STATUS_FOCUS: reason = "focus"; break;
    case PTC_UI_STATUS_RESUME_AND_FOCUS: reason = "resume_and_focus"; break;
    case PTC_UI_STATUS_OVERLAY_READY: reason = "overlay_ready"; break;
    case PTC_UI_STATUS_TOUCH: reason = "touch"; break;
    default: stop(t, "invalid_record", now_ns); return;
    }
    if (!atom(request_id, 80) || focus_state < -1 || focus_state > 3) { stop(t, "invalid_record", now_ns); return; }
    snprintf(line, sizeof(line), "{\"schema_version\":1,\"kind\":\"ui_status_trace_request\","
        "\"sequence\":%u,\"request_id\":\"%s\",\"origin\":\"%s\",\"monotonic_ns\":\"%llu\","
        "\"focus_state\":%d,\"transport_status\":%d}", t->count + 1, request_id, reason,
        (unsigned long long)now_ns, focus_state, transport_status);
    /* Do not retry a partial append, and never change the original request result. */
    if (!t->storage->vtable->append_line(t->storage, t->output_path, line)) { t->active = false; return; }
    ++t->count;
    t->last_ns = now_ns;
    if (t->count == t->max_records) stop(t, "record_limit", now_ns);
}

void ptc_ui_status_trace_close(PtcUiStatusTrace *t, uint64_t now_ns)
{
    stop(t, "client_exit", now_ns);
}
