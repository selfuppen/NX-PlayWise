#ifndef PTC_COMPANION_NRO_APP_INTERNAL_H
#define PTC_COMPANION_NRO_APP_INTERNAL_H

#include <switch.h>
#include "release_manifest.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../../companion/auth.h"
#include "../../companion/album_restriction.h"
#include "../../companion/file_protocol.h"
#include "../../companion/transport_client.h"
#include "../../companion/switch_ipc_client.h"
#include "../../platform/switch/fs_storage.h"
#include "../../platform/install_defaults.h"
#include "../../third_party/cjson/cJSON.h"
#include "../../common/support/support_export.h"
#include "../../common/time/ptc_time.h"
#include "../../common/rules/holiday_calendar.h"
#include "../../common/security/credential_policy.h"
#include "../../common/crypto/sha256.h"
#include "../../common/token/token_v2.h"
#include "../../common/version.h"
#include "../../third_party/qrcodegen/qrcodegen.h"
#include "ui_graphics.h"
#include "ui_state.h"
#include "ui_layout.h"
#ifndef PLAYWISE_EDEN
#include "hot_reload.h"
#endif
#ifdef PLAYWISE_EDEN
#include "eden_runtime.h"
#endif

#define APP_ROOT PLAYWISE_SD_ROOT
#define RULES_PATH APP_ROOT "/rules.json"
#define CONFIG_PATH APP_ROOT "/config.json"
#define CREDENTIALS_PATH APP_ROOT "/credentials.json"
#define ISSUED_NONCES_PATH APP_ROOT "/grant-issued.json"
#define LEDGER_PATH APP_ROOT "/ledger/used_nonces.jsonl"
#define REDEMPTION_HISTORY_PATH APP_ROOT "/ledger/redemption-history.jsonl"
#define ACTIVITY_HISTORY_PATH APP_ROOT "/activity/history.jsonl"
#ifndef PLAYWISE_EDEN
#define STANDARD_BOOT_FLAG_PATH "sdmc:/atmosphere/contents/4200000000BD2300/flags/boot2.flag"
#endif
#define RESULT_TEXT_SIZE 8192
#define REQUEST_TIMEOUT_MS 30000
#define INPUT_LOOP_SLEEP_NS 20000000LL
#define INPUT_LOOP_SLEEP_FAST_NS 5000000LL
#define INPUT_LOOP_MS 20
#define DRAW_INTERVAL_MS 40
#define DRAW_INTERVAL_FAST_MS 20
#define BACKGROUND_POLL_INTERVAL_MS 100
#define INPUT_SAMPLES_FOR_MS(milliseconds) (((milliseconds) + INPUT_LOOP_MS - 1) / INPUT_LOOP_MS)
#define HIDDEN_HOLD_TICKS INPUT_SAMPLES_FOR_MS(2000)
#define HIDDEN_LEFT_SHOULDER_MASK (HidNpadButton_L | HidNpadButton_ZL)
#define HIDDEN_RIGHT_SHOULDER_MASK (HidNpadButton_R | HidNpadButton_ZR)
#define CUSTOM_SHORTCUT_HOLD_TICKS INPUT_SAMPLES_FOR_MS(400)
#define DANGER_CONFIRM_HOLD_TICKS INPUT_SAMPLES_FOR_MS(1000)
#define PIN_KEYBOARD_HOLD_TICKS INPUT_SAMPLES_FOR_MS(1000)
#define DIRECTION_REPEAT_INITIAL_TICKS INPUT_SAMPLES_FOR_MS(400)
#define DIRECTION_REPEAT_TICKS INPUT_SAMPLES_FOR_MS(100)
#define CUSTOM_SHORTCUT_VALID_MASK (HidNpadButton_X | HidNpadButton_Y | HidNpadButton_Plus | HidNpadButton_Minus | \
    HidNpadButton_L | HidNpadButton_ZL | HidNpadButton_R | HidNpadButton_ZR | \
    HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right)
#define STICK_DEADZONE 16000
#define DIRECTION_BUTTON_MASK (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right)


