#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

#include "release_manifest.h"
#include "../layout.h"

extern "C" {
#include "../bridge.h"
#include "../input_model.h"
#include "platform/switch/fs_storage.h"
}

namespace {

[[gnu::used]] constexpr char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

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
constexpr tsl::Color WAITING_COLOR{ 0xF, 0xA, 0x2, 0xFF };

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

enum class OverlayRequestKind {
    None,
    Status,
    PreviewOfflineCode,
    OfflineCode,
};

class PlayWiseOverlayFrame final : public tsl::elm::OverlayFrame {
public:
    PlayWiseOverlayFrame(const std::string &title, const std::string &subtitle)
        : tsl::elm::OverlayFrame(title, subtitle) {}

    void layout(u16 parent_x, u16 parent_y, u16 parent_width, u16 parent_height) override
    {
        setBoundaries(parent_x, parent_y, parent_width, parent_height);
        if (m_contentElement != nullptr) {
            m_contentElement->setBoundaries(
                parent_x + PTC_OVERLAY_CONTENT_X,
                parent_y + PTC_OVERLAY_CONTENT_Y,
                parent_width - 85,
                parent_height - 73 - PTC_OVERLAY_CONTENT_Y);
            m_contentElement->invalidate();
        }
    }
};

class PctcGui final : public tsl::Gui {
public:
    PctcGui(PtcOverlayBridge *bridge, PtcOverlayInput *input) : bridge_(bridge), input_(input) {}

