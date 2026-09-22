#include "nro_app_internal.h"

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
