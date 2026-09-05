#include "ui_theme.h"

#include <string.h>

static const PtcUiPalette LIGHT_PALETTE = {
    .page_bg = 0xF0F3FA,
    .surface = 0xFFFFFF,
    .surface_raised = 0xE8EDF7,
    .border_decorative = 0xDBE1E9,
    .border_control = 0x79879D,
    .text_primary = 0x172640,
    .text_secondary = 0x53627A,
    .text_disabled = 0x9199A5,
    .accent = 0x245BC4,
    .focus = 0x1247A8,
    .on_accent = 0xFFFFFF,
    .success = 0x16734E,
    .warning = 0x865600,
    .danger = 0xB92F46,
    .accent_soft = 0xE4EDFF,
    .hero = 0x214EA3,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xDCE8FF,
    .coral = 0xF67C73,
    .success_soft = 0xE5F4EB,
    .warning_soft = 0xFFF1D8,
    .danger_soft = 0xFFE9ED,
    .scrim = 0xD7DFEC,
    .key_glyph_bg = 0xDCE3ED,
    .key_glyph_border = 0xB8C5D6,
    .gauge_slot = 0xD3DCED,
    .gauge_slot_border = 0x3D5375,
    .shadow_strength = 100,
};

static const PtcUiPalette DARK_PALETTE = {
    .page_bg = 0x0B0F19,
    .surface = 0x162032,
    .surface_raised = 0x1E2C44,
    .border_decorative = 0x2A3B54,
    .border_control = 0x7E90AA,
    .text_primary = 0xF3F6FD,
    .text_secondary = 0xB6C4D9,
    .text_disabled = 0x818C98,
    .accent = 0x91B9FF,
    .focus = 0xBCD4FF,
    .on_accent = 0x122344,
    .success = 0x5CCB8A,
    .warning = 0xF2C14E,
    .danger = 0xFF7A85,
    .accent_soft = 0x273E60,
    .hero = 0x233F6A,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xDCE8FF,
    .coral = 0xFF938A,
    .success_soft = 0x193C32,
    .warning_soft = 0x40351F,
    .danger_soft = 0x462936,
    .scrim = 0x070B14,
    .key_glyph_bg = 0x24354D,
    .key_glyph_border = 0x3D506E,
    .gauge_slot = 0x19273D,
    .gauge_slot_border = 0x3D5375,
    .shadow_strength = 130,
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
