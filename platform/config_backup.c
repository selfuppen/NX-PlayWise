#include "config_backup.h"
#include "calendar_store.h"
#include "../common/protocol/request_schema.h"
#include "../common/crypto/sha256.h"
#include "../common/security/credential_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool number(const cJSON *o, const char *key, double min, double max, bool optional)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return (!v && optional) || (cJSON_IsNumber(v) && v->valuedouble >= min &&
        v->valuedouble <= max && v->valuedouble == (double)(int64_t)v->valuedouble);
}

static bool boolean(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return !v || cJSON_IsBool(v);
}

static bool hex(const char *s, size_t length)
{
    if (!s || strlen(s) != length) return false;
    for (size_t i = 0; i < length; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

bool ptc_config_json_valid(const cJSON *o, unsigned depth)
{
    if (!o || depth > 16) return false;
    for (const cJSON *a = o->child; a; a = a->next) {
        if (cJSON_IsObject(o)) {
            if (!a->string) return false;
            for (const cJSON *b = a->next; b; b = b->next)
                if (b->string && strcmp(a->string, b->string) == 0) return false;
        }
        if (!ptc_config_json_valid(a, depth + 1)) return false;
    }
    return true;
}

cJSON *ptc_config_read_json(PtcStorage *s, const char *root, const char *relative, size_t limit)
{
    char path[512];
    char *text = malloc(limit);
    cJSON *json = NULL;
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    if (text && s->vtable->read_text(s, path, text, limit))
        json = cJSON_ParseWithOpts(text, NULL, true);
    free(text);
    if (json && !ptc_config_json_valid(json, 0)) { cJSON_Delete(json); json = NULL; }
    return json;
}

bool ptc_config_write_json(PtcStorage *s, const char *root, const char *relative, const cJSON *json)
{
    char path[512];
    char *text = cJSON_PrintUnformatted(json);
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    bool ok = text && s->vtable->write_text_atomic(s, path, text);
    free(text);
    return ok;
}

bool ptc_config_stage_path(char *out, size_t size, const char *root, const char *id)
{
    if (!ptc_request_id_is_valid(id)) return false;
    char hash[65];
    ptc_calendar_sha256_hex(id, strlen(id), hash);
    int n = snprintf(out, size, "%s/backups/cfg-%.16s", root, hash);
    return n > 0 && (size_t)n < size;
}

unsigned ptc_config_rule_group(const char *k)
{
    if (!strcmp(k, "week")) return PTC_CONFIG_WEEK;
    if (!strncmp(k, "scheduled_override_", 19)) return PTC_CONFIG_SCHEDULED;
    if (!strncmp(k, "today_override_", 15)) return PTC_CONFIG_TODAY;
    if (!strcmp(k, "daily_buffer_minutes")) return PTC_CONFIG_BUFFER;
    if (!strncmp(k, "holiday_", 8) || !strncmp(k, "makeup_workday_", 15)) return PTC_CONFIG_HOLIDAY;
    if (!strncmp(k, "bedtime_", 8)) return PTC_CONFIG_BEDTIME;
    if (!strncmp(k, "eye_care_", 9)) return PTC_CONFIG_EYE;
    if (!strcmp(k, "force_docked") || !strncmp(k, "undocked_", 9)) return PTC_CONFIG_DOCK;
    return 0;
}

static bool confirmation(const char *k)
{
    return !strcmp(k, "bedtime_confirmation_version") || !strcmp(k, "bedtime_official_setting_confirmed_at") ||
        !strcmp(k, "bedtime_confirmed_environment") || !strcmp(k, "bedtime_overlay_risk_accepted");
}

static bool rule_mode(const cJSON *v, bool bedtime)
{
    return cJSON_IsString(v) && (!strcmp(v->valuestring, bedtime ? "inherit" : "limit") ||
        !strcmp(v->valuestring, bedtime ? "disabled" : "unlimited") ||
        (bedtime && !strcmp(v->valuestring, "custom")));
}

static uint16_t u16(const cJSON *o, const char *key, uint16_t fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? (uint16_t)v->valueint : fallback;
}

static bool flag(const cJSON *o, const char *key, bool fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return v ? cJSON_IsTrue(v) : fallback;
}

static void decode_day(const cJSON *o, const char *prefix, PtcDayRule *rule)
{
    char key[64];
    snprintf(key, sizeof(key), "%smode", prefix);
    const char *mode = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key));
    if (mode) rule->mode = !strcmp(mode, "unlimited") ? PTC_RULE_MODE_UNLIMITED : PTC_RULE_MODE_LIMIT;
    snprintf(key, sizeof(key), "%sminutes", prefix);
    rule->minutes = u16(o, key, rule->minutes);
}

