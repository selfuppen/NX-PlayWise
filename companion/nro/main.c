#include <switch.h>
#include "release_manifest.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../companion/auth.h"
#include "../../companion/album_restriction.h"
#include "../../companion/file_protocol.h"
#include "../../companion/transport_client.h"
#include "../../companion/switch_ipc_client.h"
#include "../../platform/switch/fs_storage.h"
#include "../../third_party/cjson/cJSON.h"
#include "../../common/support/support_export.h"
#include "../../common/time/ptc_time.h"
#include "../../common/rules/holiday_calendar.h"
#include "../../common/security/credential_policy.h"
#include "../../common/token/token_v2.h"
#include "../../common/version.h"
#include "../../third_party/qrcodegen/qrcodegen.h"
#include "ui_graphics.h"

#define APP_ROOT "sdmc:/switch/playwise"
#define RULES_PATH APP_ROOT "/rules.json"
#define CONFIG_PATH APP_ROOT "/config.json"
#define CREDENTIALS_PATH APP_ROOT "/credentials.json"
#define ISSUED_NONCES_PATH APP_ROOT "/grant-issued.json"
#define LEDGER_PATH APP_ROOT "/ledger/used_nonces.jsonl"
#define RESULT_TEXT_SIZE 8192
#define REQUEST_TIMEOUT_MS 30000
#define LOOP_SLEEP_NS 100000000LL
#define LOOP_SLEEP_MS 100
#define HIDDEN_HOLD_TICKS 20
#define HIDDEN_LEFT_SHOULDER_MASK (HidNpadButton_L | HidNpadButton_ZL)
#define HIDDEN_RIGHT_SHOULDER_MASK (HidNpadButton_R | HidNpadButton_ZR)
#define CUSTOM_SHORTCUT_HOLD_TICKS 4
#define DANGER_CONFIRM_HOLD_TICKS 10
#define CUSTOM_SHORTCUT_CAPTURE_MASK (HidNpadButton_X | HidNpadButton_Y | HidNpadButton_Plus | HidNpadButton_Minus | \
    HidNpadButton_L | HidNpadButton_ZL | HidNpadButton_R | HidNpadButton_ZR | \
    HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right)
#define STICK_DEADZONE 16000
#define DIRECTION_BUTTON_MASK (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right)

__attribute__((used)) static const char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

typedef enum {
    AUTH_RETRY_NONE = 0,
    AUTH_RETRY_ENTER_PARENT,
    AUTH_RETRY_SETUP_PIN,
    AUTH_RETRY_SAVE_CREDENTIAL,
    AUTH_RETRY_CHANGE_PIN,
    AUTH_RETRY_EDIT_URL,
    AUTH_RETRY_RESET_URL,
    AUTH_RETRY_GENERATE_CODE,
    AUTH_RETRY_SHOW_QR,
    AUTH_RETRY_EXPORT_CONFIG,
    AUTH_RETRY_REVEAL_CREDENTIAL
} AuthRetryAction;

typedef struct {
    PtcCompanionFileClient client;
    PtcCompanionTransportClient transport;
    PtcSwitchIpcClient ipc;
    PtcCompanionAuth auth;
    PtcUiModel model;
    char active_request_id[PTC_COMPANION_REQUEST_ID_SIZE];
    char last_result[RESULT_TEXT_SIZE];
    int elapsed_ms;
    int hidden_ticks;
    int custom_shortcut_ticks;
    bool custom_shortcut_latched;
    bool minus_pending;
    bool plus_exit_pending;
    int danger_confirm_ticks;
    bool waiting;
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
} UiState;

static void request_parent_navigation(UiState *ui, int target_page, bool leave_parent);

static void handle_parent_action(UiState *ui);
static void handle_today_action_ready(UiState *ui, int index);
static void refresh_security_state(UiState *ui);
static void update_weekly_dirty(UiState *ui);
static void update_holiday_dirty(UiState *ui);
static void apply_pending_navigation(UiState *ui);
static void handle_setup_input(UiState *ui, u64 down, u64 held);
static void refresh_album_restriction(UiState *ui);
static void open_confirm_overlay(UiState *ui, PtcUiOperation operation, const char *title, const char *body);
static void retry_error(UiState *ui);
static void dispatch_auth_retry(UiState *ui, AuthRetryAction action);
static void show_pending_redemption(UiState *ui);
static void show_grant_manager(UiState *ui, int selection);

static int64_t unix_ms_now(void)
{
    return (int64_t)time(NULL) * 1000;
}

static void make_next_request_id(char *out, size_t out_size)
{
    static uint16_t counter = 0;
    uint16_t random16 = (uint16_t)((rand() ^ counter++) & 0xffff);
    if (ptc_companion_make_request_id(out, out_size, unix_ms_now() + (counter % 1000), random16) != PTC_COMPANION_OK) {
        snprintf(out, out_size, "0-%04x", random16);
    }
}

static const char *companion_status_zh(PtcCompanionStatus status)
{
    switch (status) {
    case PTC_COMPANION_OK:
        return "成功";
    case PTC_COMPANION_PENDING:
        return "后台仍在处理";
    case PTC_COMPANION_TIMEOUT:
        return "等待后台响应超时";
    case PTC_COMPANION_BAD_ARGUMENT:
        return "请求参数无效";
    case PTC_COMPANION_WRITE_FAILED:
        return "写入请求失败";
    case PTC_COMPANION_RENAME_FAILED:
        return "提交请求失败";
    case PTC_COMPANION_RESULT_INVALID:
        return "后台结果格式无效";
    case PTC_COMPANION_RESULT_MISMATCH:
        return "后台结果与本次请求不匹配";
    default:
        return "未知错误";
    }
}

static const char *auth_status_zh(PtcAuthStatus status)
{
    switch (status) {
    case PTC_AUTH_OK:
        return "成功";
    case PTC_AUTH_EMPTY:
        return "尚未设置 任我玩 PIN";
    case PTC_AUTH_BAD_ARGUMENT:
        return "PIN 参数无效";
    case PTC_AUTH_READ_FAILED:
        return "无法读取 PIN 设置";
    case PTC_AUTH_WRITE_FAILED:
        return "无法保存 PIN 设置";
    case PTC_AUTH_INVALID_FILE:
        return "PIN 设置文件无效";
    case PTC_AUTH_DENIED:
        return "PIN 不正确";
    case PTC_AUTH_COOLDOWN:
        return "PIN 错误次数过多，暂时锁定";
    default:
        return "未知认证错误";
    }
}

static void set_message(UiState *ui, const char *prefix, PtcCompanionStatus status)
{
    ui->model.feedback_detail[0] = '\0';
    snprintf(ui->model.message, sizeof(ui->model.message), "%s：%s", prefix, companion_status_zh(status));
    snprintf(ui->model.result_status, sizeof(ui->model.result_status), "error");
    if (ui->model.view == PTC_UI_CHILD) {
        ui->model.view = PTC_UI_ERROR;
    }
}

static void show_auth_error(UiState *ui, const char *title, const char *message, int64_t retry_after)
{
    if (!ui) return;
    ui->auth_return_overlay = ui->model.overlay == PTC_UI_OVERLAY_AUTH_ERROR
        ? ui->auth_return_overlay : ui->model.overlay;
    ui->model.overlay = PTC_UI_OVERLAY_AUTH_ERROR;
    ui->auth_cooldown_until = retry_after > 0 ? (int64_t)time(NULL) + retry_after : 0;
    ui->model.auth_cooldown_seconds = retry_after > 0 ? (int)retry_after : 0;
    snprintf(ui->model.auth_error_title, sizeof(ui->model.auth_error_title), "%s",
             title ? title : "PIN 验证未通过");
    snprintf(ui->model.auth_error_message, sizeof(ui->model.auth_error_message), "%s",
             message ? message : "PIN 不正确，请重试。");
    snprintf(ui->model.message, sizeof(ui->model.message), "%s", ui->model.auth_error_message);
    snprintf(ui->model.result_status, sizeof(ui->model.result_status), "error");
}

static void close_auth_error(UiState *ui, bool cancelled)
{
    if (!ui) return;
    ui->model.overlay = ui->auth_return_overlay;
    ui->auth_return_overlay = PTC_UI_OVERLAY_NONE;
    ui->auth_cooldown_until = 0;
    ui->model.auth_cooldown_seconds = 0;
    ui->model.auth_error_title[0] = '\0';
    ui->model.auth_error_message[0] = '\0';
    if (cancelled) {
        ui->auth_retry_action = AUTH_RETRY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消 PIN 验证。");
        ui->model.result_status[0] = '\0';
    }
}

static void set_command_name(UiState *ui, const char *type)
{
    ptc_ui_set_execution(
        &ui->model,
        ptc_companion_request_command_label_zh(type),
        ui->model.transport_label);
}

static void sync_transport_label(UiState *ui)
{
    ptc_ui_set_execution(
        &ui->model,
        ui->model.command_name,
        ptc_companion_transport_route_label_zh(ptc_companion_transport_route(&ui->transport)));
}

static void set_local_sd_command(UiState *ui, const char *command_name)
{
    ptc_ui_set_execution(
        &ui->model,
        command_name,
        ptc_companion_transport_route_label_zh(PTC_TRANSPORT_ROUTE_LOCAL_SD_FLAG));
}

static bool hidden_parent_combo_held(u64 buttons)
{
    return (buttons & HidNpadButton_X) &&
           (buttons & HIDDEN_LEFT_SHOULDER_MASK) &&
           (buttons & HIDDEN_RIGHT_SHOULDER_MASK);
}

static bool custom_parent_combo_held(const UiState *ui, u64 buttons)
{
    if (!ui || !ui->model.custom_shortcut_enabled || ui->model.custom_shortcut_mask == 0) {
        return false;
    }
    return (buttons & ui->model.custom_shortcut_mask) == ui->model.custom_shortcut_mask;
}

static void refresh_disable_flag(UiState *ui)
{
    char path[160];
    bool was_disabled;
    if (!ui || !ui->client.storage) {
        return;
    }
    was_disabled = ui->model.disable_flag_present;
    snprintf(path, sizeof(path), "%s/flags/disable.flag", APP_ROOT);
    ui->model.disable_flag_present = ui->client.storage->vtable->exists(ui->client.storage, path);
    if (ui->model.disable_flag_present &&
        ui->model.overlay == PTC_UI_OVERLAY_NUMPAD &&
        ui->model.numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) {
        ptc_ui_numpad_finish(&ui->model);
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "检测到紧急停用，本次未确认的分钟输入已取消；此前周计划草稿仍然保留。");
    }
    if (!was_disabled && ui->model.disable_flag_present &&
        ui->model.overlay == PTC_UI_OVERLAY_WEEKLY_LEAVE) {
        ui->model.weekly_leave_selection = 2;
    }
}

static bool weekly_editing_blocked(UiState *ui)
{
    bool cancelling_weekly_input = ui &&
        ui->model.overlay == PTC_UI_OVERLAY_NUMPAD &&
        ui->model.numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES;
    refresh_disable_flag(ui);
    if (!ui->model.disable_flag_present) {
        return false;
    }
    if (!cancelling_weekly_input) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "紧急停用中，周计划暂时只读；解除停用后才能修改和保存。");
    }
    return true;
}

static u64 stick_direction_buttons(HidAnalogStickState stick)
{
    u64 buttons = 0;
    if (stick.x > STICK_DEADZONE) buttons |= HidNpadButton_Right;
    if (stick.x < -STICK_DEADZONE) buttons |= HidNpadButton_Left;
    if (stick.y > STICK_DEADZONE) buttons |= HidNpadButton_Up;
    if (stick.y < -STICK_DEADZONE) buttons |= HidNpadButton_Down;
    return buttons;
}

static bool switch_random(uint8_t *out, size_t out_size, void *ctx)
{
    (void)ctx;
    randomGet(out, out_size);
    return true;
}

static bool keyboard_input(
    const char *header,
    const char *guide,
    char *out,
    size_t out_size,
    bool password,
    bool numeric,
    bool download_code)
{
    SwkbdConfig keyboard;
    Result result;
    if (!out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    result = swkbdCreate(&keyboard, 0);
    if (R_FAILED(result)) {
        return false;
    }
    if (password) {
        swkbdConfigMakePresetPassword(&keyboard);
    } else if (download_code) {
        swkbdConfigMakePresetDownloadCode(&keyboard);
    } else {
        swkbdConfigMakePresetDefault(&keyboard);
    }
    if (numeric) swkbdConfigSetType(&keyboard, SwkbdType_NumPad);
    swkbdConfigSetStringLenMin(&keyboard, 1);
    swkbdConfigSetStringLenMax(&keyboard, (u32)(out_size - 1));
    swkbdConfigSetHeaderText(&keyboard, header);
    swkbdConfigSetGuideText(&keyboard, guide);
    swkbdConfigSetOkButtonText(&keyboard, "确认");
    result = swkbdShow(&keyboard, out, out_size);
    swkbdClose(&keyboard);
    return R_SUCCEEDED(result) && out[0] != '\0';
}

static u64 shortcut_preset_mask(int index)
{
    static const u64 masks[] = {
        HidNpadButton_L | HidNpadButton_R,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Up,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Down,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Left,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Right,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Plus,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Minus,
        HidNpadButton_ZL | HidNpadButton_ZR,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Up,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Down,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Left,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Right,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Plus,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Minus
    };
    if (index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        return masks[0];
    }
    return masks[index];
}

static bool shortcut_mask_valid(u64 mask)
{
    return mask != 0 && (mask & ~((u64)CUSTOM_SHORTCUT_CAPTURE_MASK)) == 0;
}

static bool parse_shortcut_mask(const char *text, u64 *out)
{
    char *end = NULL;
    unsigned long long value;
    if (!text || !text[0] || !out) {
        return false;
    }
    value = strtoull(text, &end, 16);
    if (end == text || *end != '\0' || value == 0) {
        return false;
    }
    if (!shortcut_mask_valid((u64)value)) {
        return false;
    }
    *out = (u64)value;
    return true;
}

static void append_shortcut_token(char *out, size_t out_size, bool *first, const char *token)
{
    size_t length;
    if (!out || !first || !token || !token[0]) {
        return;
    }
    length = strlen(out);
    snprintf(out + length, out_size > length ? out_size - length : 0,
             "%s%s", *first ? "" : " + ", token);
    *first = false;
}

static void format_shortcut_label(u64 mask, char *out, size_t out_size)
{
    static const struct {
        u64 mask;
        const char *label;
    } buttons[] = {
        {HidNpadButton_B, "B"},
        {HidNpadButton_L, "L"},
        {HidNpadButton_ZL, "ZL"},
        {HidNpadButton_R, "R"},
        {HidNpadButton_ZR, "ZR"},
        {HidNpadButton_Up, "上"},
        {HidNpadButton_Down, "下"},
        {HidNpadButton_Left, "左"},
        {HidNpadButton_Right, "右"},
        {HidNpadButton_X, "X"},
        {HidNpadButton_Y, "Y"},
        {HidNpadButton_Plus, "Plus(＋)"},
        {HidNpadButton_Minus, "Minus(－)"}
    };
    bool first = true;
    size_t index;
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    for (index = 0; index < sizeof(buttons) / sizeof(buttons[0]); ++index) {
        if ((mask & buttons[index].mask) != 0) {
            append_shortcut_token(out, out_size, &first, buttons[index].label);
        }
    }
    if (first) {
        snprintf(out, out_size, "未设置");
    }
}

static void refresh_custom_shortcut_label(UiState *ui)
{
    if (!ui) {
        return;
    }
    format_shortcut_label(ui->model.custom_shortcut_mask,
                          ui->model.custom_shortcut_label,
                          sizeof(ui->model.custom_shortcut_label));
}

static void refresh_shortcut_draft_label(UiState *ui)
{
    if (!ui) return;
    format_shortcut_label(ui->model.shortcut_draft_mask,
                          ui->model.shortcut_draft_label,
                          sizeof(ui->model.shortcut_draft_label));
}

static void load_ui_preferences(UiState *ui)
{
    char text[RESULT_TEXT_SIZE];
    u64 mask;
    cJSON *root;
    const cJSON *item;
    if (!ui) {
        return;
    }
    ui->model.custom_shortcut_mask = shortcut_preset_mask(PTC_UI_SHORTCUT_PRESET_LR);
    ui->model.custom_shortcut_enabled = false;
    ui->model.show_parent_shortcut_hint = true;
    ui->model.setup_step = 0;
    ui->model.setup_shortcut_index = PTC_UI_SHORTCUT_PRESET_LR;
    ui->model.setup_zone_index = 1;
    ui->model.shortcut_draft_mask = ui->model.custom_shortcut_mask;
    ui->model.shortcut_draft_enabled = false;
    ui->model.shortcut_draft_show_hint = true;
    refresh_shortcut_draft_label(ui);
    if (!ui->client.storage->vtable->read_text(ui->client.storage, CONFIG_PATH, text, sizeof(text))) {
        refresh_custom_shortcut_label(ui);
        return;
    }
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        refresh_custom_shortcut_label(ui);
        return;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "parent_shortcut_mask");
    if (cJSON_IsString(item) && parse_shortcut_mask(item->valuestring, &mask)) {
        ui->model.custom_shortcut_mask = mask;
        /* Existing installations predate the explicit enabled flag. */
        ui->model.custom_shortcut_enabled = true;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "custom_shortcut_enabled");
    if (cJSON_IsBool(item)) {
        ui->model.custom_shortcut_enabled = cJSON_IsTrue(item);
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "show_parent_shortcut_hint");
    if (cJSON_IsBool(item)) {
        ui->model.show_parent_shortcut_hint = cJSON_IsTrue(item);
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "setup_wizard_step");
    if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= PTC_UI_SETUP_ZONE) {
        ui->model.setup_step = item->valueint;
        if (item->valueint == 4) {
            cJSON *wizard_version = cJSON_GetObjectItemCaseSensitive(root, "setup_wizard_version");
            if (!cJSON_IsNumber(wizard_version) || wizard_version->valueint < 2) ui->model.setup_step = PTC_UI_SETUP_ZONE;
        }
    }
    for (int index = 0; index < PTC_UI_SHORTCUT_PRESET_COUNT; ++index) {
        if (ui->model.custom_shortcut_mask == shortcut_preset_mask(index)) {
            ui->model.setup_shortcut_index = index;
            break;
        }
    }
    cJSON_Delete(root);
    refresh_custom_shortcut_label(ui);
    ui->model.shortcut_draft_mask = ui->model.custom_shortcut_mask;
    ui->model.shortcut_draft_enabled = ui->model.custom_shortcut_enabled;
    ui->model.shortcut_draft_show_hint = ui->model.show_parent_shortcut_hint;
    refresh_shortcut_draft_label(ui);
}

