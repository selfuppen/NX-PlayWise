#include "online_update.h"
#include "../../common/version.h"
#include "release_manifest.h"
#include <minizip/unzip.h>
#include <mbedtls/md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#include <sys/stat.h>

static const char *string(const cJSON *o, const char *key) { return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key)); }
static bool integer(const cJSON *o, const char *key, int expected)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) && v->valuedouble == expected;
}

static uint64_t little(const unsigned char *p, unsigned n)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < n; ++i) value |= (uint64_t)p[i] << (8u * i);
    return value;
}

static bool nro_asset(PtcOnlineUpdater *u, const char *relative, const char *version, bool icon)
{
    char path[768], hash[65], title[512], display[17] = {0};
    unsigned char header[0x80], asset[0x38]; uint64_t size;
    if (!u->fs.io.hash(&u->fs, relative, &size, hash) || !ptc_update_fs_path(&u->fs, relative, path, sizeof(path))) return false;
    FILE *file = fopen(path, "rb"); if (!file) return false;
    bool ok = fread(header, 1, sizeof(header), file) == sizeof(header) && !memcmp(header + 0x10, "NRO0", 4);
    uint64_t base = ok ? little(header + 0x18, 4) : 0;
    ok = ok && base >= sizeof(header) && base + sizeof(asset) <= size && fseek(file, (long)base, SEEK_SET) == 0 &&
        fread(asset, 1, sizeof(asset), file) == sizeof(asset) && !memcmp(asset, "ASET", 4) && !little(asset + 4, 4);
    if (ok) {
        uint64_t icon_offset = little(asset + 8, 8), icon_size = little(asset + 16, 8);
        uint64_t offset = little(asset + 24, 8), length = little(asset + 32, 8);
        ok = (!icon || icon_size) && (!icon_size || (icon_offset <= size - base && icon_size <= size - base - icon_offset)) &&
            offset >= sizeof(asset) && offset <= size - base && length == 0x4000 && length <= size - base - offset &&
            fseek(file, (long)(base + offset), SEEK_SET) == 0 && fread(title, 1, sizeof(title), file) == sizeof(title) &&
            !memcmp(title, "\xE4\xBB\xBB\xE6\x88\x91\xE7\x8E\xA9", 10) &&
            fseek(file, (long)(base + offset + 0x3060), SEEK_SET) == 0 && fread(display, 1, 16, file) == 16 &&
            !strcmp(display, version);
    }
    if (fclose(file)) ok = false;
    return ok;
}

