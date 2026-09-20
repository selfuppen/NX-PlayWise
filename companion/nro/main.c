#include "nro_app_internal.h"

__attribute__((used)) static const char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

void draw(UiState *ui)
{
    if (ui->model.overlay == PTC_UI_OVERLAY_AUTH_ERROR && ui->auth_cooldown_until > 0) {
        int64_t remaining = ui->auth_cooldown_until - (int64_t)time(NULL);
        ui->model.auth_cooldown_seconds = remaining > 0 ? (int)remaining : 0;
    }
    ui->model.waiting = ui->waiting;
    snprintf(ui->model.request_id, sizeof(ui->model.request_id), "%s", ui->active_request_id);
    ptc_ui_graphics_draw(&ui->model, &ui->theme_view);
}

void retry_error(UiState *ui)
{
    if (!ui) {
        return;
    }
    if (ui->model.error_code == 306) {
        ui->model.view = PTC_UI_CHILD;
        submit_status(ui);
    } else {
        ui->model.view = PTC_UI_CHILD;
        open_offline_code_input(ui);
    }
}

static void run_console_fallback(void)
{
    PadState pad;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    g_active_pad = &pad;
    printf("任我玩\n");
    printf("Play Wise. Play More.\n\n");
    printf("图形界面初始化失败。\n");
    printf("请确认系统共享中文字体和 FreeType 运行环境可用。\n\n");
    printf("按 + 退出。\n");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
}

