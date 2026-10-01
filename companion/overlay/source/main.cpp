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
#include "../../auth.h"
#include "../input_model.h"
#include "../../ui_language.h"
#include "third_party/cjson/cJSON.h"
#include "common/crypto/sha256.h"
#include "common/time/ptc_time.h"
#include "platform/switch/fs_storage.h"
}

namespace {

[[gnu::used]] constexpr char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

constexpr char APP_ROOT[] = "sdmc:/switch/playwise";
constexpr tsl::Color PANEL_COLOR{ 0x1, 0x1, 0x1, 0xE };
constexpr tsl::Color CARD_COLOR{ 0x2, 0x2, 0x2, 0xF };
constexpr tsl::Color KEY_COLOR{ 0x2, 0x2, 0x3, 0xF };
constexpr tsl::Color FOCUS_BG{ 0x0, 0x5, 0x6, 0xF };
constexpr tsl::Color FOCUS_BORDER{ 0x0, 0xD, 0xC, 0xF };
constexpr tsl::Color TEXT_COLOR{ 0xF, 0xF, 0xF, 0xF };
constexpr tsl::Color DARK_TEXT_COLOR{ 0x0, 0x1, 0x1, 0xF };
constexpr tsl::Color MUTED_COLOR{ 0x8, 0x9, 0xA, 0xF };
constexpr tsl::Color DISABLED_COLOR{ 0x2, 0x2, 0x2, 0xF };
constexpr tsl::Color SUCCESS_COLOR{ 0x1, 0xD, 0x7, 0xF };
constexpr tsl::Color ERROR_COLOR{ 0xF, 0x3, 0x4, 0xF };
constexpr tsl::Color WAITING_COLOR{ 0xF, 0x9, 0x1, 0xF };
constexpr tsl::Color DANGER_BG{ 0x3, 0x1, 0x1, 0xF };
constexpr tsl::Color WARNING_BG{ 0x3, 0x2, 0x1, 0xF };
constexpr tsl::Color BACKSPACE_BORDER{ 0xC, 0x7, 0x2, 0xF };
constexpr tsl::Color CLEAR_BORDER{ 0xC, 0x3, 0x3, 0xF };

static std::pair<u32, u32> draw_localized(tsl::gfx::Renderer *renderer,
    const char *value, bool monospace, s32 x, s32 y, float font_size,
    tsl::Color color, ssize_t max_width = 0)
{
    char localized[2048];
    return renderer->drawString(ptc_ui_localize(value, localized, sizeof(localized)),
        monospace, x, y, font_size, color, max_width);
}

static void load_overlay_language(PtcStorage *storage)
{
    char config[8192];
    PtcUiLanguagePreference preference = PTC_UI_LANGUAGE_SYSTEM;
    PtcUiSystemLanguage system_language = PTC_UI_SYSTEM_LANGUAGE_UNKNOWN;
    u64 code;
    SetLanguage language;
    if (storage && storage->vtable->read_text(storage, "sdmc:/switch/playwise/config.json",
        config, sizeof(config))) {
        cJSON *root = cJSON_Parse(config);
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "ui_language");
        if (cJSON_IsString(item))
            (void)ptc_ui_language_parse_preference(item->valuestring, &preference);
        cJSON_Delete(root);
    }
    if (R_SUCCEEDED(setInitialize())) {
        Result result = setGetSystemLanguage(&code);
        if (R_SUCCEEDED(result)) result = setMakeLanguage(code, &language);
        setExit();
        if (R_SUCCEEDED(result))
            system_language = language == SetLanguage_ZHTW || language == SetLanguage_ZHHANT
                ? PTC_UI_SYSTEM_LANGUAGE_TRADITIONAL : PTC_UI_SYSTEM_LANGUAGE_SIMPLIFIED;
    }
    ptc_ui_language_set_resolved(ptc_ui_language_resolve(preference, system_language));
}

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

// The offline code MAC binds the day index reported by status, so the console date
// must come from the summary. A local-clock guess could advertise a day the
// sysmodule would reject.
static void format_console_date(const PtcCompanionResultSummary &summary, char *out, size_t out_size)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    if (!out || out_size == 0) return;
    if (!summary.valid || summary.day_index < 0 || summary.day_index > 0xffff ||
        !ptc_date_from_day_index(static_cast<uint16_t>(summary.day_index), &year, &month, &day)) {
        std::snprintf(out, out_size, "主机日期待刷新");
        return;
    }
    std::snprintf(out, out_size, "主机今天 %04u-%02u-%02u",
                  static_cast<unsigned int>(year), static_cast<unsigned int>(month),
                  static_cast<unsigned int>(day));
}

static void environment_fingerprint(PtcOverlayBridge *bridge, char out[65])
{
    char environment[1024];
    uint8_t digest[PTC_SHA256_DIGEST_SIZE];
    static constexpr char HEX[] = "0123456789abcdef";
    if (!bridge || !bridge->transport.file.storage ||
        !bridge->transport.file.storage->vtable->read_text(
            bridge->transport.file.storage, "sdmc:/switch/playwise/environment.json",
            environment, sizeof(environment))) {
        std::snprintf(out, 65, "environment-unavailable");
        return;
    }
    PtcSha256Ctx hash;
    ptc_sha256_init(&hash);
    ptc_sha256_update(&hash, reinterpret_cast<const uint8_t *>(environment), std::strlen(environment));
    ptc_sha256_final(&hash, digest);
    for (size_t i = 0; i < sizeof(digest); ++i) {
        out[i * 2] = HEX[digest[i] >> 4];
        out[i * 2 + 1] = HEX[digest[i] & 0xfu];
    }
    out[64] = '\0';
}

enum class OverlayRequestKind {
    None,
    Status,
    OverlayReady,
    PreviewOfflineCode,
    OfflineCode,
    ClaimDailyBuffer,
    AddTodayMinutes,
    DisableTodayLimit,
    SkipBedtime,
    ClearBedtimeSkip,
    DisableBedtime,
    RestoreInstallSnapshot,
};

enum class ParentView {
    Child,
    Pin,
    Actions,
    Confirm,
    Result,
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
    PctcGui(PtcOverlayBridge *bridge, PtcOverlayInput *input) : bridge_(bridge), input_(input)
    {
        ptc_companion_auth_init(&auth_, APP_ROOT, bridge_ ? bridge_->transport.file.storage : nullptr);
    }

