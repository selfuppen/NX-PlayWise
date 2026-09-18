#include "sysmodule_internal.h"

static bool process_clear_redemption_history(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now)
{
    char path[320];
    char previous[PTC_REDEMPTION_HISTORY_FILE_SIZE];
    bool existed;
    bool restored;
    join_path(path, sizeof(path), sysmodule->app_root, "ledger/redemption-history.jsonl");
    existed = sysmodule->storage->vtable->exists(sysmodule->storage, path);
    if (existed && !sysmodule->storage->vtable->read_text(
            sysmodule->storage, path, previous, sizeof(previous))) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    if (!clear_redemption_history(sysmodule)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "redemption_history_cleared");
    if (write_current_status_result(
            sysmodule, request, "release", false, now, false)) {
        return true;
    }
    restored = existed
        ? sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, previous)
        : (!sysmodule->storage->vtable->exists(sysmodule->storage, path) ||
           sysmodule->storage->vtable->remove_path(sysmodule->storage, path));
    append_event(sysmodule, request,
        restored ? "state_rollback_ok" : "state_rollback_failed",
        restored ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED,
        "redemption_history_clear");
    return false;
}

static bool process_clear_activity_history(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now)
{
    char path[320];
    char previous[PTC_ACTIVITY_HISTORY_FILE_SIZE];
    bool existed;
    bool restored;
    join_path(path, sizeof(path), sysmodule->app_root, "activity/history.jsonl");
    existed = sysmodule->storage->vtable->exists(sysmodule->storage, path);
    if (existed && !sysmodule->storage->vtable->read_text(
            sysmodule->storage, path, previous, sizeof(previous))) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_READ_FAILED, now.day_index);
    }
    if (!clear_activity_history(sysmodule)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "activity_history_cleared");
    if (write_current_status_result(
            sysmodule, request, "release", false, now, false)) return true;
    restored = existed
        ? sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, previous)
        : (!sysmodule->storage->vtable->exists(sysmodule->storage, path) ||
           sysmodule->storage->vtable->remove_path(sysmodule->storage, path));
    append_event(sysmodule, request, restored ? "state_rollback_ok" : "state_rollback_failed",
        restored ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED, "activity_history_clear");
    return false;
}

static bool process_claim_daily_buffer(PtcSysmodule *sysmodule, const PtcRequest *request,
    bool disable_flag, PtcClockSnapshot now)
{
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcPctlStatus pctl_status;
    PtcPctlStatus observed_status;
    PtcErrorCode err;
    uint16_t grant;
    uint16_t played;
    uint16_t base;
    uint16_t target;
    uint16_t effective;
    if (disable_flag) return finish_with_error(sysmodule, request, "release",
        true, PTC_ERR_DISABLED, now.day_index);
    if (bedtime_blocks_grants(sysmodule, now)) return finish_with_error(
        sysmodule, request, "release", true,
        PTC_ERR_BEDTIME_ACTIVE, now.day_index);
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &runtime_state)) {
        return finish_with_error(sysmodule, request, "release", true,
            PTC_ERR_RULES_INVALID, now.day_index);
    }
    grant = rules.autonomy_policy.daily_buffer_minutes;
    if (grant == 0u) return finish_with_error(sysmodule, request, "release",
        true, PTC_ERR_AUTONOMY_DISABLED, now.day_index);
    if (runtime_state.buffer_claimed && runtime_state.buffer_claim_day_index == now.day_index) {
        return finish_with_error(sysmodule, request, "release",
            true, PTC_ERR_DAILY_BUFFER_ALREADY_CLAIMED, now.day_index);
    }
    err = sysmodule->pctl->vtable->read_status(
        sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    if (err != PTC_ERR_OK) return finish_with_error(sysmodule, request,
        "release", true, err, now.day_index);
    if (!pctl_status.limited_today || pctl_status.unrestricted_today) {
        return finish_with_error(sysmodule, request, "release",
            true, PTC_ERR_DAILY_BUFFER_LIMITED_ONLY, now.day_index);
    }
    if (!recovery_begin(sysmodule, request, now)) return finish_with_error(sysmodule, request,
        "release", false, PTC_ERR_PCTL_BACKUP_FAILED, now.day_index);
    played = ptc_pctl_played_minutes(&pctl_status);
    {
        PtcDayRule active = ptc_rules_today_rule(&rules, now.day_index,
            ptc_weekday_from_day_index(now.day_index));
        base = active.mode == PTC_RULE_MODE_LIMIT ? active.minutes : 0u;
        if (played > base) base = played;
    }
    target = accumulate_today_limit(&rules, now.day_index,
        ptc_weekday_from_day_index(now.day_index), grant, played);
    effective = target >= base ? (uint16_t)(target - base) : 0u;
    err = apply_target(sysmodule, request, now, "release",
        PTC_PCTL_TARGET_LIMIT, target);
    if (err == PTC_ERR_OK) err = observe_target_with_optional_activation(sysmodule, request, now,
        "release", PTC_PCTL_TARGET_LIMIT, target,
        "daily_buffer", &observed_status);
    if (err != PTC_ERR_OK || !save_rules(sysmodule, &rules)) {
        if (err == PTC_ERR_OK) err = PTC_ERR_STORAGE_WRITE_FAILED;
        if (!recovery_rollback(sysmodule)) {
            write_disable_flag(sysmodule, "transaction_restore_failed\n");
            err = PTC_ERR_RECOVERY_FAILED;
        }
        return finish_with_error(sysmodule, request, "release", false,
            err, now.day_index);
    }
    runtime_state.buffer_claimed = true;
    runtime_state.buffer_claim_day_index = now.day_index;
    runtime_state.buffer_claimed_minutes = effective;
    if (runtime_state.summary_day_index != now.day_index) {
        runtime_state.summary_day_index = now.day_index;
        runtime_state.summary_grant_minutes = 0u;
    }
    runtime_state.summary_grant_minutes = (uint16_t)(
        runtime_state.summary_grant_minutes + effective > 1440u ? 1440u :
        runtime_state.summary_grant_minutes + effective);
    if (!save_state(sysmodule, &runtime_state, now.unix_seconds) ||
        !record_activity(sysmodule, request, now, grant, effective)) {
        err = PTC_ERR_STORAGE_WRITE_FAILED;
        if (!recovery_rollback(sysmodule)) {
            write_disable_flag(sysmodule, "transaction_restore_failed\n");
            err = PTC_ERR_RECOVERY_FAILED;
        }
        return finish_with_error(sysmodule, request, "release", false,
            err, now.day_index);
    }
    append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "daily_buffer");
    if (write_current_status_result(sysmodule, request, "release",
            false, now, true)) {
        recovery_clear(sysmodule);
        return true;
    }
    if (!recovery_rollback(sysmodule)) write_disable_flag(sysmodule, "transaction_restore_failed\n");
    return false;
}