int main(int argc, char **argv)
{
    PtcFsStorage fs;
    UiState ui;
    PadState pad;
    PadRepeater direction_repeater;
    HidTouchScreenState touch;
    bool touch_down = false;
    bool running = true;
    bool install_defaults_ready;
#ifndef PLAYWISE_EDEN
    bool backend_expected;
#endif
#ifdef PLAYWISE_EDEN
    /* PtcSysmodule alone is ~10 KiB, far too much for the main thread stack. */
    static PtcEdenRuntime eden_runtime;
    bool eden_runtime_ready;
#endif
    AppletHookCookie hook_cookie;
    u64 previous_stick_buttons = 0;
    int draw_elapsed_ms = DRAW_INTERVAL_MS;
    int background_poll_elapsed_ms = BACKGROUND_POLL_INTERVAL_MS;
    (void)argc;
    (void)argv;
    { const char *volatile manifest_anchor = PLAYWISE_EMBEDDED_MANIFEST; (void)manifest_anchor; }

    if (!ptc_ui_graphics_init()) {
        run_console_fallback();
        return 1;
    }
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    /* PIN entry reuses the application's active pad while it runs its modal
     * input loop.  Keep the graphical path registered just like fallback. */
    g_active_pad = &pad;
    padRepeaterInitialize(&direction_repeater,
        DIRECTION_REPEAT_INITIAL_TICKS, DIRECTION_REPEAT_TICKS);
    hidInitializeTouchScreen();
    srand((unsigned int)time(NULL));

    memset(&ui, 0, sizeof(ui));
    ui.pending_today_action = -1;
    ui.pending_parent_page = -1;
    ui.model.view = PTC_UI_CHILD;
    ui.model.parent_page = PTC_UI_PARENT_TODAY;
    ui.model.remaining_minutes = -1;
    ui.model.play_timer_enabled = -1;
    ui.model.restricted_now = -1;
    ptc_ui_set_execution(&ui.model, NULL, NULL);
    snprintf(ui.model.message, sizeof(ui.model.message), "正在读取今天的游玩状态...");
    ptc_fs_storage_init(&fs);
#ifndef PLAYWISE_EDEN
    ptc_hot_reload_init(&ui.hot_reload);
    ptc_hot_reload_recover_startup(&ui.hot_reload);
#endif
#ifdef PLAYWISE_EDEN
    /* Seeds the live root files, so the materialization below finds them all
       present and succeeds without a packaged defaults/ directory. Keep this
       call ahead of it. */
    eden_runtime_ready = ptc_eden_runtime_init(&eden_runtime, ptc_fs_storage_as_storage(&fs));
#endif
    install_defaults_ready = ptc_install_materialize_defaults(ptc_fs_storage_as_storage(&fs), APP_ROOT);
    ptc_companion_file_client_init(&ui.client, APP_ROOT, ptc_fs_storage_as_storage(&fs));
    load_ui_preferences(&ui);
    refresh_theme(&ui);
    appletHook(&hook_cookie, applet_hook, &ui);
    refresh_disable_flag(&ui);
#ifdef PLAYWISE_EDEN
    /* Eden exposes the embedded core through the existing IPC abstraction.
       Its Windows-backed SD implementation cannot reliably claim queue files
       with cross-directory rename, so requests complete synchronously and
       still persist their normal result JSON before returning to the UI. */
    ptc_companion_transport_init(&ui.transport, APP_ROOT, ptc_fs_storage_as_storage(&fs),
        ptc_eden_runtime_ipc_backend(), &eden_runtime);
#else
    backend_expected = standard_backend_expected();
    if (backend_expected) ptc_switch_ipc_client_init(&ui.ipc);
    ptc_companion_transport_init(&ui.transport, APP_ROOT, ptc_fs_storage_as_storage(&fs),
        backend_expected ? ptc_switch_ipc_backend() : NULL, &ui.ipc);
    if (ui.hot_reload.status == PTC_HOT_RELOAD_UNKNOWN) ptc_hot_reload_inspect(&ui.hot_reload);
    sync_hot_reload_model(&ui);
#endif
    ptc_companion_auth_init(&ui.auth, APP_ROOT, ptc_fs_storage_as_storage(&fs));
    ui.last_setup_refresh_second = -1;
    load_rule_drafts(&ui);
    if (!install_defaults_ready
#ifdef PLAYWISE_EDEN
        || !eden_runtime_ready
#endif
    ) {
        snprintf(ui.model.message, sizeof(ui.model.message),
                 "安装数据初始化失败，请重新覆盖安装包并确认 SD 卡可写。");
    }
#ifndef PLAYWISE_EDEN
    else if (ui.hot_reload.phase == PTC_HOT_RELOAD_PHASE_FAILED) {
        snprintf(ui.model.message, sizeof(ui.model.message), "%s", ui.hot_reload.detail);
    } else if (!backend_expected) {
        snprintf(ui.model.message, sizeof(ui.model.message),
                 "标准后台启动标志当前未启用。请先用 Device Lab 恢复正常后台并完整重启主机。");
    }
#endif
    else if (!restore_pending_redemption(&ui)) {
        submit_status(&ui);
    }

    while (appletMainLoop() && running) {
        u64 down;
        u64 held;
        u64 stick_buttons;
        bool parent_combo_held;
        bool custom_combo_held;
        bool custom_combo_triggered;
        bool custom_combo_candidate;
        bool touch_active;
        int touch_x = -1;
        int touch_y = -1;
        if (ui.theme_refresh_pending) refresh_theme(&ui);
        if (ui.status_refresh_pending) trigger_resume_status_refresh(&ui, &background_poll_elapsed_ms);
        padUpdate(&pad);
        down = padGetButtonsDown(&pad);
        held = padGetButtons(&pad);
        stick_buttons = stick_direction_buttons(padGetStickPos(&pad, 0));
        down |= stick_buttons & ~previous_stick_buttons;
        previous_stick_buttons = stick_buttons;
        held |= stick_buttons;
        touch_active = hidGetTouchScreenStates(&touch, 1) && touch.count > 0;
        if (touch_active) {
            touch_x = (int)touch.touches[0].x;
            touch_y = (int)touch.touches[0].y;
        }
        padRepeaterUpdate(&direction_repeater, held & DIRECTION_BUTTON_MASK);
        down |= padRepeaterGetButtons(&direction_repeater);
        parent_combo_held = hidden_parent_combo_held(held);
        custom_combo_held = custom_parent_combo_held(&ui, held);
        custom_combo_candidate = ui.model.view == PTC_UI_CHILD &&
            ui.model.overlay == PTC_UI_OVERLAY_NONE && ui.model.custom_shortcut_enabled &&
            (held & ui.model.custom_shortcut_mask) != 0;

        custom_combo_triggered = ptc_ui_shortcut_hold_update(
            &ui.custom_shortcut_hold, custom_combo_held, CUSTOM_SHORTCUT_HOLD_TICKS);
        if (custom_combo_triggered && ui.model.view == PTC_UI_CHILD &&
            ui.model.overlay == PTC_UI_OVERLAY_NONE) {
            ui.minus_pending = false;
            enter_parent_area(&ui);
        }

        if ((ui.model.view == PTC_UI_CHILD || ui.model.view == PTC_UI_SETUP) &&
            ui.model.overlay == PTC_UI_OVERLAY_NONE && parent_combo_held) {
            ++ui.hidden_ticks;
            if (ui.hidden_ticks == HIDDEN_HOLD_TICKS) {
                enter_parent_area(&ui);
            }
        } else {
            ui.hidden_ticks = 0;
        }

        if (ui.model.overlay != PTC_UI_OVERLAY_NONE) {
            if (ui.model.overlay == PTC_UI_OVERLAY_CONFIRM && ui.model.confirm_hold_required) {
                bool pad_confirm_held = (held & HidNpadButton_A) && ui.model.overlay_selection == 1;
                bool touch_confirm_held = touch_active &&
                    ptc_ui_rect_contains(ptc_ui_confirm_rect(ui.model.overlay), touch_x, touch_y);
                if (touch_confirm_held) touch_down = true;
                if (down & HidNpadButton_B) {
                    ptc_ui_confirm_hold_update(&ui.confirm_hold, false, DANGER_CONFIRM_HOLD_TICKS);
                    handle_overlay_input(&ui, down);
                } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
                    ptc_ui_confirm_hold_update(&ui.confirm_hold, false, DANGER_CONFIRM_HOLD_TICKS);
                    handle_overlay_input(&ui, down);
                } else {
                    if (ptc_ui_confirm_hold_update(&ui.confirm_hold,
                            pad_confirm_held || touch_confirm_held, DANGER_CONFIRM_HOLD_TICKS)) {
                        confirm_operation(&ui);
                    }
                }
                ui.model.confirm_hold_progress = ptc_ui_confirm_hold_progress(
                    &ui.confirm_hold, DANGER_CONFIRM_HOLD_TICKS);
            } else {
                ptc_ui_confirm_hold_update(&ui.confirm_hold, false, DANGER_CONFIRM_HOLD_TICKS);
                ui.model.confirm_hold_progress = 0;
                handle_overlay_input(&ui, down);
            }
            if (ui.model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
                HidAnalogStickState r_stick = padGetStickPos(&pad, 1);
                int v_dir = 0;
                int h_dir = 0;
                if (r_stick.y > STICK_DEADZONE) v_dir = 1;
                else if (r_stick.y < -STICK_DEADZONE) v_dir = -1;
                if (r_stick.x > STICK_DEADZONE) h_dir = 1;
                else if (r_stick.x < -STICK_DEADZONE) h_dir = -1;

                if (h_dir != 0 && h_dir != ui.r_stick_prev_h_dir) {
                    if (h_dir > 0) ptc_ui_duration_select_field(&ui.model, PTC_UI_DURATION_MINUTES);
                    else ptc_ui_duration_select_field(&ui.model, PTC_UI_DURATION_HOURS);
                    (void)ptc_ui_value_repeat_update(&ui.r_stick_repeat, 0, false, 0);
                }
                ui.r_stick_prev_h_dir = h_dir;

                if (v_dir != 0) {
                    int step = ptc_ui_value_repeat_update(&ui.r_stick_repeat, v_dir,
                        ui.model.duration_field == PTC_UI_DURATION_HOURS, ptc_ui_anim_now_ms());
                    ui.model.duration_scroll_dir = (int8_t)v_dir;
                    if (step != 0) {
                        int magnitude = step < 0 ? -step : step;
                        ui.model.duration_step_feedback = (uint8_t)magnitude;
                        ptc_ui_duration_step_field(&ui.model, step);
                        ui.model.duration_scroll_anim_ticks = 6;
                    }
                } else {
                    (void)ptc_ui_value_repeat_update(&ui.r_stick_repeat, 0, false, 0);
                    ui.model.duration_scroll_dir = 0;
                    ui.model.duration_step_feedback = 1;
                }

                if (ui.model.duration_scroll_anim_ticks > 0) {
                    --ui.model.duration_scroll_anim_ticks;
                }
            } else {
                (void)ptc_ui_value_repeat_update(&ui.r_stick_repeat, 0, false, 0);
                ui.r_stick_prev_h_dir = 0;
                ui.model.duration_scroll_dir = 0;
                ui.model.duration_scroll_anim_ticks = 0;
            }
        } else if (ui.model.view == PTC_UI_CHILD) {
            if (down & HidNpadButton_Minus) {
                /* Preserve the fixed Minus-on-release entrance even when Minus
                 * is also one member of the configured custom chord. */
                ui.minus_pending = true;
            }
            if (custom_combo_candidate) {
                /* Do not let buttons from an in-progress custom chord reach
                 * ordinary child-area actions before its hold decision. */
            } else if (down & HidNpadButton_B) {
                running = false;
            } else if (ui.minus_pending && !(held & HidNpadButton_Minus)) {
                ui.minus_pending = false;
                enter_parent_area(&ui);
            } else if (down & HidNpadButton_A) {
                if (ui.waiting) {
                    snprintf(ui.model.message, sizeof(ui.model.message), "请等待当前操作完成后再提交加时码。");
                } else {
                    open_offline_code_input(&ui);
                }
            } else if (down & HidNpadButton_Y) {
                submit_status(&ui);
            } else if (down & HidNpadButton_Plus) {
                ptc_ui_open_home_details(&ui.model);
            } else if (down & HidNpadButton_X) {
                if (ui.model.disable_flag_present) {
                    snprintf(ui.model.message, sizeof(ui.model.message), "控制已停用，自主缓冲暂不可领取。");
                } else if (ui.model.daily_buffer_available && !ui.waiting) {
                    submit_transport_empty(&ui, "claim_daily_buffer",
                        "正在领取今日自主缓冲...", "领取今日自主缓冲失败");
                } else if (ui.model.daily_buffer_claimed) {
                    snprintf(ui.model.message, sizeof(ui.model.message),
                        "今日已使用缓冲，明天可以再次领取。");
                } else {
                    snprintf(ui.model.message, sizeof(ui.model.message),
                        "今天没有可领取的自主缓冲。");
                }
            }
        } else if (ui.model.view == PTC_UI_SETUP) {
            if (down & HidNpadButton_Y) {
                submit_status(&ui);
            } else {
                handle_setup_input(&ui, down, held);
            }
        } else if (ui.model.view == PTC_UI_ERROR) {
            if (down & HidNpadButton_A) {
                retry_error(&ui);
            } else if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                enter_child_area(&ui);
            }
        } else {
            if ((down & HidNpadButton_Plus) && ui.model.parent_page == PTC_UI_PARENT_TODAY) {
                ptc_ui_open_home_details(&ui.model);
            } else if (ui.model.parent_footer_focused) {
                if (down & HidNpadButton_B) {
                    request_parent_navigation(&ui, -1, true);
                } else if (down & HidNpadButton_L &&
                           !(ui.model.parent_page == PTC_UI_PARENT_PLAN && ui.model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
                    request_parent_navigation(&ui,
                        (ui.model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
                } else if (down & HidNpadButton_R &&
                           !(ui.model.parent_page == PTC_UI_PARENT_PLAN && ui.model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
                    request_parent_navigation(&ui,
                        (ui.model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
                } else if (down & HidNpadButton_Up) {
                    ui.model.parent_footer_focused = false;
                    ui.model.selected_index = ui.model.parent_content_selection;
                } else if (down & HidNpadButton_Left) {
                    ui.model.parent_footer_selection = 0;
                } else if (down & HidNpadButton_Right) {
                    ui.model.parent_footer_selection =
                        ptc_ui_parent_status_alert_visible(&ui.model) ? 1 : 0;
                } else if (down & HidNpadButton_Y) {
                    refresh_disable_flag(&ui);
                    submit_status(&ui);
                } else if (down & HidNpadButton_A) {
                    if (ui.model.parent_footer_selection == 0) submit_status(&ui);
                    else activate_parent_status(&ui);
                }
            } else if (ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_WEEKLY) {
                PtcDayRule *day = &ui.model.draft_week[ui.model.editor_index];
                if (ui.waiting) {
                    if (down) {
                        snprintf(ui.model.message, sizeof(ui.model.message),
                                 "请等待周计划保存完成后再继续编辑。");
                    }
                } else if (down & HidNpadButton_B) {
                    request_parent_navigation(&ui, -1, true);
                } else if (down & (HidNpadButton_L | HidNpadButton_R)) {
                    request_parent_navigation(&ui, -1, true);
                } else if (down & HidNpadButton_Left) {
                    ptc_ui_move_weekly_focus(&ui.model, -1, 0);
                } else if (down & HidNpadButton_Right) {
                    ptc_ui_move_weekly_focus(&ui.model, 1, 0);
                } else if (down & HidNpadButton_X) {
                    if (!weekly_editing_blocked(&ui)) {
                        day->mode = ptc_ui_next_rule_mode(day->mode);
                        update_weekly_dirty(&ui);
                    }
                    if (!ui.model.disable_flag_present && day->mode == PTC_RULE_MODE_LIMIT) {
                        snprintf(ui.model.message, sizeof(ui.model.message),
                                 "已恢复此前的每日限额：%u 分钟。", (unsigned int)day->minutes);
                    }
                } else if (down & HidNpadButton_Up) {
                    ptc_ui_move_weekly_focus(&ui.model, 0, -1);
                } else if (down & HidNpadButton_Down) {
                    if (ui.model.selected_index != 0) {
                        ui.model.parent_content_selection = ui.model.selected_index;
                        ui.model.parent_footer_focused = true;
                        ui.model.parent_footer_selection =
                            ptc_ui_parent_status_alert_visible(&ui.model) ? 1 : 0;
                    } else {
                        ptc_ui_move_weekly_focus(&ui.model, 0, 1);
                    }
                } else if (down & HidNpadButton_Y) {
                    refresh_disable_flag(&ui);
                    submit_status(&ui);
                } else if (down & HidNpadButton_A) {
                    if (ui.model.selected_index == 2) {
                        if (weekly_editing_blocked(&ui)) {
                            snprintf(ui.model.message, sizeof(ui.model.message), "紧急停用中，批量操作暂不可用。");
                        } else {
                            ui.model.overlay = PTC_UI_OVERLAY_WEEKLY_BULK;
                            ui.model.overlay_selection = 0;
                            snprintf(ui.model.overlay_title, sizeof(ui.model.overlay_title), "批量快捷操作");
                            snprintf(ui.model.overlay_body, sizeof(ui.model.overlay_body), "把最后选中日期的完整草稿规则复制到一组日期。");
                        }
                    } else if (ui.model.selected_index == 3) {
                        if (ui.model.weekly_dirty) {
                            memcpy(ui.model.draft_week, ui.model.current_week, sizeof(ui.model.draft_week));
                            ui.model.weekly_dirty = false;
                            snprintf(ui.model.message, sizeof(ui.model.message), "已放弃未保存的周计划修改。");
                        } else {
                            snprintf(ui.model.message, sizeof(ui.model.message), "周计划没有修改。");
                        }
                    } else if (weekly_editing_blocked(&ui)) {
                        /* Focus remains movable while emergency stop makes the editor read-only. */
                    } else if (ui.model.selected_index == 1) {
                        day->mode = ptc_ui_next_rule_mode(day->mode);
                        update_weekly_dirty(&ui);
                        if (day->mode == PTC_RULE_MODE_LIMIT) {
                            snprintf(ui.model.message, sizeof(ui.model.message),
                                     "已恢复此前的每日限额：%u 分钟。", (unsigned int)day->minutes);
                        }
                    } else if (ui.model.selected_index == 4) {
                        save_weekly_from_page(&ui);
                    } else if (day->mode == PTC_RULE_MODE_LIMIT) {
                        edit_weekly_minutes(&ui);
                    } else {
                        snprintf(ui.model.message, sizeof(ui.model.message),
                                 "该日为不限时，没有可编辑的分钟数；请选择“切换模式”改为限时。");
                    }
                } else if (down & HidNpadButton_Plus) {
                    ui.model.selected_index = 4;
                    if (!weekly_editing_blocked(&ui)) {
                        save_weekly_from_page(&ui);
                    }
                } else if (down & HidNpadButton_ZL) {
                    ui.model.selected_index = 3;
                    if (ui.model.weekly_dirty) {
                        memcpy(ui.model.draft_week, ui.model.current_week, sizeof(ui.model.draft_week));
                        ui.model.weekly_dirty = false;
                        snprintf(ui.model.message, sizeof(ui.model.message), "已放弃未保存的周计划修改。");
                    } else {
                        snprintf(ui.model.message, sizeof(ui.model.message), "周计划没有修改。");
                    }
                }
            } else if (ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_BEDTIME) {
                PtcBedtimePolicy *draft = &ui.model.draft_bedtime_policy;
                if (ui.waiting) {
                    if (down) snprintf(ui.model.message, sizeof(ui.model.message),
                        "请等待就寝时间设置保存完成后再继续编辑。");
                } else if (down & HidNpadButton_B) {
                    request_bedtime_leave(&ui, -1, true);
                } else if (down & HidNpadButton_L) {
                    select_bedtime_section(&ui, ui.model.bedtime_section - 1);
                } else if (down & HidNpadButton_R) {
                    select_bedtime_section(&ui, ui.model.bedtime_section + 1);
                } else if (down & HidNpadButton_Up) {
                    ptc_ui_move_bedtime_focus(&ui.model, 0, -1);
                } else if (down & HidNpadButton_Down) {
                    ptc_ui_move_bedtime_focus(&ui.model, 0, 1);
                } else if (down & HidNpadButton_Left) {
                    ptc_ui_move_bedtime_focus(&ui.model, -1, 0);
                } else if (down & HidNpadButton_Right) {
                    ptc_ui_move_bedtime_focus(&ui.model, 1, 0);
                } else if (down & HidNpadButton_Minus) {
                    if (!ui.model.disable_flag_present) {
                        draft->enabled = !draft->enabled;
                        update_bedtime_dirty(&ui);
                        snprintf(ui.model.message, sizeof(ui.model.message), "就寝时间总开关已%s；保存后生效。",
                            draft->enabled ? "开启" : "关闭");
                    }
                } else if (ui.model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED &&
                           !ui.model.bedtime_section_focused &&
                           (down & (HidNpadButton_ZL | HidNpadButton_ZR))) {
                    int direction = down & HidNpadButton_ZR ? 1 : -1;
                    int date_step = 7;
                    if (ui.model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED &&
                        ui.model.selected_index == 1) {
                        uint32_t duration = draft->scheduled_override.end_day_index >=
                            draft->scheduled_override.start_day_index
                            ? (uint32_t)draft->scheduled_override.end_day_index -
                              draft->scheduled_override.start_day_index + 1u : 1u;
                        int next = (int)draft->scheduled_override.start_day_index + direction * date_step;
                        if (next < (int)ui.model.day_index) next = ui.model.day_index;
                        if ((uint32_t)next + duration - 1u > UINT16_MAX) next = UINT16_MAX - (int)duration + 1;
                        draft->scheduled_override.start_day_index = (uint16_t)next;
                        draft->scheduled_override.end_day_index = (uint16_t)(next + (int)duration - 1);
                        update_bedtime_dirty(&ui);
                    } else if (ui.model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED &&
                               ui.model.selected_index == 2) {
                        uint32_t duration = draft->scheduled_override.end_day_index >=
                            draft->scheduled_override.start_day_index
                            ? (uint32_t)draft->scheduled_override.end_day_index -
                              draft->scheduled_override.start_day_index + 1u : 1u;
                        int next = (int)duration + direction * date_step;
                        if (next < 1) next = 1;
                        if (next > 366) next = 366;
                        if ((uint32_t)draft->scheduled_override.start_day_index + (uint32_t)next - 1u > UINT16_MAX)
                            next = UINT16_MAX - draft->scheduled_override.start_day_index + 1;
                        draft->scheduled_override.end_day_index =
                            (uint16_t)(draft->scheduled_override.start_day_index + next - 1);
                        update_bedtime_dirty(&ui);
                    }
                } else if ((down & HidNpadButton_X) && !ui.model.bedtime_section_focused) {
                    if (ui.model.disable_flag_present) {
                        snprintf(ui.model.message, sizeof(ui.model.message), "紧急停用中，就寝时间设置暂时只读。");
                    } else if (ui.model.bedtime_section == PTC_UI_BEDTIME_WEEKLY &&
                               ui.model.selected_index < 7) {
                        int day = ptc_ui_weekday_for_display_slot(ui.model.selected_index);
                        draft->week[day].enabled = !draft->week[day].enabled;
                        ui.model.bedtime_editor_day = day;
                        update_bedtime_dirty(&ui);
                    } else if (ui.model.bedtime_section == PTC_UI_BEDTIME_CALENDAR &&
                               ui.model.selected_index == 0) {
                        draft->calendar_enabled = !draft->calendar_enabled;
                        update_bedtime_dirty(&ui);
                    } else if (ui.model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED &&
                               ui.model.selected_index == 0) {
                        draft->scheduled_override.present = !draft->scheduled_override.present;
                        if (draft->scheduled_override.start_day_index < ui.model.day_index) {
                            draft->scheduled_override.start_day_index = ui.model.day_index;
                            draft->scheduled_override.end_day_index = ui.model.day_index;
                        }
                        update_bedtime_dirty(&ui);
                    } else if (ui.model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED &&
                               ui.model.selected_index == 3) {
                        PtcBedtimeSpecialRule *rule = &draft->scheduled_override.rule;
                        rule->mode = (PtcBedtimeOverrideMode)((rule->mode + 1) % 3);
                        if (rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM) rule->window.enabled = true;
                        update_bedtime_dirty(&ui);
                    }
                } else if ((down & HidNpadButton_A) && !ui.model.bedtime_section_focused) {
                    if (ui.model.bedtime_section == PTC_UI_BEDTIME_WEEKLY) {
                        if (ui.model.selected_index < 7) {
                            int day = ptc_ui_weekday_for_display_slot(ui.model.selected_index);
                            ui.model.bedtime_editor_day = day;
                            open_bedtime_window_editor(&ui, day);
                        } else if (ui.model.selected_index == 7 || ui.model.selected_index == 8) {
                            ui.model.overlay = PTC_UI_OVERLAY_BEDTIME_BULK;
                            ui.model.overlay_selection = ui.model.selected_index - 7;
                            snprintf(ui.model.overlay_title, sizeof(ui.model.overlay_title), "复制每周就寝窗口");
                            snprintf(ui.model.overlay_body, sizeof(ui.model.overlay_body),
                                "把最后编辑日期的完整开关和时间复制到所选日期组。");
                        } else if (ui.model.selected_index == 9) discard_bedtime_draft(&ui);
                        else save_bedtime_from_page(&ui);
                    } else if (ui.model.bedtime_section == PTC_UI_BEDTIME_CALENDAR) {
                        if (ui.model.selected_index == 0) {
                            draft->calendar_enabled = !draft->calendar_enabled;
                            update_bedtime_dirty(&ui);
                        } else if (ui.model.selected_index <= 2) {
                            open_bedtime_special_editor(&ui, ui.model.selected_index - 1);
                        } else if (ui.model.selected_index == 3) discard_bedtime_draft(&ui);
                        else save_bedtime_from_page(&ui);
                    } else {
                        if (ui.model.selected_index == 0) {
                            draft->scheduled_override.present = !draft->scheduled_override.present;
                            if (draft->scheduled_override.start_day_index < ui.model.day_index) {
                                draft->scheduled_override.start_day_index = ui.model.day_index;
                                draft->scheduled_override.end_day_index = ui.model.day_index;
                            }
                            update_bedtime_dirty(&ui);
                        } else if (ui.model.selected_index == 1) {
                            if (edit_date_range_start(&ui, &draft->scheduled_override.start_day_index,
                                                      &draft->scheduled_override.end_day_index))
                                update_bedtime_dirty(&ui);
                        } else if (ui.model.selected_index == 2) {
                            if (edit_date_range_span(&ui, draft->scheduled_override.start_day_index,
                                                     &draft->scheduled_override.end_day_index))
                                update_bedtime_dirty(&ui);
                        } else if (ui.model.selected_index == 3) {
                            open_bedtime_special_editor(&ui, 2);
                        } else if (ui.model.selected_index == 4) discard_bedtime_draft(&ui);
                        else if (ui.model.selected_index == 5) save_bedtime_from_page(&ui);
                    }
                } else if (down & HidNpadButton_Plus) {
                    save_bedtime_from_page(&ui);
                } else if (down & HidNpadButton_ZL) {
                    discard_bedtime_draft(&ui);
                }
            } else if (ui.waiting && ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
                if (down) {
                    snprintf(ui.model.message, sizeof(ui.model.message),
                             "请等待国家节假日设置保存完成后再继续编辑。");
                }
            } else if (down & HidNpadButton_B) {
                request_parent_navigation(&ui, -1, true);
            } else if (down & HidNpadButton_L &&
                       !(ui.model.parent_page == PTC_UI_PARENT_PLAN && ui.model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
                request_parent_navigation(&ui,
                    (ui.model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
            } else if (down & HidNpadButton_R &&
                       !(ui.model.parent_page == PTC_UI_PARENT_PLAN && ui.model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
                request_parent_navigation(&ui,
                    (ui.model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
            } else if (down & HidNpadButton_Left) {
                ptc_ui_move_parent_selection(&ui.model, -1, 0);
            } else if (down & HidNpadButton_Right) {
                ptc_ui_move_parent_selection(&ui.model, 1, 0);
            } else if (down & HidNpadButton_Up) {
                ptc_ui_move_parent_selection(&ui.model, 0, -1);
            } else if (down & HidNpadButton_Down) {
                ptc_ui_move_parent_selection(&ui.model, 0, 1);
            } else if (down & HidNpadButton_Y) {
                refresh_disable_flag(&ui);
                submit_status(&ui);
            } else if ((down & HidNpadButton_X) && ptc_ui_operation_feedback_visible(&ui.model) &&
                       (ui.model.feedback_detail[0] || strcmp(ui.model.result_status, "error") == 0 ||
                        ui.model.parent_page == PTC_UI_PARENT_SUPPORT)) {
                ui.model.overlay = PTC_UI_OVERLAY_NOTICE_DETAILS;
            } else if (down & HidNpadButton_X && ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
                if (ui.model.disable_flag_present) {
                    snprintf(ui.model.message, sizeof(ui.model.message), "紧急停用中，规则暂时只读。");
                } else if (ui.model.selected_index == 1 ||
                           (ui.model.selected_index != 2 && ui.model.holiday_last_rule == 0)) {
                    ui.model.holiday_last_rule = 0;
                    ui.model.draft_holiday_rule.mode = ptc_ui_next_rule_mode(ui.model.draft_holiday_rule.mode);
                    update_holiday_dirty(&ui);
                } else {
                    ui.model.holiday_last_rule = 1;
                    ui.model.draft_makeup_workday_rule.mode = ptc_ui_next_rule_mode(ui.model.draft_makeup_workday_rule.mode);
                    update_holiday_dirty(&ui);
                }
            } else if (down & HidNpadButton_Plus && ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
                ui.model.selected_index = 5;
                save_holiday_from_page(&ui);
            } else if (down & HidNpadButton_ZL && ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
                ui.model.selected_index = 4;
                discard_holiday_draft(&ui);
            } else if (down & HidNpadButton_A) {
                if (ui.waiting) {
                    snprintf(ui.model.message, sizeof(ui.model.message), "请等待当前操作完成后再执行其他设置。");
                } else if (ui.model.parent_footer_focused) {
                    activate_parent_status(&ui);
                } else if (ui.model.parent_page == PTC_UI_PARENT_SUPPORT && ui.model.selected_index >= 6) {
                    int visible_index = ui.model.selected_index - 6;
                    int event_index = ui.model.recent_event_count - 1 - visible_index;
                    if (event_index >= 0 && event_index < ui.model.recent_event_count) {
                        ui.model.overlay = PTC_UI_OVERLAY_SUPPORT_EVENT;
                        ui.model.overlay_selection = event_index;
                        snprintf(ui.model.overlay_title, sizeof(ui.model.overlay_title), "最近事件详情");
                        snprintf(ui.model.overlay_body, sizeof(ui.model.overlay_body),
                                 "家长区已通过 PIN 验证；这里显示完整诊断字段，但不会显示 PIN、密钥或可复用授权材料。");
                    }
                } else {
                    handle_parent_action(&ui);
                }
            }
        }

        if (touch_active) {
            if (!touch_down) {
                touch_down = true;
                if (!(ui.model.overlay == PTC_UI_OVERLAY_CONFIRM && ui.model.confirm_hold_required &&
                      ptc_ui_rect_contains(ptc_ui_confirm_rect(ui.model.overlay), touch_x, touch_y))) {
                    handle_touch(&ui, touch_x, touch_y);
                }
            }
        } else {
            touch_down = false;
        }

        if (ui.exit_requested) {
            running = false;
        }

        background_poll_elapsed_ms += INPUT_LOOP_MS;
        if (background_poll_elapsed_ms >= BACKGROUND_POLL_INTERVAL_MS) {
#ifdef PLAYWISE_EDEN
            ptc_eden_runtime_tick(&eden_runtime);
#else
            poll_hot_reload(&ui);
#endif
            poll_pending_redemption(&ui);
            poll_result(&ui, false);
            refresh_setup_activation(&ui);
            background_poll_elapsed_ms = 0;
        }
        draw_elapsed_ms += INPUT_LOOP_MS;
        if (draw_elapsed_ms >= (ui.animating ? DRAW_INTERVAL_FAST_MS : DRAW_INTERVAL_MS)) {
            ui.animating = update_animations(&ui.model);
            draw(&ui);
            draw_elapsed_ms = 0;
        }
        svcSleepThread(ui.animating ? INPUT_LOOP_SLEEP_FAST_NS : INPUT_LOOP_SLEEP_NS);
    }

    appletUnhook(&hook_cookie);
    ptc_ui_graphics_exit();
#ifndef PLAYWISE_EDEN
    ptc_hot_reload_exit(&ui.hot_reload);
    ptc_switch_ipc_client_exit(&ui.ipc);
#endif
    return 0;
}
