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
    PTC_OVERLAY_SLOT_Y = 108,
    PTC_OVERLAY_SLOT_W = 44,
    PTC_OVERLAY_SLOT_H = 64,
    PTC_OVERLAY_SLOT_GAP = 5,
    PTC_OVERLAY_KEYPAD_Y = 236,
    PTC_OVERLAY_KEYPAD_H = 248,
    PTC_OVERLAY_KEY_ROW_STEP = 64,
    PTC_OVERLAY_KEY_W = 124,
    PTC_OVERLAY_KEY_H = 56,
    PTC_OVERLAY_SUBMIT_Y = 492,
    PTC_OVERLAY_SUBMIT_H = 50,
    /* Unified Typography Scale (掌机模式全局字体尺寸统一分级契约) */
    PTC_OVERLAY_FONT_HERO = 56,
    PTC_OVERLAY_FONT_DISPLAY = 44,
    PTC_OVERLAY_FONT_CODE = 40,
    PTC_OVERLAY_FONT_KEYPAD = 36,
    PTC_OVERLAY_FONT_TITLE_LG = 44,
    PTC_OVERLAY_FONT_TITLE = 40,
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
    return ptc_overlay_rect(origin_x + 12, origin_y + 550, 190, 52);
}

static inline PtcOverlayRect ptc_overlay_keypad_nav_parent_rect(int origin_x, int origin_y, int width)
{
    return ptc_overlay_rect(origin_x + 214, origin_y + 550, width - 226, 52);
}

#endif

