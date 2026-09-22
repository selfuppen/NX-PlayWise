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

int ptc_ui_migrate_setup_step(int step, int wizard_version)
{
    if (step <= 0) return 0;
    if (wizard_version >= 4) return step <= PTC_UI_SETUP_ZONE ? step : PTC_UI_SETUP_SHORTCUT;
    return PTC_UI_SETUP_SHORTCUT;
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

void ptc_ui_format_custom_shortcut_hint(
    const char *shortcut_label,
    char *out,
    size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "长按约 400ms：%s 进入家长区",
             shortcut_label && shortcut_label[0] ? shortcut_label : "自定义组合");
}

int ptc_ui_weekday_for_display_slot(int slot)
{
    static const int ORDER[] = {1, 2, 3, 4, 5, 6, 0};
    return slot >= 0 && slot < 7 ? ORDER[slot] : 0;
}

int ptc_ui_parent_action_count(PtcUiParentPage page)
{
    switch (page) {
    case PTC_UI_PARENT_PLAN:
        return 5;
    case PTC_UI_PARENT_GRANT:
        return 4;
    case PTC_UI_PARENT_SETTINGS:
        return 5;
    case PTC_UI_PARENT_SUPPORT:
        return 6;
    case PTC_UI_PARENT_TODAY:
        return 6;
    default:
        return 5;
    }
}

const char *ptc_ui_settings_status_label(const PtcUiModel *model)
{
    if (!model) return NULL;
    if (model->disable_flag_present || model->recovery_active ||
        strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) {
        return "需处理";
    }
    if (model->setup_phase[0] && strcmp(model->setup_phase, "active") != 0) return "待完成";
    return NULL;
}

PtcUiActionState ptc_ui_settings_support_state(const PtcUiModel *model)
{
    return ptc_ui_settings_status_label(model) ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_AVAILABLE;
}

void ptc_ui_change_parent_page(PtcUiModel *model, int direction)
{
    int page;
    if (!model || direction == 0) {
        return;
    }
    page = (int)model->parent_page + (direction > 0 ? 1 : -1);
    if (page < 0) {
        page = PTC_UI_PARENT_PAGE_COUNT - 1;
    } else if (page >= PTC_UI_PARENT_PAGE_COUNT) {
        page = 0;
    }
    model->parent_page = (PtcUiParentPage)page;
    if (model->parent_page == PTC_UI_PARENT_PLAN) model->plan_page = PTC_UI_PLAN_PAGE_ROOT;
    if (!model->parent_footer_focused) model->selected_index = 0;
}

