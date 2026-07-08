#include "fs_storage.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void ensure_parent_dirs(const char *path)
{
    char tmp[320];
    size_t i;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (i = 0; tmp[i] != '\0'; ++i) {
        if (tmp[i] != '/') {
            continue;
        }
        tmp[i] = '\0';
        if (tmp[0] != '\0' && tmp[strlen(tmp) - 1] != ':') {
            (void)mkdir(tmp, 0777);
        }
        tmp[i] = '/';
    }
}

static bool fs_read_text(PtcStorage *storage, const char *path, char *out, size_t out_size)
{
    FILE *file;
    size_t read_size;
    int extra;
    (void)storage;

    if (!out || out_size == 0) {
        return false;
    }
    file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    read_size = fread(out, 1, out_size - 1, file);
    extra = fgetc(file);
    fclose(file);
    if (extra != EOF) {
        out[0] = '\0';
        return false;
    }
    out[read_size] = '\0';
    return true;
}

static bool fs_write_text_atomic(PtcStorage *storage, const char *path, const char *text)
{
    char tmp_path[352];
    FILE *file;
    size_t text_len;
    (void)storage;

    ensure_parent_dirs(path);
    snprintf(tmp_path, sizeof(tmp_path), "%s.write", path);
    file = fopen(tmp_path, "wb");
    if (!file) {
        return false;
    }
    text_len = strlen(text);
    if (fwrite(text, 1, text_len, file) != text_len) {
        fclose(file);
        remove(tmp_path);
        return false;
    }
    if (fclose(file) != 0) {
        remove(tmp_path);
        return false;
    }
    (void)remove(path);
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return false;
    }
    return true;
}

static bool fs_append_line(PtcStorage *storage, const char *path, const char *line)
{
    FILE *file;
    (void)storage;

    ensure_parent_dirs(path);
    file = fopen(path, "ab");
    if (!file) {
        return false;
    }
    if (fprintf(file, "%s\n", line) < 0) {
        fclose(file);
        return false;
    }
    return fclose(file) == 0;
}

static bool fs_rename_path(PtcStorage *storage, const char *from, const char *to)
{
    (void)storage;
    ensure_parent_dirs(to);
    (void)remove(to);
    return rename(from, to) == 0;
}

static bool fs_remove_path(PtcStorage *storage, const char *path)
{
    (void)storage;
    return remove(path) == 0;
}

static bool fs_exists(PtcStorage *storage, const char *path)
{
    (void)storage;
    return access(path, F_OK) == 0;
}

static bool fs_list_json(PtcStorage *storage, const char *dir, char names[][128], size_t max, size_t *count)
{
    DIR *handle;
    struct dirent *entry;
    size_t found = 0;
    (void)storage;

    handle = opendir(dir);
    if (!handle) {
        *count = 0;
        return false;
    }
    while ((entry = readdir(handle)) != NULL && found < max) {
        size_t name_len = strlen(entry->d_name);
        if (name_len < 5 || strcmp(entry->d_name + name_len - 5, ".json") != 0) {
            continue;
        }
        snprintf(names[found], 128, "%s", entry->d_name);
        ++found;
    }
    closedir(handle);
    *count = found;
    return true;
}

static const PtcStorageVTable FS_STORAGE_VTABLE = {
    fs_read_text,
    fs_write_text_atomic,
    fs_append_line,
    fs_rename_path,
    fs_remove_path,
    fs_exists,
    fs_list_json,
};

void ptc_fs_storage_init(PtcFsStorage *fs)
{
    fs->storage.vtable = &FS_STORAGE_VTABLE;
    fs->storage.ctx = fs;
}

PtcStorage *ptc_fs_storage_as_storage(PtcFsStorage *fs)
{
    return &fs->storage;
}
