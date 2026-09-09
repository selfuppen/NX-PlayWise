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
    bool enabled;
    uint16_t start_minute;
    uint16_t end_minute;
} PtcBedtimeWindow;

typedef enum {
    PTC_BEDTIME_OVERRIDE_INHERIT = 0,
    PTC_BEDTIME_OVERRIDE_DISABLED = 1,
    PTC_BEDTIME_OVERRIDE_CUSTOM = 2
} PtcBedtimeOverrideMode;

typedef struct {
    PtcBedtimeOverrideMode mode;
    PtcBedtimeWindow window;
} PtcBedtimeSpecialRule;

typedef struct {
    bool present;
    uint16_t start_day_index;
    uint16_t end_day_index;
    PtcBedtimeSpecialRule rule;
} PtcBedtimeScheduledOverride;

typedef struct {
    bool enabled;
    PtcBedtimeWindow week[7];
    bool calendar_enabled;
    PtcBedtimeSpecialRule holiday_rule;
    PtcBedtimeSpecialRule makeup_workday_rule;
    PtcBedtimeScheduledOverride scheduled_override;
    uint16_t confirmation_version;
    int64_t official_setting_confirmed_at;
    char confirmed_environment[65];
    bool unverified_overlay_risk_accepted;
} PtcBedtimePolicy;

typedef enum {
    PTC_BEDTIME_SOURCE_WEEKLY = 0,
    PTC_BEDTIME_SOURCE_SCHEDULED_OVERRIDE = 1,
    PTC_BEDTIME_SOURCE_STATUTORY_HOLIDAY = 2,
    PTC_BEDTIME_SOURCE_MAKEUP_WORKDAY = 3
} PtcBedtimeSource;

typedef struct {
    PtcBedtimeWindow window;
    PtcBedtimeSource source;
    bool calendar_covered;
} PtcEffectiveBedtime;

typedef struct {
    bool active;
    uint16_t start_day_index;
    uint16_t start_minute;
    uint16_t end_minute;
    uint64_t window_instance_id;
    PtcBedtimeSource source;
} PtcBedtimeEvaluation;

typedef struct {
    PtcDayRule week[7];
    PtcTodayOverride today_override;
    PtcScheduledOverride scheduled_override;
    PtcAutonomyPolicy autonomy_policy;
    bool holiday_enabled;
    PtcDayRule holiday_rule;
    PtcDayRule makeup_workday_rule;
    PtcBedtimePolicy bedtime;
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
bool ptc_bedtime_window_is_valid(const PtcBedtimeWindow *window);
bool ptc_bedtime_policy_is_valid(const PtcBedtimePolicy *policy);
PtcEffectiveBedtime ptc_bedtime_resolve_start_day(
    const PtcRules *rules,
    uint16_t start_day_index,
    uint8_t weekday);
PtcBedtimeEvaluation ptc_bedtime_evaluate(
    const PtcRules *rules,
    uint16_t day_index,
    uint8_t weekday,
    uint16_t minute_of_day);
uint64_t ptc_bedtime_window_instance_id(uint16_t start_day_index, uint16_t start_minute);
const char *ptc_bedtime_source_name(PtcBedtimeSource source);

#endif
