#include <switch.h>
#include "release_manifest.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../companion/auth.h"
#include "../../companion/file_protocol.h"
#include "../../companion/transport_client.h"
#include "../../companion/switch_ipc_client.h"
#include "../../platform/switch/fs_storage.h"
#include "../../third_party/cjson/cJSON.h"
#include "../../common/support/support_export.h"
#include "../../common/time/ptc_time.h"
#include "../../common/security/credential_policy.h"
#include "../../common/token/token_v2.h"
#include "../../third_party/qrcodegen/qrcodegen.h"
#include "ui_graphics.h"

#define APP_ROOT "sdmc:/switch/playwise"
#define RULES_PATH APP_ROOT "/rules.json"
#define CONFIG_PATH APP_ROOT "/config.json"
#define CREDENTIALS_PATH APP_ROOT "/credentials.json"
#define ISSUED_NONCES_PATH APP_ROOT "/grant-issued.json"
#define LEDGER_PATH APP_ROOT "/ledger/used_nonces.jsonl"
#define RESULT_TEXT_SIZE 8192
#define REQUEST_TIMEOUT_MS 60000
#define LOOP_SLEEP_NS 100000000LL
#define LOOP_SLEEP_MS 100
#define HIDDEN_HOLD_TICKS 20
#define HIDDEN_LEFT_SHOULDER_MASK (HidNpadButton_L | HidNpadButton_ZL)
#define HIDDEN_RIGHT_SHOULDER_MASK (HidNpadButton_R | HidNpadButton_ZR)
#define STICK_DEADZONE 16000
#define DIRECTION_BUTTON_MASK (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right)

__attribute__((used)) static const char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

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
    bool waiting;
    bool exit_requested;
    PtcUiView request_view;
    int64_t last_setup_refresh_second;
    int pending_today_action;
    int pending_parent_page;
    bool pending_leave_parent;
} UiState;

static void handle_parent_action(UiState *ui);
static void handle_today_action_ready(UiState *ui, int index);
static void refresh_security_state(UiState *ui);
static void update_weekly_dirty(UiState *ui);
static void apply_pending_navigation(UiState *ui);

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
        return "尚未设置 PlayWise PIN";
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

static void set_auth_message(UiState *ui, const char *prefix, PtcAuthStatus status)
{
    ui->model.feedback_detail[0] = '\0';
    snprintf(ui->model.message, sizeof(ui->model.message), "%s：%s", prefix, auth_status_zh(status));
    snprintf(ui->model.result_status, sizeof(ui->model.result_status), "error");
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

static void refresh_disable_flag(UiState *ui)
{
    char path[160];
    if (!ui || !ui->client.storage) {
        return;
    }
    snprintf(path, sizeof(path), "%s/flags/disable.flag", APP_ROOT);
    ui->model.disable_flag_present = ui->client.storage->vtable->exists(ui->client.storage, path);
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
    PtcDayRule *day = &ui->model.draft_week[ui->model.editor_index];
    if (day->mode == PTC_RULE_MODE_LIMIT) {
        char title[64];
        char guide[128];
        snprintf(title, sizeof(title), "修改%s的额度", WEEKDAYS[ui->model.editor_index]);
        snprintf(guide, sizeof(guide), "输入 1 到 1440 分钟\n将影响%s的每日额度", WEEKDAYS[ui->model.editor_index]);
        ptc_ui_numpad_open(
            &ui->model, PTC_UI_NUMPAD_WEEKLY_MINUTES, PTC_UI_OVERLAY_NONE,
            title, guide, 4, 1, 1440, day->minutes);
    }
}

static void open_offline_code_input(UiState *ui)
{
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "紧急停用已开启，当前不能兑换加时码；状态和恢复仍可使用。");
        return;
    }
    ptc_ui_numpad_open(
        &ui->model, PTC_UI_NUMPAD_OFFLINE_CODE, PTC_UI_OVERLAY_NONE,
        "输入离线加时短码", "8 位数字；领码加时前，记得向窗外远眺至少 5 分钟！", 8, 0, 0, 0);
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
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_offline_code(&ui->transport, ui->active_request_id, time(NULL), code);
    set_command_name(ui, "offline_code");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "offline_code", "加时码已提交，正在等待后台确认…");
        return;
    }
    ui->waiting = false;
    set_message(ui, "加时码提交失败", status);
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
    memcpy(ui->model.draft_week, rules.week, sizeof(rules.week));
    memcpy(ui->model.current_week, rules.week, sizeof(rules.week));
    ui->model.weekly_dirty = false;
    cJSON_Delete(root);
}

