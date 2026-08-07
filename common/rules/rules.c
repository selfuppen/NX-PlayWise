#include "rules.h"

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
}

PtcDayRule ptc_rules_today_rule(const PtcRules *rules, uint16_t day_index, uint8_t weekday)
{
    if (rules->today_override.present && rules->today_override.day_index == day_index) {
        return rules->today_override.rule;
    }
    return rules->week[weekday % 7];
}
