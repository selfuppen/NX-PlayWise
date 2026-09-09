#include "rules.h"

#include "holiday_calendar.h"

#include <string.h>

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
    rules->bedtime.enabled = false;
    rules->bedtime.calendar_enabled = true;
    rules->bedtime.confirmation_version = 0;
    rules->bedtime.official_setting_confirmed_at = 0;
    rules->bedtime.confirmed_environment[0] = '\0';
    rules->bedtime.unverified_overlay_risk_accepted = false;
    for (i = 0; i < 7; ++i) {
        rules->bedtime.week[i].enabled = true;
        rules->bedtime.week[i].start_minute = (i == 5 || i == 6) ? 1320 : 1260;
        rules->bedtime.week[i].end_minute = (i == 5 || i == 6) ? 480 : 420;
    }
    rules->bedtime.holiday_rule.mode = PTC_BEDTIME_OVERRIDE_CUSTOM;
    rules->bedtime.holiday_rule.window.enabled = true;
    rules->bedtime.holiday_rule.window.start_minute = 1320;
    rules->bedtime.holiday_rule.window.end_minute = 480;
    rules->bedtime.makeup_workday_rule.mode = PTC_BEDTIME_OVERRIDE_CUSTOM;
    rules->bedtime.makeup_workday_rule.window.enabled = true;
    rules->bedtime.makeup_workday_rule.window.start_minute = 1260;
    rules->bedtime.makeup_workday_rule.window.end_minute = 420;
    rules->bedtime.scheduled_override.present = false;
    rules->bedtime.scheduled_override.start_day_index = 0;
    rules->bedtime.scheduled_override.end_day_index = 0;
    rules->bedtime.scheduled_override.rule.mode = PTC_BEDTIME_OVERRIDE_INHERIT;
    rules->bedtime.scheduled_override.rule.window.enabled = false;
    rules->bedtime.scheduled_override.rule.window.start_minute = 1260;
    rules->bedtime.scheduled_override.rule.window.end_minute = 420;
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

bool ptc_bedtime_window_is_valid(const PtcBedtimeWindow *window)
{
    if (!window) return false;
    if (!window->enabled) return true;
    return window->start_minute < 1440u && window->end_minute < 1440u &&
        window->start_minute > window->end_minute;
}

static bool special_rule_is_valid(const PtcBedtimeSpecialRule *rule)
{
    if (!rule || rule->mode < PTC_BEDTIME_OVERRIDE_INHERIT ||
        rule->mode > PTC_BEDTIME_OVERRIDE_CUSTOM) return false;
    return rule->mode != PTC_BEDTIME_OVERRIDE_CUSTOM ||
        (rule->window.enabled && ptc_bedtime_window_is_valid(&rule->window));
}

static bool bedtime_windows_can_follow(
    const PtcBedtimeWindow *previous,
    const PtcBedtimeWindow *next)
{
    return !previous->enabled || !next->enabled || previous->end_minute <= next->start_minute;
}

bool ptc_bedtime_policy_is_valid(const PtcBedtimePolicy *policy)
{
    unsigned int i;
    unsigned int j;
    uint32_t range_length;
    const PtcBedtimeWindow *specials[3];
    unsigned int special_count = 0;
    if (!policy) return false;
    for (i = 0; i < 7; ++i) {
        const PtcBedtimeWindow *current = &policy->week[i];
        const PtcBedtimeWindow *next = &policy->week[(i + 1u) % 7u];
        if (!ptc_bedtime_window_is_valid(current)) return false;
        if (!bedtime_windows_can_follow(current, next)) return false;
    }
    if (!special_rule_is_valid(&policy->holiday_rule) ||
        !special_rule_is_valid(&policy->makeup_workday_rule) ||
        !special_rule_is_valid(&policy->scheduled_override.rule)) return false;
    if (policy->holiday_rule.mode == PTC_BEDTIME_OVERRIDE_CUSTOM)
        specials[special_count++] = &policy->holiday_rule.window;
    if (policy->makeup_workday_rule.mode == PTC_BEDTIME_OVERRIDE_CUSTOM)
        specials[special_count++] = &policy->makeup_workday_rule.window;
    if (policy->scheduled_override.rule.mode == PTC_BEDTIME_OVERRIDE_CUSTOM)
        specials[special_count++] = &policy->scheduled_override.rule.window;
    /* A holiday or date override can replace any weekday and can be adjacent to
       another special date. Reject every possible boundary overlap up front. */
    for (i = 0; i < special_count; ++i) {
        for (j = 0; j < 7; ++j) {
            if (!bedtime_windows_can_follow(specials[i], &policy->week[j]) ||
                !bedtime_windows_can_follow(&policy->week[j], specials[i])) return false;
        }
        for (j = 0; j < special_count; ++j) {
            if (!bedtime_windows_can_follow(specials[i], specials[j])) return false;
        }
    }
    if (!policy->scheduled_override.present) return true;
    if (policy->scheduled_override.end_day_index < policy->scheduled_override.start_day_index) return false;
    range_length = (uint32_t)policy->scheduled_override.end_day_index -
        policy->scheduled_override.start_day_index + 1u;
    return range_length >= 1u && range_length <= 366u;
}

