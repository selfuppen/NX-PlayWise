#ifndef PTC_NOTIFICATION_PREFERENCES_H
#define PTC_NOTIFICATION_PREFERENCES_H

#include "ui_language.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The language projection is optional. Invalid or missing JSON disables reminders. */
bool ptc_reason_notification_preferences(const char *config, bool *enabled,
    PtcUiLanguagePreference *language);

#ifdef __cplusplus
}
#endif
#endif
