/* Uses the deterministic host storage/PCTL fixtures from test_host_core.c. */
#include "../../platform/config_backup.h"
#include "../../sysmodule/sysmodule_internal.h"

static bool backup_random(void *ctx, uint8_t *out, size_t size)
{ return fixed_random(out, size, ctx); }

static void backup_fixture(PtcMemStorage *mem, PtcPctlStub *pctl, PtcFakeTime *clock, PtcSysmodule *core)
{
    uint8_t seed = 9;
    PtcCompanionAuth auth;
    seed_active_buffer_fixture(mem, pctl, clock, core, 60, 10, true);
    ptc_companion_auth_init(&auth, "app", &mem->storage);
    check_int(ptc_companion_auth_set_pin(&auth, "1234", 1, fixed_random, &seed), PTC_AUTH_OK, "backup real PIN fixture");
    
    PtcRuntimeState state;
    check_true(load_state(core, &state), "load backup state");
    state.buffer_claimed = true; state.buffer_claim_day_index = 2380; state.buffer_claimed_minutes = 10;
    state.dock_tracking_started = true; state.dock_day_index = 2380; state.undocked_used_ns = 60000000000ULL * 7;
    check_true(save_state(core, &state, clock->snapshot.unix_seconds), "seed non-migrating eligibility");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/ledger/used_nonces.jsonl",
        "{\"day_index\":2380,\"nonce\":4660}\n"), "seed current replay ledger");
}

static void backup_restore_request(PtcSysmodule *core, const char *id, const char *stage_id, const char *digest, unsigned groups)
{
    char request[512];
    snprintf(request, sizeof(request), "{\"version\":1,\"request_id\":\"%s\",\"type\":\"restore_config_backup\","
        "\"created_at\":1,\"payload\":{\"groups\":%u,\"stage_id\":\"%s\",\"sha256\":\"%s\"}}", id, groups, stage_id, digest);
    process_request_text(core, request, id);
}

