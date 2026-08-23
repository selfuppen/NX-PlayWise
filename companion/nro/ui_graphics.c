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
#define COLOR(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

typedef enum {
    UI_COLOR_FILL = 0,
    UI_COLOR_BORDER,
    UI_COLOR_TEXT
} UiColorRole;

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
static PtcUiResolvedTheme g_resolved_theme = PTC_UI_RESOLVED_LIGHT;
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
static void draw_toggle_switch(uint32_t *pixels, uint32_t stride, UiRect rect, bool is_on,
                               bool selected, bool disabled, const char *on_label, const char *off_label);

static uint32_t pack_rgb(uint32_t rgb)
{
    return RGBA8_MAXALPHA((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

static bool color_is(uint32_t color, unsigned int red, unsigned int green, unsigned int blue)
{
    return color == COLOR(red, green, blue);
}

static uint32_t resolve_color(uint32_t source, UiColorRole role)
{
    unsigned int red;
    unsigned int green;
    unsigned int blue;
    if (g_resolved_theme == PTC_UI_RESOLVED_LIGHT || !g_palette) return pack_rgb(source);
    red = (source >> 16) & 0xff;
    green = (source >> 8) & 0xff;
    blue = source & 0xff;
    /* Pale light-theme tints remain raised surfaces; only their text/border
     * carries status color in dark mode. */
    if (role == UI_COLOR_FILL && red >= 225 && green >= 225 && blue >= 225) {
        return pack_rgb(red >= 252 && green >= 252 && blue >= 252
            ? g_palette->surface : g_palette->surface_raised);
    }
    if (color_is(source, 28, 118, 188) || color_is(source, 42, 105, 188) ||
        color_is(source, 20, 90, 160) || color_is(source, 230, 242, 255)) {
        return pack_rgb(role == UI_COLOR_BORDER ? g_palette->focus : g_palette->accent);
    }
    if (color_is(source, 25, 132, 95) || color_is(source, 7, 93, 76) ||
        color_is(source, 235, 248, 242) || color_is(source, 235, 249, 242) ||
        color_is(source, 240, 248, 244)) return pack_rgb(g_palette->success);
    if (color_is(source, 215, 139, 25) || color_is(source, 220, 161, 65) ||
        color_is(source, 170, 109, 18) || color_is(source, 255, 244, 230) ||
        color_is(source, 255, 247, 229) || color_is(source, 255, 249, 238)) return pack_rgb(g_palette->warning);
    if (color_is(source, 194, 61, 61) || color_is(source, 216, 49, 54) ||
        color_is(source, 170, 35, 48) || color_is(source, 170, 65, 65) ||
        color_is(source, 218, 118, 118) || color_is(source, 255, 232, 235) ||
        color_is(source, 255, 235, 238) || color_is(source, 255, 240, 240) ||
        color_is(source, 255, 244, 244) || color_is(source, 255, 245, 245)) return pack_rgb(g_palette->danger);
    if (role == UI_COLOR_TEXT) {
        if (red >= 245 && green >= 245 && blue >= 245) return pack_rgb(g_palette->on_accent);
        if (red <= 70 && green <= 80 && blue <= 95) return pack_rgb(g_palette->text_primary);
        if (red <= 128 && green <= 136 && blue <= 150) return pack_rgb(g_palette->text_secondary);
        return pack_rgb(g_palette->text_disabled);
    }
    if (role == UI_COLOR_BORDER) {
        if (red >= 245 && green >= 245 && blue >= 245) return pack_rgb(g_palette->focus);
        if (red < 150 && green < 160 && blue < 180) return pack_rgb(g_palette->border_control);
        return pack_rgb(g_palette->border_decorative);
    }
    if (red >= 252 && green >= 252 && blue >= 252) return pack_rgb(g_palette->surface);
    return pack_rgb(g_palette->surface_raised);
}

static const UiAction TODAY_ACTIONS[] = {
    {"刷新状态", "读取今天的最新游玩状态", COLOR(42, 105, 188)},
    {"设置今日总额度", "指定今天全天可玩的分钟数", COLOR(42, 105, 188)},
    {"临时加时", "在今天额度上增加分钟", COLOR(25, 132, 95)},
    {"今日不限时", "今天不设时间上限", COLOR(25, 132, 95)},
    {"恢复周计划", "清除今日临时设置，恢复本周规则", COLOR(91, 100, 116)},
};

static const UiAction HOLIDAY_ACTIONS[] = {
    {"国家节假日规则", "开启或关闭自动日历规则", COLOR(42, 105, 188)},
    {"法定休假", "切换模式或设置休息日额度", COLOR(25, 132, 95)},
    {"调休工作日", "切换模式或设置补班日额度", COLOR(215, 139, 25)},
    {"查看当前节假日安排", "查看内置年份、放假日期和调休工作日", COLOR(91, 100, 116)},
    {"保存全部节假日设置", "保存后立即按新设置重新计算今天", COLOR(42, 105, 188)}
};

static const UiAction GRANT_ACTIONS[] = {
    {"立即生成加时码", "在本机生成 8 位加时码", COLOR(25, 132, 95)},
    {"手机/电脑生成加时码", "优先使用完整交付包中的离线网页", COLOR(42, 105, 188)},
    {"加时码生成管理", "管理设备名、密钥、导出配置和二维码地址", COLOR(91, 100, 116)},
};

static const UiAction SETTINGS_ACTIONS[] = {
    {"外观主题", "跟随系统、浅色或暗色", COLOR(42, 105, 188)},
    {"修改任我玩PIN", "验证当前 PIN 后设置新 PIN", COLOR(42, 105, 188)},
    {"家长区快捷键管理", "选择组合并管理孩子区提示", COLOR(42, 105, 188)},
    {"高级设置", "高级启动方式与兼容性选项", COLOR(91, 100, 116)},
    {"支持与恢复", "兼容状态、诊断和恢复操作", COLOR(91, 100, 116)},
};

static const UiAction ADVANCED_ACTIONS[] = {
    {"自制程序菜单高级入口", "改变 hbmenu 启动方式，不提供防篡改保护", COLOR(194, 61, 61)},
};

static const UiAction GRANT_MANAGER_ACTIONS[] = {
    {"管理加时码设备名", "查看、输入或随机生成设备名", COLOR(42, 105, 188)},
    {"管理加时码密钥", "查看、输入或随机生成签名密钥", COLOR(194, 61, 61)},
    {"导出手机/电脑配置", "导出供手机或电脑使用的配置文件", COLOR(25, 132, 95)},
    {"编辑二维码跳转地址", "修改扫码后打开的网页地址", COLOR(42, 105, 188)},
    {"恢复二维码跳转默认地址", "恢复项目提供的默认网页地址", COLOR(91, 100, 116)},
};

static const UiAction SUPPORT_ACTIONS[] = {
    {"确认接管系统控制", "预检、保存快照后启用额度管理", COLOR(42, 105, 188)},
    {"重试修复", "重新检查并恢复安全前置条件", COLOR(25, 132, 95)},
    {"紧急停用控制", "立即停止后台控制操作", COLOR(194, 61, 61)},
    {"恢复安装前状态", "恢复原始设置并停用 任我玩", COLOR(194, 61, 61)},
    {"导出诊断包", "生成不含密钥、PIN 和离线码的支持文件", COLOR(91, 100, 116)},
    {"软件信息", "查看版本、项目仓库和家长网页", COLOR(42, 105, 188)},
};

static const UiAction RESUME_CONTROL_ACTION = {
    "解除停用并重新接管", "安全预检通过后恢复后台控制", COLOR(25, 132, 95)
};

static const UiAction RECONFIRM_ENVIRONMENT_ACTION = {
    "重新检测并接管", "系统环境变化，确认兼容后恢复控制", COLOR(215, 139, 25)
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
    fill_rect_packed(pixels, stride, rect, resolve_color(color, UI_COLOR_FILL));
}

static void draw_horizontal_triangle(
    uint32_t *pixels,
    uint32_t stride,
    int x,
    int center_y,
    int size,
    bool points_right,
    uint32_t color)
{
    int column;
    for (column = 0; column < size; ++column) {
        int progress = points_right ? size - column - 1 : column;
        int half_height = 1 + progress * (size / 2) / (size - 1);
        fill_rect(pixels, stride,
                  (UiRect){x + column, center_y - half_height, 1, half_height * 2 + 1}, color);
    }
}

static void fill_round_rect(uint32_t *pixels, uint32_t stride, UiRect rect, int radius, uint32_t color)
{
    int radius_squared = radius * radius;
    uint32_t resolved = resolve_color(color, UI_COLOR_FILL);
    int y;
    for (y = rect.y; y < rect.y + rect.height; ++y) {
        int x;
        for (x = rect.x; x < rect.x + rect.width; ++x) {
            int dx = 0;
            int dy = 0;
            if (x < rect.x + radius) {
                dx = rect.x + radius - x;
            } else if (x >= rect.x + rect.width - radius) {
                dx = x - (rect.x + rect.width - radius - 1);
            }
            if (y < rect.y + radius) {
                dy = rect.y + radius - y;
            } else if (y >= rect.y + rect.height - radius) {
                dy = y - (rect.y + rect.height - radius - 1);
            }
            if (dx == 0 || dy == 0 || dx * dx + dy * dy <= radius_squared) {
                set_pixel(pixels, stride, x, y, resolved);
            }
        }
    }
}

static void draw_rect_outline(uint32_t *pixels, uint32_t stride, UiRect rect, int width, uint32_t color)
{
    uint32_t resolved = resolve_color(color, UI_COLOR_BORDER);
    fill_rect_packed(pixels, stride, (UiRect){rect.x, rect.y, rect.width, width}, resolved);
    fill_rect_packed(pixels, stride, (UiRect){rect.x, rect.y + rect.height - width, rect.width, width}, resolved);
    fill_rect_packed(pixels, stride, (UiRect){rect.x, rect.y, width, rect.height}, resolved);
    fill_rect_packed(pixels, stride, (UiRect){rect.x + rect.width - width, rect.y, width, rect.height}, resolved);
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
    int outer_squared = radius * radius;
    int inner_radius = radius - width;
    int inner_squared = inner_radius > 0 ? inner_radius * inner_radius : 0;
    uint32_t resolved = resolve_color(color, UI_COLOR_BORDER);
    int y;
    for (y = -radius; y <= radius; ++y) {
        int x;
        for (x = -radius; x <= radius; ++x) {
            int distance_squared = x * x + y * y;
            if (distance_squared <= outer_squared && distance_squared >= inner_squared) {
                set_pixel(pixels, stride, center_x + x, center_y + y, resolved);
            }
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
    uint32_t resolved = resolve_color(color, UI_COLOR_BORDER);
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
    draw_rect_outline(pixels, stride, (UiRect){x, y, 20, 20}, 2, color);
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
    uint32_t resolved = resolve_color(color, UI_COLOR_TEXT);
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
    fill_rect(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, 92}, COLOR(255, 255, 255));
    fill_rect(pixels, stride, (UiRect){0, 90, SCREEN_WIDTH, 2}, COLOR(222, 227, 234));
    fill_round_rect(pixels, stride, (UiRect){54, 28, 38, 38}, 8, COLOR(216, 49, 54));
    fill_round_rect(pixels, stride, (UiRect){68, 28, 38, 38}, 8, COLOR(28, 118, 188));
    draw_text(pixels, stride, 126, 53, title, 29, COLOR(28, 34, 43));
    draw_text(pixels, stride, 126, 77, subtitle, 18, COLOR(97, 106, 120));
}

static void draw_footer_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect, const char *label)
{
    UiRect box = to_uirect(rect);
    fill_round_rect(pixels, stride, box, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, box, 1, COLOR(203, 211, 222));
    draw_text_center(pixels, stride, box, label, 19, COLOR(47, 57, 71));
}

static bool parent_status_is_exception(const PtcUiModel *model)
{
    int64_t age = ptc_ui_status_age_seconds(model, (int64_t)time(NULL));
    return strcmp(model->setup_phase, "protection") == 0 ||
        strcmp(model->setup_phase, "failed") == 0 || model->recovery_active ||
        model->disable_flag_present ||
        (model->temporary_unlocked_available && model->temporary_unlocked) ||
        model->error_code != 0 || age > 120;
}

static void draw_parent_status_footer(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect box = to_uirect(ptc_ui_parent_footer_rect(4));
    char summary[160];
    char fitted[160];
    uint32_t color = parent_status_is_exception(model) ? COLOR(194, 61, 61) : COLOR(28, 118, 188);
    ptc_ui_format_parent_status_summary(model, (int64_t)time(NULL), summary, sizeof(summary));
    fill_round_rect(pixels, stride, box, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, box,
                      model->parent_footer_focused && model->parent_footer_selection == 1 ? 3 : 1,
                      model->parent_footer_focused && model->parent_footer_selection == 1
                        ? color : COLOR(203, 211, 222));
    fit_text(fitted, sizeof(fitted), summary, 17, box.width - 32);
    draw_text_center(pixels, stride, box, fitted, 17, color);
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
    fill_round_rect(pixels, stride, box, 8, background);
    if (outline) {
        draw_rect_outline(pixels, stride, box, 1, COLOR(203, 211, 222));
    }
    draw_text_center(pixels, stride, box, label, 21, foreground);
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

static void draw_candidate_button(
    uint32_t *pixels,
    uint32_t stride,
    PtcUiRect rect,
    const char *label,
    uint32_t background,
    uint32_t foreground,
    bool selected,
    bool disabled)
{
    UiRect box = to_uirect(rect);
    bool primary = foreground == COLOR(255, 255, 255);
    uint32_t fill = disabled ? COLOR(235, 238, 243) : (selected && !primary ? COLOR(230, 242, 255) : background);
    uint32_t text_color = disabled ? COLOR(145, 153, 165) : foreground;
    fill_round_rect(pixels, stride, box, 8, fill);
    draw_rect_outline(pixels, stride, box, selected ? 3 : 1,
                      selected ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
    draw_text_center(pixels, stride, box, label, 20, text_color);
    if (selected) {
        draw_text(pixels, stride, box.x + box.width - 30, box.y + 24, "A", 16,
                  primary && !disabled ? COLOR(255, 255, 255) : COLOR(28, 118, 188));
    }
}

static void draw_overlay_actions(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, const char *confirm_label)
{
    PtcUiRect confirm = ptc_ui_confirm_rect(model->overlay);
    draw_dialog_button(pixels, stride, confirm, confirm_label, COLOR(28, 118, 188), COLOR(255, 255, 255), false);
    if (model->confirm_hold_required && model->overlay == PTC_UI_OVERLAY_CONFIRM && model->confirm_hold_progress > 0) {
        UiRect progress = to_uirect(confirm);
        progress.width = progress.width * model->confirm_hold_progress / 1000;
        fill_round_rect(pixels, stride, progress, 8, COLOR(25, 132, 95));
        draw_text_center(pixels, stride, to_uirect(confirm),
                         model->confirm_hold_progress >= 1000 ? "确认完成" : "继续按住...",
                         20, COLOR(255, 255, 255));
        draw_rect_outline(pixels, stride, to_uirect(confirm), 2, COLOR(16, 102, 72));
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  取消", COLOR(235, 238, 243), COLOR(66, 74, 86), true);
    if (model->overlay == PTC_UI_OVERLAY_CONFIRM &&
        (model->operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
         model->operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
         model->operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY)) {
        PtcUiRect selected = model->overlay_selection == 0
            ? ptc_ui_cancel_rect(model->overlay) : ptc_ui_confirm_rect(model->overlay);
        draw_rect_outline(pixels, stride, to_uirect(selected), 3, COLOR(28, 118, 188));
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
    if (timestamp <= 0) {
        snprintf(out, out_size, "时间未知");
        return;
    }
    event_day = ptc_day_index_from_unix_utc8(timestamp);
    today = ptc_day_index_from_unix_utc8((int64_t)time(NULL));
    minute = ptc_minute_of_day_from_unix_utc8(timestamp);
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

static void describe_status(const PtcUiModel *model, char *today, size_t today_size, char *remaining, size_t remaining_size)
{
    ptc_ui_format_today_mode(model, today, today_size);
    ptc_ui_format_quota_remaining(model, remaining, remaining_size);
}

static void draw_status_tile(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    const char *label,
    const char *value,
    uint32_t accent)
{
    fill_round_rect(pixels, stride, rect, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, rect, 1, COLOR(219, 225, 233));
    fill_round_rect(pixels, stride, (UiRect){rect.x + 18, rect.y + 22, 10, rect.height - 44}, 5, accent);
    draw_text(pixels, stride, rect.x + 47, rect.y + 34, label, 18, COLOR(103, 111, 124));
    draw_text(pixels, stride, rect.x + 47, rect.y + 69, value, 26, COLOR(28, 34, 43));
}

static void draw_disable_banner(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect banner = {790, 42, 436, 38};
    if (!model->disable_flag_present) return;
    fill_round_rect(pixels, stride, banner, 7, COLOR(255, 232, 235));
    draw_rect_outline(pixels, stride, banner, 1, COLOR(194, 61, 61));
    draw_status_symbol(pixels, stride, banner.x + 8, banner.y + 5, COLOR(194, 61, 61), 3);
    draw_text_center(pixels, stride, banner, "紧急停用已开启  |  新的时间控制不会应用", 18, COLOR(170, 35, 48));
}

static void draw_notice(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, int y, int height)
{
    UiRect rect = {54, y, 1172, height};
    uint32_t accent = COLOR(91, 100, 116);
    bool compact = height < 128;
    char fitted[192];
    char detail[192];
    char execution[150];
    if (model->waiting) {
        accent = COLOR(215, 139, 25);
    } else if (strcmp(model->result_status, "ok") == 0) {
        accent = COLOR(25, 132, 95);
    } else if (strcmp(model->result_status, "error") == 0) {
        accent = COLOR(194, 61, 61);
    }
    fill_round_rect(pixels, stride, rect, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, rect, 1, COLOR(219, 225, 233));
    fill_rect(pixels, stride, (UiRect){rect.x, rect.y, 6, rect.height}, accent);
    draw_status_symbol(pixels, stride, rect.x + 20, rect.y + 9, accent,
                       model->waiting ? 2 : (strcmp(model->result_status, "ok") == 0 ? 1 :
                       (strcmp(model->result_status, "error") == 0 ? 3 : 0)));
    draw_text(pixels, stride, rect.x + 48, rect.y + (compact ? 22 : 25),
              model->waiting ? "正在执行" : "最近执行", compact ? 17 : 18, accent);
    snprintf(
        execution,
        sizeof(execution),
        "命令：%s    %s",
        model->command_name[0] ? model->command_name : "未开始",
        model->transport_label[0] ? model->transport_label : "传输：未开始");
    fit_text(fitted, sizeof(fitted), execution, compact ? 17 : 18, rect.width - 48);
    draw_text(pixels, stride, rect.x + 24, rect.y + (compact ? 58 : 64), fitted,
              compact ? 17 : 18, COLOR(77, 86, 99));
    fit_text(fitted, sizeof(fitted), model->message, compact ? 19 : 21, rect.width - 48);
    draw_text(pixels, stride, rect.x + 24, rect.y + (compact ? 82 : 91), fitted,
              compact ? 19 : 20, COLOR(45, 52, 62));
    if (model->feedback_detail[0]) {
        fit_text(detail, sizeof(detail), model->feedback_detail, compact ? 15 : 17, rect.width - 48);
        draw_text(pixels, stride, rect.x + 24, rect.y + (compact ? 98 : 112), detail,
                  compact ? 15 : 16, accent);
    }
}

static void draw_timer_status_tile(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    const char *timer,
    const char *mode,
    uint32_t mode_accent)
{
    const int split_x = rect.x + 143;
    fill_round_rect(pixels, stride, rect, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, rect, 1, COLOR(219, 225, 233));
    fill_rect(pixels, stride, (UiRect){split_x, rect.y + 18, 1, rect.height - 36}, COLOR(219, 225, 233));
    draw_text(pixels, stride, rect.x + 16, rect.y + 34, "系统计时器", 15, COLOR(103, 111, 124));
    draw_text(pixels, stride, rect.x + 16, rect.y + 68, timer, 21, COLOR(28, 34, 43));
    draw_text(pixels, stride, split_x + 15, rect.y + 34, "PlayWise 状态", 15, COLOR(103, 111, 124));
    draw_text(pixels, stride, split_x + 15, rect.y + 68, mode, 19, mode_accent);
}

static void draw_child(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char today[32];
    char remaining[32];
    char played[32];
    char timer[32];
    char parent_hint[128];
    char fitted_hint[128];
    char rule_line[160];
    const char *rule_label = "周计划";
    const char *mode = model->disable_flag_present ? "控制已停用" :
        (strcmp(model->setup_phase, "active") == 0 ? "正常运行" :
        (strcmp(model->setup_phase, "protection") == 0 ? "保护模式" : "兼容性待确认"));
    describe_status(model, today, sizeof(today), remaining, sizeof(remaining));
    ptc_ui_format_timer_status(model, timer, sizeof(timer));
    if (model->played_minutes_available && model->played_minutes >= 0) {
        snprintf(played, sizeof(played), "约 %d 分钟", model->played_minutes);
    } else {
        snprintf(played, sizeof(played), "暂不可用");
    }
    draw_header(pixels, stride, "自律小达人  |  加时奖励", "遵守约定、合理安排时间");
    draw_disable_banner(pixels, stride, model);
    draw_status_tile(pixels, stride, (UiRect){54, 118, 278, 92}, "今日规则", today, COLOR(216, 49, 54));
    draw_status_tile(pixels, stride, (UiRect){350, 118, 278, 92}, "额度剩余", remaining, COLOR(25, 132, 95));
    draw_status_tile(pixels, stride, (UiRect){646, 118, 278, 92}, "额度已耗（估算）", played, COLOR(215, 139, 25));
    draw_timer_status_tile(pixels, stride, (UiRect){942, 118, 284, 92},
                           timer, mode,
                           model->disable_flag_present ? COLOR(194, 61, 61) : COLOR(28, 118, 188));
    if (strcmp(model->rule_source, "today_override") == 0) rule_label = "今日临时设置";
    else if (strcmp(model->rule_source, "statutory_holiday") == 0) rule_label = "国家法定休假日";
    else if (strcmp(model->rule_source, "makeup_workday") == 0) rule_label = "国家调休工作日";
    snprintf(rule_line, sizeof(rule_line), "当前按%s执行%s", rule_label,
             model->calendar_update_warning ? "  |  节假日日历需要更新" : "");
    if (model->temporary_unlocked_available && model->temporary_unlocked) {
        snprintf(rule_line, sizeof(rule_line), "当前按%s执行  |  临时解除中，锁屏后恢复限制", rule_label);
    }
    draw_text_center(pixels, stride, (UiRect){54, 212, 1172, 22}, rule_line, 17,
                     model->calendar_update_warning ? COLOR(194, 61, 61) : COLOR(77, 86, 99));

    fill_round_rect(pixels, stride, (UiRect){54, 238, 760, 246}, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, (UiRect){54, 238, 760, 246}, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, 86, 275, "加时码", 18, COLOR(28, 118, 188));
    fill_round_rect(pixels, stride, to_uirect(ptc_ui_child_submit_rect()), 8,
                    model->disable_flag_present ? COLOR(244, 246, 249) : COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, to_uirect(ptc_ui_child_submit_rect()), 2,
                      model->disable_flag_present ? COLOR(203, 211, 222) : COLOR(28, 118, 188));
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_submit_rect()),
                     model->disable_flag_present ? "紧急停用中" : "输入加时码", 34,
                     model->disable_flag_present ? COLOR(91, 100, 116) : COLOR(28, 118, 188));
    draw_text(pixels, stride, 86, 416,
              model->disable_flag_present ? "加时码兑换暂不可用" :
              "加时之前，记得向窗外远眺至少 5 分钟，让眼睛放松一下吧！",
              18, COLOR(85, 94, 107));
    draw_text(pixels, stride, 86, 455,
              model->disable_flag_present ? "" : "按 A 或点击输入  |  仅支持 8 位数字码",
              17, COLOR(103, 111, 124));

    fill_round_rect(pixels, stride, (UiRect){836, 238, 390, 274}, 8, COLOR(250, 251, 253));
    draw_rect_outline(pixels, stride, (UiRect){836, 238, 390, 274}, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, 866, 282, "游戏时间统计", 24, COLOR(28, 34, 43));
    draw_text(pixels, stride, 866, 329, "游戏明细暂不可用", 22, COLOR(85, 94, 107));
    draw_text(pixels, stride, 866, 362, "前 3 名游戏将在数据可用后显示", 17, COLOR(103, 111, 124));
    fill_round_rect(pixels, stride, (UiRect){866, 384, 330, 50}, 7, COLOR(244, 248, 253));
    draw_text(pixels, stride, 884, 416, "今日额度已耗（估算）", 17, COLOR(103, 111, 124));
    draw_text(pixels, stride, 1034, 416, played, 20, COLOR(28, 118, 188));
    draw_dialog_button(pixels, stride, ptc_ui_child_refresh_rect(),
                       model->waiting ? "正在刷新状态..." : "Y  立即刷新状态",
                       model->waiting ? COLOR(215, 139, 25) : COLOR(28, 118, 188),
                       COLOR(255, 255, 255), false);

    draw_notice(pixels, stride, model, 530, 100);
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(0),
                       model->disable_flag_present ? "紧急停用中" : "A  输入加时码");
    fill_round_rect(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), 1, COLOR(203, 211, 222));
    if (model->show_parent_shortcut_hint && model->custom_shortcut_enabled) {
        ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label, parent_hint, sizeof(parent_hint));
    } else {
        snprintf(parent_hint, sizeof(parent_hint), "状态会在后台自动同步");
    }
    fit_text(fitted_hint, sizeof(fitted_hint), parent_hint, 16, ptc_ui_child_footer_rect(1).w - 16);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), fitted_hint, 16, COLOR(47, 57, 71));
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), "B  退出");
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
    snprintf(title, sizeof(title), "首次设置  |  %d/6", step);
    draw_header(pixels, stride, grace_remaining >= 0 ? "正在同步" : title,
        grace_remaining >= 0 ? "系统设置正在同步，完成后继续选择进入的区域" : "按步骤完成 任我玩 的家长设置");
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    if (grace_remaining >= 0) {
        draw_text(pixels, stride, 204, 190, "环境检查已通过", 31, COLOR(25, 132, 95));
        snprintf(phase_line, sizeof(phase_line), "当前状态：正在同步    安装前快照：%s",
                 model->setup_snapshot_available ? "已保存" : "不可用");
        draw_text(pixels, stride, 204, 248, phase_line, 22, COLOR(77, 86, 99));
        if (grace_remaining > 0) {
            snprintf(countdown_line, sizeof(countdown_line), "系统设置同步中（约 %lld 秒）...", (long long)grace_remaining);
        } else {
            snprintf(countdown_line, sizeof(countdown_line), "同步完成，正在启用额度管理...");
        }
        draw_text(pixels, stride, 204, 310, countdown_line, 34, COLOR(28, 118, 188));
        draw_text(pixels, stride, 204, 356, "无需操作；同步完成后会进入第 5 步选择区域。", 22, COLOR(45, 52, 62));
    } else {
        draw_text(pixels, stride, 104, 154, "1 快捷键", 16, step == PTC_UI_SETUP_SHORTCUT ? COLOR(28, 118, 188) : COLOR(91, 100, 116));
        draw_text(pixels, stride, 286, 154, "2 PIN", 16, step == PTC_UI_SETUP_PIN ? COLOR(28, 118, 188) : COLOR(91, 100, 116));
        draw_text(pixels, stride, 452, 154, "3 外观主题", 16, step == PTC_UI_SETUP_THEME ? COLOR(28, 118, 188) : COLOR(91, 100, 116));
        draw_text(pixels, stride, 708, 154, "4 接管", 16, step == PTC_UI_SETUP_TAKEOVER ? COLOR(28, 118, 188) : COLOR(91, 100, 116));
        draw_text(pixels, stride, 912, 154, "5 进入区域", 16, step == PTC_UI_SETUP_ZONE ? COLOR(28, 118, 188) : COLOR(91, 100, 116));
        if (step == PTC_UI_SETUP_SHORTCUT) {
            UiRect compact_fixed = {204, 184, 872, 54};
            fill_round_rect(pixels, stride, compact_fixed, 8, COLOR(244, 249, 255));
            draw_rect_outline(pixels, stride, compact_fixed, 2, COLOR(28, 118, 188));
            draw_text(pixels, stride, 232, 218, "固定入口 Minus：松开即可进入，无需长按", 20, COLOR(28, 118, 188));
            draw_text(pixels, stride, 204, 264, "自定义组合需长按约 400ms；按 A 加入草稿，按 + 确认后生效", 18, COLOR(77, 86, 99));
            for (int index = 0; index < PTC_UI_SHORTCUT_PRESET_COUNT; ++index) {
                UiRect card = to_uirect(ptc_ui_setup_shortcut_card_rect(index));
                bool selected = index == model->setup_shortcut_index;
                fill_round_rect(pixels, stride, card, 8, selected ? COLOR(230, 242, 255) : COLOR(250, 251, 253));
                draw_rect_outline(pixels, stride, card, selected ? 2 : 1,
                                  selected ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
                draw_text_center(pixels, stride, card,
                                 ptc_ui_shortcut_common_label(index), 16,
                                 selected ? COLOR(28, 118, 188) : COLOR(45, 52, 62));
            }
            draw_text(pixels, stride, 204, 554,
                      model->shortcut_draft_enabled ? "待确认自定义组合（需长按）：" : "待确认状态：仅保留 Minus（松开进入）",
                      17, COLOR(91, 100, 116));
            if (model->shortcut_draft_enabled) {
                fit_text(fitted, sizeof(fitted), model->shortcut_draft_label, 18, 250);
                draw_text(pixels, stride, 420, 554, fitted, 18, COLOR(28, 118, 188));
            }
        } else if (step == PTC_UI_SETUP_PIN) {
            draw_text(pixels, stride, 204, 220, "任我玩 PIN 已设置", 30, COLOR(28, 34, 43));
            draw_text(pixels, stride, 204, 262, "全新安装默认 PIN：110", 24, COLOR(215, 139, 25));
            draw_text(pixels, stride, 204, 294, "默认值属于弱保护；可继续使用，也可现在修改为 1到64 位数字。", 20, COLOR(77, 86, 99));
            draw_dialog_button(pixels, stride, ptc_ui_setup_pin_rect(), "A / 点击  修改 PIN",
                               COLOR(28, 118, 188), COLOR(255, 255, 255), false);
            draw_text(pixels, stride, 204, 420, "选择“继续使用”不会保存 PIN 明文；认证文件只保存随机盐和哈希。", 19, COLOR(91, 100, 116));
        } else if (step == PTC_UI_SETUP_TAKEOVER) {
            bool resuming_restored_setup = model->disable_flag_present && strcmp(phase, "restored") == 0;
            bool reconfirming_environment = ptc_ui_runtime_fingerprint_reconfirmation_needed(model);
            bool takeover_complete = ptc_ui_setup_takeover_complete(model);
            draw_text(pixels, stride, 204, 218,
                      takeover_complete ? "系统控制接管已完成" :
                      (reconfirming_environment ? "系统环境已变化" :
                       (resuming_restored_setup ? "解除停用并重新接管" : "确认接管系统控制")),
                      30, takeover_complete ? COLOR(25, 132, 95) : COLOR(28, 34, 43));
            snprintf(phase_line, sizeof(phase_line), "当前状态：%s    安装前快照：%s",
                     takeover_complete ? (strcmp(phase, "active") == 0 ? "正常运行" : "正在同步") :
                     (strcmp(phase, "protection") == 0 ? "保护模式" :
                     (strcmp(phase, "failed") == 0 ? "检查失败" :
                      (resuming_restored_setup ? "已恢复并停用" : "等待家长确认"))),
                     model->setup_snapshot_available ? "已保存" : "待保存");
            draw_text(pixels, stride, 204, 266, phase_line, 21, COLOR(77, 86, 99));
            draw_text(pixels, stride, 204, 324,
                      takeover_complete
                          ? "此步骤已经完成；继续不会重复保存快照或写入系统设置。"
                          : reconfirming_environment
                           ? "系统版本或运行环境与上次确认时不同，需要家长重新确认兼容性。"
                          : resuming_restored_setup
                           ? "确认后会重新执行只读兼容预检；通过后才解除紧急停用并重新接管。"
                           : "确认后会先执行只读兼容预检，再保存安装前快照并启用额度管理。",
                      21, COLOR(45, 52, 62));
            draw_text(pixels, stride, 204, 360,
                      takeover_complete
                          ? "按 A 或点击继续，进入第 5 步选择区域。"
                          : reconfirming_environment
                           ? "确认后先只读检测；通过后保留现有配置并恢复控制。"
                           : resuming_restored_setup
                            ? "恢复接管会保留现有配置，并按安全恢复流程重新同步。"
                            : "首次接管会原样保留今天的总额度和剩余时间，不会先临时解限。",
                      21, COLOR(45, 52, 62));
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(),
                               takeover_complete ? "A / 点击  继续到第 5 步" :
                               (reconfirming_environment ? "A / 点击  重新检测并接管" :
                                (resuming_restored_setup ? "A / 点击  解除停用并重新接管" : "A / 点击  确认接管")),
                               takeover_complete ? COLOR(25, 132, 95) : COLOR(28, 118, 188),
                               COLOR(255, 255, 255), false);
        } else if (step == PTC_UI_SETUP_THEME) {
            static const char *LABELS[] = {"跟随系统", "浅色", "暗色"};
            static const char *DETAILS[] = {"随 Switch 设置", "经典浅色外观", "OLED Hybrid"};
            draw_text(pixels, stride, 204, 220, "选择外观主题", 30, COLOR(28, 34, 43));
            draw_text(pixels, stride, 204, 252, "默认跟随系统；只改变主机应用外观，不影响计时和后台控制。", 18, COLOR(77, 86, 99));
            for (int index = 0; index < 3; ++index) {
                UiRect option = to_uirect(ptc_ui_setup_theme_rect(index));
                bool selected = index == model->setup_theme_index;
                fill_round_rect(pixels, stride, option, 8, selected ? COLOR(230, 242, 255) : COLOR(248, 250, 252));
                draw_rect_outline(pixels, stride, option, selected ? 3 : 1,
                                  selected ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
                draw_text_center(pixels, stride, (UiRect){option.x, option.y + 22, option.width, 36},
                                 LABELS[index], 23, COLOR(28, 34, 43));
                draw_text_center(pixels, stride, (UiRect){option.x, option.y + 70, option.width, 28},
                                 DETAILS[index], 16, COLOR(91, 100, 114));
            }
            draw_text(pixels, stride, 204, 448, "左右选择  |  A / + 保存并继续", 18, COLOR(28, 118, 188));
        } else {
            draw_text(pixels, stride, 204, 214, "初始化完成，选择进入区域", 30, COLOR(28, 34, 43));
            draw_text(pixels, stride, 204, 254, "之后可在两个区域之间切换；进入家长区会受 PIN 保护。", 21, COLOR(77, 86, 99));
            for (int index = 0; index < 2; ++index) {
                UiRect card = to_uirect(ptc_ui_setup_zone_rect(index));
                bool selected = index == model->setup_zone_index;
                fill_round_rect(pixels, stride, card, 8, selected ? COLOR(230, 242, 255) : COLOR(250, 251, 253));
                draw_rect_outline(pixels, stride, card, selected ? 3 : 1,
                                  selected ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 26, card.width, 34},
                                 index == 0 ? "孩子区" : "家长区", 27,
                                 selected ? COLOR(28, 118, 188) : COLOR(45, 52, 62));
                if (index == 0) {
                    char shortcut_hint[160];
                    char fitted_shortcut_hint[160];
                    ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label,
                                                       shortcut_hint, sizeof(shortcut_hint));
                    fit_text(fitted_shortcut_hint, sizeof(fitted_shortcut_hint), shortcut_hint, 17, card.width - 36);
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 86, card.width - 36, 26},
                                     model->show_parent_shortcut_hint && model->custom_shortcut_enabled
                                        ? fitted_shortcut_hint : "家长区快捷提示未显示", 17, COLOR(77, 86, 99));
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     "家长区需要输入 任我玩 PIN", 17, COLOR(91, 100, 116));
                } else {
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 86, card.width - 36, 26},
                                     "固定 Minus：松开进入，无需长按", 18, COLOR(77, 86, 99));
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     model->custom_shortcut_enabled ? "自定义组合：长按约 400ms" : "未启用自定义组合", 17, COLOR(91, 100, 116));
                }
            }
        }
        if (model->message[0] && step != PTC_UI_SETUP_SHORTCUT) {
            fit_text(fitted, sizeof(fitted), model->message, 18, 1160);
            draw_text(pixels, stride, 64, 530, fitted, 18, COLOR(91, 100, 116));
        }
        if (model->feedback_detail[0]) {
            fit_text(fitted, sizeof(fitted), model->feedback_detail, 17, 1160);
            draw_text(pixels, stride, 64, 552, fitted, 17, COLOR(194, 61, 61));
        }
    }
    if (grace_remaining < 0) {
        draw_dialog_button(pixels, stride, ptc_ui_setup_back_rect(), "B  返回上一步",
                           COLOR(235, 238, 243), COLOR(66, 74, 86), true);
        if (step == PTC_UI_SETUP_SHORTCUT) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), "+  确认快捷键并继续",
                               COLOR(28, 118, 188), COLOR(255, 255, 255), false);
        } else if (step == PTC_UI_SETUP_ZONE) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(),
                               model->setup_zone_index == 1 ? "A  确认进入家长区" : "A  确认进入孩子区",
                               COLOR(28, 118, 188), COLOR(255, 255, 255), false);
        } else if (step == PTC_UI_SETUP_PIN) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), "A  继续使用当前 PIN",
                               COLOR(28, 118, 188), COLOR(255, 255, 255), false);
        } else if (step == PTC_UI_SETUP_THEME) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), "A  保存主题并继续",
                               COLOR(28, 118, 188), COLOR(255, 255, 255), false);
        }
    }
}

