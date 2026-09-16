#ifndef PTC_CONTROL_POLICY_H
#define PTC_CONTROL_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "../protocol/error_code.h"
#include "../rules/rules.h"

typedef enum {
    PTC_OPERATION_STATUS = 1,
    PTC_OPERATION_GRANT_MINUTES = 2,
    PTC_OPERATION_SET_TODAY_LIMIT = 3,
    PTC_OPERATION_DISABLE_TODAY_LIMIT = 4,
    PTC_OPERATION_RULE_UPDATE = 8
} PtcOperation;

typedef struct {
    bool dry_run;
    bool may_read_pctl;
    bool may_write_pctl;
    bool requires_backup;
    bool consume_nonce_after_success;
    PtcErrorCode error;
} PtcPolicyDecision;

PtcPolicyDecision ptc_policy_decide(
    bool disable_flag,
    PtcOperation operation);

#endif
