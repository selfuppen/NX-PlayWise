#include "lab_selfdrive.h"

#ifdef PLAYWISE_DEVICE_LAB

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lab_session.h"
#include "lab_session_internal.h"
#include "../common/time/ptc_time.h"

/* Generous upper bound: a timer-activation A/B run is ~7 timed phases of up to
   90s each, so 20 minutes leaves slack for recovery without driving forever. */
#define SELFDRIVE_WATCHDOG_SECONDS 1200LL
/* Hard cap on issued steps so a logic error can never loop unboundedly. */
#define SELFDRIVE_MAX_STEPS 64
/* Prompt poll while a step is pending (between phases); kept sub-second so the
   autonomous run does not stall on the default minute-aligned scan cadence. */
#define SELFDRIVE_PENDING_WAIT_MS 200U

typedef struct {
    bool active;
    char mode[32];
    char run_id[80];
    int64_t watchdog;
    int steps;
    char verdict[32];
} SelfdriveMeta;

/* Minimal JSON scalar readers, matching lab_session.c's own tiny parser so the
   persisted self-drive meta stays independent of the lab session format. */
static const char *sd_skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    return p;
}

static const char *sd_find_key(const char *text, const char *key)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(text, pattern);
}

static bool sd_json_string(const char *text, const char *key, char *out, size_t out_size)
{
    const char *pos = sd_find_key(text, key);
    const char *end;
    size_t size;
    if (!pos || !(pos = strchr(pos, ':'))) return false;
    pos = sd_skip_ws(pos + 1);
    if (*pos++ != '\"' || !(end = strchr(pos, '\"'))) return false;
    size = (size_t)(end - pos);
    if (size >= out_size) return false;
    memcpy(out, pos, size);
    out[size] = '\0';
    return true;
}
static bool sd_json_i64(const char *text, const char *key, int64_t *out)
{
    const char *pos = sd_find_key(text, key);
    char *end;
    long long value;
    if (!pos || !(pos = strchr(pos, ':'))) return false;
    pos = sd_skip_ws(pos + 1);
    value = strtoll(pos, &end, 10);
    if (end == pos) return false;
    *out = (int64_t)value;
    return true;
}

