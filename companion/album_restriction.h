#ifndef PTC_COMPANION_ALBUM_RESTRICTION_H
#define PTC_COMPANION_ALBUM_RESTRICTION_H

#include <stdbool.h>
#include <stddef.h>

#include "../platform/storage.h"

#define PTC_ALBUM_OVERRIDE_PATH "sdmc:/atmosphere/config/override_config.ini"
#define PTC_ALBUM_PACKAGE_PATH "sdmc:/switch/.packages/More Menu/Photo Album/package.ini"
#define PTC_ALBUM_BACKUP_ROOT "sdmc:/switch/playwise/backups/album_restriction"
#define PTC_ALBUM_CONFIG_MAX_BYTES (64u * 1024u)

typedef enum {
    PTC_ALBUM_RESTRICTION_OFF = 0,
    PTC_ALBUM_RESTRICTION_CONFIGURED = 1,
    PTC_ALBUM_RESTRICTION_ANOMALY = 2,
    PTC_ALBUM_RESTRICTION_UNKNOWN = 3,
    PTC_ALBUM_RESTRICTION_EXTERNAL = 4
} PtcAlbumRestrictionState;

typedef struct {
    PtcAlbumRestrictionState state;
    bool backup_valid;
    bool restart_required;
    char detail[160];
} PtcAlbumRestrictionStatus;

bool ptc_album_restriction_transform_ini(const char *input, char *output, size_t output_size);
bool ptc_album_restriction_get_status(PtcStorage *storage, PtcAlbumRestrictionStatus *status);
bool ptc_album_restriction_enable(PtcStorage *storage, char *error, size_t error_size);
bool ptc_album_restriction_restore(PtcStorage *storage, bool force, char *error, size_t error_size);

#endif
