#include "ui_graphics.h"

#include <switch.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../common/time/ptc_time.h"

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define COLOR(r, g, b) ((uint32_t)RGBA8_MAXALPHA((r), (g), (b)))

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

static UiRect to_uirect(PtcUiRect rect);
static const char *rule_mode_label(PtcRuleMode mode);
static void format_status_age(const PtcUiModel *model, char *out, size_t out_size);
static uint32_t status_age_color(const PtcUiModel *model);

static const UiAction TODAY_ACTIONS[] = {
    {"刷新状态", "读取今天的最新游玩状态", COLOR(42, 105, 188)},
    {"设置今日额度", "指定今天可玩的分钟数", COLOR(42, 105, 188)},
    {"临时加时", "在今天额度上增加分钟", COLOR(25, 132, 95)},
    {"今日不限时", "今天不设时间上限", COLOR(25, 132, 95)},
    {"恢复周计划", "清除今天的临时调整", COLOR(91, 100, 116)},
};

static const UiAction SECURITY_ACTIONS[] = {
    {"管理设备名", "查看、输入或随机生成设备名称", COLOR(42, 105, 188)},
    {"管理加时码密钥", "查看、生成或切换演示密钥", COLOR(194, 61, 61)},
    {"加时码生成", "优先用手机扫码，也可在本机生成 8 位码", COLOR(25, 132, 95)},
    {"管理 任你玩 PIN", "本应用独立 PIN，区别于 Nintendo 家长管理 PIN", COLOR(42, 105, 188)},
    {"孩子区快捷键说明", "显示或隐藏进入家长区的操作提示", COLOR(42, 105, 188)},
};

static const UiAction SUPPORT_ACTIONS[] = {
    {"确认接管系统控制", "预检、保存快照后启用额度管理", COLOR(42, 105, 188)},
    {"重试修复", "重新检查并恢复安全前置条件", COLOR(25, 132, 95)},
    {"紧急停用控制", "立即停止后台控制操作", COLOR(194, 61, 61)},
    {"恢复安装前状态", "恢复原始设置并停用 任你玩", COLOR(194, 61, 61)},
    {"导出诊断包", "生成不含密钥、PIN 和离线码的支持文件", COLOR(91, 100, 116)},
};

static const UiAction RESUME_CONTROL_ACTION = {
    "解除紧急停用", "删除停用标记并恢复后台控制", COLOR(25, 132, 95)
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
    *destination = COLOR(
        (red * alpha + destination_red * (255 - alpha)) / 255,
        (green * alpha + destination_green * (255 - alpha)) / 255,
        (blue * alpha + destination_blue * (255 - alpha)) / 255);
}

static void fill_rect(uint32_t *pixels, uint32_t stride, UiRect rect, uint32_t color)
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
                set_pixel(pixels, stride, x, y, color);
            }
        }
    }
}

static void draw_rect_outline(uint32_t *pixels, uint32_t stride, UiRect rect, int width, uint32_t color)
{
    fill_rect(pixels, stride, (UiRect){rect.x, rect.y, rect.width, width}, color);
    fill_rect(pixels, stride, (UiRect){rect.x, rect.y + rect.height - width, rect.width, width}, color);
    fill_rect(pixels, stride, (UiRect){rect.x, rect.y, width, rect.height}, color);
    fill_rect(pixels, stride, (UiRect){rect.x + rect.width - width, rect.y, width, rect.height}, color);
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
                    color,
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
    const char *source = text ? text : "";
    const char *cursor = source;
    const char *end = cursor;
    size_t copy_size;
    int width = 0;
    bool truncated = false;
    if (!out || out_size == 0) {
        return;
    }
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

static void draw_overlay_actions(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, const char *confirm_label)
{
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), confirm_label, COLOR(28, 118, 188), COLOR(255, 255, 255), false);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  取消", COLOR(235, 238, 243), COLOR(66, 74, 86), true);
}

