#ifndef PTC_RESULT_BUILDER_H
#define PTC_RESULT_BUILDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "error_code.h"

typedef struct {
    uint16_t day_index;
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
