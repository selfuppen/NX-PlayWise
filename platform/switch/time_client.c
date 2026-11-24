#include "time_client.h"

#include <string.h>

#ifdef __SWITCH__

/* Horizon OS time:u (IStaticService) command IDs:
 *   0: GetStandardUserSystemClock -> ISystemClock (cmd 0: GetCurrentTime)
 *   3: GetTimeZoneService         -> ITimeZoneService (cmd 101: ToCalendarTimeWithMyRule)
 * Avoids libnx timeInitialize, which also retains NetworkSystemClock,
 * SteadyClock, LocalSystemClock, time:u factory, and shared memory. */
enum {
    TIME_STATIC_CMD_GET_USER_CLOCK = 0,
    TIME_STATIC_CMD_GET_TIME_ZONE = 3,
    TIME_CLOCK_CMD_GET_CURRENT_TIME = 0,
    TIME_TZ_CMD_TO_CALENDAR_WITH_MY_RULE = 101,
};

typedef struct {
    TimeCalendarTime caltime;
    TimeCalendarAdditionalInfo info;
} PtcTimeCalendarOut;

static Service g_user_clock;
static Service g_time_zone;
static bool g_time_initialized = false;

static Result open_sub_service(Service *factory, u32 cmd_id, Service *out)
{
    SfDispatchParams params;
    memset(&params, 0, sizeof(params));
    params.out_num_objects = 1;
    params.out_objects = out;
    return serviceDispatchImpl(factory, cmd_id, NULL, 0, NULL, 0, params);
}

static Result read_user_clock_unix(Service *user_clock, u64 *out_ts)
{
    SfDispatchParams params;
    memset(&params, 0, sizeof(params));
    *out_ts = 0;
    return serviceDispatchImpl(user_clock, TIME_CLOCK_CMD_GET_CURRENT_TIME,
                               NULL, 0, out_ts, sizeof(*out_ts), params);
}

static Result convert_to_calendar(Service *tz, u64 ts, TimeCalendarTime *out_cal)
{
    SfDispatchParams params;
    PtcTimeCalendarOut res;
    Result rc;
    memset(&params, 0, sizeof(params));
    memset(&res, 0, sizeof(res));
    rc = serviceDispatchImpl(tz, TIME_TZ_CMD_TO_CALENDAR_WITH_MY_RULE,
                             &ts, sizeof(ts), &res, sizeof(res), params);
    if (R_SUCCEEDED(rc)) {
        *out_cal = res.caltime;
    }
    return rc;
}

static void fill_local_time(PtcSwitchLocalTime *out, u64 ts, const TimeCalendarTime *cal)
{
    out->unix_seconds = ts;
    out->year = cal->year;
    out->month = cal->month;
    out->day = cal->day;
    out->hour = cal->hour;
    out->minute = cal->minute;
    out->second = cal->second;
}

Result ptc_switch_time_init(void)
{
    Service factory;
    Result rc;

    if (g_time_initialized) {
        return 0;
    }

    memset(&factory, 0, sizeof(factory));
    memset(&g_user_clock, 0, sizeof(g_user_clock));
    memset(&g_time_zone, 0, sizeof(g_time_zone));

    rc = smGetService(&factory, "time:u");
    if (R_SUCCEEDED(rc)) {
        rc = open_sub_service(&factory, TIME_STATIC_CMD_GET_USER_CLOCK, &g_user_clock);
    }
    if (R_SUCCEEDED(rc)) {
        rc = open_sub_service(&factory, TIME_STATIC_CMD_GET_TIME_ZONE, &g_time_zone);
    }

    /* Release the time:u port session immediately; sub-sessions live independently. */
    serviceClose(&factory);

    if (R_FAILED(rc)) {
        serviceClose(&g_time_zone);
        serviceClose(&g_user_clock);
        memset(&g_user_clock, 0, sizeof(g_user_clock));
        memset(&g_time_zone, 0, sizeof(g_time_zone));
        return rc;
    }

    g_time_initialized = true;
    return 0;
}

void ptc_switch_time_exit(void)
{
    serviceClose(&g_time_zone);
    serviceClose(&g_user_clock);
    memset(&g_user_clock, 0, sizeof(g_user_clock));
    memset(&g_time_zone, 0, sizeof(g_time_zone));
    g_time_initialized = false;
}

bool ptc_switch_time_read_local(PtcSwitchLocalTime *out)
{
    u64 ts = 0;
    TimeCalendarTime cal;

    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!g_time_initialized) {
        return false;
    }

    if (R_FAILED(read_user_clock_unix(&g_user_clock, &ts)) ||
        ts == 0 || ts > (u64)INT64_MAX) {
        return false;
    }
    memset(&cal, 0, sizeof(cal));
    if (R_FAILED(convert_to_calendar(&g_time_zone, ts, &cal))) {
        return false;
    }

    fill_local_time(out, ts, &cal);
    return true;
}

bool ptc_switch_time_read_local_once(PtcSwitchLocalTime *out)
{
    Service factory;
    Service user_clock;
    Service tz;
    u64 ts = 0;
    TimeCalendarTime cal;
    Result rc;
    bool ok = false;

    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    rc = smInitialize();
    if (R_FAILED(rc)) {
        return false;
    }

    memset(&factory, 0, sizeof(factory));
    memset(&user_clock, 0, sizeof(user_clock));
    memset(&tz, 0, sizeof(tz));
    memset(&cal, 0, sizeof(cal));

    rc = smGetService(&factory, "time:u");
    if (R_SUCCEEDED(rc)) {
        rc = open_sub_service(&factory, TIME_STATIC_CMD_GET_USER_CLOCK, &user_clock);
        if (R_SUCCEEDED(rc)) {
            rc = read_user_clock_unix(&user_clock, &ts);
        }
        /* Close clock before opening timezone so peak sessions stay <= 2. */
        serviceClose(&user_clock);
    }

    if (R_SUCCEEDED(rc) && ts > 0 && ts <= (u64)INT64_MAX) {
        rc = open_sub_service(&factory, TIME_STATIC_CMD_GET_TIME_ZONE, &tz);
        if (R_SUCCEEDED(rc)) {
            rc = convert_to_calendar(&tz, ts, &cal);
        }
        serviceClose(&tz);
        if (R_SUCCEEDED(rc)) {
            fill_local_time(out, ts, &cal);
            ok = true;
        }
    }

    serviceClose(&factory);
    smExit();
    return ok;
}

#else

Result ptc_switch_time_init(void) { return 0; }
void ptc_switch_time_exit(void) {}
bool ptc_switch_time_read_local(PtcSwitchLocalTime *out)
{
    if (out) memset(out, 0, sizeof(*out));
    return false;
}
bool ptc_switch_time_read_local_once(PtcSwitchLocalTime *out)
{
    if (out) memset(out, 0, sizeof(*out));
    return false;
}

#endif
