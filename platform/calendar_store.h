#ifndef PTC_CALENDAR_STORE_H
#define PTC_CALENDAR_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "storage.h"
#include "../common/rules/holiday_calendar.h"

#define PTC_CALENDAR_MAX_INDEX_ENTRIES 64u
#define PTC_CALENDAR_INDEX_TEXT_SIZE 16384u

typedef struct {
    char region_id[PTC_CALENDAR_REGION_ID_SIZE];
    char region_name[PTC_CALENDAR_REGION_NAME_SIZE];
    uint16_t year;
    char sha256[65];
} PtcCalendarIndexEntry;

typedef struct {
    char option_id[PTC_CALENDAR_REGION_ID_SIZE];
    size_t count;
    PtcCalendarIndexEntry entries[PTC_CALENDAR_MAX_INDEX_ENTRIES];
} PtcCalendarIndex;

typedef struct {
    char option_id[PTC_CALENDAR_REGION_ID_SIZE];
    char region_name[PTC_CALENDAR_REGION_NAME_SIZE];
    bool builtin;
    PtcHolidayCalendarSet set;
    PtcImportedCalendarYear years[3];
} PtcCalendarRuntime;

bool ptc_calendar_file_name_valid(const char *name);
void ptc_calendar_sha256_hex(const char *text, size_t length, char out[65]);
bool ptc_calendar_index_load(PtcStorage *storage, const char *root, bool active,
                             PtcCalendarIndex *out, char digest[65]);
bool ptc_calendar_index_save(PtcStorage *storage, const char *root, bool active,
                             const PtcCalendarIndex *index);
bool ptc_calendar_import_file(PtcStorage *storage, const char *root,
                              const char *file_name, const char *expected_sha256,
                              PtcCalendarIndex *catalog);
bool ptc_calendar_build_selection(const PtcCalendarIndex *catalog,
                                  const char *option_id, PtcCalendarIndex *selected);
bool ptc_calendar_selection_validate(PtcStorage *storage, const char *root,
                                     const PtcCalendarIndex *selected);
bool ptc_calendar_runtime_load(PtcStorage *storage, const char *root,
                               uint16_t day_index, PtcCalendarRuntime *out);
bool ptc_calendar_runtime_from_selection(PtcStorage *storage, const char *root,
                                         const PtcCalendarIndex *selected,
                                         uint16_t day_index, PtcCalendarRuntime *out);

#endif
