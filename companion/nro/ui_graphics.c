#include "ui_graphics.h"
#include "../album_restriction.h"

#include <switch.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../common/time/ptc_time.h"
#include "../../common/rules/holiday_calendar.h"
#include "../../common/version.h"

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define UI_RGB(rgb) (0x01000000u | (rgb))

/* Semantic tokens can be used in constant action tables. UI_RGB is reserved
 * for explicit palette values; neither path infers meaning from RGB ranges. */
typedef enum {
    UI_INK = 0x02000000u + 0,
    UI_MUTED = 0x02000000u + 1,
    UI_DISABLED = 0x02000000u + 2,
    UI_SURFACE = 0x02000000u + 3,
    UI_RAISED = 0x02000000u + 4,
    UI_PAGE = 0x02000000u + 5,
    UI_BORDER = 0x02000000u + 6,
    UI_CONTROL = 0x02000000u + 7,
    UI_ACCENT = 0x02000000u + 8,
    UI_FOCUS = 0x02000000u + 9,
    UI_ON_ACCENT = 0x02000000u + 10,
    UI_ACCENT_SOFT = 0x02000000u + 11,
    UI_SUCCESS = 0x02000000u + 12,
    UI_WARNING = 0x02000000u + 13,
    UI_DANGER = 0x02000000u + 14,
    UI_SUCCESS_SOFT = 0x02000000u + 15,
    UI_WARNING_SOFT = 0x02000000u + 16,
    UI_DANGER_SOFT = 0x02000000u + 17,
    UI_CORAL = 0x02000000u + 18,
} UiColor;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} UiRect;

typedef struct {
    Framebuffer framebuffer;
    FT_Library library;
    FT_Face face;
    bool framebuffer_ready;
    bool font_ready;
    bool pl_ready;
} UiRuntime;

typedef struct {
    const char *title;
    const char *subtitle;
    uint32_t accent;
} UiAction;

static UiRuntime g_ui;
static const PtcUiPalette *g_palette;
static PtcUiThemeView g_theme = {
    PTC_UI_THEME_SYSTEM,
    PTC_UI_RESOLVED_LIGHT,
    NULL,
    false
};

static UiRect to_uirect(PtcUiRect rect);
static const char *rule_mode_label(PtcRuleMode mode);
static void format_status_age(const PtcUiModel *model, char *out, size_t out_size);
static uint32_t status_age_color(const PtcUiModel *model);
static const char *rule_source_label(const char *source);
static void draw_toggle_switch(uint32_t *pixels, uint32_t stride, UiRect rect, bool is_on,
                               bool selected, bool disabled, const char *on_label, const char *off_label);

static uint32_t pack_rgb(uint32_t rgb)
{
    return RGBA8_MAXALPHA((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

static uint32_t resolve_color(uint32_t source)
{
    if (source & 0x01000000u) return pack_rgb(source & 0xFFFFFFu);
    switch (source) {
    case UI_INK: return pack_rgb(g_palette->text_primary);
    case UI_MUTED: return pack_rgb(g_palette->text_secondary);
    case UI_DISABLED: return pack_rgb(g_palette->text_disabled);
    case UI_SURFACE: return pack_rgb(g_palette->surface);
    case UI_RAISED: return pack_rgb(g_palette->surface_raised);
    case UI_PAGE: return pack_rgb(g_palette->page_bg);
    case UI_BORDER: return pack_rgb(g_palette->border_decorative);
    case UI_CONTROL: return pack_rgb(g_palette->border_control);
    case UI_ACCENT: return pack_rgb(g_palette->accent);
    case UI_FOCUS: return pack_rgb(g_palette->focus);
    case UI_ON_ACCENT: return pack_rgb(g_palette->on_accent);
    case UI_ACCENT_SOFT: return pack_rgb(g_palette->accent_soft);
    case UI_SUCCESS: return pack_rgb(g_palette->success);
    case UI_WARNING: return pack_rgb(g_palette->warning);
    case UI_DANGER: return pack_rgb(g_palette->danger);
    case UI_SUCCESS_SOFT: return pack_rgb(g_palette->success_soft);
    case UI_WARNING_SOFT: return pack_rgb(g_palette->warning_soft);
    case UI_DANGER_SOFT: return pack_rgb(g_palette->danger_soft);
    case UI_CORAL: return pack_rgb(g_palette->coral);
    default: return pack_rgb(source);
    }
}

static const UiAction TODAY_ACTIONS[] = {
    {"设置今日总额度", "指定今天全天可玩的分钟数", UI_ACCENT},
    {"临时加时", "在今天额度上增加分钟", UI_SUCCESS},
    {"今日不限时", "今天不设时间上限", UI_SUCCESS},
    {"恢复周计划", "清除今日临时设置，恢复本周规则", UI_MUTED},
};

static const UiAction HOLIDAY_ACTIONS[] = {
    {"国家节假日规则", "开启或关闭自动日历规则", UI_ACCENT},
    {"法定休假", "切换模式或设置休息日额度", UI_SUCCESS},
    {"调休工作日", "切换模式或设置补班日额度", UI_WARNING},
    {"查看当前节假日安排", "查看内置年份、放假日期和调休工作日", UI_MUTED},
    {"保存全部节假日设置", "保存后立即按新设置重新计算今天", UI_ACCENT}
};

static const UiAction GRANT_ACTIONS[] = {
    {"立即生成加时码", "选时长、生成、告诉孩子", UI_SUCCESS},
    {"手机/电脑生成加时码", "优先使用完整交付包中的离线网页", UI_ACCENT},
    {"加时码生成管理", "管理配对信息与导出配置", UI_MUTED},
    {"加时码使用记录", "查看最近 100 条成功兑换", UI_MUTED},
};

static const UiAction SETTINGS_ACTIONS[] = {
    {"外观主题", "跟随系统、浅色或暗色", UI_ACCENT},
    {"修改任我玩PIN", "验证当前 PIN 后设置新 PIN", UI_ACCENT},
    {"家长区快捷键管理", "选择组合并管理孩子区提示", UI_ACCENT},
    {"高级设置", "日期计划、自主缓冲与启动设置", UI_MUTED},
    {"支持与恢复", "遇到问题时，从这里检查和恢复", UI_MUTED},
};

static const UiAction ADVANCED_ACTIONS[] = {
    {"自制程序菜单\n高级入口", "改变 hbmenu 启动方式，不提供防篡改保护", UI_DANGER},
    {"临时日期计划", "考试周、假期或旅行使用，最多 366 天", UI_ACCENT},
    {"今日自主缓冲", "孩子每天可自主领取一次小额缓冲", UI_SUCCESS},
    {"家庭活动记录", "规则、加时和保护事件，最多 200 条", UI_MUTED},
};

static const UiAction GRANT_MANAGER_ACTIONS[] = {
    {"管理加时码设备名", "查看、输入或随机生成设备名", UI_ACCENT},
    {"管理加时码密钥", "查看、输入或随机生成签名密钥", UI_DANGER},
    {"导出手机/电脑配置", "导出供手机或电脑使用的配置文件", UI_SUCCESS},
    {"编辑二维码跳转地址", "修改扫码后打开的网页地址", UI_ACCENT},
    {"恢复二维码跳转默认地址", "恢复项目提供的默认网页地址", UI_MUTED},
};

static const UiAction SUPPORT_ACTIONS[] = {
    {"确认接管系统控制", "预检、保存快照后启用额度管理", UI_ACCENT},
    {"重试修复", "重新检查并恢复安全前置条件", UI_SUCCESS},
    {"紧急停用控制", "停止新的控制写入，仍可诊断与恢复", UI_DANGER},
    {"恢复安装前状态", "恢复原始设置并停用 任我玩", UI_DANGER},
    {"导出诊断包", "导出诊断，不含密钥、PIN 或离线码", UI_MUTED},
    {"软件信息", "查看版本、项目仓库和家长网页", UI_ACCENT},
};

static const UiAction RESUME_CONTROL_ACTION = {
    "解除停用并重新接管", "安全预检通过后恢复后台控制", UI_SUCCESS
};

static const UiAction RECONFIRM_ENVIRONMENT_ACTION = {
    "重新检测并接管", "系统环境变化，确认兼容后恢复控制", UI_WARNING
};

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

static void blend_pixel(uint32_t *pixels, uint32_t stride, int x, int y, uint32_t color, uint8_t alpha)
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

static void fill_rect_packed(uint32_t *pixels, uint32_t stride, UiRect rect, uint32_t color)
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

static void fill_rect(uint32_t *pixels, uint32_t stride, UiRect rect, uint32_t color)
{
    fill_rect_packed(pixels, stride, rect, resolve_color(color));
}

/* Pixel coverage uses eighth-pixel coordinates: 4 x 4 sample centres at
 * 1, 3, 5, 7. Only pixels intersecting a curve need multiple samples. */
static int disk_coverage(int dx, int dy, int radius)
{
    int near_x = dx > 4 ? dx - 4 : 0;
    int near_y = dy > 4 ? dy - 4 : 0;
    int squared = radius * radius;
    int count = 0;
    if ((dx + 4) * (dx + 4) + (dy + 4) * (dy + 4) <= squared) return 16;
    if (near_x * near_x + near_y * near_y >= squared) return 0;
    for (int sy = -3; sy <= 3; sy += 2)
        for (int sx = -3; sx <= 3; sx += 2)
            if ((dx + sx) * (dx + sx) + (dy + sy) * (dy + sy) <= squared) ++count;
    return count;
}

static int round_rect_coverage(UiRect rect, int radius, int x, int y)
{
    int px = x - rect.x, py = y - rect.y;
    if (px < 0 || py < 0 || px >= rect.width || py >= rect.height) return 0;
    int dx = px < radius ? radius * 8 - (px * 8 + 4) :
             (px >= rect.width - radius ? px * 8 + 4 - (rect.width - radius) * 8 : 0);
    int dy = py < radius ? radius * 8 - (py * 8 + 4) :
             (py >= rect.height - radius ? py * 8 + 4 - (rect.height - radius) * 8 : 0);
    return dx && dy ? disk_coverage(dx, dy, radius * 8) : 16;
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
            if (stroke) coverage -= round_rect_coverage(inner, inner_radius, x, y);
            if (coverage == 16) set_pixel(pixels, stride, x, y, resolved);
            else if (coverage > 0) blend_pixel(pixels, stride, x, y, resolved, (uint8_t)((coverage * 255 + 8) / 16));
        }
    }
}

static void fill_round_rect(uint32_t *pixels, uint32_t stride, UiRect rect, int radius, uint32_t color)
{
    paint_round_rect(pixels, stride, rect, radius, 0, color);
}

static void draw_rect_outline(uint32_t *pixels, uint32_t stride, UiRect rect, int radius, int width, uint32_t color)
{
    /* Subtract inner coverage, never repaint the contents under the stroke. */
    if (width > 0) paint_round_rect(pixels, stride, rect, radius, width, color);
}

static void draw_focus_ring(uint32_t *pixels, uint32_t stride, UiRect rect, int radius)
{
    /* A one-pixel gap keeps focus distinct even on an accent-filled button. */
    draw_rect_outline(pixels, stride,
        (UiRect){rect.x - 4, rect.y - 4, rect.width + 8, rect.height + 8}, radius + 4, 3, UI_FOCUS);
}

static void draw_inner_top_highlight(uint32_t *pixels, uint32_t stride, UiRect rect, int radius)
{
    if (rect.width <= radius * 2 || rect.height <= 4) return;
    int y = rect.y + 1;
    if (y < 0 || y >= SCREEN_HEIGHT) return;
    int x_start = rect.x + radius;
    int x_end = rect.x + rect.width - radius;
    if (x_start < 0) x_start = 0;
    if (x_end > SCREEN_WIDTH) x_end = SCREEN_WIDTH;
    uint8_t alpha = (g_theme.resolved == PTC_UI_RESOLVED_DARK) ? 40 : 80;
    uint32_t white = RGBA8_MAXALPHA(255, 255, 255);
    for (int x = x_start; x < x_end; ++x) {
        blend_pixel(pixels, stride, x, y, white, alpha);
    }
}

static void draw_drop_shadow(uint32_t *pixels, uint32_t stride, UiRect rect, int radius, int depth)
{
    (void)radius;
    if (depth <= 0) return;
    uint32_t black = RGBA8_MAXALPHA(0, 0, 0);

    /* 绘制多层递减透明度的外展阴影带 */
    for (int d = 1; d <= depth; ++d) {
        uint8_t alpha = (uint8_t)(32 - d * 4);
        int sx = rect.x - d;
        int sy = rect.y + d;
        int sw = rect.width + d * 2;
        int sh = rect.height + d;

        int x_start = sx < 0 ? 0 : sx;
        int y_start = sy < 0 ? 0 : sy;
        int x_end = sx + sw > SCREEN_WIDTH ? SCREEN_WIDTH : sx + sw;
        int y_end = sy + sh > SCREEN_HEIGHT ? SCREEN_HEIGHT : sy + sh;

        for (int x = x_start; x < x_end; ++x) {
            int y = rect.y + rect.height + d - 1;
            if (y >= 0 && y < SCREEN_HEIGHT) {
                blend_pixel(pixels, stride, x, y, black, alpha);
            }
        }
        for (int y = y_start; y < y_end; ++y) {
            if (sx >= 0 && sx < SCREEN_WIDTH) {
                blend_pixel(pixels, stride, sx, y, black, alpha);
            }
            if (sx + sw - 1 >= 0 && sx + sw - 1 < SCREEN_WIDTH) {
                blend_pixel(pixels, stride, sx + sw - 1, y, black, alpha);
            }
        }
    }
}

static void draw_circle_outline(
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
            if (inner_radius > 0) coverage -= disk_coverage(dx, dy, inner_radius * 8);
            if (coverage > 0) blend_pixel(pixels, stride, center_x + x, center_y + y,
                resolved, (uint8_t)((coverage * 255 + 8) / 16));
        }
    }
}

static void draw_line(
    uint32_t *pixels,
    uint32_t stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int width,
    uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    uint32_t resolved = resolve_color(color);
    for (;;) {
        fill_rect_packed(pixels, stride, (UiRect){x0 - width / 2, y0 - width / 2, width, width}, resolved);
        if (x0 == x1 && y0 == y1) break;
        {
            int twice = error * 2;
            if (twice >= dy) { error += dy; x0 += sx; }
            if (twice <= dx) { error += dx; y0 += sy; }
        }
    }
}

static void draw_status_symbol(
    uint32_t *pixels,
    uint32_t stride,
    int x,
    int y,
    uint32_t color,
    int kind)
{
    draw_rect_outline(pixels, stride, (UiRect){x, y, 20, 20}, 4, 2, color);
    if (kind == 1) {
        draw_line(pixels, stride, x + 4, y + 11, x + 8, y + 15, 2, color);
        draw_line(pixels, stride, x + 8, y + 15, x + 16, y + 5, 2, color);
    } else if (kind == 2) {
        fill_rect(pixels, stride, (UiRect){x + 8, y + 4, 4, 9}, color);
        fill_rect(pixels, stride, (UiRect){x + 8, y + 15, 4, 3}, color);
    } else if (kind == 3) {
        draw_line(pixels, stride, x + 5, y + 5, x + 15, y + 15, 2, color);
        draw_line(pixels, stride, x + 15, y + 5, x + 5, y + 15, 2, color);
    } else {
        fill_rect(pixels, stride, (UiRect){x + 8, y + 4, 4, 3}, color);
        fill_rect(pixels, stride, (UiRect){x + 8, y + 9, 4, 8}, color);
    }
}

static bool set_font_size(int size)
{
    return g_ui.font_ready && FT_Set_Pixel_Sizes(g_ui.face, 0, (FT_UInt)size) == 0;
}

static int measure_text(const char *text, int size)
{
    int width = 0;
    const char *cursor = text;
    if (!text || !set_font_size(size)) {
        return 0;
    }
    while (*cursor) {
        uint32_t codepoint = ui_decode_utf8(&cursor);
        if (FT_Load_Char(g_ui.face, codepoint, FT_LOAD_DEFAULT) == 0) {
            width += (int)(g_ui.face->glyph->advance.x >> 6);
        }
    }
    return width;
}

static void draw_text(uint32_t *pixels, uint32_t stride, int x, int baseline, const char *text, int size, uint32_t color)
{
    const char *cursor = text;
    int pen_x = x;
    uint32_t resolved = resolve_color(color);
    if (!text || !set_font_size(size)) {
        return;
    }
    while (*cursor) {
        uint32_t codepoint = ui_decode_utf8(&cursor);
        FT_GlyphSlot glyph;
        int row;
        if (FT_Load_Char(g_ui.face, codepoint, FT_LOAD_RENDER) != 0) {
            continue;
        }
        glyph = g_ui.face->glyph;
        for (row = 0; row < (int)glyph->bitmap.rows; ++row) {
            int column;
            for (column = 0; column < (int)glyph->bitmap.width; ++column) {
                uint8_t alpha = glyph->bitmap.buffer[row * glyph->bitmap.pitch + column];
                blend_pixel(
                    pixels,
                    stride,
                    pen_x + glyph->bitmap_left + column,
                    baseline - glyph->bitmap_top + row,
                    resolved,
                    alpha);
            }
        }
        pen_x += (int)(glyph->advance.x >> 6);
    }
}

static void draw_text_bold(uint32_t *pixels, uint32_t stride, int x, int baseline, const char *text, int size, uint32_t color)
{
    draw_text(pixels, stride, x, baseline, text, size, color);
    draw_text(pixels, stride, x + 1, baseline, text, size, color);
}

static void draw_text_center(uint32_t *pixels, uint32_t stride, UiRect rect, const char *text, int size, uint32_t color)
{
    int width = measure_text(text, size);
    int baseline = rect.y + (rect.height + size) / 2 - 3;
    draw_text(pixels, stride, rect.x + (rect.width - width) / 2, baseline, text, size, color);
}

