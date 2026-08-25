#include "lab_session.h"

#ifdef PLAYWISE_DEVICE_LAB

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/crypto/sha256.h"
#include "../common/protocol/result_builder.h"
#include "../common/time/ptc_time.h"
#include "../common/version.h"

#define LAB_PHASE_SECONDS 75LL
#define LAB_RESTRICTION_SECONDS 15LL
#define LAB_RESTRICTION_EVENT_POLL_MS 100U
#define LAB_REPORT_BUFFER 16384U

static const char *const LAB_PHASES[] = {
    "home_stopped", "home_started", "game_foreground",
    "game_suspended", "sleep_wake", "restriction_effect"
};

typedef struct {
    char run_id[48];
    char state[32];
    int next_phase;
    char active_phase[32];
    int64_t started_at;
    int64_t deadline;
    char observation[32];
    bool event_armed;
    int restriction_weekday;
    bool restored;
    char restore_verdict[32];
    PtcPctlSettingsSnapshot original;
    PtcPctlForensicSample before;
} LabState;

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    return p;
}

static const char *find_key(const char *text, const char *key)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(text, pattern);
}

static bool json_string(const char *text, const char *key, char *out, size_t out_size)
{
    const char *pos = find_key(text, key);
    const char *end;
    size_t size;
    if (!pos || !(pos = strchr(pos, ':'))) return false;
    pos = skip_ws(pos + 1);
    if (*pos++ != '\"' || !(end = strchr(pos, '\"'))) return false;
    size = (size_t)(end - pos);
    if (size >= out_size) return false;
    memcpy(out, pos, size);
    out[size] = '\0';
    return true;
}

static bool json_i64(const char *text, const char *key, int64_t *out)
{
    const char *pos = find_key(text, key);
    char *end;
    long long value;
    if (!pos || !(pos = strchr(pos, ':'))) return false;
    pos = skip_ws(pos + 1);
    value = strtoll(pos, &end, 10);
    if (end == pos) return false;
    *out = (int64_t)value;
    return true;
}

