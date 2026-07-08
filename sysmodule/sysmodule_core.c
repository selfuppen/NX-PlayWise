#include "sysmodule_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/policy/control_policy.h"
#include "../common/protocol/result_builder.h"
#include "../common/token/token_v1.h"

typedef struct {
    char device_id[80];
    char grant_secret[128];
    uint16_t max_add_minutes;
    PtcControlMode mode;
    bool allow_unlimited_to_limited;
} PtcRuntimeConfig;

static void join_path(char *out, size_t out_size, const char *a, const char *b)
{
    snprintf(out, out_size, "%s/%s", a, b);
}

static bool json_string(const char *text, const char *key, char *out, size_t out_size)
{
    char pattern[64];
    const char *pos;
    const char *start;
    const char *end;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(text, pattern);
    if (!pos) {
        return false;
    }
    pos = strchr(pos + strlen(pattern), ':');
    if (!pos) {
        return false;
    }
    start = strchr(pos, '"');
    if (!start) {
        return false;
    }
    ++start;
    end = strchr(start, '"');
    if (!end || (size_t)(end - start) >= out_size) {
        return false;
    }
    memcpy(out, start, (size_t)(end - start));
    out[end - start] = '\0';
    return true;
}

static bool json_u16(const char *text, const char *key, uint16_t *out)
{
    char pattern[64];
    const char *pos;
    long value;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(text, pattern);
    if (!pos) {
        return false;
    }
    pos = strchr(pos + strlen(pattern), ':');
    if (!pos) {
        return false;
    }
    value = strtol(pos + 1, NULL, 10);
    if (value < 0 || value > 65535) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

static bool json_bool_value(const char *text, const char *key, bool *out)
{
    char pattern[64];
    const char *pos;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(text, pattern);
    if (!pos) {
        return false;
    }
    pos = strchr(pos + strlen(pattern), ':');
    if (!pos) {
        return false;
    }
    while (*pos == ':' || *pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') {
        ++pos;
    }
    if (strncmp(pos, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool load_config(PtcSysmodule *sysmodule, PtcRuntimeConfig *config)
{
    char path[320];
    char text[4096];
    char mode[24];
    join_path(path, sizeof(path), sysmodule->app_root, "config.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        return false;
    }
    if (!json_string(text, "device_id", config->device_id, sizeof(config->device_id)) ||
        !json_string(text, "grant_secret", config->grant_secret, sizeof(config->grant_secret))) {
        return false;
    }
    if (!json_u16(text, "max_add_minutes", &config->max_add_minutes)) {
        config->max_add_minutes = 120;
    }
    if (!json_string(text, "control_mode", mode, sizeof(mode))) {
        snprintf(mode, sizeof(mode), "observe");
    }
    config->mode = ptc_control_mode_from_string(mode);
    if (!json_bool_value(text, "allow_unlimited_to_limited", &config->allow_unlimited_to_limited)) {
        config->allow_unlimited_to_limited = false;
    }
    return true;
}

static PtcCapabilities load_capabilities(PtcSysmodule *sysmodule)
{
    PtcCapabilities caps;
    char path[320];
    char text[1024];
    caps.raw_block_verified = false;
    caps.suspend_verified = false;
    join_path(path, sizeof(path), sysmodule->app_root, "capabilities.json");
    if (sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        (void)json_bool_value(text, "raw_block_verified", &caps.raw_block_verified);
        (void)json_bool_value(text, "suspend_verified", &caps.suspend_verified);
    }
    return caps;
}

static bool nonce_used(uint16_t day_index, uint32_t nonce, void *ctx)
{
    PtcSysmodule *sysmodule = (PtcSysmodule *)ctx;
    char path[320];
    char text[4096];
    char needle[96];
    join_path(path, sizeof(path), sysmodule->app_root, "ledger/used_nonces.jsonl");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, text, sizeof(text))) {
        return false;
    }
    snprintf(needle, sizeof(needle), "\"day_index\":%u,\"nonce\":%lu", day_index, (unsigned long)nonce);
    return strstr(text, needle) != NULL;
}

static bool consume_nonce(PtcSysmodule *sysmodule, uint16_t day_index, uint32_t nonce)
{
    char path[320];
    char line[128];
    join_path(path, sizeof(path), sysmodule->app_root, "ledger/used_nonces.jsonl");
    snprintf(line, sizeof(line), "{\"day_index\":%u,\"nonce\":%lu}", day_index, (unsigned long)nonce);
    return sysmodule->storage->vtable->append_line(sysmodule->storage, path, line);
}

static void state_from_pctl(PtcResultState *state, uint16_t day_index, const PtcPctlStatus *status)
{
    ptc_result_state_default(state, day_index);
    state->limited_today = status->limited_today ? 1 : 0;
    state->blocked_today = status->blocked_today ? 1 : 0;
    state->unrestricted_today = status->unrestricted_today ? 1 : 0;
    state->remaining_available = status->remaining_available;
    state->remaining_minutes = status->remaining_available ? status->remaining_minutes : -1;
    state->play_timer_enabled = status->play_timer_enabled ? 1 : 0;
    state->restricted_now = status->restricted_now ? 1 : 0;
}

static bool write_result(PtcSysmodule *sysmodule, const char *request_id, const char *json)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/results/%s.json", sysmodule->app_root, request_id);
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, json);
}

