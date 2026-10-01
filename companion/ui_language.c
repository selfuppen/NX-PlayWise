#include "ui_language.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t source;
    uint32_t target;
} PtcUiCharacterPair;

/* This fixed table is generated from the Switch UI source corpus. It is kept
 * in the repository so device builds need no conversion library. */
static const PtcUiCharacterPair CHARACTER_MAP[] = {
#include "ui_language_map.inc"
};

typedef struct {
    const char *source;
    const char *target;
} PtcUiPhrasePair;

/* Context-sensitive Taiwanese terms take precedence over character mapping. */
static const PtcUiPhrasePair PHRASE_MAP[] = {
    {"配置文件", "設定檔"},
    {"系统设置", "系統設定"},
    {"时间计划", "時間計畫"},
    {"周计划", "週計畫"},
    {"界面", "介面"},
    {"浮窗", "浮動視窗"},
    {"设置", "設定"},
    {"计划", "計畫"},
    {"软件", "軟體"},
    {"文件", "檔案"},
    {"网络", "網路"},
    {"信息", "資訊"},
    {"视频", "影片"},
    {"默认", "預設"},
    {"保存", "儲存"},
    {"加载", "載入"},
    {"刷新", "重新整理"},
    {"支持", "支援"},
    {"分钟", "分鐘"}
};

/* Authoritative English static dictionary, sorted by UTF-8 bytes for bsearch. */
static const PtcUiPhrasePair ENGLISH_MAP[] = {
#include "ui_language_en.inc"
};

static PtcUiLanguagePreference g_resolved_language = PTC_UI_LANGUAGE_SIMPLIFIED;

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
    case PTC_UI_LANGUAGE_SIMPLIFIED: return "简体中文";
    case PTC_UI_LANGUAGE_TRADITIONAL: return "繁体中文";
    case PTC_UI_LANGUAGE_ENGLISH: return "English";
    default: return "跟随系统";
    }
}

