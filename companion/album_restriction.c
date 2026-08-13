#include "album_restriction.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char TARGET_SECTION[] =
    "[hbl_config]\n"
    "override_any_app=false\n"
    "program_id_0=0100000000001003\n"
    "override_key_0=X\n"
    "override_address_space=39_bit\n";

typedef struct {
    char phase[32];
    bool override_existed;
    bool package_existed;
    unsigned long override_hash;
    unsigned long package_hash;
    unsigned long target_hash;
} TransactionState;

static unsigned long checksum(const char *text)
{
    unsigned long value = 2166136261u;
    const unsigned char *p = (const unsigned char *)(text ? text : "");
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
    bool present = storage->vtable->exists(storage, path);
    if (existed) *existed = present;
    if (!present) return NULL;
    buffer = (char *)malloc(PTC_ALBUM_CONFIG_MAX_BYTES + 1u);
    if (!buffer) return NULL;
    if (!storage->vtable->read_text(storage, path, buffer, PTC_ALBUM_CONFIG_MAX_BYTES + 1u)) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static bool parse_state(const char *text, TransactionState *state)
{
    int oe, pe;
    if (!text || !state ||
        sscanf(text,
               "version=1\nphase=%31[^\n]\noverride_existed=%d\npackage_existed=%d\noverride_hash=%lu\npackage_hash=%lu\ntarget_hash=%lu",
               state->phase, &oe, &pe, &state->override_hash, &state->package_hash,
               &state->target_hash) != 6) return false;
    if ((oe != 0 && oe != 1) || (pe != 0 && pe != 1)) return false;
    state->override_existed = oe != 0;
    state->package_existed = pe != 0;
    return strcmp(state->phase, "enabling") == 0 || strcmp(state->phase, "configured") == 0 ||
           strcmp(state->phase, "restoring") == 0 || strcmp(state->phase, "rollback_required") == 0;
}

static bool load_state(PtcStorage *storage, TransactionState *state)
{
    char text[320];
    return storage->vtable->read_text(storage, PTC_ALBUM_STATE_PATH, text, sizeof(text)) &&
           parse_state(text, state);
}

static bool save_state(PtcStorage *storage, const TransactionState *state, const char *phase)
{
    char text[320];
    snprintf(text, sizeof(text),
             "version=1\nphase=%s\noverride_existed=%d\npackage_existed=%d\noverride_hash=%lu\npackage_hash=%lu\ntarget_hash=%lu\n",
             phase, state->override_existed ? 1 : 0, state->package_existed ? 1 : 0,
             state->override_hash, state->package_hash, state->target_hash);
    return storage->vtable->write_text_atomic(storage, PTC_ALBUM_STATE_PATH, text);
}

static bool validate_backups(PtcStorage *storage, const TransactionState *state)
{
    bool exists;
    char *text;
    if (state->override_existed) {
        text = read_alloc(storage, PTC_ALBUM_OVERRIDE_BACKUP_PATH, &exists);
        if (!exists || !text || checksum(text) != state->override_hash) { free(text); return false; }
        free(text);
    } else if (storage->vtable->exists(storage, PTC_ALBUM_OVERRIDE_BACKUP_PATH)) return false;
    if (state->package_existed) {
        text = read_alloc(storage, PTC_ALBUM_PACKAGE_BACKUP_PATH, &exists);
        if (!exists || !text || checksum(text) != state->package_hash) { free(text); return false; }
        free(text);
    } else if (storage->vtable->exists(storage, PTC_ALBUM_PACKAGE_BACKUP_PATH)) return false;
    return true;
}

static bool artifacts_exist(PtcStorage *storage)
{
    return storage->vtable->exists(storage, PTC_ALBUM_STATE_PATH) ||
           storage->vtable->exists(storage, PTC_ALBUM_OVERRIDE_BACKUP_PATH) ||
           storage->vtable->exists(storage, PTC_ALBUM_PACKAGE_BACKUP_PATH) ||
           storage->vtable->exists(storage, PTC_ALBUM_OVERRIDE_CONFLICT_PATH) ||
           storage->vtable->exists(storage, PTC_ALBUM_PACKAGE_CONFLICT_PATH);
}

bool ptc_album_restriction_transform_ini(const char *input, char *output, size_t output_size)
{
    const char *cursor = input ? input : "";
    size_t used = 0;
    bool skipping = false;
    const bool bom = (unsigned char)cursor[0] == 0xef && (unsigned char)cursor[1] == 0xbb &&
                     (unsigned char)cursor[2] == 0xbf;
    const char *newline = strstr(cursor, "\r\n") ? "\r\n" : "\n";
    if (!output || output_size < sizeof(TARGET_SECTION) + 4u) return false;
    if (bom) { memcpy(output, cursor, 3); used = 3; cursor += 3; }
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
    TransactionState state;
    char *current = NULL;
    char *target = NULL;
    bool current_exists;
    bool package_exists;
    bool state_exists;
    bool state_valid;
    bool target_active = false;
    if (!storage || !status) return false;
    memset(status, 0, sizeof(*status));
    current = read_alloc(storage, PTC_ALBUM_OVERRIDE_PATH, &current_exists);
    package_exists = storage->vtable->exists(storage, PTC_ALBUM_PACKAGE_PATH);
    if (current_exists && !current) {
        status->state = PTC_ALBUM_RESTRICTION_UNKNOWN;
        snprintf(status->detail, sizeof(status->detail), "无法完整读取当前配置");
        return true;
    }
    if (current) {
        target = (char *)malloc(PTC_ALBUM_CONFIG_MAX_BYTES + 1u);
        target_active = target && ptc_album_restriction_transform_ini(current, target,
                         PTC_ALBUM_CONFIG_MAX_BYTES + 1u) && strcmp(current, target) == 0 && !package_exists;
    }
    state_exists = storage->vtable->exists(storage, PTC_ALBUM_STATE_PATH);
    state_valid = state_exists && load_state(storage, &state);
    if (!state_exists && !storage->vtable->exists(storage, PTC_ALBUM_OVERRIDE_BACKUP_PATH) &&
        !storage->vtable->exists(storage, PTC_ALBUM_PACKAGE_BACKUP_PATH)) {
        status->state = target_active ? PTC_ALBUM_RESTRICTION_EXTERNAL : PTC_ALBUM_RESTRICTION_OFF;
        snprintf(status->detail, sizeof(status->detail), "%s",
                 target_active ? "入口已由外部配置；PlayWise 未修改它，也不能恢复原启动方式" : "未配置");
    } else if (state_valid) {
        status->backup_valid = validate_backups(storage, &state);
        if (strcmp(state.phase, "configured") == 0 && status->backup_valid && target_active &&
            checksum(current) == state.target_hash) {
            status->state = PTC_ALBUM_RESTRICTION_CONFIGURED;
            status->restart_required = true;
            snprintf(status->detail, sizeof(status->detail), "已由 PlayWise 配置，重启主机后生效");
        } else {
            status->state = PTC_ALBUM_RESTRICTION_ANOMALY;
            snprintf(status->detail, sizeof(status->detail), "相邻备份、事务状态或当前配置不一致");
        }
    } else {
        status->state = PTC_ALBUM_RESTRICTION_ANOMALY;
        snprintf(status->detail, sizeof(status->detail), "检测到无法验证的 PlayWise 恢复文件");
    }
    free(current); free(target);
    return true;
}

static bool rollback_enable(PtcStorage *storage, const TransactionState *state)
{
    bool ok = true;
    if (storage->vtable->exists(storage, PTC_ALBUM_OVERRIDE_BACKUP_PATH)) {
        if (storage->vtable->exists(storage, PTC_ALBUM_OVERRIDE_PATH) &&
            !storage->vtable->remove_path(storage, PTC_ALBUM_OVERRIDE_PATH)) ok = false;
        if (ok && !storage->vtable->rename_path(storage, PTC_ALBUM_OVERRIDE_BACKUP_PATH,
                                                PTC_ALBUM_OVERRIDE_PATH)) ok = false;
    } else if (!state->override_existed && storage->vtable->exists(storage, PTC_ALBUM_OVERRIDE_PATH) &&
               !storage->vtable->remove_path(storage, PTC_ALBUM_OVERRIDE_PATH)) {
        ok = false;
    }
    if (state->package_existed && storage->vtable->exists(storage, PTC_ALBUM_PACKAGE_BACKUP_PATH) &&
        !storage->vtable->rename_path(storage, PTC_ALBUM_PACKAGE_BACKUP_PATH,
                                     PTC_ALBUM_PACKAGE_PATH)) ok = false;
    if (ok) {
        if (storage->vtable->exists(storage, PTC_ALBUM_STATE_PATH) &&
            !storage->vtable->remove_path(storage, PTC_ALBUM_STATE_PATH)) ok = false;
    }
    if (!ok) (void)save_state(storage, state, "rollback_required");
    return ok;
}

bool ptc_album_restriction_enable(PtcStorage *storage, char *error, size_t error_size)
{
    TransactionState state;
    PtcAlbumRestrictionStatus status;
    char *original_override = NULL;
    char *original_package = NULL;
    char *updated = NULL;
    bool override_existed, package_existed;
    if (!storage) return false;
    if (ptc_album_restriction_get_status(storage, &status) && status.state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
        set_error(error, error_size, "入口已由外部配置，PlayWise 不会把当前结果伪装成原始备份");
        return false;
    }
    if (artifacts_exist(storage)) {
        set_error(error, error_size, "检测到已有 PlayWise 备份、事务或冲突文件，已拒绝覆盖");
        return false;
    }
    original_override = read_alloc(storage, PTC_ALBUM_OVERRIDE_PATH, &override_existed);
    original_package = read_alloc(storage, PTC_ALBUM_PACKAGE_PATH, &package_existed);
    if ((override_existed && !original_override) || (package_existed && !original_package)) {
        set_error(error, error_size, "原配置过大或无法完整读取"); goto done;
    }
    updated = (char *)malloc(PTC_ALBUM_CONFIG_MAX_BYTES + 1u);
    if (!updated || !ptc_album_restriction_transform_ini(original_override ? original_override : "", updated,
                                                           PTC_ALBUM_CONFIG_MAX_BYTES + 1u)) {
        set_error(error, error_size, "override_config.ini 格式异常或超过安全上限"); goto done;
    }
    memset(&state, 0, sizeof(state));
    state.override_existed = override_existed;
    state.package_existed = package_existed;
    state.override_hash = checksum(original_override);
    state.package_hash = checksum(original_package);
    state.target_hash = checksum(updated);
    if (!save_state(storage, &state, "enabling")) {
        set_error(error, error_size, "无法创建相邻事务状态"); goto done;
    }
    if (override_existed && !storage->vtable->rename_path(storage, PTC_ALBUM_OVERRIDE_PATH,
                                                          PTC_ALBUM_OVERRIDE_BACKUP_PATH)) {
        set_error(error, error_size, "无法就地备份 override_config.ini"); goto rollback;
    }
    if (package_existed && !storage->vtable->rename_path(storage, PTC_ALBUM_PACKAGE_PATH,
                                                         PTC_ALBUM_PACKAGE_BACKUP_PATH)) {
        set_error(error, error_size, "无法就地备份并移除 Photo Album package.ini"); goto rollback;
    }
    if (!storage->vtable->write_text_atomic(storage, PTC_ALBUM_OVERRIDE_PATH, updated)) {
        set_error(error, error_size, "写入 override_config.ini 失败"); goto rollback;
    }
    if (!save_state(storage, &state, "configured")) {
        set_error(error, error_size, "无法提交高级入口事务"); goto rollback;
    }
    free(original_override); free(original_package); free(updated);
    return true;
rollback:
    if (!rollback_enable(storage, &state))
        set_error(error, error_size, "操作失败且未能完整回滚；恢复文件已保留");
done:
    free(original_override); free(original_package); free(updated);
    return false;
}

static bool rescue_current(PtcStorage *storage, bool *override_rescued, bool *package_rescued)
{
    *override_rescued = false;
    *package_rescued = false;
    if (storage->vtable->exists(storage, PTC_ALBUM_OVERRIDE_CONFLICT_PATH) ||
        storage->vtable->exists(storage, PTC_ALBUM_PACKAGE_CONFLICT_PATH)) return false;
    if (storage->vtable->exists(storage, PTC_ALBUM_OVERRIDE_PATH)) {
        if (!storage->vtable->rename_path(storage, PTC_ALBUM_OVERRIDE_PATH,
                                          PTC_ALBUM_OVERRIDE_CONFLICT_PATH)) return false;
        *override_rescued = true;
    }
    if (storage->vtable->exists(storage, PTC_ALBUM_PACKAGE_PATH)) {
        if (!storage->vtable->rename_path(storage, PTC_ALBUM_PACKAGE_PATH,
                                          PTC_ALBUM_PACKAGE_CONFLICT_PATH)) {
            if (*override_rescued) (void)storage->vtable->rename_path(
                storage, PTC_ALBUM_OVERRIDE_CONFLICT_PATH, PTC_ALBUM_OVERRIDE_PATH);
            *override_rescued = false;
            return false;
        }
        *package_rescued = true;
    }
    return true;
}

bool ptc_album_restriction_restore(PtcStorage *storage, bool force, char *error, size_t error_size)
{
    TransactionState state;
    PtcAlbumRestrictionStatus status;
    bool override_rescued = false, package_rescued = false;
    bool ok = true;
    if (!storage || !load_state(storage, &state) || !validate_backups(storage, &state)) {
        set_error(error, error_size, "相邻备份缺失或校验失败，已拒绝恢复"); return false;
    }
    (void)ptc_album_restriction_get_status(storage, &status);
    if (!force && status.state != PTC_ALBUM_RESTRICTION_CONFIGURED) {
        set_error(error, error_size, "检测到外部改动，需由家长确认强制恢复"); return false;
    }
    if (force && status.state == PTC_ALBUM_RESTRICTION_ANOMALY &&
        !rescue_current(storage, &override_rescued, &package_rescued)) {
        set_error(error, error_size, "冲突副本已存在或无法保存，已拒绝强制恢复"); return false;
    }
    if (!save_state(storage, &state, "restoring")) {
        set_error(error, error_size, "无法持久化恢复事务"); return false;
    }
    if (state.package_existed && storage->vtable->exists(storage, PTC_ALBUM_PACKAGE_PATH)) {
        (void)save_state(storage, &state, "rollback_required");
        set_error(error, error_size, "Photo Album 原路径被外部文件占用，已拒绝部分恢复");
        return false;
    }
    if (!force && storage->vtable->exists(storage, PTC_ALBUM_OVERRIDE_PATH) &&
        !storage->vtable->remove_path(storage, PTC_ALBUM_OVERRIDE_PATH)) ok = false;
    if (state.override_existed && !storage->vtable->rename_path(storage,
        PTC_ALBUM_OVERRIDE_BACKUP_PATH, PTC_ALBUM_OVERRIDE_PATH)) ok = false;
    if (state.package_existed && !storage->vtable->rename_path(storage,
        PTC_ALBUM_PACKAGE_BACKUP_PATH, PTC_ALBUM_PACKAGE_PATH)) ok = false;
    if (ok && !storage->vtable->remove_path(storage, PTC_ALBUM_STATE_PATH)) ok = false;
    if (!ok) {
        (void)save_state(storage, &state, "rollback_required");
        set_error(error, error_size, "恢复未完整完成，相邻恢复文件已保留");
    }
    (void)override_rescued; (void)package_rescued;
    return ok;
}