static PtcErrorCode backup_before_write(PtcSysmodule *sysmodule)
{
    char path[320];
    PtcPctlBackup backup;
    PtcErrorCode err = sysmodule->pctl->vtable->backup(sysmodule->pctl, &backup);
    if (err != PTC_ERR_OK) {
        return err;
    }
    snprintf(path, sizeof(path), "%s/backups/last_pctl_backup.txt", sysmodule->app_root);
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, backup.text)) {
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    return PTC_ERR_OK;
}

static PtcErrorCode apply_grant(PtcSysmodule *sysmodule, uint16_t minutes)
{
    PtcPctlTarget target;
    PtcErrorCode err = backup_before_write(sysmodule);
    if (err != PTC_ERR_OK) {
        return err;
    }
    target.mode = PTC_PCTL_TARGET_LIMIT;
    target.minutes = minutes;
    return sysmodule->pctl->vtable->apply_target(sysmodule->pctl, &target);
}

static void finish_with_error(
    PtcSysmodule *sysmodule,
    const char *request_id,
    const char *request_type,
    const char *mode,
    bool dry_run,
    PtcErrorCode error,
    uint16_t day_index)
{
    char json[2048];
    PtcResultState state;
    ptc_result_state_default(&state, day_index);
    (void)ptc_result_error_json(json, sizeof(json), request_id, request_type, mode, dry_run, error, &state, sysmodule->time_provider->vtable->now(sysmodule->time_provider).unix_seconds);
    (void)write_result(sysmodule, request_id, json);
}

static void process_request_text(PtcSysmodule *sysmodule, const char *request_text)
{
    char request_id[80] = "unknown";
    char request_type[40] = "unknown";
    char code[80] = "";
    PtcRuntimeConfig config;
    PtcCapabilities caps;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    PtcPctlStatus pctl_status;
    PtcResultState state;
    bool disable_flag;
    char disable_path[320];
    PtcPolicyDecision decision;
    PtcErrorCode err;

    (void)json_string(request_text, "request_id", request_id, sizeof(request_id));
    (void)json_string(request_text, "type", request_type, sizeof(request_type));

    if (!load_config(sysmodule, &config)) {
        finish_with_error(sysmodule, request_id, request_type, "observe", true, PTC_ERR_CONFIG_INVALID, now.day_index);
        return;
    }

    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    disable_flag = sysmodule->storage->vtable->exists(sysmodule->storage, disable_path);
    caps = load_capabilities(sysmodule);

    if (strcmp(request_type, "status") == 0) {
        char json[2048];
        decision = ptc_policy_decide(config.mode, disable_flag, PTC_OPERATION_STATUS, &caps, false, config.allow_unlimited_to_limited);
        if (decision.error != PTC_ERR_OK) {
            finish_with_error(sysmodule, request_id, request_type, ptc_control_mode_name(config.mode), true, decision.error, now.day_index);
            return;
        }
        err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, &pctl_status);
        if (err != PTC_ERR_OK) {
            finish_with_error(sysmodule, request_id, request_type, ptc_control_mode_name(config.mode), decision.dry_run, err, now.day_index);
            return;
        }
        state_from_pctl(&state, now.day_index, &pctl_status);
        (void)ptc_result_ok_json(json, sizeof(json), request_id, request_type, ptc_control_mode_name(config.mode), decision.dry_run, &state, now.unix_seconds);
        (void)write_result(sysmodule, request_id, json);
        return;
    }

    if (strcmp(request_type, "offline_code") == 0) {
        PtcTokenPayload token;
        char json[2048];
        (void)json_string(request_text, "code", code, sizeof(code));
        err = ptc_token_verify(code, config.device_id, config.grant_secret, now.day_index, config.max_add_minutes, nonce_used, sysmodule, &token);
        if (err != PTC_ERR_OK) {
            finish_with_error(sysmodule, request_id, request_type, ptc_control_mode_name(config.mode), true, err, now.day_index);
            return;
        }
        (void)sysmodule->pctl->vtable->read_status(sysmodule->pctl, &pctl_status);
        decision = ptc_policy_decide(config.mode, disable_flag, PTC_OPERATION_GRANT_MINUTES, &caps, pctl_status.unrestricted_today, config.allow_unlimited_to_limited);
        if (decision.error != PTC_ERR_OK) {
            finish_with_error(sysmodule, request_id, request_type, ptc_control_mode_name(config.mode), decision.dry_run, decision.error, now.day_index);
            return;
        }
        if (decision.may_write_pctl) {
            err = apply_grant(sysmodule, token.minutes);
            if (err != PTC_ERR_OK) {
                finish_with_error(sysmodule, request_id, request_type, ptc_control_mode_name(config.mode), false, err, now.day_index);
                return;
            }
        }
        (void)sysmodule->pctl->vtable->read_status(sysmodule->pctl, &pctl_status);
        state_from_pctl(&state, now.day_index, &pctl_status);
        (void)ptc_result_ok_json(json, sizeof(json), request_id, request_type, ptc_control_mode_name(config.mode), decision.dry_run, &state, now.unix_seconds);
        if (write_result(sysmodule, request_id, json) && decision.consume_nonce_after_success) {
            (void)consume_nonce(sysmodule, token.day_index_since_2020, token.nonce);
        }
        return;
    }

    finish_with_error(sysmodule, request_id, request_type, ptc_control_mode_name(config.mode), true, PTC_ERR_UNKNOWN_REQUEST_TYPE, now.day_index);
}

