#include "nro_app_internal.h"

static void format_today_label(uint16_t day_index, char *out, size_t out_size)
{
    static const char *WEEKDAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    uint16_t year;
    uint8_t month;
    uint8_t day;
    if (ptc_date_from_day_index(day_index, &year, &month, &day)) {
        snprintf(out, out_size, "%u 月 %u 日（%s，今天）", month, day, WEEKDAYS[ptc_weekday_from_day_index(day_index)]);
    } else {
        snprintf(out, out_size, "今天");
    }
}

static PtcDayRule effective_today_rule(const UiState *ui)
{
    uint8_t weekday = ptc_weekday_from_day_index(ui->model.day_index);
    if (ui->model.today_override_present) {
        return ui->model.today_override_rule;
    }
    if (strcmp(ui->model.rule_source, "statutory_holiday") == 0) {
        return ui->model.holiday_rule;
    }
    if (strcmp(ui->model.rule_source, "makeup_workday") == 0) {
        return ui->model.makeup_workday_rule;
    }
    return ui->model.current_week[weekday];
}

static uint16_t current_today_limit_value(const UiState *ui)
{
    PtcDayRule rule = effective_today_rule(ui);
    uint16_t fallback = 0;
    if (ui->model.unrestricted_today != 1 && rule.mode == PTC_RULE_MODE_LIMIT &&
        rule.minutes >= 1 && rule.minutes <= 1440) {
        fallback = rule.minutes;
    }
    return ptc_ui_today_limit_start_value(&ui->model, fallback);
}

static PtcUiSystemTheme read_system_theme(void)
{
    ColorSetId color_set;
    Result result = setsysInitialize();
    if (R_FAILED(result)) return PTC_UI_SYSTEM_THEME_UNAVAILABLE;
    result = setsysGetColorSetId(&color_set);
    setsysExit();
    if (R_FAILED(result)) return PTC_UI_SYSTEM_THEME_UNAVAILABLE;
    if (color_set == ColorSetId_Light) return PTC_UI_SYSTEM_THEME_LIGHT;
    if (color_set == ColorSetId_Dark) return PTC_UI_SYSTEM_THEME_DARK;
    return PTC_UI_SYSTEM_THEME_UNAVAILABLE;
}

void refresh_theme(UiState *ui)
{
    if (!ui) return;
    ui->system_theme = read_system_theme();
    ui->theme_view = ptc_ui_theme_make_view(ui->theme_preference, ui->system_theme);
    ui->theme_refresh_pending = false;
}

void trigger_resume_status_refresh(UiState *ui, int *background_poll_elapsed_ms)
{
    if (!ui || ui->waiting || ui->recovering_redemption) return;
    ui->status_refresh_pending = false;
    refresh_disable_flag(ui);
    refresh_album_restriction(ui);
    refresh_security_state(ui);
    submit_status(ui);
    if (background_poll_elapsed_ms) {
        *background_poll_elapsed_ms = BACKGROUND_POLL_INTERVAL_MS;
    }
}

void applet_hook(AppletHookType hook, void *param)
{
    UiState *ui = (UiState *)param;
    if (ui && (hook == AppletHookType_OnResume ||
               (hook == AppletHookType_OnFocusState && appletGetFocusState() == AppletFocusState_InFocus))) {
        ui->theme_refresh_pending = true;
        ui->status_refresh_pending = true;
    }
}

bool apply_theme_preference(UiState *ui, PtcUiThemePreference preference)
{
    PtcUiThemePreference previous;
    if (!ui) return false;
    previous = ui->theme_preference;
    ui->theme_preference = preference;
    ui->theme_view = ptc_ui_theme_make_view(preference, ui->system_theme);
    if (save_ui_preferences(ui)) return true;
    ui->theme_preference = previous;
    ui->theme_view = ptc_ui_theme_make_view(previous, ui->system_theme);
    return false;
}

