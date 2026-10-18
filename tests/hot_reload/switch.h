#ifndef PLAYWISE_TEST_HOT_RELOAD_SWITCH_H
#define PLAYWISE_TEST_HOT_RELOAD_SWITCH_H
#include <stdint.h>
typedef uint64_t u64;
typedef uint32_t Result;
typedef struct { u64 program_id; unsigned int storageID; } NcmProgramLocation;
enum { NcmStorageId_None = 0, PmLaunchFlag_None = 0 };
#define R_FAILED(rc) ((rc) != 0)
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_MODULE(rc) ((rc) & 511U)
#define R_DESCRIPTION(rc) ((rc) >> 9)
Result pmshellInitialize(void);
void pmshellExit(void);
Result pmshellGetProcessId(u64 *pid, u64 program);
Result pmshellLaunchProgram(unsigned int flags, const NcmProgramLocation *location, u64 *pid);
Result fsdevCommitDevice(const char *device);
void svcSleepThread(int64_t ns);
u64 armGetSystemTick(void);
u64 armTicksToNs(u64 ticks);
u64 randomGet64(void);
#endif
