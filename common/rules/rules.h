#ifndef PTC_RULES_H
#define PTC_RULES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PTC_RULE_MODE_LIMIT = 1,
    PTC_RULE_MODE_UNLIMITED = 2
} PtcRuleMode;

typedef struct {
    PtcRuleMode mode;
    uint16_t minutes;
} PtcDayRule;

typedef struct {
    uint16_t day_index;
    bool present;
    PtcDayRule rule;
} PtcTodayOverride;

typedef struct {
    PtcDayRule week[7];
    PtcTodayOverride today_override;
} PtcRules;

void ptc_rules_default(PtcRules *rules);
PtcDayRule ptc_rules_today_rule(const PtcRules *rules, uint16_t day_index, uint8_t weekday);

#endif
