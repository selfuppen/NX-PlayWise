#ifndef PTC_CONTROL_POLICY_H
#define PTC_CONTROL_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "../protocol/error_code.h"
#include "../rules/rules.h"

typedef enum {
    PTC_CONTROL_DISABLED = 1,
    PTC_CONTROL_OBSERVE = 2,
    PTC_CONTROL_GRANT = 3,
    PTC_CONTROL_ENFORCE = 4
} PtcControlMode;

typedef enum {
    PTC_OPERATION_STATUS = 1,
    PTC_OPERATION_GRANT_MINUTES = 2,
    PTC_OPERATION_SET_TODAY_LIMIT = 3,
    PTC_OPERATION_DISABLE_TODAY_LIMIT = 4,
    PTC_OPERATION_BLOCK_TODAY = 5,
    PTC_OPERATION_PROBE_RAW_BLOCK = 6,
    PTC_OPERATION_PROBE_SUSPEND = 7,
    PTC_OPERATION_RULE_UPDATE = 8
} PtcOperation;

typedef struct {
    bool raw_block_verified;
    bool suspend_verified;
} PtcCapabilities;

typedef struct {
    bool dry_run;
    bool may_read_pctl;
    bool may_write_pctl;
    bool requires_backup;
    bool consume_nonce_after_success;
    PtcErrorCode error;
} PtcPolicyDecision;

PtcControlMode ptc_control_mode_from_string(const char *value);
const char *ptc_control_mode_name(PtcControlMode mode);
PtcPolicyDecision ptc_policy_decide(
    PtcControlMode mode,
    bool disable_flag,
    PtcOperation operation,
    const PtcCapabilities *capabilities,
    bool current_unlimited,
    bool allow_unlimited_to_limited);

#endif
