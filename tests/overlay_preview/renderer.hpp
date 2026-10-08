#pragma once
// Host adapter for the drawRect/drawString surface used by the real Overlay.
// Uses the vendored stb rasterizer and pinned documentation font; no Switch APIs.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../companion/ui_emoji.h"
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
            if (code == 0xFE0E || code == 0xFE0F) continue;
            if (is_builtin_emoji(code)) {
                int em_size = static_cast<int>(size);
                int em_y = baseline - static_cast<int>(size * 0.82f);
                float s = static_cast<float>(em_size);
                if (color.a) {
                    for (int ey = 0; ey < em_size; ++ey) {
                        for (int ex = 0; ex < em_size; ++ex) {
                            int cov = 0;
                            int total_r = 0, total_g = 0, total_b = 0;
                            for (int sy = 0; sy < 3; ++sy) {
                                float v = (static_cast<float>(ey) + (static_cast<float>(sy) + 0.5f) / 3.0f) / s;
                                for (int sx = 0; sx < 3; ++sx) {
                                    float u = (static_cast<float>(ex) + (static_cast<float>(sx) + 0.5f) / 3.0f) / s;
                                    uint32_t s_rgb = 0;
                                    if (sample_emoji_color(code, u, v, &s_rgb)) {
                                        ++cov;
                                        total_r += static_cast<int>((s_rgb >> 16) & 0xff);
                                        total_g += static_cast<int>((s_rgb >> 8) & 0xff);
                                        total_b += static_cast<int>(s_rgb & 0xff);
                                    }
                                }
                            }
                            if (cov > 0) {
                                int r = total_r / cov;
                                int g = total_g / cov;
                                int b = total_b / cov;
                                int alpha = (color.a * 17) * (cov * 255 / 9) / 255;
                                int px = pen + ex;
                                int py = em_y + ey;
                                if (px >= 0 && px < width && py >= 0 && py < height) {
                                    for (int c = 0; c < 3; ++c) {
                                        auto &px_ref = pixels[(py * width + px) * 3 + c];
                                        int src_c = (c == 0 ? r : c == 1 ? g : b);
                                        px_ref = static_cast<unsigned char>((src_c * alpha + px_ref * (255 - alpha)) / 255);
                                    }
                                }
                            }
                        }
                    }
                }
                pen += static_cast<int>(size * 1.15f);
                continue;
            }
            unsigned lookup_code = code;
            if (stbtt_FindGlyphIndex(&font_, lookup_code) == 0 && code >= 0xE000 && code <= 0xE0FF) {
                switch (code) {
                case 0xE0E0: lookup_code = 'A'; break;
                case 0xE0E1: lookup_code = 'B'; break;
                case 0xE0E2: lookup_code = 'X'; break;
                case 0xE0E3: lookup_code = 'Y'; break;
                case 0xE0E4: lookup_code = 'L'; break;
                case 0xE0E5: lookup_code = 'R'; break;
                case 0xE0E6: lookup_code = 'Z'; break;
                case 0xE0E7: lookup_code = 'Z'; break;
                case 0xE0EB: lookup_code = '+'; break;
                case 0xE0EC: lookup_code = '-'; break;
                default: break;
                }
            }
            int advance, bearing;
            stbtt_GetCodepointHMetrics(&font_, mono ? 'W' : lookup_code, &advance, &bearing);
            advance = static_cast<int>(advance * scale);
            // Match vendored libtesla: maxWidth truncates on the next iteration.
            if (color.a) {
                int w, h, dx, dy;
                auto bitmap = stbtt_GetCodepointBitmap(&font_, scale, scale, lookup_code, &w, &h, &dx, &dy);
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
