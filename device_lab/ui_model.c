#include "ui_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *text)
{
    while (text && (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')) ++text;
    return text;
}

static const char *find_value(const char *text, const char *key)
{
    char pattern[80];
    const char *match;
    if (!text || !key || snprintf(pattern, sizeof(pattern), "\"%s\"", key) >= (int)sizeof(pattern)) return NULL;
    match = text;
    while ((match = strstr(match, pattern)) != NULL) {
        const char *colon = skip_ws(match + strlen(pattern));
        if (*colon == ':') return skip_ws(colon + 1);
        match += strlen(pattern);
    }
    return NULL;
}

bool ptc_lab_json_string(const char *text, const char *key, char *out, size_t out_size)
{
    const char *value = find_value(text, key);
    const char *end;
    if (!value || !out || out_size == 0 || *value++ != '"') return false;
    end = strchr(value, '"');
    if (!end || (size_t)(end - value) >= out_size) return false;
    memcpy(out, value, (size_t)(end - value));
    out[end - value] = '\0';
    return true;
}

static bool json_int(const char *text, const char *key, int64_t *out)
{
    const char *value = find_value(text, key);
    char *end;
    long long parsed;
    if (!value || !out) return false;
    parsed = strtoll(value, &end, 10);
    if (end == value) return false;
    *out = (int64_t)parsed;
    return true;
}

static bool json_bool(const char *text, const char *key, bool *out)
{
    const char *value = find_value(text, key);
    if (!value || !out) return false;
    if (strncmp(value, "true", 4) == 0) *out = true;
    else if (strncmp(value, "false", 5) == 0) *out = false;
    else return false;
    return true;
}

bool ptc_lab_session_parse(const char *text, PtcLabSessionView *view)
{
    PtcLabSessionView parsed;
    int64_t next_phase;
    if (!text || !view) return false;
    memset(&parsed, 0, sizeof(parsed));
    snprintf(parsed.mode, sizeof(parsed.mode), "full");
    if (!ptc_lab_json_string(text, "run_id", parsed.run_id, sizeof(parsed.run_id)) ||
        !ptc_lab_json_string(text, "state", parsed.state, sizeof(parsed.state)) ||
        !ptc_lab_json_string(text, "active_phase", parsed.active_phase, sizeof(parsed.active_phase)) ||
        !ptc_lab_json_string(text, "restore_verdict", parsed.restore_verdict, sizeof(parsed.restore_verdict)) ||
        !json_int(text, "next_phase", &next_phase) ||
        !json_int(text, "deadline", &parsed.deadline) ||
        !json_bool(text, "restored", &parsed.restored)) return false;
    (void)ptc_lab_json_string(text, "mode", parsed.mode, sizeof(parsed.mode));
    (void)json_bool(text, "baseline_all_zero", &parsed.baseline_all_zero);
    parsed.required_phases = strcmp(parsed.mode, "restriction_quick") == 0 ? 1 :
        (strcmp(parsed.mode, "timer_activation_ab") == 0 ? 8 : 6);
    if (next_phase < 0 || next_phase > parsed.required_phases) return false;
    parsed.next_phase = (int)next_phase;
    snprintf(parsed.last_verdict, sizeof(parsed.last_verdict), "pending");
    *view = parsed;
    return true;
}

bool ptc_lab_result_error(const char *text, int *error_code, char *reason, size_t reason_size)
{
    int64_t code;
    char status[16];
    if (!text || !ptc_lab_json_string(text, "status", status, sizeof(status)) || strcmp(status, "error") != 0 ||
        !json_int(text, "code", &code)) return false;
    if (error_code) *error_code = (int)code;
    if (reason && reason_size && !ptc_lab_json_string(text, "reason", reason, reason_size)) reason[0] = '\0';
    return true;
}

PtcLabNroStage ptc_lab_nro_stage(const PtcLabBootStatus *boot, bool lab_service_running,
    PtcLabSessionLoadStatus session_status, const PtcLabSessionView *session)
{
    if (!boot) return PTC_LAB_NRO_CONFLICT;
    if (boot->state == PTC_LAB_BOOT_CONFLICT) return PTC_LAB_NRO_CONFLICT;
    if (boot->state == PTC_LAB_BOOT_RECOVERY_REQUIRED) return PTC_LAB_NRO_RECOVER_FLAGS;
    if (boot->state == PTC_LAB_BOOT_NORMAL) return PTC_LAB_NRO_PREPARE;
    if (boot->state == PTC_LAB_BOOT_RESTORED)
        return lab_service_running ? PTC_LAB_NRO_REBOOT_TO_NORMAL : PTC_LAB_NRO_PREPARE;
    if (session_status == PTC_LAB_SESSION_INVALID) return PTC_LAB_NRO_SESSION_INVALID;
    if (!lab_service_running) return PTC_LAB_NRO_REBOOT_TO_LAB;
    if (session_status == PTC_LAB_SESSION_MISSING || !session) return PTC_LAB_NRO_START_OVERLAY;
    if (strcmp(session->state, "restore_required") == 0 || strcmp(session->state, "error") == 0 ||
        (strcmp(session->state, "complete") == 0 && !session->restored)) return PTC_LAB_NRO_RESTORE_PCTL;
    if (strcmp(session->state, "complete") == 0 && session->restored &&
        strcmp(session->restore_verdict, "exact_restore_proved") == 0) return PTC_LAB_NRO_RESTORE_NORMAL;
    return PTC_LAB_NRO_RESUME_OVERLAY;
}

const char *ptc_lab_nro_stage_title_zh(PtcLabNroStage stage)
{
    switch (stage) {
    case PTC_LAB_NRO_PREPARE: return "准备启用实验后台";
    case PTC_LAB_NRO_REBOOT_TO_LAB: return "需要重启主机";
    case PTC_LAB_NRO_START_OVERLAY: return "可以开始真机取证";
    case PTC_LAB_NRO_RESUME_OVERLAY: return "取证会话尚未完成";
    case PTC_LAB_NRO_RESTORE_PCTL: return "必须先恢复 PCTL 原设置";
    case PTC_LAB_NRO_RESTORE_NORMAL: return "取证完成，可以恢复正常后台";
    case PTC_LAB_NRO_RECOVER_FLAGS: return "启动切换曾被中断";
    case PTC_LAB_NRO_SESSION_INVALID: return "取证会话文件损坏";
    case PTC_LAB_NRO_REBOOT_TO_NORMAL: return "正常后台已恢复，需要重启";
    case PTC_LAB_NRO_CONFLICT: default: return "检测到启动文件冲突";
    }
}

const char *ptc_lab_nro_stage_body_zh(PtcLabNroStage stage)
{
    switch (stage) {
    case PTC_LAB_NRO_PREPARE: return "启用后会暂时停用正常后台，并在下次重启运行隔离的实验后台。标准数据和二进制不会被修改。";
    case PTC_LAB_NRO_REBOOT_TO_LAB: return "实验后台启动标志已经准备完成。请退出本程序并完整重启主机，然后从 Tesla 或 Ultrahand 打开 Device Lab 浮窗。";
    case PTC_LAB_NRO_START_OVERLAY: return "实验后台正在运行。请打开 Device Lab 浮窗，选择聚焦限制复测、Timer 激活 A/B 或高级完整取证。";
    case PTC_LAB_NRO_RESUME_OVERLAY: return "会话进度已经保存。重新打开 Device Lab 浮窗即可从当前阶段继续。";
    case PTC_LAB_NRO_RESTORE_PCTL: return "当前尚未证明 PCTL 原设置已精确恢复。程序不会切换启动标志，请先执行立即恢复。";
    case PTC_LAB_NRO_RESTORE_NORMAL: return "PCTL 原设置已精确恢复。完整会话会生成正式报告；提前停止的会话只保留草稿。现在可以安全切回正常 PlayWise 后台。";
    case PTC_LAB_NRO_RECOVER_FLAGS: return "上次启动切换没有完整结束。只允许按事务记录恢复，不会覆盖未知文件。";
    case PTC_LAB_NRO_SESSION_INVALID: return "无法可信读取 session.json。为避免覆盖取证现场，已禁止开始新会话或切换启动标志。";
    case PTC_LAB_NRO_REBOOT_TO_NORMAL: return "启动标志已经精确恢复。请退出本程序并完整重启主机，重启后标准 PlayWise 后台将恢复运行。";
    case PTC_LAB_NRO_CONFLICT: default: return "发现未知标志、备份或不一致的事务记录。程序不会自动覆盖这些文件。";
    }
}

const char *ptc_lab_nro_stage_action_zh(PtcLabNroStage stage)
{
    switch (stage) {
    case PTC_LAB_NRO_PREPARE: return "启用实验后台";
    case PTC_LAB_NRO_RESTORE_PCTL: return "立即恢复 PCTL 原设置";
    case PTC_LAB_NRO_RESTORE_NORMAL:
    case PTC_LAB_NRO_RECOVER_FLAGS: return "恢复正常后台";
    case PTC_LAB_NRO_REBOOT_TO_LAB: return "我知道了，退出程序";
    case PTC_LAB_NRO_REBOOT_TO_NORMAL: return "完成并退出，准备重启";
    case PTC_LAB_NRO_START_OVERLAY:
    case PTC_LAB_NRO_RESUME_OVERLAY: return "我知道了，返回 HOME";
    case PTC_LAB_NRO_SESSION_INVALID:
    case PTC_LAB_NRO_CONFLICT:
    default: return "安全退出";
    }
}

const char *ptc_lab_session_state_zh(const char *state)
{
    if (!state || !state[0] || strcmp(state, "not_started") == 0) return "尚未开始";
    if (strcmp(state, "ready") == 0) return "等待开始下一阶段";
    if (strcmp(state, "sampling") == 0) return "正在自动采样";
    if (strcmp(state, "awaiting_observation") == 0) return "等待人工观察";
    if (strcmp(state, "complete") == 0) return "取证完成";
    if (strcmp(state, "restore_required") == 0) return "必须立即恢复";
    if (strcmp(state, "error") == 0) return "取证阶段异常";
    return "未知状态";
}

const char *ptc_lab_phase_title_zh(int phase)
{
    static const char *const labels[] = {
        "HOME 菜单：计时保持停止", "HOME 菜单：主动启动计时", "游戏保持前台运行",
        "游戏挂起并返回 HOME", "主机待机并再次唤醒", "观察时间限制效果"
    };
    return phase >= 0 && phase < 6 ? labels[phase] : "所有阶段已完成";
}

const char *ptc_lab_phase_title_for_mode_zh(const char *mode, int phase)
{
    static const char *const activation[] = {
        "HOME 亮屏自然计时", "待机与唤醒", "限时设置：不调用 1451",
        "耗尽设置：不调用 1451", "加时设置：不调用 1451",
        "再次耗尽：准备不限时 A/B", "今日不限时：不调用 1451",
        "按需执行 1451 fallback"
    };
    if (mode && strcmp(mode, "timer_activation_ab") == 0)
        return phase >= 0 && phase < 8 ? activation[phase] : "所有阶段已完成";
    return ptc_lab_phase_title_zh(phase);
}

const char *ptc_lab_phase_instruction_zh(int phase)
{
    static const char *const labels[] = {
        "停留在 HOME 菜单，不要打开游戏。准备好后开始 75 秒自动采样。",
        "继续停留在 HOME 菜单。Lab 会主动启动计时并采样 75 秒。",
        "打开一个无重要进度的游戏，保持游戏在前台，再重新打开本浮窗。",
        "让刚才的游戏保持挂起并返回 HOME 菜单，然后重新打开本浮窗。",
        "开始后关闭浮窗，让主机待机一次并唤醒，再重新打开本浮窗。",
        "确认游戏没有未保存进度。长按 A 两秒后观察约 15 秒内是否出现限制。"
    };
    return phase >= 0 && phase < 6 ? labels[phase] : "报告已生成，请返回 NRO 恢复正常后台。";
}

const char *ptc_lab_phase_instruction_for_mode_zh(const char *mode, int phase)
{
    static const char *const activation[] = {
        "停留在 HOME 菜单并保持亮屏，采样 75 秒自然计时行为。",
        "开始后关闭浮窗，让主机待机并唤醒，再等待本阶段结束。",
        "Lab 只写入安全的 1440 分钟限时设置，不主动启动 timer。",
        "Lab 写入当日额度耗尽但不启动 timer；可能出现系统限制提示。",
        "Lab 从耗尽状态写入 1440 分钟加时目标，不主动启动 timer。",
        "Lab 再次写入耗尽状态，为今日不限时 A/B 准备现场。",
        "Lab 写入今日不限时，不主动启动 timer，并观察限制是否解除。",
        "仅当前述 settings-only 证据不足时调用一次 1451，随后精确恢复。"
    };
    if (mode && strcmp(mode, "timer_activation_ab") == 0)
        return phase >= 0 && phase < 8 ? activation[phase] : "报告已生成，请返回 NRO 恢复正常后台。";
    return ptc_lab_phase_instruction_zh(phase);
}

const char *ptc_lab_verdict_zh(const char *verdict)
{
    if (!verdict || !verdict[0] || strcmp(verdict, "pending") == 0) return "等待本阶段证据";
    if (strcmp(verdict, "evidence_recorded") == 0) return "证据已记录，等待人工评审";
    if (strcmp(verdict, "unsafe_for_home_start") == 0) return "旧版结论：HOME 会计时；需按主机使用语义重评";
    if (strcmp(verdict, "home_usage_counted") == 0) return "HOME 亮屏使用会消耗主机额度";
    if (strcmp(verdict, "home_usage_not_counted") == 0) return "本阶段未观察到 HOME 使用计时";
    if (strcmp(verdict, "sleep_exclusion_observed") == 0) return "本阶段未观察到待机消耗";
    if (strcmp(verdict, "sleep_usage_observed") == 0) return "待机阶段出现额度变化，需人工复核";
    if (strcmp(verdict, "settings_only_timer_started") == 0) return "只写限时设置后 timer 已运行";
    if (strcmp(verdict, "settings_only_timer_not_started") == 0) return "只写限时设置后 timer 未运行";
    if (strcmp(verdict, "grant_settings_only_cleared") == 0) return "只写加时设置已解除瞬时限制";
    if (strcmp(verdict, "grant_settings_only_not_cleared") == 0) return "只写加时设置未解除瞬时限制";
    if (strcmp(verdict, "unlimited_settings_only_cleared") == 0) return "只写不限时设置已解除瞬时限制";
    if (strcmp(verdict, "unlimited_settings_only_not_cleared") == 0) return "只写不限时设置未解除瞬时限制";
    if (strcmp(verdict, "start_fallback_executed") == 0) return "已执行一次 1451 fallback";
    if (strcmp(verdict, "start_fallback_not_required") == 0) return "本轮不需要 1451 fallback";
    if (strcmp(verdict, "stopped_timer_stable") == 0) return "计时停止时额度保持稳定";
    if (strcmp(verdict, "restriction_ipc_observed") == 0) return "IPC 已观察到限制状态";
    if (strcmp(verdict, "precondition_not_met") == 0) return "全零基线不足以证明游戏生命周期";
    if (strcmp(verdict, "exact_restore_proved") == 0) return "已证明原设置精确恢复";
    if (strcmp(verdict, "restore_not_proved") == 0) return "尚未证明原设置恢复";
    return "证据已保存，查看技术详情";
}

const char *ptc_lab_mode_zh(const char *mode)
{
    if (mode && strcmp(mode, "restriction_quick") == 0) return "聚焦限制复测";
    if (mode && strcmp(mode, "timer_activation_ab") == 0) return "Timer 激活 A/B";
    return "高级完整取证";
}

const char *ptc_lab_transport_error_zh(int status)
{
    switch (status) {
    case 2: return "实验后台响应超时。请确认已经重启并启用实验后台，然后重试。";
    case 3: return "请求参数无效。请保留现场并重新打开浮窗。";
    case 4:
    case 5: return "无法把请求安全写入 SD 卡。请检查 SD 卡后重试。";
    case 6:
    case 7: return "后台返回结果损坏或与本次请求不匹配。请勿继续下一阶段。";
    default: return "无法连接实验后台。请确认已重启主机，并检查 Device Lab 安装是否完整。";
    }
}

PtcLabUiRect ptc_lab_nro_action_rect(int index)
{
    PtcLabUiRect rect = {72, 402 + index * 88, 760, 72};
    return rect;
}

bool ptc_lab_ui_rect_contains(PtcLabUiRect rect, int x, int y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

int ptc_lab_nro_hit_test(int x, int y)
{
    int index;
    for (index = 0; index < 3; ++index) {
        if (ptc_lab_ui_rect_contains(ptc_lab_nro_action_rect(index), x, y)) return index;
    }
    return -1;
}
