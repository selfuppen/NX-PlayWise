/* Deterministic host screenshots of the real renderer, not a reimplemented UI. */
#define _POSIX_C_SOURCE 200809L
#ifdef _WIN32
#include <direct.h>
#define ptc_mkdir(dir) _mkdir(dir)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define ptc_mkdir(dir) mkdir(dir, 0777)
#endif
#include "../../companion/nro/ui_render_internal.h"

uint32_t preview_pixels[1280 * 720];
static bool preview_all_styles;

static struct {
    bool enabled, started;
    int labels, failed;
} dock_editor_trace;

static struct {
    bool enabled;
    int title_count, summary_count;
    UiRect title, summary;
} child_heading_trace;

static struct {
    bool enabled;
    const char *status;
    int title_count, status_count;
    UiRect title, badge;
} support_tab_trace;

static struct {
    bool enabled;
    PtcUiOverlay overlay;
    int failed, hints;
    char guide[512];
} bedtime_trace;

void ptc_ui_preview_record_text(int x, int baseline, const char *text, int size)
{
    if (!text) return;
    UiRect bounds = {x, baseline - size, measure_text(text, size), size};
    if (bedtime_trace.enabled) {
        PtcUiRect dialog = ptc_ui_dialog_rect(960, 560);
        PtcUiRect mode = ptc_ui_bedtime_overlay_field_rect(bedtime_trace.overlay, 0);
        if (size == 18 && x == dialog.x + 34 && baseline >= dialog.y + 70 && baseline <= dialog.y + 125) {
            size_t used = strlen(bedtime_trace.guide);
            snprintf(bedtime_trace.guide + used, sizeof(bedtime_trace.guide) - used,
                "%s%s", used ? " " : "", text);
            if (baseline + 8 > mode.y || x < dialog.x + 34 ||
                x + bounds.width > dialog.x + dialog.w - 34) ++bedtime_trace.failed;
        }
        if (strcmp(text, ptc_ui_text(PTC_UI_T_A_FINE_ADJUSTMENT)) == 0 ||
            strcmp(text, ptc_ui_text(PTC_UI_T_NOT_EDITABLE)) == 0) {
            ++bedtime_trace.hints;
            bool inside = false;
            for (int field = 1; field < 3; ++field) {
                PtcUiRect row = ptc_ui_bedtime_overlay_field_rect(bedtime_trace.overlay, field);
                inside |= bounds.x >= row.x + 18 && bounds.x + bounds.width <= row.x + row.w - 18 &&
                    bounds.y >= row.y && baseline <= row.y + row.h &&
                    bounds.x >= row.x + 18 + measure_text("23:59", 22) + 12;
            }
            if (!inside) ++bedtime_trace.failed;
        }
    }
    if (dock_editor_trace.enabled) {
        PtcUiRect area = ptc_ui_minute_editor_summary_rect();
        if (strcmp(text, ptc_ui_text(PTC_UI_T_DOCK_CURRENT_POLICY)) == 0) dock_editor_trace.started = true;
        if (dock_editor_trace.started && x >= area.x && bounds.y >= area.y && bounds.y < area.y + area.h) {
            if (bounds.x + bounds.width > area.x + area.w || baseline > area.y + area.h)
                ++dock_editor_trace.failed;
            const PtcUiTextId labels[] = {PTC_UI_T_DOCK_CURRENT_POLICY, PTC_UI_T_DOCK_USED_SHORT,
                PTC_UI_T_DOCK_AFTER_LEFT_SHORT, PTC_UI_T_DOCK_AFTER_STATUS};
            for (int i = 0; i < 4; ++i)
                if (strcmp(text, ptc_ui_text(labels[i])) == 0) ++dock_editor_trace.labels;
        }
    }
    if (support_tab_trace.enabled && bounds.y >= 100 && baseline <= 164) {
        if (strcmp(text, ptc_ui_text(PTC_UI_T_SUPPORT_RECOVERY)) == 0) {
            support_tab_trace.title = bounds;
            ++support_tab_trace.title_count;
        } else if (support_tab_trace.status && strcmp(text, support_tab_trace.status) == 0) {
            support_tab_trace.badge = bounds;
            ++support_tab_trace.status_count;
        }
    }
    if (!child_heading_trace.enabled) return;
    if (strcmp(text, ptc_ui_text(PTC_UI_T_7_DAY_PLAN_BEDTIME_FORECAST)) == 0) {
        child_heading_trace.title = bounds;
        ++child_heading_trace.title_count;
    } else if (strcmp(text, ptc_ui_text(PTC_UI_T_REST_AS_PLANNED)) == 0) {
        child_heading_trace.summary = bounds;
        ++child_heading_trace.summary_count;
    }
}