static void draw_error(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {214, 148, 852, 444};
    char fitted[192];
    char execution[150];
    draw_header(pixels, stride, "操作未完成", "请查看错误信息后重试或返回");
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    fill_round_rect(pixels, stride, (UiRect){254, 194, 64, 64}, 8, COLOR(194, 61, 61));
    draw_text_center(pixels, stride, (UiRect){254, 194, 64, 64}, "!", 34, COLOR(255, 255, 255));
    draw_text(pixels, stride, 342, 214, "加时码处理失败", 28, COLOR(28, 34, 43));
    snprintf(
        execution,
        sizeof(execution),
        "命令：%s    %s",
        model->command_name[0] ? model->command_name : "未开始",
        model->transport_label[0] ? model->transport_label : "传输：未开始");
    fit_text(fitted, sizeof(fitted), execution, 18, 756);
    draw_text(pixels, stride, 254, 286, fitted, 18, COLOR(91, 100, 114));
    fit_text(fitted, sizeof(fitted), model->message, 23, 756);
    draw_text(pixels, stride, 254, 342, fitted, 23, COLOR(77, 86, 99));
    if (model->feedback_detail[0]) {
        fit_text(fitted, sizeof(fitted), model->feedback_detail, 17, 756);
        draw_text(pixels, stride, 254, 390, fitted, 17, COLOR(194, 61, 61));
    }

    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_retry_rect()), 8, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_retry_rect()),
                    model->error_code == 306 ? "A  重新检测" : "A  重新输入", 25, COLOR(255, 255, 255));
    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 8, COLOR(235, 238, 243));
    draw_rect_outline(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 1, COLOR(203, 211, 222));
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_back_rect()), "B  返回主页", 25, COLOR(66, 74, 86));
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
    int index;
    for (index = 0; index < PTC_UI_PARENT_PAGE_COUNT; ++index) {
        UiRect tab = to_uirect(ptc_ui_parent_tab_rect(index));
        uint32_t background = index == (int)model->parent_page ? COLOR(28, 118, 188) : COLOR(235, 238, 243);
        uint32_t foreground = index == (int)model->parent_page ? COLOR(255, 255, 255) : COLOR(66, 74, 86);
        fill_round_rect(pixels, stride, tab, 8, background);
        draw_text_center(pixels, stride, tab, LABELS[index], 18, foreground);
    }
    if (!(model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page != PTC_UI_SETTINGS_ROOT)) {
        draw_text(pixels, stride, 1038, 140, "L / R 切换", 19, COLOR(97, 106, 120));
    }
}

