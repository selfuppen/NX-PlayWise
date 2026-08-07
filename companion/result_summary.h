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
    bool remaining_available;
    bool played_minutes_available;
    char status[8];
    char type[48];
    char reason[64];
    char message[160];
} PtcCompanionResultSummary;

bool ptc_companion_result_summary_parse(const char *result_json, PtcCompanionResultSummary *out);
bool ptc_companion_result_summary_format(const PtcCompanionResultSummary *summary, char *out, size_t out_size);

#endif