static bool json_bool(const char *text, const char *key, bool *out)
{
    const char *pos = find_key(text, key);
    if (!pos || !(pos = strchr(pos, ':'))) return false;
    pos = skip_ws(pos + 1);
    if (strncmp(pos, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(pos, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static void bytes_hex(const uint8_t *bytes, size_t size, char *out, size_t out_size)
{
    static const char HEX[] = "0123456789abcdef";
    size_t i;
    if (out_size < size * 2U + 1U) { if (out_size) out[0] = '\0'; return; }
    for (i = 0; i < size; ++i) {
        out[i * 2U] = HEX[bytes[i] >> 4];
        out[i * 2U + 1U] = HEX[bytes[i] & 15U];
    }
    out[size * 2U] = '\0';
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool hex_bytes(const char *text, uint8_t *out, size_t size)
{
    size_t i;
    if (!text || strlen(text) != size * 2U) return false;
    for (i = 0; i < size; ++i) {
        int high = hex_value(text[i * 2U]);
        int low = hex_value(text[i * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        out[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static void settings_hash(const PtcPctlForensicSample *sample, char out[65])
{
    PtcSha256Ctx ctx;
    uint8_t digest[PTC_SHA256_DIGEST_SIZE];
    ptc_sha256_init(&ctx);
    ptc_sha256_update(&ctx, sample->settings, sizeof(sample->settings));
    ptc_sha256_final(&ctx, digest);
    bytes_hex(digest, sizeof(digest), out, 65);
}

static bool sample_ok(const PtcPctlForensicSample *sample)
{
    return sample->timer_enabled_result == 0 && sample->remaining_result == 0 &&
        sample->restricted_result == 0 && sample->spent_result == 0 && sample->settings_result == 0;
}

static void state_path(PtcSysmodule *sysmodule, char *out, size_t size)
{
    snprintf(out, size, "%s/lab/session.json", sysmodule->app_root);
}

static void report_path(PtcSysmodule *sysmodule, const LabState *state, char *out, size_t size)
{
    snprintf(out, size, "%s/reports/%s.json", sysmodule->app_root, state->run_id);
}

static bool save_state(PtcSysmodule *sysmodule, const LabState *state)
{
    char path[320];
    char original_hex[PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U + 1U];
    char before_hex[PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U + 1U];
    char text[2048];
    bytes_hex(state->original.data, sizeof(state->original.data), original_hex, sizeof(original_hex));
    bytes_hex(state->before.settings, sizeof(state->before.settings), before_hex, sizeof(before_hex));
    snprintf(text, sizeof(text),
        "{\"version\":1,\"run_id\":\"%s\",\"state\":\"%s\",\"next_phase\":%d,"
        "\"active_phase\":\"%s\",\"started_at\":%lld,\"deadline\":%lld,"
        "\"observation\":\"%s\",\"event_armed\":%s,\"restriction_weekday\":%d,\"restored\":%s,"
        "\"restore_verdict\":\"%s\",\"original_timer\":%s,\"original_hex\":\"%s\","
        "\"before_monotonic_ns\":%llu,\"before_timer_rc\":%u,\"before_timer\":%s,"
        "\"before_remaining_rc\":%u,\"before_remaining_ns\":%lld,"
        "\"before_restricted_rc\":%u,\"before_restricted\":%s,"
        "\"before_spent_rc\":%u,\"before_spent_ns\":%lld,"
        "\"before_settings_rc\":%u,\"before_settings_hex\":\"%s\"}\n",
        state->run_id, state->state, state->next_phase, state->active_phase,
        (long long)state->started_at, (long long)state->deadline, state->observation,
        state->event_armed ? "true" : "false", state->restriction_weekday,
        state->restored ? "true" : "false",
        state->restore_verdict, state->original.timer_enabled ? "true" : "false", original_hex,
        (unsigned long long)state->before.monotonic_ns,
        state->before.timer_enabled_result, state->before.timer_enabled ? "true" : "false",
        state->before.remaining_result, (long long)state->before.remaining_ns,
        state->before.restricted_result, state->before.restricted ? "true" : "false",
        state->before.spent_result, (long long)state->before.spent_ns,
        state->before.settings_result, before_hex);
    state_path(sysmodule, path, sizeof(path));
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text);
}

static bool load_state(PtcSysmodule *sysmodule, LabState *state)
{
    char path[320];
    char text[2048];
    char original_hex[PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U + 1U];
    char before_hex[PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U + 1U];
    int64_t value;
    memset(state, 0, sizeof(*state));
    state->restriction_weekday = -1;
    state_path(sysmodule, path, sizeof(path));
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) return false;
    if (!json_string(text, "run_id", state->run_id, sizeof(state->run_id)) ||
        !json_string(text, "state", state->state, sizeof(state->state)) ||
        !json_i64(text, "next_phase", &value) ||
        !json_string(text, "active_phase", state->active_phase, sizeof(state->active_phase)) ||
        !json_i64(text, "started_at", &state->started_at) || !json_i64(text, "deadline", &state->deadline) ||
        !json_string(text, "observation", state->observation, sizeof(state->observation)) ||
        !json_bool(text, "event_armed", &state->event_armed) || !json_bool(text, "restored", &state->restored) ||
        !json_string(text, "restore_verdict", state->restore_verdict, sizeof(state->restore_verdict)) ||
        !json_bool(text, "original_timer", &state->original.timer_enabled) ||
        !json_string(text, "original_hex", original_hex, sizeof(original_hex)) ||
        !json_string(text, "before_settings_hex", before_hex, sizeof(before_hex))) return false;
    state->next_phase = (int)value;
    if (json_i64(text, "restriction_weekday", &value) && value >= 0 && value < 7)
        state->restriction_weekday = (int)value;
    state->original.size = PTC_PCTL_OPAQUE_SETTINGS_SIZE;
    if (!hex_bytes(original_hex, state->original.data, sizeof(state->original.data)) ||
        !hex_bytes(before_hex, state->before.settings, sizeof(state->before.settings))) return false;
    if (json_i64(text, "before_monotonic_ns", &value)) state->before.monotonic_ns = (uint64_t)value;
    if (json_i64(text, "before_timer_rc", &value)) state->before.timer_enabled_result = (uint32_t)value;
    (void)json_bool(text, "before_timer", &state->before.timer_enabled);
    if (json_i64(text, "before_remaining_rc", &value)) state->before.remaining_result = (uint32_t)value;
    (void)json_i64(text, "before_remaining_ns", &state->before.remaining_ns);
    if (json_i64(text, "before_restricted_rc", &value)) state->before.restricted_result = (uint32_t)value;
    (void)json_bool(text, "before_restricted", &state->before.restricted);
    if (json_i64(text, "before_spent_rc", &value)) state->before.spent_result = (uint32_t)value;
    (void)json_i64(text, "before_spent_ns", &state->before.spent_ns);
    if (json_i64(text, "before_settings_rc", &value)) state->before.settings_result = (uint32_t)value;
    return true;
}

static bool write_result(PtcSysmodule *sysmodule, const PtcRequest *request,
    const LabState *state, PtcErrorCode error)
{
    PtcResultState result_state;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    char path[320];
    char report[320];
    char json[2048];
    size_t used;
    ptc_result_state_default(&result_state, now.day_index);
    if (error == PTC_ERR_OK) {
        (void)ptc_result_ok_json(json, sizeof(json), request->request_id, request->type_text,
            "device_lab", true, &result_state, now.unix_seconds);
    } else {
        (void)ptc_result_error_json(json, sizeof(json), request->request_id, request->type_text,
            "device_lab", true, error, &result_state, now.unix_seconds);
    }
    used = strlen(json);
    if (used >= 2U && json[used - 1U] == '\n' && json[used - 2U] == '}') json[used - 2U] = '\0';
    report_path(sysmodule, state, report, sizeof(report));
    snprintf(json + strlen(json), sizeof(json) - strlen(json),
        ",\"lab_session\":{\"run_id\":\"%s\",\"state\":\"%s\",\"next_phase\":%d,"
        "\"active_phase\":\"%s\",\"deadline\":%lld,\"restored\":%s,"
        "\"restore_verdict\":\"%s\",\"report_path\":\"%s\"}}\n",
        state->run_id, state->state, state->next_phase, state->active_phase,
        (long long)state->deadline, state->restored ? "true" : "false", state->restore_verdict, report);
    snprintf(path, sizeof(path), "%s/results/%s.json", sysmodule->app_root, request->request_id);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, json);
}

static bool write_public_evidence(PtcSysmodule *sysmodule, const LabState *state,
    const PtcPctlPublicParity *parity, bool same_value_write, bool same_value_restored)
{
    char path[320];
    char text[3072];
    bool comparable_1006 = parity->raw_temporary_unlocked_result == 0 &&
        parity->libnx_temporary_unlocked_result == 0;
    bool comparable_1031 = parity->raw_restriction_enabled_result == 0 &&
        parity->libnx_restriction_enabled_result == 0;
    bool comparable_1035 = parity->raw_current_settings_result == 0 &&
        parity->libnx_current_settings_result == 0;
    bool comparable_1457 = parity->raw_suspend_event_result == 0 &&
        parity->libnx_suspend_event_result == 0;
    bool comparable_1458 = parity->raw_alarm_disabled_result == 0 &&
        parity->libnx_alarm_disabled_result == 0;
    snprintf(path, sizeof(path), "%s/lab/public.json", sysmodule->app_root);
    snprintf(text, sizeof(text),
        "{\"commands\":{"
        "\"1006\":{\"raw_result\":%u,\"libnx_result\":%u,\"comparable\":%s,\"value_equal\":%s},"
        "\"1031\":{\"raw_result\":%u,\"libnx_result\":%u,\"comparable\":%s,\"value_equal\":%s},"
        "\"1035\":{\"raw_result\":%u,\"libnx_result\":%u,\"comparable\":%s,\"structure_equal\":%s},"
        "\"1457\":{\"raw_result\":%u,\"libnx_result\":%u,\"comparable\":%s,\"raw_handle_valid\":%s,\"libnx_handle_valid\":%s},"
        "\"1458\":{\"raw_result\":%u,\"libnx_result\":%u,\"comparable\":%s,\"value_equal\":%s}},"
        "\"settings_0x44\":{\"same_value_write_succeeded\":%s,\"exactly_restored\":%s},"
        "\"verdicts\":{\"ipc_callable\":%s,\"wire_shape_confirmed\":%s,\"product_semantics\":\"evidence_only\"}}",
        parity->raw_temporary_unlocked_result, parity->libnx_temporary_unlocked_result,
        comparable_1006 ? "true" : "false",
        comparable_1006 && parity->raw_temporary_unlocked == parity->libnx_temporary_unlocked ? "true" : "false",
        parity->raw_restriction_enabled_result, parity->libnx_restriction_enabled_result,
        comparable_1031 ? "true" : "false",
        comparable_1031 && parity->raw_restriction_enabled == parity->libnx_restriction_enabled ? "true" : "false",
        parity->raw_current_settings_result, parity->libnx_current_settings_result,
        comparable_1035 ? "true" : "false", comparable_1035 && parity->current_settings_equal ? "true" : "false",
        parity->raw_suspend_event_result, parity->libnx_suspend_event_result,
        comparable_1457 ? "true" : "false",
        parity->raw_suspend_event_valid ? "true" : "false", parity->libnx_suspend_event_valid ? "true" : "false",
        parity->raw_alarm_disabled_result, parity->libnx_alarm_disabled_result,
        comparable_1458 ? "true" : "false",
        comparable_1458 && parity->raw_alarm_disabled == parity->libnx_alarm_disabled ? "true" : "false",
        same_value_write ? "true" : "false", same_value_restored ? "true" : "false",
        parity->raw_temporary_unlocked_result == 0 && parity->raw_restriction_enabled_result == 0 ? "true" : "false",
        comparable_1035 && parity->current_settings_equal && same_value_write && same_value_restored ? "true" : "false");
    (void)state;
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text);
}

static bool read_fragment(PtcSysmodule *sysmodule, const char *relative, char *out, size_t size)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", sysmodule->app_root, relative);
    return sysmodule->storage->vtable->read_text(sysmodule->storage, path, out, size);
}

static bool append_text(char *out, size_t out_size, const char *value)
{
    size_t used = strlen(out);
    size_t added = strlen(value);
    if (used >= out_size || added >= out_size - used) return false;
    memcpy(out + used, value, added + 1U);
    return true;
}

static bool rebuild_report(PtcSysmodule *sysmodule, const LabState *state)
{
    char report[LAB_REPORT_BUFFER];
    char fragment[4096];
    char path[320];
    size_t i;
    unsigned int completed_phases = 0;
    bool observation_recorded = state->observation[0] != '\0';
    bool complete;
    int written = snprintf(report, sizeof(report),
        "{\"version\":1,\"run_id\":\"%s\",\"environment\":{\"title_id\":\"%s\","
        "\"ipc_service\":\"%s\",\"sd_root\":\"%s\",\"runtime\":",
        state->run_id, PLAYWISE_TITLE_ID, PLAYWISE_IPC_SERVICE, PLAYWISE_SD_ROOT);
    if (written < 0 || (size_t)written >= sizeof(report)) return false;
    if (!read_fragment(sysmodule, "environment.json", fragment, sizeof(fragment)))
        snprintf(fragment, sizeof(fragment), "null");
    if (!append_text(report, sizeof(report), fragment) ||
        !append_text(report, sizeof(report), ",\"build\":")) return false;
    if (!read_fragment(sysmodule, "build.json", fragment, sizeof(fragment)))
        snprintf(fragment, sizeof(fragment), "null");
    if (!append_text(report, sizeof(report), fragment) ||
        !append_text(report, sizeof(report), "},\"durations\":{\"phase_seconds\":75,"
            "\"restriction_restore_seconds\":15},\"public_parity\":")) return false;
    if (!read_fragment(sysmodule, "lab/public.json", fragment, sizeof(fragment))) snprintf(fragment, sizeof(fragment), "null");
    if (!append_text(report, sizeof(report), fragment) ||
        !append_text(report, sizeof(report), ",\"phases\":[")) return false;
    for (i = 0; i < sizeof(LAB_PHASES) / sizeof(LAB_PHASES[0]); ++i) {
        char relative[96];
        bool phase_present;
        if (i && !append_text(report, sizeof(report), ",")) return false;
        snprintf(relative, sizeof(relative), "lab/phase-%u.json", (unsigned int)i);
        phase_present = read_fragment(sysmodule, relative, fragment, sizeof(fragment));
        if (!phase_present) snprintf(fragment, sizeof(fragment), "null");
        else ++completed_phases;
        if (!append_text(report, sizeof(report), fragment)) return false;
    }
    complete = completed_phases == sizeof(LAB_PHASES) / sizeof(LAB_PHASES[0]) &&
        observation_recorded && state->restored &&
        strcmp(state->restore_verdict, "exact_restore_proved") == 0 &&
        strcmp(state->state, "complete") == 0;
    snprintf(fragment, sizeof(fragment),
        "],\"manual_observation\":%s%s%s,\"restoration\":{\"proved\":%s,\"verdict\":\"%s\"},"
        "\"summary\":{\"automated_phases_completed\":%u,\"manual_observation_recorded\":%s,"
        "\"complete\":%s,\"ipc_callable\":\"see_commands\",\"wire_shape_confirmed\":\"see_commands\","
        "\"product_semantics\":\"evidence_only_until_review\"}}\n",
        state->observation[0] ? "\"" : "", state->observation[0] ? state->observation : "null",
        state->observation[0] ? "\"" : "",
        state->restored ? "true" : "false", state->restore_verdict,
        completed_phases, observation_recorded ? "true" : "false", complete ? "true" : "false");
    if (!append_text(report, sizeof(report), fragment)) return false;
    report_path(sysmodule, state, path, sizeof(path));
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, report);
}

static bool restore_original(PtcSysmodule *sysmodule, LabState *state)
{
    PtcPctlSettingsSnapshot verified;
    PtcErrorCode err;
    bool exact;
    if (!sysmodule->pctl->vtable->restore_settings || !sysmodule->pctl->vtable->snapshot_settings) return false;
    err = sysmodule->pctl->vtable->restore_settings(sysmodule->pctl, &state->original);
    if (err != PTC_ERR_OK) return false;
    memset(&verified, 0, sizeof(verified));
    err = sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &verified);
    exact = err == PTC_ERR_OK && verified.size == state->original.size &&
        verified.timer_enabled == state->original.timer_enabled &&
        memcmp(verified.data, state->original.data, PTC_PCTL_OPAQUE_SETTINGS_SIZE) == 0;
    state->restored = exact;
    snprintf(state->restore_verdict, sizeof(state->restore_verdict), "%s",
        exact ? "exact_restore_proved" : "restore_not_proved");
    return exact;
}

static void enter_restore_required(PtcSysmodule *sysmodule, LabState *state)
{
    char path[320];
    snprintf(state->state, sizeof(state->state), "restore_required");
    state->restored = false;
    snprintf(state->restore_verdict, sizeof(state->restore_verdict), "restore_not_proved");
    snprintf(path, sizeof(path), "%s/flags/disable.flag", sysmodule->app_root);
    (void)sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, "device_lab_restore_not_proved\n");
    (void)save_state(sysmodule, state);
    (void)rebuild_report(sysmodule, state);
}

static bool format_settings_offsets(const uint8_t *original, const uint8_t *active,
    size_t expected_start, size_t expected_end, char *changed, size_t changed_size,
    char *outside, size_t outside_size, unsigned int *changed_count,
    unsigned int *outside_count)
{
    size_t changed_used = 1U;
    size_t outside_used = 1U;
    size_t i;
    changed[0] = '[';
    changed[1] = '\0';
    outside[0] = '[';
    outside[1] = '\0';
    *changed_count = 0;
    *outside_count = 0;
    for (i = 0; i < PTC_PCTL_OPAQUE_SETTINGS_SIZE; ++i) {
        int written;
        if (original[i] == active[i]) continue;
        written = snprintf(changed + changed_used, changed_size - changed_used, "%s%u",
            *changed_count ? "," : "", (unsigned int)i);
        if (written < 0 || (size_t)written >= changed_size - changed_used) return false;
        changed_used += (size_t)written;
        ++*changed_count;
        if (i >= expected_start && i < expected_end) continue;
        written = snprintf(outside + outside_used, outside_size - outside_used, "%s%u",
            *outside_count ? "," : "", (unsigned int)i);
        if (written < 0 || (size_t)written >= outside_size - outside_used) return false;
        outside_used += (size_t)written;
        ++*outside_count;
    }
    if (changed_used + 2U > changed_size || outside_used + 2U > outside_size) return false;
    changed[changed_used++] = ']';
    changed[changed_used] = '\0';
    outside[outside_used++] = ']';
    outside[outside_used] = '\0';
    return true;
}

static bool write_phase(PtcSysmodule *sysmodule, const LabState *state,
    const PtcPctlForensicSample *after, const PtcPctlSuspendEventEvidence *event,
    uint8_t weekday)
{
    char path[320];
    char before_hash[65];
    char after_hash[65];
    char before_hex[PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U + 1U];
    char after_hex[PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U + 1U];
    char original_hex[PTC_PCTL_OPAQUE_SETTINGS_SIZE * 2U + 1U];
    char changed_offsets[320];
    char outside_offsets[320];
    char settings_scope[1200];
    char first_signal[32];
    char verdict[48] = "evidence_recorded";
    char text[4096];
    int64_t remaining_delta = after->remaining_ns - state->before.remaining_ns;
    int64_t spent_delta = after->spent_ns - state->before.spent_ns;
    bool callable = sample_ok(&state->before) && sample_ok(after);
    bool restriction = strcmp(state->active_phase, "restriction_effect") == 0;
    uint8_t target_weekday = state->restriction_weekday >= 0 && state->restriction_weekday < 7
        ? (uint8_t)state->restriction_weekday : weekday;
    size_t expected_start = (7U + (size_t)target_weekday * 4U) * 2U;
    size_t expected_end = expected_start + 8U;
    unsigned int changed_count = 0;
    unsigned int outside_count = 0;
    bool offsets_ok = false;
    if (strcmp(state->active_phase, "home_started") == 0 &&
        ((state->before.remaining_result == 0 && after->remaining_result == 0 && remaining_delta < 0) ||
         (state->before.spent_result == 0 && after->spent_result == 0 && spent_delta > 0))) {
        snprintf(verdict, sizeof(verdict), "unsafe_for_home_start");
    } else if (strcmp(state->active_phase, "home_stopped") == 0 && remaining_delta == 0) {
        snprintf(verdict, sizeof(verdict), "stopped_timer_stable");
    } else if (restriction && after->restricted_result == 0 && after->restricted) {
        snprintf(verdict, sizeof(verdict), "restriction_ipc_observed");
    }
    settings_hash(&state->before, before_hash);
    settings_hash(after, after_hash);
    bytes_hex(state->before.settings, sizeof(state->before.settings), before_hex, sizeof(before_hex));
    bytes_hex(after->settings, sizeof(after->settings), after_hex, sizeof(after_hex));
    bytes_hex(state->original.data, sizeof(state->original.data), original_hex, sizeof(original_hex));
    if (expected_end > sizeof(after->settings)) expected_end = sizeof(after->settings);
    if (restriction && after->settings_result == 0 && expected_start < expected_end) {
        offsets_ok = format_settings_offsets(state->original.data, after->settings,
            expected_start, expected_end, changed_offsets, sizeof(changed_offsets),
            outside_offsets, sizeof(outside_offsets), &changed_count, &outside_count);
    }
    if (restriction) {
        snprintf(settings_scope, sizeof(settings_scope),
            "{\"comparison\":\"session_original_to_restriction_active\",\"target_weekday\":%u,"
            "\"expected_byte_start\":%u,\"expected_byte_end_exclusive\":%u,"
            "\"original_settings_hex\":\"%s\",\"changed_byte_count\":%u,"
            "\"changed_offsets\":%s,\"outside_today_changed_offsets\":%s,"
            "\"unrelated_bytes_unchanged\":%s}",
            (unsigned int)target_weekday, (unsigned int)expected_start, (unsigned int)expected_end,
            original_hex, changed_count, offsets_ok ? changed_offsets : "null",
            offsets_ok ? outside_offsets : "null",
            offsets_ok && outside_count == 0 ? "true" : "false");
    } else {
        snprintf(settings_scope, sizeof(settings_scope),
            "{\"comparison\":\"not_applicable\",\"unrelated_bytes_unchanged\":true}");
    }
    if (event && event->signaled) {
        snprintf(first_signal, sizeof(first_signal), "%llu",
            (unsigned long long)event->first_signaled_monotonic_ns);
    } else {
        snprintf(first_signal, sizeof(first_signal), "null");
    }
    snprintf(text, sizeof(text),
        "{\"phase\":\"%s\",\"started_at\":%lld,\"ended_at\":%lld,"
        "\"before\":{\"monotonic_ns\":%llu,\"1453\":{\"result\":%u,\"value\":%s},"
        "\"1454\":{\"result\":%u,\"nanoseconds\":%lld},\"1455\":{\"result\":%u,\"value\":%s},"
        "\"1952\":{\"result\":%u,\"nanoseconds\":%lld},\"settings_result\":%u,"
        "\"settings_sha256\":\"%s\",\"settings_hex\":\"%s\"},"
        "\"after\":{\"monotonic_ns\":%llu,\"1453\":{\"result\":%u,\"value\":%s},"
        "\"1454\":{\"result\":%u,\"nanoseconds\":%lld},\"1455\":{\"result\":%u,\"value\":%s},"
        "\"1952\":{\"result\":%u,\"nanoseconds\":%lld},\"settings_result\":%u,"
        "\"settings_sha256\":\"%s\",\"settings_hex\":\"%s\"},"
        "\"deltas\":{\"remaining_ns\":%lld,\"spent_ns\":%lld},"
        "\"suspend_event\":{\"known\":%s,\"check_count\":%u,\"signaled\":%s,"
        "\"first_signaled_monotonic_ns\":%s},\"settings_write_scope\":%s,"
        "\"verdicts\":{\"ipc_callable\":%s,\"wire_shape_confirmed\":%s,\"product_semantics\":\"%s\"}}",
        state->active_phase, (long long)state->started_at, (long long)state->deadline,
        (unsigned long long)state->before.monotonic_ns,
        state->before.timer_enabled_result, state->before.timer_enabled ? "true" : "false",
        state->before.remaining_result, (long long)state->before.remaining_ns,
        state->before.restricted_result, state->before.restricted ? "true" : "false",
        state->before.spent_result, (long long)state->before.spent_ns, state->before.settings_result, before_hash, before_hex,
        (unsigned long long)after->monotonic_ns,
        after->timer_enabled_result, after->timer_enabled ? "true" : "false",
        after->remaining_result, (long long)after->remaining_ns,
        after->restricted_result, after->restricted ? "true" : "false",
        after->spent_result, (long long)after->spent_ns, after->settings_result, after_hash, after_hex,
        (long long)remaining_delta, (long long)spent_delta,
        event && event->known ? "true" : "false", event ? event->check_count : 0U,
        event && event->signaled ? "true" : "false", first_signal, settings_scope,
        callable ? "true" : "false",
        callable ? "true" : "false", verdict);
    snprintf(path, sizeof(path), "%s/lab/phase-%d.json", sysmodule->app_root, state->next_phase);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text);
}

static PtcErrorCode start_session(PtcSysmodule *sysmodule, LabState *state)
{
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    PtcPctlPublicParity parity;
    PtcPctlSettingsSnapshot verified;
    PtcErrorCode err;
    bool write_ok;
    bool restored_ok;
    size_t i;
    memset(state, 0, sizeof(*state));
    state->restriction_weekday = -1;
    snprintf(state->run_id, sizeof(state->run_id), "%lld-%s", (long long)now.unix_seconds, sysmodule->boot_id);
    snprintf(state->state, sizeof(state->state), "ready");
    snprintf(state->restore_verdict, sizeof(state->restore_verdict), "pending");
    for (i = 0; i < sizeof(LAB_PHASES) / sizeof(LAB_PHASES[0]); ++i) {
        char old_path[320];
        snprintf(old_path, sizeof(old_path), "%s/lab/phase-%u.json", sysmodule->app_root, (unsigned int)i);
        if (sysmodule->storage->vtable->exists(sysmodule->storage, old_path))
            (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, old_path);
    }
    if (!sysmodule->pctl || !sysmodule->pctl->vtable || !sysmodule->pctl->vtable->snapshot_settings ||
        !sysmodule->pctl->vtable->restore_settings || !sysmodule->pctl->vtable->forensic_sample ||
        !sysmodule->pctl->vtable->public_parity) return PTC_ERR_PCTL_INIT_FAILED;
    err = sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &state->original);
    if (err != PTC_ERR_OK || state->original.size != PTC_PCTL_OPAQUE_SETTINGS_SIZE) return PTC_ERR_PCTL_READ_FAILED;
    err = sysmodule->pctl->vtable->public_parity(sysmodule->pctl, &parity);
    if (err != PTC_ERR_OK) return err;
    write_ok = sysmodule->pctl->vtable->restore_settings(sysmodule->pctl, &state->original) == PTC_ERR_OK;
    memset(&verified, 0, sizeof(verified));
    restored_ok = write_ok && sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &verified) == PTC_ERR_OK &&
        verified.size == state->original.size && verified.timer_enabled == state->original.timer_enabled &&
        memcmp(verified.data, state->original.data, PTC_PCTL_OPAQUE_SETTINGS_SIZE) == 0;
    if (!write_public_evidence(sysmodule, state, &parity, write_ok, restored_ok)) return PTC_ERR_STORAGE_WRITE_FAILED;
    if (!restored_ok) return PTC_ERR_PCTL_RESTORE_FAILED;
    err = sysmodule->pctl->vtable->stop_timer(sysmodule->pctl);
    if (err != PTC_ERR_OK) return err;
    if (!save_state(sysmodule, state) || !rebuild_report(sysmodule, state)) return PTC_ERR_STORAGE_WRITE_FAILED;
    return PTC_ERR_OK;
}