static void draw_settings_badge(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *label = ptc_ui_settings_status_label(model);
    uint32_t color = label && strcmp(label, "需处理") == 0 ? COLOR(194, 61, 61) : COLOR(215, 139, 25);
    UiRect badge;
    if (!label) return;
    badge = (UiRect){54 + PTC_UI_PARENT_SETTINGS * 174 + 96, 113, 56, 24};
    fill_round_rect(pixels, stride, badge, 6, color);
    draw_text_center(pixels, stride, badge, label, 11, COLOR(255, 255, 255));
}

static void draw_settings_hierarchy(
    uint32_t *pixels,
    uint32_t stride,
    PtcUiRect hierarchy_rect,
    PtcUiRect back_rect,
    const char *title)
{
    UiRect bar = to_uirect(hierarchy_rect);
    UiRect badge = {bar.x + 218, bar.y + 14, 84, 28};
    fill_round_rect(pixels, stride, bar, 8, COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, bar, 1, COLOR(184, 211, 239));
    draw_text(pixels, stride, bar.x + 18, bar.y + 36, title, 20, COLOR(28, 118, 188));
    fill_round_rect(pixels, stride, badge, 6, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, badge, "二级页面", 13, COLOR(255, 255, 255));
    draw_dialog_button(pixels, stride, back_rect, "B  返回设置",
                       COLOR(255, 255, 255), COLOR(47, 57, 71), true);
}

static void draw_action_card(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    const UiAction *action,
    bool selected,
    PtcUiActionState state,
    int reserved_right)
{
    bool disabled = state == PTC_UI_ACTION_DISABLED;
    bool recommended = state == PTC_UI_ACTION_RECOMMENDED;
    int title_size = 24;
    int subtitle_size = 18;
    int compact = rect.height < 90;
    uint32_t background = disabled ? COLOR(244, 246, 249) :
                          selected ? COLOR(244, 249, 255) : COLOR(255, 255, 255);
    uint32_t border = disabled ? COLOR(230, 233, 238) :
                      selected ? action->accent : COLOR(219, 225, 233);
    uint32_t title_color = disabled ? COLOR(160, 168, 180) : COLOR(28, 34, 43);
    uint32_t sub_color = disabled ? COLOR(180, 186, 196) : COLOR(91, 100, 114);
    fill_round_rect(pixels, stride, rect, 8, background);
    draw_rect_outline(pixels, stride, rect, selected && !disabled ? 3 : 1, border);
    fill_round_rect(pixels, stride, (UiRect){rect.x + 20, rect.y + (compact ? 20 : 25), 12,
                    rect.height - (compact ? 40 : 50)}, 6,
                    disabled ? COLOR(200, 206, 214) : action->accent);
    while (title_size > 18 && measure_text(action->title, title_size) > rect.width - 112 - reserved_right) --title_size;
    while (subtitle_size > 14 && measure_text(action->subtitle, subtitle_size) > rect.width - 80 - reserved_right) --subtitle_size;
    draw_text(pixels, stride, rect.x + 54, rect.y + (compact ? 38 : 46), action->title, title_size, title_color);
    {
        char subtitle[192];
        fit_text(subtitle, sizeof(subtitle), action->subtitle, subtitle_size, rect.width - 80 - reserved_right);
        draw_text(pixels, stride, rect.x + 54, rect.y + (compact ? 66 : 78), subtitle, subtitle_size, sub_color);
    }
    if (recommended && !disabled) {
        fill_round_rect(pixels, stride, (UiRect){rect.x + rect.width - 110, rect.y + 12, 90, 28}, 6, COLOR(25, 132, 95));
        draw_text_center(pixels, stride, (UiRect){rect.x + rect.width - 110, rect.y + 12, 90, 28}, "强烈建议", 17, COLOR(255, 255, 255));
    }
    if (selected && !disabled) {
        draw_text(pixels, stride, rect.x + rect.width - 74, rect.y + 64, "A", 23, action->accent);
    }
}

static void draw_safety_status(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 324};
    char troubleshoot[128];
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, panel.x + 26, panel.y + 36, "状态详情", 23, COLOR(28, 34, 43));
    draw_text(pixels, stride, panel.x + 26, panel.y + 70, "运行环境与软件信息", 17, COLOR(103, 111, 124));
    if (model->environment_available) {
        char row[96];
        snprintf(row, sizeof(row), "系统版本：%s", model->environment_hos[0] ? model->environment_hos : "未知");
        draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 96, row, 16, panel.width - 52, 20, 2, COLOR(28, 34, 43));
        snprintf(row, sizeof(row), "主机型号：%s", model->environment_model[0] ? model->environment_model : "未知");
        draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 120, row, 16, panel.width - 52, 20, 2, COLOR(28, 34, 43));
        snprintf(row, sizeof(row), "Atmosphère：%s", model->environment_atmosphere ? "已检测" : "未检测");
        draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 144, row, 16, panel.width - 52, 20, 2, COLOR(28, 34, 43));
    } else {
        draw_text(pixels, stride, panel.x + 26, panel.y + 98, "运行环境暂不可用", 16, COLOR(194, 61, 61));
    }

    if (model->disable_flag_present) snprintf(troubleshoot, sizeof(troubleshoot), "紧急停用：%s",
        model->disable_reason[0] ? model->disable_reason : "旧版本未记录原因");
    else if (model->recovery_active) snprintf(troubleshoot, sizeof(troubleshoot), "存在待处理恢复事务");
    else if (model->error_code) snprintf(troubleshoot, sizeof(troubleshoot), "最近错误：%d", model->error_code);
    else snprintf(troubleshoot, sizeof(troubleshoot), "未发现需要处理的故障");
    draw_text(pixels, stride, panel.x + 26, panel.y + 181, "排障摘要", 17, COLOR(103, 111, 124));
    fit_text(troubleshoot, sizeof(troubleshoot), troubleshoot, 17, panel.width - 52);
    draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 207, troubleshoot, 16, panel.width - 52, 20, 2,
              (model->disable_flag_present || model->recovery_active || model->error_code)
                  ? COLOR(194, 61, 61) : COLOR(25, 132, 95));
    draw_text(pixels, stride, panel.x + 26, panel.y + 246, "最近事件", 17, COLOR(103, 111, 124));
    if (model->recent_event_count > 0) {
        for (int event_index = 0; event_index < model->recent_event_count; ++event_index) {
            char latest[128];
            char event_time[48];
            int source_index = model->recent_event_count - 1 - event_index;
            format_event_time(model->recent_event_timestamps[source_index], false, event_time, sizeof(event_time));
            snprintf(latest, sizeof(latest), "%s  |  %s", model->recent_events[source_index], event_time);
            fit_text(latest, sizeof(latest), latest, 13, panel.width - 52);
            draw_text(pixels, stride, panel.x + 26, panel.y + 270 + event_index * 26,
                      latest, 13, event_index + 6 == model->selected_index ? COLOR(28, 118, 188) : COLOR(91, 100, 116));
        }
    } else {
        draw_text(pixels, stride, panel.x + 26, panel.y + 274,
                  model->recent_events_available ? "最近没有需要注意的事件" : "暂时无法读取最近事件，可刷新后重试",
                  14, model->recent_events_available ? COLOR(91, 100, 116) : COLOR(194, 61, 61));
    }
}

static void draw_status_row(
    uint32_t *pixels,
    uint32_t stride,
    UiRect panel,
    int y,
    const char *label,
    const char *value,
    uint32_t value_color)
{
    draw_text(pixels, stride, panel.x + 26, y, label, 19, COLOR(103, 111, 124));
    draw_text(pixels, stride, panel.x + 172, y, value, 19, value_color);
}

static void draw_today_status(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 324};
    char today[32];
    char remaining[32];
    char played[32];
    char freshness[64];
    char timer[32];
    uint32_t today_color;
    bool runtime_notice = (model->temporary_unlocked_available && model->temporary_unlocked) ||
        (model->restriction_enabled_available && !model->restriction_enabled);
    describe_status(model, today, sizeof(today), remaining, sizeof(remaining));
    ptc_ui_format_timer_status(model, timer, sizeof(timer));
    if (model->played_minutes_available && model->played_minutes >= 0) {
        snprintf(played, sizeof(played), "约 %d 分钟", model->played_minutes);
    } else {
        snprintf(played, sizeof(played), "暂不可用");
    }
    format_status_age(model, freshness, sizeof(freshness));
    if (model->blocked_today == 1) {
        today_color = COLOR(194, 61, 61);
    } else if (model->unrestricted_today == 1) {
        today_color = COLOR(25, 132, 95);
    } else if (model->limited_today == 1) {
        today_color = model->restricted_now == 1 ||
            (model->remaining_available && model->remaining_minutes <= 0)
            ? COLOR(194, 61, 61) : COLOR(28, 118, 188);
    } else {
        today_color = COLOR(91, 100, 116);
    }
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, panel.x + 26, panel.y + 43, "今日状态", 23, COLOR(28, 34, 43));
    draw_status_row(pixels, stride, panel, panel.y + 72, "今日规则", today, today_color);
    draw_status_row(pixels, stride, panel, panel.y + 102, "额度剩余", remaining,
                    model->remaining_available && model->remaining_minutes <= 0
                        ? COLOR(194, 61, 61) :
                    (model->remaining_available ? COLOR(28, 34, 43) : COLOR(91, 100, 116)));
    draw_status_row(pixels, stride, panel, panel.y + 132, "额度已耗（估算）", played,
                    model->played_minutes_available ? COLOR(28, 34, 43) : COLOR(91, 100, 116));
    draw_status_row(pixels, stride, panel, panel.y + 162, "PlayWise 状态",
                    model->disable_flag_present ? "控制已停用" :
                    (strcmp(model->setup_phase, "active") == 0 ? "正常运行" :
                    (strcmp(model->setup_phase, "protection") == 0 ? "保护模式" : "兼容性待确认")),
                    model->disable_flag_present ? COLOR(194, 61, 61) :
                    (strcmp(model->setup_phase, "active") == 0 ? COLOR(25, 132, 95) : COLOR(215, 139, 25)));
    draw_status_row(pixels, stride, panel, panel.y + 192, "系统计时器",
                    timer,
                    model->play_timer_enabled == 1 ? COLOR(25, 132, 95) :
                    (model->play_timer_enabled == 0 ? COLOR(28, 118, 188) : COLOR(91, 100, 116)));
    if (model->temporary_unlocked_available && model->temporary_unlocked) {
        snprintf(freshness, sizeof(freshness), "临时解除中 | 锁屏后恢复限制");
    } else if (model->restriction_enabled_available && !model->restriction_enabled) {
        snprintf(freshness, sizeof(freshness), "Nintendo 家长控制未启用");
    }
    fill_round_rect(pixels, stride, (UiRect){panel.x + 20, panel.y + 224, panel.width - 40, 42}, 7,
                    model->waiting || runtime_notice ? COLOR(255, 247, 229) : COLOR(244, 248, 253));
    draw_text_center(pixels, stride, (UiRect){panel.x + 20, panel.y + 224, panel.width - 40, 42},
                     freshness, 17,
                     model->waiting || runtime_notice ? COLOR(170, 109, 18) : status_age_color(model));
}

