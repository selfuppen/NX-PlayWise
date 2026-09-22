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

void refresh_security_state(UiState *ui)
{
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    ui->model.demo_secret_enabled = read_pairing_values(ui, NULL, 0, secret, sizeof(secret)) &&
        ptc_grant_secret_is_demo(secret);
}

bool verify_sensitive_pin(UiState *ui, const char *action)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus status;
    int64_t retry_after = 0;
    const PtcUiOverlay return_overlay = ui ? ui->model.overlay : PTC_UI_OVERLAY_NONE;
    if (!pin_input(ui, "验证 任我玩 管理 PIN", action, pin, sizeof(pin))) {
        if (ui && return_overlay != PTC_UI_OVERLAY_NONE && ui->model.overlay == PTC_UI_OVERLAY_NONE) {
            ui->model.overlay = return_overlay;
        }
        ui->auth_retry_action = AUTH_RETRY_NONE;
        snprintf(ui->model.message, sizeof(ui->model.message), "已取消敏感操作。");
        return false;
    }
    /* pin_input owns the PIN dialog and closes it on return. Sensitive actions
     * must continue in the dialog that initiated authentication. */
    if (return_overlay != PTC_UI_OVERLAY_NONE && ui->model.overlay == PTC_UI_OVERLAY_NONE) {
        ui->model.overlay = return_overlay;
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

bool load_redemption_history(UiState *ui)
{
    char text[PTC_REDEMPTION_HISTORY_FILE_SIZE];
    if (!ui || !ui->client.storage) return false;
    if (!ui->client.storage->vtable->exists(ui->client.storage, REDEMPTION_HISTORY_PATH)) {
        return ptc_ui_apply_redemption_history_text(&ui->model, "");
    }
    if (!ui->client.storage->vtable->read_text(
            ui->client.storage, REDEMPTION_HISTORY_PATH, text, sizeof(text))) {
        ui->model.redemption_history_available = false;
        ui->model.redemption_history_count = 0;
        ui->model.redemption_history_page = 0;
        return false;
    }
    return ptc_ui_apply_redemption_history_text(&ui->model, text);
}

void open_redemption_history(UiState *ui)
{
    (void)load_redemption_history(ui);
    ui->model.overlay = PTC_UI_OVERLAY_REDEMPTION_HISTORY;
    ui->model.confirm_hold_required = false;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "加时码使用记录");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "仅显示 Switch 上成功兑换的最近 100 条；不保存完整加时码或 nonce。");
}

void request_clear_redemption_history(UiState *ui)
{
    if (!ui) return;
    if (ui->model.redemption_history_available && ui->model.redemption_history_count == 0) {
        snprintf(ui->model.message, sizeof(ui->model.message), "当前没有可清空的加时码使用记录。");
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_CLEAR_REDEMPTION_HISTORY;
    if (!verify_sensitive_pin(ui, "清空全部加时码使用记录前，请再次输入本应用 PIN")) return;
    open_danger_confirm_overlay(ui, PTC_UI_OPERATION_CLEAR_REDEMPTION_HISTORY,
        "清空全部加时码使用记录？",
        "此操作不可撤销，但不会清除防重复兑换账本；已经使用的加时码仍然不能再次使用。请长按确认。");
}

void submit_scheduled_override(UiState *ui)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_scheduled_override(&ui->transport,
        ui->active_request_id, time(NULL), &ui->model.draft_scheduled_override);
    set_command_name(ui, "set_scheduled_override");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "set_scheduled_override", "正在保存临时额度计划...");
    else set_message(ui, "临时额度计划提交失败", status);
}

void submit_autonomy_policy(UiState *ui)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_autonomy_policy(&ui->transport,
        ui->active_request_id, time(NULL), &ui->model.draft_autonomy_policy);
    set_command_name(ui, "set_autonomy_policy");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "set_autonomy_policy", "正在保存自主缓冲设置...");
    else set_message(ui, "自主缓冲设置提交失败", status);
}