static void fit_text(char *out, size_t out_size, const char *text, int size, int max_width)
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
    if (!set_font_size(size)) {
        return;
    }
    while (*cursor) {
        const char *next = cursor;
        uint32_t codepoint = ui_decode_utf8(&next);
        int advance = 0;
        if (FT_Load_Char(g_ui.face, codepoint, FT_LOAD_DEFAULT) == 0) {
            advance = (int)(g_ui.face->glyph->advance.x >> 6);
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

static void draw_header(uint32_t *pixels, uint32_t stride, const char *title, const char *subtitle)
{
    fill_round_rect(pixels, stride, (UiRect){54, 25, 48, 48}, 16, UI_ACCENT);
    fill_round_rect(pixels, stride, (UiRect){87, 24, 12, 12}, 6, UI_CORAL);
    draw_line(pixels, stride, 65, 49, 79, 49, 3, UI_ON_ACCENT);
    draw_line(pixels, stride, 72, 42, 72, 56, 3, UI_ON_ACCENT);
    draw_circle_outline(pixels, stride, 90, 44, 3, 3, UI_ON_ACCENT);
    draw_circle_outline(pixels, stride, 86, 55, 3, 3, UI_ON_ACCENT);
    draw_text_bold(pixels, stride, 124, 49, title, 30, UI_INK);
    draw_text(pixels, stride, 124, 77, subtitle, 18, UI_MUTED);
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
        if (g_theme.resolved == PTC_UI_RESOLVED_DARK) {
            bg_col = 0x0124354D;
            border_col = 0x013D506E;
            fg_col = UI_RGB(0xF3F6FD);
        } else {
            bg_col = 0x01DCE3ED;
            border_col = 0x01B8C5D6;
            fg_col = UI_RGB(0x172640);
        }
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

static void draw_shoulder_key_glyph(uint32_t *pixels, uint32_t stride, int x, int y, int width, int height, const char *key_str, bool disabled)
{
    uint32_t bg_col;
    uint32_t border_col;
    uint32_t fg_col;

    if (disabled) {
        bg_col = UI_RAISED;
        border_col = UI_BORDER;
        fg_col = UI_DISABLED;
    } else {
        if (g_theme.resolved == PTC_UI_RESOLVED_DARK) {
            bg_col = 0x0124354D;
            border_col = 0x013D506E;
            fg_col = UI_RGB(0xF3F6FD);
        } else {
            bg_col = 0x01DCE3ED;
            border_col = 0x01B8C5D6;
            fg_col = UI_RGB(0x172640);
        }
    }
    fill_round_rect(pixels, stride, (UiRect){x, y, width, height}, 5, bg_col);
    draw_rect_outline(pixels, stride, (UiRect){x, y, width, height}, 5, 1, border_col);
    draw_text_center(pixels, stride, (UiRect){x, y, width, height}, key_str, 13, fg_col);
}

static void draw_button_label(uint32_t *pixels, uint32_t stride, UiRect box, const char *label, int size, uint32_t color)
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

static void draw_footer_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect, const char *label)
{
    draw_button_label(pixels, stride, to_uirect(rect), label, 18, UI_MUTED);
}

static bool parent_status_is_exception(const PtcUiModel *model)
{
    return strcmp(model->setup_phase, "protection") == 0 ||
        strcmp(model->setup_phase, "failed") == 0 || model->recovery_active ||
        model->disable_flag_present ||
        (model->temporary_unlocked_available && model->temporary_unlocked) ||
        !ptc_ui_status_is_fresh(model, (int64_t)time(NULL));
}

static void draw_parent_status_footer(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect box = to_uirect(ptc_ui_parent_footer_rect(4));
    char summary[160];
    char fitted[160];
    uint32_t color = parent_status_is_exception(model) ? UI_DANGER : UI_ACCENT;
    ptc_ui_format_parent_status_summary(model, (int64_t)time(NULL), summary, sizeof(summary));
    fill_round_rect(pixels, stride, box, 12, UI_RGB(g_palette->surface));
    if (model->parent_footer_focused && model->parent_footer_selection == 1) {
        fill_round_rect(pixels, stride, box, 12, UI_RGB(g_palette->focus));
        fill_round_rect(pixels, stride, (UiRect){box.x + 3, box.y + 3, box.width - 6, box.height - 6},
            9, UI_RGB(g_palette->surface_raised));
    }
    fit_text(fitted, sizeof(fitted), summary, 18, box.width - 32);
    draw_text_center(pixels, stride, box, fitted, 18, color);
}

static UiRect to_uirect(PtcUiRect rect)
{
    UiRect out = {rect.x, rect.y, rect.w, rect.h};
    return out;
}

static void draw_dialog_button(
    uint32_t *pixels,
    uint32_t stride,
    PtcUiRect rect,
    const char *label,
    uint32_t background,
    uint32_t foreground,
    bool outline)
{
    UiRect box = to_uirect(rect);
    fill_round_rect(pixels, stride, box, 12, outline ? UI_RGB(g_palette->surface_raised) : background);
    draw_button_label(pixels, stride, box, label, 21, foreground);
}

static int draw_wrapped_text(
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
            if (set_font_size(size) && FT_Load_Char(g_ui.face, codepoint, FT_LOAD_DEFAULT) == 0) {
                advance = (int)(g_ui.face->glyph->advance.x >> 6);
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

static void draw_candidate_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect,
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

static void draw_overlay_actions(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, const char *confirm_label)
{
    PtcUiRect confirm = ptc_ui_confirm_rect(model->overlay);
    draw_dialog_button(pixels, stride, confirm, confirm_label, UI_ACCENT, UI_ON_ACCENT, false);
    if (model->confirm_hold_required && model->overlay == PTC_UI_OVERLAY_CONFIRM && model->confirm_hold_progress > 0) {
        UiRect progress = to_uirect(confirm);
        progress.width = progress.width * model->confirm_hold_progress / 1000;
        fill_round_rect(pixels, stride, progress, 12, UI_SUCCESS);
        draw_text_center(pixels, stride, to_uirect(confirm),
                         model->confirm_hold_progress >= 1000 ? "确认完成" : "继续按住...",
                         20, UI_ON_ACCENT);
        draw_rect_outline(pixels, stride, to_uirect(confirm), 12, 2, UI_SUCCESS);
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

static void format_event_time(int64_t timestamp, bool full, char *out, size_t out_size)
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
        time_t now_time = time(NULL);
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

static void draw_disable_banner(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect banner = {790, 42, 436, 38};
    if (!model->disable_flag_present) return;
    fill_round_rect(pixels, stride, banner, 6, UI_DANGER_SOFT);
    draw_rect_outline(pixels, stride, banner, 6, 1, UI_DANGER);
    draw_status_symbol(pixels, stride, banner.x + 8, banner.y + 5, UI_DANGER, 3);
    draw_text_center(pixels, stride, banner, "紧急停用已开启  |  新的时间控制不会应用", 18, UI_DANGER);
}

static void draw_notice(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, int y, int height)
{
    bool error = strcmp(model->result_status, "error") == 0;
    bool support = model->view == PTC_UI_PARENT && model->parent_page == PTC_UI_PARENT_SETTINGS &&
        model->settings_page == PTC_UI_SETTINGS_SUPPORT;
    bool expanded = error || model->waiting || model->feedback_detail[0] || support ||
        measure_text(model->message, 20) > 1094;
    uint32_t accent = error ? UI_DANGER : (model->waiting ? UI_WARNING : UI_SUCCESS);
    UiRect box = {54, y, 1172, height};
    fill_round_rect(pixels, stride, box, 16, error ? UI_DANGER_SOFT : (model->waiting ? UI_WARNING_SOFT : UI_SURFACE));
    if (expanded) {
        PtcUiRect status_icon = ptc_ui_notice_status_icon_rect(y);
        int baseline = y + 30;
        draw_status_symbol(pixels, stride, status_icon.x, status_icon.y, accent, error ? 3 : (model->waiting ? 2 : 1));
        baseline = draw_wrapped_text(pixels, stride, box.x + 54, baseline,
            model->message[0] ? model->message : "状态会在后台自动同步", 20, box.width - 78, 26,
            2, UI_INK);
        if (model->feedback_detail[0])
            baseline = draw_wrapped_text(pixels, stride, box.x + 54, baseline + 2,
                model->feedback_detail, 18, box.width - 78, 24, (y + height - baseline - 4) / 24, UI_MUTED);
        if (support && baseline + 24 < y + height) {
            char execution[160];
            PtcUiRect command = ptc_ui_notice_command_text_rect(y, height);
            snprintf(execution, sizeof(execution), "命令：%s    %s", model->command_name, model->transport_label);
            if (baseline <= command.y)
                draw_wrapped_text(pixels, stride, command.x, command.y + 18, execution, 18, command.w, 24, 1, UI_MUTED);
        }
    } else {
        int icon_y = y + (height - 24) / 2;
        int baseline = y + (height + 20) / 2 - 3;
        draw_status_symbol(pixels, stride, box.x + 18, icon_y, accent, 1);
        draw_text(pixels, stride, box.x + 54, baseline,
            model->message[0] ? model->message : "状态会在后台自动同步", 20, UI_MUTED);
    }
}

static void home_button(uint32_t *pixels, uint32_t stride, PtcUiRect target,
    const char *label, bool primary, bool selected, bool disabled)
{
    UiRect box = to_uirect(target);
    uint32_t fill = disabled ? UI_RAISED : (primary ? UI_ACCENT : UI_ACCENT_SOFT);
    fill_round_rect(pixels, stride, box, 12, fill);
    if (selected) draw_focus_ring(pixels, stride, box, 12);
    draw_button_label(pixels, stride, box, label, target.h <= 48 ? 18 : 22,
        disabled ? UI_DISABLED : (primary ? UI_ON_ACCENT : UI_ACCENT));
}

static const char *home_runtime_notice(const PtcUiModel *model)
{
    if (model->disable_flag_present) return "控制已停用，请家长到支持与恢复处理";
    if (model->recovery_active) return "正在恢复设置，请等待恢复完成";
    if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0)
        return "需要家长处理，请进入支持与恢复";
    if (model->restriction_enabled_available && !model->restriction_enabled)
        return "Nintendo 家长控制未启用，请家长检查系统设置";
    if (model->temporary_unlocked_available && model->temporary_unlocked)
        return "临时解除期间不计时，进入睡眠后恢复今日限制";
    if (model->apply_pending_confirmation) return "设置等待确认生效，请稍候";
    if (model->restricted_now == 1) return "已进入时间限制，可兑换加时码或请家长调整额度";
    if (model->remaining_available && model->remaining_minutes == 0 && model->unrestricted_today != 1)
        return "额度已用完，限制可能即将生效，可兑换加时码";
    return "";
}

static void draw_home_notice(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    bool expanded = ptc_ui_home_notice_expanded(model) || model->waiting || measure_text(model->message, 20) > 1100;
    bool error = strcmp(model->result_status, "error") == 0;
    const char *runtime = home_runtime_notice(model);
    const char *title = runtime[0] ? runtime : (error ? "操作未完成" : (model->waiting ? "正在同步" : ""));
    UiRect box = {48, 520, 1184, 128};
    uint32_t accent = error || model->disable_flag_present ? UI_DANGER : UI_WARNING;
    fill_round_rect(pixels, stride, box, 16, expanded ? (error || model->disable_flag_present ? UI_DANGER_SOFT : UI_WARNING_SOFT) : UI_SURFACE);
    if (expanded) {
        int y = 548;
        draw_status_symbol(pixels, stride, 68, 535, accent, error ? 3 : (model->waiting ? 2 : 1));
        draw_text(pixels, stride, 100, y, title, 20, accent);
        y += 26;
        y = draw_wrapped_text(pixels, stride, 100, y, model->message, 18, 1100, 23, 2, UI_INK);
        if (model->feedback_detail[0])
            draw_wrapped_text(pixels, stride, 100, y, model->feedback_detail, 18, 1100, 23,
                (644 - y) / 23 + 1, UI_MUTED);
    } else {
        int icon_y = 520 + (128 - 24) / 2;
        int baseline = 520 + (128 + 20) / 2 - 3;
        draw_status_symbol(pixels, stride, 68, icon_y, UI_SUCCESS, 1);
        draw_text(pixels, stride, 100, baseline,
            model->message[0] ? model->message : "状态会在后台自动同步", 20, UI_MUTED);
    }
}

static void draw_home_summary(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, bool parent)
{
    UiRect box = to_uirect(ptc_ui_home_summary_rect(parent));
    char remaining[64], today[64], line[192], age[64];
    int x = box.x + 28;
    ptc_ui_format_home_remaining(model, (int64_t)time(NULL), remaining, sizeof(remaining));
    ptc_ui_format_today_mode(model, today, sizeof(today));
    fill_round_rect(pixels, stride, box, 16, UI_RGB(g_palette->hero));
    draw_inner_top_highlight(pixels, stride, box, 16);
    draw_text_bold(pixels, stride, x, box.y + 42, "今天还可玩", 22, UI_RGB(g_palette->hero_secondary));
    draw_circle_outline(pixels, stride, box.x + box.width - 46, box.y + 40, 16, 2, UI_RGB(g_palette->hero_secondary));
    draw_line(pixels, stride, box.x + box.width - 46, box.y + 29, box.x + box.width - 46, box.y + 40, 2, UI_RGB(g_palette->hero_secondary));
    draw_line(pixels, stride, box.x + box.width - 46, box.y + 40, box.x + box.width - 38, box.y + 44, 2, UI_RGB(g_palette->hero_secondary));
    /* Split only the existing numeric formatter output. Unknown/stale states
     * retain their words and are never converted to a numeric zero. */
    char *unit = strstr(remaining, " 分钟");
    if (unit) {
        *unit = '\0';
        int minutes = atoi(remaining);
        int num_w = measure_text(remaining, 80);
        draw_text_bold(pixels, stride, x, box.y + 133, remaining, 80, UI_RGB(g_palette->on_hero));
        int unit_x = x + num_w + 12;
        draw_text(pixels, stride, unit_x, box.y + 130, "分钟", 24, UI_RGB(g_palette->hero_secondary));
        if (minutes >= 60) {
            char duration_str[64];
            if (minutes % 60 == 0) {
                snprintf(duration_str, sizeof(duration_str), "（%d 小时）", minutes / 60);
            } else {
                snprintf(duration_str, sizeof(duration_str), "（%d 小时 %d 分钟）", minutes / 60, minutes % 60);
            }
            int dur_x = unit_x + measure_text("分钟", 24) + 12;
            draw_text(pixels, stride, dur_x, box.y + 130, duration_str, 20, UI_RGB(g_palette->hero_secondary));
        }
    } else {
        draw_wrapped_text(pixels, stride, x, box.y + 124, remaining, 40, box.width - 56, 48, 2, UI_RGB(g_palette->on_hero));
    }
    /* 今日额度胶囊进度槽 (Time Progress Gauge) */
    UiRect gauge_bg = {box.x + 28, box.y + 150, box.width - 56, 8};
    uint32_t slot_bg = (g_theme.resolved == PTC_UI_RESOLVED_DARK) ? 0x0119273D : 0x01D3DCED;
    fill_round_rect(pixels, stride, gauge_bg, 4, slot_bg);
    draw_rect_outline(pixels, stride, gauge_bg, 4, 1, 0x013D5375);

    uint32_t health_color = UI_SUCCESS;
    int remaining_mins = model->remaining_available ? model->remaining_minutes : -1;
    if (remaining_mins >= 0) {
        if (remaining_mins <= 10) health_color = UI_DANGER;
        else if (remaining_mins <= 30) health_color = UI_WARNING;
        else health_color = UI_SUCCESS;
    }

    if (model->unrestricted_today == 1) {
        fill_round_rect(pixels, stride, gauge_bg, 4, UI_SUCCESS);
    } else if (model->remaining_available && model->played_minutes_available &&
               (model->remaining_minutes + model->played_minutes > 0)) {
        int total_mins = model->remaining_minutes + model->played_minutes;
        int remain_w = (int)((int64_t)gauge_bg.width * model->remaining_minutes / total_mins);
        if (remain_w < 6 && model->remaining_minutes > 0) remain_w = 6;
        if (remain_w > gauge_bg.width) remain_w = gauge_bg.width;
        if (remain_w > 0) {
            fill_round_rect(pixels, stride, (UiRect){gauge_bg.x, gauge_bg.y, remain_w, gauge_bg.height}, 4, health_color);
        }
    } else if (model->remaining_available && model->remaining_minutes > 0) {
        int fill_w = (int)((int64_t)gauge_bg.width * (model->remaining_minutes > 120 ? 120 : model->remaining_minutes) / 120);
        if (fill_w < 6) fill_w = 6;
        fill_round_rect(pixels, stride, (UiRect){gauge_bg.x, gauge_bg.y, fill_w, gauge_bg.height}, 4, health_color);
    }

    snprintf(line, sizeof(line), "今日%s  /  %s", today,
        model->status_loaded ? rule_source_label(model->rule_source) : "待确认规则");
    draw_text(pixels, stride, x, box.y + 176, line, 18, UI_RGB(g_palette->hero_secondary));
    /* Supporting information sits on a separate surface, below the hero. */
    UiRect lower = {box.x + 12, box.y + 200, box.width - 24, box.height - 212};
    fill_round_rect(pixels, stride, lower, 16, UI_SURFACE);
    if (parent) {
        ptc_ui_format_home_total(model, line, sizeof(line));
        draw_text(pixels, stride, x, box.y + 228, line, 20, UI_INK);
        if (model->played_minutes_available && model->played_minutes >= 0)
            snprintf(line, sizeof(line), "额度消耗估算  约 %d 分钟", model->played_minutes);
        else snprintf(line, sizeof(line), "额度消耗估算  暂不可用");
        draw_text(pixels, stride, x, box.y + 259, line, 18, UI_MUTED);
    } else {
        draw_text(pixels, stride, x, box.y + 232, "明天的安排", 18, UI_MUTED);
        if (model->forecast_available) {
            const PtcResultForecastDay *day = &model->forecast[1];
            if (day->mode == PTC_RULE_MODE_UNLIMITED) snprintf(line, sizeof(line), "不限时");
            else snprintf(line, sizeof(line), "%u 分钟", (unsigned int)day->minutes);
            draw_text(pixels, stride, x, box.y + 270, line, 28, UI_INK);
            draw_text(pixels, stride, x + 200, box.y + 270,
                rule_source_label(day->rule_source), 18, UI_MUTED);
        } else draw_text(pixels, stride, x, box.y + 270, "安排暂不可用", 24, UI_MUTED);
    }
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, x, box.y + box.height - 27, age, 18, UI_MUTED);
}

static void draw_child(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char buffer[128], hint[160], fitted_hint[160];
    bool disabled = model->disable_flag_present || model->waiting;
    draw_header(pixels, stride, "今天的约定", "合理安排时间，完成今天的约定");
    draw_disable_banner(pixels, stride, model);
    draw_home_summary(pixels, stride, model, false);
    fill_round_rect(pixels, stride, (UiRect){704, 120, 528, 384}, 16, UI_RGB(g_palette->surface));
    draw_text(pixels, stride, 736, 164, "需要多一点时间？", 28, UI_RGB(g_palette->text_primary));
    draw_text(pixels, stride, 736, 195, "输入家长给你的 8 位数字", 18, UI_RGB(g_palette->text_secondary));
    home_button(pixels, stride, ptc_ui_child_submit_rect(),
        model->disable_flag_present ? "兑换暂不可用" : "A  输入加时码", true, false, disabled);
    if (model->daily_buffer_available)
        snprintf(buffer, sizeof(buffer), "X  领取自主缓冲  +%u 分钟", (unsigned int)model->daily_buffer_minutes);
    else snprintf(buffer, sizeof(buffer), "%s", model->daily_buffer_claimed ? "今日已使用缓冲" :
        (model->daily_buffer_minutes == 0 ? "今日自主缓冲未开启" : "自主缓冲仅可在限时日领取"));
    home_button(pixels, stride, ptc_ui_child_buffer_rect(), buffer, false, false,
        disabled || !model->daily_buffer_available);
    home_button(pixels, stride, ptc_ui_home_details_rect(false), "+  使用详情", false, false, model->waiting);
    draw_home_notice(pixels, stride, model);
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_footer_rect(0)), "A  输入加时码", 18, disabled ? UI_DISABLED : UI_MUTED);
    if (model->show_parent_shortcut_hint && model->custom_shortcut_enabled)
        ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label, hint, sizeof(hint));
    else snprintf(hint, sizeof(hint), "状态会在后台自动同步");
    fit_text(fitted_hint, sizeof(fitted_hint), hint, 18, ptc_ui_child_footer_rect(1).w - 24);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), fitted_hint, 18, UI_RGB(g_palette->text_secondary));
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), "B  退出");
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_refresh_rect()), "Y  刷新", 18, model->waiting ? UI_DISABLED : UI_MUTED);
}

static void draw_setup(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {54, 120, 1172, 500};
    const char *phase = model->setup_phase[0] ? model->setup_phase : "pending";
    int64_t grace_remaining = ptc_ui_setup_grace_remaining(model, (int64_t)time(NULL));
    char title[64];
    char phase_line[192];
    char countdown_line[80];
    char fitted[220];
    int step = model->setup_step > 0 ? model->setup_step : PTC_UI_SETUP_SHORTCUT;
    snprintf(title, sizeof(title), "首次设置  |  %d/5", step);
    draw_header(pixels, stride, grace_remaining >= 0 ? "正在同步" : title,
        grace_remaining >= 0 ? "系统设置正在同步，完成后继续选择进入的区域" : "按步骤完成 任我玩 的家长设置");
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    for (int i = 0; i < 5; ++i)
        fill_round_rect(pixels, stride, (UiRect){104 + i * 212, 167, 188, 5}, 2,
            i < step ? UI_ACCENT : UI_RAISED);
    if (grace_remaining >= 0) {
        draw_text(pixels, stride, 204, 190, "环境检查已通过", 31, UI_SUCCESS);
        snprintf(phase_line, sizeof(phase_line), "当前状态：正在同步    安装前快照：%s",
                 model->setup_snapshot_available ? "已保存" : "不可用");
        draw_text(pixels, stride, 204, 248, phase_line, 22, UI_MUTED);
        if (grace_remaining > 0) {
            snprintf(countdown_line, sizeof(countdown_line), "系统设置同步中（约 %lld 秒）...", (long long)grace_remaining);
        } else {
            snprintf(countdown_line, sizeof(countdown_line), "同步完成，正在启用额度管理...");
        }
        draw_text(pixels, stride, 204, 310, countdown_line, 34, UI_ACCENT);
        draw_text(pixels, stride, 204, 356, "无需操作；同步完成后会进入第 5 步选择区域。", 22, UI_INK);
    } else {
        draw_text(pixels, stride, 104, 154, "1 快捷键", 16, step == PTC_UI_SETUP_SHORTCUT ? UI_ACCENT : UI_MUTED);
        draw_text(pixels, stride, 286, 154, "2 PIN", 16, step == PTC_UI_SETUP_PIN ? UI_ACCENT : UI_MUTED);
        draw_text(pixels, stride, 452, 154, "3 外观主题", 16, step == PTC_UI_SETUP_THEME ? UI_ACCENT : UI_MUTED);
        draw_text(pixels, stride, 708, 154, "4 启用管理", 16, step == PTC_UI_SETUP_TAKEOVER ? UI_ACCENT : UI_MUTED);
        draw_text(pixels, stride, 912, 154, "5 进入区域", 16, step == PTC_UI_SETUP_ZONE ? UI_ACCENT : UI_MUTED);
        if (step == PTC_UI_SETUP_SHORTCUT) {
            UiRect compact_fixed = {204, 184, 872, 54};
            fill_round_rect(pixels, stride, compact_fixed, 12, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, compact_fixed, 12, 2, UI_ACCENT);
            draw_text(pixels, stride, 232, 218, "固定入口 Minus：松开即可进入，无需长按", 20, UI_ACCENT);
            draw_text(pixels, stride, 204, 264, "自定义组合需长按约 400ms；按 A 加入草稿，按 + 确认后生效", 18, UI_MUTED);
            for (int index = 0; index < PTC_UI_SHORTCUT_PRESET_COUNT; ++index) {
                UiRect card = to_uirect(ptc_ui_setup_shortcut_card_rect(index));
                bool selected = index == model->setup_shortcut_index;
                fill_round_rect(pixels, stride, card, 16, selected ? UI_ACCENT_SOFT : UI_RAISED);
                draw_rect_outline(pixels, stride, card, 16, selected ? 2 : 1, selected ? UI_ACCENT : UI_CONTROL);
                draw_text_center(pixels, stride, card,
                                 ptc_ui_shortcut_common_label(index), 16,
                                 selected ? UI_ACCENT : UI_INK);
            }
            draw_text(pixels, stride, 204, 554,
                      model->shortcut_draft_enabled ? "待确认自定义组合（需长按）：" : "待确认状态：仅保留 Minus（松开进入）",
                      17, UI_MUTED);
            if (model->shortcut_draft_enabled) {
                fit_text(fitted, sizeof(fitted), model->shortcut_draft_label, 18, 250);
                draw_text(pixels, stride, 420, 554, fitted, 18, UI_ACCENT);
            }
        } else if (step == PTC_UI_SETUP_PIN) {
            draw_text(pixels, stride, 204, 220, "任我玩 PIN 已设置", 30, UI_INK);
            draw_text(pixels, stride, 204, 262, "全新安装默认 PIN：110", 24, UI_WARNING);
            draw_text(pixels, stride, 204, 294, "默认值属于弱保护；可继续使用，也可现在修改为 1到64 位数字。", 20, UI_MUTED);
            draw_dialog_button(pixels, stride, ptc_ui_setup_pin_rect(), "X / 点击  修改 PIN",
                               UI_ACCENT, UI_ON_ACCENT, false);
            draw_text(pixels, stride, 204, 420, "此 PIN 用于进入家长区，与 Nintendo 官方家长控制 PIN 不同。", 19, UI_MUTED);
        } else if (step == PTC_UI_SETUP_TAKEOVER) {
            bool resuming_restored_setup = model->disable_flag_present && strcmp(phase, "restored") == 0;
            bool reconfirming_environment = ptc_ui_runtime_fingerprint_reconfirmation_needed(model);
            bool takeover_complete = ptc_ui_setup_takeover_complete(model);
            draw_text(pixels, stride, 204, 218,
                      takeover_complete ? "系统控制接管已完成" :
                      (reconfirming_environment ? "系统环境已变化" :
                       (resuming_restored_setup ? "解除停用并重新接管" : "确认接管系统控制")),
                      30, takeover_complete ? UI_SUCCESS : UI_INK);
            snprintf(phase_line, sizeof(phase_line), "当前状态：%s    安装前快照：%s",
                     takeover_complete ? (strcmp(phase, "active") == 0 ? "正常运行" : "正在同步") :
                     (strcmp(phase, "protection") == 0 ? "保护模式" :
                     (strcmp(phase, "failed") == 0 ? "检查失败" :
                      (resuming_restored_setup ? "已恢复并停用" : "等待家长确认"))),
                     model->setup_snapshot_available ? "已保存" : "待保存");
            draw_text(pixels, stride, 204, 266, phase_line, 21, UI_MUTED);
            draw_text(pixels, stride, 204, 324,
                      takeover_complete
                          ? "游玩时间管理已启用，可以继续选择进入的区域。"
                          : reconfirming_environment
                           ? "系统版本或运行环境与上次确认时不同，需要家长重新确认兼容性。"
                          : resuming_restored_setup
                           ? "确认后先检查主机；通过后恢复游玩时间管理。"
                           : "确认后先检查主机是否支持，再启用游玩时间管理。",
                      21, UI_INK);
            draw_text(pixels, stride, 204, 360,
                      takeover_complete
                          ? "按 A 或点击继续，进入第 5 步选择区域。"
                          : reconfirming_environment
                           ? "检查通过后保留现有计划，恢复游玩时间管理。"
                           : resuming_restored_setup
                            ? "保留现有计划；需要撤销时可到“支持与恢复”操作。"
                            : "首次接管会原样保留今天的总额度和剩余时间，不会先临时解限。",
                      21, UI_INK);
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(),
                               takeover_complete ? "A / 点击  继续到第 5 步" :
                               (reconfirming_environment ? "A / 点击  重新检测并接管" :
                                (resuming_restored_setup ? "A / 点击  解除停用并重新接管" : "A / 点击  确认接管")),
                               takeover_complete ? UI_SUCCESS : UI_ACCENT,
                               UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_THEME) {
            static const char *LABELS[] = {"跟随系统", "浅色", "暗色"};
            static const char *DETAILS[] = {"随 Switch 设置", "经典浅色外观", "柔和的深色背景"};
            draw_text(pixels, stride, 204, 220, "选择外观主题", 30, UI_INK);
            draw_text(pixels, stride, 204, 252, "默认跟随系统；只改变主机应用外观，不影响计时和后台控制。", 18, UI_MUTED);
            for (int index = 0; index < 3; ++index) {
                UiRect option = to_uirect(ptc_ui_setup_theme_rect(index));
                bool selected = index == model->setup_theme_index;
                fill_round_rect(pixels, stride, option, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
                draw_rect_outline(pixels, stride, option, 12, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
                draw_text_center(pixels, stride, (UiRect){option.x, option.y + 22, option.width, 36},
                                 LABELS[index], 23, UI_INK);
                draw_text_center(pixels, stride, (UiRect){option.x, option.y + 70, option.width, 28},
                                 DETAILS[index], 16, UI_MUTED);
            }
            draw_text(pixels, stride, 204, 448, "左右选择  |  A / + 保存并继续", 18, UI_ACCENT);
        } else {
            draw_text(pixels, stride, 204, 214, "初始化完成，选择进入区域", 30, UI_INK);
            draw_text(pixels, stride, 204, 254, "之后可在两个区域之间切换；进入家长区会受 PIN 保护。", 21, UI_MUTED);
            for (int index = 0; index < 2; ++index) {
                UiRect card = to_uirect(ptc_ui_setup_zone_rect(index));
                bool selected = index == model->setup_zone_index;
                fill_round_rect(pixels, stride, card, 16, selected ? UI_ACCENT_SOFT : UI_RAISED);
                draw_rect_outline(pixels, stride, card, 16, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 26, card.width, 34},
                                 index == 0 ? "孩子区" : "家长区", 27,
                                 selected ? UI_ACCENT : UI_INK);
                if (index == 0) {
                    char shortcut_hint[160];
                    char fitted_shortcut_hint[160];
                    ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label,
                                                       shortcut_hint, sizeof(shortcut_hint));
                    fit_text(fitted_shortcut_hint, sizeof(fitted_shortcut_hint), shortcut_hint, 17, card.width - 36);
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 86, card.width - 36, 26},
                                     model->show_parent_shortcut_hint && model->custom_shortcut_enabled
                                        ? fitted_shortcut_hint : "家长区快捷提示未显示", 17, UI_MUTED);
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     "家长区需要输入 任我玩 PIN", 17, UI_MUTED);
                } else {
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 86, card.width - 36, 26},
                                     "固定 Minus：松开进入，无需长按", 18, UI_MUTED);
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     model->custom_shortcut_enabled ? "自定义组合：长按约 400ms" : "未启用自定义组合", 17, UI_MUTED);
                }
            }
        }
        if (model->message[0] && step != PTC_UI_SETUP_SHORTCUT) {
            fit_text(fitted, sizeof(fitted), model->message, 18, 1160);
            draw_text(pixels, stride, 64, 530, fitted, 18, UI_MUTED);
        }
        if (model->feedback_detail[0]) {
            fit_text(fitted, sizeof(fitted), model->feedback_detail, 17, 1160);
            draw_text(pixels, stride, 64, 552, fitted, 17, UI_DANGER);
        }
    }
    if (grace_remaining < 0) {
        draw_dialog_button(pixels, stride, ptc_ui_setup_back_rect(), "B  返回上一步",
                           UI_RAISED, UI_INK, true);
        if (step == PTC_UI_SETUP_SHORTCUT) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), "+  确认快捷键并继续",
                               UI_ACCENT, UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_ZONE) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(),
                               model->setup_zone_index == 1 ? "A  确认进入家长区" : "A  确认进入孩子区",
                               UI_ACCENT, UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_PIN) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), "A  继续使用当前 PIN",
                               UI_ACCENT, UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_THEME) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), "A  保存主题并继续",
                               UI_ACCENT, UI_ON_ACCENT, false);
        }
    }
}

