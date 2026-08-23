#include "rules.h"

#include "holiday_calendar.h"

void ptc_rules_default(PtcRules *rules)
{
    unsigned int i;
    for (i = 0; i < 7; ++i) {
        rules->week[i].mode = (i == 0 || i == 6) ? PTC_RULE_MODE_UNLIMITED : PTC_RULE_MODE_LIMIT;
        rules->week[i].minutes = (i == 0 || i == 6) ? 120 : 60;
    }
    rules->today_override.present = false;
    rules->today_override.day_index = 0;
    rules->today_override.rule.mode = PTC_RULE_MODE_LIMIT;
    rules->today_override.rule.minutes = 60;
    rules->scheduled_override.enabled = false;
    rules->scheduled_override.start_day_index = 0;
    rules->scheduled_override.end_day_index = 0;
    rules->scheduled_override.rule.mode = PTC_RULE_MODE_LIMIT;
    rules->scheduled_override.rule.minutes = 60;
    rules->autonomy_policy.daily_buffer_minutes = 0;
    rules->holiday_enabled = false;
    rules->holiday_rule.mode = PTC_RULE_MODE_UNLIMITED;
    rules->holiday_rule.minutes = 120;
    rules->makeup_workday_rule.mode = PTC_RULE_MODE_LIMIT;
    rules->makeup_workday_rule.minutes = 60;
}

PtcEffectiveRule ptc_rules_resolve(const PtcRules *rules, uint16_t day_index, uint8_t weekday)
{
    PtcEffectiveRule result;
    bool covered = false;
    PtcCalendarDayType day_type = ptc_holiday_calendar_classify(day_index, &covered);
    result.calendar_covered = covered;
    if (rules->today_override.present && rules->today_override.day_index == day_index) {
        result.rule = rules->today_override.rule;
        result.source = PTC_RULE_SOURCE_TODAY_OVERRIDE;
        return result;
    }
    if (rules->scheduled_override.enabled &&
        day_index >= rules->scheduled_override.start_day_index &&
        day_index <= rules->scheduled_override.end_day_index) {
        result.rule = rules->scheduled_override.rule;
        result.source = PTC_RULE_SOURCE_SCHEDULED_OVERRIDE;
        return result;
    }
    if (rules->holiday_enabled && covered && day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY) {
        result.rule = rules->holiday_rule;
        result.source = PTC_RULE_SOURCE_STATUTORY_HOLIDAY;
        return result;
    }
    if (rules->holiday_enabled && covered && day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY) {
        result.rule = rules->makeup_workday_rule;
        result.source = PTC_RULE_SOURCE_MAKEUP_WORKDAY;
        return result;
    }
    result.rule = rules->week[weekday % 7];
    result.source = PTC_RULE_SOURCE_WEEKLY;
    return result;
}

PtcDayRule ptc_rules_today_rule(const PtcRules *rules, uint16_t day_index, uint8_t weekday)
{
    return ptc_rules_resolve(rules, day_index, weekday).rule;
}

const char *ptc_rule_source_name(PtcRuleSource source)
{
    switch (source) {
    case PTC_RULE_SOURCE_TODAY_OVERRIDE: return "today_override";
    case PTC_RULE_SOURCE_SCHEDULED_OVERRIDE: return "scheduled_override";
    case PTC_RULE_SOURCE_STATUTORY_HOLIDAY: return "statutory_holiday";
    case PTC_RULE_SOURCE_MAKEUP_WORKDAY: return "makeup_workday";
    case PTC_RULE_SOURCE_WEEKLY:
    default: return "weekly";
    }
}

bool ptc_scheduled_override_is_valid(const PtcScheduledOverride *override_rule)
{
    uint32_t length;
    if (!override_rule) return false;
    if (!override_rule->enabled) return true;
    if (override_rule->end_day_index < override_rule->start_day_index) return false;
    length = (uint32_t)override_rule->end_day_index - override_rule->start_day_index + 1u;
    if (length < 1u || length > 366u) return false;
    return override_rule->rule.mode == PTC_RULE_MODE_UNLIMITED ||
        (override_rule->rule.mode == PTC_RULE_MODE_LIMIT &&
         override_rule->rule.minutes >= 1u && override_rule->rule.minutes <= 1440u);
}

bool ptc_autonomy_policy_is_valid(const PtcAutonomyPolicy *policy)
{
    return policy && (policy->daily_buffer_minutes == 0u ||
        policy->daily_buffer_minutes == 5u || policy->daily_buffer_minutes == 10u ||
        policy->daily_buffer_minutes == 15u);
}
