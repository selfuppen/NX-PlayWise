#include "control_policy.h"

const char *ptc_control_mode_name(PtcControlMode mode)
{
    (void)mode;
    return "release";
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

    if (operation == PTC_OPERATION_STATUS) {
        out.may_read_pctl = true;
        return out;
    }

    if (disable_flag) {
        out.error = PTC_ERR_DISABLED;
        return out;
    }

    out.may_read_pctl = true;
#ifdef PLAYWISE_DEVICE_LAB
    (void)capabilities;
#else
    (void)capabilities;
#endif
    (void)mode;
    (void)current_unlimited;
    (void)allow_unlimited_to_limited;

    if (operation_is_write(operation)) {
        out.dry_run = false;
        out.may_write_pctl = true;
        out.requires_backup = true;
        out.consume_nonce_after_success = operation == PTC_OPERATION_GRANT_MINUTES;
    }
    return out;
}