static int check_bedtime_editors(const PtcUiModel *baseline)
{
    int failed = 0;
    for (int lang = PTC_UI_LANGUAGE_SIMPLIFIED; lang <= PTC_UI_LANGUAGE_ENGLISH; ++lang) {
        ptc_ui_language_set_resolved((PtcUiLanguagePreference)lang);
        for (int style = 0; style < PTC_UI_THEME_STYLE_COUNT; ++style) {
            for (int dark = 0; dark < 2; ++dark) {
                PtcUiThemeView theme = ptc_ui_theme_make_styled_view(
                    dark ? PTC_UI_THEME_DARK : PTC_UI_THEME_LIGHT, (PtcUiThemeStyle)style,
                    PTC_UI_SYSTEM_THEME_UNAVAILABLE);
                for (int state = 0; state < 4; ++state) {
                    PtcUiModel model = *baseline;
                    model.view = PTC_UI_PARENT;
                    model.parent_page = PTC_UI_PARENT_PLAN;
                    model.plan_page = PTC_UI_PLAN_PAGE_BEDTIME;
                    model.overlay_open_frames = PTC_UI_OVERLAY_OPEN_FRAMES;
                    model.overlay = state == 0 ? PTC_UI_OVERLAY_BEDTIME_WINDOW : PTC_UI_OVERLAY_BEDTIME_SPECIAL;
                    model.rendered_overlay = model.overlay;
                    model.bedtime_editor_day = 1;
                    model.bedtime_special_kind = 2;
                    model.draft_bedtime_policy.week[1] = (PtcBedtimeWindow){true, 1380, 480};
                    model.draft_bedtime_policy.scheduled_override.rule.mode = (PtcBedtimeOverrideMode)(state - 1);
                    model.draft_bedtime_policy.scheduled_override.rule.window = (PtcBedtimeWindow){true, 1380, 480};
                    snprintf(model.overlay_body, sizeof(model.overlay_body), "%s", ptc_ui_text(state == 0 ?
                        PTC_UI_T_SET_THE_NO_PLAY_PERIOD_FROM_THAT : PTC_UI_T_SELECT_INHERIT_CLOSE_OR_CUSTOM_CUSTOM_WINDOWS));
                    memset(&bedtime_trace, 0, sizeof(bedtime_trace));
                    bedtime_trace.enabled = true;
                    bedtime_trace.overlay = model.overlay;
                    ptc_ui_graphics_draw(&model, &theme);
                    bedtime_trace.enabled = false;
                    if (bedtime_trace.failed || bedtime_trace.hints != 2 ||
                        strcmp(bedtime_trace.guide, model.overlay_body) != 0) {
                        fprintf(stderr, "FAIL: bedtime lang=%d style=%d dark=%d state=%d bounds=%d hints=%d guide=%s\n",
                            lang, style, dark, state, bedtime_trace.failed, bedtime_trace.hints, bedtime_trace.guide);
                        ++failed;
                    }
                }
            }
        }
    }
    fprintf(stderr, "%s: bedtime complete guide and separated field hints (3 languages, 4 styles, 2 modes, 4 editor states)\n",
        failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}

static int check_parent_tabs(const PtcUiModel *baseline)
{
    int failed = 0;
    for (int lang = PTC_UI_LANGUAGE_SIMPLIFIED; lang <= PTC_UI_LANGUAGE_ENGLISH; ++lang) {
        ptc_ui_language_set_resolved((PtcUiLanguagePreference)lang);
        for (int dark = 0; dark < 2; ++dark) {
            PtcUiThemeView theme = ptc_ui_theme_make_view(dark ? PTC_UI_THEME_DARK : PTC_UI_THEME_LIGHT,
                PTC_UI_SYSTEM_THEME_UNAVAILABLE);
            for (int state = 0; state < 5; ++state) {
                for (int page = 0; page <= PTC_UI_PARENT_PAGE_COUNT; ++page) {
                    PtcUiModel model = *baseline;
                    bool subpage = page == PTC_UI_PARENT_PAGE_COUNT;
                    model.view = PTC_UI_PARENT;
                    model.parent_page = subpage ? PTC_UI_PARENT_PLAN : (PtcUiParentPage)page;
                    model.last_parent_page = model.parent_page;
                    model.plan_page = subpage ? PTC_UI_PLAN_PAGE_WEEKLY : PTC_UI_PLAN_PAGE_ROOT;
                    model.disable_flag_present = state == 1;
                    model.recovery_active = state == 2;
                    snprintf(model.setup_phase, sizeof(model.setup_phase), "%s",
                        state == 3 ? "pending" : (state == 4 ? "failed" : "active"));
                    memset(&support_tab_trace, 0, sizeof(support_tab_trace));
                    support_tab_trace.enabled = true;
                    support_tab_trace.status = ptc_ui_settings_status_label(&model);
                    ptc_ui_graphics_draw(&model, &theme);
                    support_tab_trace.enabled = false;
                    PtcUiRect tab = ptc_ui_parent_tab_rect(PTC_UI_PARENT_SUPPORT);
                    UiRect title = support_tab_trace.title, badge = support_tab_trace.badge;
                    bool fits = subpage ? support_tab_trace.title_count == 0 && support_tab_trace.status_count == 0 :
                        support_tab_trace.title_count == 1 && title.x >= tab.x + 4 &&
                        title.x + title.width <= tab.x + tab.w - 4 && title.y >= tab.y &&
                        title.y + title.height <= tab.y + tab.h;
                    if (!subpage && support_tab_trace.status) {
                        fits = fits && support_tab_trace.status_count == 1 && badge.x >= tab.x + 8 &&
                            badge.x + badge.width <= tab.x + tab.w - 8 && badge.y >= tab.y &&
                            badge.y + badge.height <= tab.y + tab.h && title.y + title.height + 2 <= badge.y;
                    } else {
                        fits = fits && support_tab_trace.status_count == 0;
                    }
                    if (!fits) {
                        fprintf(stderr, "FAIL: support tab lang=%d dark=%d state=%d page=%d title=%d [%d,%d,%d,%d] badge=%d [%d,%d,%d,%d]\n",
                            lang, dark, state, page, support_tab_trace.title_count, title.x, title.y, title.width, title.height,
                            support_tab_trace.status_count, badge.x, badge.y, badge.width, badge.height);
                        ++failed;
                    }
                }
            }
        }
    }
    fprintf(stderr, "%s: support tab complete text, separated bounds and subpage visibility (3 languages, 2 themes, 5 states, 6 pages)\n",
        failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}

static int check_child_forecast_heading(bool dark)
{
    PtcUiRect forecast = ptc_ui_child_forecast_rect();
    UiRect title = child_heading_trace.title, summary = child_heading_trace.summary;
    bool fits = child_heading_trace.title_count == 1 && child_heading_trace.summary_count == 1 &&
        title.x >= forecast.x + 16 && title.x + title.width + 12 <= summary.x &&
        summary.x + summary.width <= forecast.x + forecast.w - 16 &&
        title.y >= forecast.y + 8 && summary.y >= forecast.y + 8 &&
        title.y + title.height <= forecast.y + 56 && summary.y + summary.height <= forecast.y + 56;
    fprintf(stderr, "%s: child forecast heading lang=%d dark=%d title=[%d,%d) summary=[%d,%d)\n",
        fits ? "PASS" : "FAIL", (int)ptc_ui_language_get_resolved(), dark,
        title.x, title.x + title.width, summary.x, summary.x + summary.width);
    return fits ? 0 : 1;
}

/* Child home grid: hero, seven-day forecast and the right action column must share one
 * bottom edge (644) above the footer, keep the 14px gap, and the details entry must
 * carry a hint that still fits inside its own button. */
static int check_child_home_grid(bool dark)
{
    PtcUiRect hero = ptc_ui_child_hero_rect();
    PtcUiRect forecast = ptc_ui_child_forecast_rect();
    PtcUiRect submit = ptc_ui_child_submit_rect();
    PtcUiRect buffer = ptc_ui_child_buffer_rect();
    PtcUiRect details = ptc_ui_home_details_rect(false);
    PtcUiRect status = ptc_ui_child_status_rect();
    int bottom = hero.y + hero.h + 14 + forecast.h;
    int hint_max = details.w - 60;
    int hint_lines = count_wrapped_lines(ptc_ui_text(PTC_UI_T_USAGE_DETAILS_HINT), 13, hint_max, 99);
    bool fits = hero.x == forecast.x && hero.w == forecast.w &&
        hero.y + hero.h + 14 == forecast.y &&
        forecast.y + forecast.h == bottom && status.y + status.h == bottom &&
        bottom < ptc_ui_child_footer_rect(1).y &&
        submit.x == buffer.x && buffer.x == details.x && details.x == status.x &&
        submit.w == buffer.w && buffer.w == details.w && details.w == status.w &&
        submit.y == hero.y &&
        buffer.y - (submit.y + submit.h) == 14 &&
        details.y - (buffer.y + buffer.h) == 14 &&
        status.y - (details.y + details.h) == 14 &&
        hint_max > 0 && hint_lines >= 1 && hint_lines <= 3;
    fprintf(stderr, "%s: child home grid dark=%d hero=%d..%d forecast=%d..%d right=%d..%d hint_lines=%d\n",
        fits ? "PASS" : "FAIL", dark, hero.y, hero.y + hero.h, forecast.y,
        forecast.y + forecast.h, status.y, status.y + status.h, hint_lines);
    return fits ? 0 : 1;
}

/* The child usage-details page leads with a reading-order guide bar. It must stay
 * on one measured line inside its own rect and above the first card row, and the
 * three-column cards must not grow into the shared bottom button band. */
static int check_child_details_guide(bool dark)
{
    UiRect guide = to_uirect(ptc_ui_home_details_guide_rect());
    int size = 13;
    int width = measure_text(ptc_ui_text(PTC_UI_T_DETAIL_CHILD_GUIDE), size);
    bool fits = guide.x >= 40 && guide.x + guide.width <= 1240 &&
        guide.y >= 72 && guide.y + guide.height <= 144 &&
        guide.height >= size + 12 &&
        width <= guide.width - 46;
    PtcUiModel child = {0};
    child.view = PTC_UI_CHILD;
    PtcUiRect button = ptc_ui_home_details_action_rect(&child, 0);
    fits = fits && button.y >= guide.y + guide.height + 280 &&
        button.y + button.h <= 616;
    /* 孩子区按钮收薄 16px 以让出导读条高度，底边仍与家长区一致。 */
    fits = fits && button.y == 514 && button.h == 102;
    PtcUiModel parent = {0};
    parent.view = PTC_UI_PARENT;
    PtcUiRect parent_button = ptc_ui_home_details_action_rect(&parent, 0);
    fits = fits && parent_button.y == 498 && parent_button.h == 118;
    fprintf(stderr, "%s: child details guide lang=%d dark=%d width=%d/%d button=%d..%d\n",
        fits ? "PASS" : "FAIL", (int)ptc_ui_language_get_resolved(), dark,
        width, guide.width - 46, button.y, button.y + button.h);
    return fits ? 0 : 1;
}

static int check_primitives(void)
{
    const uint32_t background = pack_rgb(0x182838), ink = pack_rgb(0xe0c0a0);
    UiRect rect = {20, 20, 96, 64};
    int failed = 0, mixed = 0;
    g_palette = ptc_ui_theme_palette(PTC_UI_RESOLVED_LIGHT);
    fill_rect_packed(preview_pixels, 1280, (UiRect){0, 0, 1280, 720}, background);
    fill_round_rect(preview_pixels, 1280, rect, 16, UI_RGB(0xe0c0a0));
    for (int y = 0; y < rect.height; ++y) {
        for (int x = 0; x < rect.width; ++x) {
            uint32_t pixel = preview_pixels[(rect.y + y) * 1280 + rect.x + x];
            if (pixel != preview_pixels[(rect.y + y) * 1280 + rect.x + rect.width - 1 - x] ||
                pixel != preview_pixels[(rect.y + rect.height - 1 - y) * 1280 + rect.x + x]) ++failed;
            if (pixel != background && pixel != ink) ++mixed;
        }
    }
    if (!mixed || preview_pixels[20 * 1280 + 20] != background || preview_pixels[40 * 1280 + 40] != ink) ++failed;
    /* A stroke must not erase text or other content inside its inner contour. */
    preview_pixels[40 * 1280 + 40] = pack_rgb(0xff00ff);
    draw_rect_outline(preview_pixels, 1280, rect, 16, 3, UI_RGB(0xabcdef));
    if (preview_pixels[40 * 1280 + 40] != pack_rgb(0xff00ff) || preview_pixels[20 * 1280 + 20] != background) ++failed;
    if (preview_pixels[21 * 1280 + 60] != pack_rgb(0xabcdef) || preview_pixels[40 * 1280 + 21] != pack_rgb(0xabcdef)) ++failed;
    draw_focus_ring(preview_pixels, 1280, rect, 16);
    if (preview_pixels[40 * 1280 + 4] != background ||
        preview_pixels[40 * 1280 + 16] == background ||
        preview_pixels[40 * 1280 + 19] != resolve_color(UI_FOCUS) ||
        preview_pixels[40 * 1280 + 20] != resolve_color(UI_ACCENT) ||
        preview_pixels[40 * 1280 + 40] != pack_rgb(0xff00ff)) ++failed;
    /* Requested radii remain distinct even on large cards. */
    fill_round_rect(preview_pixels, 1280, (UiRect){200, 20, 96, 64}, 8, UI_RGB(0xe0c0a0));
    if (preview_pixels[22 * 1280 + 202] == preview_pixels[22 * 1280 + 22]) ++failed;
    fill_rect_packed(preview_pixels, 1280, (UiRect){0, 0, 1280, 720}, background);
    draw_circle_outline(preview_pixels, 1280, 100, 100, 16, 3, UI_RGB(0xe0c0a0));
    mixed = 0;
    for (int y = -17; y <= 17; ++y) {
        for (int x = -17; x <= 17; ++x) {
            uint32_t pixel = preview_pixels[(100 + y) * 1280 + 100 + x];
            if (pixel != preview_pixels[(100 - y) * 1280 + 100 - x]) ++failed;
            if (pixel != ink && pixel != background) ++mixed;
        }
    }
    if (!mixed || preview_pixels[100 * 1280 + 100] != background) ++failed;
    /* Non-default stride and off-screen shapes must preserve all guard pixels. */
    uint32_t stride = 1284;
    uint32_t *guarded = malloc(stride * 722 * sizeof(*guarded));
    if (!guarded) return 1;
    for (unsigned int i = 0; i < stride * 722; ++i) guarded[i] = background;
    uint32_t *canvas = guarded + stride + 2;
    fill_round_rect(canvas, stride, (UiRect){-18, -15, 80, 60}, 16, UI_RGB(0xffffff));
    draw_rect_outline(canvas, stride, (UiRect){1250, 680, 90, 90}, 16, 3, UI_RGB(0xffffff));
    draw_circle_outline(canvas, stride, 0, 719, 20, 3, UI_RGB(0xffffff));
    fill_round_rect(canvas, stride, (UiRect){1279, 0, 1, 1}, 16, UI_RGB(0xffffff));
    draw_rect_outline(canvas, stride, (UiRect){1279, 0, 1, 1}, 16, 3, UI_RGB(0xffffff));
    fill_round_rect(canvas, stride, (UiRect){0, 0, 0, 0}, 16, UI_RGB(0xffffff));
    for (unsigned int y = 0; y < 722; ++y)
        for (unsigned int x = 0; x < stride; ++x)
            if ((y == 0 || y == 721 || x < 2 || x >= 1282) && guarded[y * stride + x] != background) ++failed;
    free(guarded);
    /* Procedural emoji glyphs, variation selectors, and text width measurement */
    {
        if (measure_text("🌙", 12) != 12) ++failed;
        if (measure_text("🎁", 12) != 12) ++failed;
        if (measure_text("🎯", 12) != 12) ++failed;
        if (measure_text("🛡️", 12) != 12) ++failed; /* 🛡 (12) + ️ (0) = 12 */
        if (measure_text("⏰", 12) != 12) ++failed;
        if (measure_text("🔒", 12) != 12) ++failed;
        if (measure_text("⭐", 12) != 12) ++failed;
        if (measure_text("🌿", 12) != 12) ++failed;
        if (measure_text("ℹ️", 12) != 12) ++failed; /* ℹ (12) + ️ (0) = 12 */
        if (measure_text("🎮", 12) != 12) ++failed;
        if (measure_text("📺", 12) != 12) ++failed;
        if (measure_text("⚙️", 12) != 12) ++failed;
        if (measure_text("🔑", 12) != 12) ++failed;
        if (measure_text("📱", 12) != 12) ++failed;
        if (measure_text("📊", 12) != 12) ++failed;
        if (measure_text("⚡", 12) != 12) ++failed;
        if (measure_text("🔄", 12) != 12) ++failed;
        if (measure_text("🛑", 12) != 12) ++failed;
        if (measure_text("🔧", 12) != 12) ++failed;
        if (measure_text("📦", 12) != 12) ++failed;
        if (measure_text("🎨", 12) != 12) ++failed;
        if (measure_text("🔊", 12) != 12) ++failed;
        if (measure_text("📋", 12) != 12) ++failed;
        if (measure_text("🕒", 12) != 12) ++failed;
        if (measure_text("💤", 12) != 12) ++failed;
        if (measure_text("✨", 12) != 12) ++failed;
        if (measure_text("💾", 12) != 12) ++failed;
        if (measure_text("📜", 12) != 12) ++failed;
        if (measure_text("🌐", 12) != 12) ++failed;
        if (measure_text("💡", 12) != 12) ++failed;
        if (measure_text("🌟", 12) != 12) ++failed;
        if (measure_text("🔓", 12) != 12) ++failed;
        if (measure_text("📅", 12) != 12) ++failed;
        if (measure_text("›", 12) <= 0 || measure_text("‹", 12) <= 0) ++failed;
        if (measure_text("•", 12) <= 0) ++failed;
        /* Verify rendering into pixel buffer blends without crashing or guard corruption */
        fill_rect_packed(preview_pixels, 1280, (UiRect){0, 0, 100, 50}, background);
        draw_text(preview_pixels, 1280, 10, 30, "🌙 🎁 🛡️", 12, pack_rgb(0xffffff));
        int blended_pixels = 0;
        for (int py = 0; py < 50; ++py) {
            for (int px = 0; px < 100; ++px) {
                if (preview_pixels[py * 1280 + px] != background) ++blended_pixels;
            }
        }
        if (blended_pixels < 30) ++failed;
        /* Verify multi-color emoji pixels retain distinct chromatic components (not monochrome) */
        int has_gold = 0, has_blue = 0;
        for (int py = 0; py < 50; ++py) {
            for (int px = 0; px < 100; ++px) {
                uint32_t p = preview_pixels[py * 1280 + px];
                if (p != background) {
                    uint8_t r = p & 0xff;
                    uint8_t g = (p >> 8) & 0xff;
                    uint8_t b = (p >> 16) & 0xff;
                    if (r > 180 && g > 140 && b < 100) has_gold = 1;
                    if (b > 160 && r < 120) has_blue = 1;
                }
            }
        }
        if (!has_gold || !has_blue) ++failed;
    }
    /* Success, waiting and error status symbols all use the same centered
     * 20x20 boundary and must not paint outside it. */
    for (int kind = 1; kind <= 3; ++kind) {
        fill_rect_packed(preview_pixels, 1280, (UiRect){70, 70, 60, 60}, background);
        draw_status_symbol(preview_pixels, 1280, 100, 100, UI_RGB(0xe0c0a0), kind);
        int painted = 0;
        for (int y = 70; y < 130; ++y) {
            for (int x = 70; x < 130; ++x) {
                if (preview_pixels[y * 1280 + x] == background) continue;
                ++painted;
                if (x < 90 || x >= 110 || y < 90 || y >= 110) ++failed;
            }
        }
        if (!painted) ++failed;
    }
    fill_rect_packed(preview_pixels, 1280, (UiRect){0, 0, 100, 100}, background);
    ui_set_vertical_clip(40, 60);
    fill_round_rect(preview_pixels, 1280, (UiRect){10, 10, 80, 80}, 8, UI_RGB(0xffffff));
    draw_text(preview_pixels, 1280, 12, 40, "TEST", 12, UI_RGB(0xffffff));
    ui_set_vertical_clip(0, SCREEN_HEIGHT);
    int clipped_pixels = 0;
    for (int y = 0; y < 100; ++y) for (int x = 0; x < 100; ++x) {
        if (preview_pixels[y * 1280 + x] == background) continue;
        ++clipped_pixels;
        if (y < 40 || y >= 60) ++failed;
    }
    if (!clipped_pixels) ++failed;
    printf("%s: UI primitive coverage, symmetry, strokes and clipping\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}

static int save_preview(const char *directory, const char *module, const char *name, const PtcUiModel *model, bool dark)
{
    char dir_path[1024];
    char path[1024];
    FILE *file;
    unsigned char *rgb;
    int n;
    PtcUiThemeView theme = ptc_ui_theme_make_view(dark ? PTC_UI_THEME_DARK : PTC_UI_THEME_LIGHT, PTC_UI_SYSTEM_THEME_UNAVAILABLE);
    /* Previews skip update_animations, so render the tweened values at rest. */
    PtcUiModel settled = *model;
    settled.bedtime_status_enabled = settled.bedtime_policy.enabled;
    settled.displayed_remaining_minutes = settled.remaining_minutes;
    settled.displayed_grant_minutes = settled.grant_minutes;
    settled.last_parent_page = settled.parent_page;
    settled.rendered_overlay = settled.overlay;
    settled.overlay_open_frames = PTC_UI_OVERLAY_OPEN_FRAMES;
    memset(&child_heading_trace, 0, sizeof(child_heading_trace));
    child_heading_trace.enabled = strcmp(module, "child") == 0 && strcmp(name, "child") == 0;
    ptc_ui_graphics_draw(&settled, &theme);
    bool check_heading = child_heading_trace.enabled;
    child_heading_trace.enabled = false;
    if (check_heading && check_child_forecast_heading(dark)) return 1;
    /* The details dialog is tall enough that the marker at y=100 would land on its
       card titles (and the child guide bar), so it moves into the scrim under the
       dialog. Shorter dialogs keep the marker in the scrim above them. */
    if (settled.overlay == PTC_UI_OVERLAY_HOME_DETAILS)
        draw_text(preview_pixels, 1280, 540, 712, "HOST PREVIEW / SAMPLE DATA", 11, UI_RGB(g_palette->text_secondary));
    else
        draw_text(preview_pixels, 1280, 540, 100, "HOST PREVIEW / SAMPLE DATA", 11, UI_RGB(g_palette->text_secondary));
    n = snprintf(dir_path, sizeof(dir_path), "%s/%s", directory, module);
    if (n < 0 || (size_t)n >= sizeof(dir_path)) return 1;
    ptc_mkdir(dir_path);
    n = snprintf(path, sizeof(path), "%s/%s-%s.ppm", dir_path, name, dark ? "dark" : "light");
    if (n < 0 || (size_t)n >= sizeof(path)) return 1;
    const size_t total_payload = 1280 * 720 * 3;
    const char *header = "P6\n1280 720\n255\n";
    const size_t header_len = strlen(header);

    rgb = malloc(total_payload);
    if (!rgb) return 1;
    for (int y = 0; y < 720; ++y) {
        for (int x = 0; x < 1280; ++x) {
            uint32_t pixel = preview_pixels[y * 1280 + x];
            int offset = (y * 1280 + x) * 3;
            rgb[offset] = pixel & 255;
            rgb[offset + 1] = (pixel >> 8) & 255;
            rgb[offset + 2] = (pixel >> 16) & 255;
        }
    }

    /* Incremental optimization: skip re-writing identical PPM files to preserve timestamps and avoid mount I/O */
    FILE *existing = fopen(path, "rb");
    if (existing) {
        char check_header[32];
        bool identical = false;
        if (fread(check_header, 1, header_len, existing) == header_len &&
            memcmp(check_header, header, header_len) == 0) {
            unsigned char chunk_buf[65536];
            size_t compared = 0;
            identical = true;
            while (compared < total_payload) {
                size_t to_read = total_payload - compared;
                if (to_read > sizeof(chunk_buf)) to_read = sizeof(chunk_buf);
                size_t r = fread(chunk_buf, 1, to_read, existing);
                if (r != to_read || memcmp(chunk_buf, rgb + compared, to_read) != 0) {
                    identical = false;
                    break;
                }
                compared += to_read;
            }
            if (identical && fgetc(existing) != EOF) {
                identical = false;
            }
        }
        fclose(existing);
        if (identical) {
            free(rgb);
            return 0;
        }
    }

    file = fopen(path, "wb");
    if (!file) { free(rgb); return 1; }
    if (fwrite(header, 1, header_len, file) != header_len) {
        fclose(file);
        free(rgb);
        return 1;
    }
    /* One frame write avoids hundreds of small writes across a Docker bind mount. */
    bool written = fwrite(rgb, 1, total_payload, file) == total_payload;
    free(rgb);
    if (!written) { fclose(file); return 1; }
    return fclose(file) != 0;
}

static int check_dock_editor(const PtcUiModel *baseline)
{
    int failed = 0;
    const char *directory = "build/ui-dock-editor";
    (void)ptc_mkdir(directory);
    for (int lang = PTC_UI_LANGUAGE_SIMPLIFIED; lang <= PTC_UI_LANGUAGE_ENGLISH; ++lang) {
        ptc_ui_language_set_resolved((PtcUiLanguagePreference)lang);
        for (int style = 0; style < PTC_UI_THEME_STYLE_COUNT; ++style) {
            ptc_ui_theme_set_style((PtcUiThemeStyle)style);
            for (int dark = 0; dark < 2; ++dark) {
                PtcUiThemeView theme = ptc_ui_theme_make_view(dark ? PTC_UI_THEME_DARK : PTC_UI_THEME_LIGHT,
                    PTC_UI_SYSTEM_THEME_UNAVAILABLE);
                for (int scenario = 0; scenario < 5; ++scenario) {
                    PtcUiModel model = *baseline;
                    model.view = PTC_UI_PARENT;
                    model.parent_page = PTC_UI_PARENT_PLAN;
                    model.plan_page = PTC_UI_PLAN_PAGE_DOCK;
                    model.status_updated_at = scenario == 3 ? 879 : 1000;
                    model.dock_available = true;
                    model.undocked_usage_available = scenario != 2;
                    model.undocked_used_minutes = 30;
                    model.dock_policy = (PtcDockPolicy){true, true, 0};
                    model.draft_dock_policy = (PtcDockPolicy){false, true, 94};
                    ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_DOCK_MINUTES, PTC_UI_OVERLAY_NONE,
                        ptc_ui_text(PTC_UI_T_DOCK_LIMIT), ptc_ui_text(PTC_UI_T_DOCK_ALLOWANCE),
                        4, 5, 1440, scenario == 1 ? 30 : 94);
                    model.last_parent_page = model.parent_page;
                    model.rendered_overlay = model.overlay;
                    model.overlay_open_frames = PTC_UI_OVERLAY_OPEN_FRAMES;
                    if (scenario == 4) model.duration_hours_text[0] = '\0';
                    memset(&dock_editor_trace, 0, sizeof(dock_editor_trace));
                    dock_editor_trace.enabled = true;
                    ptc_ui_graphics_draw(&model, &theme);
                    dock_editor_trace.enabled = false;
                    if (dock_editor_trace.labels != 4 || dock_editor_trace.failed) {
                        fprintf(stderr, "FAIL: dock editor lang=%d style=%d dark=%d scenario=%d labels=%d bounds=%d\n",
                            lang, style, dark, scenario, dock_editor_trace.labels, dock_editor_trace.failed);
                        ++failed;
                    }
                    if (style == 0 && (scenario == 0 ||
                        (lang == PTC_UI_LANGUAGE_SIMPLIFIED && scenario == 2))) {
                        char name[64];
                        snprintf(name, sizeof(name), "dock-editor-lang-%d-%s", lang, scenario == 0 ? "94" : "unknown");
                        failed |= save_preview(directory, "plan", name, &model, dark);
                    }
                }
            }
        }
    }
    fprintf(stderr, "%s: dock editor preview bounds and labels (3 languages, 4 styles, 2 themes, 5 states)\n",
        failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}

static int render_visual_matrix(const char *directory, const PtcUiModel *baseline)
{
    const char *titles[] = {
        "", ptc_ui_text(PTC_UI_T_ADJUST_TODAY_LIMIT), ptc_ui_text(PTC_UI_T_EDIT_WEEKLY_PLAN), ptc_ui_text(PTC_UI_T_CONFIRM_CHANGES), ptc_ui_text(PTC_UI_T_ENTER_THE_EXTRA_TIME_CODE), ptc_ui_text(PTC_UI_T_GRANT_SECRET_MANAGEMENT),
        ptc_ui_text(PTC_UI_T_EXTRA_TIME_CODE_GENERATION_MANAGEMENT), ptc_ui_text(PTC_UI_T_PHONE_PC_PAIRING), ptc_ui_text(PTC_UI_T_KEEP_WEEKLY_PLAN_DRAFT), ptc_ui_text(PTC_UI_T_PARENT_SHORTCUT), ptc_ui_text(PTC_UI_T_GENERATE_CODE_ON_CONSOLE),
        ptc_ui_text(PTC_UI_T_KEEP_KEY_CHANGES), ptc_ui_text(PTC_UI_T_REDEMPTION_RESULT), ptc_ui_text(PTC_UI_T_VERIFICATION_FAILED), ptc_ui_text(PTC_UI_T_SOFTWARE_INFO), ptc_ui_text(PTC_UI_T_HOLIDAY_SCHEDULE), ptc_ui_text(PTC_UI_T_KEEP_HOLIDAY_DRAFT),
        ptc_ui_text(PTC_UI_T_SUPPORT_EVENT_DETAILS), ptc_ui_text(PTC_UI_T_BATCH_SETTINGS), ptc_ui_text(PTC_UI_T_ADVANCED_ENTRY_TO_HOMEBREW_MENU), ptc_ui_text(PTC_UI_T_ADJUST_DURATION), ptc_ui_text(PTC_UI_T_APPEARANCE_THEME), ptc_ui_text(PTC_UI_T_ENTER_PARENT_PIN),
        ptc_ui_text(PTC_UI_T_EXTRA_TIME_CODE_USAGE_RECORD), ptc_ui_text(PTC_UI_T_SPECIFIED_DATE_QUOTA), ptc_ui_text(PTC_UI_T_TODAY_S_INDEPENDENT_BUFFERING), ptc_ui_text(PTC_UI_T_FAMILY_ACTIVITY_RECORD), ptc_ui_text(PTC_UI_T_TODAY_DETAILS), ptc_ui_text(PTC_UI_T_KEEP_SCHEDULED_QUOTA_DRAFT),
        ptc_ui_text(PTC_UI_T_BEDTIME), ptc_ui_text(PTC_UI_T_QUICK_GRANT), ptc_ui_text(PTC_UI_T_EDIT_WEEKLY_BEDTIME_WINDOWS), ptc_ui_text(PTC_UI_T_EDIT_SPECIAL_BEDTIME_RULES),
        ptc_ui_text(PTC_UI_T_LEAVE_BEDTIME_EDITING), ptc_ui_text(PTC_UI_T_COPY_WEEKLY_BEDTIME_WINDOW),
        ptc_ui_text(PTC_UI_T_TODAY_DETAILS), ptc_ui_text(PTC_UI_T_TODAY_DETAILS), ptc_ui_text(PTC_UI_T_UI_LANGUAGE),
        ptc_ui_text(PTC_UI_T_EXPORT_CONFIGURATION_FILES_FOR_USE_ON_MOBILE), ptc_ui_text(PTC_UI_T_EYE_CARE),
        ptc_ui_text(PTC_UI_T_REGION_CALENDAR_MANAGER), ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_TITLE)
    };
    int failed = 0;
    for (int dark = 0; dark < 2; ++dark) {
        for (int overlay = PTC_UI_OVERLAY_MINUTES; overlay <= PTC_UI_OVERLAY_CALENDAR_FORMAT; ++overlay) {
            PtcUiModel model = *baseline;
            char name[48];
            PtcRules rules;
            uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
            ptc_rules_default(&rules);
            memcpy(model.current_week, rules.week, sizeof(model.current_week));
            memcpy(model.draft_week, rules.week, sizeof(model.draft_week));
            model.view = PTC_UI_PARENT;
            model.overlay = (PtcUiOverlay)overlay;
            model.minimum_minutes = 1; model.maximum_minutes = 1440; model.draft_minutes = 120;
            model.grant_minutes = 30; model.grant_max_minutes = 240;
            model.numpad_purpose = PTC_UI_NUMPAD_OFFLINE_CODE;
            model.numpad_max_digits = 8;
            model.daily_buffer_minutes = 10;
            model.draft_autonomy_policy.daily_buffer_minutes = 10;
            model.draft_eye_care_policy.play_minutes = 40;
            model.draft_eye_care_policy.rest_minutes = 10;
            model.calendar_manager_count = 6;
            model.calendar_manager_selected = 2;
            snprintf(model.calendar_pending_option_id, sizeof(model.calendar_pending_option_id), "JP");
            for (int i = 0; i < 6; ++i) {
                snprintf(model.calendar_manager_rows[i].title, sizeof(model.calendar_manager_rows[i].title),
                    "JP-%d", 2026 + i);
                snprintf(model.calendar_manager_rows[i].detail, sizeof(model.calendar_manager_rows[i].detail),
                    "%s", ptc_ui_text(PTC_UI_T_CALENDAR_USER_IMPORT));
            }
            model.calendar_preview_count = 12;
            for (int i = 0; i < 12; ++i)
                snprintf(model.calendar_preview_lines[i], sizeof(model.calendar_preview_lines[i]),
                    "%s", ptc_ui_text(i == 0 ? PTC_UI_T_CALENDAR_BUILTIN_CHINA : PTC_UI_T_CALENDAR_UNCOVERED));
            model.draft_scheduled_override = (PtcScheduledOverride){true, 2380, 2386, {PTC_RULE_MODE_LIMIT, 120}};
            model.bedtime_policy = rules.bedtime;
            model.draft_bedtime_policy = rules.bedtime;
            model.redemption_history_available = model.activity_history_available = true;
            snprintf(model.overlay_title, sizeof(model.overlay_title), "%s", titles[overlay]);
            snprintf(model.numpad_title, sizeof(model.numpad_title), ptc_ui_text(PTC_UI_T_ENTER_THE_EXTRA_TIME_CODE));
            snprintf(model.numpad_text, sizeof(model.numpad_text), "1234");
            snprintf(model.pin_title, sizeof(model.pin_title), ptc_ui_text(PTC_UI_T_ENTER_PARENT_PIN));
            snprintf(model.pin_guide, sizeof(model.pin_guide), ptc_ui_text(PTC_UI_T_CONTINUE_CURRENT_OPERATION_AFTER_VERIFICATION));
            snprintf(model.pin_text, sizeof(model.pin_text), "1234");
            snprintf(model.auth_error_title, sizeof(model.auth_error_title), ptc_ui_text(PTC_UI_T_PIN_IS_INCORRECT));
            snprintf(model.auth_error_message, sizeof(model.auth_error_message), ptc_ui_text(PTC_UI_T_PLEASE_CHECK_AND_RE_ENTER));
            snprintf(model.pairing_base_url, sizeof(model.pairing_base_url), "%s", PTC_PAIRING_BASE_URL);
            model.demo_secret_enabled = true;
            if (!ptc_build_pairing_url("kid-switch", PTC_DEMO_GRANT_SECRET, model.pairing_url, sizeof(model.pairing_url)) ||
                !qrcodegen_encodeText(model.pairing_url, temp, model.qr_code,
                    qrcodegen_Ecc_LOW, 1, 40, qrcodegen_Mask_AUTO, true)) return 1;
            snprintf(name, sizeof(name), "matrix-overlay-%02d", overlay);
            failed |= save_preview(directory, "matrix", name, &model, dark != 0);
            if (overlay == PTC_UI_OVERLAY_CALENDAR_MANAGER) {
                model.calendar_manager_tab = 1;
                model.calendar_preview_count = 12;
                snprintf(model.calendar_preview_lines[0], sizeof(model.calendar_preview_lines[0]),
                    "JP (JP) - 2028");
                snprintf(model.calendar_preview_lines[1], sizeof(model.calendar_preview_lines[1]),
                    ptc_ui_text(PTC_UI_T_CALENDAR_DATES_SUMMARY), 14u, 2u);
                for (int i = 2; i < 12; ++i)
                    snprintf(model.calendar_preview_lines[i], sizeof(model.calendar_preview_lines[i]),
                        "2028-%02d-01 - 2028-%02d-02", i, i);
                for (int i = 0; i < 6; ++i) {
                    snprintf(model.calendar_manager_rows[i].title,
                        sizeof(model.calendar_manager_rows[i].title), "JP-%d.json", 2026 + i);
                    snprintf(model.calendar_manager_rows[i].detail,
                        sizeof(model.calendar_manager_rows[i].detail), "%s",
                        ptc_ui_text(PTC_UI_T_CALENDAR_USER_IMPORT));
                }
                failed |= save_preview(directory, "holiday", "calendar-import", &model, dark != 0);
                model.calendar_manager_count = model.calendar_preview_count = 0;
                failed |= save_preview(directory, "holiday", "calendar-import-empty", &model, dark != 0);
            }
        }
        for (int parent = 0; parent < 2; ++parent) {
            for (int state = 0; state < 6; ++state) {
                PtcUiModel model = *baseline;
                char name[48];
                model.view = parent ? PTC_UI_PARENT : PTC_UI_CHILD;
                if (state == 0) {
                    model.status_loaded = model.remaining_available = model.played_minutes_available = model.forecast_available = false;
                    snprintf(model.message, sizeof(model.message), ptc_ui_text(PTC_UI_T_WAITING_TO_READ_CONSOLE_STATUS));
                }
                if (state == 1) model.remaining_minutes = 0;
                if (state == 2) { model.unrestricted_today = 1; model.limited_today = 0; }
                if (state == 3) model.status_updated_at = 879;
                if (state == 4) { model.waiting = true; snprintf(model.message, sizeof(model.message), ptc_ui_text(PTC_UI_T_SYNCHRONIZING_PLEASE_WAIT)); }
                if (state == 5) { model.remaining_minutes = 1440; model.played_minutes = 0; model.forecast[0].minutes = 1440; }
                snprintf(name, sizeof(name), "matrix-%s-state-%d", parent ? "parent" : "child", state);
                failed |= save_preview(directory, "matrix", name, &model, dark != 0);
                ptc_ui_open_home_details(&model);
                if (model.overlay == PTC_UI_OVERLAY_HOME_DETAILS) {
                    snprintf(name, sizeof(name), "details-%s-state-%d", parent ? "parent" : "child", state);
                    failed |= save_preview(directory, "matrix", name, &model, dark != 0);
                }
            }
        }
        for (int state = 0; state < 6; ++state) {
            PtcUiModel details = *baseline;
            char name[64];
            details.view = PTC_UI_PARENT;
            details.parent_page = PTC_UI_PARENT_TODAY;
            if (state == 0) details.usage_summary_available = details.played_minutes_available = false;
            if (state == 1) details.disable_flag_present = true;
            if (state == 2) details.apply_pending_confirmation = true;
            if (state == 3) {
                details.usage_known_days_7 = details.usage_known_days_30 = 0;
                details.usage_consumed_minutes_7 = details.usage_consumed_minutes_30 = 0;
            }
            if (state == 4) snprintf(details.setup_phase, sizeof(details.setup_phase), "protection");
            if (state == 5) {
                details.disable_flag_present = true;
                snprintf(details.result_status, sizeof(details.result_status), "error");
                snprintf(details.message, sizeof(details.message), ptc_ui_text(PTC_UI_T_SETUP_INCOMPLETE_PLEASE_WAIT_FOR_STATUS_SYNC));
                snprintf(details.feedback_detail, sizeof(details.feedback_detail), ptc_ui_text(PTC_UI_T_DO_NOT_RESUBMIT_STATUS_REFRESH_AND_DIAGNOSTICS));
            }
            ptc_ui_open_home_details(&details);
            snprintf(name, sizeof(name), "details-special-%d", state);
            failed |= save_preview(directory, "matrix", name, &details, dark != 0);
        }
        PtcUiModel model = *baseline;
        model.view = PTC_UI_ERROR;
        model.error_code = 306;
        snprintf(model.message, sizeof(model.message), ptc_ui_text(PTC_UI_T_SYSTEM_ENVIRONMENT_CHANGED_PARENT_RE_CHECK_REQUIRED));
        failed |= save_preview(directory, "error", "error-page", &model, dark != 0);
    }
    return failed;
}

static int render_setup_previews(const char *directory, const PtcUiModel *baseline, bool dark)
{
    int failed = 0;
    PtcUiModel model = *baseline;
    model.view = PTC_UI_SETUP;
    snprintf(model.setup_phase, sizeof(model.setup_phase), "pending");
    model.message[0] = '\0';
    model.setup_pin_ready = true;
    model.environment_available = model.environment_atmosphere = true;
    snprintf(model.environment_hos, sizeof(model.environment_hos), "22.5.0");
    snprintf(model.environment_model, sizeof(model.environment_model), "mariko-oled");
    snprintf(model.environment_atmosphere_version, sizeof(model.environment_atmosphere_version), "1.11.2");
    for (int step = 1; step <= PTC_UI_SETUP_CONFIRM; ++step) {
        char name[32];
        model.setup_step = step;
        snprintf(name, sizeof(name), "setup-step-%d", step);
        failed |= save_preview(directory, "setup", name, &model, dark);
    }
    model.setup_step = PTC_UI_SETUP_PREPARE;
    model.setup_focus = 2;
    ptc_ui_open_setup_pctl_help(&model);
    failed |= save_preview(directory, "setup", "setup-pctl-help", &model, dark);
    ptc_ui_cancel_overlay(&model);
    model.setup_focus = 0;
    model.setup_time_help = true;
    model.restriction_enabled_available = false;
    model.environment_available = false;
    failed |= save_preview(directory, "setup", "setup-time-help", &model, dark);
    model.setup_step = PTC_UI_SETUP_PARENT;
    model.setup_more = true;
    model.setup_focus = 3;
    failed |= save_preview(directory, "setup", "setup-more", &model, dark);
    model.setup_step = PTC_UI_SETUP_CONFIRM;
    model.setup_pin_ready = model.status_loaded = false;
    ptc_ui_setup_record_issue(&model, PTC_UI_SETUP_ISSUE_PROGRESS, PTC_ERR_STORAGE_WRITE_FAILED);
    snprintf(model.message, sizeof(model.message), "%s", ptc_ui_text(PTC_UI_T_SETUP_SAVE_SESSION_ONLY));
    failed |= save_preview(directory, "setup", "setup-error", &model, dark);
    return failed;
}

static int render_support_guide_previews(const char *directory, const PtcUiModel *baseline, bool dark)
{
    int failed = 0;
    PtcUiModel guide = *baseline;
    guide.view = PTC_UI_PARENT;
    guide.parent_page = PTC_UI_PARENT_SUPPORT;
    ptc_ui_open_support_guide(&guide);
    for (int page = 0; page < 4; ++page) {
        char name[64];
        snprintf(name, sizeof(name), "support-guide-%d", page + 1);
        failed |= save_preview(directory, "support", name, &guide, dark);
        ptc_ui_support_guide_change_page(&guide, 1);
    }
    guide.restriction_enabled_available = false;
    failed |= save_preview(directory, "support", "support-guide-unknown", &guide, dark);
    guide.restriction_enabled_available = true;
    guide.restriction_enabled = false;
    failed |= save_preview(directory, "support", "support-guide-pctl-off", &guide, dark);
    guide.restriction_enabled = true;
    guide.status_updated_at = 879;
    failed |= save_preview(directory, "support", "support-guide-stale", &guide, dark);
    return failed;
}

/* Sample an unobstructed part of the actual convergence banner. This catches
 * the stale-state regression where all cards were unknown but the gate was green. */
static int check_rules_gate_color(uint32_t color)
{
    if (preview_pixels[580 * 1280 + 200] == resolve_color(color)) return 0;
    fprintf(stderr, "FAIL: rendered rule gate has the wrong semantic state\n");
    return 1;
}

static int render_today_details_previews(const char *directory, const PtcUiModel *baseline, bool dark)
{
    int failed = 0;
    PtcUiModel today = *baseline;
    today.view = PTC_UI_PARENT;
    today.overlay = PTC_UI_OVERLAY_HOME_DETAILS;
    today.home_details_preview_second = 16 * 3600;
    today.home_details_page = 0;
    today.home_details_focus = 0;
    today.dock_policy = (PtcDockPolicy){false, true, 30};
    today.dock_dirty = false; today.bedtime_dirty = false; today.eye_care_dirty = false;
    today.dock_available = today.undocked_usage_available = true;
    today.dock_status_available = today.bedtime_status_available = true;
    today.bedtime_status_enabled = true;
    today.undocked_used_minutes = 12; today.undocked_remaining_minutes = 18;
    today.dock_restriction_active = today.dock_waived_today = false;
    snprintf(today.operation_mode, sizeof(today.operation_mode), "undocked");
    today.eye_care_policy = (PtcEyeCarePolicy){true, 40, 10};
    today.eye_care_used_minutes = 25;
    snprintf(today.eye_care_phase, sizeof(today.eye_care_phase), "playing");
    today.bedtime_policy.enabled = true;
    for (int i = 0; i < 7; ++i)
        today.bedtime_policy.week[i] = (PtcBedtimeWindow){true, 21 * 60 + 30, 7 * 60};
    today.bedtime_active = today.bedtime_skipped = today.bedtime_skipped_window_available = false;
    today.bedtime_next_available = true;
    today.bedtime_next_start_day_index = today.day_index;
    today.bedtime_next_start_minute = 21 * 60 + 30;
    today.bedtime_next_end_minute = 7 * 60;
    today.bedtime_next_window_instance_id = ptc_bedtime_window_instance_id(today.day_index, 21 * 60 + 30);
    failed |= save_preview(directory, "parent", "today-planning", &today, dark);
    {
        PtcUiModel variant = today;
        snprintf(variant.operation_mode, sizeof(variant.operation_mode), "docked");
        failed |= save_preview(directory, "parent", "today-tv", &variant, dark);
        variant.forecast[0].mode = PTC_RULE_MODE_UNLIMITED;
        variant.current_week[ptc_weekday_from_day_index(variant.day_index)].mode = PTC_RULE_MODE_UNLIMITED;
        variant.unrestricted_today = 1;
        variant.daily_buffer_available = false;
        failed |= save_preview(directory, "parent", "today-unlimited-tv", &variant, dark);
        variant = today;
        snprintf(variant.eye_care_phase, sizeof(variant.eye_care_phase), "resting");
        variant.eye_care_rest_remaining_seconds = 600; variant.eye_care_break_id = 99;
        variant.remaining_minutes = 0; variant.restricted_now = variant.blocked_today = 1;
        failed |= save_preview(directory, "parent", "today-resting", &variant, dark);
        variant = today;
        variant.bedtime_skipped_window_available = true;
        variant.bedtime_skipped_window_instance_id = ptc_bedtime_window_instance_id(variant.day_index, 21 * 60 + 30);
        failed |= save_preview(directory, "parent", "today-skipped", &variant, dark);
        variant = today;
        variant.recovery_active = true;
        failed |= save_preview(directory, "parent", "today-recovery", &variant, dark);
        variant = today;
        variant.eye_care_policy.enabled = variant.bedtime_policy.enabled = variant.bedtime_status_enabled = false;
        variant.dock_policy.force_docked = variant.dock_policy.undocked_limit_enabled = false;
        variant.daily_buffer_minutes = 0; variant.daily_buffer_available = false;
        variant.usage_present = true; variant.usage_captured_at = variant.status_updated_at;
        variant.forecast[0].mode = PTC_RULE_MODE_UNLIMITED; variant.forecast[0].minutes = 0;
        failed |= save_preview(directory, "parent", "today-policies-off", &variant, dark);
        variant.status_updated_at = 879; variant.usage_captured_at = 879;
        failed |= save_preview(directory, "parent", "today-policies-off-stale", &variant, dark);
    }
    today.home_details_page = 1;
    failed |= save_preview(directory, "parent", "today-rules", &today, dark);
    failed |= check_rules_gate_color(UI_SUCCESS_SOFT);
    {
        PtcUiModel variant = today;
        variant.rules_dialog_open = true;
        failed |= save_preview(directory, "parent", "today-rules-explanation", &variant, dark);
        variant = today;
        variant.status_updated_at = 879;
        failed |= save_preview(directory, "parent", "today-rules-stale", &variant, dark);
        failed |= check_rules_gate_color(UI_WARNING_SOFT);
        variant = today;
        variant.disable_flag_present = true;
        variant.forecast[0].mode = PTC_RULE_MODE_UNLIMITED;
        variant.current_week[ptc_weekday_from_day_index(variant.day_index)].mode = PTC_RULE_MODE_UNLIMITED;
        variant.unrestricted_today = 1;
        variant.daily_buffer_available = false;
        variant.eye_care_policy.enabled = variant.bedtime_policy.enabled = false;
        variant.dock_policy.force_docked = variant.dock_policy.undocked_limit_enabled = false;
        failed |= save_preview(directory, "parent", "today-rules-control-off", &variant, dark);
        failed |= check_rules_gate_color(UI_WARNING_SOFT);
        variant = today;
        variant.home_details_page = 0;
        variant.forecast[0].mode = PTC_RULE_MODE_UNLIMITED;
        variant.current_week[ptc_weekday_from_day_index(variant.day_index)].mode = PTC_RULE_MODE_UNLIMITED;
        variant.unrestricted_today = 1;
        variant.daily_buffer_available = false;
        variant.eye_care_policy.play_minutes = variant.eye_care_policy.rest_minutes = 1;
        variant.eye_care_used_minutes = 0;
        failed |= save_preview(directory, "parent", "today-planning-dense", &variant, dark);
    }

    {
        PtcUiModel history = today;
        history.usage_direct_present = true;
        history.usage_direct_days_7 = 3; history.usage_direct_days_30 = 8;
        history.usage_direct_minutes_7 = 234; history.usage_direct_minutes_30 = 901;
        history.usage_latest_available = true;
        failed |= save_preview(directory, "parent", "history-mixed", &history, dark);
        history.usage_latest_available = false;
        failed |= save_preview(directory, "parent", "history-read-failed", &history, dark);
        history.usage_direct_days_7 = history.usage_direct_days_30 = 0;
        history.usage_direct_minutes_7 = history.usage_direct_minutes_30 = 0;
        history.usage_summary_available = false;
        failed |= save_preview(directory, "parent", "history-unknown", &history, dark);
    }
    today.home_details_data_expanded = true; today.home_details_scroll = 280;
    failed |= save_preview(directory, "parent", "today-data", &today, dark);
    today.home_details_focus = 6;
    failed |= save_preview(directory, "parent", "today-data-focus", &today, dark);
    today.home_details_focus = 0;
    today.view = PTC_UI_CHILD; today.home_details_scroll = 160;
    failed |= save_preview(directory, "parent", "today-child-data", &today, dark);
    today.view = PTC_UI_PARENT; today.home_details_page = 0;
    today.status_updated_at = 879;
    failed |= save_preview(directory, "parent", "today-stale", &today, dark);
    today.status_updated_at = 998;
    today.home_details_refresh_failed = true;
    failed |= save_preview(directory, "parent", "today-refresh-failed", &today, dark);
    today.home_details_refresh_failed = false;
    today.daily_restriction_active = today.bedtime_active = today.dock_restriction_active = true;
    today.today_override_present = true;
    today.today_override_rule = (PtcDayRule){PTC_RULE_MODE_LIMIT, 60};
    today.forecast[0].minutes = 60;
    today.forecast[0].rule_source = "today_override";
    snprintf(today.rule_source, sizeof(today.rule_source), "today_override");
    today.played_minutes = today.forecast[0].minutes;
    today.undocked_used_minutes = today.dock_policy.undocked_daily_minutes;
    today.undocked_remaining_minutes = 0;
    today.remaining_minutes = 0; today.restricted_now = today.blocked_today = 1;
    today.bedtime_start_minute = 16 * 60; today.bedtime_end_minute = 7 * 60;
    today.bedtime_policy.week[ptc_weekday_from_day_index(today.day_index)].start_minute = 16 * 60;
    today.bedtime_window_instance_id = ptc_bedtime_window_instance_id(today.day_index, 16 * 60);
    today.daily_buffer_available = false;
    snprintf(today.eye_care_phase, sizeof(today.eye_care_phase), "resting");
    today.eye_care_rest_remaining_seconds = 600; today.eye_care_break_id = 99;
    failed |= save_preview(directory, "parent", "today-multiple-limits", &today, dark);
    today.home_details_page = 1;
    failed |= save_preview(directory, "parent", "parent-details-rules-restricted", &today, dark);
    failed |= check_rules_gate_color(UI_DANGER_SOFT);
    today = *baseline;
    today.view = PTC_UI_PARENT;
    today.overlay = PTC_UI_OVERLAY_DAY_DECISION;
    today.forecast_detail_day_offset = 0;
    failed |= save_preview(directory, "parent", "date-decision-today", &today, dark);
    today.forecast_detail_day_offset = 1;
    failed |= save_preview(directory, "parent", "date-decision-tomorrow", &today, dark);
    return failed;
}

static int render_all_previews(const char *directory, const PtcUiModel *baseline_ptr)
{
    PtcUiModel model;
    int failed = 0;
    const PtcUiModel baseline = *baseline_ptr;

    for (int dark = 0; dark <= 1; ++dark) {
        model = baseline;
        model.view = PTC_UI_CHILD;
        failed |= check_child_home_grid(dark);
        failed |= save_preview(directory, "child", "child", &model, dark);
        {
            PtcUiModel claimed = model;
            claimed.daily_buffer_available = false;
            claimed.daily_buffer_claimed = true;
            failed |= save_preview(directory, "child", "child-buffer-claimed", &claimed, dark);
        }
        {
            PtcUiModel exhausted = model;
            exhausted.remaining_minutes = 0;
            exhausted.displayed_remaining_minutes = 0;
            exhausted.played_minutes = 120;
            exhausted.forecast[0].minutes = 120;
            exhausted.daily_restriction_active = true;
            exhausted.restricted_now = 1;
            exhausted.blocked_today = 1;
            exhausted.daily_buffer_available = false;
            failed |= save_preview(directory, "child", "child-exhausted", &exhausted, dark);
        }
        ptc_ui_open_home_details(&model);
        failed |= check_child_details_guide(dark);
        failed |= save_preview(directory, "child", "child-details", &model, dark);
        ptc_ui_cancel_overlay(&model);
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_TODAY;
        failed |= save_preview(directory, "parent", "parent", &model, dark);
        {
            PtcUiModel direct = model;
            direct.usage_present = true;
            direct.usage_captured_at = direct.status_updated_at;
            direct.usage_ns = UINT64_C(4191000000000);
            direct.played_minutes_available = true;
            direct.played_minutes = 69;
            direct.remaining_minutes = direct.displayed_remaining_minutes = 110;
            direct.forecast[0].minutes = 180;
            failed |= save_preview(directory, "parent", "direct-usage", &direct, dark);
            {
                PtcUiModel stale = direct;
                stale.usage_captured_at -= 121;
                failed |= save_preview(directory, "parent", "direct-usage-stale", &stale, dark);
            }
            direct.remaining_minutes = direct.displayed_remaining_minutes = 0;
            direct.forecast[0].minutes = 60;
            direct.daily_restriction_active = true;
            failed |= save_preview(directory, "parent", "direct-usage-overrun", &direct, dark);
            direct.overlay = PTC_UI_OVERLAY_CONFIRM;
            direct.operation = PTC_UI_OPERATION_ADD_TODAY_MINUTES;
            direct.draft_minutes = 15;
            direct.played_minutes = 100;
            direct.usage_ns = UINT64_C(6059000000000);
            snprintf(direct.overlay_title, sizeof(direct.overlay_title), ptc_ui_text(PTC_UI_T_CONFIRM_RAPID_GRANT));
            failed |= save_preview(directory, "parent", "direct-grant-overrun", &direct, dark);
            direct.forecast[0].minutes = 1435;
            direct.remaining_minutes = direct.displayed_remaining_minutes = 1334;
            failed |= save_preview(directory, "parent", "direct-grant-partial", &direct, dark);
            direct.forecast[0].minutes = 1440;
            direct.remaining_minutes = direct.displayed_remaining_minutes = 1339;
            failed |= save_preview(directory, "parent", "direct-grant-capped", &direct, dark);
        }
        model.today_override_present = true;
        model.today_override_rule = (PtcDayRule){PTC_RULE_MODE_LIMIT, 90};
        model.forecast[0].minutes = 90;
        model.forecast[0].rule_source = "today_override";
        snprintf(model.rule_source, sizeof(model.rule_source), "today_override");
        failed |= save_preview(directory, "parent", "today-active", &model, dark);
        model.today_override_present = false;
        model.today_override_cleared_in_session = true;
        model.forecast[0].minutes = 120;
        model.forecast[0].rule_source = "weekly";
        snprintf(model.rule_source, sizeof(model.rule_source), "weekly");
        failed |= save_preview(directory, "parent", "today-cleared", &model, dark);
        {
            PtcUiModel notice_model = baseline;
            notice_model.view = PTC_UI_PARENT;
            notice_model.parent_page = PTC_UI_PARENT_TODAY;
            notice_model.waiting = true;
            snprintf(notice_model.message, sizeof(notice_model.message), ptc_ui_text(PTC_UI_T_SYNCHRONIZING_PLEASE_WAIT));
            failed |= save_preview(directory, "parent", "parent-notice-waiting", &notice_model, dark);
            notice_model = baseline;
            notice_model.view = PTC_UI_PARENT;
            notice_model.parent_page = PTC_UI_PARENT_TODAY;
            notice_model.remaining_minutes = 0;
            failed |= save_preview(directory, "parent", "parent-notice-exhausted", &notice_model, dark);
            notice_model = baseline;
            notice_model.view = PTC_UI_PARENT;
            notice_model.parent_page = PTC_UI_PARENT_TODAY;
            snprintf(notice_model.result_status, sizeof(notice_model.result_status), "error");
            snprintf(notice_model.command_name, sizeof(notice_model.command_name), ptc_ui_text(PTC_UI_T_SET_TODAY_S_TOTAL_QUOTA));
            snprintf(notice_model.message, sizeof(notice_model.message), ptc_ui_text(PTC_UI_T_FAILED_TO_SAVE_TODAY_LIMIT_CHECK_AND));
            snprintf(notice_model.feedback_detail, sizeof(notice_model.feedback_detail),
                     ptc_ui_text(PTC_UI_T_DRAFT_RETAINED_REFRESH_STATUS_FIRST_OR_CHECK));
            failed |= save_preview(directory, "parent", "parent-notice-failed", &notice_model, dark);
            if (!ptc_ui_open_notice_details(&notice_model)) return 1;
            failed |= save_preview(directory, "parent", "parent-notice-details", &notice_model, dark);
        }
        model.today_override_cleared_in_session = false;
        model.bedtime_policy.enabled = true;
        model.bedtime_active = true;
        model.bedtime_window_instance_id = ptc_bedtime_window_instance_id(2380, 1260);
        model.bedtime_start_day_index = 2380;
        model.bedtime_start_minute = 1260;
        model.bedtime_end_minute = 420;
        snprintf(model.bedtime_source, sizeof(model.bedtime_source), "weekly");
        failed |= save_preview(directory, "parent", "bedtime-current", &model, dark);
        model.bedtime_active = false;
        model.bedtime_next_available = true;
        model.bedtime_next_window_instance_id = ptc_bedtime_window_instance_id(2380, 1260);
        model.bedtime_next_start_day_index = 2380;
        model.bedtime_next_start_minute = 1260;
        model.bedtime_next_end_minute = 420;
        failed |= save_preview(directory, "parent", "bedtime-tonight", &model, dark);
        model.bedtime_next_window_instance_id = ptc_bedtime_window_instance_id(2382, 1410);
        model.bedtime_next_start_day_index = 2382;
        model.bedtime_next_start_minute = 1410;
        model.bedtime_next_end_minute = 480;
        failed |= save_preview(directory, "parent", "bedtime-future", &model, dark);
        model.bedtime_next_available = false;
        model.bedtime_skipped_window_available = true;
        model.bedtime_skipped_window_instance_id = ptc_bedtime_window_instance_id(2380, 1260);
        model.bedtime_skipped_start_day_index = 2380;
        model.bedtime_skipped_start_minute = 1260;
        model.bedtime_skipped_end_minute = 420;
        snprintf(model.bedtime_skipped_source, sizeof(model.bedtime_skipped_source), "weekly");
        failed |= save_preview(directory, "parent", "bedtime-skipped", &model, dark);
        model.bedtime_policy.enabled = false;
        model.bedtime_skipped_window_available = false;
        failed |= save_preview(directory, "parent", "bedtime-none", &model, dark);
        model = baseline;
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_TODAY;
        ptc_ui_open_home_details(&model);
        failed |= save_preview(directory, "parent", "parent-details-today", &model, dark);
        {
            PtcUiModel active_details = model;
            active_details.bedtime_policy.enabled = true;
            active_details.bedtime_active = true;
            active_details.bedtime_skipped = false;
            active_details.bedtime_start_day_index = active_details.day_index;
            active_details.bedtime_start_minute = 1260;
            active_details.bedtime_end_minute = 420;
            snprintf(active_details.bedtime_source, sizeof(active_details.bedtime_source), "weekly");
            failed |= save_preview(directory, "parent", "parent-details-bedtime-active", &active_details, dark);
            active_details.home_details_page = 1;
            failed |= save_preview(directory, "parent", "parent-details-rules-bedtime-active", &active_details, dark);
        }
        model.home_details_page = 1;
        failed |= save_preview(directory, "parent", "parent-details-rules", &model, dark);
        failed |= render_today_details_previews(directory, &model, dark);

        ptc_ui_cancel_overlay(&model);
        model.parent_page = PTC_UI_PARENT_GRANT;
        failed |= save_preview(directory, "grant", "grant-entry", &model, dark);
        model.overlay = PTC_UI_OVERLAY_GRANT_LOCAL;
        model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
        model.grant_minutes = 20;
        model.grant_max_minutes = 240;
        model.grant_has_code = false;
        snprintf(model.overlay_title, sizeof(model.overlay_title), ptc_ui_text(PTC_UI_T_NATIVELY_GENERATES_8_BIT_PLUS_TIME_CODE));
        failed |= save_preview(directory, "grant", "grant-empty", &model, dark);
        model.overlay_selection = PTC_UI_GRANT_LOCAL_ADJUST_FIRST;
        ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_GRANT_MINUTES, PTC_UI_OVERLAY_GRANT_LOCAL,
            ptc_ui_text(PTC_UI_T_SET_CODE_DURATION), ptc_ui_text(PTC_UI_T_SELECT_A_VALID_TIER_SUPPORTED_BY_THE), 4, 1, 240, 20);
        failed |= save_preview(directory, "grant", "grant-duration-editor", &model, dark);
        ptc_ui_cancel_overlay(&model);
        model.grant_has_code = true;
        model.grant_issued_minutes = 20;
        model.grant_day_index = 2380;
        model.grant_estimate_available = true;
        model.grant_estimate_minutes = 71;
        model.grant_minutes = 240;
        snprintf(model.grant_code, sizeof(model.grant_code), "12345678");
        failed |= save_preview(directory, "grant", "grant-issued", &model, dark);
        model.played_minutes = 1430;
        model.remaining_minutes = 5;
        model.grant_estimate_minutes = 10;
        model.grant_estimate_capped = true;
        failed |= save_preview(directory, "grant", "grant-capped", &model, dark);
        model.status_updated_at = 879;
        failed |= save_preview(directory, "grant", "grant-stale", &model, dark);
        model.status_updated_at = 998;
        model.played_minutes = 69;
        model.remaining_minutes = 51;
        model.grant_estimate_minutes = 71;
        model.grant_estimate_capped = false;
        model.grant_status_refresh_failed = true;
        snprintf(model.grant_notice, sizeof(model.grant_notice), ptc_ui_text(PTC_UI_T_FAILED_TO_SAVE_PLEASE_GO_BACK_AND));
        failed |= save_preview(directory, "grant", "grant-error", &model, dark);
        model.grant_status_refresh_failed = false;
        model.grant_notice[0] = '\0';
        ptc_ui_cancel_overlay(&model);
        model.view = PTC_UI_CHILD;
        ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_OFFLINE_CODE, PTC_UI_OVERLAY_NONE,
            ptc_ui_text(PTC_UI_T_ENTER_THE_EXTRA_TIME_CODE), ptc_ui_text(PTC_UI_T_ENTER_THE_8_DIGIT_CODE_GIVEN_TO), 8, 0, 0, 0);
        failed |= save_preview(directory, "redeem", "redeem-input-empty", &model, dark);
        snprintf(model.numpad_text, sizeof(model.numpad_text), "123");
        failed |= save_preview(directory, "redeem", "redeem-input-partial", &model, dark);
        snprintf(model.numpad_text, sizeof(model.numpad_text), "12345678");
        failed |= save_preview(directory, "redeem", "redeem-input", &model, dark);
        ptc_ui_cancel_overlay(&model);
        model.confirm_hold_required = false;
        model.operation = PTC_UI_OPERATION_REDEEM_OFFLINE_CODE;
        model.overlay = PTC_UI_OVERLAY_CONFIRM;
        model.code_grant_minutes = 30;
        model.code_preview_after_available = true;
        model.code_preview_after_minutes = 56;
        model.code_effective_add_minutes = 5;
        model.code_preview_capped = true;
        snprintf(model.overlay_title, sizeof(model.overlay_title), ptc_ui_text(PTC_UI_T_CONFIRM_PLAYTIME_GRANT));
        failed |= save_preview(directory, "redeem", "redeem-confirm", &model, dark);
        model.confirm_hold_required = true;
        model.code_preview_after_minutes = 0;
        failed |= save_preview(directory, "redeem", "redeem-confirm-hold", &model, dark);
        model.confirm_hold_progress = 250;
        failed |= save_preview(directory, "redeem", "redeem-confirm-hold-progress", &model, dark);
        model.confirm_hold_progress = 0;
        model.code_preview_after_minutes = 56;
        model.overlay = PTC_UI_OVERLAY_CODE_RESULT;
        model.code_actual_add_available = true;
        model.code_actual_add_minutes = 5;
        model.remaining_minutes = 56;
        failed |= save_preview(directory, "redeem", "redeem-success-capped", &model, dark);
        model.code_actual_add_minutes = 30;
        failed |= save_preview(directory, "redeem", "redeem-success", &model, dark);
        model.status_updated_at = 880;
        failed |= save_preview(directory, "redeem", "redeem-age-120", &model, dark);
        model.status_updated_at = 879;
        failed |= save_preview(directory, "redeem", "redeem-old-result", &model, dark);
        model.status_updated_at = 998;
        model.code_actual_add_available = false;
        failed |= save_preview(directory, "redeem", "redeem-missing-record", &model, dark);
        model.code_result_pending = true;
        failed |= save_preview(directory, "redeem", "redeem-pending", &model, dark);
        model.code_result_pending = false;
        model.code_result_failed = true;
        snprintf(model.result_status, sizeof(model.result_status), "error");
        failed |= save_preview(directory, "redeem", "redeem-failed", &model, dark);
        model.error_code = 206;
        failed |= save_preview(directory, "redeem", "redeem-used", &model, dark);
        model.error_code = 205;
        failed |= save_preview(directory, "redeem", "redeem-wrong-date", &model, dark);
        model.error_code = 501;
        failed |= save_preview(directory, "redeem", "redeem-storage-error", &model, dark);
        model.error_code = 0;
        model.code_result_failed = false;
        snprintf(model.result_status, sizeof(model.result_status), "ok");
        model.remaining_minutes = 51;
        ptc_ui_cancel_overlay(&model);
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_TODAY;
        model.operation = PTC_UI_OPERATION_SET_TODAY_LIMIT;
        ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_MINUTES, PTC_UI_OVERLAY_NONE,
            ptc_ui_text(PTC_UI_T_SET_TODAY_S_TOTAL_QUOTA), ptc_ui_text(PTC_UI_T_THE_TOTAL_QUOTA_FOR_THE_WHOLE_DAY_3), 4, 1, 1440, 1440);
        failed |= save_preview(directory, "parent", "quota-editor", &model, dark);
        model.today_limit_unlimited_draft = true;
        failed |= save_preview(directory, "parent", "quota-unlimited-editor", &model, dark);
        model.today_limit_unlimited_draft = false;
        ptc_ui_cancel_overlay(&model);
        model.overlay = PTC_UI_OVERLAY_CONFIRM;
        model.confirm_hold_required = true;
        snprintf(model.overlay_title, sizeof(model.overlay_title), ptc_ui_text(PTC_UI_T_MAY_BE_RESTRICTED_IMMEDIATELY_AFTER_SETTING));
        snprintf(model.overlay_body, sizeof(model.overlay_body), ptc_ui_text(PTC_UI_T_TOTAL_DAILY_QUOTA_INCLUDES_PLAYTIME_ALREADY_USED));
        failed |= save_preview(directory, "parent", "confirmation", &model, dark);
        {
            PtcUiModel positive = model;
            positive.draft_minutes = 150;
            positive.confirm_hold_required = false;
            snprintf(positive.overlay_title, sizeof(positive.overlay_title), ptc_ui_text(PTC_UI_T_CONFIRM_TODAY_LIMIT));
            failed |= save_preview(directory, "parent", "quota-positive-confirm", &positive, dark);
            positive.quota_refresh_failed = true;
            failed |= save_preview(directory, "parent", "quota-refresh-failed", &positive, dark);

            PtcUiModel quick_add = model;
            quick_add.operation = PTC_UI_OPERATION_ADD_TODAY_MINUTES;
            quick_add.draft_minutes = 30;
            quick_add.confirm_hold_required = false;
            snprintf(quick_add.overlay_title, sizeof(quick_add.overlay_title), ptc_ui_text(PTC_UI_T_CONFIRM_RAPID_GRANT));
            snprintf(quick_add.overlay_body, sizeof(quick_add.overlay_body), ptc_ui_text(PTC_UI_T_VERIFY_REMAINING_TIME_BEFORE_INCREASING_TODAY_LIMIT));
            failed |= save_preview(directory, "parent", "quick-add-confirm", &quick_add, dark);

            PtcUiModel unlimited = model;
            unlimited.operation = PTC_UI_OPERATION_DISABLE_TODAY_LIMIT;
            unlimited.confirm_hold_required = false;
            snprintf(unlimited.overlay_title, sizeof(unlimited.overlay_title), ptc_ui_text(PTC_UI_T_CONFIRM_UNLIMITED_TODAY));
            snprintf(unlimited.overlay_body, sizeof(unlimited.overlay_body), ptc_ui_text(PTC_UI_T_NO_DAILY_PLAYTIME_LIMIT_FOR_TODAY));
            failed |= save_preview(directory, "parent", "unlimited-confirm", &unlimited, dark);

            PtcUiModel unchanged = model;
            PtcEffectiveRule current = ptc_ui_plan_rule(&unchanged, PTC_UI_PLAN_SAVED);
            unchanged.operation = PTC_UI_OPERATION_SET_TODAY_LIMIT;
            unchanged.draft_minutes = current.rule.minutes;
            unchanged.confirm_hold_required = false;
            snprintf(unchanged.overlay_title, sizeof(unchanged.overlay_title), ptc_ui_text(PTC_UI_T_CONFIRM_TODAY_LIMIT));
            snprintf(unchanged.overlay_body, sizeof(unchanged.overlay_body), ptc_ui_text(PTC_UI_T_INPUT_MATCHES_CURRENT_LIMIT));
            failed |= save_preview(directory, "parent", "quota-unchanged-confirm", &unchanged, dark);
        }
        ptc_ui_cancel_overlay(&model);
    }
    for (int dark = 0; dark <= 1; ++dark) {
        PtcRules rules;
        ptc_rules_default(&rules);
        model = baseline;
        model.view = PTC_UI_PARENT;
        memcpy(model.current_week, rules.week, sizeof(rules.week));
        memcpy(model.draft_week, rules.week, sizeof(rules.week));
        model.parent_page = PTC_UI_PARENT_PLAN;
        model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
        model.bedtime_policy = model.draft_bedtime_policy = rules.bedtime;
        model.bedtime_policy.enabled = model.draft_bedtime_policy.enabled = true;
        for (int day = 0; day < 7; ++day) {
            model.bedtime_policy.week[day] = model.draft_bedtime_policy.week[day] =
                (PtcBedtimeWindow){true, 1260, 420};
            model.forecast[day] = (PtcResultForecastDay){
                .day_index = (uint16_t)(2380 + day),
                .mode = PTC_RULE_MODE_LIMIT,
                .minutes = (uint16_t)(day == 0 ? 120 : day == 1 ? 90 : 60),
                .rule_source = day == 0 ? "today_override" : day == 1 ? "scheduled_override" : "weekly"
            };
        }
        failed |= save_preview(directory, "plan", "plan-root", &model, dark);
        model.plan_page = PTC_UI_PLAN_PAGE_WEEKLY;
        for (int slot = 0; slot < 7; ++slot)
            if (ptc_ui_weekday_for_display_slot(slot) == ptc_weekday_from_day_index(model.day_index)) model.weekly_grid_slot = slot;
        failed |= save_preview(directory, "plan", "plan-saved", &model, dark);
        {
            PtcUiModel confirmation = model;
            uint8_t other_day = (uint8_t)((ptc_weekday_from_day_index(confirmation.day_index) + 1) % 7);
            confirmation.draft_week[other_day].minutes = 90;
            confirmation.weekly_dirty = true;
            confirmation.operation = PTC_UI_OPERATION_SAVE_WEEKLY;
            confirmation.overlay = PTC_UI_OVERLAY_CONFIRM;
            confirmation.confirm_hold_required = false;
            snprintf(confirmation.overlay_title, sizeof(confirmation.overlay_title), ptc_ui_text(PTC_UI_T_CONFIRM_TO_SAVE_WEEKLY_PLAN));
            failed |= save_preview(directory, "plan", "weekly-confirm-other-day", &confirmation, dark);
        }
        model.draft_week[ptc_weekday_from_day_index(model.day_index)].minutes = 90;
        model.weekly_dirty = true;
        failed |= save_preview(directory, "plan", "plan-draft-today", &model, dark);
        {
            PtcUiModel panel_state = model;
            panel_state.status_updated_at = 879;
            failed |= save_preview(directory, "plan", "plan-draft-unknown", &panel_state, dark);
            panel_state = model;
            panel_state.waiting = true;
            failed |= save_preview(directory, "plan", "plan-draft-waiting", &panel_state, dark);
            panel_state = model;
            panel_state.draft_week[ptc_weekday_from_day_index(panel_state.day_index)].minutes = 30;
            failed |= save_preview(directory, "plan", "plan-draft-danger", &panel_state, dark);
        }
        {
            PtcUiModel confirmation = model;
            confirmation.operation = PTC_UI_OPERATION_SAVE_WEEKLY;
            confirmation.overlay = PTC_UI_OVERLAY_CONFIRM;
            confirmation.confirm_hold_required = false;
            snprintf(confirmation.overlay_title, sizeof(confirmation.overlay_title), ptc_ui_text(PTC_UI_T_WEEKLY_PLANS_WILL_AFFECT_TODAY));
            failed |= save_preview(directory, "plan", "weekly-confirm-change", &confirmation, dark);
        }
        model.scheduled_override = (PtcScheduledOverride){true, 2380, 2386, {PTC_RULE_MODE_LIMIT, 120}};
        snprintf(model.rule_source, sizeof(model.rule_source), "scheduled_override");
        model.selected_index = 4;
        failed |= save_preview(directory, "plan", "plan-covered", &model, dark);
        {
            PtcUiModel confirmation = model;
            confirmation.operation = PTC_UI_OPERATION_SAVE_WEEKLY;
            confirmation.overlay = PTC_UI_OVERLAY_CONFIRM;
            confirmation.confirm_hold_required = false;
            snprintf(confirmation.overlay_title, sizeof(confirmation.overlay_title), ptc_ui_text(PTC_UI_T_CONFIRM_TO_SAVE_WEEKLY_PLAN));
            failed |= save_preview(directory, "plan", "weekly-confirm-covered", &confirmation, dark);
        }
        {
            PtcUiModel confirmation = model;
            confirmation.scheduled_override.enabled = false;
            snprintf(confirmation.rule_source, sizeof(confirmation.rule_source), "weekly");
            confirmation.draft_week[ptc_weekday_from_day_index(confirmation.day_index)].minutes = 30;
            confirmation.operation = PTC_UI_OPERATION_SAVE_WEEKLY;
            confirmation.overlay = PTC_UI_OVERLAY_CONFIRM;
            confirmation.confirm_hold_required = true;
            snprintf(confirmation.overlay_title, sizeof(confirmation.overlay_title), ptc_ui_text(PTC_UI_T_WEEKLY_PLANS_WILL_AFFECT_TODAY));
            failed |= save_preview(directory, "plan", "weekly-confirm-danger", &confirmation, dark);
            confirmation.status_updated_at = 879;
            failed |= save_preview(directory, "plan", "weekly-confirm-unknown", &confirmation, dark);
        }
        model.editor_index = ptc_weekday_from_day_index(model.day_index);
        ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_WEEKLY_MINUTES, PTC_UI_OVERLAY_NONE,
            ptc_ui_text(PTC_UI_T_ADJUST_WEEKLY_PLAN_LIMITS), ptc_ui_text(PTC_UI_T_UPDATES_DRAFT_ON_INPUT_APPLIED_ONLY_AFTER), 4, 1, 1440, 90);
        failed |= save_preview(directory, "plan", "plan-minute-editor", &model, dark);
        ptc_ui_cancel_overlay(&model);
        {
            PtcUiModel friday_editor = model;
            if (!ptc_day_index_from_date(2026, 9, 18, &friday_editor.day_index)) return 1;
            friday_editor.scheduled_override.enabled = false;
            friday_editor.editor_index = 5;
            memcpy(friday_editor.draft_week, friday_editor.current_week, sizeof(friday_editor.draft_week));
            friday_editor.current_week[5] = (PtcDayRule){PTC_RULE_MODE_LIMIT, 120};
            friday_editor.draft_week[5] = friday_editor.current_week[5];
            snprintf(friday_editor.rule_source, sizeof(friday_editor.rule_source), "weekly");
            ptc_ui_numpad_open(&friday_editor, PTC_UI_NUMPAD_WEEKLY_MINUTES, PTC_UI_OVERLAY_NONE,
                ptc_ui_text(PTC_UI_T_SET_FRIDAY_LIMIT), ptc_ui_text(PTC_UI_T_UPDATES_DRAFT_ON_INPUT_APPLIED_ONLY_AFTER), 4, 1, 1440, 90);
            failed |= save_preview(directory, "plan", "weekly-friday-duration-editor", &friday_editor, dark);
        }
        snprintf(model.result_status, sizeof(model.result_status), "error");
        snprintf(model.message, sizeof(model.message), ptc_ui_text(PTC_UI_T_FAILED_TO_SAVE_PLAN_CHANGES_RETAINED_CHECK));
        model.error_code = 501;
        failed |= save_preview(directory, "plan", "plan-failed", &model, dark);
        model.error_code = 0;
        snprintf(model.result_status, sizeof(model.result_status), "ok");
        snprintf(model.message, sizeof(model.message), ptc_ui_text(PTC_UI_T_SAVED_PLAN_LOADED));
        model.parent_page = PTC_UI_PARENT_PLAN;
        model.plan_page = PTC_UI_PLAN_PAGE_HOLIDAY;
        model.holiday_rule = model.draft_holiday_rule = rules.holiday_rule;
        model.makeup_workday_rule = model.draft_makeup_workday_rule = rules.makeup_workday_rule;
        model.draft_holiday_enabled = true;
        model.holiday_dirty = true;
        model.selected_index = 1;
        failed |= save_preview(directory, "holiday", "holiday-draft", &model, dark);
        {
            PtcUiModel panel_state = model;
            panel_state.waiting = true;
            failed |= save_preview(directory, "holiday", "holiday-waiting", &panel_state, dark);
            panel_state = model;
            snprintf(panel_state.result_status, sizeof(panel_state.result_status), "error");
            snprintf(panel_state.message, sizeof(panel_state.message), ptc_ui_text(PTC_UI_T_SAVE_FAILED_DRAFT_RETAINED_CHECK_AND_RETRY));
            panel_state.error_code = 501;
            failed |= save_preview(directory, "holiday", "holiday-failed", &panel_state, dark);
        }
        {
            PtcUiModel makeup_editor = model;
            if (!ptc_day_index_from_date(2026, 9, 20, &makeup_editor.day_index)) return 1;
            makeup_editor.holiday_enabled = makeup_editor.draft_holiday_enabled = true;
            makeup_editor.holiday_rule = makeup_editor.draft_holiday_rule;
            makeup_editor.makeup_workday_rule = makeup_editor.draft_makeup_workday_rule;
            makeup_editor.current_week[ptc_weekday_from_day_index(makeup_editor.day_index)] =
                (PtcDayRule){PTC_RULE_MODE_LIMIT, 120};
            makeup_editor.draft_week[ptc_weekday_from_day_index(makeup_editor.day_index)] =
                makeup_editor.current_week[ptc_weekday_from_day_index(makeup_editor.day_index)];
            ptc_ui_numpad_open(&makeup_editor, PTC_UI_NUMPAD_MAKEUP_MINUTES, PTC_UI_OVERLAY_NONE,
                ptc_ui_text(PTC_UI_T_SET_THE_AMOUNT_OF_REST_WORK_DAYS), ptc_ui_text(PTC_UI_T_UPDATES_DRAFT_ON_INPUT_APPLIED_ONLY_AFTER), 4, 1, 1440, 75);
            failed |= save_preview(directory, "holiday", "holiday-makeup-duration-editor", &makeup_editor, dark);
        }
        {
            PtcUiModel confirmation = model;
            if (!ptc_day_index_from_date(2026, 1, 1, &confirmation.day_index)) return 1;
            confirmation.scheduled_override.enabled = false;
            confirmation.holiday_enabled = false;
            confirmation.draft_holiday_enabled = true;
            confirmation.current_week[ptc_weekday_from_day_index(confirmation.day_index)] =
                (PtcDayRule){PTC_RULE_MODE_LIMIT, 120};
            confirmation.draft_week[ptc_weekday_from_day_index(confirmation.day_index)] =
                confirmation.current_week[ptc_weekday_from_day_index(confirmation.day_index)];
            confirmation.operation = PTC_UI_OPERATION_SAVE_HOLIDAY;
            confirmation.overlay = PTC_UI_OVERLAY_CONFIRM;
            confirmation.confirm_hold_required = false;
            snprintf(confirmation.overlay_title, sizeof(confirmation.overlay_title),
                     ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAY_SETTING_WILL_AFFECT_TODAY));
            failed |= save_preview(directory, "holiday", "holiday-confirm", &confirmation, dark);
        }
        model.disable_flag_present = true;
        failed |= save_preview(directory, "holiday", "holiday-disabled", &model, dark);
        model.disable_flag_present = false;
        model.plan_page = PTC_UI_PLAN_PAGE_BEDTIME;
        model.draft_bedtime_policy.enabled = true;
        model.draft_bedtime_policy.week[1] = (PtcBedtimeWindow){true, 1327, 420};
        model.bedtime_dirty = true;
        for (int section = PTC_UI_BEDTIME_WEEKLY; section <= PTC_UI_BEDTIME_SCHEDULED; ++section) {
            char name[48];
            model.bedtime_section = (PtcUiBedtimeSection)section;
            model.selected_index = 0;
            snprintf(name, sizeof(name), "bedtime-section-%d", section);
            failed |= save_preview(directory, "bedtime", name, &model, dark);
        }
        {
            PtcUiModel pending_off = model;
            pending_off.bedtime_section = PTC_UI_BEDTIME_WEEKLY;
            pending_off.draft_bedtime_policy.enabled = false;
            pending_off.bedtime_dirty = true;
            failed |= save_preview(directory, "bedtime", "bedtime-master-pending-off", &pending_off, dark);
        }
        {
            PtcUiModel danger = model;
            int64_t preview_now = ptc_ui_render_now();
            uint16_t preview_minute = ptc_ui_render_minute_of_day(preview_now);
            PtcBedtimeWindow danger_window = {true, 1260, 600};
            /* Keep the preview inside a valid bedtime window at any build time. */
            if (preview_minute >= 600 && preview_minute < 1260)
                danger_window = (PtcBedtimeWindow){true, (uint16_t)(preview_minute - 1), 480};
            danger.draft_bedtime_policy.enabled = true;
            danger.draft_bedtime_policy.calendar_enabled = false;
            danger.draft_bedtime_policy.scheduled_override.present = false;
            for (int day = 0; day < 7; ++day)
                danger.draft_bedtime_policy.week[day] = danger_window;
            danger.bedtime_dirty = true;
            danger.bedtime_section = PTC_UI_BEDTIME_WEEKLY;
            danger.selected_index = 10;
            danger.status_loaded = true;
            danger.status_updated_at = (int64_t)preview_now;
            danger.bedtime_active = false;
            danger.bedtime_skipped = false;
            if (ptc_ui_bedtime_save_impact(&danger, preview_minute, (int64_t)preview_now) !=
                PTC_UI_BEDTIME_IMPACT_RESTRICT) return 1;
            failed |= save_preview(directory, "bedtime", "bedtime-save-danger", &danger, dark);
            danger.overlay = PTC_UI_OVERLAY_CONFIRM;
            danger.operation = PTC_UI_OPERATION_SAVE_BEDTIME;
            danger.confirm_hold_required = true;
            danger.overlay_selection = 1;
            snprintf(danger.overlay_title, sizeof(danger.overlay_title), ptc_ui_text(PTC_UI_T_ENTER_BEDTIME_RESTRICTION_NOW));
            snprintf(danger.overlay_body, sizeof(danger.overlay_body),
                ptc_ui_text(PTC_UI_T_THE_CURRENT_TIME_IS_INSIDE_THE_BEDTIME));
            failed |= save_preview(directory, "bedtime", "bedtime-save-danger-confirm", &danger, dark);
        }
        snprintf(model.result_status, sizeof(model.result_status), "error");
        snprintf(model.command_name, sizeof(model.command_name), ptc_ui_text(PTC_UI_T_SAVE_BEDTIME_SCHEDULE));
        snprintf(model.message, sizeof(model.message), ptc_ui_text(PTC_UI_T_SAVE_FAILED_DRAFT_RETAINED_CHECK_AND_RETRY));
        model.error_code = 501;
        failed |= save_preview(directory, "bedtime", "bedtime-feedback-error", &model, dark);
        model.error_code = 0;
        snprintf(model.result_status, sizeof(model.result_status), "ok");
        snprintf(model.command_name, sizeof(model.command_name), ptc_ui_text(PTC_UI_T_REFRESH_STATUS));
        snprintf(model.message, sizeof(model.message), ptc_ui_text(PTC_UI_T_SAVED_PLAN_LOADED));
        model.overlay = PTC_UI_OVERLAY_BEDTIME_WINDOW;
        model.bedtime_editor_day = 1;
        model.overlay_selection = 1;
        snprintf(model.overlay_title, sizeof(model.overlay_title), ptc_ui_text(PTC_UI_T_EDIT_WEEKLY_BEDTIME_WINDOWS));
        snprintf(model.overlay_body, sizeof(model.overlay_body), "%s", ptc_ui_text(PTC_UI_T_SET_THE_NO_PLAY_PERIOD_FROM_THAT));
        failed |= save_preview(directory, "bedtime", "bedtime-window-input", &model, dark);
        model.overlay = PTC_UI_OVERLAY_BEDTIME_SPECIAL;
        model.bedtime_special_kind = 2;
        model.overlay_selection = 0;
        snprintf(model.overlay_title, sizeof(model.overlay_title), ptc_ui_text(PTC_UI_T_EDIT_SPECIFIC_DATE_BEDTIME_RULES));
        snprintf(model.overlay_body, sizeof(model.overlay_body), "%s", ptc_ui_text(PTC_UI_T_SELECT_INHERIT_CLOSE_OR_CUSTOM_CUSTOM_WINDOWS));
        model.draft_bedtime_policy.scheduled_override.rule.mode = PTC_BEDTIME_OVERRIDE_INHERIT;
        failed |= save_preview(directory, "bedtime", "bedtime-special-inherit", &model, dark);
        model.draft_bedtime_policy.scheduled_override.rule.mode = PTC_BEDTIME_OVERRIDE_DISABLED;
        failed |= save_preview(directory, "bedtime", "bedtime-special-closed", &model, dark);
        model.draft_bedtime_policy.scheduled_override.rule.mode = PTC_BEDTIME_OVERRIDE_CUSTOM;
        model.draft_bedtime_policy.scheduled_override.rule.window = (PtcBedtimeWindow){true, 1350, 450};
        model.overlay_selection = 1;
        failed |= save_preview(directory, "bedtime", "bedtime-special-custom", &model, dark);
        model.overlay = PTC_UI_OVERLAY_BEDTIME_WINDOW;
        model.overlay_selection = 1;
        ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_BEDTIME_TIME, PTC_UI_OVERLAY_BEDTIME_WINDOW,
            ptc_ui_text(PTC_UI_T_SET_BEDTIME_START_TIME), ptc_ui_text(PTC_UI_T_SELECT_HOURS_OR_MINUTES_ADJUST_THE_RIGHT), 4, 0, 1439, 1327);
        failed |= save_preview(directory, "bedtime", "bedtime-time-editor", &model, dark);
        ptc_ui_cancel_overlay(&model);
        ptc_ui_cancel_overlay(&model);
        {
            PtcUiModel autonomy = baseline;
            autonomy.view = PTC_UI_PARENT;
            autonomy.overlay = PTC_UI_OVERLAY_AUTONOMY;
            autonomy.overlay_selection = 2;
            autonomy.draft_autonomy_policy.daily_buffer_minutes = 10;
            snprintf(autonomy.overlay_title, sizeof(autonomy.overlay_title), ptc_ui_text(PTC_UI_T_TODAY_S_INDEPENDENT_BUFFERING));
            snprintf(autonomy.overlay_body, sizeof(autonomy.overlay_body),
                     ptc_ui_text(PTC_UI_T_CHILD_CAN_CLAIM_ONCE_DAILY_ON_LIMITED));
            failed |= save_preview(directory, "autonomy", "autonomy-policy", &autonomy, dark);
        }
        model.parent_page = PTC_UI_PARENT_PLAN;
        model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
        model.draft_scheduled_override = model.scheduled_override;
        model.draft_scheduled_override.rule.minutes = 90;
        model.overlay = PTC_UI_OVERLAY_SCHEDULED;
        model.overlay_selection = 1;
        snprintf(model.overlay_title, sizeof(model.overlay_title), ptc_ui_text(PTC_UI_T_SPECIFIED_DATE_QUOTA));
        snprintf(model.overlay_body, sizeof(model.overlay_body), ptc_ui_text(PTC_UI_T_SCHEDULE_DAILY_LIMITS_FOR_A_DATE_RANGE));
        failed |= save_preview(directory, "scheduled", "scheduled-draft", &model, dark);
        ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_SCHEDULED_MINUTES, PTC_UI_OVERLAY_SCHEDULED,
            ptc_ui_text(PTC_UI_T_SET_THE_QUOTA_FOR_THE_SPECIFIED_DATE), ptc_ui_text(PTC_UI_T_ENTER_HOURS_AND_MINUTES_SEPARATELY), 4, 1, 1440, 90);
        failed |= save_preview(directory, "scheduled", "scheduled-duration-editor", &model, dark);
        ptc_ui_cancel_overlay(&model);
        model.draft_scheduled_override.end_day_index = 2745;
        model.draft_scheduled_override.rule.mode = PTC_RULE_MODE_UNLIMITED;
        failed |= save_preview(directory, "scheduled", "scheduled-long", &model, dark);
        ptc_ui_cancel_overlay(&model);
        failed |= save_preview(directory, "scheduled", "scheduled-leave", &model, dark);
        ptc_ui_cancel_overlay(&model);
        model.error_code = 501;
        snprintf(model.result_status, sizeof(model.result_status), "error");
        failed |= save_preview(directory, "scheduled", "scheduled-failed", &model, dark);
        model = baseline;
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_SETTINGS;
        model.selected_index = 3;
        failed |= save_preview(directory, "settings", "settings-root", &model, dark);
        {
            PtcUiModel settings_dlg = model;
            settings_dlg.overlay = PTC_UI_OVERLAY_PIN;
            snprintf(settings_dlg.pin_title, sizeof(settings_dlg.pin_title), "%s", ptc_ui_text(PTC_UI_T_CHANGE_PIN));
            snprintf(settings_dlg.pin_guide, sizeof(settings_dlg.pin_guide), "%s", ptc_ui_text(PTC_UI_T_CONTINUE_CURRENT_OPERATION_AFTER_VERIFICATION));
            snprintf(settings_dlg.pin_text, sizeof(settings_dlg.pin_text), "1234");
            failed |= save_preview(directory, "settings", "settings-pin", &settings_dlg, dark);

            settings_dlg = model;
            settings_dlg.overlay = PTC_UI_OVERLAY_THEME;
            settings_dlg.overlay_selection = 3;
            /* 单选草稿：模式与风格各有一项选中；预览按已保存状态绘制，不显示待保存提示。 */
            settings_dlg.theme_draft_valid = true;
            settings_dlg.theme_draft_preference = dark ? PTC_UI_THEME_DARK : PTC_UI_THEME_LIGHT;
            settings_dlg.theme_draft_style = PTC_UI_THEME_STYLE_SLATE;
            settings_dlg.theme_saved_preference = settings_dlg.theme_draft_preference;
            settings_dlg.theme_saved_style = settings_dlg.theme_draft_style;
            snprintf(settings_dlg.overlay_title, sizeof(settings_dlg.overlay_title), "%s", ptc_ui_text(PTC_UI_T_APPEARANCE_THEME));
            failed |= save_preview(directory, "settings", "settings-theme", &settings_dlg, dark);

            settings_dlg = model;
            settings_dlg.overlay = PTC_UI_OVERLAY_LANGUAGE;
            settings_dlg.overlay_selection = 0;
            snprintf(settings_dlg.overlay_title, sizeof(settings_dlg.overlay_title), "%s", ptc_ui_text(PTC_UI_T_UI_LANGUAGE));
            failed |= save_preview(directory, "settings", "settings-language", &settings_dlg, dark);

            settings_dlg = model;
            settings_dlg.overlay = PTC_UI_OVERLAY_SHORTCUT_MANAGER;
            settings_dlg.setup_shortcut_index = 0;
            settings_dlg.shortcut_draft_enabled = true;
            settings_dlg.shortcut_draft_show_hint = true;
            snprintf(settings_dlg.shortcut_draft_label, sizeof(settings_dlg.shortcut_draft_label), "Minus");
            snprintf(settings_dlg.overlay_title, sizeof(settings_dlg.overlay_title), "%s", ptc_ui_text(PTC_UI_T_PARENT_SHORTCUT));
            failed |= save_preview(directory, "settings", "settings-shortcut", &settings_dlg, dark);

            settings_dlg = model;
            settings_dlg.overlay = PTC_UI_OVERLAY_ALBUM_MANAGER;
            settings_dlg.album_restriction_state = 1;
            snprintf(settings_dlg.overlay_title, sizeof(settings_dlg.overlay_title), "%s", ptc_ui_text(PTC_UI_T_HOMEBREW_ACCESS));
            failed |= save_preview(directory, "settings", "settings-album", &settings_dlg, dark);

            settings_dlg = model;
            settings_dlg.overlay = PTC_UI_OVERLAY_ACTIVITY_HISTORY;
            settings_dlg.activity_history_available = true;
            settings_dlg.activity_history_count = 3;
            settings_dlg.activity_history[0] = (PtcActivityHistoryRecord){.occurred_at = 998, .day_index = 2380, .action = "today_limit", .minutes = 120, .effective_minutes = 120};
            settings_dlg.activity_history[1] = (PtcActivityHistoryRecord){.occurred_at = 950, .day_index = 2380, .action = "offline_grant", .minutes = 30, .effective_minutes = 30};
            settings_dlg.activity_history[2] = (PtcActivityHistoryRecord){.occurred_at = 900, .day_index = 2380, .action = "today_unlimited", .minutes = 0, .effective_minutes = 0};
            snprintf(settings_dlg.overlay_title, sizeof(settings_dlg.overlay_title), "%s", ptc_ui_text(PTC_UI_T_FAMILY_ACTIVITIES));
            failed |= save_preview(directory, "settings", "settings-activity", &settings_dlg, dark);

            settings_dlg = model;
            settings_dlg.overlay = PTC_UI_OVERLAY_REDEMPTION_HISTORY;
            settings_dlg.redemption_history_available = true;
            settings_dlg.redemption_history_count = 2;
            settings_dlg.redemption_history[0] = (PtcRedemptionHistoryRecord){.redeemed_at = 998, .day_index = 2380, .token_version = 1, .grant_minutes = 30, .effective_add_minutes = 30, .remaining_after_available = true, .remaining_after_minutes = 60};
            settings_dlg.redemption_history[1] = (PtcRedemptionHistoryRecord){.redeemed_at = 950, .day_index = 2379, .token_version = 1, .grant_minutes = 60, .effective_add_minutes = 45, .remaining_after_available = true, .remaining_after_minutes = 45};
            snprintf(settings_dlg.overlay_title, sizeof(settings_dlg.overlay_title), "%s", ptc_ui_text(PTC_UI_T_USAGE_HISTORY));
            failed |= save_preview(directory, "settings", "settings-redemption", &settings_dlg, dark);

            settings_dlg = model;
            settings_dlg.overlay = PTC_UI_OVERLAY_GRANT_MANAGER;
            snprintf(settings_dlg.overlay_title, sizeof(settings_dlg.overlay_title), "%s", ptc_ui_text(PTC_UI_T_GRANT_SETTINGS));
            failed |= save_preview(directory, "grant", "grant-manager", &settings_dlg, dark);

            settings_dlg = model;
            settings_dlg.overlay = PTC_UI_OVERLAY_SOFTWARE_INFO;
            snprintf(settings_dlg.software_version, sizeof(settings_dlg.software_version), "v0.9.0");
            snprintf(settings_dlg.app_release_id, sizeof(settings_dlg.app_release_id), "playwise-nro-release");
            snprintf(settings_dlg.backend_release_id, sizeof(settings_dlg.backend_release_id), "playwise-sysmodule-release");
            snprintf(settings_dlg.repository_url, sizeof(settings_dlg.repository_url), "https://github.com/selfuppen/NX-PlayWise");
            snprintf(settings_dlg.pwa_url, sizeof(settings_dlg.pwa_url), "https://selfuppen.github.io/NX-PlayWise/");
            settings_dlg.hot_reload_status = PTC_UI_HOT_RELOAD_CURRENT;
            snprintf(settings_dlg.overlay_title, sizeof(settings_dlg.overlay_title), "%s", ptc_ui_text(PTC_UI_T_SOFTWARE_INFO));
            failed |= save_preview(directory, "settings", "settings-software-info", &settings_dlg, dark);
            settings_dlg.overlay = PTC_UI_OVERLAY_ONLINE_UPDATE;
            settings_dlg.update_check_on_startup = true;
            settings_dlg.update_status = 3;
            snprintf(settings_dlg.update_installed_version, sizeof(settings_dlg.update_installed_version), "3.1.2-alpha");
            snprintf(settings_dlg.update_version, sizeof(settings_dlg.update_version), "3.2.0-beta.10");
            {
                PtcUiModel badge = baseline;
                badge.view = PTC_UI_CHILD; badge.overlay = PTC_UI_OVERLAY_NONE; badge.update_status = 3;
                strcpy(badge.update_version, "3.2.0-beta.10");
                badge.draft_eye_care_policy = badge.eye_care_policy;
                failed |= save_preview(directory, "child", "child-update-available", &badge, dark);
                badge.view = PTC_UI_PARENT; badge.parent_page = PTC_UI_PARENT_TODAY;
                failed |= save_preview(directory, "parent", "parent-update-available", &badge, dark);
            }
            snprintf(settings_dlg.update_notes, sizeof(settings_dlg.update_notes), "%s", ptc_ui_text(PTC_UI_T_UPDATE_TRUST));
            failed |= save_preview(directory, "settings", "settings-online-update", &settings_dlg, dark);
            settings_dlg.update_notes_open = true;
            settings_dlg.update_notes[0] = '\0';
            for (int note = 0; note < 28; ++note) {
                const char *line = ptc_ui_text(PTC_UI_T_UPDATE_TRUST);
                size_t n = strlen(settings_dlg.update_notes), length = strlen(line);
                if (n + length + 2 >= sizeof(settings_dlg.update_notes)) break;
                memcpy(settings_dlg.update_notes + n, line, length);
                settings_dlg.update_notes[n + length] = '\n'; settings_dlg.update_notes[n + length + 1] = '\0';
            }
            failed |= save_preview(directory, "settings", "settings-online-update-notes", &settings_dlg, dark);
            settings_dlg.update_notes_page = 1;
            failed |= save_preview(directory, "settings", "settings-online-update-notes-next", &settings_dlg, dark);
            settings_dlg.update_notes_open = false;
            snprintf(settings_dlg.update_notes, sizeof(settings_dlg.update_notes), "%s", ptc_ui_text(PTC_UI_T_UPDATE_TRUST));
            settings_dlg.update_busy = true; settings_dlg.update_status = 6; settings_dlg.update_received_kib = 2048;
            failed |= save_preview(directory, "settings", "settings-online-update-downloading", &settings_dlg, dark);
            settings_dlg.update_busy = false; settings_dlg.update_status = 14;
            failed |= save_preview(directory, "settings", "settings-online-update-recovery", &settings_dlg, dark);
            settings_dlg.overlay = PTC_UI_OVERLAY_UPDATE_BACKUPS;
            settings_dlg.update_backup_count = 2;
            snprintf(settings_dlg.update_backup_labels[0], sizeof(settings_dlg.update_backup_labels[0]), "2026-10-28 12:00 / %s\nplaywise-3.1.2-alpha+0123456789ab", ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_VERIFIED));
            snprintf(settings_dlg.update_backup_labels[1], sizeof(settings_dlg.update_backup_labels[1]), "2026-10-27 12:00 / %s\nplaywise-3.1.1-beta.1+abcdef012345", ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_VERIFIED));
            failed |= save_preview(directory, "settings", "settings-update-backups", &settings_dlg, dark);
            settings_dlg.overlay = PTC_UI_OVERLAY_SOFTWARE_INFO;
            settings_dlg.hot_reload_status = PTC_UI_HOT_RELOAD_PENDING;
            settings_dlg.hot_reload_capture_restart = true;
            snprintf(settings_dlg.backend_release_id, sizeof(settings_dlg.backend_release_id), "%s", settings_dlg.app_release_id);
            snprintf(settings_dlg.hot_reload_detail, sizeof(settings_dlg.hot_reload_detail), "%s", ptc_ui_text(PTC_UI_T_CAPTURE_RESTART_READY));
            failed |= save_preview(directory, "settings", "settings-capture-restart", &settings_dlg, dark);
            settings_dlg.overlay = PTC_UI_OVERLAY_CONFIRM;
            settings_dlg.operation = PTC_UI_OPERATION_HOT_RELOAD;
            settings_dlg.overlay_selection = 1;
            snprintf(settings_dlg.overlay_title, sizeof(settings_dlg.overlay_title), "%s", ptc_ui_text(PTC_UI_T_CAPTURE_RESTART_TITLE));
            snprintf(settings_dlg.overlay_body, sizeof(settings_dlg.overlay_body), "%s", ptc_ui_text(PTC_UI_T_CAPTURE_RESTART_BODY));
            failed |= save_preview(directory, "settings", "settings-capture-restart-confirm", &settings_dlg, dark);
        }
        model.parent_page = PTC_UI_PARENT_SUPPORT;
        model.selected_index = 4;
        failed |= save_preview(directory, "support", "support-healthy", &model, dark);
        failed |= render_support_guide_previews(directory, &model, dark);
        snprintf(model.setup_phase, sizeof(model.setup_phase), "pending");
        failed |= save_preview(directory, "support", "support-incomplete", &model, dark);
        snprintf(model.setup_phase, sizeof(model.setup_phase), "active");
        model.disable_flag_present = true;
        model.selected_index = 0;
        failed |= save_preview(directory, "support", "support-disabled", &model, dark);
        snprintf(model.setup_phase, sizeof(model.setup_phase), "failed");
        model.selected_index = 1;
        model.recent_events_available = true;
        model.recent_event_count = 3;
        for (int i = 0; i < 3; ++i) {
            snprintf(model.recent_events[i], sizeof(model.recent_events[i]), ptc_ui_text(PTC_UI_T_RULE_SAVE_INCOMPLETE_PLEASE_VIEW_DETAILS));
            model.recent_event_timestamps[i] = 998;
        }
        failed |= save_preview(directory, "support", "support-failed", &model, dark);
        {
            PtcUiModel support_dlg = model;
            support_dlg.overlay = PTC_UI_OVERLAY_SUPPORT_EVENT;
            snprintf(support_dlg.overlay_title, sizeof(support_dlg.overlay_title), "%s", ptc_ui_text(PTC_UI_T_SUPPORT_EVENT_DETAILS));
            failed |= save_preview(directory, "support", "support-event-details", &support_dlg, dark);
        }
        model.waiting = true;
        failed |= save_preview(directory, "support", "support-waiting", &model, dark);
        failed |= render_setup_previews(directory, &baseline, dark);
    }
    model = baseline;
    model.view = PTC_UI_CHILD;
    model.remaining_minutes = 0;
    model.temporary_unlocked_available = model.temporary_unlocked = true;
    snprintf(model.result_status, sizeof(model.result_status), "error");
    snprintf(model.message, sizeof(model.message), ptc_ui_text(PTC_UI_T_SETUP_INCOMPLETE_PLEASE_WAIT_FOR_STATUS_SYNCHRONIZATION));
    snprintf(model.feedback_detail, sizeof(model.feedback_detail), ptc_ui_text(PTC_UI_T_DO_NOT_SUBMIT_REPEATEDLY_REFRESH_AND_DIAGNOSTICS));
    failed |= save_preview(directory, "child", "child-error", &model, false);
    failed |= save_preview(directory, "child", "child-error", &model, true);
    failed |= render_visual_matrix(directory, &baseline);
    return failed;
}

