// Included inside the production GUI and deterministic host preview view.
// Keep drawings here so documentation follows the installed Overlay code.

    // Draws a rounded outline and filled surface using CPU spans for smooth corners.
    // Decoration stays strictly inside the published rectangle.
    static void draw_round_rect_surface(tsl::gfx::Renderer *renderer, s32 x, s32 y,
        s32 w, s32 h, s32 r, tsl::Color bg, tsl::Color border, s32 border_width = 1)
    {
        if (w <= 0 || h <= 0) return;
        if (r > w / 2) r = w / 2;
        if (r > h / 2) r = h / 2;
        if (r < 0) r = 0;

        // Radii offsets precomputed for radius 4 (standard overlay corner)
        // For r = 4, corner offsets per row i in [0..3]:
        // row 0: inset 3 (dx=3)
        // row 1: inset 2 (dx=2)
        // row 2: inset 1 (dx=1)
        // row 3: inset 0 (dx=0)
        // For general r, we compute standard circle inset: r - sqrt(r*r - (r-1-i)^2)
        const auto a_border = renderer->a(border);
        const auto a_bg = renderer->a(bg);
        const bool has_border = border_width > 0 && border.a > 0;

        // Middle body (between top and bottom curved bands)
        if (h > 2 * r) {
            if (has_border) {
                renderer->drawRect(x, y + r, border_width, h - 2 * r, a_border);
                renderer->drawRect(x + w - border_width, y + r, border_width, h - 2 * r, a_border);
                renderer->drawRect(x + border_width, y + r, w - 2 * border_width, h - 2 * r, a_bg);
            } else {
                renderer->drawRect(x, y + r, w, h - 2 * r, a_bg);
            }
        }

        // Top and bottom rounded bands
        for (int i = 0; i < r; ++i) {
            // Compute horizontal inset from circle equation
            int dy = r - 1 - i;
            int dx = 0;
            if (r == 4) {
                dx = (i == 0) ? 3 : ((i == 1) ? 2 : ((i == 2) ? 1 : 0));
            } else if (r == 3) {
                dx = (i == 0) ? 2 : ((i == 1) ? 1 : 0);
            } else if (r <= 2) {
                dx = (i == 0) ? 1 : 0;
            } else {
                dx = r - (int)std::round(std::sqrt(std::max(0, r * r - dy * dy)));
            }

            for (const int row_y : {y + i, y + h - 1 - i}) {
                int row_w = w - 2 * dx;
                if (row_w <= 0) continue;
                if (!has_border) {
                    renderer->drawRect(x + dx, row_y, row_w, 1, a_bg);
                } else if (i < border_width || (h - 1 - (row_y - y)) < border_width) {
                    // Fully border rows at extreme top/bottom
                    renderer->drawRect(x + dx, row_y, row_w, 1, a_border);
                } else {
                    int inner_dx = dx + border_width;
                    int inner_w = w - 2 * inner_dx;
                    if (inner_w > 0) {
                        renderer->drawRect(x + dx, row_y, border_width, 1, a_border);
                        renderer->drawRect(x + dx + border_width, row_y, inner_w, 1, a_bg);
                        renderer->drawRect(x + w - dx - border_width, row_y, border_width, 1, a_border);
                    } else {
                        renderer->drawRect(x + dx, row_y, row_w, 1, a_border);
                    }
                }
            }
        }
    }

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
        draw_round_rect_surface(renderer, x, y, width, height, 4, bg, border, border_width);
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
        draw_round_rect_surface(renderer, x, y, width, height, height / 2, bg, border, border_width);
    }

    const OverlayPalette &current_palette() const
    {
        return get_overlay_palette(theme_);
    }

    static bool same_color(tsl::Color left, tsl::Color right)
    {
        return left.r == right.r && left.g == right.g && left.b == right.b;
    }

    tsl::Color policy_color(const PtcPolicyDisplay &policy) const
    {
        const auto &pal = current_palette();
        if (policy.state == PTC_POLICY_ACTIVE) return pal.danger;
        if (policy.state == PTC_POLICY_UNKNOWN || policy.state == PTC_POLICY_WAITING ||
            policy.state == PTC_POLICY_EXHAUSTED ||
            (policy.state == PTC_POLICY_PENDING && policy.minutes <= 15)) return pal.warning;
        if (policy.state == PTC_POLICY_AVAILABLE || policy.state == PTC_POLICY_TV_ONLY ||
            policy.state == PTC_POLICY_SKIPPED || policy.state == PTC_POLICY_WAIVED) return pal.success;
        return pal.text_secondary;
    }

    tsl::Color dashboard_soft(tsl::Color color) const
    {
        const auto &pal = current_palette();
        if (same_color(color, pal.danger)) return pal.danger_soft;
        if (same_color(color, pal.warning)) return pal.warning_soft;
        if (same_color(color, pal.success)) return pal.success_soft;
        return pal.accent_soft;
    }

    // Smooth rounded dashboard surface using Tesla's drawRect CPU spans.
    // Decoration stays strictly inside the published rectangle and needs no textures.
    void draw_dashboard_surface(tsl::gfx::Renderer *renderer, PtcOverlayRect rect,
        tsl::Color bg, tsl::Color border, bool emphasized = false)
    {
        if (rect.w <= 0 || rect.h <= 0) return;
        const int edge = emphasized ? 2 : 1;

        // Central body between top and bottom corner bands
        renderer->drawRect(rect.x, rect.y + 2, rect.w, rect.h - 4, renderer->a(border));
        renderer->drawRect(rect.x + edge, rect.y + 3, rect.w - edge * 2,
            rect.h - 6, renderer->a(bg));

        // Smooth rounded corner spans (circular arc: clips (0,0), (1,0), (0,1))
        // Top and bottom symmetric bands
        for (const int y : {rect.y, rect.y + rect.h - 1}) {
            // Row 0 / Row h-1: starts at x+2 to create smooth circular curve
            renderer->drawRect(rect.x + 2, y, rect.w - 4, 1, renderer->a(border));
        }

        for (const int y : {rect.y + 1, rect.y + rect.h - 2}) {
            // Row 1 / Row h-2: outer starts at x+1
            renderer->drawRect(rect.x + 1, y, rect.w - 2, 1, renderer->a(border));
            if (edge == 1) {
                // For 1px border, inner starts at x+3 so border is 2px wide (x+1, x+2) to 4-connect to row 0 (x+2)
                renderer->drawRect(rect.x + 3, y, rect.w - 6, 1, renderer->a(bg));
            }
            // For edge == 2, row 1 remains solid border
        }

        for (const int y : {rect.y + 2, rect.y + rect.h - 3}) {
            // Row 2 / Row h-3: outer starts at x+0
            // Border is at x+0, x+1 (inner at x+2 for edge=1, x+3 for edge=2)
            const int inner = edge == 1 ? 2 : 3;
            renderer->drawRect(rect.x + inner, y, rect.w - inner * 2, 1, renderer->a(bg));
        }

        // Subtly recessed top decorative highlight line
        renderer->drawRect(rect.x + 6, rect.y + edge, rect.w - 12, 1,
            renderer->a(current_palette().border_decorative));
    }

    void draw_child_icon(tsl::gfx::Renderer *renderer, ChildCardKind kind,
        int x, int y, tsl::Color color)
    {
        const auto ink = renderer->a(color);
        if (kind == ChildCardKind::Eye) {
            for (int row = 0; row < 12; ++row) {
                const int inset = std::abs(row * 2 - 11) - 1;
                renderer->drawRect(x + inset, y + row + 4, 2, 1, ink);
                renderer->drawRect(x + 20 - inset, y + row + 4, 2, 1, ink);
            }
            renderer->drawRect(x + 9, y + 8, 4, 6, ink);
        } else if (kind == ChildCardKind::Bedtime) {
            for (int row = 0; row < 20; ++row) {
                const int inset = std::abs(row - 9) / 2;
                const int width = row < 4 || row > 15 ? 16 - inset * 2 : 4;
                renderer->drawRect(x + inset + 2, y + row, width, 1, ink);
            }
        } else if (kind == ChildCardKind::Dock) {
            draw_outline(renderer, x, y + 2, 22, 14, 2, color);
            renderer->drawRect(x + 10, y + 16, 2, 3, ink);
            renderer->drawRect(x + 5, y + 19, 12, 2, ink);
        }
    }

    void dashboard_panel(tsl::gfx::Renderer *renderer,
        std::initializer_list<OverlayPanelLine> lines, tsl::Color color)
    {
        const auto &vp = page_layout_.viewport;
        const int gap = (parent_view_ == ParentView::Child || parent_view_ == ParentView::Pin) ? 6 : 12;
        int height = 0;
        for (const auto &line : lines)
            height += overlay_text_height(renderer, line.text, vp.w - 36, line.size) + gap;
        draw_dashboard_surface(renderer, ptc_overlay_rect(vp.x + 4,
            flow_y_ - static_cast<int>(page_scroll_), vp.w - 8, height + 8),
            dashboard_soft(color), current_palette().border_decorative);
        renderer->drawRect(vp.x + 4, flow_y_ + 6 - static_cast<int>(page_scroll_),
            3, height - 4, renderer->a(color));
        flow_y_ += 4;
        for (const auto &line : lines) flow_text(renderer, line.text, line.size, line.color);
        flow_y_ += 8;
    }

    void draw_parent_action_icon(tsl::gfx::Renderer *renderer,
        PtcOverlayParentAction action, int x, int y, tsl::Color color)
    {
        if (action == PTC_OVERLAY_PARENT_SKIP_EYE_CARE)
            draw_child_icon(renderer, ChildCardKind::Eye, x, y, color);
        else if (action == PTC_OVERLAY_PARENT_WAIVE_DOCK)
            draw_child_icon(renderer, ChildCardKind::Dock, x, y, color);
        else if (action == PTC_OVERLAY_PARENT_SKIP_BEDTIME ||
                 action == PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP || action == PTC_OVERLAY_PARENT_DISABLE_BEDTIME)
            draw_child_icon(renderer, ChildCardKind::Bedtime, x, y, color);
        else if (action == PTC_OVERLAY_PARENT_RESTORE_SNAPSHOT) {
            draw_outline(renderer, x + 4, y + 4, 16, 14, 2, color);
            renderer->drawRect(x, y + 5, 10, 2, renderer->a(color));
            for (int step = 0; step < 4; ++step) {
                renderer->drawRect(x + step, y + 5 - step, 2, 1, renderer->a(color));
                renderer->drawRect(x + step, y + 6 + step, 2, 1, renderer->a(color));
            }
        }
        else {
            draw_outline(renderer, x + 1, y + 1, 20, 20, 2, color);
            renderer->drawRect(x + 10, y + 5, 2, 7, renderer->a(color));
            renderer->drawRect(x + 10, y + 10, 6, 2, renderer->a(color));
        }
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
        if (armTicksToNs(armGetSystemTick() - last_refresh_tick_) >= 30000000000ULL) return true;
        if (displayed_summary_.usage_present && !displayed_summary_.played_minutes_available) return true;
        return false;
    }

    bool needs_refresh() const
    {
        if (!has_status_snapshot_ || last_refresh_tick_ == 0) return true;
        if (status_is_stale()) return true;
        if (displayed_summary_.usage_present && !displayed_summary_.played_minutes_available) return true;
        return false;
    }

    bool child_status_fresh(const PtcCompanionResultSummary &summary, int &day, int &second) const
    {
        day = second = -1;
        bool clock_fresh = true;
#ifdef PTC_OVERLAY_PREVIEW_CLOCK
        day = 2469;
        second = 496 * 60;
#else
        const long long now = static_cast<long long>(std::time(nullptr));
        (void)ptc_policy_local_clock(now, &day, &second);
        clock_fresh = last_refresh_wall_time_ > 0 && now >= last_refresh_wall_time_ &&
            now - last_refresh_wall_time_ < 30;
#endif
        return summary.valid && summary.day_index == day && clock_fresh && has_status_snapshot_ && last_refresh_tick_ != 0 && !status_is_stale() &&
            !(error_ && last_request_kind_ == OverlayRequestKind::Status);
    }

    PtcOverlayEyeCareDisplay child_eye_care_status(const PtcCompanionResultSummary &summary) const
    {
        int day, second;
        const bool fresh = child_status_fresh(summary, day, second);
        const auto age = last_refresh_tick_ ? armTicksToNs(armGetSystemTick() - last_refresh_tick_) / 1000000000ULL : 0;
        PtcOverlayEyeCareDisplay eye;
        ptc_overlay_project_eye_care(&summary, fresh, static_cast<int64_t>(age), &eye);
        return eye;
    }

    PtcPolicyStatus child_policy_status(const PtcCompanionResultSummary &summary) const
    {
        int day, second;
        const bool fresh = child_status_fresh(summary, day, second);
        PtcPolicyStatus policies;
        ptc_policy_status_project(&summary, fresh, day, second, &policies);
        return policies;
    }

#include "render_pages.hpp"