static void draw_grant_help(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 220};
    (void)model;
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, panel.x + 26, panel.y + 43, "加时码如何生成", 23, COLOR(28, 34, 43));
    draw_text(pixels, stride, panel.x + 26, panel.y + 78, "本机：直接生成 8 位加时码", 17, COLOR(25, 132, 95));
    draw_text(pixels, stride, panel.x + 26, panel.y + 108, "手机/电脑：扫码或直接打开网页", 17, COLOR(28, 118, 188));
    draw_text(pixels, stride, panel.x + 26, panel.y + 138, "管理：设备名、密钥和网页地址", 17, COLOR(77, 86, 99));
    draw_text(pixels, stride, panel.x + 26, panel.y + 174, "配置和二维码包含加时权限，请勿外传。", 15, COLOR(170, 65, 65));
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
        ? COLOR(25, 132, 95)
        : (model->diagnostic_status == PTC_UI_DIAGNOSTIC_ERROR
            ? COLOR(194, 61, 61) : COLOR(215, 139, 25));
    char line[320];
    fill_round_rect(pixels, stride, rect, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, rect, 1, COLOR(219, 225, 233));
    fill_rect(pixels, stride, (UiRect){rect.x, rect.y, 6, rect.height}, accent);
    if (model->diagnostic_status == PTC_UI_DIAGNOSTIC_EXPORTING) {
        draw_text(pixels, stride, rect.x + 24, rect.y + 36, "正在导出诊断包...", 21, accent);
        draw_text(pixels, stride, rect.x + 24, rect.y + 76,
                  "诊断包会排除密钥、PIN、离线码和完整 nonce。", 17, COLOR(77, 86, 99));
        return;
    }
    if (model->diagnostic_status == PTC_UI_DIAGNOSTIC_ERROR) {
        draw_text(pixels, stride, rect.x + 24, rect.y + 36, "诊断包导出失败。", 21, accent);
        draw_text(pixels, stride, rect.x + 24, rect.y + 76,
                  "请确认 SD 卡可写后重试。", 17, COLOR(77, 86, 99));
        return;
    }
    snprintf(line, sizeof(line), "诊断包导出成功：%s", model->diagnostic_path);
    draw_text(pixels, stride, rect.x + 24, rect.y + 32, line, 17, COLOR(28, 34, 43));
    draw_text(pixels, stride, rect.x + 24, rect.y + 66,
              "如遇到问题，提交 GitHub Issue 时请附上此文件。", 17, COLOR(45, 52, 62));
    draw_text(pixels, stride, rect.x + 24, rect.y + 100,
              "GitHub 地址：https://github.com/selfuppen/NX-PlayWise/issues", 17, accent);
}

static uint32_t time_state_accent(bool available, bool unlimited, int minutes)
{
    if (!available) return COLOR(215, 139, 25);
    if (unlimited) return COLOR(25, 132, 95);
    if (minutes <= 0) return COLOR(194, 61, 61);
    if (minutes <= 15) return COLOR(215, 139, 25);
    return COLOR(25, 132, 95);
}

static void draw_time_state_card(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    const char *label,
    const char *value,
    uint32_t accent)
{
    int label_size = 16;
    int value_size = 23;
    while (label_size > 12 && measure_text(label, label_size) > rect.width - 16) --label_size;
    while (value_size > 17 && measure_text(value, value_size) > rect.width - 16) --value_size;
    fill_round_rect(pixels, stride, rect, 8, COLOR(248, 250, 253));
    draw_rect_outline(pixels, stride, rect, 2, accent);
    draw_text_center(pixels, stride, (UiRect){rect.x + 8, rect.y + 10, rect.width - 16, 26}, label, label_size, COLOR(91, 100, 114));
    draw_text_center(pixels, stride, (UiRect){rect.x + 8, rect.y + 38, rect.width - 16, 38}, value, value_size, accent);
}

static void format_rule_remaining_label(const PtcUiModel *model, PtcDayRule rule, char *out, size_t out_size)
{
    int remaining;
    if (rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(out, out_size, "不限时");
    } else if (!model->played_minutes_available || model->played_minutes < 0) {
        snprintf(out, out_size, "暂不可用");
    } else {
        remaining = (int)rule.minutes - model->played_minutes;
        if (remaining < 0) remaining = 0;
        snprintf(out, out_size, "%d 分钟", remaining);
    }
}

static void format_status_age(const PtcUiModel *model, char *out, size_t out_size)
{
    int64_t age;
    if (model->waiting) {
        snprintf(out, out_size, "正在刷新状态...");
        return;
    }
    age = ptc_ui_status_age_seconds(model, (int64_t)time(NULL));
    if (age < 0) {
        snprintf(out, out_size, "尚未刷新");
    } else if (age == 0) {
        snprintf(out, out_size, "刚刚刷新");
    } else if (age < 60) {
        snprintf(out, out_size, "上次刷新：%lld 秒前", (long long)age);
    } else {
        snprintf(out, out_size, "上次刷新：%lld 分钟前", (long long)(age / 60));
    }
}

static uint32_t status_age_color(const PtcUiModel *model)
{
    int64_t age = ptc_ui_status_age_seconds(model, (int64_t)time(NULL));
    if (model->waiting) return COLOR(215, 139, 25);
    if (age >= 30) return COLOR(194, 61, 61);
    return COLOR(91, 100, 116);
}

static const char *rule_source_label(const char *source)
{
    if (!source || !source[0]) return "尚未刷新";
    if (strcmp(source, "today_override") == 0) return "今日临时设置";
    if (strcmp(source, "statutory_holiday") == 0) return "国家法定休假日";
    if (strcmp(source, "makeup_workday") == 0) return "国家调休工作日";
    return "周计划";
}

static void draw_weekly_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    int slot;
    char detail[64];
    char freshness[64];
    uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
    format_status_age(model, freshness, sizeof(freshness));
    draw_text(pixels, stride, 54, 202, "周一到周日  |  点按模式或额度区直接修改", 16, COLOR(91, 100, 114));
    for (slot = 0; slot < 7; ++slot) {
        int day = ptc_ui_weekday_for_display_slot(slot);
        bool selected = slot == model->weekly_grid_slot && model->selected_index == 0;
        bool today = day == weekday;
        UiRect card = to_uirect(ptc_ui_weekly_day_rect(slot));
        UiRect header = to_uirect(ptc_ui_weekly_day_header_rect(slot));
        UiRect mode = to_uirect(ptc_ui_weekly_day_mode_rect(slot));
        UiRect minutes = to_uirect(ptc_ui_weekly_day_minutes_rect(slot));
        uint32_t background = day == 0 ? COLOR(255, 245, 245) : (day == 6 ? COLOR(255, 249, 238) : COLOR(255, 255, 255));
        uint32_t border = today ? COLOR(25, 132, 95) : (selected ? COLOR(28, 118, 188) : (day == 0 ? COLOR(218, 118, 118) : (day == 6 ? COLOR(220, 161, 65) : COLOR(219, 225, 233))));
        bool limited = model->draft_week[day].mode == PTC_RULE_MODE_LIMIT;
        if (selected) background = COLOR(244, 249, 255);
        fill_round_rect(pixels, stride, card, 8, background);
        draw_rect_outline(pixels, stride, card, today ? 4 : (selected ? 3 : 1), border);
        draw_text_center(pixels, stride, header, DAYS[day], 19, COLOR(28, 34, 43));
        fill_round_rect(pixels, stride, (UiRect){mode.x + 6, mode.y + 4, mode.width - 12, 28}, 14,
                        model->disable_flag_present ? COLOR(220, 224, 230) :
                        (limited ? COLOR(42, 105, 188) : COLOR(25, 132, 95)));
        draw_text_center(pixels, stride, (UiRect){mode.x + 6, mode.y + 4, mode.width - 12, 28},
                         limited ? "限时" : "不限时", 15,
                         model->disable_flag_present ? COLOR(145, 154, 168) : COLOR(255, 255, 255));
        draw_text_center(pixels, stride, (UiRect){mode.x + 4, mode.y + 32, mode.width - 8, 15},
                         today ? "今天" : " ", 12, today ? COLOR(25, 132, 95) : COLOR(91, 100, 114));
        if (limited) {
            snprintf(detail, sizeof(detail), "%u", (unsigned int)model->draft_week[day].minutes);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 16, minutes.width, 40}, detail, 28,
                             model->disable_flag_present ? COLOR(145, 154, 168) : COLOR(28, 118, 188));
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 55, minutes.width, 24}, "分钟", 14, COLOR(91, 100, 114));
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 79, minutes.width, 20}, "A / 点按", 12,
                             model->disable_flag_present ? COLOR(145, 154, 168) : COLOR(91, 100, 114));
        } else {
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 29, minutes.width, 36}, "不限时间", 18,
                             model->disable_flag_present ? COLOR(145, 154, 168) : COLOR(25, 132, 95));
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 72, minutes.width, 20}, "点按看提示", 11, COLOR(145, 154, 168));
        }
    }
    {
        UiRect panel = {838, 176, 388, 324};
        bool today_changed = ptc_ui_weekly_today_changed(model);
        fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
        draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
        fill_round_rect(pixels, stride, (UiRect){panel.x + 20, panel.y + 18, panel.width - 40, 34}, 7,
                        status_age_color(model) == COLOR(194, 61, 61) ? COLOR(255, 235, 238) : COLOR(242, 246, 250));
        draw_text_center(pixels, stride, (UiRect){panel.x + 20, panel.y + 18, panel.width - 40, 34},
                         freshness, 15, status_age_color(model));
        draw_text(pixels, stride, panel.x + 22, panel.y + 84, "今天影响预估", 20, COLOR(28, 34, 43));
        if (today_changed) {
        char current_value[48];
        char after_value[48];
        int current_minutes = model->remaining_available ? model->remaining_minutes : -1;
        if (model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "不限时");
        else format_duration(current_minutes, current_value, sizeof(current_value));
        format_rule_remaining_label(model, model->draft_week[weekday], after_value, sizeof(after_value));
        draw_time_state_card(pixels, stride, (UiRect){panel.x + 20, panel.y + 104, 160, 86},
                             "今天还可玩",
                             current_value,
                             time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                               model->unrestricted_today == 1, current_minutes));
        draw_text_center(pixels, stride, (UiRect){panel.x + 180, panel.y + 126, 28, 38}, "到", 24, COLOR(91, 100, 114));
        draw_time_state_card(pixels, stride, (UiRect){panel.x + 208, panel.y + 104, 160, 86},
                             model->today_override_present ? "恢复周计划生效后" : "保存后今天生效",
                             after_value,
                             time_state_accent(model->draft_week[weekday].mode == PTC_RULE_MODE_UNLIMITED ||
                                               model->played_minutes_available,
                                               model->draft_week[weekday].mode == PTC_RULE_MODE_UNLIMITED,
                                               model->draft_week[weekday].mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available
                                                  ? (int)model->draft_week[weekday].minutes - model->played_minutes : -1));
        draw_wrapped_text(pixels, stride, panel.x + 22, panel.y + 218,
                          model->today_override_present
                            ? "今天当前不变；预计值将在恢复周计划生效后使用。"
                            : "保存后会由后台按新计划重新计算今天。",
                          14, panel.width - 44, 20, 3, model->today_override_present ? COLOR(215, 139, 25) : COLOR(91, 100, 114));
        } else {
            draw_text(pixels, stride, panel.x + 22, panel.y + 126, "今天对应规则未修改", 19, COLOR(91, 100, 114));
            draw_wrapped_text(pixels, stride, panel.x + 22, panel.y + 158,
                              model->weekly_dirty
                                ? "本次只修改了其他日期，保存不会改变今天对应的周计划。"
                                : "选择日期编辑，或使用批量快捷操作复制规则。",
                              15, panel.width - 44, 22, 3, COLOR(91, 100, 114));
        }
        if (model->disable_flag_present) {
            draw_text(pixels, stride, panel.x + 22, panel.y + 266, "紧急停用中，当前只读", 15, COLOR(194, 61, 61));
        }
    }
    draw_candidate_button(pixels, stride, ptc_ui_weekly_page_mode_rect(), "X  切换模式",
                           COLOR(244, 246, 249), COLOR(28, 118, 188), model->selected_index == 1,
                           model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_bulk_rect(), "批量设置",
                          COLOR(244, 246, 249), COLOR(28, 118, 188), model->selected_index == 2,
                          model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_discard_rect(), "ZL  放弃",
                           COLOR(244, 246, 249), COLOR(66, 74, 86), model->selected_index == 3,
                           !model->weekly_dirty);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_save_rect(),
                           model->disable_flag_present ? "只读" : (model->weekly_dirty ? "+  保存" : "+  未修改"),
                           COLOR(28, 118, 188), COLOR(255, 255, 255), model->selected_index == 4,
                           !model->weekly_dirty || model->disable_flag_present);
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
    uint32_t bg_color = disabled ? COLOR(220, 224, 230) :
                       (is_on ? COLOR(25, 132, 95) : COLOR(190, 196, 204));
    uint32_t knob_color = disabled ? COLOR(240, 243, 246) : COLOR(255, 255, 255);
    int knob_size = rect.height - 6;
    int knob_x = is_on ? (rect.x + rect.width - 3 - knob_size) : (rect.x + 3);
    int knob_y = rect.y + 3;

    if (selected && !disabled) {
        fill_round_rect(pixels, stride, (UiRect){rect.x - 3, rect.y - 3, rect.width + 6, rect.height + 6},
                        radius + 3, COLOR(28, 118, 188));
    }
    fill_round_rect(pixels, stride, rect, radius, bg_color);
    fill_round_rect(pixels, stride, (UiRect){knob_x, knob_y, knob_size, knob_size}, knob_size / 2, knob_color);

    if (on_label && off_label) {
        const char *label = is_on ? on_label : off_label;
        uint32_t text_color = is_on ? COLOR(255, 255, 255) : COLOR(91, 100, 114);
        int label_x = is_on ? (rect.x + 12) : (rect.x + knob_size + 8);
        draw_text(pixels, stride, label_x, rect.y + rect.height / 2 + 5, label, 15, text_color);
    }
}