static int render_eye_care_previews(const char *directory, const PtcUiModel *baseline)
{
    int failed = 0;
    for (int dark = 0; dark <= 1; ++dark) {
        PtcUiModel model = *baseline;
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_TODAY;
        model.eye_care_policy.enabled = true;
        model.eye_care_policy.play_minutes = 40;
        model.eye_care_policy.rest_minutes = 10;
        model.eye_care_used_minutes = 12;
        snprintf(model.eye_care_phase, sizeof(model.eye_care_phase), "playing");
        model.parent_page = PTC_UI_PARENT_PLAN;
        model.plan_page = PTC_UI_PLAN_PAGE_EYE_CARE;
        model.draft_eye_care_policy = model.eye_care_policy;
        model.eye_care_field_focus = 0;
        failed |= save_preview(directory, "plan", "eye-care-switch-on", &model, dark);
        {
            PtcUiModel off = model;
            off.eye_care_policy.enabled = false;
            off.draft_eye_care_policy.enabled = false;
            snprintf(off.eye_care_phase, sizeof(off.eye_care_phase), "off");
            failed |= save_preview(directory, "plan", "eye-care-switch-off", &off, dark);
        }
        model.parent_page = PTC_UI_PARENT_TODAY;
        if (!ptc_ui_open_home_details(&model)) return 1;
        failed |= save_preview(directory, "parent", "parent-details-eye-care-playing", &model, dark);
        ptc_ui_cancel_overlay(&model);
        snprintf(model.eye_care_phase, sizeof(model.eye_care_phase), "resting");
        model.eye_care_break_id = 123;
        model.eye_care_rest_remaining_seconds = 94;
        model.blocked_today = 1;
        model.restricted_now = 1;
        model.remaining_available = true;
        model.remaining_minutes = 0;
        model.daily_restriction_active = false;
        failed |= save_preview(directory, "parent", "eye-care-resting", &model, dark);
        {
            PtcUiModel protected = model;
            protected.disable_flag_present = true;
            protected.eye_care_rest_remaining_seconds = 0;
            protected.selected_index = 6;
            failed |= save_preview(directory, "parent", "eye-care-protected-expired", &protected, dark);
        }
        {
            PtcUiModel editor = model;
            editor.parent_page = PTC_UI_PARENT_PLAN;
            editor.plan_page = PTC_UI_PLAN_PAGE_EYE_CARE;
            editor.eye_care_field_focus = 4;
            failed |= save_preview(directory, "plan", "eye-care-skip", &editor, dark);
        }
        if (!ptc_ui_open_home_details(&model)) return 1;
        failed |= save_preview(directory, "parent", "parent-details-eye-care-resting", &model, dark);
    }
    return failed;
}


