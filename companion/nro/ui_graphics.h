#ifndef PTC_COMPANION_UI_GRAPHICS_H
#define PTC_COMPANION_UI_GRAPHICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/rules/rules.h"

typedef enum {
    PTC_UI_CHILD = 0,
    PTC_UI_PARENT = 1
} PtcUiView;

typedef enum {
    PTC_UI_PARENT_TODAY = 0,
    PTC_UI_PARENT_PLAN = 1,
    PTC_UI_PARENT_SAFETY = 2,
    PTC_UI_PARENT_PAGE_COUNT = 3
} PtcUiParentPage;

typedef enum {
    PTC_UI_OVERLAY_NONE = 0,
    PTC_UI_OVERLAY_MINUTES = 1,
    PTC_UI_OVERLAY_WEEKLY = 2,
    PTC_UI_OVERLAY_BEDTIME = 3,
    PTC_UI_OVERLAY_LIMIT_ACTION = 4,
    PTC_UI_OVERLAY_CONFIRM = 5
} PtcUiOverlay;

typedef enum {
    PTC_UI_OPERATION_NONE = 0,
    PTC_UI_OPERATION_SET_TODAY_LIMIT = 1,
    PTC_UI_OPERATION_ADD_TODAY_MINUTES = 2,
    PTC_UI_OPERATION_PARENT_UNLOCK = 3,
    PTC_UI_OPERATION_BLOCK_TODAY = 4,
    PTC_UI_OPERATION_QUICK_TEST = 5,
    PTC_UI_OPERATION_EMERGENCY_DISABLE = 6,
    PTC_UI_OPERATION_RESUME_CONTROL = 7
} PtcUiOperation;

typedef struct {
    PtcUiView view;
    PtcUiParentPage parent_page;
    int selected_index;
    bool waiting;
    bool status_loaded;
    bool remaining_available;
    int limited_today;
    int blocked_today;
    int unrestricted_today;
    int remaining_minutes;
    int play_timer_enabled;
    int restricted_now;
    bool bedtime_active;
    bool parent_unlock_active;
    bool play_timer_write_verified;
    bool play_timer_effect_verified;
    bool raw_block_verified;
    bool suspend_verified;
    char mode[24];
    char request_id[80];
    char message[192];
    char result_status[24];
    char result_type[48];
    PtcUiOverlay overlay;
    PtcUiOperation operation;
    uint16_t draft_minutes;
    uint16_t minimum_minutes;
    uint16_t maximum_minutes;
    PtcDayRule draft_week[7];
    PtcBedtimeRule draft_bedtime;
    PtcLimitAction draft_limit_action;
    int editor_index;
    char overlay_title[64];
    char overlay_body[192];
} PtcUiModel;

bool ptc_ui_graphics_init(void);
void ptc_ui_graphics_exit(void);
void ptc_ui_graphics_draw(const PtcUiModel *model);

int ptc_ui_parent_action_count(PtcUiParentPage page);
void ptc_ui_change_parent_page(PtcUiModel *model, int direction);
void ptc_ui_move_parent_selection(PtcUiModel *model, int horizontal, int vertical);
uint16_t ptc_ui_adjust_minutes(uint16_t value, int delta, uint16_t minimum, uint16_t maximum);
uint16_t ptc_ui_adjust_minute_of_day(uint16_t value, int delta);
PtcRuleMode ptc_ui_next_rule_mode(PtcRuleMode mode);
PtcLimitAction ptc_ui_shift_limit_action(PtcLimitAction action, int direction);
bool ptc_ui_cancel_overlay(PtcUiModel *model);
PtcUiOperation ptc_ui_take_confirmed_operation(PtcUiModel *model);
bool ptc_ui_apply_result_json(PtcUiModel *model, const char *text);

#endif
