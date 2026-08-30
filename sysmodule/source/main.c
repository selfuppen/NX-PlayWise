#include <switch.h>
#include <string.h>

#include <stdbool.h>
#include <stdio.h>

#include "../../platform/switch/fs_storage.h"
#include "../../platform/switch/pctl_adapter.h"
#include "../../platform/switch/time_provider.h"
#include "../../platform/switch/usage_stats_adapter.h"
#include "../../platform/install_defaults.h"
#include "../../common/time/ptc_time.h"
#include "../../common/protocol/atmosphere_version.h"
#include "../../common/version.h"
#include "../sysmodule_core.h"
#include "../ipc_server.h"
#include "release_manifest.h"

#define PTC_APP_ROOT PLAYWISE_SD_ROOT
#define PTC_INNER_HEAP_SIZE 0x80000
#define PTC_STARTUP_DELAY_NS 15000000000LL

__attribute__((used)) static const char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

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
    if (now.unix_seconds >= PTC_DAY_INDEX_EPOCH_UNIX && ptc_format_date(now.day_index, date)) {
        snprintf(event_path, sizeof(event_path), PTC_APP_ROOT "/logs/%s/events.jsonl", date);
        snprintf(log_path, sizeof(log_path), PTC_APP_ROOT "/logs/%s/sysmodule.log", date);
    } else {
        snprintf(event_path, sizeof(event_path), PTC_APP_ROOT "/logs/undated/%s/events.jsonl", sysmodule->boot_id);
        snprintf(log_path, sizeof(log_path), PTC_APP_ROOT "/logs/undated/%s/sysmodule.log", sysmodule->boot_id);
    }
    snprintf(line, sizeof(line), "{\"ts\":%lld,\"event\":\"boot\",\"message\":\"%s\"}", (long long)now.unix_seconds, message);
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, event_path, line);
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, log_path, message);
}

static void ensure_credentials(PtcStorage *storage)
{
    static const char HEX[] = "0123456789abcdef";
    uint8_t secret[32];
    char secret_hex[65];
    char json[128];
    size_t i;
    if (storage->vtable->exists(storage, PTC_APP_ROOT "/credentials.json")) return;
    randomGet(secret, sizeof(secret));
    for (i = 0; i < sizeof(secret); ++i) {
        secret_hex[i * 2] = HEX[secret[i] >> 4];
        secret_hex[i * 2 + 1] = HEX[secret[i] & 0x0f];
    }
    secret_hex[64] = '\0';
    snprintf(json, sizeof(json), "{\"version\":1,\"grant_secret\":\"%s\"}\n", secret_hex);
    (void)storage->vtable->write_text_atomic(storage, PTC_APP_ROOT "/credentials.json", json);
}

static const char *product_model_name(SetSysProductModel model)
{
    switch (model) {
    case SetSysProductModel_Nx: return "erista";
    case SetSysProductModel_Copper: return "erista-simulation";
    case SetSysProductModel_Iowa: return "mariko";
    case SetSysProductModel_Hoag: return "mariko-lite";
    case SetSysProductModel_Calcio: return "mariko-simulation";
    case SetSysProductModel_Aula: return "mariko-oled";
    default: return "unknown";
    }
}

