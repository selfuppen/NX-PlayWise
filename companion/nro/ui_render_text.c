#include "ui_render_internal.h"
#include "../ui_language.h"

static uint32_t ui_decode_utf8(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    if (s[0] < 0x80) {
        *text += 1;
        return s[0];
    }
    if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
        *text += 2;
        return ((uint32_t)(s[0] & 0x1f) << 6) | (uint32_t)(s[1] & 0x3f);
    }
    if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
        *text += 3;
        return ((uint32_t)(s[0] & 0x0f) << 12) |
               ((uint32_t)(s[1] & 0x3f) << 6) |
               (uint32_t)(s[2] & 0x3f);
    }
    if ((s[0] & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 &&
        (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) {
        *text += 4;
        return ((uint32_t)(s[0] & 0x07) << 18) |
               ((uint32_t)(s[1] & 0x3f) << 12) |
               ((uint32_t)(s[2] & 0x3f) << 6) |
               (uint32_t)(s[3] & 0x3f);
    }
    *text += 1;
    return '?';
}

/* Glyph 位图缓存：键为码点与像素字号，命中后整帧不再触发 FreeType 渲染。
 * 采用开放寻址；占用超过阈值时整体清空（单帧工作集远小于容量，极少触发）。 */
#define UI_GLYPH_CACHE_SIZE 1024
#define UI_GLYPH_CACHE_LIMIT (UI_GLYPH_CACHE_SIZE * 3 / 4)

typedef struct {
    uint32_t codepoint;
    uint32_t size;
    int16_t bearing_x;
    int16_t bearing_y;
    int16_t advance;
    uint16_t width;
    uint16_t height;
    bool is_color;
    uint8_t *bitmap;
} UiGlyphEntry;

static UiGlyphEntry g_glyph_cache[UI_GLYPH_CACHE_SIZE];
static int g_glyph_cache_count;
int g_font_pixel_size = -1;

static uint32_t ui_glyph_hash(uint32_t codepoint, uint32_t size)
{
    uint32_t hash = codepoint * 0x9E3779B1u;
    hash ^= size * 0x85EBCA6Bu;
    hash ^= hash >> 15;
    hash *= 0x2545F491u;
    hash ^= hash >> 13;
    return hash & (UI_GLYPH_CACHE_SIZE - 1);
}

void ui_glyph_cache_clear(void)
{
    int i;
    for (i = 0; i < UI_GLYPH_CACHE_SIZE; ++i) {
        free(g_glyph_cache[i].bitmap);
        g_glyph_cache[i].bitmap = NULL;
        g_glyph_cache[i].codepoint = 0;
        g_glyph_cache[i].size = 0;
        g_glyph_cache[i].is_color = false;
    }
    g_glyph_cache_count = 0;
}

#include "../ui_emoji.h"

static uint32_t *ui_render_builtin_emoji(uint32_t codepoint, int size, bool bold,
                                         int *out_w, int *out_h, int *out_bearing_x,
                                         int *out_bearing_y, int *out_advance)
{
    int extra = bold ? 1 : 0;
    int w = size + extra;
    int h = size;
    uint32_t *copy = (uint32_t *)malloc((size_t)w * h * sizeof(uint32_t));
    if (!copy) {
        return NULL;
    }
    float s = (float)size;
    for (int y = 0; y < size; ++y) {
        uint32_t *line = copy + (size_t)y * w;
        for (int x = 0; x < size; ++x) {
            int cov = 0;
            int total_r = 0, total_g = 0, total_b = 0;
            for (int sy = 0; sy < 3; ++sy) {
                float v = ((float)y + ((float)sy + 0.5f) / 3.0f) / s;
                for (int sx = 0; sx < 3; ++sx) {
                    float u = ((float)x + ((float)sx + 0.5f) / 3.0f) / s;
                    uint32_t s_rgb = 0;
                    if (sample_emoji_color(codepoint, u, v, &s_rgb)) {
                        ++cov;
                        total_r += (int)((s_rgb >> 16) & 0xff);
                        total_g += (int)((s_rgb >> 8) & 0xff);
                        total_b += (int)(s_rgb & 0xff);
                    }
                }
            }
            if (cov > 0) {
                int avg_r = total_r / cov;
                int avg_g = total_g / cov;
                int avg_b = total_b / cov;
                uint8_t alpha = (uint8_t)((cov * 255 + 4) / 9);
                uint32_t packed = pack_rgb(((uint32_t)avg_r << 16) | ((uint32_t)avg_g << 8) | (uint32_t)avg_b);
                line[x] = (packed & 0x00ffffffu) | ((uint32_t)alpha << 24);
            } else {
                line[x] = 0;
            }
        }
        if (bold) {
            line[size] = line[size - 1];
            for (int x = size - 1; x > 0; --x) {
                uint8_t a_cur = (uint8_t)(line[x] >> 24);
                uint8_t a_prev = (uint8_t)(line[x - 1] >> 24);
                if (a_prev > a_cur) {
                    line[x] = line[x - 1];
                }
            }
        }
    }
    *out_w = w;
    *out_h = h;
    *out_bearing_x = 0;
    *out_bearing_y = (size * 7 + 4) / 8;
    *out_advance = size + extra;
    return copy;
}

static bool set_font_size(int size)
{
    if (size <= 0) {
        return false;
    }
    if (size == g_font_pixel_size) {
        return true;
    }
    if (g_ui.font_ready) {
        if (FT_Set_Pixel_Sizes(g_ui.face, 0, (FT_UInt)size) != 0) {
            return false;
        }
        if (g_ui.standard_face &&
            FT_Set_Pixel_Sizes(g_ui.standard_face, 0, (FT_UInt)size) != 0) return false;
        if (g_ui.traditional_face &&
            FT_Set_Pixel_Sizes(g_ui.traditional_face, 0, (FT_UInt)size) != 0) return false;
        if (g_ui.emoji_ready && g_ui.emoji_face &&
            FT_Set_Pixel_Sizes(g_ui.emoji_face, 0, (FT_UInt)size) != 0) return false;
    }
    g_font_pixel_size = size;
    return true;
}

/* 命中返回缓存项；未命中渲染一次并写入缓存。失败（缺字等）返回 NULL，不缓存。
 * bold 走独立缓存键，位图做水平向右单向平滑加粗 1px（高度与垂直笔画间隙保持不变，避免复杂汉字横画粘连）。 */
static const UiGlyphEntry *ui_glyph_fetch(uint32_t codepoint, int size, bool bold)
{
    uint32_t mask = UI_GLYPH_CACHE_SIZE - 1;
    uint32_t size_key = (uint32_t)size | (bold ? 0x10000u : 0u);
    uint32_t slot = ui_glyph_hash(codepoint, size_key);
    UiGlyphEntry *entry;
    if (!set_font_size(size)) {
        return NULL;
    }
    for (;;) {
        entry = &g_glyph_cache[slot];
        if (entry->codepoint == 0) break;
        if (entry->codepoint == codepoint && entry->size == size_key) return entry;
        slot = (slot + 1) & mask;
    }
    if (g_glyph_cache_count >= UI_GLYPH_CACHE_LIMIT) {
        ui_glyph_cache_clear();
        slot = ui_glyph_hash(codepoint, size_key);
        entry = &g_glyph_cache[slot];
    }
    /* 变体选择符（VS16/VS15）直接消费为 0 宽占位项，消除豆腐块。 */
    if (codepoint == 0xFE0F || codepoint == 0xFE0E) {
        entry->codepoint = codepoint;
        entry->size = size_key;
        entry->bearing_x = 0;
        entry->bearing_y = 0;
        entry->advance = 0;
        entry->width = 0;
        entry->height = 0;
        entry->is_color = false;
        entry->bitmap = NULL;
        ++g_glyph_cache_count;
        return entry;
    }
    /* 程序化抗锯齿圆点列表符号回退 (Bullet: U+2022, 实心圆: U+25CF, 连字点: U+2027)
     * Switch 共享中文字体缺少 U+2022，在此光栅化平滑圆点，继承文字颜色与粗细。 */
    if (codepoint == 0x2022 || codepoint == 0x25CF || codepoint == 0x2027) {
        float r = (float)size * 0.16f;
        if (r < 1.6f) r = 1.6f;
        if (bold) r *= 1.15f;
        int d = (int)ceilf(r * 2.0f + 2.0f);
        int w = d, h = d;
        uint8_t *dot_bmp = (uint8_t *)malloc((size_t)w * h);
        if (dot_bmp) {
            float cx = (float)w / 2.0f;
            float cy = (float)h / 2.0f;
            int y_idx, x_idx;
            for (y_idx = 0; y_idx < h; ++y_idx) {
                for (x_idx = 0; x_idx < w; ++x_idx) {
                    float dx = (float)x_idx + 0.5f - cx;
                    float dy = (float)y_idx + 0.5f - cy;
                    float dist = sqrtf(dx * dx + dy * dy);
                    float cov = (r + 0.5f - dist);
                    if (cov <= 0.0f) dot_bmp[y_idx * w + x_idx] = 0;
                    else if (cov >= 1.0f) dot_bmp[y_idx * w + x_idx] = 255;
                    else dot_bmp[y_idx * w + x_idx] = (uint8_t)(cov * 255.0f);
                }
            }
            int adv = (int)((float)size * 0.52f + 0.5f);
            if (adv < w + 2) adv = w + 2;
            int center_above_base = (int)((float)size * 0.38f + 0.5f);
            entry->codepoint = codepoint;
            entry->size = size_key;
            entry->bearing_x = (int16_t)((adv - w) / 2);
            entry->bearing_y = (int16_t)(center_above_base + (int)cy);
            entry->advance = (int16_t)adv;
            entry->width = (uint16_t)w;
            entry->height = (uint16_t)h;
            entry->is_color = false;
            entry->bitmap = dot_bmp;
            ++g_glyph_cache_count;
            return entry;
        }
    }
    /* 预置常用 Emoji 矢量回退：自带丰富固定色彩。 */
    if (is_builtin_emoji(codepoint)) {
        int em_w = 0, em_h = 0, em_bx = 0, em_by = 0, em_adv = 0;
        uint32_t *em_bmp = ui_render_builtin_emoji(codepoint, size, bold,
                                                   &em_w, &em_h, &em_bx, &em_by, &em_adv);
        if (em_bmp) {
            entry->codepoint = codepoint;
            entry->size = size_key;
            entry->bearing_x = (int16_t)em_bx;
            entry->bearing_y = (int16_t)em_by;
            entry->advance = (int16_t)em_adv;
            entry->width = (uint16_t)em_w;
            entry->height = (uint16_t)em_h;
            entry->is_color = true;
            entry->bitmap = (uint8_t *)em_bmp;
            ++g_glyph_cache_count;
            return entry;
        }
    }
    /* 单角引号/操作微指示符回退 (›: U+203A, ‹: U+2039)
     * Switch 共享中文字体缺少 U+203A/U+2039，在此光栅化平滑矢量微尖括号，继承文字粗细与高度。 */
    if (codepoint == 0x203A || codepoint == 0x2039) {
        float h_f = (float)size * 0.52f;
        if (h_f < 6.0f) h_f = 6.0f;
        float w_f = h_f * 0.55f;
        float th = (float)size * 0.09f;
        if (th < 1.4f) th = 1.4f;
        if (bold) th *= 1.25f;
        int w = (int)ceilf(w_f + th * 2.0f + 2.0f);
        int h = (int)ceilf(h_f + th * 2.0f + 2.0f);
        uint8_t *bmp = (uint8_t *)calloc(1, (size_t)w * h);
        if (bmp) {
            float cx = (float)w / 2.0f;
            float cy = (float)h / 2.0f;
            float x_tip = (codepoint == 0x203A) ? (cx + w_f * 0.5f) : (cx - w_f * 0.5f);
            float x_base = (codepoint == 0x203A) ? (cx - w_f * 0.5f) : (cx + w_f * 0.5f);
            float y_top = cy - h_f * 0.5f;
            float y_mid = cy;
            float y_bot = cy + h_f * 0.5f;
            int y_idx, x_idx;
            for (y_idx = 0; y_idx < h; ++y_idx) {
                for (x_idx = 0; x_idx < w; ++x_idx) {
                    float px = (float)x_idx + 0.5f;
                    float py = (float)y_idx + 0.5f;
                    float d1 = sqrtf(dist_to_segment_sq(px, py, x_base, y_top, x_tip, y_mid));
                    float d2 = sqrtf(dist_to_segment_sq(px, py, x_tip, y_mid, x_base, y_bot));
                    float dist = d1 < d2 ? d1 : d2;
                    float cov = (th * 0.5f + 0.5f - dist);
                    if (cov <= 0.0f) bmp[y_idx * w + x_idx] = 0;
                    else if (cov >= 1.0f) bmp[y_idx * w + x_idx] = 255;
                    else bmp[y_idx * w + x_idx] = (uint8_t)(cov * 255.0f);
                }
            }
            int adv = (int)((float)size * 0.42f + 0.5f);
            if (adv < w + 1) adv = w + 1;
            int center_above_base = (int)((float)size * 0.32f + 0.5f);
            entry->codepoint = codepoint;
            entry->size = size_key;
            entry->bearing_x = (int16_t)((adv - w) / 2);
            entry->bearing_y = (int16_t)(center_above_base + (int)cy);
            entry->advance = (int16_t)adv;
            entry->width = (uint16_t)w;
            entry->height = (uint16_t)h;
            entry->is_color = false;
            entry->bitmap = bmp;
            ++g_glyph_cache_count;
            return entry;
        }
    }
    FT_Face face = g_ui.face;
    PtcUiLanguagePreference lang = ptc_ui_language_get_resolved();
    if (lang == PTC_UI_LANGUAGE_ENGLISH && g_ui.standard_face &&
        FT_Get_Char_Index(g_ui.standard_face, codepoint) != 0)
        face = g_ui.standard_face;
    else if (lang == PTC_UI_LANGUAGE_TRADITIONAL && g_ui.traditional_face &&
        FT_Get_Char_Index(g_ui.traditional_face, codepoint) != 0)
        face = g_ui.traditional_face;
    else if (g_ui.standard_face && codepoint < 0x80 &&
        FT_Get_Char_Index(g_ui.standard_face, codepoint) != 0)
        face = g_ui.standard_face;
    else if (g_ui.font_ready && FT_Get_Char_Index(g_ui.face, codepoint) == 0 &&
        g_ui.traditional_face && FT_Get_Char_Index(g_ui.traditional_face, codepoint) != 0)
        face = g_ui.traditional_face;
    else if (g_ui.emoji_ready && g_ui.emoji_face && FT_Get_Char_Index(g_ui.emoji_face, codepoint) != 0 &&
        (!g_ui.font_ready || FT_Get_Char_Index(g_ui.face, codepoint) == 0))
        face = g_ui.emoji_face;
    if (!g_ui.font_ready || FT_Get_Char_Index(face, codepoint) == 0 ||
        FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0) {
        return NULL;
    }
    {
        FT_GlyphSlot glyph = face->glyph;
        uint8_t *copy = NULL;
        int extra = (bold && size >= 15) ? 1 : 0;
        if (glyph->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA && glyph->bitmap.rows > 0 &&
            glyph->bitmap.width > 0) {
            int width = (int)glyph->bitmap.width;
            int height = (int)glyph->bitmap.rows;
            uint32_t *color_copy = (uint32_t *)malloc((size_t)width * height * sizeof(uint32_t));
            if (color_copy) {
                for (int row = 0; row < height; ++row) {
                    const uint8_t *src_line = glyph->bitmap.buffer + (size_t)row * glyph->bitmap.pitch;
                    uint32_t *dst_line = color_copy + (size_t)row * width;
                    for (int col = 0; col < width; ++col) {
                        uint8_t b = src_line[col * 4 + 0];
                        uint8_t g = src_line[col * 4 + 1];
                        uint8_t r = src_line[col * 4 + 2];
                        uint8_t a = src_line[col * 4 + 3];
                        uint32_t packed = pack_rgb(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
                        dst_line[col] = (packed & 0x00ffffffu) | ((uint32_t)a << 24);
                    }
                }
                entry->codepoint = codepoint;
                entry->size = size_key;
                entry->bearing_x = (int16_t)glyph->bitmap_left;
                entry->bearing_y = (int16_t)glyph->bitmap_top;
                entry->advance = (int16_t)(glyph->advance.x >> 6);
                entry->width = (uint16_t)width;
                entry->height = (uint16_t)height;
                entry->is_color = true;
                entry->bitmap = (uint8_t *)color_copy;
                ++g_glyph_cache_count;
                return entry;
            }
        }
        if (glyph->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY && glyph->bitmap.rows > 0 &&
            glyph->bitmap.width > 0) {
            int width = (int)glyph->bitmap.width + extra;
            int height = (int)glyph->bitmap.rows;
            copy = (uint8_t *)malloc((size_t)width * height);
            if (copy) {
                int row;
                for (row = 0; row < height; ++row) {
                    uint8_t *out_line = copy + (size_t)row * width;
                    const uint8_t *src_line = glyph->bitmap.buffer + (size_t)row * glyph->bitmap.pitch;
                    if (!bold || size < 15) {
                        memcpy(out_line, src_line, (size_t)glyph->bitmap.width);
                        continue;
                    }
                    /* 水平单向抗锯齿平滑加粗：高度不变，横画间隙绝不粘连；
                     * 宽度仅向右扩展 1px，竖画与笔画自然加厚。 */
                    int column;
                    for (column = 0; column < width; ++column) {
                        uint8_t c0 = (column < (int)glyph->bitmap.width) ? src_line[column] : 0;
                        uint8_t cl = (column > 0) ? src_line[column - 1] : 0;
                        out_line[column] = c0 > cl ? c0 : cl;
                    }
                }
                entry->bearing_x = (int16_t)glyph->bitmap_left;
                entry->bearing_y = (int16_t)glyph->bitmap_top;
            }
        }
        if (!copy) {
            /* 零尺寸（空格）或非灰度位图也占位缓存，避免每帧重复加载。 */
            entry->codepoint = codepoint;
            entry->size = size_key;
            entry->bearing_x = 0;
            entry->bearing_y = 0;
            entry->advance = (int16_t)(glyph->advance.x >> 6);
            entry->width = 0;
            entry->height = 0;
            entry->is_color = false;
            entry->bitmap = NULL;
            ++g_glyph_cache_count;
            return entry;
        }
        entry->codepoint = codepoint;
        entry->size = size_key;
        entry->advance = (int16_t)(glyph->advance.x >> 6);
        entry->width = (uint16_t)((int)glyph->bitmap.width + extra);
        entry->height = (uint16_t)glyph->bitmap.rows;
        entry->is_color = false;
        entry->bitmap = copy;
        ++g_glyph_cache_count;
        return entry;
    }
}

int measure_text(const char *text, int size)
{
    int width = 0;
    char localized[4096];
    const char *cursor;
    if (!text) {
        return 0;
    }
    cursor = ptc_ui_localize(text, localized, sizeof(localized));
    while (*cursor) {
        uint32_t codepoint = ui_decode_utf8(&cursor);
        const UiGlyphEntry *entry = ui_glyph_fetch(codepoint, size, false);
        if (entry) {
            width += entry->advance;
        }
    }
    return width;
}

static void draw_text_impl(uint32_t *pixels, uint32_t stride, int x, int baseline, const char *text, int size, uint32_t color, bool bold)
{
    char localized[4096];
    const char *cursor;
    int pen_x = x;
    uint32_t resolved = resolve_color(color);
    if (!text) {
        return;
    }
    cursor = ptc_ui_localize(text, localized, sizeof(localized));
    while (*cursor) {
        uint32_t codepoint = ui_decode_utf8(&cursor);
        const UiGlyphEntry *entry = ui_glyph_fetch(codepoint, size, bold);
        int row;
        if (!entry) {
            continue;
        }
        if (entry->is_color) {
            const uint32_t *src_pixels = (const uint32_t *)entry->bitmap;
            for (row = 0; row < entry->height; ++row) {
                int column;
                for (column = 0; column < entry->width; ++column) {
                    uint32_t px = src_pixels[(size_t)row * entry->width + column];
                    uint8_t a = (uint8_t)(px >> 24);
                    if (a > 0) {
                        blend_pixel(
                            pixels,
                            stride,
                            pen_x + entry->bearing_x + column,
                            baseline - entry->bearing_y + row,
                            px & 0x00FFFFFFu,
                            a);
                    }
                }
            }
        } else {
            for (row = 0; row < entry->height; ++row) {
                const uint8_t *line = entry->bitmap + (size_t)row * entry->width;
                int column;
                for (column = 0; column < entry->width; ++column) {
                    if (line[column]) {
                        blend_pixel(
                            pixels,
                            stride,
                            pen_x + entry->bearing_x + column,
                            baseline - entry->bearing_y + row,
                            resolved,
                            line[column]);
                    }
                }
            }
        }
        pen_x += entry->advance;
    }
}

/* 粗体走缓存的膨胀变体，单次绘制，无位移重影。 */
void draw_text(uint32_t *pixels, uint32_t stride, int x, int baseline, const char *text, int size, uint32_t color)
{
    draw_text_impl(pixels, stride, x, baseline, text, size, color, false);
}

void draw_text_bold(uint32_t *pixels, uint32_t stride, int x, int baseline, const char *text, int size, uint32_t color)
{
    draw_text_impl(pixels, stride, x, baseline, text, size, color, true);
}

void draw_text_center(uint32_t *pixels, uint32_t stride, UiRect rect, const char *text, int size, uint32_t color)
{
    if (!text || !*text) return;
    int cur_size = size;
    int width = measure_text(text, cur_size);
    while (width > rect.width - 6 && cur_size > 11) {
        cur_size--;
        width = measure_text(text, cur_size);
    }
    char fitted[256];
    const char *to_draw = text;
    if (width > rect.width - 4) {
        fit_text(fitted, sizeof(fitted), text, cur_size, rect.width - 4);
        to_draw = fitted;
        width = measure_text(to_draw, cur_size);
    }
    int baseline = rect.y + (rect.height + cur_size) / 2 - 3;
    draw_text(pixels, stride, rect.x + (rect.width - width) / 2, baseline, to_draw, cur_size, color);
}

void fit_text(char *out, size_t out_size, const char *text, int size, int max_width)
{
    char source_copy[512];
    char localized[4096];
    const char *source = text ? text : "";
    const char *cursor;
    const char *end;
    size_t copy_size;
    int width = 0;
    bool truncated = false;
    if (!out || out_size == 0) {
        return;
    }
    if (out == text) {
        snprintf(source_copy, sizeof(source_copy), "%s", source);
        source = source_copy;
    }
    source = ptc_ui_localize(source, localized, sizeof(localized));
    if (measure_text(source, size) <= max_width) {
        snprintf(out, out_size, "%s", source);
        return;
    }
    cursor = source;
    end = cursor;
    out[0] = '\0';
    int ellipsis_width = measure_text("...", size);
    if (ellipsis_width < 10) ellipsis_width = 10;
    while (*cursor) {
        const char *next = cursor;
        uint32_t codepoint = ui_decode_utf8(&next);
        const UiGlyphEntry *entry = ui_glyph_fetch(codepoint, size, false);
        int advance = 0;
        if (entry) {
            advance = entry->advance;
        }
        if (width + advance > max_width || (*next && width + advance + ellipsis_width > max_width)) {
            truncated = true;
            break;
        }
        width += advance;
        cursor = next;
        end = cursor;
    }
    copy_size = (size_t)(end - source);
    if (copy_size >= out_size) {
        copy_size = out_size - 1;
        while (copy_size > 0 && ((unsigned char)source[copy_size] & 0xc0) == 0x80) {
            --copy_size;
        }
        if (copy_size > 0 && ((unsigned char)source[copy_size] & 0x80) != 0) {
            --copy_size;
        }
        truncated = true;
    }
    memcpy(out, source, copy_size);
    out[copy_size] = '\0';
    if (truncated && copy_size + 3 < out_size) {
        strcat(out, "...");
    }
}

int draw_wrapped_text(
    uint32_t *pixels,
    uint32_t stride,
    int x,
    int baseline,
    const char *text,
    int size,
    int max_width,
    int line_height,
    int max_lines,
    uint32_t color)
{
    char localized[4096];
    const char *cursor = ptc_ui_localize(text ? text : "", localized, sizeof(localized));
    int line = 0;
    while (*cursor && line < max_lines) {
        const char *end = cursor;
        const char *newline = strchr(cursor, '\n');
        const char *last_space = NULL;
        int width = 0;
        char buffer[PTC_PAIRING_BASE_URL_MAX_LEN + 1];
        while (*end && end != newline) {
            if (*end == ' ') last_space = end;
            const char *next = end;
            uint32_t codepoint = ui_decode_utf8(&next);
            int advance = 0;
            const UiGlyphEntry *entry = ui_glyph_fetch(codepoint, size, false);
            if (entry) {
                advance = (int)entry->advance;
            }
            if (end > cursor && width + advance > max_width) {
                if (last_space && last_space > cursor) {
                    end = last_space;
                }
                break;
            }
            width += advance;
            end = next;
        }
        if (end == cursor) {
            const char *next = cursor;
            (void)ui_decode_utf8(&next);
            end = next;
        }
        {
            size_t bytes = (size_t)(end - cursor);
            if (bytes >= sizeof(buffer)) bytes = sizeof(buffer) - 1;
            memcpy(buffer, cursor, bytes);
            buffer[bytes] = '\0';
        }
        draw_text(pixels, stride, x, baseline + line * line_height, buffer, size, color);
        cursor = *end == '\n' ? end + 1 : (*end == ' ' ? end + 1 : end);
        ++line;
    }
    return baseline + line * line_height;
}
