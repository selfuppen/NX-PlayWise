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

bool ptc_ui_bedtime_save_will_restrict(const PtcUiModel *model, uint16_t minute_of_day)
{
    PtcRules rules;
    PtcBedtimeEvaluation evaluation;
    if (!model || minute_of_day >= 1440 ||
        !ptc_bedtime_policy_is_valid(&model->draft_bedtime_policy) ||
        !model->draft_bedtime_policy.enabled ||
        (model->bedtime_active && !model->bedtime_skipped)) {
        return false;
    }
    memset(&rules, 0, sizeof(rules));
    rules.bedtime = model->draft_bedtime_policy;
    evaluation = ptc_bedtime_evaluate(
        &rules,
        model->day_index,
        ptc_weekday_from_day_index(model->day_index),
        minute_of_day);
    return evaluation.active;
}

const char *ptc_ui_effective_rule_label(PtcRuleSource source)
{
    switch (source) {
    case PTC_RULE_SOURCE_STATUTORY_HOLIDAY: return "国家法定休假日";
    case PTC_RULE_SOURCE_MAKEUP_WORKDAY: return "国家调休工作日";
    case PTC_RULE_SOURCE_TODAY_OVERRIDE: return "今日额度调整";
    case PTC_RULE_SOURCE_SCHEDULED_OVERRIDE: return "临时额度计划";
    case PTC_RULE_SOURCE_WEEKLY:
    default: return "周计划";
    }
}
#define effective_rule_label ptc_ui_effective_rule_label

static void format_rule_basis(PtcDayRule rule, int played_minutes, bool played_available,
                              char *out, size_t out_size)
{
    int remaining;
    if (!out || out_size == 0) return;
    if (rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(out, out_size, "不限时");
    } else if (!played_available || played_minutes < 0) {
        snprintf(out, out_size, "额度 %u 分钟；额度消耗估算不可用，暂不能估算剩余",
                 (unsigned int)rule.minutes);
    } else {
        remaining = (int)rule.minutes - played_minutes;
        if (remaining < 0) remaining = 0;
        snprintf(out, out_size, "额度 %u 分钟 - 已耗 %d 分钟 = 预计剩余 %d 分钟",
                 (unsigned int)rule.minutes, played_minutes, remaining);
    }
}

bool ptc_ui_scheduled_dirty(const PtcUiModel *model)
{
    const PtcScheduledOverride *a = &model->scheduled_override;
    const PtcScheduledOverride *b = &model->draft_scheduled_override;
    /* Disabled editor defaults are not an unsaved active plan. */
    return a->enabled != b->enabled || (b->enabled &&
        (a->start_day_index != b->start_day_index || a->end_day_index != b->end_day_index ||
         ptc_ui_day_rule_effectively_changed(a->rule, b->rule)));
}

void ptc_ui_discard_scheduled(PtcUiModel *model)
{
    model->draft_scheduled_override = model->scheduled_override;
    model->overlay = PTC_UI_OVERLAY_NONE;
}

void ptc_ui_reconcile_scheduled_result(PtcUiModel *model, const PtcScheduledOverride *draft, bool preserve)
{
    bool saved = strcmp(model->result_type, "set_scheduled_override") == 0 &&
                 strcmp(model->result_status, "ok") == 0;
    /* Call after reloading persisted rules: unrelated refreshes and failures keep the local draft. */
    if (preserve && !saved) model->draft_scheduled_override = *draft;
    if (saved && model->overlay == PTC_UI_OVERLAY_SCHEDULED) model->overlay = PTC_UI_OVERLAY_NONE;
}

