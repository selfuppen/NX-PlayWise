#include "boot_flags.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

typedef struct {
    char phase[32];
    bool standard_was_enabled;
} Journal;

static bool exists(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0;
}

static bool empty_file(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0 && info.st_size == 0;
}

static void set_message(char *message, size_t size, const char *value)
{
    if (message && size) snprintf(message, size, "%s", value);
}

static void commit_sd(void)
{
#ifdef __SWITCH__
    (void)fsdevCommitDevice("sdmc");
#endif
}

static bool temporary_journal_path(const PtcLabBootFlagPaths *paths, char *out, size_t out_size)
{
    int written = snprintf(out, out_size, "%s.tmp", paths->journal);
    return written > 0 && (size_t)written < out_size;
}

static int journal_phase_order(const char *phase)
{
    if (strcmp(phase, "prepared") == 0) return 0;
    if (strcmp(phase, "standard_disabled") == 0) return 1;
    if (strcmp(phase, "lab_enabled") == 0) return 2;
    if (strcmp(phase, "lab_disabled") == 0) return 3;
    if (strcmp(phase, "restored") == 0) return 4;
    return -1;
}

static bool read_journal_path(const char *path, Journal *journal)
{
    char text[256];
    char *phase;
    char *end;
    FILE *file = fopen(path, "rb");
    size_t got;
    if (!file) return false;
    got = fread(text, 1, sizeof(text) - 1U, file);
    if (fclose(file) != 0 || got == 0 || got >= sizeof(text) - 1U) return false;
    text[got] = '\0';
    /* v2 adds an explicit generation while preserving v1 recovery records
       produced by already-distributed Device Lab packages. */
    if (!strstr(text, "\"version\":1") && !strstr(text, "\"version\":2")) return false;
    phase = strstr(text, "\"phase\":\"");
    if (!phase) return false;
    phase += strlen("\"phase\":\"");
    end = strchr(phase, '\"');
    if (!end || (size_t)(end - phase) >= sizeof(journal->phase)) return false;
    memcpy(journal->phase, phase, (size_t)(end - phase));
    journal->phase[end - phase] = '\0';
    if (journal_phase_order(journal->phase) < 0) return false;
    if (strstr(text, "\"standard_was_enabled\":true")) journal->standard_was_enabled = true;
    else if (strstr(text, "\"standard_was_enabled\":false")) journal->standard_was_enabled = false;
    else return false;
    return true;
}

static bool recover_journal(const PtcLabBootFlagPaths *paths, Journal *journal)
{
    char temporary[512];
    Journal current;
    Journal pending;
    bool current_exists;
    bool current_valid;
    bool pending_valid;
    if (!temporary_journal_path(paths, temporary, sizeof(temporary))) return false;
    current_exists = exists(paths->journal);
    current_valid = current_exists && read_journal_path(paths->journal, &current);
    if (!exists(temporary)) {
        if (!current_valid) return false;
        *journal = current;
        return true;
    }
    pending_valid = read_journal_path(temporary, &pending);
    if (!pending_valid) {
        /* A torn next-generation write is safe to discard while the prior journal is intact. */
        if (!current_valid || remove(temporary) != 0) return false;
        commit_sd();
        *journal = current;
        return true;
    }
    if (current_valid && (current.standard_was_enabled != pending.standard_was_enabled ||
            journal_phase_order(pending.phase) < journal_phase_order(current.phase))) return false;
    if (current_exists && remove(paths->journal) != 0) return false;
    commit_sd();
    if (rename(temporary, paths->journal) != 0) return false;
    commit_sd();
    *journal = pending;
    return true;
}

static bool journal_material_exists(const PtcLabBootFlagPaths *paths)
{
    char temporary[512];
    if (exists(paths->journal)) return true;
    return temporary_journal_path(paths, temporary, sizeof(temporary)) && exists(temporary);
}

