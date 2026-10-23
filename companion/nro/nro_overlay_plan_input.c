#include "nro_app_internal.h"

static const struct {
    uint16_t start;
    uint16_t end;
} BEDTIME_PRESETS[] = {
    {21 * 60, 7 * 60},
    {21 * 60 + 30, 7 * 60},
    {22 * 60, 7 * 60},
    {22 * 60 + 30, 7 * 60 + 30},
    {23 * 60, 8 * 60}
};

void apply_bedtime_preset(UiState *ui, int preset)
{
    PtcBedtimeWindow *window = NULL;
    if (!ui || preset < 0 || preset >= 5) return;
    if (ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_WINDOW) {
        window = &ui->model.draft_bedtime_policy.week[ui->model.bedtime_editor_day];
    } else if (ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_SPECIAL) {
        PtcBedtimeSpecialRule *rule = ui->model.bedtime_special_kind == 0
            ? &ui->model.draft_bedtime_policy.holiday_rule
            : (ui->model.bedtime_special_kind == 1
                ? &ui->model.draft_bedtime_policy.makeup_workday_rule
                : &ui->model.draft_bedtime_policy.scheduled_override.rule);
        rule->mode = PTC_BEDTIME_OVERRIDE_CUSTOM;
        window = &rule->window;
    }
    if (!window) return;
    window->start_minute = BEDTIME_PRESETS[preset].start;
    window->end_minute = BEDTIME_PRESETS[preset].end;
    window->enabled = true;
    update_bedtime_dirty(ui);
}

static int next_bedtime_preset(const PtcBedtimeWindow *window)
{
    if (!window) return 0;
    for (int preset = 0; preset < 5; ++preset) {
        if (window->start_minute == BEDTIME_PRESETS[preset].start &&
            window->end_minute == BEDTIME_PRESETS[preset].end)
            return (preset + 1) % 5;
    }
    return 0;
}

static void handle_quick_add_overlay_input(UiState *ui, u64 down)
{
    static const uint16_t OPTIONS[] = {15, 30, 60};
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & HidNpadButton_Left) {
        ui->model.overlay_selection = ui->model.overlay_selection <= 0 ? 3 : ui->model.overlay_selection - 1;
    } else if (down & HidNpadButton_Right) {
        ui->model.overlay_selection = (ui->model.overlay_selection + 1) % 4;
    } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
        if (ui->model.overlay_selection == 3) {
            ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_MINUTES, PTC_UI_OVERLAY_QUICK_ADD,
                ptc_ui_text(PTC_UI_T_CUSTOMIZED_QUICK_GRANT), ptc_ui_text(PTC_UI_T_ENTER_1_TO_120_MINUTES_CONFIRMATION_WILL),
                3, 1, 120, ui->model.draft_minutes);
            ui->model.operation = PTC_UI_OPERATION_ADD_TODAY_MINUTES;
        } else {
            char body[192];
            ui->model.draft_minutes = OPTIONS[ui->model.overlay_selection];
            snprintf(body, sizeof(body), ptc_ui_text(PTC_UI_T_ADD_U_MIN_TO_TODAY_S_CURRENT),
                (unsigned int)ui->model.draft_minutes);
            open_confirm_overlay(ui, PTC_UI_OPERATION_ADD_TODAY_MINUTES, ptc_ui_text(PTC_UI_T_CONFIRM_RAPID_GRANT), body);
        }
    }
    return;
}

