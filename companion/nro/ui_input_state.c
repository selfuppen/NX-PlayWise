#include "ui_input_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../common/time/ptc_time.h"

const char *ptc_ui_shortcut_common_label(int index)
{
    static const char *labels[] = {
        "L + R", "L + R + 上", "L + R + 下", "L + R + 左", "L + R + 右", "L + R + Plus(+)", "L + R + Minus(-)",
        "ZL + ZR", "ZL + ZR + 上", "ZL + ZR + 下", "ZL + ZR + 左", "ZL + ZR + 右", "ZL + ZR + Plus(+)", "ZL + ZR + Minus(-)"
    };
    if (index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        return "未选择";
    }
    return labels[index];
}

bool ptc_ui_shortcut_mask_held(uint64_t configured_mask, uint64_t buttons)
{
    return configured_mask != 0 && (buttons & configured_mask) == configured_mask;
}

bool ptc_ui_shortcut_hold_update(PtcUiShortcutHoldState *state, bool combo_held, int required_samples)
{
    if (!state || required_samples <= 0) return false;
    if (!combo_held) {
        state->held_samples = 0;
        state->latched = false;
        return false;
    }
    if (state->latched) return false;
    if (state->held_samples < required_samples) ++state->held_samples;
    if (state->held_samples < required_samples) return false;
    state->latched = true;
    return true;
}

bool ptc_ui_confirm_hold_update(PtcUiConfirmHoldState *state, bool held, int64_t now_ms, int required_ms)
{
    int64_t elapsed;
    if (!state || required_ms <= 0) return false;
    if (!held) {
        state->started_ms = 0;
        state->holding = false;
        state->completed = false;
        return false;
    }
    if (state->completed) return false;
    if (!state->holding) {
        state->started_ms = now_ms;
        state->holding = true;
        return false;
    }
    if (now_ms < state->started_ms) {
        /* A monotonic clock should not move backwards. Restart safely if the
         * platform clock is replaced or reset while the button is held. */
        state->started_ms = now_ms;
        return false;
    }
    elapsed = now_ms - state->started_ms;
    if (elapsed < required_ms) return false;
    state->completed = true;
    return true;
}

bool ptc_ui_touch_after_entry_allowed(bool *ignore_until_release, bool touch_active)
{
    if (!ignore_until_release) return false;
    if (*ignore_until_release) {
        if (!touch_active) *ignore_until_release = false;
        return false;
    }
    return true;
}

uint16_t ptc_ui_confirm_hold_progress(const PtcUiConfirmHoldState *state, int64_t now_ms, int required_ms)
{
    int64_t elapsed;
    int64_t progress;
    if (!state || required_ms <= 0 || !state->holding) return 0;
    if (state->completed) return 1000;
    if (now_ms < state->started_ms) return 0;
    elapsed = now_ms - state->started_ms;
    progress = elapsed * 1000 / required_ms;
    return (uint16_t)(progress > 1000 ? 1000 : progress);
}

bool ptc_ui_overlay_primary_uses_plus(PtcUiOverlay overlay)
{
    switch (overlay) {
    case PTC_UI_OVERLAY_MINUTES:
    case PTC_UI_OVERLAY_WEEKLY:
    case PTC_UI_OVERLAY_NUMPAD:
    case PTC_UI_OVERLAY_CREDENTIAL:
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
    case PTC_UI_OVERLAY_WEEKLY_BULK:
    case PTC_UI_OVERLAY_MINUTE_EDITOR:
    case PTC_UI_OVERLAY_SCHEDULED:
    case PTC_UI_OVERLAY_AUTONOMY:
    case PTC_UI_OVERLAY_BEDTIME:
    case PTC_UI_OVERLAY_BEDTIME_WINDOW:
    case PTC_UI_OVERLAY_BEDTIME_SPECIAL:
        return true;
    default:
        return false;
    }
}

uint16_t ptc_ui_adjust_minutes(uint16_t value, int delta, uint16_t minimum, uint16_t maximum)
{
    int adjusted = (int)value + delta;
    if (adjusted < (int)minimum) {
        adjusted = minimum;
    }
    if (adjusted > (int)maximum) {
        adjusted = maximum;
    }
    if (adjusted < 0) {
        adjusted += 1440;
    }
    return (uint16_t)adjusted;
}

