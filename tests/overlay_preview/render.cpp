#define PTC_OVERLAY_PREVIEW_CLOCK
// Deterministic sample states rendered by the production Overlay methods.
#include "renderer.hpp"
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include "../ui_preview/selection.h"
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
    PtcOverlayBridge *bridge_;
    PtcOverlayInput *input_;
    PtcCompanionResultSummary displayed_summary_{}, preview_summary_{}, redemption_before_{};
    OverlayRequestKind active_request_kind_ = OverlayRequestKind::None;
    OverlayRequestKind last_request_kind_ = OverlayRequestKind::None;
    u64 last_refresh_tick_ = armGetSystemTick();
    u64 usage_snapshot_tick_ = 0;
    bool has_status_snapshot_ = true, status_expanded_ = false, preview_ready_ = false;
    bool preview_changed_ = false, error_ = false, success_visible_ = false;
    bool result_pending_ = false, result_failed_ = false, touch_hold_warning_ = false;
    bool keypad_active_ = false;
    bool language_confirm_active_ = false;
    PtcOverlayLanguageSetting language_draft_selection_ = PtcOverlayLanguageSetting::Auto;
    int language_confirm_focus_ = 0;
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
    PtcOverlayCountdownWatch countdown_watch_{};
    bool countdown_recheck_pending_ = false;
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
    PreviewView view;
    const PtcUiTextId small_buttons[] = {PTC_UI_T_B_CANCEL_2, PTC_UI_T_B_BACK_2, PTC_UI_T_Y_RE_VERIFY};
    for (const auto label : small_buttons) {
        const int size = view.fit_rail_button_size(&renderer, ptc_ui_text(label), 154);
        require(overlay_text_height(&renderer, ptc_ui_text(label), 130, size) <=
            PTC_OVERLAY_RAIL_H - 8, "localized small footer button must fit the rail");
    }
    const PtcUiTextId large_buttons[] = {PTC_UI_T_A_CONFIRM_2, PTC_UI_T_HOLD_A_TO_CONFIRM,
        PTC_UI_T_HOLD_A_1S_TO_RESTORE, PTC_UI_T_VERIFY_PIN, PTC_UI_T_R_PARENT_AREA};
    for (const auto label : large_buttons) {
        const int size = view.fit_rail_button_size(&renderer, ptc_ui_text(label), 250);
        require(overlay_text_height(&renderer, ptc_ui_text(label), 226, size) <=
            PTC_OVERLAY_RAIL_H - 8, "localized primary footer button must fit the rail");
    }
}

static void test_child_cards(tsl::gfx::Renderer &renderer)
{
    PtcOverlayBridge bridge{};
    PtcOverlayInput input{};
    PreviewView view;
    view.bridge_ = &bridge;
    view.input_ = &input;
    view.page_layout_.viewport = ptc_overlay_rect(16, 100, 416, 400);
    for (const auto kind : {ChildCardKind::Eye, ChildCardKind::Bedtime, ChildCardKind::Dock}) {
        for (const bool long_value : {false, true}) {
            view.flow_y_ = 100;
            renderer.text_runs.clear();
            const auto rect = view.child_rule(&renderer, "Rule", long_value ?
                "A long status explanation that cannot share one line with the rule title" : "Ready",
                "Supporting note", DARK_OVERLAY_PALETTE.success, kind);
            const auto &runs = renderer.text_runs;
            require(runs.front().size == 32 && runs[1].size == 28 && runs.back().size == 24,
                "all child rules must share 32/28/24 typography");
            for (const auto &run : runs)
                require(run.x >= rect.x + 14 && run.x + static_cast<int>(measure_localized(
                    &renderer, run.text.c_str(), run.size).first) <= rect.x + rect.w - 14,
                    "rule decorations must leave all measured text inside its padding");
            if (long_value) require(runs.front().x == rect.x + 14 && runs[1].y > runs.front().y,
                "long rules must drop icons and stack without shrinking text");
            else require(runs.front().x == rect.x + 44 && runs[1].x >= runs.front().x +
                static_cast<int>(measure_localized(&renderer, runs.front().text.c_str(), 32).first) + 12,
                "short rules must reserve icon space and keep values edge aligned");
        }
    }
    PtcPolicyDisplay policy{};
    policy.state = PTC_POLICY_ACTIVE;
    require(view.same_color(view.policy_color(policy), DARK_OVERLAY_PALETTE.danger), "active policy uses danger");
    for (const auto state : {PTC_POLICY_UNKNOWN, PTC_POLICY_WAITING, PTC_POLICY_EXHAUSTED}) {
        policy.state = state;
        require(view.same_color(view.policy_color(policy), DARK_OVERLAY_PALETTE.warning),
            "unknown, boundary and TV-connected exhaustion must use warning");
    }
    policy.state = PTC_POLICY_PENDING;
    policy.minutes = 15;
    require(view.same_color(view.policy_color(policy), DARK_OVERLAY_PALETTE.warning), "near bedtime uses warning");
    policy.minutes = 16;
    require(view.same_color(view.policy_color(policy), DARK_OVERLAY_PALETTE.text_secondary), "later bedtime stays neutral");
    policy.state = PTC_POLICY_OFF;
    require(view.same_color(view.policy_color(policy), DARK_OVERLAY_PALETTE.text_secondary), "disabled rules stay neutral");
}

