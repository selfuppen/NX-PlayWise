#ifndef PTC_COMPANION_OVERLAY_LAYOUT_H
#define PTC_COMPANION_OVERLAY_LAYOUT_H

#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "bridge.h"
#include "../policy_status.h"
#ifdef __cplusplus
}
#endif

typedef struct {
    int x;
    int y;
    int w;
    int h;
} PtcOverlayRect;

typedef enum {
    PTC_OVERLAY_PREVIEW_NEUTRAL = 0,
    PTC_OVERLAY_PREVIEW_WARNING = 1,
    PTC_OVERLAY_PREVIEW_DANGER = 2
} PtcOverlayPreviewVisualLevel;

enum {
    PTC_OVERLAY_CONTENT_X = 16,
    PTC_OVERLAY_CONTENT_Y = 40,
    PTC_OVERLAY_CONTENT_W = 416,
    PTC_OVERLAY_CONTENT_H = 607,
    PTC_OVERLAY_PARENT_CONTENT_Y = 40,
    PTC_OVERLAY_TOP_BANNER_Y = 0,
    PTC_OVERLAY_TOP_BANNER_H = 34,
    PTC_OVERLAY_REFRESH_X = 312,
    PTC_OVERLAY_REFRESH_Y = 0,
    PTC_OVERLAY_REFRESH_W = 96,
    PTC_OVERLAY_REFRESH_H = 34,
    PTC_OVERLAY_SLOT_Y = 90,
    PTC_OVERLAY_SLOT_W = 44,
    PTC_OVERLAY_SLOT_H = 54,
    PTC_OVERLAY_SLOT_GAP = 5,
    PTC_OVERLAY_KEYPAD_Y = 176,
    PTC_OVERLAY_KEYPAD_H = 224,
    PTC_OVERLAY_KEY_ROW_STEP = 54,
    PTC_OVERLAY_KEY_W = 124,
    PTC_OVERLAY_KEY_H = 48,
    PTC_OVERLAY_SUBMIT_Y = 408,
    PTC_OVERLAY_SUBMIT_H = 50,
    PTC_OVERLAY_STATUS_Y = 446,
    PTC_OVERLAY_STATUS_COLLAPSED_H = 40,
    PTC_OVERLAY_STATUS_NORMAL_H = 146,
    PTC_OVERLAY_STATUS_DETAIL_H = 146,

    /* Scheme A Overview constants */
    PTC_OVERLAY_HERO_CARD_Y = 40,
    PTC_OVERLAY_HERO_CARD_H = 136,
    PTC_OVERLAY_POLICY_GROUP_Y = 184,
    PTC_OVERLAY_POLICY_CARD_H = 54,
    PTC_OVERLAY_REDEEM_BANNER_Y = 304,
    PTC_OVERLAY_REDEEM_BANNER_H = 74,
    PTC_OVERLAY_ACTIONS_BAR_Y = 386,
    PTC_OVERLAY_ACTIONS_BAR_H = 52,

    /* Unified Typography Scale (掌机模式全局字体尺寸统一分级契约) */
    PTC_OVERLAY_FONT_HERO = 48,       /* 巨幕焦点数值 (剩余游玩时间、护眼倒计时) */
    PTC_OVERLAY_FONT_DISPLAY = 32,    /* 特大展示与卡片关键值 (加时码插槽字符、重点比对数值) */
    PTC_OVERLAY_FONT_KEYPAD = 30,     /* 软键盘按键与单字符 (3x4数字键、PIN方向键) */
    PTC_OVERLAY_FONT_TITLE_LG = 28,   /* 警示大标题与主结果 (场景感知受限标题、加时成功大标题) */
    PTC_OVERLAY_FONT_TITLE = 26,      /* 模块主标题与品牌标识 (PlayWise品牌名、家长区/PIN标题、加时确认标题) */
    PTC_OVERLAY_FONT_BTN = 22,        /* 关键操作大按钮 (50~56px高按钮：确认、取消、输入加时码) */
    PTC_OVERLAY_FONT_BODY_LG = 20,    /* 强力正文与卡片标签 (规则大卡片标签、引导卡片主标题、家长动作卡片标题) */
    PTC_OVERLAY_FONT_BODY = 18,       /* 舒适常规正文与导航 (规则状态详情、次级按钮、列表项正文) */
    PTC_OVERLAY_FONT_SECONDARY = 16,  /* 辅助说明与提示信息 (卡片副文本、状态栏摘要、警告原因) */
    PTC_OVERLAY_FONT_CAPTION = 15     /* 紧凑辅助标注 (绝对字号底线，防止中文笔画栅格化粘连) */
};

