#include "nro_app_internal.h"

#ifndef PLAYWISE_EDEN
bool standard_backend_expected(void)
{
    struct stat info;
    return stat(STANDARD_BOOT_FLAG_PATH, &info) == 0 && info.st_size == 0;
}

#endif
void current_environment_fingerprint(UiState *ui, char out[65])
{
    char environment[1024];
    uint8_t digest[PTC_SHA256_DIGEST_SIZE];
    static const char HEX[] = "0123456789abcdef";
    size_t index;
    if (!ui || !ui->client.storage ||
        !ui->client.storage->vtable->read_text(ui->client.storage,
            APP_ROOT "/environment.json", environment, sizeof(environment))) {
        snprintf(out, 65, "environment-unavailable");
        return;
    }
    {
        PtcSha256Ctx hash;
        ptc_sha256_init(&hash);
        ptc_sha256_update(&hash, (const uint8_t *)environment, strlen(environment));
        ptc_sha256_final(&hash, digest);
    }
    for (index = 0; index < sizeof(digest); ++index) {
        out[index * 2] = HEX[digest[index] >> 4];
        out[index * 2 + 1] = HEX[digest[index] & 0x0fu];
    }
    out[64] = '\0';
}

PadState *g_active_pad;

#ifndef PLAYWISE_EDEN
void sync_hot_reload_model(UiState *ui)
{
    if (!ui) return;
    ui->model.hot_reload_status = (int)ui->hot_reload.status;
    snprintf(ui->model.app_release_id, sizeof(ui->model.app_release_id), "%s", PLAYWISE_BUILD_RELEASE_ID);
    snprintf(ui->model.backend_release_id, sizeof(ui->model.backend_release_id), "%s",
        ui->hot_reload.journal.source_release_id);
    snprintf(ui->model.hot_reload_detail, sizeof(ui->model.hot_reload_detail), "%s",
        ui->hot_reload.detail);
}

void open_hot_reload_confirmation(UiState *ui)
{
    char body[320];
    if (!ui || ui->model.view != PTC_UI_PARENT ||
        ui->hot_reload.status != PTC_HOT_RELOAD_PENDING || ui->waiting) return;
    snprintf(body, sizeof(body),
        "当前后台：%.88s\n待加载：%.88s\n将短暂停止接单，安全退出旧后台后加载已安装版本；PIN、规则和运行数据保持不变。",
        ui->hot_reload.journal.source_release_id, PLAYWISE_BUILD_RELEASE_ID);
    open_confirm_overlay(ui, PTC_UI_OPERATION_HOT_RELOAD, "加载已安装的新版本", body);
}

void poll_hot_reload(UiState *ui)
{
    if (!ui) return;
    ptc_hot_reload_tick(&ui->hot_reload);
    if (ui->hot_reload.phase == PTC_HOT_RELOAD_PHASE_NEED_IPC_CLOSE) {
        ptc_companion_transport_cancel(&ui->transport);
        ptc_switch_ipc_client_exit(&ui->ipc);
        ui->hot_reload_ipc_closed = true;
        ptc_hot_reload_confirm_ipc_closed(&ui->hot_reload);
    }
    sync_hot_reload_model(ui);
    if (ui->hot_reload.status == PTC_HOT_RELOAD_RUNNING) {
        snprintf(ui->model.message, sizeof(ui->model.message), "%s", ui->hot_reload.detail);
    }
    if (ui->hot_reload.phase == PTC_HOT_RELOAD_PHASE_COMPLETE && !ui->hot_reload_terminal_handled) {
        ui->hot_reload_terminal_handled = true;
        ptc_switch_ipc_client_init(&ui->ipc);
        ui->hot_reload_ipc_closed = false;
        ui->waiting = false;
        ui->model.waiting = false;
        if (ptc_switch_ipc_client_probe(&ui->ipc)) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                "新版后台已安全加载，正在刷新状态...");
            submit_status(ui);
        } else {
            ui->hot_reload.status = PTC_HOT_RELOAD_UNAVAILABLE;
            snprintf(ui->hot_reload.detail, sizeof(ui->hot_reload.detail),
                "后台已启动但 IPC 重连失败，请完整重启主机");
            sync_hot_reload_model(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "%s", ui->hot_reload.detail);
        }
    } else if (ui->hot_reload.phase == PTC_HOT_RELOAD_PHASE_FAILED && !ui->hot_reload_terminal_handled) {
        ui->hot_reload_terminal_handled = true;
        ui->waiting = false;
        ui->model.waiting = false;
        if (ui->hot_reload_ipc_closed) {
            ptc_switch_ipc_client_init(&ui->ipc);
            (void)ptc_switch_ipc_client_probe(&ui->ipc);
            ui->hot_reload_ipc_closed = false;
        }
        snprintf(ui->model.message, sizeof(ui->model.message), "%s", ui->hot_reload.detail);
    }
    if (ui->hot_reload.status == PTC_HOT_RELOAD_PENDING &&
        ui->model.view == PTC_UI_CHILD && !ui->waiting &&
        ui->model.overlay == PTC_UI_OVERLAY_NONE && !ui->hot_reload_child_notice_shown) {
        ui->hot_reload_child_notice_shown = true;
        snprintf(ui->model.message, sizeof(ui->model.message),
            "已安装新版本；请进入家长区，通过 PIN 确认加载。");
    }
}
#endif

