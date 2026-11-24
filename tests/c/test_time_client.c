#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include "../../platform/switch/time_client.h"

static int handles, peak_handles, sm_users, failure;
static int factory_handles, clock_handles, tz_handles;
static u64 epoch = UINT64_C(1795528709);

static void track_open(Service *s, int handle)
{
    s->handle = handle;
    ++handles;
    if (handles > peak_handles) peak_handles = handles;
    if (handle == 1) ++factory_handles;
    else if (handle == 2) ++clock_handles;
    else if (handle == 3) ++tz_handles;
}

Result smInitialize(void)
{
    if (failure == 1) return 0x160A;
    ++sm_users;
    return 0;
}

void smExit(void)
{
    assert(sm_users > 0);
    --sm_users;
}

Result smGetService(Service *s, const char *name)
{
    assert(strcmp(name, "time:u") == 0);
    if (failure == 2) return 0x160A;
    track_open(s, 1);
    return 0;
}

Result serviceDispatchImpl(Service *s, u32 id, const void *in, u32 in_size,
                           void *out, u32 out_size, SfDispatchParams p)
{
    if (s->handle == 1) {
        assert(!in && !in_size && !out && !out_size && p.out_num_objects == 1 && p.out_objects);
        if (id == 0) {
            track_open(p.out_objects, 2);
            return failure == 3 ? 0x160A : 0;
        }
        if (id == 3) {
            track_open(p.out_objects, 3);
            return failure == 5 ? 0x160A : 0;
        }
        assert(0 && "unexpected time:u factory command");
    }
    if (s->handle == 2) {
        assert(id == 0 && !in && !in_size && out && out_size == sizeof(u64) && !p.out_num_objects);
        *(u64 *)out = epoch;
        return failure == 4 ? 0x160A : 0;
    }
    if (s->handle == 3) {
        typedef struct {
            TimeCalendarTime caltime;
            TimeCalendarAdditionalInfo info;
        } CalOut;
        assert(id == 101 && in && in_size == sizeof(u64) && out && out_size == sizeof(CalOut) && !p.out_num_objects);
        assert(*(const u64 *)in == epoch);
        CalOut res;
        memset(&res, 0, sizeof(res));
        res.caltime.year = 2026;
        res.caltime.month = 11;
        res.caltime.day = 24;
        res.caltime.hour = 22;
        res.caltime.minute = 45;
        res.caltime.second = 12;
        memcpy(out, &res, sizeof(res));
        return failure == 6 ? 0x160A : 0;
    }
    assert(0 && "unexpected handle");
    return 1;
}

void serviceClose(Service *s)
{
    if (!s || !s->handle) return;
    if (s->handle == 1) --factory_handles;
    else if (s->handle == 2) --clock_handles;
    else if (s->handle == 3) --tz_handles;
    --handles;
    s->handle = 0;
    assert(handles >= 0 && factory_handles >= 0 && clock_handles >= 0 && tz_handles >= 0);
}

static void reset_peaks(void) { peak_handles = handles; }

int main(void)
{
    PtcSwitchLocalTime local;

    /* 1. Retained sysmodule path: before init must fail cleanly. */
    assert(!ptc_switch_time_read_local(&local));
    assert(!ptc_switch_time_read_local(NULL));

    /* Init failure at every stage must leak zero handles. */
    for (int f = 2; f <= 5; ++f) {
        if (f == 4) continue;
        failure = f;
        reset_peaks();
        assert(R_FAILED(ptc_switch_time_init()));
        assert(handles == 0 && factory_handles == 0 && clock_handles == 0 && tz_handles == 0);
    }

    /* Successful retained init: factory closed immediately, only 2 sub-sessions held. */
    failure = 0;
    reset_peaks();
    assert(R_SUCCEEDED(ptc_switch_time_init()));
    assert(factory_handles == 0 && clock_handles == 1 && tz_handles == 1 && handles == 2);
    assert(peak_handles == 3);
    /* Re-init is idempotent and opens no extra sessions. */
    assert(R_SUCCEEDED(ptc_switch_time_init()));
    assert(handles == 2);

    assert(ptc_switch_time_read_local(&local));
    assert(local.unix_seconds == epoch && local.year == 2026 && local.month == 11 &&
           local.day == 24 && local.hour == 22 && local.minute == 45 && local.second == 12);

    /* Read failures and invalid timestamps fail closed without losing sessions. */
    failure = 4;
    assert(!ptc_switch_time_read_local(&local) && local.unix_seconds == 0);
    failure = 6;
    assert(!ptc_switch_time_read_local(&local) && local.unix_seconds == 0);
    failure = 0;
    u64 saved_epoch = epoch;
    epoch = 0;
    assert(!ptc_switch_time_read_local(&local));
    epoch = UINT64_MAX;
    assert(!ptc_switch_time_read_local(&local));
    epoch = saved_epoch;
    assert(ptc_switch_time_read_local(&local));

    ptc_switch_time_exit();
    assert(handles == 0 && factory_handles == 0 && clock_handles == 0 && tz_handles == 0);
    assert(!ptc_switch_time_read_local(&local));

    /* 2. Short-lived overlay path: peak <= 2 handles, 0 retained on success & every error. */
    reset_peaks();
    assert(ptc_switch_time_read_local_once(&local));
    assert(local.unix_seconds == epoch && local.hour == 22 && local.minute == 45);
    assert(handles == 0 && sm_users == 0 && peak_handles <= 2);

    for (failure = 1; failure <= 6; ++failure) {
        reset_peaks();
        assert(!ptc_switch_time_read_local_once(&local) && local.unix_seconds == 0);
        assert(handles == 0 && sm_users == 0 && peak_handles <= 2);
    }
    failure = 0;
    epoch = 0;
    assert(!ptc_switch_time_read_local_once(&local) && handles == 0 && sm_users == 0);
    epoch = saved_epoch;

    /* Retained external SM reference survives repeated short-lived reads. */
    sm_users = 1;
    reset_peaks();
    for (int i = 0; i < 1000; ++i) {
        assert(ptc_switch_time_read_local_once(&local));
    }
    assert(sm_users == 1 && handles == 0 && peak_handles <= 2);
    sm_users = 0;

    puts("Minimal Switch time client (sysmodule retained & overlay short-lived) passed");
    return 0;
}
