#include "ultrahand_config.h"
#include "../third_party/cjson/cJSON.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OVLMENU_PATH "sdmc:/switch/.overlays/ovlmenu.ovl"
#define PLAYWISE_OVL_PATH "sdmc:/switch/.overlays/playwise.ovl"
#define OVLLOADER_EXEFS_PATH "sdmc:/atmosphere/contents/420000000007E51A/exefs.nsp"
#define OVLLOADER_BOOT2_PATH "sdmc:/atmosphere/contents/420000000007E51A/flags/boot2.flag"
#define OVLLOADER_TOOLBOX_PATH "sdmc:/atmosphere/contents/420000000007E51A/toolbox.json"
#define ULTRAHAND_CONFIG_PATH "sdmc:/config/ultrahand/config.ini"
#define ULTRAHAND_OVERLAYS_PATH "sdmc:/config/ultrahand/overlays.ini"
#define ULTRAHAND_PLAYWISE_MUTE_FLAG "sdmc:/config/ultrahand/flags/notifications/playwise.flag"

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p)
{
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

static void copy_cstr_bounded(char *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
    size_t len = 0;
    if (!dst || dst_size == 0) return;
    while (len < src_size && src[len] != '\0') ++len;
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static bool str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool str_contains_ci(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0) return true;
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) ++i;
        if (i == nlen) return true;
    }
    return false;
}

bool ptc_overlay_version_parse(const char *raw, PtcOverlayVersion *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!raw || !raw[0]) return false;
    snprintf(out->raw, sizeof(out->raw), "%s", raw);
    const char *p = raw;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 'v' || *p == 'V') ++p;
    if (!isdigit((unsigned char)*p)) return false;

    unsigned parts[3] = {0, 0, 0};
    int count = 0;
    while (count < 3 && isdigit((unsigned char)*p)) {
        unsigned val = 0;
        while (isdigit((unsigned char)*p)) {
            if (val > 6553) return false;
            val = val * 10u + (unsigned)(*p - '0');
            if (val > 65535u) return false;
            ++p;
        }
        parts[count++] = val;
        if (*p == '.') {
            ++p;
            if (!isdigit((unsigned char)*p)) return false;
        } else {
            break;
        }
    }
    if (count < 2) return false;
    /* Allow optional trailing suffix like -beta.1, +dirty, whitespace, or end-of-string. */
    if (*p != '\0' && *p != '-' && *p != '+' && *p != ' ' && *p != '\t') return false;
    out->major = (uint16_t)parts[0];
    out->minor = (uint16_t)parts[1];
    out->patch = (uint16_t)(count >= 3 ? parts[2] : 0);
    out->valid = true;
    return true;
}

bool ptc_overlay_version_at_least(const PtcOverlayVersion *v,
                                  uint16_t major, uint16_t minor, uint16_t patch)
{
    if (!v || !v->valid) return false;
    if (v->major != major) return v->major > major;
    if (v->minor != minor) return v->minor > minor;
    return v->patch >= patch;
}

/* Bounded, exact section/key lookup. Ambiguous duplicate sections or keys return -1. */
static int ini(const char *text, const char *section, const char *key, char *out, size_t size)
{
    bool inside = false, found = false;
    unsigned sections = 0;
    const char *p = text;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        while (n && (p[n-1] == '\r' || p[n-1] == ' ' || p[n-1] == '\t')) --n;
        while (n && (*p == ' ' || *p == '\t')) { ++p; --n; }
        if (n && *p == '[') {
            inside = n == strlen(section) + 2 && p[n-1] == ']' && !strncmp(p+1, section, n-2);
            if (inside && ++sections > 1) return -1;
        } else if (inside && n > strlen(key) && !strncmp(p, key, strlen(key))) {
            const char *v = p + strlen(key);
            while (v < p+n && (*v == ' ' || *v == '\t')) ++v;
            if (v < p+n && *v == '=') {
                ++v;
                while (v < p+n && (*v == ' ' || *v == '\t')) ++v;
                size_t length = (size_t)(p+n-v);
                if (found || length >= size) return -1;
                memcpy(out, v, length); out[length] = '\0'; found = true;
            }
        }
        if (!end) break;
        p = end + 1;
    }
    if (sections == 0) return -2; /* section itself absent */
    return found ? 1 : 0;
}

