#ifndef PTC_COMPANION_UI_LAYOUT_H
#define PTC_COMPANION_UI_LAYOUT_H

#include "ui_model.h"

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
    PTC_UI_HIT_CHILD_BUFFER,
    PTC_UI_HIT_CHILD_PARENT,
    PTC_UI_HIT_CHILD_EXIT,
    PTC_UI_HIT_ERROR_RETRY,
    PTC_UI_HIT_ERROR_BACK,
    PTC_UI_HIT_SETUP_SHORTCUT_CARD,
    PTC_UI_HIT_SETUP_PRIMARY,
    PTC_UI_HIT_SETUP_BACK,
    PTC_UI_HIT_SETUP_PIN,
    PTC_UI_HIT_SETUP_THEME_OPTION,
    PTC_UI_HIT_SETUP_CHILD_ZONE,
    PTC_UI_HIT_SETUP_PARENT_ZONE,
    PTC_UI_HIT_PARENT_PREV_PAGE,
    PTC_UI_HIT_PARENT_NEXT_PAGE,
    PTC_UI_HIT_PARENT_REFRESH,
    PTC_UI_HIT_PARENT_STATUS,
    PTC_UI_HIT_PARENT_BACK,
    PTC_UI_HIT_PARENT_TAB,
    PTC_UI_HIT_PARENT_CARD,
    PTC_UI_HIT_OVERLAY_CONFIRM,
    PTC_UI_HIT_OVERLAY_CANCEL,
    PTC_UI_HIT_MINUTES_DEC,
    PTC_UI_HIT_MINUTES_INC,
    PTC_UI_HIT_MINUTES_DEC_LARGE,
    PTC_UI_HIT_MINUTES_INC_LARGE,
    PTC_UI_HIT_MINUTES_VALUE,
    PTC_UI_HIT_WEEKLY_DAY,
    PTC_UI_HIT_WEEKLY_MODE,
    PTC_UI_HIT_HOLIDAY_ENABLE,
    PTC_UI_HIT_HOLIDAY_MODE,
    PTC_UI_HIT_HOLIDAY_MINUTES,
    PTC_UI_HIT_HOLIDAY_CALENDAR,
    PTC_UI_HIT_HOLIDAY_PAGE_ACTION,
    PTC_UI_HIT_WEEKLY_MIN_UP,
    PTC_UI_HIT_WEEKLY_MIN_DOWN,
    PTC_UI_HIT_WEEKLY_MIN_DEC,
    PTC_UI_HIT_WEEKLY_MIN_INC,
    PTC_UI_HIT_WEEKLY_MIN_INPUT,
    PTC_UI_HIT_NUMPAD_KEY,
    PTC_UI_HIT_NUMPAD_QUICK,
    PTC_UI_HIT_DURATION_FIELD,
    PTC_UI_HIT_WEEKLY_SAVE,
    PTC_UI_HIT_WEEKLY_DISCARD,
    PTC_UI_HIT_WEEKLY_BULK,
    PTC_UI_HIT_WEEKLY_BULK_TARGET,
    PTC_UI_HIT_ALBUM_ACTION,
    PTC_UI_HIT_ALBUM_REFRESH,
    PTC_UI_HIT_OVERLAY_DISCARD,
    PTC_UI_HIT_SUPPORT_EVENT,
    PTC_UI_HIT_CREDENTIAL_INPUT,
    PTC_UI_HIT_CREDENTIAL_RANDOM,
    PTC_UI_HIT_CREDENTIAL_REVEAL,
    PTC_UI_HIT_CREDENTIAL_SAVE,
    PTC_UI_HIT_CREDENTIAL_DEMO,
    PTC_UI_HIT_GRANT_MANAGER_CARD,
    PTC_UI_HIT_GRANT_GENERATE,
    PTC_UI_HIT_SHORTCUT_OPTION,
    PTC_UI_HIT_SHORTCUT_DISABLE,
    PTC_UI_HIT_SHORTCUT_HINT,
    PTC_UI_HIT_GRANT_ADJUST,
    PTC_UI_HIT_THEME_OPTION,
    PTC_UI_HIT_PIN_KEY,
    PTC_UI_HIT_PIN_BACKSPACE,
    PTC_UI_HIT_PIN_CONFIRM,
    PTC_UI_HIT_PIN_CANCEL,
    PTC_UI_HIT_PIN_KEYBOARD,
    PTC_UI_HIT_HISTORY_PREV,
    PTC_UI_HIT_HISTORY_NEXT,
    PTC_UI_HIT_SCHEDULED_FIELD,
    PTC_UI_HIT_AUTONOMY_OPTION,
    PTC_UI_HIT_QUICK_ADD_OPTION,
    PTC_UI_HIT_BEDTIME_SECTION,
    PTC_UI_HIT_BEDTIME_FIELD,
    PTC_UI_HIT_BEDTIME_MASTER_SWITCH,
    PTC_UI_HIT_BEDTIME_OVERLAY_FIELD,
    PTC_UI_HIT_HOME_DETAILS,
    PTC_UI_HIT_HOME_DETAILS_TAB
} PtcUiHitKind;

