#include <switch.h>
#include <stdio.h>
#include <sys/stat.h>
#include "probe_identity.h"

static void applet_comparison(void)
{
    FILE *flag=fopen("sdmc:/switch/playwise-usage-probe/foreground.flag","rb");
    if(!flag) return;
    fclose(flag);remove("sdmc:/switch/playwise-usage-probe/foreground.flag");
    appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
    u64 started=armGetSystemTick();
    u64 id=0;Result id_rc=appletGetCurrentApplicationId(&id);
    while(appletMainLoop() && armTicksToNs(armGetSystemTick()-started)<INT64_C(180000000000)) {
        u64 active=0;Result rc=appletGetProgramTotalActiveTime(&active);
        PdmApplicationPlayStatistics s={0};s32 got=0;
        Result stats_rc=R_SUCCEEDED(id_rc) ? appletQueryApplicationPlayStatistics(&s,&id,1,&got) : id_rc;
        FILE *f=fopen("sdmc:/switch/playwise-usage-probe/applet.jsonl","ab");
        if(f) {
            fprintf(f,"{\"tick_ns\":\"%llu\",\"result\":%u,\"active_ns\":\"%llu\",\"focus\":%u,\"application_id_result\":%u,\"application_id\":\"%016llx\",\"statistics_result\":%u,\"statistics_count\":%d,\"statistics_playtime_ns\":\"%llu\"}\n",
                (unsigned long long)armTicksToNs(armGetSystemTick()),rc,(unsigned long long)active,appletGetFocusState(),id_rc,(unsigned long long)id,stats_rc,got,(unsigned long long)s.playtime);
            fclose(f);fsdevCommitDevice("sdmc");
        }
        svcSleepThread(INT64_C(1000000000));
    }
}

/* No interactive wizard: the host drives this entry once through hbmenu. */
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    mkdir("sdmc:/switch/playwise-usage-probe", 0777);
    u64 pid = 0;
    u64 program=UINT64_C(0x4200000000BD23F1);
    FILE *flag=fopen("sdmc:/switch/playwise-usage-probe/launch-release.flag","rb");
    if(flag) {fclose(flag);program=UINT64_C(0x4200000000BD2300);}
    Result init = pmshellInitialize();
    Result rc = init;
    if (R_SUCCEEDED(init)) {
        rc = pmshellGetProcessId(&pid, program);
        if (R_FAILED(rc) && R_MODULE(rc) == 15 && R_DESCRIPTION(rc) == 1) {
            NcmProgramLocation location = { .program_id = program, .storageID = NcmStorageId_None };
            rc = pmshellLaunchProgram(PmLaunchFlag_None, &location, &pid);
        }
        pmshellExit();
    }
    FILE *f = fopen("sdmc:/switch/playwise-usage-probe/launcher.json", "wb");
    if (f) {
        fprintf(f, "{\"result\":%u,\"pid\":%llu,\"program_id\":\"%016llx\",\"identity\":\"%s\"}\n", rc, (unsigned long long)pid,(unsigned long long)program, PROBE_SOURCE_HASH);
        fclose(f); fsdevCommitDevice("sdmc");
    }
    if(R_SUCCEEDED(rc)) {
        if(program==UINT64_C(0x4200000000BD2300)) remove("sdmc:/switch/playwise-usage-probe/launch-release.flag");
        else applet_comparison();
    }
    return R_FAILED(rc) ? 1 : 0;
}
