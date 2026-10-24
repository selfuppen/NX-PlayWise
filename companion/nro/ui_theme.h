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

typedef enum {
    PTC_UI_THEME_STYLE_SLATE = 0,    /* 石墨 (Graphite) - 默认 */
    PTC_UI_THEME_STYLE_INDIGO = 1,   /* 纸墨 (Paper & Ink) */
    PTC_UI_THEME_STYLE_TEAL = 2,     /* 雾青 (Mist Teal) */
    PTC_UI_THEME_STYLE_AMETHYST = 3, /* 暮霭 (Dusk) */
    PTC_UI_THEME_STYLE_COUNT = 4
} PtcUiThemeStyle;

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
    uint32_t accent_soft;
    uint32_t hero;
    uint32_t on_hero;
    uint32_t hero_secondary;
    uint32_t coral;
    uint32_t success_soft;
    uint32_t warning_soft;
    uint32_t danger_soft;
    uint32_t scrim;
    /* 键位徽章、额度槽的表面色，以及阴影峰值强度（百分比，暗色主题更强）。 */
    uint32_t key_glyph_bg;
    uint32_t key_glyph_border;
    uint32_t gauge_slot;
    uint32_t gauge_slot_border;
    uint32_t shadow_strength;
    /* 语义边框与状态文本强调色 */
    uint32_t success_border;
    uint32_t warning_border;
    uint32_t danger_border;
    uint32_t warning_text;
    /* 就寝/夜间作息语义色（星夜紫） */
    uint32_t night;
    uint32_t night_soft;
    uint32_t night_border;
} PtcUiPalette;

typedef struct {
    PtcUiThemePreference preference;
    PtcUiThemeStyle style;
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

/* 主题风格解析与本地化辅助。name 用于持久化兼容，keyword 用于界面英文关键词。 */
bool ptc_ui_theme_parse_style(const char *text, PtcUiThemeStyle *out);
const char *ptc_ui_theme_style_name(PtcUiThemeStyle style);
const char *ptc_ui_theme_style_keyword(PtcUiThemeStyle style);
const char *ptc_ui_theme_style_label(PtcUiThemeStyle style);
const char *ptc_ui_theme_style_desc(PtcUiThemeStyle style);

/* 全局活动风格控制 */
void ptc_ui_theme_set_style(PtcUiThemeStyle style);
PtcUiThemeStyle ptc_ui_theme_get_style(void);

/* 调色板查询：支持指定风格以及默认活动风格 */
const PtcUiPalette *ptc_ui_theme_style_palette(PtcUiResolvedTheme theme, PtcUiThemeStyle style);
const PtcUiPalette *ptc_ui_theme_palette(PtcUiResolvedTheme theme);

PtcUiThemeView ptc_ui_theme_make_view(
    PtcUiThemePreference preference,
    PtcUiSystemTheme system_theme);
PtcUiThemeView ptc_ui_theme_make_styled_view(
    PtcUiThemePreference preference,
    PtcUiThemeStyle style,
    PtcUiSystemTheme system_theme);

#endif
