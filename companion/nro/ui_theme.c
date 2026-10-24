#include "ui_theme.h"
#include "../ui_language.h"

#include <string.h>

static PtcUiThemeStyle s_active_style = PTC_UI_THEME_STYLE_SLATE;

/* 1. 纸墨 (Paper & Ink) */
static const PtcUiPalette LIGHT_PALETTE = {
    .page_bg = 0xF5F1E8,
    .surface = 0xFFFDF8,
    .surface_raised = 0xEDE7DA,
    .border_decorative = 0xE2DACB,
    .border_control = 0x8A8172,
    .text_primary = 0x211E19,
    .text_secondary = 0x5D564B,
    .text_disabled = 0x8A8172,
    .accent = 0x2C4A6E,
    .focus = 0x1B3350,
    .on_accent = 0xFFFFFF,
    .accent_soft = 0xE7EDF4,
    .hero = 0x233D5B,
    .on_hero = 0xFBF9F4,
    .hero_secondary = 0xC3D1E0,
    .success = 0x3E7A4E,
    .success_soft = 0xEBF3E9,
    .success_border = 0xA8CDA4,
    .warning = 0x96601A,
    .warning_soft = 0xFAF1DF,
    .warning_border = 0xE3C07E,
    .warning_text = 0x7F4F10,
    .danger = 0xAE3E38,
    .danger_soft = 0xFAEBE7,
    .danger_border = 0xE3A79F,
    .coral = 0xC46A55,
    .night = 0x5D4A8A,
    .night_soft = 0xF0EBF7,
    .night_border = 0xC5B7DE,
    .scrim = 0xE6DFD1,
    .key_glyph_bg = 0xEAE3D5,
    .key_glyph_border = 0xC3B7A3,
    .gauge_slot = 0xE8E1D2,
    .gauge_slot_border = 0x6E6151,
    .shadow_strength = 95,
};

static const PtcUiPalette DARK_PALETTE = {
    .page_bg = 0x111111,
    .surface = 0x221111,
    .surface_raised = 0x332222,
    .border_decorative = 0x443333,
    .border_control = 0x887777,
    .text_primary = 0xFFEEDD,
    .text_secondary = 0xCCBBAA,
    .text_disabled = 0x776666,
    .accent = 0x99BBDD,
    .focus = 0xBBDDFF,
    .on_accent = 0x112233,
    .accent_soft = 0x223344,
    .hero = 0x223344,
    .on_hero = 0xFFEEDD,
    .hero_secondary = 0xAACCDD,
    .success = 0x88CC99,
    .success_soft = 0x113322,
    .success_border = 0x336644,
    .warning = 0xEEBB66,
    .warning_soft = 0x332211,
    .warning_border = 0x775522,
    .warning_text = 0xEEBB66,
    .danger = 0xEE9988,
    .danger_soft = 0x441111,
    .danger_border = 0x883322,
    .coral = 0xEE9988,
    .night = 0xCCAAEE,
    .night_soft = 0x221133,
    .night_border = 0x664488,
    .scrim = 0x110000,
    .key_glyph_bg = 0x332222,
    .key_glyph_border = 0x554444,
    .gauge_slot = 0x332222,
    .gauge_slot_border = 0x665555,
    .shadow_strength = 140,
};

/* 2. 雾青 (Mist Teal) */
static const PtcUiPalette LIGHT_TEAL_PALETTE = {
    .page_bg = 0xF2F5F2,
    .surface = 0xFBFCFA,
    .surface_raised = 0xE9EFEA,
    .border_decorative = 0xDCE4DE,
    .border_control = 0x768A81,
    .text_primary = 0x182420,
    .text_secondary = 0x52645C,
    .text_disabled = 0x7F8C85,
    .accent = 0x1F6B5C,
    .focus = 0x124F44,
    .on_accent = 0xFFFFFF,
    .accent_soft = 0xE2F0EB,
    .hero = 0x1B4B43,
    .on_hero = 0xF7FCFA,
    .hero_secondary = 0xBFDCD3,
    .success = 0x2E7D4F,
    .success_soft = 0xEAF5EE,
    .success_border = 0x9BD3AE,
    .warning = 0x9A6212,
    .warning_soft = 0xFBF3E3,
    .warning_border = 0xE7C27B,
    .warning_text = 0x85510B,
    .danger = 0xB33B45,
    .danger_soft = 0xFAECEC,
    .danger_border = 0xE4A3A8,
    .coral = 0xC96A5E,
    .night = 0x5A4E9E,
    .night_soft = 0xEFEBF8,
    .night_border = 0xBFB4E0,
    .scrim = 0xDCE5E0,
    .key_glyph_bg = 0xE6EDE8,
    .key_glyph_border = 0xB9C8C1,
    .gauge_slot = 0xDCE7E1,
    .gauge_slot_border = 0x4A6B60,
    .shadow_strength = 90,
};