    tsl::elm::Element *createUI() override
    {
        char title[64], subtitle[64];
        auto frame = new PlayWiseOverlayFrame(
            ptc_ui_localize("自律约定", title, sizeof(title)),
            ptc_ui_localize("额度与就寝恢复", subtitle, sizeof(subtitle)));
        if (!restore_pending_redemption()) {
            (void)begin_overlay_ready();
        }
        frame->setContent(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->setPreferTraditionalChinese(
                ptc_ui_language_get_resolved() == PTC_UI_LANGUAGE_TRADITIONAL);
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
            if (active_request_kind_ == OverlayRequestKind::OverlayReady &&
                bridge_->summary.valid && bridge_->summary.ok) {
                displayed_summary_ = bridge_->summary;
                has_status_snapshot_ = true;
                error_ = false;
                (void)begin_status_refresh();
            } else if (ptc_overlay_bridge_status_succeeded(bridge_)) {
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
            } else if (is_access_recovery_request(active_request_kind_) && bridge_->summary.valid) {
                if (bridge_->summary.ok) {
                    displayed_summary_ = bridge_->summary;
                    has_status_snapshot_ = true;
                    last_refresh_tick_ = armGetSystemTick();
                }
                parent_view_ = ParentView::Result;
                parent_action_succeeded_ = bridge_->summary.ok;
                error_ = !bridge_->summary.ok;
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
        } else if (is_access_recovery_request(active_request_kind_)) {
            parent_view_ = ParentView::Result;
            parent_action_succeeded_ = false;
            error_ = true;
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
        const u64 dpad_down = keysDown & (HidNpadButton_Up | HidNpadButton_Right |
            HidNpadButton_Down | HidNpadButton_Left);

        constexpr s32 STICK_DEADZONE = 16000;
        u64 stick_keys = 0;
        if (left.x > STICK_DEADZONE) stick_keys |= HidNpadButton_Right;
        if (left.x < -STICK_DEADZONE) stick_keys |= HidNpadButton_Left;
        if (left.y > STICK_DEADZONE) stick_keys |= HidNpadButton_Up;
        if (left.y < -STICK_DEADZONE) stick_keys |= HidNpadButton_Down;
        keysDown |= stick_keys & ~prev_stick_keys_;
        keysHeld |= stick_keys;
        prev_stick_keys_ = stick_keys;

        if (access_recovery_visible()) {
            return handle_parent_input(keysDown, keysHeld, dpad_down, touch,
                left, right, input_elapsed_ms);
        }

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
                ptc_overlay_hold_reset(&confirm_hold_);
                ptc_overlay_input_init(input_);
            } else if (touch_confirm && dangerous) {
                touch_hold_warning_ = true;
                ptc_overlay_hold_reset(&confirm_hold_);
            } else if ((!dangerous && ((keysDown & (HidNpadButton_A | HidNpadButton_Plus)) || touch_confirm))) {
                touch_hold_warning_ = false;
                ptc_overlay_hold_reset(&confirm_hold_);
                (void)begin_preview_request(true);
            } else if (dangerous && (keysHeld & HidNpadButton_A)) {
                if (ptc_overlay_hold_update(&confirm_hold_, true, input_elapsed_ms, 1000)) {
                    touch_hold_warning_ = false;
                    (void)begin_preview_request(true);
                }
            } else {
                ptc_overlay_hold_reset(&confirm_hold_);
            }
            prev_touch_down_ = touch_down;
            return true;
        }

        // 触屏交互处理。
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

            if (!status_expanded_ && ptc_overlay_rect_contains(
                    ptc_overlay_child_buffer_rect(cx, cy), rel_x, rel_y)) {
                if (request_actions_enabled && displayed_summary_.valid &&
                    displayed_summary_.daily_buffer_available && !bedtime_restricted())
                    (void)begin_claim_daily_buffer();
                prev_touch_down_ = touch_down;
                return true;
            }
            if (!status_expanded_ && ptc_overlay_rect_contains(
                    ptc_overlay_child_parent_rect(cx, cy, PTC_OVERLAY_CONTENT_W), rel_x, rel_y)) {
                open_parent_pin();
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

        if (keysDown & HidNpadButton_Minus) {
            status_expanded_ = !status_expanded_;
            return true;
        }

        if (keysDown & HidNpadButton_L) {
            if (request_actions_enabled && displayed_summary_.valid &&
                displayed_summary_.daily_buffer_available && !bedtime_restricted())
                (void)begin_claim_daily_buffer();
            return true;
        }

        if (keysDown & HidNpadButton_R) {
            open_parent_pin();
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
                return "正在读取后台结果...";
            return "正在等待后台处理...";
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

    bool begin_overlay_ready()
    {
        char boot_id[32];
        char fingerprint[65];
        if (!bridge_ || bridge_->waiting) return false;
        environment_fingerprint(bridge_, fingerprint);
        std::snprintf(boot_id, sizeof(boot_id), "overlay-%016llx",
            static_cast<unsigned long long>(armGetSystemTick()));
        PtcCompanionStatus status = ptc_overlay_bridge_submit_overlay_ready(
            bridge_, static_cast<int64_t>(std::time(nullptr)), ++request_nonce_,
            PLAYWISE_BUILD_RELEASE_ID, boot_id, fingerprint);
        if (status != PTC_COMPANION_OK) {
            error_ = true;
            last_request_kind_ = OverlayRequestKind::OverlayReady;
            return false;
        }
        active_request_kind_ = OverlayRequestKind::OverlayReady;
        last_request_kind_ = OverlayRequestKind::OverlayReady;
        request_started_tick_ = armGetSystemTick();
        last_elapsed_ms_ = 0;
        error_ = false;
        return true;
    }

    static bool is_access_recovery_request(OverlayRequestKind kind)
    {
        return kind == OverlayRequestKind::ClaimDailyBuffer ||
            kind == OverlayRequestKind::AddTodayMinutes ||
            kind == OverlayRequestKind::DisableTodayLimit ||
            kind == OverlayRequestKind::SkipBedtime ||
            kind == OverlayRequestKind::ClearBedtimeSkip ||
            kind == OverlayRequestKind::DisableBedtime ||
            kind == OverlayRequestKind::RestoreInstallSnapshot;
    }

    bool begin_claim_daily_buffer()
    {
        if (!bridge_ || bridge_->waiting) return false;
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        PtcCompanionStatus status = ptc_overlay_bridge_claim_daily_buffer(
            bridge_, now, ++request_nonce_);
        if (status != PTC_COMPANION_OK) {
            error_ = true;
            last_request_kind_ = OverlayRequestKind::ClaimDailyBuffer;
            parent_view_ = ParentView::Result;
            parent_action_succeeded_ = false;
            return false;
        }
        active_request_kind_ = OverlayRequestKind::ClaimDailyBuffer;
        last_request_kind_ = OverlayRequestKind::ClaimDailyBuffer;
        request_started_tick_ = armGetSystemTick();
        last_elapsed_ms_ = 0;
        error_ = false;
        parent_view_ = ParentView::Result;
        parent_action_succeeded_ = false;
        return true;
    }

    bool bedtime_restricted() const
    {
        return displayed_summary_.valid && displayed_summary_.bedtime_active &&
            !displayed_summary_.bedtime_skipped;
    }

    bool access_recovery_visible() const
    {
        return parent_view_ != ParentView::Child;
    }

    void open_parent_pin()
    {
        parent_view_ = ParentView::Pin;
        ptc_overlay_hold_reset(&confirm_hold_);
        parent_authorized_ = false;
        std::memset(pin_, 0, sizeof(pin_));
        pin_length_ = 0;
        pin_message_[0] = '\0';
        left_pin_latched_ = -1;
        right_pin_latched_ = -1;
        if ((!has_status_snapshot_ || status_is_stale()) && !bridge_->waiting)
            (void)begin_status_refresh();
    }

    const char *parent_action_reason(PtcOverlayParentAction action) const
    {
        if (bridge_->waiting) return "后台处理中";
        if (action != PTC_OVERLAY_PARENT_RESTORE_SNAPSHOT &&
            (!has_status_snapshot_ || status_is_stale())) return "请先刷新状态";
        return ptc_overlay_parent_action_unavailable_reason(&displayed_summary_, action);
    }

    bool parent_action_requires_hold(PtcOverlayParentAction action) const
    {
        if (action == PTC_OVERLAY_PARENT_ADD_MINUTES ||
            action == PTC_OVERLAY_PARENT_SKIP_BEDTIME ||
            action == PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP) return false;
        return true;
    }

    bool submit_parent_action()
    {
        if (!bridge_ || bridge_->waiting || !parent_authorized_) return false;
        const auto action = static_cast<PtcOverlayParentAction>(parent_action_);
        if (parent_action_reason(action)) return false;
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        PtcCompanionStatus status = PTC_COMPANION_BAD_ARGUMENT;
        OverlayRequestKind kind = OverlayRequestKind::None;
        parent_authorized_ = false;
        switch (action) {
        case PTC_OVERLAY_PARENT_ADD_MINUTES:
            kind = OverlayRequestKind::AddTodayMinutes;
            status = ptc_overlay_bridge_add_today_minutes(bridge_, now, ++request_nonce_,
                static_cast<uint16_t>(daily_add_minutes_));
            break;
        case PTC_OVERLAY_PARENT_UNLIMITED:
            kind = OverlayRequestKind::DisableTodayLimit;
            status = ptc_overlay_bridge_disable_today_limit(bridge_, now, ++request_nonce_);
            break;
        case PTC_OVERLAY_PARENT_SKIP_BEDTIME:
            kind = OverlayRequestKind::SkipBedtime;
            status = ptc_overlay_bridge_skip_bedtime(bridge_, now, ++request_nonce_,
                ptc_overlay_parent_skip_instance_id(&displayed_summary_));
            break;
        case PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP:
            kind = OverlayRequestKind::ClearBedtimeSkip;
            status = ptc_overlay_bridge_clear_bedtime_skip(bridge_, now, ++request_nonce_,
                displayed_summary_.bedtime_skipped_window_instance_id);
            break;
        case PTC_OVERLAY_PARENT_DISABLE_BEDTIME:
            kind = OverlayRequestKind::DisableBedtime;
            status = ptc_overlay_bridge_disable_bedtime(bridge_, now, ++request_nonce_);
            break;
        case PTC_OVERLAY_PARENT_RESTORE_SNAPSHOT:
            kind = OverlayRequestKind::RestoreInstallSnapshot;
            status = ptc_overlay_bridge_restore_install_snapshot(bridge_, now, ++request_nonce_);
            break;
        default:
            return false;
        }
        active_request_kind_ = kind;
        last_request_kind_ = kind;
        parent_view_ = ParentView::Result;
        parent_action_succeeded_ = false;
        ptc_overlay_hold_reset(&confirm_hold_);
        request_started_tick_ = armGetSystemTick();
        last_elapsed_ms_ = 0;
        error_ = status != PTC_COMPANION_OK;
        return status == PTC_COMPANION_OK;
    }

    void append_pin_digit(int digit)
    {
        if (digit < 0 || digit > 9 || pin_length_ >= PTC_AUTH_PIN_MAX_LEN) return;
        pin_[pin_length_++] = static_cast<char>('0' + digit);
        pin_[pin_length_] = '\0';
        pin_message_[0] = '\0';
    }

    bool handle_parent_input(u64 keysDown, u64 keysHeld, u64 dpad_down,
        const HidTouchState &touch, HidAnalogStickState left,
        HidAnalogStickState right, int elapsed_ms)
    {
        const bool touch_down = touch.x != 0 || touch.y != 0;
        const bool touch_pressed = touch_down && !prev_touch_down_;
        const s32 tx = touch.x > 400 ? static_cast<s32>(touch.x) - 880 : static_cast<s32>(touch.x);
        const s32 ty = static_cast<s32>(touch.y);
        prev_touch_down_ = touch_down;
        constexpr s32 cx = PTC_OVERLAY_CONTENT_X;
        constexpr s32 cy = PTC_OVERLAY_CONTENT_Y;
        if (parent_view_ == ParentView::Pin) {
            const int left_digit = ptc_overlay_pin_digit_from_vector(left.x, left.y, 16000);
            const int right_digit = ptc_overlay_pin_digit_from_vector(right.x, right.y, 16000);
            if (left_digit < 0) left_pin_latched_ = -1;
            if (right_digit < 0) right_pin_latched_ = -1;
            if (left_digit >= 0 && left_pin_latched_ < 0) {
                append_pin_digit(left_digit);
                left_pin_latched_ = left_digit;
            } else if (left_digit < 0 && right_digit >= 0 && right_pin_latched_ < 0) {
                append_pin_digit(right_digit);
                right_pin_latched_ = right_digit;
            }
            const int direction_digit = ptc_overlay_pin_digit_from_direction(
                to_overlay_buttons(dpad_down));
            if (direction_digit >= 0) append_pin_digit(direction_digit);
            if (touch_pressed) {
                if (ptc_overlay_rect_contains(ptc_overlay_backspace_rect(cx, cy), tx, ty))
                    keysDown |= HidNpadButton_ZL;
                else if (tx >= cx && tx < cx + PTC_OVERLAY_CONTENT_W &&
                         ty >= cy + 390 && ty < cy + 436)
                    keysDown |= HidNpadButton_Plus;
                else if (ty >= cy + 500 && ty < cy + 550)
                    keysDown |= HidNpadButton_B;
            }
            if (keysDown & HidNpadButton_X) append_pin_digit(0);
            else if (keysDown & HidNpadButton_Y) append_pin_digit(9);
            else if ((keysDown & HidNpadButton_ZL) && pin_length_ > 0) {
                pin_[--pin_length_] = '\0';
            } else if (keysDown & HidNpadButton_B) {
                parent_view_ = ParentView::Child;
                std::memset(pin_, 0, sizeof(pin_));
                pin_length_ = 0;
            } else if (keysDown & HidNpadButton_Plus) {
                if (pin_length_ == 0) {
                    std::snprintf(pin_message_, sizeof(pin_message_), "请先输入 PIN");
                    return true;
                }
                int64_t retry_after = 0;
                PtcAuthStatus auth_status = ptc_companion_auth_verify_pin(
                    &auth_, pin_, static_cast<int64_t>(std::time(nullptr)), &retry_after);
                std::memset(pin_, 0, sizeof(pin_));
                pin_length_ = 0;
                if (auth_status == PTC_AUTH_OK) {
                    parent_authorized_ = true;
                    parent_action_ = 0;
                    parent_view_ = ParentView::Actions;
                    pin_message_[0] = '\0';
                    action_repeat_ = {};
                    if ((!has_status_snapshot_ || status_is_stale()) && !bridge_->waiting)
                        (void)begin_status_refresh();
                } else if (auth_status == PTC_AUTH_COOLDOWN) {
                    std::snprintf(pin_message_, sizeof(pin_message_), "PIN 已冷却，请等待 %lld 秒",
                        static_cast<long long>(retry_after));
                } else if (auth_status == PTC_AUTH_EMPTY) {
                    std::snprintf(pin_message_, sizeof(pin_message_), "未设置 PIN；只能使用外部恢复");
                } else {
                    std::snprintf(pin_message_, sizeof(pin_message_), "PIN 错误或认证数据不可用");
                }
            }
            return true;
        }
        if (parent_view_ == ParentView::Actions) {
            bool action_changed = false;
            if (touch_pressed && tx >= cx + 246 && tx < cx + PTC_OVERLAY_CONTENT_W - 12 &&
                ty >= cy + 58 && ty < cy + 104) keysDown |= HidNpadButton_Y;
            if ((keysDown & HidNpadButton_Y) && !bridge_->waiting)
                (void)begin_status_refresh();
            const unsigned int direction = ptc_overlay_direction_step(&action_repeat_,
                to_overlay_buttons(keysDown), to_overlay_buttons(keysHeld), elapsed_ms);
            if (touch_pressed) {
                for (int index = 0; index < PTC_OVERLAY_PARENT_ACTION_COUNT; ++index) {
                    if (tx >= cx + 12 && tx < cx + PTC_OVERLAY_CONTENT_W - 12 &&
                        ty >= cy + 135 + index * 52 && ty < cy + 181 + index * 52) {
                        const bool was_selected = parent_action_ == index;
                        parent_action_ = index;
                        ptc_overlay_hold_reset(&confirm_hold_);
                        action_changed = true;
                        if (was_selected && !parent_action_requires_hold(
                                static_cast<PtcOverlayParentAction>(index))) keysDown |= HidNpadButton_A;
                        break;
                    }
                }
                if (ty >= cy + 500 && ty < cy + 550) keysDown |= HidNpadButton_B;
            }
            if (parent_action_ == PTC_OVERLAY_PARENT_ADD_MINUTES && (direction & PTC_OVERLAY_BUTTON_LEFT))
                daily_add_minutes_ = daily_add_minutes_ <= 5 ? 5 : daily_add_minutes_ - 5;
            if (parent_action_ == PTC_OVERLAY_PARENT_ADD_MINUTES && (direction & PTC_OVERLAY_BUTTON_RIGHT))
                daily_add_minutes_ = daily_add_minutes_ >= 120 ? 120 : daily_add_minutes_ + 5;
            if (direction & PTC_OVERLAY_BUTTON_UP) {
                parent_action_ = (parent_action_ + PTC_OVERLAY_PARENT_ACTION_COUNT - 1) %
                    PTC_OVERLAY_PARENT_ACTION_COUNT;
                ptc_overlay_hold_reset(&confirm_hold_);
                action_changed = true;
            }
            if (direction & PTC_OVERLAY_BUTTON_DOWN) {
                parent_action_ = (parent_action_ + 1) % PTC_OVERLAY_PARENT_ACTION_COUNT;
                ptc_overlay_hold_reset(&confirm_hold_);
                action_changed = true;
            }
            if (keysDown & HidNpadButton_B) {
                parent_authorized_ = false;
                parent_view_ = ParentView::Child;
                ptc_overlay_hold_reset(&confirm_hold_);
            } else if (!parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_)) &&
                       !parent_action_requires_hold(static_cast<PtcOverlayParentAction>(parent_action_)) &&
                       (keysDown & HidNpadButton_A)) {
                if (parent_action_ == PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP) {
                    parent_view_ = ParentView::Confirm;
                    parent_confirm_armed_ = false;
                    ptc_overlay_hold_reset(&confirm_hold_);
                } else {
                    (void)submit_parent_action();
                }
            } else if (!parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_)) &&
                       parent_action_requires_hold(static_cast<PtcOverlayParentAction>(parent_action_)) &&
                       !action_changed && (keysHeld & HidNpadButton_A)) {
                if (ptc_overlay_hold_update(&confirm_hold_, true, elapsed_ms, 1000))
                    (void)submit_parent_action();
            } else {
                ptc_overlay_hold_reset(&confirm_hold_);
            }
            return true;
        }
        if (parent_view_ == ParentView::Confirm) {
            const bool immediate = displayed_summary_.bedtime_active &&
                displayed_summary_.bedtime_skipped;
            if (!(keysHeld & HidNpadButton_A)) parent_confirm_armed_ = true;
            if (touch_pressed && ty >= cy + 480 && ty < cy + 540) {
                if (tx < cx + 184) keysDown |= HidNpadButton_B;
                else if (!immediate && parent_confirm_armed_) keysDown |= HidNpadButton_A;
            }
            if (keysDown & HidNpadButton_B) {
                parent_view_ = ParentView::Actions;
                ptc_overlay_hold_reset(&confirm_hold_);
            } else if (!parent_action_reason(PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP) &&
                       !immediate && parent_confirm_armed_ && (keysDown & HidNpadButton_A)) {
                (void)submit_parent_action();
            } else if (!parent_action_reason(PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP) &&
                       immediate && parent_confirm_armed_ && (keysHeld & HidNpadButton_A)) {
                if (ptc_overlay_hold_update(&confirm_hold_, true, elapsed_ms, 1000))
                    (void)submit_parent_action();
            } else {
                ptc_overlay_hold_reset(&confirm_hold_);
            }
            return true;
        }
        if (touch_pressed && ty >= cy + 480 && ty < cy + 540) {
            if (last_request_kind_ == OverlayRequestKind::ClaimDailyBuffer || tx >= cx + 184)
                keysDown |= HidNpadButton_B;
            else
                keysDown |= HidNpadButton_Y;
        }
        if (keysDown & HidNpadButton_B) {
            parent_view_ = ParentView::Child;
            error_ = false;
        } else if ((keysDown & HidNpadButton_Y) && !bridge_->waiting) {
            if (last_request_kind_ == OverlayRequestKind::ClaimDailyBuffer) {
                parent_view_ = ParentView::Child;
                error_ = false;
            } else {
                open_parent_pin();
            }
        }
        return true;
    }

    bool begin_code_preview()
    {
        char code[32];
        if (bedtime_restricted()) return false;
        if (has_status_snapshot_ && !status_is_stale() &&
            displayed_summary_.unrestricted_today == 1) return false;
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
        if (!bridge_ || bridge_->waiting || pending_code_[0] == '\0' ||
            bedtime_restricted()) return false;
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
        if (kind == OverlayRequestKind::ClaimDailyBuffer) return "领取自主缓冲";
        if (kind == OverlayRequestKind::ClearBedtimeSkip) return "恢复本次就寝";
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
        draw_localized(renderer, preview_changed_ ? "状态已变化，请再次确认" : "确认兑换加时码",
                             false, cx + 14, cy + 52, 18, renderer->a(TEXT_COLOR));
        std::snprintf(line, sizeof(line), "本次增加 %d 分钟", preview_summary_.grant_minutes);
        draw_localized(renderer, line, false, cx + 14, cy + 84, 15, renderer->a(FOCUS_BORDER));
        draw_localized(renderer, "今天有效，成功兑换后只能使用一次", false,
                             cx + 14, cy + 110, 12, renderer->a(MUTED_COLOR));

        renderer->drawRect(cx + 12, cy + 142, cw - 24, 86, renderer->a(CARD_COLOR));
        draw_localized(renderer, "今天还可玩", false, cx + 24, cy + 168, 12, renderer->a(MUTED_COLOR));
        if (preview_summary_.converts_unlimited_to_limited) {
            draw_localized(renderer, "不限时", false, cx + 155, cy + 171, 19, renderer->a(SUCCESS_COLOR));
        } else if (preview_summary_.remaining_available) {
            std::snprintf(line, sizeof(line), "%d 分钟", preview_summary_.remaining_minutes);
            draw_localized(renderer, line, false, cx + 155, cy + 171, 19, renderer->a(SUCCESS_COLOR));
        } else {
            draw_localized(renderer, "暂不可用", false, cx + 155, cy + 171, 16, renderer->a(MUTED_COLOR));
        }

        renderer->drawRect(cx + 12, cy + 244, cw - 24, 86, renderer->a(CARD_COLOR));
        draw_localized(renderer, "兑换后预计还可玩", false, cx + 24, cy + 270, 12, renderer->a(MUTED_COLOR));
        if (preview_summary_.remaining_after_available) {
            std::snprintf(line, sizeof(line), "%d 分钟", preview_summary_.remaining_after_minutes);
            draw_localized(renderer, line, false, cx + 155, cy + 273, 19,
                                 renderer->a(preview_summary_.remaining_after_minutes == 0 ? ERROR_COLOR : SUCCESS_COLOR));
        } else {
            draw_localized(renderer, "暂不可用", false, cx + 155, cy + 273, 16, renderer->a(ERROR_COLOR));
        }

        const PtcOverlayPreviewVisualLevel preview_level = ptc_overlay_preview_visual_level(
            preview_summary_.remaining_after_available,
            preview_summary_.remaining_after_minutes,
            preview_summary_.preview_capped,
            preview_summary_.converts_unlimited_to_limited);
        const bool dangerous = preview_level == PTC_OVERLAY_PREVIEW_DANGER;
        if (preview_level != PTC_OVERLAY_PREVIEW_NEUTRAL) {
            const tsl::Color alert_bg = dangerous ? DANGER_BG : WARNING_BG;
            const tsl::Color alert_accent = dangerous ? ERROR_COLOR : WAITING_COLOR;
            renderer->drawRect(cx + 12, cy + 342, cw - 24, 50, renderer->a(alert_bg));
            draw_outline(renderer, cx + 12, cy + 342, cw - 24, 50, 2, alert_accent);
        }
        if (preview_summary_.converts_unlimited_to_limited) {
            draw_localized(renderer, "警告：兑换后将从不限时改为限时", false, cx + 22, cy + 372, 13, renderer->a(ERROR_COLOR));
        } else if (!preview_summary_.remaining_after_available) {
            draw_localized(renderer, "警告：暂时无法确认兑换后的可玩时间", false, cx + 22, cy + 372, 13, renderer->a(ERROR_COLOR));
        } else if (preview_summary_.remaining_after_minutes == 0) {
            draw_localized(renderer, "警告：兑换后预计没有可玩时间", false, cx + 22, cy + 372, 13, renderer->a(ERROR_COLOR));
        } else if (preview_summary_.preview_capped) {
            std::snprintf(line, sizeof(line), "受每日上限影响，实际增加 %d 分钟", preview_summary_.effective_add_minutes);
            draw_localized(renderer, line, false, cx + 22, cy + 372, 13, renderer->a(WAITING_COLOR));
        } else {
            draw_localized(renderer, "确认前不会消费这枚加时码", false, cx + 16, cy + 366, 13, renderer->a(MUTED_COLOR));
        }
        draw_localized(renderer, touch_hold_warning_ ? "请使用手柄长按 A 确认" :
                             (dangerous ? "长按 A 1 秒确认；B 取消" : "A / + 确认；B 取消"),
                             false, cx + 16, cy + 414, 13,
                             renderer->a(dangerous ? ERROR_COLOR : FOCUS_BORDER));
        renderer->drawRect(cx, cy + 500, 145, 50, renderer->a(CARD_COLOR));
        draw_outline(renderer, cx, cy + 500, 145, 50, 1, MUTED_COLOR);
        draw_localized(renderer, "B 取消", false, cx + 45, cy + 530, 14, renderer->a(TEXT_COLOR));
        renderer->drawRect(cx + 170, cy + 500, 160, 50, renderer->a(FOCUS_BG));
        draw_outline(renderer, cx + 170, cy + 500, 160, 50, 2, FOCUS_BORDER);
        draw_localized(renderer, dangerous ? "长按 A 确认" : "A 确认", false,
                             cx + 202, cy + 530, 14, renderer->a(TEXT_COLOR));
        if (dangerous) {
            const int progress = confirm_hold_.fired ? 0 :
                ptc_overlay_hold_progress(&confirm_hold_, 1000);
            renderer->drawRect(cx + 176, cy + 541, 148, 4, renderer->a(CARD_COLOR));
            if (progress > 0)
                renderer->drawRect(cx + 176, cy + 541, 148 * progress / 1000, 4,
                    renderer->a(ERROR_COLOR));
        }
    }

    void draw_code_success(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw)
    {
        char line[128];
        renderer->drawRect(cx, cy + 36, cw, 460, renderer->a(PANEL_COLOR));
        const tsl::Color accent = result_failed_ ? ERROR_COLOR : SUCCESS_COLOR;
        draw_outline(renderer, cx, cy + 36, cw, 460, 2, accent);
        draw_localized(renderer, result_pending_ ? "加时结果确认中" : (result_failed_ ? "兑换未成功" : "加时成功"),
                             false, cx + 14, cy + 74, 22, renderer->a(accent));
        std::snprintf(line, sizeof(line), result_pending_ ? "预计增加 %d 分钟" :
                      (result_failed_ ? "原计划增加 %d 分钟" : "已增加 %d 分钟"),
                      preview_summary_.grant_minutes);
        draw_localized(renderer, line, false, cx + 14, cy + 108, 15, renderer->a(TEXT_COLOR));
        draw_localized(renderer, result_pending_ ? "正在核对最终结果，请勿重复输入这枚加时码" :
                             (result_failed_ ? "后台已确认失败；该码未消费，可重新输入" :
                              "该加时码已经使用，不能再次使用"),
                             false, cx + 14, cy + 136, 12, renderer->a(MUTED_COLOR));
        draw_localized(renderer, "兑换前", false, cx + 18, cy + 190, 12, renderer->a(MUTED_COLOR));
        if (redemption_before_.converts_unlimited_to_limited) std::snprintf(line, sizeof(line), "不限时");
        else if (redemption_before_.remaining_available) std::snprintf(line, sizeof(line), "%d 分钟", redemption_before_.remaining_minutes);
        else std::snprintf(line, sizeof(line), "暂不可用");
        draw_localized(renderer, line, false, cx + 130, cy + 193, 18, renderer->a(TEXT_COLOR));
        draw_localized(renderer, result_pending_ ? "预览兑换后" : "实际兑换后", false, cx + 18, cy + 254, 12, renderer->a(MUTED_COLOR));
        const PtcCompanionResultSummary &after = result_pending_ ? preview_summary_ : displayed_summary_;
        if (result_pending_ ? after.remaining_after_available : after.remaining_available) {
            std::snprintf(line, sizeof(line), "%d 分钟",
                          result_pending_ ? after.remaining_after_minutes : after.remaining_minutes);
        }
        else std::snprintf(line, sizeof(line), "暂不可用");
        draw_localized(renderer, line, false, cx + 130, cy + 257, 18, renderer->a(accent));
        draw_localized(renderer, result_pending_ ? "可关闭界面；下次打开会继续确认" :
                             (result_failed_ ? "失败结果已确认" : "结果已确认并保存"),
                             false, cx + 18, cy + 324, 14, renderer->a(accent));
        renderer->drawRect(cx + 30, cy + 392, cw - 60, 56, renderer->a(FOCUS_BG));
        draw_outline(renderer, cx + 30, cy + 392, cw - 60, 56, 2, FOCUS_BORDER);
        draw_localized(renderer, "A / B  返回空白输入页", false, cx + 82, cy + 426, 14, renderer->a(TEXT_COLOR));
    }

    void draw_parent_page(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw)
    {
        char line[160];
        renderer->drawRect(cx, cy + 18, cw, 548, renderer->a(PANEL_COLOR));
        draw_outline(renderer, cx, cy + 18, cw, 548, 2, FOCUS_BORDER);
        draw_localized(renderer, parent_view_ == ParentView::Pin ? "家长 PIN 验证" :
            (parent_view_ == ParentView::Actions ? "家长区" :
             (parent_view_ == ParentView::Confirm ? "确认恢复限制" : "操作结果")),
            false, cx + 14, cy + 56, 21, renderer->a(TEXT_COLOR));
        const char *state = (!has_status_snapshot_ || status_is_stale()) ? "状态待刷新" :
            (bedtime_restricted() ? "就寝限制生效中" :
             (displayed_summary_.daily_restriction_active ? "今日额度已耗尽" : "当前可正常使用"));
        draw_localized(renderer, state, false, cx + 14, cy + 88, 13,
            renderer->a(bedtime_restricted() ? ERROR_COLOR : MUTED_COLOR));

        if (parent_view_ == ParentView::Pin) {
            char masked[PTC_AUTH_PIN_MAX_LEN + 1];
            (void)ptc_overlay_pin_mask(pin_length_, masked, sizeof(masked));
            std::snprintf(line, sizeof(line), "已输入 %u 位  %s",
                static_cast<unsigned int>(pin_length_), masked);
            draw_localized(renderer, line, false, cx + 14, cy + 152, 15, renderer->a(TEXT_COLOR), 330);
            draw_localized(renderer, "摇杆八方向输入 1 到 8；十字键输入 1/3/5/7", false,
                cx + 14, cy + 180, 12, renderer->a(MUTED_COLOR));
            renderer->drawRect(cx, cy + PTC_OVERLAY_KEYPAD_Y, cw,
                PTC_OVERLAY_KEYPAD_H, renderer->a(CARD_COLOR));
            static constexpr const char *DIRECTIONS[8] = {
                "8 左上", "1 上", "2 右上", "7 左", "3 右",
                "6 左下", "5 下", "4 右下"
            };
            static constexpr int COLS[8] = {0, 1, 2, 0, 2, 0, 1, 2};
            static constexpr int ROWS[8] = {0, 0, 0, 1, 1, 2, 2, 2};
            for (int i = 0; i < 8; ++i) {
                const s32 x = cx + 12 + COLS[i] * 117;
                const s32 y = cy + PTC_OVERLAY_KEYPAD_Y + 4 + ROWS[i] * 44;
                renderer->drawRect(x, y, 105, 42, renderer->a(KEY_COLOR));
                draw_localized(renderer, DIRECTIONS[i], false, x + 12, y + 27, 13,
                    renderer->a(TEXT_COLOR));
            }
            draw_localized(renderer, "X 0    Y 9", false, cx + 142, cy + 267, 15,
                renderer->a(TEXT_COLOR));
            const PtcOverlayRect backspace = ptc_overlay_backspace_rect(cx, cy);
            draw_localized(renderer, "ZL 退格", false, backspace.x + 14, backspace.y + 27, 13,
                renderer->a(BACKSPACE_BORDER));
            renderer->drawRect(cx, cy + 390, cw, 46, renderer->a(FOCUS_BG));
            draw_localized(renderer, "+ 验证 PIN", false, cx + 125, cy + 420, 16,
                renderer->a(TEXT_COLOR));
            if (pin_message_[0]) draw_localized(renderer, pin_message_, false,
                cx + 14, cy + 468, 13, renderer->a(ERROR_COLOR), 330);
            draw_localized(renderer, "B 返回加时码页", false, cx + 14, cy + 532, 14,
                renderer->a(MUTED_COLOR));
            return;
        }

        if (parent_view_ == ParentView::Actions) {
            static constexpr const char *LABELS[PTC_OVERLAY_PARENT_ACTION_COUNT] = {
                "快速加时", "今日不限时", "跳过本次就寝", "恢复本次就寝", "关闭就寝计划",
                "恢复安装前设置并停用 PlayWise"
            };
            renderer->drawRect(cx + 246, cy + 58, cw - 258, 46,
                renderer->a(bridge_->waiting ? DISABLED_COLOR : CARD_COLOR));
            draw_localized(renderer, "Y 刷新", false, cx + 264, cy + 87, 13,
                renderer->a(bridge_->waiting ? MUTED_COLOR : FOCUS_BORDER));
            for (int i = 0; i < PTC_OVERLAY_PARENT_ACTION_COUNT; ++i) {
                const s32 y = cy + 135 + i * 52;
                const bool selected = i == parent_action_;
                const char *reason = parent_action_reason(
                    static_cast<PtcOverlayParentAction>(i));
                renderer->drawRect(cx + 12, y, cw - 24, 46,
                    renderer->a(selected ? FOCUS_BG : CARD_COLOR));
                draw_outline(renderer, cx + 12, y, cw - 24, 46,
                    selected ? 2 : 1, selected ? FOCUS_BORDER : MUTED_COLOR);
                if (i == PTC_OVERLAY_PARENT_ADD_MINUTES)
                    std::snprintf(line, sizeof(line), "快速加时 +%d 分钟（左右调整）",
                        daily_add_minutes_);
                else
                    std::snprintf(line, sizeof(line), "%s", LABELS[i]);
                draw_localized(renderer, line, false, cx + 24, y + 20, 13,
                    renderer->a(reason ? MUTED_COLOR : TEXT_COLOR), 310);
                if (reason) draw_localized(renderer, reason, false, cx + 24, y + 38, 11,
                    renderer->a(WAITING_COLOR), 305);
                if (selected && !reason && parent_action_requires_hold(static_cast<PtcOverlayParentAction>(i))) {
                    const int progress = ptc_overlay_hold_progress(&confirm_hold_, 1000);
                    renderer->drawRect(cx + 20, y + 42, cw - 40, 3, renderer->a(CARD_COLOR));
                    if (progress > 0)
                        renderer->drawRect(cx + 20, y + 42, (cw - 40) * progress / 1000, 3,
                            renderer->a(ERROR_COLOR));
                }
            }
            const auto selected = static_cast<PtcOverlayParentAction>(parent_action_);
            const char *reason = parent_action_reason(selected);
            const bool hold = parent_action_requires_hold(selected);
            draw_localized(renderer, reason ? "该动作当前不可用；B 返回" :
                (hold ? "长按 A 1 秒；Y 刷新；B 返回" : "A 执行；Y 刷新；B 返回"),
                false, cx + 14, cy + 470, 13,
                renderer->a(reason ? WAITING_COLOR : (hold ? ERROR_COLOR : FOCUS_BORDER)));
            return;
        }

        if (parent_view_ == ParentView::Confirm) {
            const bool immediate = displayed_summary_.bedtime_active &&
                displayed_summary_.bedtime_skipped;
            const char *reason = parent_action_reason(PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP);
            draw_localized(renderer, "恢复本次就寝限制？", false, cx + 14, cy + 158, 19,
                renderer->a(TEXT_COLOR), 330);
            draw_localized(renderer, immediate ?
                "当前仍在就寝时段，确认后会立即限制使用并暂停游戏。" :
                "将清除本次跳过；到达该就寝时段后按原计划限制使用。",
                false, cx + 14, cy + 212, 13,
                renderer->a(immediate ? ERROR_COLOR : MUTED_COLOR), 330);
            if (reason) draw_localized(renderer, reason, false, cx + 14, cy + 270, 13,
                renderer->a(WAITING_COLOR), 330);
            renderer->drawRect(cx + 12, cy + 480, 155, 58, renderer->a(CARD_COLOR));
            draw_localized(renderer, "B 取消", false, cx + 35, cy + 515, 13,
                renderer->a(TEXT_COLOR));
            renderer->drawRect(cx + 184, cy + 480, cw - 196, 58,
                renderer->a(immediate ? DISABLED_COLOR : FOCUS_BG));
            draw_localized(renderer, immediate ? "长按 A 1 秒恢复" : "A 确认恢复", false,
                cx + 202, cy + 515, 13,
                renderer->a(immediate ? ERROR_COLOR : TEXT_COLOR));
            if (immediate) {
                const int progress = ptc_overlay_hold_progress(&confirm_hold_, 1000);
                renderer->drawRect(cx + 194, cy + 533, (cw - 216) * progress / 1000, 3,
                    renderer->a(ERROR_COLOR));
            }
            return;
        }

        draw_localized(renderer, bridge_->waiting ? "请求处理中..." :
            (parent_action_succeeded_ ? "操作已完成" : "操作未完成"),
            false, cx + 14, cy + 180, 19,
            renderer->a(bridge_->waiting ? WAITING_COLOR :
                (parent_action_succeeded_ ? SUCCESS_COLOR : ERROR_COLOR)));
        if (!bridge_->waiting && parent_action_succeeded_) {
            if (last_request_kind_ == OverlayRequestKind::ClaimDailyBuffer) {
                std::snprintf(line, sizeof(line), "自主缓冲已领取，增加 %d 分钟",
                    displayed_summary_.daily_buffer_minutes);
                draw_localized(renderer, line, false, cx + 14, cy + 225, 14,
                    renderer->a(SUCCESS_COLOR), 320);
            }
            const char *result = !displayed_summary_.access_recovery_required ?
                "已重读 PCTL：限制原因已消失" :
                (bedtime_restricted() ? "仍受就寝限制" :
                 (displayed_summary_.daily_restriction_active ?
                  "仍受每日额度限制" : "请核对当前限制状态"));
            draw_localized(renderer, result, false, cx + 14, cy + 270, 14,
                renderer->a(displayed_summary_.access_recovery_required ?
                    WAITING_COLOR : SUCCESS_COLOR), 320);
        } else if (!bridge_->waiting) {
            draw_localized(renderer, ptc_overlay_bridge_error_message_zh(bridge_), false,
                cx + 14, cy + 225, 13, renderer->a(ERROR_COLOR), 320);
            if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                std::snprintf(line, sizeof(line), "错误码：%d", bridge_->summary.error_code);
                draw_localized(renderer, line, false, cx + 14, cy + 270, 12,
                    renderer->a(ERROR_COLOR));
            }
            draw_localized(renderer, "后台不可用时请从 SD 或外部环境恢复。", false,
                cx + 14, cy + 324, 12, renderer->a(MUTED_COLOR), 320);
        }
        if (last_request_kind_ != OverlayRequestKind::ClaimDailyBuffer) {
            renderer->drawRect(cx + 12, cy + 480, 155, 58, renderer->a(CARD_COLOR));
            draw_localized(renderer, "Y 重新验证", false, cx + 26, cy + 515, 13,
                renderer->a(FOCUS_BORDER));
        }
        renderer->drawRect(cx + 184, cy + 480, cw - 196, 58,
            renderer->a(CARD_COLOR));
        draw_localized(renderer, "B 返回", false, cx + 223, cy + 515, 13,
            renderer->a(FOCUS_BORDER));
    }
    void draw_overlay(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw, s32 ch)
    {
        (void)ch;
        if (access_recovery_visible()) {
            draw_parent_page(renderer, cx, cy, cw);
            return;
        }
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

        // --- 0. Top Prominent Status Banner (醒目展示额度消耗估算与修改后/当前可玩时长) ---
        const s32 top_banner_y = cy + PTC_OVERLAY_TOP_BANNER_Y;
        const s32 top_banner_h = PTC_OVERLAY_TOP_BANNER_H;
        renderer->drawRect(cx, top_banner_y, cw, top_banner_h, renderer->a(CARD_COLOR));
        draw_outline(renderer, cx, top_banner_y, cw, top_banner_h,
                     remaining_refresh_pending ? 2 : 1,
                     remaining_refresh_pending ? WAITING_COLOR : FOCUS_BORDER);

        char quota_label[32];
        char quota_val[32];
        char quota_note[32];
        ptc_overlay_format_child_quota_parts(&summary,
            quota_label, sizeof(quota_label),
            quota_val, sizeof(quota_val),
            quota_note, sizeof(quota_note));

        draw_localized(renderer, quota_label, false, cx + 10, top_banner_y + 21, 11, renderer->a(MUTED_COLOR));
        draw_localized(renderer, quota_val, false, cx + 70, top_banner_y + 23, 15, renderer->a(TEXT_COLOR));
        if (quota_note[0]) {
            draw_localized(renderer, quota_note, false, cx + 124, top_banner_y + 21, 11, renderer->a(MUTED_COLOR));
        }

        draw_localized(renderer, success_visible_ ? "修改后还可玩" : "今天还可玩", false,
                              cx + 10, top_banner_y + 51, 11, renderer->a(MUTED_COLOR));
        const bool unlimited_today = summary.valid && summary.unrestricted_today == 1;
        const tsl::Color remaining_accent = remaining_refresh_pending ? WAITING_COLOR :
            (summary.valid && (summary.remaining_available || unlimited_today) ? SUCCESS_COLOR : MUTED_COLOR);
        renderer->drawRect(cx + 108, top_banner_y + 34, 104, 34, renderer->a(KEY_COLOR));
        renderer->drawRect(cx + 108, top_banner_y + 66, 104, 2, renderer->a(remaining_accent));
        if (remaining_refresh_pending) {
            draw_localized(renderer, "正在刷新...", false, cx + 116, top_banner_y + 58, 14,
                                 renderer->a(WAITING_COLOR));
        } else if (unlimited_today) {
            draw_localized(renderer, "不限时", false, cx + 126, top_banner_y + 62, 22, renderer->a(SUCCESS_COLOR));
        } else if (summary.valid && summary.remaining_available) {
            std::snprintf(line, sizeof(line), "%d 分钟", summary.remaining_minutes);
            draw_localized(renderer, line, false, cx + 114, top_banner_y + 62, 22, renderer->a(SUCCESS_COLOR));
        } else {
            draw_localized(renderer, "暂不可用", false, cx + 116, top_banner_y + 58, 14, renderer->a(MUTED_COLOR));
        }

        const bool busy = bridge_->waiting;
        renderer->drawRect(cx + PTC_OVERLAY_REFRESH_X, cy + PTC_OVERLAY_REFRESH_Y,
                           PTC_OVERLAY_REFRESH_W, PTC_OVERLAY_REFRESH_H,
                           renderer->a(busy ? DISABLED_COLOR : FOCUS_BG));
        draw_outline(renderer, cx + PTC_OVERLAY_REFRESH_X, cy + PTC_OVERLAY_REFRESH_Y,
                     PTC_OVERLAY_REFRESH_W, PTC_OVERLAY_REFRESH_H, 1,
                     remaining_refresh_pending ? WAITING_COLOR : (busy ? MUTED_COLOR : FOCUS_BORDER));
        draw_localized(renderer, remaining_refresh_pending ? "确认中" : (busy ? "刷新中" : "Y  刷新"),
                             false, cx + PTC_OVERLAY_REFRESH_X + 18,
                             cy + PTC_OVERLAY_REFRESH_Y + 20, 12,
                             renderer->a(remaining_refresh_pending ? WAITING_COLOR :
                                         (busy ? MUTED_COLOR : TEXT_COLOR)));
        draw_localized(renderer, remaining_refresh_pending ? "提交后等待结果" :
                             (status_is_stale() ? "数据可能已过期" : age),
                             false, cx + PTC_OVERLAY_REFRESH_X, top_banner_y + 64, 11,
                             renderer->a(remaining_refresh_pending ? WAITING_COLOR :
                                         (status_is_stale() ? ERROR_COLOR : MUTED_COLOR)));

        // --- 1. Header Prompt & Guidance (受限时间与护眼提醒) ---
        char restriction_guidance[128];
        ptc_overlay_format_child_restriction_guidance(&summary, restriction_guidance, sizeof(restriction_guidance));
        const bool restriction_urgent = (summary.valid &&
            ((summary.bedtime_active && !summary.bedtime_skipped) ||
             summary.daily_restriction_active ||
             (summary.remaining_available && summary.remaining_minutes == 0)));
        draw_localized(renderer, restriction_guidance, false, cx + 5, cy + 94, 12,
            renderer->a(restriction_urgent ? ERROR_COLOR : FOCUS_BORDER), 350);

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
            renderer->drawRect(sx, slot_y, slot_w, slot_h, renderer->a(is_cursor ? FOCUS_BG : CARD_COLOR));
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
                std::snprintf(symbol, sizeof(symbol), " | ");
            }
            const auto symbol_size = draw_localized(renderer,
                symbol, false, 0, 0, 30, tsl::style::color::ColorTransparent);
            draw_localized(renderer, symbol, false,
                sx + (slot_w - static_cast<s32>(symbol_size.first)) / 2,
                slot_y + 36, 30,
                renderer->a(index < input_->length ? TEXT_COLOR : (is_cursor ? FOCUS_BORDER : MUTED_COLOR)));
        }

        char console_date[48];
        format_console_date(summary, console_date, sizeof(console_date));
        std::snprintf(line, sizeof(line), "已输入 %u/8 位  高亮：%c  %s", input_->length,
                      ptc_overlay_input_charset()[input_->cursor], console_date);
        draw_localized(renderer, line, false, cx + 5, cy + 180, 14, renderer->a(MUTED_COLOR));

        // --- 3. Keypad 3x4 Grid (软键盘放大) ---
        const char *charset = ptc_overlay_input_charset();
        const s32 panel_y = cy + PTC_OVERLAY_KEYPAD_Y;
        const s32 panel_h = PTC_OVERLAY_KEYPAD_H;
        renderer->drawRect(cx, panel_y, cw, panel_h, renderer->a(PANEL_COLOR));
        draw_outline(renderer, cx, panel_y, cw, panel_h, 1, MUTED_COLOR);

        // 绘制数字键 0-9
        for (unsigned int index = 0; index < PTC_OVERLAY_KEY_COUNT; ++index) {
            char symbol[2] = { charset[index], '\0' };
            const PtcOverlayRect key = ptc_overlay_key_rect(cx, cy, index);
            const bool focused = (index == input_->cursor);

            renderer->drawRect(key.x, key.y, key.w, key.h, renderer->a(focused ? FOCUS_BG : KEY_COLOR));
            draw_outline(renderer, key.x, key.y, key.w, key.h, focused ? 3 : 1, focused ? FOCUS_BORDER : MUTED_COLOR);
            const auto key_text_size = draw_localized(renderer,
                symbol, false, 0, 0, 24, tsl::style::color::ColorTransparent);
            draw_localized(renderer, symbol, false, key.x + (key.w - static_cast<s32>(key_text_size.first)) / 2,
                                 key.y + 30, 24, renderer->a(focused ? FOCUS_BORDER : TEXT_COLOR));
        }

        // 第四行辅助按键 [X] 退格 和 [Y] 清空
        const PtcOverlayRect backspace = ptc_overlay_backspace_rect(cx, cy);
        renderer->drawRect(backspace.x, backspace.y, backspace.w, backspace.h, renderer->a(KEY_COLOR));
        draw_outline(renderer, backspace.x, backspace.y, backspace.w, backspace.h, 1, BACKSPACE_BORDER);
        draw_localized(renderer, "X 退格", false, backspace.x + 28, backspace.y + 26, 12, renderer->a(BACKSPACE_BORDER));

        const PtcOverlayRect clear = ptc_overlay_clear_rect(cx, cy);
        renderer->drawRect(clear.x, clear.y, clear.w, clear.h, renderer->a(KEY_COLOR));
        draw_outline(renderer, clear.x, clear.y, clear.w, clear.h, 1, CLEAR_BORDER);
        draw_localized(renderer, "点按清空", false, clear.x + 23, clear.y + 26, 12, renderer->a(CLEAR_BORDER));

        // --- 4. Control & Submit Bar (操作与提交栏) ---
        const bool code_unavailable = has_status_snapshot_ && !status_is_stale() &&
            summary.unrestricted_today == 1;
        const bool can_submit = !bedtime_restricted() && !code_unavailable &&
            ptc_overlay_request_action_enabled(bridge_->waiting) &&
            ptc_overlay_input_can_submit(input_);
        const s32 submit_y = cy + PTC_OVERLAY_SUBMIT_Y;
        const s32 submit_h = PTC_OVERLAY_SUBMIT_H;

        // 提交加时大按钮
        renderer->drawRect(cx, submit_y, cw, submit_h, renderer->a(can_submit ? FOCUS_BG : DISABLED_COLOR));
        draw_outline(renderer, cx, submit_y, cw, submit_h, can_submit ? 3 : 1, can_submit ? FOCUS_BORDER : MUTED_COLOR);

        if (can_submit) {
            draw_localized(renderer, "+ 提交加时奖励（点击或按 +）", false, cx + 50, submit_y + 24, 14, renderer->a(TEXT_COLOR));
        } else if (bedtime_restricted()) {
            draw_localized(renderer, "请家长先解除就寝限制；可继续输入加时码", false,
                cx + 32, submit_y + 24, 13, renderer->a(WAITING_COLOR));
        } else if (code_unavailable) {
            draw_localized(renderer, "今日不限时，加时码不可用", false,
                cx + 65, submit_y + 24, 13, renderer->a(WAITING_COLOR));
        } else if (bridge_->waiting) {
            draw_localized(renderer, "后台处理中，可继续编辑输入", false, cx + 66, submit_y + 24, 13,
                                 renderer->a(MUTED_COLOR));
        } else {
            draw_localized(renderer, "+ 提交加时（需输满 8 位数字）", false, cx + 58, submit_y + 24, 13, renderer->a(MUTED_COLOR));
        }

        // --- 5. Collapsible Status Panel (可折叠命令与状态栏) ---
        const s32 status_y = cy + PTC_OVERLAY_STATUS_Y;
        const s32 status_w = cw;

        if (!status_expanded_) {
            renderer->drawRect(cx, status_y, status_w, PTC_OVERLAY_STATUS_COLLAPSED_H, renderer->a(PANEL_COLOR));
            draw_outline(renderer, cx, status_y, status_w, PTC_OVERLAY_STATUS_COLLAPSED_H, 1, MUTED_COLOR);

            if (bridge_->waiting && active_request_kind_ == OverlayRequestKind::Status) {
                draw_localized(renderer, "[-] 正在刷新状态...（按 - 展开）", false, cx + 12, status_y + 21, 12, renderer->a(FOCUS_BORDER));
            } else if (bridge_->waiting) {
                draw_localized(renderer, "[-] 正在处理加时...（按 - 展开）", false, cx + 12, status_y + 21, 12, renderer->a(FOCUS_BORDER));
            } else if (error_) {
                draw_localized(renderer,
                    (last_request_kind_ == OverlayRequestKind::OfflineCode ||
                     last_request_kind_ == OverlayRequestKind::PreviewOfflineCode)
                        ? "[-] 请求失败（按 - 展开，请重新输入）"
                        : "[-] 请求失败（按 - 展开，Y 重试）",
                    false, cx + 12, status_y + 21, 12, renderer->a(ERROR_COLOR));
            } else if (success_visible_) {
                draw_localized(renderer, "[-] 加时成功！（按 - 展开）", false, cx + 12, status_y + 21, 12, renderer->a(SUCCESS_COLOR));
            } else if (has_status_snapshot_) {
                char restriction_summary[128];
                ptc_overlay_format_child_restriction_summary(&summary, restriction_summary, sizeof(restriction_summary));
                draw_localized(renderer, restriction_summary, false, cx + 12, status_y + 21, 12,
                                     renderer->a(MUTED_COLOR));
            } else {
                draw_localized(renderer, "[-] 命令与状态（点击或按 -）", false, cx + 12, status_y + 21, 12, renderer->a(MUTED_COLOR));
            }
        } else {
            const s32 expanded_h = status_needs_detail()
                ? PTC_OVERLAY_STATUS_DETAIL_H : PTC_OVERLAY_STATUS_NORMAL_H;
            renderer->drawRect(cx, status_y, status_w, expanded_h, renderer->a(PANEL_COLOR));
            draw_outline(renderer, cx, status_y, status_w, expanded_h, 2, FOCUS_BORDER);

            if (error_) {
                draw_localized(renderer, "[-] 命令与状态详情（按 - 收起）", false, cx + 12, status_y + 18, 12, renderer->a(FOCUS_BORDER));
                std::snprintf(line, sizeof(line), "%s命令：%s", bridge_->waiting ? "当前" : "最近", request_label());
                draw_localized(renderer, line, false, cx + 12, status_y + 36, 12, renderer->a(TEXT_COLOR));
                draw_localized(renderer, ptc_overlay_bridge_transport_label(bridge_), false, cx + 12, status_y + 52, 11, renderer->a(MUTED_COLOR));
                const char *message = ptc_overlay_bridge_error_message_zh(bridge_);
                draw_localized(renderer, message, false, cx + 12, status_y + 72, 12, renderer->a(ERROR_COLOR), 290);
                const bool code_error = last_request_kind_ == OverlayRequestKind::OfflineCode ||
                    last_request_kind_ == OverlayRequestKind::PreviewOfflineCode;
                if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                    std::snprintf(line, sizeof(line), code_error ? "错误码：%d  |  请重新输入" :
                                  "错误码：%d  |  按 Y 重试", bridge_->summary.error_code);
                    draw_localized(renderer, line, false, cx + 12, status_y + 108, 11, renderer->a(ERROR_COLOR));
                } else {
                    draw_localized(renderer, code_error ? "请重新输入；Y 返回输入" : "按 Y 重试",
                                         false, cx + 12, status_y + 108, 11, renderer->a(ERROR_COLOR));
                }
            } else if (success_visible_) {
                draw_localized(renderer, "[-] 命令与状态详情（按 - 收起）", false, cx + 12, status_y + 18, 12, renderer->a(FOCUS_BORDER));
                std::snprintf(line, sizeof(line), "%s命令：%s", bridge_->waiting ? "当前" : "最近", request_label());
                draw_localized(renderer, line, false, cx + 12, status_y + 36, 12, renderer->a(TEXT_COLOR));
                draw_localized(renderer, ptc_overlay_bridge_transport_label(bridge_), false, cx + 12, status_y + 52, 11, renderer->a(MUTED_COLOR));
                draw_localized(renderer, "加时成功！", false, cx + 12, status_y + 74, 16, renderer->a(SUCCESS_COLOR));
                std::snprintf(line, sizeof(line), "修改后还可玩 %d 分钟", summary.remaining_minutes);
                draw_localized(renderer, line, false, cx + 12, status_y + 96, 15, renderer->a(SUCCESS_COLOR));
                if (summary.played_minutes_available) {
                    std::snprintf(line, sizeof(line), "额度已耗约 %d 分钟（估算），即将自动关闭...", summary.played_minutes);
                    draw_localized(renderer, line, false, cx + 12, status_y + 116, 12, renderer->a(SUCCESS_COLOR));
                } else {
                    draw_localized(renderer, "状态已刷新，即将自动关闭...", false, cx + 12, status_y + 116, 12, renderer->a(SUCCESS_COLOR));
                }
            } else if (bridge_->waiting) {
                draw_localized(renderer, "[-] 命令与状态详情（按 - 收起）", false, cx + 12, status_y + 18, 12, renderer->a(FOCUS_BORDER));
                std::snprintf(line, sizeof(line), "%s命令：%s", "当前", request_label());
                draw_localized(renderer, line, false, cx + 12, status_y + 36, 12, renderer->a(TEXT_COLOR));
                draw_localized(renderer, ptc_overlay_bridge_transport_label(bridge_), false, cx + 12, status_y + 52, 11, renderer->a(MUTED_COLOR));
                const char *stage = transport_stage(bridge_);
                if (stage[0]) {
                    draw_localized(renderer, stage, false, cx + 12, status_y + 72, 12, renderer->a(FOCUS_BORDER));
                }
            } else if (has_status_snapshot_) {
                draw_localized(renderer, "[-] 今日额度与受限详情（按 - 收起）", false, cx + 12, status_y + 18, 12, renderer->a(FOCUS_BORDER));
                char total_str[32];
                if (summary.unrestricted_today == 1) {
                    std::snprintf(total_str, sizeof(total_str), "不限时");
                } else if (summary.remaining_available && summary.played_minutes_available &&
                           summary.remaining_minutes >= 0 && summary.played_minutes >= 0) {
                    std::snprintf(total_str, sizeof(total_str), "%d 分钟",
                        summary.remaining_minutes + summary.played_minutes);
                } else if (summary.remaining_available && summary.remaining_minutes >= 0) {
                    std::snprintf(total_str, sizeof(total_str), "%d 分钟", summary.remaining_minutes);
                } else {
                    std::snprintf(total_str, sizeof(total_str), "-- 分钟");
                }
                const char *rule_lbl = ptc_overlay_rule_source_label(summary.rule_source);
                if (summary.played_minutes_available && summary.played_minutes >= 0) {
                    std::snprintf(line, sizeof(line), "今日总额度：%s（%s）  |  已玩约 %d 分钟",
                        total_str, rule_lbl, summary.played_minutes);
                } else {
                    std::snprintf(line, sizeof(line), "今日总额度：%s（%s）", total_str, rule_lbl);
                }
                draw_localized(renderer, line, false, cx + 12, status_y + 36, 12, renderer->a(TEXT_COLOR));

                char restriction[96];
                ptc_overlay_format_child_restriction_detail(&summary, restriction, sizeof(restriction));
                std::snprintf(line, sizeof(line), "受限：%s", restriction);
                draw_localized(renderer, line, false, cx + 12, status_y + 54, 12, renderer->a(FOCUS_BORDER), 340);

                char buffer_buf[64];
                ptc_overlay_format_child_buffer_status(&summary, buffer_buf, sizeof(buffer_buf));
                std::snprintf(line, sizeof(line), "自主缓冲：%s  |  %s", buffer_buf, ptc_overlay_bridge_transport_label(bridge_));
                draw_localized(renderer, line, false, cx + 12, status_y + 72, 11, renderer->a(MUTED_COLOR));
            } else {
                draw_localized(renderer, "[-] 命令与状态详情（按 - 收起）", false, cx + 12, status_y + 18, 12, renderer->a(FOCUS_BORDER));
                draw_localized(renderer, "尚未获取状态，请按 Y 刷新", false, cx + 12, status_y + 36, 12, renderer->a(MUTED_COLOR));
            }
        }
        if (!status_expanded_) {
            const PtcOverlayRect buffer = ptc_overlay_child_buffer_rect(cx, cy);
            const PtcOverlayRect parent = ptc_overlay_child_parent_rect(cx, cy, cw);
            const bool buffer_ready = summary.valid && summary.daily_buffer_available &&
                !bedtime_restricted() && !bridge_->waiting;
            renderer->drawRect(buffer.x, buffer.y, buffer.w, buffer.h,
                renderer->a(buffer_ready ? FOCUS_BG : CARD_COLOR));
            draw_outline(renderer, buffer.x, buffer.y, buffer.w, buffer.h, 1,
                buffer_ready ? FOCUS_BORDER : MUTED_COLOR);
            draw_localized(renderer, buffer_ready ? "L 领取自主缓冲" :
                (summary.daily_buffer_claimed ? "缓冲今日已领" : "缓冲暂不可领"),
                false, buffer.x + 9, buffer.y + 27, 12,
                renderer->a(buffer_ready ? TEXT_COLOR : MUTED_COLOR));
            renderer->drawRect(parent.x, parent.y, parent.w, parent.h, renderer->a(FOCUS_BG));
            draw_outline(renderer, parent.x, parent.y, parent.w, parent.h, 1, FOCUS_BORDER);
            draw_localized(renderer, "R 家长区", false, parent.x + 29, parent.y + 27, 13,
                renderer->a(TEXT_COLOR));
        }

    }