static bool code_is_v2_candidate(const char *code)
{
    size_t length;
    size_t index;
    bool all_digits = true;
    if (!code) return false;
    length = strlen(code);
    if (strchr(code, '-') != NULL) return false;
    for (index = 0; index < length; ++index) {
        if (code[index] < '0' || code[index] > '9') all_digits = false;
    }
    /* Exactly eight characters is an intended short-code entry even when a
       non-digit typo makes it malformed. Other all-digit lengths are also
       treated as malformed v2, except 16 symbols which is a valid v1 shape. */
    return length == PTC_TOKEN_V2_TEXT_LEN || (length != 0u && length != PTC_TOKEN_SYMBOLS && all_digits);
}

typedef struct {
    uint16_t day_index;
    uint32_t nonce;
    uint16_t minutes;
    unsigned int version;
    bool is_v2;
} PtcVerifiedOfflineCode;

static PtcErrorCode verify_offline_code(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcRuntimeConfig *config,
    PtcClockSnapshot now,
    PtcRuntimeState *runtime_state,
    PtcVerifiedOfflineCode *verified)
{
    PtcTokenPayload token_v1;
    PtcTokenV2Payload token_v2;
    PtcErrorCode err;
    bool is_v2;
    if (!sysmodule || !request || !config || !runtime_state || !verified) return PTC_ERR_BAD_REQUEST;
    memset(verified, 0, sizeof(*verified));
    verified->day_index = now.day_index;
    verified->version = 1u;
    is_v2 = code_is_v2_candidate(request->code);
    verified->is_v2 = is_v2;
    if (is_v2) {
        if (!load_state(sysmodule, runtime_state)) return PTC_ERR_STORAGE_READ_FAILED;
        if (runtime_state->v2_cooldown_until > now.unix_seconds) return PTC_ERR_CODE_COOLDOWN;
        if (runtime_state->v2_cooldown_until != 0) {
            runtime_state->v2_failed_attempts = 0;
            runtime_state->v2_cooldown_until = 0;
        }
        err = ptc_token_v2_verify(request->code, config->device_id, config->grant_secret, now.day_index,
            config->max_add_minutes, nonce_used_v2, sysmodule, &token_v2);
        if (err == PTC_ERR_BAD_SIGNATURE) {
            sysmodule->config_cache_valid = false;
            if (load_config(sysmodule, config)) {
                err = ptc_token_v2_verify(request->code, config->device_id, config->grant_secret, now.day_index,
                    config->max_add_minutes, nonce_used_v2, sysmodule, &token_v2);
            }
        }
        if (err == PTC_ERR_BAD_CODE || err == PTC_ERR_BAD_SIGNATURE) {
            if (runtime_state->v2_failed_attempts < PTC_V2_FAILURE_LIMIT) ++runtime_state->v2_failed_attempts;
            if (runtime_state->v2_failed_attempts >= PTC_V2_FAILURE_LIMIT) {
                runtime_state->v2_cooldown_until = now.unix_seconds + PTC_V2_COOLDOWN_SECONDS;
            }
            if (!save_state(sysmodule, runtime_state, now.unix_seconds)) return PTC_ERR_STORAGE_WRITE_FAILED;
        }
        if (err == PTC_ERR_OK) {
            verified->minutes = token_v2.minutes;
            verified->nonce = token_v2.nonce;
            verified->version = 2u;
        }
        return err;
    }
    err = ptc_token_verify(request->code, config->device_id, config->grant_secret, now.day_index,
        config->max_add_minutes, nonce_used_v1, sysmodule, &token_v1);
    if (err == PTC_ERR_BAD_SIGNATURE) {
        sysmodule->config_cache_valid = false;
        if (load_config(sysmodule, config)) {
            err = ptc_token_verify(request->code, config->device_id, config->grant_secret, now.day_index,
                config->max_add_minutes, nonce_used_v1, sysmodule, &token_v1);
        }
    }
    if (err == PTC_ERR_OK) {
        verified->day_index = token_v1.day_index_since_2020;
        verified->minutes = token_v1.minutes;
        verified->nonce = token_v1.nonce;
    }
    return err;
}

