#ifndef PTC_COMPANION_UI_GRAPHICS_H
#define PTC_COMPANION_UI_GRAPHICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/rules/rules.h"
#include "ui_theme.h"
#include "../../common/security/credential_policy.h"
#include "../../third_party/qrcodegen/qrcodegen.h"

typedef enum {
    PTC_UI_CHILD = 0,
    PTC_UI_PARENT = 1,
    PTC_UI_ERROR = 2,
    PTC_UI_SETUP = 3
} PtcUiView;

typedef enum {
    PTC_UI_SETUP_SHORTCUT = 1,
    PTC_UI_SETUP_PIN = 2,
    PTC_UI_SETUP_ALBUM = 3,
    PTC_UI_SETUP_THEME = 4,
    PTC_UI_SETUP_TAKEOVER = 5,
    PTC_UI_SETUP_ZONE = 6
} PtcUiSetupStep;

typedef enum {
    PTC_UI_SHORTCUT_PRESET_LR = 0,
    PTC_UI_SHORTCUT_PRESET_COUNT = 14
} PtcUiShortcutPreset;

typedef enum {
    PTC_UI_PARENT_TODAY = 0,
    PTC_UI_PARENT_PLAN = 1,
    PTC_UI_PARENT_HOLIDAY = 2,
    PTC_UI_PARENT_SECURITY = 3,
    PTC_UI_PARENT_SUPPORT = 4,
    PTC_UI_PARENT_PAGE_COUNT = 5
} PtcUiParentPage;

typedef enum {
    PTC_UI_OVERLAY_NONE = 0,
    PTC_UI_OVERLAY_MINUTES = 1,
    PTC_UI_OVERLAY_WEEKLY = 2,
    PTC_UI_OVERLAY_CONFIRM = 3,
    PTC_UI_OVERLAY_NUMPAD = 4,
    PTC_UI_OVERLAY_CREDENTIAL = 5,
    PTC_UI_OVERLAY_GRANT_MANAGER = 6,
    PTC_UI_OVERLAY_QR = 7,
    PTC_UI_OVERLAY_WEEKLY_LEAVE = 8,
    PTC_UI_OVERLAY_SHORTCUT_MANAGER = 9,
    PTC_UI_OVERLAY_GRANT_LOCAL = 10,
    PTC_UI_OVERLAY_CREDENTIAL_LEAVE = 11,
    PTC_UI_OVERLAY_CODE_RESULT = 12,
    PTC_UI_OVERLAY_AUTH_ERROR = 13,
    PTC_UI_OVERLAY_SOFTWARE_INFO = 14,
    PTC_UI_OVERLAY_HOLIDAY_CALENDAR = 15,
    PTC_UI_OVERLAY_HOLIDAY_LEAVE = 16,
    PTC_UI_OVERLAY_SUPPORT_EVENT = 17,
    PTC_UI_OVERLAY_WEEKLY_BULK = 18,
    PTC_UI_OVERLAY_ALBUM_MANAGER = 19,
    PTC_UI_OVERLAY_MINUTE_EDITOR = 20,
    PTC_UI_OVERLAY_THEME = 21
} PtcUiOverlay;

typedef enum {
    PTC_UI_CREDENTIAL_INPUT = 0,
    PTC_UI_CREDENTIAL_RANDOM = 1,
    PTC_UI_CREDENTIAL_REVEAL = 2,
    PTC_UI_CREDENTIAL_DEMO = 3,
    PTC_UI_CREDENTIAL_SAVE = 4
} PtcUiCredentialSelection;

typedef enum {
    PTC_UI_GRANT_MANAGER_DEVICE = 0,
    PTC_UI_GRANT_MANAGER_SECRET = 1,
    PTC_UI_GRANT_MANAGER_EXPORT = 2,
    PTC_UI_GRANT_MANAGER_EDIT_URL = 3,
    PTC_UI_GRANT_MANAGER_RESET_URL = 4,
    PTC_UI_GRANT_MANAGER_COUNT = 5
} PtcUiGrantManagerSelection;