static void describe_status(const PtcUiModel *model, char *today, size_t today_size, char *remaining, size_t remaining_size)
{
    if (!model->status_loaded) {
        snprintf(today, today_size, "等待刷新");
        snprintf(remaining, remaining_size, "--");
        return;
    }
    if (model->blocked_today == 1) {
        snprintf(today, today_size, "系统当前受限");
    } else if (model->unrestricted_today == 1) {
        snprintf(today, today_size, "不限时");
    } else if (model->limited_today == 1) {
        snprintf(today, today_size, model->restricted_now == 1 ? "已到限制" : "限时中");
    } else {
        snprintf(today, today_size, "状态未知");
    }
    if (model->remaining_available && model->remaining_minutes >= 0) {
        snprintf(remaining, remaining_size, "%d 分钟", model->remaining_minutes);
    } else {
        snprintf(remaining, remaining_size, "暂不可用");
    }
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
    draw_text_center(pixels, stride, banner, "紧急停用已开启 · 新的时间控制不会应用", 18, COLOR(170, 35, 48));
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
    draw_text(pixels, stride, rect.x + 24, rect.y + (compact ? 22 : 25),
              model->waiting ? "正在执行" : "最近执行", compact ? 17 : 18, accent);
    snprintf(
        execution,
        sizeof(execution),
        "命令：%s    %s",
        model->command_name[0] ? model->command_name : "未开始",
        model->transport_label[0] ? model->transport_label : "传输：未开始");
    fit_text(fitted, sizeof(fitted), execution, compact ? 17 : 18, rect.width - 48);
    draw_text(pixels, stride, rect.x + 24, rect.y + (compact ? 45 : 50), fitted,
              compact ? 17 : 18, COLOR(77, 86, 99));
    fit_text(fitted, sizeof(fitted), model->message, compact ? 19 : 21, rect.width - 48);
    draw_text(pixels, stride, rect.x + 24, rect.y + (compact ? 70 : 78), fitted,
              compact ? 19 : 20, COLOR(45, 52, 62));
    if (model->feedback_detail[0]) {
        fit_text(detail, sizeof(detail), model->feedback_detail, compact ? 15 : 17, rect.width - 48);
        draw_text(pixels, stride, rect.x + 24, rect.y + (compact ? 92 : 105), detail,
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
    draw_text(pixels, stride, split_x + 15, rect.y + 34, "运行状态", 15, COLOR(103, 111, 124));
    draw_text(pixels, stride, split_x + 15, rect.y + 68, mode, 19, mode_accent);
}

static void draw_child(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char today[32];
    char remaining[32];
    char played[32];
    char parent_hint[128];
    char fitted_hint[128];
    const char *mode = model->disable_flag_present ? "控制已停用" :
        (strcmp(model->setup_phase, "active") == 0 ? "正常运行" :
        (strcmp(model->setup_phase, "protection") == 0 ? "保护模式" : "兼容性待确认"));
    describe_status(model, today, sizeof(today), remaining, sizeof(remaining));
    if (model->played_minutes_available && model->played_minutes >= 0) {
        snprintf(played, sizeof(played), "约 %d 分钟", model->played_minutes);
    } else {
        snprintf(played, sizeof(played), "暂不可用");
    }
    draw_header(pixels, stride, "自律小达人 · 加时奖励", "遵守约定、合理安排时间");
    draw_disable_banner(pixels, stride, model);
    draw_status_tile(pixels, stride, (UiRect){54, 118, 278, 92}, "今日状态", today, COLOR(216, 49, 54));
    draw_status_tile(pixels, stride, (UiRect){350, 118, 278, 92}, "剩余时间", remaining, COLOR(25, 132, 95));
    draw_status_tile(pixels, stride, (UiRect){646, 118, 278, 92}, "已玩时间", played, COLOR(215, 139, 25));
    draw_timer_status_tile(pixels, stride, (UiRect){942, 118, 284, 92},
                           model->play_timer_enabled == 1 ? "已开启" : "未确认", mode,
                           model->disable_flag_present ? COLOR(194, 61, 61) : COLOR(28, 118, 188));

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
              model->disable_flag_present ? "" : "按 A 或点击输入 · 仅支持 8 位数字码",
              17, COLOR(103, 111, 124));

    fill_round_rect(pixels, stride, (UiRect){836, 238, 390, 274}, 8, COLOR(250, 251, 253));
    draw_rect_outline(pixels, stride, (UiRect){836, 238, 390, 274}, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, 866, 282, "游戏时间统计", 24, COLOR(28, 34, 43));
    draw_text(pixels, stride, 866, 329, "游戏明细暂不可用", 22, COLOR(85, 94, 107));
    draw_text(pixels, stride, 866, 362, "前 3 名游戏将在数据可用后显示", 17, COLOR(103, 111, 124));
    fill_round_rect(pixels, stride, (UiRect){866, 384, 330, 50}, 7, COLOR(244, 248, 253));
    draw_text(pixels, stride, 884, 416, "今日累计已玩", 17, COLOR(103, 111, 124));
    draw_text(pixels, stride, 1034, 416, played, 20, COLOR(28, 118, 188));
    draw_dialog_button(pixels, stride, ptc_ui_child_refresh_rect(),
                       model->waiting ? "正在刷新状态…" : "Y  立即刷新状态",
                       model->waiting ? COLOR(215, 139, 25) : COLOR(28, 118, 188),
                       COLOR(255, 255, 255), false);

    draw_notice(pixels, stride, model, 530, 100);
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(0),
                       model->disable_flag_present ? "紧急停用中" : "A  输入加时码");
    fill_round_rect(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), 1, COLOR(203, 211, 222));
    if (model->show_parent_shortcut_hint) {
        snprintf(parent_hint, sizeof(parent_hint), "Minus - / %s  家长区", model->custom_shortcut_label);
    } else {
        snprintf(parent_hint, sizeof(parent_hint), "家长区快捷键提示已关闭");
    }
    fit_text(fitted_hint, sizeof(fitted_hint), parent_hint, 16, ptc_ui_child_footer_rect(1).w - 16);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), fitted_hint, 16, COLOR(47, 57, 71));
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), "B / +  退出");
}