static void build_plan_rules(const PtcUiModel *model, PtcUiPlanKind kind, PtcRules *rules)
{
    ptc_rules_default(rules);
    memcpy(rules->week, kind == PTC_UI_PLAN_WEEKLY ? model->draft_week : model->current_week,
           sizeof(rules->week));
    rules->today_override = (PtcTodayOverride){model->day_index, model->today_override_present,
                                             model->today_override_rule};
    rules->scheduled_override = kind == PTC_UI_PLAN_SCHEDULED
        ? model->draft_scheduled_override : model->scheduled_override;
    rules->holiday_enabled = kind == PTC_UI_PLAN_HOLIDAY ? model->draft_holiday_enabled : model->holiday_enabled;
    rules->holiday_rule = kind == PTC_UI_PLAN_HOLIDAY ? model->draft_holiday_rule : model->holiday_rule;
    rules->makeup_workday_rule = kind == PTC_UI_PLAN_HOLIDAY ? model->draft_makeup_workday_rule : model->makeup_workday_rule;
}

PtcEffectiveRule ptc_ui_plan_rule(const PtcUiModel *model, PtcUiPlanKind kind)
{
    PtcRules rules;
    build_plan_rules(model, kind, &rules);
    return ptc_rules_resolve(&rules, model->day_index, ptc_weekday_from_day_index(model->day_index));
}

const char *ptc_ui_decision_state_label(PtcUiDecisionState state)
{
    switch (state) {
    case PTC_UI_DECISION_SELECTED: return "当前生效";
    case PTC_UI_DECISION_OVERRIDDEN: return "命中但被覆盖";
    case PTC_UI_DECISION_NOT_CONFIGURED: return "未设置";
    case PTC_UI_DECISION_NOT_MATCHED: return "未命中";
    case PTC_UI_DECISION_DISABLED: return "未开启";
    case PTC_UI_DECISION_CALENDAR_UNCOVERED: return "日历未覆盖";
    case PTC_UI_DECISION_UNKNOWN:
    default: return "状态待确认";
    }
}

static void set_decision_step(PtcUiDecisionStep *step, PtcUiDecisionState state,
                              PtcDayRule rule, const char *reason)
{
    if (!step) return;
    step->state = state;
    step->rule = rule;
    snprintf(step->reason, sizeof(step->reason), "%s", reason ? reason : "");
}