private:
    PtcOverlayBridge *bridge_;
    PtcOverlayInput *input_;
    PtcCompanionAuth auth_{};
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
    PtcOverlayHoldState confirm_hold_{};
    char pending_code_[9]{};
    PtcPendingRedemption pending_redemption_{};
    u64 recovery_last_poll_tick_ = 0;
    bool prev_touch_down_ = false;
    u64 prev_stick_keys_ = 0;
    ParentView parent_view_ = ParentView::Child;
    bool parent_authorized_ = false;
    bool parent_confirm_armed_ = false;
    bool parent_action_succeeded_ = false;
    int parent_action_ = 0;
    int daily_add_minutes_ = 15;
    int left_pin_latched_ = -1;
    int right_pin_latched_ = -1;
    PtcOverlayDirectionRepeat action_repeat_{};
    char pin_[PTC_AUTH_PIN_MAX_LEN + 1]{};
    size_t pin_length_ = 0;
    char pin_message_[128]{};
};

class PctcOverlay final : public tsl::Overlay {
public:
    void initServices() override
    {
        fsdevMountSdmc();
        ptc_fs_storage_init(&storage_);
        load_overlay_language(ptc_fs_storage_as_storage(&storage_));
        ptc_overlay_bridge_init(&bridge_, APP_ROOT, ptc_fs_storage_as_storage(&storage_));
        ptc_overlay_input_init(&input_);
    }

    void exitServices() override
    {
        ptc_overlay_bridge_exit(&bridge_);
        fsdevUnmountDevice("sdmc");
    }

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