bool ptc_ui_parse_minutes(const char *text, uint16_t minimum, uint16_t maximum, uint16_t *out)
{
    unsigned long value;
    const char *p;
    if (!text || !out || !text[0]) {
        return false;
    }
    for (p = text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    value = strtoul(text, NULL, 10);
    if (value < minimum || value > maximum) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

static bool parse_fixed_digits(const char *text, size_t length, unsigned long *out)
{
    size_t index;
    if (!text || !out || strlen(text) != length) return false;
    for (index = 0; index < length; ++index) {
        if (text[index] < '0' || text[index] > '9') return false;
    }
    *out = strtoul(text, NULL, 10);
    return true;
}

bool ptc_ui_parse_date_yyyymmdd(const char *text, uint16_t today_day_index, uint16_t *out_day_index)
{
    unsigned long value;
    uint16_t day_index;
    if (!out_day_index || !parse_fixed_digits(text, 8, &value) ||
        !ptc_day_index_from_date((uint16_t)(value / 10000UL),
                                 (uint8_t)((value / 100UL) % 100UL),
                                 (uint8_t)(value % 100UL), &day_index) ||
        day_index < today_day_index) {
        return false;
    }
    *out_day_index = day_index;
    return true;
}

bool ptc_ui_parse_time_hhmm(const char *text, uint16_t *out_minute_of_day)
{
    unsigned long value;
    unsigned long hours;
    unsigned long minutes;
    if (!out_minute_of_day || !parse_fixed_digits(text, 4, &value)) return false;
    hours = value / 100UL;
    minutes = value % 100UL;
    if (hours > 23UL || minutes > 59UL) return false;
    *out_minute_of_day = (uint16_t)(hours * 60UL + minutes);
    return true;
}

bool ptc_ui_parse_span_days(const char *text, uint16_t *out_days)
{
    return ptc_ui_parse_minutes(text, 1, 366, out_days);
}

bool ptc_ui_grant_minutes_legal(uint16_t minutes, uint16_t maximum)
{
    if (minutes == 0 || minutes > maximum) return false;
    if (minutes <= 4) return true;
    if (minutes <= 120) return minutes % 5 == 0;
    return minutes == 150 || minutes == 180 || minutes == 210 || minutes == 240;
}

static bool duration_purpose(PtcUiNumpadPurpose purpose)
{
    return purpose == PTC_UI_NUMPAD_MINUTES ||
        purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES ||
        purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
        purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES ||
        purpose == PTC_UI_NUMPAD_SCHEDULED_MINUTES ||
        purpose == PTC_UI_NUMPAD_GRANT_MINUTES ||
        purpose == PTC_UI_NUMPAD_BEDTIME_TIME;
}

static bool parse_duration_component(const char *text, unsigned int maximum, unsigned int *out)
{
    const char *p;
    unsigned long value;
    if (!text || !text[0] || !out) return false;
    for (p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    value = strtoul(text, NULL, 10);
    if (value > maximum) return false;
    *out = (unsigned int)value;
    return true;
}

bool ptc_ui_duration_value(const PtcUiModel *model, uint16_t *out_value)
{
    unsigned int hours;
    unsigned int minutes;
    unsigned int total;
    if (!model || !out_value || !duration_purpose(model->numpad_purpose) ||
        !parse_duration_component(model->duration_hours_text,
                                  model->numpad_purpose == PTC_UI_NUMPAD_BEDTIME_TIME ? 23 : 24,
                                  &hours) ||
        !parse_duration_component(model->duration_minutes_text, 59, &minutes)) {
        return false;
    }
    total = hours * 60U + minutes;
    if (total < model->numpad_minimum || total > model->numpad_maximum) return false;
    *out_value = (uint16_t)total;
    return true;
}

static void set_duration_value(PtcUiModel *model, uint16_t value)
{
    if (!model) return;
    snprintf(model->duration_hours_text, sizeof(model->duration_hours_text), "%u",
             (unsigned int)(value / 60U));
    snprintf(model->duration_minutes_text, sizeof(model->duration_minutes_text), "%u",
             (unsigned int)(value % 60U));
    model->numpad_current = value;
}

void ptc_ui_duration_select_field(PtcUiModel *model, PtcUiDurationField field)
{
    if (!model || !duration_purpose(model->numpad_purpose) ||
        (field != PTC_UI_DURATION_HOURS && field != PTC_UI_DURATION_MINUTES)) return;
    model->duration_field = field;
    model->numpad_error[0] = '\0';
}

void ptc_ui_duration_toggle_field(PtcUiModel *model)
{
    if (!model || !duration_purpose(model->numpad_purpose)) return;
    ptc_ui_duration_select_field(model, model->duration_field == PTC_UI_DURATION_HOURS
        ? PTC_UI_DURATION_MINUTES : PTC_UI_DURATION_HOURS);
}

bool ptc_ui_duration_step_field(PtcUiModel *model, int step)
{
    unsigned int hours = 0;
    unsigned int minutes = 0;
    int total;
    int direction;

    if (!model || !duration_purpose(model->numpad_purpose) || step == 0) return false;

    if (!parse_duration_component(model->duration_hours_text, 24, &hours) ||
        !parse_duration_component(model->duration_minutes_text, 59, &minutes)) {
        hours = model->numpad_current / 60U;
        minutes = model->numpad_current % 60U;
    }

    total = (int)(hours * 60U + minutes);
    direction = step > 0 ? 1 : -1;
    total += model->duration_field == PTC_UI_DURATION_HOURS ? direction * 60 : step;
    if (model->numpad_purpose == PTC_UI_NUMPAD_BEDTIME_TIME) {
        total %= 1440;
        if (total < 0) total += 1440;
    } else if (total < (int)model->numpad_minimum) {
        total = (int)model->numpad_minimum;
    } else if (total > (int)model->numpad_maximum) {
        total = (int)model->numpad_maximum;
    }
    if (model->numpad_purpose == PTC_UI_NUMPAD_GRANT_MINUTES &&
        !ptc_ui_grant_minutes_legal((uint16_t)total, model->numpad_maximum)) {
        int candidate = total;
        while (candidate >= (int)model->numpad_minimum &&
               candidate <= (int)model->numpad_maximum &&
               !ptc_ui_grant_minutes_legal((uint16_t)candidate, model->numpad_maximum)) {
            candidate += direction;
        }
        if (candidate < (int)model->numpad_minimum || candidate > (int)model->numpad_maximum) {
            candidate = (int)model->numpad_current;
        }
        total = candidate;
    }
    set_duration_value(model, (uint16_t)total);
    model->duration_hours_replace_on_input = false;
    model->duration_minutes_replace_on_input = false;
    model->numpad_error[0] = '\0';
    return true;
}

int ptc_ui_value_repeat_update(
    PtcUiValueRepeatState *state,
    int direction,
    bool hour_field,
    int64_t now_ms)
{
    int64_t elapsed;
    int magnitude;
    int interval_ms;

    if (!state) return 0;
    if (direction == 0) {
        memset(state, 0, sizeof(*state));
        return 0;
    }
    direction = direction > 0 ? 1 : -1;
    if (state->direction != direction || now_ms < state->started_ms) {
        state->direction = direction;
        state->started_ms = now_ms;
        state->next_step_ms = now_ms + 300;
        return direction;
    }
    if (now_ms < state->next_step_ms) return 0;

    elapsed = now_ms - state->started_ms;
    if (elapsed >= 1400) {
        magnitude = hour_field ? 1 : 15;
        interval_ms = 33;
    } else if (elapsed >= 700) {
        magnitude = hour_field ? 1 : 5;
        interval_ms = 67;
    } else {
        magnitude = 1;
        interval_ms = 100;
    }
    state->next_step_ms = now_ms + interval_ms;
    return direction * magnitude;
}

void ptc_ui_numpad_open(
    PtcUiModel *model,
    PtcUiNumpadPurpose purpose,
    PtcUiOverlay return_overlay,
    const char *title,
    const char *guide,
    uint8_t max_digits,
    uint16_t minimum,
    uint16_t maximum,
    uint16_t current)
{
    if (!model || purpose == PTC_UI_NUMPAD_NONE || max_digits == 0 || max_digits > 8) {
        return;
    }
    model->overlay = PTC_UI_OVERLAY_NUMPAD;
    model->numpad_purpose = purpose;
    model->numpad_return_overlay = return_overlay;
    model->numpad_text[0] = '\0';
    model->numpad_cursor = 0;
    model->numpad_max_digits = max_digits;
    model->numpad_minimum = minimum;
    model->numpad_maximum = maximum;
    model->numpad_current = current;
    model->numpad_replace_on_input = false;
    model->duration_scroll_dir = 0;
    model->duration_scroll_anim_ticks = 0;
    model->duration_step_feedback = 1;
    if (duration_purpose(purpose)) {
        set_duration_value(model, current);
        model->duration_field = PTC_UI_DURATION_MINUTES;
        model->duration_hours_replace_on_input = true;
        model->duration_minutes_replace_on_input = true;
        model->overlay = PTC_UI_OVERLAY_MINUTE_EDITOR;
    }
    model->numpad_error[0] = '\0';
    snprintf(model->numpad_title, sizeof(model->numpad_title), "%s", title ? title : "数字输入");
    snprintf(model->numpad_guide, sizeof(model->numpad_guide), "%s", guide ? guide : "使用方向键或摇杆选择数字");
}

void ptc_ui_numpad_move(PtcUiModel *model, int horizontal, int vertical)
{
    int row;
    int column;
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
        return;
    }
    row = model->numpad_cursor / 3;
    column = model->numpad_cursor % 3;
    if (horizontal != 0) {
        column = (column + (horizontal > 0 ? 1 : 2)) % 3;
    }
    if (vertical != 0) {
        row = (row + (vertical > 0 ? 1 : 3)) % 4;
    }
    model->numpad_cursor = row * 3 + column;
}

void ptc_ui_numpad_backspace(PtcUiModel *model)
{
    char *text;
    bool *replace;
    size_t length;
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
        return;
    }
    if (duration_purpose(model->numpad_purpose)) {
        text = model->duration_field == PTC_UI_DURATION_HOURS
            ? model->duration_hours_text : model->duration_minutes_text;
        replace = model->duration_field == PTC_UI_DURATION_HOURS
            ? &model->duration_hours_replace_on_input : &model->duration_minutes_replace_on_input;
    } else {
        text = model->numpad_text;
        replace = &model->numpad_replace_on_input;
    }
    if (*replace) {
        text[0] = '\0';
        *replace = false;
        return;
    }
    length = strlen(text);
    if (length > 0) {
        text[length - 1] = '\0';
    }
    model->numpad_error[0] = '\0';
}

void ptc_ui_numpad_clear(PtcUiModel *model)
{
    char *text;
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
        return;
    }
    if (duration_purpose(model->numpad_purpose)) {
        text = model->duration_field == PTC_UI_DURATION_HOURS
            ? model->duration_hours_text : model->duration_minutes_text;
        text[0] = '\0';
        if (model->duration_field == PTC_UI_DURATION_HOURS) model->duration_hours_replace_on_input = false;
        else model->duration_minutes_replace_on_input = false;
    } else {
        model->numpad_text[0] = '\0';
        model->numpad_replace_on_input = false;
    }
    model->numpad_error[0] = '\0';
}