/* 数字缓动按帧间隔归一：40ms 拍下与旧 (差值/4 + 1) 公式等价，拍长变化时节奏不变。 */
static void tween_displayed_minutes(int *value, int target, int64_t delta_ms)
{
    int diff = target - *value;
    int distance = diff > 0 ? diff : -diff;
    int step;
    if (diff == 0) return;
    step = (int)((int64_t)distance * delta_ms / 160) + 1;
    if (step > distance) step = distance;
    *value += diff > 0 ? step : -step;
}

/* 动画全部按真实时间推进：弹窗开合以上次切换时刻推算帧数，数字缓动按帧间隔
 * 归一。返回是否仍有进行中的动画，供主循环在动画期自适应提高帧率。 */
bool update_animations(PtcUiModel *model)
{
    static int64_t last_update_ms = 0;
    int64_t now = ptc_ui_anim_now_ms();
    int64_t delta = now - last_update_ms;
    int64_t elapsed;
    int frames;
    if (delta <= 0 || delta > 200) delta = PTC_UI_OVERLAY_OPEN_FRAME_MS;
    last_update_ms = now;

    if (model->overlay != model->previous_overlay) {
        if (model->overlay == PTC_UI_OVERLAY_NONE && model->previous_overlay != PTC_UI_OVERLAY_NONE) {
            model->closing_overlay = model->previous_overlay;
            model->closing_started_ms = now;
        } else {
            if (model->previous_overlay == PTC_UI_OVERLAY_NUMPAD ||
                model->previous_overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
                model->numpad_purpose = PTC_UI_NUMPAD_NONE;
                model->duration_hours_text[0] = '\0';
                model->duration_minutes_text[0] = '\0';
            }
            model->closing_overlay = PTC_UI_OVERLAY_NONE;
        }
        model->overlay_opened_at_ms = now;
        model->previous_overlay = model->overlay;
    }
    if (model->closing_overlay != PTC_UI_OVERLAY_NONE &&
        now - model->closing_started_ms >= PTC_UI_OVERLAY_CLOSE_TOTAL_MS) {
        if (model->closing_overlay == PTC_UI_OVERLAY_NUMPAD ||
            model->closing_overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
            model->numpad_purpose = PTC_UI_NUMPAD_NONE;
            model->duration_hours_text[0] = '\0';
            model->duration_minutes_text[0] = '\0';
        }
        model->closing_overlay = PTC_UI_OVERLAY_NONE;
    }
    elapsed = now - model->overlay_opened_at_ms;
    if (elapsed < 0) elapsed = 0;
    frames = (int)(elapsed / PTC_UI_OVERLAY_OPEN_FRAME_MS);
    if (frames > PTC_UI_OVERLAY_OPEN_FRAMES) frames = PTC_UI_OVERLAY_OPEN_FRAMES;
    model->overlay_open_frames = frames;
    model->rendered_overlay = model->overlay != PTC_UI_OVERLAY_NONE ? model->overlay : model->closing_overlay;

    if (model->parent_page != model->last_parent_page) {
        model->last_parent_page = model->parent_page;
        model->page_switched_at_ms = now;
    }

    tween_displayed_minutes(&model->displayed_remaining_minutes, model->remaining_minutes, delta);
    tween_displayed_minutes(&model->displayed_grant_minutes, model->grant_minutes, delta);

    return model->overlay_open_frames < PTC_UI_OVERLAY_OPEN_FRAMES ||
           model->closing_overlay != PTC_UI_OVERLAY_NONE ||
           model->displayed_remaining_minutes != model->remaining_minutes ||
           model->displayed_grant_minutes != model->grant_minutes ||
           now - model->page_switched_at_ms < PTC_UI_PAGE_SWITCH_TOTAL_MS;
}

