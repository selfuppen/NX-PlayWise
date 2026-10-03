#include "ui_language.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static PtcUiLanguagePreference g_resolved_language = PTC_UI_LANGUAGE_SIMPLIFIED;

#define PTC_UI_TEXT_VALUE(key, value) [key] = value,
static const char *const UI_TEXT_HANS[PTC_UI_TEXT_COUNT] = {
#include "ui_text_zh_hans.inc"
};
#undef PTC_UI_TEXT_VALUE
#define PTC_UI_TEXT_VALUE(key, value) [key] = value,
static const char *const UI_TEXT_HANT[PTC_UI_TEXT_COUNT] = {
#include "ui_text_zh_hant.inc"
};
#undef PTC_UI_TEXT_VALUE
#define PTC_UI_TEXT_VALUE(key, value) [key] = value,
static const char *const UI_TEXT_EN[PTC_UI_TEXT_COUNT] = {
#include "ui_text_en.inc"
};
#undef PTC_UI_TEXT_VALUE

#define PTC_UI_TEXT_KEY(key) #key,
static const char *const UI_TEXT_NAMES[PTC_UI_TEXT_COUNT] = {
#include "ui_text_keys.inc"
};
#undef PTC_UI_TEXT_KEY

const char *ptc_ui_text(PtcUiTextId id)
{
    if ((unsigned)id >= PTC_UI_TEXT_COUNT) return "[missing text]";
    switch (g_resolved_language) {
    case PTC_UI_LANGUAGE_TRADITIONAL: return UI_TEXT_HANT[id];
    case PTC_UI_LANGUAGE_ENGLISH: return UI_TEXT_EN[id];
    default: return UI_TEXT_HANS[id];
    }
}

const char *ptc_ui_text_resolve(const char *value)
{
    if (!value || strncmp(value, "PTC_UI_T_", 9) != 0) return value;
    for (size_t i = 0; i < PTC_UI_TEXT_COUNT; ++i)
        if (strcmp(value, UI_TEXT_NAMES[i]) == 0) return ptc_ui_text((PtcUiTextId)i);
    return "[missing text]";
}

const char *ptc_ui_weekday_label(unsigned weekday)
{
    static const PtcUiTextId DAYS[7] = {
        PTC_UI_T_WEEKDAY_SUN, PTC_UI_T_WEEKDAY_MON, PTC_UI_T_WEEKDAY_TUE,
        PTC_UI_T_WEEKDAY_WED, PTC_UI_T_WEEKDAY_THU, PTC_UI_T_WEEKDAY_FRI,
        PTC_UI_T_WEEKDAY_SAT
    };
    return ptc_ui_text(DAYS[weekday < 7 ? weekday : 0]);
}

static size_t utf8_width(const char *text)
{
    unsigned char lead = (unsigned char)*text;
    size_t width = 1;
    if ((lead & 0xf8) == 0xf0) width = 4;
    else if ((lead & 0xf0) == 0xe0) width = 3;
    else if ((lead & 0xe0) == 0xc0) width = 2;
    for (size_t i = 1; i < width; ++i)
        if (!text[i] || ((unsigned char)text[i] & 0xc0) != 0x80) return 1;
    return width;
}

static bool append_text(char *out, size_t out_size, size_t *used, const char *piece)
{
    while (*piece) {
        size_t width = utf8_width(piece);
        if (width >= out_size - *used) return false;
        memcpy(out + *used, piece, width);
        *used += width;
        out[*used] = '\0';
        piece += width;
    }
    return true;
}