static void draw_error(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {214, 148, 852, 444};
    char fitted[192];
    char execution[150];
    draw_header(pixels, stride, "操作未完成", "请查看错误信息后重试或返回");
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    fill_round_rect(pixels, stride, (UiRect){254, 194, 64, 64}, 16, UI_DANGER);
    draw_text_center(pixels, stride, (UiRect){254, 194, 64, 64}, "!", 34, UI_ON_ACCENT);
    draw_text(pixels, stride, 342, 214, "加时码处理失败", 28, UI_INK);
    snprintf(
        execution,
        sizeof(execution),
        "命令：%s    %s",
        model->command_name[0] ? model->command_name : "未开始",
        model->transport_label[0] ? model->transport_label : "传输：未开始");
    fit_text(fitted, sizeof(fitted), execution, 18, 756);
    draw_text(pixels, stride, 254, 286, fitted, 18, UI_MUTED);
    fit_text(fitted, sizeof(fitted), model->message, 23, 756);
    draw_text(pixels, stride, 254, 342, fitted, 23, UI_MUTED);
    if (model->feedback_detail[0]) {
        fit_text(fitted, sizeof(fitted), model->feedback_detail, 17, 756);
        draw_text(pixels, stride, 254, 390, fitted, 17, UI_DANGER);
    }

    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_retry_rect()), 12, UI_ACCENT);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_retry_rect()),
                    model->error_code == 306 ? "A  重新检测" : "A  重新输入", 25, UI_ON_ACCENT);
    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 12, UI_RAISED);
    draw_rect_outline(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 12, 1, UI_CONTROL);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_back_rect()), "B  返回主页", 25, UI_INK);
}

static const UiAction *actions_for_page(PtcUiParentPage page, int *count)
{
    if (page == PTC_UI_PARENT_PLAN) {
        *count = 0;
        return NULL;
    }
    if (page == PTC_UI_PARENT_HOLIDAY) {
        *count = (int)(sizeof(HOLIDAY_ACTIONS) / sizeof(HOLIDAY_ACTIONS[0]));
        return HOLIDAY_ACTIONS;
    }
    if (page == PTC_UI_PARENT_GRANT) {
        *count = (int)(sizeof(GRANT_ACTIONS) / sizeof(GRANT_ACTIONS[0]));
        return GRANT_ACTIONS;
    }
    if (page == PTC_UI_PARENT_SETTINGS) {
        *count = (int)(sizeof(SETTINGS_ACTIONS) / sizeof(SETTINGS_ACTIONS[0]));
        return SETTINGS_ACTIONS;
    }
    *count = (int)(sizeof(TODAY_ACTIONS) / sizeof(TODAY_ACTIONS[0]));
    return TODAY_ACTIONS;
}

static void draw_tabs(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *LABELS[] = {"今日总额度", "周计划", "国家节假日", "加时码", "设置"};
    if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page != PTC_UI_SETTINGS_ROOT) {
        bool support = model->settings_page == PTC_UI_SETTINGS_SUPPORT;
        home_button(pixels, stride, support ? ptc_ui_support_back_rect() : ptc_ui_advanced_back_rect(),
                    "B  返回设置", false, false, false);
        draw_text(pixels, stride, 278, 140, support ? "设置 / 支持与恢复" : "设置 / 高级设置", 22, UI_MUTED);
        return;
    }
    int index;
    for (index = 0; index < PTC_UI_PARENT_PAGE_COUNT; ++index) {
        UiRect tab = to_uirect(ptc_ui_parent_tab_rect(index));
        uint32_t background = index == (int)model->parent_page ? UI_ACCENT : UI_RAISED;
        uint32_t foreground = index == (int)model->parent_page ? UI_ON_ACCENT : UI_INK;
        fill_round_rect(pixels, stride, tab, 12, background);
        draw_text_center(pixels, stride, tab, LABELS[index], 18, foreground);
    }
    if (!(model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page != PTC_UI_SETTINGS_ROOT)) {
        draw_text(pixels, stride, 1038, 140, "L / R 切换", 19, UI_MUTED);
    }
}

static void draw_settings_badge(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *label = ptc_ui_settings_status_label(model);
    uint32_t color = label && strcmp(label, "需处理") == 0 ? UI_DANGER : UI_WARNING;
    UiRect badge;
    if (!label) return;
    badge = (UiRect){54 + PTC_UI_PARENT_SETTINGS * 174 + 96, 113, 56, 24};
    if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page != PTC_UI_SETTINGS_ROOT)
        badge = (UiRect){534, 115, 72, 32};
    fill_round_rect(pixels, stride, badge, 6, color);
    draw_text_center(pixels, stride, badge, label, 11, UI_ON_ACCENT);
}

static void draw_card_action_icon(uint32_t *pixels, uint32_t stride, int cx, int cy, const char *title, uint32_t color, bool disabled)
{
    (void)disabled;
    if (!title) return;

    /* 1. 时钟/时间类：包含 "额度", "加时", "时间", "缓冲" */
    if (strstr(title, "额度") || strstr(title, "加时") || strstr(title, "时间") || strstr(title, "缓冲")) {
        draw_circle_outline(pixels, stride, cx, cy, 10, 2, color);
        draw_line(pixels, stride, cx, cy, cx, cy - 5, 2, color);
        draw_line(pixels, stride, cx, cy, cx + 4, cy + 1, 2, color);
        return;
    }

    /* 2. 日历/休假类：包含 "日历", "节假日", "休假", "调休", "计划" */
    if (strstr(title, "日历") || strstr(title, "节假日") || strstr(title, "休假") ||
        strstr(title, "调休") || strstr(title, "计划")) {
        draw_rect_outline(pixels, stride, (UiRect){cx - 9, cy - 8, 18, 16}, 3, 2, color);
        draw_line(pixels, stride, cx - 9, cy - 2, cx + 9, cy - 2, 2, color);
        draw_line(pixels, stride, cx - 4, cy - 10, cx - 4, cy - 7, 2, color);
        draw_line(pixels, stride, cx + 4, cy - 10, cx + 4, cy - 7, 2, color);
        return;
    }

    /* 3. 钥匙/安全类：包含 "PIN", "密钥" */
    if (strstr(title, "PIN") || strstr(title, "密钥")) {
        draw_circle_outline(pixels, stride, cx - 4, cy - 4, 6, 2, color);
        draw_line(pixels, stride, cx, cy, cx + 7, cy + 7, 2, color);
        draw_line(pixels, stride, cx + 4, cy + 4, cx + 7, cy + 1, 2, color);
        return;
    }

    /* 4. 设备/二维码类：包含 "二维码", "手机", "电脑", "设备" */
    if (strstr(title, "二维码") || strstr(title, "手机") || strstr(title, "电脑") || strstr(title, "设备")) {
        draw_rect_outline(pixels, stride, (UiRect){cx - 8, cy - 9, 16, 18}, 3, 2, color);
        draw_line(pixels, stride, cx - 3, cy + 4, cx + 3, cy + 4, 2, color);
        fill_rect(pixels, stride, (UiRect){cx - 1, cy - 6, 2, 2}, color);
        return;
    }

    /* 5. 外观主题：包含 "主题", "外观" */
    if (strstr(title, "主题") || strstr(title, "外观")) {
        draw_circle_outline(pixels, stride, cx, cy, 9, 2, color);
        fill_rect(pixels, stride, (UiRect){cx - 4, cy - 4, 3, 3}, color);
        fill_rect(pixels, stride, (UiRect){cx + 1, cy - 3, 3, 3}, color);
        fill_rect(pixels, stride, (UiRect){cx - 2, cy + 2, 3, 3}, color);
        return;
    }

    /* 6. 快捷键/程序入口：包含 "快捷键", "自制程序" */
    if (strstr(title, "快捷键") || strstr(title, "自制程序")) {
        draw_rect_outline(pixels, stride, (UiRect){cx - 10, cy - 6, 20, 13}, 4, 2, color);
        draw_line(pixels, stride, cx - 5, cy, cx - 1, cy, 2, color);
        draw_line(pixels, stride, cx - 3, cy - 2, cx - 3, cy + 2, 2, color);
        fill_rect(pixels, stride, (UiRect){cx + 3, cy - 2, 2, 2}, color);
        fill_rect(pixels, stride, (UiRect){cx + 5, cy + 1, 2, 2}, color);
        return;
    }

    /* 7. 安全/恢复/接管：包含 "接管", "重试", "停用", "恢复", "诊断", "支持" */
    if (strstr(title, "接管") || strstr(title, "重试") || strstr(title, "停用") ||
        strstr(title, "恢复") || strstr(title, "诊断") || strstr(title, "支持")) {
        draw_line(pixels, stride, cx - 8, cy - 8, cx + 8, cy - 8, 2, color);
        draw_line(pixels, stride, cx - 8, cy - 8, cx - 8, cy, 2, color);
        draw_line(pixels, stride, cx - 8, cy, cx, cy + 9, 2, color);
        draw_line(pixels, stride, cx + 8, cy - 8, cx + 8, cy, 2, color);
        draw_line(pixels, stride, cx + 8, cy, cx, cy + 9, 2, color);
        draw_line(pixels, stride, cx - 3, cy, cx - 1, cy + 3, 2, color);
        draw_line(pixels, stride, cx - 1, cy + 3, cx + 4, cy - 3, 2, color);
        return;
    }

    /* 8. 默认：齿轮/设置/记录 */
    draw_circle_outline(pixels, stride, cx, cy, 7, 2, color);
    draw_line(pixels, stride, cx, cy - 9, cx, cy - 7, 2, color);
    draw_line(pixels, stride, cx, cy + 7, cx, cy + 9, 2, color);
    draw_line(pixels, stride, cx - 9, cy, cx - 7, cy, 2, color);
    draw_line(pixels, stride, cx + 7, cy, cx + 9, cy, 2, color);
}

static void draw_action_card(uint32_t *pixels, uint32_t stride, UiRect rect,
    const UiAction *action, bool selected, PtcUiActionState state, int reserved_right)
{
    bool disabled = state == PTC_UI_ACTION_DISABLED;
    bool recommended = state == PTC_UI_ACTION_RECOMMENDED;
    bool compact = rect.height < 90;
    int title_size = 22;
    int content_width = rect.width - 76;
    int title_width = content_width - reserved_right - (recommended ? 64 : 0);
    uint32_t background = disabled ? UI_RAISED : (selected ? UI_ACCENT_SOFT : UI_SURFACE);
    fill_round_rect(pixels, stride, rect, 16, background);
    draw_inner_top_highlight(pixels, stride, rect, 16);
    if (selected) {
        draw_focus_ring(pixels, stride, rect, 16);
    } else {
        draw_rect_outline(pixels, stride, rect, 16, 1, UI_BORDER);
    }
    /* 左侧精致微图标徽章 */
    int icon_cx = rect.x + 32;
    int icon_cy = rect.y + rect.height / 2;
    UiRect badge_rect = {icon_cx - 17, icon_cy - 17, 34, 34};
    uint32_t badge_bg = disabled ? UI_PAGE :
        (action->accent == UI_SUCCESS ? UI_SUCCESS_SOFT :
        (action->accent == UI_DANGER ? UI_DANGER_SOFT :
        (action->accent == UI_WARNING ? UI_WARNING_SOFT : UI_ACCENT_SOFT)));
    fill_round_rect(pixels, stride, badge_rect, 10, badge_bg);
    draw_rect_outline(pixels, stride, badge_rect, 10, 1, UI_BORDER);
    draw_card_action_icon(pixels, stride, icon_cx, icon_cy, action->title,
        disabled ? UI_DISABLED : action->accent, disabled);

    int title_lines = measure_text(action->title, title_size) > title_width ? 2 : 1;
    int baseline = draw_wrapped_text(pixels, stride, rect.x + 58, rect.y + (compact ? 28 : 32),
        action->title, title_size, title_width, 25, title_lines, disabled ? UI_DISABLED : UI_INK);
    draw_wrapped_text(pixels, stride, rect.x + 58, baseline + 3, action->subtitle, 18,
        content_width, 22, (rect.y + rect.height - baseline - 4) / 22 + 1,
        disabled ? UI_DISABLED : UI_MUTED);
    if (recommended && !disabled) {
        fill_round_rect(pixels, stride, (UiRect){rect.x + rect.width - 66, rect.y + 8, 56, 24}, 6, UI_SUCCESS);
        draw_text_center(pixels, stride, (UiRect){rect.x + rect.width - 66, rect.y + 8, 56, 24}, "建议", 16, UI_ON_ACCENT);
    }
}

static void draw_safety_status(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 324};
    char troubleshoot[128];
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    int recommended = ptc_ui_support_recommended_action(model);
    draw_text(pixels, stride, panel.x + 26, panel.y + 36, "当前问题", 23, UI_INK);
    draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 70, ptc_ui_support_problem(model),
                      19, panel.width - 52, 26, 2, UI_RGB(g_palette->text_primary));
    const char *next = recommended == 0 ? (model->disable_flag_present ? "建议：解除停用并重新接管" : "建议：重新检测并接管") :
                       recommended == 1 ? "建议：选择重试修复" :
                       recommended == 4 ? "建议：导出诊断包，保留问题记录" :
                       (model->waiting || model->apply_pending_confirmation ? "请等待结果，再刷新状态" : "无需恢复操作，可按 B 返回设置");
    draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 132, next, 17,
                      panel.width - 52, 24, 2, UI_RGB(g_palette->accent));
    char age[80];
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, panel.x + 26, panel.y + 192, age, 15, status_age_color(model));
    if (model->environment_available)
        snprintf(troubleshoot, sizeof(troubleshoot), "HOS %s  |  %s", model->environment_hos, model->environment_model);
    else snprintf(troubleshoot, sizeof(troubleshoot), "环境详情暂不可用，可导出诊断");
    fit_text(troubleshoot, sizeof(troubleshoot), troubleshoot, 15, panel.width - 52);
    draw_text(pixels, stride, panel.x + 26, panel.y + 218, troubleshoot, 15, UI_RGB(g_palette->text_secondary));
    draw_text(pixels, stride, panel.x + 26, panel.y + 246, "最近事件", 17, UI_MUTED);
    if (model->recent_event_count > 0) {
        for (int event_index = 0; event_index < model->recent_event_count; ++event_index) {
            char latest[192];
            char event_time[48];
            int source_index = model->recent_event_count - 1 - event_index;
            format_event_time(model->recent_event_timestamps[source_index], false, event_time, sizeof(event_time));
            snprintf(latest, sizeof(latest), "%s  |  %s", model->recent_events[source_index], event_time);
            fit_text(latest, sizeof(latest), latest, 13, panel.width - 52);
            if (event_index + 6 == model->selected_index && !model->parent_footer_focused)
                draw_rect_outline(pixels, stride, to_uirect(ptc_ui_support_event_rect(event_index)), 6, 2, UI_RGB(g_palette->focus));
            draw_text(pixels, stride, panel.x + 26, panel.y + 270 + event_index * 22,
                      latest, 13, event_index + 6 == model->selected_index ? UI_ACCENT : UI_MUTED);
        }
    } else {
        draw_text(pixels, stride, panel.x + 26, panel.y + 274,
                  model->recent_events_available ? "最近没有需要注意的事件" : "暂时无法读取最近事件，可刷新后重试",
                  14, model->recent_events_available ? UI_MUTED : UI_DANGER);
    }
}

static void draw_today_status(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    draw_home_summary(pixels, stride, model, true);
    for (int index = 0; index < 4; ++index) {
        UiRect box = to_uirect(ptc_ui_today_card_rect(index));
        bool focused = !model->parent_footer_focused && model->selected_index == index;
        bool disabled = model->disable_flag_present || model->waiting;
        uint32_t fill = disabled ? UI_RAISED : (index == 0 ? UI_ACCENT : (index == 1 ? UI_ACCENT_SOFT : UI_SURFACE));
        fill_round_rect(pixels, stride, box, 16, fill);
        if (focused) {
            draw_focus_ring(pixels, stride, box, 16);
        }
        draw_text(pixels, stride, box.x + 24, box.y + 40, TODAY_ACTIONS[index].title, 24,
            disabled ? UI_DISABLED : (index == 0 ? UI_ON_ACCENT : UI_INK));
        draw_wrapped_text(pixels, stride, box.x + 24, box.y + 73,
            TODAY_ACTIONS[index].subtitle, 18, box.width - 48, 23, 2,
            disabled ? UI_DISABLED : (index == 0 ? UI_ON_ACCENT : UI_MUTED));
    }
    home_button(pixels, stride, ptc_ui_home_details_rect(true), "+  查看详情", false, false, model->waiting);
}

static void draw_grant_help(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 274};
    (void)model;
    fill_round_rect(pixels, stride, panel, 16, UI_RGB(g_palette->surface));
    draw_text(pixels, stride, 866, 216, "把加时码告诉孩子", 24, UI_RGB(g_palette->text_primary));
    draw_text(pixels, stride, 866, 258, "1  选择要增加的时长", 20, UI_RGB(g_palette->text_primary));
    draw_text(pixels, stride, 866, 294, "2  验证 PIN，生成代码", 20, UI_RGB(g_palette->text_primary));
    draw_text(pixels, stride, 866, 330, "3  孩子输入代码，确认加时", 20, UI_RGB(g_palette->text_primary));
    draw_wrapped_text(pixels, stride, 866, 373, "当天有效，成功兑换后仅可使用一次。无需联网。", 18, 336, 25, 2, UI_RGB(g_palette->text_secondary));
}

static void format_duration(int minutes, char *out, size_t out_size)
{
    if (minutes < 0) {
        snprintf(out, out_size, "暂不可用");
        return;
    }
    if (minutes < 60) {
        snprintf(out, out_size, "%d 分钟", minutes);
    } else if (minutes % 60 == 0) {
        snprintf(out, out_size, "%d 小时", minutes / 60);
    } else {
        snprintf(out, out_size, "%d 小时 %d 分钟", minutes / 60, minutes % 60);
    }
}

static void draw_diagnostic_notice(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect rect = {54, 522, 1172, 128};
    uint32_t accent = model->diagnostic_status == PTC_UI_DIAGNOSTIC_SUCCESS
        ? UI_SUCCESS
        : (model->diagnostic_status == PTC_UI_DIAGNOSTIC_ERROR
            ? UI_DANGER : UI_WARNING);
    char line[320];
    fill_round_rect(pixels, stride, rect, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, rect, 16, 1, UI_BORDER);
    fill_round_rect(pixels, stride, (UiRect){rect.x + 12, rect.y + 20, 4, rect.height - 40}, 2, accent);
    if (model->diagnostic_status == PTC_UI_DIAGNOSTIC_EXPORTING) {
        draw_text(pixels, stride, rect.x + 24, rect.y + 36, "正在导出诊断包...", 21, accent);
        draw_text(pixels, stride, rect.x + 24, rect.y + 76,
                  "诊断包会排除密钥、PIN、离线码和完整 nonce。", 17, UI_MUTED);
        return;
    }
    if (model->diagnostic_status == PTC_UI_DIAGNOSTIC_ERROR) {
        draw_text(pixels, stride, rect.x + 24, rect.y + 36, "诊断包导出失败。", 21, accent);
        draw_text(pixels, stride, rect.x + 24, rect.y + 76,
                  "请确认 SD 卡可写后重试。", 17, UI_MUTED);
        return;
    }
    snprintf(line, sizeof(line), "诊断包导出成功：%s", model->diagnostic_path);
    draw_text(pixels, stride, rect.x + 24, rect.y + 32, line, 17, UI_INK);
    draw_text(pixels, stride, rect.x + 24, rect.y + 66,
              "如遇到问题，提交 GitHub Issue 时请附上此文件。", 17, UI_INK);
    draw_text(pixels, stride, rect.x + 24, rect.y + 100,
              "GitHub 地址：https://github.com/selfuppen/NX-PlayWise/issues", 17, accent);
}

static uint32_t time_state_accent(bool available, bool unlimited, int minutes)
{
    if (!available) return UI_WARNING;
    if (unlimited) return UI_SUCCESS;
    if (minutes <= 0) return UI_DANGER;
    if (minutes <= 15) return UI_WARNING;
    return UI_SUCCESS;
}

