#include "../../sysmodule/reason_notification.h"
#include "../../companion/notification_text.h"

static PtcReasonObservation reason_sample(uint64_t rev, PtcRestrictionReason reason, uint64_t instance, int64_t wall)
{
    PtcReasonObservation s = {0};
    s.revision = rev; s.reliable = true; s.restricted = reason != PTC_REASON_NONE;
    s.day = 2380; s.captured_at = wall; s.reason = reason; s.instance = instance;
    return s;
}

static void test_reason_events(void)
{
    PtcReasonEvent e;
    PtcReasonObservation s;
    for (int reason = PTC_REASON_DAILY; reason <= PTC_REASON_DOCK; ++reason) {
        ptc_reason_event_init(&e, "r1", "0123456789abcdef");
        s = reason_sample(1, (PtcRestrictionReason)reason, 9, 100);
        check_true(ptc_reason_event_update(&e, "r1", "0123456789abcdef", &s, 1000, 100, true, false), "first reliable restriction");
        ptc_reason_event_attempted(&e, 1000);
        for (int i = 0; i < 200; ++i) {
            s.revision++; s.captured_at = 101;
            check_true(!ptc_reason_event_update(&e, "r1", "0123456789abcdef", &s, 2000, 101, true, false), "status storm deduplicated");
        }
        s = reason_sample(s.revision+1, PTC_REASON_EYE, 10, 102);
        check_true(!ptc_reason_event_update(&e, "r1", "0123456789abcdef", &s, 3000, 102, true, false), "handoff rate limited");
        s = reason_sample(s.revision+1, PTC_REASON_BEDTIME, 11, 110);
        check_true(ptc_reason_event_update(&e, "r1", "0123456789abcdef", &s, 11000, 110, true, false), "latest fresh handoff coalesced");
        ptc_reason_event_attempted(&e, 11000);
        s = reason_sample(s.revision+1, PTC_REASON_NONE, 0, 111);
        check_true(!ptc_reason_event_update(&e, "r1", "0123456789abcdef", &s, 12000, 111, true, false), "release has no can-play notification");
        s = reason_sample(s.revision+1, PTC_REASON_EYE, 12, 120);
        check_true(ptc_reason_event_update(&e, "r1", "0123456789abcdef", &s, 21000, 120, true, false), "new break same day");
    }
    ptc_reason_event_init(&e, "r1", "0123456789abcdef");
    s = reason_sample(1, PTC_REASON_DAILY, 1, 100);
    check_true(!ptc_reason_event_update(&e,"wrong","0123456789abcdef",&s,1,100,true,false),"wrong release rejected");
    check_true(!ptc_reason_event_update(&e,"r1","wrong",&s,2,100,true,false),"wrong boot rejected");
    s.revision++; s.reliable=false;
    check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,3,100,true,false),"failed observation rejected");
    s.revision++; s.reliable=true;
    check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,4,102,true,false),"TTL exact boundary rejected");
    s.revision++; s.captured_at=103;
    check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,5,103,true,true),"visible own details suppressed");
    s.revision++;
    check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,6,103,true,false),"no replay on leaving details");
    s.revision++; s.instance=2;
    check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,7,103,false,false),"disabled suppresses current instance");
    s.revision++; s.instance=3;
    check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,6,102,true,false),"clock rollback suppresses");

    /* A -> B -> A is a real handoff even when B could not be published inside
       the ten-second limit. The final A must still be freshly confirmed. */
    ptc_reason_event_init(&e, "r1", "0123456789abcdef");
    s = reason_sample(1, PTC_REASON_DAILY, 1, 100);
    check_true(ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,1000,100,true,false),"publish initial daily instance");
    ptc_reason_event_attempted(&e,1000);
    s = reason_sample(2, PTC_REASON_BEDTIME, 2, 102);
    check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,3000,102,true,false),"intermediate bedtime waits for rate limit");
    check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,11000,110,true,false),"expired bedtime cannot publish when rate limit ends");
    s = reason_sample(3, PTC_REASON_DAILY, 1, 111);
    check_true(ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,12000,111,true,false),"fresh return to same daily instance is a handoff");
    ptc_reason_event_attempted(&e,12000);
    s.revision++;
    check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,12250,111,true,false),"returned instance is deduplicated after attempt");

    for (int cancel = 0; cancel < 6; ++cancel) {
        ptc_reason_event_init(&e,"r1","0123456789abcdef");
        s = reason_sample(1,PTC_REASON_DAILY,1,100);
        (void)ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,1000,100,true,false);
        ptc_reason_event_attempted(&e,1000);
        s = reason_sample(2,PTC_REASON_BEDTIME,2,102);
        (void)ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,3000,102,true,false);
        s = reason_sample(3,PTC_REASON_DAILY,1,104);
        check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,5000,104,true,false),"return before deadline is coalesced rather than sent early");
        if (cancel == 1) {
            s.revision++; s.reliable=false;
            (void)ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,6000,105,true,false);
        } else if (cancel == 2) {
            (void)ptc_reason_event_update(&e,"foreign","0123456789abcdef",&s,6000,105,true,false);
        } else if (cancel == 3) {
            /* Entering details must discard a queued reminder even after the
               policy sample has aged; leaving must not replay it. */
            (void)ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,9000,108,true,true);
        } else if (cancel == 4) {
            (void)ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,4000,103,true,false);
        } else if (cancel == 5) {
            s.revision++; s.reason=PTC_REASON_NONE;
            (void)ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,6000,105,true,false);
        } else {
            check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,11000,110,true,false),"a coalesced return still cannot reuse an expired snapshot");
        }
        s = reason_sample(s.revision+1,PTC_REASON_DAILY,1,111);
        bool ready = ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,12000,111,true,false);
        check_true(ready == (cancel == 0),"fresh final observation preserves handoff intent unless cancelled by unknown identity or details");
    }
    ptc_reason_event_init(&e,"r1","0123456789abcdef");
    s=reason_sample(1,PTC_REASON_DAILY,1,100);
    (void)ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,1000,100,true,false);
    ptc_reason_event_attempted(&e,1000);
    s=reason_sample(2,PTC_REASON_BEDTIME,2,100);
    check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,4000,103,true,false),"stale intermediate snapshot is never accepted as a transition");
    s=reason_sample(3,PTC_REASON_DAILY,1,110);
    check_true(!ptc_reason_event_update(&e,"r1","0123456789abcdef",&s,11000,110,true,false),"stale intermediate cannot manufacture a return notification");
}