    tsl::elm::Element *createUI() override
    {
        auto frame = new PlayWiseOverlayFrame("自律约定", "兑换加时奖励");
        if (!restore_pending_redemption()) {
            (void)begin_status_refresh();
        }
        frame->setContent(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            draw_overlay(renderer, x, y, w, h);
        }));
        return frame;
    }

    void update() override
    {
        if (recovery_active_) {
            poll_pending_redemption();
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
            if (ptc_overlay_bridge_status_succeeded(bridge_)) {
                displayed_summary_ = bridge_->summary;
                last_refresh_tick_ = armGetSystemTick();
                has_status_snapshot_ = true;
                error_ = false;
            } else if (ptc_overlay_bridge_preview_succeeded(bridge_)) {
                displayed_summary_ = bridge_->summary;
                last_refresh_tick_ = armGetSystemTick();
                has_status_snapshot_ = true;
                error_ = false;
                const bool after_zero = bridge_->summary.remaining_after_available &&
                    bridge_->summary.remaining_after_minutes == 0;
                const bool material_change = preview_recheck_ &&
                    (previous_after_available_ != bridge_->summary.remaining_after_available ||
                     previous_after_zero_ != after_zero ||
                     previous_capped_ != bridge_->summary.preview_capped ||
                     previous_converts_unlimited_ != bridge_->summary.converts_unlimited_to_limited);
                preview_summary_ = bridge_->summary;
                preview_recheck_ = false;
                if (!material_change && active_request_kind_ == OverlayRequestKind::PreviewOfflineCode &&
                    awaiting_confirm_recheck_) {
                    awaiting_confirm_recheck_ = false;
                    redemption_before_ = bridge_->summary;
                    (void)begin_actual_code_submit();
                } else {
                    awaiting_confirm_recheck_ = false;
                    preview_changed_ = material_change;
                    preview_ready_ = true;
                }
            } else if (ptc_overlay_bridge_offline_code_succeeded(bridge_)) {
                displayed_summary_ = bridge_->summary;
                last_refresh_tick_ = armGetSystemTick();
                has_status_snapshot_ = true;
                error_ = false;
                success_visible_ = true;
                result_pending_ = false;
                result_failed_ = false;
                preview_ready_ = false;
                pending_code_[0] = '\0';
                ptc_overlay_input_init(input_);
                status_expanded_ = true;
            } else if (active_request_kind_ == OverlayRequestKind::OfflineCode &&
                       bridge_->summary.valid && strcmp(bridge_->summary.type, "offline_code") == 0) {
                displayed_summary_ = bridge_->summary;
                error_ = false;
                success_visible_ = true;
                result_pending_ = false;
                result_failed_ = true;
                preview_ready_ = false;
                pending_code_[0] = '\0';
                ptc_overlay_input_init(input_);
                status_expanded_ = true;
            } else {
                error_ = true;
                preview_ready_ = false;
                awaiting_confirm_recheck_ = false;
                preview_recheck_ = false;
                pending_code_[0] = '\0';
                ptc_overlay_input_init(input_);
                status_expanded_ = true;
            }
        } else if (active_request_kind_ == OverlayRequestKind::OfflineCode) {
            ptc_companion_transport_cancel(&bridge_->transport);
            recovery_active_ = true;
            result_pending_ = true;
            result_failed_ = false;
            success_visible_ = true;
            error_ = false;
            preview_ready_ = false;
            pending_code_[0] = '\0';
            ptc_overlay_input_init(input_);
        } else {
            error_ = true;
            preview_ready_ = false;
            awaiting_confirm_recheck_ = false;
            preview_recheck_ = false;
            pending_code_[0] = '\0';
            ptc_overlay_input_init(input_);
            status_expanded_ = true;
        }
        if (!bridge_->waiting) active_request_kind_ = OverlayRequestKind::None;
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
        const bool request_actions_enabled = ptc_overlay_request_action_enabled(bridge_->waiting);

        if (success_visible_) {
            const bool touch_down = touch.x != 0 || touch.y != 0;
            if ((touch_down && !prev_touch_down_) ||
                (keysDown & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))) {
                const bool terminal = !result_pending_;
                success_visible_ = false;
                status_expanded_ = false;
                ptc_overlay_input_init(input_);
                if (terminal) {
                    (void)ptc_companion_pending_redemption_clear(&bridge_->transport.file);
                    std::memset(&pending_redemption_, 0, sizeof(pending_redemption_));
                    result_failed_ = false;
                }
            }
            prev_touch_down_ = touch_down;
            return true;
        }

        if (preview_ready_) {
            const bool touch_down = touch.x != 0 || touch.y != 0;
            bool touch_confirm = false;
            bool touch_cancel = false;
            if (touch_down && !prev_touch_down_) {
                const s32 rel_x = touch.x > 400 ? static_cast<s32>(touch.x) - 880 : static_cast<s32>(touch.x);
                const s32 rel_y = static_cast<s32>(touch.y);
                touch_cancel = rel_x >= PTC_OVERLAY_CONTENT_X && rel_x < PTC_OVERLAY_CONTENT_X + 145 &&
                    rel_y >= PTC_OVERLAY_CONTENT_Y + 500 && rel_y < PTC_OVERLAY_CONTENT_Y + 550;
                touch_confirm = rel_x >= PTC_OVERLAY_CONTENT_X + 170 && rel_x < PTC_OVERLAY_CONTENT_X + 330 &&
                    rel_y >= PTC_OVERLAY_CONTENT_Y + 500 && rel_y < PTC_OVERLAY_CONTENT_Y + 550;
            }
            const bool dangerous = !preview_summary_.remaining_after_available ||
                preview_summary_.remaining_after_minutes == 0 ||
                preview_summary_.converts_unlimited_to_limited;
            if ((keysDown & HidNpadButton_B) || touch_cancel) {
                preview_ready_ = false;
                preview_changed_ = false;
                pending_code_[0] = '\0';
                confirm_hold_ms_ = 0;
                ptc_overlay_input_init(input_);
            } else if (touch_confirm && dangerous) {
                touch_hold_warning_ = true;
            } else if ((!dangerous && ((keysDown & (HidNpadButton_A | HidNpadButton_Plus)) || touch_confirm))) {
                touch_hold_warning_ = false;
                (void)begin_preview_request(true);
            } else if (dangerous && (keysHeld & HidNpadButton_A)) {
                confirm_hold_ms_ += input_elapsed_ms > 0 ? input_elapsed_ms : 0;
                if (confirm_hold_ms_ >= 1000) {
                    confirm_hold_ms_ = 0;
                    touch_hold_warning_ = false;
                    (void)begin_preview_request(true);
                }
            } else {
                confirm_hold_ms_ = 0;
            }
            prev_touch_down_ = touch_down;
            return true;
        }

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

            constexpr s32 cx = PTC_OVERLAY_CONTENT_X;
            constexpr s32 cy = PTC_OVERLAY_CONTENT_Y;

            // 顶部刷新按钮；后台忙碌时只禁用新的请求，本地编辑仍可继续。
            if (ptc_overlay_rect_contains(ptc_overlay_refresh_rect(cx, cy), rel_x, rel_y)) {
                if (request_actions_enabled) (void)begin_status_refresh();
                prev_touch_down_ = touch_down;
                return true;
            }

            // 点击小键盘按键
            const char *charset = ptc_overlay_input_charset();
            for (unsigned int index = 0; index < PTC_OVERLAY_KEY_COUNT; ++index) {
                if (ptc_overlay_rect_contains(ptc_overlay_key_rect(cx, cy, index), rel_x, rel_y)) {
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
            if (ptc_overlay_rect_contains(ptc_overlay_backspace_rect(cx, cy), rel_x, rel_y)) {
                (void)ptc_overlay_input_handle(input_, PTC_OVERLAY_BUTTON_X, 0, 0);
                prev_touch_down_ = touch_down;
                return true;
            }

            // 点击清空（物理 Y 保留给状态刷新）。
            if (ptc_overlay_rect_contains(ptc_overlay_clear_rect(cx, cy), rel_x, rel_y)) {
                (void)ptc_overlay_input_handle(input_, PTC_OVERLAY_BUTTON_Y, 0, 0);
                prev_touch_down_ = touch_down;
                return true;
            }

            // 点击 [+] 提交按钮
            if (ptc_overlay_rect_contains(ptc_overlay_submit_rect(cx, cy, PTC_OVERLAY_CONTENT_W), rel_x, rel_y)) {
                if (request_actions_enabled) (void)begin_code_preview();
                prev_touch_down_ = touch_down;
                return true;
            }

            // 点击状态与命令栏
            if (ptc_overlay_rect_contains(
                    ptc_overlay_status_rect(cx, cy, PTC_OVERLAY_CONTENT_W, status_expanded_, status_needs_detail()),
                    rel_x, rel_y)) {
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
            if (request_actions_enabled) (void)begin_code_preview();
            return true;
        }

        if (keysDown & HidNpadButton_Y) {
            if (error_) {
                if (last_request_kind_ == OverlayRequestKind::OfflineCode ||
                    last_request_kind_ == OverlayRequestKind::PreviewOfflineCode) {
                    error_ = false;
                    status_expanded_ = false;
                    pending_code_[0] = '\0';
                    ptc_overlay_input_init(input_);
                } else {
                    (void)retry_last_request();
                }
            } else {
                if (request_actions_enabled) (void)begin_status_refresh();
            }
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

    void apply_pending_preview()
    {
        preview_summary_ = {};
        preview_summary_.valid = true;
        preview_summary_.ok = true;
        preview_summary_.preview_available = true;
        preview_summary_.grant_minutes = pending_redemption_.grant_minutes;
        preview_summary_.remaining_available = pending_redemption_.before_remaining_available;
        preview_summary_.remaining_minutes = pending_redemption_.before_remaining_minutes;
        preview_summary_.remaining_after_available = pending_redemption_.after_remaining_available;
        preview_summary_.remaining_after_minutes = pending_redemption_.after_remaining_minutes;
        preview_summary_.effective_add_minutes = pending_redemption_.effective_add_minutes;
        preview_summary_.preview_capped = pending_redemption_.capped;
        preview_summary_.converts_unlimited_to_limited = pending_redemption_.converts_unlimited_to_limited;
        redemption_before_ = preview_summary_;
    }

    bool restore_pending_redemption()
    {
        bool found = false;
        PtcCompanionStatus status = ptc_companion_pending_redemption_load(
            &bridge_->transport.file, &pending_redemption_, &found);
        if (status != PTC_COMPANION_OK) {
            if (!found) return false;
            std::memset(&bridge_->summary, 0, sizeof(bridge_->summary));
            bridge_->summary.valid = true;
            std::snprintf(bridge_->summary.message, sizeof(bridge_->summary.message),
                          "上次加时恢复信息无法读取，请勿重复输入该码");
            error_ = true;
            status_expanded_ = true;
            last_request_kind_ = OverlayRequestKind::OfflineCode;
            return true;
        }
        if (!found) return false;
        if (!ptc_companion_pending_redemption_has_submission(&bridge_->transport.file, &pending_redemption_)) {
            (void)ptc_companion_pending_redemption_clear(&bridge_->transport.file);
            std::memset(&bridge_->summary, 0, sizeof(bridge_->summary));
            bridge_->summary.valid = true;
            std::snprintf(bridge_->summary.message, sizeof(bridge_->summary.message),
                          "上次确认在提交前中断；加时码未消费，请重新输入");
            error_ = true;
            status_expanded_ = true;
            last_request_kind_ = OverlayRequestKind::OfflineCode;
            return true;
        }
        apply_pending_preview();
        recovery_active_ = true;
        result_pending_ = true;
        result_failed_ = false;
        success_visible_ = true;
        active_request_kind_ = OverlayRequestKind::OfflineCode;
        last_request_kind_ = OverlayRequestKind::OfflineCode;
        poll_pending_redemption();
        return true;
    }

    void poll_pending_redemption()
    {
        const u64 now = armGetSystemTick();
        if (recovery_last_poll_tick_ != 0 &&
            armTicksToNs(now - recovery_last_poll_tick_) < 250000000ULL) return;
        recovery_last_poll_tick_ = now;
        PtcCompanionStatus status = ptc_companion_read_result(
            &bridge_->transport.file, pending_redemption_.request_id, 0, -1,
            bridge_->result_json, sizeof(bridge_->result_json));
        if (status != PTC_COMPANION_OK) return;
        if (ptc_companion_parse_result_summary(bridge_->result_json, &bridge_->summary) != PTC_COMPANION_OK ||
            std::strcmp(bridge_->summary.type, "offline_code") != 0) return;
        recovery_active_ = false;
        result_pending_ = false;
        result_failed_ = !bridge_->summary.ok;
        displayed_summary_ = bridge_->summary;
        apply_pending_preview();
        success_visible_ = true;
        error_ = false;
        status_expanded_ = true;
        active_request_kind_ = OverlayRequestKind::None;
        pending_code_[0] = '\0';
        ptc_overlay_input_init(input_);
    }

    bool begin_status_refresh()
    {
        if (!bridge_ || bridge_->waiting) return false;
        PtcCompanionStatus status = ptc_overlay_bridge_submit_status(
            bridge_, static_cast<int64_t>(std::time(nullptr)), ++request_nonce_);
        if (status != PTC_COMPANION_OK) {
            error_ = true;
            status_expanded_ = true;
            last_request_kind_ = OverlayRequestKind::Status;
            return false;
        }
        active_request_kind_ = OverlayRequestKind::Status;
        last_request_kind_ = OverlayRequestKind::Status;
        request_started_tick_ = armGetSystemTick();
        last_elapsed_ms_ = 0;
        error_ = false;
        return true;
    }

    bool begin_code_preview()
    {
        char code[32];
        if (recovery_active_) {
            result_pending_ = true;
            success_visible_ = true;
            return false;
        }
        if (!bridge_ || bridge_->waiting || !ptc_overlay_input_can_submit(input_) ||
            !ptc_overlay_input_format(input_, code, sizeof(code))) return false;
        std::snprintf(pending_code_, sizeof(pending_code_), "%.8s", code);
        return begin_preview_request(false);
    }

    bool begin_preview_request(bool recheck)
    {
        if (!bridge_ || bridge_->waiting || pending_code_[0] == '\0') return false;
        if (recheck) {
            previous_after_available_ = preview_summary_.remaining_after_available;
            previous_after_zero_ = preview_summary_.remaining_after_available &&
                preview_summary_.remaining_after_minutes == 0;
            previous_capped_ = preview_summary_.preview_capped;
            previous_converts_unlimited_ = preview_summary_.converts_unlimited_to_limited;
            preview_recheck_ = true;
            awaiting_confirm_recheck_ = true;
            preview_ready_ = false;
        }
        PtcCompanionStatus status = ptc_overlay_bridge_preview(
            bridge_, pending_code_, static_cast<int64_t>(std::time(nullptr)), ++request_nonce_);
        if (status != PTC_COMPANION_OK) {
            error_ = true;
            status_expanded_ = true;
            last_request_kind_ = OverlayRequestKind::PreviewOfflineCode;
            return false;
        }
        active_request_kind_ = OverlayRequestKind::PreviewOfflineCode;
        last_request_kind_ = OverlayRequestKind::PreviewOfflineCode;
        request_started_tick_ = armGetSystemTick();
        last_elapsed_ms_ = 0;
        error_ = false;
        status_expanded_ = true;
        return true;
    }

    bool begin_actual_code_submit()
    {
        if (!bridge_ || bridge_->waiting || pending_code_[0] == '\0') return false;
        PtcCompanionStatus status = ptc_overlay_bridge_submit(
            bridge_, pending_code_, static_cast<int64_t>(std::time(nullptr)), ++request_nonce_,
            &redemption_before_);
        if (status != PTC_COMPANION_OK) {
            pending_code_[0] = '\0';
            ptc_overlay_input_init(input_);
            std::memset(&bridge_->summary, 0, sizeof(bridge_->summary));
            bridge_->summary.valid = true;
            std::snprintf(bridge_->summary.type, sizeof(bridge_->summary.type), "offline_code");
            std::snprintf(bridge_->summary.message, sizeof(bridge_->summary.message),
                          "提交失败；加时码未消费，请重新输入");
            error_ = true;
            status_expanded_ = true;
            last_request_kind_ = OverlayRequestKind::OfflineCode;
            return false;
        }
        bool marker_found = false;
        (void)ptc_companion_pending_redemption_load(
            &bridge_->transport.file, &pending_redemption_, &marker_found);
        (void)marker_found;
        apply_pending_preview();
        active_request_kind_ = OverlayRequestKind::OfflineCode;
        last_request_kind_ = OverlayRequestKind::OfflineCode;
        request_started_tick_ = armGetSystemTick();
        last_elapsed_ms_ = 0;
        error_ = false;
        status_expanded_ = true;
        return true;
    }

    bool retry_last_request()
    {
        error_ = false;
        if (last_request_kind_ == OverlayRequestKind::OfflineCode ||
            last_request_kind_ == OverlayRequestKind::PreviewOfflineCode) return false;
        return begin_status_refresh();
    }

    const char *request_label() const
    {
        OverlayRequestKind kind = active_request_kind_ != OverlayRequestKind::None
            ? active_request_kind_ : last_request_kind_;
        if (kind == OverlayRequestKind::Status) return "刷新今日状态";
        if (kind == OverlayRequestKind::PreviewOfflineCode) return "预览今日加时";
        if (kind == OverlayRequestKind::OfflineCode) return "提交今日加时";
        return "未开始";
    }

    void format_refresh_age(char *out, size_t out_size) const
    {
        if (!out || out_size == 0) return;
        if (active_request_kind_ == OverlayRequestKind::Status && bridge_->waiting) {
            std::snprintf(out, out_size, "刷新中");
            return;
        }
        if (!has_status_snapshot_ || last_refresh_tick_ == 0) {
            std::snprintf(out, out_size, "尚未刷新");
            return;
        }
        const u64 age_seconds = armTicksToNs(armGetSystemTick() - last_refresh_tick_) / 1000000000ULL;
        if (age_seconds < 2) {
            std::snprintf(out, out_size, "刚刚刷新");
        } else if (age_seconds < 60) {
            std::snprintf(out, out_size, "%llu 秒前", static_cast<unsigned long long>(age_seconds));
        } else {
            std::snprintf(out, out_size, "%llu 分钟前", static_cast<unsigned long long>(age_seconds / 60));
        }
    }

    bool status_is_stale() const
    {
        if (!has_status_snapshot_ || last_refresh_tick_ == 0) return false;
        return armTicksToNs(armGetSystemTick() - last_refresh_tick_) >= 30000000000ULL;
    }

    bool status_needs_detail() const
    {
        return error_ || success_visible_;
    }

    void draw_code_preview(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw)
    {
        char line[128];
        renderer->drawRect(cx, cy + 18, cw, 540, renderer->a(PANEL_COLOR));
        draw_outline(renderer, cx, cy + 18, cw, 540, 2, FOCUS_BORDER);
        renderer->drawString(preview_changed_ ? "状态已变化，请再次确认" : "确认兑换加时码",
                             false, cx + 14, cy + 52, 18, renderer->a(TEXT_COLOR));
        std::snprintf(line, sizeof(line), "本次增加 %d 分钟", preview_summary_.grant_minutes);
        renderer->drawString(line, false, cx + 14, cy + 84, 15, renderer->a(FOCUS_BORDER));
        renderer->drawString("今天有效，成功兑换后只能使用一次", false,
                             cx + 14, cy + 110, 12, renderer->a(MUTED_COLOR));

        renderer->drawRect(cx + 12, cy + 142, cw - 24, 86, renderer->a(CARD_COLOR));
        renderer->drawString("当前还能玩", false, cx + 24, cy + 168, 12, renderer->a(MUTED_COLOR));
        if (preview_summary_.converts_unlimited_to_limited) {
            renderer->drawString("不限时", false, cx + 155, cy + 171, 19, renderer->a(SUCCESS_COLOR));
        } else if (preview_summary_.remaining_available) {
            std::snprintf(line, sizeof(line), "%d 分钟", preview_summary_.remaining_minutes);
            renderer->drawString(line, false, cx + 155, cy + 171, 19, renderer->a(SUCCESS_COLOR));
        } else {
            renderer->drawString("暂不可用", false, cx + 155, cy + 171, 16, renderer->a(MUTED_COLOR));
        }

        renderer->drawRect(cx + 12, cy + 244, cw - 24, 86, renderer->a(CARD_COLOR));
        renderer->drawString("兑换后预计", false, cx + 24, cy + 270, 12, renderer->a(MUTED_COLOR));
        if (preview_summary_.remaining_after_available) {
            std::snprintf(line, sizeof(line), "%d 分钟", preview_summary_.remaining_after_minutes);
            renderer->drawString(line, false, cx + 155, cy + 273, 19,
                                 renderer->a(preview_summary_.remaining_after_minutes == 0 ? ERROR_COLOR : SUCCESS_COLOR));
        } else {
            renderer->drawString("暂不可用", false, cx + 155, cy + 273, 16, renderer->a(ERROR_COLOR));
        }

        if (preview_summary_.converts_unlimited_to_limited) {
            renderer->drawString("警告：兑换后将从不限时改为限时", false, cx + 16, cy + 366, 13, renderer->a(ERROR_COLOR));
        } else if (preview_summary_.preview_capped) {
            std::snprintf(line, sizeof(line), "受每日上限影响，实际增加 %d 分钟", preview_summary_.effective_add_minutes);
            renderer->drawString(line, false, cx + 16, cy + 366, 13, renderer->a(ERROR_COLOR));
        } else {
            renderer->drawString("确认前不会消费这枚加时码", false, cx + 16, cy + 366, 13, renderer->a(MUTED_COLOR));
        }
        const bool dangerous = !preview_summary_.remaining_after_available ||
            preview_summary_.remaining_after_minutes == 0 ||
            preview_summary_.converts_unlimited_to_limited;
        renderer->drawString(touch_hold_warning_ ? "请使用手柄长按 A 确认" :
                             (dangerous ? "长按 A 1 秒确认；B 取消" : "A / + 确认；B 取消"),
                             false, cx + 16, cy + 414, 13,
                             renderer->a(dangerous ? ERROR_COLOR : FOCUS_BORDER));
        renderer->drawRect(cx, cy + 500, 145, 50, renderer->a(CARD_COLOR));
        draw_outline(renderer, cx, cy + 500, 145, 50, 1, MUTED_COLOR);
        renderer->drawString("B 取消", false, cx + 45, cy + 530, 14, renderer->a(TEXT_COLOR));
        renderer->drawRect(cx + 170, cy + 500, 160, 50, renderer->a(FOCUS_BG));
        draw_outline(renderer, cx + 170, cy + 500, 160, 50, 2, FOCUS_BORDER);
        renderer->drawString(dangerous ? "长按 A 确认" : "A 确认", false,
                             cx + 202, cy + 530, 14, renderer->a(TEXT_COLOR));
    }

    void draw_code_success(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw)
    {
        char line[128];
        renderer->drawRect(cx, cy + 36, cw, 460, renderer->a(PANEL_COLOR));
        const tsl::Color accent = result_failed_ ? ERROR_COLOR : SUCCESS_COLOR;
        draw_outline(renderer, cx, cy + 36, cw, 460, 2, accent);
        renderer->drawString(result_pending_ ? "加时结果确认中" : (result_failed_ ? "兑换未成功" : "加时成功"),
                             false, cx + 14, cy + 74, 22, renderer->a(accent));
        std::snprintf(line, sizeof(line), result_pending_ ? "预计增加 %d 分钟" :
                      (result_failed_ ? "原计划增加 %d 分钟" : "已增加 %d 分钟"),
                      preview_summary_.grant_minutes);
        renderer->drawString(line, false, cx + 14, cy + 108, 15, renderer->a(TEXT_COLOR));
        renderer->drawString(result_pending_ ? "正在核对最终结果，请勿重复输入这枚加时码" :
                             (result_failed_ ? "后台已确认失败；该码未消费，可重新输入" :
                              "该加时码已经使用，不能再次使用"),
                             false, cx + 14, cy + 136, 12, renderer->a(MUTED_COLOR));
        renderer->drawString("兑换前", false, cx + 18, cy + 190, 12, renderer->a(MUTED_COLOR));
        if (redemption_before_.converts_unlimited_to_limited) std::snprintf(line, sizeof(line), "不限时");
        else if (redemption_before_.remaining_available) std::snprintf(line, sizeof(line), "%d 分钟", redemption_before_.remaining_minutes);
        else std::snprintf(line, sizeof(line), "暂不可用");
        renderer->drawString(line, false, cx + 130, cy + 193, 18, renderer->a(TEXT_COLOR));
        renderer->drawString(result_pending_ ? "预览兑换后" : "实际兑换后", false, cx + 18, cy + 254, 12, renderer->a(MUTED_COLOR));
        const PtcCompanionResultSummary &after = result_pending_ ? preview_summary_ : displayed_summary_;
        if (result_pending_ ? after.remaining_after_available : after.remaining_available) {
            std::snprintf(line, sizeof(line), "%d 分钟",
                          result_pending_ ? after.remaining_after_minutes : after.remaining_minutes);
        }
        else std::snprintf(line, sizeof(line), "暂不可用");
        renderer->drawString(line, false, cx + 130, cy + 257, 18, renderer->a(accent));
        renderer->drawString(result_pending_ ? "可关闭界面；下次打开会继续确认" :
                             (result_failed_ ? "失败结果已确认" : "结果已确认并保存"),
                             false, cx + 18, cy + 324, 14, renderer->a(accent));
        renderer->drawRect(cx + 30, cy + 392, cw - 60, 56, renderer->a(FOCUS_BG));
        draw_outline(renderer, cx + 30, cy + 392, cw - 60, 56, 2, FOCUS_BORDER);
        renderer->drawString("A / B  返回空白输入页", false, cx + 82, cy + 426, 14, renderer->a(TEXT_COLOR));
    }

    void draw_overlay(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw, s32 ch)
    {
        (void)ch;
        if (success_visible_) {
            draw_code_success(renderer, cx, cy, cw);
            return;
        }
        if (preview_ready_) {
            draw_code_preview(renderer, cx, cy, cw);
            return;
        }
        char line[128];
        char age[32];
        const PtcCompanionResultSummary &summary = displayed_summary_;
        format_refresh_age(age, sizeof(age));
        const bool remaining_refresh_pending = ptc_overlay_remaining_refresh_pending(
            bridge_->waiting, active_request_kind_ == OverlayRequestKind::OfflineCode);

        // --- 0. Top Prominent Status Banner (醒目展示今日已玩与修改后/当前可玩时长) ---
        const s32 top_banner_y = cy + PTC_OVERLAY_TOP_BANNER_Y;
        const s32 top_banner_h = PTC_OVERLAY_TOP_BANNER_H;
        renderer->drawRect(cx, top_banner_y, cw, top_banner_h, renderer->a(CARD_COLOR));
        draw_outline(renderer, cx, top_banner_y, cw, top_banner_h,
                     remaining_refresh_pending ? 2 : 1,
                     remaining_refresh_pending ? WAITING_COLOR : FOCUS_BORDER);

        renderer->drawString("今日已玩", false, cx + 10, top_banner_y + 21, 11, renderer->a(MUTED_COLOR));
        if (summary.valid && summary.played_minutes_available) {
            std::snprintf(line, sizeof(line), "%d 分钟", summary.played_minutes);
        } else {
            std::snprintf(line, sizeof(line), "-- 分钟");
        }
        renderer->drawString(line, false, cx + 78, top_banner_y + 23, 16, renderer->a(TEXT_COLOR));

        renderer->drawString(success_visible_ ? "修改后可玩" : "当前还剩可玩", false,
                             cx + 10, top_banner_y + 51, 11, renderer->a(MUTED_COLOR));
        if (remaining_refresh_pending) {
            renderer->drawString("正在刷新…", false, cx + 116, top_banner_y + 54, 14,
                                 renderer->a(WAITING_COLOR));
        } else if (summary.valid && summary.remaining_available) {
            std::snprintf(line, sizeof(line), "%d 分钟", summary.remaining_minutes);
            renderer->drawString(line, false, cx + 116, top_banner_y + 54, 16, renderer->a(SUCCESS_COLOR));
        } else {
            renderer->drawString("暂不可用", false, cx + 116, top_banner_y + 54, 14, renderer->a(MUTED_COLOR));
        }

        const bool busy = bridge_->waiting;
        renderer->drawRect(cx + PTC_OVERLAY_REFRESH_X, cy + PTC_OVERLAY_REFRESH_Y,
                           PTC_OVERLAY_REFRESH_W, PTC_OVERLAY_REFRESH_H,
                           renderer->a(busy ? DISABLED_COLOR : FOCUS_BG));
        draw_outline(renderer, cx + PTC_OVERLAY_REFRESH_X, cy + PTC_OVERLAY_REFRESH_Y,
                     PTC_OVERLAY_REFRESH_W, PTC_OVERLAY_REFRESH_H, 1,
                     remaining_refresh_pending ? WAITING_COLOR : (busy ? MUTED_COLOR : FOCUS_BORDER));
        renderer->drawString(remaining_refresh_pending ? "确认中" : (busy ? "刷新中" : "Y  刷新"),
                             false, cx + PTC_OVERLAY_REFRESH_X + 18,
                             cy + PTC_OVERLAY_REFRESH_Y + 20, 12,
                             renderer->a(remaining_refresh_pending ? WAITING_COLOR :
                                         (busy ? MUTED_COLOR : TEXT_COLOR)));
        renderer->drawString(remaining_refresh_pending ? "提交后等待结果" :
                             (status_is_stale() ? "数据可能已过期" : age),
                             false, cx + PTC_OVERLAY_REFRESH_X, top_banner_y + 64, 11,
                             renderer->a(remaining_refresh_pending ? WAITING_COLOR :
                                         (status_is_stale() ? ERROR_COLOR : MUTED_COLOR)));

        // --- 1. Header Prompt & Guidance (护眼提醒) ---
        renderer->drawString("提示：加时前记得向窗外远眺 5 分钟！", false, cx + 5, cy + 94, 14, renderer->a(FOCUS_BORDER));

        // --- 2. Code Display Slots (8位卡片槽 - 增大更醒目) ---
        const s32 slot_y = cy + PTC_OVERLAY_SLOT_Y;
        const s32 slot_w = PTC_OVERLAY_SLOT_W;
        const s32 slot_h = PTC_OVERLAY_SLOT_H;
        const s32 slot_gap = PTC_OVERLAY_SLOT_GAP;
        const s32 slot_group_w = PTC_OVERLAY_CODE_SYMBOLS * slot_w +
            (PTC_OVERLAY_CODE_SYMBOLS - 1) * slot_gap;
        const s32 slot_start_x = cx + (cw - slot_group_w) / 2;

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
            const auto symbol_size = renderer->drawString(
                symbol, false, 0, 0, 30, tsl::style::color::ColorTransparent);
            renderer->drawString(symbol, false,
                sx + (slot_w - static_cast<s32>(symbol_size.first)) / 2,
                slot_y + 36, 30,
                renderer->a(index < input_->length ? TEXT_COLOR : (is_cursor ? FOCUS_BORDER : MUTED_COLOR)));
        }

        std::snprintf(line, sizeof(line), "已输入 %u/8 位   当前高亮数字：%c", input_->length, ptc_overlay_input_charset()[input_->cursor]);
        renderer->drawString(line, false, cx + 5, cy + 180, 14, renderer->a(MUTED_COLOR));

        // --- 3. Keypad 3x4 Grid (软键盘放大) ---
        const char *charset = ptc_overlay_input_charset();
        const s32 panel_y = cy + PTC_OVERLAY_KEYPAD_Y;
        const s32 panel_h = PTC_OVERLAY_KEYPAD_H;
        renderer->drawRect(cx, panel_y, cw, panel_h, renderer->a(PANEL_COLOR));
        draw_outline(renderer, cx, panel_y, cw, panel_h, 1, MUTED_COLOR);

        // 绘制数字键 0-9
        for (unsigned int index = 0; index < PTC_OVERLAY_KEY_COUNT; ++index) {
            char symbol[2] = { charset[index], '\0' };
            unsigned int row = index == 0 ? 3u : (index - 1u) / 3u;
            unsigned int col = index == 0 ? 1u : (index - 1u) % 3u;
            s32 key_x = cx + 12 + static_cast<s32>(col) * 102;
            s32 key_y = panel_y + 6 + static_cast<s32>(row) * PTC_OVERLAY_KEY_ROW_STEP;
            const bool focused = (index == input_->cursor);

            renderer->drawRect(key_x, key_y, 90, PTC_OVERLAY_KEY_H, renderer->a(focused ? FOCUS_BG : KEY_COLOR));
            draw_outline(renderer, key_x, key_y, 90, PTC_OVERLAY_KEY_H, focused ? 3 : 1, focused ? FOCUS_BORDER : MUTED_COLOR);
            const auto key_text_size = renderer->drawString(
                symbol, false, 0, 0, 24, tsl::style::color::ColorTransparent);
            renderer->drawString(symbol, false, key_x + (90 - static_cast<s32>(key_text_size.first)) / 2,
                                 key_y + 27, 24, renderer->a(focused ? FOCUS_BORDER : TEXT_COLOR));
        }

        // 第四行辅助按键 [X] 退格 和 [Y] 清空
        s32 x_btn_x = cx + 12;
        s32 btn_y = panel_y + 6 + 3 * PTC_OVERLAY_KEY_ROW_STEP;
        renderer->drawRect(x_btn_x, btn_y, 90, PTC_OVERLAY_KEY_H, renderer->a(KEY_COLOR));
        draw_outline(renderer, x_btn_x, btn_y, 90, PTC_OVERLAY_KEY_H, 1, MUTED_COLOR);
        renderer->drawString("X 退格", false, x_btn_x + 22, btn_y + 23, 12, renderer->a(MUTED_COLOR));

        s32 y_btn_x = cx + 216;
        renderer->drawRect(y_btn_x, btn_y, 90, PTC_OVERLAY_KEY_H, renderer->a(KEY_COLOR));
        draw_outline(renderer, y_btn_x, btn_y, 90, PTC_OVERLAY_KEY_H, 1, MUTED_COLOR);
        renderer->drawString("点按清空", false, y_btn_x + 17, btn_y + 23, 12, renderer->a(MUTED_COLOR));

        // --- 4. Control & Submit Bar (操作与提交栏) ---
        const bool can_submit = ptc_overlay_request_action_enabled(bridge_->waiting) &&
            ptc_overlay_input_can_submit(input_);
        const s32 submit_y = cy + PTC_OVERLAY_SUBMIT_Y;
        const s32 submit_h = PTC_OVERLAY_SUBMIT_H;

        // 提交加时大按钮
        renderer->drawRect(cx, submit_y, cw, submit_h, renderer->a(can_submit ? FOCUS_BG : DISABLED_COLOR));
        draw_outline(renderer, cx, submit_y, cw, submit_h, can_submit ? 3 : 1, can_submit ? FOCUS_BORDER : MUTED_COLOR);

        if (can_submit) {
            renderer->drawString("+ 提交加时奖励（点击或按 +）", false, cx + 50, submit_y + 24, 14, renderer->a(TEXT_COLOR));
        } else if (bridge_->waiting) {
            renderer->drawString("后台处理中，可继续编辑输入", false, cx + 66, submit_y + 24, 13,
                                 renderer->a(MUTED_COLOR));
        } else {
            renderer->drawString("+ 提交加时（需输满 8 位数字）", false, cx + 58, submit_y + 24, 13, renderer->a(MUTED_COLOR));
        }

        // --- 5. Collapsible Status Panel (可折叠命令与状态栏) ---
        const s32 status_y = cy + PTC_OVERLAY_STATUS_Y;
        const s32 status_w = cw;

        if (!status_expanded_) {
            renderer->drawRect(cx, status_y, status_w, PTC_OVERLAY_STATUS_COLLAPSED_H, renderer->a(PANEL_COLOR));
            draw_outline(renderer, cx, status_y, status_w, PTC_OVERLAY_STATUS_COLLAPSED_H, 1, MUTED_COLOR);

            if (bridge_->waiting && active_request_kind_ == OverlayRequestKind::Status) {
                renderer->drawString("[-] 正在刷新状态…（按 - 展开）", false, cx + 12, status_y + 21, 12, renderer->a(FOCUS_BORDER));
            } else if (bridge_->waiting) {
                renderer->drawString("[-] 正在处理加时…（按 - 展开）", false, cx + 12, status_y + 21, 12, renderer->a(FOCUS_BORDER));
            } else if (error_) {
                renderer->drawString(
                    (last_request_kind_ == OverlayRequestKind::OfflineCode ||
                     last_request_kind_ == OverlayRequestKind::PreviewOfflineCode)
                        ? "[-] 请求失败（按 - 展开，请重新输入）"
                        : "[-] 请求失败（按 - 展开，Y 重试）",
                    false, cx + 12, status_y + 21, 12, renderer->a(ERROR_COLOR));
            } else if (success_visible_) {
                renderer->drawString("[-] 加时成功！（按 - 展开）", false, cx + 12, status_y + 21, 12, renderer->a(SUCCESS_COLOR));
            } else if (has_status_snapshot_) {
                renderer->drawString("[-] 状态已刷新（按 - 展开）", false, cx + 12, status_y + 21, 12,
                                     renderer->a(MUTED_COLOR));
            } else {
                renderer->drawString("[-] 命令与状态（点击或按 - / L / R）", false, cx + 12, status_y + 21, 12, renderer->a(MUTED_COLOR));
            }
        } else {
            const s32 expanded_h = status_needs_detail()
                ? PTC_OVERLAY_STATUS_DETAIL_H : PTC_OVERLAY_STATUS_NORMAL_H;
            renderer->drawRect(cx, status_y, status_w, expanded_h, renderer->a(PANEL_COLOR));
            draw_outline(renderer, cx, status_y, status_w, expanded_h, 2, FOCUS_BORDER);

            renderer->drawString("[-] 命令与状态详情（按 - 收起）", false, cx + 12, status_y + 18, 12, renderer->a(FOCUS_BORDER));
            std::snprintf(line, sizeof(line), "%s命令：%s", bridge_->waiting ? "当前" : "最近", request_label());
            renderer->drawString(line, false, cx + 12, status_y + 36, 12, renderer->a(TEXT_COLOR));
            renderer->drawString(ptc_overlay_bridge_transport_label(bridge_), false, cx + 12, status_y + 52, 11, renderer->a(MUTED_COLOR));

            const char *stage = transport_stage(bridge_);
            if (stage[0]) {
                renderer->drawString(stage, false, cx + 12, status_y + 72, 12, renderer->a(FOCUS_BORDER));
            }

            if (error_) {
                const char *message = ptc_overlay_bridge_error_message_zh(bridge_);
                renderer->drawString(message, false, cx + 12, status_y + 72, 12, renderer->a(ERROR_COLOR), 290);
                const bool code_error = last_request_kind_ == OverlayRequestKind::OfflineCode ||
                    last_request_kind_ == OverlayRequestKind::PreviewOfflineCode;
                if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                    std::snprintf(line, sizeof(line), code_error ? "错误码：%d · 请重新输入" :
                                  "错误码：%d · 按 Y 重试", bridge_->summary.error_code);
                    renderer->drawString(line, false, cx + 12, status_y + 108, 11, renderer->a(ERROR_COLOR));
                } else {
                    renderer->drawString(code_error ? "请重新输入；Y 返回输入" : "按 Y 重试",
                                         false, cx + 12, status_y + 108, 11, renderer->a(ERROR_COLOR));
                }
            } else if (success_visible_) {
                renderer->drawString("加时成功！", false, cx + 12, status_y + 74, 16, renderer->a(SUCCESS_COLOR));
                std::snprintf(line, sizeof(line), "修改后可玩 %d 分钟", summary.remaining_minutes);
                renderer->drawString(line, false, cx + 12, status_y + 96, 15, renderer->a(SUCCESS_COLOR));
                if (summary.played_minutes_available) {
                    std::snprintf(line, sizeof(line), "今日已玩约 %d 分钟，即将自动关闭…", summary.played_minutes);
                    renderer->drawString(line, false, cx + 12, status_y + 116, 12, renderer->a(SUCCESS_COLOR));
                } else {
                    renderer->drawString("状态已刷新，即将自动关闭…", false, cx + 12, status_y + 116, 12, renderer->a(SUCCESS_COLOR));
                }
            } else if (!bridge_->waiting && has_status_snapshot_) {
                renderer->drawString("状态刷新完成", false, cx + 12, status_y + 74, 15, renderer->a(SUCCESS_COLOR));
            }
        }
    }

private:
    PtcOverlayBridge *bridge_;
    PtcOverlayInput *input_;
    PtcCompanionResultSummary displayed_summary_{};
    PtcCompanionResultSummary preview_summary_{};
    PtcCompanionResultSummary redemption_before_{};
    OverlayRequestKind active_request_kind_ = OverlayRequestKind::None;
    OverlayRequestKind last_request_kind_ = OverlayRequestKind::None;
    unsigned int request_nonce_ = 0;
    u64 request_started_tick_ = 0;
    u64 last_input_tick_ = 0;
    u64 last_refresh_tick_ = 0;
    int last_elapsed_ms_ = 0;
    bool error_ = false;
    bool has_status_snapshot_ = false;
    bool status_expanded_ = false;
    bool preview_ready_ = false;
    bool preview_changed_ = false;
    bool preview_recheck_ = false;
    bool awaiting_confirm_recheck_ = false;
    bool previous_after_available_ = false;
    bool previous_after_zero_ = false;
    bool previous_capped_ = false;
    bool previous_converts_unlimited_ = false;
    bool success_visible_ = false;
    bool result_pending_ = false;
    bool result_failed_ = false;
    bool recovery_active_ = false;
    bool touch_hold_warning_ = false;
    int confirm_hold_ms_ = 0;
    char pending_code_[9]{};
    PtcPendingRedemption pending_redemption_{};
    u64 recovery_last_poll_tick_ = 0;
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
