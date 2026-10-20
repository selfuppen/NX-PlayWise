#ifndef TEST_AUTH_CLOCK_SWITCH_H
#define TEST_AUTH_CLOCK_SWITCH_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint32_t Result;
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { int handle; } Service;
typedef struct { int out_num_objects; Service *out_objects; } SfDispatchParams;
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
Result smInitialize(void);
void smExit(void);
Result smGetService(Service *, const char *);
Result serviceDispatchImpl(Service *, u32, const void *, u32, void *, u32, SfDispatchParams);
void serviceClose(Service *);
#endif
