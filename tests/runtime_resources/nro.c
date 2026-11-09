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

static void reference_draw(UiRect r, int radius, int blur, int alpha, int bias)
{
    uint32_t rgb = ui_mix_rgb(g_palette->text_primary, 0, 65);
    reference_shadow(before, SCREEN_WIDTH, r, radius, blur, alpha, bias, pack_rgb(rgb), true);
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
