#ifndef TEST_PCTL_ADAPTER_SWITCH_H
#define TEST_PCTL_ADAPTER_SWITCH_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef uint32_t Result;
typedef struct { int handle; } Service;
typedef struct { bool in_send_pid; int out_num_objects; Service *out_objects; } SfDispatchParams;
typedef struct { u8 rating_age; bool sns_post_restriction; bool free_communication_restriction; } PctlRestrictionSettings;
typedef struct { int year, month, day; } TimeCalendarTime;
typedef struct { int unused; } TimeCalendarAdditionalInfo;
#define TimeType_Default 0
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
Result smGetService(Service *, const char *);
Result serviceConvertToDomain(Service *);
Result serviceDispatchImpl(Service *, u32, const void *, u32, void *, u32, SfDispatchParams);
void serviceClose(Service *);
u64 armGetSystemTick(void);
u64 armTicksToNs(u64);
Result timeGetCurrentTime(int, u64 *);
Result timeToCalendarTimeWithMyRule(u64, TimeCalendarTime *, TimeCalendarAdditionalInfo *);
#endif