void handle_today_action_ready(UiState *ui, int index)
{
    char date[64];
    char body[320];
    char played[32];
    char remaining[32];
    format_today_label(ui->model.day_index, date, sizeof(date));
    if (ui->model.played_minutes_available) snprintf(played, sizeof(played), "约 %d 分钟", ui->model.played_minutes);
    else snprintf(played, sizeof(played), "暂不可用");
    if (ui->model.unrestricted_today == 1) snprintf(remaining, sizeof(remaining), "不限时");
    else if (ui->model.remaining_available) snprintf(remaining, sizeof(remaining), "%d 分钟", ui->model.remaining_minutes);
    else snprintf(remaining, sizeof(remaining), "暂不可用");
    switch (index) {
    case PTC_UI_OPERATION_SET_TODAY_LIMIT:
        ui->model.operation = PTC_UI_OPERATION_SET_TODAY_LIMIT;
        ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_MINUTES, PTC_UI_OVERLAY_NONE,
            "设置今日总额度",
            !ui->model.played_minutes_available || ui->model.played_minutes < 0
                ? "全天总额度包含今日额度消耗；当前消耗估算未知，设置后可能立即限制。"
                : (ui->model.unrestricted_today == 1
                    ? "全天总额度包含今日额度消耗；保存后今天将从不限时改为限时。"
                    : "全天总额度包含今日额度消耗；右侧显示调整前后的可玩时间。"),
            4, 1, 1440, current_today_limit_value(ui));
        break;
    case PTC_UI_OPERATION_ADD_TODAY_MINUTES:
        if (ui->model.unrestricted_today == 1) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "今天已不限时，无需加时；如需恢复限时，请使用“设置今日总额度”。");
        } else {
            ui->model.operation = PTC_UI_OPERATION_ADD_TODAY_MINUTES;
            ui->model.draft_minutes = 15;
            ui->model.overlay = PTC_UI_OVERLAY_QUICK_ADD;
            ui->model.overlay_selection = 0;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "快速加时");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                "选择常用时长或自定义；确认前不会修改今天的额度。");
        }
        break;
    case PTC_UI_OPERATION_DISABLE_TODAY_LIMIT:
        snprintf(body, sizeof(body), "%s：额度已耗（估算）%s，今天还可玩 %s。\n设置后今天不限时；明天继续使用每周计划。",
                 date, played, remaining);
        open_confirm_overlay(ui, PTC_UI_OPERATION_DISABLE_TODAY_LIMIT, "将今天设为不限时", body);
        break;
    case PTC_UI_OPERATION_RESTORE_TODAY_POLICY: {
        PtcEffectiveRule restored = ptc_ui_rule_after_today_restore(&ui->model);
        char basis[256];
        ptc_ui_format_restore_today_basis(&ui->model, basis, sizeof(basis));
        snprintf(body, sizeof(body), "%s\n%s", date, basis);
        bool requires_hold = restored.rule.mode == PTC_RULE_MODE_LIMIT &&
            (!ui->model.played_minutes_available || ui->model.played_minutes < 0 ||
             (int)restored.rule.minutes - ui->model.played_minutes <= 0);
        if (requires_hold)
            open_danger_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_TODAY_POLICY, "清除今日额度调整", body);
        else
            open_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_TODAY_POLICY, "清除今日额度调整", body);
        break;
    }
    case PTC_UI_OPERATION_SKIP_BEDTIME: {
        uint64_t instance_id = 0;
        uint16_t start_day = 0, start_minute = 0, end_minute = 0;
        char start_date[64];
        if (!ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL))) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                "就寝状态仍待确认，请刷新后重试。");
            break;
        }
        if (ui->model.bedtime_active) {
            if (!ui->model.bedtime_skipped) {
                instance_id = ui->model.bedtime_window_instance_id;
                start_day = ui->model.bedtime_start_day_index;
                start_minute = ui->model.bedtime_start_minute;
                end_minute = ui->model.bedtime_end_minute;
            }
        } else if (ui->model.bedtime_next_available) {
            instance_id = ui->model.bedtime_next_window_instance_id;
            start_day = ui->model.bedtime_next_start_day_index;
            start_minute = ui->model.bedtime_next_start_minute;
            end_minute = ui->model.bedtime_next_end_minute;
        }
        if (instance_id == 0) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                ui->model.bedtime_skipped_window_available
                    ? "最近一次就寝窗口已经跳过。" : "当前没有可跳过的就寝窗口。");
            break;
        }
        ui->model.pending_bedtime_skip_instance_id = instance_id;
        ui->model.pending_bedtime_skip_start_day_index = start_day;
        ui->model.pending_bedtime_skip_start_minute = start_minute;
        ui->model.pending_bedtime_skip_end_minute = end_minute;
        ui->auth_retry_action = AUTH_RETRY_SKIP_BEDTIME;
        if (!verify_sensitive_pin(ui, "跳过最近一次就寝窗口前，请再次输入本应用 PIN")) break;
        format_today_label(start_day, start_date, sizeof(start_date));
        snprintf(body, sizeof(body), "%s %02u:%02u 到次日 %02u:%02u。\n只跳过这一次；后续就寝计划不变。",
            start_date, (unsigned int)(start_minute / 60), (unsigned int)(start_minute % 60),
            (unsigned int)(end_minute / 60), (unsigned int)(end_minute % 60));
        open_confirm_overlay(ui, PTC_UI_OPERATION_SKIP_BEDTIME, "跳过这一次就寝时间？", body);
        break;
    }
    default:
        break;
    }
}

