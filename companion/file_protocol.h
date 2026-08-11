#ifndef PTC_COMPANION_FILE_PROTOCOL_H
#define PTC_COMPANION_FILE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "../common/rules/rules.h"
#include "../platform/storage.h"
#include "result_summary.h"

#define PTC_COMPANION_REQUEST_ID_SIZE 32

typedef enum {
    PTC_COMPANION_OK = 0,
    PTC_COMPANION_PENDING = 1,
    PTC_COMPANION_TIMEOUT = 2,
    PTC_COMPANION_BAD_ARGUMENT = 3,
    PTC_COMPANION_WRITE_FAILED = 4,
    PTC_COMPANION_RENAME_FAILED = 5,
    PTC_COMPANION_RESULT_INVALID = 6,
    PTC_COMPANION_RESULT_MISMATCH = 7,
} PtcCompanionStatus;

typedef struct {
    char app_root[96];
    PtcStorage *storage;
} PtcCompanionFileClient;

typedef struct {
    char request_id[PTC_COMPANION_REQUEST_ID_SIZE];
    int64_t confirmed_at;
    bool submitted;
    int grant_minutes;
    bool before_remaining_available;
    int before_remaining_minutes;
    bool before_unlimited;
    bool after_remaining_available;
    int after_remaining_minutes;
    int effective_add_minutes;
    bool capped;
    bool converts_unlimited_to_limited;
} PtcPendingRedemption;

void ptc_companion_file_client_init(PtcCompanionFileClient *client, const char *app_root, PtcStorage *storage);
PtcCompanionStatus ptc_companion_make_request_id(char *out, size_t out_size, int64_t unix_ms, uint16_t random16);
PtcCompanionStatus ptc_companion_submit_status(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_submit_offline_code(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, const char *code);
PtcCompanionStatus ptc_companion_submit_set_today_limit(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, uint16_t minutes);
PtcCompanionStatus ptc_companion_submit_add_today_minutes(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, uint16_t minutes);
PtcCompanionStatus ptc_companion_submit_disable_today_limit(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_submit_restore_today_policy(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_submit_set_weekly_template(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, const PtcDayRule week[7]);
PtcCompanionStatus ptc_companion_submit_set_holiday_policy(PtcCompanionFileClient *client, const char *request_id,
    int64_t created_at, bool enabled, PtcDayRule holiday_rule, PtcDayRule makeup_workday_rule);
PtcCompanionStatus ptc_companion_set_disable_flag(PtcCompanionFileClient *client, bool enabled);
PtcCompanionStatus ptc_companion_pending_redemption_save(
    PtcCompanionFileClient *client,
    const PtcPendingRedemption *pending);
PtcCompanionStatus ptc_companion_pending_redemption_load(
    PtcCompanionFileClient *client,
    PtcPendingRedemption *out,
    bool *found);
PtcCompanionStatus ptc_companion_pending_redemption_clear(PtcCompanionFileClient *client);
bool ptc_companion_pending_redemption_has_submission(
    PtcCompanionFileClient *client,
    const PtcPendingRedemption *pending);
PtcCompanionStatus ptc_companion_read_result(
    PtcCompanionFileClient *client,
    const char *request_id,
    int elapsed_ms,
    int timeout_ms,
    char *out,
    size_t out_size);
PtcCompanionStatus ptc_companion_format_result_summary(const char *result_json, char *out, size_t out_size);
PtcCompanionStatus ptc_companion_parse_result_summary(const char *result_json, PtcCompanionResultSummary *out);
const char *ptc_companion_status_name(PtcCompanionStatus status);

#endif
