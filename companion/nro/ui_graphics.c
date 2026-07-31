#include "ui_graphics.h"

#include <switch.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static const UiAction TODAY_ACTIONS[] = {
    {"刷新状态", "读取今天的最新游玩状态", COLOR(42, 105, 188)},
    {"设置今日额度", "指定今天可玩的分钟数", COLOR(42, 105, 188)},
    {"临时加时", "在今天额度上增加分钟", COLOR(25, 132, 95)},
    {"今日不限", "暂时取消今天的时间额度", COLOR(25, 132, 95)},
    {"今日禁玩", "立即设置今天不可游玩", COLOR(194, 61, 61)},
    {"恢复周计划", "清除今天的临时调整", COLOR(91, 100, 116)},
};

static const UiAction PLAN_ACTIONS[] = {
    {"每周计划", "分别设置周日至周六", COLOR(42, 105, 188)},
    {"就寝时间", "设置夜间不可游玩的时段", COLOR(48, 92, 151)},
    {"限制方式", "提醒、阻止或暂停软件", COLOR(91, 100, 116)},
    {"临时解锁", "短时间暂停本地规则", COLOR(25, 132, 95)},
    {"结束解锁", "立即恢复本地规则", COLOR(194, 61, 61)},
};

static const UiAction SAFETY_ACTIONS[] = {
    {"快速设备测试", "验证写入、计时与自动恢复", COLOR(42, 105, 188)},
    {"紧急停用控制", "创建 disable.flag 进入安全状态", COLOR(194, 61, 61)},
    {"恢复控制", "移除 disable.flag 并恢复处理", COLOR(25, 132, 95)},
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

static void draw_key_hint(uint32_t *pixels, uint32_t stride, int x, int width, const char *key, const char *label)
{
    UiRect key_rect = {x, 671, width, 32};
    fill_round_rect(pixels, stride, key_rect, 8, COLOR(235, 238, 243));
    draw_text_center(pixels, stride, key_rect, key, 17, COLOR(39, 45, 55));
    draw_text(pixels, stride, x + width + 9, 695, label, 19, COLOR(77, 86, 99));
}

static void describe_status(const PtcUiModel *model, char *today, size_t today_size, char *remaining, size_t remaining_size)
{
    if (!model->status_loaded) {
        snprintf(today, today_size, "等待刷新");
        snprintf(remaining, remaining_size, "--");
        return;
    }
    if (model->blocked_today == 1) {
        snprintf(today, today_size, "今日禁玩");
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
    UiRect rect = {54, y, 1172, 78};
    uint32_t accent = COLOR(91, 100, 116);
    char fitted[190];
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
    draw_text(pixels, stride, rect.x + 24, rect.y + 29, model->waiting ? "正在处理" : "最近反馈", 18, accent);
    fit_text(fitted, sizeof(fitted), model->message, 21, rect.width - 48);
    draw_text(pixels, stride, rect.x + 24, rect.y + 59, fitted, 21, COLOR(45, 52, 62));
}

static void draw_child(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char today[32];
    char remaining[32];
    const char *mode = model->mode[0] ? model->mode : "--";
    describe_status(model, today, sizeof(today), remaining, sizeof(remaining));
    draw_header(pixels, stride, "游玩时间", "查看今天的状态，使用家长提供的加时码");
    draw_status_tile(pixels, stride, (UiRect){54, 118, 362, 92}, "今日状态", today, COLOR(216, 49, 54));
    draw_status_tile(pixels, stride, (UiRect){438, 118, 362, 92}, "剩余时间", remaining, COLOR(25, 132, 95));
    draw_status_tile(pixels, stride, (UiRect){822, 118, 404, 92}, "控制模式", mode, COLOR(28, 118, 188));

    fill_round_rect(pixels, stride, (UiRect){54, 238, 760, 246}, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, (UiRect){54, 238, 760, 246}, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, 86, 286, "今天还想再玩一会儿？", 27, COLOR(28, 34, 43));
    draw_text(pixels, stride, 86, 322, "输入 16 位离线加时码，后台确认后会更新今日时间。", 21, COLOR(85, 94, 107));
    fill_round_rect(pixels, stride, (UiRect){86, 354, 696, 92}, 8, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, (UiRect){86, 354, 696, 92}, "A  输入加时码", 31, COLOR(255, 255, 255));

    fill_round_rect(pixels, stride, (UiRect){836, 238, 390, 246}, 8, COLOR(250, 251, 253));
    draw_rect_outline(pixels, stride, (UiRect){836, 238, 390, 246}, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, 866, 282, "状态详情", 24, COLOR(28, 34, 43));
    draw_text(pixels, stride, 866, 324, model->play_timer_enabled == 1 ? "游玩计时器：已开启" : "游玩计时器：未确认", 20, COLOR(77, 86, 99));
    draw_text(pixels, stride, 866, 362, model->bedtime_active ? "就寝限制：当前生效" : "就寝限制：未生效", 20, COLOR(77, 86, 99));
    draw_text(pixels, stride, 866, 400, model->parent_unlock_active ? "临时解锁：已开启" : "临时解锁：未开启", 20, COLOR(77, 86, 99));
    draw_text(pixels, stride, 866, 448, "Y  刷新状态", 20, COLOR(28, 118, 188));

    draw_notice(pixels, stride, model, 510);
    draw_key_hint(pixels, stride, 54, 38, "A", "输入加时码");
    draw_key_hint(pixels, stride, 258, 38, "Y", "刷新");
    draw_key_hint(pixels, stride, 410, 94, "L+R+X", "家长区");
    draw_key_hint(pixels, stride, 676, 38, "B", "退出");
    draw_key_hint(pixels, stride, 818, 38, "+", "退出");
}

static const UiAction *actions_for_page(PtcUiParentPage page, int *count)
{
    if (page == PTC_UI_PARENT_PLAN) {
        *count = (int)(sizeof(PLAN_ACTIONS) / sizeof(PLAN_ACTIONS[0]));
        return PLAN_ACTIONS;
    }
    if (page == PTC_UI_PARENT_SAFETY) {
        *count = (int)(sizeof(SAFETY_ACTIONS) / sizeof(SAFETY_ACTIONS[0]));
        return SAFETY_ACTIONS;
    }
    *count = (int)(sizeof(TODAY_ACTIONS) / sizeof(TODAY_ACTIONS[0]));
    return TODAY_ACTIONS;
}

static void draw_tabs(uint32_t *pixels, uint32_t stride, PtcUiParentPage active)
{
    static const char *LABELS[] = {"今日管理", "时间计划", "安全工具"};
    int index;
    for (index = 0; index < PTC_UI_PARENT_PAGE_COUNT; ++index) {
        UiRect tab = {54 + index * 214, 108, 194, 48};
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
    bool selected)
{
    uint32_t background = selected ? COLOR(244, 249, 255) : COLOR(255, 255, 255);
    uint32_t border = selected ? action->accent : COLOR(219, 225, 233);
    fill_round_rect(pixels, stride, rect, 8, background);
    draw_rect_outline(pixels, stride, rect, selected ? 3 : 1, border);
    fill_round_rect(pixels, stride, (UiRect){rect.x + 20, rect.y + 25, 12, rect.height - 50}, 6, action->accent);
    draw_text(pixels, stride, rect.x + 54, rect.y + 46, action->title, 24, COLOR(28, 34, 43));
    draw_text(pixels, stride, rect.x + 54, rect.y + 78, action->subtitle, 18, COLOR(91, 100, 114));
    if (selected) {
        draw_text(pixels, stride, rect.x + rect.width - 74, rect.y + 64, "A", 23, action->accent);
    }
}

static void draw_capabilities(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 324};
    fill_round_rect(pixels, stride, panel, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, panel, 1, COLOR(219, 225, 233));
    draw_text(pixels, stride, panel.x + 26, panel.y + 43, "设备能力", 23, COLOR(28, 34, 43));
    draw_text(pixels, stride, panel.x + 26, panel.y + 88, model->play_timer_write_verified ? "写入验证      已通过" : "写入验证      未通过", 19,
              model->play_timer_write_verified ? COLOR(25, 132, 95) : COLOR(194, 61, 61));
    draw_text(pixels, stride, panel.x + 26, panel.y + 130, model->play_timer_effect_verified ? "计时效果      已通过" : "计时效果      未通过", 19,
              model->play_timer_effect_verified ? COLOR(25, 132, 95) : COLOR(194, 61, 61));
    draw_text(pixels, stride, panel.x + 26, panel.y + 172, model->raw_block_verified ? "强制阻止      已验证" : "强制阻止      未验证", 19,
              model->raw_block_verified ? COLOR(25, 132, 95) : COLOR(91, 100, 116));
    draw_text(pixels, stride, panel.x + 26, panel.y + 214, model->suspend_verified ? "暂停软件      已验证" : "暂停软件      未验证", 19,
              model->suspend_verified ? COLOR(25, 132, 95) : COLOR(91, 100, 116));
    draw_text(pixels, stride, panel.x + 26, panel.y + 270, "高风险操作会再次要求确认", 18, COLOR(194, 61, 61));
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
        int column = index % 2;
        int row = index / 2;
        UiRect card = {54 + column * 385, 176 + row * 110, 365, 94};
        draw_action_card(pixels, stride, card, &actions[index], index == model->selected_index);
    }
    draw_capabilities(pixels, stride, model);
    draw_notice(pixels, stride, model, 522);
    draw_key_hint(pixels, stride, 54, 38, "A", "执行");
    draw_key_hint(pixels, stride, 180, 78, "方向键", "选择");
    draw_key_hint(pixels, stride, 346, 52, "L/R", "切页");
    draw_key_hint(pixels, stride, 516, 38, "Y", "刷新结果");
    draw_key_hint(pixels, stride, 724, 38, "B", "返回孩子页");
}

static const char *rule_mode_label(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_UNLIMITED:
        return "不限时";
    case PTC_RULE_MODE_BLOCKED:
        return "禁玩";
    case PTC_RULE_MODE_LIMIT:
    default:
        return "限时";
    }
}

