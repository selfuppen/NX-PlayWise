#ifndef PTC_SWITCH_TIME_CLIENT_H
#define PTC_SWITCH_TIME_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __SWITCH__
#include <switch.h>
#else
typedef uint32_t Result;
#endif

typedef struct {
    uint64_t unix_seconds;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} PtcSwitchLocalTime;

#ifdef __cplusplus
extern "C" {
#endif

/* Retained minimal-session API for the sysmodule:
 * keeps only UserSystemClock + TimeZoneService open and closes the time:u
 * factory session right after init (2 retained sub-sessions vs 6 + shmem). */
Result ptc_switch_time_init(void);
void ptc_switch_time_exit(void);
bool ptc_switch_time_read_local(PtcSwitchLocalTime *out);

/* On-demand short-lived API for the Tesla overlay:
 * closes UserSystemClock before opening TimeZoneService (peak <= 2 handles,
 * 0 handles retained after return or while hidden). */
bool ptc_switch_time_read_local_once(PtcSwitchLocalTime *out);

#ifdef __cplusplus
}
#endif

#endif
