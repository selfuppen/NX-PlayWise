#include "ui_render_internal.h"

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

static bool is_builtin_emoji(uint32_t codepoint)
{
    switch (codepoint) {
    case 0x1F319: /* 🌙 Moon */
    case 0x1F381: /* 🎁 Gift */
    case 0x1F3AF: /* 🎯 Target / Bullseye */
    case 0x1F6E1: /* 🛡️ Shield */
    case 0x26A0:  /* ⚠️ Warning Triangle */
    case 0x2713:  /* ✓ Checkmark */
    case 0x2714:  /* ✔️ Heavy Checkmark */
    case 0x2715:  /* ✕ Cross */
    case 0x2716:  /* ✖ Heavy Cross */
    case 0x274C:  /* ❌ Cross Mark */
    case 0x23F0:  /* ⏰ Alarm Clock */
    case 0x23F3:  /* ⏳ Hourglass Flowing */
    case 0x231B:  /* ⌛ Hourglass */
    case 0x1F512: /* 🔒 Lock */
    case 0x1F513: /* 🔓 Unlock */
    case 0x2B50:  /* ⭐ Star */
    case 0x1F31F: /* 🌟 Glowing Star */
    case 0x1F4C5: /* 📅 Calendar */
    case 0x1F4A1: /* 💡 Lightbulb */
    case 0x1F525: /* 🔥 Fire */
    case 0x1F33F: /* 🌿 Herb / Seedling */
    case 0x2139:  /* ℹ️ Information */
        return true;
    default:
        return false;
    }
}

static float dist_to_segment_sq(float px, float py, float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1, dy = y2 - y1;
    float l2 = dx * dx + dy * dy;
    float t = ((px - x1) * dx + (py - y1) * dy) / (l2 > 1e-6f ? l2 : 1e-6f);
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    float qx = x1 + t * dx, qy = y1 + t * dy;
    return (px - qx) * (px - qx) + (py - qy) * (py - qy);
}

static bool is_in_leaf(float u, float v, float cx, float cy, float len, float wid, float cos_a, float sin_a)
{
    float lx = (u - cx) * cos_a + (v - cy) * sin_a;
    float ly = -(u - cx) * sin_a + (v - cy) * cos_a;
    float t = lx / len;
    if (fabsf(t) <= 1.0f) {
        float max_w = wid * (1.0f - t * t);
        if (fabsf(ly) <= max_w) return true;
    }
    return false;
}

