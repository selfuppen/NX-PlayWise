#ifndef PTC_HOLIDAY_CALENDAR_H
#define PTC_HOLIDAY_CALENDAR_H

#include <stdbool.h>
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

PtcCalendarDayType ptc_holiday_calendar_classify(uint16_t day_index, bool *covered);
const PtcHolidayCalendarInfo *ptc_holiday_calendar_info(void);

#endif
