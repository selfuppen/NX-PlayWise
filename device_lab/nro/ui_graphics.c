#include "ui_graphics.h"

#include <switch.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

typedef struct {
    Framebuffer framebuffer;
    FT_Library library;
    FT_Face face;
    bool framebuffer_ready;
    bool font_ready;
    bool pl_ready;
} Graphics;

static Graphics g;

static uint32_t packed(uint32_t color)
{
    return RGBA8_MAXALPHA((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
}

static void pixel(uint32_t *pixels, uint32_t stride, int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && x < SCREEN_WIDTH && y < SCREEN_HEIGHT) pixels[y * stride + x] = color;
}

static void blend(uint32_t *pixels, uint32_t stride, int x, int y, uint32_t color, uint8_t alpha)
{
    uint32_t dst;
    unsigned int red;
    unsigned int green;
    unsigned int blue;
    unsigned int dst_red;
    unsigned int dst_green;
    unsigned int dst_blue;
    if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT || alpha == 0) return;
    if (alpha == 255) { pixel(pixels, stride, x, y, color); return; }
    dst = pixels[y * stride + x];
    red = color & 0xff;
    green = (color >> 8) & 0xff;
    blue = (color >> 16) & 0xff;
    dst_red = dst & 0xff;
    dst_green = (dst >> 8) & 0xff;
    dst_blue = (dst >> 16) & 0xff;
    pixels[y * stride + x] = RGBA8_MAXALPHA(
        (red * alpha + dst_red * (255U - alpha)) / 255U,
        (green * alpha + dst_green * (255U - alpha)) / 255U,
        (blue * alpha + dst_blue * (255U - alpha)) / 255U);
}

static void fill(uint32_t *pixels, uint32_t stride, PtcLabUiRect rect, uint32_t color)
{
    int y;
    uint32_t value = packed(color);
    for (y = rect.y < 0 ? 0 : rect.y; y < rect.y + rect.h && y < SCREEN_HEIGHT; ++y) {
        int x0 = rect.x < 0 ? 0 : rect.x;
        int x1 = rect.x + rect.w > SCREEN_WIDTH ? SCREEN_WIDTH : rect.x + rect.w;
        int x;
        for (x = x0; x < x1; ++x) pixels[y * stride + x] = value;
    }
}

static void round_rect(uint32_t *pixels, uint32_t stride, PtcLabUiRect rect, int radius, uint32_t color)
{
    int y;
    fill(pixels, stride, (PtcLabUiRect){rect.x + radius, rect.y, rect.w - radius * 2, rect.h}, color);
    fill(pixels, stride, (PtcLabUiRect){rect.x, rect.y + radius, rect.w, rect.h - radius * 2}, color);
    for (y = 0; y < radius; ++y) {
        int x;
        for (x = 0; x < radius; ++x) {
            int dx = radius - x;
            int dy = radius - y;
            if (dx * dx + dy * dy <= radius * radius) {
                pixel(pixels, stride, rect.x + x, rect.y + y, packed(color));
                pixel(pixels, stride, rect.x + rect.w - 1 - x, rect.y + y, packed(color));
                pixel(pixels, stride, rect.x + x, rect.y + rect.h - 1 - y, packed(color));
                pixel(pixels, stride, rect.x + rect.w - 1 - x, rect.y + rect.h - 1 - y, packed(color));
            }
        }
    }
}

static void outline(uint32_t *pixels, uint32_t stride, PtcLabUiRect rect, int width, uint32_t color)
{
    fill(pixels, stride, (PtcLabUiRect){rect.x, rect.y, rect.w, width}, color);
    fill(pixels, stride, (PtcLabUiRect){rect.x, rect.y + rect.h - width, rect.w, width}, color);
    fill(pixels, stride, (PtcLabUiRect){rect.x, rect.y, width, rect.h}, color);
    fill(pixels, stride, (PtcLabUiRect){rect.x + rect.w - width, rect.y, width, rect.h}, color);
}

static uint32_t lab_decode_utf8(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    uint32_t code;
    if (p[0] < 0x80) { *text += 1; return p[0]; }
    if ((p[0] & 0xe0) == 0xc0 && p[1]) { code = ((p[0] & 0x1f) << 6) | (p[1] & 0x3f); *text += 2; return code; }
    if ((p[0] & 0xf0) == 0xe0 && p[1] && p[2]) { code = ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f); *text += 3; return code; }
    if ((p[0] & 0xf8) == 0xf0 && p[1] && p[2] && p[3]) { code = ((p[0] & 7) << 18) | ((p[1] & 0x3f) << 12) | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f); *text += 4; return code; }
    *text += 1;
    return '?';
}