static const PtcUiPalette DARK_TEAL_PALETTE = {
    .page_bg = 0x111111,
    .surface = 0x112222,
    .surface_raised = 0x223333,
    .border_decorative = 0x334444,
    .border_control = 0x778888,
    .text_primary = 0xEEFFFF,
    .text_secondary = 0xAACCCC,
    .text_disabled = 0x667777,
    .accent = 0x77CCBB,
    .focus = 0xAADDDD,
    .on_accent = 0x002222,
    .accent_soft = 0x113333,
    .hero = 0x114433,
    .on_hero = 0xEEFFFF,
    .hero_secondary = 0xAACCCC,
    .success = 0x77CC99,
    .success_soft = 0x113322,
    .success_border = 0x336644,
    .warning = 0xEEBB66,
    .warning_soft = 0x332211,
    .warning_border = 0x775522,
    .warning_text = 0xEEBB66,
    .danger = 0xEE8899,
    .danger_soft = 0x441122,
    .danger_border = 0x882233,
    .coral = 0xEE8899,
    .night = 0xBBAAFF,
    .night_soft = 0x221144,
    .night_border = 0x554488,
    .scrim = 0x001111,
    .key_glyph_bg = 0x223333,
    .key_glyph_border = 0x445555,
    .gauge_slot = 0x223333,
    .gauge_slot_border = 0x446655,
    .shadow_strength = 140,
};

/* 3. 暮霭 (Dusk) */
static const PtcUiPalette LIGHT_AMETHYST_PALETTE = {
    .page_bg = 0xF5F2F5,
    .surface = 0xFDFBFD,
    .surface_raised = 0xEDE7ED,
    .border_decorative = 0xE1D9E1,
    .border_control = 0x877C88,
    .text_primary = 0x221D24,
    .text_secondary = 0x5D5360,
    .text_disabled = 0x8B7F8C,
    .accent = 0x7A4670,
    .focus = 0x5A2F52,
    .on_accent = 0xFFFFFF,
    .accent_soft = 0xF3E8F1,
    .hero = 0x4E2E48,
    .on_hero = 0xFCF7FB,
    .hero_secondary = 0xD9C0D4,
    .success = 0x3F7A55,
    .success_soft = 0xEAF4ED,
    .success_border = 0xA0CCA9,
    .warning = 0x96601A,
    .warning_soft = 0xFAF1DF,
    .warning_border = 0xE3C07E,
    .warning_text = 0x7F4F10,
    .danger = 0xB03E46,
    .danger_soft = 0xFAEAEC,
    .danger_border = 0xE3A3A8,
    .coral = 0xC46A63,
    .night = 0x4653A8,
    .night_soft = 0xEAEBF8,
    .night_border = 0xB4BAE3,
    .scrim = 0xE7DEE5,
    .key_glyph_bg = 0xECE5EB,
    .key_glyph_border = 0xC4B8C2,
    .gauge_slot = 0xEAE2E9,
    .gauge_slot_border = 0x6F606D,
    .shadow_strength = 95,
};

static const PtcUiPalette DARK_AMETHYST_PALETTE = {
    .page_bg = 0x111111,
    .surface = 0x221122,
    .surface_raised = 0x332233,
    .border_decorative = 0x443344,
    .border_control = 0x887788,
    .text_primary = 0xFFEEFF,
    .text_secondary = 0xCCBBCC,
    .text_disabled = 0x776677,
    .accent = 0xDD99CC,
    .focus = 0xEEBBEE,
    .on_accent = 0x331122,
    .accent_soft = 0x332233,
    .hero = 0x331133,
    .on_hero = 0xFFEEFF,
    .hero_secondary = 0xCCBBCC,
    .success = 0x77CC99,
    .success_soft = 0x113322,
    .success_border = 0x336644,
    .warning = 0xEEBB66,
    .warning_soft = 0x332211,
    .warning_border = 0x775522,
    .warning_text = 0xEEBB66,
    .danger = 0xEE8899,
    .danger_soft = 0x441122,
    .danger_border = 0x882233,
    .coral = 0xEE8899,
    .night = 0x99AAFF,
    .night_soft = 0x222244,
    .night_border = 0x444488,
    .scrim = 0x110011,
    .key_glyph_bg = 0x332233,
    .key_glyph_border = 0x554455,
    .gauge_slot = 0x332233,
    .gauge_slot_border = 0x665566,
    .shadow_strength = 140,
};

