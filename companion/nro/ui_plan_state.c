#include "ui_state.h"
#include "ui_layout.h"
#include "../ui_language.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../common/protocol/error_code.h"
#include "../../common/rules/holiday_calendar.h"
#include "../../common/time/ptc_time.h"

/* Match the displayed row order; imported workdays follow holiday groups. */
static void nearest_calendar_row(uint16_t today, uint16_t first, uint16_t last,
    size_t row, int *best_distance, uint16_t *best_day, int *page)
{
    uint16_t nearest = today < first ? first : today > last ? last : today;
    int distance = abs((int)nearest - today);
    if (distance < *best_distance ||
        (distance == *best_distance && nearest > *best_day)) {
        *best_distance = distance;
        *best_day = nearest;
        *page = (int)(row / 4u);
    }
}

int ptc_ui_holiday_calendar_nearest_page(const PtcUiModel *model, int *out_distance)
{
    int distance = INT_MAX, page = 0;
    uint16_t best_day = 0, jan1;
    size_t i;
    if (out_distance) *out_distance = INT_MAX;
    if (!model) return 0;
    if (model->holiday_calendar_data) {
        const PtcImportedCalendarYear *data = model->holiday_calendar_data;
        size_t row = data->group_count;
        for (i = 0; i < data->group_count; ++i)
            nearest_calendar_row(model->day_index, data->groups[i].first_day_index,
                data->groups[i].last_day_index, i, &distance, &best_day, &page);
        if (!ptc_day_index_from_date(data->year, 1, 1, &jan1)) return page;
        for (i = 0; i < 366u; ++i) {
            if (data->days[i] != PTC_CALENDAR_DAY_MAKEUP_WORKDAY) continue;
            nearest_calendar_row(model->day_index, (uint16_t)(jan1 + i),
                (uint16_t)(jan1 + i), row++, &distance, &best_day, &page);
        }
    } else if (model->calendar_builtin) {
        uint16_t year = ptc_holiday_calendar_info()->last_year;
        PtcHolidayCalendarMatch match;
        for (i = 0; i < ptc_holiday_calendar_arrangement_count(year); ++i) {
            const PtcHolidayArrangement *entry = ptc_holiday_calendar_arrangement(year, i);
            uint16_t first, last;
            if (ptc_day_index_from_date(year, entry->start_month, entry->start_day, &first) &&
                ptc_day_index_from_date(year, entry->end_month, entry->end_day, &last))
                nearest_calendar_row(model->day_index, first, last, i,
                    &distance, &best_day, &page);
        }
        if (!ptc_day_index_from_date(year, 1, 1, &jan1)) return page;
        while (ptc_holiday_calendar_find(PTC_CALENDAR_DAY_MAKEUP_WORKDAY, jan1, &match)) {
            for (i = 0; i < ptc_holiday_calendar_arrangement_count(year); ++i)
                if (match.arrangement == ptc_holiday_calendar_arrangement(year, i))
                    nearest_calendar_row(model->day_index, match.day_index, match.day_index,
                        i, &distance, &best_day, &page);
            if (match.day_index == UINT16_MAX) break;
            jan1 = (uint16_t)(match.day_index + 1u);
        }
    }
    if (out_distance) *out_distance = distance;
    return page;
}
static void copy_ui_text(PtcUiTextId id, char *out, size_t size)
{
    if (out && size) snprintf(out, size, "%s", ptc_ui_text(id));
}

static void format_ui_text_number(PtcUiTextId id, const char *name, int value,
                                  char *out, size_t size)
{
    PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER(name, value)};
    (void)ptc_ui_text_format(id, out, size, args, 1);
}

static void format_ui_text_string(PtcUiTextId id, const char *name, const char *value,
                                  char *out, size_t size)
{
    PtcUiTextArg args[] = {PTC_UI_TEXT_STRING(name, value)};
    (void)ptc_ui_text_format(id, out, size, args, 1);
}
bool ptc_ui_bedtime_section_dirty(const PtcUiModel *model, PtcUiBedtimeSection section)
{
    const PtcBedtimePolicy *draft;
    const PtcBedtimePolicy *saved;
    if (!model) return false;
    draft = &model->draft_bedtime_policy;
    saved = &model->bedtime_policy;
    if (draft->enabled != saved->enabled) return true;
    switch (section) {
    case PTC_UI_BEDTIME_WEEKLY:
        return memcmp(draft->week, saved->week, sizeof(saved->week)) != 0;
    case PTC_UI_BEDTIME_CALENDAR:
        return draft->calendar_enabled != saved->calendar_enabled ||
            memcmp(&draft->holiday_rule, &saved->holiday_rule, sizeof(saved->holiday_rule)) != 0 ||
            memcmp(&draft->makeup_workday_rule, &saved->makeup_workday_rule,
                sizeof(saved->makeup_workday_rule)) != 0;
    case PTC_UI_BEDTIME_SCHEDULED:
        return memcmp(&draft->scheduled_override, &saved->scheduled_override,
            sizeof(saved->scheduled_override)) != 0;
    default:
        return false;
    }
}

