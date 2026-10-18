#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <switch.h>
#include "../../sysmodule/pctl_usage_tap.h"
#include "../../third_party/cjson/cJSON.h"
#include "../../build/pctl-capture/generated/capture_identity.h"

static int failure, handles, peak_handles, opens, reads;
static u64 ticks=1;
Result smGetService(Service *s,const char *name)
{
    assert(strcmp(name,"pctl:s")==0);++opens;
    if(failure==1) return 123;
    s->handle=1;++handles;if(handles>peak_handles) peak_handles=handles;return 0;
}
Result serviceConvertToDomain(Service *s) {assert(s->handle==1);return failure==2 ? 123 : 0;}
void serviceClose(Service *s) {if(s->handle) {--handles;s->handle=0;}assert(handles>=0);}
u64 armGetSystemTick(void) {return ticks;}
u64 armTicksToNs(u64 tick) {return tick;}
Result serviceDispatchImpl(Service *s,u32 id,const void *in,u32 in_size,void *value,u32 size,SfDispatchParams p)
{
    if(s->handle==1) {
        assert(id==1&&in&&in_size==8&&p.in_send_pid&&p.out_num_objects==1);
        if(failure==3) return 123;
        p.out_objects->handle=2;++handles;if(handles>peak_handles) peak_handles=handles;return 0;
    }
    assert(s->handle==2&&!in&&!in_size);
    if(id==1) return failure==4 ? 123 : 0;
    ++reads;
    if(id==1453||id==1455) {assert(size==1);*(u8 *)value=id==1453;return 0;}
    if(id==1952||id==1454) {
        assert(size==8);if(id==1952&&failure==5) return 123;
        *(s64 *)value=id==1952 ? INT64_C(1234000000000) : -INT64_C(1174000000000);return 0;
    }
    assert(id==145601&&size==0x44);memset(value,0,size);return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2);failure=atoi(argv[1]);
    mkdir("sdmc:",0700);mkdir("sdmc:/switch",0700);mkdir("sdmc:/switch/playwise-pctl-capture",0700);
    const char *config="sdmc:/switch/playwise-pctl-capture/capture.json";
    FILE *f=fopen(config,"wb");assert(f);
    fprintf(f,"{\"version\":1,\"run_id\":\"0123456789abcdef0123456789abcdef\",\"source_hash\":\"%s\",\"duration_s\":300}",failure==6 ? "wrong" : PCTL_CAPTURE_SOURCE_HASH);fclose(f);
    pctl_usage_tap_init("b1");
    for(int i=0;i<20;++i) {ticks=1+(u64)i*UINT64_C(5000000000);pctl_usage_tap_tick("standard_running",1791897000+i*5,2477,false);assert(handles==0);}
    assert(peak_handles<=2);
    if(failure==6) {assert(opens==0);puts("PASS capture rejects unbound config");return 0;}
    assert(opens==(failure ? 3 : 20));
    if(failure>0&&failure<5) assert(reads==0);
    f=fopen("sdmc:/switch/playwise-pctl-capture/0123456789abcdef0123456789abcdef-b1.jsonl","rb");assert(f);
    char text[1600];assert(fgets(text,sizeof(text),f));fclose(f);
    cJSON *j=cJSON_Parse(text);assert(j);
    const cJSON *spent_rc=cJSON_GetObjectItemCaseSensitive(j,"spent_result");
    assert(spent_rc->valueint==(failure ? 123 : 0));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"tap_read_only")));
    if(!failure) assert(strcmp(cJSON_GetObjectItemCaseSensitive(j,"spent_ns")->valuestring,"1234000000000")==0);
    cJSON_Delete(j);
    remove(config);ticks+=UINT64_C(5000000000);int before=opens;
    pctl_usage_tap_tick("standard_running",1791897105,2477,false);assert(opens==before);
    puts("PASS capture read/error cleanup, bounded handles and disable marker");return 0;
}