void ptc_ui_numpad_adjust(PtcUiModel *model, int delta)
{
    uint16_t value;
    int direction;
    if (!model || !duration_purpose(model->numpad_purpose)) {
        return;
    }
    value = model->numpad_current;
    if (ptc_ui_duration_value(model, &value)) {
        /* A quick adjustment commits any complete value already typed. */
    }
    if (model->numpad_purpose == PTC_UI_NUMPAD_BEDTIME_TIME) {
        int wrapped = ((int)value + delta) % 1440;
        if (wrapped < 0) wrapped += 1440;
        value = (uint16_t)wrapped;
    } else {
        value = ptc_ui_adjust_minutes(
            value, delta, model->numpad_minimum, model->numpad_maximum);
    }
    if (model->numpad_purpose == PTC_UI_NUMPAD_GRANT_MINUTES &&
        !ptc_ui_grant_minutes_legal(value, model->numpad_maximum)) {
        int candidate = value;
        direction = delta > 0 ? 1 : -1;
        while (candidate >= (int)model->numpad_minimum &&
               candidate <= (int)model->numpad_maximum &&
               !ptc_ui_grant_minutes_legal((uint16_t)candidate, model->numpad_maximum)) {
            candidate += direction;
        }
        if (candidate >= (int)model->numpad_minimum && candidate <= (int)model->numpad_maximum)
            value = (uint16_t)candidate;
        else value = model->numpad_current;
    }
    set_duration_value(model, value);
    model->duration_hours_replace_on_input = false;
    model->duration_minutes_replace_on_input = false;
    model->numpad_error[0] = '\0';
}

