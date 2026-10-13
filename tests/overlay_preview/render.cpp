#define PTC_OVERLAY_PREVIEW_CLOCK
// Deterministic sample states rendered by the production Overlay methods.
#include "renderer.hpp"
#include <cstring>
#include <filesystem>
extern "C" {
#include "../../companion/overlay/bridge.h"
#include "../../companion/auth.h"
#include "../../companion/overlay/input_model.h"
#include "../../companion/ui_language.h"
#include "../../common/time/ptc_time.h"
}
#include "../../companion/overlay/layout.h"
namespace {
#include "../../companion/overlay/source/render_types.hpp"
class PreviewView {
public:
    PtcUiResolvedTheme theme_ = PTC_UI_RESOLVED_DARK;
    PtcOverlayBridge *bridge_;
    PtcOverlayInput *input_;
    PtcCompanionResultSummary displayed_summary_{}, preview_summary_{}, redemption_before_{};
    OverlayRequestKind active_request_kind_ = OverlayRequestKind::None;
    OverlayRequestKind last_request_kind_ = OverlayRequestKind::None;
    u64 last_refresh_tick_ = armGetSystemTick();
    bool has_status_snapshot_ = true, status_expanded_ = false, preview_ready_ = false;
    bool preview_changed_ = false, error_ = false, success_visible_ = false;
    bool result_pending_ = false, result_failed_ = false, touch_hold_warning_ = false;
    bool keypad_active_ = false;
    PtcOverlayHoldState confirm_hold_{};
    ParentView parent_view_ = ParentView::Child;
    bool parent_action_succeeded_ = false;
    int parent_action_ = 0, daily_add_minutes_ = 15;
    char pin_[PTC_AUTH_PIN_MAX_LEN + 1]{}, pin_message_[128]{};
    size_t pin_length_ = 0;
#include "../../companion/overlay/source/render_methods.hpp"
};
}
static void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

