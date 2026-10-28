#include "../../companion/nro/online_update.h"
#include "curl/curl.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef size_t (*Receive)(void *, size_t, size_t, void *);
struct TestCurl { Receive receive, header; void *data, *header_data, *progress_data; char url[1024]; long response; };
static struct TestCurl client;
static int scenario, requests, resources;
static u64 ticks;
static const char body[] = "{\"packages\":[{\"name\":\"NX-PlayWise\",\"version\":\"999.0.0\",\"md5\":\"00000000000000000000000000000000\",\"filesize\":3465}]}";

Result socketInitializeDefault(void) { ++resources; return 0; }
void socketExit(void) { --resources; }
Result nifmInitialize(int service) { (void)service; ++resources; return 0; }
void nifmExit(void) { --resources; }
Result threadCreate(Thread *t, void (*entry)(void *), void *ctx, void *stack, size_t size, int priority, int core)
{ (void)stack; (void)size; (void)priority; (void)core; t->entry = entry; t->ctx = ctx; return 0; }
Result threadStart(Thread *t) { t->entry(t->ctx); return 0; }
Result threadWaitForExit(Thread *t) { (void)t; return 0; }
Result threadClose(Thread *t) { (void)t; return 0; }
Result pmshellInitialize(void) { return 0; }
void pmshellExit(void) {}
Result pmshellGetProcessId(u64 *pid, u64 program) { (void)pid; (void)program; return 15u | (1u << 9); }
void svcSleepThread(int64_t ns) { ticks += (u64)ns; }
u64 armGetSystemTick(void) { return ticks += 1000000; }
u64 armTicksToNs(u64 value) { return value; }
u64 randomGet64(void) { return 1; }
bool ptc_online_update_package(PtcOnlineUpdater *u) { (void)u; return false; }
bool ptc_online_update_verify_current(PtcOnlineUpdater *u) { (void)u; return false; }
int curl_global_init(long flags) { (void)flags; ++resources; return 0; }
void curl_global_cleanup(void) { --resources; }
CURL *curl_easy_init(void) { memset(&client, 0, sizeof(client)); ++resources; return &client; }
void curl_easy_cleanup(CURL *curl) { (void)curl; --resources; }
int curl_easy_setopt(CURL *c, int option, ...)
{
    va_list args; va_start(args, option);
    switch (option) {
    case CURLOPT_URL: snprintf(c->url, sizeof(c->url), "%s", va_arg(args, const char *)); assert(ptc_update_url_allowed(c->url)); break;
    case CURLOPT_WRITEFUNCTION: c->receive = va_arg(args, Receive); break;
    case CURLOPT_HEADERFUNCTION: c->header = va_arg(args, Receive); break;
    case CURLOPT_WRITEDATA: c->data = va_arg(args, void *); break;
    case CURLOPT_HEADERDATA: c->header_data = va_arg(args, void *); break;
    case CURLOPT_XFERINFODATA: c->progress_data = va_arg(args, void *); break;
    case CURLOPT_SSL_VERIFYPEER: assert(va_arg(args, long) == 1); break;
    case CURLOPT_SSL_VERIFYHOST: assert(va_arg(args, long) == 2); break;
    case CURLOPT_PROTOCOLS:
    case CURLOPT_REDIR_PROTOCOLS: assert(va_arg(args, long) == CURLPROTO_HTTPS); break;
    case CURLOPT_CONNECTTIMEOUT:
    case CURLOPT_TIMEOUT: assert(va_arg(args, long) > 0); break;
    case CURLOPT_FOLLOWLOCATION: assert(va_arg(args, long) == 0); break;
    default: break;
    }
    va_end(args); return 0;
}
int curl_easy_perform(CURL *c)
{
    ++requests;
    if (scenario == 3) return 51; /* Certificate verification failed. */
    bool redirect = requests == 1 && (scenario == 1 || scenario == 4 || scenario == 6);
    c->response = redirect ? 301 : (scenario == 2 ? 404 : 200);
    char header[64]; snprintf(header, sizeof(header), "HTTP/1.1 %ld status\r\n", c->response);
    assert(c->header(header, 1, strlen(header), c->header_data) == strlen(header));
    if (scenario == 5) atomic_store(&((PtcOnlineUpdater *)c->progress_data)->cancel, true);
    if (scenario == 7) return c->receive((void *)body, 1, PTC_UPDATE_METADATA_LIMIT + 1u, c->data) == 0 ? 23 : 0;
    if (scenario == 8) return c->receive((void *)body, SIZE_MAX, 2, c->data) == 0 ? 23 : 0;
    if (scenario == 4) ticks += UINT64_C(21000000000);
    const char *payload = redirect ? "redirect response body" : body;
    return c->receive((void *)payload, 1, strlen(payload), c->data) == strlen(payload) ? 0 : 23;
}
int curl_easy_getinfo(CURL *c, int info, ...)
{
    va_list args; va_start(args, info);
    if (info == CURLINFO_RESPONSE_CODE) *va_arg(args, long *) = c->response;
    if (info == CURLINFO_REDIRECT_URL) *va_arg(args, char **) = scenario == 6 ? "https://other.invalid/package" : "https://download.nswiki.cn/next.json";
    va_end(args); return 0;
}

