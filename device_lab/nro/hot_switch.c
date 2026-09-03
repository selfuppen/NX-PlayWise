#include "hot_switch.h"

#include <switch.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../../common/version.h"
#include "../handoff_guard.h"
#include "release_manifest.h"

#define STANDARD_ROOT PLAYWISE_RELEASE_SD_ROOT
#define LAB_ROOT PLAYWISE_DEVICE_LAB_SD_ROOT
#define HANDOFF_TIMEOUT_NS 10000000000ULL
#define TARGET_READY_TIMEOUT_NS 30000000000ULL

static bool write_atomic(const char *path, const char *text)
{
    char temporary[384];
    FILE *file;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    if (!file || fwrite(text, 1, strlen(text), file) != strlen(text)) {
        if (file) (void)fclose(file);
        (void)remove(temporary);
        return false;
    }
    if (fclose(file) != 0 || rename(temporary, path) != 0) {
        (void)remove(temporary);
        return false;
    }
    return fsdevCommitDevice("sdmc") == 0;
}

static bool read_text(const char *path, char *out, size_t out_size)
{
    FILE *file = fopen(path, "rb");
    size_t got;
    if (!file) return false;
    got = fread(out, 1, out_size - 1U, file);
    if (fclose(file) != 0 || got == 0 || got >= out_size - 1U) return false;
    out[got] = '\0';
    return true;
}

static bool contains_identity(const char *text, const char *key, const char *value)
{
    char expected[192];
    snprintf(expected, sizeof(expected), "\"%s\":\"%s\"", key, value);
    return strstr(text, expected) != NULL;
}

static bool contains_pid(const char *text, u64 pid)
{
    char expected[64];
    snprintf(expected, sizeof(expected), "\"pid\":%llu", (unsigned long long)pid);
    return strstr(text, expected) != NULL;
}

static bool wait_ack(const char *root, const char *transaction_id)
{
    char path[320];
    char text[768];
    u64 started = armGetSystemTick();
    snprintf(path, sizeof(path), "%s/handover/ready.json", root);
    while (armTicksToNs(armGetSystemTick() - started) < HANDOFF_TIMEOUT_NS) {
        if (read_text(path, text, sizeof(text)) &&
            contains_identity(text, "transaction_id", transaction_id) &&
            contains_identity(text, "status", "ready")) return true;
        svcSleepThread(100000000LL);
    }
    return false;
}

static bool wait_absent(u64 program_id)
{
    u64 pid;
    unsigned int consecutive = 0;
    u64 started = armGetSystemTick();
    while (armTicksToNs(armGetSystemTick() - started) < HANDOFF_TIMEOUT_NS) {
        Result rc = pmshellGetProcessId(&pid, program_id);
        if (R_SUCCEEDED(rc)) consecutive = 0;
        else if (R_MODULE(rc) == 15 && R_DESCRIPTION(rc) == 1) {
            if (++consecutive >= 3U) return true;
        } else {
            return false;
        }
        svcSleepThread(100000000LL);
    }
    return false;
}

static bool wait_ready(const char *root, u64 program_id, u64 expected_pid, const char *profile)
{
    char path[320];
    char text[1280];
    u64 actual_pid;
    u64 started = armGetSystemTick();
    snprintf(path, sizeof(path), "%s/handover/runtime-ready.json", root);
    while (armTicksToNs(armGetSystemTick() - started) < TARGET_READY_TIMEOUT_NS) {
        if (R_SUCCEEDED(pmshellGetProcessId(&actual_pid, program_id)) && actual_pid == expected_pid &&
            read_text(path, text, sizeof(text)) && contains_pid(text, expected_pid) &&
            contains_identity(text, "profile", profile) && strstr(text, "\"boot_id\":\"") != NULL &&
            contains_identity(text, "release_id", PLAYWISE_BUILD_RELEASE_ID)) return true;
        svcSleepThread(100000000LL);
    }
    return false;
}

