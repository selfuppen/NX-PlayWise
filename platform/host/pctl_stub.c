#include "pctl_stub.h"

#include <stdio.h>
#include <string.h>

#include "../switch/play_timer_settings_layout.h"

static uint16_t stub_minutes_for_status(const PtcPctlStatus *status)
{
    if (status->unrestricted_today) {
        return 0xffffu;
    }
    if (status->blocked_today) {
        return 0u;
    }
    return status->remaining_minutes > UINT16_MAX
        ? UINT16_MAX
        : (uint16_t)status->remaining_minutes;
}

static uint16_t stub_configured_minutes(const PtcPctlStub *stub)
{
    return stub->model_elapsed_time ? stub->configured_minutes : stub_minutes_for_status(&stub->status);
}

static void stub_raw_and_slots(const PtcPctlStub *stub, char *raw_hex, size_t raw_size, char *slots, size_t slots_size)
{
    const PtcPctlStatus *status = &stub->status;
    uint16_t words[PTC_PLAY_TIMER_SETTINGS_WORDS];
    uint16_t minutes = stub_configured_minutes(stub);
    unsigned int day;
    memset(words, 0, sizeof(words));
    words[0] = 0x0101U;
    words[1] = 1U;
    for (day = 0; day < PTC_PLAY_TIMER_DAY_COUNT; ++day) {
        unsigned int base = PTC_PLAY_TIMER_HEADER_WORDS + (day * PTC_PLAY_TIMER_DAY_WORDS);
        words[base + PTC_PLAY_TIMER_DAY_FLAG_WORD] = status->unrestricted_today ? 0U : PTC_PLAY_TIMER_DAY_CONFIGURED;
        words[base + PTC_PLAY_TIMER_DAY_ENABLE_WORD] = status->limited_today || status->blocked_today ? PTC_PLAY_TIMER_DAY_RESTRICTED : 0U;
        words[base + PTC_PLAY_TIMER_DAY_MINUTES_WORD] = minutes;
    }
    ptc_play_timer_settings_hex(raw_hex, raw_size, words, PTC_PLAY_TIMER_SETTINGS_WORDS);
    ptc_play_timer_settings_summary(slots, slots_size, words, PTC_PLAY_TIMER_SETTINGS_WORDS);
}

static PtcErrorCode stub_read_status(PtcPctl *pctl, uint8_t weekday, PtcPctlStatus *out)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    (void)weekday;
    if (stub->read_error != PTC_ERR_OK || (stub->read_fails_after_apply && stub->applied)) {
        if (stub->read_error == PTC_ERR_OK) {
            return PTC_ERR_PCTL_READ_FAILED;
        }
        return stub->read_error;
    }
    *out = stub->status;
    if (stub->model_elapsed_time) {
        out->played_minutes_available = true;
        out->played_minutes = stub->played_minutes_today;
    }
    if (stub->model_elapsed_time && out->limited_today) {
        out->configured_minutes_available = true;
        out->configured_minutes = stub->configured_minutes;
    }
    if (stub->expiry_observed && stub->timer_started && stub->last_target.minutes == 1U) {
        out->remaining_minutes = 0;
        out->restricted_now = true;
    }
    return PTC_ERR_OK;
}

static PtcErrorCode stub_backup(PtcPctl *pctl, PtcPctlBackup *out)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    char raw_hex[160];
    char slots[320];
    if (stub->backup_error != PTC_ERR_OK) {
        return stub->backup_error;
    }
    stub_raw_and_slots(stub, raw_hex, sizeof(raw_hex), slots, sizeof(slots));
    snprintf(out->text, sizeof(out->text), "limited=%d blocked=%d unrestricted=%d remaining=%u play_timer_settings_hex=%s play_timer_slots=%s",
        stub->status.limited_today,
        stub->status.blocked_today,
        stub->status.unrestricted_today,
        stub->status.remaining_minutes,
        raw_hex,
        slots);
    return PTC_ERR_OK;
}

