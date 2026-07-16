#ifndef PTC_COMPANION_FILE_PROTOCOL_H
#define PTC_COMPANION_FILE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "../common/rules/rules.h"
#include "../platform/storage.h"

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

void ptc_companion_file_client_init(PtcCompanionFileClient *client, const char *app_root, PtcStorage *storage);
PtcCompanionStatus ptc_companion_make_request_id(char *out, size_t out_size, int64_t unix_ms, uint16_t random16);
PtcCompanionStatus ptc_companion_submit_status(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_submit_offline_code(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, const char *code);
PtcCompanionStatus ptc_companion_submit_set_today_limit(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, uint16_t minutes);
PtcCompanionStatus ptc_companion_submit_add_today_minutes(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, uint16_t minutes);
PtcCompanionStatus ptc_companion_submit_disable_today_limit(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_submit_block_today(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_submit_restore_today_policy(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_submit_set_weekly_template(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, const PtcDayRule week[7]);
PtcCompanionStatus ptc_companion_submit_set_bedtime(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, const PtcBedtimeRule *bedtime);
PtcCompanionStatus ptc_companion_submit_set_limit_action(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, PtcLimitAction action);
PtcCompanionStatus ptc_companion_submit_parent_unlock_start(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, uint16_t duration_minutes);
PtcCompanionStatus ptc_companion_submit_parent_unlock_end(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_submit_probe_play_timer_write(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_submit_probe_play_timer_effect(PtcCompanionFileClient *client, const char *request_id, int64_t created_at, bool wait_for_expiry);
PtcCompanionStatus ptc_companion_submit_probe_apply_today_limit(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_submit_probe_raw_block(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_submit_probe_suspend(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);
PtcCompanionStatus ptc_companion_set_disable_flag(PtcCompanionFileClient *client, bool enabled);
PtcCompanionStatus ptc_companion_read_result(
    PtcCompanionFileClient *client,
    const char *request_id,
    int elapsed_ms,
    int timeout_ms,
    char *out,
    size_t out_size);
PtcCompanionStatus ptc_companion_format_result_summary(const char *result_json, char *out, size_t out_size);
const char *ptc_companion_status_name(PtcCompanionStatus status);

#endif
