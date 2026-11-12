#ifndef PTC_SYSMODULE_POWER_LEDGER_H
#define PTC_SYSMODULE_POWER_LEDGER_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* 后台冻结账本。

   2026-10-20 的真机 POWER 休眠原始流显示：主机真正挂起期间后台进程不被调度
   （138 秒内没有任何采样），而设备单调时钟照常前进。因此主循环相邻两次到达
   同一位置的单调间隔，若比本轮请求的等待时长多出 PTC_POWER_LEDGER_GAP_NS 以上，
   就记为一次进程冻结：真实休眠，或同样值得记录的极端调度／存储停顿。

   账本只在内存中累计。调用方在主机连续运行 PTC_POWER_LEDGER_SETTLE_NS 之后才
   一次性写出，夜间的短暂唤醒因此不产生 SD 写入。它不访问任何电源、电量或显示
   服务，不区分半唤醒与亮屏，也不能换算掉电百分比；它只回答后台实际被冻结了
   多久、中间醒了几次、每次醒了多久。 */
#define PTC_POWER_LEDGER_GAP_NS UINT64_C(15000000000)
#define PTC_POWER_LEDGER_SETTLE_NS UINT64_C(120000000000)
#define PTC_POWER_LEDGER_RETRY_NS UINT64_C(60000000000)
#define PTC_POWER_LEDGER_EPISODES 24u

typedef struct {
    int64_t resumed_at;   /* 冻结结束后第一次循环的墙钟秒 */
    int64_t wall_s;       /* 同一区间的墙钟差；与 suspended_s 相差很大说明期间改过时钟 */
    uint32_t awake_s;     /* 这次冻结之前连续运行的秒数 */
    uint32_t suspended_s; /* 单调时钟上的冻结秒数 */
} PtcPowerEpisode;

typedef struct {
    bool started;
    bool unsaved;
    uint64_t last_ns;
    int64_t last_wall;
    int64_t started_at;
    uint64_t run_ns;       /* 自上次冻结或启动以来的连续运行时间 */
    uint64_t retry_run_ns; /* 写出失败后，run_ns 达到该值才重试 */
    uint64_t awake_ns;     /* 本次启动累计运行时间 */
    uint64_t suspended_ns; /* 本次启动累计冻结时间 */
    uint32_t gaps;
    uint32_t loops;        /* 主循环轮次，用于核对实际唤醒频率 */
    uint32_t ticks;        /* 其中执行了完整调度轮次的次数 */
    uint32_t next;
    uint32_t count;
    PtcPowerEpisode episodes[PTC_POWER_LEDGER_EPISODES];
} PtcPowerLedger;

static inline uint32_t ptc_power_ledger_seconds(uint64_t ns)
{
    uint64_t seconds = ns / UINT64_C(1000000000);
    return seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
}

static inline uint64_t ptc_power_ledger_add(uint64_t total, uint64_t value)
{
    return value > UINT64_MAX - total ? UINT64_MAX : total + value;
}

/* 每个主循环轮次在同一位置调用一次。wait_ms 是本轮请求的等待上限，full_tick
   表示本轮执行了完整调度轮次。 */
