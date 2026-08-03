#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

extern "C" {
#include "../bridge.h"
#include "../input_model.h"
#include "platform/switch/fs_storage.h"
}

namespace {

constexpr char APP_ROOT[] = "sdmc:/switch/play-time-control";
constexpr tsl::Color PANEL_COLOR{ 0x1, 0x1, 0x1, 0xF };
constexpr tsl::Color KEY_COLOR{ 0x3, 0x3, 0x3, 0xF };
constexpr tsl::Color FOCUS_COLOR{ 0x0, 0xF, 0xD, 0xF };
constexpr tsl::Color TEXT_COLOR{ 0xF, 0xF, 0xF, 0xF };
constexpr tsl::Color DARK_TEXT_COLOR{ 0x0, 0x1, 0x1, 0xF };
constexpr tsl::Color MUTED_COLOR{ 0x7, 0x7, 0x7, 0xF };
constexpr tsl::Color DISABLED_COLOR{ 0x3, 0x3, 0x3, 0xF };

static unsigned int to_overlay_buttons(u64 keys)
{
    unsigned int result = 0;
    if (keys & HidNpadButton_Up) result |= PTC_OVERLAY_BUTTON_UP;
    if (keys & HidNpadButton_Down) result |= PTC_OVERLAY_BUTTON_DOWN;
    if (keys & HidNpadButton_Left) result |= PTC_OVERLAY_BUTTON_LEFT;
    if (keys & HidNpadButton_Right) result |= PTC_OVERLAY_BUTTON_RIGHT;
    if (keys & HidNpadButton_A) result |= PTC_OVERLAY_BUTTON_A;
    if (keys & HidNpadButton_B) result |= PTC_OVERLAY_BUTTON_B;
    if (keys & HidNpadButton_X) result |= PTC_OVERLAY_BUTTON_X;
    if (keys & HidNpadButton_Y) result |= PTC_OVERLAY_BUTTON_Y;
    if (keys & HidNpadButton_Plus) result |= PTC_OVERLAY_BUTTON_PLUS;
    return result;
}

class PctcGui final : public tsl::Gui {
public:
    PctcGui(PtcOverlayBridge *bridge, PtcOverlayInput *input) : bridge_(bridge), input_(input) {}