void ptc_ui_build_day_decision(const PtcUiModel *model, PtcUiPlanKind kind, uint16_t day_index, int64_t now,
                               PtcUiTodayDecision *decision)
{
    PtcRules rules;
    PtcCalendarDayType day_type;
    bool calendar_covered = false;
    bool scheduled_matches;
    bool holiday_matches;
    bool is_today;
    uint8_t weekday;
    PtcDayRule empty = {PTC_RULE_MODE_LIMIT, 0};
    if (!decision) return;
    memset(decision, 0, sizeof(*decision));
    if (!model || !ptc_ui_status_is_fresh(model, now)) {
        set_decision_step(&decision->today_override, PTC_UI_DECISION_UNKNOWN, empty, "尚无可靠状态");
        set_decision_step(&decision->scheduled_override, PTC_UI_DECISION_UNKNOWN, empty, "尚无可靠状态");
        set_decision_step(&decision->holiday, PTC_UI_DECISION_UNKNOWN, empty, "尚无可靠状态");
        set_decision_step(&decision->weekly, PTC_UI_DECISION_UNKNOWN, empty, "尚无可靠状态");
        snprintf(decision->final_reason, sizeof(decision->final_reason), "刷新后确认规则决策");
        snprintf(decision->bedtime, sizeof(decision->bedtime), "就寝时间：状态待确认");
        snprintf(decision->autonomy, sizeof(decision->autonomy), "自主缓冲：状态待确认");
        return;
    }
    is_today = (day_index == model->day_index);
    build_plan_rules(model, kind, &rules);
    weekday = ptc_weekday_from_day_index(day_index);
    decision->effective = ptc_rules_resolve(&rules, day_index, weekday);
    day_type = ptc_holiday_calendar_classify(day_index, &calendar_covered);
    scheduled_matches = rules.scheduled_override.enabled &&
        day_index >= rules.scheduled_override.start_day_index &&
        day_index <= rules.scheduled_override.end_day_index;
    holiday_matches = rules.holiday_enabled && calendar_covered &&
        (day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY ||
         day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY);

    if (!is_today) {
        set_decision_step(&decision->today_override, PTC_UI_DECISION_NOT_CONFIGURED, empty,
                          "仅限当天生效，不作用于未来日期");
    } else if (!rules.today_override.present) {
        set_decision_step(&decision->today_override, PTC_UI_DECISION_NOT_CONFIGURED, empty,
                          model->today_override_cleared_in_session ? "本次会话已清除" : "今天没有单独额度调整");
    } else {
        set_decision_step(&decision->today_override,
            decision->effective.source == PTC_RULE_SOURCE_TODAY_OVERRIDE
                ? PTC_UI_DECISION_SELECTED : PTC_UI_DECISION_OVERRIDDEN,
            rules.today_override.rule, "今天存在单独额度调整");
    }

    if (!rules.scheduled_override.enabled) {
        set_decision_step(&decision->scheduled_override, PTC_UI_DECISION_NOT_CONFIGURED, empty,
                          "没有启用临时额度计划");
    } else if (!scheduled_matches) {
        set_decision_step(&decision->scheduled_override, PTC_UI_DECISION_NOT_MATCHED,
                          rules.scheduled_override.rule, is_today ? "今天不在计划日期范围内" : "当天不在计划日期范围内");
    } else {
        set_decision_step(&decision->scheduled_override,
            decision->effective.source == PTC_RULE_SOURCE_SCHEDULED_OVERRIDE
                ? PTC_UI_DECISION_SELECTED : PTC_UI_DECISION_OVERRIDDEN,
            rules.scheduled_override.rule,
            decision->effective.source == PTC_RULE_SOURCE_TODAY_OVERRIDE
                ? "日期命中，但被今日额度调整覆盖" : (is_today ? "今天命中临时额度计划" : "当天命中临时额度计划"));
    }

    if (!rules.holiday_enabled) {
        set_decision_step(&decision->holiday, PTC_UI_DECISION_DISABLED, empty,
                          "国家节假日总开关未开启");
    } else if (!calendar_covered) {
        set_decision_step(&decision->holiday, PTC_UI_DECISION_CALENDAR_UNCOVERED, empty,
                          is_today ? "内置日历未覆盖今天" : "内置日历未覆盖当天");
    } else if (!holiday_matches) {
        set_decision_step(&decision->holiday, PTC_UI_DECISION_NOT_MATCHED, empty,
                          is_today ? "今天是普通日期" : "当天是普通日期");
    } else {
        PtcDayRule holiday_rule = day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY
            ? rules.holiday_rule : rules.makeup_workday_rule;
        const char *hit = day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY
            ? "命中法定休假日" : "命中调休工作日";
        set_decision_step(&decision->holiday,
            decision->effective.source == PTC_RULE_SOURCE_STATUTORY_HOLIDAY ||
            decision->effective.source == PTC_RULE_SOURCE_MAKEUP_WORKDAY
                ? PTC_UI_DECISION_SELECTED : PTC_UI_DECISION_OVERRIDDEN,
            holiday_rule, hit);
        if (decision->holiday.state == PTC_UI_DECISION_OVERRIDDEN) {
            snprintf(decision->holiday.reason, sizeof(decision->holiday.reason), "%s，但被更高优先级规则覆盖", hit);
        }
    }

    set_decision_step(&decision->weekly,
        decision->effective.source == PTC_RULE_SOURCE_WEEKLY
            ? PTC_UI_DECISION_SELECTED : PTC_UI_DECISION_OVERRIDDEN,
        rules.week[weekday], decision->effective.source == PTC_RULE_SOURCE_WEEKLY
            ? "没有更高优先级规则命中" : (is_today ? "作为今天的基础规则保留" : "作为当天的基础规则保留"));

    snprintf(decision->final_reason, sizeof(decision->final_reason), "最终采用%s%s",
             effective_rule_label(decision->effective.source),
             decision->effective.rule.mode == PTC_RULE_MODE_UNLIMITED ? (is_today ? "，今天不限时" : "，当天不限时") : "");

    if (is_today) {
        if (!model->bedtime_policy.enabled) {
            snprintf(decision->bedtime, sizeof(decision->bedtime), "就寝时间：未开启");
        } else if (model->bedtime_active && model->bedtime_skipped) {
            snprintf(decision->bedtime, sizeof(decision->bedtime), "就寝时间：当前窗口已跳过");
        } else if (model->bedtime_active) {
            snprintf(decision->bedtime, sizeof(decision->bedtime), "就寝时间：当前并行限制中");
        } else if (model->bedtime_next_available) {
            snprintf(decision->bedtime, sizeof(decision->bedtime), "就寝时间：已开启，等待下次窗口");
        } else {
            snprintf(decision->bedtime, sizeof(decision->bedtime), "就寝时间：已开启，今天无可用窗口");
        }
        if (model->daily_buffer_minutes == 0) {
            snprintf(decision->autonomy, sizeof(decision->autonomy), "自主缓冲：未开启");
        } else if (model->daily_buffer_claimed) {
            snprintf(decision->autonomy, sizeof(decision->autonomy), "自主缓冲：今日已领取 %u 分钟",
                     (unsigned int)model->daily_buffer_minutes);
        } else if (model->daily_buffer_available) {
            snprintf(decision->autonomy, sizeof(decision->autonomy), "自主缓冲：今日可领取 %u 分钟",
                     (unsigned int)model->daily_buffer_minutes);
        } else {
            snprintf(decision->autonomy, sizeof(decision->autonomy), "自主缓冲：已配置，但今天暂不可领取");
        }
    } else {
        PtcEffectiveBedtime bt = ptc_bedtime_resolve_start_day(&rules, day_index, weekday);
        if (!rules.bedtime.enabled || !bt.window.enabled) {
            snprintf(decision->bedtime, sizeof(decision->bedtime), "就寝时间：当天无就寝限制");
        } else {
            const char *src_label = bt.source == PTC_BEDTIME_SOURCE_SCHEDULED_OVERRIDE ? "临时特例" :
                (bt.source == PTC_BEDTIME_SOURCE_STATUTORY_HOLIDAY ? "法定节假日" :
                 (bt.source == PTC_BEDTIME_SOURCE_MAKEUP_WORKDAY ? "调休工作日" : "每周常规"));
            snprintf(decision->bedtime, sizeof(decision->bedtime), "就寝时间：%02u:%02u 开始（来源：%s）",
                     (unsigned int)(bt.window.start_minute / 60),
                     (unsigned int)(bt.window.start_minute % 60),
                     src_label);
        }
        if (model->daily_buffer_minutes == 0) {
            snprintf(decision->autonomy, sizeof(decision->autonomy), "自主缓冲：未开启");
        } else {
            snprintf(decision->autonomy, sizeof(decision->autonomy), "自主缓冲：每日 %u 分钟（当天有效）",
                     (unsigned int)model->daily_buffer_minutes);
        }
    }
}

