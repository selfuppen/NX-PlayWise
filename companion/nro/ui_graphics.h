#ifndef PTC_COMPANION_UI_GRAPHICS_H
#define PTC_COMPANION_UI_GRAPHICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/rules/rules.h"

typedef enum {
    PTC_UI_CHILD = 0,
    PTC_UI_PARENT = 1,
    PTC_UI_ERROR = 2
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
    PTC_UI_OPERATION_RESUME_CONTROL = 7,
    PTC_UI_OPERATION_PROBE_RAW_BLOCK = 8,
    PTC_UI_OPERATION_PROBE_SUSPEND = 9
} PtcUiOperation;

typedef struct {
    PtcUiView view;
    PtcUiParentPage parent_page;
    int selected_index;
    bool waiting;
    bool status_loaded;
    bool remaining_available;
    bool played_minutes_available;
    int limited_today;
    int blocked_today;
    int unrestricted_today;
    int remaining_minutes;
    int played_minutes;
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
    char feedback_detail[192];
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

typedef struct {
    int x;
    int y;
    int w;
    int h;
} PtcUiRect;

typedef enum {
    PTC_UI_HIT_NONE = 0,
    PTC_UI_HIT_CHILD_SUBMIT_CODE,
    PTC_UI_HIT_CHILD_REFRESH,
    PTC_UI_HIT_CHILD_EXIT,
    PTC_UI_HIT_ERROR_RETRY,
    PTC_UI_HIT_ERROR_BACK,
    PTC_UI_HIT_PARENT_PREV_PAGE,
    PTC_UI_HIT_PARENT_NEXT_PAGE,
    PTC_UI_HIT_PARENT_REFRESH,
    PTC_UI_HIT_PARENT_BACK,
    PTC_UI_HIT_PARENT_TAB,
    PTC_UI_HIT_PARENT_CARD,
    PTC_UI_HIT_OVERLAY_CONFIRM,
    PTC_UI_HIT_OVERLAY_CANCEL,
    PTC_UI_HIT_MINUTES_DEC,
    PTC_UI_HIT_MINUTES_INC,
    PTC_UI_HIT_MINUTES_VALUE,
    PTC_UI_HIT_WEEKLY_DAY,
    PTC_UI_HIT_WEEKLY_MODE,
    PTC_UI_HIT_WEEKLY_MIN_UP,
    PTC_UI_HIT_WEEKLY_MIN_DOWN,
    PTC_UI_HIT_WEEKLY_MIN_INPUT,
    PTC_UI_HIT_BEDTIME_FIELD,
    PTC_UI_HIT_BEDTIME_ADJ_UP,
    PTC_UI_HIT_BEDTIME_ADJ_DOWN,
    PTC_UI_HIT_LIMIT_ACTION_OPTION
} PtcUiHitKind;

typedef struct {
    PtcUiHitKind kind;
    int index;
} PtcUiHit;

bool ptc_ui_graphics_init(void);
void ptc_ui_graphics_exit(void);
void ptc_ui_graphics_draw(const PtcUiModel *model);

int ptc_ui_parent_action_count(PtcUiParentPage page);
void ptc_ui_change_parent_page(PtcUiModel *model, int direction);
void ptc_ui_move_parent_selection(PtcUiModel *model, int horizontal, int vertical);
uint16_t ptc_ui_adjust_minutes(uint16_t value, int delta, uint16_t minimum, uint16_t maximum);
bool ptc_ui_parse_minutes(const char *text, uint16_t minimum, uint16_t maximum, uint16_t *out);
uint16_t ptc_ui_adjust_minute_of_day(uint16_t value, int delta);
PtcRuleMode ptc_ui_next_rule_mode(PtcRuleMode mode);
PtcLimitAction ptc_ui_shift_limit_action(PtcLimitAction action, int direction);
bool ptc_ui_cancel_overlay(PtcUiModel *model);
PtcUiOperation ptc_ui_take_confirmed_operation(PtcUiModel *model);
bool ptc_ui_apply_result_json(PtcUiModel *model, const char *text);

/* Shared control geometry (single source of truth for drawing and touch). */
PtcUiRect ptc_ui_child_submit_rect(void);
PtcUiRect ptc_ui_child_refresh_rect(void);
PtcUiRect ptc_ui_child_footer_rect(int index);
PtcUiRect ptc_ui_error_retry_rect(void);
PtcUiRect ptc_ui_error_back_rect(void);
PtcUiRect ptc_ui_parent_footer_rect(int index);
PtcUiRect ptc_ui_parent_tab_rect(int index);
PtcUiRect ptc_ui_parent_card_rect(int index);
PtcUiRect ptc_ui_dialog_rect(int width, int height);
PtcUiRect ptc_ui_minutes_value_rect(void);
PtcUiRect ptc_ui_minutes_dec_rect(void);
PtcUiRect ptc_ui_minutes_inc_rect(void);
PtcUiRect ptc_ui_weekly_day_rect(int index);
PtcUiRect ptc_ui_weekly_mode_rect(void);
PtcUiRect ptc_ui_weekly_min_up_rect(void);
PtcUiRect ptc_ui_weekly_min_down_rect(void);
PtcUiRect ptc_ui_weekly_min_input_rect(void);
PtcUiRect ptc_ui_bedtime_field_rect(int index);
PtcUiRect ptc_ui_bedtime_adj_up_rect(void);
PtcUiRect ptc_ui_bedtime_adj_down_rect(void);
PtcUiRect ptc_ui_limit_option_rect(int index);
PtcUiRect ptc_ui_confirm_rect(PtcUiOverlay overlay);
PtcUiRect ptc_ui_cancel_rect(PtcUiOverlay overlay);
bool ptc_ui_rect_contains(PtcUiRect rect, int x, int y);
PtcUiHit ptc_ui_hit_test(const PtcUiModel *model, int x, int y);

#endif
