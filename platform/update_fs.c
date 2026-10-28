#define _POSIX_C_SOURCE 200809L
#include "update_fs.h"
#include "../common/crypto/sha256.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#define fdopen _fdopen
#define open _open
#define write _write
#define close _close
#else
#include <sys/statvfs.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

bool ptc_update_fs_path(const PtcUpdateFs *fs, const char *relative, char *out, size_t size)
{
    if (!fs || !relative || *relative == '/' || strstr(relative, "..") || strchr(relative, '\\') || strchr(relative, ':')) return false;
    int n = snprintf(out, size, "%s/%s", fs->root, relative);
    return n > 0 && (size_t)n < size;
}

bool ptc_update_fs_mkdirs(const char *path)
{
    char copy[768];
    if (!path || strlen(path) >= sizeof(copy)) return false;
    strcpy(copy, path);
    for (char *p = copy + 1; *p; ++p) if (*p == '/') {
        if (p[-1] == ':') continue;
        *p = '\0';
        struct stat st;
        bool ok = stat(copy, &st) == 0 ? S_ISDIR(st.st_mode) : mkdir(copy, 0777) == 0;
        *p = '/'; if (!ok) return false;
    }
    return true;
}

static bool commit(void *ctx)
{
    PtcUpdateFs *fs = ctx;
#ifdef __SWITCH__
    fs->last_result = fsdevCommitDevice("sdmc");
    return R_SUCCEEDED(fs->last_result);
#else
    (void)fs; return true;
#endif
}

static bool exists(void *ctx, const char *relative)
{
    char path[768]; struct stat st;
    return ptc_update_fs_path(ctx, relative, path, sizeof(path)) && stat(path, &st) == 0;
}