static void draw_setup(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {54, 120, 1172, 500};
    UiRect fixed_card = {204, 188, 872, 68};
    const char *phase = model->setup_phase[0] ? model->setup_phase : "pending";
    int64_t grace_remaining = ptc_ui_setup_grace_remaining(model, (int64_t)time(NULL));
    char title[64];
    char phase_line[192];
    char countdown_line[80];
    char fitted[220];
    int step = model->setup_step > 0 ? model->setup_step : PTC_UI_SETUP_SHORTCUT;
    snprintf(title, sizeof(title), "首次设置 · %d/4", step);
    draw_header(pixels, stride, grace_remaining >= 0 ? "正在同步" : title,
        grace_remaining >= 0 ? "系统设置正在同步，完成后继续选择进入的区域" : "按步骤完成 任你玩 的家长设置");
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    if (grace_remaining >= 0) {
        draw_text(pixels, stride, 204, 190, "环境检查已通过", 31, COLOR(25, 132, 95));
        snprintf(phase_line, sizeof(phase_line), "当前状态：正在同步    安装前快照：%s",
                 model->setup_snapshot_available ? "已保存" : "不可用");
        draw_text(pixels, stride, 204, 248, phase_line, 22, COLOR(77, 86, 99));
        if (grace_remaining > 0) {
            snprintf(countdown_line, sizeof(countdown_line), "系统设置同步中（约 %lld 秒）…", (long long)grace_remaining);
        } else {
            snprintf(countdown_line, sizeof(countdown_line), "同步完成，正在启用额度管理…");
        }
        draw_text(pixels, stride, 204, 310, countdown_line, 34, COLOR(28, 118, 188));
        draw_text(pixels, stride, 204, 356, "无需操作；同步完成后会进入第 4 步选择孩子区或家长区。", 22, COLOR(45, 52, 62));
    } else {
        draw_text(pixels, stride, 204, 154, "1 快捷键", 18, step == PTC_UI_SETUP_SHORTCUT ? COLOR(28, 118, 188) : COLOR(91, 100, 116));
        draw_text(pixels, stride, 450, 154, "2 PIN", 18, step == PTC_UI_SETUP_PIN ? COLOR(28, 118, 188) : COLOR(91, 100, 116));
        draw_text(pixels, stride, 660, 154, "3 接管", 18, step == PTC_UI_SETUP_TAKEOVER ? COLOR(28, 118, 188) : COLOR(91, 100, 116));
        draw_text(pixels, stride, 870, 154, "4 进入区域", 18, step == PTC_UI_SETUP_ZONE ? COLOR(28, 118, 188) : COLOR(91, 100, 116));
        if (step == PTC_UI_SETUP_SHORTCUT) {
            fill_round_rect(pixels, stride, fixed_card, 8, COLOR(244, 249, 255));
            draw_rect_outline(pixels, stride, fixed_card, 2, COLOR(28, 118, 188));
            draw_text(pixels, stride, 232, 218, "固定入口", 18, COLOR(91, 100, 116));
            draw_text(pixels, stride, 350, 226, "Minus -", 26, COLOR(28, 118, 188));
            draw_text(pixels, stride, 510, 222, "左 Joy-Con 下方 · 始终有效", 19, COLOR(77, 86, 99));
            draw_text(pixels, stride, 204, 288, "再选择一个自定义入口", 22, COLOR(28, 34, 43));
            draw_text(pixels, stride, 480, 288, "与 Minus - 同时有效", 17, COLOR(91, 100, 116));
            for (int index = 0; index < PTC_UI_SHORTCUT_PRESET_COUNT; ++index) {
                UiRect card = to_uirect(ptc_ui_setup_shortcut_card_rect(index));
                bool selected = index == model->setup_shortcut_index;
                fill_round_rect(pixels, stride, card, 8, selected ? COLOR(230, 242, 255) : COLOR(250, 251, 253));
                draw_rect_outline(pixels, stride, card, selected ? 3 : 1,
                                  selected ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 12, card.width, 34},
                                 ptc_ui_shortcut_common_label(index), 23,
                                 selected ? COLOR(28, 118, 188) : COLOR(45, 52, 62));
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 53, card.width, 22},
                                 selected ? "当前选择" : "常用组合", 16, COLOR(91, 100, 116));
            }
            draw_dialog_button(pixels, stride, ptc_ui_setup_shortcut_capture_rect(),
                               model->shortcut_capture_active ? "按住组合，A 确认" : "X / 点击  手动录入",
                               model->shortcut_capture_active ? COLOR(255, 247, 229) : COLOR(235, 238, 243),
                               model->shortcut_capture_active ? COLOR(170, 109, 18) : COLOR(28, 118, 188), true);
            fill_round_rect(pixels, stride, (UiRect){204, 424, 872, 62}, 8, COLOR(247, 249, 252));
            draw_text(pixels, stride, 232, 462, "当前生效：Minus -  或", 18, COLOR(91, 100, 116));
            draw_text(pixels, stride, 472, 462, model->custom_shortcut_label, 22, COLOR(28, 118, 188));
        } else if (step == PTC_UI_SETUP_PIN) {
            draw_text(pixels, stride, 204, 220, "设置 任你玩 PIN", 30, COLOR(28, 34, 43));
            draw_text(pixels, stride, 204, 262, "这是进入家长区、修改规则和安全设置时使用的本应用 PIN。", 21, COLOR(77, 86, 99));
            draw_text(pixels, stride, 204, 294, "请输入 1–64 位纯数字，系统会引导你再次输入确认。", 21, COLOR(77, 86, 99));
            draw_dialog_button(pixels, stride, ptc_ui_setup_pin_rect(), "A / 点击  设置或确认 PIN",
                               COLOR(28, 118, 188), COLOR(255, 255, 255), false);
            draw_text(pixels, stride, 204, 420, "PIN 少于 4 位时只提示弱保护风险，不会阻止保存。", 20, COLOR(215, 139, 25));
        } else if (step == PTC_UI_SETUP_TAKEOVER) {
            bool resuming_restored_setup = model->disable_flag_present && strcmp(phase, "restored") == 0;
            draw_text(pixels, stride, 204, 218,
                      resuming_restored_setup ? "解除停用并重新接管" : "确认接管系统控制",
                      30, COLOR(28, 34, 43));
            snprintf(phase_line, sizeof(phase_line), "当前状态：%s    安装前快照：%s",
                     strcmp(phase, "protection") == 0 ? "保护模式" :
                     (strcmp(phase, "failed") == 0 ? "检查失败" :
                      (resuming_restored_setup ? "已恢复并停用" : "等待家长确认")),
                     model->setup_snapshot_available ? "已保存" : "待保存");
            draw_text(pixels, stride, 204, 266, phase_line, 21, COLOR(77, 86, 99));
            draw_text(pixels, stride, 204, 324,
                      resuming_restored_setup
                          ? "确认后会重新执行只读兼容预检；通过后才解除紧急停用并重新接管。"
                          : "确认后会先执行只读兼容预检，再保存安装前快照并启用额度管理。",
                      21, COLOR(45, 52, 62));
            draw_text(pixels, stride, 204, 360, "接管成功后会保留同步宽限，系统控制不会立即跳变。", 21, COLOR(45, 52, 62));
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(),
                               resuming_restored_setup ? "A / 点击  解除停用并重新接管" : "A / 点击  确认接管",
                               COLOR(28, 118, 188), COLOR(255, 255, 255), false);
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
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 86, card.width - 36, 26},
                                     model->show_parent_shortcut_hint ? "进入家长区：Minus - 或自定义组合" : "家长区提示已关闭", 17, COLOR(77, 86, 99));
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     "家长区需要输入 任你玩 PIN", 17, COLOR(91, 100, 116));
                } else {
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 86, card.width - 36, 26},
                                     "返回孩子区：B", 19, COLOR(77, 86, 99));
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     "当前入口：Minus - 和自定义组合", 17, COLOR(91, 100, 116));
                }
            }
        }
        if (model->message[0]) {
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
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), "+  下一步",
                               COLOR(28, 118, 188), COLOR(255, 255, 255), false);
        } else if (step == PTC_UI_SETUP_ZONE) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(),
                               model->setup_zone_index == 1 ? "A  确认进入家长区" : "A  确认进入孩子区",
                               COLOR(28, 118, 188), COLOR(255, 255, 255), false);
        } else if (step == PTC_UI_SETUP_PIN) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), "A  设置并继续",
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
    if (page == PTC_UI_PARENT_SECURITY) {
        *count = (int)(sizeof(SECURITY_ACTIONS) / sizeof(SECURITY_ACTIONS[0]));
        return SECURITY_ACTIONS;
    }
    if (page == PTC_UI_PARENT_SUPPORT) {
        *count = (int)(sizeof(SUPPORT_ACTIONS) / sizeof(SUPPORT_ACTIONS[0]));
        return SUPPORT_ACTIONS;
    }
    *count = (int)(sizeof(TODAY_ACTIONS) / sizeof(TODAY_ACTIONS[0]));
    return TODAY_ACTIONS;
}

static void draw_tabs(uint32_t *pixels, uint32_t stride, PtcUiParentPage active)
{
    static const char *LABELS[] = {"今日额度", "周计划", "加时码与安全", "支持与恢复"};
    int index;
    for (index = 0; index < PTC_UI_PARENT_PAGE_COUNT; ++index) {
        UiRect tab = to_uirect(ptc_ui_parent_tab_rect(index));
        uint32_t background = index == (int)active ? COLOR(28, 118, 188) : COLOR(235, 238, 243);
        uint32_t foreground = index == (int)active ? COLOR(255, 255, 255) : COLOR(66, 74, 86);
        fill_round_rect(pixels, stride, tab, 8, background);
        draw_text_center(pixels, stride, tab, LABELS[index], 21, foreground);
    }
    draw_text(pixels, stride, 1038, 140, "L / R 切换", 19, COLOR(97, 106, 120));
}

