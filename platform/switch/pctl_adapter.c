#include "pctl_adapter.h"

#include <stdio.h>
#include <string.h>
#include <switch.h>

#define PTC_PCTL_FACTORY_CREATE_SERVICE 1
#define PTC_PCTL_CMD_INITIALIZE 1
#define PTC_PCTL_CMD_IS_RESTRICTION_TEMPORARY_UNLOCKED 1006
#define PTC_PCTL_CMD_IS_RESTRICTION_ENABLED 1031
#define PTC_PCTL_CMD_GET_CURRENT_SETTINGS 1035
#define PTC_PCTL_CMD_START_PLAY_TIMER 1451
#define PTC_PCTL_CMD_STOP_PLAY_TIMER 1452
#define PTC_PCTL_CMD_IS_PLAY_TIMER_ENABLED 1453
#define PTC_PCTL_CMD_GET_PLAY_TIMER_REMAINING_TIME 1454
#define PTC_PCTL_CMD_IS_RESTRICTED_BY_PLAY_TIMER 1455
#define PTC_PCTL_CMD_GET_PLAY_TIMER_SETTINGS 145601
#define PTC_PCTL_CMD_SET_PLAY_TIMER_SETTINGS_FOR_DEBUG 195101
#define PTC_PCTL_CMD_IS_PLAY_TIMER_ALARM_DISABLED 1458

#define PTC_PLAY_TIMER_SETTINGS_WORDS 34
#define PTC_PLAY_TIMER_DAY_STRIDE 4
#define PTC_PLAY_TIMER_UNLIMITED 0xffff

typedef struct {
    u16 words[PTC_PLAY_TIMER_SETTINGS_WORDS];
} PtcSwitchPlayTimerSettings;

typedef char PtcSwitchPlayTimerSettingsSizeCheck[
    sizeof(PtcSwitchPlayTimerSettings) == 0x44 ? 1 : -1];

typedef struct {
    Service factory;
    Service service;
} PtcSwitchSession;

static Result dispatch_no_io(Service *service, u32 request_id)
{
    SfDispatchParams params;
    memset(&params, 0, sizeof(params));
    return serviceDispatchImpl(service, request_id, NULL, 0, NULL, 0, params);
}

static Result dispatch_in(Service *service, u32 request_id, const void *in_data, u32 in_size)
{
    SfDispatchParams params;
    memset(&params, 0, sizeof(params));
    return serviceDispatchImpl(service, request_id, in_data, in_size, NULL, 0, params);
}

static Result dispatch_out(Service *service, u32 request_id, void *out_data, u32 out_size)
{
    SfDispatchParams params;
    memset(&params, 0, sizeof(params));
    return serviceDispatchImpl(service, request_id, NULL, 0, out_data, out_size, params);
}

static Result open_session(PtcSwitchPctl *adapter, const char *service_name, PtcSwitchSession *session)
{
    u64 reserved_pid = 0;
    SfDispatchParams params;
    Result rc;

    memset(session, 0, sizeof(*session));
    rc = smGetService(&session->factory, service_name);
    if (R_FAILED(rc)) {
        adapter->last_result = rc;
        return rc;
    }
    rc = serviceConvertToDomain(&session->factory);
    if (R_FAILED(rc)) {
        adapter->last_result = rc;
        serviceClose(&session->factory);
        return rc;
    }
    memset(&params, 0, sizeof(params));
    params.in_send_pid = true;
    params.out_num_objects = 1;
    params.out_objects = &session->service;
    rc = serviceDispatchImpl(
        &session->factory,
        PTC_PCTL_FACTORY_CREATE_SERVICE,
        &reserved_pid,
        sizeof(reserved_pid),
        NULL,
        0,
        params);
    if (R_FAILED(rc)) {
        adapter->last_result = rc;
        serviceClose(&session->factory);
        return rc;
    }
    rc = dispatch_no_io(&session->service, PTC_PCTL_CMD_INITIALIZE);
    if (R_FAILED(rc)) {
        adapter->last_result = rc;
        serviceClose(&session->service);
        serviceClose(&session->factory);
        return rc;
    }
    return 0;
}

static void close_session(PtcSwitchSession *session)
{
    serviceClose(&session->service);
    serviceClose(&session->factory);
}

