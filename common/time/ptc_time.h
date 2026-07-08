#ifndef PTC_TIME_H
#define PTC_TIME_H

#include <stdbool.h>
#include <stdint.h>

#define PTC_SECONDS_PER_DAY 86400
#define PTC_DAY_INDEX_EPOCH_UNIX 1577836800

uint16_t ptc_day_index_from_unix(int64_t unix_seconds);
uint8_t ptc_weekday_from_day_index(uint16_t day_index_since_2020);
bool ptc_bedtime_active(uint16_t minute_of_day, uint16_t start_min, uint16_t end_min);

#endif
