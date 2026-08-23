#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "release_manifest.h"

extern "C" {
#include "common/version.h"
#include "companion/transport_client.h"
#include "companion/switch_ipc_client.h"
#include "platform/switch/fs_storage.h"
}

namespace {

[[gnu::used]] constexpr char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

struct SessionView {
    char run_id[48]{};
    char state[32]{"not_started"};
    char active_phase[32]{};
    char restore_verdict[32]{"not_started"};
    char last_verdict[48]{"pending"};
    int next_phase = 0;
    long long deadline = 0;
    bool restored = false;
};

static const char *const PHASES[] = {
    "home_stopped", "home_started", "game_foreground",
    "game_suspended", "sleep_wake", "restriction_effect"
};

static const char *const ACTIONS[] = {
    "Stay on HOME; timer will remain stopped.",
    "Stay on HOME; Lab will start timer for 75 seconds.",
    "Open a safe game, reopen Overlay, then press A.",
    "Return HOME with that game suspended, then press A.",
    "Press A, sleep/wake once, then reopen Overlay.",
    "Use a noncritical game with NO unsaved progress. Hold A 2 seconds."
};

static const char *find_value(const char *text, const char *key)
{
    static char pattern[64];
    std::snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    return std::strstr(text, pattern);
}

static bool read_string(const char *text, const char *key, char *out, std::size_t size)
{
    const char *value = find_value(text, key);
    const char *end;
    if (!value) return false;
    value = std::strchr(value, ':') + 1;
    while (*value == ' ') ++value;
    if (*value++ != '\"' || !(end = std::strchr(value, '\"')) || static_cast<std::size_t>(end - value) >= size) return false;
    std::memcpy(out, value, static_cast<std::size_t>(end - value));
    out[end - value] = '\0';
    return true;
}

static bool load_session(SessionView *view)
{
    char text[2048];
    FILE *file = std::fopen(PLAYWISE_SD_ROOT "/lab/session.json", "rb");
    std::size_t got;
    const char *value;
    if (!file) return false;
    got = std::fread(text, 1, sizeof(text) - 1U, file);
    std::fclose(file);
    text[got] = '\0';
    SessionView loaded;
    if (!read_string(text, "run_id", loaded.run_id, sizeof(loaded.run_id)) ||
        !read_string(text, "state", loaded.state, sizeof(loaded.state)) ||
        !read_string(text, "active_phase", loaded.active_phase, sizeof(loaded.active_phase)) ||
        !read_string(text, "restore_verdict", loaded.restore_verdict, sizeof(loaded.restore_verdict))) return false;
    value = find_value(text, "next_phase");
    if (value) loaded.next_phase = std::atoi(std::strchr(value, ':') + 1);
    value = find_value(text, "deadline");
    if (value) loaded.deadline = std::strtoll(std::strchr(value, ':') + 1, nullptr, 10);
    value = find_value(text, "restored");
    loaded.restored = value && std::strncmp(std::strchr(value, ':') + 1, "true", 4) == 0;
    if (loaded.next_phase > 0) {
        char phase_path[256];
        char phase_text[1536];
        std::snprintf(phase_path, sizeof(phase_path), PLAYWISE_SD_ROOT "/lab/phase-%d.json", loaded.next_phase - 1);
        FILE *phase_file = std::fopen(phase_path, "rb");
        if (phase_file) {
            std::size_t phase_size = std::fread(phase_text, 1, sizeof(phase_text) - 1U, phase_file);
            std::fclose(phase_file);
            phase_text[phase_size] = '\0';
            (void)read_string(phase_text, "product_semantics", loaded.last_verdict, sizeof(loaded.last_verdict));
        }
    }
    *view = loaded;
    return true;
}

class LabGui final : public tsl::Gui {
public:
    explicit LabGui(PtcCompanionTransportClient *transport) : transport_(transport) {}

