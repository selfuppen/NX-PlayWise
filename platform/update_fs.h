#ifndef PTC_UPDATE_FS_H
#define PTC_UPDATE_FS_H

#include "../companion/update_install.h"
#include "storage.h"

typedef struct {
    const char *root;
    PtcStorage *storage;
    int last_errno;
    uint32_t last_result;
    PtcUpdateIo io;
} PtcUpdateFs;

void ptc_update_fs_init(PtcUpdateFs *fs, const char *sd_root, PtcStorage *storage);
bool ptc_update_fs_path(const PtcUpdateFs *fs, const char *relative, char *out, size_t size);
bool ptc_update_fs_mkdirs(const char *path);
bool ptc_update_fs_space(const PtcUpdateFs *fs, uint64_t required);
bool ptc_update_fs_contains(PtcUpdateFs *fs, const char *relative, const char *needle);
bool ptc_update_fs_busy(PtcStorage *storage, const char *app_root, bool startup);

#endif