static bool process_preview_offline_code(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const PtcRuntimeConfig *config,
    bool disable_flag,
    PtcClockSnapshot now)
{
    PtcRuntimeConfig active_config = *config;
    PtcRuntimeState runtime_state;
    PtcVerifiedOfflineCode verified;
    PtcPctlStatus pctl_status;
    PtcRules rules;
    PtcRules preview_rules;
    PtcResultState state;
    PtcOfflineCodePreview preview;
    PtcErrorCode err;
    int64_t played_minutes;
    uint16_t played_for_apply;
    uint16_t base_minutes;
    uint16_t target_minutes;
    char json[2304];

    if (bedtime_blocks_grants(sysmodule, now)) return finish_with_error(
        sysmodule, request, "release", true,
        PTC_ERR_BEDTIME_ACTIVE, now.day_index);

    if (disable_flag) {
        return finish_with_error(sysmodule, request, "release", true,
            PTC_ERR_DISABLED, now.day_index);
    }
    err = verify_offline_code(sysmodule, request, &active_config, now, &runtime_state, &verified);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", true,
            err, now.day_index);
    }
    err = sysmodule->pctl->vtable->read_status(
        sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", true,
            err, now.day_index);
    }
    if (!load_rules(sysmodule, &rules)) {
        return finish_with_error(sysmodule, request, "release", true,
            PTC_ERR_RULES_INVALID, now.day_index);
    }
    preview_rules = rules;
    played_for_apply = ptc_pctl_played_minutes(&pctl_status);
    {
        PtcDayRule active = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
        base_minutes = active.mode == PTC_RULE_MODE_LIMIT ? active.minutes : 0u;
        if (played_for_apply > base_minutes) base_minutes = played_for_apply;
    }
    target_minutes = accumulate_today_limit(&preview_rules, now.day_index,
        ptc_weekday_from_day_index(now.day_index), verified.minutes, played_for_apply);
    memset(&preview, 0, sizeof(preview));
    preview.grant_minutes = verified.minutes;
    preview.effective_add_minutes = target_minutes >= base_minutes ? (uint16_t)(target_minutes - base_minutes) : 0u;
    preview.capped = preview.effective_add_minutes < verified.minutes;
    preview.converts_unlimited_to_limited = pctl_status.unrestricted_today;
    played_minutes = result_played_minutes(&pctl_status);
    preview.remaining_after_available = played_minutes >= 0;
    preview.remaining_after_minutes = preview.remaining_after_available
        ? ((int64_t)target_minutes > played_minutes ? (int64_t)target_minutes - played_minutes : 0)
        : -1;
    result_state_from_pctl(&state, now.day_index, &pctl_status);
    fill_extended_result_state(sysmodule, &state, &rules, &runtime_state, &pctl_status, now);
    if (ptc_result_preview_ok_json(json, sizeof(json), request->request_id, request->type_text,
            &state, &preview, now.unix_seconds) != 0) {
        append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "preview_json");
        return false;
    }
    if (!write_result(sysmodule, request->request_id, json)) {
        append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "preview");
        return false;
    }
    append_event(sysmodule, request, "result_ok", PTC_ERR_OK, "preview_offline_code");
    return true;
}