static const char *limit_action_label(PtcLimitAction action)
{
    switch (action) {
    case PTC_LIMIT_ACTION_RAW_BLOCK:
        return "强制阻止";
    case PTC_LIMIT_ACTION_SUSPEND:
        return "暂停软件";
    case PTC_LIMIT_ACTION_REMIND:
    default:
        return "仅提醒";
    }
}

static void format_clock(char *out, size_t out_size, uint16_t minute_of_day)
{
    snprintf(
        out,
        out_size,
        "%02u:%02u",
        (unsigned int)(minute_of_day / 60),
        (unsigned int)(minute_of_day % 60));
}

static void draw_dialog_shell(
    uint32_t *pixels,
    uint32_t stride,
    const PtcUiModel *model,
    UiRect *dialog,
    int width,
    int height)
{
    char body[190];
    *dialog = (UiRect){(SCREEN_WIDTH - width) / 2, (SCREEN_HEIGHT - height) / 2 - 10, width, height};
    fill_rect(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, COLOR(226, 230, 236));
    fill_round_rect(pixels, stride, *dialog, 8, COLOR(255, 255, 255));
    draw_rect_outline(pixels, stride, *dialog, 2, COLOR(203, 211, 222));
    draw_text(pixels, stride, dialog->x + 34, dialog->y + 54, model->overlay_title, 29, COLOR(28, 34, 43));
    if (model->overlay_body[0]) {
        fit_text(body, sizeof(body), model->overlay_body, 20, dialog->width - 68);
        draw_text(pixels, stride, dialog->x + 34, dialog->y + 88, body, 20, COLOR(91, 100, 114));
    }
}

