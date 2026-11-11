#include "notification_test.h"
#include "../third_party/cjson/cJSON.h"
#include <stdio.h>
#include <string.h>

#define NOTICE_DIR "sdmc:/config/ultrahand/notifications/"

static const char *string(const cJSON *j, const char *key)
{
    const char *v = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j, key));
    return v ? v : "";
}

static bool valid_name(const char *name)
{
    const size_t prefix = sizeof("playwise-test-") - 1;
    if (strlen(name) != prefix + 16 + 7 || strncmp(name, "playwise-test-", prefix) ||
        strcmp(name + prefix + 16, ".notify")) return false;
    for (size_t i = prefix; i < prefix + 16; ++i)
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
    return true;
}

static bool remove_owned(PtcStorage *s, const char *path, const char *payload)
{
    PtcStorageMetadata m;
    char text[1024];
    if (!s->vtable->metadata(s, path, &m)) return false;
    return m.type == PTC_STORAGE_ENTRY_MISSING || (m.type == PTC_STORAGE_ENTRY_FILE &&
        s->vtable->read_text(s, path, text, sizeof(text)) && !strcmp(text, payload) && s->vtable->remove_path(s, path));
}

bool ptc_notification_test_cleanup(PtcNotificationTest *t, PtcStorage *s, const char *root)
{
    char journal[256], text[2048], stage[280];
    PtcStorageMetadata m;
    snprintf(journal, sizeof(journal), "%s/support/notification-test-owned.json", root);
    if (!s->vtable->metadata(s, journal, &m)) return false;
    if (m.type == PTC_STORAGE_ENTRY_MISSING) {
        if (t->pending) return false;
        t->initialized = true;
        return true;
    }
    if (m.type != PTC_STORAGE_ENTRY_FILE || !s->vtable->read_text(s, journal, text, sizeof(text))) return false;
    cJSON *j = cJSON_ParseWithOpts(text, NULL, true);
    const char *name = string(j, "file"), *payload = string(j, "payload");
    cJSON *p = cJSON_ParseWithOpts(payload, NULL, true);
    bool ok = cJSON_IsObject(j) && cJSON_GetArraySize(j) == 2 && valid_name(name) &&
        strlen(payload) < sizeof(t->payload) && !strcmp(string(p, "pw_owner"), "test-v1") &&
        !strcmp(string(p, "pw_file"), name);
    if (ok && t->pending) ok = !strcmp(t->payload, payload) &&
        !strcmp(t->path + strlen(NOTICE_DIR), name);
    if (ok) {
        snprintf(t->path, sizeof(t->path), NOTICE_DIR "%s", name);
        snprintf(t->payload, sizeof(t->payload), "%s", payload);
        t->pending = true;
        snprintf(stage, sizeof(stage), "%s.stage", t->path);
        ok = remove_owned(s, t->path, t->payload) && remove_owned(s, stage, t->payload) &&
            remove_owned(s, journal, text);
    }
    cJSON_Delete(p); cJSON_Delete(j);
    if (ok) { t->pending = false; t->initialized = true; }
    return ok;
}

void ptc_notification_test_poll(PtcNotificationTest *t, PtcStorage *s, const char *root, uint64_t ms)
{
    if (t->blocked || (t->initialized && !t->pending) || ms < t->retry_ms ||
        (t->pending && ms < t->expires_ms)) return;
    if (!ptc_notification_test_cleanup(t, s, root)) {
        t->retry_ms = ms + 10000;
        if (++t->failures >= 5) t->blocked = true;
    }
}

bool ptc_notification_test_send(PtcNotificationTest *t, PtcStorage *s, const char *root,
    uint64_t ms, uint64_t random_id, const char *title, const char *body)
{
    if (t->blocked || t->pending || (t->sent && (ms < t->last_send_ms || ms - t->last_send_ms < 10000))) return false;
    t->last_send_ms = ms; t->sent = true;
    if (!ptc_notification_test_cleanup(t, s, root)) return false;
    char name[64], journal[256], stage[280];
    snprintf(name, sizeof(name), "playwise-test-%016llx.notify", (unsigned long long)random_id);
    snprintf(t->path, sizeof(t->path), NOTICE_DIR "%s", name);
    snprintf(stage, sizeof(stage), "%s.stage", t->path);
    snprintf(journal, sizeof(journal), "%s/support/notification-test-owned.json", root);
    PtcStorageMetadata m;
    /* A random collision or unknown file is never overwritten. */
    if (!s->vtable->metadata(s, t->path, &m) || m.type != PTC_STORAGE_ENTRY_MISSING ||
        !s->vtable->metadata(s, stage, &m) || m.type != PTC_STORAGE_ENTRY_MISSING) return false;
    cJSON *p = cJSON_CreateObject();
    if (!p) return false;
    bool ok = cJSON_AddStringToObject(p, "title", title) && cJSON_AddStringToObject(p, "text", body) &&
        cJSON_AddNumberToObject(p, "font_size", 26) && cJSON_AddNumberToObject(p, "duration", 10000) &&
        cJSON_AddStringToObject(p, "show_time", "false") && cJSON_AddNumberToObject(p, "priority", 30) &&
        cJSON_AddStringToObject(p, "pw_owner", "test-v1") && cJSON_AddStringToObject(p, "pw_file", name);
    char *payload = ok ? cJSON_PrintUnformatted(p) : NULL;
    ok = payload && strlen(payload) < sizeof(t->payload);
    if (ok) snprintf(t->payload, sizeof(t->payload), "%s", payload);
    cJSON_free(payload); cJSON_Delete(p);
    if (!ok) return false;
    cJSON *j = cJSON_CreateObject();
    if (!j) return false;
    ok = cJSON_AddStringToObject(j, "file", name) && cJSON_AddStringToObject(j, "payload", t->payload);
    char *record = ok ? cJSON_PrintUnformatted(j) : NULL;
    /* Treat a failed write as possibly published. Retain ownership for cleanup. */
    t->pending = true; t->expires_ms = ms + 2000; t->last_send_ms = ms; t->sent = true;
    ok = record && s->vtable->write_text_atomic(s, journal, record) &&
        s->vtable->write_text_atomic(s, stage, t->payload) && s->vtable->rename_path(s, stage, t->path);
    cJSON_free(record); cJSON_Delete(j);
    return ok;
}