static void decode_special(const cJSON *o, const char *prefix, PtcBedtimeSpecialRule *rule)
{
    char key[64];
    snprintf(key, sizeof(key), "%smode", prefix);
    const char *mode = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key));
    if (mode) rule->mode = !strcmp(mode, "custom") ? PTC_BEDTIME_OVERRIDE_CUSTOM :
        !strcmp(mode, "disabled") ? PTC_BEDTIME_OVERRIDE_DISABLED : PTC_BEDTIME_OVERRIDE_INHERIT;
    snprintf(key, sizeof(key), "%senabled", prefix); rule->window.enabled = flag(o, key, rule->window.enabled);
    snprintf(key, sizeof(key), "%sstart_minute", prefix); rule->window.start_minute = u16(o, key, rule->window.start_minute);
    snprintf(key, sizeof(key), "%send_minute", prefix); rule->window.end_minute = u16(o, key, rule->window.end_minute);
}

bool ptc_config_rules_decode(const cJSON *o, PtcRules *r)
{
    if (!ptc_config_rules_valid(o) || !r) return false;
    ptc_rules_default(r);
    const cJSON *week = cJSON_GetObjectItemCaseSensitive(o, "week");
    for (int i = 0; i < 7; ++i) decode_day(cJSON_GetArrayItem(week, i), "", &r->week[i]);
    r->today_override.present = flag(o, "today_override_present", false);
    r->today_override.day_index = u16(o, "today_override_day_index", 0);
    decode_day(o, "today_override_", &r->today_override.rule);
    r->scheduled_override.enabled = flag(o, "scheduled_override_enabled", false);
    r->scheduled_override.start_day_index = u16(o, "scheduled_override_start_day_index", 0);
    r->scheduled_override.end_day_index = u16(o, "scheduled_override_end_day_index", 0);
    decode_day(o, "scheduled_override_", &r->scheduled_override.rule);
    r->holiday_enabled = flag(o, "holiday_enabled", false);
    decode_day(o, "holiday_", &r->holiday_rule); decode_day(o, "makeup_workday_", &r->makeup_workday_rule);
    r->autonomy_policy.daily_buffer_minutes = u16(o, "daily_buffer_minutes", 0);
    r->eye_care.enabled = flag(o, "eye_care_enabled", false);
    r->eye_care.play_minutes = u16(o, "eye_care_play_minutes", 40);
    r->eye_care.rest_minutes = u16(o, "eye_care_rest_minutes", 10);
    r->dock_policy.force_docked = flag(o, "force_docked", false);
    r->dock_policy.undocked_limit_enabled = flag(o, "undocked_limit_enabled", false);
    r->dock_policy.undocked_daily_minutes = u16(o, "undocked_daily_minutes", 30);
    r->bedtime.enabled = flag(o, "bedtime_enabled", false);
    const cJSON *bw = cJSON_GetObjectItemCaseSensitive(o, "bedtime_week");
    if (bw) for (int i = 0; i < 7; ++i) {
        const cJSON *v = cJSON_GetArrayItem(bw, i);
        r->bedtime.week[i].enabled = flag(v, "enabled", false);
        r->bedtime.week[i].start_minute = u16(v, "start_minute", 0);
        r->bedtime.week[i].end_minute = u16(v, "end_minute", 0);
    }
    r->bedtime.calendar_enabled = flag(o, "bedtime_calendar_enabled", r->bedtime.calendar_enabled);
    decode_special(o, "bedtime_holiday_", &r->bedtime.holiday_rule);
    decode_special(o, "bedtime_makeup_", &r->bedtime.makeup_workday_rule);
    r->bedtime.scheduled_override.present = flag(o, "bedtime_scheduled_present", false);
    r->bedtime.scheduled_override.start_day_index = u16(o, "bedtime_scheduled_start_day_index", 0);
    r->bedtime.scheduled_override.end_day_index = u16(o, "bedtime_scheduled_end_day_index", 0);
    decode_special(o, "bedtime_scheduled_", &r->bedtime.scheduled_override.rule);
    return ptc_scheduled_override_is_valid(&r->scheduled_override) && ptc_autonomy_policy_is_valid(&r->autonomy_policy) &&
        ptc_eye_care_policy_is_valid(&r->eye_care) && ptc_bedtime_policy_is_valid(&r->bedtime);
}