static void test_config_backup(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    PtcSysmodule *core = malloc(sizeof(*core));
    PtcPctlStub pctl; PtcFakeTime clock;
    char stage[256], digest[65], original[6144], text[32768], pin[1024];
    check_true(mem && core, "allocate backup tests"); if (!mem || !core) goto done;
    backup_fixture(mem, &pctl, &clock, core);
    check_true(ptc_config_stage_path(stage, sizeof(stage), "app", "backup-stage"), "request-owned stage");
    process_request_text(core,
        "{\"version\":1,\"request_id\":\"backup-stage\",\"type\":\"create_config_backup\",\"created_at\":1,\"payload\":{}}", "backup-stage");
    check_true(ptc_config_stage_validate(&mem->storage, stage, NULL), "backend complete consistent snapshot");
    check_true(ptc_config_archive_save(&mem->storage, stage, "app/backups/config-backup.json"), "archive all config");
    check_true(mem->storage.vtable->read_text(&mem->storage, "app/backups/config-backup.json", text, sizeof(text)) &&
        strstr(text, "grant_secret") && strstr(text, "pin_salt") && !strstr(text, "used_nonces"), "archive scope includes secrets, excludes ledger");
    mem->fail_write_path_contains_once = "backups/config-backup.json";
    check_true(!ptc_config_archive_save(&mem->storage, stage, "app/backups/config-backup.json"), "failed overwrite refused");
    char retained[32768];
    check_true(mem->storage.vtable->read_text(&mem->storage, "app/backups/config-backup.json", retained, sizeof(retained)) &&
        !strcmp(text, retained), "failed atomic overwrite retains original");
    check_true(ptc_config_archive_load(&mem->storage, "app/backups/config-backup.json", stage, digest), "archive parse and split");
    check_true(mem->storage.vtable->read_text(&mem->storage, "app/auth.json", pin, sizeof(pin)) &&
        ptc_companion_auth_backup_pin_matches(pin, "1234") && !ptc_companion_auth_backup_pin_matches(pin, "1235"), "source PIN verified without mutation");
    cJSON *old = ptc_config_read_json(&mem->storage, "app", "rules.json", 6144);
    PtcRules source_rules; ptc_rules_default(&source_rules);
    for (int i = 0; i < 7; ++i) source_rules.week[i].minutes = 90;
    source_rules.today_override.present = true; source_rules.today_override.day_index = 2380;
    source_rules.today_override.rule.minutes = 100;
    source_rules.scheduled_override.enabled = true; source_rules.scheduled_override.start_day_index = 2380;
    source_rules.scheduled_override.end_day_index = 2381; source_rules.scheduled_override.rule.minutes = 110;
    source_rules.autonomy_policy.daily_buffer_minutes = 15;
    source_rules.holiday_enabled = true; source_rules.holiday_rule.minutes = 120;
    source_rules.eye_care.play_minutes = 45; source_rules.dock_policy.undocked_daily_minutes = 50;
    check_true(save_rules(core, &source_rules), "write full source policy");
    cJSON *source = ptc_config_read_json(&mem->storage, "app", "rules.json", 6144);
    for (unsigned group = 1; group <= PTC_CONFIG_DOCK; group <<= 1) {
        cJSON *merged = ptc_config_merge_rules(old, source, group, 2380);
        check_true(merged != NULL, "each rule group independently merges");
        for (const cJSON *v = source->child; merged && v; v = v->next) {
            const cJSON *expected = (ptc_config_rule_group(v->string) == group) ? v : cJSON_GetObjectItemCaseSensitive(old, v->string);
            if (expected && strcmp(v->string, "version") && !strstr(v->string, "confirmation") && !strstr(v->string, "confirmed") && !strstr(v->string, "risk_accepted")) check_true(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(merged, v->string), expected, true), "unselected rule values retained");
        }
        cJSON_Delete(merged);
    }
    cJSON *expired = ptc_config_merge_rules(old, source, PTC_CONFIG_TODAY, 2381);
    check_true(!expired, "expired today group refused"); cJSON_Delete(expired);
    cJSON *defaults = ptc_config_merge_rules(source, old, PTC_CONFIG_EYE | PTC_CONFIG_DOCK | PTC_CONFIG_BEDTIME, 2380);
    PtcRules decoded;
    check_true(ptc_config_rules_decode(defaults, &decoded) && !decoded.eye_care.enabled && !decoded.dock_policy.force_docked &&
        !decoded.bedtime.enabled, "old rules restore safe missing-field defaults");
    cJSON_Delete(defaults); cJSON_Delete(source); cJSON_Delete(old);

    /* Each fresh fixture injects a failure after staging, so rollback must also
       preserve usage, eligibility and the current replay ledger. */
    for (int fault = 0; fault < 6; ++fault) {
        backup_fixture(mem, &pctl, &clock, core);
        PtcRules r; check_true(load_rules(core, &r), "load restore source");
        for (int i = 0; i < 7; ++i) r.week[i].minutes = 90;
        check_true(save_rules(core, &r) && ptc_config_stage_create(&mem->storage, "app", stage, 1) &&
            ptc_config_manifest_digest(&mem->storage, stage, digest), "stage restore fixture");
        for (int i = 0; i < 7; ++i) r.week[i].minutes = 60;
        check_true(save_rules(core, &r) && mem->storage.vtable->read_text(&mem->storage, "app/rules.json", original, sizeof(original)), "save destination before restore");
        if (fault == 1) mem->fail_write_path_contains_once = "app/rules.json";
        if (fault == 2) { pctl.write_error = PTC_ERR_PCTL_WRITE_FAILED; pctl.apply_target_fail_on_call = pctl.apply_target_calls + 1; }
        if (fault == 3) pctl.read_fails_after_apply = true;
        if (fault == 4) mem->fail_write_path_contains_once = "results/restore-test.json";
        if (fault == 5) digest[0] = digest[0] == 'a' ? 'b' : 'a';
        backup_restore_request(core, "restore-test", "backup-stage", digest, PTC_CONFIG_WEEK);
        check_true(mem->storage.vtable->read_text(&mem->storage, "app/rules.json", text, sizeof(text)), "destination remains readable");
        if (fault) check_true(!strcmp(original, text), "failed restore has no partial rule import");
        else check_true(load_rules(core, &r) && r.week[0].minutes == 90 && r.autonomy_policy.daily_buffer_minutes == 10, "selected week immediately applies, buffer retained");
        PtcRuntimeState state; check_true(load_state(core, &state) && state.buffer_claimed && state.buffer_claimed_minutes == 10 &&
            state.undocked_used_ns == 60000000000ULL * 7, "restore preserves buffer eligibility and non-TV usage");
        check_int(pctl.played_minutes_today, 20, "restore never clears played time");
        check_true(mem->storage.vtable->read_text(&mem->storage, "app/ledger/used_nonces.jsonl", text, sizeof(text)) && strstr(text, "4660"), "restore never consumes nonce");
    }
    backup_fixture(mem, &pctl, &clock, core);
    check_true(ptc_config_stage_create(&mem->storage, "app", stage, 1) && ptc_config_manifest_digest(&mem->storage, stage, digest), "stage security source");
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage, "app/config.json",
        "{\"version\":1,\"device_id\":\"destination\",\"theme\":\"dark\",\"setup_wizard_completed\":true}"), "destination pairing/preferences");
    uint8_t seed = 55; core->config_random = backup_random; core->config_random_ctx = &seed;
    backup_restore_request(core, "restore-security", "backup-stage", digest, PTC_CONFIG_PREFS | PTC_CONFIG_PIN | PTC_CONFIG_PAIRING);
    check_true(mem->storage.vtable->read_text(&mem->storage, "app/results/restore-security.json", text, sizeof(text)) && strstr(text,"\"status\":\"ok\"") &&
        !strstr(text,"grant_secret") && !strstr(text,"pin_hash") && !strstr(text,"theme"), "security restore results expose no secrets or theme");
    check_true(mem->storage.vtable->read_text(&mem->storage, "app/config.json", text, sizeof(text)) && strstr(text,"kid-switch") &&
        strstr(text,"setup_wizard_completed"), "pairing name restored, local setup preserved");
    check_true(mem->storage.vtable->read_text(&mem->storage, "app/credentials.json", text, sizeof(text)) && !strstr(text,"0123456789abcdef"), "restoring pairing always generates a new secret");
    check_true(mem->storage.vtable->read_text(&mem->storage, "app/auth.json", text, sizeof(text)) && ptc_companion_auth_backup_pin_matches(text,"1234"), "selected PIN restored");

    /* Failure while building the final result must not commit an error result
       or discard recovery evidence when PCTL rollback is unavailable. */
    for (int rollback_fault = 0; rollback_fault < 2; ++rollback_fault) {
        backup_fixture(mem, &pctl, &clock, core);
        check_true(ptc_config_stage_create(&mem->storage,"app",stage,1) &&
            ptc_config_manifest_digest(&mem->storage,stage,digest), "stage final-read failure source");
        check_true(mem->storage.vtable->write_text_atomic(&mem->storage,"app/config.json",
            "{\"version\":1,\"device_id\":\"kid-switch\",\"theme\":\"dark\"}") &&
            mem->storage.vtable->read_text(&mem->storage,"app/config.json",original,sizeof(original)), "destination preference preimage");
        pctl.read_status_fail_on_call = pctl.read_status_calls + 1;
        if (rollback_fault) pctl.restore_error = PTC_ERR_PCTL_WRITE_FAILED;
        backup_restore_request(core,"restore-final-read","backup-stage",digest,PTC_CONFIG_PREFS);
        check_true(mem->storage.vtable->read_text(&mem->storage,"app/config.json",text,sizeof(text)) &&
            !strcmp(original,text), "final-read failure rolls preferences back");
        check_true(mem->storage.vtable->read_text(&mem->storage,"app/results/restore-final-read.json",text,sizeof(text)) &&
            strstr(text,"\"status\":\"error\""), "final-read failure reports error");
        check_true(recovery_path_exists(core) == (rollback_fault != 0), "failed rollback retains recovery transaction");
        if (rollback_fault) {
            check_true(mem->storage.vtable->exists(&mem->storage,"app/flags/disable.flag") &&
                mem->storage.vtable->exists(&mem->storage,"app/recovery/active/config-input/manifest.json"), "protection retains private import material");
            pctl.restore_error = PTC_ERR_OK;
            ptc_sysmodule_init(core,"app",&mem->storage,&pctl.pctl,&clock.provider);
            check_int(ptc_sysmodule_bootstrap_setup(core),1,"restart retries failed import rollback");
            check_true(!recovery_path_exists(core),"successful restart recovery clears journal");
        }
        check_int(pctl.played_minutes_today,20,"final-read failure retains played time");
    }

    /* Calendars include all saved catalog references and the independent active selection. */
    for (int fault = 0; fault < 3; ++fault) {
        backup_fixture(mem, &pctl, &clock, core);
        const char *calendar = "{\"format_version\":1,\"region_id\":\"JP\",\"region_name\":\"Japan test\",\"year\":2028,\"holidays\":[],\"workdays\":[]}";
        PtcCalendarIndex catalog = {0}, active = {0}; char hash[65], library[256];
        ptc_calendar_sha256_hex(calendar, strlen(calendar), hash);
        check_true(mem->storage.vtable->write_text_atomic(&mem->storage,"app/calendar-import/JP-2028.json",calendar) &&
            ptc_calendar_import_file(&mem->storage,"app","JP-2028.json",hash,&catalog) &&
            ptc_calendar_index_save(&mem->storage,"app",false,&catalog) &&
            ptc_calendar_build_selection(&catalog,"JP",&active) && ptc_calendar_index_save(&mem->storage,"app",true,&active), "save catalog and selected calendar");
        check_true(ptc_config_stage_create(&mem->storage,"app",stage,1) && ptc_config_manifest_digest(&mem->storage,stage,digest), "backup calendar documents");
        snprintf(library,sizeof(library),"app/calendars/library/JP-2028-%s.json",hash);
        check_true(mem->storage.vtable->remove_path(&mem->storage,library) && mem->storage.vtable->remove_path(&mem->storage,"app/calendars/catalog.json") &&
            mem->storage.vtable->remove_path(&mem->storage,"app/calendars/active.json"), "destination uses builtin calendar");
        if (fault == 1) mem->fail_write_path_contains_once = "app/calendars/active.json";
        if (fault == 2) mem->fail_write_path_contains_once = "results/restore-calendar.json";
        backup_restore_request(core,"restore-calendar","backup-stage",digest,PTC_CONFIG_HOLIDAY);
        check_true(ptc_calendar_index_load(&mem->storage,"app",true,&active,NULL) &&
            !strcmp(active.option_id,fault ? "builtin-cn" : "JP"), "calendar restore or complete rollback");
        check_true(mem->storage.vtable->exists(&mem->storage,library) == !fault, "calendar additions are journaled and removed on rollback");
    }
    backup_fixture(mem,&pctl,&clock,core);
    check_true(ptc_config_stage_create(&mem->storage,"app",stage,1), "validation snapshot");
    cJSON *manifest = ptc_config_read_json(&mem->storage,stage,"manifest.json",PTC_CONFIG_BACKUP_MANIFEST_SIZE);
    cJSON *version = cJSON_GetObjectItemCaseSensitive(manifest,"version"); cJSON_SetNumberValue(version,2);
    check_true(ptc_config_write_json(&mem->storage,stage,"manifest.json",manifest) && !ptc_config_stage_validate(&mem->storage,stage,NULL), "unsupported archive version refused");
    cJSON_SetNumberValue(version,1);
    cJSON *files = cJSON_GetObjectItemCaseSensitive(manifest,"files");
    cJSON *file = cJSON_GetArrayItem(files,0); cJSON_ReplaceItemInObjectCaseSensitive(file,"path",cJSON_CreateString("../config.json"));
    check_true(ptc_config_write_json(&mem->storage,stage,"manifest.json",manifest) && !ptc_config_stage_validate(&mem->storage,stage,NULL), "path traversal refused");
    cJSON_Delete(manifest);
    cJSON *duplicates = cJSON_Parse("{\"week\":[],\"week\":[]}");
    check_true(!ptc_config_json_valid(duplicates,0), "duplicate keys refused"); cJSON_Delete(duplicates);
    cJSON *invalid = cJSON_Parse("{\"version\":1,\"device_id\":\"kid\",\"theme\":\"invalid\"}");
    check_true(!ptc_config_preferences_valid(invalid), "preference enum bounds refused"); cJSON_Delete(invalid);
    check_true(mem->storage.vtable->write_text_atomic(&mem->storage,"app/rules.json","[{}]") &&
        !ptc_config_stage_create(&mem->storage,"app",stage,1), "non-object source rules refused without dereferencing array keys");

    /* Simulate power loss after each published configuration preimage. */
    for (int phase = 0; phase < 4; ++phase) {
        backup_fixture(mem, &pctl, &clock, core);
        PtcRequest request = {0}; strcpy(request.request_id, "restore-interrupted");
        check_true(recovery_begin(core, &request, clock.snapshot), "begin interrupted config transaction");
        const char *names[] = {"rules.json", "config.json", "auth.json", "credentials.json"};
        for (int i = 0; i <= phase; ++i) check_true(mem->storage.vtable->write_text_atomic(&mem->storage,
            (snprintf(text,sizeof(text),"app/%s",names[i]),text), "{}"), "publish interrupted file");
        ptc_sysmodule_init(core, "app", &mem->storage, &pctl.pctl, &clock.provider);
        check_int(ptc_sysmodule_bootstrap_setup(core), 1, "restart recovers interrupted import");
        PtcRules r; check_true(load_rules(core, &r) && r.week[0].minutes == 60 &&
            mem->storage.vtable->read_text(&mem->storage,"app/auth.json",text,sizeof(text)) && ptc_companion_auth_backup_pin_matches(text,"1234"), "restart restores every config preimage");
    }
done:
    free(core); free(mem);
}
