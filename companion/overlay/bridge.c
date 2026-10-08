#include "bridge.h"
#include "../ui_language.h"

#include <stdio.h>
#include <string.h>

#include "../../common/protocol/error_code.h"

void ptc_overlay_bridge_init(PtcOverlayBridge *bridge, const char *app_root, PtcStorage *storage)
{
    if (!bridge) return;
    memset(bridge, 0, sizeof(*bridge));
#ifdef __SWITCH__
    ptc_switch_ipc_client_init(&bridge->ipc);
    ptc_companion_transport_init(&bridge->transport, app_root, storage, ptc_switch_ipc_backend(), &bridge->ipc);
#else
    ptc_companion_transport_init(&bridge->transport, app_root, storage, NULL, NULL);
#endif
}

void ptc_overlay_bridge_exit(PtcOverlayBridge *bridge)
{
    if (!bridge) return;
    ptc_companion_transport_cancel(&bridge->transport);
#ifdef __SWITCH__
    ptc_switch_ipc_client_exit(&bridge->ipc);
#endif
    bridge->waiting = false;
}

static PtcCompanionStatus begin_request(PtcOverlayBridge *bridge, PtcCompanionStatus status)
{
    if (!bridge) return PTC_COMPANION_BAD_ARGUMENT;
    bridge->last_status = status;
    if (status == PTC_COMPANION_OK) {
        bridge->elapsed_ms = 0;
        bridge->waiting = true;
    }
    return status;
}

static bool prepare_request(PtcOverlayBridge *bridge, int64_t created_at, uint16_t random16)
{
    if (!bridge || bridge->waiting) return false;
    memset(&bridge->summary, 0, sizeof(bridge->summary));
    bridge->last_status = PTC_COMPANION_PENDING;
    if (ptc_companion_make_request_id(bridge->request_id, sizeof(bridge->request_id),
            created_at * 1000, random16) != PTC_COMPANION_OK) {
        bridge->last_status = PTC_COMPANION_BAD_ARGUMENT;
        return false;
    }
    return true;
}