static inline void ptc_power_ledger_observe(PtcPowerLedger *ledger, uint64_t now_ns,
    int64_t wall, uint32_t wait_ms, bool full_tick)
{
    uint64_t waited = (uint64_t)wait_ms * UINT64_C(1000000);
    uint64_t delta;
    if (!ledger->started || now_ns < ledger->last_ns) {
        /* 首次调用或单调时钟回退：只重建基线，不凭空生成区间。 */
        if (!ledger->started) ledger->started_at = wall;
        ledger->started = true;
        ledger->last_ns = now_ns;
        ledger->last_wall = wall;
        return;
    }
    delta = now_ns - ledger->last_ns;
    if (ledger->loops != UINT32_MAX) ++ledger->loops;
    if (full_tick && ledger->ticks != UINT32_MAX) ++ledger->ticks;
    if (delta > waited && delta - waited >= PTC_POWER_LEDGER_GAP_NS) {
        PtcPowerEpisode *episode = &ledger->episodes[ledger->next];
        uint64_t gap = delta - waited;
        episode->resumed_at = wall;
        episode->wall_s = wall - ledger->last_wall;
        episode->awake_s = ptc_power_ledger_seconds(ptc_power_ledger_add(ledger->run_ns, waited));
        episode->suspended_s = ptc_power_ledger_seconds(gap);
        ledger->next = (ledger->next + 1u) % PTC_POWER_LEDGER_EPISODES;
        if (ledger->count < PTC_POWER_LEDGER_EPISODES) ++ledger->count;
        if (ledger->gaps != UINT32_MAX) ++ledger->gaps;
        ledger->suspended_ns = ptc_power_ledger_add(ledger->suspended_ns, gap);
        ledger->awake_ns = ptc_power_ledger_add(ledger->awake_ns, waited);
        ledger->run_ns = 0;
        ledger->retry_run_ns = 0;
        ledger->unsaved = true;
    } else {
        ledger->run_ns = ptc_power_ledger_add(ledger->run_ns, delta);
        ledger->awake_ns = ptc_power_ledger_add(ledger->awake_ns, delta);
    }
    ledger->last_ns = now_ns;
    ledger->last_wall = wall;
}

/* 有新的冻结区间，且主机已连续运行足够久（多半是真实使用而非夜间短暂唤醒）。 */
static inline bool ptc_power_ledger_due(const PtcPowerLedger *ledger)
{
    return ledger->unsaved && ledger->run_ns >= PTC_POWER_LEDGER_SETTLE_NS &&
        ledger->run_ns >= ledger->retry_run_ns;
}

static inline void ptc_power_ledger_saved(PtcPowerLedger *ledger)
{
    ledger->unsaved = false;
}

/* 写出失败不逐轮重试：SD 持续异常时每分钟至多一次。 */
static inline void ptc_power_ledger_failed(PtcPowerLedger *ledger)
{
    ledger->retry_run_ns = ptc_power_ledger_add(ledger->run_ns, PTC_POWER_LEDGER_RETRY_NS);
}

/* release 与 boot 必须是调用方已校验的标识（构建常量与 16 位十六进制）。 */
static inline bool ptc_power_ledger_format(const PtcPowerLedger *ledger, const char *release,
    const char *boot, int64_t wall, char *out, size_t size)
{
    size_t used;
    uint32_t i;
    int n = snprintf(out, size,
        "{\"version\":1,\"release_id\":\"%s\",\"backend_boot_id\":\"%s\",\"updated_at\":%lld,"
        "\"started_at\":%lld,\"awake_s\":%u,\"suspended_s\":%u,\"gaps\":%u,\"loops\":%u,"
        "\"full_ticks\":%u,\"gap_threshold_s\":%u,\"episodes\":[",
        release, boot, (long long)wall, (long long)ledger->started_at,
        ptc_power_ledger_seconds(ledger->awake_ns), ptc_power_ledger_seconds(ledger->suspended_ns),
        ledger->gaps, ledger->loops, ledger->ticks,
        ptc_power_ledger_seconds(PTC_POWER_LEDGER_GAP_NS));
    if (n < 0 || (size_t)n >= size) return false;
    used = (size_t)n;
    for (i = 0; i < ledger->count; ++i) {
        const PtcPowerEpisode *episode = &ledger->episodes[
            (ledger->next + PTC_POWER_LEDGER_EPISODES - ledger->count + i) % PTC_POWER_LEDGER_EPISODES];
        n = snprintf(out + used, size - used,
            "%s{\"resumed_at\":%lld,\"awake_s\":%u,\"suspended_s\":%u,\"wall_s\":%lld}",
            i ? "," : "", (long long)episode->resumed_at, episode->awake_s,
            episode->suspended_s, (long long)episode->wall_s);
        if (n < 0 || (size_t)n >= size - used) return false;
        used += (size_t)n;
    }
    n = snprintf(out + used, size - used, "]}\n");
    return n > 0 && (size_t)n < size - used;
}
#endif