    tsl::elm::Element *createUI() override
    {
        auto frame = new tsl::elm::OverlayFrame("PCTC", "8-digit grant code");
        frame->setContent(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            (void)x; (void)y; (void)w; (void)h;
            draw_overlay(renderer);
        }));
        return frame;
    }

    void update() override
    {
        if (close_after_frames_ > 0) {
            --close_after_frames_;
            if (close_after_frames_ == 0) tsl::Overlay::get()->close();
            return;
        }
        if (!bridge_->waiting) return;
        const u64 elapsed_ns = armTicksToNs(armGetSystemTick() - request_started_tick_);
        const u64 elapsed_ms_u64 = elapsed_ns / 1000000ULL;
        const int elapsed_ms = elapsed_ms_u64 > static_cast<u64>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(elapsed_ms_u64);
        const int elapsed_delta_ms = elapsed_ms - last_elapsed_ms_;
        last_elapsed_ms_ = elapsed_ms;
        PtcCompanionStatus status = ptc_overlay_bridge_poll(
            bridge_, elapsed_delta_ms, PTC_OVERLAY_REQUEST_TIMEOUT_MS);
        if (status == PTC_COMPANION_PENDING) return;
        if (status == PTC_COMPANION_OK) {
            if (bridge_->summary.unlock_observed) {
                close_after_frames_ = 90;
            } else {
                error_ = true;
            }
        } else {
            error_ = true;
        }
    }

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) override
    {
        const u64 now = armGetSystemTick();
        int input_elapsed_ms = 0;
        if (last_input_tick_ != 0) {
            const u64 elapsed_ms = armTicksToNs(now - last_input_tick_) / 1000000ULL;
            input_elapsed_ms = elapsed_ms > static_cast<u64>(std::numeric_limits<int>::max())
                ? std::numeric_limits<int>::max()
                : static_cast<int>(elapsed_ms);
        }
        last_input_tick_ = now;
        if (close_after_frames_ > 0) return true;
        if (bridge_->waiting) return true;
        if (keysDown & HidNpadButton_B) {
            tsl::Overlay::get()->close();
            return true;
        }
        if (keysDown & HidNpadButton_Plus) {
            char code[32];
            if (ptc_overlay_input_can_submit(input_) && ptc_overlay_input_format(input_, code, sizeof(code))) {
                PtcCompanionStatus status = ptc_overlay_bridge_submit(bridge_, code, static_cast<int64_t>(std::time(nullptr)), ++request_nonce_);
                if (status == PTC_COMPANION_OK) {
                    request_started_tick_ = armGetSystemTick();
                    last_elapsed_ms_ = 0;
                } else {
                    error_ = true;
                }
            }
            return true;
        }
        if (error_ && (keysDown & HidNpadButton_Y)) {
            error_ = false;
            (void)ptc_overlay_input_handle(input_, PTC_OVERLAY_BUTTON_Y, 0, 0);
            return true;
        }
        return ptc_overlay_input_handle(
            input_,
            to_overlay_buttons(keysDown),
            to_overlay_buttons(keysHeld),
            input_elapsed_ms);
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

    static s32 code_slot_x(unsigned int index)
    {
        return 90 + static_cast<s32>(index) * 38;
    }

    void draw_overlay(tsl::gfx::Renderer *renderer)
    {
        char line[96];
        renderer->drawString("5-120 minute tier code", false, 90, 190, 28, renderer->a(0xFFFF));
        for (unsigned int index = 0; index < PTC_OVERLAY_CODE_SYMBOLS; ++index) {
            const s32 slot_x = code_slot_x(index);
            const bool is_input_cursor = input_->length < PTC_OVERLAY_CODE_SYMBOLS && index == input_->length;
            char symbol[2] = { index < input_->length ? input_->symbols[index] : '_', '\0' };
            if (is_input_cursor) {
                draw_outline(renderer, slot_x - 3, 215, 18, 42, 2, FOCUS_COLOR);
                renderer->drawRect(slot_x - 1, 258, 14, 3, renderer->a(FOCUS_COLOR));
            }
            renderer->drawString(
                symbol,
                false,
                slot_x,
                247,
                27,
                renderer->a(index < input_->length ? TEXT_COLOR : MUTED_COLOR));
        }
        std::snprintf(line, sizeof(line), "Current: %c   Entered: %u/8", ptc_overlay_input_charset()[input_->cursor], input_->length);
        renderer->drawString(line, false, 90, 300, 23, renderer->a(TEXT_COLOR));

        const char *charset = ptc_overlay_input_charset();
        renderer->drawRect(76, 333, 326, 118, renderer->a(PANEL_COLOR));
        draw_outline(renderer, 76, 333, 326, 118, 2, MUTED_COLOR);
        for (unsigned int index = 0; index < PTC_OVERLAY_KEY_COUNT; ++index) {
            char symbol[2] = { charset[index], '\0' };
            s32 key_x = 100 + static_cast<s32>(index % PTC_OVERLAY_KEY_COLUMNS) * 58;
            s32 key_y = 380 + static_cast<s32>(index / PTC_OVERLAY_KEY_COLUMNS) * 48;
            const bool focused = index == input_->cursor;
            renderer->drawRect(key_x - 6, key_y - 28, 34, 38, renderer->a(focused ? FOCUS_COLOR : KEY_COLOR));
            draw_outline(renderer, key_x - 6, key_y - 28, 34, 38, focused ? 3 : 1, focused ? TEXT_COLOR : MUTED_COLOR);
            renderer->drawString(symbol, false, key_x, key_y, 28, renderer->a(focused ? DARK_TEXT_COLOR : TEXT_COLOR));
        }

        const bool can_submit = ptc_overlay_input_can_submit(input_);
        renderer->drawString("A digit  X backspace  Y clear  B close", false, 90, 500, 18, renderer->a(TEXT_COLOR));
        renderer->drawRect(250, 520, 145, 42, renderer->a(can_submit ? FOCUS_COLOR : DISABLED_COLOR));
        draw_outline(renderer, 250, 520, 145, 42, 2, can_submit ? TEXT_COLOR : MUTED_COLOR);
        renderer->drawString("+  SUBMIT", false, 270, 551, 23, renderer->a(can_submit ? DARK_TEXT_COLOR : MUTED_COLOR));
        if (bridge_->waiting) renderer->drawString("Waiting for sysmodule...", false, 90, 655, 22, renderer->a(0xFF0F));
        if (error_) {
            const char *reason = bridge_->summary.valid && bridge_->summary.dry_run
                ? "observe_dry_run_no_unlock"
                : (bridge_->summary.valid && bridge_->summary.reason[0]
                    ? bridge_->summary.reason : "request_failed_or_timed_out");
            renderer->drawString(reason, false, 90, 645, 19, renderer->a(0xF00F));
            renderer->drawString("Still locked. Y to retry.", false, 90, 670, 20, renderer->a(0xF00F));
        }
        if (close_after_frames_ > 0) {
            if (bridge_->summary.played_minutes_available) {
                std::snprintf(line, sizeof(line), "Granted. %d remaining, about %d played.",
                    bridge_->summary.remaining_minutes, bridge_->summary.played_minutes);
            } else {
                std::snprintf(line, sizeof(line), "Granted. %d minutes remaining. Played unavailable.",
                    bridge_->summary.remaining_minutes);
            }
            renderer->drawString(line, false, 90, 655, 22, renderer->a(0x0F0F));
        }
    }

private:
    PtcOverlayBridge *bridge_;
    PtcOverlayInput *input_;
    unsigned int close_after_frames_ = 0;
    unsigned int request_nonce_ = 0;
    u64 request_started_tick_ = 0;
    u64 last_input_tick_ = 0;
    int last_elapsed_ms_ = 0;
    bool error_ = false;
};

class PctcOverlay final : public tsl::Overlay {
public:
    void initServices() override
    {
        fsdevMountSdmc();
        ptc_fs_storage_init(&storage_);
        ptc_overlay_bridge_init(&bridge_, APP_ROOT, ptc_fs_storage_as_storage(&storage_));
        ptc_overlay_input_init(&input_);
    }

    void exitServices() override { fsdevUnmountDevice("sdmc"); }

    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<PctcGui>(&bridge_, &input_); }

private:
    PtcFsStorage storage_{};
    PtcOverlayBridge bridge_{};
    PtcOverlayInput input_{};
};

} // namespace

int main(int argc, char **argv)
{
    return tsl::loop<PctcOverlay>(argc, argv);
}
