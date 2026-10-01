#ifndef PLAYWISE_COMPANION_UI_LANGUAGE_H
#define PLAYWISE_COMPANION_UI_LANGUAGE_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PTC_UI_LANGUAGE_SYSTEM = 0,
    PTC_UI_LANGUAGE_SIMPLIFIED = 1,
    PTC_UI_LANGUAGE_TRADITIONAL = 2
} PtcUiLanguagePreference;

typedef enum {
    PTC_UI_SYSTEM_LANGUAGE_UNKNOWN = 0,
    PTC_UI_SYSTEM_LANGUAGE_SIMPLIFIED = 1,
    PTC_UI_SYSTEM_LANGUAGE_TRADITIONAL = 2
} PtcUiSystemLanguage;

bool ptc_ui_language_parse_preference(const char *value, PtcUiLanguagePreference *out);
const char *ptc_ui_language_preference_name(PtcUiLanguagePreference preference);
const char *ptc_ui_language_preference_label(PtcUiLanguagePreference preference);
PtcUiLanguagePreference ptc_ui_language_resolve(
    PtcUiLanguagePreference preference, PtcUiSystemLanguage system_language);
void ptc_ui_language_set_resolved(PtcUiLanguagePreference language);
PtcUiLanguagePreference ptc_ui_language_get_resolved(void);
const char *ptc_ui_localize(const char *source, char *buffer, size_t buffer_size);

#endif