PtcCompanionStatus ptc_overlay_bridge_submit(
    PtcOverlayBridge *bridge,
    const char *code,
    int64_t created_at,
    uint16_t random16,
    const PtcCompanionResultSummary *preview)
{
    PtcCompanionStatus status;
    PtcPendingRedemption pending;
    if (!bridge || !code || code[0] == '\0' || !preview || !preview->preview_available) {
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    memset(&bridge->summary, 0, sizeof(bridge->summary));
    bridge->last_status = PTC_COMPANION_PENDING;
    if (ptc_companion_make_request_id(bridge->request_id, sizeof(bridge->request_id), created_at * 1000, random16) != PTC_COMPANION_OK) {
        bridge->last_status = PTC_COMPANION_BAD_ARGUMENT;
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    memset(&pending, 0, sizeof(pending));
    snprintf(pending.request_id, sizeof(pending.request_id), "%s", bridge->request_id);
    pending.confirmed_at = created_at;
    pending.grant_minutes = preview->grant_minutes;
    pending.before_remaining_available = preview->remaining_available;
    pending.before_remaining_minutes = preview->remaining_minutes;
    pending.before_unlimited = preview->converts_unlimited_to_limited;
    pending.after_remaining_available = preview->remaining_after_available;
    pending.after_remaining_minutes = preview->remaining_after_minutes;
    pending.effective_add_minutes = preview->effective_add_minutes;
    pending.capped = preview->preview_capped;
    pending.converts_unlimited_to_limited = preview->converts_unlimited_to_limited;
    status = ptc_companion_pending_redemption_save(&bridge->transport.file, &pending);
    if (status != PTC_COMPANION_OK) {
        bridge->last_status = status;
        return status;
    }
    status = ptc_companion_transport_submit_offline_code(&bridge->transport, bridge->request_id, created_at, code);
    bridge->last_status = status;
    if (status == PTC_COMPANION_OK) {
        pending.submitted = true;
        (void)ptc_companion_pending_redemption_save(&bridge->transport.file, &pending);
        bridge->elapsed_ms = 0;
        bridge->waiting = true;
    } else {
        (void)ptc_companion_pending_redemption_clear(&bridge->transport.file);
    }
    return status;
}

PtcCompanionStatus ptc_overlay_bridge_submit_status(PtcOverlayBridge *bridge, int64_t created_at, uint16_t random16)
{
    PtcCompanionStatus status;
    if (!bridge) return PTC_COMPANION_BAD_ARGUMENT;
    memset(&bridge->summary, 0, sizeof(bridge->summary));
    bridge->last_status = PTC_COMPANION_PENDING;
    if (ptc_companion_make_request_id(bridge->request_id, sizeof(bridge->request_id), created_at * 1000, random16) != PTC_COMPANION_OK) {
        bridge->last_status = PTC_COMPANION_BAD_ARGUMENT;
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    status = ptc_companion_transport_submit_status(&bridge->transport, bridge->request_id, created_at);
    bridge->last_status = status;
    if (status == PTC_COMPANION_OK) {
        bridge->elapsed_ms = 0;
        bridge->waiting = true;
    }
    return status;
}

PtcCompanionStatus ptc_overlay_bridge_submit_overlay_ready(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16, const char *release_id,
    const char *boot_id, const char *environment_fingerprint)
{
    if (!release_id || !boot_id || !environment_fingerprint ||
        !prepare_request(bridge, created_at, random16)) return PTC_COMPANION_BAD_ARGUMENT;
    return begin_request(bridge,
        ptc_companion_transport_submit_overlay_ready(&bridge->transport, bridge->request_id,
            created_at, release_id, boot_id, environment_fingerprint));
}

PtcCompanionStatus ptc_overlay_bridge_skip_bedtime(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16, uint64_t window_instance_id)
{
    if (window_instance_id == 0 || !prepare_request(bridge, created_at, random16))
        return PTC_COMPANION_BAD_ARGUMENT;
    return begin_request(bridge,
        ptc_companion_transport_submit_skip_bedtime(&bridge->transport, bridge->request_id,
            created_at, window_instance_id));
}

PtcCompanionStatus ptc_overlay_bridge_waive_dock(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16, uint16_t day_index)
{
    if (!prepare_request(bridge, created_at, random16)) return PTC_COMPANION_BAD_ARGUMENT;
    return begin_request(bridge, ptc_companion_transport_submit_waive_dock_policy(
        &bridge->transport, bridge->request_id, created_at, day_index));
}

PtcCompanionStatus ptc_overlay_bridge_skip_eye_care(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16, uint64_t break_id)
{
    if (break_id == 0 || !prepare_request(bridge, created_at, random16))
        return PTC_COMPANION_BAD_ARGUMENT;
    return begin_request(bridge,
        ptc_companion_transport_submit_skip_eye_care_break(&bridge->transport,
            bridge->request_id, created_at, break_id));
}

PtcCompanionStatus ptc_overlay_bridge_clear_bedtime_skip(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16, uint64_t window_instance_id)
{
    if (window_instance_id == 0 || !prepare_request(bridge, created_at, random16))
        return PTC_COMPANION_BAD_ARGUMENT;
    return begin_request(bridge,
        ptc_companion_transport_submit_clear_bedtime_skip(&bridge->transport, bridge->request_id,
            created_at, window_instance_id));
}

PtcCompanionStatus ptc_overlay_bridge_disable_bedtime(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16)
{
    if (!prepare_request(bridge, created_at, random16)) return PTC_COMPANION_BAD_ARGUMENT;
    return begin_request(bridge,
        ptc_companion_transport_submit_empty(&bridge->transport, bridge->request_id,
            created_at, "disable_bedtime"));
}

PtcCompanionStatus ptc_overlay_bridge_add_today_minutes(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16, uint16_t minutes)
{
    if (minutes < 1u || minutes > 120u ||
        !prepare_request(bridge, created_at, random16)) return PTC_COMPANION_BAD_ARGUMENT;
    return begin_request(bridge,
        ptc_companion_transport_submit_add_today_minutes(&bridge->transport,
            bridge->request_id, created_at, minutes));
}

PtcCompanionStatus ptc_overlay_bridge_disable_today_limit(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16)
{
    if (!prepare_request(bridge, created_at, random16)) return PTC_COMPANION_BAD_ARGUMENT;
    return begin_request(bridge,
        ptc_companion_transport_submit_empty(&bridge->transport, bridge->request_id,
            created_at, "disable_today_limit"));
}

PtcCompanionStatus ptc_overlay_bridge_restore_install_snapshot(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16)
{
    if (!prepare_request(bridge, created_at, random16)) return PTC_COMPANION_BAD_ARGUMENT;
    return begin_request(bridge,
        ptc_companion_transport_submit_empty(&bridge->transport, bridge->request_id,
            created_at, "restore_install_snapshot"));
}

PtcCompanionStatus ptc_overlay_bridge_claim_daily_buffer(PtcOverlayBridge *bridge,
    int64_t created_at, uint16_t random16)
{
    if (!prepare_request(bridge, created_at, random16)) return PTC_COMPANION_BAD_ARGUMENT;
    return begin_request(bridge,
        ptc_companion_transport_submit_empty(&bridge->transport, bridge->request_id,
            created_at, "claim_daily_buffer"));
}

PtcCompanionStatus ptc_overlay_bridge_poll(PtcOverlayBridge *bridge, int elapsed_ms, int timeout_ms)
{
    PtcCompanionStatus status;
    if (!bridge || !bridge->waiting) return PTC_COMPANION_BAD_ARGUMENT;
    if (elapsed_ms > 0) bridge->elapsed_ms += elapsed_ms;
    status = ptc_companion_transport_poll(&bridge->transport, elapsed_ms, timeout_ms,
        bridge->result_json, sizeof(bridge->result_json));
    bridge->last_status = status;
    if (status == PTC_COMPANION_OK) {
        if (ptc_companion_parse_result_summary(bridge->result_json, &bridge->summary) != PTC_COMPANION_OK) {
            bridge->waiting = false;
            bridge->last_status = PTC_COMPANION_RESULT_INVALID;
            return PTC_COMPANION_RESULT_INVALID;
        }
        bridge->waiting = false;
    } else if (status != PTC_COMPANION_PENDING) {
        bridge->waiting = false;
    }
    return status;
}

PtcCompanionTransportRoute ptc_overlay_bridge_transport_state(const PtcOverlayBridge *bridge)
{
    return bridge ? ptc_companion_transport_route(&bridge->transport) : PTC_TRANSPORT_ROUTE_NONE;
}

const char *ptc_overlay_bridge_transport_label(const PtcOverlayBridge *bridge)
{
    return ptc_companion_transport_route_label_zh(ptc_overlay_bridge_transport_state(bridge));
}

const char *ptc_overlay_bridge_error_message_zh(const PtcOverlayBridge *bridge)
{
    if (!bridge) return ptc_ui_text(PTC_UI_T_INVALID_REQUEST_PARAMETERS);
    if (bridge->summary.valid) {
        if (!bridge->summary.ok) {
            if (bridge->summary.error_code == PTC_ERR_CONTROL_BUSY) return ptc_ui_text(PTC_UI_T_CONTROL_BUSY);
            if (bridge->summary.message[0]) return bridge->summary.message;
            if (bridge->summary.error_code > 0)
                return ptc_error_message_zh((PtcErrorCode)bridge->summary.error_code);
            return ptc_ui_text(PTC_UI_T_REQUEST_FAILED_PLEASE_TRY_AGAIN_LATER);
        }
        return ptc_ui_text(PTC_UI_T_REQUEST_SUCCESSFUL);
    }
    switch (bridge->last_status) {
    case PTC_COMPANION_TIMEOUT: return ptc_ui_text(PTC_UI_T_BACKGROUND_SERVICE_TIMED_OUT_PLEASE_RETRY);
    case PTC_COMPANION_WRITE_FAILED:
    case PTC_COMPANION_RENAME_FAILED: return ptc_ui_text(PTC_UI_T_FAILED_TO_WRITE_REQUEST_PLEASE_CHECK_SD);
    case PTC_COMPANION_RESULT_INVALID:
    case PTC_COMPANION_RESULT_MISMATCH: return ptc_ui_text(PTC_UI_T_INVALID_RESULT_RETURNED_FROM_BACKGROUND);
    case PTC_COMPANION_BAD_ARGUMENT: return ptc_ui_text(PTC_UI_T_REQUEST_REJECTED_BY_BACKGROUND_SERVICE);
    case PTC_COMPANION_QUIESCING: return ptc_ui_text(PTC_UI_T_BACKGROUND_IS_SWITCHING_SAFELY_PLEASE_RETRY_LATER);
    default: return ptc_ui_text(PTC_UI_T_CANNOT_CONNECT_TO_BACKGROUND_SERVICE_PLEASE_RETRY);
    }
}

PtcCompanionStatus ptc_overlay_bridge_preview(PtcOverlayBridge *bridge, const char *code, int64_t created_at, uint16_t random16)
{
    PtcCompanionStatus status;
    if (!bridge || !code || code[0] == '\0') return PTC_COMPANION_BAD_ARGUMENT;
    memset(&bridge->summary, 0, sizeof(bridge->summary));
    bridge->last_status = PTC_COMPANION_PENDING;
    if (ptc_companion_make_request_id(bridge->request_id, sizeof(bridge->request_id), created_at * 1000, random16) != PTC_COMPANION_OK) {
        bridge->last_status = PTC_COMPANION_BAD_ARGUMENT;
        return PTC_COMPANION_BAD_ARGUMENT;
    }
    status = ptc_companion_transport_submit_preview_offline_code(
        &bridge->transport, bridge->request_id, created_at, code);
    bridge->last_status = status;
    if (status == PTC_COMPANION_OK) {
        bridge->elapsed_ms = 0;
        bridge->waiting = true;
    }
    return status;
}

bool ptc_overlay_bridge_status_succeeded(const PtcOverlayBridge *bridge)
{
    return bridge && bridge->summary.valid && bridge->summary.ok &&
        strcmp(bridge->summary.type, "status") == 0;
}

bool ptc_overlay_bridge_offline_code_succeeded(const PtcOverlayBridge *bridge)
{
    return bridge && bridge->summary.valid && bridge->summary.ok &&
        strcmp(bridge->summary.type, "offline_code") == 0;
}

bool ptc_overlay_bridge_preview_succeeded(const PtcOverlayBridge *bridge)
{
    return bridge && bridge->summary.valid && bridge->summary.ok &&
        bridge->summary.preview_available && strcmp(bridge->summary.type, "preview_offline_code") == 0;
}

const char *ptc_overlay_rule_source_label(const char *source)
{
    if (!source || !source[0]) return ptc_ui_text(PTC_UI_T_GENERAL_PLAN);
    if (strcmp(source, "today_override") == 0) return ptc_ui_text(PTC_UI_T_TODAY_S_ADJUSTMENT);
    if (strcmp(source, "scheduled_override") == 0) return ptc_ui_text(PTC_UI_T_SPECIFIED_DATE_QUOTA);
    if (strcmp(source, "statutory_holiday") == 0) return ptc_ui_text(PTC_UI_T_LEGAL_HOLIDAYS);
    if (strcmp(source, "makeup_workday") == 0) return ptc_ui_text(PTC_UI_T_ADJUSTED_WORKING_DAYS);
    if (strcmp(source, "weekly") == 0) return ptc_ui_text(PTC_UI_T_RULE_WEEKLY);
    return source;
}

void ptc_overlay_format_child_quota_parts(
    const PtcCompanionResultSummary *summary,
    char *label_out, size_t label_size,
    char *val_out, size_t val_size,
    char *note_out, size_t note_size)
{
    if (label_out && label_size > 0) label_out[0] = '\0';
    if (val_out && val_size > 0) val_out[0] = '\0';
    if (note_out && note_size > 0) note_out[0] = '\0';
    if (!summary || !summary->valid) {
        if (label_out && label_size > 0) snprintf(label_out, label_size, ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE));
        if (val_out && val_size > 0) snprintf(val_out, val_size, ptc_ui_text(PTC_UI_T_MINUTES_3));
        return;
    }
    if (summary->unrestricted_today == 1 || summary->eye_care_unlimited_capped) {
        if (label_out && label_size > 0) snprintf(label_out, label_size, ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE));
        if (val_out && val_size > 0) snprintf(val_out, val_size, ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        if (note_out && note_size > 0) {
            if (summary->eye_care_unlimited_capped)
                snprintf(note_out, note_size, "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_UNLIMITED_CAP));
            else if (summary->played_minutes_available && summary->played_minutes >= 0)
                snprintf(note_out, note_size, ptc_ui_text(PTC_UI_T_PLAYED_ABOUT_D_MIN),
                    summary->played_minutes);
        }
        return;
    }
    if (summary->remaining_available && summary->played_minutes_available &&
        summary->remaining_minutes >= 0 && summary->played_minutes >= 0) {
        int total = summary->remaining_minutes + summary->played_minutes;
        if (label_out && label_size > 0) snprintf(label_out, label_size, ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE));
        if (val_out && val_size > 0) snprintf(val_out, val_size, ptc_ui_text(PTC_UI_T_D_MIN), total);
        if (note_out && note_size > 0) {
            snprintf(note_out, note_size, ptc_ui_text(PTC_UI_T_PLAYED_ABOUT_D_MIN), summary->played_minutes);
        }
        return;
    }
    if (summary->played_minutes_available && summary->played_minutes >= 0) {
        if (label_out && label_size > 0) snprintf(label_out, label_size, ptc_ui_text(PTC_UI_T_USED_QUOTA_EST));
        if (val_out && val_size > 0) snprintf(val_out, val_size, ptc_ui_text(PTC_UI_T_D_MIN), summary->played_minutes);
        return;
    }
    if (summary->remaining_available && summary->remaining_minutes >= 0) {
        if (label_out && label_size > 0) snprintf(label_out, label_size, ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE));
        if (val_out && val_size > 0) snprintf(val_out, val_size, ptc_ui_text(PTC_UI_T_D_MIN), summary->remaining_minutes);
        return;
    }
    if (label_out && label_size > 0) snprintf(label_out, label_size, ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE));
    if (val_out && val_size > 0) snprintf(val_out, val_size, ptc_ui_text(PTC_UI_T_MINUTES_3));
}