static inline PtcOverlayRect ptc_overlay_rect(int x, int y, int w, int h)
{
    PtcOverlayRect rect = {x, y, w, h};
    return rect;
}

static inline bool ptc_overlay_rect_contains(PtcOverlayRect rect, int x, int y)
{
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

/* Visual order differs from the stable action enum; all input uses this map. */
static inline int ptc_overlay_parent_action_slot(int action)
{
    static const int slots[PTC_OVERLAY_PARENT_ACTION_COUNT] = {0, 1, 2, 3, 4, 5, 7, 6};
    return action >= 0 && action < PTC_OVERLAY_PARENT_ACTION_COUNT ? slots[action] : -1;
}

static inline int ptc_overlay_parent_action_move(int action, int direction)
{
    static const int order[PTC_OVERLAY_PARENT_ACTION_COUNT] = {0, 1, 2, 3, 4, 5, 7, 6};
    int slot = ptc_overlay_parent_action_slot(action);
    if (slot < 0) return PTC_OVERLAY_PARENT_ADD_MINUTES;
    return order[(slot + (direction < 0 ? 7 : 1)) % PTC_OVERLAY_PARENT_ACTION_COUNT];
}

#define PTC_OVERLAY_PARENT_VIEWPORT_Y 72
#define PTC_OVERLAY_PARENT_VIEWPORT_H 484
#define PTC_OVERLAY_PARENT_SCROLL_MAX 118

static inline int ptc_overlay_parent_scroll_for_action(int action)
{
    int slot = ptc_overlay_parent_action_slot(action);
    if (slot <= 1) return 0;
    if (slot == 2) return 10;
    if (slot == 3) return 35;
    if (slot == 4) return 65;
    if (slot == 5) return 90;
    if (slot >= 6) return PTC_OVERLAY_PARENT_SCROLL_MAX;
    return 0;
}

static inline PtcOverlayRect ptc_overlay_parent_group_rect(int origin_x, int origin_y, int group)
{
    if (group == 0) return ptc_overlay_rect(origin_x + 8, origin_y + 76, PTC_OVERLAY_CONTENT_W - 20, 148);
    if (group == 1) return ptc_overlay_rect(origin_x + 8, origin_y + 238, PTC_OVERLAY_CONTENT_W - 20, 322);
    if (group == 2) return ptc_overlay_rect(origin_x + 8, origin_y + 574, PTC_OVERLAY_CONTENT_W - 20, 90);
    return ptc_overlay_rect(0, 0, 0, 0);
}

static inline PtcOverlayRect ptc_overlay_parent_action_rect(int origin_x, int origin_y, int action)
{
    int slot = ptc_overlay_parent_action_slot(action);
    if (slot < 0) return ptc_overlay_rect(0, 0, 0, 0);
    int y = slot < 2 ? 108 + slot * 58 : slot < 7 ? 270 + (slot - 2) * 58 : 606;
    return ptc_overlay_rect(origin_x + 16, origin_y + y, PTC_OVERLAY_CONTENT_W - 36, 50);
}

static inline PtcOverlayRect ptc_overlay_parent_refresh_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x + PTC_OVERLAY_CONTENT_W - 106, origin_y + 18, 96, 38);
}

static inline PtcOverlayRect ptc_overlay_parent_back_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x, origin_y + 562, PTC_OVERLAY_CONTENT_W, 36);
}

static inline bool ptc_overlay_remaining_refresh_pending(bool waiting, bool offline_code_request)
{
    return waiting && offline_code_request;
}

static inline PtcOverlayPreviewVisualLevel ptc_overlay_preview_visual_level(
    bool remaining_after_available,
    int remaining_after_minutes,
    bool capped,
    bool converts_unlimited_to_limited)
{
    if (!remaining_after_available || remaining_after_minutes == 0 || converts_unlimited_to_limited) {
        return PTC_OVERLAY_PREVIEW_DANGER;
    }
    if (capped) {
        return PTC_OVERLAY_PREVIEW_WARNING;
    }
    return PTC_OVERLAY_PREVIEW_NEUTRAL;
}

static inline PtcOverlayRect ptc_overlay_refresh_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x + PTC_OVERLAY_CONTENT_W - PTC_OVERLAY_REFRESH_W, origin_y,
                            PTC_OVERLAY_REFRESH_W, PTC_OVERLAY_REFRESH_H);
}