static int render_dock_previews(const char *directory, const PtcUiModel *baseline)
{
    int failed = 0;
    for (int dark = 0; dark < 2; ++dark) {
        PtcUiModel model = *baseline;
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_PLAN;
        model.plan_page = PTC_UI_PLAN_PAGE_DOCK;
        model.dock_available = model.dock_supported_available = model.dock_supported = true;
        model.undocked_usage_available = true;
        model.dock_policy.undocked_limit_enabled = true;
        model.dock_policy.undocked_daily_minutes = 30;
        model.draft_dock_policy = model.dock_policy;
        model.undocked_used_minutes = 12;
        model.undocked_remaining_minutes = 18;
        snprintf(model.operation_mode, sizeof(model.operation_mode), "undocked");
        failed |= save_preview(directory, "plan", "dock-policy", &model, dark);
        model.view = PTC_UI_CHILD;
        failed |= save_preview(directory, "child", "dock-handheld", &model, dark);
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_TODAY;
        model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
        model.selected_index = 7;
        failed |= save_preview(directory, "parent", "dock-today-waiver", &model, dark);
        {
            const char *names[] = {"today-status-eye", "today-status-bedtime", "today-status-tv"};
            for (int i = 8; i <= 10; ++i) {
                PtcUiModel shortcut = model;
                shortcut.selected_index = i;
                failed |= save_preview(directory, "parent", names[i - 8], &shortcut, dark);
                if (!ptc_ui_open_today_settings(&shortcut, i)) return 1;
                failed |= save_preview(directory, "plan", names[i - 8], &shortcut, dark);
            }
        }
        model.dock_waived_today = true;
        failed |= save_preview(directory, "parent", "dock-today-waived", &model, dark);
        model.dock_waived_today = false;
        model.dock_available = false;
        failed |= save_preview(directory, "parent", "dock-today-unknown", &model, dark);
        model.dock_available = true;
        if (!ptc_ui_open_home_details(&model)) return 1;
        failed |= save_preview(directory, "parent", "dock-today-details", &model, dark);
        ptc_ui_cancel_overlay(&model);
        model.view = PTC_UI_CHILD;
        model.dock_restriction_active = true;
        model.blocked_today = model.restricted_now = 1;
        model.remaining_minutes = 0;
        model.undocked_used_minutes = 30;
        model.undocked_remaining_minutes = 0;
        failed |= save_preview(directory, "child", "dock-restricted", &model, dark);
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_PLAN;
        model.plan_page = PTC_UI_PLAN_PAGE_DOCK;
        model.draft_dock_policy.force_docked = model.dock_dirty = true;
        model.dock_field_focus = 3;
        failed |= save_preview(directory, "plan", "dock-force-draft", &model, dark);
        model.dock_supported = false;
        failed |= save_preview(directory, "plan", "dock-lite", &model, dark);
        model.dock_supported = true;
        model.eden_mode_controls = true;
        failed |= save_preview(directory, "plan", "dock-eden-simulation", &model, dark);
        model.eden_mode_controls = false;
        model.undocked_usage_available = false;
        snprintf(model.operation_mode, sizeof(model.operation_mode), "unknown");
        failed |= save_preview(directory, "plan", "dock-unknown", &model, dark);
    }
    return failed;
}