bool ptc_ultrahand_combo(const char *text, char *out, size_t size)
{
    static const char *tokens[] = {"A","B","X","Y","L","R","ZL","ZR","PLUS","MINUS",
        "DUP","DDOWN","DLEFT","DRIGHT","LS","RS"};
    static const char *labels[] = {"A","B","X","Y","L","R","ZL","ZR","+","−",
        "↑","↓","←","→","LS","RS"};
    unsigned seen = 0, count = 0;
    if (!size) return false;
    out[0] = '\0';
    while (text && *text) {
        const char *end = strchr(text, '+');
        size_t n = end ? (size_t)(end-text) : strlen(text);
        unsigned i;
        for (i = 0; i < 16; ++i) if (strlen(tokens[i]) == n && !strncmp(text, tokens[i], n)) break;
        if (i == 16 || (seen & (1u << i)) || ++count > 4) goto invalid;
        seen |= 1u << i;
        size_t used = strlen(out), need = strlen(labels[i]) + (count > 1 ? strlen("＋") : 0);
        if (used + need >= size) goto invalid;
        if (count > 1) strcat(out, "＋");
        strcat(out, labels[i]);
        if (!end) {
            if (count >= 2) return true;
            goto invalid;
        }
        text = end+1;
    }
invalid:
    out[0] = '\0';
    return false;
}

static bool file_is_regular(PtcStorage *s, const char *path)
{
    PtcStorageMetadata meta;
    return s && s->vtable->metadata && s->vtable->metadata(s, path, &meta) &&
        meta.type == PTC_STORAGE_ENTRY_FILE;
}

static void probe_ovlmenu_nacp(PtcStorage *s, PtcOverlayEnv *out);

void ptc_ultrahand_config_read(PtcStorage *s, PtcUltrahandConfig *out)
{
    char text[16384], value[128], args[128];
    memset(out, 0, sizeof(*out));
    out->notifications = -1;
    if (!s) return;
    out->app_muted = file_is_regular(s, ULTRAHAND_PLAYWISE_MUTE_FLAG);
    if (s->vtable->read_text(s, ULTRAHAND_CONFIG_PATH, text, sizeof(text))) {
        int found = ini(text, "ultrahand", "notifications", value, sizeof(value));
        if (found == 1) {
            if (!strcmp(value, "true")) out->notifications = 1;
            if (!strcmp(value, "false")) out->notifications = 0;
        } else if (found == 0) {
            /* Ultrahand >= 2.3.0 defaults an omitted notifications key inside [ultrahand] to true. */
            PtcOverlayEnv env;
            memset(&env, 0, sizeof(env));
            probe_ovlmenu_nacp(s, &env);
            if (env.ultrahand_notify_supported) out->notifications = 1;
        }
    }
    if (s->vtable->read_text(s, ULTRAHAND_OVERLAYS_PATH, text, sizeof(text)) &&
        ini(text, "playwise.ovl", "key_combo", value, sizeof(value)) == 1) {
        int launch = ini(text, "playwise.ovl", "use_launch_args", args, sizeof(args));
        if (launch == 0 || (launch == 1 && !strcmp(args, "false")))
            (void)ptc_ultrahand_combo(value, out->shortcut, sizeof(out->shortcut));
    }
}

