#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "release_manifest.h"

extern "C" {
#include "common/protocol/error_code.h"
#include "common/version.h"
#include "companion/transport_client.h"
#include "device_lab/ui_model.h"
#include "platform/switch/fs_storage.h"
}

namespace {

[[gnu::used]] constexpr char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

constexpr tsl::Color COLOR_TEXT{0xF, 0xF, 0xF, 0xF};
constexpr tsl::Color COLOR_MUTED{0x9, 0xA, 0xB, 0xF};
constexpr tsl::Color COLOR_PANEL{0x2, 0x3, 0x4, 0xE};
constexpr tsl::Color COLOR_PANEL_RAISED{0x3, 0x4, 0x5, 0xF};
constexpr tsl::Color COLOR_BLUE{0x3, 0xA, 0xF, 0xF};
constexpr tsl::Color COLOR_GREEN{0x2, 0xD, 0x8, 0xF};
constexpr tsl::Color COLOR_AMBER{0xF, 0xA, 0x3, 0xF};
constexpr tsl::Color COLOR_DANGER{0xF, 0x3, 0x4, 0xF};

static const char *const PHASES[] = {
    "home_stopped", "home_started", "game_foreground",
    "game_suspended", "sleep_wake", "restriction_effect"
};

static const char *const ACTIVATION_AB_PHASES[] = {
    "ab_home_awake", "ab_sleep_wake", "ab_limited_settings_only",
    "ab_restriction_settings_only", "ab_grant_settings_only",
    "ab_restriction_before_unlimited", "ab_unlimited_settings_only"
};

static bool point_in(s32 x, s32 y, s32 rx, s32 ry, s32 rw, s32 rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void draw_outline(tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h,
    s32 width, tsl::Color color)
{
    renderer->drawRect(x, y, w, width, renderer->a(color));
    renderer->drawRect(x, y + h - width, w, width, renderer->a(color));
    renderer->drawRect(x, y, width, h, renderer->a(color));
    renderer->drawRect(x + w - width, y, width, h, renderer->a(color));
}

static void draw_panel(tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h,
    tsl::Color border = COLOR_MUTED)
{
    renderer->drawRect(x, y, w, h, renderer->a(COLOR_PANEL));
    draw_outline(renderer, x, y, w, h, 1, border);
}

static const char *utf8_next(const char *cursor)
{
    const unsigned char lead = static_cast<unsigned char>(*cursor);
    if (lead < 0x80) return cursor + 1;
    if ((lead & 0xE0) == 0xC0 && cursor[1]) return cursor + 2;
    if ((lead & 0xF0) == 0xE0 && cursor[1] && cursor[2]) return cursor + 3;
    if ((lead & 0xF8) == 0xF0 && cursor[1] && cursor[2] && cursor[3]) return cursor + 4;
    return cursor + 1;
}

static void draw_wrapped(tsl::gfx::Renderer *renderer, const char *value, s32 x, s32 y,
    float size, int characters_per_line, int max_lines, tsl::Color color)
{
    const char *cursor = value ? value : "";
    for (int line = 0; *cursor && line < max_lines; ++line) {
        const char *end = cursor;
        char buffer[320];
        int characters = 0;
        while (*end && *end != '\n' && characters < characters_per_line) {
            end = utf8_next(end);
            ++characters;
        }
        std::size_t bytes = static_cast<std::size_t>(end - cursor);
        if (bytes >= sizeof(buffer)) bytes = sizeof(buffer) - 1;
        std::memcpy(buffer, cursor, bytes);
        buffer[bytes] = '\0';
        renderer->drawString(buffer, false, x, y + line * static_cast<s32>(size + 6), size, renderer->a(color));
        cursor = *end == '\n' ? end + 1 : end;
    }
}

static PtcLabSessionLoadStatus load_session(PtcLabSessionView *view)
{
    char text[4096];
    FILE *file = std::fopen(PLAYWISE_SD_ROOT "/lab/session.json", "rb");
    std::size_t got;
    if (!file) return errno == ENOENT ? PTC_LAB_SESSION_MISSING : PTC_LAB_SESSION_INVALID;
    got = std::fread(text, 1, sizeof(text) - 1U, file);
    if (std::fclose(file) != 0 || got == 0 || got >= sizeof(text) - 1U) return PTC_LAB_SESSION_INVALID;
    text[got] = '\0';
    if (!ptc_lab_session_parse(text, view)) return PTC_LAB_SESSION_INVALID;
    if (view->next_phase > 0) {
        char path[256];
        char phase_text[2048];
        const int phase_slot = std::strcmp(view->mode, "restriction_quick") == 0
            ? 5 : view->next_phase - 1;
        std::snprintf(path, sizeof(path), PLAYWISE_SD_ROOT "/lab/phase-%d.json", phase_slot);
        FILE *phase_file = std::fopen(path, "rb");
        if (phase_file) {
            std::size_t phase_size = std::fread(phase_text, 1, sizeof(phase_text) - 1U, phase_file);
            std::fclose(phase_file);
            phase_text[phase_size] = '\0';
            (void)ptc_lab_json_string(phase_text, "product_semantics", view->last_verdict,
                sizeof(view->last_verdict));
        }
    }
    return PTC_LAB_SESSION_VALID;
}

class LabGui final : public tsl::Gui {
public:
    explicit LabGui(PtcCompanionTransportClient *transport) : transport_(transport) {}

    tsl::elm::Element *createUI() override
    {
        refresh();
        auto frame = new tsl::elm::OverlayFrame("任我玩 Device Lab", "内部取证 / 危险操作");
        frame->setContent(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            draw(renderer, x, y, w, h);
        }));
        return frame;
    }