static bool write_intent(const char *root, char transaction_id[48])
{
    char path[320];
    char text[384];
    snprintf(transaction_id, 48, "%016llx%016llx",
        (unsigned long long)randomGet64(), (unsigned long long)randomGet64());
    snprintf(path, sizeof(path), "%s/handover/intent.json", root);
    snprintf(text, sizeof(text),
        "{\"version\":1,\"transaction_id\":\"%s\",\"action\":\"quiesce\","
        "\"created_at\":%lld}\n", transaction_id, (long long)time(NULL));
    return write_atomic(path, text);
}

static void remove_intent(const char *root)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/handover/intent.json", root);
    (void)remove(path);
    snprintf(path, sizeof(path), "%s/handover/ready.json", root);
    (void)remove(path);
    (void)fsdevCommitDevice("sdmc");
}

static void write_activation(const char *method, const char *detail)
{
    char text[512];
    snprintf(text, sizeof(text),
        "{\"version\":1,\"entry_method\":\"%s\",\"detail\":\"%s\","
        "\"release_id\":\"%s\"}\n", method, detail, PLAYWISE_BUILD_RELEASE_ID);
    (void)write_atomic(LAB_ROOT "/lab/activation.json", text);
}

static PtcHotSwitchResult reboot_fallback(const PtcLabBootFlagPaths *paths, bool entering,
    const char *reason, char *message, size_t message_size, char *technical, size_t technical_size)
{
    char flag_message[320];
    PtcLabBootFlagResult result = entering
        ? ptc_lab_boot_flags_enable(paths, flag_message, sizeof(flag_message))
        : ptc_lab_boot_flags_restore(paths, flag_message, sizeof(flag_message));
    write_activation("reboot", reason);
    snprintf(technical, technical_size, "entry_method=reboot\nreason=%s\nboot_flag_result=%d",
        reason, (int)result);
    if (result != PTC_LAB_FLAG_OK && result != PTC_LAB_FLAG_ALREADY_DONE) {
        snprintf(message, message_size, "安全切换失败，且无法准备重启回退：%s", flag_message);
        return PTC_HOT_SWITCH_REFUSED;
    }
    snprintf(message, message_size, "%s；已安全回退，请重启一次继续。", reason);
    return PTC_HOT_SWITCH_REBOOT_REQUIRED;
}

