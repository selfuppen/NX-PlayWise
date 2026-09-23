#include "bridge.h"

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
    if (!bridge) return "请求参数无效";
    if (bridge->summary.valid) {
        if (!bridge->summary.ok) {
            if (bridge->summary.message[0]) return bridge->summary.message;
            if (bridge->summary.error_code > 0)
                return ptc_error_message_zh((PtcErrorCode)bridge->summary.error_code);
            return "请求失败，请稍后重试";
        }
        return "请求成功";
    }
    switch (bridge->last_status) {
    case PTC_COMPANION_TIMEOUT: return "后台响应超时，请重试";
    case PTC_COMPANION_WRITE_FAILED:
    case PTC_COMPANION_RENAME_FAILED: return "请求写入失败，请检查 SD 卡";
    case PTC_COMPANION_RESULT_INVALID:
    case PTC_COMPANION_RESULT_MISMATCH: return "后台返回的结果无效";
    case PTC_COMPANION_BAD_ARGUMENT: return "请求被后台拒绝";
    case PTC_COMPANION_QUIESCING: return "后台正在安全切换，请稍后重试";
    default: return "无法连接后台服务，请重试";
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
    if (!source || !source[0]) return "常规计划";
    if (strcmp(source, "today_override") == 0) return "今日调整";
    if (strcmp(source, "scheduled_override") == 0) return "临时计划";
    if (strcmp(source, "statutory_holiday") == 0) return "法定假日";
    if (strcmp(source, "makeup_workday") == 0) return "调休工作日";
    if (strcmp(source, "weekly") == 0) return "周计划";
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
        if (label_out && label_size > 0) snprintf(label_out, label_size, "今日总额度");
        if (val_out && val_size > 0) snprintf(val_out, val_size, "-- 分钟");
        return;
    }
    if (summary->unrestricted_today == 1) {
        if (label_out && label_size > 0) snprintf(label_out, label_size, "今日总额度");
        if (val_out && val_size > 0) snprintf(val_out, val_size, "不限时");
        if (note_out && note_size > 0 && summary->played_minutes_available && summary->played_minutes >= 0) {
            snprintf(note_out, note_size, "(已玩约 %d 分)", summary->played_minutes);
        }
        return;
    }
    if (summary->remaining_available && summary->played_minutes_available &&
        summary->remaining_minutes >= 0 && summary->played_minutes >= 0) {
        int total = summary->remaining_minutes + summary->played_minutes;
        if (label_out && label_size > 0) snprintf(label_out, label_size, "今日总额度");
        if (val_out && val_size > 0) snprintf(val_out, val_size, "%d 分钟", total);
        if (note_out && note_size > 0) {
            snprintf(note_out, note_size, "(已玩约 %d 分)", summary->played_minutes);
        }
        return;
    }
    if (summary->played_minutes_available && summary->played_minutes >= 0) {
        if (label_out && label_size > 0) snprintf(label_out, label_size, "额度已耗(估算)");
        if (val_out && val_size > 0) snprintf(val_out, val_size, "%d 分钟", summary->played_minutes);
        return;
    }
    if (summary->remaining_available && summary->remaining_minutes >= 0) {
        if (label_out && label_size > 0) snprintf(label_out, label_size, "今日总额度");
        if (val_out && val_size > 0) snprintf(val_out, val_size, "%d 分钟", summary->remaining_minutes);
        return;
    }
    if (label_out && label_size > 0) snprintf(label_out, label_size, "今日总额度");
    if (val_out && val_size > 0) snprintf(val_out, val_size, "-- 分钟");
}

void ptc_overlay_format_child_restriction_guidance(
    const PtcCompanionResultSummary *summary,
    char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!summary || !summary->valid) {
        snprintf(out, out_size, "正在获取额度与受限信息...");
        return;
    }
    if (summary->bedtime_active && !summary->bedtime_skipped) {
        snprintf(out, out_size, "🌙 就寝限制生效中（游戏已暂停）");
        return;
    }
    if (summary->daily_restriction_active ||
        (summary->remaining_available && summary->remaining_minutes == 0)) {
        if (summary->daily_buffer_available) {
            snprintf(out, out_size, "⚠️ 今日额度已耗尽 ｜ 可领 %d 分钟自主缓冲",
                summary->daily_buffer_minutes);
        } else {
            snprintf(out, out_size, "⚠️ 今日额度已耗尽 ｜ 请输入加时码继续游玩");
        }
        return;
    }
    if (summary->bedtime_skipped) {
        snprintf(out, out_size, "🌙 今晚就寝限制已跳过 ｜ 额度用完后将暂停游戏");
        return;
    }
    if (summary->bedtime_next_available) {
        int start_h = summary->bedtime_next_start_minute / 60;
        int start_m = summary->bedtime_next_start_minute % 60;
        if (summary->bedtime_next_start_day_index == summary->day_index) {
            snprintf(out, out_size, "🌙 今晚 %02d:%02d 就寝立断（到点强制暂停游戏）", start_h, start_m);
        } else if (summary->bedtime_next_start_day_index == summary->day_index + 1) {
            snprintf(out, out_size, "🌙 明晚 %02d:%02d 就寝立断（到点强制暂停游戏）", start_h, start_m);
        } else {
            snprintf(out, out_size, "🌙 下次就寝 %02d:%02d（到点强制暂停游戏）", start_h, start_m);
        }
        return;
    }
    if (summary->unrestricted_today == 1) {
        snprintf(out, out_size, "🌟 今日不限时，未设就寝限制，玩耍不受限制");
        return;
    }
    if (summary->remaining_available) {
        snprintf(out, out_size, "⏱️ 额度玩完后将暂停 ｜ 今日未设就寝限制");
        return;
    }
    snprintf(out, out_size, "提示：加时前记得向窗外远眺 5 分钟！");
}

