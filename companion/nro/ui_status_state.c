#include "ui_state.h"
#include "ui_layout.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../common/protocol/error_code.h"
#include "../../common/rules/holiday_calendar.h"
#include "../../common/time/ptc_time.h"

int ptc_ui_preview_remaining_minutes(const PtcUiModel *model)
{
    int remaining;
    if (!model) {
        return 0;
    }

    if (model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        if (model->remaining_available && model->remaining_minutes >= 0) {
            return model->remaining_minutes + (int)model->draft_minutes;
        }
        return -1;
    }

    if (!model->played_minutes_available || model->played_minutes < 0) {
        return -1;
    }
    remaining = (int)model->draft_minutes;
    if (model->played_minutes_available && model->played_minutes >= 0) {
        remaining = (int)model->draft_minutes - model->played_minutes;
        if (remaining < 0) {
            remaining = 0;
        }
    }
    return remaining;
}

void ptc_ui_mark_status_updated(PtcUiModel *model, int64_t now)
{
    if (!model) {
        return;
    }
    model->status_updated_at = now > 0 ? now : 0;
}

int64_t ptc_ui_status_age_seconds(const PtcUiModel *model, int64_t now)
{
    if (!model || !model->status_loaded || model->status_updated_at <= 0) {
        return -1;
    }
    if (now < model->status_updated_at) return -1;
    if (now == model->status_updated_at) {
        return 0;
    }
    return now - model->status_updated_at;
}

bool ptc_ui_status_is_fresh(const PtcUiModel *model, int64_t now)
{
    int64_t age = ptc_ui_status_age_seconds(model, now);
    return model && age >= 0 && age <= 120 && model->error_code == 0 &&
        strcmp(model->result_status, "error") != 0;
}

bool ptc_ui_parent_status_alert_visible(const PtcUiModel *model)
{
    if (!model) return false;
    return strcmp(model->setup_phase, "protection") == 0 ||
        strcmp(model->setup_phase, "failed") == 0 || model->recovery_active ||
        model->disable_flag_present ||
        (model->temporary_unlocked_available && model->temporary_unlocked);
}

bool ptc_ui_operation_feedback_visible(const PtcUiModel *model)
{
    if (!model) return false;
    if (model->waiting || strcmp(model->result_status, "error") == 0 ||
        model->feedback_detail[0]) return true;
    return strcmp(model->result_status, "ok") == 0 && model->message[0] &&
        model->command_name[0] &&
        strcmp(model->command_name, "刷新状态") != 0 &&
        strcmp(model->command_name, "未开始") != 0;
}

const char *ptc_ui_runtime_notice_summary(const PtcUiModel *model)
{
    if (!model) return "";
    if (model->disable_flag_present) return "控制已停用，请家长到支持与恢复处理";
    if (model->recovery_active) return "正在恢复设置，请等待恢复完成";
    if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0)
        return "需要家长处理，请进入支持与恢复";
    if (model->restriction_enabled_available && !model->restriction_enabled)
        return "Nintendo 家长控制未启用，请家长检查系统设置";
    if (model->temporary_unlocked_available && model->temporary_unlocked)
        return "临时解除期间不计时，进入睡眠后恢复今日限制";
    if (model->apply_pending_confirmation) return "设置等待确认生效，请稍候";
    if (model->restricted_now == 1) return "已进入时间限制，可兑换加时码或请家长调整额度";
    if (model->remaining_available && model->remaining_minutes == 0 && model->unrestricted_today != 1)
        return "额度已用完，限制可能即将生效，可兑换加时码";
    return "";
}

