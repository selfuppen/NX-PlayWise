// Included inside the production GUI and deterministic host preview view.
// Keep drawings here so documentation follows the installed Overlay code.
    static void draw_outline(
        tsl::gfx::Renderer *renderer,
        s32 x,
        s32 y,
        s32 width,
        s32 height,
        s32 thickness,
        tsl::Color color)
    {
        renderer->drawRect(x, y, width, thickness, renderer->a(color));
        renderer->drawRect(x, y + height - thickness, width, thickness, renderer->a(color));
        renderer->drawRect(x, y, thickness, height, renderer->a(color));
        renderer->drawRect(x + width - thickness, y, thickness, height, renderer->a(color));
    }

    static const char *transport_stage(const PtcOverlayBridge *bridge)
    {
        if (bridge->waiting) {
            if (ptc_overlay_bridge_transport_state(bridge) == PTC_TRANSPORT_ROUTE_IPC_SD_RESULT)
                return ptc_ui_text(PTC_UI_T_READING_BACKGROUND_RESULTS);
            return ptc_ui_text(PTC_UI_T_WAITING_FOR_BACKGROUND_PROCESSING);
        }
        return "";
    }

    bool bedtime_restricted() const
    {
        return displayed_summary_.valid && displayed_summary_.bedtime_active &&
            !displayed_summary_.bedtime_skipped;
    }

    bool eye_care_restricted() const
    {
        return displayed_summary_.valid && displayed_summary_.eye_care_enabled &&
            std::strcmp(displayed_summary_.eye_care_phase, "resting") == 0;
    }

    bool access_recovery_visible() const
    {
        return parent_view_ != ParentView::Child;
    }

    const char *parent_action_reason(PtcOverlayParentAction action) const
    {
        if (bridge_->waiting) return ptc_ui_text(PTC_UI_T_BACKGROUND_PROCESSING);
        if (action != PTC_OVERLAY_PARENT_RESTORE_SNAPSHOT &&
            (!has_status_snapshot_ || status_is_stale())) return ptc_ui_text(PTC_UI_T_PLEASE_REFRESH_THE_STATUS_FIRST);
        return ptc_overlay_parent_action_unavailable_reason(&displayed_summary_, action);
    }

    bool parent_action_requires_hold(PtcOverlayParentAction action) const
    {
        if (action == PTC_OVERLAY_PARENT_ADD_MINUTES ||
            action == PTC_OVERLAY_PARENT_SKIP_BEDTIME ||
            action == PTC_OVERLAY_PARENT_SKIP_EYE_CARE || action == PTC_OVERLAY_PARENT_WAIVE_DOCK ||
            action == PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP) return false;
        return true;
    }

    const char *request_label() const
    {
        OverlayRequestKind kind = active_request_kind_ != OverlayRequestKind::None
            ? active_request_kind_ : last_request_kind_;
        if (kind == OverlayRequestKind::Status) return ptc_ui_text(PTC_UI_T_REFRESH_TODAY_S_STATUS);
        if (kind == OverlayRequestKind::PreviewOfflineCode) return ptc_ui_text(PTC_UI_T_PREVIEW_TODAY_S_GRANT);
        if (kind == OverlayRequestKind::OfflineCode) return ptc_ui_text(PTC_UI_T_SUBMIT_TODAY_S_GRANT);
        if (kind == OverlayRequestKind::ClaimDailyBuffer) return ptc_ui_text(PTC_UI_T_RECEIVE_INDEPENDENT_BUFFERING);
        if (kind == OverlayRequestKind::WaiveDock) return ptc_ui_text(PTC_UI_T_DOCK_WAIVE);
        if (kind == OverlayRequestKind::ClearBedtimeSkip) return ptc_ui_text(PTC_UI_T_RESTORE_THIS_BEDTIME);
        return ptc_ui_text(PTC_UI_T_NOT_STARTED);
    }

    void format_refresh_age(char *out, size_t out_size) const
    {
        if (!out || out_size == 0) return;
        if (active_request_kind_ == OverlayRequestKind::Status && bridge_->waiting) {
            std::snprintf(out, out_size, ptc_ui_text(PTC_UI_T_REFRESHING));
            return;
        }
        if (!has_status_snapshot_ || last_refresh_tick_ == 0) {
            std::snprintf(out, out_size, ptc_ui_text(PTC_UI_T_HAS_NOT_BEEN_REFRESHED_YET));
            return;
        }
        const u64 age_seconds = armTicksToNs(armGetSystemTick() - last_refresh_tick_) / 1000000000ULL;
        if (age_seconds < 2) {
            std::snprintf(out, out_size, ptc_ui_text(PTC_UI_T_JUST_REFRESHED));
        } else if (age_seconds < 60) {
            std::snprintf(out, out_size, ptc_ui_text(PTC_UI_T_LLU_SEC_AGO), static_cast<unsigned long long>(age_seconds));
        } else {
            std::snprintf(out, out_size, ptc_ui_text(PTC_UI_T_LLU_MIN_AGO), static_cast<unsigned long long>(age_seconds / 60));
        }
    }

    bool status_is_stale() const
    {
        if (!has_status_snapshot_ || last_refresh_tick_ == 0) return false;
        return armTicksToNs(armGetSystemTick() - last_refresh_tick_) >= 30000000000ULL;
    }

    bool status_needs_detail() const
    {
        return error_ || success_visible_;
    }

    void draw_code_preview(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw)
    {
        char line[128];
        renderer->drawRect(cx, cy + 18, cw, 540, renderer->a(PANEL_COLOR));
        draw_outline(renderer, cx, cy + 18, cw, 540, 2, FOCUS_BORDER);
        draw_localized(renderer, preview_changed_ ? ptc_ui_text(PTC_UI_T_STATUS_CHANGED_PLEASE_CONFIRM_AGAIN) : ptc_ui_text(PTC_UI_T_CONFIRM_PLAYTIME_GRANT),
                             false, cx + 14, cy + 52, 18, renderer->a(TEXT_COLOR));
        ptc_ui_format_actual_added(preview_summary_.grant_minutes, line, sizeof(line));
        draw_localized(renderer, line, false, cx + 14, cy + 84, 15, renderer->a(FOCUS_BORDER));
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_VALID_TODAY_CAN_ONLY_BE_USED_ONCE), false,
                             cx + 14, cy + 110, 12, renderer->a(MUTED_COLOR));

        renderer->drawRect(cx + 12, cy + 142, cw - 24, 86, renderer->a(CARD_COLOR));
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY), false, cx + 24, cy + 168, 12, renderer->a(MUTED_COLOR));
        if (preview_summary_.converts_unlimited_to_limited) {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED), false, cx + 155, cy + 171, 19, renderer->a(SUCCESS_COLOR));
        } else if (preview_summary_.remaining_available) {
            ptc_ui_format_minutes(preview_summary_.remaining_minutes, line, sizeof(line));
            draw_localized(renderer, line, false, cx + 155, cy + 171, 19, renderer->a(SUCCESS_COLOR));
        } else {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_UNAVAILABLE), false, cx + 155, cy + 171, 16, renderer->a(MUTED_COLOR));
        }

        renderer->drawRect(cx + 12, cy + 244, cw - 24, 86, renderer->a(CARD_COLOR));
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_ESTIMATED_TIME_AFTER_GRANT), false, cx + 24, cy + 270, 12, renderer->a(MUTED_COLOR));
        if (preview_summary_.remaining_after_available) {
            ptc_ui_format_minutes(preview_summary_.remaining_after_minutes, line, sizeof(line));
            draw_localized(renderer, line, false, cx + 155, cy + 273, 19,
                                 renderer->a(preview_summary_.remaining_after_minutes == 0 ? ERROR_COLOR : SUCCESS_COLOR));
        } else {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_UNAVAILABLE), false, cx + 155, cy + 273, 16, renderer->a(ERROR_COLOR));
        }

        const PtcOverlayPreviewVisualLevel preview_level = ptc_overlay_preview_visual_level(
            preview_summary_.remaining_after_available,
            preview_summary_.remaining_after_minutes,
            preview_summary_.preview_capped,
            preview_summary_.converts_unlimited_to_limited);
        const bool dangerous = preview_level == PTC_OVERLAY_PREVIEW_DANGER;
        if (preview_level != PTC_OVERLAY_PREVIEW_NEUTRAL) {
            const tsl::Color alert_bg = dangerous ? DANGER_BG : WARNING_BG;
            const tsl::Color alert_accent = dangerous ? ERROR_COLOR : WAITING_COLOR;
            renderer->drawRect(cx + 12, cy + 342, cw - 24, 50, renderer->a(alert_bg));
            draw_outline(renderer, cx + 12, cy + 342, cw - 24, 50, 2, alert_accent);
        }
        if (preview_summary_.converts_unlimited_to_limited) {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_WARNING_WILL_CHANGE_FROM_UNLIMITED_TO_LIMITED), false, cx + 22, cy + 372, 13, renderer->a(ERROR_COLOR));
        } else if (!preview_summary_.remaining_after_available) {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_WARNING_CANNOT_CONFIRM_PLAYTIME_AFTER_GRANT_RIGHT), false, cx + 22, cy + 372, 13, renderer->a(ERROR_COLOR));
        } else if (preview_summary_.remaining_after_minutes == 0) {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_WARNING_ESTIMATED_NO_PLAYTIME_REMAINING_AFTER_GRANT), false, cx + 22, cy + 372, 13, renderer->a(ERROR_COLOR));
        } else if (preview_summary_.preview_capped) {
            ptc_ui_format_daily_cap_applied(preview_summary_.effective_add_minutes, line, sizeof(line));
            draw_localized(renderer, line, false, cx + 22, cy + 372, 13, renderer->a(WAITING_COLOR));
        } else {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_CODE_WILL_NOT_BE_CONSUMED_BEFORE_CONFIRMATION), false, cx + 16, cy + 366, 13, renderer->a(MUTED_COLOR));
        }
        draw_localized(renderer, touch_hold_warning_ ? ptc_ui_text(PTC_UI_T_PLEASE_HOLD_A_ON_CONTROLLER_TO_CONFIRM) :
                             (dangerous ? ptc_ui_text(PTC_UI_T_HOLD_A_1S_TO_CONFIRM_B_CANCEL) : ptc_ui_text(PTC_UI_T_A_CONFIRM_B_CANCEL)),
                             false, cx + 16, cy + 414, 13,
                             renderer->a(dangerous ? ERROR_COLOR : FOCUS_BORDER));
        renderer->drawRect(cx, cy + 500, 145, 50, renderer->a(CARD_COLOR));
        draw_outline(renderer, cx, cy + 500, 145, 50, 1, MUTED_COLOR);
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_B_CANCEL_2), false, cx + 45, cy + 530, 14, renderer->a(TEXT_COLOR));
        renderer->drawRect(cx + 170, cy + 500, 160, 50, renderer->a(FOCUS_BG));
        draw_outline(renderer, cx + 170, cy + 500, 160, 50, 2, FOCUS_BORDER);
        draw_localized(renderer, dangerous ? ptc_ui_text(PTC_UI_T_HOLD_A_TO_CONFIRM) : ptc_ui_text(PTC_UI_T_A_CONFIRM_2), false,
                             cx + 202, cy + 530, 14, renderer->a(TEXT_COLOR));
        if (dangerous) {
            const int progress = confirm_hold_.fired ? 0 :
                ptc_overlay_hold_progress(&confirm_hold_, 1000);
            renderer->drawRect(cx + 176, cy + 541, 148, 4, renderer->a(CARD_COLOR));
            if (progress > 0)
                renderer->drawRect(cx + 176, cy + 541, 148 * progress / 1000, 4,
                    renderer->a(ERROR_COLOR));
        }
    }

    void draw_code_success(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw)
    {
        char line[128];
        renderer->drawRect(cx, cy + 36, cw, 460, renderer->a(PANEL_COLOR));
        const tsl::Color accent = result_failed_ ? ERROR_COLOR : SUCCESS_COLOR;
        draw_outline(renderer, cx, cy + 36, cw, 460, 2, accent);
        draw_localized(renderer, result_pending_ ? ptc_ui_text(PTC_UI_T_CONFIRMING_GRANT_RESULT) : (result_failed_ ? ptc_ui_text(PTC_UI_T_GRANT_FAILED) : ptc_ui_text(PTC_UI_T_GRANT_SUCCESSFUL)),
                             false, cx + 14, cy + 74, 22, renderer->a(accent));
        std::snprintf(line, sizeof(line), result_pending_ ? ptc_ui_text(PTC_UI_T_ESTIMATED_D_MIN) :
                      (result_failed_ ? ptc_ui_text(PTC_UI_T_ORIGINALLY_PLANNED_D_MIN) : ptc_ui_text(PTC_UI_T_ADDED_D_MIN)),
                      preview_summary_.grant_minutes);
        draw_localized(renderer, line, false, cx + 14, cy + 108, 15, renderer->a(TEXT_COLOR));
        draw_localized(renderer, result_pending_ ? ptc_ui_text(PTC_UI_T_VERIFYING_RESULT_PLEASE_DO_NOT_RE_ENTER) :
                             (result_failed_ ? ptc_ui_text(PTC_UI_T_VERIFICATION_FAILED_CODE_NOT_CONSUMED_CAN_RETRY) :
                              ptc_ui_text(PTC_UI_T_THIS_GRANT_CODE_HAS_ALREADY_BEEN_USED)),
                             false, cx + 14, cy + 136, 12, renderer->a(MUTED_COLOR));
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_BEFORE_GRANT), false, cx + 18, cy + 190, 12, renderer->a(MUTED_COLOR));
        if (redemption_before_.converts_unlimited_to_limited) std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else if (redemption_before_.remaining_available) ptc_ui_format_minutes(redemption_before_.remaining_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        draw_localized(renderer, line, false, cx + 130, cy + 193, 18, renderer->a(TEXT_COLOR));
        draw_localized(renderer, result_pending_ ? ptc_ui_text(PTC_UI_T_PREVIEW_AFTER_GRANT) : ptc_ui_text(PTC_UI_T_AFTER_GRANT), false, cx + 18, cy + 254, 12, renderer->a(MUTED_COLOR));
        const PtcCompanionResultSummary &after = result_pending_ ? preview_summary_ : displayed_summary_;
        if (result_pending_ ? after.remaining_after_available : after.remaining_available) {
            ptc_ui_format_minutes(result_pending_ ? after.remaining_after_minutes : after.remaining_minutes, line, sizeof(line));
        }
        else std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        draw_localized(renderer, line, false, cx + 130, cy + 257, 18, renderer->a(accent));
        draw_localized(renderer, result_pending_ ? ptc_ui_text(PTC_UI_T_YOU_MAY_CLOSE_STATUS_WILL_RESUME_NEXT) :
                             (result_failed_ ? ptc_ui_text(PTC_UI_T_FAILURE_RESULT_CONFIRMED) : ptc_ui_text(PTC_UI_T_RESULTS_CONFIRMED_AND_SAVED)),
                             false, cx + 18, cy + 324, 14, renderer->a(accent));
        renderer->drawRect(cx + 30, cy + 392, cw - 60, 56, renderer->a(FOCUS_BG));
        draw_outline(renderer, cx + 30, cy + 392, cw - 60, 56, 2, FOCUS_BORDER);
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_A_B_RETURN_TO_BLANK_INPUT_PAGE), false, cx + 82, cy + 426, 14, renderer->a(TEXT_COLOR));
    }

    void draw_parent_page(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw)
    {
        char line[160];
        renderer->drawRect(cx, cy + 18, cw, 548, renderer->a(PANEL_COLOR));
        draw_outline(renderer, cx, cy + 18, cw, 548, 2, FOCUS_BORDER);
        draw_localized(renderer, parent_view_ == ParentView::Pin ? ptc_ui_text(PTC_UI_T_PARENT_PIN_VERIFICATION) :
            (parent_view_ == ParentView::Actions ? ptc_ui_text(PTC_UI_T_PARENT_ZONE) :
             (parent_view_ == ParentView::Confirm ? ptc_ui_text(parent_action_ == PTC_OVERLAY_PARENT_WAIVE_DOCK ? PTC_UI_T_DOCK_WAIVE : PTC_UI_T_CONFIRM_RESTORING_LIMIT) : ptc_ui_text(PTC_UI_T_OPERATION_RESULT))),
            false, cx + 14, cy + 56, 21, renderer->a(TEXT_COLOR));
        const char *state = (!has_status_snapshot_ || status_is_stale()) ? ptc_ui_text(PTC_UI_T_STATUS_PENDING_REFRESH) :
            (bedtime_restricted() ? ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_3) :
             (displayed_summary_.daily_restriction_active ? ptc_ui_text(PTC_UI_T_DAILY_LIMIT_REACHED_2) :
              (eye_care_restricted() ? ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING) :
               ptc_ui_text(PTC_UI_T_NORMAL_PLAY_AVAILABLE))));
        draw_localized(renderer, state, false, cx + 14, cy + 88, 13,
            renderer->a(bedtime_restricted() ? ERROR_COLOR : MUTED_COLOR));

        if (parent_view_ == ParentView::Pin) {
            char masked[PTC_AUTH_PIN_MAX_LEN + 1];
            (void)ptc_overlay_pin_mask(pin_length_, masked, sizeof(masked));
            std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_DIGITS_ENTERED_S),
                static_cast<unsigned int>(pin_length_), masked);
            draw_localized(renderer, line, false, cx + 14, cy + 152, 15, renderer->a(TEXT_COLOR), 330);
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_STICK_8_DIRECTIONS_FOR_1_8_D), false,
                cx + 14, cy + 180, 12, renderer->a(MUTED_COLOR));
            renderer->drawRect(cx, cy + PTC_OVERLAY_KEYPAD_Y, cw,
                PTC_OVERLAY_KEYPAD_H, renderer->a(CARD_COLOR));
            const char *DIRECTIONS[8] = {
                ptc_ui_text(PTC_UI_T_8_UP_LEFT), ptc_ui_text(PTC_UI_T_1_UP), ptc_ui_text(PTC_UI_T_2_UP_RIGHT), ptc_ui_text(PTC_UI_T_7_LEFT), ptc_ui_text(PTC_UI_T_3_RIGHT),
                ptc_ui_text(PTC_UI_T_6_DOWN_LEFT), ptc_ui_text(PTC_UI_T_5_DOWN), ptc_ui_text(PTC_UI_T_4_DOWN_RIGHT)
            };
            static constexpr int COLS[8] = {0, 1, 2, 0, 2, 0, 1, 2};
            static constexpr int ROWS[8] = {0, 0, 0, 1, 1, 2, 2, 2};
            for (int i = 0; i < 8; ++i) {
                const s32 x = cx + 12 + COLS[i] * 117;
                const s32 y = cy + PTC_OVERLAY_KEYPAD_Y + 4 + ROWS[i] * 44;
                renderer->drawRect(x, y, 105, 42, renderer->a(KEY_COLOR));
                draw_localized(renderer, DIRECTIONS[i], false, x + 12, y + 27, 13,
                    renderer->a(TEXT_COLOR));
            }
            draw_localized(renderer, "X 0    Y 9", false, cx + 142, cy + 267, 15,
                renderer->a(TEXT_COLOR));
            const PtcOverlayRect backspace = ptc_overlay_backspace_rect(cx, cy);
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_ZL_BACKSPACE_2), false, backspace.x + 14, backspace.y + 27, 13,
                renderer->a(BACKSPACE_BORDER));
            renderer->drawRect(cx, cy + 390, cw, 46, renderer->a(FOCUS_BG));
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_VERIFY_PIN), false, cx + 125, cy + 420, 16,
                renderer->a(TEXT_COLOR));
            if (pin_message_[0]) draw_localized(renderer, pin_message_, false,
                cx + 14, cy + 468, 13, renderer->a(ERROR_COLOR), 330);
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_B_BACK_TO_GRANT_CODE), false, cx + 14, cy + 532, 14,
                renderer->a(MUTED_COLOR));
            return;
        }

        if (parent_view_ == ParentView::Actions) {
            const char *LABELS[PTC_OVERLAY_PARENT_ACTION_COUNT] = {
                ptc_ui_text(PTC_UI_T_QUICK_GRANT), ptc_ui_text(PTC_UI_T_NO_LIMIT_TODAY), ptc_ui_text(PTC_UI_T_SKIP_BEDTIME), ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP), ptc_ui_text(PTC_UI_T_RESTORE_THIS_BEDTIME), ptc_ui_text(PTC_UI_T_TURN_OFF_BEDTIME_PLAN),
                ptc_ui_text(PTC_UI_T_RESTORE_PRE_INSTALL_SETTINGS_DEACTIVATE), ptc_ui_text(PTC_UI_T_DOCK_WAIVE)
            };
            renderer->drawRect(cx + 246, cy + 58, cw - 258, 46,
                renderer->a(bridge_->waiting ? DISABLED_COLOR : CARD_COLOR));
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_Y_REFRESH_2), false, cx + 264, cy + 87, 13,
                renderer->a(bridge_->waiting ? MUTED_COLOR : FOCUS_BORDER));
            for (int i = 0; i < PTC_OVERLAY_PARENT_ACTION_COUNT; ++i) {
                const s32 y = cy + 125 + i * 40;
                const bool selected = i == parent_action_;
                const char *reason = parent_action_reason(
                    static_cast<PtcOverlayParentAction>(i));
                renderer->drawRect(cx + 12, y, cw - 24, 38,
                    renderer->a(selected ? FOCUS_BG : CARD_COLOR));
                draw_outline(renderer, cx + 12, y, cw - 24, 38,
                    selected ? 2 : 1, selected ? FOCUS_BORDER : MUTED_COLOR);
                if (i == PTC_OVERLAY_PARENT_ADD_MINUTES) {
                    std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_QUICK_GRANT_D_MIN_LEFT_RIGHT_TO), daily_add_minutes_);
                } else if (i == PTC_OVERLAY_PARENT_SKIP_EYE_CARE && !reason && displayed_summary_.eye_care_rest_remaining_seconds > 0) {
                    const int rest_min = (displayed_summary_.eye_care_rest_remaining_seconds + 59) / 60;
                    std::snprintf(line, sizeof(line), "%s (%d %s)", LABELS[i], rest_min, ptc_ui_text(PTC_UI_T_MIN));
                } else {
                    std::snprintf(line, sizeof(line), "%s", LABELS[i]);
                }
                draw_localized(renderer, line, false, cx + 24, y + 19, 13,
                    renderer->a(reason ? MUTED_COLOR : TEXT_COLOR), 310);
                if (reason) draw_localized(renderer, reason, false, cx + 24, y + 36, 11,
                    renderer->a(WAITING_COLOR), 305);
                if (selected && !reason && parent_action_requires_hold(static_cast<PtcOverlayParentAction>(i))) {
                    const int progress = ptc_overlay_hold_progress(&confirm_hold_, 1000);
                    renderer->drawRect(cx + 20, y + 35, cw - 40, 3, renderer->a(CARD_COLOR));
                    if (progress > 0)
                        renderer->drawRect(cx + 20, y + 35, (cw - 40) * progress / 1000, 3,
                            renderer->a(ERROR_COLOR));
                }
            }
            const auto selected = static_cast<PtcOverlayParentAction>(parent_action_);
            const char *reason = parent_action_reason(selected);
            const bool hold = parent_action_requires_hold(selected);
            draw_localized(renderer, reason ? ptc_ui_text(PTC_UI_T_ACTION_CURRENTLY_UNAVAILABLE_B_BACK) :
                (hold ? ptc_ui_text(PTC_UI_T_HOLD_A_1S_Y_REFRESH_B_BACK) : ptc_ui_text(PTC_UI_T_A_EXECUTE_Y_REFRESH_B_BACK)),
                false, cx + 14, cy + 470, 13,
                renderer->a(reason ? WAITING_COLOR : (hold ? ERROR_COLOR : FOCUS_BORDER)));
            return;
        }

        if (parent_view_ == ParentView::Confirm) {
            const bool waive = parent_action_ == PTC_OVERLAY_PARENT_WAIVE_DOCK;
            const bool immediate = !waive && displayed_summary_.bedtime_active &&
                displayed_summary_.bedtime_skipped;
            const char *reason = parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_));
            draw_localized(renderer, ptc_ui_text(waive ? PTC_UI_T_DOCK_WAIVE : PTC_UI_T_RESTORE_BEDTIME_LIMIT), false, cx + 14, cy + 158, 19,
                renderer->a(TEXT_COLOR), 330);
            draw_localized(renderer, waive ? ptc_ui_text(PTC_UI_T_DOCK_WAIVE_BODY) : immediate ?
                ptc_ui_text(PTC_UI_T_CURRENTLY_DURING_BEDTIME_CONFIRMING_WILL_RESTRICT_USE) :
                ptc_ui_text(PTC_UI_T_SKIP_WILL_BE_CLEARED_BEDTIME_RESTRICTIONS_WILL),
                false, cx + 14, cy + 212, 13,
                renderer->a(immediate ? ERROR_COLOR : MUTED_COLOR), 330);
            if (reason) draw_localized(renderer, reason, false, cx + 14, cy + 270, 13,
                renderer->a(WAITING_COLOR), 330);
            renderer->drawRect(cx + 12, cy + 480, 155, 58, renderer->a(CARD_COLOR));
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_B_CANCEL_2), false, cx + 35, cy + 515, 13,
                renderer->a(TEXT_COLOR));
            renderer->drawRect(cx + 184, cy + 480, cw - 196, 58,
                renderer->a(immediate ? DISABLED_COLOR : FOCUS_BG));
            draw_localized(renderer, immediate ? ptc_ui_text(PTC_UI_T_HOLD_A_1S_TO_RESTORE) : ptc_ui_text(waive ? PTC_UI_T_A_CONFIRM : PTC_UI_T_A_CONFIRM_RESTORE), false,
                cx + 202, cy + 515, 13,
                renderer->a(immediate ? ERROR_COLOR : TEXT_COLOR));
            if (immediate) {
                const int progress = ptc_overlay_hold_progress(&confirm_hold_, 1000);
                renderer->drawRect(cx + 194, cy + 533, (cw - 216) * progress / 1000, 3,
                    renderer->a(ERROR_COLOR));
            }
            return;
        }

        draw_localized(renderer, bridge_->waiting ? ptc_ui_text(PTC_UI_T_PROCESSING_REQUEST) :
            (parent_action_succeeded_ ? ptc_ui_text(PTC_UI_T_OPERATION_COMPLETED) : ptc_ui_text(PTC_UI_T_OPERATION_INCOMPLETE)),
            false, cx + 14, cy + 180, 19,
            renderer->a(bridge_->waiting ? WAITING_COLOR :
                (parent_action_succeeded_ ? SUCCESS_COLOR : ERROR_COLOR)));
        if (!bridge_->waiting && parent_action_succeeded_) {
            if (last_request_kind_ == OverlayRequestKind::ClaimDailyBuffer) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_SELF_BUFFER_CLAIMED_ADDED_D_MIN),
                    displayed_summary_.daily_buffer_minutes);
                draw_localized(renderer, line, false, cx + 14, cy + 225, 14,
                    renderer->a(SUCCESS_COLOR), 320);
            } else if (last_request_kind_ == OverlayRequestKind::SkipEyeCare) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_CONFIRM_BODY), false, cx + 14, cy + 225, 14,
                    renderer->a(SUCCESS_COLOR), 320);
            }
            const char *result = !displayed_summary_.access_recovery_required ?
                ptc_ui_text(PTC_UI_T_PCTL_RELOADED_RESTRICTIONS_CLEARED) :
                (bedtime_restricted() ? ptc_ui_text(PTC_UI_T_STILL_RESTRICTED_BY_BEDTIME) :
                 (displayed_summary_.daily_restriction_active ?
                  ptc_ui_text(PTC_UI_T_STILL_RESTRICTED_BY_DAILY_LIMIT) : ptc_ui_text(PTC_UI_T_PLEASE_CHECK_CURRENT_RESTRICTION_STATUS)));
            draw_localized(renderer, result, false, cx + 14, cy + 270, 14,
                renderer->a(displayed_summary_.access_recovery_required ?
                    WAITING_COLOR : SUCCESS_COLOR), 320);
        } else if (!bridge_->waiting) {
            draw_localized(renderer, ptc_overlay_bridge_error_message_zh(bridge_), false,
                cx + 14, cy + 225, 13, renderer->a(ERROR_COLOR), 320);
            if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_ERROR_CODE_D), bridge_->summary.error_code);
                draw_localized(renderer, line, false, cx + 14, cy + 270, 12,
                    renderer->a(ERROR_COLOR));
            }
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_WHEN_BACKGROUND_IS_UNAVAILABLE_PLEASE_RECOVER_VIA), false,
                cx + 14, cy + 324, 12, renderer->a(MUTED_COLOR), 320);
        }
        if (last_request_kind_ != OverlayRequestKind::ClaimDailyBuffer) {
            renderer->drawRect(cx + 12, cy + 480, 155, 58, renderer->a(CARD_COLOR));
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_Y_RE_VERIFY), false, cx + 26, cy + 515, 13,
                renderer->a(FOCUS_BORDER));
        }
        renderer->drawRect(cx + 184, cy + 480, cw - 196, 58,
            renderer->a(CARD_COLOR));
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_B_BACK_2), false, cx + 223, cy + 515, 13,
            renderer->a(FOCUS_BORDER));
    }

    void draw_overlay(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw, s32 ch)
    {
        (void)ch;
        if (access_recovery_visible()) {
            draw_parent_page(renderer, cx, cy, cw);
            return;
        }
        if (success_visible_) {
            draw_code_success(renderer, cx, cy, cw);
            return;
        }
        if (preview_ready_) {
            draw_code_preview(renderer, cx, cy, cw);
            return;
        }
        char line[128];
        char age[32];
        PtcCompanionResultSummary summary = displayed_summary_;
        if (summary.valid && summary.eye_care_enabled &&
            std::strcmp(summary.eye_care_phase, "resting") == 0 && last_refresh_tick_ != 0) {
            const u64 elapsed = armTicksToNs(armGetSystemTick() - last_refresh_tick_) / 1000000000ULL;
            summary.eye_care_rest_remaining_seconds = elapsed >=
                static_cast<u64>(summary.eye_care_rest_remaining_seconds)
                ? 0 : summary.eye_care_rest_remaining_seconds - static_cast<int>(elapsed);
        }
        format_refresh_age(age, sizeof(age));
        const bool remaining_refresh_pending = ptc_overlay_remaining_refresh_pending(
            bridge_->waiting, active_request_kind_ == OverlayRequestKind::OfflineCode);

        // --- 0. Top Prominent Status Banner (醒目展示额度消耗估算与修改后/当前可玩时长) ---
        const s32 top_banner_y = cy + PTC_OVERLAY_TOP_BANNER_Y;
        const s32 top_banner_h = PTC_OVERLAY_TOP_BANNER_H;
        const bool bedtime_res = summary.valid && summary.bedtime_active && !summary.bedtime_skipped;
        const bool eye_res = summary.valid && summary.eye_care_enabled &&
            std::strcmp(summary.eye_care_phase, "resting") == 0;
        const bool dock_res = summary.valid && summary.dock_restriction_active && !bedtime_res && !eye_res;

        tsl::Color banner_bg = CARD_COLOR;
        tsl::Color banner_border = FOCUS_BORDER;
        if (remaining_refresh_pending) {
            banner_border = WAITING_COLOR;
        } else if (bedtime_res || dock_res) {
            banner_bg = DANGER_BG;
            banner_border = ERROR_COLOR;
        } else if (eye_res) {
            banner_bg = WARNING_BG;
            banner_border = WAITING_COLOR;
        }

        renderer->drawRect(cx, top_banner_y, cw, top_banner_h, renderer->a(banner_bg));
        draw_outline(renderer, cx, top_banner_y, cw, top_banner_h,
                     (remaining_refresh_pending || bedtime_res || eye_res || dock_res) ? 2 : 1,
                     banner_border);

        char quota_label[32];
        char quota_val[32];
        char quota_note[32];
        ptc_overlay_format_child_quota_parts(&summary,
            quota_label, sizeof(quota_label),
            quota_val, sizeof(quota_val),
            quota_note, sizeof(quota_note));

        draw_localized(renderer, quota_label, false, cx + 10, top_banner_y + 21, 11, renderer->a(MUTED_COLOR));
        draw_localized(renderer, quota_val, false, cx + 70, top_banner_y + 23, 15, renderer->a(TEXT_COLOR));
        if (quota_note[0]) {
            draw_localized(renderer, quota_note, false, cx + 124, top_banner_y + 21, 11, renderer->a(MUTED_COLOR));
        }

        const char *banner_title = bedtime_res ? ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_2) :
            (eye_res ? ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING) :
             (dock_res ? ptc_ui_text(PTC_UI_T_DOCK_BLOCKED) :
              (success_visible_ ? ptc_ui_text(PTC_UI_T_STILL_PLAYABLE_AFTER_MODIFICATION) : ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY))));
        draw_localized(renderer, banner_title, false,
                              cx + 10, top_banner_y + 51, 11,
                              renderer->a(bedtime_res || dock_res ? ERROR_COLOR : (eye_res ? WAITING_COLOR : MUTED_COLOR)));

        const bool unlimited_today = summary.valid && summary.unrestricted_today == 1;
        const tsl::Color remaining_accent = remaining_refresh_pending ? WAITING_COLOR :
            (bedtime_res || dock_res ? ERROR_COLOR : (eye_res ? WAITING_COLOR :
             (summary.valid && (summary.remaining_available || unlimited_today) ? SUCCESS_COLOR : MUTED_COLOR)));
        renderer->drawRect(cx + 108, top_banner_y + 34, 104, 34, renderer->a(KEY_COLOR));
        renderer->drawRect(cx + 108, top_banner_y + 66, 104, 2, renderer->a(remaining_accent));
        if (remaining_refresh_pending) {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_REFRESHING_2), false, cx + 116, top_banner_y + 58, 14,
                                 renderer->a(WAITING_COLOR));
        } else if (bedtime_res) {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_RESTRICTED), false, cx + 124, top_banner_y + 59, 18, renderer->a(ERROR_COLOR));
        } else if (dock_res) {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_TV_BADGE), false, cx + 120, top_banner_y + 59, 16, renderer->a(ERROR_COLOR));
        } else if (eye_res) {
            int sec = summary.eye_care_rest_remaining_seconds > 0 ? summary.eye_care_rest_remaining_seconds : 0;
            std::snprintf(line, sizeof(line), "%02d:%02d", sec / 60, sec % 60);
            draw_localized(renderer, line, false, cx + 114, top_banner_y + 60, 20, renderer->a(WAITING_COLOR));
        } else if (unlimited_today) {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED), false, cx + 126, top_banner_y + 62, 22, renderer->a(SUCCESS_COLOR));
        } else if (summary.valid && summary.remaining_available) {
            std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_D_MIN), summary.remaining_minutes);
            draw_localized(renderer, line, false, cx + 114, top_banner_y + 62, 22, renderer->a(SUCCESS_COLOR));
        } else {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_UNAVAILABLE), false, cx + 116, top_banner_y + 58, 14, renderer->a(MUTED_COLOR));
        }

        const bool busy = bridge_->waiting;
        renderer->drawRect(cx + PTC_OVERLAY_REFRESH_X, cy + PTC_OVERLAY_REFRESH_Y,
                           PTC_OVERLAY_REFRESH_W, PTC_OVERLAY_REFRESH_H,
                           renderer->a(busy ? DISABLED_COLOR : FOCUS_BG));
        draw_outline(renderer, cx + PTC_OVERLAY_REFRESH_X, cy + PTC_OVERLAY_REFRESH_Y,
                     PTC_OVERLAY_REFRESH_W, PTC_OVERLAY_REFRESH_H, 1,
                     remaining_refresh_pending ? WAITING_COLOR : (busy ? MUTED_COLOR : FOCUS_BORDER));
        draw_localized(renderer, remaining_refresh_pending ? ptc_ui_text(PTC_UI_T_CONFIRMING) : (busy ? ptc_ui_text(PTC_UI_T_REFRESHING) : ptc_ui_text(PTC_UI_T_Y_REFRESH)),
                             false, cx + PTC_OVERLAY_REFRESH_X + 18,
                             cy + PTC_OVERLAY_REFRESH_Y + 20, 12,
                             renderer->a(remaining_refresh_pending ? WAITING_COLOR :
                                         (busy ? MUTED_COLOR : TEXT_COLOR)));
        draw_localized(renderer, remaining_refresh_pending ? ptc_ui_text(PTC_UI_T_WAIT_FOR_THE_RESULT_AFTER_SUBMISSION) :
                             (status_is_stale() ? ptc_ui_text(PTC_UI_T_THE_DATA_MAY_BE_OUT_OF_DATE) : age),
                             false, cx + PTC_OVERLAY_REFRESH_X, top_banner_y + 64, 11,
                             renderer->a(remaining_refresh_pending ? WAITING_COLOR :
                                         (status_is_stale() ? ERROR_COLOR : MUTED_COLOR)));

        // --- 1. Header Prompt & Guidance (受限时间与护眼提醒) ---
        const bool is_dock_res = summary.valid && summary.dock_restriction_active;
        const bool is_bedtime_res = bedtime_restricted();
        const bool is_eye_res = eye_care_restricted();
        const bool is_restricted = has_status_snapshot_ && !status_is_stale() &&
            (is_dock_res || is_bedtime_res || is_eye_res);

        if (is_restricted) {
            // --- 场景感知受限全景卡片 (Dynamic Restriction Hero Card) ---
            const s32 hero_y = cy + 104;
            const s32 hero_h = 216;
            const tsl::Color card_bg = is_eye_res ? WARNING_BG : DANGER_BG;
            const tsl::Color card_border = is_eye_res ? WAITING_COLOR : ERROR_COLOR;

            renderer->drawRect(cx, hero_y, cw, hero_h, renderer->a(card_bg));
            draw_outline(renderer, cx, hero_y, cw, hero_h, 2, card_border);

            if (is_dock_res) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_TITLE), false, cx + 16, hero_y + 30, 18, renderer->a(ERROR_COLOR));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_BLOCKED_BANNER), false, cx + 16, hero_y + 56, 13, renderer->a(TEXT_COLOR), 330);
                renderer->drawRect(cx + 16, hero_y + 76, cw - 32, 1, renderer->a(MUTED_COLOR));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_CONNECT), false, cx + 16, hero_y + 106, 12, renderer->a(WAITING_COLOR), 330);
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_TV_ACTIVE_NOTE), false, cx + 16, hero_y + 140, 11, renderer->a(MUTED_COLOR), 330);
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_RULE_EXPLANATION), false, cx + 16, hero_y + 168, 11, renderer->a(MUTED_COLOR), 330);
            } else if (is_bedtime_res) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_2), false, cx + 16, hero_y + 30, 18, renderer->a(ERROR_COLOR));
                char bt_window[64];
                std::snprintf(bt_window, sizeof(bt_window), "%02u:%02u ~ %02u:%02u",
                    summary.bedtime_start_minute / 60, summary.bedtime_start_minute % 60,
                    summary.bedtime_end_minute / 60, summary.bedtime_end_minute % 60);
                std::snprintf(line, sizeof(line), "%s (%s)", ptc_ui_text(PTC_UI_T_BEDTIME_LIMIT), bt_window);
                draw_localized(renderer, line, false, cx + 16, hero_y + 56, 13, renderer->a(TEXT_COLOR), 330);
                renderer->drawRect(cx + 16, hero_y + 76, cw - 32, 1, renderer->a(MUTED_COLOR));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_IT_IS_CURRENTLY_IN_THE_BEDTIME_PERIOD), false, cx + 16, hero_y + 108, 12, renderer->a(WAITING_COLOR), 330);
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_FOR_EXCEPTIONS_SELECT_SKIP_TONIGHT_BEDTIME_IN), false, cx + 16, hero_y + 150, 11, renderer->a(MUTED_COLOR), 330);
            } else if (is_eye_res) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING), false, cx + 16, hero_y + 28, 18, renderer->a(WAITING_COLOR));
                int sec = summary.eye_care_rest_remaining_seconds > 0 ? summary.eye_care_rest_remaining_seconds : 0;
                std::snprintf(line, sizeof(line), "%02d:%02d", sec / 60, sec % 60);
                draw_localized(renderer, line, false, cx + 16, hero_y + 68, 30, renderer->a(WAITING_COLOR));
                renderer->drawRect(cx + 16, hero_y + 90, cw - 32, 1, renderer->a(MUTED_COLOR));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_MESSAGE_2), false, cx + 16, hero_y + 120, 12, renderer->a(TEXT_COLOR), 330);
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_CONFIRM_BODY), false, cx + 16, hero_y + 162, 11, renderer->a(MUTED_COLOR), 330);
            }

            // --- 快捷解除/调整操作卡片 (Quick Action Card) ---
            const s32 action_y = cy + 332;
            const s32 action_h = 76;
            renderer->drawRect(cx, action_y, cw, action_h, renderer->a(FOCUS_BG));
            draw_outline(renderer, cx, action_y, cw, action_h, 2, FOCUS_BORDER);

            if (is_dock_res) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_WAIVE), false, cx + 16, action_y + 26, 14, renderer->a(TEXT_COLOR));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_WAIVE_BODY), false, cx + 16, action_y + 50, 11, renderer->a(MUTED_COLOR), 330);
            } else if (is_bedtime_res) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_SKIP_BEDTIME), false, cx + 16, action_y + 26, 14, renderer->a(TEXT_COLOR));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_SKIP_WILL_BE_CLEARED_BEDTIME_RESTRICTIONS_WILL), false, cx + 16, action_y + 50, 11, renderer->a(MUTED_COLOR), 330);
            } else if (is_eye_res) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP), false, cx + 16, action_y + 26, 14, renderer->a(TEXT_COLOR));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_CONFIRM_BODY), false, cx + 16, action_y + 50, 11, renderer->a(MUTED_COLOR), 330);
            }
        } else {
            // --- 1. Header Prompt & Guidance (受限时间与护眼提醒) ---
            char restriction_guidance[128];
            ptc_overlay_format_child_restriction_guidance(&summary, restriction_guidance, sizeof(restriction_guidance));
            const bool restriction_urgent = (summary.valid &&
                ((summary.bedtime_active && !summary.bedtime_skipped) ||
                 summary.daily_restriction_active || summary.dock_restriction_active ||
                 (summary.eye_care_enabled &&
                  std::strcmp(summary.eye_care_phase, "resting") == 0)));
            draw_localized(renderer, restriction_guidance, false, cx + 5, cy + 94, 12,
                renderer->a(restriction_urgent ? ERROR_COLOR : FOCUS_BORDER), 350);

            if (summary.dock_available) {
                if (status_is_stale()) std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
                else ptc_overlay_format_dock_usage(&summary, line, sizeof(line));
                draw_localized(renderer, line, false, cx + 5, cy + 164, 11, renderer->a(MUTED_COLOR), 350);
            }

            // --- 2. Code Display Slots (8位卡片槽) ---
            const s32 slot_y = cy + PTC_OVERLAY_SLOT_Y;
            const s32 slot_w = PTC_OVERLAY_SLOT_W;
            const s32 slot_h = PTC_OVERLAY_SLOT_H;
            const s32 slot_gap = PTC_OVERLAY_SLOT_GAP;
            const s32 slot_group_w = PTC_OVERLAY_CODE_SYMBOLS * slot_w +
                (PTC_OVERLAY_CODE_SYMBOLS - 1) * slot_gap;
            const s32 slot_start_x = cx + (cw - slot_group_w) / 2;

            for (unsigned int index = 0; index < PTC_OVERLAY_CODE_SYMBOLS; ++index) {
                const s32 sx = slot_start_x + static_cast<s32>(index) * (slot_w + slot_gap);
                const bool is_cursor = (input_->length < PTC_OVERLAY_CODE_SYMBOLS) && (index == input_->length);

                renderer->drawRect(sx, slot_y, slot_w, slot_h, renderer->a(is_cursor ? FOCUS_BG : CARD_COLOR));
                draw_outline(renderer, sx, slot_y, slot_w, slot_h, is_cursor ? 3 : 1, is_cursor ? FOCUS_BORDER : MUTED_COLOR);

                char symbol[8] = {0};
                if (index < input_->length) {
                    symbol[0] = input_->symbols[index];
                    symbol[1] = '\0';
                } else if (is_cursor) {
                    symbol[0] = '_';
                    symbol[1] = '\0';
                } else {
                    std::snprintf(symbol, sizeof(symbol), " | ");
                }
                const auto symbol_size = draw_localized(renderer,
                    symbol, false, 0, 0, 30, tsl::style::color::ColorTransparent);
                draw_localized(renderer, symbol, false,
                    sx + (slot_w - static_cast<s32>(symbol_size.first)) / 2,
                    slot_y + 36, 30,
                    renderer->a(index < input_->length ? TEXT_COLOR : (is_cursor ? FOCUS_BORDER : MUTED_COLOR)));
            }

            char console_date[48];
            format_console_date(summary, console_date, sizeof(console_date));
            std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_ENTERED_U_8_DIGITS_SELECTED_C_S), input_->length,
                          ptc_overlay_input_charset()[input_->cursor], console_date);
            draw_localized(renderer, line, false, cx + 5, cy + 180, 14, renderer->a(MUTED_COLOR));

            // --- 3. Keypad 3x4 Grid ---
            const char *charset = ptc_overlay_input_charset();
            const s32 panel_y = cy + PTC_OVERLAY_KEYPAD_Y;
            const s32 panel_h = PTC_OVERLAY_KEYPAD_H;
            renderer->drawRect(cx, panel_y, cw, panel_h, renderer->a(PANEL_COLOR));
            draw_outline(renderer, cx, panel_y, cw, panel_h, 1, MUTED_COLOR);

            for (unsigned int index = 0; index < PTC_OVERLAY_KEY_COUNT; ++index) {
                char symbol[2] = { charset[index], '\0' };
                const PtcOverlayRect key = ptc_overlay_key_rect(cx, cy, index);
                const bool focused = (index == input_->cursor);

                renderer->drawRect(key.x, key.y, key.w, key.h, renderer->a(focused ? FOCUS_BG : KEY_COLOR));
                draw_outline(renderer, key.x, key.y, key.w, key.h, focused ? 3 : 1, focused ? FOCUS_BORDER : MUTED_COLOR);
                const auto key_text_size = draw_localized(renderer,
                    symbol, false, 0, 0, 24, tsl::style::color::ColorTransparent);
                draw_localized(renderer, symbol, false, key.x + (key.w - static_cast<s32>(key_text_size.first)) / 2,
                                     key.y + 30, 24, renderer->a(focused ? FOCUS_BORDER : TEXT_COLOR));
            }

            const PtcOverlayRect backspace = ptc_overlay_backspace_rect(cx, cy);
            renderer->drawRect(backspace.x, backspace.y, backspace.w, backspace.h, renderer->a(KEY_COLOR));
            draw_outline(renderer, backspace.x, backspace.y, backspace.w, backspace.h, 1, BACKSPACE_BORDER);
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_X_BACKSPACE), false, backspace.x + 28, backspace.y + 26, 12, renderer->a(BACKSPACE_BORDER));

            const PtcOverlayRect clear = ptc_overlay_clear_rect(cx, cy);
            renderer->drawRect(clear.x, clear.y, clear.w, clear.h, renderer->a(KEY_COLOR));
            draw_outline(renderer, clear.x, clear.y, clear.w, clear.h, 1, CLEAR_BORDER);
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_CLICK_TO_CLEAR), false, clear.x + 23, clear.y + 26, 12, renderer->a(CLEAR_BORDER));

            // --- 4. Control & Submit Bar ---
            const bool code_unavailable = has_status_snapshot_ && !status_is_stale() &&
                (summary.unrestricted_today == 1 || summary.eye_care_unlimited_capped || summary.dock_unlimited_capped);
            const bool dock_restricted = has_status_snapshot_ && !status_is_stale() && summary.dock_restriction_active;
            const bool can_submit = !bedtime_restricted() && !eye_care_restricted() && !code_unavailable && !dock_restricted &&
                ptc_overlay_request_action_enabled(bridge_->waiting) &&
                ptc_overlay_input_can_submit(input_);
            const s32 submit_y = cy + PTC_OVERLAY_SUBMIT_Y;
            const s32 submit_h = PTC_OVERLAY_SUBMIT_H;

            renderer->drawRect(cx, submit_y, cw, submit_h, renderer->a(can_submit ? FOCUS_BG : DISABLED_COLOR));
            draw_outline(renderer, cx, submit_y, cw, submit_h, can_submit ? 3 : 1, can_submit ? FOCUS_BORDER : MUTED_COLOR);

            if (can_submit) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_SUBMIT_PLAYTIME_GRANT_CLICK_OR_PRESS), false, cx + 50, submit_y + 24, 14, renderer->a(TEXT_COLOR));
            } else if (code_unavailable) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_NO_TIME_LIMIT_TODAY_GRANT_CODES_ARE), false,
                    cx + 65, submit_y + 24, 13, renderer->a(WAITING_COLOR));
            } else if (bridge_->waiting) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_BACKGROUND_PROCESSING_YOU_CAN_CONTINUE_TO_EDIT), false, cx + 66, submit_y + 24, 13,
                                     renderer->a(MUTED_COLOR));
            } else {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_SUBMIT_GRANT_NEED_TO_ENTER_8_DIGITS), false, cx + 58, submit_y + 24, 13, renderer->a(MUTED_COLOR));
            }
        }

        // --- 5. Collapsible Status Panel (可折叠命令与状态栏) ---
        const s32 status_y = cy + PTC_OVERLAY_STATUS_Y;
        const s32 status_w = cw;

        if (!status_expanded_) {
            renderer->drawRect(cx, status_y, status_w, PTC_OVERLAY_STATUS_COLLAPSED_H, renderer->a(PANEL_COLOR));
            draw_outline(renderer, cx, status_y, status_w, PTC_OVERLAY_STATUS_COLLAPSED_H, 1, MUTED_COLOR);

            if (bridge_->waiting && active_request_kind_ == OverlayRequestKind::Status) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_REFRESHING_STATUS_PRESS_TO_EXPAND), false, cx + 12, status_y + 21, 12, renderer->a(FOCUS_BORDER));
            } else if (bridge_->waiting) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_PROCESSING_GRANT_PRESS_TO_EXPAND), false, cx + 12, status_y + 21, 12, renderer->a(FOCUS_BORDER));
            } else if (error_) {
                draw_localized(renderer,
                    (last_request_kind_ == OverlayRequestKind::OfflineCode ||
                     last_request_kind_ == OverlayRequestKind::PreviewOfflineCode)
                        ? ptc_ui_text(PTC_UI_T_REQUEST_FAILED_PRESS_TO_EXPAND_PLEASE_RE)
                        : ptc_ui_text(PTC_UI_T_REQUEST_FAILED_PRESS_TO_EXPAND_Y_TO),
                    false, cx + 12, status_y + 21, 12, renderer->a(ERROR_COLOR));
            } else if (success_visible_) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_GRANT_SUCCESSFUL_PRESS_TO_EXPAND), false, cx + 12, status_y + 21, 12, renderer->a(SUCCESS_COLOR));
            } else if (has_status_snapshot_) {
                char restriction_summary[128];
                ptc_overlay_format_child_restriction_summary(&summary, restriction_summary, sizeof(restriction_summary));
                draw_localized(renderer, restriction_summary, false, cx + 12, status_y + 21, 12,
                                     renderer->a(MUTED_COLOR));
            } else {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_COMMANDS_AND_STATUS_CLICK_OR_PRESS), false, cx + 12, status_y + 21, 12, renderer->a(MUTED_COLOR));
            }
        } else {
            const s32 expanded_h = status_needs_detail()
                ? PTC_OVERLAY_STATUS_DETAIL_H : PTC_OVERLAY_STATUS_NORMAL_H;
            renderer->drawRect(cx, status_y, status_w, expanded_h, renderer->a(PANEL_COLOR));
            draw_outline(renderer, cx, status_y, status_w, expanded_h, 2, FOCUS_BORDER);

            if (error_) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_COMMAND_AND_STATUS_DETAILS_PRESS_TO_CLOSE), false, cx + 12, status_y + 18, 12, renderer->a(FOCUS_BORDER));
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_S_COMMAND_S), bridge_->waiting ? ptc_ui_text(PTC_UI_T_CURRENT) : ptc_ui_text(PTC_UI_T_RECENT), request_label());
                draw_localized(renderer, line, false, cx + 12, status_y + 36, 12, renderer->a(TEXT_COLOR));
                draw_localized(renderer, ptc_overlay_bridge_transport_label(bridge_), false, cx + 12, status_y + 52, 11, renderer->a(MUTED_COLOR));
                const char *message = ptc_overlay_bridge_error_message_zh(bridge_);
                draw_localized(renderer, message, false, cx + 12, status_y + 72, 12, renderer->a(ERROR_COLOR), 290);
                const bool code_error = last_request_kind_ == OverlayRequestKind::OfflineCode ||
                    last_request_kind_ == OverlayRequestKind::PreviewOfflineCode;
                if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                    std::snprintf(line, sizeof(line), code_error ? ptc_ui_text(PTC_UI_T_ERROR_CODE_D_RE_ENTER) :
                                  ptc_ui_text(PTC_UI_T_ERROR_CODE_D_Y_RETRY), bridge_->summary.error_code);
                    draw_localized(renderer, line, false, cx + 12, status_y + 108, 11, renderer->a(ERROR_COLOR));
                } else {
                    draw_localized(renderer, code_error ? ptc_ui_text(PTC_UI_T_PLEASE_RE_ENTER_Y_RETURNS_TO_INPUT) : ptc_ui_text(PTC_UI_T_PRESS_Y_TO_TRY_AGAIN),
                                         false, cx + 12, status_y + 108, 11, renderer->a(ERROR_COLOR));
                }
            } else if (success_visible_) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_COMMAND_AND_STATUS_DETAILS_PRESS_TO_CLOSE), false, cx + 12, status_y + 18, 12, renderer->a(FOCUS_BORDER));
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_S_COMMAND_S), bridge_->waiting ? ptc_ui_text(PTC_UI_T_CURRENT) : ptc_ui_text(PTC_UI_T_RECENT), request_label());
                draw_localized(renderer, line, false, cx + 12, status_y + 36, 12, renderer->a(TEXT_COLOR));
                draw_localized(renderer, ptc_overlay_bridge_transport_label(bridge_), false, cx + 12, status_y + 52, 11, renderer->a(MUTED_COLOR));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_GRANT_SUCCESSFUL_2), false, cx + 12, status_y + 74, 16, renderer->a(SUCCESS_COLOR));
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_D_MIN_LEFT_AFTER_CHANGE), summary.remaining_minutes);
                draw_localized(renderer, line, false, cx + 12, status_y + 96, 15, renderer->a(SUCCESS_COLOR));
                if (summary.played_minutes_available) {
                    std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_ESTIMATED_USAGE_D_MIN_CLOSING_SHORTLY), summary.played_minutes);
                    draw_localized(renderer, line, false, cx + 12, status_y + 116, 12, renderer->a(SUCCESS_COLOR));
                } else {
                    draw_localized(renderer, ptc_ui_text(PTC_UI_T_THE_STATUS_HAS_BEEN_REFRESHED_AND_WILL), false, cx + 12, status_y + 116, 12, renderer->a(SUCCESS_COLOR));
                }
            } else if (bridge_->waiting) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_COMMAND_AND_STATUS_DETAILS_PRESS_TO_CLOSE), false, cx + 12, status_y + 18, 12, renderer->a(FOCUS_BORDER));
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_S_COMMAND_S), ptc_ui_text(PTC_UI_T_CURRENT), request_label());
                draw_localized(renderer, line, false, cx + 12, status_y + 36, 12, renderer->a(TEXT_COLOR));
                draw_localized(renderer, ptc_overlay_bridge_transport_label(bridge_), false, cx + 12, status_y + 52, 11, renderer->a(MUTED_COLOR));
                const char *stage = transport_stage(bridge_);
                if (stage[0]) {
                    draw_localized(renderer, stage, false, cx + 12, status_y + 72, 12, renderer->a(FOCUS_BORDER));
                }
            } else if (has_status_snapshot_) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_AND_RESTRICTION_DETAILS_PRESS), false, cx + 12, status_y + 18, 12, renderer->a(FOCUS_BORDER));
                char total_str[32];
                if (summary.unrestricted_today == 1) {
                    std::snprintf(total_str, sizeof(total_str), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
                } else if (summary.remaining_available && summary.played_minutes_available &&
                           summary.remaining_minutes >= 0 && summary.played_minutes >= 0) {
                    std::snprintf(total_str, sizeof(total_str), ptc_ui_text(PTC_UI_T_D_MIN),
                        summary.remaining_minutes + summary.played_minutes);
                } else if (summary.remaining_available && summary.remaining_minutes >= 0) {
                    std::snprintf(total_str, sizeof(total_str), ptc_ui_text(PTC_UI_T_D_MIN), summary.remaining_minutes);
                } else {
                    std::snprintf(total_str, sizeof(total_str), ptc_ui_text(PTC_UI_T_MINUTES_3));
                }
                const char *rule_lbl = ptc_overlay_rule_source_label(summary.rule_source);
                if (summary.played_minutes_available && summary.played_minutes >= 0) {
                    std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_TODAY_QUOTA_S_S_PLAYED_ABOUT_D),
                        total_str, rule_lbl, summary.played_minutes);
                } else {
                    std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_TODAY_QUOTA_S_S), total_str, rule_lbl);
                }
                draw_localized(renderer, line, false, cx + 12, status_y + 36, 12, renderer->a(TEXT_COLOR));

                char restriction[96];
                ptc_overlay_format_child_restriction_detail(&summary, restriction, sizeof(restriction));
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_LIMITED_S), restriction);
                draw_localized(renderer, line, false, cx + 12, status_y + 54, 12, renderer->a(FOCUS_BORDER), 340);

                char buffer_buf[64];
                ptc_overlay_format_child_buffer_status(&summary, buffer_buf, sizeof(buffer_buf));
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_SELF_BUFFER_S_S), buffer_buf, ptc_overlay_bridge_transport_label(bridge_));
                draw_localized(renderer, line, false, cx + 12, status_y + 72, 11, renderer->a(MUTED_COLOR));
                if (summary.dock_available) {
                    ptc_overlay_format_dock_usage(&summary, line, sizeof(line));
                    draw_localized(renderer, line, false, cx + 12, status_y + 90, 11, renderer->a(FOCUS_BORDER), 340);
                }
            } else {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_COMMAND_AND_STATUS_DETAILS_PRESS_TO_CLOSE), false, cx + 12, status_y + 18, 12, renderer->a(FOCUS_BORDER));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_THE_STATUS_HAS_NOT_BEEN_OBTAINED_YET), false, cx + 12, status_y + 36, 12, renderer->a(MUTED_COLOR));
            }
        }
        if (!status_expanded_) {
            const PtcOverlayRect buffer = ptc_overlay_child_buffer_rect(cx, cy);
            const PtcOverlayRect parent = ptc_overlay_child_parent_rect(cx, cy, cw);
            const bool buffer_ready = summary.valid && summary.daily_buffer_available &&
                !bedtime_restricted() && !summary.dock_restriction_active && !bridge_->waiting;
            renderer->drawRect(buffer.x, buffer.y, buffer.w, buffer.h,
                renderer->a(buffer_ready ? FOCUS_BG : CARD_COLOR));
            draw_outline(renderer, buffer.x, buffer.y, buffer.w, buffer.h, 1,
                buffer_ready ? FOCUS_BORDER : MUTED_COLOR);
            draw_localized(renderer, buffer_ready ? ptc_ui_text(PTC_UI_T_L_RECEIVE_AUTONOMOUS_BUFFER) :
                (summary.daily_buffer_claimed ? ptc_ui_text(PTC_UI_T_BUFFER_RECEIVED_TODAY) : ptc_ui_text(PTC_UI_T_THE_BUFFER_IS_TEMPORARILY_UNAVAILABLE)),
                false, buffer.x + 9, buffer.y + 27, 12,
                renderer->a(buffer_ready ? TEXT_COLOR : MUTED_COLOR));
            renderer->drawRect(parent.x, parent.y, parent.w, parent.h, renderer->a(FOCUS_BG));
            draw_outline(renderer, parent.x, parent.y, parent.w, parent.h, 1, FOCUS_BORDER);
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_R_PARENT_AREA), false, parent.x + 29, parent.y + 27, 13,
                renderer->a(TEXT_COLOR));
        }

    }
