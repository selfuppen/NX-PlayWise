#include "ui_render_internal.h"

UiRuntime g_ui;
const PtcUiPalette *g_palette;
PtcUiThemeView g_theme = {
    PTC_UI_THEME_SYSTEM,
    PTC_UI_RESOLVED_LIGHT,
    NULL,
    false
};

int64_t ptc_ui_render_now(void)
{
#ifdef PTC_UI_PREVIEW_WALL_TIME
    return PTC_UI_PREVIEW_WALL_TIME;
#else
    return (int64_t)time(NULL);
#endif
}


bool is_docked_mode(void)
{
#if defined(__SWITCH__) && !defined(PLAYWISE_EDEN)
    return appletGetOperationMode() == AppletOperationMode_Console;
#else
    return false;
#endif
}

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

void draw_header(uint32_t *pixels, uint32_t stride, const char *title, const char *subtitle)
{
    fill_round_rect(pixels, stride, (UiRect){54, 25, 48, 48}, 16, UI_ACCENT);
    fill_round_rect(pixels, stride, (UiRect){87, 24, 12, 12}, 6, UI_CORAL);
    draw_line(pixels, stride, 65, 49, 79, 49, 3, UI_ON_ACCENT);
    draw_line(pixels, stride, 72, 42, 72, 56, 3, UI_ON_ACCENT);
    draw_circle_outline(pixels, stride, 90, 44, 3, 3, UI_ON_ACCENT);
    draw_circle_outline(pixels, stride, 86, 55, 3, 3, UI_ON_ACCENT);
    draw_text(pixels, stride, 124, 49, title, 30, UI_INK);
    draw_text(pixels, stride, 124, 77, subtitle, 18, UI_MUTED);
}

uint32_t time_projection_color(PtcUiTimeState state)
{
    switch (state) {
    case PTC_UI_TIME_NORMAL: return UI_SUCCESS;
    case PTC_UI_TIME_REMINDER: return UI_WARNING;
    case PTC_UI_TIME_DANGER:
    case PTC_UI_TIME_EXHAUSTED:
    case PTC_UI_TIME_DISABLED:
    case PTC_UI_TIME_PROTECTION: return UI_DANGER;
    case PTC_UI_TIME_UNLIMITED: return UI_ACCENT;
    case PTC_UI_TIME_RECOVERY:
    case PTC_UI_TIME_TEMPORARY_UNLOCK:
    case PTC_UI_TIME_WAITING: return UI_WARNING;
    default: return UI_MUTED;
    }
}

typedef struct {
    const char *label;
    uint32_t color;
    uint32_t bg_color;
} UiActiveRuleBadge;

static UiActiveRuleBadge get_active_rule_badge(const PtcUiModel *model)
{
    UiActiveRuleBadge badge;
    if (model->bedtime_active && !model->bedtime_skipped) {
        badge.label = "就寝限制";
        badge.color = UI_DANGER;
        badge.bg_color = UI_DANGER_SOFT;
        return badge;
    }
    if (!model->status_loaded) {
        badge.label = "待确认";
        badge.color = UI_MUTED;
        badge.bg_color = UI_PAGE;
        return badge;
    }
    if (strcmp(model->rule_source, "today_override") == 0) {
        badge.label = "今日调整";
        badge.color = UI_ACCENT;
        badge.bg_color = UI_ACCENT_SOFT;
    } else if (strcmp(model->rule_source, "scheduled_override") == 0) {
        badge.label = "临时计划";
        badge.color = UI_ACCENT;
        badge.bg_color = UI_ACCENT_SOFT;
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0) {
        badge.label = "法定假日";
        badge.color = UI_SUCCESS;
        badge.bg_color = UI_SUCCESS_SOFT;
    } else if (strcmp(model->rule_source, "makeup_workday") == 0) {
        badge.label = "调休工作";
        badge.color = UI_SUCCESS;
        badge.bg_color = UI_SUCCESS_SOFT;
    } else {
        badge.label = "周计划";
        badge.color = UI_ACCENT;
        badge.bg_color = UI_ACCENT_SOFT;
    }
    return badge;
}