static void draw_holiday_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
#if 0
    UiRect top_card = to_uirect(ptc_ui_holiday_card_rect(0));
    UiRect left_card = {54, 246, 574, 198};
    UiRect right_card = {652, 246, 574, 198};
    UiRect save_button = to_uirect(ptc_ui_holiday_card_rect(6));

    UiRect mode1_rect = to_uirect(ptc_ui_holiday_card_rect(1));
    UiRect min1_rect = to_uirect(ptc_ui_holiday_card_rect(2));

    UiRect mode2_rect = to_uirect(ptc_ui_holiday_card_rect(3));
    UiRect min2_rect = to_uirect(ptc_ui_holiday_card_rect(4));

    char line[128];
    char minutes_str[64];
    bool disabled = model->disable_flag_present;

    /* Top Card (Index 0): Global Switch & Calendar Info */
    bool top_selected = (model->selected_index == 0);
    uint32_t top_bg = disabled ? COLOR(244, 246, 249) : (top_selected ? COLOR(244, 249, 255) : COLOR(255, 255, 255));
    uint32_t top_border = disabled ? COLOR(230, 233, 238) : (top_selected ? COLOR(28, 118, 188) : COLOR(219, 225, 233));
    fill_round_rect(pixels, stride, top_card, 10, top_bg);
    draw_rect_outline(pixels, stride, top_card, top_selected && !disabled ? 3 : 1, top_border);
    fill_round_rect(pixels, stride, (UiRect){top_card.x + 18, top_card.y + 16, 8, 40}, 4, COLOR(42, 105, 188));
    draw_text(pixels, stride, top_card.x + 36, top_card.y + 34, "国家节假日规则", 21, COLOR(28, 34, 43));
    draw_text(pixels, stride, top_card.x + 36, top_card.y + 55, "开启后在法定休假日与调休补班日自动应用独立规则", 14, COLOR(91, 100, 114));

    /* Calendar Status Badge */
    UiRect cal_badge = {top_card.x + 458, top_card.y + 16, 216, 32};
    uint32_t cal_bg = model->calendar_update_warning ? COLOR(255, 235, 238) : COLOR(235, 248, 242);
    uint32_t cal_fg = model->calendar_update_warning ? COLOR(194, 61, 61) : COLOR(25, 132, 95);
    fill_round_rect(pixels, stride, cal_badge, 6, cal_bg);
    {
        const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
        char badge[96];
        snprintf(badge, sizeof(badge), model->calendar_update_warning ? "内置日历即将或已经过期" : "内置日历：%u  |  v%u",
                 (unsigned int)info->last_year, (unsigned int)info->version);
        draw_text_center(pixels, stride, cal_badge, badge, 15, cal_fg);
    }

    /* Global Toggle Switch */
    draw_toggle_switch(pixels, stride, (UiRect){top_card.x + top_card.width - 100, top_card.y + 14, 80, 36},
                       model->draft_holiday_enabled, top_selected, disabled, NULL, NULL);

    /* Left Card: Statutory Holiday (Actions 1 & 2) */
    fill_round_rect(pixels, stride, left_card, 12, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, left_card, 1, COLOR(219, 225, 233));
    fill_round_rect(pixels, stride, (UiRect){left_card.x + 20, left_card.y + 18, 110, 26}, 6, COLOR(235, 248, 242));
    draw_text_center(pixels, stride, (UiRect){left_card.x + 20, left_card.y + 18, 110, 26}, "法定休假", 16, COLOR(25, 132, 95));
    draw_text(pixels, stride, left_card.x + 140, left_card.y + 36, "元旦、春节、清明、劳动、端午、中秋、国庆等", 15, COLOR(91, 100, 114));

    /* Sub-card 1: Mode Toggle (Action 1) */
    bool sel1 = (model->selected_index == 1);
    uint32_t bg1 = disabled ? COLOR(244, 246, 249) : (sel1 ? COLOR(244, 249, 255) : COLOR(248, 250, 252));
    fill_round_rect(pixels, stride, mode1_rect, 8, bg1);
    draw_rect_outline(pixels, stride, mode1_rect, sel1 && !disabled ? 2 : 1, sel1 && !disabled ? COLOR(28, 118, 188) : COLOR(228, 233, 240));
    draw_text(pixels, stride, mode1_rect.x + 16, mode1_rect.y + 30, "规则模式", 17, COLOR(28, 34, 43));
    draw_text(pixels, stride, mode1_rect.x + 110, mode1_rect.y + 30,
              model->draft_holiday_rule.mode == PTC_RULE_MODE_UNLIMITED ? "不限时模式" : "限时模式",
              16, model->draft_holiday_rule.mode == PTC_RULE_MODE_UNLIMITED ? COLOR(25, 132, 95) : COLOR(42, 105, 188));
    draw_toggle_switch(pixels, stride, (UiRect){mode1_rect.x + mode1_rect.width - 92, mode1_rect.y + 7, 76, 34},
                       model->draft_holiday_rule.mode == PTC_RULE_MODE_UNLIMITED, sel1, disabled, NULL, NULL);

    /* Sub-card 2: Minutes Quota (Action 2) */
    bool sel2 = (model->selected_index == 2);
    uint32_t bg2 = disabled ? COLOR(244, 246, 249) : (sel2 ? COLOR(244, 249, 255) : COLOR(248, 250, 252));
    fill_round_rect(pixels, stride, min1_rect, 8, bg2);
    draw_rect_outline(pixels, stride, min1_rect, sel2 && !disabled ? 2 : 1, sel2 && !disabled ? COLOR(28, 118, 188) : COLOR(228, 233, 240));
    draw_text(pixels, stride, min1_rect.x + 16, min1_rect.y + 25, "每日限时额度", 15, COLOR(91, 100, 114));
    if (model->draft_holiday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
        draw_text(pixels, stride, min1_rect.x + 16, min1_rect.y + 58, "不限时间", 23, COLOR(140, 148, 160));
        draw_text(pixels, stride, min1_rect.x + 16, min1_rect.y + 84, "如需具体额度请先切换模式", 13, COLOR(160, 168, 180));
    } else {
        unsigned int mins = model->draft_holiday_rule.minutes;
        snprintf(minutes_str, sizeof(minutes_str), "%u 分钟  (%u小时%u分)", mins, mins / 60, mins % 60);
        draw_text(pixels, stride, min1_rect.x + 16, min1_rect.y + 60, minutes_str, 22, COLOR(25, 132, 95));
        UiRect btn2 = {min1_rect.x + min1_rect.width - 170, min1_rect.y + 50, 154, 34};
        fill_round_rect(pixels, stride, btn2, 6, sel2 ? COLOR(28, 118, 188) : COLOR(230, 240, 252));
        draw_text_center(pixels, stride, btn2, "A / 点按  修改额度", 15, sel2 ? COLOR(255, 255, 255) : COLOR(28, 118, 188));
    }

    /* Right Card: Makeup Workday (Actions 3 & 4) */
    fill_round_rect(pixels, stride, right_card, 12, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, right_card, 1, COLOR(219, 225, 233));
    fill_round_rect(pixels, stride, (UiRect){right_card.x + 20, right_card.y + 18, 110, 26}, 6, COLOR(255, 244, 230));
    draw_text_center(pixels, stride, (UiRect){right_card.x + 20, right_card.y + 18, 110, 26}, "调休工作日", 16, COLOR(215, 139, 25));
    draw_text(pixels, stride, right_card.x + 140, right_card.y + 36, "因节假日调休产生的补班工作日", 15, COLOR(91, 100, 114));

    /* Sub-card 3: Mode Toggle (Action 3) */
    bool sel3 = (model->selected_index == 3);
    uint32_t bg3 = disabled ? COLOR(244, 246, 249) : (sel3 ? COLOR(244, 249, 255) : COLOR(248, 250, 252));
    fill_round_rect(pixels, stride, mode2_rect, 8, bg3);
    draw_rect_outline(pixels, stride, mode2_rect, sel3 && !disabled ? 2 : 1, sel3 && !disabled ? COLOR(28, 118, 188) : COLOR(228, 233, 240));
    draw_text(pixels, stride, mode2_rect.x + 16, mode2_rect.y + 30, "规则模式", 17, COLOR(28, 34, 43));
    draw_text(pixels, stride, mode2_rect.x + 110, mode2_rect.y + 30,
              model->draft_makeup_workday_rule.mode == PTC_RULE_MODE_UNLIMITED ? "不限时模式" : "限时模式",
              16, model->draft_makeup_workday_rule.mode == PTC_RULE_MODE_UNLIMITED ? COLOR(215, 139, 25) : COLOR(42, 105, 188));
    draw_toggle_switch(pixels, stride, (UiRect){mode2_rect.x + mode2_rect.width - 92, mode2_rect.y + 7, 76, 34},
                       model->draft_makeup_workday_rule.mode == PTC_RULE_MODE_UNLIMITED, sel3, disabled, NULL, NULL);

    /* Sub-card 4: Minutes Quota (Action 4) */
    bool sel4 = (model->selected_index == 4);
    uint32_t bg4 = disabled ? COLOR(244, 246, 249) : (sel4 ? COLOR(244, 249, 255) : COLOR(248, 250, 252));
    fill_round_rect(pixels, stride, min2_rect, 8, bg4);
    draw_rect_outline(pixels, stride, min2_rect, sel4 && !disabled ? 2 : 1, sel4 && !disabled ? COLOR(28, 118, 188) : COLOR(228, 233, 240));
    draw_text(pixels, stride, min2_rect.x + 16, min2_rect.y + 25, "每日限时额度", 15, COLOR(91, 100, 114));
    if (model->draft_makeup_workday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
        draw_text(pixels, stride, min2_rect.x + 16, min2_rect.y + 58, "不限时间", 23, COLOR(140, 148, 160));
        draw_text(pixels, stride, min2_rect.x + 16, min2_rect.y + 84, "如需具体额度请先切换模式", 13, COLOR(160, 168, 180));
    } else {
        unsigned int mins = model->draft_makeup_workday_rule.minutes;
        snprintf(minutes_str, sizeof(minutes_str), "%u 分钟  (%u小时%u分)", mins, mins / 60, mins % 60);
        draw_text(pixels, stride, min2_rect.x + 16, min2_rect.y + 60, minutes_str, 22, COLOR(215, 139, 25));
        UiRect btn4 = {min2_rect.x + min2_rect.width - 170, min2_rect.y + 50, 154, 34};
        fill_round_rect(pixels, stride, btn4, 6, sel4 ? COLOR(28, 118, 188) : COLOR(230, 240, 252));
        draw_text_center(pixels, stride, btn4, "A / 点按  修改额度", 15, sel4 ? COLOR(255, 255, 255) : COLOR(28, 118, 188));
    }

    /* Save state is explicit, and the calendar button previews this draft. */
    snprintf(line, sizeof(line), "今天生效来源：%s%s",
             model->rule_source[0] ? model->rule_source : "尚未刷新",
             model->holiday_dirty ? "   |  * 有未保存的修改" : "");
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(5), "查看当前节假日安排",
                          COLOR(244, 246, 249), COLOR(28, 118, 188), model->selected_index == 5, false);

    /* Save Button */
    bool sel5 = (model->selected_index == 6);
    bool save_disabled = disabled || model->waiting || !model->holiday_dirty;
    uint32_t save_bg = save_disabled ? COLOR(244, 246, 249) : (sel5 ? COLOR(20, 90, 160) : COLOR(28, 118, 188));
    uint32_t save_fg = save_disabled ? COLOR(160, 168, 180) : COLOR(255, 255, 255);
    fill_round_rect(pixels, stride, save_button, 10, save_bg);
    if (sel5 && !save_disabled) {
        draw_rect_outline(pixels, stride, save_button, 3, COLOR(25, 132, 95));
    }
    const char *save_text = disabled ? "紧急停用中，国家节假日设置只读" :
                            (model->waiting ? "正在保存..." :
                            (model->holiday_dirty ? "有尚未保存的更改  |  +  保存全部节假日设置" : "+  设置未修改"));
    draw_text_center(pixels, stride, save_button, save_text, 20, save_fg);
    draw_notice(pixels, stride, model, 522, 128);
#endif
    UiRect panel = {838, 176, 384, 324};
    UiRect top_card = to_uirect(ptc_ui_holiday_card_rect(0));
    const char *titles[] = {"法定休假", "调休工作日"};
    const char *descriptions[] = {"主要法定节假日的休假日期", "节假日调休产生的补班日期"};
    const uint32_t accents[] = {COLOR(25, 132, 95), COLOR(215, 139, 25)};
    char line[160];
    char priority_summary[160];
    char minutes_str[64];
    bool disabled = model->disable_flag_present;
    bool top_selected = model->selected_index == 0;
    fill_round_rect(pixels, stride, top_card, 10,
                    disabled ? COLOR(244, 246, 249) : (top_selected ? COLOR(244, 249, 255) : COLOR(255, 255, 255)));
    draw_rect_outline(pixels, stride, top_card, top_selected ? 3 : 1,
                      top_selected ? COLOR(28, 118, 188) : COLOR(219, 225, 233));
    draw_text(pixels, stride, top_card.x + 18, top_card.y + 28, "国家节假日规则", 20, COLOR(28, 34, 43));
    draw_text(pixels, stride, top_card.x + 18, top_card.y + 54, "开启后自动应用法定休假与调休工作日规则", 14, COLOR(91, 100, 114));
    {
        const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
        snprintf(line, sizeof(line), "内置日历：%u  |  v%u", (unsigned int)info->last_year, (unsigned int)info->version);
        draw_text(pixels, stride, top_card.x + 498, top_card.y + 31, line, 14,
                  model->calendar_update_warning ? COLOR(194, 61, 61) : COLOR(25, 132, 95));
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
        fill_round_rect(pixels, stride, card, 10,
                        disabled ? COLOR(244, 246, 249) : (selected ? COLOR(244, 249, 255) : COLOR(255, 255, 255)));
        draw_rect_outline(pixels, stride, card, selected ? 3 : 1,
                          selected ? COLOR(28, 118, 188) : COLOR(219, 225, 233));
        draw_text(pixels, stride, card.x + 16, card.y + 30, titles[index], 19, accents[index]);
        draw_text(pixels, stride, card.x + 16, card.y + 57, descriptions[index], 12, COLOR(91, 100, 114));
        fill_round_rect(pixels, stride, mode, 18,
                        disabled ? COLOR(220, 224, 230) : (limited ? COLOR(42, 105, 188) : COLOR(25, 132, 95)));
        draw_text_center(pixels, stride, mode, limited ? "限时" : "不限时", 14,
                         disabled ? COLOR(145, 154, 168) : COLOR(255, 255, 255));
        fill_round_rect(pixels, stride, minutes, 8, disabled || !limited ? COLOR(238, 241, 245) : COLOR(248, 250, 252));
        if (limited) {
            snprintf(minutes_str, sizeof(minutes_str), "%u 分钟（%u小时%u分）", (unsigned int)rule.minutes,
                     (unsigned int)rule.minutes / 60, (unsigned int)rule.minutes % 60);
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 32, minutes_str, 21,
                      disabled ? COLOR(145, 154, 168) : accents[index]);
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 61, "A / 点按修改额度", 13,
                      disabled ? COLOR(145, 154, 168) : COLOR(91, 100, 114));
        } else {
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 32, "不限时间", 21, COLOR(145, 154, 168));
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 61, "点按后提示先切换为限时", 13, COLOR(160, 168, 180));
        }
    }
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(3), "X  切换模式",
                           COLOR(244, 246, 249), COLOR(28, 118, 188), model->selected_index == 3, disabled);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(4), "ZL  放弃",
                           COLOR(244, 246, 249), COLOR(66, 74, 86), model->selected_index == 4,
                           !model->holiday_dirty);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(5),
                           disabled ? "紧急停用中，设置只读" : (model->waiting ? "正在保存..." :
                           (model->holiday_dirty ? "+  保存" : "+  未修改")),
                           COLOR(28, 118, 188), COLOR(255, 255, 255), model->selected_index == 5,
                           disabled || model->waiting || !model->holiday_dirty);
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, panel.x + 24, panel.y + 38, "节假日状态", 22, COLOR(28, 34, 43));
    snprintf(line, sizeof(line), "已保存：%s   草稿：%s%s",
             model->holiday_enabled ? "已开启" : "未开启", model->draft_holiday_enabled ? "已开启" : "未开启",
             model->holiday_dirty ? "  |  未保存" : "");
    draw_text(pixels, stride, panel.x + 24, panel.y + 78, line, 16, COLOR(91, 100, 114));
    snprintf(line, sizeof(line), "今天生效来源：%s", rule_source_label(model->rule_source));
    draw_text(pixels, stride, panel.x + 24, panel.y + 110, line, 16, COLOR(28, 118, 188));
    {
        const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
        snprintf(line, sizeof(line), "内置日历：至 %u 年  |  v%u", info->last_year, info->version);
        draw_text(pixels, stride, panel.x + 24, panel.y + 144, line, 16,
                  model->calendar_update_warning ? COLOR(194, 61, 61) : COLOR(25, 132, 95));
    }
    draw_candidate_button(pixels, stride, ptc_ui_holiday_calendar_rect(), "查看当前节假日安排",
                           COLOR(244, 249, 255), COLOR(28, 118, 188), model->selected_index == 6, false);
    ptc_ui_format_holiday_priority_summary(model, priority_summary, sizeof(priority_summary));
    fit_text(line, sizeof(line), priority_summary, 13, panel.width - 48);
    draw_text(pixels, stride, panel.x + 24, panel.y + 244, line, 13, COLOR(28, 118, 188));
    draw_text(pixels, stride, panel.x + 24, panel.y + 268, "1  今日临时设置：存在时优先", 12,
              model->today_override_present ? COLOR(25, 132, 95) : COLOR(91, 100, 114));
    draw_text(pixels, stride, panel.x + 24, panel.y + 288, "2  国家节假日：开启且日期命中", 12,
              !model->today_override_present && model->holiday_enabled &&
              (strcmp(model->rule_source, "statutory_holiday") == 0 ||
               strcmp(model->rule_source, "makeup_workday") == 0)
                ? COLOR(25, 132, 95) : COLOR(91, 100, 114));
    snprintf(priority_summary, sizeof(priority_summary), "3  周计划：关闭、普通日或未覆盖时回退");
    fit_text(line, sizeof(line), priority_summary, 12, panel.width - 48);
    draw_text(pixels, stride, panel.x + 24, panel.y + 308, line, 12,
              !model->today_override_present && strcmp(model->rule_source, "week") == 0
                ? COLOR(25, 132, 95) : COLOR(91, 100, 114));
    draw_notice(pixels, stride, model, 522, 128);
}