static void test_child_header_controls(const char *font_path)
{
    const int focus_slots[] = {5, 2, 4};
    const auto saved_language = ptc_ui_language_get_resolved();
    for (const float width_scale : {1.0f, 1.35f}) {
    for (const auto language : {PTC_UI_LANGUAGE_SIMPLIFIED, PTC_UI_LANGUAGE_ENGLISH,
             PTC_UI_LANGUAGE_TRADITIONAL}) {
    ptc_ui_language_set_resolved(language);
    for (const int focus : focus_slots) {
        tsl::gfx::Renderer renderer(font_path);
        renderer.horizontal_metric_scale = width_scale;
        PtcOverlayBridge bridge{};
        PtcOverlayInput input{};
        PreviewView view;
        view.bridge_ = &bridge;
        view.input_ = &input;
        auto &status = view.displayed_summary_;
        status.valid = true;
        status.remaining_available = true;
        status.remaining_minutes = 25;
        status.day_index = 2469;
        status.bedtime_available = status.dock_policy_available = status.eye_care_available = true;
        std::snprintf(status.eye_care_phase, sizeof(status.eye_care_phase), "off");
        view.child_overview_focus_ = focus;
        renderer.text_runs.clear();
        view.draw_overlay(&renderer, PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y,
            PTC_OVERLAY_CONTENT_W, PTC_OVERLAY_CONTENT_H);

        // Language, parent area and refresh share one measured header row.
        const PtcOverlayRect controls[] = {view.page_layout_.language, view.page_layout_.parent,
            view.page_layout_.refresh};
        for (const auto &rect : controls)
            require(rect.w > 0 && rect.h == 40 && rect.x >= PTC_OVERLAY_CONTENT_X &&
                rect.x + rect.w <= PTC_OVERLAY_CONTENT_X + PTC_OVERLAY_CONTENT_W &&
                rect.y >= PTC_OVERLAY_CONTENT_Y &&
                rect.y + rect.h <= PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_HEADER_H,
                "every child header control is measured inside the sidebar header");
        require(view.page_layout_.language.x + view.page_layout_.language.w <
                view.page_layout_.parent.x &&
            view.page_layout_.parent.x + view.page_layout_.parent.w < view.page_layout_.refresh.x,
            "child header order is language, parent area, refresh without overlap");
        for (int i = 0; i < 3; ++i) {
            const auto &rect = controls[i];
            const PtcUiTextId id = i == 0 ? PTC_UI_T_OVERLAY_LANG_BUTTON :
                i == 1 ? PTC_UI_T_R_PARENT_AREA : PTC_UI_T_Y_REFRESH;
            // The drawn run carries its sanitized text: the refresh label keeps an
            // emoji that the production sanitizer strips before drawing.
            char expected[256];
            ptc_overlay_sanitize_utf8(ptc_ui_text(id), expected, sizeof(expected));
            bool label_inside = false;
            for (const auto &run : renderer.text_runs) {
                if (run.text != expected || run.size != PTC_OVERLAY_FONT_SECONDARY) continue;
                require(run.x >= rect.x + 12 && run.y >= rect.y &&
                    run.y <= rect.y + rect.h &&
                    run.x + static_cast<int>(measure_localized(&renderer, run.text.c_str(),
                        run.size).first) <= rect.x + rect.w - 12,
                    "child header labels stay inside their measured buttons");
                label_inside = true;
            }
            if (!label_inside) {
                std::fprintf(stderr, "missing header label %s in [%d,%d %dx%d]\n",
                    ptc_ui_text(id), rect.x, rect.y, rect.w, rect.h);
                for (const auto &run : renderer.text_runs)
                    std::fprintf(stderr, "  run '%s' size=%g at %d,%d\n",
                        run.text.c_str(), run.size, run.x, run.y);
            }
            require(label_inside, "every child header control keeps its label visible");
        }

        // A wrapped brand can overflow below the header even if each line is
        // individually narrow. Exercise every language, not only the default.
        bool brand_root_visible = false;
        for (const auto &run : renderer.text_runs) {
            if (run.size != PTC_OVERLAY_FONT_CAPTION || run.y > controls[0].y + 70) continue;
            require(run.y <= controls[0].y + controls[0].h && run.x >= PTC_OVERLAY_CONTENT_X &&
                run.x + static_cast<int>(measure_localized(&renderer, run.text.c_str(),
                    run.size).first) <= view.page_layout_.language.x - 8,
                "brand stays on the header row and never wraps under its controls");
            if (run.text.find(ptc_ui_text(PTC_UI_T_NAV_HOME)) != std::string::npos)
                brand_root_visible = true;
        }
        require(brand_root_visible, "complete localized brand root is visible beside the header controls");

        // Only the focused control draws a focus ring, and A reaches all three.
        const auto ring_at = [&](const PtcOverlayRect &rect) {
            const auto &pal = DARK_OVERLAY_PALETTE;
            const auto pixel = [&](int x, int y) {
                const size_t i = static_cast<size_t>((y * renderer.width + x) * 3);
                return renderer.pixels[i] == pal.accent.r * 17 &&
                    renderer.pixels[i + 1] == pal.accent.g * 17 &&
                    renderer.pixels[i + 2] == pal.accent.b * 17;
            };
            return pixel(rect.x + rect.w / 2, rect.y + 1) && pixel(rect.x + 1, rect.y + rect.h / 2);
        };
        const bool focused_control[] = {focus == 5, focus == 2, focus == 4};
        for (int i = 0; i < 3; ++i)
            require(ring_at(controls[i]) == focused_control[i],
                "cursor focus is visible on exactly the selected header control");
    }
    }
    }
    ptc_ui_language_set_resolved(saved_language);
}

