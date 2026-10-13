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
