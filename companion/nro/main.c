#include <switch.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../companion/auth.h"
#include "../../companion/file_protocol.h"
#include "../../companion/self_check.h"
#include "../../platform/switch/fs_storage.h"
#include "../../third_party/cjson/cJSON.h"
#include "ui_graphics.h"

#define APP_ROOT "sdmc:/switch/play-time-control"
#define RULES_PATH APP_ROOT "/rules.json"
#define RESULT_TEXT_SIZE 4096
#define REQUEST_TIMEOUT_MS 60000
#define LOOP_SLEEP_NS 100000000LL
#define LOOP_SLEEP_MS 100
#define HIDDEN_HOLD_TICKS 20
#define HIDDEN_LEFT_SHOULDER_MASK (HidNpadButton_L | HidNpadButton_ZL)
#define HIDDEN_RIGHT_SHOULDER_MASK (HidNpadButton_R | HidNpadButton_ZR)

typedef struct {
    PtcCompanionFileClient client;
    PtcCompanionAuth auth;
    PtcUiModel model;
    char active_request_id[PTC_COMPANION_REQUEST_ID_SIZE];
    char last_result[RESULT_TEXT_SIZE];
    int elapsed_ms;
    int hidden_ticks;
    bool waiting;
    bool self_check_after_result;
    bool quick_device_test;
    PtcSelfCheckProfile self_check_after_profile;
} UiState;

typedef PtcCompanionStatus (*SubmitNoArgFn)(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);

static PtcCompanionStatus submit_effect_fast(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);

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
    snprintf(ui->model.message, sizeof(ui->model.message), "%s：%s", prefix, companion_status_zh(status));
    snprintf(ui->model.result_status, sizeof(ui->model.result_status), "error");
}

static void set_auth_message(UiState *ui, const char *prefix, PtcAuthStatus status)
{
    snprintf(ui->model.message, sizeof(ui->model.message), "%s：%s", prefix, auth_status_zh(status));
    snprintf(ui->model.result_status, sizeof(ui->model.result_status), "error");
}

static bool hidden_parent_combo_held(u64 buttons)
{
    return (buttons & HidNpadButton_X) &&
           (buttons & HIDDEN_LEFT_SHOULDER_MASK) &&
           (buttons & HIDDEN_RIGHT_SHOULDER_MASK);
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

static void begin_wait(UiState *ui, const char *message)
{
    ui->waiting = true;
    ui->elapsed_ms = 0;
    ui->last_result[0] = '\0';
    ui->self_check_after_result = false;
    ui->quick_device_test = false;
    ui->model.result_status[0] = '\0';
    snprintf(ui->model.message, sizeof(ui->model.message), "%s", message);
}

static void submit_noarg(UiState *ui, SubmitNoArgFn submit, const char *ok_message, const char *fail_prefix)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = submit(&ui->client, ui->active_request_id, time(NULL));
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, ok_message);
        return;
    }
    ui->waiting = false;
    set_message(ui, fail_prefix, status);
}

static void submit_status(UiState *ui)
{
    submit_noarg(ui, ptc_companion_submit_status, "正在刷新今天的状态…", "刷新失败");
}

static void submit_offline_code(UiState *ui)
{
    char code[80];
    PtcCompanionStatus status;
    if (!keyboard_input("输入离线加时码", "格式：XXXX-XXXX-XXXX-XXXX", code, sizeof(code), false, true)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消输入加时码。");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_submit_offline_code(&ui->client, ui->active_request_id, time(NULL), code);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "加时码已提交，正在等待后台确认…");
        return;
    }
    ui->waiting = false;
    set_message(ui, "加时码提交失败", status);
}

static void submit_minutes(UiState *ui, PtcUiOperation operation, uint16_t minutes)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    if (operation == PTC_UI_OPERATION_SET_TODAY_LIMIT) {
        status = ptc_companion_submit_set_today_limit(&ui->client, ui->active_request_id, time(NULL), minutes);
    } else if (operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        status = ptc_companion_submit_add_today_minutes(&ui->client, ui->active_request_id, time(NULL), minutes);
    } else {
        status = ptc_companion_submit_parent_unlock_start(&ui->client, ui->active_request_id, time(NULL), minutes);
    }
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "设置已提交，正在等待后台确认…");
    } else {
        ui->waiting = false;
        set_message(ui, "设置提交失败", status);
    }
}

