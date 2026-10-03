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

/* Stable UI message keys; catalog rows are deliberately independent of source text. */
typedef enum {
#define PTC_UI_TEXT_KEY(key) key,
#include "ui_text_keys.inc"
#undef PTC_UI_TEXT_KEY
    PTC_UI_TEXT_COUNT
} PtcUiTextId;

typedef struct {
    const char *name;
    const char *text;
    int64_t number;
    bool is_number;
} PtcUiTextArg;

#define PTC_UI_TEXT_STRING(name_, value_) ((PtcUiTextArg){(name_), (value_), 0, false})
#define PTC_UI_TEXT_NUMBER(name_, value_) ((PtcUiTextArg){(name_), NULL, (int64_t)(value_), true})

const char *ptc_ui_text(PtcUiTextId id);
/* Allows file-scope UI descriptors to hold compile-time key names. */
#define PTC_UI_TEXT_REFERENCE(id) #id
const char *ptc_ui_text_resolve(const char *value);
const char *ptc_ui_weekday_label(unsigned weekday);
bool ptc_ui_text_format(PtcUiTextId id, char *out, size_t out_size,
                        const PtcUiTextArg *args, size_t arg_count);

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
