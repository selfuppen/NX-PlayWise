#ifndef PTC_COMPANION_OVERLAY_LAYOUT_H
#define PTC_COMPANION_OVERLAY_LAYOUT_H

#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "bridge.h"
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
    PTC_OVERLAY_CONTENT_X = 35,
    PTC_OVERLAY_CONTENT_Y = 90,
    PTC_OVERLAY_CONTENT_W = 363,
    PTC_OVERLAY_CONTENT_H = 557,
    PTC_OVERLAY_TOP_BANNER_Y = 0,
    PTC_OVERLAY_TOP_BANNER_H = 72,
    PTC_OVERLAY_REFRESH_X = 220,
    PTC_OVERLAY_REFRESH_Y = 6,
    PTC_OVERLAY_REFRESH_W = 88,
    PTC_OVERLAY_REFRESH_H = 36,
    PTC_OVERLAY_SLOT_Y = 104,
    PTC_OVERLAY_SLOT_W = 40,
    PTC_OVERLAY_SLOT_H = 48,
    PTC_OVERLAY_SLOT_GAP = 4,
    PTC_OVERLAY_KEYPAD_Y = 192,
    PTC_OVERLAY_KEYPAD_H = 180,
    PTC_OVERLAY_KEY_ROW_STEP = 44,
    PTC_OVERLAY_KEY_W = 105,
    PTC_OVERLAY_KEY_H = 44,
    PTC_OVERLAY_SUBMIT_Y = 378,
    PTC_OVERLAY_SUBMIT_H = 36,
    PTC_OVERLAY_STATUS_Y = 420,
    PTC_OVERLAY_STATUS_COLLAPSED_H = 32,
    PTC_OVERLAY_STATUS_NORMAL_H = 92,
    PTC_OVERLAY_STATUS_DETAIL_H = 136
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

static inline PtcOverlayRect ptc_overlay_parent_group_rect(int origin_x, int origin_y, int group)
{
    if (group == 0) return ptc_overlay_rect(origin_x + 8, origin_y + 110, PTC_OVERLAY_CONTENT_W - 16, 106);
    if (group == 1) return ptc_overlay_rect(origin_x + 8, origin_y + 224, PTC_OVERLAY_CONTENT_W - 16, 220);
    if (group == 2) return ptc_overlay_rect(origin_x + 8, origin_y + 452, PTC_OVERLAY_CONTENT_W - 16, 68);
    return ptc_overlay_rect(0, 0, 0, 0);
}

static inline PtcOverlayRect ptc_overlay_parent_action_rect(int origin_x, int origin_y, int action)
{
    int slot = ptc_overlay_parent_action_slot(action);
    if (slot < 0) return ptc_overlay_rect(0, 0, 0, 0);
    int y = slot < 2 ? 138 + slot * 38 : slot < 7 ? 252 + (slot - 2) * 38 : 480;
    return ptc_overlay_rect(origin_x + 16, origin_y + y, PTC_OVERLAY_CONTENT_W - 32, 36);
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
    return ptc_overlay_rect(origin_x + PTC_OVERLAY_REFRESH_X, origin_y + PTC_OVERLAY_REFRESH_Y,
                            PTC_OVERLAY_REFRESH_W, PTC_OVERLAY_REFRESH_H);
}

static inline PtcOverlayRect ptc_overlay_key_rect(int origin_x, int origin_y, unsigned int index)
{
    unsigned int row = index == 0 ? 3u : (index - 1u) / 3u;
    unsigned int col = index == 0 ? 1u : (index - 1u) % 3u;
    return ptc_overlay_rect(origin_x + 12 + (int)col * 117,
                            origin_y + PTC_OVERLAY_KEYPAD_Y + 4 + (int)row * PTC_OVERLAY_KEY_ROW_STEP,
                            PTC_OVERLAY_KEY_W, PTC_OVERLAY_KEY_H);
}

static inline PtcOverlayRect ptc_overlay_backspace_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x + 12,
                            origin_y + PTC_OVERLAY_KEYPAD_Y + 4 + 3 * PTC_OVERLAY_KEY_ROW_STEP,
                            PTC_OVERLAY_KEY_W, PTC_OVERLAY_KEY_H);
}

static inline PtcOverlayRect ptc_overlay_clear_rect(int origin_x, int origin_y)
{
    PtcOverlayRect rect = ptc_overlay_backspace_rect(origin_x, origin_y);
    rect.x = origin_x + 246;
    return rect;
}

static inline PtcOverlayRect ptc_overlay_submit_rect(int origin_x, int origin_y, int width)
{
    return ptc_overlay_rect(origin_x, origin_y + PTC_OVERLAY_SUBMIT_Y, width, PTC_OVERLAY_SUBMIT_H);
}

static inline PtcOverlayRect ptc_overlay_status_rect(
    int origin_x, int origin_y, int width, bool expanded, bool needs_detail)
{
    int height = !expanded ? PTC_OVERLAY_STATUS_COLLAPSED_H :
        (needs_detail ? PTC_OVERLAY_STATUS_DETAIL_H : PTC_OVERLAY_STATUS_NORMAL_H);
    return ptc_overlay_rect(origin_x, origin_y + PTC_OVERLAY_STATUS_Y, width, height);
}

static inline PtcOverlayRect ptc_overlay_child_buffer_rect(int origin_x, int origin_y)
{
    return ptc_overlay_rect(origin_x, origin_y + 462, 176, 42);
}

static inline PtcOverlayRect ptc_overlay_child_parent_rect(int origin_x, int origin_y, int width)
{
    return ptc_overlay_rect(origin_x + 184, origin_y + 462, width - 184, 42);
}

#endif
