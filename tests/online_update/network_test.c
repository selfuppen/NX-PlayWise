#include "../../companion/nro/online_update.h"
#include "curl/curl.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "feed_fixture.h"
#include "../../third_party/cjson/cJSON.h"
#include "../../common/crypto/sha256.h"
#include <errno.h>

typedef size_t (*Receive)(void *, size_t, size_t, void *);
struct TestCurl { Receive receive, header; void *data, *header_data, *progress_data;
    CURLcode (*ssl_context)(CURL *, void *, void *); void *ssl_data; char url[1024]; long response; };
static struct TestCurl client;
static int scenario, requests, resources;
static u64 ticks;
static char body[4096];
static unsigned validations;


Result socketInitializeDefault(void) { if (scenario == 30) return 0x12340001u; ++resources; return 0; }
void socketExit(void) { --resources; }
Result nifmInitialize(int service) { (void)service; if (scenario == 31) return 0x12340002u; ++resources; return 0; }
void nifmExit(void) { --resources; }
Result threadCreate(Thread *t, void (*entry)(void *), void *ctx, void *stack, size_t size, int priority, int core)
{ (void)stack; (void)size; (void)priority; (void)core; if (scenario == 35) return 0x12340003u; t->entry = entry; t->ctx = ctx; return 0; }
Result threadStart(Thread *t) { if (scenario == 36) return 0x12340004u; t->entry(t->ctx); return 0; }
Result threadWaitForExit(Thread *t) { (void)t; return 0; }
Result threadClose(Thread *t) { (void)t; return 0; }
Result pmshellInitialize(void) { return 0; }
void pmshellExit(void) {}
Result pmshellGetProcessId(u64 *pid, u64 program) { (void)program; if (scenario >= 20) { *pid = 20; return 0; } return 15u | (1u << 9); }
void svcSleepThread(int64_t ns) { ticks += (u64)ns; }
u64 armGetSystemTick(void) { return ticks += 1000000; }
u64 armTicksToNs(u64 value) { return value; }
u64 randomGet64(void) { return 1; }
bool ptc_online_update_package(PtcOnlineUpdater *u)
{
    ++validations;
    char path[160], hash[65]; uint64_t size;
    snprintf(path, sizeof(path), "switch/playwise/updates/%s/download.zip", u->journal.transaction_id);
    return u->fs.io.hash(&u->fs, path, &size, hash) && size == 3 && !strcmp(hash, u->offer.sha256) && scenario != 25;
}
bool ptc_online_update_verify_current(PtcOnlineUpdater *u) { (void)u; return scenario >= 20; }
int curl_global_init(long flags) { (void)flags; if (scenario == 32) return 2; ++resources; return 0; }
curl_version_info_data *curl_version_info(int age)
{ (void)age; static curl_version_info_data value; value.version = "7.69.1";
    value.ssl_version = scenario == 16 ? "OpenSSL" : "libnx"; return &value; }
