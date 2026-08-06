#include <switch.h>

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
#include "../../common/time/ptc_time.h"
#include "ui_graphics.h"

#define APP_ROOT "sdmc:/switch/playwise"
#define RULES_PATH APP_ROOT "/rules.json"
#define CONFIG_PATH APP_ROOT "/config.json"
#define RESULT_TEXT_SIZE 8192
#define REQUEST_TIMEOUT_MS 60000
#define LOOP_SLEEP_NS 100000000LL
#define LOOP_SLEEP_MS 100
#define HIDDEN_HOLD_TICKS 20
#define HIDDEN_LEFT_SHOULDER_MASK (HidNpadButton_L | HidNpadButton_ZL)
#define HIDDEN_RIGHT_SHOULDER_MASK (HidNpadButton_R | HidNpadButton_ZR)
#define STICK_DEADZONE 16000
#define DIRECTION_BUTTON_MASK (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right)

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
    PtcBedtimeRule active_bedtime_rule;
    int64_t last_bedtime_minute;
    int64_t last_setup_refresh_second;
} UiState;

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
        return "尚未设置家长 PIN";
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
    size_t index;
    (void)ctx;
    for (index = 0; index < out_size; ++index) {
        out[index] = (uint8_t)(rand() & 0xff);
    }
    return true;
}

static bool keyboard_input(
    const char *header,
    const char *guide,
    char *out,
    size_t out_size,
    bool password,
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
    PtcDayRule *day = &ui->model.draft_week[ui->model.editor_index];
    if (day->mode == PTC_RULE_MODE_LIMIT) {
        ptc_ui_numpad_open(
            &ui->model, PTC_UI_NUMPAD_MINUTES, PTC_UI_OVERLAY_WEEKLY,
            "输入每日额度", "输入 1 到 1440 分钟", 4, 1, 1440, day->minutes);
    }
}

static void edit_bedtime_minutes(UiState *ui)
{
    int target_index = ui->model.editor_index == 2 ? 2 : 1;
    uint16_t current_min = target_index == 1 ? ui->model.draft_bedtime.start_min : ui->model.draft_bedtime.end_min;
    const char *header = target_index == 1 ? "输入就寝开始时间" : "输入就寝结束时间";
    ptc_ui_numpad_open(
        &ui->model, PTC_UI_NUMPAD_BEDTIME, PTC_UI_OVERLAY_BEDTIME,
        header, "输入 4 位 24 小时时间，例如 2130", 4, 0, 1439, current_min);
}

static void open_offline_code_input(UiState *ui)
{
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
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_status(&ui->transport, ui->active_request_id, time(NULL));
    set_command_name(ui, "status");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "status", "正在刷新今天的状态…");
    else set_message(ui, "刷新失败", status);
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
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    if (operation == PTC_UI_OPERATION_SET_TODAY_LIMIT) {
        type = "set_today_limit";
        status = ptc_companion_transport_submit_set_today_limit(&ui->transport, ui->active_request_id, time(NULL), minutes);
    } else if (operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        type = "add_today_minutes";
        status = ptc_companion_transport_submit_add_today_minutes(&ui->transport, ui->active_request_id, time(NULL), minutes);
    } else {
        type = "parent_unlock_start";
        status = ptc_companion_transport_submit_parent_unlock_start(&ui->transport, ui->active_request_id, time(NULL), minutes);
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

static void submit_bedtime(UiState *ui)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_bedtime(
        &ui->transport,
        ui->active_request_id,
        time(NULL),
        &ui->model.draft_bedtime);
    set_command_name(ui, "set_bedtime");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "set_bedtime", "就寝时间已提交，正在等待后台确认…");
    } else {
        ui->waiting = false;
        set_message(ui, "就寝时间提交失败", status);
    }
}

static void submit_limit_action(UiState *ui)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_limit_action(
        &ui->transport,
        ui->active_request_id,
        time(NULL),
        ui->model.draft_limit_action);
    set_command_name(ui, "set_limit_action");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "set_limit_action", "限制方式已提交，正在等待后台确认…");
    } else {
        ui->waiting = false;
        set_message(ui, "限制方式提交失败", status);
    }
}

