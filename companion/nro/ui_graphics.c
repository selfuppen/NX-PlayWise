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
    {"恢复周计划", "清除今日临时设置，恢复本周规则", COLOR(91, 100, 116)},
};

static const UiAction SECURITY_ACTIONS[] = {
    {"管理设备名", "查看、输入或随机生成设备名称", COLOR(42, 105, 188)},
    {"管理加时码密钥", "查看、生成或切换演示密钥", COLOR(194, 61, 61)},
    {"加时码生成", "手机扫码或在本机生成 8 位码", COLOR(25, 132, 95)},
    {"修改任我玩PIN", "验证当前 PIN 后设置新 PIN", COLOR(42, 105, 188)},
    {"家长区快捷键管理", "选择组合并管理孩子区提示", COLOR(42, 105, 188)},
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
    if (model->show_parent_shortcut_hint && model->custom_shortcut_enabled) {
        snprintf(parent_hint, sizeof(parent_hint), "%s  家长区", model->custom_shortcut_label);
    } else {
        snprintf(parent_hint, sizeof(parent_hint), "状态会在后台自动同步");
    }
    fit_text(fitted_hint, sizeof(fitted_hint), parent_hint, 16, ptc_ui_child_footer_rect(1).w - 16);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), fitted_hint, 16, COLOR(47, 57, 71));
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), "B / +  退出");
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
    snprintf(title, sizeof(title), "首次设置 · %d/4", step);
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
            UiRect compact_fixed = {204, 184, 872, 54};
            fill_round_rect(pixels, stride, compact_fixed, 8, COLOR(244, 249, 255));
            draw_rect_outline(pixels, stride, compact_fixed, 2, COLOR(28, 118, 188));
            draw_text(pixels, stride, 232, 218, "固定入口  Minus -  始终有效", 20, COLOR(28, 118, 188));
            draw_text(pixels, stride, 204, 264, "选择一个常见组合；按 A 只加入草稿，按 + 确认后才生效", 18, COLOR(77, 86, 99));
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
            draw_dialog_button(pixels, stride, ptc_ui_setup_shortcut_capture_rect(),
                               model->shortcut_capture_active ? "按住组合 · A 录入 · B 取消" : "X / 点击  录制其他组合",
                               model->shortcut_capture_active ? COLOR(255, 247, 229) : COLOR(235, 238, 243),
                               model->shortcut_capture_active ? COLOR(170, 109, 18) : COLOR(28, 118, 188), true);
            draw_text(pixels, stride, 204, 554,
                      model->shortcut_draft_enabled ? "待确认自定义组合：" : "待确认状态：仅保留 Minus -",
                      17, COLOR(91, 100, 116));
            if (model->shortcut_draft_enabled) {
                fit_text(fitted, sizeof(fitted), model->shortcut_draft_label, 18, 250);
                draw_text(pixels, stride, 420, 554, fitted, 18, COLOR(28, 118, 188));
            }
        } else if (step == PTC_UI_SETUP_PIN) {
            draw_text(pixels, stride, 204, 220, "设置 任我玩 PIN", 30, COLOR(28, 34, 43));
            draw_text(pixels, stride, 204, 262, "这是进入家长区、修改规则和安全设置时使用的本应用 PIN。", 21, COLOR(77, 86, 99));
            draw_text(pixels, stride, 204, 294, "请输入 1–64 位纯数字，系统会引导你再次输入确认。", 21, COLOR(77, 86, 99));
            draw_dialog_button(pixels, stride, ptc_ui_setup_pin_rect(), "A / 点击  设置或确认 PIN",
                               COLOR(28, 118, 188), COLOR(255, 255, 255), false);
            draw_text(pixels, stride, 204, 420, "PIN 少于 4 位时只提示弱保护风险，不会阻止保存。", 20, COLOR(215, 139, 25));
        } else if (step == PTC_UI_SETUP_TAKEOVER) {
            bool resuming_restored_setup = model->disable_flag_present && strcmp(phase, "restored") == 0;
            bool takeover_complete = ptc_ui_setup_takeover_complete(model);
            draw_text(pixels, stride, 204, 218,
                      takeover_complete ? "系统控制接管已完成" :
                      (resuming_restored_setup ? "解除停用并重新接管" : "确认接管系统控制"),
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
                          ? "此步骤已经完成；继续不会重复写入系统设置或重新开始同步宽限。"
                          : resuming_restored_setup
                           ? "确认后会重新执行只读兼容预检；通过后才解除紧急停用并重新接管。"
                           : "确认后会先执行只读兼容预检，再保存安装前快照并启用额度管理。",
                      21, COLOR(45, 52, 62));
            draw_text(pixels, stride, 204, 360,
                      takeover_complete
                          ? "按 A 或点击继续，返回第 4 步选择孩子区或家长区。"
                          : "接管成功后会保留同步宽限，系统控制不会立即跳变。",
                      21, COLOR(45, 52, 62));
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(),
                               takeover_complete ? "A / 点击  继续到第 4 步" :
                               (resuming_restored_setup ? "A / 点击  解除停用并重新接管" : "A / 点击  确认接管"),
                               takeover_complete ? COLOR(25, 132, 95) : COLOR(28, 118, 188),
                               COLOR(255, 255, 255), false);
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
                                     model->show_parent_shortcut_hint && model->custom_shortcut_enabled
                                        ? model->custom_shortcut_label : "家长区快捷提示未显示", 17, COLOR(77, 86, 99));
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     "家长区需要输入 任我玩 PIN", 17, COLOR(91, 100, 116));
                } else {
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 86, card.width - 36, 26},
                                     "返回孩子区：B", 19, COLOR(77, 86, 99));
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     model->custom_shortcut_enabled ? "当前入口：Minus - 和自定义组合" : "当前入口：仅 Minus -", 17, COLOR(91, 100, 116));
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
    UiRect panel = {842, 176, 384, 324};
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, panel.x + 26, panel.y + 43, "加时码如何生成", 23, COLOR(28, 34, 43));
    draw_text(pixels, stride, panel.x + 26, panel.y + 78, "1. 手机扫描 Switch 二维码", 17, COLOR(77, 86, 99));
    draw_text(pixels, stride, panel.x + 26, panel.y + 108, "2. 网页选择加时时长", 17, COLOR(77, 86, 99));
    draw_text(pixels, stride, panel.x + 26, panel.y + 138, "3. 将生成的 8 位码交给孩子", 17, COLOR(77, 86, 99));
    draw_text(pixels, stride, panel.x + 26, panel.y + 184, "也可以在本机生成", 18, COLOR(28, 118, 188));
    draw_text(pixels, stride, panel.x + 26, panel.y + 212, "打开“加时码生成”，选择", 16, COLOR(91, 100, 116));
    draw_text(pixels, stride, panel.x + 26, panel.y + 238, "“在本机生成 8 位码”。", 16, COLOR(91, 100, 116));
    draw_text(pixels, stride, panel.x + 26, panel.y + 274, "当天有效，兑换成功后仅用一次。", 16, COLOR(91, 100, 116));
    if (model->demo_secret_enabled) {
        fill_round_rect(pixels, stride, (UiRect){panel.x + 20, panel.y + 286, panel.width - 40, 30}, 6, COLOR(255, 235, 238));
        draw_text_center(pixels, stride, (UiRect){panel.x + 20, panel.y + 286, panel.width - 40, 30},
                         "公共演示密钥已启用", 16, COLOR(194, 61, 61));
    }
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
    fill_round_rect(pixels, stride, rect, 8, COLOR(248, 250, 253));
    draw_rect_outline(pixels, stride, rect, 2, accent);
    draw_text_center(pixels, stride, (UiRect){rect.x, rect.y + 10, rect.width, 26}, label, 16, COLOR(91, 100, 114));
    draw_text_center(pixels, stride, (UiRect){rect.x, rect.y + 38, rect.width, 38}, value, 23, accent);
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
    int slot;
    char minutes[32];
    char freshness[64];
    uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
    format_status_age(model, freshness, sizeof(freshness));
    draw_text(pixels, stride, 54, 184,
              model->disable_flag_present
                ? "周一到周日 · 紧急停用中，仅可查看和放弃已有草稿"
                : "周一到周日 · 点按整张卡片或选中后按 A 编辑",
              20, model->disable_flag_present ? COLOR(194, 61, 61) : COLOR(77, 86, 99));
    draw_text(pixels, stride, 864, 184, freshness, 17, status_age_color(model));
    for (slot = 0; slot < 7; ++slot) {
        int day = ptc_ui_weekday_for_display_slot(slot);
        bool selected = day == model->editor_index && model->selected_index == 0;
        bool today = day == weekday;
        bool selected_today = selected && today;
        UiRect card = to_uirect(ptc_ui_weekly_day_rect(slot));
        uint32_t background = day == 0 ? COLOR(255, 245, 245) : (day == 6 ? COLOR(255, 249, 238) : COLOR(255, 255, 255));
        uint32_t border = today ? COLOR(25, 132, 95) : (selected ? COLOR(28, 118, 188) : (day == 0 ? COLOR(218, 118, 118) : (day == 6 ? COLOR(220, 161, 65) : COLOR(219, 225, 233))));
        uint32_t primary_text = selected_today ? COLOR(255, 255, 255) : COLOR(28, 34, 43);
        uint32_t rule_text = selected_today ? COLOR(255, 255, 255) :
            (model->disable_flag_present ? COLOR(145, 154, 168) : COLOR(28, 118, 188));
        uint32_t detail_text = selected_today ? COLOR(224, 239, 255) :
            (model->disable_flag_present ? COLOR(145, 154, 168) : COLOR(77, 86, 99));
        if (selected) background = selected_today ? COLOR(28, 118, 188) : COLOR(244, 249, 255);
        fill_round_rect(pixels, stride, card, 8, background);
        draw_rect_outline(pixels, stride, card, today ? 4 : (selected ? 3 : 1), border);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 14, card.width, 34}, DAYS[day], 21, primary_text);
        if (today) draw_text_center(pixels, stride, (UiRect){card.x, card.y + 45, card.width, 24}, "今天 · 当前", 16,
                                    selected_today ? COLOR(220, 255, 241) : COLOR(25, 132, 95));
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 75, card.width, 34},
                         rule_mode_label(model->draft_week[day].mode), 22, rule_text);
        if (model->draft_week[day].mode == PTC_RULE_MODE_LIMIT) {
            snprintf(minutes, sizeof(minutes), "%u 分钟", (unsigned int)model->draft_week[day].minutes);
        } else {
            snprintf(minutes, sizeof(minutes), "无需分钟");
        }
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 125, card.width, 34}, minutes, 19, detail_text);
    }
    if (ptc_ui_weekly_today_changed(model)) {
        char current_value[48];
        char after_value[48];
        int current_minutes = model->remaining_available ? model->remaining_minutes : -1;
        if (model->unrestricted_today == 1) snprintf(current_value, sizeof(current_value), "不限时");
        else format_duration(current_minutes, current_value, sizeof(current_value));
        format_rule_remaining_label(model, model->draft_week[weekday], after_value, sizeof(after_value));
        draw_time_state_card(pixels, stride, (UiRect){64, 392, 300, 76},
                             model->today_override_present ? "当前有效剩余" : "当前还能玩",
                             current_value,
                             time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                               model->unrestricted_today == 1, current_minutes));
        draw_text_center(pixels, stride, (UiRect){364, 410, 52, 38}, "→", 27, COLOR(91, 100, 114));
        draw_time_state_card(pixels, stride, (UiRect){416, 392, 330, 76},
                             model->today_override_present ? "保存并在之后恢复周计划后预计" : "保存后预计还能玩",
                             after_value,
                             time_state_accent(model->draft_week[weekday].mode == PTC_RULE_MODE_UNLIMITED ||
                                               model->played_minutes_available,
                                               model->draft_week[weekday].mode == PTC_RULE_MODE_UNLIMITED,
                                               model->draft_week[weekday].mode == PTC_RULE_MODE_LIMIT && model->played_minutes_available
                                                  ? (int)model->draft_week[weekday].minutes - model->played_minutes : -1));
    }
    if (model->disable_flag_present) {
        draw_text(pixels, stride, 64, model->today_override_present ? 474 : 486,
                  "紧急停用中，周计划暂时只读；解除停用后才能修改和保存",
                  17, COLOR(194, 61, 61));
        if (model->today_override_present) {
            draw_text(pixels, stride, 64, 500, "保存周计划不会清除今天的临时设置",
                      17, COLOR(215, 139, 25));
        }
    } else {
        draw_text(pixels, stride, 64, 486,
                  model->today_override_present
                    ? "保存周计划不会清除今天的临时设置 · 方向键选择 · A 确定 · Y 刷新"
                    : "方向键选择 · A 确定 · B 返回 · Y 刷新",
                  17, model->today_override_present ? COLOR(215, 139, 25) : COLOR(77, 86, 99));
    }
    draw_candidate_button(pixels, stride, ptc_ui_weekly_mode_rect(), "X  切换模式",
                          COLOR(244, 246, 249), COLOR(28, 118, 188), model->selected_index == 1,
                          model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_discard_rect(), "ZL  放弃修改",
                          COLOR(244, 246, 249), COLOR(66, 74, 86), model->selected_index == 2,
                          !model->weekly_dirty);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_save_rect(),
                          model->disable_flag_present ? "紧急停用中" : (model->weekly_dirty ? "+  保存计划" : "+  计划未修改"),
                          COLOR(28, 118, 188), COLOR(255, 255, 255), model->selected_index == 3,
                          !model->weekly_dirty || model->disable_flag_present);
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
    draw_dialog_button(pixels, stride, ptc_ui_parent_refresh_rect(),
                       model->waiting ? "正在刷新状态…" : "Y  立即刷新状态",
                       model->waiting ? COLOR(215, 139, 25) : COLOR(28, 118, 188),
                       COLOR(255, 255, 255), false);
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
        if (model->parent_page == PTC_UI_PARENT_SUPPORT && model->disable_flag_present &&
            (index == 0 || index == 2)) {
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
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(2), "B  返回孩子页");
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
    static const char *QUICK_LABELS[] = {"ZL －15", "L －5", "R ＋5", "ZR ＋15"};
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
            uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
            PtcDayRule entered_rule;
            snprintf(current, sizeof(current), "当前值：%u 分钟  ·  范围 %u–%u",
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
                 model->today_override_present ? "当前有效剩余" : "当前还能玩", current_value);
        snprintf(right, sizeof(right), "%s：%s",
                 model->today_override_present ? "保存并在之后恢复周计划后预计" : "保存后预计还能玩", after_value);
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
    char comparison[128];
    PtcUiModel shell_model;
    bool restore = model->operation == PTC_UI_OPERATION_RESTORE_TODAY_POLICY;
    bool limit_change = model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT;
    bool code_preview = model->operation == PTC_UI_OPERATION_REDEEM_OFFLINE_CODE;
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
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "将清除今天的临时额度或不限时状态。");
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
                             "当前还能玩", current_value,
                             time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                               model->unrestricted_today == 1, model->remaining_minutes));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 354, dialog.y + 166, 52, 42}, "→", 28, COLOR(91, 100, 114));
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 406, dialog.y + 142, 300, 92},
                             "兑换后预计还能玩", after_value,
                             time_state_accent(model->code_preview_after_available, false,
                                               model->code_preview_after_minutes));
    } else if (restore) {
        uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
        PtcDayRule current = model->today_override_present ? model->today_override_rule : model->current_week[weekday];
        PtcDayRule after = model->current_week[weekday];
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
                             "当前还能玩", current_value,
                             time_state_accent(current.mode == PTC_RULE_MODE_UNLIMITED || current_minutes >= 0,
                                               current.mode == PTC_RULE_MODE_UNLIMITED, current_minutes));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 354, dialog.y + 166, 52, 42}, "→", 28, COLOR(91, 100, 114));
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
                             "今天已玩", played_value,
                             model->played_minutes_available ? COLOR(28, 118, 188) : COLOR(215, 139, 25));
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 273, dialog.y + 142, 214, 92},
                             "当前还能玩", current_value,
                             time_state_accent(model->unrestricted_today == 1 || model->remaining_available,
                                               model->unrestricted_today == 1, model->remaining_minutes));
        draw_time_state_card(pixels, stride, (UiRect){dialog.x + 512, dialog.y + 142, 214, 92},
                             "修改后还能玩", after_value,
                             time_state_accent(after_minutes >= 0, false, after_minutes));
    } else if (model->confirm_hold_required && model->played_minutes_available) {
        snprintf(comparison, sizeof(comparison), "已玩 %d 分钟       还剩 0 分钟",
                 model->played_minutes);
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 218, 652, 92}, 8, COLOR(255, 232, 235));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 226, 652, 34}, comparison, 25, COLOR(194, 61, 61));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 264, 652, 34},
                         "新额度不高于已玩时间，保存后会马上限制儿童使用", 20, COLOR(194, 61, 61));
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
        } else {
            draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                             model->confirm_hold_required
                                ? (model->played_minutes_available ? "操作后可能立即限制游玩，请长按 A 确认" : "无法取得今天已玩时间，不能判断是否立即限制")
                                : (limit_change && model->unrestricted_today == 1 ? "不限时将改为限时，请确认状态变化" : "请确认状态变化"),
                             19, model->confirm_hold_required ? COLOR(194, 61, 61) : COLOR(170, 109, 18));
        }
    } else if (!model->confirm_hold_required || !model->played_minutes_available) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 230, 620, 72}, 8,
                        danger ? COLOR(255, 240, 240) : COLOR(240, 248, 244));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 230, 620, 72},
                         danger ? "请确认已了解这项操作的影响" : "确认执行这项操作", 22,
                         danger ? COLOR(194, 61, 61) : COLOR(25, 132, 95));
    }
    draw_overlay_actions(pixels, stride, model,
                         model->confirm_hold_required ? "长按 A  确认执行" : "A / +  确认执行");
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
              "方向键选择 · A 确定 · X 手工输入 · + 保存", 16, COLOR(77, 86, 99));
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
    draw_text_center(pixels, stride, (UiRect){dialog.x + 354, dialog.y + 166, 52, 42}, "→", 28, COLOR(91, 100, 114));
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
                        : "错误 PIN 不会保留；重新输入时键盘为空",
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

