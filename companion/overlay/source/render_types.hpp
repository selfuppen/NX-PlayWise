// Shared production Overlay colors, text and view types.
#pragma once
#include <ctime>
#include <cmath>

#ifdef __cplusplus
extern "C" {
#endif
#include "companion/nro/ui_theme.h"
#ifdef __cplusplus
}
#endif

struct OverlayPalette {
    tsl::Color panel_bg;
    tsl::Color card_bg;
    tsl::Color card_raised;
    tsl::Color border_decorative;
    tsl::Color border_control;
    tsl::Color text_primary;
    tsl::Color text_secondary;
    tsl::Color text_disabled;
    tsl::Color accent;
    tsl::Color accent_soft;
    tsl::Color success;
    tsl::Color success_soft;
    tsl::Color success_border;
    tsl::Color warning;
    tsl::Color warning_soft;
    tsl::Color warning_border;
    tsl::Color danger;
    tsl::Color danger_soft;
    tsl::Color danger_border;
    tsl::Color night;
    tsl::Color night_soft;
    tsl::Color night_border;
};

// The overlay paints its own full-screen panel with panel_bg instead of relying on
// libtesla's ColorFrameBackground (0xD). 0xE (~93%) keeps a hint of the game visible
// while halving the bright-scene bleed that made text on bare page areas unreadable.
// Do not draw an extra page-level panel_bg rect: libtesla adds alpha, so 0xE over the
// 0xE fill would saturate to 0xF and silently become fully opaque.
// 0: 纸墨 (Paper & Ink) - 默认暗色基线
constexpr OverlayPalette DARK_OVERLAY_PALETTE = {
    .panel_bg = { 0x0, 0x0, 0x0, 0xE },
    .card_bg = { 0x2, 0x1, 0x1, 0xF },
    .card_raised = { 0x3, 0x2, 0x2, 0xF },
    .border_decorative = { 0x4, 0x3, 0x3, 0xF },
    .border_control = { 0x8, 0x7, 0x7, 0xF },
    .text_primary = { 0xF, 0xE, 0xD, 0xF },
    .text_secondary = { 0xC, 0xB, 0xA, 0xF },
    .text_disabled = { 0x7, 0x6, 0x6, 0xF },
    .accent = { 0x9, 0xB, 0xD, 0xF },
    .accent_soft = { 0x2, 0x3, 0x4, 0xF },
    .success = { 0x8, 0xC, 0x9, 0xF },
    .success_soft = { 0x1, 0x3, 0x2, 0xF },
    .success_border = { 0x3, 0x6, 0x4, 0xF },
    .warning = { 0xE, 0xB, 0x6, 0xF },
    .warning_soft = { 0x3, 0x2, 0x1, 0xF },
    .warning_border = { 0x7, 0x5, 0x2, 0xF },
    .danger = { 0xE, 0x9, 0x8, 0xF },
    .danger_soft = { 0x4, 0x1, 0x1, 0xF },
    .danger_border = { 0x8, 0x3, 0x2, 0xF },
    .night = { 0xC, 0xA, 0xE, 0xF },
    .night_soft = { 0x2, 0x1, 0x3, 0xF },
    .night_border = { 0x6, 0x4, 0x8, 0xF },
};

// 1: 雾青 (Mist Teal)
constexpr OverlayPalette DARK_OVERLAY_TEAL_PALETTE = {
    .panel_bg = { 0x0, 0x0, 0x0, 0xE },
    .card_bg = { 0x1, 0x2, 0x2, 0xF },
    .card_raised = { 0x2, 0x3, 0x3, 0xF },
    .border_decorative = { 0x3, 0x4, 0x4, 0xF },
    .border_control = { 0x7, 0x8, 0x8, 0xF },
    .text_primary = { 0xE, 0xF, 0xF, 0xF },
    .text_secondary = { 0xA, 0xC, 0xC, 0xF },
    .text_disabled = { 0x6, 0x7, 0x7, 0xF },
    .accent = { 0x7, 0xC, 0xB, 0xF },
    .accent_soft = { 0x1, 0x3, 0x3, 0xF },
    .success = { 0x7, 0xC, 0x9, 0xF },
    .success_soft = { 0x1, 0x3, 0x2, 0xF },
    .success_border = { 0x3, 0x6, 0x4, 0xF },
    .warning = { 0xE, 0xB, 0x6, 0xF },
    .warning_soft = { 0x3, 0x2, 0x1, 0xF },
    .warning_border = { 0x7, 0x5, 0x2, 0xF },
    .danger = { 0xE, 0x8, 0x9, 0xF },
    .danger_soft = { 0x4, 0x1, 0x2, 0xF },
    .danger_border = { 0x8, 0x2, 0x3, 0xF },
    .night = { 0xB, 0xA, 0xF, 0xF },
    .night_soft = { 0x2, 0x1, 0x4, 0xF },
    .night_border = { 0x5, 0x4, 0x8, 0xF },
};