static bool font_size(int size)
{
    return g.font_ready && FT_Set_Pixel_Sizes(g.face, 0, (FT_UInt)size) == 0;
}

static int text_width(const char *text, int size)
{
    const char *cursor = text;
    int width = 0;
    if (!text || !font_size(size)) return 0;
    while (*cursor) {
        uint32_t codepoint = lab_decode_utf8(&cursor);
        if (FT_Load_Char(g.face, codepoint, FT_LOAD_DEFAULT) == 0) width += (int)(g.face->glyph->advance.x >> 6);
    }
    return width;
}

static void text(uint32_t *pixels, uint32_t stride, int x, int baseline, const char *value, int size, uint32_t color)
{
    const char *cursor = value;
    int pen = x;
    uint32_t resolved = packed(color);
    if (!value || !font_size(size)) return;
    while (*cursor) {
        uint32_t codepoint = lab_decode_utf8(&cursor);
        FT_GlyphSlot glyph;
        int row;
        if (FT_Load_Char(g.face, codepoint, FT_LOAD_RENDER) != 0) continue;
        glyph = g.face->glyph;
        for (row = 0; row < (int)glyph->bitmap.rows; ++row) {
            int column;
            for (column = 0; column < (int)glyph->bitmap.width; ++column) {
                blend(pixels, stride, pen + glyph->bitmap_left + column,
                    baseline - glyph->bitmap_top + row, resolved,
                    glyph->bitmap.buffer[row * glyph->bitmap.pitch + column]);
            }
        }
        pen += (int)(glyph->advance.x >> 6);
    }
}

static void centered(uint32_t *pixels, uint32_t stride, PtcLabUiRect rect, const char *value, int size, uint32_t color)
{
    text(pixels, stride, rect.x + (rect.w - text_width(value, size)) / 2,
        rect.y + (rect.h + size) / 2 - 3, value, size, color);
}

static int wrapped(uint32_t *pixels, uint32_t stride, int x, int baseline, const char *value,
    int size, int max_width, int line_height, int max_lines, uint32_t color)
{
    const char *cursor = value ? value : "";
    int line = 0;
    while (*cursor && line < max_lines) {
        const char *end = cursor;
        int width = 0;
        char buffer[512];
        while (*end && *end != '\n') {
            const char *next = end;
            uint32_t codepoint = lab_decode_utf8(&next);
            int advance = 0;
            if (font_size(size) && FT_Load_Char(g.face, codepoint, FT_LOAD_DEFAULT) == 0)
                advance = (int)(g.face->glyph->advance.x >> 6);
            if (end > cursor && width + advance > max_width) break;
            width += advance;
            end = next;
        }
        if (end == cursor) { const char *next = cursor; (void)lab_decode_utf8(&next); end = next; }
        {
            size_t bytes = (size_t)(end - cursor);
            if (bytes >= sizeof(buffer)) bytes = sizeof(buffer) - 1;
            memcpy(buffer, cursor, bytes);
            buffer[bytes] = '\0';
        }
        text(pixels, stride, x, baseline + line * line_height, buffer, size, color);
        cursor = *end == '\n' ? end + 1 : end;
        ++line;
    }
    return baseline + line * line_height;
}

static void draw_steps(uint32_t *pixels, uint32_t stride, PtcLabNroStage stage)
{
    static const char *const labels[] = {"1  准备实验后台", "2  完成所选取证", "3  恢复正常后台"};
    int active = stage == PTC_LAB_NRO_PREPARE || stage == PTC_LAB_NRO_REBOOT_TO_LAB ? 0 :
        (stage == PTC_LAB_NRO_RESTORE_NORMAL || stage == PTC_LAB_NRO_REBOOT_TO_NORMAL ? 2 : 1);
    int index;
    for (index = 0; index < 3; ++index) {
        PtcLabUiRect rect = {52 + index * 398, 112, 378, 48};
        round_rect(pixels, stride, rect, 9, index == active ? RGB(229, 241, 255) : RGB(255, 255, 255));
        outline(pixels, stride, rect, index == active ? 3 : 1,
            index == active ? RGB(28, 118, 188) : RGB(210, 217, 226));
        centered(pixels, stride, rect, labels[index], 18,
            index == active ? RGB(28, 94, 160) : RGB(92, 102, 116));
    }
}

