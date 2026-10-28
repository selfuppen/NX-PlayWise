#include "../../common/update/update.h"
#include "../../common/crypto/sha256.h"
#include "../../companion/update_install.h"
#include "../../platform/host/mem_storage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { PtcMemStorage mem; unsigned calls, fail; } Fixture;
static bool step(Fixture *f) { return ++f->calls != f->fail; }
static bool exists(void *ctx, const char *path) { Fixture *f = ctx; return f->mem.storage.vtable->exists(&f->mem.storage, path); }
static bool read_file(void *ctx, const char *path, char *out, size_t size)
{ Fixture *f = ctx; return step(f) && f->mem.storage.vtable->read_text(&f->mem.storage, path, out, size); }
static bool write_file(void *ctx, const char *path, const char *text)
{ Fixture *f = ctx; return step(f) && f->mem.storage.vtable->write_text_atomic(&f->mem.storage, path, text); }
static bool remove_file(void *ctx, const char *path)
{ Fixture *f = ctx; return step(f) && f->mem.storage.vtable->remove_path(&f->mem.storage, path); }
static bool commit(void *ctx) { return step(ctx); }
static void digest(const char *text, char hex[65])
{
    PtcSha256Ctx sha; uint8_t bytes[32]; ptc_sha256_init(&sha); ptc_sha256_update(&sha, (const uint8_t *)text, strlen(text)); ptc_sha256_final(&sha, bytes);
    for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", bytes[i]);
}
static bool hash(void *ctx, const char *path, uint64_t *size, char hex[65])
{
    char text[PTC_UPDATE_JOURNAL_LIMIT];
    if (!read_file(ctx, path, text, sizeof(text))) return false;
    *size = strlen(text); digest(text, hex); return true;
}
static bool copy_file(void *ctx, const char *from, const char *to)
{
    char text[PTC_UPDATE_JOURNAL_LIMIT];
    return !exists(ctx, to) && read_file(ctx, from, text, sizeof(text)) && write_file(ctx, to, text);
}
static bool publish(void *ctx, const char *from, const char *to, const char *expected_hash, bool allow_missing)
{
    char text[PTC_UPDATE_JOURNAL_LIMIT], live_hash[65]; uint64_t size;
    if (exists(ctx, to) ? (!hash(ctx, to, &size, live_hash) || strcmp(live_hash, expected_hash)) : !allow_missing) return false;
    return read_file(ctx, from, text, sizeof(text)) && (!exists(ctx, to) || remove_file(ctx, to)) &&
        commit(ctx) && write_file(ctx, to, text) && commit(ctx);
}
static bool claim(void *ctx, const char *path, const char *text) { return !exists(ctx, path) && write_file(ctx, path, text); }
static PtcUpdateIo io(Fixture *f) { return (PtcUpdateIo){f, hash, exists, copy_file, publish, remove_file, read_file, write_file, commit, NULL, claim}; }

static void fixture(Fixture *f, PtcUpdateJournal *j)
{
    memset(f, 0, sizeof(*f)); ptc_mem_storage_init(&f->mem); memset(j, 0, sizeof(*j));
    strcpy(j->transaction_id, "0123456789abcdef0123456789abcdef");
    strcpy(j->source_release_id, "playwise-1.0.0+source"); strcpy(j->target_release_id, "playwise-1.1.0+target");
    strcpy(j->source_boot_id, "0123456789abcdef"); strcpy(j->request_id, "backup-request");
    strcpy(j->target_version, "1.1.0"); memset(j->md5, 'a', 32); memset(j->zip_sha256, 'b', 64);
    j->source_pid = 17; j->created_at = 200;
    char path[384]; PtcUpdateIo ops = io(f);
    for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) {
        assert(write_file(f, ptc_update_files[i], i == 1 ? "" : "previous-program"));
        assert(ptc_update_path(path, sizeof(path), j, "stage", i));
        assert(write_file(f, path, i == 1 ? "" : "next-program"));
        assert(hash(f, path, &j->files[i].after_size, j->files[i].after_sha256));
    }
    assert(ptc_update_path(path, sizeof(path), j, "config-backup.json", -1));
    assert(write_file(f, path, "settings-backup"));
    assert(ops.hash(f, path, &j->config_size, j->config_sha256));
    assert(write_file(f, "switch/playwise/state.json", "live-usage"));
    assert(write_file(f, "switch/playwise/ledger/used_nonces.jsonl", "consumed"));
    assert(write_file(f, "switch/playwise/backups/config-backup.json", "manual-backup"));
    f->calls = 0;
}