static void handle_bedtime_window_overlay_input(UiState *ui, u64 down)
{
    PtcBedtimeWindow *window = &ui->model.draft_bedtime_policy.week[ui->model.bedtime_editor_day];
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & (HidNpadButton_Up | HidNpadButton_Left)) {
        if (ui->model.overlay_selection > 0) --ui->model.overlay_selection;
    } else if (down & (HidNpadButton_Down | HidNpadButton_Right)) {
        if (ui->model.overlay_selection < 2) ++ui->model.overlay_selection;
    } else if ((down & (HidNpadButton_A | HidNpadButton_X)) && ui->model.overlay_selection == 0) {
        ptc_audio_play(PTC_SE_TOGGLE);
        window->enabled = !window->enabled;
        update_bedtime_dirty(ui);
    } else if ((down & HidNpadButton_A) && ui->model.overlay_selection >= 1) {
        open_bedtime_time_editor(ui, ui->model.overlay_selection == 1
            ? PTC_UI_BEDTIME_TIME_START : PTC_UI_BEDTIME_TIME_END);
    } else if (down & HidNpadButton_Y) {
        ptc_audio_play(PTC_SE_CONFIRM);
        apply_bedtime_preset(ui, next_bedtime_preset(window));
    } else if (down & HidNpadButton_Plus) {
        ptc_ui_cancel_overlay(&ui->model);
    }
    return;
}

static void handle_bedtime_special_overlay_input(UiState *ui, u64 down)
{
    PtcBedtimeSpecialRule *rule = ui->model.bedtime_special_kind == 0
        ? &ui->model.draft_bedtime_policy.holiday_rule
        : (ui->model.bedtime_special_kind == 1
            ? &ui->model.draft_bedtime_policy.makeup_workday_rule
            : &ui->model.draft_bedtime_policy.scheduled_override.rule);
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & (HidNpadButton_Up | HidNpadButton_Left)) {
        if (ui->model.overlay_selection > 0) --ui->model.overlay_selection;
    } else if (down & (HidNpadButton_Down | HidNpadButton_Right)) {
        if (ui->model.overlay_selection < 2) ++ui->model.overlay_selection;
    } else if ((down & (HidNpadButton_A | HidNpadButton_X)) && ui->model.overlay_selection == 0) {
        ptc_audio_play(PTC_SE_TOGGLE);
        rule->mode = (PtcBedtimeOverrideMode)((rule->mode + 1) % 3);
        if (rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM) rule->window.enabled = true;
        update_bedtime_dirty(ui);
    } else if ((down & HidNpadButton_A) && ui->model.overlay_selection >= 1 &&
               rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM) {
        open_bedtime_time_editor(ui, ui->model.overlay_selection == 1
            ? PTC_UI_BEDTIME_TIME_START : PTC_UI_BEDTIME_TIME_END);
    } else if ((down & HidNpadButton_Y) && rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM) {
        ptc_audio_play(PTC_SE_CONFIRM);
        apply_bedtime_preset(ui, next_bedtime_preset(&rule->window));
    } else if (down & HidNpadButton_Plus) {
        ptc_ui_cancel_overlay(&ui->model);
    }
    return;
}

static void handle_bedtime_bulk_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
        ui->model.overlay_selection = 1 - ui->model.overlay_selection;
    } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
        PtcBedtimeWindow source = ui->model.draft_bedtime_policy.week[ui->model.bedtime_editor_day];
        int target = ui->model.overlay_selection;
        if (target == 0) {
            for (int day = 1; day <= 5; ++day) ui->model.draft_bedtime_policy.week[day] = source;
        } else {
            ui->model.draft_bedtime_policy.week[0] = source;
            ui->model.draft_bedtime_policy.week[6] = source;
        }
        update_bedtime_dirty(ui);
        ptc_ui_cancel_overlay(&ui->model);
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_COPIED_TO_S_YOU_CAN_DISCARD_BEFORE),
            target == 0 ? ptc_ui_text(PTC_UI_T_MONDAY_TO_FRIDAY) : ptc_ui_text(PTC_UI_T_SATURDAY_SUNDAY));
    }
    return;
}