static void draw_action_card(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    const UiAction *action,
    bool selected,
    PtcUiActionState state)
{
    bool disabled = state == PTC_UI_ACTION_DISABLED;
    bool recommended = state == PTC_UI_ACTION_RECOMMENDED;
    uint32_t background = disabled ? COLOR(244, 246, 249) :
                          selected ? COLOR(244, 249, 255) : COLOR(255, 255, 255);
    uint32_t border = disabled ? COLOR(230, 233, 238) :
                      selected ? action->accent : COLOR(219, 225, 233);
    uint32_t title_color = disabled ? COLOR(160, 168, 180) : COLOR(28, 34, 43);
    uint32_t sub_color = disabled ? COLOR(180, 186, 196) : COLOR(91, 100, 114);
    fill_round_rect(pixels, stride, rect, 8, background);
    draw_rect_outline(pixels, stride, rect, selected && !disabled ? 3 : 1, border);
    fill_round_rect(pixels, stride, (UiRect){rect.x + 20, rect.y + 25, 12, rect.height - 50}, 6,
                    disabled ? COLOR(200, 206, 214) : action->accent);
    draw_text(pixels, stride, rect.x + 54, rect.y + 46, action->title, 24, title_color);
    draw_text(pixels, stride, rect.x + 54, rect.y + 78, action->subtitle, 18, sub_color);
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
    const char *phase_label;
    uint32_t phase_color;
    char hint[192];
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, panel.x + 26, panel.y + 43, "当前环境与恢复", 23, COLOR(28, 34, 43));
    /* Setup phase */
    if (strcmp(model->setup_phase, "active") == 0) {
        phase_label = "正常运行";
        phase_color = COLOR(25, 132, 95);
    } else if (strcmp(model->setup_phase, "released") == 0) {
        phase_label = "正在同步";
        phase_color = COLOR(28, 118, 188);
    } else if (strcmp(model->setup_phase, "pending") == 0) {
        phase_label = "正在同步";
        phase_color = COLOR(215, 139, 25);
    } else if (strcmp(model->setup_phase, "failed") == 0) {
        phase_label = "保护模式";
        phase_color = COLOR(194, 61, 61);
    } else if (strcmp(model->setup_phase, "restored") == 0) {
        phase_label = "已恢复快照";
        phase_color = COLOR(91, 100, 116);
    } else {
        phase_label = model->setup_phase[0] ? model->setup_phase : "--";
        phase_color = COLOR(91, 100, 116);
    }
    draw_text(pixels, stride, panel.x + 26, panel.y + 82, "运行状态", 18, COLOR(103, 111, 124));
    draw_text(pixels, stride, panel.x + 172, panel.y + 82, phase_label, 18, phase_color);
    draw_text(pixels, stride, panel.x + 26, panel.y + 112, "安装前快照", 18, COLOR(103, 111, 124));
    draw_text(pixels, stride, panel.x + 172, panel.y + 112,
              model->setup_snapshot_available ? "已保存" : "不可用", 18,
              model->setup_snapshot_available ? COLOR(25, 132, 95) : COLOR(91, 100, 116));
    draw_text(pixels, stride, panel.x + 26, panel.y + 142, "到期行为", 18, COLOR(103, 111, 124));
    draw_text(pixels, stride, panel.x + 172, panel.y + 142, "系统提醒", 18, COLOR(28, 118, 188));
    if (model->disable_flag_present) {
        fill_round_rect(pixels, stride, (UiRect){panel.x + 20, panel.y + 166, panel.width - 40, 68}, 7, COLOR(255, 232, 235));
        draw_text(pixels, stride, panel.x + 36, panel.y + 194, "紧急停用已开启", 22, COLOR(170, 35, 48));
        draw_text(pixels, stride, panel.x + 36, panel.y + 220, "普通控制写入已阻止", 17, COLOR(170, 35, 48));
    }
    /* Contextual hint for selected action */
    snprintf(hint, sizeof(hint), "%s", model->safety_hint[0] ? model->safety_hint : "选择操作查看引导说明。");
    draw_text(pixels, stride, panel.x + 26, panel.y + 262, hint, 17, COLOR(91, 100, 116));
    draw_text(pixels, stride, panel.x + 26, panel.y + 292, "状态和恢复在停用时仍可使用", 18, COLOR(91, 100, 116));
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
    uint32_t today_color;
    describe_status(model, today, sizeof(today), remaining, sizeof(remaining));
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
        today_color = model->restricted_now == 1 ? COLOR(194, 61, 61) : COLOR(28, 118, 188);
    } else {
        today_color = COLOR(91, 100, 116);
    }
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, panel.x + 26, panel.y + 43, "今日状态", 23, COLOR(28, 34, 43));
    draw_status_row(pixels, stride, panel, panel.y + 72, "今日模式", today, today_color);
    draw_status_row(pixels, stride, panel, panel.y + 102, "剩余时间", remaining,
                    model->remaining_available ? COLOR(28, 34, 43) : COLOR(91, 100, 116));
    draw_status_row(pixels, stride, panel, panel.y + 132, "已玩时间", played,
                    model->played_minutes_available ? COLOR(28, 34, 43) : COLOR(91, 100, 116));
    draw_status_row(pixels, stride, panel, panel.y + 162, "运行状态",
                    model->disable_flag_present ? "控制已停用" :
                    (strcmp(model->setup_phase, "active") == 0 ? "正常运行" :
                    (strcmp(model->setup_phase, "protection") == 0 ? "保护模式" : "兼容性待确认")),
                    model->disable_flag_present ? COLOR(194, 61, 61) :
                    (strcmp(model->setup_phase, "active") == 0 ? COLOR(25, 132, 95) : COLOR(215, 139, 25)));
    draw_status_row(pixels, stride, panel, panel.y + 192, "系统计时器",
                    model->play_timer_enabled == 1 ? "已开启" : "未确认",
                    model->play_timer_enabled == 1 ? COLOR(25, 132, 95) : COLOR(91, 100, 116));
    fill_round_rect(pixels, stride, (UiRect){panel.x + 20, panel.y + 224, panel.width - 40, 42}, 7,
                    model->waiting ? COLOR(255, 247, 229) : COLOR(244, 248, 253));
    draw_text_center(pixels, stride, (UiRect){panel.x + 20, panel.y + 224, panel.width - 40, 42},
                     freshness, 17, model->waiting ? COLOR(170, 109, 18) : status_age_color(model));
}

