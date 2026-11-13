#include "reason_notification.h"
#include "../companion/notification_text.h"
#include "../companion/input_owner.h"
#include "../third_party/cjson/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOTIFICATION_DIR "sdmc:/config/ultrahand/notifications/"

static const char *str(const cJSON *o, const char *key)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key));
    return s ? s : "";
}

void ptc_reason_notification_preferences(const char *config, bool *enabled,
    PtcUiLanguagePreference *language)
{
    cJSON *j = cJSON_ParseWithOpts(config, NULL, true);
    bool valid = cJSON_IsObject(j);
    for (const cJSON *a = j ? j->child : NULL; valid && a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) valid = false;
    *enabled = valid && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "reason_notifications_enabled"));
    const char *lang = str(j, "ui_language");
    if (!lang[0] || !strcmp(lang, "system")) lang = str(j, "reason_notification_language");
    *language = !strcmp(lang, "zh-Hans") ? PTC_UI_LANGUAGE_SIMPLIFIED :
        !strcmp(lang, "zh-Hant") ? PTC_UI_LANGUAGE_TRADITIONAL : PTC_UI_LANGUAGE_ENGLISH;
    cJSON_Delete(j);
}

static void state(PtcReasonNotification *n, const char *value)
{
    char path[256], text[768];
    if (!strcmp(n->state, value)) return;
    snprintf(n->state, sizeof(n->state), "%s", value);
    snprintf(path, sizeof(path), "%s/support/reason-notification.json", n->root);
    snprintf(text, sizeof(text), "{\"version\":1,\"state\":\"%s\",\"display_confirmed\":false,"
        "\"receiver\":\"unknown\",\"io_failures\":%u,\"sequence\":%llu,\"io_max_ms\":%llu,"
        "\"release_id\":\"%s\",\"backend_boot_id\":\"%s\",\"day_index\":%u,\"reason\":%u,"
        "\"instance\":\"%llu\",\"captured_at\":%lld,\"expires_mono_ms\":\"%llu\"}",
        value, n->failures, (unsigned long long)n->sequence, (unsigned long long)n->io_max_ms,
        n->event.release, n->event.boot, n->event.latest.day, (unsigned)n->event.latest.reason,
        (unsigned long long)n->event.latest.instance, (long long)n->event.latest.captured_at,
        (unsigned long long)n->expires_ms);
    (void)n->storage->vtable->write_text_atomic(n->storage, path, text);
}

/* Exact bytes + confined filename, not just a familiar prefix, authorize removal. */
static bool remove_owned(PtcReasonNotification *n, const char *path)
{
    PtcStorageMetadata meta;
    char observed[1536];
    if (!n->storage->vtable->metadata(n->storage, path, &meta)) return false;
    if (meta.type == PTC_STORAGE_ENTRY_MISSING) return true;
    return meta.type == PTC_STORAGE_ENTRY_FILE &&
        n->storage->vtable->read_text(n->storage, path, observed, sizeof(observed)) &&
        !strcmp(observed, n->payload) && n->storage->vtable->remove_path(n->storage, path);
}

static bool clear(PtcReasonNotification *n)
{
    char stage[280];
    snprintf(stage, sizeof(stage), "%s.stage", n->path);
    if (n->path[0] && (!remove_owned(n, n->path) || !remove_owned(n, stage))) return false;
    PtcStorageMetadata meta;
    if (!n->storage->vtable->metadata(n->storage, n->journal, &meta)) return false;
    if (meta.type != PTC_STORAGE_ENTRY_MISSING) {
        char text[4096];
        if (meta.type != PTC_STORAGE_ENTRY_FILE ||
            !n->storage->vtable->read_text(n->storage, n->journal, text, sizeof(text))) return false;
        cJSON *j = cJSON_ParseWithOpts(text, NULL, true);
        bool owned = cJSON_IsObject(j) && cJSON_GetArraySize(j) == 2 &&
            !strcmp(str(j, "payload"), n->payload) &&
            !strncmp(n->path, NOTIFICATION_DIR, strlen(NOTIFICATION_DIR)) &&
            !strcmp(str(j, "file"), n->path + strlen(NOTIFICATION_DIR));
        cJSON_Delete(j);
        if (!owned || !n->storage->vtable->remove_path(n->storage, n->journal)) return false;
    }
    n->path[0] = n->payload[0] = '\0';
    n->pending = false;
    return true;
}