static bool save_ui_preferences(UiState *ui)
{
    char text[RESULT_TEXT_SIZE];
    char mask_text[32];
    cJSON *root;
    char *rendered;
    bool ok;
    if (!ui || !shortcut_mask_valid(ui->model.custom_shortcut_mask)) {
        return false;
    }
    if (ui->client.storage->vtable->read_text(ui->client.storage, CONFIG_PATH, text, sizeof(text))) {
        root = cJSON_Parse(text);
        if (!cJSON_IsObject(root)) {
            cJSON_Delete(root);
            return false;
        }
    } else {
        root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "version", 1);
    }
    snprintf(mask_text, sizeof(mask_text), "%llx", (unsigned long long)ui->model.custom_shortcut_mask);
    cJSON_DeleteItemFromObject(root, "parent_shortcut_mask");
    cJSON_AddStringToObject(root, "parent_shortcut_mask", mask_text);
    cJSON_DeleteItemFromObject(root, "custom_shortcut_enabled");
    cJSON_AddBoolToObject(root, "custom_shortcut_enabled", ui->model.custom_shortcut_enabled);
    cJSON_DeleteItemFromObject(root, "show_parent_shortcut_hint");
    cJSON_AddBoolToObject(root, "show_parent_shortcut_hint", ui->model.show_parent_shortcut_hint);
    cJSON_DeleteItemFromObject(root, "setup_wizard_step");
    cJSON_AddNumberToObject(root, "setup_wizard_step", ui->model.setup_step);
    cJSON_DeleteItemFromObject(root, "setup_wizard_version");
    cJSON_AddNumberToObject(root, "setup_wizard_version", 2);
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = rendered && ui->client.storage->vtable->write_text_atomic(ui->client.storage, CONFIG_PATH, rendered);
    free(rendered);
    return ok;
}

static bool save_setup_step(UiState *ui, int step)
{
    int previous;
    if (!ui || step < 0 || step > PTC_UI_SETUP_ZONE) {
        return false;
    }
    previous = ui->model.setup_step;
    ui->model.setup_step = step;
    if (save_ui_preferences(ui)) {
        return true;
    }
    ui->model.setup_step = previous;
    snprintf(ui->model.message, sizeof(ui->model.message), "无法保存首次设置进度，请确认 SD 卡可写。");
    return false;
}

static void edit_overlay_minutes(UiState *ui)
{
    char guide[96];
    snprintf(guide, sizeof(guide), "输入 %u 到 %u 分钟",
             (unsigned int)ui->model.minimum_minutes, (unsigned int)ui->model.maximum_minutes);
    ptc_ui_numpad_open(
        &ui->model, PTC_UI_NUMPAD_MINUTES, PTC_UI_OVERLAY_MINUTES,
        ui->model.overlay_title, guide, 4,
        ui->model.minimum_minutes, ui->model.maximum_minutes, ui->model.draft_minutes);
}

static void edit_weekly_minutes(UiState *ui)
{
    static const char *WEEKDAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    PtcDayRule *day;
    if (weekly_editing_blocked(ui)) {
        return;
    }
    day = &ui->model.draft_week[ui->model.editor_index];
    if (day->mode == PTC_RULE_MODE_LIMIT) {
        char title[64];
        char guide[128];
        snprintf(title, sizeof(title), "设置%s的周计划额度", WEEKDAYS[ui->model.editor_index]);
        snprintf(guide, sizeof(guide), "输入 1 到 1440 分钟\n仅修改%s的周计划模板", WEEKDAYS[ui->model.editor_index]);
        ptc_ui_numpad_open(
            &ui->model, PTC_UI_NUMPAD_WEEKLY_MINUTES, PTC_UI_OVERLAY_NONE,
            title, guide, 4, 1, 1440, day->minutes);
    }
}

static void open_offline_code_input(UiState *ui)
{
    if (ui->recovering_redemption) {
        show_pending_redemption(ui);
        return;
    }
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "紧急停用已开启，当前不能兑换加时码；状态和恢复仍可使用。");
        return;
    }
    ptc_ui_numpad_open(
        &ui->model, PTC_UI_NUMPAD_OFFLINE_CODE, PTC_UI_OVERLAY_NONE,
        "A 输入加时码", "加时之前，记得向窗外远眺至少 5 分钟，\n让眼睛放松一下吧！", 8, 0, 0, 0);
}

static void begin_wait(UiState *ui, const char *type, const char *message)
{
    ui->request_view = ui->model.view;
    ui->waiting = true;
    ui->elapsed_ms = 0;
    ui->last_result[0] = '\0';
    ui->model.result_status[0] = '\0';
    ui->model.feedback_detail[0] = '\0';
    set_command_name(ui, type);
    sync_transport_label(ui);
    snprintf(ui->model.message, sizeof(ui->model.message), "%s", message);
}

static void submit_transport_empty(UiState *ui, const char *type, const char *ok_message, const char *fail_prefix)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_empty(&ui->transport, ui->active_request_id, time(NULL), type);
    set_command_name(ui, type);
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, type, ok_message);
        return;
    }
    ui->waiting = false;
    set_message(ui, fail_prefix, status);
}

static void submit_status(UiState *ui)
{
    PtcCompanionStatus status;
    if (!ui || ui->waiting) {
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_status(&ui->transport, ui->active_request_id, time(NULL));
    set_command_name(ui, "status");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "status", "正在刷新今天的状态…");
    else set_message(ui, "刷新失败", status);
}

static bool parent_status_needs_support(const PtcUiModel *model)
{
    int64_t age = ptc_ui_status_age_seconds(model, (int64_t)time(NULL));
    return strcmp(model->setup_phase, "protection") == 0 ||
        strcmp(model->setup_phase, "failed") == 0 || model->recovery_active ||
        model->disable_flag_present ||
        (model->temporary_unlocked_available && model->temporary_unlocked) ||
        model->error_code != 0 || age > 120;
}

static void activate_parent_status(UiState *ui)
{
    if (parent_status_needs_support(&ui->model)) {
        if (ui->model.parent_page != PTC_UI_PARENT_SUPPORT) {
            request_parent_navigation(ui, PTC_UI_PARENT_SUPPORT, false);
            ui->model.parent_footer_focused = true;
        } else {
            ui->model.parent_footer_focused = false;
            ui->model.selected_index = 0;
        }
    } else {
        refresh_disable_flag(ui);
        submit_status(ui);
    }
}

static void enter_child_area(UiState *ui)
{
    if (!ui) {
        return;
    }
    ui->model.view = PTC_UI_CHILD;
    submit_status(ui);
}

static void submit_offline_code(UiState *ui, const char *code)
{
    PtcCompanionStatus status;
    PtcPendingRedemption pending;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    memset(&pending, 0, sizeof(pending));
    snprintf(pending.request_id, sizeof(pending.request_id), "%s", ui->active_request_id);
    pending.confirmed_at = (int64_t)time(NULL);
    pending.grant_minutes = ui->model.code_grant_minutes;
    pending.before_remaining_available = ui->model.code_before_remaining_available;
    pending.before_remaining_minutes = ui->model.code_before_remaining_minutes;
    pending.before_unlimited = ui->model.code_before_unlimited;
    pending.after_remaining_available = ui->model.code_preview_after_available;
    pending.after_remaining_minutes = ui->model.code_preview_after_minutes;
    pending.effective_add_minutes = ui->model.code_effective_add_minutes;
    pending.capped = ui->model.code_preview_capped;
    pending.converts_unlimited_to_limited = ui->model.code_preview_converts_unlimited;
    status = ptc_companion_pending_redemption_save(&ui->client, &pending);
    if (status != PTC_COMPANION_OK) {
        ui->waiting = false;
        ui->model.pending_code[0] = '\0';
        set_message(ui, "无法保存兑换恢复信息；加时码未提交，仍可使用", status);
        return;
    }
    status = ptc_companion_transport_submit_offline_code(&ui->transport, ui->active_request_id, time(NULL), code);
    set_command_name(ui, "offline_code");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        pending.submitted = true;
        (void)ptc_companion_pending_redemption_save(&ui->client, &pending);
        ui->pending_redemption = pending;
        begin_wait(ui, "offline_code", "加时码已提交，正在等待后台确认…");
        return;
    }
    (void)ptc_companion_pending_redemption_clear(&ui->client);
    ui->model.pending_code[0] = '\0';
    ui->waiting = false;
    set_message(ui, "加时码提交失败；该码未消费，仍可使用", status);
}

static void apply_pending_redemption_preview(UiState *ui, const PtcPendingRedemption *pending)
{
    ui->model.code_grant_minutes = pending->grant_minutes;
    ui->model.code_before_remaining_available = pending->before_remaining_available;
    ui->model.code_before_remaining_minutes = pending->before_remaining_minutes;
    ui->model.code_before_unlimited = pending->before_unlimited;
    ui->model.code_preview_after_available = pending->after_remaining_available;
    ui->model.code_preview_after_minutes = pending->after_remaining_minutes;
    ui->model.code_effective_add_minutes = pending->effective_add_minutes;
    ui->model.code_preview_capped = pending->capped;
    ui->model.code_preview_converts_unlimited = pending->converts_unlimited_to_limited;
}

static void show_pending_redemption(UiState *ui)
{
    apply_pending_redemption_preview(ui, &ui->pending_redemption);
    ui->model.view = PTC_UI_CHILD;
    ui->model.overlay = PTC_UI_OVERLAY_CODE_RESULT;
    ui->model.code_result_pending = true;
    ui->model.code_result_failed = false;
    snprintf(ui->model.message, sizeof(ui->model.message),
             "已恢复上次确认的加时请求，结果确认中；请勿重复输入这枚加时码。");
    snprintf(ui->model.result_status, sizeof(ui->model.result_status), "pending");
    snprintf(ui->active_request_id, sizeof(ui->active_request_id), "%s", ui->pending_redemption.request_id);
    set_command_name(ui, "offline_code");
}

static void poll_pending_redemption(UiState *ui)
{
    PtcCompanionStatus status;
    if (!ui || !ui->recovering_redemption) return;
    status = ptc_companion_read_result(
        &ui->client, ui->pending_redemption.request_id, 0, -1,
        ui->last_result, sizeof(ui->last_result));
    if (status != PTC_COMPANION_OK) {
        if (status == PTC_COMPANION_RESULT_INVALID || status == PTC_COMPANION_RESULT_MISMATCH) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "兑换结果正在确认，已读取到的结果尚不能安全核对；请勿重复输入这枚加时码。");
        }
        return;
    }
    if (!ptc_ui_apply_result_json(&ui->model, ui->last_result) ||
        strcmp(ui->model.result_type, "offline_code") != 0) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "兑换结果正在确认，后台返回内容尚不能安全核对；请勿重复输入这枚加时码。");
        return;
    }
    ui->recovering_redemption = false;
    apply_pending_redemption_preview(ui, &ui->pending_redemption);
    ui->model.view = PTC_UI_CHILD;
    ui->model.overlay = PTC_UI_OVERLAY_CODE_RESULT;
    ui->model.code_result_pending = false;
    ui->model.code_result_failed = strcmp(ui->model.result_status, "ok") != 0;
    if (ui->model.code_result_failed) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "后台已确认兑换未成功，加时码没有被消费，可以重新输入。");
    } else {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "已恢复并确认上次兑换成功；这枚加时码已经使用，不能再次使用。");
    }
}

static bool restore_pending_redemption(UiState *ui)
{
    PtcCompanionStatus status;
    bool found = false;
    status = ptc_companion_pending_redemption_load(&ui->client, &ui->pending_redemption, &found);
    if (status != PTC_COMPANION_OK) {
        ui->model.view = PTC_UI_ERROR;
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "上次加时的恢复信息无法读取。为避免重复兑换，请暂勿再次输入该码。");
        return found;
    }
    if (!found) return false;
    if (!ptc_companion_pending_redemption_has_submission(&ui->client, &ui->pending_redemption)) {
        (void)ptc_companion_pending_redemption_clear(&ui->client);
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "上次确认在提交前中断，加时码未消费；请重新输入。");
        return true;
    }
    ui->recovering_redemption = true;
    show_pending_redemption(ui);
    poll_pending_redemption(ui);
    return true;
}

static void submit_preview_offline_code(UiState *ui, const char *code)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_preview_offline_code(
        &ui->transport, ui->active_request_id, time(NULL), code);
    set_command_name(ui, "preview_offline_code");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "preview_offline_code", "正在验证加时码并计算生效预览…");
        return;
    }
    ui->waiting = false;
    set_message(ui, "加时码预览失败", status);
}

static bool code_error_stays_in_input(int error_code)
{
    return error_code >= PTC_ERR_BAD_CODE && error_code <= PTC_ERR_CODE_COOLDOWN;
}

static void open_code_preview_confirm(UiState *ui, bool refreshed)
{
    const char *body = refreshed
        ? "实时状态发生了重要变化，已重新计算预览。\n确认后才会生效并消费这枚加时码。"
        : "请核对当前状态和兑换后的预计结果。\n确认前不会消费这枚加时码。";
    open_confirm_overlay(ui, PTC_UI_OPERATION_REDEEM_OFFLINE_CODE,
                         refreshed ? "状态已变化，请再次确认" : "确认兑换加时码", body);
    ui->model.confirm_hold_required = !ui->model.code_preview_after_available ||
        ui->model.code_preview_after_minutes == 0 ||
        ui->model.code_preview_converts_unlimited;
}

static void submit_minutes(UiState *ui, PtcUiOperation operation, uint16_t minutes)
{
    PtcCompanionStatus status;
    const char *type;
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用已开启，额度修改不可用。");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    if (operation == PTC_UI_OPERATION_SET_TODAY_LIMIT) {
        type = "set_today_limit";
        status = ptc_companion_transport_submit_set_today_limit(&ui->transport, ui->active_request_id, time(NULL), minutes);
    } else if (operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        type = "add_today_minutes";
        status = ptc_companion_transport_submit_add_today_minutes(&ui->transport, ui->active_request_id, time(NULL), minutes);
    } else {
        ui->waiting = false;
        snprintf(ui->model.message, sizeof(ui->model.message), "不支持的额度操作。");
        return;
    }
    set_command_name(ui, type);
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, type, "设置已提交，正在等待后台确认…");
    } else {
        ui->waiting = false;
        set_message(ui, "设置提交失败", status);
    }
}

static void submit_weekly(UiState *ui)
{
    PtcCompanionStatus status;
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用已开启，周计划暂不能保存。");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_weekly_template(
        &ui->transport,
        ui->active_request_id,
        time(NULL),
        ui->model.draft_week);
    set_command_name(ui, "set_weekly_template");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "set_weekly_template", "每周计划已提交，正在等待后台确认…");
    } else {
        ui->waiting = false;
        set_message(ui, "每周计划提交失败", status);
    }
}

static void submit_holiday_policy(UiState *ui)
{
    PtcCompanionStatus status;
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，国家节假日设置暂时只读。");
        return;
    }
    if (!ui->model.holiday_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), "国家节假日设置没有修改。");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_holiday_policy(&ui->transport, ui->active_request_id, time(NULL),
        ui->model.draft_holiday_enabled, ui->model.draft_holiday_rule, ui->model.draft_makeup_workday_rule);
    set_command_name(ui, "set_holiday_policy");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "set_holiday_policy", "国家节假日设置已提交，正在等待后台确认…");
    } else {
        set_message(ui, "国家节假日设置提交失败", status);
    }
}

static PtcRuleMode parse_rule_mode(const char *mode)
{
    if (mode && strcmp(mode, "unlimited") == 0) {
        return PTC_RULE_MODE_UNLIMITED;
    }
    return PTC_RULE_MODE_LIMIT;
}

static const char *rule_json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : NULL;
}

static int rule_json_int(const cJSON *object, const char *name, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static uint16_t clamp_rule_minutes(int value)
{
    if (value < 15) {
        return 15;
    }
    if (value > 1440) {
        return 1440;
    }
    return (uint16_t)value;
}

static void load_rule_drafts(UiState *ui)
{
    PtcRules rules;
    char text[RESULT_TEXT_SIZE];
    cJSON *root;
    const cJSON *week;
    const cJSON *version;
    const cJSON *override_present;
    const cJSON *override_mode;
    const cJSON *override_minutes;
    const cJSON *override_day;
    unsigned int index;
    ptc_rules_default(&rules);
    memcpy(ui->model.draft_week, rules.week, sizeof(rules.week));
    memcpy(ui->model.current_week, rules.week, sizeof(rules.week));
    ui->model.today_override_present = false;
    ui->model.holiday_enabled = rules.holiday_enabled;
    ui->model.draft_holiday_enabled = rules.holiday_enabled;
    ui->model.holiday_rule = rules.holiday_rule;
    ui->model.draft_holiday_rule = rules.holiday_rule;
    ui->model.makeup_workday_rule = rules.makeup_workday_rule;
    ui->model.draft_makeup_workday_rule = rules.makeup_workday_rule;
    if (!ui->client.storage->vtable->read_text(ui->client.storage, RULES_PATH, text, sizeof(text))) {
        return;
    }
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valueint != 1) {
        cJSON_Delete(root);
        return;
    }
    week = cJSON_GetObjectItemCaseSensitive(root, "week");
    if (cJSON_IsArray(week) && cJSON_GetArraySize(week) == 7) {
        for (index = 0; index < 7; ++index) {
            const cJSON *day = cJSON_GetArrayItem(week, (int)index);
            rules.week[index].mode = parse_rule_mode(rule_json_string(day, "mode"));
            rules.week[index].minutes = clamp_rule_minutes(rule_json_int(day, "minutes", rules.week[index].minutes));
        }
    }
    override_present = cJSON_GetObjectItemCaseSensitive(root, "today_override_present");
    override_day = cJSON_GetObjectItemCaseSensitive(root, "today_override_day_index");
    override_mode = cJSON_GetObjectItemCaseSensitive(root, "today_override_mode");
    override_minutes = cJSON_GetObjectItemCaseSensitive(root, "today_override_minutes");
    if (cJSON_IsTrue(override_present) && cJSON_IsNumber(override_day) &&
        override_day->valueint == (int)ui->model.day_index) {
        ui->model.today_override_present = true;
        ui->model.today_override_rule.mode = parse_rule_mode(cJSON_GetStringValue(override_mode));
        ui->model.today_override_rule.minutes = clamp_rule_minutes(
            cJSON_IsNumber(override_minutes) ? override_minutes->valueint : 60);
    }
    rules.holiday_enabled = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "holiday_enabled"));
    rules.holiday_rule.mode = parse_rule_mode(rule_json_string(root, "holiday_mode"));
    rules.holiday_rule.minutes = clamp_rule_minutes(rule_json_int(root, "holiday_minutes", rules.holiday_rule.minutes));
    rules.makeup_workday_rule.mode = parse_rule_mode(rule_json_string(root, "makeup_workday_mode"));
    rules.makeup_workday_rule.minutes = clamp_rule_minutes(
        rule_json_int(root, "makeup_workday_minutes", rules.makeup_workday_rule.minutes));
    memcpy(ui->model.draft_week, rules.week, sizeof(rules.week));
    memcpy(ui->model.current_week, rules.week, sizeof(rules.week));
    ui->model.weekly_dirty = false;
    ui->model.holiday_enabled = rules.holiday_enabled;
    ui->model.draft_holiday_enabled = rules.holiday_enabled;
    ui->model.holiday_rule = rules.holiday_rule;
    ui->model.draft_holiday_rule = rules.holiday_rule;
    ui->model.makeup_workday_rule = rules.makeup_workday_rule;
    ui->model.draft_makeup_workday_rule = rules.makeup_workday_rule;
    ui->model.holiday_dirty = false;
    cJSON_Delete(root);
}

