#include "reason_notification.h"
#include "../companion/notification_preferences.h"
#include "../companion/notification_text.h"
#include "../companion/input_owner.h"
#include "../third_party/cjson/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOTIFICATION_DIR "sdmc:/config/ultrahand/notifications/"
#define READY_RENEWAL_WAIT_MS UINT64_C(500)
#define DETAIL_SETTLE_MS UINT64_C(500)

static const char *str(const cJSON *o, const char *key)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key));
    return s ? s : "";
}

void ptc_reason_preferences_update(PtcReasonPreferences *preferences, const char *config)
{
    if (preferences->parsed_config[0] && config &&
        !strcmp(preferences->parsed_config, config)) return;
    preferences->parsed_config[0] = '\0';
    if (ptc_reason_notification_preferences(config, &preferences->enabled,
            &preferences->language) && strlen(config) < sizeof(preferences->parsed_config))
        strcpy(preferences->parsed_config, config);
}

static void state(PtcReasonNotification *n, const char *value)
{
    char path[256], text[1536];
    const PtcReasonDetailDecision *d = &n->detail_decision;
    if (!strcmp(n->state, value) && !n->detail_decision_dirty) return;
    snprintf(path, sizeof(path), "%s/support/reason-notification.json", n->root);
    int length = snprintf(text, sizeof(text), "{\"version\":1,\"state\":\"%s\",\"display_confirmed\":false,"
        "\"receiver\":\"unknown\",\"io_failures\":%u,\"sequence\":%llu,\"io_max_ms\":%llu,"
        "\"release_id\":\"%s\",\"backend_boot_id\":\"%s\",\"day_index\":%u,\"reason\":%u,"
        "\"instance\":\"%llu\",\"captured_at\":%lld,\"expires_mono_ms\":\"%llu\","
        "\"detail_accepted_ms\":\"%llu\",\"detail_draw_deadline_ms\":\"%llu\","
        "\"detail_decision\":{\"version\":1,\"available\":%s,\"decision_ms\":\"%llu\","
        "\"day_index\":%u,\"reason\":%u,\"instance\":\"%llu\",\"repeat\":%s,"
        "\"failures\":%u,\"ready_read_attempted\":%s,\"ready_read_ok\":%s,"
        "\"ready_release_match\":%s,\"ready_boot_match\":%s,\"ready_fresh\":%s,"
        "\"ready_ui_instance\":\"%llu\",\"ready_issued_ms\":\"%llu\",\"ready_expires_ms\":\"%llu\","
        "\"clock_budget_available\":%s,\"sequence_available\":%s}}",
        value, n->failures, (unsigned long long)n->sequence, (unsigned long long)n->io_max_ms,
        n->event.release, n->event.boot, n->event.latest.day, (unsigned)n->event.latest.reason,
        (unsigned long long)n->event.latest.instance, (long long)n->event.latest.captured_at,
        (unsigned long long)n->expires_ms, (unsigned long long)n->detail_accepted_ms,
        (unsigned long long)n->detail_draw_deadline_ms,
        d->available ? "true" : "false", (unsigned long long)d->ms,
        d->day, (unsigned)d->reason, (unsigned long long)d->instance, d->repeat ? "true" : "false",
        d->failures, d->ready_read_attempted ? "true" : "false", d->ready_read_ok ? "true" : "false",
        d->ready_release_match ? "true" : "false", d->ready_boot_match ? "true" : "false",
        d->ready_fresh ? "true" : "false", (unsigned long long)d->ready_ui_instance,
        (unsigned long long)d->ready_issued_ms, (unsigned long long)d->ready_expires_ms,
        d->clock_budget_available ? "true" : "false", d->sequence_available ? "true" : "false");
    if (length < 0 || (size_t)length >= sizeof(text)) return;
    snprintf(n->state, sizeof(n->state), "%s", value);
    n->detail_decision_dirty = false;
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

static bool observation_same_day(const PtcReasonObservation *s, int64_t wall)
{
    if (!s->reliable || !s->restricted || s->reason <= PTC_REASON_NONE || s->reason > PTC_REASON_DOCK ||
        s->captured_at <= 0 || wall < s->captured_at) return false;
    uint64_t age = (uint64_t)wall - (uint64_t)s->captured_at;
    return !s->local_clock_available ||
        (s->local_second_of_day < 86400 && age < 86400u - s->local_second_of_day);
}

static bool observation_current(const PtcReasonObservation *s, int64_t wall)
{
    return observation_same_day(s, wall) && wall - s->captured_at < 2;
}

bool ptc_reason_notification_body(const PtcReasonObservation *s, int64_t wall,
    PtcUiLanguagePreference lang, const char *shortcut, char *out, size_t size)
{
    static const PtcUiTextId reasons[] = {PTC_UI_T_NOTIFY_DAILY, PTC_UI_T_NOTIFY_DAILY,
        PTC_UI_T_NOTIFY_BEDTIME, PTC_UI_T_NOTIFY_EYE, PTC_UI_T_NOTIFY_DOCK};
    char time[256], duration[96];
    if (!size) return false;
    out[0] = '\0';
    if (!observation_current(s, wall)) return false;
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

static bool publish(PtcReasonNotification *n, PtcUiLanguagePreference lang, int64_t wall, uint64_t ms)
{
    char name[96], stage[280], body[512];
    if (ms > UINT64_MAX - 2000) return false;
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
    char issued[32], expires[32];
    snprintf(issued, sizeof(issued), "%llu", (unsigned long long)ms);
    snprintf(expires, sizeof(expires), "%llu", (unsigned long long)(ms + 2000));
    ok = ok && cJSON_AddStringToObject(p, "pw_issued_ms", issued) &&
        cJSON_AddStringToObject(p, "pw_expires_ms", expires);
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
    if (!ok || !n->storage->vtable->metadata(n->storage, n->path, &meta) ||
        meta.type != PTC_STORAGE_ENTRY_MISSING ||
        !n->storage->vtable->write_text_atomic(n->storage, stage, n->payload)) return false;
    /* The receiver scans through the publication window, even if rename reports failure. */
    if (n->wake) n->wake(n->wake_ctx);
    return n->storage->vtable->rename_path(n->storage, stage, n->path);
}

typedef enum {
    DETAIL_CONTINUE,
    DETAIL_CONFIRMED,
    DETAIL_FALLBACK,
    DETAIL_CLEANUP_FAILED,
} DetailResult;

static DetailResult advance_detail(PtcReasonNotification *n, const PtcReasonObservation *s,
    uint64_t ms, int64_t wall, bool cancel, bool suppressed)
{
    if (!n->detail_pending) return DETAIL_CONTINUE;
    if (!cancel && !n->detail_draw_deadline_ms && ms >= n->detail.issued_ms &&
        ms - n->detail.issued_ms < PTC_REASON_DETAIL_PICKUP_MS + PTC_REASON_DETAIL_DRAW_MS + DETAIL_SETTLE_MS) {
        PtcReasonDetail accepted;
        if (ptc_reason_detail_read(n->storage, n->root, "accepted", &accepted) && accepted.stage_ms <= ms) {
            n->detail_draw_deadline_ms = ptc_reason_detail_draw_deadline(&n->detail, &accepted);
            if (n->detail_draw_deadline_ms) {
                n->detail_accepted_ms = accepted.stage_ms;
            }
        }
    }
    uint64_t deadline = n->detail_draw_deadline_ms ? n->detail_draw_deadline_ms : n->detail.expires_ms;
    /* A frame drawn in time can take another poll to commit and be read.
       Keep the same bounded grace for a cached visible owner to clear. */
    bool expired = ms >= deadline;
    bool grace = expired && ms - deadline < DETAIL_SETTLE_MS && (n->detail_draw_deadline_ms || suppressed);
    PtcReasonDetail ack;
    /* A live visible owner is expected when confirming a drawn detail. */
    bool confirmed = !cancel && n->detail_draw_deadline_ms && (!expired || grace) &&
        ptc_reason_detail_read(n->storage, n->root, "ack", &ack) &&
        ptc_reason_detail_same(&n->detail, &ack) &&
        ack.stage_ms >= n->detail_accepted_ms && ack.stage_ms < deadline && ack.stage_ms <= ms &&
        ptc_input_owner_confirm(n->storage, n->root, n->event.release, n->event.boot,
            ack.owner, ms * UINT64_C(1000000));
    if (!confirmed && !cancel && (!expired || grace)) return DETAIL_CONTINUE;
    if (!ptc_reason_detail_clear(n->storage, n->root, &n->detail)) return DETAIL_CLEANUP_FAILED;
    n->detail_pending = false;
    state(n, confirmed ? "detail_drawn_confirmed" : cancel ? "detail_cancelled" : "detail_timeout");
    if (confirmed) return DETAIL_CONFIRMED;
    return !cancel && !suppressed && observation_current(s, wall) ? DETAIL_FALLBACK : DETAIL_CONTINUE;
}

/* True when the event is waiting for a renewed ready lease or a detail ACK. */
static bool try_detail(PtcReasonNotification *n, const PtcReasonObservation *s, uint64_t ms)
{
    PtcReasonDetail detail;
    PtcReasonDetailDecision *d = &n->detail_decision;
    *d = (PtcReasonDetailDecision){.available = true, .repeat = n->event.repeat,
        .failures = n->detail_failures, .day = s->day, .reason = s->reason,
        .instance = s->instance, .ms = ms,
        .clock_budget_available = ms <= UINT64_MAX - PTC_REASON_DETAIL_PICKUP_MS - PTC_REASON_DETAIL_DRAW_MS,
        .sequence_available = n->detail_sequence != UINT64_MAX};
    n->detail_decision_dirty = true;
    /* A continuing countdown gets only a brief notice, never another focus grab. */
    if (!d->repeat && d->failures < 5) {
        d->ready_read_attempted = true;
        d->ready_read_ok = ptc_reason_detail_read(n->storage, n->root, "ready", &detail);
        if (d->ready_read_ok) {
            /* Retain the actual read before replacing its lease with an offer. */
            d->ready_release_match = !strcmp(detail.release, n->event.release);
            d->ready_boot_match = !strcmp(detail.boot, n->event.boot);
            d->ready_ui_instance = detail.ui_instance;
            d->ready_issued_ms = detail.issued_ms;
            d->ready_expires_ms = detail.expires_ms;
            d->ready_fresh = ptc_reason_detail_fresh(&detail, n->event.release, n->event.boot, ms);
        }
    }
    /* A same-boot expired hint may be awaiting a slow renewal. Wait only for
       this event, and require a fresh lease before offering detail. */
    bool expired_matching_lease = d->ready_read_ok && detail.expires_ms &&
        ms >= detail.expires_ms &&
        ptc_reason_detail_fresh(&detail, n->event.release, n->event.boot, detail.expires_ms - 1);
    bool same_identity_or_missing = !d->ready_read_ok ||
        (d->ready_release_match && d->ready_boot_match);
    if (!d->ready_fresh && !d->repeat && d->failures < 5 && d->clock_budget_available && d->sequence_available &&
        (expired_matching_lease || (n->ready_wait_deadline_ms && same_identity_or_missing))) {
        if (!n->ready_wait_deadline_ms) n->ready_wait_deadline_ms = ms + READY_RENEWAL_WAIT_MS;
        if (ms < n->ready_wait_deadline_ms) return true;
    }
    n->ready_wait_deadline_ms = 0;
    ptc_reason_event_attempted(&n->event, ms);
    if (!d->ready_fresh || !d->clock_budget_available || !d->sequence_available) return false;
    detail.sequence = ++n->detail_sequence;
    detail.issued_ms = ms; detail.expires_ms = ms + PTC_REASON_DETAIL_PICKUP_MS;
    detail.day = s->day; detail.reason = s->reason; detail.instance = s->instance;
    n->detail = detail; n->detail_pending = true;
    n->detail_accepted_ms = n->detail_draw_deadline_ms = 0;
    if (ptc_reason_detail_write(n->storage, n->root, "request", &detail)) {
        if (n->wake) n->wake(n->wake_ctx);
        state(n, "detail_requested");
    } else {
        /* A failed write may already have renamed. Let the offer expire before fallback. */
        ++n->detail_failures;
        state(n, "detail_write_unknown");
    }
    return true;
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
    bool change = (n->pending || n->detail_pending || n->ready_wait_deadline_ms) &&
        (s->reason != n->event.latest.reason || s->instance != n->event.latest.instance ||
            s->day != n->event.latest.day);
    bool detail_cancel = stopping || !enabled || !observation_same_day(s, wall) ||
        ms < n->event.last_ms || wall < n->event.last_wall || change;
    bool cancel = detail_cancel || suppressed;
    if (cancel) n->ready_wait_deadline_ms = 0;
    DetailResult detail = advance_detail(n, s, ms, wall, detail_cancel, suppressed);
    if (detail == DETAIL_CLEANUP_FAILED) goto failed;
    if (n->path[0] && (cancel || !n->pending || ms >= n->expires_ms)) {
        if (!clear(n)) goto failed;
        state(n, cancel ? "suppressed" : "expired_display_unknown");
    }
    if (stopping) return;
    bool ready = ptc_reason_event_update(&n->event, n->event.release, n->event.boot, s, ms, wall,
        enabled, suppressed);
    if ((!enabled || suppressed) && detail == DETAIL_CONTINUE && !n->detail_pending) state(n, "suppressed");
    if (n->pending || n->detail_pending) return;
    if (detail != DETAIL_FALLBACK && (!ready || try_detail(n, s, ms))) return;
    /* The receiver switch and shortcut label matter only when a fallback toast
       is about to be published. Read them here, at most once per two seconds,
       instead of re-reading two SD files on every idle round. */
    if (ms >= n->config_ms) {
        ptc_ultrahand_config_read(n->storage, &n->external);
        n->config_ms = ms > UINT64_MAX - 2000 ? UINT64_MAX : ms + 2000;
    }
    if (n->external.app_muted) { state(n, "fallback_receiver_muted"); return; }
    if (n->external.notifications != 1) { state(n, "fallback_receiver_unavailable"); return; }
    if (!publish(n, lang, wall, ms)) goto failed;
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
