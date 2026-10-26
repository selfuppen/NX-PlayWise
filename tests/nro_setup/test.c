#include "nro_app_internal.h"
#include "../../common/protocol/request_schema.h"
#include "../../platform/host/mem_storage.h"
#include "../../companion/nro/eden_observe.h"

static int failures;
static int glyph_refreshes;
static SetLanguage system_language = SetLanguage_ENUS;
static int system_focus_state = AppletFocusState_InFocus;
static const char *pin_values[2];
static int pin_cursor;
static int pin_opens;
static bool pin_opened_empty;
static int64_t fixed_time = 1800000000;
static void check(bool condition, const char *label);
static int eden_dispatches;
static PtcEdenRequest *eden_dispatch_slot;
static void test_eden_dispatch(void *ctx, const char *id, const char *json)
{
    ++eden_dispatches;
    check(strcmp(id, "owned-request") == 0 && strcmp(json, "{\"owned\":true}") == 0,
          "Eden dispatch uses its owned immutable request copy");
    check(ctx == eden_dispatch_slot && !ptc_eden_request_submit(eden_dispatch_slot, "reentrant", "{}"),
          "running Eden request cannot be overwritten by reentrant submission");
}
time_t time(time_t *out) { if (out) *out = (time_t)fixed_time; return (time_t)fixed_time; }
Result setsysInitialize(void) { return 0; }
Result setsysGetColorSetId(ColorSetId *out) { *out = ColorSetId_Light; return 0; }
void setsysExit(void) {}
Result setInitialize(void) { return 0; }
Result setGetSystemLanguage(u64 *out) { *out = 1; return 0; }
Result setMakeLanguage(u64 code, SetLanguage *out) { (void)code; *out = system_language; return 0; }
void setExit(void) {}
int appletGetFocusState(void) { return system_focus_state; }
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
    ptc_ui_pin_open(&ui->model, title, guide);
    ++pin_opens;
    pin_opened_empty = ui->model.pin_text[0] == '\0';
    const char *pin = pin_cursor < 2 ? pin_values[pin_cursor++] : NULL;
    if (!pin) { ptc_ui_pin_finish(&ui->model); return false; }
    snprintf(out, size, "%s", pin);
    ptc_ui_pin_finish(&ui->model);
    return true;
}
static void check(bool condition, const char *label)
{ if (!condition) { fprintf(stderr, "FAIL setup: %s\n", label); ++failures; } }
static void test_eden_request_release_boundary(void)
{
    PtcEdenRequest slot = {0};
    char id[] = "owned-request", json[] = "{\"owned\":true}";
    char large[PTC_IPC_MAX_REQUEST_SIZE + 2], long_id[PTC_COMPANION_REQUEST_ID_SIZE + 1];
    eden_dispatches = 0;
    eden_dispatch_slot = &slot;
    check(ptc_eden_request_submit(&slot, id, json), "Eden accepts one bounded request");
    id[0] = 'x'; json[0] = 'x';
    check(!ptc_eden_request_submit(&slot, "second", "{}"), "queued Eden request cannot be overwritten");
    for (int i = 0; i < 100; ++i)
        check(!ptc_eden_request_dispatch(&slot, true, &slot, test_eden_dispatch),
              "held input defers synchronous core work");
    check(eden_dispatches == 0 && slot.state == PTC_EDEN_REQUEST_QUEUED,
          "held input neither executes nor consumes the pending request");
    check(!ptc_eden_request_dispatch(&slot, false, &slot, NULL), "missing processor leaves request pending");
    check(ptc_eden_request_dispatch(&slot, false, &slot, test_eden_dispatch) && eden_dispatches == 1 &&
          slot.state == PTC_EDEN_REQUEST_IDLE && !slot.id[0] && !slot.json[0],
          "neutral input executes once and clears the owned request");
    check(!ptc_eden_request_dispatch(&slot, false, &slot, test_eden_dispatch) && eden_dispatches == 1,
          "dispatch never replays an outcome even when the callback produced no receipt");
    memset(large, 'x', sizeof(large)); large[PTC_IPC_MAX_REQUEST_SIZE] = '\0';
    check(ptc_eden_request_submit(&slot, "limit", large), "Eden accepts the protocol byte limit");
    memset(&slot, 0, sizeof(slot));
    large[PTC_IPC_MAX_REQUEST_SIZE] = 'x'; large[sizeof(large) - 1] = '\0';
    memset(long_id, 'a', sizeof(long_id)); long_id[sizeof(long_id) - 1] = '\0';
    check(!ptc_eden_request_submit(&slot, "limit", large) &&
          !ptc_eden_request_submit(&slot, long_id, "{}") && !ptc_eden_request_submit(&slot, "bad/id", "{}") &&
          !ptc_eden_request_submit(&slot, "empty", "") && !ptc_eden_request_submit(NULL, "none", "{}") &&
          slot.state == PTC_EDEN_REQUEST_IDLE, "invalid or oversized Eden requests never enter the slot");
}
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
    pin_opens = 0;
    pin_opened_empty = false;
    pin_values[0] = pin_values[1] = NULL;
}