static void test_stale_rest(const char *font_path, const std::filesystem::path &dir)
{
    const char *names[] = {"overlay-rest-stale", "overlay-rest-refresh-failed",
        "overlay-rest-refreshing", "overlay-rest-expired"};
    for (int i = 0; i < 4; ++i) {
        if (!ptc_preview_selected((dir / (std::string(names[i]) + ".ppm")).generic_string().c_str(), false)) continue;
        tsl::gfx::Renderer renderer(font_path);
        PtcOverlayBridge bridge{};
        PtcOverlayInput input{};
        PreviewView view;
        view.bridge_ = &bridge; view.input_ = &input;
        auto &s = view.displayed_summary_;
        s.valid = s.eye_care_available = s.eye_care_enabled = true;
        s.day_index = 2469;
        s.eye_care_play_minutes = 40; s.eye_care_rest_minutes = 10;
        s.eye_care_rest_remaining_seconds = 600; s.eye_care_break_id = 123;
        std::snprintf(s.eye_care_phase, sizeof(s.eye_care_phase), "resting");
        const u64 receipt_age = [&] {
            if (i != 3) return 121ULL;
            // The watcher only reports the end of a countdown this same view
            // already saw running, so draw it once while it still has time left.
            // A separate renderer keeps this priming frame out of the saved one.
            tsl::gfx::Renderer priming(font_path);
            view.last_refresh_tick_ = armGetSystemTick() - 121ULL * 1000000000ULL;
            view.usage_snapshot_tick_ = view.last_refresh_tick_;
            view.draw_overlay(&priming, PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y,
                PTC_OVERLAY_CONTENT_W, PTC_OVERLAY_CONTENT_H);
            require(!view.countdown_recheck_pending_,
                "a countdown that still has time left does not re-read status");
            return 600ULL;
        }();
        view.last_refresh_tick_ = armGetSystemTick() - receipt_age * 1000000000ULL;
        view.usage_snapshot_tick_ = view.last_refresh_tick_;
        if (i == 1) { view.error_ = true; view.last_request_kind_ = OverlayRequestKind::Status; }
        if (i == 2) { bridge.waiting = true; view.active_request_kind_ = OverlayRequestKind::Status; }
        renderer.text_runs.clear();
        view.draw_overlay(&renderer, PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y,
            PTC_OVERLAY_CONTENT_W, PTC_OVERLAY_CONTENT_H);
        if (view.page_identity() != 21 || view.page_layout_.scroll_max != 0)
            std::fprintf(stderr, "Rest preview %s: page=%d, overflow=%d\n", names[i],
                view.page_identity(), view.page_layout_.scroll_max);
        require(view.page_identity() == 21 && view.page_layout_.scroll_max == 0,
            "stale rest stays on its one-screen restriction page");
        bool countdown_visible = false;
        for (const auto &run : renderer.text_runs) {
            if (run.text == "07:59" && run.size == PTC_OVERLAY_FONT_HERO) countdown_visible = true;
        }
        require(countdown_visible == (i != 3), "stale rest countdown stays visible until its real deadline");
        const auto refresh_lines = overlay_wrap(&renderer, ptc_ui_text(i == 2 ?
            PTC_UI_T_REFRESHING : PTC_UI_T_Y_REFRESH), PTC_OVERLAY_FONT_SECONDARY,
            view.page_layout_.refresh.w - 24);
        for (const auto &line : refresh_lines) {
            bool found = false;
            for (const auto &run : renderer.text_runs)
                if (run.text == line && run.x >= view.page_layout_.refresh.x &&
                    run.y <= view.page_layout_.refresh.y + view.page_layout_.refresh.h) found = true;
            require(found, "countdown retains a visible refresh reminder");
        }
        int day, second;
        require(!view.child_status_fresh(s, day, second),
            "displaying a countdown never promotes cached status to fresh");
        // A watched countdown that reaches zero asks for one immediate re-read.
        require(view.countdown_recheck_pending_ == (i == 3),
            "only the expired rest countdown schedules a status re-read");
        renderer.drawRect(0, 678, 448, 42, {0, 0, 0, 13});
        renderer.drawRect(15, 647, 448 - 30, 1, {14, 14, 15, 15});
        draw_overlay_footer_hints(&renderer, {14, 14, 15, 15});
        renderer.drawString("12:34", false, 448 - 30 - 55, 693, PTC_OVERLAY_FONT_BTN, {14, 14, 15, 15});
        renderer.save((dir / (std::string(names[i]) + ".ppm")).string());
    }
}

