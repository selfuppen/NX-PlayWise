#ifndef PLAYWISE_DEVICE_LAB_BOOT_FLAGS_H
#define PLAYWISE_DEVICE_LAB_BOOT_FLAGS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *standard_flag;
    const char *standard_backup;
    const char *lab_flag;
    const char *journal;
} PtcLabBootFlagPaths;

typedef enum {
    PTC_LAB_FLAG_OK = 0,
    PTC_LAB_FLAG_ALREADY_DONE = 1,
    PTC_LAB_FLAG_RECOVERY_REQUIRED = 2,
    PTC_LAB_FLAG_CONFLICT = 3,
    PTC_LAB_FLAG_IO_ERROR = 4
} PtcLabBootFlagResult;

typedef enum {
    PTC_LAB_BOOT_NORMAL = 0,
    PTC_LAB_BOOT_ENABLED = 1,
    PTC_LAB_BOOT_RESTORED = 2,
    PTC_LAB_BOOT_RECOVERY_REQUIRED = 3,
    PTC_LAB_BOOT_CONFLICT = 4
} PtcLabBootState;

typedef struct {
    PtcLabBootState state;
    bool standard_flag_present;
    bool standard_backup_present;
    bool lab_flag_present;
    bool journal_present;
    bool pending_journal_present;
    bool standard_was_enabled;
    char journal_phase[32];
} PtcLabBootStatus;

bool ptc_lab_boot_flags_inspect(const PtcLabBootFlagPaths *paths, PtcLabBootStatus *status);

PtcLabBootFlagResult ptc_lab_boot_flags_enable(const PtcLabBootFlagPaths *paths,
    char *message, size_t message_size);
PtcLabBootFlagResult ptc_lab_boot_flags_restore(const PtcLabBootFlagPaths *paths,
    char *message, size_t message_size);

#endif
