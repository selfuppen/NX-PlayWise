#include "nro_app_internal.h"

void refresh_setup_activation(UiState *ui)
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
    ui->model.plan_page = PTC_UI_PLAN_PAGE_ROOT;
    ui->model.selected_index = 0;
    snprintf(ui->model.message, sizeof(ui->model.message), "家长区已解锁。进入孩子区请按 B。");
#ifndef PLAYWISE_EDEN
    ptc_hot_reload_inspect(&ui->hot_reload);
    sync_hot_reload_model(ui);
    if (ui->hot_reload.status == PTC_HOT_RELOAD_PENDING && !ui->hot_reload_prompted) {
        ui->hot_reload_prompted = true;
        open_hot_reload_confirmation(ui);
        return;
    }
#endif
    if (ui->model.parent_page == PTC_UI_PARENT_TODAY) {
        submit_status(ui);
    }
}

void enter_parent_area(UiState *ui)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    char pin_confirm[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus state = ptc_companion_auth_state(&ui->auth);
    ui->auth_retry_action = AUTH_RETRY_ENTER_PARENT;
    if (state == PTC_AUTH_EMPTY) {
        if (!pin_input(ui, "设置 任我玩 PIN", "摇杆方向输入；X=0，Y=9；输入内容只显示为圆点。", pin, sizeof(pin)) ||
            !pin_input(ui, "确认 任我玩 PIN", "请再次输入相同的 PIN；输入内容只显示为圆点。", pin_confirm, sizeof(pin_confirm))) {
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
    if (!pin_input(ui, "任我玩 PIN", "摇杆方向输入；X=0，Y=9；输入内容只显示为圆点。", pin, sizeof(pin))) {
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

void select_setup_shortcut(UiState *ui, int index)
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

bool commit_shortcut_preferences(UiState *ui)
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

void open_shortcut_manager(UiState *ui)
{
    if (!ui) return;
    ui->model.shortcut_draft_mask = ui->model.custom_shortcut_mask;
    ui->model.shortcut_draft_enabled = ui->model.custom_shortcut_enabled;
    ui->model.shortcut_draft_show_hint = ui->model.show_parent_shortcut_hint;
    refresh_shortcut_draft_label(ui);
    ui->model.overlay = PTC_UI_OVERLAY_SHORTCUT_MANAGER;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "家长区快捷键管理");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "自定义组合需长按约 400ms；固定 Minus 松开即可进入，无需长按。所有修改按 + 确认后才生效。");
}

void setup_pin(UiState *ui)
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
        if (!pin_input(ui, "修改默认 PIN", "输入新的 1到64 位数字；保留当前 PIN 可按 B 取消。",
                       pin, sizeof(pin)) ||
            !pin_input(ui, "确认新 PIN", "请再次输入相同的 PIN；输入内容只显示为圆点。",
                       pin_confirm, sizeof(pin_confirm))) {
            ui->auth_retry_action = AUTH_RETRY_NONE;
            snprintf(ui->model.message, sizeof(ui->model.message), "已保留当前 PIN。");
            return;
        }
        if (strcmp(pin, pin_confirm) != 0) {
            show_auth_error(ui, "两次 PIN 不一致", "两次输入的新 PIN 不一致，已全部清空，请重新设置。", 0);
            return;
        }
        state = ptc_companion_auth_set_pin(&ui->auth, pin, time(NULL), switch_random, NULL);
        if (state != PTC_AUTH_OK) {
            show_auth_error(ui, "PIN 修改失败", auth_status_zh(state), 0);
            return;
        }
        ui->auth_retry_action = AUTH_RETRY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 strlen(pin) < 4U ? "PIN 已修改；当前仍属于弱保护，建议使用更长 PIN。" : "PIN 已修改。");
        return;
    }
    if (state != PTC_AUTH_EMPTY) {
        show_auth_error(ui, "无法设置 任我玩 PIN", auth_status_zh(state), 0);
        return;
    }
    if (!pin_input(ui, "设置 任我玩 PIN", "输入 1到64 位数字；短 PIN 仅提示风险，不会阻止保存。",
                   pin, sizeof(pin)) ||
        !pin_input(ui, "确认 任我玩 PIN", "请再次输入相同的 PIN；输入内容只显示为圆点。",
                   pin_confirm, sizeof(pin_confirm))) {
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
    if (save_setup_step(ui, PTC_UI_SETUP_THEME)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 strlen(pin) < 4U
                     ? "PIN 已保存；当前 PIN 少于 4 位，容易被猜到，建议修改。"
                     : "PIN 已保存；下一步选择外观主题。");
    }
}

