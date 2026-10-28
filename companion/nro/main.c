#include "nro_app_internal.h"
#include "eden_observe.h"

__attribute__((used)) static const char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

void draw(UiState *ui)
{
    refresh_auth_error_cooldown(ui);
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
    printf(ptc_ui_text(PTC_UI_T_PLAYWISE_2));
    printf("Play Wise. Play More.\n\n");
    printf(ptc_ui_text(PTC_UI_T_FAILED_TO_INITIALIZE_GRAPHICS));
    printf(ptc_ui_text(PTC_UI_T_CHECK_THAT_THE_SHARED_SYSTEM_FONT_AND));
    printf(ptc_ui_text(PTC_UI_T_PRESS_TO_EXIT));
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
    int details_touch_y = -1;
    bool details_touch_drag = false;
    bool running = true;
    bool install_defaults_ready;
#ifndef PLAYWISE_EDEN
    bool backend_expected;
#endif
#ifdef PLAYWISE_EDEN
    /* PtcSysmodule alone is ~10 KiB, far too much for the main thread stack. */
    static PtcEdenRuntime eden_runtime;
    bool eden_runtime_ready;
    bool eden_status_dirty = false;
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
    ptc_audio_init();
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
    ui.pending_bedtime_section = -1;
    ui.bedtime_saved_section = -1;
    ui.model.view = PTC_UI_CHILD;
    ui.model.parent_page = PTC_UI_PARENT_TODAY;
    ui.model.remaining_minutes = -1;
    ui.model.eye_care_policy.play_minutes = 40;
    ui.model.eye_care_policy.rest_minutes = 10;
    ui.model.play_timer_enabled = -1;
    ui.model.restricted_now = -1;
    ptc_ui_set_execution(&ui.model, NULL, NULL);
    snprintf(ui.model.message, sizeof(ui.model.message), ptc_ui_text(PTC_UI_T_READING_TODAY_S_GAME_STATUS));
    ptc_fs_storage_init(&fs);
#ifndef PLAYWISE_EDEN
    ptc_online_update_init(&ui.updater, ptc_fs_storage_as_storage(&fs));
    ptc_hot_reload_init(&ui.hot_reload);
    if (ui.updater.status != PTC_UPDATE_RECOVERY_REQUIRED) ptc_hot_reload_recover_startup(&ui.hot_reload);
#endif
#ifdef PLAYWISE_EDEN
    /* Seeds the live root files, so the materialization below finds them all
       present and succeeds without a packaged defaults/ directory. Keep this
       call ahead of it. */
    ui.model.eden_mode_controls = true;
    eden_runtime_ready = ptc_eden_runtime_init(&eden_runtime, ptc_fs_storage_as_storage(&fs));
#endif
    install_defaults_ready =
#ifndef PLAYWISE_EDEN
        ui.updater.status != PTC_UPDATE_RECOVERY_REQUIRED &&
#endif
        ptc_install_materialize_defaults(ptc_fs_storage_as_storage(&fs), APP_ROOT);
    ptc_companion_file_client_init(&ui.client, APP_ROOT, ptc_fs_storage_as_storage(&fs));
#ifndef PLAYWISE_EDEN
    ptc_ui_status_trace_init(&ui.status_trace, ptc_fs_storage_as_storage(&fs), APP_ROOT,
        PLAYWISE_BUILD_COMMIT, PLAYWISE_BUILD_RELEASE_ID, "nro", armTicksToNs(armGetSystemTick()));
    ptc_input_owner_reader_init(&ui.input_owner, ptc_fs_storage_as_storage(&fs), APP_ROOT,
        PLAYWISE_BUILD_RELEASE_ID);
#endif
    load_ui_preferences(&ui);
    refresh_theme(&ui);
    refresh_language(&ui);
    appletHook(&hook_cookie, applet_hook, &ui);
    refresh_disable_flag(&ui);
    if (!ui.model.setup_wizard_completed) {
        ui.model.view = PTC_UI_SETUP;
        if (ui.model.setup_step == 0) ui.model.setup_step = PTC_UI_SETUP_PREPARE;
    }
#ifdef PLAYWISE_EDEN
    /* Eden exposes the embedded core through the existing IPC abstraction.
       Its Windows-backed SD implementation cannot reliably claim queue files
       with cross-directory rename, so requests complete synchronously and
       still persist their normal result JSON before returning to the UI. */
    ptc_companion_transport_init(&ui.transport, APP_ROOT, ptc_fs_storage_as_storage(&fs),
        ptc_eden_runtime_ipc_backend(), &eden_runtime);
#else
    backend_expected = standard_backend_expected() && ui.updater.status != PTC_UPDATE_RECOVERY_REQUIRED && ui.hot_reload.phase != PTC_HOT_RELOAD_PHASE_FAILED &&
        ui.hot_reload.status != PTC_HOT_RELOAD_RUNNING;
    if (backend_expected) ptc_switch_ipc_client_init(&ui.ipc);
    ptc_companion_transport_init(&ui.transport, APP_ROOT, ptc_fs_storage_as_storage(&fs),
        (backend_expected || ui.hot_reload.status == PTC_HOT_RELOAD_RUNNING) ? ptc_switch_ipc_backend() : NULL, &ui.ipc);
    if (ui.hot_reload.phase == PTC_HOT_RELOAD_PHASE_FAILED || ui.hot_reload.status == PTC_HOT_RELOAD_RUNNING)
        ptc_companion_transport_set_suspended(&ui.transport, true);
    if (ui.hot_reload.status == PTC_HOT_RELOAD_UNKNOWN && ui.updater.status != PTC_UPDATE_RECOVERY_REQUIRED) ptc_hot_reload_inspect(&ui.hot_reload);
    sync_hot_reload_model(&ui);
#endif
    ptc_companion_auth_init(&ui.auth, APP_ROOT, ptc_fs_storage_as_storage(&fs));
    ui.last_setup_refresh_second = -1;
    load_rule_drafts(&ui);
    ui.model.setup_pin_ready = ptc_companion_auth_state(&ui.auth) == PTC_AUTH_OK;
    if (!install_defaults_ready
#ifdef PLAYWISE_EDEN
        || !eden_runtime_ready
#endif
    ) {
        ptc_ui_setup_record_issue(&ui.model, PTC_UI_SETUP_ISSUE_DEFAULTS, PTC_ERR_STORAGE_WRITE_FAILED);
        snprintf(ui.model.message, sizeof(ui.model.message),
                 ptc_ui_text(PTC_UI_T_THE_INSTALLATION_DATA_INITIALIZATION_FAILED_PLEASE_OVERWRITE));
    }
#ifndef PLAYWISE_EDEN
    else if (ui.hot_reload.phase == PTC_HOT_RELOAD_PHASE_FAILED) {
        snprintf(ui.model.message, sizeof(ui.model.message), "%s", ui.hot_reload.detail);
    } else if (ui.hot_reload.status == PTC_HOT_RELOAD_RUNNING) {
        ui.waiting = true;
        ui.model.waiting = true;
        snprintf(ui.model.message, sizeof(ui.model.message), "%s", ui.hot_reload.detail);
    } else if (!backend_expected) {
        ptc_ui_setup_record_issue(&ui.model, PTC_UI_SETUP_ISSUE_STATUS, PTC_ERR_PCTL_INIT_FAILED);
        snprintf(ui.model.message, sizeof(ui.model.message),
                 ptc_ui_text(PTC_UI_T_THE_STANDARD_BACKGROUND_STARTUP_FLAG_IS_NOT));
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
        bool parent_minus_released;
        bool custom_combo_held;
        bool custom_combo_triggered;
        bool custom_combo_candidate;
        bool touch_active;
        HidAnalogStickState left_stick;
        HidAnalogStickState right_stick;
        int touch_x = -1;
        int touch_y = -1;
        if (ui.theme_refresh_pending) {
            refresh_theme(&ui);
            refresh_language(&ui);
        }
        /* 主题页草稿只在该页有效：其他路径关闭页面时恢复已保存外观。 */
        if (ui.model.theme_draft_valid && ui.model.overlay != PTC_UI_OVERLAY_THEME) {
            discard_theme_draft(&ui);
        }
        if (ui.status_refresh_pending) trigger_resume_status_refresh(&ui, &background_poll_elapsed_ms);
#ifdef PLAYWISE_EDEN
        poll_eden_ready_result(&ui);
#endif
        padUpdate(&pad);
        down = padGetButtonsDown(&pad);
        held = padGetButtons(&pad);
        left_stick = padGetStickPos(&pad, 0);
        right_stick = padGetStickPos(&pad, 1);
        stick_buttons = stick_direction_buttons(left_stick);
        down |= stick_buttons & ~previous_stick_buttons;
        previous_stick_buttons = stick_buttons;
        held |= stick_buttons;
        touch_active = hidGetTouchScreenStates(&touch, 1) && touch.count > 0;
        if (touch_active) {
            touch_x = (int)touch.touches[0].x;
            touch_y = (int)touch.touches[0].y;
        }
        if (input_owner_blocks(&ui, down || held || touch_active ||
                abs(right_stick.x) > STICK_DEADZONE || abs(right_stick.y) > STICK_DEADZONE)) {
            down = held = previous_stick_buttons = 0;
            memset(&left_stick, 0, sizeof(left_stick));
            memset(&right_stick, 0, sizeof(right_stick));
            touch_active = touch_down = details_touch_drag = false;
            touch_x = touch_y = details_touch_y = -1;
            ui.minus_pending = false;
            ui.hidden_ticks = 0;
            memset(&ui.custom_shortcut_hold, 0, sizeof(ui.custom_shortcut_hold));
            memset(&ui.r_stick_repeat, 0, sizeof(ui.r_stick_repeat));
            ui.r_stick_prev_h_dir = 0;
        }
        padRepeaterUpdate(&direction_repeater, held & DIRECTION_BUTTON_MASK);
#ifdef PLAYWISE_EDEN
        ptc_eden_observe_input(held != 0 || touch_active);
#endif
        down |= padRepeaterGetButtons(&direction_repeater);
        parent_minus_released = ptc_ui_single_button_release_update(
            &ui.minus_pending, HidNpadButton_Minus, down, held,
            ui.model.view == PTC_UI_CHILD && ui.model.overlay == PTC_UI_OVERLAY_NONE);
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
                int64_t confirm_now_ms = ptc_ui_anim_now_ms();
                bool pad_confirm_held = (held & HidNpadButton_A) && ui.model.overlay_selection == 1;
                bool touch_confirm_held = touch_active &&
                    ptc_ui_rect_contains(ptc_ui_confirm_rect(ui.model.overlay), touch_x, touch_y);
                if (touch_confirm_held) touch_down = true;
                if (ui.waiting || ui.quota_recheck_pending) {
                    if (ui.confirm_hold.holding || ui.model.confirm_hold_progress > 0) {
                        ptc_audio_stop();
                    }
                    ptc_ui_confirm_hold_update(&ui.confirm_hold, false, confirm_now_ms, DANGER_CONFIRM_HOLD_MS);
                    ui.model.confirm_hold_progress = 0;
                } else if (down & HidNpadButton_B) {
                    if (ui.confirm_hold.holding || ui.model.confirm_hold_progress > 0) {
                        ptc_audio_stop();
                    }
                    ptc_ui_confirm_hold_update(&ui.confirm_hold, false, confirm_now_ms, DANGER_CONFIRM_HOLD_MS);
                    ui.model.confirm_hold_progress = 0;
                    handle_overlay_input(&ui, down);
                } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
                    if (ui.confirm_hold.holding || ui.model.confirm_hold_progress > 0) {
                        ptc_audio_stop();
                    }
                    ptc_ui_confirm_hold_update(&ui.confirm_hold, false, confirm_now_ms, DANGER_CONFIRM_HOLD_MS);
                    ui.model.confirm_hold_progress = 0;
                    handle_overlay_input(&ui, down);
                } else if ((down & HidNpadButton_Y) &&
                           quota_operation_needs_recheck(ui.model.operation)) {
                    if (ui.confirm_hold.holding || ui.model.confirm_hold_progress > 0) {
                        ptc_audio_stop();
                    }
                    ptc_ui_confirm_hold_update(&ui.confirm_hold, false, confirm_now_ms, DANGER_CONFIRM_HOLD_MS);
                    ui.model.confirm_hold_progress = 0;
                    handle_overlay_input(&ui, down);
                } else {
                    bool holding_now = pad_confirm_held || touch_confirm_held;
                    bool was_holding = ui.confirm_hold.holding;
                    bool completed = ptc_ui_confirm_hold_update(&ui.confirm_hold,
                            holding_now, confirm_now_ms, DANGER_CONFIRM_HOLD_MS);
                    uint16_t cur_progress = ptc_ui_confirm_hold_progress(
                        &ui.confirm_hold, confirm_now_ms, DANGER_CONFIRM_HOLD_MS);
                    ui.model.confirm_hold_progress = cur_progress;

                    if (completed) {
                        ptc_audio_stop();
                        ptc_audio_play(PTC_SE_HOLD_CONFIRM);
                        confirm_operation(&ui);
                        ptc_ui_confirm_hold_update(&ui.confirm_hold, false, confirm_now_ms, DANGER_CONFIRM_HOLD_MS);
                        ui.model.confirm_hold_progress = 0;
                    } else if (holding_now && !was_holding) {
                        /* Start continuous rising charge-up cue immediately upon pressing down */
                        ptc_audio_play(PTC_SE_HOLD_CHARGE);
                    } else if (!holding_now && was_holding) {
                        /* Cut off charge audio immediately when releasing before completion */
                        ptc_audio_stop();
                    }
                }
            } else {
                if (ui.confirm_hold.holding || ui.model.confirm_hold_progress > 0) {
                    ptc_audio_stop();
                }
                ptc_ui_confirm_hold_update(&ui.confirm_hold, false, ptc_ui_anim_now_ms(), DANGER_CONFIRM_HOLD_MS);
                ui.model.confirm_hold_progress = 0;
                handle_overlay_input(&ui, down);
            }
            if (ui.model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
                HidAnalogStickState r_stick = right_stick;
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
                    ptc_audio_play(PTC_SE_TOGGLE);
                }
                ui.r_stick_prev_h_dir = h_dir;

                if (v_dir != 0) {
                    int step = ptc_ui_value_repeat_update(&ui.r_stick_repeat, v_dir,
                        ui.model.duration_field == PTC_UI_DURATION_HOURS, ptc_ui_anim_now_ms());
                    ui.model.duration_scroll_dir = (int8_t)v_dir;
                    if (step != 0) {
                        int magnitude = step < 0 ? -step : step;
                        ui.model.duration_step_feedback = (uint8_t)magnitude;
                        if (ptc_ui_duration_step_field(&ui.model, step)) {
                            ptc_audio_play(PTC_SE_STEP);
                        }
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
        } else {
            running = handle_page_input(&ui, down, held, custom_combo_candidate, parent_minus_released);
        }

        if (touch_active) {
            if (!touch_down) {
                details_touch_y = -1;
                details_touch_drag = ui.model.overlay == PTC_UI_OVERLAY_HOME_DETAILS &&
                    ui.model.home_details_page == 1 && ui.model.home_details_data_expanded &&
                    ptc_ui_rect_contains(ptc_ui_home_details_body_rect(), touch_x, touch_y) &&
                    !ptc_ui_rect_contains(ptc_ui_home_details_action_rect(&ui.model, 0), touch_x, touch_y);
                if (details_touch_drag) details_touch_y = touch_y;
                touch_down = true;
                if (!details_touch_drag && !(ui.model.overlay == PTC_UI_OVERLAY_CONFIRM && ui.model.confirm_hold_required &&
                      ptc_ui_rect_contains(ptc_ui_confirm_rect(ui.model.overlay), touch_x, touch_y))) {
                    handle_touch(&ui, touch_x, touch_y);
                }
            } else if (details_touch_drag && details_touch_y >= 0) {
                ptc_ui_home_details_scroll(&ui.model, details_touch_y - touch_y);
                details_touch_y = touch_y;
            }
        } else {
            touch_down = false;
            details_touch_drag = false;
        }

        if (ui.exit_requested) {
            running = false;
        }

        background_poll_elapsed_ms += INPUT_LOOP_MS;
#ifdef PLAYWISE_EDEN
        ptc_eden_trace_stage(PTC_EDEN_TRACE_DISPATCH_DONE, 0);
#endif
        if (background_poll_elapsed_ms >= BACKGROUND_POLL_INTERVAL_MS) {
#ifdef PLAYWISE_EDEN
            ptc_eden_trace_stage(PTC_EDEN_TRACE_TICK_BEGIN, 0);
            if (ptc_eden_runtime_tick(&eden_runtime)) eden_status_dirty = true;
            ptc_eden_trace_stage(PTC_EDEN_TRACE_TICK_END, 0);
#else
            poll_hot_reload(&ui);
            poll_online_update(&ui);
#endif
            poll_pending_redemption(&ui);
#ifdef PLAYWISE_EDEN
            ptc_eden_trace_stage(PTC_EDEN_TRACE_POLL_BEGIN, 0);
#endif
            poll_result(&ui, false);
#ifdef PLAYWISE_EDEN
            ptc_eden_trace_stage(PTC_EDEN_TRACE_POLL_END, 0);
#endif
            refresh_setup_activation(&ui);
#ifdef PLAYWISE_EDEN
            if (eden_status_dirty && !ui.waiting && ui.model.overlay == PTC_UI_OVERLAY_NONE &&
                ui.model.view != PTC_UI_SETUP) {
                submit_status(&ui);
                eden_status_dirty = false;
            }
#endif
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

#ifndef PLAYWISE_EDEN
    ptc_online_update_exit(&ui.updater);
#endif
    if (!ui.waiting && ui.config_stage_id[0]) config_backup_action(&ui, 15);
    appletUnhook(&hook_cookie);
    ptc_audio_exit();
    ptc_ui_graphics_exit();
    free(ui.calendar_view_data);
#ifndef PLAYWISE_EDEN
    ptc_ui_status_trace_close(&ui.status_trace, armTicksToNs(armGetSystemTick()));
    ptc_hot_reload_exit(&ui.hot_reload);
    ptc_switch_ipc_client_exit(&ui.ipc);
#endif
    return 0;
}
