#ifndef PTC_COMPANION_FILE_PROTOCOL_H
#define PTC_COMPANION_FILE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

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