static PtcRuleMode parse_rule_mode(const char *mode)
{
    if (mode && strcmp(mode, "unlimited") == 0) {
        return PTC_RULE_MODE_UNLIMITED;
    }
    if (mode && strcmp(mode, "blocked") == 0) {
        return PTC_RULE_MODE_BLOCKED;
    }
    return PTC_RULE_MODE_LIMIT;
}

static PtcLimitAction parse_limit_action(const char *action)
{
    if (action && strcmp(action, "raw_block") == 0) {
        return PTC_LIMIT_ACTION_RAW_BLOCK;
    }
    if (action && strcmp(action, "suspend") == 0) {
        return PTC_LIMIT_ACTION_SUSPEND;
    }
    return PTC_LIMIT_ACTION_REMIND;
}

static const char *limit_action_label(PtcLimitAction action)
{
    switch (action) {
    case PTC_LIMIT_ACTION_RAW_BLOCK:
        return "强制阻止";
    case PTC_LIMIT_ACTION_SUSPEND:
        return "暂停软件";
    case PTC_LIMIT_ACTION_REMIND:
    default:
        return "仅提醒";
    }
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

static uint16_t valid_minute_of_day(int value, uint16_t fallback)
{
    return value >= 0 && value < 1440 ? (uint16_t)value : fallback;
}

static uint16_t current_minute_of_day_utc8(void)
{
    int64_t minute = ((int64_t)time(NULL) / 60 + 8 * 60) % 1440;
    return (uint16_t)(minute < 0 ? minute + 1440 : minute);
}

static void load_rule_drafts(UiState *ui)
{
    PtcRules rules;
    char text[RESULT_TEXT_SIZE];
    cJSON *root;
    const cJSON *week;
    const cJSON *bedtime_enabled;
    const cJSON *version;
    unsigned int index;
    ptc_rules_default(&rules);
    memcpy(ui->model.draft_week, rules.week, sizeof(rules.week));
    ui->model.draft_bedtime = rules.bedtime;
    ui->model.current_limit_action = rules.limit_action;
    ui->model.draft_limit_action = rules.limit_action;
    ui->model.current_limit_action_loaded = true;
    ui->active_bedtime_rule = rules.bedtime;
    if (!ui->client.storage->vtable->read_text(ui->client.storage, RULES_PATH, text, sizeof(text))) {
        return;
    }
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        ui->model.current_limit_action_loaded = false;
        cJSON_Delete(root);
        return;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valueint != 1) {
        ui->model.current_limit_action_loaded = false;
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
    bedtime_enabled = cJSON_GetObjectItemCaseSensitive(root, "bedtime_enabled");
    if (cJSON_IsBool(bedtime_enabled)) {
        rules.bedtime.enabled = cJSON_IsTrue(bedtime_enabled);
    }
    rules.bedtime.start_min = valid_minute_of_day(
        rule_json_int(root, "bedtime_start_min", rules.bedtime.start_min),
        rules.bedtime.start_min);
    rules.bedtime.end_min = valid_minute_of_day(
        rule_json_int(root, "bedtime_end_min", rules.bedtime.end_min),
        rules.bedtime.end_min);
    rules.limit_action = parse_limit_action(rule_json_string(root, "limit_action"));
    memcpy(ui->model.draft_week, rules.week, sizeof(rules.week));
    ui->model.draft_bedtime = rules.bedtime;
    ui->model.current_limit_action = rules.limit_action;
    ui->model.draft_limit_action = rules.limit_action;
    ui->model.current_limit_action_loaded = true;
    ui->active_bedtime_rule = rules.bedtime;
    cJSON_Delete(root);
}

static void refresh_bedtime_state(UiState *ui, bool force)
{
    int64_t now = (int64_t)time(NULL);
    int64_t minute = now / 60;
    if (!force && minute == ui->last_bedtime_minute) {
        return;
    }
    ui->model.bedtime_active = ui->active_bedtime_rule.enabled && ptc_bedtime_active(
        ptc_minute_of_day_from_unix_utc8(now),
        ui->active_bedtime_rule.start_min,
        ui->active_bedtime_rule.end_min);
    ui->last_bedtime_minute = minute;
}

static void poll_result(UiState *ui, bool force)
{
    PtcCompanionStatus status;
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
        if (!ptc_ui_apply_result_json(&ui->model, ui->last_result)) {
            set_message(ui, "读取结果失败", PTC_COMPANION_RESULT_INVALID);
            if (ui->request_view == PTC_UI_CHILD) ui->model.view = PTC_UI_ERROR;
            return;
        }
        load_rule_drafts(ui);
        refresh_disable_flag(ui);
        refresh_bedtime_state(ui, true);
        if (ui->request_view == PTC_UI_CHILD && strcmp(ui->model.result_status, "error") == 0) {
            ui->model.view = PTC_UI_ERROR;
        }
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

static void enter_parent_area(UiState *ui)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    char pin_confirm[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus state = ptc_companion_auth_state(&ui->auth);
    if (state == PTC_AUTH_EMPTY) {
        if (!keyboard_input("设置家长 PIN", "输入 1 到 32 个字符", pin, sizeof(pin), true, false) ||
            !keyboard_input("确认家长 PIN", "请再次输入相同 PIN", pin_confirm, sizeof(pin_confirm), true, false) ||
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
    if (!keyboard_input("家长 PIN", "输入本地管理 PIN", pin, sizeof(pin), true, false)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消进入家长区。");
        return;
    }
    state = ptc_companion_auth_verify_pin(&ui->auth, pin);
    if (state != PTC_AUTH_OK) {
        set_auth_message(ui, "验证失败", state);
        return;
    }
    refresh_disable_flag(ui);
    ui->model.view = PTC_UI_PARENT;
    ui->model.parent_page = ui->model.setup_phase[0] && strcmp(ui->model.setup_phase, "active") != 0
        ? PTC_UI_PARENT_SAFETY : PTC_UI_PARENT_TODAY;
    ui->model.selected_index = 0;
    snprintf(ui->model.message, sizeof(ui->model.message), "家长区已解锁。");
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

static void open_weekly_overlay(UiState *ui)
{
    load_rule_drafts(ui);
    ui->model.overlay = PTC_UI_OVERLAY_WEEKLY;
    ui->model.editor_index = 0;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "编辑每周计划");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "设置每一天的游玩模式与分钟数。");
}

static void open_bedtime_overlay(UiState *ui)
{
    load_rule_drafts(ui);
    ui->model.overlay = PTC_UI_OVERLAY_BEDTIME;
    ui->model.editor_index = 0;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "设置就寝时间");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "可设置跨越午夜的不可游玩时段。");
}

static void open_limit_action_overlay(UiState *ui)
{
    load_rule_drafts(ui);
    ui->model.overlay = PTC_UI_OVERLAY_LIMIT_ACTION;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "选择限制方式");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "未验证的强控制方式仍会被后台安全门禁拒绝。");
}

