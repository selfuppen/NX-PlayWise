#include "rules.h"

#include "../time/ptc_time.h"

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
    rules->bedtime.enabled = false;
    rules->bedtime.start_min = 1260;
    rules->bedtime.end_min = 480;
    rules->limit_action = PTC_LIMIT_ACTION_REMIND;
}

PtcDayRule ptc_rules_today_rule(const PtcRules *rules, uint16_t day_index, uint8_t weekday)
{
    if (rules->today_override.present && rules->today_override.day_index == day_index) {
        return rules->today_override.rule;
    }
    return rules->week[weekday % 7];
}

PtcRuleEvaluation ptc_rules_evaluate(
    const PtcRules *rules,
    uint16_t day_index,
    uint8_t weekday,
    uint16_t minute_of_day,
    bool parent_unlock_active)
{
    PtcRuleEvaluation out;
    out.active_rule = ptc_rules_today_rule(rules, day_index, weekday);
    out.parent_unlock_active = parent_unlock_active;
    out.bedtime_active = rules->bedtime.enabled &&
        ptc_bedtime_active(minute_of_day, rules->bedtime.start_min, rules->bedtime.end_min);
    out.restricted_now = !parent_unlock_active &&
        (out.active_rule.mode == PTC_RULE_MODE_BLOCKED || out.bedtime_active);
    return out;
}