typedef struct {
    PtcUiHitKind kind;
    int index;
} PtcUiHit;

/* Shared control geometry (single source of truth for drawing and touch). */
PtcUiRect ptc_ui_child_submit_rect(void);
PtcUiRect ptc_ui_child_refresh_rect(void);
PtcUiRect ptc_ui_child_buffer_rect(void);
PtcUiRect ptc_ui_child_footer_rect(int index);
PtcUiRect ptc_ui_error_retry_rect(void);
PtcUiRect ptc_ui_error_back_rect(void);
PtcUiRect ptc_ui_setup_shortcut_card_rect(int index);
PtcUiRect ptc_ui_setup_primary_rect(void);
PtcUiRect ptc_ui_setup_back_rect(void);
PtcUiRect ptc_ui_setup_pin_rect(void);
PtcUiRect ptc_ui_setup_theme_rect(int index);
PtcUiRect ptc_ui_setup_zone_rect(int index);
PtcUiRect ptc_ui_notice_status_icon_rect(int y);
PtcUiRect ptc_ui_notice_command_text_rect(int y, int height);
PtcUiRect ptc_ui_parent_footer_rect(int index);
PtcUiRect ptc_ui_parent_tab_rect(int index);
PtcUiRect ptc_ui_parent_card_rect(int index);
PtcUiRect ptc_ui_plan_card_rect(int index);
PtcUiRect ptc_ui_today_card_rect(int index);
PtcUiRect ptc_ui_home_summary_rect(bool parent);
PtcUiRect ptc_ui_home_details_rect(bool parent);
PtcUiRect ptc_ui_home_details_tab_rect(int index);
PtcUiOperation ptc_ui_today_operation(int index);
bool ptc_ui_open_home_details(PtcUiModel *model);
bool ptc_ui_home_notice_expanded(const PtcUiModel *model);
PtcUiRect ptc_ui_advanced_back_rect(void);
PtcUiRect ptc_ui_support_card_rect(int index);
PtcUiRect ptc_ui_holiday_card_rect(int index);
PtcUiRect ptc_ui_holiday_enable_rect(void);
PtcUiRect ptc_ui_holiday_mode_rect(int index);
PtcUiRect ptc_ui_holiday_minutes_rect(int index);
PtcUiRect ptc_ui_holiday_calendar_rect(void);
PtcUiRect ptc_ui_holiday_page_action_rect(int index);
PtcUiRect ptc_ui_support_event_rect(int index);
PtcUiRect ptc_ui_redemption_history_prev_rect(void);
PtcUiRect ptc_ui_redemption_history_next_rect(void);
PtcUiRect ptc_ui_scheduled_field_rect(int index);
PtcUiRect ptc_ui_autonomy_option_rect(int index);
PtcUiRect ptc_ui_bedtime_section_rect(int index);
PtcUiRect ptc_ui_bedtime_field_rect(int section, int index);
PtcUiRect ptc_ui_bedtime_master_switch_rect(void);
PtcUiRect ptc_ui_dialog_rect(int width, int height);
PtcUiRect ptc_ui_minutes_value_rect(void);
PtcUiRect ptc_ui_minutes_dec_rect(void);
PtcUiRect ptc_ui_minutes_inc_rect(void);
PtcUiRect ptc_ui_minutes_dec_large_rect(void);
PtcUiRect ptc_ui_minutes_inc_large_rect(void);
PtcUiRect ptc_ui_weekly_day_rect(int index);
PtcUiRect ptc_ui_weekly_day_header_rect(int index);
PtcUiRect ptc_ui_weekly_day_mode_rect(int index);
PtcUiRect ptc_ui_weekly_day_minutes_rect(int index);
PtcUiRect ptc_ui_weekly_bulk_rect(void);
PtcUiRect ptc_ui_weekly_mode_rect(void);
PtcUiRect ptc_ui_weekly_page_mode_rect(void);
PtcUiRect ptc_ui_weekly_min_up_rect(void);
PtcUiRect ptc_ui_weekly_min_down_rect(void);
PtcUiRect ptc_ui_weekly_min_dec_rect(void);
PtcUiRect ptc_ui_weekly_min_inc_rect(void);
PtcUiRect ptc_ui_weekly_min_input_rect(void);
PtcUiRect ptc_ui_numpad_display_rect(void);
PtcUiRect ptc_ui_numpad_key_rect(int index);
PtcUiRect ptc_ui_numpad_quick_rect(int index);
PtcUiRect ptc_ui_pin_dialog_rect(void);
PtcUiRect ptc_ui_pin_key_rect(int digit);
PtcUiRect ptc_ui_pin_backspace_rect(void);
PtcUiRect ptc_ui_pin_confirm_rect(void);
PtcUiRect ptc_ui_pin_cancel_rect(void);
PtcUiRect ptc_ui_pin_keyboard_rect(void);
PtcUiRect ptc_ui_minute_editor_key_rect(int index);
PtcUiRect ptc_ui_minute_editor_quick_rect(int index);
PtcUiRect ptc_ui_minute_editor_field_rect(PtcUiDurationField field);
PtcUiRect ptc_ui_code_slot_rect(int index);
void ptc_ui_move_bedtime_focus(PtcUiModel *model, int horizontal, int vertical);
PtcUiRect ptc_ui_confirm_rect(PtcUiOverlay overlay);
PtcUiRect ptc_ui_cancel_rect(PtcUiOverlay overlay);
PtcUiRect ptc_ui_discard_rect(PtcUiOverlay overlay);
PtcUiRect ptc_ui_weekly_save_rect(void);
PtcUiRect ptc_ui_weekly_discard_rect(void);
PtcUiRect ptc_ui_weekly_bulk_target_rect(int index);
PtcUiRect ptc_ui_album_action_rect(int index);
PtcUiRect ptc_ui_album_refresh_rect(void);
PtcUiRect ptc_ui_theme_option_rect(int index);
PtcUiRect ptc_ui_credential_input_rect(void);
PtcUiRect ptc_ui_credential_random_rect(void);
PtcUiRect ptc_ui_credential_reveal_rect(void);
PtcUiRect ptc_ui_credential_demo_rect(void);
PtcUiRect ptc_ui_grant_manager_card_rect(int index);
PtcUiRect ptc_ui_grant_generate_rect(void);
PtcUiRect ptc_ui_shortcut_option_rect(int index);
PtcUiRect ptc_ui_shortcut_disable_rect(void);
PtcUiRect ptc_ui_shortcut_hint_rect(void);
PtcUiRect ptc_ui_grant_adjust_rect(int index);
bool ptc_ui_rect_contains(PtcUiRect rect, int x, int y);
PtcUiHit ptc_ui_hit_test(const PtcUiModel *model, int x, int y);


#endif
