#include <time.h>
#include "companion/nro/ui_render_internal.h"

UiRuntime g_ui;
const PtcUiPalette *g_palette;
PtcUiThemeView g_theme;
unsigned ptc_resource_font_sizes, ptc_resource_font_loads;
extern unsigned ptc_resource_shadow_distances;
static bool docked, fail_bitmap;
static int failures;
static uint32_t before[SCREEN_WIDTH * SCREEN_HEIGHT], after[SCREEN_WIDTH * SCREEN_HEIGHT];
bool is_docked_mode(void) { return docked; }
void ptc_ui_preview_record_text(int x, int y, const char *text, int size)
{ (void)x; (void)y; (void)text; (void)size; }
void *ptc_resource_bitmap_malloc(size_t size) { return fail_bitmap ? NULL : malloc(size); }
void *ptc_resource_bitmap_calloc(size_t count, size_t size) { return fail_bitmap ? NULL : calloc(count, size); }
#include "shadow_reference.inc"

static void check(bool value, const char *message)
{
    if (!value) { fprintf(stderr, "FAIL NRO resource regression: %s\n", message); ++failures; }
}

#include "nro_wrap_reference.inc"

static void reference_draw(UiRect r, int radius, int blur, int alpha, int bias)
{
    uint32_t rgb = ui_mix_rgb(g_palette->text_primary, 0, 65);
    reference_shadow(before, SCREEN_WIDTH, r, radius, blur, alpha, bias, pack_rgb(rgb), true);
}

static void check_blend_pixels(void)
{
    const unsigned alphas[] = {0, 1, 127, 254, 255};
    const unsigned stride = SCREEN_WIDTH + 7;
    uint32_t pixels[2 * (SCREEN_WIDTH + 7)];
    ui_set_vertical_clip(1, 2);
    for (unsigned a = 0; a < sizeof(alphas) / sizeof(alphas[0]); ++a) {
        for (unsigned src = 0; src < 256; ++src) {
            for (unsigned dst = 0; dst < 256; ++dst) {
                unsigned alpha = alphas[a];
                uint32_t color = (src << 24) | (src * 0x010101u);
                uint32_t old = (dst << 24) | (dst * 0x010101u);
                unsigned mixed = (src * alpha + dst * (255 - alpha)) / 255;
                uint32_t expected = alpha ? 0xff000000u | (mixed * 0x010101u) : old;
                pixels[stride + 3] = old;
                blend_pixel(pixels, stride, 3, 1, color, (uint8_t)alpha);
                check(pixels[stride + 3] == expected, "blend matches scalar channels and output alpha");
            }
        }
    }
    for (unsigned i = 0; i < sizeof(pixels) / sizeof(pixels[0]); ++i) pixels[i] = 0x12345678u;
    blend_pixel(pixels, stride, 1, 0, 0, 255);
    blend_pixel(pixels, stride, -1, 1, 0, 255);
    blend_pixel(pixels, stride, SCREEN_WIDTH, 1, 0, 255);
    blend_pixel(pixels, stride, 1, 2, 0, 255);
    blend_pixel(pixels, stride, 1, SCREEN_HEIGHT, 0, 255);
    for (unsigned i = 0; i < sizeof(pixels) / sizeof(pixels[0]); ++i)
        check(pixels[i] == 0x12345678u, "opaque clipping preserves guards and stride padding");
    ui_set_vertical_clip(0, SCREEN_HEIGHT);
}

static void draw_clip_case(uint32_t *pixels, uint32_t stride, int kind, int x, int y)
{
    UiRect rect = {x, y, 83, 57};
    switch (kind) {
    case 0: fill_round_rect(pixels, stride, rect, 17, UI_ACCENT); break;
    case 1: draw_rect_outline(pixels, stride, rect, 17, 3, UI_ACCENT); break;
    case 2: fill_round_rect_gradient(pixels, stride, rect, 17, UI_ACCENT, UI_SURFACE); break;
    case 3: draw_circle_outline(pixels, stride, x, y, 29, 4, UI_ACCENT); break;
    case 4: draw_line(pixels, stride, x - 30, y - 20, x + 55, y + 45, 7, UI_ACCENT); break;
    case 5: draw_text(pixels, stride, x, y, "PlayWise 今日 • 🎮", 24, UI_INK); break;
    case 6: draw_text_bold(pixels, stride, x, y, "PlayWise 今日 • 🎮", 24, UI_INK); break;
    default: draw_text(pixels, stride, x, y, "🎮", 24, UI_INK); break;
    }
}

