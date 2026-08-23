#include "boot_flags.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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

static bool write_journal(const PtcLabBootFlagPaths *paths, const Journal *journal)
{
    char temporary[512];
    FILE *file;
    snprintf(temporary, sizeof(temporary), "%s.tmp", paths->journal);
    file = fopen(temporary, "wb");
    if (!file) return false;
    if (fprintf(file, "{\"version\":1,\"phase\":\"%s\",\"standard_was_enabled\":%s}\n",
            journal->phase, journal->standard_was_enabled ? "true" : "false") < 0) {
        (void)fclose(file);
        (void)remove(temporary);
        return false;
    }
    if (fclose(file) != 0) {
        (void)remove(temporary);
        return false;
    }
    if (rename(temporary, paths->journal) != 0) {
        (void)remove(temporary);
        return false;
    }
    return true;
}

static bool read_journal(const PtcLabBootFlagPaths *paths, Journal *journal)
{
    char text[256];
    char *phase;
    char *end;
    FILE *file = fopen(paths->journal, "rb");
    size_t got;
    if (!file) return false;
    got = fread(text, 1, sizeof(text) - 1U, file);
    if (fclose(file) != 0 || got == 0 || got >= sizeof(text) - 1U) return false;
    text[got] = '\0';
    phase = strstr(text, "\"phase\":\"");
    if (!phase) return false;
    phase += strlen("\"phase\":\"");
    end = strchr(phase, '\"');
    if (!end || (size_t)(end - phase) >= sizeof(journal->phase)) return false;
    memcpy(journal->phase, phase, (size_t)(end - phase));
    journal->phase[end - phase] = '\0';
    if (strstr(text, "\"standard_was_enabled\":true")) journal->standard_was_enabled = true;
    else if (strstr(text, "\"standard_was_enabled\":false")) journal->standard_was_enabled = false;
    else return false;
    return true;
}

static bool create_empty_atomic(const char *path)
{
    char temporary[512];
    FILE *file;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (exists(temporary)) return false;
    file = fopen(temporary, "wb");
    if (!file) return false;
    if (fclose(file) != 0) { (void)remove(temporary); return false; }
    if (rename(temporary, path) != 0) { (void)remove(temporary); return false; }
    return true;
}

PtcLabBootFlagResult ptc_lab_boot_flags_enable(const PtcLabBootFlagPaths *paths,
    char *message, size_t message_size)
{
    Journal journal;
    bool standard;
    if (!paths || !paths->standard_flag || !paths->standard_backup || !paths->lab_flag || !paths->journal) {
        set_message(message, message_size, "Invalid flag paths.");
        return PTC_LAB_FLAG_IO_ERROR;
    }
    if (exists(paths->journal)) {
        if (!read_journal(paths, &journal)) {
            set_message(message, message_size, "Journal unreadable. Choose Restore normal package.");
            return PTC_LAB_FLAG_RECOVERY_REQUIRED;
        }
        if (strcmp(journal.phase, "lab_enabled") == 0 && empty_file(paths->lab_flag) &&
            ((journal.standard_was_enabled && !exists(paths->standard_flag) && exists(paths->standard_backup)) ||
             (!journal.standard_was_enabled && !exists(paths->standard_flag) && !exists(paths->standard_backup)))) {
            set_message(message, message_size, "Lab already enabled. Reboot the console.");
            return PTC_LAB_FLAG_ALREADY_DONE;
        }
        if (strcmp(journal.phase, "restored") == 0 && !exists(paths->lab_flag) && !exists(paths->standard_backup) &&
            (journal.standard_was_enabled ? exists(paths->standard_flag) : !exists(paths->standard_flag))) {
            if (remove(paths->journal) != 0) {
                set_message(message, message_size, "Could not rotate completed journal; nothing changed.");
                return PTC_LAB_FLAG_IO_ERROR;
            }
        } else {
            set_message(message, message_size, "Interrupted/conflicting switch. Choose Restore normal package.");
            return PTC_LAB_FLAG_RECOVERY_REQUIRED;
        }
    }
    if (exists(paths->standard_backup) || exists(paths->lab_flag)) {
        set_message(message, message_size, "Unknown flag or backup exists; nothing was overwritten.");
        return PTC_LAB_FLAG_CONFLICT;
    }
    memset(&journal, 0, sizeof(journal));
    standard = exists(paths->standard_flag);
    journal.standard_was_enabled = standard;
    snprintf(journal.phase, sizeof(journal.phase), "prepared");
    if (!write_journal(paths, &journal)) {
        set_message(message, message_size, "Could not create transaction journal.");
        return PTC_LAB_FLAG_IO_ERROR;
    }
    if (standard && rename(paths->standard_flag, paths->standard_backup) != 0) {
        set_message(message, message_size, "Could not preserve standard flag. Restore is required.");
        return PTC_LAB_FLAG_RECOVERY_REQUIRED;
    }
    snprintf(journal.phase, sizeof(journal.phase), "standard_disabled");
    if (!write_journal(paths, &journal) || !create_empty_atomic(paths->lab_flag)) {
        set_message(message, message_size, "Lab flag creation interrupted. Choose Restore normal package.");
        return PTC_LAB_FLAG_RECOVERY_REQUIRED;
    }
    snprintf(journal.phase, sizeof(journal.phase), "lab_enabled");
    if (!write_journal(paths, &journal)) {
        set_message(message, message_size, "Lab flag exists but journal finalization failed. Restore required.");
        return PTC_LAB_FLAG_RECOVERY_REQUIRED;
    }
    set_message(message, message_size, "Lab enabled safely. Reboot, then open the Device Lab Overlay.");
    return PTC_LAB_FLAG_OK;
}