static void handle_bedtime_leave_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        cancel_bedtime_navigation(ui);
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & HidNpadButton_X) {
        ptc_audio_play(PTC_SE_CANCEL);
        discard_bedtime_draft(ui);
        ptc_ui_cancel_overlay(&ui->model);
        finish_bedtime_navigation(ui);
    } else if ((down & (HidNpadButton_A | HidNpadButton_Plus)) && !ui->model.disable_flag_present) {
        ui->model.overlay = PTC_UI_OVERLAY_NONE;
        save_bedtime_from_page(ui);
        if (!ui->waiting && ui->model.overlay != PTC_UI_OVERLAY_CONFIRM)
            cancel_bedtime_navigation(ui);
    }
    return;
}

static void handle_bedtime_overlay_input(UiState *ui, u64 down)
{
    PtcBedtimePolicy *draft = &ui->model.draft_bedtime_policy;
    if (ui->waiting) return;
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & (HidNpadButton_Up | HidNpadButton_Left)) {
        if (ui->model.overlay_selection > 0) --ui->model.overlay_selection;
    } else if (down & (HidNpadButton_Down | HidNpadButton_Right)) {
        if (ui->model.overlay_selection < 3) ++ui->model.overlay_selection;
    } else if ((down & (HidNpadButton_A | HidNpadButton_X)) && ui->model.overlay_selection == 0) {
        ptc_audio_play(PTC_SE_TOGGLE);
        draft->enabled = !draft->enabled;
    } else if ((down & HidNpadButton_A) &&
               (ui->model.overlay_selection == 1 || ui->model.overlay_selection == 2)) {
        open_bedtime_time_editor(ui, ui->model.overlay_selection == 1
            ? PTC_UI_BEDTIME_TIME_START : PTC_UI_BEDTIME_TIME_END);
    } else if (down & HidNpadButton_Plus) {
        update_bedtime_dirty(ui);
        save_bedtime_from_page(ui);
    }
    return;
}

static void handle_scheduled_leave_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
    else if (down & HidNpadButton_A) ptc_ui_discard_scheduled(&ui->model);
    return;
}

static void handle_scheduled_overlay_input(UiState *ui, u64 down)
{
    if (ui->waiting) return;
    if (ui->model.disable_flag_present && !(down & HidNpadButton_B)) return;
    PtcScheduledOverride *draft = &ui->model.draft_scheduled_override;
    uint32_t duration = draft->end_day_index >= draft->start_day_index
        ? (uint32_t)draft->end_day_index - draft->start_day_index + 1u : 1u;
    int direction = (down & (HidNpadButton_Right | HidNpadButton_R | HidNpadButton_ZR)) ? 1 :
        ((down & (HidNpadButton_Left | HidNpadButton_L | HidNpadButton_ZL)) ? -1 : 0);
    int step = (down & (HidNpadButton_ZL | HidNpadButton_ZR)) ? 7 : 1;
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & HidNpadButton_Up) {
        ui->model.overlay_selection = ui->model.overlay_selection <= 0 ? 3 : ui->model.overlay_selection - 1;
    } else if (down & HidNpadButton_Down) {
        ui->model.overlay_selection = (ui->model.overlay_selection + 1) % 4;
    } else if (down & HidNpadButton_X) {
        ptc_audio_play(PTC_SE_TOGGLE);
        draft->rule.mode = ptc_ui_next_rule_mode(draft->rule.mode);
    } else if ((down & HidNpadButton_A) && ui->model.overlay_selection == 0) {
        ptc_audio_play(PTC_SE_TOGGLE);
        draft->enabled = !draft->enabled;
    } else if ((down & HidNpadButton_A) && ui->model.overlay_selection == 1) {
        (void)edit_date_range_start(ui, &draft->start_day_index, &draft->end_day_index);
    } else if ((down & HidNpadButton_A) && ui->model.overlay_selection == 2) {
        (void)edit_date_range_span(ui, draft->start_day_index, &draft->end_day_index);
    } else if ((down & HidNpadButton_A) && ui->model.overlay_selection == 3) {
        edit_scheduled_minutes(ui);
    } else if (direction != 0 && ui->model.overlay_selection == 1) {
        int start = (int)draft->start_day_index + direction * step;
        if (start < (int)ui->model.day_index) start = ui->model.day_index;
        if (start > 65535 - (int)duration + 1) start = 65535 - (int)duration + 1;
        draft->start_day_index = (uint16_t)start;
        draft->end_day_index = (uint16_t)(start + (int)duration - 1);
        ptc_audio_play(PTC_SE_STEP);
    } else if (direction != 0 && ui->model.overlay_selection == 2) {
        int next = (int)duration + direction * step;
        if (next < 1) next = 1;
        if (next > 366) next = 366;
        if ((uint32_t)draft->start_day_index + (uint32_t)next - 1u > UINT16_MAX) {
            next = (int)(UINT16_MAX - draft->start_day_index + 1u);
        }
        draft->end_day_index = (uint16_t)(draft->start_day_index + next - 1);
        ptc_audio_play(PTC_SE_STEP);
    } else if (down & HidNpadButton_Plus) {
        save_scheduled_from_overlay(ui);
    }
    return;
}