PtcBedtimePolicy ptc_ui_bedtime_section_policy(const PtcUiModel *model,
    PtcUiBedtimeSection section)
{
    PtcBedtimePolicy policy = model->bedtime_policy;
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    policy.enabled = draft->enabled;
    switch (section) {
    case PTC_UI_BEDTIME_WEEKLY:
        memcpy(policy.week, draft->week, sizeof(policy.week));
        break;
    case PTC_UI_BEDTIME_CALENDAR:
        policy.calendar_enabled = draft->calendar_enabled;
        policy.holiday_rule = draft->holiday_rule;
        policy.makeup_workday_rule = draft->makeup_workday_rule;
        break;
    case PTC_UI_BEDTIME_SCHEDULED:
        policy.scheduled_override = draft->scheduled_override;
        break;
    default:
        break;
    }
    return policy;
}

void ptc_ui_bedtime_discard_section(PtcUiModel *model, PtcUiBedtimeSection section)
{
    PtcBedtimePolicy *draft;
    const PtcBedtimePolicy *saved;
    if (!model) return;
    draft = &model->draft_bedtime_policy;
    saved = &model->bedtime_policy;
    draft->enabled = saved->enabled;
    switch (section) {
    case PTC_UI_BEDTIME_WEEKLY:
        memcpy(draft->week, saved->week, sizeof(saved->week));
        break;
    case PTC_UI_BEDTIME_CALENDAR:
        draft->calendar_enabled = saved->calendar_enabled;
        draft->holiday_rule = saved->holiday_rule;
        draft->makeup_workday_rule = saved->makeup_workday_rule;
        break;
    case PTC_UI_BEDTIME_SCHEDULED:
        draft->scheduled_override = saved->scheduled_override;
        break;
    default:
        break;
    }
}

PtcUiBedtimeImpact ptc_ui_bedtime_save_impact(const PtcUiModel *model,
    uint16_t minute_of_day, int64_t now)
{
    PtcRules rules;
    PtcBedtimeEvaluation evaluation;
    if (!model || minute_of_day >= 1440) return PTC_UI_BEDTIME_IMPACT_NONE;
    memset(&rules, 0, sizeof(rules));
    rules.bedtime = ptc_ui_bedtime_section_policy(model, model->bedtime_section);
    if (!ptc_bedtime_policy_is_valid(&rules.bedtime) || !rules.bedtime.enabled)
        return PTC_UI_BEDTIME_IMPACT_NONE;
    evaluation = ptc_bedtime_evaluate(
        &rules,
        model->day_index,
        ptc_weekday_from_day_index(model->day_index),
        minute_of_day);
    if (!evaluation.active) return PTC_UI_BEDTIME_IMPACT_NONE;
    if (!ptc_ui_status_is_fresh(model, now)) return PTC_UI_BEDTIME_IMPACT_UNKNOWN;
    if (model->bedtime_active && !model->bedtime_skipped) return PTC_UI_BEDTIME_IMPACT_NONE;
    if (model->bedtime_skipped_window_available &&
        model->bedtime_skipped_window_instance_id == evaluation.window_instance_id)
        return PTC_UI_BEDTIME_IMPACT_SKIPPED;
    if (model->bedtime_active && model->bedtime_skipped &&
        model->bedtime_window_instance_id == evaluation.window_instance_id)
        return PTC_UI_BEDTIME_IMPACT_SKIPPED;
    return PTC_UI_BEDTIME_IMPACT_RESTRICT;
}

bool ptc_ui_bedtime_skip_matches_policy(const PtcUiModel *model,
    const PtcBedtimePolicy *policy)
{
    PtcRules rules;
    PtcEffectiveBedtime effective;
    uint16_t start_day;
    if (!model || !policy || !policy->enabled ||
        !model->bedtime_skipped_window_available) return false;
    start_day = model->bedtime_skipped_start_day_index;
    memset(&rules, 0, sizeof(rules));
    rules.bedtime = *policy;
    effective = ptc_bedtime_resolve_start_day(&rules, start_day,
        ptc_weekday_from_day_index(start_day));
    return effective.window.enabled &&
        model->bedtime_skipped_window_instance_id ==
            ptc_bedtime_window_instance_id(start_day, effective.window.start_minute) &&
        model->bedtime_skipped_start_minute == effective.window.start_minute;
}