bool ptc_config_rules_valid(const cJSON *o)
{
    if (!cJSON_IsObject(o) || !number(o, "version", 1, 2, false)) return false;
    const cJSON *week = cJSON_GetObjectItemCaseSensitive(o, "week");
    if (!cJSON_IsArray(week) || cJSON_GetArraySize(week) != 7) return false;
    for (const cJSON *v = week->child; v; v = v->next)
        if (!cJSON_IsObject(v) || !rule_mode(cJSON_GetObjectItemCaseSensitive(v, "mode"), false) ||
            !number(v, "minutes", 0, 1440, false)) return false;
    for (const cJSON *v = o->child; v; v = v->next) {
        const char *k = v->string;
        size_t len = strlen(k);
        if (confirmation(k)) continue;
        if (len >= 8 && !strcmp(k + len - 8, "_minutes")) {
            double max = !strcmp(k, "eye_care_play_minutes") ? 240 : !strcmp(k, "eye_care_rest_minutes") ? 60 : 1440;
            double min = !strncmp(k, "eye_care_", 9) ? 1 : 0;
            if (!number(o, k, min, max, false)) return false;
            if (!strcmp(k, "daily_buffer_minutes") && v->valueint != 0 && v->valueint != 5 && v->valueint != 10 && v->valueint != 15) return false;
        } else if (strstr(k, "_day_index")) {
            if (!number(o, k, 0, UINT16_MAX, false)) return false;
        } else if (len >= 7 && !strcmp(k + len - 7, "_minute")) {
            if (!number(o, k, 0, 1439, false)) return false;
        } else if (len >= 5 && !strcmp(k + len - 5, "_mode")) {
            if (!rule_mode(v, !strncmp(k, "bedtime_", 8))) return false;
        } else if (strstr(k, "_enabled") || strstr(k, "_present") || !strcmp(k, "force_docked")) {
            if (!cJSON_IsBool(v)) return false;
        }
    }
    const cJSON *bw = cJSON_GetObjectItemCaseSensitive(o, "bedtime_week");
    if (bw) {
        if (!cJSON_IsArray(bw) || cJSON_GetArraySize(bw) != 7) return false;
        for (const cJSON *v = bw->child; v; v = v->next)
            if (!cJSON_IsObject(v) || !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(v, "enabled")) ||
                !number(v, "start_minute", 0, 1439, false) || !number(v, "end_minute", 0, 1439, false)) return false;
    }
    if (cJSON_HasObjectItem(o, "eye_care_enabled") &&
        (!number(o, "eye_care_play_minutes", 1, 240, false) || !number(o, "eye_care_rest_minutes", 1, 60, false))) return false;
    if (cJSON_HasObjectItem(o, "bedtime_enabled") &&
        (!bw || !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(o, "bedtime_calendar_enabled")) ||
         !rule_mode(cJSON_GetObjectItemCaseSensitive(o, "bedtime_holiday_mode"), true) ||
         !rule_mode(cJSON_GetObjectItemCaseSensitive(o, "bedtime_makeup_mode"), true) ||
         !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(o, "bedtime_scheduled_present")))) return false;
    return true;
}