static bool process_offline_code(PtcSysmodule *sysmodule, const PtcRequest *request, const PtcRuntimeConfig *config, bool disable_flag, PtcClockSnapshot now)
{
    PtcPctlStatus pctl_status;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcResultState state;
    char json[2048];
    PtcErrorCode err;
    PtcRuntimeConfig active_config;
    PtcVerifiedOfflineCode verified;
    uint16_t effective_add_minutes = 0;

    if (config) {
        active_config = *config;
    } else {
        memset(&active_config, 0, sizeof(active_config));
    }

    if (bedtime_blocks_grants(sysmodule, now)) return finish_with_error(
        sysmodule, request, "release", true,
        PTC_ERR_BEDTIME_ACTIVE, now.day_index);

    if (disable_flag) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_DISABLED, now.day_index);
    }
    err = verify_offline_code(sysmodule, request, &active_config, now, &runtime_state, &verified);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", true, err, now.day_index);
    }
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", true, err, now.day_index);
    }
    {
        uint16_t new_minutes;
        uint16_t played_minutes = ptc_pctl_played_minutes(&pctl_status);
        uint16_t base_minutes;
        PtcDayRule active_rule;
        (void)load_rules(sysmodule, &rules);
        active_rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
        base_minutes = active_rule.mode == PTC_RULE_MODE_LIMIT ? active_rule.minutes : 0u;
        if (played_minutes > base_minutes) base_minutes = played_minutes;
        /* Stack the granted minutes onto today's existing limit or played time rather than
           overwriting it, matching the add_today_minutes token action. */
        new_minutes = accumulate_today_limit(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index), verified.minutes, played_minutes);
        effective_add_minutes = new_minutes >= base_minutes ? (uint16_t)(new_minutes - base_minutes) : 0u;
        /* Apply to PCTL first (idempotent absolute write); persist the override
           only after it succeeds. On failure the nonce is not consumed and the
           same code may be re-entered, so persisting first would double-count. */
        err = apply_target(sysmodule, request, now, "release", PTC_PCTL_TARGET_LIMIT, new_minutes);
        if (err != PTC_ERR_OK) {
            return finish_with_error(sysmodule, request, "release", false, err, now.day_index);
        }
        err = observe_target_with_optional_activation(sysmodule, request, now,
            "release", PTC_PCTL_TARGET_LIMIT, new_minutes,
            "offline_code", &pctl_status);
        if (err != PTC_ERR_OK) {
            if (!recovery_rollback(sysmodule)) {
                write_disable_flag(sysmodule, "transaction_restore_failed\n");
                err = PTC_ERR_RECOVERY_FAILED;
            }
            return finish_with_error(sysmodule, request, "release", false, err, now.day_index);
        }
        if (!save_rules(sysmodule, &rules)) {
            append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "rules");
            err = PTC_ERR_STORAGE_WRITE_FAILED;
            if (!recovery_rollback(sysmodule)) {
                write_disable_flag(sysmodule, "transaction_restore_failed\n");
                err = PTC_ERR_RECOVERY_FAILED;
            }
            return finish_with_error(sysmodule, request, "release", false, err, now.day_index);
        }
        append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "offline_code");
    }
    (void)load_rules(sysmodule, &rules);
    (void)load_state(sysmodule, &runtime_state);
    (void)sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    result_state_from_pctl(&state, now.day_index, &pctl_status);
    fill_extended_result_state(sysmodule, &state, &rules, &runtime_state, &pctl_status, now);
    (void)ptc_result_ok_json(json, sizeof(json), request->request_id, request->type_text, "release", false, &state, now.unix_seconds);
    if (write_result(sysmodule, request->request_id, json)) {
        if (verified.is_v2 &&
            (runtime_state.v2_failed_attempts != 0 || runtime_state.v2_cooldown_until != 0)) {
            bool cleared;
            runtime_state.v2_failed_attempts = 0;
            runtime_state.v2_cooldown_until = 0;
            cleared = save_state(sysmodule, &runtime_state, now.unix_seconds);
            append_event(sysmodule, request,
                cleared ? "v2_failures_cleared" : "result_write_failed",
                cleared ? PTC_ERR_OK : PTC_ERR_STORAGE_WRITE_FAILED,
                "v2_cooldown");
        }
        {
            PtcRedemptionHistoryRecord history_record;
            history_record.redeemed_at = now.unix_seconds;
            history_record.day_index = verified.day_index;
            history_record.token_version = verified.version;
            history_record.grant_minutes = verified.minutes;
            history_record.effective_add_minutes = effective_add_minutes;
            history_record.remaining_after_available = state.remaining_available;
            history_record.remaining_after_minutes = state.remaining_available ? state.remaining_minutes : -1;
            if (!save_redemption_history(sysmodule, &history_record)) {
                char result_path[320];
                snprintf(result_path, sizeof(result_path), "%s/results/%s.json", sysmodule->app_root, request->request_id);
                (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, result_path);
                append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED,
                    "redemption_history");
                err = PTC_ERR_STORAGE_WRITE_FAILED;
                if (!recovery_rollback(sysmodule)) {
                    write_disable_flag(sysmodule, "transaction_restore_failed\n");
                    err = PTC_ERR_RECOVERY_FAILED;
                }
                return finish_with_error(sysmodule, request, "release", false,
                    err, now.day_index);
            }
        }
        {
            if (runtime_state.summary_day_index != now.day_index) {
                runtime_state.summary_day_index = now.day_index;
                runtime_state.summary_grant_minutes = 0u;
            }
            runtime_state.summary_grant_minutes = (uint16_t)(
                runtime_state.summary_grant_minutes + effective_add_minutes > 1440u ? 1440u :
                runtime_state.summary_grant_minutes + effective_add_minutes);
            if (!save_state(sysmodule, &runtime_state, now.unix_seconds) ||
                !record_activity(sysmodule, request, now, verified.minutes, effective_add_minutes)) {
                char result_path[320];
                snprintf(result_path, sizeof(result_path), "%s/results/%s.json",
                    sysmodule->app_root, request->request_id);
                (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, result_path);
                err = PTC_ERR_STORAGE_WRITE_FAILED;
                if (!recovery_rollback(sysmodule)) {
                    write_disable_flag(sysmodule, "transaction_restore_failed\n");
                    err = PTC_ERR_RECOVERY_FAILED;
                }
                return finish_with_error(sysmodule, request, "release", false,
                    err, now.day_index);
            }
        }
        if (!consume_nonce(sysmodule, request, verified.day_index, verified.nonce, verified.version)) {
            char result_path[320];
            snprintf(result_path, sizeof(result_path), "%s/results/%s.json", sysmodule->app_root, request->request_id);
            (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, result_path);
            err = PTC_ERR_STORAGE_WRITE_FAILED;
            if (!recovery_rollback(sysmodule)) {
                write_disable_flag(sysmodule, "transaction_restore_failed\n");
                err = PTC_ERR_RECOVERY_FAILED;
            }
            return finish_with_error(sysmodule, request, "release", false,
                err, now.day_index);
        }
        append_event(sysmodule, request, "result_ok", PTC_ERR_OK, "");
        recovery_clear(sysmodule);
        return true;
    }
    append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED, "");
    if (recovery_path_exists(sysmodule)) {
        bool restored = recovery_rollback(sysmodule);
        append_event(sysmodule, request,
            restored ? "state_rollback_ok" : "state_rollback_failed",
            restored ? PTC_ERR_OK : PTC_ERR_RECOVERY_FAILED, "offline_code");
        if (!restored) write_disable_flag(sysmodule, "transaction_restore_failed\n");
    }
    return false;
}