static void poll_result(UiState *ui, bool force)
{
    PtcCompanionStatus status;
    PtcDayRule saved_draft[7];
    bool preserve_weekly_draft;
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
        if (!ptc_ui_apply_result_json(&ui->model, ui->last_result)) {
            ui->pending_parent_page = -1;
            ui->pending_leave_parent = false;
            set_message(ui, "读取结果失败", PTC_COMPANION_RESULT_INVALID);
            if (ui->request_view == PTC_UI_CHILD) ui->model.view = PTC_UI_ERROR;
            return;
        }
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
        refresh_disable_flag(ui);
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
        if (ui->request_view == PTC_UI_CHILD && strcmp(ui->model.result_status, "error") == 0) {
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

static void enter_parent_area(UiState *ui)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    char pin_confirm[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus state = ptc_companion_auth_state(&ui->auth);
    if (state == PTC_AUTH_EMPTY) {
        if (!keyboard_input("设置 PlayWise PIN", "请输入 1–64 位数字；长度由家长决定", pin, sizeof(pin), true, true, false) ||
            !keyboard_input("确认 PlayWise PIN", "请再次输入相同的数字 PIN", pin_confirm, sizeof(pin_confirm), true, true, false) ||
            strcmp(pin, pin_confirm) != 0) {
            snprintf(ui->model.message, sizeof(ui->model.message), "PIN 设置已取消，或两次输入不一致。");
            return;
        }
        state = ptc_companion_auth_set_pin(&ui->auth, pin, time(NULL), switch_random, NULL);
        if (state != PTC_AUTH_OK) {
            set_auth_message(ui, "PIN 设置失败", state);
            return;
        }
    } else if (state != PTC_AUTH_OK) {
        set_auth_message(ui, "无法进入家长区", state);
        return;
    }
    if (!keyboard_input("PlayWise PIN", "输入本应用独立管理 PIN", pin, sizeof(pin), true, true, false)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消进入家长区。");
        return;
    }
    {
        int64_t retry_after = 0;
        state = ptc_companion_auth_verify_pin(&ui->auth, pin, (int64_t)time(NULL), &retry_after);
        if (state == PTC_AUTH_COOLDOWN && retry_after > 0) {
            snprintf(ui->model.message, sizeof(ui->model.message), "验证失败：请等待 %lld 秒后再试。", (long long)retry_after);
            return;
        }
    }
    if (state != PTC_AUTH_OK) {
        set_auth_message(ui, "验证失败", state);
        return;
    }
    refresh_disable_flag(ui);
    refresh_security_state(ui);
    ui->model.view = PTC_UI_PARENT;
    ui->model.parent_page = ui->model.setup_phase[0] && strcmp(ui->model.setup_phase, "active") != 0
        ? PTC_UI_PARENT_SUPPORT : PTC_UI_PARENT_TODAY;
    ui->model.selected_index = 0;
    snprintf(ui->model.message, sizeof(ui->model.message), "%s",
             strlen(pin) < 4U ? "家长区已解锁；当前 PIN 少于 4 位，很容易被猜到，建议尽快修改。" : "家长区已解锁。");
    if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        submit_status(ui);
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
    ui->model.overlay = PTC_UI_OVERLAY_CONFIRM;
    ui->model.operation = operation;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s", title);
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s", body);
}

static void open_weekly_page(UiState *ui)
{
    load_rule_drafts(ui);
    ui->model.editor_index = 0;
    ui->model.parent_page = PTC_UI_PARENT_PLAN;
    snprintf(ui->model.message, sizeof(ui->model.message), "直接调整七日计划，完成后选择“保存计划”。");
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
    ui->model.grant_has_code = false;
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
    if (!keyboard_input("验证 PlayWise 管理 PIN", action, pin, sizeof(pin), true, true, false)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消敏感操作。");
        return false;
    }
    status = ptc_companion_auth_verify_pin(&ui->auth, pin, (int64_t)time(NULL), &retry_after);
    if (status != PTC_AUTH_OK) {
        if (status == PTC_AUTH_COOLDOWN && retry_after > 0) {
            snprintf(ui->model.message, sizeof(ui->model.message), "验证失败：请等待 %lld 秒后再试。", (long long)retry_after);
            return false;
        }
        set_auth_message(ui, "验证失败", status);
        return false;
    }
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
    ui->model.overlay = PTC_UI_OVERLAY_NONE;
    refresh_security_state(ui);
    if (ui->model.credential_kind == 1) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "设备名已更新；旧设备名生成的加时码不可用，请重新生成配对二维码。");
    } else {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "加时码密钥已更新；旧网页配置和此前生成的加时码已失效，请重新配对。");
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
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s", kind == 1 ? "管理设备名" : "管理加时码密钥");
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
    if (!keyboard_input("修改 PlayWise PIN", "请输入新的 1–64 位数字", pin, sizeof(pin), true, true, false) ||
        !keyboard_input("确认新 PIN", "请再次输入相同的数字 PIN", confirm, sizeof(confirm), true, true, false) ||
        strcmp(pin, confirm) != 0) {
        snprintf(ui->model.message, sizeof(ui->model.message), "PIN 修改已取消，或两次输入不一致。");
        return;
    }
    status = ptc_companion_auth_set_pin(&ui->auth, pin, time(NULL), switch_random, NULL);
    if (status == PTC_AUTH_OK) snprintf(ui->model.message, sizeof(ui->model.message), "%s",
        strlen(pin) < 4U ? "PlayWise PIN 已更新；当前 PIN 少于 4 位，冷却也无法提供可靠保护。" : "PlayWise PIN 已更新。");
    else set_auth_message(ui, "PIN 修改失败", status);
}