PtcLabBootFlagResult ptc_lab_boot_flags_restore(const PtcLabBootFlagPaths *paths,
    char *message, size_t message_size)
{
    Journal journal;
    if (!paths || !exists(paths->journal) || !read_journal(paths, &journal)) {
        set_message(message, message_size, "No trustworthy journal; no flags were changed.");
        return PTC_LAB_FLAG_CONFLICT;
    }
    if (strcmp(journal.phase, "restored") == 0) {
        bool exact = !exists(paths->lab_flag) && !exists(paths->standard_backup) &&
            (journal.standard_was_enabled ? exists(paths->standard_flag) : !exists(paths->standard_flag));
        set_message(message, message_size, exact ? "Normal package already restored." :
            "Flags changed after restore; nothing was overwritten.");
        return exact ? PTC_LAB_FLAG_ALREADY_DONE : PTC_LAB_FLAG_CONFLICT;
    }
    if (exists(paths->lab_flag)) {
        if (!empty_file(paths->lab_flag)) {
            set_message(message, message_size, "Lab flag externally modified; nothing was removed.");
            return PTC_LAB_FLAG_CONFLICT;
        }
        if (remove(paths->lab_flag) != 0) {
            set_message(message, message_size, "Could not remove Lab boot flag.");
            return PTC_LAB_FLAG_IO_ERROR;
        }
    }
    snprintf(journal.phase, sizeof(journal.phase), "lab_disabled");
    if (!write_journal(paths, &journal)) {
        set_message(message, message_size, "Lab disabled; journal update failed. Retry restore.");
        return PTC_LAB_FLAG_RECOVERY_REQUIRED;
    }
    if (journal.standard_was_enabled) {
        if (exists(paths->standard_flag) && exists(paths->standard_backup)) {
            set_message(message, message_size, "Standard flag and backup both exist; nothing overwritten.");
            return PTC_LAB_FLAG_CONFLICT;
        }
        if (!exists(paths->standard_flag)) {
            if (!exists(paths->standard_backup) || rename(paths->standard_backup, paths->standard_flag) != 0) {
                set_message(message, message_size, "Standard flag restore incomplete. Retry restore.");
                return PTC_LAB_FLAG_RECOVERY_REQUIRED;
            }
        }
    } else if (exists(paths->standard_flag) || exists(paths->standard_backup)) {
        set_message(message, message_size, "Unexpected standard flag/backup; nothing overwritten.");
        return PTC_LAB_FLAG_CONFLICT;
    }
    snprintf(journal.phase, sizeof(journal.phase), "restored");
    if (!write_journal(paths, &journal)) {
        set_message(message, message_size, "Flags restored; journal finalization failed.");
        return PTC_LAB_FLAG_RECOVERY_REQUIRED;
    }
    set_message(message, message_size, "Normal package restored exactly. Reboot the console.");
    return PTC_LAB_FLAG_OK;
}
