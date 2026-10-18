#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../companion/nro/hot_reload.h"
#include "../../common/crypto/sha256.h"
#include "release_manifest.h"

#define ROOT "sdmc:/switch/playwise"
#define FLAG "sdmc:/atmosphere/contents/4200000000BD2300/flags/boot2.flag"
#define REQUEST ROOT "/support/backend-restart.json"
#define CLAIM ROOT "/support/backend-restart-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.claimed.json"
#define CONFIG ROOT "/support/usage-evidence.json"
static const char *source_boot = "dddddddddddddddd";
static u64 ticks, running, random_value;
static unsigned launches, pm_sessions;
static bool wrong_boot;
static unsigned event_open_failures, event_open_attempts, event_write_calls, event_close_calls;
static int event_open_errno;
static uint32_t event_fs_result;
static bool event_write_failure, event_close_failure;
static FILE *event_stream;

FILE *ptc_test_fopen(const char *path, const char *mode)
{
    if (strcmp(mode, "ab") == 0 && strstr(path, "/handover/events/")) {
        ++event_open_attempts;
        if (event_open_failures) {
            --event_open_failures;
            errno = event_open_errno;
            return NULL;
        }
        event_stream = fopen(path, mode);
        return event_stream;
    }
    return fopen(path, mode);
}

int ptc_test_fprintf(FILE *file, const char *format, ...)
{
    va_list args;
    int result;
    if (file == event_stream) {
        ++event_write_calls;
        if (event_write_failure) {
            assert(fputs("{", file) >= 0);
            errno = EIO;
            return -1;
        }
    }
    va_start(args, format);
    result = vfprintf(file, format, args);
    va_end(args);
    return result;
}

int ptc_test_fclose(FILE *file)
{
    bool event = file == event_stream;
    int result = fclose(file);
    if (event) {
        event_stream = NULL;
        ++event_close_calls;
        if (event_close_failure) {
            errno = EIO;
            return EOF;
        }
    }
    return result;
}

uint32_t fsdevGetLastResult(void) { return event_fs_result; }

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fputs(text, f) >= 0); assert(fclose(f) == 0);
}

static bool exists(const char *path)
{
    struct stat s; return stat(path, &s) == 0;
}

