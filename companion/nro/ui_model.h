#ifndef PTC_COMPANION_UI_MODEL_H
#define PTC_COMPANION_UI_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/protocol/redemption_history.h"
#include "../../common/protocol/activity_history.h"
#include "../../common/protocol/result_builder.h"
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
    PTC_UI_SETUP_THEME = 3,
    PTC_UI_SETUP_TAKEOVER = 4,
    PTC_UI_SETUP_ZONE = 5
} PtcUiSetupStep;

typedef enum {
    PTC_UI_SHORTCUT_PRESET_LR = 0,
    PTC_UI_SHORTCUT_PRESET_COUNT = 14
} PtcUiShortcutPreset;

typedef enum {
    PTC_UI_PARENT_TODAY = 0,
    PTC_UI_PARENT_PLAN = 1,
    PTC_UI_PARENT_GRANT = 2,
    PTC_UI_PARENT_SETTINGS = 3,
    PTC_UI_PARENT_SUPPORT = 4,
    PTC_UI_PARENT_PAGE_COUNT = 5
} PtcUiParentPage;

typedef enum {
    PTC_UI_PLAN_PAGE_ROOT = 0,
    PTC_UI_PLAN_PAGE_WEEKLY = 1,
    PTC_UI_PLAN_PAGE_HOLIDAY = 2,
    PTC_UI_PLAN_PAGE_BEDTIME = 3
} PtcUiPlanPage;

typedef enum {
    PTC_UI_BEDTIME_WEEKLY = 0,
    PTC_UI_BEDTIME_CALENDAR = 1,
    PTC_UI_BEDTIME_SCHEDULED = 2
} PtcUiBedtimeSection;

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
    PTC_UI_OVERLAY_THEME = 21,
    PTC_UI_OVERLAY_PIN = 22,
    PTC_UI_OVERLAY_REDEMPTION_HISTORY = 23,
    PTC_UI_OVERLAY_SCHEDULED = 24,
    PTC_UI_OVERLAY_AUTONOMY = 25,
    PTC_UI_OVERLAY_ACTIVITY_HISTORY = 26,
    PTC_UI_OVERLAY_HOME_DETAILS = 27,
    PTC_UI_OVERLAY_SCHEDULED_LEAVE = 28,
    PTC_UI_OVERLAY_BEDTIME = 29,
    PTC_UI_OVERLAY_QUICK_ADD = 30,
    PTC_UI_OVERLAY_BEDTIME_WINDOW = 31,
    PTC_UI_OVERLAY_BEDTIME_SPECIAL = 32,
    PTC_UI_OVERLAY_BEDTIME_LEAVE = 33,
    PTC_UI_OVERLAY_BEDTIME_BULK = 34
} PtcUiOverlay;

#define PTC_UI_PIN_MAX_DIGITS 64

/* 弹窗入场动画：8 帧内完成淡入与上浮，帧长 40ms，按动画时钟推进。 */
#define PTC_UI_OVERLAY_OPEN_FRAMES 8
#define PTC_UI_OVERLAY_OPEN_FRAME_MS 40
#define PTC_UI_OVERLAY_OPEN_TOTAL_MS (PTC_UI_OVERLAY_OPEN_FRAMES * PTC_UI_OVERLAY_OPEN_FRAME_MS)

/* 弹窗退场动画：5 帧反向淡出下沉，期间输入已回到下层页面。 */
#define PTC_UI_OVERLAY_CLOSE_FRAMES 5
#define PTC_UI_OVERLAY_CLOSE_FRAME_MS 40
#define PTC_UI_OVERLAY_CLOSE_TOTAL_MS (PTC_UI_OVERLAY_CLOSE_FRAMES * PTC_UI_OVERLAY_CLOSE_FRAME_MS)

/* 家长区页签指示胶囊滑动时长。 */
#define PTC_UI_PAGE_SWITCH_TOTAL_MS 240

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
    PTC_UI_HOT_RELOAD_UNKNOWN = 0,
    PTC_UI_HOT_RELOAD_CURRENT = 1,
    PTC_UI_HOT_RELOAD_PENDING = 2,
    PTC_UI_HOT_RELOAD_UNAVAILABLE = 3,
    PTC_UI_HOT_RELOAD_INCOMPLETE = 4,
    PTC_UI_HOT_RELOAD_RECOVERY_REQUIRED = 5,
    PTC_UI_HOT_RELOAD_RUNNING = 6,
    PTC_UI_HOT_RELOAD_SUCCESS = 7
} PtcUiHotReloadStatus;

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
    PTC_UI_NUMPAD_MAKEUP_MINUTES = 5,
    PTC_UI_NUMPAD_SCHEDULED_MINUTES = 6,
    PTC_UI_NUMPAD_GRANT_MINUTES = 7,
    PTC_UI_NUMPAD_BEDTIME_TIME = 8
} PtcUiNumpadPurpose;

