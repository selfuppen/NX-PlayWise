#include "pctl_adapter.h"

#include <stdio.h>
#include <string.h>
#include <switch.h>

#define PTC_PCTL_CMD_START_PLAY_TIMER 1451
#define PTC_PCTL_CMD_STOP_PLAY_TIMER 1452
#define PTC_PCTL_CMD_IS_PLAY_TIMER_ENABLED 1453
#define PTC_PCTL_CMD_GET_PLAY_TIMER_REMAINING_TIME 1454
#define PTC_PCTL_CMD_IS_RESTRICTED_BY_PLAY_TIMER 1455
#define PTC_PCTL_CMD_GET_PLAY_TIMER_SETTINGS 145601
#define PTC_PCTL_CMD_SET_PLAY_TIMER_SETTINGS_FOR_DEBUG 195101

#define PTC_PLAY_TIMER_SETTINGS_WORDS 34
#define PTC_PLAY_TIMER_DAY_STRIDE 4
#define PTC_PLAY_TIMER_UNLIMITED 0xffff

typedef struct {
    u16 words[PTC_PLAY_TIMER_SETTINGS_WORDS];
} PtcSwitchPlayTimerSettings;

typedef char PtcSwitchPlayTimerSettingsSizeCheck[
    sizeof(PtcSwitchPlayTimerSettings) == 0x44 ? 1 : -1];

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

static PtcErrorCode get_play_timer_settings(PtcSwitchPlayTimerSettings *settings)
{
    Service *service = pctlGetServiceSession_Service();
    Result rc = serviceDispatchOut(service, PTC_PCTL_CMD_GET_PLAY_TIMER_SETTINGS, *settings);
    return R_SUCCEEDED(rc) ? PTC_ERR_OK : PTC_ERR_PCTL_READ_FAILED;
}

static PtcErrorCode set_play_timer_settings(const PtcSwitchPlayTimerSettings *settings)
{
    Service *service = pctlGetServiceSession_Service();
    Result rc = serviceDispatchIn(service, PTC_PCTL_CMD_SET_PLAY_TIMER_SETTINGS_FOR_DEBUG, *settings);
    return R_SUCCEEDED(rc) ? PTC_ERR_OK : PTC_ERR_PCTL_WRITE_FAILED;
}

static void settings_hex(char *out, size_t out_size, const PtcSwitchPlayTimerSettings *settings)
{
    size_t used = 0;
    size_t i;
    for (i = 0; i < PTC_PLAY_TIMER_SETTINGS_WORDS && used + 5 < out_size; ++i) {
        int written = snprintf(out + used, out_size - used, "%04x", settings->words[i]);
        if (written < 0) {
            break;
        }
        used += (size_t)written;
    }
}

static PtcErrorCode switch_read_status(PtcPctl *pctl, PtcPctlStatus *out)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcErrorCode err = ensure_pctl(adapter);
    bool enabled = false;
    bool unlocked = false;
    bool alarm_disabled = false;
    bool timer_enabled = false;
    bool restricted = false;
    u32 remaining = 0;
    Service *service;
    if (err != PTC_ERR_OK) {
        return err;
    }
    service = pctlGetServiceSession_Service();
    memset(out, 0, sizeof(*out));
    if (R_FAILED(pctlIsRestrictionEnabled(&enabled))) {
        return PTC_ERR_PCTL_READ_FAILED;
    }
    (void)pctlIsRestrictionTemporaryUnlocked(&unlocked);
    (void)pctlIsPlayTimerAlarmDisabled(&alarm_disabled);
    if (R_SUCCEEDED(serviceDispatchOut(service, PTC_PCTL_CMD_IS_PLAY_TIMER_ENABLED, timer_enabled))) {
        out->play_timer_enabled = timer_enabled;
    } else {
        out->play_timer_enabled = enabled && !alarm_disabled;
    }
    if (R_SUCCEEDED(serviceDispatchOut(service, PTC_PCTL_CMD_GET_PLAY_TIMER_REMAINING_TIME, remaining))) {
        out->remaining_available = true;
        out->remaining_minutes = (uint16_t)(remaining > 65535 ? 65535 : remaining);
    }
    if (R_SUCCEEDED(serviceDispatchOut(service, PTC_PCTL_CMD_IS_RESTRICTED_BY_PLAY_TIMER, restricted))) {
        out->restricted_now = restricted;
    }
    out->unrestricted_today = !enabled || unlocked;
    out->limited_today = enabled && !unlocked;
    out->blocked_today = false;
    return PTC_ERR_OK;
}