static void edit_grant_secret(UiState *ui)
{
    char input_buf[128];
    char config_text[4096];
    cJSON *root;
    char *out_json;
    char *trimmed;
    size_t len;
    const char *new_secret;

    if (!keyboard_input("修改加时码密钥", "输入 8 到 64 位加时码密钥 (留空恢复默认)", input_buf, sizeof(input_buf), false, false)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消修改加时码密钥。");
        return;
    }

    trimmed = input_buf;
    while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\r' || *trimmed == '\n') trimmed++;
    len = strlen(trimmed);
    while (len > 0 && (trimmed[len - 1] == ' ' || trimmed[len - 1] == '\t' || trimmed[len - 1] == '\r' || trimmed[len - 1] == '\n')) {
        trimmed[--len] = '\0';
    }

    new_secret = (len == 0) ? "replace-with-long-random-secret" : trimmed;

    if (!ui->client.storage->vtable->read_text(ui->client.storage, CONFIG_PATH, config_text, sizeof(config_text))) {
        snprintf(ui->model.message, sizeof(ui->model.message), "读取 config.json 失败。");
        return;
    }

    root = cJSON_Parse(config_text);
    if (!cJSON_IsObject(root)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "解析 config.json 失败。");
        cJSON_Delete(root);
        return;
    }

    cJSON_DeleteItemFromObject(root, "grant_secret");
    cJSON_AddStringToObject(root, "grant_secret", new_secret);

    out_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!out_json) {
        snprintf(ui->model.message, sizeof(ui->model.message), "生成 config.json 失败。");
        return;
    }

    if (ui->client.storage->vtable->write_text_atomic(ui->client.storage, CONFIG_PATH, out_json)) {
        if (len == 0) {
            snprintf(ui->model.message, sizeof(ui->model.message), "加时码密钥已重置为默认值。");
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message), "加时码密钥已成功更新。");
        }
    } else {
        snprintf(ui->model.message, sizeof(ui->model.message), "写入 config.json 失败。");
    }
    free(out_json);
}