static int64_t unix_ms_now(void)
{
    return (int64_t)time(NULL) * 1000;
}

void make_next_request_id(char *out, size_t out_size)
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
    case PTC_COMPANION_QUIESCING:
        return "后台正在安全切换，请稍后重试";
    default:
        return "未知错误";
    }
}

const char *auth_status_zh(PtcAuthStatus status)
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

void set_message(UiState *ui, const char *prefix, PtcCompanionStatus status)
{
    ui->model.feedback_detail[0] = '\0';
    snprintf(ui->model.message, sizeof(ui->model.message), "%s：%s", prefix, companion_status_zh(status));
    snprintf(ui->model.result_status, sizeof(ui->model.result_status), "error");
    if (ui->model.view == PTC_UI_CHILD) {
        ui->model.view = PTC_UI_ERROR;
    }
}

void show_auth_error(UiState *ui, const char *title, const char *message, int64_t retry_after)
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

void close_auth_error(UiState *ui, bool cancelled)
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

void set_command_name(UiState *ui, const char *type)
{
    ptc_ui_set_execution(
        &ui->model,
        ptc_companion_request_command_label_zh(type),
        ui->model.transport_label);
}

void sync_transport_label(UiState *ui)
{
#ifdef PLAYWISE_EDEN
    /* The emulator build never talks to pctc:u, so name the in-process core
       instead of a transport route it does not use. */
    ptc_ui_set_execution(&ui->model, ui->model.command_name, "传输：Eden 模拟后台");
#else
    ptc_ui_set_execution(
        &ui->model,
        ui->model.command_name,
        ptc_companion_transport_route_label_zh(ptc_companion_transport_route(&ui->transport)));
#endif
}

void set_local_sd_command(UiState *ui, const char *command_name)
{
    ptc_ui_set_execution(
        &ui->model,
        command_name,
        ptc_companion_transport_route_label_zh(PTC_TRANSPORT_ROUTE_LOCAL_SD_FLAG));
}

bool hidden_parent_combo_held(u64 buttons)
{
    return (buttons & HidNpadButton_X) &&
           (buttons & HIDDEN_LEFT_SHOULDER_MASK) &&
           (buttons & HIDDEN_RIGHT_SHOULDER_MASK);
}

bool custom_parent_combo_held(const UiState *ui, u64 buttons)
{
    if (!ui || !ui->model.custom_shortcut_enabled || ui->model.custom_shortcut_mask == 0) {
        return false;
    }
    return ptc_ui_shortcut_mask_held(ui->model.custom_shortcut_mask, buttons);
}

void refresh_disable_flag(UiState *ui)
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
        (ui->model.overlay == PTC_UI_OVERLAY_NUMPAD ||
         ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) &&
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