void ptc_ui_build_today_decision(const PtcUiModel *model, PtcUiPlanKind kind, int64_t now,
                                 PtcUiTodayDecision *decision)
{
    if (!model) {
        if (decision) memset(decision, 0, sizeof(*decision));
        return;
    }
    ptc_ui_build_day_decision(model, kind, model->day_index, now, decision);
}

void ptc_ui_format_today_adjustment_status(const PtcUiModel *model, int64_t now,
                                           char *badge, size_t badge_size,
                                           char *detail, size_t detail_size)
{
    PtcUiTodayDecision decision;
    const char *source;
    if (!badge || badge_size == 0 || !detail || detail_size == 0) return;
    if (!model || !ptc_ui_status_is_fresh(model, now)) {
        snprintf(badge, badge_size, "待确认");
        snprintf(detail, detail_size, "状态待确认，刷新后判断今日额度调整");
        return;
    }
    ptc_ui_build_today_decision(model, PTC_UI_PLAN_SAVED, now, &decision);
    source = effective_rule_label(decision.effective.source);
    if (model->disable_flag_present) {
        snprintf(badge, badge_size, "控制停用");
        snprintf(detail, detail_size, "规则仍保留，但控制已停用");
    } else if (model->recovery_active) {
        snprintf(badge, badge_size, "恢复中");
        snprintf(detail, detail_size, "正在恢复设置，暂不判断是否生效");
    } else if (model->apply_pending_confirmation) {
        snprintf(badge, badge_size, "等待生效");
        snprintf(detail, detail_size, "设置已提交，等待后台确认");
    } else if (model->temporary_unlocked_available && model->temporary_unlocked) {
        snprintf(badge, badge_size, "暂不计时");
        snprintf(detail, detail_size, "规则已保留；临时解除期间不计时");
    } else if (model->today_override_present &&
               decision.effective.source == PTC_RULE_SOURCE_TODAY_OVERRIDE) {
        if (model->bedtime_active && !model->bedtime_skipped) {
            snprintf(badge, badge_size, "就寝立断");
            if (model->today_override_rule.mode == PTC_RULE_MODE_UNLIMITED)
                snprintf(detail, detail_size, "就寝限制中，调整不限时已保留");
            else
                snprintf(detail, detail_size, "就寝限制中，调整%u分钟已保留",
                         (unsigned int)model->today_override_rule.minutes);
        } else {
            snprintf(badge, badge_size, "生效中");
            if (model->today_override_rule.mode == PTC_RULE_MODE_UNLIMITED)
                snprintf(detail, detail_size, "今日额度调整：不限时");
            else
                snprintf(detail, detail_size, "今日额度调整：%u 分钟",
                         (unsigned int)model->today_override_rule.minutes);
        }
    } else if (model->today_override_cleared_in_session) {
        snprintf(badge, badge_size, "已清除");
        if (model->bedtime_active && !model->bedtime_skipped)
            snprintf(detail, detail_size, "就寝限制中，额度由%s生效", source);
        else
            snprintf(detail, detail_size, "当前改由%s生效", source);
    } else {
        snprintf(badge, badge_size, "未设置");
        if (model->bedtime_active && !model->bedtime_skipped)
            snprintf(detail, detail_size, "就寝限制中，额度由%s生效", source);
        else
            snprintf(detail, detail_size, "当前由%s生效", source);
    }
}

