#ifndef PTC_COMPANION_UI_STATE_H
#define PTC_COMPANION_UI_STATE_H

#include "ui_model.h"

typedef enum {
    PTC_UI_PLAN_IMPACT_CURRENT = 0,
    PTC_UI_PLAN_IMPACT_CHANGES_TODAY,
    PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE,
    PTC_UI_PLAN_IMPACT_UNKNOWN,
    PTC_UI_PLAN_IMPACT_EXHAUSTED
} PtcUiPlanImpactState;

typedef struct {
    PtcUiPlanImpactState state;
    PtcEffectiveRule before;
    PtcEffectiveRule after;
    bool quota_changes_today;
    bool source_changes_today;
    bool remaining_available;
    int remaining_minutes;
} PtcUiPlanImpactProjection;

int ptc_ui_parent_action_count(PtcUiParentPage page);
const char *ptc_ui_settings_status_label(const PtcUiModel *model);
PtcUiActionState ptc_ui_settings_support_state(const PtcUiModel *model);
const char *ptc_ui_shortcut_common_label(int index);
void ptc_ui_format_custom_shortcut_hint(
    const char *shortcut_label,
    char *out,
    size_t out_size);
bool ptc_ui_shortcut_mask_held(uint64_t configured_mask, uint64_t buttons);
bool ptc_ui_shortcut_hold_update(PtcUiShortcutHoldState *state, bool combo_held, int required_samples);
bool ptc_ui_confirm_hold_update(PtcUiConfirmHoldState *state, bool held, int required_samples);
bool ptc_ui_touch_after_entry_allowed(bool *ignore_until_release, bool touch_active);
uint16_t ptc_ui_confirm_hold_progress(const PtcUiConfirmHoldState *state, int required_samples);
int ptc_ui_migrate_setup_step(int step, int wizard_version);
PtcEffectiveRule ptc_ui_rule_after_today_restore(const PtcUiModel *model);
void ptc_ui_build_day_decision(const PtcUiModel *model, PtcUiPlanKind kind, uint16_t day_index, int64_t now,
                                PtcUiTodayDecision *decision);
void ptc_ui_build_today_decision(const PtcUiModel *model, PtcUiPlanKind kind, int64_t now,
                                 PtcUiTodayDecision *decision);
const char *ptc_ui_decision_state_label(PtcUiDecisionState state);
void ptc_ui_format_today_adjustment_status(const PtcUiModel *model, int64_t now,
                                           char *badge, size_t badge_size,
                                           char *detail, size_t detail_size);
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
bool ptc_ui_parse_date_yyyymmdd(const char *text, uint16_t today_day_index, uint16_t *out_day_index);
bool ptc_ui_parse_time_hhmm(const char *text, uint16_t *out_minute_of_day);
bool ptc_ui_parse_span_days(const char *text, uint16_t *out_days);
bool ptc_ui_grant_minutes_legal(uint16_t minutes, uint16_t maximum);
bool ptc_ui_duration_value(const PtcUiModel *model, uint16_t *out_value);
void ptc_ui_duration_select_field(PtcUiModel *model, PtcUiDurationField field);
void ptc_ui_duration_toggle_field(PtcUiModel *model);
bool ptc_ui_duration_step_field(PtcUiModel *model, int step);
int ptc_ui_value_repeat_update(
    PtcUiValueRepeatState *state,
    int direction,
    bool hour_field,
    int64_t now_ms);
void ptc_ui_pin_open(PtcUiModel *model, const char *title, const char *guide);
bool ptc_ui_pin_append(PtcUiModel *model, int digit);
bool ptc_ui_pin_backspace(PtcUiModel *model);
bool ptc_ui_pin_validate(PtcUiModel *model);
void ptc_ui_pin_finish(PtcUiModel *model);
int ptc_ui_pin_digit_from_vector(int x, int y, int deadzone);
int ptc_ui_pin_digit_from_button(int direction);
void ptc_ui_pin_format_mask(const PtcUiModel *model, char *out, size_t out_size);
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
bool ptc_ui_status_is_fresh(const PtcUiModel *model, int64_t now);
bool ptc_ui_parent_status_alert_visible(const PtcUiModel *model);
bool ptc_ui_operation_feedback_visible(const PtcUiModel *model);
const char *ptc_ui_runtime_notice_summary(const PtcUiModel *model);
void ptc_ui_project_notice(const PtcUiModel *model, PtcUiNoticeProjection *out);
void ptc_ui_project_time_status(
    const PtcUiModel *model,
    int64_t now,
    PtcUiTimeProjection *out);