static void test_parent_cards(tsl::gfx::Renderer &renderer)
{
    PreviewView view;
    view.parent_view_ = ParentView::Actions;
    view.page_layout_.viewport = ptc_overlay_rect(16, 100, 416, 400);
    for (const auto *label : {"Quick grant", "Restore all pre-install settings and deactivate the current configuration"}) {
        view.flow_y_ = 100;
        renderer.text_runs.clear();
        const auto rect = view.flow_card(&renderer, label, "15 min", "A supporting explanation that wraps without losing its content",
            32, DARK_OVERLAY_PALETTE.text_primary, DARK_OVERLAY_PALETTE.danger,
            PTC_OVERLAY_PARENT_RESTORE_SNAPSHOT, true);
        require(rect.h >= PTC_OVERLAY_PARENT_CARD_MIN_H && rect.h <= view.page_layout_.viewport.h,
            "parent action decoration must preserve readable, reachable measured cards");
        for (const auto &run : renderer.text_runs) {
            require(run.x >= rect.x + 14 && run.x + static_cast<int>(measure_localized(
                &renderer, run.text.c_str(), run.size).first) <= rect.x + rect.w - 14,
                "parent action icons must not overlap or clip measured text");
            require(run.y <= rect.y + rect.h, "all parent explanations must stay inside their measured card");
        }
    }
}