    tsl::elm::Element *createUI() override
    {
        refresh();
        auto frame = new tsl::elm::OverlayFrame("PlayWise Device Lab", "INTERNAL / DANGEROUS");
        frame->setContent(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            draw(renderer, x, y, w, h);
        }));
        return frame;
    }

    void update() override
    {
        const u64 tick = armGetSystemTick();
        if (waiting_) {
            char result[2048];
            int elapsed = last_poll_tick_ == 0 ? 16 : static_cast<int>(armTicksToNs(tick - last_poll_tick_) / 1000000ULL);
            last_poll_tick_ = tick;
            PtcCompanionStatus status = ptc_companion_transport_poll(transport_, elapsed, 10000, result, sizeof(result));
            if (status != PTC_COMPANION_PENDING) {
                waiting_ = false;
                error_ = status != PTC_COMPANION_OK;
                refresh();
            }
        }
        if (last_refresh_tick_ == 0 || armTicksToNs(tick - last_refresh_tick_) >= 500000000ULL) refresh();
    }

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) override
    {
        if (keysDown & HidNpadButton_B) { tsl::Overlay::get()->close(); return true; }
        if (waiting_) return true;
        if (std::strcmp(view_.state, "awaiting_observation") == 0) {
            if (keysDown & HidNpadButton_Up) observation_ = observation_ == 0 ? 2 : observation_ - 1;
            if (keysDown & HidNpadButton_Down) observation_ = (observation_ + 1) % 3;
            if (keysDown & HidNpadButton_A) submit_observation();
            return true;
        }
        if (std::strcmp(view_.state, "restore_required") == 0) {
            if (keysDown & HidNpadButton_A) submit_empty("lab_session_restore");
            return true;
        }
        if (keysDown & HidNpadButton_X) { submit_empty("lab_session_restore"); return true; }
        if (view_.next_phase == 5 && std::strcmp(view_.state, "ready") == 0) {
            if (keysHeld & HidNpadButton_A) {
                if (danger_hold_tick_ == 0) danger_hold_tick_ = armGetSystemTick();
                if (armTicksToNs(armGetSystemTick() - danger_hold_tick_) >= 2000000000ULL) {
                    danger_hold_tick_ = 0;
                    submit_phase();
                }
            } else danger_hold_tick_ = 0;
            return true;
        }
        if (keysDown & HidNpadButton_A) {
            if (std::strcmp(view_.state, "not_started") == 0)
                submit_empty("lab_session_start");
            else if (std::strcmp(view_.state, "ready") == 0) submit_phase();
            return true;
        }
        return false;
    }