static PtcErrorCode begin_phase(PtcSysmodule *sysmodule, LabState *state, const char *phase)
{
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    PtcErrorCode err;
    if (strcmp(state->state, "ready") != 0 || state->next_phase < 0 || state->next_phase >= 6 ||
        strcmp(phase, LAB_PHASES[state->next_phase]) != 0) return PTC_ERR_BAD_REQUEST;
    if (strcmp(phase, "home_stopped") == 0) {
        err = sysmodule->pctl->vtable->stop_timer(sysmodule->pctl);
        if (err != PTC_ERR_OK) return err;
    } else if (strcmp(phase, "home_started") == 0) {
        err = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
        if (err != PTC_ERR_OK) {
            if (!restore_original(sysmodule, state)) enter_restore_required(sysmodule, state);
            return err;
        }
    } else if (strcmp(phase, "restriction_effect") == 0) {
        PtcPctlTarget target;
        if (!sysmodule->pctl->vtable->arm_suspend_event) return PTC_ERR_PCTL_INIT_FAILED;
        err = sysmodule->pctl->vtable->arm_suspend_event(sysmodule->pctl);
        if (err != PTC_ERR_OK) return err;
        state->event_armed = true;
        target.mode = PTC_PCTL_TARGET_BLOCKED;
        target.minutes = 0;
        target.weekday = ptc_weekday_from_day_index(now.day_index);
        state->restriction_weekday = target.weekday;
        err = sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &target);
        if (err != PTC_ERR_OK) {
            if (!restore_original(sysmodule, state)) enter_restore_required(sysmodule, state);
            return err;
        }
        err = sysmodule->pctl->vtable->start_timer(sysmodule->pctl);
        if (err != PTC_ERR_OK) {
            if (!restore_original(sysmodule, state)) enter_restore_required(sysmodule, state);
            return err;
        }
    }
    memset(&state->before, 0, sizeof(state->before));
    err = sysmodule->pctl->vtable->forensic_sample(sysmodule->pctl, &state->before);
    if (err != PTC_ERR_OK) {
        if (!restore_original(sysmodule, state)) enter_restore_required(sysmodule, state);
        return err;
    }
    snprintf(state->active_phase, sizeof(state->active_phase), "%s", phase);
    snprintf(state->state, sizeof(state->state), "sampling");
    state->started_at = now.unix_seconds;
    state->deadline = now.unix_seconds + (strcmp(phase, "restriction_effect") == 0
        ? LAB_RESTRICTION_SECONDS : LAB_PHASE_SECONDS);
    if (!save_state(sysmodule, state)) {
        if (!restore_original(sysmodule, state)) enter_restore_required(sysmodule, state);
        return PTC_ERR_STORAGE_WRITE_FAILED;
    }
    return PTC_ERR_OK;
}