typedef enum {
    PTC_UI_TIME_UNKNOWN = 0,
    PTC_UI_TIME_NORMAL,
    PTC_UI_TIME_REMINDER,
    PTC_UI_TIME_DANGER,
    PTC_UI_TIME_EXHAUSTED,
    PTC_UI_TIME_UNLIMITED,
    PTC_UI_TIME_WAITING,
    PTC_UI_TIME_DISABLED,
    PTC_UI_TIME_PROTECTION,
    PTC_UI_TIME_RECOVERY,
    PTC_UI_TIME_TEMPORARY_UNLOCK
} PtcUiTimeState;

typedef struct {
    char clock_text[6];
    char remaining_text[48];
    char freshness_text[48];
    bool progress_available;
    uint16_t progress_per_mille;
    PtcUiTimeState state;
} PtcUiTimeProjection;

typedef enum {
    PTC_UI_DURATION_HOURS = 0,
    PTC_UI_DURATION_MINUTES = 1
} PtcUiDurationField;

typedef enum {
    PTC_UI_BEDTIME_TIME_START = 0,
    PTC_UI_BEDTIME_TIME_END = 1
} PtcUiBedtimeTimeTarget;

typedef struct {
    int direction;
    int64_t started_ms;
    int64_t next_step_ms;
} PtcUiValueRepeatState;

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
    PTC_UI_OPERATION_EXPORT_DIAGNOSTICS = 18,
    PTC_UI_OPERATION_CLEAR_REDEMPTION_HISTORY = 19,
    PTC_UI_OPERATION_SAVE_SCHEDULED = 20,
    PTC_UI_OPERATION_SAVE_AUTONOMY = 21,
    PTC_UI_OPERATION_CLEAR_ACTIVITY_HISTORY = 22,
    PTC_UI_OPERATION_HOT_RELOAD = 23,
    PTC_UI_OPERATION_SKIP_BEDTIME = 24,
    PTC_UI_OPERATION_SAVE_HOLIDAY = 25,
    PTC_UI_OPERATION_SAVE_BEDTIME = 26
} PtcUiOperation;

typedef enum {
    PTC_UI_DECISION_UNKNOWN = 0,
    PTC_UI_DECISION_SELECTED,
    PTC_UI_DECISION_OVERRIDDEN,
    PTC_UI_DECISION_NOT_CONFIGURED,
    PTC_UI_DECISION_NOT_MATCHED,
    PTC_UI_DECISION_DISABLED,
    PTC_UI_DECISION_CALENDAR_UNCOVERED
} PtcUiDecisionState;

typedef struct {
    PtcUiDecisionState state;
    PtcDayRule rule;
    char reason[96];
} PtcUiDecisionStep;

typedef struct {
    PtcEffectiveRule effective;
    PtcUiDecisionStep today_override;
    PtcUiDecisionStep scheduled_override;
    PtcUiDecisionStep holiday;
    PtcUiDecisionStep weekly;
    char final_reason[128];
    char bedtime[128];
    char autonomy[128];
} PtcUiTodayDecision;

typedef enum {
    PTC_UI_ACTION_AVAILABLE = 0,
    PTC_UI_ACTION_RECOMMENDED = 1,
    PTC_UI_ACTION_DISABLED = 2
} PtcUiActionState;