void ptc_ui_move_parent_selection(PtcUiModel *model, int horizontal, int vertical)
{
    int count;
    int index;
    int row;
    int row_count;
    int column;
    int target;
    if (!model) {
        return;
    }
    if (model->parent_footer_focused) {
        if (!ptc_ui_parent_status_alert_visible(model)) model->parent_footer_selection = 0;
        if (horizontal < 0 && model->parent_footer_selection > 0) {
            --model->parent_footer_selection;
        } else if (horizontal > 0 && model->parent_footer_selection < 1 &&
                   ptc_ui_parent_status_alert_visible(model)) {
            ++model->parent_footer_selection;
        }
        if (vertical < 0) {
            model->parent_footer_focused = false;
            model->selected_index = model->parent_content_selection;
        }
        return;
    }
    count = model->parent_page == PTC_UI_PARENT_PLAN &&
            model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY
        ? 7 : ptc_ui_parent_action_count(model->parent_page);
    if (count <= 0) {
        model->selected_index = 0;
        return;
    }
    index = model->selected_index;
    if (index < 0 ||
        (model->parent_page == PTC_UI_PARENT_SUPPORT
            ? index >= count + model->recent_event_count
            : (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_ROOT && model->forecast_available
                ? index >= count + 7
                : index >= count))) {
        index = 0;
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
        static const int left[7]  = {0, 1, 1, 3, 3, 4, 5};
        static const int right[7] = {0, 2, 6, 4, 5, 6, 6};
        static const int up[7]    = {0, 0, 0, 1, 1, 2, 2};
        static const int down[7]  = {1, 3, 5, 3, 4, 5, 6};
        int previous = index;
        if (horizontal < 0) index = left[index];
        else if (horizontal > 0) index = right[index];
        else if (vertical < 0) index = up[index];
        else if (vertical > 0) index = down[index];
        model->selected_index = index;
        if (index == 1 || index == 2) model->holiday_last_rule = index - 1;
        if (vertical > 0 && previous == down[previous]) {
            model->parent_content_selection = previous;
            model->parent_footer_focused = true;
            model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
        }
        return;
    }
    if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
        int event_count = model->recent_event_count;
        int max_index = 5 + event_count;
        if (index > max_index) index = 0;
        if (index >= 6) {
            if (vertical < 0) index = index == 6 ? 4 : index - 1;
            else if (vertical > 0 && index < max_index) ++index;
            else if (vertical > 0) {
                model->parent_content_selection = index;
                model->parent_footer_focused = true;
                model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
            }
        } else {
            int previous = index;
            if (horizontal < 0 && index % 2 == 1) --index;
            else if (horizontal > 0 && index % 2 == 0) ++index;
            else if (vertical < 0 && index >= 2) index -= 2;
            else if (vertical > 0 && index < 4) index += 2;
            else if (vertical > 0 && event_count > 0) index = 6;
            else if (vertical > 0) {
                model->parent_content_selection = previous;
                model->parent_footer_focused = true;
                model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
            }
        }
        model->selected_index = index;
        return;
    }
    if (model->parent_page == PTC_UI_PARENT_TODAY) {
        if (horizontal < 0 && index % 2 == 1) --index;
        else if (horizontal > 0 && index % 2 == 0) ++index;
        else if (vertical < 0 && index >= 2) index -= 2;
        else if (vertical > 0 && index < 4) index += 2;
        else if (vertical > 0) {
            model->parent_content_selection = index;
            model->parent_footer_focused = true;
            model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
        }
        model->selected_index = index;
        return;
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_ROOT) {
        static const int left_target[5] = {0, 1, 2, 0, 1};
        static const int right_target[5] = {3, 4, 4, 3, 4};
        int previous = index;
        if (index >= 5 && index <= 11) {
            if (horizontal < 0) {
                index = index <= 6 ? 3 : 4;
            } else if (vertical < 0) {
                if (index > 5) --index;
            } else if (vertical > 0) {
                if (index < 11) ++index;
                else {
                    model->parent_content_selection = previous;
                    model->parent_footer_focused = true;
                    model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
                }
            }
        } else {
            if (horizontal < 0 && index >= 3) {
                index = left_target[index];
            } else if (horizontal > 0) {
                if (index < 3) {
                    index = right_target[index];
                } else if (model->forecast_available) {
                    index = (index == 3) ? 5 : 7;
                }
            } else if (vertical < 0) {
                if (index == 1 || index == 2 || index == 4) --index;
            } else if (vertical > 0) {
                if (index == 0 || index == 1 || index == 3) ++index;
                else {
                    model->parent_content_selection = previous;
                    model->parent_footer_focused = true;
                    model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
                }
            }
        }
        model->selected_index = index;
        return;
    }
    column = index % 2;
    if (horizontal < 0 && column > 0) {
        --index;
    } else if (horizontal > 0 && column == 0 && index + 1 < count) {
        ++index;
    }
    {
        int previous_row = index / 2;
        if (vertical != 0) {
        row = index / 2;
        column = index % 2;
        row_count = (count + 1) / 2;
        row = (row + (vertical > 0 ? 1 : row_count - 1)) % row_count;
        target = row * 2 + column;
        if (target >= count) {
            target = row * 2;
        }
            index = target;
        }
        model->selected_index = index;
        if (vertical > 0 && previous_row == row_count - 1) {
            model->parent_content_selection = index;
            model->parent_footer_focused = true;
            model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
        }
    }
}

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

