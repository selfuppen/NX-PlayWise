#ifndef PTC_TIME_H
#define PTC_TIME_H

#include <stdbool.h>
#include <stdint.h>

#define PTC_SECONDS_PER_DAY 86400
#define PTC_DAY_INDEX_EPOCH_UNIX 1577836800
#define PTC_UTC8_OFFSET_SECONDS 28800
#define PTC_NANOSECONDS_PER_MINUTE INT64_C(60000000000)

uint16_t ptc_day_index_from_unix(int64_t unix_seconds);
uint16_t ptc_day_index_from_unix_utc8(int64_t unix_seconds);
uint16_t ptc_minute_of_day_from_unix_utc8(int64_t unix_seconds);
bool ptc_day_index_from_date(uint16_t year, uint8_t month, uint8_t day, uint16_t *out);
bool ptc_date_from_day_index(uint16_t day_index, uint16_t *year, uint8_t *month, uint8_t *day);
bool ptc_format_date(uint16_t day_index, char out[11]);
bool ptc_format_date_utc8(int64_t unix_seconds, char out[11]);
uint8_t ptc_weekday_from_day_index(uint16_t day_index_since_2020);
uint32_t ptc_nonnegative_minutes_from_nanoseconds(int64_t nanoseconds);

#endif