bool ptc_config_pin_valid(const cJSON *o)
{
    const char *hash = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "pin_hash"));
    const char *salt = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "pin_salt"));
    const char *algorithm = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "hash"));
    return cJSON_IsObject(o) && number(o, "version", 1, 1, false) && hash && salt &&
        ((!hash[0] && !salt[0]) || (hex(hash, 64) && hex(salt, 32) && algorithm && !strcmp(algorithm, "hmac-sha256")));
}

bool ptc_config_preferences_valid(const cJSON *o)
{
    if (!cJSON_IsObject(o) || !number(o, "version", 1, 1, false)) return false;
    const char *device = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "device_id"));
    if (!ptc_device_id_valid(device)) return false;
    for (const unsigned char *p = (const unsigned char *)device; *p; ++p)
        if (*p < 33 || *p > 126 || *p == '"' || *p == '\\') return false;
    const char *theme = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "theme"));
    const char *lang = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "ui_language"));
    if ((theme && strcmp(theme, "system") && strcmp(theme, "light") && strcmp(theme, "dark")) ||
        (lang && strcmp(lang, "system") && strcmp(lang, "zh-Hans") && strcmp(lang, "zh-Hant") && strcmp(lang, "en"))) return false;
    if ((cJSON_HasObjectItem(o, "theme") && !theme) || (cJSON_HasObjectItem(o, "ui_language") && !lang)) return false;
    const char *url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "pairing_base_url"));
    if (cJSON_HasObjectItem(o, "pairing_base_url") && (!url || !ptc_pairing_base_url_valid(url))) return false;
    const cJSON *mask = cJSON_GetObjectItemCaseSensitive(o, "parent_shortcut_mask");
    if (mask) {
        char *end;
        if (!cJSON_IsString(mask) || !mask->valuestring[0] || strlen(mask->valuestring) > 16) return false;
        unsigned long long value = strtoull(mask->valuestring, &end, 16);
        /* Same supported buttons as CUSTOM_SHORTCUT_VALID_MASK (X/Y, shoulders, +/- and directions). */
        if (*end || !value || (value & ~0xffccULL)) return false;
    }
    return boolean(o, "custom_shortcut_enabled") && boolean(o, "show_parent_shortcut_hint") &&
        boolean(o, "sound_effects_enabled") && number(o, "max_add_minutes", 1, 1440, true) &&
        number(o, "default_request_timeout_ms", 1, 2147483647, true);
}

static bool allowed_path(const char *p)
{
    static const char *fixed[] = {"config.json", "rules.json", "auth.json", "credentials.json", "calendars/catalog.json", "calendars/active.json"};
    if (!p) return false;
    for (size_t i = 0; i < 6; ++i) if (!strcmp(p, fixed[i])) return true;
    return !strncmp(p, "calendars/library/", 18) && ptc_calendar_file_name_valid(p + 18) && !strchr(p + 18, '/');
}

