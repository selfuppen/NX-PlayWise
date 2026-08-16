#include "pctl_adapter.h"

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "../../common/time/ptc_time.h"
#include "play_timer_settings_layout.h"

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
#define PTC_PCTL_CMD_GET_PLAY_TIMER_EVENT_TO_REQUEST_SUSPENSION 1457
#define PTC_PCTL_CMD_IS_PLAY_TIMER_ALARM_DISABLED 1458
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

/* Requests a copy handle out; caller owns the returned handle. */
static Result dispatch_out_handle(Service *service, u32 request_id, Handle *out_handle)
{
    SfDispatchParams params;
    memset(&params, 0, sizeof(params));
    params.out_handle_attrs.attr0 = SfOutHandleAttr_HipcCopy;
    params.out_handles = out_handle;
    *out_handle = INVALID_HANDLE;
    return serviceDispatchImpl(service, request_id, NULL, 0, NULL, 0, params);
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
    ptc_play_timer_settings_hex(out, out_size, settings->words, PTC_PLAY_TIMER_SETTINGS_WORDS);
}

static void settings_slots(char *out, size_t out_size, const PtcSwitchPlayTimerSettings *settings)
{
    ptc_play_timer_settings_summary(out, out_size, settings->words, PTC_PLAY_TIMER_SETTINGS_WORDS);
}

static PtcErrorCode switch_read_status(PtcPctl *pctl, uint8_t weekday, PtcPctlStatus *out)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchSession session;
    PtcErrorCode err;
    bool enabled = false;
    bool unlocked = false;
    bool alarm_disabled = false;
    bool timer_enabled = false;
    bool restricted = false;
    s64 remaining_ns = 0;
    PtcSwitchPlayTimerSettings timer_settings;
    PtcSwitchSession settings_session;
    uint16_t configured_minutes = 0;
    bool day_restricted = false;
    bool day_settings_known = false;
    Service *service;

    err = open_read_session(adapter, &session);
    if (err != PTC_ERR_OK) {
        return err;
    }
    service = &session.service;
    memset(out, 0, sizeof(*out));
    Result restriction_rc = dispatch_out(service, PTC_PCTL_CMD_IS_RESTRICTION_ENABLED, &enabled, sizeof(enabled));
    err = map_result(
        adapter,
        restriction_rc,
        PTC_ERR_PCTL_READ_FAILED);
    if (err != PTC_ERR_OK) {
        close_session(&session);
        return err;
    }
    out->restriction_enabled_available = R_SUCCEEDED(restriction_rc);
    out->restriction_enabled = enabled;
    Result unlocked_rc = dispatch_out(service, PTC_PCTL_CMD_IS_RESTRICTION_TEMPORARY_UNLOCKED, &unlocked, sizeof(unlocked));
    out->temporary_unlocked_available = R_SUCCEEDED(unlocked_rc);
    out->temporary_unlocked = unlocked;
    (void)dispatch_out(service, PTC_PCTL_CMD_IS_PLAY_TIMER_ALARM_DISABLED, &alarm_disabled, sizeof(alarm_disabled));
    if (R_SUCCEEDED(dispatch_out(service, PTC_PCTL_CMD_IS_PLAY_TIMER_ENABLED, &timer_enabled, sizeof(timer_enabled)))) {
        out->play_timer_enabled = timer_enabled;
    } else {
        out->play_timer_enabled = enabled && !alarm_disabled;
    }
    if (R_SUCCEEDED(dispatch_out(service, PTC_PCTL_CMD_GET_PLAY_TIMER_REMAINING_TIME, &remaining_ns, sizeof(remaining_ns)))) {
        out->remaining_available = true;
        out->remaining_minutes = ptc_nonnegative_minutes_from_nanoseconds(remaining_ns);
    }
    /* Do not use private command 1952 here: device observations show that it can
       track wall time since Play Timer start instead of foreground game time. */
    if (R_SUCCEEDED(dispatch_out(service, PTC_PCTL_CMD_IS_RESTRICTED_BY_PLAY_TIMER, &restricted, sizeof(restricted)))) {
        out->restricted_now = restricted;
    }
    close_session(&session);
    /* The private settings command is available through pctl:s. Failure is a
       soft degradation: ordinary status remains usable, only played time is unavailable. */
    if (weekday < PTC_PLAY_TIMER_DAY_COUNT &&
        open_write_session(adapter, &settings_session) == PTC_ERR_OK) {
        if (get_play_timer_settings(adapter, &settings_session.service, &timer_settings) == PTC_ERR_OK &&
            ptc_play_timer_settings_get_day(
                timer_settings.words,
                PTC_PLAY_TIMER_SETTINGS_WORDS,
                weekday,
                &day_restricted,
                &configured_minutes) &&
            (configured_minutes <= PTC_PLAY_TIMER_MAX_LIMIT_MINUTES || configured_minutes == PTC_PLAY_TIMER_UNLIMITED)) {
            day_settings_known = true;
        }
        close_session(&settings_session);
    }
    if (!enabled || unlocked) {
        out->unrestricted_today = true;
    } else if (day_settings_known && !day_restricted && configured_minutes == PTC_PLAY_TIMER_UNLIMITED) {
        out->unrestricted_today = true;
    } else if (day_settings_known && day_restricted && configured_minutes == 0U) {
        out->blocked_today = true;
    } else {
        out->limited_today = true;
    }
    if (out->limited_today && day_settings_known) {
        out->configured_minutes_available = true;
        out->configured_minutes = configured_minutes;
    }
    if (out->unrestricted_today) {
        out->remaining_available = false;
        out->remaining_minutes = 0;
    }
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
    uint16_t minutes;
    if (err != PTC_ERR_OK) {
        return err;
    }
    err = get_play_timer_settings(adapter, &session.service, &settings);
    if (err != PTC_ERR_OK) {
        close_session(&session);
        return err;
    }
    if (target->mode == PTC_PCTL_TARGET_UNLIMITED) {
        minutes = PTC_PLAY_TIMER_UNLIMITED;
    } else if (target->mode == PTC_PCTL_TARGET_BLOCKED) {
        minutes = 0;
    } else {
        minutes = target->minutes;
    }
    if (!ptc_play_timer_settings_set_day(
            settings.words,
            PTC_PLAY_TIMER_SETTINGS_WORDS,
            target->weekday,
            target->mode != PTC_PCTL_TARGET_UNLIMITED,
            minutes)) {
        close_session(&session);
        return PTC_ERR_PCTL_WRITE_FAILED;
    }
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

