#include <switch.h>

#include <stdbool.h>
#include <stdio.h>

#include "../../platform/switch/fs_storage.h"
#include "../../platform/switch/pctl_adapter.h"
#include "../../platform/switch/time_provider.h"
#include "../sysmodule_core.h"

#define PTC_APP_ROOT "sdmc:/switch/play-time-control"
#define PTC_LOOP_SLEEP_NS 500000000LL
#define PTC_STARTUP_DELAY_NS 15000000000LL

static void append_boot_log(PtcStorage *storage, const char *message)
{
    char line[160];
    snprintf(line, sizeof(line), "{\"ts\":0,\"event\":\"boot\",\"message\":\"%s\"}", message);
    (void)storage->vtable->append_line(storage, PTC_APP_ROOT "/logs/events.jsonl", line);
    (void)storage->vtable->append_line(storage, PTC_APP_ROOT "/logs/sysmodule.log", message);
}

int main(int argc, char **argv)
{
    PtcFsStorage fs;
    PtcSwitchPctl pctl;
    PtcSwitchTimeProvider time_provider;
    PtcSysmodule sysmodule;
    PtcStorage *storage;
    int recovered;
    (void)argc;
    (void)argv;

    svcSleepThread(PTC_STARTUP_DELAY_NS);
    socketInitializeDefault();
    fsdevMountSdmc();

    ptc_fs_storage_init(&fs);
    ptc_switch_pctl_init(&pctl);
    ptc_switch_time_provider_init(&time_provider);
    storage = ptc_fs_storage_as_storage(&fs);
    ptc_sysmodule_init(
        &sysmodule,
        PTC_APP_ROOT,
        storage,
        ptc_switch_pctl_as_pctl(&pctl),
        ptc_switch_time_provider_as_provider(&time_provider));

    append_boot_log(storage, "play-time-control sysmodule started");
    recovered = ptc_sysmodule_recover_processing(&sysmodule);
    if (recovered > 0) {
        append_boot_log(storage, "recovered processing requests");
    }

    while (true) {
        (void)ptc_sysmodule_process_all(&sysmodule);
        svcSleepThread(PTC_LOOP_SLEEP_NS);
    }

    ptc_switch_pctl_exit(&pctl);
    fsdevUnmountDevice("sdmc");
    socketExit();
    return 0;
}