PtcRuleMode ptc_ui_next_rule_mode(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_LIMIT:
        return PTC_RULE_MODE_UNLIMITED;
    case PTC_RULE_MODE_UNLIMITED:
    default:
        return PTC_RULE_MODE_LIMIT;
    }
}

bool ptc_ui_day_rule_effectively_changed(PtcDayRule before, PtcDayRule after)
{
    if (before.mode != after.mode) {
        return true;
    }
    return before.mode == PTC_RULE_MODE_LIMIT && before.minutes != after.minutes;
}

bool ptc_ui_weekly_today_changed(const PtcUiModel *model)
{
    uint8_t weekday;
    if (!model) {
        return false;
    }
    weekday = ptc_weekday_from_day_index(model->day_index);
    return ptc_ui_day_rule_effectively_changed(
        model->current_week[weekday], model->draft_week[weekday]);
}

bool ptc_ui_limit_minutes_would_restrict(const PtcUiModel *model, uint16_t minutes)
{
    return model && model->played_minutes_available && model->played_minutes >= 0 &&
        minutes <= (uint16_t)model->played_minutes;
}

bool ptc_ui_today_limit_requires_hold(const PtcUiModel *model, uint16_t minutes)
{
    return model && (model->unrestricted_today == 1 ||
        !model->played_minutes_available || model->played_minutes < 0 ||
        ptc_ui_limit_minutes_would_restrict(model, minutes));
}

void ptc_ui_format_today_limit_confirmation(
    const PtcUiModel *model,
    char *risk,
    size_t risk_size,
    char *recovery,
    size_t recovery_size)
{
    if (risk && risk_size > 0) {
        if (!model || !model->played_minutes_available || model->played_minutes < 0) {
            snprintf(risk, risk_size,
                     "风险：无法取得额度消耗估算，设置后可能立即进入时间限制");
        } else if (ptc_ui_limit_minutes_would_restrict(model, model->draft_minutes)) {
            snprintf(risk, risk_size,
                     "风险：新额度不高于额度消耗估算，设置后会立即进入时间限制");
        } else if (model->unrestricted_today == 1) {
            snprintf(risk, risk_size,
                     "提示：今天将从不限时改为限时，请确认修改后剩余时间");
        } else {
            snprintf(risk, risk_size, "提示：请确认今天的实时状态和修改结果");
        }
    }
    if (recovery && recovery_size > 0) {
        snprintf(recovery, recovery_size,
                 "解除：选择“今日不限时”“临时加时”，或兑换加时码");
    }
}

bool ptc_ui_day_rule_would_restrict(const PtcUiModel *model, PtcDayRule rule)
{
    return rule.mode == PTC_RULE_MODE_LIMIT && ptc_ui_limit_minutes_would_restrict(model, rule.minutes);
}

bool ptc_ui_setup_takeover_complete(const PtcUiModel *model)
{
    return model &&
        (strcmp(model->setup_phase, "released") == 0 ||
         (strcmp(model->setup_phase, "active") == 0 && !model->disable_flag_present));
}

bool ptc_ui_runtime_fingerprint_reconfirmation_needed(const PtcUiModel *model)
{
    return model && model->disable_flag_present &&
        strcmp(model->setup_phase, "protection") == 0 &&
        strcmp(model->disable_reason, "runtime_fingerprint_changed") == 0;
}

int64_t ptc_ui_setup_grace_remaining(const PtcUiModel *model, int64_t now)
{
    if (!model || strcmp(model->setup_phase, "released") != 0 || model->setup_activate_after <= 0) {
        return -1;
    }
    if (now >= model->setup_activate_after) {
        return 0;
    }
    return model->setup_activate_after - now;
}