bool weekly_editing_blocked(UiState *ui)
{
    bool cancelling_weekly_input = ui &&
        (ui->model.overlay == PTC_UI_OVERLAY_NUMPAD ||
         ui->model.overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) &&
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

u64 stick_direction_buttons(HidAnalogStickState stick)
{
    u64 buttons = 0;
    if (stick.x > STICK_DEADZONE) buttons |= HidNpadButton_Right;
    if (stick.x < -STICK_DEADZONE) buttons |= HidNpadButton_Left;
    if (stick.y > STICK_DEADZONE) buttons |= HidNpadButton_Up;
    if (stick.y < -STICK_DEADZONE) buttons |= HidNpadButton_Down;
    return buttons;
}

bool switch_random(uint8_t *out, size_t out_size, void *ctx)
{
    (void)ctx;
    randomGet(out, out_size);
    return true;
}

static bool keyboard_input_initial(
    const char *header,
    const char *guide,
    char *out,
    size_t out_size,
    bool password,
    bool numeric,
    bool download_code,
    const char *initial)
{
    SwkbdConfig keyboard;
    Result result;
    if (!out || out_size == 0) {
        return false;
    }
    if (!initial) out[0] = '\0';
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
    if (initial && initial[0]) swkbdConfigSetInitialText(&keyboard, initial);
    swkbdConfigSetOkButtonText(&keyboard, "确认");
    result = swkbdShow(&keyboard, out, out_size);
    swkbdClose(&keyboard);
    return R_SUCCEEDED(result) && out[0] != '\0';
}

bool keyboard_input(
    const char *header,
    const char *guide,
    char *out,
    size_t out_size,
    bool password,
    bool numeric,
    bool download_code)
{
    return keyboard_input_initial(header, guide, out, out_size, password, numeric, download_code, NULL);
}

static bool keyboard_date(UiState *ui, uint16_t current, uint16_t *out_day_index)
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    char initial[9];
    char value[9];
    if (!ui || !out_day_index || !ui->model.status_loaded ||
        !ptc_date_from_day_index(current, &year, &month, &day)) {
        if (ui) snprintf(ui->model.message, sizeof(ui->model.message),
                         "主机日期尚未加载，请刷新状态后再输入日期。");
        return false;
    }
    initial[0] = (char)('0' + (year / 1000u) % 10u);
    initial[1] = (char)('0' + (year / 100u) % 10u);
    initial[2] = (char)('0' + (year / 10u) % 10u);
    initial[3] = (char)('0' + year % 10u);
    initial[4] = (char)('0' + (month / 10u) % 10u);
    initial[5] = (char)('0' + month % 10u);
    initial[6] = (char)('0' + (day / 10u) % 10u);
    initial[7] = (char)('0' + day % 10u);
    initial[8] = '\0';
    snprintf(value, sizeof(value), "%s", initial);
    if (!keyboard_input_initial("格式：YYYYMMDD", "仅限今天或未来日期（8 位）",
                                value, sizeof(value), false, true, false, initial)) return false;
    if (!ptc_ui_parse_date_yyyymmdd(value, ui->model.day_index, out_day_index)) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "日期无效；请输入今天或未来的 8 位日期 YYYYMMDD。");
        return false;
    }
    return true;
}

static bool keyboard_span(UiState *ui, uint16_t current, uint16_t *out_days)
{
    char initial[4];
    char value[4];
    if (!ui || !out_days) return false;
    if (current < 1u) current = 1u;
    if (current > 366u) current = 366u;
    snprintf(initial, sizeof(initial), "%u", (unsigned)current);
    snprintf(value, sizeof(value), "%s", initial);
    if (!keyboard_input_initial("输入持续天数", "范围 1 到 366 天", value, sizeof(value),
                                false, true, false, initial)) return false;
    if (!ptc_ui_parse_span_days(value, out_days)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "持续天数必须是 1 到 366。");
        return false;
    }
    return true;
}

bool edit_date_range_start(UiState *ui, uint16_t *start, uint16_t *end)
{
    uint32_t duration;
    uint16_t next;
    if (!ui || !start || !end) return false;
    duration = *end >= *start ? (uint32_t)*end - *start + 1u : 1u;
    if (!keyboard_date(ui, *start, &next)) return false;
    if ((uint32_t)next + duration - 1u > UINT16_MAX) {
        snprintf(ui->model.message, sizeof(ui->model.message), "该开始日期会使结束日期越界，未修改草稿。");
        return false;
    }
    *start = next;
    *end = (uint16_t)(next + duration - 1u);
    return true;
}