static void handle_parent_action(UiState *ui)
{
    int index = ui->model.selected_index;
    if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        switch (index) {
        case 0:
            submit_status(ui);
            break;
        case 1:
            open_minutes_overlay(ui, PTC_UI_OPERATION_SET_TODAY_LIMIT, "设置今日额度", "选择今天最多可玩的时间。", 60, 1, 1440);
            break;
        case 2:
            open_minutes_overlay(ui, PTC_UI_OPERATION_ADD_TODAY_MINUTES, "临时加时", "在今天现有额度上增加时间。", 15, 1, 120);
            break;
        case 3:
            open_confirm_overlay(ui, PTC_UI_OPERATION_DISABLE_TODAY_LIMIT, "解除当前限制",
                                 "立即解除当前限制并将今天设为不限时；明天继续使用每周计划。");
            break;
        case 4:
            {
                char body[192];
                if (ui->model.played_minutes_available) {
                    snprintf(body, sizeof(body),
                             "已用约 %d 分钟；设置为禁玩。页面可能立即受限。",
                             ui->model.played_minutes);
                } else {
                    snprintf(body, sizeof(body),
                             "已用时间暂不可用；设置为禁玩。页面可能立即受限。");
                }
                open_confirm_overlay(ui, PTC_UI_OPERATION_BLOCK_TODAY, "确认今日禁玩", body);
            }
            break;
        case 5:
            submit_transport_empty(ui, "restore_today_policy", "正在恢复每周计划…", "恢复计划失败");
            break;
        default:
            break;
        }
        return;
    }
    if (ui->model.parent_page == PTC_UI_PARENT_PLAN) {
        switch (index) {
        case 0:
            open_weekly_overlay(ui);
            break;
        case 1:
            open_bedtime_overlay(ui);
            break;
        case 2:
            open_limit_action_overlay(ui);
            break;
        case 3:
            open_minutes_overlay(ui, PTC_UI_OPERATION_PARENT_UNLOCK, "临时解锁", "选择暂停本地规则的时长。", 15, 1, 1440);
            break;
        case 4:
            submit_transport_empty(ui, "parent_unlock_end", "正在结束临时解锁…", "结束解锁失败");
            break;
        default:
            break;
        }
        return;
    }
    switch (index) {
    case 0:
        open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "启用自动控制",
                             "功能：完成首次设置，宽限后按当前规则自动控制。\n适用：前置解限成功且 phase=released。");
        break;
    case 1:
        open_confirm_overlay(ui, PTC_UI_OPERATION_RETRY_SETUP_RELEASE, "重试前置解限",
                             "功能：保留安装快照并重新解除当前限制。\n适用：首次解限等待过久或失败（pending/failed）。");
        break;
    case 2:
        open_confirm_overlay(ui, PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT, "恢复安装前状态",
                             "功能：恢复安装前官方设置与计时器，并停用 PlayWise。\n适用：卸载回退或多次恢复失败。");
        break;
    case 3:
        refresh_disable_flag(ui);
        if (ui->model.disable_flag_present) {
            open_confirm_overlay(ui, PTC_UI_OPERATION_RESUME_CONTROL, "解除紧急停用",
                                 "功能：删除 disable.flag，恢复后台正常控制。\n适用：故障已排除且确认当前规则配置安全。");
        } else {
            open_confirm_overlay(ui, PTC_UI_OPERATION_EMERGENCY_DISABLE, "紧急停用控制",
                                 "功能：创建 disable.flag，立即停止正常控制写入。\n适用：异常限制、写入故障或需要保留现场。");
        }
        break;
    case 4:
        open_confirm_overlay(ui, PTC_UI_OPERATION_PROBE_RAW_BLOCK, "验证强制阻止能力",
                             "功能：真机写入并回滚，验证强制阻止能力。\n适用：准备使用强制阻止前，仅需成功执行一次。");
        break;
    case 5:
        open_confirm_overlay(ui, PTC_UI_OPERATION_PROBE_SUSPEND, "验证暂停软件能力",
                             "功能：真机写入并回滚，验证暂停软件能力。\n适用：准备使用暂停软件前，仅需成功执行一次。");
        break;
    case 6:
        edit_grant_secret(ui);
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
    case PTC_UI_OPERATION_SAVE_BEDTIME:
        submit_bedtime(ui);
        break;
    case PTC_UI_OPERATION_SAVE_LIMIT_ACTION:
        submit_limit_action(ui);
        break;
    case PTC_UI_OPERATION_BLOCK_TODAY:
        submit_transport_empty(ui, "block_today", "正在设置今日禁玩…", "今日禁玩设置失败");
        break;
    case PTC_UI_OPERATION_DISABLE_TODAY_LIMIT:
        submit_transport_empty(ui, "disable_today_limit", "正在解除当前限制…", "解除当前限制失败");
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
    case PTC_UI_OPERATION_PROBE_RAW_BLOCK:
        submit_transport_empty(ui, "probe_raw_block", "正在验证强制阻止能力…", "强制阻止验证提交失败");
        break;
    case PTC_UI_OPERATION_PROBE_SUSPEND:
        submit_transport_empty(ui, "probe_suspend", "正在验证暂停软件能力…", "暂停软件验证提交失败");
        break;
    default:
        break;
    }
}