void ptc_ui_format_bedtime_quota_notice(const PtcUiModel *model, int64_t now,
    char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!model) return;
    if (!ptc_ui_status_is_fresh(model, now)) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_BEDTIME_NOTICE_PENDING));
    } else if (model->bedtime_active && !model->bedtime_skipped) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_BEDTIME_NOTICE_ACTIVE));
    } else if (model->bedtime_active && model->bedtime_skipped) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_BEDTIME_NOTICE_SKIPPED));
    } else if (model->bedtime_next_available &&
               model->bedtime_next_start_day_index == model->day_index) {
        char time_text[8];
        snprintf(time_text, sizeof(time_text), "%02u:%02u",
            (unsigned)(model->bedtime_next_start_minute / 60),
            (unsigned)(model->bedtime_next_start_minute % 60));
        PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("time", time_text)};
        (void)ptc_ui_text_format(PTC_UI_T_BEDTIME_NOTICE_TONIGHT, out, out_size, args, 1);
    }
}

const char *ptc_ui_effective_rule_label(PtcRuleSource source)
{
    switch (source) {
    case PTC_RULE_SOURCE_STATUTORY_HOLIDAY: return ptc_ui_text(PTC_UI_T_RULE_HOLIDAY);
    case PTC_RULE_SOURCE_MAKEUP_WORKDAY: return ptc_ui_text(PTC_UI_T_RULE_MAKEUP);
    case PTC_RULE_SOURCE_TODAY_OVERRIDE: return ptc_ui_text(PTC_UI_T_RULE_TODAY);
    case PTC_RULE_SOURCE_SCHEDULED_OVERRIDE: return ptc_ui_text(PTC_UI_T_RULE_SCHEDULED);
    case PTC_RULE_SOURCE_WEEKLY:
    default: return ptc_ui_text(PTC_UI_T_RULE_WEEKLY);
    }
}
#define effective_rule_label ptc_ui_effective_rule_label