static void check_clipped_draws(void)
{
    const unsigned stride = SCREEN_WIDTH + 7;
    const size_t count = stride * SCREEN_HEIGHT;
    const uint32_t untouched = 0x2351472bu;
    const int origins[][2] = {{-20, -12}, {33, 60}, {1255, 703}, {100, 740}, {0, 49}, {1280, 50}};
    const int clips[][2] = {{0, 720}, {48, 85}, {0, 0}, {100, 20}, {-80, -5}, {725, 800}, {-5, 40}, {690, 800}};
    uint32_t *full = malloc(count * sizeof(*full)), *clipped = malloc(count * sizeof(*clipped));
    check(full && clipped, "clip fixture allocation");
    if (!full || !clipped) { free(full); free(clipped); return; }
    for (int language = PTC_UI_LANGUAGE_SIMPLIFIED; language <= PTC_UI_LANGUAGE_ENGLISH; ++language) {
        ptc_ui_language_set_resolved((PtcUiLanguagePreference)language);
        ui_glyph_cache_clear();
        for (int kind = 0; kind < 8; ++kind) {
            for (unsigned origin = 0; origin < sizeof(origins) / sizeof(origins[0]); ++origin) {
                for (size_t p = 0; p < count; ++p) full[p] = untouched;
                ui_set_vertical_clip(0, SCREEN_HEIGHT);
                draw_clip_case(full, stride, kind, origins[origin][0], origins[origin][1]);
                for (unsigned clip = 0; clip < sizeof(clips) / sizeof(clips[0]); ++clip) {
                    for (size_t p = 0; p < count; ++p) clipped[p] = untouched;
                    ui_set_vertical_clip(clips[clip][0], clips[clip][1]);
                    draw_clip_case(clipped, stride, kind, origins[origin][0], origins[origin][1]);
                    bool same = true;
                    for (int y = 0; y < SCREEN_HEIGHT && same; ++y) {
                        for (unsigned x = 0; x < stride; ++x) {
                            size_t p = (size_t)y * stride + x;
                            uint32_t expected = y >= clips[clip][0] && y < clips[clip][1] ? full[p] : untouched;
                            if (clipped[p] != expected) { same = false; break; }
                        }
                    }
                    check(same, "clipped primitives and glyphs equal masked full render, including padding");
                }
            }
        }
    }
    ui_set_vertical_clip(0, SCREEN_HEIGHT);
    ui_glyph_cache_clear();
    free(full); free(clipped);
}