static void draw_minutes_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char value[32];
    draw_dialog_shell(pixels, stride, model, &dialog, 720, 360);
    snprintf(value, sizeof(value), "%u 分钟", (unsigned int)model->draft_minutes);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 170, dialog.y + 126, 380, 104}, 8, COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, (UiRect){dialog.x + 170, dialog.y + 126, 380, 104}, 2, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 170, dialog.y + 126, 380, 104}, value, 37, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 244, 580, 34}, "↑↓ 调整 5 分钟     ←→ 调整 15 分钟", 20, COLOR(77, 86, 99));
    draw_key_hint(pixels, stride, 410, 38, "A", "确认");
    draw_key_hint(pixels, stride, 586, 38, "B", "取消");
}

static void draw_weekly_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    UiRect dialog;
    int day;
    draw_dialog_shell(pixels, stride, model, &dialog, 1172, 470);
    for (day = 0; day < 7; ++day) {
        UiRect card = {dialog.x + 26 + day * 160, dialog.y + 122, 142, 190};
        uint32_t border = day == model->editor_index ? COLOR(28, 118, 188) : COLOR(219, 225, 233);
        char minutes[32];
        fill_round_rect(pixels, stride, card, 8, day == model->editor_index ? COLOR(244, 249, 255) : COLOR(250, 251, 253));
        draw_rect_outline(pixels, stride, card, day == model->editor_index ? 3 : 1, border);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 14, card.width, 34}, DAYS[day], 21, COLOR(28, 34, 43));
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 69, card.width, 34}, rule_mode_label(model->draft_week[day].mode), 22,
                         model->draft_week[day].mode == PTC_RULE_MODE_BLOCKED ? COLOR(194, 61, 61) : COLOR(28, 118, 188));
        if (model->draft_week[day].mode == PTC_RULE_MODE_LIMIT) {
            snprintf(minutes, sizeof(minutes), "%u 分钟", (unsigned int)model->draft_week[day].minutes);
        } else {
            snprintf(minutes, sizeof(minutes), "--");
        }
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 119, card.width, 34}, minutes, 19, COLOR(77, 86, 99));
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 80, dialog.y + 334, dialog.width - 160, 34},
                     "←→ 选择日期     X 切换模式     ↑↓ 调整 15 分钟", 20, COLOR(77, 86, 99));
    draw_key_hint(pixels, stride, 410, 38, "A", "保存计划");
    draw_key_hint(pixels, stride, 626, 38, "B", "取消");
}

