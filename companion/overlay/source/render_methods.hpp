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

    static void draw_card(
        tsl::gfx::Renderer *renderer,
        s32 x,
        s32 y,
        s32 width,
        s32 height,
        tsl::Color bg,
        tsl::Color border,
        s32 border_width = 1)
    {
        renderer->drawRect(x, y, width, height, renderer->a(bg));
        if (border_width > 0 && border.a > 0) {
            draw_outline(renderer, x, y, width, height, border_width, border);
        }
    }

    static void draw_pill(
        tsl::gfx::Renderer *renderer,
        s32 x,
        s32 y,
        s32 width,
        s32 height,
        tsl::Color bg,
        tsl::Color border,
        s32 border_width = 1)
    {
        renderer->drawRect(x, y, width, height, renderer->a(bg));
        if (border_width > 0 && border.a > 0) {
            draw_outline(renderer, x, y, width, height, border_width, border);
        }
    }

    const OverlayPalette &current_palette() const
    {
        return get_overlay_palette(theme_);
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

    bool is_keypad_mode() const
    {
        return keypad_active_ || (input_ != nullptr && input_->length > 0);
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

    PtcPolicyStatus child_policy_status(const PtcCompanionResultSummary &summary) const
    {
        int day = -1, second = -1;
        bool clock_fresh = true;
#ifdef PTC_OVERLAY_PREVIEW_CLOCK
        day = summary.day_index;
        second = 496 * 60;
#else
        const long long now = static_cast<long long>(std::time(nullptr));
        (void)ptc_policy_local_clock(now, &day, &second);
        clock_fresh = last_refresh_wall_time_ > 0 && now >= last_refresh_wall_time_ &&
            now - last_refresh_wall_time_ < 30;
#endif
        PtcPolicyStatus policies;
        bool fresh = clock_fresh && has_status_snapshot_ && last_refresh_tick_ != 0 && !status_is_stale() &&
            !(error_ && last_request_kind_ == OverlayRequestKind::Status);
        ptc_policy_status_project(&summary, fresh, day, second, &policies);
        return policies;
    }

    void draw_child_policies(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw,
        const PtcPolicyStatus &policies)
    {
        const auto &pal = get_overlay_palette(theme_);
        if (!status_expanded_) {
            const auto card = ptc_overlay_child_policy_rect(cx, cy, cw);
            draw_card(renderer, card.x, card.y, card.w, card.h, pal.card_bg, pal.border_decorative, 1);
        }
        for (int row = 0; row < 2; ++row) {
            const PtcPolicyDisplay &policy = row == 0 ? policies.bedtime : policies.dock;
            char text[384];
            const char *label = ptc_ui_text(row == 0 ? PTC_UI_T_POLICY_BED_LABEL :
                policies.mode == PTC_UI_T_STATUS_TO_CONFIRM ? PTC_UI_T_POLICY_DOCK_LABEL : policies.mode);
            std::snprintf(text, sizeof(text), "%s: %s", label, policy.detail);
            if (static_cast<s32>(measure_localized(renderer, text, PTC_OVERLAY_FONT_SECONDARY).first) > cw - 28)
                std::snprintf(text, sizeof(text), "%s: %s", label, policy.compact);
            const auto color = policy.state == PTC_POLICY_ACTIVE ? pal.danger :
                policy.state == PTC_POLICY_PENDING || policy.state == PTC_POLICY_WAITING ||
                policy.state == PTC_POLICY_EXHAUSTED ? pal.warning :
                policy.state == PTC_POLICY_WAIVED || policy.state == PTC_POLICY_SKIPPED ?
                pal.success : pal.text_secondary;
            draw_localized_fitted(renderer, text, cx + 14,
                ptc_overlay_policy_row_y(cy, status_expanded_, row), PTC_OVERLAY_FONT_SECONDARY, renderer->a(color), cw - 28);
        }
    }

    bool status_needs_detail() const
    {
        return error_ || success_visible_;
    }

    void draw_code_preview(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw)
    {
        const auto &pal = current_palette();
        char line[128];
        draw_card(renderer, cx, cy + 10, cw, 550, pal.panel_bg, pal.accent, 2);
        draw_localized(renderer, preview_changed_ ? ptc_ui_text(PTC_UI_T_STATUS_CHANGED_PLEASE_CONFIRM_AGAIN) : ptc_ui_text(PTC_UI_T_CONFIRM_PLAYTIME_GRANT),
                             false, cx + 14, cy + 46, PTC_OVERLAY_FONT_TITLE, renderer->a(pal.text_primary));
        ptc_ui_format_actual_added(preview_summary_.grant_minutes, line, sizeof(line));
        draw_localized(renderer, line, false, cx + 14, cy + 82, PTC_OVERLAY_FONT_TITLE, renderer->a(pal.accent));
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_VALID_TODAY_CAN_ONLY_BE_USED_ONCE), false,
                             cx + 14, cy + 114, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_secondary));

        const s32 card1_x = cx + 12;
        const s32 card1_y = cy + 138;
        const s32 card1_w = cw - 24;
        const s32 card1_h = 86;
        draw_card(renderer, card1_x, card1_y, card1_w, card1_h, pal.card_bg, pal.border_decorative, 1);
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY), false, card1_x + 14, card1_y + 48, PTC_OVERLAY_FONT_BODY_LG, renderer->a(pal.text_secondary));
        if (preview_summary_.converts_unlimited_to_limited) {
            draw_localized_right(renderer, ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED), card1_x + card1_w - 14, card1_y + 51, PTC_OVERLAY_FONT_DISPLAY, pal.success);
        } else if (preview_summary_.remaining_available) {
            ptc_ui_format_minutes(preview_summary_.remaining_minutes, line, sizeof(line));
            draw_localized_right(renderer, line, card1_x + card1_w - 14, card1_y + 51, PTC_OVERLAY_FONT_DISPLAY, pal.success);
        } else {
            draw_localized_right(renderer, ptc_ui_text(PTC_UI_T_UNAVAILABLE), card1_x + card1_w - 14, card1_y + 51, PTC_OVERLAY_FONT_BODY_LG, pal.text_disabled);
        }

        const s32 card2_x = cx + 12;
        const s32 card2_y = cy + 238;
        const s32 card2_w = cw - 24;
        const s32 card2_h = 86;
        draw_card(renderer, card2_x, card2_y, card2_w, card2_h, pal.card_bg, pal.border_decorative, 1);
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_ESTIMATED_TIME_AFTER_GRANT), false, card2_x + 14, card2_y + 48, PTC_OVERLAY_FONT_BODY_LG, renderer->a(pal.text_secondary));
        if (preview_summary_.remaining_after_available) {
            ptc_ui_format_minutes(preview_summary_.remaining_after_minutes, line, sizeof(line));
            const tsl::Color after_col = (preview_summary_.remaining_after_minutes == 0) ? pal.danger : pal.success;
            draw_localized_right(renderer, line, card2_x + card2_w - 14, card2_y + 51, PTC_OVERLAY_FONT_DISPLAY, after_col);
        } else {
            draw_localized_right(renderer, ptc_ui_text(PTC_UI_T_UNAVAILABLE), card2_x + card2_w - 14, card2_y + 51, PTC_OVERLAY_FONT_BODY_LG, pal.danger);
        }

        const PtcOverlayPreviewVisualLevel preview_level = ptc_overlay_preview_visual_level(
            preview_summary_.remaining_after_available,
            preview_summary_.remaining_after_minutes,
            preview_summary_.preview_capped,
            preview_summary_.converts_unlimited_to_limited);
        const bool dangerous = preview_level == PTC_OVERLAY_PREVIEW_DANGER;
        if (preview_level != PTC_OVERLAY_PREVIEW_NEUTRAL) {
            const tsl::Color alert_bg = dangerous ? pal.danger_soft : pal.warning_soft;
            const tsl::Color alert_border = dangerous ? pal.danger_border : pal.warning_border;
            draw_card(renderer, cx + 12, cy + 338, cw - 24, 52, alert_bg, alert_border, 1);
        }
        if (preview_summary_.converts_unlimited_to_limited) {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_WARNING_WILL_CHANGE_FROM_UNLIMITED_TO_LIMITED), false, cx + 22, cy + 370, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.danger));
        } else if (!preview_summary_.remaining_after_available) {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_WARNING_CANNOT_CONFIRM_PLAYTIME_AFTER_GRANT_RIGHT), false, cx + 22, cy + 370, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.danger));
        } else if (preview_summary_.remaining_after_minutes == 0) {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_WARNING_ESTIMATED_NO_PLAYTIME_REMAINING_AFTER_GRANT), false, cx + 22, cy + 370, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.danger));
        } else if (preview_summary_.preview_capped) {
            ptc_ui_format_daily_cap_applied(preview_summary_.effective_add_minutes, line, sizeof(line));
            draw_localized(renderer, line, false, cx + 22, cy + 370, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.warning));
        } else {
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_CODE_WILL_NOT_BE_CONSUMED_BEFORE_CONFIRMATION), false, cx + 16, cy + 368, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_secondary));
        }
        draw_localized(renderer, touch_hold_warning_ ? ptc_ui_text(PTC_UI_T_PLEASE_HOLD_A_ON_CONTROLLER_TO_CONFIRM) :
                             (dangerous ? ptc_ui_text(PTC_UI_T_HOLD_A_1S_TO_CONFIRM_B_CANCEL) : ptc_ui_text(PTC_UI_T_A_CONFIRM_B_CANCEL)),
                             false, cx + 16, cy + 418, PTC_OVERLAY_FONT_SECONDARY,
                             renderer->a(dangerous ? pal.danger : pal.accent));

        const s32 btn_cancel_x = cx;
        const s32 btn_cancel_w = 145;
        const s32 btn_y = cy + 490;
        const s32 btn_h = 54;
        draw_card(renderer, btn_cancel_x, btn_y, btn_cancel_w, btn_h, pal.card_bg, pal.border_control, 1);
        draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_B_CANCEL_2), btn_cancel_x, btn_y, btn_cancel_w, btn_h, PTC_OVERLAY_FONT_BTN, pal.text_primary);

        const s32 btn_confirm_x = cx + 160;
        const s32 btn_confirm_w = cw - 160;
        const tsl::Color confirm_bg = dangerous ? pal.danger_soft : pal.accent_soft;
        const tsl::Color confirm_border = dangerous ? pal.danger : pal.accent;
        draw_card(renderer, btn_confirm_x, btn_y, btn_confirm_w, btn_h, confirm_bg, confirm_border, 2);
        draw_localized_center(renderer, dangerous ? ptc_ui_text(PTC_UI_T_HOLD_A_TO_CONFIRM) : ptc_ui_text(PTC_UI_T_A_CONFIRM_2),
                             btn_confirm_x, btn_y, btn_confirm_w, btn_h, PTC_OVERLAY_FONT_BTN, dangerous ? pal.danger : pal.text_primary);
        if (dangerous) {
            const int progress = confirm_hold_.fired ? 0 :
                ptc_overlay_hold_progress(&confirm_hold_, 1000);
            renderer->drawRect(btn_confirm_x + 6, btn_y + 46, btn_confirm_w - 12, 4, renderer->a(pal.card_bg));
            if (progress > 0)
                renderer->drawRect(btn_confirm_x + 6, btn_y + 46, (btn_confirm_w - 12) * progress / 1000, 4,
                    renderer->a(pal.danger));
        }
    }

    void draw_code_success(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw)
    {
        const auto &pal = current_palette();
        char line[128];
        const tsl::Color accent = result_failed_ ? pal.danger : pal.success;
        draw_card(renderer, cx, cy + 24, cw, 490, pal.panel_bg, accent, 2);
        draw_localized(renderer, result_pending_ ? ptc_ui_text(PTC_UI_T_CONFIRMING_GRANT_RESULT) : (result_failed_ ? ptc_ui_text(PTC_UI_T_GRANT_FAILED) : ptc_ui_text(PTC_UI_T_GRANT_SUCCESSFUL)),
                             false, cx + 14, cy + 68, PTC_OVERLAY_FONT_TITLE_LG, renderer->a(accent));
        std::snprintf(line, sizeof(line), result_pending_ ? ptc_ui_text(PTC_UI_T_ESTIMATED_D_MIN) :
                      (result_failed_ ? ptc_ui_text(PTC_UI_T_ORIGINALLY_PLANNED_D_MIN) : ptc_ui_text(PTC_UI_T_ADDED_D_MIN)),
                      preview_summary_.grant_minutes);
        draw_localized(renderer, line, false, cx + 14, cy + 104, PTC_OVERLAY_FONT_TITLE, renderer->a(pal.text_primary));
        draw_localized(renderer, result_pending_ ? ptc_ui_text(PTC_UI_T_VERIFYING_RESULT_PLEASE_DO_NOT_RE_ENTER) :
                             (result_failed_ ? ptc_ui_text(PTC_UI_T_VERIFICATION_FAILED_CODE_NOT_CONSUMED_CAN_RETRY) :
                              ptc_ui_text(PTC_UI_T_THIS_GRANT_CODE_HAS_ALREADY_BEEN_USED)),
                             false, cx + 14, cy + 138, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_secondary));

        const s32 row_x = cx + 14;
        const s32 row_w = cw - 28;
        draw_card(renderer, row_x, cy + 160, row_w, 64, pal.card_bg, pal.border_decorative, 1);
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_BEFORE_GRANT), false, row_x + 12, cy + 198, PTC_OVERLAY_FONT_BODY_LG, renderer->a(pal.text_secondary));
        if (redemption_before_.converts_unlimited_to_limited) std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else if (redemption_before_.remaining_available) ptc_ui_format_minutes(redemption_before_.remaining_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        draw_localized_right(renderer, line, row_x + row_w - 12, cy + 200, PTC_OVERLAY_FONT_DISPLAY, pal.text_primary);

        draw_card(renderer, row_x, cy + 236, row_w, 64, pal.card_bg, pal.border_decorative, 1);
        draw_localized(renderer, result_pending_ ? ptc_ui_text(PTC_UI_T_PREVIEW_AFTER_GRANT) : ptc_ui_text(PTC_UI_T_AFTER_GRANT), false, row_x + 12, cy + 274, PTC_OVERLAY_FONT_BODY_LG, renderer->a(pal.text_secondary));
        const PtcCompanionResultSummary &after = result_pending_ ? preview_summary_ : displayed_summary_;
        if (result_pending_ ? after.remaining_after_available : after.remaining_available) {
            ptc_ui_format_minutes(result_pending_ ? after.remaining_after_minutes : after.remaining_minutes, line, sizeof(line));
        } else {
            std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        }
        draw_localized_right(renderer, line, row_x + row_w - 12, cy + 276, PTC_OVERLAY_FONT_DISPLAY, accent);

        draw_localized(renderer, result_pending_ ? ptc_ui_text(PTC_UI_T_YOU_MAY_CLOSE_STATUS_WILL_RESUME_NEXT) :
                             (result_failed_ ? ptc_ui_text(PTC_UI_T_FAILURE_RESULT_CONFIRMED) : ptc_ui_text(PTC_UI_T_RESULTS_CONFIRMED_AND_SAVED)),
                             false, cx + 18, cy + 342, PTC_OVERLAY_FONT_BODY, renderer->a(accent));

        const s32 btn_x = cx + 24;
        const s32 btn_w = cw - 48;
        const s32 btn_y = cy + 420;
        const s32 btn_h = 56;
        draw_card(renderer, btn_x, btn_y, btn_w, btn_h, pal.accent_soft, pal.accent, 2);
        draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_A_B_RETURN_TO_BLANK_INPUT_PAGE), btn_x, btn_y, btn_w, btn_h, PTC_OVERLAY_FONT_BTN, pal.text_primary);
    }

    void draw_parent_page(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw)
    {
        const auto &pal = current_palette();
        char line[160];
        draw_card(renderer, cx, cy + 6, cw, 580, pal.panel_bg, pal.accent, 2);
        draw_localized(renderer, parent_view_ == ParentView::Pin ? ptc_ui_text(PTC_UI_T_PARENT_PIN_VERIFICATION) :
            (parent_view_ == ParentView::Actions ? ptc_ui_text(PTC_UI_T_PARENT_ZONE) :
             (parent_view_ == ParentView::Confirm ? ptc_ui_text(parent_action_ == PTC_OVERLAY_PARENT_WAIVE_DOCK ? PTC_UI_T_DOCK_WAIVE : PTC_UI_T_CONFIRM_RESTORING_LIMIT) : ptc_ui_text(PTC_UI_T_OPERATION_RESULT))),
            false, cx + 14, cy + 36, PTC_OVERLAY_FONT_TITLE, renderer->a(pal.text_primary));
        const char *state = (!has_status_snapshot_ || status_is_stale()) ? ptc_ui_text(PTC_UI_T_STATUS_PENDING_REFRESH) :
            (bedtime_restricted() ? ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_3) :
             (displayed_summary_.daily_restriction_active ? ptc_ui_text(PTC_UI_T_DAILY_LIMIT_REACHED_2) :
              (eye_care_restricted() ? ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING) :
               ptc_ui_text(PTC_UI_T_NORMAL_PLAY_AVAILABLE))));
        draw_localized(renderer, state, false, cx + 14, cy + 64, PTC_OVERLAY_FONT_SECONDARY,
            renderer->a(bedtime_restricted() ? pal.danger : pal.text_secondary));

        if (parent_view_ == ParentView::Pin) {
            char masked[PTC_AUTH_PIN_MAX_LEN + 1];
            (void)ptc_overlay_pin_mask(pin_length_, masked, sizeof(masked));
            std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_DIGITS_ENTERED_S),
                static_cast<unsigned int>(pin_length_), masked);
            draw_localized(renderer, line, false, cx + 14, cy + 152, PTC_OVERLAY_FONT_TITLE, renderer->a(pal.text_primary), 330);
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_STICK_8_DIRECTIONS_FOR_1_8_D), false,
                cx + 14, cy + 182, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_secondary));

            draw_card(renderer, cx, cy + PTC_OVERLAY_KEYPAD_Y, cw,
                PTC_OVERLAY_KEYPAD_H, pal.card_bg, pal.border_decorative, 1);
            const char *DIRECTIONS[8] = {
                ptc_ui_text(PTC_UI_T_8_UP_LEFT), ptc_ui_text(PTC_UI_T_1_UP), ptc_ui_text(PTC_UI_T_2_UP_RIGHT), ptc_ui_text(PTC_UI_T_7_LEFT), ptc_ui_text(PTC_UI_T_3_RIGHT),
                ptc_ui_text(PTC_UI_T_6_DOWN_LEFT), ptc_ui_text(PTC_UI_T_5_DOWN), ptc_ui_text(PTC_UI_T_4_DOWN_RIGHT)
            };
            static constexpr int COLS[8] = {0, 1, 2, 0, 2, 0, 1, 2};
            static constexpr int ROWS[8] = {0, 0, 0, 1, 1, 2, 2, 2};
            for (int i = 0; i < 8; ++i) {
                const s32 x = cx + 14 + COLS[i] * 132;
                const s32 y = cy + PTC_OVERLAY_KEYPAD_Y + ROWS[i] * 54;
                draw_card(renderer, x, y, 124, 48, pal.card_raised, pal.border_decorative, 1);
                draw_localized_center(renderer, DIRECTIONS[i], x, y, 124, 48, PTC_OVERLAY_FONT_BODY_LG, pal.text_primary);
            }
            draw_localized_center(renderer, "\uE0E2 0    \uE0E3 9", cx + 14 + 132, cy + PTC_OVERLAY_KEYPAD_Y + 54, 124, 48, PTC_OVERLAY_FONT_BODY_LG, pal.text_primary);
            const PtcOverlayRect backspace = ptc_overlay_backspace_rect(cx, cy);
            draw_card(renderer, backspace.x, backspace.y, backspace.w, backspace.h, pal.card_raised, pal.warning_border, 1);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_ZL_BACKSPACE_2), backspace.x, backspace.y, backspace.w, backspace.h, PTC_OVERLAY_FONT_BODY, pal.warning);

            draw_card(renderer, cx, cy + 410, cw, 52, pal.accent_soft, pal.accent, 2);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_VERIFY_PIN), cx, cy + 410, cw, 52, PTC_OVERLAY_FONT_BTN, pal.text_primary);
            if (pin_message_[0]) draw_localized(renderer, pin_message_, false,
                cx + 14, cy + 482, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.danger), 330);
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_B_BACK_TO_GRANT_CODE), false, cx + 14, cy + 540, PTC_OVERLAY_FONT_SECONDARY,
                renderer->a(pal.text_secondary));
            return;
        }

        if (parent_view_ == ParentView::Actions) {
            const char *LABELS[PTC_OVERLAY_PARENT_ACTION_COUNT] = {
                ptc_ui_text(PTC_UI_T_QUICK_GRANT), ptc_ui_text(PTC_UI_T_NO_LIMIT_TODAY), ptc_ui_text(PTC_UI_T_SKIP_BEDTIME), ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP), ptc_ui_text(PTC_UI_T_RESTORE_THIS_BEDTIME), ptc_ui_text(PTC_UI_T_TURN_OFF_BEDTIME_PLAN),
                ptc_ui_text(PTC_UI_T_RESTORE_PRE_INSTALL_SETTINGS_DEACTIVATE), ptc_ui_text(PTC_UI_T_DOCK_WAIVE)
            };
            const auto refresh_btn = ptc_overlay_parent_refresh_rect(cx, cy);
            draw_card(renderer, refresh_btn.x, refresh_btn.y, refresh_btn.w, refresh_btn.h,
                bridge_->waiting ? pal.card_raised : pal.accent_soft,
                bridge_->waiting ? pal.border_control : pal.accent, 1);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_Y_REFRESH_2),
                refresh_btn.x, refresh_btn.y, refresh_btn.w, refresh_btn.h, PTC_OVERLAY_FONT_BODY,
                bridge_->waiting ? pal.text_secondary : pal.accent);

            const s32 scroll_y = static_cast<s32>(parent_scroll_y_);
            const s32 vp_x = cx;
            const s32 vp_y = cy + PTC_OVERLAY_PARENT_VIEWPORT_Y;
            const s32 vp_w = cw;
            const s32 vp_h = PTC_OVERLAY_PARENT_VIEWPORT_H;

            renderer->enableScissoring(vp_x, vp_y, vp_w, vp_h);

            const PtcUiTextId group_labels[] = {PTC_UI_T_CARD_QUOTA_ADJUST, PTC_UI_T_CARD_RESTRICTION_ACTIONS, PTC_UI_T_CARD_RECOVERY_ACTIONS};
            for (int group = 0; group < 3; ++group) {
                const auto raw_rect = ptc_overlay_parent_group_rect(cx, cy, group);
                const auto rect = ptc_overlay_rect(raw_rect.x, raw_rect.y - scroll_y, raw_rect.w, raw_rect.h);
                draw_card(renderer, rect.x, rect.y, rect.w, rect.h, pal.panel_bg, pal.border_decorative, 1);
                draw_localized(renderer, ptc_ui_text(group_labels[group]), false, rect.x + 14, rect.y + 22, PTC_OVERLAY_FONT_BODY,
                    renderer->a(group == 2 ? pal.danger : pal.accent));
            }
            for (int i = 0; i < PTC_OVERLAY_PARENT_ACTION_COUNT; ++i) {
                const auto raw_rect = ptc_overlay_parent_action_rect(cx, cy, i);
                const auto rect = ptc_overlay_rect(raw_rect.x, raw_rect.y - scroll_y, raw_rect.w, raw_rect.h);
                const s32 y = rect.y;
                const bool selected = i == parent_action_;
                const char *reason = parent_action_reason(
                    static_cast<PtcOverlayParentAction>(i));
                draw_card(renderer, rect.x, y, rect.w, rect.h,
                    selected ? pal.accent_soft : pal.card_bg,
                    selected ? pal.accent : pal.border_decorative,
                    selected ? 2 : 1);
                if (selected) {
                    renderer->drawRect(rect.x + 3, y + 4, 4, rect.h - 8, renderer->a(pal.accent));
                }
                if (i == PTC_OVERLAY_PARENT_ADD_MINUTES) {
                    std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_QUICK_GRANT_D_MIN_LEFT_RIGHT_TO), daily_add_minutes_);
                } else if (i == PTC_OVERLAY_PARENT_SKIP_EYE_CARE && !reason && displayed_summary_.eye_care_rest_remaining_seconds > 0) {
                    const int rest_min = (displayed_summary_.eye_care_rest_remaining_seconds + 59) / 60;
                    std::snprintf(line, sizeof(line), "%s (%d %s)", LABELS[i], rest_min, ptc_ui_text(PTC_UI_T_MIN));
                } else {
                    std::snprintf(line, sizeof(line), "%s", LABELS[i]);
                }
                if (reason) {
                    draw_localized(renderer, line, false, rect.x + 14, y + 20, PTC_OVERLAY_FONT_BODY,
                        renderer->a(pal.text_disabled), rect.w - 28);
                    draw_localized(renderer, reason, false, rect.x + 14, y + 38, PTC_OVERLAY_FONT_CAPTION,
                        renderer->a(pal.warning), rect.w - 28);
                } else {
                    draw_localized(renderer, line, false, rect.x + 14, y + 31, PTC_OVERLAY_FONT_BODY_LG,
                        renderer->a(pal.text_primary), rect.w - 28);
                }
                if (selected && !reason && parent_action_requires_hold(static_cast<PtcOverlayParentAction>(i))) {
                    const int progress = ptc_overlay_hold_progress(&confirm_hold_, 1000);
                    renderer->drawRect(rect.x + 6, y + 45, rect.w - 12, 3, renderer->a(pal.card_bg));
                    if (progress > 0)
                        renderer->drawRect(rect.x + 6, y + 45, (rect.w - 12) * progress / 1000, 3,
                            renderer->a(pal.danger));
                }
            }

            renderer->disableScissoring();

            if (PTC_OVERLAY_PARENT_SCROLL_MAX > 0) {
                const s32 track_x = cx + cw - 8;
                const s32 track_y = vp_y + 12;
                const s32 track_h = vp_h - 24;
                renderer->drawRect(track_x, track_y, 3, track_h, renderer->a(pal.border_decorative));
                const s32 thumb_h = track_h * vp_h / (vp_h + PTC_OVERLAY_PARENT_SCROLL_MAX);
                const s32 thumb_y = track_y + (track_h - thumb_h) * scroll_y / PTC_OVERLAY_PARENT_SCROLL_MAX;
                renderer->drawRect(track_x, thumb_y, 3, thumb_h, renderer->a(pal.accent));
            }

            const auto selected = static_cast<PtcOverlayParentAction>(parent_action_);
            const char *reason = parent_action_reason(selected);
            const bool hold = parent_action_requires_hold(selected);
            const auto back_r = ptc_overlay_parent_back_rect(cx, cy);
            draw_card(renderer, back_r.x, back_r.y, back_r.w, back_r.h, pal.card_bg, pal.border_decorative, 1);
            draw_localized(renderer, reason ? ptc_ui_text(PTC_UI_T_ACTION_CURRENTLY_UNAVAILABLE_B_BACK) :
                (hold ? ptc_ui_text(PTC_UI_T_HOLD_A_1S_Y_REFRESH_B_BACK) : ptc_ui_text(PTC_UI_T_A_EXECUTE_Y_REFRESH_B_BACK)),
                false, back_r.x + 14, back_r.y + 24, PTC_OVERLAY_FONT_SECONDARY,
                renderer->a(reason ? pal.warning : (hold ? pal.danger : pal.accent)));
            return;
        }

        if (parent_view_ == ParentView::Confirm) {
            const bool waive = parent_action_ == PTC_OVERLAY_PARENT_WAIVE_DOCK;
            const bool immediate = !waive && displayed_summary_.bedtime_active &&
                displayed_summary_.bedtime_skipped;
            const char *reason = parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_));
            draw_localized(renderer, ptc_ui_text(waive ? PTC_UI_T_DOCK_WAIVE : PTC_UI_T_RESTORE_BEDTIME_LIMIT), false, cx + 14, cy + 158, PTC_OVERLAY_FONT_TITLE,
                renderer->a(pal.text_primary), 330);
            draw_localized(renderer, waive ? ptc_ui_text(PTC_UI_T_DOCK_WAIVE_BODY) : immediate ?
                ptc_ui_text(PTC_UI_T_CURRENTLY_DURING_BEDTIME_CONFIRMING_WILL_RESTRICT_USE) :
                ptc_ui_text(PTC_UI_T_SKIP_WILL_BE_CLEARED_BEDTIME_RESTRICTIONS_WILL),
                false, cx + 14, cy + 212, PTC_OVERLAY_FONT_BODY,
                renderer->a(immediate ? pal.danger : pal.text_secondary), 330);
            if (reason) draw_localized(renderer, reason, false, cx + 14, cy + 270, PTC_OVERLAY_FONT_SECONDARY,
                renderer->a(pal.warning), 330);

            const s32 btn_c_x = cx + 12;
            const s32 btn_c_w = 155;
            const s32 btn_c_y = cy + 480;
            const s32 btn_c_h = 58;
            draw_card(renderer, btn_c_x, btn_c_y, btn_c_w, btn_c_h, pal.card_bg, pal.border_control, 1);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_B_CANCEL_2), btn_c_x, btn_c_y, btn_c_w, btn_c_h, PTC_OVERLAY_FONT_BTN, pal.text_primary);

            const s32 btn_ok_x = cx + 184;
            const s32 btn_ok_w = cw - 196;
            draw_card(renderer, btn_ok_x, btn_c_y, btn_ok_w, btn_c_h,
                immediate ? pal.danger_soft : pal.accent_soft,
                immediate ? pal.danger : pal.accent, 2);
            draw_localized_center(renderer, immediate ? ptc_ui_text(PTC_UI_T_HOLD_A_1S_TO_RESTORE) : ptc_ui_text(waive ? PTC_UI_T_A_CONFIRM : PTC_UI_T_A_CONFIRM_RESTORE),
                btn_ok_x, btn_c_y, btn_ok_w, btn_c_h, PTC_OVERLAY_FONT_BTN,
                immediate ? pal.danger : pal.text_primary);
            if (immediate) {
                const int progress = ptc_overlay_hold_progress(&confirm_hold_, 1000);
                renderer->drawRect(btn_ok_x + 10, btn_c_y + 53, (btn_ok_w - 20) * progress / 1000, 3,
                    renderer->a(pal.danger));
            }
            return;
        }

        draw_localized(renderer, bridge_->waiting ? ptc_ui_text(PTC_UI_T_PROCESSING_REQUEST) :
            (parent_action_succeeded_ ? ptc_ui_text(PTC_UI_T_OPERATION_COMPLETED) : ptc_ui_text(PTC_UI_T_OPERATION_INCOMPLETE)),
            false, cx + 14, cy + 180, PTC_OVERLAY_FONT_TITLE,
            renderer->a(bridge_->waiting ? pal.warning :
                (parent_action_succeeded_ ? pal.success : pal.danger)));
        if (!bridge_->waiting && parent_action_succeeded_) {
            if (last_request_kind_ == OverlayRequestKind::ClaimDailyBuffer) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_SELF_BUFFER_CLAIMED_ADDED_D_MIN),
                    displayed_summary_.daily_buffer_minutes);
                draw_localized(renderer, line, false, cx + 14, cy + 225, PTC_OVERLAY_FONT_BODY,
                    renderer->a(pal.success), 320);
            } else if (last_request_kind_ == OverlayRequestKind::SkipEyeCare) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_CONFIRM_BODY), false, cx + 14, cy + 225, PTC_OVERLAY_FONT_BODY,
                    renderer->a(pal.success), 320);
            }
            const char *result = !displayed_summary_.access_recovery_required ?
                ptc_ui_text(PTC_UI_T_PCTL_RELOADED_RESTRICTIONS_CLEARED) :
                (bedtime_restricted() ? ptc_ui_text(PTC_UI_T_STILL_RESTRICTED_BY_BEDTIME) :
                 (displayed_summary_.daily_restriction_active ?
                  ptc_ui_text(PTC_UI_T_STILL_RESTRICTED_BY_DAILY_LIMIT) : ptc_ui_text(PTC_UI_T_PLEASE_CHECK_CURRENT_RESTRICTION_STATUS)));
            draw_localized(renderer, result, false, cx + 14, cy + 270, PTC_OVERLAY_FONT_BODY,
                renderer->a(displayed_summary_.access_recovery_required ?
                    pal.warning : pal.success), 320);
        } else if (!bridge_->waiting) {
            draw_localized(renderer, ptc_overlay_bridge_error_message_zh(bridge_), false,
                cx + 14, cy + 225, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.danger), 320);
            if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_ERROR_CODE_D), bridge_->summary.error_code);
                draw_localized(renderer, line, false, cx + 14, cy + 270, PTC_OVERLAY_FONT_SECONDARY,
                    renderer->a(pal.danger));
            }
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_WHEN_BACKGROUND_IS_UNAVAILABLE_PLEASE_RECOVER_VIA), false,
                cx + 14, cy + 324, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.text_secondary), 320);
        }
        if (last_request_kind_ != OverlayRequestKind::ClaimDailyBuffer) {
            draw_card(renderer, cx + 12, cy + 480, 155, 58, pal.card_bg, pal.accent, 1);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_Y_RE_VERIFY), cx + 12, cy + 480, 155, 58, PTC_OVERLAY_FONT_BODY_LG, pal.accent);
        }
        draw_card(renderer, cx + 184, cy + 480, cw - 196, 58, pal.card_bg, pal.accent, 1);
        draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_B_BACK_2), cx + 184, cy + 480, cw - 196, 58, PTC_OVERLAY_FONT_BODY_LG, pal.accent);
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
        const auto &pal = current_palette();
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

        // --- 0. 顶部紧凑栏 (Top Status Bar) ---
        const s32 top_banner_y = cy + PTC_OVERLAY_TOP_BANNER_Y;
        const bool bedtime_res = summary.valid && summary.bedtime_active && !summary.bedtime_skipped;
        const bool eye_res = summary.valid && summary.eye_care_enabled &&
            std::strcmp(summary.eye_care_phase, "resting") == 0;
        const bool dock_res = summary.valid && summary.dock_restriction_active && !bedtime_res && !eye_res;
        const bool is_restricted = has_status_snapshot_ && !status_is_stale() &&
            (dock_res || bedtime_res || eye_res);

        // 左侧品牌标识
        draw_localized(renderer, ptc_ui_text(PTC_UI_T_PLAYWISE), false, cx + 4, top_banner_y + 24, PTC_OVERLAY_FONT_TITLE, renderer->a(pal.accent));

        // 右侧刷新按钮与时间
        const bool busy = bridge_->waiting;
        const auto refresh_rect = ptc_overlay_refresh_rect(cx, cy);
        draw_card(renderer, refresh_rect.x, refresh_rect.y, refresh_rect.w, refresh_rect.h,
                  busy ? pal.card_raised : pal.accent_soft,
                  remaining_refresh_pending ? pal.warning : (busy ? pal.border_control : pal.accent), 1);
        draw_localized_center(renderer, remaining_refresh_pending ? ptc_ui_text(PTC_UI_T_CONFIRMING) : (busy ? ptc_ui_text(PTC_UI_T_REFRESHING) : ptc_ui_text(PTC_UI_T_Y_REFRESH)),
                              refresh_rect.x, refresh_rect.y, refresh_rect.w, refresh_rect.h, PTC_OVERLAY_FONT_SECONDARY,
                              remaining_refresh_pending ? pal.warning : (busy ? pal.text_secondary : pal.accent));

        const bool unlimited_today = summary.valid && summary.unrestricted_today == 1;

        if (is_restricted) {
            // --- 场景感知受限全景卡片 (Dynamic Restriction Hero Card) ---
            const s32 hero_y = cy + 42;
            const s32 hero_h = 420;
            const s32 pad_x = cx + 18;
            const s32 text_w = cw - 36;
            const tsl::Color card_bg = eye_res ? pal.warning_soft : pal.danger_soft;
            const tsl::Color card_border = eye_res ? pal.warning_border : pal.danger_border;

            draw_card(renderer, cx, hero_y, cw, hero_h, card_bg, card_border, 2);

            if (dock_res) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_TITLE), false, pad_x, hero_y + 36, PTC_OVERLAY_FONT_TITLE_LG, renderer->a(pal.danger));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_BLOCKED_BANNER), false, pad_x, hero_y + 72, PTC_OVERLAY_FONT_BODY, renderer->a(pal.text_primary), text_w);
                renderer->drawRect(pad_x, hero_y + 98, text_w, 1, renderer->a(pal.border_decorative));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_CONNECT), false, pad_x, hero_y + 130, PTC_OVERLAY_FONT_BODY, renderer->a(pal.warning), text_w);
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_TV_ACTIVE_NOTE), false, pad_x, hero_y + 172, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_secondary), text_w);
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_DOCK_RULE_EXPLANATION), false, pad_x, hero_y + 210, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_secondary), text_w);

                const s32 guide_y = hero_y + 280;
                const s32 guide_h = 84;
                draw_card(renderer, pad_x, guide_y, text_w, guide_h, pal.card_raised, pal.border_control, 1);
                draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_R_PARENT_AREA), pad_x, guide_y + 22, text_w, 24, PTC_OVERLAY_FONT_BODY_LG, pal.accent);
                draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_DOCK_WAIVE), pad_x, guide_y + 54, text_w, 20, PTC_OVERLAY_FONT_BODY, pal.text_secondary);
            } else if (bedtime_res) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_2), false, pad_x, hero_y + 36, PTC_OVERLAY_FONT_TITLE_LG, renderer->a(pal.danger));
                char bt_window[64];
                std::snprintf(bt_window, sizeof(bt_window), "%02u:%02u ~ %02u:%02u",
                    summary.bedtime_start_minute / 60, summary.bedtime_start_minute % 60,
                    summary.bedtime_end_minute / 60, summary.bedtime_end_minute % 60);
                std::snprintf(line, sizeof(line), "%s (%s)", ptc_ui_text(PTC_UI_T_BEDTIME_LIMIT), bt_window);
                draw_localized(renderer, line, false, pad_x, hero_y + 72, PTC_OVERLAY_FONT_BODY, renderer->a(pal.text_primary), text_w);
                renderer->drawRect(pad_x, hero_y + 98, text_w, 1, renderer->a(pal.border_decorative));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_IT_IS_CURRENTLY_IN_THE_BEDTIME_PERIOD), false, pad_x, hero_y + 130, PTC_OVERLAY_FONT_BODY, renderer->a(pal.warning), text_w);
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_FOR_EXCEPTIONS_SELECT_SKIP_TONIGHT_BEDTIME_IN), false, pad_x, hero_y + 176, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_secondary), text_w);

                const s32 guide_y = hero_y + 280;
                const s32 guide_h = 84;
                draw_card(renderer, pad_x, guide_y, text_w, guide_h, pal.card_raised, pal.border_control, 1);
                draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_R_PARENT_AREA), pad_x, guide_y + 22, text_w, 24, PTC_OVERLAY_FONT_BODY_LG, pal.accent);
                draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_SKIP_BEDTIME), pad_x, guide_y + 54, text_w, 20, PTC_OVERLAY_FONT_BODY, pal.text_secondary);
            } else if (eye_res) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING), false, pad_x, hero_y + 36, PTC_OVERLAY_FONT_TITLE_LG, renderer->a(pal.warning));
                int sec = summary.eye_care_rest_remaining_seconds > 0 ? summary.eye_care_rest_remaining_seconds : 0;
                std::snprintf(line, sizeof(line), "%02d:%02d", sec / 60, sec % 60);
                draw_localized(renderer, line, false, pad_x, hero_y + 84, PTC_OVERLAY_FONT_HERO, renderer->a(pal.warning));
                renderer->drawRect(pad_x, hero_y + 104, text_w, 1, renderer->a(pal.border_decorative));
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_MESSAGE_2), false, pad_x, hero_y + 136, PTC_OVERLAY_FONT_BODY, renderer->a(pal.text_primary), text_w);
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_CONFIRM_BODY), false, pad_x, hero_y + 176, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_secondary), text_w);

                const s32 guide_y = hero_y + 280;
                const s32 guide_h = 84;
                draw_card(renderer, pad_x, guide_y, text_w, guide_h, pal.card_raised, pal.border_control, 1);
                draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_R_PARENT_AREA), pad_x, guide_y + 22, text_w, 24, PTC_OVERLAY_FONT_BODY_LG, pal.accent);
                draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP), pad_x, guide_y + 54, text_w, 20, PTC_OVERLAY_FONT_BODY, pal.text_secondary);
            }
        } else if (is_keypad_mode()) {
            // --- 模式 2：输码键盘模式 (Keypad Mode) ---
            // A. 精简时间条
            const s32 hero_y = cy + 40;
            const s32 hero_h = 44;
            draw_card(renderer, cx, hero_y, cw, hero_h, pal.card_bg, pal.border_decorative, 1);
            if (unlimited_today) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED), false, cx + 14, hero_y + 28, PTC_OVERLAY_FONT_BODY_LG, renderer->a(pal.success));
            } else if (summary.valid && summary.remaining_available) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_D_MIN_LEFT_AFTER_CHANGE), summary.remaining_minutes);
                draw_localized(renderer, line, false, cx + 14, hero_y + 28, PTC_OVERLAY_FONT_BODY_LG, renderer->a(pal.success));
            } else {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY), false, cx + 14, hero_y + 28, PTC_OVERLAY_FONT_BODY, renderer->a(pal.text_secondary));
            }
            draw_localized_right(renderer, age, cx + cw - 14, hero_y + 28, PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary);

            // B. 8位加大加宽插槽
            const s32 slot_y = cy + PTC_OVERLAY_SLOT_Y;
            const s32 slot_w = PTC_OVERLAY_SLOT_W;
            const s32 slot_h = PTC_OVERLAY_SLOT_H;
            const s32 slot_gap = PTC_OVERLAY_SLOT_GAP;
            const s32 slot_group_w = PTC_OVERLAY_CODE_SYMBOLS * slot_w + (PTC_OVERLAY_CODE_SYMBOLS - 1) * slot_gap;
            const s32 slot_start_x = cx + (cw - slot_group_w) / 2;

            for (unsigned int index = 0; index < PTC_OVERLAY_CODE_SYMBOLS; ++index) {
                const s32 sx = slot_start_x + static_cast<s32>(index) * (slot_w + slot_gap);
                const bool is_cursor = (input_->length < PTC_OVERLAY_CODE_SYMBOLS) && (index == input_->length);
                const bool has_symbol = index < input_->length;

                const tsl::Color slot_bg = is_cursor ? pal.accent_soft : (has_symbol ? pal.card_raised : pal.card_bg);
                const tsl::Color slot_border = is_cursor ? pal.accent : (has_symbol ? pal.border_control : pal.border_decorative);
                draw_card(renderer, sx, slot_y, slot_w, slot_h, slot_bg, slot_border, is_cursor ? 2 : 1);

                char symbol[8] = {0};
                if (has_symbol) {
                    symbol[0] = input_->symbols[index];
                    symbol[1] = '\0';
                } else if (is_cursor) {
                    symbol[0] = '_';
                    symbol[1] = '\0';
                } else {
                    std::snprintf(symbol, sizeof(symbol), "|");
                }
                const tsl::Color sym_col = has_symbol ? pal.text_primary : (is_cursor ? pal.accent : pal.text_disabled);
                draw_localized_center(renderer, symbol, sx, slot_y, slot_w, slot_h, PTC_OVERLAY_FONT_DISPLAY, sym_col);
            }

            char console_date[48];
            format_console_date(summary, console_date, sizeof(console_date));
            std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_ENTERED_U_8_DIGITS_SELECTED_C_S), input_->length,
                          ptc_overlay_input_charset()[input_->cursor], console_date);
            draw_localized(renderer, line, false, cx + 14, cy + 160, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_secondary));

            // C. 3x4 舒适大数字键盘
            const char *charset = ptc_overlay_input_charset();
            const s32 panel_y = cy + PTC_OVERLAY_KEYPAD_Y;
            const s32 panel_h = PTC_OVERLAY_KEYPAD_H;
            draw_card(renderer, cx, panel_y, cw, panel_h, pal.card_bg, pal.border_decorative, 1);

            for (unsigned int index = 0; index < PTC_OVERLAY_KEY_COUNT; ++index) {
                char symbol[2] = { charset[index], '\0' };
                const PtcOverlayRect key = ptc_overlay_key_rect(cx, cy, index);
                const bool focused = (index == input_->cursor);

                draw_card(renderer, key.x, key.y, key.w, key.h,
                    focused ? pal.accent_soft : pal.card_raised,
                    focused ? pal.accent : pal.border_decorative,
                    focused ? 2 : 1);
                draw_localized_center(renderer, symbol, key.x, key.y, key.w, key.h, PTC_OVERLAY_FONT_KEYPAD,
                    focused ? pal.accent : pal.text_primary);
            }

            const PtcOverlayRect backspace = ptc_overlay_backspace_rect(cx, cy);
            draw_card(renderer, backspace.x, backspace.y, backspace.w, backspace.h, pal.card_raised, pal.warning_border, 1);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_X_BACKSPACE), backspace.x, backspace.y, backspace.w, backspace.h, PTC_OVERLAY_FONT_BODY, pal.warning);

            const PtcOverlayRect clear = ptc_overlay_clear_rect(cx, cy);
            draw_card(renderer, clear.x, clear.y, clear.w, clear.h, pal.card_raised, pal.danger_border, 1);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_CLICK_TO_CLEAR), clear.x, clear.y, clear.w, clear.h, PTC_OVERLAY_FONT_BODY, pal.danger);

            // D. 提交按钮
            const bool code_unavailable = has_status_snapshot_ && !status_is_stale() &&
                (summary.unrestricted_today == 1 || summary.eye_care_unlimited_capped || summary.dock_unlimited_capped);
            const bool dock_restricted = has_status_snapshot_ && !status_is_stale() && summary.dock_restriction_active;
            const bool can_submit = !bedtime_restricted() && !eye_care_restricted() && !code_unavailable && !dock_restricted &&
                ptc_overlay_request_action_enabled(bridge_->waiting) &&
                ptc_overlay_input_can_submit(input_);
            const auto submit_r = ptc_overlay_submit_rect(cx, cy, cw);

            draw_card(renderer, submit_r.x, submit_r.y, submit_r.w, submit_r.h,
                can_submit ? pal.accent_soft : pal.card_raised,
                can_submit ? pal.accent : pal.border_decorative,
                can_submit ? 2 : 1);

            if (can_submit) {
                draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_SUBMIT_PLAYTIME_GRANT_CLICK_OR_PRESS), submit_r.x, submit_r.y, submit_r.w, submit_r.h, PTC_OVERLAY_FONT_BODY_LG, pal.accent);
            } else if (code_unavailable) {
                draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_NO_TIME_LIMIT_TODAY_GRANT_CODES_ARE), submit_r.x, submit_r.y, submit_r.w, submit_r.h, PTC_OVERLAY_FONT_SECONDARY, pal.warning);
            } else if (bridge_->waiting) {
                draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_BACKGROUND_PROCESSING_YOU_CAN_CONTINUE_TO_EDIT), submit_r.x, submit_r.y, submit_r.w, submit_r.h, PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary);
            } else {
                draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_SUBMIT_GRANT_NEED_TO_ENTER_8_DIGITS), submit_r.x, submit_r.y, submit_r.w, submit_r.h, PTC_OVERLAY_FONT_SECONDARY, pal.text_disabled);
            }

            // E. 底部退出输入与导航按钮
            const auto nav_back = ptc_overlay_keypad_nav_back_rect(cx, cy);
            const auto nav_parent = ptc_overlay_keypad_nav_parent_rect(cx, cy, cw);
            draw_card(renderer, nav_back.x, nav_back.y, nav_back.w, nav_back.h, pal.card_bg, pal.border_control, 1);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_B_CANCEL_2), nav_back.x, nav_back.y, nav_back.w, nav_back.h, PTC_OVERLAY_FONT_BODY, pal.text_primary);

            draw_card(renderer, nav_parent.x, nav_parent.y, nav_parent.w, nav_parent.h, pal.accent_soft, pal.accent, 1);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_R_PARENT_AREA), nav_parent.x, nav_parent.y, nav_parent.w, nav_parent.h, PTC_OVERLAY_FONT_BODY, pal.accent);
        } else {
            // --- 模式 3：概览大字模式 (Overview Mode，日常高频默认态) ---
            // A. Hero 巨幕时间卡片
            const auto hero_r = ptc_overlay_overview_hero_rect(cx, cy, cw);
            draw_card(renderer, hero_r.x, hero_r.y, hero_r.w, hero_r.h, pal.card_bg, pal.border_decorative, 1);
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY), false, hero_r.x + 16, hero_r.y + 26, PTC_OVERLAY_FONT_BODY, renderer->a(pal.text_secondary));

            if (unlimited_today) {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED), false, hero_r.x + 16, hero_r.y + 78, PTC_OVERLAY_FONT_HERO, renderer->a(pal.success));
            } else if (summary.valid && summary.remaining_available) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_D_MIN), summary.remaining_minutes);
                draw_localized(renderer, line, false, hero_r.x + 16, hero_r.y + 78, PTC_OVERLAY_FONT_HERO, renderer->a(pal.success));
            } else {
                draw_localized(renderer, ptc_ui_text(PTC_UI_T_UNAVAILABLE), false, hero_r.x + 16, hero_r.y + 78, PTC_OVERLAY_FONT_DISPLAY, renderer->a(pal.text_disabled));
            }

            char quota_label[32], quota_val[32], quota_note[32];
            ptc_overlay_format_child_quota_parts(&summary, quota_label, sizeof(quota_label),
                quota_val, sizeof(quota_val), quota_note, sizeof(quota_note));
            std::snprintf(line, sizeof(line), "%s %s %s", quota_label, quota_val, quota_note);
            draw_localized(renderer, line, false, hero_r.x + 16, hero_r.y + 118, PTC_OVERLAY_FONT_BODY, renderer->a(pal.text_secondary));

            // B. 规则状态大卡片
            const auto pol0 = ptc_overlay_overview_policy_rect(cx, cy, cw, 0);
            const auto pol1 = ptc_overlay_overview_policy_rect(cx, cy, cw, 1);
            draw_card(renderer, pol0.x, pol0.y, pol0.w, pol0.h, pal.card_bg, pal.border_decorative, 1);
            draw_card(renderer, pol1.x, pol1.y, pol1.w, pol1.h, pal.card_bg, pal.border_decorative, 1);

            const PtcPolicyStatus policies = child_policy_status(summary);
            draw_localized(renderer, ptc_ui_text(PTC_UI_T_POLICY_BED_LABEL), false, pol0.x + 16, pol0.y + 35, PTC_OVERLAY_FONT_BODY_LG, renderer->a(pal.text_primary));
            const tsl::Color col0 = policies.bedtime.state == PTC_POLICY_ACTIVE ? pal.danger :
                (policies.bedtime.state == PTC_POLICY_WAIVED || policies.bedtime.state == PTC_POLICY_SKIPPED ? pal.success : pal.text_secondary);
            draw_localized_right(renderer, policies.bedtime.detail, pol0.x + pol0.w - 16, pol0.y + 35, PTC_OVERLAY_FONT_BODY, col0);

            const char *dock_lbl = ptc_ui_text(policies.mode == PTC_UI_T_STATUS_TO_CONFIRM ? PTC_UI_T_POLICY_DOCK_LABEL : policies.mode);
            draw_localized(renderer, dock_lbl, false, pol1.x + 16, pol1.y + 35, PTC_OVERLAY_FONT_BODY_LG, renderer->a(pal.text_primary));
            const tsl::Color col1 = policies.dock.state == PTC_POLICY_ACTIVE ? pal.danger :
                (policies.dock.state == PTC_POLICY_WAIVED ? pal.success : pal.text_secondary);
            draw_localized_right(renderer, policies.dock.detail, pol1.x + pol1.w - 16, pol1.y + 35, PTC_OVERLAY_FONT_BODY, col1);

            // C. 加时码唤起引导卡片
            const auto redeem_r = ptc_overlay_overview_redeem_rect(cx, cy, cw);
            draw_card(renderer, redeem_r.x, redeem_r.y, redeem_r.w, redeem_r.h, pal.card_raised, pal.accent, 1);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_ENTER_THE_EXTRA_TIME_CODE),
                redeem_r.x, redeem_r.y + 6, redeem_r.w, 32, PTC_OVERLAY_FONT_BTN, pal.accent);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_ENTER_THE_8_DIGIT_CODE_GIVEN_TO),
                redeem_r.x, redeem_r.y + 38, redeem_r.w, 28, PTC_OVERLAY_FONT_BODY, pal.text_secondary);

            // D. 快捷操作大按钮栏
            const auto buf_r = ptc_overlay_child_buffer_rect(cx, cy);
            const auto par_r = ptc_overlay_child_parent_rect(cx, cy, cw);
            const bool buffer_ready = summary.valid && summary.daily_buffer_available &&
                !bedtime_restricted() && !summary.dock_restriction_active && !bridge_->waiting;
            draw_card(renderer, buf_r.x, buf_r.y, buf_r.w, buf_r.h,
                buffer_ready ? pal.accent_soft : pal.card_bg,
                buffer_ready ? pal.accent : pal.border_decorative, 1);
            draw_localized_center(renderer, buffer_ready ? ptc_ui_text(PTC_UI_T_L_RECEIVE_AUTONOMOUS_BUFFER) :
                (summary.daily_buffer_claimed ? ptc_ui_text(PTC_UI_T_BUFFER_RECEIVED_TODAY) : ptc_ui_text(PTC_UI_T_THE_BUFFER_IS_TEMPORARILY_UNAVAILABLE)),
                buf_r.x, buf_r.y, buf_r.w, buf_r.h, PTC_OVERLAY_FONT_BODY, buffer_ready ? pal.text_primary : pal.text_secondary);

            draw_card(renderer, par_r.x, par_r.y, par_r.w, par_r.h, pal.accent_soft, pal.accent, 1);
            draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_R_PARENT_AREA), par_r.x, par_r.y, par_r.w, par_r.h, PTC_OVERLAY_FONT_BODY_LG, pal.accent);
        }

        // --- 5. 状态面板 (Collapsible Status Panel) ---
        if (!is_keypad_mode()) {
            const s32 status_y = cy + PTC_OVERLAY_STATUS_Y;
            const s32 status_w = cw;

            if (!status_expanded_) {
                draw_card(renderer, cx, status_y, status_w, PTC_OVERLAY_STATUS_COLLAPSED_H, pal.card_bg, pal.border_decorative, 1);
                if (bridge_->waiting && active_request_kind_ == OverlayRequestKind::Status) {
                    draw_localized(renderer, ptc_ui_text(PTC_UI_T_REFRESHING_STATUS_PRESS_TO_EXPAND), false, cx + 14, status_y + 25, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.accent));
                } else if (bridge_->waiting) {
                    draw_localized(renderer, ptc_ui_text(PTC_UI_T_PROCESSING_GRANT_PRESS_TO_EXPAND), false, cx + 14, status_y + 25, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.accent));
                } else if (error_) {
                    draw_localized(renderer,
                        (last_request_kind_ == OverlayRequestKind::OfflineCode ||
                         last_request_kind_ == OverlayRequestKind::PreviewOfflineCode)
                            ? ptc_ui_text(PTC_UI_T_REQUEST_FAILED_PRESS_TO_EXPAND_PLEASE_RE)
                            : ptc_ui_text(PTC_UI_T_REQUEST_FAILED_PRESS_TO_EXPAND_Y_TO),
                        false, cx + 14, status_y + 25, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.danger));
                } else if (has_status_snapshot_) {
                    char restriction_summary[128];
                    ptc_overlay_format_child_restriction_summary(&summary, restriction_summary, sizeof(restriction_summary));
                    draw_localized(renderer, restriction_summary, false, cx + 14, status_y + 25, PTC_OVERLAY_FONT_SECONDARY,
                                         renderer->a(pal.text_secondary));
                } else {
                    draw_localized(renderer, ptc_ui_text(PTC_UI_T_COMMANDS_AND_STATUS_CLICK_OR_PRESS), false, cx + 14, status_y + 25, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_secondary));
                }
            } else {
                const s32 expanded_h = status_needs_detail()
                    ? PTC_OVERLAY_STATUS_DETAIL_H : PTC_OVERLAY_STATUS_NORMAL_H;
                draw_card(renderer, cx, status_y, status_w, expanded_h, pal.panel_bg, pal.accent, 2);

                if (error_) {
                    draw_localized(renderer, ptc_ui_text(PTC_UI_T_COMMAND_AND_STATUS_DETAILS_PRESS_TO_CLOSE), false, cx + 14, status_y + 22, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.accent));
                    std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_S_COMMAND_S), bridge_->waiting ? ptc_ui_text(PTC_UI_T_CURRENT) : ptc_ui_text(PTC_UI_T_RECENT), request_label());
                    draw_localized(renderer, line, false, cx + 14, status_y + 44, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_primary));
                    draw_localized(renderer, ptc_overlay_bridge_transport_label(bridge_), false, cx + 14, status_y + 64, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_secondary));
                    const char *message = ptc_overlay_bridge_error_message_zh(bridge_);
                    draw_localized_fitted(renderer, message, cx + 14, status_y + 88, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.danger), cw - 28);
                    const bool code_error = last_request_kind_ == OverlayRequestKind::OfflineCode ||
                        last_request_kind_ == OverlayRequestKind::PreviewOfflineCode;
                    if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                        std::snprintf(line, sizeof(line), code_error ? ptc_ui_text(PTC_UI_T_ERROR_CODE_D_RE_ENTER) :
                                      ptc_ui_text(PTC_UI_T_ERROR_CODE_D_Y_RETRY), bridge_->summary.error_code);
                        draw_localized(renderer, line, false, cx + 14, status_y + 112, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.danger));
                    } else {
                        draw_localized(renderer, code_error ? ptc_ui_text(PTC_UI_T_PLEASE_RE_ENTER_Y_RETURNS_TO_INPUT) : ptc_ui_text(PTC_UI_T_PRESS_Y_TO_TRY_AGAIN),
                                             false, cx + 14, status_y + 112, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.danger));
                    }
                } else if (has_status_snapshot_) {
                    draw_localized(renderer, ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_AND_RESTRICTION_DETAILS_PRESS), false, cx + 14, status_y + 22, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.accent));
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
                    draw_localized(renderer, line, false, cx + 14, status_y + 44, PTC_OVERLAY_FONT_SECONDARY, renderer->a(pal.text_primary));

                    char restriction[96];
                    ptc_overlay_format_child_restriction_detail(&summary, restriction, sizeof(restriction));
                    std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_LIMITED_S), restriction);
                    draw_localized(renderer, line, false, cx + 14, status_y + 66, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.accent), cw - 28);

                    char buffer_buf[64];
                    ptc_overlay_format_child_buffer_status(&summary, buffer_buf, sizeof(buffer_buf));
                    std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_SELF_BUFFER_S_S), buffer_buf, ptc_overlay_bridge_transport_label(bridge_));
                    draw_localized(renderer, line, false, cx + 14, status_y + 88, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.text_secondary));
                } else {
                    draw_localized(renderer, ptc_ui_text(PTC_UI_T_COMMAND_AND_STATUS_DETAILS_PRESS_TO_CLOSE), false, cx + 14, status_y + 22, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.accent));
                    draw_localized(renderer, ptc_ui_text(PTC_UI_T_THE_STATUS_HAS_NOT_BEEN_OBTAINED_YET), false, cx + 14, status_y + 44, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.text_secondary));
                }
            }
        }
    }