static void sync_setup_wizard(UiState *ui)
{
    bool active;
    if (!ui) {
        return;
    }
    active = strcmp(ui->model.setup_phase, "active") == 0;
    if (!active) {
        if (strcmp(ui->model.setup_phase, "restored") == 0) {
            if (ui->model.setup_step != PTC_UI_SETUP_TAKEOVER) {
                (void)save_setup_step(ui, PTC_UI_SETUP_TAKEOVER);
            }
        } else if (ui->model.setup_step == 0) {
            (void)save_setup_step(ui, PTC_UI_SETUP_SHORTCUT);
        }
        ui->model.view = PTC_UI_SETUP;
    } else if (ui->model.setup_step > 0) {
        ui->model.view = PTC_UI_SETUP;
    } else if (ui->model.view == PTC_UI_SETUP) {
        ui->model.view = PTC_UI_CHILD;
    }
}

static void poll_result(UiState *ui, bool force)
{
    PtcCompanionStatus status;
    PtcDayRule saved_draft[7];
    bool preserve_weekly_draft;
    bool preserve_holiday_draft;
    bool saved_holiday_enabled;
    PtcDayRule saved_holiday_rule;
    PtcDayRule saved_makeup_rule;
    if (!ui->waiting) {
        if (force) {
            submit_status(ui);
        }
        return;
    }
    if (ui->active_request_id[0] == '\0') {
        if (force) {
            submit_status(ui);
        }
        return;
    }
    if (ui->waiting) {
        ui->elapsed_ms += LOOP_SLEEP_MS;
    }
    status = ptc_companion_transport_poll(
        &ui->transport,
        LOOP_SLEEP_MS,
        REQUEST_TIMEOUT_MS,
        ui->last_result,
        sizeof(ui->last_result));
    sync_transport_label(ui);
    if (status == PTC_COMPANION_PENDING) {
        snprintf(ui->model.message, sizeof(ui->model.message), "后台正在处理，请稍候…");
        return;
    }
    ui->waiting = false;
    if (status == PTC_COMPANION_OK) {
        preserve_weekly_draft = ui->model.weekly_dirty;
        if (preserve_weekly_draft) {
            memcpy(saved_draft, ui->model.draft_week, sizeof(saved_draft));
        }
        preserve_holiday_draft = ui->model.holiday_dirty;
        saved_holiday_enabled = ui->model.draft_holiday_enabled;
        saved_holiday_rule = ui->model.draft_holiday_rule;
        saved_makeup_rule = ui->model.draft_makeup_workday_rule;
        if (!ptc_ui_apply_result_json(&ui->model, ui->last_result)) {
            ui->pending_parent_page = -1;
            ui->pending_leave_parent = false;
            set_message(ui, "读取结果失败", PTC_COMPANION_RESULT_INVALID);
            if (ui->request_view == PTC_UI_CHILD) ui->model.view = PTC_UI_ERROR;
            return;
        }
        if (strcmp(ui->model.result_type, "status") == 0 &&
            ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
            ui->model.grant_status_refresh_failed = strcmp(ui->model.result_status, "ok") != 0;
        }
        if (strcmp(ui->model.result_type, "complete_setup") == 0 &&
            strcmp(ui->model.result_status, "ok") == 0) {
            ui->model.setup_zone_index = 1;
            ui->model.setup_album_enable = false;
            (void)save_setup_step(ui, PTC_UI_SETUP_ALBUM);
        }
        sync_setup_wizard(ui);
        load_rule_drafts(ui);
        if (ui->model.status_loaded) {
            ptc_ui_mark_status_updated(&ui->model, (int64_t)time(NULL));
        }
        if (preserve_weekly_draft &&
            !(strcmp(ui->model.result_type, "set_weekly_template") == 0 &&
              strcmp(ui->model.result_status, "ok") == 0)) {
            memcpy(ui->model.draft_week, saved_draft, sizeof(saved_draft));
            update_weekly_dirty(ui);
        }
        if (preserve_holiday_draft &&
            !(strcmp(ui->model.result_type, "set_holiday_policy") == 0 &&
              strcmp(ui->model.result_status, "ok") == 0)) {
            ui->model.draft_holiday_enabled = saved_holiday_enabled;
            ui->model.draft_holiday_rule = saved_holiday_rule;
            ui->model.draft_makeup_workday_rule = saved_makeup_rule;
            update_holiday_dirty(ui);
        }
        refresh_disable_flag(ui);
        if (strcmp(ui->model.result_type, "preview_offline_code") == 0) {
            if (strcmp(ui->model.result_status, "ok") == 0) {
                bool after_zero = ui->model.code_preview_after_available &&
                    ui->model.code_preview_after_minutes == 0;
                bool material_change = ui->code_preview_recheck &&
                    (ui->code_previous_after_available != ui->model.code_preview_after_available ||
                     ui->code_previous_after_zero != after_zero ||
                     ui->code_previous_capped != ui->model.code_preview_capped ||
                     ui->code_previous_converts_unlimited != ui->model.code_preview_converts_unlimited);
                if (ui->code_preview_recheck && !material_change) {
                    ui->code_preview_recheck = false;
                    ui->model.code_before_remaining_available = ui->model.remaining_available;
                    ui->model.code_before_remaining_minutes = ui->model.remaining_minutes;
                    ui->model.code_before_unlimited = ui->model.unrestricted_today == 1;
                    submit_offline_code(ui, ui->model.pending_code);
                } else {
                    ui->code_preview_recheck = false;
                    open_code_preview_confirm(ui, material_change);
                }
            } else {
                ui->code_preview_recheck = false;
                ui->model.pending_code[0] = '\0';
                if (code_error_stays_in_input(ui->model.error_code)) {
                    char error[96];
                    snprintf(error, sizeof(error), "%.95s", ui->model.message);
                    open_offline_code_input(ui);
                    snprintf(ui->model.numpad_error, sizeof(ui->model.numpad_error), "%s", error);
                } else {
                    ui->model.view = PTC_UI_ERROR;
                }
            }
        } else if (strcmp(ui->model.result_type, "offline_code") == 0) {
            ui->model.pending_code[0] = '\0';
            ui->model.code_result_pending = false;
            ui->model.code_result_failed = strcmp(ui->model.result_status, "ok") != 0;
            if (strcmp(ui->model.result_status, "ok") == 0) {
                ui->model.overlay = PTC_UI_OVERLAY_CODE_RESULT;
                ui->model.operation = PTC_UI_OPERATION_NONE;
            } else if (code_error_stays_in_input(ui->model.error_code)) {
                char error[96];
                snprintf(error, sizeof(error), "%.95s", ui->model.message);
                open_offline_code_input(ui);
                snprintf(ui->model.numpad_error, sizeof(ui->model.numpad_error), "%s", error);
                (void)ptc_companion_pending_redemption_clear(&ui->client);
            } else {
                char original[192];
                snprintf(original, sizeof(original), "%s", ui->model.message);
                snprintf(ui->model.message, sizeof(ui->model.message),
                         "兑换未成功，加时码仍可使用。");
                snprintf(ui->model.feedback_detail, sizeof(ui->model.feedback_detail), "%s", original);
                ui->model.view = PTC_UI_ERROR;
                (void)ptc_companion_pending_redemption_clear(&ui->client);
            }
        }
        if (ui->pending_today_action >= 0 && strcmp(ui->model.result_type, "status") == 0) {
            int action = ui->pending_today_action;
            ui->pending_today_action = -1;
            if (strcmp(ui->model.result_status, "ok") == 0) {
                handle_today_action_ready(ui, action);
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message),
                         "无法刷新当前状态，已取消本次时间调整。请重试。");
            }
        }
        if (ui->request_view == PTC_UI_CHILD && strcmp(ui->model.result_status, "error") == 0 &&
            strcmp(ui->model.result_type, "preview_offline_code") != 0 &&
            strcmp(ui->model.result_type, "offline_code") != 0) {
            ui->model.view = PTC_UI_ERROR;
        }
        if (ui->pending_parent_page >= 0 || ui->pending_leave_parent) {
            if (strcmp(ui->model.result_type, "set_weekly_template") == 0 &&
                strcmp(ui->model.result_status, "ok") == 0) {
                apply_pending_navigation(ui);
            } else if (strcmp(ui->model.result_type, "set_weekly_template") == 0) {
                ui->pending_parent_page = -1;
                ui->pending_leave_parent = false;
                snprintf(ui->model.message, sizeof(ui->model.message),
                         "周计划保存未完成，修改仍保留，请重试。");
            }
        }
        return;
    }
    if (ui->pending_parent_page >= 0 || ui->pending_leave_parent) {
        ui->pending_parent_page = -1;
        ui->pending_leave_parent = false;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
        ui->model.grant_status_refresh_failed = true;
    }
    if (ui->pending_redemption.request_id[0] != '\0' &&
        strcmp(ui->pending_redemption.request_id, ui->active_request_id) == 0) {
        ptc_companion_transport_cancel(&ui->transport);
        ui->recovering_redemption = true;
        show_pending_redemption(ui);
        return;
    }
    set_message(ui, "读取结果失败", status);
    if (ui->request_view == PTC_UI_CHILD) ui->model.view = PTC_UI_ERROR;
}

static void refresh_setup_activation(UiState *ui)
{
    int64_t now;
    if (!ui || ui->waiting || ui->model.view != PTC_UI_SETUP) {
        return;
    }
    now = (int64_t)time(NULL);
    if (ptc_ui_setup_grace_remaining(&ui->model, now) != 0 ||
        ui->last_setup_refresh_second == now) {
        return;
    }
    ui->last_setup_refresh_second = now;
    submit_status(ui);
}

static void enter_parent_area_unlocked(UiState *ui)
{
    if (!ui) {
        return;
    }
    refresh_disable_flag(ui);
    refresh_security_state(ui);
    ui->model.view = PTC_UI_PARENT;
    ui->model.parent_page = ui->model.setup_phase[0] && strcmp(ui->model.setup_phase, "active") != 0
        ? PTC_UI_PARENT_SUPPORT : PTC_UI_PARENT_TODAY;
    ui->model.selected_index = 0;
    snprintf(ui->model.message, sizeof(ui->model.message), "家长区已解锁。进入孩子区请按 B。");
    if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        submit_status(ui);
    }
}

static void enter_parent_area(UiState *ui)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    char pin_confirm[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus state = ptc_companion_auth_state(&ui->auth);
    ui->auth_retry_action = AUTH_RETRY_ENTER_PARENT;
    if (state == PTC_AUTH_EMPTY) {
        if (!keyboard_input("设置 任我玩 PIN", "请输入 1–64 位数字；长度由家长决定", pin, sizeof(pin), true, true, false) ||
            !keyboard_input("确认 任我玩 PIN", "请再次输入相同的数字 PIN", pin_confirm, sizeof(pin_confirm), true, true, false)) {
            ui->auth_retry_action = AUTH_RETRY_NONE;
            snprintf(ui->model.message, sizeof(ui->model.message), "已取消 PIN 设置。");
            return;
        }
        if (strcmp(pin, pin_confirm) != 0) {
            show_auth_error(ui, "两次 PIN 不一致", "两次输入的 PIN 不一致，已全部清空，请重新设置。", 0);
            return;
        }
        state = ptc_companion_auth_set_pin(&ui->auth, pin, time(NULL), switch_random, NULL);
        if (state != PTC_AUTH_OK) {
            show_auth_error(ui, "PIN 设置失败", auth_status_zh(state), 0);
            return;
        }
    } else if (state != PTC_AUTH_OK) {
        show_auth_error(ui, "无法进入家长区", auth_status_zh(state), 0);
        return;
    }
    if (!keyboard_input("任我玩 PIN", "输入本应用独立管理 PIN", pin, sizeof(pin), true, true, false)) {
        ui->auth_retry_action = AUTH_RETRY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消进入家长区。");
        return;
    }
    {
        int64_t retry_after = 0;
        state = ptc_companion_auth_verify_pin(&ui->auth, pin, (int64_t)time(NULL), &retry_after);
        if (state == PTC_AUTH_COOLDOWN && retry_after > 0) {
            show_auth_error(ui, "PIN 暂时锁定", "PIN 错误次数过多，请等待倒计时结束后重试。", retry_after);
            return;
        }
    }
    if (state != PTC_AUTH_OK) {
        show_auth_error(ui, "PIN 验证未通过",
                        state == PTC_AUTH_DENIED ? "PIN 不正确，请重试。" : auth_status_zh(state), 0);
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_NONE;
    enter_parent_area_unlocked(ui);
    if (strlen(pin) < 4U) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "家长区已解锁；当前 PIN 少于 4 位，很容易被猜到，建议尽快修改。");
    }
}

static void select_setup_shortcut(UiState *ui, int index)
{
    if (!ui || index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        return;
    }
    ui->model.setup_shortcut_index = index;
    ui->model.shortcut_draft_mask = shortcut_preset_mask(index);
    ui->model.shortcut_draft_enabled = true;
    refresh_shortcut_draft_label(ui);
    snprintf(ui->model.message, sizeof(ui->model.message), "待确认组合：%s。按 + 确认后才会生效。",
             ui->model.shortcut_draft_label);
}

static void begin_setup_shortcut_capture(UiState *ui)
{
    if (!ui) {
        return;
    }
    ui->model.shortcut_capture_active = true;
    ui->model.captured_shortcut_mask = 0;
    snprintf(ui->model.message, sizeof(ui->model.message),
             "请同时按住要绑定的按键；A 录入草稿，B 取消。Minus - 始终保留。");
}

static void update_setup_shortcut_capture(UiState *ui, u64 down, u64 held)
{
    u64 captured;
    if (!ui || !ui->model.shortcut_capture_active) {
        return;
    }
    captured = held & (u64)CUSTOM_SHORTCUT_CAPTURE_MASK;
    if (captured != 0) {
        ui->model.captured_shortcut_mask = captured;
    }
    if (down & HidNpadButton_B) {
        ui->model.shortcut_capture_active = false;
        ui->model.captured_shortcut_mask = 0;
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消手动录入。");
        return;
    }
    if (down & HidNpadButton_A) {
        if (!shortcut_mask_valid(ui->model.captured_shortcut_mask)) {
            snprintf(ui->model.message, sizeof(ui->model.message), "请先按住至少一个可绑定的按键。");
            return;
        }
        ui->model.shortcut_draft_mask = ui->model.captured_shortcut_mask;
        ui->model.shortcut_draft_enabled = true;
        ui->model.shortcut_capture_active = false;
        refresh_shortcut_draft_label(ui);
        snprintf(ui->model.message, sizeof(ui->model.message), "已录入待确认组合：%s。请按 + 保存。",
                 ui->model.shortcut_draft_label);
    }
}

static bool commit_shortcut_preferences(UiState *ui)
{
    u64 old_mask;
    bool old_enabled;
    bool old_hint;
    if (!ui || !shortcut_mask_valid(ui->model.shortcut_draft_mask)) return false;
    old_mask = ui->model.custom_shortcut_mask;
    old_enabled = ui->model.custom_shortcut_enabled;
    old_hint = ui->model.show_parent_shortcut_hint;
    ui->model.custom_shortcut_mask = ui->model.shortcut_draft_mask;
    ui->model.custom_shortcut_enabled = ui->model.shortcut_draft_enabled;
    ui->model.show_parent_shortcut_hint = ui->model.shortcut_draft_show_hint;
    refresh_custom_shortcut_label(ui);
    if (save_ui_preferences(ui)) return true;
    ui->model.custom_shortcut_mask = old_mask;
    ui->model.custom_shortcut_enabled = old_enabled;
    ui->model.show_parent_shortcut_hint = old_hint;
    refresh_custom_shortcut_label(ui);
    return false;
}

static void open_shortcut_manager(UiState *ui)
{
    if (!ui) return;
    ui->model.shortcut_draft_mask = ui->model.custom_shortcut_mask;
    ui->model.shortcut_draft_enabled = ui->model.custom_shortcut_enabled;
    ui->model.shortcut_draft_show_hint = ui->model.show_parent_shortcut_hint;
    refresh_shortcut_draft_label(ui);
    ui->model.overlay = PTC_UI_OVERLAY_SHORTCUT_MANAGER;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "家长区快捷键管理");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "选择常见组合或录制其他组合；所有修改按 + 确认后才生效。");
}

static void setup_pin(UiState *ui)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    char pin_confirm[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus state;
    if (!ui) {
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_SETUP_PIN;
    state = ptc_companion_auth_state(&ui->auth);
    if (state == PTC_AUTH_OK) {
        if (save_setup_step(ui, PTC_UI_SETUP_TAKEOVER)) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "当前 任我玩 PIN 已存在，可以继续确认接管系统控制。");
        }
        return;
    }
    if (state != PTC_AUTH_EMPTY) {
        show_auth_error(ui, "无法设置 任我玩 PIN", auth_status_zh(state), 0);
        return;
    }
    if (!keyboard_input("设置 任我玩 PIN", "请输入 1–64 位数字；短 PIN 仅提示风险，不会阻止保存",
                        pin, sizeof(pin), true, true, false) ||
        !keyboard_input("确认 任我玩 PIN", "请再次输入相同的数字 PIN",
                        pin_confirm, sizeof(pin_confirm), true, true, false)) {
        ui->auth_retry_action = AUTH_RETRY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消 PIN 设置。");
        return;
    }
    if (strcmp(pin, pin_confirm) != 0) {
        show_auth_error(ui, "两次 PIN 不一致", "两次输入的 PIN 不一致，已全部清空，请重新设置。", 0);
        return;
    }
    state = ptc_companion_auth_set_pin(&ui->auth, pin, time(NULL), switch_random, NULL);
    if (state != PTC_AUTH_OK) {
        show_auth_error(ui, "PIN 设置失败", auth_status_zh(state), 0);
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_NONE;
    if (save_setup_step(ui, PTC_UI_SETUP_TAKEOVER)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 strlen(pin) < 4U
                     ? "PIN 已保存；当前 PIN 少于 4 位，容易被猜到，但不会阻止继续设置。"
                     : "PIN 已保存；下一步确认接管系统控制。");
    }
}

static void finish_setup(UiState *ui)
{
    int zone;
    if (!ui || strcmp(ui->model.setup_phase, "active") != 0) {
        return;
    }
    zone = ui->model.setup_zone_index;
    if (!save_setup_step(ui, 0)) {
        return;
    }
    ui->model.setup_zone_index = zone;
    if (zone == 1) {
        enter_parent_area_unlocked(ui);
    } else {
        enter_child_area(ui);
    }
}

static void setup_previous(UiState *ui)
{
    if (!ui) {
        return;
    }
    if (ui->model.setup_step <= PTC_UI_SETUP_SHORTCUT) {
        ui->exit_requested = true;
        return;
    }
    (void)save_setup_step(ui, ui->model.setup_step - 1);
}