void ptc_ui_numpad_activate(PtcUiModel *model)
{
    char *text;
    bool *replace;
    size_t capacity;
    size_t length;
    int digit;
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
        return;
    }
    if (model->numpad_cursor == 9) {
        ptc_ui_numpad_backspace(model);
        return;
    }
    if (model->numpad_cursor == 11) {
        ptc_ui_numpad_clear(model);
        return;
    }
    if (duration_purpose(model->numpad_purpose)) {
        text = model->duration_field == PTC_UI_DURATION_HOURS
            ? model->duration_hours_text : model->duration_minutes_text;
        replace = model->duration_field == PTC_UI_DURATION_HOURS
            ? &model->duration_hours_replace_on_input : &model->duration_minutes_replace_on_input;
        capacity = model->duration_field == PTC_UI_DURATION_HOURS
            ? sizeof(model->duration_hours_text) : sizeof(model->duration_minutes_text);
    } else {
        text = model->numpad_text;
        replace = &model->numpad_replace_on_input;
        capacity = sizeof(model->numpad_text);
    }
    if (*replace) {
        text[0] = '\0';
        *replace = false;
    }
    length = strlen(text);
    if ((duration_purpose(model->numpad_purpose) && length >= 2U) ||
        length + 1 >= capacity ||
        (!duration_purpose(model->numpad_purpose) && length >= model->numpad_max_digits)) {
        snprintf(model->numpad_error, sizeof(model->numpad_error), "当前输入项最多输入 %u 位数字",
                 duration_purpose(model->numpad_purpose) ? 2U : (unsigned int)model->numpad_max_digits);
        return;
    }
    digit = model->numpad_cursor == 10 ? 0 : model->numpad_cursor + 1;
    text[length] = (char)('0' + digit);
    text[length + 1] = '\0';
    model->numpad_error[0] = '\0';
}