void draw_time_status_bar(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    PtcUiTimeProjection status;
    UiRect box = {754, 24, 472, 66};
    char fitted[64];
    char fitted_fresh[64];
    uint32_t color;
    UiActiveRuleBadge badge;

    ptc_ui_project_time_status(model, ptc_ui_render_now(), &status);
    color = time_projection_color(status.state);
    badge = get_active_rule_badge(model);

    fill_round_rect(pixels, stride, box, 14, UI_SURFACE);
    draw_rect_outline(pixels, stride, box, 14, 1, UI_BORDER);

    /* 时钟显示 */
    draw_text(pixels, stride, box.x + 14, box.y + 26, status.clock_text, 17, UI_INK);

    /* 当前生效规则胶囊徽章 */
    UiRect pill = {box.x + 68, box.y + 11, 74, 22};
    fill_round_rect(pixels, stride, pill, 6, badge.bg_color);
    draw_rect_outline(pixels, stride, pill, 6, 1, badge.color);
    fill_round_rect(pixels, stride, (UiRect){pill.x + 6, pill.y + 8, 6, 6}, 3, badge.color);
    draw_text(pixels, stride, pill.x + 16, box.y + 26, badge.label, 12, badge.color);

    /* 剩余/状态文本 */
    fit_text(fitted, sizeof(fitted), status.remaining_text, 16, 185);
    draw_text(pixels, stride, box.x + 150, box.y + 26, fitted, 16, color);

    /* 更新时效 */
    fit_text(fitted_fresh, sizeof(fitted_fresh), status.freshness_text, 12, 110);
    int fresh_w = measure_text(fitted_fresh, 12);
    draw_text(pixels, stride, box.x + box.width - 14 - fresh_w, box.y + 26, fitted_fresh, 12, UI_MUTED);

    /* 额度进度槽 */
    int demo_w = model->demo_secret_enabled ? 46 : 0;
    UiRect track = {box.x + 14, box.y + 46, box.width - 28 - demo_w, 6};
    fill_round_rect(pixels, stride, track, 3, UI_RAISED);
    if (status.progress_available && status.progress_per_mille > 0) {
        int width = track.width * status.progress_per_mille / 1000;
        if (width < 4) width = 4;
        if (width > track.width) width = track.width;
        fill_round_rect(pixels, stride, (UiRect){track.x, track.y, width, track.height}, 3, color);
    }
    if (model->demo_secret_enabled) {
        UiRect dbadge = {box.x + box.width - 14 - 38, box.y + 40, 38, 18};
        fill_round_rect(pixels, stride, dbadge, 5, UI_DANGER_SOFT);
        draw_text_center(pixels, stride, dbadge, "演示", 11, UI_DANGER);
    }
}

static void draw_single_key_glyph(uint32_t *pixels, uint32_t stride, int x, int y, int size, const char *key_str, bool disabled)
{
    int d = size;
    int r = d / 2;
    uint32_t bg_col;
    uint32_t border_col;
    uint32_t fg_col;

    if (disabled) {
        bg_col = UI_RAISED;
        border_col = UI_BORDER;
        fg_col = UI_DISABLED;
    } else {
        bg_col = UI_KEY_GLYPH_BG;
        border_col = UI_KEY_GLYPH_BORDER;
        fg_col = UI_INK;
    }
    fill_round_rect(pixels, stride, (UiRect){x, y, d, d}, r, bg_col);
    draw_rect_outline(pixels, stride, (UiRect){x, y, d, d}, r, 1, border_col);

    if (strcmp(key_str, "+") == 0) {
        int cx = x + r, cy = y + r;
        draw_line(pixels, stride, cx - 4, cy, cx + 4, cy, 2, fg_col);
        draw_line(pixels, stride, cx, cy - 4, cx, cy + 4, 2, fg_col);
    } else if (strcmp(key_str, "-") == 0) {
        int cx = x + r, cy = y + r;
        draw_line(pixels, stride, cx - 4, cy, cx + 4, cy, 2, fg_col);
    } else {
        draw_text_center(pixels, stride, (UiRect){x, y, d, d}, key_str, 14, fg_col);
    }
}