void handle_parent_action(UiState *ui)
{
    int index = ui->model.selected_index;
    if (ui->model.disable_flag_present && ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "紧急停用已开启，此项控制写入不可用；请到支持与恢复解除停用。");
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        if (ui->waiting) return;
        if (index == 3 && ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL)) &&
            !ui->model.today_override_present) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "今天没有可清除的额度调整；当前继续使用下级规则。");
            return;
        }
        if (ptc_ui_today_operation(index) != PTC_UI_OPERATION_NONE) {
            submit_status(ui);
            /* A failed submit must not leave an action for a later auto-refresh. */
            ui->pending_today_action = ui->waiting ? (int)ptc_ui_today_operation(index) : -1;
            if (ui->waiting)
                snprintf(ui->model.message, sizeof(ui->model.message), "正在刷新额度消耗估算和今天还可玩...");
        } else if (index == 5) {
            if (ui->model.daily_buffer_minutes == 0) {
                snprintf(ui->model.message, sizeof(ui->model.message),
                    "自主缓冲当前关闭；可到时间计划中设置。");
            } else if (ui->model.daily_buffer_claimed) {
                snprintf(ui->model.message, sizeof(ui->model.message),
                    "孩子今天已经领取自主缓冲，明天恢复资格。");
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message),
                    "自主缓冲为只读状态；领取操作仍在孩子页。");
            }
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.plan_page == PTC_UI_PLAN_PAGE_ROOT) {
        switch (index) {
        case 0:
            if (!ptc_ui_scheduled_dirty(&ui->model))
                ui->model.draft_scheduled_override = ui->model.scheduled_override;
            if (!ptc_ui_scheduled_dirty(&ui->model) && !ui->model.draft_scheduled_override.enabled) {
                ui->model.draft_scheduled_override.start_day_index = ui->model.day_index;
                ui->model.draft_scheduled_override.end_day_index = ui->model.day_index;
                ui->model.draft_scheduled_override.rule.mode = PTC_RULE_MODE_LIMIT;
                ui->model.draft_scheduled_override.rule.minutes = 60;
            }
            ui->model.overlay = PTC_UI_OVERLAY_SCHEDULED;
            ui->model.overlay_selection = 0;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "临时额度计划");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                "只改每天可玩额度；指定日期就寝只改开始和结束，两者可同时生效。");
            break;
        case 1:
            ui->model.plan_page = PTC_UI_PLAN_PAGE_HOLIDAY;
            ui->model.selected_index = 0;
            break;
        case 2:
            open_weekly_page(ui);
            break;
        case 3:
            ui->model.draft_bedtime_policy = ui->model.bedtime_policy;
            ui->model.bedtime_dirty = false;
            ui->model.bedtime_section = PTC_UI_BEDTIME_WEEKLY;
            ui->model.plan_page = PTC_UI_PLAN_PAGE_BEDTIME;
            ui->model.selected_index = 0;
            break;
        case 4:
            ui->model.draft_autonomy_policy = ui->model.autonomy_policy;
            ui->model.overlay = PTC_UI_OVERLAY_AUTONOMY;
            ui->model.overlay_selection = ui->model.draft_autonomy_policy.daily_buffer_minutes / 5;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "今日自主缓冲");
            ui->model.overlay_body[0] = '\0';
            break;
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
            if (ui->model.forecast_available) {
                ui->model.forecast_detail_day_offset = index - 5;
                ui->model.overlay = PTC_UI_OVERLAY_DAY_DECISION;
                ui->model.overlay_selection = index - 5;
            }
            break;
        default:
            break;
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
        if (ui->model.disable_flag_present && index != 4 && index != 6) {
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，国家节假日设置暂时只读。");
            return;
        }
        switch (index) {
        case 0:
            ui->model.draft_holiday_enabled = !ui->model.draft_holiday_enabled;
            update_holiday_dirty(ui);
            break;
        case 1:
            ui->model.holiday_last_rule = 0;
            if (ui->model.draft_holiday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(ui->model.message, sizeof(ui->model.message), "当前为不限时模式，请先切换为限时模式。");
            } else {
                ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_HOLIDAY_MINUTES, PTC_UI_OVERLAY_NONE,
                    "设置法定休假日额度", "分别输入小时和分钟，总计 1 到 1440 分钟", 4, 1, 1440, ui->model.draft_holiday_rule.minutes);
            }
            break;
        case 2:
            ui->model.holiday_last_rule = 1;
            if (ui->model.draft_makeup_workday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(ui->model.message, sizeof(ui->model.message), "当前为不限时模式，请先切换为限时模式。");
            } else {
                ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_MAKEUP_MINUTES, PTC_UI_OVERLAY_NONE,
                    "设置调休工作日额度", "分别输入小时和分钟，总计 1 到 1440 分钟", 4, 1, 1440,
                    ui->model.draft_makeup_workday_rule.minutes);
            }
            break;
        case 3: {
            PtcDayRule *rule = ui->model.holiday_last_rule == 1
                ? &ui->model.draft_makeup_workday_rule : &ui->model.draft_holiday_rule;
            rule->mode = ptc_ui_next_rule_mode(rule->mode);
            update_holiday_dirty(ui);
            break;
        }
        case 4:
            discard_holiday_draft(ui);
            break;
        case 6: {
            char holiday_rule[48];
            char makeup_rule[48];
            if (ui->model.draft_holiday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(holiday_rule, sizeof(holiday_rule), "不限时");
            } else {
                snprintf(holiday_rule, sizeof(holiday_rule), "%u 分钟",
                         (unsigned int)ui->model.draft_holiday_rule.minutes);
            }
            if (ui->model.draft_makeup_workday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(makeup_rule, sizeof(makeup_rule), "不限时");
            } else {
                snprintf(makeup_rule, sizeof(makeup_rule), "%u 分钟",
                         (unsigned int)ui->model.draft_makeup_workday_rule.minutes);
            }
            ui->model.overlay = PTC_UI_OVERLAY_HOLIDAY_CALENDAR;
            ui->model.holiday_calendar_page = 0;
            ui->model.overlay_selection = 2;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "内置节假日安排");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                     "当前%s  |  法定休假：%s  |  调休工作日：%s",
                     ui->model.draft_holiday_enabled ? "已开启" : "未开启",
                     holiday_rule, makeup_rule);
            break;
        }
        case 5:
            save_holiday_from_page(ui);
            break;
        default:
            break;
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_GRANT) {
        switch (index) {
        case 0: open_local_grant(ui); break;
        case 1: show_pairing_qr(ui); break;
        case 2: open_grant_manager(ui); break;
        case 3: open_redemption_history(ui); break;
        default: break;
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_SETTINGS) {
        switch (index) {
        case 0:
            ui->model.overlay = PTC_UI_OVERLAY_THEME;
            ui->model.overlay_selection = (int)ui->theme_preference;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "外观主题");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                     "仅改变任我玩主机应用的绘制外观；计时、请求和后台控制不会重启或改变。");
            break;
        case 1: change_parent_pin(ui); break;
        case 2: open_shortcut_manager(ui); break;
        case 3:
            refresh_album_restriction(ui);
            ui->model.overlay = PTC_UI_OVERLAY_ALBUM_MANAGER;
            ui->model.overlay_selection = ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_OFF ? 0 : 1;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "自制程序菜单高级入口");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                     "此功能只改变 hbmenu 启动方式，不提供防篡改保护。");
            break;
        case 4:
            open_activity_history(ui);
            break;
        default: break;
        }
        return;
    }
    if (ui->model.parent_page != PTC_UI_PARENT_SUPPORT) return;
    if (ptc_ui_safety_action_available(&ui->model, index) == PTC_UI_ACTION_DISABLED) {
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 ptc_ui_safety_action_hint(&ui->model, index));
        return;
    }
    switch (index) {
    case 0:
        if (ptc_ui_runtime_fingerprint_reconfirmation_needed(&ui->model)) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "系统环境已变化，重新检测并接管",
                                 "系统版本或运行环境与上次确认时不同。将执行只读兼容预检；通过后保留现有配置并恢复额度管理。");
        } else if (ui->model.disable_flag_present) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "解除停用并重新接管",
                                 "重新执行只读兼容预检；仅预检通过后才解除停用并恢复额度管理。");
        } else {
            open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "确认接管系统控制",
                                 "先执行只读兼容预检；通过后保存安装快照并启用额度管理。");
        }
        break;
    case 1:
        open_confirm_overlay(ui, PTC_UI_OPERATION_RETRY_SETUP_RELEASE, "重试修复",
                             "重新执行安全前置检查，并在可恢复时继续首次设置。");
        break;
    case 2:
        refresh_disable_flag(ui);
        if (ui->model.disable_flag_present) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "解除停用并重新接管",
                                 "功能：重新执行只读安全预检，通过后解除 disable.flag 并恢复控制。\n适用：故障已排除且确认当前规则配置安全。");
        } else {
            open_confirm_overlay(ui, PTC_UI_OPERATION_EMERGENCY_DISABLE, "紧急停用控制",
                                 "功能：创建 disable.flag，立即停止正常控制写入。\n适用：异常限制、写入故障或需要保留现场。");
        }
        break;
    case 3:
        open_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT, "恢复安装前状态",
                             "恢复原始家长控制设置与计时器，并停止新的控制写入。");
        break;
    case 4:
        open_confirm_overlay(ui, PTC_UI_OPERATION_EXPORT_DIAGNOSTICS, "导出诊断包？",
            "诊断文件可能包含设备标识和文件路径信息，请只发送给可信的支持人员。\nPIN、加时码密钥和可复用授权材料不会导出。");
        break;
    case 5:
