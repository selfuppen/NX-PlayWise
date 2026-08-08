#include "credential_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool device_char_valid(unsigned char value)
{
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') || value == '-' || value == '_';
}

bool ptc_device_id_valid(const char *value)
{
    size_t index;
    size_t length = value ? strlen(value) : 0U;
    if (length == 0U || length > PTC_DEVICE_ID_MAX_LEN) return false;
    for (index = 0; index < length; ++index) {
        if (!device_char_valid((unsigned char)value[index])) return false;
    }
    return true;
}

bool ptc_grant_secret_valid(const char *value)
{
    size_t index;
    size_t length = value ? strlen(value) : 0U;
    if (length < PTC_GRANT_SECRET_MIN_LEN || length > PTC_GRANT_SECRET_MAX_LEN) return false;
    for (index = 0; index < length; ++index) {
        unsigned char byte = (unsigned char)value[index];
        if (byte < 0x21U || byte > 0x7eU) return false;
    }
    return true;
}

bool ptc_grant_secret_is_demo(const char *value)
{
    return value && strcmp(value, PTC_DEMO_GRANT_SECRET) == 0;
}

bool ptc_hex_from_random(const uint8_t *bytes, size_t byte_count, char *out, size_t out_size)
{
    static const char HEX[] = "0123456789abcdef";
    size_t index;
    if (!bytes || !out || out_size < byte_count * 2U + 1U) return false;
    for (index = 0; index < byte_count; ++index) {
        out[index * 2U] = HEX[bytes[index] >> 4];
        out[index * 2U + 1U] = HEX[bytes[index] & 0x0fU];
    }
    out[byte_count * 2U] = '\0';
    return true;
}

bool ptc_random_device_id(const uint8_t bytes[3], char *out, size_t out_size)
{
    char suffix[7];
    int written;
    if (!ptc_hex_from_random(bytes, 3U, suffix, sizeof(suffix))) return false;
    written = snprintf(out, out_size, "playwise-%s", suffix);
    return written > 0 && (size_t)written < out_size;
}

static bool append_text(char *out, size_t out_size, size_t *used, const char *text)
{
    size_t length = strlen(text);
    if (*used + length >= out_size) return false;
    memcpy(out + *used, text, length);
    *used += length;
    out[*used] = '\0';
    return true;
}

static bool append_encoded(char *out, size_t out_size, size_t *used, const char *text)
{
    static const char HEX[] = "0123456789ABCDEF";
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        if (device_char_valid(*cursor) || *cursor == '.' || *cursor == '~') {
            if (*used + 1U >= out_size) return false;
            out[(*used)++] = (char)*cursor;
        } else {
            if (*used + 3U >= out_size) return false;
            out[(*used)++] = '%';
            out[(*used)++] = HEX[*cursor >> 4];
            out[(*used)++] = HEX[*cursor & 0x0fU];
        }
        ++cursor;
    }
    out[*used] = '\0';
    return true;
}

static bool private_ipv4_host(const char *host)
{
    unsigned int a;
    unsigned int b;
    unsigned int c;
    unsigned int d;
    char tail;
    if (sscanf(host, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 ||
        a > 255U || b > 255U || c > 255U || d > 255U) return false;
    return a == 10U || a == 127U || (a == 172U && b >= 16U && b <= 31U) || (a == 192U && b == 168U);
}

static bool port_valid(const char *text, size_t length)
{
    size_t index;
    if (length == 0U || length > 5U) return false;
    for (index = 0; index < length; ++index) {
        if (text[index] < '0' || text[index] > '9') return false;
    }
    return true;
}

static bool authority_syntax_valid(const char *authority, size_t authority_length)
{
    size_t host_length = authority_length;
    size_t index;
    const char *colon;
    if (authority_length == 0U) return false;
    if (authority[0] == '[') {
        const char *close = memchr(authority, ']', authority_length);
        if (!close || close == authority + 1) return false;
        host_length = (size_t)(close - authority) + 1U;
        return host_length == authority_length ||
            (authority[host_length] == ':' && port_valid(authority + host_length + 1U, authority_length - host_length - 1U));
    }
    colon = memchr(authority, ':', authority_length);
    if (colon) {
        host_length = (size_t)(colon - authority);
        if (!port_valid(colon + 1, authority_length - host_length - 1U)) return false;
    }
    if (host_length == 0U || authority[0] == '.' || authority[host_length - 1U] == '.') return false;
    for (index = 0; index < host_length; ++index) {
        unsigned char ch = (unsigned char)authority[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '-')) return false;
    }
    return true;
}

static bool local_http_authority(const char *authority, size_t authority_length)
{
    char host[128];
    size_t host_length = authority_length;
    const char *colon;
    if (authority_length == 0U || authority_length >= sizeof(host) ||
        !authority_syntax_valid(authority, authority_length)) return false;
    if (authority[0] == '[') {
        const char *close = memchr(authority, ']', authority_length);
        if (!close) return false;
        host_length = (size_t)(close - authority) + 1U;
        if (host_length != authority_length && authority[host_length] != ':') return false;
    } else {
        colon = memchr(authority, ':', authority_length);
        if (colon) host_length = (size_t)(colon - authority);
    }
    if (host_length == 0U || host_length >= sizeof(host)) return false;
    memcpy(host, authority, host_length);
    host[host_length] = '\0';
    return strcmp(host, "localhost") == 0 || strcmp(host, "[::1]") == 0 || private_ipv4_host(host);
}

bool ptc_pairing_base_url_valid(const char *value)
{
    const char *authority;
    const char *authority_end;
    size_t length = value ? strlen(value) : 0U;
    size_t index;
    bool secure;
    if (length == 0U || length > PTC_PAIRING_BASE_URL_MAX_LEN) return false;
    for (index = 0; index < length; ++index) {
        unsigned char ch = (unsigned char)value[index];
        if (ch <= 0x20U || ch == 0x7fU || ch == '#') return false;
    }
    if (strncmp(value, "https://", 8U) == 0) {
        secure = true;
        authority = value + 8;
    } else if (strncmp(value, "http://", 7U) == 0) {
        secure = false;
        authority = value + 7;
    } else {
        return false;
    }
    authority_end = authority + strcspn(authority, "/?");
    if (authority_end == authority || memchr(authority, '@', (size_t)(authority_end - authority)) ||
        !authority_syntax_valid(authority, (size_t)(authority_end - authority))) return false;
    return secure || local_http_authority(authority, (size_t)(authority_end - authority));
}

bool ptc_build_pairing_url_with_base(
    const char *base_url,
    const char *device_id,
    const char *grant_secret,
    char *out,
    size_t out_size)
{
    size_t used = 0U;
    if (!out || out_size == 0U || !ptc_pairing_base_url_valid(base_url) ||
        !ptc_device_id_valid(device_id) || !ptc_grant_secret_valid(grant_secret)) {
        return false;
    }
    out[0] = '\0';
    return append_text(out, out_size, &used, base_url) &&
        append_text(out, out_size, &used, "#device_id=") &&
        append_encoded(out, out_size, &used, device_id) &&
        append_text(out, out_size, &used, "&grant_secret=") &&
        append_encoded(out, out_size, &used, grant_secret);
}

bool ptc_build_pairing_url(const char *device_id, const char *grant_secret, char *out, size_t out_size)
{
    return ptc_build_pairing_url_with_base(PTC_PAIRING_BASE_URL, device_id, grant_secret, out, out_size);
}