bool edit_date_range_span(UiState *ui, uint16_t start, uint16_t *end)
{
    uint32_t duration = *end >= start ? (uint32_t)*end - start + 1u : 1u;
    uint16_t next;
    if (!ui || !end || !keyboard_span(ui, (uint16_t)duration, &next)) return false;
    if ((uint32_t)start + next - 1u > UINT16_MAX) {
        snprintf(ui->model.message, sizeof(ui->model.message), "持续天数超出可用日期范围，未修改草稿。");
        return false;
    }
    *end = (uint16_t)(start + next - 1u);
    return true;
}

static bool pin_keyboard_fallback(UiState *ui)
{
    char value[PTC_UI_PIN_MAX_DIGITS + 1];
    if (!ui) return false;
    snprintf(value, sizeof(value), "%s", ui->model.pin_text);
    ui->model.pin_keyboard_mode = true;
    if (keyboard_input_initial(ui->model.pin_title, ui->model.pin_guide,
                                value, sizeof(value), true, true, false,
                                ui->model.pin_text)) {
        snprintf(ui->model.pin_text, sizeof(ui->model.pin_text), "%s", value);
        ui->model.pin_error[0] = '\0';
        ui->model.pin_keyboard_mode = false;
        return true;
    }
    ui->model.pin_keyboard_mode = false;
    return false;
}