static void notify_tick(PtcReasonNotification *n, PtcReasonObservation *s, uint64_t ms, int64_t wall, bool on)
{
    ptc_reason_notification_tick(n, s, ms, wall, on, false, false, PTC_UI_LANGUAGE_SIMPLIFIED);
}

static void test_reason_transport(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    PtcReasonNotification n, restarted;
    PtcReasonObservation s = reason_sample(1, PTC_REASON_EYE, 5, 100);
    char text[4096], original[1536], path[256], combo[96];
    check_true(mem != NULL,"allocate notification fixture"); if (!mem) return;
    ptc_mem_storage_init(mem);
    PtcStorage *storage = &mem->storage;
    check_true(storage->vtable->write_text_atomic(storage,"sdmc:/config/ultrahand/config.ini","[ultrahand]\nnotifications=true\n"),"external config fixture");
    check_true(storage->vtable->write_text_atomic(storage,"sdmc:/config/ultrahand/overlays.ini","[playwise.ovl]\nkey_combo=L+DDOWN\nuse_launch_args=false\n"),"shortcut fixture");
    storage->vtable->write_text_atomic(storage,"app/ledger/used_nonces.jsonl","business sentinel");
    ptc_reason_notification_init(&n,storage,"app","release1","0123456789abcdef");
    notify_tick(&n,&s,1000,100,false);
    check_true(!n.pending && n.sequence==0,"default off never publishes");
    s.revision++; s.instance++;
    notify_tick(&n,&s,1250,100,true);
    check_true(n.pending && n.sequence==1,"atomic publish");
    snprintf(path,sizeof(path),"%s",n.path); snprintf(original,sizeof(original),"%s",n.payload);
    check_true(storage->vtable->read_text(storage,path,text,sizeof(text)),"published file exists");
    cJSON *j=cJSON_Parse(text);
    check_true(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j,"show_time")),"false") &&
        cJSON_GetObjectItemCaseSensitive(j,"duration")->valueint==10000 && strstr(text,"L＋↓") &&
        strstr(text,ptc_notification_text(PTC_UI_T_NOTIFY_EYE,PTC_UI_LANGUAGE_SIMPLIFIED)),"UTF8 protocol round trip with real combo");
    cJSON_Delete(j);
    notify_tick(&n,&s,3249,101,true);
    check_true(storage->vtable->exists(storage,path),"before pending TTL");
    notify_tick(&n,&s,3250,102,true);
    check_true(!storage->vtable->exists(storage,path) && !n.pending &&
        !strcmp(n.state,"expired_display_unknown"),"absent receiver cleaned at exact TTL");
    s=reason_sample(3,PTC_REASON_DAILY,1,111);
    notify_tick(&n,&s,12000,111,true);
    check_true(n.pending,"new event published after interval");
    snprintf(path,sizeof(path),"%s",n.path);
    storage->vtable->remove_path(storage,path);
    notify_tick(&n,&s,14000,113,true);
    check_true(!n.pending && !strcmp(n.state,"expired_display_unknown"),"disappearance never becomes delivered");
    s=reason_sample(4,PTC_REASON_BEDTIME,7,121);
    notify_tick(&n,&s,22000,121,true);
    snprintf(path,sizeof(path),"%s",n.path);
    snprintf(original,sizeof(original),"%s",n.payload);
    storage->vtable->write_text_atomic(storage,path,"third party replacement");
    notify_tick(&n,&s,24000,123,true);
    check_true(n.failures==1 && storage->vtable->read_text(storage,path,text,sizeof(text)) &&
        !strcmp(text,"third party replacement"),"replacement is preserved and further publication blocked");
    storage->vtable->write_text_atomic(storage,path,original);
    ptc_reason_notification_init(&restarted,storage,"app","release2","fedcba9876543210");
    s=reason_sample(1,PTC_REASON_NONE,0,130);
    notify_tick(&restarted,&s,30000,130,false);
    check_true(restarted.initialized && !storage->vtable->exists(storage,path),"restart exact journal cleanup even disabled");
    for(int mode=0;mode<3;++mode) {
        ptc_reason_notification_init(&n,storage,"app","release1","0123456789abcdef");
        mem->fail_writes=mode==0; mem->fail_renames=mode==1;
        s=reason_sample(1,PTC_REASON_EYE,9,140);
        notify_tick(&n,&s,40000,140,true);
        if(mode<2) check_true(!n.pending && n.failures==1,"atomic write or rename failure bounded");
        mem->fail_writes=mem->fail_renames=false;
        mem->fail_removes=mode==2;
        s.revision++; s.restricted=false;
        notify_tick(&n,&s,50000,150,false);
        if(mode==2) check_true(n.failures==1,"cleanup failure retained");
        mem->fail_removes=false;
        notify_tick(&n,&s,60000,160,false);
        check_true(!n.path[0],"failure cleanup recovers without publishing stale event");
    }
    check_true(ptc_ultrahand_combo("ZR+ZL+DUP",combo,sizeof(combo)) && !strcmp(combo,"ZR＋ZL＋↑"),"actual alternative combo");
    check_true(!ptc_ultrahand_combo("L+L",combo,sizeof(combo)) && !combo[0],"duplicate combo rejected");
    check_true(!ptc_ultrahand_combo("L+BOGUS",combo,sizeof(combo)) && !combo[0],"unknown combo fallback");
    storage->vtable->write_text_atomic(storage,"sdmc:/config/ultrahand/overlays.ini",
        "[playwise.ovl]\nkey_combo=L+DDOWN\nuse_launch_args=false\nuse_launch_args=true\n");
    PtcUltrahandConfig external;
    ptc_ultrahand_config_read(storage,&external);
    check_true(!external.shortcut[0],"ambiguous launch args cannot claim direct details");
    bool enabled; PtcUiLanguagePreference language;
    ptc_reason_notification_preferences("{}",&enabled,&language);
    check_true(!enabled,"legacy config default off");
    ptc_reason_notification_preferences("{\"reason_notifications_enabled\":true,\"reason_notifications_enabled\":false}",&enabled,&language);
    check_true(!enabled,"ambiguous preference defaults off");
    ptc_reason_notification_preferences("{\"reason_notifications_enabled\":true,\"ui_language\":\"system\",\"reason_notification_language\":\"zh-Hant\"}",&enabled,&language);
    check_true(enabled && language==PTC_UI_LANGUAGE_TRADITIONAL,"saved existing UI system language source");
    check_true(storage->vtable->read_text(storage,"sdmc:/config/ultrahand/config.ini",text,sizeof(text)) &&
        !strcmp(text,"[ultrahand]\nnotifications=true\n"),"third party settings untouched");
    check_true(storage->vtable->read_text(storage,"app/ledger/used_nonces.jsonl",text,sizeof(text)) &&
        !strcmp(text,"business sentinel"),"notification failures never alter business ledger");
    ptc_reason_notification_init(&n,storage,"app","release1","0123456789abcdef");
    mem->fail_writes=true;
    for(unsigned i=0;i<5;++i) {
        s=reason_sample(i+1,PTC_REASON_DAILY,i+1,200+i*10);
        notify_tick(&n,&s,100000+i*10000,200+i*10,true);
    }
    check_true(n.blocked && n.failures==5 && n.sequence==5,"five failed publications latch bounded stop");
    mem->fail_writes=false;
    s=reason_sample(9,PTC_REASON_EYE,99,300);
    notify_tick(&n,&s,200000,300,true);
    check_true(n.sequence==5,"latched failure cannot publish a sixth file");
    ptc_reason_notification_init(&n,storage,"app","release1","fedcba9876543210");
    s=reason_sample(1,PTC_REASON_EYE,99,310);
    notify_tick(&n,&s,210000,310,true);
    check_true(n.pending,"suppress lifecycle fixture publishes");
    ptc_reason_notification_tick(&n,&s,210250,310,true,true,false,PTC_UI_LANGUAGE_ENGLISH);
    check_true(!n.pending && !n.path[0],"visible details withdraws own pending material");
    s=reason_sample(2,PTC_REASON_DAILY,1,320);
    notify_tick(&n,&s,220000,320,true);
    check_true(n.pending,"handover lifecycle fixture publishes");
    ptc_reason_notification_tick(&n,&s,220250,320,true,false,true,PTC_UI_LANGUAGE_ENGLISH);
    check_true(!n.pending && !n.path[0],"normal handover withdraws own material");
    storage->vtable->write_text_atomic(storage,"sdmc:/config/ultrahand/config.ini","[ultrahand]\nnotifications=false\n");
    s=reason_sample(3,PTC_REASON_EYE,100,330);
    notify_tick(&n,&s,230000,330,true);
    check_true(!n.pending && n.sequence==2,"external notifications off does not publish");
    storage->vtable->write_text_atomic(storage,n.journal,"unknown recovery record");
    ptc_reason_notification_init(&restarted,storage,"app","release1","fedcba9876543210");
    for(unsigned i=0;i<5;++i) notify_tick(&restarted,&s,240000+i*10000,340+i*10,false);
    check_true(restarted.blocked && storage->vtable->read_text(storage,n.journal,text,sizeof(text)) &&
        !strcmp(text,"unknown recovery record"),"unknown startup ownership preserved with finite retries");

    ptc_mem_storage_init(mem);
    storage->vtable->write_text_atomic(storage,"sdmc:/config/ultrahand/config.ini","[ultrahand]\nnotifications=true\n");
    ptc_reason_notification_init(&n,storage,"app","release1","0123456789abcdef");
    s=reason_sample(1,PTC_REASON_DAILY,1,100);
    notify_tick(&n,&s,1000,100,true);
    snprintf(path,sizeof(path),"%s",n.path);
    check_true(n.pending && n.sequence==1,"handoff transport starts with one daily message");
    s=reason_sample(2,PTC_REASON_BEDTIME,2,102);
    notify_tick(&n,&s,3000,102,true);
    check_true(!n.pending && !storage->vtable->exists(storage,path) && n.sequence==1,"rate-limited bedtime withdraws previous owned file");
    notify_tick(&n,&s,11000,110,true);
    check_true(!n.pending && n.sequence==1,"adapter cannot publish an aged intermediate reason");
    s=reason_sample(3,PTC_REASON_DAILY,1,111);
    notify_tick(&n,&s,12000,111,true);
    check_true(n.pending && n.sequence==2 && strcmp(n.path,path),"fresh return publishes once with a distinct owned filename");
    j=cJSON_Parse(n.payload);
    check_true(j && cJSON_GetObjectItemCaseSensitive(j,"pw_reason")->valueint==PTC_REASON_DAILY &&
        !strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j,"pw_instance")),"1"),"handoff payload describes latest daily instance, never intermediate bedtime");
    cJSON_Delete(j);
    s.revision++;
    notify_tick(&n,&s,12250,111,true);
    check_true(n.sequence==2,"status after returned instance does not republish");
    notify_tick(&n,&s,14000,113,true);
    check_true(!n.pending && n.sequence==2,"returned message retains original two-second pending lifetime");
    free(mem);
}

