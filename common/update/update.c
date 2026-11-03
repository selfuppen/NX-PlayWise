#include "update.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
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

static bool prefix(const char *url, const char *base)
{
    return !strncmp(url, base, strlen(base));
}

bool ptc_update_url_for(const char *url, unsigned source, bool package, bool redirect)
{
    if (!url || source > 1 || strlen(url) >= 4096) return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p)
        if (*p <= 32 || *p >= 127 || *p == '\\' || *p == '#') return false;
    /* curl normalizes dot segments; reject path escapes before dispatch. Signed
       CDN query strings may contain percent escapes and are kept verbatim. */
    for (const char *p = url; *p && *p != '?'; ++p) {
        if (*p == '/' && p[1] == '.' && (p[2] == '/' || (p[2] == '.' && (p[3] == '/' || !p[3])) || !p[2])) return false;
        if (*p == '%' && p[1] && p[2]) {
            int a = tolower((unsigned char)p[1]), b = tolower((unsigned char)p[2]);
            if ((a == '2' && (b == 'e' || b == 'f')) || (a == '5' && b == 'c')) return false;
        }
    }
    if (!package) {
        if (!source) return !strcmp(url, PTC_UPDATE_REPO_URL) || (redirect &&
            prefix(url, "https://raw.giteeusercontent.com/selfuppen/nx-playwise/raw/update-feed/updates.json?") );
        return !strcmp(url, PTC_UPDATE_FALLBACK_URL);
    }
    if (!source) return prefix(url, "https://gitee.com/selfuppen/nx-playwise/releases/download/") ||
        (redirect && prefix(url, "https://gitee.com/attachments/download/"));
    return prefix(url, "https://github.com/selfuppen/NX-PlayWise/releases/download/") ||
        (redirect && (prefix(url, "https://release-assets.githubusercontent.com/github-production-release-asset/") ||
                      prefix(url, "https://objects.githubusercontent.com/github-production-release-asset-2e65be/")));
}

bool ptc_update_url_allowed(const char *url)
{
    return ptc_update_url_for(url, 0, false, true) || ptc_update_url_for(url, 1, false, true) ||
        ptc_update_url_for(url, 0, true, true) || ptc_update_url_for(url, 1, true, true);
}

static const char *field(const cJSON *o, const char *name)
{
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, name));
}

static bool offer_entry(const cJSON *o, PtcUpdateOffer *offer)
{
    const char *version = field(o, "version"), *tag = field(o, "tag"), *notes = field(o, "notes");
    const cJSON *pre = cJSON_GetObjectItemCaseSensitive(o, "prerelease"), *p = cJSON_GetObjectItemCaseSensitive(o, "package");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(p, "size");
    const char *md5 = field(p, "md5"), *sha = field(p, "sha256"), *name = field(p, "name");
    const char *gitee = field(p, "gitee_url"), *github = field(p, "github_url");
    int order; char expected[512];
    if (!cJSON_IsObject(o) || !version || !ptc_update_version_compare(version, version, &order) ||
        !tag || tag[0] != 'v' || strcmp(tag + 1, version) || !cJSON_IsBool(pre) ||
        !notes || strlen(notes) >= sizeof(offer->notes) || !cJSON_IsObject(p) ||
        !cJSON_IsNumber(size) || size->valuedouble < 1 || size->valuedouble > PTC_UPDATE_ZIP_LIMIT ||
        size->valuedouble != (double)(unsigned)size->valuedouble ||
        !ptc_update_hex(md5, 32) || !ptc_update_hex(sha, 64) || !name || !gitee || !github) return false;
    snprintf(expected, sizeof(expected), "playwise-%s.zip", version);
    if (strcmp(name, expected)) return false;
    snprintf(expected, sizeof(expected), "https://gitee.com/selfuppen/nx-playwise/releases/download/%s/%s", tag, name);
    if (strcmp(gitee, expected) || !ptc_update_url_for(gitee, 0, true, false)) return false;
    snprintf(expected, sizeof(expected), "https://github.com/selfuppen/NX-PlayWise/releases/download/%s/%s", tag, name);
    if (strcmp(github, expected) || !ptc_update_url_for(github, 1, true, false)) return false;
    memset(offer, 0, sizeof(*offer));
    strcpy(offer->version, version); strcpy(offer->md5, md5); strcpy(offer->sha256, sha);
    strcpy(offer->gitee_url, gitee); strcpy(offer->github_url, github);
    offer->size_bytes = (unsigned)size->valuedouble; offer->size_kib = (offer->size_bytes + 1023u) / 1024u;
    offer->prerelease = cJSON_IsTrue(pre) || ptc_update_version_prerelease(version);
    /* Remote descriptions are plain text; JSON validation already checked UTF-8. */
    size_t n = 0;
    for (const unsigned char *s = (const unsigned char *)notes; *s; ++s)
        if (*s >= 32 || *s == '\n') offer->notes[n++] = (char)*s;
    offer->notes[n] = 0;
    return true;
}

bool ptc_update_feed_parse(const char *text, PtcUpdateFeed *feed)
{
    if (!feed) return false;
    memset(feed, 0, sizeof(*feed));
    cJSON *o = ptc_update_parse_json(text);
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(o, "schema_version"), *items = cJSON_GetObjectItemCaseSensitive(o, "releases");
    bool ok = cJSON_IsNumber(schema) && schema->valuedouble == 1 && cJSON_IsArray(items) && cJSON_GetArraySize(items) <= 2;
    bool seen[2] = {false, false};
    for (const cJSON *v = ok ? items->child : NULL; v; v = v->next) {
        PtcUpdateOffer *offer = &feed->offers[feed->count];
        if (!offer_entry(v, offer) || seen[offer->prerelease ? 1 : 0]) { ok = false; break; }
        seen[offer->prerelease ? 1 : 0] = true; ++feed->count;
    }
    cJSON_Delete(o);
    if (!ok) memset(feed, 0, sizeof(*feed));
    return ok;
}

bool ptc_update_feed_select(const PtcUpdateFeed *feed, bool stable_only, PtcUpdateOffer *offer)
{
    if (!feed || !offer || feed->count > 2) return false;
    memset(offer, 0, sizeof(*offer));
    for (unsigned i = 0; i < feed->count; ++i) {
        const PtcUpdateOffer *v = &feed->offers[i]; int order;
        if (stable_only && v->prerelease) continue;
        if (!offer->version[0] || (ptc_update_version_compare(v->version, offer->version, &order) && order > 0)) *offer = *v;
    }
    /* Retain an excluded offer so UI can explain an empty stable channel. */
    if (!offer->version[0] && feed->count) *offer = feed->offers[0];
    return true;
}

bool ptc_update_offer_parse(const char *text, PtcUpdateOffer *offer)
{
    PtcUpdateFeed feed;
    return ptc_update_feed_parse(text, &feed) && ptc_update_feed_select(&feed, false, offer);
}

PtcUpdateStatus ptc_update_offer_status(const PtcUpdateOffer *offer, const char *local, bool stable_only)
{
    int order;
    if (!offer) return PTC_UPDATE_FAILED;
    if (!offer->version[0]) return PTC_UPDATE_CURRENT;
    if (!ptc_update_version_compare(offer->version, local, &order)) return PTC_UPDATE_FAILED;
    if (stable_only && (offer->prerelease || ptc_update_version_prerelease(offer->version))) return PTC_UPDATE_CHANNEL_EXCLUDED;
    if (order < 0) return PTC_UPDATE_LOCAL_NEWER;
    if (!order) return PTC_UPDATE_CURRENT;
    return PTC_UPDATE_AVAILABLE;
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