static void handle_autonomy_overlay_input(UiState *ui, u64 down)
{
    static const uint16_t OPTIONS[] = {0, 5, 10, 15};
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & HidNpadButton_Left) {
        ui->model.overlay_selection = ui->model.overlay_selection <= 0 ? 3 : ui->model.overlay_selection - 1;
        ui->model.draft_autonomy_policy.daily_buffer_minutes = OPTIONS[ui->model.overlay_selection];
    } else if (down & HidNpadButton_Right) {
        ui->model.overlay_selection = (ui->model.overlay_selection + 1) % 4;
        ui->model.draft_autonomy_policy.daily_buffer_minutes = OPTIONS[ui->model.overlay_selection];
    } else if (down & HidNpadButton_A) {
        ui->model.draft_autonomy_policy.daily_buffer_minutes = OPTIONS[ui->model.overlay_selection];
    } else if (down & HidNpadButton_Plus) {
        ui->model.overlay = PTC_UI_OVERLAY_NONE;
        submit_autonomy_policy(ui);
    }
    return;
}

static void handle_eye_care_overlay_input(UiState *ui, u64 down)
{
    uint16_t *minutes = ui->model.overlay_selection == 1
        ? &ui->model.draft_eye_care_policy.play_minutes
        : &ui->model.draft_eye_care_policy.rest_minutes;
    uint16_t maximum = ui->model.overlay_selection == 1 ? 240u : 60u;
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & HidNpadButton_Up) {
        ui->model.overlay_selection = (ui->model.overlay_selection + 2) % 3;
    } else if (down & HidNpadButton_Down) {
        ui->model.overlay_selection = (ui->model.overlay_selection + 1) % 3;
    } else if (down & (HidNpadButton_Left | HidNpadButton_Right | HidNpadButton_ZL | HidNpadButton_ZR)) {
        int delta = (down & (HidNpadButton_ZL | HidNpadButton_ZR)) ? 10 : 1;
        if (down & (HidNpadButton_Left | HidNpadButton_ZL)) delta = -delta;
        if (ui->model.overlay_selection == 0) {
            ui->model.draft_eye_care_policy.enabled = !ui->model.draft_eye_care_policy.enabled;
        } else {
            *minutes = ptc_ui_adjust_minutes(*minutes, delta, 1u, maximum);
        }
    } else if (down & HidNpadButton_A && ui->model.overlay_selection == 0) {
        ui->model.draft_eye_care_policy.enabled = !ui->model.draft_eye_care_policy.enabled;
    } else if (down & HidNpadButton_Plus) {
        if (ui->model.draft_eye_care_policy.enabled && !ui->model.bedtime_official_setting_confirmed) {
            ui->pending_eye_care_save = true;
            submit_bedtime_confirmation(ui);
        } else {
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            submit_eye_care_policy(ui);
        }
    }
    return;
}