static void draw_bedtime_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char start[16];
    char end[16];
    UiRect enabled = {0};
    UiRect start_rect = {0};
    UiRect end_rect = {0};
    draw_dialog_shell(pixels, stride, model, &dialog, 820, 410);
    format_clock(start, sizeof(start), model->draft_bedtime.start_min);
    format_clock(end, sizeof(end), model->draft_bedtime.end_min);
    enabled = (UiRect){dialog.x + 46, dialog.y + 124, 210, 92};
    start_rect = (UiRect){dialog.x + 282, dialog.y + 124, 210, 92};
    end_rect = (UiRect){dialog.x + 518, dialog.y + 124, 210, 92};
    fill_round_rect(pixels, stride, enabled, 8, model->draft_bedtime.enabled ? COLOR(230, 247, 239) : COLOR(244, 246, 249));
    draw_rect_outline(pixels, stride, enabled, model->editor_index == 0 ? 3 : 1,
                      model->editor_index == 0 ? COLOR(25, 132, 95) : COLOR(219, 225, 233));
    draw_text_center(pixels, stride, (UiRect){enabled.x, enabled.y + 5, enabled.width, 38}, "状态", 18, COLOR(91, 100, 114));
    draw_text_center(pixels, stride, (UiRect){enabled.x, enabled.y + 39, enabled.width, 40}, model->draft_bedtime.enabled ? "已启用" : "未启用", 25,
                     model->draft_bedtime.enabled ? COLOR(25, 132, 95) : COLOR(91, 100, 114));
    fill_round_rect(pixels, stride, start_rect, 8, COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, start_rect, model->editor_index == 1 ? 3 : 1,
                      model->editor_index == 1 ? COLOR(28, 118, 188) : COLOR(219, 225, 233));
    draw_text_center(pixels, stride, (UiRect){start_rect.x, start_rect.y + 5, start_rect.width, 38}, "开始", 18, COLOR(91, 100, 114));
    draw_text_center(pixels, stride, (UiRect){start_rect.x, start_rect.y + 39, start_rect.width, 40}, start, 28, COLOR(28, 118, 188));
    fill_round_rect(pixels, stride, end_rect, 8, COLOR(244, 249, 255));
    draw_rect_outline(pixels, stride, end_rect, model->editor_index == 2 ? 3 : 1,
                      model->editor_index == 2 ? COLOR(28, 118, 188) : COLOR(219, 225, 233));
    draw_text_center(pixels, stride, (UiRect){end_rect.x, end_rect.y + 5, end_rect.width, 38}, "结束", 18, COLOR(91, 100, 114));
    draw_text_center(pixels, stride, (UiRect){end_rect.x, end_rect.y + 39, end_rect.width, 40}, end, 28, COLOR(28, 118, 188));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 80, dialog.y + 244, dialog.width - 160, 34},
                     "←→ 选择项目     X 切换启用     ↑↓ 调整 15 分钟", 20, COLOR(77, 86, 99));
    draw_key_hint(pixels, stride, 410, 38, "A", "保存设置");
    draw_key_hint(pixels, stride, 626, 38, "B", "取消");
}