static void submit_weekly(UiState *ui)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_submit_set_weekly_template(
        &ui->client,
        ui->active_request_id,
        time(NULL),
        ui->model.draft_week);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "每周计划已提交，正在等待后台确认…");
    } else {
        ui->waiting = false;
        set_message(ui, "每周计划提交失败", status);
    }
}

static void submit_bedtime(UiState *ui)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_submit_set_bedtime(
        &ui->client,
        ui->active_request_id,
        time(NULL),
        &ui->model.draft_bedtime);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "就寝时间已提交，正在等待后台确认…");
    } else {
        ui->waiting = false;
        set_message(ui, "就寝时间提交失败", status);
    }
}

static void submit_limit_action(UiState *ui)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_submit_set_limit_action(
        &ui->client,
        ui->active_request_id,
        time(NULL),
        ui->model.draft_limit_action);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "限制方式已提交，正在等待后台确认…");
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
    ui->model.draft_limit_action = rules.limit_action;
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
    ui->model.draft_limit_action = rules.limit_action;
    cJSON_Delete(root);
}

static void arm_self_check_after_result(UiState *ui, PtcSelfCheckProfile profile)
{
    ui->self_check_after_result = true;
    ui->self_check_after_profile = profile;
}

static void run_self_check_profile(UiState *ui, PtcSelfCheckProfile profile)
{
    PtcSelfCheckResult result;
    char report[RESULT_TEXT_SIZE];
    result = ptc_self_check_run(
        ui->client.storage,
        APP_ROOT,
        ui->active_request_id,
        profile,
        NULL,
        report,
        sizeof(report));
    ui->quick_device_test = false;
    if (result.status == PTC_SELF_CHECK_PASS) {
        snprintf(ui->model.result_status, sizeof(ui->model.result_status), "ok");
        snprintf(ui->model.message, sizeof(ui->model.message), "快速设备测试通过，设置已自动恢复。");
    } else {
        PtcCompanionStatus disable_status = ptc_companion_set_disable_flag(&ui->client, true);
        snprintf(ui->model.result_status, sizeof(ui->model.result_status), "error");
        snprintf(
            ui->model.message,
            sizeof(ui->model.message),
            "快速设备测试未通过，已自动停用控制：%s",
            companion_status_zh(disable_status));
    }
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
    status = ptc_companion_read_result(
        &ui->client,
        ui->active_request_id,
        ui->elapsed_ms,
        REQUEST_TIMEOUT_MS,
        ui->last_result,
        sizeof(ui->last_result));
    if (status == PTC_COMPANION_PENDING) {
        snprintf(ui->model.message, sizeof(ui->model.message), "后台正在处理，请稍候…");
        return;
    }
    ui->waiting = false;
    if (status == PTC_COMPANION_OK) {
        if (!ptc_ui_apply_result_json(&ui->model, ui->last_result)) {
            set_message(ui, "读取结果失败", PTC_COMPANION_RESULT_INVALID);
            ui->self_check_after_result = false;
            return;
        }
        if (ui->self_check_after_result) {
            ui->self_check_after_result = false;
            run_self_check_profile(ui, ui->self_check_after_profile);
        }
        return;
    }
    ui->self_check_after_result = false;
    if (ui->quick_device_test) {
        PtcCompanionStatus disable_status = ptc_companion_set_disable_flag(&ui->client, true);
        ui->quick_device_test = false;
        snprintf(ui->model.result_status, sizeof(ui->model.result_status), "error");
        snprintf(
            ui->model.message,
            sizeof(ui->model.message),
            "快速设备测试失败，已自动停用控制：%s",
            companion_status_zh(disable_status));
        return;
    }
    set_message(ui, "读取结果失败", status);
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
    ui->model.view = PTC_UI_PARENT;
    ui->model.parent_page = PTC_UI_PARENT_TODAY;
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

static void handle_parent_action(UiState *ui)
{
    int index = ui->model.selected_index;
    if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        switch (index) {
        case 0:
            submit_status(ui);
            break;
        case 1:
            open_minutes_overlay(ui, PTC_UI_OPERATION_SET_TODAY_LIMIT, "设置今日额度", "选择今天最多可玩的时间。", 60, 5, 1440);
            break;
        case 2:
            open_minutes_overlay(ui, PTC_UI_OPERATION_ADD_TODAY_MINUTES, "临时加时", "在今天现有额度上增加时间。", 15, 5, 120);
            break;
        case 3:
            submit_noarg(ui, ptc_companion_submit_disable_today_limit, "正在设置今日不限时…", "设置今日不限失败");
            break;
        case 4:
            open_confirm_overlay(ui, PTC_UI_OPERATION_BLOCK_TODAY, "确认今日禁玩", "这会把今天设置为不可游玩。");
            break;
        case 5:
            submit_noarg(ui, ptc_companion_submit_restore_today_policy, "正在恢复每周计划…", "恢复计划失败");
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
            open_minutes_overlay(ui, PTC_UI_OPERATION_PARENT_UNLOCK, "临时解锁", "选择暂停本地规则的时长。", 15, 5, 1440);
            break;
        case 4:
            submit_noarg(ui, ptc_companion_submit_parent_unlock_end, "正在结束临时解锁…", "结束解锁失败");
            break;
        default:
            break;
        }
        return;
    }
    switch (index) {
    case 0:
        open_confirm_overlay(ui, PTC_UI_OPERATION_QUICK_TEST, "运行快速设备测试", "测试会短暂写入计时器并自动恢复；失败时将自动停用控制。");
        break;
    case 1:
        open_confirm_overlay(ui, PTC_UI_OPERATION_EMERGENCY_DISABLE, "紧急停用控制", "创建 disable.flag 后，后台将停止执行控制操作。");
        break;
    case 2:
        open_confirm_overlay(ui, PTC_UI_OPERATION_RESUME_CONTROL, "恢复后台控制", "仅在确认设备状态正常后移除 disable.flag。");
        break;
    case 3:
        open_confirm_overlay(ui, PTC_UI_OPERATION_PROBE_RAW_BLOCK, "验证强制阻止能力",
                             "探针会尝试真机 raw block 写入并回滚，需 grant/enforce 模式。适配层未实现时会返回失败，能力保持未验证。");
        break;
    case 4:
        open_confirm_overlay(ui, PTC_UI_OPERATION_PROBE_SUSPEND, "验证暂停软件能力",
                             "探针会尝试真机 suspend 写入并回滚，需 grant/enforce 模式。适配层未实现时会返回失败，能力保持未验证。");
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
    case PTC_UI_OPERATION_BLOCK_TODAY:
        submit_noarg(ui, ptc_companion_submit_block_today, "正在设置今日禁玩…", "今日禁玩设置失败");
        break;
    case PTC_UI_OPERATION_QUICK_TEST:
        submit_noarg(ui, submit_effect_fast, "快速设备测试正在运行…", "快速设备测试提交失败");
        if (ui->waiting) {
            ui->quick_device_test = true;
            arm_self_check_after_result(ui, PTC_SELF_CHECK_PLAY_TIMER_EFFECT_PROBE);
        } else {
            status = ptc_companion_set_disable_flag(&ui->client, true);
            snprintf(ui->model.message, sizeof(ui->model.message), "测试无法启动，已停用控制：%s", companion_status_zh(status));
        }
        break;
    case PTC_UI_OPERATION_EMERGENCY_DISABLE:
        status = ptc_companion_set_disable_flag(&ui->client, true);
        if (status == PTC_COMPANION_OK) {
            snprintf(ui->model.result_status, sizeof(ui->model.result_status), "ok");
            snprintf(ui->model.message, sizeof(ui->model.message), "后台控制已紧急停用。");
        } else {
            set_message(ui, "紧急停用失败", status);
        }
        break;
    case PTC_UI_OPERATION_RESUME_CONTROL:
        status = ptc_companion_set_disable_flag(&ui->client, false);
        if (status == PTC_COMPANION_OK) {
            snprintf(ui->model.result_status, sizeof(ui->model.result_status), "ok");
            snprintf(ui->model.message, sizeof(ui->model.message), "后台控制已恢复。");
        } else {
            set_message(ui, "恢复控制失败", status);
        }
        break;
    case PTC_UI_OPERATION_PROBE_RAW_BLOCK:
        submit_noarg(ui, ptc_companion_submit_probe_raw_block, "正在验证强制阻止能力…", "强制阻止验证提交失败");
        break;
    case PTC_UI_OPERATION_PROBE_SUSPEND:
        submit_noarg(ui, ptc_companion_submit_probe_suspend, "正在验证暂停软件能力…", "暂停软件验证提交失败");
        break;
    default:
        break;
    }
}

