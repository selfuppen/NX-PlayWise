#include "calendar_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/crypto/sha256.h"
#include "../common/time/ptc_time.h"
#include "../third_party/cjson/cJSON.h"

static bool hash_valid(const char *hash)
{
    size_t i;
    if (!hash || strlen(hash) != 64u) return false;
    for (i = 0; i < 64u; ++i)
        if (!((hash[i] >= '0' && hash[i] <= '9') ||
              (hash[i] >= 'a' && hash[i] <= 'f'))) return false;
    return true;
}

void ptc_calendar_sha256_hex(const char *text, size_t length, char out[65])
{
    static const char HEX[] = "0123456789abcdef";
    PtcSha256Ctx sha;
    uint8_t digest[PTC_SHA256_DIGEST_SIZE];
    size_t i;
    ptc_sha256_init(&sha);
    ptc_sha256_update(&sha, (const uint8_t *)text, length);
    ptc_sha256_final(&sha, digest);
    for (i = 0; i < PTC_SHA256_DIGEST_SIZE; ++i) {
        out[i * 2u] = HEX[digest[i] >> 4];
        out[i * 2u + 1u] = HEX[digest[i] & 15u];
    }
    out[64] = '\0';
}

bool ptc_calendar_file_name_valid(const char *name)
{
    size_t i, length;
    if (!name) return false;
    length = strlen(name);
    if (length < 6u || length > 80u || strcmp(name + length - 5u, ".json") != 0 ||
        strstr(name, "..")) return false;
    for (i = 0; i < length; ++i) {
        char c = name[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') continue;
        return false;
    }
    return true;
}

static bool index_path(char *out, size_t size, const char *root, bool active)
{
    int n = snprintf(out, size, "%s/calendars/%s.json", root, active ? "active" : "catalog");
    return n > 0 && (size_t)n < size;
}

static bool library_path(char *out, size_t size, const char *root,
                         const PtcCalendarIndexEntry *entry)
{
    int n = snprintf(out, size, "%s/calendars/library/%s-%u-%s.json", root,
        entry->region_id, (unsigned int)entry->year, entry->sha256);
    return n > 0 && (size_t)n < size;
}

static bool entry_parse(const cJSON *item, PtcCalendarIndexEntry *entry)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "region_id");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "region_name");
    const cJSON *year = cJSON_GetObjectItemCaseSensitive(item, "year");
    const cJSON *sha = cJSON_GetObjectItemCaseSensitive(item, "sha256");
    if (!cJSON_IsObject(item) || !cJSON_IsString(id) || !cJSON_IsString(name) ||
        !cJSON_IsNumber(year) || !cJSON_IsString(sha) ||
        !id->valuestring || !name->valuestring || !sha->valuestring ||
        strlen(id->valuestring) >= sizeof(entry->region_id) ||
        strlen(name->valuestring) == 0u || strlen(name->valuestring) >= sizeof(entry->region_name) ||
        year->valuedouble != (double)year->valueint || year->valueint < 2020 ||
        year->valueint > 2198 || !ptc_calendar_region_id_valid(id->valuestring) ||
        !hash_valid(sha->valuestring)) return false;
    snprintf(entry->region_id, sizeof(entry->region_id), "%s", id->valuestring);
    snprintf(entry->region_name, sizeof(entry->region_name), "%s", name->valuestring);
    entry->year = (uint16_t)year->valueint;
    snprintf(entry->sha256, sizeof(entry->sha256), "%s", sha->valuestring);
    return true;
}