    void update() override
    {
        const u64 tick = armGetSystemTick();
        if (waiting_) {
            char result[4096]{};
            int elapsed = last_poll_tick_ == 0 ? 16 :
                static_cast<int>(armTicksToNs(tick - last_poll_tick_) / 1000000ULL);
            last_poll_tick_ = tick;
            PtcCompanionStatus status = ptc_companion_transport_poll(transport_, elapsed, 10000,
                result, sizeof(result));
            if (status != PTC_COMPANION_PENDING) {
                waiting_ = false;
                last_transport_status_ = status;
                if (status != PTC_COMPANION_OK) {
                    set_transport_error(status);
                } else {
                    char reason[64]{};
                    int code = 0;
                    if (ptc_lab_result_error(result, &code, reason, sizeof(reason))) {
                        request_error_ = true;
                        error_code_ = code;
                        std::snprintf(error_reason_, sizeof(error_reason_), "%s", reason);
                        std::snprintf(error_message_, sizeof(error_message_), "%s",
                            ptc_error_message_zh(static_cast<PtcErrorCode>(code)));
                    } else {
                        request_error_ = false;
                        error_code_ = 0;
                        error_reason_[0] = '\0';
                        error_message_[0] = '\0';
                    }
                }
                refresh();
            }
        }
        if (last_refresh_tick_ == 0 || armTicksToNs(tick - last_refresh_tick_) >= 500000000ULL) refresh();
    }

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touch,
        HidAnalogStickState left, HidAnalogStickState) override
    {
        constexpr s32 deadzone = 16000;
        u64 stick = 0;
        if (left.y > deadzone) stick |= HidNpadButton_Up;
        if (left.y < -deadzone) stick |= HidNpadButton_Down;
        u64 stick_down = stick & ~previous_stick_;
        previous_stick_ = stick;
        keysDown |= stick_down;

        bool touch_down = touch.x != 0 || touch.y != 0;
        bool touch_pressed = touch_down && !previous_touch_;
        s32 touch_x = static_cast<s32>(touch.x);
        if (touch_x > 400) touch_x -= 880;
        s32 touch_y = static_cast<s32>(touch.y);
        previous_touch_ = touch_down;

        if (keysDown & HidNpadButton_B) { tsl::Overlay::get()->close(); return true; }
        if ((keysDown & HidNpadButton_Minus) || (touch_pressed && point_in(touch_x, touch_y,
                draw_x_ + 12, draw_y_ + 470, draw_w_ - 24, 38))) {
            details_visible_ = !details_visible_;
            return true;
        }
        if (session_status_ == PTC_LAB_SESSION_INVALID) return true;
        if (waiting_) return true;

        bool primary_touch = touch_pressed && point_in(touch_x, touch_y,
            draw_x_ + 20, draw_y_ + 330, draw_w_ - 40, 52);
        bool restore_touch = touch_pressed && point_in(touch_x, touch_y,
            draw_x_ + 20, draw_y_ + 394, draw_w_ - 40, 44);
        if (session_status_ == PTC_LAB_SESSION_VALID &&
            std::strcmp(view_.state, "awaiting_observation") != 0 &&
            ((keysDown & HidNpadButton_X) || restore_touch)) {
            submit_empty("lab_session_restore");
            return true;
        }
        if (request_error_) {
            if ((keysDown & HidNpadButton_Y) || primary_touch) retry_last();
            return true;
        }
        if (std::strcmp(view_.state, "awaiting_observation") == 0) {
            int &selection = observation_step_ == 0 ? observation_ : runtime_effect_;
            const int count = observation_step_ == 0 ? 3 : 4;
            if (keysDown & HidNpadButton_Up) selection = selection == 0 ? count - 1 : selection - 1;
            if (keysDown & HidNpadButton_Down) selection = (selection + 1) % count;
            if (touch_pressed) {
                for (int index = 0; index < count; ++index) {
                    if (point_in(touch_x, touch_y, draw_x_ + 26, draw_y_ + 176 + index * 38,
                            draw_w_ - 52, 36)) selection = index;
                }
            }
            if ((keysDown & HidNpadButton_A) || primary_touch) {
                if (observation_step_ == 0) observation_step_ = 1;
                else submit_observation();
            }
            return true;
        }
        if (std::strcmp(view_.state, "restore_required") == 0 || std::strcmp(view_.state, "error") == 0) {
            if ((keysDown & HidNpadButton_A) || primary_touch) submit_empty("lab_session_restore");
            return true;
        }
        if (is_danger_phase() && std::strcmp(view_.state, "ready") == 0) {
            if (primary_touch) touch_hold_warning_ = true;
            if (keysHeld & HidNpadButton_A) {
                if (danger_hold_tick_ == 0) danger_hold_tick_ = armGetSystemTick();
                uint64_t elapsed = armTicksToNs(armGetSystemTick() - danger_hold_tick_);
                danger_progress_ = elapsed >= 2000000000ULL ? 100 : static_cast<int>(elapsed / 20000000ULL);
                if (elapsed >= 2000000000ULL) {
                    danger_hold_tick_ = 0;
                    danger_progress_ = 0;
                    touch_hold_warning_ = false;
                    submit_phase();
                }
            } else {
                danger_hold_tick_ = 0;
                danger_progress_ = 0;
            }
            return true;
        }
        if ((keysDown & HidNpadButton_A) || primary_touch) {
            if (std::strcmp(view_.state, "not_started") == 0) submit_start();
            else if (std::strcmp(view_.state, "ready") == 0) submit_phase();
            return true;
        }
        if (std::strcmp(view_.state, "not_started") == 0) {
            if (keysDown & HidNpadButton_Up) selected_mode_ = selected_mode_ == 0 ? 2 : selected_mode_ - 1;
            if (keysDown & HidNpadButton_Down) selected_mode_ = (selected_mode_ + 1) % 3;
            if (touch_pressed) {
                for (int index = 0; index < 3; ++index) {
                    if (point_in(touch_x, touch_y, draw_x_ + 26, draw_y_ + 178 + index * 46,
                            draw_w_ - 52, 40)) selected_mode_ = index;
                }
            }
            return true;
        }
        return false;
    }

