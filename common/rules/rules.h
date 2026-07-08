#ifndef PTC_RULES_H
#define PTC_RULES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PTC_RULE_MODE_LIMIT = 1,
    PTC_RULE_MODE_UNLIMITED = 2,
    PTC_RULE_MODE_BLOCKED = 3
} PtcRuleMode;

typedef enum {
    PTC_LIMIT_ACTION_REMIND = 1,
    PTC_LIMIT_ACTION_RAW_BLOCK = 2,
    PTC_LIMIT_ACTION_SUSPEND = 3
} PtcLimitAction;

typedef struct {
    PtcRuleMode mode;
    uint16_t minutes;
} PtcDayRule;

typedef struct {
    bool enabled;
    uint16_t start_min;
    uint16_t end_min;
} PtcBedtimeRule;

typedef struct {
    uint16_t day_index;
    bool present;
    PtcDayRule rule;
} PtcTodayOverride;

typedef struct {
    PtcDayRule week[7];
    PtcTodayOverride today_override;
    PtcBedtimeRule bedtime;
    PtcLimitAction limit_action;
} PtcRules;

typedef struct {
    PtcDayRule active_rule;
    bool bedtime_active;
    bool parent_unlock_active;
    bool restricted_now;
} PtcRuleEvaluation;

void ptc_rules_default(PtcRules *rules);
PtcDayRule ptc_rules_today_rule(const PtcRules *rules, uint16_t day_index, uint8_t weekday);
PtcRuleEvaluation ptc_rules_evaluate(
    const PtcRules *rules,
    uint16_t day_index,
    uint8_t weekday,
    uint16_t minute_of_day,
    bool parent_unlock_active);

#endif
