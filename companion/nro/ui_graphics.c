#include "ui_graphics.h"

#include <switch.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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

static const UiAction TODAY_ACTIONS[] = {
    {"刷新状态", "读取今天的最新游玩状态", COLOR(42, 105, 188)},
    {"设置今日额度", "指定今天可玩的分钟数", COLOR(42, 105, 188)},
    {"临时加时", "在今天额度上增加分钟", COLOR(25, 132, 95)},
    {"今日不限时", "今天不设时间上限", COLOR(25, 132, 95)},
    {"恢复周计划", "清除今天的临时调整", COLOR(91, 100, 116)},
};

static const UiAction PLAN_ACTIONS[] = {
    {"每周计划", "每天选择限时或不限时", COLOR(42, 105, 188)},
};

static const UiAction SECURITY_ACTIONS[] = {
    {"修改设备名", "更改加时码绑定的设备名称", COLOR(42, 105, 188)},
    {"导入家长网页", "生成只供家长使用的导入文件", COLOR(25, 132, 95)},
    {"重置加时码密钥", "生成新的加时码信任关系", COLOR(194, 61, 61)},
    {"修改家长 PIN", "设置新的 6 位数字 PIN", COLOR(42, 105, 188)},
};

static const UiAction SUPPORT_ACTIONS[] = {
    {"确认接管系统控制", "预检、保存快照后启用额度管理", COLOR(42, 105, 188)},
    {"重试修复", "重新检查并恢复安全前置条件", COLOR(25, 132, 95)},
    {"紧急停用控制", "立即停止后台控制操作", COLOR(194, 61, 61)},
    {"恢复安装前状态", "恢复原始设置并停用 PlayWise", COLOR(194, 61, 61)},
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

static void draw_notice(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, int y)
{
    UiRect rect = {54, y, 1172, 128};
    uint32_t accent = COLOR(91, 100, 116);
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
    draw_text(pixels, stride, rect.x + 24, rect.y + 25, model->waiting ? "正在执行" : "最近执行", 18, accent);
    snprintf(
        execution,
        sizeof(execution),
        "命令：%s    %s",
        model->command_name[0] ? model->command_name : "未开始",
        model->transport_label[0] ? model->transport_label : "传输：未开始");
    fit_text(fitted, sizeof(fitted), execution, 18, rect.width - 48);
    draw_text(pixels, stride, rect.x + 24, rect.y + 50, fitted, 18, COLOR(77, 86, 99));
    fit_text(fitted, sizeof(fitted), model->message, 21, rect.width - 48);
    draw_text(pixels, stride, rect.x + 24, rect.y + 78, fitted, 20, COLOR(45, 52, 62));
    if (model->feedback_detail[0]) {
        fit_text(detail, sizeof(detail), model->feedback_detail, 17, rect.width - 48);
        draw_text(pixels, stride, rect.x + 24, rect.y + 105, detail, 16, accent);
    }
}

static void draw_child(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char today[32];
    char remaining[32];
    char played[32];
    const char *mode = strcmp(model->setup_phase, "active") == 0 ? "正常运行" :
        (strcmp(model->setup_phase, "protection") == 0 ? "保护模式" : "兼容性待确认");
    describe_status(model, today, sizeof(today), remaining, sizeof(remaining));
    if (model->played_minutes_available && model->played_minutes >= 0) {
        snprintf(played, sizeof(played), "约 %d 分钟", model->played_minutes);
    } else {
        snprintf(played, sizeof(played), "暂不可用");
    }
    draw_header(pixels, stride, "游玩时间", "查看今天的状态，使用家长提供的加时码");
    draw_status_tile(pixels, stride, (UiRect){54, 118, 278, 92}, "今日状态", today, COLOR(216, 49, 54));
    draw_status_tile(pixels, stride, (UiRect){350, 118, 278, 92}, "剩余时间", remaining, COLOR(25, 132, 95));
    draw_status_tile(pixels, stride, (UiRect){646, 118, 278, 92}, "已玩时间", played, COLOR(215, 139, 25));
    draw_status_tile(pixels, stride, (UiRect){942, 118, 284, 92}, "运行状态", mode, COLOR(28, 118, 188));

    fill_round_rect(pixels, stride, (UiRect){54, 238, 760, 246}, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, (UiRect){54, 238, 760, 246}, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, 86, 286, "自律小达人 · 加时奖励", 27, COLOR(28, 34, 43));
    draw_text(pixels, stride, 86, 322, "遵守约定、合理安排时间。加时之前，记得向窗外远眺至少 5 分钟，让眼睛放松一下吧！", 19, COLOR(85, 94, 107));
    fill_round_rect(pixels, stride, to_uirect(ptc_ui_child_submit_rect()), 8, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_submit_rect()), "A  输入加时码", 31, COLOR(255, 255, 255));

    fill_round_rect(pixels, stride, (UiRect){836, 238, 390, 246}, 8, COLOR(250, 251, 253));
    draw_rect_outline(pixels, stride, (UiRect){836, 238, 390, 246}, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, 866, 282, "状态详情", 24, COLOR(28, 34, 43));
    draw_text(pixels, stride, 866, 320, model->play_timer_enabled == 1 ? "游玩计时器：已开启" : "游玩计时器：未确认", 19, COLOR(77, 86, 99));
    draw_text(pixels, stride, 866, 352, "到期行为：系统提醒", 19, COLOR(77, 86, 99));
    draw_text(pixels, stride, 866, 384, "不会强制锁屏或关闭游戏", 19, COLOR(77, 86, 99));
    draw_text(pixels, stride, 866, 416,
        strcmp(model->setup_phase, "active") == 0 ? "额度管理：已启用" : "额度管理：尚未启用",
        20, strcmp(model->setup_phase, "active") == 0 ? COLOR(25, 132, 95) : COLOR(215, 139, 25));

    draw_notice(pixels, stride, model, 510);
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(0), "A  输入加时码");
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(1), "Y  刷新");
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), "B / +  退出");
}

