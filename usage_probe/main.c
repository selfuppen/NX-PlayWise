#include <switch.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "platform/switch/pctl_adapter.h"
#include "platform/switch/play_timer_settings_layout.h"
#include "third_party/cjson/cJSON.h"
#include "probe_identity.h"
#include "boot_policy.h"

#define ROOT "sdmc:/switch/playwise-usage-probe"
#define RELEASE_ID UINT64_C(0x4200000000BD2300)
#define LAB_ID UINT64_C(0x4200000000BD23F0)
#define NS_PER_MIN INT64_C(60000000000)
#define RELEASE_FLAG "sdmc:/atmosphere/contents/4200000000BD2300/flags/boot2.flag"
#define RELEASE_BACKUP RELEASE_FLAG ".usage-probe"
#define PROBE_FLAG "sdmc:/atmosphere/contents/4200000000BD23F1/flags/boot2.flag"

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;
static PtcSwitchPctl adapter;
static PtcPctl *pctl;
static char boot[32];
static u64 write_tick, restricted_deadline;
extern TimeServiceType __nx_time_service_type;
static Result pdm_rc, pm_rc;
static bool writing, finished, sampling_paused;
static bool read_only_boot, boot_plan_valid;
static char boot_plan[65];
static u64 observe_started;
static unsigned observe_timeout_s=1200;
static s32 next_event = -1;
static u64 applications[16];
static size_t app_count;
static AccountUid users[8];
static size_t user_count;

void __libnx_initheap(void)
{
    static u8 heap[0x100000];
    extern void *fake_heap_start, *fake_heap_end;
    fake_heap_start = heap; fake_heap_end = heap + sizeof(heap);
}
void __appInit(void)
{
    Result rc = smInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(rc);
    rc = fsInitialize(); if (R_FAILED(rc)) diagAbortWithResult(rc);
    fsdevMountSdmc();
    timeInitialize();
    SetSysFirmwareVersion fw;
    if (R_SUCCEEDED(setsysInitialize())) {
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }
}
void __appExit(void)
{
    ptc_switch_pctl_exit(&adapter);
    if (R_SUCCEEDED(pdm_rc)) pdmqryExit();
    if (R_SUCCEEDED(pm_rc)) pmshellExit();
    timeExit(); fsdevUnmountAll(); fsExit(); smExit();
}
static bool save_json(const char *path, cJSON *json)
{
    char tmp[384]; snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    char *s = cJSON_PrintUnformatted(json);
    if (!s) return false;
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(s, 1, strlen(s), f) == strlen(s);
    if (f && fclose(f) != 0) ok = false;
    free(s);
    /* Horizon/FAT rename does not replace an existing file. Metadata is
       advisory; preserve the old generation until the new one is installed. */
    char previous[384]; snprintf(previous,sizeof(previous),"%s.previous",path);
    FILE *old=fopen(path,"rb"); bool moved=false;
    if(old) {fclose(old);remove(previous);moved=rename(path,previous)==0;if(!moved) ok=false;}
    if (ok) ok = rename(tmp, path) == 0 && fsdevCommitDevice("sdmc") == 0;
    if(!ok && moved) rename(previous,path);
    if(ok && moved) remove(previous);
    if (!ok) remove(tmp);
    return ok;
}
static cJSON *load_json(const char *path)
{
    char s[8192]; FILE *f = fopen(path, "rb"); if (!f) return NULL;
    size_t n = fread(s, 1, sizeof(s)-1, f); bool ok = !ferror(f) && feof(f);
    fclose(f); s[n] = 0;
    return ok ? cJSON_Parse(s) : NULL;
}
static bool append_json(const char *path, cJSON *json)
{
    char *s = cJSON_PrintUnformatted(json); if (!s) return false;
    FILE *f = fopen(path, "ab");
    bool ok = f && fprintf(f, "%s\n", s) > 0;
    if (f && fclose(f) != 0) ok = false;
    free(s); return ok;
}
/* Counters remain decimal strings: browser/Python consumers must not lose
   nanosecond precision by routing signed 64-bit data through JSON doubles. */