static void write_environment_fingerprint(PtcStorage *storage)
{
    SetSysFirmwareVersion firmware;
    SetSysFirmwareVersionDigest digest;
    SetSysProductModel model = SetSysProductModel_Invalid;
    PtcAtmosphereVersion atmosphere_version;
    uint64_t atmosphere_raw = 0;
    Result atmosphere_result = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    char json[896];
    char atmosphere_version_json[32];
    bool read_ok = false;
    bool firmware_read_ok = false;
    bool atmosphere = false;
    memset(&firmware, 0, sizeof(firmware));
    memset(&digest, 0, sizeof(digest));
    if (R_SUCCEEDED(setsysInitialize())) {
        firmware_read_ok = R_SUCCEEDED(setsysGetFirmwareVersion(&firmware));
        read_ok = firmware_read_ok && R_SUCCEEDED(setsysGetFirmwareVersionDigest(&digest)) &&
            R_SUCCEEDED(setsysGetProductModel(&model));
        setsysExit();
    }
    /* Sysmodules provide their own __appInit, so libnx's normal application
       startup may not populate hosversion. Public wrappers such as command
       1458 gate on this value even though the raw service is callable. */
    if (firmware_read_ok && hosversionGet() == 0)
        hosversionSet(MAKEHOSVERSION(firmware.major, firmware.minor, firmware.micro));
    memset(&atmosphere_version, 0, sizeof(atmosphere_version));
    if (R_SUCCEEDED(splInitialize())) {
        atmosphere_result = splGetConfig((SplConfigItem)65000, &atmosphere_raw);
        splExit();
        atmosphere = R_SUCCEEDED(atmosphere_result) &&
            ptc_atmosphere_version_decode(atmosphere_raw, &atmosphere_version);
    }
    if (atmosphere) {
        snprintf(atmosphere_version_json, sizeof(atmosphere_version_json), "\"%u.%u.%u\"",
            (unsigned int)atmosphere_version.major,
            (unsigned int)atmosphere_version.minor,
            (unsigned int)atmosphere_version.micro);
    } else {
        snprintf(atmosphere_version_json, sizeof(atmosphere_version_json), "null");
    }
    snprintf(
        json,
        sizeof(json),
        "{\"version\":1,\"read_ok\":%s,\"hos\":\"%u.%u.%u\",\"firmware_hash\":\"%.64s\","
        "\"firmware_digest\":\"%.64s\",\"model\":\"%s\",\"atmosphere\":%s,"
        "\"atmosphere_version\":%s,\"atmosphere_detection_source\":\"spl:ExosphereApiVersion\","
        "\"atmosphere_detection\":{\"source\":\"spl:ExosphereApiVersion\","
        "\"result\":%u,\"raw\":\"0x%016llx\",\"version\":%s}}\n",
        read_ok ? "true" : "false",
        (unsigned int)firmware.major,
        (unsigned int)firmware.minor,
        (unsigned int)firmware.micro,
        firmware.version_hash,
        digest.digest,
        product_model_name(model),
        atmosphere ? "true" : "false",
        atmosphere_version_json,
        (unsigned int)atmosphere_result,
        (unsigned long long)atmosphere_raw,
        atmosphere_version_json);
    (void)storage->vtable->write_text_atomic(storage, PTC_APP_ROOT "/environment.json", json);
}

int main(int argc, char **argv)
{
    /* Process-lifetime singletons kept in .bss, not on the stack: PtcIpcServer is
       ~68 KiB (result_cache[8] of 8 KiB each) and PtcSysmodule ~10 KiB, which left
       too little of the main thread stack for the retention and request paths. */
    static PtcFsStorage fs;
    static PtcSwitchPctl pctl;
    static PtcSwitchTimeProvider time_provider;
    static PtcSwitchUsageStats usage_stats;
    static PtcSysmodule sysmodule;
    static PtcIpcServer ipc_server;
    PtcStorage *storage;
    bool ipc_available;
    int recovered;
    char boot_id[24];
    (void)argc;
    (void)argv;

    svcSleepThread(PTC_STARTUP_DELAY_NS);

    ptc_fs_storage_init(&fs);
    ptc_switch_pctl_init(&pctl);
    ptc_switch_time_provider_init(&time_provider);
    ptc_switch_usage_stats_init(&usage_stats);
    storage = ptc_fs_storage_as_storage(&fs);
#ifndef PLAYWISE_DEVICE_LAB
    /* Package defaults live outside mutable paths so an archive overlay cannot
       replace user data. Do not start the control loop until missing live data
       has been materialized successfully. */
    while (!ptc_install_materialize_defaults(storage, PTC_APP_ROOT)) {
        svcSleepThread(1000000000LL);
    }
#endif
    ensure_credentials(storage);
    write_environment_fingerprint(storage);
    ptc_sysmodule_init(
        &sysmodule,
        PTC_APP_ROOT,
        storage,
        ptc_switch_pctl_as_pctl(&pctl),
        ptc_switch_time_provider_as_provider(&time_provider));
    snprintf(boot_id, sizeof(boot_id), "%016llx", (unsigned long long)randomGet64());
    ptc_sysmodule_set_boot_id(&sysmodule, boot_id);

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
    if (!ipc_available) append_boot_log(&sysmodule, PLAYWISE_IPC_SERVICE " unavailable; using file transport only");

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
