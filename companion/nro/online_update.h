#ifndef PTC_NRO_ONLINE_UPDATE_H
#define PTC_NRO_ONLINE_UPDATE_H

#include "../../platform/update_fs.h"
#include <stdatomic.h>
#include <switch.h>

typedef struct {
    bool attempted, curl_result_available, http_status_available, fetched;
    bool metadata_valid_available, metadata_valid;
    bool os_errno_available, tls_verify_result_available;
    bool ca_import_attempted;
    uint32_t ca_import_result;
    unsigned transfers;
    unsigned long curl_result;
    long http_status;
    long os_errno, tls_verify_result;
    uint64_t received;
    const char *stage;
} PtcUpdateNetworkSource;

typedef struct {
    char observation_id[33], curl_version[64], tls_backend[64];
    uint64_t begin_ns;
    bool socket_attempted, nifm_attempted, curl_global_attempted;
    bool thread_error_available;
    uint32_t socket_result, nifm_result;
    uint32_t thread_error_result;
    unsigned long curl_global_result;
    const char *initialization_stage;
    const char *thread_error_stage;
    PtcUpdateNetworkSource sources[2];
} PtcUpdateNetworkEvidence;

#define PTC_ONLINE_JOB_CALENDAR 5

typedef struct {
    PtcUpdateFs fs;
    PtcUpdateOffer offer;
    PtcUpdateFeed feed;
    unsigned source; /* 0 unknown, 1 Gitee, 2 GitHub; consumed after worker join. */
    PtcUpdateJournal journal;
    PtcUpdateStatus status;
    unsigned long network_result;
    PtcUpdateNetworkEvidence network;
    int error;
    uint64_t received, total;
    bool stable_only, started, worker_live, backup_submitted;
    atomic_bool cancel, done;
    atomic_ullong progress;
    Thread worker;
    int job;
    PtcUpdateStatus calendar_status;
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

/* Jobs: check=1, download+validate=2, backup-program+install=3, rollback=4, calendar check=5. */
bool ptc_online_update_package(PtcOnlineUpdater *u);
bool ptc_online_update_verify_current(PtcOnlineUpdater *u);

#endif
