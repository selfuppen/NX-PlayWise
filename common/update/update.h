#ifndef PTC_ONLINE_UPDATE_H
#define PTC_ONLINE_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../third_party/cjson/cJSON.h"

#define PTC_UPDATE_REPO_URL "https://download.nswiki.cn/gayking/store/repo.json"
#define PTC_UPDATE_ZIP_URL "https://download.nswiki.cn/gayking/store/zips/NX-PlayWise.zip"
#define PTC_UPDATE_METADATA_LIMIT (2u * 1024u * 1024u)
#define PTC_UPDATE_ZIP_LIMIT (32u * 1024u * 1024u)
#define PTC_UPDATE_EXTRACT_LIMIT (64u * 1024u * 1024u)
#define PTC_UPDATE_FILE_COUNT 13
#define PTC_UPDATE_JOURNAL_LIMIT 12288

typedef struct {
    char version[64];
    char md5[33];
    char notes[2048];
    unsigned size_kib;
} PtcUpdateOffer;

typedef enum {
    PTC_UPDATE_IDLE, PTC_UPDATE_CHECKING, PTC_UPDATE_CURRENT, PTC_UPDATE_AVAILABLE,
    PTC_UPDATE_LOCAL_NEWER, PTC_UPDATE_CHANNEL_EXCLUDED, PTC_UPDATE_DOWNLOADING,
    PTC_UPDATE_VERIFYING, PTC_UPDATE_BACKUP_PENDING, PTC_UPDATE_INSTALLING,
    PTC_UPDATE_AWAITING_REOPEN, PTC_UPDATE_COMPLETED, PTC_UPDATE_FAILED,
    PTC_UPDATE_CANCELED, PTC_UPDATE_RECOVERY_REQUIRED, PTC_UPDATE_MANUAL_REQUIRED,
    PTC_UPDATE_BACKEND_PENDING, PTC_UPDATE_RECONFIRM, PTC_UPDATE_BACKUP_UNKNOWN
} PtcUpdateStatus;

typedef enum {
    PTC_UPDATE_ERROR_NONE, PTC_UPDATE_ERROR_NETWORK, PTC_UPDATE_ERROR_MANUAL,
    PTC_UPDATE_ERROR_PACKAGE, PTC_UPDATE_ERROR_STORAGE, PTC_UPDATE_ERROR_BACKUP,
    PTC_UPDATE_ERROR_CONFLICT
} PtcUpdateError;

extern const char *const ptc_update_files[PTC_UPDATE_FILE_COUNT];
bool ptc_update_version_compare(const char *left, const char *right, int *order);
bool ptc_update_version_prerelease(const char *version);
cJSON *ptc_update_parse_json(const char *text);
bool ptc_update_offer_parse(const char *text, PtcUpdateOffer *offer);
PtcUpdateStatus ptc_update_offer_status(const PtcUpdateOffer *offer, const char *local, bool stable_only);
bool ptc_update_url_allowed(const char *url);
bool ptc_update_hex(const char *text, size_t digits);
bool ptc_update_request_id(const char *text);
/* Returns a runtime asset index; descriptions and directories are never assets. */
int ptc_update_asset_index(const char *path);
bool ptc_update_archive_path(const char *path, bool directory);

#endif
