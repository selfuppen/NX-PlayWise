#ifndef PTC_SYSMODULE_CORE_H
#define PTC_SYSMODULE_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "../platform/pctl.h"
#include "../platform/storage.h"
#include "../platform/time_provider.h"

typedef struct {
    char app_root[96];
    PtcStorage *storage;
    PtcPctl *pctl;
    PtcTimeProvider *time_provider;
    uint32_t scan_backoff_ms;
    uint16_t last_minute_day_index;
    uint16_t last_minute_of_day;
    uint16_t last_cleanup_day_index;
    bool minute_initialized;
    bool cleanup_initialized;
    bool startup_recovery_checked;
    bool disable_initialized;
    bool disable_present;
    char boot_id[24];
    char config_cache_text[4096];
    char rules_cache_text[4096];
    char state_cache_text[1024];
    char capabilities_cache_text[1024];
    PtcStorageMetadata config_meta;
    PtcStorageMetadata rules_meta;
    PtcStorageMetadata state_meta;
    PtcStorageMetadata capabilities_meta;
    bool config_cache_valid;
    bool rules_cache_valid;
    bool state_cache_valid;
    bool capabilities_cache_valid;
} PtcSysmodule;

void ptc_sysmodule_init(
    PtcSysmodule *sysmodule,
    const char *app_root,
    PtcStorage *storage,
    PtcPctl *pctl,
    PtcTimeProvider *time_provider);
void ptc_sysmodule_set_boot_id(PtcSysmodule *sysmodule, const char *boot_id);
int ptc_sysmodule_recover_processing(PtcSysmodule *sysmodule);
int ptc_sysmodule_bootstrap_setup(PtcSysmodule *sysmodule);
int ptc_sysmodule_process_all(PtcSysmodule *sysmodule);
int ptc_sysmodule_enforce_tick(PtcSysmodule *sysmodule);
uint32_t ptc_sysmodule_note_scan_result(PtcSysmodule *sysmodule, bool found_work);
uint32_t ptc_sysmodule_current_scan_interval(const PtcSysmodule *sysmodule);
uint32_t ptc_sysmodule_next_wait_ms(PtcSysmodule *sysmodule);
int ptc_sysmodule_scheduler_tick(PtcSysmodule *sysmodule, bool storage_notified);
int ptc_sysmodule_cleanup(PtcSysmodule *sysmodule);
int ptc_sysmodule_rollover_legacy_logs(PtcSysmodule *sysmodule);
bool ptc_sysmodule_refresh_caches(PtcSysmodule *sysmodule);

#endif
