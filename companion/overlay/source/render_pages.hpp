// Included in the production GUI and host preview. All interactive geometry is
// published in page_layout_; input never recreates measured content positions.
    PtcOverlayPageLayout page_layout_{};
    float page_scroll_ = 0, page_scroll_target_ = 0;
    int page_identity_ = -1;
    bool page_changed_ = false;
    int flow_y_ = 0;

    bool child_is_restricted() const
    {
        return has_status_snapshot_ && !status_is_stale() &&
            (bedtime_restricted() || eye_care_restricted() || displayed_summary_.dock_restriction_active);
    }

    int page_identity() const
    {
        if (access_recovery_visible()) return 10 + static_cast<int>(parent_view_);
        if (success_visible_) return 2;
        if (preview_ready_) return 3;
        if (status_expanded_) return 4;
        if (child_is_restricted()) return bedtime_restricted() ? 20 : eye_care_restricted() ? 21 : 22;
        if (is_keypad_mode()) return 5;
        return 0;
    }

    void reset_page_scroll()
    {
        page_identity_ = -1;
        page_scroll_ = page_scroll_target_ = 0;
    }

    void prepare_page()
    {
        const int identity = page_identity();
        page_changed_ = identity != page_identity_;
        if (page_changed_) {
            page_scroll_ = page_scroll_target_ = 0;
            page_identity_ = identity;
        }
        page_layout_ = {};
    }

    void draw_focus_ring(tsl::gfx::Renderer *renderer, PtcOverlayRect rect, tsl::Color color)
    {
        if (rect.w <= 4 || rect.h <= 4) return;
        renderer->drawRect(rect.x + 1, rect.y + 1, rect.w - 2, 2, renderer->a(color));
        renderer->drawRect(rect.x + 1, rect.y + rect.h - 3, rect.w - 2, 2, renderer->a(color));
        renderer->drawRect(rect.x + 1, rect.y + 1, 2, rect.h - 2, renderer->a(color));
        renderer->drawRect(rect.x + rect.w - 3, rect.y + 1, 2, rect.h - 2, renderer->a(color));
    }

    void draw_button(tsl::gfx::Renderer *renderer, PtcOverlayRect rect,
        const char *text, tsl::Color color, bool emphasized = false, int size = PTC_OVERLAY_FONT_BTN, bool focused = false)
    {
        const auto &pal = current_palette();
        const bool danger = color.r == pal.danger.r && color.g == pal.danger.g && color.b == pal.danger.b;
        const bool warning = color.r == pal.warning.r && color.g == pal.warning.g && color.b == pal.warning.b;
        const auto tone = focused ? pal.accent : color;
        const auto soft = danger ? pal.danger_soft : warning ? pal.warning_soft : pal.accent_soft;
        const bool emp = emphasized || focused;
        draw_dashboard_surface(renderer, rect, emp ? soft : pal.card_raised,
            focused ? pal.accent : emp ? tone : pal.border_control, emp);
        if (focused) {
            draw_focus_ring(renderer, rect, pal.accent);
        }
        const int height = overlay_text_height(renderer, text, rect.w - 24, size);
        overlay_text(renderer, text, rect.x + 12, rect.y + (rect.h - height) / 2,
            rect.w - 24, size, focused ? pal.accent : emp ? color : color, true);
    }

    void begin_page(tsl::gfx::Renderer *renderer, int cx, int cy, int cw,
        const char *title, bool refresh = false, int title_size = PTC_OVERLAY_FONT_TITLE)
    {
        const auto &pal = current_palette();
        const int title_width = refresh ? cw - 156 : cw - 32;
        const int header = std::max(56, overlay_text_height(renderer, title, title_width,
            title_size) + 8);
        overlay_text(renderer, title, cx + 16, cy, title_width, title_size, pal.text_primary);
        renderer->drawRect(cx + 4, cy + header + 2, cw - 8, 1, renderer->a(pal.border_decorative));
        renderer->drawRect(cx + 4, cy + header + 2, 32, 2, renderer->a(pal.accent));
        if (refresh) {
            page_layout_.refresh = ptc_overlay_rect(cx + cw - 132, cy + 4, 128, 52);
            const bool highlight = needs_refresh() && !bridge_->waiting;
            const bool restricted = child_is_restricted();
            const bool refresh_focused = (parent_view_ == ParentView::Actions && parent_action_ == -1) ||
                (status_expanded_ && status_details_focus_ == 2) ||
                (!status_expanded_ && !is_keypad_mode() && parent_view_ == ParentView::Child &&
                 child_overview_focus_ == (restricted ? 3 : 4));
            draw_button(renderer, page_layout_.refresh,
                ptc_ui_text(bridge_->waiting ? PTC_UI_T_REFRESHING : PTC_UI_T_Y_REFRESH),
                bridge_->waiting ? pal.text_secondary : highlight ? pal.warning : pal.accent,
                highlight || bridge_->waiting, PTC_OVERLAY_FONT_SECONDARY, refresh_focused);
        }
        page_layout_.viewport = ptc_overlay_rect(cx, cy + header + 8, cw, 516 - header - 8);
        page_layout_.secondary = ptc_overlay_rect(cx, cy + 526, 154, 76);
        page_layout_.primary = ptc_overlay_rect(cx + 166, cy + 526, cw - 166, 76);
        flow_y_ = page_layout_.viewport.y;
        renderer->enableScissoring(cx, page_layout_.viewport.y, cw, page_layout_.viewport.h);
    }

    int flow_text(tsl::gfx::Renderer *renderer, const char *text, int size,
        tsl::Color color, int gap = 12)
    {
        const auto &vp = page_layout_.viewport;
        const int height = overlay_text(renderer, text, vp.x + 16,
            flow_y_ - static_cast<int>(page_scroll_), vp.w - 36, size, color);
        flow_y_ += height + ((parent_view_ == ParentView::Child || parent_view_ == ParentView::Pin) ? std::min(gap, 6) : gap);
        return height;
    }

    PtcOverlayRect flow_card(tsl::gfx::Renderer *renderer, const char *label,
        const char *value, const char *note, int value_size, tsl::Color color,
        tsl::Color tone, PtcOverlayParentAction action, bool highlighted = false, bool disabled = false)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int width = vp.w - 40;
        const int label_h = overlay_text_height(renderer, label, width, PTC_OVERLAY_FONT_BODY_LG);
        const int value_h = value && *value ? overlay_text_height(renderer, value, width, value_size) : 0;
        const int note_h = note && *note ? overlay_text_height(renderer, note, width, PTC_OVERLAY_FONT_SECONDARY) : 0;
        const int height = std::max(104, 28 + label_h + (value_h ? value_h + 8 : 0) + (note_h ? note_h + 8 : 0));
        const auto raw = ptc_overlay_rect(vp.x + 4, flow_y_, vp.w - 12, height);
        const int y = raw.y - static_cast<int>(page_scroll_);
        flow_y_ += height + 12;
        // Still measure hidden cards for scrolling, but avoid rasterizing their
        // glyphs into the bounded Tesla cache when the entire card is clipped.
        if (y + height <= vp.y || y >= vp.y + vp.h) return raw;
        draw_dashboard_surface(renderer, ptc_overlay_rect(raw.x, y, raw.w, raw.h),
            highlighted ? dashboard_soft(tone) : tsl::Color{1, 2, 3, 15},
            highlighted ? tone : pal.border_decorative, highlighted);
        renderer->drawRect(raw.x, y + 10, 3, height - 20, renderer->a(tone));
        // Optional icons do not change measured card height or wrap a title.
        const int icon_space = static_cast<int>(measure_localized(renderer, label,
            PTC_OVERLAY_FONT_BODY_LG).first) + 30 <= width ? 30 : 0;
        int top = y + 12;
        if (icon_space) draw_parent_action_icon(renderer, action, raw.x + 14, top + 6,
            disabled ? pal.text_disabled : tone);
        overlay_text(renderer, label, raw.x + 14 + icon_space, top, width - icon_space, PTC_OVERLAY_FONT_BODY_LG,
            disabled ? pal.text_disabled : pal.text_primary);
        top += label_h + 8;
        if (value_h) {
            overlay_text(renderer, value, raw.x + 14, top, width, value_size, color);
            top += value_h + 8;
        }
        if (note_h) overlay_text(renderer, note, raw.x + 14, top, width, PTC_OVERLAY_FONT_SECONDARY,
            disabled ? pal.warning : pal.text_secondary);
        return raw;
    }

    // Child pages use measured rows instead of the parent's 104px minimum.
    // Stack only when the two measured texts cannot share a line safely.
    PtcOverlayRect child_card(tsl::gfx::Renderer *renderer, const char *label,
        const char *value, const char *note, int value_size, tsl::Color color,
        int label_size = PTC_OVERLAY_FONT_BODY_LG, ChildCardKind kind = ChildCardKind::Plain, bool focused = false)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int width = vp.w - 36;
        const int label_w = static_cast<int>(measure_localized(renderer, label, label_size).first);
        const int value_w = static_cast<int>(measure_localized(renderer, value, value_size).first);
        const bool inline_value = label_w + value_w + 12 <= width;
        const bool rule = kind == ChildCardKind::Eye || kind == ChildCardKind::Bedtime || kind == ChildCardKind::Dock;
        // Decorative icons must never force an extra line or reduce note width.
        const int icon_space = rule && inline_value && label_w + value_w + 12 + 30 <= width ? 30 : 0;
        const int label_h = overlay_text_height(renderer, label, width, label_size);
        const int value_h = overlay_text_height(renderer, value, width, value_size);
        const int note_h = note && *note ? overlay_text_height(renderer, note, width, PTC_OVERLAY_FONT_SECONDARY) : 0;
        const int content_h = inline_value ? std::max(label_h, value_h) : label_h + value_h + 2;
        const int height = 16 + content_h + (note_h ? note_h + 2 : 0);
        const auto raw = ptc_overlay_rect(vp.x + 4, flow_y_, vp.w - 8, height);
        const int y = raw.y - static_cast<int>(page_scroll_);
        flow_y_ += height + 6;
        if (y + height <= vp.y || y >= vp.y + vp.h) return raw;
        const auto surface = focused ? pal.accent_soft : kind == ChildCardKind::Time ? tsl::Color{1, 2, 3, 15} : pal.card_bg;
        draw_dashboard_surface(renderer, ptc_overlay_rect(raw.x, y, raw.w, raw.h), surface,
            focused ? pal.accent : pal.border_decorative, focused);
        if (focused) {
            draw_focus_ring(renderer, ptc_overlay_rect(raw.x, y, raw.w, raw.h), pal.accent);
        }
        if (kind == ChildCardKind::Time) {
            // A bounded pattern in the bottom padding leaves all text unobscured.
            for (int i = 0; i < 7; ++i)
                renderer->drawRect(raw.x + raw.w - 66 + i * 8, y + height - 5, 3, 2, renderer->a(pal.border_decorative));
        }
        renderer->drawRect(raw.x, y + 8, 3, height - 16,
            renderer->a(rule ? color : kind == ChildCardKind::Time ? pal.accent : pal.border_decorative));
        if (icon_space) draw_child_icon(renderer, kind, raw.x + 14, y + 8 + (content_h - 22) / 2, pal.accent);
        if (inline_value) {
            const int top = y + 8;
            overlay_text(renderer, label, raw.x + 14 + icon_space, top + (content_h - label_h) / 2,
                label_w + 1, label_size, pal.text_primary);
            overlay_text(renderer, value, raw.x + raw.w - 14 - value_w,
                top + (content_h - value_h) / 2, value_w + 1, value_size, color);
        } else {
            overlay_text(renderer, label, raw.x + 14, y + 8, width, label_size, pal.text_primary);
            overlay_text(renderer, value, raw.x + 14, y + 10 + label_h, width, value_size, color);
        }
        if (note_h) overlay_text(renderer, note, raw.x + 14, y + 10 + content_h,
            width, PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary);
        return raw;
    }

    PtcOverlayRect child_rule(tsl::gfx::Renderer *renderer, const char *label,
        const char *value, const char *note, tsl::Color color, ChildCardKind kind)
    {
        return child_card(renderer, label, value, note, PTC_OVERLAY_FONT_BODY,
            color, PTC_OVERLAY_FONT_BODY_LG, kind, false);
    }

    void end_page(tsl::gfx::Renderer *renderer)
    {
        renderer->disableScissoring();
        const auto &vp = page_layout_.viewport;
        page_layout_.scroll_max = ptc_overlay_scroll_max(flow_y_ - vp.y, vp.h);
        page_scroll_target_ = std::clamp(page_scroll_target_, 0.0f, static_cast<float>(page_layout_.scroll_max));
        page_scroll_ = std::clamp(page_scroll_, 0.0f, static_cast<float>(page_layout_.scroll_max));
        if (page_layout_.scroll_max > 0) {
            const auto &pal = current_palette();
            const int h = std::max(24, vp.h * vp.h / (vp.h + page_layout_.scroll_max));
            const int y = vp.y + (vp.h - h) * static_cast<int>(page_scroll_) / page_layout_.scroll_max;
            renderer->drawRect(vp.x + vp.w - 3, vp.y, 3, vp.h, renderer->a(pal.border_decorative));
            renderer->drawRect(vp.x + vp.w - 3, y, 3, h, renderer->a(pal.accent));
        }
    }

    void draw_hold(tsl::gfx::Renderer *renderer, PtcOverlayRect rect, tsl::Color color)
    {
        const auto &pal = current_palette();
        const int progress = confirm_hold_.fired ? 0 : ptc_overlay_hold_progress(&confirm_hold_, 1000);
        renderer->drawRect(rect.x + 8, rect.y + rect.h - 7, rect.w - 16, 3, renderer->a(pal.card_bg));
        if (progress) renderer->drawRect(rect.x + 8, rect.y + rect.h - 7,
            (rect.w - 16) * progress / 1000, 3, renderer->a(color));
    }

    void draw_code_preview(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        char line[256];
        begin_page(renderer, cx, cy, cw, ptc_ui_text(preview_changed_ ?
            PTC_UI_T_STATUS_CHANGED_PLEASE_CONFIRM_AGAIN : PTC_UI_T_CONFIRM_PLAYTIME_GRANT));
        ptc_ui_format_actual_added(preview_summary_.grant_minutes, line, sizeof(line));
        dashboard_panel(renderer, {{line, PTC_OVERLAY_FONT_DISPLAY, pal.accent},
            {ptc_ui_text(PTC_UI_T_OVERLAY_CODE_ONCE), PTC_OVERLAY_FONT_BODY_LG, pal.text_secondary}}, pal.accent);
        if (preview_summary_.converts_unlimited_to_limited) std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else if (preview_summary_.remaining_available) ptc_ui_format_minutes(preview_summary_.remaining_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        child_card(renderer, ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY), line, nullptr,
            PTC_OVERLAY_FONT_DISPLAY, pal.success, PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
        if (preview_summary_.remaining_after_available) ptc_ui_format_minutes(preview_summary_.remaining_after_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        const auto level = ptc_overlay_preview_visual_level(preview_summary_.remaining_after_available,
            preview_summary_.remaining_after_minutes, preview_summary_.preview_capped, preview_summary_.converts_unlimited_to_limited);
        const bool dangerous = level == PTC_OVERLAY_PREVIEW_DANGER;
        child_card(renderer, ptc_ui_text(PTC_UI_T_ESTIMATED_TIME_AFTER_GRANT), line, nullptr,
            PTC_OVERLAY_FONT_DISPLAY, dangerous ? pal.danger : pal.success, PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
        const char *warning = ptc_ui_text(PTC_UI_T_OVERLAY_CODE_NOT_USED);
        if (preview_summary_.converts_unlimited_to_limited) warning = ptc_ui_text(PTC_UI_T_WARNING_WILL_CHANGE_FROM_UNLIMITED_TO_LIMITED);
        else if (!preview_summary_.remaining_after_available) warning = ptc_ui_text(PTC_UI_T_WARNING_CANNOT_CONFIRM_PLAYTIME_AFTER_GRANT_RIGHT);
        else if (!preview_summary_.remaining_after_minutes) warning = ptc_ui_text(PTC_UI_T_WARNING_ESTIMATED_NO_PLAYTIME_REMAINING_AFTER_GRANT);
        else if (preview_summary_.preview_capped) {
            ptc_ui_format_daily_cap_applied(preview_summary_.effective_add_minutes, line, sizeof(line));
            warning = line;
        }
        flow_text(renderer, warning, PTC_OVERLAY_FONT_BODY_LG,
            dangerous ? pal.danger : level == PTC_OVERLAY_PREVIEW_WARNING ? pal.warning : pal.text_secondary);
        end_page(renderer);
        draw_button(renderer, page_layout_.secondary, ptc_ui_text(PTC_UI_T_B_CANCEL_2), pal.text_primary, false, PTC_OVERLAY_FONT_BTN, preview_focus_ == 0);
        draw_button(renderer, page_layout_.primary, ptc_ui_text(dangerous ? PTC_UI_T_HOLD_A_TO_CONFIRM : PTC_UI_T_A_CONFIRM_2),
            dangerous ? pal.danger : pal.accent, true, PTC_OVERLAY_FONT_BTN, preview_focus_ == 1);
        if (dangerous) draw_hold(renderer, page_layout_.primary, pal.danger);
    }

    void draw_code_success(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        const auto accent = result_failed_ ? pal.danger : result_pending_ ? pal.warning : pal.success;
        char line[256];
        begin_page(renderer, cx, cy, cw, ptc_ui_text(result_pending_ ? PTC_UI_T_CONFIRMING_GRANT_RESULT :
            result_failed_ ? PTC_UI_T_GRANT_FAILED : PTC_UI_T_GRANT_SUCCESSFUL), false, PTC_OVERLAY_FONT_TITLE_LG);
        std::snprintf(line, sizeof(line), ptc_ui_text(result_pending_ ? PTC_UI_T_ESTIMATED_D_MIN :
            result_failed_ ? PTC_UI_T_ORIGINALLY_PLANNED_D_MIN : PTC_UI_T_ADDED_D_MIN), preview_summary_.grant_minutes);
        dashboard_panel(renderer, {{line, PTC_OVERLAY_FONT_DISPLAY, accent},
            {ptc_ui_text(result_pending_ ? PTC_UI_T_VERIFYING_RESULT_PLEASE_DO_NOT_RE_ENTER :
            result_failed_ ? PTC_UI_T_VERIFICATION_FAILED_CODE_NOT_CONSUMED_CAN_RETRY : PTC_UI_T_THIS_GRANT_CODE_HAS_ALREADY_BEEN_USED),
            PTC_OVERLAY_FONT_BODY_LG, pal.text_secondary}}, accent);
        if (redemption_before_.converts_unlimited_to_limited) std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else if (redemption_before_.remaining_available) ptc_ui_format_minutes(redemption_before_.remaining_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        child_card(renderer, ptc_ui_text(PTC_UI_T_BEFORE_GRANT), line, nullptr,
            PTC_OVERLAY_FONT_DISPLAY, pal.text_primary, PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
        const auto &after = result_pending_ ? preview_summary_ : displayed_summary_;
        if (result_pending_ ? after.remaining_after_available : after.remaining_available)
            ptc_ui_format_minutes(result_pending_ ? after.remaining_after_minutes : after.remaining_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        child_card(renderer, ptc_ui_text(result_pending_ ? PTC_UI_T_PREVIEW_AFTER_GRANT : PTC_UI_T_AFTER_GRANT),
            line, nullptr, PTC_OVERLAY_FONT_DISPLAY, accent, PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
        if (result_failed_) flow_text(renderer, ptc_overlay_bridge_error_message_zh(bridge_), PTC_OVERLAY_FONT_BODY_LG, pal.danger);
        flow_text(renderer, ptc_ui_text(result_pending_ ? PTC_UI_T_YOU_MAY_CLOSE_STATUS_WILL_RESUME_NEXT :
            result_failed_ ? PTC_UI_T_FAILURE_RESULT_CONFIRMED : PTC_UI_T_RESULTS_CONFIRMED_AND_SAVED), PTC_OVERLAY_FONT_BODY, accent);
        end_page(renderer);
        page_layout_.primary = ptc_overlay_rect(cx, cy + 526, cw, 76);
        page_layout_.secondary = {};
        draw_button(renderer, page_layout_.primary, ptc_ui_text(PTC_UI_T_A_B_RETURN_TO_BLANK_INPUT_PAGE), pal.accent, true, PTC_OVERLAY_FONT_BTN, true);
    }

    void draw_parent_page(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        char line[384];
        const char *title = ptc_ui_text(parent_view_ == ParentView::Pin ? PTC_UI_T_PARENT_PIN_VERIFICATION :
            parent_view_ == ParentView::Actions ? PTC_UI_T_PARENT_ZONE :
            parent_view_ == ParentView::Confirm ? PTC_UI_T_CONFIRM_RESTORING_LIMIT : PTC_UI_T_OPERATION_RESULT);
        if (parent_view_ == ParentView::Confirm && parent_action_ == PTC_OVERLAY_PARENT_WAIVE_DOCK) title = ptc_ui_text(PTC_UI_T_DOCK_WAIVE);
        begin_page(renderer, cx, cy, cw, title, parent_view_ == ParentView::Actions);
        const bool stale = !has_status_snapshot_ || status_is_stale();
        const char *state = ptc_ui_text(stale ? PTC_UI_T_STATUS_PENDING_REFRESH :
            bedtime_restricted() ? PTC_UI_T_BEDTIME_ACTIVE_3 : displayed_summary_.daily_restriction_active ? PTC_UI_T_DAILY_LIMIT_REACHED_2 :
            eye_care_restricted() ? PTC_UI_T_EYE_CARE_RESTING : displayed_summary_.dock_restriction_active ?
            PTC_UI_T_DOCK_TITLE : PTC_UI_T_NORMAL_PLAY_AVAILABLE);
        const auto state_color = stale ? pal.warning :
            bedtime_restricted() || displayed_summary_.daily_restriction_active ? pal.danger :
            eye_care_restricted() ? pal.warning : displayed_summary_.dock_restriction_active ? pal.danger : pal.success;
        dashboard_panel(renderer, {{state, PTC_OVERLAY_FONT_SECONDARY, state_color}}, state_color);
        if (parent_view_ == ParentView::Pin) {
            std::memset(page_layout_.pin_keys, 0, sizeof(page_layout_.pin_keys));
            char masked[PTC_AUTH_PIN_MAX_LEN + 1];
            (void)ptc_overlay_pin_mask(pin_length_, masked, sizeof(masked));
            std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_DIGITS_ENTERED_S), static_cast<unsigned>(pin_length_), masked);
            dashboard_panel(renderer, {{line, PTC_OVERLAY_FONT_BODY_LG, pal.text_primary}}, pal.accent);
            if (pin_message_[0]) flow_text(renderer, pin_message_, PTC_OVERLAY_FONT_BODY, pal.danger);

            if (!parent_pin_standard_keypad_) {
                flow_text(renderer, ptc_ui_text(PTC_UI_T_STICK_8_DIRECTIONS_FOR_1_8_D), PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary, 6);
                const PtcUiTextId directions[] = {PTC_UI_T_8_UP_LEFT, PTC_UI_T_1_UP, PTC_UI_T_2_UP_RIGHT,
                    PTC_UI_T_7_LEFT, PTC_UI_T_3_RIGHT, PTC_UI_T_6_DOWN_LEFT, PTC_UI_T_5_DOWN, PTC_UI_T_4_DOWN_RIGHT};
                for (unsigned i = 0; i < 8; ++i) {
                    const auto r = ptc_overlay_pin_wheel_rect(cx, flow_y_, i);
                    page_layout_.pin_keys[i] = r;
                    auto vr = r;
                    vr.y -= static_cast<int>(page_scroll_);
                    draw_button(renderer, vr, ptc_ui_text(directions[i]), pal.text_primary, false, PTC_OVERLAY_FONT_SECONDARY);
                }
                const auto r0 = ptc_overlay_pin_center_0_rect(cx, flow_y_);
                page_layout_.pin_keys[8] = r0;
                auto vr0 = r0;
                vr0.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, vr0, "\uE0E2 0", pal.text_primary, false, PTC_OVERLAY_FONT_SECONDARY);

                const auto r9 = ptc_overlay_pin_center_9_rect(cx, flow_y_);
                page_layout_.pin_keys[9] = r9;
                auto vr9 = r9;
                vr9.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, vr9, "\uE0E3 9", pal.text_primary, false, PTC_OVERLAY_FONT_SECONDARY);

                flow_y_ += 220;
            } else {
                flow_text(renderer, ptc_ui_text(PTC_UI_T_TOUCH_NUMERIC_KEYPAD), PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary, 6);
                for (int d = 0; d < 10; ++d) {
                    const auto r = ptc_overlay_pin_numpad_key_rect(cx, flow_y_, d);
                    page_layout_.pin_keys[d] = r;
                    auto vr = r;
                    vr.y -= static_cast<int>(page_scroll_);
                    char digit_str[4];
                    std::snprintf(digit_str, sizeof(digit_str), "%d", d);
                    const bool digit_focused = parent_pin_standard_keypad_ && parent_pin_focus_ == (d == 0 ? 11 : d);
                    draw_button(renderer, vr, digit_str, pal.text_primary, false, PTC_OVERLAY_FONT_KEYPAD, digit_focused);
                }
                const auto r_bs = ptc_overlay_rect(cx + 14, flow_y_ + 3 * 66, 124, 58);
                page_layout_.pin_keys[12] = r_bs;
                auto vr_bs = r_bs;
                vr_bs.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, vr_bs, ptc_ui_text(PTC_UI_T_X_BACKSPACE), pal.warning, false, PTC_OVERLAY_FONT_SECONDARY,
                    parent_pin_standard_keypad_ && parent_pin_focus_ == 10);

                const auto r_clr = ptc_overlay_pin_numpad_clear_rect(cx, flow_y_);
                page_layout_.pin_keys[11] = r_clr;
                auto vr_clr = r_clr;
                vr_clr.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, vr_clr, ptc_ui_text(PTC_UI_T_OVERLAY_CLEAR_PIN), pal.danger, false, PTC_OVERLAY_FONT_SECONDARY,
                    parent_pin_standard_keypad_ && parent_pin_focus_ == 12);

                flow_y_ += 260;
            }

            // Bottom action row: Toggle button on the left, Backspace button on the right
            const int btn_h = 48;
            const int toggle_w = 214;
            const int bs_w = cw - 12 - toggle_w - 10;
            const auto toggle_rect = ptc_overlay_rect(cx + 4, flow_y_, toggle_w, btn_h);
            page_layout_.pin_keys[10] = toggle_rect;
            auto visible_toggle = toggle_rect;
            visible_toggle.y -= static_cast<int>(page_scroll_);
            draw_button(renderer, visible_toggle,
                ptc_ui_text(parent_pin_standard_keypad_ ? PTC_UI_T_SWITCH_TO_DIAL_PAD : PTC_UI_T_SWITCH_TO_NUMPAD),
                pal.accent, parent_pin_standard_keypad_, PTC_OVERLAY_FONT_SECONDARY,
                parent_pin_standard_keypad_ && parent_pin_focus_ == 0);

            page_layout_.backspace = ptc_overlay_rect(cx + 4 + toggle_w + 10, flow_y_, bs_w, btn_h);
            auto visible_bs = page_layout_.backspace;
            visible_bs.y -= static_cast<int>(page_scroll_);
            draw_button(renderer, visible_bs, ptc_ui_text(PTC_UI_T_ZL_BACKSPACE_2), pal.warning, false, PTC_OVERLAY_FONT_SECONDARY,
                parent_pin_standard_keypad_ && parent_pin_focus_ == 15);
            flow_y_ += btn_h + 4;

            end_page(renderer);
            draw_button(renderer, page_layout_.secondary, ptc_ui_text(PTC_UI_T_B_BACK_2), pal.text_primary, false, PTC_OVERLAY_FONT_BTN,
                parent_pin_standard_keypad_ && parent_pin_focus_ == 13);
            draw_button(renderer, page_layout_.primary, ptc_ui_text(PTC_UI_T_VERIFY_PIN), pal.accent, true, PTC_OVERLAY_FONT_BTN,
                parent_pin_standard_keypad_ && parent_pin_focus_ == 14);
            return;
        }
        if (parent_view_ == ParentView::Actions) {
            const PtcUiTextId labels[] = {PTC_UI_T_QUICK_GRANT, PTC_UI_T_NO_LIMIT_TODAY, PTC_UI_T_SKIP_BEDTIME,
                PTC_UI_T_EYE_CARE_SKIP, PTC_UI_T_RESTORE_THIS_BEDTIME, PTC_UI_T_TURN_OFF_BEDTIME_PLAN,
                PTC_UI_T_RESTORE_PRE_INSTALL_SETTINGS_DEACTIVATE, PTC_UI_T_DOCK_WAIVE};
            const PtcUiTextId groups[] = {PTC_UI_T_CARD_QUOTA_ADJUST, PTC_UI_T_CARD_RESTRICTION_ACTIONS, PTC_UI_T_CARD_RECOVERY_ACTIONS};
            for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot) {
                int index = 0;
                while (ptc_overlay_parent_action_slot(index) != slot) ++index;
                if (slot == 0 || slot == 2 || slot == 7) {
                    const auto group_color = slot == 7 ? pal.danger : pal.accent;
                    if (slot != 0) {
                        renderer->drawRect(page_layout_.viewport.x + 4,
                            flow_y_ - static_cast<int>(page_scroll_), page_layout_.viewport.w - 12, 1,
                            renderer->a(pal.border_decorative));
                        flow_y_ += 10;
                    }
                    renderer->drawRect(page_layout_.viewport.x + 4,
                        flow_y_ - static_cast<int>(page_scroll_) + 4, 4,
                        PTC_OVERLAY_FONT_TITLE - 10, renderer->a(group_color));
                    flow_text(renderer, ptc_ui_text(groups[slot == 0 ? 0 : slot == 2 ? 1 : 2]),
                        PTC_OVERLAY_FONT_TITLE, group_color, 12);
                }
                const auto action = static_cast<PtcOverlayParentAction>(index);
                const char *reason = parent_action_reason(action);
                const char *value = nullptr;
                const char *note = reason;
                char action_note[512];
                if (index == PTC_OVERLAY_PARENT_ADD_MINUTES) {
                    ptc_ui_format_minutes(daily_add_minutes_, line, sizeof(line));
                    value = line;
                    std::snprintf(action_note, sizeof(action_note), "%s%s%s",
                        ptc_ui_text(PTC_UI_T_OVERLAY_ADJUST_MINUTES), reason ? "\n" : "", reason ? reason : "");
                    note = action_note;
                } else if (index == PTC_OVERLAY_PARENT_SKIP_EYE_CARE && !reason && displayed_summary_.eye_care_rest_remaining_seconds > 0) {
                    ptc_ui_format_minutes((displayed_summary_.eye_care_rest_remaining_seconds + 59) / 60, line, sizeof(line));
                    value = line;
                }
                const int start = flow_y_;
                const auto tone = reason ? pal.text_disabled : parent_action_requires_hold(action) ? pal.danger : pal.accent;
                page_layout_.actions[index] = flow_card(renderer, ptc_ui_text(labels[index]), value, note,
                    PTC_OVERLAY_FONT_BODY_LG, reason ? pal.text_disabled : pal.text_primary,
                    tone, action, parent_action_ == index, reason != nullptr);
                if (parent_action_ == index && !reason && parent_action_requires_hold(action)) {
                    auto rect = page_layout_.actions[index];
                    rect.y = start - static_cast<int>(page_scroll_);
                    draw_hold(renderer, rect, pal.danger);
                }
            }
            end_page(renderer);
            if (page_changed_) page_scroll_target_ = static_cast<float>(ptc_overlay_scroll_to_rect(
                page_layout_.viewport, page_layout_.actions[parent_action_], 0, page_layout_.scroll_max));
            page_layout_.primary = ptc_overlay_rect(cx, cy + 526, cw, 76);
            page_layout_.secondary = {};
            const auto selected = static_cast<PtcOverlayParentAction>(parent_action_);
            const char *footer = ptc_ui_text(parent_action_reason(selected) ? PTC_UI_T_ACTION_CURRENTLY_UNAVAILABLE_B_BACK :
                parent_action_requires_hold(selected) ? PTC_UI_T_HOLD_A_1S_Y_REFRESH_B_BACK : PTC_UI_T_A_EXECUTE_Y_REFRESH_B_BACK);
            draw_button(renderer, page_layout_.primary, footer, parent_action_reason(selected) ? pal.warning : pal.accent,
                false, PTC_OVERLAY_FONT_SECONDARY);
            return;
        }
        if (parent_view_ == ParentView::Confirm) {
            const bool waive = parent_action_ == PTC_OVERLAY_PARENT_WAIVE_DOCK;
            const bool immediate = !waive && displayed_summary_.bedtime_active && displayed_summary_.bedtime_skipped;
            dashboard_panel(renderer, {{ptc_ui_text(waive ? PTC_UI_T_DOCK_WAIVE_BODY : immediate ?
                PTC_UI_T_CURRENTLY_DURING_BEDTIME_CONFIRMING_WILL_RESTRICT_USE : PTC_UI_T_SKIP_WILL_BE_CLEARED_BEDTIME_RESTRICTIONS_WILL),
                PTC_OVERLAY_FONT_BODY_LG, immediate ? pal.danger : pal.text_primary}}, immediate ? pal.danger : pal.accent);
            const char *reason = parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_));
            if (reason) flow_text(renderer, reason, PTC_OVERLAY_FONT_BODY, pal.warning);
            end_page(renderer);
            draw_button(renderer, page_layout_.secondary, ptc_ui_text(PTC_UI_T_B_CANCEL_2), pal.text_primary, false, PTC_OVERLAY_FONT_BTN, confirm_focus_ == 0);
            draw_button(renderer, page_layout_.primary, ptc_ui_text(immediate ? PTC_UI_T_HOLD_A_1S_TO_RESTORE :
                waive ? PTC_UI_T_A_CONFIRM : PTC_UI_T_A_CONFIRM_RESTORE), reason ? pal.text_disabled : immediate ? pal.danger : pal.accent, true, PTC_OVERLAY_FONT_BTN, confirm_focus_ == 1);
            if (immediate) draw_hold(renderer, page_layout_.primary, pal.danger);
            return;
        }
        const auto result_color = bridge_->waiting ? pal.warning : parent_action_succeeded_ ? pal.success : pal.danger;
        dashboard_panel(renderer, {{ptc_ui_text(bridge_->waiting ? PTC_UI_T_PROCESSING_REQUEST : parent_action_succeeded_ ?
            PTC_UI_T_OPERATION_COMPLETED : PTC_UI_T_OPERATION_INCOMPLETE), PTC_OVERLAY_FONT_TITLE_LG,
            result_color}}, result_color);
        if (!bridge_->waiting && parent_action_succeeded_) {
            if (last_request_kind_ == OverlayRequestKind::ClaimDailyBuffer) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_SELF_BUFFER_CLAIMED_ADDED_D_MIN), displayed_summary_.daily_buffer_minutes);
                flow_text(renderer, line, PTC_OVERLAY_FONT_BODY_LG, pal.success);
            } else if (last_request_kind_ == OverlayRequestKind::SkipEyeCare)
                flow_text(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_CONFIRM_BODY), PTC_OVERLAY_FONT_BODY_LG, pal.success);
            flow_text(renderer, ptc_ui_text(!displayed_summary_.access_recovery_required ? PTC_UI_T_PCTL_RELOADED_RESTRICTIONS_CLEARED :
                bedtime_restricted() ? PTC_UI_T_STILL_RESTRICTED_BY_BEDTIME : displayed_summary_.daily_restriction_active ?
                PTC_UI_T_STILL_RESTRICTED_BY_DAILY_LIMIT : PTC_UI_T_PLEASE_CHECK_CURRENT_RESTRICTION_STATUS),
                PTC_OVERLAY_FONT_BODY_LG, displayed_summary_.access_recovery_required ? pal.warning : pal.success);
        } else if (!bridge_->waiting) {
            flow_text(renderer, ptc_overlay_bridge_error_message_zh(bridge_), PTC_OVERLAY_FONT_BODY_LG, pal.danger);
            if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_ERROR_CODE_D), bridge_->summary.error_code);
                flow_text(renderer, line, PTC_OVERLAY_FONT_BODY, pal.danger);
            }
            flow_text(renderer, ptc_ui_text(PTC_UI_T_WHEN_BACKGROUND_IS_UNAVAILABLE_PLEASE_RECOVER_VIA), PTC_OVERLAY_FONT_BODY, pal.text_secondary);
        }
        end_page(renderer);
        if (last_request_kind_ != OverlayRequestKind::ClaimDailyBuffer)
            draw_button(renderer, page_layout_.secondary, ptc_ui_text(PTC_UI_T_Y_RE_VERIFY), pal.accent, false, PTC_OVERLAY_FONT_BTN, result_focus_ == 1);
        else page_layout_.secondary = {};
        draw_button(renderer, page_layout_.primary, ptc_ui_text(PTC_UI_T_B_BACK_2), pal.accent, true, PTC_OVERLAY_FONT_BTN, result_focus_ == 0);
    }

    void draw_status_details(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        const auto &summary = displayed_summary_;
        char line[384];
        begin_page(renderer, cx, cy, cw, ptc_ui_text(PTC_UI_T_OPERATION_RESULT), true);
        char age[64];
        format_refresh_age(age, sizeof(age));
        flow_text(renderer, age, PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary, 6);
        if (error_) {
            dashboard_panel(renderer, {
                {request_label(), PTC_OVERLAY_FONT_BODY_LG, pal.text_primary},
                {ptc_overlay_bridge_error_message_zh(bridge_), PTC_OVERLAY_FONT_BODY_LG, pal.danger}
            }, pal.danger);
            if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_ERROR_CODE_D), bridge_->summary.error_code);
                flow_text(renderer, line, PTC_OVERLAY_FONT_BODY, pal.danger, 6);
            }
            flow_text(renderer, ptc_ui_text(last_request_kind_ == OverlayRequestKind::OfflineCode ||
                last_request_kind_ == OverlayRequestKind::PreviewOfflineCode ? PTC_UI_T_PLEASE_RE_ENTER_Y_RETURNS_TO_INPUT : PTC_UI_T_PRESS_Y_TO_TRY_AGAIN),
                PTC_OVERLAY_FONT_BODY, pal.danger, 10);
        } else if (has_status_snapshot_) {
            // Logical Block 1: 今日核心状态 (Core Status)
            renderer->drawRect(page_layout_.viewport.x + 4,
                flow_y_ - static_cast<int>(page_scroll_), page_layout_.viewport.w - 12, 1,
                renderer->a(pal.border_decorative));
            flow_text(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_GROUP_CORE_STATUS), PTC_OVERLAY_FONT_BODY, pal.accent, 6);
            char label[128], value[128], note[128];
            int day, second;
            ptc_overlay_format_child_quota_parts(&summary, label, sizeof(label), value, sizeof(value), note, sizeof(note));
            child_card(renderer, label, value, note, PTC_OVERLAY_FONT_DISPLAY,
                child_status_fresh(summary, day, second) ? pal.success : pal.warning,
                PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
            ptc_overlay_format_child_restriction_detail(&summary, line, sizeof(line));
            if (line[0]) flow_text(renderer, line, PTC_OVERLAY_FONT_BODY_LG, pal.warning, 6);

            // Logical Block 2: 控制规则与策略 (Rules & Policies)
            renderer->drawRect(page_layout_.viewport.x + 4,
                flow_y_ - static_cast<int>(page_scroll_), page_layout_.viewport.w - 12, 1,
                renderer->a(pal.border_decorative));
            flow_text(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_GROUP_RULES), PTC_OVERLAY_FONT_BODY, pal.accent, 6);
            const auto policies = child_policy_status(summary);
            const auto eye = child_eye_care_status(summary);
            child_rule(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_EYE_LABEL), eye.compact, eye.detail,
                eye.warning ? pal.warning : summary.eye_care_enabled ? pal.success : pal.text_secondary, ChildCardKind::Eye);
            child_rule(renderer, ptc_ui_text(PTC_UI_T_POLICY_BED_LABEL), policies.bedtime.detail, nullptr,
                policy_color(policies.bedtime), ChildCardKind::Bedtime);
            child_rule(renderer, ptc_ui_text(PTC_UI_T_POLICY_DOCK_LABEL), policies.dock.detail, nullptr,
                policy_color(policies.dock), ChildCardKind::Dock);

            // Logical Block 3: 技术诊断与通道细节 (Collapsible secondary details)
            renderer->drawRect(page_layout_.viewport.x + 4,
                flow_y_ - static_cast<int>(page_scroll_), page_layout_.viewport.w - 12, 1,
                renderer->a(pal.border_decorative));
            const auto toggle_rect = ptc_overlay_rect(page_layout_.viewport.x + 4, flow_y_, page_layout_.viewport.w - 8, 52);
            auto visible_toggle = toggle_rect;
            visible_toggle.y -= static_cast<int>(page_scroll_);
            draw_button(renderer, visible_toggle,
                ptc_ui_text(details_tech_expanded_ ? PTC_UI_T_COLLAPSE_SECONDARY_DETAILS : PTC_UI_T_EXPAND_SECONDARY_DETAILS),
                pal.accent, details_tech_expanded_, PTC_OVERLAY_FONT_SECONDARY, status_details_focus_ == 1);
            page_layout_.status = toggle_rect;
            flow_y_ += 60;

            if (details_tech_expanded_) {
                flow_text(renderer, ptc_overlay_rule_source_label(summary.rule_source), PTC_OVERLAY_FONT_BODY, pal.text_secondary, 4);
                ptc_overlay_format_child_buffer_status(&summary, line, sizeof(line));
                flow_text(renderer, line, PTC_OVERLAY_FONT_BODY, pal.text_secondary, 4);
                flow_text(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_FOOTNOTE), PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary, 4);
                if (summary.dock_available) flow_text(renderer, ptc_ui_text(PTC_UI_T_DOCK_RULE_EXPLANATION), PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary, 4);
                flow_text(renderer, ptc_overlay_bridge_transport_label(bridge_), PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary, 4);
                if (bridge_->waiting) flow_text(renderer, transport_stage(bridge_), PTC_OVERLAY_FONT_BODY, pal.warning, 4);
            }
        } else {
            flow_text(renderer, ptc_ui_text(PTC_UI_T_THE_STATUS_HAS_NOT_BEEN_OBTAINED_YET), PTC_OVERLAY_FONT_BODY_LG, pal.text_secondary);
        }
        end_page(renderer);
        page_layout_.primary = ptc_overlay_rect(cx, cy + 526, cw, 76);
        page_layout_.secondary = {};
        draw_button(renderer, page_layout_.primary, ptc_ui_text(PTC_UI_T_B_BACK_2), pal.accent, true, PTC_OVERLAY_FONT_BTN, status_details_focus_ == 0);
    }

    void draw_keypad(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        const auto &summary = displayed_summary_;
        char line[128], age[64], date[64];
        overlay_text(renderer, ptc_ui_text(PTC_UI_T_PLAYWISE), cx + 4, cy, cw - 148, PTC_OVERLAY_FONT_TITLE, pal.accent);
        page_layout_.refresh = ptc_overlay_rect(cx + cw - 132, cy + 4, 128, 52);
        const bool highlight = needs_refresh() && !bridge_->waiting;
        draw_button(renderer, page_layout_.refresh, ptc_ui_text(bridge_->waiting ? PTC_UI_T_REFRESHING : PTC_UI_T_Y_REFRESH),
            bridge_->waiting ? pal.text_secondary : highlight ? pal.warning : pal.accent,
            highlight || bridge_->waiting, PTC_OVERLAY_FONT_SECONDARY);
        if (ptc_overlay_remaining_refresh_pending(bridge_->waiting, active_request_kind_ == OverlayRequestKind::OfflineCode))
            std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_CONFIRMING));
        else if (summary.valid && summary.unrestricted_today == 1) std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else if (summary.valid && summary.remaining_available) ptc_ui_format_minutes(summary.remaining_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        format_refresh_age(age, sizeof(age));
        draw_localized(renderer, line, false, cx + 14, cy + 88, PTC_OVERLAY_FONT_BODY, renderer->a(pal.success));
        const int left_width = static_cast<int>(measure_localized(renderer, line, PTC_OVERLAY_FONT_BODY).first);
        if (left_width + static_cast<int>(measure_localized(renderer, age, PTC_OVERLAY_FONT_SECONDARY).first) + 40 <= cw)
            draw_localized_right(renderer, age, cx + cw - 14, cy + 88, PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary);
        const int total = PTC_OVERLAY_CODE_SYMBOLS * PTC_OVERLAY_SLOT_W + (PTC_OVERLAY_CODE_SYMBOLS - 1) * PTC_OVERLAY_SLOT_GAP;
        for (unsigned i = 0; i < PTC_OVERLAY_CODE_SYMBOLS; ++i) {
            const int x = cx + (cw - total) / 2 + static_cast<int>(i) * (PTC_OVERLAY_SLOT_W + PTC_OVERLAY_SLOT_GAP);
            const bool cursor = input_->length < PTC_OVERLAY_CODE_SYMBOLS && i == input_->length;
            const bool entered = i < input_->length;
            draw_dashboard_surface(renderer, ptc_overlay_rect(x, cy + PTC_OVERLAY_SLOT_Y, PTC_OVERLAY_SLOT_W, PTC_OVERLAY_SLOT_H),
                cursor ? pal.accent_soft : pal.card_raised, cursor ? pal.accent : pal.border_control, cursor);
            const char symbol[] = {entered ? input_->symbols[i] : cursor ? '_' : '|', '\0'};
            draw_localized_center(renderer, symbol, x, cy + PTC_OVERLAY_SLOT_Y, PTC_OVERLAY_SLOT_W, PTC_OVERLAY_SLOT_H,
                PTC_OVERLAY_FONT_CODE, entered ? pal.text_primary : cursor ? pal.accent : pal.text_disabled);
        }
        std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_OVERLAY_INPUT_COUNT), input_->length);
        draw_localized(renderer, line, false, cx + 14, cy + 198, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.text_secondary));
        format_console_date(summary, date, sizeof(date));
        draw_localized(renderer, date, false, cx + 14, cy + 226, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.text_secondary));
        const char *charset = ptc_overlay_input_charset();
        for (unsigned i = 0; i < PTC_OVERLAY_KEY_COUNT; ++i) {
            const char symbol[] = {charset[i], '\0'};
            const auto rect = ptc_overlay_key_rect(cx, cy, i);
            const bool key_focused = keypad_nav_focus_ == -1 && i == input_->cursor;
            draw_dashboard_surface(renderer, rect, key_focused ? pal.accent_soft : pal.card_raised,
                key_focused ? pal.accent : pal.border_control, key_focused);
            if (key_focused) {
                draw_focus_ring(renderer, rect, pal.accent);
            }
            draw_localized_center(renderer, symbol, rect.x, rect.y, rect.w, rect.h, PTC_OVERLAY_FONT_KEYPAD,
                key_focused ? pal.accent : pal.text_primary);
        }
        draw_button(renderer, ptc_overlay_backspace_rect(cx, cy), ptc_ui_text(PTC_UI_T_X_BACKSPACE), pal.warning, false, PTC_OVERLAY_FONT_SECONDARY, keypad_nav_focus_ == 10);
        draw_button(renderer, ptc_overlay_clear_rect(cx, cy), ptc_ui_text(PTC_UI_T_OVERLAY_CLEAR), pal.danger, false, PTC_OVERLAY_FONT_SECONDARY, keypad_nav_focus_ == 11);
        page_layout_.primary = ptc_overlay_submit_rect(cx, cy, cw);
        const bool unavailable = has_status_snapshot_ && !status_is_stale() &&
            (summary.unrestricted_today == 1 || summary.eye_care_unlimited_capped || summary.dock_unlimited_capped);
        const bool ready = !unavailable && !bridge_->waiting && ptc_overlay_input_can_submit(input_);
        const char *submit = ptc_ui_text(unavailable ? PTC_UI_T_OVERLAY_CODE_UNAVAILABLE :
            bridge_->waiting ? PTC_UI_T_BACKGROUND_PROCESSING : ready ? PTC_UI_T_OVERLAY_SUBMIT : PTC_UI_T_OVERLAY_NEED_CODE);
        draw_button(renderer, page_layout_.primary, submit, ready ? pal.accent : unavailable ? pal.warning : pal.text_secondary,
            ready, PTC_OVERLAY_FONT_SECONDARY, keypad_nav_focus_ == 13);
        page_layout_.secondary = ptc_overlay_keypad_nav_back_rect(cx, cy);
        page_layout_.parent = ptc_overlay_keypad_nav_parent_rect(cx, cy, cw);
        draw_button(renderer, page_layout_.secondary, ptc_ui_text(PTC_UI_T_B_CANCEL_2), pal.text_primary, false, PTC_OVERLAY_FONT_BTN, keypad_nav_focus_ == 12);
        draw_button(renderer, page_layout_.parent, ptc_ui_text(PTC_UI_T_R_PARENT_AREA), pal.accent, true, PTC_OVERLAY_FONT_BTN, keypad_nav_focus_ == 14);
    }

    void draw_child_overview(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        auto summary = displayed_summary_;
        char line[384], age[64];
        begin_page(renderer, cx, cy, cw, ptc_ui_text(PTC_UI_T_PLAYWISE), true);
        const bool restricted = child_is_restricted();
        // Only the ordinary overview reserves a separate redeem row.
        if (!restricted) page_layout_.viewport.h -= 76;
        renderer->disableScissoring();
        renderer->enableScissoring(cx, page_layout_.viewport.y, cw, page_layout_.viewport.h);
        if (restricted) {
            const bool bedtime = bedtime_restricted();
            const bool eye = !bedtime && eye_care_restricted();
            const auto color = eye ? pal.warning : pal.danger;
            const auto *title = ptc_ui_text(bedtime ? PTC_UI_T_BEDTIME_ACTIVE_2 : eye ? PTC_UI_T_EYE_CARE_RESTING : PTC_UI_T_DOCK_TITLE);
            if (bedtime) {
                std::snprintf(line, sizeof(line), "%02u:%02u ~ %02u:%02u", summary.bedtime_start_minute / 60,
                    summary.bedtime_start_minute % 60, summary.bedtime_end_minute / 60, summary.bedtime_end_minute % 60);
                dashboard_panel(renderer, {{title, PTC_OVERLAY_FONT_TITLE_LG, color},
                    {line, PTC_OVERLAY_FONT_DISPLAY, pal.text_primary},
                    {ptc_ui_text(PTC_UI_T_FOR_EXCEPTIONS_SELECT_SKIP_TONIGHT_BEDTIME_IN), PTC_OVERLAY_FONT_BODY_LG, pal.text_secondary}}, color);
            } else if (eye) {
                const auto status = child_eye_care_status(summary);
                if (status.rest_seconds >= 0)
                    std::snprintf(line, sizeof(line), "%02d:%02d", status.rest_seconds / 60, status.rest_seconds % 60);
                else std::snprintf(line, sizeof(line), "%s", status.compact);
                dashboard_panel(renderer, {{title, PTC_OVERLAY_FONT_TITLE_LG, color},
                    {line, status.rest_seconds >= 0 ? PTC_OVERLAY_FONT_HERO : PTC_OVERLAY_FONT_BODY, pal.warning},
                    {status.detail, PTC_OVERLAY_FONT_BODY, pal.text_secondary}}, color);
                flow_text(renderer, ptc_ui_text(PTC_UI_T_MESSAGE_2), PTC_OVERLAY_FONT_BODY_LG, pal.text_primary);
                flow_text(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_CONFIRM_BODY), PTC_OVERLAY_FONT_BODY_LG, pal.text_secondary);
            } else {
                dashboard_panel(renderer, {{title, PTC_OVERLAY_FONT_TITLE_LG, color},
                    {ptc_ui_text(PTC_UI_T_DOCK_CONNECT), PTC_OVERLAY_FONT_BODY_LG, pal.warning},
                    {ptc_ui_text(PTC_UI_T_OVERLAY_DOCK_SUMMARY), PTC_OVERLAY_FONT_BODY_LG, pal.text_secondary}}, color);
            }
            if (!eye && summary.eye_care_enabled) {
                const auto status = child_eye_care_status(summary);
                child_rule(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_EYE_LABEL), status.compact, nullptr,
                    status.warning ? pal.warning : pal.success, ChildCardKind::Eye);
            }
        } else {
            if (ptc_overlay_remaining_refresh_pending(bridge_->waiting, active_request_kind_ == OverlayRequestKind::OfflineCode))
                std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_CONFIRMING));
            else if (summary.valid && summary.unrestricted_today == 1) std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
            else if (summary.valid && summary.remaining_available) ptc_ui_format_minutes(summary.remaining_minutes, line, sizeof(line));
            else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
            format_refresh_age(age, sizeof(age));
            char label[128], value[128], note[128];
            ptc_overlay_format_child_quota_parts(&summary, label, sizeof(label), value, sizeof(value), note, sizeof(note));
            // Keep the quota and usage together so the eye-care row fits without
            // shrinking text. Full proxy-timer explanations stay in details.
            char quota[256];
            const bool used = summary.valid && summary.played_minutes_available && summary.played_minutes >= 0;
            PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("quota", value),
                PTC_UI_TEXT_NUMBER("minutes", summary.played_minutes)};
            if (used && !summary.remaining_available && !summary.daily_rule_available &&
                summary.unrestricted_today != 1 && !summary.eye_care_unlimited_capped)
                std::snprintf(quota, sizeof(quota), "%s %s", label, value);
            else if (summary.usage_present) {
                char daily_quota[96], console_usage[96];
                ptc_ui_text_format(PTC_UI_T_OVERLAY_QUOTA, daily_quota, sizeof(daily_quota), args, 1);
                if (used) {
                    std::snprintf(console_usage, sizeof(console_usage), ptc_ui_text(PTC_UI_T_CONSOLE_USAGE_D_MIN),
                        summary.played_minutes);
                } else {
                    std::snprintf(console_usage, sizeof(console_usage), "%s %s",
                        ptc_ui_text(PTC_UI_T_CONSOLE_USAGE), ptc_ui_text(PTC_UI_T_POLICY_PENDING));
                }
                std::snprintf(quota, sizeof(quota), "%s / %s", daily_quota, console_usage);
            }
            else ptc_ui_text_format(used ? PTC_UI_T_OVERLAY_QUOTA_USAGE :
                PTC_UI_T_OVERLAY_QUOTA, quota, sizeof(quota), args, used ? 2 : 1);
            child_card(renderer, ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY), line, quota, PTC_OVERLAY_FONT_HERO,
                !summary.valid || !summary.remaining_available ? pal.text_disabled : (status_is_stale() || (summary.remaining_available && summary.remaining_minutes == 0)) ? pal.warning : pal.success,
                PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
            const auto policies = child_policy_status(summary);
            const auto eye = child_eye_care_status(summary);
            child_rule(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_EYE_LABEL), eye.compact, nullptr,
                eye.warning ? pal.warning : summary.eye_care_enabled ? pal.success : pal.text_secondary, ChildCardKind::Eye);
            child_rule(renderer, ptc_ui_text(PTC_UI_T_POLICY_BED_LABEL), policies.bedtime.compact, nullptr,
                policy_color(policies.bedtime), ChildCardKind::Bedtime);
            child_rule(renderer, ptc_ui_text(policies.mode == PTC_UI_T_STATUS_TO_CONFIRM ? PTC_UI_T_POLICY_DOCK_LABEL : policies.mode),
                policies.dock.compact, nullptr, policy_color(policies.dock), ChildCardKind::Dock);
        }
        page_layout_.status = child_card(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_STATUS),
            error_ ? ptc_ui_text(PTC_UI_T_OVERLAY_FAILED_DETAILS) : bridge_->waiting ? ptc_ui_text(PTC_UI_T_BACKGROUND_PROCESSING) :
            ptc_ui_text(PTC_UI_T_OVERLAY_OPEN_DETAILS), nullptr, PTC_OVERLAY_FONT_SECONDARY,
            error_ ? pal.danger : pal.accent, PTC_OVERLAY_FONT_SECONDARY, ChildCardKind::Plain,
            child_overview_focus_ == (restricted ? 2 : 3));
        end_page(renderer);
        page_layout_.redeem = restricted ? PtcOverlayRect{} : ptc_overlay_rect(cx, cy + 450, cw, 64);
        page_layout_.buffer = restricted ? PtcOverlayRect{} : ptc_overlay_rect(cx, cy + 526, 202, 76);
        page_layout_.parent = restricted ? ptc_overlay_rect(cx + 166, cy + 526, cw - 166, 76) : ptc_overlay_rect(cx + 214, cy + 526, cw - 214, 76);
        const bool buffer_ready = summary.valid && summary.daily_buffer_available && !restricted && !bridge_->waiting;
        if (!restricted) {
            char redeem_btn[96];
            std::snprintf(redeem_btn, sizeof(redeem_btn), "\uE0E0  %s", ptc_ui_text(PTC_UI_T_ENTER_THE_EXTRA_TIME_CODE));
            draw_button(renderer, page_layout_.redeem, redeem_btn, pal.accent, true, PTC_OVERLAY_FONT_BTN, child_overview_focus_ == 0);
            draw_button(renderer, page_layout_.buffer, ptc_ui_text(buffer_ready ? PTC_UI_T_L_RECEIVE_AUTONOMOUS_BUFFER :
                summary.daily_buffer_claimed ? PTC_UI_T_BUFFER_RECEIVED_TODAY : PTC_UI_T_THE_BUFFER_IS_TEMPORARILY_UNAVAILABLE),
                buffer_ready ? pal.accent : pal.text_secondary, buffer_ready, PTC_OVERLAY_FONT_SECONDARY, child_overview_focus_ == 1);
        } else {
            page_layout_.secondary = ptc_overlay_rect(cx, cy + 526, 154, 76);
            draw_button(renderer, page_layout_.secondary, ptc_ui_text(PTC_UI_T_B_BACK_2), pal.text_primary, false, PTC_OVERLAY_FONT_BTN, child_overview_focus_ == 1);
        }
        draw_button(renderer, page_layout_.parent, ptc_ui_text(PTC_UI_T_R_PARENT_AREA), pal.accent, true, PTC_OVERLAY_FONT_BTN, child_overview_focus_ == (restricted ? 0 : 2));
        page_layout_.primary = {};
        if (!restricted) page_layout_.secondary = {};
    }

    void draw_overlay(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw, s32 ch)
    {
        (void)ch;
        prepare_page();
        if (access_recovery_visible()) draw_parent_page(renderer, cx, cy, cw);
        else if (success_visible_) draw_code_success(renderer, cx, cy, cw);
        else if (preview_ready_) draw_code_preview(renderer, cx, cy, cw);
        else if (status_expanded_) draw_status_details(renderer, cx, cy, cw);
        else if (is_keypad_mode() && !child_is_restricted()) draw_keypad(renderer, cx, cy, cw);
        else draw_child_overview(renderer, cx, cy, cw);
    }
