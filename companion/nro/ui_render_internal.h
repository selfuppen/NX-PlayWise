#ifndef PTC_COMPANION_UI_RENDER_INTERNAL_H
#define PTC_COMPANION_UI_RENDER_INTERNAL_H

#include "ui_graphics.h"
#include "ui_layout.h"
#include "ui_state.h"
#include "../album_restriction.h"

#include <switch.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../common/time/ptc_time.h"
#include "../../common/rules/holiday_calendar.h"
#include "../../common/version.h"

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define UI_RGB(rgb) (0x01000000u | (rgb))

typedef enum {
    UI_INK = 0x02000000u + 0,
    UI_MUTED = 0x02000000u + 1,
    UI_DISABLED = 0x02000000u + 2,
    UI_SURFACE = 0x02000000u + 3,
    UI_RAISED = 0x02000000u + 4,
    UI_PAGE = 0x02000000u + 5,
    UI_BORDER = 0x02000000u + 6,
    UI_CONTROL = 0x02000000u + 7,
    UI_ACCENT = 0x02000000u + 8,
    UI_FOCUS = 0x02000000u + 9,
    UI_ON_ACCENT = 0x02000000u + 10,
    UI_ACCENT_SOFT = 0x02000000u + 11,
    UI_SUCCESS = 0x02000000u + 12,
    UI_WARNING = 0x02000000u + 13,
    UI_DANGER = 0x02000000u + 14,
    UI_SUCCESS_SOFT = 0x02000000u + 15,
    UI_WARNING_SOFT = 0x02000000u + 16,
    UI_DANGER_SOFT = 0x02000000u + 17,
    UI_CORAL = 0x02000000u + 18,
    UI_KEY_GLYPH_BG = 0x02000000u + 19,
    UI_KEY_GLYPH_BORDER = 0x02000000u + 20,
    UI_GAUGE_SLOT = 0x02000000u + 21,
    UI_GAUGE_SLOT_BORDER = 0x02000000u + 22,
} UiColor;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} UiRect;

typedef enum {
    UI_ACTION_ICON_CLOCK = 0,
    UI_ACTION_ICON_ADD_TIME,
    UI_ACTION_ICON_INFINITY,
    UI_ACTION_ICON_RESTORE,
    UI_ACTION_ICON_CLEAR_OVERRIDE,
    UI_ACTION_ICON_MOON,
    UI_ACTION_ICON_BUFFER,
    UI_ACTION_ICON_CALENDAR_RANGE,
    UI_ACTION_ICON_HOLIDAY,
    UI_ACTION_ICON_WEEKLY,
    UI_ACTION_ICON_CONSOLE,
    UI_ACTION_ICON_DEVICE,
    UI_ACTION_ICON_SLIDERS,
    UI_ACTION_ICON_HISTORY,
    UI_ACTION_ICON_THEME,
    UI_ACTION_ICON_KEY,
    UI_ACTION_ICON_CONTROLLER,
    UI_ACTION_ICON_HOMEBREW,
    UI_ACTION_ICON_ACTIVITY,
    UI_ACTION_ICON_SHIELD,
    UI_ACTION_ICON_REPAIR,
    UI_ACTION_ICON_STOP,
    UI_ACTION_ICON_EXPORT,
    UI_ACTION_ICON_INFO
} UiActionIcon;

typedef enum {
    UI_ACTION_VISUAL_NONE = 0,
    UI_ACTION_VISUAL_QUICK_ADD,
    UI_ACTION_VISUAL_THEME
} UiActionVisual;

typedef struct {
    Framebuffer framebuffer;
    FT_Library library;
    FT_Face face;
    bool framebuffer_ready;
    bool font_ready;
    bool pl_ready;
} UiRuntime;

typedef struct {
    const char *title;
    const char *subtitle;
    uint32_t accent;
    UiActionIcon icon;
    UiActionVisual visual;
} UiAction;

extern UiRuntime g_ui;
extern const PtcUiPalette *g_palette;
extern PtcUiThemeView g_theme;
extern uint32_t *g_background_cache;
extern const PtcUiPalette *g_background_palette;
extern bool g_background_docked;
extern int g_font_pixel_size;

#ifndef PTC_UI_PREVIEW_ANIM_CLOCK_MS
extern PtcUiPalette g_palette_from;
extern int64_t g_theme_blend_started_ms;
extern bool g_theme_blending;
extern const PtcUiPalette *g_palette_prev;
uint32_t ui_theme_mix(uint32_t from_rgb, uint32_t to_rgb);
#define UI_BLENDED(field) ui_theme_mix(g_palette_from.field, g_palette->field)
#else
#define UI_BLENDED(field) (g_palette->field)
#endif

bool is_docked_mode(void);
int64_t ptc_ui_render_now(void);
uint32_t pack_rgb(uint32_t rgb);
uint32_t ui_mix_rgb(uint32_t base, uint32_t target, int percent);
uint32_t resolve_color(uint32_t source);
void blend_pixel(uint32_t *pixels, uint32_t stride, int x, int y, uint32_t color, uint8_t alpha);
void fill_rect_packed(uint32_t *pixels, uint32_t stride, UiRect rect, uint32_t color);
void fill_rect(uint32_t *pixels, uint32_t stride, UiRect rect, uint32_t color);
void fill_round_rect(uint32_t *pixels, uint32_t stride, UiRect rect, int radius, uint32_t color);
void fill_round_rect_gradient(uint32_t *pixels, uint32_t stride, UiRect rect, int radius,
                              uint32_t top, uint32_t bottom);