bool ptc_ui_cancel_overlay(PtcUiModel *model)
{
    bool clear_pending_code;
    if (!model || model->overlay == PTC_UI_OVERLAY_NONE) {
        return false;
    }
    if (model->overlay == PTC_UI_OVERLAY_SCHEDULED && ptc_ui_scheduled_dirty(model)) {
        model->overlay = PTC_UI_OVERLAY_SCHEDULED_LEAVE;
        return true;
    }
    if (model->overlay == PTC_UI_OVERLAY_SCHEDULED_LEAVE) {
        model->overlay = PTC_UI_OVERLAY_SCHEDULED;
        return true;
    }
    clear_pending_code = model->operation == PTC_UI_OPERATION_REDEEM_OFFLINE_CODE ||
        model->overlay == PTC_UI_OVERLAY_CODE_RESULT;
    if (model->overlay == PTC_UI_OVERLAY_PIN) {
        ptc_ui_pin_finish(model);
    } else if (model->overlay == PTC_UI_OVERLAY_NUMPAD ||
        model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
        ptc_ui_numpad_finish(model);
    } else if (model->overlay == PTC_UI_OVERLAY_CONFIRM &&
               model->confirm_return_overlay != PTC_UI_OVERLAY_NONE) {
        model->overlay = model->confirm_return_overlay;
        model->confirm_return_overlay = PTC_UI_OVERLAY_NONE;
        snprintf(model->overlay_title, sizeof(model->overlay_title), "%s", model->confirm_return_title);
        snprintf(model->overlay_body, sizeof(model->overlay_body), "%s", model->confirm_return_body);
        model->confirm_return_title[0] = '\0';
        model->confirm_return_body[0] = '\0';
        model->operation = PTC_UI_OPERATION_NONE;
    } else if (model->overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
        model->overlay = PTC_UI_OVERLAY_CREDENTIAL;
        snprintf(model->overlay_title, sizeof(model->overlay_title), "%s",
                 model->credential_kind == 1 ? "管理加时码设备名" : "管理加时码密钥");
        snprintf(model->overlay_body, sizeof(model->overlay_body), "%s",
                 model->credential_kind == 1
                    ? "当前值只读；可手工输入或随机生成新设备名。"
                    : "当前密钥默认遮挡；建议使用随机生成的 64 位十六进制密钥。");
    } else {
        model->overlay = PTC_UI_OVERLAY_NONE;
        model->confirm_return_overlay = PTC_UI_OVERLAY_NONE;
        model->confirm_return_title[0] = '\0';
        model->confirm_return_body[0] = '\0';
        model->overlay_title[0] = '\0';
        model->overlay_body[0] = '\0';
        model->operation = PTC_UI_OPERATION_NONE;
    }
    if (clear_pending_code) model->pending_code[0] = '\0';
    return true;
}

PtcUiOperation ptc_ui_take_confirmed_operation(PtcUiModel *model)
{
    PtcUiOperation operation;
    if (!model || model->overlay != PTC_UI_OVERLAY_CONFIRM) {
        return PTC_UI_OPERATION_NONE;
    }
    operation = model->operation;
    model->overlay = PTC_UI_OVERLAY_NONE;
    model->confirm_return_overlay = PTC_UI_OVERLAY_NONE;
    model->confirm_return_title[0] = '\0';
    model->confirm_return_body[0] = '\0';
    model->overlay_title[0] = '\0';
    model->overlay_body[0] = '\0';
    model->operation = PTC_UI_OPERATION_NONE;
    return operation;
}

void ptc_ui_set_execution(PtcUiModel *model, const char *command_name, const char *transport_label)
{
    char command_copy[sizeof(model->command_name)];
    char transport_copy[sizeof(model->transport_label)];
    if (!model) {
        return;
    }
    snprintf(
        command_copy,
        sizeof(command_copy),
        "%s",
        command_name && command_name[0] ? command_name : "未开始");
    snprintf(
        transport_copy,
        sizeof(transport_copy),
        "%s",
        transport_label && transport_label[0] ? transport_label : "传输：未开始");
    snprintf(
        model->command_name,
        sizeof(model->command_name),
        "%s",
        command_copy);
    snprintf(
        model->transport_label,
        sizeof(model->transport_label),
        "%s",
        transport_copy);
}

