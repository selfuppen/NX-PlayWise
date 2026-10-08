#ifndef PTC_UI_EMOJI_H
#define PTC_UI_EMOJI_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline bool is_builtin_emoji(uint32_t codepoint)
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
    case 0x1F3AE: /* 🎮 Video Game Controller */
    case 0x1F4FA: /* 📺 Television */
    case 0x2699:  /* ⚙️ Gear */
    case 0x1F511: /* 🔑 Key */
    case 0x1F4F1: /* 📱 Mobile Phone */
    case 0x1F4CA: /* 📊 Bar Chart */
    case 0x26A1:  /* ⚡ Lightning Bolt */
    case 0x1F504: /* 🔄 Counterclockwise Arrows */
    case 0x1F6D1: /* 🛑 Stop Sign */
    case 0x1F527: /* 🔧 Wrench */
    case 0x1F4E6: /* 📦 Package */
    case 0x1F3A8: /* 🎨 Artist Palette */
    case 0x1F50A: /* 🔊 Speaker High Volume */
    case 0x1F4CB: /* 📋 Clipboard */
    case 0x1F552: /* 🕒 Clock */
    case 0x1F4A4: /* 💤 Sleep / Zzz */
    case 0x2728:  /* ✨ Sparkles */
    case 0x1F4BE: /* 💾 Floppy Disk / Save */
    case 0x1F4DC: /* 📜 Scroll */
    case 0x1F310: /* 🌐 Globe */
        return true;
    default:
        return false;
    }
}

static inline float dist_to_segment_sq(float px, float py, float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1, dy = y2 - y1;
    float l2 = dx * dx + dy * dy;
    float t = ((px - x1) * dx + (py - y1) * dy) / (l2 > 1e-6f ? l2 : 1e-6f);
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    float qx = x1 + t * dx, qy = y1 + t * dy;
    return (px - qx) * (px - qx) + (py - qy) * (py - qy);
}

static inline bool is_in_leaf(float u, float v, float cx, float cy, float len, float wid, float cos_a, float sin_a)
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