static bool process_disable_today_limit(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    bool disable_flag,
    PtcClockSnapshot now)
{
    PtcPctlSettingsSnapshot original_snapshot;
    PtcPctlSettingsSnapshot restored_snapshot;
    PtcPctlStatus before_status;
    PtcPctlStatus observed_status;
    PtcPctlStatus restored_status;
    PtcRules original_rules;
    PtcRules updated_rules;
    PtcRuntimeState runtime_state;
    PtcErrorCode err;
    PtcErrorCode restore_error = PTC_ERR_OK;
    bool rules_existed;
    bool rules_persisted = false;
    bool pctl_changed = false;
    bool raw_restored = false;
    bool timer_restored = false;
    char rules_path[320];

    memset(&original_snapshot, 0, sizeof(original_snapshot));
    memset(&restored_snapshot, 0, sizeof(restored_snapshot));
    memset(&before_status, 0, sizeof(before_status));
    memset(&observed_status, 0, sizeof(observed_status));
    memset(&restored_status, 0, sizeof(restored_status));
    if (disable_flag) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_DISABLED, now.day_index);
    }
    if (!load_rules(sysmodule, &original_rules)) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_RULES_INVALID, now.day_index);
    }
    if (!load_state(sysmodule, &runtime_state)) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_BAD_REQUEST, now.day_index);
    }
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &before_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", true, err, now.day_index);
    }
    if (!sysmodule->pctl->vtable->snapshot_settings ||
        sysmodule->pctl->vtable->snapshot_settings(sysmodule->pctl, &original_snapshot) != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", false, PTC_ERR_PCTL_BACKUP_FAILED, now.day_index);
    }

    updated_rules = original_rules;
    updated_rules.today_override.present = true;
    updated_rules.today_override.day_index = now.day_index;
    updated_rules.today_override.rule.mode = PTC_RULE_MODE_UNLIMITED;
    updated_rules.today_override.rule.minutes = 0;
    join_path(rules_path, sizeof(rules_path), sysmodule->app_root, "rules.json");
    rules_existed = sysmodule->storage->vtable->exists(sysmodule->storage, rules_path);

    pctl_changed = true;
    err = apply_target(sysmodule, request, now, "release", PTC_PCTL_TARGET_UNLIMITED, 0);
    if (err == PTC_ERR_OK) {
        err = observe_target_with_optional_activation(sysmodule, request, now,
            "release", PTC_PCTL_TARGET_UNLIMITED, 0,
            "disable_today_limit", &observed_status);
    }
    if (err != PTC_ERR_OK) {
        goto disable_today_rollback;
    }
    if (!save_rules(sysmodule, &updated_rules)) {
        err = PTC_ERR_STORAGE_WRITE_FAILED;
        goto disable_today_rollback;
    }
    rules_persisted = true;
    append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "disable_today_limit");
    if (write_current_status_result(sysmodule, request, "release", false, now, true)) {
        recovery_clear(sysmodule);
        return true;
    }
    err = PTC_ERR_STORAGE_WRITE_FAILED;
    append_event(sysmodule, request, "result_write_failed", err, "disable_today_limit");

disable_today_rollback:
    if (pctl_changed) {
        restore_error = restore_snapshot_exact(sysmodule, &original_snapshot, &restored_snapshot, &restored_status,
            ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
        append_event(sysmodule, request,
            restore_error == PTC_ERR_OK ? "effect_restore" : "effect_restore_failed",
            restore_error, "disable_today_limit");
    }
    if (rules_persisted) {
        if (!restore_rules(sysmodule, &original_rules, rules_existed)) {
            restore_error = PTC_ERR_STORAGE_WRITE_FAILED;
        }
    }
    if (restore_error != PTC_ERR_OK) {
        err = PTC_ERR_PCTL_RESTORE_FAILED;
        write_disable_flag(sysmodule, "transaction_restore_failed\n");
    } else {
        recovery_clear(sysmodule);
    }
    return finish_with_error(sysmodule, request, "release", false, err, now.day_index);
}