static PtcErrorCode stub_apply_target(PtcPctl *pctl, const PtcPctlTarget *target)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    ++stub->apply_target_calls;
    if (stub->write_error != PTC_ERR_OK &&
        (stub->apply_target_fail_on_call == 0U || stub->apply_target_fail_on_call == stub->apply_target_calls)) {
        return stub->write_error;
    }
    stub->last_target = *target;
    stub->applied = true;
    if (!stub->runtime_effect_succeeds) {
        return PTC_ERR_OK;
    }
    stub->status.limited_today = target->mode == PTC_PCTL_TARGET_LIMIT && target->minutes > 0U;
    stub->status.blocked_today = target->mode == PTC_PCTL_TARGET_BLOCKED ||
        (target->mode == PTC_PCTL_TARGET_LIMIT && target->minutes == 0U);
    stub->status.unrestricted_today = target->mode == PTC_PCTL_TARGET_UNLIMITED;
    stub->status.remaining_available = target->mode != PTC_PCTL_TARGET_UNLIMITED;
    stub->status.configured_minutes_available = target->mode == PTC_PCTL_TARGET_LIMIT;
    stub->status.configured_minutes = target->minutes;
    if (stub->model_elapsed_time) {
        stub->configured_minutes = target->minutes;
        stub->status.played_minutes_available = true;
        stub->status.played_minutes = stub->played_minutes_today;
        stub->status.remaining_minutes = target->minutes > stub->played_minutes_today
            ? target->minutes - stub->played_minutes_today
            : 0U;
        stub->status.restricted_now = target->mode == PTC_PCTL_TARGET_LIMIT &&
            target->minutes <= stub->played_minutes_today;
    } else {
        stub->status.remaining_minutes = target->minutes;
        /* A blocked day allows no play time at all, so the device reports it as
           restricted immediately rather than waiting for elapsed time. */
        stub->status.restricted_now = target->mode == PTC_PCTL_TARGET_BLOCKED ||
            (target->mode == PTC_PCTL_TARGET_LIMIT && target->minutes == 0U);
    }
    return PTC_ERR_OK;
}

static PtcErrorCode stub_start_timer(PtcPctl *pctl)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    ++stub->start_timer_calls;
    if (stub->start_timer_error != PTC_ERR_OK &&
        (stub->start_timer_fail_on_call == 0U || stub->start_timer_fail_on_call == stub->start_timer_calls)) {
        return stub->start_timer_error;
    }
    stub->timer_started = true;
    stub->status.play_timer_enabled = !stub->model_elapsed_time || stub->status.remaining_minutes > 0U;
    return PTC_ERR_OK;
}

static PtcErrorCode stub_stop_timer(PtcPctl *pctl)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    stub->timer_stopped = true;
    stub->status.play_timer_enabled = false;
    return PTC_ERR_OK;
}

static PtcErrorCode stub_probe_suspend(PtcPctl *pctl, PtcProbeResult *out)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    out->verified = stub->suspend_probe_succeeds;
    snprintf(out->detail, sizeof(out->detail), "%s", out->verified ? "stub suspend ok" : "stub suspend failed");
    return out->verified ? PTC_ERR_OK : PTC_ERR_PCTL_WRITE_FAILED;
}

static PtcErrorCode stub_probe_play_timer_write(PtcPctl *pctl, PtcProbeResult *out)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    out->verified = stub->play_timer_write_probe_succeeds;
    snprintf(out->detail, sizeof(out->detail), "%s", out->verified ? "stub play timer write ok" : "stub play timer write failed");
    return out->verified ? PTC_ERR_OK : PTC_ERR_PCTL_WRITE_FAILED;
}

static void stub_encode_snapshot(const PtcPctlStub *stub, PtcPctlSettingsSnapshot *out)
{
    uint16_t words[PTC_PLAY_TIMER_SETTINGS_WORDS];
    unsigned int day;
    uint16_t minutes = stub_configured_minutes(stub);
    memset(out, 0, sizeof(*out));
    memset(words, 0, sizeof(words));
    words[0] = 0x0101U;
    words[1] = 1U;
    for (day = 0; day < PTC_PLAY_TIMER_DAY_COUNT; ++day) {
        unsigned int base = PTC_PLAY_TIMER_HEADER_WORDS + (day * PTC_PLAY_TIMER_DAY_WORDS);
        words[base + PTC_PLAY_TIMER_DAY_FLAG_WORD] = stub->status.unrestricted_today ? 0U : PTC_PLAY_TIMER_DAY_CONFIGURED;
        words[base + PTC_PLAY_TIMER_DAY_ENABLE_WORD] = stub->status.limited_today || stub->status.blocked_today ? PTC_PLAY_TIMER_DAY_RESTRICTED : 0U;
        words[base + PTC_PLAY_TIMER_DAY_MINUTES_WORD] = minutes;
    }
    memcpy(out->data, words, sizeof(words));
    out->size = PTC_PCTL_OPAQUE_SETTINGS_SIZE;
    out->timer_enabled = stub->status.play_timer_enabled;
}