void ptc_ui_project_notice(const PtcUiModel *model, PtcUiNoticeProjection *out)
{
    const char *runtime;
    bool error;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->level = PTC_UI_NOTICE_SUCCESS;
    if (!model || model->view != PTC_UI_PARENT) return;

    runtime = ptc_ui_runtime_notice_summary(model);
    error = strcmp(model->result_status, "error") == 0;
    out->visible = runtime[0] || ptc_ui_operation_feedback_visible(model) ||
        ptc_ui_home_notice_expanded(model);
    if (!out->visible) return;

    if (runtime[0]) snprintf(out->summary, sizeof(out->summary), "%s", runtime);
    else if (model->message[0]) snprintf(out->summary, sizeof(out->summary), "%s", model->message);
    else if (error) snprintf(out->summary, sizeof(out->summary), "操作未完成");
    else if (model->waiting) snprintf(out->summary, sizeof(out->summary), "正在同步，请稍候");
    else snprintf(out->summary, sizeof(out->summary), "状态已更新");

    if (error || model->disable_flag_present || model->restricted_now == 1 ||
        (model->remaining_available && model->remaining_minutes == 0 && model->unrestricted_today != 1) ||
        strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) {
        out->level = PTC_UI_NOTICE_DANGER;
    } else if (model->waiting || runtime[0]) {
        out->level = PTC_UI_NOTICE_WARNING;
    }

    if (model->feedback_detail[0]) {
        snprintf(out->details, sizeof(out->details), "%s", model->feedback_detail);
    } else if (error) {
        snprintf(out->details, sizeof(out->details),
                 "可先按 Y 刷新状态；如果仍然失败，请进入支持与恢复查看当前问题和诊断信息。");
    } else if (model->disable_flag_present) {
        snprintf(out->details, sizeof(out->details),
                 "新的控制写入已停止。请进入支持与恢复，完成安全检查后解除停用并重新接管。");
    } else if (model->recovery_active) {
        snprintf(out->details, sizeof(out->details),
                 "后台正在恢复此前设置。恢复完成前请勿重复提交，状态和诊断仍可继续刷新。");
    } else if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) {
        snprintf(out->details, sizeof(out->details),
                 "请进入支持与恢复查看当前问题，并按页面建议重新检测、修复或导出诊断。");
    } else if (model->restriction_enabled_available && !model->restriction_enabled) {
        snprintf(out->details, sizeof(out->details),
                 "请检查 Nintendo 系统家长控制是否已启用，再返回 PlayWise 刷新状态。");
    } else if (model->temporary_unlocked_available && model->temporary_unlocked) {
        snprintf(out->details, sizeof(out->details),
                 "系统临时解除期间不会累计今日计时；主机进入睡眠后会恢复今日限制。");
    } else if (model->apply_pending_confirmation) {
        snprintf(out->details, sizeof(out->details),
                 "后台正在确认设置是否已经生效。完成前请勿重复提交，可稍后按 Y 刷新。");
    } else if (model->restricted_now == 1 ||
               (model->remaining_available && model->remaining_minutes == 0 && model->unrestricted_today != 1)) {
        snprintf(out->details, sizeof(out->details),
                 "可以兑换加时码，或由家长调整今日额度、临时加时或设为今日不限时。");
    }
    out->has_details = out->details[0] != '\0';
}

