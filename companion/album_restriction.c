#include "album_restriction.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define META_PATH PTC_ALBUM_BACKUP_ROOT "/current.meta"
#define OVERRIDE_BACKUP_PATH PTC_ALBUM_BACKUP_ROOT "/override_config.ini.bak"
#define PACKAGE_BACKUP_PATH PTC_ALBUM_BACKUP_ROOT "/package.ini.bak"
#define ACTIVE_PATH PTC_ALBUM_BACKUP_ROOT "/active"
#define CONFLICT_OVERRIDE_PATH PTC_ALBUM_BACKUP_ROOT "/conflict.override_config.ini"
#define CONFLICT_PACKAGE_PATH PTC_ALBUM_BACKUP_ROOT "/conflict.package.ini"

static const char TARGET_SECTION[] =
    "[hbl_config]\n"
    "override_any_app=false\n"
    "program_id_0=0100000000001003\n"
    "override_key_0=!R+X\n"
    "override_address_space=39_bit\n";

typedef struct {
    bool override_existed;
    bool package_existed;
    unsigned long override_hash;
    unsigned long package_hash;
} BackupMeta;

static unsigned long checksum(const char *text)
{
    unsigned long value = 2166136261u;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        value ^= *p++;
        value *= 16777619u;
    }
    return value;
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size) snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static char *read_alloc(PtcStorage *storage, const char *path, bool *existed)
{
    char *buffer;
    if (existed) *existed = storage->vtable->exists(storage, path);
    if (existed && !*existed) return NULL;
    buffer = (char *)malloc(PTC_ALBUM_CONFIG_MAX_BYTES + 1u);
    if (!buffer) return NULL;
    if (!storage->vtable->read_text(storage, path, buffer, PTC_ALBUM_CONFIG_MAX_BYTES + 1u)) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static bool parse_meta(const char *text, BackupMeta *meta)
{
    int oe, pe;
    unsigned long oh, ph;
    if (!text || !meta || sscanf(text, "version=1\noverride_existed=%d\npackage_existed=%d\noverride_hash=%lu\npackage_hash=%lu",
                                  &oe, &pe, &oh, &ph) != 4) return false;
    if ((oe != 0 && oe != 1) || (pe != 0 && pe != 1)) return false;
    meta->override_existed = oe != 0;
    meta->package_existed = pe != 0;
    meta->override_hash = oh;
    meta->package_hash = ph;
    return true;
}

static bool load_backup(PtcStorage *storage, BackupMeta *meta, char **override_text, char **package_text)
{
    char meta_text[256];
    bool exists;
    *override_text = NULL;
    *package_text = NULL;
    if (!storage->vtable->read_text(storage, META_PATH, meta_text, sizeof(meta_text)) || !parse_meta(meta_text, meta)) return false;
    if (meta->override_existed) {
        *override_text = read_alloc(storage, OVERRIDE_BACKUP_PATH, &exists);
        if (!exists || !*override_text || checksum(*override_text) != meta->override_hash) goto fail;
    }
    if (meta->package_existed) {
        *package_text = read_alloc(storage, PACKAGE_BACKUP_PATH, &exists);
        if (!exists || !*package_text || checksum(*package_text) != meta->package_hash) goto fail;
    }
    return true;
fail:
    free(*override_text);
    free(*package_text);
    *override_text = NULL;
    *package_text = NULL;
    return false;
}

static bool restore_one(PtcStorage *storage, const char *path, bool existed, const char *text)
{
    if (existed) return text && storage->vtable->write_text_atomic(storage, path, text);
    if (!storage->vtable->exists(storage, path)) return true;
    return storage->vtable->remove_path(storage, path);
}

bool ptc_album_restriction_transform_ini(const char *input, char *output, size_t output_size)
{
    const char *cursor = input ? input : "";
    size_t used = 0;
    bool skipping = false;
    const bool bom = (unsigned char)cursor[0] == 0xef && (unsigned char)cursor[1] == 0xbb && (unsigned char)cursor[2] == 0xbf;
    const char *newline = strstr(cursor, "\r\n") ? "\r\n" : "\n";
    if (!output || output_size < sizeof(TARGET_SECTION) + 4u) return false;
    if (bom) {
        if (output_size < 4) return false;
        memcpy(output, cursor, 3); used = 3; cursor += 3;
    }
    while (*cursor) {
        const char *end = strchr(cursor, '\n');
        size_t line_len = end ? (size_t)(end - cursor + 1) : strlen(cursor);
        const char *p = cursor;
        const char *trim_end;
        while (p < cursor + line_len && (*p == ' ' || *p == '\t')) ++p;
        trim_end = p;
        while (trim_end < cursor + line_len && *trim_end != '\r' && *trim_end != '\n') ++trim_end;
        if (p < trim_end && *p == '[') {
            const char *close = memchr(p, ']', (size_t)(trim_end - p));
            if (!close) return false;
            skipping = (size_t)(close - p) == strlen("[hbl_config") &&
                       strncmp(p, "[hbl_config]", strlen("[hbl_config]")) == 0;
        }
        if (!skipping) {
            if (used + line_len + 1u > output_size) return false;
            memcpy(output + used, cursor, line_len); used += line_len;
        }
        cursor += line_len;
    }
    if (used && output[used - 1] != '\n') {
        size_t n = strlen(newline);
        if (used + n + 1u > output_size) return false;
        memcpy(output + used, newline, n); used += n;
    }
    {
        const char *p = TARGET_SECTION;
        while (*p) {
            const char *end = strchr(p, '\n');
            size_t n = end ? (size_t)(end - p) : strlen(p);
            if (used + n + strlen(newline) + 1u > output_size) return false;
            memcpy(output + used, p, n); used += n;
            memcpy(output + used, newline, strlen(newline)); used += strlen(newline);
            p += n + (end ? 1u : 0u);
        }
    }
    output[used] = '\0';
    return true;
}

bool ptc_album_restriction_get_status(PtcStorage *storage, PtcAlbumRestrictionStatus *status)
{
    char *current = NULL;
    char *target = NULL;
    char *backup_override = NULL;
    char *backup_package = NULL;
    BackupMeta meta;
    bool current_exists;
    bool package_exists;
    bool backup_valid;
    char phase[32] = {0};
    bool active_configured;
    if (!storage || !status) return false;
    memset(status, 0, sizeof(*status));
    current = read_alloc(storage, PTC_ALBUM_OVERRIDE_PATH, &current_exists);
    package_exists = storage->vtable->exists(storage, PTC_ALBUM_PACKAGE_PATH);
    (void)storage->vtable->read_text(storage, ACTIVE_PATH, phase, sizeof(phase));
    active_configured = strcmp(phase, "configured\n") == 0 || strcmp(phase, "enabling\n") == 0 ||
                        strcmp(phase, "restoring\n") == 0 || strcmp(phase, "rollback_required\n") == 0;
    backup_valid = load_backup(storage, &meta, &backup_override, &backup_package);
    status->backup_valid = backup_valid;
    if (current_exists && current) {
        target = (char *)malloc(PTC_ALBUM_CONFIG_MAX_BYTES + 1u);
        if (target && ptc_album_restriction_transform_ini(current, target, PTC_ALBUM_CONFIG_MAX_BYTES + 1u) &&
            strcmp(current, target) == 0 && !package_exists) {
            status->state = backup_valid ? PTC_ALBUM_RESTRICTION_CONFIGURED : PTC_ALBUM_RESTRICTION_ANOMALY;
            status->restart_required = backup_valid;
            snprintf(status->detail, sizeof(status->detail), "%s", backup_valid ? "已配置，重启主机后生效" : "疑似已受限，但缺少可信备份");
        } else {
            status->state = active_configured ? PTC_ALBUM_RESTRICTION_ANOMALY : PTC_ALBUM_RESTRICTION_OFF;
            snprintf(status->detail, sizeof(status->detail), "%s", status->state == PTC_ALBUM_RESTRICTION_OFF ? "未配置" : "配置与事务记录不一致");
        }
    } else {
        status->state = active_configured ? PTC_ALBUM_RESTRICTION_ANOMALY : PTC_ALBUM_RESTRICTION_OFF;
        snprintf(status->detail, sizeof(status->detail), "%s", current_exists ? "无法完整读取配置" : "未配置");
    }
    free(current); free(target); free(backup_override); free(backup_package);
    return true;
}

bool ptc_album_restriction_enable(PtcStorage *storage, char *error, size_t error_size)
{
    char *original_override = NULL;
    char *original_package = NULL;
    char *updated = NULL;
    char meta[256];
    bool override_existed, package_existed;
    bool ok = false;
    if (!storage) return false;
    original_override = read_alloc(storage, PTC_ALBUM_OVERRIDE_PATH, &override_existed);
    if (override_existed && !original_override) { set_error(error, error_size, "override_config.ini 过大或无法读取"); goto done; }
    original_package = read_alloc(storage, PTC_ALBUM_PACKAGE_PATH, &package_existed);
    if (package_existed && !original_package) { set_error(error, error_size, "package.ini 过大或无法读取"); goto done; }
    updated = (char *)malloc(PTC_ALBUM_CONFIG_MAX_BYTES + 1u);
    if (!updated || !ptc_album_restriction_transform_ini(original_override ? original_override : "", updated,
                                                           PTC_ALBUM_CONFIG_MAX_BYTES + 1u)) {
        set_error(error, error_size, "override_config.ini 格式异常或超过安全上限"); goto done;
    }
    if ((override_existed && !storage->vtable->write_text_atomic(storage, OVERRIDE_BACKUP_PATH, original_override)) ||
        (package_existed && !storage->vtable->write_text_atomic(storage, PACKAGE_BACKUP_PATH, original_package))) {
        set_error(error, error_size, "无法保存完整原始备份"); goto done;
    }
    snprintf(meta, sizeof(meta), "version=1\noverride_existed=%d\npackage_existed=%d\noverride_hash=%lu\npackage_hash=%lu\n",
             override_existed ? 1 : 0, package_existed ? 1 : 0,
             checksum(original_override ? original_override : ""), checksum(original_package ? original_package : ""));
    if (!storage->vtable->write_text_atomic(storage, META_PATH, meta) ||
        !storage->vtable->write_text_atomic(storage, ACTIVE_PATH, "enabling\n")) {
        set_error(error, error_size, "无法持久化相册限制事务"); goto done;
    }
    if (!storage->vtable->write_text_atomic(storage, PTC_ALBUM_OVERRIDE_PATH, updated)) {
        set_error(error, error_size, "写入 override_config.ini 失败"); goto rollback;
    }
    if (package_existed && !storage->vtable->remove_path(storage, PTC_ALBUM_PACKAGE_PATH)) {
        set_error(error, error_size, "删除 Photo Album package.ini 失败"); goto rollback;
    }
    if (!storage->vtable->write_text_atomic(storage, ACTIVE_PATH, "configured\n")) {
        set_error(error, error_size, "无法提交相册限制事务"); goto rollback;
    }
    ok = true;
    goto done;
rollback:
    (void)restore_one(storage, PTC_ALBUM_OVERRIDE_PATH, override_existed, original_override);
    (void)restore_one(storage, PTC_ALBUM_PACKAGE_PATH, package_existed, original_package);
    (void)storage->vtable->write_text_atomic(storage, ACTIVE_PATH, "rollback_required\n");
done:
    free(original_override); free(original_package); free(updated);
    return ok;
}

bool ptc_album_restriction_restore(PtcStorage *storage, bool force, char *error, size_t error_size)
{
    BackupMeta meta;
    char *override_text = NULL;
    char *package_text = NULL;
    PtcAlbumRestrictionStatus status;
    bool ok;
    if (!storage || !load_backup(storage, &meta, &override_text, &package_text)) {
        set_error(error, error_size, "备份缺失或校验失败，已拒绝自动恢复"); return false;
    }
    (void)ptc_album_restriction_get_status(storage, &status);
    if (!force && status.state != PTC_ALBUM_RESTRICTION_CONFIGURED) {
        free(override_text); free(package_text);
        set_error(error, error_size, "检测到外部改动，需由家长确认强制恢复"); return false;
    }
    if (force && status.state == PTC_ALBUM_RESTRICTION_ANOMALY) {
        bool current_exists;
        char *current_override = read_alloc(storage, PTC_ALBUM_OVERRIDE_PATH, &current_exists);
        bool current_package_exists;
        char *current_package = read_alloc(storage, PTC_ALBUM_PACKAGE_PATH, &current_package_exists);
        bool rescued = (!current_exists || (current_override && storage->vtable->write_text_atomic(
                            storage, CONFLICT_OVERRIDE_PATH, current_override))) &&
                       (!current_package_exists || (current_package && storage->vtable->write_text_atomic(
                            storage, CONFLICT_PACKAGE_PATH, current_package)));
        free(current_override);
        free(current_package);
        if (!rescued) {
            free(override_text); free(package_text);
            set_error(error, error_size, "无法保存外部改动救援副本，已拒绝强制恢复"); return false;
        }
    }
    if (!storage->vtable->write_text_atomic(storage, ACTIVE_PATH, "restoring\n")) {
        free(override_text); free(package_text); set_error(error, error_size, "无法持久化恢复事务"); return false;
    }
    ok = restore_one(storage, PTC_ALBUM_OVERRIDE_PATH, meta.override_existed, override_text) &&
         restore_one(storage, PTC_ALBUM_PACKAGE_PATH, meta.package_existed, package_text);
    if (ok) (void)storage->vtable->write_text_atomic(storage, ACTIVE_PATH, "restored\n");
    else set_error(error, error_size, "恢复未完整完成，备份已保留");
    free(override_text); free(package_text);
    return ok;
}