static void draw_grant_help(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 440};
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, panel.x + 26, panel.y + 43, "加时码如何生成", 23, COLOR(28, 34, 43));
    draw_text(pixels, stride, panel.x + 26, panel.y + 82, "1. 用手机扫描 Switch 二维码", 18, COLOR(77, 86, 99));
    draw_text(pixels, stride, panel.x + 26, panel.y + 116, "2. 在网页确认设备并选择时长", 18, COLOR(77, 86, 99));
    draw_text(pixels, stride, panel.x + 26, panel.y + 150, "3. 将网页生成的 8 位码交给孩子", 18, COLOR(77, 86, 99));
    draw_text(pixels, stride, panel.x + 26, panel.y + 202, "任你玩 管理 PIN", 19, COLOR(28, 118, 188));
    draw_text(pixels, stride, panel.x + 26, panel.y + 232, "仅保护本应用家长区", 17, COLOR(91, 100, 116));
    draw_text(pixels, stride, panel.x + 26, panel.y + 258, "不是 Nintendo 系统家长管理 PIN", 17, COLOR(91, 100, 116));
    draw_text(pixels, stride, panel.x + 26, panel.y + 294, "进入家长区", 19, COLOR(28, 118, 188));
    draw_text(pixels, stride, panel.x + 26, panel.y + 324, "Minus - 或", 17, COLOR(91, 100, 116));
    draw_text(pixels, stride, panel.x + 126, panel.y + 324, model->custom_shortcut_label, 17, COLOR(28, 118, 188));
    draw_text(pixels, stride, panel.x + 26, panel.y + 350,
              model->show_parent_shortcut_hint ? "孩子区提示：已显示" : "孩子区提示：已关闭", 17, COLOR(91, 100, 116));
    if (model->demo_secret_enabled) {
        fill_round_rect(pixels, stride, (UiRect){panel.x + 20, panel.y + 400, panel.width - 40, 34}, 6, COLOR(255, 235, 238));
        draw_text_center(pixels, stride, (UiRect){panel.x + 20, panel.y + 400, panel.width - 40, 34},
                         "公共演示密钥已启用", 18, COLOR(194, 61, 61));
    }
}

static void format_duration(int minutes, char *out, size_t out_size)
{
    if (minutes < 0) {
        snprintf(out, out_size, "暂不可用");
        return;
    }
    snprintf(out, out_size, "%d 小时 %d 分钟", minutes / 60, minutes % 60);
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
        snprintf(out, out_size, "正在刷新状态…");
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

static void draw_weekly_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    int day;
    char minutes[32];
    char today_hint[240];
    uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
    draw_text(pixels, stride, 54, 184, "直接调整每一天，完成后统一保存", 20, COLOR(77, 86, 99));
    for (day = 0; day < 7; ++day) {
        UiRect card = to_uirect(ptc_ui_weekly_day_rect(day));
        uint32_t border = day == model->editor_index ? COLOR(28, 118, 188) : COLOR(219, 225, 233);
        fill_round_rect(pixels, stride, card, 8, day == model->editor_index ? COLOR(244, 249, 255) : COLOR(255, 255, 255));
        draw_rect_outline(pixels, stride, card, day == model->editor_index ? 3 : 1, border);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 14, card.width, 34}, DAYS[day], 21, COLOR(28, 34, 43));
        if (day == weekday) draw_text_center(pixels, stride, (UiRect){card.x, card.y + 45, card.width, 24}, "今天", 16, COLOR(25, 132, 95));
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 75, card.width, 34},
                         rule_mode_label(model->draft_week[day].mode), 22, COLOR(28, 118, 188));
        if (model->draft_week[day].mode == PTC_RULE_MODE_LIMIT) {
            snprintf(minutes, sizeof(minutes), "%u 分钟", (unsigned int)model->draft_week[day].minutes);
        } else {
            snprintf(minutes, sizeof(minutes), "--");
        }
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 125, card.width, 34}, minutes, 19, COLOR(77, 86, 99));
    }
    if (model->today_override_present) {
        char active_remaining[32];
        char weekly_remaining[32];
        char line1[120];
        format_rule_remaining_label(model, model->today_override_rule, active_remaining, sizeof(active_remaining));
        format_rule_remaining_label(model, model->current_week[weekday], weekly_remaining, sizeof(weekly_remaining));
        snprintf(line1, sizeof(line1), "当前有效剩余：%s  ·  恢复周计划后：%s", active_remaining, weekly_remaining);
        draw_text(pixels, stride, 64, 416, line1, 18, COLOR(215, 139, 25));
        draw_text(pixels, stride, 64, 444, "提示：保存计划不会清除今日临时设置，可到“今日额度”恢复原计划。", 17, COLOR(91, 100, 116));
    } else if (model->draft_week[weekday].mode == PTC_RULE_MODE_LIMIT) {
        int remaining = model->played_minutes_available
            ? (int)model->draft_week[weekday].minutes - model->played_minutes : -1;
        if (remaining < 0 && model->played_minutes_available) remaining = 0;
        if (remaining >= 0) snprintf(today_hint, sizeof(today_hint), "今天已玩约 %d 分钟；保存后还可玩约 %d 分钟。", model->played_minutes, remaining);
        else snprintf(today_hint, sizeof(today_hint), "今天的实际剩余将在计划保存并同步后刷新。");
        draw_text(pixels, stride, 64, 424, today_hint, 18, COLOR(25, 132, 95));
    } else {
        snprintf(today_hint, sizeof(today_hint), "今天保存后为不限时；系统将在后台自动刷新同步。");
        draw_text(pixels, stride, 64, 424, today_hint, 18, COLOR(25, 132, 95));
    }
    draw_text(pixels, stride, 64, 478, "左右选择日期 · X 切换限时/不限时 · 上下 ±15 · Y 手工输入", 17, COLOR(77, 86, 99));
    draw_dialog_button(pixels, stride, ptc_ui_weekly_mode_rect(), "切换模式",
                       COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_dialog_button(pixels, stride, ptc_ui_weekly_discard_rect(), "放弃修改", COLOR(235, 238, 243), COLOR(66, 74, 86), true);
    draw_dialog_button(pixels, stride, ptc_ui_weekly_save_rect(),
                       model->disable_flag_present ? "紧急停用中" : (model->weekly_dirty ? "保存计划" : "计划未修改"),
                       model->weekly_dirty && !model->disable_flag_present ? COLOR(28, 118, 188) : COLOR(203, 211, 222),
                       COLOR(255, 255, 255), false);
}