bool ptc_lab_request_type(PtcRequestType type)
{
    return type >= PTC_REQUEST_LAB_SESSION_START && type <= PTC_REQUEST_LAB_SESSION_RESTORE;
}

bool ptc_lab_process_request(PtcSysmodule *sysmodule, const PtcRequest *request)
{
    LabState state;
    PtcErrorCode err = PTC_ERR_OK;
    bool loaded = load_state(sysmodule, &state);
    char disable_path[320];
    bool writes_disabled;
    if (!loaded) {
        memset(&state, 0, sizeof(state));
        snprintf(state.state, sizeof(state.state), "not_started");
        snprintf(state.restore_verdict, sizeof(state.restore_verdict), "not_started");
    }
    snprintf(disable_path, sizeof(disable_path), "%s/flags/disable.flag", sysmodule->app_root);
    writes_disabled = sysmodule->storage->vtable->exists(sysmodule->storage, disable_path);
    switch (request->type) {
    case PTC_REQUEST_LAB_SESSION_START:
        if (writes_disabled) {
            err = PTC_ERR_DISABLED;
        } else if (loaded && strcmp(state.state, "complete") != 0 && strcmp(state.state, "not_started") != 0) {
            err = PTC_ERR_BAD_REQUEST;
        } else {
            err = start_session(sysmodule, &state);
            if (err != PTC_ERR_OK && state.original.size == PTC_PCTL_OPAQUE_SETTINGS_SIZE) {
                if (!restore_original(sysmodule, &state)) enter_restore_required(sysmodule, &state);
            }
        }
        break;
    case PTC_REQUEST_LAB_PHASE_START:
        if (writes_disabled) err = PTC_ERR_DISABLED;
        else if (!loaded) err = PTC_ERR_BAD_REQUEST;
        else err = begin_phase(sysmodule, &state, request->phase);
        break;
    case PTC_REQUEST_LAB_SESSION_STATUS:
        break;
    case PTC_REQUEST_LAB_OBSERVATION:
        if (!loaded || strcmp(state.state, "awaiting_observation") != 0) err = PTC_ERR_BAD_REQUEST;
        else {
            snprintf(state.observation, sizeof(state.observation), "%s", request->observation);
            snprintf(state.state, sizeof(state.state), "complete");
            state.next_phase = 6;
            state.active_phase[0] = '\0';
            state.deadline = 0;
            if (!save_state(sysmodule, &state) || !rebuild_report(sysmodule, &state)) err = PTC_ERR_STORAGE_WRITE_FAILED;
        }
        break;
    case PTC_REQUEST_LAB_SESSION_RESTORE:
        if (!loaded || state.original.size != PTC_PCTL_OPAQUE_SETTINGS_SIZE) err = PTC_ERR_RECOVERY_UNAVAILABLE;
        else if (!restore_original(sysmodule, &state)) {
            enter_restore_required(sysmodule, &state);
            err = PTC_ERR_PCTL_RESTORE_FAILED;
        } else {
            /* The restriction phase already restores before asking what the
               operator saw. A redundant recovery request must not erase that
               still-required observation or turn an incomplete report final. */
            if (strcmp(state.state, "awaiting_observation") != 0)
                snprintf(state.state, sizeof(state.state), "complete");
            state.active_phase[0] = '\0';
            state.deadline = 0;
            if (sysmodule->storage->vtable->exists(sysmodule->storage, disable_path))
                (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, disable_path);
            if (!save_state(sysmodule, &state) || !rebuild_report(sysmodule, &state)) err = PTC_ERR_STORAGE_WRITE_FAILED;
        }
        break;
    default:
        return false;
    }
    (void)write_result(sysmodule, request, &state, err);
    return true;
}