static bool sd_json_bool(const char *text, const char *key, bool *out)
{
    const char *pos = sd_find_key(text, key);
    if (!pos || !(pos = strchr(pos, ':'))) return false;
    pos = sd_skip_ws(pos + 1);
    if (strncmp(pos, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(pos, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static void sd_meta_path(PtcSysmodule *sysmodule, char *out, size_t size)
{
    snprintf(out, size, "%s/lab/selfdrive.json", sysmodule->app_root);
}

static bool sd_save_meta(PtcSysmodule *sysmodule, const SelfdriveMeta *meta)
{
    char path[320];
    char text[1024];
    sd_meta_path(sysmodule, path, sizeof(path));
    snprintf(text, sizeof(text),
        "{\"version\":1,\"active\":%s,\"mode\":\"%s\",\"run_id\":\"%s\","
        "\"watchdog\":%lld,\"steps\":%d,\"verdict\":\"%s\"}\n",
        meta->active ? "true" : "false", meta->mode, meta->run_id,
        (long long)meta->watchdog, meta->steps, meta->verdict);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text);
}

static bool sd_load_meta(PtcSysmodule *sysmodule, SelfdriveMeta *meta)
{
    char path[320];
    char text[1024];
    int64_t value;
    memset(meta, 0, sizeof(*meta));
    sd_meta_path(sysmodule, path, sizeof(path));
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) return false;
    (void)sd_json_bool(text, "active", &meta->active);
    (void)sd_json_string(text, "mode", meta->mode, sizeof(meta->mode));
    (void)sd_json_string(text, "run_id", meta->run_id, sizeof(meta->run_id));
    if (sd_json_i64(text, "watchdog", &value)) meta->watchdog = value;
    if (sd_json_i64(text, "steps", &value)) meta->steps = (int)value;
    (void)sd_json_string(text, "verdict", meta->verdict, sizeof(meta->verdict));
    return true;
}
static void sd_make_request(PtcRequest *request, PtcRequestType type, int64_t unix_seconds, int step)
{
    memset(request, 0, sizeof(*request));
    request->type = type;
    snprintf(request->type_text, sizeof(request->type_text), "%s", ptc_request_type_name(type));
    snprintf(request->request_id, sizeof(request->request_id), "selfdrive-%lld-%d",
        (long long)unix_seconds, step);
}

static void sd_issue_session_start(PtcSysmodule *sysmodule, const SelfdriveMeta *meta, int64_t now_s)
{
    PtcRequest request;
    sd_make_request(&request, PTC_REQUEST_LAB_SESSION_START, now_s, meta->steps);
    request.self_driven = true;
    snprintf(request.lab_mode, sizeof(request.lab_mode), "%s",
        meta->mode[0] ? meta->mode : "timer_activation_ab");
    (void)ptc_lab_process_request(sysmodule, &request);
}

static void sd_issue_phase_start(PtcSysmodule *sysmodule, const char *phase, int64_t now_s, int step)
{
    PtcRequest request;
    sd_make_request(&request, PTC_REQUEST_LAB_PHASE_START, now_s, step);
    snprintf(request.phase, sizeof(request.phase), "%s", phase);
    (void)ptc_lab_process_request(sysmodule, &request);
}

static void sd_issue_observation(PtcSysmodule *sysmodule, int64_t now_s, int step)
{
    PtcRequest request;
    sd_make_request(&request, PTC_REQUEST_LAB_OBSERVATION, now_s, step);
    /* The driver cannot watch the screen, so it records that the restriction
       visibility / runtime effect was NOT humanly observed. This is the line
       the project will not cross: never claim a human observation. */
    snprintf(request.observation, sizeof(request.observation), "self_driven_unobserved");
    snprintf(request.runtime_effect, sizeof(request.runtime_effect), "not_observed");
    (void)ptc_lab_process_request(sysmodule, &request);
}

static void sd_issue_restore(PtcSysmodule *sysmodule, int64_t now_s, int step)
{
    PtcRequest request;
    sd_make_request(&request, PTC_REQUEST_LAB_SESSION_RESTORE, now_s, step);
    (void)ptc_lab_process_request(sysmodule, &request);
}

static void sd_write_summary(PtcSysmodule *sysmodule, const SelfdriveMeta *meta,
    const LabState *state, int64_t now_s)
{
    char path[320];
    char text[1024];
    bool restored = state ? state->restored : false;
    const char *restore_verdict = state ? state->restore_verdict : "";
    snprintf(path, sizeof(path), "%s/lab/selfdrive/summary.json", sysmodule->app_root);
    snprintf(text, sizeof(text),
        "{\"version\":1,\"schema_version\":1,\"self_driven\":true,\"advisory\":true,"
        "\"mode\":\"%s\",\"run_id\":\"%s\",\"verdict\":\"%s\",\"steps\":%d,"
        "\"restored\":%s,\"restore_verdict\":\"%s\",\"finished_at\":%lld}\n",
        meta->mode, meta->run_id, meta->verdict, meta->steps,
        restored ? "true" : "false", restore_verdict, (long long)now_s);
    (void)sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text);
}

static int sd_finalize(PtcSysmodule *sysmodule, SelfdriveMeta *meta, const char *verdict,
    const LabState *state, int64_t now_s)
{
    snprintf(meta->verdict, sizeof(meta->verdict), "%s", verdict);
    meta->active = false;
    (void)sd_save_meta(sysmodule, meta);
    sd_write_summary(sysmodule, meta, state, now_s);
    return 1;
}
PtcErrorCode ptc_lab_selfdrive_arm(PtcSysmodule *sysmodule, const PtcRequest *request)
{
    SelfdriveMeta meta;
    LabState state;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    if (sd_load_meta(sysmodule, &meta) && meta.active) return PTC_ERR_BAD_REQUEST;
    /* Refuse while any prior lab session is still open or unrestored. If an
       authoritative campaign is active, the self-driven session_start fails
       cleanly on the next tick (before touching PCTL), so no campaign-aware
       check is needed here. */
    if (lab_load_state(sysmodule, &state) &&
        (strcmp(state.state, "complete") != 0 || !state.restored ||
            strcmp(state.restore_verdict, "exact_restore_proved") != 0))
        return PTC_ERR_BAD_REQUEST;
    memset(&meta, 0, sizeof(meta));
    meta.active = true;
    snprintf(meta.mode, sizeof(meta.mode), "%s",
        request->lab_mode[0] ? request->lab_mode : "timer_activation_ab");
    meta.watchdog = now.unix_seconds + SELFDRIVE_WATCHDOG_SECONDS;
    snprintf(meta.verdict, sizeof(meta.verdict), "armed");
    return sd_save_meta(sysmodule, &meta) ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED;
}

int ptc_lab_selfdrive_tick(PtcSysmodule *sysmodule)
{
    SelfdriveMeta meta;
    LabState state;
    PtcClockSnapshot now;
    int guard;
    bool acted = false;
    if (!sysmodule || !sd_load_meta(sysmodule, &meta) || !meta.active) return 0;
    now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    if (now.unix_seconds > meta.watchdog)
        return sd_finalize(sysmodule, &meta, "timeout", NULL, now.unix_seconds);
    for (guard = 0; guard < 8; ++guard) {
        bool loaded = lab_load_state(sysmodule, &state);
        if (meta.steps >= SELFDRIVE_MAX_STEPS)
            return sd_finalize(sysmodule, &meta, "runaway", loaded ? &state : NULL, now.unix_seconds);
        if (!meta.run_id[0]) {
            sd_issue_session_start(sysmodule, &meta, now.unix_seconds);
            ++meta.steps;
            if (lab_load_state(sysmodule, &state) && strcmp(state.state, "ready") == 0 && state.run_id[0]) {
                snprintf(meta.run_id, sizeof(meta.run_id), "%s", state.run_id);
                (void)sd_save_meta(sysmodule, &meta);
                acted = true;
                continue;
            }
            return sd_finalize(sysmodule, &meta, "session_start_failed",
                loaded ? &state : NULL, now.unix_seconds);
        }
        if (!loaded || strcmp(state.run_id, meta.run_id) != 0)
            return sd_finalize(sysmodule, &meta, "session_lost", NULL, now.unix_seconds);
        if (strcmp(state.state, "sampling") == 0) break;
        if (strcmp(state.state, "ready") == 0) {
            const char *phase = lab_expected_phase(&state);
            if (!phase)
                return sd_finalize(sysmodule, &meta, "no_expected_phase", &state, now.unix_seconds);
            sd_issue_phase_start(sysmodule, phase, now.unix_seconds, meta.steps);
            ++meta.steps;
            (void)sd_save_meta(sysmodule, &meta);
            acted = true;
            continue;
        }
        if (strcmp(state.state, "awaiting_observation") == 0) {
            sd_issue_observation(sysmodule, now.unix_seconds, meta.steps);
            ++meta.steps;
            (void)sd_save_meta(sysmodule, &meta);
            acted = true;
            continue;
        }
        if (strcmp(state.state, "complete") == 0) {
            if (!state.restored) {
                sd_issue_restore(sysmodule, now.unix_seconds, meta.steps);
                ++meta.steps;
                (void)sd_save_meta(sysmodule, &meta);
                acted = true;
                continue;
            }
            return sd_finalize(sysmodule, &meta, "completed", &state, now.unix_seconds);
        }
        /* restore_required / error / not_started / anything unexpected */
        return sd_finalize(sysmodule, &meta,
            state.state[0] ? state.state : "unknown", &state, now.unix_seconds);
    }
    return acted ? 1 : 0;
}

uint32_t ptc_lab_selfdrive_next_wait_ms(PtcSysmodule *sysmodule, uint32_t current_wait_ms)
{
    SelfdriveMeta meta;
    LabState state;
    if (!sysmodule || !sd_load_meta(sysmodule, &meta) || !meta.active) return current_wait_ms;
    if (!meta.run_id[0])
        return current_wait_ms < SELFDRIVE_PENDING_WAIT_MS ? current_wait_ms : SELFDRIVE_PENDING_WAIT_MS;
    if (lab_load_state(sysmodule, &state) && strcmp(state.state, "sampling") == 0)
        return current_wait_ms;
    return current_wait_ms < SELFDRIVE_PENDING_WAIT_MS ? current_wait_ms : SELFDRIVE_PENDING_WAIT_MS;
}

#endif
