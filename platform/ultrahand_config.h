#ifndef PTC_ULTRAHAND_CONFIG_H
#define PTC_ULTRAHAND_CONFIG_H

#include "storage.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int notifications; /* -1 unknown, 0 off, 1 on; never means receiver present. */
    bool app_muted;    /* true if flags/notifications/playwise.flag exists. */
    char shortcut[96];
} PtcUltrahandConfig;

typedef enum {
    PTC_OVERLAY_MANAGER_MISSING = 0,
    PTC_OVERLAY_MANAGER_UNKNOWN = 1,
    PTC_OVERLAY_MANAGER_TESLA = 2,
    PTC_OVERLAY_MANAGER_ULTRAHAND = 3,
} PtcOverlayManagerKind;

typedef struct {
    bool valid;
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    char raw[24];
} PtcOverlayVersion;

typedef struct {
    PtcOverlayManagerKind manager;
    char manager_title[64];
    char manager_author[64];
    PtcOverlayVersion manager_version;
    bool loader_installed;
    bool loader_boot2;
    PtcOverlayVersion loader_version;
    bool playwise_overlay_installed;
    bool ultrahand_notify_supported;       /* Ultrahand >= 2.3.0 */
    int notifications;                     /* -1 unknown, 0 off, 1 on */
    bool notifications_implicit_default;   /* [ultrahand] present without key on Ultrahand >= 2.3.0 */
    bool app_muted;                        /* flags/notifications/playwise.flag present */
    char shortcut[96];
} PtcOverlayEnv;

typedef enum {
    PTC_ULTRAHAND_PREPARE_UNAVAILABLE = 0,
    PTC_ULTRAHAND_PREPARE_ALREADY_ON = 1,
    PTC_ULTRAHAND_PREPARE_SAVED = 2,
    PTC_ULTRAHAND_PREPARE_FAILED = 3,
} PtcUltrahandPrepare;

typedef struct {
    PtcOverlayEnv env;
    PtcUltrahandPrepare notify_prepare;
    char notify_action[32];
} PtcOverlayBootReport;

bool ptc_overlay_version_parse(const char *raw, PtcOverlayVersion *out);
bool ptc_overlay_version_at_least(const PtcOverlayVersion *v,
                                  uint16_t major, uint16_t minor, uint16_t patch);
void ptc_overlay_env_probe(PtcStorage *storage, PtcOverlayEnv *out);
void ptc_overlay_env_format_manager(const PtcOverlayEnv *env, char *out, size_t size);

void ptc_ultrahand_config_read(PtcStorage *storage, PtcUltrahandConfig *out);
bool ptc_ultrahand_combo(const char *text, char *out, size_t size);

/* User enable action or strict boot reconcile. Changes one existing, unambiguous key
   and reads back. SAVED/ALREADY_ON prove SD configuration, never a resident receiver ACK. */
PtcUltrahandPrepare ptc_ultrahand_prepare_notifications(PtcStorage *storage);

/* Sysmodule boot-time dependency check and conservative auto-reconcile.
   Never modifies third-party files when notifications_enabled is false or when
   ovlmenu.ovl/nx-ovlloader do not strictly satisfy the verified Ultrahand >= 2.3.0 baseline.
   Writes bounded diagnostic snapshot to <root>/support/overlay-env.json. */
void ptc_overlay_env_boot_reconcile(PtcStorage *storage, const char *root,
                                    bool notifications_enabled,
                                    PtcOverlayBootReport *out);

#ifdef __cplusplus
}
#endif

#endif