// 2: 暮霭 (Dusk)
constexpr OverlayPalette DARK_OVERLAY_AMETHYST_PALETTE = {
    .panel_bg = { 0x0, 0x0, 0x0, 0xE },
    .card_bg = { 0x2, 0x1, 0x2, 0xF },
    .card_raised = { 0x3, 0x2, 0x3, 0xF },
    .border_decorative = { 0x4, 0x3, 0x4, 0xF },
    .border_control = { 0x8, 0x7, 0x8, 0xF },
    .text_primary = { 0xF, 0xE, 0xF, 0xF },
    .text_secondary = { 0xC, 0xB, 0xC, 0xF },
    .text_disabled = { 0x7, 0x6, 0x7, 0xF },
    .accent = { 0xD, 0x9, 0xC, 0xF },
    .accent_soft = { 0x3, 0x2, 0x3, 0xF },
    .success = { 0x7, 0xC, 0x9, 0xF },
    .success_soft = { 0x1, 0x3, 0x2, 0xF },
    .success_border = { 0x3, 0x6, 0x4, 0xF },
    .warning = { 0xE, 0xB, 0x6, 0xF },
    .warning_soft = { 0x3, 0x2, 0x1, 0xF },
    .warning_border = { 0x7, 0x5, 0x2, 0xF },
    .danger = { 0xE, 0x8, 0x9, 0xF },
    .danger_soft = { 0x4, 0x1, 0x2, 0xF },
    .danger_border = { 0x8, 0x2, 0x3, 0xF },
    .night = { 0x9, 0xA, 0xF, 0xF },
    .night_soft = { 0x2, 0x2, 0x4, 0xF },
    .night_border = { 0x4, 0x4, 0x8, 0xF },
};

// 3: 石墨 (Graphite)
constexpr OverlayPalette DARK_OVERLAY_SLATE_PALETTE = {
    .panel_bg = { 0x0, 0x0, 0x0, 0xE },
    .card_bg = { 0x2, 0x2, 0x2, 0xF },
    .card_raised = { 0x3, 0x3, 0x3, 0xF },
    .border_decorative = { 0x4, 0x4, 0x4, 0xF },
    .border_control = { 0x8, 0x8, 0x8, 0xF },
    .text_primary = { 0xE, 0xE, 0xE, 0xF },
    .text_secondary = { 0xB, 0xB, 0xB, 0xF },
    .text_disabled = { 0x7, 0x7, 0x7, 0xF },
    .accent = { 0x8, 0xA, 0xE, 0xF },
    .accent_soft = { 0x2, 0x3, 0x4, 0xF },
    .success = { 0x7, 0xC, 0x9, 0xF },
    .success_soft = { 0x1, 0x3, 0x2, 0xF },
    .success_border = { 0x3, 0x6, 0x4, 0xF },
    .warning = { 0xE, 0xB, 0x6, 0xF },
    .warning_soft = { 0x3, 0x2, 0x1, 0xF },
    .warning_border = { 0x7, 0x5, 0x2, 0xF },
    .danger = { 0xE, 0x8, 0x9, 0xF },
    .danger_soft = { 0x4, 0x1, 0x2, 0xF },
    .danger_border = { 0x8, 0x2, 0x3, 0xF },
    .night = { 0xB, 0xA, 0xF, 0xF },
    .night_soft = { 0x2, 0x1, 0x4, 0xF },
    .night_border = { 0x5, 0x4, 0x8, 0xF },
};

