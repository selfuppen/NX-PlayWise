#include "ui_theme.h"

#include <string.h>

static const PtcUiPalette LIGHT_PALETTE = {
    0xF0F3FA,
    0xFFFFFF,
    0xE8EDF7,
    0xDBE1E9,
    0x79879D,
    0x172640,
    0x53627A,
    0x9199A5,
    0x245BC4,
    0x1247A8,
    0xFFFFFF,
    0x16734E,
    0x865600,
    0xB92F46,
    0xE4EDFF,
    0x214EA3,
    0xFFFFFF,
    0xDCE8FF,
    0xF67C73,
    0xE5F4EB,
    0xFFF1D8,
    0xFFE9ED,
    0xD7DFEC,
};

static const PtcUiPalette DARK_PALETTE = {
    0x0B0F19,
    0x162032,
    0x1E2C44,
    0x2A3B54,
    0x7E90AA,
    0xF3F6FD,
    0xB6C4D9,
    0x818C98,
    0x91B9FF,
    0xBCD4FF,
    0x122344,
    0x5CCB8A,
    0xF2C14E,
    0xFF7A85,
    0x273E60,
    0x233F6A,
    0xFFFFFF,
    0xDCE8FF,
    0xFF938A,
    0x193C32,
    0x40351F,
    0x462936,
    0x070B14,
};

bool ptc_ui_theme_parse_preference(const char *text, PtcUiThemePreference *out)
{
    if (!text || !out) return false;
    if (strcmp(text, "system") == 0) *out = PTC_UI_THEME_SYSTEM;
    else if (strcmp(text, "light") == 0) *out = PTC_UI_THEME_LIGHT;
    else if (strcmp(text, "dark") == 0) *out = PTC_UI_THEME_DARK;
    else return false;
    return true;
}

const char *ptc_ui_theme_preference_name(PtcUiThemePreference preference)
{
    switch (preference) {
    case PTC_UI_THEME_LIGHT: return "light";
    case PTC_UI_THEME_DARK: return "dark";
    case PTC_UI_THEME_SYSTEM:
    default: return "system";
    }
}

const char *ptc_ui_theme_preference_label(PtcUiThemePreference preference)
{
    switch (preference) {
    case PTC_UI_THEME_LIGHT: return "浅色";
    case PTC_UI_THEME_DARK: return "暗色";
    case PTC_UI_THEME_SYSTEM:
    default: return "跟随系统";
    }
}

PtcUiResolvedTheme ptc_ui_theme_resolve(
    PtcUiThemePreference preference,
    PtcUiSystemTheme system_theme)
{
    if (preference == PTC_UI_THEME_DARK) return PTC_UI_RESOLVED_DARK;
    if (preference == PTC_UI_THEME_LIGHT) return PTC_UI_RESOLVED_LIGHT;
    /* A failed or unknown system observation preserves the legacy light UI. */
    return system_theme == PTC_UI_SYSTEM_THEME_DARK
        ? PTC_UI_RESOLVED_DARK : PTC_UI_RESOLVED_LIGHT;
}

const PtcUiPalette *ptc_ui_theme_palette(PtcUiResolvedTheme theme)
{
    return theme == PTC_UI_RESOLVED_DARK ? &DARK_PALETTE : &LIGHT_PALETTE;
}

PtcUiThemeView ptc_ui_theme_make_view(
    PtcUiThemePreference preference,
    PtcUiSystemTheme system_theme)
{
    PtcUiThemeView view;
    view.preference = preference;
    view.resolved = ptc_ui_theme_resolve(preference, system_theme);
    view.palette = ptc_ui_theme_palette(view.resolved);
    view.system_theme_available = system_theme != PTC_UI_SYSTEM_THEME_UNAVAILABLE;
    return view;
}