static int render_pairing_qr_previews(const char *directory, const PtcUiModel *baseline)
{
    int failed = 0;
    for (int dark = 0; dark < 2; ++dark) {
        PtcUiModel model = *baseline;
        uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_GRANT;
        model.overlay = PTC_UI_OVERLAY_QR;
        snprintf(model.overlay_title, sizeof(model.overlay_title), "%s",
                 ptc_ui_text(PTC_UI_T_MOBILE_PHONE_COMPUTER_GENERATES_GRANT_CODE));
        snprintf(model.overlay_body, sizeof(model.overlay_body), "%s",
                 ptc_ui_text(PTC_UI_T_YOU_CAN_SCAN_THE_QR_CODE_FOR));
        snprintf(model.pairing_base_url, sizeof(model.pairing_base_url), "%s", PTC_PAIRING_BASE_URL);
        model.demo_secret_enabled = true;
        if (!ptc_build_pairing_url("kid-switch", PTC_DEMO_GRANT_SECRET, model.pairing_url, sizeof(model.pairing_url)) ||
            !qrcodegen_encodeText(model.pairing_url, temp, model.qr_code,
                qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) return 1;
        failed |= save_preview(directory, "parent", "pairing-qr-demo", &model, dark);
    }
    return failed;
}

static int render_policy_previews(const char *directory, const PtcUiModel *baseline)
{
    int failed = 0;
    const char *names[] = {"policy-pending", "policy-active", "policy-skipped", "policy-tv-empty", "policy-unknown", "policy-none-today"};
    for (int dark = 0; dark < 2; ++dark) for (int scene = 0; scene < 6; ++scene) {
        PtcUiModel model = *baseline;
        model.overlay = PTC_UI_OVERLAY_NONE;
        model.bedtime_policy.enabled = true;
        model.bedtime_next_available = true;
        model.bedtime_next_start_day_index = model.day_index;
        model.bedtime_next_start_minute = 540;
        model.bedtime_next_end_minute = 420;
        model.bedtime_next_window_instance_id = ptc_bedtime_window_instance_id(model.day_index, 540);
        model.eye_care_policy.enabled = true;
        model.eye_care_policy.play_minutes = 40;
        model.eye_care_policy.rest_minutes = 10;
        model.eye_care_used_minutes = 10;
        snprintf(model.eye_care_phase, sizeof(model.eye_care_phase), "playing");
        model.dock_policy.undocked_limit_enabled = true;
        model.dock_policy.undocked_daily_minutes = 30;
        model.undocked_usage_available = true;
        model.undocked_used_minutes = 12;
        model.undocked_remaining_minutes = 18;
        if (scene == 1) {
            model.bedtime_active = true;
            model.bedtime_start_day_index = model.day_index - 1;
            model.bedtime_start_minute = 1260;
            model.bedtime_end_minute = 526;
            model.bedtime_window_instance_id = ptc_bedtime_window_instance_id(model.day_index - 1, 1260);
        } else if (scene == 2) {
            model.bedtime_skipped_window_available = true;
            model.bedtime_skipped_start_day_index = model.day_index;
            model.bedtime_skipped_start_minute = 540;
            model.bedtime_skipped_end_minute = 420;
            model.bedtime_skipped_window_instance_id = model.bedtime_next_window_instance_id;
        } else if (scene == 3) {
            model.undocked_used_minutes = 30;
            model.undocked_remaining_minutes = 0;
            snprintf(model.operation_mode, sizeof(model.operation_mode), "docked");
        } else if (scene == 4) {
            model.status_updated_at = 1;
        } else if (scene == 5) {
            model.bedtime_next_start_day_index++;
            model.bedtime_next_window_instance_id = ptc_bedtime_window_instance_id(model.day_index + 1, 540);
        }
        model.view = PTC_UI_CHILD;
        failed |= save_preview(directory, "child", names[scene], &model, dark);
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_TODAY;
        failed |= save_preview(directory, "parent", names[scene], &model, dark);
    }
    return failed;
}

static int render_config_backup_previews(const char *directory, const PtcUiModel *baseline)
{
    int failed = 0;
    for (int dark = 0; dark < 2; ++dark) {
        PtcUiModel model = *baseline;
        model.view = PTC_UI_PARENT; model.parent_page = PTC_UI_PARENT_SETTINGS;
        model.overlay = PTC_UI_OVERLAY_NONE; model.selected_index = 7;
        failed |= save_preview(directory, "settings", "settings-backup-entry", &model, dark);
        model.overlay = PTC_UI_OVERLAY_CONFIG_BACKUP;
        model.config_backup_tab = 0; model.overlay_selection = 13;
        model.config_backup_ready = true;
        snprintf(model.config_metadata, sizeof(model.config_metadata), "kid-switch / 2026-10-06 12:00");
        failed |= save_preview(directory, "settings", "config-backup-create", &model, dark);
        model.config_backup_tab = 1; model.overlay_selection = 14;
        model.config_today_available = model.config_pin_available = true;
        model.config_groups = 511;
        snprintf(model.config_preview, sizeof(model.config_preview), ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_PREVIEW),
            120u, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_APPLY));
        failed |= save_preview(directory, "settings", "config-backup", &model, dark);
        model.config_today_available = false; model.config_groups = 2043;
        snprintf(model.config_preview, sizeof(model.config_preview), "%s", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NEW_KEY));
        failed |= save_preview(directory, "settings", "config-backup-security", &model, dark);
        model.config_backup_ready = false; model.config_groups = 0;
        snprintf(model.config_preview, sizeof(model.config_preview), "%s", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_INVALID));
        failed |= save_preview(directory, "settings", "config-backup-invalid", &model, dark);
    }
    return failed;
}