// Light theme palette fallback (Tesla overlay strictly runs in dark mode)
constexpr OverlayPalette LIGHT_OVERLAY_PALETTE = DARK_OVERLAY_PALETTE;

static inline PtcUiThemeStyle s_overlay_theme_style = PTC_UI_THEME_STYLE_INDIGO;

inline void set_overlay_theme_style(PtcUiThemeStyle style)
{
    if (style >= 0 && style < PTC_UI_THEME_STYLE_COUNT) {
        s_overlay_theme_style = style;
    }
}

inline PtcUiThemeStyle get_overlay_theme_style()
{
    return s_overlay_theme_style;
}

enum class PtcOverlayLanguageSetting {
    Auto = 0,
    Simplified = 1,
    Traditional = 2,
    English = 3
};

[[maybe_unused]] static inline PtcOverlayLanguageSetting s_overlay_lang_setting = PtcOverlayLanguageSetting::Auto;
[[maybe_unused]] static inline PtcUiLanguagePreference s_nro_language_preference = PTC_UI_LANGUAGE_SYSTEM;
[[maybe_unused]] static inline PtcUiSystemLanguage s_system_language = PTC_UI_SYSTEM_LANGUAGE_UNKNOWN;

inline void set_overlay_language_setting(PtcOverlayLanguageSetting setting)
{
    s_overlay_lang_setting = setting;
}

inline PtcOverlayLanguageSetting get_overlay_language_setting()
{
    return s_overlay_lang_setting;
}

inline const char *overlay_language_setting_name(PtcOverlayLanguageSetting setting)
{
    switch (setting) {
    case PtcOverlayLanguageSetting::Simplified: return "zh-Hans";
    case PtcOverlayLanguageSetting::Traditional: return "zh-Hant";
    case PtcOverlayLanguageSetting::English: return "en";
    case PtcOverlayLanguageSetting::Auto:
    default: return "auto";
    }
}

inline bool parse_overlay_language_setting(const char *val, PtcOverlayLanguageSetting *out)
{
    if (!val || !out) return false;
    if (std::strcmp(val, "zh-Hans") == 0) {
        *out = PtcOverlayLanguageSetting::Simplified;
        return true;
    }
    if (std::strcmp(val, "zh-Hant") == 0) {
        *out = PtcOverlayLanguageSetting::Traditional;
        return true;
    }
    if (std::strcmp(val, "en") == 0) {
        *out = PtcOverlayLanguageSetting::English;
        return true;
    }
    if (std::strcmp(val, "auto") == 0 || std::strcmp(val, "follow_nro") == 0) {
        *out = PtcOverlayLanguageSetting::Auto;
        return true;
    }
    return false;
}

inline PtcUiLanguagePreference resolve_overlay_effective_language(
    PtcOverlayLanguageSetting setting,
    PtcUiLanguagePreference nro_pref,
    PtcUiSystemLanguage sys_lang)
{
    switch (setting) {
    case PtcOverlayLanguageSetting::Simplified:
        return PTC_UI_LANGUAGE_SIMPLIFIED;
    case PtcOverlayLanguageSetting::Traditional:
        return PTC_UI_LANGUAGE_TRADITIONAL;
    case PtcOverlayLanguageSetting::English:
        return PTC_UI_LANGUAGE_ENGLISH;
    case PtcOverlayLanguageSetting::Auto:
    default:
        return ptc_ui_language_resolve(nro_pref, sys_lang);
    }
}

inline void draw_overlay_footer_hints(tsl::gfx::Renderer *renderer, tsl::Color color)
{
    if (!renderer) return;
    const char *back_lbl = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_BACK);
    const char *ok_lbl = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_OK);

    // Language lives in the child-overview header, so the host footer only
    // advertises the two keys that always apply.
    s32 cur_x = 24;
    renderer->drawString(back_lbl, false, cur_x, 693, PTC_OVERLAY_FONT_BTN, color);
    const auto back_sz = renderer->drawString(back_lbl, false, 0, 0,
        PTC_OVERLAY_FONT_BTN, tsl::style::color::ColorTransparent);
    cur_x += static_cast<s32>(back_sz.first) + 24;

    renderer->drawString(ok_lbl, false, cur_x, 693, PTC_OVERLAY_FONT_BTN, color);
}