static void draw_grant_setup_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect qr_button = to_uirect(ptc_ui_grant_qr_rect());
    char base[160];
    draw_dialog_shell(pixels, stride, model, &dialog, 900, 570);

    fill_round_rect(pixels, stride, qr_button, 8,
                    model->overlay_selection == PTC_UI_GRANT_SETUP_QR ? COLOR(230, 242, 255) : COLOR(28, 118, 188));
    draw_rect_outline(pixels, stride, qr_button,
                      model->overlay_selection == PTC_UI_GRANT_SETUP_QR ? 3 : 1,
                      model->overlay_selection == PTC_UI_GRANT_SETUP_QR ? COLOR(28, 118, 188) : COLOR(28, 118, 188));
    draw_text_center(pixels, stride, (UiRect){qr_button.x, qr_button.y + 8, qr_button.width, 40},
                     "Y  扫二维码生成加时码", 27,
                     model->overlay_selection == PTC_UI_GRANT_SETUP_QR ? COLOR(28, 118, 188) : COLOR(255, 255, 255));
    draw_text_center(pixels, stride, (UiRect){qr_button.x, qr_button.y + 48, qr_button.width, 28},
                     "用手机扫描后，可收藏打开的网页地址", 17,
                     model->overlay_selection == PTC_UI_GRANT_SETUP_QR ? COLOR(77, 86, 99) : COLOR(223, 239, 252));
    if (model->overlay_selection == PTC_UI_GRANT_SETUP_QR) {
        draw_text(pixels, stride, qr_button.x + qr_button.width - 34, qr_button.y + 26, "A", 16, COLOR(28, 118, 188));
    }

    draw_candidate_button(pixels, stride, ptc_ui_grant_local_toggle_rect(),
                          "X  进入本机生成 8 位码",
                          COLOR(244, 246, 249), COLOR(7, 93, 76),
                          model->overlay_selection == PTC_UI_GRANT_SETUP_LOCAL, false);
    draw_candidate_button(pixels, stride, ptc_ui_grant_more_toggle_rect(),
                          model->grant_more_expanded ? "收起更多设置" : "更多设置",
                          COLOR(244, 246, 249), COLOR(66, 74, 86),
                          model->overlay_selection == PTC_UI_GRANT_SETUP_MORE, false);

    if (model->grant_more_expanded) {
        draw_candidate_button(pixels, stride, ptc_ui_grant_export_rect(), "+  导出配置",
                              COLOR(25, 132, 95), COLOR(255, 255, 255),
                              model->overlay_selection == PTC_UI_GRANT_SETUP_EXPORT, false);
        draw_candidate_button(pixels, stride, ptc_ui_grant_edit_url_rect(), "R  编辑跳转地址",
                              COLOR(244, 246, 249), COLOR(28, 118, 188),
                              model->overlay_selection == PTC_UI_GRANT_SETUP_EDIT_URL, false);
        draw_candidate_button(pixels, stride, ptc_ui_grant_reset_url_rect(), "ZR  恢复官方地址",
                              COLOR(244, 246, 249), COLOR(66, 74, 86),
                              model->overlay_selection == PTC_UI_GRANT_SETUP_RESET_URL, false);
        fit_text(base, sizeof(base), model->pairing_base_url, 16, 780);
        draw_text(pixels, stride, dialog.x + 52, dialog.y + 446, base, 16, COLOR(91, 100, 114));
    } else {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 374, 816, 54},
                         "二维码是推荐方式；本机生成会进入独立时间选择页面", 17, COLOR(91, 100, 114));
    }
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 474,
              "方向键选择 · A 确定 · Y 二维码 · X 本机生成 · B 返回", 16, COLOR(77, 86, 99));
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  关闭",
                        COLOR(235, 238, 243), COLOR(66, 74, 86), true);
}