bool pin_input(UiState *ui, const char *title, const char *guide,
                      char *out, size_t out_size)
{
    int left_latched = -1;
    int right_latched = -1;
    int plus_samples = 0;
    bool plus_was_held = false;
    bool plus_keyboard = false;
    bool touch_was_active = false;
    bool touch_plus_was_held = false;
    bool ignore_touch_until_release = false;
    int touch_x = -1;
    int touch_y = -1;
    int draw_elapsed_ms = DRAW_INTERVAL_MS;
    HidTouchScreenState initial_touch;
    if (!ui || !out || out_size == 0 || !g_active_pad) return false;
    out[0] = '\0';
    ptc_ui_pin_open(&ui->model, title, guide);
    /* A touch that opened this PIN flow belongs to the previous page.  Do not
     * reinterpret it as a PIN digit, cancel, confirm, or keyboard action. */
    ignore_touch_until_release =
        hidGetTouchScreenStates(&initial_touch, 1) && initial_touch.count > 0;
    touch_was_active = ignore_touch_until_release;
    while (appletMainLoop() && !ui->exit_requested) {
        u64 buttons_down;
        u64 buttons_held;
        HidAnalogStickState left;
        HidAnalogStickState right;
        bool touch_active;
        bool touch_allowed;
        HidTouchScreenState touch;
        int left_digit;
        int right_digit;
        int digit;
        padUpdate(g_active_pad);
        buttons_down = padGetButtonsDown(g_active_pad);
        buttons_held = padGetButtons(g_active_pad);
        left = padGetStickPos(g_active_pad, 0);
        right = padGetStickPos(g_active_pad, 1);
        left_digit = ptc_ui_pin_digit_from_vector(left.x, left.y, STICK_DEADZONE);
        right_digit = ptc_ui_pin_digit_from_vector(right.x, right.y, STICK_DEADZONE);
        if (left_digit < 0) left_latched = -1;
        if (right_digit < 0) right_latched = -1;
        if (left_digit >= 0 && left_latched < 0) {
            ptc_ui_pin_append(&ui->model, left_digit);
            left_latched = left_digit;
        } else if (left_digit < 0 && right_digit >= 0 && right_latched < 0) {
            ptc_ui_pin_append(&ui->model, right_digit);
            right_latched = right_digit;
        }
        digit = -1;
        if (buttons_down & HidNpadButton_Up) digit = ptc_ui_pin_digit_from_button(0);
        else if (buttons_down & HidNpadButton_Right) digit = ptc_ui_pin_digit_from_button(1);
        else if (buttons_down & HidNpadButton_Down) digit = ptc_ui_pin_digit_from_button(2);
        else if (buttons_down & HidNpadButton_Left) digit = ptc_ui_pin_digit_from_button(3);
        if (digit >= 0) ptc_ui_pin_append(&ui->model, digit);
        if (buttons_down & HidNpadButton_X) ptc_ui_pin_append(&ui->model, 0);
        if (buttons_down & HidNpadButton_Y) ptc_ui_pin_append(&ui->model, 9);
        if (buttons_down & HidNpadButton_ZL) ptc_ui_pin_backspace(&ui->model);
        if (buttons_down & HidNpadButton_B) {
            ptc_ui_pin_finish(&ui->model);
            return false;
        }

        if (buttons_held & HidNpadButton_Plus) {
            if (!plus_was_held) plus_samples = 0;
            if (plus_samples < PIN_KEYBOARD_HOLD_TICKS) ++plus_samples;
            if (plus_samples >= PIN_KEYBOARD_HOLD_TICKS && !plus_keyboard) {
                plus_keyboard = true;
                (void)pin_keyboard_fallback(ui);
            }
        } else {
            if (plus_was_held && !plus_keyboard) {
                if (ptc_ui_pin_validate(&ui->model)) {
                    snprintf(out, out_size, "%s", ui->model.pin_text);
                    ptc_ui_pin_finish(&ui->model);
                    return true;
                }
            }
            plus_samples = 0;
            plus_keyboard = false;
        }
        plus_was_held = (buttons_held & HidNpadButton_Plus) != 0;

        touch_active = hidGetTouchScreenStates(&touch, 1) && touch.count > 0;
        touch_allowed = ptc_ui_touch_after_entry_allowed(&ignore_touch_until_release, touch_active);
        if (!touch_allowed) {
            if (!touch_active) touch_was_active = false;
        } else if (touch_active) {
            touch_x = (int)touch.touches[0].x;
            touch_y = (int)touch.touches[0].y;
            if (ptc_ui_rect_contains(ptc_ui_pin_confirm_rect(), touch_x, touch_y)) {
                if (!touch_plus_was_held) plus_samples = 0;
                if (plus_samples < PIN_KEYBOARD_HOLD_TICKS) ++plus_samples;
                if (plus_samples >= PIN_KEYBOARD_HOLD_TICKS && !plus_keyboard) {
                    plus_keyboard = true;
                    (void)pin_keyboard_fallback(ui);
                }
                touch_plus_was_held = true;
            } else {
                if (touch_plus_was_held) {
                    /* Sliding away cancels the confirm hold instead of turning
                     * the later release into an unintended short confirm. */
                    plus_samples = 0;
                    plus_keyboard = false;
                    touch_plus_was_held = false;
                } else if (!touch_was_active) {
                    PtcUiHit hit = ptc_ui_hit_test(&ui->model, touch_x, touch_y);
                    if (hit.kind == PTC_UI_HIT_PIN_KEY) ptc_ui_pin_append(&ui->model, hit.index);
                    else if (hit.kind == PTC_UI_HIT_PIN_BACKSPACE) ptc_ui_pin_backspace(&ui->model);
                    else if (hit.kind == PTC_UI_HIT_PIN_CONFIRM && ptc_ui_pin_validate(&ui->model)) {
                        snprintf(out, out_size, "%s", ui->model.pin_text);
                        ptc_ui_pin_finish(&ui->model);
                        return true;
                    } else if (hit.kind == PTC_UI_HIT_PIN_CANCEL) {
                        ptc_ui_pin_finish(&ui->model);
                        return false;
                    } else if (hit.kind == PTC_UI_HIT_PIN_KEYBOARD) {
                        (void)pin_keyboard_fallback(ui);
                    }
                }
            }
        } else if (touch_plus_was_held) {
            if (!plus_keyboard && ptc_ui_pin_validate(&ui->model)) {
                snprintf(out, out_size, "%s", ui->model.pin_text);
                ptc_ui_pin_finish(&ui->model);
                return true;
            }
            plus_samples = 0;
            plus_keyboard = false;
            touch_plus_was_held = false;
        }
        if (touch_allowed) touch_was_active = touch_active;
        draw_elapsed_ms += INPUT_LOOP_MS;
        if (draw_elapsed_ms >= (ui->animating ? DRAW_INTERVAL_FAST_MS : DRAW_INTERVAL_MS)) {
            ui->animating = update_animations(&ui->model);
            draw(ui);
            draw_elapsed_ms = 0;
        }
        svcSleepThread(ui->animating ? INPUT_LOOP_SLEEP_FAST_NS : INPUT_LOOP_SLEEP_NS);
    }
    ptc_ui_pin_finish(&ui->model);
    return false;
}