void ptc_ui_project_time_status(const PtcUiModel *model, int64_t now, PtcUiTimeProjection *out)
{
    time_t clock_value = (time_t)now;
    struct tm *local;
    int remaining;
    int total;
    int64_t age;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    snprintf(out->clock_text, sizeof(out->clock_text), "--:--");
    snprintf(out->remaining_text, sizeof(out->remaining_text), "状态待确认");
    snprintf(out->freshness_text, sizeof(out->freshness_text), "等待刷新");
    out->state = PTC_UI_TIME_UNKNOWN;
    local = localtime(&clock_value);
    if (local) {
        snprintf(out->clock_text, sizeof(out->clock_text), "%02d:%02d",
                 local->tm_hour, local->tm_min);
    }
    if (!model) return;

    age = ptc_ui_status_age_seconds(model, now);
    if (model->waiting) {
        snprintf(out->freshness_text, sizeof(out->freshness_text), "正在同步");
        out->state = PTC_UI_TIME_WAITING;
    } else if (!model->status_loaded) {
        snprintf(out->freshness_text, sizeof(out->freshness_text),
                 model->error_code || strcmp(model->result_status, "error") == 0
                     ? "暂不可用" : "等待刷新");
    } else if (!ptc_ui_status_is_fresh(model, now)) {
        snprintf(out->freshness_text, sizeof(out->freshness_text),
                 model->error_code || strcmp(model->result_status, "error") == 0
                     ? "暂不可用" : "状态待确认");
    } else if (age <= 0) {
        snprintf(out->freshness_text, sizeof(out->freshness_text), "刚刚更新");
    } else {
        snprintf(out->freshness_text, sizeof(out->freshness_text), "%lld 秒前更新",
                 (long long)age);
    }

    if (ptc_ui_status_is_fresh(model, now)) {
        if (model->unrestricted_today == 1) {
            snprintf(out->remaining_text, sizeof(out->remaining_text), "今天还可玩：不限时");
            out->progress_available = true;
            out->progress_per_mille = 1000;
            out->state = PTC_UI_TIME_UNLIMITED;
        } else if (model->remaining_available && model->remaining_minutes >= 0 &&
                   model->forecast_available && model->forecast[0].day_index == model->day_index &&
                   model->forecast[0].mode == PTC_RULE_MODE_LIMIT) {
            remaining = model->remaining_minutes;
            total = model->forecast[0].minutes;
            if (total > 0) {
                snprintf(out->remaining_text, sizeof(out->remaining_text), "今天还可玩：%d 分钟", remaining);
                out->progress_available = true;
                if (remaining >= total) out->progress_per_mille = 1000;
                else out->progress_per_mille = (uint16_t)(remaining * 1000 / total);
                if (remaining == 0) out->state = PTC_UI_TIME_EXHAUSTED;
                else if (remaining < 10) out->state = PTC_UI_TIME_DANGER;
                else if (remaining < 30) out->state = PTC_UI_TIME_REMINDER;
                else out->state = PTC_UI_TIME_NORMAL;
            }
        } else {
            snprintf(out->remaining_text, sizeof(out->remaining_text), "状态待确认");
        }
    }

    /* Operational exceptions are badges over the last authoritative quota;
       they never manufacture a countdown or a zero value. */
    if (model->recovery_active) {
        out->state = PTC_UI_TIME_RECOVERY;
        snprintf(out->freshness_text, sizeof(out->freshness_text), "恢复中");
    } else if (strcmp(model->setup_phase, "protection") == 0 ||
               strcmp(model->setup_phase, "failed") == 0) {
        out->state = PTC_UI_TIME_PROTECTION;
        snprintf(out->freshness_text, sizeof(out->freshness_text), "保护中");
    } else if (model->disable_flag_present) {
        out->state = PTC_UI_TIME_DISABLED;
        snprintf(out->freshness_text, sizeof(out->freshness_text), "控制已停用");
    } else if (model->temporary_unlocked_available && model->temporary_unlocked) {
        out->state = PTC_UI_TIME_TEMPORARY_UNLOCK;
        snprintf(out->freshness_text, sizeof(out->freshness_text), "临时解除中");
    } else if (model->waiting) {
        out->state = PTC_UI_TIME_WAITING;
    }
}

void ptc_ui_format_status_age(const PtcUiModel *model, int64_t now, char *out, size_t out_size)
{
    int64_t age = ptc_ui_status_age_seconds(model, now);
    if (!out || !out_size) return;
    if (!model || !model->status_loaded) snprintf(out, out_size, "等待刷新");
    else if (model->waiting) snprintf(out, out_size, "正在刷新状态...");
    else if (!ptc_ui_status_is_fresh(model, now)) snprintf(out, out_size, "状态待确认，请刷新");
    else if (age == 0) snprintf(out, out_size, "刚刚刷新");
    else if (age < 60) snprintf(out, out_size, "上次刷新：%lld 秒前", (long long)age);
    else snprintf(out, out_size, "上次刷新：%lld 分钟前", (long long)(age / 60));
}