static bool valid_name(const char *s)
{
    const char *prefix = "playwise-reason-";
    size_t length = strlen(prefix);
    if (strlen(s) != length + 16 + 1 + 16 + 7 || strncmp(s, prefix, length) ||
        s[length+16] != '-' || strcmp(s+length+33, ".notify")) return false;
    for (size_t i = length; i < length+33; ++i)
        if (i != length+16 && !((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

static bool startup(PtcReasonNotification *n)
{
    PtcStorageMetadata meta;
    char text[4096];
    if (!n->storage->vtable->metadata(n->storage, n->journal, &meta)) return false;
    if (meta.type == PTC_STORAGE_ENTRY_MISSING) return true;
    if (meta.type != PTC_STORAGE_ENTRY_FILE ||
        !n->storage->vtable->read_text(n->storage, n->journal, text, sizeof(text))) return false;
    cJSON *j = cJSON_ParseWithOpts(text, NULL, true);
    const char *name = str(j, "file"), *payload = str(j, "payload");
    cJSON *p = cJSON_ParseWithOpts(payload, NULL, true);
    bool ok = cJSON_IsObject(j) && cJSON_GetArraySize(j) == 2 &&
        valid_name(name) && strlen(payload) < sizeof(n->payload) &&
        !strcmp(str(p, "pw_owner"), "reason-v1") && !strcmp(str(p, "pw_file"), name);
    if (ok) {
        snprintf(n->path, sizeof(n->path), NOTIFICATION_DIR "%s", name);
        snprintf(n->payload, sizeof(n->payload), "%s", payload);
    }
    cJSON_Delete(j); cJSON_Delete(p);
    return ok && clear(n);
}

void ptc_reason_notification_init(PtcReasonNotification *n, PtcStorage *s,
    const char *root, const char *release, const char *boot)
{
    memset(n, 0, sizeof(*n));
    ptc_reason_event_init(&n->event, release, boot);
    n->storage = s;
    snprintf(n->root, sizeof(n->root), "%s", root);
    snprintf(n->journal, sizeof(n->journal), "%s/support/reason-notification-owned.json", root);
    n->external.notifications = -1;
    /* Only a system-generated 16-hex boot may form a filename. */
    if (strlen(boot) != 16) n->blocked = true;
    for (size_t i = 0; boot[i]; ++i)
        if (!((boot[i] >= '0' && boot[i] <= '9') || (boot[i] >= 'a' && boot[i] <= 'f'))) n->blocked = true;
}

bool ptc_reason_notification_body(const PtcReasonObservation *s, int64_t wall,
    PtcUiLanguagePreference lang, const char *shortcut, char *out, size_t size)
{
    static const PtcUiTextId reasons[] = {PTC_UI_T_NOTIFY_DAILY, PTC_UI_T_NOTIFY_DAILY,
        PTC_UI_T_NOTIFY_BEDTIME, PTC_UI_T_NOTIFY_EYE, PTC_UI_T_NOTIFY_DOCK};
    char time[256], duration[96];
    if (!size) return false;
    out[0] = '\0';
    if (!s->reliable || !s->restricted || s->reason <= PTC_REASON_NONE || s->reason > PTC_REASON_DOCK ||
        wall < s->captured_at || wall - s->captured_at >= 2 ||
        (s->local_clock_available && (s->local_second_of_day >= 86400 ||
            wall - s->captured_at >= 86400 - s->local_second_of_day))) return false;
    PtcUiTextId time_id = PTC_UI_T_NOTIFY_TIME_UNKNOWN;
    if (s->reason == PTC_REASON_DAILY && s->time_kind == PTC_REASON_TIME_DAILY_ZERO)
        time_id = PTC_UI_T_NOTIFY_DAILY_ZERO;
    else if (s->reason == PTC_REASON_DOCK) {
        if (s->time_kind == PTC_REASON_TIME_DOCK_ZERO) time_id = PTC_UI_T_NOTIFY_DOCK_ZERO;
        if (s->time_kind == PTC_REASON_TIME_CONNECT_TV) time_id = PTC_UI_T_NOTIFY_CONNECT_TV;
    }
    uint32_t age = (uint32_t)(wall - s->captured_at);
    uint32_t max_seconds = s->reason == PTC_REASON_EYE ? 3600 : 86400;
    if ((s->reason == PTC_REASON_EYE || s->reason == PTC_REASON_BEDTIME) &&
        s->time_kind == PTC_REASON_TIME_REMAINING && s->remaining_seconds > age &&
        s->remaining_seconds <= max_seconds) {
        uint32_t seconds = s->remaining_seconds - age;
        unsigned minutes = (seconds + 59) / 60;
        if (seconds < 60) snprintf(duration, sizeof(duration), "%s", ptc_notification_text(PTC_UI_T_NOTIFY_UNDER_MINUTE, lang));
        else if (minutes < 60) snprintf(duration, sizeof(duration), ptc_notification_text(PTC_UI_T_NOTIFY_MINUTES, lang), minutes);
        else if (minutes % 60) snprintf(duration, sizeof(duration), ptc_notification_text(PTC_UI_T_NOTIFY_HOURS_MINUTES, lang), minutes / 60, minutes % 60);
        else snprintf(duration, sizeof(duration), ptc_notification_text(PTC_UI_T_NOTIFY_HOURS, lang), minutes / 60);
        snprintf(time, sizeof(time), ptc_notification_text(s->reason == PTC_REASON_EYE ?
            PTC_UI_T_NOTIFY_REST_TIME : PTC_UI_T_NOTIFY_BED_TIME, lang), duration);
    } else snprintf(time, sizeof(time), "%s", ptc_notification_text(time_id, lang));
    const char *hint = ptc_notification_text(shortcut[0] ? PTC_UI_T_NOTIFY_SHORTCUT : PTC_UI_T_NOTIFY_OPEN, lang);
    int length = snprintf(out, size, "%s\n%s\n%s%s", ptc_notification_text(reasons[s->reason], lang), time, shortcut, hint);
    if (length < 0 || (size_t)length >= size) { out[0] = '\0'; return false; }
    return true;
}

static bool publish(PtcReasonNotification *n, PtcUiLanguagePreference lang, int64_t wall)
{
    char name[96], stage[280], body[512];
    if (!ptc_reason_notification_body(&n->event.latest, wall, lang, n->external.shortcut, body, sizeof(body))) return false;
    if (++n->sequence == 0) return false;
    snprintf(name, sizeof(name), "playwise-reason-%s-%016llx.notify", n->event.boot,
        (unsigned long long)n->sequence);
    snprintf(n->path, sizeof(n->path), NOTIFICATION_DIR "%s", name);
    snprintf(stage, sizeof(stage), "%s.stage", n->path);
    cJSON *p = cJSON_CreateObject();
    if (!p) return false;
    bool ok = cJSON_AddStringToObject(p, "title", ptc_notification_text(PTC_UI_T_NOTIFY_TITLE, lang)) &&
        cJSON_AddStringToObject(p, "text", body) && cJSON_AddNumberToObject(p, "font_size", 26) &&
        cJSON_AddNumberToObject(p, "duration", 10000) && cJSON_AddStringToObject(p, "show_time", "false") &&
        cJSON_AddNumberToObject(p, "priority", 30) && cJSON_AddStringToObject(p, "pw_owner", "reason-v1") &&
        cJSON_AddStringToObject(p, "pw_file", name) && cJSON_AddStringToObject(p, "pw_release", n->event.release) &&
        cJSON_AddNumberToObject(p, "pw_day", n->event.latest.day);
    char instance[32];
    snprintf(instance, sizeof(instance), "%llu", (unsigned long long)n->event.latest.instance);
    ok = ok && cJSON_AddNumberToObject(p, "pw_reason", n->event.latest.reason) &&
        cJSON_AddStringToObject(p, "pw_instance", instance);
    char *payload = ok ? cJSON_PrintUnformatted(p) : NULL;
    ok = payload && strlen(payload) < sizeof(n->payload);
    if (ok) snprintf(n->payload, sizeof(n->payload), "%s", payload);
    free(payload); cJSON_Delete(p);
    if (!ok) return false;
    cJSON *j = cJSON_CreateObject();
    ok = j && cJSON_AddStringToObject(j, "file", name) && cJSON_AddStringToObject(j, "payload", n->payload);
    char *journal = ok ? cJSON_PrintUnformatted(j) : NULL;
    /* Journal before publication permits exact-byte cleanup after normal restart. */
    ok = journal && n->storage->vtable->write_text_atomic(n->storage, n->journal, journal);
    free(journal); cJSON_Delete(j);
    PtcStorageMetadata meta;
    return ok && n->storage->vtable->metadata(n->storage, n->path, &meta) &&
        meta.type == PTC_STORAGE_ENTRY_MISSING &&
        n->storage->vtable->write_text_atomic(n->storage, stage, n->payload) &&
        n->storage->vtable->rename_path(n->storage, stage, n->path);
}

void ptc_reason_notification_tick(PtcReasonNotification *n, const PtcReasonObservation *s,
    uint64_t ms, int64_t wall, bool enabled, bool suppressed, bool stopping, PtcUiLanguagePreference lang)
{
    if (n->blocked) { state(n, "blocked_unknown_ownership"); return; }
    if (ms < n->retry_ms) return;
    if (!n->initialized) {
        if (!startup(n)) goto failed;
        n->initialized = true;
    }
    bool change = (n->pending || n->detail_pending) && (s->reason != n->event.latest.reason || s->instance != n->event.latest.instance ||
        s->day != n->event.latest.day);
    bool cancel = stopping || !enabled || suppressed || !s->reliable || !s->restricted ||
        ms < n->event.last_ms || wall < n->event.last_wall || change;
    bool fallback = false;
    bool detail_confirmed = false;
    if (n->detail_pending) {
        PtcReasonDetail ack;
        char body[512];
        bool current = ptc_reason_notification_body(s, wall, lang, "", body, sizeof(body));
        bool confirmed = !change && current && enabled && !stopping &&
            ms >= n->event.last_ms && wall >= n->event.last_wall &&
            ptc_reason_detail_fresh(&n->detail, n->event.release, n->event.boot, ms) &&
            ptc_reason_detail_read(n->storage, n->root, "ack", &ack) &&
            ptc_reason_detail_same(&n->detail, &ack) &&
            ptc_input_owner_confirm(n->storage, n->root, n->event.release, n->event.boot,
                ack.owner, ms * UINT64_C(1000000));
        detail_confirmed = confirmed;
        bool detail_cancel = stopping || !enabled || !s->reliable || !s->restricted || change ||
            ms < n->event.last_ms || wall < n->event.last_wall;
        /* The reader may cache the UI's release for one 250ms poll. Give that
           release at most 500ms to drain; the UI offer itself still expires at 2s. */
        bool timed_out = ms >= n->detail.expires_ms &&
            (!suppressed || ms - n->detail.expires_ms >= 500);
        if (confirmed || detail_cancel || !current || timed_out) {
            if (!ptc_reason_detail_clear(n->storage, n->root, &n->detail)) goto failed;
            n->detail_pending = false;
            fallback = !confirmed && !cancel && current;
            state(n, confirmed ? "detail_drawn_confirmed" : detail_cancel ? "detail_cancelled" : "detail_timeout");
        }
    }
    if (n->path[0] && (cancel || !n->pending || ms >= n->expires_ms)) {
        if (!clear(n)) goto failed;
        state(n, cancel ? "suppressed" : "expired_display_unknown");
    }
    if (stopping) return;
    bool ready = ptc_reason_event_update(&n->event, n->event.release, n->event.boot, s, ms, wall,
        enabled, suppressed);
    if ((!enabled || suppressed) && !detail_confirmed) state(n, "suppressed");
    if ((!ready && !fallback) || n->pending || n->detail_pending) return;
    if (!fallback) {
        ptc_reason_event_attempted(&n->event, ms);
        PtcReasonDetail detail;
        /* A continuing countdown gets only a brief notice, never another focus grab. */
        if (!n->event.repeat && n->detail_failures < 5 && ptc_reason_detail_read(n->storage, n->root, "ready", &detail) &&
            ptc_reason_detail_fresh(&detail, n->event.release, n->event.boot, ms) &&
            ms <= UINT64_MAX - 2000 && n->detail_sequence != UINT64_MAX) {
            detail.sequence = ++n->detail_sequence;
            detail.issued_ms = ms; detail.expires_ms = ms + 2000;
            detail.day = s->day; detail.reason = s->reason; detail.instance = s->instance;
            n->detail = detail; n->detail_pending = true;
            if (ptc_reason_detail_write(n->storage, n->root, "request", &detail)) {
                state(n, "detail_requested");
                return;
            }
            /* A failed write may already have renamed. Wait out its same lease
               before fallback, rather than race a late accepted offer. */
            ++n->detail_failures;
            state(n, "detail_write_unknown");
            return;
        }
    }
    /* The receiver switch and shortcut label matter only when a fallback toast
       is about to be published. Read them here, at most once per two seconds,
       instead of re-reading two SD files on every idle round. */
    if (ms >= n->config_ms) {
        ptc_ultrahand_config_read(n->storage, &n->external);
        n->config_ms = ms > UINT64_MAX - 2000 ? UINT64_MAX : ms + 2000;
    }
    if (n->external.notifications != 1) { state(n, "fallback_receiver_unavailable"); return; }
    if (!publish(n, lang, wall)) goto failed;
    n->pending = true;
    n->expires_ms = ms + 2000;
    state(n, "pending_display_unknown");
    return;
failed:
    /* Retain ownership journal and prevent another publication. Bounded retry
       (5 attempts/boot), no per-250ms log flood, never alter control outcomes. */
    n->retry_ms = ms + 10000;
    if (++n->failures >= 5) n->blocked = true;
    state(n, "write_failed");
}