int main(int argc, char **argv)
{
    const UiRect rects[] = {{108,176,1064,400}, {0,0,1,1}, {1,2,17,9}, {-10,-5,43,28},
        {1260,709,50,30}, {100,200,52,52}, {7,9,1280,720}, {200,300,0,3}};
    const int radii[] = {0,4,12,80};
    unsigned comparisons = 0, hits_sizes, hits_loads, before_distances, after_distances;
    double before_seconds, after_seconds;
    double before_samples[5], after_samples[5];
    if (argc != 2) return 2;
    check_blend_pixels();
    for (int style = 0; style < PTC_UI_THEME_STYLE_COUNT; ++style) {
        for (int dark = 0; dark < 2; ++dark) {
            g_palette = ptc_ui_theme_style_palette(dark ? PTC_UI_RESOLVED_DARK : PTC_UI_RESOLVED_LIGHT,
                (PtcUiThemeStyle)style);
            for (unsigned ri = 0; ri < sizeof(rects)/sizeof(rects[0]); ++ri) {
                for (unsigned r = 0; r < sizeof(radii)/sizeof(radii[0]); ++r) {
                    for (int clip = 0; clip < 3; ++clip) {
                        ui_set_vertical_clip(clip == 1 ? 200 : 0, clip == 2 ? 310 : 720);
                        for (size_t p = 0; p < SCREEN_WIDTH * SCREEN_HEIGHT; ++p)
                            before[p] = after[p] = 0xff51472bu + (uint32_t)(p % 181);
                        int blur = ri % 2 ? 4 : 9, bias = clip - 1;
                        reference_draw(rects[ri], radii[r], blur, 52, bias);
                        draw_round_rect_shadow(after, SCREEN_WIDTH, rects[ri], radii[r], blur, 52, bias);
                        check(!memcmp(before, after, sizeof(before)), "shadow pixels including clipped corners");
                        ++comparisons;
                    }
                }
            }
        }
    }
    ui_set_vertical_clip(0,720);
    reference_distances = ptc_resource_shadow_distances = 0;
    before_seconds = after_seconds = 0;
    for (int sample = 0; sample < 5; ++sample) {
        clock_t start = clock();
        for (int i = 0; i < 30; ++i) reference_draw(rects[0], 12, 9, 52, 3);
        before_samples[sample] = (double)(clock() - start) / CLOCKS_PER_SEC;
        before_seconds += before_samples[sample];
        start = clock();
        for (int i = 0; i < 30; ++i) draw_round_rect_shadow(after, SCREEN_WIDTH, rects[0], 12, 9, 52, 3);
        after_samples[sample] = (double)(clock() - start) / CLOCKS_PER_SEC;
        after_seconds += after_samples[sample];
    }
    before_distances = reference_distances;
    after_distances = ptc_resource_shadow_distances;
    check(after_distances < before_distances / 4, "shadow skips the solid card interior");

    FILE *font = fopen(argv[1], "rb");
    if (!font) return 2;
    fseek(font, 0, SEEK_END);
    long length = ftell(font);
    rewind(font);
    unsigned char *bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, font) != (size_t)length) return 2;
    fclose(font);
    check(FT_New_Memory_Face(NULL, bytes, length, 0, &g_ui.face) == 0, "load pinned font");
    g_ui.font_ready = true;
    check_clipped_draws();
    check_nro_wrapping();
    int widths[3];
    for (int i = 0; i < 3; ++i) widths[i] = measure_text("PlayWise 123", 18 + i * 6);
    ptc_resource_font_sizes = ptc_resource_font_loads = 0;
    for (int i = 0; i < 1000; ++i)
        check(measure_text("PlayWise 123", 18 + (i % 3) * 6) == widths[i % 3], "warm alternating sizes preserve width");
    hits_sizes = ptc_resource_font_sizes;
    hits_loads = ptc_resource_font_loads;
    check(!hits_sizes && !hits_loads, "cache hits never configure or rasterize fonts");
    for (int language = PTC_UI_LANGUAGE_SIMPLIFIED; language <= PTC_UI_LANGUAGE_ENGLISH; ++language) {
        ptc_ui_language_set_resolved((PtcUiLanguagePreference)language);
        ui_glyph_cache_clear();
        for (int mode = 0; mode < 2; ++mode) {
            docked = mode != 0;
            draw_text(after, SCREEN_WIDTH, 30, 80, "PlayWise 123 今日", 24, UI_INK);
            draw_text_bold(after, SCREEN_WIDTH, 30, 110, "PlayWise 123 今日", 24, UI_INK);
            check(measure_text("PlayWise 123", 24) == widths[1], "language invalidation and bold retain normal metrics");
        }
    }
    ui_glyph_cache_clear();
    fail_bitmap = true;
    int failed_width = measure_text("A", 24);
    unsigned failed_loads = ptc_resource_font_loads;
    check(failed_width > 0 && measure_text("A", 24) == failed_width &&
        ptc_resource_font_loads == failed_loads + 1, "allocation failure retains metrics but never caches an empty bitmap");
    fail_bitmap = false;
    check(measure_text("A", 24) == failed_width && ptc_resource_font_loads == failed_loads + 2,
        "failed bitmap is retried successfully");
    check(measure_text("A", 24) == failed_width && ptc_resource_font_loads == failed_loads + 2,
        "recovered bitmap is cached");
    check(measure_text("\xe2\x80\xa2 \xf0\x9f\x8e\xae", 24) > 0, "procedural bullet and color emoji");
    check(measure_text("A", 0) == 0 && measure_text("A", -1) == 0, "invalid font sizes rejected");
    ui_glyph_cache_clear();
    FT_Done_Face(g_ui.face);
    free(bytes);
    printf("{\"schema_version\":1,\"pixel_comparisons\":%u,\"shadow_distance_calls_before\":%u,"
        "\"shadow_distance_calls_after\":%u,\"shadow_seconds_before\":%.6f,\"shadow_seconds_after\":%.6f,"
        "\"shadow_seconds_before_samples\":[%.6f,%.6f,%.6f,%.6f,%.6f],"
        "\"shadow_seconds_after_samples\":[%.6f,%.6f,%.6f,%.6f,%.6f],"
        "\"warm_measurements\":1000,\"warm_font_size_calls\":%u,\"warm_font_loads\":%u,\"failures\":%d}\n",
        comparisons, before_distances, after_distances, before_seconds, after_seconds,
        before_samples[0],before_samples[1],before_samples[2],before_samples[3],before_samples[4],
        after_samples[0],after_samples[1],after_samples[2],after_samples[3],after_samples[4], hits_sizes, hits_loads, failures);
    return failures ? 1 : 0;
}
