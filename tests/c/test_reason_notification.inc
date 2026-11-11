#include "../../sysmodule/reason_notification.h"
#include "../../companion/notification_text.h"
#include "../../companion/input_owner.h"
#include "../../companion/overlay/detail_status.h"

static void test_notification_dependency(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    check_true(mem != NULL, "dependency fixture allocation"); if (!mem) return;
    ptc_mem_storage_init(mem);
    PtcStorage *s = &mem->storage;
    const char *path = "sdmc:/config/ultrahand/config.ini";
    char text[1024];
    check_true(ptc_ultrahand_prepare_notifications(s) == PTC_ULTRAHAND_PREPARE_UNAVAILABLE &&
        !s->vtable->exists(s, path), "missing manager is not installed or fabricated");
    const char *before = "; retained\r\n[ultrahand]\r\n notifications = false \r\nfoo=bar\r\n[other]\r\nnotifications=false\r\n";
    s->vtable->write_text_atomic(s, path, before);
    check_true(ptc_ultrahand_prepare_notifications(s) == PTC_ULTRAHAND_PREPARE_SAVED &&
        s->vtable->read_text(s, path, text, sizeof(text)) &&
        !strcmp(text, "; retained\r\n[ultrahand]\r\n notifications = true \r\nfoo=bar\r\n[other]\r\nnotifications=false\r\n"),
        "enable changes only owned boolean with byte-preserved CRLF and other settings");
    check_true(ptc_ultrahand_prepare_notifications(s) == PTC_ULTRAHAND_PREPARE_ALREADY_ON,
        "repeat preparation reads saved on without claiming resident ACK");
    const char *invalid[] = {"[ultrahand]\n", "[ultrahand]\nnotifications=yes\n",
        "[ultrahand]\nnotifications=false\nnotifications=true\n",
        "[ultrahand]\nnotifications=false\n[ultrahand]\nother=true\n"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        s->vtable->write_text_atomic(s, path, invalid[i]);
        check_true(ptc_ultrahand_prepare_notifications(s) == PTC_ULTRAHAND_PREPARE_UNAVAILABLE &&
            s->vtable->read_text(s, path, text, sizeof(text)) && !strcmp(text, invalid[i]),
            "ambiguous or unsupported manager settings preserved");
    }
    s->vtable->write_text_atomic(s, path, before);
    mem->fail_writes = true;
    check_true(ptc_ultrahand_prepare_notifications(s) == PTC_ULTRAHAND_PREPARE_FAILED,
        "manager write failure is not successful preparation");
    mem->fail_writes = false;
    check_true(s->vtable->read_text(s, path, text, sizeof(text)) && !strcmp(text, before), "failed preparation preserves preimage");
    free(mem);
}

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

