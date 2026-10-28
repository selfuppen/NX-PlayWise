#include "../../companion/nro/online_update.h"
#include "../../common/crypto/sha256.h"
#include "minizip/unzip.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct { const char *name; unsigned char *data; size_t size; } Entry;
static Entry entries[16];
static unsigned position, cursor, entry_count;
static int fault;
static const char version[] = "99.0.0-beta.1";

unzFile unzOpen64(const char *path) { (void)path; position = cursor = 0; return entries; }
int unzClose(unzFile file) { (void)file; return 0; }
int unzGetGlobalInfo64(unzFile file, unz_global_info64 *info) { (void)file; info->number_entry = entry_count; return 0; }
int unzGoToFirstFile(unzFile file) { (void)file; position = 0; return entry_count ? 0 : UNZ_END_OF_LIST_OF_FILE; }
int unzGoToNextFile(unzFile file) { (void)file; return ++position < entry_count ? 0 : UNZ_END_OF_LIST_OF_FILE; }
int unzGetCurrentFileInfo64(unzFile file, unz_file_info64 *info, char *name, uLong size, void *extra, uLong extra_size, char *comment, uLong comment_size)
{
    (void)file; (void)extra; (void)extra_size; (void)comment; (void)comment_size;
    Entry *entry = &entries[position]; memset(info, 0, sizeof(*info));
    info->size_filename = (unsigned long)strlen(entry->name); info->compression_method = 8;
    info->uncompressed_size = entry->size; info->compressed_size = entry->size;
    if (name && size) { size_t n = strlen(entry->name); if (n >= size) n = size - 1; memcpy(name, entry->name, n); name[n] = '\0'; }
    if (!position) {
        if (fault == 6) info->flag = 1;
        if (fault == 7) info->external_fa = 0120000u << 16;
        if (fault == 8) info->uncompressed_size = PTC_UPDATE_ZIP_LIMIT + 1u;
        if (fault == 9) info->compression_method = 99;
        if (fault == 10) ++info->size_filename;
    }
    return 0;
}
int unzOpenCurrentFile(unzFile file) { (void)file; cursor = 0; return 0; }
int unzReadCurrentFile(unzFile file, void *buffer, unsigned length)
{
    (void)file; Entry *entry = &entries[position];
    if (fault == 5) return -1;
    size_t n = entry->size - cursor; if (n > length) n = length;
    memcpy(buffer, entry->data + cursor, n); cursor += (unsigned)n; return (int)n;
}
int unzCloseCurrentFile(unzFile file) { (void)file; return fault == 4 ? -1 : 0; }

