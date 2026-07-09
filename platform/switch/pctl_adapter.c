#include "pctl_adapter.h"

#include <stdio.h>
#include <string.h>
#include <switch.h>

static PtcErrorCode ensure_pctl(PtcSwitchPctl *adapter)
{
    Result rc;
    if (adapter->initialized) {
        return PTC_ERR_OK;
    }
    rc = pctlInitialize();
    if (R_FAILED(rc)) {
        return PTC_ERR_PCTL_INIT_FAILED;
    }
    adapter->initialized = true;
    return PTC_ERR_OK;
}

static PtcErrorCode switch_read_status(PtcPctl *pctl, PtcPctlStatus *out)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcErrorCode err = ensure_pctl(adapter);
    bool enabled = false;
    bool unlocked = false;
    bool alarm_disabled = false;
    if (err != PTC_ERR_OK) {
        return err;
    }
    memset(out, 0, sizeof(*out));
    if (R_FAILED(pctlIsRestrictionEnabled(&enabled))) {
        return PTC_ERR_PCTL_READ_FAILED;
    }
    (void)pctlIsRestrictionTemporaryUnlocked(&unlocked);
    (void)pctlIsPlayTimerAlarmDisabled(&alarm_disabled);
    out->play_timer_enabled = enabled && !alarm_disabled;
    out->unrestricted_today = !enabled || unlocked;
    out->limited_today = enabled && !unlocked;
    out->blocked_today = false;
    out->remaining_available = false;
    out->remaining_minutes = 0;
    out->restricted_now = false;
    return PTC_ERR_OK;
}

static PtcErrorCode switch_backup(PtcPctl *pctl, PtcPctlBackup *out)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcErrorCode err = ensure_pctl(adapter);
    PctlRestrictionSettings settings;
    bool enabled = false;
    bool unlocked = false;
    if (err != PTC_ERR_OK) {
        return err;
    }
    memset(&settings, 0, sizeof(settings));
    (void)pctlIsRestrictionEnabled(&enabled);
    (void)pctlIsRestrictionTemporaryUnlocked(&unlocked);
    if (R_FAILED(pctlGetCurrentSettings(&settings))) {
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    snprintf(
        out->text,
        sizeof(out->text),
        "pctl_current_settings rating_age=%u sns=%u free_comm=%u restriction_enabled=%u temporary_unlocked=%u play_timer_raw_unverified=1",
        (unsigned int)settings.rating_age,
        settings.sns_post_restriction ? 1U : 0U,
        settings.free_communication_restriction ? 1U : 0U,
        enabled ? 1U : 0U,
        unlocked ? 1U : 0U);
    return PTC_ERR_OK;
}

static PtcErrorCode switch_apply_target(PtcPctl *pctl, const PtcPctlTarget *target)
{
    (void)pctl;
    (void)target;
    return PTC_ERR_PCTL_WRITE_FAILED;
}

static PtcErrorCode switch_start_timer(PtcPctl *pctl)
{
    (void)pctl;
    return PTC_ERR_PCTL_WRITE_FAILED;
}

static PtcErrorCode switch_stop_timer(PtcPctl *pctl)
{
    (void)pctl;
    return PTC_ERR_PCTL_WRITE_FAILED;
}

static PtcErrorCode switch_probe_raw_block(PtcPctl *pctl, PtcProbeResult *out)
{
    (void)pctl;
    out->verified = false;
    snprintf(out->detail, sizeof(out->detail), "raw block probe requires verified play timer write adapter");
    return PTC_ERR_PCTL_WRITE_FAILED;
}

static PtcErrorCode switch_probe_suspend(PtcPctl *pctl, PtcProbeResult *out)
{
    (void)pctl;
    out->verified = false;
    snprintf(out->detail, sizeof(out->detail), "suspend probe requires verified play timer write adapter");
    return PTC_ERR_PCTL_WRITE_FAILED;
}

static const PtcPctlVTable SWITCH_PCTL_VTABLE = {
    switch_read_status,
    switch_backup,
    switch_apply_target,
    switch_start_timer,
    switch_stop_timer,
    switch_probe_raw_block,
    switch_probe_suspend,
};

void ptc_switch_pctl_init(PtcSwitchPctl *adapter)
{
    memset(adapter, 0, sizeof(*adapter));
    adapter->pctl.vtable = &SWITCH_PCTL_VTABLE;
    adapter->pctl.ctx = adapter;
}

void ptc_switch_pctl_exit(PtcSwitchPctl *adapter)
{
    if (adapter->initialized) {
        pctlExit();
        adapter->initialized = false;
    }
}

PtcPctl *ptc_switch_pctl_as_pctl(PtcSwitchPctl *adapter)
{
    return &adapter->pctl;
}