static bool read_text(PtcStorage *storage, const char *path, char *out, size_t size)
{
    (void)storage; FILE *f = fopen(path, "rb"); if (!f) return false;
    size_t n = fread(out, 1, size - 1, f); out[n] = 0;
    bool ok = !ferror(f) && fgetc(f) == EOF; if (fclose(f)) ok = false; return ok;
}

static void pending_reopen(const char *root)
{
    PtcStorageVTable vtable = {0}; vtable.read_text = read_text;
    PtcStorage storage = {&vtable, NULL};
    PtcOnlineUpdater u, reopened; memset(&u, 0, sizeof(u)); ptc_update_fs_init(&u.fs, root, &storage);
    strcpy(u.journal.transaction_id, "11111111111111111111111111111111");
    strcpy(u.journal.source_release_id, "previous-source"); strcpy(u.journal.target_release_id, "previous-target");
    strcpy(u.journal.target_version, "1.1.0"); strcpy(u.journal.source_boot_id, "1111111111111111");
    strcpy(u.journal.request_id, "previous-request"); memset(u.journal.md5, '0', 32); memset(u.journal.zip_sha256, 'a', 64);
    u.journal.source_pid = 7; u.journal.created_at = 10;
    for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) {
        char path[384];
        assert(u.fs.io.write(&u.fs, ptc_update_files[i], i == 1 ? "" : "old"));
        assert(ptc_update_path(path, sizeof(path), &u.journal, "stage", i));
        assert(u.fs.io.write(&u.fs, path, i == 1 ? "" : "new"));
        assert(u.fs.io.hash(&u.fs, path, &u.journal.files[i].after_size, u.journal.files[i].after_sha256));
    }
    char path[384]; assert(ptc_update_path(path, sizeof(path), &u.journal, "config-backup.json", -1));
    assert(u.fs.io.write(&u.fs, path, "configuration"));
    assert(u.fs.io.hash(&u.fs, path, &u.journal.config_size, u.journal.config_sha256));
    assert(ptc_update_prepare(&u.fs.io, &u.journal) && ptc_update_install(&u.fs.io, &u.journal) && ptc_update_rollback(&u.fs.io, &u.journal));
    memset(&u.journal, 0, sizeof(u.journal));
    strcpy(u.journal.transaction_id, "22222222222222222222222222222222"); strcpy(u.journal.request_id, "original-pending-request");
    strcpy(u.journal.source_release_id, "current-source"); strcpy(u.journal.target_release_id, "next-target");
    strcpy(u.journal.source_boot_id, "2222222222222222"); strcpy(u.journal.target_version, "2.0.0");
    memset(u.journal.md5, '0', 32); memset(u.journal.zip_sha256, 'b', 64); u.journal.source_pid = 9; u.journal.created_at = 20;
    for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) {
        u.journal.files[i].after_size = (uint64_t)i;
        memset(u.journal.files[i].after_sha256, 'c', 64);
    }
    assert(ptc_online_update_pending_backup(&u, true));
    ptc_online_update_init_at(&reopened, &storage, root);
    assert(reopened.status == PTC_UPDATE_BACKUP_PENDING && reopened.backup_submitted);
    assert(!strcmp(reopened.journal.request_id, "original-pending-request"));
    assert(!strcmp(reopened.journal.transaction_id, "22222222222222222222222222222222"));
    assert(reopened.journal.source_pid == 9); /* Prior terminal journal must not replace a newer unknown receipt. */
    for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) assert(reopened.journal.files[i].after_size == (uint64_t)i &&
        !strcmp(reopened.journal.files[i].after_sha256, u.journal.files[i].after_sha256));
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (scenario = 0; scenario <= 9; ++scenario) {
        PtcOnlineUpdater u; memset(&u, 0, sizeof(u));
        atomic_init(&u.cancel, false); atomic_init(&u.done, false); atomic_init(&u.progress, 0);
        requests = resources = 0; ticks = 0;
        assert(ptc_online_update_start(&u, 1));
        assert(u.worker_live && !ptc_online_update_start(&u, 1));
        if (scenario == 9) atomic_store(&u.cancel, true); /* Completed worker, not yet joined. */
        assert(ptc_online_update_poll(&u)); assert(!u.worker_live && !resources);
        if (scenario == 0 || scenario == 1) { assert(u.status == PTC_UPDATE_AVAILABLE); assert(u.offer.size_kib == 3465); }
        else if (scenario == 5 || scenario == 9) assert(u.status == PTC_UPDATE_CANCELED);
        else assert(u.status == PTC_UPDATE_FAILED);
        assert(requests == (scenario == 1 ? 2 : 1));
        ptc_online_update_exit(&u); assert(!resources);
    }
    assert(argc == 2); pending_reopen(argv[1]);
    puts("PASS real update transport orchestration: HTTPS policy, redirects, total timeout, limits, early/late cancel, one worker and resource release");
    return 0;
}