PtcErrorCode restore_bedtime_base(PtcSysmodule *sysmodule, const PtcRequest *request,
    const PtcRules *rules,
    PtcRuntimeState *runtime_state, PtcClockSnapshot now)
{
    PtcPctlSettingsSnapshot snapshot;
    PtcPctlSettingsSnapshot restored;
    PtcPctlStatus status;
    PtcPctlStatus observed;
    uint64_t snapshot_instance = 0;
    uint16_t snapshot_start_day = 0;
    bool raw_restored = false;
    bool timer_restored = false;
    PtcErrorCode err;
    if (!runtime_state->bedtime_enforced) return PTC_ERR_OK;
    if (!recovery_begin(sysmodule, request, now)) return PTC_ERR_PCTL_BACKUP_FAILED;
    if (now.day_index == runtime_state->bedtime_start_day_index &&
        load_bedtime_snapshot(sysmodule, &snapshot, &snapshot_instance, &snapshot_start_day) &&
        snapshot_instance == runtime_state->bedtime_window_instance_id &&
        snapshot_start_day == runtime_state->bedtime_start_day_index) {
        err = restore_snapshot_exact(sysmodule, &snapshot, &restored, &status,
            ptc_weekday_from_day_index(now.day_index), &raw_restored, &timer_restored);
        if (err != PTC_ERR_OK || !raw_restored || !timer_restored) return PTC_ERR_BEDTIME_RECOVERY_FAILED;
    } else {
        PtcDayRule base = ptc_rules_today_rule(
            rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
        err = apply_target(sysmodule, request, now, "release",
            target_from_day_rule(base), base.minutes);
        if (err != PTC_ERR_OK) return PTC_ERR_BEDTIME_RECOVERY_FAILED;
        err = observe_target_with_optional_activation(sysmodule, request, now,
            "release", target_from_day_rule(base), base.minutes,
            "bedtime_restore_base", &observed);
        if (err != PTC_ERR_OK) return PTC_ERR_BEDTIME_RECOVERY_FAILED;
    }
    runtime_state->bedtime_enforced = false;
    runtime_state->bedtime_window_instance_id = 0;
    runtime_state->bedtime_start_day_index = 0;
    runtime_state->last_enforced_day_index = 0;
    runtime_state->last_enforced_mode = 0;
    runtime_state->last_enforced_minutes = 0;
    if (!save_state(sysmodule, runtime_state, now.unix_seconds)) return PTC_ERR_STORAGE_WRITE_FAILED;
    clear_bedtime_snapshot(sysmodule);
    append_event(sysmodule, request, "bedtime_recovered", PTC_ERR_OK,
        now.day_index == snapshot_start_day ? "same_day_snapshot" : "current_day_rule");
    return PTC_ERR_OK;
}

static bool bedtime_instance_is_upcoming(const PtcRules *rules, PtcClockSnapshot now, uint64_t instance_id)
{
    unsigned int offset;
    PtcBedtimeEvaluation current = ptc_bedtime_evaluate(
        rules, now.day_index, ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
    if (current.active && current.window_instance_id == instance_id) return true;
    for (offset = 0; offset < 8u; ++offset) {
        uint16_t day = (uint16_t)(now.day_index + offset);
        PtcEffectiveBedtime next = ptc_bedtime_resolve_start_day(
            rules, day, ptc_weekday_from_day_index(day));
        if (!next.window.enabled || (offset == 0u && now.minute_of_day >= next.window.start_minute)) continue;
        return ptc_bedtime_window_instance_id(day, next.window.start_minute) == instance_id;
    }
    return false;
}

static bool process_bedtime_recovery_request(PtcSysmodule *sysmodule, const PtcRequest *request,
    PtcClockSnapshot now)
{
    PtcRules rules;
    PtcRuntimeState state;
    PtcBedtimeEvaluation current;
    PtcErrorCode err;
    if (!load_rules(sysmodule, &rules) || !load_state(sysmodule, &state)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_RULES_INVALID, now.day_index);
    }
    current = ptc_bedtime_evaluate(
        &rules, now.day_index, ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
    if (request->type == PTC_REQUEST_SKIP_BEDTIME) {
        if (!bedtime_instance_is_upcoming(&rules, now, request->bedtime_window_instance_id)) {
            return finish_with_error(sysmodule, request, "release", false,
                PTC_ERR_BEDTIME_INSTANCE_NOT_ACTIVE, now.day_index);
        }
        state.bedtime_skipped_instance_id = request->bedtime_window_instance_id;
    } else {
        rules.bedtime.enabled = false;
    }
    if (current.active && state.bedtime_enforced &&
        (request->type == PTC_REQUEST_DISABLE_BEDTIME ||
         request->bedtime_window_instance_id == current.window_instance_id)) {
        err = restore_bedtime_base(sysmodule, request, &rules, &state, now);
        if (err != PTC_ERR_OK) {
            write_disable_flag(sysmodule, "bedtime_restore_failed\n");
            return finish_with_error(sysmodule, request, "release", false,
                PTC_ERR_BEDTIME_RECOVERY_FAILED, now.day_index);
        }
    }
    if (!save_rules(sysmodule, &rules) || !save_state(sysmodule, &state, now.unix_seconds)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    (void)record_activity(sysmodule, request, now, 0, 0);
    return write_current_status_result(sysmodule, request, "release",
        false, now, recovery_path_exists(sysmodule));
}

static bool process_overlay_ready(PtcSysmodule *sysmodule, const PtcRequest *request,
    PtcClockSnapshot now)
{
    char path[320];
    char text[512];
    char build[1024];
    char build_release_id[96];
    char fingerprint[65];
    join_path(path, sizeof(path), sysmodule->app_root, "build.json");
    if (!sysmodule->storage->vtable->read_text(sysmodule->storage, path, build, sizeof(build)) ||
        !json_string(build, "release_id", build_release_id, sizeof(build_release_id)) ||
        strcmp(build_release_id, request->overlay_release_id) != 0 ||
        !sysmodule_environment_fingerprint(sysmodule, fingerprint) ||
        strcmp(fingerprint, request->environment_fingerprint) != 0) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_OVERLAY_UNVERIFIED, now.day_index);
    }
    join_path(path, sizeof(path), sysmodule->app_root, "overlay/ready.json");
    snprintf(text, sizeof(text),
        "{\"version\":1,\"release_id\":\"%s\",\"boot_id\":\"%s\","
        "\"environment_fingerprint\":\"%s\",\"confirmed_at\":%lld}\n",
        build_release_id, sysmodule->boot_id, request->environment_fingerprint,
        (long long)now.unix_seconds);
    if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, text)) {
        return finish_with_error(sysmodule, request, "release", false,
            PTC_ERR_STORAGE_WRITE_FAILED, now.day_index);
    }
    return write_current_status_result(sysmodule, request, "release",
        false, now, false);
}