static inline PtcOverlayRect ptc_overlay_key_rect(int origin_x, int origin_y, unsigned int index)
{
    unsigned int row = index == 0 ? 3u : (index - 1u) / 3u;
    unsigned int col = index == 0 ? 1u : (index - 1u) % 3u;
    return ptc_overlay_rect(origin_x + 14 + (int)col * 132,
                            origin_y + PTC_OVERLAY_KEYPAD_Y + (int)row * PTC_OVERLAY_KEY_ROW_STEP,
                            PTC_OVERLAY_KEY_W, PTC_OVERLAY_KEY_H);
}

static inline PtcOverlayRect ptc_overlay_backspace_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x + 14,
                            origin_y + PTC_OVERLAY_KEYPAD_Y + 3 * PTC_OVERLAY_KEY_ROW_STEP,
                            PTC_OVERLAY_KEY_W, PTC_OVERLAY_KEY_H);
}

static inline PtcOverlayRect ptc_overlay_clear_rect(int origin_x, int origin_y)
{
    PtcOverlayRect rect = ptc_overlay_backspace_rect(origin_x, origin_y);
    rect.x = origin_x + 14 + 2 * 132;
    return rect;
}

static inline PtcOverlayRect ptc_overlay_submit_rect(int origin_x, int origin_y, int width)
{
    return ptc_overlay_rect(origin_x + 12, origin_y + PTC_OVERLAY_SUBMIT_Y, width - 24, PTC_OVERLAY_SUBMIT_H);
}

static inline PtcOverlayRect ptc_overlay_status_rect(
    int origin_x, int origin_y, int width, bool expanded, bool needs_detail)
{
    int height = !expanded ? PTC_OVERLAY_STATUS_COLLAPSED_H :
        (needs_detail ? PTC_OVERLAY_STATUS_DETAIL_H : PTC_OVERLAY_STATUS_NORMAL_H);
    return ptc_overlay_rect(origin_x, origin_y + PTC_OVERLAY_STATUS_Y, width, height);
}

static inline PtcOverlayRect ptc_overlay_child_policy_rect(int origin_x, int origin_y, int width)
{
    return ptc_overlay_rect(origin_x, origin_y + 510, width, 46);
}

static inline int ptc_overlay_policy_row_y(int origin_y, bool expanded, int row)
{
    return origin_y + (expanded ? PTC_OVERLAY_STATUS_Y + 98 : 528) + row * 20;
}

static inline PtcOverlayRect ptc_overlay_child_buffer_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x, origin_y + PTC_OVERLAY_ACTIONS_BAR_Y, 202, PTC_OVERLAY_ACTIONS_BAR_H);
}

static inline PtcOverlayRect ptc_overlay_child_parent_rect(int origin_x, int origin_y, int width)
{
    return ptc_overlay_rect(origin_x + 214, origin_y + PTC_OVERLAY_ACTIONS_BAR_Y, width - 214, PTC_OVERLAY_ACTIONS_BAR_H);
}

/* Scheme A Overview & Keypad Mode specific rect helpers */
static inline PtcOverlayRect ptc_overlay_overview_hero_rect(int origin_x, int origin_y, int width)
{
    return ptc_overlay_rect(origin_x, origin_y + PTC_OVERLAY_HERO_CARD_Y, width, PTC_OVERLAY_HERO_CARD_H);
}

static inline PtcOverlayRect ptc_overlay_overview_policy_rect(int origin_x, int origin_y, int width, int index)
{
    int y = origin_y + PTC_OVERLAY_POLICY_GROUP_Y + index * 60;
    return ptc_overlay_rect(origin_x, y, width, PTC_OVERLAY_POLICY_CARD_H);
}

static inline PtcOverlayRect ptc_overlay_overview_redeem_rect(int origin_x, int origin_y, int width)
{
    return ptc_overlay_rect(origin_x, origin_y + PTC_OVERLAY_REDEEM_BANNER_Y, width, PTC_OVERLAY_REDEEM_BANNER_H);
}

static inline PtcOverlayRect ptc_overlay_keypad_nav_back_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x + 12, origin_y + 466, 190, 46);
}

static inline PtcOverlayRect ptc_overlay_keypad_nav_parent_rect(int origin_x, int origin_y, int width)
{
    return ptc_overlay_rect(origin_x + 214, origin_y + 466, width - 226, 46);
}

#endif