static PtcErrorCode map_result(PtcSwitchPctl *adapter, Result rc, PtcErrorCode error)
{
    adapter->last_result = rc;
    return R_SUCCEEDED(rc) ? PTC_ERR_OK : error;
}

static PtcErrorCode open_read_session(PtcSwitchPctl *adapter, PtcSwitchSession *session)
{
    return map_result(adapter, open_session(adapter, "pctl", session), PTC_ERR_PCTL_INIT_FAILED);
}

static PtcErrorCode open_write_session(PtcSwitchPctl *adapter, PtcSwitchSession *session)
{
    return map_result(adapter, open_session(adapter, "pctl:s", session), PTC_ERR_PCTL_INIT_FAILED);
}

static PtcErrorCode get_play_timer_settings(
    PtcSwitchPctl *adapter,
    Service *service,
    PtcSwitchPlayTimerSettings *settings)
{
    Result rc = dispatch_out(service, PTC_PCTL_CMD_GET_PLAY_TIMER_SETTINGS, settings, sizeof(*settings));
    return map_result(adapter, rc, PTC_ERR_PCTL_READ_FAILED);
}

static PtcErrorCode set_play_timer_settings(
    PtcSwitchPctl *adapter,
    Service *service,
    const PtcSwitchPlayTimerSettings *settings)
{
    Result rc = dispatch_in(service, PTC_PCTL_CMD_SET_PLAY_TIMER_SETTINGS_FOR_DEBUG, settings, sizeof(*settings));
    return map_result(adapter, rc, PTC_ERR_PCTL_WRITE_FAILED);
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

static void settings_slots(char *out, size_t out_size, const PtcSwitchPlayTimerSettings *settings)
{
    size_t used = 0;
    unsigned int day;
    out[0] = '\0';
    for (day = 0; day < 7U && used + 1 < out_size; ++day) {
        unsigned int base = day * PTC_PLAY_TIMER_DAY_STRIDE;
        int written = snprintf(
            out + used,
            out_size - used,
            "%sd%u:e%u,l%u,m%u,x%u",
            day == 0U ? "" : ";",
            day,
            (unsigned int)settings->words[base],
            (unsigned int)settings->words[base + 1],
            (unsigned int)settings->words[base + 2],
            (unsigned int)settings->words[base + 3]);
        if (written < 0 || (size_t)written >= out_size - used) {
            break;
        }
        used += (size_t)written;
    }
}

static PtcErrorCode switch_read_status(PtcPctl *pctl, PtcPctlStatus *out)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchSession session;
    PtcErrorCode err;
    bool enabled = false;
    bool unlocked = false;
    bool alarm_disabled = false;
    bool timer_enabled = false;
    bool restricted = false;
    u32 remaining = 0;
    Service *service;

    err = open_read_session(adapter, &session);
    if (err != PTC_ERR_OK) {
        return err;
    }
    service = &session.service;
    memset(out, 0, sizeof(*out));
    err = map_result(
        adapter,
        dispatch_out(service, PTC_PCTL_CMD_IS_RESTRICTION_ENABLED, &enabled, sizeof(enabled)),
        PTC_ERR_PCTL_READ_FAILED);
    if (err != PTC_ERR_OK) {
        close_session(&session);
        return err;
    }
    (void)dispatch_out(service, PTC_PCTL_CMD_IS_RESTRICTION_TEMPORARY_UNLOCKED, &unlocked, sizeof(unlocked));
    (void)dispatch_out(service, PTC_PCTL_CMD_IS_PLAY_TIMER_ALARM_DISABLED, &alarm_disabled, sizeof(alarm_disabled));
    if (R_SUCCEEDED(dispatch_out(service, PTC_PCTL_CMD_IS_PLAY_TIMER_ENABLED, &timer_enabled, sizeof(timer_enabled)))) {
        out->play_timer_enabled = timer_enabled;
    } else {
        out->play_timer_enabled = enabled && !alarm_disabled;
    }
    if (R_SUCCEEDED(dispatch_out(service, PTC_PCTL_CMD_GET_PLAY_TIMER_REMAINING_TIME, &remaining, sizeof(remaining)))) {
        out->remaining_available = true;
        out->remaining_minutes = (uint16_t)(remaining > 65535 ? 65535 : remaining);
    }
    if (R_SUCCEEDED(dispatch_out(service, PTC_PCTL_CMD_IS_RESTRICTED_BY_PLAY_TIMER, &restricted, sizeof(restricted)))) {
        out->restricted_now = restricted;
    }
    out->unrestricted_today = !enabled || unlocked;
    out->limited_today = enabled && !unlocked;
    out->blocked_today = false;
    close_session(&session);
    return PTC_ERR_OK;
}