static void open_grant_setup(UiState *ui)
{
    uint16_t maximum = PTC_TOKEN_V2_MAX_MINUTES;
    if (!read_pairing_config(ui, ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), &maximum)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "读取本机生成器配置失败。");
        return;
    }
    ui->model.overlay = PTC_UI_OVERLAY_GRANT_SETUP;
    ui->model.grant_max_minutes = maximum;
    ui->model.grant_minutes = legal_grant_minutes(20U, maximum);
    ui->model.grant_has_code = false;
    ui->model.grant_code[0] = '\0';
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "生成加时码");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "本机生成今天有效的一次性 8 位码，也可继续使用网页配对。");
}

static void edit_pairing_base_url(UiState *ui)
{
    char value[PTC_PAIRING_BASE_URL_MAX_LEN + 1];
    if (!verify_sensitive_pin(ui, "修改二维码跳转地址前，请再次输入本应用 PIN")) return;
    if (!keyboard_input("二维码跳转地址", "仅使用可信 HTTPS；局域网调试可用 localhost 或私有 IP",
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
    snprintf(ui->model.message, sizeof(ui->model.message), "二维码跳转地址已更新；自定义站点可读取配对密钥，请仅使用可信站点。");
}

static void reset_pairing_base_url(UiState *ui)
{
    if (!verify_sensitive_pin(ui, "恢复官方二维码地址前，请再次输入本应用 PIN")) return;
    if (!save_pairing_base_url(ui, PTC_PAIRING_BASE_URL)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "恢复官方二维码地址失败。");
        return;
    }
    snprintf(ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), "%s", PTC_PAIRING_BASE_URL);
    snprintf(ui->model.message, sizeof(ui->model.message), "已恢复官方二维码跳转地址。");
}

