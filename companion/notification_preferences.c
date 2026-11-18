#include "notification_preferences.h"
#include "../third_party/cjson/cJSON.h"
#include <string.h>

bool ptc_reason_notification_preferences(const char *config, bool *enabled,
    PtcUiLanguagePreference *language)
{
    cJSON *root = config ? cJSON_ParseWithOpts(config, NULL, true) : NULL;
    bool valid = cJSON_IsObject(root);
    for (const cJSON *a = root ? root->child : NULL; valid && a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) valid = false;
    *enabled = valid && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "reason_notifications_enabled"));
    if (language) {
        const char *lang = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "ui_language"));
        if (!lang || !lang[0] || !strcmp(lang, "system"))
            lang = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "reason_notification_language"));
        *language = lang && !strcmp(lang, "zh-Hans") ? PTC_UI_LANGUAGE_SIMPLIFIED :
            lang && !strcmp(lang, "zh-Hant") ? PTC_UI_LANGUAGE_TRADITIONAL : PTC_UI_LANGUAGE_ENGLISH;
    }
    cJSON_Delete(root);
    return valid;
}
