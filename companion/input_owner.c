#include "input_owner.h"

#include <stdio.h>
#include <string.h>
#include "../third_party/cjson/cJSON.h"

typedef struct {
    char owner[33];
    uint64_t issued;
    uint64_t expires;
    bool visible;
} Lease;

static const char *string(const cJSON *value, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(value, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static bool hex(const char *value, size_t size)
{
    size_t i;
    if (!value || strlen(value) != size) return false;
    for (i = 0; i < size; ++i)
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    return true;
}

static bool ns(const char *value, uint64_t *out)
{
    uint64_t result = 0;
    size_t i, size = strlen(value);
    if (!size || size > 20 || value[0] == '0') return false;
    for (i = 0; i < size; ++i) {
        unsigned digit = (unsigned)(value[i] - '0');
        if (digit > 9 || result > (UINT64_MAX - digit) / 10) return false;
        result = result * 10 + digit;
    }
    *out = result;
    return true;
}

static cJSON *object(const char *text)
{
    cJSON *value = cJSON_ParseWithOpts(text, NULL, true);
    const cJSON *a, *b;
    if (!cJSON_IsObject(value)) { cJSON_Delete(value); return NULL; }
    cJSON_ArrayForEach(a, value) {
        for (b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) { cJSON_Delete(value); return NULL; }
    }
    return value;
}

static bool context_init(PtcInputOwnerContext *c, PtcStorage *storage,
    const char *root, const char *release)
{
    size_t i;
    int written;
    memset(c, 0, sizeof(*c));
    if (!storage || !storage->vtable || !storage->vtable->read_text ||
        !storage->vtable->metadata || !root || strlen(root) >= sizeof(c->root) ||
        !release || !release[0] || strlen(release) >= sizeof(c->release)) return false;
    for (i = 0; release[i]; ++i)
        if (!((release[i] >= '0' && release[i] <= '9') || (release[i] >= 'a' && release[i] <= 'z') ||
              (release[i] >= 'A' && release[i] <= 'Z') || strchr("-_.+", release[i]))) return false;
    written = snprintf(c->runtime_path, sizeof(c->runtime_path), "%s/handover/runtime-ready.json", root);
    if (written < 0 || (size_t)written >= sizeof(c->runtime_path)) return false;
    c->storage = storage;
    snprintf(c->root, sizeof(c->root), "%s", root);
    snprintf(c->release, sizeof(c->release), "%s", release);
    return true;
}

/* -2 is known missing, -1 unknown, 0 another candidate, 1 this candidate. */
static int runtime(PtcInputOwnerContext *c, char boot[17])
{
    char text[768];
    cJSON *value;
    PtcStorageMetadata metadata;
    int result;
    if (!c->storage || !c->storage->vtable->metadata(c->storage, c->runtime_path, &metadata)) return -1;
    if (metadata.type == PTC_STORAGE_ENTRY_MISSING) return -2;
    if (metadata.type != PTC_STORAGE_ENTRY_FILE ||
        !c->storage->vtable->read_text(c->storage, c->runtime_path, text, sizeof(text))) return -1;
    value = object(text);
    if (!value || !hex(string(value, "boot_id"), 16) || strcmp(string(value, "profile"), "release")) {
        cJSON_Delete(value); return -1;
    }
    result = strcmp(string(value, "release_id"), c->release) ? 0 : 1;
    if (result == 1) snprintf(boot, 17, "%s", string(value, "boot_id"));
    cJSON_Delete(value);
    return result;
}

static bool bind(PtcInputOwnerContext *c, const char boot[17])
{
    unsigned slot;
    for (slot = 0; slot < 2; ++slot) {
        int n = snprintf(c->paths[slot], sizeof(c->paths[slot]), "%s/support/overlay-input-%s-%u.json", c->root, boot, slot);
        if (n < 0 || (size_t)n >= sizeof(c->paths[slot])) { c->bound = false; return false; }
    }
    snprintf(c->boot, sizeof(c->boot), "%s", boot);
    c->bound = true;
    return true;
}

/* -1 rejects unknown storage, malformed records and another identity. */
static int read_lease(PtcInputOwnerContext *c, unsigned slot, Lease *lease)
{
    char text[512];
    PtcStorageMetadata metadata;
    cJSON *value;
    const cJSON *version, *visible;
    bool ok;
    if (!c->storage->vtable->metadata(c->storage, c->paths[slot], &metadata)) return -1;
    if (metadata.type == PTC_STORAGE_ENTRY_MISSING) return 0;
    if (metadata.type != PTC_STORAGE_ENTRY_FILE ||
        !c->storage->vtable->read_text(c->storage, c->paths[slot], text, sizeof(text))) return -1;
    value = object(text);
    version = cJSON_GetObjectItemCaseSensitive(value, "schema_version");
    visible = cJSON_GetObjectItemCaseSensitive(value, "visible");
    ok = value && cJSON_GetArraySize(value) == 7 && cJSON_IsNumber(version) && version->valuedouble == 1 &&
        cJSON_IsBool(visible) && !strcmp(string(value, "release_id"), c->release) &&
        !strcmp(string(value, "backend_boot_id"), c->boot) && hex(string(value, "owner_id"), 32) &&
        ns(string(value, "issued_mono_ns"), &lease->issued) && ns(string(value, "expires_mono_ns"), &lease->expires) &&
        lease->expires > lease->issued && lease->expires - lease->issued <= PTC_INPUT_OWNER_LEASE_NS;
    if (ok) {
        snprintf(lease->owner, sizeof(lease->owner), "%s", string(value, "owner_id"));
        lease->visible = cJSON_IsTrue(visible);
    }
    cJSON_Delete(value);
    return ok ? 1 : -1;
}

static bool write_lease(PtcInputOwnerWriter *w, unsigned slot, uint64_t now, bool visible)
{
    char text[512], observed[512];
    if (!now || now > UINT64_MAX - PTC_INPUT_OWNER_LEASE_NS) return false;
    snprintf(text, sizeof(text), "{\"schema_version\":1,\"release_id\":\"%s\",\"backend_boot_id\":\"%s\","
        "\"owner_id\":\"%s\",\"issued_mono_ns\":\"%llu\",\"expires_mono_ns\":\"%llu\",\"visible\":%s}",
        w->context.release, w->context.boot, w->owner, (unsigned long long)now,
        (unsigned long long)(now + PTC_INPUT_OWNER_LEASE_NS), visible ? "true" : "false");
    return w->context.storage->vtable->write_text_atomic(w->context.storage, w->context.paths[slot], text) &&
        w->context.storage->vtable->read_text(w->context.storage, w->context.paths[slot], observed, sizeof(observed)) &&
        !strcmp(text, observed);
}

void ptc_input_owner_writer_init(PtcInputOwnerWriter *w, PtcStorage *storage,
    const char *root, const char *release, uint64_t instance_ns)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    if (context_init(&w->context, storage, root, release) && storage->vtable->write_text_atomic)
        w->instance_ns = instance_ns;
}

bool ptc_input_owner_acquire(PtcInputOwnerWriter *w, uint64_t now)
{
    char boot[17];
    unsigned slot;
    Lease lease;
    if (!w || !w->instance_ns || !now || now <= w->epoch_ns) return false;
    if (w->active) return ptc_input_owner_maintain(w, now);
    if (runtime(&w->context, boot) != 1 || !bind(&w->context, boot)) return false;
    for (slot = 0; slot < 2; ++slot) {
        int found = read_lease(&w->context, slot, &lease);
        if (found < 0 || (found && (lease.issued > now || (lease.visible && lease.expires > now)))) return false;
    }
    snprintf(w->owner, sizeof(w->owner), "%016llx%016llx", (unsigned long long)w->instance_ns, (unsigned long long)now);
    w->epoch_ns = w->last_ns = now;
    w->released = w->failed = false;
    /* Two slots keep a valid lease across the FS adapter's remove/rename gap. */
    w->active = write_lease(w, 0, now, true) && write_lease(w, 1, now, true);
    w->failed = !w->active;
    w->expires_ns = now <= UINT64_MAX - PTC_INPUT_OWNER_LEASE_NS ? now + PTC_INPUT_OWNER_LEASE_NS : 0;
    w->renew_ns = now <= UINT64_MAX - PTC_INPUT_OWNER_RENEW_NS ? now + PTC_INPUT_OWNER_RENEW_NS : 0;
    w->next_slot = 0;
    return w->active;
}

bool ptc_input_owner_maintain(PtcInputOwnerWriter *w, uint64_t now)
{
    char boot[17];
    unsigned slot;
    Lease lease;
    if (!w || !w->active) return false;
    if (now < w->last_ns || now >= w->expires_ns) goto failed;
    w->last_ns = now;
    if (now < w->renew_ns) return true;
    if (runtime(&w->context, boot) != 1 || strcmp(boot, w->context.boot)) goto failed;
    for (slot = 0; slot < 2; ++slot)
        if (read_lease(&w->context, slot, &lease) != 1 || strcmp(lease.owner, w->owner) || !lease.visible) goto failed;
    if (!write_lease(w, w->next_slot, now, true)) goto failed;
    w->next_slot ^= 1;
    w->expires_ns = now + PTC_INPUT_OWNER_LEASE_NS;
    w->renew_ns = now + PTC_INPUT_OWNER_RENEW_NS;
    return true;
failed:
    w->active = false;
    w->failed = true;
    return false;
}

bool ptc_input_owner_accept(PtcInputOwnerWriter *w, uint64_t now)
{
    char boot[17];
    if (!ptc_input_owner_maintain(w, now)) return false;
    if (runtime(&w->context, boot) == 1 && !strcmp(boot, w->context.boot)) return true;
    w->active = false;
    w->failed = true;
    return false;
}

bool ptc_input_owner_begin_ui(PtcInputOwnerWriter *w)
{
    if (!w || !w->active || w->failed || !strcmp(w->ui_boot, w->context.boot)) return false;
    snprintf(w->ui_boot, sizeof(w->ui_boot), "%s", w->context.boot);
    return true;
}

void ptc_input_owner_release(PtcInputOwnerWriter *w, uint64_t now)
{
    unsigned slot;
    Lease lease;
    if (!w || !w->owner[0] || w->released) return;
    w->active = false;
    w->released = true;
    if (now < w->last_ns) { w->failed = true; return; }
    for (slot = 0; slot < 2; ++slot) {
        int found = read_lease(&w->context, slot, &lease);
        /* Never overwrite another owner or an unreadable recovery record. */
        if (found < 0 || (found && strcmp(lease.owner, w->owner))) { w->failed = true; continue; }
        if (!write_lease(w, slot, now, false)) w->failed = true;
    }
}

void ptc_input_owner_reader_init(PtcInputOwnerReader *r, PtcStorage *storage,
    const char *root, const char *release)
{
    if (!r) return;
    memset(r, 0, sizeof(*r));
    (void)context_init(&r->context, storage, root, release);
}

bool ptc_input_owner_block(PtcInputOwnerReader *r, uint64_t now, bool non_neutral)
{
    char boot[17];
    unsigned slot;
    Lease lease, newest = { { 0 }, 0, 0, false };
    bool active = false, unknown = false;
    int binding;
    if (!r || !r->context.storage) return false;
    if (!now || now < r->last_ns) { r->blocked = r->needs_neutral = true; return true; }
    if (!non_neutral && now < r->next_poll_ns) return r->blocked || r->needs_neutral;
    r->last_ns = now;
    r->next_poll_ns = now <= UINT64_MAX - PTC_INPUT_OWNER_POLL_NS ? now + PTC_INPUT_OWNER_POLL_NS : now;
    binding = runtime(&r->context, boot);
    if (binding == -1) unknown = true;
    if (binding == 1 && (!r->context.bound || strcmp(boot, r->context.boot))) {
        if (!bind(&r->context, boot)) unknown = true;
    } else if (binding == 0) r->context.bound = false;
    /* An old backend during normal hot update has no matching new owner. An
     * unreadable runtime retains an already bound lease instead of unblocking. */
    if (r->context.bound) {
        for (slot = 0; slot < 2; ++slot) {
            int found = read_lease(&r->context, slot, &lease);
            if (found < 0) unknown = true;
            if (found == 1) {
                if (lease.issued > now) unknown = true;
                if (lease.visible && now < lease.expires) active = true;
                if (lease.issued >= newest.issued) newest = lease;
            }
        }
    }
    if (active || unknown) {
        r->blocked = r->needs_neutral = true;
        return true;
    }
    if (newest.owner[0] && strcmp(newest.owner, r->acknowledged_owner)) r->needs_neutral = true;
    if (r->needs_neutral && non_neutral) { r->blocked = true; return true; }
    if (newest.owner[0]) snprintf(r->acknowledged_owner, sizeof(r->acknowledged_owner), "%s", newest.owner);
    r->blocked = r->needs_neutral = false;
    return false;
}
