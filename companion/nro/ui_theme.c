#include "ui_theme.h"
#include "../ui_language.h"

#include <string.h>

static PtcUiThemeStyle s_active_style = PTC_UI_THEME_STYLE_INDIGO;

/* 0. 沉静靛蓝 (Serene Indigo) - 默认品牌风格 */
static const PtcUiPalette LIGHT_PALETTE = {
    .page_bg = 0xF0F3FA,
    .surface = 0xFFFFFF,
    .surface_raised = 0xE8EDF7,
    .border_decorative = 0xDBE1E9,
    .border_control = 0x6E7D94,
    .text_primary = 0x172640,
    .text_secondary = 0x53627A,
    .text_disabled = 0x9199A5,
    .accent = 0x245BC4,
    .focus = 0x1247A8,
    .on_accent = 0xFFFFFF,
    .success = 0x15803D,
    .warning = 0xB45309,
    .danger = 0xC42B45,
    .accent_soft = 0xE4EDFF,
    .hero = 0x214EA3,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xDCE8FF,
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
    .accent = 0x77AAFF,
    .focus = 0xAACCFF,
    .on_accent = 0x112244,
    .success = 0x55DD77,
    .warning = 0xFFBB33,
    .danger = 0xFF7788,
    .accent_soft = 0x223355,
    .hero = 0x223388,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xCCDDFF,
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

/* 1. 清爽青绿 (Ocean Teal) */
static const PtcUiPalette LIGHT_TEAL_PALETTE = {
    .page_bg = 0xF0F3FA,
    .surface = 0xFFFFFF,
    .surface_raised = 0xE8EDF7,
    .border_decorative = 0xDBE1E9,
    .border_control = 0x6E7D94,
    .text_primary = 0x172640,
    .text_secondary = 0x53627A,
    .text_disabled = 0x9199A5,
    .accent = 0x0D7A70,
    .focus = 0x08544D,
    .on_accent = 0xFFFFFF,
    .success = 0x15803D,
    .warning = 0xB45309,
    .danger = 0xC42B45,
    .accent_soft = 0xE0F4F2,
    .hero = 0x0B665E,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xD0EFEA,
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

static const PtcUiPalette DARK_TEAL_PALETTE = {
    .page_bg = 0x111122,
    .surface = 0x112233,
    .surface_raised = 0x223344,
    .border_decorative = 0x334455,
    .border_control = 0x778899,
    .text_primary = 0xEEEEFF,
    .text_secondary = 0xAABBCC,
    .text_disabled = 0x667788,
    .accent = 0x44DDCC,
    .focus = 0x77FFEE,
    .on_accent = 0x002222,
    .success = 0x55DD77,
    .warning = 0xFFBB33,
    .danger = 0xFF7788,
    .accent_soft = 0x113333,
    .hero = 0x115555,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xBBDDEE,
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

/* 2. 暮紫星夜 (Night Amethyst) */
static const PtcUiPalette LIGHT_AMETHYST_PALETTE = {
    .page_bg = 0xF0F3FA,
    .surface = 0xFFFFFF,
    .surface_raised = 0xE8EDF7,
    .border_decorative = 0xDBE1E9,
    .border_control = 0x6E7D94,
    .text_primary = 0x172640,
    .text_secondary = 0x53627A,
    .text_disabled = 0x9199A5,
    .accent = 0x6340B8,
    .focus = 0x482B96,
    .on_accent = 0xFFFFFF,
    .success = 0x15803D,
    .warning = 0xB45309,
    .danger = 0xC42B45,
    .accent_soft = 0xF1ECFC,
    .hero = 0x5332A0,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xE6DCFA,
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

static const PtcUiPalette DARK_AMETHYST_PALETTE = {
    .page_bg = 0x111122,
    .surface = 0x112233,
    .surface_raised = 0x223344,
    .border_decorative = 0x334455,
    .border_control = 0x778899,
    .text_primary = 0xEEEEFF,
    .text_secondary = 0xAABBCC,
    .text_disabled = 0x667788,
    .accent = 0xBB99FF,
    .focus = 0xDDCCFF,
    .on_accent = 0x221144,
    .success = 0x55DD77,
    .warning = 0xFFBB33,
    .danger = 0xFF7788,
    .accent_soft = 0x332255,
    .hero = 0x442277,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xEEDDFF,
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

/* 3. 极简冷灰 (Slate Neutral) */
static const PtcUiPalette LIGHT_SLATE_PALETTE = {
    .page_bg = 0xF0F3FA,
    .surface = 0xFFFFFF,
    .surface_raised = 0xE8EDF7,
    .border_decorative = 0xDBE1E9,
    .border_control = 0x6E7D94,
    .text_primary = 0x172640,
    .text_secondary = 0x53627A,
    .text_disabled = 0x9199A5,
    .accent = 0x3B5270,
    .focus = 0x25364D,
    .on_accent = 0xFFFFFF,
    .success = 0x15803D,
    .warning = 0xB45309,
    .danger = 0xC42B45,
    .accent_soft = 0xEAF0F6,
    .hero = 0x30435C,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xDCE5EF,
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

static const PtcUiPalette DARK_SLATE_PALETTE = {
    .page_bg = 0x111122,
    .surface = 0x112233,
    .surface_raised = 0x223344,
    .border_decorative = 0x334455,
    .border_control = 0x778899,
    .text_primary = 0xEEEEFF,
    .text_secondary = 0xAABBCC,
    .text_disabled = 0x667788,
    .accent = 0x99BBDD,
    .focus = 0xCCDDEE,
    .on_accent = 0x112233,
    .success = 0x55DD77,
    .warning = 0xFFBB33,
    .danger = 0xFF7788,
    .accent_soft = 0x223344,
    .hero = 0x224466,
    .on_hero = 0xFFFFFF,
    .hero_secondary = 0xCCDDEE,
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