static void draw_parent(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const UiAction *actions;
    int action_count;
    int index;
    draw_header(pixels, stride,
                model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT
                    ? "支持与恢复" :
                model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED
                    ? "高级设置" : "家长时间管理",
                model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT
                    ? "兼容状态、诊断与安全恢复" :
                model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED
                    ? "高级启动方式与兼容性选项" : "本地规则与设备安全设置");
    draw_disable_banner(pixels, stride, model);
    if (model->demo_secret_enabled) {
        UiRect warning = model->disable_flag_present ? (UiRect){526, 42, 246, 38} : (UiRect){900, 42, 326, 36};
        fill_round_rect(pixels, stride, warning, 7, COLOR(255, 235, 238));
        draw_text_center(pixels, stride, warning,
                         model->disable_flag_present ? "公共演示密钥已启用" : "公共演示密钥已启用  |  低安全模式",
                         17, COLOR(194, 61, 61));
    }
    draw_tabs(pixels, stride, model);
    if (model->parent_page == PTC_UI_PARENT_SETTINGS &&
        model->settings_page == PTC_UI_SETTINGS_ADVANCED) {
        draw_settings_hierarchy(pixels, stride, ptc_ui_advanced_hierarchy_rect(),
                                ptc_ui_advanced_back_rect(), "设置 / 高级设置");
    } else if (model->parent_page == PTC_UI_PARENT_SETTINGS &&
               model->settings_page == PTC_UI_SETTINGS_SUPPORT) {
        draw_settings_hierarchy(pixels, stride, ptc_ui_support_hierarchy_rect(),
                                ptc_ui_support_back_rect(), "设置 / 支持与恢复");
    }
    if (model->parent_page != PTC_UI_PARENT_PLAN && model->parent_page != PTC_UI_PARENT_HOLIDAY) {
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
                ? (model->settings_page == PTC_UI_SETTINGS_ADVANCED && index == 0
                    ? ptc_ui_advanced_card_rect()
                    : (model->settings_page == PTC_UI_SETTINGS_SUPPORT
                        ? ptc_ui_support_card_rect(index) : ptc_ui_parent_card_rect(index)))
                : ptc_ui_parent_card_rect(index));
            PtcUiActionState astate = PTC_UI_ACTION_AVAILABLE;
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT) {
                if (!ptc_ui_safety_action_visible(model, index)) continue;
                astate = ptc_ui_safety_action_available(model, index);
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
            }
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ROOT && index == 0) {
                dynamic_action = *action;
                dynamic_action.subtitle = ptc_ui_theme_preference_label(g_theme.preference);
                action = &dynamic_action;
            }
            draw_action_card(pixels, stride, card, action, index == model->selected_index, astate,
                             model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED && index == 0 ? 100 : 0);
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ADVANCED && index == 0) {
                const char *state_label = "状态未知";
                uint32_t state_color = COLOR(194, 61, 61);
                if (model->album_restriction_state == 0) {
                    state_label = "未开启";
                    state_color = COLOR(91, 100, 116);
                } else if (model->album_restriction_state == 1) {
                    state_label = "已开启";
                    state_color = COLOR(25, 132, 95);
                } else if (model->album_restriction_state == 2) {
                    state_label = "需要处理";
                    state_color = COLOR(215, 139, 25);
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
                    state_label = "外部配置";
                    state_color = COLOR(28, 118, 188);
                }
                UiRect badge = {card.x + card.width - 104, card.y + 10, 88, 28};
                fill_round_rect(pixels, stride, badge, 6, COLOR(244, 246, 249));
                draw_text_center(pixels, stride, badge, state_label, 13, state_color);
            }
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_ROOT && index == 4) {
                const char *label = ptc_ui_settings_status_label(model);
                if (label) {
                    UiRect badge = {card.x + card.width - 210, card.y + 12, 88, 28};
                    uint32_t color = strcmp(label, "需处理") == 0 ? COLOR(194, 61, 61) : COLOR(215, 139, 25);
                    fill_round_rect(pixels, stride, badge, 6, COLOR(244, 246, 249));
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
    }
    if (model->parent_page == PTC_UI_PARENT_SETTINGS && model->settings_page == PTC_UI_SETTINGS_SUPPORT &&
        model->diagnostic_status != PTC_UI_DIAGNOSTIC_IDLE) {
        draw_diagnostic_notice(pixels, stride, model);
    } else if (model->parent_page != PTC_UI_PARENT_PLAN && model->parent_page != PTC_UI_PARENT_HOLIDAY) {
        draw_notice(pixels, stride, model, 522, 128);
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
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_parent_footer_rect(3)), 3, COLOR(28, 118, 188));
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
    *dialog = (UiRect){(SCREEN_WIDTH - width) / 2, (SCREEN_HEIGHT - height) / 2 - 10, width, height};
    fill_rect_packed(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT},
                     pack_rgb(g_resolved_theme == PTC_UI_RESOLVED_DARK
                         ? g_palette->page_bg : 0xE2E6EC));
    fill_round_rect(pixels, stride, *dialog, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, *dialog, 2, COLOR(203, 211, 222));
    draw_text(pixels, stride, dialog->x + 34, dialog->y + 54, title, 29, COLOR(28, 34, 43));
    if (description[0]) {
        draw_wrapped_text(pixels, stride, dialog->x + 34, dialog->y + 88, description,
                          18, dialog->width - 68, 26, 6, COLOR(91, 100, 114));
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
    fill_round_rect(pixels, stride, value_box, 8, COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, value_box, 2, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, value_box, value, 39, COLOR(28, 118, 188));
    draw_dialog_button(pixels, stride, ptc_ui_minutes_dec_rect(), "-5", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_inc_rect(), "+5", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_inc_large_rect(), "+15", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_dec_large_rect(), "-15", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 242, 640, 26}, duration, 19,
                     COLOR(28, 118, 188));

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
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 316, 640, 24}, date_line, 17, COLOR(77, 86, 99));
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 44, dialog.y + 344, 304, 38}, 7, COLOR(248, 250, 253));
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 372, dialog.y + 344, 304, 38}, 7, COLOR(248, 250, 253));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 44, dialog.y + 344, 304, 38}, played_line, 17, COLOR(77, 86, 99));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 372, dialog.y + 344, 304, 38}, remaining_line, 17,
                     model->unrestricted_today == 1 ? COLOR(25, 132, 95) : COLOR(77, 86, 99));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 390, 640, 30}, preview_line, 20, COLOR(25, 132, 95));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 423, 640, 22}, freshness, 16,
                     status_age_color(model));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 50, dialog.y + 448, 620, 22},
                     "本次修改只影响今天  |  Y 或点击数值手动输入", 16, COLOR(77, 86, 99));
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
        uint32_t border = day == model->editor_index ? COLOR(28, 118, 188) : COLOR(219, 225, 233);
        char minutes[32];
        fill_round_rect(pixels, stride, card, 8, day == model->editor_index ? COLOR(244, 249, 255) : COLOR(250, 251, 253));
        draw_rect_outline(pixels, stride, card, day == model->editor_index ? 3 : 1, border);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 14, card.width, 34}, DAYS[day], 21, COLOR(28, 34, 43));
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 69, card.width, 34}, rule_mode_label(model->draft_week[day].mode), 22,
                         COLOR(28, 118, 188));
        if (model->draft_week[day].mode == PTC_RULE_MODE_LIMIT) {
            snprintf(minutes, sizeof(minutes), "%u 分钟", (unsigned int)model->draft_week[day].minutes);
        } else {
            snprintf(minutes, sizeof(minutes), "--");
        }
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 119, card.width, 34}, minutes, 19, COLOR(77, 86, 99));
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 80, dialog.y + 320, dialog.width - 160, 30},
                     "选择日期后：X 切换模式，Y 或点数值手动输入", 19, COLOR(77, 86, 99));
    draw_dialog_button(pixels, stride, ptc_ui_weekly_mode_rect(), "X 切换模式", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    if (model->draft_week[model->editor_index].mode == PTC_RULE_MODE_LIMIT) {
        snprintf(selected_minutes, sizeof(selected_minutes), "%u 分钟",
                 (unsigned int)model->draft_week[model->editor_index].minutes);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_up_rect(), "+15", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_down_rect(), "-15", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_dec_rect(), "-5", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_inc_rect(), "+5", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_input_rect(), selected_minutes,
                           COLOR(244, 249, 255), COLOR(28, 118, 188), true);
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
        size_t len = strlen(model->numpad_text);
        size_t pos = 0;
        for (int i = 0; i < 8; ++i) {
            if (i > 0) shown[pos++] = ' ';
            if (i < (int)len) {
                shown[pos++] = model->numpad_text[i];
            } else {
                shown[pos++] = '_';
            }
        }
        shown[pos] = '\0';
    } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        snprintf(shown, sizeof(shown), "输入加时码");
    } else if (model->numpad_text[0]) {
        snprintf(shown, sizeof(shown), "%s", model->numpad_text);
    } else {
        snprintf(shown, sizeof(shown), "%.*s", model->numpad_max_digits, "________");
    }
    fill_round_rect(pixels, stride, display, 8, COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, display, 2, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, display, shown, 30, COLOR(28, 118, 188));

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
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 218, dialog.width - 80, 22}, current, 16, COLOR(91, 100, 114));
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
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 32, dialog.y + 242, 266, 32}, 6, COLOR(248, 250, 253));
        draw_rect_outline(pixels, stride, (UiRect){dialog.x + 32, dialog.y + 242, 266, 32}, 1,
                          time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                            model->unrestricted_today == 1, model->remaining_minutes));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 244, 258, 28}, left, 14, COLOR(66, 74, 86));
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 322, dialog.y + 242, 266, 32}, 6, COLOR(248, 250, 253));
        draw_rect_outline(pixels, stride, (UiRect){dialog.x + 322, dialog.y + 242, 266, 32}, 1,
                          time_state_accent(model->played_minutes_available, false, after_minutes));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 326, dialog.y + 244, 258, 28}, right,
                         model->today_override_present ? 12 : 14, COLOR(66, 74, 86));
    } else if (duration[0]) {
        char duration_line[80];
        snprintf(duration_line, sizeof(duration_line), "换算：%s", duration);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 244, dialog.width - 80, 22},
                         duration_line, 17, COLOR(28, 118, 188));
    }
    if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
        model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES ||
        model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
        model->numpad_purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) {
        for (index = 0; index < 4; ++index) {
            draw_dialog_button(pixels, stride, ptc_ui_numpad_quick_rect(index), QUICK_LABELS[index],
                               COLOR(235, 238, 243), COLOR(28, 118, 188), true);
        }
    }
    for (index = 0; index < 12; ++index) {
        UiRect key = to_uirect(ptc_ui_numpad_key_rect(index));
        bool selected = index == model->numpad_cursor;
        fill_round_rect(pixels, stride, key, 8, selected ? COLOR(230, 242, 255) : COLOR(250, 251, 253));
        draw_rect_outline(pixels, stride, key, selected ? 3 : 1,
                          selected ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
        draw_text_center(pixels, stride, key, KEY_LABELS[index], index == 9 || index == 11 ? 18 : 30,
                         selected ? COLOR(28, 118, 188) : COLOR(66, 74, 86));
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 35, dialog.y + 548, dialog.width - 70, 24},
                     "方向键/摇杆选择  A 输入  X 退格  Y 清空  + 完成", 17, COLOR(77, 86, 99));
    if (model->numpad_error[0]) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 35, dialog.y + 576, dialog.width - 70, 24},
                         model->numpad_error, 17, COLOR(194, 61, 61));
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
    fill_round_rect(pixels, stride, display, 8, COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, display, 2, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, display, mask[0] ? mask : "输入内容只显示为圆点", mask[0] ? 26 : 18,
                     mask[0] ? COLOR(28, 118, 188) : COLOR(91, 100, 114));
    snprintf(count, sizeof(count), "已输入 %u 位", (unsigned int)strlen(model->pin_text));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 188, 480, 24}, count, 17, COLOR(91, 100, 114));

    draw_text(pixels, stride, dialog.x + 40, dialog.y + 222, "摇杆方向映射", 20, COLOR(28, 34, 43));
    {
        int i;
        int cx = dialog.x + 180;
        int cy = dialog.y + 338;
        fill_round_rect(pixels, stride, (UiRect){cx - 48, cy - 48, 96, 96}, 48, COLOR(250, 251, 253));
        draw_circle_outline(pixels, stride, cx, cy, 48, 2, COLOR(28, 118, 188));
        draw_circle_outline(pixels, stride, cx, cy, 36, 2, COLOR(203, 211, 222));
        draw_text_center(pixels, stride, (UiRect){cx - 32, cy - 18, 64, 36}, "摇杆", 13, COLOR(28, 118, 188));
        
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
            draw_text_center(pixels, stride, cell, label, 22, COLOR(28, 118, 188));
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
            fill_round_rect(pixels, stride, cell, 26, COLOR(250, 251, 253));
            draw_circle_outline(pixels, stride, bcx + dx, bcy + dy, 26, 2,
                                digit >= 0 ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
            if (digit >= 0) {
                char digit_label[4];
                snprintf(digit_label, sizeof(digit_label), "%d", digit);
                draw_text_center(pixels, stride, (UiRect){cell.x, cell.y + 3, cell.width, 21},
                                 button, 12, COLOR(91, 100, 114));
                draw_text_center(pixels, stride, (UiRect){cell.x, cell.y + 19, cell.width, 28},
                                 digit_label, 20, COLOR(28, 118, 188));
            }
        }
    }
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 452, "左右摇杆均可输入；十字键支持上、右、下、左", 16, COLOR(77, 86, 99));

    draw_text(pixels, stride, dialog.x + 590, dialog.y + 212, "触摸数字键盘", 20, COLOR(28, 34, 43));
    for (row = 0; row < 10; ++row) {
        UiRect key = to_uirect(ptc_ui_pin_key_rect(row));
        bool selected = model->pin_focus == row;
        char label[4];
        snprintf(label, sizeof(label), "%d", row);
        fill_round_rect(pixels, stride, key, 7, selected ? COLOR(230, 242, 255) : COLOR(250, 251, 253));
        draw_rect_outline(pixels, stride, key, selected ? 2 : 1,
                          selected ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
        draw_text_center(pixels, stride, key, label, 24, selected ? COLOR(28, 118, 188) : COLOR(66, 74, 86));
    }
    draw_dialog_button(pixels, stride, ptc_ui_pin_backspace_rect(), "ZL  退格",
                       COLOR(255, 244, 230), COLOR(180, 103, 15), true);
    draw_dialog_button(pixels, stride, ptc_ui_pin_confirm_rect(), "+  确认",
                       COLOR(28, 118, 188), COLOR(255, 255, 255), false);
    draw_dialog_button(pixels, stride, ptc_ui_pin_cancel_rect(), "B  取消",
                       COLOR(235, 238, 243), COLOR(66, 74, 86), true);
    draw_dialog_button(pixels, stride, ptc_ui_pin_keyboard_rect(), "长按 + 传统键盘",
                       COLOR(235, 238, 243), COLOR(66, 74, 86), true);
    draw_text(pixels, stride, dialog.x + 590, dialog.y + 594,
              model->pin_error[0] ? model->pin_error : "短按 + 确认；长按 + 约 1 秒切换传统键盘",
              16, model->pin_error[0] ? COLOR(194, 61, 61) : COLOR(91, 100, 114));
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
                 "本次增加 %d 分钟；今天有效，成功兑换后只能使用一次。",
                 model->code_grant_minutes);
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
        draw_text_center(pixels, stride, (UiRect){dialog.x + 354, dialog.y + 166, 52, 42}, "到", 28, COLOR(91, 100, 114));
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
        draw_text_center(pixels, stride, (UiRect){dialog.x + 354, dialog.y + 166, 52, 42}, "到", 28, COLOR(91, 100, 114));
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
                             model->played_minutes_available ? COLOR(28, 118, 188) : COLOR(215, 139, 25));
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
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 218, 652, 92}, 8, COLOR(255, 232, 235));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 226, 652, 34}, comparison, 25, COLOR(194, 61, 61));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 264, 652, 34},
                         "新额度不高于额度消耗估算，保存后会马上限制儿童使用", 20, COLOR(194, 61, 61));
    }
    if (restore || limit_change || code_preview) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54}, 8,
                        model->confirm_hold_required ? COLOR(255, 232, 235) : COLOR(255, 247, 229));
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
                snprintf(warning, sizeof(warning), "受每日 1440 分钟上限影响，实际增加 %d 分钟",
                         model->code_effective_add_minutes);
            } else {
                snprintf(warning, sizeof(warning), "确认后才会生效并消费这枚一次性加时码");
            }
            draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                             warning, 18, model->confirm_hold_required ? COLOR(194, 61, 61) : COLOR(170, 109, 18));
        } else if (limit_change) {
            char risk[160];
            char recovery[128];
            ptc_ui_format_today_limit_confirmation(model, risk, sizeof(risk), recovery, sizeof(recovery));
            draw_text_center(pixels, stride, (UiRect){dialog.x + 64, dialog.y + 254, 632, 24},
                             risk, 15, model->confirm_hold_required ? COLOR(194, 61, 61) : COLOR(170, 109, 18));
            draw_text_center(pixels, stride, (UiRect){dialog.x + 64, dialog.y + 280, 632, 22},
                             recovery, 15, COLOR(77, 86, 99));
        } else {
            draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                             model->confirm_hold_required
                                ? (model->played_minutes_available ? "操作后可能立即限制游玩，请长按 A 确认" : "无法取得额度消耗估算，不能判断是否立即限制")
                                : (limit_change && model->unrestricted_today == 1 ? "不限时将改为限时，请确认状态变化" : "请确认状态变化"),
                             19, model->confirm_hold_required ? COLOR(194, 61, 61) : COLOR(170, 109, 18));
        }
    } else if (!album_change && (!model->confirm_hold_required || !model->played_minutes_available)) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 230, 620, 72}, 8,
                        danger ? COLOR(255, 240, 240) : COLOR(240, 248, 244));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 230, 620, 72},
                         danger ? "请确认已了解这项操作的影响" : "确认执行这项操作", 22,
                         danger ? COLOR(194, 61, 61) : COLOR(25, 132, 95));
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
        fill_round_rect(pixels, stride, rect, 8, selected ? COLOR(230, 242, 255) : COLOR(244, 249, 255));
        draw_rect_outline(pixels, stride, rect, selected ? 3 : 1,
                          selected ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
        draw_text_center(pixels, stride, rect,
                         field == PTC_UI_DURATION_HOURS ? hours_value : minutes_value,
                         25, selected ? COLOR(28, 118, 188) : COLOR(66, 74, 86));
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 218, 350, 28},
                     total_value, 19, entered_valid ? COLOR(28, 118, 188) : COLOR(194, 61, 61));
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
                } else if (!model->holiday_enabled) {
                    snprintf(holiday_detail, sizeof(holiday_detail), "今日日期命中：%s（保存后启用）",
                             match.arrangement ? match.arrangement->display_name : kind);
                } else if (model->today_override_present) {
                    snprintf(holiday_detail, sizeof(holiday_detail), "今日日期命中：%s（被临时设置覆盖）",
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
        draw_text(pixels, stride, dialog.x + 548, dialog.y + 276, holiday_line, 17, COLOR(28, 34, 43));
        fit_text(fitted, sizeof(fitted), holiday_detail, 15, 326);
        draw_text(pixels, stride, dialog.x + 548, dialog.y + 310, fitted, 15,
                  today_match ? COLOR(25, 132, 95) : COLOR(77, 86, 99));
        draw_text(pixels, stride, dialog.x + 548, dialog.y + 344,
                  model->draft_holiday_enabled ? "当前预设：已开启" : "当前预设：未开启", 16,
                  model->draft_holiday_enabled ? COLOR(25, 132, 95) : COLOR(215, 139, 25));
        draw_text(pixels, stride, dialog.x + 548, dialog.y + 378,
                  model->today_override_present ? "今天临时设置优先，当前额度可能未生效" : "节假日规则命中后优先于周计划",
                  15, model->today_override_present ? COLOR(215, 139, 25) : COLOR(91, 100, 114));
    } else {
        static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        uint8_t today_weekday = ptc_weekday_from_day_index(model->day_index);
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 264, 350, 74}, "额度已耗（估算）", played,
                             model->played_minutes_available ? COLOR(28, 118, 188) : COLOR(215, 139, 25));
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
                             15, COLOR(91, 100, 114));
        } else {
            draw_text_center(pixels, stride, (UiRect){dialog.x + 536, dialog.y + 518, 350, 28}, freshness, 15,
                             status_age_color(model));
        }
    }

    for (int index = 0; index < 4; ++index) {
        draw_dialog_button(pixels, stride, ptc_ui_minute_editor_quick_rect(index), QUICK_LABELS[index],
                           COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    }
    for (int index = 0; index < 12; ++index) {
        UiRect key = to_uirect(ptc_ui_minute_editor_key_rect(index));
        bool selected = index == model->numpad_cursor;
        fill_round_rect(pixels, stride, key, 8, selected ? COLOR(230, 242, 255) : COLOR(250, 251, 253));
        draw_rect_outline(pixels, stride, key, selected ? 3 : 1,
                          selected ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
        draw_text_center(pixels, stride, key, KEY_LABELS[index], index == 9 || index == 11 ? 16 : 28,
                         selected ? COLOR(28, 118, 188) : COLOR(66, 74, 86));
    }
    if (model->numpad_error[0]) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 488, 450, 28},
                         model->numpad_error, 16, COLOR(194, 61, 61));
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 518, 450, 24},
                     "Minus 切换小时/分钟", 15, COLOR(77, 86, 99));
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
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 122, "当前值", 17, COLOR(91, 100, 114));
    fill_round_rect(pixels, stride, current_box, 7, COLOR(244, 246, 249));
    if (model->credential_kind == 2 && model->credential_revealed) {
        if (strlen(current) > 32U) {
            char first[33];
            memcpy(first, current, 32U);
            first[32] = '\0';
            draw_text(pixels, stride, current_box.x + 16, current_box.y + 23, first, 14, COLOR(45, 52, 62));
            draw_text(pixels, stride, current_box.x + 16, current_box.y + 44, current + 32, 14, COLOR(45, 52, 62));
        } else {
            draw_text(pixels, stride, current_box.x + 16, current_box.y + 36, current, 14, COLOR(45, 52, 62));
        }
    } else {
        draw_text(pixels, stride, current_box.x + 16, current_box.y + 36, current, 18, COLOR(45, 52, 62));
    }
    if (model->credential_kind == 2) {
        draw_candidate_button(pixels, stride, ptc_ui_credential_reveal_rect(),
                              model->credential_revealed ? "ZR  隐藏当前密钥" : "ZR  显示当前密钥",
                              COLOR(244, 246, 249), COLOR(28, 118, 188),
                              model->overlay_selection == PTC_UI_CREDENTIAL_REVEAL, false);
    }
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 215, "新值", 17, COLOR(91, 100, 114));
    fill_round_rect(pixels, stride, input_box, 7, COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, input_box,
                      model->overlay_selection == PTC_UI_CREDENTIAL_INPUT ? 3 : 1,
                      model->overlay_selection == PTC_UI_CREDENTIAL_INPUT ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
    if (model->overlay_selection == PTC_UI_CREDENTIAL_INPUT) {
        draw_text(pixels, stride, input_box.x + input_box.width - 72, input_box.y + 20, "A / X", 14, COLOR(28, 118, 188));
    }
    if (model->credential_kind == 2 && model->credential_new_revealed) {
        if (strlen(next) > 32U) {
            char first[33];
            memcpy(first, next, 32U);
            first[32] = '\0';
            draw_text(pixels, stride, input_box.x + 16, input_box.y + 25, first, 14, COLOR(28, 34, 43));
            draw_text(pixels, stride, input_box.x + 16, input_box.y + 48, next + 32, 14, COLOR(28, 34, 43));
        } else {
            draw_text(pixels, stride, input_box.x + 16, input_box.y + 40, next, 14, COLOR(28, 34, 43));
        }
    } else {
        draw_text(pixels, stride, input_box.x + 16, input_box.y + 40, next, 18, COLOR(28, 34, 43));
    }
    draw_candidate_button(pixels, stride, ptc_ui_credential_random_rect(), "Y  随机生成",
                          COLOR(244, 246, 249), COLOR(28, 118, 188),
                          model->overlay_selection == PTC_UI_CREDENTIAL_RANDOM, false);
    if (model->credential_kind == 2) {
        draw_candidate_button(pixels, stride, ptc_ui_credential_demo_rect(),
                              model->demo_secret_enabled ? "R  退出演示并换新密钥" : "R  使用公共演示密钥",
                              model->demo_secret_enabled ? COLOR(244, 246, 249) : COLOR(255, 235, 238),
                              model->demo_secret_enabled ? COLOR(28, 118, 188) : COLOR(194, 61, 61),
                              model->overlay_selection == PTC_UI_CREDENTIAL_DEMO, false);
        draw_text(pixels, stride, dialog.x + 332, dialog.y + 350,
                  "建议使用随机生成；手工密钥至少 32 个字符。", 17, COLOR(91, 100, 114));
    }
    valid = model->credential_kind == 1
        ? ptc_device_id_valid(model->credential_new)
        : ptc_grant_secret_valid(model->credential_new);
    dirty = strcmp(model->credential_current, model->credential_new) != 0;
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                          dirty ? "+  保存" : "+  没有修改",
                          COLOR(28, 118, 188), COLOR(255, 255, 255),
                          model->overlay_selection == PTC_UI_CREDENTIAL_SAVE, !valid || !dirty);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       COLOR(235, 238, 243), COLOR(66, 74, 86), true);
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 406,
              "方向键选择  |  A 确定  |  X 手工输入  |  + 保存", 16, COLOR(77, 86, 99));
}

