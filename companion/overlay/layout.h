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
    PTC_OVERLAY_CONTENT_Y = 12,
    PTC_OVERLAY_CONTENT_W = 416,
    PTC_OVERLAY_CONTENT_H = 635,
    PTC_OVERLAY_PARENT_CONTENT_Y = 12,
    /* Unified fixed footer rail: content bottom 639, viewport bottom 563,
       Tesla safe line 647. All non-child pages share this geometry. */
    PTC_OVERLAY_RAIL_H = 68,
    PTC_OVERLAY_RAIL_Y = PTC_OVERLAY_CONTENT_H - 8 - PTC_OVERLAY_RAIL_H,
    PTC_OVERLAY_RAIL_BOTTOM = PTC_OVERLAY_CONTENT_H - 8,
    PTC_OVERLAY_VIEWPORT_BOTTOM = PTC_OVERLAY_RAIL_Y - 8,
    /* Top path bar (方案 A): 22px breadcrumb replaces the old page title. */
    PTC_OVERLAY_HEADER_H = 44,
    PTC_OVERLAY_PATH_X = 16,
    PTC_OVERLAY_PATH_Y = 8,
    PTC_OVERLAY_PARENT_CARD_MIN_H = 96,
    PTC_OVERLAY_SLOT_Y = 104,
    PTC_OVERLAY_SLOT_W = 44,
    PTC_OVERLAY_SLOT_H = 64,
    PTC_OVERLAY_SLOT_GAP = 5,
    PTC_OVERLAY_KEYPAD_Y = 232,
    PTC_OVERLAY_KEYPAD_H = 248,
    PTC_OVERLAY_KEY_ROW_STEP = 64,
    PTC_OVERLAY_KEY_W = 124,
    PTC_OVERLAY_KEY_H = 56,
    PTC_OVERLAY_SUBMIT_Y = 491,
    PTC_OVERLAY_SUBMIT_H = 60,
    /* Unified Typography Scale (掌机模式全局字体尺寸统一分级契约) */
    PTC_OVERLAY_FONT_HERO = 56,
    PTC_OVERLAY_FONT_DISPLAY = 44,
    PTC_OVERLAY_FONT_CODE = 40,
    PTC_OVERLAY_FONT_KEYPAD = 36,
    PTC_OVERLAY_FONT_TITLE_LG = 44,
    PTC_OVERLAY_FONT_TITLE = 34,
    PTC_OVERLAY_FONT_BTN = 30,
    PTC_OVERLAY_FONT_BODY_LG = 32,
    PTC_OVERLAY_FONT_BODY = 28,
    PTC_OVERLAY_FONT_SECONDARY = 24,
    PTC_OVERLAY_FONT_CAPTION = 22
};

/* Measured once by the shared renderer; input consumes these exact rectangles. */
typedef struct {
    PtcOverlayRect viewport;
    PtcOverlayRect primary;
    PtcOverlayRect secondary;
    PtcOverlayRect refresh;
    PtcOverlayRect redeem;
    PtcOverlayRect status;
    PtcOverlayRect buffer;
    PtcOverlayRect parent;
    PtcOverlayRect backspace;
    PtcOverlayRect actions[PTC_OVERLAY_PARENT_ACTION_COUNT];
    PtcOverlayRect pin_keys[16];
    int scroll_max;
} PtcOverlayPageLayout;

static inline int ptc_overlay_scroll_max(int content_height, int viewport_height)
{
    return content_height > viewport_height ? content_height - viewport_height : 0;
}

static inline int ptc_overlay_scroll_to_rect(PtcOverlayRect viewport, PtcOverlayRect item,
                                            int offset, int maximum)
{
    if (item.y < viewport.y + offset) offset = item.y - viewport.y;
    if (item.y + item.h > viewport.y + viewport.h + offset)
        offset = item.y + item.h - viewport.y - viewport.h;
    return offset < 0 ? 0 : offset > maximum ? maximum : offset;
}

static inline bool ptc_overlay_scrolled_hit(PtcOverlayRect viewport, PtcOverlayRect item,
                                           int offset, int x, int y)
{
    return x >= viewport.x && x < viewport.x + viewport.w &&
        y >= viewport.y && y < viewport.y + viewport.h &&
        x >= item.x && x < item.x + item.w &&
        y + offset >= item.y && y + offset < item.y + item.h;
}

typedef struct {
    bool active;
    bool dragged;
    bool scrollable;
    int page;
    int start_x;
    int start_y;
    int last_y;
} PtcOverlayGesture;

typedef struct {
    bool clicked;
    int x;
    int y;
    int scroll_delta;
} PtcOverlayGestureEvent;