static void generate_local_grant_code(UiState *ui)
{
    bool consumed[PTC_TOKEN_V2_MAX_NONCE + 1U] = {false};
    bool issued[PTC_TOKEN_V2_MAX_NONCE + 1U] = {false};
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    uint16_t start;
    uint16_t nonce = 0;
    uint8_t tier;
    unsigned int offset;
    bool found = false;
    if (!ui->model.status_loaded) {
        snprintf(ui->model.message, sizeof(ui->model.message), "无法确认设备日期，请先关闭弹层并刷新设备状态。");
        return;
    }
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
    start &= PTC_TOKEN_V2_MAX_NONCE;
    for (offset = 0; offset <= PTC_TOKEN_V2_MAX_NONCE; ++offset) {
        nonce = (uint16_t)((start + offset) & PTC_TOKEN_V2_MAX_NONCE);
        if (!consumed[nonce] && !issued[nonce]) {
            found = true;
            break;
        }
    }
    if (!found) {
        if (!verify_sensitive_pin(ui, "当天签发编号已用尽；再次验证将只清除未兑换签发记录")) return;
        memset(issued, 0, sizeof(issued));
        for (nonce = 0; nonce <= PTC_TOKEN_V2_MAX_NONCE; ++nonce) {
            if (!consumed[nonce]) {
                found = true;
                break;
            }
        }
    }
    if (!found) {
        snprintf(ui->model.message, sizeof(ui->model.message), "今天的 512 个加时码编号均已消费，无法继续生成。");
        return;
    }
    if (ptc_token_v2_encode(tier, nonce, device, secret, ui->model.day_index, ui->model.grant_code) != PTC_ERR_OK) {
        snprintf(ui->model.message, sizeof(ui->model.message), "本机生成加时码失败。");
        return;
    }
    issued[nonce] = true;
    if (!save_issued_nonces(ui, ui->model.day_index, issued)) {
        ui->model.grant_code[0] = '\0';
        snprintf(ui->model.message, sizeof(ui->model.message), "保存已签发记录失败，本次加时码未显示。");
        return;
    }
    ui->model.grant_day_index = ui->model.day_index;
    ui->model.grant_has_code = true;
    snprintf(ui->model.message, sizeof(ui->model.message), "已在本机生成今天有效的 %u 分钟加时码。",
             (unsigned int)ui->model.grant_minutes);
}

static void show_pairing_qr(UiState *ui)
{
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    uint16_t maximum;
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
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "扫描二维码生成加时码");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "二维码包含设备名和密钥，只允许家长扫描；关闭后即不再显示。");
}

static void export_parent_import(UiState *ui)
{
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    cJSON *root;
    char *json;
    bool ok;
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
    ui->model.overlay = PTC_UI_OVERLAY_NONE;
    snprintf(ui->model.message, sizeof(ui->model.message), "%s",
             ok ? "已导出到 sdmc:/switch/playwise/parent-import.json；把文件导入家长网页。文件包含密钥，请仅交给家长。"
                : "生成家长网页导入文件失败。");
}

static void export_diagnostics(UiState *ui)
{
    cJSON *bundle = cJSON_CreateObject();
    char path[192];
    char text[4096];
    char output_path[192];
    char *rendered;
    size_t i;
    bool rejected_sensitive_file = false;
    if (!bundle) return;
    cJSON_AddNumberToObject(bundle, "version", 1);
    cJSON_AddStringToObject(bundle, "redaction", "credentials-auth-codes-and-nonces-omitted");
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
    snprintf(output_path, sizeof(output_path), APP_ROOT "/support/diagnostic-%lld.json", (long long)time(NULL));
    if (rendered && ui->client.storage->vtable->write_text_atomic(ui->client.storage, output_path, rendered)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "脱敏诊断包已生成到 support 目录。");
    } else {
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
    if (ui->model.parent_page == PTC_UI_PARENT_SECURITY) {
        switch (index) {
        case 0: open_credential_manager(ui, 1); break;
        case 1: open_credential_manager(ui, 2); break;
        case 2: open_grant_setup(ui); break;
        case 3: change_parent_pin(ui); break;
        default: break;
        }
        return;
    }
    switch (index) {
    case 0:
        open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "确认接管系统控制",
                             "先执行只读兼容预检；通过后保存安装快照并启用额度管理。");
        break;
    case 1:
        open_confirm_overlay(ui, PTC_UI_OPERATION_RETRY_SETUP_RELEASE, "重试修复",
                             "重新执行安全前置检查，并在可恢复时继续首次设置。");
        break;
    case 2:
        refresh_disable_flag(ui);
        if (ui->model.disable_flag_present) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_RESUME_CONTROL, "解除紧急停用",
                                 "功能：删除 disable.flag，恢复后台正常控制。\n适用：故障已排除且确认当前规则配置安全。");
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
    default:
        break;
    }
}

