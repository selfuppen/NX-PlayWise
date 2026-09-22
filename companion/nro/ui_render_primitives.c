#include "ui_render_internal.h"

uint32_t pack_rgb(uint32_t rgb)
{
    return RGBA8_MAXALPHA((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

/* 0xRRGGBB 颜色向目标色按百分比混合，用于派生弱化色与主题过渡。 */
uint32_t ui_mix_rgb(uint32_t base, uint32_t target, int percent)
{
    uint32_t channel;
    uint32_t mixed = 0;
    int shift;
    if (percent <= 0) return base;
    if (percent > 100) percent = 100;
    for (shift = 0; shift <= 16; shift += 8) {
        int base_value = (int)((base >> shift) & 0xff);
        int target_value = (int)((target >> shift) & 0xff);
        channel = (uint32_t)(base_value + (target_value - base_value) * percent / 100);
        mixed |= (channel & 0xff) << shift;
    }
    return mixed;
}

/* 主题切换过渡：palette 指针变化时快照旧值，320ms 内所有取色向新值缓出混合。
 * 预览构建固定时钟且逐帧切换主题，过渡整体关闭以保证逐像素可复现。 */
#ifndef PTC_UI_PREVIEW_ANIM_CLOCK_MS
#define UI_THEME_BLEND_MS 320
PtcUiPalette g_palette_from;
int64_t g_theme_blend_started_ms;
bool g_theme_blending;
const PtcUiPalette *g_palette_prev;
#endif

#ifndef PTC_UI_PREVIEW_ANIM_CLOCK_MS
uint32_t ui_theme_mix(uint32_t from_rgb, uint32_t to_rgb)
{
    int64_t elapsed;
    int permille;
    if (!g_theme_blending) return to_rgb;
    elapsed = ptc_ui_anim_now_ms() - g_theme_blend_started_ms;
    if (elapsed >= UI_THEME_BLEND_MS) {
        g_theme_blending = false;
        return to_rgb;
    }
    if (elapsed < 0) elapsed = 0;
    permille = (int)(elapsed * 1000 / UI_THEME_BLEND_MS);
    return ui_mix_rgb(from_rgb, to_rgb, permille / 10);
}
#endif

uint32_t resolve_color(uint32_t source)
{
    if (source & 0x01000000u) return pack_rgb(source & 0xFFFFFFu);
    switch (source) {
    case UI_INK: return pack_rgb(UI_BLENDED(text_primary));
    case UI_MUTED:
        if (is_docked_mode()) {
            uint32_t primary = UI_BLENDED(text_primary);
            bool is_dark = ((primary & 0xFF) > 0x80);
            return pack_rgb(is_dark ? 0xC8D5E8 : 0x3E4C62);
        }
        return pack_rgb(UI_BLENDED(text_secondary));
    case UI_DISABLED: return pack_rgb(UI_BLENDED(text_disabled));
    case UI_SURFACE: return pack_rgb(UI_BLENDED(surface));
    case UI_RAISED: return pack_rgb(UI_BLENDED(surface_raised));
    case UI_PAGE: return pack_rgb(UI_BLENDED(page_bg));
    case UI_BORDER: return pack_rgb(UI_BLENDED(border_decorative));
    case UI_CONTROL: return pack_rgb(UI_BLENDED(border_control));
    case UI_ACCENT: return pack_rgb(UI_BLENDED(accent));
    case UI_FOCUS: return pack_rgb(UI_BLENDED(focus));
    case UI_ON_ACCENT: return pack_rgb(UI_BLENDED(on_accent));
    case UI_ACCENT_SOFT: return pack_rgb(UI_BLENDED(accent_soft));
    case UI_SUCCESS: return pack_rgb(UI_BLENDED(success));
    case UI_WARNING: return pack_rgb(UI_BLENDED(warning));
    case UI_DANGER: return pack_rgb(UI_BLENDED(danger));
    case UI_SUCCESS_SOFT: return pack_rgb(UI_BLENDED(success_soft));
    case UI_WARNING_SOFT: return pack_rgb(UI_BLENDED(warning_soft));
    case UI_DANGER_SOFT: return pack_rgb(UI_BLENDED(danger_soft));
    case UI_CORAL: return pack_rgb(UI_BLENDED(coral));
    case UI_KEY_GLYPH_BG: return pack_rgb(UI_BLENDED(key_glyph_bg));
    case UI_KEY_GLYPH_BORDER: return pack_rgb(UI_BLENDED(key_glyph_border));
    case UI_GAUGE_SLOT: return pack_rgb(UI_BLENDED(gauge_slot));
    case UI_GAUGE_SLOT_BORDER: return pack_rgb(UI_BLENDED(gauge_slot_border));
    default: return pack_rgb(source);
    }
}

static void set_pixel(uint32_t *pixels, uint32_t stride, int x, int y, uint32_t color)
{
    if ((unsigned int)x >= SCREEN_WIDTH || (unsigned int)y >= SCREEN_HEIGHT) {
        return;
    }
    pixels[(uint32_t)y * stride + (uint32_t)x] = color;
}

void blend_pixel(uint32_t *pixels, uint32_t stride, int x, int y, uint32_t color, uint8_t alpha)
{
    uint32_t *destination;
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    uint32_t destination_red;
    uint32_t destination_green;
    uint32_t destination_blue;
    if ((unsigned int)x >= SCREEN_WIDTH || (unsigned int)y >= SCREEN_HEIGHT || alpha == 0) {
        return;
    }
    destination = &pixels[(uint32_t)y * stride + (uint32_t)x];
    red = color & 0xff;
    green = (color >> 8) & 0xff;
    blue = (color >> 16) & 0xff;
    destination_red = *destination & 0xff;
    destination_green = (*destination >> 8) & 0xff;
    destination_blue = (*destination >> 16) & 0xff;
    *destination = RGBA8_MAXALPHA(
        (red * alpha + destination_red * (255 - alpha)) / 255,
        (green * alpha + destination_green * (255 - alpha)) / 255,
        (blue * alpha + destination_blue * (255 - alpha)) / 255);
}

void fill_rect_packed(uint32_t *pixels, uint32_t stride, UiRect rect, uint32_t color)
{
    int x_start = rect.x < 0 ? 0 : rect.x;
    int y_start = rect.y < 0 ? 0 : rect.y;
    int x_end = rect.x + rect.width > SCREEN_WIDTH ? SCREEN_WIDTH : rect.x + rect.width;
    int y_end = rect.y + rect.height > SCREEN_HEIGHT ? SCREEN_HEIGHT : rect.y + rect.height;
    int y;
    for (y = y_start; y < y_end; ++y) {
        uint32_t *row = pixels + (uint32_t)y * stride;
        int x;
        for (x = x_start; x < x_end; ++x) {
            row[x] = color;
        }
    }
}

void fill_rect(uint32_t *pixels, uint32_t stride, UiRect rect, uint32_t color)
{
    fill_rect_packed(pixels, stride, rect, resolve_color(color));
}

/* Pixel coverage uses Analytical Signed Distance Field (SDF) with cubic Hermite smoothstep. */
static int disk_coverage(int dx_eighths, int dy_eighths, int radius_eighths)
{
    float dx = (float)dx_eighths * 0.125f;
    float dy = (float)dy_eighths * 0.125f;
    float r = (float)radius_eighths * 0.125f;
    float dist = r - sqrtf(dx * dx + dy * dy);
    const float feather = 1.25f;
    const float half_feather = feather * 0.5f;
    if (dist >= half_feather) return 255;
    if (dist <= -half_feather) return 0;
    float t = (dist + half_feather) / feather;
    int cov = (int)(t * t * (3.0f - 2.0f * t) * 255.0f + 0.5f);
    return cov > 255 ? 255 : (cov < 0 ? 0 : cov);
}

static int round_rect_coverage(UiRect rect, int radius, int x, int y)
{
    int px = x - rect.x, py = y - rect.y;
    if ((unsigned int)px >= (unsigned int)rect.width || (unsigned int)py >= (unsigned int)rect.height) return 0;
    if (radius <= 0) return 255;
    if (radius * 2 > rect.width) radius = rect.width / 2;
    if (radius * 2 > rect.height) radius = rect.height / 2;

    /* 直边中心交叉区域全覆盖 */
    bool in_cx = (px >= radius && px < rect.width - radius);
    bool in_cy = (py >= radius && py < rect.height - radius);
    if (in_cx || in_cy) return 255;

    /* 映射到最近外边界的对称像素坐标（严格满足左右与上下像素级几何对称） */
    int sx = px < radius ? px : (rect.width - 1 - px);
    int sy = py < radius ? py : (rect.height - 1 - py);

    /* 像素中心到圆角圆心的精确几何距离 */
    float dx = (float)(radius - 1 - sx) + 0.5f;
    float dy = (float)(radius - 1 - sy) + 0.5f;
    float dist = (float)radius - sqrtf(dx * dx + dy * dy);

    /* 亚像素三次 Smoothstep 平滑过渡，彻底消除台阶锯齿 */
    const float feather = 1.25f;
    const float half_feather = feather * 0.5f;
    if (dist >= half_feather) return 255;
    if (dist <= -half_feather) return 0;

    float t = (dist + half_feather) / feather;
    int cov = (int)(t * t * (3.0f - 2.0f * t) * 255.0f + 0.5f);
    return cov > 255 ? 255 : (cov < 0 ? 0 : cov);
}

static void paint_round_rect(uint32_t *pixels, uint32_t stride, UiRect rect,
                             int radius, int stroke, uint32_t color)
{
    if (rect.width <= 0 || rect.height <= 0) return;
    if (radius < 0) radius = 0;
    if (radius > rect.width / 2) radius = rect.width / 2;
    if (radius > rect.height / 2) radius = rect.height / 2;
    if (stroke * 2 >= rect.width || stroke * 2 >= rect.height) stroke = 0;
    UiRect inner = {rect.x + stroke, rect.y + stroke, rect.width - 2 * stroke, rect.height - 2 * stroke};
    int inner_radius = radius > stroke ? radius - stroke : 0;
    int band = radius > stroke ? radius : stroke;
    uint32_t resolved = resolve_color(color);
    int first_y = rect.y < 0 ? 0 : rect.y;
    int last_y = rect.y + rect.height > SCREEN_HEIGHT ? SCREEN_HEIGHT : rect.y + rect.height;
    for (int y = first_y; y < last_y; ++y) {
        int local_y = y - rect.y;
        if (local_y >= band && local_y < rect.height - band) {
            if (!stroke) fill_rect_packed(pixels, stride, (UiRect){rect.x, y, rect.width, 1}, resolved);
            else {
                fill_rect_packed(pixels, stride, (UiRect){rect.x, y, stroke, 1}, resolved);
                fill_rect_packed(pixels, stride, (UiRect){rect.x + rect.width - stroke, y, stroke, 1}, resolved);
            }
            continue;
        }
        int first_x = rect.x < 0 ? 0 : rect.x;
        int last_x = rect.x + rect.width > SCREEN_WIDTH ? SCREEN_WIDTH : rect.x + rect.width;
        for (int x = first_x; x < last_x; ++x) {
            if (x >= rect.x + band && x < rect.x + rect.width - band) {
                int end = rect.x + rect.width - band;
                if (end > last_x) end = last_x;
                if (!stroke || local_y < stroke || local_y >= rect.height - stroke)
                    fill_rect_packed(pixels, stride, (UiRect){x, y, end - x, 1}, resolved);
                x = end - 1;
                continue;
            }
            int coverage = round_rect_coverage(rect, radius, x, y);
            if (stroke) {
                int inner_cov = round_rect_coverage(inner, inner_radius, x, y);
                coverage -= inner_cov;
                if (coverage < 0) coverage = 0;
            }
            if (coverage >= 255) set_pixel(pixels, stride, x, y, resolved);
            else if (coverage > 0) blend_pixel(pixels, stride, x, y, resolved, (uint8_t)coverage);
        }
    }
}

void fill_round_rect(uint32_t *pixels, uint32_t stride, UiRect rect, int radius, uint32_t color)
{
    paint_round_rect(pixels, stride, rect, radius, 0, color);
}

static void paint_round_rect_gradient(uint32_t *pixels, uint32_t stride, UiRect rect,
                                      int radius, uint32_t top_color, uint32_t bottom_color)
{
    if (rect.width <= 0 || rect.height <= 0) return;
    if (radius < 0) radius = 0;
    if (radius > rect.width / 2) radius = rect.width / 2;
    if (radius > rect.height / 2) radius = rect.height / 2;
    uint32_t top_rgb = resolve_color(top_color);
    uint32_t bottom_rgb = resolve_color(bottom_color);

    int first_y = rect.y < 0 ? 0 : rect.y;
    int last_y = rect.y + rect.height > SCREEN_HEIGHT ? SCREEN_HEIGHT : rect.y + rect.height;
    int band = radius;

    for (int y = first_y; y < last_y; ++y) {
        int local_y = y - rect.y;
        int percent = (local_y * 100) / (rect.height > 0 ? rect.height : 1);
        uint32_t resolved = pack_rgb(ui_mix_rgb(top_rgb, bottom_rgb, percent));

        if (local_y >= band && local_y < rect.height - band) {
            fill_rect_packed(pixels, stride, (UiRect){rect.x, y, rect.width, 1}, resolved);
            continue;
        }
        int first_x = rect.x < 0 ? 0 : rect.x;
        int last_x = rect.x + rect.width > SCREEN_WIDTH ? SCREEN_WIDTH : rect.x + rect.width;
        for (int x = first_x; x < last_x; ++x) {
            if (x >= rect.x + band && x < rect.x + rect.width - band) {
                int end = rect.x + rect.width - band;
                if (end > last_x) end = last_x;
                fill_rect_packed(pixels, stride, (UiRect){x, y, end - x, 1}, resolved);
                x = end - 1;
                continue;
            }
            int coverage = round_rect_coverage(rect, radius, x, y);
            if (coverage >= 255) set_pixel(pixels, stride, x, y, resolved);
            else if (coverage > 0) blend_pixel(pixels, stride, x, y, resolved, (uint8_t)coverage);
        }
    }
}

/* 按百分比压暗 0xRRGGBB 颜色，用于从表面色派生渐变底色。 */
uint32_t ui_darken(uint32_t rgb, int percent)
{
    return ui_mix_rgb(rgb, 0x000000, percent);
}

void fill_round_rect_gradient(uint32_t *pixels, uint32_t stride, UiRect rect, int radius,
                                     uint32_t top_color, uint32_t bottom_color)
{
    paint_round_rect_gradient(pixels, stride, rect, radius, top_color, bottom_color);
}

void draw_rect_outline(uint32_t *pixels, uint32_t stride, UiRect rect, int radius, int width, uint32_t color)
{
    /* Subtract inner coverage, never repaint the contents under the stroke. */
    if (width > 0) paint_round_rect(pixels, stride, rect, radius, width, color);
}

/* 动画时钟（毫秒）。设备走系统计数器；宿主预览可固定，保证逐像素可复现。 */
#if defined(PTC_UI_PREVIEW_ANIM_CLOCK_MS)
int64_t ptc_ui_anim_now_ms(void)
{
    return PTC_UI_PREVIEW_ANIM_CLOCK_MS;
}
#elif defined(__SWITCH__)
int64_t ptc_ui_anim_now_ms(void)
{
    return (int64_t)(armTicksToNs(armGetSystemTick()) / 1000000ULL);
}
#else
int64_t ptc_ui_anim_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}
#endif

/* 呼吸周期 1280ms：亮度从基色线性升到高光再回落，与旧 32 拍（40ms 拍长）节奏一致。 */
#define UI_BREATHING_CYCLE_MS 1280

/* ease-out cubic：动效统一缓动曲线，t 为 0..1 进度。 */
float ui_ease_out(float t)
{
    float inv;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

int get_breathing_phase(void)
{
    int64_t cycle = ptc_ui_anim_now_ms() % UI_BREATHING_CYCLE_MS;
    int64_t half = UI_BREATHING_CYCLE_MS / 2;
    int phase = (int)((cycle * 16) / half);
    if (cycle >= half) phase = (int)(((UI_BREATHING_CYCLE_MS - cycle) * 16) / half);
    return phase > 15 ? 15 : phase;
}

void draw_focus_ring(uint32_t *pixels, uint32_t stride, UiRect rect, int radius)
{
    int phase = get_breathing_phase();
    int offset = is_docked_mode() ? 4 : 3;
    int stroke_w = is_docked_mode() ? 3 : 2;

    /* 呼吸动态颜色：在基础 UI_FOCUS 与呼吸高光荧光亮蓝之间平滑脉冲插值 */
    uint32_t focus_token = UI_FOCUS;
    if (phase > 0 && g_palette) {
        uint32_t raw_rgb = UI_BLENDED(focus);
        uint32_t r = (raw_rgb >> 16) & 0xff;
        uint32_t g = (raw_rgb >> 8) & 0xff;
        uint32_t b = raw_rgb & 0xff;
        r += (uint32_t)((215 - (int)r) * phase) / 30u;
        g += (uint32_t)((238 - (int)g) * phase) / 30u;
        b += (uint32_t)((255 - (int)b) * phase) / 30u;
        focus_token = UI_RGB((r << 16) | (g << 8) | b);
    }

    /* 绘制完整、闭合、严密同心的圆角焦点框，彻底根除断裂悬空的多余线条 */
    draw_rect_outline(pixels, stride,
        (UiRect){rect.x - offset, rect.y - offset, rect.width + 2 * offset, rect.height + 2 * offset},
        radius + offset, stroke_w, focus_token);
}


/* 圆角矩形符号距离（负值在内部）。轴对齐带状区域免开方，仅角部走 sqrt。 */
static float round_rect_sdf(UiRect rect, int radius, float x, float y)
{
    float hw = rect.width * 0.5f;
    float hh = rect.height * 0.5f;
    float r = (float)radius;
    float qx;
    float qy;
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    qx = fabsf(x - (rect.x + hw)) - (hw - r);
    qy = fabsf(y - (rect.y + hh)) - (hh - r);
    if (qx <= 0 && qy <= 0) return (qx > qy ? qx : qy) - r;
    if (qx <= 0) return qy - r;
    if (qy <= 0) return qx - r;
    return sqrtf(qx * qx + qy * qy) - r;
}

/* 五次 Smootherstep（Ken Perlin 改进公式）：一阶与二阶导数在两端皆为 0，
 * 彻底消除阴影外缘光晕消散时的色环断阶（Mach bands）。 */
static inline float smootherstep(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/* 工业级圆角感知软阴影：
 * 1. 采用精准圆角 SDF 几何遮罩剔除卡片实心本体，保留四个转角弧线外的完整阴影与抗锯齿带，
 *    根除外接矩形 AABB 直角截断导致的转角分层断裂；
 * 2. 单次像素遍历中融合 Key（向下投影核心）与 Ambient（大范围漫反射光晕）双层物理软阴影；
 * 3. 采用五次 Smootherstep 极平滑衰减，并在单次 blend_pixel 中完成 Alpha 复合，画质与帧率兼顾。 */
void draw_round_rect_shadow(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    int radius,
    int blur,
    int peak_alpha,
    int y_bias)
{
    if (blur <= 0 || peak_alpha <= 0) return;
    if (rect.width <= 0 || rect.height <= 0) return;
    if (radius > rect.width / 2) radius = rect.width / 2;
    if (radius > rect.height / 2) radius = rect.height / 2;

    /* 阴影峰值按主题 shadow_strength 缩放，暗色主题阴影更实。 */
    if (g_palette && g_palette->shadow_strength) {
        peak_alpha = peak_alpha * (int)g_palette->shadow_strength / 100;
        if (peak_alpha > 255) peak_alpha = 255;
    }

    /* 双层软阴影配比分解：
     * Key 层：聚集清晰，偏置随光源下移；
     * Ambient 层：扩散更广，偏置轻微，营造柔和弥散光晕。 */
    int blur_key = blur * 3 / 5;
    if (blur_key < 3) blur_key = 3;
    int blur_amb = blur + 2;
    int alpha_key = peak_alpha * 65 / 100;
    int alpha_amb = peak_alpha * 40 / 100;
    int bias_key = y_bias;
    int bias_amb = y_bias > 1 ? y_bias / 3 : 0;

    int max_blur = blur_amb;
    int margin = max_blur + 2;
    int min_y_bias = bias_amb < bias_key ? bias_amb : bias_key;
    int max_y_bias = bias_amb > bias_key ? bias_amb : bias_key;
    int y_top = rect.y + (min_y_bias < 0 ? min_y_bias : 0) - margin;
    int y_bottom = rect.y + rect.height + (max_y_bias > 0 ? max_y_bias : 0) + margin;

    int x_start = rect.x - margin < 0 ? 0 : rect.x - margin;
    int x_end = rect.x + rect.width + margin > SCREEN_WIDTH ? SCREEN_WIDTH : rect.x + rect.width + margin;
    int y_start = y_top < 0 ? 0 : y_top;
    int y_end = y_bottom > SCREEN_HEIGHT ? SCREEN_HEIGHT : y_bottom;

    float inv_blur_key = 1.0f / (float)blur_key;
    float inv_blur_amb = 1.0f / (float)blur_amb;
    float outer_cutoff = (float)(max_blur + max_y_bias);

    for (int y = y_start; y < y_end; ++y) {
        float py = (float)y + 0.5f;
        for (int x = x_start; x < x_end; ++x) {
            float px = (float)x + 0.5f;

            /* 精准卡片本体遮罩：仅剔除完全落在卡片本体实心内部的像素（<= -0.5f），
             * 彻底解决旧 AABB 直角判断在圆角转弯处将阴影粗暴截断造成的分层断裂。
             * 边缘亚像素过渡带（> -0.5f）保留绘制阴影，后续卡片绘制时亚像素叠加即可实现完美抗锯齿。 */
            float body_dist = round_rect_sdf(rect, radius, px, py);
            if (body_dist <= -0.5f) continue;
            if (body_dist >= outer_cutoff) continue;

            /* Ambient 层计算 */
            int a_amb = 0;
            float dist_amb = round_rect_sdf(rect, radius, px, py - (float)bias_amb);
            if (dist_amb < (float)blur_amb) {
                if (dist_amb <= 0.0f) {
                    a_amb = alpha_amb;
                } else {
                    float t = 1.0f - dist_amb * inv_blur_amb;
                    a_amb = (int)(alpha_amb * smootherstep(t) + 0.5f);
                }
            }

            /* Key 层计算 */
            int a_key = 0;
            float dist_key = round_rect_sdf(rect, radius, px, py - (float)bias_key);
            if (dist_key < (float)blur_key) {
                if (dist_key <= 0.0f) {
                    a_key = alpha_key;
                } else {
                    float t = 1.0f - dist_key * inv_blur_key;
                    a_key = (int)(alpha_key * smootherstep(t) + 0.5f);
                }
            }

            if (a_amb == 0 && a_key == 0) continue;

            /* 标准复合 Alpha 混合：A_total = A1 + A2 - A1*A2/255 */
            int total_alpha = a_amb + a_key - (a_amb * a_key + 127) / 255;
            if (total_alpha > 255) total_alpha = 255;
            if (total_alpha > 0) {
                blend_pixel(pixels, stride, x, y, RGBA8_MAXALPHA(0, 0, 0), (uint8_t)total_alpha);
            }
        }
    }
}

/* 海拔档位：页面卡片用轻阴影；弹窗在 draw_dialog_shell 用更强更扩散的一档。 */
void draw_card_shadow(uint32_t *pixels, uint32_t stride, UiRect rect, int radius)
{
    draw_round_rect_shadow(pixels, stride, rect, radius, 10, 42, 3);
}

/* 背景氛围层：页面底色加两团极低透明度的主题色光斑，整幅缓存在离屏，
 * 仅在主题、底座状态或过渡进行中重建；绘制时一次整幅拷贝。 */
uint32_t *g_background_cache;
const PtcUiPalette *g_background_palette;
bool g_background_docked;

static void ui_background_glow(int center_x, int center_y, int radius, uint32_t rgb, int peak_alpha)
{
    int radius_sq = radius * radius;
    int x0 = center_x - radius < 0 ? 0 : center_x - radius;
    int x1 = center_x + radius > SCREEN_WIDTH ? SCREEN_WIDTH : center_x + radius;
    int y0 = center_y - radius < 0 ? 0 : center_y - radius;
    int y1 = center_y + radius > SCREEN_HEIGHT ? SCREEN_HEIGHT : center_y + radius;
    uint32_t red = rgb & 0xff;
    uint32_t green = (rgb >> 8) & 0xff;
    uint32_t blue = (rgb >> 16) & 0xff;
    int x;
    int y;
    for (y = y0; y < y1; ++y) {
        int dy = y - center_y;
        uint32_t *row = g_background_cache + (size_t)y * SCREEN_WIDTH;
        for (x = x0; x < x1; ++x) {
            int dx = x - center_x;
            int dist_sq = dx * dx + dy * dy;
            int alpha;
            uint32_t destination;
            if (dist_sq >= radius_sq) continue;
            alpha = peak_alpha * (radius_sq - dist_sq) / radius_sq;
            if (alpha <= 0) continue;
            destination = row[x];
            row[x] = RGBA8_MAXALPHA(
                (red * alpha + (destination & 0xff) * (255 - alpha)) / 255,
                (green * alpha + ((destination >> 8) & 0xff) * (255 - alpha)) / 255,
                (blue * alpha + ((destination >> 16) & 0xff) * (255 - alpha)) / 255);
        }
    }
}

void ui_background_rebuild(void)
{
    uint32_t base = pack_rgb(UI_BLENDED(page_bg));
    int y;
    if (!g_background_cache) return;
    for (y = 0; y < SCREEN_HEIGHT; ++y) {
        uint32_t *row = g_background_cache + (size_t)y * SCREEN_WIDTH;
        int x;
        for (x = 0; x < SCREEN_WIDTH; ++x) row[x] = base;
    }
    ui_background_glow(230, 40, 760, UI_BLENDED(accent), 12);
    ui_background_glow(1180, 730, 680, UI_BLENDED(coral), 10);
    g_background_palette = g_palette;
    g_background_docked = is_docked_mode();
}

void draw_circle_outline(
    uint32_t *pixels,
    uint32_t stride,
    int center_x,
    int center_y,
    int radius,
    int width,
    uint32_t color)
{
    if (radius <= 0 || width <= 0) return;
    int inner_radius = radius - width;
    uint32_t resolved = resolve_color(color);
    int y;
    for (y = -radius; y <= radius; ++y) {
        int x;
        for (x = -radius; x <= radius; ++x) {
            int dx = (x < 0 ? -x : x) * 8, dy = (y < 0 ? -y : y) * 8;
            int coverage = disk_coverage(dx, dy, radius * 8);
            if (inner_radius > 0) {
                int inner_cov = disk_coverage(dx, dy, inner_radius * 8);
                coverage -= inner_cov;
                if (coverage < 0) coverage = 0;
            }
            if (coverage > 0) blend_pixel(pixels, stride, center_x + x, center_y + y,
                resolved, (uint8_t)(coverage > 255 ? 255 : coverage));
        }
    }
}

/* 抗锯齿环形进度：radius 为环中心线半径，stroke 为环宽。弧从正上方顺时针，
 * 前沿按弧长做羽化；fraction 不小于 1 时画满环。 */
void draw_ring_progress(
    uint32_t *pixels,
    uint32_t stride,
    int center_x,
    int center_y,
    int radius,
    int stroke,
    float fraction,
    uint32_t track_color,
    uint32_t fill_color)
{
    const float feather = 1.25f;
    const float half_feather = feather * 0.5f;
    const float two_pi = 6.2831853f;
    float half_stroke = stroke * 0.5f;
    float reach = (float)radius + half_stroke + half_feather;
    uint32_t track_resolved = resolve_color(track_color);
    uint32_t fill_resolved = resolve_color(fill_color);
    int x_start = center_x - (int)reach - 1;
    int y_start = center_y - (int)reach - 1;
    int x_end = center_x + (int)reach + 2;
    int y_end = center_y + (int)reach + 2;
    int x;
    int y;
    bool full;
    float sweep;
    if (stroke <= 0 || radius <= 0) return;
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;
    full = fraction >= 0.999f;
    sweep = fraction * two_pi;
    if (x_start < 0) x_start = 0;
    if (y_start < 0) y_start = 0;
    if (x_end > SCREEN_WIDTH) x_end = SCREEN_WIDTH;
    if (y_end > SCREEN_HEIGHT) y_end = SCREEN_HEIGHT;
    for (y = y_start; y < y_end; ++y) {
        for (x = x_start; x < x_end; ++x) {
            float fx = (float)x + 0.5f - (float)center_x;
            float fy = (float)y + 0.5f - (float)center_y;
            float dist = sqrtf(fx * fx + fy * fy);
            float band = fabsf(dist - (float)radius);
            float band_t;
            float band_coverage;
            float fill_coverage;
            if (band >= half_stroke + half_feather) continue;
            band_t = (half_stroke + half_feather - band) / feather;
            if (band_t > 1) band_t = 1;
            band_coverage = band_t * band_t * (3.0f - 2.0f * band_t);
            fill_coverage = band_coverage;
            if (!full) {
                float angle = atan2f(fx, -fy);
                float signed_arc;
                float fill_t;
                if (angle < 0) angle += two_pi;
                /* 跨前沿的带符号弧长：越入弧内越接近 1，越出弧外为 0，边缘半羽化。 */
                signed_arc = (sweep - angle) * dist;
                fill_t = (signed_arc + half_feather) / feather;
                if (fill_t < 0) fill_t = 0;
                if (fill_t > 1) fill_t = 1;
                fill_coverage = band_coverage * (fill_t * fill_t * (3.0f - 2.0f * fill_t));
            }
            if (band_coverage > 0) {
                blend_pixel(pixels, stride, x, y, track_resolved,
                            (uint8_t)(band_coverage * 255.0f + 0.5f));
            }
            if (fill_coverage > 0) {
                blend_pixel(pixels, stride, x, y, fill_resolved,
                            (uint8_t)(fill_coverage * 255.0f + 0.5f));
            }
        }
    }
}

/* 抗锯齿粗线：段距离 SDF + 与圆角一致的 1.25px smoothstep 羽化，端点为圆头。 */
void draw_line(
    uint32_t *pixels,
    uint32_t stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int width,
    uint32_t color)
{
    const float feather = 1.25f;
    const float half_feather = feather * 0.5f;
    float px0 = (float)x0 + 0.5f;
    float py0 = (float)y0 + 0.5f;
    float dx = (float)x1 - (float)x0;
    float dy = (float)y1 - (float)y0;
    float length_sq = dx * dx + dy * dy;
    float half = width * 0.5f;
    uint32_t resolved = resolve_color(color);
    int x_start = (x0 < x1 ? x0 : x1) - width - 2;
    int x_end = (x0 > x1 ? x0 : x1) + width + 3;
    int y_start = (y0 < y1 ? y0 : y1) - width - 2;
    int y_end = (y0 > y1 ? y0 : y1) + width + 3;
    int x;
    int y;
    if (width <= 0) return;
    if (length_sq < 0.000001f) length_sq = 1.0f;
    if (x_start < 0) x_start = 0;
    if (y_start < 0) y_start = 0;
    if (x_end > SCREEN_WIDTH) x_end = SCREEN_WIDTH;
    if (y_end > SCREEN_HEIGHT) y_end = SCREEN_HEIGHT;
    for (y = y_start; y < y_end; ++y) {
        for (x = x_start; x < x_end; ++x) {
            float fx = (float)x + 0.5f - px0;
            float fy = (float)y + 0.5f - py0;
            float t = (fx * dx + fy * dy) / length_sq;
            float nx;
            float ny;
            float edge;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            nx = px0 + dx * t - ((float)x + 0.5f);
            ny = py0 + dy * t - ((float)y + 0.5f);
            edge = sqrtf(nx * nx + ny * ny) - half;
            if (edge >= half_feather) continue;
            if (edge <= -half_feather) {
                set_pixel(pixels, stride, x, y, resolved);
            } else {
                float s = (half_feather - edge) / feather;
                blend_pixel(pixels, stride, x, y, resolved,
                            (uint8_t)(s * s * (3.0f - 2.0f * s) * 255.0f + 0.5f));
            }
        }
    }
}

void draw_status_symbol(
    uint32_t *pixels,
    uint32_t stride,
    int x,
    int y,
    uint32_t color,
    int kind)
{
    /* Callers pass the visual center; keep every status glyph inside one
     * shared 20x20 box so the symbol aligns with the capsule text baseline. */
    int left = x - 10;
    int top = y - 10;
    draw_rect_outline(pixels, stride, (UiRect){left, top, 20, 20}, 4, 2, color);
    if (kind == 1) {
        draw_line(pixels, stride, left + 4, top + 11, left + 8, top + 15, 2, color);
        draw_line(pixels, stride, left + 8, top + 15, left + 16, top + 5, 2, color);
    } else if (kind == 2) {
        fill_rect(pixels, stride, (UiRect){left + 8, top + 4, 4, 9}, color);
        fill_rect(pixels, stride, (UiRect){left + 8, top + 15, 4, 3}, color);
    } else if (kind == 3) {
        draw_line(pixels, stride, left + 5, top + 5, left + 15, top + 15, 2, color);
        draw_line(pixels, stride, left + 15, top + 5, left + 5, top + 15, 2, color);
    } else {
        fill_rect(pixels, stride, (UiRect){left + 8, top + 4, 4, 3}, color);
        fill_rect(pixels, stride, (UiRect){left + 8, top + 9, 4, 8}, color);
    }
}
