#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "companion/overlay/vendor/libtesla/include/playwise_glyph_cache.hpp"

namespace {
std::size_t live_bytes = 0;
std::size_t live_bitmaps = 0;
std::size_t peak_bytes = 0;

struct BitmapDeleter {
    std::size_t bytes = 0;
    void operator()(unsigned char *bitmap) const {
        assert(live_bytes >= bytes && live_bitmaps > 0);
        live_bytes -= bytes;
        --live_bitmaps;
        std::free(bitmap);
    }
};

struct Glyph {
    std::unique_ptr<unsigned char, BitmapDeleter> bitmap;
};

template<typename Cache>
void rasterize(Cache &cache, std::uint64_t key, std::size_t bytes, bool fail = false) {
    auto *glyph = cache.insert(key, bytes);
    assert(glyph);
    if (fail) {
        cache.erase(key);
        assert(!cache.find(key));
        return;
    }
    auto *bitmap = static_cast<unsigned char *>(std::malloc(bytes));
    assert(bitmap);
    glyph->bitmap = std::unique_ptr<unsigned char, BitmapDeleter>(bitmap, BitmapDeleter{bytes});
    live_bytes += bytes;
    ++live_bitmaps;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    assert(live_bytes == cache.bytes());
    assert(live_bitmaps == cache.size());
}

void test_lru_and_limits() {
    tsl::gfx::detail::GlyphCache<Glyph, 4, 64> cache;
    for (std::uint64_t key = 1; key <= 4; ++key) rasterize(cache, key, 8);
    assert(cache.find(1)); // Touching a glyph protects it from the next eviction.
    rasterize(cache, 5, 8);
    assert(cache.find(1) && !cache.find(2));
    assert(cache.size() == 4 && cache.bytes() == 32);
    assert(!cache.insert(6, 65)); // Oversized bitmaps leave existing owners intact.
    assert(cache.size() == 4 && live_bytes == 32);
    rasterize(cache, 7, 48); // Evict before allocation, including byte-budget pressure.
    assert(cache.bytes() <= 64 && live_bytes <= 64);
    cache.erase(999);
    cache.clear();
    cache.clear();
    assert(cache.size() == 0 && cache.bytes() == 0);
    assert(live_bitmaps == 0 && live_bytes == 0);
}

void test_failed_bitmap_and_replacement() {
    tsl::gfx::detail::GlyphCache<Glyph, 4, 64> cache;
    rasterize(cache, 1, 16);
    rasterize(cache, 1, 24); // Replacing a key releases its previous allocation.
    assert(live_bitmaps == 1 && live_bytes == 24);
    rasterize(cache, 2, 40, true);
    assert(live_bitmaps == 1 && live_bytes == 24);
    rasterize(cache, 2, 40); // A failed allocation may be retried.
    assert(cache.size() == 2 && live_bytes == 64);
}

void test_sustained_language_size_churn() {
    constexpr std::size_t budget = 256 * 1024;
    for (int reopen = 0; reopen < 10; ++reopen) {
        tsl::gfx::detail::GlyphCache<Glyph, 512, budget> cache;
        for (std::uint64_t i = 0; i < 10000; ++i) {
            const auto character = 0x4e00 + i % 2000;
            const auto key = (character << 32) | ((i % 2) << 30) | ((14 + i % 29) * 16);
            if (!cache.find(key)) rasterize(cache, key, (14 + i % 29) * (14 + i % 29));
            assert(cache.size() <= 512 && cache.bytes() <= budget);
        }
        // Hiding retains a bounded working set; reopening hits it without allocation.
        assert(cache.find((UINT64_C(0x4e00) + 9999 % 2000) << 32 |
            (UINT64_C(9999 % 2) << 30) | ((14 + 9999 % 29) * 16)));
    }
    assert(live_bytes == 0 && live_bitmaps == 0);
    assert(peak_bytes <= budget);
}
} // namespace

int main() {
    test_lru_and_limits();
    test_failed_bitmap_and_replacement();
    assert(live_bytes == 0 && live_bitmaps == 0);
    test_sustained_language_size_churn();
    std::printf("overlay cache: 100000 inserts, peak=%zu bytes, live=%zu, bitmaps=%zu\n",
        peak_bytes, live_bytes, live_bitmaps);
}