static void handle_overlay_input(UiState *ui, u64 down)
{
    if (down & HidNpadButton_B) {
        ptc_ui_cancel_overlay(&ui->model);
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消修改。");
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_MINUTES) {
        if (down & HidNpadButton_Up) {
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Down) {
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Right) {
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_Left) {
            ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        } else if (down & HidNpadButton_A) {
            PtcUiOperation operation = ui->model.operation;
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            ui->model.operation = PTC_UI_OPERATION_NONE;
            submit_minutes(ui, operation, ui->model.draft_minutes);
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
            day->minutes = ptc_ui_adjust_minutes(day->minutes, 15, 15, 1440);
        } else if ((down & HidNpadButton_Down) && day->mode == PTC_RULE_MODE_LIMIT) {
            day->minutes = ptc_ui_adjust_minutes(day->minutes, -15, 15, 1440);
        } else if (down & HidNpadButton_A) {
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            submit_weekly(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_BEDTIME) {
        if (down & HidNpadButton_Left) {
            ui->model.editor_index = ui->model.editor_index <= 0 ? 2 : ui->model.editor_index - 1;
        } else if (down & HidNpadButton_Right) {
            ui->model.editor_index = ui->model.editor_index >= 2 ? 0 : ui->model.editor_index + 1;
        } else if ((down & HidNpadButton_X) && ui->model.editor_index == 0) {
            ui->model.draft_bedtime.enabled = !ui->model.draft_bedtime.enabled;
        } else if ((down & HidNpadButton_Up) && ui->model.editor_index == 1) {
            ui->model.draft_bedtime.start_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.start_min, 15);
        } else if ((down & HidNpadButton_Down) && ui->model.editor_index == 1) {
            ui->model.draft_bedtime.start_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.start_min, -15);
        } else if ((down & HidNpadButton_Up) && ui->model.editor_index == 2) {
            ui->model.draft_bedtime.end_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.end_min, 15);
        } else if ((down & HidNpadButton_Down) && ui->model.editor_index == 2) {
            ui->model.draft_bedtime.end_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.end_min, -15);
        } else if (down & HidNpadButton_A) {
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            submit_bedtime(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_LIMIT_ACTION) {
        if (down & HidNpadButton_Left) {
            ui->model.draft_limit_action = ptc_ui_shift_limit_action(ui->model.draft_limit_action, -1);
        } else if (down & HidNpadButton_Right) {
            ui->model.draft_limit_action = ptc_ui_shift_limit_action(ui->model.draft_limit_action, 1);
        } else if (down & HidNpadButton_A) {
            ui->model.overlay = PTC_UI_OVERLAY_NONE;
            submit_limit_action(ui);
        }
        return;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM && (down & HidNpadButton_A)) {
        confirm_operation(ui);
    }
}

