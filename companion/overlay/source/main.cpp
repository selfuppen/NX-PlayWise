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
#include "../resource_stats.h"

extern "C" {
#include "../bridge.h"
#include "../detail_status.h"
#include "../hidden_poll.h"
#include "../reason_wake.h"
#include "../hidden_timing.h"
#include "../brief_notice.h"
#include "../../auth.h"
#include "../../auth_clock.h"
#include "../input_model.h"
#include "../../ui_language.h"
#include "../../ui_status_trace.h"
#include "../../input_owner.h"
#include "../../notification_preferences.h"
#include "third_party/cjson/cJSON.h"
#include "common/crypto/sha256.h"
#include "common/time/ptc_time.h"
#include "platform/switch/fs_storage.h"
#include "platform/ultrahand_config.h"
}

namespace {

[[gnu::used]] constexpr char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

constexpr char APP_ROOT[] = "sdmc:/switch/playwise";
#include "render_types.hpp"

static PtcUiResolvedTheme s_overlay_theme = PTC_UI_RESOLVED_DARK;
static PtcUiStatusTrace s_status_trace;
static PtcInputOwnerWriter s_input_owner;
static bool s_owner_visible = false;
static bool s_automatic_detail = false;
static bool s_automatic_detail_confirmed = false;

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

    // The Tesla host layer is a translucent dark backdrop (ColorFrameBackground = 0x000D), but PlayWise
    // covers it with its own palette panel_bg instead of drawing on the bare host fill.
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

    void set_footer_spec_provider(std::function<OverlayFooterSpec()> provider)
    {
        footer_spec_provider_ = std::move(provider);
    }

    void set_draw_predicate(std::function<bool()> should_draw)
    {
        should_draw_ = std::move(should_draw);
    }