static void parent_error_action(UiState *ui, bool touch, u64 button)
{
    if (!touch) {
        check(handle_parent_auth_error_input(ui, button), "entry error consumes pad input");
        return;
    }
    PtcUiRect rect = button == HidNpadButton_A ? ptc_ui_confirm_rect(ui->model.overlay)
        : (button == HidNpadButton_B ? ptc_ui_cancel_rect(ui->model.overlay) : ptc_ui_auth_error_guide_rect());
    check(handle_parent_auth_error_touch(ui, rect.x + rect.w / 2, rect.y + rect.h / 2),
          "entry error consumes touch input at the rendered button");
}

static void init_parent_entry(UiState *ui, PtcMemStorage *mem)
{
    init(ui, mem);
    check(ptc_companion_auth_set_pin(&ui->auth, "1234", fixed_time, switch_random, NULL) == PTC_AUTH_OK,
          "entry PIN fixture");
    ui->model.view = PTC_UI_CHILD;
    ui->model.selected_index = 2;
    ui->model.setup_pin_ready = true;
    pin_values[0] = "9999";
    enter_parent_area(ui);
}

static void test_parent_pin_error(void)
{
    static PtcMemStorage mem; UiState ui;
    const int64_t start = fixed_time;
    for (int touch = 0; touch < 2; ++touch) {
        init_parent_entry(&ui, &mem);
        check(ui.model.view == PTC_UI_CHILD && ui.model.overlay == PTC_UI_OVERLAY_AUTH_ERROR &&
              ui.model.auth_error_parent_entry && ui.auth_retry_action == AUTH_RETRY_ENTER_PARENT &&
              !ui.setup_parent_authorized && !ui.waiting, "wrong PIN shows choices without support navigation or authorization");
        check(strcmp(ui.model.auth_error_title, ptc_ui_text(PTC_UI_T_PIN_IS_INCORRECT)) == 0 &&
              strcmp(ui.model.auth_error_message, ptc_ui_text(PTC_UI_T_PARENT_PIN_ERROR_CHOICE)) == 0,
              "entry error uses its own title and choice message");
        check(ui.model.pin_text[0] == '\0', "failed PIN is cleared");
        pin_cursor = 0;
        parent_error_action(&ui, touch, HidNpadButton_A);
        check(ui.model.overlay == PTC_UI_OVERLAY_AUTH_ERROR && pin_opens == 2 && pin_opened_empty,
              "repeated wrong PIN returns to choices with an empty input");
        parent_error_action(&ui, touch, HidNpadButton_B);
        check(ui.model.view == PTC_UI_CHILD && ui.model.selected_index == 2 && ui.model.overlay == PTC_UI_OVERLAY_NONE &&
              ui.auth_retry_action == AUTH_RETRY_NONE && !ui.model.auth_error_parent_entry,
              "return preserves child page and focus and clears retry");
        pin_cursor = 0; enter_parent_area(&ui);
        pin_cursor = 0; pin_values[0] = "1234";
        parent_error_action(&ui, touch, HidNpadButton_A);
        check(ui.model.view == PTC_UI_PARENT && ui.model.parent_page == PTC_UI_PARENT_TODAY &&
              ui.setup_parent_authorized && !ui.model.parent_support_only && pin_opened_empty,
              "correct retry unlocks the normal today page");

        init_parent_entry(&ui, &mem);
        parent_error_action(&ui, touch, HidNpadButton_X);
        check(ui.model.view == PTC_UI_PARENT && ui.model.parent_page == PTC_UI_PARENT_SUPPORT &&
              ui.model.selected_index == 4 && ui.model.parent_support_only && !ui.setup_parent_authorized &&
              ui.auth_retry_action == AUTH_RETRY_NONE && ui.model.overlay == PTC_UI_OVERLAY_NONE && !ui.waiting,
              "support choice opens read-only diagnostics and clears retry without a guide overlay");
        for (int action = 0; action < 4; ++action)
            check(!ptc_ui_parent_read_only_action(&ui.model, action), "read-only support disallows control actions");
        export_diagnostics(&ui);
        check(ui.model.diagnostic_status == PTC_UI_DIAGNOSTIC_SUCCESS, "support choice permits redacted diagnostics");

        init_parent_entry(&ui, &mem);
        for (int attempt = 1; attempt < 5; ++attempt) {
            pin_cursor = 0;
            parent_error_action(&ui, touch, HidNpadButton_A);
        }
        check(ui.model.auth_cooldown_seconds == 30 && ui.model.overlay == PTC_UI_OVERLAY_AUTH_ERROR && pin_opens == 5,
              "fifth wrong PIN starts the existing 30-second cooldown");
        char before[1024], after[1024];
        mem.storage.vtable->read_text(&mem.storage, APP_ROOT "/auth.json", before, sizeof(before));
        pin_cursor = 0; pin_values[0] = "1234";
        parent_error_action(&ui, touch, HidNpadButton_A);
        check(pin_opens == 5 && !ui.setup_parent_authorized, "cooldown retry cannot open input or authorize");
        parent_error_action(&ui, touch, HidNpadButton_B);
        ptc_companion_auth_init(&ui.auth, APP_ROOT, &mem.storage);
        pin_cursor = 0; enter_parent_area(&ui);
        check(ui.model.auth_cooldown_seconds == 30 && ui.model.auth_error_parent_entry && !ui.setup_parent_authorized,
              "closing and re-entering does not bypass persisted cooldown even with the correct PIN");
        mem.storage.vtable->read_text(&mem.storage, APP_ROOT "/auth.json", after, sizeof(after));
        check(strcmp(before, after) == 0, "cooldown re-entry does not alter persisted failure counters");
        fixed_time = start + 29;
        refresh_auth_error_cooldown(&ui);
        check(ui.model.auth_cooldown_seconds == 1, "cooldown countdown uses fixed time");
        fixed_time = start + 30;
        refresh_auth_error_cooldown(&ui);
        check(ui.model.auth_cooldown_seconds == 0 && ui.model.overlay == PTC_UI_OVERLAY_AUTH_ERROR && pin_opens == 6,
              "cooldown expiry enables retry without opening input automatically");
        pin_cursor = 0;
        parent_error_action(&ui, touch, HidNpadButton_A);
        check(ui.setup_parent_authorized && !ui.model.parent_support_only && pin_opened_empty,
              "explicit correct retry succeeds at the cooldown boundary");
        fixed_time = start;
    }
    init_parent_entry(&ui, &mem);
    snprintf(ui.model.setup_phase, sizeof(ui.model.setup_phase), "failed");
    pin_cursor = 0; pin_values[0] = "1234";
    parent_error_action(&ui, false, HidNpadButton_A);
    check(ui.model.parent_page == PTC_UI_PARENT_SUPPORT && ui.setup_parent_authorized &&
          !ui.model.parent_support_only && ui.model.result_status[0] == '\0',
          "successful retry into support clears the previous authentication error feedback");
    init_parent_entry(&ui, &mem);
    pin_cursor = 0; pin_values[0] = NULL;
    parent_error_action(&ui, false, HidNpadButton_A);
    check(ui.model.view == PTC_UI_CHILD && ui.model.overlay == PTC_UI_OVERLAY_NONE &&
          ui.auth_retry_action == AUTH_RETRY_NONE && !ui.setup_parent_authorized,
          "canceling the retry PIN dialog returns without authorization");
    init_parent_entry(&ui, &mem);
    close_auth_error(&ui, true);
    mem.fail_write_path_contains = "/auth.json";
    pin_cursor = 0; enter_parent_area(&ui);
    check(ui.model.parent_support_only && ui.model.parent_page == PTC_UI_PARENT_SUPPORT &&
          !ui.model.auth_error_parent_entry, "authentication persistence failure retains support fallback");
    mem.fail_write_path_contains = NULL;
    init_parent_entry(&ui, &mem);
    close_auth_error(&ui, true);
    {
        char before[1024], after[1024];
        mem.storage.vtable->read_text(&mem.storage, APP_ROOT "/auth.json", before, sizeof(before));
        fixed_time = 0;
        pin_cursor = 0; pin_values[0] = "1234";
        enter_parent_area(&ui);
        check(ui.model.parent_support_only && !ui.setup_parent_authorized &&
              strcmp(ui.model.message, ptc_ui_text(PTC_UI_T_PIN_CLOCK_UNAVAILABLE)) == 0,
              "unavailable clock cannot authorize and explains the unchanged failure count");
        mem.storage.vtable->read_text(&mem.storage, APP_ROOT "/auth.json", after, sizeof(after));
        check(strcmp(before, after) == 0, "unavailable clock never resets or increments persisted authentication state");
        fixed_time = start;
    }
    mem.storage.vtable->write_text_atomic(&mem.storage, APP_ROOT "/auth.json", "{}");
    enter_parent_area(&ui);
    check(ui.model.parent_support_only && ui.model.overlay == PTC_UI_OVERLAY_NONE &&
          ui.auth_retry_action == AUTH_RETRY_NONE, "invalid credentials retain read-only support fallback");
    init_parent_entry(&ui, &mem);
    close_auth_error(&ui, true);
    ui.model.view = PTC_UI_SETUP;
    pin_cursor = 0; enter_parent_area(&ui);
    check(ui.model.view == PTC_UI_SETUP && ui.model.overlay == PTC_UI_OVERLAY_NONE &&
          ui.auth_retry_action == AUTH_RETRY_NONE && !ui.model.auth_error_parent_entry,
          "setup entry errors retain inline feedback");
    ui.model.view = PTC_UI_PARENT;
    ui.auth_retry_action = AUTH_RETRY_CHANGE_PIN;
    show_auth_error(&ui, ptc_ui_text(PTC_UI_T_PIN_VERIFICATION_FAILED),
                    ptc_ui_text(PTC_UI_T_PIN_IS_INCORRECT_PLEASE_TRY_AGAIN), 0);
    check(!ui.model.auth_error_parent_entry && !handle_parent_auth_error_input(&ui, HidNpadButton_X) &&
          !handle_parent_auth_error_touch(&ui, 640, 470) && ui.auth_retry_action == AUTH_RETRY_CHANGE_PIN,
          "sensitive-operation error keeps its existing handlers and retry action");
}
static void test_bedtime_legacy_optional_windows(void)
{
    static PtcMemStorage mem;
    UiState ui;
    init(&ui, &mem);
    mem.storage.vtable->write_text_atomic(&mem.storage, APP_ROOT "/rules.json",
        "{\"version\":2,\"bedtime_holiday_mode\":\"inherit\",\"bedtime_makeup_mode\":\"inherit\"}");
    load_rule_drafts(&ui);
    check(ui.model.bedtime_policy.holiday_rule.window.enabled &&
          ui.model.bedtime_policy.makeup_workday_rule.window.enabled,
          "legacy absent optional special window flags retain storage defaults");
    ui.model.draft_bedtime_policy = ui.model.bedtime_policy;
    ui.model.draft_bedtime_policy.week[1].start_minute = 1290;
    PtcBedtimePolicy merged = ptc_ui_bedtime_section_policy(&ui.model, PTC_UI_BEDTIME_WEEKLY);
    check(merged.holiday_rule.window.enabled && merged.makeup_workday_rule.window.enabled,
          "saving weekly legacy draft preserves unrelated special window flags");
    mem.storage.vtable->write_text_atomic(&mem.storage, APP_ROOT "/rules.json",
        "{\"version\":2,\"bedtime_holiday_mode\":\"inherit\",\"bedtime_makeup_mode\":\"inherit\","
        "\"bedtime_holiday_enabled\":false,\"bedtime_makeup_enabled\":false}");
    load_rule_drafts(&ui);
    check(!ui.model.bedtime_policy.holiday_rule.window.enabled &&
          !ui.model.bedtime_policy.makeup_workday_rule.window.enabled,
          "explicit false special window flags remain false instead of receiving defaults");
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
    check(ui.model.config_backup_tab == 0 && ui.model.overlay_selection == 13,
          "backup entry defaults to create tab and create action");
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

static void test_today_detail_refresh(void)
{
    static PtcMemStorage mem;
    UiState ui;
    char text[32768], path[320];
    init(&ui, &mem);
    ui.model.view = PTC_UI_CHILD;
    ui.model.setup_wizard_completed = true;
    ui.model.overlay = PTC_UI_OVERLAY_HOME_DETAILS;
    ui.model.home_details_page = 1; ui.model.home_details_focus = 1;
    ui.model.home_details_data_expanded = true; ui.model.home_details_scroll = 160;
    ui.model.weekly_dirty = ui.model.holiday_dirty = ui.model.bedtime_dirty = ui.model.dock_dirty = ui.model.eye_care_dirty = true;
    ui.model.draft_week[0] = (PtcDayRule){PTC_RULE_MODE_LIMIT, 111};
    ui.model.draft_holiday_enabled = true;
    ui.model.draft_holiday_rule = (PtcDayRule){PTC_RULE_MODE_LIMIT, 222};
    ui.model.draft_scheduled_override = (PtcScheduledOverride){.enabled = true, .start_day_index = 1, .end_day_index = 3,
        .rule = {PTC_RULE_MODE_LIMIT, 333}};
    ui.model.draft_bedtime_policy.enabled = true;
    ui.model.draft_dock_policy.undocked_daily_minutes = 444;
    ui.model.draft_eye_care_policy = (PtcEyeCarePolicy){true, 55, 11};
    ui.model.draft_autonomy_policy.daily_buffer_minutes = 15;
    submit_status(&ui);
    check(ui.waiting && ui.model.home_details_refresh_failed, "detail refresh suspends cached projection");
    int requests = 0;
    for (int i = 0; i < PTC_MEM_STORAGE_MAX_FILES; ++i) if (mem.files[i].present && strstr(mem.files[i].path, "/inbox/pending/")) {
        PtcRequest parsed;
        check(ptc_request_parse(mem.files[i].text, &parsed) == PTC_ERR_OK && parsed.type == PTC_REQUEST_STATUS,
            "detail refresh submits only status");
        ++requests;
    }
    check(requests == 1, "one status request submitted");
    char first_id[80]; snprintf(first_id, sizeof(first_id), "%s", ui.active_request_id);
    submit_status(&ui);
    check(strcmp(first_id, ui.active_request_id) == 0, "waiting prevents duplicate status request");
    mem.storage.vtable->write_text_atomic(&mem.storage, APP_ROOT "/results/unrelated.json", "{}");
    for (int i = 0; i < 100; ++i) poll_eden_ready_result(&ui);
    check(ui.waiting && ui.model.waiting && ui.elapsed_ms == 0 && ui.transport.elapsed_ms == 0 &&
          strcmp(first_id, ui.active_request_id) == 0,
          "Eden pre-input poll keeps absent exact receipt pending without accelerating timeout or resubmitting");
    PtcResultState state;
    ptc_result_state_default(&state, 1);
    state.played_minutes_available = true; state.played_minutes = 69;
    ptc_result_ok_json(text, sizeof(text), ui.active_request_id, "status", NULL, false, &state, fixed_time);
    snprintf(path, sizeof(path), APP_ROOT "/results/%s.json", ui.active_request_id);
    mem.storage.vtable->write_text_atomic(&mem.storage, path, text);
    poll_eden_ready_result(&ui);
    check(!ui.waiting && !ui.model.home_details_refresh_failed, "successful refresh resumes projection");
    check(!ui.model.waiting, "ready Eden receipt clears the hit-test waiting gate before HID dispatch");
    check(ui.model.played_minutes_available && ui.model.played_minutes == 69,
        "refresh accepts the new reliable consumption reading");
    check(ui.model.overlay == PTC_UI_OVERLAY_HOME_DETAILS && ui.model.home_details_page == 1 &&
        ui.model.home_details_focus == 1 && ui.model.home_details_data_expanded && ui.model.home_details_scroll == 160,
        "refresh preserves page focus expansion and scroll");
    check(ui.model.draft_week[0].minutes == 111 && ui.model.draft_holiday_rule.minutes == 222 &&
        ui.model.draft_scheduled_override.rule.minutes == 333 && ui.model.draft_bedtime_policy.enabled &&
        ui.model.draft_dock_policy.undocked_daily_minutes == 444 && ui.model.draft_eye_care_policy.play_minutes == 55 &&
        ui.model.draft_autonomy_policy.daily_buffer_minutes == 15, "detail refresh preserves all unsaved plan drafts");
    submit_status(&ui);
    state.played_minutes_available = false; state.played_minutes = -1;
    ptc_result_ok_json(text, sizeof(text), ui.active_request_id, "status", NULL, false, &state, fixed_time);
    snprintf(path, sizeof(path), APP_ROOT "/results/%s.json", ui.active_request_id);
    mem.storage.vtable->write_text_atomic(&mem.storage, path, text);
    ptc_ui_home_details_back(&ui.model); ptc_ui_home_details_back(&ui.model);
    poll_result(&ui, false);
    check(!ui.model.played_minutes_available && ui.model.played_minutes == -1,
        "refreshed unknown consumption does not inherit a cached reading");
    check(ui.model.draft_autonomy_policy.daily_buffer_minutes == 15,
        "leaving details during refresh preserves the unsaved buffer draft");
    check(ptc_ui_open_home_details(&ui.model), "child can reopen details after refresh");
    submit_status(&ui);
    state.day_index = 2;
    ptc_result_ok_json(text, sizeof(text), ui.active_request_id, "status", NULL, false, &state, fixed_time);
    snprintf(path, sizeof(path), APP_ROOT "/results/%s.json", ui.active_request_id);
    mem.storage.vtable->write_text_atomic(&mem.storage, path, text);
    ui.model.played_minutes_available = true; ui.model.played_minutes = 69;
    poll_result(&ui, false);
    check(ui.model.day_index == 2 && !ui.model.played_minutes_available,
        "next-day refresh never carries yesterday's consumption forward");
    submit_status(&ui);
    ptc_result_error_json(text, sizeof(text), ui.active_request_id, "status", NULL, false, PTC_ERR_PCTL_READ_FAILED, &state, fixed_time);
    snprintf(path, sizeof(path), APP_ROOT "/results/%s.json", ui.active_request_id);
    mem.storage.vtable->write_text_atomic(&mem.storage, path, text);
    poll_eden_ready_result(&ui);
    check(ui.model.view == PTC_UI_CHILD && ui.model.overlay == PTC_UI_OVERLAY_HOME_DETAILS &&
        ui.model.home_details_refresh_failed, "backend failure keeps child detail and invalidates estimates");
    mem.fail_writes = true;
    submit_status(&ui);
    check(!ui.waiting && ui.model.view == PTC_UI_CHILD && ui.model.home_details_refresh_failed,
        "submission failure keeps child details");
}

static void test_theme_draft_commit(void)
{
    static PtcMemStorage mem;
    UiState ui, reopened;
    char text[8192];
    init(&ui, &mem);
    check(ui.theme_preference == PTC_UI_THEME_SYSTEM && ui.theme_style == PTC_UI_THEME_STYLE_SLATE,
          "theme draft fixture starts on defaults");

    open_theme_overlay(&ui);
    check(ui.model.overlay == PTC_UI_OVERLAY_THEME && ui.model.theme_draft_valid &&
          ui.model.theme_draft_preference == PTC_UI_THEME_SYSTEM &&
          ui.model.theme_draft_style == PTC_UI_THEME_STYLE_SLATE &&
          ui.model.theme_saved_preference == PTC_UI_THEME_SYSTEM &&
          ui.model.theme_saved_style == PTC_UI_THEME_STYLE_SLATE,
          "theme page opens with both radio groups on the saved pair");
    theme_draft_select_mode(&ui, PTC_UI_THEME_LIGHT);
    theme_draft_select_style(&ui, PTC_UI_THEME_STYLE_TEAL);
    check(ui.theme_preference == PTC_UI_THEME_SYSTEM && ui.theme_style == PTC_UI_THEME_STYLE_SLATE,
          "preview never mutates the saved preferences");
    check(!mem.storage.vtable->exists(&mem.storage, CONFIG_PATH),
          "preview writes no config before confirm");
    check(ui.model.overlay_selection == 3 + PTC_UI_THEME_STYLE_TEAL,
          "style radio takes the shared focus index");
    check(commit_theme_draft(&ui), "theme draft commit succeeds");
    check(ui.theme_preference == PTC_UI_THEME_LIGHT && ui.theme_style == PTC_UI_THEME_STYLE_TEAL &&
          !ui.model.theme_draft_valid, "confirm saves theme mode and style together");
    check(mem.storage.vtable->read_text(&mem.storage, CONFIG_PATH, text, sizeof(text)) &&
          strstr(text, "\"theme\":\"light\"") && strstr(text, "\"theme_style\":\"teal\""),
          "confirm persists both preference keys in one write");
    memset(&reopened, 0, sizeof(reopened));
    reopened.client = ui.client;
    load_ui_preferences(&reopened);
    check(reopened.theme_preference == PTC_UI_THEME_LIGHT &&
          reopened.theme_style == PTC_UI_THEME_STYLE_TEAL,
          "reopen restores the committed theme pair");

    open_theme_overlay(&ui);
    theme_draft_select_mode(&ui, PTC_UI_THEME_DARK);
    mem.fail_writes = true;
    check(!commit_theme_draft(&ui), "failed config write reports an unsaved draft");
    mem.fail_writes = false;
    check(ui.theme_preference == PTC_UI_THEME_LIGHT && ui.theme_style == PTC_UI_THEME_STYLE_TEAL &&
          ui.model.theme_draft_valid && ui.model.theme_draft_preference == PTC_UI_THEME_LIGHT &&
          ui.model.theme_draft_style == PTC_UI_THEME_STYLE_TEAL &&
          ui.model.overlay == PTC_UI_OVERLAY_THEME,
          "failed save restores the saved pair and keeps the page open");
    check(mem.storage.vtable->read_text(&mem.storage, CONFIG_PATH, text, sizeof(text)) &&
          strstr(text, "\"theme\":\"light\""),
          "failed save leaves the persisted theme untouched");

    theme_draft_select_style(&ui, PTC_UI_THEME_STYLE_AMETHYST);
    discard_theme_draft(&ui);
    check(!ui.model.theme_draft_valid && ui.theme_style == PTC_UI_THEME_STYLE_TEAL,
          "discard drops the draft and restores the saved appearance");
}

int main(void)
{
    static PtcMemStorage mem;
    UiState ui, reopened;
    char text[8192];
    test_eden_request_release_boundary();
    init(&ui, &mem);
    ui.model.view = PTC_UI_PARENT;
    ui.model.setup_step = 0;
    snprintf(ui.model.setup_phase, sizeof(ui.model.setup_phase), "pending");
    ptc_ui_return_from_parent(&ui.model);
    check(ui.model.view == PTC_UI_SETUP && ui.model.setup_step == PTC_UI_SETUP_PREPARE,
          "unfinished wizard returns from support at an actionable preparation step");
    handle_setup_input(&ui, HidNpadButton_A, 0);
    check(ui.model.setup_step == PTC_UI_SETUP_PARENT,
          "continue advances the returned wizard through its real input handler");
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
    ui.minus_pending = true;
    system_focus_state = 0;
    applet_hook(AppletHookType_OnFocusState, &ui);
    check(!ui.minus_pending, "losing focus discards a pending standalone Minus press");
    ui.minus_pending = true;
    system_focus_state = AppletFocusState_InFocus;
    applet_hook(AppletHookType_OnFocusState, &ui);
    check(!ui.minus_pending && ui.status_refresh_pending,
          "returning to focus refreshes status without replaying Minus");
    check(ui.status_refresh_origin == PTC_UI_STATUS_FOCUS,
          "focus refresh retains its own provenance");
    ui.minus_pending = true;
    applet_hook(AppletHookType_OnResume, &ui);
    check(!ui.minus_pending, "resume discards a pending standalone Minus press");
    check(ui.status_refresh_origin == PTC_UI_STATUS_RESUME_AND_FOCUS,
          "coalesced focus and resume are not reported as a Y press");
    ui.waiting = true;
    trigger_resume_status_refresh(&ui, NULL);
    check(ui.status_refresh_pending && ui.status_refresh_origin == PTC_UI_STATUS_RESUME_AND_FOCUS,
          "deferred refresh keeps its provenance while another request waits");
    ui.waiting = false;
    trigger_resume_status_refresh(&ui, NULL);
    check(!ui.status_refresh_pending && ui.status_refresh_origin == PTC_UI_STATUS_OTHER && ui.waiting,
          "resume dispatch consumes its cause once without changing status submission");
    test_config_backup_ui();
    test_bedtime_legacy_optional_windows();
    test_today_detail_refresh();
    test_parent_pin_error();
    test_theme_draft_commit();
    {
        PtcUiModel model = {0};
        char text[8192], small[8];
        PtcEdenInputObservation input = {0};
        cJSON *json;
        check(ptc_eden_plan_request_recordable(PTC_REQUEST_SET_BEDTIME_POLICY) &&
              ptc_eden_plan_request_recordable(PTC_REQUEST_IMPORT_HOLIDAY_CALENDAR) &&
              ptc_eden_plan_request_recordable(PTC_REQUEST_ACTIVATE_HOLIDAY_CALENDAR) &&
              !ptc_eden_plan_request_recordable(PTC_REQUEST_OFFLINE_CODE) &&
              !ptc_eden_plan_request_recordable(PTC_REQUEST_STATUS),
              "plan raw evidence whitelist excludes codes and unrelated requests");
        ptc_eden_sample_input(&input, true);
        check(input.sequence == 1 && input.active, "Eden observes a sampled active input without exporting its value");
        ptc_eden_sample_input(&input, false);
        check(input.sequence == 2 && !input.active, "Eden observes a later neutral release");
        input.sequence = UINT64_MAX;
        ptc_eden_sample_input(&input, true);
        check(input.sequence == UINT64_MAX, "Eden input sequence saturates instead of wrapping");
        model.view = PTC_UI_SETUP;
        model.overlay = PTC_UI_OVERLAY_PIN;
        snprintf(model.pin_text, sizeof(model.pin_text), "987654");
        snprintf(model.numpad_text, sizeof(model.numpad_text), "87654321");
        check(ptc_eden_format_ui(&model, text, sizeof(text)), "Eden observation formats a PIN dialog");
        json = cJSON_Parse(text);
        check(json && cJSON_GetObjectItemCaseSensitive(json, "pin_length")->valueint == 6,
              "Eden observation exposes length for bounded input assertions");
        check(!strstr(text, "987654") && !strstr(text, "87654321"), "Eden observation excludes sensitive text");
        check(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "overlay_animating")),
              "new PIN dialog remains animating until its opening frames complete");
        cJSON_Delete(json);
        model.overlay_open_frames = PTC_UI_OVERLAY_OPEN_FRAMES;
        check(ptc_eden_format_ui(&model, text, sizeof(text)), "stable PIN observation formats");
        json = cJSON_Parse(text);
        check(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(json, "overlay_animating")),
              "completed opening frames are stable rather than perpetually animating");
        cJSON_Delete(json);
        model.overlay = PTC_UI_OVERLAY_NONE;
        model.closing_overlay = PTC_UI_OVERLAY_PIN;
        check(ptc_eden_format_ui(&model, text, sizeof(text)), "closing PIN observation formats");
        json = cJSON_Parse(text);
        check(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "overlay_animating")),
              "closing overlay still blocks dependent input");
        cJSON_Delete(json);
        model.closing_overlay = PTC_UI_OVERLAY_NONE;
        check(ptc_eden_format_ui(&model, text, sizeof(text)), "plain page observation formats");
        json = cJSON_Parse(text);
        check(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(json, "overlay_animating")),
              "plain page remains stable with saturated opening frames");
        check(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(json, "plan_page")) &&
              cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(json, "daily_buffer_claimed")),
              "journey observation exposes typed navigation and business projections");
        {
            cJSON *targets = cJSON_GetObjectItemCaseSensitive(json, "targets");
            cJSON *target = cJSON_GetObjectItemCaseSensitive(targets, "dock_7");
            PtcUiRect rect = ptc_ui_dock_field_rect(7);
            check(cJSON_IsArray(target) && cJSON_GetArrayItem(target, 0)->valueint == rect.x + rect.w / 2 &&
                  cJSON_GetArrayItem(target, 1)->valueint == rect.y + rect.h / 2,
                  "Eden mode input target is derived from production layout");
        }
        cJSON_Delete(json);
        {
            PtcEdenInputObservation input = {.sequence = UINT64_MAX, .active = true};
            check(ptc_eden_format_input(text, sizeof(text), &input, 123, 999),
                  "independent HID observation formats before UI presentation");
            json = cJSON_Parse(text);
            check(json && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "input_active")) &&
                  cJSON_GetObjectItemCaseSensitive(json, "input_sampling_version")->valueint == 2 &&
                  cJSON_GetObjectItemCaseSensitive(json, "input_stream_version")->valueint == 1 &&
                  strcmp(cJSON_GetObjectItemCaseSensitive(json, "instance")->valuestring, "7b") == 0 &&
                  strcmp(cJSON_GetObjectItemCaseSensitive(json, "tick")->valuestring, "999") == 0,
                  "independent HID observation preserves typed edge and instance without UI data");
            check(!strstr(text, "ui") && !strstr(text, "pin") && !strstr(text, "mask"),
                  "independent HID observation excludes UI and input contents");
            cJSON_Delete(json);
            input.active = false;
            check(ptc_eden_format_input(text, sizeof(text), &input, 123, 1000) && strstr(text, "\"input_active\":false"),
                  "independent HID observation formats release");
            check(!ptc_eden_format_input(small, sizeof(small), &input, 1, 1) && !small[0] &&
                  !ptc_eden_format_input(text, sizeof(text), NULL, 1, 1) && !text[0],
                  "independent HID observation rejects truncation and missing sample");
            input.active = true;
            check(ptc_eden_format_progress(text, sizeof(text), &input, PTC_EDEN_TRACE_PRESENT_BEGIN, -1,
                    123, 7, 999), "Eden progress trace formats numeric-only stage data");
            json = cJSON_Parse(text);
            check(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "input_active")) &&
                  cJSON_GetObjectItemCaseSensitive(json, "stage")->valueint == PTC_EDEN_TRACE_PRESENT_BEGIN &&
                  cJSON_GetObjectItemCaseSensitive(json, "detail")->valueint == -1 &&
                  strcmp(cJSON_GetObjectItemCaseSensitive(json, "tick")->valuestring, "999") == 0,
                  "Eden progress trace retains types and signed diagnostic result");
            cJSON_Delete(json);
            check(!ptc_eden_format_progress(small, sizeof(small), &input, PTC_EDEN_TRACE_INPUT, 0, 1, 1, 1) && !small[0],
                  "Eden progress trace rejects truncated records");
            check(!ptc_eden_format_progress(text, sizeof(text), &input, 17, 0, 1, 1, 1) && !text[0],
                  "Eden progress trace rejects unknown stage");
        }
        model.parent_page = PTC_UI_PARENT_SUPPORT;
        model.disable_flag_present = true;
        model.recovery_active = false;
        model.diagnostic_status = PTC_UI_DIAGNOSTIC_SUCCESS;
        check(ptc_eden_format_ui(&model, text, sizeof(text)), "support observation formats");
        json = cJSON_Parse(text);
        check(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "disable_flag_present")) &&
              cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(json, "recovery_active")) &&
              cJSON_GetObjectItemCaseSensitive(json, "diagnostic_status")->valueint == PTC_UI_DIAGNOSTIC_SUCCESS,
              "support observation exposes typed recovery and local diagnostic state");
        {
            cJSON *target = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(json, "targets"), "card_3");
            PtcUiRect rect = ptc_ui_support_card_rect(3);
            check(cJSON_GetArrayItem(target, 0)->valueint == rect.x + rect.w / 2 &&
                  cJSON_GetArrayItem(target, 1)->valueint == rect.y + rect.h / 2,
                  "support recovery target uses its own production layout");
        }
        cJSON_Delete(json);
        model.bedtime_section = PTC_UI_BEDTIME_SCHEDULED;
        model.bedtime_dirty = true;
        model.draft_bedtime_policy.scheduled_override.present = true;
        model.draft_bedtime_policy.scheduled_override.rule.mode = PTC_BEDTIME_OVERRIDE_CUSTOM;
        model.draft_bedtime_policy.scheduled_override.rule.window.start_minute = 1380;
        model.forecast[0].rule_source = "statutory_holiday";
        model.forecast[1].rule_source = "987654-sensitive-unknown";
        model.forecast[0].minutes = 90;
        check(ptc_eden_format_ui(&model, text, sizeof(text)), "plan observations fit bounded output");
        json = cJSON_Parse(text);
        check(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "bedtime_dirty")) &&
              cJSON_GetObjectItemCaseSensitive(json, "bedtime_section")->valueint == 2 &&
              cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "bedtime_draft_present")),
              "plan observations expose typed partition and draft state");
        {
            cJSON *forecast = cJSON_GetObjectItemCaseSensitive(json, "forecast");
            cJSON *windows = cJSON_GetObjectItemCaseSensitive(json, "bedtime_windows");
            cJSON *target = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(json, "targets"), "bedtime_field_3");
            PtcUiRect rect = ptc_ui_bedtime_field_rect(PTC_UI_BEDTIME_SCHEDULED, 3);
            check(cJSON_GetArraySize(forecast) == 7 && cJSON_GetArrayItem(cJSON_GetArrayItem(forecast, 0), 3)->valueint == 3 &&
                  cJSON_GetArrayItem(cJSON_GetArrayItem(forecast, 1), 3)->valueint == -1 && !strstr(text, "sensitive-unknown"),
                  "forecast source whitelist rejects unknown strings without exporting text");
            check(cJSON_GetArraySize(windows) == 10 && cJSON_GetArrayItem(cJSON_GetArrayItem(windows, 9), 1)->valueint == 1380,
                  "draft windows include specified-date custom minutes");
            check(cJSON_GetArrayItem(target, 0)->valueint == rect.x + rect.w / 2 &&
                  cJSON_GetArrayItem(target, 1)->valueint == rect.y + rect.h / 2,
                  "specified-date target uses production geometry");
        }
        cJSON_Delete(json);
        check(!ptc_eden_format_ui(&model, small, sizeof(small)) && small[0] == '\0',
              "Eden observation never publishes truncated JSON");
        check(!ptc_eden_format_ui(NULL, text, sizeof(text)), "Eden observation rejects missing model");
    }
    printf("%s: NRO setup integration\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
