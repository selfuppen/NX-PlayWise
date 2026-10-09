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

void handle_overlay_input(UiState *ui, u64 down)
{
    if (ui->model.overlay == PTC_UI_OVERLAY_CONFIG_BACKUP) {
        int sel = ui->model.overlay_selection;
        if (down & HidNpadButton_B) {
            ptc_audio_play(PTC_SE_CANCEL);
            config_backup_action(ui, 15);
        } else if (down & HidNpadButton_Y) {
            ptc_audio_play(PTC_SE_CONFIRM);
            open_config_backup(ui);
        } else if (down & (HidNpadButton_L | HidNpadButton_ZL)) {
            ptc_audio_play(PTC_SE_FOCUS);
            ui->model.config_backup_tab = 0;
            if (sel != 15 && sel != 16 && sel != 17) ui->model.overlay_selection = 13;
        } else if (down & (HidNpadButton_R | HidNpadButton_ZR)) {
            ptc_audio_play(PTC_SE_FOCUS);
            ui->model.config_backup_tab = 1;
            if (sel != 15 && sel != 16 && sel != 17) ui->model.overlay_selection = 14;
        } else if (down & HidNpadButton_Up) {
            ptc_audio_play(PTC_SE_FOCUS);
            if (ui->model.config_backup_tab == 0) {
                if (sel == 16 || sel == 17) ui->model.overlay_selection = 15;
                else if (sel == 13) ui->model.overlay_selection = 16;
                else if (sel == 15) ui->model.overlay_selection = 13;
                else ui->model.overlay_selection = 13;
            } else {
                if (sel == 16 || sel == 17) ui->model.overlay_selection = 15;
                else if (sel == 0) ui->model.overlay_selection = 16;
                else if (sel == 1) ui->model.overlay_selection = 17;
                else if (sel == 2) ui->model.overlay_selection = 0;
                else if (sel == 3) ui->model.overlay_selection = 1;
                else if (sel == 4) ui->model.overlay_selection = 2;
                else if (sel == 5) ui->model.overlay_selection = 3;
                else if (sel == 6) ui->model.overlay_selection = 4;
                else if (sel == 7) ui->model.overlay_selection = 5;
                else if (sel == 8) ui->model.overlay_selection = 6;
                else if (sel == 9) ui->model.overlay_selection = 8;
                else if (sel == 10) ui->model.overlay_selection = 7;
                else if (sel == 11) ui->model.overlay_selection = 9;
                else if (sel == 12) ui->model.overlay_selection = 10;
                else if (sel == 14) ui->model.overlay_selection = 11;
                else if (sel == 15) ui->model.overlay_selection = 14;
                else ui->model.overlay_selection = 14;
            }
        } else if (down & HidNpadButton_Down) {
            ptc_audio_play(PTC_SE_FOCUS);
            if (ui->model.config_backup_tab == 0) {
                if (sel == 16 || sel == 17) ui->model.overlay_selection = 13;
                else if (sel == 13) ui->model.overlay_selection = 15;
                else if (sel == 15) ui->model.overlay_selection = 16;
                else ui->model.overlay_selection = 13;
            } else {
                if (sel == 16) ui->model.overlay_selection = 0;
                else if (sel == 17) ui->model.overlay_selection = 1;
                else if (sel == 0) ui->model.overlay_selection = 2;
                else if (sel == 1) ui->model.overlay_selection = 3;
                else if (sel == 2) ui->model.overlay_selection = 4;
                else if (sel == 3) ui->model.overlay_selection = 5;
                else if (sel == 4) ui->model.overlay_selection = 6;
                else if (sel == 5) ui->model.overlay_selection = 7;
                else if (sel == 6) ui->model.overlay_selection = 8;
                else if (sel == 7) ui->model.overlay_selection = 10;
                else if (sel == 8) ui->model.overlay_selection = 9;
                else if (sel == 9) ui->model.overlay_selection = 11;
                else if (sel == 10) ui->model.overlay_selection = 12;
                else if (sel == 11 || sel == 12) ui->model.overlay_selection = 14;
                else if (sel == 14) ui->model.overlay_selection = 15;
                else if (sel == 15) ui->model.overlay_selection = 16;
                else ui->model.overlay_selection = 14;
            }
        } else if (down & HidNpadButton_Left) {
            ptc_audio_play(PTC_SE_FOCUS);
            if (sel == 17) ui->model.overlay_selection = 16;
            else if (ui->model.config_backup_tab == 1) {
                if (sel == 1) ui->model.overlay_selection = 0;
                else if (sel == 3) ui->model.overlay_selection = 2;
                else if (sel == 5) ui->model.overlay_selection = 4;
                else if (sel == 7) ui->model.overlay_selection = 6;
                else if (sel == 10) ui->model.overlay_selection = 9;
                else if (sel == 12) ui->model.overlay_selection = 11;
                else if (sel == 14) ui->model.overlay_selection = 10;
            }
        } else if (down & HidNpadButton_Right) {
            ptc_audio_play(PTC_SE_FOCUS);
            if (sel == 16) ui->model.overlay_selection = 17;
            else if (ui->model.config_backup_tab == 1) {
                if (sel == 0) ui->model.overlay_selection = 1;
                else if (sel == 2) ui->model.overlay_selection = 3;
                else if (sel == 4) ui->model.overlay_selection = 5;
                else if (sel == 6) ui->model.overlay_selection = 7;
                else if (sel == 9) ui->model.overlay_selection = 10;
                else if (sel == 11) ui->model.overlay_selection = 12;
                else if (sel == 1 || sel == 3 || sel == 5 || sel == 7 || sel == 8 || sel == 10 || sel == 12) ui->model.overlay_selection = 14;
            }
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            ptc_audio_play(PTC_SE_CONFIRM);
            if (sel == 16) {
                ui->model.config_backup_tab = 0;
                ui->model.overlay_selection = 13;
            } else if (sel == 17) {
                ui->model.config_backup_tab = 1;
                ui->model.overlay_selection = 14;
            } else {
                config_backup_action(ui, sel);
            }
        }
        return;
    }
    if (down & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right)) {
        ptc_audio_play(PTC_SE_FOCUS);
    } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
        ptc_audio_play(PTC_SE_CONFIRM);
    } else if (down & HidNpadButton_B) {
        ptc_audio_play(PTC_SE_CANCEL);
    }

    if (ui->model.overlay == PTC_UI_OVERLAY_SUPPORT_GUIDE) {
        if (down & (HidNpadButton_B | HidNpadButton_A)) ptc_ui_cancel_overlay(&ui->model);
        else if (down & (HidNpadButton_L | HidNpadButton_Left)) ptc_ui_support_guide_change_page(&ui->model, -1);
        else if (down & (HidNpadButton_R | HidNpadButton_Right)) ptc_ui_support_guide_change_page(&ui->model, 1);
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_NOTICE_DETAILS ||
        ui->model.overlay == PTC_UI_OVERLAY_SETUP_PCTL_HELP) {
        if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Minus)) {
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            ui->model.overlay_title[0] = '\0';
            ui->model.overlay_body[0] = '\0';
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_QUICK_ADD) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_WINDOW) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_SPECIAL) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_BULK) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_BEDTIME) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_SCHEDULED_LEAVE) {
        if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
        else if (down & HidNpadButton_A) ptc_ui_discard_scheduled(&ui->model);
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_HOME_DETAILS) {
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
            ptc_audio_play(PTC_SE_CANCEL);
            ptc_ui_home_details_back(&ui->model);
        } else if (down & HidNpadButton_Y) {
            if (!ui->waiting) submit_status(ui);
        } else if (down & (HidNpadButton_ZL | HidNpadButton_ZR)) {
            ptc_ui_home_details_scroll(&ui->model, down & HidNpadButton_ZR ? 200 : -200);
        } else if (down & (HidNpadButton_Up | HidNpadButton_Left)) {
            ptc_ui_home_details_move(&ui->model, -1);
        } else if (down & (HidNpadButton_Down | HidNpadButton_Right)) {
            ptc_ui_home_details_move(&ui->model, 1);
        } else if (down & HidNpadButton_A) {
            int focus = ui->model.home_details_focus;
            if (focus == 2) ptc_ui_home_details_back(&ui->model);
            else if (focus == 1) { if (!ui->waiting) submit_status(ui); }
            else if (focus == 0) { ptc_audio_play(PTC_SE_POPUP); ptc_ui_home_details_activate(&ui->model, 0); }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_DAY_DECISION) {
        if (down & (HidNpadButton_Left | HidNpadButton_L | HidNpadButton_ZL)) {
            if (ui->model.forecast_detail_day_offset > 0) {
                ui->model.forecast_detail_day_offset--;
                ui->model.overlay_selection = ui->model.forecast_detail_day_offset;
                ptc_audio_play(PTC_SE_POPUP);
            }
        } else if (down & (HidNpadButton_Right | HidNpadButton_R | HidNpadButton_ZR)) {
            if (ui->model.forecast_detail_day_offset < (int)PTC_RESULT_FORECAST_DAYS - 1) {
                ui->model.forecast_detail_day_offset++;
                ui->model.overlay_selection = ui->model.forecast_detail_day_offset;
                ptc_audio_play(PTC_SE_POPUP);
            }
        } else if (down & HidNpadButton_Y) {
            if (!ui->waiting) submit_status(ui);
        } else if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus)) {
            ptc_audio_play(PTC_SE_CANCEL);
            ptc_ui_cancel_overlay(&ui->model);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_SCHEDULED) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_AUTONOMY) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_EYE_CARE) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_ACTIVITY_HISTORY) {
        if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
        else if (down & (HidNpadButton_L | HidNpadButton_Left)) {
            ptc_audio_play(PTC_SE_TAB);
            ptc_ui_change_activity_history_page(&ui->model, -1);
        } else if (down & (HidNpadButton_R | HidNpadButton_Right)) {
            ptc_audio_play(PTC_SE_TAB);
            ptc_ui_change_activity_history_page(&ui->model, 1);
        } else if (down & HidNpadButton_X) {
            request_clear_activity_history(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_REDEMPTION_HISTORY) {
        if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & (HidNpadButton_L | HidNpadButton_Left)) {
            ptc_audio_play(PTC_SE_TAB);
            ptc_ui_change_redemption_history_page(&ui->model, -1);
        } else if (down & (HidNpadButton_R | HidNpadButton_Right)) {
            ptc_audio_play(PTC_SE_TAB);
            ptc_ui_change_redemption_history_page(&ui->model, 1);
        } else if (down & HidNpadButton_X) {
            request_clear_redemption_history(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_THEME) {
        if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & HidNpadButton_Left) {
            ui->model.overlay_selection = ui->model.overlay_selection <= 0 ? 2 : ui->model.overlay_selection - 1;
        } else if (down & HidNpadButton_Right) {
            ui->model.overlay_selection = (ui->model.overlay_selection + 1) % 3;
        } else if (down & HidNpadButton_A) {
            PtcUiThemePreference preference = (PtcUiThemePreference)ui->model.overlay_selection;
            if (apply_theme_preference(ui, preference)) {
                ptc_ui_cancel_overlay(&ui->model);
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THEME_CHANGED_TO_S),
                         ptc_ui_theme_preference_label(preference));
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_THEME_SETTINGS_ARE_NOT_SAVED_AND));
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_LANGUAGE) {
        if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            int col = ui->model.overlay_selection % 2;
            int row = ui->model.overlay_selection / 2;
            col = (col + 1) % 2;
            ui->model.overlay_selection = row * 2 + col;
        } else if (down & (HidNpadButton_Up | HidNpadButton_Down)) {
            int col = ui->model.overlay_selection % 2;
            int row = ui->model.overlay_selection / 2;
            row = (row + 1) % 2;
            ui->model.overlay_selection = row * 2 + col;
        } else if (down & HidNpadButton_A) {
            PtcUiLanguagePreference preference = (PtcUiLanguagePreference)ui->model.overlay_selection;
            if (apply_language_preference(ui, preference)) {
                ptc_ui_cancel_overlay(&ui->model);
                char localized_label[64];
                const char *label = ptc_ui_localize(ptc_ui_language_preference_label(preference),
                    localized_label, sizeof(localized_label));
                PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("language", label)};
                (void)ptc_ui_text_format(PTC_UI_T_LANGUAGE_CHANGED, ui->model.message,
                    sizeof(ui->model.message), args, 1);
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                    ptc_ui_text(PTC_UI_T_LANGUAGE_SAVE_FAILED));
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_WEEKLY_BULK) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_ALBUM_MANAGER) {
        if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & HidNpadButton_Y) {
            ptc_audio_play(PTC_SE_CONFIRM);
            refresh_album_restriction(ui);
            refresh_recovery_state(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_HOMEBREW_MENU_ADVANCED_ENTRY_STATUS_HAS_BEEN));
        } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            ui->model.overlay_selection = 1 - ui->model.overlay_selection;
        } else if (down & HidNpadButton_A) {
            if (ui->model.overlay_selection == 0 && ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_OFF) {
                open_confirm_overlay(ui, PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION, ptc_ui_text(PTC_UI_T_CONFIGURE_ADVANCED_ENTRY_TO_HOMEBREW_MENU),
                    ptc_ui_text(PTC_UI_T_CURRENT_NOT_CONFIGURED_TARGET_ADVANCED_ENTRY_AVAILABLE));
            } else if (ui->model.overlay_selection == 1 && ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED) {
                open_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY, ptc_ui_text(PTC_UI_T_RESTORE_THE_ORIGINAL_STARTUP_MODE_2),
                    ptc_ui_text(PTC_UI_T_WILL_RESTORE_THE_ORIGINAL_CONFIGURATION_ACCORDING_TO));
            } else if (ui->model.overlay_selection == 1 && ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_ANOMALY && ui->model.album_backup_valid) {
                char body[320];
                snprintf(body, sizeof(body), ptc_ui_text(PTC_UI_T_EXTERNAL_CHANGES_DETECTED_120S_CONTINUING_DISCARDS_THEM),
                         ui->model.album_restriction_detail[0] ? ui->model.album_restriction_detail : ptc_ui_text(PTC_UI_T_CONFIGURATION_IS_INCONSISTENT_WITH_RECORDS));
                open_danger_confirm_overlay(ui, PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY, ptc_ui_text(PTC_UI_T_FORCE_RESTORE_OF_TRUSTED_BACKUP), body);
            } else if (ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
                snprintf(ui->model.message, sizeof(ui->model.message),
                         ptc_ui_text(PTC_UI_T_THE_PORTAL_HAS_BEEN_CONFIGURED_EXTERNALLY_AND));
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_CURRENT_STATUS_DOES_NOT_ALLOW_THIS));
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CALENDAR_FORMAT) {
        if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))
            ptc_ui_cancel_overlay(&ui->model);
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_DOCK_RULES) {
        if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus)) {
            ptc_audio_play(PTC_SE_CANCEL);
            ptc_ui_cancel_overlay(&ui->model);
            ui->model.dock_field_focus = 5;
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CALENDAR_MANAGER) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_CALENDAR) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_SOFTWARE_INFO) {
        if ((down & (HidNpadButton_A | HidNpadButton_Plus)) &&
            ui->model.hot_reload_status == PTC_UI_HOT_RELOAD_PENDING) {
#ifndef PLAYWISE_EDEN
            open_hot_reload_confirmation(ui);
#endif
        } else if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus)) {
            ptc_ui_cancel_overlay(&ui->model);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_SUPPORT_EVENT) {
        if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus)) {
            ptc_ui_cancel_overlay(&ui->model);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_AUTH_ERROR) {
        if (down & HidNpadButton_B) {
            close_auth_error(ui, true);
        } else if ((down & (HidNpadButton_A | HidNpadButton_Plus)) &&
                   ui->model.auth_cooldown_seconds <= 0) {
            AuthRetryAction action = ui->auth_retry_action;
            close_auth_error(ui, false);
            ui->auth_retry_action = AUTH_RETRY_NONE;
            dispatch_auth_retry(ui, action);
        } else if (down & HidNpadButton_X) {
            close_auth_error(ui, true);
            ui->model.overlay = PTC_UI_OVERLAY_SUPPORT_GUIDE;
            ui->model.support_guide_page = 0;
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_SHORTCUT_MANAGER) {
        if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
            refresh_custom_shortcut_label(ui);
        } else if (down & HidNpadButton_Up) {
            ui->model.setup_shortcut_index = ui->model.setup_shortcut_index <= 0
                ? PTC_UI_SHORTCUT_PRESET_COUNT - 1 : ui->model.setup_shortcut_index - 1;
        } else if (down & HidNpadButton_Down) {
            ui->model.setup_shortcut_index = (ui->model.setup_shortcut_index + 1) % PTC_UI_SHORTCUT_PRESET_COUNT;
        } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            ui->model.setup_shortcut_index = (ui->model.setup_shortcut_index + 7) % PTC_UI_SHORTCUT_PRESET_COUNT;
        } else if (down & HidNpadButton_A) {
            select_setup_shortcut(ui, ui->model.setup_shortcut_index);
        } else if (down & HidNpadButton_Y) {
            ptc_audio_play(PTC_SE_TOGGLE);
            ui->model.shortcut_draft_show_hint = !ui->model.shortcut_draft_show_hint;
        } else if (down & HidNpadButton_ZL) {
            ptc_audio_play(PTC_SE_TOGGLE);
            ui->model.shortcut_draft_enabled = false;
        } else if (down & HidNpadButton_Plus) {
            if (commit_shortcut_preferences(ui)) {
                ui->model.overlay = PTC_UI_OVERLAY_NONE;
                snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                         ui->model.custom_shortcut_enabled
                            ? ptc_ui_text(PTC_UI_T_THE_PARENT_AREA_SHORTCUT_KEYS_HAVE_BEEN)
                            : ptc_ui_text(PTC_UI_T_CUSTOM_SHORTCUT_KEYS_TURNED_OFF_FIXED_MINUS));
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_SHORTCUT_KEY_SETTINGS_ARE_NOT_SAVED));
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_WEEKLY_LEAVE) {
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
    if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL) {
        if (down & HidNpadButton_B) {
            if (strcmp(ui->model.credential_current, ui->model.credential_new) != 0) {
                ui->model.overlay = PTC_UI_OVERLAY_CREDENTIAL_LEAVE;
                ui->model.overlay_selection = 1;
                snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_GIVE_UP_MODIFYING_PAIRING_INFORMATION));
                snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                         ptc_ui_text(PTC_UI_T_MANUAL_INPUT_AND_RANDOM_GENERATION_ONLY_MODIFY));
            } else {
                show_grant_manager(ui, ui->model.credential_kind == 1
                    ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
            }
        } else if (down & (HidNpadButton_Up | HidNpadButton_Left)) {
            ptc_ui_move_overlay_selection(&ui->model, -1, 0);
        } else if (down & (HidNpadButton_Down | HidNpadButton_Right)) {
            ptc_ui_move_overlay_selection(&ui->model, 1, 0);
        } else if (down & HidNpadButton_X) {
            ptc_audio_play(PTC_SE_KEYSTROKE);
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
            edit_credential_input(ui);
        } else if (down & HidNpadButton_ZR) {
            ptc_audio_play(PTC_SE_TOGGLE);
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_REVEAL;
            reveal_current_credential(ui);
        } else if (down & HidNpadButton_Y) {
            ptc_audio_play(PTC_SE_CONFIRM);
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_RANDOM;
            randomize_credential(ui);
        } else if ((down & HidNpadButton_R) && ui->model.credential_kind == 2) {
            ptc_audio_play(PTC_SE_CONFIRM);
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_DEMO;
            if (ptc_grant_secret_is_demo(ui->model.credential_current)) randomize_credential(ui);
            else snprintf(ui->model.credential_new, sizeof(ui->model.credential_new), "%s", PTC_DEMO_GRANT_SECRET);
            ui->model.credential_new_revealed = true;
        } else if (down & HidNpadButton_A) {
            if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_RANDOM) randomize_credential(ui);
            else if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_REVEAL && ui->model.credential_kind == 2) {
                handle_overlay_input(ui, HidNpadButton_ZR);
            } else if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_DEMO && ui->model.credential_kind == 2) {
                handle_overlay_input(ui, HidNpadButton_R);
            } else if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_SAVE) request_save_credential(ui);
            else edit_credential_input(ui);
        } else if (down & HidNpadButton_Plus) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_SAVE;
            request_save_credential(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
        if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            ui->model.overlay_selection = ui->model.overlay_selection == 0 ? 1 : 0;
        } else if (down & (HidNpadButton_B | HidNpadButton_A)) {
            if ((down & HidNpadButton_A) && ui->model.overlay_selection == 0) {
                show_grant_manager(ui, ui->model.credential_kind == 1
                    ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
                snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_UNSAVED_PAIRING_INFORMATION_MODIFICATIONS_WERE_ABANDONED));
            } else {
                ptc_ui_cancel_overlay(&ui->model);
            }
        } else if (down & HidNpadButton_X) {
            ptc_audio_play(PTC_SE_CANCEL);
            show_grant_manager(ui, ui->model.credential_kind == 1
                ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
            snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_UNSAVED_PAIRING_INFORMATION_MODIFICATIONS_WERE_ABANDONED));
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_MANAGER) {
        if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
        else if (down & HidNpadButton_Left) ptc_ui_move_overlay_selection(&ui->model, -1, 0);
        else if (down & HidNpadButton_Right) ptc_ui_move_overlay_selection(&ui->model, 1, 0);
        else if (down & HidNpadButton_Up) ptc_ui_move_overlay_selection(&ui->model, 0, -1);
        else if (down & HidNpadButton_Down) ptc_ui_move_overlay_selection(&ui->model, 0, 1);
        else if (down & HidNpadButton_A) {
            if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_DEVICE) open_credential_manager(ui, 1);
            else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_SECRET) open_credential_manager(ui, 2);
            else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_EXPORT) export_parent_import(ui);
            else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_EDIT_URL) edit_pairing_base_url(ui);
            else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_RESET_URL) request_reset_pairing_base_url(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
        if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & HidNpadButton_Up) {
            ui->model.overlay_selection = ui->model.overlay_selection == PTC_UI_GRANT_LOCAL_BACK
                ? PTC_UI_GRANT_LOCAL_GENERATE : (ui->model.overlay_selection == PTC_UI_GRANT_LOCAL_GENERATE
                    ? PTC_UI_GRANT_LOCAL_ADJUST_FIRST : PTC_UI_GRANT_LOCAL_BACK);
        } else if (down & HidNpadButton_Down) {
            ui->model.overlay_selection = ui->model.overlay_selection == PTC_UI_GRANT_LOCAL_ADJUST_FIRST
                ? PTC_UI_GRANT_LOCAL_GENERATE : (ui->model.overlay_selection == PTC_UI_GRANT_LOCAL_GENERATE
                    ? PTC_UI_GRANT_LOCAL_BACK : PTC_UI_GRANT_LOCAL_ADJUST_FIRST);
        }
        else if (down & HidNpadButton_Plus) {
            ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
            generate_local_grant_code(ui);
        } else if (down & HidNpadButton_A) {
            int selection = ui->model.overlay_selection;
            if (selection == PTC_UI_GRANT_LOCAL_ADJUST_FIRST) edit_grant_minutes(ui);
            else if (selection == PTC_UI_GRANT_LOCAL_BACK) handle_overlay_input(ui, HidNpadButton_B);
            else generate_local_grant_code(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_QR) {
        if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
        else if (down & HidNpadButton_A) export_parent_import(ui);
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_PARENT_EXPORT_RESULT) {
        if (down & (HidNpadButton_A | HidNpadButton_B)) {
            ui->model.overlay = ui->export_return_overlay;
            ui->model.overlay_selection = PTC_UI_GRANT_MANAGER_EXPORT;
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
        if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus)) {
            close_code_result(ui);
        }
        return;
    }
    if (down & HidNpadButton_B) {
        ui->today_limit_refresh_pending = false;
        ui->today_limit_save_pending = false;
        ptc_ui_cancel_overlay(&ui->model);
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_MODIFICATION_CANCELED));
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_NUMPAD ||
        ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
        bool today_mode = ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR &&
            ui->model.numpad_purpose == PTC_UI_NUMPAD_MINUTES &&
            ui->model.operation == PTC_UI_OPERATION_SET_TODAY_LIMIT;
        if (today_mode && ui->today_limit_refresh_pending) return;
        if (today_mode && (down & HidNpadButton_StickR)) {
            ptc_audio_play(PTC_SE_CONFIRM);
            refresh_today_limit_editor(ui, false);
        } else if (today_mode && (down & (HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_L | HidNpadButton_R))) {
            ui->model.today_limit_unlimited_draft = (down & (HidNpadButton_ZR | HidNpadButton_R)) != 0;
            ptc_audio_play(PTC_SE_TOGGLE);
        } else if (today_mode && ui->model.today_limit_unlimited_draft &&
                   (down & (HidNpadButton_X | HidNpadButton_Left))) {
            ui->model.today_limit_unlimited_draft = false;
            ptc_audio_play(PTC_SE_TOGGLE);
        } else if (today_mode && ui->model.today_limit_unlimited_draft &&
                   (down & (HidNpadButton_A | HidNpadButton_Plus))) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_DISABLE_TODAY_LIMIT,
                ptc_ui_text(PTC_UI_T_MAKE_TODAY_UNLIMITED), ptc_ui_text(PTC_UI_T_THERE_IS_NO_DAILY_QUOTA_LIMIT_TODAY));
        } else if (today_mode && ui->model.today_limit_unlimited_draft) {
            /* Keep the previously entered limited value for a mode switch back. */
        } else if (ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR && (down & HidNpadButton_Minus)) {
            ptc_ui_duration_toggle_field(&ui->model);
            ptc_audio_play(PTC_SE_TOGGLE);
        } else if (down & HidNpadButton_Left) {
            ptc_ui_numpad_move(&ui->model, -1, 0);
        } else if (down & HidNpadButton_Right) {
            ptc_ui_numpad_move(&ui->model, 1, 0);
        } else if (down & HidNpadButton_Up) {
            ptc_ui_numpad_move(&ui->model, 0, -1);
        } else if (down & HidNpadButton_Down) {
            ptc_ui_numpad_move(&ui->model, 0, 1);
        } else if (down & HidNpadButton_X) {
            ptc_audio_play(PTC_SE_KEYSTROKE);
            ptc_ui_numpad_backspace(&ui->model);
        } else if (down & HidNpadButton_Y) {
            ptc_audio_play(PTC_SE_KEYSTROKE);
            ptc_ui_numpad_clear(&ui->model);
        } else if (down & HidNpadButton_A) {
            ptc_audio_play(PTC_SE_KEYSTROKE);
            ptc_ui_numpad_activate(&ui->model);
        } else if (down & HidNpadButton_Plus) {
            PtcUiNumpadPurpose purpose = ui->model.numpad_purpose;
            PtcUiOperation operation = ui->model.operation;
            uint16_t value = 0;
            if (!ptc_ui_numpad_validate(&ui->model, &value)) return;
            if (today_mode) {
                ui->model.draft_minutes = value;
                refresh_today_limit_editor(ui, true);
                return;
            }
            accept_numpad(ui);
            if (purpose == PTC_UI_NUMPAD_MINUTES) {
                if (operation == PTC_UI_OPERATION_SET_TODAY_LIMIT) {
                    if (ptc_ui_limit_minutes_would_restrict(&ui->model, value)) {
                        char body[192];
                        snprintf(body, sizeof(body),
                                 ptc_ui_text(PTC_UI_T_ESTIMATED_USAGE_ABOUT_D_MIN_SET_TOTAL),
                                 ui->model.played_minutes, (unsigned int)value);
                        open_danger_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_THE_NEW_QUOTA_IS_NOT_HIGHER_THAN), body);
                    } else if (!ui->model.played_minutes_available || ui->model.played_minutes < 0) {
                        char body[192];
                        snprintf(body, sizeof(body),
                                 ptc_ui_text(PTC_UI_T_USAGE_ESTIMATE_UNAVAILABLE_SET_TOTAL_QUOTA_U),
                                 (unsigned int)value);
                        open_danger_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_UNABLE_TO_CONFIRM_WHETHER_TO_LIMIT_IMMEDIATELY), body);
                    } else if (ui->model.unrestricted_today == 1) {
                        char body[192];
                        int preview = (int)value - ui->model.played_minutes;
                        if (preview < 0) preview = 0;
                        snprintf(body, sizeof(body),
                                 ptc_ui_text(PTC_UI_T_TODAY_IS_UNLIMITED_A_U_MIN_TOTAL_2),
                                 (unsigned int)value, preview);
                        open_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_UNLIMITED_TIME_WILL_BE_CHANGED_TO_LIMITED_2), body);
                    } else {
                        ui->model.operation = PTC_UI_OPERATION_NONE;
                        submit_minutes(ui, operation, value);
                    }
                } else if (operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
                    char body[192];
                    snprintf(body, sizeof(body), ptc_ui_text(PTC_UI_T_ADD_U_MIN_TO_TODAY_S_CURRENT),
                        (unsigned int)value);
                    open_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_CONFIRM_RAPID_GRANT), body);
                } else {
                    ui->model.operation = PTC_UI_OPERATION_NONE;
                    submit_minutes(ui, operation, value);
                }
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_MINUTES) {
        if (down & HidNpadButton_Up) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Down) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Right) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Left) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Y) {
            ptc_audio_play(PTC_SE_CONFIRM);
            edit_overlay_minutes(ui);
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            PtcUiOperation operation = ui->model.operation;
            if (operation == PTC_UI_OPERATION_SET_TODAY_LIMIT) {
                if (ptc_ui_limit_minutes_would_restrict(&ui->model, ui->model.draft_minutes)) {
                    char body[192];
                    snprintf(body, sizeof(body),
                             ptc_ui_text(PTC_UI_T_ESTIMATED_USAGE_ABOUT_D_MIN_SET_TOTAL_2),
                             ui->model.played_minutes, (unsigned int)ui->model.draft_minutes);
                    open_danger_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_THE_NEW_QUOTA_IS_NOT_HIGHER_THAN), body);
                } else if (!ui->model.played_minutes_available || ui->model.played_minutes < 0) {
                    char body[192];
                    snprintf(body, sizeof(body),
                             ptc_ui_text(PTC_UI_T_USAGE_ESTIMATE_UNAVAILABLE_SET_TOTAL_QUOTA_U),
                             (unsigned int)ui->model.draft_minutes);
                    open_danger_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_UNABLE_TO_CONFIRM_WHETHER_TO_LIMIT_IMMEDIATELY), body);
                } else if (ui->model.unrestricted_today == 1) {
                    char body[192];
                    int preview = (int)ui->model.draft_minutes - ui->model.played_minutes;
                    if (preview < 0) preview = 0;
                    snprintf(body, sizeof(body),
                             ptc_ui_text(PTC_UI_T_ESTIMATED_USAGE_D_MIN_NEW_QUOTA_U),
                             ui->model.played_minutes, (unsigned int)ui->model.draft_minutes, preview);
                    open_confirm_overlay(ui, operation, ptc_ui_text(PTC_UI_T_UNLIMITED_TIME_WILL_BE_CHANGED_TO_LIMITED_2), body);
                } else {
                    ui->model.overlay = PTC_UI_OVERLAY_NONE;
                    ui->model.operation = PTC_UI_OPERATION_NONE;
                    submit_minutes(ui, operation, ui->model.draft_minutes);
                }
            } else {
                ui->model.overlay = PTC_UI_OVERLAY_NONE;
                ui->model.operation = PTC_UI_OPERATION_NONE;
                submit_minutes(ui, operation, ui->model.draft_minutes);
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_WEEKLY) {
        PtcDayRule *day = &ui->model.draft_week[ui->model.editor_index];
        if (down & HidNpadButton_Left) {
            ui->model.editor_index = ui->model.editor_index <= 0 ? 6 : ui->model.editor_index - 1;
        } else if (down & HidNpadButton_Right) {
            ui->model.editor_index = ui->model.editor_index >= 6 ? 0 : ui->model.editor_index + 1;
        } else if ((down & (HidNpadButton_X | HidNpadButton_Up | HidNpadButton_Down |
                           HidNpadButton_Y | HidNpadButton_A | HidNpadButton_Plus)) &&
                    weekly_editing_blocked(ui)) {
            return;
        } else if (down & HidNpadButton_X) {
            ptc_audio_play(PTC_SE_TOGGLE);
            day->mode = ptc_ui_next_rule_mode(day->mode);
        } else if ((down & HidNpadButton_Up) && day->mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            day->minutes = ptc_ui_adjust_minutes(day->minutes, 15, 1, 1440);
        } else if ((down & HidNpadButton_Down) && day->mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            day->minutes = ptc_ui_adjust_minutes(day->minutes, -15, 1, 1440);
        } else if ((down & HidNpadButton_Y) && day->mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_CONFIRM);
            edit_weekly_minutes(ui);
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            save_weekly_from_page(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM) {
        bool album_change = ui->model.operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
                            ui->model.operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
                            ui->model.operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY;
        if ((down & HidNpadButton_Y) &&
            quota_operation_needs_recheck(ui->model.operation)) {
            ptc_audio_play(PTC_SE_CONFIRM);
            start_quota_recheck(ui, true);
        } else if (down & HidNpadButton_B) {
            ui->quota_recheck_ready = false;
            if (ui->model.operation == PTC_UI_OPERATION_SAVE_BEDTIME)
                cancel_bedtime_navigation(ui);
            if (ui->model.operation == PTC_UI_OPERATION_SAVE_WEEKLY) {
                ui->pending_parent_page = -1;
                ui->pending_leave_parent = false;
            }
            ptc_ui_cancel_overlay(&ui->model);
        } else if (album_change && (down & (HidNpadButton_Left | HidNpadButton_Right))) {
            ui->model.overlay_selection = 1 - ui->model.overlay_selection;
        } else if (album_change && (down & HidNpadButton_A) && ui->model.overlay_selection == 0) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if ((down & (HidNpadButton_A | HidNpadButton_Plus)) &&
                   (!album_change || ui->model.overlay_selection == 1)) {
            confirm_operation(ui);
        }
    }
}
