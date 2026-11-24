#ifndef TEST_TIME_CLIENT_SWITCH_H
#define TEST_TIME_CLIENT_SWITCH_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef uint32_t Result;
typedef struct { int handle; } Service;
typedef struct { int out_num_objects; Service *out_objects; } SfDispatchParams;
typedef struct {
    u16 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
    u8 pad;
} TimeCalendarTime;
typedef struct {
    u32 wday;
    u32 yday;
    char timezoneName[8];
    u32 DST;
    s32 offset;
} TimeCalendarAdditionalInfo;
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
Result smInitialize(void);
void smExit(void);
Result smGetService(Service *, const char *);
Result serviceDispatchImpl(Service *, u32, const void *, u32, void *, u32, SfDispatchParams);
void serviceClose(Service *);
#endif
