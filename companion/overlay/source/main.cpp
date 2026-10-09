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
#include "render_types.hpp"

static PtcUiResolvedTheme s_overlay_theme = PTC_UI_RESOLVED_DARK;

static void load_overlay_config(PtcStorage *storage)
{
    char config[2048];
    PtcUiLanguagePreference lang_preference = PTC_UI_LANGUAGE_SYSTEM;
    PtcUiSystemLanguage system_language = PTC_UI_SYSTEM_LANGUAGE_UNKNOWN;
    PtcUiThemePreference theme_preference = PTC_UI_THEME_SYSTEM;
    PtcUiSystemTheme system_theme = PTC_UI_SYSTEM_THEME_UNAVAILABLE;
    u64 code;
    SetLanguage language;
    if (storage && storage->vtable->read_text(storage, "sdmc:/switch/playwise/config.json",
        config, sizeof(config))) {
        cJSON *root = cJSON_Parse(config);
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "ui_language");
        if (cJSON_IsString(item))
            (void)ptc_ui_language_parse_preference(item->valuestring, &lang_preference);
        const cJSON *theme_item = cJSON_GetObjectItemCaseSensitive(root, "theme");
        if (cJSON_IsString(theme_item))
            (void)ptc_ui_theme_parse_preference(theme_item->valuestring, &theme_preference);
        cJSON_Delete(root);
    }
    if (R_SUCCEEDED(setInitialize())) {
        Result result = setGetSystemLanguage(&code);
        if (R_SUCCEEDED(result)) result = setMakeLanguage(code, &language);
        setExit();
        if (R_SUCCEEDED(result)) {
            if (language == SetLanguage_ZHCN || language == SetLanguage_ZHHANS)
                system_language = PTC_UI_SYSTEM_LANGUAGE_SIMPLIFIED;
            else if (language == SetLanguage_ZHTW || language == SetLanguage_ZHHANT)
                system_language = PTC_UI_SYSTEM_LANGUAGE_TRADITIONAL;
            else if (language == SetLanguage_ENUS || language == SetLanguage_ENGB)
                system_language = PTC_UI_SYSTEM_LANGUAGE_ENGLISH;
            else
                system_language = PTC_UI_SYSTEM_LANGUAGE_ENGLISH;
        }
    }
    ptc_ui_language_set_resolved(ptc_ui_language_resolve(lang_preference, system_language));

    if (R_SUCCEEDED(setsysInitialize())) {
        ColorSetId color_set;
        if (R_SUCCEEDED(setsysGetColorSetId(&color_set))) {
            if (color_set == ColorSetId_Light) system_theme = PTC_UI_SYSTEM_THEME_LIGHT;
            else if (color_set == ColorSetId_Dark) system_theme = PTC_UI_SYSTEM_THEME_DARK;
        }
        setsysExit();
    }
    s_overlay_theme = ptc_ui_theme_resolve(theme_preference, system_theme);
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
    PctcGui(PtcOverlayBridge *bridge, PtcOverlayInput *input, PtcUiResolvedTheme theme = s_overlay_theme)
        : bridge_(bridge), input_(input), theme_(theme)
    {
        ptc_companion_auth_init(&auth_, APP_ROOT, bridge_ ? bridge_->transport.file.storage : nullptr);
    }

    tsl::elm::Element *createUI() override
    {
        char title[64], subtitle[64];
        auto frame = new PlayWiseOverlayFrame(
            ptc_ui_localize(ptc_ui_text(PTC_UI_T_PLAYWISE), title, sizeof(title)),
            ptc_ui_localize(ptc_ui_text(PTC_UI_T_PLAYTIME_BEDTIME_CONTROLS), subtitle, sizeof(subtitle)));
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

        const bool is_restricted = has_status_snapshot_ && !status_is_stale() &&
            (displayed_summary_.dock_restriction_active || bedtime_restricted() || eye_care_restricted());

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

            if (is_restricted && !status_expanded_ && ptc_overlay_rect_contains(
                    ptc_overlay_rect(cx, cy + 332, PTC_OVERLAY_CONTENT_W, 76), rel_x, rel_y)) {
                open_parent_pin();
                prev_touch_down_ = touch_down;
                return true;
            }

            if (!status_expanded_ && ptc_overlay_rect_contains(
                    ptc_overlay_child_buffer_rect(cx, cy), rel_x, rel_y)) {
                if (request_actions_enabled && displayed_summary_.valid &&
                    displayed_summary_.daily_buffer_available && !bedtime_restricted() &&
                    !eye_care_restricted())
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

            if (!is_restricted) {
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
                displayed_summary_.daily_buffer_available && !bedtime_restricted() &&
                !eye_care_restricted() && !displayed_summary_.dock_restriction_active)
                (void)begin_claim_daily_buffer();
            return true;
        }

        if (keysDown & HidNpadButton_R) {
            open_parent_pin();
            return true;
        }

        if (keysDown & HidNpadButton_Plus) {
            if (is_restricted) {
                open_parent_pin();
                return true;
            }
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

        if (is_restricted) {
            if (keysDown & HidNpadButton_A) {
                open_parent_pin();
                return true;
            }
            return true;
        }

        return ptc_overlay_input_handle(
            input_,
            to_overlay_buttons(keysDown),
            to_overlay_buttons(keysHeld),
            input_elapsed_ms);
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
                          ptc_ui_text(PTC_UI_T_THE_LAST_GRANT_RECOVERY_INFORMATION_CANNOT_BE));
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
                          ptc_ui_text(PTC_UI_T_THE_LAST_CONFIRMATION_WAS_INTERRUPTED_BEFORE_SUBMISSION));
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
            kind == OverlayRequestKind::SkipEyeCare || kind == OverlayRequestKind::WaiveDock ||
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
        case PTC_OVERLAY_PARENT_WAIVE_DOCK:
            kind = OverlayRequestKind::WaiveDock;
            status = ptc_overlay_bridge_waive_dock(bridge_, now, ++request_nonce_,
                static_cast<uint16_t>(displayed_summary_.day_index));
            break;
        case PTC_OVERLAY_PARENT_SKIP_EYE_CARE:
            kind = OverlayRequestKind::SkipEyeCare;
            status = ptc_overlay_bridge_skip_eye_care(bridge_, now, ++request_nonce_,
                displayed_summary_.eye_care_break_id);
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
                    std::snprintf(pin_message_, sizeof(pin_message_), "%s", ptc_ui_text(PTC_UI_T_PIN_ENTER_FIRST));
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
                    PtcUiTextArg args[] = {{"seconds", nullptr, retry_after, true}};
                    (void)ptc_ui_text_format(PTC_UI_T_PIN_COOLDOWN,
                        pin_message_, sizeof(pin_message_), args, 1);
                } else if (auth_status == PTC_AUTH_EMPTY) {
                    std::snprintf(pin_message_, sizeof(pin_message_), "%s", ptc_ui_text(PTC_UI_T_PIN_MISSING));
                } else {
                    std::snprintf(pin_message_, sizeof(pin_message_), "%s", ptc_ui_text(PTC_UI_T_PIN_INVALID));
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
                    if (ptc_overlay_rect_contains(ptc_overlay_parent_action_rect(cx, cy, index), tx, ty)) {
                        const bool was_selected = parent_action_ == index;
                        parent_action_ = index;
                        ptc_overlay_hold_reset(&confirm_hold_);
                        action_changed = true;
                        if (was_selected && !parent_action_requires_hold(
                                static_cast<PtcOverlayParentAction>(index))) keysDown |= HidNpadButton_A;
                        break;
                    }
                }
                if (ty >= cy + 526 && ty < cy + 557) keysDown |= HidNpadButton_B;
            }
            if (parent_action_ == PTC_OVERLAY_PARENT_ADD_MINUTES && (direction & PTC_OVERLAY_BUTTON_LEFT))
                daily_add_minutes_ = daily_add_minutes_ <= 5 ? 5 : daily_add_minutes_ - 5;
            if (parent_action_ == PTC_OVERLAY_PARENT_ADD_MINUTES && (direction & PTC_OVERLAY_BUTTON_RIGHT))
                daily_add_minutes_ = daily_add_minutes_ >= 120 ? 120 : daily_add_minutes_ + 5;
            if (direction & PTC_OVERLAY_BUTTON_UP) {
                parent_action_ = ptc_overlay_parent_action_move(parent_action_, -1);
                ptc_overlay_hold_reset(&confirm_hold_);
                action_changed = true;
            }
            if (direction & PTC_OVERLAY_BUTTON_DOWN) {
                parent_action_ = ptc_overlay_parent_action_move(parent_action_, 1);
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
                if (parent_action_ == PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP ||
                    parent_action_ == PTC_OVERLAY_PARENT_WAIVE_DOCK) {
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
            const bool immediate = parent_action_ != PTC_OVERLAY_PARENT_WAIVE_DOCK && displayed_summary_.bedtime_active &&
                displayed_summary_.bedtime_skipped;
            if (!(keysHeld & HidNpadButton_A)) parent_confirm_armed_ = true;
            if (touch_pressed && ty >= cy + 480 && ty < cy + 540) {
                if (tx < cx + 184) keysDown |= HidNpadButton_B;
                else if (!immediate && parent_confirm_armed_) keysDown |= HidNpadButton_A;
            }
            if (keysDown & HidNpadButton_B) {
                parent_view_ = ParentView::Actions;
                ptc_overlay_hold_reset(&confirm_hold_);
            } else if (!parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_)) &&
                       !immediate && parent_confirm_armed_ && (keysDown & HidNpadButton_A)) {
                (void)submit_parent_action();
            } else if (!parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_)) &&
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
        if (bedtime_restricted() || eye_care_restricted() || displayed_summary_.dock_restriction_active) return false;
        if (has_status_snapshot_ && !status_is_stale() &&
            (displayed_summary_.unrestricted_today == 1 ||
             (displayed_summary_.eye_care_unlimited_capped || displayed_summary_.dock_unlimited_capped))) return false;
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
                          ptc_ui_text(PTC_UI_T_SUBMISSION_FAILED_THE_GRANT_CODE_HAS_NOT));
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

#include "render_methods.hpp"

private:
    PtcOverlayBridge *bridge_;
    PtcOverlayInput *input_;
    PtcUiResolvedTheme theme_ = PTC_UI_RESOLVED_DARK;
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
        load_overlay_config(ptc_fs_storage_as_storage(&storage_));
        ptc_overlay_bridge_init(&bridge_, APP_ROOT, ptc_fs_storage_as_storage(&storage_));
        ptc_overlay_input_init(&input_);
    }

    void exitServices() override
    {
        ptc_overlay_bridge_exit(&bridge_);
        fsdevUnmountDevice("sdmc");
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<PctcGui>(&bridge_, &input_, s_overlay_theme); }

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
