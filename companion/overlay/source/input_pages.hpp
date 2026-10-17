    PtcOverlayGesture gesture_{};

    bool page_hit(PtcOverlayRect rect, int x, int y) const
    {
        return rect.w > 0 && rect.h > 0 && ptc_overlay_rect_contains(rect, x, y);
    }

    bool flow_hit(PtcOverlayRect rect, int x, int y) const
    {
        return rect.w > 0 && rect.h > 0 && ptc_overlay_scrolled_hit(page_layout_.viewport,
            rect, static_cast<int>(page_scroll_), x, y);
    }

    // Resolve touch on release. Crossing the drag threshold cancels the click,
    // including gestures that start on a button and finish over another control.
    HidTouchState page_touch(const HidTouchState &raw)
    {
        const bool down = raw.x != 0 || raw.y != 0;
        const int x = raw.x > 400 ? static_cast<int>(raw.x) - 880 : static_cast<int>(raw.x);
        const int y = static_cast<int>(raw.y);
        HidTouchState click{};
        const auto event = ptc_overlay_gesture_update(&gesture_, down, x, y, page_identity(), page_layout_.viewport);
        if (event.scroll_delta) {
            page_scroll_target_ = std::clamp(page_scroll_target_ + event.scroll_delta,
                0.0f, static_cast<float>(page_layout_.scroll_max));
            page_scroll_ = page_scroll_target_;
            ptc_overlay_hold_reset(&confirm_hold_);
        }
        if (event.clicked) { click.x = event.x; click.y = event.y; }
        return click;
    }

    void focus_parent_action()
    {
        page_scroll_target_ = static_cast<float>(ptc_overlay_scroll_to_rect(page_layout_.viewport,
            page_layout_.actions[parent_action_], static_cast<int>(page_scroll_target_), page_layout_.scroll_max));
    }

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &raw_touch,
        HidAnalogStickState left, HidAnalogStickState right) override
    {
        const u64 now = armGetSystemTick();
        int elapsed_ms = 0;
        if (last_input_tick_) {
            const u64 elapsed = armTicksToNs(now - last_input_tick_) / 1000000ULL;
            elapsed_ms = static_cast<int>(std::min(elapsed, static_cast<u64>(std::numeric_limits<int>::max())));
        }
        last_input_tick_ = now;
        const bool requests_enabled = ptc_overlay_request_action_enabled(bridge_->waiting);
        const u64 dpad_down = keysDown & (HidNpadButton_Up | HidNpadButton_Right | HidNpadButton_Down | HidNpadButton_Left);
        u64 stick_keys = 0;
        if (left.x > 16000) stick_keys |= HidNpadButton_Right;
        if (left.x < -16000) stick_keys |= HidNpadButton_Left;
        if (left.y > 16000) stick_keys |= HidNpadButton_Up;
        if (left.y < -16000) stick_keys |= HidNpadButton_Down;
        keysDown |= stick_keys & ~prev_stick_keys_;
        keysHeld |= stick_keys;
        prev_stick_keys_ = stick_keys;
        const auto touch = page_touch(raw_touch);
        const bool tapped = touch.x != 0 || touch.y != 0;
        const int tx = static_cast<int>(touch.x), ty = static_cast<int>(touch.y);
        // PIN keeps both sticks as digit input. Other pages use the right stick
        // for scrolling; explanatory pages also use Up/Down without executing.
        if (parent_view_ != ParentView::Pin && std::abs(right.y) > 16000) {
            const float delta = -static_cast<float>(right.y) / 32768.0f * std::min(elapsed_ms, 50) * 0.35f;
            page_scroll_target_ = std::clamp(page_scroll_target_ + delta, 0.0f, static_cast<float>(page_layout_.scroll_max));
        }
        if (gesture_.dragged && gesture_.active) ptc_overlay_hold_reset(&confirm_hold_);
        prev_touch_down_ = false;
        if (access_recovery_visible())
            return handle_parent_input(keysDown, keysHeld, dpad_down, touch, left, right, elapsed_ms);
        if (success_visible_) {
            if ((tapped && page_hit(page_layout_.primary, tx, ty)) ||
                (keysDown & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))) {
                const bool terminal = !result_pending_;
                success_visible_ = status_expanded_ = keypad_active_ = false;
                ptc_overlay_input_init(input_);
                if (terminal) {
                    (void)ptc_companion_pending_redemption_clear(&bridge_->transport.file);
                    std::memset(&pending_redemption_, 0, sizeof(pending_redemption_));
                    result_failed_ = false;
                }
                reset_page_scroll();
            }
            return true;
        }
        if (preview_ready_) {
            const bool touch_confirm = tapped && page_hit(page_layout_.primary, tx, ty);
            const bool touch_cancel = tapped && page_hit(page_layout_.secondary, tx, ty);
            const bool dangerous = !preview_summary_.remaining_after_available || !preview_summary_.remaining_after_minutes ||
                preview_summary_.converts_unlimited_to_limited;
            if (keysDown & (HidNpadButton_Left | HidNpadButton_Right | HidNpadButton_Up | HidNpadButton_Down)) {
                preview_focus_ = preview_focus_ == 0 ? 1 : 0;
            }
            if ((keysDown & HidNpadButton_B) || touch_cancel ||
                (preview_focus_ == 0 && (keysDown & HidNpadButton_A))) {
                preview_ready_ = preview_changed_ = keypad_active_ = false;
                pending_code_[0] = '\0';
                ptc_overlay_hold_reset(&confirm_hold_);
                ptc_overlay_input_init(input_);
                reset_page_scroll();
            } else if (touch_confirm && dangerous) {
                touch_hold_warning_ = true;
                ptc_overlay_hold_reset(&confirm_hold_);
            } else if (!dangerous && (touch_confirm || (keysDown & HidNpadButton_Plus) ||
                       (preview_focus_ == 1 && (keysDown & HidNpadButton_A)))) {
                touch_hold_warning_ = false;
                ptc_overlay_hold_reset(&confirm_hold_);
                (void)begin_preview_request(true);
            } else if (dangerous && preview_focus_ == 1 && (keysHeld & HidNpadButton_A) && !(gesture_.active && gesture_.dragged)) {
                if (ptc_overlay_hold_update(&confirm_hold_, true, elapsed_ms, 1000)) {
                    touch_hold_warning_ = false;
                    (void)begin_preview_request(true);
                }
            } else ptc_overlay_hold_reset(&confirm_hold_);
            return true;
        }
        if (status_expanded_) {
            if (keysDown & HidNpadButton_Up) {
                if (status_details_focus_ == 0) status_details_focus_ = 1;
                else if (status_details_focus_ == 1) status_details_focus_ = 2;
                if (status_details_focus_ == 1) {
                    page_scroll_target_ = static_cast<float>(ptc_overlay_scroll_to_rect(
                        page_layout_.viewport, page_layout_.status,
                        static_cast<int>(page_scroll_target_), page_layout_.scroll_max));
                }
            } else if (keysDown & HidNpadButton_Down) {
                if (status_details_focus_ == 2) status_details_focus_ = 1;
                else if (status_details_focus_ == 1) status_details_focus_ = 0;
                if (status_details_focus_ == 1) {
                    page_scroll_target_ = static_cast<float>(ptc_overlay_scroll_to_rect(
                        page_layout_.viewport, page_layout_.status,
                        static_cast<int>(page_scroll_target_), page_layout_.scroll_max));
                }
            }
            if (tapped && page_hit(page_layout_.refresh, tx, ty) && requests_enabled) {
                status_details_focus_ = 2;
                if (error_) (void)retry_last_request();
                else (void)begin_status_refresh();
                return true;
            }
            if ((keysDown & HidNpadButton_X) || (tapped && flow_hit(page_layout_.status, tx, ty))) {
                status_details_focus_ = 1;
                details_tech_expanded_ = !details_tech_expanded_;
                return true;
            }
            if ((keysDown & (HidNpadButton_B | HidNpadButton_Minus)) ||
                (tapped && page_hit(page_layout_.primary, tx, ty))) {
                status_expanded_ = false;
                reset_page_scroll();
            } else if (keysDown & HidNpadButton_A) {
                if (status_details_focus_ == 0) {
                    status_expanded_ = false;
                    reset_page_scroll();
                } else if (status_details_focus_ == 1) {
                    details_tech_expanded_ = !details_tech_expanded_;
                } else if (status_details_focus_ == 2 && requests_enabled) {
                    if (error_ && (last_request_kind_ == OverlayRequestKind::OfflineCode ||
                        last_request_kind_ == OverlayRequestKind::PreviewOfflineCode)) {
                        error_ = status_expanded_ = keypad_active_ = false;
                        pending_code_[0] = '\0';
                        ptc_overlay_input_init(input_);
                    } else if (error_) (void)retry_last_request();
                    else (void)begin_status_refresh();
                }
            } else if ((keysDown & HidNpadButton_Y) && requests_enabled) {
                status_details_focus_ = 2;
                if (error_ && (last_request_kind_ == OverlayRequestKind::OfflineCode ||
                    last_request_kind_ == OverlayRequestKind::PreviewOfflineCode)) {
                    error_ = status_expanded_ = keypad_active_ = false;
                    pending_code_[0] = '\0';
                    ptc_overlay_input_init(input_);
                } else if (error_) (void)retry_last_request();
                else (void)begin_status_refresh();
            }
            return true;
        }
        const bool restricted = child_is_restricted();
        if (tapped) {
            if (page_hit(page_layout_.refresh, tx, ty)) {
                child_overview_focus_ = restricted ? 3 : 4;
                if (requests_enabled) (void)begin_status_refresh();
                return true;
            }
            if (page_hit(page_layout_.parent, tx, ty)) {
                child_overview_focus_ = restricted ? 0 : 2;
                open_parent_pin();
                return true;
            }
            if (restricted) {
                if (page_hit(page_layout_.secondary, tx, ty)) {
                    child_overview_focus_ = 1;
                    tsl::Overlay::get()->close();
                } else if (flow_hit(page_layout_.status, tx, ty)) {
                    child_overview_focus_ = 2;
                    status_expanded_ = true;
                }
                return true;
            }
            if (!is_keypad_mode()) {
                if (page_hit(page_layout_.buffer, tx, ty)) {
                    child_overview_focus_ = 1;
                    if (requests_enabled && displayed_summary_.valid && displayed_summary_.daily_buffer_available)
                        (void)begin_claim_daily_buffer();
                } else if (page_hit(page_layout_.redeem, tx, ty)) {
                    child_overview_focus_ = 0;
                    keypad_active_ = true;
                    keypad_nav_focus_ = -1;
                } else if (flow_hit(page_layout_.status, tx, ty)) {
                    child_overview_focus_ = 3;
                    status_expanded_ = true;
                }
                return true;
            }
            if (page_hit(page_layout_.secondary, tx, ty)) {
                keypad_active_ = false;
                keypad_nav_focus_ = -1;
                ptc_overlay_input_init(input_);
                return true;
            }
            const char *charset = ptc_overlay_input_charset();
            for (unsigned i = 0; i < PTC_OVERLAY_KEY_COUNT; ++i) {
                if (page_hit(ptc_overlay_key_rect(PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y, i), tx, ty)) {
                    keypad_nav_focus_ = -1;
                    input_->cursor = i;
                    if (input_->length < PTC_OVERLAY_CODE_SYMBOLS) {
                        input_->symbols[input_->length++] = charset[i];
                        input_->symbols[input_->length] = '\0';
                    }
                    return true;
                }
            }
            if (page_hit(ptc_overlay_backspace_rect(PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y), tx, ty)) {
                keypad_nav_focus_ = 10;
                (void)ptc_overlay_input_handle(input_, PTC_OVERLAY_BUTTON_X, 0, 0);
            } else if (page_hit(ptc_overlay_clear_rect(PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y), tx, ty)) {
                keypad_nav_focus_ = 11;
                (void)ptc_overlay_input_handle(input_, PTC_OVERLAY_BUTTON_Y, 0, 0);
            } else if (page_hit(page_layout_.primary, tx, ty)) {
                keypad_nav_focus_ = 13;
                if (requests_enabled) (void)begin_code_preview();
            }
        }
        if (restricted) {
            const int prev_focus = child_overview_focus_;
            if (keysDown & HidNpadButton_Up) {
                if (child_overview_focus_ == 0 || child_overview_focus_ == 1) child_overview_focus_ = 2;
                else if (child_overview_focus_ == 2) child_overview_focus_ = 3;
            } else if (keysDown & HidNpadButton_Down) {
                if (child_overview_focus_ == 3) child_overview_focus_ = 2;
                else if (child_overview_focus_ == 2) child_overview_focus_ = 0;
            } else if (keysDown & (HidNpadButton_Left | HidNpadButton_Right)) {
                if (child_overview_focus_ == 0 || child_overview_focus_ == 1)
                    child_overview_focus_ = child_overview_focus_ == 0 ? 1 : 0;
            }
            if (child_overview_focus_ == 2 && child_overview_focus_ != prev_focus) {
                page_scroll_target_ = static_cast<float>(ptc_overlay_scroll_to_rect(
                    page_layout_.viewport, page_layout_.status,
                    static_cast<int>(page_scroll_target_), page_layout_.scroll_max));
            }
            if (keysDown & HidNpadButton_A) {
                if (child_overview_focus_ == 0) open_parent_pin();
                else if (child_overview_focus_ == 1) tsl::Overlay::get()->close();
                else if (child_overview_focus_ == 2) status_expanded_ = true;
                else if (child_overview_focus_ == 3 && requests_enabled) (void)begin_status_refresh();
                return true;
            }
            if (keysDown & HidNpadButton_B) { tsl::Overlay::get()->close(); return true; }
            if (keysDown & HidNpadButton_Minus) { status_expanded_ = true; return true; }
            if (keysDown & HidNpadButton_R) { open_parent_pin(); return true; }
            if (keysDown & HidNpadButton_Y) {
                if (requests_enabled) (void)begin_status_refresh();
                return true;
            }
            return true;
        }
        if (!is_keypad_mode()) {
            const int prev_focus = child_overview_focus_;
            if (keysDown & HidNpadButton_Up) {
                if (child_overview_focus_ == 1 || child_overview_focus_ == 2) child_overview_focus_ = 0;
                else if (child_overview_focus_ == 0) child_overview_focus_ = 3;
                else if (child_overview_focus_ == 3) child_overview_focus_ = 4;
            } else if (keysDown & HidNpadButton_Down) {
                if (child_overview_focus_ == 4) child_overview_focus_ = 3;
                else if (child_overview_focus_ == 3) child_overview_focus_ = 0;
                else if (child_overview_focus_ == 0) child_overview_focus_ = 1;
            } else if (keysDown & HidNpadButton_Left) {
                if (child_overview_focus_ == 2) child_overview_focus_ = 1;
            } else if (keysDown & HidNpadButton_Right) {
                if (child_overview_focus_ == 1) child_overview_focus_ = 2;
            }
            if (child_overview_focus_ == 3 && child_overview_focus_ != prev_focus) {
                page_scroll_target_ = static_cast<float>(ptc_overlay_scroll_to_rect(
                    page_layout_.viewport, page_layout_.status,
                    static_cast<int>(page_scroll_target_), page_layout_.scroll_max));
            }
            if (keysDown & HidNpadButton_A) {
                if (child_overview_focus_ == 0) {
                    keypad_active_ = true;
                    keypad_nav_focus_ = -1;
                } else if (child_overview_focus_ == 1) {
                    if (requests_enabled && displayed_summary_.valid && displayed_summary_.daily_buffer_available)
                        (void)begin_claim_daily_buffer();
                } else if (child_overview_focus_ == 2) {
                    open_parent_pin();
                } else if (child_overview_focus_ == 3) {
                    status_expanded_ = true;
                } else if (child_overview_focus_ == 4 && requests_enabled) {
                    (void)begin_status_refresh();
                }
                return true;
            }
            if (keysDown & HidNpadButton_B) { tsl::Overlay::get()->close(); return true; }
            if (keysDown & HidNpadButton_Minus) { status_expanded_ = true; return true; }
            if (keysDown & HidNpadButton_R) { open_parent_pin(); return true; }
            if (keysDown & HidNpadButton_L) {
                if (requests_enabled && displayed_summary_.valid && displayed_summary_.daily_buffer_available)
                    (void)begin_claim_daily_buffer();
                return true;
            }
            if (keysDown & HidNpadButton_Y) {
                if (error_) {
                    if (last_request_kind_ == OverlayRequestKind::OfflineCode || last_request_kind_ == OverlayRequestKind::PreviewOfflineCode) {
                        error_ = keypad_active_ = false;
                        pending_code_[0] = '\0';
                        ptc_overlay_input_init(input_);
                    } else if (requests_enabled) (void)retry_last_request();
                } else if (requests_enabled) (void)begin_status_refresh();
                return true;
            }
            return true;
        }
        // is_keypad_mode()
        if (keysDown & HidNpadButton_B) {
            keypad_active_ = false;
            keypad_nav_focus_ = -1;
            ptc_overlay_input_init(input_);
            return true;
        }
        if (keysDown & HidNpadButton_R) { open_parent_pin(); return true; }
        if (keysDown & HidNpadButton_Y) {
            (void)ptc_overlay_input_handle(input_, PTC_OVERLAY_BUTTON_Y, 0, 0);
            return true;
        }
        if (keysDown & HidNpadButton_X) {
            (void)ptc_overlay_input_handle(input_, PTC_OVERLAY_BUTTON_X, 0, 0);
            return true;
        }
        if ((keysDown & HidNpadButton_Plus) && requests_enabled) {
            (void)begin_code_preview();
            return true;
        }

        if (keypad_nav_focus_ == -1) {
            if ((keysDown & HidNpadButton_Down) && input_->cursor >= 5) {
                keypad_nav_focus_ = input_->cursor <= 6 ? 10 : 11;
                return true;
            }
            return ptc_overlay_input_handle(input_, to_overlay_buttons(keysDown), to_overlay_buttons(keysHeld), elapsed_ms);
        } else {
            if (keysDown & HidNpadButton_Up) {
                if (keypad_nav_focus_ == 10) {
                    keypad_nav_focus_ = -1;
                    input_->cursor = 6;
                } else if (keypad_nav_focus_ == 11) {
                    keypad_nav_focus_ = -1;
                    input_->cursor = 8;
                } else if (keypad_nav_focus_ == 13) {
                    keypad_nav_focus_ = 10;
                } else if (keypad_nav_focus_ == 12 || keypad_nav_focus_ == 14) {
                    keypad_nav_focus_ = 13;
                }
            } else if (keysDown & HidNpadButton_Down) {
                if (keypad_nav_focus_ == 10 || keypad_nav_focus_ == 11) {
                    keypad_nav_focus_ = 13;
                } else if (keypad_nav_focus_ == 13) {
                    keypad_nav_focus_ = 12;
                }
            } else if (keysDown & HidNpadButton_Left) {
                if (keypad_nav_focus_ == 11) keypad_nav_focus_ = 10;
                else if (keypad_nav_focus_ == 14) keypad_nav_focus_ = 12;
            } else if (keysDown & HidNpadButton_Right) {
                if (keypad_nav_focus_ == 10) keypad_nav_focus_ = 11;
                else if (keypad_nav_focus_ == 12) keypad_nav_focus_ = 14;
            }
            if (keysDown & HidNpadButton_A) {
                if (keypad_nav_focus_ == 10) {
                    (void)ptc_overlay_input_handle(input_, PTC_OVERLAY_BUTTON_X, 0, 0);
                } else if (keypad_nav_focus_ == 11) {
                    (void)ptc_overlay_input_handle(input_, PTC_OVERLAY_BUTTON_Y, 0, 0);
                } else if (keypad_nav_focus_ == 12) {
                    keypad_active_ = false;
                    keypad_nav_focus_ = -1;
                    ptc_overlay_input_init(input_);
                } else if (keypad_nav_focus_ == 13 && requests_enabled) {
                    (void)begin_code_preview();
                } else if (keypad_nav_focus_ == 14) {
                    open_parent_pin();
                }
                return true;
            }
            return true;
        }
    }
