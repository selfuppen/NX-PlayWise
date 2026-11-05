#include "holiday_calendar.h"

#include <stddef.h>
#include <string.h>

#include "../time/ptc_time.h"

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t arrangement_index;
} PtcMakeupDate;

static const PtcHolidayCalendarInfo PTC_CALENDAR_2026_INFO = {
    2026, 2026, 1, "2025-11-04",
    "https://www.gov.cn/yaowen/liebiao/202511/content_7047099.htm"
};

static const PtcHolidayCalendarInfo PTC_CALENDAR_INFO = {
    2026, 2027, 2, "2026-11-05",
    "https://www.gov.cn/zhengce/content/202611/content_7085101.htm"
};

/* Holiday ranges drive both the classifier and the calendar view. */
static const PtcHolidayArrangement PTC_2026_ARRANGEMENTS[] = {
    {2026, "new_year", "元旦", 1, 1, 1, 3, "1月4日"},
    {2026, "spring_festival", "春节", 2, 15, 2, 23, "2月14日、2月28日"},
    {2026, "qingming", "清明节", 4, 4, 4, 6, "无"},
    {2026, "labour_day", "劳动节", 5, 1, 5, 5, "5月9日"},
    {2026, "dragon_boat", "端午节", 6, 19, 6, 21, "无"},
    {2026, "mid_autumn", "中秋节", 9, 25, 9, 27, "9月20日"},
    {2026, "national_day", "国庆节", 10, 1, 10, 7, "10月10日"}
};

static const PtcHolidayArrangement PTC_2027_ARRANGEMENTS[] = {
    {2027, "new_year", "元旦", 1, 1, 1, 3, "无"},
    {2027, "spring_festival", "春节", 2, 5, 2, 13, "1月31日、2月14日"},
    {2027, "qingming", "清明节", 4, 3, 4, 5, "无"},
    {2027, "labour_day", "劳动节", 5, 1, 5, 5, "5月8日"},
    {2027, "dragon_boat", "端午节", 6, 9, 6, 9, "无"},
    {2027, "mid_autumn", "中秋节", 9, 15, 9, 15, "无"},
    {2027, "national_day", "国庆节", 10, 1, 10, 7, "9月26日、10月9日"}
};

static const PtcMakeupDate PTC_MAKEUP_DATES[] = {
    {2026, 1, 4, 0}, {2026, 2, 14, 1}, {2026, 2, 28, 1},
    {2026, 5, 9, 3}, {2026, 9, 20, 5}, {2026, 10, 10, 6},
    {2027, 1, 31, 1}, {2027, 2, 14, 1}, {2027, 5, 8, 3},
    {2027, 9, 26, 6}, {2027, 10, 9, 6}
};

const PtcHolidayCalendarInfo *ptc_holiday_calendar_info(void)
{
    return &PTC_CALENDAR_INFO;
}

const PtcHolidayCalendarInfo *ptc_holiday_calendar_info_for_year(uint16_t year)
{
    if (year == 2026) return &PTC_CALENDAR_2026_INFO;
    return year == 2027 ? &PTC_CALENDAR_INFO : NULL;
}

size_t ptc_holiday_calendar_arrangement_count(uint16_t year)
{
    if (year == 2026) return sizeof(PTC_2026_ARRANGEMENTS) / sizeof(PTC_2026_ARRANGEMENTS[0]);
    if (year == 2027) return sizeof(PTC_2027_ARRANGEMENTS) / sizeof(PTC_2027_ARRANGEMENTS[0]);
    return 0;
}

const PtcHolidayArrangement *ptc_holiday_calendar_arrangement(uint16_t year, size_t index)
{
    if (index >= ptc_holiday_calendar_arrangement_count(year)) return NULL;
    return year == 2026 ? &PTC_2026_ARRANGEMENTS[index] : &PTC_2027_ARRANGEMENTS[index];
}

static const PtcHolidayArrangement *arrangement_for_date(
    uint16_t year, uint8_t month, uint8_t day, PtcCalendarDayType *type)
{
    int date = month * 100 + day;
    *type = PTC_CALENDAR_DAY_ORDINARY;
    for (size_t i = 0; i < ptc_holiday_calendar_arrangement_count(year); ++i) {
        const PtcHolidayArrangement *entry = ptc_holiday_calendar_arrangement(year, i);
        if (date >= entry->start_month * 100 + entry->start_day &&
            date <= entry->end_month * 100 + entry->end_day) {
            *type = PTC_CALENDAR_DAY_STATUTORY_HOLIDAY;
            return entry;
        }
    }
    for (size_t i = 0; i < sizeof(PTC_MAKEUP_DATES) / sizeof(PTC_MAKEUP_DATES[0]); ++i) {
        const PtcMakeupDate *entry = &PTC_MAKEUP_DATES[i];
        if (entry->year == year && entry->month == month && entry->day == day) {
            *type = PTC_CALENDAR_DAY_MAKEUP_WORKDAY;
            return ptc_holiday_calendar_arrangement(year, entry->arrangement_index);
        }
    }
    return NULL;
}

PtcCalendarDayType ptc_holiday_calendar_classify(uint16_t day_index, bool *covered)
{
    uint16_t year;
    uint8_t month, day;
    PtcCalendarDayType type = PTC_CALENDAR_DAY_ORDINARY;
    bool available = ptc_date_from_day_index(day_index, &year, &month, &day) &&
        ptc_holiday_calendar_info_for_year(year) != NULL;
    if (covered) *covered = available;
    if (available) (void)arrangement_for_date(year, month, day, &type);
    return type;
}

bool ptc_holiday_calendar_find(PtcCalendarDayType type, uint16_t from_day_index,
                               PtcHolidayCalendarMatch *out)
{
    uint32_t day_index;
    uint16_t first_day_index;
    uint16_t last_day_index;
    if (!out || (type != PTC_CALENDAR_DAY_STATUTORY_HOLIDAY &&
                 type != PTC_CALENDAR_DAY_MAKEUP_WORKDAY)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!ptc_day_index_from_date(PTC_CALENDAR_INFO.first_year, 1, 1, &first_day_index) ||
        !ptc_day_index_from_date(PTC_CALENDAR_INFO.last_year, 12, 31, &last_day_index) ||
        from_day_index < first_day_index || from_day_index > last_day_index) {
        return false;
    }
    for (day_index = from_day_index; day_index <= last_day_index; ++day_index) {
        uint16_t year;
        uint8_t month;
        uint8_t day;
        if (!ptc_date_from_day_index((uint16_t)day_index, &year, &month, &day)) break;
        PtcCalendarDayType actual;
        const PtcHolidayArrangement *arrangement = arrangement_for_date(year, month, day, &actual);
        if (actual == type) {
            out->type = type;
            out->day_index = (uint16_t)day_index;
            out->arrangement = arrangement;
            return true;
        }
    }
    return false;
}
