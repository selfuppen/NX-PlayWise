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

static void apply_bedtime_preset(UiState *ui, int preset)
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
    if (ui->model.overlay == PTC_UI_OVERLAY_NOTICE_DETAILS) {
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
                    "自定义快速加时", "输入 1 到 120 分钟；完成后仍需确认才会提交。",
                    3, 1, 120, ui->model.draft_minutes);
                ui->model.operation = PTC_UI_OPERATION_ADD_TODAY_MINUTES;
            } else {
                char body[192];
                ui->model.draft_minutes = OPTIONS[ui->model.overlay_selection];
                snprintf(body, sizeof(body), "将在今天当前额度上增加 %u 分钟。\n确认前不会修改额度。",
                    (unsigned int)ui->model.draft_minutes);
                open_confirm_overlay(ui, PTC_UI_OPERATION_ADD_TODAY_MINUTES, "确认快速加时", body);
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
            window->enabled = !window->enabled;
            update_bedtime_dirty(ui);
        } else if ((down & HidNpadButton_A) && ui->model.overlay_selection >= 1) {
            open_bedtime_time_editor(ui, ui->model.overlay_selection == 1
                ? PTC_UI_BEDTIME_TIME_START : PTC_UI_BEDTIME_TIME_END);
        } else if (down & HidNpadButton_Y) {
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
            rule->mode = (PtcBedtimeOverrideMode)((rule->mode + 1) % 3);
            if (rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM) rule->window.enabled = true;
            update_bedtime_dirty(ui);
        } else if ((down & HidNpadButton_A) && ui->model.overlay_selection >= 1 &&
                   rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM) {
            open_bedtime_time_editor(ui, ui->model.overlay_selection == 1
                ? PTC_UI_BEDTIME_TIME_START : PTC_UI_BEDTIME_TIME_END);
        } else if ((down & HidNpadButton_Y) && rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM) {
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
            snprintf(ui->model.message, sizeof(ui->model.message), "已复制到%s；保存前仍可放弃。",
                target == 0 ? "周一至周五" : "周六、周日");
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE) {
        if (down & HidNpadButton_B) {
            ui->pending_parent_page = -1;
            ui->pending_leave_parent = false;
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & HidNpadButton_X) {
            discard_bedtime_draft(ui);
            ptc_ui_cancel_overlay(&ui->model);
            apply_pending_navigation(ui);
        } else if ((down & (HidNpadButton_A | HidNpadButton_Plus)) && !ui->model.disable_flag_present) {
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            save_bedtime_from_page(ui);
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
            draft->enabled = !draft->enabled;
        } else if ((down & HidNpadButton_A) &&
                   (ui->model.overlay_selection == 1 || ui->model.overlay_selection == 2)) {
            open_bedtime_time_editor(ui, ui->model.overlay_selection == 1
                ? PTC_UI_BEDTIME_TIME_START : PTC_UI_BEDTIME_TIME_END);
        } else if (down & HidNpadButton_Plus) {
            if (!ptc_bedtime_policy_is_valid(draft)) {
                snprintf(ui->model.message, sizeof(ui->model.message),
                    "就寝窗口必须跨越午夜，开始时间需要晚于结束时间。");
            } else if (draft->enabled && !ui->model.bedtime_official_setting_confirmed) {
                submit_bedtime_confirmation(ui);
            } else {
                submit_bedtime_policy(ui);
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_SCHEDULED_LEAVE) {
        if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
        else if (down & HidNpadButton_A) ptc_ui_discard_scheduled(&ui->model);
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_HOME_DETAILS || ui->model.overlay == PTC_UI_OVERLAY_DAY_DECISION) {
        if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus)) {
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
            draft->rule.mode = ptc_ui_next_rule_mode(draft->rule.mode);
        } else if ((down & HidNpadButton_A) && ui->model.overlay_selection == 0) {
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
        } else if (direction != 0 && ui->model.overlay_selection == 2) {
            int next = (int)duration + direction * step;
            if (next < 1) next = 1;
            if (next > 366) next = 366;
            if ((uint32_t)draft->start_day_index + (uint32_t)next - 1u > UINT16_MAX) {
                next = (int)(UINT16_MAX - draft->start_day_index + 1u);
            }
            draft->end_day_index = (uint16_t)(draft->start_day_index + next - 1);
        } else if (down & HidNpadButton_Plus) {
            if (ui->model.disable_flag_present || !ptc_ui_scheduled_dirty(&ui->model)) {
                return;
            } else if (!ptc_scheduled_override_is_valid(draft)) {
                snprintf(ui->model.message, sizeof(ui->model.message), "临时额度计划无效，请检查 1 到 366 天范围和额度。");
            } else {
                submit_scheduled_override(ui);
            }
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
    if (ui->model.overlay == PTC_UI_OVERLAY_ACTIVITY_HISTORY) {
        if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
        else if (down & (HidNpadButton_L | HidNpadButton_Left))
            ptc_ui_change_activity_history_page(&ui->model, -1);
        else if (down & (HidNpadButton_R | HidNpadButton_Right))
            ptc_ui_change_activity_history_page(&ui->model, 1);
        else if (down & HidNpadButton_X) request_clear_activity_history(ui);
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_REDEMPTION_HISTORY) {
        if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & (HidNpadButton_L | HidNpadButton_Left)) {
            ptc_ui_change_redemption_history_page(&ui->model, -1);
        } else if (down & (HidNpadButton_R | HidNpadButton_Right)) {
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
                snprintf(ui->model.message, sizeof(ui->model.message), "外观主题已切换为%s。",
                         ptc_ui_theme_preference_label(preference));
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message), "主题设置未保存，已恢复原外观；请确认 SD 卡可写。");
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
                     changed ? (weekend ? "已把来源规则复制到周末草稿；请确认后保存。" : "已把来源规则复制到工作日草稿；请确认后保存。")
                             : "目标日期已经使用相同草稿规则，没有产生修改。");
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_ALBUM_MANAGER) {
        if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & HidNpadButton_Y) {
            refresh_album_restriction(ui);
            refresh_recovery_state(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "自制程序菜单高级入口状态已重新检测。");
        } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            ui->model.overlay_selection = 1 - ui->model.overlay_selection;
        } else if (down & HidNpadButton_A) {
            if (ui->model.overlay_selection == 0 && ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_OFF) {
                open_confirm_overlay(ui, PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION, "配置自制程序菜单高级入口？",
                    "当前状态：未配置\n目标状态：高级入口可用\n此功能只改变 hbmenu 启动方式，不提供防篡改保护。将就地备份原文件；重启后，在桌面‘手柄设置’图标上按住 X，再按 A 进入 hbmenu。\n可回到此处恢复原配置。");
            } else if (ui->model.overlay_selection == 1 && ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED) {
                open_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY, "恢复原来的启动方式？",
                    "将按可信备份恢复原配置。卸载或删除 PlayWise 数据前必须完成恢复；保存后需重启主机生效。");
            } else if (ui->model.overlay_selection == 1 && ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_ANOMALY && ui->model.album_backup_valid) {
                char body[320];
                snprintf(body, sizeof(body), "检测到外部修改：%.120s\n继续会放弃这些修改并强制恢复可信备份。请先确认外部修改不再需要。",
                         ui->model.album_restriction_detail[0] ? ui->model.album_restriction_detail : "配置与记录不一致");
                open_danger_confirm_overlay(ui, PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY, "强制恢复可信备份？", body);
            } else if (ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
                snprintf(ui->model.message, sizeof(ui->model.message),
                         "入口已由外部配置且可以使用；PlayWise 没有原始备份，因此不会重复开启或自动恢复。");
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message), "当前状态不允许执行这项操作，请重新检测。");
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_CALENDAR) {
        int pages = (int)((ptc_holiday_calendar_arrangement_count(ptc_holiday_calendar_info()->last_year) + 3u) / 4u);
        if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
        else if ((down & HidNpadButton_L) && ui->model.holiday_calendar_page > 0) --ui->model.holiday_calendar_page;
        else if ((down & HidNpadButton_R) && ui->model.holiday_calendar_page + 1 < pages) ++ui->model.holiday_calendar_page;
        else if (down & HidNpadButton_Left) {
            do {
                ui->model.overlay_selection = (ui->model.overlay_selection + 2) % 3;
            } while ((ui->model.overlay_selection == 0 && ui->model.holiday_calendar_page == 0) ||
                     (ui->model.overlay_selection == 1 && ui->model.holiday_calendar_page + 1 >= pages));
        } else if (down & HidNpadButton_Right) {
            do {
                ui->model.overlay_selection = (ui->model.overlay_selection + 1) % 3;
            } while ((ui->model.overlay_selection == 0 && ui->model.holiday_calendar_page == 0) ||
                     (ui->model.overlay_selection == 1 && ui->model.holiday_calendar_page + 1 >= pages));
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            if (ui->model.overlay_selection == 0 && ui->model.holiday_calendar_page > 0) --ui->model.holiday_calendar_page;
            else if (ui->model.overlay_selection == 1 && ui->model.holiday_calendar_page + 1 < pages) ++ui->model.holiday_calendar_page;
            else if (ui->model.overlay_selection == 2) ptc_ui_cancel_overlay(&ui->model);
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
            ui->model.shortcut_draft_show_hint = !ui->model.shortcut_draft_show_hint;
        } else if (down & HidNpadButton_ZL) {
            ui->model.shortcut_draft_enabled = false;
        } else if (down & HidNpadButton_Plus) {
            if (commit_shortcut_preferences(ui)) {
                ui->model.overlay = PTC_UI_OVERLAY_NONE;
                snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                         ui->model.custom_shortcut_enabled
                            ? "家长区快捷键已确认更新。"
                            : "自定义快捷键已关闭；固定 Minus - 仍然有效。");
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message), "快捷键设置未保存，请确认 SD 卡可写。");
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
            memcpy(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week));
            ui->model.weekly_dirty = false;
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            apply_pending_navigation(ui);
        } else if (down & HidNpadButton_Plus) {
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            if (ui->model.disable_flag_present) {
                apply_pending_navigation(ui);
            } else {
                submit_weekly(ui);
                if (!ui->waiting) {
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
                snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "放弃配对信息修改？");
                snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                         "手工输入和随机生成只修改草稿，尚未保存。");
            } else {
                show_grant_manager(ui, ui->model.credential_kind == 1
                    ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
            }
        } else if (down & (HidNpadButton_Up | HidNpadButton_Left)) {
            ptc_ui_move_overlay_selection(&ui->model, -1, 0);
        } else if (down & (HidNpadButton_Down | HidNpadButton_Right)) {
            ptc_ui_move_overlay_selection(&ui->model, 1, 0);
        } else if (down & HidNpadButton_X) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
            edit_credential_input(ui);
        } else if (down & HidNpadButton_ZR) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_REVEAL;
            reveal_current_credential(ui);
        } else if (down & HidNpadButton_Y) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_RANDOM;
            randomize_credential(ui);
        } else if ((down & HidNpadButton_R) && ui->model.credential_kind == 2) {
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
                snprintf(ui->model.message, sizeof(ui->model.message), "已放弃未保存的配对信息修改。");
            } else {
                ptc_ui_cancel_overlay(&ui->model);
            }
        } else if (down & HidNpadButton_X) {
            show_grant_manager(ui, ui->model.credential_kind == 1
                ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
            snprintf(ui->model.message, sizeof(ui->model.message), "已放弃未保存的配对信息修改。");
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
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
        if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus)) {
            close_code_result(ui);
        }
        return;
    }
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消修改。");
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_NUMPAD ||
        ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
        if (ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR && (down & HidNpadButton_Minus)) {
            ptc_ui_duration_toggle_field(&ui->model);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR && (down & HidNpadButton_L)) {
            ptc_ui_duration_select_field(&ui->model, PTC_UI_DURATION_HOURS);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR && (down & HidNpadButton_R)) {
            ptc_ui_duration_select_field(&ui->model, PTC_UI_DURATION_MINUTES);
        } else if (down & HidNpadButton_Left) {
            ptc_ui_numpad_move(&ui->model, -1, 0);
        } else if (down & HidNpadButton_Right) {
            ptc_ui_numpad_move(&ui->model, 1, 0);
        } else if (down & HidNpadButton_Up) {
            ptc_ui_numpad_move(&ui->model, 0, -1);
        } else if (down & HidNpadButton_Down) {
            ptc_ui_numpad_move(&ui->model, 0, 1);
        } else if (down & HidNpadButton_X) {
            ptc_ui_numpad_backspace(&ui->model);
        } else if (down & HidNpadButton_Y) {
            ptc_ui_numpad_clear(&ui->model);
        } else if (down & HidNpadButton_ZL) {
            ptc_ui_numpad_adjust(&ui->model, -15);
        } else if (down & HidNpadButton_ZR) {
            ptc_ui_numpad_adjust(&ui->model, 15);
        } else if (down & HidNpadButton_A) {
            ptc_ui_numpad_activate(&ui->model);
        } else if (down & HidNpadButton_Plus) {
            PtcUiNumpadPurpose purpose = ui->model.numpad_purpose;
            PtcUiOperation operation = ui->model.operation;
            uint16_t value = 0;
            if (!ptc_ui_numpad_validate(&ui->model, &value)) return;
            accept_numpad(ui);
            if (purpose == PTC_UI_NUMPAD_MINUTES) {
                if (operation == PTC_UI_OPERATION_SET_TODAY_LIMIT &&
                    ptc_ui_today_limit_requires_hold(&ui->model, value)) {
                    char body[192];
                    const char *title;
                    if (!ui->model.played_minutes_available || ui->model.played_minutes < 0) {
                        title = "无法确认是否立即限制";
                        snprintf(body, sizeof(body),
                                 "额度消耗估算不可用；设置总额度 %u 分钟。", (unsigned int)value);
                    } else {
                        title = ui->model.unrestricted_today == 1
                            ? "不限时将改为限时" : "新额度不高于额度消耗估算";
                        snprintf(body, sizeof(body), "额度已耗约 %d 分钟（估算）；设置总额度 %u 分钟。",
                                 ui->model.played_minutes, (unsigned int)value);
                    }
                    open_danger_confirm_overlay(ui, operation, title, body);
                } else if (operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
                    char body[192];
                    snprintf(body, sizeof(body), "将在今天当前额度上增加 %u 分钟。\n确认前不会修改额度。",
                        (unsigned int)value);
                    open_confirm_overlay(ui, operation, "确认快速加时", body);
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
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Down) {
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Right) {
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Left) {
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Y) {
            edit_overlay_minutes(ui);
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            PtcUiOperation operation = ui->model.operation;
            if (operation == PTC_UI_OPERATION_SET_TODAY_LIMIT &&
                ptc_ui_today_limit_requires_hold(&ui->model, ui->model.draft_minutes)) {
                char body[192];
                if (ui->model.unrestricted_today == 1) {
                    if (ui->model.played_minutes_available && ui->model.played_minutes >= 0) {
                        int preview = (int)ui->model.draft_minutes - ui->model.played_minutes;
                        if (preview < 0) preview = 0;
                        snprintf(body, sizeof(body),
                                 "额度已耗 %d 分钟（估算）；新额度 %u 分钟。\n修改后还剩 %d 分钟可玩。",
                                 ui->model.played_minutes, (unsigned int)ui->model.draft_minutes, preview);
                        open_danger_confirm_overlay(ui, operation, "不限时将改为限时", body);
                    } else {
                        snprintf(body, sizeof(body),
                                 "今天当前为不限时；设置 %u 分钟后将恢复限时。\n额度消耗估算不可用，暂时无法估算修改后剩余。",
                                 (unsigned int)ui->model.draft_minutes);
                        open_danger_confirm_overlay(ui, operation, "不限时将改为限时", body);
                    }
                } else if (!ui->model.played_minutes_available || ui->model.played_minutes < 0) {
                    snprintf(body, sizeof(body),
                             "额度消耗估算不可用；设置总额度 %u 分钟。\n暂时无法估算修改后剩余。",
                             (unsigned int)ui->model.draft_minutes);
                    open_danger_confirm_overlay(ui, operation, "无法确认是否立即限制", body);
                } else {
                    snprintf(body, sizeof(body),
                             "额度已耗约 %d 分钟（估算）；设置总额度 %u 分钟。\n调整后可能立即没有可玩时间。",
                             ui->model.played_minutes, (unsigned int)ui->model.draft_minutes);
                    open_danger_confirm_overlay(ui, operation, "新额度不高于额度消耗估算", body);
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
            day->mode = ptc_ui_next_rule_mode(day->mode);
        } else if ((down & HidNpadButton_Up) && day->mode == PTC_RULE_MODE_LIMIT) {
            day->minutes = ptc_ui_adjust_minutes(day->minutes, 15, 1, 1440);
        } else if ((down & HidNpadButton_Down) && day->mode == PTC_RULE_MODE_LIMIT) {
            day->minutes = ptc_ui_adjust_minutes(day->minutes, -15, 1, 1440);
        } else if ((down & HidNpadButton_Y) && day->mode == PTC_RULE_MODE_LIMIT) {
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
        if (down & HidNpadButton_B) {
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
/*
 * Touch dispatch. A tap resolves to the same control geometry the renderer used
 * (ptc_ui_hit_test), then drives the exact action its button shortcut would, so
 * pointer and pad stay in lock-step.
 */
void handle_touch(UiState *ui, int x, int y)
{
    PtcUiHit hit = ptc_ui_hit_test(&ui->model, x, y);
    if (ui->waiting && ui->model.view == PTC_UI_PARENT &&
        ui->model.overlay == PTC_UI_OVERLAY_NONE) {
        snprintf(ui->model.message, sizeof(ui->model.message), "请等待当前操作完成后再修改设置。");
        return;
    }
    switch (hit.kind) {
    case PTC_UI_HIT_CHILD_SUBMIT_CODE:
        if (ui->waiting) {
            snprintf(ui->model.message, sizeof(ui->model.message), "请等待当前操作完成后再提交加时码。");
        } else {
            open_offline_code_input(ui);
        }
        break;
    case PTC_UI_HIT_CHILD_REFRESH:
        submit_status(ui);
        break;
    case PTC_UI_HIT_CHILD_BUFFER:
        if (!ui->waiting && ui->model.daily_buffer_available) {
            submit_transport_empty(ui, "claim_daily_buffer",
                "正在领取今日自主缓冲...", "领取今日自主缓冲失败");
        }
        break;
    case PTC_UI_HIT_CHILD_PARENT:
        enter_parent_area(ui);
        break;
    case PTC_UI_HIT_CHILD_EXIT:
        ui->exit_requested = true;
        break;
    case PTC_UI_HIT_ERROR_RETRY:
        retry_error(ui);
        break;
    case PTC_UI_HIT_ERROR_BACK:
        enter_child_area(ui);
        break;
    case PTC_UI_HIT_SETUP_SHORTCUT_CARD:
        select_setup_shortcut(ui, hit.index);
        break;
    case PTC_UI_HIT_SETUP_PRIMARY:
        setup_primary(ui);
        break;
    case PTC_UI_HIT_SETUP_BACK:
        setup_previous(ui);
        break;
    case PTC_UI_HIT_SETUP_PIN:
        setup_pin(ui);
        break;
    case PTC_UI_HIT_SETUP_CHILD_ZONE:
        ui->model.setup_zone_index = 0;
        break;
    case PTC_UI_HIT_SETUP_PARENT_ZONE:
        ui->model.setup_zone_index = 1;
        break;
    case PTC_UI_HIT_PARENT_PREV_PAGE:
        request_parent_navigation(ui,
            (ui->model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
        break;
    case PTC_UI_HIT_PARENT_NEXT_PAGE:
        request_parent_navigation(ui, (ui->model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
        break;
    case PTC_UI_HIT_PARENT_REFRESH:
        ui->model.parent_footer_focused = true;
        ui->model.parent_footer_selection = 0;
        refresh_disable_flag(ui);
        submit_status(ui);
        break;
    case PTC_UI_HIT_PARENT_STATUS:
        if (!ptc_ui_parent_status_alert_visible(&ui->model)) break;
        ui->model.parent_footer_focused = true;
        ui->model.parent_footer_selection = 1;
        activate_parent_status(ui);
        break;
    case PTC_UI_HIT_PARENT_BACK:
        request_parent_navigation(ui, -1, true);
        break;
    case PTC_UI_HIT_PARENT_TAB:
        request_parent_navigation(ui, hit.index, false);
        break;
    case PTC_UI_HIT_PARENT_CARD:
        if (ui->waiting) {
            snprintf(ui->model.message, sizeof(ui->model.message), "请等待当前操作完成后再执行其他设置。");
        } else {
            ui->model.selected_index = hit.index;
            if (!(ui->model.parent_page == PTC_UI_PARENT_PLAN &&
                  ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) || hit.index >= 3) {
                handle_parent_action(ui);
            }
        }
        break;
    case PTC_UI_HIT_HOLIDAY_CALENDAR:
        ui->model.selected_index = 6;
        handle_parent_action(ui);
        break;
    case PTC_UI_HIT_HOLIDAY_PAGE_ACTION:
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_SUPPORT_EVENT:
        if (hit.index >= 0 && hit.index < ui->model.recent_event_count) {
            ui->model.selected_index = 6 + (ui->model.recent_event_count - 1 - hit.index);
            ui->model.overlay = PTC_UI_OVERLAY_SUPPORT_EVENT;
            ui->model.overlay_selection = hit.index;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "最近事件详情");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                     "家长区已通过 PIN 验证；这里显示完整诊断字段，但不会显示 PIN、密钥或可复用授权材料。");
        }
        break;
    case PTC_UI_HIT_FORECAST_DAY:
        if (hit.index >= 0 && hit.index < 7 && ui->model.forecast_available) {
            ui->model.selected_index = 5 + hit.index;
            ui->model.forecast_detail_day_offset = hit.index;
            ui->model.overlay = PTC_UI_OVERLAY_DAY_DECISION;
            ui->model.overlay_selection = hit.index;
        }
        break;
    case PTC_UI_HIT_HOME_DETAILS:
        ptc_ui_open_home_details(&ui->model);
        break;
    case PTC_UI_HIT_OVERLAY_CANCEL:
        if (ui->model.overlay == PTC_UI_OVERLAY_HOME_DETAILS ||
            ui->model.overlay == PTC_UI_OVERLAY_NOTICE_DETAILS ||
            ui->model.overlay == PTC_UI_OVERLAY_DAY_DECISION) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_REDEMPTION_HISTORY) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_AUTH_ERROR) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL ||
            ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL ||
            ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
                ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_BACK;
            }
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
            close_code_result(ui);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM &&
                   (ui->model.operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
                    ui->model.operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
                    ui->model.operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY)) {
            ui->model.overlay_selection = 0;
            handle_overlay_input(ui, HidNpadButton_A);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE ||
                   ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else {
            ptc_ui_cancel_overlay(&ui->model);
            snprintf(ui->model.message, sizeof(ui->model.message), "已取消修改。");
        }
        break;
    case PTC_UI_HIT_OVERLAY_CONFIRM:
        if (ui->model.overlay == PTC_UI_OVERLAY_REDEMPTION_HISTORY ||
            ui->model.overlay == PTC_UI_OVERLAY_ACTIVITY_HISTORY) {
            handle_overlay_input(ui, HidNpadButton_X);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
            close_code_result(ui);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE ||
            ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE) {
            handle_overlay_input(ui, HidNpadButton_A);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_SUPPORT_EVENT) {
            handle_overlay_input(ui, HidNpadButton_A);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_CALENDAR) {
            handle_overlay_input(ui, HidNpadButton_A);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM &&
            (ui->model.operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
             ui->model.operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
             ui->model.operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY)) {
            ui->model.overlay_selection = 1;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_SAVE;
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            ui->model.overlay_selection = 1;
        }
        if (ui->model.confirm_hold_required) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "为避免误操作，请长按手柄 A 或持续按住触摸确认按钮。");
        } else {
            handle_overlay_input(ui,
                ui->model.overlay == PTC_UI_OVERLAY_NUMPAD ||
                ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR ||
                ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL ||
                ui->model.overlay == PTC_UI_OVERLAY_SHORTCUT_MANAGER ||
                ui->model.overlay == PTC_UI_OVERLAY_WEEKLY_LEAVE ||
                ui->model.overlay == PTC_UI_OVERLAY_SCHEDULED ||
                ui->model.overlay == PTC_UI_OVERLAY_AUTONOMY ||
                ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_WINDOW ||
                ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_SPECIAL ||
                ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_BULK
                    ? HidNpadButton_Plus : HidNpadButton_A);
        }
        break;
    case PTC_UI_HIT_OVERLAY_DISCARD:
        if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE ||
            ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE) {
            handle_overlay_input(ui, HidNpadButton_X);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            ui->model.overlay_selection = 0;
        }
        handle_overlay_input(ui, HidNpadButton_X);
        break;
    case PTC_UI_HIT_HISTORY_PREV:
        handle_overlay_input(ui, HidNpadButton_Left);
        break;
    case PTC_UI_HIT_HISTORY_NEXT:
        handle_overlay_input(ui, HidNpadButton_Right);
        break;
    case PTC_UI_HIT_SCHEDULED_FIELD:
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_AUTONOMY_OPTION:
        ui->model.overlay_selection = hit.index;
        ui->model.draft_autonomy_policy.daily_buffer_minutes = (uint16_t)(hit.index * 5);
        break;
    case PTC_UI_HIT_QUICK_ADD_OPTION:
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_BEDTIME_SECTION:
        select_bedtime_section(ui, hit.index);
        break;
    case PTC_UI_HIT_BEDTIME_MASTER_SWITCH:
        if (!ui->model.disable_flag_present) {
            ui->model.draft_bedtime_policy.enabled = !ui->model.draft_bedtime_policy.enabled;
            update_bedtime_dirty(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "就寝管控总闸已%s；保存后生效。",
                     ui->model.draft_bedtime_policy.enabled ? "接通" : "切断");
        }
        break;
    case PTC_UI_HIT_BEDTIME_FIELD:
        ui->model.bedtime_section_focused = false;
        ui->model.selected_index = hit.index;
        if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
            ui->model.plan_page == PTC_UI_PLAN_PAGE_BEDTIME) {
            if (ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY && hit.index < 7) {
                int day = ptc_ui_weekday_for_display_slot(hit.index);
                ui->model.bedtime_editor_day = day;
                open_bedtime_window_editor(ui, day);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY &&
                       (hit.index == 7 || hit.index == 8)) {
                ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_BULK;
                ui->model.overlay_selection = hit.index - 7;
                snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "复制每周就寝窗口");
                snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                    "把最后编辑日期的完整开关和时间复制到所选日期组。");
            } else if ((ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY && hit.index == 9) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index == 3) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 4)) {
                discard_bedtime_draft(ui);
            } else if ((ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY && hit.index == 10) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index == 4) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 5)) {
                save_bedtime_from_page(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index == 0) {
                ui->model.draft_bedtime_policy.calendar_enabled =
                    !ui->model.draft_bedtime_policy.calendar_enabled;
                update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index <= 2) {
                open_bedtime_special_editor(ui, hit.index - 1);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 0) {
                ui->model.draft_bedtime_policy.scheduled_override.present =
                    !ui->model.draft_bedtime_policy.scheduled_override.present;
                update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 1) {
                if (edit_date_range_start(ui,
                        &ui->model.draft_bedtime_policy.scheduled_override.start_day_index,
                        &ui->model.draft_bedtime_policy.scheduled_override.end_day_index))
                    update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 2) {
                if (edit_date_range_span(ui,
                        ui->model.draft_bedtime_policy.scheduled_override.start_day_index,
                        &ui->model.draft_bedtime_policy.scheduled_override.end_day_index))
                    update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 3) {
                open_bedtime_special_editor(ui, 2);
            }
        }
        break;
    case PTC_UI_HIT_BEDTIME_OVERLAY_FIELD:
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_BEDTIME_PRESET:
        apply_bedtime_preset(ui, hit.index);
        break;
    case PTC_UI_HIT_MINUTES_INC:
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_DEC:
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_INC_LARGE:
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_DEC_LARGE:
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_VALUE:
        edit_overlay_minutes(ui);
        break;
    case PTC_UI_HIT_WEEKLY_DAY:
        ui->model.editor_index = hit.index;
        for (int slot = 0; slot < 7; ++slot) {
            if (ptc_ui_weekday_for_display_slot(slot) == hit.index) {
                ui->model.weekly_grid_slot = slot;
                ui->model.weekly_last_day_slot = slot;
                break;
            }
        }
        ui->model.selected_index = 0;
        break;
    case PTC_UI_HIT_WEEKLY_BULK:
        ui->model.selected_index = 2;
        if (weekly_editing_blocked(ui)) {
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，批量操作暂不可用。");
        } else {
            ui->model.overlay = PTC_UI_OVERLAY_WEEKLY_BULK;
            ui->model.overlay_selection = 0;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "批量快捷操作");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "把最后选中日期的完整草稿规则复制到一组日期。");
        }
        break;
    case PTC_UI_HIT_WEEKLY_BULK_TARGET:
        ui->model.overlay_selection = hit.index;
        break;
    case PTC_UI_HIT_ALBUM_ACTION:
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_ALBUM_REFRESH:
        handle_overlay_input(ui, HidNpadButton_Y);
        break;
    case PTC_UI_HIT_WEEKLY_MODE:
        if (hit.index >= 0 && hit.index < 7) {
            ui->model.editor_index = hit.index;
            for (int slot = 0; slot < 7; ++slot) {
                if (ptc_ui_weekday_for_display_slot(slot) == hit.index) {
                    ui->model.weekly_grid_slot = slot;
                    ui->model.weekly_last_day_slot = slot;
                    break;
                }
            }
        }
        ui->model.selected_index = 0;
        if (weekly_editing_blocked(ui)) break;
        ui->model.draft_week[ui->model.editor_index].mode =
            ptc_ui_next_rule_mode(ui->model.draft_week[ui->model.editor_index].mode);
        update_weekly_dirty(ui);
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            snprintf(ui->model.message, sizeof(ui->model.message), "已恢复此前的每日限额：%u 分钟。",
                     (unsigned int)ui->model.draft_week[ui->model.editor_index].minutes);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_UP:
        if (weekly_editing_blocked(ui)) break;
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 15, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DOWN:
        if (weekly_editing_blocked(ui)) break;
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -15, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DEC:
        if (weekly_editing_blocked(ui)) break;
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -5, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_INC:
        if (weekly_editing_blocked(ui)) break;
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 5, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_INPUT:
        if (hit.index >= 0 && hit.index < 7) {
            ui->model.editor_index = hit.index;
            for (int slot = 0; slot < 7; ++slot) {
                if (ptc_ui_weekday_for_display_slot(slot) == hit.index) {
                    ui->model.weekly_grid_slot = slot;
                    ui->model.weekly_last_day_slot = slot;
                    break;
                }
            }
        }
        ui->model.selected_index = 0;
        if (weekly_editing_blocked(ui)) break;
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            edit_weekly_minutes(ui);
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "当前为不限时模式，请先切换为限时模式。");
        }
        break;
    case PTC_UI_HIT_HOLIDAY_ENABLE:
        ui->model.selected_index = 0;
        if (ui->model.disable_flag_present) {
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，规则暂时只读。");
        } else {
            ui->model.draft_holiday_enabled = !ui->model.draft_holiday_enabled;
            update_holiday_dirty(ui);
        }
        break;
    case PTC_UI_HIT_HOLIDAY_MODE:
        ui->model.selected_index = hit.index + 1;
        ui->model.holiday_last_rule = hit.index;
        if (ui->model.disable_flag_present) {
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，规则暂时只读。");
        } else if (hit.index == 0) {
            ui->model.draft_holiday_rule.mode = ptc_ui_next_rule_mode(ui->model.draft_holiday_rule.mode);
            update_holiday_dirty(ui);
        } else if (hit.index == 1) {
            ui->model.draft_makeup_workday_rule.mode = ptc_ui_next_rule_mode(ui->model.draft_makeup_workday_rule.mode);
            update_holiday_dirty(ui);
        }
        break;
    case PTC_UI_HIT_HOLIDAY_MINUTES:
        ui->model.selected_index = hit.index + 1;
        ui->model.holiday_last_rule = hit.index;
        if (ui->model.disable_flag_present) {
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，规则暂时只读。");
        } else {
            handle_parent_action(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_SAVE:
        ui->model.selected_index = 4;
        save_weekly_from_page(ui);
        break;
    case PTC_UI_HIT_WEEKLY_DISCARD:
        ui->model.selected_index = 3;
        if (ui->model.weekly_dirty) {
            memcpy(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week));
            ui->model.weekly_dirty = false;
            snprintf(ui->model.message, sizeof(ui->model.message), "已放弃未保存的周计划修改。");
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message), "周计划没有修改。");
        }
        break;
    case PTC_UI_HIT_CREDENTIAL_INPUT:
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
        handle_overlay_input(ui, HidNpadButton_X);
        break;
    case PTC_UI_HIT_CREDENTIAL_RANDOM:
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_RANDOM;
        handle_overlay_input(ui, HidNpadButton_Y);
        break;
    case PTC_UI_HIT_CREDENTIAL_REVEAL:
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_REVEAL;
        handle_overlay_input(ui, HidNpadButton_ZR);
        break;
    case PTC_UI_HIT_CREDENTIAL_DEMO:
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_DEMO;
        handle_overlay_input(ui, HidNpadButton_R);
        break;
    case PTC_UI_HIT_GRANT_MANAGER_CARD:
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_GRANT_GENERATE:
        ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
        generate_local_grant_code(ui);
        break;
    case PTC_UI_HIT_SHORTCUT_OPTION:
        select_setup_shortcut(ui, hit.index);
        break;
    case PTC_UI_HIT_SHORTCUT_DISABLE:
        ui->model.shortcut_draft_enabled = false;
        break;
    case PTC_UI_HIT_SHORTCUT_HINT:
        ui->model.shortcut_draft_show_hint = !ui->model.shortcut_draft_show_hint;
        break;
    case PTC_UI_HIT_THEME_OPTION:
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_SETUP_THEME_OPTION:
        ui->model.setup_theme_index = hit.index;
        setup_primary(ui);
        break;
    case PTC_UI_HIT_GRANT_ADJUST:
        ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_ADJUST_FIRST;
        edit_grant_minutes(ui);
        break;
    case PTC_UI_HIT_NUMPAD_KEY:
        ui->model.numpad_cursor = hit.index;
        ptc_ui_numpad_activate(&ui->model);
        break;
    case PTC_UI_HIT_NUMPAD_QUICK:
        if (hit.index >= 0 && hit.index < 2) {
            static const int DELTAS[] = {-15, 15};
            ptc_ui_numpad_adjust(&ui->model, DELTAS[hit.index]);
        }
        break;
    case PTC_UI_HIT_DURATION_FIELD:
        ptc_ui_duration_select_field(&ui->model, (PtcUiDurationField)hit.index);
        break;
    case PTC_UI_HIT_NOTICE_DETAILS:
        ptc_ui_open_notice_details(&ui->model);
        break;
    case PTC_UI_HIT_NONE:
    default:
        break;
    }
}
