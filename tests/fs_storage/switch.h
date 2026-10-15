#ifndef TEST_FS_STORAGE_SWITCH_H
#define TEST_FS_STORAGE_SWITCH_H
#include <stdint.h>
void svcSleepThread(int64_t nanoseconds);
int fsdevCommitDevice(const char *name);
#endif