uint64_t ptc_overlay_parent_skip_instance_id(const PtcCompanionResultSummary *summary)
{
    if (!summary || !summary->valid) return 0;
    if (summary->bedtime_active && !summary->bedtime_skipped)
        return summary->bedtime_window_instance_id;
    if (summary->bedtime_next_available)
        return summary->bedtime_next_window_instance_id;
    return 0;
}

const char *ptc_overlay_parent_action_unavailable_reason(
    const PtcCompanionResultSummary *summary, PtcOverlayParentAction action)
{
    if (action == PTC_OVERLAY_PARENT_RESTORE_SNAPSHOT) return NULL;
    if (!summary || !summary->valid) return ptc_ui_text(PTC_UI_T_PLEASE_REFRESH_THE_STATUS_FIRST);
    switch (action) {
    case PTC_OVERLAY_PARENT_ADD_MINUTES:
    case PTC_OVERLAY_PARENT_UNLIMITED:
        if (summary->dock_restriction_active) return ptc_ui_text(PTC_UI_T_DOCK_CONNECT);
        if (summary->eye_care_enabled && strcmp(summary->eye_care_phase, "resting") == 0)
            return ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING);
        if (summary->bedtime_active && !summary->bedtime_skipped)
            return ptc_ui_text(PTC_UI_T_PLEASE_DEAL_WITH_BEDTIME_RESTRICTIONS_FIRST);
        if (summary->unrestricted_today == 1 || summary->eye_care_unlimited_capped)
            return ptc_ui_text(PTC_UI_T_THERE_IS_NO_TIME_LIMIT_TODAY);
        return NULL;
    case PTC_OVERLAY_PARENT_SKIP_BEDTIME:
        return ptc_overlay_parent_skip_instance_id(summary) ? NULL : ptc_ui_text(PTC_UI_T_NO_SKIPPABLE_BEDTIME_WINDOW);
    case PTC_OVERLAY_PARENT_SKIP_EYE_CARE:
        if (!summary->eye_care_enabled)
            return ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_OFF);
        if (summary->bedtime_active && !summary->bedtime_skipped)
            return ptc_ui_text(PTC_UI_T_PLEASE_DEAL_WITH_BEDTIME_RESTRICTIONS_FIRST);
        if (strcmp(summary->eye_care_phase, "resting") != 0)
            return ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_NOT_RESTING);
        if (summary->eye_care_break_id == 0 ||
            (!summary->disable_flag_present && summary->eye_care_rest_remaining_seconds <= 0))
            return ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_REFRESH);
        return NULL;
    case PTC_OVERLAY_PARENT_WAIVE_DOCK:
        if (!summary->dock_available) return ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM);
        if (summary->dock_waived_today) return ptc_ui_text(PTC_UI_T_DOCK_WAIVED);
        return summary->force_docked || summary->undocked_limit_enabled ? NULL : ptc_ui_text(PTC_UI_T_DOCK_OFF);
    case PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP:
        return summary->bedtime_skipped_window_available ? NULL : ptc_ui_text(PTC_UI_T_THIS_BEDTIME_WAS_NOT_SKIPPED);
    case PTC_OVERLAY_PARENT_DISABLE_BEDTIME:
        return summary->bedtime_enabled ? NULL : ptc_ui_text(PTC_UI_T_BEDTIME_SCHEDULE_IS_NOT_ENABLED);
    default:
        return ptc_ui_text(PTC_UI_T_ACTION_NOT_AVAILABLE);
    }
}

