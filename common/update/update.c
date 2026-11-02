#include "update.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

const char *const ptc_update_files[PTC_UPDATE_FILE_COUNT] = {
    "atmosphere/contents/4200000000BD2300/exefs.nsp",
    "atmosphere/contents/4200000000BD2300/flags/boot2.flag",
    "switch/.overlays/playwise.ovl",
    "switch/playwise/defaults/auth.json", "switch/playwise/defaults/compatibility.json",
    "switch/playwise/defaults/config.json", "switch/playwise/defaults/rules.json",
    "switch/playwise/defaults/setup.json", "switch/playwise/defaults/state.json",
    "switch/playwise/fonts/emoji.ttf", "switch/playwise/package-artifacts.json",
    "switch/playwise/pctc.nro", "switch/playwise/build.json"
};

typedef struct { unsigned core[3]; char pre[64]; } Version;

static bool identifiers(const char *start, size_t length, bool pre)
{
    size_t part = 0;
    if (!length) return false;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || start[i] == '.') {
            bool numeric = true;
            if (i == part) return false;
            for (size_t j = part; j < i; ++j) if (!isdigit((unsigned char)start[j])) numeric = false;
            if (pre && numeric && i - part > 1 && start[part] == '0') return false;
            part = i + 1;
        } else if (!((start[i] >= 'a' && start[i] <= 'z') || (start[i] >= 'A' && start[i] <= 'Z') ||
                     (start[i] >= '0' && start[i] <= '9') || start[i] == '-')) return false;
    }
    return true;
}

static bool parse_version(const char *text, Version *v)
{
    const char *p = text;
    if (!p || !*p || strlen(p) >= 64) return false;
    memset(v, 0, sizeof(*v));
    for (int i = 0; i < 3; ++i) {
        const char *start = p;
        unsigned value = 0;
        while (*p >= '0' && *p <= '9') {
            if (value > (UINT_MAX - (unsigned)(*p - '0')) / 10u) return false;
            value = value * 10u + (unsigned)(*p++ - '0');
        }
        if (p == start || (p - start > 1 && *start == '0')) return false;
        v->core[i] = value;
        if (i != 2 && *p++ != '.') return false;
    }
    if (*p == '-') {
        const char *start = ++p;
        while (*p && *p != '+') ++p;
        if (!identifiers(start, (size_t)(p - start), true)) return false;
        memcpy(v->pre, start, (size_t)(p - start));
    }
    if (*p == '+') { ++p; if (!identifiers(p, strlen(p), false)) return false; p += strlen(p); }
    return !*p;
}

bool ptc_update_version_compare(const char *left, const char *right, int *order)
{
    Version a, b;
    if (!order || !parse_version(left, &a) || !parse_version(right, &b)) return false;
    *order = 0;
    for (int i = 0; i < 3; ++i) if (a.core[i] != b.core[i]) { *order = a.core[i] > b.core[i] ? 1 : -1; return true; }
    if (!a.pre[0] || !b.pre[0]) { *order = a.pre[0] ? -1 : (b.pre[0] ? 1 : 0); return true; }
    const char *x = a.pre, *y = b.pre;
    while (*x && *y) {
        size_t nx = strcspn(x, "."), ny = strcspn(y, ".");
        bool dx = true, dy = true;
        for (size_t i = 0; i < nx; ++i) if (!isdigit((unsigned char)x[i])) dx = false;
        for (size_t i = 0; i < ny; ++i) if (!isdigit((unsigned char)y[i])) dy = false;
        int cmp = 0;
        if (dx != dy) cmp = dx ? -1 : 1;
        else if (dx && nx != ny) cmp = nx > ny ? 1 : -1;
        else { cmp = memcmp(x, y, nx < ny ? nx : ny); if (!cmp && nx != ny) cmp = nx > ny ? 1 : -1; }
        if (cmp) { *order = cmp > 0 ? 1 : -1; return true; }
        x += nx; y += ny;
        if (*x) ++x;
        if (*y) ++y;
    }
    *order = *x ? 1 : (*y ? -1 : 0);
    return true;
}

bool ptc_update_version_prerelease(const char *version)
{
    Version v;
    return parse_version(version, &v) && v.pre[0];
}

static bool json_valid(const cJSON *node, unsigned depth)
{
    if (!node || depth > 24) return false;
    if (cJSON_IsObject(node)) {
        if (cJSON_GetArraySize(node) > 128) return false;
        for (const cJSON *v = node->child; v; v = v->next) {
            if (!v->string || !json_valid(v, depth + 1)) return false;
            for (const cJSON *other = v->next; other; other = other->next)
                if (other->string && !strcmp(v->string, other->string)) return false;
        }
    } else if (cJSON_IsArray(node)) {
        if (cJSON_GetArraySize(node) > 4096) return false;
        for (const cJSON *v = node->child; v; v = v->next) if (!json_valid(v, depth + 1)) return false;
    }
    return true;
}