static void setup_primary(UiState *ui)
{
    if (!ui || ui->model.shortcut_capture_active) {
        return;
    }
    switch (ui->model.setup_step) {
    case PTC_UI_SETUP_SHORTCUT:
        if (!commit_shortcut_preferences(ui)) {
            snprintf(ui->model.message, sizeof(ui->model.message), "快捷键设置未保存，请确认 SD 卡可写。");
            break;
        }
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 ui->model.custom_shortcut_enabled
                    ? "快捷键已确认启用；固定 Minus - 仍然有效。"
                    : "未启用自定义组合；当前只保留固定 Minus -。");
        (void)save_setup_step(ui, PTC_UI_SETUP_PIN);
        break;
    case PTC_UI_SETUP_PIN:
        setup_pin(ui);
        break;
    case PTC_UI_SETUP_TAKEOVER:
        if (ptc_ui_setup_takeover_complete(&ui->model)) {
            ui->model.setup_album_enable = false;
            if (save_setup_step(ui, PTC_UI_SETUP_ALBUM)) {
                snprintf(ui->model.message, sizeof(ui->model.message),
                         "系统控制接管已完成，可选择是否限制相册入口。");
            }
        } else if (!ui->waiting) {
            if (ui->model.disable_flag_present && strcmp(ui->model.setup_phase, "restored") == 0) {
                open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "解除停用并重新接管",
                                     "将重新执行只读兼容预检；仅预检通过后才解除紧急停用并重新启用额度管理。");
            } else {
                open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "确认接管系统控制",
                                     "先执行只读兼容预检；通过后保存安装快照并启用额度管理。");
            }
        }
        break;
    case PTC_UI_SETUP_ALBUM:
        if (ui->model.setup_album_enable) {
            char error[160] = {0};
            if (!ptc_album_restriction_enable(ui->client.storage, error, sizeof(error))) {
                snprintf(ui->model.message, sizeof(ui->model.message), "%s；可关闭开关暂时跳过。", error);
                break;
            }
            snprintf(ui->model.message, sizeof(ui->model.message), "相册入口配置已保存，重启主机后生效。");
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "已暂时跳过相册入口限制，可稍后在加时码与安全中开启。");
        }
        ui->model.setup_zone_index = 1;
        (void)save_setup_step(ui, PTC_UI_SETUP_ZONE);
        break;
    case PTC_UI_SETUP_ZONE:
        finish_setup(ui);
        break;
    default:
        break;
    }
}

static void handle_setup_input(UiState *ui, u64 down, u64 held)
{
    if (!ui || ui->model.view != PTC_UI_SETUP || ui->waiting) {
        if (ui && ui->model.shortcut_capture_active) {
            update_setup_shortcut_capture(ui, down, held);
        }
        return;
    }
    if (ui->model.shortcut_capture_active) {
        update_setup_shortcut_capture(ui, down, held);
        return;
    }
    if (down & HidNpadButton_B) {
        setup_previous(ui);
        return;
    }
    if (ui->model.setup_step == PTC_UI_SETUP_SHORTCUT) {
        if (down & HidNpadButton_Up) {
            ui->model.setup_shortcut_index = ui->model.setup_shortcut_index <= 0
                ? PTC_UI_SHORTCUT_PRESET_COUNT - 1 : ui->model.setup_shortcut_index - 1;
        } else if (down & HidNpadButton_Down) {
            ui->model.setup_shortcut_index = (ui->model.setup_shortcut_index + 1) % PTC_UI_SHORTCUT_PRESET_COUNT;
        } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            ui->model.setup_shortcut_index = (ui->model.setup_shortcut_index + 7) % PTC_UI_SHORTCUT_PRESET_COUNT;
        } else if (down & HidNpadButton_A) {
            select_setup_shortcut(ui, ui->model.setup_shortcut_index);
        } else if (down & HidNpadButton_X) {
            begin_setup_shortcut_capture(ui);
        } else if (down & HidNpadButton_Plus) {
            setup_primary(ui);
        }
    } else if (ui->model.setup_step == PTC_UI_SETUP_ALBUM) {
        if (down & (HidNpadButton_Left | HidNpadButton_Right | HidNpadButton_X)) {
            ui->model.setup_album_enable = !ui->model.setup_album_enable;
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            setup_primary(ui);
        }
    } else if (ui->model.setup_step == PTC_UI_SETUP_ZONE) {
        if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            ui->model.setup_zone_index = ui->model.setup_zone_index == 0 ? 1 : 0;
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            setup_primary(ui);
        }
    } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
        setup_primary(ui);
    }
}

static void open_minutes_overlay(
    UiState *ui,
    PtcUiOperation operation,
    const char *title,
    const char *body,
    uint16_t value,
    uint16_t minimum,
    uint16_t maximum)
{
    ui->model.overlay = PTC_UI_OVERLAY_MINUTES;
    ui->model.operation = operation;
    ui->model.draft_minutes = value;
    ui->model.minimum_minutes = minimum;
    ui->model.maximum_minutes = maximum;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s", title);
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s", body);
}

static void open_confirm_overlay(UiState *ui, PtcUiOperation operation, const char *title, const char *body)
{
    ui->model.confirm_return_overlay = ui->model.overlay;
    snprintf(ui->model.confirm_return_title, sizeof(ui->model.confirm_return_title), "%s", ui->model.overlay_title);
    snprintf(ui->model.confirm_return_body, sizeof(ui->model.confirm_return_body), "%s", ui->model.overlay_body);
    ui->model.overlay = PTC_UI_OVERLAY_CONFIRM;
    ui->model.operation = operation;
    ui->model.confirm_hold_required = false;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s", title);
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s", body);
}

static void open_weekly_page(UiState *ui)
{
    PtcDayRule saved_draft[7];
    int saved_editor = ui->model.editor_index;
    bool preserve_draft = ui->model.weekly_dirty;
    if (preserve_draft) {
        memcpy(saved_draft, ui->model.draft_week, sizeof(saved_draft));
    }
    load_rule_drafts(ui);
    if (preserve_draft) {
        memcpy(ui->model.draft_week, saved_draft, sizeof(saved_draft));
        update_weekly_dirty(ui);
        ui->model.editor_index = saved_editor;
    } else {
        ui->model.editor_index = ptc_weekday_from_day_index(ui->model.day_index);
    }
    ui->model.parent_page = PTC_UI_PARENT_PLAN;
    ui->model.selected_index = 0;
    submit_status(ui);
    snprintf(ui->model.message, sizeof(ui->model.message), "正在刷新周计划；选择日期后按 A 或点按卡片编辑。");
}

static bool read_pairing_values(UiState *ui, char *device_id, size_t device_size, char *secret, size_t secret_size)
{
    char config_text[4096];
    char credentials_text[512];
    cJSON *config;
    cJSON *credentials;
    const char *device;
    const char *grant_secret;
    if (!ui->client.storage->vtable->read_text(ui->client.storage, CONFIG_PATH, config_text, sizeof(config_text)) ||
        !ui->client.storage->vtable->read_text(ui->client.storage, CREDENTIALS_PATH, credentials_text, sizeof(credentials_text))) {
        return false;
    }
    config = cJSON_Parse(config_text);
    credentials = cJSON_Parse(credentials_text);
    device = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(config, "device_id"));
    grant_secret = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(credentials, "grant_secret"));
    if (!device || !grant_secret) {
        cJSON_Delete(config);
        cJSON_Delete(credentials);
        return false;
    }
    if (device_id && device_size) snprintf(device_id, device_size, "%s", device);
    if (secret && secret_size) snprintf(secret, secret_size, "%s", grant_secret);
    cJSON_Delete(config);
    cJSON_Delete(credentials);
    return true;
}

static bool read_pairing_config(UiState *ui, char *base_url, size_t base_size, uint16_t *max_add_minutes)
{
    char text[4096];
    cJSON *root;
    const cJSON *base;
    const cJSON *maximum;
    if (!ui || !base_url || base_size == 0U || !max_add_minutes ||
        !ui->client.storage->vtable->read_text(ui->client.storage, CONFIG_PATH, text, sizeof(text))) return false;
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    base = cJSON_GetObjectItemCaseSensitive(root, "pairing_base_url");
    maximum = cJSON_GetObjectItemCaseSensitive(root, "max_add_minutes");
    snprintf(base_url, base_size, "%s",
             cJSON_IsString(base) && base->valuestring && ptc_pairing_base_url_valid(base->valuestring)
                 ? base->valuestring : PTC_PAIRING_BASE_URL);
    *max_add_minutes = cJSON_IsNumber(maximum) && maximum->valueint >= 1
        ? (uint16_t)maximum->valueint : PTC_TOKEN_V2_MAX_MINUTES;
    if (*max_add_minutes > PTC_TOKEN_V2_MAX_MINUTES) *max_add_minutes = PTC_TOKEN_V2_MAX_MINUTES;
    cJSON_Delete(root);
    return true;
}

static bool save_pairing_base_url(UiState *ui, const char *base_url)
{
    char text[4096];
    cJSON *root;
    char *rendered;
    bool ok;
    if (!ptc_pairing_base_url_valid(base_url) ||
        !ui->client.storage->vtable->read_text(ui->client.storage, CONFIG_PATH, text, sizeof(text))) return false;
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    cJSON_DeleteItemFromObject(root, "pairing_base_url");
    cJSON_AddStringToObject(root, "pairing_base_url", base_url);
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = rendered && ui->client.storage->vtable->write_text_atomic(ui->client.storage, CONFIG_PATH, rendered);
    free(rendered);
    return ok;
}

static uint16_t legal_grant_minutes(uint16_t requested, uint16_t maximum)
{
    uint8_t tier;
    if (maximum > PTC_TOKEN_V2_MAX_MINUTES) maximum = PTC_TOKEN_V2_MAX_MINUTES;
    if (requested > maximum) requested = maximum;
    while (requested > 1U && ptc_token_v2_tier_for_minutes(requested, &tier) != PTC_ERR_OK) --requested;
    return requested > 0U ? requested : 1U;
}

static void adjust_grant_minutes(UiState *ui, int direction)
{
    int next = ui->model.grant_minutes;
    do {
        next += direction > 0 ? 1 : -1;
        if (next < 1) next = ui->model.grant_max_minutes;
        if (next > ui->model.grant_max_minutes) next = 1;
    } while (next != ui->model.grant_minutes &&
             ptc_token_v2_tier_for_minutes((uint16_t)next, &(uint8_t){0}) != PTC_ERR_OK);
    ui->model.grant_minutes = (uint16_t)next;
}

static void adjust_grant_minutes_delta(UiState *ui, int delta)
{
    int target;
    int best;
    int best_distance = 10000;
    int candidate;
    uint8_t tier;
    if (!ui || delta == 0) return;
    target = (int)ui->model.grant_minutes + delta;
    if (target < 1) target = 1;
    if (target > ui->model.grant_max_minutes) target = ui->model.grant_max_minutes;
    best = ui->model.grant_minutes;
    for (candidate = 1; candidate <= ui->model.grant_max_minutes; ++candidate) {
        int distance;
        if (ptc_token_v2_tier_for_minutes((uint16_t)candidate, &tier) != PTC_ERR_OK) continue;
        distance = abs(candidate - target);
        if (distance < best_distance ||
            (distance == best_distance && ((delta > 0 && candidate > best) || (delta < 0 && candidate < best)))) {
            best = candidate;
            best_distance = distance;
        }
    }
    ui->model.grant_minutes = (uint16_t)best;
}

static void load_consumed_nonces(uint16_t day_index, bool used[PTC_TOKEN_V2_MAX_NONCE + 1U])
{
    FILE *file = fopen(LEDGER_PATH, "r");
    char line[256];
    if (!file) return;
    while (fgets(line, sizeof(line), file)) {
        unsigned int day;
        unsigned int nonce;
        if (sscanf(line, "{\"day_index\":%u,\"nonce\":%u,\"token_version\":2", &day, &nonce) == 2 &&
            day == day_index && nonce <= PTC_TOKEN_V2_MAX_NONCE) used[nonce] = true;
    }
    fclose(file);
}

static bool load_issued_nonces(UiState *ui, uint16_t day_index, bool issued[PTC_TOKEN_V2_MAX_NONCE + 1U])
{
    char text[8192];
    cJSON *root;
    const cJSON *stored_day;
    const cJSON *nonces;
    const cJSON *item;
    if (!ui->client.storage->vtable->exists(ui->client.storage, ISSUED_NONCES_PATH)) return true;
    if (!ui->client.storage->vtable->read_text(ui->client.storage, ISSUED_NONCES_PATH, text, sizeof(text))) return false;
    root = cJSON_Parse(text);
    stored_day = cJSON_GetObjectItemCaseSensitive(root, "day_index");
    nonces = cJSON_GetObjectItemCaseSensitive(root, "nonces");
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(stored_day) || !cJSON_IsArray(nonces)) {
        cJSON_Delete(root);
        return false;
    }
    if (stored_day->valueint == day_index) {
        cJSON_ArrayForEach(item, nonces) {
            if (cJSON_IsNumber(item) && item->valueint >= 0 &&
                (unsigned int)item->valueint <= PTC_TOKEN_V2_MAX_NONCE) {
                issued[item->valueint] = true;
            }
        }
    }
    cJSON_Delete(root);
    return true;
}

static bool save_issued_nonces(UiState *ui, uint16_t day_index, const bool issued[PTC_TOKEN_V2_MAX_NONCE + 1U])
{
    cJSON *root = cJSON_CreateObject();
    cJSON *nonces = cJSON_AddArrayToObject(root, "nonces");
    char *rendered;
    bool ok;
    unsigned int nonce;
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "day_index", day_index);
    for (nonce = 0; nonce <= PTC_TOKEN_V2_MAX_NONCE; ++nonce) {
        if (issued[nonce]) cJSON_AddItemToArray(nonces, cJSON_CreateNumber(nonce));
    }
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = rendered && ui->client.storage->vtable->write_text_atomic(ui->client.storage, ISSUED_NONCES_PATH, rendered);
    free(rendered);
    return ok;
}

static void refresh_security_state(UiState *ui)
{
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    ui->model.demo_secret_enabled = read_pairing_values(ui, NULL, 0, secret, sizeof(secret)) &&
        ptc_grant_secret_is_demo(secret);
}

static bool verify_sensitive_pin(UiState *ui, const char *action)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus status;
    int64_t retry_after = 0;
    if (!keyboard_input("验证 任我玩 管理 PIN", action, pin, sizeof(pin), true, true, false)) {
        ui->auth_retry_action = AUTH_RETRY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消敏感操作。");
        return false;
    }
    status = ptc_companion_auth_verify_pin(&ui->auth, pin, (int64_t)time(NULL), &retry_after);
    if (status != PTC_AUTH_OK) {
        if (status == PTC_AUTH_COOLDOWN && retry_after > 0) {
            show_auth_error(ui, "PIN 暂时锁定", "PIN 错误次数过多，请等待倒计时结束后重试。", retry_after);
            return false;
        }
        show_auth_error(ui, "PIN 验证未通过",
                        status == PTC_AUTH_DENIED ? "PIN 不正确，请重试。" : auth_status_zh(status), 0);
        return false;
    }
    ui->auth_retry_action = AUTH_RETRY_NONE;
    return true;
}

static bool commit_credential(UiState *ui)
{
    const char *path = ui->model.credential_kind == 1 ? CONFIG_PATH : CREDENTIALS_PATH;
    const char *field = ui->model.credential_kind == 1 ? "device_id" : "grant_secret";
    char text[4096];
    cJSON *root;
    char *rendered;
    bool ok;
    if (!ui->client.storage->vtable->read_text(ui->client.storage, path, text, sizeof(text))) return false;
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    cJSON_DeleteItemFromObject(root, field);
    cJSON_AddStringToObject(root, field, ui->model.credential_new);
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = rendered && ui->client.storage->vtable->write_text_atomic(ui->client.storage, path, rendered);
    free(rendered);
    if (!ok) return false;
    snprintf(ui->model.credential_current, sizeof(ui->model.credential_current), "%s", ui->model.credential_new);
    show_grant_manager(ui, ui->model.credential_kind == 1
        ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
    refresh_security_state(ui);
    if (ui->model.credential_kind == 1) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "设备名已更新；使用更新前设备名签发的加时码不可用，请重新生成配对二维码。");
    } else {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "加时码密钥已更新；当前网页配对和使用原密钥签发的加时码已失效，请重新配对。");
    }
    return true;
}

static void open_credential_manager(UiState *ui, int kind)
{
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret))) {
        snprintf(ui->model.message, sizeof(ui->model.message), "读取当前设备配对信息失败。");
        return;
    }
    ui->model.credential_kind = kind;
    ui->model.credential_revealed = false;
    ui->model.credential_new_revealed = kind == 1;
    snprintf(ui->model.credential_current, sizeof(ui->model.credential_current), "%s", kind == 1 ? device : secret);
    snprintf(ui->model.credential_new, sizeof(ui->model.credential_new), "%s", kind == 1 ? device : secret);
    ui->model.overlay = PTC_UI_OVERLAY_CREDENTIAL;
    ui->model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s",
             kind == 1 ? "管理加时码设备名" : "管理加时码密钥");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
             kind == 1 ? "当前值只读；可手工输入或随机生成新设备名。" :
                         "当前密钥默认遮挡；建议使用随机生成的 64 位十六进制密钥。");
}

static void edit_credential_input(UiState *ui)
{
    char value[80];
    const char *header = ui->model.credential_kind == 1 ? "输入新设备名" : "输入新加时码密钥";
    const char *guide = ui->model.credential_kind == 1
        ? "1–32 位：字母、数字、-、_"
        : "建议随机生成；手工输入 32–64 个非空白 ASCII 字符";
    if (!keyboard_input(header, guide, value, sizeof(value), ui->model.credential_kind == 2, false, false)) return;
    snprintf(ui->model.credential_new, sizeof(ui->model.credential_new), "%s", value);
}

static void randomize_credential(UiState *ui)
{
    uint8_t bytes[32];
    randomGet(bytes, sizeof(bytes));
    if (ui->model.credential_kind == 1) {
        (void)ptc_random_device_id(bytes, ui->model.credential_new, sizeof(ui->model.credential_new));
    } else {
        (void)ptc_hex_from_random(bytes, sizeof(bytes), ui->model.credential_new, sizeof(ui->model.credential_new));
        ui->model.credential_new_revealed = true;
    }
}

static void request_save_credential(UiState *ui)
{
    bool valid = ui->model.credential_kind == 1
        ? ptc_device_id_valid(ui->model.credential_new)
        : ptc_grant_secret_valid(ui->model.credential_new);
    if (!valid) {
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 ui->model.credential_kind == 1
                    ? "设备名必须为 1–32 位，只能包含字母、数字、- 和 _。"
                    : "密钥必须为 32–64 个非空白可打印 ASCII 字符；建议使用随机生成。");
        return;
    }
    if (strcmp(ui->model.credential_current, ui->model.credential_new) == 0) {
        snprintf(ui->model.message, sizeof(ui->model.message), "新值与当前值相同，无需保存。");
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_SAVE_CREDENTIAL;
    if (!verify_sensitive_pin(ui, "保存设备配对信息前，请再次输入本应用 PIN")) return;
    if (ui->model.credential_kind == 2 && ptc_grant_secret_is_demo(ui->model.credential_new)) {
        open_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_CREDENTIAL, "启用公共演示密钥",
            "任何知道设备名的人都能生成多个有效加时码，并可把当天额度累计到 1440 分钟。\n启用后家长页面会持续显示红色警告。");
        return;
    }
    if (!commit_credential(ui)) snprintf(ui->model.message, sizeof(ui->model.message), "保存配对信息失败。");
}

