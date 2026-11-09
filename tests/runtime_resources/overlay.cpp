#include <cstdlib>
#include <new>
#include <ctime>
static bool record_allocations;
static size_t allocations;
// Keep the replacement allocator opaque: GCC 15 otherwise inlines delete and
// misdiagnoses its matching malloc/free implementation as new/free mismatch.
#if defined(__GNUC__)
#define PTC_ALLOCATOR_NOINLINE __attribute__((noinline))
#else
#define PTC_ALLOCATOR_NOINLINE
#endif
PTC_ALLOCATOR_NOINLINE void *operator new(std::size_t size) {
    if (record_allocations) ++allocations;
    if (void *p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
PTC_ALLOCATOR_NOINLINE void operator delete(void *p) noexcept { std::free(p); }
PTC_ALLOCATOR_NOINLINE void operator delete(void *p, std::size_t) noexcept { std::free(p); }
PTC_ALLOCATOR_NOINLINE void *operator new[](std::size_t size) { return ::operator new(size); }
PTC_ALLOCATOR_NOINLINE void operator delete[](void *p) noexcept { std::free(p); }
PTC_ALLOCATOR_NOINLINE void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
#undef PTC_ALLOCATOR_NOINLINE

#include "tests/overlay_preview/renderer.hpp"
extern "C" {
#include "companion/overlay/bridge.h"
#include "companion/overlay/input_model.h"
#include "companion/ui_language.h"
#include "common/time/ptc_time.h"
}
#include "companion/overlay/layout.h"
#include "companion/overlay/source/render_types.hpp"
#include "wrap_reference.inc"

static int failures;
static void check(bool value, const char *label) {
    if (!value) { std::fprintf(stderr, "FAIL overlay resource regression: %s\n", label); ++failures; }
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    tsl::gfx::Renderer renderer(argv[1]);
    std::string long_word(900, 'W');
    std::string repeated;
    for (int i = 0; i < 100; ++i) repeated += "one two 三 ";
    const char *cases[] = {"", " ", "\n", "first\n\nlast\n", "one two", "  A   B   ",
        "今日已玩 69 分钟", "今日已玩 69 分鐘", "Played 69 min today", "tiny SUPERCALIFRAGILISTIC word",
        "🎮 游戏 → 12:00\n睡眠・休息", "a\tb\rc", "a\xc0\xaf z", long_word.c_str(), repeated.c_str()};
    unsigned comparisons = 0;
    for (const auto language : {PTC_UI_LANGUAGE_SIMPLIFIED, PTC_UI_LANGUAGE_TRADITIONAL, PTC_UI_LANGUAGE_ENGLISH}) {
        ptc_ui_language_set_resolved(language);
        for (float metric : {1.0f, 1.35f}) {
            renderer.horizontal_metric_scale = metric;
            for (int size : {18,24,32}) for (int width : {-1,0,1,31,180,380,100000}) {
                for (const char *text : cases) {
                    auto expected = reference_wrap(&renderer, text, size, width);
                    auto actual = overlay_wrap(&renderer, text, size, width);
                    if (expected != actual) std::fprintf(stderr, "layout mismatch size=%d width=%d text=%.70s\n", size, width, text);
                    check(expected == actual, "identical lines with Latin/CJK/blank/oversized tokens");
                    check(overlay_text_height(&renderer, text, width, size) ==
                        static_cast<int>(expected.size()) * ((size * 6 + 4) / 5), "height equals reference lines");
                    ++comparisons;
                }
            }
        }
    }
    for (const auto language : {PTC_UI_LANGUAGE_SIMPLIFIED, PTC_UI_LANGUAGE_TRADITIONAL, PTC_UI_LANGUAGE_ENGLISH}) {
        ptc_ui_language_set_resolved(language);
        for (const char *text : cases) for (int width : {20,140,300}) for (int height : {0,32,80}) {
            int old_size = 32, new_size = 32;
            const auto old_lines = reference_button_lines(&renderer, text, width, height, old_size);
            const auto new_lines = overlay_button_lines(&renderer, text, width, height, new_size);
            check(old_size == new_size && old_lines == new_lines, "fixed-button size and ellipsis unchanged");
        }
    }
    ptc_ui_language_set_resolved(PTC_UI_LANGUAGE_ENGLISH);
    renderer.horizontal_metric_scale = 1.0f;
    const auto expected = reference_wrap(&renderer, repeated.c_str(), 24, 380);
    double before_seconds = 0, after_seconds = 0, before_samples[5], after_samples[5];
    size_t before_allocations = 0, before_bytes = 0, after_allocations = 0, after_bytes = 0;
    int height = 0;
    for (int sample = 0; sample < 5; ++sample) {
        allocations = renderer.measured_bytes = 0;
        record_allocations = true;
        clock_t start = clock();
        for (int i = 0; i < 100; ++i) (void)reference_wrap(&renderer, repeated.c_str(), 24, 380);
        before_samples[sample] = double(clock() - start) / CLOCKS_PER_SEC;
        before_seconds += before_samples[sample];
        record_allocations = false;
        before_allocations += allocations; before_bytes += renderer.measured_bytes;
        allocations = renderer.measured_bytes = 0;
        record_allocations = true;
        start = clock();
        for (int i = 0; i < 100; ++i) height = overlay_text_height(&renderer, repeated.c_str(), 380, 24);
        after_samples[sample] = double(clock() - start) / CLOCKS_PER_SEC;
        after_seconds += after_samples[sample];
        record_allocations = false;
        after_allocations += allocations; after_bytes += renderer.measured_bytes;
    }
    check(height == static_cast<int>(expected.size()) * ((24 * 6 + 4) / 5), "benchmark produces same height");
    check(!after_allocations && before_allocations > 0, "height measurement needs no temporary heap allocations");
    check(after_bytes <= repeated.size() * 1000 && after_bytes < before_bytes, "measured bytes linear in input length");
    // The visitor passes the exact final-line width used by centered rendering.
    renderer.text_runs.clear();
    const char *centered = "  first  line\nsecond\n";
    const auto centered_lines = reference_wrap(&renderer, centered, 24, 300);
    overlay_text(&renderer, centered, 20, 20, 300, 24, {15,15,15,15}, true);
    check(renderer.text_runs.size() == centered_lines.size(), "centered line count unchanged");
    for (size_t i = 0; i < renderer.text_runs.size() && i < centered_lines.size(); ++i) {
        const auto width = renderer.drawString(centered_lines[i].c_str(), false, 0, 0, 24, {0,0,0,0}).first;
        check(renderer.text_runs[i].text == centered_lines[i] &&
            renderer.text_runs[i].x == 20 + (300 - static_cast<int>(width)) / 2, "centered text reuses exact measured widths");
    }
    std::printf("{\"schema_version\":1,\"line_comparisons\":%u,\"iterations\":500,\"input_bytes\":%zu,"
        "\"measurement_bytes_before\":%zu,\"measurement_bytes_after\":%zu,\"allocations_before\":%zu,"
        "\"allocations_after\":%zu,\"seconds_before\":%.6f,\"seconds_after\":%.6f,"
        "\"seconds_before_samples\":[%.6f,%.6f,%.6f,%.6f,%.6f],"
        "\"seconds_after_samples\":[%.6f,%.6f,%.6f,%.6f,%.6f],\"failures\":%d}\n",
        comparisons, repeated.size(), before_bytes, after_bytes, before_allocations, after_allocations,
        before_seconds, after_seconds,
        before_samples[0],before_samples[1],before_samples[2],before_samples[3],before_samples[4],
        after_samples[0],after_samples[1],after_samples[2],after_samples[3],after_samples[4], failures);
    return failures ? 1 : 0;
}