inline void get_overlay_current_time(char *out, size_t size)
{
    if (!out || size == 0) return;
    out[0] = '\0';
#if defined(__SWITCH__)
    u64 ts = 0;
    TimeCalendarTime cal{};
    TimeCalendarAdditionalInfo info{};
    if (R_SUCCEEDED(timeGetCurrentTime(TimeType_Default, &ts)) &&
        R_SUCCEEDED(timeToCalendarTimeWithMyRule(ts, &cal, &info))) {
        std::snprintf(out, size, "%02u:%02u", (unsigned)cal.hour, (unsigned)cal.minute);
        return;
    }
#else
    // Switch libc may report uptime when the system clock is unavailable.
    // Only host previews may fall back to the host wall clock.
    const std::time_t now = std::time(nullptr);
    struct tm tm_buf{};
#if defined(_WIN32)
    if (localtime_s(&tm_buf, &now) == 0) {
#else
    if (localtime_r(&now, &tm_buf) != nullptr) {
#endif
        std::snprintf(out, size, "%02d:%02d", tm_buf.tm_hour, tm_buf.tm_min);
        return;
    }
#endif
    std::snprintf(out, size, "--:--");
}

inline const OverlayPalette &get_overlay_palette(PtcUiResolvedTheme theme = PTC_UI_RESOLVED_DARK)
{
    (void)theme;
    switch (s_overlay_theme_style) {
    case PTC_UI_THEME_STYLE_TEAL: return DARK_OVERLAY_TEAL_PALETTE;
    case PTC_UI_THEME_STYLE_AMETHYST: return DARK_OVERLAY_AMETHYST_PALETTE;
    case PTC_UI_THEME_STYLE_SLATE: return DARK_OVERLAY_SLATE_PALETTE;
    case PTC_UI_THEME_STYLE_INDIGO:
    default: return DARK_OVERLAY_PALETTE;
    }
}

// Backward-compatible named colors aligned with the dark design baseline.
constexpr tsl::Color PANEL_COLOR = DARK_OVERLAY_PALETTE.panel_bg;
constexpr tsl::Color CARD_COLOR = DARK_OVERLAY_PALETTE.card_bg;
constexpr tsl::Color KEY_COLOR = DARK_OVERLAY_PALETTE.card_raised;
constexpr tsl::Color FOCUS_BG = DARK_OVERLAY_PALETTE.accent_soft;
constexpr tsl::Color FOCUS_BORDER = DARK_OVERLAY_PALETTE.accent;
constexpr tsl::Color TEXT_COLOR = DARK_OVERLAY_PALETTE.text_primary;
constexpr tsl::Color DARK_TEXT_COLOR{ 0x1, 0x2, 0x4, 0xF };
constexpr tsl::Color MUTED_COLOR = DARK_OVERLAY_PALETTE.text_secondary;
constexpr tsl::Color DISABLED_COLOR = DARK_OVERLAY_PALETTE.card_raised;
constexpr tsl::Color SUCCESS_COLOR = DARK_OVERLAY_PALETTE.success;
constexpr tsl::Color ERROR_COLOR = DARK_OVERLAY_PALETTE.danger;
constexpr tsl::Color WAITING_COLOR = DARK_OVERLAY_PALETTE.warning;
constexpr tsl::Color DANGER_BG = DARK_OVERLAY_PALETTE.danger_soft;
constexpr tsl::Color WARNING_BG = DARK_OVERLAY_PALETTE.warning_soft;
constexpr tsl::Color BACKSPACE_BORDER = DARK_OVERLAY_PALETTE.warning_border;
constexpr tsl::Color CLEAR_BORDER = DARK_OVERLAY_PALETTE.danger_border;
constexpr tsl::Color NIGHT_COLOR = DARK_OVERLAY_PALETTE.night;
constexpr tsl::Color NIGHT_BG = DARK_OVERLAY_PALETTE.night_soft;

// Zero-allocation UTF-8 Emoji filter: removes 4-byte emojis and unsupported symbols
// preventing missing glyph artifacts and mojibake in libtesla's shared font renderer.
static inline void ptc_overlay_sanitize_utf8(const char *src, char *dst, size_t dst_size)
{
    if (!src || !dst || dst_size == 0) return;
    size_t d = 0;
    const unsigned char *s = reinterpret_cast<const unsigned char *>(src);
    while (*s && d + 4 < dst_size) {
        if (*s < 0x80) {
            dst[d++] = *s++;
        } else if ((*s & 0xE0) == 0xC0) {
            if (s[1] && (s[1] & 0xC0) == 0x80) {
                dst[d++] = *s++;
                dst[d++] = *s++;
            } else {
                s++;
            }
        } else if ((*s & 0xF0) == 0xE0) {
            if (s[1] && (s[1] & 0xC0) == 0x80 && s[2] && (s[2] & 0xC0) == 0x80) {
                u32 cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
                if ((cp >= 0x2300 && cp <= 0x23FF) || (cp >= 0x2600 && cp <= 0x27BF)) {
                    s += 3;
                } else {
                    dst[d++] = *s++;
                    dst[d++] = *s++;
                    dst[d++] = *s++;
                }
            } else {
                s++;
            }
        } else if ((*s & 0xF8) == 0xF0) {
            if (s[1] && (s[1] & 0xC0) == 0x80 && s[2] && (s[2] & 0xC0) == 0x80 && s[3] && (s[3] & 0xC0) == 0x80) {
                s += 4;
            } else {
                s++;
            }
        } else {
            s++;
        }
    }
    dst[d] = '\0';
}

static std::pair<u32, u32> draw_localized(tsl::gfx::Renderer *renderer,
    const char *value, bool monospace, s32 x, s32 y, float font_size,
    tsl::Color color, ssize_t max_width = 0)
{
    char localized[2048];
    char sanitized[2048];
    const char *trans = ptc_ui_localize(value, localized, sizeof(localized));
    ptc_overlay_sanitize_utf8(trans, sanitized, sizeof(sanitized));
    return renderer->drawString(sanitized, monospace, x, y, font_size, color, max_width);
}

static inline std::pair<u32, u32> measure_localized(tsl::gfx::Renderer *renderer,
    const char *value, float font_size, bool monospace = false)
{
    return draw_localized(renderer, value, monospace, 0, 0, font_size, tsl::style::color::ColorTransparent);
}

// Word wrapping for Latin text, codepoint wrapping for CJK/long words. Measurement
// uses the same system font and sanitization as drawing, without bitmap allocation.
static std::vector<std::string> overlay_wrap(tsl::gfx::Renderer *renderer,
    const char *value, int size, int width)
{
    char localized[2048], sanitized[2048];
    ptc_overlay_sanitize_utf8(ptc_ui_localize(value, localized, sizeof(localized)),
        sanitized, sizeof(sanitized));
    std::vector<std::string> lines;
    std::string line;
    const std::string text(sanitized);
    auto measured = [&](const std::string &s) {
        return static_cast<int>(renderer->drawString(s.c_str(), false, 0, 0,
            size, tsl::style::color::ColorTransparent).first);
    };
    auto flush = [&] {
        while (!line.empty() && line.back() == ' ') line.pop_back();
        lines.push_back(line);
        line.clear();
    };
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\n') { flush(); ++i; continue; }
        if (text[i] == ' ' && line.empty()) { ++i; continue; }
        size_t end = i + 1;
        const auto c = static_cast<unsigned char>(text[i]);
        if (c >= 0xc0) {
            while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) ++end;
        } else if (c != ' ') {
            while (end < text.size() && static_cast<unsigned char>(text[end]) < 0x80 &&
                text[end] != ' ' && text[end] != '\n') ++end;
        }
        const std::string token = text.substr(i, end - i);
        if (!line.empty() && measured(line + token) > width) flush();
        if (token == " " && line.empty()) { i = end; continue; }
        if (measured(token) > width) {
            for (size_t j = 0; j < token.size();) {
                size_t next = j + 1;
                while (next < token.size() && (static_cast<unsigned char>(token[next]) & 0xc0) == 0x80) ++next;
                const std::string character = token.substr(j, next - j);
                if (!line.empty() && measured(line + character) > width) flush();
                line += character;
                j = next;
            }
        } else line += token;
        i = end;
    }
    if (!line.empty() || lines.empty() || (!text.empty() && text.back() == '\n')) flush();
    return lines;
}