static void draw_setup(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {154, 132, 972, 430};
    const char *phase = model->setup_phase[0] ? model->setup_phase : "pending";
    int64_t grace_remaining = ptc_ui_setup_grace_remaining(model, (int64_t)time(NULL));
    char phase_line[160];
    char countdown_line[80];
    draw_header(pixels, stride, grace_remaining >= 0 ? "正在同步" : "首次设置",
        grace_remaining >= 0 ? "系统设置正在同步，通常稍后完成" : "完成家长设置后才会接管系统控制");
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, 204, 190,
        grace_remaining >= 0 ? "环境检查已通过" :
        (strcmp(phase, "protection") == 0 ? "保护模式" : "兼容性待确认"), 31,
        model->setup_restriction_cleared ? COLOR(25, 132, 95) : COLOR(215, 139, 25));
    snprintf(phase_line, sizeof(phase_line), "当前状态：%s    安装前快照：%s",
        grace_remaining >= 0 ? "正在同步" : (strcmp(phase, "protection") == 0 ? "保护模式" : "等待家长确认"),
        model->setup_snapshot_available ? "已保存" : "不可用");
    draw_text(pixels, stride, 204, 248, phase_line, 22, COLOR(77, 86, 99));
    if (grace_remaining >= 0) {
        if (grace_remaining > 0) {
            snprintf(countdown_line, sizeof(countdown_line), "系统设置同步中（约 %lld 秒）…", (long long)grace_remaining);
        } else {
            snprintf(countdown_line, sizeof(countdown_line), "同步完成，正在启用额度管理…");
        }
        draw_text(pixels, stride, 204, 310, countdown_line, 34, COLOR(28, 118, 188));
        draw_text(pixels, stride, 204, 356, "无需操作；完成后将自动进入游玩时间页面。", 22, COLOR(45, 52, 62));
    } else if (strcmp(phase, "pending") == 0) {
        draw_text(pixels, stride, 204, 300, "系统设置正在同步，通常稍后完成。", 23, COLOR(45, 52, 62));
        draw_text(pixels, stride, 204, 344, "如长时间无变化，可进入【支持与恢复】选择重试修复。", 23, COLOR(45, 52, 62));
    } else if (strcmp(phase, "failed") == 0 || strcmp(phase, "protection") == 0) {
        draw_text(pixels, stride, 204, 300, "已暂停新的控制修改，状态和恢复功能仍可使用。", 23, COLOR(215, 139, 25));
        draw_text(pixels, stride, 204, 344, "请在【支持与恢复】查看当前唯一推荐操作。", 23, COLOR(45, 52, 62));
    } else if (strcmp(phase, "restored") == 0) {
        draw_text(pixels, stride, 204, 300, "已恢复至安装前状态。若要重新验证及启用控制，", 23, COLOR(45, 52, 62));
        draw_text(pixels, stride, 204, 344, "请清理状态文件并重启主机以重新开始首次引导。", 23, COLOR(45, 52, 62));
    } else {
        draw_text(pixels, stride, 204, 300, "1. 创建 6 位家长 PIN    2. 确认周计划与加时码设置", 23, COLOR(45, 52, 62));
        draw_text(pixels, stride, 204, 344, "3. 在【支持与恢复】确认接管系统控制", 23, COLOR(45, 52, 62));
    }
    draw_text(pixels, stride, 204, 400, model->message[0] ? model->message : "Y 可刷新后台状态。", 20, COLOR(91, 100, 116));
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(0), "Minus  家长设置");
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(1), "Y  刷新");
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), "B / +  退出");
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
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_retry_rect()), "A  重新输入", 25, COLOR(255, 255, 255));
    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 8, COLOR(235, 238, 243));
    draw_rect_outline(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 1, COLOR(203, 211, 222));
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_back_rect()), "B  返回主页", 25, COLOR(66, 74, 86));
}

