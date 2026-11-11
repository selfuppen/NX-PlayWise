#include "notification_text.h"
/* The background links only these short messages, never the drawing catalog. */
const char *ptc_notification_text(PtcUiTextId id, PtcUiLanguagePreference lang)
{
    if (lang == PTC_UI_LANGUAGE_SIMPLIFIED) {
        switch (id) {
#define PTC_UI_TEXT_VALUE(key, value) case key: return value;
#include "ui_text/zh_hans/notification.inc"
#undef PTC_UI_TEXT_VALUE
        default: return "";
        }
    }
    if (lang == PTC_UI_LANGUAGE_TRADITIONAL) {
        switch (id) {
#define PTC_UI_TEXT_VALUE(key, value) case key: return value;
#include "ui_text/zh_hant/notification.inc"
#undef PTC_UI_TEXT_VALUE
        default: return "";
        }
    }
    switch (id) {
#define PTC_UI_TEXT_VALUE(key, value) case key: return value;
#include "ui_text/en/notification.inc"
#undef PTC_UI_TEXT_VALUE
    default: return "";
    }
}
