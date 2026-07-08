#include "mem_storage.h"

#include <stdio.h>
#include <string.h>

static int find_file(PtcMemStorage *mem, const char *path)
{
    int i;
    for (i = 0; i < PTC_MEM_STORAGE_MAX_FILES; ++i) {
        if (mem->files[i].present && strcmp(mem->files[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static int allocate_file(PtcMemStorage *mem, const char *path)
{
    int i;
    for (i = 0; i < PTC_MEM_STORAGE_MAX_FILES; ++i) {
        if (!mem->files[i].present) {
            mem->files[i].present = true;
            snprintf(mem->files[i].path, sizeof(mem->files[i].path), "%s", path);
            mem->files[i].text[0] = '\0';
            return i;
        }
    }
    return -1;
}

static bool mem_read_text(PtcStorage *storage, const char *path, char *out, size_t out_size)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    int idx;
    if (mem->fail_reads) {
        return false;
    }
    idx = find_file(mem, path);
    if (idx < 0) {
        return false;
    }
    snprintf(out, out_size, "%s", mem->files[idx].text);
    return true;
}

static bool mem_write_text_atomic(PtcStorage *storage, const char *path, const char *text)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    int idx;
    if (mem->fail_writes) {
        return false;
    }
    idx = find_file(mem, path);
    if (idx < 0) {
        idx = allocate_file(mem, path);
    }
    if (idx < 0) {
        return false;
    }
    snprintf(mem->files[idx].text, sizeof(mem->files[idx].text), "%s", text);
    return true;
}

static bool mem_append_line(PtcStorage *storage, const char *path, const char *line)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    int idx;
    size_t used;
    if (mem->fail_writes) {
        return false;
    }
    idx = find_file(mem, path);
    if (idx < 0) {
        idx = allocate_file(mem, path);
    }
    if (idx < 0) {
        return false;
    }
    used = strlen(mem->files[idx].text);
    snprintf(mem->files[idx].text + used, sizeof(mem->files[idx].text) - used, "%s\n", line);
    return true;
}

static bool mem_rename_path(PtcStorage *storage, const char *from, const char *to)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    int idx;
    if (mem->fail_renames) {
        return false;
    }
    idx = find_file(mem, from);
    if (idx < 0 || find_file(mem, to) >= 0) {
        return false;
    }
    snprintf(mem->files[idx].path, sizeof(mem->files[idx].path), "%s", to);
    return true;
}

static bool mem_remove_path(PtcStorage *storage, const char *path)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    int idx = find_file(mem, path);
    if (idx < 0) {
        return false;
    }
    mem->files[idx].present = false;
    return true;
}

static bool mem_exists(PtcStorage *storage, const char *path)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    return find_file(mem, path) >= 0;
}

static bool mem_list_json(PtcStorage *storage, const char *dir, char names[][128], size_t max, size_t *count)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    size_t dir_len = strlen(dir);
    size_t found = 0;
    int i;
    for (i = 0; i < PTC_MEM_STORAGE_MAX_FILES && found < max; ++i) {
        const char *path = mem->files[i].path;
        const char *name;
        size_t path_len;
        if (!mem->files[i].present || strncmp(path, dir, dir_len) != 0 || path[dir_len] != '/') {
            continue;
        }
        name = path + dir_len + 1;
        if (strchr(name, '/') != NULL) {
            continue;
        }
        path_len = strlen(name);
        if (path_len < 5 || strcmp(name + path_len - 5, ".json") != 0) {
            continue;
        }
        snprintf(names[found], 128, "%s", name);
        ++found;
    }
    *count = found;
    return true;
}

static const PtcStorageVTable MEM_STORAGE_VTABLE = {
    mem_read_text,
    mem_write_text_atomic,
    mem_append_line,
    mem_rename_path,
    mem_remove_path,
    mem_exists,
    mem_list_json,
};

void ptc_mem_storage_init(PtcMemStorage *mem)
{
    memset(mem, 0, sizeof(*mem));
    mem->storage.vtable = &MEM_STORAGE_VTABLE;
    mem->storage.ctx = mem;
}

PtcStorage *ptc_mem_storage_as_storage(PtcMemStorage *mem)
{
    return &mem->storage;
}