void draw_rect_outline(uint32_t *pixels, uint32_t stride, UiRect rect, int radius, int width, uint32_t color);
void draw_focus_ring(uint32_t *pixels, uint32_t stride, UiRect rect, int radius);
void draw_card_shadow(uint32_t *pixels, uint32_t stride, UiRect rect, int radius);
void draw_circle_outline(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, int width, uint32_t color);
void draw_ring_progress(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius,
                        int thickness, float fraction, uint32_t track, uint32_t fill);
void draw_line(uint32_t *pixels, uint32_t stride, int x0, int y0, int x1, int y1, int width, uint32_t color);
void draw_status_symbol(uint32_t *pixels, uint32_t stride, int x, int y, uint32_t color, int kind);
float ui_ease_out(float value);
int get_breathing_phase(void);
uint32_t ui_darken(uint32_t rgb, int percent);
void draw_round_rect_shadow(uint32_t *pixels, uint32_t stride, UiRect rect,
                            int radius, int blur, int peak_alpha, int y_bias);
int measure_text(const char *text, int size);
void draw_text(uint32_t *pixels, uint32_t stride, int x, int baseline, const char *text, int size, uint32_t color);
void draw_text_bold(uint32_t *pixels, uint32_t stride, int x, int baseline, const char *text, int size, uint32_t color);
void draw_text_center(uint32_t *pixels, uint32_t stride, UiRect rect, const char *text, int size, uint32_t color);
void fit_text(char *out, size_t out_size, const char *text, int size, int max_width);
void draw_header(uint32_t *pixels, uint32_t stride, const char *title, const char *subtitle);
void draw_time_status_bar(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
uint32_t time_projection_color(PtcUiTimeState state);
void draw_shoulder_key_glyph(uint32_t *pixels, uint32_t stride, int x, int y,
                             int width, int height, const char *key_str, bool disabled);
void draw_arrow_glyph(uint32_t *pixels, uint32_t stride, int cx, int cy, bool up, uint32_t color);
void draw_r_stick_glyph(uint32_t *pixels, uint32_t stride, int x, int y, int size, int direction);
void draw_r_stick_axis_glyph(uint32_t *pixels, uint32_t stride, int x, int y, int size, bool vertical, int direction);
void draw_button_label(uint32_t *pixels, uint32_t stride, UiRect box, const char *label, int size, uint32_t color);
void draw_footer_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect, const char *label);
void draw_parent_status_footer(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
UiRect to_uirect(PtcUiRect rect);
void draw_dialog_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect,
                        const char *label, uint32_t background, uint32_t foreground, bool outline);
int draw_wrapped_text(uint32_t *pixels, uint32_t stride, int x, int baseline,
                      const char *text, int size, int max_width, int line_height,
                      int max_lines, uint32_t color);
void draw_candidate_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect,
                           const char *label, uint32_t background, uint32_t foreground,
                           bool selected, bool disabled);
void draw_overlay_actions(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                          const char *confirm_label);
void draw_notice(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void format_event_time(int64_t timestamp, bool full, char *out, size_t out_size);

void draw_child(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void draw_setup(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void draw_error(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void draw_parent(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
bool draw_parent_plan_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
const char *ui_rule_source_label(const char *source);
void draw_plan_impact(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                      PtcUiPlanKind kind, bool dirty, UiRect rect);
void draw_plan_impact_compact(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                              PtcUiPlanKind kind, UiRect rect);
uint32_t time_state_accent(bool available, bool unlimited, int minutes);
uint32_t status_age_color(const PtcUiModel *model);
void format_status_age(const PtcUiModel *model, char *out, size_t out_size);
void format_duration(int minutes, char *out, size_t out_size);
void draw_time_state_card(uint32_t *pixels, uint32_t stride, UiRect rect,
                          const char *label, const char *value, uint32_t accent);
void draw_transition_arrow(
    uint32_t *pixels, uint32_t stride, int cx, int cy, uint32_t color);
void draw_remaining_transition(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    const char *before_label,
    const char *before_value,
    uint32_t before_accent,
    const char *after_label,
    const char *after_value,
    uint32_t after_accent);
void draw_unchanged_quota_card(
    uint32_t *pixels, uint32_t stride, UiRect rect, const char *reason);
void draw_action_card(uint32_t *pixels, uint32_t stride, UiRect rect,
                      const UiAction *action, bool selected, PtcUiActionState state,
                      int reserved_right);
void draw_plan_card(uint32_t *pixels, uint32_t stride, UiRect card, bool focused);
void draw_toggle_switch(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    bool is_on,
    bool selected,
    bool disabled,
    const char *on_label,
    const char *off_label);
const char *bedtime_override_label(PtcBedtimeOverrideMode mode);
void home_button(uint32_t *pixels, uint32_t stride, PtcUiRect target,
                 const char *label, bool primary, bool selected, bool disabled);
extern const UiAction GRANT_MANAGER_ACTIONS[];

void draw_dialog_shell(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                       UiRect *dialog, int width, int height);
void draw_minutes_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void draw_weekly_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void draw_numpad_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void draw_pin_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void draw_confirm_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void draw_minute_editor_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void draw_notice_details_dialog(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
bool draw_account_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
bool draw_plan_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
bool draw_support_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void draw_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model);
void ui_glyph_cache_clear(void);
void ui_background_rebuild(void);

#endif
