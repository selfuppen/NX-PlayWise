#ifndef PTC_RESULT_BUILDER_H
#define PTC_RESULT_BUILDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "error_code.h"

#define PTC_RESULT_FORECAST_DAYS 7u

typedef struct {
    uint16_t day_index;
    int mode;
    uint16_t minutes;
    const char *rule_source;
    bool calendar_covered;
} PtcResultForecastDay;

typedef struct {
    uint16_t day_index;
    bool restriction_enabled_available;
    bool restriction_enabled;
    bool temporary_unlocked_available;
    bool temporary_unlocked;
    int limited_today;
    int blocked_today;
    int unrestricted_today;
    bool remaining_available;
    int64_t remaining_minutes;
    bool played_minutes_available;
    int64_t played_minutes;
    int play_timer_enabled;
    int restricted_now;
    const char *rule_source;
    bool calendar_covered;
    bool calendar_update_warning;
    PtcResultForecastDay forecast[PTC_RESULT_FORECAST_DAYS];
    uint16_t daily_buffer_minutes;
    bool daily_buffer_claimed;
    bool daily_buffer_available;
    const char *daily_buffer_reason;
    bool usage_summary_available;
    uint16_t usage_known_days_7;
    uint32_t usage_consumed_minutes_7;
    uint16_t usage_known_days_30;
    uint32_t usage_consumed_minutes_30;
} PtcResultState;

typedef struct {
    uint16_t grant_minutes;
    bool remaining_after_available;
    int64_t remaining_after_minutes;
    uint16_t effective_add_minutes;
    bool capped;
    bool converts_unlimited_to_limited;
} PtcOfflineCodePreview;

void ptc_result_state_default(PtcResultState *state, uint16_t day_index);
PtcErrorCode ptc_result_validate(const char *text);
int ptc_result_ok_json(
    char *out,
    size_t out_size,
    const char *request_id,
    const char *request_type,
    const char *mode,
    bool dry_run,
    const PtcResultState *state,
    int64_t completed_at);
int ptc_result_preview_ok_json(
    char *out,
    size_t out_size,
    const char *request_id,
    const char *request_type,
    const PtcResultState *state,
    const PtcOfflineCodePreview *preview,
    int64_t completed_at);
int ptc_result_error_json(
    char *out,
    size_t out_size,
    const char *request_id,
    const char *request_type,
    const char *mode,
    bool dry_run,
    PtcErrorCode error,
    const PtcResultState *state,
    int64_t completed_at);

#endif