bool ptc_calendar_index_load(PtcStorage *storage, const char *root, bool active,
                             PtcCalendarIndex *out, char digest[65])
{
    char path[320];
    char *text = NULL;
    cJSON *json = NULL;
    const cJSON *entries, *option;
    bool ok = false;
    int i;
    if (!storage || !root || !out || !index_path(path, sizeof(path), root, active)) return false;
    memset(out, 0, sizeof(*out));
    if (digest) digest[0] = '\0';
    if (!storage->vtable->exists(storage, path)) {
        if (active) snprintf(out->option_id, sizeof(out->option_id), "builtin-cn");
        if (digest) ptc_calendar_sha256_hex("", 0, digest);
        return true;
    }
    text = (char *)malloc(PTC_CALENDAR_INDEX_TEXT_SIZE);
    if (!text || !storage->vtable->read_text(storage, path, text, PTC_CALENDAR_INDEX_TEXT_SIZE)) goto done;
    json = cJSON_ParseWithLengthOpts(text, strlen(text) + 1u, NULL, 1);
    if (!cJSON_IsObject(json)) goto done;
    option = cJSON_GetObjectItemCaseSensitive(json, "option_id");
    entries = cJSON_GetObjectItemCaseSensitive(json, "entries");
    if (!cJSON_IsArray(entries) || cJSON_GetArraySize(entries) > (int)PTC_CALENDAR_MAX_INDEX_ENTRIES)
        goto done;
    if (active) {
        if (!cJSON_IsString(option) || !option->valuestring ||
            strlen(option->valuestring) >= sizeof(out->option_id) ||
            (strcmp(option->valuestring, "builtin-cn") != 0 &&
             !ptc_calendar_region_id_valid(option->valuestring))) goto done;
        snprintf(out->option_id, sizeof(out->option_id), "%s", option->valuestring);
    }
    for (i = 0; i < cJSON_GetArraySize(entries); ++i) {
        size_t j;
        PtcCalendarIndexEntry *entry = &out->entries[out->count];
        if (!entry_parse(cJSON_GetArrayItem(entries, i), entry)) goto done;
        if (active && (strcmp(out->option_id, "builtin-cn") == 0 ||
            strcmp(out->option_id, entry->region_id) != 0)) goto done;
        for (j = 0; j < out->count; ++j) {
            const PtcCalendarIndexEntry *prior = &out->entries[j];
            if (strcmp(prior->region_id, entry->region_id) == 0 &&
                (prior->year == entry->year || strcmp(prior->region_name, entry->region_name) != 0)) goto done;
        }
        ++out->count;
    }
    if (active && strcmp(out->option_id, "builtin-cn") != 0 && out->count == 0) goto done;
    if (digest) ptc_calendar_sha256_hex(text, strlen(text), digest);
    ok = true;
done:
    cJSON_Delete(json);
    free(text);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

bool ptc_calendar_index_save(PtcStorage *storage, const char *root, bool active,
                             const PtcCalendarIndex *index)
{
    cJSON *json = NULL, *entries = NULL;
    char *text = NULL, path[320];
    bool ok = false;
    size_t i;
    if (!storage || !index || index->count > PTC_CALENDAR_MAX_INDEX_ENTRIES ||
        !index_path(path, sizeof(path), root, active)) return false;
    json = cJSON_CreateObject();
    entries = cJSON_CreateArray();
    if (!json || !entries) goto done;
    if (active && !cJSON_AddStringToObject(json, "option_id", index->option_id)) goto done;
    if (!cJSON_AddItemToObject(json, "entries", entries)) goto done;
    entries = NULL;
    for (i = 0; i < index->count; ++i) {
        const PtcCalendarIndexEntry *entry = &index->entries[i];
        cJSON *item = cJSON_CreateObject();
        if (!item || !cJSON_AddStringToObject(item, "region_id", entry->region_id) ||
            !cJSON_AddStringToObject(item, "region_name", entry->region_name) ||
            !cJSON_AddNumberToObject(item, "year", entry->year) ||
            !cJSON_AddStringToObject(item, "sha256", entry->sha256) ||
            !cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(json, "entries"), item)) {
            cJSON_Delete(item);
            goto done;
        }
    }
    text = cJSON_PrintUnformatted(json);
    if (!text || strlen(text) >= PTC_CALENDAR_INDEX_TEXT_SIZE) goto done;
    ok = storage->vtable->write_text_atomic(storage, path, text);
done:
    free(text);
    cJSON_Delete(entries);
    cJSON_Delete(json);
    return ok;
}