bool ptc_ui_plan_save_requires_hold(const PtcUiModel *model, PtcUiPlanKind kind, int64_t now)
{
    PtcEffectiveRule before;
    PtcEffectiveRule after;
    if (!model) return false;
    before = ptc_ui_plan_rule(model, PTC_UI_PLAN_SAVED);
    after = ptc_ui_plan_rule(model, kind);
    /* A source-only change with the same effective quota cannot newly restrict today. */
    if (!ptc_ui_day_rule_effectively_changed(before.rule, after.rule)) return false;
    if (after.rule.mode == PTC_RULE_MODE_UNLIMITED) return false;
    if (!ptc_ui_status_is_fresh(model, now) || !model->played_minutes_available || model->played_minutes < 0) return true;
    return ptc_ui_day_rule_would_restrict(model, after.rule);
}

void ptc_ui_project_plan_impact(const PtcUiModel *model, PtcUiPlanKind kind,
                                bool dirty, int64_t now, PtcUiPlanImpactProjection *out)
{
    int remaining;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!model) {
        out->state = PTC_UI_PLAN_IMPACT_UNKNOWN;
        return;
    }
    out->before = ptc_ui_plan_rule(model, PTC_UI_PLAN_SAVED);
    out->after = ptc_ui_plan_rule(model, kind);
    out->quota_changes_today = ptc_ui_day_rule_effectively_changed(out->before.rule, out->after.rule);
    out->source_changes_today = out->before.source != out->after.source;
    if (!dirty) {
        out->state = PTC_UI_PLAN_IMPACT_CURRENT;
    } else if (!out->quota_changes_today) {
        out->state = PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE;
    } else if (out->after.rule.mode == PTC_RULE_MODE_UNLIMITED) {
        out->state = PTC_UI_PLAN_IMPACT_CHANGES_TODAY;
    } else if (!ptc_ui_status_is_fresh(model, now) ||
               !model->played_minutes_available || model->played_minutes < 0) {
        out->state = PTC_UI_PLAN_IMPACT_UNKNOWN;
    } else {
        remaining = (int)out->after.rule.minutes - model->played_minutes;
        if (remaining < 0) remaining = 0;
        out->remaining_available = true;
        out->remaining_minutes = remaining;
        out->state = remaining == 0 ? PTC_UI_PLAN_IMPACT_EXHAUSTED :
            PTC_UI_PLAN_IMPACT_CHANGES_TODAY;
    }

    if (out->state == PTC_UI_PLAN_IMPACT_CURRENT &&
        out->after.rule.mode == PTC_RULE_MODE_LIMIT &&
        ptc_ui_status_is_fresh(model, now) && model->played_minutes_available &&
        model->played_minutes >= 0) {
        remaining = (int)out->after.rule.minutes - model->played_minutes;
        out->remaining_available = true;
        out->remaining_minutes = remaining > 0 ? remaining : 0;
    }
}