static inline bool sample_emoji_color(uint32_t codepoint, float u, float v, uint32_t *out_rgb)
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
    case 0x1F3AE: { /* 🎮 Video Game Controller */
        float dx = (u - 0.50f) / 0.38f;
        float dy = (v - 0.54f) / 0.22f;
        if (dx * dx + dy * dy <= 1.0f) {
            float px = fabsf(u - 0.32f);
            float py = fabsf(v - 0.54f);
            if ((px <= 0.08f && py <= 0.03f) || (px <= 0.03f && py <= 0.08f)) {
                *out_rgb = 0x1E293Bu;
                return true;
            }
            float bx = u - 0.68f;
            float by = v - 0.54f;
            if (bx * bx + (by + 0.07f) * (by + 0.07f) <= 0.032f * 0.032f) { *out_rgb = 0xFBBF24u; return true; }
            if (bx * bx + (by - 0.07f) * (by - 0.07f) <= 0.032f * 0.032f) { *out_rgb = 0x10B981u; return true; }
            if ((bx + 0.07f) * (bx + 0.07f) + by * by <= 0.032f * 0.032f) { *out_rgb = 0x3B82F6u; return true; }
            if ((bx - 0.07f) * (bx - 0.07f) + by * by <= 0.032f * 0.032f) { *out_rgb = 0xEF4444u; return true; }
            if ((u - 0.44f) * (u - 0.44f) + (v - 0.46f) * (v - 0.46f) <= 0.022f * 0.022f ||
                (u - 0.56f) * (u - 0.56f) + (v - 0.46f) * (v - 0.46f) <= 0.022f * 0.022f) {
                *out_rgb = 0x1E293Bu;
                return true;
            }
            *out_rgb = 0x475569u;
            return true;
        }
        float gl_x = u - 0.22f, gl_y = v - 0.70f;
        if (gl_x * gl_x + gl_y * gl_y <= 0.12f * 0.12f) {
            *out_rgb = 0x334155u;
            return true;
        }
        float gr_x = u - 0.78f, gr_y = v - 0.70f;
        if (gr_x * gr_x + gr_y * gr_y <= 0.12f * 0.12f) {
            *out_rgb = 0x334155u;
            return true;
        }
        return false;
    }
    case 0x1F4FA: { /* 📺 Television */
        if (dist_to_segment_sq(u, v, 0.50f, 0.26f, 0.32f, 0.12f) <= 0.025f * 0.025f ||
            dist_to_segment_sq(u, v, 0.50f, 0.26f, 0.68f, 0.12f) <= 0.025f * 0.025f) {
            *out_rgb = 0x94A3B8u;
            return true;
        }
        if (u >= 0.16f && u <= 0.84f && v >= 0.26f && v <= 0.82f) {
            if (u >= 0.22f && u <= 0.68f && v >= 0.32f && v <= 0.76f) {
                if (fabsf((u - 0.22f) + (v - 0.32f) - 0.35f) <= 0.04f) {
                    *out_rgb = 0xBAE6FDu;
                    return true;
                }
                *out_rgb = 0x0284C7u;
                return true;
            }
            if ((u - 0.76f) * (u - 0.76f) + (v - 0.44f) * (v - 0.44f) <= 0.04f * 0.04f ||
                (u - 0.76f) * (u - 0.76f) + (v - 0.62f) * (v - 0.62f) <= 0.04f * 0.04f) {
                *out_rgb = 0xF59E0Bu;
                return true;
            }
            *out_rgb = 0x475569u;
            return true;
        }
        if (dist_to_segment_sq(u, v, 0.28f, 0.82f, 0.22f, 0.92f) <= 0.035f * 0.035f ||
            dist_to_segment_sq(u, v, 0.72f, 0.82f, 0.78f, 0.92f) <= 0.035f * 0.035f) {
            *out_rgb = 0x1E293Bu;
            return true;
        }
        return false;
    }
    case 0x2699: { /* ⚙️ Gear */
        float dx = u - 0.50f, dy = v - 0.50f;
        float r2 = dx * dx + dy * dy;
        if (r2 <= 0.12f * 0.12f) return false;
        if (r2 <= 0.34f * 0.34f) {
            *out_rgb = 0x94A3B8u;
            return true;
        }
        if (r2 <= 0.45f * 0.45f) {
            float angle = atan2f(dy, dx);
            float c8 = cosf(8.0f * angle);
            if (c8 >= 0.25f) {
                *out_rgb = 0x64748Bu;
                return true;
            }
        }
        return false;
    }
    case 0x1F511: { /* 🔑 Key */
        float dh = (u - 0.66f) * (u - 0.66f) + (v - 0.34f) * (v - 0.34f);
        if (dh <= 0.20f * 0.20f) {
            if (dh <= 0.09f * 0.09f) return false;
            *out_rgb = 0xFBBF24u;
            return true;
        }
        if (dist_to_segment_sq(u, v, 0.52f, 0.48f, 0.22f, 0.78f) <= 0.045f * 0.045f) {
            *out_rgb = 0xF59E0Bu;
            return true;
        }
        if (dist_to_segment_sq(u, v, 0.32f, 0.68f, 0.38f, 0.74f) <= 0.04f * 0.04f ||
            dist_to_segment_sq(u, v, 0.22f, 0.78f, 0.28f, 0.84f) <= 0.04f * 0.04f) {
            *out_rgb = 0xF59E0Bu;
            return true;
        }
        return false;
    }
    case 0x1F4F1: { /* 📱 Mobile Phone */
        if (u >= 0.24f && u <= 0.76f && v >= 0.12f && v <= 0.88f) {
            if (u >= 0.28f && u <= 0.72f && v >= 0.20f && v <= 0.80f) {
                *out_rgb = 0x38BDF8u;
                return true;
            }
            if (u >= 0.42f && u <= 0.58f && v >= 0.15f && v <= 0.17f) {
                *out_rgb = 0x94A3B8u;
                return true;
            }
            if ((u - 0.50f) * (u - 0.50f) + (v - 0.84f) * (v - 0.84f) <= 0.025f * 0.025f) {
                *out_rgb = 0x94A3B8u;
                return true;
            }
            *out_rgb = 0x334155u;
            return true;
        }
        return false;
    }
    case 0x1F4CA: { /* 📊 Bar Chart */
        if (u >= 0.14f && u <= 0.86f && v >= 0.82f && v <= 0.88f) {
            *out_rgb = 0x64748Bu;
            return true;
        }
        if (u >= 0.20f && u <= 0.32f && v >= 0.56f && v <= 0.82f) { *out_rgb = 0x3B82F6u; return true; }
        if (u >= 0.38f && u <= 0.50f && v >= 0.36f && v <= 0.82f) { *out_rgb = 0x10B981u; return true; }
        if (u >= 0.56f && u <= 0.68f && v >= 0.46f && v <= 0.82f) { *out_rgb = 0xF59E0Bu; return true; }
        if (u >= 0.74f && u <= 0.86f && v >= 0.18f && v <= 0.82f) { *out_rgb = 0xEF4444u; return true; }
        return false;
    }
    case 0x26A1: { /* ⚡ Lightning Bolt */
        static const float BOLT_PTS[7][2] = {
            {0.56f, 0.08f}, {0.24f, 0.52f}, {0.50f, 0.52f},
            {0.36f, 0.94f}, {0.76f, 0.46f}, {0.50f, 0.46f}, {0.62f, 0.08f}
        };
        int crossings = 0;
        for (int k = 0; k < 7; ++k) {
            float x1 = BOLT_PTS[k][0], y1 = BOLT_PTS[k][1];
            float x2 = BOLT_PTS[(k + 1) % 7][0], y2 = BOLT_PTS[(k + 1) % 7][1];
            if (((y1 <= v && v < y2) || (y2 <= v && v < y1)) &&
                (u < (x2 - x1) * (v - y1) / (y2 - y1) + x1)) {
                ++crossings;
            }
        }
        if ((crossings & 1) != 0) {
            *out_rgb = (u + v > 0.9f) ? 0xF59E0Bu : 0xFDE047u;
            return true;
        }
        return false;
    }
    case 0x1F504: { /* 🔄 Counterclockwise Arrows */
        float dx = u - 0.50f, dy = v - 0.50f;
        float r2 = dx * dx + dy * dy;
        if (r2 >= 0.24f * 0.24f && r2 <= 0.40f * 0.40f) {
            if (!(dx >= 0.16f && dy <= -0.16f) && !(dx <= -0.16f && dy >= 0.16f)) {
                *out_rgb = 0x06B6D4u;
                return true;
            }
        }
        if (dist_to_segment_sq(u, v, 0.40f, 0.14f, 0.50f, 0.28f) <= 0.04f * 0.04f ||
            dist_to_segment_sq(u, v, 0.60f, 0.86f, 0.50f, 0.72f) <= 0.04f * 0.04f) {
            *out_rgb = 0x06B6D4u;
            return true;
        }
        return false;
    }
    case 0x1F6D1: { /* 🛑 Stop Sign */
        float ax = fabsf(u - 0.50f), ay = fabsf(v - 0.50f);
        if (ax <= 0.42f && ay <= 0.42f && (ax + ay <= 0.60f)) {
            if (ax <= 0.38f && ay <= 0.38f && (ax + ay <= 0.54f)) {
                if (ax <= 0.28f && ay <= 0.08f) {
                    *out_rgb = 0xFFFFFFu;
                    return true;
                }
                *out_rgb = 0xDC2626u;
                return true;
            }
            *out_rgb = 0xFFFFFFu;
            return true;
        }
        return false;
    }
    case 0x1F527: { /* 🔧 Wrench */
        if (dist_to_segment_sq(u, v, 0.24f, 0.76f, 0.58f, 0.42f) <= 0.055f * 0.055f) {
            *out_rgb = 0x94A3B8u;
            return true;
        }
        float dh = (u - 0.68f) * (u - 0.68f) + (v - 0.32f) * (v - 0.32f);
        if (dh <= 0.18f * 0.18f) {
            if (dist_to_segment_sq(u, v, 0.68f, 0.32f, 0.82f, 0.18f) <= 0.07f * 0.07f) return false;
            *out_rgb = 0xCBD5E1u;
            return true;
        }
        return false;
    }
    case 0x1F4E6: { /* 📦 Package */
        if (u >= 0.18f && u <= 0.82f && v >= 0.22f && v <= 0.82f) {
            if (u >= 0.44f && u <= 0.56f) {
                *out_rgb = 0xFDE047u;
                return true;
            }
            if (v >= 0.36f && v <= 0.40f) {
                *out_rgb = 0xB45309u;
                return true;
            }
            *out_rgb = 0xD97706u;
            return true;
        }
        return false;
    }
    case 0x1F3A8: { /* 🎨 Artist Palette */
        float px = (u - 0.48f) / 0.40f;
        float py = (v - 0.50f) / 0.36f;
        if (px * px + py * py <= 1.0f) {
            if ((u - 0.68f) * (u - 0.68f) + (v - 0.60f) * (v - 0.60f) <= 0.07f * 0.07f) return false;
            if ((u - 0.28f) * (u - 0.28f) + (v - 0.36f) * (v - 0.36f) <= 0.055f * 0.055f) { *out_rgb = 0xEF4444u; return true; }
            if ((u - 0.46f) * (u - 0.46f) + (v - 0.28f) * (v - 0.28f) <= 0.055f * 0.055f) { *out_rgb = 0x3B82F6u; return true; }
            if ((u - 0.64f) * (u - 0.64f) + (v - 0.34f) * (v - 0.34f) <= 0.055f * 0.055f) { *out_rgb = 0x10B981u; return true; }
            if ((u - 0.30f) * (u - 0.30f) + (v - 0.58f) * (v - 0.58f) <= 0.055f * 0.055f) { *out_rgb = 0xA855F7u; return true; }
            *out_rgb = 0xFBBF24u;
            return true;
        }
        return false;
    }
    case 0x1F50A: { /* 🔊 Speaker High Volume */
        if (u >= 0.18f && u <= 0.32f && v >= 0.36f && v <= 0.64f) {
            *out_rgb = 0x334155u;
            return true;
        }
        if (u >= 0.32f && u <= 0.48f) {
            float dy = (u - 0.32f) / 0.16f;
            float top = 0.36f - dy * 0.16f;
            float bot = 0.64f + dy * 0.16f;
            if (v >= top && v <= bot) {
                *out_rgb = 0x475569u;
                return true;
            }
        }
        float d1 = (u - 0.42f) * (u - 0.42f) + (v - 0.50f) * (v - 0.50f);
        if (d1 >= 0.18f * 0.18f && d1 <= 0.24f * 0.24f && u >= 0.50f) {
            *out_rgb = 0x38BDF8u;
            return true;
        }
        float d2 = (u - 0.42f) * (u - 0.42f) + (v - 0.50f) * (v - 0.50f);
        if (d2 >= 0.32f * 0.32f && d2 <= 0.38f * 0.38f && u >= 0.58f) {
            *out_rgb = 0x38BDF8u;
            return true;
        }
        return false;
    }
    case 0x1F4CB: { /* 📋 Clipboard */
        if (u >= 0.20f && u <= 0.80f && v >= 0.16f && v <= 0.88f) {
            if (u >= 0.34f && u <= 0.66f && v <= 0.24f) {
                *out_rgb = 0x94A3B8u;
                return true;
            }
            if (u >= 0.26f && u <= 0.74f && v >= 0.24f && v <= 0.82f) {
                if ((v >= 0.38f && v <= 0.42f && u <= 0.66f) ||
                    (v >= 0.50f && v <= 0.54f && u <= 0.66f) ||
                    (v >= 0.62f && v <= 0.66f && u <= 0.56f)) {
                    *out_rgb = 0x94A3B8u;
                    return true;
                }
                *out_rgb = 0xFFFFFFu;
                return true;
            }
            *out_rgb = 0xB45309u;
            return true;
        }
        return false;
    }
    case 0x1F552: { /* 🕒 Clock */
        float dc = (u - 0.50f) * (u - 0.50f) + (v - 0.50f) * (v - 0.50f);
        if (dc <= 0.42f * 0.42f) {
            if (dc >= 0.35f * 0.35f) {
                *out_rgb = 0x3B82F6u;
                return true;
            }
            if (dc <= 0.05f * 0.05f ||
                (u >= 0.47f && u <= 0.53f && v >= 0.20f && v <= 0.50f) ||
                (u >= 0.50f && u <= 0.74f && v >= 0.47f && v <= 0.53f)) {
                *out_rgb = 0x1E293Bu;
                return true;
            }
            *out_rgb = 0xFFFFFFu;
            return true;
        }
        return false;
    }
    case 0x1F4A4: { /* 💤 Zzz */
        if ((v >= 0.18f && v <= 0.22f && u >= 0.56f && u <= 0.82f) ||
            (v >= 0.36f && v <= 0.40f && u >= 0.56f && u <= 0.82f) ||
            (dist_to_segment_sq(u, v, 0.82f, 0.20f, 0.56f, 0.38f) <= 0.035f * 0.035f)) {
            *out_rgb = 0x818CF8u;
            return true;
        }
        if ((v >= 0.44f && v <= 0.48f && u >= 0.32f && u <= 0.54f) ||
            (v >= 0.60f && v <= 0.64f && u >= 0.32f && u <= 0.54f) ||
            (dist_to_segment_sq(u, v, 0.54f, 0.46f, 0.32f, 0.62f) <= 0.03f * 0.03f)) {
            *out_rgb = 0x6366F1u;
            return true;
        }
        if ((v >= 0.68f && v <= 0.72f && u >= 0.16f && u <= 0.34f) ||
            (v >= 0.80f && v <= 0.84f && u >= 0.16f && u <= 0.34f) ||
            (dist_to_segment_sq(u, v, 0.34f, 0.70f, 0.16f, 0.82f) <= 0.025f * 0.025f)) {
            *out_rgb = 0x4F46E5u;
            return true;
        }
        return false;
    }
    case 0x2728: { /* ✨ Sparkles */
        float dx1 = fabsf(u - 0.62f), dy1 = fabsf(v - 0.36f);
        if (sqrtf(dx1) + sqrtf(dy1) <= sqrtf(0.30f)) {
            *out_rgb = 0xFDE047u;
            return true;
        }
        float dx2 = fabsf(u - 0.28f), dy2 = fabsf(v - 0.68f);
        if (sqrtf(dx2) + sqrtf(dy2) <= sqrtf(0.18f)) {
            *out_rgb = 0xFACC15u;
            return true;
        }
        float dx3 = fabsf(u - 0.30f), dy3 = fabsf(v - 0.26f);
        if (sqrtf(dx3) + sqrtf(dy3) <= sqrtf(0.11f)) {
            *out_rgb = 0xFBBF24u;
            return true;
        }
        return false;
    }
    case 0x1F4BE: { /* 💾 Floppy Disk */
        if (u >= 0.20f && u <= 0.80f && v >= 0.18f && v <= 0.82f) {
            if (u >= 0.74f && v <= 0.24f) return false;
            if (u >= 0.30f && u <= 0.70f && v >= 0.18f && v <= 0.44f) {
                if (u >= 0.44f && u <= 0.52f && v >= 0.24f && v <= 0.38f) {
                    *out_rgb = 0x1E293Bu;
                    return true;
                }
                *out_rgb = 0xE2E8F0u;
                return true;
            }
            if (u >= 0.28f && u <= 0.72f && v >= 0.52f && v <= 0.78f) {
                *out_rgb = 0xFFFFFFu;
                return true;
            }
            *out_rgb = 0x2563EBu;
            return true;
        }
        return false;
    }
    case 0x1F4DC: { /* 📜 Scroll */
        if (u >= 0.22f && u <= 0.78f && v >= 0.22f && v <= 0.78f) {
            if ((v >= 0.36f && v <= 0.40f && u <= 0.68f) ||
                (v >= 0.48f && v <= 0.52f && u <= 0.68f) ||
                (v >= 0.60f && v <= 0.64f && u <= 0.58f)) {
                *out_rgb = 0xB45309u;
                return true;
            }
            *out_rgb = 0xFDE68Au;
            return true;
        }
        if ((u >= 0.18f && u <= 0.82f && v >= 0.16f && v <= 0.24f) ||
            (u >= 0.18f && u <= 0.82f && v >= 0.76f && v <= 0.84f)) {
            *out_rgb = 0xD97706u;
            return true;
        }
        return false;
    }
    case 0x1F310: { /* 🌐 Globe */
        float dg = (u - 0.50f) * (u - 0.50f) + (v - 0.50f) * (v - 0.50f);
        if (dg <= 0.38f * 0.38f) {
            if (fabsf(v - 0.50f) <= 0.025f || fabsf(u - 0.50f) <= 0.025f) {
                *out_rgb = 0xFFFFFFu;
                return true;
            }
            float dx = fabsf(u - 0.50f) / 0.22f;
            float dy = fabsf(v - 0.50f) / 0.38f;
            if (fabsf(dx * dx + dy * dy - 1.0f) <= 0.14f) {
                *out_rgb = 0xFFFFFFu;
                return true;
            }
            *out_rgb = 0x0284C7u;
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}



#ifdef __cplusplus
}
#endif

#endif /* PTC_UI_EMOJI_H */