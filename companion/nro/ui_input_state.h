#ifndef PTC_COMPANION_UI_INPUT_STATE_H
#define PTC_COMPANION_UI_INPUT_STATE_H

#include "ui_model.h"

const char *ptc_ui_shortcut_common_label(int index);
bool ptc_ui_shortcut_mask_held(uint64_t configured_mask, uint64_t buttons);
bool ptc_ui_shortcut_hold_update(PtcUiShortcutHoldState *state, bool combo_held, int required_samples);
bool ptc_ui_confirm_hold_update(PtcUiConfirmHoldState *state, bool held, int required_samples);
bool ptc_ui_touch_after_entry_allowed(bool *ignore_until_release, bool touch_active);
uint16_t ptc_ui_confirm_hold_progress(const PtcUiConfirmHoldState *state, int required_samples);
uint16_t ptc_ui_adjust_minutes(uint16_t value, int delta, uint16_t minimum, uint16_t maximum);
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

#endif