void ptc_ui_format_code(const char *code, char *out, size_t out_size)
{
    char grouped[10] = "____ ____";
    size_t length = code ? strlen(code) : 0;
    if (!out || !out_size) return;
    for (size_t i = 0; i < 8 && i < length; ++i) grouped[i + (i >= 4)] = code[i];
    snprintf(out, out_size, "%s", grouped);
}

const char *ptc_ui_code_failure_guidance(int error_code)
{
    switch (error_code) {
    case PTC_ERR_USED_TOKEN:
        return "这枚代码已使用，请家长生成另一枚代码；返回后输入新码。";
    case PTC_ERR_WRONG_DATE:
        return "代码日期与主机不一致，请刷新主机日期，再请家长按这一天生成新码。";
    case PTC_ERR_BAD_CLOCK:
        return "主机日期无法确认，请家长检查系统日期，返回刷新后再试。";
    case PTC_ERR_BAD_CODE:
    case PTC_ERR_BAD_TOKEN_VERSION:
    case PTC_ERR_UNSUPPORTED_TOKEN_ACTION:
    case PTC_ERR_BAD_SIGNATURE:
        return "代码未通过验证，请核对 8 位数字；仍失败时请家长核对设备并生成新码。";
    case PTC_ERR_MINUTES_EXCEED_LIMIT:
        return "代码时长超过允许上限，请家长选择较短时长生成新码。";
    case PTC_ERR_CODE_COOLDOWN:
        return "输入尝试过多，请稍候再输入；等待期间可返回孩子区。";
    case PTC_ERR_STORAGE_READ_FAILED:
    case PTC_ERR_STORAGE_WRITE_FAILED:
        return "读写未完成，请家长检查 SD 卡空间，并到设置的支持与恢复中确认状态。";
    default:
        return "兑换未完成，请家长到设置的支持与恢复查看原因，恢复正常后再试。";
    }
}

void ptc_ui_format_today_mode(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || !model->status_loaded) {
        snprintf(out, out_size, "等待刷新");
    } else if (model->blocked_today == 1) {
        snprintf(out, out_size, "禁止游玩");
    } else if (model->unrestricted_today == 1) {
        snprintf(out, out_size, "不限时");
    } else if (model->limited_today == 1) {
        snprintf(out, out_size,
                 model->remaining_available && model->remaining_minutes <= 0
                     ? "限时 | 额度用完"
                     : "限时");
    } else {
        snprintf(out, out_size, "状态未知");
    }
}

void ptc_ui_format_quota_remaining(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || !model->status_loaded) {
        snprintf(out, out_size, "--");
    } else if (model->unrestricted_today == 1) {
        snprintf(out, out_size, "不限时");
    } else if (model->remaining_available && model->remaining_minutes >= 0) {
        snprintf(out, out_size, "%d 分钟", model->remaining_minutes);
    } else {
        snprintf(out, out_size, "暂不可用");
    }
}

void ptc_ui_format_timer_status(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || model->play_timer_enabled < 0) {
        snprintf(out, out_size, "未确认");
    } else if (model->play_timer_enabled == 1) {
        snprintf(out, out_size, "已计时");
    } else if (model->unrestricted_today == 1) {
        snprintf(out, out_size, "无需计时");
    } else {
        snprintf(out, out_size, "未计时");
    }
}

void ptc_ui_format_console_date(const PtcUiModel *model, char *out, size_t out_size)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    if (!out || out_size == 0) return;
    /* The offline code MAC binds the day index reported by status. Never fall back
       to the NRO local clock here: a guessed date would advertise a day the
       sysmodule does not accept. */
    if (!model || !model->status_loaded ||
        !ptc_date_from_day_index(model->day_index, &year, &month, &day)) {
        snprintf(out, out_size, "主机日期待刷新");
        return;
    }
    snprintf(out, out_size, "主机今天 %04u-%02u-%02u",
             (unsigned int)year, (unsigned int)month, (unsigned int)day);
}