void submit_bedtime_confirmation(UiState *ui)
{
    char fingerprint[65];
    PtcCompanionStatus status;
    current_environment_fingerprint(ui, fingerprint);
    if (strcmp(fingerprint, "environment-unavailable") == 0) {
        snprintf(ui->model.message, sizeof(ui->model.message),
            "无法读取当前环境指纹，不能确认就寝限制环境。");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_confirm_bedtime_requirements(&ui->transport,
        ui->active_request_id, time(NULL), true, true, 1, fingerprint);
    set_command_name(ui, "confirm_bedtime_requirements");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "confirm_bedtime_requirements", "正在确认任天堂家长控制与当前环境...");
    } else {
        set_message(ui, "就寝限制环境确认失败", status);
    }
}

void submit_bedtime_policy(UiState *ui)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_bedtime_policy(&ui->transport,
        ui->active_request_id, time(NULL), &ui->model.draft_bedtime_policy, true);
    set_command_name(ui, "set_bedtime_policy");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "set_bedtime_policy", "正在保存就寝计划...");
    else set_message(ui, "就寝计划提交失败", status);
}

void submit_bedtime_skip(UiState *ui)
{
    PtcCompanionStatus status;
    uint64_t instance_id = ui->model.pending_bedtime_skip_instance_id;
    if (instance_id == 0) {
        snprintf(ui->model.message, sizeof(ui->model.message), "没有可提交的就寝窗口。");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_skip_bedtime(&ui->transport,
        ui->active_request_id, time(NULL), instance_id);
    set_command_name(ui, "skip_bedtime");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "skip_bedtime", "正在跳过这一次就寝时间...");
    else set_message(ui, "跳过就寝时间提交失败", status);
}

static bool load_activity_history(UiState *ui)
{
    char text[PTC_ACTIVITY_HISTORY_FILE_SIZE];
    if (!ui || !ui->client.storage) return false;
    if (!ui->client.storage->vtable->exists(ui->client.storage, ACTIVITY_HISTORY_PATH)) {
        return ptc_ui_apply_activity_history_text(&ui->model, "");
    }
    if (!ui->client.storage->vtable->read_text(
            ui->client.storage, ACTIVITY_HISTORY_PATH, text, sizeof(text))) {
        ui->model.activity_history_available = false;
        ui->model.activity_history_count = 0;
        ui->model.activity_history_page = 0;
        return false;
    }
    return ptc_ui_apply_activity_history_text(&ui->model, text);
}

void open_activity_history(UiState *ui)
{
    (void)load_activity_history(ui);
    ui->model.overlay = PTC_UI_OVERLAY_ACTIVITY_HISTORY;
    ui->model.confirm_hold_required = false;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "家庭活动记录");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
        "最多保留 200 条规则、加时、自主缓冲和保护事件；统计失败不会影响控制。");
}