static void accept_numpad(UiState *ui)
{
    PtcUiNumpadPurpose purpose = ui->model.numpad_purpose;
    PtcUiOverlay return_overlay = ui->model.numpad_return_overlay;
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
    if (purpose == PTC_UI_NUMPAD_MINUTES) {
        if (return_overlay == PTC_UI_OVERLAY_WEEKLY) {
            ui->model.draft_week[ui->model.editor_index].minutes = value;
        } else {
            ui->model.draft_minutes = value;
        }
    } else if (purpose == PTC_UI_NUMPAD_BEDTIME) {
        if (ui->model.editor_index == 2) {
            ui->model.draft_bedtime.end_min = value;
        } else {
            ui->model.draft_bedtime.start_min = value;
        }
    }
    ptc_ui_numpad_finish(&ui->model);
}

static void handle_overlay_input(UiState *ui, u64 down)
{
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
                ptc_ui_limit_minutes_would_restrict(&ui->model, ui->model.draft_minutes)) {
                char body[192];
                snprintf(body, sizeof(body),
                         "已用约 %d 分钟；设置 %u 分钟。页面可能立即受限。",
                         ui->model.played_minutes, (unsigned int)ui->model.draft_minutes);
                open_confirm_overlay(ui, operation, "额度低于已用时间", body);
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
                if (today.mode == PTC_RULE_MODE_BLOCKED) {
                    snprintf(body, sizeof(body),
                             "已用时间%s；今天设为禁玩。页面可能立即受限。",
                             ui->model.played_minutes_available ? "见今日状态" : "暂不可用");
                } else {
                    snprintf(body, sizeof(body),
                             "已用约 %d 分钟；今天设置 %u 分钟。页面可能立即受限。",
                             ui->model.played_minutes, (unsigned int)today.minutes);
                }
                open_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_WEEKLY, "每周计划可能立即生效", body);
            } else {
                ui->model.overlay = PTC_UI_OVERLAY_NONE;
                submit_weekly(ui);
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_BEDTIME) {
        if (down & HidNpadButton_Left) {
            ui->model.editor_index = ui->model.editor_index <= 0 ? 2 : ui->model.editor_index - 1;
        } else if (down & HidNpadButton_Right) {
            ui->model.editor_index = ui->model.editor_index >= 2 ? 0 : ui->model.editor_index + 1;
        } else if (down & HidNpadButton_X) {
            ui->model.draft_bedtime.enabled = !ui->model.draft_bedtime.enabled;
        } else if ((down & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Y)) && !ui->model.draft_bedtime.enabled && ui->model.editor_index > 0) {
            snprintf(ui->model.message, sizeof(ui->model.message), "就寝限制未启用，请先按 X 键或点击【状态】启用。");
        } else if ((down & HidNpadButton_Up) && ui->model.editor_index == 1) {
            ui->model.draft_bedtime.start_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.start_min, 15);
        } else if ((down & HidNpadButton_Down) && ui->model.editor_index == 1) {
            ui->model.draft_bedtime.start_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.start_min, -15);
        } else if ((down & HidNpadButton_Up) && ui->model.editor_index == 2) {
            ui->model.draft_bedtime.end_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.end_min, 15);
        } else if ((down & HidNpadButton_Down) && ui->model.editor_index == 2) {
            ui->model.draft_bedtime.end_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.end_min, -15);
        } else if ((down & HidNpadButton_Y) && ui->model.draft_bedtime.enabled && ui->model.editor_index > 0) {
            edit_bedtime_minutes(ui);
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            if (ptc_ui_bedtime_active_at(&ui->model.draft_bedtime, current_minute_of_day_utc8())) {
                char body[192];
                snprintf(body, sizeof(body),
                         "已用时间%s；设置 %02u:%02u-%02u:%02u。页面可能立即受限。",
                         ui->model.played_minutes_available ? "见今日状态" : "暂不可用",
                         ui->model.draft_bedtime.start_min / 60, ui->model.draft_bedtime.start_min % 60,
                         ui->model.draft_bedtime.end_min / 60, ui->model.draft_bedtime.end_min % 60);
                open_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_BEDTIME, "就寝限制当前生效", body);
            } else {
                ui->model.overlay = PTC_UI_OVERLAY_NONE;
                submit_bedtime(ui);
            }
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_LIMIT_ACTION) {
        if (down & HidNpadButton_Left) {
            ui->model.draft_limit_action = ptc_ui_shift_limit_action(ui->model.draft_limit_action, -1);
        } else if (down & HidNpadButton_Right) {
            ui->model.draft_limit_action = ptc_ui_shift_limit_action(ui->model.draft_limit_action, 1);
        } else if (down & (HidNpadButton_A | HidNpadButton_Plus)) {
            if (ui->model.draft_limit_action != PTC_LIMIT_ACTION_REMIND &&
                ui->model.remaining_available && ui->model.remaining_minutes == 0) {
                char body[192];
                snprintf(body, sizeof(body),
                         "已用时间%s；设置为%s。页面可能立即受限。",
                         ui->model.played_minutes_available ? "见今日状态" : "暂不可用",
                         limit_action_label(ui->model.draft_limit_action));
                open_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_LIMIT_ACTION, "限制方式可能立即生效", body);
            } else {
                ui->model.overlay = PTC_UI_OVERLAY_NONE;
                submit_limit_action(ui);
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
        ui->model.view = PTC_UI_CHILD;
        break;
    case PTC_UI_HIT_PARENT_PREV_PAGE:
        ptc_ui_change_parent_page(&ui->model, -1);
        break;
    case PTC_UI_HIT_PARENT_NEXT_PAGE:
        ptc_ui_change_parent_page(&ui->model, 1);
        break;
    case PTC_UI_HIT_PARENT_REFRESH:
        refresh_disable_flag(ui);
        poll_result(ui, true);
        break;
    case PTC_UI_HIT_PARENT_BACK:
        ui->model.view = ui->model.setup_phase[0] && strcmp(ui->model.setup_phase, "active") != 0
            ? PTC_UI_SETUP : PTC_UI_CHILD;
        snprintf(ui->model.message, sizeof(ui->model.message), "已返回主页面。");
        break;
    case PTC_UI_HIT_PARENT_TAB:
        ui->model.parent_page = (PtcUiParentPage)hit.index;
        ui->model.selected_index = 0;
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
        handle_overlay_input(ui, ui->model.overlay == PTC_UI_OVERLAY_NUMPAD ? HidNpadButton_Plus : HidNpadButton_A);
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
        break;
    case PTC_UI_HIT_WEEKLY_MIN_UP:
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 15, 1, 1440);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DOWN:
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -15, 1, 1440);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DEC:
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -5, 1, 1440);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_INC:
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 5, 1, 1440);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_INPUT:
        if (hit.index >= 0 && hit.index < 7) {
            ui->model.editor_index = hit.index;
        }
        edit_weekly_minutes(ui);
        break;
    case PTC_UI_HIT_BEDTIME_FIELD:
        ui->model.editor_index = hit.index;
        if (hit.index == 0) {
            ui->model.draft_bedtime.enabled = !ui->model.draft_bedtime.enabled;
        } else if (ui->model.draft_bedtime.enabled) {
            edit_bedtime_minutes(ui);
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message), "就寝限制未启用，请先按 X 键或点击【状态】启用。");
        }
        break;
    case PTC_UI_HIT_BEDTIME_ADJ_UP:
        ui->model.editor_index = hit.index == 2 ? 2 : 1;
        if (!ui->model.draft_bedtime.enabled) {
            snprintf(ui->model.message, sizeof(ui->model.message), "就寝限制未启用，请先按 X 键或点击【状态】启用。");
        } else if (ui->model.editor_index == 1) {
            ui->model.draft_bedtime.start_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.start_min, 15);
        } else if (ui->model.editor_index == 2) {
            ui->model.draft_bedtime.end_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.end_min, 15);
        }
        break;
    case PTC_UI_HIT_BEDTIME_ADJ_DOWN:
        ui->model.editor_index = hit.index == 2 ? 2 : 1;
        if (!ui->model.draft_bedtime.enabled) {
            snprintf(ui->model.message, sizeof(ui->model.message), "就寝限制未启用，请先按 X 键或点击【状态】启用。");
        } else if (ui->model.editor_index == 1) {
            ui->model.draft_bedtime.start_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.start_min, -15);
        } else if (ui->model.editor_index == 2) {
            ui->model.draft_bedtime.end_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.end_min, -15);
        }
        break;
    case PTC_UI_HIT_LIMIT_ACTION_OPTION:
        if (hit.index == 0) {
            ui->model.draft_limit_action = PTC_LIMIT_ACTION_REMIND;
        } else if (hit.index == 1) {
            ui->model.draft_limit_action = PTC_LIMIT_ACTION_RAW_BLOCK;
        } else if (hit.index == 2) {
            ui->model.draft_limit_action = PTC_LIMIT_ACTION_SUSPEND;
        }
        break;
    case PTC_UI_HIT_NUMPAD_KEY:
        ui->model.numpad_cursor = hit.index;
        ptc_ui_numpad_activate(&ui->model);
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
    ui.last_bedtime_minute = -1;
    ui.last_setup_refresh_second = -1;
    load_rule_drafts(&ui);
    refresh_bedtime_state(&ui, true);
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
                ui.model.view = PTC_UI_CHILD;
            }
        } else {
            if (down & HidNpadButton_B) {
                ui.model.view = ui.model.setup_phase[0] && strcmp(ui.model.setup_phase, "active") != 0
                    ? PTC_UI_SETUP : PTC_UI_CHILD;
                snprintf(ui.model.message, sizeof(ui.model.message), "已返回主页面。");
            } else if (down & HidNpadButton_L) {
                ptc_ui_change_parent_page(&ui.model, -1);
            } else if (down & HidNpadButton_R) {
                ptc_ui_change_parent_page(&ui.model, 1);
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
        refresh_bedtime_state(&ui, false);
        draw(&ui);
        svcSleepThread(LOOP_SLEEP_NS);
    }

    ptc_ui_graphics_exit();
    ptc_switch_ipc_client_exit(&ui.ipc);
    return 0;
}