void ptc_sysmodule_init(
    PtcSysmodule *sysmodule,
    const char *app_root,
    PtcStorage *storage,
    PtcPctl *pctl,
    PtcTimeProvider *time_provider)
{
    snprintf(sysmodule->app_root, sizeof(sysmodule->app_root), "%s", app_root);
    sysmodule->storage = storage;
    sysmodule->pctl = pctl;
    sysmodule->time_provider = time_provider;
}

int ptc_sysmodule_recover_processing(PtcSysmodule *sysmodule)
{
    char dir[320];
    char names[32][128];
    size_t count = 0;
    size_t i;
    int recovered = 0;
    snprintf(dir, sizeof(dir), "%s/inbox/processing", sysmodule->app_root);
    if (!sysmodule->storage->vtable->list_json(sysmodule->storage, dir, names, 32, &count)) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        char from[320];
        char to[320];
        snprintf(from, sizeof(from), "%s/inbox/processing/%s", sysmodule->app_root, names[i]);
        snprintf(to, sizeof(to), "%s/inbox/pending/%s", sysmodule->app_root, names[i]);
        if (sysmodule->storage->vtable->rename_path(sysmodule->storage, from, to)) {
            ++recovered;
        }
    }
    return recovered;
}

int ptc_sysmodule_process_all(PtcSysmodule *sysmodule)
{
    char dir[320];
    char names[32][128];
    size_t count = 0;
    size_t i;
    int processed = 0;
    snprintf(dir, sizeof(dir), "%s/inbox/pending", sysmodule->app_root);
    if (!sysmodule->storage->vtable->list_json(sysmodule->storage, dir, names, 32, &count)) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        char pending[320];
        char processing[320];
        char done[320];
        char text[4096];
        snprintf(pending, sizeof(pending), "%s/inbox/pending/%s", sysmodule->app_root, names[i]);
        snprintf(processing, sizeof(processing), "%s/inbox/processing/%s", sysmodule->app_root, names[i]);
        snprintf(done, sizeof(done), "%s/inbox/done/%s", sysmodule->app_root, names[i]);
        if (!sysmodule->storage->vtable->rename_path(sysmodule->storage, pending, processing)) {
            continue;
        }
        if (sysmodule->storage->vtable->read_text(sysmodule->storage, processing, text, sizeof(text))) {
            process_request_text(sysmodule, text);
        }
        (void)sysmodule->storage->vtable->rename_path(sysmodule->storage, processing, done);
        ++processed;
    }
    return processed;
}