static void draw_code_result_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell_model = *model;
    char before_value[48];
    char after_value[48];
    if (model->code_result_pending) {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "加时结果确认中");
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "已恢复上次确认的兑换请求，正在读取最终结果；请勿重复输入这枚加时码。");
    } else if (model->code_result_failed) {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "兑换未成功");
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "后台已确认本次兑换失败，加时码没有被消费，可以重新输入。");
    } else {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "加时成功");
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "已增加 %d 分钟。该加时码已经使用，不能再次使用。", model->code_grant_minutes);
    }
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 760, 420);
    if (model->code_before_unlimited) snprintf(before_value, sizeof(before_value), "不限时");
    else format_duration(model->code_before_remaining_available ? model->code_before_remaining_minutes : -1,
                         before_value, sizeof(before_value));
    if (model->code_result_pending) {
        format_duration(model->code_preview_after_available ? model->code_preview_after_minutes : -1,
                        after_value, sizeof(after_value));
    } else if (model->unrestricted_today == 1) snprintf(after_value, sizeof(after_value), "不限时");
    else format_duration(model->remaining_available ? model->remaining_minutes : -1,
                         after_value, sizeof(after_value));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 142, 300, 92},
                         "兑换前", before_value,
                         time_state_accent(model->code_before_unlimited || model->code_before_remaining_available,
                                           model->code_before_unlimited, model->code_before_remaining_minutes));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 354, dialog.y + 166, 52, 42}, "到", 28, COLOR(91, 100, 114));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 406, dialog.y + 142, 300, 92},
                         model->code_result_pending ? "预览兑换后" : "实际兑换后", after_value,
                         time_state_accent(model->code_result_pending ? model->code_preview_after_available :
                                           (model->unrestricted_today == 1 || model->remaining_available),
                                           model->code_result_pending ? false : model->unrestricted_today == 1,
                                           model->code_result_pending ? model->code_preview_after_minutes : model->remaining_minutes));
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54}, 8,
                    model->code_result_failed ? COLOR(255, 235, 238) : COLOR(235, 249, 242));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                     model->code_result_pending ? "结果确认期间可关闭；下次打开会继续确认" :
                     (model->code_result_failed ? "失败结果已确认；关闭后可重新输入" : "兑换结果已确认并保存"),
                     19, model->code_result_failed ? COLOR(194, 61, 61) : COLOR(25, 132, 95));
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回孩子区",
                       COLOR(235, 238, 243), COLOR(66, 74, 86), true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  完成",
                       COLOR(28, 118, 188), COLOR(255, 255, 255), false);
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
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 44, dialog.y + 142, dialog.width - 88, 72}, 8,
                    COLOR(255, 235, 238));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 58, dialog.y + 142, dialog.width - 116, 72},
                     model->auth_cooldown_seconds > 0
                        ? "错误次数过多，倒计时结束后才能重试"
                        : "错误 PIN 不会保留；重新输入时输入框为空",
                     18, COLOR(194, 61, 61));
    if (model->auth_cooldown_seconds > 0) {
        snprintf(retry_label, sizeof(retry_label), "请等待 %d 秒", model->auth_cooldown_seconds);
    } else {
        snprintf(retry_label, sizeof(retry_label), "A  重新输入");
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  取消",
                       COLOR(235, 238, 243), COLOR(66, 74, 86), true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), retry_label,
                       model->auth_cooldown_seconds > 0 ? COLOR(220, 224, 230) : COLOR(28, 118, 188),
                       model->auth_cooldown_seconds > 0 ? COLOR(120, 128, 140) : COLOR(255, 255, 255), false);
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
              fitted[0] ? COLOR(25, 132, 95) : COLOR(77, 86, 99));
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       COLOR(235, 238, 243), COLOR(66, 74, 86), true);
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
        fill_round_rect(pixels, stride, option, 7, selected ? COLOR(230, 242, 255) : COLOR(249, 250, 252));
        draw_rect_outline(pixels, stride, option, selected ? 2 : 1,
                          selected ? COLOR(28, 118, 188) : COLOR(211, 218, 228));
        draw_text(pixels, stride, option.x + 14, option.y + 23,
                  ptc_ui_shortcut_common_label(index), 16,
                  selected ? COLOR(28, 118, 188) : COLOR(45, 52, 62));
        if (chosen) draw_text(pixels, stride, option.x + option.width - 74, option.y + 23,
                              "待保存", 15, COLOR(25, 132, 95));
    }
    draw_dialog_button(pixels, stride, ptc_ui_shortcut_disable_rect(),
                       "ZL  关闭自定义快捷键", COLOR(255, 244, 244), COLOR(194, 61, 61), true);
    draw_dialog_button(pixels, stride, ptc_ui_shortcut_hint_rect(),
                       model->shortcut_draft_show_hint ? "Y  孩子区提示：显示" : "Y  孩子区提示：隐藏",
                       COLOR(244, 246, 249), COLOR(66, 74, 86), true);
    ptc_ui_format_custom_shortcut_hint(model->shortcut_draft_label, shortcut_hint, sizeof(shortcut_hint));
    snprintf(status, sizeof(status), "%s",
             model->shortcut_draft_enabled ? shortcut_hint : "自定义入口已关闭；固定 Minus 松开即可进入，无需长按");
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 516, status, 18,
              model->shortcut_draft_enabled ? COLOR(25, 132, 95) : COLOR(194, 61, 61));
    draw_overlay_actions(pixels, stride, model, "+  确认保存");
}

static void draw_grant_local_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *ADJUST[] = {"上一档", "下一档", "L -15", "R +15", "ZL -30", "ZR +30"};
    UiRect dialog;
    char minutes[64];
    char date[48];
    char issued_date[48];
    char generate[96];
    char played[48];
    char remaining[48];
    char estimate[64];
    char freshness[80];
    char issued_detail[256];
    bool estimate_capped = false;
    bool status_available = model->status_loaded && !model->grant_status_refresh_failed;
    int estimate_minutes = ptc_ui_grant_estimate_remaining(model, model->grant_minutes, &estimate_capped);
    uint16_t year;
    uint8_t month;
    uint8_t day;
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 920, 650);
    if (ptc_date_from_day_index(model->day_index, &year, &month, &day)) {
        snprintf(date, sizeof(date), "%u-%02u-%02u（今天）", (unsigned int)year, (unsigned int)month, (unsigned int)day);
    } else {
        snprintf(date, sizeof(date), "设备日期待刷新");
    }
    if (model->grant_has_code && ptc_date_from_day_index(model->grant_day_index, &year, &month, &day)) {
        snprintf(issued_date, sizeof(issued_date), "%u-%02u-%02u", (unsigned int)year, (unsigned int)month, (unsigned int)day);
    } else {
        snprintf(issued_date, sizeof(issued_date), "日期待刷新");
    }
    format_duration(status_available && model->played_minutes_available ? model->played_minutes : -1,
                    played, sizeof(played));
    if (status_available && model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), "不限时");
    else format_duration(status_available && model->remaining_available ? model->remaining_minutes : -1,
                         remaining, sizeof(remaining));
    format_duration(estimate_minutes, estimate, sizeof(estimate));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 112, 250, 84},
                         "额度已耗（估算）", played,
                         status_available && model->played_minutes_available ? COLOR(28, 118, 188) : COLOR(215, 139, 25));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 335, dialog.y + 112, 250, 84},
                         "今天还可玩", remaining,
                         time_state_accent(status_available && (model->unrestricted_today == 1 || model->remaining_available),
                                           model->unrestricted_today == 1, model->remaining_minutes));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 628, dialog.y + 112, 250, 84},
                         "兑换后预计还可玩", estimate,
                         time_state_accent(estimate_minutes >= 0, false, estimate_minutes));
    if (model->grant_status_refresh_failed) snprintf(freshness, sizeof(freshness), "刷新失败，实时状态暂不可用");
    else format_status_age(model, freshness, sizeof(freshness));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 202, dialog.width - 84, 24},
                     freshness, 16, model->grant_status_refresh_failed ? COLOR(194, 61, 61) : status_age_color(model));

    snprintf(minutes, sizeof(minutes), "下一枚代码：增加 %u 分钟", (unsigned int)model->grant_minutes);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 230, dialog.width - 84, 50}, 8, COLOR(244, 249, 255));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 230, dialog.width - 84, 50},
                     minutes, 24, COLOR(28, 118, 188));
    draw_horizontal_triangle(pixels, stride, dialog.x + 230, dialog.y + 255, 16, false, COLOR(28, 118, 188));
    draw_horizontal_triangle(pixels, stride, dialog.x + dialog.width - 230, dialog.y + 255, 16, true, COLOR(28, 118, 188));
    for (index = 0; index < 6; ++index) {
        draw_candidate_button(pixels, stride, ptc_ui_grant_adjust_rect(index), ADJUST[index],
                              COLOR(244, 246, 249), COLOR(66, 74, 86),
                              model->overlay_selection == index, false);
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 356, dialog.width - 84, 34},
                     model->grant_has_code ? model->grant_code : "生成前会再次验证 任我玩 PIN",
                     model->grant_has_code ? 30 : 18,
                     model->grant_has_code ? COLOR(7, 93, 76) : COLOR(91, 100, 114));
    if (model->grant_has_code) {
        char frozen[48];
        format_duration(model->grant_estimate_available ? model->grant_estimate_minutes : -1, frozen, sizeof(frozen));
        snprintf(issued_detail, sizeof(issued_detail), "已生成：增加 %u 分钟  |  兑换后预计 %s（按生成时状态估算%s）  |  签发日 %s",
                 (unsigned int)model->grant_issued_minutes, frozen,
                 model->grant_estimate_capped ? "，受每日 24 小时上限限制" : "", issued_date);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 390, dialog.width - 84, 24},
                         issued_detail, 16, COLOR(7, 93, 76));
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 420, dialog.width - 84, 24},
                     "同日签发的其他代码可能仍可兑换；调整时长不会使已签发代码失效。", 16, COLOR(194, 61, 61));
    snprintf(issued_detail, sizeof(issued_detail), "%s   |   %s",
             date, estimate_capped ? "预计时间受每日 24 小时上限限制" : "仅在代码成功兑换后生效");
    draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 448, dialog.width - 84, 24},
                     issued_detail, 16, estimate_capped ? COLOR(215, 139, 25) : COLOR(91, 100, 114));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 474, dialog.width - 84, 20},
                     "方向键选择  |  A 确定  |  L/R、ZL/ZR 调整  |  + 生成  |  B 返回", 15, COLOR(77, 86, 99));
    snprintf(generate, sizeof(generate), "%s", model->grant_has_code ? "+  再生成一个" : "+  生成今天有效的 8 位码");
    draw_candidate_button(pixels, stride, ptc_ui_grant_generate_rect(), generate,
                          COLOR(7, 93, 76), COLOR(255, 255, 255),
                          model->overlay_selection == PTC_UI_GRANT_LOCAL_GENERATE, false);
    draw_candidate_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                          COLOR(244, 246, 249), COLOR(66, 74, 86),
                          model->overlay_selection == PTC_UI_GRANT_LOCAL_BACK, false);
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
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 142, "推荐方案一：联网扫码", 23, COLOR(7, 93, 76));
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
              "仅在网页可访问时扫码；地址：", 14, COLOR(91, 100, 114));
    next_y = draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 552,
                               model->pairing_base_url, 13, 400, 19, 3, COLOR(28, 118, 188));
    draw_text(pixels, stride, dialog.x + 34, next_y + 4,
              "二维码包含加时码密钥，请仅由家长使用。", 14, COLOR(170, 65, 65));

    draw_text(pixels, stride, dialog.x + 470, dialog.y + 142, "备用方案二：单文件离线版", 23, COLOR(28, 34, 43));
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 178,
              "1. 解压完整交付包，取得 playwise-offline.html", 15, COLOR(45, 52, 62));
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 208,
              "2. 返回“加时码生成管理”，导出手机/电脑配置", 15, COLOR(45, 52, 62));
    draw_text(pixels, stride, dialog.x + 488, dialog.y + 234,
              PLAYWISE_SD_ROOT "/parent-import.json", 15, COLOR(28, 118, 188));
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 266,
              "3. 将 HTML 和配置文件传到可信的手机或电脑", 15, COLOR(45, 52, 62));
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 298,
              "4. 用系统浏览器打开 HTML，再点击“导入配置文件”", 15, COLOR(45, 52, 62));
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 330,
              "5. 选择 parent-import.json，再点击“导入此设备”", 15, COLOR(45, 52, 62));
    next_y = dialog.y + 362;
    draw_text(pixels, stride, dialog.x + 470, next_y + 6,
              "日常生成无需网络，也无需安装应用或本地服务器。", 14, COLOR(7, 93, 76));
    draw_text(pixels, stride, dialog.x + 470, next_y + 32,
              "手机请先保存文件，再交给系统浏览器打开；", 14, COLOR(91, 100, 114));
    draw_text(pixels, stride, dialog.x + 470, next_y + 58,
              "不要使用聊天软件或网盘的内置预览器。", 14, COLOR(91, 100, 114));
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 470, next_y + 74, 610, 48}, 7, COLOR(255, 235, 238));
    draw_text(pixels, stride, dialog.x + 486, next_y + 104,
              "配置文件包含加时码密钥，请勿发送给他人。", 15, COLOR(170, 35, 48));

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       COLOR(235, 238, 243), COLOR(66, 74, 86), true);
}