static void probe_ovlmenu_nacp(PtcStorage *s, PtcOverlayEnv *out)
{
    PtcStorageMetadata meta;
    if (!s || !s->vtable->metadata || !s->vtable->metadata(s, OVLMENU_PATH, &meta)) {
        out->manager = PTC_OVERLAY_MANAGER_UNKNOWN;
        return;
    }
    if (meta.type == PTC_STORAGE_ENTRY_MISSING) {
        out->manager = PTC_OVERLAY_MANAGER_MISSING;
        return;
    }
    if (meta.type != PTC_STORAGE_ENTRY_FILE || !s->vtable->read_range) {
        out->manager = PTC_OVERLAY_MANAGER_UNKNOWN;
        return;
    }
    uint8_t nro_hdr[0x20];
    size_t got = 0;
    if (!s->vtable->read_range(s, OVLMENU_PATH, 0x10, nro_hdr, sizeof(nro_hdr), &got) ||
        got < 0x0C || read_le32(nro_hdr) != 0x304F524EU) { /* "NRO0" */
        out->manager = PTC_OVERLAY_MANAGER_UNKNOWN;
        return;
    }
    uint32_t nro_size = read_le32(nro_hdr + 0x08);
    if (nro_size < 0x80) {
        out->manager = PTC_OVERLAY_MANAGER_UNKNOWN;
        return;
    }
    uint8_t aset_hdr[0x38];
    if (!s->vtable->read_range(s, OVLMENU_PATH, (uint64_t)nro_size, aset_hdr, sizeof(aset_hdr), &got) ||
        got < sizeof(aset_hdr) || read_le32(aset_hdr) != 0x54455341U) { /* "ASET" */
        out->manager = PTC_OVERLAY_MANAGER_UNKNOWN;
        return;
    }
    uint64_t nacp_off = read_le64(aset_hdr + 0x18);
    uint64_t nacp_size = read_le64(aset_hdr + 0x20);
    if (nacp_size < 0x3070 || nacp_off > 0x10000000ULL) {
        out->manager = PTC_OVERLAY_MANAGER_UNKNOWN;
        return;
    }
    uint64_t nacp_base = (uint64_t)nro_size + nacp_off;
    uint8_t lang_entry[0x300];
    uint8_t ver_bytes[0x10];
    if (!s->vtable->read_range(s, OVLMENU_PATH, nacp_base, lang_entry, sizeof(lang_entry), &got) ||
        got < sizeof(lang_entry)) {
        out->manager = PTC_OVERLAY_MANAGER_UNKNOWN;
        return;
    }
    if (!s->vtable->read_range(s, OVLMENU_PATH, nacp_base + 0x3060ULL, ver_bytes, sizeof(ver_bytes), &got) ||
        got < sizeof(ver_bytes)) {
        out->manager = PTC_OVERLAY_MANAGER_UNKNOWN;
        return;
    }
    copy_cstr_bounded(out->manager_title, sizeof(out->manager_title), lang_entry, 0x200);
    copy_cstr_bounded(out->manager_author, sizeof(out->manager_author), lang_entry + 0x200, 0x100);
    char ver_str[17];
    copy_cstr_bounded(ver_str, sizeof(ver_str), ver_bytes, sizeof(ver_bytes));
    (void)ptc_overlay_version_parse(ver_str, &out->manager_version);

    if (str_contains_ci(out->manager_title, "Ultrahand") || str_ieq(out->manager_author, "ppkantorski")) {
        out->manager = PTC_OVERLAY_MANAGER_ULTRAHAND;
        out->ultrahand_notify_supported = ptc_overlay_version_at_least(&out->manager_version, 2, 3, 0);
    } else if (str_contains_ci(out->manager_title, "Tesla")) {
        out->manager = PTC_OVERLAY_MANAGER_TESLA;
    } else {
        out->manager = PTC_OVERLAY_MANAGER_UNKNOWN;
    }
}

static void probe_ovlloader(PtcStorage *s, PtcOverlayEnv *out)
{
    char text[1024];
    out->loader_installed = file_is_regular(s, OVLLOADER_EXEFS_PATH);
    out->loader_boot2 = file_is_regular(s, OVLLOADER_BOOT2_PATH);
    if (s->vtable->read_text(s, OVLLOADER_TOOLBOX_PATH, text, sizeof(text))) {
        cJSON *root = cJSON_ParseWithOpts(text, NULL, true);
        if (cJSON_IsObject(root)) {
            const char *ver = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "version"));
            if (ver) (void)ptc_overlay_version_parse(ver, &out->loader_version);
        }
        cJSON_Delete(root);
    }
}