static void i64(cJSON *j, const char *key, s64 value)
{
    char text[32]; snprintf(text, sizeof(text), "%lld", (long long)value); cJSON_AddStringToObject(j, key, text);
}
static void u64hex(cJSON *j, const char *key, u64 value)
{
    char text[32]; snprintf(text, sizeof(text), "%016llx", (unsigned long long)value); cJSON_AddStringToObject(j, key, text);
}
static void bytes_hex(char *out, const void *data, size_t size)
{
    const u8 *p = data; for (size_t i=0; i<size; ++i) sprintf(out+i*2, "%02x", p[i]);
    out[size*2] = 0;
}
static cJSON *stamp(void)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "schema_version", 1);
    cJSON_AddStringToObject(j, "boot_id", boot);
    i64(j, "tick_ns", armTicksToNs(armGetSystemTick()));
    u64 ts = 0; Result rc = timeGetCurrentTime(TimeType_UserSystemClock, &ts);
    cJSON_AddNumberToObject(j,"user_clock_result",rc); i64(j,"unix_s",ts);
    /* libnx's shared-memory shortcut aliases Local to User. Query the actual
       Local session so a successful setter cannot hide behind User auto-sync. */
    u64 local_ts=0;Service *local_clock=timeGetServiceSession_SystemClock(TimeType_LocalSystemClock);
    Result local_rc=serviceDispatchOut(local_clock,0,local_ts);
    cJSON_AddNumberToObject(j,"local_clock_result",local_rc);i64(j,"local_unix_s",local_ts);
    cJSON_AddStringToObject(j,"local_clock_method","session_ipc");
    TimeCalendarTime calendar={0};TimeCalendarAdditionalInfo info={0};
    rc=timeToCalendarTimeWithMyRule(ts,&calendar,&info);
    cJSON_AddNumberToObject(j,"calendar_result",rc);cJSON_AddNumberToObject(j,"utc_offset_s",info.offset);
    TimeSteadyClockTimePoint steady = {0};
    rc = timeGetStandardSteadyClockTimePoint(&steady);
    cJSON_AddNumberToObject(j,"steady_result",rc); i64(j,"steady_s",steady.time_point);
    char id[33]; bytes_hex(id,&steady.source_id,sizeof(steady.source_id)); cJSON_AddStringToObject(j,"steady_source",id);
    TimeSteadyClockTimePoint ipc_steady={0};
    rc=serviceDispatchOut(timeGetServiceSession_SteadyClock(),0,ipc_steady);
    cJSON_AddNumberToObject(j,"steady_ipc_result",rc);i64(j,"steady_ipc_s",ipc_steady.time_point);
    s64 internal_offset=0;rc=timeGetStandardSteadyClockInternalOffset(&internal_offset);
    cJSON_AddNumberToObject(j,"steady_internal_offset_result",rc);i64(j,"steady_internal_offset_ns",internal_offset);
    return j;
}
static bool owner_absent(u64 id)
{
    u64 pid; Result rc = pmshellGetProcessId(&pid,id);
    return R_FAILED(rc) && R_MODULE(rc)==15 && R_DESCRIPTION(rc)==1;
}
static bool exclusive(void)
{
    return R_SUCCEEDED(pm_rc) && owner_absent(RELEASE_ID) && owner_absent(LAB_ID);
}
static int current_weekday(void)
{
    u64 ts=0;TimeCalendarTime calendar;TimeCalendarAdditionalInfo info;
    if(R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock,&ts)) ||
       R_FAILED(timeToCalendarTimeWithMyRule(ts,&calendar,&info))) return -1;
    return info.wday;
}
static void remember_app(u64 id)
{
    if (!id) return;
    for(size_t i=0;i<app_count;++i) if(applications[i]==id) return;
    if(app_count<16) applications[app_count++]=id;
    else {memmove(applications,applications+1,15*sizeof(*applications));applications[15]=id;}
}
static void events(void)
{
    if(R_FAILED(pdm_rc)) return;
    s32 total=0,start=0,end=0; Result rc=pdmqryGetAvailablePlayEventRange(&total,&start,&end);
    cJSON *range=stamp(); cJSON_AddNumberToObject(range,"result",rc);
    cJSON_AddNumberToObject(range,"count",total); cJSON_AddNumberToObject(range,"start",start); cJSON_AddNumberToObject(range,"end",end);
    append_json(ROOT "/ranges.jsonl",range); cJSON_Delete(range);
    if(R_FAILED(rc)||total<=0) return;
    if(next_event<0) next_event=read_only_boot && end-start>511 ? end-511 : start;
    if(next_event<start) {
        cJSON *gap=stamp(); cJSON_AddStringToObject(gap,"gap","event_retention"); cJSON_AddNumberToObject(gap,"from",next_event); cJSON_AddNumberToObject(gap,"to",start);
        append_json(ROOT "/ranges.jsonl",gap); cJSON_Delete(gap); next_event=start;
    }
    /* Bound each loop; initial history is drained without blocking controls. */
    for(unsigned page=0;page<8 && next_event<=end;++page) {
        PdmPlayEvent buffer[128]; s32 got=0;
        rc=pdmqryQueryPlayEvent(next_event,buffer,128,&got);
        if(R_FAILED(rc)||got<=0) break;
        for(s32 i=0;i<got;++i) {
            PdmPlayEvent *e=&buffer[i]; cJSON *j=stamp(); char raw[sizeof(*e)*2+1]; bytes_hex(raw,e,sizeof(*e));
            cJSON_AddNumberToObject(j,"index",next_event+i); cJSON_AddNumberToObject(j,"type",e->play_event_type); cJSON_AddStringToObject(j,"raw_hex",raw);
            i64(j,"event_user_s",e->timestamp_user); i64(j,"event_network_s",e->timestamp_network); i64(j,"event_steady_s",e->timestamp_steady);
            if(e->play_event_type==PdmPlayEventType_Applet) {
                u64 id=((u64)e->event_data.applet.program_id[0]<<32)|e->event_data.applet.program_id[1];
                u64hex(j,"program_id",id); remember_app(id);
                cJSON_AddNumberToObject(j,"applet_id",e->event_data.applet.applet_id); cJSON_AddNumberToObject(j,"event_type",e->event_data.applet.event_type);
                cJSON_AddNumberToObject(j,"log_policy",e->event_data.applet.log_policy);
            } else if(e->play_event_type==PdmPlayEventType_PowerStateChange) cJSON_AddNumberToObject(j,"power_value",e->event_data.power_state_change.value);
            else if(e->play_event_type==PdmPlayEventType_Account) {
                AccountUid uid;
                uid.uid[0]=((u64)e->event_data.account.uid[0]<<32)|e->event_data.account.uid[1];
                uid.uid[1]=((u64)e->event_data.account.uid[2]<<32)|e->event_data.account.uid[3];
                bool known=false; for(size_t u=0;u<user_count;++u) if(memcmp(&users[u],&uid,sizeof(uid))==0) known=true;
                if(!known && user_count<8) users[user_count++]=uid;
                /* Raw account identity stays on device; exports use ordinal. */
            }
            if(!append_json(ROOT "/events.jsonl",j)) writing=false;
            cJSON_Delete(j);
        }
        next_event+=got;
    }
}
static void stats(void)
{
    if(R_FAILED(pdm_rc)) return;
    for(size_t i=0;i<app_count;++i) {
        for(size_t u=0;u<=user_count;++u) {
            PdmPlayStatistics s={0};
            Result rc=u==0 ? pdmqryQueryPlayStatisticsByApplicationId(applications[i],true,&s) :
                pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(applications[i],users[u-1],true,&s);
            cJSON *j=stamp(); cJSON_AddNumberToObject(j,"result",rc); u64hex(j,"program_id",applications[i]);
            char raw[sizeof(s)*2+1];bytes_hex(raw,&s,sizeof(s));cJSON_AddStringToObject(j,"raw_hex",raw);
            cJSON_AddNumberToObject(j,"account_ordinal",u); i64(j,"playtime_ns",s.playtime);
            cJSON_AddNumberToObject(j,"launches",s.total_launches); cJSON_AddNumberToObject(j,"last_index",s.last_entry_index);
            i64(j,"last_user_s",s.last_timestamp_user);
            i64(j,"first_user_s",s.first_timestamp_user);i64(j,"last_network_s",s.last_timestamp_network);
            append_json(ROOT "/stats.jsonl",j); cJSON_Delete(j);
        }
    }
}
static cJSON *sample(void)
{
    cJSON *j=stamp(); PtcPctlForensicSample s;
    PtcErrorCode err=pctl->vtable->forensic_sample(pctl,&s);
    cJSON_AddNumberToObject(j,"sample_error",err);
    cJSON_AddNumberToObject(j,"remaining_result",s.remaining_result); i64(j,"remaining_ns",s.remaining_ns);
    cJSON_AddNumberToObject(j,"spent_result",s.spent_result); i64(j,"spent_ns",s.spent_ns);
    cJSON_AddNumberToObject(j,"timer_result",s.timer_enabled_result); cJSON_AddBoolToObject(j,"timer_enabled",s.timer_enabled);
    cJSON_AddNumberToObject(j,"restricted_result",s.restricted_result); cJSON_AddBoolToObject(j,"restricted",s.restricted);
    cJSON_AddNumberToObject(j,"settings_result",s.settings_result);
    char hex[137]; bytes_hex(hex,s.settings,68); cJSON_AddStringToObject(j,"settings_hex",hex);
    PtcPctlStatus status={0}; int weekday=current_weekday();
    err=weekday<0 ? PTC_ERR_PCTL_READ_FAILED : pctl->vtable->read_status(pctl,weekday,&status);
    cJSON_AddNumberToObject(j,"status_error",err); cJSON_AddNumberToObject(j,"weekday",weekday);
    cJSON_AddBoolToObject(j,"configured_available",status.configured_minutes_available); cJSON_AddNumberToObject(j,"configured_minutes",status.configured_minutes);
    cJSON_AddBoolToObject(j,"restriction_enabled_available",status.restriction_enabled_available); cJSON_AddBoolToObject(j,"restriction_enabled",status.restriction_enabled);
    cJSON_AddBoolToObject(j,"temporary_unlocked_available",status.temporary_unlocked_available); cJSON_AddBoolToObject(j,"temporary_unlocked",status.temporary_unlocked);
    cJSON_AddBoolToObject(j,"writing_enabled",writing); cJSON_AddBoolToObject(j,"exclusive_owner",exclusive());
    cJSON_AddBoolToObject(j,"sampling_paused",sampling_paused);
    cJSON_AddBoolToObject(j,"read_only_boot",read_only_boot);
    cJSON_AddStringToObject(j,"boot_plan_id",boot_plan);
    u64 pid=0,id=0; Result rc=pmshellGetApplicationProcessIdForShell(&pid);
    cJSON_AddNumberToObject(j,"application_pid_result",rc); i64(j,"application_pid",pid);
    if(R_SUCCEEDED(rc) && R_SUCCEEDED(pminfoInitialize())) { rc=pminfoGetProgramId(&id,pid); pminfoExit(); remember_app(id); }
    u64hex(j,"application_id",id); cJSON_AddNumberToObject(j,"application_id_result",rc);
    return j;
}
static bool snapshot(void)
{
    PtcPctlSettingsSnapshot s; if(pctl->vtable->snapshot_settings(pctl,&s)!=PTC_ERR_OK) return false;
    cJSON *j=stamp(); char hex[137]; bytes_hex(hex,s.data,68); cJSON_AddStringToObject(j,"settings_hex",hex);
    cJSON_AddStringToObject(j,"source_hash",PROBE_SOURCE_HASH);
    cJSON_AddBoolToObject(j,"timer_enabled",s.timer_enabled);
    bool ok=save_json(ROOT "/baseline.json",j); cJSON_Delete(j); return ok;
}
static bool restore(void)
{
    cJSON *j=load_json(ROOT "/baseline.json"); if(!j) return false;
    const cJSON *h=cJSON_GetObjectItemCaseSensitive(j,"settings_hex"), *t=cJSON_GetObjectItemCaseSensitive(j,"timer_enabled");
    PtcPctlSettingsSnapshot s={.size=68},check;
    bool ok=cJSON_IsString(h)&&strlen(h->valuestring)==136&&cJSON_IsBool(t);
    if(ok) for(size_t i=0;i<68;++i) { unsigned b; if(sscanf(h->valuestring+i*2,"%2x",&b)!=1) {ok=false; break;} s.data[i]=b; }
    s.timer_enabled=cJSON_IsTrue(t); cJSON_Delete(j);
    if(!ok || !exclusive()) return false;
    ok=pctl->vtable->restore_settings(pctl,&s)==PTC_ERR_OK;
    if(ok) ok=pctl->vtable->snapshot_settings(pctl,&check)==PTC_ERR_OK && memcmp(s.data,check.data,68)==0;
    /* Do not activate a timer merely to restore a historical boolean. */
    return ok;
}
static bool valid_id(const char *id)
{
    size_t n=strlen(id); if(!n||n>64) return false;
    for(size_t i=0;i<n;++i) if(!((id[i]>='a'&&id[i]<='z')||(id[i]>='0'&&id[i]<='9')||id[i]=='-')) return false;
    return true;
}
static bool empty_flag(const char *path)
{
    FILE *f=fopen(path,"rb");if(!f) return false;
    bool empty=fgetc(f)==EOF && !ferror(f);fclose(f);return empty;
}
static Result launch_standard(u64 *pid)
{
    Result rc=pmshellGetProcessId(pid,RELEASE_ID);
    if(R_SUCCEEDED(rc)) return rc;
    if(R_MODULE(rc)!=15 || R_DESCRIPTION(rc)!=1) return rc;
    NcmProgramLocation location={.program_id=RELEASE_ID,.storageID=NcmStorageId_None};
    return pmshellLaunchProgram(PmLaunchFlag_None,&location,pid);
}
static void observe_deadline(void)
{
    if(!read_only_boot || !boot_plan_valid || armTicksToNs(armGetSystemTick()-observe_started)<(u64)observe_timeout_s*UINT64_C(1000000000)) return;
    /* Fixed, empty flags only; never synthesize an absent original flag. */
    bool restored=empty_flag(RELEASE_FLAG);
    if(!restored && empty_flag(RELEASE_BACKUP)) restored=rename(RELEASE_BACKUP,RELEASE_FLAG)==0;
    bool removed=false;
    if(restored && empty_flag(PROBE_FLAG)) removed=remove(PROBE_FLAG)==0;
    fsdevCommitDevice("sdmc");
    u64 pid=0;Result rc=0;
    if(restored && removed) rc=launch_standard(&pid);
    cJSON *j=stamp();cJSON_AddStringToObject(j,"boot_plan_id",boot_plan);
    cJSON_AddBoolToObject(j,"standard_flag_restored",restored);cJSON_AddBoolToObject(j,"probe_flag_removed",removed);
    cJSON_AddNumberToObject(j,"launch_result",rc);i64(j,"standard_pid",pid);
    cJSON_AddBoolToObject(j,"needs_manual_reboot",!restored || !removed || R_FAILED(rc));
    save_json(ROOT "/observe-timeout.json",j);cJSON_Delete(j);finished=true;
}
static void command(void)
{
    cJSON *q=load_json(ROOT "/command.json"); if(!q) return;
    const cJSON *id=cJSON_GetObjectItemCaseSensitive(q,"id"),*op=cJSON_GetObjectItemCaseSensitive(q,"op"),*v=cJSON_GetObjectItemCaseSensitive(q,"value");
    if(!cJSON_IsString(id)||!valid_id(id->valuestring)||!cJSON_IsString(op)) {cJSON_Delete(q);return;}
    char path[384]; snprintf(path,sizeof(path),ROOT "/results/%s.json",id->valuestring);
    FILE *exists=fopen(path,"rb"); if(exists) {fclose(exists);remove(ROOT "/command.json");cJSON_Delete(q);return;}
    cJSON *result=stamp(),*before=sample(); cJSON_AddItemToObject(result,"before",before);
    cJSON_AddStringToObject(result,"id",id->valuestring); cJSON_AddStringToObject(result,"op",op->valuestring);
    bool ok=false, power_request=false; Result raw=0;
    if(!probe_boot_operation_allowed(read_only_boot,op->valuestring))
        cJSON_AddStringToObject(result,"reason","read_only_boot");
    else if(strcmp(op->valuestring,"sample")==0) ok=true;
    else if(strcmp(op->valuestring,"export")==0) {sampling_paused=true;ok=true;}
    else if(strcmp(op->valuestring,"collect")==0) {sampling_paused=false;ok=true;}
    else if(strcmp(op->valuestring,"exit")==0 && !writing) {ok=true;finished=true;}
    else if(read_only_boot && (strcmp(op->valuestring,"reboot_readonly")==0 || strcmp(op->valuestring,"handoff_standard")==0)) {
        bool authorized=boot_plan_valid && cJSON_IsString(v) && strcmp(v->valuestring,boot_plan)==0 && !writing && exclusive();
        if(!authorized) cJSON_AddStringToObject(result,"reason","boot_plan_or_owner_mismatch");
        else if(strcmp(op->valuestring,"reboot_readonly")==0) {
            if(!sampling_paused) cJSON_AddStringToObject(result,"reason","export_barrier_required");
            else {raw=bpcInitialize();ok=R_SUCCEEDED(raw);power_request=ok;}
        } else {
            /* Host restores the original flag and removes our flag first. */
            FILE *release_flag=fopen(RELEASE_FLAG,"rb");
            FILE *probe_flag=fopen(PROBE_FLAG,"rb");
            bool flags_ok=release_flag && !probe_flag;
            if(release_flag) fclose(release_flag);
            if(probe_flag) fclose(probe_flag);
            if(!flags_ok) cJSON_AddStringToObject(result,"reason","restored_boot_flags_required");
            else {
                u64 pid=0;raw=launch_standard(&pid);ok=R_SUCCEEDED(raw);
                i64(result,"standard_pid",pid);finished=true;
                /* Exit even on launch failure, releasing our resources. The
                   restored standard boot flag remains the recovery fallback. */
            }
        }
    }
    else if(strcmp(op->valuestring,"begin")==0) { if(!writing && exclusive()) {writing=snapshot();write_tick=armGetSystemTick();} ok=writing; }
    else if(strcmp(op->valuestring,"resume")==0 && !writing && exclusive()) {
        cJSON *baseline=load_json(ROOT "/baseline.json");
        const cJSON *hash=baseline ? cJSON_GetObjectItemCaseSensitive(baseline,"source_hash") : NULL;
        ok=cJSON_IsString(hash)&&strcmp(hash->valuestring,PROBE_SOURCE_HASH)==0;
        cJSON_Delete(baseline);if(ok){writing=true;write_tick=armGetSystemTick();}
    }
    else if(writing && exclusive()) {
        int weekday=current_weekday();
        if(strcmp(op->valuestring,"limit")==0 && weekday>=0 && cJSON_IsNumber(v) && v->valuedouble==v->valueint && v->valueint>=0 && v->valueint<=1440) {
            PtcPctlTarget target={.mode=PTC_PCTL_TARGET_LIMIT,.minutes=v->valueint,.weekday=weekday};
            ok=pctl->vtable->apply_target(pctl,&target)==PTC_ERR_OK;
            if(ok) restricted_deadline=v->valueint<10 ? armGetSystemTick()+armNsToTicks(INT64_C(30000000000)) : 0;
        } else if(strcmp(op->valuestring,"unlimited")==0 && weekday>=0) {
            PtcPctlTarget target={.mode=PTC_PCTL_TARGET_UNLIMITED,.weekday=weekday}; ok=pctl->vtable->apply_target(pctl,&target)==PTC_ERR_OK;
            if(ok) restricted_deadline=0;
        } else if(strcmp(op->valuestring,"unconfigured")==0 && weekday>=0) {
            PtcPctlSettingsSnapshot s;
            if(pctl->vtable->snapshot_settings(pctl,&s)==PTC_ERR_OK) {
                size_t base=PTC_PLAY_TIMER_HEADER_WORDS+weekday*PTC_PLAY_TIMER_DAY_WORDS;
                memset(s.data+base*2,0,6); ok=pctl->vtable->restore_settings(pctl,&s)==PTC_ERR_OK;
            }
        } else if(strcmp(op->valuestring,"start")==0) ok=pctl->vtable->start_timer(pctl)==PTC_ERR_OK;
        else if(strcmp(op->valuestring,"stop")==0) ok=pctl->vtable->stop_timer(pctl)==PTC_ERR_OK;
        else if(strcmp(op->valuestring,"restore")==0) ok=restore();
        else if((strcmp(op->valuestring,"clock")==0 || strcmp(op->valuestring,"clock_local")==0) && cJSON_IsString(v)) {
            /* time:s privilege is selected only for this explicit experiment. */
            timeExit(); __nx_time_service_type=TimeServiceType_System; raw=timeInitialize();
            if(R_SUCCEEDED(raw)) raw=timeSetCurrentTime(strcmp(op->valuestring,"clock_local")==0 ? TimeType_LocalSystemClock : TimeType_UserSystemClock,strtoull(v->valuestring,NULL,10));
            ok=R_SUCCEEDED(raw);
        } else if(strcmp(op->valuestring,"finish")==0) {ok=restore(); if(ok){writing=false;finished=true;}}
        else if(strcmp(op->valuestring,"reboot")==0) {raw=bpcInitialize();ok=R_SUCCEEDED(raw);power_request=ok;}
    }
    cJSON_AddBoolToObject(result,"ok",ok); cJSON_AddNumberToObject(result,"raw_result",raw);
    if(power_request) cJSON_AddStringToObject(result,"ack_kind","pre_reboot_ack");
    cJSON_AddItemToObject(result,"after",sample());
    bool saved=save_json(path,result); cJSON_Delete(result);
    if(saved) remove(ROOT "/command.json"); else {if(writing) restore();writing=false;}
    if(saved && ok && power_request) {
        fsdevCommitDevice("sdmc");Result reboot_rc=bpcRebootSystem();bpcExit();
        /* Initialization success is not proof of an actual reboot. */
        cJSON *returned=stamp();cJSON_AddStringToObject(returned,"request_id",id->valuestring);
        cJSON_AddNumberToObject(returned,"reboot_call_result",reboot_rc);
        save_json(ROOT "/reboot-return.json",returned);cJSON_Delete(returned);
    }
    cJSON_Delete(q);
}
int main(int argc,char **argv)
{
    (void)argc;(void)argv;
    mkdir(ROOT,0777);mkdir(ROOT "/results",0777);
    FILE *marker=fopen(ROOT "/boot-observe.json","rb");
    read_only_boot=marker!=NULL;if(marker) fclose(marker);
    if(read_only_boot) {
        cJSON *cfg=load_json(ROOT "/boot-observe.json");
        const cJSON *plan=cfg ? cJSON_GetObjectItemCaseSensitive(cfg,"plan_id") : NULL;
        const cJSON *hash=cfg ? cJSON_GetObjectItemCaseSensitive(cfg,"source_hash") : NULL;
        const cJSON *version=cfg ? cJSON_GetObjectItemCaseSensitive(cfg,"version") : NULL;
        const cJSON *timeout=cfg ? cJSON_GetObjectItemCaseSensitive(cfg,"observe_timeout_s") : NULL;
        boot_plan_valid=cJSON_IsNumber(version)&&version->valueint==1&&cJSON_IsString(plan)&&valid_id(plan->valuestring)&&cJSON_IsString(hash)&&strcmp(hash->valuestring,PROBE_SOURCE_HASH)==0;
        if(cJSON_IsNumber(timeout) && timeout->valuedouble==timeout->valueint && timeout->valueint>=120 && timeout->valueint<=3600) observe_timeout_s=timeout->valueint;
        if(boot_plan_valid) snprintf(boot_plan,sizeof(boot_plan),"%s",plan->valuestring);
        cJSON_Delete(cfg);svcSleepThread(INT64_C(5000000000));
    }
    snprintf(boot,sizeof(boot),"%016llx",(unsigned long long)randomGet64());
    ptc_switch_pctl_init(&adapter);pctl=ptc_switch_pctl_as_pctl(&adapter);
    pdm_rc=pdmqryInitialize();pm_rc=pmshellInitialize();
    cJSON *identity=stamp();cJSON_AddStringToObject(identity,"source_hash",PROBE_SOURCE_HASH);
    cJSON_AddNumberToObject(identity,"pdm_init_result",pdm_rc);cJSON_AddNumberToObject(identity,"pm_init_result",pm_rc);
    cJSON_AddNumberToObject(identity,"hos",hosversionGet());
    cJSON_AddStringToObject(identity,"session_kind","probe_process");
    cJSON_AddBoolToObject(identity,"read_only_boot",read_only_boot);
    cJSON_AddBoolToObject(identity,"boot_plan_valid",boot_plan_valid);
    cJSON_AddStringToObject(identity,"boot_plan_id",boot_plan);
    cJSON_AddStringToObject(identity,"history_mode",read_only_boot ? "recent_512" : "full");
    cJSON_AddNumberToObject(identity,"observe_timeout_s",observe_timeout_s);
    u64 pid=0;svcGetProcessId(&pid,CUR_PROCESS_HANDLE);i64(identity,"pid",pid);
    save_json(ROOT "/ready.json",identity);cJSON_Delete(identity);
    if(read_only_boot && !boot_plan_valid) return 1;
    observe_started=armGetSystemTick();
    unsigned tick=0;
    while(!finished) {
        cJSON *j=sample();
        if(!sampling_paused && !append_json(ROOT "/samples.jsonl",j)) {
            if(writing) restore();
            writing=false;
        }
        save_json(ROOT "/latest.json",j);cJSON_Delete(j);
        command();
        if(!sampling_paused && tick%5==0) events();
        if(!sampling_paused && tick%10==0) stats();
        ++tick;
        observe_deadline();
        /* Device deadline releases temporary restrictions even if host dies. */
        if(writing && ((restricted_deadline && armGetSystemTick()>restricted_deadline) ||
            armTicksToNs(armGetSystemTick()-write_tick)>INT64_C(3600000000000))) {
            bool ok=restore(); cJSON *deadline=stamp();cJSON_AddStringToObject(deadline,"op","deadline_restore");cJSON_AddBoolToObject(deadline,"ok",ok);
            append_json(ROOT "/actions.jsonl",deadline);cJSON_Delete(deadline);restricted_deadline=0;
            if(!ok) writing=false;
        }
        fsdevCommitDevice("sdmc");svcSleepThread(INT64_C(1000000000));
    }
    return 0;
}