static void sha(const char *text, char out[65])
{
    PtcSha256Ctx ctx; uint8_t digest[32];
    ptc_sha256_init(&ctx); ptc_sha256_update(&ctx, (const uint8_t *)text, strlen(text));
    ptc_sha256_final(&ctx, digest);
    for (size_t i = 0; i < sizeof(digest); ++i) snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

static void runtime(u64 pid, const char *boot)
{
    char text[512];
    snprintf(text, sizeof(text), "{\"profile\":\"release\",\"pid\":%llu,\"boot_id\":\"%s\","
        "\"release_id\":\"%s\",\"ipc_version\":2,\"ipc_available\":true}",
        (unsigned long long)pid, boot, PLAYWISE_BUILD_RELEASE_ID);
    put(ROOT "/handover/runtime-ready.json", text);
}

Result pmshellInitialize(void) { ++pm_sessions; return 0; }
void pmshellExit(void) { assert(pm_sessions); --pm_sessions; }
Result pmshellGetProcessId(u64 *pid, u64 program)
{
    assert(program == UINT64_C(0x4200000000BD2300));
    if (!running) return 15U | (1U << 9);
    *pid = running; return 0;
}
Result pmshellLaunchProgram(unsigned int flags, const NcmProgramLocation *location, u64 *pid)
{
    assert(!running && exists(FLAG));
    assert(flags == PmLaunchFlag_None && location->storageID == NcmStorageId_None);
    running = *pid = 142; ++launches;
    runtime(running, wrong_boot ? source_boot : "eeeeeeeeeeeeeeee");
    return 0;
}
Result fsdevCommitDevice(const char *device) { assert(strcmp(device, "sdmc") == 0); return 0; }
void svcSleepThread(int64_t ns) { ticks += (u64)ns; }
u64 armGetSystemTick(void) { return ticks; }
u64 armTicksToNs(u64 value) { return value; }
u64 randomGet64(void) { return ++random_value; }

static void fixture(unsigned index)
{
    char directory[96], config[512], request[768], artifacts[1024], hash[65];
    snprintf(directory, sizeof(directory), "build/test-capture-restart-%u", index);
    (void)mkdir(directory, 0777); assert(chdir(directory) == 0);
    const char *dirs[] = {"sdmc:", "sdmc:/switch", ROOT, ROOT "/support", ROOT "/handover",
        ROOT "/handover/events", "sdmc:/switch/.overlays", "sdmc:/atmosphere", "sdmc:/atmosphere/contents",
        "sdmc:/atmosphere/contents/4200000000BD2300", "sdmc:/atmosphere/contents/4200000000BD2300/flags"};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) (void)mkdir(dirs[i], 0777);
    const char *owned[] = {FLAG ".playwise-hot-reload-backup", CLAIM, REQUEST,
        ROOT "/handover/reload.json", ROOT "/handover/reload.json.tmp", ROOT "/handover/intent.json",
        ROOT "/handover/ready.json", ROOT "/handover/events/00000000000000010000000000000002.jsonl"};
    for (size_t i = 0; i < sizeof(owned) / sizeof(owned[0]); ++i) (void)remove(owned[i]);
    ticks = 1; running = 131; random_value = 0; launches = pm_sessions = 0; wrong_boot = false;
    event_open_failures = event_open_attempts = event_write_calls = event_close_calls = 0;
    event_open_errno = 0; event_fs_result = 0; event_write_failure = event_close_failure = false;
    event_stream = NULL;
    put(FLAG, ""); runtime(running, source_boot);
    put(ROOT "/pctc.nro", "component"); put("sdmc:/switch/.overlays/playwise.ovl", "component");
    put("sdmc:/atmosphere/contents/4200000000BD2300/exefs.nsp", "component");
    sha("component", hash);
    snprintf(artifacts, sizeof(artifacts), "{\"schema_version\":1,\"release_id\":\"%s\",\"artifacts\":{"
        "\"switch/playwise/pctc.nro\":{\"size\":9,\"sha256\":\"%s\"},"
        "\"switch/.overlays/playwise.ovl\":{\"size\":9,\"sha256\":\"%s\"},"
        "\"atmosphere/contents/4200000000BD2300/exefs.nsp\":{\"size\":9,\"sha256\":\"%s\"}}}",
        PLAYWISE_BUILD_RELEASE_ID, hash, hash, hash);
    put(ROOT "/package-artifacts.json", artifacts);
    snprintf(config, sizeof(config), "{\"schema_version\":1,\"run_id\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\","
        "\"commit\":\"%s\",\"release_id\":\"%s\",\"duration_s\":900}", PLAYWISE_BUILD_COMMIT, PLAYWISE_BUILD_RELEASE_ID);
    put(CONFIG, config); sha(config, hash);
    snprintf(request, sizeof(request), "{\"schema_version\":1,\"purpose\":\"usage_evidence\","
        "\"request_id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"source_boot_id\":\"%s\","
        "\"release_id\":\"%s\",\"run_id\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"config_sha256\":\"%s\"}",
        source_boot, PLAYWISE_BUILD_RELEASE_ID, hash);
    put(REQUEST, request);
}

static void begin(PtcHotReloadController *c)
{
    ptc_hot_reload_init(c); ptc_hot_reload_inspect(c);
    assert(c->status == PTC_HOT_RELOAD_PENDING && ptc_hot_reload_begin(c));
    assert(!exists(REQUEST) && exists(CLAIM) && exists(FLAG));
    char ack[512];
    snprintf(ack, sizeof(ack), "{\"handover_version\":1,\"transaction_id\":\"%s\",\"status\":\"ready\","
        "\"source_pid\":131,\"profile\":\"release\",\"release_id\":\"%s\",\"boot_id\":\"%s\"}",
        c->journal.transaction_id, PLAYWISE_BUILD_RELEASE_ID, source_boot);
    put(ROOT "/handover/ready.json", ack); ptc_hot_reload_tick(c);
    assert(c->phase == PTC_HOT_RELOAD_PHASE_NEED_IPC_CLOSE);
}