typedef enum {
    PTC_UI_DIAGNOSTIC_IDLE = 0,
    PTC_UI_DIAGNOSTIC_EXPORTING = 1,
    PTC_UI_DIAGNOSTIC_SUCCESS = 2,
    PTC_UI_DIAGNOSTIC_ERROR = 3
} PtcUiDiagnosticStatus;

typedef enum {
    PTC_UI_GRANT_LOCAL_ADJUST_FIRST = 0,
    PTC_UI_GRANT_LOCAL_ADJUST_LAST = 5,
    PTC_UI_GRANT_LOCAL_GENERATE = 6,
    PTC_UI_GRANT_LOCAL_BACK = 7
} PtcUiGrantLocalSelection;

typedef enum {
    PTC_UI_NUMPAD_NONE = 0,
    PTC_UI_NUMPAD_OFFLINE_CODE = 1,
    PTC_UI_NUMPAD_MINUTES = 2,
    PTC_UI_NUMPAD_WEEKLY_MINUTES = 3,
    PTC_UI_NUMPAD_HOLIDAY_MINUTES = 4,
    PTC_UI_NUMPAD_MAKEUP_MINUTES = 5
} PtcUiNumpadPurpose;

typedef enum {
    PTC_UI_OPERATION_NONE = 0,
    PTC_UI_OPERATION_SET_TODAY_LIMIT = 1,
    PTC_UI_OPERATION_ADD_TODAY_MINUTES = 2,
    PTC_UI_OPERATION_COMPLETE_SETUP = 3,
    PTC_UI_OPERATION_EMERGENCY_DISABLE = 4,
    PTC_UI_OPERATION_RESUME_CONTROL = 5,
    PTC_UI_OPERATION_SAVE_WEEKLY = 6,
    PTC_UI_OPERATION_RETRY_SETUP_RELEASE = 7,
    PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT = 8,
    PTC_UI_OPERATION_DISABLE_TODAY_LIMIT = 9,
    PTC_UI_OPERATION_RESTORE_TODAY_POLICY = 10,
    PTC_UI_OPERATION_SAVE_CREDENTIAL = 11,
    PTC_UI_OPERATION_SAVE_WEEKLY_LEAVE = 12,
    PTC_UI_OPERATION_REDEEM_OFFLINE_CODE = 13,
    PTC_UI_OPERATION_RESET_PAIRING_URL = 14,
    PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION = 15,
    PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY = 16,
    PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY = 17,
    PTC_UI_OPERATION_EXPORT_DIAGNOSTICS = 18
} PtcUiOperation;

typedef enum {
    PTC_UI_ACTION_AVAILABLE = 0,
    PTC_UI_ACTION_RECOMMENDED = 1,
    PTC_UI_ACTION_DISABLED = 2
} PtcUiActionState;

