#ifndef PTC_HOLIDAY_CALENDAR_H
#define PTC_HOLIDAY_CALENDAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PTC_CALENDAR_DAY_ORDINARY = 0,
    PTC_CALENDAR_DAY_STATUTORY_HOLIDAY = 1,
    PTC_CALENDAR_DAY_MAKEUP_WORKDAY = 2
} PtcCalendarDayType;

typedef struct {
    uint16_t first_year;
    uint16_t last_year;
    uint16_t version;
    const char *published_date;
    const char *source_url;
} PtcHolidayCalendarInfo;

typedef struct {
    uint16_t year;
    const char *holiday_id;
    const char *display_name;
    uint8_t start_month;
    uint8_t start_day;
    uint8_t end_month;
    uint8_t end_day;
    const char *makeup_workdays;
} PtcHolidayArrangement;

typedef struct {
    PtcCalendarDayType type;
    uint16_t day_index;
    const PtcHolidayArrangement *arrangement;
} PtcHolidayCalendarMatch;

PtcCalendarDayType ptc_holiday_calendar_classify(uint16_t day_index, bool *covered);
const PtcHolidayCalendarInfo *ptc_holiday_calendar_info(void);
size_t ptc_holiday_calendar_arrangement_count(uint16_t year);
const PtcHolidayArrangement *ptc_holiday_calendar_arrangement(uint16_t year, size_t index);
bool ptc_holiday_calendar_find(PtcCalendarDayType type, uint16_t from_day_index,
                               PtcHolidayCalendarMatch *out);

#endif