bool ptc_ui_numpad_validate(PtcUiModel *model, uint16_t *out_value)
{
    uint16_t value = 0;
    size_t length;
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
        return false;
    }
    length = strlen(model->numpad_text);
    if (model->numpad_purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        if (length != 8) {
            snprintf(model->numpad_error, sizeof(model->numpad_error), "加时码必须为 8 位数字");
            return false;
        }
        return true;
    }
    if (duration_purpose(model->numpad_purpose)) {
        if (!ptc_ui_duration_value(model, &value)) {
            if (model->numpad_purpose == PTC_UI_NUMPAD_BEDTIME_TIME) {
                snprintf(model->numpad_error, sizeof(model->numpad_error),
                         "请输入有效时间：小时 0-23，分钟 0-59");
            } else {
                snprintf(model->numpad_error, sizeof(model->numpad_error), "请输入完整时长，总计范围为 %u 到 %u 分钟",
                         (unsigned int)model->numpad_minimum, (unsigned int)model->numpad_maximum);
            }
            return false;
        }
        if (model->numpad_purpose == PTC_UI_NUMPAD_GRANT_MINUTES &&
            !ptc_ui_grant_minutes_legal(value, model->numpad_maximum)) {
            snprintf(model->numpad_error, sizeof(model->numpad_error),
                     "支持 1-4、5-120 的 5 分钟档，以及 150/180/210/240 分钟");
            return false;
        }
    } else {
        return false;
    }
    if (out_value) {
        *out_value = value;
    }
    model->numpad_error[0] = '\0';
    return true;
}

void ptc_ui_numpad_finish(PtcUiModel *model)
{
    if (!model || (model->overlay != PTC_UI_OVERLAY_NUMPAD &&
                   model->overlay != PTC_UI_OVERLAY_MINUTE_EDITOR)) {
        return;
    }
    model->overlay = model->numpad_return_overlay;
    model->numpad_replace_on_input = false;
    model->duration_hours_replace_on_input = false;
    model->duration_minutes_replace_on_input = false;
    model->duration_step_feedback = 1;
    model->numpad_return_overlay = PTC_UI_OVERLAY_NONE;
    model->numpad_error[0] = '\0';
}