static void ready(PtcHotReloadController *c)
{
    for (unsigned i = 0; i < 3; ++i) ptc_hot_reload_tick(c);
    assert(c->phase == PTC_HOT_RELOAD_PHASE_WAIT_TARGET_READY && launches == 1 && exists(FLAG));
    ptc_hot_reload_tick(c);
}

static void event_fixture(unsigned index, PtcHotReloadController *c)
{
    fixture(index);
    ptc_hot_reload_init(c);
    snprintf(c->journal.transaction_id, sizeof(c->journal.transaction_id), "00000000000000010000000000000002");
}

static unsigned event_lines(void)
{
    FILE *file = fopen(ROOT "/handover/events/00000000000000010000000000000002.jsonl", "rb");
    unsigned lines = 0;
    if (!file) return 0;
    for (int ch; (ch = fgetc(file)) != EOF;) if (ch == '\n') ++lines;
    assert(fclose(file) == 0);
    return lines;
}

static void test_event_append(void)
{
    PtcHotReloadController c;
    event_fixture(10, &c);
    event_open_failures = 2; event_open_errno = EIO; event_fs_result = 0xE02;
    ptc_hot_reload_record_event(&c, "runtime_ready", NULL, 0);
    assert(event_open_attempts == 3 && event_write_calls == 1 && event_close_calls == 1);
    assert(ticks == UINT64_C(20000001) && event_lines() == 1);
    ptc_hot_reload_record_event(&c, "status_submitted", "ui-status", 0);
    assert(event_lines() == 2 && event_write_calls == 2);
    assert(chdir("../..") == 0);

    event_fixture(11, &c);
    event_open_failures = 100; event_open_errno = EIO; event_fs_result = 0xE02;
    ptc_hot_reload_record_event(&c, "runtime_ready", NULL, 0);
    assert(event_open_attempts == 51 && !event_write_calls && !event_close_calls && !event_lines());
    assert(ticks == UINT64_C(500000001)); assert(chdir("../..") == 0);

    event_fixture(12, &c);
    event_open_failures = 2; event_open_errno = EIO; event_fs_result = 0x1234;
    ptc_hot_reload_record_event(&c, "runtime_ready", NULL, 0);
    assert(event_open_attempts == 1 && !event_write_calls && !event_lines() && ticks == 1);
    assert(chdir("../..") == 0);

    event_fixture(13, &c);
    event_write_failure = true;
    ptc_hot_reload_record_event(&c, "runtime_ready", NULL, 0);
    assert(event_open_attempts == 1 && event_write_calls == 1 && event_close_calls == 1 && !event_lines());
    assert(chdir("../..") == 0);

    event_fixture(14, &c);
    event_close_failure = true;
    ptc_hot_reload_record_event(&c, "runtime_ready", NULL, 0);
    assert(event_open_attempts == 1 && event_write_calls == 1 && event_close_calls == 1 && event_lines() == 1);
    assert(chdir("../..") == 0);
}