static int overlay_text(tsl::gfx::Renderer *renderer, const char *value,
    int x, int top, int width, int size, tsl::Color color, bool centered = false)
{
    const auto lines = overlay_wrap(renderer, value, size, width);
    const int step = (size * 6 + 4) / 5;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto measured = renderer->drawString(lines[i].c_str(), false, 0, 0,
            size, tsl::style::color::ColorTransparent);
        const int left = centered ? x + (width - static_cast<int>(measured.first)) / 2 : x;
        renderer->drawString(lines[i].c_str(), false, left, top + size + static_cast<int>(i) * step,
            size, renderer->a(color));
    }
    return static_cast<int>(lines.size()) * step;
}

static int overlay_text_height(tsl::gfx::Renderer *renderer, const char *value, int width, int size)
{
    return static_cast<int>(overlay_wrap(renderer, value, size, width).size()) * ((size * 6 + 4) / 5);
}

static inline std::pair<u32, u32> draw_localized_center(tsl::gfx::Renderer *renderer,
    const char *value, s32 box_x, s32 box_y, s32 box_w, s32 box_h,
    float font_size, tsl::Color color, bool monospace = false)
{
    const auto size = measure_localized(renderer, value, font_size, monospace);
    const s32 x = box_x + (box_w - static_cast<s32>(size.first)) / 2;
    const s32 y = box_y + static_cast<s32>((box_h + font_size * 0.72f) / 2.0f);
    return draw_localized(renderer, value, monospace, x, y, font_size, color);
}

