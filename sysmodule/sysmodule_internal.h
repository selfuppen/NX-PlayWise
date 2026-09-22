#ifndef PTC_SYSMODULE_INTERNAL_H
#define PTC_SYSMODULE_INTERNAL_H

#include "sysmodule_core.h"
#include "lab_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/crypto/sha256.h"
#include "../common/protocol/activity_history.h"
#include "../common/protocol/request_schema.h"
#include "../common/protocol/redemption_history.h"
#include "../common/protocol/result_builder.h"
#include "../common/rules/rules.h"
#include "../common/rules/holiday_calendar.h"
#include "../common/time/ptc_time.h"
#include "../common/usage/daily_summary.h"
#include "../common/token/token_v1.h"
#include "../common/token/token_v2.h"
#include "../common/version.h"

typedef struct {
    char device_id[80];
    char grant_secret[128];
    uint16_t max_add_minutes;
} PtcRuntimeConfig;

typedef struct {
    uint16_t last_enforced_day_index;
    PtcPctlTargetMode last_enforced_mode;
    uint16_t last_enforced_minutes;
    bool apply_pending_confirmation;
    int64_t apply_confirmation_deadline;
    PtcPctlTargetMode pending_mode;
    uint16_t pending_minutes;
    uint16_t v2_failed_attempts;
    int64_t v2_cooldown_until;
    bool buffer_claimed;
    uint16_t buffer_claim_day_index;
    uint16_t buffer_claimed_minutes;
    uint16_t summary_day_index;
    uint16_t summary_grant_minutes;
    bool bedtime_enforced;
    uint64_t bedtime_window_instance_id;
    uint16_t bedtime_start_day_index;
    uint64_t bedtime_skipped_instance_id;
} PtcRuntimeState;

typedef struct {
    char phase[32];
    char compatibility_status[24];
    bool restriction_cleared;
    bool snapshot_available;
    int64_t activate_after;
    bool handover_today_pending;
    uint16_t handover_day_index;
    bool handover_unlimited;
    uint16_t handover_minutes;
    bool handover_remaining_available;
    uint16_t handover_remaining_minutes;
    char last_error[64];
} PtcSetupState;

#define PTC_V2_FAILURE_LIMIT 5u
#define PTC_V2_COOLDOWN_SECONDS 600

/* Retention shares a single PtcStorageEntry array across the log scan and both
   cleanup_timestamped_json calls. One array is ~39 KiB; nesting two of them
   (cleanup -> cleanup_timestamped_json) overflowed the 128 KiB main thread stack.
   Capacity stays at 256 because list_entries has no offset/paging parameter, so a
   smaller value would silently truncate retention. */
#define PTC_CLEANUP_MAX_ENTRIES 256u

void join_path(char *out, size_t out_size, const char *a, const char *b);
bool daily_log_path(PtcSysmodule *sysmodule, const char *name, char *out, size_t out_size);
void invalidate_all_caches(PtcSysmodule *sysmodule);
bool json_string(const char *text, const char *key, char *out, size_t out_size);
bool json_bool_value(const char *text, const char *key, bool *out);
bool load_setup_state(PtcSysmodule *sysmodule, PtcSetupState *setup);
bool save_setup_state(PtcSysmodule *sysmodule, const PtcSetupState *setup);
bool save_install_snapshot(PtcSysmodule *sysmodule, const PtcPctlSettingsSnapshot *snapshot, int64_t captured_at);
bool load_install_snapshot(PtcSysmodule *sysmodule, PtcPctlSettingsSnapshot *snapshot);
bool save_bedtime_snapshot(PtcSysmodule *sysmodule, const PtcPctlSettingsSnapshot *snapshot,
    const PtcBedtimeEvaluation *evaluation, int64_t captured_at);
bool load_bedtime_snapshot(PtcSysmodule *sysmodule, PtcPctlSettingsSnapshot *snapshot,
    uint64_t *window_instance_id, uint16_t *start_day_index);
void clear_bedtime_snapshot(PtcSysmodule *sysmodule);
bool recovery_path_exists(PtcSysmodule *sysmodule);
bool recovery_begin(PtcSysmodule *sysmodule, const PtcRequest *request, PtcClockSnapshot now);
void recovery_clear(PtcSysmodule *sysmodule);
bool recovery_rollback(PtcSysmodule *sysmodule);
const char *pctl_target_mode_name(PtcPctlTargetMode mode);
bool sysmodule_environment_fingerprint(PtcSysmodule *sysmodule, char out[65]);
bool load_config(PtcSysmodule *sysmodule, PtcRuntimeConfig *config);
bool load_rules(PtcSysmodule *sysmodule, PtcRules *rules);
bool save_rules(PtcSysmodule *sysmodule, const PtcRules *rules);
bool restore_rules(PtcSysmodule *sysmodule, const PtcRules *rules, bool existed);
bool load_state(PtcSysmodule *sysmodule, PtcRuntimeState *state);
bool save_state(PtcSysmodule *sysmodule, const PtcRuntimeState *state, int64_t updated_at);
void append_event(PtcSysmodule *sysmodule, const PtcRequest *request, const char *event, PtcErrorCode error, const char *detail);
void take_pctl_debug_snapshot(PtcSysmodule *sysmodule, PtcPctlDebugSnapshot *out);
uint32_t last_pctl_ipc_result(PtcSysmodule *sysmodule);
void append_pctl_debug(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *stage,
    const char *mode,
    const PtcPctlTarget *target,
    PtcErrorCode error,
    uint32_t ipc_result,
    const PtcPctlDebugSnapshot *before,
    const PtcPctlDebugSnapshot *after);