u64 shortcut_preset_mask(int index)
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

bool shortcut_mask_valid(u64 mask)
{
    return mask != 0 && (mask & ~((u64)CUSTOM_SHORTCUT_VALID_MASK)) == 0;
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
        {HidNpadButton_Plus, "Plus(+)"},
        {HidNpadButton_Minus, "Minus(-)"}
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

void refresh_custom_shortcut_label(UiState *ui)
{
    if (!ui) {
        return;
    }
    format_shortcut_label(ui->model.custom_shortcut_mask,
                          ui->model.custom_shortcut_label,
                          sizeof(ui->model.custom_shortcut_label));
}

void refresh_shortcut_draft_label(UiState *ui)
{
    if (!ui) return;
    format_shortcut_label(ui->model.shortcut_draft_mask,
                          ui->model.shortcut_draft_label,
                          sizeof(ui->model.shortcut_draft_label));
}

void load_ui_preferences(UiState *ui)
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
    ui->model.setup_theme_index = PTC_UI_THEME_SYSTEM;
    ui->model.setup_zone_index = 1;
    ui->theme_preference = PTC_UI_THEME_SYSTEM;
    ui->system_theme = PTC_UI_SYSTEM_THEME_UNAVAILABLE;
    ui->theme_view = ptc_ui_theme_make_view(ui->theme_preference, ui->system_theme);
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
    item = cJSON_GetObjectItemCaseSensitive(root, "theme");
    if (cJSON_IsString(item)) {
        PtcUiThemePreference preference;
        if (ptc_ui_theme_parse_preference(item->valuestring, &preference)) {
            ui->theme_preference = preference;
        }
    }
    ui->theme_view = ptc_ui_theme_make_view(ui->theme_preference, ui->system_theme);
    item = cJSON_GetObjectItemCaseSensitive(root, "setup_wizard_step");
    if (cJSON_IsNumber(item)) {
        cJSON *wizard_version = cJSON_GetObjectItemCaseSensitive(root, "setup_wizard_version");
        ui->model.setup_step = ptc_ui_migrate_setup_step(
            item->valueint, cJSON_IsNumber(wizard_version) ? wizard_version->valueint : 1);
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

bool save_ui_preferences(UiState *ui)
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
    cJSON_DeleteItemFromObject(root, "theme");
    cJSON_AddStringToObject(root, "theme", ptc_ui_theme_preference_name(ui->theme_preference));
    cJSON_DeleteItemFromObject(root, "setup_wizard_step");
    cJSON_AddNumberToObject(root, "setup_wizard_step", ui->model.setup_step);
    cJSON_DeleteItemFromObject(root, "setup_wizard_version");
    cJSON_AddNumberToObject(root, "setup_wizard_version", 4);
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = rendered && ui->client.storage->vtable->write_text_atomic(ui->client.storage, CONFIG_PATH, rendered);
    free(rendered);
    return ok;
}

bool save_setup_step(UiState *ui, int step)
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

void edit_overlay_minutes(UiState *ui)
{
    char guide[96];
    snprintf(guide, sizeof(guide), "分别输入小时和分钟，总计范围 %u 到 %u 分钟",
             (unsigned int)ui->model.minimum_minutes, (unsigned int)ui->model.maximum_minutes);
    ptc_ui_numpad_open(
        &ui->model, PTC_UI_NUMPAD_MINUTES, PTC_UI_OVERLAY_MINUTES,
        ui->model.overlay_title, guide, 4,
        ui->model.minimum_minutes, ui->model.maximum_minutes, ui->model.draft_minutes);
}

void edit_weekly_minutes(UiState *ui)
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
        snprintf(guide, sizeof(guide), "分别输入小时和分钟，总计 1 到 1440 分钟\n仅修改%s的周计划模板", WEEKDAYS[ui->model.editor_index]);
        ptc_ui_numpad_open(
            &ui->model, PTC_UI_NUMPAD_WEEKLY_MINUTES, PTC_UI_OVERLAY_NONE,
            title, guide, 4, 1, 1440, day->minutes);
    }
}