void request_clear_activity_history(UiState *ui)
{
    if (!ui) return;
    if (ui->model.activity_history_available && ui->model.activity_history_count == 0) {
        snprintf(ui->model.message, sizeof(ui->model.message), "当前没有可清空的家庭活动记录。");
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_CLEAR_ACTIVITY_HISTORY;
    if (!verify_sensitive_pin(ui, "清空全部家庭活动记录前，请再次输入本应用 PIN")) return;
    open_danger_confirm_overlay(ui, PTC_UI_OPERATION_CLEAR_ACTIVITY_HISTORY,
        "清空全部家庭活动记录？",
        "此操作不可撤销；加时码防重复兑换账本和控制规则保持不变。请长按确认。");
}

bool commit_credential(UiState *ui)
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

void open_credential_manager(UiState *ui, int kind)
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

void edit_credential_input(UiState *ui)
{
    char value[80];
    const char *header = ui->model.credential_kind == 1 ? "输入新设备名" : "输入新加时码密钥";
    const char *guide = ui->model.credential_kind == 1
        ? "1到32 位：字母、数字、-、_"
        : "建议随机生成；手工输入 32到64 个非空白 ASCII 字符";
    if (!keyboard_input(header, guide, value, sizeof(value), ui->model.credential_kind == 2, false, false)) return;
    snprintf(ui->model.credential_new, sizeof(ui->model.credential_new), "%s", value);
}

void randomize_credential(UiState *ui)
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

void request_save_credential(UiState *ui)
{
    bool valid = ui->model.credential_kind == 1
        ? ptc_device_id_valid(ui->model.credential_new)
        : ptc_grant_secret_valid(ui->model.credential_new);
    if (!valid) {
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 ui->model.credential_kind == 1
                    ? "设备名必须为 1到32 位，只能包含字母、数字、- 和 _。"
                    : "密钥必须为 32到64 个非空白可打印 ASCII 字符；建议使用随机生成。");
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

void change_parent_pin(UiState *ui)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    char confirm[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus status;
    ui->auth_retry_action = AUTH_RETRY_CHANGE_PIN;
    if (!verify_sensitive_pin(ui, "修改 PIN 前，请先输入当前任我玩 PIN")) return;
    if (!pin_input(ui, "修改 PlayWise PIN", "请输入新的 1到64 位数字。", pin, sizeof(pin)) ||
        !pin_input(ui, "确认新 PIN", "请再次输入相同的 PIN；输入内容只显示为圆点。", confirm, sizeof(confirm))) {
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

void show_grant_manager(UiState *ui, int selection)
{
    ui->model.overlay = PTC_UI_OVERLAY_GRANT_MANAGER;
    ui->model.overlay_selection = selection >= 0 && selection < PTC_UI_GRANT_MANAGER_COUNT ? selection : 0;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "加时码生成管理");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "管理生成加时码所需的设备信息、导出配置和二维码网页地址。");
}

void open_grant_manager(UiState *ui)
{
    ui->model.message[0] = '\0';
    show_grant_manager(ui, PTC_UI_GRANT_MANAGER_DEVICE);
}

void open_local_grant(UiState *ui)
{
    uint16_t maximum = PTC_TOKEN_V2_MAX_MINUTES;
    if (!read_pairing_config(ui, ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), &maximum)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "读取本机生成器配置失败。");
        return;
    }
    ui->model.grant_max_minutes = maximum;
    ui->model.grant_minutes = legal_grant_minutes(20U, maximum);
    ui->model.grant_notice[0] = '\0';
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
             "选时长、生成，再把代码告诉孩子。");
    submit_status(ui);
    if (!ui->waiting) ui->model.grant_status_refresh_failed = true;
}

void edit_pairing_base_url(UiState *ui)
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

void apply_default_pairing_base_url(UiState *ui)
{
    if (!save_pairing_base_url(ui, PTC_PAIRING_BASE_URL)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "恢复官方二维码地址失败。");
    } else {
        snprintf(ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), "%s", PTC_PAIRING_BASE_URL);
        snprintf(ui->model.message, sizeof(ui->model.message), "已恢复二维码跳转默认地址。");
    }
    show_grant_manager(ui, PTC_UI_GRANT_MANAGER_RESET_URL);
}

void request_reset_pairing_base_url(UiState *ui)
{
    char body[320];
    ui->auth_retry_action = AUTH_RETRY_RESET_URL;
    if (!verify_sensitive_pin(ui, "恢复二维码跳转默认地址前，请再次输入本应用 PIN")) return;
    snprintf(body, sizeof(body), "将当前二维码跳转地址恢复为默认地址：\n%s", PTC_PAIRING_BASE_URL);
    open_confirm_overlay(ui, PTC_UI_OPERATION_RESET_PAIRING_URL,
                         "恢复二维码跳转默认地址", body);
}