static PtcErrorCode switch_backup(PtcPctl *pctl, PtcPctlBackup *out)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchSession session;
    PtcSwitchPlayTimerSettings timer_settings;
    PctlRestrictionSettings settings;
    char raw_hex[160];
    char slots[320];
    bool enabled = false;
    bool unlocked = false;
    PtcErrorCode err = open_write_session(adapter, &session);
    if (err != PTC_ERR_OK) {
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    memset(&settings, 0, sizeof(settings));
    (void)dispatch_out(&session.service, PTC_PCTL_CMD_IS_RESTRICTION_ENABLED, &enabled, sizeof(enabled));
    (void)dispatch_out(&session.service, PTC_PCTL_CMD_IS_RESTRICTION_TEMPORARY_UNLOCKED, &unlocked, sizeof(unlocked));
    err = map_result(
        adapter,
        dispatch_out(&session.service, PTC_PCTL_CMD_GET_CURRENT_SETTINGS, &settings, sizeof(settings)),
        PTC_ERR_PCTL_BACKUP_FAILED);
    if (err != PTC_ERR_OK) {
        close_session(&session);
        return err;
    }
    memset(&timer_settings, 0, sizeof(timer_settings));
    err = get_play_timer_settings(adapter, &session.service, &timer_settings);
    close_session(&session);
    if (err != PTC_ERR_OK) {
        return PTC_ERR_PCTL_BACKUP_FAILED;
    }
    settings_hex(raw_hex, sizeof(raw_hex), &timer_settings);
    settings_slots(slots, sizeof(slots), &timer_settings);
    snprintf(
        out->text,
        sizeof(out->text),
        "pctl_current_settings rating_age=%u sns=%u free_comm=%u restriction_enabled=%u temporary_unlocked=%u play_timer_settings_hex=%s play_timer_slots=%s",
        (unsigned int)settings.rating_age,
        settings.sns_post_restriction ? 1U : 0U,
        settings.free_communication_restriction ? 1U : 0U,
        enabled ? 1U : 0U,
        unlocked ? 1U : 0U,
        raw_hex,
        slots);
    return PTC_ERR_OK;
}

static PtcErrorCode switch_apply_target(PtcPctl *pctl, const PtcPctlTarget *target)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchSession session;
    PtcSwitchPlayTimerSettings settings;
    PtcErrorCode err = open_write_session(adapter, &session);
    unsigned int weekday;
    unsigned int base;
    if (err != PTC_ERR_OK) {
        return err;
    }
    err = get_play_timer_settings(adapter, &session.service, &settings);
    if (err != PTC_ERR_OK) {
        close_session(&session);
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
    err = set_play_timer_settings(adapter, &session.service, &settings);
    close_session(&session);
    return err;
}

static PtcErrorCode switch_start_timer(PtcPctl *pctl)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchSession session;
    PtcErrorCode err = open_write_session(adapter, &session);
    Result rc;
    if (err != PTC_ERR_OK) {
        return err;
    }
    rc = dispatch_no_io(&session.service, PTC_PCTL_CMD_START_PLAY_TIMER);
    close_session(&session);
    return map_result(adapter, rc, PTC_ERR_PCTL_WRITE_FAILED);
}