static void localize_preview_baseline(PtcUiModel *model)
{
    snprintf(model->command_name, sizeof(model->command_name), "%s", ptc_ui_text(PTC_UI_T_REFRESH_STATUS));
    snprintf(model->transport_label, sizeof(model->transport_label), "%s", ptc_ui_text(PTC_UI_T_TRANSPORT_IPC));
    snprintf(model->message, sizeof(model->message), "%s", ptc_ui_text(PTC_UI_T_TODAY_S_STATUS_HAS_BEEN_UPDATED));
}

static int render_parent_pin_error_previews(const char *directory, const PtcUiModel *base)
{
    int failed = 0;
    for (int dark = 0; dark < 2; ++dark) {
        PtcUiModel model = *base;
        model.view = PTC_UI_CHILD;
        model.overlay = PTC_UI_OVERLAY_AUTH_ERROR;
        model.auth_error_parent_entry = true;
        snprintf(model.auth_error_title, sizeof(model.auth_error_title), "%s", ptc_ui_text(PTC_UI_T_PIN_IS_INCORRECT));
        snprintf(model.auth_error_message, sizeof(model.auth_error_message), "%s", ptc_ui_text(PTC_UI_T_PARENT_PIN_ERROR_CHOICE));
        failed |= save_preview(directory, "support", "parent-pin-error", &model, dark);
        model.auth_cooldown_seconds = 30;
        snprintf(model.auth_error_title, sizeof(model.auth_error_title), "%s", ptc_ui_text(PTC_UI_T_PIN_TEMPORARILY_LOCKED));
        snprintf(model.auth_error_message, sizeof(model.auth_error_message), "%s", ptc_ui_text(PTC_UI_T_TOO_MANY_PIN_ERRORS_PLEASE_WAIT_FOR));
        failed |= save_preview(directory, "support", "parent-pin-cooldown", &model, dark);
    }
    return failed;
}