static void draw_limit_action_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const PtcLimitAction ACTIONS[] = {
        PTC_LIMIT_ACTION_REMIND,
        PTC_LIMIT_ACTION_RAW_BLOCK,
        PTC_LIMIT_ACTION_SUSPEND,
    };
    UiRect dialog;
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 850, 360);
    for (index = 0; index < 3; ++index) {
        UiRect option = {dialog.x + 46 + index * 254, dialog.y + 132, 228, 94};
        bool selected = ACTIONS[index] == model->draft_limit_action;
        fill_round_rect(pixels, stride, option, 8, selected ? COLOR(244, 249, 255) : COLOR(250, 251, 253));
        draw_rect_outline(pixels, stride, option, selected ? 3 : 1,
                          selected ? COLOR(28, 118, 188) : COLOR(219, 225, 233));
        draw_text_center(pixels, stride, option, limit_action_label(ACTIONS[index]), 23,
                         selected ? COLOR(28, 118, 188) : COLOR(77, 86, 99));
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 80, dialog.y + 244, dialog.width - 160, 34}, "←→ 选择限制方式", 20, COLOR(77, 86, 99));
    draw_key_hint(pixels, stride, 410, 38, "A", "保存设置");
    draw_key_hint(pixels, stride, 626, 38, "B", "取消");
}

static void draw_confirm_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    bool danger = model->operation == PTC_UI_OPERATION_BLOCK_TODAY ||
                  model->operation == PTC_UI_OPERATION_EMERGENCY_DISABLE ||
                  model->operation == PTC_UI_OPERATION_QUICK_TEST;
    draw_dialog_shell(pixels, stride, model, &dialog, 760, 330);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 130, 620, 74}, 8,
                    danger ? COLOR(255, 240, 240) : COLOR(240, 248, 244));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 70, dialog.y + 130, 620, 74},
                     danger ? "请确认已了解这项操作的影响" : "确认执行这项操作", 22,
                     danger ? COLOR(194, 61, 61) : COLOR(25, 132, 95));
    draw_key_hint(pixels, stride, 410, 38, "A", "确认执行");
    draw_key_hint(pixels, stride, 636, 38, "B", "取消");
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
    case PTC_UI_OVERLAY_BEDTIME:
        draw_bedtime_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_LIMIT_ACTION:
        draw_limit_action_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_CONFIRM:
        draw_confirm_overlay(pixels, stride, model);
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
    } else {
        draw_child(pixels, stride, model);
    }
    if (model->overlay != PTC_UI_OVERLAY_NONE) {
        draw_overlay(pixels, stride, model);
    }
    framebufferEnd(&g_ui.framebuffer);
}