static void format_rule_basis(PtcDayRule rule, int played_minutes, bool played_available,
                              char *out, size_t out_size)
{
    int remaining;
    if (!out || out_size == 0) return;
    if (rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    } else if (!played_available || played_minutes < 0) {
        PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("quota", rule.minutes)};
        (void)ptc_ui_text_format(PTC_UI_T_BASIS_UNKNOWN, out, out_size, args, 1);
    } else {
        remaining = (int)rule.minutes - played_minutes;
        if (remaining < 0) remaining = 0;
        PtcUiTextArg args[] = {
            PTC_UI_TEXT_NUMBER("quota", rule.minutes),
            PTC_UI_TEXT_NUMBER("used", played_minutes),
            PTC_UI_TEXT_NUMBER("remaining", remaining)
        };
        (void)ptc_ui_text_format(PTC_UI_T_BASIS_REMAINING, out, out_size, args, 3);
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
    rules->calendar = model->calendar;
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
    case PTC_UI_DECISION_SELECTED: return ptc_ui_text(PTC_UI_T_DECISION_SELECTED);
    case PTC_UI_DECISION_OVERRIDDEN: return ptc_ui_text(PTC_UI_T_DECISION_OVERRIDDEN);
    case PTC_UI_DECISION_NOT_CONFIGURED: return ptc_ui_text(PTC_UI_T_DECISION_NOT_CONFIGURED);
    case PTC_UI_DECISION_NOT_MATCHED: return ptc_ui_text(PTC_UI_T_DECISION_NOT_MATCHED);
    case PTC_UI_DECISION_DISABLED: return ptc_ui_text(PTC_UI_T_DECISION_DISABLED);
    case PTC_UI_DECISION_CALENDAR_UNCOVERED: return ptc_ui_text(PTC_UI_T_DECISION_UNCOVERED);
    case PTC_UI_DECISION_UNKNOWN:
    default: return ptc_ui_text(PTC_UI_T_DECISION_UNKNOWN);
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
        const char *reason = ptc_ui_text(PTC_UI_T_REASON_NO_STATUS);
        set_decision_step(&decision->today_override, PTC_UI_DECISION_UNKNOWN, empty, reason);
        set_decision_step(&decision->scheduled_override, PTC_UI_DECISION_UNKNOWN, empty, reason);
        set_decision_step(&decision->holiday, PTC_UI_DECISION_UNKNOWN, empty, reason);
        set_decision_step(&decision->weekly, PTC_UI_DECISION_UNKNOWN, empty, reason);
        snprintf(decision->final_reason, sizeof(decision->final_reason), "%s",
                 ptc_ui_text(PTC_UI_T_REASON_REFRESH));
        copy_ui_text(PTC_UI_T_BEDTIME_STATUS_PENDING, decision->bedtime, sizeof(decision->bedtime));
        copy_ui_text(PTC_UI_T_AUTONOMY_STATUS_PENDING, decision->autonomy, sizeof(decision->autonomy));
        return;
    }
    is_today = (day_index == model->day_index);
    build_plan_rules(model, kind, &rules);
    weekday = ptc_weekday_from_day_index(day_index);
    decision->effective = ptc_rules_resolve(&rules, day_index, weekday);
    day_type = ptc_holiday_calendar_classify_in(model->calendar, day_index, &calendar_covered);
    scheduled_matches = rules.scheduled_override.enabled &&
        day_index >= rules.scheduled_override.start_day_index &&
        day_index <= rules.scheduled_override.end_day_index;
    holiday_matches = rules.holiday_enabled && calendar_covered &&
        (day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY ||
         day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY);

    if (!is_today) {
        set_decision_step(&decision->today_override, PTC_UI_DECISION_NOT_CONFIGURED, empty,
                          ptc_ui_text(PTC_UI_T_REASON_TODAY_ONLY));
    } else if (!rules.today_override.present) {
        set_decision_step(&decision->today_override, PTC_UI_DECISION_NOT_CONFIGURED, empty,
                           ptc_ui_text(model->today_override_cleared_in_session
                               ? PTC_UI_T_REASON_JUST_CLEARED : PTC_UI_T_REASON_NO_TODAY));
    } else {
        set_decision_step(&decision->today_override,
            decision->effective.source == PTC_RULE_SOURCE_TODAY_OVERRIDE
                ? PTC_UI_DECISION_SELECTED : PTC_UI_DECISION_OVERRIDDEN,
            rules.today_override.rule, ptc_ui_text(PTC_UI_T_REASON_HAS_TODAY));
    }

    if (!rules.scheduled_override.enabled) {
        set_decision_step(&decision->scheduled_override, PTC_UI_DECISION_NOT_CONFIGURED, empty,
                          ptc_ui_text(PTC_UI_T_REASON_SCHEDULE_DISABLED));
    } else if (!scheduled_matches) {
        set_decision_step(&decision->scheduled_override, PTC_UI_DECISION_NOT_MATCHED,
                          rules.scheduled_override.rule, ptc_ui_text(is_today
                              ? PTC_UI_T_REASON_SCHEDULE_OUT_TODAY : PTC_UI_T_REASON_SCHEDULE_OUT_DAY));
    } else {
        set_decision_step(&decision->scheduled_override,
            decision->effective.source == PTC_RULE_SOURCE_SCHEDULED_OVERRIDE
                ? PTC_UI_DECISION_SELECTED : PTC_UI_DECISION_OVERRIDDEN,
            rules.scheduled_override.rule,
            ptc_ui_text(decision->effective.source == PTC_RULE_SOURCE_TODAY_OVERRIDE
                ? PTC_UI_T_REASON_SCHEDULE_OVERRIDDEN : (is_today
                    ? PTC_UI_T_REASON_SCHEDULE_MATCH_TODAY : PTC_UI_T_REASON_SCHEDULE_MATCH_DAY)));
    }

    if (!rules.holiday_enabled) {
        set_decision_step(&decision->holiday, PTC_UI_DECISION_DISABLED, empty,
                          ptc_ui_text(PTC_UI_T_REASON_HOLIDAY_DISABLED));
    } else if (!calendar_covered) {
        set_decision_step(&decision->holiday, PTC_UI_DECISION_CALENDAR_UNCOVERED, empty,
                          ptc_ui_text(is_today ? PTC_UI_T_REASON_CALENDAR_OUT_TODAY
                                               : PTC_UI_T_REASON_CALENDAR_OUT_DAY));
    } else if (!holiday_matches) {
        set_decision_step(&decision->holiday, PTC_UI_DECISION_NOT_MATCHED, empty,
                          ptc_ui_text(is_today ? PTC_UI_T_REASON_NORMAL_TODAY
                                               : PTC_UI_T_REASON_NORMAL_DAY));
    } else {
        PtcDayRule holiday_rule = day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY
            ? rules.holiday_rule : rules.makeup_workday_rule;
        const char *hit = day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY
            ? ptc_ui_text(PTC_UI_T_REASON_HOLIDAY_MATCH)
            : ptc_ui_text(PTC_UI_T_REASON_MAKEUP_MATCH);
        set_decision_step(&decision->holiday,
            decision->effective.source == PTC_RULE_SOURCE_STATUTORY_HOLIDAY ||
            decision->effective.source == PTC_RULE_SOURCE_MAKEUP_WORKDAY
                ? PTC_UI_DECISION_SELECTED : PTC_UI_DECISION_OVERRIDDEN,
            holiday_rule, hit);
        if (decision->holiday.state == PTC_UI_DECISION_OVERRIDDEN) {
            PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("rule", hit)};
            (void)ptc_ui_text_format(PTC_UI_T_REASON_OVERRIDDEN,
                decision->holiday.reason, sizeof(decision->holiday.reason), args, 1);
        }
    }

    set_decision_step(&decision->weekly,
        decision->effective.source == PTC_RULE_SOURCE_WEEKLY
            ? PTC_UI_DECISION_SELECTED : PTC_UI_DECISION_OVERRIDDEN,
        rules.week[weekday], ptc_ui_text(decision->effective.source == PTC_RULE_SOURCE_WEEKLY
            ? PTC_UI_T_REASON_WEEKLY_SELECTED : (is_today
                ? PTC_UI_T_REASON_WEEKLY_BASE_TODAY : PTC_UI_T_REASON_WEEKLY_BASE_DAY)));

    PtcUiTextId final_id = decision->effective.rule.mode == PTC_RULE_MODE_UNLIMITED
        ? (is_today ? PTC_UI_T_REASON_FINAL_UNLIMITED_TODAY : PTC_UI_T_REASON_FINAL_UNLIMITED_DAY)
        : PTC_UI_T_REASON_FINAL;
    PtcUiTextArg final_args[] = {
        PTC_UI_TEXT_STRING("source", effective_rule_label(decision->effective.source))
    };
    (void)ptc_ui_text_format(final_id, decision->final_reason,
                              sizeof(decision->final_reason), final_args, 1);

    if (is_today) {
        PtcUiTextId bedtime_id;
        if (!model->bedtime_policy.enabled) {
            bedtime_id = PTC_UI_T_BEDTIME_STATUS_OFF;
        } else if (model->bedtime_active && model->bedtime_skipped) {
            bedtime_id = PTC_UI_T_BEDTIME_STATUS_SKIPPED;
        } else if (model->bedtime_active) {
            bedtime_id = PTC_UI_T_BEDTIME_STATUS_ACTIVE;
        } else if (model->bedtime_next_available) {
            bedtime_id = PTC_UI_T_BEDTIME_STATUS_WAITING;
        } else {
            bedtime_id = PTC_UI_T_BEDTIME_STATUS_NO_WINDOW;
        }
        copy_ui_text(bedtime_id, decision->bedtime, sizeof(decision->bedtime));
        if (model->daily_buffer_minutes == 0) {
            copy_ui_text(PTC_UI_T_AUTONOMY_STATUS_OFF, decision->autonomy, sizeof(decision->autonomy));
        } else if (model->daily_buffer_claimed) {
            format_ui_text_number(PTC_UI_T_AUTONOMY_STATUS_CLAIMED, "minutes",
                model->daily_buffer_minutes, decision->autonomy, sizeof(decision->autonomy));
        } else if (model->daily_buffer_available) {
            format_ui_text_number(PTC_UI_T_AUTONOMY_STATUS_AVAILABLE, "minutes",
                model->daily_buffer_minutes, decision->autonomy, sizeof(decision->autonomy));
        } else {
            copy_ui_text(PTC_UI_T_AUTONOMY_STATUS_UNAVAILABLE, decision->autonomy, sizeof(decision->autonomy));
        }
    } else {
        PtcEffectiveBedtime bt = ptc_bedtime_resolve_start_day(&rules, day_index, weekday);
        if (!rules.bedtime.enabled || !bt.window.enabled) {
            copy_ui_text(PTC_UI_T_BEDTIME_STATUS_NONE_DAY, decision->bedtime, sizeof(decision->bedtime));
        } else {
            PtcUiTextId source_id = bt.source == PTC_BEDTIME_SOURCE_SCHEDULED_OVERRIDE
                ? PTC_UI_T_BEDTIME_SOURCE_SPECIAL : bt.source == PTC_BEDTIME_SOURCE_STATUTORY_HOLIDAY
                ? PTC_UI_T_BEDTIME_SOURCE_HOLIDAY : bt.source == PTC_BEDTIME_SOURCE_MAKEUP_WORKDAY
                ? PTC_UI_T_BEDTIME_SOURCE_WORKDAY : PTC_UI_T_BEDTIME_SOURCE_WEEKLY;
            char time_text[8];
            snprintf(time_text, sizeof(time_text), "%02u:%02u",
                (unsigned int)(bt.window.start_minute / 60),
                (unsigned int)(bt.window.start_minute % 60));
            PtcUiTextArg args[] = {
                PTC_UI_TEXT_STRING("time", time_text),
                PTC_UI_TEXT_STRING("source", ptc_ui_text(source_id))
            };
            (void)ptc_ui_text_format(PTC_UI_T_BEDTIME_STATUS_START,
                decision->bedtime, sizeof(decision->bedtime), args, 2);
        }
        if (model->daily_buffer_minutes == 0) {
            copy_ui_text(PTC_UI_T_AUTONOMY_STATUS_OFF, decision->autonomy, sizeof(decision->autonomy));
        } else {
            format_ui_text_number(PTC_UI_T_AUTONOMY_STATUS_DAILY, "minutes",
                model->daily_buffer_minutes, decision->autonomy, sizeof(decision->autonomy));
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
        copy_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING, badge, badge_size);
        copy_ui_text(PTC_UI_T_ADJUST_DETAIL_PENDING, detail, detail_size);
        return;
    }
    ptc_ui_build_today_decision(model, PTC_UI_PLAN_SAVED, now, &decision);
    source = effective_rule_label(decision.effective.source);
    if (model->disable_flag_present) {
        copy_ui_text(PTC_UI_T_ADJUST_BADGE_DISABLED, badge, badge_size);
        copy_ui_text(PTC_UI_T_ADJUST_DETAIL_DISABLED, detail, detail_size);
    } else if (model->recovery_active) {
        copy_ui_text(PTC_UI_T_ADJUST_BADGE_RECOVERING, badge, badge_size);
        copy_ui_text(PTC_UI_T_ADJUST_DETAIL_RECOVERING, detail, detail_size);
    } else if (model->apply_pending_confirmation) {
        copy_ui_text(PTC_UI_T_ADJUST_BADGE_WAITING, badge, badge_size);
        copy_ui_text(PTC_UI_T_ADJUST_DETAIL_WAITING, detail, detail_size);
    } else if (model->temporary_unlocked_available && model->temporary_unlocked) {
        copy_ui_text(PTC_UI_T_ADJUST_BADGE_PAUSED, badge, badge_size);
        copy_ui_text(PTC_UI_T_ADJUST_DETAIL_PAUSED, detail, detail_size);
    } else if (model->today_override_present &&
               decision.effective.source == PTC_RULE_SOURCE_TODAY_OVERRIDE) {
        if (model->bedtime_active && !model->bedtime_skipped) {
            copy_ui_text(PTC_UI_T_ADJUST_BADGE_BEDTIME, badge, badge_size);
            if (model->today_override_rule.mode == PTC_RULE_MODE_UNLIMITED)
                copy_ui_text(PTC_UI_T_ADJUST_DETAIL_BEDTIME_UNLIMITED, detail, detail_size);
            else
                format_ui_text_number(PTC_UI_T_ADJUST_DETAIL_BEDTIME_MINUTES, "minutes",
                    model->today_override_rule.minutes, detail, detail_size);
        } else {
            copy_ui_text(model->today_override_rule.mode == PTC_RULE_MODE_UNLIMITED
                ? PTC_UI_T_ADJUST_BADGE_UNLIMITED : PTC_UI_T_ADJUST_BADGE_ACTIVE, badge, badge_size);
            if (model->today_override_rule.mode == PTC_RULE_MODE_UNLIMITED)
                copy_ui_text(PTC_UI_T_ADJUST_DETAIL_UNLIMITED, detail, detail_size);
            else
                format_ui_text_number(PTC_UI_T_ADJUST_DETAIL_MINUTES, "minutes",
                    model->today_override_rule.minutes, detail, detail_size);
        }
    } else if (model->today_override_cleared_in_session) {
        copy_ui_text(PTC_UI_T_ADJUST_BADGE_CLEARED, badge, badge_size);
        if (model->bedtime_active && !model->bedtime_skipped)
            format_ui_text_string(PTC_UI_T_ADJUST_DETAIL_BEDTIME_SOURCE, "source", source, detail, detail_size);
        else
            format_ui_text_string(PTC_UI_T_ADJUST_DETAIL_NOW_SOURCE, "source", source, detail, detail_size);
    } else {
        copy_ui_text(PTC_UI_T_ADJUST_BADGE_NOT_SET, badge, badge_size);
        if (model->bedtime_active && !model->bedtime_skipped)
            format_ui_text_string(PTC_UI_T_ADJUST_DETAIL_BEDTIME_SOURCE, "source", source, detail, detail_size);
        else
            format_ui_text_string(PTC_UI_T_ADJUST_DETAIL_SOURCE, "source", source, detail, detail_size);
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
    PtcUiTextId text_id;
    int remaining = 0;
    if (!out || out_size == 0) return;
    ptc_ui_project_plan_impact(model, kind, true, now, &impact);
    after = impact.after;
    if (!ptc_ui_status_is_fresh(model, now)) {
        if (!impact.quota_changes_today)
            text_id = PTC_UI_T_IMPACT_UNCHANGED_STALE;
        else
            text_id = PTC_UI_T_IMPACT_PENDING;
    } else if (!impact.quota_changes_today) {
        if (impact.source_changes_today) {
            text_id = PTC_UI_T_IMPACT_SOURCE_SWITCH;
        } else if (kind == PTC_UI_PLAN_WEEKLY) {
            text_id = PTC_UI_T_IMPACT_WEEKLY_LATER;
        } else if (kind == PTC_UI_PLAN_HOLIDAY) {
            text_id = PTC_UI_T_IMPACT_HOLIDAY_LATER;
        } else text_id = PTC_UI_T_IMPACT_SCHEDULE_LATER;
    } else if (after.rule.mode == PTC_RULE_MODE_UNLIMITED) {
        text_id = PTC_UI_T_IMPACT_UNLIMITED;
    } else if (!model->played_minutes_available) {
        text_id = PTC_UI_T_IMPACT_REMAINING_UNKNOWN;
    } else {
        remaining = (int)after.rule.minutes - model->played_minutes;
        if (remaining < 0) remaining = 0;
        text_id = PTC_UI_T_IMPACT_REMAINING;
    }
    PtcUiTextArg args[] = {
        PTC_UI_TEXT_STRING("source", effective_rule_label(after.source)),
        PTC_UI_TEXT_NUMBER("quota", after.rule.minutes),
        PTC_UI_TEXT_NUMBER("remaining", remaining)
    };
    (void)ptc_ui_text_format(text_id, out, out_size, args, 3);
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
    rules.calendar = model->calendar;
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
        copy_ui_text(PTC_UI_T_RESTORE_NO_STATUS, out, out_size);
        return;
    }
    after = ptc_ui_rule_after_today_restore(model);
    format_rule_basis(model->today_override_rule, model->played_minutes, model->played_minutes_available,
                      current, sizeof(current));
    format_rule_basis(after.rule, model->played_minutes, model->played_minutes_available,
                      restored, sizeof(restored));
    PtcUiTextArg args[] = {
        PTC_UI_TEXT_STRING("current", current),
        PTC_UI_TEXT_STRING("source", effective_rule_label(after.source)),
        PTC_UI_TEXT_STRING("restored", restored)
    };
    (void)ptc_ui_text_format(PTC_UI_T_RESTORE_BASIS, out, out_size, args, 3);
}

