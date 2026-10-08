#include "nro_app_internal.h"
#include "../../common/protocol/request_schema.h"
#include "../../platform/host/mem_storage.h"

static int failures;
static int glyph_refreshes;
static SetLanguage system_language = SetLanguage_ENUS;
static const char *pin_values[2];
static int pin_cursor;
static int64_t fixed_time = 1800000000;
time_t time(time_t *out) { if (out) *out = (time_t)fixed_time; return (time_t)fixed_time; }
Result setsysInitialize(void) { return 0; }
Result setsysGetColorSetId(ColorSetId *out) { *out = ColorSetId_Light; return 0; }
void setsysExit(void) {}
Result setInitialize(void) { return 0; }
Result setGetSystemLanguage(u64 *out) { *out = 1; return 0; }
Result setMakeLanguage(u64 code, SetLanguage *out) { (void)code; *out = system_language; return 0; }
void setExit(void) {}
int appletGetFocusState(void) { return AppletFocusState_InFocus; }
void randomGet(void *out, size_t size) { memset(out, 0x35, size); }
void ptc_ui_graphics_language_changed(void) { ++glyph_refreshes; }
int64_t ptc_ui_anim_now_ms(void) { return fixed_time * 1000; }
void refresh_security_state(UiState *ui) { (void)ui; }
void cancel_bedtime_navigation(UiState *ui) { (void)ui; }
void finish_bedtime_navigation(UiState *ui) { (void)ui; }
void finish_quota_recheck(UiState *ui, bool success) { (void)ui; (void)success; }
void finish_today_limit_refresh(UiState *ui, bool success) { (void)ui; (void)success; }
void update_weekly_dirty(UiState *ui) { (void)ui; }
void update_holiday_dirty(UiState *ui) { (void)ui; }
void update_bedtime_dirty(UiState *ui) { (void)ui; }
void show_pending_redemption(UiState *ui) { (void)ui; }
void calendar_manager_refresh(UiState *ui) { (void)ui; }
void open_redemption_history(UiState *ui) { (void)ui; }
void open_activity_history(UiState *ui) { (void)ui; }
void open_offline_code_input(UiState *ui) { (void)ui; }
bool load_redemption_history(UiState *ui) { (void)ui; return false; }
void handle_today_action_ready(UiState *ui, int index) { (void)ui; (void)index; }
void apply_pending_navigation(UiState *ui) { (void)ui; }
void submit_eye_care_policy(UiState *ui) { (void)ui; }
void save_dock_from_page(UiState *ui) { (void)ui; }
void request_dock_waiver(UiState *ui) { (void)ui; }
void save_bedtime_from_page(UiState *ui) { (void)ui; }
bool pin_input(UiState *ui, const char *title, const char *guide, char *out, size_t size)
{
    (void)ui; (void)title; (void)guide;
    const char *pin = pin_cursor < 2 ? pin_values[pin_cursor++] : NULL;
    if (!pin) return false;
    snprintf(out, size, "%s", pin);
    return true;
}
static void check(bool condition, const char *label)
{ if (!condition) { fprintf(stderr, "FAIL setup: %s\n", label); ++failures; } }
static void init(UiState *ui, PtcMemStorage *mem)
{
    ptc_mem_storage_init(mem);
    memset(ui, 0, sizeof(*ui));
    ui->pending_parent_page = ui->pending_bedtime_section = -1;
    ptc_companion_file_client_init(&ui->client, APP_ROOT, &mem->storage);
    ptc_companion_transport_init(&ui->transport, APP_ROOT, &mem->storage, NULL, NULL);
    ptc_companion_auth_init(&ui->auth, APP_ROOT, &mem->storage);
    mem->storage.vtable->write_text_atomic(&mem->storage, APP_ROOT "/auth.json",
        "{\"version\":1,\"pin_hash\":\"\",\"pin_salt\":\"\",\"hash\":\"hmac-sha256\"}");
    load_ui_preferences(ui);
    ui->model.view = PTC_UI_SETUP;
    ui->model.setup_step = PTC_UI_SETUP_PREPARE;
    pin_cursor = 0;
    pin_values[0] = pin_values[1] = NULL;
}
static void test_config_backup_ui(void)
{
    static PtcMemStorage mem; UiState ui; char stage[256], text[8192], path[320];
    init(&ui,&mem);
    check(ptc_companion_auth_set_pin(&ui.auth,"1234",fixed_time,switch_random,NULL) == PTC_AUTH_OK,"backup UI PIN");
    mem.storage.vtable->write_text_atomic(&mem.storage,APP_ROOT "/config.json","{\"version\":1,\"device_id\":\"kid\",\"theme\":\"light\"}");
    mem.storage.vtable->write_text_atomic(&mem.storage,APP_ROOT "/credentials.json","{\"version\":1,\"grant_secret\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}");
    mem.storage.vtable->write_text_atomic(&mem.storage,APP_ROOT "/rules.json",
        "{\"version\":1,\"week\":[{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},"
        "{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60},{\"mode\":\"limit\",\"minutes\":60}],\"today_override_day_index\":2}");
    ui.model.view = PTC_UI_PARENT; ui.model.parent_page = PTC_UI_PARENT_SETTINGS;
    ui.model.selected_index = 7; ui.model.day_index = 1; ui.model.status_loaded = true;
    ui.model.status_updated_at = fixed_time;
    check(ptc_config_stage_path(stage,sizeof(stage),APP_ROOT,"ui-backup") && ptc_config_stage_create(&mem.storage,APP_ROOT,stage,fixed_time) &&
        ptc_config_archive_save(&mem.storage,stage,APP_ROOT "/backups/config-backup.json"),"UI backup fixture");
    open_config_backup(&ui);
    check(ui.model.config_backup_ready && ui.model.config_groups == 507 && !ui.model.config_today_available,"ordinary defaults exclude expired today and security");
    config_backup_action(&ui,2); check(ui.model.config_groups == 507,"expired today cannot toggle");
    config_backup_action(&ui,11); check(ui.model.config_groups == 2043,"all selects available groups");
    config_backup_action(&ui,12); check(ui.model.config_groups == 0,"clear selection");
    config_backup_action(&ui,14); check(ui.model.overlay == PTC_UI_OVERLAY_CONFIG_BACKUP && !ui.waiting,"empty import disabled");
    config_backup_action(&ui,8);
    pin_cursor=0; pin_values[0]="1234";
    request_config_restore(&ui);
    check(ui.model.overlay == PTC_UI_OVERLAY_CONFIRM && ui.model.confirm_hold_required,"import requires hold after current PIN");
    ptc_ui_cancel_overlay(&ui.model);
    check(ui.model.overlay == PTC_UI_OVERLAY_CONFIG_BACKUP && !ui.waiting,"cancel hold returns to backup without submission");
    config_backup_action(&ui,9); pin_cursor=0; pin_values[0]="1234"; pin_values[1]="9999";
    request_config_restore(&ui);
    check(ui.model.overlay == PTC_UI_OVERLAY_AUTH_ERROR && !ui.waiting,"incorrect source PIN cannot confirm import");
    close_auth_error(&ui, true);
    pin_cursor=0; pin_values[0]=pin_values[1]="1234";
    request_config_restore(&ui);
    check(ui.model.overlay == PTC_UI_OVERLAY_CONFIRM && ui.model.confirm_hold_required,"source PIN authorizes restore");
    ptc_ui_cancel_overlay(&ui.model);
    ui.model.recovery_active=true;
    request_config_restore(&ui); check(!ui.waiting && ui.model.overlay == PTC_UI_OVERLAY_CONFIG_BACKUP,"recovery blocks import");
    ui.model.recovery_active=false;
    submit_config_backup(&ui,true);
    snprintf(path,sizeof(path),APP_ROOT "/inbox/pending/%s.json",ui.active_request_id);
    PtcRequest request;
    check(mem.storage.vtable->read_text(&mem.storage,path,text,sizeof(text)) && ptc_request_parse(text,&request) == PTC_ERR_OK &&
        request.type == PTC_REQUEST_RESTORE_CONFIG_BACKUP && !strstr(text,"1234") && !strstr(text,"pin_hash") && !strstr(text,"theme"),"NRO submits only valid metadata IPC");
    ui.waiting=false; ui.model.waiting=false; config_backup_action(&ui,15);
    check(ui.model.overlay == PTC_UI_OVERLAY_NONE && ui.model.selected_index == 7,"backup back restores settings card focus");

    snprintf(ui.active_request_id,sizeof(ui.active_request_id),"ui-create-failed");
    check(ptc_config_stage_path(stage,sizeof(stage),APP_ROOT,ui.active_request_id) &&
        ptc_config_stage_create(&mem.storage,APP_ROOT,stage,fixed_time),"failed save stage fixture");
    snprintf(path,sizeof(path),"%s/manifest.json",stage);
    snprintf(ui.model.result_type,sizeof(ui.model.result_type),"create_config_backup");
    snprintf(ui.model.result_status,sizeof(ui.model.result_status),"ok");
    mem.fail_write_path_contains_once="backups/config-backup.json";
    config_backup_result(&ui);
    check(!mem.storage.vtable->exists(&mem.storage,path) &&
        mem.storage.vtable->exists(&mem.storage,APP_ROOT "/backups/config-backup.json"),"failed save removes owned secret stage and retains backup");

    open_config_backup(&ui);
    ui.config_submitted_groups=PTC_CONFIG_PIN; ui.setup_parent_authorized=true;
    snprintf(ui.model.result_type,sizeof(ui.model.result_type),"restore_config_backup");
    check(ptc_config_stage_path(stage,sizeof(stage),APP_ROOT,ui.config_stage_id),"source PIN preview stage");
    snprintf(path,sizeof(path),"%s/manifest.json",stage);
    config_backup_result(&ui);
    check(ui.model.view == PTC_UI_CHILD && ui.model.overlay == PTC_UI_OVERLAY_NONE &&
        !ui.setup_parent_authorized && !ui.config_stage_id[0] && !mem.storage.vtable->exists(&mem.storage,path),
        "PIN restore leaves parent session and cleans owned preview");
}

