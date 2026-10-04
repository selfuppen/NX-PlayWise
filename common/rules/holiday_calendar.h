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

#define PTC_CALENDAR_IMPORT_MAX_BYTES 16384u
#define PTC_CALENDAR_MAX_GROUPS 64u
#define PTC_CALENDAR_REGION_ID_SIZE 17u
#define PTC_CALENDAR_REGION_NAME_SIZE 65u

typedef struct {
    char name[65];
    uint16_t first_day_index;
    uint16_t last_day_index;
} PtcCalendarHolidayGroup;

typedef struct {
    uint16_t year;
    char region_id[PTC_CALENDAR_REGION_ID_SIZE];
    char region_name[PTC_CALENDAR_REGION_NAME_SIZE];
    char source_note[129];
    PtcCalendarDayType days[366];
    PtcCalendarHolidayGroup groups[PTC_CALENDAR_MAX_GROUPS];
    uint16_t group_count;
    uint16_t holiday_day_count;
    uint16_t workday_count;
} PtcImportedCalendarYear;

/* A null set selects the immutable built-in China 2026 calendar. */
typedef struct {
    PtcImportedCalendarYear *years[3];
    size_t count;
} PtcHolidayCalendarSet;

bool ptc_calendar_region_id_valid(const char *id);
bool ptc_holiday_calendar_parse_import(const char *text, size_t length,
                                       PtcImportedCalendarYear *out);
PtcCalendarDayType ptc_holiday_calendar_classify_in(const PtcHolidayCalendarSet *set,
                                                    uint16_t day_index, bool *covered);

PtcCalendarDayType ptc_holiday_calendar_classify(uint16_t day_index, bool *covered);
const PtcHolidayCalendarInfo *ptc_holiday_calendar_info(void);
size_t ptc_holiday_calendar_arrangement_count(uint16_t year);
const PtcHolidayArrangement *ptc_holiday_calendar_arrangement(uint16_t year, size_t index);
bool ptc_holiday_calendar_find(PtcCalendarDayType type, uint16_t from_day_index,
                               PtcHolidayCalendarMatch *out);

#endif