bool ensure_default_setup_pin(UiState *ui)
{
    PtcAuthStatus state;
    if (!ui) return false;
    state = ptc_companion_auth_state(&ui->auth);
    if (state == PTC_AUTH_OK) {
        ui->auth_retry_action = AUTH_RETRY_NONE;
        return true;
    }
    if (state != PTC_AUTH_EMPTY) {
        ui->auth_retry_action = AUTH_RETRY_DEFAULT_SETUP_PIN;
        show_auth_error(ui, "无法创建默认 PIN", auth_status_zh(state), 0);
        return false;
    }
    state = ptc_companion_auth_set_pin(&ui->auth, "110", time(NULL), switch_random, NULL);
    if (state != PTC_AUTH_OK) {
        ui->auth_retry_action = AUTH_RETRY_DEFAULT_SETUP_PIN;
        show_auth_error(ui, "默认 PIN 设置失败", auth_status_zh(state), 0);
        return false;
    }
    ui->auth_retry_action = AUTH_RETRY_NONE;
    snprintf(ui->model.message, sizeof(ui->model.message),
             "已创建默认 PIN 110；这是弱保护，建议在此修改。");
    return true;
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

void setup_previous(UiState *ui)
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

void setup_primary(UiState *ui)
{
    if (!ui) {
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
        if (save_setup_step(ui, PTC_UI_SETUP_PIN)) (void)ensure_default_setup_pin(ui);
        break;
    case PTC_UI_SETUP_PIN:
        if (ensure_default_setup_pin(ui)) {
            ui->model.setup_theme_index = (int)ui->theme_preference;
            (void)save_setup_step(ui, PTC_UI_SETUP_THEME);
        }
        break;
    case PTC_UI_SETUP_THEME:
        if (apply_theme_preference(ui, (PtcUiThemePreference)ui->model.setup_theme_index)) {
            (void)save_setup_step(ui, PTC_UI_SETUP_TAKEOVER);
            snprintf(ui->model.message, sizeof(ui->model.message), "外观主题已保存；下一步确认接管系统控制。");
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message), "主题设置未保存，已恢复原外观；请确认 SD 卡可写。");
        }
        break;
    case PTC_UI_SETUP_TAKEOVER:
        if (ptc_ui_setup_takeover_complete(&ui->model)) {
            ui->model.setup_zone_index = 1;
            if (save_setup_step(ui, PTC_UI_SETUP_ZONE)) {
                snprintf(ui->model.message, sizeof(ui->model.message), "系统控制接管已完成；请选择进入区域。");
            }
        } else if (!ui->waiting) {
            if (ptc_ui_runtime_fingerprint_reconfirmation_needed(&ui->model)) {
                open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "系统环境已变化，重新检测并接管",
                                     "系统版本或运行环境与上次确认时不同。将执行只读兼容预检；通过后保留现有配置并恢复额度管理。");
            } else if (ui->model.disable_flag_present && strcmp(ui->model.setup_phase, "restored") == 0) {
                open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "解除停用并重新接管",
                                     "将重新执行只读兼容预检；仅预检通过后才解除紧急停用并重新启用额度管理。");
            } else {
                open_confirm_overlay(ui, PTC_UI_OPERATION_COMPLETE_SETUP, "确认接管系统控制",
                                     "先执行只读兼容预检；通过后保存安装快照并启用额度管理。");
            }
        }
        break;
    case PTC_UI_SETUP_ZONE:
        finish_setup(ui);
        break;
    default:
        break;
    }
}

void handle_setup_input(UiState *ui, u64 down, u64 held)
{
    (void)held;
    if (!ui || ui->model.view != PTC_UI_SETUP || ui->waiting) {
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
        } else if (down & HidNpadButton_Plus) {
            setup_primary(ui);
        }
    } else if (ui->model.setup_step == PTC_UI_SETUP_PIN && (down & HidNpadButton_X)) {
        setup_pin(ui);
    } else if (ui->model.setup_step == PTC_UI_SETUP_THEME) {
        if (down & HidNpadButton_Left) {
            ui->model.setup_theme_index = ui->model.setup_theme_index <= 0 ? 2 : ui->model.setup_theme_index - 1;
        } else if (down & HidNpadButton_Right) {
            ui->model.setup_theme_index = (ui->model.setup_theme_index + 1) % 3;
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

void open_confirm_overlay(UiState *ui, PtcUiOperation operation, const char *title, const char *body)
{
    ui->model.confirm_return_overlay = ui->model.overlay;
    snprintf(ui->model.confirm_return_title, sizeof(ui->model.confirm_return_title), "%s", ui->model.overlay_title);
    snprintf(ui->model.confirm_return_body, sizeof(ui->model.confirm_return_body), "%s", ui->model.overlay_body);
    ui->model.overlay = PTC_UI_OVERLAY_CONFIRM;
    ui->model.operation = operation;
    ui->model.confirm_hold_required = false;
    ui->model.overlay_selection = 1;
    if (operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
        operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
        operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY) {
        ui->model.overlay_selection = 0;
    }
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s", title);
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s", body);
}

void open_danger_confirm_overlay(UiState *ui, PtcUiOperation operation, const char *title, const char *body)
{
    open_confirm_overlay(ui, operation, title, body);
    ui->model.confirm_hold_required = true;
}

void open_weekly_page(UiState *ui)
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
    ui->model.plan_page = PTC_UI_PLAN_PAGE_WEEKLY;
    ui->model.selected_index = 0;
    for (int slot = 0; slot < 7; ++slot) {
        if (ptc_ui_weekday_for_display_slot(slot) == ui->model.editor_index) {
            ui->model.weekly_grid_slot = slot;
            ui->model.weekly_last_day_slot = slot;
            break;
        }
    }
    submit_status(ui);
    snprintf(ui->model.message, sizeof(ui->model.message), "正在刷新周计划；选择日期后按 A 或点按卡片编辑。");
}