static void draw_parent(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const UiAction *actions;
    int action_count;
    int index;
    draw_header(pixels, stride, "家长时间管理", "本地规则与设备安全设置");
    draw_disable_banner(pixels, stride, model);
    if (model->demo_secret_enabled) {
        UiRect warning = model->disable_flag_present ? (UiRect){526, 42, 246, 38} : (UiRect){900, 42, 326, 36};
        fill_round_rect(pixels, stride, warning, 7, COLOR(255, 235, 238));
        draw_text_center(pixels, stride, warning,
                         model->disable_flag_present ? "公共演示密钥已启用" : "公共演示密钥已启用 · 低安全模式",
                         17, COLOR(194, 61, 61));
    }
    draw_tabs(pixels, stride, model->parent_page);
    if (model->parent_page != PTC_UI_PARENT_PLAN) {
        draw_dialog_button(pixels, stride, ptc_ui_parent_refresh_rect(),
                           model->waiting ? "正在刷新状态…" : "Y  立即刷新状态",
                           model->waiting ? COLOR(215, 139, 25) : COLOR(28, 118, 188),
                           COLOR(255, 255, 255), false);
    }
    actions = actions_for_page(model->parent_page, &action_count);
    for (index = 0; index < action_count; ++index) {
        UiRect card = to_uirect(ptc_ui_parent_card_rect(index));
        PtcUiActionState astate = PTC_UI_ACTION_AVAILABLE;
        if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
            astate = ptc_ui_safety_action_available(model, index);
        } else if (model->disable_flag_present && model->parent_page == PTC_UI_PARENT_TODAY && index > 0) {
            astate = PTC_UI_ACTION_DISABLED;
        }
        const UiAction *action = &actions[index];
        if (model->parent_page == PTC_UI_PARENT_SUPPORT && index == 2 && model->disable_flag_present) {
            action = &RESUME_CONTROL_ACTION;
        }
        draw_action_card(pixels, stride, card, action, index == model->selected_index, astate);
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN) {
        draw_weekly_page(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_TODAY) {
        draw_today_status(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_SECURITY) {
        draw_grant_help(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
        draw_safety_status(pixels, stride, model);
    } else {
        draw_safety_status(pixels, stride, model);
    }
    if (model->parent_page != PTC_UI_PARENT_PLAN) draw_notice(pixels, stride, model, 522, 128);
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(0), "L  上一页");
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(1), "R  下一页");
    if (model->parent_page == PTC_UI_PARENT_PLAN) {
        draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(2),
                           model->disable_flag_present ? "紧急停用中" : "A  保存计划");
    }
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(3), "B  返回孩子页");
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
    char body[320];
    char first_line[320];
    const char *title = model->overlay == PTC_UI_OVERLAY_NUMPAD ? model->numpad_title : model->overlay_title;
    const char *description = model->overlay == PTC_UI_OVERLAY_NUMPAD ? model->numpad_guide : model->overlay_body;
    *dialog = (UiRect){(SCREEN_WIDTH - width) / 2, (SCREEN_HEIGHT - height) / 2 - 10, width, height};
    fill_rect(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, COLOR(226, 230, 236));
    fill_round_rect(pixels, stride, *dialog, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, *dialog, 2, COLOR(203, 211, 222));
    draw_text(pixels, stride, dialog->x + 34, dialog->y + 54, title, 29, COLOR(28, 34, 43));
    if (description[0]) {
        const char *current = description;
        int line_y = dialog->y + 88;
        int is_first = 1;
        while (current && *current) {
            const char *next = strchr(current, '\n');
            size_t len = next ? (size_t)(next - current) : strlen(current);
            if (len >= sizeof(first_line)) len = sizeof(first_line) - 1;
            memcpy(first_line, current, len);
            first_line[len] = '\0';

            int font_size = is_first ? 20 : 18;
            fit_text(body, sizeof(body), first_line, font_size, dialog->width - 68);
            draw_text(pixels, stride, dialog->x + 34, line_y, body, font_size, COLOR(91, 100, 114));

            line_y += font_size + 8;
            is_first = 0;
            current = next ? next + 1 : NULL;
        }
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
    draw_dialog_button(pixels, stride, ptc_ui_minutes_dec_rect(), "－5", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_inc_rect(), "＋5", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_inc_large_rect(), "＋15", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_dec_large_rect(), "－15", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 242, 640, 26}, duration, 19,
                     COLOR(28, 118, 188));

    if (model->status_loaded && ptc_date_from_day_index(model->day_index, &year, &month, &day)) {
        snprintf(date_line, sizeof(date_line), "影响日期：%u 年 %u 月 %u 日（今天）", year, month, day);
    } else {
        snprintf(date_line, sizeof(date_line), "影响日期：今天");
    }
    if (played_min >= 0) {
        snprintf(played_line, sizeof(played_line), "当前已玩：约 %d 分钟", played_min);
    } else {
        snprintf(played_line, sizeof(played_line), "当前已玩：暂不可用");
    }
    if (model->unrestricted_today == 1) {
        snprintf(remaining_line, sizeof(remaining_line), "当前剩余：不限时");
    } else if (model->remaining_available && model->remaining_minutes >= 0) {
        snprintf(remaining_line, sizeof(remaining_line), "当前剩余：%d 分钟", model->remaining_minutes);
    } else {
        snprintf(remaining_line, sizeof(remaining_line), "当前剩余：暂不可用");
    }
    if (preview_min >= 0) {
        snprintf(preview_line, sizeof(preview_line), "调整后剩余：约 %d 分钟", preview_min);
    } else if (model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        snprintf(preview_line, sizeof(preview_line), "调整后剩余将在刷新生效后确认");
    } else {
        snprintf(preview_line, sizeof(preview_line), "调整后总额度：%u 分钟；实际剩余将在刷新后确认", (unsigned int)model->draft_minutes);
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
                     "本次修改只影响今天 · Y 或点击数值手动输入", 16, COLOR(77, 86, 99));
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
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_up_rect(), "＋15", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_down_rect(), "－15", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_dec_rect(), "－5", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
        draw_dialog_button(pixels, stride, ptc_ui_weekly_min_inc_rect(), "＋5", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
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
    static const char *QUICK_LABELS[] = {"－15", "－5", "＋5", "＋15"};
    UiRect dialog;
    UiRect display = to_uirect(ptc_ui_numpad_display_rect());
    char shown[32];
    char current[64];
    char duration[64];
    uint16_t entered_minutes = 0;
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 620, 700);
    if ((model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
         model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) && model->numpad_text[0]) {
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
        snprintf(current, sizeof(current), "当前值：%u 分钟  ·  范围 %u–%u",
                 (unsigned int)model->numpad_current, (unsigned int)model->numpad_minimum,
                 (unsigned int)model->numpad_maximum);
        if (!ptc_ui_parse_minutes(model->numpad_text, model->numpad_minimum, model->numpad_maximum, &entered_minutes)) {
            entered_minutes = model->numpad_current;
        }
        format_duration(entered_minutes, duration, sizeof(duration));
    } else {
        if (model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) {
            snprintf(current, sizeof(current), "当前值：%u 分钟  ·  范围 %u–%u",
                     (unsigned int)model->numpad_current, (unsigned int)model->numpad_minimum,
                     (unsigned int)model->numpad_maximum);
            if (!ptc_ui_parse_minutes(model->numpad_text, model->numpad_minimum, model->numpad_maximum, &entered_minutes)) {
                entered_minutes = model->numpad_current;
            }
            format_duration(entered_minutes, duration, sizeof(duration));
        } else if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
            unsigned int len = (unsigned int)strlen(model->numpad_text);
            snprintf(current, sizeof(current), "请输入 8 位加时码  ·  当前已输入 %u/8 位", len);
            duration[0] = '\0';
        } else {
            snprintf(current, sizeof(current), "请输入完整的 8 位加时码");
            duration[0] = '\0';
        }
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 218, dialog.width - 80, 22}, current, 16, COLOR(91, 100, 114));
    if (duration[0]) {
        char duration_line[80];
        snprintf(duration_line, sizeof(duration_line), "换算：%s", duration);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 244, dialog.width - 80, 22},
                         duration_line, 17, COLOR(28, 118, 188));
    }
    if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
        model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) {
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

static void draw_confirm_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    bool danger = model->operation == PTC_UI_OPERATION_DISABLE_TODAY_LIMIT ||
                  model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT ||
                  model->operation == PTC_UI_OPERATION_SAVE_WEEKLY ||
                  model->operation == PTC_UI_OPERATION_EMERGENCY_DISABLE ||
                  model->operation == PTC_UI_OPERATION_RESUME_CONTROL ||
                  model->operation == PTC_UI_OPERATION_COMPLETE_SETUP ||
                  model->operation == PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT;
    draw_dialog_shell(pixels, stride, model, &dialog, 760, 330);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 154, 620, 64}, 8,
                    danger ? COLOR(255, 240, 240) : COLOR(240, 248, 244));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 154, 620, 64},
                     danger ? "请确认已了解这项操作的影响" : "确认执行这项操作", 22,
                     danger ? COLOR(194, 61, 61) : COLOR(25, 132, 95));
    draw_overlay_actions(pixels, stride, model, "A / +  确认执行");
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
    draw_dialog_shell(pixels, stride, model, &dialog, 900, 500);
    current_box = (UiRect){dialog.x + 42, dialog.y + 132, 600, 56};
    masked_value(model->credential_current, model->credential_kind == 1 || model->credential_revealed, current, sizeof(current));
    masked_value(model->credential_new, model->credential_kind == 1 || model->credential_new_revealed, next, sizeof(next));
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 122, "当前值", 17, COLOR(91, 100, 114));
    fill_round_rect(pixels, stride, current_box, 7, COLOR(244, 246, 249));
    draw_text(pixels, stride, current_box.x + 16, current_box.y + 36, current, 18, COLOR(45, 52, 62));
    if (model->credential_kind == 2) {
        draw_dialog_button(pixels, stride, ptc_ui_credential_reveal_rect(),
                           model->credential_revealed ? "X  隐藏当前密钥" : "X  显示当前密钥",
                           COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    }
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 215, "新值", 17, COLOR(91, 100, 114));
    fill_round_rect(pixels, stride, input_box, 7, COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, input_box, 2, COLOR(28, 118, 188));
    draw_text(pixels, stride, input_box.x + 16, input_box.y + 40, next, 18, COLOR(28, 34, 43));
    draw_dialog_button(pixels, stride, ptc_ui_credential_random_rect(), "Y  随机生成",
                       COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    if (model->credential_kind == 2) {
        draw_dialog_button(pixels, stride, ptc_ui_credential_demo_rect(),
                           model->demo_secret_enabled ? "R  退出演示并换新密钥" : "R  使用公共演示密钥",
                           model->demo_secret_enabled ? COLOR(235, 238, 243) : COLOR(255, 235, 238),
                           model->demo_secret_enabled ? COLOR(28, 118, 188) : COLOR(194, 61, 61), true);
        draw_text(pixels, stride, dialog.x + 332, dialog.y + 350,
                  "建议使用随机生成；手工密钥至少 32 个字符。", 17, COLOR(91, 100, 114));
    }
    draw_overlay_actions(pixels, stride, model, "A  手工输入    +  验证 PIN 并保存");
}