#ifndef PLAYWISE_EDEN
        ptc_hot_reload_inspect(&ui->hot_reload);
        sync_hot_reload_model(ui);
#endif
        ui->model.overlay = PTC_UI_OVERLAY_SOFTWARE_INFO;
        snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "软件信息");
        ui->model.overlay_body[0] = '\0';
        snprintf(ui->model.software_version, sizeof(ui->model.software_version), "%s", PLAYWISE_VERSION);
        snprintf(ui->model.repository_url, sizeof(ui->model.repository_url), "%s", PLAYWISE_REPOSITORY_URL);
        snprintf(ui->model.pwa_url, sizeof(ui->model.pwa_url), "%s", PTC_PAIRING_BASE_URL);
        break;
    default:
        break;
    }
}

void confirm_operation(UiState *ui)
{
    PtcCompanionStatus status;
    PtcUiOverlay return_overlay = ui->model.confirm_return_overlay;
    PtcUiOperation operation = ptc_ui_take_confirmed_operation(&ui->model);
    switch (operation) {
#ifndef PLAYWISE_EDEN
    case PTC_UI_OPERATION_HOT_RELOAD:
        if (ui->waiting || ui->model.recovery_active) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                "当前请求或恢复事务尚未完成，暂不能热加载。");
        } else if (ptc_hot_reload_begin(&ui->hot_reload)) {
            ui->hot_reload_terminal_handled = false;
            ui->waiting = true;
            ui->model.waiting = true;
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            sync_hot_reload_model(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "%s", ui->hot_reload.detail);
        } else {
            sync_hot_reload_model(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                ui->hot_reload.detail[0] ? ui->hot_reload.detail : "热加载前置检查未通过");
        }
        break;