int main(void)
{
    static PtcMemStorage mem;
    UiState ui, reopened;
    char text[8192];
    init(&ui, &mem);
    check(ui.language_preference == PTC_UI_LANGUAGE_SYSTEM, "new installation follows system");
    ui.model.overlay = PTC_UI_OVERLAY_CALENDAR_MANAGER;
    snprintf(ui.model.overlay_title, sizeof(ui.model.overlay_title), "Calendar manager");
    open_confirm_overlay(&ui, PTC_UI_OPERATION_IMPORT_CALENDAR, "Import", "Preview checked");
    check(ui.model.overlay == PTC_UI_OVERLAY_CONFIRM && !ui.model.confirm_hold_required,
          "calendar import opens the real ordinary confirmation");
    ptc_ui_cancel_overlay(&ui.model);
    check(ui.model.overlay == PTC_UI_OVERLAY_CALENDAR_MANAGER &&
          strcmp(ui.model.overlay_title, "Calendar manager") == 0,
          "cancel confirmation restores calendar manager and title");
    open_danger_confirm_overlay(&ui, PTC_UI_OPERATION_ACTIVATE_CALENDAR, "Apply", "May restrict");
    check(ui.model.overlay == PTC_UI_OVERLAY_CONFIRM && ui.model.confirm_hold_required,
          "calendar activation uses the real hold confirmation");
    ptc_ui_cancel_overlay(&ui.model);
    ptc_ui_cancel_overlay(&ui.model);

    handle_setup_input(&ui, HidNpadButton_Down, 0);
    handle_setup_input(&ui, HidNpadButton_Down, 0);
    handle_setup_input(&ui, HidNpadButton_A, 0);
    check(ui.model.setup_focus == 2 && ui.model.overlay == PTC_UI_OVERLAY_SETUP_PCTL_HELP &&
          ui.model.setup_step == PTC_UI_SETUP_PREPARE && !ui.waiting && !ui.active_request_id[0],
          "controller opens parental control help without advancing or submitting setup");
    ptc_ui_cancel_overlay(&ui.model);
    handle_setup_input(&ui, HidNpadButton_Down, 0);
    handle_setup_input(&ui, HidNpadButton_A, 0);
    check(ui.model.setup_focus == 3 && ui.model.setup_time_help,
          "clock help remains reachable after adding parental control help");
    ui.model.setup_focus = 0;

    refresh_language(&ui);
    for (int i = 0; i < 4; ++i) {
        check(apply_language_preference(&ui, (PtcUiLanguagePreference)i), "language preference saves");
        check(ptc_ui_language_get_resolved() == (i == 0 ? PTC_UI_LANGUAGE_ENGLISH : (PtcUiLanguagePreference)i),
              "selected language immediately resolves");
    }
    check(glyph_refreshes > 0, "language changes clear font cache");
    mem.fail_writes = true;
    check(!apply_language_preference(&ui, PTC_UI_LANGUAGE_TRADITIONAL), "language save failure reported");
    check(ui.language_preference == PTC_UI_LANGUAGE_TRADITIONAL &&
          ptc_ui_language_get_resolved() == PTC_UI_LANGUAGE_TRADITIONAL, "unsaved language remains for session");
    setup_primary(&ui);
    check(ui.model.setup_step == PTC_UI_SETUP_PARENT && ui.model.overlay == PTC_UI_OVERLAY_NONE,
          "progress and PIN write failures do not block next step or open error modal");
    setup_primary(&ui);
    check(ui.model.setup_step == PTC_UI_SETUP_CONFIRM, "PIN not ready does not block confirmation");
    finish_setup(&ui, false);
    check(ui.model.setup_wizard_completed && ui.model.view == PTC_UI_PARENT && ui.model.parent_support_only &&
          ui.model.parent_page == PTC_UI_PARENT_SUPPORT && ui.model.selected_index == 4,
          "failed persistence still opens read-only support focused on diagnostics");
    ptc_ui_setup_sync(&ui.model);
    check(ui.model.view == PTC_UI_PARENT, "status sync cannot reopen completed guide");
    mem.fail_writes = false;
    export_diagnostics(&ui);
    check(ui.model.diagnostic_status == PTC_UI_DIAGNOSTIC_SUCCESS, "diagnostic export works without backend or PIN");
    check(mem.storage.vtable->read_text(&mem.storage, ui.model.diagnostic_path, text, sizeof(text)) &&
          strstr(text, "onboarding") && strstr(text, "language_save") && strstr(text, "progress_save") &&
          strstr(text, "pin_setup") && strstr(text, "\"status_loaded\":false") && !strstr(text, "pin_hash"),
          "export captures safe setup failures and missing status");
    check(save_ui_preferences(&ui), "completion marker eventually saves");
    memset(&reopened, 0, sizeof(reopened));
    reopened.client = ui.client;
    load_ui_preferences(&reopened);
    check(reopened.model.setup_wizard_completed && reopened.model.setup_step == 0, "reopen preserves skipped guide completion");
    mem.storage.vtable->write_text_atomic(&mem.storage, CONFIG_PATH,
        "{\"version\":1,\"setup_wizard_version\":4,\"setup_wizard_step\":0,\"ui_language\":\"en\"}");
    load_ui_preferences(&reopened);
    snprintf(reopened.model.setup_phase, sizeof(reopened.model.setup_phase), "protection");
    ptc_ui_setup_sync(&reopened.model);
    check(reopened.model.setup_wizard_completed && reopened.model.setup_step == 0,
          "legacy completion survives protection and unavailable backend");
    init(&ui, &mem);
    setup_primary(&ui);
    check(ui.model.setup_pin_ready && ptc_companion_auth_state(&ui.auth) == PTC_AUTH_OK, "default PIN is created");
    pin_values[0] = "1234"; pin_values[1] = "5678"; pin_cursor = 0;
    setup_pin(&ui);
    check(ui.model.setup_step == PTC_UI_SETUP_PARENT && ui.model.overlay == PTC_UI_OVERLAY_NONE,
          "mismatched PINs remain inline");
    check(ptc_companion_auth_verify_pin(&ui.auth, "110", fixed_time, NULL) == PTC_AUTH_OK, "mismatched PINs preserve old PIN");
    setup_primary(&ui);
    setup_primary(&ui);
    char request_id[80];
    snprintf(request_id, sizeof(request_id), "%s", ui.active_request_id);
    check(ui.waiting && ui.model.setup_activation_pending, "activation submits once");
    setup_primary(&ui);
    check(strcmp(request_id, ui.active_request_id) == 0, "repeat activation does not submit again");
    ui.elapsed_ms = REQUEST_TIMEOUT_MS;
    poll_result(&ui, false);
    check(ui.waiting && ui.model.setup_activation_pending && ui.model.setup_wizard_completed &&
          ui.model.parent_page == PTC_UI_PARENT_SUPPORT, "activation timeout enters support while retaining pending result");
    export_diagnostics(&ui);
    check(ui.model.diagnostic_status == PTC_UI_DIAGNOSTIC_SUCCESS, "export is available during activation");
    char path[240];
    snprintf(path, sizeof(path), APP_ROOT "/results/%s.json", request_id);
    snprintf(text, sizeof(text), "{\"version\":1,\"request_id\":\"%s\",\"type\":\"complete_setup\",\"status\":\"ok\","
        "\"state\":{\"day_index\":1,\"limited_today\":1,\"blocked_today\":0,\"unrestricted_today\":0,"
        "\"remaining_available\":true,\"remaining_minutes\":60,\"play_timer_enabled\":1,\"restricted_now\":0},"
        "\"setup\":{\"phase\":\"active\"},\"completed_at\":1800000000}", request_id);
    check(mem.storage.vtable->write_text_atomic(&mem.storage, path, text), "late activation result seeded");
    {
        cJSON *wrong_type = cJSON_Parse(text);
        cJSON_DeleteItemFromObject(wrong_type, "type");
        cJSON_AddStringToObject(wrong_type, "type", "status");
        char *wrong_text = cJSON_PrintUnformatted(wrong_type);
        mem.storage.vtable->write_text_atomic(&mem.storage, path, wrong_text);
        free(wrong_text);
        cJSON_Delete(wrong_type);
        poll_result(&ui, false);
        check(ui.waiting && ui.model.setup_activation_pending && strcmp(ui.active_request_id, request_id) == 0,
              "unrelated result type retains original activation request");
        mem.storage.vtable->write_text_atomic(&mem.storage, path, text);
    }
    poll_result(&ui, false);
    check(!ui.waiting && !ui.model.setup_activation_pending && strcmp(ui.model.setup_phase, "active") == 0 &&
          ui.model.parent_page == PTC_UI_PARENT_SUPPORT && ui.model.diagnostic_status == PTC_UI_DIAGNOSTIC_SUCCESS,
          "late result is read using original request and preserves diagnostic view");
    check(strcmp(request_id, ui.active_request_id) == 0, "late result never resubmits activation");
    init(&ui, &mem);
    setup_primary(&ui); setup_primary(&ui); setup_primary(&ui);
    snprintf(path, sizeof(path), APP_ROOT "/results/%s.json", ui.active_request_id);
    snprintf(text, sizeof(text), "{\"version\":1,\"request_id\":\"%s\",\"type\":\"complete_setup\",\"status\":\"error\","
        "\"error\":{\"code\":401,\"reason\":\"pctl_read_failed\",\"message\":\"PCTL read failed\"},"
        "\"state\":{\"day_index\":1,\"limited_today\":-1,\"blocked_today\":-1,\"unrestricted_today\":-1,"
        "\"remaining_available\":false,\"remaining_minutes\":-1,\"play_timer_enabled\":-1,\"restricted_now\":-1},"
        "\"setup\":{\"phase\":\"failed\"},\"completed_at\":1800000000}", ui.active_request_id);
    mem.storage.vtable->write_text_atomic(&mem.storage, path, text);
    poll_result(&ui, false);
    check(!ui.waiting && !ui.model.setup_activation_pending && ui.model.error_code == 401 &&
          ui.model.setup_wizard_completed && ui.model.parent_page == PTC_UI_PARENT_SUPPORT &&
          strcmp(ui.model.setup_phase, "active") != 0, "activation failure finishes guide without claiming active controls");
    init(&ui, &mem);
    setup_primary(&ui); setup_primary(&ui);
    ui.setup_parent_authorized = false;
    pin_values[0] = "999"; pin_cursor = 0;
    setup_primary(&ui);
    check(!ui.waiting && ui.model.parent_support_only && ui.model.parent_page == PTC_UI_PARENT_SUPPORT &&
          ui.active_request_id[0] == '\0', "failed authentication cannot submit activation but opens support");
    export_diagnostics(&ui);
    check(ui.model.diagnostic_status == PTC_UI_DIAGNOSTIC_SUCCESS,
          "authentication failure still permits redacted diagnostics");
    init(&ui, &mem);
    setup_primary(&ui); setup_primary(&ui); setup_primary(&ui);
    {
        PtcResultState state;
        ptc_result_state_default(&state, 1);
        ptc_result_ok_json(text, sizeof(text), ui.active_request_id, "complete_setup", NULL, false, &state, fixed_time);
        snprintf(path, sizeof(path), APP_ROOT "/results/%s.json", ui.active_request_id);
        mem.storage.vtable->write_text_atomic(&mem.storage, path, text);
        snprintf(ui.model.setup_phase, sizeof(ui.model.setup_phase), "active");
        poll_result(&ui, false);
        check(!ui.waiting && ui.model.parent_page == PTC_UI_PARENT_SUPPORT && ui.model.setup_phase[0] == '\0',
              "missing activation evidence cannot reuse an old active phase");
    }
    test_config_backup_ui();
    printf("%s: NRO setup integration\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