static void untouched(Fixture *f)
{
    char text[80];
    assert(read_file(f, "switch/playwise/state.json", text, sizeof(text)) && !strcmp(text, "live-usage"));
    assert(read_file(f, "switch/playwise/ledger/used_nonces.jsonl", text, sizeof(text)) && !strcmp(text, "consumed"));
    assert(read_file(f, "switch/playwise/backups/config-backup.json", text, sizeof(text)) && !strcmp(text, "manual-backup"));
}

static void previous_programs(Fixture *f)
{
    char text[80];
    for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i)
        assert(read_file(f, ptc_update_files[i], text, sizeof(text)) && !strcmp(text, i == 1 ? "" : "previous-program"));
}

static void versions(void)
{
    const char *ordered[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.10", "1.0.0-rc.1", "1.0.0", "1.0.1-alpha", "1.0.9", "1.0.10"};
    for (size_t i = 0; i < sizeof(ordered) / sizeof(ordered[0]); ++i) for (size_t k = 0; k < sizeof(ordered) / sizeof(ordered[0]); ++k) {
        int order; assert(ptc_update_version_compare(ordered[i], ordered[k], &order)); assert(order == (i > k ? 1 : (i < k ? -1 : 0)));
    }
    const char *bad[] = {"1", "1.0", "01.0.0", "1.0.0-beta.01", "1.0.0-", "1.0.0+", "1.0.0-a..b", "1.0.0+b..c", "1.0.0/evil", "42949672960.0.0"};
    int order;
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) assert(!ptc_update_version_compare(bad[i], "1.0.0", &order));
    assert(ptc_update_version_compare("1.0.0+build.1", "1.0.0+build.2", &order) && !order);
    PtcUpdateOffer offer;
    assert(ptc_update_offer_parse("{\"packages\":[{\"name\":\"other\"},{\"name\":\"NX-PlayWise\",\"version\":\"3.1.1-beta.1\",\"md5\":\"64c6d32de7a9188d00ff92d6bafd131f\"}]}", &offer));
    assert(ptc_update_offer_status(&offer, "3.1.2-alpha", false) == PTC_UPDATE_LOCAL_NEWER);
    assert(ptc_update_offer_status(&offer, "3.1.0", false) == PTC_UPDATE_CHANNEL_EXCLUDED);
    assert(ptc_update_offer_status(&offer, "3.1.1-alpha", false) == PTC_UPDATE_AVAILABLE);
    assert(!ptc_update_offer_parse("{\"packages\":[],\"packages\":[]}", &offer));
    assert(!ptc_update_offer_parse("{\"packages\":[{\"name\":\"NX-PlayWise\"},{\"name\":\"NX-PlayWise\"}]}", &offer));
    assert(!ptc_update_parse_json("{\"version\":1,\"version\":2}"));
    char deep[2050];
    const unsigned depths[] = {24, 25, 1024};
    for (unsigned i = 0; i < sizeof(depths) / sizeof(depths[0]); ++i) {
        unsigned n = depths[i]; memset(deep, '[', n); deep[n] = '0'; memset(deep + n + 1, ']', n); deep[n * 2 + 1] = '\0';
        cJSON *value = ptc_update_parse_json(deep);
        assert((value != NULL) == (n == 24)); cJSON_Delete(value);
    }
    char wide[4096] = "{";
    for (int i = 0; i < 129; ++i) {
        char member[32]; snprintf(member, sizeof(member), "%s\"key%d\":0", i ? "," : "", i); strcat(wide, member);
    }
    strcat(wide, "}"); assert(!ptc_update_parse_json(wide));
    cJSON *quoted = ptc_update_parse_json("{\"text\":\"[]{}\\\"[]{}\"}");
    assert(quoted); cJSON_Delete(quoted);
    assert(ptc_update_url_allowed(PTC_UPDATE_ZIP_URL));
    assert(!ptc_update_url_allowed("https://download.nswiki.cn.evil/a"));
    assert(!ptc_update_url_allowed("http://download.nswiki.cn/a"));
    assert(!ptc_update_archive_path("switch/playwise/../state.json", false));
    assert(!ptc_update_archive_path("switch/playwise/state.json", false));
    assert(!ptc_update_archive_path("C:/evil", false));
    assert(!ptc_update_archive_path("switch\\playwise\\pctc.nro", false));
    assert(ptc_update_archive_path("switch/playwise/pctc.nro", false));
    assert(ptc_update_archive_path("switch/playwise/inbox/pending/", true));
}