static void confirm_operation(UiState *ui)
{
    PtcCompanionStatus status;
    PtcUiOperation operation = ptc_ui_take_confirmed_operation(&ui->model);
    switch (operation) {
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
    case PTC_UI_OPERATION_SAVE_CREDENTIAL:
        if (!commit_credential(ui)) snprintf(ui->model.message, sizeof(ui->model.message), "保存加时码密钥失败。");
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
    if (!ptc_ui_numpad_validate(&ui->model, &value)) {
        return;
    }
    if (purpose == PTC_UI_NUMPAD_OFFLINE_CODE) {
        snprintf(code, sizeof(code), "%s", ui->model.numpad_text);
        ptc_ui_numpad_finish(&ui->model);
        submit_offline_code(ui, code);
        return;
    }
    if (purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES) {
        ui->model.draft_week[ui->model.editor_index].minutes = value;
        ui->model.weekly_dirty = memcmp(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week)) != 0;
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

static void save_weekly_from_page(UiState *ui)
{
    uint8_t weekday;
    PtcDayRule before;
    PtcDayRule after;
    char body[192];
    if (!ui->model.weekly_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), "周计划没有修改。");
        return;
    }
    weekday = ptc_weekday_from_day_index(ui->model.day_index);
    before = ui->model.current_week[weekday];
    after = ui->model.draft_week[weekday];
    if (!ui->model.today_override_present &&
        (before.mode != after.mode || before.minutes != after.minutes)) {
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
            }
        }
    }
    ui->pending_parent_page = -1;
    ui->pending_leave_parent = false;
}

static void request_parent_navigation(UiState *ui, int target_page, bool leave_parent)
{
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN && ui->model.weekly_dirty) {
        ui->pending_parent_page = target_page;
        ui->pending_leave_parent = leave_parent;
        ui->model.overlay = PTC_UI_OVERLAY_WEEKLY_LEAVE;
        snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "离开周计划？");
        snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                 "请选择保存修改、放弃修改，或返回继续编辑。");
        return;
    }
    ui->pending_parent_page = target_page;
    ui->pending_leave_parent = leave_parent;
    apply_pending_navigation(ui);
}