static bool process_rule_request(PtcSysmodule *sysmodule, const PtcRequest *request,
    bool disable_flag, PtcClockSnapshot now)
{
    PtcPctlStatus pctl_status;
    PtcPctlStatus observed_status;
    PtcRules rules;
    PtcRuntimeState runtime_state;
    PtcErrorCode err;
    PtcDayRule active_rule;
    bool pctl_request = request->type == PTC_REQUEST_SET_TODAY_LIMIT ||
        request->type == PTC_REQUEST_ADD_TODAY_MINUTES ||
        request->type == PTC_REQUEST_RESTORE_TODAY_POLICY ||
        request->type == PTC_REQUEST_SET_WEEKLY_TEMPLATE ||
        request->type == PTC_REQUEST_SET_HOLIDAY_POLICY ||
        request->type == PTC_REQUEST_SET_SCHEDULED_OVERRIDE ||
        request->type == PTC_REQUEST_SET_AUTONOMY_POLICY ||
        request->type == PTC_REQUEST_SET_BEDTIME_POLICY;
    PtcDayRule before_active_rule;
    if (disable_flag) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_DISABLED, now.day_index);
    }
    if (!load_rules(sysmodule, &rules)) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_RULES_INVALID, now.day_index);
    }
    if (!load_state(sysmodule, &runtime_state)) {
        return finish_with_error(sysmodule, request, "release", true, PTC_ERR_BAD_REQUEST, now.day_index);
    }
    before_active_rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
    err = sysmodule->pctl->vtable->read_status(sysmodule->pctl, ptc_weekday_from_day_index(now.day_index), &pctl_status);
    if (err != PTC_ERR_OK) {
        return finish_with_error(sysmodule, request, "release", true, err, now.day_index);
    }
    {
        if (pctl_request && !recovery_begin(sysmodule, request, now)) {
            return finish_with_error(sysmodule, request, "release", false,
                PTC_ERR_PCTL_BACKUP_FAILED, now.day_index);
        }
        uint16_t played_minutes = ptc_pctl_played_minutes(&pctl_status);
        err = update_rules_for_request(sysmodule, request, &rules, &runtime_state, now, played_minutes);
        if (err != PTC_ERR_OK) {
            if (pctl_request && recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule)) {
                write_disable_flag(sysmodule, "transaction_restore_failed\n");
                err = PTC_ERR_RECOVERY_FAILED;
            }
            return finish_with_error(sysmodule, request, "release", false, err, now.day_index);
        }
        if (request->type == PTC_REQUEST_SET_BEDTIME_POLICY && runtime_state.bedtime_enforced) {
            PtcBedtimeEvaluation bedtime_now = ptc_bedtime_evaluate(
                &rules, now.day_index, ptc_weekday_from_day_index(now.day_index), now.minute_of_day);
            if (!bedtime_now.active || runtime_state.bedtime_skipped_instance_id == bedtime_now.window_instance_id) {
                err = restore_bedtime_base(sysmodule, request, &rules, &runtime_state, now);
                if (err != PTC_ERR_OK) {
                    write_disable_flag(sysmodule, "bedtime_restore_failed\n");
                    return finish_with_error(sysmodule, request, "release", false,
                        PTC_ERR_BEDTIME_RECOVERY_FAILED, now.day_index);
                }
            }
        }
        append_event(sysmodule, request, "state_persisted", PTC_ERR_OK, "");
        if (request->type == PTC_REQUEST_SET_TODAY_LIMIT ||
            request->type == PTC_REQUEST_ADD_TODAY_MINUTES ||
            request->type == PTC_REQUEST_DISABLE_TODAY_LIMIT ||
            request->type == PTC_REQUEST_RESTORE_TODAY_POLICY ||
            request->type == PTC_REQUEST_SET_HOLIDAY_POLICY ||
            ((request->type == PTC_REQUEST_SET_SCHEDULED_OVERRIDE ||
              request->type == PTC_REQUEST_SET_WEEKLY_TEMPLATE) &&
             (before_active_rule.mode != ptc_rules_today_rule(&rules, now.day_index,
                 ptc_weekday_from_day_index(now.day_index)).mode ||
              before_active_rule.minutes != ptc_rules_today_rule(&rules, now.day_index,
                 ptc_weekday_from_day_index(now.day_index)).minutes))) {
            active_rule = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
            err = apply_target(sysmodule, request, now, "release", target_from_day_rule(active_rule), active_rule.minutes);
            if (err != PTC_ERR_OK) {
                return finish_with_error(sysmodule, request, "release", false, err, now.day_index);
            }
            err = observe_target_with_optional_activation(sysmodule, request, now,
                "release", target_from_day_rule(active_rule),
                active_rule.minutes, "rule_request", &observed_status);
            if (err != PTC_ERR_OK) {
                if (!recovery_rollback(sysmodule)) {
                    write_disable_flag(sysmodule, "transaction_restore_failed\n");
                    err = PTC_ERR_RECOVERY_FAILED;
                }
                return finish_with_error(sysmodule, request, "release", false, err, now.day_index);
            }
        }
        {
            uint16_t activity_minutes = request->minutes;
            if (request->type == PTC_REQUEST_SET_SCHEDULED_OVERRIDE) {
                activity_minutes = request->scheduled_override.enabled
                    ? request->scheduled_override.rule.minutes : 0u;
            } else if (request->type == PTC_REQUEST_SET_AUTONOMY_POLICY) {
                activity_minutes = request->autonomy_policy.daily_buffer_minutes;
            } else if (request->type == PTC_REQUEST_SET_WEEKLY_TEMPLATE) {
                PtcDayRule today_r = ptc_rules_today_rule(&rules, now.day_index, ptc_weekday_from_day_index(now.day_index));
                activity_minutes = today_r.mode == PTC_RULE_MODE_LIMIT ? today_r.minutes : 0u;
            }
            if (!record_activity(sysmodule, request, now, activity_minutes, activity_minutes)) {
                append_event(sysmodule, request, "result_write_failed", PTC_ERR_STORAGE_WRITE_FAILED,
                    "activity_history");
                if (recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule)) {
                    write_disable_flag(sysmodule, "transaction_restore_failed\n");
                    err = PTC_ERR_RECOVERY_FAILED;
                } else err = PTC_ERR_STORAGE_WRITE_FAILED;
                return finish_with_error(sysmodule, request, "release", false,
                    err, now.day_index);
            }
        }
    }
    {
        bool ok = write_current_status_result(sysmodule, request, "release",
            false, now, pctl_request && recovery_path_exists(sysmodule));
        if (ok) recovery_clear(sysmodule);
        else if (recovery_path_exists(sysmodule) && !recovery_rollback(sysmodule))
            write_disable_flag(sysmodule, "transaction_restore_failed\n");
        return ok;
    }
}

