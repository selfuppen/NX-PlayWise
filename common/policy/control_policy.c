#include "control_policy.h"

#include <string.h>

PtcControlMode ptc_control_mode_from_string(const char *value)
{
    if (value && strcmp(value, "disabled") == 0) {
        return PTC_CONTROL_DISABLED;
    }
    if (value && strcmp(value, "grant") == 0) {
        return PTC_CONTROL_GRANT;
    }
    if (value && strcmp(value, "enforce") == 0) {
        return PTC_CONTROL_ENFORCE;
    }
    return PTC_CONTROL_OBSERVE;
}

const char *ptc_control_mode_name(PtcControlMode mode)
{
    switch (mode) {
    case PTC_CONTROL_DISABLED:
        return "disabled";
    case PTC_CONTROL_GRANT:
        return "grant";
    case PTC_CONTROL_ENFORCE:
        return "enforce";
    case PTC_CONTROL_OBSERVE:
    default:
        return "observe";
    }
}

static bool operation_is_write(PtcOperation operation)
{
    return operation != PTC_OPERATION_STATUS;
}

PtcPolicyDecision ptc_policy_decide(
    PtcControlMode mode,
    bool disable_flag,
    PtcOperation operation,
    const PtcCapabilities *capabilities,
    bool current_unlimited,
    bool allow_unlimited_to_limited)
{
    PtcPolicyDecision out;
    out.dry_run = true;
    out.may_read_pctl = false;
    out.may_write_pctl = false;
    out.requires_backup = false;
    out.consume_nonce_after_success = false;
    out.error = PTC_ERR_OK;

    if (disable_flag || mode == PTC_CONTROL_DISABLED) {
        out.error = PTC_ERR_DISABLED;
        return out;
    }

    out.may_read_pctl = true;
    if (operation == PTC_OPERATION_BLOCK_TODAY &&
        (!capabilities || !capabilities->raw_block_verified)) {
        out.error = PTC_ERR_RAW_BLOCK_NOT_VERIFIED;
        return out;
    }
    if (mode == PTC_CONTROL_OBSERVE) {
        return out;
    }

    if ((operation == PTC_OPERATION_GRANT_MINUTES || operation == PTC_OPERATION_SET_TODAY_LIMIT) &&
        current_unlimited && !allow_unlimited_to_limited) {
        out.error = PTC_ERR_UNLIMITED_NOT_ALLOWED;
        return out;
    }

    if (operation_is_write(operation)) {
        out.dry_run = false;
        out.may_write_pctl = true;
        out.requires_backup = true;
        out.consume_nonce_after_success = operation == PTC_OPERATION_GRANT_MINUTES;
    }
    return out;
}
