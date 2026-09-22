#include "nro_app_internal.h"

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
