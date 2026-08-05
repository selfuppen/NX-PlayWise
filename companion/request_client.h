#ifndef PTC_COMPANION_REQUEST_CLIENT_H
#define PTC_COMPANION_REQUEST_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../common/rules/rules.h"

int ptc_companion_status_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at);
int ptc_companion_offline_code_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *code);
int ptc_companion_parent_minutes_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *type, uint16_t minutes);
int ptc_companion_empty_payload_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *type);
int ptc_companion_set_weekly_template_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const PtcDayRule week[7]);
int ptc_companion_set_bedtime_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const PtcBedtimeRule *bedtime);
int ptc_companion_set_limit_action_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, PtcLimitAction action);
int ptc_companion_parent_unlock_start_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, uint16_t duration_minutes);
int ptc_companion_probe_play_timer_effect_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, bool wait_for_expiry);
int ptc_companion_prepare_device_test_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at);

#endif
