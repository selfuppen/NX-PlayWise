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
    bool enabled;
    uint16_t start_day_index;
    uint16_t end_day_index;
    PtcDayRule rule;
} PtcScheduledOverride;

typedef struct {
    uint16_t daily_buffer_minutes;
} PtcAutonomyPolicy;

typedef struct {
    PtcDayRule week[7];
    PtcTodayOverride today_override;
    PtcScheduledOverride scheduled_override;
    PtcAutonomyPolicy autonomy_policy;
    bool holiday_enabled;
    PtcDayRule holiday_rule;
    PtcDayRule makeup_workday_rule;
} PtcRules;

typedef enum {
    PTC_RULE_SOURCE_WEEKLY = 0,
    PTC_RULE_SOURCE_TODAY_OVERRIDE = 1,
    PTC_RULE_SOURCE_SCHEDULED_OVERRIDE = 2,
    PTC_RULE_SOURCE_STATUTORY_HOLIDAY = 3,
    PTC_RULE_SOURCE_MAKEUP_WORKDAY = 4
} PtcRuleSource;

typedef struct {
    PtcDayRule rule;
    PtcRuleSource source;
    bool calendar_covered;
} PtcEffectiveRule;

void ptc_rules_default(PtcRules *rules);
PtcDayRule ptc_rules_today_rule(const PtcRules *rules, uint16_t day_index, uint8_t weekday);
PtcEffectiveRule ptc_rules_resolve(const PtcRules *rules, uint16_t day_index, uint8_t weekday);
const char *ptc_rule_source_name(PtcRuleSource source);
bool ptc_scheduled_override_is_valid(const PtcScheduledOverride *override_rule);
bool ptc_autonomy_policy_is_valid(const PtcAutonomyPolicy *policy);

#endif