void draw_shoulder_key_glyph(uint32_t *pixels, uint32_t stride, int x, int y, int width, int height, const char *key_str, bool disabled)
{
    uint32_t bg_col;
    uint32_t border_col;
    uint32_t fg_col;

    if (disabled) {
        bg_col = UI_RAISED;
        border_col = UI_BORDER;
        fg_col = UI_DISABLED;
    } else {
        bg_col = UI_KEY_GLYPH_BG;
        border_col = UI_KEY_GLYPH_BORDER;
        fg_col = UI_INK;
    }
    fill_round_rect(pixels, stride, (UiRect){x, y, width, height}, 5, bg_col);
    draw_rect_outline(pixels, stride, (UiRect){x, y, width, height}, 5, 1, border_col);
    draw_text_center(pixels, stride, (UiRect){x, y, width, height}, key_str, 13, fg_col);
}

void draw_arrow_glyph(uint32_t *pixels, uint32_t stride, int cx, int cy, bool up, uint32_t color)
{
    int h = 5;
    for (int dy = 0; dy <= h; ++dy) {
        int span = up ? dy : (h - dy);
        int y = up ? (cy - h / 2 + dy) : (cy - h / 2 + dy);
        draw_line(pixels, stride, cx - span, y, cx + span, y, 1, color);
    }
}

void draw_r_stick_glyph(uint32_t *pixels, uint32_t stride, int x, int y, int size, int dir)
{
    int r = size / 2;
    int cx = x + r;
    uint32_t bg_col = UI_KEY_GLYPH_BG;
    uint32_t border_col = UI_KEY_GLYPH_BORDER;
    uint32_t fg_col = UI_INK;

    fill_round_rect(pixels, stride, (UiRect){x, y, size, size}, r, bg_col);
    draw_rect_outline(pixels, stride, (UiRect){x, y, size, size}, r, 1, border_col);

    int offset_y = dir > 0 ? -1 : (dir < 0 ? 1 : 0);
    draw_text_center(pixels, stride, (UiRect){x, y + offset_y, size, size}, "R", 12, fg_col);

    draw_line(pixels, stride, cx, y - 4, cx, y - 2, 1, dir > 0 ? UI_ACCENT : UI_MUTED);
    draw_line(pixels, stride, cx, y + size + 2, cx, y + size + 4, 1, dir < 0 ? UI_ACCENT : UI_MUTED);
}

void draw_button_label(uint32_t *pixels, uint32_t stride, UiRect box, const char *label, int size, uint32_t color)
{
    if (!label || !*label) return;

    bool disabled = (color == UI_DISABLED);

    /* 匹配复合肩键 "L/R  " 或 "L/R " */
    if (strncmp(label, "L/R  ", 5) == 0 || strncmp(label, "L/R ", 4) == 0) {
        const char *rest = strncmp(label, "L/R  ", 5) == 0 ? label + 5 : label + 4;
        int rest_w = measure_text(rest, size);
        int slash_w = measure_text("/", 14);
        int total_w = 24 + 4 + slash_w + 4 + 24 + 8 + rest_w;
        int start_x = box.x + (box.width - total_w) / 2;
        if (start_x < box.x + 2) start_x = box.x + 2;
        int gly_y = box.y + (box.height - 20) / 2;
        int baseline = box.y + (box.height + size - 4) / 2;

        draw_shoulder_key_glyph(pixels, stride, start_x, gly_y, 24, 20, "L", disabled);
        draw_text(pixels, stride, start_x + 28, baseline, "/", 14, color);
        draw_shoulder_key_glyph(pixels, stride, start_x + 28 + slash_w + 4, gly_y, 24, 20, "R", disabled);
        draw_text(pixels, stride, start_x + 28 + slash_w + 4 + 24 + 8, baseline, rest, size, color);
        return;
    }

    /* 匹配单肩键 "ZL  " 或 "ZR  " */
    if (strncmp(label, "ZL  ", 4) == 0 || strncmp(label, "ZR  ", 4) == 0) {
        char key_buf[4];
        memcpy(key_buf, label, 2);
        key_buf[2] = '\0';
        const char *rest = label + 4;
        int rest_w = measure_text(rest, size);
        int total_w = 32 + 8 + rest_w;
        int start_x = box.x + (box.width - total_w) / 2;
        if (start_x < box.x + 2) start_x = box.x + 2;
        int gly_y = box.y + (box.height - 20) / 2;
        int baseline = box.y + (box.height + size - 4) / 2;

        draw_shoulder_key_glyph(pixels, stride, start_x, gly_y, 32, 20, key_buf, disabled);
        draw_text(pixels, stride, start_x + 32 + 8, baseline, rest, size, color);
        return;
    }

    /* 匹配单字符圆键 "A  ", "B  ", "X  ", "Y  ", "+  ", "-  " */
    if ((label[0] == 'A' || label[0] == 'B' || label[0] == 'X' || label[0] == 'Y' ||
         label[0] == '+' || label[0] == '-') && (label[1] == ' ' && label[2] == ' ')) {
        char key_buf[2] = {label[0], '\0'};
        const char *rest = label + 3;
        int rest_w = measure_text(rest, size);
        int total_w = 22 + 8 + rest_w;
        int start_x = box.x + (box.width - total_w) / 2;
        if (start_x < box.x + 2) start_x = box.x + 2;
        int gly_y = box.y + (box.height - 22) / 2;
        int baseline = box.y + (box.height + size - 4) / 2;

        draw_single_key_glyph(pixels, stride, start_x, gly_y, 22, key_buf, disabled);
        draw_text(pixels, stride, start_x + 22 + 8, baseline, rest, size, color);
        return;
    }

    /* 普通文本居中展示 */
    draw_text_center(pixels, stride, box, label, size, color);
}

