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

constexpr char APP_ROOT[] = "sdmc:/switch/playwise";
constexpr tsl::Color PANEL_COLOR{ 0x1, 0x1, 0x2, 0xEE };
constexpr tsl::Color CARD_COLOR{ 0x2, 0x2, 0x3, 0xFF };
constexpr tsl::Color KEY_COLOR{ 0x2, 0x2, 0x3, 0xFF };
constexpr tsl::Color FOCUS_BG{ 0x0, 0x9, 0xC, 0xFF };
constexpr tsl::Color FOCUS_BORDER{ 0x0, 0xF, 0xF, 0xFF };
constexpr tsl::Color TEXT_COLOR{ 0xF, 0xF, 0xF, 0xFF };
constexpr tsl::Color DARK_TEXT_COLOR{ 0x0, 0x1, 0x1, 0xFF };
constexpr tsl::Color MUTED_COLOR{ 0x8, 0x8, 0x9, 0xFF };
constexpr tsl::Color DISABLED_COLOR{ 0x2, 0x2, 0x2, 0xFF };
constexpr tsl::Color SUCCESS_COLOR{ 0x2, 0xE, 0x6, 0xFF };
constexpr tsl::Color ERROR_COLOR{ 0xF, 0x3, 0x3, 0xFF };

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
    if (keys & HidNpadButton_Minus) result |= PTC_OVERLAY_BUTTON_MINUS;
    if (keys & HidNpadButton_L) result |= PTC_OVERLAY_BUTTON_L;
    if (keys & HidNpadButton_R) result |= PTC_OVERLAY_BUTTON_R;
    return result;
}

class PctcGui final : public tsl::Gui {
public:
    PctcGui(PtcOverlayBridge *bridge, PtcOverlayInput *input) : bridge_(bridge), input_(input) {}

