#include "nro_app_internal.h"

static bool keyboard_input_initial(
    const char *header,
    const char *guide,
    char *out,
    size_t out_size,
    bool password,
    bool numeric,
    bool download_code,
    const char *initial)
{
    SwkbdConfig keyboard;
    Result result;
    if (!out || out_size == 0) {
        return false;
    }
    if (!initial) out[0] = '\0';
    result = swkbdCreate(&keyboard, 0);
    if (R_FAILED(result)) {
        return false;
    }
    if (password) {
        swkbdConfigMakePresetPassword(&keyboard);
    } else if (download_code) {
        swkbdConfigMakePresetDownloadCode(&keyboard);
    } else {
        swkbdConfigMakePresetDefault(&keyboard);
    }
    if (numeric) swkbdConfigSetType(&keyboard, SwkbdType_NumPad);
    swkbdConfigSetStringLenMin(&keyboard, 1);
    swkbdConfigSetStringLenMax(&keyboard, (u32)(out_size - 1));
    swkbdConfigSetHeaderText(&keyboard, header);
    swkbdConfigSetGuideText(&keyboard, guide);
    if (initial && initial[0]) swkbdConfigSetInitialText(&keyboard, initial);
    swkbdConfigSetOkButtonText(&keyboard, "确认");
    result = swkbdShow(&keyboard, out, out_size);
    swkbdClose(&keyboard);
    return R_SUCCEEDED(result) && out[0] != '\0';
}

bool keyboard_input(
    const char *header,
    const char *guide,
    char *out,
    size_t out_size,
    bool password,
    bool numeric,
    bool download_code)
{
    return keyboard_input_initial(header, guide, out, out_size, password, numeric, download_code, NULL);
}

static bool keyboard_date(UiState *ui, uint16_t current, uint16_t *out_day_index)
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    char initial[9];
    char value[9];
    if (!ui || !out_day_index || !ui->model.status_loaded ||
        !ptc_date_from_day_index(current, &year, &month, &day)) {
        if (ui) snprintf(ui->model.message, sizeof(ui->model.message),
                         "主机日期尚未加载，请刷新状态后再输入日期。");
        return false;
    }
    initial[0] = (char)('0' + (year / 1000u) % 10u);
    initial[1] = (char)('0' + (year / 100u) % 10u);
    initial[2] = (char)('0' + (year / 10u) % 10u);
    initial[3] = (char)('0' + year % 10u);
    initial[4] = (char)('0' + (month / 10u) % 10u);
    initial[5] = (char)('0' + month % 10u);
    initial[6] = (char)('0' + (day / 10u) % 10u);
    initial[7] = (char)('0' + day % 10u);
    initial[8] = '\0';
    snprintf(value, sizeof(value), "%s", initial);
    if (!keyboard_input_initial("格式：YYYYMMDD", "仅限今天或未来日期（8 位）",
                                value, sizeof(value), false, true, false, initial)) return false;
    if (!ptc_ui_parse_date_yyyymmdd(value, ui->model.day_index, out_day_index)) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "日期无效；请输入今天或未来的 8 位日期 YYYYMMDD。");
        return false;
    }
    return true;
}

static bool keyboard_span(UiState *ui, uint16_t current, uint16_t *out_days)
{
    char initial[4];
    char value[4];
    if (!ui || !out_days) return false;
    if (current < 1u) current = 1u;
    if (current > 366u) current = 366u;
    snprintf(initial, sizeof(initial), "%u", (unsigned)current);
    snprintf(value, sizeof(value), "%s", initial);
    if (!keyboard_input_initial("输入持续天数", "范围 1 到 366 天", value, sizeof(value),
                                false, true, false, initial)) return false;
    if (!ptc_ui_parse_span_days(value, out_days)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "持续天数必须是 1 到 366。");
        return false;
    }
    return true;
}

bool edit_date_range_start(UiState *ui, uint16_t *start, uint16_t *end)
{
    uint32_t duration;
    uint16_t next;
    if (!ui || !start || !end) return false;
    duration = *end >= *start ? (uint32_t)*end - *start + 1u : 1u;
    if (!keyboard_date(ui, *start, &next)) return false;
    if ((uint32_t)next + duration - 1u > UINT16_MAX) {
        snprintf(ui->model.message, sizeof(ui->model.message), "该开始日期会使结束日期越界，未修改草稿。");
        return false;
    }
    *start = next;
    *end = (uint16_t)(next + duration - 1u);
    return true;
}

bool edit_date_range_span(UiState *ui, uint16_t start, uint16_t *end)
{
    uint32_t duration = *end >= start ? (uint32_t)*end - start + 1u : 1u;
    uint16_t next;
    if (!ui || !end || !keyboard_span(ui, (uint16_t)duration, &next)) return false;
    if ((uint32_t)start + next - 1u > UINT16_MAX) {
        snprintf(ui->model.message, sizeof(ui->model.message), "持续天数超出可用日期范围，未修改草稿。");
        return false;
    }
    *end = (uint16_t)(start + next - 1u);
    return true;
}