bool ptc_calendar_import_file(PtcStorage *storage, const char *root,
                              const char *file_name, const char *expected_sha256,
                              PtcCalendarIndex *catalog)
{
    char path[320], library[320], actual_hash[65];
    char *text = NULL;
    PtcImportedCalendarYear *calendar = NULL;
    PtcCalendarIndexEntry entry;
    size_t i;
    bool ok = false;
    if (!storage || !root || !catalog || !ptc_calendar_file_name_valid(file_name) ||
        !hash_valid(expected_sha256)) return false;
    if (snprintf(path, sizeof(path), "%s/calendar-import/%s", root, file_name) >= (int)sizeof(path)) return false;
    text = (char *)malloc(PTC_CALENDAR_IMPORT_MAX_BYTES + 1u);
    calendar = (PtcImportedCalendarYear *)malloc(sizeof(*calendar));
    if (!text || !calendar || !storage->vtable->read_text(storage, path, text,
        PTC_CALENDAR_IMPORT_MAX_BYTES + 1u)) goto done;
    ptc_calendar_sha256_hex(text, strlen(text), actual_hash);
    if (strcmp(actual_hash, expected_sha256) != 0 ||
        !ptc_holiday_calendar_parse_import(text, strlen(text), calendar)) goto done;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.region_id, sizeof(entry.region_id), "%s", calendar->region_id);
    snprintf(entry.region_name, sizeof(entry.region_name), "%s", calendar->region_name);
    entry.year = calendar->year;
    snprintf(entry.sha256, sizeof(entry.sha256), "%s", actual_hash);
    for (i = 0; i < catalog->count; ++i) {
        if (strcmp(catalog->entries[i].region_id, entry.region_id) != 0) continue;
        if (strcmp(catalog->entries[i].region_name, entry.region_name) != 0) goto done;
        if (catalog->entries[i].year == entry.year) break;
    }
    if (i == catalog->count && catalog->count >= PTC_CALENDAR_MAX_INDEX_ENTRIES) goto done;
    if (!library_path(library, sizeof(library), root, &entry)) goto done;
    /* Rewriting the content-addressed copy also repairs a corrupted prior copy. */
    if (!storage->vtable->write_text_atomic(storage, library, text)) goto done;
    catalog->entries[i] = entry;
    if (i == catalog->count) ++catalog->count;
    ok = true;
done:
    free(calendar);
    free(text);
    return ok;
}

bool ptc_calendar_build_selection(const PtcCalendarIndex *catalog,
                                  const char *option_id, PtcCalendarIndex *selected)
{
    size_t i;
    if (!catalog || !selected || !option_id ||
        (strcmp(option_id, "builtin-cn") != 0 && !ptc_calendar_region_id_valid(option_id))) return false;
    memset(selected, 0, sizeof(*selected));
    snprintf(selected->option_id, sizeof(selected->option_id), "%s", option_id);
    if (strcmp(option_id, "builtin-cn") == 0) return true;
    for (i = 0; i < catalog->count; ++i) {
        if (strcmp(catalog->entries[i].region_id, option_id) == 0)
            selected->entries[selected->count++] = catalog->entries[i];
    }
    return selected->count > 0;
}