void ptc_overlay_format_child_restriction_guidance(
    const PtcCompanionResultSummary *summary,
    char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!summary || !summary->valid) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_OBTAINING_QUOTA_AND_RESTRICTION_INFORMATION));
        return;
    }
    if (summary->bedtime_active && !summary->bedtime_skipped) {
        if (summary->bedtime_end_minute > 0) {
            snprintf(out, out_size, "%s (%02d:%02d)",
                ptc_ui_text(PTC_UI_T_BEDTIME_RESTRICTIONS_ARE_IN_EFFECT_PARENTS_PLEASE),
                summary->bedtime_end_minute / 60, summary->bedtime_end_minute % 60);
        } else {
            snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_BEDTIME_RESTRICTIONS_ARE_IN_EFFECT_PARENTS_PLEASE));
        }
        return;
    }
    if (summary->dock_restriction_active) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_DOCK_CONNECT));
        return;
    }
    if (summary->daily_restriction_active) {
        if (summary->daily_buffer_available) {
            snprintf(out, out_size, ptc_ui_text(PTC_UI_T_TODAY_QUOTA_EXHAUSTED_CLAIM_D_MIN_SELF),
                summary->daily_buffer_minutes);
        } else {
            snprintf(out, out_size, ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_HAS_BEEN_EXHAUSTED_PLEASE));
        }
        return;
    }
    if (summary->eye_care_enabled && strcmp(summary->eye_care_phase, "resting") == 0) {
        snprintf(out, out_size, "%s: %d %s", ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING),
            (summary->eye_care_rest_remaining_seconds + 59) / 60, ptc_ui_text(PTC_UI_T_MIN));
        return;
    }
    if (summary->eye_care_enabled && strcmp(summary->eye_care_phase, "playing") == 0) {
        int remaining = summary->eye_care_play_minutes - summary->eye_care_used_minutes;
        snprintf(out, out_size, "%s: %d %s", ptc_ui_text(PTC_UI_T_EYE_CARE),
            remaining > 0 ? remaining : 0, ptc_ui_text(PTC_UI_T_MIN));
        return;
    }
    if (summary->bedtime_skipped) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_THE_BEDTIME_LIMIT_HAS_BEEN_SKIPPED_TONIGHT));
        return;
    }
    if (summary->bedtime_next_available) {
        int start_h = summary->bedtime_next_start_minute / 60;
        int start_m = summary->bedtime_next_start_minute % 60;
        if (summary->bedtime_next_start_day_index == summary->day_index) {
            snprintf(out, out_size, ptc_ui_text(PTC_UI_T_RESTRICTION_STARTS_TONIGHT_AT_02D_02D), start_h, start_m);
        } else if (summary->bedtime_next_start_day_index == summary->day_index + 1) {
            snprintf(out, out_size, ptc_ui_text(PTC_UI_T_RESTRICTION_STARTS_TOMORROW_NIGHT_AT_02D_02D), start_h, start_m);
        } else {
            snprintf(out, out_size, ptc_ui_text(PTC_UI_T_NEXT_BEDTIME_02D_02D_SOFTWARE_PAUSES_THEN), start_h, start_m);
        }
        return;
    }
    if (summary->unrestricted_today == 1 || summary->eye_care_unlimited_capped) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_NO_TIME_LIMIT_TODAY_NO_BEDTIME_RESTRICTIONS));
        return;
    }
    if (summary->remaining_available) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_WILL_BE_SUSPENDED_AFTER_THE_QUOTA_IS));
        return;
    }
    snprintf(out, out_size, ptc_ui_text(PTC_UI_T_TIP_REMEMBER_TO_LOOK_OUT_THE_WINDOW));
}

