#include "ui_theme.h"
#include "../ui_language.h"

#include <string.h>

static const PtcUiPalette LIGHT_PALETTE = {
    .page_bg = 0xF2F6F5,
    .surface = 0xFFFFFF,
    .surface_raised = 0xE5EFEB,
    .border_decorative = 0xD2DFDB,
    .border_control = 0x6E8A83,
    .text_primary = 0x132521,
    .text_secondary = 0x49655F,
    .text_disabled = 0x8A9E99,
    .accent = 0x126B5B,
    .focus = 0x0A4D41,
    .on_accent = 0xFFFFFF,
    .success = 0x15803D,
    .warning = 0xB45309,
    .danger = 0xC42B45,
    .accent_soft = 0xE2F1ED,
    .hero = 0x0E5B4D,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xC8E6DE,
    .coral = 0xF67C73,
    .success_soft = 0xEDF9F0,
    .warning_soft = 0xFFFBEB,
    .danger_soft = 0xFEF2F2,
    .scrim = 0xD7DFEC,
    .key_glyph_bg = 0xDFEAE6,
    .key_glyph_border = 0xB6CAC3,
    .gauge_slot = 0xD4E3DE,
    .gauge_slot_border = 0x42635B,
    .shadow_strength = 100,
    .success_border = 0x86EFAC,
    .warning_border = 0xFCD34D,
    .danger_border = 0xFCA5A5,
    .warning_text = 0xB45309,
    .night = 0x5B47D6,
    .night_soft = 0xF0EDFF,
    .night_border = 0xC4B5FD,
};

static const PtcUiPalette DARK_PALETTE = {
    .page_bg = 0x111122,
    .surface = 0x112233,
    .surface_raised = 0x223344,
    .border_decorative = 0x334455,
    .border_control = 0x778899,
    .text_primary = 0xEEEEFF,
    .text_secondary = 0xAABBCC,
    .text_disabled = 0x667788,
    .accent = 0x33DDBB,
    .focus = 0x77FFEE,
    .on_accent = 0x003322,
    .success = 0x55DD77,
    .warning = 0xFFBB33,
    .danger = 0xFF7788,
    .accent_soft = 0x113333,
    .hero = 0x113344,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xAACCFF,
    .coral = 0xFF7788,
    .success_soft = 0x113322,
    .warning_soft = 0x332211,
    .danger_soft = 0x441122,
    .scrim = 0x001122,
    .key_glyph_bg = 0x223344,
    .key_glyph_border = 0x445566,
    .gauge_slot = 0x223344,
    .gauge_slot_border = 0x446677,
    .shadow_strength = 130,
    .success_border = 0x226633,
    .warning_border = 0x885511,
    .danger_border = 0x882233,
    .warning_text = 0xFFBB33,
    .night = 0xAA99FF,
    .night_soft = 0x221144,
    .night_border = 0x5544AA,
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
    case PTC_UI_THEME_LIGHT: return ptc_ui_text(PTC_UI_T_LIGHT);
    case PTC_UI_THEME_DARK: return ptc_ui_text(PTC_UI_T_DARK);
    case PTC_UI_THEME_SYSTEM:
    default: return ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM);
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
