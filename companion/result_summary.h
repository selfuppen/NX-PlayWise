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
    char rule_source[32];
    char status[8];
    char type[48];
    char reason[64];
    char message[160];
} PtcCompanionResultSummary;

bool ptc_companion_result_summary_parse(const char *result_json, PtcCompanionResultSummary *out);
bool ptc_companion_result_summary_format(const PtcCompanionResultSummary *summary, char *out, size_t out_size);

#endif