static bool descriptor_valid(const cJSON *m)
{
    const char *format = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(m, "format"));
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(m, "files");
    const char *device = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(m, "source_device"));
    if (!ptc_device_id_valid(device) || !format || strcmp(format, "playwise-config-backup") || !number(m, "version", 1, 1, false) ||
        !number(m, "created_at", 0, 9007199254740991.0, false) || !cJSON_IsArray(files) ||
        cJSON_GetArraySize(files) < 6 || cJSON_GetArraySize(files) > (int)PTC_CONFIG_BACKUP_MAX_FILES) return false;
    for (const cJSON *a = files->child; a; a = a->next) {
        const char *path = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(a, "path"));
        const char *sha = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(a, "sha256"));
        if (!allowed_path(path) || !hex(sha, 64) || !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(a, "exists"))) return false;
        for (const cJSON *b = a->next; b; b = b->next) {
            const char *other = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(b, "path"));
            if (other && !strcmp(path, other)) return false;
        }
    }
    static const char *required[] = {"config.json", "rules.json", "auth.json", "credentials.json", "calendars/catalog.json", "calendars/active.json"};
    for (size_t i = 0; i < 6; ++i) {
        bool found = false;
        for (const cJSON *a = files->child; a; a = a->next)
            if (!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(a, "path")), required[i])) {
                found = i >= 4 || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "exists")); break;
            }
        if (!found) return false;
    }
    return true;
}

static bool add_file(PtcStorage *s, const char *root, const char *stage, cJSON *files, const char *relative)
{
    for (const cJSON *f = files->child; f; f = f->next)
        if (!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "path")), relative)) return true;
    if (cJSON_GetArraySize(files) >= (int)PTC_CONFIG_BACKUP_MAX_FILES) return false;
    char path[512], target[512], text[PTC_CONFIG_BACKUP_FILE_SIZE], digest[65];
    PtcStorageMetadata meta;
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    if (!s->vtable->metadata(s, path, &meta)) return false;
    bool exists = meta.type == PTC_STORAGE_ENTRY_FILE;
    if (!exists && meta.type != PTC_STORAGE_ENTRY_MISSING) return false;
    text[0] = '\0';
    if (exists && !s->vtable->read_text(s, path, text, sizeof(text))) return false;
    if (exists && (!strcmp(relative, "config.json") || !strcmp(relative, "rules.json") || !strcmp(relative, "auth.json"))) {
        cJSON *json = cJSON_ParseWithOpts(text, NULL, true);
        if (!cJSON_IsObject(json) || !ptc_config_json_valid(json, 0)) { cJSON_Delete(json); return false; }
        for (cJSON *v = json->child; v;) {
            cJSON *next = v->next;
            bool keep = true;
            if (!strcmp(relative, "config.json")) {
                static const char *keys[] = {"version", "device_id", "max_add_minutes", "default_request_timeout_ms", "pairing_base_url",
                    "theme", "ui_language", "sound_effects_enabled", "parent_shortcut_mask", "custom_shortcut_enabled", "show_parent_shortcut_hint"};
                keep = false;
                for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) if (!strcmp(v->string, keys[i])) keep = true;
            } else if (!strcmp(relative, "rules.json")) keep = !confirmation(v->string);
            else keep = !strcmp(v->string, "version") || !strcmp(v->string, "hash") ||
                !strcmp(v->string, "pin_hash") || !strcmp(v->string, "pin_salt");
            if (!keep) cJSON_DeleteItemFromObjectCaseSensitive(json, v->string);
            v = next;
        }
        char *clean = cJSON_PrintUnformatted(json);
        bool valid = clean && strlen(clean) < sizeof(text);
        if (valid) memcpy(text, clean, strlen(clean) + 1);
        free(clean); cJSON_Delete(json);
        if (!valid) return false;
    }
    ptc_calendar_sha256_hex(text, strlen(text), digest);
    snprintf(target, sizeof(target), "%s/%s", stage, relative);
    if (exists && !s->vtable->write_text_atomic(s, target, text)) return false;
    cJSON *f = cJSON_CreateObject();
    if (!f || !cJSON_AddStringToObject(f, "path", relative) || !cJSON_AddStringToObject(f, "sha256", digest) ||
        !cJSON_AddBoolToObject(f, "exists", exists) || !cJSON_AddItemToArray(files, f)) { cJSON_Delete(f); return false; }
    return true;
}