void ptc_overlay_format_child_restriction_summary(
    const PtcCompanionResultSummary *summary,
    char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!summary || !summary->valid) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_COMMANDS_STATUS_CLICK_OR_PRESS_L_R));
        return;
    }
    if (summary->bedtime_active && !summary->bedtime_skipped) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_ALERT_BEDTIME_ACTIVE_PRESS_FOR_DETAILS));
        return;
    }
    if (summary->dock_restriction_active) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_DOCK_CONNECT));
        return;
    }
    if (summary->daily_restriction_active) {
        if (summary->daily_buffer_available) {
            snprintf(out, out_size, ptc_ui_text(PTC_UI_T_ALERT_DAILY_LIMIT_REACHED_BUFFER_READY_PRESS));
        } else {
            snprintf(out, out_size, ptc_ui_text(PTC_UI_T_ALERT_DAILY_LIMIT_REACHED_PRESS_FOR_DETAILS));
        }
        return;
    }
    if (summary->eye_care_enabled && strcmp(summary->eye_care_phase, "resting") == 0) {
        snprintf(out, out_size, "%s: %d %s", ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING),
            (summary->eye_care_rest_remaining_seconds + 59) / 60, ptc_ui_text(PTC_UI_T_MIN));
        return;
    }
    if (summary->bedtime_next_available && summary->bedtime_next_start_day_index == summary->day_index) {
        int start_h = summary->bedtime_next_start_minute / 60;
        int start_m = summary->bedtime_next_start_minute % 60;
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_REMINDER_RESTRICTION_STARTS_TONIGHT_AT_02D_02D), start_h, start_m);
        return;
    }
    if (summary->bedtime_skipped) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_ALERT_TONIGHT_S_BEDTIME_SKIPPED_PRESS));
        return;
    }
    if (summary->unrestricted_today == 1 || summary->eye_care_unlimited_capped) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_RULES_NO_LIMIT_TODAY_NO_BEDTIME_PRESS));
        return;
    }
    if (summary->remaining_available && summary->played_minutes_available &&
        summary->remaining_minutes >= 0 && summary->played_minutes >= 0) {
        int total = summary->remaining_minutes + summary->played_minutes;
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_RULE_DETAILS_TODAY_QUOTA_D_MIN_PRESS), total);
        return;
    }
    snprintf(out, out_size, ptc_ui_text(PTC_UI_T_RULES_STATUS_DETAILS_PRESS_L_R));
}

