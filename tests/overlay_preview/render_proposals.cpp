#define PTC_OVERLAY_PREVIEW_CLOCK
#include "renderer.hpp"
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
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
    PtcOverlayBridge *bridge_ = nullptr;
    PtcOverlayInput *input_ = nullptr;
    PtcCompanionResultSummary displayed_summary_{}, preview_summary_{}, redemption_before_{};
    OverlayRequestKind active_request_kind_ = OverlayRequestKind::None;
    OverlayRequestKind last_request_kind_ = OverlayRequestKind::None;
    u64 last_refresh_tick_ = armGetSystemTick();
    bool has_status_snapshot_ = true, status_expanded_ = false, preview_ready_ = false;
    bool preview_changed_ = false, error_ = false, success_visible_ = false;
    bool result_pending_ = false, result_failed_ = false, touch_hold_warning_ = false;
    bool keypad_active_ = false;
    bool details_tech_expanded_ = false, parent_pin_standard_keypad_ = false;
    PtcOverlayHoldState confirm_hold_{};
    ParentView parent_view_ = ParentView::Child;
    bool parent_action_succeeded_ = false;
    int parent_action_ = 0, daily_add_minutes_ = 15;
    char pin_[PTC_AUTH_PIN_MAX_LEN + 1]{}, pin_message_[128]{};
    size_t pin_length_ = 0;
    int child_overview_focus_ = 0;
    int status_details_focus_ = 0;
    int parent_pin_focus_ = 1;
    int preview_focus_ = 1;
    int confirm_focus_ = 1;
    int result_focus_ = 0;
    int keypad_nav_focus_ = -1;
#include "../../companion/overlay/source/render_methods.hpp"
};
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "Usage: render_proposals <font_path> <output_dir>\n";
        return 1;
    }
    const char *font_path = argv[1];
    std::filesystem::path out_dir = argv[2];
    std::filesystem::create_directories(out_dir);

    tsl::gfx::Renderer renderer(font_path);
    ptc_ui_language_set_resolved(PTC_UI_LANGUAGE_SIMPLIFIED);

    struct SceneDef {
        const char *name;
        int scene_id;
    };

    SceneDef scenes[] = {
        {"child_overview", 0},
        {"code_entry", 1},
        {"code_confirm", 2},
        {"parent_pin", 3},
        {"parent_actions", 4}
    };

    OverlayNavbarScheme schemes[] = {
        OverlayNavbarScheme::Breadcrumb,
        OverlayNavbarScheme::Capsule,
        OverlayNavbarScheme::Stepper
    };

    const char *scheme_names[] = {
        "scheme1_breadcrumb",
        "scheme2_capsule",
        "scheme3_stepper"
    };

    for (int sc = 0; sc < 3; ++sc) {
        for (const auto &scenedef : scenes) {
            std::fill(renderer.pixels.begin(), renderer.pixels.end(), 17);
            renderer.text_runs.clear();

            PtcOverlayBridge bridge{};
            PtcOverlayInput input{};
            ptc_overlay_input_init(&input);
            PreviewView view;
            view.bridge_ = &bridge;
            view.input_ = &input;
            view.navbar_scheme_ = schemes[sc];

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

            if (scenedef.scene_id == 0) {
                // Child overview
                view.parent_view_ = ParentView::Child;
            } else if (scenedef.scene_id == 1) {
                // Code entry
                std::snprintf(input.symbols, sizeof(input.symbols), "1234");
                input.length = 4;
                view.keypad_active_ = true;
            } else if (scenedef.scene_id == 2) {
                // Code confirm
                view.preview_ready_ = true;
                view.preview_summary_ = status;
                view.preview_summary_.grant_minutes = 30;
                view.preview_summary_.remaining_after_available = true;
                view.preview_summary_.remaining_after_minutes = 55;
            } else if (scenedef.scene_id == 3) {
                // Parent PIN
                view.parent_view_ = ParentView::Pin;
                view.pin_length_ = 4;
            } else if (scenedef.scene_id == 4) {
                // Parent actions
                view.parent_view_ = ParentView::Actions;
                view.parent_action_ = PTC_OVERLAY_PARENT_ADD_MINUTES;
                view.daily_add_minutes_ = 30;
            }

            view.draw_overlay(&renderer, PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y,
                PTC_OVERLAY_CONTENT_W, PTC_OVERLAY_CONTENT_H);

            std::string filename = std::string(scheme_names[sc]) + "_" + scenedef.name + ".ppm";
            renderer.save((out_dir / filename).string());
            std::cout << "Rendered " << filename << std::endl;
        }
    }

    std::cout << "PASS: All proposal previews rendered successfully.\n";
    return 0;
}
