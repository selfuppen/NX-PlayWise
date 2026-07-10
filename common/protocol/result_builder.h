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
    int remaining_minutes;
    int play_timer_enabled;
    int restricted_now;
    bool bedtime_active;
    bool parent_unlock_active;
    bool play_timer_write_verified;
    bool raw_block_verified;
    bool suspend_verified;
} PtcResultState;

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