typedef enum {
    AUTH_RETRY_NONE = 0,
    AUTH_RETRY_ENTER_PARENT,
    AUTH_RETRY_DEFAULT_SETUP_PIN,
    AUTH_RETRY_SETUP_PIN,
    AUTH_RETRY_SAVE_CREDENTIAL,
    AUTH_RETRY_CHANGE_PIN,
    AUTH_RETRY_EDIT_URL,
    AUTH_RETRY_RESET_URL,
    AUTH_RETRY_GENERATE_CODE,
    AUTH_RETRY_SHOW_QR,
    AUTH_RETRY_EXPORT_CONFIG,
    AUTH_RETRY_REVEAL_CREDENTIAL,
    AUTH_RETRY_CLEAR_REDEMPTION_HISTORY,
    AUTH_RETRY_CLEAR_ACTIVITY_HISTORY,
    AUTH_RETRY_SKIP_BEDTIME
} AuthRetryAction;

typedef struct {
    PtcCompanionFileClient client;
    PtcCompanionTransportClient transport;
    PtcSwitchIpcClient ipc;
    PtcCompanionAuth auth;
    PtcUiModel model;
    PtcUiThemePreference theme_preference;
    PtcUiSystemTheme system_theme;
    PtcUiThemeView theme_view;
    volatile bool theme_refresh_pending;
    volatile bool status_refresh_pending;
    char active_request_id[PTC_COMPANION_REQUEST_ID_SIZE];
    char last_result[RESULT_TEXT_SIZE];
    int elapsed_ms;
    int hidden_ticks;
    PtcUiShortcutHoldState custom_shortcut_hold;
    PtcUiConfirmHoldState confirm_hold;
    bool minus_pending;
    PtcUiValueRepeatState r_stick_repeat;
    int r_stick_prev_h_dir;
    bool waiting;
    bool animating;
    bool exit_requested;
    PtcUiView request_view;
    int64_t last_setup_refresh_second;
    int pending_today_action;
    int pending_parent_page;
    bool pending_leave_parent;
    bool code_preview_recheck;
    bool code_previous_after_available;
    bool code_previous_after_zero;
    bool code_previous_capped;
    bool code_previous_converts_unlimited;
    PtcPendingRedemption pending_redemption;
    bool recovering_redemption;
    AuthRetryAction auth_retry_action;
    PtcUiOverlay auth_return_overlay;
    int64_t auth_cooldown_until;
#ifndef PLAYWISE_EDEN
    PtcHotReloadController hot_reload;
    bool hot_reload_prompted;
    bool hot_reload_ipc_closed;
    bool hot_reload_terminal_handled;
    bool hot_reload_child_notice_shown;
#endif
} UiState;

extern PadState *g_active_pad;

