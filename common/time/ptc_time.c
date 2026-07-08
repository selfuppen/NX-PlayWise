#include "ptc_time.h"

uint16_t ptc_day_index_from_unix(int64_t unix_seconds)
{
    if (unix_seconds < PTC_DAY_INDEX_EPOCH_UNIX) {
        return 0;
    }
    return (uint16_t)((unix_seconds - PTC_DAY_INDEX_EPOCH_UNIX) / PTC_SECONDS_PER_DAY);
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