static void draw_time_state_card(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    const char *label,
    const char *value,
    uint32_t accent)
{
    int label_size = 18;
    int value_size = 23;
    while (label_size > 16 && measure_text(label, label_size) > rect.width - 16) --label_size;
    while (value_size > 17 && measure_text(value, value_size) > rect.width - 16) --value_size;
    fill_round_rect(pixels, stride, rect, 16, UI_RGB(g_palette->surface_raised));
    draw_text_center(pixels, stride, (UiRect){rect.x + 8, rect.y + 10, rect.width - 16, 26}, label, label_size, UI_MUTED);
    draw_text_center(pixels, stride, (UiRect){rect.x + 8, rect.y + 38, rect.width - 16, 38}, value, value_size, accent);
}

static void format_status_age(const PtcUiModel *model, char *out, size_t out_size)
{
    ptc_ui_format_status_age(model, (int64_t)time(NULL), out, out_size);
}

static uint32_t status_age_color(const PtcUiModel *model)
{
    if (model->waiting) return UI_RGB(g_palette->warning);
    return UI_RGB(ptc_ui_status_is_fresh(model, (int64_t)time(NULL))
        ? g_palette->text_secondary : g_palette->warning);
}

static const char *rule_source_label(const char *source)
{
    if (!source || !source[0]) return "尚未刷新";
    if (strcmp(source, "today_override") == 0) return "今日临时设置";
    if (strcmp(source, "scheduled_override") == 0) return "日期计划";
    if (strcmp(source, "statutory_holiday") == 0) return "国家法定休假日";
    if (strcmp(source, "makeup_workday") == 0) return "国家调休工作日";
    return "周计划";
}

static void draw_plan_card(uint32_t *pixels, uint32_t stride, UiRect card, bool focused)
{
    fill_round_rect(pixels, stride, card, 16, UI_RGB(g_palette->surface));
    draw_rect_outline(pixels, stride, card, 16, focused ? 3 : 1, UI_RGB(focused ? g_palette->focus : g_palette->border_control));
}

static void draw_plan_impact(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                             PtcUiPlanKind kind, bool dirty, UiRect panel)
{
    char impact[256];
    char age[80];
    draw_plan_card(pixels, stride, panel, false);
    draw_text(pixels, stride, panel.x + 22, panel.y + 38,
              dirty ? "修改草稿，尚未保存" : "已保存的计划", 22, dirty ? UI_WARNING : UI_SUCCESS);
    draw_text(pixels, stride, panel.x + 22, panel.y + 76,
              model->disable_flag_present ? "控制已停用，计划只读" :
              (model->waiting ? "正在保存，请稍候" :
               (model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR ? "按 + 完成输入，再保存计划" :
                (dirty ? "按 + 保存后才会应用" : "编辑后可在这里查看影响"))),
              18, UI_RGB(g_palette->text_secondary));
    draw_text(pixels, stride, panel.x + 22, panel.y + 112, "对今天的影响", 20, UI_RGB(g_palette->text_primary));
    ptc_ui_format_plan_impact(model, kind, (int64_t)time(NULL), impact, sizeof(impact));
    draw_wrapped_text(pixels, stride, panel.x + 22, panel.y + 140, impact,
                      18, panel.width - 44, 26, 3, UI_RGB(g_palette->text_secondary));
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, panel.x + 22, panel.y + (kind == PTC_UI_PLAN_HOLIDAY ? 292 : 242),
              age, 16, status_age_color(model));
}

static void draw_weekly_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    int slot;
    char detail[64];
    char freshness[64];
    uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
    format_status_age(model, freshness, sizeof(freshness));
    draw_rect_outline(pixels, stride, (UiRect){54, 180, 26, 24}, 4, 2, UI_ACCENT);
    draw_line(pixels, stride, 54, 188, 80, 188, 2, UI_ACCENT);
    draw_line(pixels, stride, 61, 176, 61, 183, 3, UI_ACCENT);
    draw_line(pixels, stride, 73, 176, 73, 183, 3, UI_ACCENT);
    draw_text(pixels, stride, 92, 202, "周一到周日  |  点按模式或额度区直接修改", 18, UI_MUTED);
    for (slot = 0; slot < 7; ++slot) {
        int day = ptc_ui_weekday_for_display_slot(slot);
        bool selected = slot == model->weekly_grid_slot && model->selected_index == 0;
        bool today = model->status_loaded && day == weekday;
        UiRect card = to_uirect(ptc_ui_weekly_day_rect(slot));
        UiRect header = to_uirect(ptc_ui_weekly_day_header_rect(slot));
        UiRect mode = to_uirect(ptc_ui_weekly_day_mode_rect(slot));
        UiRect minutes = to_uirect(ptc_ui_weekly_day_minutes_rect(slot));
        bool limited = model->draft_week[day].mode == PTC_RULE_MODE_LIMIT;
        draw_plan_card(pixels, stride, card, selected && !model->parent_footer_focused);
        draw_text_center(pixels, stride, header, DAYS[day], 19, UI_INK);
        fill_round_rect(pixels, stride, (UiRect){mode.x + 6, mode.y + 4, mode.width - 12, 28}, 14,
                        model->disable_flag_present ? UI_BORDER :
                        (limited ? UI_ACCENT : UI_SUCCESS));
        draw_text_center(pixels, stride, (UiRect){mode.x + 6, mode.y + 4, mode.width - 12, 28},
                         limited ? "限时" : "不限时", 15,
                         model->disable_flag_present ? UI_DISABLED : UI_ON_ACCENT);
        draw_text_center(pixels, stride, (UiRect){mode.x + 4, mode.y + 38, mode.width - 8, 20},
                         today ? "今天" : " ", 15, today ? UI_SUCCESS : UI_MUTED);
        if (limited) {
            snprintf(detail, sizeof(detail), "%u", (unsigned int)model->draft_week[day].minutes);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 16, minutes.width, 40}, detail, 28,
                             model->disable_flag_present ? UI_DISABLED : UI_ACCENT);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 55, minutes.width, 24}, "分钟", 14, UI_MUTED);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 79, minutes.width, 20}, "A / 点按", 12,
                             model->disable_flag_present ? UI_DISABLED : UI_MUTED);
        } else {
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 29, minutes.width, 36}, "不限时间", 18,
                             model->disable_flag_present ? UI_DISABLED : UI_SUCCESS);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 72, minutes.width, 20}, "点按看提示", 11, UI_DISABLED);
        }
        /* 每日容量柱状直方图 (Weekly Capacity Histogram Bar) */
        int bar_w = card.width - 24;
        int bar_x = card.x + 12;
        int bar_y = card.y + card.height - 11;
        UiRect bar_bg = {bar_x, bar_y, bar_w, 4};
        fill_round_rect(pixels, stride, bar_bg, 2, UI_BORDER);
        if (limited) {
            int fill_w = (int)((int64_t)bar_w * (model->draft_week[day].minutes > 180 ? 180 : model->draft_week[day].minutes) / 180);
            if (fill_w < 4 && model->draft_week[day].minutes > 0) fill_w = 4;
            uint32_t bar_col = model->disable_flag_present ? UI_DISABLED :
                (model->draft_week[day].minutes <= 30 ? UI_WARNING : UI_ACCENT);
            if (fill_w > 0) {
                fill_round_rect(pixels, stride, (UiRect){bar_x, bar_y, fill_w, 4}, 2, bar_col);
            }
        } else {
            fill_round_rect(pixels, stride, bar_bg, 2, model->disable_flag_present ? UI_DISABLED : UI_SUCCESS);
        }
    }
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_WEEKLY, model->weekly_dirty,
                     (UiRect){838, 176, 388, 324});
    draw_candidate_button(pixels, stride, ptc_ui_weekly_page_mode_rect(), "X  切换模式",
                           UI_PAGE, UI_ACCENT, model->selected_index == 1,
                           model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_bulk_rect(), "批量设置",
                          UI_PAGE, UI_ACCENT, model->selected_index == 2,
                          model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_discard_rect(), "ZL  放弃",
                           UI_PAGE, UI_INK, model->selected_index == 3,
                           !model->weekly_dirty);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_save_rect(),
                           model->disable_flag_present ? "只读" : (model->waiting ? "保存中" : (model->weekly_dirty ? "+  保存草稿" : "已保存")),
                           UI_ACCENT, UI_ON_ACCENT, model->selected_index == 4,
                           !model->weekly_dirty || model->disable_flag_present || model->waiting);
    draw_notice(pixels, stride, model, 522, 128);
}

static void draw_toggle_switch(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    bool is_on,
    bool selected,
    bool disabled,
    const char *on_label,
    const char *off_label)
{
    int radius = rect.height / 2;
    uint32_t bg_color = disabled ? UI_BORDER :
                       (is_on ? UI_SUCCESS : UI_BORDER);
    uint32_t knob_color = disabled ? UI_RAISED : UI_SURFACE;
    int knob_size = rect.height - 6;
    int knob_x = is_on ? (rect.x + rect.width - 3 - knob_size) : (rect.x + 3);
    int knob_y = rect.y + 3;

    if (selected && !disabled) {
        fill_round_rect(pixels, stride, (UiRect){rect.x - 3, rect.y - 3, rect.width + 6, rect.height + 6},
                        radius + 3, UI_ACCENT);
    }
    fill_round_rect(pixels, stride, rect, radius, bg_color);
    fill_round_rect(pixels, stride, (UiRect){knob_x, knob_y, knob_size, knob_size}, knob_size / 2, knob_color);

    if (on_label && off_label) {
        const char *label = is_on ? on_label : off_label;
        uint32_t text_color = is_on ? UI_ON_ACCENT : UI_MUTED;
        int label_x = is_on ? (rect.x + 12) : (rect.x + knob_size + 8);
        draw_text(pixels, stride, label_x, rect.y + rect.height / 2 + 5, label, 15, text_color);
    }
}

static void draw_holiday_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {838, 176, 384, 324};
    UiRect top_card = to_uirect(ptc_ui_holiday_card_rect(0));
    const char *titles[] = {"法定休假", "调休工作日"};
    const char *descriptions[] = {"主要法定节假日的休假日期", "节假日调休产生的补班日期"};
    char line[160];
    char minutes_str[64];
    bool disabled = model->disable_flag_present;
    bool top_selected = model->selected_index == 0;
    fill_round_rect(pixels, stride, top_card, 16, disabled ? UI_PAGE : (top_selected ? UI_ACCENT_SOFT : UI_SURFACE));
    draw_rect_outline(pixels, stride, top_card, 16, top_selected ? 3 : 1, top_selected ? UI_ACCENT : UI_BORDER);
    draw_text(pixels, stride, top_card.x + 18, top_card.y + 28, "国家节假日规则", 20, UI_INK);
    draw_text(pixels, stride, top_card.x + 18, top_card.y + 54, "开启后自动应用法定休假与调休工作日规则", 14, UI_MUTED);
    {
        const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
        snprintf(line, sizeof(line), "内置日历：%u  |  v%u", (unsigned int)info->last_year, (unsigned int)info->version);
        draw_text(pixels, stride, top_card.x + 498, top_card.y + 31, line, 14,
                  model->calendar_update_warning ? UI_DANGER : UI_SUCCESS);
    }
    draw_toggle_switch(pixels, stride, to_uirect(ptc_ui_holiday_enable_rect()), model->draft_holiday_enabled,
                       top_selected, disabled, NULL, NULL);

    for (int index = 0; index < 2; ++index) {
        PtcDayRule rule = index == 0 ? model->draft_holiday_rule : model->draft_makeup_workday_rule;
        UiRect card = to_uirect(ptc_ui_holiday_card_rect(index + 1));
        UiRect mode = to_uirect(ptc_ui_holiday_mode_rect(index));
        UiRect minutes = to_uirect(ptc_ui_holiday_minutes_rect(index));
        bool selected = model->selected_index == index + 1;
        bool limited = rule.mode == PTC_RULE_MODE_LIMIT;
        draw_plan_card(pixels, stride, card, selected && !model->parent_footer_focused);
        draw_text(pixels, stride, card.x + 16, card.y + 30, titles[index], 19, UI_RGB(g_palette->text_primary));
        draw_text(pixels, stride, card.x + 16, card.y + 57, descriptions[index], 12, UI_MUTED);
        fill_round_rect(pixels, stride, mode, 18,
                        disabled ? UI_BORDER : (limited ? UI_ACCENT : UI_SUCCESS));
        draw_text_center(pixels, stride, mode, limited ? "限时" : "不限时", 14,
                         disabled ? UI_DISABLED : UI_ON_ACCENT);
        fill_round_rect(pixels, stride, minutes, 12, disabled || !limited ? UI_RAISED : UI_RAISED);
        if (limited) {
            snprintf(minutes_str, sizeof(minutes_str), "%u 分钟（%u小时%u分）", (unsigned int)rule.minutes,
                     (unsigned int)rule.minutes / 60, (unsigned int)rule.minutes % 60);
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 32, minutes_str, 21,
                      disabled ? UI_DISABLED : UI_RGB(g_palette->accent));
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 61, "A / 点按修改额度", 13,
                      disabled ? UI_DISABLED : UI_MUTED);
        } else {
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 32, "不限时间", 21, UI_DISABLED);
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 61, "点按后提示先切换为限时", 13, UI_DISABLED);
        }
    }
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(3), "X  切换模式",
                           UI_PAGE, UI_ACCENT, model->selected_index == 3, disabled);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(4), "ZL  放弃",
                           UI_PAGE, UI_INK, model->selected_index == 4,
                           !model->holiday_dirty);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(5),
                           disabled ? "紧急停用中，设置只读" : (model->waiting ? "正在保存..." :
                           (model->holiday_dirty ? "+  保存草稿" : "已保存")),
                           UI_ACCENT, UI_ON_ACCENT, model->selected_index == 5,
                           disabled || model->waiting || !model->holiday_dirty);
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_HOLIDAY, model->holiday_dirty, panel);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_calendar_rect(), "查看节假日安排",
                           UI_ACCENT_SOFT, UI_ACCENT, model->selected_index == 6, false);
    draw_notice(pixels, stride, model, 522, 128);
}

static const char *short_rule_source(const char *source)
{
    if (source && strcmp(source, "today_override") == 0) return "今日";
    if (source && strcmp(source, "scheduled_override") == 0) return "日期";
    if (source && strcmp(source, "statutory_holiday") == 0) return "休假";
    if (source && strcmp(source, "makeup_workday") == 0) return "调休";
    return "周计划";
}

static void draw_advanced_preview(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 248};
    int index;
    char line[96];
    fill_round_rect(pixels, stride, panel, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    draw_text(pixels, stride, panel.x + 20, panel.y + 29, "未来 7 天规则", 19, UI_INK);
    if (!model->forecast_available) {
        draw_text(pixels, stride, panel.x + 20, panel.y + 68, "刷新状态后显示", 16, UI_MUTED);
        return;
    }
    for (index = 0; index < (int)PTC_RESULT_FORECAST_DAYS; ++index) {
        const PtcResultForecastDay *day = &model->forecast[index];
        if (day->mode == PTC_RULE_MODE_UNLIMITED) {
            snprintf(line, sizeof(line), "%s D+%d  不限时  %s",
                index == 0 ? "今天" : "", index, short_rule_source(day->rule_source));
        } else {
            snprintf(line, sizeof(line), "%s D+%d  %u 分钟  %s",
                index == 0 ? "今天" : "", index, (unsigned int)day->minutes,
                short_rule_source(day->rule_source));
        }
        draw_text(pixels, stride, panel.x + 20, panel.y + 58 + index * 19,
            line, 13, index == 0 ? UI_ACCENT : UI_MUTED);
    }
}

static void draw_parent(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *TITLES[] = {"家长时间管理", "每周游玩计划", "国家节假日安排", "离线加时码", "家长设置"};
    const UiAction *actions;
    int action_count;
    int index;
    draw_header(pixels, stride,
                model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT
                    ? "支持与恢复" :
                model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED
                    ? "高级设置" : TITLES[model->parent_page >= 0 && model->parent_page < PTC_UI_PARENT_PAGE_COUNT ? model->parent_page : 0],
                model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT
                    ? "兼容状态、诊断与安全恢复" :
                model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED
                    ? "日期计划、自主缓冲与启动设置" : "本地规则与设备安全设置");
    draw_disable_banner(pixels, stride, model);
    if (model->demo_secret_enabled) {
        UiRect warning = model->disable_flag_present ? (UiRect){526, 42, 246, 38} : (UiRect){900, 42, 326, 36};
        fill_round_rect(pixels, stride, warning, 6, UI_DANGER_SOFT);
        draw_text_center(pixels, stride, warning,
                         model->disable_flag_present ? "公共演示密钥已启用" : "公共演示密钥已启用  |  低安全模式",
                         17, UI_DANGER);
    }
    draw_tabs(pixels, stride, model);
    if (model->parent_page != PTC_UI_PARENT_PLAN && model->parent_page != PTC_UI_PARENT_HOLIDAY &&
        model->parent_page != PTC_UI_PARENT_TODAY) {
        if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT) {
            actions = SUPPORT_ACTIONS;
            action_count = (int)(sizeof(SUPPORT_ACTIONS) / sizeof(SUPPORT_ACTIONS[0]));
        } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED) {
            actions = ADVANCED_ACTIONS;
            action_count = (int)(sizeof(ADVANCED_ACTIONS) / sizeof(ADVANCED_ACTIONS[0]));
        } else {
            actions = actions_for_page(model->parent_page, &action_count);
        }
        for (index = 0; index < action_count; ++index) {
            UiRect card = to_uirect(model->parent_page == PTC_UI_PARENT_SETTINGS
                ? (model->settings_page == PTC_UI_SETTINGS_ADVANCED
                    ? ptc_ui_advanced_feature_rect(index)
                    : (model->settings_page == PTC_UI_SETTINGS_SUPPORT
                        ? ptc_ui_support_card_rect(index) : ptc_ui_parent_card_rect(index)))
                : ptc_ui_parent_card_rect(index));
            PtcUiActionState astate = PTC_UI_ACTION_AVAILABLE;
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT) {
                if (!ptc_ui_safety_action_visible(model, index)) continue;
                astate = ptc_ui_safety_action_available(model, index);
                if (astate != PTC_UI_ACTION_DISABLED)
                    astate = index == ptc_ui_support_recommended_action(model)
                        ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_AVAILABLE;
            } else if (model->disable_flag_present && model->parent_page == PTC_UI_PARENT_TODAY && index > 0) {
                astate = PTC_UI_ACTION_DISABLED;
            } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ROOT &&
                       index == 4) {
                astate = ptc_ui_settings_support_state(model);
            }
            const UiAction *action = &actions[index];
            UiAction dynamic_action;
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT &&
                model->disable_flag_present && index == 0) {
                action = ptc_ui_runtime_fingerprint_reconfirmation_needed(model)
                    ? &RECONFIRM_ENVIRONMENT_ACTION : &RESUME_CONTROL_ACTION;
            }
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED && index == 0) {
                const char *detail = "重新检查后才能修改";
                dynamic_action = *action;
                if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_OFF) {
                    detail = "自制程序菜单高级入口未配置";
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED) {
                    detail = "需按住 X，再按 A 进入自制程序菜单";
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_ANOMALY) {
                    detail = "检测到 PlayWise 事务异常，请查看详情";
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
                    detail = "入口已可用，但不是由 PlayWise 配置";
                }
                /* Use the card's single subtitle row; a second row at the same y overlaps it. */
                dynamic_action.subtitle = detail;
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_SETTINGS &&
                       model->settings_page == PTC_UI_SETTINGS_ADVANCED && index == 1) {
                dynamic_action = *action;
                dynamic_action.subtitle = model->scheduled_override.enabled
                    ? "当前已启用，打开可调整日期、天数和额度" : "当前关闭";
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_SETTINGS &&
                       model->settings_page == PTC_UI_SETTINGS_ADVANCED && index == 2) {
                static char autonomy_detail[64];
                dynamic_action = *action;
                if (model->autonomy_policy.daily_buffer_minutes > 0u) {
                    snprintf(autonomy_detail, sizeof(autonomy_detail), "当前每天 %u 分钟",
                        (unsigned int)model->autonomy_policy.daily_buffer_minutes);
                } else {
                    snprintf(autonomy_detail, sizeof(autonomy_detail), "当前关闭");
                }
                dynamic_action.subtitle = autonomy_detail;
                action = &dynamic_action;
            }
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ROOT && index == 0) {
                dynamic_action = *action;
                dynamic_action.subtitle = ptc_ui_theme_preference_label(g_theme.preference);
                action = &dynamic_action;
            }
            draw_action_card(pixels, stride, card, action, index == model->selected_index && !model->parent_footer_focused, astate,
                             model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED && index == 0 ? 100 : 0);
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED && index == 0) {
                const char *state_label = "状态未知";
                uint32_t state_color = UI_DANGER;
                if (model->album_restriction_state == 0) {
                    state_label = "未开启";
                    state_color = UI_MUTED;
                } else if (model->album_restriction_state == 1) {
                    state_label = "已开启";
                    state_color = UI_SUCCESS;
                } else if (model->album_restriction_state == 2) {
                    state_label = "需要处理";
                    state_color = UI_WARNING;
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
                    state_label = "外部配置";
                    state_color = UI_ACCENT;
                }
                UiRect badge = {card.x + card.width - 104, card.y + 10, 88, 28};
                fill_round_rect(pixels, stride, badge, 6, UI_PAGE);
                draw_text_center(pixels, stride, badge, state_label, 13, state_color);
            }
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ROOT && index == 4) {
                const char *label = ptc_ui_settings_status_label(model);
                if (label) {
                    UiRect badge = {card.x + card.width - 210, card.y + 12, 88, 28};
                    uint32_t color = strcmp(label, "需处理") == 0 ? UI_DANGER : UI_WARNING;
                    fill_round_rect(pixels, stride, badge, 6, UI_PAGE);
                    draw_text_center(pixels, stride, badge, label, 13, color);
                }
            }
        }
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN) {
        draw_weekly_page(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_HOLIDAY) {
        draw_holiday_page(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_TODAY) {
        draw_today_status(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_GRANT) {
        draw_grant_help(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT) {
        draw_safety_status(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED) {
        draw_advanced_preview(pixels, stride, model);
    }
    if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT &&
        model->diagnostic_status != PTC_UI_DIAGNOSTIC_IDLE) {
        draw_diagnostic_notice(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_TODAY) {
        draw_home_notice(pixels, stride, model);
    } else if (model->parent_page != PTC_UI_PARENT_PLAN && model->parent_page != PTC_UI_PARENT_HOLIDAY) {
        draw_notice(pixels, stride, model, 522, 128);
    }
    if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ROOT) {
        UiRect help = {842, 176, 384, 324};
        draw_plan_card(pixels, stride, help, false);
        draw_text(pixels, stride, 866, 216, "按需要调整", 24, UI_RGB(g_palette->text_primary));
        draw_text(pixels, stride, 866, 262, "日常使用", 20, UI_RGB(g_palette->text_primary));
        draw_text(pixels, stride, 866, 294, "外观、PIN 和家长区快捷键", 18, UI_RGB(g_palette->text_secondary));
        draw_text(pixels, stride, 866, 340, "更多安排", 20, UI_RGB(g_palette->text_primary));
        draw_text(pixels, stride, 866, 372, "高级设置内有日期计划和缓冲", 18, UI_RGB(g_palette->text_secondary));
        draw_text(pixels, stride, 866, 420, "遇到问题", 20, UI_RGB(g_palette->text_primary));
        draw_text(pixels, stride, 866, 452, "打开支持与恢复，查看下一步", 18, UI_RGB(g_palette->text_secondary));
    }
    draw_settings_badge(pixels, stride, model);
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(0),
                       model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page != PTC_UI_SETTINGS_ROOT ? "" : "L  上一页");
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(1),
                       model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page != PTC_UI_SETTINGS_ROOT ? "" : "R  下一页");
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(2),
                       model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page != PTC_UI_SETTINGS_ROOT ? "B  返回设置" : "B  返回孩子页");
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(3), "Y  刷新");
    if (model->parent_footer_focused && model->parent_footer_selection == 0) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_parent_footer_rect(3)), 12, 3, UI_ACCENT);
    }
    draw_parent_status_footer(pixels, stride, model);
}

