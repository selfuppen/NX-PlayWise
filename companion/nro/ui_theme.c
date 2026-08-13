#include "ui_theme.h"

#include <string.h>

static const PtcUiPalette LIGHT_PALETTE = {
    0xF4F6F9,
    0xFFFFFF,
    0xF8FAFC,
    0xDBE1E9,
    0xCBD3DE,
    0x1C222B,
    0x5B6472,
    0x9199A5,
    0x1C76BC,
    0x1460A8,
    0xFFFFFF,
    0x19845F,
    0xAA6D12,
    0xC23D3D,
};

static const PtcUiPalette DARK_PALETTE = {
    0x000000,
    0x14191E,
    0x1C232A,
    0x303942,
    0x64717E,
    0xF2F5F7,
    0xAAB4BF,
    0x818C98,
    0x6EA8FE,
    0xA9C8FF,
    0x061425,
    0x5CCB8A,
    0xF2C14E,
    0xFF7A85,
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