static PtcCompanionStatus submit_effect_fast(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    return ptc_companion_submit_probe_play_timer_effect(client, request_id, created_at, false);
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
            submit_offline_code(ui);
        }
        break;
    case PTC_UI_HIT_CHILD_REFRESH:
        submit_status(ui);
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
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_MINUTES_INC:
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_DEC:
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -15, ui->model.minimum_minutes, ui->model.maximum_minutes);
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
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 15, 15, 1440);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DOWN:
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -15, 15, 1440);
        }
        break;
    case PTC_UI_HIT_BEDTIME_FIELD:
        ui->model.editor_index = hit.index;
        if (hit.index == 0) {
            ui->model.draft_bedtime.enabled = !ui->model.draft_bedtime.enabled;
        }
        break;
    case PTC_UI_HIT_BEDTIME_ADJ_UP:
        if (ui->model.editor_index == 1) {
            ui->model.draft_bedtime.start_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.start_min, 15);
        } else if (ui->model.editor_index == 2) {
            ui->model.draft_bedtime.end_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.end_min, 15);
        }
        break;
    case PTC_UI_HIT_BEDTIME_ADJ_DOWN:
        if (ui->model.editor_index == 1) {
            ui->model.draft_bedtime.start_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.start_min, -15);
        } else if (ui->model.editor_index == 2) {
            ui->model.draft_bedtime.end_min = ptc_ui_adjust_minute_of_day(ui->model.draft_bedtime.end_min, -15);
        }
        break;
    case PTC_UI_HIT_LIMIT_ACTION_OPTION: {
        static const PtcLimitAction OPTIONS[] = {
            PTC_LIMIT_ACTION_REMIND,
            PTC_LIMIT_ACTION_RAW_BLOCK,
            PTC_LIMIT_ACTION_SUSPEND,
        };
        if (hit.index >= 0 && hit.index < 3) {
            ui->model.draft_limit_action = OPTIONS[hit.index];
        }
        break;
    }
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
    printf("游玩时间控制\n\n");
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
    HidTouchScreenState touch;
    bool touch_down = false;
    bool running = true;
    (void)argc;
    (void)argv;

    if (!ptc_ui_graphics_init()) {
        run_console_fallback();
        return 1;
    }
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    srand((unsigned int)time(NULL));

    memset(&ui, 0, sizeof(ui));
    ui.model.view = PTC_UI_CHILD;
    ui.model.parent_page = PTC_UI_PARENT_TODAY;
    ui.model.remaining_minutes = -1;
    ui.model.play_timer_enabled = -1;
    ui.model.restricted_now = -1;
    snprintf(ui.model.message, sizeof(ui.model.message), "正在读取今天的游玩状态…");
    ptc_fs_storage_init(&fs);
    ptc_companion_file_client_init(&ui.client, APP_ROOT, ptc_fs_storage_as_storage(&fs));
    ptc_companion_auth_init(&ui.auth, APP_ROOT, ptc_fs_storage_as_storage(&fs));
    submit_status(&ui);

    while (appletMainLoop() && running) {
        u64 down;
        u64 held;
        bool parent_combo_held;
        padUpdate(&pad);
        down = padGetButtonsDown(&pad);
        held = padGetButtons(&pad);
        parent_combo_held = hidden_parent_combo_held(held);

        if (ui.model.view == PTC_UI_CHILD && ui.model.overlay == PTC_UI_OVERLAY_NONE && parent_combo_held) {
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
            } else if (down & HidNpadButton_A) {
                if (ui.waiting) {
                    snprintf(ui.model.message, sizeof(ui.model.message), "请等待当前操作完成后再提交加时码。");
                } else {
                    submit_offline_code(&ui);
                }
            } else if (down & HidNpadButton_Y) {
                submit_status(&ui);
            } else if (down & HidNpadButton_Minus) {
                enter_parent_area(&ui);
            }
        } else {
            if (down & HidNpadButton_B) {
                ui.model.view = PTC_UI_CHILD;
                snprintf(ui.model.message, sizeof(ui.model.message), "已返回孩子页面。");
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

        poll_result(&ui, false);
        draw(&ui);
        svcSleepThread(LOOP_SLEEP_NS);
    }

    ptc_ui_graphics_exit();
    return 0;
}