void draw_footer_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect, const char *label)
{
    draw_button_label(pixels, stride, to_uirect(rect), label, 18, UI_MUTED);
}

void draw_parent_status_footer(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect box = to_uirect(ptc_ui_parent_footer_rect(4));
    char summary[96];
    uint32_t color = UI_DANGER;

    if (!ptc_ui_parent_status_alert_visible(model)) return;
    if (model->disable_flag_present) {
        snprintf(summary, sizeof(summary), "▲ 控制已停用  |  按 A 查看恢复");
    } else if (model->recovery_active) {
        snprintf(summary, sizeof(summary), "▲ 存在待恢复事务  |  按 A 进入排障");
    } else if (strcmp(model->setup_phase, "protection") == 0) {
        snprintf(summary, sizeof(summary), "▲ 系统防护已激活  |  按 A 查看详情");
    } else if (model->temporary_unlocked_available && model->temporary_unlocked) {
        snprintf(summary, sizeof(summary), "● 临时解除中  |  按 A 管理设置");
        color = UI_WARNING;
    } else {
        snprintf(summary, sizeof(summary), "▲ 系统异常需处理  |  按 A 进入支持");
    }

    fill_round_rect(pixels, stride, box, 12,
        color == UI_WARNING ? UI_WARNING_SOFT : UI_DANGER_SOFT);
    draw_rect_outline(pixels, stride, box, 12, 1, color);
    if (model->parent_footer_focused && model->parent_footer_selection == 1) {
        fill_round_rect(pixels, stride, box, 12, UI_RGB(UI_BLENDED(focus)));
        fill_round_rect(pixels, stride, (UiRect){box.x + 3, box.y + 3, box.width - 6, box.height - 6},
            9, UI_RGB(UI_BLENDED(surface_raised)));
    }
    draw_text_center(pixels, stride, box, summary, 17, color);
}

UiRect to_uirect(PtcUiRect rect)
{
    UiRect out = {rect.x, rect.y, rect.w, rect.h};
    return out;
}

void draw_dialog_button(
    uint32_t *pixels,
    uint32_t stride,
    PtcUiRect rect,
    const char *label,
    uint32_t background,
    uint32_t foreground,
    bool outline)
{
    UiRect box = to_uirect(rect);
    fill_round_rect(pixels, stride, box, 12, outline ? UI_RGB(UI_BLENDED(surface_raised)) : background);
    draw_button_label(pixels, stride, box, label, 21, foreground);
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

void draw_candidate_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect,
    const char *label, uint32_t background, uint32_t foreground, bool selected, bool disabled)
{
    UiRect box = to_uirect(rect);
    bool primary = foreground == UI_ON_ACCENT;
    uint32_t fill = disabled ? UI_RAISED : (selected && !primary ? UI_ACCENT_SOFT : background);
    fill_round_rect(pixels, stride, box, 12, fill);
    if (selected) draw_focus_ring(pixels, stride, box, 12);
    else draw_rect_outline(pixels, stride, box, 12, 1, UI_CONTROL);
    draw_button_label(pixels, stride, box, label, 20, disabled ? UI_DISABLED : foreground);
}