static void draw_shortcut_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char status[192];
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
    draw_dialog_button(pixels, stride, ptc_ui_shortcut_capture_rect(),
                       model->shortcut_capture_active ? "按住组合 · A 录入 · B 取消" : "X  录制其他组合",
                       COLOR(244, 246, 249), COLOR(28, 118, 188), true);
    draw_dialog_button(pixels, stride, ptc_ui_shortcut_disable_rect(),
                       "ZL  关闭自定义快捷键", COLOR(255, 244, 244), COLOR(194, 61, 61), true);
    draw_dialog_button(pixels, stride, ptc_ui_shortcut_hint_rect(),
                       model->shortcut_draft_show_hint ? "Y  孩子区提示：显示" : "Y  孩子区提示：隐藏",
                       COLOR(244, 246, 249), COLOR(66, 74, 86), true);
    snprintf(status, sizeof(status), "%s    当前草稿：%s",
             model->shortcut_draft_enabled ? "自定义入口：启用" : "自定义入口：关闭（仅 Minus -）",
             model->shortcut_draft_enabled ? model->shortcut_draft_label : "无");
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 516, status, 18,
              model->shortcut_draft_enabled ? COLOR(25, 132, 95) : COLOR(194, 61, 61));
    draw_overlay_actions(pixels, stride, model, "+  确认保存");
}