/* Parse a single boolean key inside a unique [section] and replace false<->true byte-preservingly. */
static PtcUltrahandPrepare replace_ini_bool(PtcStorage *s, const char *file, const char *section,
                                            const char *key, bool desired, char *buf_a, char *buf_b,
                                            char *buf_c, size_t buf_size)
{
    char value[128];
    const char *want = desired ? "true" : "false";
    const char *other = desired ? "false" : "true";
    if (!s->vtable->read_text(s, file, buf_a, buf_size) ||
        ini(buf_a, section, key, value, sizeof(value)) != 1 ||
        (strcmp(value, "true") && strcmp(value, "false")))
        return PTC_ULTRAHAND_PREPARE_UNAVAILABLE;

    size_t sec_len = strlen(section);
    size_t key_len = strlen(key);
    const char *p = buf_a, *target = NULL;
    unsigned sections = 0;
    bool inside = false;
    while (*p) {
        const char *end = strchr(p, '\n');
        if (!end) end = p + strlen(p);
        const char *start = p, *trim = end;
        while (start < trim && (*start == ' ' || *start == '\t')) ++start;
        while (trim > start && (trim[-1] == '\r' || trim[-1] == ' ' || trim[-1] == '\t')) --trim;
        if (start < trim && *start == '[') {
            inside = (size_t)(trim - start) == sec_len + 2 && trim[-1] == ']' &&
                !strncmp(start + 1, section, sec_len);
            if (inside) ++sections;
        } else if (inside && (size_t)(trim - start) > key_len && !strncmp(start, key, key_len)) {
            const char *v = start + key_len;
            while (v < trim && (*v == ' ' || *v == '\t')) ++v;
            if (v < trim && *v++ == '=') {
                while (v < trim && (*v == ' ' || *v == '\t')) ++v;
                target = v;
            }
        }
        p = *end ? end + 1 : end;
    }
    if (sections != 1 || !target) return PTC_ULTRAHAND_PREPARE_UNAVAILABLE;
    if (!strcmp(value, want)) return PTC_ULTRAHAND_PREPARE_ALREADY_ON;
    size_t offset = (size_t)(target - buf_a);
    size_t want_len = strlen(want);
    size_t other_len = strlen(other);
    size_t tail_len = strlen(target + other_len);
    if (offset + want_len + tail_len + 1u > buf_size) return PTC_ULTRAHAND_PREPARE_FAILED;
    memcpy(buf_b, buf_a, offset);
    memcpy(buf_b + offset, want, want_len);
    memcpy(buf_b + offset + want_len, target + other_len, tail_len + 1u);
    if (!s->vtable->read_text(s, file, buf_c, buf_size) || strcmp(buf_c, buf_a))
        return PTC_ULTRAHAND_PREPARE_FAILED;
    if (!s->vtable->write_text_atomic(s, file, buf_b) ||
        !s->vtable->read_text(s, file, buf_c, buf_size) || strcmp(buf_c, buf_b))
        return PTC_ULTRAHAND_PREPARE_FAILED;
    return PTC_ULTRAHAND_PREPARE_SAVED;
}

PtcUltrahandPrepare ptc_ultrahand_prepare_notifications(PtcStorage *s)
{
    if (!s) return PTC_ULTRAHAND_PREPARE_UNAVAILABLE;
    /* If ovlmenu.ovl is actually present, refuse to touch config.ini for Tesla-Menu
       or Ultrahand < 2.3.0. When ovlmenu.ovl is absent (e.g. minimal fixtures), fall
       through to the strict config.ini validation contract. */
    PtcOverlayEnv env;
    memset(&env, 0, sizeof(env));
    probe_ovlmenu_nacp(s, &env);
    if (env.manager != PTC_OVERLAY_MANAGER_MISSING && !env.ultrahand_notify_supported)
        return PTC_ULTRAHAND_PREPARE_UNAVAILABLE;

    char text[16384], check[16384], current[16384];
    return replace_ini_bool(s, ULTRAHAND_CONFIG_PATH, "ultrahand", "notifications",
                            true, text, check, current, sizeof(text));
}

void ptc_overlay_env_probe(PtcStorage *s, PtcOverlayEnv *out)
{
    PtcUltrahandConfig cfg;
    char text[16384], value[128];
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->notifications = -1;
    if (!s) return;

    probe_ovlmenu_nacp(s, out);
    probe_ovlloader(s, out);
    out->playwise_overlay_installed = file_is_regular(s, PLAYWISE_OVL_PATH);

    ptc_ultrahand_config_read(s, &cfg);
    out->notifications = cfg.notifications;
    out->app_muted = cfg.app_muted;
    snprintf(out->shortcut, sizeof(out->shortcut), "%s", cfg.shortcut);

    /* Ultrahand >= 2.3.0 treats a missing notifications key inside [ultrahand] as true. */
    if ((out->notifications == -1 || out->notifications == 1) && out->ultrahand_notify_supported &&
        s->vtable->read_text(s, ULTRAHAND_CONFIG_PATH, text, sizeof(text)) &&
        ini(text, "ultrahand", "notifications", value, sizeof(value)) == 0) {
        out->notifications_implicit_default = true;
        out->notifications = 1;
    }
}

void ptc_overlay_env_format_manager(const PtcOverlayEnv *env, char *out, size_t size)
{
    if (!out || size == 0) return;
    out[0] = '\0';
    if (!env) return;
    if (env->manager == PTC_OVERLAY_MANAGER_ULTRAHAND) {
        if (env->manager_version.valid)
            snprintf(out, size, "Ultrahand %u.%u.%u",
                     env->manager_version.major, env->manager_version.minor, env->manager_version.patch);
        else
            snprintf(out, size, "Ultrahand");
    } else if (env->manager == PTC_OVERLAY_MANAGER_TESLA) {
        if (env->manager_version.raw[0])
            snprintf(out, size, "Tesla Menu %s", env->manager_version.raw);
        else
            snprintf(out, size, "Tesla Menu");
    } else if (env->manager == PTC_OVERLAY_MANAGER_UNKNOWN) {
        if (env->manager_title[0])
            snprintf(out, size, "%s", env->manager_title);
    }
}