int main(void)
{
    versions();
    Fixture *f = malloc(sizeof(*f)); assert(f); PtcUpdateJournal j, reopened;
    fixture(f, &j); PtcUpdateIo ops = io(f);
    assert(ptc_update_prepare(&ops, &j)); assert(ptc_update_install(&ops, &j));
    assert(ptc_update_files_match(&ops, &j, true)); unsigned operations = f->calls;
    assert(ptc_update_journal_load(&ops, &reopened)); assert(!strcmp(reopened.phase, "files_installed"));
    assert(ptc_update_rollback(&ops, &reopened)); assert(ptc_update_files_match(&ops, &reopened, false)); untouched(f);
    char prior_text[PTC_UPDATE_JOURNAL_LIMIT];
    assert(read_file(f, "switch/playwise/updates/active.json", prior_text, sizeof(prior_text)));
    cJSON *prior = ptc_update_parse_json(prior_text);
    assert(prior && cJSON_ReplaceItemInObjectCaseSensitive(prior, "transaction_id", cJSON_CreateString("ffffffffffffffffffffffffffffffff")));
    char *prior_json = cJSON_PrintUnformatted(prior); assert(prior_json); cJSON_Delete(prior);
    for (unsigned previous = 0; previous < 2; ++previous) for (unsigned fail = 1; fail <= operations + 32; ++fail) {
        fixture(f, &j); f->fail = fail;
        if (previous) { f->fail = 0; assert(write_file(f, "switch/playwise/updates/active.json", prior_json)); f->calls = 0; f->fail = fail; }
        bool prepared = ptc_update_prepare(&ops, &j);
        bool installed = prepared && ptc_update_install(&ops, &j);
        f->fail = 0;
        if (!installed) {
            if (ptc_update_journal_load(&ops, &reopened) && !strcmp(reopened.transaction_id, j.transaction_id)) assert(ptc_update_rollback(&ops, &reopened));
            previous_programs(f);
        }
        untouched(f);
    }
    free(prior_json);
    fixture(f, &j); assert(ptc_update_prepare(&ops, &j)); assert(ptc_update_install(&ops, &j));
    unsigned start = f->calls; assert(ptc_update_rollback(&ops, &j)); unsigned rollback_ops = f->calls - start;
    for (unsigned fail = 1; fail <= rollback_ops; ++fail) {
        fixture(f, &j); assert(ptc_update_prepare(&ops, &j)); assert(ptc_update_install(&ops, &j));
        f->fail = f->calls + fail; (void)ptc_update_rollback(&ops, &j); f->fail = 0;
        assert(ptc_update_journal_load(&ops, &reopened));
        if (strcmp(reopened.phase, "rolled_back")) assert(ptc_update_rollback(&ops, &reopened));
        assert(ptc_update_files_match(&ops, &reopened, false)); untouched(f);
    }
    fixture(f, &j); assert(ptc_update_prepare(&ops, &j)); assert(ptc_update_install(&ops, &j));
    assert(write_file(f, ptc_update_files[0], "external-change"));
    assert(!ptc_update_rollback(&ops, &j));
    fixture(f, &j);
    char changed[384]; assert(ptc_update_path(changed, sizeof(changed), &j, "stage", 0));
    assert(write_file(f, changed, "changed-after-download"));
    assert(!ptc_update_prepare(&ops, &j) && !ptc_update_prepare(&ops, &j));
    assert(!exists(f, "switch/playwise/updates/install.lock"));
    previous_programs(f); untouched(f);
    fixture(f, &j); j.files[0].after_sha256[0] = '\0';
    assert(!ptc_update_prepare(&ops, &j)); previous_programs(f); untouched(f);
    assert(write_file(f, "switch/playwise/updates/active.json", "{\"version\":9}"));
    assert(!ptc_update_journal_load(&ops, &reopened));
    free(f); puts("PASS online update versions, store parsing, path policy, every install/rollback I/O failure, readback and live-data preservation");
    return 0;
}