void ptc_ui_format_weekly_save_result(const PtcUiModel *model, char *message, size_t message_size,
                                      char *detail, size_t detail_size)
{
    uint8_t weekday;
    char basis[112];
    char current_basis[112];
    const char *current_source;
    const char *detail_source;
    PtcUiTextId message_id;
    PtcUiTextId detail_id;
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
    detail_source = current_source;
    if (!today_changed) {
        message_id = PTC_UI_T_WEEKLY_SAVED_OTHER_DAYS;
        detail_id = PTC_UI_T_WEEKLY_DETAIL_CONTINUE;
    } else if (strcmp(model->rule_source, "today_override") == 0) {
        PtcEffectiveRule restored = ptc_ui_rule_after_today_restore(model);
        message_id = PTC_UI_T_WEEKLY_SAVED_TODAY_OVERRIDE;
        if (restored.source == PTC_RULE_SOURCE_WEEKLY) {
            detail_id = PTC_UI_T_WEEKLY_DETAIL_AFTER_CLEAR;
        } else {
            detail_id = PTC_UI_T_WEEKLY_DETAIL_RESTORED_SOURCE;
            detail_source = effective_rule_label(restored.source);
        }
    } else if (strcmp(model->rule_source, "scheduled_override") == 0) {
        message_id = PTC_UI_T_WEEKLY_SAVED_SCHEDULED;
        detail_id = PTC_UI_T_WEEKLY_DETAIL_SCHEDULED;
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0 ||
               strcmp(model->rule_source, "makeup_workday") == 0) {
        message_id = PTC_UI_T_WEEKLY_SAVED_HOLIDAY;
        detail_id = PTC_UI_T_WEEKLY_DETAIL_HOLIDAY;
    } else {
        message_id = PTC_UI_T_WEEKLY_SAVED_EFFECTIVE;
        detail_id = PTC_UI_T_WEEKLY_DETAIL_EFFECTIVE;
    }
    PtcUiTextArg args[] = {
        PTC_UI_TEXT_STRING("source", detail_source),
        PTC_UI_TEXT_STRING("current", current_basis),
        PTC_UI_TEXT_STRING("basis", basis)
    };
    (void)ptc_ui_text_format(message_id, message, message_size, args, 3);
    (void)ptc_ui_text_format(detail_id, detail, detail_size, args, 3);
}

