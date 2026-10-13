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
    PtcOverlayHoldState confirm_hold_{};
    ParentView parent_view_ = ParentView::Child;
    bool parent_action_succeeded_ = false;
    int parent_action_ = 0, daily_add_minutes_ = 15;
    char pin_[PTC_AUTH_PIN_MAX_LEN + 1]{}, pin_message_[128]{};
    size_t pin_length_ = 0;
#include "../../companion/overlay/source/render_methods.hpp"
};
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
            "overlay-policy-error-detail"};
        for (int language = 0; language < 3; ++language) {
            ptc_ui_language_set_resolved(languages[language]);
            auto dir = std::filesystem::path(argv[2]) / folders[language] / "overlay";
            std::filesystem::create_directories(dir);
            for (int scene = 0; scene < 17; ++scene) {
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
                std::snprintf(status.rule_source, sizeof(status.rule_source), "weekly");
                std::snprintf(input.symbols, sizeof(input.symbols), "1234"); input.length = 4;
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
                    view.parent_view_ = ParentView::Actions; view.parent_action_ = 2;
                } else if (scene == 5) {
                    status.eye_care_enabled = true; status.eye_care_rest_remaining_seconds = 300;
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
                std::fill(renderer.pixels.begin(), renderer.pixels.end(), 17);
                // Parent views hide the title/subtitle banner and raise the
                // content top; mirror that here so previews match the device.
                const bool parent_scene = view.access_recovery_visible();
                const int content_y = parent_scene ? PTC_OVERLAY_PARENT_CONTENT_Y : PTC_OVERLAY_CONTENT_Y;
                if (!parent_scene) {
                    renderer.drawString(ptc_ui_text(PTC_UI_T_PLAYWISE), false, 20, 50, 30, TEXT_COLOR);
                    renderer.drawString(ptc_ui_text(PTC_UI_T_PLAYTIME_BEDTIME_CONTROLS), false, 20, 70, 15, MUTED_COLOR);
                }
                view.draw_overlay(&renderer, PTC_OVERLAY_CONTENT_X, content_y,
                    PTC_OVERLAY_CONTENT_W, PTC_OVERLAY_CONTENT_H);
                renderer.drawRect(0, 678, 448, 42, {0, 0, 0, 15});
                renderer.drawString("HOST PREVIEW / SAMPLE DATA", false, 20, 699, 18, WAITING_COLOR);
                renderer.save((dir / (std::string(scenes[scene]) + ".ppm")).string());
            }
        }
        std::printf("PASS: %zu production Overlay previews (3 languages, %zu sample states)\n",
            3 * (sizeof(scenes) / sizeof(scenes[0])), sizeof(scenes) / sizeof(scenes[0]));
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
    }
}