private:
    void refresh()
    {
        SessionView fresh;
        if (load_session(&fresh)) view_ = fresh;
        else view_ = SessionView{};
        last_refresh_tick_ = armGetSystemTick();
    }

    void submit_json(const char *type, const char *payload)
    {
        char request[512];
        char request_id[80];
        long long now = static_cast<long long>(std::time(nullptr));
        std::snprintf(request_id, sizeof(request_id), "lab-ui-%016llx", static_cast<unsigned long long>(randomGet64()));
        std::snprintf(request, sizeof(request),
            "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"created_at\":%lld,\"payload\":%s}\n",
            request_id, type, now, payload);
        PtcCompanionStatus status = ptc_companion_transport_submit_json(transport_, request_id, request);
        waiting_ = status == PTC_COMPANION_OK;
        error_ = status != PTC_COMPANION_OK;
        last_poll_tick_ = armGetSystemTick();
    }

    void submit_empty(const char *type) { submit_json(type, "{}"); }

    void submit_phase()
    {
        if (view_.next_phase < 0 || view_.next_phase >= 6) return;
        char payload[96];
        std::snprintf(payload, sizeof(payload), "{\"phase\":\"%s\"}", PHASES[view_.next_phase]);
        submit_json("lab_phase_start", payload);
    }

    void submit_observation()
    {
        static const char *const VALUES[] = {"restriction_visible", "no_visible_restriction", "unsure"};
        char payload[96];
        std::snprintf(payload, sizeof(payload), "{\"observation\":\"%s\"}", VALUES[observation_]);
        submit_json("lab_observation", payload);
    }

    void draw(tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h)
    {
        (void)w; (void)h;
        constexpr tsl::Color white{0xF,0xF,0xF,0xF};
        constexpr tsl::Color muted{0x9,0x9,0x9,0xF};
        constexpr tsl::Color danger{0xF,0x3,0x3,0xF};
        constexpr tsl::Color good{0x2,0xE,0x8,0xF};
        char line[256];
        renderer->drawString("Dedicated Lab root + pwtl:u", false, x + 20, y + 42, 16, renderer->a(muted));
        std::snprintf(line, sizeof(line), "State: %s", view_.state);
        renderer->drawString(line, false, x + 20, y + 92, 22, renderer->a(
            std::strcmp(view_.state, "restore_required") == 0 ? danger : white));
        std::snprintf(line, sizeof(line), "Automatic verdict: %s", view_.last_verdict);
        renderer->drawString(line, false, x + 20, y + 118, 14, renderer->a(muted));
        if (std::strcmp(view_.state, "sampling") == 0) {
            long long remaining = view_.deadline - static_cast<long long>(std::time(nullptr));
            if (remaining < 0) remaining = 0;
            std::snprintf(line, sizeof(line), "Phase: %s", view_.active_phase);
            renderer->drawString(line, false, x + 20, y + 135, 18, renderer->a(white));
            std::snprintf(line, sizeof(line), "Automatic sample: %lld s", remaining);
            renderer->drawString(line, false, x + 20, y + 175, 24, renderer->a(good));
            renderer->drawString("You may close this Overlay. Recovery stays active.", false, x + 20, y + 220, 15, renderer->a(muted));
        } else if (std::strcmp(view_.state, "awaiting_observation") == 0) {
            static const char *const LABELS[] = {"Restriction visible", "No visible restriction", "Unsure"};
            renderer->drawString("What did the user see?", false, x + 20, y + 140, 20, renderer->a(white));
            for (int i = 0; i < 3; ++i) {
                std::snprintf(line, sizeof(line), "%s %s", observation_ == i ? ">" : " ", LABELS[i]);
                renderer->drawString(line, false, x + 30, y + 185 + i * 36, 18, renderer->a(observation_ == i ? good : white));
            }
            renderer->drawString("Up/Down choose; A confirms", false, x + 20, y + 320, 15, renderer->a(muted));
        } else if (std::strcmp(view_.state, "complete") == 0) {
            renderer->drawString("Evidence complete; original settings restored.", false, x + 20, y + 145, 18, renderer->a(good));
            std::snprintf(line, sizeof(line), "Send: %s/reports/%s.json", PLAYWISE_SD_ROOT, view_.run_id);
            renderer->drawString(line, false, x + 20, y + 190, 14, renderer->a(white));
            renderer->drawString("Run the Device Lab NRO: Restore normal package; reboot.", false, x + 20, y + 240, 15, renderer->a(muted));
        } else if (std::strcmp(view_.state, "restore_required") == 0) {
            renderer->drawString("STOP: restoration is not proved. No more writes allowed.", false, x + 20, y + 145, 16, renderer->a(danger));
            renderer->drawString("Press A to retry immediate restore.", false, x + 20, y + 195, 20, renderer->a(white));
        } else {
            int phase = view_.next_phase;
            if (std::strcmp(view_.state, "not_started") == 0) {
                renderer->drawString("Press A to start guided evidence session.", false, x + 20, y + 155, 19, renderer->a(good));
            } else if (phase >= 0 && phase < 6) {
                std::snprintf(line, sizeof(line), "Next: %s", PHASES[phase]);
                renderer->drawString(line, false, x + 20, y + 140, 20, renderer->a(white));
                renderer->drawString(ACTIONS[phase], false, x + 20, y + 185, 15, renderer->a(phase == 5 ? danger : muted));
                renderer->drawString(phase == 5 ? "Hold A for 2 seconds" : "Press A when ready", false, x + 20, y + 245, 19, renderer->a(phase == 5 ? danger : good));
            }
        }
        renderer->drawString("X: restore original settings now   B: close", false, x + 20, y + 420, 14, renderer->a(muted));
        if (waiting_) renderer->drawString("Submitting...", false, x + 20, y + 465, 16, renderer->a(good));
        if (error_) renderer->drawString("Request failed. Check Lab service and retry.", false, x + 20, y + 465, 16, renderer->a(danger));
    }

    PtcCompanionTransportClient *transport_;
    SessionView view_{};
    bool waiting_ = false;
    bool error_ = false;
    int observation_ = 0;
    u64 danger_hold_tick_ = 0;
    u64 last_poll_tick_ = 0;
    u64 last_refresh_tick_ = 0;
};

class LabOverlay final : public tsl::Overlay {
public:
    void initServices() override
    {
        fsdevMountSdmc();
        ptc_fs_storage_init(&storage_);
        ptc_switch_ipc_client_init(&ipc_);
        ptc_companion_transport_init(&transport_, PLAYWISE_SD_ROOT,
            ptc_fs_storage_as_storage(&storage_), ptc_switch_ipc_backend(), &ipc_);
    }

    void exitServices() override
    {
        ptc_companion_transport_cancel(&transport_);
        ptc_switch_ipc_client_exit(&ipc_);
        fsdevUnmountDevice("sdmc");
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<LabGui>(&transport_); }

private:
    PtcFsStorage storage_{};
    PtcSwitchIpcClient ipc_{};
    PtcCompanionTransportClient transport_{};
};

} // namespace

int main(int argc, char **argv)
{
    return tsl::loop<LabOverlay>(argc, argv);
}
