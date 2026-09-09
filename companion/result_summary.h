#ifndef PTC_COMPANION_RESULT_SUMMARY_H
#define PTC_COMPANION_RESULT_SUMMARY_H

#include <stdbool.h>
#include <stddef.h>

/* Small, UI-safe projection of the result contract. It intentionally omits
   request payloads, secrets, paths and diagnostic evidence. */
typedef struct {
    bool valid;
    bool ok;
    bool unlock_observed;
    int error_code;
    int day_index;
    int remaining_minutes;
    int played_minutes;
    int play_timer_enabled;
    int restricted_now;
    int unrestricted_today;
    bool remaining_available;
    bool played_minutes_available;
    bool preview_available;
    int grant_minutes;
    bool remaining_after_available;
    int remaining_after_minutes;
    int effective_add_minutes;
    bool preview_capped;
    bool converts_unlimited_to_limited;
    bool calendar_covered;
    bool calendar_update_warning;
    bool bedtime_enabled;
    bool bedtime_active;
    bool bedtime_skipped;
    unsigned long long bedtime_window_instance_id;
    int bedtime_start_day_index;
    int bedtime_start_minute;
    int bedtime_end_minute;
    bool bedtime_next_available;
    int bedtime_next_start_day_index;
    int bedtime_next_start_minute;
    int bedtime_next_end_minute;
    unsigned long long bedtime_next_window_instance_id;
    bool bedtime_official_setting_confirmed;
    bool bedtime_overlay_verified;
    bool daily_restriction_active;
    char bedtime_source[32];
    char bedtime_recovery_phase[32];
    char rule_source[32];
    char status[8];
    char type[48];
    char reason[64];
    char message[160];
} PtcCompanionResultSummary;

bool ptc_companion_result_summary_parse(const char *result_json, PtcCompanionResultSummary *out);
bool ptc_companion_result_summary_format(const PtcCompanionResultSummary *summary, char *out, size_t out_size);

#endif
