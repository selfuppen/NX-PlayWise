#include "reason_detail.h"
#include "../third_party/cjson/cJSON.h"
#include <stdio.h>
#include <string.h>

static bool path(char out[256], const char *root, const char *kind)
{
    if (strcmp(kind, "ready") && strcmp(kind, "request") && strcmp(kind, "ack")) return false;
    int n = snprintf(out, 256, "%s/support/reason-detail-%s.json", root, kind);
    return n > 0 && n < 256;
}

static const char *string(const cJSON *j, const char *key)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j, key));
    return s ? s : "";
}

static bool number(const cJSON *j, const char *key, uint64_t *out)
{
    const char *s = string(j, key);
    uint64_t n = 0;
    if (!*s || (s[0] == '0' && s[1])) return false;
    for (; *s; ++s) {
        unsigned digit = (unsigned)(*s - '0');
        if (digit > 9 || n > (UINT64_MAX - digit) / 10) return false;
        n = n * 10 + digit;
    }
    *out = n;
    return true;
}

static bool hex(const char *s, size_t n)
{
    if (strlen(s) != n) return false;
    for (; *s; ++s) if (!strchr("0123456789abcdef", *s)) return false;
    return true;
}

bool ptc_reason_detail_read(PtcStorage *s, const char *root, const char *kind, PtcReasonDetail *out)
{
    char file[256], text[1024];
    memset(out, 0, sizeof(*out));
    if (!path(file, root, kind) || !s->vtable->read_text(s, file, text, sizeof(text))) return false;
    cJSON *j = cJSON_ParseWithOpts(text, NULL, true);
    bool ok = cJSON_IsObject(j) && cJSON_GetArraySize(j) == 12;
    for (const cJSON *a = j ? j->child : NULL; ok && a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) ok = false;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "version");
    uint64_t day = 0, reason = 0;
    ok = ok && cJSON_IsNumber(v) && v->valuedouble == 1 &&
        !strcmp(string(j, "kind"), kind) && strlen(string(j, "release")) > 0 &&
        strlen(string(j, "release")) < sizeof(out->release) && hex(string(j, "boot"), 16) &&
        (!strcmp(kind, "ack") ? hex(string(j, "owner"), 32) : !string(j, "owner")[0]) &&
        number(j, "ui_instance", &out->ui_instance) && out->ui_instance &&
        number(j, "sequence", &out->sequence) && number(j, "issued_ms", &out->issued_ms) &&
        number(j, "expires_ms", &out->expires_ms) && number(j, "instance", &out->instance) &&
        number(j, "day", &day) && day <= UINT16_MAX && number(j, "reason", &reason) && reason <= PTC_REASON_DOCK;
    if (ok) {
        snprintf(out->release, sizeof(out->release), "%s", string(j, "release"));
        snprintf(out->boot, sizeof(out->boot), "%s", string(j, "boot"));
        snprintf(out->owner, sizeof(out->owner), "%s", string(j, "owner"));
        out->day = (uint16_t)day; out->reason = (PtcRestrictionReason)reason;
        ok = !strcmp(kind, "ready") ? !out->sequence && !out->instance && !out->reason :
            out->sequence && out->instance && out->reason > PTC_REASON_NONE;
    }
    cJSON_Delete(j);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

bool ptc_reason_detail_write(PtcStorage *s, const char *root, const char *kind, const PtcReasonDetail *v)
{
    char file[256], text[1024];
    if (!path(file, root, kind)) return false;
    cJSON *j = cJSON_CreateObject();
    if (!j) return false;
    bool ok = cJSON_AddNumberToObject(j, "version", 1) && cJSON_AddStringToObject(j, "kind", kind) &&
        cJSON_AddStringToObject(j, "release", v->release) && cJSON_AddStringToObject(j, "boot", v->boot) &&
        cJSON_AddStringToObject(j, "owner", v->owner);
    const char *keys[] = {"ui_instance", "sequence", "issued_ms", "expires_ms", "instance", "day", "reason"};
    uint64_t values[] = {v->ui_instance, v->sequence, v->issued_ms, v->expires_ms, v->instance, v->day, v->reason};
    for (unsigned i = 0; i < 7; ++i) {
        char value[32]; snprintf(value, sizeof(value), "%llu", (unsigned long long)values[i]);
        ok = ok && cJSON_AddStringToObject(j, keys[i], value);
    }
    char *json = ok ? cJSON_PrintUnformatted(j) : NULL;
    ok = json && strlen(json) < sizeof(text);
    if (ok) snprintf(text, sizeof(text), "%s", json);
    cJSON_free(json); cJSON_Delete(j);
    return ok && s->vtable->write_text_atomic(s, file, text);
}

bool ptc_reason_detail_fresh(const PtcReasonDetail *v, const char *release, const char *boot, uint64_t ms)
{
    return !strcmp(v->release, release) && !strcmp(v->boot, boot) &&
        v->issued_ms <= ms && ms < v->expires_ms && v->expires_ms > v->issued_ms &&
        v->expires_ms - v->issued_ms <= 2000;
}

bool ptc_reason_detail_same(const PtcReasonDetail *a, const PtcReasonDetail *b)
{
    return !strcmp(a->release, b->release) && !strcmp(a->boot, b->boot) &&
        a->ui_instance == b->ui_instance && a->sequence == b->sequence && a->issued_ms == b->issued_ms &&
        a->expires_ms == b->expires_ms && a->day == b->day && a->reason == b->reason && a->instance == b->instance;
}

bool ptc_reason_detail_clear(PtcStorage *s, const char *root, const PtcReasonDetail *owned)
{
    char file[256]; PtcStorageMetadata meta; PtcReasonDetail observed;
    if (!path(file, root, "request") || !s->vtable->metadata(s, file, &meta)) return false;
    if (meta.type == PTC_STORAGE_ENTRY_MISSING) return true;
    return ptc_reason_detail_read(s, root, "request", &observed) && ptc_reason_detail_same(owned, &observed) &&
        s->vtable->remove_path(s, file);
}

bool ptc_reason_detail_runtime(PtcStorage *s, const char *root, const char *release, char boot[17])
{
    char file[256], text[768];
    int n = snprintf(file, sizeof(file), "%s/handover/runtime-ready.json", root);
    if (n < 0 || (size_t)n >= sizeof(file) || !s->vtable->read_text(s, file, text, sizeof(text))) return false;
    cJSON *j = cJSON_ParseWithOpts(text, NULL, true);
    bool ok = cJSON_IsObject(j);
    for (const cJSON *a = j ? j->child : NULL; ok && a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) ok = false;
    ok = ok && !strcmp(string(j, "release_id"), release) && !strcmp(string(j, "profile"), "release") &&
        hex(string(j, "boot_id"), 16);
    if (ok) snprintf(boot, 17, "%s", string(j, "boot_id"));
    cJSON_Delete(j);
    return ok;
}