void ptc_overlay_format_child_restriction_summary(
    const PtcCompanionResultSummary *summary,
    char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!summary || !summary->valid) {
        snprintf(out, out_size, "[-] 命令与状态（点击或按 - / L / R 展开）");
        return;
    }
    if (summary->bedtime_active && !summary->bedtime_skipped) {
        snprintf(out, out_size, "[-] 限制提醒：就寝限制生效中（按 - 展开详情）");
        return;
    }
    if (summary->daily_restriction_active ||
        (summary->remaining_available && summary->remaining_minutes == 0)) {
        if (summary->daily_buffer_available) {
            snprintf(out, out_size, "[-] 限制提醒：额度已耗尽，可领缓冲（按 - 展开详情）");
        } else {
            snprintf(out, out_size, "[-] 限制提醒：今日额度已耗尽（按 - 展开详情）");
        }
        return;
    }
    if (summary->bedtime_next_available && summary->bedtime_next_start_day_index == summary->day_index) {
        int start_h = summary->bedtime_next_start_minute / 60;
        int start_m = summary->bedtime_next_start_minute % 60;
        snprintf(out, out_size, "[-] 限制提醒：今晚 %02d:%02d 就寝立断（按 - 展开详情）", start_h, start_m);
        return;
    }
    if (summary->bedtime_skipped) {
        snprintf(out, out_size, "[-] 限制提醒：今晚就寝已跳过（按 - 展开详情）");
        return;
    }
    if (summary->unrestricted_today == 1) {
        snprintf(out, out_size, "[-] 规则详情：今日不限时，无就寝限制（按 - 展开）");
        return;
    }
    if (summary->remaining_available && summary->played_minutes_available &&
        summary->remaining_minutes >= 0 && summary->played_minutes >= 0) {
        int total = summary->remaining_minutes + summary->played_minutes;
        snprintf(out, out_size, "[-] 规则详情：今日总额度 %d 分钟（按 - 展开详情）", total);
        return;
    }
    snprintf(out, out_size, "[-] 规则与状态详情（按 - / L / R 展开）");
}

void ptc_overlay_format_child_restriction_detail(
    const PtcCompanionResultSummary *summary,
    char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!summary || !summary->valid) {
        snprintf(out, out_size, "规则状态待确认");
        return;
    }
    if (summary->bedtime_active && !summary->bedtime_skipped) {
        snprintf(out, out_size, "就寝限制生效中（游戏已暂停，独立于额度立断锁定）");
        return;
    }
    if (summary->daily_restriction_active ||
        (summary->remaining_available && summary->remaining_minutes == 0)) {
        if (summary->bedtime_next_available && summary->bedtime_next_start_day_index == summary->day_index) {
            snprintf(out, out_size, "额度已用尽 ｜ 今晚 %02d:%02d 就寝立断",
                summary->bedtime_next_start_minute / 60, summary->bedtime_next_start_minute % 60);
        } else {
            snprintf(out, out_size, "今日额度已耗尽暂停 ｜ 可输入加时码继续游玩");
        }
        return;
    }
    if (summary->bedtime_skipped) {
        snprintf(out, out_size, "今晚就寝已跳过 ｜ 额度玩完后将暂停游戏");
        return;
    }
    if (summary->bedtime_next_available) {
        const char *prefix = summary->bedtime_next_start_day_index == summary->day_index ? "今晚" :
            (summary->bedtime_next_start_day_index == summary->day_index + 1 ? "明晚" : "下次");
        int start_h = summary->bedtime_next_start_minute / 60;
        int start_m = summary->bedtime_next_start_minute % 60;
        if (summary->unrestricted_today == 1) {
            snprintf(out, out_size, "今日额度不限时 ｜ %s %02d:%02d 就寝立断暂停",
                prefix, start_h, start_m);
        } else {
            snprintf(out, out_size, "额度玩完后暂停 ｜ %s %02d:%02d 就寝立断暂停",
                prefix, start_h, start_m);
        }
        return;
    }
    if (summary->unrestricted_today == 1) {
        snprintf(out, out_size, "今日额度不限时 ｜ 未开启就寝限制");
    } else {
        snprintf(out, out_size, "额度玩完后暂停 ｜ 未开启就寝限制");
    }
}

void ptc_overlay_format_child_buffer_status(
    const PtcCompanionResultSummary *summary,
    char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!summary || !summary->valid) {
        snprintf(out, out_size, "暂不可用");
        return;
    }
    if (summary->daily_buffer_available) {
        snprintf(out, out_size, "+%d 分钟可用（应急）", summary->daily_buffer_minutes);
    } else if (summary->daily_buffer_claimed) {
        snprintf(out, out_size, "今日已使用（明日恢复）");
    } else if (summary->daily_buffer_minutes == 0) {
        snprintf(out, out_size, "今日未开启");
    } else {
        snprintf(out, out_size, "暂不可领取");
    }
}