#endif
    case PTC_UI_OPERATION_EXPORT_DIAGNOSTICS:
        export_diagnostics(ui);
        break;
    case PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION:
    case PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY:
    case PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY: {
        char error[160] = {0};
        bool ok = operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION
            ? ptc_album_restriction_enable(ui->client.storage, error, sizeof(error))
            : ptc_album_restriction_restore(ui->client.storage,
                operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY, error, sizeof(error));
        refresh_album_restriction(ui);
        refresh_recovery_state(ui);
        if (return_overlay == PTC_UI_OVERLAY_ALBUM_MANAGER) {
            ui->model.overlay = PTC_UI_OVERLAY_ALBUM_MANAGER;
            ui->model.overlay_selection = ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_OFF ? 0 : 1;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "自制程序菜单高级入口");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "状态已重新检测；配置变更需重启主机后生效。");
        }
        if (ok) {
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                     operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION
                       ? "已开启保护。重启后，在桌面‘手柄设置’图标上按住 X，再按 A，进入自制程序菜单（hbmenu）。"
                       : "已恢复原来的自制程序菜单入口方式，请重启主机后确认。");
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "设置没有更改：%s", error[0] ? error : "请稍后重试");
        }
        break;
    }
    case PTC_UI_OPERATION_SET_TODAY_LIMIT:
        submit_minutes(ui, operation, ui->model.draft_minutes);
        break;
    case PTC_UI_OPERATION_ADD_TODAY_MINUTES:
        submit_minutes(ui, operation, ui->model.draft_minutes);
        break;
    case PTC_UI_OPERATION_SAVE_WEEKLY:
        submit_weekly(ui);
        break;
    case PTC_UI_OPERATION_SAVE_HOLIDAY:
        submit_holiday_policy(ui);
        break;
    case PTC_UI_OPERATION_SAVE_BEDTIME:
        submit_bedtime_policy(ui);
        break;
    case PTC_UI_OPERATION_DISABLE_TODAY_LIMIT:
        submit_transport_empty(ui, "disable_today_limit", "正在解除当前限制...", "解除当前限制失败");
        break;
    case PTC_UI_OPERATION_RESTORE_TODAY_POLICY:
        submit_transport_empty(ui, "restore_today_policy", "正在清除今日额度调整...", "清除今日额度调整失败");
        break;
    case PTC_UI_OPERATION_SKIP_BEDTIME:
        submit_bedtime_skip(ui);
        break;
    case PTC_UI_OPERATION_CLEAR_REDEMPTION_HISTORY:
        submit_transport_empty(ui, "clear_redemption_history", "正在清空加时码使用记录...", "清空使用记录失败");
        break;
    case PTC_UI_OPERATION_CLEAR_ACTIVITY_HISTORY:
        submit_transport_empty(ui, "clear_activity_history", "正在清空家庭活动记录...", "清空家庭活动记录失败");
        break;
    case PTC_UI_OPERATION_REDEEM_OFFLINE_CODE:
        ui->code_previous_after_available = ui->model.code_preview_after_available;
        ui->code_previous_after_zero = ui->model.code_preview_after_available &&
            ui->model.code_preview_after_minutes == 0;
        ui->code_previous_capped = ui->model.code_preview_capped;
        ui->code_previous_converts_unlimited = ui->model.code_preview_converts_unlimited;
        ui->code_preview_recheck = true;
        submit_preview_offline_code(ui, ui->model.pending_code);
        break;
    case PTC_UI_OPERATION_SAVE_CREDENTIAL:
        if (!commit_credential(ui)) snprintf(ui->model.message, sizeof(ui->model.message), "保存加时码密钥失败。");
        break;
    case PTC_UI_OPERATION_RESET_PAIRING_URL:
        apply_default_pairing_base_url(ui);
        break;
    case PTC_UI_OPERATION_COMPLETE_SETUP:
        submit_transport_empty(ui, "complete_setup", "正在完成首次设置...", "启用自动控制失败");
        break;
    case PTC_UI_OPERATION_RETRY_SETUP_RELEASE:
        submit_transport_empty(ui, "retry_setup_release", "正在重试解除当前限制...", "重试前置解限失败");
        break;
    case PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT:
        submit_transport_empty(ui, "restore_install_snapshot", "正在恢复安装前状态...", "恢复安装前状态失败");
        break;
    case PTC_UI_OPERATION_EMERGENCY_DISABLE:
        set_local_sd_command(ui, "紧急停用控制");
        status = ptc_companion_set_disable_flag(&ui->client, true);
        (void)ptc_companion_transport_notify_storage_changed(&ui->transport);
        ui->model.feedback_detail[0] = '\0';
        if (status == PTC_COMPANION_OK) {
            ui->model.disable_flag_present = true;
            snprintf(ui->model.result_status, sizeof(ui->model.result_status), "ok");
            snprintf(ui->model.message, sizeof(ui->model.message), "后台控制已紧急停用。");
        } else {
            set_message(ui, "紧急停用失败", status);
        }
        break;
    case PTC_UI_OPERATION_RESUME_CONTROL:
        set_local_sd_command(ui, "解除紧急停用");
        status = ptc_companion_set_disable_flag(&ui->client, false);
        (void)ptc_companion_transport_notify_storage_changed(&ui->transport);
        ui->model.feedback_detail[0] = '\0';
        if (status == PTC_COMPANION_OK) {
            ui->model.disable_flag_present = false;
            snprintf(ui->model.result_status, sizeof(ui->model.result_status), "ok");
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用已解除，后台控制已恢复。");
        } else {
            set_message(ui, "解除紧急停用失败", status);
        }
        break;
    default:
        break;
    }
}

void accept_numpad(UiState *ui)
{
    PtcUiNumpadPurpose purpose = ui->model.numpad_purpose;
    uint16_t value = 0;
    char code[9];
    if (purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES && weekly_editing_blocked(ui)) {
        return;
    }
    if (!ptc_ui_numpad_validate(&ui->model, &value)) {
        return;
    }
    if (purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        snprintf(code, sizeof(code), "%s", ui->model.numpad_text);
        snprintf(ui->model.pending_code, sizeof(ui->model.pending_code), "%s", code);
        ptc_ui_numpad_finish(&ui->model);
        ui->code_preview_recheck = false;
        submit_preview_offline_code(ui, code);
        return;
    }
    if (purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) {
        ui->model.draft_week[ui->model.editor_index].minutes = value;
        ui->model.weekly_dirty = memcmp(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week)) != 0;
    } else if (purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES) {
        ui->model.draft_holiday_rule.minutes = value;
        update_holiday_dirty(ui);
    } else if (purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) {
        ui->model.draft_makeup_workday_rule.minutes = value;
        update_holiday_dirty(ui);
    } else if (purpose == PTC_UI_NUMPAD_SCHEDULED_MINUTES) {
        ui->model.draft_scheduled_override.rule.minutes = value;
    } else if (purpose == PTC_UI_NUMPAD_GRANT_MINUTES) {
        ui->model.grant_minutes = value;
    } else if (purpose == PTC_UI_NUMPAD_BEDTIME_TIME) {
        PtcBedtimeWindow *window;
        if (ui->model.numpad_return_overlay == PTC_UI_OVERLAY_BEDTIME_WINDOW) {
            window = &ui->model.draft_bedtime_policy.week[ui->model.bedtime_editor_day];
        } else if (ui->model.numpad_return_overlay == PTC_UI_OVERLAY_BEDTIME) {
            window = &ui->model.draft_bedtime_policy.week[0];
        } else {
            PtcBedtimeSpecialRule *rule = ui->model.bedtime_special_kind == 0
                ? &ui->model.draft_bedtime_policy.holiday_rule
                : (ui->model.bedtime_special_kind == 1
                    ? &ui->model.draft_bedtime_policy.makeup_workday_rule
                    : &ui->model.draft_bedtime_policy.scheduled_override.rule);
            rule->mode = PTC_BEDTIME_OVERRIDE_CUSTOM;
            rule->window.enabled = true;
            window = &rule->window;
        }
        if (ui->model.bedtime_editor_time_target == PTC_UI_BEDTIME_TIME_START)
            window->start_minute = value;
        else
            window->end_minute = value;
        if (ui->model.numpad_return_overlay == PTC_UI_OVERLAY_BEDTIME) {
            for (int day = 1; day < 7; ++day) {
                if (ui->model.bedtime_editor_time_target == PTC_UI_BEDTIME_TIME_START)
                    ui->model.draft_bedtime_policy.week[day].start_minute = value;
                else
                    ui->model.draft_bedtime_policy.week[day].end_minute = value;
            }
        }
        update_bedtime_dirty(ui);
    } else if (purpose == PTC_UI_NUMPAD_MINUTES) {
        ui->model.draft_minutes = value;
    }
    ptc_ui_numpad_finish(&ui->model);
}