static bool md5_file(PtcOnlineUpdater *u, const char *relative)
{
    char path[768], hex[33]; unsigned char buffer[16384], digest[16] = {0}; size_t got;
    if (!ptc_update_fs_path(&u->fs, relative, path, sizeof(path))) return false;
    FILE *f = fopen(path, "rb"); if (!f) return false;
    mbedtls_md5_context md5; mbedtls_md5_init(&md5);
    bool ok = mbedtls_md5_starts_ret(&md5) == 0;
    while (ok && (got = fread(buffer, 1, sizeof(buffer), f)) != 0)
        ok = !atomic_load(&u->cancel) && mbedtls_md5_update_ret(&md5, buffer, got) == 0;
    if (ferror(f)) ok = false;
    if (fclose(f)) ok = false;
    ok = ok && mbedtls_md5_finish_ret(&md5, digest) == 0;
    mbedtls_md5_free(&md5);
    for (size_t i = 0; i < sizeof(digest); ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return ok && !strcmp(hex, u->offer.md5);
}

static char *embedded_json(const cJSON *build)
{
    /* The build tool embeds ensure_ascii=True JSON. cJSON prints UTF-8 instead;
       compare the same representation, including surrogate pairs and DEL. */
    char *utf8 = cJSON_PrintUnformatted(build);
    if (!utf8 || strlen(utf8) >= 16384) { free(utf8); return NULL; }
    char *ascii = malloc(strlen(utf8) * 6 + 1);
    if (!ascii) { free(utf8); return NULL; }
    char *out = ascii;
    for (const unsigned char *p = (const unsigned char *)utf8; *p;) {
        if (*p < 127) { *out++ = (char)*p++; continue; }
        unsigned count = *p == 127 ? 1 : (*p < 0xe0 ? 2 : (*p < 0xf0 ? 3 : 4));
        unsigned code = count == 1 ? 127 : *p & ((1u << (7u - count)) - 1u);
        for (unsigned i = 1; i < count; ++i) {
            if ((p[i] & 0xc0u) != 0x80u) { free(ascii); free(utf8); return NULL; }
            code = (code << 6) | (p[i] & 63u);
        }
        p += count;
        if (code > 0x10ffffu || (code >= 0xd800u && code <= 0xdfffu)) { free(ascii); free(utf8); return NULL; }
        if (code <= 0xffffu) { snprintf(out, 7, "\\u%04x", code & 0xffffu); out += 6; }
        else {
            code -= 0x10000u;
            snprintf(out, 13, "\\u%04x\\u%04x", 0xd800u | ((code >> 10) & 1023u), 0xdc00u | (code & 1023u)); out += 12;
        }
    }
    *out = '\0'; free(utf8); return ascii;
}

static bool verify_components(PtcOnlineUpdater *u, const char *prefix, const cJSON *build)
{
    char name[384], text[8192];
    snprintf(name, sizeof(name), "%sswitch/playwise/package-artifacts.json", prefix);
    if (!u->fs.io.read(&u->fs, name, text, sizeof(text))) return false;
    cJSON *root = ptc_update_parse_json(text);
    const cJSON *artifacts = cJSON_GetObjectItemCaseSensitive(root, "artifacts");
    const char *release = string(root, "release_id"), *expected_release = string(build, "release_id");
    bool ok = string(build, "playwise_version") && integer(root, "schema_version", 1) && release && expected_release && !strcmp(release, expected_release) &&
        cJSON_IsObject(artifacts) && cJSON_GetArraySize(artifacts) == 3;
    const int indices[] = {0, 2, 11};
    char *embedded = embedded_json(build);
    for (size_t i = 0; ok && i < sizeof(indices) / sizeof(indices[0]); ++i) {
        const char *path = ptc_update_files[indices[i]];
        const cJSON *entry = cJSON_GetObjectItemCaseSensitive(artifacts, path);
        const cJSON *size = cJSON_GetObjectItemCaseSensitive(entry, "size");
        const char *hash = string(entry, "sha256");
        uint64_t actual; char hex[65];
        snprintf(name, sizeof(name), "%s%s", prefix, path);
        ok = ptc_update_hex(hash, 64) && cJSON_IsNumber(size) && size->valuedouble >= 0 &&
            u->fs.io.hash(&u->fs, name, &actual, hex) && size->valuedouble == (double)actual && !strcmp(hash, hex);
        if (ok && indices[i] != 0) ok = embedded && ptc_update_fs_contains(&u->fs, name, embedded) &&
            nro_asset(u, name, string(build, "playwise_version"), indices[i] == 11);
    }
    free(embedded); cJSON_Delete(root); return ok;
}

bool ptc_online_update_verify_current(PtcOnlineUpdater *u)
{
    char text[16384];
    if (!u->fs.io.read(&u->fs, "switch/playwise/build.json", text, sizeof(text))) return false;
    cJSON *build = ptc_update_parse_json(text);
    const char *release = string(build, "release_id");
    const char *version = string(build, "playwise_version"), *profile = string(build, "profile");
    bool ok = release && !strcmp(release, PLAYWISE_BUILD_RELEASE_ID) && version && !strcmp(version, PLAYWISE_VERSION) &&
        profile && !strcmp(profile, "release") && verify_components(u, "", build);
    cJSON_Delete(build); return ok;
}

static bool verify_stage(PtcOnlineUpdater *u)
{
    char prefix[128], path[384], text[16384];
    snprintf(prefix, sizeof(prefix), "switch/playwise/updates/%s/stage/", u->journal.transaction_id);
    snprintf(path, sizeof(path), "%sswitch/playwise/build.json", prefix);
    if (!u->fs.io.read(&u->fs, path, text, sizeof(text))) return false;
    cJSON *build = ptc_update_parse_json(text);
    const char *version = string(build, "playwise_version"), *release = string(build, "release_id"), *profile = string(build, "profile");
    bool ok = version && !strcmp(version, u->offer.version) && release && strlen(release) < sizeof(u->journal.target_release_id) &&
        profile && !strcmp(profile, "release") && integer(build, "schema_version", 1) &&
        integer(build, "protocol_version", PLAYWISE_PROTOCOL_VERSION) && integer(build, "recovery_version", PLAYWISE_RECOVERY_VERSION) &&
        integer(build, "pctl_layout_version", PLAYWISE_PCTL_LAYOUT_VERSION);
    if (ok && !integer(build, "online_update_version", 1)) { u->error = 2; ok = false; }
    if (ok) ok = verify_components(u, prefix, build);
    if (ok) {
        strcpy(u->journal.target_release_id, release); strcpy(u->journal.target_version, version);
        strcpy(u->journal.md5, u->offer.md5);
    }
    cJSON_Delete(build);
    const char *defaults[] = {"auth", "compatibility", "config", "rules", "setup", "state"};
    for (size_t i = 0; ok && i < sizeof(defaults) / sizeof(defaults[0]); ++i) {
        snprintf(path, sizeof(path), "%sswitch/playwise/defaults/%s.json", prefix, defaults[i]);
        ok = u->fs.io.read(&u->fs, path, text, sizeof(text));
        cJSON *o = ok ? ptc_update_parse_json(text) : NULL;
        ok = ok && integer(o, "version", i == 3 ? 2 : 1); cJSON_Delete(o);
    }
    uint64_t size; char hash[65];
    snprintf(path, sizeof(path), "%s%s", prefix, ptc_update_files[1]);
    return ok && u->fs.io.hash(&u->fs, path, &size, hash) && !size;
}

static bool extract(PtcOnlineUpdater *u, const char *zip)
{
    char path[768], name[256], seen[96][256]; unsigned count = 0, assets = 0;
    uint64_t total = 0;
    if (!ptc_update_fs_path(&u->fs, zip, path, sizeof(path))) return false;
    unzFile archive = unzOpen64(path); if (!archive) return false;
    unz_global_info64 global;
    bool ok = unzGetGlobalInfo64(archive, &global) == UNZ_OK && global.number_entry <= 96 && global.number_entry >= PTC_UPDATE_FILE_COUNT;
    uint64_t required = 8u * 1024u * 1024u;
    int pre = ok ? unzGoToFirstFile(archive) : UNZ_END_OF_LIST_OF_FILE;
    while (ok && pre == UNZ_OK) {
        unz_file_info64 info;
        ok = unzGetCurrentFileInfo64(archive, &info, NULL, 0, NULL, 0, NULL, 0) == UNZ_OK &&
            info.uncompressed_size <= PTC_UPDATE_ZIP_LIMIT && required + info.uncompressed_size <= PTC_UPDATE_EXTRACT_LIMIT + 8u * 1024u * 1024u;
        if (ok) required += info.uncompressed_size;
        pre = unzGoToNextFile(archive);
    }
    for (int i = 0; ok && i < PTC_UPDATE_FILE_COUNT; ++i) if (u->fs.io.exists(&u->fs, ptc_update_files[i])) {
        uint64_t size; char digest[65];
        ok = u->fs.io.hash(&u->fs, ptc_update_files[i], &size, digest) && size <= PTC_UPDATE_ZIP_LIMIT;
        if (ok) required += size;
    }
    ok = ok && pre == UNZ_END_OF_LIST_OF_FILE && ptc_update_fs_space(&u->fs, required);
    int next = ok ? unzGoToFirstFile(archive) : UNZ_END_OF_LIST_OF_FILE;
    while (ok && next == UNZ_OK) {
        unz_file_info64 info;
        memset(name, 0, sizeof(name));
        ok = unzGetCurrentFileInfo64(archive, &info, name, sizeof(name), NULL, 0, NULL, 0) == UNZ_OK &&
            info.size_filename && info.size_filename < sizeof(name) && strlen(name) == info.size_filename;
        if (!ok) break;
        bool directory = name[strlen(name) - 1] == '/';
        unsigned mode = (unsigned)(info.external_fa >> 16) & 0170000u;
        ok = ptc_update_archive_path(name, directory) && !(info.flag & 1u) &&
            (info.compression_method == 0 || info.compression_method == 8) && info.disk_num_start == 0 &&
            mode != 0120000u && (mode == 0 || mode == 0100000u || (directory && mode == 0040000u)) &&
            (!directory || !info.uncompressed_size) && info.uncompressed_size <= PTC_UPDATE_ZIP_LIMIT && total + info.uncompressed_size <= PTC_UPDATE_EXTRACT_LIMIT &&
            (!info.uncompressed_size || (info.compressed_size && info.uncompressed_size / info.compressed_size <= 512));
        for (unsigned i = 0; ok && i < count; ++i) if (!strcasecmp(seen[i], name)) ok = false;
        if (!ok || count >= 96) break;
        strcpy(seen[count++], name); total += info.uncompressed_size;
        if (!directory) {
            int index = ptc_update_asset_index(name);
            if (index >= 0) assets |= 1u << index;
            char relative[384];
            snprintf(relative, sizeof(relative), "switch/playwise/updates/%s/stage/%s", u->journal.transaction_id, name);
            ok = ptc_update_fs_path(&u->fs, relative, path, sizeof(path)) && !u->fs.io.exists(&u->fs, relative) &&
                ptc_update_fs_mkdirs(path) && unzOpenCurrentFile(archive) == UNZ_OK;
            FILE *out = ok ? fopen(path, "wb") : NULL;
            if (!out) ok = false;
            unsigned char buffer[16384]; uint64_t written = 0; int got = 0;
            while (ok && (got = unzReadCurrentFile(archive, buffer, sizeof(buffer))) > 0) {
                written += (unsigned)got;
                ok = !atomic_load(&u->cancel) && written <= info.uncompressed_size && fwrite(buffer, 1, (size_t)got, out) == (size_t)got;
            }
            if (got < 0 || written != info.uncompressed_size) ok = false;
            if (out && fclose(out)) ok = false;
            if (unzCloseCurrentFile(archive) != UNZ_OK) ok = false;
        }
        next = unzGoToNextFile(archive);
    }
    ok = ok && next == UNZ_END_OF_LIST_OF_FILE && count == global.number_entry && assets == (1u << PTC_UPDATE_FILE_COUNT) - 1u;
    unzClose(archive);
    return ok && u->fs.io.commit(&u->fs);
}

bool ptc_online_update_package(PtcOnlineUpdater *u)
{
    char zip[160], path[384], text[2048]; uint64_t size;
    snprintf(zip, sizeof(zip), "switch/playwise/updates/%s/download.zip", u->journal.transaction_id);
    if (!md5_file(u, zip) || !u->fs.io.hash(&u->fs, zip, &size, u->journal.zip_sha256) ||
        !extract(u, zip)) return false;
    snprintf(path, sizeof(path), "switch/playwise/updates/%s/stage/info.json", u->journal.transaction_id);
    bool info = u->fs.io.exists(&u->fs, path);
    if (info) {
        if (!u->fs.io.read(&u->fs, path, text, sizeof(text))) return false;
        cJSON *o = ptc_update_parse_json(text);
        const char *version = string(o, "version");
        bool ok = version && !strcmp(version, u->offer.version); cJSON_Delete(o); if (!ok) return false;
    }
    snprintf(path, sizeof(path), "switch/playwise/updates/%s/stage/manifest.install", u->journal.transaction_id);
    if (info != u->fs.io.exists(&u->fs, path)) return false;
    if (info) {
        if (!u->fs.io.read(&u->fs, path, text, sizeof(text))) return false;
        unsigned mask = 0;
        for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
            size_t n = strlen(line); if (n && line[n - 1] == '\r') line[n - 1] = '\0';
            int index = !strncmp(line, "U: ", 3) ? ptc_update_asset_index(line + 3) : -1;
            if (index < 0 || (mask & (1u << index))) return false;
            mask |= 1u << index;
        }
        if (mask != (1u << PTC_UPDATE_FILE_COUNT) - 1u) return false;
    }
    if (!verify_stage(u)) return false;
    /* Freeze all runtime bytes before requesting the serial configuration
       snapshot; reopening may not adopt newly changed staging files. */
    for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) {
        if (!ptc_update_path(path, sizeof(path), &u->journal, "stage", i) ||
            !u->fs.io.hash(&u->fs, path, &u->journal.files[i].after_size, u->journal.files[i].after_sha256)) return false;
    }
    return true;
}