int ptc_lab_scheduler_tick(PtcSysmodule *sysmodule)
{
    LabState state;
    PtcClockSnapshot now;
    PtcPctlForensicSample after;
    PtcPctlSuspendEventEvidence event;
    bool restriction;
    uint8_t weekday;
    if (!load_state(sysmodule, &state) || strcmp(state.state, "sampling") != 0) return 0;
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    restriction = strcmp(state.active_phase, "restriction_effect") == 0;
    weekday = ptc_weekday_from_day_index(now.day_index);
    memset(&event, 0, sizeof(event));
    if (restriction && sysmodule->pctl->vtable->poll_suspend_event)
        (void)sysmodule->pctl->vtable->poll_suspend_event(sysmodule->pctl, &event);
    if (now.unix_seconds < state.deadline) return 0;
    memset(&after, 0, sizeof(after));
    if (sysmodule->pctl->vtable->forensic_sample(sysmodule->pctl, &after) != PTC_ERR_OK) {
        if (!restore_original(sysmodule, &state)) enter_restore_required(sysmodule, &state);
        else { snprintf(state.state, sizeof(state.state), "error"); (void)save_state(sysmodule, &state); }
        (void)rebuild_report(sysmodule, &state);
        return 1;
    }
    if (!write_phase(sysmodule, &state, &after, restriction ? &event : NULL, weekday)) {
        if (!restore_original(sysmodule, &state)) enter_restore_required(sysmodule, &state);
        else { snprintf(state.state, sizeof(state.state), "error"); (void)save_state(sysmodule, &state); }
        return 1;
    }
    if (strcmp(state.active_phase, "home_started") == 0) (void)sysmodule->pctl->vtable->stop_timer(sysmodule->pctl);
    if (restriction) {
        if (!restore_original(sysmodule, &state)) {
            enter_restore_required(sysmodule, &state);
            return 1;
        }
        snprintf(state.state, sizeof(state.state), "awaiting_observation");
    } else {
        snprintf(state.state, sizeof(state.state), "ready");
    }
    ++state.next_phase;
    state.active_phase[0] = '\0';
    state.deadline = 0;
    (void)save_state(sysmodule, &state);
    (void)rebuild_report(sysmodule, &state);
    return 1;
}

uint32_t ptc_lab_next_wait_ms(PtcSysmodule *sysmodule, uint32_t current_wait_ms)
{
    LabState state;
    PtcClockSnapshot now;
    uint64_t remaining_ms;
    if (!sysmodule || !load_state(sysmodule, &state) || strcmp(state.state, "sampling") != 0) return current_wait_ms;
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    if (state.deadline <= now.unix_seconds) return 1U;
    remaining_ms = (uint64_t)(state.deadline - now.unix_seconds) * 1000ULL;
    if (strcmp(state.active_phase, "restriction_effect") == 0 &&
        remaining_ms > LAB_RESTRICTION_EVENT_POLL_MS) remaining_ms = LAB_RESTRICTION_EVENT_POLL_MS;
    return remaining_ms < current_wait_ms ? (uint32_t)remaining_ms : current_wait_ms;
}

#endif
