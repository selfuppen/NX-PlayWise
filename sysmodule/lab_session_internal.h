#ifndef PTC_LAB_SESSION_INTERNAL_H
#define PTC_LAB_SESSION_INTERNAL_H

#ifdef PLAYWISE_DEVICE_LAB

#include "lab_session.h"

#define LAB_PHASE_SECONDS 75LL
#define LAB_ACTIVATION_PHASE_SECONDS 90LL
#define LAB_RESTRICTION_SECONDS 15LL
#define LAB_RESTRICTION_EVENT_POLL_MS 100U
#define LAB_REPORT_BUFFER 32768U
#define LAB_MINIMUM_REMAINING_NS 600000000000LL
#define LAB_CAMPAIGN_SLOT_COUNT 4

typedef struct {
    char run_id[80];
    char mode[32];
    char campaign_id[48];
    char campaign_slot[40];
    char game_slot[8];
    char official_pause_expected[16];
    int campaign_attempt;
    char state[32];
    int next_phase;
    char active_phase[32];
    int64_t started_at;
    int64_t deadline;
    char observation[32];
    char runtime_effect[32];
    bool baseline_all_zero;
    bool activation_preconditions_met;
    int64_t baseline_remaining_ns;
    int home_awake_counted;
    int sleep_excluded;
    int limited_settings_only_runtime_ready;
    int grant_settings_only_runtime_ready;
    int unlimited_settings_only_runtime_ready;
    int limited_fallback_called;
    int grant_fallback_called;
    int unlimited_fallback_called;
    int limited_fallback_succeeded;
    int grant_fallback_succeeded;
    int unlimited_fallback_succeeded;
    bool event_armed;
    int restriction_weekday;
    bool restored;
    char restore_verdict[32];
    PtcPctlSettingsSnapshot original;
    PtcPctlForensicSample before;
} LabState;

typedef struct {
    char campaign_id[48];
    char state[16];
    char original_pause_state[8];
    char entry_method[16];
    int next_slot;
    int attempts[LAB_CAMPAIGN_SLOT_COUNT];
    char accepted_run_ids[LAB_CAMPAIGN_SLOT_COUNT][80];
} LabCampaign;

bool lab_read_fragment(PtcSysmodule *sysmodule, const char *relative, char *out, size_t size);
void lab_activation_method(PtcSysmodule *sysmodule, char out[16]);
void lab_id_suffix(const char *value, char out[13]);
void lab_final_report_path(PtcSysmodule *sysmodule, const LabState *state, char *out, size_t size);
void lab_draft_report_path(PtcSysmodule *sysmodule, const LabState *state, char *out, size_t size);
unsigned int lab_required_phase_count(const LabState *state);
bool lab_write_public_evidence(PtcSysmodule *sysmodule, const LabState *state,
    const PtcPctlPublicParity *parity, bool same_value_write, bool same_value_restored);
bool lab_rebuild_report(PtcSysmodule *sysmodule, const LabState *state);

#endif

#endif
