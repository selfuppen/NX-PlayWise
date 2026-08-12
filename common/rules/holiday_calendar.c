#include "holiday_calendar.h"

#include <stddef.h>

#include "../time/ptc_time.h"

typedef struct {
    uint8_t month;
    uint8_t day;
    PtcCalendarDayType type;
} PtcHolidayDate;

/* 国办发明电〔2025〕7号. Dates are kept as the auditable source form; the
   classifier converts the platform-independent day index before matching. */
static const PtcHolidayDate PTC_2026_DATES[] = {
    {1, 1, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {1, 2, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {1, 3, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {1, 4, PTC_CALENDAR_DAY_MAKEUP_WORKDAY},
    {2, 14, PTC_CALENDAR_DAY_MAKEUP_WORKDAY},
    {2, 15, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {2, 16, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {2, 17, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {2, 18, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {2, 19, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {2, 20, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {2, 21, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {2, 22, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {2, 23, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {2, 28, PTC_CALENDAR_DAY_MAKEUP_WORKDAY},
    {4, 4, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {4, 5, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {4, 6, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {5, 1, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {5, 2, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {5, 3, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {5, 4, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {5, 5, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {5, 9, PTC_CALENDAR_DAY_MAKEUP_WORKDAY},
    {6, 19, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {6, 20, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {6, 21, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {9, 25, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {9, 26, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {9, 27, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {10, 1, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {10, 2, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {10, 3, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {10, 4, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {10, 5, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {10, 6, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {10, 7, PTC_CALENDAR_DAY_STATUTORY_HOLIDAY},
    {9, 20, PTC_CALENDAR_DAY_MAKEUP_WORKDAY},
    {10, 10, PTC_CALENDAR_DAY_MAKEUP_WORKDAY}
};

static const PtcHolidayCalendarInfo PTC_CALENDAR_INFO = {
    2026, 2026, 1, "2025-11-04",
    "https://www.gov.cn/yaowen/liebiao/202511/content_7047099.htm"
};

/* Display data shares this authoritative module with the classifier so UI and policy cannot drift. */
static const PtcHolidayArrangement PTC_2026_ARRANGEMENTS[] = {
    {2026, "new_year", "元旦", 1, 1, 1, 3, "1月4日"},
    {2026, "spring_festival", "春节", 2, 15, 2, 23, "2月14日、2月28日"},
    {2026, "qingming", "清明节", 4, 4, 4, 6, "无"},
    {2026, "labour_day", "劳动节", 5, 1, 5, 5, "5月9日"},
    {2026, "dragon_boat", "端午节", 6, 19, 6, 21, "无"},
    {2026, "mid_autumn", "中秋节", 9, 25, 9, 27, "9月20日"},
    {2026, "national_day", "国庆节", 10, 1, 10, 7, "10月10日"}
};

PtcCalendarDayType ptc_holiday_calendar_classify(uint16_t day_index, bool *covered)
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    size_t i;
    if (covered) *covered = false;
    if (!ptc_date_from_day_index(day_index, &year, &month, &day) || year != 2026) {
        return PTC_CALENDAR_DAY_ORDINARY;
    }
    if (covered) *covered = true;
    for (i = 0; i < sizeof(PTC_2026_DATES) / sizeof(PTC_2026_DATES[0]); ++i) {
        if (PTC_2026_DATES[i].month == month && PTC_2026_DATES[i].day == day) {
            return PTC_2026_DATES[i].type;
        }
    }
    return PTC_CALENDAR_DAY_ORDINARY;
}

const PtcHolidayCalendarInfo *ptc_holiday_calendar_info(void)
{
    return &PTC_CALENDAR_INFO;
}

size_t ptc_holiday_calendar_arrangement_count(uint16_t year)
{
    return year == 2026 ? sizeof(PTC_2026_ARRANGEMENTS) / sizeof(PTC_2026_ARRANGEMENTS[0]) : 0;
}

const PtcHolidayArrangement *ptc_holiday_calendar_arrangement(uint16_t year, size_t index)
{
    if (year != 2026 || index >= ptc_holiday_calendar_arrangement_count(year)) return NULL;
    return &PTC_2026_ARRANGEMENTS[index];
}