typedef struct {
    PtcUiView view;
    PtcUiParentPage parent_page;
    PtcUiPlanPage plan_page;
    PtcUiBedtimeSection bedtime_section;
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
    bool redemption_history_available;
    int redemption_history_count;
    int redemption_history_page;
    PtcRedemptionHistoryRecord redemption_history[PTC_REDEMPTION_HISTORY_MAX_RECORDS];
    bool activity_history_available;
    int activity_history_count;
    int activity_history_page;
    PtcActivityHistoryRecord activity_history[PTC_ACTIVITY_HISTORY_MAX_RECORDS];
    PtcResultForecastDay forecast[PTC_RESULT_FORECAST_DAYS];
    char forecast_rule_sources[PTC_RESULT_FORECAST_DAYS][32];
    bool forecast_available;
    uint16_t daily_buffer_minutes;
    bool daily_buffer_claimed;
    bool daily_buffer_available;
    char daily_buffer_reason[32];
    PtcScheduledOverride scheduled_override;
    PtcScheduledOverride draft_scheduled_override;
    PtcAutonomyPolicy autonomy_policy;
    PtcAutonomyPolicy draft_autonomy_policy;
    PtcBedtimePolicy bedtime_policy;
    PtcBedtimePolicy draft_bedtime_policy;
    bool bedtime_active;
    bool bedtime_skipped;
    uint64_t bedtime_window_instance_id;
    uint16_t bedtime_start_day_index;
    uint16_t bedtime_start_minute;
    uint16_t bedtime_end_minute;
    char bedtime_source[32];
    bool bedtime_next_available;
    uint16_t bedtime_next_start_day_index;
    uint16_t bedtime_next_start_minute;
    uint16_t bedtime_next_end_minute;
    uint64_t bedtime_next_window_instance_id;
    bool bedtime_skipped_window_available;
    uint64_t bedtime_skipped_window_instance_id;
    uint16_t bedtime_skipped_start_day_index;
    uint16_t bedtime_skipped_start_minute;
    uint16_t bedtime_skipped_end_minute;
    char bedtime_skipped_source[32];
    bool bedtime_official_setting_confirmed;
    bool bedtime_overlay_verified;
    bool usage_summary_available;
    uint16_t usage_known_days_7;
    uint32_t usage_consumed_minutes_7;
    uint16_t usage_known_days_30;
    uint32_t usage_consumed_minutes_30;
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
    bool today_override_cleared_in_session;
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
    bool bedtime_dirty;
    bool bedtime_section_focused;
    int bedtime_editor_day;
    int bedtime_special_kind;
    PtcUiBedtimeTimeTarget bedtime_editor_time_target;
    int bedtime_bulk_target;
    uint64_t pending_bedtime_skip_instance_id;
    uint16_t pending_bedtime_skip_start_day_index;
    uint16_t pending_bedtime_skip_start_minute;
    uint16_t pending_bedtime_skip_end_minute;
    int album_restriction_state;
    bool album_backup_valid;
    char album_restriction_detail[160];
    char rule_source[32];
    int home_details_page;
    int editor_index;
    char overlay_title[64];
    char overlay_body[320];
    bool confirm_hold_required;
    uint16_t confirm_hold_progress;
    PtcUiNumpadPurpose numpad_purpose;
    PtcUiOverlay numpad_return_overlay;
    char numpad_text[9];
    int numpad_cursor;
    uint8_t numpad_max_digits;
    uint16_t numpad_minimum;
    uint16_t numpad_maximum;
    uint16_t numpad_current;
    bool numpad_replace_on_input;
    char duration_hours_text[6];
    char duration_minutes_text[6];
    PtcUiDurationField duration_field;
    bool duration_hours_replace_on_input;
    bool duration_minutes_replace_on_input;
    int8_t duration_scroll_dir;
    uint8_t duration_scroll_anim_ticks;
    uint8_t duration_step_feedback;
    char numpad_title[64];
    char numpad_guide[128];
    char numpad_error[96];
    char pin_text[PTC_UI_PIN_MAX_DIGITS + 1];
    char pin_title[64];
    char pin_guide[192];
    char pin_error[96];
    bool pin_keyboard_mode;
    int pin_focus;
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
    int64_t code_completed_at;
    bool code_actual_add_available;
    int code_actual_add_minutes;
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
    char grant_notice[192];
    char software_version[32];
    char app_release_id[96];
    char backend_release_id[96];
    char hot_reload_detail[192];
    int hot_reload_status;
    char repository_url[128];
    char pwa_url[PTC_PAIRING_BASE_URL_MAX_LEN + 1];
    uint8_t qr_code[qrcodegen_BUFFER_LEN_MAX];
    PtcUiDiagnosticStatus diagnostic_status;
    char diagnostic_path[192];
    int overlay_open_frames;
    int64_t overlay_opened_at_ms;
    PtcUiOverlay previous_overlay;
    PtcUiOverlay closing_overlay;
    int64_t closing_started_ms;
    /* 绘制侧可见的弹层：打开时等于 overlay，关闭动画期间保持上一个弹层。 */
    PtcUiOverlay rendered_overlay;
    PtcUiParentPage last_parent_page;
    int64_t page_switched_at_ms;
    int displayed_remaining_minutes;
    int displayed_grant_minutes;
} PtcUiModel;

typedef struct {
    int held_samples;
    bool latched;
} PtcUiShortcutHoldState;

typedef struct {
    int held_samples;
    bool completed;
} PtcUiConfirmHoldState;

typedef struct {
    PtcDayRule rule;
    int count;
} PtcUiWeeklyBulkRuleCount;

typedef struct {
    int target_count;
    int changed_count;
    int unchanged_count;
    int rule_group_count;
    PtcUiWeeklyBulkRuleCount rule_groups[5];
} PtcUiWeeklyBulkStats;

typedef enum { PTC_UI_PLAN_SAVED, PTC_UI_PLAN_WEEKLY, PTC_UI_PLAN_HOLIDAY, PTC_UI_PLAN_SCHEDULED } PtcUiPlanKind;

#endif