static void draw_grant_local_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *ADJUST[] = {"上一档", "下一档", "L －15", "R ＋15", "ZL －30", "ZR ＋30"};
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
                         "今天已玩", played,
                         status_available && model->played_minutes_available ? COLOR(28, 118, 188) : COLOR(215, 139, 25));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 335, dialog.y + 112, 250, 84},
                         "当前还能玩", remaining,
                         time_state_accent(status_available && (model->unrestricted_today == 1 || model->remaining_available),
                                           model->unrestricted_today == 1, model->remaining_minutes));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 628, dialog.y + 112, 250, 84},
                         "兑换后预计还能玩", estimate,
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
        snprintf(issued_detail, sizeof(issued_detail), "已生成：增加 %u 分钟 · 兑换后预计 %s（按生成时状态估算%s） · 签发日 %s",
                 (unsigned int)model->grant_issued_minutes, frozen,
                 model->grant_estimate_capped ? "，受每日 24 小时上限限制" : "", issued_date);
        draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 390, dialog.width - 84, 24},
                         issued_detail, 16, COLOR(7, 93, 76));
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 420, dialog.width - 84, 24},
                     "同日签发的其他代码可能仍可兑换；调整时长不会使已签发代码失效。", 16, COLOR(194, 61, 61));
    snprintf(issued_detail, sizeof(issued_detail), "%s  ·  %s",
             date, estimate_capped ? "预计时间受每日 24 小时上限限制" : "仅在代码成功兑换后生效");
    draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 448, dialog.width - 84, 24},
                     issued_detail, 16, estimate_capped ? COLOR(215, 139, 25) : COLOR(91, 100, 114));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 42, dialog.y + 474, dialog.width - 84, 20},
                     "方向键选择 · A 确定 · L/R、ZL/ZR 调整 · + 生成 · B 返回", 15, COLOR(77, 86, 99));
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
    draw_text(pixels, stride, dialog.x + 510, dialog.y + 194, "获取【任我玩】网页加时工具", 20, COLOR(28, 118, 188));

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
    bool refreshing = strcmp(model->overlay_title, "刷新周计划？") == 0;
    bool disabled = model->disable_flag_present;
    draw_dialog_shell(pixels, stride, model, &dialog, 860, 350);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 150, dialog.width - 80, 34},
                     "周计划还有未保存的修改", 22, COLOR(215, 139, 25));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 190, dialog.width - 80, 24},
                     "左右选择 · A 确认 · 也可直接使用按钮快捷键", 17, COLOR(91, 100, 114));
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
                     "左右选择 · A 确定 · B 继续编辑", 17, COLOR(91, 100, 114));
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

static void draw_diagnostic_result_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect details;
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 420);
    details = (UiRect){dialog.x + 34, dialog.y + 126, dialog.width - 68, 160};
    fill_round_rect(pixels, stride, details, 8, COLOR(248, 250, 253));
    draw_rect_outline(pixels, stride, details, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, details.x + 24, details.y + 42, "已生成到", 18, COLOR(103, 111, 124));
    draw_text(pixels, stride, details.x + 140, details.y + 42, model->diagnostic_path, 18, COLOR(28, 34, 43));
    draw_text(pixels, stride, details.x + 24, details.y + 90, "使用方式", 18, COLOR(103, 111, 124));
    draw_text(pixels, stride, details.x + 140, details.y + 90, "请将该文件提交到项目的 GitHub Issue 中进行反馈", 18, COLOR(25, 132, 95));
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  关闭",
                       COLOR(28, 118, 188), COLOR(255, 255, 255), false);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 378, "也可按 B 返回", 16, COLOR(91, 100, 114));
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
    case PTC_UI_OVERLAY_DIAGNOSTIC_RESULT:
        draw_diagnostic_result_overlay(pixels, stride, model);
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