static const char *manager_kind_name(PtcOverlayManagerKind kind)
{
    switch (kind) {
    case PTC_OVERLAY_MANAGER_ULTRAHAND: return "ultrahand";
    case PTC_OVERLAY_MANAGER_TESLA:     return "tesla";
    case PTC_OVERLAY_MANAGER_MISSING:   return "missing";
    default:                            return "unknown";
    }
}

static const char *prepare_name(PtcUltrahandPrepare p)
{
    switch (p) {
    case PTC_ULTRAHAND_PREPARE_SAVED:       return "saved";
    case PTC_ULTRAHAND_PREPARE_ALREADY_ON:  return "already_on";
    case PTC_ULTRAHAND_PREPARE_FAILED:      return "failed";
    default:                                return "unavailable";
    }
}

void ptc_overlay_env_boot_reconcile(PtcStorage *s, const char *root,
                                    bool notifications_enabled,
                                    PtcOverlayBootReport *out)
{
    PtcOverlayBootReport local;
    PtcOverlayBootReport *rep = out ? out : &local;
    memset(rep, 0, sizeof(*rep));
    if (!s) return;

    ptc_overlay_env_probe(s, &rep->env);
    snprintf(rep->notify_action, sizeof(rep->notify_action), "skipped_disabled");

    bool strict_env_ok = rep->env.ultrahand_notify_supported &&
        rep->env.loader_installed && rep->env.loader_boot2;

    if (notifications_enabled) {
        if (!strict_env_ok) {
            rep->notify_prepare = PTC_ULTRAHAND_PREPARE_UNAVAILABLE;
            snprintf(rep->notify_action, sizeof(rep->notify_action), "skipped_incompatible");
        } else if (rep->env.notifications == 1) {
            rep->notify_prepare = PTC_ULTRAHAND_PREPARE_ALREADY_ON;
            snprintf(rep->notify_action, sizeof(rep->notify_action), "already_on");
        } else {
            rep->notify_prepare = ptc_ultrahand_prepare_notifications(s);
            snprintf(rep->notify_action, sizeof(rep->notify_action), "%s", prepare_name(rep->notify_prepare));
        }
    }

    /* Refresh probe snapshot after any mutation so the diagnostic reflects final on-SD state. */
    ptc_overlay_env_probe(s, &rep->env);

    if (root && root[0]) {
        char path[256], json[1024], mgr_ver[32], ldr_ver[32];
        snprintf(path, sizeof(path), "%s/support/overlay-env.json", root);
        if (rep->env.manager_version.valid)
            snprintf(mgr_ver, sizeof(mgr_ver), "\"%u.%u.%u\"",
                     rep->env.manager_version.major, rep->env.manager_version.minor, rep->env.manager_version.patch);
        else
            snprintf(mgr_ver, sizeof(mgr_ver), "null");
        if (rep->env.loader_version.valid)
            snprintf(ldr_ver, sizeof(ldr_ver), "\"%u.%u.%u\"",
                     rep->env.loader_version.major, rep->env.loader_version.minor, rep->env.loader_version.patch);
        else
            snprintf(ldr_ver, sizeof(ldr_ver), "null");

        int len = snprintf(json, sizeof(json),
            "{\"version\":1,\"manager\":\"%s\",\"manager_version\":%s,"
            "\"loader_installed\":%s,\"loader_boot2\":%s,\"loader_version\":%s,"
            "\"playwise_overlay_installed\":%s,"
            "\"ultrahand_notify_supported\":%s,"
            "\"notifications\":%d,\"notifications_implicit_default\":%s,\"app_muted\":%s,"
            "\"boot_notify_action\":\"%s\"}\n",
            manager_kind_name(rep->env.manager), mgr_ver,
            rep->env.loader_installed ? "true" : "false",
            rep->env.loader_boot2 ? "true" : "false",
            ldr_ver,
            rep->env.playwise_overlay_installed ? "true" : "false",
            rep->env.ultrahand_notify_supported ? "true" : "false",
            rep->env.notifications,
            rep->env.notifications_implicit_default ? "true" : "false",
            rep->env.app_muted ? "true" : "false",
            rep->notify_action);
        if (len > 0 && (size_t)len < sizeof(json)) {
            (void)s->vtable->write_text_atomic(s, path, json);
        }
    }
}