/*
 * Suspend capability is verified read-only. The play timer suspension-request event
 * (1457) is the surface Horizon uses to ask a running title to suspend, so a handle we
 * can actually wait on is the evidence that the suspend limit action is deliverable.
 * 1458 reports whether the alarm is disabled, which would silently swallow that request.
 * No settings write happens here: the raw block probe covers the write path.
 */
static PtcErrorCode __attribute__((unused)) switch_probe_suspend(PtcPctl *pctl, PtcProbeResult *out)
{
    PtcSwitchPctl *adapter = (PtcSwitchPctl *)pctl->ctx;
    PtcSwitchSession session;
    Handle suspension_event = INVALID_HANDLE;
    bool alarm_disabled = false;
    bool alarm_known;
    Result rc;
    PtcErrorCode err = open_write_session(adapter, &session);

    if (err != PTC_ERR_OK) {
        out->verified = false;
        snprintf(out->detail, sizeof(out->detail), "pctl:s session open failed result=0x%x", adapter->last_result);
        return err;
    }
    alarm_known = R_SUCCEEDED(dispatch_out(
        &session.service,
        PTC_PCTL_CMD_IS_PLAY_TIMER_ALARM_DISABLED,
        &alarm_disabled,
        sizeof(alarm_disabled)));
    rc = dispatch_out_handle(
        &session.service,
        PTC_PCTL_CMD_GET_PLAY_TIMER_EVENT_TO_REQUEST_SUSPENSION,
        &suspension_event);
    err = map_result(adapter, rc, PTC_ERR_PCTL_WRITE_FAILED);
    if (err == PTC_ERR_OK && suspension_event == INVALID_HANDLE) {
        err = PTC_ERR_PCTL_WRITE_FAILED;
    }
    if (suspension_event != INVALID_HANDLE) {
        svcCloseHandle(suspension_event);
    }
    close_session(&session);
    if (err != PTC_ERR_OK) {
        out->verified = false;
        snprintf(
            out->detail,
            sizeof(out->detail),
            "suspension event unavailable result=0x%x alarm_disabled=%s",
            adapter->last_result,
            alarm_known ? (alarm_disabled ? "true" : "false") : "unknown");
        return err;
    }
    if (alarm_disabled) {
        /* The channel exists but the console would swallow the request, so this is a
           configuration failure rather than an IPC failure: keep the successful IPC
           result and say so in the detail instead of inventing an error code. */
        out->verified = false;
        snprintf(out->detail, sizeof(out->detail), "suspension event ok but play timer alarm is disabled");
        return PTC_ERR_PCTL_WRITE_FAILED;
    }
    out->verified = true;
    snprintf(
        out->detail,
        sizeof(out->detail),
        "suspension event handle ok alarm_disabled=%s",
        alarm_known ? "false" : "unknown");
    return PTC_ERR_OK;
}

static PtcErrorCode __attribute__((unused)) switch_probe_play_timer_write(PtcPctl *pctl, PtcProbeResult *out)
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
    NULL,
    NULL,
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