static void handle_weekly_bulk_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
        ui->model.overlay_selection = 1 - ui->model.overlay_selection;
    } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
        bool changed = ptc_ui_apply_weekly_bulk(&ui->model, ui->model.overlay_selection == 1);
        bool weekend = ui->model.overlay_selection == 1;
        ui->model.overlay = PTC_UI_OVERLAY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 changed ? (weekend ? ptc_ui_text(PTC_UI_T_THE_SOURCE_RULE_HAS_BEEN_COPIED_TO) : ptc_ui_text(PTC_UI_T_THE_SOURCE_RULE_HAS_BEEN_COPIED_TO_2))
                         : ptc_ui_text(PTC_UI_T_THE_TARGET_DATE_IS_ALREADY_USING_THE));
    }
    return;
}

static void handle_calendar_format_overlay_input(UiState *ui, u64 down)
{
    if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))
        ptc_ui_cancel_overlay(&ui->model);
    return;
}

static void handle_dock_rules_overlay_input(UiState *ui, u64 down)
{
    if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus)) {
        ptc_audio_play(PTC_SE_CANCEL);
        ptc_ui_cancel_overlay(&ui->model);
        ui->model.dock_field_focus = 5;
    }
    return;
}

static void handle_calendar_manager_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) calendar_manager_nav(ui, 5);
    else if (down & HidNpadButton_X) calendar_manager_nav(ui, 3);
    else if (down & HidNpadButton_Y) calendar_manager_nav(ui, 4);
    else if (down & HidNpadButton_L) calendar_manager_select_tab(ui, 0);
    else if (down & HidNpadButton_R) calendar_manager_select_tab(ui, 1);
    else if (down & (HidNpadButton_Left | HidNpadButton_ZL)) calendar_manager_nav(ui, 0);
    else if (down & (HidNpadButton_Right | HidNpadButton_ZR)) calendar_manager_nav(ui, 1);
    else if ((down & HidNpadButton_Up) && ui->model.calendar_manager_selected > 0) {
        --ui->model.calendar_manager_selected;
        ui->model.calendar_manager_page = ui->model.calendar_manager_selected / 6;
        calendar_manager_select_row(ui, ui->model.calendar_manager_selected % 6);
    } else if ((down & HidNpadButton_Down) &&
        ui->model.calendar_manager_selected + 1 < ui->model.calendar_manager_count) {
        ++ui->model.calendar_manager_selected;
        ui->model.calendar_manager_page = ui->model.calendar_manager_selected / 6;
        calendar_manager_select_row(ui, ui->model.calendar_manager_selected % 6);
    } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) calendar_manager_nav(ui, 2);
    return;
}

static void handle_holiday_calendar_overlay_input(UiState *ui, u64 down)
{
    int pages = holiday_calendar_view_pages(&ui->model);
    bool has_previous = ui->model.holiday_calendar_page > 0 || ui->calendar_view_year_index > 0;
    bool has_next = ui->model.holiday_calendar_page + 1 < pages ||
        ui->calendar_view_year_index + 1 < ui->calendar_view_year_count;
    if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
    else if ((down & HidNpadButton_L) && has_previous) {
        ptc_audio_play(PTC_SE_TAB);
        holiday_calendar_view_step(ui, -1);
    } else if ((down & HidNpadButton_R) && has_next) {
        ptc_audio_play(PTC_SE_TAB);
        holiday_calendar_view_step(ui, 1);
    } else if (down & HidNpadButton_Left) {
        do {
            ui->model.overlay_selection = (ui->model.overlay_selection + 2) % 3;
        } while ((ui->model.overlay_selection == 0 && !has_previous) ||
                 (ui->model.overlay_selection == 1 && !has_next));
    } else if (down & HidNpadButton_Right) {
        do {
            ui->model.overlay_selection = (ui->model.overlay_selection + 1) % 3;
        } while ((ui->model.overlay_selection == 0 && !has_previous) ||
                 (ui->model.overlay_selection == 1 && !has_next));
    } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
        if (ui->model.overlay_selection == 0 && has_previous) {
            ptc_audio_play(PTC_SE_TAB);
            holiday_calendar_view_step(ui, -1);
        } else if (ui->model.overlay_selection == 1 && has_next) {
            ptc_audio_play(PTC_SE_TAB);
            holiday_calendar_view_step(ui, 1);
        } else if (ui->model.overlay_selection == 2) ptc_ui_cancel_overlay(&ui->model);
    }
    return;
}