bool nonce_used_v1(uint16_t day_index, uint32_t nonce, void *ctx);
bool nonce_used_v2(uint16_t day_index, uint32_t nonce, void *ctx);
bool consume_nonce(PtcSysmodule *sysmodule, const PtcRequest *request, uint16_t day_index, uint32_t nonce, unsigned int token_version);
bool save_redemption_history(
    PtcSysmodule *sysmodule,
    const PtcRedemptionHistoryRecord *record);
bool clear_redemption_history(PtcSysmodule *sysmodule);
bool save_activity_history(PtcSysmodule *sysmodule, const PtcActivityHistoryRecord *record);
bool clear_activity_history(PtcSysmodule *sysmodule);
int64_t result_played_minutes(const PtcPctlStatus *status);
void result_state_from_pctl(
    PtcResultState *state,
    uint16_t day_index,
    const PtcPctlStatus *status);
bool write_result(PtcSysmodule *sysmodule, const char *request_id, const char *json);
bool record_activity(PtcSysmodule *sysmodule, const PtcRequest *request,
    PtcClockSnapshot now, uint16_t minutes, uint16_t effective_minutes);
bool bedtime_overlay_verified(PtcSysmodule *sysmodule);
void fill_extended_result_state(PtcSysmodule *sysmodule, PtcResultState *state,
    const PtcRules *rules, const PtcRuntimeState *runtime_state,
    const PtcPctlStatus *pctl_status, PtcClockSnapshot now);
bool write_current_status_result(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *mode,
    bool dry_run,
    PtcClockSnapshot now,
    bool commits_recovery);
bool process_status(PtcSysmodule *sysmodule, const PtcRequest *request,
    PtcClockSnapshot now);
int usage_summary_tick(PtcSysmodule *sysmodule, PtcClockSnapshot now);
bool request_file_path(char *out, size_t out_size, const PtcSysmodule *sysmodule, const char *queue, const char *name);
PtcErrorCode ptc_backup_before_write(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *mode);
bool ptc_pctl_settings_snapshot_equal(
    const PtcPctlSettingsSnapshot *a,
    const PtcPctlSettingsSnapshot *b);
PtcErrorCode apply_target(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now,
    const char *mode_name,
    PtcPctlTargetMode mode,
    uint16_t minutes);
bool request_file_stem(const char *name, char *out, size_t out_size);
bool finish_with_error(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *mode,
    bool dry_run,
    PtcErrorCode error,
    uint16_t day_index);
PtcPctlTargetMode target_from_day_rule(PtcDayRule rule);
uint16_t ptc_pctl_played_minutes(const PtcPctlStatus *status);
uint16_t accumulate_today_limit(PtcRules *rules, uint16_t day_index, uint8_t weekday, uint16_t add_minutes, uint16_t played_minutes);
PtcErrorCode update_rules_for_request(PtcSysmodule *sysmodule, const PtcRequest *request, PtcRules *rules, PtcRuntimeState *runtime_state, PtcClockSnapshot now, uint16_t played_minutes);
void effect_wait(PtcSysmodule *sysmodule, uint32_t milliseconds);
bool bedtime_blocks_grants(PtcSysmodule *sysmodule, PtcClockSnapshot now);
bool target_settings_observed(
    PtcPctlTargetMode mode,
    uint16_t minutes,
    const PtcPctlStatus *status);
PtcErrorCode observe_target_with_optional_activation(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    PtcClockSnapshot now,
    const char *mode_name,
    PtcPctlTargetMode target_mode,
    uint16_t minutes,
    const char *event_detail,
    PtcPctlStatus *observed);
PtcErrorCode restore_snapshot_exact(
    PtcSysmodule *sysmodule,
    const PtcPctlSettingsSnapshot *original,
    PtcPctlSettingsSnapshot *restored_snapshot,
    PtcPctlStatus *restored_status,
    uint8_t weekday,
    bool *raw_restored,
    bool *timer_restored);
bool process_complete_setup(PtcSysmodule *sysmodule, const PtcRequest *request,
    const PtcRuntimeConfig *config, bool disable_flag, PtcClockSnapshot now);
bool process_retry_setup_release(PtcSysmodule *sysmodule, const PtcRequest *request,
    const PtcRuntimeConfig *config, PtcClockSnapshot now);
bool process_restore_install_snapshot(PtcSysmodule *sysmodule, const PtcRequest *request,
    PtcClockSnapshot now);
void write_disable_flag(PtcSysmodule *sysmodule, const char *reason);
PtcErrorCode restore_bedtime_base(PtcSysmodule *sysmodule, const PtcRequest *request,
    const PtcRules *rules,
    PtcRuntimeState *runtime_state, PtcClockSnapshot now);
bool process_grant_request_surface(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const PtcRuntimeConfig *config,
    bool disable_flag,
    PtcClockSnapshot now);
bool process_recovery_request_surface(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    bool disable_flag,
    PtcClockSnapshot now);
void process_request_text(PtcSysmodule *sysmodule, const char *request_text, const char *expected_request_id);

#endif
