#include "support_export.h"

#include <string.h>

static const char *const PTC_SUPPORT_FILES[] = {
    "build.json",
    "config.json",
    "rules.json",
    "state.json",
    "setup.json",
    "compatibility.json",
    "environment.json",
};

static const char *const PTC_SENSITIVE_MARKERS[] = {
    "\"grant_secret\"",
    "\"pin_hash\"",
    "\"pin_salt\"",
    "\"offline_code\"",
    "\"nonce\"",
    "\"used_nonces\"",
    "credentials.json",
    "auth.json",
    "ledger/",
    "recovery/active",
};

size_t ptc_support_export_file_count(void)
{
    return sizeof(PTC_SUPPORT_FILES) / sizeof(PTC_SUPPORT_FILES[0]);
}

const char *ptc_support_export_file(size_t index)
{
    return index < ptc_support_export_file_count() ? PTC_SUPPORT_FILES[index] : NULL;
}

bool ptc_support_export_text_safe(const char *text)
{
    size_t index;
    if (!text) return false;
    for (index = 0; index < sizeof(PTC_SENSITIVE_MARKERS) / sizeof(PTC_SENSITIVE_MARKERS[0]); ++index) {
        if (strstr(text, PTC_SENSITIVE_MARKERS[index])) return false;
    }
    return true;
}