void ptc_ui_format_status_age(const PtcUiModel *model, int64_t now, char *out, size_t out_size);
void ptc_ui_match_redemption_result(PtcUiModel *model);
const char *ptc_ui_code_failure_guidance(int error_code);
void ptc_ui_format_code(const char *code, char *out, size_t out_size);
void ptc_ui_format_today_mode(const PtcUiModel *model, char *out, size_t out_size);
void ptc_ui_format_quota_remaining(const PtcUiModel *model, char *out, size_t out_size);
void ptc_ui_format_home_remaining(const PtcUiModel *model, int64_t now, char *out, size_t out_size);
void ptc_ui_format_home_total(const PtcUiModel *model, char *out, size_t out_size);
void ptc_ui_format_home_total_value(const PtcUiModel *model, char *out, size_t out_size);
void ptc_ui_format_timer_status(const PtcUiModel *model, char *out, size_t out_size);
void ptc_ui_format_console_date(const PtcUiModel *model, char *out, size_t out_size);
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
bool ptc_ui_today_limit_requires_hold(const PtcUiModel *model, uint16_t minutes);
void ptc_ui_format_today_limit_confirmation(
    const PtcUiModel *model,
    char *risk,
    size_t risk_size,
    char *recovery,
    size_t recovery_size);
bool ptc_ui_day_rule_would_restrict(const PtcUiModel *model, PtcDayRule rule);
bool ptc_ui_plan_save_requires_hold(const PtcUiModel *model, PtcUiPlanKind kind, int64_t now);
void ptc_ui_project_plan_impact(const PtcUiModel *model, PtcUiPlanKind kind,
                                bool dirty, int64_t now, PtcUiPlanImpactProjection *out);
bool ptc_ui_setup_takeover_complete(const PtcUiModel *model);
bool ptc_ui_runtime_fingerprint_reconfirmation_needed(const PtcUiModel *model);
void ptc_ui_weekly_leave_move(PtcUiModel *model, int direction);
void ptc_ui_move_weekly_focus(PtcUiModel *model, int horizontal, int vertical);
bool ptc_ui_apply_weekly_bulk(PtcUiModel *model, bool weekend);
void ptc_ui_weekly_bulk_stats(const PtcUiModel *model, bool weekend, PtcUiWeeklyBulkStats *stats);
void ptc_ui_move_overlay_selection(PtcUiModel *model, int horizontal, int vertical);
int ptc_ui_grant_estimate_remaining(const PtcUiModel *model, uint16_t grant_minutes, bool *capped);
int64_t ptc_ui_setup_grace_remaining(const PtcUiModel *model, int64_t now);
bool ptc_ui_cancel_overlay(PtcUiModel *model);
bool ptc_ui_scheduled_dirty(const PtcUiModel *model);
void ptc_ui_discard_scheduled(PtcUiModel *model);
void ptc_ui_reconcile_scheduled_result(PtcUiModel *model, const PtcScheduledOverride *draft, bool preserve);
PtcEffectiveRule ptc_ui_plan_rule(const PtcUiModel *model, PtcUiPlanKind kind);
const char *ptc_ui_effective_rule_label(PtcRuleSource source);
void ptc_ui_format_plan_impact(const PtcUiModel *model, PtcUiPlanKind kind,
                             int64_t now, char *out, size_t out_size);
int ptc_ui_support_recommended_action(const PtcUiModel *model);
const char *ptc_ui_support_problem(const PtcUiModel *model);
PtcUiOperation ptc_ui_take_confirmed_operation(PtcUiModel *model);
bool ptc_ui_apply_result_json(PtcUiModel *model, const char *text);
bool ptc_ui_apply_redemption_history_text(PtcUiModel *model, const char *text);
bool ptc_ui_apply_activity_history_text(PtcUiModel *model, const char *text);
int ptc_ui_redemption_history_page_count(const PtcUiModel *model);
void ptc_ui_change_redemption_history_page(PtcUiModel *model, int direction);
int ptc_ui_activity_history_page_count(const PtcUiModel *model);
void ptc_ui_change_activity_history_page(PtcUiModel *model, int direction);
void ptc_ui_set_execution(PtcUiModel *model, const char *command_name, const char *transport_label);

PtcUiActionState ptc_ui_safety_action_available(const PtcUiModel *model, int index);
bool ptc_ui_safety_action_visible(const PtcUiModel *model, int index);
const char *ptc_ui_safety_action_hint(const PtcUiModel *model, int index);

#endif
