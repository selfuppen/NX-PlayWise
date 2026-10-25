#define TESLA_INIT_IMPL
#define TESLA_DISABLE_EMOJI_FONT
#include <tesla.hpp>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <limits>

#include "release_manifest.h"
#include "../layout.h"

extern "C" {
#include "../bridge.h"
#include "../../auth.h"
#include "../../auth_clock.h"
#include "../input_model.h"
#include "../../ui_language.h"
#include "../../ui_status_trace.h"
#include "../../input_owner.h"
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
static PtcUiStatusTrace s_status_trace;
static PtcInputOwnerWriter s_input_owner;
static bool s_owner_visible = false;

static bool save_overlay_language_setting(PtcStorage *storage, PtcOverlayLanguageSetting setting)
{
    if (!storage) return false;
    char text[4096];
    cJSON *root = nullptr;
    if (storage->vtable->read_text(storage, "sdmc:/switch/playwise/config.json", text, sizeof(text))) {
        root = cJSON_Parse(text);
        if (!cJSON_IsObject(root)) {
            if (root) cJSON_Delete(root);
            root = cJSON_CreateObject();
        }
    } else {
        root = cJSON_CreateObject();
    }
    cJSON_DeleteItemFromObject(root, "overlay_ui_language");
    cJSON_AddStringToObject(root, "overlay_ui_language", overlay_language_setting_name(setting));
    char *rendered = cJSON_PrintUnformatted(root);
    bool ok = false;
    if (rendered) {
        ok = storage->vtable->write_text_atomic(storage, "sdmc:/switch/playwise/config.json", rendered);
        cJSON_free(rendered);
    }
    cJSON_Delete(root);
    return ok;
}

static void load_overlay_config(PtcStorage *storage)
{
    char config[2048];
    PtcUiLanguagePreference lang_preference = PTC_UI_LANGUAGE_SYSTEM;
    PtcOverlayLanguageSetting ovl_lang_setting = PtcOverlayLanguageSetting::Auto;
    PtcUiSystemLanguage system_language = PTC_UI_SYSTEM_LANGUAGE_UNKNOWN;
    PtcUiThemePreference theme_preference = PTC_UI_THEME_SYSTEM;
    u64 code;
    SetLanguage language;
    if (storage && storage->vtable->read_text(storage, "sdmc:/switch/playwise/config.json",
        config, sizeof(config))) {
        cJSON *root = cJSON_Parse(config);
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "ui_language");
        if (cJSON_IsString(item))
            (void)ptc_ui_language_parse_preference(item->valuestring, &lang_preference);
        const cJSON *ovl_item = cJSON_GetObjectItemCaseSensitive(root, "overlay_ui_language");
        if (cJSON_IsString(ovl_item))
            (void)parse_overlay_language_setting(ovl_item->valuestring, &ovl_lang_setting);
        const cJSON *theme_item = cJSON_GetObjectItemCaseSensitive(root, "theme");
        if (cJSON_IsString(theme_item))
            (void)ptc_ui_theme_parse_preference(theme_item->valuestring, &theme_preference);
        const cJSON *style_item = cJSON_GetObjectItemCaseSensitive(root, "theme_style");
        if (cJSON_IsString(style_item)) {
            PtcUiThemeStyle style;
            if (ptc_ui_theme_parse_style(style_item->valuestring, &style))
                set_overlay_theme_style(style);
        }
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
    s_nro_language_preference = lang_preference;
    s_system_language = system_language;
    set_overlay_language_setting(ovl_lang_setting);
    ptc_ui_language_set_resolved(resolve_overlay_effective_language(
        ovl_lang_setting, lang_preference, system_language));

    // The Tesla overlay environment is universally a dark sidebar overlay (ColorFrameBackground = 0x000D).
    // The overlay UI must always adhere to the dark baseline palette for optimal contrast and seamless aesthetics.
    s_overlay_theme = PTC_UI_RESOLVED_DARK;
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

    void set_parent_view_predicate(std::function<bool()> is_parent)
    {
        is_parent_view_ = std::move(is_parent);
    }

    void draw(tsl::gfx::Renderer *renderer) override
    {
        // PlayWise renders its own compact brand & status bar directly inside draw_overlay.
        // We bypass drawing the default 90px title/subtitle banner in all views to maximize vertical space.
        renderer->fillScreen(a(tsl::style::color::ColorFrameBackground));
        renderer->drawRect(tsl::cfg::FramebufferWidth - 1, 0, 1, tsl::cfg::FramebufferHeight, a(0xF222));
        renderer->drawRect(15, tsl::cfg::FramebufferHeight - 73, tsl::cfg::FramebufferWidth - 30, 1,
            a(tsl::style::color::ColorText));
        draw_overlay_footer_hints(renderer, a(tsl::style::color::ColorText));

        char time_buf[16];
        get_overlay_current_time(time_buf, sizeof(time_buf));
        if (time_buf[0]) {
            const auto measured = renderer->drawString(time_buf, false, 0, 0,
                PTC_OVERLAY_FONT_BTN, tsl::style::color::ColorTransparent);
            const s32 time_x = tsl::cfg::FramebufferWidth - 30 - static_cast<s32>(measured.first);
            renderer->drawString(time_buf, false, time_x, 693, PTC_OVERLAY_FONT_BTN, a(tsl::style::color::ColorText));
        }

        if (m_contentElement != nullptr)
            m_contentElement->frame(renderer);
    }

    void layout(u16 parent_x, u16 parent_y, u16 parent_width, u16 parent_height) override
    {
        setBoundaries(parent_x, parent_y, parent_width, parent_height);
        if (m_contentElement != nullptr) {
            const int content_y = (is_parent_view_ && is_parent_view_())
                ? PTC_OVERLAY_PARENT_CONTENT_Y : PTC_OVERLAY_CONTENT_Y;
            m_contentElement->setBoundaries(
                parent_x + PTC_OVERLAY_CONTENT_X,
                parent_y + content_y,
                PTC_OVERLAY_CONTENT_W,
                parent_height - 73 - content_y);
            m_contentElement->invalidate();
        }
    }

private:
    std::function<bool()> is_parent_view_;
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
        frame_ = frame;
        frame->set_parent_view_predicate([this] { return access_recovery_visible(); });
        content_parent_layout_ = access_recovery_visible();
        frame->setContent(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->setPreferTraditionalChinese(
                ptc_ui_language_get_resolved() == PTC_UI_LANGUAGE_TRADITIONAL);
            draw_overlay(renderer, x, y, w, h);
        }));
        return frame;
    }

    void expire_usage_snapshot()
    {
        // Tesla bypasses libnx time initialization: even a positive time() can
        // be uptime rather than Unix time. Use the bound request's monotonic age.
        const u64 tick = armGetSystemTick();
        const uint64_t age = usage_snapshot_tick_ != 0 && tick >= usage_snapshot_tick_
            ? armTicksToNs(tick - usage_snapshot_tick_) / 1000000000ULL : UINT64_MAX;
        ptc_companion_result_summary_expire_usage_elapsed(&displayed_summary_, age);
    }

    void update() override
    {
        if (s_owner_visible && !ptc_input_owner_maintain(&s_input_owner, armTicksToNs(armGetSystemTick()))) {
            tsl::Overlay::get()->hide();
            return;
        }
        if (s_owner_visible && ptc_input_owner_begin_ui(&s_input_owner)) {
            if (!restore_pending_redemption()) (void)begin_overlay_ready();
        }
        expire_usage_snapshot();
        // Parent views use a raised content top (banner hidden); re-layout the
        // frame whenever we cross the child<->parent boundary. Runs every frame
        // before draw, so it must stay ahead of the waiting-only early return.
        const bool parent_layout = access_recovery_visible();
        if (frame_ != nullptr && parent_layout != content_parent_layout_) {
            content_parent_layout_ = parent_layout;
            frame_->invalidate();
        }
        page_scroll_ += (page_scroll_target_ - page_scroll_) * 0.35f;
        if (std::abs(page_scroll_target_ - page_scroll_) < 0.5f) page_scroll_ = page_scroll_target_;
        if (recovery_active_) {
            poll_pending_redemption();
            expire_usage_snapshot();
        }
        // A countdown the child is watching ends once; re-read the state right
        // away instead of leaving "please refresh" on screen for 30 seconds.
        if (countdown_recheck_pending_ && !bridge_->waiting) {
            countdown_recheck_pending_ = false;
            (void)begin_status_refresh(PTC_UI_STATUS_COUNTDOWN_END);
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
                usage_snapshot_tick_ = request_started_tick_;
                has_status_snapshot_ = true;
                error_ = false;
                (void)begin_status_refresh(PTC_UI_STATUS_OVERLAY_READY);
            } else if (ptc_overlay_bridge_status_succeeded(bridge_)) {
                displayed_summary_ = bridge_->summary;
                usage_snapshot_tick_ = request_started_tick_;
                last_refresh_tick_ = armGetSystemTick();
                has_status_snapshot_ = true;
                error_ = false;
            } else if (ptc_overlay_bridge_preview_succeeded(bridge_)) {
                displayed_summary_ = bridge_->summary;
                usage_snapshot_tick_ = request_started_tick_;
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
                    if (material_change) reset_page_scroll();
                    preview_ready_ = true;
                }
            } else if (ptc_overlay_bridge_offline_code_succeeded(bridge_)) {
                displayed_summary_ = bridge_->summary;
                usage_snapshot_tick_ = request_started_tick_;
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
                usage_snapshot_tick_ = request_started_tick_;
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
                    usage_snapshot_tick_ = request_started_tick_;
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
        // Check a newly received snapshot before this frame draws it, too.
        expire_usage_snapshot();
        if (!bridge_->waiting) active_request_kind_ = OverlayRequestKind::None;
    }