static bool hash(void *ctx, const char *relative, uint64_t *size, char hex[65])
{
    char path[768]; uint8_t bytes[16384], digest[32]; size_t got;
    PtcSha256Ctx sha;
    if (!ptc_update_fs_path(ctx, relative, path, sizeof(path))) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    ptc_sha256_init(&sha); *size = 0;
    bool bounded = true;
    while ((got = fread(bytes, 1, sizeof(bytes), file)) != 0) {
        *size += got;
        if (*size > PTC_UPDATE_EXTRACT_LIMIT) { bounded = false; break; }
        ptc_sha256_update(&sha, bytes, got);
    }
    bool ok = bounded && !ferror(file); if (fclose(file)) ok = false;
    if (!ok) return false;
    ptc_sha256_final(&sha, digest);
    for (size_t i = 0; i < sizeof(digest); ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[64] = '\0'; return true;
}

static bool copy_file(void *ctx, const char *source, const char *target)
{
    char from[768], to[768]; uint8_t buffer[16384]; size_t got;
    if (!ptc_update_fs_path(ctx, source, from, sizeof(from)) || !ptc_update_fs_path(ctx, target, to, sizeof(to)) ||
        exists(ctx, target) || !ptc_update_fs_mkdirs(to)) return false;
    FILE *in = fopen(from, "rb");
    int fd = in ? open(to, O_WRONLY | O_CREAT | O_EXCL, 0600) : -1;
    FILE *out = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (fd >= 0 && !out) close(fd);
    bool ok = in && out;
    uint64_t copied = 0;
    while (ok && (got = fread(buffer, 1, sizeof(buffer), in)) != 0) {
        copied += got;
        if (copied > PTC_UPDATE_EXTRACT_LIMIT || fwrite(buffer, 1, got, out) != got) ok = false;
    }
    if (in && ferror(in)) ok = false;
    if (in && fclose(in)) ok = false;
    if (out && fclose(out)) ok = false;
    /* Leave failed copies for inspection; never publish incomplete bytes. */
    return ok && commit(ctx);
}

static bool remove_file(void *ctx, const char *relative)
{
    char path[768];
    return ptc_update_fs_path(ctx, relative, path, sizeof(path)) && remove(path) == 0;
}

static bool publish(void *ctx, const char *source, const char *target, const char *expected_hash, bool allow_missing)
{
    PtcUpdateFs *fs = ctx;
    char temporary[384], from[768], to[768], hash_from[65], hash_to[65];
    uint64_t from_size, to_size;
    int n = snprintf(temporary, sizeof(temporary), "%s.playwise-update.tmp", target);
    if (n <= 0 || (size_t)n >= sizeof(temporary) || !copy_file(ctx, source, temporary) ||
        !hash(ctx, source, &from_size, hash_from) || !hash(ctx, temporary, &to_size, hash_to) ||
        from_size != to_size || strcmp(hash_from, hash_to) ||
        !ptc_update_fs_path(fs, temporary, from, sizeof(from)) || !ptc_update_fs_path(fs, target, to, sizeof(to))) return false;
    if (exists(ctx, target)) {
        if (!hash(ctx, target, &to_size, hash_to) || strcmp(hash_to, expected_hash)) return false;
        if (remove(to)) { fs->last_errno = errno; return false; }
    } else if (!allow_missing) return false;
    if (!commit(ctx) || rename(from, to)) { fs->last_errno = errno; return false; }
    return commit(ctx);
}

static bool read_text(void *ctx, const char *relative, char *out, size_t size)
{
    PtcUpdateFs *fs = ctx; char path[768];
    return ptc_update_fs_path(fs, relative, path, sizeof(path)) && fs->storage->vtable->read_text(fs->storage, path, out, size);
}

static bool write_text(void *ctx, const char *relative, const char *text)
{
    PtcUpdateFs *fs = ctx; char path[768];
    if (!ptc_update_fs_path(fs, relative, path, sizeof(path)) || !ptc_update_fs_mkdirs(path)) return false;
    char temporary[800]; snprintf(temporary, sizeof(temporary), "%s.online-tmp", path);
    struct stat st;
    if (stat(temporary, &st) == 0) {
        char *old = malloc(PTC_UPDATE_JOURNAL_LIMIT);
        PtcUpdateJournal *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
        bool owned = old && fs->storage->vtable->read_text(fs->storage, temporary, old, PTC_UPDATE_JOURNAL_LIMIT) && !strcmp(old, text);
        if (!owned && old && a && b && ptc_update_journal_parse(old, a) && ptc_update_journal_parse(text, b)) {
            memset(a->phase, 0, sizeof(a->phase)); memset(b->phase, 0, sizeof(b->phase)); a->next_file = b->next_file = 0;
            a->target_pid = b->target_pid = 0;
            memset(a->target_boot_id, 0, sizeof(a->target_boot_id)); memset(b->target_boot_id, 0, sizeof(b->target_boot_id));
            memset(a->status_request_id, 0, sizeof(a->status_request_id)); memset(b->status_request_id, 0, sizeof(b->status_request_id));
            memset(a->status_sha256, 0, sizeof(a->status_sha256)); memset(b->status_sha256, 0, sizeof(b->status_sha256));
            owned = !memcmp(a, b, sizeof(*a));
        }
        free(old); free(a); free(b);
        if (!owned || remove(temporary) || !commit(ctx)) return false;
    }
    FILE *file = fopen(temporary, "wb");
    if (!file) return false;
    bool ok = fwrite(text, 1, strlen(text), file) == strlen(text);
    if (fclose(file)) ok = false;
    if (!ok || !commit(ctx)) return false;
    if (exists(ctx, relative) && remove(path)) return false;
    if (!commit(ctx) || rename(temporary, path)) return false;
    return commit(ctx);
}

static bool discard_temporary(void *ctx, const char *path, const char *before, const char *after)
{
    char relative[384], hash_value[65]; uint64_t size;
    if (snprintf(relative, sizeof(relative), "%s.playwise-update.tmp", path) >= (int)sizeof(relative)) return false;
    if (!exists(ctx, relative)) return true;
    return hash(ctx, relative, &size, hash_value) && (!strcmp(hash_value, before) || !strcmp(hash_value, after)) &&
        remove_file(ctx, relative) && commit(ctx);
}

static bool claim(void *ctx, const char *relative, const char *text)
{
    char path[768];
    if (!ptc_update_fs_path(ctx, relative, path, sizeof(path)) || !ptc_update_fs_mkdirs(path)) return false;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return false;
    int written = (int)write(fd, text, (unsigned)strlen(text));
    bool ok = written >= 0 && (size_t)written == strlen(text);
    if (close(fd)) ok = false;
    return ok && commit(ctx);
}

void ptc_update_fs_init(PtcUpdateFs *fs, const char *root, PtcStorage *storage)
{
    memset(fs, 0, sizeof(*fs)); fs->root = root; fs->storage = storage;
    fs->io = (PtcUpdateIo){fs, hash, exists, copy_file, publish, remove_file, read_text, write_text, commit, discard_temporary, claim};
}

bool ptc_update_fs_space(const PtcUpdateFs *fs, uint64_t required)
{
    char root[768]; snprintf(root, sizeof(root), "%s/", fs->root);
#ifdef _WIN32
    ULARGE_INTEGER available;
    return GetDiskFreeSpaceExA(root, &available, NULL, NULL) && available.QuadPart >= required;
#else
    struct statvfs status;
    return statvfs(root, &status) == 0 && status.f_frsize &&
        (uint64_t)status.f_bavail >= required / status.f_frsize + (required % status.f_frsize != 0);
#endif
}

bool ptc_update_fs_contains(PtcUpdateFs *fs, const char *relative, const char *needle)
{
    char path[768]; size_t length = strlen(needle), keep = 0, got;
    if (!length || length >= 16384 || !ptc_update_fs_path(fs, relative, path, sizeof(path))) return false;
    FILE *file = fopen(path, "rb"); if (!file) return false;
    char *buffer = malloc(32768); bool found = false;
    while (buffer && (got = fread(buffer + keep, 1, 16384, file)) != 0) {
        size_t total = keep + got;
        for (size_t i = 0; i + length <= total; ++i) if (!memcmp(buffer + i, needle, length)) { found = true; break; }
        if (found) break;
        keep = total < length - 1 ? total : length - 1;
        memmove(buffer, buffer + total - keep, keep);
    }
    free(buffer); fclose(file); return found;
}

bool ptc_update_fs_busy(PtcStorage *storage, const char *app_root, bool startup)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/updates/install.lock", app_root);
    if (storage->vtable->exists(storage, path)) return true;
    snprintf(path, sizeof(path), "%s/updates/active.json", app_root);
    if (!storage->vtable->exists(storage, path)) return false;
    char *text = malloc(PTC_UPDATE_JOURNAL_LIMIT);
    PtcUpdateJournal *j = malloc(sizeof(*j));
    bool busy = !text || !j || !storage->vtable->read_text(storage, path, text, PTC_UPDATE_JOURNAL_LIMIT) ||
        !ptc_update_journal_parse(text, j);
    if (!busy) busy = ptc_update_install_busy_phase(j->phase) || (!startup && !strcmp(j->phase, "prepared"));
    free(text); free(j); return busy;
}