void draw_overlay_actions(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, const char *confirm_label)
{
    PtcUiRect confirm = ptc_ui_confirm_rect(model->overlay);
    bool hold = model->confirm_hold_required && model->overlay == PTC_UI_OVERLAY_CONFIRM;
    if (hold) {
        UiRect button = to_uirect(confirm);
        char progress_label[48];
        fill_round_rect(pixels, stride, button, 12, UI_ACCENT);
        if (model->confirm_hold_progress > 0) {
            UiRect progress = button;
            progress.width = progress.width * model->confirm_hold_progress / 1000;
            fill_round_rect(pixels, stride, progress, 12, UI_SUCCESS);
            snprintf(progress_label, sizeof(progress_label), "A  继续按住  %u%%",
                     (unsigned int)(model->confirm_hold_progress / 10));
        } else {
            snprintf(progress_label, sizeof(progress_label), "A  按住 1 秒确认");
        }
        draw_text_center(pixels, stride, button,
                         model->confirm_hold_progress >= 1000 ? "A  确认完成" : progress_label,
                         20, UI_ON_ACCENT);
        draw_rect_outline(pixels, stride, button, 12, 2,
                          model->confirm_hold_progress > 0 ? UI_SUCCESS : UI_ACCENT);
    } else {
        draw_dialog_button(pixels, stride, confirm, confirm_label, UI_ACCENT, UI_ON_ACCENT, false);
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  取消", UI_RAISED, UI_INK, true);
    if (model->overlay == PTC_UI_OVERLAY_CONFIRM &&
        (model->operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
         model->operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
         model->operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY)) {
        PtcUiRect selected = model->overlay_selection == 0
            ? ptc_ui_cancel_rect(model->overlay) : ptc_ui_confirm_rect(model->overlay);
        draw_rect_outline(pixels, stride, to_uirect(selected), 12, 3, UI_ACCENT);
    }
}

void format_event_time(int64_t timestamp, bool full, char *out, size_t out_size)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint16_t event_day;
    uint16_t today;
    uint16_t minute;
    if (!out || out_size == 0) return;
    snprintf(out, out_size, "时间未知");
    if (timestamp <= 0) {
        snprintf(out, out_size, "时间未知");
        return;
    }
    {
        time_t event_time = (time_t)timestamp;
        time_t now_time = (time_t)ptc_ui_render_now();
        struct tm event_local;
        struct tm today_local;
        if (localtime_r(&event_time, &event_local) == NULL ||
            localtime_r(&now_time, &today_local) == NULL ||
            !ptc_day_index_from_date((uint16_t)(event_local.tm_year + 1900),
                (uint8_t)(event_local.tm_mon + 1), (uint8_t)event_local.tm_mday, &event_day) ||
            !ptc_day_index_from_date((uint16_t)(today_local.tm_year + 1900),
                (uint8_t)(today_local.tm_mon + 1), (uint8_t)today_local.tm_mday, &today)) return;
        minute = (uint16_t)(event_local.tm_hour * 60 + event_local.tm_min);
    }
    if (full && ptc_date_from_day_index(event_day, &year, &month, &day)) {
        snprintf(out, out_size, "%u-%02u-%02u %02u:%02u", year, month, day, minute / 60, minute % 60);
    } else if (event_day == today) {
        snprintf(out, out_size, "今天 %02u:%02u", minute / 60, minute % 60);
    } else if ((uint16_t)(event_day + 1u) == today) {
        snprintf(out, out_size, "昨天 %02u:%02u", minute / 60, minute % 60);
    } else if (ptc_date_from_day_index(event_day, &year, &month, &day)) {
        snprintf(out, out_size, "%u-%02u-%02u %02u:%02u", year, month, day, minute / 60, minute % 60);
    } else {
        snprintf(out, out_size, "时间未知");
    }
}