#include "input_pages.hpp"

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
        usage_snapshot_tick_ = 0;
        apply_pending_preview();
        success_visible_ = true;
        error_ = false;
        status_expanded_ = true;
        active_request_kind_ = OverlayRequestKind::None;
        pending_code_[0] = '\0';
        ptc_overlay_input_init(input_);
    }

    bool begin_status_refresh(PtcUiStatusOrigin origin = PTC_UI_STATUS_OTHER)
    {
        if (!bridge_ || bridge_->waiting) return false;
        request_started_tick_ = armGetSystemTick();
        PtcCompanionStatus status = ptc_overlay_bridge_submit_status(
            bridge_, static_cast<int64_t>(std::time(nullptr)), ++request_nonce_);
        ptc_ui_status_trace_record(&s_status_trace, bridge_->request_id,
            origin == PTC_UI_STATUS_OTHER ? status_trigger_origin_ : origin,
            armTicksToNs(armGetSystemTick()), -1, status);
        if (status != PTC_COMPANION_OK) {
            error_ = true;
            status_expanded_ = true;
            last_request_kind_ = OverlayRequestKind::Status;
            return false;
        }
        active_request_kind_ = OverlayRequestKind::Status;
        last_request_kind_ = OverlayRequestKind::Status;
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
        request_started_tick_ = armGetSystemTick();
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
        request_started_tick_ = armGetSystemTick();
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
        last_elapsed_ms_ = 0;
        error_ = false;
        parent_view_ = ParentView::Result;
        parent_action_succeeded_ = false;
        return true;
    }

    void open_parent_pin()
    {
        parent_view_ = ParentView::Pin;
        parent_pin_standard_keypad_ = false;
        parent_pin_focus_ = 1;
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
        request_started_tick_ = armGetSystemTick();
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
        const s32 tx = static_cast<s32>(touch.x);
        const s32 ty = static_cast<s32>(touch.y);
        prev_touch_down_ = touch_down;
        if (parent_view_ == ParentView::Pin) {
            if (keysDown & HidNpadButton_Minus) {
                parent_pin_standard_keypad_ = !parent_pin_standard_keypad_;
                parent_pin_focus_ = parent_pin_standard_keypad_ ? 1 : 0;
                return true;
            }
            if (!parent_pin_standard_keypad_) {
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
                if (keysDown & HidNpadButton_X) append_pin_digit(0);
                else if (keysDown & HidNpadButton_Y) append_pin_digit(9);
            } else {
                if (keysDown & HidNpadButton_X) {
                    if (pin_length_ > 0) pin_[--pin_length_] = '\0';
                } else if (keysDown & HidNpadButton_Y) {
                    std::memset(pin_, 0, sizeof(pin_));
                    pin_length_ = 0;
                }
            }
            if (parent_pin_standard_keypad_) {
                int old_focus = parent_pin_focus_;
                if (keysDown & HidNpadButton_Up) {
                    if (parent_pin_focus_ >= 1 && parent_pin_focus_ <= 3) {
                        // Top row of numpad: stay at 1..3
                    } else if (parent_pin_focus_ >= 4 && parent_pin_focus_ <= 9) {
                        parent_pin_focus_ -= 3;
                    } else if (parent_pin_focus_ == 10) {
                        parent_pin_focus_ = 7;
                    } else if (parent_pin_focus_ == 11) {
                        parent_pin_focus_ = 8;
                    } else if (parent_pin_focus_ == 12) {
                        parent_pin_focus_ = 9;
                    } else if (parent_pin_focus_ == 0) {
                        parent_pin_focus_ = 10;
                    } else if (parent_pin_focus_ == 15) {
                        parent_pin_focus_ = 12;
                    } else if (parent_pin_focus_ == 13) {
                        parent_pin_focus_ = 0;
                    } else if (parent_pin_focus_ == 14) {
                        parent_pin_focus_ = 15;
                    }
                } else if (keysDown & HidNpadButton_Down) {
                    if (parent_pin_focus_ >= 1 && parent_pin_focus_ <= 6) {
                        parent_pin_focus_ += 3;
                    } else if (parent_pin_focus_ == 7) {
                        parent_pin_focus_ = 10;
                    } else if (parent_pin_focus_ == 8) {
                        parent_pin_focus_ = 11;
                    } else if (parent_pin_focus_ == 9) {
                        parent_pin_focus_ = 12;
                    } else if (parent_pin_focus_ == 10 || parent_pin_focus_ == 11) {
                        parent_pin_focus_ = 0;
                    } else if (parent_pin_focus_ == 12) {
                        parent_pin_focus_ = 15;
                    } else if (parent_pin_focus_ == 0) {
                        parent_pin_focus_ = 13;
                    } else if (parent_pin_focus_ == 15) {
                        parent_pin_focus_ = 14;
                    }
                } else if (keysDown & HidNpadButton_Left) {
                    if (parent_pin_focus_ == 2 || parent_pin_focus_ == 3 ||
                        parent_pin_focus_ == 5 || parent_pin_focus_ == 6 ||
                        parent_pin_focus_ == 8 || parent_pin_focus_ == 9) {
                        parent_pin_focus_ -= 1;
                    } else if (parent_pin_focus_ == 11 || parent_pin_focus_ == 12) {
                        parent_pin_focus_ -= 1;
                    } else if (parent_pin_focus_ == 15) {
                        parent_pin_focus_ = 0;
                    } else if (parent_pin_focus_ == 14) {
                        parent_pin_focus_ = 13;
                    }
                } else if (keysDown & HidNpadButton_Right) {
                    if (parent_pin_focus_ == 1 || parent_pin_focus_ == 2 ||
                        parent_pin_focus_ == 4 || parent_pin_focus_ == 5 ||
                        parent_pin_focus_ == 7 || parent_pin_focus_ == 8) {
                        parent_pin_focus_ += 1;
                    } else if (parent_pin_focus_ == 10 || parent_pin_focus_ == 11) {
                        parent_pin_focus_ += 1;
                    } else if (parent_pin_focus_ == 0) {
                        parent_pin_focus_ = 15;
                    } else if (parent_pin_focus_ == 13) {
                        parent_pin_focus_ = 14;
                    }
                }
                if (parent_pin_focus_ != old_focus) {
                    PtcOverlayRect target_rect{};
                    if (parent_pin_focus_ == 0) target_rect = page_layout_.pin_keys[10];
                    else if (parent_pin_focus_ >= 1 && parent_pin_focus_ <= 9) target_rect = page_layout_.pin_keys[parent_pin_focus_];
                    else if (parent_pin_focus_ == 10) target_rect = page_layout_.pin_keys[12];
                    else if (parent_pin_focus_ == 11) target_rect = page_layout_.pin_keys[0];
                    else if (parent_pin_focus_ == 12) target_rect = page_layout_.pin_keys[11];
                    else if (parent_pin_focus_ == 15) target_rect = page_layout_.backspace;
                    else if (parent_pin_focus_ == 13) target_rect = page_layout_.secondary;
                    else if (parent_pin_focus_ == 14) target_rect = page_layout_.primary;
                    if (target_rect.w > 0) {
                        page_scroll_target_ = static_cast<float>(ptc_overlay_scroll_to_rect(
                            page_layout_.viewport, target_rect, static_cast<int>(page_scroll_target_), page_layout_.scroll_max));
                    }
                }
                if (keysDown & HidNpadButton_A) {
                    if (parent_pin_focus_ == 0) {
                        parent_pin_standard_keypad_ = false;
                        return true;
                    } else if (parent_pin_focus_ >= 1 && parent_pin_focus_ <= 9) {
                        append_pin_digit(parent_pin_focus_);
                        return true;
                    } else if (parent_pin_focus_ == 10 || parent_pin_focus_ == 15) {
                        keysDown |= HidNpadButton_ZL;
                    } else if (parent_pin_focus_ == 11) {
                        append_pin_digit(0);
                        return true;
                    } else if (parent_pin_focus_ == 12) {
                        std::memset(pin_, 0, sizeof(pin_));
                        pin_length_ = 0;
                        return true;
                    } else if (parent_pin_focus_ == 13) {
                        keysDown |= HidNpadButton_B;
                    } else if (parent_pin_focus_ == 14) {
                        keysDown |= HidNpadButton_Plus;
                    }
                }
            }
            if (touch_pressed) {
                if (flow_hit(page_layout_.pin_keys[10], tx, ty)) {
                    parent_pin_standard_keypad_ = !parent_pin_standard_keypad_;
                    parent_pin_focus_ = parent_pin_standard_keypad_ ? 1 : 0;
                    return true;
                }
                if (!parent_pin_standard_keypad_) {
                    static const int wheel_digits[8] = {8, 1, 2, 7, 3, 6, 5, 4};
                    for (int i = 0; i < 8; ++i) {
                        if (flow_hit(page_layout_.pin_keys[i], tx, ty)) {
                            append_pin_digit(wheel_digits[i]);
                            return true;
                        }
                    }
                    if (flow_hit(page_layout_.pin_keys[8], tx, ty)) {
                        append_pin_digit(0);
                        return true;
                    }
                    if (flow_hit(page_layout_.pin_keys[9], tx, ty)) {
                        append_pin_digit(9);
                        return true;
                    }
                } else {
                    for (int d = 0; d < 10; ++d) {
                        if (flow_hit(page_layout_.pin_keys[d], tx, ty)) {
                            parent_pin_focus_ = (d == 0 ? 11 : d);
                            append_pin_digit(d);
                            return true;
                        }
                    }
                    if (flow_hit(page_layout_.pin_keys[11], tx, ty)) {
                        parent_pin_focus_ = 12;
                        std::memset(pin_, 0, sizeof(pin_));
                        pin_length_ = 0;
                        return true;
                    }
                    if (flow_hit(page_layout_.pin_keys[12], tx, ty)) {
                        parent_pin_focus_ = 10;
                        keysDown |= HidNpadButton_ZL;
                    }
                }
                if (flow_hit(page_layout_.backspace, tx, ty)) {
                    parent_pin_focus_ = 15;
                    keysDown |= HidNpadButton_ZL;
                } else if (page_hit(page_layout_.primary, tx, ty)) {
                    parent_pin_focus_ = 14;
                    keysDown |= HidNpadButton_Plus;
                } else if (page_hit(page_layout_.secondary, tx, ty)) {
                    parent_pin_focus_ = 13;
                    keysDown |= HidNpadButton_B;
                }
            }
            if ((keysDown & HidNpadButton_ZL) && pin_length_ > 0) {
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
                PtcAuthStatus auth_status = ptc_companion_auth_verify_pin_now(&auth_, pin_, &retry_after);
                std::memset(pin_, 0, sizeof(pin_));
                pin_length_ = 0;
                if (auth_status == PTC_AUTH_OK) {
                    parent_authorized_ = true;
                    parent_action_ = 0;
                    reset_page_scroll();
                    parent_view_ = ParentView::Actions;
                    pin_message_[0] = '\0';
                    action_repeat_ = {};
                    if ((!has_status_snapshot_ || status_is_stale()) && !bridge_->waiting)
                        (void)begin_status_refresh();
                } else if (auth_status == PTC_AUTH_COOLDOWN) {
                    PtcUiTextArg args[] = {{"seconds", nullptr, retry_after, true}};
                    (void)ptc_ui_text_format(PTC_UI_T_PIN_COOLDOWN,
                        pin_message_, sizeof(pin_message_), args, 1);
                } else if (auth_status == PTC_AUTH_TIME_UNAVAILABLE) {
                    std::snprintf(pin_message_, sizeof(pin_message_), "%s", ptc_ui_text(PTC_UI_T_PIN_CLOCK_UNAVAILABLE));
                } else if (auth_status == PTC_AUTH_EMPTY) {
                    std::snprintf(pin_message_, sizeof(pin_message_), "%s", ptc_ui_text(PTC_UI_T_PIN_MISSING));
                } else {
                    std::snprintf(pin_message_, sizeof(pin_message_), "%s", ptc_ui_text(PTC_UI_T_PIN_INVALID));
                }
                if (auth_status != PTC_AUTH_OK) reset_page_scroll();
            }
            return true;
        }
        if (parent_view_ == ParentView::Actions) {
            bool action_changed = false;
            if (touch_pressed && page_hit(page_layout_.refresh, tx, ty))
                keysDown |= HidNpadButton_Y;
            if ((keysDown & HidNpadButton_Y) && !bridge_->waiting)
                (void)begin_status_refresh();
            const unsigned int direction = ptc_overlay_direction_step(&action_repeat_,
                to_overlay_buttons(keysDown), to_overlay_buttons(keysHeld), elapsed_ms);
            if (touch_pressed) {
                for (int index = 0; index < PTC_OVERLAY_PARENT_ACTION_COUNT; ++index) {
                    if (flow_hit(page_layout_.actions[index], tx, ty)) {
                        const bool was_selected = parent_action_ == index;
                        parent_action_ = index;
                        focus_parent_action();
                        ptc_overlay_hold_reset(&confirm_hold_);
                        action_changed = true;
                        if (was_selected && !parent_action_requires_hold(
                                static_cast<PtcOverlayParentAction>(index))) keysDown |= HidNpadButton_A;
                        break;
                    }
                }
                if (page_hit(page_layout_.primary, tx, ty))
                    keysDown |= HidNpadButton_B;
            }
            if (parent_action_ == PTC_OVERLAY_PARENT_ADD_MINUTES && (direction & PTC_OVERLAY_BUTTON_LEFT))
                daily_add_minutes_ = daily_add_minutes_ <= 5 ? 5 : daily_add_minutes_ - 5;
            if (parent_action_ == PTC_OVERLAY_PARENT_ADD_MINUTES && (direction & PTC_OVERLAY_BUTTON_RIGHT))
                daily_add_minutes_ = daily_add_minutes_ >= 120 ? 120 : daily_add_minutes_ + 5;
            if (direction & PTC_OVERLAY_BUTTON_UP) {
                if (parent_action_ == 0) {
                    parent_action_ = -1;
                } else if (parent_action_ > 0) {
                    parent_action_ = ptc_overlay_parent_action_move(parent_action_, -1);
                    focus_parent_action();
                }
                ptc_overlay_hold_reset(&confirm_hold_);
                action_changed = true;
            }
            if (direction & PTC_OVERLAY_BUTTON_DOWN) {
                if (parent_action_ == -1) {
                    parent_action_ = 0;
                    focus_parent_action();
                } else {
                    parent_action_ = ptc_overlay_parent_action_move(parent_action_, 1);
                    focus_parent_action();
                }
                ptc_overlay_hold_reset(&confirm_hold_);
                action_changed = true;
            }
            if (parent_action_ == -1) {
                if ((keysDown & HidNpadButton_A) && !bridge_->waiting) {
                    (void)begin_status_refresh();
                } else if (keysDown & HidNpadButton_B) {
                    parent_authorized_ = false;
                    parent_view_ = ParentView::Child;
                    ptc_overlay_hold_reset(&confirm_hold_);
                }
            } else if (keysDown & HidNpadButton_B) {
                parent_authorized_ = false;
                parent_view_ = ParentView::Child;
                ptc_overlay_hold_reset(&confirm_hold_);
            } else if (!parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_)) &&
                       !parent_action_requires_hold(static_cast<PtcOverlayParentAction>(parent_action_)) &&
                       (keysDown & HidNpadButton_A)) {
                if (parent_action_ == PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP ||
                    parent_action_ == PTC_OVERLAY_PARENT_WAIVE_DOCK) {
                    parent_view_ = ParentView::Confirm;
                    confirm_focus_ = 1;
                    parent_confirm_armed_ = false;
                    ptc_overlay_hold_reset(&confirm_hold_);
                } else {
                    (void)submit_parent_action();
                }
            } else if (!parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_)) &&
                       parent_action_requires_hold(static_cast<PtcOverlayParentAction>(parent_action_)) &&
                       !action_changed && !(gesture_.active && gesture_.dragged) && (keysHeld & HidNpadButton_A)) {
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
            if (touch_pressed) {
                if (page_hit(page_layout_.secondary, tx, ty)) {
                    confirm_focus_ = 0;
                    keysDown |= HidNpadButton_B;
                } else if (page_hit(page_layout_.primary, tx, ty) && !immediate && parent_confirm_armed_) {
                    confirm_focus_ = 1;
                    keysDown |= HidNpadButton_A;
                }
            }
            if (keysDown & (HidNpadButton_Left | HidNpadButton_Right)) {
                confirm_focus_ = confirm_focus_ == 0 ? 1 : 0;
            }
            if (keysDown & HidNpadButton_B) {
                parent_view_ = ParentView::Actions;
                ptc_overlay_hold_reset(&confirm_hold_);
            } else if ((keysDown & HidNpadButton_A) && confirm_focus_ == 0) {
                parent_view_ = ParentView::Actions;
                ptc_overlay_hold_reset(&confirm_hold_);
            } else if (!parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_)) &&
                       !immediate && parent_confirm_armed_ && (keysDown & HidNpadButton_A) && confirm_focus_ == 1) {
                (void)submit_parent_action();
            } else if (!parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_)) &&
                       immediate && parent_confirm_armed_ && !(gesture_.active && gesture_.dragged) &&
                       (keysHeld & HidNpadButton_A) && confirm_focus_ == 1) {
                if (ptc_overlay_hold_update(&confirm_hold_, true, elapsed_ms, 1000))
                    (void)submit_parent_action();
            } else {
                ptc_overlay_hold_reset(&confirm_hold_);
            }
            return true;
        }
        const bool can_reverify = last_request_kind_ != OverlayRequestKind::ClaimDailyBuffer;
        if (touch_pressed) {
            if (page_hit(page_layout_.primary, tx, ty)) {
                result_focus_ = 0;
                keysDown |= HidNpadButton_B;
            } else if (page_hit(page_layout_.secondary, tx, ty) && can_reverify) {
                result_focus_ = 1;
                keysDown |= HidNpadButton_Y;
            }
        }
        if (can_reverify && (keysDown & (HidNpadButton_Left | HidNpadButton_Right))) {
            result_focus_ = result_focus_ == 0 ? 1 : 0;
        }
        if (keysDown & HidNpadButton_B) {
            parent_view_ = ParentView::Child;
            error_ = false;
        } else if (keysDown & HidNpadButton_A) {
            if (result_focus_ == 0) {
                parent_view_ = ParentView::Child;
                error_ = false;
            } else if (result_focus_ == 1 && can_reverify && !bridge_->waiting) {
                open_parent_pin();
            }
        } else if ((keysDown & HidNpadButton_Y) && !bridge_->waiting) {
            if (!can_reverify) {
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
        request_started_tick_ = armGetSystemTick();
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
        last_elapsed_ms_ = 0;
        error_ = false;
        status_expanded_ = true;
        return true;
    }

    bool begin_actual_code_submit()
    {
        if (!bridge_ || bridge_->waiting || pending_code_[0] == '\0' ||
            bedtime_restricted()) return false;
        request_started_tick_ = armGetSystemTick();
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
    PtcUiStatusOrigin status_trigger_origin_ = PTC_UI_STATUS_OTHER;
    bool owner_input_latched_ = false;
    PtcOverlayBridge *bridge_;
    PtcOverlayInput *input_;
    PlayWiseOverlayFrame *frame_ = nullptr;
    bool content_parent_layout_ = false;
    bool keypad_active_ = false;
    PtcUiResolvedTheme theme_ = PTC_UI_RESOLVED_DARK;
    PtcCompanionAuth auth_{};
    PtcCompanionResultSummary displayed_summary_{};
    PtcCompanionResultSummary preview_summary_{};
    PtcCompanionResultSummary redemption_before_{};
    OverlayRequestKind active_request_kind_ = OverlayRequestKind::None;
    OverlayRequestKind last_request_kind_ = OverlayRequestKind::None;
    unsigned int request_nonce_ = 0;
    u64 request_started_tick_ = 0;
    u64 usage_snapshot_tick_ = 0;
    u64 last_input_tick_ = 0;
    u64 last_refresh_tick_ = 0;
    int last_elapsed_ms_ = 0;
    bool error_ = false;
    bool has_status_snapshot_ = false;
    bool status_expanded_ = false;
    bool details_tech_expanded_ = false;
    bool parent_pin_standard_keypad_ = false;
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
    int child_overview_focus_ = 0;
    int status_details_focus_ = 0;
    int parent_pin_focus_ = 1;
    int preview_focus_ = 1;
    int confirm_focus_ = 1;
    int result_focus_ = 0;
    int keypad_nav_focus_ = -1;
    bool language_confirm_active_ = false;
    PtcOverlayLanguageSetting language_draft_selection_ = PtcOverlayLanguageSetting::Auto;
    int language_confirm_focus_ = 0;
    PtcOverlayCountdownWatch countdown_watch_{};
    bool countdown_recheck_pending_ = false;

    void open_language_confirm()
    {
        language_confirm_active_ = true;
        language_draft_selection_ = s_overlay_lang_setting;
        language_confirm_focus_ = static_cast<int>(s_overlay_lang_setting);
        reset_page_scroll();
        if (frame_) frame_->invalidate();
    }

    void cancel_language_confirm()
    {
        language_confirm_active_ = false;
        reset_page_scroll();
        if (frame_) frame_->invalidate();
    }

    void apply_language_confirm()
    {
        set_overlay_language_setting(language_draft_selection_);
        PtcStorage *storage = bridge_ && bridge_->transport.file.storage
            ? bridge_->transport.file.storage : nullptr;
        save_overlay_language_setting(storage, language_draft_selection_);

        PtcUiLanguagePreference effective = resolve_overlay_effective_language(
            language_draft_selection_, s_nro_language_preference, s_system_language);
        ptc_ui_language_set_resolved(effective);

        language_confirm_active_ = false;
        reset_page_scroll();
        if (frame_ != nullptr) {
            char title[64], subtitle[64];
            frame_->setTitle(ptc_ui_localize(ptc_ui_text(PTC_UI_T_PLAYWISE), title, sizeof(title)));
            frame_->setSubtitle(ptc_ui_localize(ptc_ui_text(PTC_UI_T_PLAYTIME_BEDTIME_CONTROLS), subtitle, sizeof(subtitle)));
            frame_->invalidate();
        }
    }

    void cycle_language_draft()
    {
        language_draft_selection_ = static_cast<PtcOverlayLanguageSetting>(
            (static_cast<int>(language_draft_selection_) + 1) % 4);
        language_confirm_focus_ = static_cast<int>(language_draft_selection_);
        if (frame_) frame_->invalidate();
    }
};

class PctcOverlay final : public tsl::Overlay {
public:
    void onShow() override
    {
        s_owner_visible = true;
        if (!ptc_input_owner_acquire(&s_input_owner, armTicksToNs(armGetSystemTick()))) hide();
    }

    void onHide() override
    {
        s_owner_visible = false;
        ptc_input_owner_release(&s_input_owner, armTicksToNs(armGetSystemTick()));
    }

    void initServices() override
    {
#if defined(__SWITCH__)
        timeInitialize();
#endif
        fsdevMountSdmc();
        ptc_fs_storage_init(&storage_);
        ptc_input_owner_writer_init(&s_input_owner, ptc_fs_storage_as_storage(&storage_), APP_ROOT,
            PLAYWISE_BUILD_RELEASE_ID, armTicksToNs(armGetSystemTick()));
        load_overlay_config(ptc_fs_storage_as_storage(&storage_));
        ptc_overlay_bridge_init(&bridge_, APP_ROOT, ptc_fs_storage_as_storage(&storage_));
        ptc_ui_status_trace_init(&s_status_trace, ptc_fs_storage_as_storage(&storage_), APP_ROOT,
            PLAYWISE_BUILD_COMMIT, PLAYWISE_BUILD_RELEASE_ID, "overlay", armTicksToNs(armGetSystemTick()));
        ptc_overlay_input_init(&input_);
    }

    void exitServices() override
    {
        onHide();
        ptc_ui_status_trace_close(&s_status_trace, armTicksToNs(armGetSystemTick()));
        ptc_overlay_bridge_exit(&bridge_);
        fsdevUnmountDevice("sdmc");
#if defined(__SWITCH__)
        timeExit();
#endif
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
