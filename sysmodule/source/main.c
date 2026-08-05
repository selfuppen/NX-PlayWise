#include <switch.h>

#include <stdbool.h>
#include <stdio.h>

#include "../../platform/switch/fs_storage.h"
#include "../../platform/switch/pctl_adapter.h"
#include "../../platform/switch/time_provider.h"
#include "../../common/time/ptc_time.h"
#include "../sysmodule_core.h"
#include "../ipc_server.h"

#define PTC_APP_ROOT "sdmc:/switch/playwise"
#define PTC_INNER_HEAP_SIZE 0x80000
#define PTC_STARTUP_DELAY_NS 15000000000LL

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void)
{
    static u8 inner_heap[PTC_INNER_HEAP_SIZE];
    extern void *fake_heap_start;
    extern void *fake_heap_end;

    fake_heap_start = inner_heap;
    fake_heap_end = inner_heap + sizeof(inner_heap);
}

void __appInit(void)
{
    Result rc;

    rc = smInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    }

    rc = fsInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    }
    (void)fsdevMountSdmc();

    rc = timeInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_Time));
    }
}

void __appExit(void)
{
    timeExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

static void append_boot_log(PtcSysmodule *sysmodule, const char *message)
{
    char date[11];
    char event_path[320];
    char log_path[320];
    char line[160];
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    if (!ptc_format_date_utc8(now.unix_seconds, date)) return;
    snprintf(event_path, sizeof(event_path), PTC_APP_ROOT "/logs/%s/events.jsonl", date);
    snprintf(log_path, sizeof(log_path), PTC_APP_ROOT "/logs/%s/sysmodule.log", date);
    snprintf(line, sizeof(line), "{\"ts\":%lld,\"event\":\"boot\",\"message\":\"%s\"}", (long long)now.unix_seconds, message);
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, event_path, line);
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, log_path, message);
}

int main(int argc, char **argv)
{
    /* Process-lifetime singletons kept in .bss, not on the stack: PtcIpcServer is
       ~68 KiB (result_cache[8] of 8 KiB each) and PtcSysmodule ~10 KiB, which left
       too little of the main thread stack for the retention and request paths. */
    static PtcFsStorage fs;
    static PtcSwitchPctl pctl;
    static PtcSwitchTimeProvider time_provider;
    static PtcSysmodule sysmodule;
    static PtcIpcServer ipc_server;
    PtcStorage *storage;
    bool ipc_available;
    int recovered;
    (void)argc;
    (void)argv;

    svcSleepThread(PTC_STARTUP_DELAY_NS);

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

    (void)ptc_sysmodule_rollover_legacy_logs(&sysmodule);
    append_boot_log(&sysmodule, "playwise sysmodule started");
    (void)ptc_sysmodule_bootstrap_setup(&sysmodule);
    recovered = ptc_sysmodule_recover_processing(&sysmodule);
    if (recovered > 0) {
        append_boot_log(&sysmodule, "recovered processing requests");
    }
    (void)ptc_sysmodule_cleanup(&sysmodule);
    (void)ptc_sysmodule_scheduler_tick(&sysmodule, false);
    ipc_available = ptc_ipc_server_start(&ipc_server, &sysmodule);
    if (!ipc_available) append_boot_log(&sysmodule, "pctc:u unavailable; using file transport only");

    while (true) {
        bool notified;
        uint32_t wait_ms;
        wait_ms = ptc_sysmodule_next_wait_ms(&sysmodule);
        notified = ipc_available ? ptc_ipc_server_wait(&ipc_server, wait_ms) : false;
        if (!ipc_available) svcSleepThread((int64_t)wait_ms * 1000000LL);
        if (ipc_available) ptc_ipc_server_lock_storage(&ipc_server);
        (void)ptc_sysmodule_scheduler_tick(&sysmodule, notified);
        if (ipc_available) ptc_ipc_server_unlock_storage(&ipc_server);
        if (ipc_available) ptc_ipc_server_signal_completed(&ipc_server);
    }

    ptc_switch_pctl_exit(&pctl);
    return 0;
}