void update_weekly_dirty(UiState *ui)
{
    ui->model.weekly_dirty = memcmp(
        ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week)) != 0;
}

void update_holiday_dirty(UiState *ui)
{
    ui->model.holiday_dirty = ui->model.draft_holiday_enabled != ui->model.holiday_enabled ||
        memcmp(&ui->model.draft_holiday_rule, &ui->model.holiday_rule, sizeof(PtcDayRule)) != 0 ||
        memcmp(&ui->model.draft_makeup_workday_rule, &ui->model.makeup_workday_rule, sizeof(PtcDayRule)) != 0;
}

void refresh_album_restriction(UiState *ui)
{
    PtcAlbumRestrictionStatus status;
    if (!ui) return;
    if (!ptc_album_restriction_get_status(ui->client.storage, &status)) {
        ui->model.album_restriction_state = PTC_ALBUM_RESTRICTION_UNKNOWN;
        ui->model.album_backup_valid = false;
        snprintf(ui->model.album_restriction_detail,
                 sizeof(ui->model.album_restriction_detail),
                 "状态读取失败，请重新检测");
        return;
    }
    ui->model.album_restriction_state = (int)status.state;
    ui->model.album_backup_valid = status.backup_valid;
    snprintf(ui->model.album_restriction_detail, sizeof(ui->model.album_restriction_detail), "%s", status.detail);
}

void save_weekly_from_page(UiState *ui)
{
    PtcEffectiveRule before;
    PtcEffectiveRule after;
    char body[192];
    bool hold;
    if (weekly_editing_blocked(ui)) {
        return;
    }
    if (!ui->model.weekly_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), "周计划没有修改。");
        return;
    }
    hold = ptc_ui_plan_save_requires_hold(
        &ui->model, PTC_UI_PLAN_WEEKLY, (int64_t)time(NULL));
    if (!hold) {
        submit_weekly(ui);
        return;
    }
    before = ptc_ui_plan_rule(&ui->model, PTC_UI_PLAN_SAVED);
    after = ptc_ui_plan_rule(&ui->model, PTC_UI_PLAN_WEEKLY);
    snprintf(body, sizeof(body), "保存前请核对今天的最终规则、预计剩余时间和覆盖原因。");
    open_danger_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_WEEKLY,
        (before.source != after.source || ptc_ui_day_rule_effectively_changed(before.rule, after.rule))
            ? "周计划将影响今天" : "确认保存每周计划", body);
}

void save_holiday_from_page(UiState *ui)
{
    PtcEffectiveRule before;
    PtcEffectiveRule after;
    bool hold;
    if (!ui || ui->model.disable_flag_present) {
        if (ui) snprintf(ui->model.message, sizeof(ui->model.message),
                         "紧急停用中，国家节假日设置暂时只读。");
        return;
    }
    if (!ui->model.holiday_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), "国家节假日设置没有修改。");
        return;
    }
    hold = ptc_ui_plan_save_requires_hold(
        &ui->model, PTC_UI_PLAN_HOLIDAY, (int64_t)time(NULL));
    if (!hold) {
        submit_holiday_policy(ui);
        return;
    }
    before = ptc_ui_plan_rule(&ui->model, PTC_UI_PLAN_SAVED);
    after = ptc_ui_plan_rule(&ui->model, PTC_UI_PLAN_HOLIDAY);
    open_danger_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_HOLIDAY,
        (before.source != after.source || ptc_ui_day_rule_effectively_changed(before.rule, after.rule))
            ? "国家节假日设置将影响今天" : "确认保存国家节假日设置",
        "保存前请核对今天的最终规则、预计剩余时间和覆盖原因。");
}