static PtcErrorCode switch_stop_timer(PtcPctl *pctl)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchSession session;
    PtcErrorCode err = open_write_session(adapter, &session);
    Result rc;
    if (err != PTC_ERR_OK) {
        return err;
    }
    rc = dispatch_no_io(&session.service, PTC_PCTL_CMD_STOP_PLAY_TIMER);
    close_session(&session);
    return map_result(adapter, rc, PTC_ERR_PCTL_WRITE_FAILED);
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
    PtcSwitchSession session;
    PtcSwitchPlayTimerSettings before;
    PtcSwitchPlayTimerSettings after;
    PtcErrorCode err = open_write_session(adapter, &session);
    if (err != PTC_ERR_OK) {
        out->verified = false;
        snprintf(out->detail, sizeof(out->detail), "pctl:s session open failed result=0x%x", adapter->last_result);
        return err;
    }
    err = get_play_timer_settings(adapter, &session.service, &before);
    if (err == PTC_ERR_OK) {
        err = set_play_timer_settings(adapter, &session.service, &before);
    }
    if (err == PTC_ERR_OK) {
        err = get_play_timer_settings(adapter, &session.service, &after);
    }
    close_session(&session);
    if (err != PTC_ERR_OK || memcmp(&before, &after, sizeof(before)) != 0) {
        out->verified = false;
        snprintf(out->detail, sizeof(out->detail), "pctl:s play timer write readback failed result=0x%x", adapter->last_result);
        return err == PTC_ERR_OK ? PTC_ERR_PCTL_WRITE_FAILED : err;
    }
    out->verified = true;
    snprintf(out->detail, sizeof(out->detail), "pctl:s play timer write readback ok");
    return PTC_ERR_OK;
}

static PtcErrorCode switch_snapshot_settings(PtcPctl *pctl, PtcPctlSettingsSnapshot *out)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchSession session;
    PtcSwitchPlayTimerSettings settings;
    bool timer_enabled = false;
    PtcErrorCode err = open_write_session(adapter, &session);
    if (err != PTC_ERR_OK) {
        return err;
    }
    err = get_play_timer_settings(adapter, &session.service, &settings);
    if (err == PTC_ERR_OK) {
        (void)dispatch_out(&session.service, PTC_PCTL_CMD_IS_PLAY_TIMER_ENABLED, &timer_enabled, sizeof(timer_enabled));
        memset(out, 0, sizeof(*out));
        memcpy(out->data, settings.words, sizeof(settings.words));
        out->size = sizeof(settings.words);
        out->timer_enabled = timer_enabled;
    }
    close_session(&session);
    return err;
}

static PtcErrorCode switch_restore_settings(PtcPctl *pctl, const PtcPctlSettingsSnapshot *snapshot)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchSession session;
    PtcSwitchPlayTimerSettings settings;
    PtcErrorCode err;
    if (!snapshot || snapshot->size != sizeof(settings.words)) {
        return PTC_ERR_PCTL_WRITE_FAILED;
    }
    memcpy(settings.words, snapshot->data, sizeof(settings.words));
    err = open_write_session(adapter, &session);
    if (err != PTC_ERR_OK) {
        return err;
    }
    err = set_play_timer_settings(adapter, &session.service, &settings);
    close_session(&session);
    return err;
}

static PtcErrorCode switch_debug_snapshot(PtcPctl *pctl, PtcPctlDebugSnapshot *out)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchSession session;
    PtcSwitchPlayTimerSettings settings;
    PtcErrorCode err;

    memset(out, 0, sizeof(*out));
    err = open_write_session(adapter, &session);
    if (err != PTC_ERR_OK) {
        out->available = false;
        out->error = err;
        out->ipc_result = adapter->last_result;
        return err;
    }
    err = get_play_timer_settings(adapter, &session.service, &settings);
    close_session(&session);
    out->available = err == PTC_ERR_OK;
    out->error = err;
    out->ipc_result = adapter->last_result;
    if (err != PTC_ERR_OK) {
        return err;
    }
    settings_hex(out->raw_hex, sizeof(out->raw_hex), &settings);
    settings_slots(out->decoded_slots, sizeof(out->decoded_slots), &settings);
    return PTC_ERR_OK;
}

static uint32_t switch_last_ipc_result(PtcPctl *pctl)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    return adapter->last_result;
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
    switch_snapshot_settings,
    switch_restore_settings,
    switch_debug_snapshot,
    switch_last_ipc_result,
};

void ptc_switch_pctl_init(PtcSwitchPctl *adapter)
{
    memset(adapter, 0, sizeof(*adapter));
    adapter->pctl.vtable = &SWITCH_PCTL_VTABLE;
    adapter->pctl.ctx = adapter;
}

void ptc_switch_pctl_exit(PtcSwitchPctl *adapter)
{
    (void)adapter;
}

PtcPctl *ptc_switch_pctl_as_pctl(PtcSwitchPctl *adapter)
{
    return &adapter->pctl;
}
