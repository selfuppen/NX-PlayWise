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
    ptc_hot_reload_init(&ui.hot_reload);
    ptc_hot_reload_recover_startup(&ui.hot_reload);
#endif
#ifdef PLAYWISE_EDEN
    /* Seeds the live root files, so the materialization below finds them all
       present and succeeds without a packaged defaults/ directory. Keep this
       call ahead of it. */
    ui.model.eden_mode_controls = true;
    eden_runtime_ready = ptc_eden_runtime_init(&eden_runtime, ptc_fs_storage_as_storage(&fs));
#endif
    install_defaults_ready = ptc_install_materialize_defaults(ptc_fs_storage_as_storage(&fs), APP_ROOT);
    ptc_companion_file_client_init(&ui.client, APP_ROOT, ptc_fs_storage_as_storage(&fs));
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
        bool custom_combo_held;
        bool custom_combo_triggered;
        bool custom_combo_candidate;
        bool touch_active;
        int touch_x = -1;
        int touch_y = -1;
        if (ui.theme_refresh_pending) {
            refresh_theme(&ui);
            refresh_language(&ui);
        }
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
                ptc_audio_play(PTC_SE_CANCEL);
                running = false;
            } else if (ui.minus_pending && !(held & HidNpadButton_Minus)) {
                ui.minus_pending = false;
                ptc_audio_play(PTC_SE_CONFIRM);
                enter_parent_area(&ui);
            } else if (down & HidNpadButton_A) {
                if (ui.waiting) {
                    ptc_audio_play(PTC_SE_ERROR);
                    snprintf(ui.model.message, sizeof(ui.model.message), ptc_ui_text(PTC_UI_T_PLEASE_WAIT_FOR_THE_CURRENT_OPERATION_TO));
                } else {
                    ptc_audio_play(PTC_SE_CONFIRM);
                    open_offline_code_input(&ui);
                }
            } else if (down & HidNpadButton_Y) {
                ptc_audio_play(PTC_SE_CONFIRM);
                submit_status(&ui);
            } else if (down & HidNpadButton_Plus) {
                ptc_audio_play(PTC_SE_POPUP);
                ptc_ui_open_home_details(&ui.model);
            } else if (down & HidNpadButton_X) {
                if (ui.model.disable_flag_present) {
                    ptc_audio_play(PTC_SE_ERROR);
                    snprintf(ui.model.message, sizeof(ui.model.message), ptc_ui_text(PTC_UI_T_CONTROL_HAS_BEEN_DEACTIVATED_AND_AUTONOMOUS_BUFFERING));
                } else if (ui.model.daily_buffer_available && !ui.waiting) {
                    ptc_audio_play(PTC_SE_CLAIM_BUFFER);
                    submit_transport_empty(&ui, "claim_daily_buffer",
                        ptc_ui_text(PTC_UI_T_IS_RECEIVING_TODAY_S_INDEPENDENT_BUFFER), ptc_ui_text(PTC_UI_T_FAILED_TO_RECEIVE_TODAY_S_INDEPENDENT_BUFFERING));
                } else if (ui.model.daily_buffer_claimed) {
                    ptc_audio_play(PTC_SE_ERROR);
                    snprintf(ui.model.message, sizeof(ui.model.message),
                        ptc_ui_text(PTC_UI_T_THE_BUFFER_HAS_BEEN_USED_TODAY_AND));
                } else {
                    ptc_audio_play(PTC_SE_ERROR);
                    snprintf(ui.model.message, sizeof(ui.model.message),
                        ptc_ui_text(PTC_UI_T_THERE_ARE_NO_AUTONOMOUS_BUFFERS_TO_CLAIM));
                }
            }
        } else if (ui.model.view == PTC_UI_SETUP) {
            if (down & HidNpadButton_Y) {
                ptc_audio_play(PTC_SE_CONFIRM);
                submit_status(&ui);
            } else {
                handle_setup_input(&ui, down, held);
            }
        } else if (ui.model.view == PTC_UI_ERROR) {
            if (down & HidNpadButton_A) {
                ptc_audio_play(PTC_SE_CONFIRM);
                retry_error(&ui);
            } else if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                ptc_audio_play(PTC_SE_CANCEL);
                enter_child_area(&ui);
            }
        } else {
            if ((down & HidNpadButton_X) && ptc_ui_open_notice_details(&ui.model)) {
            } else if ((down & HidNpadButton_Plus) && ui.model.parent_page == PTC_UI_PARENT_TODAY) {
                ptc_ui_open_home_details(&ui.model);
            } else if (ui.model.parent_footer_focused) {
                if (down & HidNpadButton_B) {
                    ptc_audio_play(PTC_SE_CANCEL);
                    request_parent_navigation(&ui, -1, true);
                } else if (down & HidNpadButton_L &&
                           !(ui.model.parent_page == PTC_UI_PARENT_PLAN && ui.model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
                    ptc_audio_play(PTC_SE_TAB);
                    request_parent_navigation(&ui,
                        (ui.model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
                } else if (down & HidNpadButton_R &&
                           !(ui.model.parent_page == PTC_UI_PARENT_PLAN && ui.model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
                    ptc_audio_play(PTC_SE_TAB);
                    request_parent_navigation(&ui,
                        (ui.model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
                } else if (down & HidNpadButton_Up) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ui.model.parent_footer_focused = false;
                    ui.model.selected_index = ui.model.parent_content_selection;
                } else if (down & HidNpadButton_Left) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ui.model.parent_footer_selection = 0;
                } else if (down & HidNpadButton_Right) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ui.model.parent_footer_selection =
                        ptc_ui_parent_status_alert_visible(&ui.model) ? 1 : 0;
                } else if (down & HidNpadButton_Y) {
                    ptc_audio_play(PTC_SE_CONFIRM);
                    refresh_disable_flag(&ui);
                    submit_status(&ui);
                } else if (down & HidNpadButton_A) {
                    ptc_audio_play(PTC_SE_CONFIRM);
                    if (ui.model.parent_footer_selection == 0) submit_status(&ui);
                    else activate_parent_status(&ui);
                }
            } else if (ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_WEEKLY) {
                PtcDayRule *day = &ui.model.draft_week[ui.model.editor_index];
                if (ui.waiting) {
                    if (down) {
                        ptc_audio_play(PTC_SE_ERROR);
                        snprintf(ui.model.message, sizeof(ui.model.message),
                                 ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_WEEKLY_PLAN_IS));
                    }
                } else if (down & HidNpadButton_B) {
                    ptc_audio_play(PTC_SE_CANCEL);
                    request_parent_navigation(&ui, -1, true);
                } else if (down & (HidNpadButton_L | HidNpadButton_R)) {
                    ptc_audio_play(PTC_SE_TAB);
                    request_parent_navigation(&ui, -1, true);
                } else if (down & HidNpadButton_Left) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ptc_ui_move_weekly_focus(&ui.model, -1, 0);
                } else if (down & HidNpadButton_Right) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ptc_ui_move_weekly_focus(&ui.model, 1, 0);
                } else if (down & HidNpadButton_X) {
                    if (!weekly_editing_blocked(&ui)) {
                        ptc_audio_play(PTC_SE_TOGGLE);
                        day->mode = ptc_ui_next_rule_mode(day->mode);
                        update_weekly_dirty(&ui);
                    } else {
                        ptc_audio_play(PTC_SE_ERROR);
                    }
                    if (!ui.model.disable_flag_present && day->mode == PTC_RULE_MODE_LIMIT) {
                        snprintf(ui.model.message, sizeof(ui.model.message),
                                 ptc_ui_text(PTC_UI_T_PREVIOUS_DAILY_LIMIT_RESTORED_U_MIN), (unsigned int)day->minutes);
                    }
                } else if (down & HidNpadButton_Up) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ptc_ui_move_weekly_focus(&ui.model, 0, -1);
                } else if (down & HidNpadButton_Down) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    if (ui.model.selected_index != 0) {
                        ui.model.parent_content_selection = ui.model.selected_index;
                        ui.model.parent_footer_focused = true;
                        ui.model.parent_footer_selection =
                            ptc_ui_parent_status_alert_visible(&ui.model) ? 1 : 0;
                    } else {
                        ptc_ui_move_weekly_focus(&ui.model, 0, 1);
                    }
                } else if (down & HidNpadButton_Y) {
                    ptc_audio_play(PTC_SE_CONFIRM);
                    refresh_disable_flag(&ui);
                    submit_status(&ui);
                } else if (down & HidNpadButton_A) {
                    if (ui.model.selected_index == 2) {
                        if (weekly_editing_blocked(&ui)) {
                            ptc_audio_play(PTC_SE_ERROR);
                            snprintf(ui.model.message, sizeof(ui.model.message), ptc_ui_text(PTC_UI_T_IS_IN_EMERGENCY_DEACTIVATION_BATCH_OPERATIONS_ARE));
                        } else {
                            ptc_audio_play(PTC_SE_CONFIRM);
                            ui.model.overlay = PTC_UI_OVERLAY_WEEKLY_BULK;
                            ui.model.overlay_selection = 0;
                            snprintf(ui.model.overlay_title, sizeof(ui.model.overlay_title), ptc_ui_text(PTC_UI_T_BATCH_QUICK_OPERATIONS));
                            snprintf(ui.model.overlay_body, sizeof(ui.model.overlay_body), ptc_ui_text(PTC_UI_T_COPIES_THE_COMPLETE_DRAFT_RULE_FOR_THE));
                        }
                    } else if (ui.model.selected_index == 3) {
                        ptc_audio_play(PTC_SE_CANCEL);
                        if (ui.model.weekly_dirty) {
                            memcpy(ui.model.draft_week, ui.model.current_week, sizeof(ui.model.draft_week));
                            ui.model.weekly_dirty = false;
                            snprintf(ui.model.message, sizeof(ui.model.message), ptc_ui_text(PTC_UI_T_UNSAVED_WEEKLY_SCHEDULE_MODIFICATIONS_ABANDONED));
                        } else {
                            snprintf(ui.model.message, sizeof(ui.model.message), ptc_ui_text(PTC_UI_T_THE_WEEKLY_PLAN_HAS_NOT_BEEN_MODIFIED));
                        }
                    } else if (weekly_editing_blocked(&ui)) {
                        /* Focus remains movable while emergency stop makes the editor read-only. */
                        ptc_audio_play(PTC_SE_ERROR);
                    } else if (ui.model.selected_index == 1) {
                        ptc_audio_play(PTC_SE_TOGGLE);
                        day->mode = ptc_ui_next_rule_mode(day->mode);
                        update_weekly_dirty(&ui);
                        if (day->mode == PTC_RULE_MODE_LIMIT) {
                            snprintf(ui.model.message, sizeof(ui.model.message),
                                     ptc_ui_text(PTC_UI_T_PREVIOUS_DAILY_LIMIT_RESTORED_U_MIN), (unsigned int)day->minutes);
                        }
                    } else if (ui.model.selected_index == 4) {
                        ptc_audio_play(PTC_SE_CONFIRM);
                        save_weekly_from_page(&ui);
                    } else if (day->mode == PTC_RULE_MODE_LIMIT) {
                        ptc_audio_play(PTC_SE_CONFIRM);
                        edit_weekly_minutes(&ui);
                    } else {
                        ptc_audio_play(PTC_SE_ERROR);
                        snprintf(ui.model.message, sizeof(ui.model.message),
                                 ptc_ui_text(PTC_UI_T_THIS_DAY_IS_UNLIMITED_SELECT_CHANGE_MODE));
                    }
                } else if (down & HidNpadButton_Plus) {
                    ui.model.selected_index = 4;
                    if (!weekly_editing_blocked(&ui)) {
                        ptc_audio_play(PTC_SE_CONFIRM);
                        save_weekly_from_page(&ui);
                    } else {
                        ptc_audio_play(PTC_SE_ERROR);
                    }
                } else if (down & HidNpadButton_ZL) {
                    ptc_audio_play(PTC_SE_CANCEL);
                    ui.model.selected_index = 3;
                    if (ui.model.weekly_dirty) {
                        memcpy(ui.model.draft_week, ui.model.current_week, sizeof(ui.model.draft_week));
                        ui.model.weekly_dirty = false;
                        snprintf(ui.model.message, sizeof(ui.model.message), ptc_ui_text(PTC_UI_T_UNSAVED_WEEKLY_SCHEDULE_MODIFICATIONS_ABANDONED));
                    } else {
                        snprintf(ui.model.message, sizeof(ui.model.message), ptc_ui_text(PTC_UI_T_THE_WEEKLY_PLAN_HAS_NOT_BEEN_MODIFIED));
                    }
                }
            } else if (ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_BEDTIME) {
                PtcBedtimePolicy *draft = &ui.model.draft_bedtime_policy;
                if (ui.waiting) {
                    if (down) {
                        ptc_audio_play(PTC_SE_ERROR);
                        snprintf(ui.model.message, sizeof(ui.model.message),
                            ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_BEDTIME_SETTINGS_ARE));
                    }
                } else if (down & HidNpadButton_B) {
                    ptc_audio_play(PTC_SE_CANCEL);
                    request_bedtime_leave(&ui, -1, true);
                } else if (down & HidNpadButton_Y) {
                    ptc_audio_play(PTC_SE_CONFIRM);
                    handle_today_action_ready(&ui, PTC_UI_OPERATION_SKIP_BEDTIME);
                } else if (down & HidNpadButton_X) {
                    ptc_audio_play(PTC_SE_CONFIRM);
                    request_clear_bedtime_skip(&ui);
                } else if (down & HidNpadButton_L) {
                    ptc_audio_play(PTC_SE_TAB);
                    select_bedtime_section(&ui, ui.model.bedtime_section - 1);
                } else if (down & HidNpadButton_R) {
                    ptc_audio_play(PTC_SE_TAB);
                    select_bedtime_section(&ui, ui.model.bedtime_section + 1);
                } else if (down & HidNpadButton_Up) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ptc_ui_move_bedtime_focus(&ui.model, 0, -1);
                } else if (down & HidNpadButton_Down) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ptc_ui_move_bedtime_focus(&ui.model, 0, 1);
                } else if (down & HidNpadButton_Left) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ptc_ui_move_bedtime_focus(&ui.model, -1, 0);
                } else if (down & HidNpadButton_Right) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ptc_ui_move_bedtime_focus(&ui.model, 1, 0);
                } else if (down & HidNpadButton_Minus) {
                    if (!ui.model.disable_flag_present) {
                        ptc_audio_play(PTC_SE_TOGGLE);
                        draft->enabled = !draft->enabled;
                        update_bedtime_dirty(&ui);
                        snprintf(ui.model.message, sizeof(ui.model.message), ptc_ui_text(PTC_UI_T_BEDTIME_MASTER_SWITCH_DRAFT_IS_S_SAVE),
                            draft->enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF));
                    } else {
                        ptc_audio_play(PTC_SE_ERROR);
                    }
                } else if (ui.model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED &&
                           !ui.model.bedtime_section_focused &&
                           (down & (HidNpadButton_ZL | HidNpadButton_ZR))) {
                    int direction = down & HidNpadButton_ZR ? 1 : -1;
                    int date_step = 7;
                    ptc_audio_play(PTC_SE_STEP);
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
                } else if ((down & HidNpadButton_A) && ui.model.bedtime_master_focused) {
                    if (!ui.model.disable_flag_present) {
                        ptc_audio_play(PTC_SE_TOGGLE);
                        draft->enabled = !draft->enabled;
                        update_bedtime_dirty(&ui);
                        snprintf(ui.model.message, sizeof(ui.model.message),
                                 ptc_ui_text(PTC_UI_T_BEDTIME_MASTER_SWITCH_DRAFT_IS_S_SAVE), draft->enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF));
                    } else {
                        ptc_audio_play(PTC_SE_ERROR);
                    }
                } else if ((down & HidNpadButton_A) && !ui.model.bedtime_section_focused) {
                    if (ui.model.bedtime_section == PTC_UI_BEDTIME_WEEKLY) {
                        if (ui.model.selected_index < 7) {
                            ptc_audio_play(PTC_SE_CONFIRM);
                            int day = ptc_ui_weekday_for_display_slot(ui.model.selected_index);
                            ui.model.bedtime_editor_day = day;
                            open_bedtime_window_editor(&ui, day);
                        } else if (ui.model.selected_index == 7 || ui.model.selected_index == 8) {
                            ptc_audio_play(PTC_SE_CONFIRM);
                            ui.model.overlay = PTC_UI_OVERLAY_BEDTIME_BULK;
                            ui.model.overlay_selection = ui.model.selected_index - 7;
                            snprintf(ui.model.overlay_title, sizeof(ui.model.overlay_title), ptc_ui_text(PTC_UI_T_COPY_WEEKLY_BEDTIME_WINDOW));
                            snprintf(ui.model.overlay_body, sizeof(ui.model.overlay_body),
                                ptc_ui_text(PTC_UI_T_COPIES_THE_COMPLETE_SWITCHES_AND_TIME_OF));
                        } else if (ui.model.selected_index == 9) {
                            ptc_audio_play(PTC_SE_CANCEL);
                            discard_bedtime_draft(&ui);
                        } else {
                            ptc_audio_play(PTC_SE_CONFIRM);
                            save_bedtime_from_page(&ui);
                        }
                    } else if (ui.model.bedtime_section == PTC_UI_BEDTIME_CALENDAR) {
                        if (ui.model.selected_index == 0) {
                            ptc_audio_play(PTC_SE_TOGGLE);
                            draft->calendar_enabled = !draft->calendar_enabled;
                            update_bedtime_dirty(&ui);
                        } else if (ui.model.selected_index <= 2) {
                            ptc_audio_play(PTC_SE_CONFIRM);
                            open_bedtime_special_editor(&ui, ui.model.selected_index - 1);
                        } else if (ui.model.selected_index == 3) {
                            ptc_audio_play(PTC_SE_CANCEL);
                            discard_bedtime_draft(&ui);
                        } else {
                            ptc_audio_play(PTC_SE_CONFIRM);
                            save_bedtime_from_page(&ui);
                        }
                    } else {
                        if (ui.model.selected_index == 0) {
                            ptc_audio_play(PTC_SE_TOGGLE);
                            draft->scheduled_override.present = !draft->scheduled_override.present;
                            if (draft->scheduled_override.start_day_index < ui.model.day_index) {
                                draft->scheduled_override.start_day_index = ui.model.day_index;
                                draft->scheduled_override.end_day_index = ui.model.day_index;
                            }
                            update_bedtime_dirty(&ui);
                        } else if (ui.model.selected_index == 1) {
                            ptc_audio_play(PTC_SE_CONFIRM);
                            if (edit_date_range_start(&ui, &draft->scheduled_override.start_day_index,
                                                      &draft->scheduled_override.end_day_index))
                                update_bedtime_dirty(&ui);
                        } else if (ui.model.selected_index == 2) {
                            ptc_audio_play(PTC_SE_CONFIRM);
                            if (edit_date_range_span(&ui, draft->scheduled_override.start_day_index,
                                                     &draft->scheduled_override.end_day_index))
                                update_bedtime_dirty(&ui);
                        } else if (ui.model.selected_index == 3) {
                            ptc_audio_play(PTC_SE_CONFIRM);
                            open_bedtime_special_editor(&ui, 2);
                        } else if (ui.model.selected_index == 4) {
                            ptc_audio_play(PTC_SE_CANCEL);
                            discard_bedtime_draft(&ui);
                        } else if (ui.model.selected_index == 5) {
                            ptc_audio_play(PTC_SE_CONFIRM);
                            save_bedtime_from_page(&ui);
                        }
                    }
                } else if (down & HidNpadButton_Plus) {
                    ptc_audio_play(PTC_SE_CONFIRM);
                    save_bedtime_from_page(&ui);
                } else if (down & HidNpadButton_ZL) {
                    ptc_audio_play(PTC_SE_CANCEL);
                    discard_bedtime_draft(&ui);
                }
            } else if (ui.model.parent_page == PTC_UI_PARENT_PLAN && ui.model.plan_page == PTC_UI_PLAN_PAGE_DOCK) {
                if (!ui.waiting) {
                    if (down & HidNpadButton_B) dock_page_action(&ui, 5, 0);
                    else if (down & HidNpadButton_Y) dock_page_action(&ui, 6, 0);
                    else if (down & HidNpadButton_X) dock_page_action(&ui, 4, 0);
                    else if (down & HidNpadButton_Plus) dock_page_action(&ui, 3, 0);
                    else if (down & HidNpadButton_Up) {
                        int focus = ui.model.dock_field_focus;
                        if (focus == 7 || focus == 8) ui.model.dock_field_focus = focus == 7 ? 3 : 4;
                        else if (focus == 3 || focus == 4) ui.model.dock_field_focus = 2;
                        else if (focus == 2) ui.model.dock_field_focus = 1;
                        else if (focus == 1) ui.model.dock_field_focus = 0;
                        else if (focus == 0) ui.model.dock_field_focus = ui.model.eden_mode_controls ? 7 : 3;
                    }
                    else if (down & HidNpadButton_Down) {
                        int focus = ui.model.dock_field_focus;
                        if (focus == 0) ui.model.dock_field_focus = 1;
                        else if (focus == 1) ui.model.dock_field_focus = 2;
                        else if (focus == 2) ui.model.dock_field_focus = 3;
                        else if (focus == 3 || focus == 4) {
                            if (ui.model.eden_mode_controls) ui.model.dock_field_focus = focus == 3 ? 7 : 8;
                            else ui.model.dock_field_focus = 0;
                        }
                        else if (focus == 7 || focus == 8) ui.model.dock_field_focus = 0;
                    }
                    else if (down & HidNpadButton_Left) {
                        int focus = ui.model.dock_field_focus;
                        if (focus == 2) dock_page_action(&ui, 2, -1);
                        else if (focus == 4) ui.model.dock_field_focus = 3;
                        else if (focus == 8) ui.model.dock_field_focus = 7;
                    }
                    else if (down & HidNpadButton_Right) {
                        int focus = ui.model.dock_field_focus;
                        if (focus == 2) dock_page_action(&ui, 2, 1);
                        else if (focus == 3) ui.model.dock_field_focus = 4;
                        else if (focus == 7) ui.model.dock_field_focus = 8;
                    }
                    else if (down & HidNpadButton_A) dock_page_action(&ui, ui.model.dock_field_focus, 0);
                }
            } else if (ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_EYE_CARE) {
                PtcEyeCarePolicy *draft = &ui.model.draft_eye_care_policy;
                bool resting = (strcmp(ui.model.eye_care_phase, "resting") == 0);
                int focus_count = resting ? 5 : 4;
                if (ui.waiting) {
                    if (down) {
                        ptc_audio_play(PTC_SE_ERROR);
                        snprintf(ui.model.message, sizeof(ui.model.message),
                                 ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_CURRENT_OPERATION_IS));
                    }
                } else if (down & HidNpadButton_B) {
                    ptc_audio_play(PTC_SE_CANCEL);
                    ui.model.draft_eye_care_policy = ui.model.eye_care_policy;
                    ui.model.eye_care_dirty = false;
                    ui.model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
                    ui.model.selected_index = 4;
                } else if (down & (HidNpadButton_L | HidNpadButton_R)) {
                    ptc_audio_play(PTC_SE_CANCEL);
                    ui.model.draft_eye_care_policy = ui.model.eye_care_policy;
                    ui.model.eye_care_dirty = false;
                    ui.model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
                    ui.model.selected_index = 4;
                } else if (down & HidNpadButton_Up) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ui.model.eye_care_field_focus = (ui.model.eye_care_field_focus + focus_count - 1) % focus_count;
                } else if (down & HidNpadButton_Down) {
                    ptc_audio_play(PTC_SE_FOCUS);
                    ui.model.eye_care_field_focus = (ui.model.eye_care_field_focus + 1) % focus_count;
                } else if (down & (HidNpadButton_Left | HidNpadButton_Right | HidNpadButton_ZL | HidNpadButton_ZR)) {
                    int delta = 0;
                    if (down & HidNpadButton_Left) delta = -1;
                    else if (down & HidNpadButton_Right) delta = 1;
                    else if (down & HidNpadButton_ZL) delta = -10;
                    else if (down & HidNpadButton_ZR) delta = 10;

                    if (ui.model.eye_care_field_focus == 0) {
                        ptc_audio_play(PTC_SE_TOGGLE);
                        draft->enabled = !draft->enabled;
                        ui.model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui.model);
                    } else if (ui.model.eye_care_field_focus == 1) {
                        ptc_audio_play(PTC_SE_STEP);
                        draft->play_minutes = ptc_ui_adjust_minutes(draft->play_minutes, delta, 1, 240);
                        ui.model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui.model);
                    } else if (ui.model.eye_care_field_focus == 2) {
                        int rest_delta = delta;
                        if (down & HidNpadButton_ZL) rest_delta = -5;
                        else if (down & HidNpadButton_ZR) rest_delta = 5;
                        ptc_audio_play(PTC_SE_STEP);
                        draft->rest_minutes = ptc_ui_adjust_minutes(draft->rest_minutes, rest_delta, 1, 60);
                        ui.model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui.model);
                    }
                } else if (down & HidNpadButton_A) {
                    if (ui.model.eye_care_field_focus == 0) {
                        ptc_audio_play(PTC_SE_TOGGLE);
                        draft->enabled = !draft->enabled;
                        ui.model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui.model);
                    } else if (ui.model.eye_care_field_focus == 1) {
                        ptc_audio_play(PTC_SE_POPUP);
                        ptc_ui_numpad_open(&ui.model, PTC_UI_NUMPAD_EYE_CARE_PLAY, PTC_UI_OVERLAY_NONE,
                                           ptc_ui_text(PTC_UI_T_EYE_CARE_PLAY),
                                           ptc_ui_text(PTC_UI_T_EYE_CARE_PLAY_SUBTITLE),
                                           3, 1, 240, draft->play_minutes);
                    } else if (ui.model.eye_care_field_focus == 2) {
                        ptc_audio_play(PTC_SE_POPUP);
                        ptc_ui_numpad_open(&ui.model, PTC_UI_NUMPAD_EYE_CARE_REST, PTC_UI_OVERLAY_NONE,
                                           ptc_ui_text(PTC_UI_T_EYE_CARE_REST),
                                           ptc_ui_text(PTC_UI_T_EYE_CARE_REST_SUBTITLE),
                                           2, 1, 60, draft->rest_minutes);
                    } else if (ui.model.eye_care_field_focus == 3) {
                        ptc_audio_play(PTC_SE_CONFIRM);
                        submit_eye_care_policy(&ui);
                    } else if (ui.model.eye_care_field_focus == 4) {
                        ptc_audio_play(PTC_SE_CONFIRM);
                        submit_eye_care_skip(&ui);
                    }
                } else if (down & HidNpadButton_Minus) {
                    ptc_audio_play(PTC_SE_TOGGLE);
                    draft->enabled = !draft->enabled;
                    ui.model.eye_care_dirty = ptc_ui_eye_care_dirty(&ui.model);
                } else if (down & HidNpadButton_Plus) {
                    ptc_audio_play(PTC_SE_CONFIRM);
                    submit_eye_care_policy(&ui);
                } else if (down & HidNpadButton_X) {
                    if (resting) {
                        ptc_audio_play(PTC_SE_CONFIRM);
                        submit_eye_care_skip(&ui);
                    } else {
                        ptc_audio_play(PTC_SE_ERROR);
                    }
                } else if (down & HidNpadButton_Y) {
                    ptc_audio_play(PTC_SE_CONFIRM);
                    refresh_disable_flag(&ui);
                    submit_status(&ui);
                }
            } else if (ui.waiting && ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
                if (down) {
                    ptc_audio_play(PTC_SE_ERROR);
                    snprintf(ui.model.message, sizeof(ui.model.message),
                             ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_NATIONAL_HOLIDAY_SETTINGS));
                }
            } else if (down & HidNpadButton_B) {
                ptc_audio_play(PTC_SE_CANCEL);
                request_parent_navigation(&ui, -1, true);
            } else if (down & HidNpadButton_L &&
                       !(ui.model.parent_page == PTC_UI_PARENT_PLAN && ui.model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
                ptc_audio_play(PTC_SE_TAB);
                request_parent_navigation(&ui,
                    (ui.model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
            } else if (down & HidNpadButton_R &&
                       !(ui.model.parent_page == PTC_UI_PARENT_PLAN && ui.model.plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
                ptc_audio_play(PTC_SE_TAB);
                request_parent_navigation(&ui,
                    (ui.model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
            } else if (down & HidNpadButton_Left) {
                ptc_audio_play(PTC_SE_FOCUS);
                ptc_ui_move_parent_selection(&ui.model, -1, 0);
            } else if (down & HidNpadButton_Right) {
                ptc_audio_play(PTC_SE_FOCUS);
                ptc_ui_move_parent_selection(&ui.model, 1, 0);
            } else if (down & HidNpadButton_Up) {
                ptc_audio_play(PTC_SE_FOCUS);
                ptc_ui_move_parent_selection(&ui.model, 0, -1);
            } else if (down & HidNpadButton_Down) {
                ptc_audio_play(PTC_SE_FOCUS);
                ptc_ui_move_parent_selection(&ui.model, 0, 1);
            } else if (down & HidNpadButton_Y) {
                ptc_audio_play(PTC_SE_CONFIRM);
                refresh_disable_flag(&ui);
                submit_status(&ui);
            } else if (down & HidNpadButton_X && ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
                if (ui.model.disable_flag_present) {
                    ptc_audio_play(PTC_SE_ERROR);
                    snprintf(ui.model.message, sizeof(ui.model.message), ptc_ui_text(PTC_UI_T_IN_EMERGENCY_DEACTIVATION_THE_RULES_ARE_TEMPORARILY));
                } else if (ui.model.selected_index == 1 ||
                           (ui.model.selected_index != 2 && ui.model.holiday_last_rule == 0)) {
                    ptc_audio_play(PTC_SE_TOGGLE);
                    ui.model.holiday_last_rule = 0;
                    ui.model.draft_holiday_rule.mode = ptc_ui_next_rule_mode(ui.model.draft_holiday_rule.mode);
                    update_holiday_dirty(&ui);
                } else {
                    ptc_audio_play(PTC_SE_TOGGLE);
                    ui.model.holiday_last_rule = 1;
                    ui.model.draft_makeup_workday_rule.mode = ptc_ui_next_rule_mode(ui.model.draft_makeup_workday_rule.mode);
                    update_holiday_dirty(&ui);
                }
            } else if (down & HidNpadButton_Plus && ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
                ptc_audio_play(PTC_SE_CONFIRM);
                ui.model.selected_index = 5;
                save_holiday_from_page(&ui);
            } else if (down & HidNpadButton_ZL && ui.model.parent_page == PTC_UI_PARENT_PLAN &&
                       ui.model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
                ptc_audio_play(PTC_SE_CANCEL);
                ui.model.selected_index = 4;
                discard_holiday_draft(&ui);
            } else if (down & HidNpadButton_A) {
                if (ui.waiting && !ptc_ui_parent_read_only_action(&ui.model, ui.model.selected_index)) {
                    ptc_audio_play(PTC_SE_ERROR);
                    snprintf(ui.model.message, sizeof(ui.model.message), ptc_ui_text(PTC_UI_T_PLEASE_WAIT_UNTIL_THE_CURRENT_OPERATION_IS));
                } else if (ui.model.parent_footer_focused) {
                    ptc_audio_play(PTC_SE_CONFIRM);
                    activate_parent_status(&ui);
                } else if (ui.model.parent_page == PTC_UI_PARENT_SUPPORT &&
                           ui.model.selected_index == 6 + ui.model.recent_event_count) {
                    ptc_ui_open_support_guide(&ui.model);
                } else if (ui.model.parent_page == PTC_UI_PARENT_SUPPORT && ui.model.selected_index >= 6) {
                    int visible_index = ui.model.selected_index - 6;
                    int event_index = ui.model.recent_event_count - 1 - visible_index;
                    if (event_index >= 0 && event_index < ui.model.recent_event_count) {
                        ptc_audio_play(PTC_SE_POPUP);
                        ui.model.overlay = PTC_UI_OVERLAY_SUPPORT_EVENT;
                        ui.model.overlay_selection = event_index;
                        snprintf(ui.model.overlay_title, sizeof(ui.model.overlay_title), ptc_ui_text(PTC_UI_T_RECENT_EVENT_DETAILS));
                        snprintf(ui.model.overlay_body, sizeof(ui.model.overlay_body),
                                 ptc_ui_text(PTC_UI_T_THE_PARENT_AREA_IS_PIN_AUTHENTICATED_THE));
                    }
                } else {
                    ptc_audio_play(PTC_SE_CONFIRM);
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
            if (ptc_eden_runtime_tick(&eden_runtime)) eden_status_dirty = true;
#else
            poll_hot_reload(&ui);
#endif
            poll_pending_redemption(&ui);
            poll_result(&ui, false);
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

    if (!ui.waiting && ui.config_stage_id[0]) config_backup_action(&ui, 15);
    appletUnhook(&hook_cookie);
    ptc_audio_exit();
    ptc_ui_graphics_exit();
    free(ui.calendar_view_data);
#ifndef PLAYWISE_EDEN
    ptc_hot_reload_exit(&ui.hot_reload);
    ptc_switch_ipc_client_exit(&ui.ipc);
#endif
    return 0;
}
