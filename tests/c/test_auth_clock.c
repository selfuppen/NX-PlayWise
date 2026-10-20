#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <switch.h>
#include "../../companion/auth_clock.h"
#include "../../platform/host/mem_storage.h"

static int handles, peak_handles, sm_users, failure, libc_reads;
static u64 epoch = UINT64_C(1792479331);

/* Reproduce Tesla's positive uptime. Production Switch authentication must
 * never consult it, even when all service calls fail. */
time_t time(time_t *out)
{
    time_t uptime = 135031;
    ++libc_reads;
    if (out) *out = uptime;
    return uptime;
}
Result smInitialize(void) { if (failure == 1) return 11; ++sm_users; return 0; }
void smExit(void) { assert(sm_users > 0); --sm_users; }
static void opened(Service *s, int handle)
{
    s->handle = handle;
    ++handles;
    if (handles > peak_handles) peak_handles = handles;
}
Result smGetService(Service *s, const char *name)
{
    assert(sm_users > 0 && strcmp(name, "time:u") == 0);
    if (failure == 2) return 11;
    opened(s, 1);
    return 0;
}
Result serviceDispatchImpl(Service *s, u32 id, const void *in, u32 in_size, void *out, u32 size, SfDispatchParams p)
{
    assert(id == 0 && !in && !in_size);
    if (s->handle == 1) {
        assert(!out && !size && p.out_num_objects == 1 && p.out_objects);
        opened(p.out_objects, 2);
        return failure == 3 ? 11 : 0; /* Also release a partially returned object. */
    }
    assert(s->handle == 2 && !p.out_num_objects && !p.out_objects && size == sizeof(u64));
    *(u64 *)out = epoch;
    return failure == 4 ? 11 : 0;
}
void serviceClose(Service *s)
{
    if (s->handle) { --handles; s->handle = 0; }
    assert(handles >= 0);
}
static bool random_bytes(uint8_t *out, size_t n, void *ctx)
{
    (void)ctx;
    memset(out, 0x42, n);
    return true;
}
static void balanced(void) { assert(handles == 0 && sm_users == 0 && peak_handles <= 2 && libc_reads == 0); }

int main(void)
{
    PtcMemStorage mem;
    PtcCompanionAuth auth;
    int64_t now = 99, retry;
    char before[1024], after[1024];
    assert(ptc_companion_auth_read_clock(&now) && now == (int64_t)epoch);
    balanced();
    ptc_mem_storage_init(&mem);
    ptc_companion_auth_init(&auth, "app", &mem.storage);
    assert(ptc_companion_auth_set_pin(&auth, "123456", epoch, random_bytes, NULL) == PTC_AUTH_OK);
    for (int i = 0; i < 4; ++i)
        assert(ptc_companion_auth_verify_pin_now(&auth, "000000", &retry) == PTC_AUTH_DENIED);
    assert(ptc_companion_auth_verify_pin_now(&auth, "000000", &retry) == PTC_AUTH_COOLDOWN && retry == 30);
    /* NRO compares the same Unix deadline, not the positive Tesla uptime. */
    assert(ptc_companion_auth_verify_pin(&auth, "123456", epoch + 10, &retry) == PTC_AUTH_COOLDOWN && retry == 20);
    epoch += 30;
    assert(ptc_companion_auth_verify_pin(&auth, "123456", epoch, &retry) == PTC_AUTH_OK);
    for (int i = 0; i < 5; ++i)
        assert(ptc_companion_auth_verify_pin(&auth, "000000", epoch, &retry) == (i == 4 ? PTC_AUTH_COOLDOWN : PTC_AUTH_DENIED));
    epoch += 10;
    assert(ptc_companion_auth_verify_pin_now(&auth, "123456", &retry) == PTC_AUTH_COOLDOWN && retry == 20);
    epoch += 20;
    assert(ptc_companion_auth_verify_pin_now(&auth, "000000", &retry) == PTC_AUTH_COOLDOWN && retry == 60);
    assert(mem.storage.vtable->read_text(&mem.storage, "app/auth.json", before, sizeof(before)));
    for (failure = 1; failure <= 4; ++failure) {
        now = 99;
        assert(!ptc_companion_auth_read_clock(&now) && now == 0);
        assert(ptc_companion_auth_verify_pin_now(&auth, "123456", &retry) == PTC_AUTH_TIME_UNAVAILABLE && retry == 0);
        assert(ptc_companion_auth_verify_pin_now(&auth, "000000", &retry) == PTC_AUTH_TIME_UNAVAILABLE);
        assert(mem.storage.vtable->read_text(&mem.storage, "app/auth.json", after, sizeof(after)) && !strcmp(before, after));
        balanced();
    }
    failure = 0;
    u64 saved_epoch = epoch;
    const u64 invalid[] = {0, UINT64_MAX, (u64)INT64_MAX - PTC_AUTH_MAX_COOLDOWN_SECONDS + 1};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        epoch = invalid[i];
        assert(ptc_companion_auth_verify_pin_now(&auth, "123456", &retry) == PTC_AUTH_TIME_UNAVAILABLE);
        assert(mem.storage.vtable->read_text(&mem.storage, "app/auth.json", after, sizeof(after)) && !strcmp(before, after));
        balanced();
    }
    epoch = saved_epoch + 59;
    assert(ptc_companion_auth_verify_pin_now(&auth, "123456", &retry) == PTC_AUTH_COOLDOWN && retry == 1);
    ++epoch;
    assert(ptc_companion_auth_verify_pin_now(&auth, "123456", &retry) == PTC_AUTH_OK && retry == 0);
    for (int i = 0; i < 1000; ++i) {
        assert(ptc_companion_auth_read_clock(&now));
        balanced();
    }
    sm_users = 1; /* A caller's retained SM reference must survive the read. */
    assert(ptc_companion_auth_read_clock(&now) && sm_users == 1 && handles == 0);
    sm_users = 0;
    assert(ptc_companion_auth_verify_pin_now(NULL, "123456", &retry) == PTC_AUTH_BAD_ARGUMENT);
    balanced();
    puts("PIN canonical clock, shared deadlines and all-path resource release passed");
    return 0;
}