bool ptc_ui_text_format(PtcUiTextId id, char *out, size_t out_size,
                        const PtcUiTextArg *args, size_t arg_count)
{
    const char *cursor = ptc_ui_text(id);
    size_t used = 0;
    if (!out || out_size == 0 || (arg_count && !args)) return false;
    out[0] = '\0';
    while (*cursor) {
        if (*cursor == '{') {
            const char *end = strchr(cursor + 1, '}');
            if (!end) return false;
            size_t name_length = (size_t)(end - cursor - 1);
            const PtcUiTextArg *match = NULL;
            for (size_t i = 0; i < arg_count; ++i) {
                if (args[i].name && strlen(args[i].name) == name_length &&
                    strncmp(args[i].name, cursor + 1, name_length) == 0) {
                    match = &args[i];
                    break;
                }
            }
            if (!match) return false;
            char number[32];
            if (match->is_number) snprintf(number, sizeof(number), "%lld", (long long)match->number);
            if (!append_text(out, out_size, &used,
                             match->is_number ? number : (match->text ? match->text : ""))) return false;
            cursor = end + 1;
        } else {
            size_t width = utf8_width(cursor);
            if (width >= out_size - used) return false;
            memcpy(out + used, cursor, width);
            used += width;
            cursor += width;
            out[used] = '\0';
        }
    }
    return true;
}

bool ptc_ui_language_parse_preference(const char *value, PtcUiLanguagePreference *out)
{
    if (!value || !out) return false;
    if (strcmp(value, "system") == 0) *out = PTC_UI_LANGUAGE_SYSTEM;
    else if (strcmp(value, "zh-Hans") == 0) *out = PTC_UI_LANGUAGE_SIMPLIFIED;
    else if (strcmp(value, "zh-Hant") == 0) *out = PTC_UI_LANGUAGE_TRADITIONAL;
    else if (strcmp(value, "en") == 0) *out = PTC_UI_LANGUAGE_ENGLISH;
    else return false;
    return true;
}

const char *ptc_ui_language_preference_name(PtcUiLanguagePreference preference)
{
    switch (preference) {
    case PTC_UI_LANGUAGE_SIMPLIFIED: return "zh-Hans";
    case PTC_UI_LANGUAGE_TRADITIONAL: return "zh-Hant";
    case PTC_UI_LANGUAGE_ENGLISH: return "en";
    default: return "system";
    }
}

const char *ptc_ui_language_preference_label(PtcUiLanguagePreference preference)
{
    switch (preference) {
    case PTC_UI_LANGUAGE_SIMPLIFIED: return ptc_ui_text(PTC_UI_T_SIMPLIFIED_CHINESE);
    case PTC_UI_LANGUAGE_TRADITIONAL: return ptc_ui_text(PTC_UI_T_TRADITIONAL_CHINESE);
    case PTC_UI_LANGUAGE_ENGLISH: return "English";
    default: return ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM);
    }
}

const char *ptc_ui_language_preference_sublabel(PtcUiLanguagePreference preference)
{
    switch (preference) {
    case PTC_UI_LANGUAGE_SIMPLIFIED: return ptc_ui_text(PTC_UI_T_ALWAYS_SHOW_SIMPLIFIED_CHINESE);
    case PTC_UI_LANGUAGE_TRADITIONAL: return ptc_ui_text(PTC_UI_T_ALWAYS_SHOW_TRADITIONAL_CHINESE);
    case PTC_UI_LANGUAGE_ENGLISH: return ptc_ui_text(PTC_UI_T_ALWAYS_USE_ENGLISH);
    default: return ptc_ui_text(PTC_UI_T_USE_SWITCH_SYSTEM_LANGUAGE);
    }
}

PtcUiLanguagePreference ptc_ui_language_resolve(
    PtcUiLanguagePreference preference, PtcUiSystemLanguage system_language)
{
    if (preference != PTC_UI_LANGUAGE_SYSTEM) return preference;
    switch (system_language) {
    case PTC_UI_SYSTEM_LANGUAGE_TRADITIONAL: return PTC_UI_LANGUAGE_TRADITIONAL;
    case PTC_UI_SYSTEM_LANGUAGE_ENGLISH: return PTC_UI_LANGUAGE_ENGLISH;
    default: return PTC_UI_LANGUAGE_SIMPLIFIED;
    }
}