static void change_parent_pin(UiState *ui)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    char confirm[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus status;
    ui->auth_retry_action = AUTH_RETRY_CHANGE_PIN;
    if (!verify_sensitive_pin(ui, "修改 PIN 前，请先输入当前任我玩 PIN")) return;
    if (!keyboard_input("修改 PlayWise PIN", "请输入新的 1–64 位数字", pin, sizeof(pin), true, true, false) ||
        !keyboard_input("确认新 PIN", "请再次输入相同的数字 PIN", confirm, sizeof(confirm), true, true, false)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消 PIN 修改。");
        return;
    }
    if (strcmp(pin, confirm) != 0) {
        ui->auth_retry_action = AUTH_RETRY_CHANGE_PIN;
        show_auth_error(ui, "两次 PIN 不一致", "两次输入的新 PIN 不一致，已全部清空，请重新开始。", 0);
        return;
    }
    status = ptc_companion_auth_set_pin(&ui->auth, pin, time(NULL), switch_random, NULL);
    if (status == PTC_AUTH_OK) snprintf(ui->model.message, sizeof(ui->model.message), "%s",
        strlen(pin) < 4U ? "PlayWise PIN 已更新；当前 PIN 少于 4 位，冷却也无法提供可靠保护。" : "PlayWise PIN 已更新。");
    else {
        ui->auth_retry_action = AUTH_RETRY_CHANGE_PIN;
        show_auth_error(ui, "PIN 修改失败", auth_status_zh(status), 0);
    }
}

static void show_grant_manager(UiState *ui, int selection)
{
    ui->model.overlay = PTC_UI_OVERLAY_GRANT_MANAGER;
    ui->model.overlay_selection = selection >= 0 && selection < PTC_UI_GRANT_MANAGER_COUNT ? selection : 0;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "加时码生成管理");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "管理生成加时码所需的设备信息、导出配置和二维码网页地址。");
}

static void open_grant_manager(UiState *ui)
{
    ui->model.message[0] = '\0';
    show_grant_manager(ui, PTC_UI_GRANT_MANAGER_DEVICE);
}

static void open_local_grant(UiState *ui)
{
    uint16_t maximum = PTC_TOKEN_V2_MAX_MINUTES;
    if (!read_pairing_config(ui, ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), &maximum)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "读取本机生成器配置失败。");
        return;
    }
    ui->model.grant_max_minutes = maximum;
    ui->model.grant_minutes = legal_grant_minutes(20U, maximum);
    ui->model.grant_has_code = false;
    ui->model.grant_issued_minutes = 0;
    ui->model.grant_estimate_available = false;
    ui->model.grant_estimate_minutes = 0;
    ui->model.grant_estimate_capped = false;
    ui->model.grant_estimate_unrestricted = false;
    ui->model.grant_estimated_at = 0;
    ui->model.grant_status_refresh_failed = false;
    ui->model.grant_code[0] = '\0';
    ui->model.overlay = PTC_UI_OVERLAY_GRANT_LOCAL;
    ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
    ui->model.grant_status_refresh_failed = false;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "本机生成 8 位加时码");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "选择今天要增加的时间；快捷键会自动吸附到协议支持的合法档位。");
    submit_status(ui);
    if (!ui->waiting) ui->model.grant_status_refresh_failed = true;
}

static void edit_pairing_base_url(UiState *ui)
{
    char value[PTC_PAIRING_BASE_URL_MAX_LEN + 1];
    ui->auth_retry_action = AUTH_RETRY_EDIT_URL;
    if (!verify_sensitive_pin(ui, "修改二维码跳转地址前，请再次输入本应用 PIN")) return;
    if (!keyboard_input("二维码跳转地址", "填写官方或可信的家长网页；自定义页面可读取加时码密钥",
                        value, sizeof(value), false, false, false)) return;
    if (!ptc_pairing_base_url_valid(value)) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "地址无效：最长 256 字符，不得含账号、控制字符或 #；HTTP 仅限本机和私有网络。");
        return;
    }
    if (!save_pairing_base_url(ui, value)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "保存二维码跳转地址失败。");
        return;
    }
    snprintf(ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), "%s", value);
    snprintf(ui->model.message, sizeof(ui->model.message),
             "二维码跳转地址已更新；页面须支持二维码配对和导入配置文件，请仅使用可信的家长网页。");
}

static void apply_default_pairing_base_url(UiState *ui)
{
    if (!save_pairing_base_url(ui, PTC_PAIRING_BASE_URL)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "恢复官方二维码地址失败。");
    } else {
        snprintf(ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), "%s", PTC_PAIRING_BASE_URL);
        snprintf(ui->model.message, sizeof(ui->model.message), "已恢复二维码跳转默认地址。");
    }
    show_grant_manager(ui, PTC_UI_GRANT_MANAGER_RESET_URL);
}

static void request_reset_pairing_base_url(UiState *ui)
{
    char body[320];
    ui->auth_retry_action = AUTH_RETRY_RESET_URL;
    if (!verify_sensitive_pin(ui, "恢复二维码跳转默认地址前，请再次输入本应用 PIN")) return;
    snprintf(body, sizeof(body), "将当前二维码跳转地址恢复为默认地址：\n%s", PTC_PAIRING_BASE_URL);
    open_confirm_overlay(ui, PTC_UI_OPERATION_RESET_PAIRING_URL,
                         "恢复二维码跳转默认地址", body);
}

static void generate_local_grant_code(UiState *ui)
{
    bool consumed[PTC_TOKEN_V2_MAX_NONCE + 1U] = {false};
    bool issued[PTC_TOKEN_V2_MAX_NONCE + 1U] = {false};
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    char next_code[9];
    uint16_t start;
    uint16_t nonce = 0;
    uint8_t tier;
    bool found = false;
    if (!ui->model.status_loaded) {
        snprintf(ui->model.message, sizeof(ui->model.message), "无法确认设备日期，请先关闭弹层并刷新设备状态。");
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_GENERATE_CODE;
    if (!verify_sensitive_pin(ui, "本机生成加时码前，请再次输入本应用 PIN")) return;
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret)) ||
        ptc_token_v2_tier_for_minutes(ui->model.grant_minutes, &tier) != PTC_ERR_OK) {
        snprintf(ui->model.message, sizeof(ui->model.message), "生成参数或设备配对信息无效。");
        return;
    }
    load_consumed_nonces(ui->model.day_index, consumed);
    if (!load_issued_nonces(ui, ui->model.day_index, issued)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "读取本机已签发记录失败。");
        return;
    }
    randomGet(&start, sizeof(start));
    found = ptc_token_v2_find_available_nonce(consumed, issued, start, &nonce);
    if (!found) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "今日生成的加时码已达 512 枚上限，请明日再试；此前生成的代码不受影响。");
        return;
    }
    if (ptc_token_v2_encode(tier, nonce, device, secret, ui->model.day_index, next_code) != PTC_ERR_OK) {
        snprintf(ui->model.message, sizeof(ui->model.message), "本机生成加时码失败。");
        return;
    }
    issued[nonce] = true;
    if (!save_issued_nonces(ui, ui->model.day_index, issued)) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "保存已签发记录失败，本次未生成新码；此前显示的代码仍然有效。");
        return;
    }
    snprintf(ui->model.grant_code, sizeof(ui->model.grant_code), "%s", next_code);
    ui->model.grant_day_index = ui->model.day_index;
    ui->model.grant_has_code = true;
    ui->model.grant_issued_minutes = ui->model.grant_minutes;
    ui->model.grant_estimate_minutes = ptc_ui_grant_estimate_remaining(
        &ui->model, ui->model.grant_minutes, &ui->model.grant_estimate_capped);
    ui->model.grant_estimate_available = ui->model.grant_estimate_minutes >= 0;
    ui->model.grant_estimate_unrestricted = ui->model.unrestricted_today == 1;
    ui->model.grant_estimated_at = (int64_t)time(NULL);
    ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
    snprintf(ui->model.message, sizeof(ui->model.message), "已在本机生成今天有效的 %u 分钟加时码。",
             (unsigned int)ui->model.grant_minutes);
}

static void show_pairing_qr(UiState *ui)
{
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    uint16_t maximum;
    ui->auth_retry_action = AUTH_RETRY_SHOW_QR;
    if (!verify_sensitive_pin(ui, "显示包含加时码密钥的二维码前，请再次输入本应用 PIN")) return;
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret)) ||
        !read_pairing_config(ui, ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), &maximum) ||
        !ptc_build_pairing_url_with_base(ui->model.pairing_base_url, device, secret,
                                        ui->model.pairing_url, sizeof(ui->model.pairing_url)) ||
        !qrcodegen_encodeText(ui->model.pairing_url, temp, ui->model.qr_code,
            qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
            qrcodegen_Mask_AUTO, true)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "生成配对二维码失败。");
        return;
    }
    ui->model.overlay = PTC_UI_OVERLAY_QR;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "手机/电脑生成加时码");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "可扫描二维码自动配对，也可手动打开当前家长网页并导入配置文件。");
}

static void export_parent_import(UiState *ui)
{
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    cJSON *root;
    char *json;
    bool ok;
    ui->auth_retry_action = AUTH_RETRY_EXPORT_CONFIG;
    if (!verify_sensitive_pin(ui, "导出包含加时码密钥的配置前，请再次输入本应用 PIN")) return;
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret))) {
        snprintf(ui->model.message, sizeof(ui->model.message), "读取家长网页导入信息失败。");
        return;
    }
    root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "device_id", device);
    cJSON_AddStringToObject(root, "grant_secret", secret);
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = json && ui->client.storage->vtable->write_text_atomic(
        ui->client.storage, APP_ROOT "/parent-import.json", json);
    free(json);
    show_grant_manager(ui, PTC_UI_GRANT_MANAGER_EXPORT);
    snprintf(ui->model.message, sizeof(ui->model.message), "%s",
             ok ? "已导出到 sdmc:/switch/playwise/parent-import.json；把文件导入家长网页。文件包含密钥，请妥善保管。"
                : "生成家长网页导入文件失败。");
}

static void reveal_current_credential(UiState *ui)
{
    if (!ui) return;
    if (ui->model.credential_kind == 1 || ui->model.credential_revealed) {
        ui->model.credential_revealed = !ui->model.credential_revealed;
        ui->model.credential_new_revealed = ui->model.credential_revealed;
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_REVEAL_CREDENTIAL;
    if (!verify_sensitive_pin(ui, "显示当前加时码密钥前，请再次输入本应用 PIN")) return;
    ui->model.credential_revealed = true;
    ui->model.credential_new_revealed = true;
}

static void dispatch_auth_retry(UiState *ui, AuthRetryAction action)
{
    if (!ui) return;
    switch (action) {
    case AUTH_RETRY_ENTER_PARENT: enter_parent_area(ui); break;
    case AUTH_RETRY_SETUP_PIN: setup_pin(ui); break;
    case AUTH_RETRY_SAVE_CREDENTIAL: request_save_credential(ui); break;
    case AUTH_RETRY_CHANGE_PIN: change_parent_pin(ui); break;
    case AUTH_RETRY_EDIT_URL: edit_pairing_base_url(ui); break;
    case AUTH_RETRY_RESET_URL: request_reset_pairing_base_url(ui); break;
    case AUTH_RETRY_GENERATE_CODE: generate_local_grant_code(ui); break;
    case AUTH_RETRY_SHOW_QR: show_pairing_qr(ui); break;
    case AUTH_RETRY_EXPORT_CONFIG: export_parent_import(ui); break;
    case AUTH_RETRY_REVEAL_CREDENTIAL: reveal_current_credential(ui); break;
    case AUTH_RETRY_NONE:
    default:
        break;
    }
}

static void export_diagnostics(UiState *ui)
{
    cJSON *bundle = cJSON_CreateObject();
    cJSON *runtime;
    char path[192];
    char text[4096];
    char output_path[192];
    char *rendered;
    int64_t exported_at;
    size_t i;
    bool rejected_sensitive_file = false;
    ui->model.diagnostic_status = PTC_UI_DIAGNOSTIC_EXPORTING;
    ui->model.diagnostic_path[0] = '\0';
    snprintf(ui->model.message, sizeof(ui->model.message), "正在导出诊断包…");
    if (!bundle) {
        ui->model.diagnostic_status = PTC_UI_DIAGNOSTIC_ERROR;
        snprintf(ui->model.message, sizeof(ui->model.message), "生成诊断包失败。");
        return;
    }
    cJSON_AddNumberToObject(bundle, "version", 1);
    cJSON_AddStringToObject(bundle, "redaction", "credentials-auth-codes-and-nonces-omitted");
    exported_at = (int64_t)time(NULL);
    runtime = cJSON_AddObjectToObject(bundle, "runtime_snapshot");
    if (runtime) {
        cJSON_AddNumberToObject(runtime, "captured_at", (double)exported_at);
        cJSON_AddBoolToObject(runtime, "status_loaded", ui->model.status_loaded);
        cJSON_AddNumberToObject(runtime, "status_updated_at", (double)ui->model.status_updated_at);
        cJSON_AddNumberToObject(runtime, "status_age_seconds",
            ui->model.status_loaded && ui->model.status_updated_at > 0 && exported_at >= ui->model.status_updated_at
                ? (double)(exported_at - ui->model.status_updated_at) : -1.0);
        cJSON_AddNumberToObject(runtime, "day_index", ui->model.day_index);
        cJSON_AddNumberToObject(runtime, "limited_today", ui->model.limited_today);
        cJSON_AddNumberToObject(runtime, "blocked_today", ui->model.blocked_today);
        cJSON_AddNumberToObject(runtime, "unrestricted_today", ui->model.unrestricted_today);
        cJSON_AddBoolToObject(runtime, "remaining_available", ui->model.remaining_available);
        cJSON_AddNumberToObject(runtime, "remaining_minutes", ui->model.remaining_minutes);
        cJSON_AddBoolToObject(runtime, "played_minutes_available", ui->model.played_minutes_available);
        cJSON_AddNumberToObject(runtime, "played_minutes", ui->model.played_minutes);
        cJSON_AddNumberToObject(runtime, "play_timer_enabled", ui->model.play_timer_enabled);
        cJSON_AddNumberToObject(runtime, "restricted_now", ui->model.restricted_now);
        cJSON_AddStringToObject(runtime, "rule_source", ui->model.rule_source);
        cJSON_AddBoolToObject(runtime, "disable_flag_present", ui->model.disable_flag_present);
        snprintf(path, sizeof(path), APP_ROOT "/recovery/active");
        cJSON_AddBoolToObject(runtime, "active_recovery_present",
            ui->client.storage->vtable->exists(ui->client.storage, path));
        cJSON_AddBoolToObject(runtime, "request_in_progress", ui->waiting);
        cJSON_AddStringToObject(runtime, "transport", ui->model.transport_label);
    }
    for (i = 0; i < ptc_support_export_file_count(); ++i) {
        const char *file_name = ptc_support_export_file(i);
        cJSON *item;
        snprintf(path, sizeof(path), APP_ROOT "/%s", file_name);
        if (!ui->client.storage->vtable->read_text(ui->client.storage, path, text, sizeof(text))) continue;
        if (!ptc_support_export_text_safe(text)) {
            rejected_sensitive_file = true;
            continue;
        }
        item = cJSON_Parse(text);
        if (item) cJSON_AddItemToObject(bundle, file_name, item);
    }
    if (rejected_sensitive_file) cJSON_AddStringToObject(bundle, "redaction_warning", "sensitive-source-file-omitted");
    rendered = cJSON_PrintUnformatted(bundle);
    cJSON_Delete(bundle);
    snprintf(output_path, sizeof(output_path), APP_ROOT "/support/diagnostic-%lld.json", (long long)exported_at);
    if (rendered && ui->client.storage->vtable->write_text_atomic(ui->client.storage, output_path, rendered)) {
        snprintf(ui->model.diagnostic_path, sizeof(ui->model.diagnostic_path),
                 "sdmc:/switch/playwise/support/diagnostic-%lld.json", (long long)exported_at);
        ui->model.diagnostic_status = PTC_UI_DIAGNOSTIC_SUCCESS;
        snprintf(ui->model.message, sizeof(ui->model.message), "诊断包导出成功。");
    } else {
        ui->model.diagnostic_status = PTC_UI_DIAGNOSTIC_ERROR;
        snprintf(ui->model.message, sizeof(ui->model.message), "生成诊断包失败。");
    }
    free(rendered);
}

static void format_today_label(uint16_t day_index, char *out, size_t out_size)
{
    static const char *WEEKDAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    uint16_t year;
    uint8_t month;
    uint8_t day;
    if (ptc_date_from_day_index(day_index, &year, &month, &day)) {
        snprintf(out, out_size, "%u 月 %u 日（%s，今天）", month, day, WEEKDAYS[ptc_weekday_from_day_index(day_index)]);
    } else {
        snprintf(out, out_size, "今天");
    }
}

static PtcDayRule effective_today_rule(const UiState *ui)
{
    uint8_t weekday = ptc_weekday_from_day_index(ui->model.day_index);
    if (ui->model.today_override_present) {
        return ui->model.today_override_rule;
    }
    return ui->model.current_week[weekday];
}

static void format_rule_remaining(const PtcUiModel *model, PtcDayRule rule, char *out, size_t out_size)
{
    int remaining;
    if (rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(out, out_size, "不限时");
        return;
    }
    if (!model->played_minutes_available || model->played_minutes < 0) {
        snprintf(out, out_size, "暂不可用");
        return;
    }
    remaining = (int)rule.minutes - model->played_minutes;
    if (remaining < 0) remaining = 0;
    snprintf(out, out_size, "%d 分钟", remaining);
}

static uint16_t current_today_limit_value(const UiState *ui)
{
    PtcDayRule rule = effective_today_rule(ui);
    int total;
    if (rule.mode == PTC_RULE_MODE_LIMIT && rule.minutes >= 1 && rule.minutes <= 1440) {
        return rule.minutes;
    }
    if (ui->model.remaining_available && ui->model.remaining_minutes >= 0 &&
        ui->model.played_minutes_available && ui->model.played_minutes >= 0) {
        total = ui->model.remaining_minutes + ui->model.played_minutes;
        if (total >= 1 && total <= 1440) return (uint16_t)total;
    }
    return 60;
}

static void handle_today_action_ready(UiState *ui, int index)
{
    char date[64];
    char body[320];
    char played[32];
    char remaining[32];
    format_today_label(ui->model.day_index, date, sizeof(date));
    if (ui->model.played_minutes_available) snprintf(played, sizeof(played), "约 %d 分钟", ui->model.played_minutes);
    else snprintf(played, sizeof(played), "暂不可用");
    if (ui->model.unrestricted_today == 1) snprintf(remaining, sizeof(remaining), "不限时");
    else if (ui->model.remaining_available) snprintf(remaining, sizeof(remaining), "%d 分钟", ui->model.remaining_minutes);
    else snprintf(remaining, sizeof(remaining), "暂不可用");
    switch (index) {
    case 1:
        open_minutes_overlay(ui, PTC_UI_OPERATION_SET_TODAY_LIMIT, "设置今日额度",
            ui->model.unrestricted_today == 1
                ? "今天当前为不限时。设置额度后将恢复限时。"
                : "设置今天的总额度；页面会显示调整前后的可玩时间。",
            current_today_limit_value(ui), 1, 1440);
        break;
    case 2:
        if (ui->model.unrestricted_today == 1) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "今天已不限时，无需加时；如需恢复限时，请使用“设置今日额度”。");
        } else {
            open_minutes_overlay(ui, PTC_UI_OPERATION_ADD_TODAY_MINUTES, "临时加时",
                "在今天现有额度上增加时间；当前状态和调整后剩余如下。", 15, 1, 120);
        }
        break;
    case 3:
        snprintf(body, sizeof(body), "%s：当前已玩 %s，当前剩余 %s。\n设置后今天不限时；明天继续使用每周计划。",
                 date, played, remaining);
        open_confirm_overlay(ui, PTC_UI_OPERATION_DISABLE_TODAY_LIMIT, "将今天设为不限时", body);
        break;
    case 4: {
        PtcDayRule current = effective_today_rule(ui);
        uint8_t weekday = ptc_weekday_from_day_index(ui->model.day_index);
        PtcDayRule weekly = ui->model.current_week[weekday];
        char current_remaining[32];
        char weekly_remaining[32];
        format_rule_remaining(&ui->model, current, current_remaining, sizeof(current_remaining));
        format_rule_remaining(&ui->model, weekly, weekly_remaining, sizeof(weekly_remaining));
        snprintf(body, sizeof(body),
                 "%s\n当前生效剩余：%s\n恢复周计划后：%s\n将清除今天的临时额度或不限时状态。",
                 date, current_remaining, weekly_remaining);
        open_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_TODAY_POLICY, "恢复周计划", body);
        if (weekly.mode == PTC_RULE_MODE_LIMIT) {
            ui->model.confirm_hold_required = !ui->model.played_minutes_available ||
                ui->model.played_minutes < 0 || (int)weekly.minutes - ui->model.played_minutes <= 0;
        }
        break;
    }
    default:
        break;
    }
}

