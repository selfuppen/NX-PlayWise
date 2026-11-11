#ifndef PTC_SYSMODULE_CORE_H
#define PTC_SYSMODULE_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "../platform/pctl.h"
#include "../platform/storage.h"
#include "../platform/time_provider.h"
#include "../platform/calendar_store.h"
#include "../platform/operation_mode.h"
#include "../common/rules/rules.h"
#include "runtime_state.h"

typedef struct {
    char app_root[96];
    PtcStorage *storage;
    bool (*config_random)(void *ctx, uint8_t *out, size_t size);
    void *config_random_ctx;
    PtcPctl *pctl;
    PtcTimeProvider *time_provider;
    PtcOperationModeProvider *operation_mode_provider;
    int64_t dock_last_sample_at;
    bool dock_boot_sampled;
    uint32_t scan_backoff_ms;
    uint16_t last_minute_day_index;
    uint16_t last_minute_of_day;
    uint16_t last_cleanup_day_index;
    bool minute_initialized;
    bool cleanup_initialized;
    bool startup_recovery_checked;
    bool disable_initialized;
    bool disable_present;
    bool quiescing;
    /* 唤醒提示：每个调度轮次末尾刷新，下一次唤醒计算与轮次到期探测直接复用，
       使同一轮次内的规则/日历与状态只加载一次。仅主循环线程读写，不跨进程；
       它们只决定何时执行完整轮次，完整轮次仍重新读取并校验全部输入。 */
    bool wait_hint_dock_policy;
    bool wait_hint_eye_valid;
    int64_t wait_hint_eye_deadline;
    /* 最近一次读到或写入的 setup 为 released 且带宽限期限时的到期秒，否则为 0。 */
    int64_t setup_activate_after_hint;
    char boot_id[24];
    char config_cache_text[4096];
    PtcRules rules_cache;
    PtcRuntimeState state_cache;
    PtcStorageMetadata config_meta;
    PtcStorageMetadata rules_meta;
    PtcStorageMetadata state_meta;
    bool config_cache_valid;
    bool rules_cache_valid;
    bool state_cache_valid;
    PtcCalendarRuntime calendar_runtime;
} PtcSysmodule;

void ptc_sysmodule_init(
    PtcSysmodule *sysmodule,
    const char *app_root,
    PtcStorage *storage,
    PtcPctl *pctl,
    PtcTimeProvider *time_provider);
void ptc_sysmodule_set_boot_id(PtcSysmodule *sysmodule, const char *boot_id);
int ptc_sysmodule_recover_processing(PtcSysmodule *sysmodule);
int ptc_sysmodule_bootstrap_setup(PtcSysmodule *sysmodule);
int ptc_sysmodule_process_all(PtcSysmodule *sysmodule);
int ptc_sysmodule_enforce_tick(PtcSysmodule *sysmodule);
uint32_t ptc_sysmodule_note_scan_result(PtcSysmodule *sysmodule, bool found_work);
uint32_t ptc_sysmodule_current_scan_interval(const PtcSysmodule *sysmodule);
uint32_t ptc_sysmodule_next_wait_ms(PtcSysmodule *sysmodule);
int ptc_sysmodule_scheduler_tick(PtcSysmodule *sysmodule, bool storage_notified);
/* 主循环在未收到 IPC 通知时用它决定是否执行完整轮次。elapsed_ms 为距上次完整
   轮次的单调时间。返回 false 表示此刻执行完整轮次不会产生任何可观察动作：
   没有待处理请求、标志变化、分钟/日期变化、到期的护眼或 setup 宽限，也未到
   队列扫描退避间隔。它不加载规则、状态或 setup，也不访问 PCTL。 */
bool ptc_sysmodule_tick_due(PtcSysmodule *sysmodule, uint32_t elapsed_ms);
int ptc_sysmodule_cleanup(PtcSysmodule *sysmodule);
int ptc_sysmodule_rollover_legacy_logs(PtcSysmodule *sysmodule);
bool ptc_sysmodule_refresh_caches(PtcSysmodule *sysmodule);
#ifdef PLAYWISE_EDEN
/* Emulator-only synchronous bridge. Eden's host-backed SD filesystem cannot
   reliably rename open queue files across directories. */
void ptc_sysmodule_process_request_direct(PtcSysmodule *sysmodule,
    const char *request_text, const char *expected_request_id);
#endif

#endif