static int render_language_pass(const char *root_dir, const char *subdir, PtcUiLanguagePreference lang, const PtcUiModel *base_model)
{
    char target_dir[1024];
    if (subdir && subdir[0] != '\0') {
        int n = snprintf(target_dir, sizeof(target_dir), "%s/%s", root_dir, subdir);
        if (n < 0 || (size_t)n >= sizeof(target_dir)) return 1;
    } else {
        int n = snprintf(target_dir, sizeof(target_dir), "%s", root_dir);
        if (n < 0 || (size_t)n >= sizeof(target_dir)) return 1;
    }
    ptc_mkdir(target_dir);
    ptc_ui_language_set_resolved(lang);
    PtcUiModel model = *base_model;
    localize_preview_baseline(&model);
    int failed = 0;
    for (int style = 0; style < (preview_all_styles ? PTC_UI_THEME_STYLE_COUNT : 1); ++style) {
        char styled_dir[1200];
        if (style == 0) {
            snprintf(styled_dir, sizeof(styled_dir), "%s", target_dir);
        } else {
            snprintf(styled_dir, sizeof(styled_dir), "%s/style-%s", target_dir,
                ptc_ui_theme_style_name((PtcUiThemeStyle)style));
            ptc_mkdir(styled_dir);
        }
        ptc_ui_theme_set_style((PtcUiThemeStyle)style);
        failed |= render_all_previews(styled_dir, &model);
        failed |= render_eye_care_previews(styled_dir, &model);
        failed |= render_dock_previews(styled_dir, &model);
        failed |= render_config_backup_previews(styled_dir, &model);
        failed |= render_pairing_qr_previews(styled_dir, &model);
        failed |= render_policy_previews(styled_dir, &model);
        failed |= render_parent_pin_error_previews(styled_dir, &model);
    }
    return failed;
}