static void draw_grant_setup_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect qr_button = to_uirect(ptc_ui_grant_qr_rect());
    char minutes[64];
    char date[40];
    char base[160];
    char generate_label[64];
    int minutes_width;
    int minutes_x;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    draw_dialog_shell(pixels, stride, model, &dialog, 900, 570);
    if (ptc_date_from_day_index(model->day_index, &year, &month, &day)) {
        snprintf(date, sizeof(date), "%u-%02u-%02u（今天）",
                 (unsigned int)year, (unsigned int)month, (unsigned int)day);
    } else {
        snprintf(date, sizeof(date), "设备日期待刷新");
    }

    fill_round_rect(pixels, stride, qr_button, 8, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, (UiRect){qr_button.x, qr_button.y + 8, qr_button.width, 40},
                     "A  扫二维码生成加时码", 27, COLOR(255, 255, 255));
    draw_text_center(pixels, stride, (UiRect){qr_button.x, qr_button.y + 48, qr_button.width, 28},
                     "用手机扫描后，可收藏打开的网页地址", 17, COLOR(223, 239, 252));

    draw_dialog_button(pixels, stride, ptc_ui_grant_local_toggle_rect(),
                       model->grant_local_expanded ? "X  收起本机生成" : "X  在本机生成 8 位码",
                       model->grant_local_expanded ? COLOR(230, 244, 238) : COLOR(244, 246, 249),
                       model->grant_local_expanded ? COLOR(7, 93, 76) : COLOR(66, 74, 86), true);
    draw_dialog_button(pixels, stride, ptc_ui_grant_more_toggle_rect(),
                       model->grant_more_expanded ? "Y  收起更多设置" : "Y  更多设置",
                       COLOR(244, 246, 249), COLOR(66, 74, 86), true);

    if (model->grant_local_expanded) {
        snprintf(minutes, sizeof(minutes), "%u 分钟    上限 %u 分钟",
                 (unsigned int)model->grant_minutes, (unsigned int)model->grant_max_minutes);
        draw_text(pixels, stride, dialog.x + 52, dialog.y + 378, date, 17, COLOR(91, 100, 114));
        if (model->grant_has_code) {
            draw_text(pixels, stride, dialog.x + 520, dialog.y + 378,
                      "成功兑换后仅可使用一次", 15, COLOR(91, 100, 114));
        }
        minutes_width = measure_text(minutes, 22);
        minutes_x = dialog.x + (dialog.width - minutes_width) / 2;
        draw_horizontal_triangle(pixels, stride, minutes_x - 34, dialog.y + 393, 15, false, COLOR(28, 118, 188));
        draw_text(pixels, stride, minutes_x, dialog.y + 401, minutes, 22, COLOR(28, 118, 188));
        draw_horizontal_triangle(pixels, stride, minutes_x + minutes_width + 19, dialog.y + 393, 15, true, COLOR(28, 118, 188));
        if (model->grant_has_code) {
            snprintf(generate_label, sizeof(generate_label), "+  %s · 再生成一个", model->grant_code);
        } else {
            snprintf(generate_label, sizeof(generate_label), "+  生成今天有效的 8 位码");
        }
        draw_dialog_button(pixels, stride, ptc_ui_grant_generate_rect(), generate_label,
                           COLOR(7, 93, 76), COLOR(255, 255, 255), false);
    } else if (model->grant_more_expanded) {
        draw_dialog_button(pixels, stride, ptc_ui_grant_export_rect(), "+  导出配置",
                           COLOR(25, 132, 95), COLOR(255, 255, 255), false);
        draw_dialog_button(pixels, stride, ptc_ui_grant_edit_url_rect(), "R  编辑跳转地址",
                           COLOR(235, 238, 243), COLOR(28, 118, 188), true);
        draw_dialog_button(pixels, stride, ptc_ui_grant_reset_url_rect(), "ZR  恢复官方地址",
                           COLOR(235, 238, 243), COLOR(66, 74, 86), true);
        fit_text(base, sizeof(base), model->pairing_base_url, 16, 780);
        draw_text(pixels, stride, dialog.x + 52, dialog.y + 458, base, 16, COLOR(91, 100, 114));
    } else {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 374, 816, 54},
                         "二维码是推荐方式；本机生成和地址设置按需展开", 17, COLOR(91, 100, 114));
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  关闭",
                        COLOR(235, 238, 243), COLOR(66, 74, 86), true);
}