static const UiAction *actions_for_page(PtcUiParentPage page, int *count)
{
    if (page == PTC_UI_PARENT_PLAN) {
        *count = (int)(sizeof(PLAN_ACTIONS) / sizeof(PLAN_ACTIONS[0]));
        return PLAN_ACTIONS;
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
        fill_round_rect(pixels, stride, (UiRect){rect.x + rect.width - 92, rect.y + 12, 72, 28}, 6, COLOR(25, 132, 95));
        draw_text_center(pixels, stride, (UiRect){rect.x + rect.width - 92, rect.y + 12, 72, 28}, "建议", 17, COLOR(255, 255, 255));
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
    uint32_t today_color;
    describe_status(model, today, sizeof(today), remaining, sizeof(remaining));
    if (model->played_minutes_available && model->played_minutes >= 0) {
        snprintf(played, sizeof(played), "约 %d 分钟", model->played_minutes);
    } else {
        snprintf(played, sizeof(played), "暂不可用");
    }
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
                    strcmp(model->setup_phase, "active") == 0 ? "正常运行" :
                    (strcmp(model->setup_phase, "protection") == 0 ? "保护模式" : "兼容性待确认"),
                    strcmp(model->setup_phase, "active") == 0 ? COLOR(25, 132, 95) : COLOR(215, 139, 25));
    draw_status_row(pixels, stride, panel, panel.y + 192, "系统计时器",
                    model->play_timer_enabled == 1 ? "已开启" : "未确认",
                    model->play_timer_enabled == 1 ? COLOR(25, 132, 95) : COLOR(91, 100, 116));
}

static void draw_parent(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const UiAction *actions;
    int action_count;
    int index;
    draw_header(pixels, stride, "家长时间管理", "本地规则与设备安全设置");
    draw_tabs(pixels, stride, model->parent_page);
    actions = actions_for_page(model->parent_page, &action_count);
    for (index = 0; index < action_count; ++index) {
        UiRect card = to_uirect(ptc_ui_parent_card_rect(index));
        PtcUiActionState astate = PTC_UI_ACTION_AVAILABLE;
        if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
            astate = ptc_ui_safety_action_available(model, index);
        }
        const UiAction *action = &actions[index];
        if (model->parent_page == PTC_UI_PARENT_SUPPORT && index == 2 && model->disable_flag_present) {
            action = &RESUME_CONTROL_ACTION;
        }
        draw_action_card(pixels, stride, card, action, index == model->selected_index, astate);
    }
    if (model->parent_page == PTC_UI_PARENT_TODAY) {
        draw_today_status(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
        draw_safety_status(pixels, stride, model);
    } else {
        draw_safety_status(pixels, stride, model);
    }
    draw_notice(pixels, stride, model, 522);
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(0), "L  上一页");
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(1), "R  下一页");
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(2), "Y  刷新结果");
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
    char body[192];
    char first_line[192];
    const char *second_line = NULL;
    const char *line_break;
    const char *title = model->overlay == PTC_UI_OVERLAY_NUMPAD ? model->numpad_title : model->overlay_title;
    const char *description = model->overlay == PTC_UI_OVERLAY_NUMPAD ? model->numpad_guide : model->overlay_body;
    *dialog = (UiRect){(SCREEN_WIDTH - width) / 2, (SCREEN_HEIGHT - height) / 2 - 10, width, height};
    fill_rect(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, COLOR(226, 230, 236));
    fill_round_rect(pixels, stride, *dialog, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, *dialog, 2, COLOR(203, 211, 222));
    draw_text(pixels, stride, dialog->x + 34, dialog->y + 54, title, 29, COLOR(28, 34, 43));
    if (description[0]) {
        line_break = strchr(description, '\n');
        if (line_break) {
            size_t first_length = (size_t)(line_break - description);
            if (first_length >= sizeof(first_line)) first_length = sizeof(first_line) - 1;
            memcpy(first_line, description, first_length);
            first_line[first_length] = '\0';
            second_line = line_break + 1;
        } else {
            snprintf(first_line, sizeof(first_line), "%s", description);
        }
        fit_text(body, sizeof(body), first_line, 20, dialog->width - 68);
        draw_text(pixels, stride, dialog->x + 34, dialog->y + 88, body, 20, COLOR(91, 100, 114));
        if (second_line && second_line[0]) {
            fit_text(body, sizeof(body), second_line, 18, dialog->width - 68);
            draw_text(pixels, stride, dialog->x + 34, dialog->y + 116, body, 18, COLOR(91, 100, 114));
        }
    }
}