void ptc_overlay_format_child_restriction_detail(
    const PtcCompanionResultSummary *summary,
    char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!summary || !summary->valid) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_RULES_PENDING_CONFIRMATION));
        return;
    }
    if (summary->bedtime_active && !summary->bedtime_skipped) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_PLAY_RESTRICTED_EVEN_IF_LIMIT));
        return;
    }
    if (summary->dock_restriction_active) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_DOCK_CONNECT));
        return;
    }
    if (summary->daily_restriction_active) {
        if (summary->bedtime_next_available && summary->bedtime_next_start_day_index == summary->day_index) {
            snprintf(out, out_size, ptc_ui_text(PTC_UI_T_QUOTA_EXHAUSTED_RESTRICTION_STARTS_TONIGHT_AT_02D),
                summary->bedtime_next_start_minute / 60, summary->bedtime_next_start_minute % 60);
        } else {
            snprintf(out, out_size, ptc_ui_text(PTC_UI_T_DAILY_LIMIT_REACHED_ENTER_GRANT_CODE_TO));
        }
        return;
    }
    if (summary->eye_care_enabled && strcmp(summary->eye_care_phase, "resting") == 0) {
        snprintf(out, out_size, "%s: %d %s", ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING),
            (summary->eye_care_rest_remaining_seconds + 59) / 60, ptc_ui_text(PTC_UI_T_MIN));
        return;
    }
    if (summary->bedtime_skipped) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_TONIGHT_S_BEDTIME_SKIPPED_GAME_SUSPENDS_AFTER));
        return;
    }
    if (summary->bedtime_next_available) {
        const char *prefix = summary->bedtime_next_start_day_index == summary->day_index ? ptc_ui_text(PTC_UI_T_TONIGHT) :
            (summary->bedtime_next_start_day_index == summary->day_index + 1 ? ptc_ui_text(PTC_UI_T_TOMORROW_NIGHT) : ptc_ui_text(PTC_UI_T_NEXT_TIME));
        int start_h = summary->bedtime_next_start_minute / 60;
        int start_m = summary->bedtime_next_start_minute % 60;
        if (summary->unrestricted_today == 1 || summary->eye_care_unlimited_capped) {
            snprintf(out, out_size, ptc_ui_text(PTC_UI_T_TODAY_UNLIMITED_RESTRICTION_STARTS_S_02D_02D),
                prefix, start_h, start_m);
        } else {
            snprintf(out, out_size, ptc_ui_text(PTC_UI_T_RESTRICTION_AFTER_QUOTA_STARTS_S_02D_02D),
                prefix, start_h, start_m);
        }
        return;
    }
    if (summary->unrestricted_today == 1 || summary->eye_care_unlimited_capped) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_NO_DAILY_LIMIT_TODAY_BEDTIME_SCHEDULE_DISABLED));
    } else {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_GAME_SUSPENDS_AFTER_DAILY_LIMIT_BEDTIME_SCHEDULE));
    }
}