static bool sample_emoji_color(uint32_t codepoint, float u, float v, uint32_t *out_rgb)
{
    switch (codepoint) {
    case 0x2139: { /* ℹ️ Information */
        float dx = u - 0.50f, dy = v - 0.50f;
        if (dx * dx + dy * dy <= 0.40f * 0.40f) {
            float dot_dy = v - 0.30f;
            if (dx * dx + dot_dy * dot_dy <= 0.065f * 0.065f) {
                *out_rgb = 0xFFFFFFu;
                return true;
            }
            if (u >= 0.43f && u <= 0.57f && v >= 0.43f && v <= 0.72f) {
                *out_rgb = 0xFFFFFFu;
                return true;
            }
            if (u >= 0.37f && u <= 0.50f && v >= 0.43f && v <= 0.50f) {
                *out_rgb = 0xFFFFFFu;
                return true;
            }
            if (u >= 0.37f && u <= 0.63f && v >= 0.66f && v <= 0.73f) {
                *out_rgb = 0xFFFFFFu;
                return true;
            }
            *out_rgb = 0x3B82F6u;
            return true;
        }
        return false;
    }
    case 0x1F33F: { /* 🌿 Herb / Sprig */
        float s1 = dist_to_segment_sq(u, v, 0.24f, 0.82f, 0.40f, 0.62f);
        float s2 = dist_to_segment_sq(u, v, 0.40f, 0.62f, 0.56f, 0.42f);
        float s3 = dist_to_segment_sq(u, v, 0.56f, 0.42f, 0.70f, 0.22f);
        float s_min = s1 < s2 ? s1 : s2;
        if (s3 < s_min) s_min = s3;

        if (is_in_leaf(u, v, 0.76f, 0.16f, 0.14f, 0.07f, 0.7660f, -0.6428f)) {
            *out_rgb = 0x4ADE80u;
            return true;
        }
        if (is_in_leaf(u, v, 0.42f, 0.36f, 0.15f, 0.08f, -0.8660f, 0.5000f)) {
            *out_rgb = 0x22C55Eu;
            return true;
        }
        if (is_in_leaf(u, v, 0.74f, 0.38f, 0.16f, 0.085f, 0.9659f, -0.2588f)) {
            *out_rgb = 0x22C55Eu;
            return true;
        }
        if (is_in_leaf(u, v, 0.25f, 0.58f, 0.16f, 0.085f, -0.7660f, 0.6428f)) {
            *out_rgb = 0x16A34Au;
            return true;
        }
        if (is_in_leaf(u, v, 0.58f, 0.58f, 0.16f, 0.085f, 0.9848f, -0.1736f)) {
            *out_rgb = 0x16A34Au;
            return true;
        }
        if (s_min <= 0.038f * 0.038f) {
            *out_rgb = 0x15803Du;
            return true;
        }
        return false;
    }
    case 0x1F319: { /* 🌙 Crescent Moon */
        float dx_out = u - 0.48f, dy_out = v - 0.50f;
        float dx_in  = u - 0.64f, dy_in  = v - 0.38f;
        if ((dx_out * dx_out + dy_out * dy_out <= 0.42f * 0.42f) &&
            (dx_in * dx_in + dy_in * dy_in >= 0.36f * 0.36f)) {
            float d2 = dx_out * dx_out + dy_out * dy_out;
            *out_rgb = (d2 > 0.38f * 0.38f) ? 0xF59E0Bu : 0xF6C338u;
            return true;
        }
        return false;
    }
    case 0x1F381: { /* 🎁 Wrapped Present */
        float d1 = (u - 0.36f) * (u - 0.36f) + (v - 0.21f) * (v - 0.21f);
        if (d1 >= 0.05f * 0.05f && d1 <= 0.13f * 0.13f && v <= 0.30f) {
            *out_rgb = 0xF5C518u;
            return true;
        }
        float d2 = (u - 0.64f) * (u - 0.64f) + (v - 0.21f) * (v - 0.21f);
        if (d2 >= 0.05f * 0.05f && d2 <= 0.13f * 0.13f && v <= 0.30f) {
            *out_rgb = 0xF5C518u;
            return true;
        }
        float dk = (u - 0.50f) * (u - 0.50f) + (v - 0.27f) * (v - 0.27f);
        if (dk <= 0.06f * 0.06f) {
            *out_rgb = 0xF5C518u;
            return true;
        }
        if (u >= 0.16f && u <= 0.84f && v >= 0.30f && v <= 0.42f) {
            *out_rgb = (u >= 0.45f && u <= 0.55f) ? 0xF5C518u : 0xDE3535u;
            return true;
        }
        if (u >= 0.22f && u <= 0.78f && v >= 0.44f && v <= 0.88f) {
            if ((u >= 0.45f && u <= 0.55f) || (v >= 0.61f && v <= 0.71f)) {
                *out_rgb = 0xF5C518u;
            } else {
                *out_rgb = 0xE83E48u;
            }
            return true;
        }
        return false;
    }
    case 0x1F3AF: { /* 🎯 Target */
        float d2 = (u - 0.50f) * (u - 0.50f) + (v - 0.50f) * (v - 0.50f);
        if (d2 <= 0.12f * 0.12f) {
            *out_rgb = 0xDE3535u;
            return true;
        }
        if (d2 >= 0.22f * 0.22f && d2 <= 0.30f * 0.30f) {
            *out_rgb = 0xFFFFFFu;
            return true;
        }
        if (d2 >= 0.40f * 0.40f && d2 <= 0.48f * 0.48f) {
            *out_rgb = 0xDE3535u;
            return true;
        }
        return false;
    }
    case 0x1F6E1: { /* 🛡️ Shield */
        if (u >= 0.18f && u <= 0.82f && v >= 0.18f && v <= 0.86f) {
            float arch = 0.20f - 0.04f * (1.0f - ((u - 0.50f) / 0.32f) * ((u - 0.50f) / 0.32f));
            if (v >= arch) {
                bool in_shield = false;
                if (v <= 0.48f) {
                    in_shield = true;
                } else {
                    float dy = (v - 0.48f) / 0.38f;
                    float max_dx = (1.0f - dy * dy) * 0.32f;
                    if (fabsf(u - 0.50f) <= max_dx) in_shield = true;
                }
                if (in_shield) {
                    if ((u >= 0.46f && u <= 0.54f && v >= 0.22f && v <= 0.76f) ||
                        (u >= 0.26f && u <= 0.74f && v >= 0.37f && v <= 0.45f)) {
                        *out_rgb = 0xF5C518u;
                    } else {
                        *out_rgb = 0x3B82F6u;
                    }
                    return true;
                }
            }
        }
        return false;
    }
    case 0x26A0: { /* ⚠️ Warning Triangle */
        if (v >= 0.16f && v <= 0.86f) {
            float max_w = (v - 0.16f) / 0.70f * 0.40f;
            if (fabsf(u - 0.50f) <= max_w) {
                if (fabsf(u - 0.50f) <= 0.05f && v >= 0.38f && v <= 0.62f) {
                    *out_rgb = 0x18181Bu;
                    return true;
                }
                if ((u - 0.50f) * (u - 0.50f) + (v - 0.74f) * (v - 0.74f) <= 0.05f * 0.05f) {
                    *out_rgb = 0x18181Bu;
                    return true;
                }
                *out_rgb = 0xF59E0Bu;
                return true;
            }
        }
        return false;
    }
    case 0x2713:
    case 0x2714: { /* ✓ Checkmark */
        if (dist_to_segment_sq(u, v, 0.18f, 0.54f, 0.40f, 0.76f) <= 0.07f * 0.07f ||
            dist_to_segment_sq(u, v, 0.40f, 0.76f, 0.82f, 0.24f) <= 0.07f * 0.07f) {
            *out_rgb = 0x10B981u;
            return true;
        }
        return false;
    }
    case 0x2715:
    case 0x2716:
    case 0x274C: { /* ✕ Cross */
        if (dist_to_segment_sq(u, v, 0.24f, 0.24f, 0.76f, 0.76f) <= 0.07f * 0.07f ||
            dist_to_segment_sq(u, v, 0.76f, 0.24f, 0.24f, 0.76f) <= 0.07f * 0.07f) {
            *out_rgb = 0xEF4444u;
            return true;
        }
        return false;
    }
    case 0x23F0: { /* ⏰ Alarm Clock */
        float d2 = (u - 0.50f) * (u - 0.50f) + (v - 0.54f) * (v - 0.54f);
        float db1 = (u - 0.26f) * (u - 0.26f) + (v - 0.26f) * (v - 0.26f);
        if (db1 >= 0.04f * 0.04f && db1 <= 0.10f * 0.10f && u + v < 0.54f) {
            *out_rgb = 0xEF4444u;
            return true;
        }
        float db2 = (u - 0.74f) * (u - 0.74f) + (v - 0.26f) * (v - 0.26f);
        if (db2 >= 0.04f * 0.04f && db2 <= 0.10f * 0.10f && v - u < -0.20f) {
            *out_rgb = 0xEF4444u;
            return true;
        }
        if ((u >= 0.24f && u <= 0.32f && v >= 0.84f && v <= 0.92f) ||
            (u >= 0.68f && u <= 0.76f && v >= 0.84f && v <= 0.92f)) {
            *out_rgb = 0x64748Bu;
            return true;
        }
        if (d2 >= 0.30f * 0.30f && d2 <= 0.38f * 0.38f) {
            *out_rgb = 0xEF4444u;
            return true;
        }
        if (d2 < 0.30f * 0.30f) {
            if (d2 <= 0.06f * 0.06f ||
                (u >= 0.47f && u <= 0.53f && v >= 0.26f && v <= 0.54f) ||
                (u >= 0.50f && u <= 0.68f && v >= 0.51f && v <= 0.57f)) {
                *out_rgb = 0x1F2937u;
            } else {
                *out_rgb = 0xFFFFFFu;
            }
            return true;
        }
        return false;
    }
    case 0x23F3:
    case 0x231B: { /* ⏳ Hourglass */
        if ((v >= 0.16f && v <= 0.24f && u >= 0.22f && u <= 0.78f) ||
            (v >= 0.76f && v <= 0.84f && u >= 0.22f && u <= 0.78f)) {
            *out_rgb = 0x854D0Eu;
            return true;
        }
        if (v > 0.24f && v < 0.76f) {
            float dy = fabsf(v - 0.50f) / 0.26f;
            float max_w = 0.06f + dy * 0.22f;
            if (fabsf(u - 0.50f) <= max_w) {
                if (v >= 0.58f || (v <= 0.38f && fabsf(u - 0.50f) <= max_w * 0.7f)) {
                    *out_rgb = 0xF59E0Bu;
                } else {
                    *out_rgb = 0xBAE6FDu;
                }
                return true;
            }
        }
        return false;
    }
    case 0x1F512:
    case 0x1F513: { /* 🔒 / 🔓 Lock */
        float scx = (codepoint == 0x1F512) ? 0.50f : 0.44f;
        float dsh = (u - scx) * (u - scx) + (v - 0.36f) * (v - 0.36f);
        if (dsh >= 0.12f * 0.12f && dsh <= 0.20f * 0.20f && v <= 0.48f) {
            if (codepoint == 0x1F512 || u <= 0.52f || v <= 0.36f) {
                *out_rgb = 0x94A3B8u;
                return true;
            }
        }
        if (u >= 0.24f && u <= 0.76f && v >= 0.46f && v <= 0.86f) {
            float dkh = (u - 0.50f) * (u - 0.50f) + (v - 0.62f) * (v - 0.62f);
            if (dkh <= 0.07f * 0.07f || (u >= 0.47f && u <= 0.53f && v >= 0.62f && v <= 0.74f)) {
                *out_rgb = 0x1E293Bu;
            } else {
                *out_rgb = 0xF59E0Bu;
            }
            return true;
        }
        return false;
    }
    case 0x2B50:
    case 0x1F31F: { /* ⭐ Star */
        static const float STAR_PTS[10][2] = {
            {0.5000f, 0.0500f}, {0.6176f, 0.3382f}, {0.9279f, 0.3610f}, {0.6882f, 0.5543f},
            {0.7645f, 0.8640f}, {0.5000f, 0.7000f}, {0.2355f, 0.8640f}, {0.3118f, 0.5543f},
            {0.0721f, 0.3610f}, {0.3824f, 0.3382f}
        };
        int crossings = 0;
        for (int k = 0; k < 10; ++k) {
            float x1 = STAR_PTS[k][0], y1 = STAR_PTS[k][1];
            float x2 = STAR_PTS[(k + 1) % 10][0], y2 = STAR_PTS[(k + 1) % 10][1];
            if (((y1 <= v && v < y2) || (y2 <= v && v < y1)) &&
                (u < (x2 - x1) * (v - y1) / (y2 - y1) + x1)) {
                ++crossings;
            }
        }
        if ((crossings & 1) != 0) {
            *out_rgb = 0xFBBF24u;
            return true;
        }
        return false;
    }
    case 0x1F4C5: { /* 📅 Calendar */
        if ((u >= 0.30f && u <= 0.38f && v >= 0.16f && v <= 0.26f) ||
            (u >= 0.62f && u <= 0.70f && v >= 0.16f && v <= 0.26f)) {
            *out_rgb = 0x94A3B8u;
            return true;
        }
        if (u >= 0.18f && u <= 0.82f && v >= 0.24f && v <= 0.84f) {
            if (v <= 0.40f) {
                *out_rgb = 0xDC2626u;
            } else if (v >= 0.40f && v <= 0.44f) {
                return false;
            } else {
                if (((u >= 0.32f && u <= 0.40f) || (u >= 0.46f && u <= 0.54f) || (u >= 0.60f && u <= 0.68f)) &&
                    ((v >= 0.52f && v <= 0.60f) || (v >= 0.68f && v <= 0.76f))) {
                    *out_rgb = 0x475569u;
                } else {
                    *out_rgb = 0xFFFFFFu;
                }
            }
            return true;
        }
        return false;
    }
    case 0x1F4A1: { /* 💡 Lightbulb */
        float db = (u - 0.50f) * (u - 0.50f) + (v - 0.42f) * (v - 0.42f);
        if (db <= 0.30f * 0.30f ||
            (u >= 0.32f && u <= 0.68f && v >= 0.42f && v <= 0.68f &&
             fabsf(u - 0.50f) <= (0.30f - (v - 0.42f) * 0.46f))) {
            *out_rgb = 0xFACC15u;
            return true;
        }
        if (u >= 0.38f && u <= 0.62f && v >= 0.70f && v <= 0.84f && !(v >= 0.76f && v <= 0.78f)) {
            *out_rgb = 0x94A3B8u;
            return true;
        }
        return false;
    }
    case 0x1F525: { /* 🔥 Fire */
        float df = (u - 0.50f) * (u - 0.50f) + (v - 0.66f) * (v - 0.66f);
        bool in_f = false;
        if (df <= 0.32f * 0.32f) {
            in_f = true;
        } else if (u >= 0.30f && u <= 0.70f && v >= 0.20f && v <= 0.66f) {
            float dy = (v - 0.20f) / 0.46f;
            if (fabsf(u - (0.50f + 0.08f * (1.0f - dy))) <= dy * 0.28f) in_f = true;
        }
        if (in_f) {
            float df2 = (u - 0.50f) * (u - 0.50f) + (v - 0.70f) * (v - 0.70f);
            if (df2 <= 0.14f * 0.14f ||
                (u >= 0.42f && u <= 0.58f && v >= 0.44f && v <= 0.70f &&
                 fabsf(u - 0.50f) <= ((v - 0.44f) / 0.26f) * 0.10f)) {
                *out_rgb = 0xFDE047u;
            } else {
                *out_rgb = 0xF97316u;
            }
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

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
    if (!g_ui.font_ready || FT_Get_Char_Index(g_ui.face, codepoint) == 0 ||
        FT_Load_Char(g_ui.face, codepoint, FT_LOAD_RENDER) != 0) {
        return NULL;
    }
    {
        FT_GlyphSlot glyph = g_ui.face->glyph;
        uint8_t *copy = NULL;
        int extra = bold ? 1 : 0;
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
                    if (!bold) {
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
    const char *cursor = text;
    if (!text) {
        return 0;
    }
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
    const char *cursor = text;
    int pen_x = x;
    uint32_t resolved = resolve_color(color);
    if (!text) {
        return;
    }
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
    int width = measure_text(text, size);
    int baseline = rect.y + (rect.height + size) / 2 - 3;
    draw_text(pixels, stride, rect.x + (rect.width - width) / 2, baseline, text, size, color);
}

void fit_text(char *out, size_t out_size, const char *text, int size, int max_width)
{
    char source_copy[512];
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
    cursor = source;
    end = cursor;
    out[0] = '\0';
    while (*cursor) {
        const char *next = cursor;
        uint32_t codepoint = ui_decode_utf8(&next);
        const UiGlyphEntry *entry = ui_glyph_fetch(codepoint, size, false);
        int advance = 0;
        if (entry) {
            advance = entry->advance;
        }
        if (width + advance > max_width || (*next && width + advance + 28 > max_width)) {
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
    const char *cursor = text ? text : "";
    int line = 0;
    while (*cursor && line < max_lines) {
        const char *end = cursor;
        const char *newline = strchr(cursor, '\n');
        int width = 0;
        char buffer[PTC_PAIRING_BASE_URL_MAX_LEN + 1];
        while (*end && end != newline) {
            const char *next = end;
            uint32_t codepoint = ui_decode_utf8(&next);
            int advance = 0;
            const UiGlyphEntry *entry = ui_glyph_fetch(codepoint, size, false);
            if (entry) {
                advance = (int)entry->advance;
            }
            if (end > cursor && width + advance > max_width) break;
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
        cursor = *end == '\n' ? end + 1 : end;
        ++line;
    }
    return baseline + line * line_height;
}