bool ptc_lab_boot_flags_inspect(const PtcLabBootFlagPaths *paths, PtcLabBootStatus *status)
{
    char temporary[512];
    Journal current;
    Journal pending;
    Journal selected;
    bool current_valid;
    bool pending_valid;
    bool selected_valid = false;
    if (!paths || !status || !paths->standard_flag || !paths->standard_backup ||
        !paths->lab_flag || !paths->journal ||
        !temporary_journal_path(paths, temporary, sizeof(temporary))) return false;
    memset(status, 0, sizeof(*status));
    status->standard_flag_present = exists(paths->standard_flag);
    status->standard_backup_present = exists(paths->standard_backup);
    status->lab_flag_present = exists(paths->lab_flag);
    status->journal_present = exists(paths->journal);
    status->pending_journal_present = exists(temporary);
    current_valid = status->journal_present && read_journal_path(paths->journal, &current);
    pending_valid = status->pending_journal_present && read_journal_path(temporary, &pending);
    if (pending_valid && (!current_valid ||
            (current.standard_was_enabled == pending.standard_was_enabled &&
             journal_phase_order(pending.phase) >= journal_phase_order(current.phase)))) {
        selected = pending;
        selected_valid = true;
    } else if (current_valid && (!status->pending_journal_present || !pending_valid)) {
        selected = current;
        selected_valid = true;
    }
    if (!status->journal_present && !status->pending_journal_present) {
        status->state = status->standard_backup_present || status->lab_flag_present
            ? PTC_LAB_BOOT_CONFLICT : PTC_LAB_BOOT_NORMAL;
        return true;
    }
    if (!selected_valid) {
        status->state = PTC_LAB_BOOT_CONFLICT;
        return true;
    }
    status->standard_was_enabled = selected.standard_was_enabled;
    snprintf(status->journal_phase, sizeof(status->journal_phase), "%s", selected.phase);
    if (strcmp(selected.phase, "lab_enabled") == 0 && empty_file(paths->lab_flag) &&
        ((selected.standard_was_enabled && !status->standard_flag_present && status->standard_backup_present) ||
         (!selected.standard_was_enabled && !status->standard_flag_present && !status->standard_backup_present))) {
        status->state = PTC_LAB_BOOT_ENABLED;
    } else if (strcmp(selected.phase, "restored") == 0 && !status->lab_flag_present &&
        !status->standard_backup_present &&
        (selected.standard_was_enabled ? status->standard_flag_present : !status->standard_flag_present)) {
        status->state = PTC_LAB_BOOT_RESTORED;
    } else if (strcmp(selected.phase, "prepared") == 0 ||
        strcmp(selected.phase, "standard_disabled") == 0 ||
        strcmp(selected.phase, "lab_disabled") == 0) {
        status->state = PTC_LAB_BOOT_RECOVERY_REQUIRED;
    } else {
        status->state = PTC_LAB_BOOT_CONFLICT;
    }
    return true;
}

static bool write_journal(const PtcLabBootFlagPaths *paths, const Journal *journal)
{
    char temporary[512];
    FILE *file;
    if (!temporary_journal_path(paths, temporary, sizeof(temporary)) || exists(temporary)) return false;
    file = fopen(temporary, "wb");
    if (!file) return false;
    if (fprintf(file,
            "{\"version\":2,\"phase\":\"%s\",\"generation\":%d,\"standard_was_enabled\":%s}\n",
            journal->phase, journal_phase_order(journal->phase),
            journal->standard_was_enabled ? "true" : "false") < 0) {
        (void)fclose(file);
        (void)remove(temporary);
        return false;
    }
    if (fclose(file) != 0) {
        (void)remove(temporary);
        return false;
    }
    commit_sd();
    /* Horizon's SD rename does not replace an existing destination. Keep the
       complete .tmp generation until it has been promoted so a power loss is recoverable. */
    if (exists(paths->journal) && remove(paths->journal) != 0) return false;
    commit_sd();
    if (rename(temporary, paths->journal) != 0) {
        return false;
    }
    commit_sd();
    return true;
}

static bool create_empty_atomic(const char *path)
{
    char temporary[512];
    FILE *file;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (exists(path) || exists(temporary)) return false;
    file = fopen(temporary, "wb");
    if (!file) return false;
    if (fclose(file) != 0) { (void)remove(temporary); return false; }
    commit_sd();
    if (rename(temporary, path) != 0) { (void)remove(temporary); return false; }
    commit_sd();
    return true;
}