static void draw_weekly_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    bool refreshing = strcmp(model->overlay_title, "刷新周计划？") == 0;
    bool disabled = model->disable_flag_present;
    draw_dialog_shell(pixels, stride, model, &dialog, 860, 350);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 150, dialog.width - 80, 34},
                     "周计划还有未保存的修改", 22, COLOR(215, 139, 25));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 190, dialog.width - 80, 24},
                     "左右选择  |  A 确认  |  也可直接使用按钮快捷键", 17, COLOR(91, 100, 114));
    draw_dialog_button(pixels, stride, ptc_ui_discard_rect(model->overlay),
                       refreshing ? "X  放弃并刷新" : "X  放弃并离开",
                       COLOR(255, 235, 238), COLOR(194, 61, 61), true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), disabled ? "B  返回" : "B  继续编辑",
                        COLOR(235, 238, 243), COLOR(66, 74, 86), true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       disabled
                         ? (refreshing ? "+  保留草稿并刷新" : "+  保留草稿并离开")
                         : (refreshing ? "+  保存并刷新" : "+  保存并离开"),
                       COLOR(28, 118, 188), COLOR(255, 255, 255), false);
    if (model->weekly_leave_selection == 0) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_discard_rect(model->overlay)), 3, COLOR(194, 61, 61));
    } else if (model->weekly_leave_selection == 1) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_cancel_rect(model->overlay)), 3, COLOR(28, 118, 188));
    } else {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_confirm_rect(model->overlay)), 3, COLOR(255, 255, 255));
    }
}

static void draw_credential_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 720, 300);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 132, dialog.width - 72, 30},
                     "左右选择  |  A 确定  |  B 继续编辑", 17, COLOR(91, 100, 114));
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay), "X  放弃修改",
                          COLOR(255, 235, 238), COLOR(194, 61, 61),
                          model->overlay_selection == 0, false);
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  继续编辑",
                          COLOR(244, 246, 249), COLOR(28, 118, 188),
                          model->overlay_selection == 1, false);
}

static void draw_software_info_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect details;
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 480);
    details = (UiRect){dialog.x + 34, dialog.y + 126, dialog.width - 68, 224};
    fill_round_rect(pixels, stride, details, 8, COLOR(248, 250, 253));
    draw_rect_outline(pixels, stride, details, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, details.x + 24, details.y + 42, "软件名称", 18, COLOR(103, 111, 124));
    draw_text(pixels, stride, details.x + 180, details.y + 42, "PlayWise（任我玩）", 20, COLOR(28, 34, 43));
    draw_text(pixels, stride, details.x + 24, details.y + 84, "当前版本", 18, COLOR(103, 111, 124));
    draw_text(pixels, stride, details.x + 180, details.y + 84, model->software_version, 20, COLOR(28, 118, 188));
    draw_text(pixels, stride, details.x + 24, details.y + 126, "项目仓库", 18, COLOR(103, 111, 124));
    draw_text(pixels, stride, details.x + 180, details.y + 126, model->repository_url, 18, COLOR(28, 118, 188));
    draw_text(pixels, stride, details.x + 24, details.y + 168, "家长网页", 18, COLOR(103, 111, 124));
    draw_text(pixels, stride, details.x + 180, details.y + 168, model->pwa_url, 18, COLOR(25, 132, 95));
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  关闭",
                       COLOR(28, 118, 188), COLOR(255, 255, 255), false);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 438, "也可按 B 返回", 16, COLOR(91, 100, 114));
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
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 600);
    if (model->holiday_dirty) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + dialog.width - 330, dialog.y + 24, 290, 34},
                        7, COLOR(255, 247, 229));
        draw_text_center(pixels, stride, (UiRect){dialog.x + dialog.width - 330, dialog.y + 24, 290, 34},
                         "预览尚未保存的设置", 16, COLOR(170, 109, 18));
    }
    if (page < 0 || page >= pages) page = 0;
    for (int row = 0; row < per_page; ++row) {
        size_t index = (size_t)(page * per_page + row);
        const PtcHolidayArrangement *entry = ptc_holiday_calendar_arrangement(info->last_year, index);
        UiRect card = {dialog.x + 34, dialog.y + 146 + row * 82, dialog.width - 68, 70};
        if (!entry) break;
        fill_round_rect(pixels, stride, card, 7, COLOR(248, 250, 253));
        draw_rect_outline(pixels, stride, card, 1, COLOR(219, 225, 233));
        draw_text(pixels, stride, card.x + 20, card.y + 28, entry->display_name, 21, COLOR(28, 34, 43));
        snprintf(line, sizeof(line), "放假：%u月%u日-%u月%u日    调休上班：%s",
                 entry->start_month, entry->start_day, entry->end_month, entry->end_day, entry->makeup_workdays);
        draw_text(pixels, stride, card.x + 150, card.y + 28, line, 18, COLOR(77, 86, 99));
    }
    snprintf(line, sizeof(line), "%u 年  |  v%u  |  发布于 %s  |  来源：www.gov.cn    第 %d/%d 页",
             info->last_year, info->version, info->published_date, page + 1, pages);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 30, dialog.y + 480, dialog.width - 60, 30}, line, 16, COLOR(91, 100, 114));
    for (int index = 0; index < 3; ++index) {
        bool disabled = (index == 0 && page == 0) || (index == 1 && page + 1 >= pages);
        const char *label = index == 0 ? "L  上一页" : (index == 1 ? "R  下一页" : "A / B  关闭");
        draw_candidate_button(pixels, stride, ptc_ui_holiday_page_action_rect(index), label,
                              index == 2 ? COLOR(28, 118, 188) : COLOR(244, 246, 249),
                              index == 2 ? COLOR(255, 255, 255) : COLOR(28, 118, 188),
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
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 116, source, 19, COLOR(28, 34, 43));
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 148, "1. 选择目标", 17, COLOR(91, 100, 114));
    for (int index = 0; index < 2; ++index) {
        UiRect card = to_uirect(ptc_ui_weekly_bulk_target_rect(index));
        bool selected = model->overlay_selection == index;
        fill_round_rect(pixels, stride, card, 8, selected ? COLOR(244, 249, 255) : COLOR(248, 250, 253));
        draw_rect_outline(pixels, stride, card, selected ? 3 : 1,
                          selected ? COLOR(28, 118, 188) : COLOR(219, 225, 233));
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 18, card.width, 34},
                         index == 0 ? "工作日" : "周末", 22, COLOR(28, 34, 43));
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 54, card.width, 28},
                         index == 0 ? "周一至周五" : "周六与周日", 17, COLOR(91, 100, 114));
    }
    ptc_ui_weekly_bulk_stats(model, model->overlay_selection == 1, &stats);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 490, dialog.y + 148, 510, 270}, 8, COLOR(248, 250, 253));
    draw_rect_outline(pixels, stride, (UiRect){dialog.x + 490, dialog.y + 148, 510, 270}, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 180, "2. 覆盖预览", 20, COLOR(28, 34, 43));
    snprintf(line, sizeof(line), "目标 %d 天；会改变 %d 天；相同跳过 %d 天",
             stats.target_count, stats.changed_count, stats.unchanged_count);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 218, line, 17, COLOR(28, 118, 188));
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 256, "目标当前规则：", 16, COLOR(91, 100, 114));
    for (int index = 0; index < stats.rule_group_count; ++index) {
        PtcDayRule group = stats.rule_groups[index].rule;
        if (group.mode == PTC_RULE_MODE_UNLIMITED) snprintf(line, sizeof(line), "不限时：%d 天", stats.rule_groups[index].count);
        else snprintf(line, sizeof(line), "限时 %u 分钟：%d 天", (unsigned int)group.minutes, stats.rule_groups[index].count);
        draw_text(pixels, stride, dialog.x + 536, dialog.y + 288 + index * 27, line, 16, COLOR(66, 74, 86));
    }
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 424,
              "应用后只修改本次运行中的草稿，仍需回到周计划页面保存。", 16, COLOR(215, 139, 25));
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       COLOR(235, 238, 243), COLOR(66, 74, 86), true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       stats.changed_count > 0 ? "A / +  应用到草稿" : "A / +  无需修改",
                       stats.changed_count > 0 ? COLOR(28, 118, 188) : COLOR(145, 153, 165),
                       COLOR(255, 255, 255), false);
}

static void draw_album_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const char *state = model->album_restriction_state == 0 ? "未开启" :
                        model->album_restriction_state == 1 ? "已开启" :
                        model->album_restriction_state == 2 ? "需要处理" :
                        model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? "外部已配置" : "状态未知";
    draw_dialog_shell(pixels, stride, model, &dialog, 980, 560);
    draw_text(pixels, stride, dialog.x + 38, dialog.y + 126, "当前状态", 17, COLOR(91, 100, 114));
    draw_text(pixels, stride, dialog.x + 136, dialog.y + 126, state, 19,
              model->album_restriction_state == 1 ? COLOR(25, 132, 95) :
              model->album_restriction_state == 2 ? COLOR(215, 139, 25) :
              model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? COLOR(28, 118, 188) : COLOR(91, 100, 114));
    draw_candidate_button(pixels, stride, ptc_ui_album_refresh_rect(), "Y  重新检测",
                          COLOR(244, 246, 249), COLOR(28, 118, 188), false, false);
    for (int index = 0; index < 2; ++index) {
        UiRect card = to_uirect(ptc_ui_album_action_rect(index));
        bool selected = model->overlay_selection == index;
        bool enabled = index == 0 ? model->album_restriction_state == 0 :
                       (model->album_restriction_state == 1 ||
                        (model->album_restriction_state == 2 && model->album_backup_valid));
        fill_round_rect(pixels, stride, card, 8, enabled && selected ? COLOR(244, 249, 255) :
                        (enabled ? COLOR(255, 255, 255) : COLOR(244, 246, 249)));
        draw_rect_outline(pixels, stride, card, enabled && selected ? 3 : 1,
                          enabled && selected ? COLOR(28, 118, 188) : COLOR(219, 225, 233));
        draw_text(pixels, stride, card.x + 24, card.y + 42,
                  index == 0 ? (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL
                                  ? "无需重复配置" : "配置自制程序菜单高级入口") :
                  (model->album_restriction_state == 2 && model->album_backup_valid ? "强制恢复可信备份" : "恢复原来的启动方式"),
                  21, enabled ? COLOR(28, 34, 43) : COLOR(145, 153, 165));
        draw_wrapped_text(pixels, stride, card.x + 24, card.y + 84,
                          index == 0
                            ? (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL
                                ? "当前磁盘配置已经提供相同入口。PlayWise 从未修改它，因此不会伪造安装前备份或声称可以恢复。"
                                : "先完整备份相关配置；重启后，在桌面‘手柄设置’图标上按住 X，再按 A，进入自制程序菜单（hbmenu）。")
                            : "按可信备份恢复原配置。卸载或删除 PlayWise 数据前必须完成恢复；外部修改不会被静默覆盖。",
                          16, card.width - 48, 25, 5, enabled ? COLOR(91, 100, 114) : COLOR(165, 172, 182));
        draw_text(pixels, stride, card.x + 24, card.y + 194,
                  enabled ? "A  继续" : "当前状态不可用", 16,
                  enabled ? COLOR(28, 118, 188) : COLOR(145, 153, 165));
    }
    if (model->album_restriction_detail[0]) {
        draw_wrapped_text(pixels, stride, dialog.x + 38, dialog.y + 432,
                          model->album_restriction_detail, 14, dialog.width - 76, 20, 2, COLOR(91, 100, 114));
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       COLOR(235, 238, 243), COLOR(66, 74, 86), true);
}

static void draw_theme_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *LABELS[] = {"跟随系统", "浅色", "暗色"};
    static const char *DETAILS[] = {"随 Switch 设置", "保留经典外观", "OLED Hybrid"};
    UiRect dialog;
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 820, 360);
    for (index = 0; index < 3; ++index) {
        UiRect option = to_uirect(ptc_ui_theme_option_rect(index));
        bool selected = index == model->overlay_selection;
        fill_round_rect(pixels, stride, option, 8, selected ? COLOR(230, 242, 255) : COLOR(248, 250, 252));
        draw_rect_outline(pixels, stride, option, selected ? 3 : 1,
                          selected ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
        draw_text_center(pixels, stride, (UiRect){option.x, option.y + 14, option.width, 34},
                         LABELS[index], 22, COLOR(28, 34, 43));
        draw_text_center(pixels, stride, (UiRect){option.x, option.y + 52, option.width, 26},
                         DETAILS[index], 15, COLOR(91, 100, 114));
    }
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 294,
              !g_theme.system_theme_available && model->overlay_selection == PTC_UI_THEME_SYSTEM
                  ? "系统主题暂不可用，将安全回退为浅色。" : "方向键选择  |  A 立即应用并保存  |  B 取消",
              16, !g_theme.system_theme_available ? COLOR(215, 139, 25) : COLOR(91, 100, 114));
}

static void draw_holiday_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 720, 320);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 148, dialog.width - 72, 28},
                     "离开后将丢失尚未保存的节假日设置", 19, COLOR(215, 139, 25));
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay), "X  放弃更改",
                          COLOR(255, 235, 238), COLOR(194, 61, 61), model->holiday_leave_selection == 0, false);
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  继续编辑",
                          COLOR(244, 246, 249), COLOR(28, 118, 188), model->holiday_leave_selection == 1, false);
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
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y, row, 18, dialog.width - 84, 28, 2, COLOR(28, 34, 43));
    snprintf(row, sizeof(row), "操作类型：%s", model->recent_event_types[index][0] ? model->recent_event_types[index] : "未记录");
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2, COLOR(77, 86, 99));
    snprintf(row, sizeof(row), "结果：%s", model->recent_event_errors[index][0] ? model->recent_event_errors[index] : "成功");
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2,
                          model->recent_event_errors[index][0] ? COLOR(194, 61, 61) : COLOR(25, 132, 95));
    snprintf(row, sizeof(row), "时间：%s", time_text);
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2, COLOR(77, 86, 99));
    snprintf(row, sizeof(row), "请求 ID：%s", model->recent_event_request_ids[index][0] ? model->recent_event_request_ids[index] : "未记录");
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 16, dialog.width - 84, 24, 3, COLOR(77, 86, 99));
    snprintf(row, sizeof(row), "内部详情：%s", model->recent_event_details[index][0] ? model->recent_event_details[index] : "无");
    draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 16, dialog.width - 84, 24, 3, COLOR(77, 86, 99));
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A / B  关闭",
                       COLOR(28, 118, 188), COLOR(255, 255, 255), false);
}

static void draw_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
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
    g_resolved_theme = theme->resolved;
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
    fill_rect_packed(pixels, stride, (UiRect){1080, 12, 184, 28}, pack_rgb(COLOR(176, 42, 55)));
    draw_text(pixels, stride, 1094, 18, "EDEN TEST", 16, COLOR(255, 255, 255));
#endif
    framebufferEnd(&g_ui.framebuffer);
}