void ptc_ui_format_plan_impact(const PtcUiModel *model, PtcUiPlanKind kind,
                             int64_t now, char *out, size_t out_size)
{
    PtcUiPlanImpactProjection impact;
    PtcEffectiveRule after;
    ptc_ui_project_plan_impact(model, kind, true, now, &impact);
    after = impact.after;
    if (!ptc_ui_status_is_fresh(model, now)) {
        if (!impact.quota_changes_today)
            snprintf(out, out_size, "今天额度不变，继续按%s执行。", effective_rule_label(after.source));
        else
            snprintf(out, out_size, "状态待确认，刷新后查看对今天的影响。");
    } else if (!impact.quota_changes_today) {
        if (impact.source_changes_today) {
            snprintf(out, out_size, "今天额度不变；保存后规则来源切换为%s。",
                     effective_rule_label(after.source));
        } else if (kind == PTC_UI_PLAN_WEEKLY) {
            snprintf(out, out_size, "今天不变，继续按%s执行；新规则将在对应星期且无更高优先级覆盖时生效。",
                     effective_rule_label(after.source));
        } else if (kind == PTC_UI_PLAN_HOLIDAY) {
            snprintf(out, out_size, "今天不变，继续按%s执行；新规则将在开关开启且内置日历命中时生效。",
                     effective_rule_label(after.source));
        } else
            snprintf(out, out_size, "今天不变，继续按%s执行；临时额度将在日期范围命中且无今日调整覆盖时生效。",
                     effective_rule_label(after.source));
    } else if (after.rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(out, out_size, "保存后今天按%s：不限时。", effective_rule_label(after.source));
    } else if (!model->played_minutes_available) {
        snprintf(out, out_size, "保存后今天按%s：%u 分钟；剩余暂不可用。",
                 effective_rule_label(after.source), (unsigned int)after.rule.minutes);
    } else {
        int remaining = (int)after.rule.minutes - model->played_minutes;
        snprintf(out, out_size, "保存后今天按%s：总额度 %u 分钟，预计还可玩 %d 分钟。",
                 effective_rule_label(after.source), (unsigned int)after.rule.minutes, remaining > 0 ? remaining : 0);
    }
}