static const char *rule_mode_label(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_UNLIMITED:
        return "不限时";
    case PTC_RULE_MODE_LIMIT:
    default:
        return "限时";
    }
}

static void draw_dialog_shell(
    uint32_t *pixels,
    uint32_t stride,
    const PtcUiModel *model,
    UiRect *dialog,
    int width,
    int height)
{
    bool numeric = model->overlay == PTC_UI_OVERLAY_NUMPAD || model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR;
    bool pin = model->overlay == PTC_UI_OVERLAY_PIN;
    const char *title = numeric ? model->numpad_title : (pin ? model->pin_title : model->overlay_title);
    const char *description = numeric ? model->numpad_guide : (pin ? model->pin_guide : model->overlay_body);
    *dialog = to_uirect(ptc_ui_dialog_rect(width, height));
    fill_rect_packed(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT},
                     pack_rgb(g_palette->scrim));
    draw_drop_shadow(pixels, stride, *dialog, 16, 6);
    fill_round_rect(pixels, stride, *dialog, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, *dialog, 16, 1, UI_BORDER);
    draw_inner_top_highlight(pixels, stride, *dialog, 16);
    fill_round_rect(pixels, stride, (UiRect){dialog->x + 34, dialog->y + 15, 36, 5}, 2, UI_CORAL);
    draw_text_bold(pixels, stride, dialog->x + 34, dialog->y + 54, title, 29, UI_INK);
    if (description[0]) {
        draw_wrapped_text(pixels, stride, dialog->x + 34, dialog->y + 88, description,
                          18, dialog->width - 68, 26, 6, UI_MUTED);
    }
}

static void draw_minutes_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect value_box = to_uirect(ptc_ui_minutes_value_rect());
    char value[32];
    char duration[64];
    char preview_line[128];
    char played_line[64];
    char remaining_line[64];
    char date_line[64];
    char freshness[64];
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    int preview_min = ptc_ui_preview_remaining_minutes(model);
    int played_min = model->played_minutes_available ? model->played_minutes : -1;

    draw_dialog_shell(pixels, stride, model, &dialog, 720, 560);
    snprintf(value, sizeof(value), "%u 分钟", (unsigned int)model->draft_minutes);
    format_duration(model->draft_minutes, duration, sizeof(duration));
    fill_round_rect(pixels, stride, value_box, 16, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, value_box, 16, 2, UI_ACCENT);
    draw_text_center(pixels, stride, value_box, value, 39, UI_ACCENT);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_dec_rect(), "-5", UI_RAISED, UI_ACCENT, true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_inc_rect(), "+5", UI_RAISED, UI_ACCENT, true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_inc_large_rect(), "+15", UI_RAISED, UI_ACCENT, true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_dec_large_rect(), "-15", UI_RAISED, UI_ACCENT, true);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 242, 640, 26}, duration, 19,
                     UI_ACCENT);

    if (model->status_loaded && ptc_date_from_day_index(model->day_index, &year, &month, &day)) {
        snprintf(date_line, sizeof(date_line), "影响日期：%u 年 %u 月 %u 日（今天）", year, month, day);
    } else {
        snprintf(date_line, sizeof(date_line), "影响日期：今天");
    }
    if (played_min >= 0) {
        snprintf(played_line, sizeof(played_line), "额度已耗（估算）：约 %d 分钟", played_min);
    } else {
        snprintf(played_line, sizeof(played_line), "额度已耗（估算）：暂不可用");
    }
    if (model->unrestricted_today == 1) {
        snprintf(remaining_line, sizeof(remaining_line), "今天还可玩：不限时");
    } else if (model->remaining_available && model->remaining_minutes >= 0) {
        snprintf(remaining_line, sizeof(remaining_line), "今天还可玩：%d 分钟", model->remaining_minutes);
    } else {
        snprintf(remaining_line, sizeof(remaining_line), "今天还可玩：暂不可用");
    }
    if (preview_min >= 0) {
        snprintf(preview_line, sizeof(preview_line), "修改后预计还可玩：约 %d 分钟", preview_min);
    } else if (model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        snprintf(preview_line, sizeof(preview_line), "修改后预计还可玩：暂不可用；生效后刷新确认");
    } else {
        snprintf(preview_line, sizeof(preview_line), "今日总额度将改为 %u 分钟；实际剩余将在刷新后确认", (unsigned int)model->draft_minutes);
    }
    format_status_age(model, freshness, sizeof(freshness));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 316, 640, 24}, date_line, 17, UI_MUTED);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 44, dialog.y + 344, 304, 38}, 16, UI_RAISED);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 372, dialog.y + 344, 304, 38}, 16, UI_RAISED);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 44, dialog.y + 344, 304, 38}, played_line, 17, UI_MUTED);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 372, dialog.y + 344, 304, 38}, remaining_line, 17,
                     model->unrestricted_today == 1 ? UI_SUCCESS : UI_MUTED);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 390, 640, 30}, preview_line, 20, UI_SUCCESS);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 423, 640, 22}, freshness, 16,
                     status_age_color(model));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 50, dialog.y + 448, 620, 22},
                     "本次修改只影响今天  |  Y 或点击数值手动输入", 16, UI_MUTED);
    draw_overlay_actions(pixels, stride, model, "A / +  提交并刷新");
}

static void draw_weekly_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    UiRect dialog;
    int day;
    char selected_minutes[32];
    draw_dialog_shell(pixels, stride, model, &dialog, 1172, 560);
    for (day = 0; day < 7; ++day) {
        UiRect card = to_uirect(ptc_ui_weekly_day_rect(day));
        uint32_t border = day == model->editor_index ? UI_ACCENT : UI_BORDER;
        char minutes[32];
        fill_round_rect(pixels, stride, card, 16, day == model->editor_index ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, card, 16, day == model->editor_index ? 3 : 1, border);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 14, card.width, 34}, DAYS[day], 21, UI_INK);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 69, card.width, 34}, rule_mode_label(model->draft_week[day].mode), 22,
                         UI_ACCENT);
        if (model->draft_week[day].mode == PTC_RULE_MODE_LIMIT) {
            snprintf(minutes, sizeof(minutes), "%u 分钟", (unsigned int)model->draft_week[day].minutes);
        } else {
            snprintf(minutes, sizeof(minutes), "--");
        }
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 119, card.width, 34}, minutes, 19, UI_MUTED);
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 80, dialog.y + 320, dialog.width - 160, 30},
                     "选择日期后：X 切换模式，Y 或点数值手动输入", 19, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_weekly_mode_rect(), "X 切换模式", UI_RAISED, UI_ACCENT, true);
    if (model->draft_week[model->editor_index].mode == PTC_RULE_MODE_LIMIT) {
        snprintf(selected_minutes, sizeof(selected_minutes), "%u 分钟",
                 (unsigned int)model->draft_week[model->editor_index].minutes);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_up_rect(), "+15", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_down_rect(), "-15", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_dec_rect(), "-5", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_inc_rect(), "+5", UI_RAISED, UI_ACCENT, true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_input_rect(), selected_minutes,
                           UI_ACCENT_SOFT, UI_ACCENT, true);
    }
    draw_overlay_actions(pixels, stride, model, "A / +  保存并刷新");
}

static void draw_numpad_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *KEY_LABELS[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "X 退格", "0", "Y 清空"
    };
    static const char *QUICK_LABELS[] = {"ZL -15", "L -5", "R +5", "ZR +15"};
    UiRect dialog;
    UiRect display = to_uirect(ptc_ui_numpad_display_rect());
    char shown[32];
    char current[64];
    char duration[64];
    uint16_t entered_minutes = 0;
    bool weekly_today_preview = false;
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 620, 700);
    if ((model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
         model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES ||
         model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
         model->numpad_purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) && model->numpad_text[0]) {
        snprintf(shown, sizeof(shown), "%s 分钟", model->numpad_text);
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE && model->numpad_text[0]) {
        ptc_ui_format_code(model->numpad_text, shown, sizeof(shown));
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        snprintf(shown, sizeof(shown), "输入加时码");
    } else if (model->numpad_text[0]) {
        snprintf(shown, sizeof(shown), "%s", model->numpad_text);
    } else {
        snprintf(shown, sizeof(shown), "%.*s", model->numpad_max_digits, "________");
    }
    fill_round_rect(pixels, stride, display, 16, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, display, 16, 2, UI_ACCENT);
    draw_text_center(pixels, stride, display, shown, 30, UI_ACCENT);

    if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES) {
        snprintf(current, sizeof(current), "当前值：%u 分钟   |   范围 %u到%u",
                 (unsigned int)model->numpad_current, (unsigned int)model->numpad_minimum,
                 (unsigned int)model->numpad_maximum);
        if (!ptc_ui_parse_minutes(model->numpad_text, model->numpad_minimum, model->numpad_maximum, &entered_minutes)) {
            entered_minutes = model->numpad_current;
        }
        format_duration(entered_minutes, duration, sizeof(duration));
    } else {
        if (model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) {
            uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
            PtcDayRule entered_rule;
            snprintf(current, sizeof(current), "当前值：%u 分钟   |   范围 %u到%u",
                     (unsigned int)model->numpad_current, (unsigned int)model->numpad_minimum,
                     (unsigned int)model->numpad_maximum);
            if (!ptc_ui_parse_minutes(model->numpad_text, model->numpad_minimum, model->numpad_maximum, &entered_minutes)) {
                entered_minutes = model->numpad_current;
            }
            format_duration(entered_minutes, duration, sizeof(duration));
            entered_rule = model->draft_week[model->editor_index];
            entered_rule.minutes = entered_minutes;
            weekly_today_preview = model->editor_index == weekday &&
                ptc_ui_day_rule_effectively_changed(model->current_week[weekday], entered_rule);
        } else if (model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
                   model->numpad_purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) {
            snprintf(current, sizeof(current), "当前值：%u 分钟  |  范围 %u到%u",
                     (unsigned int)model->numpad_current, (unsigned int)model->numpad_minimum,
                     (unsigned int)model->numpad_maximum);
            if (!ptc_ui_parse_minutes(model->numpad_text, model->numpad_minimum, model->numpad_maximum, &entered_minutes)) {
                entered_minutes = model->numpad_current;
            }
            format_duration(entered_minutes, duration, sizeof(duration));
        } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
            unsigned int len = (unsigned int)strlen(model->numpad_text);
            snprintf(current, sizeof(current), "请输入 8 位加时码   |   当前已输入 %u/8 位", len);
            duration[0] = '\0';
        } else {
            snprintf(current, sizeof(current), "请输入完整的 8 位加时码");
            duration[0] = '\0';
        }
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 218, dialog.width - 80, 22}, current, 16, UI_MUTED);
    if (weekly_today_preview) {
        char current_value[48];
        char after_value[48];
        char left[96];
        char right[112];
        int after_minutes = model->played_minutes_available
            ? (int)entered_minutes - model->played_minutes : -1;
        if (after_minutes < 0 && model->played_minutes_available) after_minutes = 0;
        if (model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "不限时");
        else format_duration(model->remaining_available ? model->remaining_minutes : -1,
                             current_value, sizeof(current_value));
        format_duration(after_minutes, after_value, sizeof(after_value));
        snprintf(left, sizeof(left), "%s：%s",
                 "今天还可玩", current_value);
        snprintf(right, sizeof(right), "%s：%s",
                 model->today_override_present ? "恢复后预计还可玩" : "保存后预计还可玩", after_value);
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 32, dialog.y + 242, 266, 32}, 6, UI_RAISED);
        draw_rect_outline(pixels, stride, (UiRect){dialog.x + 32, dialog.y + 242, 266, 32}, 6, 1, time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                            model->unrestricted_today == 1, model->remaining_minutes));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 244, 258, 28}, left, 14, UI_INK);
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 322, dialog.y + 242, 266, 32}, 6, UI_RAISED);
        draw_rect_outline(pixels, stride, (UiRect){dialog.x + 322, dialog.y + 242, 266, 32}, 6, 1, time_state_accent(model->played_minutes_available, false, after_minutes));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 326, dialog.y + 244, 258, 28}, right,
                         model->today_override_present ? 12 : 14, UI_INK);
    } else if (duration[0]) {
        char duration_line[80];
        snprintf(duration_line, sizeof(duration_line), "换算：%s", duration);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 244, dialog.width - 80, 22},
                         duration_line, 17, UI_ACCENT);
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        char console_date[64];
        char date_line[128];
        ptc_ui_format_console_date(model, console_date, sizeof(console_date));
        snprintf(date_line, sizeof(date_line), "%s  |  生成加时码请选这一天", console_date);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 244, dialog.width - 80, 22},
                         date_line, 16, model->status_loaded ? UI_ACCENT : UI_WARNING);
    }
    if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
        model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES ||
        model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
        model->numpad_purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) {
        for (index = 0; index < 4; ++index) {
            draw_dialog_button(pixels, stride, ptc_ui_numpad_quick_rect(index), QUICK_LABELS[index],
                               UI_RAISED, UI_ACCENT, true);
        }
    }
    for (index = 0; index < 12; ++index) {
        UiRect key = to_uirect(ptc_ui_numpad_key_rect(index));
        bool selected = index == model->numpad_cursor;
        fill_round_rect(pixels, stride, key, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, key, 12, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
        draw_text_center(pixels, stride, key, KEY_LABELS[index], index == 9 || index == 11 ? 18 : 30,
                         selected ? UI_ACCENT : UI_INK);
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 35, dialog.y + 548, dialog.width - 70, 24},
                     "方向键/摇杆选择  A 输入  X 退格  Y 清空  + 完成", 17, UI_MUTED);
    if (model->numpad_error[0]) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 35, dialog.y + 576, dialog.width - 70, 24},
                         model->numpad_error, 17, UI_DANGER);
    }
    draw_overlay_actions(pixels, stride, model, "+  完成输入");
}

static void draw_pin_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog = to_uirect(ptc_ui_pin_dialog_rect());
    UiRect display = {dialog.x + 40, dialog.y + 112, 480, 70};
    char mask[PTC_UI_PIN_MAX_DIGITS + 1];
    char count[64];
    int row;
    ptc_ui_pin_format_mask(model, mask, sizeof(mask));
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 620);
    fill_round_rect(pixels, stride, display, 16, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, display, 16, 2, UI_ACCENT);
    draw_text_center(pixels, stride, display, mask[0] ? mask : "输入内容只显示为圆点", mask[0] ? 26 : 18,
                     mask[0] ? UI_ACCENT : UI_MUTED);
    snprintf(count, sizeof(count), "已输入 %u 位", (unsigned int)strlen(model->pin_text));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 188, 480, 24}, count, 17, UI_MUTED);

    draw_text(pixels, stride, dialog.x + 40, dialog.y + 222, "摇杆方向映射", 20, UI_INK);
    {
        int i;
        int cx = dialog.x + 180;
        int cy = dialog.y + 338;
        fill_round_rect(pixels, stride, (UiRect){cx - 48, cy - 48, 96, 96}, 48, UI_RAISED);
        draw_circle_outline(pixels, stride, cx, cy, 48, 2, UI_ACCENT);
        draw_circle_outline(pixels, stride, cx, cy, 36, 2, UI_CONTROL);
        draw_text_center(pixels, stride, (UiRect){cx - 32, cy - 18, 64, 36}, "摇杆", 13, UI_ACCENT);
        
        for (i = 1; i <= 8; ++i) {
            int dx = 0, dy = 0;
            UiRect cell;
            char label[4];
            switch (i) {
            case 1: dx = 0; dy = -78; break;
            case 2: dx = 62; dy = -62; break;
            case 3: dx = 78; dy = 0; break;
            case 4: dx = 62; dy = 62; break;
            case 5: dx = 0; dy = 78; break;
            case 6: dx = -62; dy = 62; break;
            case 7: dx = -78; dy = 0; break;
            case 8: dx = -62; dy = -62; break;
            }
            cell = (UiRect){cx + dx - 18, cy + dy - 18, 36, 36};
            snprintf(label, sizeof(label), "%d", i);
            draw_text_center(pixels, stride, cell, label, 22, UI_ACCENT);
        }
        
        int bcx = dialog.x + 410;
        int bcy = cy;
        for (i = 0; i < 4; ++i) {
            int dx = 0, dy = 0;
            int d = 48;
            int digit = -1;
            const char *button = "";
            UiRect cell;
            switch (i) {
            case 0: dx = 0; dy = -d; digit = 0; button = "X"; break;
            case 1: dx = d; dy = 0; break;
            case 2: dx = 0; dy = d; break;
            case 3: dx = -d; dy = 0; digit = 9; button = "Y"; break;
            }
            cell = (UiRect){bcx + dx - 26, bcy + dy - 26, 52, 52};
            fill_round_rect(pixels, stride, cell, 26, UI_RAISED);
            draw_circle_outline(pixels, stride, bcx + dx, bcy + dy, 26, 2,
                                digit >= 0 ? UI_ACCENT : UI_CONTROL);
            if (digit >= 0) {
                char digit_label[4];
                snprintf(digit_label, sizeof(digit_label), "%d", digit);
                draw_text_center(pixels, stride, (UiRect){cell.x, cell.y + 3, cell.width, 21},
                                 button, 12, UI_MUTED);
                draw_text_center(pixels, stride, (UiRect){cell.x, cell.y + 19, cell.width, 28},
                                 digit_label, 20, UI_ACCENT);
            }
        }
    }
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 452, "左右摇杆均可输入；十字键支持上、右、下、左", 16, UI_MUTED);

    draw_text(pixels, stride, dialog.x + 590, dialog.y + 212, "触摸数字键盘", 20, UI_INK);
    for (row = 0; row < 10; ++row) {
        UiRect key = to_uirect(ptc_ui_pin_key_rect(row));
        bool selected = model->pin_focus == row;
        char label[4];
        snprintf(label, sizeof(label), "%d", row);
        fill_round_rect(pixels, stride, key, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, key, 12, selected ? 2 : 1, selected ? UI_ACCENT : UI_CONTROL);
        draw_text_center(pixels, stride, key, label, 24, selected ? UI_ACCENT : UI_INK);
    }
    draw_dialog_button(pixels, stride, ptc_ui_pin_backspace_rect(), "ZL  退格",
                       UI_WARNING_SOFT, UI_WARNING, true);
    draw_dialog_button(pixels, stride, ptc_ui_pin_confirm_rect(), "+  确认",
                       UI_ACCENT, UI_ON_ACCENT, false);
    draw_dialog_button(pixels, stride, ptc_ui_pin_cancel_rect(), "B  取消",
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_pin_keyboard_rect(), "长按 + 传统键盘",
                       UI_RAISED, UI_INK, true);
    draw_text(pixels, stride, dialog.x + 590, dialog.y + 594,
              model->pin_error[0] ? model->pin_error : "短按 + 确认；长按 + 约 1 秒切换传统键盘",
              16, model->pin_error[0] ? UI_DANGER : UI_MUTED);
}