int main(int argc, char **argv)
{
    FILE *font;
    long length;
    unsigned char *bytes;
    PtcUiModel model;
    PtcRules baseline_rules;
    int failed = 0;
    if (argc == 2 && strcmp(argv[1], "--check-primitives") == 0) return check_primitives();
    preview_all_styles = argc == 4 && strcmp(argv[3], "--all-styles") == 0;
    if ((argc != 3 && !preview_all_styles) || !(font = fopen(argv[1], "rb"))) return 1;
    fseek(font, 0, SEEK_END); length = ftell(font); rewind(font);
    if (length <= 0 || !(bytes = malloc((size_t)length))) { fclose(font); return 1; }
    if (fread(bytes, 1, length, font) != (size_t)length) { fclose(font); free(bytes); return 1; }
    fclose(font);
    if (FT_New_Memory_Face(NULL, bytes, length, 0, &g_ui.face)) { free(bytes); return 1; }
    g_ui.font_ready = g_ui.framebuffer_ready = true;
    memset(&model, 0, sizeof(model));
    model.view = PTC_UI_CHILD;
    model.status_loaded = model.remaining_available = model.played_minutes_available = true;
    model.dock_available = model.dock_status_available = model.bedtime_status_available = true;
    snprintf(model.operation_mode, sizeof(model.operation_mode), "undocked");
    model.status_updated_at = 998;
    model.remaining_minutes = 51;
    model.played_minutes = 69;
    model.limited_today = model.play_timer_enabled = 1;
    model.restriction_enabled_available = model.restriction_enabled = true;
    model.day_index = 2380;
    model.forecast_available = true;
    model.forecast[0] = (PtcResultForecastDay){.day_index=2380, .mode=PTC_RULE_MODE_LIMIT, .minutes=120, .rule_source="weekly"};
    model.forecast[1] = (PtcResultForecastDay){.day_index=2381, .mode=PTC_RULE_MODE_LIMIT, .minutes=90, .rule_source="weekly"};
    model.daily_buffer_available = true;
    model.daily_buffer_minutes = 10;
    ptc_rules_default(&baseline_rules);
    memcpy(model.current_week, baseline_rules.week, sizeof(model.current_week));
    memcpy(model.draft_week, baseline_rules.week, sizeof(model.draft_week));
    model.current_week[ptc_weekday_from_day_index(model.day_index)].minutes = 120;
    model.draft_week[ptc_weekday_from_day_index(model.day_index)].minutes = 120;
    model.holiday_enabled = model.draft_holiday_enabled = baseline_rules.holiday_enabled;
    model.holiday_rule = model.draft_holiday_rule = baseline_rules.holiday_rule;
    model.makeup_workday_rule = model.draft_makeup_workday_rule = baseline_rules.makeup_workday_rule;
    model.bedtime_policy = model.draft_bedtime_policy = baseline_rules.bedtime;
    model.usage_summary_available = true;
    model.usage_known_days_7 = 5; model.usage_consumed_minutes_7 = 350;
    model.usage_known_days_30 = 20; model.usage_consumed_minutes_30 = 1420;
    snprintf(model.setup_phase, sizeof(model.setup_phase), "active");
    snprintf(model.rule_source, sizeof(model.rule_source), "weekly");
    snprintf(model.result_status, sizeof(model.result_status), "ok");

    if (strcmp(argv[2], "--check-parent-tabs") == 0 || strcmp(argv[2], "--check-bedtime-editors") == 0 ||
        strcmp(argv[2], "--check-dock-editor") == 0) {
        failed = strcmp(argv[2], "--check-parent-tabs") == 0 ? check_parent_tabs(&model) :
            strcmp(argv[2], "--check-dock-editor") == 0 ? check_dock_editor(&model) : check_bedtime_editors(&model);
        FT_Done_Face(g_ui.face);
        free(bytes);
        return failed;
    }

    #if defined(_WIN32)
    failed |= render_language_pass(argv[2], "", PTC_UI_LANGUAGE_SIMPLIFIED, &model);
    failed |= render_language_pass(argv[2], "en", PTC_UI_LANGUAGE_ENGLISH, &model);
    failed |= render_language_pass(argv[2], "zh_hant", PTC_UI_LANGUAGE_TRADITIONAL, &model);
    #else
    typedef struct {
        const char *subdir;
        PtcUiLanguagePreference lang;
    } LanguageJob;
    const LanguageJob jobs[3] = {
        { "", PTC_UI_LANGUAGE_SIMPLIFIED },
        { "en", PTC_UI_LANGUAGE_ENGLISH },
        { "zh_hant", PTC_UI_LANGUAGE_TRADITIONAL }
    };
    pid_t pids[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            /* Fallback to synchronous execution on fork failure */
            failed |= render_language_pass(argv[2], jobs[i].subdir, jobs[i].lang, &model);
        } else if (pid == 0) {
            int res = render_language_pass(argv[2], jobs[i].subdir, jobs[i].lang, &model);
            FT_Done_Face(g_ui.face);
            free(bytes);
            _exit(res ? 1 : 0);
        } else {
            pids[i] = pid;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (pids[i] > 0) {
            int status = 0;
            if (waitpid(pids[i], &status, 0) > 0) {
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    failed = 1;
                }
            } else {
                failed = 1;
            }
        }
    }
    #endif
    FT_Done_Face(g_ui.face);
    free(bytes);
    return failed;
}