static void handle_holiday_leave_overlay_input(UiState *ui, u64 down)
{
    if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
        ui->model.holiday_leave_selection = 1 - ui->model.holiday_leave_selection;
    } else if (down & (HidNpadButton_B | HidNpadButton_A)) {
        ptc_ui_cancel_overlay(&ui->model);
        ui->pending_parent_page = -1;
        ui->pending_leave_parent = false;
    } else if (down & HidNpadButton_X) {
        ptc_audio_play(PTC_SE_CANCEL);
        discard_holiday_draft(ui);
        ui->model.overlay = PTC_UI_OVERLAY_NONE;
        apply_pending_navigation(ui);
    }
    return;
}

static void handle_weekly_leave_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_Left) {
        ptc_ui_weekly_leave_move(&ui->model, -1);
    } else if (down & HidNpadButton_Right) {
        ptc_ui_weekly_leave_move(&ui->model, 1);
    } else if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
    } else if (down & HidNpadButton_X) {
        ptc_audio_play(PTC_SE_CANCEL);
        memcpy(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week));
        ui->model.weekly_dirty = false;
        ui->model.overlay = PTC_UI_OVERLAY_NONE;
        apply_pending_navigation(ui);
    } else if (down & HidNpadButton_Plus) {
        ui->model.overlay = PTC_UI_OVERLAY_NONE;
        if (ui->model.disable_flag_present) {
            apply_pending_navigation(ui);
        } else {
            save_weekly_from_page(ui);
            if (!ui->waiting && ui->model.overlay != PTC_UI_OVERLAY_CONFIRM) {
                ui->pending_parent_page = -1;
                ui->pending_leave_parent = false;
            }
        }
    } else if (down & HidNpadButton_A) {
        if (ui->model.weekly_leave_selection == 0) {
            handle_overlay_input(ui, HidNpadButton_X);
        } else if (ui->model.weekly_leave_selection == 1) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else {
            handle_overlay_input(ui, HidNpadButton_Plus);
        }
    }
    return;
}

bool handle_plan_overlay_input(UiState *ui, u64 down)
{
    switch (ui->model.overlay) {
    case PTC_UI_OVERLAY_QUICK_ADD:
        handle_quick_add_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_WINDOW:
        handle_bedtime_window_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_SPECIAL:
        handle_bedtime_special_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_BULK:
        handle_bedtime_bulk_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_LEAVE:
        handle_bedtime_leave_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_BEDTIME:
        handle_bedtime_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_SCHEDULED_LEAVE:
        handle_scheduled_leave_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_SCHEDULED:
        handle_scheduled_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_AUTONOMY:
        handle_autonomy_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_EYE_CARE:
        handle_eye_care_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_WEEKLY_BULK:
        handle_weekly_bulk_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_CALENDAR_FORMAT:
        handle_calendar_format_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_DOCK_RULES:
        handle_dock_rules_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_CALENDAR_MANAGER:
        handle_calendar_manager_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_HOLIDAY_CALENDAR:
        handle_holiday_calendar_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_HOLIDAY_LEAVE:
        handle_holiday_leave_overlay_input(ui, down);
        return true;
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
        handle_weekly_leave_overlay_input(ui, down);
        return true;
    default:
        return false;
    }
}