static void draw_confirm_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char comparison[128];
    PtcUiModel shell_model;
    bool restore = model->operation == PTC_UI_OPERATION_RESTORE_TODAY_POLICY;
    bool limit_change = model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT;
    bool code_preview = model->operation == PTC_UI_OPERATION_REDEEM_OFFLINE_CODE;
    bool album_change = model->operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
                        model->operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
                        model->operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY;
    bool danger = model->operation == PTC_UI_OPERATION_DISABLE_TODAY_LIMIT ||
                  model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT ||
                  model->operation == PTC_UI_OPERATION_SAVE_WEEKLY ||
                  model->operation == PTC_UI_OPERATION_EMERGENCY_DISABLE ||
                  model->operation == PTC_UI_OPERATION_RESUME_CONTROL ||
                  model->operation == PTC_UI_OPERATION_COMPLETE_SETUP ||
                  model->operation == PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT ||
                  code_preview;
    shell_model = *model;
    if (code_preview) {
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "代码时长 %d 分钟，本次预计增加 %d 分钟。成功兑换后仅可使用一次。",
                 model->code_grant_minutes, model->code_effective_add_minutes);
    } else if (restore) {
        ptc_ui_format_restore_today_basis(model, shell_model.overlay_body, sizeof(shell_model.overlay_body));
    } else if (limit_change) {
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "请核对今天的实时状态和修改结果。");
    }
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 760, 420);
    if (code_preview) {
        char current_value[48];
        char after_value[48];
        if (model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "不限时");
        else format_duration(model->remaining_available ? model->remaining_minutes : -1,
                             current_value, sizeof(current_value));
        format_duration(model->code_preview_after_available ? model->code_preview_after_minutes : -1,
                        after_value, sizeof(after_value));
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 142, 300, 92},
                             "今天还可玩", current_value,
                             time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                               model->unrestricted_today == 1, model->remaining_minutes));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 354, dialog.y + 166, 52, 42}, "到", 28, UI_MUTED);
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 406, dialog.y + 142, 300, 92},
                             "兑换后预计还可玩", after_value,
                             time_state_accent(model->code_preview_after_available, false,
                                               model->code_preview_after_minutes));
    } else if (restore) {
        PtcEffectiveRule restored = ptc_ui_rule_after_today_restore(model);
        PtcDayRule current = model->today_override_present ? model->today_override_rule : restored.rule;
        PtcDayRule after = restored.rule;
        char current_value[48];
        char after_value[48];
        int current_minutes = current.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available
            ? (int)current.minutes - model->played_minutes : -1;
        int after_minutes = after.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available
            ? (int)after.minutes - model->played_minutes : -1;
        if (current_minutes < 0 && current.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available) current_minutes = 0;
        if (after_minutes < 0 && after.mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available) after_minutes = 0;
        if (current.mode == PTC_RULE_MODE_UNLIMITED) snprintf(current_value, sizeof(current_value), "不限时");
        else format_duration(current_minutes, current_value, sizeof(current_value));
        if (after.mode == PTC_RULE_MODE_UNLIMITED) snprintf(after_value, sizeof(after_value), "不限时");
        else format_duration(after_minutes, after_value, sizeof(after_value));
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 142, 300, 92},
                             "今天还可玩", current_value,
                             time_state_accent(current.mode == PTC_RULE_MODE_UNLIMITED || current_minutes >= 0,
                                               current.mode == PTC_RULE_MODE_UNLIMITED, current_minutes));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 354, dialog.y + 166, 52, 42}, "到", 28, UI_MUTED);
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 406, dialog.y + 142, 300, 92},
                             "恢复后还能玩", after_value,
                             time_state_accent(after.mode == PTC_RULE_MODE_UNLIMITED || after_minutes >= 0,
                                               after.mode == PTC_RULE_MODE_UNLIMITED, after_minutes));
    } else if (limit_change) {
        char played_value[48];
        char current_value[48];
        char after_value[48];
        int after_minutes = model->played_minutes_available ? (int)model->draft_minutes - model->played_minutes : -1;
        if (after_minutes < 0 && model->played_minutes_available) after_minutes = 0;
        format_duration(model->played_minutes_available ? model->played_minutes : -1, played_value, sizeof(played_value));
        if (model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "不限时");
        else format_duration(model->remaining_available ? model->remaining_minutes : -1, current_value, sizeof(current_value));
        format_duration(after_minutes, after_value, sizeof(after_value));
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 142, 214, 92},
                             "额度已耗（估算）", played_value,
                             model->played_minutes_available ? UI_ACCENT : UI_WARNING);
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 273, dialog.y + 142, 214, 92},
                             "今天还可玩", current_value,
                             time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                               model->unrestricted_today == 1, model->remaining_minutes));
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 512, dialog.y + 142, 214, 92},
                             "修改后预计还可玩", after_value,
                             time_state_accent(after_minutes >= 0, false, after_minutes));
    } else if (model->confirm_hold_required && model->played_minutes_available) {
        snprintf(comparison, sizeof(comparison), "额度已耗 %d 分钟       还剩 0 分钟",
                 model->played_minutes);
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 218, 652, 92}, 16, UI_DANGER_SOFT);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 226, 652, 34}, comparison, 25, UI_DANGER);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 264, 652, 34},
                         "新额度不高于额度消耗估算，保存后会马上限制儿童使用", 20, UI_DANGER);
    }
    if (restore || limit_change || code_preview) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54}, 16, model->confirm_hold_required ? UI_DANGER_SOFT : UI_WARNING_SOFT);
        if (code_preview) {
            char warning[160];
            if (model->code_preview_converts_unlimited) {
                snprintf(warning, sizeof(warning), "当前不限时，兑换后将改为限时%s",
                         model->confirm_hold_required ? "；请长按 A 确认" : "");
            } else if (!model->code_preview_after_available) {
                snprintf(warning, sizeof(warning), "实时状态未知；请长按 A 确认");
            } else if (model->code_preview_after_minutes == 0) {
                snprintf(warning, sizeof(warning), "兑换后预计没有可玩时间；请长按 A 确认");
            } else if (model->code_preview_capped) {
                snprintf(warning, sizeof(warning), "受每日 1440 分钟上限影响，预计增加 %d 分钟",
                         model->code_effective_add_minutes);
            } else {
                snprintf(warning, sizeof(warning), "确认后才会生效并消费这枚一次性加时码");
            }
            draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                             warning, 18, model->confirm_hold_required ? UI_DANGER : UI_WARNING);
        } else if (limit_change) {
            char risk[160];
            char recovery[128];
            ptc_ui_format_today_limit_confirmation(model, risk, sizeof(risk), recovery, sizeof(recovery));
            draw_text_center(pixels, stride, (UiRect){dialog.x + 64, dialog.y + 254, 632, 24},
                             risk, 15, model->confirm_hold_required ? UI_DANGER : UI_WARNING);
            draw_text_center(pixels, stride, (UiRect){dialog.x + 64, dialog.y + 280, 632, 22},
                             recovery, 15, UI_MUTED);
        } else {
            draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                             model->confirm_hold_required
                                ? (model->played_minutes_available ? "操作后可能立即限制游玩，请长按 A 确认" : "无法取得额度消耗估算，不能判断是否立即限制")
                                : (limit_change && model->unrestricted_today == 1 ? "不限时将改为限时，请确认状态变化" : "请确认状态变化"),
                             19, model->confirm_hold_required ? UI_DANGER : UI_WARNING);
        }
    } else if (!album_change && (!model->confirm_hold_required || !model->played_minutes_available)) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 230, 620, 72}, 16, danger ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 230, 620, 72},
                         danger ? "请确认已了解这项操作的影响" : "确认执行这项操作", 22,
                         danger ? UI_DANGER : UI_SUCCESS);
    }
    draw_overlay_actions(pixels, stride, model,
                         model->confirm_hold_required ? "长按 A / 触摸按住" : "A  确认执行");
}

static void draw_minute_editor_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *KEY_LABELS[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "X 退格", "0", "Y 清空"
    };
    static const char *QUICK_LABELS[] = {"ZL -15", "L -5", "R +5", "ZR +15"};
    UiRect dialog;
    uint16_t entered = model->numpad_current;
    char value[48];
    char hours_value[32];
    char minutes_value[32];
    char total_value[48];
    char played[48];
    char remaining[48];
    char after[48];
    char freshness[64];
    char holiday_line[160];
    char holiday_detail[160];
    char holiday_date[64];
    char fitted[160];
    int after_minutes = -1;
    bool weekly = model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES;
    bool holiday = model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
                   model->numpad_purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES;
    bool entered_valid = ptc_ui_duration_value(model, &entered);
    draw_dialog_shell(pixels, stride, model, &dialog, 920, 620);
    if (entered_valid) {
        snprintf(value, sizeof(value), "%u 分钟", (unsigned int)entered);
        snprintf(total_value, sizeof(total_value), "总计 %u 分钟", (unsigned int)entered);
    } else {
        snprintf(value, sizeof(value), "暂不可用");
        snprintf(total_value, sizeof(total_value), "总计 -- 分钟");
    }
    snprintf(hours_value, sizeof(hours_value), "%s 小时",
             model->duration_hours_text[0] ? model->duration_hours_text : "--");
    snprintf(minutes_value, sizeof(minutes_value), "%s 分钟",
             model->duration_minutes_text[0] ? model->duration_minutes_text : "--");
    format_duration(model->played_minutes_available ? model->played_minutes : -1, played, sizeof(played));
    if (model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), "不限时");
    else format_duration(model->remaining_available ? model->remaining_minutes : -1, remaining, sizeof(remaining));
    if (weekly) {
        uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
        if (entered_valid && model->editor_index == weekday && model->played_minutes_available) {
            after_minutes = (int)entered - model->played_minutes;
        }
    } else if (entered_valid && model->numpad_purpose == PTC_UI_NUMPAD_MINUTES && model->played_minutes_available) {
        after_minutes = model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES && model->remaining_available
            ? model->remaining_minutes + entered : (int)entered - model->played_minutes;
    }
    if (after_minutes < 0 && model->played_minutes_available &&
        (model->numpad_purpose != PTC_UI_NUMPAD_WEEKLY_MINUTES ||
         model->editor_index == ptc_weekday_from_day_index(model->day_index))) after_minutes = 0;
    format_duration(after_minutes, after, sizeof(after));
    format_status_age(model, freshness, sizeof(freshness));

    /* The compact editor keeps the keypad on the left and puts the two-part duration on the right. */
    for (int field = 0; field < 2; ++field) {
        UiRect rect = to_uirect(ptc_ui_minute_editor_field_rect((PtcUiDurationField)field));
        bool selected = model->duration_field == (PtcUiDurationField)field;
        fill_round_rect(pixels, stride, rect, 16, selected ? UI_ACCENT_SOFT : UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, rect, 16, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
        draw_text_center(pixels, stride, rect,
                         field == PTC_UI_DURATION_HOURS ? hours_value : minutes_value,
                         25, selected ? UI_ACCENT : UI_INK);
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 218, 350, 28},
                     total_value, 19, entered_valid ? UI_ACCENT : UI_DANGER);
    if (holiday) {
        PtcCalendarDayType type = model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES
            ? PTC_CALENDAR_DAY_STATUTORY_HOLIDAY : PTC_CALENDAR_DAY_MAKEUP_WORKDAY;
        PtcHolidayCalendarMatch match;
        bool today_match = model->status_loaded && ptc_holiday_calendar_find(type, model->day_index, &match) &&
                           match.day_index == model->day_index;
        bool found = today_match || (model->status_loaded && model->day_index < UINT16_MAX &&
                     ptc_holiday_calendar_find(type, (uint16_t)(model->day_index + 1u), &match));
        uint16_t year = 0;
        uint8_t month = 0;
        uint8_t day = 0;
        const char *kind = type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY ? "法定休假日" : "调休工作日";
        snprintf(holiday_line, sizeof(holiday_line), "规则类型：%s", kind);
        if (found && ptc_date_from_day_index(match.day_index, &year, &month, &day)) {
            snprintf(holiday_date, sizeof(holiday_date), "%u-%02u-%02u", year, month, day);
            if (today_match) {
                if (!model->draft_holiday_enabled) {
                    snprintf(holiday_detail, sizeof(holiday_detail), "今日日期命中：%s（预设未开启）",
                             match.arrangement ? match.arrangement->display_name : kind);
                } else if (model->today_override_present) {
                    snprintf(holiday_detail, sizeof(holiday_detail), "今日日期命中：%s（被临时设置覆盖）",
                             match.arrangement ? match.arrangement->display_name : kind);
                } else if (ptc_ui_plan_rule(model, PTC_UI_PLAN_HOLIDAY).source == PTC_RULE_SOURCE_SCHEDULED_OVERRIDE) {
                    snprintf(holiday_detail, sizeof(holiday_detail), "今日日期命中：%s（被日期计划覆盖）",
                             match.arrangement ? match.arrangement->display_name : kind);
                } else if (!model->holiday_enabled) {
                    snprintf(holiday_detail, sizeof(holiday_detail), "今日日期命中：%s（保存后启用）",
                             match.arrangement ? match.arrangement->display_name : kind);
                } else {
                    snprintf(holiday_detail, sizeof(holiday_detail), "今日正在适用：%s",
                             match.arrangement ? match.arrangement->display_name : kind);
                }
            } else {
                snprintf(holiday_detail, sizeof(holiday_detail), "下一次安排：%s  |  %s",
                         match.arrangement ? match.arrangement->display_name : kind, holiday_date);
            }
        } else {
            snprintf(holiday_detail, sizeof(holiday_detail), "内置日历未覆盖或暂无后续安排");
        }
        draw_text(pixels, stride, dialog.x + 548, dialog.y + 276, holiday_line, 17, UI_INK);
        fit_text(fitted, sizeof(fitted), holiday_detail, 15, 326);
        draw_text(pixels, stride, dialog.x + 548, dialog.y + 310, fitted, 15,
                  today_match ? UI_SUCCESS : UI_MUTED);
        draw_text(pixels, stride, dialog.x + 548, dialog.y + 344,
                  model->draft_holiday_enabled ? "当前预设：已开启" : "当前预设：未开启", 16,
                  model->draft_holiday_enabled ? UI_SUCCESS : UI_WARNING);
        draw_text(pixels, stride, dialog.x + 548, dialog.y + 378,
                  model->today_override_present ? "今天临时设置优先，当前额度可能未生效" : "节假日规则命中后优先于周计划",
                  15, model->today_override_present ? UI_WARNING : UI_MUTED);
    } else if (weekly) {
        PtcUiModel preview = *model;
        if (entered_valid) {
            preview.draft_week[model->editor_index].minutes = entered;
            draw_plan_impact(pixels, stride, &preview, PTC_UI_PLAN_WEEKLY, true,
                             (UiRect){dialog.x + 536, dialog.y + 264, 350, 254});
        } else {
            draw_text(pixels, stride, dialog.x + 558, dialog.y + 310, "请先输入有效额度", 18, UI_RGB(g_palette->danger));
        }
    } else {
        static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        uint8_t today_weekday = ptc_weekday_from_day_index(model->day_index);
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 264, 350, 74}, "额度已耗（估算）", played,
                             model->played_minutes_available ? UI_ACCENT : UI_WARNING);
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 350, 350, 74}, "今天还可玩", remaining,
                             time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                               model->unrestricted_today == 1, model->remaining_minutes));
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 436, 350, 74},
                             weekly && model->editor_index != today_weekday ? "所选星期草稿额度" :
                             (weekly ? "保存后预计还可玩" : "修改后预计还可玩"),
                             weekly && model->editor_index != today_weekday ? value : after,
                             time_state_accent(weekly && model->editor_index != today_weekday ? entered_valid : after_minutes >= 0,
                                               false,
                                               weekly && model->editor_index != today_weekday && entered_valid ? (int)entered : after_minutes));
        if (weekly && model->editor_index != today_weekday) {
            snprintf(freshness, sizeof(freshness), "%s  |  今天不受本次修改影响", DAYS[model->editor_index]);
            draw_text_center(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 518, 350, 28}, freshness,
                             15, UI_MUTED);
        } else {
            draw_text_center(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 518, 350, 28}, freshness, 18,
                             status_age_color(model));
        }
    }

    for (int index = 0; index < 4; ++index) {
        draw_dialog_button(pixels, stride, ptc_ui_minute_editor_quick_rect(index), QUICK_LABELS[index],
                           UI_RAISED, UI_ACCENT, true);
    }
    for (int index = 0; index < 12; ++index) {
        UiRect key = to_uirect(ptc_ui_minute_editor_key_rect(index));
        bool selected = index == model->numpad_cursor;
        fill_round_rect(pixels, stride, key, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, key, 12, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
        draw_text_center(pixels, stride, key, KEY_LABELS[index], index == 9 || index == 11 ? 18 : 28,
                         selected ? UI_ACCENT : UI_INK);
    }
    if (model->numpad_error[0]) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 488, 450, 28},
                         model->numpad_error, 18, UI_DANGER);
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 518, 450, 24},
                     "Minus 切换小时/分钟", 18, UI_MUTED);
    draw_overlay_actions(pixels, stride, model, "+  完成输入");
}

static void masked_value(const char *value, bool revealed, char *out, size_t out_size)
{
    size_t length;
    if (revealed) {
        snprintf(out, out_size, "%s", value && value[0] ? value : "--");
        return;
    }
    length = value ? strlen(value) : 0U;
    snprintf(out, out_size, "%s", length ? "************************" : "--");
}

static void draw_credential_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect current_box;
    UiRect input_box = to_uirect(ptc_ui_credential_input_rect());
    char current[96];
    char next[96];
    bool valid;
    bool dirty;
    draw_dialog_shell(pixels, stride, model, &dialog, 900, 500);
    current_box = (UiRect){dialog.x + 42, dialog.y + 132, 600, 56};
    masked_value(model->credential_current, model->credential_kind == 1 || model->credential_revealed, current, sizeof(current));
    masked_value(model->credential_new, model->credential_kind == 1 || model->credential_new_revealed, next, sizeof(next));
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 122, "当前值", 17, UI_MUTED);
    fill_round_rect(pixels, stride, current_box, 16, UI_PAGE);
    if (model->credential_kind == 2 && model->credential_revealed) {
        if (strlen(current) > 32U) {
            char first[33];
            memcpy(first, current, 32U);
            first[32] = '\0';
            draw_text(pixels, stride, current_box.x + 16, current_box.y + 23, first, 14, UI_INK);
            draw_text(pixels, stride, current_box.x + 16, current_box.y + 44, current + 32, 14, UI_INK);
        } else {
            draw_text(pixels, stride, current_box.x + 16, current_box.y + 36, current, 14, UI_INK);
        }
    } else {
        draw_text(pixels, stride, current_box.x + 16, current_box.y + 36, current, 18, UI_INK);
    }
    if (model->credential_kind == 2) {
        draw_candidate_button(pixels, stride, ptc_ui_credential_reveal_rect(),
                              model->credential_revealed ? "ZR  隐藏当前密钥" : "ZR  显示当前密钥",
                              UI_PAGE, UI_ACCENT,
                              model->overlay_selection == PTC_UI_CREDENTIAL_REVEAL, false);
    }
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 215, "新值", 17, UI_MUTED);
    fill_round_rect(pixels, stride, input_box, 12, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, input_box, 12, model->overlay_selection == PTC_UI_CREDENTIAL_INPUT ? 3 : 1, model->overlay_selection == PTC_UI_CREDENTIAL_INPUT ? UI_ACCENT : UI_CONTROL);
    if (model->overlay_selection == PTC_UI_CREDENTIAL_INPUT) {
        draw_text(pixels, stride, input_box.x + input_box.width - 72, input_box.y + 20, "A / X", 14, UI_ACCENT);
    }
    if (model->credential_kind == 2 && model->credential_new_revealed) {
        if (strlen(next) > 32U) {
            char first[33];
            memcpy(first, next, 32U);
            first[32] = '\0';
            draw_text(pixels, stride, input_box.x + 16, input_box.y + 25, first, 14, UI_INK);
            draw_text(pixels, stride, input_box.x + 16, input_box.y + 48, next + 32, 14, UI_INK);
        } else {
            draw_text(pixels, stride, input_box.x + 16, input_box.y + 40, next, 14, UI_INK);
        }
    } else {
        draw_text(pixels, stride, input_box.x + 16, input_box.y + 40, next, 18, UI_INK);
    }
    draw_candidate_button(pixels, stride, ptc_ui_credential_random_rect(), "Y  随机生成",
                          UI_PAGE, UI_ACCENT,
                          model->overlay_selection == PTC_UI_CREDENTIAL_RANDOM, false);
    if (model->credential_kind == 2) {
        draw_candidate_button(pixels, stride, ptc_ui_credential_demo_rect(),
                              model->demo_secret_enabled ? "R  退出演示并换新密钥" : "R  使用公共演示密钥",
                              model->demo_secret_enabled ? UI_PAGE : UI_DANGER_SOFT,
                              model->demo_secret_enabled ? UI_ACCENT : UI_DANGER,
                              model->overlay_selection == PTC_UI_CREDENTIAL_DEMO, false);
        draw_text(pixels, stride, dialog.x + 332, dialog.y + 350,
                  "建议使用随机生成；手工密钥至少 32 个字符。", 17, UI_MUTED);
    }
    valid = model->credential_kind == 1
        ? ptc_device_id_valid(model->credential_new)
        : ptc_grant_secret_valid(model->credential_new);
    dirty = strcmp(model->credential_current, model->credential_new) != 0;
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                          dirty ? "+  保存" : "+  没有修改",
                          UI_ACCENT, UI_ON_ACCENT,
                          model->overlay_selection == PTC_UI_CREDENTIAL_SAVE, !valid || !dirty);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 406,
              "方向键选择  |  A 确定  |  X 手工输入  |  + 保存", 16, UI_MUTED);
}

static void draw_code_result_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell_model = *model;
    char before_value[48];
    char after_value[48];
    char actual_value[48];
    bool remaining_fresh = !model->code_result_failed && ptc_ui_status_is_fresh(model, (int64_t)time(NULL));
    format_duration(model->code_actual_add_minutes, actual_value, sizeof(actual_value));
    if (model->code_result_pending) {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "加时结果确认中");
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "已恢复上次确认的兑换请求，正在读取最终结果；请勿重复输入这枚加时码。");
    } else if (model->code_result_failed) {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "兑换未成功");
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "%s", ptc_ui_code_failure_guidance(model->error_code));
    } else {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "加时成功");
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "该加时码已经使用，不能再次使用。");
    }
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 760, 420);
    if (model->code_before_unlimited) snprintf(before_value, sizeof(before_value), "不限时");
    else format_duration(model->code_before_remaining_available ? model->code_before_remaining_minutes : -1,
                         before_value, sizeof(before_value));
    if (model->code_result_pending) {
        format_duration(model->code_preview_after_available ? model->code_preview_after_minutes : -1,
                        after_value, sizeof(after_value));
    } else if (!remaining_fresh) snprintf(after_value, sizeof(after_value), "状态待确认");
    else if (model->unrestricted_today == 1) snprintf(after_value, sizeof(after_value), "不限时");
    else format_duration(model->remaining_available ? model->remaining_minutes : -1,
                         after_value, sizeof(after_value));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 142, 300, 92},
                         model->code_result_pending || model->code_result_failed ? "兑换前" : "实际增加",
                         model->code_result_pending || model->code_result_failed ? before_value :
                         (model->code_actual_add_available ? actual_value : "暂不可用"),
                         UI_RGB(g_palette->text_primary));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 406, dialog.y + 142, 300, 92},
                         model->code_result_pending ? "预览兑换后" : (model->code_result_failed ? "上次剩余读数" : "兑换后剩余"), after_value,
                         time_state_accent(model->code_result_pending ? model->code_preview_after_available :
                                           (remaining_fresh && (model->unrestricted_today == 1 || model->remaining_available)),
                                           model->code_result_pending ? false : model->unrestricted_today == 1,
                                           model->code_result_pending ? model->code_preview_after_minutes : model->remaining_minutes));
    if (!model->code_result_pending) {
        char age[80];
        format_status_age(model, age, sizeof(age));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 312, 652, 24}, age, 18, status_age_color(model));
    }
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54}, 16, model->code_result_pending ? UI_RGB(g_palette->surface_raised) :
                    (model->code_result_failed ? UI_DANGER_SOFT : UI_SUCCESS_SOFT));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                     model->code_result_pending ? "结果确认期间可关闭；下次打开会继续确认" :
                     (model->code_result_failed ? "本次未消费代码；已使用或过期的代码仍不可用" : (model->code_actual_add_available && model->code_actual_add_minutes < model->code_grant_minutes
                         ? "已到每日上限，实际增加少于代码时长" :
                         (model->code_actual_add_available ? "兑换结果已确认并保存" : "成功已确认；加时明细暂不可核对，请查看使用记录"))),
                     19, model->code_result_pending ? UI_RGB(g_palette->text_secondary) :
                     (model->code_result_failed ? UI_DANGER : UI_SUCCESS));
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回孩子区",
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), model->code_result_pending ? "A  关闭" : "A  完成",
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_auth_error_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell_model = *model;
    char retry_label[64];
    snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "%s",
             model->auth_error_title[0] ? model->auth_error_title : "PIN 验证未通过");
    snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body), "%s",
             model->auth_error_message[0] ? model->auth_error_message : "PIN 不正确，请重试。");
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 720, 340);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 44, dialog.y + 142, dialog.width - 88, 72}, 16, UI_DANGER_SOFT);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 58, dialog.y + 142, dialog.width - 116, 72},
                     model->auth_cooldown_seconds > 0
                        ? "错误次数过多，倒计时结束后才能重试"
                        : "错误 PIN 不会保留；重新输入时输入框为空",
                     18, UI_DANGER);
    if (model->auth_cooldown_seconds > 0) {
        snprintf(retry_label, sizeof(retry_label), "请等待 %d 秒", model->auth_cooldown_seconds);
    } else {
        snprintf(retry_label, sizeof(retry_label), "A  重新输入");
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  取消",
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), retry_label,
                       model->auth_cooldown_seconds > 0 ? UI_BORDER : UI_ACCENT,
                       model->auth_cooldown_seconds > 0 ? UI_MUTED : UI_ON_ACCENT, false);
}

static void draw_grant_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char fitted[192];
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 650);
    for (index = 0; index < PTC_UI_GRANT_MANAGER_COUNT; ++index) {
        draw_action_card(
            pixels,
            stride,
            to_uirect(ptc_ui_grant_manager_card_rect(index)),
            &GRANT_MANAGER_ACTIONS[index],
            model->overlay_selection == index,
            PTC_UI_ACTION_AVAILABLE,
            0);
    }
    fit_text(fitted, sizeof(fitted), model->message, 16, dialog.width - 330);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 506,
              fitted[0] ? fitted : "方向键选择  |  A 确定  |  B 返回加时码", 16,
              fitted[0] ? UI_SUCCESS : UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
}