bool standard_backend_expected(void);
void current_environment_fingerprint(UiState *ui, char out[65]);
void sync_hot_reload_model(UiState *ui);
void open_hot_reload_confirmation(UiState *ui);
void poll_hot_reload(UiState *ui);
bool update_animations(PtcUiModel *model);
void make_next_request_id(char *out, size_t out_size);
const char *auth_status_zh(PtcAuthStatus status);
void set_message(UiState *ui, const char *prefix, PtcCompanionStatus status);
void show_auth_error(UiState *ui, const char *title, const char *message, int64_t retry_after);
void close_auth_error(UiState *ui, bool cancelled);
void set_command_name(UiState *ui, const char *type);
void sync_transport_label(UiState *ui);
void set_local_sd_command(UiState *ui, const char *command_name);
bool hidden_parent_combo_held(u64 buttons);
bool custom_parent_combo_held(const UiState *ui, u64 buttons);
void refresh_disable_flag(UiState *ui);
bool weekly_editing_blocked(UiState *ui);
u64 stick_direction_buttons(HidAnalogStickState stick);
bool switch_random(uint8_t *out, size_t out_size, void *ctx);
bool keyboard_input( const char *header, const char *guide, char *out, size_t out_size, bool password, bool numeric, bool download_code);
bool edit_date_range_start(UiState *ui, uint16_t *start, uint16_t *end);
bool edit_date_range_span(UiState *ui, uint16_t start, uint16_t *end);
bool pin_input(UiState *ui, const char *title, const char *guide, char *out, size_t out_size);
u64 shortcut_preset_mask(int index);
bool shortcut_mask_valid(u64 mask);
void refresh_custom_shortcut_label(UiState *ui);
void refresh_shortcut_draft_label(UiState *ui);
void load_ui_preferences(UiState *ui);
bool save_ui_preferences(UiState *ui);
bool save_setup_step(UiState *ui, int step);
void edit_overlay_minutes(UiState *ui);
void edit_weekly_minutes(UiState *ui);
void open_offline_code_input(UiState *ui);
void begin_wait(UiState *ui, const char *type, const char *message);
void submit_transport_empty(UiState *ui, const char *type, const char *ok_message, const char *fail_prefix);
void submit_status(UiState *ui);
void activate_parent_status(UiState *ui);
void enter_child_area(UiState *ui);
void show_pending_redemption(UiState *ui);
void poll_pending_redemption(UiState *ui);
bool restore_pending_redemption(UiState *ui);
void submit_preview_offline_code(UiState *ui, const char *code);
void submit_minutes(UiState *ui, PtcUiOperation operation, uint16_t minutes);
void submit_weekly(UiState *ui);
void submit_holiday_policy(UiState *ui);
void edit_scheduled_minutes(UiState *ui);
void edit_grant_minutes(UiState *ui);
void load_rule_drafts(UiState *ui);
void poll_result(UiState *ui, bool force);
void refresh_setup_activation(UiState *ui);
void enter_parent_area(UiState *ui);
void select_setup_shortcut(UiState *ui, int index);
bool commit_shortcut_preferences(UiState *ui);
void open_shortcut_manager(UiState *ui);
void setup_pin(UiState *ui);
bool ensure_default_setup_pin(UiState *ui);
void setup_previous(UiState *ui);
void setup_primary(UiState *ui);
void handle_setup_input(UiState *ui, u64 down, u64 held);
void open_confirm_overlay(UiState *ui, PtcUiOperation operation, const char *title, const char *body);
void open_weekly_page(UiState *ui);
void refresh_security_state(UiState *ui);
bool verify_sensitive_pin(UiState *ui, const char *action);
bool load_redemption_history(UiState *ui);
void open_redemption_history(UiState *ui);
void request_clear_redemption_history(UiState *ui);
void submit_scheduled_override(UiState *ui);
void submit_autonomy_policy(UiState *ui);
void submit_bedtime_confirmation(UiState *ui);
void submit_bedtime_policy(UiState *ui);
void submit_bedtime_skip(UiState *ui);
void open_activity_history(UiState *ui);
void request_clear_activity_history(UiState *ui);
bool commit_credential(UiState *ui);
void open_credential_manager(UiState *ui, int kind);
void edit_credential_input(UiState *ui);
void randomize_credential(UiState *ui);
void request_save_credential(UiState *ui);
void change_parent_pin(UiState *ui);
void show_grant_manager(UiState *ui, int selection);
void open_grant_manager(UiState *ui);
void open_local_grant(UiState *ui);
void edit_pairing_base_url(UiState *ui);
void apply_default_pairing_base_url(UiState *ui);
void request_reset_pairing_base_url(UiState *ui);
void generate_local_grant_code(UiState *ui);
void show_pairing_qr(UiState *ui);
void export_parent_import(UiState *ui);
void reveal_current_credential(UiState *ui);
void dispatch_auth_retry(UiState *ui, AuthRetryAction action);
void export_diagnostics(UiState *ui);
void refresh_theme(UiState *ui);
void trigger_resume_status_refresh(UiState *ui, int *background_poll_elapsed_ms);
void applet_hook(AppletHookType hook, void *param);
bool apply_theme_preference(UiState *ui, PtcUiThemePreference preference);
void handle_today_action_ready(UiState *ui, int index);
void handle_parent_action(UiState *ui);
void confirm_operation(UiState *ui);
void accept_numpad(UiState *ui);
void update_weekly_dirty(UiState *ui);
void update_holiday_dirty(UiState *ui);
void refresh_album_restriction(UiState *ui);
void save_weekly_from_page(UiState *ui);
void save_holiday_from_page(UiState *ui);
void apply_pending_navigation(UiState *ui);
void refresh_recovery_state(UiState *ui);
void discard_holiday_draft(UiState *ui);
void update_bedtime_dirty(UiState *ui);
void discard_bedtime_draft(UiState *ui);
void save_bedtime_from_page(UiState *ui);
void select_bedtime_section(UiState *ui, int section);
void open_bedtime_window_editor(UiState *ui, int weekday);
void open_bedtime_special_editor(UiState *ui, int kind);
void open_bedtime_time_editor(UiState *ui, PtcUiBedtimeTimeTarget target);
void request_bedtime_leave(UiState *ui, int target_page, bool leave_parent);
void request_parent_navigation(UiState *ui, int target_page, bool leave_parent);
void close_code_result(UiState *ui);
void handle_overlay_input(UiState *ui, u64 down);
void handle_touch(UiState *ui, int x, int y);
void draw(UiState *ui);
void retry_error(UiState *ui);

#endif