static void handle_parent_action(UiState *ui)
{
    int index = ui->model.selected_index;
    if (ui->model.disable_flag_present && ui->model.parent_page == PTC_UI_PARENT_TODAY && index > 0) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "紧急停用已开启，此项控制写入不可用；请到支持与恢复解除停用。");
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        if (index == 0) {
            submit_status(ui);
        } else {
            ui->pending_today_action = index;
            submit_status(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "正在刷新当前已玩和剩余时间…");
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN) {
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_HOLIDAY) {
        if (ui->model.disable_flag_present && index != 5) {
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，国家节假日设置暂时只读。");
            return;
        }
        switch (index) {
        case 0:
            ui->model.draft_holiday_enabled = !ui->model.draft_holiday_enabled;
            update_holiday_dirty(ui);
            break;
        case 1:
            ui->model.draft_holiday_rule.mode = ui->model.draft_holiday_rule.mode == PTC_RULE_MODE_LIMIT
                ? PTC_RULE_MODE_UNLIMITED : PTC_RULE_MODE_LIMIT;
            update_holiday_dirty(ui);
            break;
        case 2:
            if (ui->model.draft_holiday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(ui->model.message, sizeof(ui->model.message), "请先把法定休假日切换为限时模式。");
            } else {
                ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_HOLIDAY_MINUTES, PTC_UI_OVERLAY_NONE,
                    "设置法定休假日额度", "输入 1 到 1440 分钟", 4, 1, 1440, ui->model.draft_holiday_rule.minutes);
            }
            break;
        case 3:
            ui->model.draft_makeup_workday_rule.mode = ui->model.draft_makeup_workday_rule.mode == PTC_RULE_MODE_LIMIT
                ? PTC_RULE_MODE_UNLIMITED : PTC_RULE_MODE_LIMIT;
            update_holiday_dirty(ui);
            break;
        case 4:
            if (ui->model.draft_makeup_workday_rule.mode == PTC_RULE_MODE_UNLIMITED) {
                snprintf(ui->model.message, sizeof(ui->model.message), "请先把调休工作日切换为限时模式。");
            } else {
                ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_MAKEUP_MINUTES, PTC_UI_OVERLAY_NONE,
                    "设置调休工作日额度", "输入 1 到 1440 分钟", 4, 1, 1440,
                    ui->model.draft_makeup_workday_rule.minutes);
            }
            break;
        case 5:
            ui->model.overlay = PTC_UI_OVERLAY_HOLIDAY_CALENDAR;
            ui->model.holiday_calendar_page = 0;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "内置节假日安排");
            break;
        case 6:
            submit_holiday_policy(ui);
            break;
        default:
            break;
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_SECURITY) {
        switch (index) {
        case 0: open_local_grant(ui); break;
        case 1: show_pairing_qr(ui); break;
        case 2: open_grant_manager(ui); break;
        case 3: change_parent_pin(ui); break;
        case 4: open_shortcut_manager(ui); break;
        case 5:
            refresh_album_restriction(ui);
            if (ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_OFF) {
                open_confirm_overlay(ui, PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION, "启用相册入口限制？",
                    "将备份 Atmosphère 与 More Menu 配置，并把 hbmenu 入口改为在相册图标按住 R+X 后按 A。保存后需重启主机生效。");
            } else if (ui->model.album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED) {
                open_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY, "恢复原来的相册入口？",
                    "将按可信备份精确恢复两个原文件。保存后需重启主机生效；恢复备份仍会保留。");
            } else if (ui->model.album_backup_valid) {
                open_confirm_overlay(ui, PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY, "检测到外部改动",
                    "配置与事务记录不一致。继续会使用可信原始备份强制恢复；请确认可以放弃后续外部修改。");
                ui->model.confirm_hold_required = true;
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message),
                         "相册入口状态异常且备份不可用；已拒绝自动修改。请保留现场并人工恢复。");
            }
            break;
        default: break;
        }
        return;
    }
    switch (index) {
    case 0:
        if (ui->model.disable_flag_present) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "解除停用并重新接管",
                                 "重新执行只读兼容预检；仅预检通过后才解除停用并恢复额度管理。");
        } else {
            open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "确认接管系统控制",
                                 "先执行只读兼容预检；通过后保存安装快照并启用额度管理。");
        }
        break;
    case 1:
        open_confirm_overlay(ui, PTC_UI_OPERATION_RETRY_SETUP_RELEASE, "重试修复",
                             "重新执行安全前置检查，并在可恢复时继续首次设置。");
        break;
    case 2:
        refresh_disable_flag(ui);
        if (ui->model.disable_flag_present) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "解除停用并重新接管",
                                 "功能：重新执行只读安全预检，通过后解除 disable.flag 并恢复控制。\n适用：故障已排除且确认当前规则配置安全。");
        } else {
            open_confirm_overlay(ui, PTC_UI_OPERATION_EMERGENCY_DISABLE, "紧急停用控制",
                                 "功能：创建 disable.flag，立即停止正常控制写入。\n适用：异常限制、写入故障或需要保留现场。");
        }
        break;
    case 3:
        open_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT, "恢复安装前状态",
                             "恢复原始家长控制设置与计时器，并停止新的控制写入。");
        break;
    case 4:
        export_diagnostics(ui);
        break;
    case 5:
        refresh_disable_flag(ui);
        submit_status(ui);
        break;
    default:
        break;
    }
}

static void confirm_operation(UiState *ui)
{
    PtcCompanionStatus status;
    PtcUiOperation operation = ptc_ui_take_confirmed_operation(&ui->model);
    switch (operation) {
    case PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION:
    case PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY:
    case PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY: {
        char error[160] = {0};
        bool ok = operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION
            ? ptc_album_restriction_enable(ui->client.storage, error, sizeof(error))
            : ptc_album_restriction_restore(ui->client.storage,
                operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY, error, sizeof(error));
        refresh_album_restriction(ui);
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 ok ? "相册入口配置已保存，请重启主机后确认生效。" : error);
        break;
    }
    case PTC_UI_OPERATION_SET_TODAY_LIMIT:
        submit_minutes(ui, operation, ui->model.draft_minutes);
        break;
    case PTC_UI_OPERATION_SAVE_WEEKLY:
        submit_weekly(ui);
        break;
    case PTC_UI_OPERATION_DISABLE_TODAY_LIMIT:
        submit_transport_empty(ui, "disable_today_limit", "正在解除当前限制…", "解除当前限制失败");
        break;
    case PTC_UI_OPERATION_RESTORE_TODAY_POLICY:
        submit_transport_empty(ui, "restore_today_policy", "正在恢复周计划…", "恢复计划失败");
        break;
    case PTC_UI_OPERATION_REDEEM_OFFLINE_CODE:
        ui->code_previous_after_available = ui->model.code_preview_after_available;
        ui->code_previous_after_zero = ui->model.code_preview_after_available &&
            ui->model.code_preview_after_minutes == 0;
        ui->code_previous_capped = ui->model.code_preview_capped;
        ui->code_previous_converts_unlimited = ui->model.code_preview_converts_unlimited;
        ui->code_preview_recheck = true;
        submit_preview_offline_code(ui, ui->model.pending_code);
        break;
    case PTC_UI_OPERATION_SAVE_CREDENTIAL:
        if (!commit_credential(ui)) snprintf(ui->model.message, sizeof(ui->model.message), "保存加时码密钥失败。");
        break;
    case PTC_UI_OPERATION_RESET_PAIRING_URL:
        apply_default_pairing_base_url(ui);
        break;
    case PTC_UI_OPERATION_COMPLETE_SETUP:
        submit_transport_empty(ui, "complete_setup", "正在完成首次设置…", "启用自动控制失败");
        break;
    case PTC_UI_OPERATION_RETRY_SETUP_RELEASE:
        submit_transport_empty(ui, "retry_setup_release", "正在重试解除当前限制…", "重试前置解限失败");
        break;
    case PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT:
        submit_transport_empty(ui, "restore_install_snapshot", "正在恢复安装前状态…", "恢复安装前状态失败");
        break;
    case PTC_UI_OPERATION_EMERGENCY_DISABLE:
        set_local_sd_command(ui, "紧急停用控制");
        status = ptc_companion_set_disable_flag(&ui->client, true);
        (void)ptc_companion_transport_notify_storage_changed(&ui->transport);
        ui->model.feedback_detail[0] = '\0';
        if (status == PTC_COMPANION_OK) {
            ui->model.disable_flag_present = true;
            snprintf(ui->model.result_status, sizeof(ui->model.result_status), "ok");
            snprintf(ui->model.message, sizeof(ui->model.message), "后台控制已紧急停用。");
        } else {
            set_message(ui, "紧急停用失败", status);
        }
        break;
    case PTC_UI_OPERATION_RESUME_CONTROL:
        set_local_sd_command(ui, "解除紧急停用");
        status = ptc_companion_set_disable_flag(&ui->client, false);
        (void)ptc_companion_transport_notify_storage_changed(&ui->transport);
        ui->model.feedback_detail[0] = '\0';
        if (status == PTC_COMPANION_OK) {
            ui->model.disable_flag_present = false;
            snprintf(ui->model.result_status, sizeof(ui->model.result_status), "ok");
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用已解除，后台控制已恢复。");
        } else {
            set_message(ui, "解除紧急停用失败", status);
        }
        break;
    default:
        break;
    }
}

static void accept_numpad(UiState *ui)
{
    PtcUiNumpadPurpose purpose = ui->model.numpad_purpose;
    uint16_t value = 0;
    char code[9];
    if (purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES && weekly_editing_blocked(ui)) {
        return;
    }
    if (!ptc_ui_numpad_validate(&ui->model, &value)) {
        return;
    }
    if (purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        snprintf(code, sizeof(code), "%s", ui->model.numpad_text);
        snprintf(ui->model.pending_code, sizeof(ui->model.pending_code), "%s", code);
        ptc_ui_numpad_finish(&ui->model);
        ui->code_preview_recheck = false;
        submit_preview_offline_code(ui, code);
        return;
    }
    if (purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) {
        ui->model.draft_week[ui->model.editor_index].minutes = value;
        ui->model.weekly_dirty = memcmp(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week)) != 0;
    } else if (purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES) {
        ui->model.draft_holiday_rule.minutes = value;
        update_holiday_dirty(ui);
    } else if (purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) {
        ui->model.draft_makeup_workday_rule.minutes = value;
        update_holiday_dirty(ui);
    } else if (purpose == PTC_UI_NUMPAD_MINUTES) {
        ui->model.draft_minutes = value;
    }
    ptc_ui_numpad_finish(&ui->model);
}

static void update_weekly_dirty(UiState *ui)
{
    ui->model.weekly_dirty = memcmp(
        ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week)) != 0;
}

static void update_holiday_dirty(UiState *ui)
{
    ui->model.holiday_dirty = ui->model.draft_holiday_enabled != ui->model.holiday_enabled ||
        memcmp(&ui->model.draft_holiday_rule, &ui->model.holiday_rule, sizeof(PtcDayRule)) != 0 ||
        memcmp(&ui->model.draft_makeup_workday_rule, &ui->model.makeup_workday_rule, sizeof(PtcDayRule)) != 0;
}

static void refresh_album_restriction(UiState *ui)
{
    PtcAlbumRestrictionStatus status;
    if (!ui || !ptc_album_restriction_get_status(ui->client.storage, &status)) return;
    ui->model.album_restriction_state = (int)status.state;
    ui->model.album_backup_valid = status.backup_valid;
    snprintf(ui->model.album_restriction_detail, sizeof(ui->model.album_restriction_detail), "%s", status.detail);
}

static void save_weekly_from_page(UiState *ui)
{
    uint8_t weekday;
    PtcDayRule before;
    PtcDayRule after;
    char body[192];
    if (weekly_editing_blocked(ui)) {
        return;
    }
    if (!ui->model.weekly_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), "周计划没有修改。");
        return;
    }
    weekday = ptc_weekday_from_day_index(ui->model.day_index);
    before = ui->model.current_week[weekday];
    after = ui->model.draft_week[weekday];
    if (!ui->model.today_override_present &&
        ptc_ui_day_rule_effectively_changed(before, after)) {
        if (after.mode == PTC_RULE_MODE_UNLIMITED) {
            snprintf(body, sizeof(body), "今天对应的周计划将改为不限时。\n保存后由后台同步；当前已玩和剩余会在刷新后更新。");
        } else if (ui->model.played_minutes_available) {
            int remaining = (int)after.minutes - ui->model.played_minutes;
            if (remaining < 0) remaining = 0;
            snprintf(body, sizeof(body), "今天已玩约 %d 分钟；新额度 %u 分钟。\n保存并同步后还可玩约 %d 分钟。",
                     ui->model.played_minutes, (unsigned int)after.minutes, remaining);
        } else {
            snprintf(body, sizeof(body), "今天的新额度为 %u 分钟。\n当前已玩暂不可用；实际剩余将在同步后刷新。",
                     (unsigned int)after.minutes);
        }
        open_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_WEEKLY, "周计划将影响今天", body);
        ui->model.confirm_hold_required = !ui->model.played_minutes_available ||
            ptc_ui_day_rule_would_restrict(&ui->model, after);
    } else {
        submit_weekly(ui);
    }
}

static void apply_pending_navigation(UiState *ui)
{
    if (ui->pending_leave_parent) {
        ui->model.view = ui->model.setup_phase[0] && strcmp(ui->model.setup_phase, "active") != 0
            ? PTC_UI_SETUP : PTC_UI_CHILD;
        snprintf(ui->model.message, sizeof(ui->model.message), "已返回主页面。");
        if (ui->model.view == PTC_UI_CHILD) {
            submit_status(ui);
        }
    } else if (ui->pending_parent_page >= 0) {
        if (ui->pending_parent_page == PTC_UI_PARENT_PLAN) {
            open_weekly_page(ui);
        } else {
            ui->model.parent_page = (PtcUiParentPage)ui->pending_parent_page;
            ui->model.selected_index = 0;
            if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
                submit_status(ui);
            } else if (ui->model.parent_page == PTC_UI_PARENT_SECURITY) {
                refresh_album_restriction(ui);
            }
        }
    }
    ui->pending_parent_page = -1;
    ui->pending_leave_parent = false;
}

static void request_parent_navigation(UiState *ui, int target_page, bool leave_parent)
{
    if (ui->model.parent_page == PTC_UI_PARENT_SUPPORT &&
        (leave_parent || target_page != PTC_UI_PARENT_SUPPORT)) {
        ui->model.diagnostic_status = PTC_UI_DIAGNOSTIC_IDLE;
        ui->model.diagnostic_path[0] = '\0';
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.weekly_dirty) {
        ui->pending_parent_page = target_page;
        ui->pending_leave_parent = leave_parent;
        ui->model.overlay = PTC_UI_OVERLAY_WEEKLY_LEAVE;
        refresh_disable_flag(ui);
        ui->model.weekly_leave_selection = ui->model.disable_flag_present ? 2 : 1;
        if (!leave_parent && target_page == PTC_UI_PARENT_PLAN) {
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "刷新周计划？");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
                     ui->model.disable_flag_present
                       ? "紧急停用期间不能保存；请选择保留草稿并刷新、放弃草稿并刷新，或返回。"
                       : "刷新会重新读取后台规则；请选择先保存、放弃草稿并刷新，或返回编辑。");
        } else {
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "离开周计划？");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
                     ui->model.disable_flag_present
                       ? "紧急停用期间不能保存；请选择保留草稿并离开、放弃草稿并离开，或返回。"
                       : "请选择保存修改、放弃修改，或返回继续编辑。");
        }
        return;
    }
    ui->pending_parent_page = target_page;
    ui->pending_leave_parent = leave_parent;
    apply_pending_navigation(ui);
}

static void close_code_result(UiState *ui)
{
    bool terminal;
    if (!ui || ui->model.overlay != PTC_UI_OVERLAY_CODE_RESULT) return;
    terminal = !ui->model.code_result_pending;
    ptc_ui_cancel_overlay(&ui->model);
    if (terminal) {
        if (ptc_companion_pending_redemption_clear(&ui->client) != PTC_COMPANION_OK) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "兑换结果已显示，但恢复标记暂未清除；下次打开可能再次显示同一结果。");
        }
        memset(&ui->pending_redemption, 0, sizeof(ui->pending_redemption));
        ui->model.code_result_failed = false;
    } else {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "加时结果仍在确认中；可继续使用其他页面，下次打开也会继续确认。");
    }
}

