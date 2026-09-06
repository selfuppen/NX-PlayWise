#ifndef PLAYWISE_NRO_HOT_RELOAD_H
#define PLAYWISE_NRO_HOT_RELOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <switch.h>

#include "../hot_reload_guard.h"

typedef enum {
    PTC_HOT_RELOAD_UNKNOWN = 0,
    PTC_HOT_RELOAD_CURRENT = 1,
    PTC_HOT_RELOAD_PENDING = 2,
    PTC_HOT_RELOAD_UNAVAILABLE = 3,
    PTC_HOT_RELOAD_INCOMPLETE = 4,
    PTC_HOT_RELOAD_RECOVERY_REQUIRED = 5,
    PTC_HOT_RELOAD_RUNNING = 6,
    PTC_HOT_RELOAD_SUCCESS = 7
} PtcHotReloadStatus;

typedef enum {
    PTC_HOT_RELOAD_PHASE_IDLE = 0,
    PTC_HOT_RELOAD_PHASE_WAIT_ACK = 1,
    PTC_HOT_RELOAD_PHASE_NEED_IPC_CLOSE = 2,
    PTC_HOT_RELOAD_PHASE_WAIT_SOURCE_EXIT = 3,
    PTC_HOT_RELOAD_PHASE_WAIT_TARGET_READY = 4,
    PTC_HOT_RELOAD_PHASE_COMPLETE = 5,
    PTC_HOT_RELOAD_PHASE_FAILED = 6
} PtcHotReloadPhase;

typedef struct {
    PtcHotReloadStatus status;
    PtcHotReloadPhase phase;
    PtcHotReloadJournal journal;
    char source_boot_id[32];
    char detail[192];
    u64 deadline_tick;
    unsigned int absent_samples;
    unsigned int launch_attempts;
    bool pm_initialized;
    bool boot_disabled;
} PtcHotReloadController;

void ptc_hot_reload_init(PtcHotReloadController *controller);
void ptc_hot_reload_recover_startup(PtcHotReloadController *controller);
void ptc_hot_reload_inspect(PtcHotReloadController *controller);
bool ptc_hot_reload_begin(PtcHotReloadController *controller);
void ptc_hot_reload_confirm_ipc_closed(PtcHotReloadController *controller);
void ptc_hot_reload_tick(PtcHotReloadController *controller);
void ptc_hot_reload_exit(PtcHotReloadController *controller);
const char *ptc_hot_reload_status_label(PtcHotReloadStatus status);

#endif