    tsl::elm::Element *createUI() override
    {
        auto frame = new tsl::elm::OverlayFrame("任你玩", "今日加时");
        frame->setContent(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            draw_overlay(renderer, x, y, w, h);
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
                status_expanded_ = true;
            } else {
                error_ = true;
                status_expanded_ = true;
            }
        } else {
            error_ = true;
            status_expanded_ = true;
        }
    }

    bool handleInput(
        u64 keysDown,
        u64 keysHeld,
        const HidTouchState &touch,
        HidAnalogStickState left,
        HidAnalogStickState right) override
    {
        (void)right;
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

        // 1. 摇杆死区处理 (Analog Stick Support)
        constexpr s32 STICK_DEADZONE = 16000;
        u64 stick_keys = 0;
        if (left.x > STICK_DEADZONE)  stick_keys |= HidNpadButton_Right;
        if (left.x < -STICK_DEADZONE) stick_keys |= HidNpadButton_Left;
        if (left.y > STICK_DEADZONE)  stick_keys |= HidNpadButton_Up;
        if (left.y < -STICK_DEADZONE) stick_keys |= HidNpadButton_Down;

        u64 stick_down = stick_keys & ~prev_stick_keys_;
        prev_stick_keys_ = stick_keys;

        keysDown |= stick_down;
        keysHeld |= stick_keys;

        // 2. 触屏交互处理 (Touch Screen Support)
        bool touch_down = (touch.x != 0 || touch.y != 0);
        if (touch_down && !prev_touch_down_) {
            s32 tx = static_cast<s32>(touch.x);
            s32 ty = static_cast<s32>(touch.y);
            s32 rel_x = tx > 400 ? (tx - 880) : tx;
            s32 rel_y = ty;

            constexpr s32 cx = 35;
            constexpr s32 cy = 125;
            constexpr s32 panel_y = cy + 115; // 240

            // 点击小键盘按键
            const char *charset = ptc_overlay_input_charset();
            for (unsigned int index = 0; index < PTC_OVERLAY_KEY_COUNT; ++index) {
                unsigned int row = index == 0 ? 3u : (index - 1u) / 3u;
                unsigned int col = index == 0 ? 1u : (index - 1u) % 3u;
                s32 kx = cx + 15 + static_cast<s32>(col) * 100;
                s32 ky = panel_y + 10 + static_cast<s32>(row) * 42;
                if (rel_x >= kx && rel_x <= kx + 85 && rel_y >= ky && rel_y <= ky + 34) {
                    input_->cursor = index;
                    if (input_->length < PTC_OVERLAY_CODE_SYMBOLS) {
                        input_->symbols[input_->length++] = charset[index];
                        input_->symbols[input_->length] = '\0';
                    }
                    prev_touch_down_ = touch_down;
                    return true;
                }
            }

            // 点击 [X] 退格 (第四行左侧)
            s32 x_btn_x = cx + 15;
            s32 btn_y = panel_y + 10 + 3 * 42;
            if (rel_x >= x_btn_x && rel_x <= x_btn_x + 85 && rel_y >= btn_y && rel_y <= btn_y + 34) {
                (void)ptc_overlay_input_handle(input_, PTC_OVERLAY_BUTTON_X, 0, 0);
                prev_touch_down_ = touch_down;
                return true;
            }

            // 点击 [Y] 清空 (第四行右侧)
            s32 y_btn_x = cx + 215;
            if (rel_x >= y_btn_x && rel_x <= y_btn_x + 85 && rel_y >= btn_y && rel_y <= btn_y + 34) {
                (void)ptc_overlay_input_handle(input_, PTC_OVERLAY_BUTTON_Y, 0, 0);
                prev_touch_down_ = touch_down;
                return true;
            }

            // 点击 [+] 提交按钮
            s32 submit_y = cy + 305;
            if (rel_x >= cx && rel_x <= cx + 315 && rel_y >= submit_y && rel_y <= submit_y + 38) {
                if (ptc_overlay_input_can_submit(input_)) {
                    char code[32];
                    if (ptc_overlay_input_format(input_, code, sizeof(code))) {
                        PtcCompanionStatus status = ptc_overlay_bridge_submit(bridge_, code, static_cast<int64_t>(std::time(nullptr)), ++request_nonce_);
                        if (status == PTC_COMPANION_OK) {
                            request_started_tick_ = armGetSystemTick();
                            last_elapsed_ms_ = 0;
                            status_expanded_ = true;
                        } else {
                            error_ = true;
                            status_expanded_ = true;
                        }
                    }
                }
                prev_touch_down_ = touch_down;
                return true;
            }

            // 点击状态与命令栏
            s32 status_y = cy + 350;
            if (rel_x >= cx && rel_x <= cx + 315 && rel_y >= status_y && rel_y <= status_y + 160) {
                status_expanded_ = !status_expanded_;
                prev_touch_down_ = touch_down;
                return true;
            }
        }
        prev_touch_down_ = touch_down;

        // 3. 物理按键处理 (Button Handler)
        if (keysDown & HidNpadButton_B) {
            tsl::Overlay::get()->close();
            return true;
        }

        if (keysDown & (HidNpadButton_Minus | HidNpadButton_L | HidNpadButton_R)) {
            status_expanded_ = !status_expanded_;
            return true;
        }

        if (keysDown & HidNpadButton_Plus) {
            char code[32];
            if (ptc_overlay_input_can_submit(input_) && ptc_overlay_input_format(input_, code, sizeof(code))) {
                PtcCompanionStatus status = ptc_overlay_bridge_submit(bridge_, code, static_cast<int64_t>(std::time(nullptr)), ++request_nonce_);
                if (status == PTC_COMPANION_OK) {
                    request_started_tick_ = armGetSystemTick();
                    last_elapsed_ms_ = 0;
                    status_expanded_ = true;
                } else {
                    error_ = true;
                    status_expanded_ = true;
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

    static const char *transport_stage(const PtcOverlayBridge *bridge)
    {
        if (bridge->waiting) {
            if (ptc_overlay_bridge_transport_state(bridge) == PTC_TRANSPORT_ROUTE_IPC_SD_RESULT)
                return "正在读取后台结果…";
            return "正在等待后台处理…";
        }
        return "";
    }

    void draw_overlay(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw, s32 ch)
    {
        (void)ch;
        char line[128];

        // --- 1. Header Prompt ---
        renderer->drawString("输入 8 位加时密码", false, cx + 5, cy + 18, 18, renderer->a(TEXT_COLOR));
        renderer->drawString("方向盘/触屏选择  A/点击输入  +:提交", false, cx + 5, cy + 36, 12, renderer->a(MUTED_COLOR));

        // --- 2. Code Display Slots (8位卡片槽) ---
        const s32 slot_start_x = cx + 12;
        const s32 slot_y = cy + 48;
        const s32 slot_w = 32;
        const s32 slot_h = 38;
        const s32 slot_gap = 5;

        for (unsigned int index = 0; index < PTC_OVERLAY_CODE_SYMBOLS; ++index) {
            const s32 sx = slot_start_x + static_cast<s32>(index) * (slot_w + slot_gap);
            const bool is_cursor = (input_->length < PTC_OVERLAY_CODE_SYMBOLS) && (index == input_->length);

            // 卡片背景与边框
            renderer->drawRect(sx, slot_y, slot_w, slot_h, renderer->a(CARD_COLOR));
            draw_outline(renderer, sx, slot_y, slot_w, slot_h, is_cursor ? 3 : 1, is_cursor ? FOCUS_BORDER : MUTED_COLOR);

            // 文本字符或未输入指示
            char symbol[8] = {0};
            if (index < input_->length) {
                symbol[0] = input_->symbols[index];
                symbol[1] = '\0';
            } else if (is_cursor) {
                symbol[0] = '_';
                symbol[1] = '\0';
            } else {
                std::snprintf(symbol, sizeof(symbol), "·");
            }
            renderer->drawString(
                symbol,
                false,
                sx + (index < input_->length ? 10 : (is_cursor ? 10 : 12)),
                slot_y + 26,
                20,
                renderer->a(index < input_->length ? TEXT_COLOR : (is_cursor ? FOCUS_BORDER : MUTED_COLOR)));
        }

        std::snprintf(line, sizeof(line), "已输入 %u/8 位   当前高亮数字：%c", input_->length, ptc_overlay_input_charset()[input_->cursor]);
        renderer->drawString(line, false, cx + 5, cy + 102, 13, renderer->a(MUTED_COLOR));

        // --- 3. Keypad 3x4 Grid (软键盘) ---
        const char *charset = ptc_overlay_input_charset();
        const s32 panel_y = cy + 115;
        const s32 panel_h = 184;
        renderer->drawRect(cx, panel_y, cw, panel_h, renderer->a(PANEL_COLOR));
        draw_outline(renderer, cx, panel_y, cw, panel_h, 1, MUTED_COLOR);

        // 绘制数字键 0-9
        for (unsigned int index = 0; index < PTC_OVERLAY_KEY_COUNT; ++index) {
            char symbol[2] = { charset[index], '\0' };
            unsigned int row = index == 0 ? 3u : (index - 1u) / 3u;
            unsigned int col = index == 0 ? 1u : (index - 1u) % 3u;
            s32 key_x = cx + 15 + static_cast<s32>(col) * 100;
            s32 key_y = panel_y + 10 + static_cast<s32>(row) * 42;
            const bool focused = (index == input_->cursor);

            renderer->drawRect(key_x, key_y, 85, 34, renderer->a(focused ? FOCUS_BG : KEY_COLOR));
            draw_outline(renderer, key_x, key_y, 85, 34, focused ? 3 : 1, focused ? FOCUS_BORDER : MUTED_COLOR);
            renderer->drawString(symbol, false, key_x + 36, key_y + 24, 20, renderer->a(focused ? FOCUS_BORDER : TEXT_COLOR));
        }

        // 第四行辅助按键 [X] 退格 和 [Y] 清空 (方便触屏与按键快捷操作)
        s32 x_btn_x = cx + 15;
        s32 btn_y = panel_y + 10 + 3 * 42;
        renderer->drawRect(x_btn_x, btn_y, 85, 34, renderer->a(KEY_COLOR));
        draw_outline(renderer, x_btn_x, btn_y, 85, 34, 1, MUTED_COLOR);
        renderer->drawString("X 退格", false, x_btn_x + 20, btn_y + 22, 13, renderer->a(MUTED_COLOR));

        s32 y_btn_x = cx + 215;
        renderer->drawRect(y_btn_x, btn_y, 85, 34, renderer->a(KEY_COLOR));
        draw_outline(renderer, y_btn_x, btn_y, 85, 34, 1, MUTED_COLOR);
        renderer->drawString("Y 清空", false, y_btn_x + 20, btn_y + 22, 13, renderer->a(MUTED_COLOR));

        // --- 4. Control & Submit Bar (操作与提交栏) ---
        const bool can_submit = ptc_overlay_input_can_submit(input_);
        const s32 submit_y = cy + 305;
        const s32 submit_h = 38;

        // 提交加时大按钮
        renderer->drawRect(cx, submit_y, cw, submit_h, renderer->a(can_submit ? FOCUS_BG : DISABLED_COLOR));
        draw_outline(renderer, cx, submit_y, cw, submit_h, can_submit ? 3 : 1, can_submit ? FOCUS_BORDER : MUTED_COLOR);

        if (can_submit) {
            renderer->drawString("+ 提交今日加时 (点击或按 + 键)", false, cx + 45, submit_y + 25, 15, renderer->a(TEXT_COLOR));
        } else {
            renderer->drawString("+ 提交加时 (需输满 8 位数字)", false, cx + 55, submit_y + 25, 14, renderer->a(MUTED_COLOR));
        }

        // --- 5. Collapsible Status Panel (可折叠命令与状态栏) ---
        const s32 status_y = cy + 350;
        const s32 status_w = cw;

        if (!status_expanded_) {
            // 折叠状态 (Collapsed - 薄条形式)
            renderer->drawRect(cx, status_y, status_w, 34, renderer->a(PANEL_COLOR));
            draw_outline(renderer, cx, status_y, status_w, 34, 1, MUTED_COLOR);

            if (bridge_->waiting) {
                renderer->drawString("[-] 状态：正在处理加时… (按 - 展开 ∨)", false, cx + 12, status_y + 22, 13, renderer->a(FOCUS_BORDER));
            } else if (error_) {
                renderer->drawString("[-] 状态：加时错误 (按 - 展开 ∨)", false, cx + 12, status_y + 22, 13, renderer->a(ERROR_COLOR));
            } else if (close_after_frames_ > 0) {
                renderer->drawString("[-] 状态：加时成功！ (按 - 展开 ∨)", false, cx + 12, status_y + 22, 13, renderer->a(SUCCESS_COLOR));
            } else {
                renderer->drawString("[-] 命令与状态 (点击或按 - / L / R 展开 ∨)", false, cx + 12, status_y + 22, 13, renderer->a(MUTED_COLOR));
            }
        } else {
            // 展开状态 (Expanded - 详细卡片)
            const s32 expanded_h = 160;
            renderer->drawRect(cx, status_y, status_w, expanded_h, renderer->a(PANEL_COLOR));
            draw_outline(renderer, cx, status_y, status_w, expanded_h, 2, FOCUS_BORDER);

            renderer->drawString("[-] 命令与状态详情 (点击或按 - 收起 ∧)", false, cx + 12, status_y + 20, 13, renderer->a(FOCUS_BORDER));

            if (bridge_->request_id[0]) {
                renderer->drawString(
                    bridge_->waiting ? "当前命令：提交今日加时" : "最近命令：提交今日加时",
                    false,
                    cx + 12,
                    status_y + 40,
                    13,
                    renderer->a(TEXT_COLOR));
            } else {
                renderer->drawString("当前命令：未开始", false, cx + 12, status_y + 40, 13, renderer->a(TEXT_COLOR));
            }

            renderer->drawString(ptc_overlay_bridge_transport_label(bridge_), false, cx + 12, status_y + 58, 12, renderer->a(MUTED_COLOR));

            const char *stage = transport_stage(bridge_);
            if (stage[0]) {
                renderer->drawString(stage, false, cx + 12, status_y + 78, 13, renderer->a(FOCUS_BORDER));
            }

            if (error_) {
                const char *message = ptc_overlay_bridge_error_message_zh(bridge_);
                renderer->drawString(message, false, cx + 12, status_y + 78, 13, renderer->a(ERROR_COLOR), 290);
                if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                    std::snprintf(line, sizeof(line), "错误码：%d   (按 Y 重试)", bridge_->summary.error_code);
                    renderer->drawString(line, false, cx + 12, status_y + 120, 12, renderer->a(ERROR_COLOR));
                } else {
                    renderer->drawString("按 Y 重试", false, cx + 12, status_y + 120, 12, renderer->a(ERROR_COLOR));
                }
            }

            if (close_after_frames_ > 0) {
                std::snprintf(line, sizeof(line), "加时成功！修改后还可玩 %d 分钟", bridge_->summary.remaining_minutes);
                renderer->drawString(line, false, cx + 12, status_y + 78, 14, renderer->a(SUCCESS_COLOR));
                if (bridge_->summary.played_minutes_available) {
                    std::snprintf(line, sizeof(line), "今日已玩约 %d 分钟，即刻刷新生效…", bridge_->summary.played_minutes);
                } else {
                    std::snprintf(line, sizeof(line), "状态已实时刷新，即将自动关闭…");
                }
                renderer->drawString(line, false, cx + 12, status_y + 100, 12, renderer->a(SUCCESS_COLOR));
            }
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
    bool status_expanded_ = false;
    bool prev_touch_down_ = false;
    u64 prev_stick_keys_ = 0;
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