static void draw_action(uint32_t *pixels, uint32_t stride, int index, const char *title,
    const char *subtitle, bool selected, bool disabled, bool danger)
{
    PtcLabUiRect rect = ptc_lab_nro_action_rect(index);
    uint32_t accent = danger ? RGB(190, 50, 57) : RGB(28, 118, 188);
    round_rect(pixels, stride, rect, 10, disabled ? RGB(235, 238, 242) : RGB(255, 255, 255));
    outline(pixels, stride, rect, selected ? 3 : 1, selected ? accent : RGB(207, 214, 224));
    text(pixels, stride, rect.x + 24, rect.y + 31, title, 21,
        disabled ? RGB(145, 151, 160) : (danger ? RGB(170, 38, 46) : RGB(35, 45, 59)));
    text(pixels, stride, rect.x + 24, rect.y + 56, subtitle, 15,
        disabled ? RGB(155, 160, 169) : RGB(95, 105, 119));
    if (selected && !disabled) centered(pixels, stride,
        (PtcLabUiRect){rect.x + rect.w - 56, rect.y + 17, 36, 36}, "A", 17, accent);
}

static void draw_modal(uint32_t *pixels, uint32_t stride, const PtcLabNroUi *ui)
{
    PtcLabUiRect box = {230, 145, 820, 430};
    fill(pixels, stride, (PtcLabUiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, RGB(56, 61, 70));
    round_rect(pixels, stride, box, 14, RGB(255, 255, 255));
    outline(pixels, stride, box, 2, RGB(190, 50, 57));
    if (ui->modal == PTC_LAB_NRO_MODAL_REPORT) {
        text(pixels, stride, 276, 205, "最新取证报告", 30, RGB(34, 43, 56));
        wrapped(pixels, stride, 276, 265,
            ui->report_available ? "请只发送下面这一份 JSON 文件。报告不包含标准包 PIN、密钥或离线码。" :
                "尚未找到报告。请先重启并在 Device Lab 浮窗中完成六个阶段。",
            20, 720, 32, 3, RGB(82, 93, 108));
        if (ui->report_available) {
            round_rect(pixels, stride, (PtcLabUiRect){276, 342, 728, 105}, 10, RGB(244, 247, 250));
            wrapped(pixels, stride, 296, 380, ui->report_path, 17, 688, 27, 3, RGB(28, 94, 160));
        }
        centered(pixels, stride, (PtcLabUiRect){430, 498, 420, 48}, "A / B  返回", 18, RGB(45, 57, 72));
        return;
    }
    if (ui->modal == PTC_LAB_NRO_MODAL_WORKING) {
        text(pixels, stride, 276, 215, "正在安全恢复", 30, RGB(34, 43, 56));
        wrapped(pixels, stride, 276, 282, ui->message[0] ? ui->message :
            "正在请求实验后台恢复并核对原始 PCTL 设置。得到精确证明前不会切换启动标志。",
            21, 720, 34, 4, RGB(70, 82, 98));
        round_rect(pixels, stride, (PtcLabUiRect){276, 448, 728, 18}, 8, RGB(224, 229, 236));
        fill(pixels, stride, (PtcLabUiRect){276, 448, 260, 18}, RGB(28, 118, 188));
        centered(pixels, stride, (PtcLabUiRect){360, 495, 560, 42}, "请保持本页面打开，B 可安全取消等待", 17, RGB(90, 101, 116));
        return;
    }
    text(pixels, stride, 276, 205, "确认切换启动后台", 30, RGB(164, 35, 43));
    wrapped(pixels, stride, 276, 266, ui->stage == PTC_LAB_NRO_PREPARE ?
        "这会暂时停用正常 PlayWise 后台，并启用隔离的 Device Lab 后台。完成后必须重启主机。" :
        "这会停用 Device Lab 后台，并按事务记录精确恢复正常 PlayWise 后台的原启动状态。",
        21, 720, 34, 4, RGB(66, 77, 91));
    text(pixels, stride, 276, 380, "请同时按住 ZL + ZR + A 一秒确认", 22, RGB(164, 35, 43));
    round_rect(pixels, stride, (PtcLabUiRect){276, 418, 728, 20}, 9, RGB(231, 234, 239));
    if (ui->confirm_progress > 0) fill(pixels, stride,
        (PtcLabUiRect){276, 418, 728 * ui->confirm_progress / 100, 20}, RGB(190, 50, 57));
    centered(pixels, stride, (PtcLabUiRect){360, 482, 560, 44}, "松开按键或按 B 取消", 18, RGB(91, 102, 116));
}

bool ptc_lab_nro_graphics_init(void)
{
    PlFontData font;
    Result result;
    memset(&g, 0, sizeof(g));
    result = plInitialize(PlServiceType_User);
    if (R_FAILED(result)) return false;
    g.pl_ready = true;
    result = plGetSharedFontByType(&font, PlSharedFontType_ChineseSimplified);
    if (R_FAILED(result) || FT_Init_FreeType(&g.library) != 0 ||
        FT_New_Memory_Face(g.library, (const FT_Byte *)font.address, (FT_Long)font.size, 0, &g.face) != 0) {
        ptc_lab_nro_graphics_exit();
        return false;
    }
    g.font_ready = true;
    result = framebufferCreate(&g.framebuffer, nwindowGetDefault(), SCREEN_WIDTH, SCREEN_HEIGHT,
        PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(result)) { ptc_lab_nro_graphics_exit(); return false; }
    g.framebuffer_ready = true;
    if (R_FAILED(framebufferMakeLinear(&g.framebuffer))) { ptc_lab_nro_graphics_exit(); return false; }
    return true;
}

void ptc_lab_nro_graphics_exit(void)
{
    if (g.framebuffer_ready) framebufferClose(&g.framebuffer);
    if (g.font_ready) FT_Done_Face(g.face);
    if (g.library) FT_Done_FreeType(g.library);
    if (g.pl_ready) plExit();
    memset(&g, 0, sizeof(g));
}

void ptc_lab_nro_graphics_draw(const PtcLabNroUi *ui)
{
    uint32_t stride_bytes = 0;
    uint32_t *pixels;
    uint32_t stride;
    const char *primary_subtitle;
    bool primary_danger;
    if (!ui || !g.framebuffer_ready) return;
    pixels = (uint32_t *)framebufferBegin(&g.framebuffer, &stride_bytes);
    if (!pixels) return;
    stride = stride_bytes / sizeof(uint32_t);
    fill(pixels, stride, (PtcLabUiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, RGB(244, 246, 249));
    fill(pixels, stride, (PtcLabUiRect){0, 0, SCREEN_WIDTH, 88}, RGB(255, 255, 255));
    fill(pixels, stride, (PtcLabUiRect){0, 86, SCREEN_WIDTH, 2}, RGB(220, 225, 232));
    round_rect(pixels, stride, (PtcLabUiRect){52, 23, 58, 40}, 8, RGB(187, 42, 51));
    centered(pixels, stride, (PtcLabUiRect){52, 23, 58, 40}, "LAB", 17, RGB(255, 255, 255));
    text(pixels, stride, 128, 50, "任我玩 Device Lab", 29, RGB(31, 40, 53));
    text(pixels, stride, 128, 73, "内部 PCTL 真机取证工具", 17, RGB(99, 108, 121));
    round_rect(pixels, stride, (PtcLabUiRect){1015, 23, 215, 40}, 8, RGB(255, 235, 238));
    centered(pixels, stride, (PtcLabUiRect){1015, 23, 215, 40}, "内部使用 / 危险操作", 17, RGB(166, 35, 45));
    draw_steps(pixels, stride, ui->stage);

    round_rect(pixels, stride, (PtcLabUiRect){52, 178, 800, 198}, 12, RGB(255, 255, 255));
    outline(pixels, stride, (PtcLabUiRect){52, 178, 800, 198}, 1,
        ui->message_is_error ? RGB(190, 50, 57) : RGB(210, 217, 226));
    text(pixels, stride, 78, 222, ptc_lab_nro_stage_title_zh(ui->stage), 29,
        ui->message_is_error ? RGB(166, 35, 45) : RGB(31, 42, 56));
    wrapped(pixels, stride, 78, 267, ptc_lab_nro_stage_body_zh(ui->stage), 19, 748, 31, 4, RGB(76, 88, 103));
    if (ui->message[0]) {
        round_rect(pixels, stride, (PtcLabUiRect){78, 324, 748, 36}, 7,
            ui->message_is_error ? RGB(255, 235, 238) : RGB(235, 248, 242));
        text(pixels, stride, 92, 348, ui->message, 16,
            ui->message_is_error ? RGB(164, 35, 44) : RGB(24, 117, 83));
    }

    round_rect(pixels, stride, (PtcLabUiRect){880, 178, 350, 462}, 12, RGB(255, 255, 255));
    outline(pixels, stride, (PtcLabUiRect){880, 178, 350, 462}, 1, RGB(210, 217, 226));
    text(pixels, stride, 906, 220, "当前状态", 23, RGB(36, 46, 59));
    text(pixels, stride, 906, 257, ui->session_status == PTC_LAB_SESSION_VALID ?
        ptc_lab_session_state_zh(ui->session.state) :
        (ui->session_status == PTC_LAB_SESSION_INVALID ? "会话文件损坏" : "没有进行中的会话"),
        19, ui->message_is_error ? RGB(169, 38, 47) : RGB(28, 94, 160));
    if (ui->session_status == PTC_LAB_SESSION_VALID) {
        char phase[96];
        snprintf(phase, sizeof(phase), "%s / 进度：%d / %d", ptc_lab_mode_zh(ui->session.mode),
            ui->session.next_phase, ui->session.required_phases);
        text(pixels, stride, 906, 292, phase, 17, RGB(86, 97, 112));
        wrapped(pixels, stride, 906, 330, ptc_lab_verdict_zh(ui->session.restore_verdict),
            16, 300, 27, 3, RGB(86, 97, 112));
    }
    if (ui->session_status == PTC_LAB_SESSION_VALID &&
        strcmp(ui->session.state, "awaiting_observation") == 0) {
        text(pixels, stride, 906, 414, "报告：待人工确认", 17, RGB(184, 112, 20));
    } else if (ui->report_available) {
        text(pixels, stride, 906, 414, "报告：完整", 17, RGB(25, 125, 89));
    } else if (ui->draft_available) {
        text(pixels, stride, 906, 414, "报告：草稿（不可交付）", 17, RGB(184, 112, 20));
    } else {
        text(pixels, stride, 906, 414, "报告：尚未生成", 17, RGB(110, 118, 130));
    }
    if (ui->details_visible && ui->technical[0]) {
        text(pixels, stride, 906, 460, "技术详情", 18, RGB(50, 61, 76));
        wrapped(pixels, stride, 906, 492, ui->technical, 13, 300, 22, 6, RGB(102, 111, 123));
    } else {
        wrapped(pixels, stride, 906, 478, "按 Y 展开结果码、事务阶段、请求 ID 和相关路径。",
            15, 300, 25, 4, RGB(110, 119, 132));
    }

    primary_danger = ui->stage == PTC_LAB_NRO_PREPARE || ui->stage == PTC_LAB_NRO_RESTORE_PCTL ||
        ui->stage == PTC_LAB_NRO_RESTORE_NORMAL || ui->stage == PTC_LAB_NRO_RECOVER_FLAGS;
    primary_subtitle = primary_danger ? "需要实体按键安全确认" : "按 A 或触摸执行";
    draw_action(pixels, stride, 0, ptc_lab_nro_stage_action_zh(ui->stage), primary_subtitle,
        ui->selected == 0, false, primary_danger);
    draw_action(pixels, stride, 1, "查看本轮完整报告",
        ui->report_available ? "显示当前 run 的文件名和完整 SD 路径" :
        (ui->draft_available ? "当前只有草稿，请返回浮窗完成观察" : "完成取证后才会生成报告"),
        ui->selected == 1, !ui->report_available, false);
    draw_action(pixels, stride, 2, "退出 Device Lab", "不执行其它更改并返回 HOME",
        ui->selected == 2, false, false);

    text(pixels, stride, 54, 696, "方向键 / 摇杆选择    A 确认    B 返回或退出    Y 技术详情    支持触摸", 16, RGB(88, 99, 113));
    text(pixels, stride, 1050, 696, "DEVICE LAB", 15, RGB(172, 42, 51));
    if (ui->modal != PTC_LAB_NRO_MODAL_NONE) draw_modal(pixels, stride, ui);
    framebufferEnd(&g.framebuffer);
}