void ptc_ui_language_set_resolved(PtcUiLanguagePreference language)
{
    switch (language) {
    case PTC_UI_LANGUAGE_TRADITIONAL:
        g_resolved_language = PTC_UI_LANGUAGE_TRADITIONAL;
        break;
    case PTC_UI_LANGUAGE_ENGLISH:
        g_resolved_language = PTC_UI_LANGUAGE_ENGLISH;
        break;
    default:
        g_resolved_language = PTC_UI_LANGUAGE_SIMPLIFIED;
        break;
    }
}

PtcUiLanguagePreference ptc_ui_language_get_resolved(void)
{
    return g_resolved_language;
}

const char *ptc_ui_localize(const char *source, char *buffer, size_t buffer_size)
{
    (void)buffer;
    (void)buffer_size;
    return ptc_ui_text_resolve(source ? source : "");
}

static const char *format_number(PtcUiTextId id, const char *name, int64_t number,
                                 char *out, size_t size)
{
    PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER(name, number)};
    if (!ptc_ui_text_format(id, out, size, args, 1) && out && size) out[0] = '\0';
    return out ? out : "";
}

const char *ptc_ui_format_minutes(int minutes, char *out, size_t size)
{
    return format_number(PTC_UI_T_SHORT_MINUTES, "minutes", minutes, out, size);
}

const char *ptc_ui_format_hours_minutes(int hours, int minutes, char *out, size_t size)
{
    if (hours <= 0) return ptc_ui_format_minutes(minutes, out, size);
    if (minutes <= 0) return format_number(PTC_UI_T_DURATION_HOURS_NAMED, "hours", hours, out, size);
    PtcUiTextArg args[] = {
        PTC_UI_TEXT_NUMBER("hours", hours), PTC_UI_TEXT_NUMBER("minutes", minutes)
    };
    if (!ptc_ui_text_format(PTC_UI_T_DURATION_HOURS_MINUTES_NAMED, out, size, args, 2) && out && size)
        out[0] = '\0';
    return out ? out : "";
}

const char *ptc_ui_format_time_ago(uint64_t seconds, char *out, size_t size)
{
    if (seconds < 60) return format_number(PTC_UI_T_AGO_SECONDS_NAMED, "seconds", (int64_t)seconds, out, size);
    if (seconds < 3600) return format_number(PTC_UI_T_AGO_MINUTES_NAMED, "minutes", (int64_t)(seconds / 60), out, size);
    return format_number(PTC_UI_T_AGO_HOURS_NAMED, "hours", (int64_t)(seconds / 3600), out, size);
}

const char *ptc_ui_format_actual_added(int minutes, char *out, size_t size)
{
    return format_number(PTC_UI_T_ACTUAL_ADDED_NAMED, "minutes", minutes, out, size);
}

const char *ptc_ui_format_daily_cap_applied(int minutes, char *out, size_t size)
{
    return format_number(PTC_UI_T_DAILY_CAP_APPLIED_NAMED, "minutes", minutes, out, size);
}

const char *ptc_ui_format_remaining_today(int minutes, char *out, size_t size)
{
    if (!out || size == 0) return "";
    if (minutes < 0) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_REMAINING_UNLIMITED_NAMED));
    else if (minutes == 0) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_REMAINING_ZERO_NAMED));
    else return ptc_ui_format_minutes(minutes, out, size);
    return out;
}

const char *ptc_ui_format_code_input_progress(unsigned entered, unsigned total, char *out, size_t size)
{
    PtcUiTextArg args[] = {
        PTC_UI_TEXT_NUMBER("entered", entered), PTC_UI_TEXT_NUMBER("total", total)
    };
    if (!ptc_ui_text_format(PTC_UI_T_CODE_INPUT_PROGRESS_NAMED, out, size, args, 2) && out && size)
        out[0] = '\0';
    return out ? out : "";
}