static inline PtcOverlayGestureEvent ptc_overlay_gesture_update(PtcOverlayGesture *gesture,
    bool down, int x, int y, int page, PtcOverlayRect viewport)
{
    PtcOverlayGestureEvent event = {false, 0, 0, 0};
    if (down && !gesture->active) {
        gesture->active = true;
        gesture->dragged = false;
        gesture->page = page;
        gesture->start_x = x;
        gesture->start_y = gesture->last_y = y;
        gesture->scrollable = x >= viewport.x && x < viewport.x + viewport.w &&
            y >= viewport.y && y < viewport.y + viewport.h;
    } else if (down) {
        int dx = x - gesture->start_x;
        int dy = y - gesture->start_y;
        if (dx > 10 || dx < -10 || dy > 10 || dy < -10 || page != gesture->page)
            gesture->dragged = true;
        if (gesture->dragged && gesture->scrollable && page == gesture->page)
            event.scroll_delta = gesture->last_y - y;
        gesture->last_y = y;
    } else if (gesture->active) {
        if (!gesture->dragged && page == gesture->page) {
            event.clicked = true;
            event.x = gesture->start_x;
            event.y = gesture->start_y;
        }
        gesture->active = false;
    }
    return event;
}

static inline PtcOverlayRect ptc_overlay_rect(int x, int y, int w, int h)
{
    PtcOverlayRect rect = {x, y, w, h};
    return rect;
}

static inline bool ptc_overlay_rect_contains(PtcOverlayRect rect, int x, int y)
{
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

/* Visual order differs from the stable action enum; all input uses this map.
   Slots group the actions by what a parent is trying to do today:
   0..1 adjust today's allowance, 2..6 relax a rule that is restricting now,
   7 is the destructive system recovery that must stay apart. */
static inline const int *ptc_overlay_parent_action_order(void)
{
    static const int order[PTC_OVERLAY_PARENT_ACTION_COUNT] = {
        PTC_OVERLAY_PARENT_ADD_MINUTES,
        PTC_OVERLAY_PARENT_UNLIMITED,
        PTC_OVERLAY_PARENT_SKIP_BEDTIME,
        PTC_OVERLAY_PARENT_SKIP_EYE_CARE,
        PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP,
        PTC_OVERLAY_PARENT_DISABLE_BEDTIME,
        PTC_OVERLAY_PARENT_WAIVE_DOCK,
        PTC_OVERLAY_PARENT_RESTORE_SNAPSHOT
    };
    return order;
}

static inline int ptc_overlay_parent_action_group(int slot)
{
    if (slot < 0 || slot >= PTC_OVERLAY_PARENT_ACTION_COUNT) return -1;
    if (slot < 2) return 0;
    return slot < 7 ? 1 : 2;
}

static inline int ptc_overlay_parent_action_slot(int action)
{
    const int *order = ptc_overlay_parent_action_order();
    for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot)
        if (order[slot] == action) return slot;
    return -1;
}

static inline int ptc_overlay_parent_action_move(int action, int direction)
{
    const int *order = ptc_overlay_parent_action_order();
    int slot = ptc_overlay_parent_action_slot(action);
    if (slot < 0) return order[0];
    return order[(slot + (direction < 0 ? PTC_OVERLAY_PARENT_ACTION_COUNT - 1 : 1)) %
        PTC_OVERLAY_PARENT_ACTION_COUNT];
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

static inline PtcOverlayRect ptc_overlay_keypad_nav_back_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x + 12, origin_y + PTC_OVERLAY_RAIL_Y, 190, PTC_OVERLAY_RAIL_H);
}

static inline PtcOverlayRect ptc_overlay_keypad_nav_parent_rect(int origin_x, int origin_y, int width)
{
    return ptc_overlay_rect(origin_x + 214, origin_y + PTC_OVERLAY_RAIL_Y,
                            width - 226, PTC_OVERLAY_RAIL_H);
}

static inline PtcOverlayRect ptc_overlay_pin_wheel_rect(int origin_x, int origin_y, unsigned int index)
{
    static const int cols[8] = {0, 1, 2, 0, 2, 0, 1, 2};
    static const int rows[8] = {0, 0, 0, 1, 1, 2, 2, 2};
    if (index >= 8) return ptc_overlay_rect(0, 0, 0, 0);
    return ptc_overlay_rect(origin_x + 14 + cols[index] * 132,
                            origin_y + rows[index] * 74,
                            124, 66);
}

static inline PtcOverlayRect ptc_overlay_pin_center_0_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x + 146, origin_y + 74, 58, 66);
}

static inline PtcOverlayRect ptc_overlay_pin_center_9_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x + 212, origin_y + 74, 58, 66);
}

static inline PtcOverlayRect ptc_overlay_pin_numpad_key_rect(int origin_x, int origin_y, unsigned int digit)
{
    unsigned int row = digit == 0 ? 3u : (digit - 1u) / 3u;
    unsigned int col = digit == 0 ? 1u : (digit - 1u) % 3u;
    return ptc_overlay_rect(origin_x + 14 + (int)col * 132,
                            origin_y + (int)row * 66,
                            124, 58);
}

static inline PtcOverlayRect ptc_overlay_pin_numpad_clear_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x + 278, origin_y + 3 * 66, 124, 58);
}

#endif

