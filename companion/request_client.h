#ifndef PTC_COMPANION_REQUEST_CLIENT_H
#define PTC_COMPANION_REQUEST_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../common/rules/rules.h"

int ptc_companion_status_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at);
int ptc_companion_offline_code_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *code);
int ptc_companion_preview_offline_code_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *code);
int ptc_companion_parent_minutes_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *type, uint16_t minutes);
int ptc_companion_empty_payload_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *type);
int ptc_companion_set_weekly_template_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const PtcDayRule week[7]);
int ptc_companion_set_holiday_policy_request_json(char *out, size_t out_size, const char *request_id,
    int64_t created_at, bool enabled, PtcDayRule holiday_rule, PtcDayRule makeup_workday_rule);
int ptc_companion_set_scheduled_override_request_json(char *out, size_t out_size,
    const char *request_id, int64_t created_at, const PtcScheduledOverride *scheduled_override);
int ptc_companion_set_autonomy_policy_request_json(char *out, size_t out_size,
    const char *request_id, int64_t created_at, const PtcAutonomyPolicy *policy);

#endif