static void handle_overlay_input(UiState *ui, u64 down)
{
    if (ui->model.overlay == PTC_UI_OVERLAY_WEEKLY_LEAVE) {
        if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & HidNpadButton_X) {
            memcpy(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week));
            ui->model.weekly_dirty = false;
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            apply_pending_navigation(ui);
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            submit_weekly(ui);
            if (!ui->waiting) {
                ui->pending_parent_page = -1;
                ui->pending_leave_parent = false;
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL) {
        if (down & HidNpadButton_B) {
            ptc_ui_cancel_overlay(&ui->model);
        } else if (down & HidNpadButton_X) {
            if (ui->model.credential_kind == 1 || ui->model.credential_revealed ||
                verify_sensitive_pin(ui, "显示当前加时码密钥前，请再次输入本应用 PIN")) {
                ui->model.credential_revealed = !ui->model.credential_revealed;
                ui->model.credential_new_revealed = ui->model.credential_revealed;
            }
        } else if (down & HidNpadButton_Y) {
            randomize_credential(ui);
        } else if ((down & HidNpadButton_R) && ui->model.credential_kind == 2) {
            if (ptc_grant_secret_is_demo(ui->model.credential_current)) randomize_credential(ui);
            else snprintf(ui->model.credential_new, sizeof(ui->model.credential_new), "%s", PTC_DEMO_GRANT_SECRET);
            ui->model.credential_new_revealed = true;
        } else if (down & HidNpadButton_A) {
            edit_credential_input(ui);
        } else if (down & HidNpadButton_Plus) {
            request_save_credential(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_SETUP) {
        if (down & HidNpadButton_B) ptc_ui_cancel_overlay(&ui->model);
        else if (down & HidNpadButton_A) show_pairing_qr(ui);
        else if (down & HidNpadButton_Y) export_parent_import(ui);
        else if (down & HidNpadButton_X) generate_local_grant_code(ui);
        else if (down & HidNpadButton_R) edit_pairing_base_url(ui);
        else if (down & HidNpadButton_ZR) reset_pairing_base_url(ui);
        else if (down & HidNpadButton_Left) adjust_grant_minutes(ui, -1);
        else if (down & HidNpadButton_Right) adjust_grant_minutes(ui, 1);
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_QR) {
        if (down & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus)) ptc_ui_cancel_overlay(&ui->model);
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
                    snprintf(body, sizeof(body),
                             "今天当前为不限时；设置 %u 分钟后将恢复限时。\n实际剩余将在设置生效后刷新。",
                             (unsigned int)ui->model.draft_minutes);
                    open_confirm_overlay(ui, operation, "不限时将改为限时", body);
                } else {
                    snprintf(body, sizeof(body),
                             "今天已玩约 %d 分钟；设置总额度 %u 分钟。\n调整后可能立即没有可玩时间。",
                             ui->model.played_minutes, (unsigned int)ui->model.draft_minutes);
                    open_confirm_overlay(ui, operation, "额度低于已玩时间", body);
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
            if (ptc_ui_day_rule_would_restrict(&ui->model, today)) {
                char body[192];
                snprintf(body, sizeof(body),
                         "已用约 %d 分钟；今天设置 %u 分钟。页面可能立即受限。",
                         ui->model.played_minutes, (unsigned int)today.minutes);
                open_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_WEEKLY, "每周计划可能立即生效", body);
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
    case PTC_UI_HIT_CHILD_EXIT:
        ui->exit_requested = true;
        break;
    case PTC_UI_HIT_ERROR_RETRY:
        ui->model.view = PTC_UI_CHILD;
        open_offline_code_input(ui);
        break;
    case PTC_UI_HIT_ERROR_BACK:
        enter_child_area(ui);
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
        poll_result(ui, true);
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
        ptc_ui_cancel_overlay(&ui->model);
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消修改。");
        break;
    case PTC_UI_HIT_OVERLAY_CONFIRM:
        handle_overlay_input(ui,
            ui->model.overlay == PTC_UI_OVERLAY_NUMPAD || ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL
                ? HidNpadButton_Plus : HidNpadButton_A);
        break;
    case PTC_UI_HIT_OVERLAY_DISCARD:
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
        break;
    case PTC_UI_HIT_WEEKLY_MODE:
        ui->model.draft_week[ui->model.editor_index].mode =
            ptc_ui_next_rule_mode(ui->model.draft_week[ui->model.editor_index].mode);
        update_weekly_dirty(ui);
        break;
    case PTC_UI_HIT_WEEKLY_MIN_UP:
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 15, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DOWN:
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -15, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DEC:
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -5, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_INC:
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
        edit_weekly_minutes(ui);
        break;
    case PTC_UI_HIT_WEEKLY_SAVE:
        save_weekly_from_page(ui);
        break;
    case PTC_UI_HIT_WEEKLY_DISCARD:
        memcpy(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week));
        ui->model.weekly_dirty = false;
        snprintf(ui->model.message, sizeof(ui->model.message), "已放弃未保存的周计划修改。");
        break;
    case PTC_UI_HIT_CREDENTIAL_INPUT:
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_CREDENTIAL_RANDOM:
        handle_overlay_input(ui, HidNpadButton_Y);
        break;
    case PTC_UI_HIT_CREDENTIAL_REVEAL:
        handle_overlay_input(ui, HidNpadButton_X);
        break;
    case PTC_UI_HIT_CREDENTIAL_DEMO:
        handle_overlay_input(ui, HidNpadButton_R);
        break;
    case PTC_UI_HIT_GRANT_QR:
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_GRANT_EXPORT:
        handle_overlay_input(ui, HidNpadButton_Y);
        break;
    case PTC_UI_HIT_GRANT_GENERATE:
        handle_overlay_input(ui, HidNpadButton_X);
        break;
    case PTC_UI_HIT_GRANT_EDIT_URL:
        handle_overlay_input(ui, HidNpadButton_R);
        break;
    case PTC_UI_HIT_GRANT_RESET_URL:
        handle_overlay_input(ui, HidNpadButton_ZR);
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
    ui->model.waiting = ui->waiting;
    snprintf(ui->model.request_id, sizeof(ui->model.request_id), "%s", ui->active_request_id);
    ptc_ui_graphics_draw(&ui->model);
}

static void run_console_fallback(void)
{
    PadState pad;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    printf("任你玩 · PlayWise\n");
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
    refresh_disable_flag(&ui);
    ptc_switch_ipc_client_init(&ui.ipc);
    ptc_companion_transport_init(&ui.transport, APP_ROOT, ptc_fs_storage_as_storage(&fs), ptc_switch_ipc_backend(), &ui.ipc);
    ptc_companion_auth_init(&ui.auth, APP_ROOT, ptc_fs_storage_as_storage(&fs));
    ui.last_setup_refresh_second = -1;
    load_rule_drafts(&ui);
    submit_status(&ui);

    while (appletMainLoop() && running) {
        u64 down;
        u64 held;
        u64 stick_buttons;
        bool parent_combo_held;
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
            handle_overlay_input(&ui, down);
        } else if (ui.model.view == PTC_UI_CHILD) {
            if (down & (HidNpadButton_Plus | HidNpadButton_B)) {
                running = false;
            } else if (down & HidNpadButton_Minus) {
                enter_parent_area(&ui);
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
            if (down & (HidNpadButton_Plus | HidNpadButton_B)) {
                running = false;
            } else if (down & HidNpadButton_Minus) {
                enter_parent_area(&ui);
            } else if (down & HidNpadButton_Y) {
                submit_status(&ui);
            }
        } else if (ui.model.view == PTC_UI_ERROR) {
            if (down & HidNpadButton_A) {
                ui.model.view = PTC_UI_CHILD;
                open_offline_code_input(&ui);
            } else if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                enter_child_area(&ui);
            }
        } else {
            if (ui.model.parent_page == PTC_UI_PARENT_PLAN) {
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
                    request_parent_navigation(&ui, PTC_UI_PARENT_SECURITY, false);
                } else if (down & HidNpadButton_Left) {
                    ui.model.editor_index = ui.model.editor_index <= 0 ? 6 : ui.model.editor_index - 1;
                } else if (down & HidNpadButton_Right) {
                    ui.model.editor_index = ui.model.editor_index >= 6 ? 0 : ui.model.editor_index + 1;
                } else if (down & HidNpadButton_X) {
                    day->mode = ptc_ui_next_rule_mode(day->mode);
                    update_weekly_dirty(&ui);
                } else if ((down & HidNpadButton_Up) && day->mode == PTC_RULE_MODE_LIMIT) {
                    day->minutes = ptc_ui_adjust_minutes(day->minutes, 15, 1, 1440);
                    update_weekly_dirty(&ui);
                } else if ((down & HidNpadButton_Down) && day->mode == PTC_RULE_MODE_LIMIT) {
                    day->minutes = ptc_ui_adjust_minutes(day->minutes, -15, 1, 1440);
                    update_weekly_dirty(&ui);
                } else if ((down & HidNpadButton_Y) && day->mode == PTC_RULE_MODE_LIMIT) {
                    edit_weekly_minutes(&ui);
                } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
                    save_weekly_from_page(&ui);
                } else if (down & HidNpadButton_ZL) {
                    memcpy(ui.model.draft_week, ui.model.current_week, sizeof(ui.model.draft_week));
                    ui.model.weekly_dirty = false;
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

        poll_result(&ui, false);
        refresh_setup_activation(&ui);
        draw(&ui);
        svcSleepThread(LOOP_SLEEP_NS);
    }

    ptc_ui_graphics_exit();
    ptc_switch_ipc_client_exit(&ui.ipc);
    return 0;
}