static void test_reason_policy_observation(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    PtcSysmodule core; PtcPctlStub pctl; PtcFakeTime clock;
    PtcRules rules; PtcRuntimeState runtime = {0};
    check_true(mem != NULL,"allocate reason policy fixture"); if (!mem) return;
    seed_active_buffer_fixture(mem,&pctl,&clock,&core,60,0,true);
    ptc_rules_default(&rules);
    PtcPctlStatus status = pctl.status;
    status.restriction_enabled_available = status.restriction_enabled = true;
    status.temporary_unlocked_available = status.restricted_now_available = true;
    status.restricted_now = true; status.temporary_unlocked = false;
    status.limited_today = status.remaining_available = true; status.remaining_minutes = 0;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(core.reason_observation.reliable && core.reason_observation.reason==PTC_REASON_DAILY,"daily cause observed without guessing");
    status.remaining_available=false;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(!core.reason_observation.reliable,"restricted without known reason stays unknown");
    status.remaining_available=true;
    rules.eye_care.enabled=true; runtime.eye_care_resting=true;
    runtime.eye_care_rest_deadline=clock.snapshot.unix_seconds+60; runtime.eye_care_break_id=17;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(core.reason_observation.reliable && core.reason_observation.reason==PTC_REASON_EYE &&
        core.reason_observation.instance==17,"rest instance masks daily consistently");
    rules.bedtime.enabled=true;
    for(int i=0;i<7;++i) rules.bedtime.week[i]=(PtcBedtimeWindow){true,1200,780};
    PtcBedtimeEvaluation bed=ptc_bedtime_evaluate(&rules,clock.snapshot.day_index,
        ptc_weekday_from_day_index(clock.snapshot.day_index),clock.snapshot.minute_of_day);
    runtime.bedtime_enforced=true; runtime.bedtime_window_instance_id=bed.window_instance_id;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(bed.active && core.reason_observation.reliable && core.reason_observation.reason==PTC_REASON_BEDTIME,"bedtime instance masks rest");
    rules.bedtime.enabled=false; runtime.bedtime_enforced=false; runtime.eye_care_resting=false; runtime.dock_enforced=true;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(core.reason_observation.reliable && core.reason_observation.reason==PTC_REASON_DOCK,"existing dock enforcement reason");
    status.temporary_unlocked=true;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(core.reason_observation.reliable && !core.reason_observation.restricted && core.reason_observation.reason==PTC_REASON_NONE,"temporary official unlock emits no restriction");
    status.temporary_unlocked_available=false;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(!core.reason_observation.reliable,"unknown official unlock rejected");
    status.temporary_unlocked_available=true; runtime.apply_pending_confirmation=true;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(!core.reason_observation.reliable,"unconfirmed write rejected");
    runtime.apply_pending_confirmation=false; core.quiescing=true;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(!core.reason_observation.reliable,"handover suppressed");
    check_true(pctl.read_status_calls==0 && pctl.apply_target_calls==0 && pctl.start_timer_calls==0,"reason projection never reads or writes PCTL");
    free(mem);
}