void apply_pending_navigation(UiState *ui)
{
    if (ui->pending_leave_parent && ui->model.parent_page == PTC_UI_PARENT_PLAN &&
        ui->model.plan_page != PTC_UI_PLAN_PAGE_ROOT) {
        PtcUiPlanPage previous = ui->model.plan_page;
        ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
            ui->model.selected_index = previous == PTC_UI_PLAN_PAGE_WEEKLY ? 2 :
            (previous == PTC_UI_PLAN_PAGE_HOLIDAY ? 1 : 3);
        ui->model.parent_footer_focused = false;
        submit_status(ui);
    } else if (ui->pending_leave_parent) {
        ui->model.view = ui->model.setup_phase[0] && strcmp(ui->model.setup_phase, "active") != 0
            ? PTC_UI_SETUP : PTC_UI_CHILD;
        snprintf(ui->model.message, sizeof(ui->model.message), "已返回主页面。");
        if (ui->model.view == PTC_UI_CHILD) {
            submit_status(ui);
        }
    } else if (ui->pending_parent_page >= 0) {
        ui->model.parent_page = (PtcUiParentPage)ui->pending_parent_page;
        ui->model.selected_index = 0;
        if (ui->model.parent_page == PTC_UI_PARENT_PLAN) {
            ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
        }
        if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
            submit_status(ui);
        } else if (ui->model.parent_page == PTC_UI_PARENT_SETTINGS) {
            refresh_album_restriction(ui);
        }
    }
    ui->pending_parent_page = -1;
    ui->pending_leave_parent = false;
}

void refresh_recovery_state(UiState *ui)
{
    char path[192];
    if (!ui || !ui->client.storage) {
        return;
    }
    snprintf(path, sizeof(path), "%s/recovery/active/meta.json", APP_ROOT);
    ui->model.recovery_active = ui->client.storage->vtable->exists(ui->client.storage, path);
}

void request_parent_navigation(UiState *ui, int target_page, bool leave_parent);

void discard_holiday_draft(UiState *ui)
{
    ui->model.draft_holiday_enabled = ui->model.holiday_enabled;
    ui->model.draft_holiday_rule = ui->model.holiday_rule;
    ui->model.draft_makeup_workday_rule = ui->model.makeup_workday_rule;
    update_holiday_dirty(ui);
}

void update_bedtime_dirty(UiState *ui)
{
    if (!ui) return;
    ui->model.bedtime_dirty = memcmp(&ui->model.draft_bedtime_policy,
        &ui->model.bedtime_policy, sizeof(PtcBedtimePolicy)) != 0;
}

void discard_bedtime_draft(UiState *ui)
{
    if (!ui) return;
    ui->model.draft_bedtime_policy = ui->model.bedtime_policy;
    update_bedtime_dirty(ui);
    snprintf(ui->model.message, sizeof(ui->model.message), "已放弃未保存的就寝时间修改。");
}

void save_bedtime_from_page(UiState *ui)
{
    PtcBedtimePolicy *draft;
    if (!ui || ui->waiting) return;
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，就寝时间设置暂时只读。");
        return;
    }
    if (!ui->model.bedtime_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), "就寝时间没有修改。");
        return;
    }
    draft = &ui->model.draft_bedtime_policy;
    if (!ptc_bedtime_policy_is_valid(draft)) {
        snprintf(ui->model.message, sizeof(ui->model.message),
            "无法保存：请检查跨夜窗口、相邻日期重叠、特殊规则冲突和日期长度。");
        return;
    }
    if (draft->enabled && !ui->model.bedtime_official_setting_confirmed) {
        submit_bedtime_confirmation(ui);
        return;
    }
    {
        time_t raw_now = time(NULL);
        struct tm *tm_now = localtime(&raw_now);
        uint16_t minute_of_day = tm_now ? (uint16_t)(tm_now->tm_hour * 60 + tm_now->tm_min) : 0;
        if (ptc_ui_bedtime_save_will_restrict(&ui->model, minute_of_day)) {
            open_danger_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_BEDTIME,
                "立即进入就寝限制？",
                "当前时间处于设定的就寝时段内。保存后将立即暂停游戏并限制游玩（立断）。\n请长按 A 或持续按住确认按钮 1 秒。");
            return;
        }
    }
    submit_bedtime_policy(ui);
}

void select_bedtime_section(UiState *ui, int section)
{
    if (!ui) return;
    if (section < PTC_UI_BEDTIME_WEEKLY) section = PTC_UI_BEDTIME_SCHEDULED;
    if (section > PTC_UI_BEDTIME_SCHEDULED) section = PTC_UI_BEDTIME_WEEKLY;
    ui->model.bedtime_section = (PtcUiBedtimeSection)section;
    ui->model.selected_index = 0;
    ui->model.bedtime_section_focused = false;
    ui->model.parent_footer_focused = false;
}

void open_bedtime_window_editor(UiState *ui, int weekday)
{
    if (!ui || weekday < 0 || weekday >= 7) return;
    static const char *WEEKDAY_NAMES[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    ui->model.bedtime_editor_day = weekday;
    ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_WINDOW;
    ui->model.overlay_selection = 0;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s就寝时间窗口", WEEKDAY_NAMES[weekday]);
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
        "设定当晚至次日清晨的禁玩时段（必须跨越午夜）；到达开始时间立即强制暂停软件。");
}

void open_bedtime_special_editor(UiState *ui, int kind)
{
    if (!ui || kind < 0 || kind > 2) return;
    ui->model.bedtime_special_kind = kind;
    ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_SPECIAL;
    ui->model.overlay_selection = 0;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s",
        kind == 0 ? "法定休假就寝规则" :
        (kind == 1 ? "调休工作日就寝规则" : "指定日期就寝规则"));
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
        "选择继承、关闭或自定义；自定义窗口必须跨越午夜且不能与相邻窗口重叠。");
}

