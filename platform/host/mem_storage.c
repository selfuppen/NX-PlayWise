#include "mem_storage.h"

#include <stdio.h>
#include <string.h>

static int large_slot_for_file(const PtcMemStorage *mem, int file_index)
{
    int slot;
    for (slot = 0; slot < PTC_MEM_STORAGE_LARGE_FILE_COUNT; ++slot) {
        if (mem->large_file_indices[slot] == file_index) return slot;
    }
    return -1;
}

static char *file_text(PtcMemStorage *mem, int file_index)
{
    int slot = large_slot_for_file(mem, file_index);
    return slot >= 0 ? mem->large_text[slot] : mem->files[file_index].text;
}

static void release_large_slot(PtcMemStorage *mem, int file_index)
{
    int slot = large_slot_for_file(mem, file_index);
    if (slot >= 0) {
        mem->large_file_indices[slot] = -1;
        mem->large_text[slot][0] = '\0';
    }
}

static char *ensure_text_capacity(PtcMemStorage *mem, int file_index, size_t needed)
{
    int slot;
    if (needed <= PTC_MEM_STORAGE_TEXT_SIZE && large_slot_for_file(mem, file_index) < 0) {
        return mem->files[file_index].text;
    }
    if (needed > PTC_MEM_STORAGE_LARGE_TEXT_SIZE) return NULL;
    slot = large_slot_for_file(mem, file_index);
    if (slot >= 0) return mem->large_text[slot];
    for (slot = 0; slot < PTC_MEM_STORAGE_LARGE_FILE_COUNT; ++slot) {
        if (mem->large_file_indices[slot] < 0) {
            snprintf(mem->large_text[slot], sizeof(mem->large_text[slot]), "%s",
                     mem->files[file_index].text);
            mem->large_file_indices[slot] = file_index;
            return mem->large_text[slot];
        }
    }
    return NULL;
}

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
            mem->files[i].modified_unix_seconds = mem->now_unix_seconds;
            mem->files[i].modified_time_valid = true;
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
    snprintf(out, out_size, "%s", file_text(mem, idx));
    return true;
}

static bool mem_write_text_atomic(PtcStorage *storage, const char *path, const char *text)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    int idx;
    char *destination;
    size_t length;
    if (mem->fail_writes ||
        (mem->fail_write_path_contains && strstr(path, mem->fail_write_path_contains))) {
        return false;
    }
    if (mem->fail_write_path_contains_once && strstr(path, mem->fail_write_path_contains_once)) {
        mem->fail_write_path_contains_once = NULL;
        return false;
    }
    idx = find_file(mem, path);
    if (idx < 0) {
        idx = allocate_file(mem, path);
    }
    if (idx < 0) {
        return false;
    }
    length = strlen(text);
    destination = ensure_text_capacity(mem, idx, length + 1u);
    if (!destination) return false;
    memcpy(destination, text, length + 1u);
    if (length + 1u <= PTC_MEM_STORAGE_TEXT_SIZE &&
        destination != mem->files[idx].text) {
        memcpy(mem->files[idx].text, text, length + 1u);
        release_large_slot(mem, idx);
    }
    mem->files[idx].modified_unix_seconds = mem->now_unix_seconds++;
    mem->files[idx].modified_time_valid = true;
    return true;
}

static bool mem_append_line(PtcStorage *storage, const char *path, const char *line)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    int idx;
    size_t used;
    size_t line_length;
    char *text;
    if (mem->fail_writes || mem->fail_appends ||
        (mem->fail_write_path_contains && strstr(path, mem->fail_write_path_contains))) {
        return false;
    }
    if (mem->fail_write_path_contains_once && strstr(path, mem->fail_write_path_contains_once)) {
        mem->fail_write_path_contains_once = NULL;
        return false;
    }
    idx = find_file(mem, path);
    if (idx < 0) {
        idx = allocate_file(mem, path);
    }
    if (idx < 0) {
        return false;
    }
    text = file_text(mem, idx);
    used = strlen(text);
    line_length = strlen(line);
    text = ensure_text_capacity(mem, idx, used + line_length + 2u);
    if (!text) return false;
    memcpy(text + used, line, line_length);
    text[used + line_length] = '\n';
    text[used + line_length + 1u] = '\0';
    mem->files[idx].modified_unix_seconds = mem->now_unix_seconds++;
    mem->files[idx].modified_time_valid = true;
    return true;
}

static bool mem_rename_path(PtcStorage *storage, const char *from, const char *to)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    int idx;
    if (mem->fail_renames ||
        (mem->fail_rename_path_contains &&
         (strstr(from, mem->fail_rename_path_contains) || strstr(to, mem->fail_rename_path_contains)))) {
        return false;
    }
    idx = find_file(mem, from);
    if (idx < 0 || find_file(mem, to) >= 0) {
        return false;
    }
    snprintf(mem->files[idx].path, sizeof(mem->files[idx].path), "%s", to);
    mem->files[idx].modified_unix_seconds = mem->now_unix_seconds++;
    mem->files[idx].modified_time_valid = true;
    return true;
}