void curl_global_cleanup(void) { --resources; }
Result sslContextImportServerPki(SslContext *context, const void *buffer, uint32_t size, int format, uint64_t *id)
{
    (void)context; (void)id;
    assert(format == SslCertificateFormat_Der && size == 959 && ((const unsigned char *)buffer)[0] == 0x30);
    PtcSha256Ctx hash; unsigned char digest[32]; char hex[65];
    ptc_sha256_init(&hash); ptc_sha256_update(&hash, buffer, size); ptc_sha256_final(&hash, digest);
    for (unsigned i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    assert(!strcmp(hex, "5c58468d55f58e497e743982d2b50010b6d165374acf83a7d4a32db768c4408e"));
    return scenario == 14 ? 0x12340005u : 0;
}
CURL *curl_easy_init(void) { if (scenario == 33) return NULL; memset(&client, 0, sizeof(client)); ++resources; return &client; }
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
    case CURLOPT_SSL_CTX_FUNCTION:
        if (scenario == 15) { va_end(args); return 4; }
        c->ssl_context = va_arg(args, CURLcode (*)(CURL *, void *, void *)); break;
    case CURLOPT_SSL_CTX_DATA: c->ssl_data = va_arg(args, void *); break;
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
    bool gitee = !strncmp(c->url, "https://gitee.com/", 18) || !strncmp(c->url, "https://foruda.gitee.com/", 23) ||
        !strncmp(c->url, "https://raw.giteeusercontent.com/", 32);
    assert((c->ssl_context != NULL) == gitee);
    if (c->ssl_context) {
        SslContext context = {0}; int rc = c->ssl_context(c, &context, c->ssl_data);
        if (rc) return rc;
    }
    if (scenario == 3) return 51; /* Certificate verification failed. */
    bool redirect = (requests == 1 && (scenario == 1 || scenario == 4 || scenario == 6 || scenario == 13 || scenario == 24 || scenario == 28)) ||
        (scenario == 27 && requests <= 2);
    c->response = redirect ? 301 : ((scenario == 2 || scenario == 22 || ((scenario == 10) && requests == 1)) ? 404 : 200);
    char header[64]; snprintf(header, sizeof(header), "HTTP/1.1 %ld status\r\n", c->response);
    assert(c->header(header, 1, strlen(header), c->header_data) == strlen(header));
    if (scenario == 5 || scenario == 23) atomic_store(&((PtcOnlineUpdater *)c->progress_data)->cancel, true);
    if (scenario == 7) return c->receive((void *)body, 1, PTC_UPDATE_METADATA_LIMIT + 1u, c->data) == 0 ? 23 : 0;
    if (scenario == 8) return c->receive((void *)body, SIZE_MAX, 2, c->data) == 0 ? 23 : 0;
    if (scenario == 4) ticks += UINT64_C(21000000000);
    if (scenario == 21 && requests == 1) {
        assert(c->receive("pa", 1, 2, c->data) == 2); return 56;
    }
    const char *payload = redirect ? "redirect response body" : (scenario >= 20 && scenario <= 28 ? (scenario == 26 ? "bad!" : "zip") :
        ((scenario == 12 || (scenario == 11 && requests == 1)) ? "{}" : body));
    return c->receive((void *)payload, 1, strlen(payload), c->data) == strlen(payload) ? 0 : 23;
}
int curl_easy_getinfo(CURL *c, int info, ...)
{
    va_list args; va_start(args, info);
    if (info == CURLINFO_RESPONSE_CODE) *va_arg(args, long *) = c->response;
    if (info == CURLINFO_OS_ERRNO) *va_arg(args, long *) = 0;
    if (info == CURLINFO_SSL_VERIFYRESULT) *va_arg(args, long *) = scenario == 3 ? 77 : 0;
    if (info == CURLINFO_REDIRECT_URL) *va_arg(args, char **) = scenario == 27 ? (requests == 1 ?
        "https://gitee.com/selfuppen/nx-playwise/attach_files/3580057/download/playwise-3.0.2.zip" :
        "https://foruda.gitee.com/attach_file/1793690895109930364/playwise-3.0.2.zip?token=test&ts=1") :
        (scenario == 28 ? "https://foruda.gitee.com.evil/attach_file/1/a.zip" : scenario == 6 ? "https://other.invalid/package" : ((scenario == 13 || scenario == 24) ? PTC_UPDATE_FALLBACK_URL :
        "https://raw.giteeusercontent.com/selfuppen/nx-playwise/raw/update-feed/updates.json?signature=test"));
    va_end(args); return 0;
}

static bool read_text(PtcStorage *storage, const char *path, char *out, size_t size)
{
    (void)storage; FILE *f = fopen(path, "rb"); if (!f) return false;
    size_t n = fread(out, 1, size - 1, f); out[n] = 0;
    bool ok = !ferror(f) && fgetc(f) == EOF; if (fclose(f)) ok = false; return ok;
}

static bool empty_queue(PtcStorage *storage, const char *path, char (*names)[128], size_t capacity, size_t *count)
{
    (void)storage; (void)path; (void)names; (void)capacity; *count = 0; return true;
}

static cJSON *network_report(const char *root, int job)
{
    char path[768], text[4097], again[4097];
    snprintf(path, sizeof(path), "%s/switch/playwise/support/update-network.json", root);
    assert(read_text(NULL, path, text, sizeof(text)) && read_text(NULL, path, again, sizeof(again)));
    assert(!strcmp(text, again));
    assert(!strstr(text, "https://") && !strstr(text, "signature") && !strstr(text, "token=") &&
        !strstr(text, "redirect response body"));
    cJSON *value = cJSON_Parse(text); assert(value);
    assert(cJSON_GetObjectItemCaseSensitive(value, "schema_version")->valueint == 2);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(value, "kind")->valuestring, "online_update_network"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(value, "nro_release_id")->valuestring, "host-update-source"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(value, "operation")->valuestring, job == 1 ? "check" : "download"));
    assert(strlen(cJSON_GetObjectItemCaseSensitive(value, "observation_id")->valuestring) == 32);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(value, "sources")) == 2);
    return value;
}