PtcLabBootFlagResult ptc_lab_boot_flags_enable(const PtcLabBootFlagPaths *paths,
    char *message, size_t message_size)
{
    Journal journal;
    bool standard;
    if (!paths || !paths->standard_flag || !paths->standard_backup || !paths->lab_flag || !paths->journal) {
        set_message(message, message_size, "启动标志路径无效，未执行任何更改。");
        return PTC_LAB_FLAG_IO_ERROR;
    }
    if (journal_material_exists(paths)) {
        if (!recover_journal(paths, &journal)) {
            set_message(message, message_size, "事务记录无法读取，请选择“恢复正常后台”。");
            return PTC_LAB_FLAG_RECOVERY_REQUIRED;
        }
        if (strcmp(journal.phase, "lab_enabled") == 0 && empty_file(paths->lab_flag) &&
            ((journal.standard_was_enabled && !exists(paths->standard_flag) && exists(paths->standard_backup)) ||
             (!journal.standard_was_enabled && !exists(paths->standard_flag) && !exists(paths->standard_backup)))) {
            set_message(message, message_size, "实验后台已启用，请重启主机后打开 Device Lab 浮窗。");
            return PTC_LAB_FLAG_ALREADY_DONE;
        }
        if (strcmp(journal.phase, "restored") == 0 && !exists(paths->lab_flag) && !exists(paths->standard_backup) &&
            (journal.standard_was_enabled ? exists(paths->standard_flag) : !exists(paths->standard_flag))) {
            if (remove(paths->journal) != 0) {
                set_message(message, message_size, "无法归档已完成的事务记录，未执行新的更改。");
                return PTC_LAB_FLAG_IO_ERROR;
            }
            commit_sd();
        } else {
            set_message(message, message_size, "检测到中断或冲突，请选择“恢复正常后台”。");
            return PTC_LAB_FLAG_RECOVERY_REQUIRED;
        }
    }
    if (exists(paths->standard_backup) || exists(paths->lab_flag)) {
        set_message(message, message_size, "发现未知启动标志或备份，为避免覆盖已停止操作。");
        return PTC_LAB_FLAG_CONFLICT;
    }
    memset(&journal, 0, sizeof(journal));
    standard = exists(paths->standard_flag);
    journal.standard_was_enabled = standard;
    snprintf(journal.phase, sizeof(journal.phase), "prepared");
    if (!write_journal(paths, &journal)) {
        set_message(message, message_size, "无法创建事务记录，未更改启动状态。");
        return PTC_LAB_FLAG_IO_ERROR;
    }
    if (standard && rename(paths->standard_flag, paths->standard_backup) != 0) {
        set_message(message, message_size, "无法安全备份正常后台启动标志，请立即执行恢复。");
        return PTC_LAB_FLAG_RECOVERY_REQUIRED;
    }
    if (standard) commit_sd();
    snprintf(journal.phase, sizeof(journal.phase), "standard_disabled");
    if (!write_journal(paths, &journal) || !create_empty_atomic(paths->lab_flag)) {
        set_message(message, message_size, "创建实验后台启动标志时中断，请选择“恢复正常后台”。");
        return PTC_LAB_FLAG_RECOVERY_REQUIRED;
    }
    snprintf(journal.phase, sizeof(journal.phase), "lab_enabled");
    if (!write_journal(paths, &journal)) {
        set_message(message, message_size, "实验后台标志已创建，但事务记录未完成，请立即恢复。");
        return PTC_LAB_FLAG_RECOVERY_REQUIRED;
    }
    set_message(message, message_size, "实验后台已安全启用。请重启主机，再打开 Device Lab 浮窗。");
    return PTC_LAB_FLAG_OK;
}

PtcLabBootFlagResult ptc_lab_boot_flags_restore(const PtcLabBootFlagPaths *paths,
    char *message, size_t message_size)
{
    Journal journal;
    if (!paths || !journal_material_exists(paths) || !recover_journal(paths, &journal)) {
        set_message(message, message_size, "没有可信的事务记录，未更改任何启动标志。");
        return PTC_LAB_FLAG_CONFLICT;
    }
    if (strcmp(journal.phase, "restored") == 0) {
        bool exact = !exists(paths->lab_flag) && !exists(paths->standard_backup) &&
            (journal.standard_was_enabled ? exists(paths->standard_flag) : !exists(paths->standard_flag));
        set_message(message, message_size, exact ? "正常后台已经恢复。" :
            "恢复后启动标志又发生变化，为避免覆盖已停止操作。");
        return exact ? PTC_LAB_FLAG_ALREADY_DONE : PTC_LAB_FLAG_CONFLICT;
    }
    if (exists(paths->lab_flag)) {
        if (!empty_file(paths->lab_flag)) {
            set_message(message, message_size, "实验后台启动标志被外部修改，未删除该文件。");
            return PTC_LAB_FLAG_CONFLICT;
        }
        if (remove(paths->lab_flag) != 0) {
            set_message(message, message_size, "无法删除实验后台启动标志，请重试恢复。");
            return PTC_LAB_FLAG_IO_ERROR;
        }
        commit_sd();
    }
    snprintf(journal.phase, sizeof(journal.phase), "lab_disabled");
    if (!write_journal(paths, &journal)) {
        set_message(message, message_size, "实验后台已停用，但事务记录更新失败，请重试恢复。");
        return PTC_LAB_FLAG_RECOVERY_REQUIRED;
    }
    if (journal.standard_was_enabled) {
        if (exists(paths->standard_flag) && exists(paths->standard_backup)) {
            set_message(message, message_size, "正常后台标志和备份同时存在，为避免覆盖已停止操作。");
            return PTC_LAB_FLAG_CONFLICT;
        }
        if (!exists(paths->standard_flag)) {
            if (!exists(paths->standard_backup) || rename(paths->standard_backup, paths->standard_flag) != 0) {
                set_message(message, message_size, "正常后台启动标志尚未恢复完整，请再次重试。");
                return PTC_LAB_FLAG_RECOVERY_REQUIRED;
            }
            commit_sd();
        }
    } else if (exists(paths->standard_flag) || exists(paths->standard_backup)) {
        set_message(message, message_size, "发现意外的正常后台标志或备份，未覆盖任何文件。");
        return PTC_LAB_FLAG_CONFLICT;
    }
    snprintf(journal.phase, sizeof(journal.phase), "restored");
    if (!write_journal(paths, &journal)) {
        set_message(message, message_size, "启动标志已恢复，但事务记录收尾失败，请再次检查。");
        return PTC_LAB_FLAG_RECOVERY_REQUIRED;
    }
    set_message(message, message_size, "正常后台已精确恢复。请重启主机完成切换。");
    return PTC_LAB_FLAG_OK;
}