static void draw_qr_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    int size = qrcodegen_getSize(model->qr_code);
    int scale = size > 0 ? 420 / (size + 8) : 1;
    int total;
    int origin_x;
    int origin_y;
    int x;
    int y;
    if (scale < 2) scale = 2;
    total = (size + 8) * scale;
    draw_dialog_shell(pixels, stride, model, &dialog, 900, 610);
    origin_x = dialog.x + 42;
    origin_y = dialog.y + 126;
    fill_rect(pixels, stride, (UiRect){origin_x, origin_y, total, total}, COLOR(255, 255, 255));
    for (y = 0; y < size; ++y) {
        for (x = 0; x < size; ++x) {
            if (qrcodegen_getModule(model->qr_code, x, y)) {
                fill_rect(pixels, stride,
                          (UiRect){origin_x + (x + 4) * scale, origin_y + (y + 4) * scale, scale, scale},
                          COLOR(0, 0, 0));
            }
        }
    }
    
    draw_text(pixels, stride, dialog.x + 510, dialog.y + 150, "使用微信/相机扫码", 26, COLOR(28, 34, 43));
    draw_text(pixels, stride, dialog.x + 510, dialog.y + 194, "获取【任你玩】网页加时工具", 20, COLOR(28, 118, 188));

    fill_round_rect(pixels, stride, (UiRect){dialog.x + 510, dialog.y + 240, 330, 96}, 8, COLOR(240, 248, 245));
    draw_rect_outline(pixels, stride, (UiRect){dialog.x + 510, dialog.y + 240, 330, 96}, 2, COLOR(210, 235, 225));
    draw_text(pixels, stride, dialog.x + 526, dialog.y + 266, "建议收藏扫码后打开的网页地址", 17, COLOR(25, 132, 95));
    draw_text(pixels, stride, dialog.x + 526, dialog.y + 298, "拍照保存时，请妥善保护相册。", 17, COLOR(45, 60, 55));
    draw_text(pixels, stride, dialog.x + 526, dialog.y + 324, "二维码包含生成加时码的权限。", 16, COLOR(170, 65, 65));

    draw_text(pixels, stride, dialog.x + 510, dialog.y + 370, "网页会自动绑定本机，后续可随时生成加时码。", 15, COLOR(120, 130, 140));
    draw_text(pixels, stride, dialog.x + 510, dialog.y + 396, "不便扫码时，可返回展开“在本机生成 8 位码”。", 15, COLOR(120, 130, 140));

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  关闭二维码",
                       COLOR(235, 238, 243), COLOR(66, 74, 86), true);
}

static void draw_weekly_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 860, 350);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 150, dialog.width - 80, 34},
                     "周计划还有未保存的修改", 22, COLOR(215, 139, 25));
    draw_dialog_button(pixels, stride, ptc_ui_discard_rect(model->overlay), "放弃并离开",
                       COLOR(255, 235, 238), COLOR(194, 61, 61), true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "继续编辑",
                       COLOR(235, 238, 243), COLOR(66, 74, 86), true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "保存并离开",
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
    case PTC_UI_OVERLAY_CREDENTIAL:
        draw_credential_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_GRANT_SETUP:
        draw_grant_setup_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_QR:
        draw_qr_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
        draw_weekly_leave_overlay(pixels, stride, model);
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

void ptc_ui_graphics_draw(const PtcUiModel *model)
{
    uint32_t stride_bytes = 0;
    uint32_t *pixels;
    uint32_t stride;
    if (!model || !g_ui.framebuffer_ready) {
        return;
    }
    pixels = (uint32_t *)framebufferBegin(&g_ui.framebuffer, &stride_bytes);
    if (!pixels) {
        return;
    }
    stride = stride_bytes / sizeof(uint32_t);
    fill_rect(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, COLOR(244, 246, 249));
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
    framebufferEnd(&g_ui.framebuffer);
}
