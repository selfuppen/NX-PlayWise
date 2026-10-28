/* Host-only preview selection; unset during the independent layout checks. */
#ifndef PTC_PREVIEW_SELECTION_H
#define PTC_PREVIEW_SELECTION_H
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static inline bool ptc_preview_selected(const char *path, bool directory)
{
    const char *list = getenv("PLAYWISE_PREVIEW_FILES");
    if (!list) return true;
    size_t length = strlen(path);
    while (*list) {
        const char *end = strchr(list, '\n');
        size_t size = end ? (size_t)(end - list) : strlen(list);
        if (size >= length && memcmp(list, path, length) == 0 &&
            (directory ? (size > length && list[length] == '/') : size == length)) return true;
        if (!end) break;
        list = end + 1;
    }
    return false;
}

static inline int ptc_preview_workers(void)
{
    const char *value = getenv("PLAYWISE_PREVIEW_WORKERS");
    int workers = value ? atoi(value) : 3;
    return workers < 1 ? 1 : workers > 3 ? 3 : workers;
}
#endif
