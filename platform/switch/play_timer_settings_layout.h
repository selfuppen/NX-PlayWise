#ifndef PTC_SWITCH_PLAY_TIMER_SETTINGS_LAYOUT_H
#define PTC_SWITCH_PLAY_TIMER_SETTINGS_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PTC_PLAY_TIMER_SETTINGS_WORDS 34U
#define PTC_PLAY_TIMER_HEADER_WORDS 7U
#define PTC_PLAY_TIMER_DAY_COUNT 7U
#define PTC_PLAY_TIMER_DAY_WORDS 4U
#define PTC_PLAY_TIMER_DAY_FLAG_WORD 0U
#define PTC_PLAY_TIMER_DAY_ENABLE_WORD 1U
#define PTC_PLAY_TIMER_DAY_MINUTES_WORD 2U
#define PTC_PLAY_TIMER_DAY_CONFIGURED 0x0600U
#define PTC_PLAY_TIMER_DAY_RESTRICTED 0x0100U
#define PTC_PLAY_TIMER_MAX_LIMIT_MINUTES 1440U
#define PTC_PLAY_TIMER_UNLIMITED 0xffffU

typedef enum {
    PTC_PLAY_TIMER_DAY_MODE_UNKNOWN = 0,
    PTC_PLAY_TIMER_DAY_MODE_LIMIT = 1,
    PTC_PLAY_TIMER_DAY_MODE_UNLIMITED = 2,
    PTC_PLAY_TIMER_DAY_MODE_BLOCKED = 3
} PtcPlayTimerDayMode;

bool ptc_play_timer_settings_valid(const uint16_t *words, size_t word_count);
bool ptc_play_timer_settings_get_minutes(
    const uint16_t *words,
    size_t word_count,
    uint8_t weekday,
    uint16_t *minutes);
bool ptc_play_timer_settings_get_day(
    const uint16_t *words,
    size_t word_count,
    uint8_t weekday,
    bool *restricted,
    uint16_t *minutes);
/* True only when the weekday's day block is cleanly empty (flag/enable/minutes
   all zero): Nintendo has no play-timer configuration for that day. This is a
   distinct, benign "no limit configured" state, not the 0xffff unlimited
   sentinel and not a partially-written/garbage slot. */
bool ptc_play_timer_settings_day_is_unconfigured(
    const uint16_t *words,
    size_t word_count,
    uint8_t weekday);
bool ptc_play_timer_settings_get_mode(
    const uint16_t *words,
    size_t word_count,
    uint8_t weekday,
    PtcPlayTimerDayMode *mode,
    uint16_t *minutes);
bool ptc_play_timer_settings_set_day(
    uint16_t *words,
    size_t word_count,
    uint8_t weekday,
    bool restricted,
    uint16_t minutes);
void ptc_play_timer_settings_hex(
    char *out,
    size_t out_size,
    const uint16_t *words,
    size_t word_count);
void ptc_play_timer_settings_summary(
    char *out,
    size_t out_size,
    const uint16_t *words,
    size_t word_count);

#endif