static void draw_redemption_history_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char page_text[64];
    int pages = ptc_ui_redemption_history_page_count(model);
    int first = model->redemption_history_page * 6;
    int visible = model->redemption_history_count - first;
    int row;
    if (visible > 6) visible = 6;
    if (visible < 0) visible = 0;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 650);
    snprintf(page_text, sizeof(page_text), "最新优先  |  第 %d/%d 页  |  共 %d 条",
             model->redemption_history_page + 1, pages, model->redemption_history_count);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 104, page_text, 16, UI_MUTED);
    if (!model->redemption_history_available) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
                         "暂时无法读取使用记录；可重试，或验证 PIN 后清空损坏记录。",
                         19, UI_DANGER);
    } else if (model->redemption_history_count == 0) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
                         "暂无成功使用记录；升级前的兑换不会回填。",
                         19, UI_MUTED);
    } else {
        for (row = 0; row < visible; ++row) {
            int source = model->redemption_history_count - 1 - first - row;
            const PtcRedemptionHistoryRecord *record = &model->redemption_history[source];
            UiRect item = {dialog.x + 34, dialog.y + 132 + row * 62, dialog.width - 68, 54};
            char time_text[48];
            char allowance[160];
            char remaining[80];
            format_event_time(record->redeemed_at, true, time_text, sizeof(time_text));
            if (record->remaining_after_available) {
                char value[48];
                format_duration((int)record->remaining_after_minutes, value, sizeof(value));
                snprintf(remaining, sizeof(remaining), "兑换后 %s", value);
            } else {
                snprintf(remaining, sizeof(remaining), "兑换后暂不可用");
            }
            snprintf(allowance, sizeof(allowance), "代码 %u 分钟  |  实际计入 %u 分钟%s",
                     (unsigned int)record->grant_minutes,
                     (unsigned int)record->effective_add_minutes,
                     record->effective_add_minutes < record->grant_minutes ? "（已到每日上限）" : "");
            fill_round_rect(pixels, stride, item, 16, UI_RAISED);
            draw_rect_outline(pixels, stride, item, 16, 1, UI_BORDER);
            draw_text(pixels, stride, item.x + 16, item.y + 20, time_text, 15, UI_INK);
            draw_text(pixels, stride, item.x + 250, item.y + 20, allowance, 15, UI_ACCENT);
            draw_text(pixels, stride, item.x + 720, item.y + 20, remaining, 15, UI_INK);
            draw_text(pixels, stride, item.x + item.width - 84, item.y + 20,
                      record->token_version == 2u ? "v2 成功" : "v1 成功", 14, UI_SUCCESS);
        }
    }
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_prev_rect(), "L / 左  上一页",
                       UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_next_rect(), "R / 右  下一页",
                       UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "X  清空全部",
                       UI_DANGER_SOFT, UI_DANGER, false);
}

static const char *activity_label(const char *action)
{
    if (strcmp(action, "today_limit") == 0) return "修改今日总额度";
    if (strcmp(action, "today_add") == 0) return "家长临时加时";
    if (strcmp(action, "today_unlimited") == 0) return "今日改为不限时";
    if (strcmp(action, "today_restore") == 0) return "恢复今日计划";
    if (strcmp(action, "weekly_update") == 0) return "修改周计划";
    if (strcmp(action, "holiday_update") == 0) return "修改节假日规则";
    if (strcmp(action, "scheduled_update") == 0) return "修改日期计划";
    if (strcmp(action, "autonomy_update") == 0) return "修改自主缓冲";
    if (strcmp(action, "offline_grant") == 0) return "兑换加时码";
    if (strcmp(action, "daily_buffer") == 0) return "领取自主缓冲";
    if (strcmp(action, "protection") == 0) return "保护事件";
    return "活动记录";
}

static void draw_activity_history_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    int pages = ptc_ui_activity_history_page_count(model);
    int first = model->activity_history_page * 8;
    int visible = model->activity_history_count - first;
    int row;
    char line[160];
    if (visible > 8) visible = 8;
    if (visible < 0) visible = 0;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 650);
    snprintf(line, sizeof(line), "最新优先  |  第 %d/%d 页  |  共 %d 条  |  不含 PIN、密钥、完整代码或 nonce",
        model->activity_history_page + 1, pages, model->activity_history_count);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 104, line, 15, UI_MUTED);
    if (!model->activity_history_available) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
            "家庭活动记录暂不可用；控制功能不会因此中断。", 19, UI_DANGER);
    } else if (model->activity_history_count == 0) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
            "暂无家庭活动记录。", 19, UI_MUTED);
    } else {
        for (row = 0; row < visible; ++row) {
            int source = model->activity_history_count - 1 - first - row;
            const PtcActivityHistoryRecord *record = &model->activity_history[source];
            UiRect item = {dialog.x + 34, dialog.y + 130 + row * 45, dialog.width - 68, 38};
            char time_text[48];
            format_event_time(record->occurred_at, true, time_text, sizeof(time_text));
            snprintf(line, sizeof(line), "%s  |  %s  |  计划 %u 分钟，实际 %u 分钟",
                time_text, activity_label(record->action), (unsigned int)record->minutes,
                (unsigned int)record->effective_minutes);
            fill_round_rect(pixels, stride, item, 6, UI_RAISED);
            draw_text(pixels, stride, item.x + 14, item.y + 24, line, 14,
                strcmp(record->action, "protection") == 0 ? UI_DANGER : UI_INK);
        }
    }
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_prev_rect(), "L / 左  上一页",
        UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_next_rect(), "R / 右  下一页",
        UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "X  清空全部",
        UI_DANGER_SOFT, UI_DANGER, false);
}

static void draw_scheduled_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcScheduledOverride *draft = &model->draft_scheduled_override;
    uint32_t duration = draft->end_day_index >= draft->start_day_index
        ? (uint32_t)draft->end_day_index - draft->start_day_index + 1u : 1u;
    uint16_t year; uint8_t month, day;
    char values[4][128];
    char end_date[80];
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 640);
    snprintf(values[0], sizeof(values[0]), "日期计划：%s", draft->enabled ? "开启" : "关闭");
    if (ptc_date_from_day_index(draft->start_day_index, &year, &month, &day))
        snprintf(values[1], sizeof(values[1]), "开始日期：%04u-%02u-%02u%s", year, month, day,
                 model->status_loaded && draft->start_day_index == model->day_index ? "  今天" : "");
    else snprintf(values[1], sizeof(values[1]), "开始日期暂不可用");
    snprintf(values[2], sizeof(values[2]), "持续：%u 天  (1 到 366 天)", (unsigned int)duration);
    if (draft->rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(values[3], sizeof(values[3]), "每天额度：不限时");
    else snprintf(values[3], sizeof(values[3]), "每天额度：%u 分钟", (unsigned int)draft->rule.minutes);
    for (int index = 0; index < 4; ++index) {
        UiRect row = to_uirect(ptc_ui_scheduled_field_rect(index));
        draw_plan_card(pixels, stride, row, model->overlay_selection == index);
        draw_text(pixels, stride, row.x + 18, row.y + 38, values[index], 21, UI_RGB(g_palette->text_primary));
    }
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_SCHEDULED, ptc_ui_scheduled_dirty(model),
                     (UiRect){dialog.x + 626, dialog.y + 116, 460, 306});
    if (ptc_date_from_day_index(draft->end_day_index, &year, &month, &day))
        snprintf(end_date, sizeof(end_date), "结束日期：%04u-%02u-%02u，包含当天", year, month, day);
    else snprintf(end_date, sizeof(end_date), "请检查结束日期");
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 448, end_date, 18, UI_RGB(g_palette->text_secondary));
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 482,
              "上下选择，左右调整；X 切换状态或模式；ZL/ZR 大步调整", 18, UI_RGB(g_palette->text_secondary));
    if (strcmp(model->result_status, "error") == 0)
        draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 516,
                          "上次操作未完成，草稿仍保留。可重试，或放弃草稿后到支持与恢复检查。",
                          18, dialog.width - 68, 24, 2, UI_RGB(g_palette->danger));
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回", UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       model->waiting ? "正在保存" : (ptc_ui_scheduled_dirty(model) ? "+  保存草稿" : "已保存"),
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_scheduled_leave(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel copy = *model;
    snprintf(copy.overlay_title, sizeof(copy.overlay_title), "放弃日期计划草稿？");
    snprintf(copy.overlay_body, sizeof(copy.overlay_body), "尚未保存的修改会丢失，已保存的计划不变。");
    draw_dialog_shell(pixels, stride, &copy, &dialog, 720, 300);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  继续编辑",
                       UI_RAISED, UI_ACCENT, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  放弃草稿",
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_autonomy_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    static const uint16_t OPTIONS[] = {0, 5, 10, 15};
    int index;
    char label[48];
    draw_dialog_shell(pixels, stride, model, &dialog, 760, 420);
    for (index = 0; index < 4; ++index) {
        UiRect option = to_uirect(ptc_ui_autonomy_option_rect(index));
        bool selected = model->draft_autonomy_policy.daily_buffer_minutes == OPTIONS[index];
        if (OPTIONS[index] > 0u) {
            snprintf(label, sizeof(label), "%u 分钟", (unsigned int)OPTIONS[index]);
        } else {
            snprintf(label, sizeof(label), "关闭");
        }
        fill_round_rect(pixels, stride, option, 12, selected ? UI_SUCCESS_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 12, selected ? 2 : 1, selected ? UI_SUCCESS : UI_BORDER);
        draw_text_center(pixels, stride, option, label, 21,
            selected ? UI_SUCCESS : UI_INK);
    }
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 284,
        "孩子每天仅可领取一次，只在限时日可用；失败不会消耗领取资格。",
        16, UI_MUTED);
    draw_overlay_actions(pixels, stride, model, "+  保存缓冲设置");
}

static void draw_shortcut_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char status[192];
    char shortcut_hint[160];
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 650);
    for (index = 0; index < PTC_UI_SHORTCUT_PRESET_COUNT; ++index) {
        UiRect option = to_uirect(ptc_ui_shortcut_option_rect(index));
        bool selected = index == model->setup_shortcut_index;
        bool chosen = model->shortcut_draft_enabled && selected;
        fill_round_rect(pixels, stride, option, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 12, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
        draw_text(pixels, stride, option.x + 14, option.y + 23,
                  ptc_ui_shortcut_common_label(index), 16,
                  selected ? UI_ACCENT : UI_INK);
        if (chosen) draw_text(pixels, stride, option.x + option.width - 74, option.y + 23,
                              "待保存", 15, UI_SUCCESS);
    }
    draw_dialog_button(pixels, stride, ptc_ui_shortcut_disable_rect(),
                       "ZL  关闭自定义快捷键", UI_DANGER_SOFT, UI_DANGER, true);
    draw_dialog_button(pixels, stride, ptc_ui_shortcut_hint_rect(),
                       model->shortcut_draft_show_hint ? "Y  孩子区提示：显示" : "Y  孩子区提示：隐藏",
                       UI_PAGE, UI_INK, true);
    ptc_ui_format_custom_shortcut_hint(model->shortcut_draft_label, shortcut_hint, sizeof(shortcut_hint));
    snprintf(status, sizeof(status), "%s",
             model->shortcut_draft_enabled ? shortcut_hint : "自定义入口已关闭；固定 Minus 松开即可进入，无需长按");
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 516, status, 18,
              model->shortcut_draft_enabled ? UI_SUCCESS : UI_DANGER);
    draw_overlay_actions(pixels, stride, model, "+  确认保存");
}

static void draw_grant_local_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *ADJUST[] = {"上一档", "下一档", "L -15", "R +15", "ZL -30", "ZR +30"};
    UiRect dialog;
    char line[256], remaining[48], estimate[48], code[16], freshness[80];
    bool capped = false;
    bool reliable = ptc_ui_status_is_fresh(model, (int64_t)time(NULL)) && !model->grant_status_refresh_failed;
    int expected = reliable ? ptc_ui_grant_estimate_remaining(model, model->grant_minutes, &capped) : -1;
    uint16_t year; uint8_t month, day;
    PtcUiModel shell = *model;
    snprintf(shell.overlay_body, sizeof(shell.overlay_body), "选时长、生成，再把代码告诉孩子。实际增加以兑换结果为准。");
    draw_dialog_shell(pixels, stride, &shell, &dialog, 920, 650);
    if (reliable && model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), "不限时");
    else format_duration(reliable && model->remaining_available ? model->remaining_minutes : -1, remaining, sizeof(remaining));
    format_duration(expected, estimate, sizeof(estimate));
    format_status_age(model, freshness, sizeof(freshness));
    if (model->grant_status_refresh_failed) snprintf(freshness, sizeof(freshness), "刷新失败，返回后刷新再试");
    snprintf(line, sizeof(line), "今天还可玩 %s  |  %s", remaining, freshness);
    if (model->grant_notice[0]) snprintf(line, sizeof(line), "%s", model->grant_notice);
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 126, line, 18, UI_RGB(g_palette->text_secondary));

    fill_round_rect(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 144, 852, 192}, 16, UI_RGB(g_palette->surface_raised));
    draw_text(pixels, stride, dialog.x + 54, dialog.y + 176, "下次加时时长", 20, UI_RGB(g_palette->text_secondary));
    char played[48];
    format_duration(reliable && model->played_minutes_available ? model->played_minutes : -1, played, sizeof(played));
    snprintf(line, sizeof(line), "额度已耗（估算）%s", played);
    draw_text(pixels, stride, dialog.x + 420, dialog.y + 176, line, 18, UI_RGB(g_palette->text_secondary));
    snprintf(line, sizeof(line), "%u 分钟", (unsigned)model->grant_minutes);
    draw_text(pixels, stride, dialog.x + 54, dialog.y + 218, line, 32, UI_RGB(g_palette->text_primary));
    snprintf(line, sizeof(line), "兑换后预计 %s%s", estimate, capped ? "（已到每日上限）" : "");
    draw_text(pixels, stride, dialog.x + 260, dialog.y + 215, line, 20, UI_RGB(g_palette->text_secondary));
    for (int i = 0; i < 6; ++i)
        draw_candidate_button(pixels, stride, ptc_ui_grant_adjust_rect(i), ADJUST[i],
            UI_RGB(g_palette->surface), UI_RGB(g_palette->text_primary), model->overlay_selection == i, false);
    draw_text(pixels, stride, dialog.x + 54, dialog.y + 320, "调整这里不会改变已生成代码的时长，也不会撤销旧码。", 18, UI_RGB(g_palette->text_secondary));

    fill_round_rect(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 350, 852, 170}, 16, UI_RGB(g_palette->surface_raised));
    draw_text(pixels, stride, dialog.x + 54, dialog.y + 382, "已生成代码", 20, UI_RGB(g_palette->text_secondary));
    if (model->grant_has_code) {
        ptc_ui_format_code(model->grant_code, code, sizeof(code));
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 432, code, 42, UI_RGB(g_palette->text_primary));
        snprintf(line, sizeof(line), "代码时长 %u 分钟", (unsigned)model->grant_issued_minutes);
        draw_text(pixels, stride, dialog.x + 420, dialog.y + 426, line, 25, UI_RGB(g_palette->text_primary));
        format_duration(model->grant_estimate_available ? model->grant_estimate_minutes : -1, estimate, sizeof(estimate));
        snprintf(line, sizeof(line), "生成时预计剩余 %s%s", estimate, model->grant_estimate_capped ? "（已到每日上限）" : "");
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 466, line, 18, UI_RGB(g_palette->text_secondary));
        if (ptc_date_from_day_index(model->grant_day_index, &year, &month, &day))
            snprintf(line, sizeof(line), "%u-%02u-%02u 有效，成功兑换后仅可使用一次", (unsigned)year, (unsigned)month, (unsigned)day);
        else snprintf(line, sizeof(line), "签发日期待确认，请返回后刷新状态");
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 499, line, 18, UI_RGB(g_palette->text_secondary));
    } else {
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 430, "选好时长后，按 + 生成", 28, UI_RGB(g_palette->text_primary));
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 471, "生成前会再次验证 PIN；同日已签发的其他代码仍可能可用。", 18, UI_RGB(g_palette->text_secondary));
    }
    draw_candidate_button(pixels, stride, ptc_ui_grant_generate_rect(),
        model->grant_has_code ? "+  再生成一个" : "+  生成加时码", UI_ACCENT, UI_ON_ACCENT,
        model->overlay_selection == PTC_UI_GRANT_LOCAL_GENERATE, model->waiting);
    draw_candidate_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
        UI_RGB(g_palette->surface_raised), UI_RGB(g_palette->text_primary), model->overlay_selection == PTC_UI_GRANT_LOCAL_BACK, false);
    draw_text(pixels, stride, dialog.x + 280, dialog.y + 615, "方向键选择  |  A 确定  |  L/R、ZL/ZR 调整", 18, UI_RGB(g_palette->text_secondary));
}

static void draw_qr_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    int size = qrcodegen_getSize(model->qr_code);
    int scale = size > 0 ? 350 / (size + 8) : 1;
    int total;
    int origin_x;
    int origin_y;
    int next_y;
    int x;
    int y;
    if (scale < 2) scale = 2;
    total = (size + 8) * scale;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 650);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 142, "推荐方案一：联网扫码", 23, UI_SUCCESS);
    origin_x = dialog.x + 34;
    origin_y = dialog.y + 164;
    /* QR polarity is functional and intentionally bypasses the active theme. */
    fill_rect_packed(pixels, stride, (UiRect){origin_x, origin_y, total, total}, pack_rgb(0xFFFFFF));
    for (y = 0; y < size; ++y) {
        for (x = 0; x < size; ++x) {
            if (qrcodegen_getModule(model->qr_code, x, y)) {
                fill_rect_packed(pixels, stride,
                                 (UiRect){origin_x + (x + 4) * scale, origin_y + (y + 4) * scale, scale, scale},
                                 pack_rgb(0x000000));
            }
        }
    }
    
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 530,
              "仅在网页可访问时扫码；地址：", 14, UI_MUTED);
    next_y = draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 552,
                               model->pairing_base_url, 13, 400, 19, 3, UI_ACCENT);
    draw_text(pixels, stride, dialog.x + 34, next_y + 4,
              "二维码包含加时码密钥，请仅由家长使用。", 14, UI_DANGER);

    draw_text(pixels, stride, dialog.x + 470, dialog.y + 142, "备用方案二：单文件离线版", 23, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 178,
              "1. 解压完整交付包，取得 playwise-offline.html", 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 208,
              "2. 返回“加时码生成管理”，导出手机/电脑配置", 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 488, dialog.y + 234,
              PLAYWISE_SD_ROOT "/parent-import.json", 15, UI_ACCENT);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 266,
              "3. 将 HTML 和配置文件传到可信的手机或电脑", 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 298,
              "4. 用系统浏览器打开 HTML，再点击“导入配置文件”", 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 330,
              "5. 选择 parent-import.json，再点击“导入此设备”", 15, UI_INK);
    next_y = dialog.y + 362;
    draw_text(pixels, stride, dialog.x + 470, next_y + 6,
              "日常生成无需网络，也无需安装应用或本地服务器。", 14, UI_SUCCESS);
    draw_text(pixels, stride, dialog.x + 470, next_y + 32,
              "手机请先保存文件，再交给系统浏览器打开；", 14, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 470, next_y + 58,
              "不要使用聊天软件或网盘的内置预览器。", 14, UI_MUTED);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 470, next_y + 74, 610, 48}, 16, UI_DANGER_SOFT);
    draw_text(pixels, stride, dialog.x + 486, next_y + 104,
              "配置文件包含加时码密钥，请勿发送给他人。", 15, UI_DANGER);

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
}

static void draw_weekly_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    bool refreshing = strcmp(model->overlay_title, "刷新周计划？") == 0;
    bool disabled = model->disable_flag_present;
    draw_dialog_shell(pixels, stride, model, &dialog, 860, 350);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 150, dialog.width - 80, 34},
                     "周计划还有未保存的修改", 22, UI_WARNING);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 190, dialog.width - 80, 24},
                     "左右选择  |  A 确认  |  也可直接使用按钮快捷键", 17, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_discard_rect(model->overlay),
                       refreshing ? "X  放弃并刷新" : "X  放弃并离开",
                       UI_DANGER_SOFT, UI_DANGER, true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), disabled ? "B  返回" : "B  继续编辑",
                        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       disabled
                         ? (refreshing ? "+  保留草稿并刷新" : "+  保留草稿并离开")
                         : (refreshing ? "+  保存并刷新" : "+  保存并离开"),
                       UI_ACCENT, UI_ON_ACCENT, false);
    if (model->weekly_leave_selection == 0) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_discard_rect(model->overlay)), 12, 3, UI_DANGER);
    } else if (model->weekly_leave_selection == 1) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_cancel_rect(model->overlay)), 12, 3, UI_ACCENT);
    } else {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_confirm_rect(model->overlay)), 12, 3, UI_SURFACE);
    }
}

static void draw_credential_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 720, 300);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 132, dialog.width - 72, 30},
                     "左右选择  |  A 确定  |  B 继续编辑", 17, UI_MUTED);
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay), "X  放弃修改",
                          UI_DANGER_SOFT, UI_DANGER,
                          model->overlay_selection == 0, false);
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  继续编辑",
                          UI_PAGE, UI_ACCENT,
                          model->overlay_selection == 1, false);
}

static void draw_software_info_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect details;
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 480);
    details = (UiRect){dialog.x + 34, dialog.y + 126, dialog.width - 68, 224};
    fill_round_rect(pixels, stride, details, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, details, 16, 1, UI_BORDER);
    draw_text(pixels, stride, details.x + 24, details.y + 42, "软件名称", 18, UI_MUTED);
    draw_text(pixels, stride, details.x + 180, details.y + 42, "PlayWise（任我玩）", 20, UI_INK);
    draw_text(pixels, stride, details.x + 24, details.y + 84, "当前版本", 18, UI_MUTED);
    draw_text(pixels, stride, details.x + 180, details.y + 84, model->software_version, 20, UI_ACCENT);
    draw_text(pixels, stride, details.x + 24, details.y + 126, "项目仓库", 18, UI_MUTED);
    draw_text(pixels, stride, details.x + 180, details.y + 126, model->repository_url, 18, UI_ACCENT);
    draw_text(pixels, stride, details.x + 24, details.y + 168, "家长网页", 18, UI_MUTED);
    draw_text(pixels, stride, details.x + 180, details.y + 168, model->pwa_url, 18, UI_SUCCESS);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  关闭",
                       UI_ACCENT, UI_ON_ACCENT, false);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 438, "也可按 B 返回", 16, UI_MUTED);
}

static bool holiday_is_past_or_today(
    const PtcHolidayArrangement *entry,
    uint16_t cur_year, uint8_t cur_month, uint8_t cur_day,
    bool *is_past_out, bool *is_today_out)
{
    *is_past_out = false;
    *is_today_out = false;
    if (!entry) return false;
    uint8_t last_month = entry->end_month;
    uint8_t last_day = entry->end_day;
    if (entry->makeup_workdays && strcmp(entry->makeup_workdays, "无") != 0) {
        const char *p = entry->makeup_workdays;
        while (*p) {
            if (*p >= '0' && *p <= '9') {
                unsigned int m = 0, d = 0;
                if (sscanf(p, "%u月%u日", &m, &d) == 2) {
                    if (m > last_month || (m == last_month && d > last_day)) {
                        last_month = (uint8_t)m;
                        last_day = (uint8_t)d;
                    }
                }
                const char *next = strstr(p, "日");
                if (next) {
                    p = next + strlen("日");
                    continue;
                }
            }
            p++;
        }
    }
    if (cur_year > entry->year ||
        (cur_year == entry->year && (cur_month > last_month || (cur_month == last_month && cur_day > last_day)))) {
        *is_past_out = true;
        return true;
    }
    if (cur_year == entry->year) {
        bool in_holiday = (cur_month > entry->start_month || (cur_month == entry->start_month && cur_day >= entry->start_day)) &&
                          (cur_month < entry->end_month || (cur_month == entry->end_month && cur_day <= entry->end_day));
        if (in_holiday) {
            *is_today_out = true;
        } else if (entry->makeup_workdays && strcmp(entry->makeup_workdays, "无") != 0) {
            char today_str[32];
            snprintf(today_str, sizeof(today_str), "%u月%u日", cur_month, cur_day);
            if (strstr(entry->makeup_workdays, today_str)) {
                *is_today_out = true;
            }
        }
    }
    return true;
}