static bool refuse_report(void *ctx, const char *path, const char *text)
{
    PtcUpdateFs *fs = ctx; (void)path; (void)text;
    fs->last_errno = EACCES; fs->last_result = 0xeeeeeeeeu; errno = EACCES; return false;
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
    assert(argc == 2); test_update_metadata(body, sizeof(body), "999.0.0", false);
    for (scenario = 0; scenario <= 14; ++scenario) {
        PtcOnlineUpdater u; char root[512]; snprintf(root, sizeof(root), "%s/metadata-%d", argv[1], scenario);
        PtcStorageVTable vtable = {0}; vtable.read_text = read_text; vtable.list_json = empty_queue;
        PtcStorage storage = {&vtable, NULL};
        ptc_online_update_init_at(&u, &storage, root);
        requests = resources = 0; ticks = 0;
        assert(ptc_online_update_start(&u, 1));
        assert(u.worker_live && !ptc_online_update_start(&u, 1));
        if (scenario == 9) atomic_store(&u.cancel, true); /* Completed worker, not yet joined. */
        assert(ptc_online_update_poll(&u)); assert(!u.worker_live && !resources);
        if (scenario == 0 || scenario == 1 || scenario == 4 || scenario == 6 || scenario == 10 || scenario == 11 || scenario == 13 || scenario == 14) {
            assert(u.status == PTC_UPDATE_AVAILABLE); assert(u.offer.size_bytes == 3);
            assert(u.source == (scenario == 0 || scenario == 1 ? 1u : 2u));
        }
        else if (scenario == 5 || scenario == 9) assert(u.status == PTC_UPDATE_CANCELED);
        else assert(u.status == PTC_UPDATE_FAILED);
        assert(requests == (scenario == 0 || scenario == 5 || scenario == 9 ? 1 : 2));
        cJSON *report = network_report(root, 1);
        cJSON *sources = cJSON_GetObjectItemCaseSensitive(report, "sources");
        cJSON *gitee = cJSON_GetArrayItem(sources, 0), *github = cJSON_GetArrayItem(sources, 1);
        assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(gitee, "attempted")));
        assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(gitee, "ca_import_attempted")));
        assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(github, "ca_import_attempted")));
        if (scenario == 14) {
            assert(cJSON_GetObjectItemCaseSensitive(gitee, "ca_import_result")->valuedouble == 0x12340005u);
            assert(cJSON_GetObjectItemCaseSensitive(gitee, "curl_result")->valueint == CURLE_SSL_CACERT_BADFILE);
            assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(github, "metadata_valid")));
        }
        if (scenario == 0) {
            assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(gitee, "metadata_valid")));
            assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(github, "attempted")));
        }
        if (scenario == 3) {
            assert(cJSON_GetObjectItemCaseSensitive(gitee, "curl_result")->valueint == 51);
            assert(cJSON_GetObjectItemCaseSensitive(github, "curl_result")->valueint == 51);
            assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(github, "curl_result_available")));
            assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(gitee, "tls_verify_result_available")));
            assert(cJSON_GetObjectItemCaseSensitive(gitee, "tls_verify_result")->valueint == 77);
        }
        if (scenario == 10) {
            assert(cJSON_GetObjectItemCaseSensitive(gitee, "http_status")->valueint == 404);
            assert(cJSON_GetObjectItemCaseSensitive(github, "http_status")->valueint == 200);
        }
        if (scenario == 12) {
            assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(gitee, "metadata_valid_available")));
            assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(gitee, "metadata_valid")));
        }
        cJSON_Delete(report);
        ptc_online_update_exit(&u); assert(!resources);
    }
    for (scenario = 15; scenario <= 16; ++scenario) {
        PtcOnlineUpdater u; char root[512]; snprintf(root, sizeof(root), "%s/ca-setup-%d", argv[1], scenario);
        PtcStorageVTable vtable = {0}; vtable.read_text = read_text; vtable.list_json = empty_queue;
        PtcStorage storage = {&vtable, NULL};
        ptc_online_update_init_at(&u, &storage, root); requests = resources = 0; ticks = 0;
        assert(ptc_online_update_start(&u, 1) && ptc_online_update_poll(&u));
        assert(u.status == PTC_UPDATE_AVAILABLE && u.source == 2 && requests == 1 && !resources);
        cJSON *report = network_report(root, 1);
        cJSON *gitee = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(report, "sources"), 0);
        assert(!strcmp(cJSON_GetObjectItemCaseSensitive(gitee, "stage")->valuestring, "ca_setup_failed"));
        assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(gitee, "ca_import_attempted")));
        assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(gitee, "curl_result_available")));
        cJSON_Delete(report); ptc_online_update_exit(&u); assert(!resources);
    }
    for (scenario = 30; scenario <= 36; ++scenario) {
        PtcOnlineUpdater u; char root[512]; snprintf(root, sizeof(root), "%s/initialization-%d", argv[1], scenario);
        PtcStorageVTable vtable = {0}; vtable.read_text = read_text; vtable.list_json = empty_queue;
        PtcStorage storage = {&vtable, NULL};
        ptc_online_update_init_at(&u, &storage, root); requests = resources = 0; ticks = 0;
        if (scenario == 34) u.fs.io.write = refuse_report;
        bool started = ptc_online_update_start(&u, 1);
        assert(started == (scenario < 35));
        if (started) assert(ptc_online_update_poll(&u));
        assert(!resources && !u.worker_live && requests == (scenario == 34 ? 1 : 0));
        if (scenario == 34) {
            assert(u.status == PTC_UPDATE_AVAILABLE && !u.error);
            assert(!u.fs.last_errno && !u.fs.last_result); /* Diagnostics must not replace operation results. */
        } else {
            assert(u.status == PTC_UPDATE_FAILED);
            cJSON *report = network_report(root, 1);
            cJSON *gitee = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(report, "sources"), 0);
            assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(gitee, "curl_result_available")));
            if (scenario == 30) {
                assert(cJSON_GetObjectItemCaseSensitive(report, "socket_result")->valuedouble == 0x12340001u);
                assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(report, "nifm_attempted")));
            }
            if (scenario == 31) assert(cJSON_GetObjectItemCaseSensitive(report, "nifm_result")->valuedouble == 0x12340002u);
            if (scenario == 32) assert(cJSON_GetObjectItemCaseSensitive(report, "curl_global_result")->valueint == 2);
            if (scenario == 33) assert(!strcmp(cJSON_GetObjectItemCaseSensitive(gitee, "stage")->valuestring, "curl_easy_init"));
            if (scenario >= 35) {
                assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(report, "thread_error_available")));
                assert(cJSON_GetObjectItemCaseSensitive(report, "thread_error_result")->valuedouble ==
                    (scenario == 35 ? 0x12340003u : 0x12340004u));
            }
            cJSON_Delete(report);
        }
        ptc_online_update_exit(&u); assert(!resources);
    }
    for (scenario = 20; scenario <= 28; ++scenario) {
        PtcStorageVTable vtable = {0}; vtable.read_text = read_text; vtable.list_json = empty_queue; PtcStorage storage = {&vtable, NULL};
        PtcOnlineUpdater u; char root[512]; snprintf(root, sizeof(root), "%s/download-%d", argv[1], scenario);
        ptc_online_update_init_at(&u, &storage, root); assert(ptc_update_offer_parse(body, &u.offer));
        strcpy(u.offer.sha256, "4a70fe9aa6436e02c2dea340fbd1e352e4ef2d8ce6ca52ad25d4b95471fc8bf2");
        assert(u.fs.io.write(&u.fs, "switch/playwise/handover/runtime-ready.json",
            "{\"profile\":\"release\",\"release_id\":\"host-update-source\",\"boot_id\":\"0123456789abcdef\",\"pid\":20,\"ipc_version\":2,\"ipc_available\":true}"));
        u.status = PTC_UPDATE_AVAILABLE;
        requests = resources = 0; validations = 0; ticks = 0;
        assert(ptc_online_update_start(&u, 2)); assert(ptc_online_update_poll(&u));
        if (scenario == 20 || scenario == 21 || scenario == 24 || scenario == 27 || scenario == 28) {
            assert(u.status == PTC_UPDATE_BACKUP_PENDING && validations == 1);
            assert(u.source == (scenario == 20 || scenario == 27 ? 1u : 2u));
        } else if (scenario == 23) assert(u.status == PTC_UPDATE_CANCELED && !validations);
        else assert(u.status == PTC_UPDATE_FAILED);
        if (scenario == 25) assert(requests == 1 && validations == 1);
        if (scenario == 26) assert(requests == 1 && !validations && u.error == PTC_UPDATE_ERROR_PACKAGE);
        if (scenario == 21 || scenario == 22 || scenario == 24) assert(requests == 2);
        if (scenario == 27) assert(requests == 3);
        if (scenario == 28) assert(requests == 2);
        cJSON_Delete(network_report(root, 2));
        ptc_online_update_exit(&u); assert(!resources);
    }
    pending_reopen(argv[1]);
    puts("PASS real update transport orchestration: HTTPS policy, redirects, total timeout, limits, early/late cancel, one worker and resource release");
    return 0;
}