cJSON *ptc_update_parse_json(const char *text)
{
    if (!text || strstr(text, "\\u0000")) return NULL;
    /* Bound recursion before invoking cJSON on the worker's small stack.
       Object width is bounded below before pairwise duplicate-key checks. */
    unsigned nesting = 0; bool quoted = false, escaped = false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (*p == '\\') escaped = true;
            else if (*p == '"') quoted = false;
        } else if (*p == '"') quoted = true;
        else if (*p == '{' || *p == '[') { if (++nesting > 24) return NULL; }
        else if (*p == '}' || *p == ']') { if (!nesting) return NULL; --nesting; }
    }
    if (quoted || nesting) return NULL;
    for (const unsigned char *p = (const unsigned char *)text; *p;) {
        if (*p < 128) { ++p; continue; }
        unsigned count = *p >= 0xc2 && *p <= 0xdf ? 2 : (*p >= 0xe0 && *p <= 0xef ? 3 : (*p >= 0xf0 && *p <= 0xf4 ? 4 : 0));
        if (!count) return NULL;
        unsigned code = *p & ((1u << (7u - count)) - 1u);
        for (unsigned i = 1; i < count; ++i) { if ((p[i] & 0xc0u) != 0x80u) return NULL; code = (code << 6) | (p[i] & 63u); }
        if ((count == 3 && code < 0x800u) || (count == 4 && code < 0x10000u) ||
            (code >= 0xd800u && code <= 0xdfffu) || code > 0x10ffffu) return NULL;
        p += count;
    }
    cJSON *root = text ? cJSON_ParseWithOpts(text, NULL, true) : NULL;
    if (!json_valid(root, 0)) { cJSON_Delete(root); return NULL; }
    return root;
}

bool ptc_update_hex(const char *text, size_t digits)
{
    if (!text || strlen(text) != digits) return false;
    for (size_t i = 0; i < digits; ++i) if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) return false;
    return true;
}

bool ptc_update_request_id(const char *text)
{
    if (!text || !*text || strlen(text) >= 80) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.')) return false;
    return strcmp(text, ".") && strcmp(text, "..");
}

bool ptc_update_offer_parse(const char *text, PtcUpdateOffer *offer)
{
    cJSON *root = ptc_update_parse_json(text);
    const cJSON *packages = cJSON_GetObjectItemCaseSensitive(root, "packages"), *found = NULL;
    bool ok = offer && cJSON_IsArray(packages);
    if (offer) memset(offer, 0, sizeof(*offer));
    for (const cJSON *v = ok ? packages->child : NULL; v; v = v->next) {
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(v, "name"));
        if (name && !strcmp(name, "NX-PlayWise")) { if (found) { ok = false; break; } found = v; }
    }
    const char *version = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(found, "version"));
    const char *md5 = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(found, "md5"));
    int order;
    ok = ok && version && ptc_update_version_compare(version, version, &order) && ptc_update_hex(md5, 32);
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(found, "filesize");
    if (size) ok = ok && cJSON_IsNumber(size) && size->valuedouble >= 0 && size->valuedouble <= PTC_UPDATE_ZIP_LIMIT / 1024u &&
        (double)(unsigned)size->valuedouble == size->valuedouble;
    if (ok) {
        memcpy(offer->version, version, strlen(version) + 1);
        memcpy(offer->md5, md5, 33);
        if (size) offer->size_kib = (unsigned)size->valuedouble;
        const char *notes = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(found, "changelog"));
        if (!notes || !*notes) notes = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(found, "details"));
        /* Remote descriptions stay plain text. Keep ASCII controls out of UI. */
        size_t n = 0;
        for (const unsigned char *p = (const unsigned char *)(notes ? notes : ""); *p;) {
            unsigned count = *p < 128 ? 1 : (*p < 0xe0 ? 2 : (*p < 0xf0 ? 3 : 4));
            if (n + count >= sizeof(offer->notes)) break;
            if (*p >= 32 || *p == '\n') { memcpy(offer->notes + n, p, count); n += count; }
            p += count;
        }
        offer->notes[n] = '\0';
    }
    cJSON_Delete(root);
    return ok;
}

PtcUpdateStatus ptc_update_offer_status(const PtcUpdateOffer *offer, const char *local, bool stable_only)
{
    int order;
    if (!offer || !ptc_update_version_compare(offer->version, local, &order)) return PTC_UPDATE_FAILED;
    if (order < 0) return PTC_UPDATE_LOCAL_NEWER;
    if (!order) return PTC_UPDATE_CURRENT;
    if ((stable_only || !ptc_update_version_prerelease(local)) && ptc_update_version_prerelease(offer->version)) return PTC_UPDATE_CHANNEL_EXCLUDED;
    return PTC_UPDATE_AVAILABLE;
}

bool ptc_update_url_allowed(const char *url)
{
    const char *prefix = "https://download.nswiki.cn/";
    if (!url || strncmp(url, prefix, strlen(prefix))) return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p) if (*p <= 32 || *p == 127 || *p == '\\') return false;
    return true;
}

int ptc_update_asset_index(const char *path)
{
    if (path) for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) if (!strcmp(path, ptc_update_files[i])) return i;
    return -1;
}

bool ptc_update_archive_path(const char *path, bool directory)
{
    if (!path || !*path || *path == '/' || strlen(path) >= 240) return false;
    const char *start = path;
    for (const char *p = path;; ++p) {
        if (*p == '\\' || *p == ':' || (*p && (unsigned char)*p < 32)) return false;
        if (!*p || *p == '/') {
            size_t n = (size_t)(p - start);
            if ((!n && *p) || (n == 1 && *start == '.') || (n == 2 && !memcmp(start, "..", 2))) return false;
            if (!*p) break;
            start = p + 1;
        }
    }
    if (!directory) return ptc_update_asset_index(path) >= 0 || !strcmp(path, "manifest.install") || !strcmp(path, "info.json");
    size_t n = strlen(path);
    if (path[n - 1] != '/') return false;
    if (!strncmp(path, "switch/playwise/", 16)) return true; /* Ignore empty runtime directories; never extract them. */
    for (int i = 0; i < PTC_UPDATE_FILE_COUNT; ++i) if (!strncmp(path, ptc_update_files[i], n)) return true;
    return false;
}