static bool pin_keyboard_fallback(UiState *ui)
{
    char value[PTC_UI_PIN_MAX_DIGITS + 1];
    if (!ui) return false;
    snprintf(value, sizeof(value), "%s", ui->model.pin_text);
    ui->model.pin_keyboard_mode = true;
    if (keyboard_input_initial(ui->model.pin_title, ui->model.pin_guide,
                                value, sizeof(value), true, true, false,
                                ui->model.pin_text)) {
        snprintf(ui->model.pin_text, sizeof(ui->model.pin_text), "%s", value);
        ui->model.pin_error[0] = '\0';
        ui->model.pin_keyboard_mode = false;
        return true;
    }
    ui->model.pin_keyboard_mode = false;
    return false;
}

bool pin_input(UiState *ui, const char *title, const char *guide,
                      char *out, size_t out_size)
{
    int left_latched = -1;
    int right_latched = -1;
    int plus_samples = 0;
    bool plus_was_held = false;
    bool plus_keyboard = false;
    bool touch_was_active = false;
    bool touch_plus_was_held = false;
    bool ignore_touch_until_release = false;
    int touch_x = -1;
    int touch_y = -1;
    int draw_elapsed_ms = DRAW_INTERVAL_MS;
    HidTouchScreenState initial_touch;
    if (!ui || !out || out_size == 0 || !g_active_pad) return false;
    out[0] = '\0';
    ptc_ui_pin_open(&ui->model, title, guide);
    /* A touch that opened this PIN flow belongs to the previous page.  Do not
     * reinterpret it as a PIN digit, cancel, confirm, or keyboard action. */
    ignore_touch_until_release =
        hidGetTouchScreenStates(&initial_touch, 1) && initial_touch.count > 0;
    touch_was_active = ignore_touch_until_release;
    while (appletMainLoop() && !ui->exit_requested) {
        u64 buttons_down;
        u64 buttons_held;
        HidAnalogStickState left;
        HidAnalogStickState right;
        bool touch_active;
        bool touch_allowed;
        HidTouchScreenState touch;
        int left_digit;
        int right_digit;
        int digit;
        padUpdate(g_active_pad);
        buttons_down = padGetButtonsDown(g_active_pad);
        buttons_held = padGetButtons(g_active_pad);
        left = padGetStickPos(g_active_pad, 0);
        right = padGetStickPos(g_active_pad, 1);
        left_digit = ptc_ui_pin_digit_from_vector(left.x, left.y, STICK_DEADZONE);
        right_digit = ptc_ui_pin_digit_from_vector(right.x, right.y, STICK_DEADZONE);
        if (left_digit < 0) left_latched = -1;
        if (right_digit < 0) right_latched = -1;
        if (left_digit >= 0 && left_latched < 0) {
            ptc_ui_pin_append(&ui->model, left_digit);
            left_latched = left_digit;
        } else if (left_digit < 0 && right_digit >= 0 && right_latched < 0) {
            ptc_ui_pin_append(&ui->model, right_digit);
            right_latched = right_digit;
        }
        digit = -1;
        if (buttons_down & HidNpadButton_Up) digit = ptc_ui_pin_digit_from_button(0);
        else if (buttons_down & HidNpadButton_Right) digit = ptc_ui_pin_digit_from_button(1);
        else if (buttons_down & HidNpadButton_Down) digit = ptc_ui_pin_digit_from_button(2);
        else if (buttons_down & HidNpadButton_Left) digit = ptc_ui_pin_digit_from_button(3);
        if (digit >= 0) ptc_ui_pin_append(&ui->model, digit);
        if (buttons_down & HidNpadButton_X) ptc_ui_pin_append(&ui->model, 0);
        if (buttons_down & HidNpadButton_Y) ptc_ui_pin_append(&ui->model, 9);
        if (buttons_down & HidNpadButton_ZL) ptc_ui_pin_backspace(&ui->model);
        if (buttons_down & HidNpadButton_B) {
            ptc_ui_pin_finish(&ui->model);
            return false;
        }

        if (buttons_held & HidNpadButton_Plus) {
            if (!plus_was_held) plus_samples = 0;
            if (plus_samples < PIN_KEYBOARD_HOLD_TICKS) ++plus_samples;
            if (plus_samples >= PIN_KEYBOARD_HOLD_TICKS && !plus_keyboard) {
                plus_keyboard = true;
                (void)pin_keyboard_fallback(ui);
            }
        } else {
            if (plus_was_held && !plus_keyboard) {
                if (ptc_ui_pin_validate(&ui->model)) {
                    snprintf(out, out_size, "%s", ui->model.pin_text);
                    ptc_ui_pin_finish(&ui->model);
                    return true;
                }
            }
            plus_samples = 0;
            plus_keyboard = false;
        }
        plus_was_held = (buttons_held & HidNpadButton_Plus) != 0;

        touch_active = hidGetTouchScreenStates(&touch, 1) && touch.count > 0;
        touch_allowed = ptc_ui_touch_after_entry_allowed(&ignore_touch_until_release, touch_active);
        if (!touch_allowed) {
            if (!touch_active) touch_was_active = false;
        } else if (touch_active) {
            touch_x = (int)touch.touches[0].x;
            touch_y = (int)touch.touches[0].y;
            if (ptc_ui_rect_contains(ptc_ui_pin_confirm_rect(), touch_x, touch_y)) {
                if (!touch_plus_was_held) plus_samples = 0;
                if (plus_samples < PIN_KEYBOARD_HOLD_TICKS) ++plus_samples;
                if (plus_samples >= PIN_KEYBOARD_HOLD_TICKS && !plus_keyboard) {
                    plus_keyboard = true;
                    (void)pin_keyboard_fallback(ui);
                }
                touch_plus_was_held = true;
            } else {
                if (touch_plus_was_held) {
                    /* Sliding away cancels the confirm hold instead of turning
                     * the later release into an unintended short confirm. */
                    plus_samples = 0;
                    plus_keyboard = false;
                    touch_plus_was_held = false;
                } else if (!touch_was_active) {
                    PtcUiHit hit = ptc_ui_hit_test(&ui->model, touch_x, touch_y);
                    if (hit.kind == PTC_UI_HIT_PIN_KEY) ptc_ui_pin_append(&ui->model, hit.index);
                    else if (hit.kind == PTC_UI_HIT_PIN_BACKSPACE) ptc_ui_pin_backspace(&ui->model);
                    else if (hit.kind == PTC_UI_HIT_PIN_CONFIRM && ptc_ui_pin_validate(&ui->model)) {
                        snprintf(out, out_size, "%s", ui->model.pin_text);
                        ptc_ui_pin_finish(&ui->model);
                        return true;
                    } else if (hit.kind == PTC_UI_HIT_PIN_CANCEL) {
                        ptc_ui_pin_finish(&ui->model);
                        return false;
                    } else if (hit.kind == PTC_UI_HIT_PIN_KEYBOARD) {
                        (void)pin_keyboard_fallback(ui);
                    }
                }
            }
        } else if (touch_plus_was_held) {
            if (!plus_keyboard && ptc_ui_pin_validate(&ui->model)) {
                snprintf(out, out_size, "%s", ui->model.pin_text);
                ptc_ui_pin_finish(&ui->model);
                return true;
            }
            plus_samples = 0;
            plus_keyboard = false;
            touch_plus_was_held = false;
        }
        if (touch_allowed) touch_was_active = touch_active;
        draw_elapsed_ms += INPUT_LOOP_MS;
        if (draw_elapsed_ms >= (ui->animating ? DRAW_INTERVAL_FAST_MS : DRAW_INTERVAL_MS)) {
            ui->animating = update_animations(&ui->model);
            draw(ui);
            draw_elapsed_ms = 0;
        }
        svcSleepThread(ui->animating ? INPUT_LOOP_SLEEP_FAST_NS : INPUT_LOOP_SLEEP_NS);
    }
    ptc_ui_pin_finish(&ui->model);
    return false;
}

