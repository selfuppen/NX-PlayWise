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
        auto frame = new tsl::elm::OverlayFrame("PCTC", "offline grant code");
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

    bool handleInput(u64 keysDown, u64, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) override
    {
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
            ptc_overlay_input_init(input_);
            return true;
        }
        return ptc_overlay_input_handle(input_, to_overlay_buttons(keysDown));
    }

    void draw_overlay(tsl::gfx::Renderer *renderer)
    {
        char code[32];
        char line[96];
        ptc_overlay_input_format(input_, code, sizeof(code));
        renderer->drawString("Offline code", false, 90, 190, 30, renderer->a(0xFFFF));
        renderer->drawString(code[0] ? code : "---- ---- ---- ----", false, 90, 245, 38, renderer->a(0x0FF0));
        std::snprintf(line, sizeof(line), "Selected: %c  (%u/16)", ptc_overlay_input_charset()[input_->cursor], input_->length);
        renderer->drawString(line, false, 90, 300, 24, renderer->a(0xFFFF));
        const char *charset = ptc_overlay_input_charset();
        for (unsigned int index = 0; index < PTC_OVERLAY_KEY_COUNT; ++index) {
            char symbol[2] = { charset[index], '\0' };
            s32 key_x = 90 + static_cast<s32>(index % 8u) * 38;
            s32 key_y = 390 + static_cast<s32>(index / 8u) * 42;
            if (index == input_->cursor) renderer->drawRect(key_x - 5, key_y - 26, 32, 35, renderer->a(0x0A8F));
            renderer->drawString(symbol, false, key_x, key_y, 28, renderer->a(0xFFFF));
        }
        renderer->drawString("A enter   X delete   Y clear   + submit", false, 90, 595, 22, renderer->a(0xFFFF));
        if (bridge_->waiting) renderer->drawString("Waiting for sysmodule...", false, 90, 640, 22, renderer->a(0xFF0F));
        if (error_) {
            const char *reason = bridge_->summary.valid && bridge_->summary.dry_run
                ? "observe_dry_run_no_unlock"
                : (bridge_->summary.valid && bridge_->summary.reason[0]
                    ? bridge_->summary.reason : "request_failed_or_timed_out");
            renderer->drawString(reason, false, 90, 630, 19, renderer->a(0xF00F));
            renderer->drawString("Still locked. Y to retry.", false, 90, 655, 20, renderer->a(0xF00F));
        }
        if (close_after_frames_ > 0) {
            std::snprintf(line, sizeof(line), "Granted. %d minutes remaining.", bridge_->summary.remaining_minutes);
            renderer->drawString(line, false, 90, 640, 22, renderer->a(0x0F0F));
        }
    }

private:
    PtcOverlayBridge *bridge_;
    PtcOverlayInput *input_;
    unsigned int close_after_frames_ = 0;
    unsigned int request_nonce_ = 0;
    u64 request_started_tick_ = 0;
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
