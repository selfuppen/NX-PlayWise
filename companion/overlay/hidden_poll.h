#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 隐藏浮窗 UI 线程的协作节奏。手动快捷键扫描在输入线程，不受这里影响。

   未开启原因通知时，这个线程唯一的工作是发现家长在 NRO 里打开了开关，五秒一次
   足够；开启后保持 250ms，才能在后台提议的 2 秒有效期内完成显示与确认。后台
   身份只在启动或交接时变化，缓存十秒；读不到时每秒重试，避免在后台缺席期间
   以 250ms 反复读取同一个文件。连续五次写入失败后停止自动协作，手动入口保留。 */
#define PTC_OVERLAY_HIDDEN_FAILURE_LIMIT 5u
#define PTC_OVERLAY_HIDDEN_CONFIG_MS 5000u
#define PTC_OVERLAY_HIDDEN_RUNTIME_MS 10000u
#define PTC_OVERLAY_HIDDEN_RUNTIME_RETRY_MS 1000u

static inline uint64_t ptc_overlay_hidden_poll_ns(bool automatic_enabled, unsigned failures)
{
    if (failures >= PTC_OVERLAY_HIDDEN_FAILURE_LIMIT) return UINT64_MAX;
    return automatic_enabled ? UINT64_C(250000000) : UINT64_C(5000000000);
}

static inline uint64_t ptc_overlay_hidden_runtime_next_ms(uint64_t now_ms, bool known)
{
    uint64_t step = known ? PTC_OVERLAY_HIDDEN_RUNTIME_MS : PTC_OVERLAY_HIDDEN_RUNTIME_RETRY_MS;
    return now_ms > UINT64_MAX - step ? UINT64_MAX : now_ms + step;
}
