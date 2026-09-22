#include "nro_app_internal.h"

void open_offline_code_input(UiState *ui)
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
        "输入加时码", "输入家长给你的 8 位码，确认前会先显示加时预览。", 8, 0, 0, 0);
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

void show_pending_redemption(UiState *ui)
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

void poll_pending_redemption(UiState *ui)
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
    (void)load_redemption_history(ui);
    ptc_ui_match_redemption_result(&ui->model);
    ptc_ui_mark_status_updated(&ui->model, ui->model.code_completed_at);
    if (ui->model.code_result_failed) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "%s", ptc_ui_code_failure_guidance(ui->model.error_code));
    } else {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "已恢复并确认上次兑换成功；这枚加时码已经使用，不能再次使用。");
    }
}

bool restore_pending_redemption(UiState *ui)
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

void submit_preview_offline_code(UiState *ui, const char *code)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_preview_offline_code(
        &ui->transport, ui->active_request_id, time(NULL), code);
    set_command_name(ui, "preview_offline_code");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "preview_offline_code", "正在验证加时码并计算生效预览...");
        return;
    }
    ui->waiting = false;
    set_message(ui, "加时码预览失败", status);
}