static void test_text_layout(tsl::gfx::Renderer &renderer)
{
    const auto explicit_lines = overlay_wrap(&renderer, "first\n\nlast", 32, 380);
    require(explicit_lines.size() == 3 && explicit_lines[1].empty(), "explicit blank lines must survive wrapping");
    const auto words = overlay_wrap(&renderer, "one two", 28,
        static_cast<int>(measure_localized(&renderer, "one", 28).first) + 2);
    require(words.size() == 2 && words[0] == "one" && words[1] == "two", "English wraps at word boundaries");
    const char *samples[] = {"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", "繁體中文長說明必須完整換行顯示",
        "English words stay intact whenever they fit.", "A\n\nB", "A😀B", "\uE0E0 Confirm"};
    for (const auto *sample : samples) {
        for (const int width : {80, 160, 380}) {
            const auto lines = overlay_wrap(&renderer, sample, 32, width);
            for (const auto &line : lines)
                require(static_cast<int>(measure_localized(&renderer, line.c_str(), 32).first) <= width,
                    "wrapped text must never exceed its measured width");
        }
    }
    const auto sanitized = overlay_wrap(&renderer, "A😀B", 32, 380);
    require(sanitized.size() == 1 && sanitized[0] == "AB", "unsupported emoji is removed before measurement");
    const PtcUiTextId small_buttons[] = {PTC_UI_T_B_CANCEL_2, PTC_UI_T_B_BACK_2, PTC_UI_T_Y_RE_VERIFY};
    for (const auto label : small_buttons)
        require(overlay_text_height(&renderer, ptc_ui_text(label), 130, PTC_OVERLAY_FONT_BTN) <= 76,
            "localized small footer button must fit without shrinking");
    const PtcUiTextId large_buttons[] = {PTC_UI_T_A_CONFIRM_2, PTC_UI_T_HOLD_A_TO_CONFIRM,
        PTC_UI_T_HOLD_A_1S_TO_RESTORE, PTC_UI_T_VERIFY_PIN, PTC_UI_T_R_PARENT_AREA};
    for (const auto label : large_buttons)
        require(overlay_text_height(&renderer, ptc_ui_text(label), 226, PTC_OVERLAY_FONT_BTN) <= 76,
            "localized primary footer button must fit without shrinking");
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    try {
        tsl::gfx::Renderer renderer(argv[1]);
        const PtcUiLanguagePreference languages[] = {PTC_UI_LANGUAGE_SIMPLIFIED,
            PTC_UI_LANGUAGE_ENGLISH, PTC_UI_LANGUAGE_TRADITIONAL};
        const char *folders[] = {"", "en/", "zh_hant/"};
        const char *scenes[] = {"overlay-code-entry", "overlay-code-confirm", "overlay-code-success",
            "overlay-parent-actions", "overlay-bedtime", "overlay-eye-care", "overlay-unknown",
            "overlay-dock-handheld", "overlay-dock-restricted", "overlay-dock-waiver",
            "overlay-policy-pending", "overlay-policy-expanded", "overlay-policy-active",
            "overlay-policy-tv-empty", "overlay-policy-stale", "overlay-policy-success-detail",
            "overlay-policy-error-detail", "overlay-parent-top", "overlay-parent-middle", "overlay-parent-bottom",
            "overlay-parent-pin", "overlay-parent-pin-error", "overlay-parent-restore-confirm", "overlay-parent-result",
            "overlay-parent-error", "overlay-code-danger", "overlay-code-capped", "overlay-code-pending",
            "overlay-code-failed", "overlay-child-bedtime", "overlay-code-full", "overlay-code-unlimited",
            "overlay-code-busy", "overlay-code-changed", "overlay-max-minutes", "overlay-overview-bottom",
            "overlay-restriction-bottom", "overlay-code-interrupted", "overlay-eye-playing",
            "overlay-eye-paused", "overlay-eye-stale", "overlay-eye-boundary", "overlay-eye-unknown",
            "overlay-eye-details", "overlay-eye-off", "overlay-eye-multiple-limits",
            "overlay-eye-refresh-failed", "overlay-eye-cross-day", "overlay-eye-partial",
            "overlay-eye-rest-boundary", "overlay-eye-unlimited", "overlay-quota-unknown"};
        int child_overflows = 0;
        for (int language = 0; language < 3; ++language) {
            ptc_ui_language_set_resolved(languages[language]);
            test_text_layout(renderer);
            auto dir = std::filesystem::path(argv[2]) / folders[language] / "overlay";
            std::filesystem::create_directories(dir);
            for (int scene = 0; scene < static_cast<int>(sizeof(scenes) / sizeof(scenes[0])); ++scene) {
                PtcOverlayBridge bridge{};
                PtcOverlayInput input{};
                ptc_overlay_input_init(&input);
                PreviewView view;
                view.bridge_ = &bridge; view.input_ = &input;
                auto &status = view.displayed_summary_;
                status.valid = true; status.remaining_available = true; status.remaining_minutes = 25;
                status.played_minutes_available = true; status.played_minutes = 35;
                status.unrestricted_today = 0; status.daily_buffer_available = true;
                status.day_index = 2469;
                status.bedtime_available = status.dock_policy_available = true;
                status.eye_care_available = true;
                std::snprintf(status.eye_care_phase, sizeof(status.eye_care_phase), "off");
                status.eye_care_play_minutes = 40;
                status.eye_care_rest_minutes = 10;
                std::snprintf(status.rule_source, sizeof(status.rule_source), "weekly");
                if (scene == 0) {
                    std::snprintf(input.symbols, sizeof(input.symbols), "1234");
                    input.length = 4;
                    view.keypad_active_ = true;
                } else {
                    input.length = 0;
                    input.symbols[0] = '\0';
                    view.keypad_active_ = false;
                }
                if (scene == 1) {
                    view.preview_ready_ = true;
                    view.preview_summary_ = status;
                    view.preview_summary_.grant_minutes = 30;
                    view.preview_summary_.remaining_after_available = true;
                    view.preview_summary_.remaining_after_minutes = 55;
                } else if (scene == 2) {
                    view.success_visible_ = true; view.redemption_before_ = status;
                    view.preview_summary_.grant_minutes = 30;
                    status.remaining_minutes = 55; status.grant_minutes = 30;
                } else if (scene == 3) {
                    view.parent_view_ = ParentView::Actions;
                    status.dock_available = true;
                    status.undocked_limit_enabled = true;
                    view.parent_action_ = PTC_OVERLAY_PARENT_WAIVE_DOCK;
                } else if (scene == 4) {
                    status.bedtime_active = true; status.bedtime_enabled = true;
                    status.bedtime_window_instance_id = 123;
                    view.parent_view_ = ParentView::Child;
                } else if (scene == 5) {
                    status.eye_care_enabled = true; status.eye_care_rest_remaining_seconds = 300;
                    status.eye_care_break_id = 123;
                    std::snprintf(status.eye_care_phase, sizeof(status.eye_care_phase), "resting");
                } else if (scene == 6) {
                    status = {}; view.has_status_snapshot_ = false; view.last_refresh_tick_ = 0;
                }
                if (scene >= 7) {
                    status.dock_available = status.undocked_usage_available = true;
                    status.undocked_limit_enabled = true;
                    status.undocked_daily_minutes = 30;
                    status.undocked_used_minutes = scene == 7 ? 12 : 30;
                    status.undocked_remaining_minutes = scene == 7 ? 18 : 0;
                    std::snprintf(status.operation_mode, sizeof(status.operation_mode), "undocked");
                    if (scene >= 8) status.dock_restriction_active = true;
                    if (scene == 9) {
                        view.parent_view_ = ParentView::Confirm;
                        view.parent_action_ = PTC_OVERLAY_PARENT_WAIVE_DOCK;
                    }
                }
                if (scene >= 10) {
                    status.dock_restriction_active = false;
                    status.dock_available = status.undocked_usage_available = true;
                    status.undocked_limit_enabled = true;
                    status.undocked_daily_minutes = 30;
                    status.undocked_used_minutes = 12;
                    status.undocked_remaining_minutes = 18;
                    status.bedtime_enabled = status.bedtime_next_available = true;
                    status.bedtime_next_start_day_index = status.day_index;
                    status.bedtime_next_start_minute = 540;
                    status.bedtime_next_end_minute = 420;
                    status.bedtime_next_window_instance_id = (static_cast<u64>(status.day_index) << 16) | 540;
                    if (scene == 11) view.status_expanded_ = true;
                    if (scene == 12) {
                        status.bedtime_active = true;
                        status.bedtime_start_day_index = status.day_index - 1;
                        status.bedtime_start_minute = 1260;
                        status.bedtime_end_minute = 526;
                        status.bedtime_window_instance_id = (static_cast<u64>(status.day_index - 1) << 16) | 1260;
                        status.dock_restriction_active = true;
                        status.undocked_used_minutes = 30;
                        status.undocked_remaining_minutes = 0;
                    }
                    if (scene == 13) {
                        status.undocked_remaining_minutes = 0;
                        status.undocked_used_minutes = 30;
                        std::snprintf(status.operation_mode, sizeof(status.operation_mode), "docked");
                    }
                    if (scene == 14) view.last_refresh_tick_ -= 31000000000ULL;
                    if (scene >= 15) {
                        view.status_expanded_ = true;
                        view.success_visible_ = scene == 15;
                        view.error_ = scene == 16;
                        view.last_request_kind_ = OverlayRequestKind::Status;
                    }
                }
                if (scene >= 17) {
                    status.dock_restriction_active = status.bedtime_active = false;
                    view.status_expanded_ = view.success_visible_ = view.error_ = false;
                    view.parent_view_ = ParentView::Child;
                    if (scene <= 19) {
                        view.parent_view_ = ParentView::Actions;
                        view.parent_action_ = scene == 17 ? PTC_OVERLAY_PARENT_ADD_MINUTES :
                            scene == 18 ? PTC_OVERLAY_PARENT_SKIP_EYE_CARE : PTC_OVERLAY_PARENT_RESTORE_SNAPSHOT;
                        view.daily_add_minutes_ = 120;
                    }
                    if (scene == 20 || scene == 21) {
                        view.parent_view_ = ParentView::Pin;
                        view.pin_length_ = 8;
                        if (scene == 21) std::snprintf(view.pin_message_, sizeof(view.pin_message_), "%s", ptc_ui_text(PTC_UI_T_PIN_INVALID));
                    }
                    if (scene == 22) {
                        view.parent_view_ = ParentView::Confirm;
                        view.parent_action_ = PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP;
                        status.bedtime_active = status.bedtime_skipped = true;
                        status.bedtime_window_instance_id = 123;
                        status.bedtime_skipped_window_available = true;
                    }
                    if (scene == 23 || scene == 24) {
                        view.parent_view_ = ParentView::Result;
                        view.parent_action_succeeded_ = scene == 23;
                        view.last_request_kind_ = OverlayRequestKind::SkipEyeCare;
                        bridge.summary.valid = true;
                        bridge.summary.error_code = scene == 24 ? 500 : 0;
                    }
                    if (scene == 25 || scene == 26 || scene == 33) {
                        view.preview_ready_ = true;
                        view.preview_changed_ = scene == 33;
                        view.preview_summary_ = status;
                        view.preview_summary_.grant_minutes = 30;
                        view.preview_summary_.remaining_after_available = true;
                        view.preview_summary_.remaining_after_minutes = scene == 25 ? 0 : 55;
                        view.preview_summary_.preview_capped = scene == 26;
                        view.preview_summary_.effective_add_minutes = 5;
                    }
                    if (scene == 27 || scene == 28) {
                        view.success_visible_ = true;
                        view.result_pending_ = scene == 27;
                        view.result_failed_ = scene == 28;
                        view.redemption_before_ = status;
                        view.preview_summary_ = status;
                        view.preview_summary_.grant_minutes = 30;
                        view.preview_summary_.remaining_after_available = true;
                        view.preview_summary_.remaining_after_minutes = 55;
                    }
                    if (scene == 29) { status.bedtime_active = true; status.bedtime_skipped = false; }
                    if (scene >= 30 && scene <= 32) {
                        view.keypad_active_ = true;
                        input.length = 8;
                        std::snprintf(input.symbols, sizeof(input.symbols), "12345678");
                        if (scene == 31) status.unrestricted_today = 1;
                        if (scene == 32) bridge.waiting = true;
                    }
                    if (scene == 34) { status.remaining_minutes = 1440; status.played_minutes = 1440; }
                    if (scene == 36) status.dock_restriction_active = true;
                    if (scene == 37) {
                        status.dock_restriction_active = true;
                        view.keypad_active_ = true;
                        input.length = 4;
                        std::snprintf(input.symbols, sizeof(input.symbols), "1234");
                    }
                }
                // First measure the production layout, then render the requested scroll position.
                if (scene >= 38) {
                    status.eye_care_enabled = scene != 44;
                    status.eye_care_used_minutes = 15;
                    std::snprintf(status.eye_care_phase, sizeof(status.eye_care_phase), "%s",
                        scene == 39 ? "paused" : scene == 42 ? "unknown" : scene == 44 ? "off" : "playing");
                    if (scene == 40) view.last_refresh_tick_ -= 31000000000ULL;
                    if (scene == 41) status.eye_care_used_minutes = 40;
                    if (scene == 43) view.status_expanded_ = true;
                    if (scene == 45) {
                        status.bedtime_active = true;
                        std::snprintf(status.eye_care_phase, sizeof(status.eye_care_phase), "paused");
                    }
                    if (scene == 46) { view.error_ = true; view.last_request_kind_ = OverlayRequestKind::Status; }
                    if (scene == 47) --status.day_index;
                    if (scene == 48) status.eye_care_available = false;
                    if (scene == 49) {
                        std::snprintf(status.eye_care_phase, sizeof(status.eye_care_phase), "resting");
                        status.eye_care_break_id = 123;
                        status.eye_care_rest_remaining_seconds = 0;
                    }
                    if (scene == 50) { status.unrestricted_today = 1; status.eye_care_unlimited_capped = true; }
                    if (scene == 51) status.remaining_available = false;
                    const auto eye = view.child_eye_care_status(status);
                    if (scene == 38) require(std::strstr(eye.compact, "25") != nullptr,
                        "ordinary child overview must project eye play countdown");
                    if (scene == 40 || scene == 41) require(eye.warning && eye.rest_seconds == -1,
                        "stale or boundary eye snapshot must wait for confirmation");
                    if (scene >= 46 && scene <= 49) require(eye.warning && eye.rest_seconds == -1 &&
                        std::strcmp(eye.compact, ptc_ui_text(PTC_UI_T_POLICY_BOUNDARY)) == 0,
                        "failed, cross-day, partial or expired eye status requires confirmation");
                }
                view.draw_overlay(&renderer, PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y,
                    PTC_OVERLAY_CONTENT_W, PTC_OVERLAY_CONTENT_H);
                require(view.page_layout_.viewport.h >= 0, "page header must leave a usable viewport");
                if (view.parent_view_ == ParentView::Child && !view.status_expanded_ && view.page_layout_.scroll_max) {
                    std::fprintf(stderr, "Child overflow: %s%s (%dpx)\n", folders[language], scenes[scene], view.page_layout_.scroll_max);
                    ++child_overflows;
                }
                if (scene == 37) {
                    require(view.page_identity() == 22 && view.page_layout_.primary.w == 0 &&
                        view.page_layout_.parent.w > 0 && view.page_layout_.secondary.w > 0,
                        "active restriction must replace an open keypad with a fixed parent recovery entry");
                }
                for (const auto rect : {view.page_layout_.primary, view.page_layout_.secondary,
                     view.page_layout_.parent, view.page_layout_.buffer, view.page_layout_.refresh}) {
                    if (rect.w == 0) continue;
                    require(rect.x >= 16 && rect.x + rect.w <= 432 && rect.y >= 40 && rect.y + rect.h <= 647,
                        "fixed controls must remain inside the sidebar and above the host footer");
                }
                if (view.parent_view_ == ParentView::Actions) {
                    int previous_bottom = view.page_layout_.viewport.y;
                    for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot) {
                        int index = 0;
                        while (ptc_overlay_parent_action_slot(index) != slot) ++index;
                        const auto rect = view.page_layout_.actions[index];
                        require(rect.h >= 104 && rect.y >= previous_bottom, "measured action cards must not overlap");
                        const int offset = ptc_overlay_scroll_to_rect(view.page_layout_.viewport, rect, 0, view.page_layout_.scroll_max);
                        require(rect.y - offset >= view.page_layout_.viewport.y &&
                            rect.y + rect.h - offset <= view.page_layout_.viewport.y + view.page_layout_.viewport.h,
                            "every parent action must be fully reachable by focus scrolling");
                        previous_bottom = rect.y + rect.h;
                    }
                    view.page_scroll_ = view.page_scroll_target_ = static_cast<float>(ptc_overlay_scroll_to_rect(
                        view.page_layout_.viewport, view.page_layout_.actions[view.parent_action_], 0, view.page_layout_.scroll_max));
                }
                if (scene == 25 || scene == 26 || scene == 33 || scene == 35 || scene == 36)
                    view.page_scroll_ = view.page_scroll_target_ = static_cast<float>(view.page_layout_.scroll_max);
                std::fill(renderer.pixels.begin(), renderer.pixels.end(), 17);
                view.draw_overlay(&renderer, PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y,
                    PTC_OVERLAY_CONTENT_W, PTC_OVERLAY_CONTENT_H);
                renderer.drawRect(0, 678, 448, 42, {0, 0, 0, 15});
                renderer.drawString("HOST PREVIEW / SAMPLE DATA", false, 20, 699, 18, WAITING_COLOR);
                renderer.save((dir / (std::string(scenes[scene]) + ".ppm")).string());
            }
        }
        require(child_overflows == 0, "ordinary child pages must fit on one screen in all three languages");
        std::printf("PASS: %zu production Overlay previews (3 languages, %zu sample states)\n",
            3 * (sizeof(scenes) / sizeof(scenes[0])), sizeof(scenes) / sizeof(scenes[0]));
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
    }
}
