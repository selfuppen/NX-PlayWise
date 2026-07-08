#ifndef PTC_HOST_PCTL_STUB_H
#define PTC_HOST_PCTL_STUB_H

#include "../pctl.h"

typedef struct {
    PtcPctl pctl;
    PtcPctlStatus status;
    PtcPctlTarget last_target;
    bool applied;
    bool timer_started;
    bool timer_stopped;
    bool raw_probe_succeeds;
    bool suspend_probe_succeeds;
    PtcErrorCode read_error;
    PtcErrorCode backup_error;
    PtcErrorCode write_error;
} PtcPctlStub;

void ptc_pctl_stub_init(PtcPctlStub *stub);
PtcPctl *ptc_pctl_stub_as_pctl(PtcPctlStub *stub);

#endif
