#ifdef PLAYWISE_PCTL_CAPTURE
#include <switch.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../third_party/cjson/cJSON.h"
#include "pctl_usage_tap.h"
#include "../build/pctl-capture/generated/capture_identity.h"

#define CAPTURE_ROOT "sdmc:/switch/playwise-pctl-capture"
#define CAPTURE_CONFIG CAPTURE_ROOT "/capture.json"
static bool capture_active;
static char capture_run[33], capture_boot[25], capture_path[224];
static u64 capture_started, capture_last;
static int capture_duration;
static unsigned capture_failures;

static Result out(Service *s, u32 id, void *value, u32 size)
{
    SfDispatchParams params={0};
    return serviceDispatchImpl(s,id,NULL,0,value,size,params);
}

void pctl_usage_tap_init(const char *boot_id)
{
    FILE *f=fopen(CAPTURE_CONFIG,"rb");
    if(!f) return;
    char text[512];size_t n=fread(text,1,sizeof(text)-1,f);
    bool complete=feof(f)&&!ferror(f);fclose(f);text[n]=0;
    cJSON *j=complete ? cJSON_Parse(text) : NULL;
    const cJSON *v=j ? cJSON_GetObjectItemCaseSensitive(j,"version") : NULL;
    const cJSON *r=j ? cJSON_GetObjectItemCaseSensitive(j,"run_id") : NULL;
    const cJSON *h=j ? cJSON_GetObjectItemCaseSensitive(j,"source_hash") : NULL;
    const cJSON *d=j ? cJSON_GetObjectItemCaseSensitive(j,"duration_s") : NULL;
    bool valid=cJSON_IsNumber(v)&&v->valuedouble==1&&cJSON_IsString(r)&&strlen(r->valuestring)==32&&
        strspn(r->valuestring,"0123456789abcdef")==32&&cJSON_IsString(h)&&strcmp(h->valuestring,PCTL_CAPTURE_SOURCE_HASH)==0&&
        cJSON_IsNumber(d)&&d->valuedouble==d->valueint&&d->valueint>=300&&d->valueint<=7200;
    if(valid) {
        snprintf(capture_run,sizeof(capture_run),"%s",r->valuestring);
        snprintf(capture_boot,sizeof(capture_boot),"%s",boot_id);
        snprintf(capture_path,sizeof(capture_path),CAPTURE_ROOT "/%s-%s.jsonl",capture_run,capture_boot);
        capture_duration=d->valueint;capture_started=armGetSystemTick();capture_active=true;
    }
    cJSON_Delete(j);
}

void pctl_usage_tap_tick(const char *stage, long long unix_s, int day_index, bool force)
{
    if(!capture_active) return;
    u64 tick=armGetSystemTick();
    if(armTicksToNs(tick-capture_started)>(u64)capture_duration*UINT64_C(1000000000)) {capture_active=false;return;}
    if(!force&&capture_last&&armTicksToNs(tick-capture_last)<UINT64_C(5000000000)) return;
    capture_last=tick;
    FILE *marker=fopen(CAPTURE_CONFIG,"rb");
    if(!marker) {capture_active=false;return;}
    fclose(marker);
    Service factory={0},service={0};u64 reserved_pid=0;
    Result opened=smGetService(&factory,"pctl:s");
    if(R_SUCCEEDED(opened)) opened=serviceConvertToDomain(&factory);
    if(R_SUCCEEDED(opened)) {
        SfDispatchParams params={0};params.in_send_pid=true;params.out_num_objects=1;params.out_objects=&service;
        opened=serviceDispatchImpl(&factory,1,&reserved_pid,sizeof(reserved_pid),NULL,0,params);
    }
    if(R_SUCCEEDED(opened)) {SfDispatchParams params={0};opened=serviceDispatchImpl(&service,1,NULL,0,NULL,0,params);}
    Result enabled_rc=opened,remaining_rc=opened,restricted_rc=opened,spent_rc=opened,settings_rc=opened;
    u8 enabled=0,restricted=0,settings[0x44]={0};s64 remaining=0,spent=0;
    if(R_SUCCEEDED(opened)) {
        enabled_rc=out(&service,1453,&enabled,sizeof(enabled));
        remaining_rc=out(&service,1454,&remaining,sizeof(remaining));
        restricted_rc=out(&service,1455,&restricted,sizeof(restricted));
        spent_rc=out(&service,1952,&spent,sizeof(spent));
        settings_rc=out(&service,145601,settings,sizeof(settings));
    }
    /* Every path releases the child/domain factory. No PDM, PM, input, power
       service, extra thread, timer activation or settings-write operation. */
    serviceClose(&service);serviceClose(&factory);
    char hex[0x44*2+1];
    for(size_t i=0;i<sizeof(settings);++i) snprintf(hex+i*2,3,"%02x",settings[i]);
    char row[1600];
    int n=snprintf(row,sizeof(row),
        "{\"schema_version\":1,\"capture_marker\":\"PCTL1952_CAPTURE_ONLY\",\"source_hash\":\"%s\",\"run_id\":\"%s\",\"boot_id\":\"%s\","
        "\"stage\":\"%s\",\"tick_ns\":\"%llu\",\"unix_s\":\"%lld\",\"day_index\":%d,"
        "\"sample_error\":%u,\"timer_result\":%u,\"timer_enabled\":%s,\"remaining_result\":%u,\"remaining_ns\":\"%lld\","
        "\"restricted_result\":%u,\"restricted\":%s,\"spent_result\":%u,\"spent_ns\":\"%lld\",\"settings_result\":%u,\"settings_hex\":\"%s\",\"tap_read_only\":true}\n",
        PCTL_CAPTURE_SOURCE_HASH,capture_run,capture_boot,stage,(unsigned long long)armTicksToNs(tick),unix_s,day_index,
        opened,enabled_rc,enabled ? "true" : "false",remaining_rc,(long long)remaining,
        restricted_rc,restricted ? "true" : "false",spent_rc,(long long)spent,settings_rc,hex);
    bool saved=false;
    if(n>0&&(size_t)n<sizeof(row)) {
        FILE *f=fopen(capture_path,"ab");
        if(f) {saved=fwrite(row,1,(size_t)n,f)==(size_t)n;if(fclose(f)!=0) saved=false;}
    }
    if(!saved||R_FAILED(opened)||R_FAILED(spent_rc)) ++capture_failures;else capture_failures=0;
    if(capture_failures>=3) capture_active=false;
}
#endif