const char *ptc_ui_language_preference_sublabel(PtcUiLanguagePreference preference)
{
    switch (preference) {
    case PTC_UI_LANGUAGE_SIMPLIFIED: return "始终显示简体";
    case PTC_UI_LANGUAGE_TRADITIONAL: return "始终显示繁体";
    case PTC_UI_LANGUAGE_ENGLISH: return "Always use English";
    default: return "使用 Switch 系统语言";
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

static int compare_phrase_pair(const void *key, const void *element)
{
    const char *source = (const char *)key;
    const PtcUiPhrasePair *pair = (const PtcUiPhrasePair *)element;
    return strcmp(source, pair->source);
}

static const char *lookup_english(const char *source)
{
    if (!source || !*source) return source;
    const PtcUiPhrasePair *match = (const PtcUiPhrasePair *)bsearch(
        source, ENGLISH_MAP, sizeof(ENGLISH_MAP) / sizeof(ENGLISH_MAP[0]),
        sizeof(ENGLISH_MAP[0]), compare_phrase_pair);
    return match ? match->target : NULL;
}

static uint32_t decode_utf8(const unsigned char **cursor)
{
    const unsigned char *s = *cursor;
    uint32_t codepoint;
    if (s[0] < 0x80) { *cursor += 1; return s[0]; }
    if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
        codepoint = ((uint32_t)(s[0] & 0x1f) << 6) | (s[1] & 0x3f);
        *cursor += 2;
        return codepoint;
    }
    if ((s[0] & 0xf0) == 0xe0 && s[1] && s[2] &&
        (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
        codepoint = ((uint32_t)(s[0] & 0x0f) << 12) |
            ((uint32_t)(s[1] & 0x3f) << 6) | (s[2] & 0x3f);
        *cursor += 3;
        return codepoint;
    }
    if ((s[0] & 0xf8) == 0xf0 && s[1] && s[2] && s[3] &&
        (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) {
        codepoint = ((uint32_t)(s[0] & 0x07) << 18) |
            ((uint32_t)(s[1] & 0x3f) << 12) | ((uint32_t)(s[2] & 0x3f) << 6) | (s[3] & 0x3f);
        *cursor += 4;
        return codepoint;
    }
    *cursor += 1;
    return '?';
}

static uint32_t traditional_character(uint32_t codepoint)
{
    size_t lo = 0, hi = sizeof(CHARACTER_MAP) / sizeof(CHARACTER_MAP[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (CHARACTER_MAP[mid].source < codepoint) lo = mid + 1;
        else hi = mid;
    }
    return lo < sizeof(CHARACTER_MAP) / sizeof(CHARACTER_MAP[0]) &&
        CHARACTER_MAP[lo].source == codepoint ? CHARACTER_MAP[lo].target : codepoint;
}

static size_t encode_utf8(uint32_t value, char output[4])
{
    if (value < 0x80) { output[0] = (char)value; return 1; }
    if (value < 0x800) {
        output[0] = (char)(0xc0 | (value >> 6));
        output[1] = (char)(0x80 | (value & 0x3f));
        return 2;
    }
    if (value < 0x10000) {
        output[0] = (char)(0xe0 | (value >> 12));
        output[1] = (char)(0x80 | ((value >> 6) & 0x3f));
        output[2] = (char)(0x80 | (value & 0x3f));
        return 3;
    }
    output[0] = (char)(0xf0 | (value >> 18));
    output[1] = (char)(0x80 | ((value >> 12) & 0x3f));
    output[2] = (char)(0x80 | ((value >> 6) & 0x3f));
    output[3] = (char)(0x80 | (value & 0x3f));
    return 4;
}

const char *ptc_ui_localize(const char *source, char *buffer, size_t buffer_size)
{
    const unsigned char *cursor;
    size_t used = 0;
    if (!source) return "";

    if (g_resolved_language == PTC_UI_LANGUAGE_ENGLISH) {
        const char *en = lookup_english(source);
        if (en) return en;
        return source;
    }

    if (g_resolved_language != PTC_UI_LANGUAGE_TRADITIONAL || !buffer || buffer_size == 0)
        return source;

    cursor = (const unsigned char *)source;
    while (*cursor) {
        const char *piece = NULL;
        char encoded[4];
        size_t length;
        for (size_t i = 0; i < sizeof(PHRASE_MAP) / sizeof(PHRASE_MAP[0]); ++i) {
            size_t source_length = strlen(PHRASE_MAP[i].source);
            if (strncmp((const char *)cursor, PHRASE_MAP[i].source, source_length) == 0) {
                piece = PHRASE_MAP[i].target;
                cursor += source_length;
                break;
            }
        }
        if (piece) {
            length = strlen(piece);
            if (used + length >= buffer_size) break;
            memcpy(buffer + used, piece, length);
        } else {
            uint32_t codepoint = traditional_character(decode_utf8(&cursor));
            length = encode_utf8(codepoint, encoded);
            if (used + length >= buffer_size) break;
            memcpy(buffer + used, encoded, length);
        }
        used += length;
    }
    buffer[used] = '\0';
    return buffer;
}

const char *ptc_ui_format_minutes(int minutes, char *out, size_t size)
{
    if (!out || size == 0) return "";
    if (g_resolved_language == PTC_UI_LANGUAGE_ENGLISH) {
        snprintf(out, size, "%d min", minutes);
    } else {
        snprintf(out, size, "%d 分钟", minutes);
    }
    return out;
}

const char *ptc_ui_format_hours_minutes(int hours, int minutes, char *out, size_t size)
{
    if (!out || size == 0) return "";
    if (g_resolved_language == PTC_UI_LANGUAGE_ENGLISH) {
        if (hours > 0 && minutes > 0)
            snprintf(out, size, "%d hr %d min", hours, minutes);
        else if (hours > 0)
            snprintf(out, size, "%d hr", hours);
        else
            snprintf(out, size, "%d min", minutes);
    } else {
        if (hours > 0 && minutes > 0)
            snprintf(out, size, "%d 小时 %d 分钟", hours, minutes);
        else if (hours > 0)
            snprintf(out, size, "%d 小时", hours);
        else
            snprintf(out, size, "%d 分钟", minutes);
    }
    return out;
}

const char *ptc_ui_format_time_ago(uint64_t seconds, char *out, size_t size)
{
    if (!out || size == 0) return "";
    if (g_resolved_language == PTC_UI_LANGUAGE_ENGLISH) {
        if (seconds < 60)
            snprintf(out, size, "%llu seconds ago", (unsigned long long)seconds);
        else if (seconds < 3600)
            snprintf(out, size, "%llu minutes ago", (unsigned long long)(seconds / 60));
        else
            snprintf(out, size, "%llu hours ago", (unsigned long long)(seconds / 3600));
    } else {
        if (seconds < 60)
            snprintf(out, size, "%llu 秒前", (unsigned long long)seconds);
        else if (seconds < 3600)
            snprintf(out, size, "%llu 分钟前", (unsigned long long)(seconds / 60));
        else
            snprintf(out, size, "%llu 小时前", (unsigned long long)(seconds / 3600));
    }
    return out;
}

const char *ptc_ui_format_actual_added(int minutes, char *out, size_t size)
{
    if (!out || size == 0) return "";
    if (g_resolved_language == PTC_UI_LANGUAGE_ENGLISH) {
        snprintf(out, size, "Actual time added: %d min", minutes);
    } else {
        snprintf(out, size, "本次增加 %d 分钟", minutes);
    }
    return out;
}

const char *ptc_ui_format_daily_cap_applied(int minutes, char *out, size_t size)
{
    if (!out || size == 0) return "";
    if (g_resolved_language == PTC_UI_LANGUAGE_ENGLISH) {
        snprintf(out, size, "Limited by daily cap: effectively +%d min", minutes);
    } else {
        snprintf(out, size, "受每日上限影响，实际增加 %d 分钟", minutes);
    }
    return out;
}

const char *ptc_ui_format_remaining_today(int minutes, char *out, size_t size)
{
    if (!out || size == 0) return "";
    if (g_resolved_language == PTC_UI_LANGUAGE_ENGLISH) {
        if (minutes < 0) snprintf(out, size, "No Limit");
        else if (minutes == 0) snprintf(out, size, "0 min (Limit reached)");
        else snprintf(out, size, "%d min", minutes);
    } else {
        if (minutes < 0) snprintf(out, size, "不限时");
        else if (minutes == 0) snprintf(out, size, "0 分钟 (已到限制)");
        else snprintf(out, size, "%d 分钟", minutes);
    }
    return out;
}

const char *ptc_ui_format_code_input_progress(unsigned entered, unsigned total, char *out, size_t size)
{
    if (!out || size == 0) return "";
    if (g_resolved_language == PTC_UI_LANGUAGE_ENGLISH) {
        snprintf(out, size, "Entered %u/%u digits", entered, total);
    } else {
        snprintf(out, size, "已输入 %u/%u 位", entered, total);
    }
    return out;
}
