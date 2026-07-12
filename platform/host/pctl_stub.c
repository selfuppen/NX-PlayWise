#include "pctl_stub.h"

#include <stdio.h>
#include <string.h>

static uint16_t stub_minutes_for_status(const PtcPctlStatus *status)
{
    if (status->unrestricted_today) {
        return 0xffffu;
    }
    if (status->blocked_today) {
        return 0u;
    }
    return status->remaining_minutes;
}

static void stub_raw_and_slots(const PtcPctlStatus *status, char *raw_hex, size_t raw_size, char *slots, size_t slots_size)
{
    uint16_t minutes = stub_minutes_for_status(status);
    unsigned int day;
    size_t raw_used = 0;
    size_t slots_used = 0;
    raw_hex[0] = '\0';
    slots[0] = '\0';
    for (day = 0; day < 7U; ++day) {
        unsigned int enabled = status->unrestricted_today ? 0U : 1U;
        unsigned int limited = status->limited_today || status->blocked_today ? 1U : 0U;
        int raw_written = snprintf(raw_hex + raw_used, raw_size - raw_used, "%04x%04x%04x%04x", enabled, limited, minutes, 0U);
        int slot_written = snprintf(
            slots + slots_used,
            slots_size - slots_used,
            "%sd%u:e%u,l%u,m%u,x0",
            day == 0U ? "" : ";",
            day,
            enabled,
            limited,
            (unsigned int)minutes);
        if (raw_written < 0 || slot_written < 0 ||
            (size_t)raw_written >= raw_size - raw_used ||
            (size_t)slot_written >= slots_size - slots_used) {
            break;
        }
        raw_used += (size_t)raw_written;
        slots_used += (size_t)slot_written;
    }
}

static PtcErrorCode stub_read_status(PtcPctl *pctl, PtcPctlStatus *out)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    if (stub->read_error != PTC_ERR_OK) {
        return stub->read_error;
    }
    *out = stub->status;
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
    stub_raw_and_slots(&stub->status, raw_hex, sizeof(raw_hex), slots, sizeof(slots));
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
    if (stub->write_error != PTC_ERR_OK) {
        return stub->write_error;
    }
    stub->last_target = *target;
    stub->applied = true;
    stub->status.limited_today = target->mode == PTC_PCTL_TARGET_LIMIT;
    stub->status.blocked_today = target->mode == PTC_PCTL_TARGET_BLOCKED;
    stub->status.unrestricted_today = target->mode == PTC_PCTL_TARGET_UNLIMITED;
    stub->status.remaining_available = target->mode == PTC_PCTL_TARGET_LIMIT;
    stub->status.remaining_minutes = target->minutes;
    return PTC_ERR_OK;
}

static PtcErrorCode stub_start_timer(PtcPctl *pctl)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    stub->timer_started = true;
    stub->status.play_timer_enabled = true;
    return PTC_ERR_OK;
}

static PtcErrorCode stub_stop_timer(PtcPctl *pctl)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    stub->timer_stopped = true;
    stub->status.play_timer_enabled = false;
    return PTC_ERR_OK;
}

static PtcErrorCode stub_probe_raw_block(PtcPctl *pctl, PtcProbeResult *out)
{
    PtcPctlStub *stub = (PtcPctlStub *)pctl->ctx;
    out->verified = stub->raw_probe_succeeds;
    snprintf(out->detail, sizeof(out->detail), "%s", out->verified ? "stub raw block ok" : "stub raw block failed");
    return out->verified ? PTC_ERR_OK : PTC_ERR_PCTL_WRITE_FAILED;
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
    stub_raw_and_slots(&stub->status, out->raw_hex, sizeof(out->raw_hex), out->decoded_slots, sizeof(out->decoded_slots));
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
    stub_probe_raw_block,
    stub_probe_suspend,
    stub_probe_play_timer_write,
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
}

PtcPctl *ptc_pctl_stub_as_pctl(PtcPctlStub *stub)
{
    return &stub->pctl;
}