private:
    void refresh()
    {
        PtcLabSessionView fresh{};
        session_status_ = load_session(&fresh);
        if (session_status_ == PTC_LAB_SESSION_VALID) view_ = fresh;
        else if (session_status_ == PTC_LAB_SESSION_MISSING) {
            view_ = PtcLabSessionView{};
            std::snprintf(view_.mode, sizeof(view_.mode), "restriction_quick");
            std::snprintf(view_.state, sizeof(view_.state), "not_started");
            std::snprintf(view_.restore_verdict, sizeof(view_.restore_verdict), "not_started");
            std::snprintf(view_.last_verdict, sizeof(view_.last_verdict), "pending");
            view_.required_phases = 1;
        }
        last_refresh_tick_ = armGetSystemTick();
    }

    void set_transport_error(PtcCompanionStatus status)
    {
        request_error_ = true;
        error_code_ = 0;
        std::snprintf(error_reason_, sizeof(error_reason_), "%s", ptc_companion_status_name(status));
        std::snprintf(error_message_, sizeof(error_message_), "%s", ptc_lab_transport_error_zh(status));
    }

    void submit_json(const char *type, const char *payload)
    {
        char request[512];
        char request_id[80];
        long long now = static_cast<long long>(std::time(nullptr));
        std::snprintf(request_id, sizeof(request_id), "lab-ui-%016llx",
            static_cast<unsigned long long>(randomGet64()));
        std::snprintf(request, sizeof(request),
            "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"created_at\":%lld,\"payload\":%s}\n",
            request_id, type, now, payload);
        std::snprintf(last_type_, sizeof(last_type_), "%s", type);
        std::snprintf(last_payload_, sizeof(last_payload_), "%s", payload);
        std::snprintf(last_request_id_, sizeof(last_request_id_), "%s", request_id);
        PtcCompanionStatus status = ptc_companion_transport_submit_json(transport_, request_id, request);
        waiting_ = status == PTC_COMPANION_OK;
        request_error_ = status != PTC_COMPANION_OK;
        last_transport_status_ = status;
        if (request_error_) set_transport_error(status);
        last_poll_tick_ = armGetSystemTick();
    }

    void submit_empty(const char *type) { submit_json(type, "{}"); }

    void retry_last()
    {
        char type[sizeof(last_type_)];
        char payload[sizeof(last_payload_)];
        if (!last_type_[0]) return;
        std::snprintf(type, sizeof(type), "%s", last_type_);
        std::snprintf(payload, sizeof(payload), "%s", last_payload_[0] ? last_payload_ : "{}");
        submit_json(type, payload);
    }

    void submit_start()
    {
        static const char *const modes[] = {"restriction_quick", "timer_activation_ab", "full"};
        const char *mode = modes[selected_mode_];
        char payload[80];
        std::snprintf(payload, sizeof(payload), "{\"mode\":\"%s\"}", mode);
        submit_json("lab_session_start", payload);
    }

    int current_phase_index() const
    {
        return std::strcmp(view_.mode, "restriction_quick") == 0 ? 5 : view_.next_phase;
    }

    bool is_danger_phase() const
    {
        const int phase = current_phase_index();
        if (std::strcmp(view_.mode, "restriction_quick") == 0) return true;
        if (std::strcmp(view_.mode, "timer_activation_ab") == 0)
            return phase == 3 || phase == 5;
        return phase == 5;
    }

    void submit_phase()
    {
        const int phase = current_phase_index();
        const bool activation_ab = std::strcmp(view_.mode, "timer_activation_ab") == 0;
        const int phase_count = activation_ab ? 7 : 6;
        if (phase < 0 || phase >= phase_count) return;
        char payload[96];
        std::snprintf(payload, sizeof(payload), "{\"phase\":\"%s\"}",
            activation_ab ? ACTIVATION_AB_PHASES[phase] : PHASES[phase]);
        submit_json("lab_phase_start", payload);
    }

    void submit_observation()
    {
        static const char *const values[] = {
            "restriction_visible", "no_visible_restriction", "unsure"
        };
        static const char *const effects[] = {
            "continued", "paused_or_suspended", "exited", "unsure"
        };
        char payload[160];
        std::snprintf(payload, sizeof(payload),
            "{\"observation\":\"%s\",\"runtime_effect\":\"%s\"}",
            values[observation_], effects[runtime_effect_]);
        submit_json("lab_observation", payload);
    }

    void draw_progress(tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 width)
    {
        const int completed = view_.next_phase;
        const int required = view_.required_phases > 0 ? view_.required_phases : 6;
        const s32 gap = 5;
        const s32 segment = (width - gap * (required - 1)) / required;
        for (int index = 0; index < required; ++index) {
            tsl::Color color = index < completed ? COLOR_GREEN :
                (index == completed && completed < required ? COLOR_BLUE : COLOR_PANEL_RAISED);
            renderer->drawRect(x + index * (segment + gap), y, segment, 8, renderer->a(color));
        }
    }

    void draw_button(tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h,
        const char *label, tsl::Color color, bool outlined = false)
    {
        renderer->drawRect(x, y, w, h, renderer->a(outlined ? COLOR_PANEL_RAISED : color));
        draw_outline(renderer, x, y, w, h, outlined ? 2 : 1, color);
        auto [text_width, unused] = renderer->drawString(label, false, 0, 0, 18,
            tsl::style::color::ColorTransparent);
        (void)unused;
        renderer->drawString(label, false, x + (w - static_cast<s32>(text_width)) / 2,
            y + 31, 18, renderer->a(outlined ? color : COLOR_TEXT));
    }

    void draw(tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h)
    {
        (void)h;
        draw_x_ = x;
        draw_y_ = y;
        draw_w_ = w;
        char line[320];
        const bool restore_state = std::strcmp(view_.state, "restore_required") == 0 ||
            std::strcmp(view_.state, "error") == 0;
        const tsl::Color state_color = session_status_ == PTC_LAB_SESSION_INVALID || restore_state
            ? COLOR_DANGER : (std::strcmp(view_.state, "complete") == 0 ? COLOR_GREEN : COLOR_BLUE);

        const int required = view_.required_phases > 0 ? view_.required_phases : 6;
        const int phase = current_phase_index();
        std::snprintf(line, sizeof(line), "%s / %d / %d",
            ptc_lab_mode_zh(view_.mode), view_.next_phase > required ? required : view_.next_phase, required);
        renderer->drawString(line, false, x + 20, y + 24, 16, renderer->a(COLOR_MUTED));
        renderer->drawString("PCTL 实机证据", false, x + w - 138, y + 24, 14, renderer->a(COLOR_DANGER));
        draw_progress(renderer, x + 20, y + 38, w - 40);

        draw_panel(renderer, x + 20, y + 60, w - 40, 74, state_color);
        renderer->drawString("当前状态", false, x + 34, y + 82, 13, renderer->a(COLOR_MUTED));
        renderer->drawString(session_status_ == PTC_LAB_SESSION_INVALID ? "会话文件损坏" :
            ptc_lab_session_state_zh(view_.state), false, x + 34, y + 112, 22, renderer->a(state_color));
        if (view_.last_verdict[0]) {
            draw_wrapped(renderer, ptc_lab_verdict_zh(view_.last_verdict), x + 20, y + 151,
                14, 26, 2, std::strcmp(view_.last_verdict, "unsafe_for_home_start") == 0 ? COLOR_AMBER : COLOR_MUTED);
        }

        if (session_status_ == PTC_LAB_SESSION_INVALID) {
            draw_panel(renderer, x + 20, y + 184, w - 40, 190, COLOR_DANGER);
            renderer->drawString("已停止取证", false, x + 36, y + 218, 23, renderer->a(COLOR_DANGER));
            draw_wrapped(renderer,
                "无法可信读取 session.json。为避免覆盖现场，不能开始新阶段。请退出浮窗，保留 SD 文件并运行 Device Lab NRO 查看详情。",
                x + 36, y + 260, 16, 23, 5, COLOR_TEXT);
        } else if (request_error_) {
            draw_panel(renderer, x + 20, y + 184, w - 40, 136, COLOR_DANGER);
            renderer->drawString("本次操作未成功", false, x + 36, y + 216, 21, renderer->a(COLOR_DANGER));
            draw_wrapped(renderer, error_message_, x + 36, y + 250, 15, 24, 3, COLOR_TEXT);
            draw_button(renderer, x + 20, y + 330, w - 40, 52, "Y / 触摸重试", COLOR_DANGER);
        } else if (std::strcmp(view_.state, "sampling") == 0) {
            long long remaining = view_.deadline - static_cast<long long>(std::time(nullptr));
            if (remaining < 0) remaining = 0;
            draw_panel(renderer, x + 20, y + 184, w - 40, 184, COLOR_BLUE);
            renderer->drawString(ptc_lab_phase_title_for_mode_zh(view_.mode, phase), false,
                x + 36, y + 218, 20, renderer->a(COLOR_TEXT));
            std::snprintf(line, sizeof(line), "自动采样剩余 %lld 秒", remaining);
            renderer->drawString(line, false, x + 36, y + 268, 28, renderer->a(COLOR_GREEN));
            draw_wrapped(renderer, "可以关闭本浮窗。实验后台会继续采样，并在期限到达时优先恢复原设置。",
                x + 36, y + 312, 15, 24, 3, COLOR_MUTED);
        } else if (std::strcmp(view_.state, "awaiting_observation") == 0) {
            static const char *const observations[] = {"看到了时间限制", "没有看到限制", "无法确定"};
            static const char *const effects[] = {"游戏仍可操作", "游戏已暂停或挂起", "游戏实际退出", "无法确定"};
            const char *const *labels = observation_step_ == 0 ? observations : effects;
            const int count = observation_step_ == 0 ? 3 : 4;
            const int selection = observation_step_ == 0 ? observation_ : runtime_effect_;
            renderer->drawString(observation_step_ == 0 ? "是否看到系统时间限制提示？" :
                "游戏实际发生了什么？", false, x + 24, y + 168, 18, renderer->a(COLOR_TEXT));
            for (int index = 0; index < count; ++index) {
                s32 row_y = y + 176 + index * 38;
                renderer->drawRect(x + 26, row_y, w - 52, 36,
                    renderer->a(index == selection ? COLOR_PANEL_RAISED : COLOR_PANEL));
                draw_outline(renderer, x + 26, row_y, w - 52, 36, index == selection ? 2 : 1,
                    index == selection ? COLOR_GREEN : COLOR_MUTED);
                renderer->drawString(labels[index], false, x + 42, row_y + 25, 16,
                    renderer->a(index == selection ? COLOR_GREEN : COLOR_TEXT));
            }
            draw_button(renderer, x + 20, y + 330, w - 40, 52,
                observation_step_ == 0 ? "下一步：记录实际行为" : "确认两项观察结果", COLOR_GREEN);
        } else if (std::strcmp(view_.state, "complete") == 0 && view_.restored) {
            draw_panel(renderer, x + 20, y + 184, w - 40, 186, COLOR_GREEN);
            renderer->drawString(view_.next_phase >= required ? "取证完成，原设置已恢复" :
                "会话已停止，原设置已恢复", false, x + 36, y + 220, 21, renderer->a(COLOR_GREEN));
            draw_wrapped(renderer, view_.next_phase >= required ?
                "请只发送下面这一份报告，然后运行 Device Lab NRO 恢复正常后台并重启主机。" :
                "本次取证提前结束，报告并不完整。请运行 Device Lab NRO 恢复正常后台并重启主机。",
                x + 36, y + 262, 15, 24, 3, COLOR_TEXT);
            std::snprintf(line, sizeof(line), "%s/reports/%s.json", PLAYWISE_SD_ROOT, view_.run_id);
            draw_wrapped(renderer, line, x + 36, y + 330, 13, 28, 2, COLOR_BLUE);
        } else if (restore_state) {
            draw_panel(renderer, x + 20, y + 184, w - 40, 136, COLOR_DANGER);
            renderer->drawString("停止：恢复尚未得到证明", false, x + 36, y + 218, 21, renderer->a(COLOR_DANGER));
            draw_wrapped(renderer, "后续写入已经被禁止。请立即重试恢复，成功前不要切换正常后台。",
                x + 36, y + 258, 16, 24, 3, COLOR_TEXT);
            draw_button(renderer, x + 20, y + 330, w - 40, 52, "A / 触摸立即恢复", COLOR_DANGER);
        } else {
            const bool danger_phase = is_danger_phase();
            draw_panel(renderer, x + 20, y + 184, w - 40, 136, danger_phase ? COLOR_DANGER : COLOR_BLUE);
            if (std::strcmp(view_.state, "not_started") == 0) {
                static const char *const modes[] = {
                    "聚焦限制复测（推荐）", "Timer 激活 A/B（七阶段）", "高级完整取证（六阶段）"
                };
                renderer->drawString("选择本次取证模式", false, x + 36, y + 166, 20, renderer->a(COLOR_TEXT));
                for (int index = 0; index < 3; ++index) {
                    s32 row_y = y + 178 + index * 46;
                    renderer->drawRect(x + 26, row_y, w - 52, 40,
                        renderer->a(index == selected_mode_ ? COLOR_PANEL_RAISED : COLOR_PANEL));
                    draw_outline(renderer, x + 26, row_y, w - 52, 40, index == selected_mode_ ? 2 : 1,
                        index == selected_mode_ ? COLOR_GREEN : COLOR_MUTED);
                    renderer->drawString(modes[index], false, x + 42, row_y + 27, 15,
                        renderer->a(index == selected_mode_ ? COLOR_GREEN : COLOR_TEXT));
                }
                draw_button(renderer, x + 20, y + 330, w - 40, 52, "开始并保存 PCTL 原设置", COLOR_BLUE);
            } else {
                renderer->drawString(ptc_lab_phase_title_for_mode_zh(view_.mode, phase), false,
                    x + 36, y + 218, 20, renderer->a(danger_phase ? COLOR_DANGER : COLOR_TEXT));
                const bool weak_lifecycle_baseline = std::strcmp(view_.mode, "full") == 0 &&
                    view_.baseline_all_zero && phase >= 2 && phase <= 4;
                draw_wrapped(renderer, weak_lifecycle_baseline ?
                    "原始 0x44 全零：本阶段可继续采样，但不足以证明游戏生命周期。完整复测应先配置非空 Nintendo 周计划。" :
                    ptc_lab_phase_instruction_for_mode_zh(view_.mode, phase), x + 36, y + 254,
                    15, 24, 3, COLOR_MUTED);
                draw_button(renderer, x + 20, y + 330, w - 40, 52,
                    danger_phase ? (touch_hold_warning_ ? "请使用手柄长按 A" : "长按 A 两秒开始") :
                        "A / 触摸开始", danger_phase ? COLOR_DANGER : COLOR_BLUE);
            }
            if (danger_phase && std::strcmp(view_.state, "not_started") != 0) {
                renderer->drawRect(x + 20, y + 384, w - 40, 8, renderer->a(COLOR_PANEL_RAISED));
                if (danger_progress_ > 0) renderer->drawRect(x + 20, y + 384,
                    (w - 40) * danger_progress_ / 100, 8, renderer->a(COLOR_DANGER));
            }
        }

        if (std::strcmp(view_.state, "complete") != 0 &&
            std::strcmp(view_.state, "awaiting_observation") != 0 &&
            session_status_ == PTC_LAB_SESSION_VALID)
            draw_button(renderer, x + 20, y + 394, w - 40, 44, "X / 触摸：立即恢复原设置", COLOR_DANGER, true);
        if (waiting_) renderer->drawString("正在提交并等待后台结果...", false, x + 24, y + 466, 15, renderer->a(COLOR_GREEN));

        renderer->drawString(details_visible_ ? "[-] 收起技术详情" : "[-] 展开技术详情", false,
            x + 20, y + 492, 14, renderer->a(COLOR_MUTED));
        if (details_visible_) {
            std::snprintf(line, sizeof(line), "mode=%s  state=%s\nphase=%s  restore=%s\nrequest=%s\nstatus=%s  code=%d",
                view_.mode, view_.state, view_.active_phase[0] ? view_.active_phase : "none", view_.restore_verdict,
                last_request_id_[0] ? last_request_id_ : "none",
                ptc_companion_status_name(last_transport_status_), error_code_);
            draw_wrapped(renderer, line, x + 20, y + 516, 12, 34, 5, COLOR_MUTED);
        } else {
            renderer->drawString("B 关闭浮窗", false, x + w - 112, y + 492, 14, renderer->a(COLOR_MUTED));
        }
    }

    PtcCompanionTransportClient *transport_;
    PtcLabSessionView view_{};
    PtcLabSessionLoadStatus session_status_ = PTC_LAB_SESSION_MISSING;
    bool waiting_ = false;
    bool request_error_ = false;
    bool details_visible_ = false;
    bool previous_touch_ = false;
    bool touch_hold_warning_ = false;
    int observation_ = 0;
    int runtime_effect_ = 3;
    int observation_step_ = 0;
    int selected_mode_ = 0;
    int danger_progress_ = 0;
    int error_code_ = 0;
    PtcCompanionStatus last_transport_status_ = PTC_COMPANION_OK;
    u64 danger_hold_tick_ = 0;
    u64 last_poll_tick_ = 0;
    u64 last_refresh_tick_ = 0;
    u64 previous_stick_ = 0;
    s32 draw_x_ = 0;
    s32 draw_y_ = 0;
    s32 draw_w_ = 448;
    char error_message_[256]{};
    char error_reason_[64]{};
    char last_type_[48]{};
    char last_payload_[192]{};
    char last_request_id_[80]{};
};

class LabOverlay final : public tsl::Overlay {
public:
    void initServices() override
    {
        fsdevMountSdmc();
        ptc_fs_storage_init(&storage_);
        /* Lab requests must remain recoverable even when pwtl:u is not yet
           running. Use the durable SD queue from the first overlay frame and
           let the Lab sysmodule claim requests after the required reboot. */
        ptc_companion_transport_init(&transport_, PLAYWISE_SD_ROOT,
            ptc_fs_storage_as_storage(&storage_), nullptr, nullptr);
    }

    void exitServices() override
    {
        ptc_companion_transport_cancel(&transport_);
        fsdevUnmountDevice("sdmc");
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<LabGui>(&transport_); }

private:
    PtcFsStorage storage_{};
    PtcCompanionTransportClient transport_{};
};

} // namespace

int main(int argc, char **argv)
{
    return tsl::loop<LabOverlay>(argc, argv);
}
