#include "holiday_calendar.h"

#include <string.h>

#include "../time/ptc_time.h"
#include "../../third_party/cjson/cJSON.h"

static bool utf8_valid(const char *text, size_t length)
{
    size_t i = 0;
    while (i < length) {
        unsigned char a = (unsigned char)text[i++];
        unsigned char b, c, d;
        if (a < 0x80u) continue;
        if (a < 0xc2u || a > 0xf4u || i >= length) return false;
        b = (unsigned char)text[i++];
        if (b < 0x80u || b > 0xbfu) return false;
        if (a < 0xe0u) continue;
        if (i >= length) return false;
        c = (unsigned char)text[i++];
        if (c < 0x80u || c > 0xbfu || (a == 0xe0u && b < 0xa0u) ||
            (a == 0xedu && b >= 0xa0u)) return false;
        if (a < 0xf0u) continue;
        if (i >= length) return false;
        d = (unsigned char)text[i++];
        if (d < 0x80u || d > 0xbfu || (a == 0xf0u && b < 0x90u) ||
            (a == 0xf4u && b >= 0x90u)) return false;
    }
    return true;
}

static bool integer_in_range(const cJSON *item, int low, int high, int *out)
{
    if (!cJSON_IsNumber(item) || item->valuedouble < (double)low ||
        item->valuedouble > (double)high || (double)item->valueint != item->valuedouble)
        return false;
    *out = item->valueint;
    return true;
}

static bool copy_string(const cJSON *item, char *out, size_t size, bool optional)
{
    size_t length;
    if (!item && optional) { out[0] = '\0'; return true; }
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    length = strlen(item->valuestring);
    if (length == 0 || length >= size) return false;
    memcpy(out, item->valuestring, length + 1u);
    return true;
}

bool ptc_calendar_region_id_valid(const char *id)
{
    size_t i, len;
    if (!id) return false;
    len = strlen(id);
    if (len < 2u || len >= PTC_CALENDAR_REGION_ID_SIZE || id[0] == '-' || id[len - 1u] == '-')
        return false;
    for (i = 0; i < len; ++i) {
        char c = id[i];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) continue;
        if (c == '-' && i > 0 && id[i - 1u] != '-') continue;
        return false;
    }
    return true;
}

static bool parse_date(const cJSON *item, uint16_t year, uint16_t *out)
{
    const char *s;
    unsigned int values[3] = {0, 0, 0};
    unsigned int i;
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    s = item->valuestring;
    if (strlen(s) != 10u || s[4] != '-' || s[7] != '-') return false;
    for (i = 0; i < 10u; ++i) {
        if (i == 4u || i == 7u) continue;
        if (s[i] < '0' || s[i] > '9') return false;
        if (i < 4u) values[0] = values[0] * 10u + (unsigned int)(s[i] - '0');
        else if (i < 7u) values[1] = values[1] * 10u + (unsigned int)(s[i] - '0');
        else values[2] = values[2] * 10u + (unsigned int)(s[i] - '0');
    }
    return values[0] == year && ptc_day_index_from_date((uint16_t)values[0],
        (uint8_t)values[1], (uint8_t)values[2], out);
}

bool ptc_holiday_calendar_parse_import(const char *text, size_t length,
                                       PtcImportedCalendarYear *out)
{
    cJSON *root = NULL;
    const cJSON *holidays, *workdays, *item;
    uint16_t first = 0, last = 0;
    int format_version, year, i;
    bool valid = false;
    if (!text || !out || length == 0 || length > PTC_CALENDAR_IMPORT_MAX_BYTES ||
        strlen(text) != length || !utf8_valid(text, length)) return false;
    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLengthOpts(text, length + 1u, NULL, 1);
    if (!cJSON_IsObject(root) ||
        !integer_in_range(cJSON_GetObjectItemCaseSensitive(root, "format_version"), 1, 1, &format_version) ||
        !integer_in_range(cJSON_GetObjectItemCaseSensitive(root, "year"), 2020, 2198, &year) ||
        !copy_string(cJSON_GetObjectItemCaseSensitive(root, "region_id"), out->region_id,
            sizeof(out->region_id), false) || !ptc_calendar_region_id_valid(out->region_id) ||
        !copy_string(cJSON_GetObjectItemCaseSensitive(root, "region_name"), out->region_name,
            sizeof(out->region_name), false) ||
        !copy_string(cJSON_GetObjectItemCaseSensitive(root, "source_note"), out->source_note,
            sizeof(out->source_note), true)) goto done;
    out->year = (uint16_t)year;
    if (!ptc_day_index_from_date(out->year, 1, 1, &first) ||
        !ptc_day_index_from_date(out->year, 12, 31, &last)) goto done;
    holidays = cJSON_GetObjectItemCaseSensitive(root, "holidays");
    workdays = cJSON_GetObjectItemCaseSensitive(root, "workdays");
    if (!cJSON_IsArray(holidays) || !cJSON_IsArray(workdays) ||
        cJSON_GetArraySize(holidays) > (int)PTC_CALENDAR_MAX_GROUPS ||
        cJSON_GetArraySize(workdays) > 366) goto done;
    for (i = 0; i < cJSON_GetArraySize(holidays); ++i) {
        PtcCalendarHolidayGroup *group = &out->groups[i];
        uint16_t index;
        item = cJSON_GetArrayItem(holidays, i);
        if (!cJSON_IsObject(item) ||
            !copy_string(cJSON_GetObjectItemCaseSensitive(item, "name"), group->name,
                sizeof(group->name), false) ||
            !parse_date(cJSON_GetObjectItemCaseSensitive(item, "from"), out->year,
                &group->first_day_index) ||
            !parse_date(cJSON_GetObjectItemCaseSensitive(item, "to"), out->year,
                &group->last_day_index) ||
            group->first_day_index > group->last_day_index) goto done;
        for (index = group->first_day_index; index <= group->last_day_index; ++index) {
            if (out->days[index - first] != PTC_CALENDAR_DAY_ORDINARY) goto done;
            out->days[index - first] = PTC_CALENDAR_DAY_STATUTORY_HOLIDAY;
            ++out->holiday_day_count;
        }
        ++out->group_count;
    }
    for (i = 0; i < cJSON_GetArraySize(workdays); ++i) {
        uint16_t index;
        item = cJSON_GetArrayItem(workdays, i);
        if (!parse_date(item, out->year, &index) ||
            out->days[index - first] != PTC_CALENDAR_DAY_ORDINARY) goto done;
        out->days[index - first] = PTC_CALENDAR_DAY_MAKEUP_WORKDAY;
        ++out->workday_count;
    }
    valid = true;
done:
    cJSON_Delete(root);
    if (!valid) memset(out, 0, sizeof(*out));
    return valid;
}

PtcCalendarDayType ptc_holiday_calendar_classify_in(const PtcHolidayCalendarSet *set,
                                                    uint16_t day_index, bool *covered)
{
    uint16_t year, first;
    uint8_t month, day;
    size_t i;
    if (!set) return ptc_holiday_calendar_classify(day_index, covered);
    if (covered) *covered = false;
    if (!ptc_date_from_day_index(day_index, &year, &month, &day))
        return PTC_CALENDAR_DAY_ORDINARY;
    for (i = 0; i < set->count; ++i) {
        const PtcImportedCalendarYear *calendar = set->years[i];
        if (!calendar || calendar->year != year ||
            !ptc_day_index_from_date(year, 1, 1, &first)) continue;
        if (covered) *covered = true;
        return calendar->days[day_index - first];
    }
    return PTC_CALENDAR_DAY_ORDINARY;
}