void edit_overlay_minutes(UiState *ui)
{
    char guide[96];
    snprintf(guide, sizeof(guide), "分别输入小时和分钟，总计范围 %u 到 %u 分钟",
             (unsigned int)ui->model.minimum_minutes, (unsigned int)ui->model.maximum_minutes);
    ptc_ui_numpad_open(
        &ui->model, PTC_UI_NUMPAD_MINUTES, PTC_UI_OVERLAY_MINUTES,
        ui->model.overlay_title, guide, 4,
        ui->model.minimum_minutes, ui->model.maximum_minutes, ui->model.draft_minutes);
}

void edit_weekly_minutes(UiState *ui)
{
    static const char *WEEKDAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    PtcDayRule *day;
    if (weekly_editing_blocked(ui)) {
        return;
    }
    day = &ui->model.draft_week[ui->model.editor_index];
    if (day->mode == PTC_RULE_MODE_LIMIT) {
        char title[64];
        char guide[128];
        snprintf(title, sizeof(title), "设置%s的周计划额度", WEEKDAYS[ui->model.editor_index]);
        snprintf(guide, sizeof(guide), "分别输入小时和分钟，总计 1 到 1440 分钟");
        ptc_ui_numpad_open(
            &ui->model, PTC_UI_NUMPAD_WEEKLY_MINUTES, PTC_UI_OVERLAY_NONE,
            title, guide, 4, 1, 1440, day->minutes);
    }
}
