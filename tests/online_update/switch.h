#ifndef PTC_TEST_UPDATE_SWITCH_H
#define PTC_TEST_UPDATE_SWITCH_H
#include <stdint.h>
#include "../hot_reload/switch.h"
#include <stddef.h>
typedef struct { void (*entry)(void *); void *ctx; } Thread;
enum { NifmServiceType_User = 0 };
Result socketInitializeDefault(void);
void socketExit(void);
Result nifmInitialize(int service);
void nifmExit(void);
Result threadCreate(Thread *thread, void (*entry)(void *), void *ctx, void *stack, size_t size, int priority, int core);
Result threadStart(Thread *thread);
Result threadWaitForExit(Thread *thread);
Result threadClose(Thread *thread);
#endif
