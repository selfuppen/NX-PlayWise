#include "pctl_stub.h"

#include <stdio.h>
#include <string.h>

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
    if (stub->backup_error != PTC_ERR_OK) {
        return stub->backup_error;
    }
    snprintf(out->text, sizeof(out->text), "limited=%d blocked=%d unrestricted=%d remaining=%u",
        stub->status.limited_today,
        stub->status.blocked_today,
        stub->status.unrestricted_today,
        stub->status.remaining_minutes);
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

static const PtcPctlVTable PCTL_STUB_VTABLE = {
    stub_read_status,
    stub_backup,
    stub_apply_target,
    stub_start_timer,
    stub_stop_timer,
    stub_probe_raw_block,
    stub_probe_suspend,
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