void generate_local_grant_code(UiState *ui)
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
    if (ui->waiting) return;
    ui->model.grant_notice[0] = '\0';
    if (!ui->model.status_loaded) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), "无法确认设备日期，请先关闭弹层并刷新设备状态。");
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_GENERATE_CODE;
    if (!verify_sensitive_pin(ui, "本机生成加时码前，请再次输入本应用 PIN")) return;
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret)) ||
        ptc_token_v2_tier_for_minutes(ui->model.grant_minutes, &tier) != PTC_ERR_OK) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), "配对信息无效，请返回加时码生成管理检查配置。");
        return;
    }
    load_consumed_nonces(ui->model.day_index, consumed);
    if (!load_issued_nonces(ui, ui->model.day_index, issued)) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), "读取签发记录失败，请返回检查 SD 卡后重试。");
        return;
    }
    randomGet(&start, sizeof(start));
    found = ptc_token_v2_find_available_nonce(consumed, issued, start, &nonce);
    if (!found) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice),
                 "今日已达 512 枚上限，请明日再试；旧码未撤销。");
        return;
    }
    if (ptc_token_v2_encode(tier, nonce, device, secret, ui->model.day_index, next_code) != PTC_ERR_OK) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), "生成失败，请返回检查配置后重试。");
        return;
    }
    issued[nonce] = true;
    if (!save_issued_nonces(ui, ui->model.day_index, issued)) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice),
                 "保存失败，请返回检查 SD 卡空间后重试；旧码未撤销。");
        return;
    }
    snprintf(ui->model.grant_code, sizeof(ui->model.grant_code), "%s", next_code);
    ui->model.grant_day_index = ui->model.day_index;
    ui->model.grant_has_code = true;
    ui->model.grant_issued_minutes = ui->model.grant_minutes;
    ui->model.grant_estimate_minutes = ptc_ui_grant_estimate_remaining(
        &ui->model, ui->model.grant_minutes, &ui->model.grant_estimate_capped);
    ui->model.grant_estimate_available = ui->model.grant_estimate_minutes >= 0 &&
        ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL));
    ui->model.grant_estimate_unrestricted = ui->model.unrestricted_today == 1;
    ui->model.grant_estimated_at = (int64_t)time(NULL);
    ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
    snprintf(ui->model.message, sizeof(ui->model.message), "已在本机生成今天有效的 %u 分钟加时码。",
             (unsigned int)ui->model.grant_minutes);
}

void show_pairing_qr(UiState *ui)
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

void export_parent_import(UiState *ui)
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
             ok ? "已导出到 " APP_ROOT "/parent-import.json；把文件导入家长网页。文件包含密钥，请妥善保管。"
                : "生成家长网页导入文件失败。");
}

void reveal_current_credential(UiState *ui)
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

void dispatch_auth_retry(UiState *ui, AuthRetryAction action)
{
    if (!ui) return;
    switch (action) {
    case AUTH_RETRY_ENTER_PARENT: enter_parent_area(ui); break;
    case AUTH_RETRY_DEFAULT_SETUP_PIN: (void)ensure_default_setup_pin(ui); break;
    case AUTH_RETRY_SETUP_PIN: setup_pin(ui); break;
    case AUTH_RETRY_SAVE_CREDENTIAL: request_save_credential(ui); break;
    case AUTH_RETRY_CHANGE_PIN: change_parent_pin(ui); break;
    case AUTH_RETRY_EDIT_URL: edit_pairing_base_url(ui); break;
    case AUTH_RETRY_RESET_URL: request_reset_pairing_base_url(ui); break;
    case AUTH_RETRY_GENERATE_CODE: generate_local_grant_code(ui); break;
    case AUTH_RETRY_SHOW_QR: show_pairing_qr(ui); break;
    case AUTH_RETRY_EXPORT_CONFIG: export_parent_import(ui); break;
    case AUTH_RETRY_REVEAL_CREDENTIAL: reveal_current_credential(ui); break;
    case AUTH_RETRY_CLEAR_REDEMPTION_HISTORY: request_clear_redemption_history(ui); break;
    case AUTH_RETRY_CLEAR_ACTIVITY_HISTORY: request_clear_activity_history(ui); break;
    case AUTH_RETRY_SKIP_BEDTIME:
        ui->model.parent_page = PTC_UI_PARENT_TODAY;
        ui->model.selected_index = 4;
        handle_parent_action(ui);
        break;
    case AUTH_RETRY_NONE:
    default:
        break;
    }
}

void export_diagnostics(UiState *ui)
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
    snprintf(ui->model.message, sizeof(ui->model.message), "正在导出诊断包...");
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
                 APP_ROOT "/support/diagnostic-%lld.json", (long long)exported_at);
        ui->model.diagnostic_status = PTC_UI_DIAGNOSTIC_SUCCESS;
        snprintf(ui->model.message, sizeof(ui->model.message), "诊断包导出成功。");
    } else {
        ui->model.diagnostic_status = PTC_UI_DIAGNOSTIC_ERROR;
        snprintf(ui->model.message, sizeof(ui->model.message), "生成诊断包失败。");
    }
    free(rendered);
}
