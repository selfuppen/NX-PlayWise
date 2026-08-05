#ifndef PTC_COMPANION_SELF_CHECK_H
#define PTC_COMPANION_SELF_CHECK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../platform/storage.h"

typedef enum {
    PTC_SELF_CHECK_GENERIC = 0,
    PTC_SELF_CHECK_DISABLED_STATUS = 1,
    PTC_SELF_CHECK_OBSERVE_SUCCESS = 2,
    PTC_SELF_CHECK_OBSERVE_REJECTION = 3,
    PTC_SELF_CHECK_GRANT_BEFORE_PROBE_REJECT = 4,
    PTC_SELF_CHECK_PLAY_WRITE_PROBE = 5,
    PTC_SELF_CHECK_GRANT_SUCCESS = 6,
    PTC_SELF_CHECK_GRANT_REJECTION = 7,
    PTC_SELF_CHECK_ENFORCE_SNAPSHOT = 8,
    PTC_SELF_CHECK_PLAY_TIMER_EFFECT_PROBE = 9,
    PTC_SELF_CHECK_PREPARE_DEVICE_TEST = 10,
} PtcSelfCheckProfile;

typedef enum {
    PTC_SELF_CHECK_PASS = 0,
    PTC_SELF_CHECK_WARN = 1,
    PTC_SELF_CHECK_FAIL = 2,
} PtcSelfCheckStatus;

typedef struct {
    bool has_nonce;
    uint16_t day_index;
    uint32_t nonce;
} PtcSelfCheckOptions;

typedef struct {
    PtcSelfCheckStatus status;
    int pass_count;
    int warn_count;
    int fail_count;
} PtcSelfCheckResult;

const char *ptc_self_check_profile_name(PtcSelfCheckProfile profile);
const char *ptc_self_check_status_name(PtcSelfCheckStatus status);

PtcSelfCheckResult ptc_self_check_run(
    PtcStorage *storage,
    const char *app_root,
    const char *request_id,
    PtcSelfCheckProfile profile,
    const PtcSelfCheckOptions *options,
    char *report,
    size_t report_size);

#endif