bool ptc_config_stage_create(PtcStorage *s, const char *root, const char *stage, int64_t now)
{
    static const char *fixed[] = {"config.json", "rules.json", "auth.json", "credentials.json", "calendars/catalog.json", "calendars/active.json"};
    cJSON *m = cJSON_CreateObject();
    cJSON *files = cJSON_AddArrayToObject(m, "files");
    cJSON *config = ptc_config_read_json(s, root, "config.json", 4096);
    const char *device = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(config, "device_id"));
    bool ok = m && files && device != NULL;
    ok = ok && cJSON_AddStringToObject(m, "format", "playwise-config-backup") && cJSON_AddNumberToObject(m, "version", 1) &&
        cJSON_AddNumberToObject(m, "created_at", (double)now) && cJSON_AddStringToObject(m, "source_device", device);
    for (size_t i = 0; ok && i < 6; ++i) ok = add_file(s, root, stage, files, fixed[i]);
    for (size_t i = 4; ok && i < 6; ++i) {
        PtcCalendarIndex *index = malloc(sizeof(*index));
        char digest[65];
        ok = index && ptc_calendar_index_load(s, root, i == 5, index, digest);
        for (size_t j = 0; ok && j < index->count; ++j) {
            char relative[128];
            snprintf(relative, sizeof(relative), "calendars/library/%s-%u-%s.json", index->entries[j].region_id,
                index->entries[j].year, index->entries[j].sha256);
            ok = add_file(s, root, stage, files, relative);
        }
        free(index);
    }
    if (ok) ok = ptc_config_write_json(s, stage, "manifest.json", m) && ptc_config_stage_validate(s, stage, NULL);
    cJSON_Delete(config); cJSON_Delete(m);
    return ok;
}

bool ptc_config_manifest_digest(PtcStorage *s, const char *stage, char digest[65])
{
    char path[512];
    char *text = malloc(PTC_CONFIG_BACKUP_MANIFEST_SIZE);
    snprintf(path, sizeof(path), "%s/manifest.json", stage);
    bool ok = text && s->vtable->read_text(s, path, text, PTC_CONFIG_BACKUP_MANIFEST_SIZE);
    if (ok) ptc_calendar_sha256_hex(text, strlen(text), digest);
    free(text); return ok;
}