void ptc_ui_format_holiday_save_result(const PtcUiModel *model, char *message, size_t message_size,
                                       char *detail, size_t detail_size)
{
    const char *source;
    PtcDayRule active;
    char basis[112];
    uint8_t weekday;
    PtcUiTextId message_id;
    PtcUiTextId detail_id;
    if (!model || !message || message_size == 0 || !detail || detail_size == 0) return;
    detail[0] = '\0';
    weekday = ptc_weekday_from_day_index(model->day_index);
    if (strcmp(model->rule_source, "scheduled_override") == 0) {
        copy_ui_text(PTC_UI_T_HOLIDAY_SAVED_SCHEDULED, message, message_size);
        copy_ui_text(PTC_UI_T_HOLIDAY_DETAIL_SCHEDULED, detail, detail_size);
        return;
    }
    if (strcmp(model->rule_source, "today_override") == 0) {
        format_rule_basis(model->today_override_rule, model->played_minutes,
                          model->played_minutes_available, basis, sizeof(basis));
        copy_ui_text(PTC_UI_T_HOLIDAY_SAVED_TODAY, message, message_size);
        copy_ui_text(PTC_UI_T_HOLIDAY_DETAIL_TODAY, detail, detail_size);
        return;
    }
    if (strcmp(model->rule_source, "statutory_holiday") == 0) {
        source = ptc_ui_text(PTC_UI_T_RULE_HOLIDAY);
        active = model->draft_holiday_rule;
    } else if (strcmp(model->rule_source, "makeup_workday") == 0) {
        source = ptc_ui_text(PTC_UI_T_RULE_MAKEUP);
        active = model->draft_makeup_workday_rule;
    } else {
        source = ptc_ui_text(PTC_UI_T_RULE_WEEKLY);
        active = model->draft_week[weekday];
    }
    format_rule_basis(active, model->played_minutes, model->played_minutes_available, basis, sizeof(basis));
    if (!model->draft_holiday_enabled) {
        message_id = PTC_UI_T_HOLIDAY_SAVED_DISABLED;
        detail_id = PTC_UI_T_HOLIDAY_DETAIL_DISABLED;
    } else if (!model->calendar_covered) {
        message_id = PTC_UI_T_HOLIDAY_SAVED_UNCOVERED;
        detail_id = PTC_UI_T_HOLIDAY_DETAIL_UNCOVERED;
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0 ||
               strcmp(model->rule_source, "makeup_workday") == 0) {
        message_id = PTC_UI_T_HOLIDAY_SAVED_EFFECTIVE;
        detail_id = PTC_UI_T_HOLIDAY_DETAIL_EFFECTIVE;
    } else {
        message_id = PTC_UI_T_HOLIDAY_SAVED_REGULAR;
        detail_id = PTC_UI_T_HOLIDAY_DETAIL_REGULAR;
    }
    PtcUiTextArg args[] = {
        PTC_UI_TEXT_STRING("source", source),
        PTC_UI_TEXT_STRING("basis", basis)
    };
    (void)ptc_ui_text_format(message_id, message, message_size, args, 2);
    (void)ptc_ui_text_format(detail_id, detail, detail_size, args, 2);
}