static PtcEffectiveBedtime effective_from_special(
    const PtcBedtimeWindow *fallback,
    const PtcBedtimeSpecialRule *special,
    PtcBedtimeSource source,
    bool covered)
{
    PtcEffectiveBedtime result;
    memset(&result, 0, sizeof(result));
    result.calendar_covered = covered;
    if (special->mode == PTC_BEDTIME_OVERRIDE_CUSTOM) {
        result.window = special->window;
        result.source = source;
    } else if (special->mode == PTC_BEDTIME_OVERRIDE_DISABLED) {
        result.window.enabled = false;
        result.source = source;
    } else {
        result.window = *fallback;
        result.source = PTC_BEDTIME_SOURCE_WEEKLY;
    }
    return result;
}

PtcEffectiveBedtime ptc_bedtime_resolve_start_day(
    const PtcRules *rules,
    uint16_t start_day_index,
    uint8_t weekday)
{
    PtcEffectiveBedtime result;
    bool covered = false;
    PtcCalendarDayType day_type;
    const PtcBedtimeWindow *weekly;
    memset(&result, 0, sizeof(result));
    if (!rules) return result;
    weekly = &rules->bedtime.week[weekday % 7u];
    result.window = *weekly;
    result.source = PTC_BEDTIME_SOURCE_WEEKLY;
    if (rules->bedtime.scheduled_override.present &&
        start_day_index >= rules->bedtime.scheduled_override.start_day_index &&
        start_day_index <= rules->bedtime.scheduled_override.end_day_index) {
        return effective_from_special(weekly, &rules->bedtime.scheduled_override.rule,
            PTC_BEDTIME_SOURCE_SCHEDULED_OVERRIDE, false);
    }
    day_type = ptc_holiday_calendar_classify(start_day_index, &covered);
    result.calendar_covered = covered;
    if (!rules->bedtime.calendar_enabled || !covered) return result;
    if (day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY) {
        return effective_from_special(weekly, &rules->bedtime.holiday_rule,
            PTC_BEDTIME_SOURCE_STATUTORY_HOLIDAY, true);
    }
    if (day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY) {
        return effective_from_special(weekly, &rules->bedtime.makeup_workday_rule,
            PTC_BEDTIME_SOURCE_MAKEUP_WORKDAY, true);
    }
    return result;
}

uint64_t ptc_bedtime_window_instance_id(uint16_t start_day_index, uint16_t start_minute)
{
    return ((uint64_t)start_day_index << 16) | (uint64_t)start_minute;
}

PtcBedtimeEvaluation ptc_bedtime_evaluate(
    const PtcRules *rules,
    uint16_t day_index,
    uint8_t weekday,
    uint16_t minute_of_day)
{
    PtcBedtimeEvaluation result;
    PtcEffectiveBedtime effective;
    memset(&result, 0, sizeof(result));
    if (!rules || !rules->bedtime.enabled || minute_of_day >= 1440u) return result;
    effective = ptc_bedtime_resolve_start_day(rules, day_index, weekday);
    if (effective.window.enabled && minute_of_day >= effective.window.start_minute) {
        result.active = true;
        result.start_day_index = day_index;
    } else if (day_index > 0u) {
        effective = ptc_bedtime_resolve_start_day(
            rules, (uint16_t)(day_index - 1u), (uint8_t)((weekday + 6u) % 7u));
        if (effective.window.enabled && minute_of_day < effective.window.end_minute) {
            result.active = true;
            result.start_day_index = (uint16_t)(day_index - 1u);
        }
    }
    if (!result.active) return result;
    result.start_minute = effective.window.start_minute;
    result.end_minute = effective.window.end_minute;
    result.source = effective.source;
    result.window_instance_id = ptc_bedtime_window_instance_id(
        result.start_day_index, result.start_minute);
    return result;
}

const char *ptc_bedtime_source_name(PtcBedtimeSource source)
{
    switch (source) {
    case PTC_BEDTIME_SOURCE_SCHEDULED_OVERRIDE: return "scheduled_override";
    case PTC_BEDTIME_SOURCE_STATUTORY_HOLIDAY: return "statutory_holiday";
    case PTC_BEDTIME_SOURCE_MAKEUP_WORKDAY: return "makeup_workday";
    case PTC_BEDTIME_SOURCE_WEEKLY:
    default: return "weekly";
    }
}