bool ptc_config_stage_validate(PtcStorage *s, const char *stage, const char *expected)
{
    char digest[65], text[PTC_CONFIG_BACKUP_FILE_SIZE], path[512];
    if (!ptc_config_manifest_digest(s, stage, digest) || (expected && strcmp(expected, digest))) return false;
    cJSON *m = ptc_config_read_json(s, stage, "manifest.json", PTC_CONFIG_BACKUP_MANIFEST_SIZE);
    bool ok = descriptor_valid(m);
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(m, "files");
    for (const cJSON *f = ok ? files->child : NULL; ok && f; f = f->next) {
        const char *relative = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "path"));
        bool exists = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(f, "exists"));
        snprintf(path, sizeof(path), "%s/%s", stage, relative);
        text[0] = '\0';
        if (exists && !s->vtable->read_text(s, path, text, sizeof(text))) { ok = false; break; }
        ptc_calendar_sha256_hex(text, strlen(text), digest);
        if (strcmp(digest, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "sha256")))) { ok = false; break; }
        if (!exists) continue;
        cJSON *o = cJSON_ParseWithOpts(text, NULL, true);
        ok = cJSON_IsObject(o) && ptc_config_json_valid(o, 0);
        if (ok && !strcmp(relative, "rules.json")) { PtcRules rules; ok = ptc_config_rules_decode(o, &rules); }
        if (ok && !strcmp(relative, "config.json")) ok = ptc_config_preferences_valid(o) &&
            !strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "device_id")),
                cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(m, "source_device")));
        if (ok && !strcmp(relative, "auth.json")) ok = ptc_config_pin_valid(o);
        if (ok && !strcmp(relative, "credentials.json")) {
            const char *secret = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "grant_secret"));
            ok = number(o, "version", 1, 1, false) && ptc_grant_secret_valid(secret);
            for (const unsigned char *p = (const unsigned char *)secret; ok && *p; ++p)
                if (*p < 33 || *p > 126) ok = false;
        }
        if (ok && !strncmp(relative, "calendars/library/", 18)) {
            PtcImportedCalendarYear *year = malloc(sizeof(*year));
            ok = year && ptc_holiday_calendar_parse_import(text, strlen(text), year);
            if (ok) {
                char canonical[128];
                snprintf(canonical, sizeof(canonical), "calendars/library/%s-%u-%s.json", year->region_id, year->year, digest);
                ok = !strcmp(relative, canonical);
            }
            free(year);
        }
        cJSON_Delete(o);
    }
    for (unsigned i = 0; ok && i < 2; ++i) {
        PtcCalendarIndex *index = malloc(sizeof(*index));
        ok = index && ptc_calendar_index_load(s, stage, i == 1, index, digest);
        for (size_t j = 0; ok && j < index->count; ++j) {
            char relative[128]; bool listed = false;
            snprintf(relative, sizeof(relative), "calendars/library/%s-%u-%s.json", index->entries[j].region_id,
                index->entries[j].year, index->entries[j].sha256);
            for (const cJSON *f = files->child; f; f = f->next)
                if (!strcmp(relative, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "path"))) &&
                    cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(f, "exists"))) listed = true;
            ok = listed;
        }
        if (ok && i == 1) ok = ptc_calendar_selection_validate(s, stage, index);
        if (ok && i == 0) {
            PtcCalendarIndex *region = malloc(sizeof(*region));
            ok = region != NULL;
            for (size_t j = 0; ok && j < index->count; ++j)
                ok = ptc_calendar_build_selection(index, index->entries[j].region_id, region) &&
                    ptc_calendar_selection_validate(s, stage, region);
            free(region);
        }
        free(index);
    }
    cJSON_Delete(m);
    return ok;
}

bool ptc_config_archive_save(PtcStorage *s, const char *stage, const char *path)
{
    if (!ptc_config_stage_validate(s, stage, NULL)) return false;
    cJSON *m = ptc_config_read_json(s, stage, "manifest.json", PTC_CONFIG_BACKUP_MANIFEST_SIZE);
    cJSON *files = cJSON_GetObjectItemCaseSensitive(m, "files");
    char file[512], text[PTC_CONFIG_BACKUP_FILE_SIZE];
    bool ok = m != NULL;
    for (cJSON *f = files ? files->child : NULL; ok && f; f = f->next) {
        text[0] = '\0';
        snprintf(file, sizeof(file), "%s/%s", stage, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "path")));
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(f, "exists"))) ok = s->vtable->read_text(s, file, text, sizeof(text));
        if (ok) ok = cJSON_AddStringToObject(f, "content", text) != NULL;
    }
    char *rendered = ok ? cJSON_PrintUnformatted(m) : NULL;
    ok = rendered && strlen(rendered) < PTC_CONFIG_BACKUP_MAX_BYTES && s->vtable->write_text_atomic(s, path, rendered);
    free(rendered); cJSON_Delete(m); return ok;
}