static void handle_overlay_input(UiState *ui, u64 down)
{
    if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_CALENDAR) {
        int pages = (int)((ptc_holiday_calendar_arrangement_count(ptc_holiday_calendar_info()->last_year) + 3u) / 4u);
        if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus)) ptc_ui_cancel_overlay(&ui->model);
        else if ((down & (HidNpadButton_Left | HidNpadButton_L)) && ui->model.holiday_calendar_page > 0) --ui->model.holiday_calendar_page;
        else if ((down & (HidNpadButton_Right | HidNpadButton_R)) && ui->model.holiday_calendar_page + 1 < pages) ++ui->model.holiday_calendar_page;
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_SOFTWARE_INFO) {
        if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus)) {
            ptc_ui_cancel_overlay(&ui->model);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_AUTH_ERROR) {
        if (down & HidNpadButton_B) {
            close_auth_error(ui, true);
        } else if ((down & (HidNpadButton_A | HidNpadButton_Plus)) &&
                   ui->model.auth_cooldown_seconds <= 0) {
            AuthRetryAction action = ui->auth_retry_action;
            close_auth_error(ui, false);
            ui->auth_retry_action = AUTH_RETRY_NONE;
            dispatch_auth_retry(ui, action);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_SHORTCUT_MANAGER) {
        if (ui->model.shortcut_capture_active) {
            update_setup_shortcut_capture(ui, down, down | ui->model.captured_shortcut_mask);
        } else if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
            refresh_custom_shortcut_label(ui);
        } else if (down & HidNpadButton_Up) {
            ui->model.setup_shortcut_index = ui->model.setup_shortcut_index <= 0
                ? PTC_UI_SHORTCUT_PRESET_COUNT - 1 : ui->model.setup_shortcut_index - 1;
        } else if (down & HidNpadButton_Down) {
            ui->model.setup_shortcut_index = (ui->model.setup_shortcut_index + 1) % PTC_UI_SHORTCUT_PRESET_COUNT;
        } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            ui->model.setup_shortcut_index = (ui->model.setup_shortcut_index + 7) % PTC_UI_SHORTCUT_PRESET_COUNT;
        } else if (down & HidNpadButton_A) {
            select_setup_shortcut(ui, ui->model.setup_shortcut_index);
        } else if (down & HidNpadButton_X) {
            begin_setup_shortcut_capture(ui);
        } else if (down & HidNpadButton_Y) {
            ui->model.shortcut_draft_show_hint = !ui->model.shortcut_draft_show_hint;
        } else if (down & HidNpadButton_ZL) {
            ui->model.shortcut_draft_enabled = false;
        } else if (down & HidNpadButton_Plus) {
            if (commit_shortcut_preferences(ui)) {
                ui->model.overlay = PTC_UI_OVERLAY_NONE;
                snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                         ui->model.custom_shortcut_enabled
                            ? "家长区快捷键已确认更新。"
                            : "自定义快捷键已关闭；固定 Minus - 仍然有效。");
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message), "快捷键设置未保存，请确认 SD 卡可写。");
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_WEEKLY_LEAVE) {
        if (down & HidNpadButton_Left) {
            ptc_ui_weekly_leave_move(&ui->model, -1);
        } else if (down & HidNpadButton_Right) {
            ptc_ui_weekly_leave_move(&ui->model, 1);
        } else if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & HidNpadButton_X) {
            memcpy(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week));
            ui->model.weekly_dirty = false;
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            apply_pending_navigation(ui);
        } else if (down & HidNpadButton_Plus) {
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            if (ui->model.disable_flag_present) {
                apply_pending_navigation(ui);
            } else {
                submit_weekly(ui);
                if (!ui->waiting) {
                    ui->pending_parent_page = -1;
                    ui->pending_leave_parent = false;
                }
            }
        } else if (down & HidNpadButton_A) {
            if (ui->model.weekly_leave_selection == 0) {
                handle_overlay_input(ui, HidNpadButton_X);
            } else if (ui->model.weekly_leave_selection == 1) {
                handle_overlay_input(ui, HidNpadButton_B);
            } else {
                handle_overlay_input(ui, HidNpadButton_Plus);
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL) {
        if (down & HidNpadButton_B) {
            if (strcmp(ui->model.credential_current, ui->model.credential_new) != 0) {
                ui->model.overlay = PTC_UI_OVERLAY_CREDENTIAL_LEAVE;
                ui->model.overlay_selection = 1;
                snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "放弃配对信息修改？");
                snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                         "手工输入和随机生成只修改草稿，尚未保存。");
            } else {
                show_grant_manager(ui, ui->model.credential_kind == 1
                    ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
            }
        } else if (down & (HidNpadButton_Up | HidNpadButton_Left)) {
            ptc_ui_move_overlay_selection(&ui->model, -1, 0);
        } else if (down & (HidNpadButton_Down | HidNpadButton_Right)) {
            ptc_ui_move_overlay_selection(&ui->model, 1, 0);
        } else if (down & HidNpadButton_X) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
            edit_credential_input(ui);
        } else if (down & HidNpadButton_ZR) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_REVEAL;
            reveal_current_credential(ui);
        } else if (down & HidNpadButton_Y) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_RANDOM;
            randomize_credential(ui);
        } else if ((down & HidNpadButton_R) && ui->model.credential_kind == 2) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_DEMO;
            if (ptc_grant_secret_is_demo(ui->model.credential_current)) randomize_credential(ui);
            else snprintf(ui->model.credential_new, sizeof(ui->model.credential_new), "%s", PTC_DEMO_GRANT_SECRET);
            ui->model.credential_new_revealed = true;
        } else if (down & HidNpadButton_A) {
            if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_RANDOM) randomize_credential(ui);
            else if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_REVEAL && ui->model.credential_kind == 2) {
                handle_overlay_input(ui, HidNpadButton_ZR);
            } else if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_DEMO && ui->model.credential_kind == 2) {
                handle_overlay_input(ui, HidNpadButton_R);
            } else if (ui->model.overlay_selection == PTC_UI_CREDENTIAL_SAVE) request_save_credential(ui);
            else edit_credential_input(ui);
        } else if (down & HidNpadButton_Plus) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_SAVE;
            request_save_credential(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
        if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            ui->model.overlay_selection = ui->model.overlay_selection == 0 ? 1 : 0;
        } else if (down & (HidNpadButton_B | HidNpadButton_A)) {
            if ((down & HidNpadButton_A) && ui->model.overlay_selection == 0) {
                show_grant_manager(ui, ui->model.credential_kind == 1
                    ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
                snprintf(ui->model.message, sizeof(ui->model.message), "已放弃未保存的配对信息修改。");
            } else {
                ptc_ui_cancel_overlay(&ui->model);
            }
        } else if (down & HidNpadButton_X) {
            show_grant_manager(ui, ui->model.credential_kind == 1
                ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
            snprintf(ui->model.message, sizeof(ui->model.message), "已放弃未保存的配对信息修改。");
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_MANAGER) {
        if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
        else if (down & HidNpadButton_Left) ptc_ui_move_overlay_selection(&ui->model, -1, 0);
        else if (down & HidNpadButton_Right) ptc_ui_move_overlay_selection(&ui->model, 1, 0);
        else if (down & HidNpadButton_Up) ptc_ui_move_overlay_selection(&ui->model, 0, -1);
        else if (down & HidNpadButton_Down) ptc_ui_move_overlay_selection(&ui->model, 0, 1);
        else if (down & HidNpadButton_A) {
            if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_DEVICE) open_credential_manager(ui, 1);
            else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_SECRET) open_credential_manager(ui, 2);
            else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_EXPORT) export_parent_import(ui);
            else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_EDIT_URL) edit_pairing_base_url(ui);
            else if (ui->model.overlay_selection == PTC_UI_GRANT_MANAGER_RESET_URL) request_reset_pairing_base_url(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
        if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & HidNpadButton_Left) ptc_ui_move_overlay_selection(&ui->model, -1, 0);
        else if (down & HidNpadButton_Right) ptc_ui_move_overlay_selection(&ui->model, 1, 0);
        else if (down & HidNpadButton_Up) ptc_ui_move_overlay_selection(&ui->model, 0, -1);
        else if (down & HidNpadButton_Down) ptc_ui_move_overlay_selection(&ui->model, 0, 1);
        else if (down & HidNpadButton_L) { ui->model.overlay_selection = 2; adjust_grant_minutes_delta(ui, -15); }
        else if (down & HidNpadButton_R) { ui->model.overlay_selection = 3; adjust_grant_minutes_delta(ui, 15); }
        else if (down & HidNpadButton_ZL) { ui->model.overlay_selection = 4; adjust_grant_minutes_delta(ui, -30); }
        else if (down & HidNpadButton_ZR) { ui->model.overlay_selection = 5; adjust_grant_minutes_delta(ui, 30); }
        else if (down & HidNpadButton_Plus) {
            ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
            generate_local_grant_code(ui);
        } else if (down & HidNpadButton_A) {
            int selection = ui->model.overlay_selection;
            if (selection == 0) adjust_grant_minutes(ui, -1);
            else if (selection == 1) adjust_grant_minutes(ui, 1);
            else if (selection == 2) adjust_grant_minutes_delta(ui, -15);
            else if (selection == 3) adjust_grant_minutes_delta(ui, 15);
            else if (selection == 4) adjust_grant_minutes_delta(ui, -30);
            else if (selection == 5) adjust_grant_minutes_delta(ui, 30);
            else if (selection == PTC_UI_GRANT_LOCAL_BACK) handle_overlay_input(ui, HidNpadButton_B);
            else generate_local_grant_code(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_QR) {
        if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
        if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus)) {
            close_code_result(ui);
        }
        return;
    }
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消修改。");
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_NUMPAD) {
        if (down & HidNpadButton_Left) {
            ptc_ui_numpad_move(&ui->model, -1, 0);
        } else if (down & HidNpadButton_Right) {
            ptc_ui_numpad_move(&ui->model, 1, 0);
        } else if (down & HidNpadButton_Up) {
            ptc_ui_numpad_move(&ui->model, 0, -1);
        } else if (down & HidNpadButton_Down) {
            ptc_ui_numpad_move(&ui->model, 0, 1);
        } else if (down & HidNpadButton_X) {
            ptc_ui_numpad_backspace(&ui->model);
        } else if (down & HidNpadButton_Y) {
            ptc_ui_numpad_clear(&ui->model);
        } else if (down & HidNpadButton_ZL) {
            ptc_ui_numpad_adjust(&ui->model, -15);
        } else if (down & HidNpadButton_L) {
            ptc_ui_numpad_adjust(&ui->model, -5);
        } else if (down & HidNpadButton_R) {
            ptc_ui_numpad_adjust(&ui->model, 5);
        } else if (down & HidNpadButton_ZR) {
            ptc_ui_numpad_adjust(&ui->model, 15);
        } else if (down & HidNpadButton_A) {
            ptc_ui_numpad_activate(&ui->model);
        } else if (down & HidNpadButton_Plus) {
            accept_numpad(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_MINUTES) {
        if (down & HidNpadButton_Up) {
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Down) {
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Right) {
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Left) {
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Y) {
            edit_overlay_minutes(ui);
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            PtcUiOperation operation = ui->model.operation;
            if (operation == PTC_UI_OPERATION_SET_TODAY_LIMIT &&
                (ui->model.unrestricted_today == 1 ||
                 ptc_ui_limit_minutes_would_restrict(&ui->model, ui->model.draft_minutes))) {
                char body[192];
                if (ui->model.unrestricted_today == 1) {
                    if (ui->model.played_minutes_available && ui->model.played_minutes >= 0) {
                        int preview = (int)ui->model.draft_minutes - ui->model.played_minutes;
                        if (preview < 0) preview = 0;
                        snprintf(body, sizeof(body),
                                 "当前已玩 %d 分钟；新额度 %u 分钟。\n修改后还剩 %d 分钟可玩。",
                                 ui->model.played_minutes, (unsigned int)ui->model.draft_minutes, preview);
                        open_confirm_overlay(ui, operation, "不限时将改为限时", body);
                        ui->model.confirm_hold_required = preview == 0;
                    } else {
                        snprintf(body, sizeof(body),
                                 "今天当前为不限时；设置 %u 分钟后将恢复限时。\n当前已玩不可用，暂时无法估算修改后剩余。",
                                 (unsigned int)ui->model.draft_minutes);
                        open_confirm_overlay(ui, operation, "不限时将改为限时", body);
                        ui->model.confirm_hold_required = true;
                    }
                } else {
                    snprintf(body, sizeof(body),
                             "今天已玩约 %d 分钟；设置总额度 %u 分钟。\n调整后可能立即没有可玩时间。",
                             ui->model.played_minutes, (unsigned int)ui->model.draft_minutes);
                    open_confirm_overlay(ui, operation, "新额度不高于已玩时间", body);
                    ui->model.confirm_hold_required = true;
                }
            } else {
                ui->model.overlay = PTC_UI_OVERLAY_NONE;
                ui->model.operation = PTC_UI_OPERATION_NONE;
                submit_minutes(ui, operation, ui->model.draft_minutes);
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_WEEKLY) {
        PtcDayRule *day = &ui->model.draft_week[ui->model.editor_index];
        if (down & HidNpadButton_Left) {
            ui->model.editor_index = ui->model.editor_index <= 0 ? 6 : ui->model.editor_index - 1;
        } else if (down & HidNpadButton_Right) {
            ui->model.editor_index = ui->model.editor_index >= 6 ? 0 : ui->model.editor_index + 1;
        } else if ((down & (HidNpadButton_X | HidNpadButton_Up | HidNpadButton_Down |
                           HidNpadButton_Y | HidNpadButton_A | HidNpadButton_Plus)) &&
                   weekly_editing_blocked(ui)) {
            return;
        } else if (down & HidNpadButton_X) {
            day->mode = ptc_ui_next_rule_mode(day->mode);
        } else if ((down & HidNpadButton_Up) && day->mode == PTC_RULE_MODE_LIMIT) {
            day->minutes = ptc_ui_adjust_minutes(day->minutes, 15, 1, 1440);
        } else if ((down & HidNpadButton_Down) && day->mode == PTC_RULE_MODE_LIMIT) {
            day->minutes = ptc_ui_adjust_minutes(day->minutes, -15, 1, 1440);
        } else if ((down & HidNpadButton_Y) && day->mode == PTC_RULE_MODE_LIMIT) {
            edit_weekly_minutes(ui);
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            uint8_t weekday = ptc_weekday_from_day_index(ui->model.day_index);
            PtcDayRule today = ui->model.draft_week[weekday];
            if (!ui->model.today_override_present &&
                (!ui->model.played_minutes_available || ptc_ui_day_rule_would_restrict(&ui->model, today))) {
                char body[192];
                snprintf(body, sizeof(body),
                         "已用约 %d 分钟；今天设置 %u 分钟。页面可能立即受限。",
                         ui->model.played_minutes, (unsigned int)today.minutes);
                open_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_WEEKLY, "每周计划可能立即生效", body);
                ui->model.confirm_hold_required = true;
            } else {
                ui->model.overlay = PTC_UI_OVERLAY_NONE;
                submit_weekly(ui);
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM && (down & (HidNpadButton_A | HidNpadButton_Plus))) {
        confirm_operation(ui);
    }
}

/*
 * Touch dispatch. A tap resolves to the same control geometry the renderer used
 * (ptc_ui_hit_test), then drives the exact action its button shortcut would, so
 * pointer and pad stay in lock-step.
 */
static void handle_touch(UiState *ui, int x, int y)
{
    PtcUiHit hit = ptc_ui_hit_test(&ui->model, x, y);
    if (ui->waiting && ui->model.view == PTC_UI_PARENT &&
        ui->model.overlay == PTC_UI_OVERLAY_NONE) {
        snprintf(ui->model.message, sizeof(ui->model.message), "请等待当前操作完成后再修改设置。");
        return;
    }
    switch (hit.kind) {
    case PTC_UI_HIT_CHILD_SUBMIT_CODE:
        if (ui->waiting) {
            snprintf(ui->model.message, sizeof(ui->model.message), "请等待当前操作完成后再提交加时码。");
        } else {
            open_offline_code_input(ui);
        }
        break;
    case PTC_UI_HIT_CHILD_REFRESH:
        submit_status(ui);
        break;
    case PTC_UI_HIT_CHILD_PARENT:
        enter_parent_area(ui);
        break;
    case PTC_UI_HIT_CHILD_EXIT:
        ui->exit_requested = true;
        break;
    case PTC_UI_HIT_ERROR_RETRY:
        retry_error(ui);
        break;
    case PTC_UI_HIT_ERROR_BACK:
        enter_child_area(ui);
        break;
    case PTC_UI_HIT_SETUP_SHORTCUT_CARD:
        select_setup_shortcut(ui, hit.index);
        break;
    case PTC_UI_HIT_SETUP_SHORTCUT_CAPTURE:
        begin_setup_shortcut_capture(ui);
        break;
    case PTC_UI_HIT_SETUP_PRIMARY:
        setup_primary(ui);
        break;
    case PTC_UI_HIT_SETUP_BACK:
        setup_previous(ui);
        break;
    case PTC_UI_HIT_SETUP_PIN:
        setup_pin(ui);
        break;
    case PTC_UI_HIT_SETUP_CHILD_ZONE:
        ui->model.setup_zone_index = 0;
        break;
    case PTC_UI_HIT_SETUP_PARENT_ZONE:
        ui->model.setup_zone_index = 1;
        break;
    case PTC_UI_HIT_PARENT_PREV_PAGE:
        request_parent_navigation(ui,
            (ui->model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
        break;
    case PTC_UI_HIT_PARENT_NEXT_PAGE:
        request_parent_navigation(ui, (ui->model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
        break;
    case PTC_UI_HIT_PARENT_REFRESH:
        refresh_disable_flag(ui);
        if (ui->model.parent_page == PTC_UI_PARENT_PLAN) {
            request_parent_navigation(ui, PTC_UI_PARENT_PLAN, false);
        } else {
            poll_result(ui, true);
        }
        break;
    case PTC_UI_HIT_PARENT_STATUS:
        ui->model.parent_footer_focused = true;
        activate_parent_status(ui);
        break;
    case PTC_UI_HIT_PARENT_BACK:
        request_parent_navigation(ui, -1, true);
        break;
    case PTC_UI_HIT_PARENT_TAB:
        request_parent_navigation(ui, hit.index, false);
        break;
    case PTC_UI_HIT_PARENT_CARD:
        if (ui->waiting) {
            snprintf(ui->model.message, sizeof(ui->model.message), "请等待当前操作完成后再执行其他设置。");
        } else {
            ui->model.selected_index = hit.index;
            handle_parent_action(ui);
        }
        break;
    case PTC_UI_HIT_OVERLAY_CANCEL:
        if (ui->model.overlay == PTC_UI_OVERLAY_AUTH_ERROR) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL ||
            ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL ||
            ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
                ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_BACK;
            }
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
            close_code_result(ui);
        } else {
            ptc_ui_cancel_overlay(&ui->model);
            snprintf(ui->model.message, sizeof(ui->model.message), "已取消修改。");
        }
        break;
    case PTC_UI_HIT_OVERLAY_CONFIRM:
        if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
            close_code_result(ui);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_SAVE;
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            ui->model.overlay_selection = 1;
        }
        if (ui->model.confirm_hold_required) {
            snprintf(ui->model.message, sizeof(ui->model.message), "为避免误操作，请使用手柄长按 A 确认。");
        } else {
            handle_overlay_input(ui,
                ui->model.overlay == PTC_UI_OVERLAY_NUMPAD || ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL ||
                ui->model.overlay == PTC_UI_OVERLAY_SHORTCUT_MANAGER ||
                ui->model.overlay == PTC_UI_OVERLAY_WEEKLY_LEAVE
                    ? HidNpadButton_Plus : HidNpadButton_A);
        }
        break;
    case PTC_UI_HIT_OVERLAY_DISCARD:
        if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            ui->model.overlay_selection = 0;
        }
        handle_overlay_input(ui, HidNpadButton_X);
        break;
    case PTC_UI_HIT_MINUTES_INC:
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_DEC:
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_INC_LARGE:
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_DEC_LARGE:
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_VALUE:
        edit_overlay_minutes(ui);
        break;
    case PTC_UI_HIT_WEEKLY_DAY:
        ui->model.editor_index = hit.index;
        ui->model.selected_index = 0;
        if (weekly_editing_blocked(ui)) {
            break;
        } else if (ui->model.draft_week[hit.index].mode == PTC_RULE_MODE_LIMIT) {
            edit_weekly_minutes(ui);
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "该日为不限时，没有可编辑的分钟数；请选择“切换模式”改为限时。");
        }
        break;
    case PTC_UI_HIT_WEEKLY_MODE:
        ui->model.selected_index = 1;
        if (weekly_editing_blocked(ui)) break;
        ui->model.draft_week[ui->model.editor_index].mode =
            ptc_ui_next_rule_mode(ui->model.draft_week[ui->model.editor_index].mode);
        update_weekly_dirty(ui);
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            snprintf(ui->model.message, sizeof(ui->model.message), "已恢复此前的每日限额：%u 分钟。",
                     (unsigned int)ui->model.draft_week[ui->model.editor_index].minutes);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_UP:
        if (weekly_editing_blocked(ui)) break;
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 15, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DOWN:
        if (weekly_editing_blocked(ui)) break;
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -15, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DEC:
        if (weekly_editing_blocked(ui)) break;
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -5, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_INC:
        if (weekly_editing_blocked(ui)) break;
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 5, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_INPUT:
        if (hit.index >= 0 && hit.index < 7) {
            ui->model.editor_index = hit.index;
        }
        ui->model.selected_index = 0;
        if (weekly_editing_blocked(ui)) break;
        edit_weekly_minutes(ui);
        break;
    case PTC_UI_HIT_WEEKLY_SAVE:
        ui->model.selected_index = 3;
        save_weekly_from_page(ui);
        break;
    case PTC_UI_HIT_WEEKLY_DISCARD:
        ui->model.selected_index = 2;
        if (ui->model.weekly_dirty) {
            memcpy(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week));
            ui->model.weekly_dirty = false;
            snprintf(ui->model.message, sizeof(ui->model.message), "已放弃未保存的周计划修改。");
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message), "周计划没有修改。");
        }
        break;
    case PTC_UI_HIT_CREDENTIAL_INPUT:
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
        handle_overlay_input(ui, HidNpadButton_X);
        break;
    case PTC_UI_HIT_CREDENTIAL_RANDOM:
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_RANDOM;
        handle_overlay_input(ui, HidNpadButton_Y);
        break;
    case PTC_UI_HIT_CREDENTIAL_REVEAL:
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_REVEAL;
        handle_overlay_input(ui, HidNpadButton_ZR);
        break;
    case PTC_UI_HIT_CREDENTIAL_DEMO:
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_DEMO;
        handle_overlay_input(ui, HidNpadButton_R);
        break;
    case PTC_UI_HIT_GRANT_MANAGER_CARD:
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_GRANT_GENERATE:
        ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
        generate_local_grant_code(ui);
        break;
    case PTC_UI_HIT_SHORTCUT_OPTION:
        select_setup_shortcut(ui, hit.index);
        break;
    case PTC_UI_HIT_SHORTCUT_CAPTURE:
        begin_setup_shortcut_capture(ui);
        break;
    case PTC_UI_HIT_SHORTCUT_DISABLE:
        ui->model.shortcut_draft_enabled = false;
        break;
    case PTC_UI_HIT_SHORTCUT_HINT:
        ui->model.shortcut_draft_show_hint = !ui->model.shortcut_draft_show_hint;
        break;
    case PTC_UI_HIT_GRANT_ADJUST:
        ui->model.overlay_selection = hit.index;
        if (hit.index == 0) adjust_grant_minutes(ui, -1);
        else if (hit.index == 1) adjust_grant_minutes(ui, 1);
        else if (hit.index == 2) adjust_grant_minutes_delta(ui, -15);
        else if (hit.index == 3) adjust_grant_minutes_delta(ui, 15);
        else if (hit.index == 4) adjust_grant_minutes_delta(ui, -30);
        else if (hit.index == 5) adjust_grant_minutes_delta(ui, 30);
        break;
    case PTC_UI_HIT_NUMPAD_KEY:
        ui->model.numpad_cursor = hit.index;
        ptc_ui_numpad_activate(&ui->model);
        break;
    case PTC_UI_HIT_NUMPAD_QUICK:
        if (hit.index >= 0 && hit.index < 4) {
            static const int DELTAS[] = {-15, -5, 5, 15};
            ptc_ui_numpad_adjust(&ui->model, DELTAS[hit.index]);
        }
        break;
    case PTC_UI_HIT_NONE:
    default:
        break;
    }
}