static void draw_holiday_calendar_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
    size_t count = ptc_holiday_calendar_arrangement_count(info->last_year);
    const int per_page = 4;
    int pages = (int)((count + per_page - 1) / per_page);
    int page = model->holiday_calendar_page;
    char line[192];
    uint16_t cur_year = 0;
    uint8_t cur_month = 0, cur_day = 0;
    bool has_date = model->status_loaded && ptc_date_from_day_index(model->day_index, &cur_year, &cur_month, &cur_day);
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 600);
    if (model->holiday_dirty) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + dialog.width - 330, dialog.y + 24, 290, 34}, 16, UI_WARNING_SOFT);
        draw_text_center(pixels, stride, (UiRect){dialog.x + dialog.width - 330, dialog.y + 24, 290, 34},
                         "预览尚未保存的设置", 16, UI_WARNING);
    }
    if (page < 0 || page >= pages) page = 0;
    for (int row = 0; row < per_page; ++row) {
        size_t index = (size_t)(page * per_page + row);
        const PtcHolidayArrangement *entry = ptc_holiday_calendar_arrangement(info->last_year, index);
        UiRect card = {dialog.x + 34, dialog.y + 146 + row * 82, dialog.width - 68, 70};
        if (!entry) break;
        bool is_past = false, is_today = false;
        if (has_date) {
            holiday_is_past_or_today(entry, cur_year, cur_month, cur_day, &is_past, &is_today);
        }
        uint32_t bg_color = is_today ? UI_ACCENT_SOFT : (is_past ? UI_SURFACE : UI_RAISED);
        uint32_t border_color = is_today ? UI_ACCENT : UI_BORDER;
        uint32_t title_color = is_today ? UI_ACCENT : (is_past ? UI_MUTED : UI_INK);
        uint32_t line_color = is_today ? UI_INK : (is_past ? UI_DISABLED : UI_MUTED);
        fill_round_rect(pixels, stride, card, 16, bg_color);
        draw_rect_outline(pixels, stride, card, 16, is_today ? 2 : 1, border_color);
        draw_text(pixels, stride, card.x + 20, card.y + 28, entry->display_name, 21, title_color);
        snprintf(line, sizeof(line), "放假：%u月%u日-%u月%u日    调休上班：%s",
                 entry->start_month, entry->start_day, entry->end_month, entry->end_day, entry->makeup_workdays);
        draw_text(pixels, stride, card.x + 150, card.y + 28, line, 18, line_color);
        UiRect badge = {card.x + card.width - 96, card.y + 21, 76, 28};
        if (is_today) {
            fill_round_rect(pixels, stride, badge, 6, UI_ACCENT);
            draw_text_center(pixels, stride, badge, "进行中", 14, UI_ON_ACCENT);
        } else if (is_past) {
            fill_round_rect(pixels, stride, badge, 6, UI_PAGE);
            draw_text_center(pixels, stride, badge, "已结束", 14, UI_MUTED);
        }
    }
    snprintf(line, sizeof(line), "%u 年  |  v%u  |  发布于 %s  |  来源：www.gov.cn    第 %d/%d 页",
             info->last_year, info->version, info->published_date, page + 1, pages);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 30, dialog.y + 480, dialog.width - 60, 30}, line, 16, UI_MUTED);
    for (int index = 0; index < 3; ++index) {
        bool disabled = (index == 0 && page == 0) || (index == 1 && page + 1 >= pages);
        const char *label = index == 0 ? "L  上一页" : (index == 1 ? "R  下一页" : "A / B  关闭");
        draw_candidate_button(pixels, stride, ptc_ui_holiday_page_action_rect(index), label,
                              index == 2 ? UI_ACCENT : UI_PAGE,
                              index == 2 ? UI_ON_ACCENT : UI_ACCENT,
                              model->overlay_selection == index, disabled);
    }
}

static void draw_weekly_bulk_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    UiRect dialog;
    char source[96];
    char line[128];
    PtcUiWeeklyBulkStats stats;
    int slot = model->weekly_last_day_slot >= 0 && model->weekly_last_day_slot < 7 ? model->weekly_last_day_slot : 0;
    int day = ptc_ui_weekday_for_display_slot(slot);
    PtcDayRule rule = model->draft_week[day];
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 560);
    if (rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(source, sizeof(source), "来源：%s，不限时", DAYS[day]);
    else snprintf(source, sizeof(source), "来源：%s，限时 %u 分钟", DAYS[day], (unsigned int)rule.minutes);
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 116, source, 19, UI_INK);
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 148, "1. 选择目标", 17, UI_MUTED);
    for (int index = 0; index < 2; ++index) {
        UiRect card = to_uirect(ptc_ui_weekly_bulk_target_rect(index));
        bool selected = model->overlay_selection == index;
        fill_round_rect(pixels, stride, card, 16, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, card, 16, selected ? 3 : 1, selected ? UI_ACCENT : UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 18, card.width, 34},
                         index == 0 ? "工作日" : "周末", 22, UI_INK);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 54, card.width, 28},
                         index == 0 ? "周一至周五" : "周六与周日", 17, UI_MUTED);
    }
    ptc_ui_weekly_bulk_stats(model, model->overlay_selection == 1, &stats);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 490, dialog.y + 148, 510, 270}, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, (UiRect){dialog.x + 490, dialog.y + 148, 510, 270}, 16, 1, UI_BORDER);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 180, "2. 覆盖预览", 20, UI_INK);
    snprintf(line, sizeof(line), "目标 %d 天；会改变 %d 天；相同跳过 %d 天",
             stats.target_count, stats.changed_count, stats.unchanged_count);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 218, line, 17, UI_ACCENT);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 256, "目标当前规则：", 16, UI_MUTED);
    for (int index = 0; index < stats.rule_group_count; ++index) {
        PtcDayRule group = stats.rule_groups[index].rule;
        if (group.mode == PTC_RULE_MODE_UNLIMITED) snprintf(line, sizeof(line), "不限时：%d 天", stats.rule_groups[index].count);
        else snprintf(line, sizeof(line), "限时 %u 分钟：%d 天", (unsigned int)group.minutes, stats.rule_groups[index].count);
        draw_text(pixels, stride, dialog.x + 536, dialog.y + 288 + index * 27, line, 16, UI_INK);
    }
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 424,
              "应用后只修改本次运行中的草稿，仍需回到周计划页面保存。", 16, UI_WARNING);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       stats.changed_count > 0 ? "A / +  应用到草稿" : "A / +  无需修改",
                       stats.changed_count > 0 ? UI_ACCENT : UI_DISABLED,
                       UI_ON_ACCENT, false);
}

static void draw_album_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const char *state = model->album_restriction_state == 0 ? "未开启" :
                        model->album_restriction_state == 1 ? "已开启" :
                        model->album_restriction_state == 2 ? "需要处理" :
                        model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? "外部已配置" : "状态未知";
    draw_dialog_shell(pixels, stride, model, &dialog, 980, 560);
    draw_text(pixels, stride, dialog.x + 38, dialog.y + 126, "当前状态", 17, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 136, dialog.y + 126, state, 19,
              model->album_restriction_state == 1 ? UI_SUCCESS :
              model->album_restriction_state == 2 ? UI_WARNING :
              model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? UI_ACCENT : UI_MUTED);
    draw_candidate_button(pixels, stride, ptc_ui_album_refresh_rect(), "Y  重新检测",
                          UI_PAGE, UI_ACCENT, false, false);
    for (int index = 0; index < 2; ++index) {
        UiRect card = to_uirect(ptc_ui_album_action_rect(index));
        bool selected = model->overlay_selection == index;
        bool enabled = index == 0 ? model->album_restriction_state == 0 :
                       (model->album_restriction_state == 1 ||
                        (model->album_restriction_state == 2 && model->album_backup_valid));
        fill_round_rect(pixels, stride, card, 16, enabled && selected ? UI_ACCENT_SOFT :
                        (enabled ? UI_SURFACE : UI_PAGE));
        draw_rect_outline(pixels, stride, card, 16, enabled && selected ? 3 : 1, enabled && selected ? UI_ACCENT : UI_BORDER);
        draw_text(pixels, stride, card.x + 24, card.y + 42,
                  index == 0 ? (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL
                                  ? "无需重复配置" : "配置自制程序菜单高级入口") :
                  (model->album_restriction_state == 2 && model->album_backup_valid ? "强制恢复可信备份" : "恢复原来的启动方式"),
                  21, enabled ? UI_INK : UI_DISABLED);
        draw_wrapped_text(pixels, stride, card.x + 24, card.y + 84,
                          index == 0
                            ? (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL
                                ? "当前磁盘配置已经提供相同入口。PlayWise 从未修改它，因此不会伪造安装前备份或声称可以恢复。"
                                : "先完整备份相关配置；重启后，在桌面‘手柄设置’图标上按住 X，再按 A，进入自制程序菜单（hbmenu）。")
                            : "按可信备份恢复原配置。卸载或删除 PlayWise 数据前必须完成恢复；外部修改不会被静默覆盖。",
                          16, card.width - 48, 25, 5, enabled ? UI_MUTED : UI_DISABLED);
        draw_text(pixels, stride, card.x + 24, card.y + 194,
                  enabled ? "A  继续" : "当前状态不可用", 16,
                  enabled ? UI_ACCENT : UI_DISABLED);
    }
    if (model->album_restriction_detail[0]) {
        draw_wrapped_text(pixels, stride, dialog.x + 38, dialog.y + 432,
                          model->album_restriction_detail, 14, dialog.width - 76, 20, 2, UI_MUTED);
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
}

static void draw_theme_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *LABELS[] = {"跟随系统", "浅色", "暗色"};
    static const char *DETAILS[] = {"随 Switch 设置", "清爽的浅色背景", "柔和的深色背景"};
    UiRect dialog;
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 820, 360);
    for (index = 0; index < 3; ++index) {
        UiRect option = to_uirect(ptc_ui_theme_option_rect(index));
        bool selected = index == model->overlay_selection;
        fill_round_rect(pixels, stride, option, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 12, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
        draw_text_center(pixels, stride, (UiRect){option.x, option.y + 14, option.width, 34},
                         LABELS[index], 22, UI_INK);
        draw_text_center(pixels, stride, (UiRect){option.x, option.y + 52, option.width, 26},
                         DETAILS[index], 15, UI_MUTED);
    }
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 294,
              !g_theme.system_theme_available && model->overlay_selection == PTC_UI_THEME_SYSTEM
                  ? "系统主题暂不可用，将安全回退为浅色。" : "方向键选择  |  A 立即应用并保存  |  B 取消",
              16, !g_theme.system_theme_available ? UI_WARNING : UI_MUTED);
}

static void draw_holiday_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 720, 320);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 148, dialog.width - 72, 28},
                     "离开后将丢失尚未保存的节假日设置", 19, UI_WARNING);
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay), "X  放弃更改",
                          UI_DANGER_SOFT, UI_DANGER, model->holiday_leave_selection == 0, false);
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  继续编辑",
                          UI_PAGE, UI_ACCENT, model->holiday_leave_selection == 1, false);
}

static void draw_support_event_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    int index = model->overlay_selection;
    char time_text[48];
    char row[224];
    int y;
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 560);
    if (index < 0 || index >= model->recent_event_count) index = 0;
    format_event_time(model->recent_event_timestamps[index], true, time_text, sizeof(time_text));
    y = dialog.y + 132;
    snprintf(row, sizeof(row), "事件：%s", model->recent_event_names[index][0] ? model->recent_event_names[index] : "未知");
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y, row, 18, dialog.width - 84, 28, 2, UI_INK);
    snprintf(row, sizeof(row), "操作类型：%s", model->recent_event_types[index][0] ? model->recent_event_types[index] : "未记录");
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2, UI_MUTED);
    snprintf(row, sizeof(row), "结果：%s", model->recent_event_errors[index][0] ? model->recent_event_errors[index] : "成功");
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2,
                          model->recent_event_errors[index][0] ? UI_DANGER : UI_SUCCESS);
    snprintf(row, sizeof(row), "时间：%s", time_text);
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2, UI_MUTED);
    snprintf(row, sizeof(row), "请求 ID：%s", model->recent_event_request_ids[index][0] ? model->recent_event_request_ids[index] : "未记录");
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 16, dialog.width - 84, 24, 3, UI_MUTED);
    snprintf(row, sizeof(row), "内部详情：%s", model->recent_event_details[index][0] ? model->recent_event_details[index] : "无");
    draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 16, dialog.width - 84, 24, 3, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A / B  关闭",
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_detail_metric(uint32_t *pixels, uint32_t stride, int x, int y,
                               const char *label, const char *value, uint32_t color)
{
    draw_text(pixels, stride, x, y, label, 17, UI_MUTED);
    draw_text(pixels, stride, x, y + 36, value, 28, color);
}

static void draw_home_details(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char line[256], timer[64], remaining[64], today[64], age[64], total[64];
    bool parent = model->view == PTC_UI_PARENT;
    bool error = strcmp(model->result_status, "error") == 0;
    const char *notice = home_runtime_notice(model);
    bool fresh = ptc_ui_status_is_fresh(model, (int64_t)time(NULL));
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 640);
    int x = dialog.x + 32, y = dialog.y + 80;
    UiRect hero = {x, y, 688, 200};
    UiRect status = {x + 704, y, 352, 200};
    ptc_ui_format_timer_status(model, timer, sizeof(timer));
    ptc_ui_format_home_remaining(model, (int64_t)time(NULL), remaining, sizeof(remaining));
    ptc_ui_format_home_total_value(model, total, sizeof(total));
    ptc_ui_format_today_mode(model, today, sizeof(today));
    format_status_age(model, age, sizeof(age));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 736, dialog.y + 26, 352, 30}, age, 17, status_age_color(model));

    fill_round_rect(pixels, stride, hero, 16, UI_ACCENT_SOFT);
    draw_text(pixels, stride, x + 24, y + 32, "今天还可玩", 20, UI_MUTED);
    draw_text(pixels, stride, x + 24, y + 100, remaining, 56, fresh ? UI_ACCENT : UI_MUTED);
    draw_detail_metric(pixels, stride, x + 24, y + 142, "今日总额度", total, UI_INK);
    if (model->played_minutes_available && model->played_minutes >= 0)
        snprintf(line, sizeof(line), "约 %d 分钟", model->played_minutes);
    else snprintf(line, sizeof(line), "暂不可用");
    draw_detail_metric(pixels, stride, x + 360, y + 142, "今日额度消耗估算", line, UI_INK);

    fill_round_rect(pixels, stride, status, 16, UI_RAISED);
    const char *runtime = !model->status_loaded ? "等待刷新" :
        model->disable_flag_present ? "控制已停用" : model->recovery_active ? "正在恢复" :
        model->apply_pending_confirmation ? "等待生效" : !fresh ? "状态待确认" :
        strcmp(model->setup_phase, "active") == 0 ? "正常运行" : "需家长确认";
    const char *labels[] = {"今日规则", "系统计时器", "PlayWise"};
    const char *values[] = {today, timer, runtime};
    for (int i = 0; i < 3; ++i) {
        draw_text(pixels, stride, status.x + 24, y + 28 + i * 60, labels[i], 16, UI_MUTED);
        fit_text(line, sizeof(line), values[i], 22, status.width - 48);
        draw_text(pixels, stride, status.x + 24, y + 54 + i * 60, line, 22,
            i == 2 && (notice[0] || !fresh) ? UI_WARNING : UI_INK);
    }

    int history_y = y + 216;
    if (notice[0]) {
        fill_round_rect(pixels, stride, (UiRect){x, history_y, 1056, 44}, 6,
            model->disable_flag_present ? UI_DANGER_SOFT : UI_WARNING_SOFT);
        draw_wrapped_text(pixels, stride, x + 16, history_y + 19, notice, 17, 1024, 20, 2,
            model->disable_flag_present ? UI_DANGER : UI_WARNING);
        history_y += 52;
    }
    for (int i = 0; i < 2; ++i) {
        UiRect card = {x + i * 536, history_y, 520, 98};
        unsigned int days = i ? model->usage_known_days_30 : model->usage_known_days_7;
        unsigned int minutes = i ? model->usage_consumed_minutes_30 : model->usage_consumed_minutes_7;
        fill_round_rect(pixels, stride, card, 16, UI_RAISED);
        if (model->usage_summary_available && days > 0)
            snprintf(line, sizeof(line), "%u 分钟", minutes);
        else snprintf(line, sizeof(line), "暂不可用");
        draw_detail_metric(pixels, stride, card.x + 24, card.y + 28,
            i ? "近 30 天额度消耗估算" : "近 7 天额度消耗估算", line, UI_INK);
        if (model->usage_summary_available)
            snprintf(line, sizeof(line), "可靠记录 %u 天", days);
        else snprintf(line, sizeof(line), "可靠记录暂不可用");
        draw_text(pixels, stride, card.x + 24, card.y + 86, line, 15, UI_MUTED);
    }
    draw_text(pixels, stride, x, history_y + 122,
        "本机额度消耗包含 HOME 等亮屏使用；游戏明细暂不可用，缺失日期不计入估算", 16, UI_MUTED);
    if (parent) {
        fill_rect(pixels, stride, (UiRect){x, history_y + 140, 1056, 1}, UI_BORDER);
        snprintf(line, sizeof(line), "最近执行  %s    %s", model->command_name, model->transport_label);
        fit_text(line, sizeof(line), line, 16, 1056);
        draw_text(pixels, stride, x, history_y + 162, line, 16, UI_MUTED);
        draw_wrapped_text(pixels, stride, x, history_y + 187, model->message, 19, 1056, 24, 2,
            error ? UI_DANGER : UI_INK);
        /* The last two detail lines reserve the return button's entire column. */
        draw_wrapped_text(pixels, stride, x, history_y + 231, model->feedback_detail, 16, 600, 20, 2,
            error ? UI_DANGER : UI_MUTED);
    }
    home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "A / B  返回", false, true, false);
}

static void draw_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
    case PTC_UI_OVERLAY_HOME_DETAILS:
        draw_home_details(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_MINUTES:
        draw_minutes_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_WEEKLY:
        draw_weekly_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_CONFIRM:
        draw_confirm_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_NUMPAD:
        draw_numpad_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_PIN:
        draw_pin_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_MINUTE_EDITOR:
        draw_minute_editor_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_CREDENTIAL:
        draw_credential_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_GRANT_MANAGER:
        draw_grant_manager_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_REDEMPTION_HISTORY:
        draw_redemption_history_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_ACTIVITY_HISTORY:
        draw_activity_history_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_SCHEDULED_LEAVE:
        draw_scheduled_leave(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_SCHEDULED:
        draw_scheduled_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_AUTONOMY:
        draw_autonomy_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
        draw_shortcut_manager_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_GRANT_LOCAL:
        draw_grant_local_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_QR:
        draw_qr_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
        draw_weekly_leave_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_CREDENTIAL_LEAVE:
        draw_credential_leave_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_CODE_RESULT:
        draw_code_result_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_AUTH_ERROR:
        draw_auth_error_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_SOFTWARE_INFO:
        draw_software_info_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_HOLIDAY_CALENDAR:
        draw_holiday_calendar_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_HOLIDAY_LEAVE:
        draw_holiday_leave_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_SUPPORT_EVENT:
        draw_support_event_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_WEEKLY_BULK:
        draw_weekly_bulk_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_ALBUM_MANAGER:
        draw_album_manager_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_THEME:
        draw_theme_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_NONE:
    default:
        break;
    }
}

bool ptc_ui_graphics_init(void)
{
    PlFontData font_data;
    Result result;
    memset(&g_ui, 0, sizeof(g_ui));
    result = plInitialize(PlServiceType_User);
    if (R_FAILED(result)) {
        return false;
    }
    g_ui.pl_ready = true;
    result = plGetSharedFontByType(&font_data, PlSharedFontType_ChineseSimplified);
    if (R_FAILED(result) || FT_Init_FreeType(&g_ui.library) != 0) {
        ptc_ui_graphics_exit();
        return false;
    }
    if (FT_New_Memory_Face(
            g_ui.library,
            (const FT_Byte *)font_data.address,
            (FT_Long)font_data.size,
            0,
            &g_ui.face) != 0) {
        ptc_ui_graphics_exit();
        return false;
    }
    g_ui.font_ready = true;
    result = framebufferCreate(
        &g_ui.framebuffer,
        nwindowGetDefault(),
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        PIXEL_FORMAT_RGBA_8888,
        2);
    if (R_FAILED(result)) {
        ptc_ui_graphics_exit();
        return false;
    }
    g_ui.framebuffer_ready = true;
    result = framebufferMakeLinear(&g_ui.framebuffer);
    if (R_FAILED(result)) {
        ptc_ui_graphics_exit();
        return false;
    }
    return true;
}

void ptc_ui_graphics_exit(void)
{
    if (g_ui.framebuffer_ready) {
        framebufferClose(&g_ui.framebuffer);
    }
    if (g_ui.font_ready) {
        FT_Done_Face(g_ui.face);
    }
    if (g_ui.library) {
        FT_Done_FreeType(g_ui.library);
    }
    if (g_ui.pl_ready) {
        plExit();
    }
    memset(&g_ui, 0, sizeof(g_ui));
}

void ptc_ui_graphics_draw(const PtcUiModel *model, const PtcUiThemeView *theme)
{
    uint32_t stride_bytes = 0;
    uint32_t *pixels;
    uint32_t stride;
    if (!model || !theme || !theme->palette || !g_ui.framebuffer_ready) {
        return;
    }
    g_palette = theme->palette;
    g_theme = *theme;
    pixels = (uint32_t *)framebufferBegin(&g_ui.framebuffer, &stride_bytes);
    if (!pixels) {
        return;
    }
    stride = stride_bytes / sizeof(uint32_t);
    fill_rect_packed(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, pack_rgb(g_palette->page_bg));
    if (model->view == PTC_UI_PARENT) {
        draw_parent(pixels, stride, model);
    } else if (model->view == PTC_UI_SETUP) {
        draw_setup(pixels, stride, model);
    } else if (model->view == PTC_UI_ERROR) {
        draw_error(pixels, stride, model);
    } else {
        draw_child(pixels, stride, model);
    }
    if (model->overlay != PTC_UI_OVERLAY_NONE) {
        draw_overlay(pixels, stride, model);
    }
#ifdef PLAYWISE_EDEN
    /* Permanent badge: no screenshot from the simulated build may be mistaken
       for real-device PCTL evidence. */
    fill_rect(pixels, stride, (UiRect){1080, 12, 184, 28}, UI_DANGER);
    draw_text(pixels, stride, 1094, 18, "EDEN TEST", 16, UI_ON_ACCENT);
#endif
    framebufferEnd(&g_ui.framebuffer);
}