void draw_notice(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    PtcUiNoticeProjection notice;
    ptc_ui_project_notice(model, &notice);
    if (!notice.visible) return;
    bool danger = notice.level == PTC_UI_NOTICE_DANGER;
    bool warning = notice.level == PTC_UI_NOTICE_WARNING;
    uint32_t accent = danger ? UI_DANGER : (warning ? UI_WARNING : UI_SUCCESS);

    /* 共享的右下角状态胶囊只显示摘要，详情由家长主动打开。 */
    PtcUiRect n_rect = ptc_ui_notice_rect();
    UiRect box = to_uirect(n_rect);
    draw_round_rect_shadow(pixels, stride, box, 18, 12, 40, 3);
    fill_round_rect(pixels, stride, box, 18, danger ? UI_DANGER_SOFT : (warning ? UI_WARNING_SOFT : UI_SURFACE));
    draw_rect_outline(pixels, stride, box, 18, 1, danger ? UI_DANGER : (warning ? UI_WARNING : UI_BORDER));

    PtcUiRect icon_rect = ptc_ui_notice_status_icon_rect(n_rect.y);
    int icon_cx = icon_rect.x + icon_rect.w / 2;
    int icon_cy = icon_rect.y + icon_rect.h / 2;
    draw_status_symbol(pixels, stride, icon_cx, icon_cy, accent, danger ? 3 : (warning ? 2 : 1));

    if (notice.has_details) {
        UiRect detail_btn = to_uirect(ptc_ui_notice_details_rect());
        fill_round_rect(pixels, stride, detail_btn, 8, danger ? UI_DANGER : (warning ? UI_WARNING : UI_ACCENT_SOFT));
        draw_rect_outline(pixels, stride, detail_btn, 8, 1, danger ? UI_DANGER : (warning ? UI_WARNING : UI_ACCENT));
        draw_text_center(pixels, stride, detail_btn, "X 详情", 13, (danger || warning) ? UI_ON_ACCENT : UI_ACCENT);
    }

    int max_text_w = box.width - 50 - (notice.has_details ? 88 : 16);
    char fitted_msg[128];
    fit_text(fitted_msg, sizeof(fitted_msg), notice.summary, 15, max_text_w);
    int baseline = box.y + (box.height + 15) / 2 - 2;
    draw_text(pixels, stride, box.x + 44, baseline, fitted_msg, 15, danger ? UI_DANGER : UI_INK);
}

void draw_notice_details_dialog(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell_model = *model;
    shell_model.overlay_title[0] = '\0';
    shell_model.overlay_body[0] = '\0';
    PtcUiNoticeProjection notice;
    ptc_ui_project_notice(model, &notice);
    bool danger = notice.level == PTC_UI_NOTICE_DANGER;
    bool warning = notice.level == PTC_UI_NOTICE_WARNING;
    uint32_t accent = danger ? UI_DANGER : (warning ? UI_WARNING : UI_SUCCESS);

    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 780, 420);

    int icon_cx = dialog.x + 48;
    int icon_cy = dialog.y + 70;
    draw_status_symbol(pixels, stride, icon_cx, icon_cy, accent, danger ? 3 : (warning ? 2 : 1));

    const char *msg = notice.summary[0] ? notice.summary : "操作状态与反馈";
    char fitted_msg[192];
    fit_text(fitted_msg, sizeof(fitted_msg), msg, 20, dialog.width - 108);
    draw_text(pixels, stride, dialog.x + 72, dialog.y + 76, fitted_msg, 20, danger ? UI_DANGER : UI_INK);

    UiRect card = {dialog.x + 36, dialog.y + 104, dialog.width - 72, 230};
    fill_round_rect(pixels, stride, card, 12, UI_PAGE);
    draw_rect_outline(pixels, stride, card, 12, 1, UI_BORDER);

    int card_base = card.y + 36;
    if (notice.details[0]) {
        draw_text(pixels, stride, card.x + 20, card_base, "详细信息与排查指引：", 16, UI_ACCENT);
        card_base = draw_wrapped_text(pixels, stride, card.x + 20, card_base + 32,
            notice.details, 16, card.width - 40, 24, 4, UI_INK);
    } else {
        draw_text(pixels, stride, card.x + 20, card_base, "操作已完成，当前没有更多详细日志。", 16, UI_MUTED);
        card_base += 32;
    }

    if (model->command_name[0]) {
        char execution[256];
        snprintf(execution, sizeof(execution), "执行命令：%s    传输模式：%s",
                 model->command_name[0] ? model->command_name : "无",
                 model->transport_label[0] ? model->transport_label : "默认");
        draw_text(pixels, stride, card.x + 20, card.y + card.height - 24, execution, 14, UI_MUTED);
    }

    PtcUiRect btn_rect = ptc_ui_cancel_rect(model->overlay);
    draw_dialog_button(pixels, stride, btn_rect, "A / B  关闭", UI_ACCENT, UI_ON_ACCENT, false);
}