static void draw(UiState *ui)
{
    if (ui->model.overlay == PTC_UI_OVERLAY_AUTH_ERROR && ui->auth_cooldown_until > 0) {
        int64_t remaining = ui->auth_cooldown_until - (int64_t)time(NULL);
        ui->model.auth_cooldown_seconds = remaining > 0 ? (int)remaining : 0;
    }
    ui->model.waiting = ui->waiting;
    snprintf(ui->model.request_id, sizeof(ui->model.request_id), "%s", ui->active_request_id);
    ptc_ui_graphics_draw(&ui->model);
}

static void retry_error(UiState *ui)
{
    if (!ui) {
        return;
    }
    if (ui->model.error_code == 306) {
        ui->model.view = PTC_UI_CHILD;
        submit_status(ui);
    } else {
        ui->model.view = PTC_UI_CHILD;
        open_offline_code_input(ui);
    }
}

static void run_console_fallback(void)
{
    PadState pad;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    printf("任我玩\n");
    printf("Play Wise. Play More.\n\n");
    printf("图形界面初始化失败。\n");
    printf("请确认系统共享中文字体和 FreeType 运行环境可用。\n\n");
    printf("按 + 退出。\n");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
}

int main(int argc, char **argv)
{
    PtcFsStorage fs;
    UiState ui;
    PadState pad;
    PadRepeater direction_repeater;
    HidTouchScreenState touch;
    bool touch_down = false;
    bool running = true;
    u64 previous_stick_buttons = 0;
    (void)argc;
    (void)argv;

    if (!ptc_ui_graphics_init()) {
        run_console_fallback();
        return 1;
    }
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    padRepeaterInitialize(&direction_repeater, 4, 1);
    hidInitializeTouchScreen();
    srand((unsigned int)time(NULL));

    memset(&ui, 0, sizeof(ui));
    ui.pending_today_action = -1;
    ui.pending_parent_page = -1;
    ui.model.view = PTC_UI_CHILD;
    ui.model.parent_page = PTC_UI_PARENT_TODAY;
    ui.model.remaining_minutes = -1;
    ui.model.play_timer_enabled = -1;
    ui.model.restricted_now = -1;
    ptc_ui_set_execution(&ui.model, NULL, NULL);
    snprintf(ui.model.message, sizeof(ui.model.message), "正在读取今天的游玩状态…");
    ptc_fs_storage_init(&fs);
    ptc_companion_file_client_init(&ui.client, APP_ROOT, ptc_fs_storage_as_storage(&fs));
    load_ui_preferences(&ui);
    refresh_disable_flag(&ui);
    ptc_switch_ipc_client_init(&ui.ipc);
    ptc_companion_transport_init(&ui.transport, APP_ROOT, ptc_fs_storage_as_storage(&fs), ptc_switch_ipc_backend(), &ui.ipc);
    ptc_companion_auth_init(&ui.auth, APP_ROOT, ptc_fs_storage_as_storage(&fs));
    ui.last_setup_refresh_second = -1;
    load_rule_drafts(&ui);
    if (!restore_pending_redemption(&ui)) {
        submit_status(&ui);
    }

    while (appletMainLoop() && running) {
        u64 down;
        u64 held;
        u64 stick_buttons;
        bool parent_combo_held;
        bool custom_combo_held;
        padUpdate(&pad);
        down = padGetButtonsDown(&pad);
        held = padGetButtons(&pad);
        stick_buttons = stick_direction_buttons(padGetStickPos(&pad, 0));
        down |= stick_buttons & ~previous_stick_buttons;
        previous_stick_buttons = stick_buttons;
        held |= stick_buttons;
        padRepeaterUpdate(&direction_repeater, held & DIRECTION_BUTTON_MASK);
        down |= padRepeaterGetButtons(&direction_repeater);
        parent_combo_held = hidden_parent_combo_held(held);
        custom_combo_held = ui.model.view == PTC_UI_CHILD &&
            ui.model.overlay == PTC_UI_OVERLAY_NONE && custom_parent_combo_held(&ui, held);

        if (custom_combo_held) {
            /* Plus is an exit shortcut only when it is used on its own.  As
             * soon as it participates in the configured parent combination,
             * do not let releasing it turn the attempted unlock into an exit. */
            if (ui.model.custom_shortcut_mask & HidNpadButton_Plus) {
                ui.plus_exit_pending = false;
            }
            if (!ui.custom_shortcut_latched) {
                ++ui.custom_shortcut_ticks;
                if (ui.custom_shortcut_ticks >= CUSTOM_SHORTCUT_HOLD_TICKS) {
                    ui.custom_shortcut_latched = true;
                    ui.minus_pending = false;
                    ui.plus_exit_pending = false;
                    enter_parent_area(&ui);
                }
            }
        } else {
            ui.custom_shortcut_ticks = 0;
            ui.custom_shortcut_latched = false;
        }

        if ((ui.model.view == PTC_UI_CHILD || ui.model.view == PTC_UI_SETUP) &&
            ui.model.overlay == PTC_UI_OVERLAY_NONE && parent_combo_held) {
            ++ui.hidden_ticks;
            if (ui.hidden_ticks == HIDDEN_HOLD_TICKS) {
                enter_parent_area(&ui);
            }
        } else {
            ui.hidden_ticks = 0;
        }

        if (ui.model.overlay != PTC_UI_OVERLAY_NONE) {
            if (ui.model.shortcut_capture_active) {
                update_setup_shortcut_capture(&ui, down, held);
            } else if (ui.model.overlay == PTC_UI_OVERLAY_CONFIRM && ui.model.confirm_hold_required) {
                if (down & HidNpadButton_B) {
                    ui.danger_confirm_ticks = 0;
                    handle_overlay_input(&ui, down);
                } else if (held & HidNpadButton_A) {
                    ++ui.danger_confirm_ticks;
                    if (ui.danger_confirm_ticks >= DANGER_CONFIRM_HOLD_TICKS) {
                        ui.danger_confirm_ticks = 0;
                        confirm_operation(&ui);
                    }
                } else {
                    ui.danger_confirm_ticks = 0;
                }
            } else {
                ui.danger_confirm_ticks = 0;
                handle_overlay_input(&ui, down);
            }
        } else if (ui.model.view == PTC_UI_CHILD) {
            if (down & HidNpadButton_B) {
                running = false;
            } else if (down & HidNpadButton_Plus) {
                ui.plus_exit_pending = true;
            } else if (down & HidNpadButton_Minus) {
                ui.minus_pending = true;
            } else if (ui.minus_pending && !(held & HidNpadButton_Minus)) {
                ui.minus_pending = false;
                enter_parent_area(&ui);
            } else if (ui.plus_exit_pending && !(held & HidNpadButton_Plus)) {
                ui.plus_exit_pending = false;
                running = false;
            } else if (down & HidNpadButton_A) {
                if (ui.waiting) {
                    snprintf(ui.model.message, sizeof(ui.model.message), "请等待当前操作完成后再提交加时码。");
                } else {
                    open_offline_code_input(&ui);
                }
            } else if (down & HidNpadButton_Y) {
                submit_status(&ui);
            }
        } else if (ui.model.view == PTC_UI_SETUP) {
            if (down & HidNpadButton_Y) {
                submit_status(&ui);
            } else {
                handle_setup_input(&ui, down, held);
            }
        } else if (ui.model.view == PTC_UI_ERROR) {
            if (down & HidNpadButton_A) {
                retry_error(&ui);
            } else if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                enter_child_area(&ui);
            }
        } else {
            if (ui.model.parent_footer_focused) {
                if (down & HidNpadButton_B) {
                    request_parent_navigation(&ui, -1, true);
                } else if (down & HidNpadButton_L) {
                    request_parent_navigation(&ui,
                        (ui.model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
                } else if (down & HidNpadButton_R) {
                    request_parent_navigation(&ui,
                        (ui.model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
                } else if (down & HidNpadButton_Up) {
                    ui.model.parent_footer_focused = false;
                    ui.model.selected_index = ui.model.parent_content_selection;
                } else if (down & HidNpadButton_Y) {
                    refresh_disable_flag(&ui);
                    submit_status(&ui);
                } else if (down & HidNpadButton_A) {
                    activate_parent_status(&ui);
                }
            } else if (ui.model.parent_page == PTC_UI_PARENT_PLAN) {
                PtcDayRule *day = &ui.model.draft_week[ui.model.editor_index];
                if (ui.waiting) {
                    if (down) {
                        snprintf(ui.model.message, sizeof(ui.model.message),
                                 "请等待周计划保存完成后再继续编辑。");
                    }
                } else if (down & HidNpadButton_B) {
                    request_parent_navigation(&ui, -1, true);
                } else if (down & HidNpadButton_L) {
                    request_parent_navigation(&ui, PTC_UI_PARENT_TODAY, false);
                } else if (down & HidNpadButton_R) {
                    request_parent_navigation(&ui, PTC_UI_PARENT_HOLIDAY, false);
                } else if (down & HidNpadButton_Left) {
                    ptc_ui_move_weekly_focus(&ui.model, -1, 0);
                } else if (down & HidNpadButton_Right) {
                    ptc_ui_move_weekly_focus(&ui.model, 1, 0);
                } else if (down & HidNpadButton_X) {
                    ui.model.selected_index = 1;
                    if (!weekly_editing_blocked(&ui)) {
                        day->mode = ptc_ui_next_rule_mode(day->mode);
                        update_weekly_dirty(&ui);
                    }
                    if (!ui.model.disable_flag_present && day->mode == PTC_RULE_MODE_LIMIT) {
                        snprintf(ui.model.message, sizeof(ui.model.message),
                                 "已恢复此前的每日限额：%u 分钟。", (unsigned int)day->minutes);
                    }
                } else if (down & HidNpadButton_Up) {
                    ptc_ui_move_weekly_focus(&ui.model, 0, -1);
                } else if (down & HidNpadButton_Down) {
                    if (ui.model.selected_index == 2 || ui.model.selected_index == 3) {
                        ui.model.parent_content_selection = ui.model.selected_index;
                        ui.model.parent_footer_focused = true;
                    } else {
                        ptc_ui_move_weekly_focus(&ui.model, 0, 1);
                    }
                } else if (down & HidNpadButton_Y) {
                    request_parent_navigation(&ui, PTC_UI_PARENT_PLAN, false);
                } else if (down & HidNpadButton_A) {
                    if (ui.model.selected_index == 2) {
                        if (ui.model.weekly_dirty) {
                            memcpy(ui.model.draft_week, ui.model.current_week, sizeof(ui.model.draft_week));
                            ui.model.weekly_dirty = false;
                            snprintf(ui.model.message, sizeof(ui.model.message), "已放弃未保存的周计划修改。");
                        } else {
                            snprintf(ui.model.message, sizeof(ui.model.message), "周计划没有修改。");
                        }
                    } else if (weekly_editing_blocked(&ui)) {
                        /* Focus remains movable while emergency stop makes the editor read-only. */
                    } else if (ui.model.selected_index == 1) {
                        day->mode = ptc_ui_next_rule_mode(day->mode);
                        update_weekly_dirty(&ui);
                        if (day->mode == PTC_RULE_MODE_LIMIT) {
                            snprintf(ui.model.message, sizeof(ui.model.message),
                                     "已恢复此前的每日限额：%u 分钟。", (unsigned int)day->minutes);
                        }
                    } else if (ui.model.selected_index == 3) {
                        save_weekly_from_page(&ui);
                    } else if (day->mode == PTC_RULE_MODE_LIMIT) {
                        edit_weekly_minutes(&ui);
                    } else {
                        snprintf(ui.model.message, sizeof(ui.model.message),
                                 "该日为不限时，没有可编辑的分钟数；请选择“切换模式”改为限时。");
                    }
                } else if (down & HidNpadButton_Plus) {
                    ui.model.selected_index = 3;
                    if (!weekly_editing_blocked(&ui)) {
                        save_weekly_from_page(&ui);
                    }
                } else if (down & HidNpadButton_ZL) {
                    ui.model.selected_index = 2;
                    if (ui.model.weekly_dirty) {
                        memcpy(ui.model.draft_week, ui.model.current_week, sizeof(ui.model.draft_week));
                        ui.model.weekly_dirty = false;
                        snprintf(ui.model.message, sizeof(ui.model.message), "已放弃未保存的周计划修改。");
                    } else {
                        snprintf(ui.model.message, sizeof(ui.model.message), "周计划没有修改。");
                    }
                }
            } else if (down & HidNpadButton_B) {
                request_parent_navigation(&ui, -1, true);
            } else if (down & HidNpadButton_L) {
                request_parent_navigation(&ui,
                    (ui.model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
            } else if (down & HidNpadButton_R) {
                request_parent_navigation(&ui,
                    (ui.model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
            } else if (down & HidNpadButton_Left) {
                ptc_ui_move_parent_selection(&ui.model, -1, 0);
            } else if (down & HidNpadButton_Right) {
                ptc_ui_move_parent_selection(&ui.model, 1, 0);
            } else if (down & HidNpadButton_Up) {
                ptc_ui_move_parent_selection(&ui.model, 0, -1);
            } else if (down & HidNpadButton_Down) {
                ptc_ui_move_parent_selection(&ui.model, 0, 1);
            } else if (down & HidNpadButton_Y) {
                refresh_disable_flag(&ui);
                poll_result(&ui, true);
            } else if (down & HidNpadButton_A) {
                if (ui.waiting) {
                    snprintf(ui.model.message, sizeof(ui.model.message), "请等待当前操作完成后再执行其他设置。");
                } else if (ui.model.parent_footer_focused) {
                    activate_parent_status(&ui);
                } else {
                    handle_parent_action(&ui);
                }
            }
        }

        if (hidGetTouchScreenStates(&touch, 1) && touch.count > 0) {
            if (!touch_down) {
                touch_down = true;
                handle_touch(&ui, (int)touch.touches[0].x, (int)touch.touches[0].y);
            }
        } else {
            touch_down = false;
        }

        if (ui.exit_requested) {
            running = false;
        }

        poll_pending_redemption(&ui);
        poll_result(&ui, false);
        refresh_setup_activation(&ui);
        draw(&ui);
        svcSleepThread(LOOP_SLEEP_NS);
    }

    ptc_ui_graphics_exit();
    ptc_switch_ipc_client_exit(&ui.ipc);
    return 0;
}
