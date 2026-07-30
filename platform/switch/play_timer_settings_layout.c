#include "play_timer_settings_layout.h"

#include <stdio.h>

static size_t day_base(uint8_t weekday)
{
    return PTC_PLAY_TIMER_HEADER_WORDS + ((size_t)weekday * PTC_PLAY_TIMER_DAY_WORDS);
}

bool ptc_play_timer_settings_valid(const uint16_t *words, size_t word_count)
{
    unsigned int day;
    if (!words || word_count != PTC_PLAY_TIMER_SETTINGS_WORDS) {
        return false;
    }
    for (day = 0; day < PTC_PLAY_TIMER_DAY_COUNT; ++day) {
        size_t base = day_base((uint8_t)day);
        uint16_t flag = words[base + PTC_PLAY_TIMER_DAY_FLAG_WORD];
        uint16_t enabled = words[base + PTC_PLAY_TIMER_DAY_ENABLE_WORD];
        uint16_t minutes = words[base + PTC_PLAY_TIMER_DAY_MINUTES_WORD];
        if ((flag != 0U && flag != PTC_PLAY_TIMER_DAY_CONFIGURED) ||
            (enabled != 0U && enabled != PTC_PLAY_TIMER_DAY_RESTRICTED)) {
            return false;
        }
        if (minutes > PTC_PLAY_TIMER_MAX_LIMIT_MINUTES && minutes != PTC_PLAY_TIMER_UNLIMITED) {
            return false;
        }
    }
    return true;
}

bool ptc_play_timer_settings_get_minutes(
    const uint16_t *words,
    size_t word_count,
    uint8_t weekday,
    uint16_t *minutes)
{
    if (!minutes || weekday >= PTC_PLAY_TIMER_DAY_COUNT ||
        !ptc_play_timer_settings_valid(words, word_count)) {
        return false;
    }
    *minutes = words[day_base(weekday) + PTC_PLAY_TIMER_DAY_MINUTES_WORD];
    return true;
}

bool ptc_play_timer_settings_set_day(
    uint16_t *words,
    size_t word_count,
    uint8_t weekday,
    bool restricted,
    uint16_t minutes)
{
    size_t base;
    if (weekday >= PTC_PLAY_TIMER_DAY_COUNT ||
        !ptc_play_timer_settings_valid(words, word_count) ||
        (restricted && minutes > PTC_PLAY_TIMER_MAX_LIMIT_MINUTES) ||
        (!restricted && minutes != PTC_PLAY_TIMER_UNLIMITED)) {
        return false;
    }
    base = day_base(weekday);
    if (restricted) {
        words[0] = 0x0101U;
        words[1] = 0x0001U;
        words[base + PTC_PLAY_TIMER_DAY_FLAG_WORD] = PTC_PLAY_TIMER_DAY_CONFIGURED;
        words[base + PTC_PLAY_TIMER_DAY_ENABLE_WORD] = PTC_PLAY_TIMER_DAY_RESTRICTED;
    } else {
        words[base + PTC_PLAY_TIMER_DAY_FLAG_WORD] = 0U;
        words[base + PTC_PLAY_TIMER_DAY_ENABLE_WORD] = 0U;
    }
    words[base + PTC_PLAY_TIMER_DAY_MINUTES_WORD] = minutes;
    return true;
}

void ptc_play_timer_settings_hex(
    char *out,
    size_t out_size,
    const uint16_t *words,
    size_t word_count)
{
    size_t used = 0;
    size_t i;
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!words || word_count != PTC_PLAY_TIMER_SETTINGS_WORDS) {
        return;
    }
    for (i = 0; i < word_count && used + 5 < out_size; ++i) {
        int written = snprintf(out + used, out_size - used, "%04x", (unsigned int)words[i]);
        if (written < 0 || (size_t)written >= out_size - used) {
            break;
        }
        used += (size_t)written;
    }
}

void ptc_play_timer_settings_summary(
    char *out,
    size_t out_size,
    const uint16_t *words,
    size_t word_count)
{
    size_t used = 0;
    unsigned int day;
    int written;
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!words || word_count != PTC_PLAY_TIMER_SETTINGS_WORDS) {
        return;
    }
    written = snprintf(
        out,
        out_size,
        "h:%u,%u,%u,%u,%u,%u,%u",
        (unsigned int)words[0],
        (unsigned int)words[1],
        (unsigned int)words[2],
        (unsigned int)words[3],
        (unsigned int)words[4],
        (unsigned int)words[5],
        (unsigned int)words[6]);
    if (written < 0 || (size_t)written >= out_size) {
        return;
    }
    used = (size_t)written;
    for (day = 0; day < PTC_PLAY_TIMER_DAY_COUNT; ++day) {
        size_t base = day_base((uint8_t)day);
        written = snprintf(
            out + used,
            out_size - used,
            ";d%u:flag=%u,enabled=%u,m=%u",
            day,
            (unsigned int)words[base + PTC_PLAY_TIMER_DAY_FLAG_WORD],
            (unsigned int)words[base + PTC_PLAY_TIMER_DAY_ENABLE_WORD],
            (unsigned int)words[base + PTC_PLAY_TIMER_DAY_MINUTES_WORD]);
        if (written < 0 || (size_t)written >= out_size - used) {
            break;
        }
        used += (size_t)written;
    }
}
