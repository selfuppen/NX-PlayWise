#ifndef PLAYWISE_COMPANION_UI_LANGUAGE_H
#define PLAYWISE_COMPANION_UI_LANGUAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PTC_UI_LANGUAGE_SYSTEM = 0,      /* 跟随系统 / Follow System */
    PTC_UI_LANGUAGE_SIMPLIFIED = 1,  /* 简体中文 / Simplified Chinese */
    PTC_UI_LANGUAGE_TRADITIONAL = 2, /* 繁体中文 / Traditional Chinese */
    PTC_UI_LANGUAGE_ENGLISH = 3      /* English */
} PtcUiLanguagePreference;

typedef enum {
    PTC_UI_SYSTEM_LANGUAGE_UNKNOWN = 0,
    PTC_UI_SYSTEM_LANGUAGE_SIMPLIFIED = 1,
    PTC_UI_SYSTEM_LANGUAGE_TRADITIONAL = 2,
    PTC_UI_SYSTEM_LANGUAGE_ENGLISH = 3
} PtcUiSystemLanguage;

bool ptc_ui_language_parse_preference(const char *value, PtcUiLanguagePreference *out);
const char *ptc_ui_language_preference_name(PtcUiLanguagePreference preference);
const char *ptc_ui_language_preference_label(PtcUiLanguagePreference preference);
const char *ptc_ui_language_preference_sublabel(PtcUiLanguagePreference preference);
PtcUiLanguagePreference ptc_ui_language_resolve(
    PtcUiLanguagePreference preference, PtcUiSystemLanguage system_language);
void ptc_ui_language_set_resolved(PtcUiLanguagePreference language);
PtcUiLanguagePreference ptc_ui_language_get_resolved(void);
const char *ptc_ui_localize(const char *source, char *buffer, size_t buffer_size);

/* Parameterized formatting helpers avoiding hardcoded language sentence structure */
const char *ptc_ui_format_minutes(int minutes, char *out, size_t size);
const char *ptc_ui_format_hours_minutes(int hours, int minutes, char *out, size_t size);
const char *ptc_ui_format_time_ago(uint64_t seconds, char *out, size_t size);
const char *ptc_ui_format_actual_added(int minutes, char *out, size_t size);
const char *ptc_ui_format_daily_cap_applied(int minutes, char *out, size_t size);
const char *ptc_ui_format_remaining_today(int minutes, char *out, size_t size);
const char *ptc_ui_format_code_input_progress(unsigned entered, unsigned total, char *out, size_t size);

#endif
