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