static void test_reason_detail_transport(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    check_true(mem != NULL, "detail fixture allocation"); if (!mem) return;
    PtcStorage *storage = &mem->storage;
    for (int mode = 0; mode < 13; ++mode) {
        ptc_mem_storage_init(mem);
        storage->vtable->write_text_atomic(storage, "sdmc:/config/ultrahand/config.ini", "[ultrahand]\nnotifications=true\n");
        storage->vtable->write_text_atomic(storage, "app/handover/runtime-ready.json",
            "{\"profile\":\"release\",\"release_id\":\"release1\",\"boot_id\":\"0123456789abcdef\"}");
        PtcReasonNotification n;
        PtcReasonObservation s = reason_sample(1, PTC_REASON_EYE, 7, 100);
        PtcReasonDetail ready = {0}, offer, ack;
        snprintf(ready.release, sizeof(ready.release), "release1");
        snprintf(ready.boot, sizeof(ready.boot), "0123456789abcdef");
        ready.ui_instance = 900; ready.issued_ms = 1000; ready.expires_ms = 2500;
        if (mode == 6) snprintf(ready.release, sizeof(ready.release), "foreign");
        if (mode == 7) ready.expires_ms = 1000;
        if (mode == 8) snprintf(ready.boot, sizeof(ready.boot), "fedcba9876543210");
        check_true(ptc_reason_detail_write(storage, "app", "ready", &ready), "write loaded UI readiness");
        ptc_reason_notification_init(&n, storage, "app", "release1", "0123456789abcdef");
        notify_tick(&n, &s, 1000, 100, true);
        if (mode >= 6 && mode <= 8) {
            check_true(!n.detail_pending && n.pending, "wrong identity or expired loaded UI falls back without wake");
            continue;
        }
        check_true(n.detail_pending && !n.pending && n.sequence == 0 &&
            ptc_reason_detail_read(storage, "app", "request", &offer) && offer.instance == 7,
            "loaded UI offered correct event before any short notification");
        PtcInputOwnerWriter writer;
        ptc_input_owner_writer_init(&writer, storage, "app", "release1", 900000000);
        if (mode == 0 || mode == 1 || mode == 3 || mode == 9 || mode == 12)
            check_true(ptc_input_owner_acquire(&writer, 1100000000), "detail input lease fixture");
        ack = offer;
        snprintf(ack.owner, sizeof(ack.owner), "%s", mode == 0 || mode == 1 || mode == 3 || mode == 9 || mode == 12 ? writer.owner : "0123456789abcdef0123456789abcdef");
        if (mode == 1) ++ack.ui_instance;
        if (mode == 3) ++ack.instance;
        if (mode == 9) ptc_input_owner_release(&writer, 1200000000);
        if (mode != 4 && mode != 5 && mode != 10)
            check_true(ptc_reason_detail_write(storage, "app", "ack", &ack), "write detail draw ACK fixture");
        s.revision++; s.captured_at = 101;
        if (mode == 12) {
            n.event.last_wall = 102;
            notify_tick(&n, &s, 1250, 101, true);
            check_true(!n.detail_pending && !n.pending && strcmp(n.state, "detail_drawn_confirmed"),
                "clock rollback rejects even an otherwise valid draw ACK and lease");
            continue;
        }
        ptc_reason_notification_tick(&n, &s, 1250, 101, true, mode == 0, false, PTC_UI_LANGUAGE_ENGLISH);
        if (mode == 0) {
            check_true(!n.detail_pending && !n.pending && !strcmp(n.state, "detail_drawn_confirmed"),
                "exact draw ACK plus live matching input lease succeeds even during own owner suppression");
            notify_tick(&n, &s, 3500, 102, true);
            s.revision++; s.captured_at = 120;
            notify_tick(&n, &s, 21000, 120, true);
            check_true(n.detail_sequence == 1 && n.sequence == 0, "closing detail never replays same instance or fallback");
        } else if (mode == 5) {
            s.reliable = false;
            notify_tick(&n, &s, 1500, 101, true);
            check_true(!n.detail_pending && !n.pending, "unknown status cancels detail without fallback");
        } else if (mode == 10) {
            ptc_reason_notification_tick(&n, &s, 1500, 101, true, false, true, PTC_UI_LANGUAGE_ENGLISH);
            check_true(!n.detail_pending && !n.pending, "handover withdraws detail before exit");
        } else {
            check_true(n.detail_pending && !n.pending, "missing, wrong-instance or unowned ACK cannot claim display");
            s.revision++; s.captured_at = 102;
            if (mode == 11) {
                ptc_reason_notification_tick(&n, &s, 3000, 102, true, true, false, PTC_UI_LANGUAGE_ENGLISH);
                check_true(n.detail_pending && !n.pending, "expired offer waits briefly for cached owner release");
            }
            notify_tick(&n, &s, 3000, 102, true);
            check_true(!n.detail_pending && n.pending && n.sequence == 1,
                "exact timeout issues one fresh short fallback without retrying wake");
            notify_tick(&n, &s, 3250, 102, true);
            check_true(n.sequence == 1 && n.detail_sequence == 1, "fallback does not multiply under status storm");
        }
    }
    PtcReasonDetail detail = {0};
    PtcCompanionResultSummary summary = {0};
    detail.day = 2380; detail.reason = PTC_REASON_DAILY; detail.instance = 2381;
    summary.valid = summary.ok = summary.temporary_unlocked_available = true;
    summary.day_index = 2380; summary.restricted_now = 1; summary.daily_restriction_active = true;
    check_true(ptc_overlay_detail_matches(&detail, &summary), "fresh daily page matches offered restriction");
    summary.bedtime_available = summary.bedtime_active = true; summary.bedtime_window_instance_id = 77;
    check_true(!ptc_overlay_detail_matches(&detail, &summary), "higher-priority bedtime cannot ACK a daily offer");
    detail.reason = PTC_REASON_BEDTIME; detail.instance = 77;
    check_true(ptc_overlay_detail_matches(&detail, &summary), "matching bedtime instance matches correct detail");
    summary.temporary_unlocked = true;
    check_true(!ptc_overlay_detail_matches(&detail, &summary), "official unlock cannot ACK restricted details");
    free(mem);
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
    check_true(core.reason_observation.reliable && core.reason_observation.reason==PTC_REASON_DAILY &&
        core.reason_observation.time_kind==PTC_REASON_TIME_DAILY_ZERO,"daily cause and zero observed without guessing");
    status.signed_remaining_ns_available=true; status.signed_remaining_ns=-123;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(core.reason_observation.time_kind==PTC_REASON_TIME_DAILY_ZERO,"negative daily balance displays zero");
    status.signed_remaining_ns_available=false;
    status.remaining_available=false;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(!core.reason_observation.reliable,"restricted without known reason stays unknown");
    status.remaining_available=true;
    rules.eye_care.enabled=true; runtime.eye_care_resting=true;
    runtime.eye_care_rest_deadline=clock.snapshot.unix_seconds+60; runtime.eye_care_break_id=17;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(core.reason_observation.reliable && core.reason_observation.reason==PTC_REASON_EYE &&
        core.reason_observation.instance==17 && core.reason_observation.time_kind==PTC_REASON_TIME_REMAINING &&
        core.reason_observation.remaining_seconds==60,"rest instance and same-pass time mask daily consistently");
    runtime.eye_care_rest_deadline=clock.snapshot.unix_seconds+3601;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(core.reason_observation.reliable && core.reason_observation.time_kind==PTC_REASON_TIME_UNKNOWN,
        "reliable rest cause with impossible deadline keeps time unknown");
    rules.bedtime.enabled=true;
    for(int i=0;i<7;++i) rules.bedtime.week[i]=(PtcBedtimeWindow){true,1200,780};
    PtcBedtimeEvaluation bed=ptc_bedtime_evaluate(&rules,clock.snapshot.day_index,
        ptc_weekday_from_day_index(clock.snapshot.day_index),clock.snapshot.minute_of_day);
    runtime.bedtime_enforced=true; runtime.bedtime_window_instance_id=bed.window_instance_id;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(bed.active && core.reason_observation.reliable && core.reason_observation.reason==PTC_REASON_BEDTIME,"bedtime instance masks rest");
    PtcClockSnapshot saved_clock=clock.snapshot;
    for (int next_day=0;next_day<2;++next_day) {
        clock.snapshot=saved_clock; clock.snapshot.day_index=(uint16_t)(saved_clock.day_index+next_day);
        clock.snapshot.minute_of_day=next_day ? 60 : 1439; clock.snapshot.second_of_minute=59;
        bed=ptc_bedtime_evaluate(&rules,clock.snapshot.day_index,
            ptc_weekday_from_day_index(clock.snapshot.day_index),clock.snapshot.minute_of_day);
        runtime.bedtime_window_instance_id=bed.window_instance_id;
        ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
        check_true(core.reason_observation.time_kind==PTC_REASON_TIME_REMAINING &&
            core.reason_observation.remaining_seconds==(next_day ? 43141u : 46801u),
            "cross-night bedtime uses instance end date and exact seconds");
    }
    clock.snapshot=saved_clock;
    rules.bedtime.enabled=false; runtime.bedtime_enforced=false; runtime.eye_care_resting=false; runtime.dock_enforced=true;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(core.reason_observation.reliable && core.reason_observation.reason==PTC_REASON_DOCK,"existing dock enforcement reason");
    check_true(core.reason_observation.time_kind==PTC_REASON_TIME_CONNECT_TV,"TV requirement invents no countdown");
    rules.dock_policy.undocked_limit_enabled=true; rules.dock_policy.undocked_daily_minutes=1;
    runtime.dock_usage_known=true; runtime.dock_day_index=clock.snapshot.day_index;
    runtime.dock_last_mode=PTC_OPERATION_MODE_UNDOCKED; runtime.undocked_used_ns=UINT64_C(60000000000);
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(core.reason_observation.time_kind==PTC_REASON_TIME_DOCK_ZERO,"known non-TV exhaustion displays zero");
    runtime.dock_day_index--;
    ptc_sysmodule_note_reason(&core,&rules,&runtime,&status,clock.snapshot);
    check_true(core.reason_observation.time_kind==PTC_REASON_TIME_CONNECT_TV,"yesterday's non-TV usage cannot supply zero");
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

static void test_reason_snapshot_text(void)
{
    PtcReasonObservation s=reason_sample(1,PTC_REASON_EYE,17,100);
    char body[512], small[8];
    const unsigned seconds[]={1,59,60,61,3599,3600};
    for (int lang=PTC_UI_LANGUAGE_SIMPLIFIED;lang<=PTC_UI_LANGUAGE_ENGLISH;++lang) {
        s.time_kind=PTC_REASON_TIME_REMAINING;
        for (unsigned i=0;i<sizeof(seconds)/sizeof(seconds[0]);++i) {
            s.remaining_seconds=seconds[i];
            check_true(ptc_reason_notification_body(&s,100,(PtcUiLanguagePreference)lang,"L＋↓",body,sizeof(body)),"three-language duration fits payload buffer");
            check_true(strstr(body,ptc_notification_text(PTC_UI_T_NOTIFY_EYE,(PtcUiLanguagePreference)lang)) &&
                strstr(body,"L＋↓"),"duration retains child state and actual shortcut");
            if(seconds[i]<60) check_true(strstr(body,ptc_notification_text(PTC_UI_T_NOTIFY_UNDER_MINUTE,(PtcUiLanguagePreference)lang)),"sub-minute time stays below one minute");
        }
    }
    s.remaining_seconds=61;
    check_true(ptc_reason_notification_body(&s,100,PTC_UI_LANGUAGE_ENGLISH,"",body,sizeof(body)) && strstr(body,"~2 min"),"positive seconds round minutes upward");
    check_true(ptc_reason_notification_body(&s,101,PTC_UI_LANGUAGE_ENGLISH,"",body,sizeof(body)) && strstr(body,"~1 min"),"publication deducts observation age");
    s.remaining_seconds=1;
    check_true(ptc_reason_notification_body(&s,101,PTC_UI_LANGUAGE_ENGLISH,"",body,sizeof(body)) && strstr(body,"Time left unknown"),"deadline requires confirmation instead of zero or release promise");
    s.time_kind=PTC_REASON_TIME_UNKNOWN;
    check_true(ptc_reason_notification_body(&s,100,PTC_UI_LANGUAGE_ENGLISH,"",body,sizeof(body)) && strstr(body,"Time left unknown"),"unknown time cannot inherit an earlier duration");
    check_true(!ptc_reason_notification_body(&s,102,PTC_UI_LANGUAGE_ENGLISH,"",body,sizeof(body)) && !body[0],"aged sample cannot format a current notice");
    check_true(!ptc_reason_notification_body(&s,99,PTC_UI_LANGUAGE_ENGLISH,"",body,sizeof(body)),"future capture rejected");
    s.local_clock_available=true; s.local_second_of_day=86399;
    check_true(!ptc_reason_notification_body(&s,101,PTC_UI_LANGUAGE_ENGLISH,"",body,sizeof(body)),"midnight rejects yesterday even inside two-second freshness");
    PtcReasonEvent e; ptc_reason_event_init(&e,"r","b");
    check_true(!ptc_reason_event_update(&e,"r","b",&s,1000,101,true,false),"event gate also rejects cross-day snapshot");
    s.local_second_of_day=0;
    check_true(!ptc_reason_notification_body(&s,100,PTC_UI_LANGUAGE_ENGLISH,"",small,sizeof(small)) && !small[0],"small output refuses truncation");
    s.reason=PTC_REASON_BEDTIME; s.time_kind=PTC_REASON_TIME_REMAINING; s.remaining_seconds=86340;
    check_true(ptc_reason_notification_body(&s,100,PTC_UI_LANGUAGE_ENGLISH,"",body,sizeof(body)) && strstr(body,"23 hr 59 min"),"long bedtime duration is explicit");
    s.remaining_seconds=86400;
    check_true(ptc_reason_notification_body(&s,100,PTC_UI_LANGUAGE_ENGLISH,"",body,sizeof(body)) && strstr(body,"24 hr"),"full day duration fits");
    s.reliable=false;
    check_true(!ptc_reason_notification_body(&s,100,PTC_UI_LANGUAGE_ENGLISH,"",body,sizeof(body)),"unknown cause publishes nothing");
}
