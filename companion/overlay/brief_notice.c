#include "brief_notice.h"
#include "../../third_party/cjson/cJSON.h"
#include <stdio.h>
#include <string.h>

static const char *string(const cJSON *object, const char *key)
{
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, key));
    return value ? value : "";
}

static bool unique_object(const cJSON *object)
{
    if (!cJSON_IsObject(object)) return false;
    for (const cJSON *a = object->child; a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) return false;
    return true;
}

static bool valid_name(const char *name, const char *prefix, size_t hex_count)
{
    size_t begin = strlen(prefix);
    if (strlen(name) != begin + hex_count + 7 || strncmp(name, prefix, begin) ||
        strcmp(name + begin + hex_count, ".notify")) return false;
    for (size_t i = begin; i < begin + hex_count; ++i) {
        if (hex_count == 33 && i == begin + 16) {
            if (name[i] != '-') return false;
        } else if (!((name[i] >= '0' && name[i] <= '9') ||
            (name[i] >= 'a' && name[i] <= 'f'))) return false;
    }
    return true;
}

static bool decimal(const char *text, uint64_t *value)
{
    *value = 0;
    if (!text[0] || (text[0] == '0' && text[1])) return false;
    for (size_t i = 0; text[i]; ++i) {
        if (text[i] < '0' || text[i] > '9' ||
            *value > (UINT64_MAX - (unsigned)(text[i] - '0')) / 10) return false;
        *value = *value * 10 + (unsigned)(text[i] - '0');
    }
    return true;
}

static void read_notice(PtcOverlayBriefNotice *notice, PtcStorage *storage,
    const char *root, const char *release, const char *boot, uint64_t now_ms,
    bool allowed, unsigned slot)
{
    static const char *journals[] = {"reason-notification-owned.json", "notification-test-owned.json"};
    char path[320], journal[4096], observed[1536];
    snprintf(path, sizeof(path), "%s/support/%s", root, journals[slot]);
    if (!storage->vtable->read_text(storage, path, journal, sizeof(journal))) return;
    cJSON *record = cJSON_ParseWithOpts(journal, NULL, true);
    const char *name = string(record, "file"), *payload = string(record, "payload");
    cJSON *body = cJSON_ParseWithOpts(payload, NULL, true);
    const char *title = string(body, "title"), *text = string(body, "text");
    bool valid = unique_object(record) && cJSON_GetArraySize(record) == 2 &&
        unique_object(body) && valid_name(name, slot ? "playwise-test-" : "playwise-reason-", slot ? 16 : 33) &&
        !strcmp(string(body, "pw_file"), name) &&
        !strcmp(string(body, "pw_owner"), slot ? "test-v1" : "reason-v1") &&
        title[0] && strlen(title) < sizeof(notice->title) && text[0] && strlen(text) < sizeof(notice->text);
    if (valid && !slot) valid = strlen(boot) == 16 &&
        !strncmp(name + strlen("playwise-reason-"), boot, 16) &&
        !strcmp(string(body, "pw_release"), release);
    const cJSON *font = cJSON_GetObjectItemCaseSensitive(body, "font_size");
    const cJSON *duration = cJSON_GetObjectItemCaseSensitive(body, "duration");
    uint64_t issued = 0, expires = 0;
    valid = valid && cJSON_IsNumber(font) && font->valuedouble == 26 &&
        cJSON_IsNumber(duration) && duration->valuedouble == 10000 &&
        decimal(string(body, "pw_issued_ms"), &issued) &&
        decimal(string(body, "pw_expires_ms"), &expires) &&
        expires > issued && expires - issued == 2000 && now_ms >= issued && now_ms < expires;
    if (valid && strcmp(name, notice->seen[slot])) {
        snprintf(path, sizeof(path), "sdmc:/config/ultrahand/notifications/%s", name);
        /* Journal precedes rename. Never draw a stage or a different file. */
        if (storage->vtable->read_text(storage, path, observed, sizeof(observed)) && !strcmp(observed, payload)) {
            snprintf(notice->seen[slot], sizeof(notice->seen[slot]), "%s", name);
            if (allowed && !notice->active && now_ms <= UINT64_MAX - 10000) {
                snprintf(notice->title, sizeof(notice->title), "%s", title);
                snprintf(notice->text, sizeof(notice->text), "%s", text);
                notice->expires_ms = now_ms + 10000;
                notice->active = true;
            }
        }
    }
    cJSON_Delete(body);
    cJSON_Delete(record);
}

void ptc_overlay_brief_poll(PtcOverlayBriefNotice *notice, PtcStorage *storage,
    const char *root, const char *release, const char *boot, uint64_t now_ms,
    bool allowed, bool dismiss)
{
    if (!allowed || dismiss || now_ms < notice->last_ms || now_ms >= notice->expires_ms) {
        notice->active = false;
        notice->title[0] = notice->text[0] = '\0';
    }
    notice->last_ms = now_ms;
    if (!storage) return;
    for (unsigned slot = 0; slot < 2; ++slot)
        read_notice(notice, storage, root, release, boot, now_ms, allowed && !dismiss, slot);
}
