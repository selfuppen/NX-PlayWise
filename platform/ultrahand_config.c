#include "ultrahand_config.h"
#include <stdio.h>
#include <string.h>

/* Bounded, exact section/key lookup. Ambiguous duplicate values stay unknown. */
static int ini(const char *text, const char *section, const char *key, char *out, size_t size)
{
    bool inside = false, found = false;
    const char *p = text;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        while (n && (p[n-1] == '\r' || p[n-1] == ' ' || p[n-1] == '\t')) --n;
        while (n && (*p == ' ' || *p == '\t')) { ++p; --n; }
        if (n && *p == '[') inside = n == strlen(section) + 2 &&
            p[n-1] == ']' && !strncmp(p+1, section, n-2);
        else if (inside && n > strlen(key) && !strncmp(p, key, strlen(key))) {
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
    return found;
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

void ptc_ultrahand_config_read(PtcStorage *s, PtcUltrahandConfig *out)
{
    char text[16384], value[128], args[128];
    memset(out, 0, sizeof(*out));
    out->notifications = -1;
    if (s->vtable->read_text(s, "sdmc:/config/ultrahand/config.ini", text, sizeof(text)) &&
        ini(text, "ultrahand", "notifications", value, sizeof(value)) == 1) {
        if (!strcmp(value, "true")) out->notifications = 1;
        if (!strcmp(value, "false")) out->notifications = 0;
    }
    if (s->vtable->read_text(s, "sdmc:/config/ultrahand/overlays.ini", text, sizeof(text)) &&
        ini(text, "playwise.ovl", "key_combo", value, sizeof(value)) == 1) {
        int launch = ini(text, "playwise.ovl", "use_launch_args", args, sizeof(args));
        if (launch == 0 || (launch == 1 && !strcmp(args, "false")))
            (void)ptc_ultrahand_combo(value, out->shortcut, sizeof(out->shortcut));
    }
}

PtcUltrahandPrepare ptc_ultrahand_prepare_notifications(PtcStorage *s)
{
    static const char *file = "sdmc:/config/ultrahand/config.ini";
    char text[16384], check[16384], value[128];
    if (!s->vtable->read_text(s, file, text, sizeof(text)) ||
        ini(text, "ultrahand", "notifications", value, sizeof(value)) != 1 ||
        (strcmp(value, "true") && strcmp(value, "false"))) return PTC_ULTRAHAND_PREPARE_UNAVAILABLE;
    /* Reject duplicated sections even if just one has the key. Preserve every
       byte outside the boolean value, including CRLF, comments and other keys. */
    const char *p = text, *target = NULL;
    unsigned sections = 0;
    bool inside = false;
    while (*p) {
        const char *end = strchr(p, '\n');
        if (!end) end = p + strlen(p);
        const char *start = p, *trim = end;
        while (start < trim && (*start == ' ' || *start == '\t')) ++start;
        while (trim > start && (trim[-1] == '\r' || trim[-1] == ' ' || trim[-1] == '\t')) --trim;
        if (start < trim && *start == '[') {
            inside = trim - start == 11 && !strncmp(start, "[ultrahand]", 11);
            if (inside) ++sections;
        } else if (inside && trim - start > 13 && !strncmp(start, "notifications", 13)) {
            const char *v = start + 13;
            while (v < trim && (*v == ' ' || *v == '\t')) ++v;
            if (v < trim && *v++ == '=') {
                while (v < trim && (*v == ' ' || *v == '\t')) ++v;
                target = v;
            }
        }
        p = *end ? end + 1 : end;
    }
    if (sections != 1 || !target) return PTC_ULTRAHAND_PREPARE_UNAVAILABLE;
    if (!strcmp(value, "true")) return PTC_ULTRAHAND_PREPARE_ALREADY_ON;
    size_t offset = (size_t)(target - text);
    memcpy(check, text, offset);
    memcpy(check + offset, "true", 4);
    strcpy(check + offset + 4, target + 5);
    /* No manager reload or fabricated input. A concurrent change aborts before
       replacement; the storage adapter supplies the atomic rename. */
    char current[16384];
    if (!s->vtable->read_text(s, file, current, sizeof(current)) || strcmp(current, text))
        return PTC_ULTRAHAND_PREPARE_FAILED;
    if (!s->vtable->write_text_atomic(s, file, check) ||
        !s->vtable->read_text(s, file, current, sizeof(current)) || strcmp(current, check))
        return PTC_ULTRAHAND_PREPARE_FAILED;
    return PTC_ULTRAHAND_PREPARE_SAVED;
}
