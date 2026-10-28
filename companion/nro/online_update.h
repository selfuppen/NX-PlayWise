#ifndef PTC_NRO_ONLINE_UPDATE_H
#define PTC_NRO_ONLINE_UPDATE_H

#include "../../platform/update_fs.h"
#include <stdatomic.h>
#include <switch.h>

typedef struct {
    PtcUpdateFs fs;
    PtcUpdateOffer offer;
    PtcUpdateJournal journal;
    PtcUpdateStatus status;
    unsigned long network_result;
    int error;
    uint64_t received, total;
    bool stable_only, started, worker_live, backup_submitted;
    atomic_bool cancel, done;
    atomic_ullong progress;
    Thread worker;
    int job;
    char installed_version[64];
} PtcOnlineUpdater;

void ptc_online_update_init(PtcOnlineUpdater *u, PtcStorage *storage);
void ptc_online_update_init_at(PtcOnlineUpdater *u, PtcStorage *storage, const char *sd_root);
bool ptc_online_update_start(PtcOnlineUpdater *u, int job);
bool ptc_online_update_poll(PtcOnlineUpdater *u);
void ptc_online_update_exit(PtcOnlineUpdater *u);
bool ptc_online_update_source(PtcOnlineUpdater *u);
bool ptc_online_update_can_recover(PtcOnlineUpdater *u);
bool ptc_online_update_complete(PtcOnlineUpdater *u, const char *request_id, const char *receipt);
bool ptc_online_update_record(PtcOnlineUpdater *u, const char *state);
bool ptc_online_update_pending_backup(PtcOnlineUpdater *u, bool save);

/* Jobs: check=1, download+validate=2, backup-program+install=3, rollback=4. */
bool ptc_online_update_package(PtcOnlineUpdater *u);
bool ptc_online_update_verify_current(PtcOnlineUpdater *u);

#endif