static void sha(const void *bytes, size_t size, char hex[65])
{
    PtcSha256Ctx ctx; unsigned char result[32];
    ptc_sha256_init(&ctx); ptc_sha256_update(&ctx, bytes, size); ptc_sha256_final(&ctx, result);
    for (unsigned i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", result[i]);
}
static void set_entry(unsigned i, const char *name, const void *data, size_t size)
{
    entries[i].name = name; entries[i].data = malloc(size + 1); assert(entries[i].data);
    memcpy(entries[i].data, data, size); entries[i].data[size] = 0; entries[i].size = size;
}
static void put(unsigned char *out, uint64_t value, unsigned count)
{ for (unsigned i = 0; i < count; ++i) out[i] = (unsigned char)(value >> (8u * i)); }
static void make_nro(unsigned index, const char *manifest)
{
    size_t size = 0x1000 + 0x38 + 0x4000 + 1;
    unsigned char *data = calloc(1, size); assert(data);
    memcpy(data + 0x10, "NRO0", 4); put(data + 0x18, 0x1000, 4); memcpy(data + 0x80, manifest, strlen(manifest));
    unsigned char *asset = data + 0x1000;
    memcpy(asset, "ASET", 4); put(asset + 8, 0x4038, 8); put(asset + 16, 1, 8);
    put(asset + 24, 0x38, 8); put(asset + 32, 0x4000, 8);
    memcpy(asset + 0x38, "\xE4\xBB\xBB\xE6\x88\x91\xE7\x8E\xA9", 9); memcpy(asset + 0x38 + 0x3060, version, strlen(version));
    if (fault == 13 && index == 11) data[0x10] = '!';
    if (fault == 14 && index == 2) asset[0x38 + 0x3060] = '!';
    set_entry(index, ptc_update_files[index], data, size); free(data);
}
static void fixture(void)
{
    memset(entries, 0, sizeof(entries)); entry_count = 15;
    char build[1024];
    snprintf(build, sizeof(build), "{\"schema_version\":1,\"profile\":\"%s\",\"playwise_version\":\"%s\",\"release_id\":\"target-release\",\"protocol_version\":%d,\"recovery_version\":1,\"pctl_layout_version\":1%s}",
        fault == 12 ? "eden-test" : "release", version, fault == 15 ? 2 : 1, fault == 11 ? "" :
        (fault >= 21 ? ",\"online_update_version\":1,\"label\":\"\\u4efb\\ud83c\\udfae\\u007f\"" : ",\"online_update_version\":1"));
    for (unsigned i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) {
        if (i == 2 || i == 11 || i == 10) continue;
        const char *body = i == 1 ? "" : (i == 12 ? build : (i == 6 ? "{\"version\":2}" : "{\"version\":1}"));
        set_entry(i, ptc_update_files[i], body, strlen(body));
    }
    make_nro(2, build); make_nro(11, build);
    if (fault == 22) entries[11].data[0x80] = '!';
    cJSON *artifacts = cJSON_CreateObject(), *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema_version", 1); cJSON_AddStringToObject(root, "release_id", "target-release");
    cJSON_AddItemToObject(root, "artifacts", artifacts);
    unsigned components[] = {0, 2, 11};
    for (unsigned i = 0; i < 3; ++i) {
        unsigned index = components[i]; char hex[65]; sha(entries[index].data, entries[index].size, hex);
        if (fault == 16 && !i) hex[0] = hex[0] == 'a' ? 'b' : 'a';
        cJSON *o = cJSON_CreateObject(); cJSON_AddNumberToObject(o, "size", (double)entries[index].size);
        cJSON_AddStringToObject(o, "sha256", hex); cJSON_AddItemToObject(artifacts, ptc_update_files[index], o);
    }
    char *body = cJSON_PrintUnformatted(root); set_entry(10, ptc_update_files[10], body, strlen(body)); free(body); cJSON_Delete(root);
    char info[128]; snprintf(info, sizeof(info), "{\"version\":\"%s\"}", fault == 17 ? "1.0.0" : version);
    set_entry(13, "info.json", info, strlen(info));
    char list[2048] = "";
    for (unsigned i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) { strcat(list, "U: "); strcat(list, ptc_update_files[i]); strcat(list, "\n"); }
    if (fault == 18) list[0] = 'D';
    set_entry(14, "manifest.install", list, strlen(list));
    if (fault == 1) entries[0].name = "../escape";
    if (fault == 2) entries[0].name = "switch/playwise/state.json";
    if (fault == 3) entries[0].name = entries[1].name;
    if (fault == 20) entry_count = 12;
}
static bool read_text(PtcStorage *storage, const char *path, char *out, size_t size)
{
    (void)storage; FILE *f = fopen(path, "rb"); if (!f) return false;
    size_t n = fread(out, 1, size - 1, f); out[n] = 0;
    bool ok = !ferror(f) && fgetc(f) == EOF; if (fclose(f)) ok = false; return ok;
}
static void write_bytes(PtcUpdateFs *fs, const char *relative, const void *data, size_t size)
{
    char path[768]; assert(ptc_update_fs_path(fs, relative, path, sizeof(path)));
    if (!ptc_update_fs_mkdirs(path)) { fprintf(stderr, "mkdir: %s (errno=%d)\n", path, errno); assert(false); }
    FILE *f = fopen(path, "wb"); assert(f); assert(fwrite(data, 1, size, f) == size); assert(fclose(f) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    PtcStorageVTable vtable = {0}; vtable.read_text = read_text;
    PtcStorage storage = {&vtable, NULL};
    for (fault = 0; fault <= 22; ++fault) {
        fixture(); char root[512]; snprintf(root, sizeof(root), "%s/%02d", argv[1], fault);
        PtcOnlineUpdater u; memset(&u, 0, sizeof(u)); atomic_init(&u.cancel, false);
        ptc_update_fs_init(&u.fs, root, &storage);
        strcpy(u.journal.transaction_id, "0123456789abcdef0123456789abcdef"); strcpy(u.offer.version, version);
        memset(u.offer.md5, '0', 32); if (fault == 19) u.offer.md5[0] = '1';
        write_bytes(&u.fs, "switch/playwise/updates/0123456789abcdef0123456789abcdef/download.zip", "zip", 3);
        write_bytes(&u.fs, "switch/playwise/state.json", "keep-usage", 10);
        bool ok = ptc_online_update_package(&u); assert(ok == (fault == 0 || fault == 21));
        char state[32], path[768]; assert(ptc_update_fs_path(&u.fs, "switch/playwise/state.json", path, sizeof(path)));
        assert(read_text(&storage, path, state, sizeof(state)) && !strcmp(state, "keep-usage"));
        assert(!ptc_update_fs_space(&u.fs, UINT64_MAX));
        if (!fault) {
            strcpy(u.journal.source_release_id, "source-release"); strcpy(u.journal.source_boot_id, "0123456789abcdef");
            strcpy(u.journal.request_id, "backup-request"); u.journal.source_pid = 20; u.journal.created_at = 10;
            for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) write_bytes(&u.fs, ptc_update_files[i], i == 1 ? "" : "old", i == 1 ? 0 : 3);
            char relative[384]; assert(ptc_update_path(relative, sizeof(relative), &u.journal, "config-backup.json", -1));
            write_bytes(&u.fs, relative, "backup", 6);
            assert(u.fs.io.hash(&u.fs, relative, &u.journal.config_size, u.journal.config_sha256));
            char stage[384]; assert(ptc_update_path(stage, sizeof(stage), &u.journal, "stage", 0));
            write_bytes(&u.fs, stage, "changed-after-check", 19);
            assert(!ptc_update_prepare(&u.fs.io, &u.journal));
            write_bytes(&u.fs, stage, entries[0].data, entries[0].size);
            assert(ptc_update_prepare(&u.fs.io, &u.journal)); assert(ptc_update_install(&u.fs.io, &u.journal));
            assert(ptc_update_files_match(&u.fs.io, &u.journal, true));
            assert(ptc_update_rollback(&u.fs.io, &u.journal)); assert(ptc_update_files_match(&u.fs.io, &u.journal, false));
        }
        for (unsigned i = 0; i < 15; ++i) free(entries[i].data);
    }
    puts("PASS production package validation, ZIP-service faults, NRO metadata, real filesystem install/rollback and live data preservation");
    return 0;
}