    void draw(tsl::gfx::Renderer *renderer) override
    {
        if (should_draw_ && !should_draw_()) {
            renderer->clearScreen();
            return;
        }
        // PlayWise renders its own compact brand & status bar directly inside draw_overlay.
        // We bypass drawing the default 90px title/subtitle banner in all views to maximize vertical space.
        // Fill with the palette panel color (~93% opaque) instead of libtesla's 0xD frame
        // background so game highlights cannot wash out text drawn outside the opaque cards.
        renderer->fillScreen(renderer->a(get_overlay_palette().panel_bg));
        renderer->drawRect(tsl::cfg::FramebufferWidth - 1, 0, 1, tsl::cfg::FramebufferHeight, a(0xF222));
        renderer->drawRect(15, tsl::cfg::FramebufferHeight - 73, tsl::cfg::FramebufferWidth - 30, 1,
            a(tsl::style::color::ColorText));

        char time_buf[16];
        get_overlay_current_time(time_buf, sizeof(time_buf));
        OverlayFooterSpec footer{};
        if (footer_spec_provider_) {
            footer = footer_spec_provider_();
        } else {
            footer.left_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_BACK);
            footer.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_OK);
        }
        draw_overlay_footer_hints(renderer, footer, time_buf, tsl::style::color::ColorText);

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
    std::function<OverlayFooterSpec()> footer_spec_provider_;
    std::function<bool()> should_draw_;
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
        frame->set_footer_spec_provider([this] { return current_footer_spec(); });
        frame->set_draw_predicate([this] {
            return !automatic_frame_offer_ || automatic_detail_ready(*automatic_frame_offer_);
        });
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

    bool child_detail_page_available() const
    {
        return parent_view_ == ParentView::Child &&
            !language_confirm_active_ && !is_keypad_mode() && !recovery_active_ &&
            !preview_ready_ && !success_visible_ && !result_pending_;
    }

    bool automatic_detail_allowed() const
    {
        return child_detail_page_available() && (has_status_snapshot_ || preloaded_hidden_) && !error_ && !bridge_->waiting;
    }

    void set_preloaded_hidden(bool preloaded)
    {
        preloaded_hidden_ = preloaded;
    }

    void prepare_automatic_frame(const PtcReasonDetail *offer, uint64_t deadline_ms = 0)
    {
        automatic_frame_offer_ = offer;
        automatic_frame_deadline_ms_ = deadline_ms;
    }

    void start_child_open()
    {
        preloaded_hidden_ = false;
        finish_refresh_while_hidden_ = false;
        if (!child_detail_page_available() || (error_ && last_request_kind_ != OverlayRequestKind::Status)) return;
        child_open_tick_ = armGetSystemTick();
        open_status_pending_ = true;
        status_expanded_ = false;
        reset_page_scroll();
        if (s_automatic_detail) {
            has_status_snapshot_ = false;
            displayed_summary_ = {};
        }
    }

    void cancel_child_open()
    {
        open_status_pending_ = false;
    }

    void on_child_hidden(bool unfinished_automatic)
    {
        cancel_child_open();
        if (unfinished_automatic && bridge_->waiting && status_for_open_ &&
            active_request_kind_ == OverlayRequestKind::Status)
            finish_refresh_while_hidden_ = true;
    }

    void poll_hidden_refresh()
    {
        if (!finish_refresh_while_hidden_) return;
        PtcCompanionStatus status = poll_active_request();
        if (status == PTC_COMPANION_PENDING) return;
        finish_refresh_while_hidden_ = false;
        active_request_kind_ = OverlayRequestKind::None;
        if (status == PTC_COMPANION_OK && ptc_overlay_bridge_status_succeeded(bridge_)) {
            /* Drain the original read-only refresh so a timed-out opening
               doesn't leave the hidden instance permanently unready. */
            remember_status();
        }
    }

    bool automatic_detail_ready(const PtcReasonDetail &offer) const
    {
        int day, second;
        // A registration reply can carry a different restriction. Wait for this
        // opening's status reply and keep cancelled openings transparent while hiding.
        return s_owner_visible && !status_expanded_ && child_detail_page_available() &&
            !bridge_->waiting && !error_ && status_for_open_ && last_request_kind_ == OverlayRequestKind::Status &&
            request_started_tick_ >= child_open_tick_ &&
            has_status_snapshot_ && last_refresh_tick_ >= request_started_tick_ &&
            armTicksToNs(request_started_tick_) / 1000000 >= offer.issued_ms &&
            armTicksToNs(armGetSystemTick()) / 1000000 < automatic_frame_deadline_ms_ &&
            std::strcmp(offer.boot, s_input_owner.context.boot) == 0 &&
            child_status_fresh(displayed_summary_, day, second) &&
            ptc_overlay_detail_matches(&offer, &displayed_summary_);
    }

    bool automatic_detail_drawn(const PtcReasonDetail &offer) const
    {
        return child_drawn_reason_ == offer.reason && automatic_detail_ready(offer);
    }

    void update() override
    {
        if (s_owner_visible && !ptc_input_owner_maintain(&s_input_owner, armTicksToNs(armGetSystemTick()))) {
            tsl::Overlay::get()->hide();
            return;
        }
        if (s_owner_visible && ptc_input_owner_begin_ui(&s_input_owner)) {
            if (restore_pending_redemption() || !begin_overlay_ready()) cancel_child_open();
        }
        if (open_status_pending_ && !child_detail_page_available()) cancel_child_open();
        if (open_status_pending_ && !bridge_->waiting) {
            cancel_child_open();
            countdown_recheck_pending_ = false;
            (void)begin_status_refresh(PTC_UI_STATUS_OTHER, true);
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
        PtcCompanionStatus status = poll_active_request();
        if (status == PTC_COMPANION_PENDING) return;
        const bool completing_ready = active_request_kind_ == OverlayRequestKind::OverlayReady;
        const bool completed_open_status = active_request_kind_ == OverlayRequestKind::Status && status_for_open_;
        if (completed_open_status || completing_ready) {
            // A reply from an earlier opening, or one received on a protected page, cannot affect navigation.
            if (!s_owner_visible || request_started_tick_ < child_open_tick_ || !child_detail_page_available()) {
                active_request_kind_ = OverlayRequestKind::None;
                return;
            }
            const bool successful_reply = completing_ready ? bridge_->summary.valid && bridge_->summary.ok :
                ptc_overlay_bridge_status_succeeded(bridge_);
            if (status != PTC_COMPANION_OK || !successful_reply) {
                error_ = true;
                if (completing_ready) cancel_child_open();
                active_request_kind_ = OverlayRequestKind::None;
                return;
            }
        }
        if (status == PTC_COMPANION_OK) {
            if (completing_ready) {
                displayed_summary_ = bridge_->summary;
                usage_snapshot_tick_ = request_started_tick_;
                has_status_snapshot_ = true;
                error_ = false;
                cancel_child_open();
                (void)begin_status_refresh(PTC_UI_STATUS_OVERLAY_READY, true);
            } else if (ptc_overlay_bridge_status_succeeded(bridge_)) {
                remember_status();
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
                          "%s", ptc_ui_text(PTC_UI_T_THE_LAST_GRANT_RECOVERY_INFORMATION_CANNOT_BE));
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
                          "%s", ptc_ui_text(PTC_UI_T_THE_LAST_CONFIRMATION_WAS_INTERRUPTED_BEFORE_SUBMISSION));
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
        PtcCompanionStatus status = ptc_companion_poll_recovered_result(
            &recovery_result_poll_, &bridge_->transport.file, pending_redemption_.request_id,
            armTicksToNs(armGetSystemTick()) / 1000000ULL,
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

    PtcCompanionStatus poll_active_request()
    {
        const u64 elapsed_ms_u64 = armTicksToNs(armGetSystemTick() - request_started_tick_) / 1000000ULL;
        const int elapsed_ms = elapsed_ms_u64 > static_cast<u64>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(elapsed_ms_u64);
        const int elapsed_delta_ms = elapsed_ms - last_elapsed_ms_;
        last_elapsed_ms_ = elapsed_ms;
        return ptc_overlay_bridge_poll(bridge_, elapsed_delta_ms, PTC_OVERLAY_REQUEST_TIMEOUT_MS);
    }

    void remember_status()
    {
        displayed_summary_ = bridge_->summary;
        usage_snapshot_tick_ = request_started_tick_;
        last_refresh_tick_ = armGetSystemTick();
        has_status_snapshot_ = true;
        error_ = false;
    }

    bool begin_status_refresh(PtcUiStatusOrigin origin = PTC_UI_STATUS_OTHER, bool for_open = false)
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
            if (!for_open) status_expanded_ = true;
            last_request_kind_ = OverlayRequestKind::Status;
            return false;
        }
        active_request_kind_ = OverlayRequestKind::Status;
        last_request_kind_ = OverlayRequestKind::Status;
        status_for_open_ = for_open;
        last_elapsed_ms_ = 0;
        error_ = false;
        return true;
    }

    bool should_refresh_on_nav() const
    {
        if (!bridge_ || bridge_->waiting) return false;
        // Navigation reuses the normal 30s snapshot. Explicit refresh and
        // countdown-end checks bypass this gate; action freshness is unchanged.
        return !has_status_snapshot_ || last_refresh_tick_ == 0 || status_is_stale();
    }

    bool maybe_refresh_on_nav(PtcUiStatusOrigin origin = PTC_UI_STATUS_OTHER)
    {
        if (should_refresh_on_nav()) {
            return begin_status_refresh(origin);
        }
        return false;
    }

    void open_status_details()
    {
        cancel_child_open();
        status_expanded_ = true;
        status_details_focus_ = (error_ || !has_status_snapshot_) ? 2 : 1;
        (void)maybe_refresh_on_nav(PTC_UI_STATUS_OTHER);
    }

    void close_status_details()
    {
        cancel_child_open();
        status_expanded_ = false;
        reset_page_scroll();
        (void)maybe_refresh_on_nav(PTC_UI_STATUS_RESUME);
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
        int64_t retry_after = 0;
        PtcAuthStatus cooldown_status = ptc_companion_auth_check_cooldown_now(&auth_, &retry_after);
        if (cooldown_status == PTC_AUTH_COOLDOWN && retry_after > 0) {
            PtcUiTextArg args[] = {{"seconds", nullptr, retry_after, true}};
            (void)ptc_ui_text_format(PTC_UI_T_PIN_COOLDOWN,
                pin_message_, sizeof(pin_message_), args, 1);
        }
        (void)maybe_refresh_on_nav(PTC_UI_STATUS_OTHER);
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
                        parent_pin_focus_ = 11;
                    } else if (parent_pin_focus_ == 14) {
                        parent_pin_focus_ = 0;
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
                    } else if (parent_pin_focus_ == 10 || parent_pin_focus_ == 11 || parent_pin_focus_ == 12) {
                        parent_pin_focus_ = 0;
                    } else if (parent_pin_focus_ == 0) {
                        parent_pin_focus_ = 14;
                    }
                } else if (keysDown & HidNpadButton_Left) {
                    if (parent_pin_focus_ == 2 || parent_pin_focus_ == 3 ||
                        parent_pin_focus_ == 5 || parent_pin_focus_ == 6 ||
                        parent_pin_focus_ == 8 || parent_pin_focus_ == 9) {
                        parent_pin_focus_ -= 1;
                    } else if (parent_pin_focus_ == 11 || parent_pin_focus_ == 12) {
                        parent_pin_focus_ -= 1;
                    }
                } else if (keysDown & HidNpadButton_Right) {
                    if (parent_pin_focus_ == 1 || parent_pin_focus_ == 2 ||
                        parent_pin_focus_ == 4 || parent_pin_focus_ == 5 ||
                        parent_pin_focus_ == 7 || parent_pin_focus_ == 8) {
                        parent_pin_focus_ += 1;
                    } else if (parent_pin_focus_ == 10 || parent_pin_focus_ == 11) {
                        parent_pin_focus_ += 1;
                    }
                }
                if (parent_pin_focus_ != old_focus) {
                    PtcOverlayRect target_rect{};
                    if (parent_pin_focus_ == 0) target_rect = page_layout_.pin_keys[10];
                    else if (parent_pin_focus_ >= 1 && parent_pin_focus_ <= 9) target_rect = page_layout_.pin_keys[parent_pin_focus_];
                    else if (parent_pin_focus_ == 10) target_rect = page_layout_.pin_keys[12];
                    else if (parent_pin_focus_ == 11) target_rect = page_layout_.pin_keys[0];
                    else if (parent_pin_focus_ == 12) target_rect = page_layout_.pin_keys[11];
                    else if (parent_pin_focus_ == 14) target_rect = page_layout_.primary;
                    if (target_rect.w > 0 && parent_pin_focus_ != 14) {
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
                    } else if (parent_pin_focus_ == 10) {
                        keysDown |= HidNpadButton_ZL;
                    } else if (parent_pin_focus_ == 11) {
                        append_pin_digit(0);
                        return true;
                    } else if (parent_pin_focus_ == 12) {
                        std::memset(pin_, 0, sizeof(pin_));
                        pin_length_ = 0;
                        return true;
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
                    keysDown |= HidNpadButton_ZL;
                } else if (page_hit(page_layout_.primary, tx, ty)) {
                    parent_pin_focus_ = 14;
                    keysDown |= HidNpadButton_Plus;
                }
            }
            if ((keysDown & HidNpadButton_ZL) && pin_length_ > 0) {
                pin_[--pin_length_] = '\0';
            } else if (keysDown & HidNpadButton_B) {
                parent_view_ = ParentView::Child;
                std::memset(pin_, 0, sizeof(pin_));
                pin_length_ = 0;
                (void)maybe_refresh_on_nav(PTC_UI_STATUS_RESUME);
            } else if (keysDown & HidNpadButton_Plus) {
                int64_t cooldown_retry = 0;
                PtcAuthStatus cooldown_status = ptc_companion_auth_check_cooldown_now(&auth_, &cooldown_retry);
                if (cooldown_status == PTC_AUTH_COOLDOWN && cooldown_retry > 0) {
                    std::memset(pin_, 0, sizeof(pin_));
                    pin_length_ = 0;
                    PtcUiTextArg args[] = {{"seconds", nullptr, cooldown_retry, true}};
                    (void)ptc_ui_text_format(PTC_UI_T_PIN_COOLDOWN,
                        pin_message_, sizeof(pin_message_), args, 1);
                    reset_page_scroll();
                    return true;
                }
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
                    const auto reason = child_home_reason();
                    const auto priority = reason == PTC_REASON_BEDTIME ? PTC_OVERLAY_PARENT_SKIP_BEDTIME :
                        reason == PTC_REASON_EYE ? PTC_OVERLAY_PARENT_SKIP_EYE_CARE :
                        reason == PTC_REASON_DOCK ? PTC_OVERLAY_PARENT_WAIVE_DOCK : PTC_OVERLAY_PARENT_ADD_MINUTES;
                    // Freeze the order during this parent session so a refresh cannot move a focused action.
                    promoted_parent_action_ = priority != PTC_OVERLAY_PARENT_ADD_MINUTES && !parent_action_reason(priority)
                        ? static_cast<int>(priority) : -1;
                    parent_tab_ = 0;
                    parent_unavailable_expanded_ = false;
                    parent_action_ = promoted_parent_action_ >= 0 ? promoted_parent_action_ : parent_first_action_for_tab(0);
                    reset_page_scroll();
                    parent_view_ = ParentView::Actions;
                    pin_message_[0] = '\0';
                    action_repeat_ = {};
                    (void)maybe_refresh_on_nav(PTC_UI_STATUS_OTHER);
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
            auto switch_parent_tab = [&](int next_tab) {
                parent_tab_ = (next_tab % 3 + 3) % 3;
                page_scroll_ = page_scroll_target_ = 0;
                parent_action_ = parent_first_action_for_tab(parent_tab_);
                ptc_overlay_hold_reset(&confirm_hold_);
                action_changed = true;
            };
            if (touch_pressed && page_hit(page_layout_.refresh, tx, ty))
                keysDown |= HidNpadButton_Y;
            if ((keysDown & HidNpadButton_Y) && !bridge_->waiting)
                (void)begin_status_refresh();
            if (keysDown & HidNpadButton_L) {
                switch_parent_tab(parent_tab_ - 1);
            } else if (keysDown & HidNpadButton_R) {
                switch_parent_tab(parent_tab_ + 1);
            }
            if ((keysDown & HidNpadButton_X) && parent_tab_ == 1 && page_layout_.parent_drawer.w > 0) {
                parent_unavailable_expanded_ = !parent_unavailable_expanded_;
                if (!parent_unavailable_expanded_ && parent_action_ >= 0 &&
                    parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_)) != nullptr) {
                    parent_action_ = parent_first_action_for_tab(1);
                }
                ptc_overlay_hold_reset(&confirm_hold_);
                action_changed = true;
            }
            const unsigned int direction = ptc_overlay_direction_step(&action_repeat_,
                to_overlay_buttons(keysDown), to_overlay_buttons(keysHeld), elapsed_ms);
            if (touch_pressed) {
                bool handled_touch = false;
                for (int t = 0; t < 3; ++t) {
                    if (flow_hit(page_layout_.parent_tabs[t], tx, ty)) {
                        switch_parent_tab(t);
                        handled_touch = true;
                        break;
                    }
                }
                if (!handled_touch && flow_hit(page_layout_.parent_drawer, tx, ty)) {
                    parent_unavailable_expanded_ = !parent_unavailable_expanded_;
                    if (!parent_unavailable_expanded_ && parent_action_ >= 0 &&
                        parent_action_reason(static_cast<PtcOverlayParentAction>(parent_action_)) != nullptr) {
                        parent_action_ = parent_first_action_for_tab(1);
                    }
                    ptc_overlay_hold_reset(&confirm_hold_);
                    action_changed = true;
                    handled_touch = true;
                }
                if (!handled_touch) {
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
                }
            }
            if (parent_action_ == PTC_OVERLAY_PARENT_ADD_MINUTES && (direction & PTC_OVERLAY_BUTTON_LEFT))
                daily_add_minutes_ = daily_add_minutes_ <= 5 ? 5 : daily_add_minutes_ - 5;
            if (parent_action_ == PTC_OVERLAY_PARENT_ADD_MINUTES && (direction & PTC_OVERLAY_BUTTON_RIGHT))
                daily_add_minutes_ = daily_add_minutes_ >= 120 ? 120 : daily_add_minutes_ + 5;
            int vis_actions[PTC_OVERLAY_PARENT_ACTION_COUNT];
            const int vis_count = parent_visible_actions(vis_actions);
            if (direction & PTC_OVERLAY_BUTTON_UP) {
                if (vis_count == 0 || parent_action_ == vis_actions[0]) {
                    parent_action_ = -1;
                } else if (parent_action_ >= 0) {
                    int pos = -1;
                    for (int i = 0; i < vis_count; ++i)
                        if (vis_actions[i] == parent_action_) { pos = i; break; }
                    parent_action_ = pos > 0 ? vis_actions[pos - 1] : vis_actions[vis_count - 1];
                    focus_parent_action();
                }
                ptc_overlay_hold_reset(&confirm_hold_);
                action_changed = true;
            }
            if (direction & PTC_OVERLAY_BUTTON_DOWN) {
                if (vis_count > 0) {
                    if (parent_action_ == -1) {
                        parent_action_ = vis_actions[0];
                    } else {
                        int pos = -1;
                        for (int i = 0; i < vis_count; ++i)
                            if (vis_actions[i] == parent_action_) { pos = i; break; }
                        parent_action_ = (pos >= 0 && pos + 1 < vis_count) ? vis_actions[pos + 1] : vis_actions[0];
                    }
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
                    (void)maybe_refresh_on_nav(PTC_UI_STATUS_RESUME);
                }
            } else if (keysDown & HidNpadButton_B) {
                parent_authorized_ = false;
                parent_view_ = ParentView::Child;
                ptc_overlay_hold_reset(&confirm_hold_);
                (void)maybe_refresh_on_nav(PTC_UI_STATUS_RESUME);
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
            (void)maybe_refresh_on_nav(PTC_UI_STATUS_RESUME);
        } else if (keysDown & HidNpadButton_A) {
            if (result_focus_ == 0) {
                parent_view_ = ParentView::Child;
                error_ = false;
                (void)maybe_refresh_on_nav(PTC_UI_STATUS_RESUME);
            } else if (result_focus_ == 1 && can_reverify && !bridge_->waiting) {
                open_parent_pin();
            }
        } else if ((keysDown & HidNpadButton_Y) && !bridge_->waiting) {
            if (!can_reverify) {
                parent_view_ = ParentView::Child;
                error_ = false;
                (void)maybe_refresh_on_nav(PTC_UI_STATUS_RESUME);
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
                          "%s", ptc_ui_text(PTC_UI_T_SUBMISSION_FAILED_THE_GRANT_CODE_HAS_NOT));
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
    const PtcReasonDetail *automatic_frame_offer_ = nullptr;
    uint64_t automatic_frame_deadline_ms_ = 0;
    OverlayRequestKind active_request_kind_ = OverlayRequestKind::None;
    OverlayRequestKind last_request_kind_ = OverlayRequestKind::None;
    unsigned int request_nonce_ = 0;
    u64 child_open_tick_ = 0;
    bool open_status_pending_ = false;
    bool status_for_open_ = false;
    u64 request_started_tick_ = 0;
    u64 usage_snapshot_tick_ = 0;
    u64 last_input_tick_ = 0;
    u64 last_refresh_tick_ = 0;
    int last_elapsed_ms_ = 0;
    bool error_ = false;
    bool has_status_snapshot_ = false;
    bool finish_refresh_while_hidden_ = false;
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
    PtcCompanionResultPoll recovery_result_poll_ = {};
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
    int status_details_focus_ = 1;
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
    bool preloaded_hidden_ = false;

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
    void onPreloadArgs(bool preloaded) override
    {
        preloaded_hidden_ = preloaded;
        if (preloaded) {
            if (gui_) gui_->set_preloaded_hidden(true);
            (void)ptc_ultrahand_clear_preload_state(ptc_fs_storage_as_storage(&storage_));
        }
    }

    void onSleepWakeDetected(uint64_t wakeNs, uint64_t wakeStartNs) override
    {
        /* Query lock-screen setting once per real sleep wake using libtesla's
           existing set:sys session; fall back to conservative lock_screen=true
           on error so dark wakes or unlock prompts are never interrupted. */
        bool lock_flag = false;
        wake_lock_screen_enabled_ = R_SUCCEEDED(setsysGetLockScreenFlag(&lock_flag)) ? lock_flag : true;
        wake_recheck_pending_ = automatic_enabled_ && gui_ && gui_->automatic_detail_allowed();
        wake_detected_ns_ = wakeNs;
        wake_sleep_start_ns_ = wakeStartNs;
        next_reason_subscribe_ms_ = 0;
        next_ready_ms_ = 0;
        next_detail_poll_ms_ = 0;
    }

    void onHiddenWakeSignals(uint64_t lastUserInputNs, uint64_t lastPowerEventNs, uint64_t lastHomeEventNs) override
    {
        wake_last_user_input_ns_ = lastUserInputNs;
        wake_last_power_event_ns_ = lastPowerEventNs;
        wake_last_home_event_ns_ = lastHomeEventNs;
    }

    bool wakeEarlyUnlockEnabled() const override
    {
        /* Keep early unlock release active after wake_recheck_pending_ is
           consumed so the UI does not snap back into the remaining 8s POWER
           cooldown while awaiting the backend's detail request/event. */
        return wake_lock_screen_enabled_ &&
            automatic_enabled_ && gui_ && gui_->automatic_detail_allowed();
    }

    Event* hiddenWakeEvent() override
    {
        return automatic_enabled_ && runtime_known_ && gui_ && gui_->automatic_detail_allowed() &&
            eventActive(&reason_event_) ? &reason_event_ : nullptr;
    }

    void onHiddenWake(bool usable) override
    {
        if (usable) {
            ptc_overlay_reason_wake(&reason_wake_, armTicksToNs(armGetSystemTick()) / 1000000);
        } else {
            eventClose(&reason_event_);
        }
        next_detail_poll_ms_ = next_brief_read_ms_ = 0;
    }

    u64 hiddenPollInterval() override
    {
        if (brief_notice_.active) return UINT64_C(50000000);
        uint64_t ms = armTicksToNs(armGetSystemTick()) / 1000000;
        bool wake_active = ptc_overlay_reason_wake_active(&reason_wake_, ms) ||
            (wake_recheck_pending_ && wake_detected_ns_ != 0 && wake_last_user_input_ns_ > wake_detected_ns_);
        /* Failed detail writes stop that writer. Brief reception remains read-only. */
        return ptc_overlay_hidden_poll_idle_ns(automatic_enabled_, eventActive(&reason_event_), wake_active, 0);
    }

    bool hasHiddenFrame() override { return brief_notice_.active; }

    bool onHiddenFrame(bool blocked, u64 keysDown) override
    {
        uint64_t ms = armTicksToNs(armGetSystemTick()) / 1000000;
        if (blocked) s_automatic_detail = false;
        bool allowed = !blocked && automatic_enabled_ && runtime_known_ && gui_ &&
            gui_->automatic_detail_allowed() && !s_automatic_detail;
        bool dismiss = (keysDown & HidNpadButton_Minus) != 0;
        bool read_due = allowed && (ms >= next_brief_read_ms_ || dismiss);
        if (read_due) next_brief_read_ms_ = ms + 250;
        bool read_reason = read_due && (dismiss || ptc_overlay_reason_scan(&reason_wake_,
            eventActive(&reason_event_), ms, &next_reason_audit_ms_));
        bool changed = ptc_overlay_brief_poll(&brief_notice_, read_due ? ptc_fs_storage_as_storage(&storage_) : nullptr,
            APP_ROOT, PLAYWISE_BUILD_RELEASE_ID, runtime_boot_, ms, allowed,
            dismiss, read_reason);
        brief_diagnostic(ms, blocked);
        return changed;
    }

    void drawHiddenFrame(tsl::gfx::Renderer *target) override
    {
        auto &renderer = *target;
        renderer.setPreferTraditionalChinese(ptc_ui_language_get_resolved() == PTC_UI_LANGUAGE_TRADITIONAL);
        const auto &palette = get_overlay_palette(s_overlay_theme);
        const int width = tsl::cfg::FramebufferWidth - 32;
        const int title_height = overlay_text_height(&renderer, brief_notice_.title, width - 32, 26);
        const int text_height = overlay_text_height(&renderer, brief_notice_.text, width - 32, 26);
        const int height = title_height + text_height + 48;
        if (height > tsl::cfg::FramebufferHeight - 32) {
            brief_notice_.active = false;
            return;
        }
        renderer.drawRect(28, 16, width - 24, height, palette.card_bg);
        renderer.drawRect(16, 28, width, height - 24, palette.card_bg);
        renderer.drawCircle(28, 28, 12, true, palette.card_bg);
        renderer.drawCircle(16 + width - 12, 28, 12, true, palette.card_bg);
        renderer.drawCircle(28, 16 + height - 12, 12, true, palette.card_bg);
        renderer.drawCircle(16 + width - 12, 16 + height - 12, 12, true, palette.card_bg);
        overlay_text(&renderer, brief_notice_.title, 32, 32, width - 32, 26, palette.text_primary);
        overlay_text(&renderer, brief_notice_.text, 32, 40 + title_height, width - 32, 26, palette.text_primary);
    }

    bool onHiddenPoll(bool neutral) override
    {
        uint64_t ms = armTicksToNs(armGetSystemTick()) / 1000000;
        PtcStorage *s = ptc_fs_storage_as_storage(&storage_);
        if (!gui_) return false;
        if (ms < next_detail_poll_ms_) return false;
        next_detail_poll_ms_ = ms + PTC_OVERLAY_HIDDEN_DETAIL_MS;
        gui_->poll_hidden_refresh();
        if (!gui_->automatic_detail_allowed()) return false;
        ptc_overlay_hidden_timing_poll(&hidden_timing_, ms, neutral);
        /* The preference check does not depend on input: with the five second
           disabled cadence a window without any key is rare during play, and the
           check itself is one bounded read. Cooperation below still needs neutral input. */
        if (ms >= next_config_ms_) {
            char config[8192];
            ptc_overlay_hidden_timing_read_begin(&hidden_timing_, &hidden_timing_.config_read,
                armTicksToNs(armGetSystemTick()) / 1000000);
            bool read_ok = s->vtable->read_text(s, "sdmc:/switch/playwise/config.json", config, sizeof(config));
            ptc_reason_notification_preferences(read_ok ? config : nullptr, &automatic_enabled_, nullptr);
            next_config_ms_ = ms + PTC_OVERLAY_HIDDEN_CONFIG_MS;
            ptc_overlay_hidden_timing_read_end(&hidden_timing_, &hidden_timing_.config_read,
                armTicksToNs(armGetSystemTick()) / 1000000, read_ok);
        }
        if (!automatic_enabled_) {
            wake_recheck_pending_ = false;
            eventClose(&reason_event_);
            ptc_switch_ipc_clear_reason_ready(&bridge_.ipc);
            return false;
        }
        /* The backend identity changes only on boot or handoff: cache it instead
           of re-reading and re-parsing runtime-ready.json on every 250ms poll. */
        if (ms >= next_runtime_ms_) {
            char boot[17] = {0};
            ptc_overlay_hidden_timing_read_begin(&hidden_timing_, &hidden_timing_.runtime_read,
                armTicksToNs(armGetSystemTick()) / 1000000);
            runtime_known_ = ptc_reason_detail_runtime(s, APP_ROOT, PLAYWISE_BUILD_RELEASE_ID, boot);
            if (runtime_known_ && std::strcmp(boot, runtime_boot_) != 0) {
                eventClose(&reason_event_);
                next_reason_subscribe_ms_ = 0;
                std::snprintf(runtime_boot_, sizeof(runtime_boot_), "%s", boot);
                next_ready_ms_ = 0; /* a lease bound to the previous backend is useless */
                seen_sequence_ = 0;
                brief_notice_.active = false;
            }
            next_runtime_ms_ = ptc_overlay_hidden_runtime_next_ms(ms, runtime_known_);
            ptc_overlay_hidden_timing_read_end(&hidden_timing_, &hidden_timing_.runtime_read,
                armTicksToNs(armGetSystemTick()) / 1000000, runtime_known_);
        }
        if (!runtime_known_ || !bridge_.ipc.initialized || reason_session_ != bridge_.ipc.service.session)
            eventClose(&reason_event_);
        if (!runtime_known_) return false;
        if (!eventActive(&reason_event_) && ms >= next_reason_subscribe_ms_) {
            next_reason_subscribe_ms_ = ms + PTC_OVERLAY_HIDDEN_RUNTIME_MS;
            if (!bridge_.ipc.initialized) (void)ptc_switch_ipc_client_probe(&bridge_.ipc);
            if (bridge_.ipc.initialized &&
                ptc_switch_ipc_subscribe_reason(&bridge_.ipc, PLAYWISE_BUILD_RELEASE_ID, runtime_boot_, &reason_event_)) {
                reason_session_ = bridge_.ipc.service.session;
                ptc_overlay_reason_wake(&reason_wake_, armTicksToNs(armGetSystemTick()) / 1000000);
            }
            ms = armTicksToNs(armGetSystemTick()) / 1000000;
        }
        if (!neutral || automatic_failures_ >= PTC_OVERLAY_HIDDEN_FAILURE_LIMIT) return false;
        if (ms >= next_ready_ms_) {
            PtcReasonDetail ready{};
            std::snprintf(ready.release, sizeof(ready.release), "%s", PLAYWISE_BUILD_RELEASE_ID);
            std::snprintf(ready.boot, sizeof(ready.boot), "%s", runtime_boot_);
            ready.ui_instance = ui_instance_ms_;
            /* The lease stays short on purpose: an offer nobody answers is dropped
               once its observation ages, so the backend must only see a lease from
               a UI that was neutral within the last second and a half. Prefer the
               in-memory pctc:u IPC lease (zero SD writes); fall back to SD only
               when IPC is unavailable. */
            ready.issued_ms = ms; ready.expires_ms = ms + 1500;
            next_ready_ms_ = ms + 1000;
            ptc_overlay_hidden_timing_ready_begin(&hidden_timing_, ready.issued_ms, ready.expires_ms,
                armTicksToNs(armGetSystemTick()) / 1000000);
            bool written = (bridge_.ipc.initialized &&
                ptc_switch_ipc_update_reason_ready(&bridge_.ipc, &ready)) ||
                ptc_reason_detail_write(s, APP_ROOT, "ready", &ready);
            ptc_overlay_hidden_timing_ready_end(&hidden_timing_, armTicksToNs(armGetSystemTick()) / 1000000, written);
            if (!written) { ++automatic_failures_; return false; }
        }
        if (wake_recheck_pending_) {
            const uint64_t wake_now_ns = armTicksToNs(armGetSystemTick());
            const auto gate = tsl::detail::evaluateWakeRecheck(
                wake_recheck_pending_, wake_detected_ns_, wake_sleep_start_ns_, wake_now_ns,
                wake_lock_screen_enabled_, neutral && !brief_notice_.active,
                wake_last_user_input_ns_, wake_last_power_event_ns_, wake_last_home_event_ns_);
            if (gate == tsl::detail::WakeRecheckGate::Expired || gate == tsl::detail::WakeRecheckGate::Inactive) {
                wake_recheck_pending_ = false;
            } else if (gate == tsl::detail::WakeRecheckGate::Ready) {
                wake_recheck_pending_ = false;
                if (bridge_.ipc.initialized &&
                    ptc_switch_ipc_request_wake_reason_recheck(&bridge_.ipc, PLAYWISE_BUILD_RELEASE_ID, runtime_boot_)) {
                    ptc_overlay_reason_wake(&reason_wake_, wake_now_ns / 1000000);
                    next_request_audit_ms_ = 0;
                }
            }
        }
        if (!ptc_overlay_reason_scan(&reason_wake_, eventActive(&reason_event_), ms, &next_request_audit_ms_))
            return false;
        PtcReasonDetail offer{};
        ptc_overlay_hidden_timing_read_begin(&hidden_timing_, &hidden_timing_.request_read,
            armTicksToNs(armGetSystemTick()) / 1000000);
        bool read_ok = ptc_reason_detail_read(s, APP_ROOT, "request", &offer);
        ptc_overlay_hidden_timing_read_end(&hidden_timing_, &hidden_timing_.request_read,
            armTicksToNs(armGetSystemTick()) / 1000000, read_ok);
        if (!read_ok) return false;
        ms = armTicksToNs(armGetSystemTick()) / 1000000;
        if (std::strcmp(offer.boot, runtime_boot_) != 0) {
            /* An offer from another backend boot: refresh the identity soon, but
               at most once per second while a stale offer file lingers. */
            uint64_t retry = ptc_overlay_hidden_runtime_next_ms(ms, false);
            if (next_runtime_ms_ > retry) next_runtime_ms_ = retry;
            return false;
        }
        if (!ptc_reason_detail_fresh(&offer, PLAYWISE_BUILD_RELEASE_ID, runtime_boot_, ms) ||
            offer.ui_instance != ui_instance_ms_ || offer.sequence <= seen_sequence_) return false;
        seen_sequence_ = offer.sequence;
        PtcReasonDetail accepted = offer;
        accepted.stage_ms = ms;
        uint64_t deadline = ptc_reason_detail_draw_deadline(&offer, &accepted);
        if (!deadline) return false;
        if (!ptc_reason_detail_write(s, APP_ROOT, "accepted", &accepted)) {
            ++automatic_failures_;
            return false;
        }
        PtcReasonDetail current{};
        if (!ptc_reason_detail_read(s, APP_ROOT, "request", &current) ||
            !ptc_reason_detail_same(&offer, &current) ||
            armTicksToNs(armGetSystemTick()) / 1000000 >= offer.expires_ms) return false;
        automatic_offer_ = offer;
        automatic_accepted_ms_ = ms;
        automatic_draw_deadline_ms_ = deadline;
        next_automatic_check_ms_ = ms;
        s_automatic_detail = true;
        s_automatic_detail_confirmed = false;
        automatic_ack_written_ = false;
        return true;
    }

    void onFrameRendered() override
    {
        if (!s_automatic_detail || automatic_ack_written_) return;
        uint64_t ns = armTicksToNs(armGetSystemTick()), ms = ns / 1000000;
        if (ms < automatic_accepted_ms_ || ms >= automatic_draw_deadline_ms_) {
            hide();
            return;
        }
        PtcStorage *s = ptc_fs_storage_as_storage(&storage_);
        bool drawn = gui_ && !fadeAnimationPlaying() && gui_->automatic_detail_drawn(automatic_offer_);
        if (drawn || ms >= next_automatic_check_ms_) {
            /* The backend withdraws the file on cancellation. Check during
               refresh as well as before acknowledging a completed frame. */
            PtcReasonDetail current{};
            if (!ptc_reason_detail_read(s, APP_ROOT, "request", &current) ||
                !ptc_reason_detail_same(&current, &automatic_offer_)) {
                hide();
                return;
            }
            next_automatic_check_ms_ = ms + PTC_OVERLAY_HIDDEN_DETAIL_MS;
        }
        if (!drawn) return;
        if (!ptc_input_owner_accept(&s_input_owner, ns) ||
            armTicksToNs(armGetSystemTick()) / 1000000 >= automatic_draw_deadline_ms_) return;
        PtcReasonDetail ack = automatic_offer_;
        ack.stage_ms = ms;
        std::snprintf(ack.owner, sizeof(ack.owner), "%s", s_input_owner.owner);
        /* A rendered fresh correct page and a live input lease, not file deletion. */
        if (ptc_reason_detail_write(s, APP_ROOT, "ack", &ack)) {
            automatic_ack_written_ = true;
            s_automatic_detail_confirmed = true;
            gui_->prepare_automatic_frame(nullptr);
        } else {
            ++automatic_failures_;
            hide();
        }
    }

    void onShow() override
    {
        wake_recheck_pending_ = false;
        ptc_switch_ipc_clear_reason_ready(&bridge_.ipc);
        ptc_overlay_brief_poll(&brief_notice_, nullptr, APP_ROOT, PLAYWISE_BUILD_RELEASE_ID, runtime_boot_,
            armTicksToNs(armGetSystemTick()) / 1000000, false, false, false);
        next_config_ms_ = 0;
        s_display_clock.sampled = false;
        if (gui_) gui_->prepare_automatic_frame(s_automatic_detail ? &automatic_offer_ : nullptr,
            automatic_draw_deadline_ms_);
        s_owner_visible = true;
        if (!ptc_input_owner_acquire(&s_input_owner, armTicksToNs(armGetSystemTick()))) {
            hide();
        } else if (gui_) {
            gui_->start_child_open();
        }
        ptc_overlay_resource_snapshot("show", armTicksToNs(armGetSystemTick()), bridge_.ipc.initialized, bridge_.waiting);
    }

    void onHide() override
    {
        if (gui_) gui_->on_child_hidden(s_automatic_detail && !automatic_ack_written_);
        ptc_overlay_hidden_timing_init(&hidden_timing_);
        next_detail_poll_ms_ = 0;
        next_request_audit_ms_ = next_reason_audit_ms_ = 0;
        s_owner_visible = false;
        s_automatic_detail = false;
        s_automatic_detail_confirmed = false;
        ptc_input_owner_release(&s_input_owner, armTicksToNs(armGetSystemTick()));
        ptc_overlay_resource_snapshot("hide", armTicksToNs(armGetSystemTick()), bridge_.ipc.initialized, bridge_.waiting);
    }

    void initServices() override
    {
        fsdevMountSdmc();
        ptc_fs_storage_init(&storage_);
        ptc_overlay_hidden_timing_init(&hidden_timing_);
        ptc_input_owner_writer_init(&s_input_owner, ptc_fs_storage_as_storage(&storage_), APP_ROOT,
            PLAYWISE_BUILD_RELEASE_ID, armTicksToNs(armGetSystemTick()));
        load_overlay_config(ptc_fs_storage_as_storage(&storage_));
        ptc_overlay_bridge_init(&bridge_, APP_ROOT, ptc_fs_storage_as_storage(&storage_));
        ptc_ui_status_trace_init(&s_status_trace, ptc_fs_storage_as_storage(&storage_), APP_ROOT,
            PLAYWISE_BUILD_COMMIT, PLAYWISE_BUILD_RELEASE_ID, "overlay", armTicksToNs(armGetSystemTick()));
        ptc_overlay_input_init(&input_);
        ui_instance_ms_ = armTicksToNs(armGetSystemTick()) / 1000000;
    }

    void exitServices() override
    {
        onHide();
        eventClose(&reason_event_);
        ptc_ui_status_trace_close(&s_status_trace, armTicksToNs(armGetSystemTick()));
        ptc_overlay_bridge_exit(&bridge_);
        ptc_overlay_resource_snapshot("exit", armTicksToNs(armGetSystemTick()), bridge_.ipc.initialized, bridge_.waiting);
        fsdevUnmountDevice("sdmc");
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override
    {
        auto gui = initially<PctcGui>(&bridge_, &input_, s_overlay_theme);
        gui_ = static_cast<PctcGui *>(gui.get());
        return gui;
    }

private:
    void brief_diagnostic(uint64_t ms, bool blocked)
    {
        unsigned key = (brief_notice_.active ? 1u : 0u) | (automatic_enabled_ ? 2u : 0u) |
            (runtime_known_ ? 4u : 0u) | (blocked ? 8u : 0u) | (automatic_failures_ << 8);
        if ((!automatic_enabled_ && !brief_diag_sampled_) || brief_diag_updates_ >= 32 ||
            brief_diag_failures_ >= 5 || ms < next_brief_diag_ms_ ||
            (brief_diag_sampled_ && key == brief_diag_key_ &&
             !std::strcmp(brief_diag_seen_[0], brief_notice_.seen[0]) &&
             !std::strcmp(brief_diag_seen_[1], brief_notice_.seen[1]))) return;
        next_brief_diag_ms_ = ms + 1000;
        char text[3072], timing[1536];
        if (!ptc_overlay_hidden_timing_format(&hidden_timing_, timing, sizeof(timing))) {
            ++brief_diag_failures_;
            return;
        }
        int length = std::snprintf(text, sizeof(text), "{\"version\":1,\"release_id\":\"%s\",\"boot_id\":\"%s\","
            "\"ui_instance\":\"%llu\",\"sampled_mono_ms\":\"%llu\",\"automatic_enabled\":%s,"
            "\"runtime_known\":%s,\"home_power_blocked\":%s,\"detail_io_failures\":%u,"
            "\"brief_active\":%s,\"reason_file\":\"%s\",\"test_file\":\"%s\","
            "\"update_count\":%u,\"display_confirmed\":false,\"hidden_timing\":%s}",
            PLAYWISE_BUILD_RELEASE_ID, runtime_boot_, (unsigned long long)ui_instance_ms_,
            (unsigned long long)ms, automatic_enabled_ ? "true" : "false", runtime_known_ ? "true" : "false",
            blocked ? "true" : "false", automatic_failures_, brief_notice_.active ? "true" : "false",
            brief_notice_.seen[0], brief_notice_.seen[1], brief_diag_updates_ + 1, timing);
        if (length < 0 || static_cast<size_t>(length) >= sizeof(text)) {
            ++brief_diag_failures_;
            return;
        }
        PtcStorage *s = ptc_fs_storage_as_storage(&storage_);
        if (!s->vtable->write_text_atomic(s, "sdmc:/switch/playwise/support/reason-brief-receiver.json", text)) {
            ++brief_diag_failures_;
            return;
        }
        ++brief_diag_updates_;
        brief_diag_sampled_ = true;
        brief_diag_key_ = key;
        for (unsigned i = 0; i < 2; ++i)
            std::snprintf(brief_diag_seen_[i], sizeof(brief_diag_seen_[i]), "%s", brief_notice_.seen[i]);
    }

    PtcFsStorage storage_{};
    PtcOverlayBridge bridge_{};
    PtcOverlayInput input_{};
    PctcGui *gui_ = nullptr;
    PtcReasonDetail automatic_offer_{};
    uint64_t automatic_accepted_ms_ = 0, automatic_draw_deadline_ms_ = 0, next_automatic_check_ms_ = 0;
    PtcOverlayBriefNotice brief_notice_{};
    Event reason_event_{};
    Handle reason_session_ = INVALID_HANDLE;
    PtcOverlayReasonWake reason_wake_{};
    uint64_t next_reason_subscribe_ms_ = 0, next_request_audit_ms_ = 0, next_reason_audit_ms_ = 0;
    PtcOverlayHiddenTiming hidden_timing_{};
    uint64_t ui_instance_ms_ = 0, seen_sequence_ = 0, next_ready_ms_ = 0;
    uint64_t next_brief_read_ms_ = 0, next_detail_poll_ms_ = 0;
    uint64_t next_brief_diag_ms_ = 0;
    unsigned brief_diag_key_ = 0, brief_diag_updates_ = 0, brief_diag_failures_ = 0;
    bool brief_diag_sampled_ = false;
    char brief_diag_seen_[2][96] = {};
    unsigned automatic_failures_ = 0;
    bool automatic_ack_written_ = false;
    bool automatic_enabled_ = false;
    bool preloaded_hidden_ = false;
    bool wake_recheck_pending_ = false;
    bool wake_lock_screen_enabled_ = true;
    uint64_t wake_detected_ns_ = 0, wake_sleep_start_ns_ = 0;
    uint64_t wake_last_user_input_ns_ = 0, wake_last_power_event_ns_ = 0, wake_last_home_event_ns_ = 0;
    uint64_t next_config_ms_ = 0;
    uint64_t next_runtime_ms_ = 0;
    bool runtime_known_ = false;
    char runtime_boot_[17] = {0};
};

} // namespace

int main(int argc, char **argv)
{
    return tsl::loop<PctcOverlay>(argc, argv);
}