void ptc_overlay_format_child_buffer_status(
    const PtcCompanionResultSummary *summary,
    char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!summary || !summary->valid) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        return;
    }
    if (summary->daily_buffer_available) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_D_MIN_AVAILABLE_EMERGENCY), summary->daily_buffer_minutes);
    } else if (summary->daily_buffer_claimed) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_USED_TODAY_RESETS_TOMORROW));
    } else if (summary->daily_buffer_minutes == 0) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_DISABLED_TODAY));
    } else {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_CURRENTLY_UNAVAILABLE));
    }
}


void ptc_overlay_format_dock_usage(const PtcCompanionResultSummary *summary,
    char *out, size_t out_size)
{
    char usage[160];
    const char *mode;
    if (!out || !out_size) return;
    if (!summary || !summary->dock_available) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
        return;
    }
    mode = strcmp(summary->operation_mode, "docked") == 0 ? ptc_ui_text(PTC_UI_T_DOCK_TV) :
        strcmp(summary->operation_mode, "undocked") == 0 ? ptc_ui_text(PTC_UI_T_DOCK_HANDHELD) :
        ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM);
    PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("used", summary->undocked_used_minutes),
        PTC_UI_TEXT_NUMBER("remaining", summary->undocked_remaining_minutes)};
    if (summary->dock_waived_today)
        snprintf(usage, sizeof(usage), "%s", ptc_ui_text(PTC_UI_T_DOCK_WAIVED));
    else if (summary->dock_restriction_active)
        snprintf(usage, sizeof(usage), "%s", ptc_ui_text(PTC_UI_T_DOCK_BLOCKED_BANNER));
    else if (!(summary->force_docked || summary->undocked_limit_enabled))
        snprintf(usage, sizeof(usage), "%s", ptc_ui_text(PTC_UI_T_DOCK_OFF));
    else if (!summary->undocked_usage_available)
        snprintf(usage, sizeof(usage), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    else (void)ptc_ui_text_format(summary->undocked_limit_enabled ? PTC_UI_T_DOCK_USAGE_NAMED :
        PTC_UI_T_DOCK_USAGE_ONLY_NAMED, usage, sizeof(usage), args, 2);
    snprintf(out, out_size, "%s / %s", mode, usage);
}