static void test_surface_outline(tsl::gfx::Renderer &renderer)
{
    PreviewView view;
    const tsl::Color background{1, 2, 3, 15};
    // Code slots, keypad keys, refresh, footer buttons and both card widths.
    for (const auto &dimensions : {std::pair<int, int>{44, 64}, {124, 56}, {136, 40},
        {416, PTC_OVERLAY_RAIL_H}, {408, 114}, {404, PTC_OVERLAY_PARENT_CARD_MIN_H}}) {
        for (const bool emphasized : {false, true}) {
            for (const auto color : {DARK_OVERLAY_PALETTE.border_control,
                DARK_OVERLAY_PALETTE.accent, DARK_OVERLAY_PALETTE.danger}) {
                std::fill(renderer.pixels.begin(), renderer.pixels.end(), 17);
                const auto rect = ptc_overlay_rect(16, 40, dimensions.first, dimensions.second);
                view.draw_dashboard_surface(&renderer, rect, background, color, emphasized);
                const auto border_pixel = [&](int x, int y) {
                    const size_t offset = ((rect.y + y) * renderer.width + rect.x + x) * 3;
                    return renderer.pixels[offset] == color.r * 17 &&
                        renderer.pixels[offset + 1] == color.g * 17 && renderer.pixels[offset + 2] == color.b * 17;
                };
                std::vector<bool> visited(rect.w * rect.h, false);
                std::vector<std::pair<int, int>> queue{{3, 0}};
                visited[3] = true;
                for (size_t head = 0; head < queue.size(); ++head) {
                    const auto [x, y] = queue[head];
                    for (const auto &delta : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                        const int nx = x + delta.first, ny = y + delta.second;
                        if (nx < 0 || ny < 0 || nx >= rect.w || ny >= rect.h) continue;
                        const int index = ny * rect.w + nx;
                        if (!visited[index] && border_pixel(nx, ny)) {
                            visited[index] = true;
                            queue.emplace_back(nx, ny);
                        }
                    }
                }
                for (int y = 0; y < rect.h; ++y)
                    for (int x = 0; x < rect.w; ++x)
                        if (border_pixel(x, y)) require(visited[y * rect.w + x],
                            "every normal and emphasized outline must form one connected border");
                for (const int x : {0, rect.w - 1})
                    for (const int y : {3, rect.h - 4}) require(border_pixel(x, y),
                        "all four chamfered corners must connect to their vertical strokes");
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    try {
        tsl::gfx::Renderer renderer(argv[1]);
        test_child_cards(renderer);
        test_parent_cards(renderer);
        test_surface_outline(renderer);
        test_child_header_controls(argv[1]);
        if (std::strcmp(argv[2], "--check-header") == 0) {
            std::puts("PASS: Overlay header brand, controls and focus in three languages");
            return 0;
        }
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
            "overlay-eye-rest-boundary", "overlay-eye-unlimited", "overlay-quota-unknown",
            "overlay-parent-pending", "overlay-parent-stale", "overlay-parent-busy", "overlay-parent-pin-cooldown",
            "overlay-console-usage", "overlay-console-unknown",
            "overlay-console-details", "overlay-console-details-unknown", "overlay-parent-pin-clock-unavailable",
            "overlay-language-confirm"};
        auto run_language_pass = [&](int language) -> int {
            int child_overflows = 0;
            ptc_ui_language_set_resolved(languages[language]);
            test_text_layout(renderer);
            auto dir = std::filesystem::path(argv[2]) / folders[language] / "overlay";
            if (!ptc_preview_selected(dir.generic_string().c_str(), true)) return 0;
            std::filesystem::create_directories(dir);
            test_stale_rest(argv[1], dir);
            for (int scene = 0; scene < static_cast<int>(sizeof(scenes) / sizeof(scenes[0])); ++scene) {
                if (!ptc_preview_selected((dir / (std::string(scenes[scene]) + ".ppm")).generic_string().c_str(), false)) continue;
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
                    if (std::strcmp(scenes[scene], "overlay-language-confirm") == 0) {
                        view.language_confirm_active_ = true;
                        view.language_draft_selection_ = PtcOverlayLanguageSetting::Simplified;
                        view.language_confirm_focus_ = 1;
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
                    if (scene == 40 || scene == 46) require(std::strstr(eye.compact, "25") != nullptr,
                        "stale and failed refresh keep the last observed play countdown");
                    if (scene == 41) require(eye.warning && eye.rest_seconds == -1,
                        "boundary eye snapshot must wait for confirmation");
                    if (scene >= 47 && scene <= 49) require(eye.warning && eye.rest_seconds == -1 &&
                        std::strcmp(eye.compact, ptc_ui_text(PTC_UI_T_POLICY_BOUNDARY)) == 0,
                        "failed, cross-day, partial or expired eye status requires confirmation");
                }
                if (scene >= 52 && scene <= 55) {
                    view.parent_view_ = scene == 52 ? ParentView::Result : scene == 55 ? ParentView::Pin : ParentView::Actions;
                    view.parent_action_ = PTC_OVERLAY_PARENT_ADD_MINUTES;
                    if (scene == 53) view.last_refresh_tick_ -= 31000000000ULL;
                    bridge.waiting = scene == 52 || scene == 54;
                    if (scene == 55) std::snprintf(view.pin_message_, sizeof(view.pin_message_), "%s",
                        ptc_ui_text(PTC_UI_T_TOO_MANY_PIN_ERRORS_PLEASE_WAIT_FOR));
                }
                if (scene >= 56 && scene <= 59) {
                    status.usage_present = true;
                    status.usage_ns = 4191000000000ULL;
                    status.usage_captured_at = 1000;
                    status.daily_rule_available = true;
                    status.daily_rule_minutes = 60;
                    status.remaining_minutes = 0;
                    status.played_minutes = scene == 56 || scene == 58 ? 69 : -1;
                    status.played_minutes_available = scene == 56 || scene == 58;
                    status.remaining_available = scene == 56 || scene == 58;
                    if (scene == 58 || scene == 59) {
                        view.status_expanded_ = true;
                        // Console usage lives in the collapsed diagnostics group;
                        // these scenes exist to verify it once expanded.
                        view.details_tech_expanded_ = true;
                        status.bedtime_active = true;
                        status.usage_captured_at = scene == 58 ? 1791983569 : 0;
                    }
                }
                const bool pin_clock_unavailable = std::strcmp(scenes[scene], "overlay-parent-pin-clock-unavailable") == 0;
                if (pin_clock_unavailable) {
                    view.parent_view_ = ParentView::Pin;
                    view.pin_length_ = 0;
                    std::snprintf(view.pin_message_, sizeof(view.pin_message_), "%s", ptc_ui_text(PTC_UI_T_PIN_CLOCK_UNAVAILABLE));
                }
                renderer.text_runs.clear();
                view.draw_overlay(&renderer, PTC_OVERLAY_CONTENT_X, PTC_OVERLAY_CONTENT_Y,
                    PTC_OVERLAY_CONTENT_W, PTC_OVERLAY_CONTENT_H);
                if (scene == 56 || scene == 57) {
                    std::string drawn;
                    for (const auto &run : renderer.text_runs) drawn += run.text + " ";
                    require(drawn.find("60") != std::string::npos,
                        "merged dashboard must keep the business quota even when consumption exceeds it or is unknown");
                    require(drawn.find(ptc_ui_text(PTC_UI_T_CONSOLE_USAGE)) != std::string::npos,
                        "merged dashboard must keep console usage label present");
                    if (scene == 56) {
                        require(drawn.find("69") != std::string::npos,
                            "available console consumption must display played counter");
                    } else {
                        require(drawn.find(ptc_ui_text(PTC_UI_T_POLICY_PENDING)) != std::string::npos,
                            "expired console consumption must require refresh confirmation");
                        require(drawn.find("69") == std::string::npos,
                            "merged dashboard must not retain an unavailable console counter");
                    }
                }
                if (scene == 58 || scene == 59) {
                    std::string drawn;
                    for (const auto &run : renderer.text_runs) drawn += run.text + " ";
                    require(drawn.find(ptc_ui_text(PTC_UI_T_CONSOLE_USAGE)) != std::string::npos,
                        "restricted status details must expose the direct console counter");
                    require((drawn.find("69") != std::string::npos) == (scene == 58),
                        "unknown details must not show a fabricated counter");
                    require((drawn.find("2026-10-14 13:12:49 UTC") != std::string::npos) == (scene == 58),
                        "details must show the original captured timestamp, not the refresh timestamp");
                }
                if (pin_clock_unavailable) {
                    std::string drawn, expected;
                    for (const auto &run : renderer.text_runs)
                        for (char c : run.text) if (c != ' ' && c != '\n') drawn += c;
                    for (char c : std::string(ptc_ui_text(PTC_UI_T_PIN_CLOCK_UNAVAILABLE)))
                        if (c != ' ' && c != '\n') expected += c;
                    require(drawn.find(expected) != std::string::npos,
                        "all localized clock-unavailable text must remain visible on the PIN page");
                }
                if (view.parent_view_ == ParentView::Result) {
                    const auto label = bridge.waiting ? PTC_UI_T_PROCESSING_REQUEST : view.parent_action_succeeded_ ?
                        PTC_UI_T_OPERATION_COMPLETED : PTC_UI_T_OPERATION_INCOMPLETE;
                    const auto color = bridge.waiting ? DARK_OVERLAY_PALETTE.warning : view.parent_action_succeeded_ ?
                        DARK_OVERLAY_PALETTE.success : DARK_OVERLAY_PALETTE.danger;
                    for (const auto &line : overlay_wrap(&renderer, ptc_ui_text(label), 44, 380)) {
                        bool found = false;
                        for (const auto &run : renderer.text_runs)
                            if (run.text == line && run.size == 44 && view.same_color(run.color, color)) found = true;
                        require(found, "parent pending/success/failure must use distinct visible result colors");
                    }
                }
                if (view.page_identity() == 0 || view.page_identity() == 4) {
                    const auto policies = view.child_policy_status(view.displayed_summary_);
                    for (const auto label : {PTC_UI_T_OVERLAY_EYE_LABEL, PTC_UI_T_POLICY_BED_LABEL,
                        view.page_identity() == 4 || policies.mode == PTC_UI_T_STATUS_TO_CONFIRM ?
                        PTC_UI_T_POLICY_DOCK_LABEL : policies.mode}) {
                        // Details keep 32px rule titles; the overview chain uses 28px (26px when idle).
                        const bool details_page = view.page_identity() == 4;
                        const auto title_lines = overlay_wrap(&renderer, ptc_ui_text(label), details_page ? 32 : 28, 380);
                        bool found = true;
                        for (const auto &title_line : title_lines) {
                            bool line_found = false;
                            for (const auto &run : renderer.text_runs) {
                                if (run.text == title_line) {
                                    line_found = true;
                                    require(details_page ? run.size == 32 : (run.size == 28 || run.size == 26),
                                        "production child rule titles must use their page type scale");
                                }
                            }
                            found = found && line_found;
                        }
                        if (view.page_identity() == 0 && !found) {
                            std::fprintf(stderr, "Missing rule title: %s%s (%s)\n", folders[language], scenes[scene], ptc_ui_text(label));
                            require(false, "production rule title must remain visible");
                        }
                    }
                }
                require(view.page_layout_.viewport.h >= 0, "page header must leave a usable viewport");
                if (view.parent_view_ == ParentView::Child && !view.status_expanded_ && view.page_layout_.scroll_max) {
                    std::fprintf(stderr, "Child overflow: %s%s (%dpx)\n", folders[language], scenes[scene], view.page_layout_.scroll_max);
                    ++child_overflows;
                }
                if (view.parent_view_ == ParentView::Pin && scene == 20) {
                    require(view.page_layout_.scroll_max == 0,
                        "parent pin entry must fit on one screen without scrolling");
                    require(view.page_layout_.pin_keys[10].y == view.page_layout_.backspace.y,
                        "toggle button and backspace button must share the same row");
                }
                if (scene == 37) {
                    require(view.page_identity() == 22 && view.page_layout_.primary.w == 0 &&
                        view.page_layout_.parent.w > 0 && view.page_layout_.secondary.w == 0,
                        "active restriction must replace an open keypad with a fixed parent recovery entry");
                }
                for (const auto rect : {view.page_layout_.primary, view.page_layout_.secondary,
                     view.page_layout_.parent, view.page_layout_.buffer, view.page_layout_.redeem,
                     view.page_layout_.refresh}) {
                    if (rect.w == 0) continue;
                    require(rect.x >= 16 && rect.x + rect.w <= 432 && rect.y >= PTC_OVERLAY_CONTENT_Y &&
                        rect.y + rect.h <= PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_RAIL_BOTTOM,
                        "fixed controls must remain inside the sidebar and above the host footer");
                }
                const auto region_has_ink = [&](int y0, int y1) {
                    for (int py = y0; py < y1; ++py)
                        for (int px = PTC_OVERLAY_CONTENT_X;
                             px < PTC_OVERLAY_CONTENT_X + PTC_OVERLAY_CONTENT_W; ++px) {
                            const size_t i = static_cast<size_t>((py * renderer.width + px) * 3);
                            if (renderer.pixels[i] != 17 || renderer.pixels[i + 1] != 17 ||
                                renderer.pixels[i + 2] != 17) return true;
                        }
                    return false;
                };
                require(region_has_ink(PTC_OVERLAY_CONTENT_Y,
                    PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_HEADER_H + 2),
                    "page header must be pixel-visible (no stale scissor)");
                const bool child_root_page = view.parent_view_ == ParentView::Child &&
                    !view.access_recovery_visible() && !view.success_visible_ &&
                    !view.preview_ready_ && !view.status_expanded_ &&
                    !view.language_confirm_active_ &&
                    (!view.is_keypad_mode() || view.child_is_restricted());
                if (!child_root_page) {
                    require(view.page_layout_.viewport.y == PTC_OVERLAY_CONTENT_Y +
                        PTC_OVERLAY_HEADER_H + 8, "top path header must leave the shared viewport start");
                    require(view.page_layout_.viewport.h == PTC_OVERLAY_VIEWPORT_BOTTOM -
                        (PTC_OVERLAY_HEADER_H + 8), "top path header must leave the shared viewport height");
                    const char *current = nullptr;
                    if (view.access_recovery_visible()) {
                        if (view.parent_view_ == ParentView::Pin) current = ptc_ui_text(PTC_UI_T_NAV_VERIFY);
                        else if (view.parent_view_ == ParentView::Actions) current = ptc_ui_text(PTC_UI_T_PARENT_ZONE);
                        else if (view.parent_view_ == ParentView::Confirm) current = ptc_ui_text(PTC_UI_T_NAV_CONFIRM);
                        else current = ptc_ui_text(PTC_UI_T_NAV_RESULT);
                    } else if (view.success_visible_) current = ptc_ui_text(PTC_UI_T_NAV_RESULT);
                    else if (view.preview_ready_) current = ptc_ui_text(PTC_UI_T_NAV_CONFIRM);
                    else if (view.status_expanded_) current = ptc_ui_text(PTC_UI_T_NAV_STATUS_DETAILS);
                    else if (view.is_keypad_mode()) current = ptc_ui_text(PTC_UI_T_NAV_EXTRA_TIME_CODE);
                    else if (view.language_confirm_active_) current = ptc_ui_text(PTC_UI_T_NAV_CONFIRM);
                    require(current != nullptr, "non-child pages must define a path current node");
                    bool current_found = false, title_found = false;
                    for (const auto &run : renderer.text_runs) {
                        if (run.size == 22 && run.y <= 50 && run.text == current) {
                            current_found = true;
                            require(run.x >= PTC_OVERLAY_CONTENT_X + PTC_OVERLAY_PATH_X &&
                                run.x + static_cast<int>(measure_localized(&renderer,
                                    run.text.c_str(), 22).first) <=
                                    PTC_OVERLAY_CONTENT_X + PTC_OVERLAY_CONTENT_W,
                                "path current node must stay inside the sidebar");
                        }
                        if (run.size == PTC_OVERLAY_FONT_TITLE && run.y < 60) title_found = true;
                    }
                    require(current_found, "top path bar must draw the current node");
                    require(!title_found, "top path bar must not draw a 34px page title");
                    if (view.is_keypad_mode()) {
                        require(view.page_layout_.primary.y == PTC_OVERLAY_CONTENT_Y +
                            PTC_OVERLAY_SUBMIT_Y && view.page_layout_.primary.h == PTC_OVERLAY_SUBMIT_H,
                            "keypad submit uses the shared rail contract");
                        require(view.page_layout_.primary.y + view.page_layout_.primary.h ==
                            PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_VIEWPORT_BOTTOM,
                            "keypad submit ends at the viewport bottom");
                        require(view.page_layout_.secondary.y == PTC_OVERLAY_CONTENT_Y +
                            PTC_OVERLAY_RAIL_Y && view.page_layout_.secondary.h == PTC_OVERLAY_RAIL_H &&
                            view.page_layout_.parent.y == PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_RAIL_Y &&
                            view.page_layout_.parent.h == PTC_OVERLAY_RAIL_H,
                            "keypad navigation uses the shared footer rail");
                        require(region_has_ink(PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_RAIL_Y,
                            PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_RAIL_BOTTOM),
                            "keypad navigation must be pixel-visible");
                    } else {
                        if (view.page_layout_.primary.w > 0)
                            require(view.page_layout_.primary.y == PTC_OVERLAY_CONTENT_Y +
                                PTC_OVERLAY_RAIL_Y && view.page_layout_.primary.h == PTC_OVERLAY_RAIL_H,
                                "primary action uses the shared footer rail");
                        if (view.page_layout_.secondary.w > 0)
                            require(view.page_layout_.secondary.y == PTC_OVERLAY_CONTENT_Y +
                                PTC_OVERLAY_RAIL_Y && view.page_layout_.secondary.h == PTC_OVERLAY_RAIL_H,
                                "secondary action uses the shared footer rail");
                        if (view.page_layout_.primary.w > 0 || view.page_layout_.secondary.w > 0)
                            require(region_has_ink(PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_RAIL_Y,
                                PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_RAIL_BOTTOM),
                                "fixed footer must be pixel-visible");
                    }
                } else {
                    bool brand_found = false;
                    for (const auto &run : renderer.text_runs)
                        if (run.size == 22 && run.y <= 60 &&
                            run.text.find(ptc_ui_text(PTC_UI_T_NAV_HOME)) != std::string::npos)
                            brand_found = true;
                    require(brand_found, "child root brand must carry the path root");
                    if (view.page_layout_.buffer.w > 0)
                        require(view.page_layout_.buffer.y == PTC_OVERLAY_CONTENT_Y +
                            PTC_OVERLAY_RAIL_Y && view.page_layout_.buffer.h == PTC_OVERLAY_RAIL_H,
                            "child buffer uses the shared footer rail");
                    if (view.page_layout_.redeem.w > 0)
                        require(view.page_layout_.redeem.y == PTC_OVERLAY_CONTENT_Y +
                            PTC_OVERLAY_RAIL_Y && view.page_layout_.redeem.h == PTC_OVERLAY_RAIL_H,
                            "child redeem uses the shared footer rail");
                    if (view.page_layout_.buffer.w > 0 || view.page_layout_.redeem.w > 0)
                        require(region_has_ink(PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_RAIL_Y,
                            PTC_OVERLAY_CONTENT_Y + PTC_OVERLAY_RAIL_BOTTOM),
                            "child footer must be pixel-visible");
                }
                if (view.parent_view_ == ParentView::Actions) {
                    int previous_bottom = view.page_layout_.viewport.y;
                    int previous_group = -1;
                    for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot) {
                        const int group = ptc_overlay_parent_action_group(slot);
                        require(group >= previous_group, "action group headers must follow the visual order");
                        previous_group = group;
                        const auto rect = view.page_layout_.actions[ptc_overlay_parent_action_order()[slot]];
                        require(rect.h >= PTC_OVERLAY_PARENT_CARD_MIN_H && rect.y >= previous_bottom, "measured action cards must not overlap");
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
                renderer.drawRect(0, 678, 448, 42, {0, 0, 0, 13});
                renderer.drawRect(15, 647, 448 - 30, 1, {14, 14, 15, 15});
                draw_overlay_footer_hints(&renderer, {14, 14, 15, 15});

                // Documentation samples must not dirty the source tree whenever
                // the build clock advances. The installed Overlay keeps live time.
                const char *time_buf = "12:34";
                const auto time_measured = renderer.drawString(time_buf, false, 0, 0, PTC_OVERLAY_FONT_BTN, tsl::style::color::ColorTransparent);
                const int time_x = 448 - 30 - static_cast<int>(time_measured.first);
                renderer.drawString(time_buf, false, time_x, 693, PTC_OVERLAY_FONT_BTN, {14, 14, 15, 15});
                renderer.save((dir / (std::string(scenes[scene]) + ".ppm")).string());
            }
            require(child_overflows == 0, "ordinary child pages must fit on one screen in all three languages");
            return 0;
        };

#if defined(_WIN32)
        for (int language = 0; language < 3; ++language) {
            run_language_pass(language);
        }
#else
        pid_t pids[3] = {0, 0, 0};
        const int workers = ptc_preview_workers();
        bool failed = false;
        for (int language = 0; language < 3; ++language) {
            const int slot = language % workers;
            if (pids[slot] > 0) {
                int status = 0;
                if (waitpid(pids[slot], &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) failed = true;
                pids[slot] = 0;
            }
            pid_t pid = fork();
            if (pid < 0) {
                run_language_pass(language);
            } else if (pid == 0) {
                try {
                    int res = run_language_pass(language);
                    _exit(res == 0 ? 0 : 1);
                } catch (const std::exception &e) {
                    std::fprintf(stderr, "FAIL: %s\n", e.what());
                    _exit(1);
                }
            } else {
                pids[slot] = pid;
            }
        }
        for (int language = 0; language < 3; ++language) {
            if (pids[language] > 0) {
                int status = 0;
                if (waitpid(pids[language], &status, 0) > 0) {
                    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                        failed = true;
                    }
                } else {
                    failed = true;
                }
            }
        }
        if (failed) return 1;
#endif
        std::puts("PASS: selected production Overlay scenes rendered and checked");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
    }
}