/* 0. 石墨 (Graphite) - 默认 */
static const PtcUiPalette LIGHT_SLATE_PALETTE = {
    .page_bg = 0xF3F4F6,
    .surface = 0xFCFCFD,
    .surface_raised = 0xE9EBEF,
    .border_decorative = 0xDDE0E5,
    .border_control = 0x7D8593,
    .text_primary = 0x171A20,
    .text_secondary = 0x545B68,
    .text_disabled = 0x7E8592,
    .accent = 0x2F5FD0,
    .focus = 0x1C46AC,
    .on_accent = 0xFFFFFF,
    .accent_soft = 0xE6ECFB,
    .hero = 0x1E3566,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xC3D2EE,
    .success = 0x2C7A4B,
    .success_soft = 0xEAF5EE,
    .success_border = 0x9AD2AE,
    .warning = 0x9A6114,
    .warning_soft = 0xFBF2E1,
    .warning_border = 0xE4C17B,
    .warning_text = 0x83500C,
    .danger = 0xB53A45,
    .danger_soft = 0xFAEBEC,
    .danger_border = 0xE2A2A8,
    .coral = 0xC9665C,
    .night = 0x5B4CB0,
    .night_soft = 0xEDEAF9,
    .night_border = 0xBDB4E8,
    .scrim = 0xDEE1E7,
    .key_glyph_bg = 0xE7EAEE,
    .key_glyph_border = 0xBCC2CB,
    .gauge_slot = 0xE3E7EC,
    .gauge_slot_border = 0x4C5666,
    .shadow_strength = 95,
};

