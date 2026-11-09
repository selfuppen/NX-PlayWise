// Frozen scalar reference from c495fc0fb0d7690a9a6f702124da971a1f9827ed.
// Test-only pixel oracle; production never compiles this implementation.
static unsigned reference_distances;
static float reference_sdf(UiRect rect, int radius, float x, float y)
{
    ++reference_distances;
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
static inline float reference_smootherstep(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/* 工业级圆角感知软阴影（支持自定义色彩与主题强度缩放）：
 * 1. 采用精准圆角 SDF 几何遮罩剔除卡片实心本体，保留四个转角弧线外的完整阴影与抗锯齿带，
 *    根除外接矩形 AABB 直角截断导致的转角分层断裂；
 * 2. 单次像素遍历中融合 Key（向下投影核心）与 Ambient（大范围漫反射光晕）双层物理软阴影；
 * 3. 采用五次 Smootherstep 极平滑衰减，并在单次 blend_pixel 中完成 Alpha 复合，画质与帧率兼顾。 */
static void reference_shadow(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    int radius,
    int blur,
    int peak_alpha,
    int y_bias,
    uint32_t shadow_color,
    bool scale_by_theme)
{
    if (blur <= 0 || peak_alpha <= 0) return;
    if (rect.width <= 0 || rect.height <= 0) return;
    if (radius > rect.width / 2) radius = rect.width / 2;
    if (radius > rect.height / 2) radius = rect.height / 2;

    /* 阴影峰值按主题 shadow_strength 缩放，暗色主题阴影更实。 */
    if (scale_by_theme && g_palette && g_palette->shadow_strength) {
        peak_alpha = peak_alpha * (int)g_palette->shadow_strength / 100;
        if (peak_alpha > 255) peak_alpha = 255;
    }

    /* 双层软阴影配比分解：
     * Key 层：聚集清晰，偏置随光源下移；
     * Ambient 层：扩散更广，偏置轻微，营造柔和弥散光晕。 */
    int blur_key = blur * 3 / 5;
    if (blur_key < 2) blur_key = 2;
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
            float body_dist = reference_sdf(rect, radius, px, py);
            if (body_dist <= -0.5f) continue;
            if (body_dist >= outer_cutoff) continue;

            /* Ambient 层计算 */
            int a_amb = 0;
            float dist_amb = reference_sdf(rect, radius, px, py - (float)bias_amb);
            if (dist_amb < (float)blur_amb) {
                if (dist_amb <= 0.0f) {
                    a_amb = alpha_amb;
                } else {
                    float t = 1.0f - dist_amb * inv_blur_amb;
                    a_amb = (int)(alpha_amb * reference_smootherstep(t) + 0.5f);
                }
            }

            /* Key 层计算 */
            int a_key = 0;
            float dist_key = reference_sdf(rect, radius, px, py - (float)bias_key);
            if (dist_key < (float)blur_key) {
                if (dist_key <= 0.0f) {
                    a_key = alpha_key;
                } else {
                    float t = 1.0f - dist_key * inv_blur_key;
                    a_key = (int)(alpha_key * reference_smootherstep(t) + 0.5f);
                }
            }

            if (a_amb == 0 && a_key == 0) continue;

            /* 标准复合 Alpha 混合：A_total = A1 + A2 - A1*A2/255 */
            int total_alpha = a_amb + a_key - (a_amb * a_key + 127) / 255;
            if (total_alpha > 255) total_alpha = 255;
            if (total_alpha > 0) {
                blend_pixel(pixels, stride, x, y, shadow_color, (uint8_t)total_alpha);
            }
        }
    }
}