bool ptc_config_archive_load(PtcStorage *s, const char *path, const char *stage, char digest[65])
{
    char *text = malloc(PTC_CONFIG_BACKUP_MAX_BYTES + 1);
    cJSON *m = NULL;
    if (text && s->vtable->read_text(s, path, text, PTC_CONFIG_BACKUP_MAX_BYTES + 1)) m = cJSON_ParseWithOpts(text, NULL, true);
    free(text);
    bool ok = m && ptc_config_json_valid(m, 0) && descriptor_valid(m);
    cJSON *files = cJSON_GetObjectItemCaseSensitive(m, "files");
    char actual[65], target[512];
    /* Validate the entire archive before creating any staging files. */
    for (const cJSON *f = ok ? files->child : NULL; ok && f; f = f->next) {
        const char *content = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "content"));
        if (!content || strlen(content) >= PTC_CONFIG_BACKUP_FILE_SIZE ||
            (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(f, "exists")) && content[0])) { ok = false; break; }
        ptc_calendar_sha256_hex(content, strlen(content), actual);
        ok = !strcmp(actual, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "sha256")));
    }
    for (cJSON *f = ok ? files->child : NULL; ok && f; f = f->next) {
        snprintf(target, sizeof(target), "%s/%s", stage, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "path")));
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(f, "exists")))
            ok = s->vtable->write_text_atomic(s, target, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(f, "content")));
        else if (s->vtable->exists(s, target)) ok = s->vtable->remove_path(s, target);
        cJSON_DeleteItemFromObjectCaseSensitive(f, "content");
    }
    if (ok) ok = ptc_config_write_json(s, stage, "manifest.json", m) && ptc_config_stage_validate(s, stage, NULL) &&
        ptc_config_manifest_digest(s, stage, digest);
    cJSON_Delete(m); return ok;
}

cJSON *ptc_config_merge_rules(const cJSON *current, const cJSON *source, unsigned groups, uint16_t day)
{
    if (!ptc_config_rules_valid(current) || !ptc_config_rules_valid(source)) return NULL;
    const cJSON *date = cJSON_GetObjectItemCaseSensitive(source, "today_override_day_index");
    if ((groups & PTC_CONFIG_TODAY) && (!date || date->valueint != day)) return NULL;
    cJSON *out = cJSON_Duplicate(current, true);
    for (cJSON *v = out ? out->child : NULL; v;) {
        cJSON *next = v->next;
        if ((ptc_config_rule_group(v->string) & groups) && !confirmation(v->string))
            cJSON_DeleteItemFromObjectCaseSensitive(out, v->string);
        v = next;
    }
    for (const cJSON *v = source->child; out && v; v = v->next)
        if ((ptc_config_rule_group(v->string) & groups) && !confirmation(v->string))
        {
            cJSON *copy = cJSON_Duplicate(v, true);
            if (!copy || !cJSON_AddItemToObject(out, v->string, copy)) { cJSON_Delete(copy); cJSON_Delete(out); return NULL; }
        }
    if (out) { cJSON_DeleteItemFromObjectCaseSensitive(out, "version");
        if (!cJSON_AddNumberToObject(out, "version", 2)) { cJSON_Delete(out); return NULL; } }
    return out;
}

cJSON *ptc_config_merge_preferences(const cJSON *current, const cJSON *source, unsigned groups)
{
    static const char *prefs[] = {"theme", "ui_language", "sound_effects_enabled", "parent_shortcut_mask", "custom_shortcut_enabled", "show_parent_shortcut_hint"};
    static const char *pairing[] = {"device_id", "pairing_base_url", "max_add_minutes", "default_request_timeout_ms"};
    if (!ptc_config_preferences_valid(current) || !ptc_config_preferences_valid(source)) return NULL;
    cJSON *out = cJSON_Duplicate(current, true);
    for (unsigned group = 0; out && group < 2; ++group) {
        const char **keys = group ? pairing : prefs;
        size_t count = group ? 4 : 6;
        if (!(groups & (group ? PTC_CONFIG_PAIRING : PTC_CONFIG_PREFS))) continue;
        for (size_t i = 0; i < count; ++i) {
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(source, keys[i]);
            cJSON_DeleteItemFromObjectCaseSensitive(out, keys[i]);
            if (v) {
                cJSON *copy = cJSON_Duplicate(v, true);
                if (!copy || !cJSON_AddItemToObject(out, keys[i], copy)) { cJSON_Delete(copy); cJSON_Delete(out); return NULL; }
            }
        }
    }
    return out;
}
