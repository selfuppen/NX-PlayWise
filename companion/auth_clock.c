#include "auth_clock.h"

#ifdef __SWITCH__
#include <switch.h>
#else
#include <time.h>
#endif

bool ptc_companion_auth_read_clock(int64_t *unix_seconds)
{
    if (!unix_seconds) return false;
    *unix_seconds = 0;
#ifdef __SWITCH__
    Service factory = {0}, clock = {0};
    u64 value = 0;
    Result rc = smInitialize();
    if (R_FAILED(rc)) return false;
    rc = smGetService(&factory, "time:u");
    if (R_SUCCEEDED(rc)) {
        /* libnx time:u GetStandardUserSystemClock(0), then ISystemClock
         * GetCurrentTime(0). Avoid timeInitialize's other clocks/shared memory. */
        SfDispatchParams params = {0};
        params.out_num_objects = 1;
        params.out_objects = &clock;
        rc = serviceDispatchImpl(&factory, 0, NULL, 0, NULL, 0, params);
    }
    if (R_SUCCEEDED(rc))
        rc = serviceDispatchImpl(&clock, 0, NULL, 0, &value, sizeof(value), (SfDispatchParams){0});
    serviceClose(&clock);
    serviceClose(&factory);
    smExit();
    if (R_FAILED(rc) || value == 0 || value > INT64_MAX - PTC_AUTH_MAX_COOLDOWN_SECONDS) return false;
    *unix_seconds = (int64_t)value;
#else
    /* Host/Eden state-machine tests supply their deterministic libc clock. */
    int64_t value = (int64_t)time(NULL);
    if (value <= 0 || value > INT64_MAX - PTC_AUTH_MAX_COOLDOWN_SECONDS) return false;
    *unix_seconds = value;
#endif
    return true;
}

PtcAuthStatus ptc_companion_auth_verify_pin_now(
    PtcCompanionAuth *auth, const char *pin, int64_t *retry_after_seconds)
{
    int64_t now;
    if (retry_after_seconds) *retry_after_seconds = 0;
    if (!auth || !pin) return PTC_AUTH_BAD_ARGUMENT;
    if (!ptc_companion_auth_read_clock(&now)) return PTC_AUTH_TIME_UNAVAILABLE;
    return ptc_companion_auth_verify_pin(auth, pin, now, retry_after_seconds);
}