bool ptc_calendar_selection_validate(PtcStorage *storage, const char *root,
                                     const PtcCalendarIndex *selected)
{
    char *text = NULL;
    PtcImportedCalendarYear *calendar = NULL;
    char path[320], hash[65];
    size_t i;
    bool ok = false;
    if (!storage || !root || !selected) return false;
    if (strcmp(selected->option_id, "builtin-cn") == 0) return selected->count == 0;
    if (!ptc_calendar_region_id_valid(selected->option_id) || selected->count == 0) return false;
    text = (char *)malloc(PTC_CALENDAR_IMPORT_MAX_BYTES + 1u);
    calendar = (PtcImportedCalendarYear *)malloc(sizeof(*calendar));
    if (!text || !calendar) goto done;
    for (i = 0; i < selected->count; ++i) {
        const PtcCalendarIndexEntry *entry = &selected->entries[i];
        if (!library_path(path, sizeof(path), root, entry) ||
            !storage->vtable->read_text(storage, path, text, PTC_CALENDAR_IMPORT_MAX_BYTES + 1u)) goto done;
        ptc_calendar_sha256_hex(text, strlen(text), hash);
        if (strcmp(hash, entry->sha256) != 0 ||
            !ptc_holiday_calendar_parse_import(text, strlen(text), calendar) ||
            calendar->year != entry->year ||
            strcmp(calendar->region_id, entry->region_id) != 0 ||
            strcmp(calendar->region_name, entry->region_name) != 0) goto done;
    }
    ok = true;
done:
    free(calendar);
    free(text);
    return ok;
}

bool ptc_calendar_runtime_from_selection(PtcStorage *storage, const char *root,
                                         const PtcCalendarIndex *selected,
                                         uint16_t day_index, PtcCalendarRuntime *out)
{
    char path[320], actual_hash[65];
    char *text = NULL;
    uint16_t year;
    uint8_t month, day;
    size_t i;
    bool ok = false;
    if (!out || !ptc_date_from_day_index(day_index, &year, &month, &day)) return false;
    memset(out, 0, sizeof(*out));
    if (!selected) goto done;
    snprintf(out->option_id, sizeof(out->option_id), "%s", selected->option_id);
    if (strcmp(selected->option_id, "builtin-cn") == 0) {
        out->builtin = true;
        snprintf(out->region_name, sizeof(out->region_name), "中国大陆（内置）");
        ok = true;
        goto done;
    }
    text = (char *)malloc(PTC_CALENDAR_IMPORT_MAX_BYTES + 1u);
    if (!text) goto done;
    for (i = 0; i < selected->count; ++i) {
        const PtcCalendarIndexEntry *entry = &selected->entries[i];
        PtcImportedCalendarYear *calendar;
        if (entry->year + 1u < year || entry->year > year + 1u) continue;
        if (out->set.count >= 3u || !library_path(path, sizeof(path), root, entry) ||
            !storage->vtable->read_text(storage, path, text, PTC_CALENDAR_IMPORT_MAX_BYTES + 1u)) goto done;
        ptc_calendar_sha256_hex(text, strlen(text), actual_hash);
        calendar = &out->years[out->set.count];
        if (strcmp(actual_hash, entry->sha256) != 0 ||
            !ptc_holiday_calendar_parse_import(text, strlen(text), calendar) ||
            calendar->year != entry->year ||
            strcmp(calendar->region_id, entry->region_id) != 0 ||
            strcmp(calendar->region_name, entry->region_name) != 0) goto done;
        out->set.years[out->set.count++] = calendar;
        snprintf(out->region_name, sizeof(out->region_name), "%s", entry->region_name);
    }
    if (out->region_name[0] == '\0' && selected->count > 0)
        snprintf(out->region_name, sizeof(out->region_name), "%s", selected->entries[0].region_name);
    ok = true;
done:
    free(text);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

bool ptc_calendar_runtime_load(PtcStorage *storage, const char *root,
                               uint16_t day_index, PtcCalendarRuntime *out)
{
    PtcCalendarIndex *selected;
    bool ok;
    selected = (PtcCalendarIndex *)malloc(sizeof(*selected));
    if (!selected) return false;
    ok = ptc_calendar_index_load(storage, root, true, selected, NULL) &&
        ptc_calendar_selection_validate(storage, root, selected) &&
        ptc_calendar_runtime_from_selection(storage, root, selected, day_index, out);
    free(selected);
    return ok;
}
