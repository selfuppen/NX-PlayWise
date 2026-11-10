#ifndef PTC_REASON_NOTIFICATION_H
#define PTC_REASON_NOTIFICATION_H
#include "../common/notification/reason_event.h"
#include "../platform/storage.h"
#include "../platform/ultrahand_config.h"
#include "../companion/ui_language.h"

typedef struct {
    PtcReasonEvent event;
    PtcStorage *storage;
    char root[128];
    char journal[256];
    char path[256];
    char payload[1536];
    uint64_t sequence;
    uint64_t expires_ms;
    uint64_t retry_ms;
    uint64_t config_ms;
    uint64_t io_max_ms;
    unsigned failures;
    bool initialized;
    bool blocked;
    bool pending;
    PtcUltrahandConfig external;
    char state[40];
} PtcReasonNotification;

void ptc_reason_notification_init(PtcReasonNotification *n, PtcStorage *s,
    const char *root, const char *release, const char *boot);
void ptc_reason_notification_preferences(const char *config, bool *enabled,
    PtcUiLanguagePreference *language);
/* Call outside the control/storage transaction lock, after copying the sample.
   This side channel has no PCTL or request client dependency. */
void ptc_reason_notification_tick(PtcReasonNotification *n, const PtcReasonObservation *sample,
    uint64_t ms, int64_t wall, bool enabled, bool suppressed, bool stopping,
    PtcUiLanguagePreference language);
#endif