static PtcErrorCode stub_snapshot_settings(PtcPctl *pctl, PtcPctlSettingsSnapshot *out)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    if (stub->snapshot_error != PTC_ERR_OK) {
        return stub->snapshot_error;
    }
    if (stub->read_error != PTC_ERR_OK) {
        return stub->read_error;
    }
    stub_encode_snapshot(stub, out);
    return PTC_ERR_OK;
}

static PtcErrorCode stub_restore_settings(PtcPctl *pctl, const PtcPctlSettingsSnapshot *snapshot)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    uint16_t words[PTC_PLAY_TIMER_SETTINGS_WORDS];
    uint16_t minutes = 0;
    uint8_t weekday = stub->applied ? stub->last_target.weekday : 0U;
    if (stub->restore_error != PTC_ERR_OK) {
        return stub->restore_error;
    }
    stub->restore_called = true;
    memcpy(words, snapshot->data, sizeof(words));
    if (!ptc_play_timer_settings_get_minutes(words, PTC_PLAY_TIMER_SETTINGS_WORDS, weekday, &minutes)) {
        return PTC_ERR_PCTL_WRITE_FAILED;
    }
    stub->status.unrestricted_today = minutes == PTC_PLAY_TIMER_UNLIMITED;
    stub->status.blocked_today = minutes == 0U;
    stub->status.limited_today = !stub->status.unrestricted_today && !stub->status.blocked_today;
    stub->status.remaining_available = !stub->status.unrestricted_today;
    stub->status.configured_minutes_available = stub->status.limited_today;
    stub->status.configured_minutes = stub->status.limited_today ? minutes : 0U;
    if (stub->model_elapsed_time) {
        stub->configured_minutes = minutes;
        stub->status.played_minutes_available = true;
        stub->status.played_minutes = stub->played_minutes_today;
        stub->status.remaining_minutes = minutes > stub->played_minutes_today
            ? minutes - stub->played_minutes_today
            : 0U;
        stub->status.restricted_now = !stub->status.unrestricted_today &&
            minutes <= stub->played_minutes_today;
    } else {
        stub->status.remaining_minutes = minutes;
        stub->status.restricted_now = false;
        if (stub->status.blocked_today) {
            stub->status.restricted_now = true;
        }
    }
    return PTC_ERR_OK;
}

static PtcErrorCode stub_debug_snapshot(PtcPctl *pctl, PtcPctlDebugSnapshot *out)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    memset(out, 0, sizeof(*out));
    if (stub->read_error != PTC_ERR_OK) {
        out->available = false;
        out->error = stub->read_error;
        out->ipc_result = (uint32_t)stub->read_error;
        return stub->read_error;
    }
    out->available = true;
    out->error = PTC_ERR_OK;
    out->ipc_result = 0;
    stub_raw_and_slots(stub, out->raw_hex, sizeof(out->raw_hex), out->decoded_slots, sizeof(out->decoded_slots));
    return PTC_ERR_OK;
}

static uint32_t stub_last_ipc_result(PtcPctl *pctl)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    if (stub->write_error != PTC_ERR_OK) {
        return (uint32_t)stub->write_error;
    }
    if (stub->read_error != PTC_ERR_OK) {
        return (uint32_t)stub->read_error;
    }
    if (stub->backup_error != PTC_ERR_OK) {
        return (uint32_t)stub->backup_error;
    }
    return 0;
}

static const PtcPctlVTable PCTL_STUB_VTABLE = {
    stub_read_status,
    stub_backup,
    stub_apply_target,
    stub_start_timer,
    stub_stop_timer,
    stub_probe_suspend,
    stub_probe_play_timer_write,
    stub_snapshot_settings,
    stub_restore_settings,
    stub_debug_snapshot,
    stub_last_ipc_result,
};

void ptc_pctl_stub_init(PtcPctlStub *stub)
{
    memset(stub, 0, sizeof(*stub));
    stub->pctl.vtable = &PCTL_STUB_VTABLE;
    stub->pctl.ctx = stub;
    stub->status.unrestricted_today = true;
    stub->read_error = PTC_ERR_OK;
    stub->backup_error = PTC_ERR_OK;
    stub->write_error = PTC_ERR_OK;
    stub->start_timer_error = PTC_ERR_OK;
    stub->restore_error = PTC_ERR_OK;
    stub->runtime_effect_succeeds = true;
}

PtcPctl *ptc_pctl_stub_as_pctl(PtcPctlStub *stub)
{
    return &stub->pctl;
}
