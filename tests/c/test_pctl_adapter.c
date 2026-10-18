#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../platform/switch/pctl_adapter.h"
#include "../../platform/switch/play_timer_settings_layout.h"

static int handles, peak_handles, spent_reads, failure, date_day = 14;
static bool midnight_during_read, clock_error;
static u64 tick = 1, unix_s = 1791914860;
static s64 spent_ns = INT64_C(4191000000000), remaining_ns = -INT64_C(591000000000);
static u16 quota = 60;
static Result spent_result;
static u32 failed_command;
static u64 read_delay;

Result smGetService(Service *s, const char *name)
{
    bool privileged = strcmp(name, "pctl:s") == 0;
    assert(privileged || strcmp(name, "pctl") == 0);
    if (privileged && failure == 1) return 777;
    s->handle = privileged ? 3 : 1;
    ++handles;
    if (handles > peak_handles) peak_handles = handles;
    return 0;
}
Result serviceConvertToDomain(Service *s) { return s->handle == 3 && failure == 2 ? 777 : 0; }
void serviceClose(Service *s) { if (s->handle) { --handles; s->handle = 0; } assert(handles >= 0); }
u64 armGetSystemTick(void) { return tick; }
u64 armTicksToNs(u64 value) { return value; }
Result timeGetCurrentTime(int type, u64 *out) { (void)type; *out = unix_s; return clock_error ? 777 : 0; }
Result timeToCalendarTimeWithMyRule(u64 value, TimeCalendarTime *out, TimeCalendarAdditionalInfo *info)
{
    (void)value; (void)info;
    out->year = 2026; out->month = 10; out->day = date_day;
    return 0;
}
Result serviceDispatchImpl(Service *s, u32 id, const void *in, u32 in_size, void *out, u32 size, SfDispatchParams p)
{
    if (s->handle == 1 || s->handle == 3) {
        assert(id == 1 && in && in_size == 8 && p.in_send_pid && p.out_num_objects == 1);
        if (s->handle == 3 && failure == 3) return 777;
        p.out_objects->handle = s->handle + 1;
        ++handles;
        if (handles > peak_handles) peak_handles = handles;
        return 0;
    }
    assert((s->handle == 2 || s->handle == 4) && !in && !in_size);
    if (id == 1) return s->handle == 4 && failure == 4 ? 777 : 0;
    if (id == failed_command) return 778;
    if (id == 1451) { assert(!out && !size); return 0; }
    if (id == 1031 || id == 1006 || id == 1453 || id == 1455) {
        assert(size == sizeof(bool));
        *(bool *)out = id == 1031 || id == 1453;
    } else if (id == 1454) {
        assert(size == sizeof(s64)); *(s64 *)out = remaining_ns;
    } else if (id == 1952) {
        assert(s->handle == 4 && size == sizeof(s64));
        ++spent_reads;
        tick += read_delay;
        *(s64 *)out = spent_ns;
        if (midnight_during_read) { ++date_day; ++unix_s; midnight_during_read = false; }
        return spent_result;
    } else {
        assert(id == 145601 && s->handle == 4 && size == 0x44);
        memset(out, 0, size);
        for (unsigned day = 0; day < 7; ++day)
            assert(ptc_play_timer_settings_set_day(out, PTC_PLAY_TIMER_SETTINGS_WORDS,
                day, quota != 0xffff, quota));
    }
    return 0;
}

static PtcPctlStatus read_status(PtcSwitchPctl *adapter)
{
    PtcPctlStatus status;
    assert(adapter->pctl.vtable->read_status(&adapter->pctl, 3, &status) == PTC_ERR_OK);
    assert(handles == 0 && peak_handles <= 2);
    return status;
}

