#include "lab_session_internal.h"

#ifdef PLAYWISE_DEVICE_LAB

#include <stdio.h>
#include <string.h>

#include "../common/crypto/sha256.h"
#include "../common/version.h"

void lab_final_report_path(PtcSysmodule *sysmodule, const LabState *state, char *out, size_t size)
{
    snprintf(out, size, "%s/reports/%s.json", sysmodule->app_root, state->run_id);
}

void lab_draft_report_path(PtcSysmodule *sysmodule, const LabState *state, char *out, size_t size)
{
    snprintf(out, size, "%s/lab/report-%s.draft.json", sysmodule->app_root, state->run_id);
}

unsigned int lab_required_phase_count(const LabState *state)
{
    if (strcmp(state->mode, "restriction_quick") == 0) return 1U;
    if (strcmp(state->mode, "timer_activation_ab") == 0)
        return 7U;
    return 6U;
}

static const char *tri_state_json(int value)
{
    return value < 0 ? "null" : (value ? "true" : "false");
}

bool lab_write_public_evidence(PtcSysmodule *sysmodule, const LabState *state,
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

bool lab_read_fragment(PtcSysmodule *sysmodule, const char *relative, char *out, size_t size)
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

bool lab_rebuild_report(PtcSysmodule *sysmodule, const LabState *state)
{
    char report[LAB_REPORT_BUFFER];
    char fragment[6144];
    char path[320];
    size_t i;
    unsigned int completed_phases = 0;
    unsigned int required_phases = lab_required_phase_count(state);
    unsigned int report_phase_slots = strcmp(state->mode, "timer_activation_ab") == 0
        ? required_phases : 6U;
    bool manual_required = strcmp(state->mode, "timer_activation_ab") != 0;
    bool observation_recorded = state->observation[0] != '\0';
    bool runtime_effect_recorded = state->runtime_effect[0] != '\0';
    int activation_evidence_complete = -1;
    int lifecycle_evidence_complete = -1;
    bool complete;
    char entry_method[16];
    char campaign_fragment[384];
    lab_activation_method(sysmodule, entry_method);
    if (state->campaign_id[0]) {
        snprintf(campaign_fragment, sizeof(campaign_fragment),
            "{\"campaign_id\":\"%s\",\"slot\":\"%s\",\"attempt\":%d,"
            "\"game_slot\":\"%s\",\"official_pause_expected\":\"%s\","
            "\"context_confirmed\":true}",
            state->campaign_id, state->campaign_slot, state->campaign_attempt,
            state->game_slot, state->official_pause_expected);
    } else {
        snprintf(campaign_fragment, sizeof(campaign_fragment), "null");
    }
    if (strcmp(state->mode, "timer_activation_ab") == 0) {
        activation_evidence_complete = state->home_awake_counted >= 0 && state->sleep_excluded >= 0 &&
            state->limited_settings_only_runtime_ready >= 0 && state->grant_settings_only_runtime_ready >= 0 &&
            state->unlimited_settings_only_runtime_ready >= 0 &&
            ((state->limited_settings_only_runtime_ready == 1 && state->limited_fallback_called == 0) ||
             (state->limited_settings_only_runtime_ready == 0 && state->limited_fallback_called == 1 && state->limited_fallback_succeeded == 1)) &&
            ((state->grant_settings_only_runtime_ready == 1 && state->grant_fallback_called == 0) ||
             (state->grant_settings_only_runtime_ready == 0 && state->grant_fallback_called == 1 && state->grant_fallback_succeeded == 1)) &&
            ((state->unlimited_settings_only_runtime_ready == 1 && state->unlimited_fallback_called == 0) ||
             (state->unlimited_settings_only_runtime_ready == 0 && state->unlimited_fallback_called == 1 && state->unlimited_fallback_succeeded == 1));
    }
    int written = snprintf(report, sizeof(report),
        "{\"version\":2,\"schema_version\":2,\"run_id\":\"%s\",\"mode\":\"%s\",\"report_status\":\"draft\","
        "\"entry_method\":\"%s\",\"campaign\":%s,"
        "\"baseline\":{\"settings_all_zero\":%s,\"activation_preconditions_met\":%s,"
        "\"remaining_ns\":%lld,\"minimum_remaining_ns\":%lld},"
        "\"environment\":{\"title_id\":\"%s\","
        "\"ipc_service\":\"%s\",\"sd_root\":\"%s\",\"runtime\":",
        state->run_id, state->mode, entry_method, campaign_fragment,
        state->baseline_all_zero ? "true" : "false",
        state->activation_preconditions_met ? "true" : "false",
        (long long)state->baseline_remaining_ns, (long long)LAB_MINIMUM_REMAINING_NS,
        PLAYWISE_TITLE_ID, PLAYWISE_IPC_SERVICE, PLAYWISE_SD_ROOT);
    if (written < 0 || (size_t)written >= sizeof(report)) return false;
    if (!lab_read_fragment(sysmodule, "environment.json", fragment, sizeof(fragment))) return false;
    if (!append_text(report, sizeof(report), fragment) ||
        !append_text(report, sizeof(report), ",\"build\":")) return false;
    if (!lab_read_fragment(sysmodule, "build.json", fragment, sizeof(fragment))) return false;
    if (!append_text(report, sizeof(report), fragment) ||
        !append_text(report, sizeof(report), "},\"durations\":{\"phase_seconds\":75,"
            "\"activation_home_sleep_seconds\":90,"
            "\"restriction_restore_seconds\":15},\"public_parity\":")) return false;
    if (!lab_read_fragment(sysmodule, "lab/public.json", fragment, sizeof(fragment))) snprintf(fragment, sizeof(fragment), "null");
    if (!append_text(report, sizeof(report), fragment) ||
        !append_text(report, sizeof(report), ",\"phases\":[")) return false;
    for (i = 0; i < report_phase_slots; ++i) {
        char relative[96];
        bool phase_present;
        if (i && !append_text(report, sizeof(report), ",")) return false;
        snprintf(relative, sizeof(relative), "lab/phase-%u.json", (unsigned int)i);
        phase_present = lab_read_fragment(sysmodule, relative, fragment, sizeof(fragment));
        if (!phase_present) snprintf(fragment, sizeof(fragment), "null");
        else ++completed_phases;
        if (!append_text(report, sizeof(report), fragment)) return false;
    }
    if (strcmp(state->mode, "full") == 0) {
        /* A full run may finish collecting and restoring an all-zero baseline,
           but those lifecycle phases remain evidence-incomplete. */
        lifecycle_evidence_complete = !state->baseline_all_zero && completed_phases == required_phases;
    }
    complete = completed_phases == required_phases &&
        (!manual_required || (observation_recorded && runtime_effect_recorded)) && state->restored &&
        strcmp(state->restore_verdict, "exact_restore_proved") == 0 &&
        strcmp(state->state, "complete") == 0 && activation_evidence_complete != 0;
    snprintf(fragment, sizeof(fragment),
        "],\"timer_activation_ab\":{\"home_awake_counted\":%s,\"sleep_excluded\":%s,"
        "\"fallback_cases\":["
        "{\"target\":\"limited\",\"settings_only_runtime_ready\":%s,\"fallback_called\":%s,\"fallback_succeeded\":%s},"
        "{\"target\":\"grant\",\"settings_only_runtime_ready\":%s,\"fallback_called\":%s,\"fallback_succeeded\":%s},"
        "{\"target\":\"unlimited\",\"settings_only_runtime_ready\":%s,\"fallback_called\":%s,\"fallback_succeeded\":%s}]},"
        "\"manual_observation\":%s%s%s,\"manual_runtime_effect\":%s%s%s,"
        "\"restoration\":{\"proved\":%s,\"verdict\":\"%s\"},"
        "\"summary\":{\"automated_phases_completed\":%u,\"required_automated_phases\":%u,"
        "\"manual_observation_recorded\":%s,\"manual_runtime_effect_recorded\":%s,"
        "\"activation_evidence_complete\":%s,\"lifecycle_evidence_complete\":%s,"
        "\"complete\":%s,\"ipc_callable\":\"see_commands\",\"wire_shape_confirmed\":\"see_commands\","
        "\"product_semantics\":\"evidence_only_until_review\"}}\n",
        tri_state_json(state->home_awake_counted), tri_state_json(state->sleep_excluded),
        tri_state_json(state->limited_settings_only_runtime_ready),
        tri_state_json(state->limited_fallback_called), tri_state_json(state->limited_fallback_succeeded),
        tri_state_json(state->grant_settings_only_runtime_ready),
        tri_state_json(state->grant_fallback_called), tri_state_json(state->grant_fallback_succeeded),
        tri_state_json(state->unlimited_settings_only_runtime_ready),
        tri_state_json(state->unlimited_fallback_called), tri_state_json(state->unlimited_fallback_succeeded),
        state->observation[0] ? "\"" : "", state->observation[0] ? state->observation : "null",
        state->observation[0] ? "\"" : "",
        state->runtime_effect[0] ? "\"" : "", state->runtime_effect[0] ? state->runtime_effect : "null",
        state->runtime_effect[0] ? "\"" : "",
        state->restored ? "true" : "false", state->restore_verdict,
        completed_phases, required_phases, observation_recorded ? "true" : "false",
        runtime_effect_recorded ? "true" : "false", tri_state_json(activation_evidence_complete),
        tri_state_json(lifecycle_evidence_complete),
        complete ? "true" : "false");
    if (!append_text(report, sizeof(report), fragment)) return false;
    if (complete) {
        char draft_path[320];
        char *status = strstr(report, "\"report_status\":\"draft\"");
        if (status) memcpy(status + strlen("\"report_status\":\""), "final", 5U);
        lab_final_report_path(sysmodule, state, path, sizeof(path));
        if (!sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, report)) return false;
        lab_draft_report_path(sysmodule, state, draft_path, sizeof(draft_path));
        if (sysmodule->storage->vtable->exists(sysmodule->storage, draft_path))
            (void)sysmodule->storage->vtable->remove_path(sysmodule->storage, draft_path);
        return true;
    }
    lab_draft_report_path(sysmodule, state, path, sizeof(path));
    return sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, path, report);
}

#endif