static const PtcUiPalette DARK_SLATE_PALETTE = {
    .page_bg = 0x111111,
    .surface = 0x222222,
    .surface_raised = 0x333333,
    .border_decorative = 0x444444,
    .border_control = 0x888888,
    .text_primary = 0xEEEEEE,
    .text_secondary = 0xBBBBBB,
    .text_disabled = 0x777777,
    .accent = 0x88AAEE,
    .focus = 0xBBDDFF,
    .on_accent = 0x112233,
    .accent_soft = 0x223344,
    .hero = 0x223355,
    .on_hero = 0xEEEEFF,
    .hero_secondary = 0xAABBCC,
    .success = 0x77CC99,
    .success_soft = 0x113322,
    .success_border = 0x336644,
    .warning = 0xEEBB66,
    .warning_soft = 0x332211,
    .warning_border = 0x775522,
    .warning_text = 0xEEBB66,
    .danger = 0xEE8899,
    .danger_soft = 0x441122,
    .danger_border = 0x882233,
    .coral = 0xEE8899,
    .night = 0xBBAAFF,
    .night_soft = 0x221144,
    .night_border = 0x554488,
    .scrim = 0x000000,
    .key_glyph_bg = 0x333333,
    .key_glyph_border = 0x555555,
    .gauge_slot = 0x333333,
    .gauge_slot_border = 0x666666,
    .shadow_strength = 140,
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

bool ptc_ui_theme_parse_style(const char *text, PtcUiThemeStyle *out)
{
    if (!text || !out) return false;
    if (strcmp(text, "indigo") == 0) *out = PTC_UI_THEME_STYLE_INDIGO;
    else if (strcmp(text, "teal") == 0) *out = PTC_UI_THEME_STYLE_TEAL;
    else if (strcmp(text, "amethyst") == 0) *out = PTC_UI_THEME_STYLE_AMETHYST;
    else if (strcmp(text, "slate") == 0) *out = PTC_UI_THEME_STYLE_SLATE;
    else return false;
    return true;
}

const char *ptc_ui_theme_style_name(PtcUiThemeStyle style)
{
    switch (style) {
    case PTC_UI_THEME_STYLE_TEAL: return "teal";
    case PTC_UI_THEME_STYLE_AMETHYST: return "amethyst";
    case PTC_UI_THEME_STYLE_SLATE: return "slate";
    case PTC_UI_THEME_STYLE_INDIGO:
    default: return "indigo";
    }
}

const char *ptc_ui_theme_style_keyword(PtcUiThemeStyle style)
{
    switch (style) {
    case PTC_UI_THEME_STYLE_TEAL: return "mist";
    case PTC_UI_THEME_STYLE_AMETHYST: return "dusk";
    case PTC_UI_THEME_STYLE_SLATE: return "graphite";
    case PTC_UI_THEME_STYLE_INDIGO:
    default: return "paper";
    }
}

const char *ptc_ui_theme_style_label(PtcUiThemeStyle style)
{
    switch (style) {
    case PTC_UI_THEME_STYLE_TEAL: return ptc_ui_text(PTC_UI_T_THEME_STYLE_TEAL);
    case PTC_UI_THEME_STYLE_AMETHYST: return ptc_ui_text(PTC_UI_T_THEME_STYLE_AMETHYST);
    case PTC_UI_THEME_STYLE_SLATE: return ptc_ui_text(PTC_UI_T_THEME_STYLE_SLATE);
    case PTC_UI_THEME_STYLE_INDIGO:
    default: return ptc_ui_text(PTC_UI_T_THEME_STYLE_INDIGO);
    }
}

const char *ptc_ui_theme_style_desc(PtcUiThemeStyle style)
{
    switch (style) {
    case PTC_UI_THEME_STYLE_TEAL: return ptc_ui_text(PTC_UI_T_THEME_STYLE_TEAL_DESC);
    case PTC_UI_THEME_STYLE_AMETHYST: return ptc_ui_text(PTC_UI_T_THEME_STYLE_AMETHYST_DESC);
    case PTC_UI_THEME_STYLE_SLATE: return ptc_ui_text(PTC_UI_T_THEME_STYLE_SLATE_DESC);
    case PTC_UI_THEME_STYLE_INDIGO:
    default: return ptc_ui_text(PTC_UI_T_THEME_STYLE_INDIGO_DESC);
    }
}

void ptc_ui_theme_set_style(PtcUiThemeStyle style)
{
    if (style >= 0 && style < PTC_UI_THEME_STYLE_COUNT) {
        s_active_style = style;
    }
}

PtcUiThemeStyle ptc_ui_theme_get_style(void)
{
    return s_active_style;
}

const PtcUiPalette *ptc_ui_theme_style_palette(PtcUiResolvedTheme theme, PtcUiThemeStyle style)
{
    if (theme == PTC_UI_RESOLVED_DARK) {
        switch (style) {
        case PTC_UI_THEME_STYLE_TEAL: return &DARK_TEAL_PALETTE;
        case PTC_UI_THEME_STYLE_AMETHYST: return &DARK_AMETHYST_PALETTE;
        case PTC_UI_THEME_STYLE_SLATE: return &DARK_SLATE_PALETTE;
        case PTC_UI_THEME_STYLE_INDIGO:
        default: return &DARK_PALETTE;
        }
    } else {
        switch (style) {
        case PTC_UI_THEME_STYLE_TEAL: return &LIGHT_TEAL_PALETTE;
        case PTC_UI_THEME_STYLE_AMETHYST: return &LIGHT_AMETHYST_PALETTE;
        case PTC_UI_THEME_STYLE_SLATE: return &LIGHT_SLATE_PALETTE;
        case PTC_UI_THEME_STYLE_INDIGO:
        default: return &LIGHT_PALETTE;
        }
    }
}

const PtcUiPalette *ptc_ui_theme_palette(PtcUiResolvedTheme theme)
{
    return ptc_ui_theme_style_palette(theme, s_active_style);
}

PtcUiThemeView ptc_ui_theme_make_styled_view(
    PtcUiThemePreference preference,
    PtcUiThemeStyle style,
    PtcUiSystemTheme system_theme)
{
    PtcUiThemeView view;
    view.preference = preference;
    view.style = style;
    view.resolved = ptc_ui_theme_resolve(preference, system_theme);
    view.palette = ptc_ui_theme_style_palette(view.resolved, style);
    view.system_theme_available = system_theme != PTC_UI_SYSTEM_THEME_UNAVAILABLE;
    return view;
}

PtcUiThemeView ptc_ui_theme_make_view(
    PtcUiThemePreference preference,
    PtcUiSystemTheme system_theme)
{
    return ptc_ui_theme_make_styled_view(preference, s_active_style, system_theme);
}