static PtcHotSwitchResult switch_process(const PtcLabBootFlagPaths *paths, bool entering,
    bool standard_was_enabled, char *message, size_t message_size,
    char *technical, size_t technical_size)
{
    const u64 source_program = entering ? UINT64_C(0x4200000000BD2300) : UINT64_C(0x4200000000BD23F0);
    const u64 target_program = entering ? UINT64_C(0x4200000000BD23F0) : UINT64_C(0x4200000000BD2300);
    const char *source_root = entering ? STANDARD_ROOT : LAB_ROOT;
    const char *target_root = entering ? LAB_ROOT : STANDARD_ROOT;
    const char *target_profile = entering ? "device-lab" : "release";
    char transaction_id[48];
    char flag_message[320];
    u64 source_pid;
    u64 target_pid;
    NcmProgramLocation location;
    PtcLabBootFlagResult flag_result;
    PtcLabHandoffGuard guard;
    memset(&guard, 0, sizeof(guard));
    Result rc = pmshellInitialize();
    if (R_FAILED(rc))
        return reboot_fallback(paths, entering, "pm:shell 不可用", message, message_size, technical, technical_size);
    rc = pmshellGetProcessId(&source_pid, source_program);
    if (R_FAILED(rc)) {
        pmshellExit();
        return reboot_fallback(paths, entering, "无法确认源后台进程", message, message_size, technical, technical_size);
    }
    guard.source_confirmed = true;
    rc = pmshellGetProcessId(&target_pid, target_program);
    if (R_SUCCEEDED(rc)) {
        pmshellExit();
        snprintf(message, message_size, "检测到源和目标后台可能同时存在，已拒绝切换且未修改启动状态。");
        snprintf(technical, technical_size, "entry_method=refused\nreason=target_already_running");
        return PTC_HOT_SWITCH_REFUSED;
    }
    if (R_MODULE(rc) != 15 || R_DESCRIPTION(rc) != 1) {
        pmshellExit();
        return reboot_fallback(paths, entering, "无法证明目标后台当前未运行",
            message, message_size, technical, technical_size);
    }
    guard.target_absent = true;
    if (!write_intent(source_root, transaction_id) || !wait_ack(source_root, transaction_id)) {
        remove_intent(source_root);
        pmshellExit();
        return reboot_fallback(paths, entering, "后台未在时限内完成静默交接", message, message_size, technical, technical_size);
    }
    guard.quiesce_ready = true;
    if (!ptc_lab_handoff_can_commit(&guard)) {
        remove_intent(source_root);
        pmshellExit();
        return reboot_fallback(paths, entering, "交接前置证明不完整", message, message_size, technical, technical_size);
    }
    flag_result = entering
        ? ptc_lab_boot_flags_enable(paths, flag_message, sizeof(flag_message))
        : ptc_lab_boot_flags_restore(paths, flag_message, sizeof(flag_message));
    if (flag_result != PTC_LAB_FLAG_OK && flag_result != PTC_LAB_FLAG_ALREADY_DONE) {
        remove_intent(source_root);
        pmshellExit();
        snprintf(message, message_size, "启动标志 journal 提交失败，源后台未被替换：%s", flag_message);
        snprintf(technical, technical_size, "entry_method=refused\nreason=boot_journal_failed\nresult=%d", (int)flag_result);
        return PTC_HOT_SWITCH_REFUSED;
    }
    guard.journal_committed = true;
    if (!wait_absent(source_program)) {
        pmshellExit();
        return reboot_fallback(paths, entering, "无法证明源后台已退出，禁止启动目标后台", message, message_size, technical, technical_size);
    }
    guard.source_absent = true;
    remove_intent(source_root);
    if (!entering && !standard_was_enabled) {
        write_activation("hot_switch", "lab_stopped_standard_was_disabled");
        pmshellExit();
        snprintf(message, message_size, "实验后台已安全退出；正常后台原本未启用，因此没有启动它。");
        snprintf(technical, technical_size, "entry_method=hot_switch\nsource_pid=%llu\ntarget=not_requested",
            (unsigned long long)source_pid);
        return PTC_HOT_SWITCH_COMPLETE;
    }
    memset(&location, 0, sizeof(location));
    location.program_id = target_program;
    location.storageID = NcmStorageId_None;
    {
        char stale_ready[320];
        snprintf(stale_ready, sizeof(stale_ready), "%s/handover/runtime-ready.json", target_root);
        (void)remove(stale_ready);
        (void)fsdevCommitDevice("sdmc");
    }
    if (!ptc_lab_handoff_can_launch(&guard)) {
        pmshellExit();
        return reboot_fallback(paths, entering, "源进程退出证明不完整", message, message_size, technical, technical_size);
    }
    rc = pmshellLaunchProgram(PmLaunchFlag_None, &location, &target_pid);
    if (R_FAILED(rc) || !wait_ready(target_root, target_program, target_pid, target_profile)) {
        pmshellExit();
        return reboot_fallback(paths, entering, "目标后台启动或身份校验失败", message, message_size, technical, technical_size);
    }
    pmshellExit();
    write_activation("hot_switch", entering ? "entered_lab" : "returned_release");
    snprintf(message, message_size, entering
        ? "已零重启切换到 Device Lab，可以直接打开浮窗开始资格批次。"
        : "已零重启恢复正常 PlayWise 后台。");
    snprintf(technical, technical_size,
        "entry_method=hot_switch\nsource_pid=%llu\ntarget_pid=%llu\nrelease_id=%s",
        (unsigned long long)source_pid, (unsigned long long)target_pid, PLAYWISE_BUILD_RELEASE_ID);
    return PTC_HOT_SWITCH_COMPLETE;
}

PtcHotSwitchResult ptc_hot_switch_enter(const PtcLabBootFlagPaths *paths,
    char *message, size_t message_size, char *technical, size_t technical_size)
{
    return switch_process(paths, true, true, message, message_size, technical, technical_size);
}

PtcHotSwitchResult ptc_hot_switch_leave(const PtcLabBootFlagPaths *paths,
    bool standard_was_enabled, char *message, size_t message_size,
    char *technical, size_t technical_size)
{
    return switch_process(paths, false, standard_was_enabled,
        message, message_size, technical, technical_size);
}