void ptc_ui_pin_open(PtcUiModel *model, const char *title, const char *guide)
{
    if (!model) return;
    model->overlay = PTC_UI_OVERLAY_PIN;
    model->pin_text[0] = '\0';
    model->pin_error[0] = '\0';
    model->pin_keyboard_mode = false;
    model->pin_focus = 1;
    snprintf(model->pin_title, sizeof(model->pin_title), "%s", title ? title : "任我玩 PIN");
    snprintf(model->pin_guide, sizeof(model->pin_guide), "%s", guide ? guide : "摇杆方向输入；X=0，Y=9");
}

bool ptc_ui_pin_append(PtcUiModel *model, int digit)
{
    size_t length;
    if (!model || model->overlay != PTC_UI_OVERLAY_PIN || digit < 0 || digit > 9) return false;
    length = strlen(model->pin_text);
    if (length >= PTC_UI_PIN_MAX_DIGITS) {
        snprintf(model->pin_error, sizeof(model->pin_error), "最多输入 %u 位数字", PTC_UI_PIN_MAX_DIGITS);
        return false;
    }
    model->pin_text[length] = (char)('0' + digit);
    model->pin_text[length + 1] = '\0';
    model->pin_error[0] = '\0';
    return true;
}

bool ptc_ui_pin_backspace(PtcUiModel *model)
{
    size_t length;
    if (!model || model->overlay != PTC_UI_OVERLAY_PIN) return false;
    length = strlen(model->pin_text);
    if (length == 0) return false;
    model->pin_text[length - 1] = '\0';
    model->pin_error[0] = '\0';
    return true;
}

bool ptc_ui_pin_validate(PtcUiModel *model)
{
    size_t length;
    if (!model || model->overlay != PTC_UI_OVERLAY_PIN) return false;
    length = strlen(model->pin_text);
    if (length == 0 || length > PTC_UI_PIN_MAX_DIGITS) {
        snprintf(model->pin_error, sizeof(model->pin_error), "请输入 1 到 %u 位数字", PTC_UI_PIN_MAX_DIGITS);
        return false;
    }
    model->pin_error[0] = '\0';
    return true;
}

void ptc_ui_pin_finish(PtcUiModel *model)
{
    if (!model || model->overlay != PTC_UI_OVERLAY_PIN) return;
    model->overlay = PTC_UI_OVERLAY_NONE;
    model->pin_text[0] = '\0';
    model->pin_error[0] = '\0';
    model->pin_keyboard_mode = false;
    model->pin_focus = 0;
}

int ptc_ui_pin_digit_from_vector(int x, int y, int deadzone)
{
    int ax = x < 0 ? -x : x;
    int ay = y < 0 ? -y : y;
    long long radius;
    if (deadzone < 0) deadzone = 0;
    radius = (long long)x * x + (long long)y * y;
    if (radius < (long long)deadzone * deadzone) return -1;
    if ((long long)ax * 1000 < (long long)ay * 414) return y > 0 ? 1 : 5;
    if ((long long)ay * 1000 < (long long)ax * 414) return x > 0 ? 3 : 7;
    if (x > 0 && y > 0) return 2;
    if (x > 0 && y < 0) return 4;
    if (x < 0 && y < 0) return 6;
    if (x < 0 && y > 0) return 8;
    return y > 0 ? 1 : 5;
}

int ptc_ui_pin_digit_from_button(int direction)
{
    switch (direction) {
    case 0: return 1;
    case 1: return 3;
    case 2: return 5;
    case 3: return 7;
    default: return -1;
    }
}

void ptc_ui_pin_format_mask(const PtcUiModel *model, char *out, size_t out_size)
{
    size_t length;
    size_t i;
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!model) return;
    length = strlen(model->pin_text);
    if (length > PTC_UI_PIN_MAX_DIGITS) length = PTC_UI_PIN_MAX_DIGITS;
    for (i = 0; i < length && i + 1 < out_size; ++i) out[i] = '*';
    out[i < out_size ? i : out_size - 1] = '\0';
}