void process_request_text(PtcSysmodule *sysmodule, const char *request_text, const char *expected_request_id)
{
    PtcRequest request;
    PtcRuntimeConfig config;
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    PtcErrorCode parse_err;
    bool disable_flag;
    char disable_path[320];

    parse_err = ptc_request_parse(request_text, &request);
    if (parse_err == PTC_ERR_OK && expected_request_id && strcmp(request.request_id, expected_request_id) != 0) {
        parse_err = PTC_ERR_BAD_REQUEST;
    }
    if (parse_err != PTC_ERR_OK) {
        memset(&request, 0, sizeof(request));
        snprintf(request.request_id, sizeof(request.request_id), "unknown");
        snprintf(request.type_text, sizeof(request.type_text), "unknown");
        if (expected_request_id && ptc_request_id_is_valid(expected_request_id)) {
            snprintf(request.request_id, sizeof(request.request_id), "%s", expected_request_id);
        } else {
            (void)json_string(request_text, "request_id", request.request_id, sizeof(request.request_id));
            if (!ptc_request_id_is_valid(request.request_id)) snprintf(request.request_id, sizeof(request.request_id), "unknown");
        }
        (void)json_string(request_text, "type", request.type_text, sizeof(request.type_text));
        (void)finish_with_error(sysmodule, &request, "release", true, parse_err, now.day_index);
        return;
    }
    append_event(sysmodule, &request, "request_received", PTC_ERR_OK, "");
    if (!load_config(sysmodule, &config)) {
        (void)finish_with_error(sysmodule, &request, "release", true, PTC_ERR_CONFIG_INVALID, now.day_index);
        return;
    }
    join_path(disable_path, sizeof(disable_path), sysmodule->app_root, "flags/disable.flag");
    disable_flag = sysmodule->storage->vtable->exists(sysmodule->storage, disable_path);

    if (request.type != PTC_REQUEST_STATUS &&
        request.type != PTC_REQUEST_COMPLETE_SETUP &&
        request.type != PTC_REQUEST_RETRY_SETUP_RELEASE &&
        request.type != PTC_REQUEST_RESTORE_INSTALL_SNAPSHOT &&
        request.type != PTC_REQUEST_SKIP_BEDTIME &&
        request.type != PTC_REQUEST_DISABLE_BEDTIME &&
        request.type != PTC_REQUEST_OVERLAY_READY) {
        PtcSetupState setup;
        if (!load_setup_state(sysmodule, &setup) || strcmp(setup.phase, "active") != 0) {
            (void)finish_with_error(sysmodule, &request, "release", true,
                PTC_ERR_SETUP_PENDING, now.day_index);
            return;
        }
    }

    switch (request.type) {
#ifdef PLAYWISE_DEVICE_LAB
    case PTC_REQUEST_LAB_SESSION_START:
    case PTC_REQUEST_LAB_PHASE_START:
    case PTC_REQUEST_LAB_SESSION_STATUS:
    case PTC_REQUEST_LAB_OBSERVATION:
    case PTC_REQUEST_LAB_SESSION_RESTORE:
        (void)ptc_lab_process_request(sysmodule, &request);
        break;
#endif
    case PTC_REQUEST_STATUS:
        (void)process_status(sysmodule, &request, now);
        break;
    case PTC_REQUEST_CLEAR_REDEMPTION_HISTORY:
        (void)process_clear_redemption_history(sysmodule, &request, now);
        break;
    case PTC_REQUEST_CLEAR_ACTIVITY_HISTORY:
        (void)process_clear_activity_history(sysmodule, &request, now);
        break;
    case PTC_REQUEST_CLAIM_DAILY_BUFFER:
        (void)process_claim_daily_buffer(sysmodule, &request, disable_flag, now);
        break;
    case PTC_REQUEST_OFFLINE_CODE:
        (void)process_offline_code(sysmodule, &request, &config, disable_flag, now);
        break;
    case PTC_REQUEST_PREVIEW_OFFLINE_CODE:
        (void)process_preview_offline_code(sysmodule, &request, &config, disable_flag, now);
        break;
    case PTC_REQUEST_COMPLETE_SETUP:
        (void)process_complete_setup(sysmodule, &request, &config, disable_flag, now);
        break;
    case PTC_REQUEST_RETRY_SETUP_RELEASE:
        (void)process_retry_setup_release(sysmodule, &request, &config, now);
        break;
    case PTC_REQUEST_RESTORE_INSTALL_SNAPSHOT:
        (void)process_restore_install_snapshot(sysmodule, &request, now);
        break;
    case PTC_REQUEST_SKIP_BEDTIME:
    case PTC_REQUEST_DISABLE_BEDTIME:
        (void)process_bedtime_recovery_request(sysmodule, &request, now);
        break;
    case PTC_REQUEST_OVERLAY_READY:
        (void)process_overlay_ready(sysmodule, &request, now);
        break;
    case PTC_REQUEST_DISABLE_TODAY_LIMIT:
        (void)process_disable_today_limit(sysmodule, &request, disable_flag, now);
        break;
    case PTC_REQUEST_SET_TODAY_LIMIT:
    case PTC_REQUEST_ADD_TODAY_MINUTES:
    case PTC_REQUEST_RESTORE_TODAY_POLICY:
    case PTC_REQUEST_SET_WEEKLY_TEMPLATE:
    case PTC_REQUEST_SET_HOLIDAY_POLICY:
    case PTC_REQUEST_SET_SCHEDULED_OVERRIDE:
    case PTC_REQUEST_SET_AUTONOMY_POLICY:
    case PTC_REQUEST_SET_BEDTIME_POLICY:
    case PTC_REQUEST_CONFIRM_BEDTIME_REQUIREMENTS:
        (void)process_rule_request(sysmodule, &request, disable_flag, now);
        break;
    case PTC_REQUEST_UNKNOWN:
    default:
        (void)finish_with_error(sysmodule, &request, "release", true, PTC_ERR_UNKNOWN_REQUEST_TYPE, now.day_index);
        break;
    }
}

#ifdef PLAYWISE_EDEN
void ptc_sysmodule_process_request_direct(PtcSysmodule *sysmodule,
    const char *request_text, const char *expected_request_id)
{
    if (!sysmodule || !request_text) return;
    process_request_text(sysmodule, request_text, expected_request_id);
}
#endif