typedef struct {
    PtcUiView view;
    PtcUiParentPage parent_page;
    int selected_index;
    bool waiting;
    bool status_loaded;
    int64_t status_updated_at;
    bool restriction_enabled_available;
    bool restriction_enabled;
    bool temporary_unlocked_available;
    bool temporary_unlocked;
    bool remaining_available;
    bool played_minutes_available;
    int limited_today;
    int blocked_today;
    int unrestricted_today;
    int remaining_minutes;
    int played_minutes;
    int play_timer_enabled;
    int restricted_now;
    bool disable_flag_present;
    bool recovery_active;
    bool apply_pending_confirmation;
    bool parent_footer_focused;
    int parent_footer_selection;
    int parent_content_selection;
    int error_code;
    bool setup_restriction_cleared;
    bool setup_snapshot_available;
    int64_t setup_activate_after;
    char setup_phase[32];
    char compatibility_status[32];
    char apply_status[48];
    char disable_reason[48];
    char environment_hos[32];
    char environment_model[32];
    bool environment_available;
    bool environment_atmosphere;
    bool recent_events_available;
    int recent_event_count;
    char recent_events[3][128];
    char recent_event_names[3][48];
    char recent_event_types[3][48];
    char recent_event_errors[3][48];
    char recent_event_details[3][96];
    char recent_event_request_ids[3][80];
    int64_t recent_event_timestamps[3];
    uint16_t day_index;
    char mode[24];
    char request_id[80];
    char command_name[64];
    char transport_label[64];
    char message[192];
    char feedback_detail[192];
    char result_status[24];
    char result_type[48];
    int setup_step;
    int setup_shortcut_index;
    int setup_theme_index;
    int setup_zone_index;
    bool setup_album_enable;
    uint64_t custom_shortcut_mask;
    bool custom_shortcut_enabled;
    uint64_t shortcut_draft_mask;
    bool shortcut_draft_enabled;
    bool shortcut_draft_show_hint;
    uint64_t captured_shortcut_mask;
    bool show_parent_shortcut_hint;
    char custom_shortcut_label[96];
    char shortcut_draft_label[96];
    PtcUiOverlay overlay;
    PtcUiOverlay confirm_return_overlay;
    char confirm_return_title[64];
    char confirm_return_body[320];
    PtcUiOperation operation;
    int overlay_selection;
    uint16_t draft_minutes;
    uint16_t minimum_minutes;
    uint16_t maximum_minutes;
    PtcDayRule draft_week[7];
    PtcDayRule current_week[7];
    bool weekly_dirty;
    int weekly_grid_slot;
    int weekly_last_day_slot;
    int weekly_leave_selection;
    bool today_override_present;
    PtcDayRule today_override_rule;
    bool holiday_enabled;
    bool draft_holiday_enabled;
    PtcDayRule holiday_rule;
    PtcDayRule draft_holiday_rule;
    PtcDayRule makeup_workday_rule;
    PtcDayRule draft_makeup_workday_rule;
    bool holiday_dirty;
    int holiday_leave_selection;
    bool calendar_covered;
    bool calendar_update_warning;
    int holiday_calendar_page;
    int holiday_last_rule;
    int album_restriction_state;
    bool album_backup_valid;
    char album_restriction_detail[160];
    char rule_source[32];
    int editor_index;
    char overlay_title[64];
    char overlay_body[320];
    bool confirm_hold_required;
    PtcUiNumpadPurpose numpad_purpose;
    PtcUiOverlay numpad_return_overlay;
    char numpad_text[9];
    int numpad_cursor;
    uint8_t numpad_max_digits;
    uint16_t numpad_minimum;
    uint16_t numpad_maximum;
    uint16_t numpad_current;
    bool numpad_replace_on_input;
    char numpad_title[64];
    char numpad_guide[128];
    char numpad_error[96];
    char safety_hint[192];
    char auth_error_title[64];
    char auth_error_message[192];
    int auth_cooldown_seconds;
    char pending_code[9];
    int code_grant_minutes;
    bool code_preview_after_available;
    int code_preview_after_minutes;
    int code_effective_add_minutes;
    bool code_preview_capped;
    bool code_preview_converts_unlimited;
    bool code_before_remaining_available;
    int code_before_remaining_minutes;
    bool code_before_unlimited;
    bool code_result_pending;
    bool code_result_failed;
    int credential_kind;
    bool credential_revealed;
    bool credential_new_revealed;
    bool demo_secret_enabled;
    char credential_current[80];
    char credential_new[80];
    char pairing_base_url[PTC_PAIRING_BASE_URL_MAX_LEN + 1];
    char pairing_url[768];
    uint16_t grant_minutes;
    uint16_t grant_max_minutes;
    uint16_t grant_day_index;
    bool grant_has_code;
    uint16_t grant_issued_minutes;
    bool grant_estimate_available;
    int grant_estimate_minutes;
    bool grant_estimate_capped;
    bool grant_estimate_unrestricted;
    int64_t grant_estimated_at;
    bool grant_status_refresh_failed;
    char grant_code[9];
    char software_version[32];
    char repository_url[128];
    char pwa_url[PTC_PAIRING_BASE_URL_MAX_LEN + 1];
    uint8_t qr_code[qrcodegen_BUFFER_LEN_MAX];
    PtcUiDiagnosticStatus diagnostic_status;
    char diagnostic_path[192];
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
    PTC_UI_HIT_CHILD_PARENT,
    PTC_UI_HIT_CHILD_EXIT,
    PTC_UI_HIT_ERROR_RETRY,
    PTC_UI_HIT_ERROR_BACK,
    PTC_UI_HIT_SETUP_SHORTCUT_CARD,
    PTC_UI_HIT_SETUP_PRIMARY,
    PTC_UI_HIT_SETUP_BACK,
    PTC_UI_HIT_SETUP_PIN,
    PTC_UI_HIT_SETUP_ALBUM_TOGGLE,
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
    PTC_UI_HIT_THEME_OPTION
} PtcUiHitKind;