int main(void)
{
    PtcSwitchPctl adapter;
    PtcPctlStatus status;
    ptc_switch_pctl_init(&adapter);
    status = read_status(&adapter);
    assert(status.usage_supported && status.usage_available && status.usage_ns == (u64)spent_ns);
    assert(status.signed_remaining_ns == remaining_ns && status.remaining_ns == 0);
    assert(!status.played_minutes_available); /* Does not change the control source. */
    int reads = spent_reads;
    for (int i = 0; i < 40; ++i) { tick += 100000000; read_status(&adapter); }
    assert(spent_reads == reads);
    /* Grants bypass even a successful display cache at the same tick. */
    spent_ns += UINT64_C(1000000000);
    assert(adapter.pctl.vtable->read_grant_status(&adapter.pctl, 3, &status) == PTC_ERR_OK);
    assert(status.usage_available && status.usage_ns == (u64)spent_ns && spent_reads == reads + 1);
    reads = spent_reads;
    spent_result = 777;
    assert(adapter.pctl.vtable->read_grant_status(&adapter.pctl, 3, &status) == PTC_ERR_OK);
    assert(!status.usage_available && status.usage_result == 777 && spent_reads == reads + 1);
    spent_result = 0; spent_ns -= UINT64_C(1000000000);
    assert(adapter.pctl.vtable->read_grant_status(&adapter.pctl, 3, &status) == PTC_ERR_OK);
    assert(!status.usage_available); /* Same-day decrease cannot authorize grants. */
    spent_ns += UINT64_C(1000000000);
    assert(adapter.pctl.vtable->read_grant_status(&adapter.pctl, 3, &status) == PTC_ERR_OK);
    assert(status.usage_available);
    tick += UINT64_C(5000000000); unix_s += 5; spent_result = 777;
    status = read_status(&adapter);
    assert(status.remaining_available && status.configured_minutes_available && !status.usage_available);
    assert(status.usage_result == 777 && adapter.last_result == 0);
    reads = spent_reads; read_status(&adapter); assert(spent_reads == reads);
    tick += UINT64_C(5000000000); spent_result = 0; spent_ns = -1;
    assert(!read_status(&adapter).usage_available);
    tick += UINT64_C(5000000000); spent_ns = INT64_C(4254000000000);
    assert(read_status(&adapter).usage_available);
    /* Date changes invalidate the cache even within its five-second lease. */
    ++date_day; spent_ns = 0; ++unix_s;
    status = read_status(&adapter);
    assert(status.usage_available && status.usage_ns == 0 && status.usage_day_index == 2479);
    tick += UINT64_C(5000000000); midnight_during_read = true;
    assert(!read_status(&adapter).usage_available);
    assert(read_status(&adapter).usage_available);
    /* A backward clock cannot give a preserved observation a fresh timestamp. */
    --unix_s; reads = spent_reads;
    assert(read_status(&adapter).usage_available && spent_reads == reads + 1);
    clock_error = true;
    assert(!read_status(&adapter).usage_available);
    clock_error = false;
    for (failure = 1; failure <= 4; ++failure) {
        ptc_switch_pctl_init(&adapter);
        status = read_status(&adapter);
        assert(status.usage_supported && !status.usage_available && status.remaining_available);
    }
    failure = 0;
    for (int mode = 0; mode < 2; ++mode) {
        ptc_switch_pctl_init(&adapter); quota = mode ? 0xffff : 0;
        status = read_status(&adapter);
        assert(status.usage_available && (mode ? status.unrestricted_today : status.blocked_today));
    }
    ptc_switch_pctl_init(&adapter); quota = 60; remaining_ns = INT64_C(40000000000);
    status = read_status(&adapter);
    assert(status.remaining_minutes == 0 && !ptc_pctl_remaining_exhausted(&status));
    /* Execute the production read group, including every failure boundary. */
    PtcUsageEvidenceSample raw;
    adapter.last_result = 999;
    remaining_ns = -INT64_C(591000000000);
    assert(adapter.pctl.vtable->start_timer(&adapter.pctl) == PTC_ERR_OK);
    assert(adapter.start_timer_calls == 1);
    failed_command = 1451;
    assert(adapter.pctl.vtable->start_timer(&adapter.pctl) == PTC_ERR_PCTL_WRITE_FAILED);
    assert(adapter.start_timer_calls == 2);
    failed_command = 0;
    adapter.last_result = 999;
    bool was_sampled = adapter.usage_sampled;
    u64 cached_tick = adapter.usage_sample_tick;
    read_delay = 100000000;
    assert(ptc_switch_pctl_read_evidence(&adapter, &raw));
    assert(raw.remaining_ns == remaining_ns && raw.spent_ns == spent_ns);
    assert(raw.start_timer_calls == 2 && raw.end_ns - raw.begin_ns == read_delay);
    assert(raw.timer_enabled && !raw.temporarily_unlocked && handles == 0);
    assert(adapter.last_result == 999 && adapter.usage_sampled == was_sampled && adapter.usage_sample_tick == cached_tick);
    const u32 commands[] = {1453, 1454, 1455, 1952, 145601, 1006};
    for (unsigned i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i) {
        failed_command = commands[i];
        assert(!ptc_switch_pctl_read_evidence(&adapter, &raw) && handles == 0 && adapter.last_result == 999);
        assert(raw.timer_result == (failed_command == 1453 ? 778u : 0u));
        assert(raw.remaining_result == (failed_command == 1454 ? 778u : 0u));
        assert(raw.restricted_result == (failed_command == 1455 ? 778u : 0u));
        assert(raw.spent_result == (failed_command == 1952 ? 778u : 0u));
        assert(raw.settings_result == (failed_command == 145601 ? 778u : 0u));
        assert(raw.unlocked_result == (failed_command == 1006 ? 778u : 0u));
    }
    failed_command = 0;
    for (failure = 1; failure <= 4; ++failure) {
        assert(!ptc_switch_pctl_read_evidence(&adapter, &raw) && handles == 0 && adapter.last_result == 999);
        assert(raw.session_result == 777 && raw.spent_result == 777);
    }
    failure = 0; clock_error = true;
    assert(!ptc_switch_pctl_read_evidence(&adapter, &raw));
    clock_error = false; midnight_during_read = true;
    assert(!ptc_switch_pctl_read_evidence(&adapter, &raw) && raw.day_before != raw.day_after);
    spent_ns = -1;
    assert(!ptc_switch_pctl_read_evidence(&adapter, &raw) && raw.spent_ns == -1);
    spent_ns = 0; read_delay = UINT64_C(2000000001);
    assert(!ptc_switch_pctl_read_evidence(&adapter, &raw));
    assert(handles == 0 && peak_handles <= 2);
    puts("PASS release adapter direct usage, signed remainder, evidence Result/clock guards and cleanup");
    return 0;
}