static PtcDayRule active_rule_for_source(const PtcUiModel *model)
{
    uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
    if (strcmp(model->rule_source, "today_override") == 0) return model->today_override_rule;
    if (strcmp(model->rule_source, "scheduled_override") == 0) return model->scheduled_override.rule;
    if (strcmp(model->rule_source, "statutory_holiday") == 0) return model->holiday_rule;
    if (strcmp(model->rule_source, "makeup_workday") == 0) return model->makeup_workday_rule;
    return model->draft_week[weekday];
}

PtcEffectiveRule ptc_ui_rule_after_today_restore(const PtcUiModel *model)
{
    PtcRules rules;
    PtcEffectiveRule empty;
    memset(&empty, 0, sizeof(empty));
    empty.source = PTC_RULE_SOURCE_WEEKLY;
    empty.rule.mode = PTC_RULE_MODE_LIMIT;
    empty.rule.minutes = 60;
    if (!model) return empty;
    ptc_rules_default(&rules);
    memcpy(rules.week, model->current_week, sizeof(rules.week));
    rules.today_override.present = false;
    rules.scheduled_override = model->scheduled_override;
    rules.holiday_enabled = model->holiday_enabled;
    rules.holiday_rule = model->holiday_rule;
    rules.makeup_workday_rule = model->makeup_workday_rule;
    return ptc_rules_resolve(&rules, model->day_index, ptc_weekday_from_day_index(model->day_index));
}

void ptc_ui_format_restore_today_basis(const PtcUiModel *model, char *out, size_t out_size)
{
    PtcEffectiveRule after;
    char current[112];
    char restored[112];
    if (!out || out_size == 0) return;
    if (!model) {
        snprintf(out, out_size, "当前状态不可用，恢复后请刷新确认。");
        return;
    }
    after = ptc_ui_rule_after_today_restore(model);
    format_rule_basis(model->today_override_rule, model->played_minutes, model->played_minutes_available,
                      current, sizeof(current));
    format_rule_basis(after.rule, model->played_minutes, model->played_minutes_available,
                      restored, sizeof(restored));
    snprintf(out, out_size, "今日额度调整当前值：%s\n清除后按%s：%s",
             current, effective_rule_label(after.source), restored);
}

void ptc_ui_format_weekly_save_result(const PtcUiModel *model, char *message, size_t message_size,
                                      char *detail, size_t detail_size)
{
    uint8_t weekday;
    char basis[112];
    char current_basis[112];
    const char *current_source;
    bool today_changed;
    if (!model || !message || message_size == 0 || !detail || detail_size == 0) return;
    weekday = ptc_weekday_from_day_index(model->day_index);
    today_changed = ptc_ui_day_rule_effectively_changed(model->current_week[weekday], model->draft_week[weekday]);
    format_rule_basis(model->draft_week[weekday], model->played_minutes, model->played_minutes_available,
                      basis, sizeof(basis));
    current_source = effective_rule_label(
        strcmp(model->rule_source, "today_override") == 0 ? PTC_RULE_SOURCE_TODAY_OVERRIDE :
        strcmp(model->rule_source, "scheduled_override") == 0 ? PTC_RULE_SOURCE_SCHEDULED_OVERRIDE :
        strcmp(model->rule_source, "statutory_holiday") == 0 ? PTC_RULE_SOURCE_STATUTORY_HOLIDAY :
        strcmp(model->rule_source, "makeup_workday") == 0 ? PTC_RULE_SOURCE_MAKEUP_WORKDAY : PTC_RULE_SOURCE_WEEKLY);
    format_rule_basis(active_rule_for_source(model), model->played_minutes, model->played_minutes_available,
                      current_basis, sizeof(current_basis));
    if (!today_changed) {
        snprintf(message, message_size, "周计划已保存；本次只修改其他日期，今天不受影响。");
        snprintf(detail, detail_size, "今天继续按%s执行：%s。", current_source, current_basis);
    } else if (strcmp(model->rule_source, "today_override") == 0) {
        PtcEffectiveRule restored = ptc_ui_rule_after_today_restore(model);
        snprintf(message, message_size, "周计划已保存；今天仍按临时设置执行，当前不变。");
        if (restored.source == PTC_RULE_SOURCE_WEEKLY) {
            snprintf(detail, detail_size, "当前按今日额度调整：%s；清除今日额度调整后：%s。",
                     current_basis, basis);
        } else {
            snprintf(detail, detail_size, "清除今日额度调整后仍按%s执行；本次只更新周计划。",
                     effective_rule_label(restored.source));
        }
    } else if (strcmp(model->rule_source, "scheduled_override") == 0) {
        snprintf(message, message_size, "周计划已保存；今天仍按临时额度计划执行，当前不变。");
        snprintf(detail, detail_size, "今天继续按临时额度计划：%s。", current_basis);
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0 ||
               strcmp(model->rule_source, "makeup_workday") == 0) {
        snprintf(message, message_size, "周计划已保存；今天由%s覆盖，当前不变。",
                 strcmp(model->rule_source, "statutory_holiday") == 0 ? "国家法定休假日" : "国家调休工作日");
        snprintf(detail, detail_size, "当前按%s：%s；今天对应的周计划已更新为：%s。",
                 current_source, current_basis, basis);
    } else {
        snprintf(message, message_size, "周计划已保存并生效，已影响今天。");
        snprintf(detail, detail_size, "今天按新周计划执行：%s。", basis);
    }
}