int main(void)
{
    PtcHotReloadController c, recovered;
    fixture(1); begin(&c); ptc_hot_reload_confirm_ipc_closed(&c);
    assert(!exists(FLAG) && exists(FLAG ".playwise-hot-reload-backup"));
    running = 0; ready(&c);
    assert(c.status == PTC_HOT_RELOAD_SUCCESS && !pm_sessions && !exists(ROOT "/handover/reload.json"));
    assert(strcmp(c.target_boot_id, source_boot) != 0);
    ptc_hot_reload_init(&recovered); ptc_hot_reload_inspect(&recovered);
    assert(recovered.status == PTC_HOT_RELOAD_CURRENT); assert(chdir("../..") == 0);

    fixture(2); ptc_hot_reload_init(&c); ptc_hot_reload_inspect(&c); put(CONFIG, "changed");
    assert(!ptc_hot_reload_begin(&c) && exists(FLAG) && exists(REQUEST) && !exists(CLAIM));
    assert(!launches && !pm_sessions); assert(chdir("../..") == 0);

    fixture(3); begin(&c); put(CONFIG, "changed"); ptc_hot_reload_confirm_ipc_closed(&c);
    assert(c.phase == PTC_HOT_RELOAD_PHASE_FAILED && exists(FLAG) && !launches && !pm_sessions);
    assert(!exists(ROOT "/handover/intent.json") && exists(CLAIM)); assert(chdir("../..") == 0);

    fixture(4); begin(&c); ptc_hot_reload_confirm_ipc_closed(&c);
    ticks += UINT64_C(10000000000); ptc_hot_reload_tick(&c);
    assert(c.phase == PTC_HOT_RELOAD_PHASE_FAILED && exists(FLAG) && !launches && !pm_sessions);
    ptc_hot_reload_init(&recovered); ptc_hot_reload_recover_startup(&recovered);
    assert(!exists(ROOT "/handover/reload.json") && !pm_sessions && running == 131);
    assert(chdir("../..") == 0);

    fixture(5); begin(&c); ptc_hot_reload_confirm_ipc_closed(&c);
    running = 0; pm_sessions = 0; /* A fresh NRO process after the original process was interrupted. */
    ptc_hot_reload_init(&recovered); ptc_hot_reload_recover_startup(&recovered);
    assert(exists(FLAG) && recovered.phase == PTC_HOT_RELOAD_PHASE_WAIT_SOURCE_EXIT);
    ready(&recovered); assert(recovered.status == PTC_HOT_RELOAD_SUCCESS && !pm_sessions);
    assert(chdir("../..") == 0);

    fixture(6); begin(&c); ptc_hot_reload_confirm_ipc_closed(&c);
    running = 0; wrong_boot = true; ready(&c);
    assert(c.status == PTC_HOT_RELOAD_RUNNING);
    ticks += UINT64_C(30000000000); ptc_hot_reload_tick(&c);
    assert(c.phase == PTC_HOT_RELOAD_PHASE_FAILED && launches == 1 && exists(FLAG) && !pm_sessions);
    assert(exists(ROOT "/handover/reload.json")); assert(chdir("../..") == 0);

    fixture(7); begin(&c); ptc_hot_reload_exit(&c);
    char original[1024];
    FILE *claimed = fopen(CLAIM, "rb"); assert(claimed);
    size_t count = fread(original, 1, sizeof(original) - 1, claimed);
    assert(!ferror(claimed) && fclose(claimed) == 0); original[count] = '\0';
    put(REQUEST, original);
    ptc_hot_reload_init(&recovered); ptc_hot_reload_inspect(&recovered);
    assert(recovered.status == PTC_HOT_RELOAD_UNAVAILABLE && !ptc_hot_reload_begin(&recovered));
    assert(exists(FLAG) && !launches && !pm_sessions); assert(chdir("../..") == 0);

    fixture(8); begin(&c); ptc_hot_reload_confirm_ipc_closed(&c);
    running = 0; put(ROOT "/pctc.nro", "foreign component");
    for (unsigned i = 0; i < 3; ++i) ptc_hot_reload_tick(&c);
    assert(c.phase == PTC_HOT_RELOAD_PHASE_FAILED && exists(FLAG) && !launches && !pm_sessions);
    assert(exists(ROOT "/handover/reload.json")); assert(chdir("../..") == 0);

    fixture(9); ptc_hot_reload_init(&c); ptc_hot_reload_inspect(&c); assert(ptc_hot_reload_begin(&c));
    char legacy_ack[256];
    snprintf(legacy_ack, sizeof(legacy_ack), "{\"transaction_id\":\"%s\",\"status\":\"ready\"}", c.journal.transaction_id);
    put(ROOT "/handover/ready.json", legacy_ack); ptc_hot_reload_tick(&c);
    assert(c.phase == PTC_HOT_RELOAD_PHASE_WAIT_ACK);
    ticks += UINT64_C(10000000000); ptc_hot_reload_tick(&c);
    assert(c.phase == PTC_HOT_RELOAD_PHASE_FAILED && exists(FLAG) && !launches && !pm_sessions);
    assert(chdir("../..") == 0);
    test_event_append();
    puts("PASS actual NRO capture restart, single claim, bounded exit/recovery and event append retry/no replay");
    return 0;
}