static inline std::pair<u32, u32> draw_localized_right(tsl::gfx::Renderer *renderer,
    const char *value, s32 right_x, s32 y, float font_size,
    tsl::Color color, bool monospace = false)
{
    const auto size = measure_localized(renderer, value, font_size, monospace);
    return draw_localized(renderer, value, monospace, right_x - static_cast<s32>(size.first), y, font_size, color);
}

// The offline code MAC binds the day index reported by status, so the console date
// must come from the summary. A local-clock guess could advertise a day the
// sysmodule would reject.
static void format_console_date(const PtcCompanionResultSummary &summary, char *out, size_t out_size)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    if (!out || out_size == 0) return;
    if (!summary.valid || summary.day_index < 0 || summary.day_index > 0xffff ||
        !ptc_date_from_day_index(static_cast<uint16_t>(summary.day_index), &year, &month, &day)) {
        std::snprintf(out, out_size, ptc_ui_text(PTC_UI_T_CONSOLE_DATE_PENDING_REFRESH));
        return;
    }
    std::snprintf(out, out_size, ptc_ui_text(PTC_UI_T_CONSOLE_TODAY_04U_02U_02U),
                  static_cast<unsigned int>(year), static_cast<unsigned int>(month),
                  static_cast<unsigned int>(day));
}

enum class OverlayRequestKind {
    None,
    Status,
    OverlayReady,
    PreviewOfflineCode,
    OfflineCode,
    ClaimDailyBuffer,
    AddTodayMinutes,
    DisableTodayLimit,
    SkipBedtime,
    SkipEyeCare,
    WaiveDock,
    ClearBedtimeSkip,
    DisableBedtime,
    RestoreInstallSnapshot,
};

enum class ParentView {
    Child,
    Pin,
    Actions,
    Confirm,
    Result,
};

enum class OverlayNavbarScheme {
    Default = 0,
    Breadcrumb = 1,
    Capsule = 2,
    Stepper = 3
};

enum class ChildCardKind { Plain, Time, Eye, Bedtime, Dock };

struct OverlayPanelLine {
    const char *text;
    int size;
    tsl::Color color;
};
