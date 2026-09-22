#include "nro_app_internal.h"

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