typedef struct {
    PtcUiHitKind kind;
    int index;
} PtcUiHit;

typedef struct {
    int held_samples;
    bool latched;
} PtcUiShortcutHoldState;

bool ptc_ui_graphics_init(void);
void ptc_ui_graphics_exit(void);
void ptc_ui_graphics_draw(const PtcUiModel *model, const PtcUiThemeView *theme);

int ptc_ui_parent_action_count(PtcUiParentPage page);
const char *ptc_ui_shortcut_common_label(int index);
void ptc_ui_format_custom_shortcut_hint(
    const char *shortcut_label,
    char *out,
    size_t out_size);
bool ptc_ui_shortcut_mask_held(uint64_t configured_mask, uint64_t buttons);
bool ptc_ui_shortcut_hold_update(PtcUiShortcutHoldState *state, bool combo_held, int required_samples);
int ptc_ui_migrate_setup_step(int step, int wizard_version);
PtcEffectiveRule ptc_ui_rule_after_today_restore(const PtcUiModel *model);
void ptc_ui_format_restore_today_basis(const PtcUiModel *model, char *out, size_t out_size);
void ptc_ui_format_weekly_save_result(const PtcUiModel *model, char *message, size_t message_size,
                                      char *detail, size_t detail_size);
void ptc_ui_format_holiday_save_result(const PtcUiModel *model, char *message, size_t message_size,
                                       char *detail, size_t detail_size);
int ptc_ui_weekday_for_display_slot(int slot);
void ptc_ui_change_parent_page(PtcUiModel *model, int direction);
void ptc_ui_move_parent_selection(PtcUiModel *model, int horizontal, int vertical);
uint16_t ptc_ui_adjust_minutes(uint16_t value, int delta, uint16_t minimum, uint16_t maximum);
uint16_t ptc_ui_today_limit_start_value(const PtcUiModel *model, uint16_t fallback);
bool ptc_ui_parse_minutes(const char *text, uint16_t minimum, uint16_t maximum, uint16_t *out);
void ptc_ui_numpad_open(
    PtcUiModel *model,
    PtcUiNumpadPurpose purpose,
    PtcUiOverlay return_overlay,
    const char *title,
    const char *guide,
    uint8_t max_digits,
    uint16_t minimum,
    uint16_t maximum,
    uint16_t current);
void ptc_ui_numpad_move(PtcUiModel *model, int horizontal, int vertical);
void ptc_ui_numpad_activate(PtcUiModel *model);
void ptc_ui_numpad_backspace(PtcUiModel *model);
void ptc_ui_numpad_clear(PtcUiModel *model);
void ptc_ui_numpad_adjust(PtcUiModel *model, int delta);
bool ptc_ui_numpad_validate(PtcUiModel *model, uint16_t *out_value);
void ptc_ui_numpad_finish(PtcUiModel *model);
int ptc_ui_preview_remaining_minutes(const PtcUiModel *model);
void ptc_ui_mark_status_updated(PtcUiModel *model, int64_t now);
int64_t ptc_ui_status_age_seconds(const PtcUiModel *model, int64_t now);
void ptc_ui_format_parent_status_summary(
    const PtcUiModel *model,
    int64_t now,
    char *out,
    size_t out_size);