void open_bedtime_time_editor(UiState *ui, PtcUiBedtimeTimeTarget target)
{
    PtcUiOverlay return_overlay;
    const PtcBedtimeWindow *window;
    const char *title;
    if (!ui || (target != PTC_UI_BEDTIME_TIME_START && target != PTC_UI_BEDTIME_TIME_END)) return;
    return_overlay = ui->model.overlay;
    if (return_overlay == PTC_UI_OVERLAY_BEDTIME_WINDOW) {
        window = &ui->model.draft_bedtime_policy.week[ui->model.bedtime_editor_day];
    } else if (return_overlay == PTC_UI_OVERLAY_BEDTIME) {
        window = &ui->model.draft_bedtime_policy.week[0];
    } else if (return_overlay == PTC_UI_OVERLAY_BEDTIME_SPECIAL) {
        const PtcBedtimeSpecialRule *rule = ui->model.bedtime_special_kind == 0
            ? &ui->model.draft_bedtime_policy.holiday_rule
            : (ui->model.bedtime_special_kind == 1
                ? &ui->model.draft_bedtime_policy.makeup_workday_rule
                : &ui->model.draft_bedtime_policy.scheduled_override.rule);
        window = &rule->window;
    } else {
        return;
    }
    ui->model.bedtime_editor_time_target = target;
    title = target == PTC_UI_BEDTIME_TIME_START ? "设置就寝开始时间" : "设置次日结束时间";
    ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_BEDTIME_TIME, return_overlay,
        title, "选择小时或分钟；右摇杆上下调整，长推加速", 4, 0, 1439,
        target == PTC_UI_BEDTIME_TIME_START ? window->start_minute : window->end_minute);
}

void request_bedtime_leave(UiState *ui, int target_page, bool leave_parent)
{
    if (!ui) return;
    if (!ui->model.bedtime_dirty) {
        request_parent_navigation(ui, target_page, leave_parent);
        return;
    }
    ui->pending_parent_page = target_page;
    ui->pending_leave_parent = leave_parent;
    ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_LEAVE;
    ui->model.overlay_selection = ui->model.disable_flag_present ? 2 : 0;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "离开就寝时间编辑？");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
        ui->model.disable_flag_present
            ? "紧急停用期间不能保存；可继续编辑或放弃草稿后离开。"
            : "请选择保存并离开、放弃修改，或继续编辑。");
}

void request_parent_navigation(UiState *ui, int target_page, bool leave_parent)
{
    if (ui->waiting) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "请等待当前设置保存完成后再离开页面。");
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_SUPPORT &&
        (leave_parent || target_page != PTC_UI_PARENT_SUPPORT)) {
        ui->model.diagnostic_status = PTC_UI_DIAGNOSTIC_IDLE;
        ui->model.diagnostic_path[0] = '\0';
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
        ui->model.plan_page == PTC_UI_PLAN_PAGE_WEEKLY && ui->model.weekly_dirty) {
        ui->pending_parent_page = target_page;
        ui->pending_leave_parent = leave_parent;
        ui->model.overlay = PTC_UI_OVERLAY_WEEKLY_LEAVE;
        refresh_disable_flag(ui);
        ui->model.weekly_leave_selection = ui->model.disable_flag_present ? 2 : 1;
        if (!leave_parent && target_page == PTC_UI_PARENT_PLAN) {
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "刷新周计划？");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
                     ui->model.disable_flag_present
                       ? "紧急停用期间不能保存；请选择保留草稿并刷新、放弃草稿并刷新，或返回。"
                       : "刷新会读取已保存的周计划；请选择先保存、放弃草稿并刷新，或返回编辑。");
        } else {
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "离开周计划？");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
                     ui->model.disable_flag_present
                       ? "紧急停用期间不能保存；请选择保留草稿并离开、放弃草稿并离开，或返回。"
                       : "请选择保存修改、放弃修改，或返回继续编辑。");
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
        ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY && ui->model.holiday_dirty) {
        ui->pending_parent_page = target_page;
        ui->pending_leave_parent = leave_parent;
        ui->model.overlay = PTC_UI_OVERLAY_HOLIDAY_LEAVE;
        ui->model.holiday_leave_selection = 1;
        snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "离开国家节假日设置？");
        snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                 "这里还有尚未保存的更改。继续编辑可以保留内容；放弃更改后再离开。");
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
        ui->model.plan_page == PTC_UI_PLAN_PAGE_BEDTIME && ui->model.bedtime_dirty) {
        request_bedtime_leave(ui, target_page, leave_parent);
        return;
    }
    if (leave_parent && ui->model.parent_page == PTC_UI_PARENT_PLAN &&
        ui->model.plan_page != PTC_UI_PLAN_PAGE_ROOT) {
        PtcUiPlanPage previous = ui->model.plan_page;
        ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
        ui->model.selected_index = previous == PTC_UI_PLAN_PAGE_WEEKLY ? 2 :
            (previous == PTC_UI_PLAN_PAGE_HOLIDAY ? 1 : 3);
        ui->model.parent_footer_focused = false;
        return;
    }
    ui->pending_parent_page = target_page;
    ui->pending_leave_parent = leave_parent;
    apply_pending_navigation(ui);
}

void close_code_result(UiState *ui)
{
    bool terminal;
    if (!ui || ui->model.overlay != PTC_UI_OVERLAY_CODE_RESULT) return;
    terminal = !ui->model.code_result_pending;
    ptc_ui_cancel_overlay(&ui->model);
    if (terminal) {
        if (ptc_companion_pending_redemption_clear(&ui->client) != PTC_COMPANION_OK) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "兑换结果已显示，但恢复标记暂未清除；下次打开可能再次显示同一结果。");
        }
        memset(&ui->pending_redemption, 0, sizeof(ui->pending_redemption));
        ui->model.code_result_failed = false;
    } else {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "加时结果仍在确认中；可继续使用其他页面，下次打开也会继续确认。");
    }
}
