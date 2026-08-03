#include "ptc_time.h"

#include <stdio.h>

static bool is_leap_year(uint16_t year)
{
    return (year % 4u == 0u && year % 100u != 0u) || (year % 400u == 0u);
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t MONTH_DAYS[12] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31,
    };
    if (month == 2u && is_leap_year(year)) {
        return 29;
    }
    if (month < 1u || month > 12u) {
        return 0;
    }
    return MONTH_DAYS[month - 1u];
}

uint16_t ptc_day_index_from_unix(int64_t unix_seconds)
{
    if (unix_seconds < PTC_DAY_INDEX_EPOCH_UNIX) {
        return 0;
    }
    return (uint16_t)((unix_seconds - PTC_DAY_INDEX_EPOCH_UNIX) / PTC_SECONDS_PER_DAY);
}

uint16_t ptc_day_index_from_unix_utc8(int64_t unix_seconds)
{
    if (unix_seconds < PTC_DAY_INDEX_EPOCH_UNIX - PTC_UTC8_OFFSET_SECONDS) {
        return 0;
    }
    return (uint16_t)((unix_seconds + PTC_UTC8_OFFSET_SECONDS - PTC_DAY_INDEX_EPOCH_UNIX) / PTC_SECONDS_PER_DAY);
}

uint16_t ptc_minute_of_day_from_unix_utc8(int64_t unix_seconds)
{
    int64_t shifted = unix_seconds + PTC_UTC8_OFFSET_SECONDS;
    int64_t seconds = shifted % PTC_SECONDS_PER_DAY;
    if (seconds < 0) {
        seconds += PTC_SECONDS_PER_DAY;
    }
    return (uint16_t)(seconds / 60);
}

bool ptc_day_index_from_date(uint16_t year, uint8_t month, uint8_t day, uint16_t *out)
{
    uint32_t days = 0;
    uint16_t cursor_year;
    uint8_t cursor_month;
    uint8_t month_days;

    if (!out || year < 2020u) {
        return false;
    }
    month_days = days_in_month(year, month);
    if (month_days == 0u || day < 1u || day > month_days) {
        return false;
    }

    for (cursor_year = 2020u; cursor_year < year; ++cursor_year) {
        days += is_leap_year(cursor_year) ? 366u : 365u;
        if (days > 65535u) {
            return false;
        }
    }
    for (cursor_month = 1u; cursor_month < month; ++cursor_month) {
        days += days_in_month(year, cursor_month);
        if (days > 65535u) {
            return false;
        }
    }
    days += (uint32_t)day - 1u;
    if (days > 65535u) {
        return false;
    }

    *out = (uint16_t)days;
    return true;
}

bool ptc_date_from_day_index(uint16_t day_index, uint16_t *year, uint8_t *month, uint8_t *day)
{
    uint32_t remaining = day_index;
    uint16_t y = 2020;
    uint8_t m = 1;
    uint16_t year_days;
    uint8_t month_days;
    if (!year || !month || !day) return false;
    for (;;) {
        year_days = is_leap_year(y) ? 366u : 365u;
        if (remaining < year_days) break;
        remaining -= year_days;
        if (y == 65535u) return false;
        ++y;
    }
    for (;;) {
        month_days = days_in_month(y, m);
        if (remaining < month_days) break;
        remaining -= month_days;
        if (m == 12u) return false;
        ++m;
    }
    *year = y;
    *month = m;
    *day = (uint8_t)(remaining + 1u);
    return true;
}

bool ptc_format_date_utc8(int64_t unix_seconds, char out[11])
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint16_t day_index;
    int written;
    if (!out || unix_seconds < PTC_DAY_INDEX_EPOCH_UNIX - PTC_UTC8_OFFSET_SECONDS) return false;
    day_index = ptc_day_index_from_unix_utc8(unix_seconds);
    if (!ptc_date_from_day_index(day_index, &year, &month, &day)) return false;
    written = snprintf(out, 11, "%04u-%02u-%02u", (unsigned int)year, (unsigned int)month, (unsigned int)day);
    return written == 10;
}

uint8_t ptc_weekday_from_day_index(uint16_t day_index_since_2020)
{
    return (uint8_t)((3u + day_index_since_2020) % 7u);
}

bool ptc_bedtime_active(uint16_t minute_of_day, uint16_t start_min, uint16_t end_min)
{
    if (start_min >= 1440 || end_min >= 1440 || start_min == end_min) {
        return false;
    }
    if (start_min < end_min) {
        return minute_of_day >= start_min && minute_of_day < end_min;
    }
    return minute_of_day >= start_min || minute_of_day < end_min;
}

uint32_t ptc_nonnegative_minutes_from_nanoseconds(int64_t nanoseconds)
{
    if (nanoseconds <= 0) {
        return 0;
    }
    return (uint32_t)(nanoseconds / PTC_NANOSECONDS_PER_MINUTE);
}