void ptc_ui_format_holiday_priority_summary(const PtcUiModel *model, char *out, size_t out_size);
PtcRuleMode ptc_ui_next_rule_mode(PtcRuleMode mode);
bool ptc_ui_day_rule_effectively_changed(PtcDayRule before, PtcDayRule after);
bool ptc_ui_weekly_today_changed(const PtcUiModel *model);
bool ptc_ui_limit_minutes_would_restrict(const PtcUiModel *model, uint16_t minutes);
bool ptc_ui_day_rule_would_restrict(const PtcUiModel *model, PtcDayRule rule);
bool ptc_ui_setup_takeover_complete(const PtcUiModel *model);
void ptc_ui_weekly_leave_move(PtcUiModel *model, int direction);
void ptc_ui_move_weekly_focus(PtcUiModel *model, int horizontal, int vertical);
bool ptc_ui_apply_weekly_bulk(PtcUiModel *model, bool weekend);
void ptc_ui_move_overlay_selection(PtcUiModel *model, int horizontal, int vertical);
int ptc_ui_grant_estimate_remaining(const PtcUiModel *model, uint16_t grant_minutes, bool *capped);
int64_t ptc_ui_setup_grace_remaining(const PtcUiModel *model, int64_t now);
bool ptc_ui_cancel_overlay(PtcUiModel *model);
PtcUiOperation ptc_ui_take_confirmed_operation(PtcUiModel *model);
bool ptc_ui_apply_result_json(PtcUiModel *model, const char *text);
void ptc_ui_set_execution(PtcUiModel *model, const char *command_name, const char *transport_label);

PtcUiActionState ptc_ui_safety_action_available(const PtcUiModel *model, int index);
bool ptc_ui_safety_action_visible(const PtcUiModel *model, int index);
const char *ptc_ui_safety_action_hint(const PtcUiModel *model, int index);

/* Shared control geometry (single source of truth for drawing and touch). */
PtcUiRect ptc_ui_child_submit_rect(void);
PtcUiRect ptc_ui_child_refresh_rect(void);
PtcUiRect ptc_ui_child_footer_rect(int index);
PtcUiRect ptc_ui_error_retry_rect(void);
PtcUiRect ptc_ui_error_back_rect(void);
PtcUiRect ptc_ui_setup_shortcut_card_rect(int index);
PtcUiRect ptc_ui_setup_primary_rect(void);
PtcUiRect ptc_ui_setup_back_rect(void);
PtcUiRect ptc_ui_setup_pin_rect(void);
PtcUiRect ptc_ui_setup_album_toggle_rect(void);
PtcUiRect ptc_ui_setup_theme_rect(int index);
PtcUiRect ptc_ui_setup_zone_rect(int index);
PtcUiRect ptc_ui_notice_status_icon_rect(int y);
PtcUiRect ptc_ui_notice_command_text_rect(int y, int height);
PtcUiRect ptc_ui_parent_footer_rect(int index);
PtcUiRect ptc_ui_parent_refresh_rect(void);
PtcUiRect ptc_ui_parent_tab_rect(int index);
PtcUiRect ptc_ui_parent_card_rect(int index);
PtcUiRect ptc_ui_holiday_card_rect(int index);
PtcUiRect ptc_ui_holiday_enable_rect(void);
PtcUiRect ptc_ui_holiday_mode_rect(int index);
PtcUiRect ptc_ui_holiday_minutes_rect(int index);
PtcUiRect ptc_ui_holiday_calendar_rect(void);
PtcUiRect ptc_ui_holiday_page_action_rect(int index);
PtcUiRect ptc_ui_support_event_rect(int index);
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
PtcUiRect ptc_ui_minute_editor_key_rect(int index);
PtcUiRect ptc_ui_minute_editor_quick_rect(int index);
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