static PtcErrorCode switch_backup(PtcPctl *pctl, PtcPctlBackup *out)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcErrorCode err = ensure_pctl(adapter);
    PtcSwitchPlayTimerSettings timer_settings;
    char raw_hex[160];
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
    memset(&timer_settings, 0, sizeof(timer_settings));
    err = get_play_timer_settings(&timer_settings);
    if (err != PTC_ERR_OK) {
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    settings_hex(raw_hex, sizeof(raw_hex), &timer_settings);
    snprintf(
        out->text,
        sizeof(out->text),
        "pctl_current_settings rating_age=%u sns=%u free_comm=%u restriction_enabled=%u temporary_unlocked=%u play_timer_settings_hex=%s",
        (unsigned int)settings.rating_age,
        settings.sns_post_restriction ? 1U : 0U,
        settings.free_communication_restriction ? 1U : 0U,
        enabled ? 1U : 0U,
        unlocked ? 1U : 0U,
        raw_hex);
    return PTC_ERR_OK;
}

static PtcErrorCode switch_apply_target(PtcPctl *pctl, const PtcPctlTarget *target)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchPlayTimerSettings settings;
    PtcErrorCode err = ensure_pctl(adapter);
    unsigned int weekday;
    unsigned int base;
    if (err != PTC_ERR_OK) {
        return err;
    }
    err = get_play_timer_settings(&settings);
    if (err != PTC_ERR_OK) {
        return err;
    }
    weekday = target->weekday % 7U;
    base = weekday * PTC_PLAY_TIMER_DAY_STRIDE;
    settings.words[base] = 1;
    settings.words[base + 1] = target->mode == PTC_PCTL_TARGET_UNLIMITED ? 0 : 1;
    if (target->mode == PTC_PCTL_TARGET_UNLIMITED) {
        settings.words[base + 2] = PTC_PLAY_TIMER_UNLIMITED;
    } else if (target->mode == PTC_PCTL_TARGET_BLOCKED) {
        settings.words[base + 2] = 0;
    } else {
        settings.words[base + 2] = target->minutes;
    }
    settings.words[base + 3] = 0;
    return set_play_timer_settings(&settings);
}

static PtcErrorCode switch_start_timer(PtcPctl *pctl)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcErrorCode err = ensure_pctl(adapter);
    Result rc;
    if (err != PTC_ERR_OK) {
        return err;
    }
    rc = serviceDispatch(pctlGetServiceSession_Service(), PTC_PCTL_CMD_START_PLAY_TIMER);
    return R_SUCCEEDED(rc) ? PTC_ERR_OK : PTC_ERR_PCTL_WRITE_FAILED;
}

static PtcErrorCode switch_stop_timer(PtcPctl *pctl)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcErrorCode err = ensure_pctl(adapter);
    Result rc;
    if (err != PTC_ERR_OK) {
        return err;
    }
    rc = serviceDispatch(pctlGetServiceSession_Service(), PTC_PCTL_CMD_STOP_PLAY_TIMER);
    return R_SUCCEEDED(rc) ? PTC_ERR_OK : PTC_ERR_PCTL_WRITE_FAILED;
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

static PtcErrorCode switch_probe_play_timer_write(PtcPctl *pctl, PtcProbeResult *out)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchPlayTimerSettings before;
    PtcSwitchPlayTimerSettings after;
    PtcErrorCode err = ensure_pctl(adapter);
    if (err != PTC_ERR_OK) {
        out->verified = false;
        snprintf(out->detail, sizeof(out->detail), "pctl init failed");
        return err;
    }
    err = get_play_timer_settings(&before);
    if (err != PTC_ERR_OK) {
        out->verified = false;
        snprintf(out->detail, sizeof(out->detail), "get play timer settings failed");
        return err;
    }
    err = set_play_timer_settings(&before);
    if (err != PTC_ERR_OK) {
        out->verified = false;
        snprintf(out->detail, sizeof(out->detail), "set play timer settings failed");
        return err;
    }
    err = get_play_timer_settings(&after);
    if (err != PTC_ERR_OK || memcmp(&before, &after, sizeof(before)) != 0) {
        out->verified = false;
        snprintf(out->detail, sizeof(out->detail), "play timer write readback mismatch");
        return err == PTC_ERR_OK ? PTC_ERR_PCTL_WRITE_FAILED : err;
    }
    out->verified = true;
    snprintf(out->detail, sizeof(out->detail), "play timer write readback ok");
    return PTC_ERR_OK;
}

static const PtcPctlVTable SWITCH_PCTL_VTABLE = {
    switch_read_status,
    switch_backup,
    switch_apply_target,
    switch_start_timer,
    switch_stop_timer,
    switch_probe_raw_block,
    switch_probe_suspend,
    switch_probe_play_timer_write,
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