void ptc_ui_format_holiday_save_result(const PtcUiModel *model, char *message, size_t message_size,
                                       char *detail, size_t detail_size)
{
    const char *source;
    PtcDayRule active;
    char basis[112];
    uint8_t weekday;
    if (!model || !message || message_size == 0 || !detail || detail_size == 0) return;
    detail[0] = '\0';
    weekday = ptc_weekday_from_day_index(model->day_index);
    if (strcmp(model->rule_source, "scheduled_override") == 0) {
        snprintf(message, message_size, "国家节假日设置已保存；今天仍按临时额度计划执行，当前不变。");
        snprintf(detail, detail_size, "原因：临时额度计划优先于国家节假日规则。");
        return;
    }
    if (strcmp(model->rule_source, "today_override") == 0) {
        format_rule_basis(model->today_override_rule, model->played_minutes,
                          model->played_minutes_available, basis, sizeof(basis));
        snprintf(message, message_size, "国家节假日设置已保存；今天仍按临时设置执行，当前不变。");
        snprintf(detail, detail_size, "原因：今日额度调整优先于国家节假日规则。");
        return;
    }
    if (strcmp(model->rule_source, "statutory_holiday") == 0) {
        source = "国家法定休假日";
        active = model->draft_holiday_rule;
    } else if (strcmp(model->rule_source, "makeup_workday") == 0) {
        source = "国家调休工作日";
        active = model->draft_makeup_workday_rule;
    } else {
        source = "周计划";
        active = model->draft_week[weekday];
    }
    format_rule_basis(active, model->played_minutes, model->played_minutes_available, basis, sizeof(basis));
    if (!model->draft_holiday_enabled) {
        snprintf(message, message_size, "国家节假日预设已保存但未启用；今天不受影响。");
        snprintf(detail, detail_size, "原因：国家节假日总开关未开启。");
    } else if (!model->calendar_covered) {
        snprintf(message, message_size, "国家节假日设置已保存；内置日历未覆盖今天，今天不受影响。");
        snprintf(detail, detail_size, "原因：今天不在内置日历覆盖范围内，继续使用周计划。");
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0 ||
               strcmp(model->rule_source, "makeup_workday") == 0) {
        snprintf(message, message_size, "国家节假日设置已保存并生效，已影响今天。");
        snprintf(detail, detail_size, "今天按%s执行：%s。", source, basis);
    } else {
        snprintf(message, message_size, "国家节假日设置已保存；今天是普通日期，不受影响。");
        snprintf(detail, detail_size, "原因：今天未命中国家节假日安排，继续使用周计划。");
    }
}