void ptc_ui_format_parent_status_summary(
    const PtcUiModel *model,
    int64_t now,
    char *out,
    size_t out_size)
{
    int64_t age;
    char remaining[64];
    char freshness[40];
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!model) {
        snprintf(out, out_size, "? 状态待确认  |  尚无可靠读数");
        return;
    }
    age = ptc_ui_status_age_seconds(model, now);
    if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) {
        snprintf(out, out_size, "! 保护模式  |  需要处理");
        return;
    }
    if (model->recovery_active) {
        snprintf(out, out_size, "! 恢复事务待处理  |  查看详情");
        return;
    }
    if (model->disable_flag_present) {
        snprintf(out, out_size, "! 紧急停用  |  控制写入已停止");
        return;
    }
    if (model->restriction_enabled_available && !model->restriction_enabled) {
        snprintf(out, out_size, "! Nintendo 家长控制未启用");
        return;
    }
    if (model->temporary_unlocked_available && model->temporary_unlocked) {
        snprintf(out, out_size, "! 系统限制临时解除  |  期间不计时，进入睡眠后恢复今日限制");
        return;
    }
    if (model->apply_pending_confirmation) {
        snprintf(out, out_size, "... 设置等待确认生效");
        return;
    }
    if (model->waiting) {
        snprintf(out, out_size, "... 正在检测当前状态");
        return;
    }
    if (!ptc_ui_status_is_fresh(model, now)) {
        if (age < 0) snprintf(out, out_size, "? 状态待确认  |  尚无可靠读数");
        else if (age < 3600) snprintf(out, out_size, "? 状态待确认  |  上次成功于 %lld 分钟前", (long long)(age / 60));
        else if (age < 86400) snprintf(out, out_size, "? 状态待确认  |  上次成功于 %lld 小时前", (long long)(age / 3600));
        else snprintf(out, out_size, "? 状态待确认  |  上次成功超过一天");
        return;
    }
    if (model->restricted_now == 1 || model->blocked_today == 1 ||
        (model->remaining_available && model->remaining_minutes <= 0)) {
        snprintf(out, out_size, "! 已到限制  |  今日时间已用完  |  刚刚同步");
        return;
    }
    if (model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), "今天还可玩：不限时");
    else if (model->remaining_available) snprintf(remaining, sizeof(remaining), "今天还可玩 %d 分钟", model->remaining_minutes);
    else snprintf(remaining, sizeof(remaining), "今天还可玩：暂不可用");
    if (age <= 30) snprintf(freshness, sizeof(freshness), "刚刚同步");
    else if (age < 60) snprintf(freshness, sizeof(freshness), "%lld 秒前", (long long)age);
    else snprintf(freshness, sizeof(freshness), "%lld 分钟前", (long long)(age / 60));
    snprintf(out, out_size, "控制正常  |  %s  |  %s", remaining, freshness);
}

void ptc_ui_format_holiday_priority_summary(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model) {
        snprintf(out, out_size, "当前原因：状态尚未刷新");
    } else if (model->today_override_present) {
        snprintf(out, out_size, "当前原因：今日额度调整覆盖其他规则");
    } else if (strcmp(model->rule_source, "scheduled_override") == 0) {
        snprintf(out, out_size, "当前原因：临时额度计划覆盖国家节假日规则");
    } else if (!model->holiday_enabled) {
        snprintf(out, out_size, "当前原因：节假日预设未开启，回退周计划");
    } else if (!model->calendar_covered) {
        snprintf(out, out_size, "当前原因：内置日历未覆盖，回退周计划");
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0) {
        snprintf(out, out_size, "当前原因：法定休假日命中节假日规则");
    } else if (strcmp(model->rule_source, "makeup_workday") == 0) {
        snprintf(out, out_size, "当前原因：调休工作日命中节假日规则");
    } else {
        snprintf(out, out_size, "当前原因：普通日期，回退周计划");
    }
}
