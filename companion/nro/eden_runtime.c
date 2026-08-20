#include "eden_runtime.h"

#ifdef PLAYWISE_EDEN

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <switch.h>

#include "../../common/time/ptc_time.h"
#include "../../common/version.h"
#include "release_manifest.h"

/* The emulator has no reliable time service wrapper for the sysmodule path, but
   newlib's time(NULL) already works in the NRO, so reuse it instead of
   depending on timeInitialize(). */
static PtcClockSnapshot eden_now(PtcTimeProvider *provider)
{
    PtcClockSnapshot snapshot;
    int64_t now = (int64_t)time(NULL);
    (void)provider;
    snapshot.unix_seconds = now;
    snapshot.day_index = ptc_day_index_from_unix_utc8(now);
    snapshot.minute_of_day = ptc_minute_of_day_from_unix_utc8(now);
    return snapshot;
}

static void eden_sleep_ms(PtcTimeProvider *provider, uint32_t milliseconds)
{
    (void)provider;
    svcSleepThread((int64_t)milliseconds * 1000000LL);
}

static const PtcTimeProviderVTable EDEN_TIME_VTABLE = {
    eden_now,
    eden_sleep_ms,
};

static bool ensure_directory(const char *path)
{
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

/* Existing files are test data the operator may have edited, so never clobber
   them. This mirrors ptc_install_materialize_defaults' idempotence rule. */
static bool write_missing(PtcStorage *storage, const char *relative, const char *text)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", PLAYWISE_EDEN_SD_ROOT, relative);
    return storage->vtable->exists(storage, path) ||
        storage->vtable->write_text_atomic(storage, path, text);
}

static bool seed_files(PtcStorage *storage)
{
    static const char *DIRECTORIES[] = {
        PLAYWISE_EDEN_SD_ROOT,
        PLAYWISE_EDEN_SD_ROOT "/inbox",
        PLAYWISE_EDEN_SD_ROOT "/inbox/pending",
        PLAYWISE_EDEN_SD_ROOT "/inbox/processing",
        PLAYWISE_EDEN_SD_ROOT "/inbox/done",
        PLAYWISE_EDEN_SD_ROOT "/results",
        PLAYWISE_EDEN_SD_ROOT "/logs",
        PLAYWISE_EDEN_SD_ROOT "/logs/undated",
        PLAYWISE_EDEN_SD_ROOT "/ledger",
        PLAYWISE_EDEN_SD_ROOT "/backups",
        PLAYWISE_EDEN_SD_ROOT "/recovery",
        PLAYWISE_EDEN_SD_ROOT "/recovery/active",
        PLAYWISE_EDEN_SD_ROOT "/flags",
        PLAYWISE_EDEN_SD_ROOT "/support",
    };
    static const char CONFIG[] =
        "{\"version\":1,\"device_id\":\"" PTC_EDEN_TEST_DEVICE_ID "\",\"max_add_minutes\":240,"
        "\"default_request_timeout_ms\":60000,"
        "\"pairing_base_url\":\"https://selfuppen.github.io/NX-PlayWise/\"}\n";
    static const char AUTH[] =
        "{\"version\":1,\"pin_hash\":\"\",\"pin_salt\":\"\",\"hash\":\"hmac-sha256\","
        "\"updated_at\":0,\"failed_attempts\":0,\"cooldown_until\":0}\n";
    static const char RULES[] =
        "{\"version\":1,\"week\":[{\"mode\":\"unlimited\",\"minutes\":120},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"unlimited\",\"minutes\":120}],"
        "\"today_override_present\":false,\"today_override_day_index\":0,"
        "\"today_override_mode\":\"limit\",\"today_override_minutes\":60}\n";
    static const char STATE[] =
        "{\"version\":1,\"last_enforced_day_index\":0,\"last_enforced_mode\":0,"
        "\"last_enforced_minutes\":0,\"apply_status\":\"idle\",\"updated_at\":0}\n";
    static const char COMPATIBILITY[] =
        "{\"version\":1,\"status\":\"pending\",\"accepted_fingerprint\":null,\"accepted_at\":0}\n";
    static const char SETUP[] =
        "{\"version\":1,\"phase\":\"unconfigured\",\"compatibility_status\":\"pending\","
        "\"restriction_cleared\":false,\"snapshot_available\":false,\"activate_after\":0,"
        "\"last_error\":\"\"}\n";
    static const char CREDENTIALS[] =
        "{\"version\":1,\"grant_secret\":\"" PTC_EDEN_TEST_GRANT_SECRET "\"}\n";
    /* read_ok stays false so the preflight records accepted_unknown instead of
       claiming a verified hardware environment. */
    static const char ENVIRONMENT[] =
        "{\"version\":1,\"read_ok\":false,\"hos\":\"eden\",\"firmware_hash\":\"eden-test\","
        "\"model\":\"eden-emulator\",\"atmosphere\":false}\n";
    size_t index;

    for (index = 0; index < sizeof(DIRECTORIES) / sizeof(DIRECTORIES[0]); ++index) {
        if (!ensure_directory(DIRECTORIES[index])) return false;
    }
    return write_missing(storage, "config.json", CONFIG) &&
        write_missing(storage, "auth.json", AUTH) &&
        write_missing(storage, "rules.json", RULES) &&
        write_missing(storage, "state.json", STATE) &&
        write_missing(storage, "compatibility.json", COMPATIBILITY) &&
        write_missing(storage, "setup.json", SETUP) &&
        write_missing(storage, "credentials.json", CREDENTIALS) &&
        write_missing(storage, "environment.json", ENVIRONMENT) &&
        write_missing(storage, "build.json", PLAYWISE_RELEASE_MANIFEST_JSON);
}

bool ptc_eden_runtime_init(PtcEdenRuntime *runtime, PtcStorage *storage)
{
    char boot_id[24];
    if (!runtime || !storage || !seed_files(storage)) return false;
    memset(runtime, 0, sizeof(*runtime));
    ptc_pctl_stub_init(&runtime->pctl);
    /* Model elapsed play time so 今日已玩 / 还可玩 are not trivially zero and the
       expiry path can be exercised without waiting a real day. */
    runtime->pctl.model_elapsed_time = true;
    runtime->pctl.played_minutes_today = 30;
    runtime->pctl.status.played_minutes_available = true;
    runtime->pctl.status.played_minutes = 30;
    runtime->pctl.status.play_timer_enabled = true;
    runtime->time_provider.vtable = &EDEN_TIME_VTABLE;
    runtime->time_provider.ctx = runtime;
    ptc_sysmodule_init(&runtime->sysmodule, PLAYWISE_EDEN_SD_ROOT, storage,
        ptc_pctl_stub_as_pctl(&runtime->pctl), &runtime->time_provider);
    snprintf(boot_id, sizeof(boot_id), "eden-%08llx",
        (unsigned long long)(randomGet64() & 0xffffffffULL));
    ptc_sysmodule_set_boot_id(&runtime->sysmodule, boot_id);
    (void)ptc_sysmodule_recover_processing(&runtime->sysmodule);
    (void)ptc_sysmodule_bootstrap_setup(&runtime->sysmodule);
    (void)ptc_sysmodule_scheduler_tick(&runtime->sysmodule, false);
    runtime->initialized = true;
    return true;
}

void ptc_eden_runtime_tick(PtcEdenRuntime *runtime)
{
    if (runtime && runtime->initialized) {
        (void)ptc_sysmodule_scheduler_tick(&runtime->sysmodule, false);
    }
}

#endif
