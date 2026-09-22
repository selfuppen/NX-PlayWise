#include "nro_app_internal.h"

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
