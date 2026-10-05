#pragma once
// Host adapter for the drawRect/drawString surface used by the real Overlay.
// Uses the vendored stb rasterizer and pinned documentation font; no Switch APIs.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../companion/overlay/vendor/libtesla/include/stb_truetype.h"
using u32 = uint32_t;
using s32 = int32_t;
using u64 = uint64_t;
static u64 armGetSystemTick() { return 1000000000000ULL; }
static u64 armTicksToNs(u64 ticks) { return ticks; }
namespace tsl {
struct Color { unsigned r, g, b, a; };
namespace style { namespace color { constexpr Color ColorTransparent{0, 0, 0, 0}; } }
namespace gfx {
class Renderer {
    stbtt_fontinfo font_{};
    std::vector<unsigned char> font_bytes_;
    void pixel(int x, int y, Color color, unsigned coverage = 255) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        unsigned alpha = color.a * 17 * coverage / 255;
        unsigned channels[] = {color.r * 17, color.g * 17, color.b * 17};
        for (int c = 0; c < 3; ++c) {
            auto &p = pixels[(y * width + x) * 3 + c];
            p = (channels[c] * alpha + p * (255 - alpha)) / 255;
        }
    }
public:
    static constexpr int width = 448, height = 720;
    std::vector<unsigned char> pixels = std::vector<unsigned char>(width * height * 3, 17);
    explicit Renderer(const char *font_path) {
        FILE *file = std::fopen(font_path, "rb");
        if (!file) throw std::runtime_error("missing pinned preview font");
        std::fseek(file, 0, SEEK_END);
        font_bytes_.resize(std::ftell(file));
        std::rewind(file);
        bool ok = std::fread(font_bytes_.data(), 1, font_bytes_.size(), file) == font_bytes_.size();
        std::fclose(file);
        if (!ok || !stbtt_InitFont(&font_, font_bytes_.data(), 0))
            throw std::runtime_error("invalid preview font");
    }
    Color a(Color color) const { return color; }
    void drawRect(int x, int y, int w, int h, Color color) {
        for (int py = std::max(0, y); py < std::min(height, y + h); ++py)
            for (int px = std::max(0, x); px < std::min(width, x + w); ++px) pixel(px, py, color);
    }
    std::pair<u32, u32> drawString(const char *text, bool mono, int x, int y,
        float size, Color color, ssize_t max_width = 0) {
        int pen = x, baseline = y, max_x = x;
        float scale = stbtt_ScaleForPixelHeight(&font_, size);
        const unsigned char *p = reinterpret_cast<const unsigned char *>(text);
        while (*p) {
            if (max_width > 0 && max_width < pen - x) break;
            unsigned code = *p++;
            if (code >= 0xc0) {
                int extra = code < 0xe0 ? 1 : code < 0xf0 ? 2 : 3;
                code &= (1u << (6 - extra)) - 1;
                while (extra-- && *p) code = (code << 6) | (*p++ & 63);
            }
            if (code == '\n') { max_x = std::max(max_x, pen); pen = x; baseline += size; continue; }
            int advance, bearing;
            stbtt_GetCodepointHMetrics(&font_, mono ? 'W' : code, &advance, &bearing);
            advance = static_cast<int>(advance * scale);
            // Match vendored libtesla: maxWidth truncates on the next iteration.
            if (color.a) {
                int w, h, dx, dy;
                auto bitmap = stbtt_GetCodepointBitmap(&font_, scale, scale, code, &w, &h, &dx, &dy);
                for (int row = 0; row < h; ++row)
                    for (int col = 0; col < w; ++col)
                    {
                        unsigned coverage = bitmap[row * w + col] >> 4;
                        Color glyph_color = color;
                        glyph_color.a = coverage == 15 ? color.a : coverage * color.a / 15;
                        if (coverage) pixel(pen + dx + col, baseline + dy + row, glyph_color);
                    }
                stbtt_FreeBitmap(bitmap, nullptr);
            }
            pen += advance;
        }
        return {static_cast<u32>(std::max(max_x, pen) - x), static_cast<u32>(baseline - y + size)};
    }
    void save(const std::string &path) {
        FILE *file = std::fopen(path.c_str(), "wb");
        if (!file) throw std::runtime_error("cannot write overlay preview");
        std::fprintf(file, "P6\n%d %d\n255\n", width, height);
        bool ok = std::fwrite(pixels.data(), 1, pixels.size(), file) == pixels.size();
        std::fclose(file);
        if (!ok) throw std::runtime_error("incomplete overlay preview");
    }
};
} }
