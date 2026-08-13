#ifndef PLAYWISE_COMPANION_NRO_UI_THEME_H
#define PLAYWISE_COMPANION_NRO_UI_THEME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PTC_UI_THEME_SYSTEM = 0,
    PTC_UI_THEME_LIGHT = 1,
    PTC_UI_THEME_DARK = 2
} PtcUiThemePreference;

typedef enum {
    PTC_UI_SYSTEM_THEME_UNAVAILABLE = 0,
    PTC_UI_SYSTEM_THEME_LIGHT = 1,
    PTC_UI_SYSTEM_THEME_DARK = 2
} PtcUiSystemTheme;

typedef enum {
    PTC_UI_RESOLVED_LIGHT = 0,
    PTC_UI_RESOLVED_DARK = 1
} PtcUiResolvedTheme;

/* Colors are stored as platform-independent 0xRRGGBB values. */
typedef struct {
    uint32_t page_bg;
    uint32_t surface;
    uint32_t surface_raised;
    uint32_t border_decorative;
    uint32_t border_control;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t text_disabled;
    uint32_t accent;
    uint32_t focus;
    uint32_t on_accent;
    uint32_t success;
    uint32_t warning;
    uint32_t danger;
} PtcUiPalette;

typedef struct {
    PtcUiThemePreference preference;
    PtcUiResolvedTheme resolved;
    const PtcUiPalette *palette;
    bool system_theme_available;
} PtcUiThemeView;

bool ptc_ui_theme_parse_preference(const char *text, PtcUiThemePreference *out);
const char *ptc_ui_theme_preference_name(PtcUiThemePreference preference);
const char *ptc_ui_theme_preference_label(PtcUiThemePreference preference);
PtcUiResolvedTheme ptc_ui_theme_resolve(
    PtcUiThemePreference preference,
    PtcUiSystemTheme system_theme);
const PtcUiPalette *ptc_ui_theme_palette(PtcUiResolvedTheme theme);
PtcUiThemeView ptc_ui_theme_make_view(
    PtcUiThemePreference preference,
    PtcUiSystemTheme system_theme);

#endif