const char *ptc_ui_support_problem(const PtcUiModel *model)
{
    if (model->waiting || model->apply_pending_confirmation) return "正在确认当前操作";
    if (model->recovery_active) return "有未完成的恢复，需要处理";
    if (ptc_ui_runtime_fingerprint_reconfirmation_needed(model)) return "系统环境已变化，需要重新检测";
    if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0)
        return "安全检查未通过，控制需要修复";
    if (model->disable_flag_present) return "控制已停用";
    if (!model->status_loaded) return "状态尚未读取";
    if (model->error_code) return "最近一次操作未完成";
    if (strcmp(model->setup_phase, "active") != 0) return "首次设置尚未完成";
    return "当前没有待处理的问题";
}

int ptc_ui_support_recommended_action(const PtcUiModel *model)
{
    if (model->waiting || model->apply_pending_confirmation) return -1;
    if (model->recovery_active) {
        return ptc_ui_safety_action_available(model, 1) != PTC_UI_ACTION_DISABLED ? 1 : 4;
    }
    if (ptc_ui_runtime_fingerprint_reconfirmation_needed(model)) return 0;
    if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) return 1;
    if (model->disable_flag_present) return 0;
    if (model->error_code || !model->status_loaded) return 4;
    return ptc_ui_safety_action_available(model, 0) != PTC_UI_ACTION_DISABLED ? 0 : -1;
}

PtcUiActionState ptc_ui_safety_action_available(const PtcUiModel *model, int index)
{
    if (!model) {
        return PTC_UI_ACTION_DISABLED;
    }
    switch (index) {
    case 0:
        return ptc_ui_runtime_fingerprint_reconfirmation_needed(model) || model->disable_flag_present ||
            (strcmp(model->setup_phase, "active") != 0 && strcmp(model->setup_phase, "protection") != 0 &&
             strcmp(model->setup_phase, "failed") != 0)
            ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_DISABLED;
    case 1:
        return strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0 ||
            strcmp(model->setup_phase, "pending") == 0 ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_DISABLED;
    case 2:
        return !model->disable_flag_present && strcmp(model->setup_phase, "protection") != 0 &&
            strcmp(model->setup_phase, "restored") != 0
            ? PTC_UI_ACTION_AVAILABLE : PTC_UI_ACTION_DISABLED;
    case 3:
        return model->setup_snapshot_available ? PTC_UI_ACTION_AVAILABLE : PTC_UI_ACTION_DISABLED;
    case 4:
        return PTC_UI_ACTION_AVAILABLE;
    case 5:
        return PTC_UI_ACTION_AVAILABLE;
    default:
        return PTC_UI_ACTION_DISABLED;
    }
}
bool ptc_ui_safety_action_visible(const PtcUiModel *model, int index)
{
    if (!model) return false;
    return index >= 0 && index < 6;
}

const char *ptc_ui_safety_action_hint(const PtcUiModel *model, int index)
{
    if (!model) {
        return "";
    }
    switch (index) {
    case 0:
        return strcmp(model->setup_phase, "active") == 0 ? "额度管理已启用。" : "预检通过后保存快照并接管系统控制。";
    case 1:
        return strcmp(model->setup_phase, "active") == 0
            ? "当前运行正常，无需执行修复。"
            : "重新执行兼容、快照和恢复前置检查。";
    case 2:
        return model->disable_flag_present
            ? "解除停用后才允许新的控制写入；状态和恢复始终可用。"
            : "只停止新的控制写入；状态、诊断和恢复仍可使用。";
    case 3:
        return model->setup_snapshot_available ? "精确恢复安装前状态。" : "安装前快照不可用。";
    case 4:
        return "导出时自动排除 secret、PIN、离线码和完整 nonce。";
    case 5:
        return "查看 PlayWise 版本、项目仓库和家长网页地址。";
    default:
        return "";
    }
}