static void draw_minutes_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect value_box = to_uirect(ptc_ui_minutes_value_rect());
    char value[32];
    char preview_line[128];
    int preview_min = ptc_ui_preview_remaining_minutes(model);
    int played_min = model->played_minutes_available ? model->played_minutes : -1;

    draw_dialog_shell(pixels, stride, model, &dialog, 720, 480);
    snprintf(value, sizeof(value), "%u 分钟", (unsigned int)model->draft_minutes);
    fill_round_rect(pixels, stride, value_box, 8, COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, value_box, 2, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, value_box, value, 37, COLOR(28, 118, 188));
    draw_dialog_button(pixels, stride, ptc_ui_minutes_dec_rect(), "－5", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_inc_rect(), "＋5", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_inc_large_rect(), "＋15", COLOR(235, 238, 243), COLOR(28, 118, 188), true);
    draw_dialog_button(pixels, stride, ptc_ui_minutes_dec_large_rect(), "－15", COLOR(235, 238, 243), COLOR(28, 118, 188), true);

    if (played_min >= 0) {
        snprintf(preview_line, sizeof(preview_line), "今日已玩 %d 分钟  │  修改后还可玩 %d 分钟", played_min, preview_min);
    } else {
        snprintf(preview_line, sizeof(preview_line), "修改后还可玩 %d 分钟", preview_min);
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 310, 640, 30}, preview_line, 21, COLOR(25, 132, 95));

    draw_text_center(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 342, 580, 26}, "Y 或点数值手动输入；上下 ±15，左右 ±5", 18, COLOR(77, 86, 99));
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
    UiRect dialog;
    UiRect display = to_uirect(ptc_ui_numpad_display_rect());
    char shown[32];
    char current[64];
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 620, 610);
    if (model->numpad_text[0]) {
        snprintf(shown, sizeof(shown), "%s", model->numpad_text);
    } else {
        snprintf(shown, sizeof(shown), "%.*s", model->numpad_max_digits, "________");
    }
    fill_round_rect(pixels, stride, display, 8, COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, display, 2, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, display, shown, 32, COLOR(28, 118, 188));

    if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES) {
        snprintf(current, sizeof(current), "当前值：%u 分钟  ·  范围 %u–%u",
                 (unsigned int)model->numpad_current, (unsigned int)model->numpad_minimum,
                 (unsigned int)model->numpad_maximum);
    } else {
        snprintf(current, sizeof(current), "请输入完整的 8 位加时码");
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 178, dialog.width - 80, 24}, current, 17, COLOR(91, 100, 114));
    for (index = 0; index < 12; ++index) {
        UiRect key = to_uirect(ptc_ui_numpad_key_rect(index));
        bool selected = index == model->numpad_cursor;
        fill_round_rect(pixels, stride, key, 8, selected ? COLOR(230, 242, 255) : COLOR(250, 251, 253));
        draw_rect_outline(pixels, stride, key, selected ? 3 : 1,
                          selected ? COLOR(28, 118, 188) : COLOR(203, 211, 222));
        draw_text_center(pixels, stride, key, KEY_LABELS[index], index == 9 || index == 11 ? 17 : 26,
                         selected ? COLOR(28, 118, 188) : COLOR(66, 74, 86));
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 35, dialog.y + 470, dialog.width - 70, 24},
                     "方向键/摇杆选择  A 输入  X 退格  Y 清空  + 完成", 17, COLOR(77, 86, 99));
    if (model->numpad_error[0]) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 35, dialog.y + 500, dialog.width - 70, 24},
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
