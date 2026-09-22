#include "sysmodule_internal.h"

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

bool process_grant_request_surface(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const PtcRuntimeConfig *config,
    bool disable_flag,
    PtcClockSnapshot now)
{
    switch (request->type) {
    case PTC_REQUEST_CLAIM_DAILY_BUFFER:
        (void)process_claim_daily_buffer(sysmodule, request, disable_flag, now);
        return true;
    case PTC_REQUEST_OFFLINE_CODE:
        (void)process_offline_code(sysmodule, request, config, disable_flag, now);
        return true;
    case PTC_REQUEST_PREVIEW_OFFLINE_CODE:
        (void)process_preview_offline_code(sysmodule, request, config, disable_flag, now);
        return true;
    default:
        return false;
    }
}