static bool mem_remove_path(PtcStorage *storage, const char *path)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    int idx = find_file(mem, path);
    if (mem->fail_removes || (mem->fail_remove_path_contains && strstr(path, mem->fail_remove_path_contains))) {
        return false;
    }
    if (idx < 0) {
        return false;
    }
    release_large_slot(mem, idx);
    mem->files[idx].present = false;
    return true;
}

static bool path_is_direct_child(const char *path, const char *dir, const char **name, bool *is_directory)
{
    size_t dir_len = strlen(dir);
    const char *relative;
    const char *slash;
    if (strncmp(path, dir, dir_len) != 0 || path[dir_len] != '/') return false;
    relative = path + dir_len + 1;
    if (*relative == '\0') return false;
    slash = strchr(relative, '/');
    *name = relative;
    *is_directory = slash != NULL;
    return true;
}

static bool mem_metadata(PtcStorage *storage, const char *path, PtcStorageMetadata *out)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    int idx = find_file(mem, path);
    int i;
    size_t path_len;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (idx >= 0) {
        out->type = PTC_STORAGE_ENTRY_FILE;
        out->modified_unix_seconds = mem->files[idx].modified_unix_seconds;
        out->modified_time_valid = mem->files[idx].modified_time_valid;
        return true;
    }
    path_len = strlen(path);
    for (i = 0; i < PTC_MEM_STORAGE_MAX_FILES; ++i) {
        if (mem->files[i].present && strncmp(mem->files[i].path, path, path_len) == 0 && mem->files[i].path[path_len] == '/') {
            out->type = PTC_STORAGE_ENTRY_DIRECTORY;
            return true;
        }
    }
    return false;
}

static bool mem_list_entries(PtcStorage *storage, const char *dir, PtcStorageEntry *entries, size_t max, size_t *count)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    size_t found = 0;
    int i;
    if (!entries || !count) return false;
    for (i = 0; i < PTC_MEM_STORAGE_MAX_FILES; ++i) {
        const char *name;
        const char *slash;
        bool is_directory;
        size_t name_len;
        size_t j;
        bool duplicate = false;
        if (!mem->files[i].present || !path_is_direct_child(mem->files[i].path, dir, &name, &is_directory)) continue;
        slash = strchr(name, '/');
        name_len = slash ? (size_t)(slash - name) : strlen(name);
        for (j = 0; j < found; ++j) {
            if (strlen(entries[j].name) == name_len && strncmp(entries[j].name, name, name_len) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate || found >= max || name_len >= sizeof(entries[found].name)) continue;
        memcpy(entries[found].name, name, name_len);
        entries[found].name[name_len] = '\0';
        entries[found].type = is_directory ? PTC_STORAGE_ENTRY_DIRECTORY : PTC_STORAGE_ENTRY_FILE;
        entries[found].modified_unix_seconds = mem->files[i].modified_unix_seconds;
        entries[found].modified_time_valid = !is_directory && mem->files[i].modified_time_valid;
        ++found;
    }
    *count = found;
    return true;
}

static bool mem_remove_tree(PtcStorage *storage, const char *path)
{
    PtcMemStorage *mem = (PtcMemStorage *)storage->ctx;
    size_t path_len = strlen(path);
    int i;
    bool found = false;
    if (mem->fail_removes || (mem->fail_remove_path_contains && strstr(path, mem->fail_remove_path_contains))) return false;
    for (i = 0; i < PTC_MEM_STORAGE_MAX_FILES; ++i) {
        if (mem->files[i].present && (strcmp(mem->files[i].path, path) == 0 ||
            (strncmp(mem->files[i].path, path, path_len) == 0 && mem->files[i].path[path_len] == '/'))) {
            mem->files[i].present = false;
            release_large_slot(mem, i);
            found = true;
        }
    }
    return found;
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
    mem_metadata,
    mem_list_entries,
    mem_remove_tree,
};

void ptc_mem_storage_init(PtcMemStorage *mem)
{
    int slot;
    memset(mem, 0, sizeof(*mem));
    for (slot = 0; slot < PTC_MEM_STORAGE_LARGE_FILE_COUNT; ++slot) {
        mem->large_file_indices[slot] = -1;
    }
    mem->storage.vtable = &MEM_STORAGE_VTABLE;
    mem->storage.ctx = mem;
    mem->now_unix_seconds = 1;
}

void ptc_mem_storage_set_now(PtcMemStorage *mem, int64_t unix_seconds)
{
    if (mem) mem->now_unix_seconds = unix_seconds;
}

bool ptc_mem_storage_set_mtime(PtcMemStorage *mem, const char *path, int64_t unix_seconds, bool valid)
{
    int idx;
    if (!mem || !path) return false;
    idx = find_file(mem, path);
    if (idx < 0) return false;
    mem->files[idx].modified_unix_seconds = unix_seconds;
    mem->files[idx].modified_time_valid = valid;
    return true;
}

PtcStorage *ptc_mem_storage_as_storage(PtcMemStorage *mem)
{
    return &mem->storage;
}
