#include <switch.h>

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../../common/version.h"
#include "../boot_flags.h"
#include "../ui_model.h"
#include "ui_graphics.h"
#include "release_manifest.h"

__attribute__((used)) static const char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

static const PtcLabBootFlagPaths FLAG_PATHS = {
    "sdmc:/atmosphere/contents/4200000000BD2300/flags/boot2.flag",
    "sdmc:/atmosphere/contents/4200000000BD2300/flags/boot2.flag.playwise-device-lab-backup",
    "sdmc:/atmosphere/contents/4200000000BD23F0/flags/boot2.flag",
    PLAYWISE_DEVICE_LAB_SD_ROOT "/lab/boot-switch.json"
};

#define STANDARD_FLAGS_DIR "sdmc:/atmosphere/contents/4200000000BD2300/flags"
#define DEVICE_LAB_FLAGS_DIR "sdmc:/atmosphere/contents/4200000000BD23F0/flags"

typedef enum {
    OPERATION_NONE = 0,
    OPERATION_ENABLE = 1,
    OPERATION_RESTORE_PCTL = 2,
    OPERATION_RESTORE_FLAGS = 3
} Operation;

typedef struct {
    PtcLabNroUi ui;
    PtcLabBootStatus boot;
    Operation confirm_operation;
    Operation waiting_operation;
    bool lab_service_running;
    bool touch_down;
    u64 confirm_started;
    u64 wait_started;
    char restore_request_id[80];
} App;

static bool ensure_directories(char *failed_path, size_t failed_path_size)
{
    static const char *const paths[] = {
        PLAYWISE_DEVICE_LAB_SD_ROOT,
        PLAYWISE_DEVICE_LAB_SD_ROOT "/lab",
        PLAYWISE_DEVICE_LAB_SD_ROOT "/reports",
        PLAYWISE_DEVICE_LAB_SD_ROOT "/inbox",
        PLAYWISE_DEVICE_LAB_SD_ROOT "/inbox/pending",
        /* The opt-in package intentionally has no boot2.flag. Zip extraction
           therefore may not materialize this otherwise-empty directory. */
        STANDARD_FLAGS_DIR,
        DEVICE_LAB_FLAGS_DIR
    };
    size_t index;
    for (index = 0; index < sizeof(paths) / sizeof(paths[0]); ++index) {
        if (mkdir(paths[index], 0777) != 0 && errno != EEXIST) {
            if (failed_path && failed_path_size) snprintf(failed_path, failed_path_size, "%s", paths[index]);
            return false;
        }
    }
    return true;
}

static PtcLabSessionLoadStatus load_session(PtcLabSessionView *view)
{
    char text[4096];
    FILE *file = fopen(PLAYWISE_DEVICE_LAB_SD_ROOT "/lab/session.json", "rb");
    size_t got;
    if (!file) return errno == ENOENT ? PTC_LAB_SESSION_MISSING : PTC_LAB_SESSION_INVALID;
    got = fread(text, 1, sizeof(text) - 1U, file);
    if (fclose(file) != 0 || got == 0 || got >= sizeof(text) - 1U) return PTC_LAB_SESSION_INVALID;
    text[got] = '\0';
    return ptc_lab_session_parse(text, view) ? PTC_LAB_SESSION_VALID : PTC_LAB_SESSION_INVALID;
}

static bool find_latest_report(char *path, size_t path_size)
{
    DIR *directory = opendir(PLAYWISE_DEVICE_LAB_SD_ROOT "/reports");
    struct dirent *entry;
    char latest[192] = "";
    if (!directory) return false;
    while ((entry = readdir(directory)) != NULL) {
        size_t length = strlen(entry->d_name);
        if (length > 5U && strcmp(entry->d_name + length - 5U, ".json") == 0 &&
            strcmp(entry->d_name, latest) > 0) snprintf(latest, sizeof(latest), "%s", entry->d_name);
    }
    closedir(directory);
    if (!latest[0]) return false;
    snprintf(path, path_size, "%s/reports/%s", PLAYWISE_DEVICE_LAB_SD_ROOT, latest);
    return true;
}

static void set_feedback(App *app, bool error, const char *message, const char *technical)
{
    app->ui.message_is_error = error;
    snprintf(app->ui.message, sizeof(app->ui.message), "%s", message ? message : "");
    snprintf(app->ui.technical, sizeof(app->ui.technical), "%s", technical ? technical : "");
}

static void refresh_status(App *app)
{
    bool inspected = ptc_lab_boot_flags_inspect(&FLAG_PATHS, &app->boot);
    memset(&app->ui.session, 0, sizeof(app->ui.session));
    app->ui.session_status = load_session(&app->ui.session);
    /* Never probe pwtl:u from the NRO. SM may wait indefinitely for an absent
       service both immediately after enabling the boot flag and in a torn
       boot-switch transaction. A persisted session proves that the Lab
       backend has run; all NRO recovery requests use the durable SD queue. */
    app->lab_service_running = inspected && app->boot.state == PTC_LAB_BOOT_ENABLED &&
        app->ui.session_status != PTC_LAB_SESSION_MISSING;
    app->ui.report_available = find_latest_report(app->ui.report_path, sizeof(app->ui.report_path));
    app->ui.stage = inspected ? ptc_lab_nro_stage(&app->boot, app->lab_service_running,
        app->ui.session_status, &app->ui.session) : PTC_LAB_NRO_CONFLICT;
    if (!app->ui.message[0]) {
        snprintf(app->ui.technical, sizeof(app->ui.technical),
            "boot_state=%d\njournal_phase=%s\nlab_runtime_evidence=%s\nsession=%d\nroot=%s",
            inspected ? (int)app->boot.state : -1,
            inspected && app->boot.journal_phase[0] ? app->boot.journal_phase : "none",
            app->lab_service_running ? "present" : "absent",
            (int)app->ui.session_status, PLAYWISE_DEVICE_LAB_SD_ROOT);
    }
}

static bool session_restore_proved(void)
{
    PtcLabSessionView session;
    PtcLabSessionLoadStatus status = load_session(&session);
    if (status == PTC_LAB_SESSION_MISSING) return true;
    return status == PTC_LAB_SESSION_VALID && session.restored &&
        strcmp(session.restore_verdict, "exact_restore_proved") == 0;
}

static bool queue_session_restore(App *app)
{
    char temporary[320];
    char pending[320];
    FILE *file;
    snprintf(app->restore_request_id, sizeof(app->restore_request_id),
        "lab-restore-%016llx", (unsigned long long)randomGet64());
    snprintf(temporary, sizeof(temporary), PLAYWISE_DEVICE_LAB_SD_ROOT "/inbox/pending/%s.tmp",
        app->restore_request_id);
    snprintf(pending, sizeof(pending), PLAYWISE_DEVICE_LAB_SD_ROOT "/inbox/pending/%s.json",
        app->restore_request_id);
    file = fopen(temporary, "wb");
    if (!file || fprintf(file,
            "{\"version\":1,\"request_id\":\"%s\",\"type\":\"lab_session_restore\","
            "\"created_at\":%lld,\"payload\":{}}\n", app->restore_request_id, (long long)time(NULL)) < 0) {
        if (file) (void)fclose(file);
        (void)remove(temporary);
        set_feedback(app, true, "无法创建 PCTL 恢复请求，启动标志没有改变。",
            "result=restore_request_write_failed\npath=inbox/pending\naction=check_sd_and_retry");
        return false;
    }
    if (fclose(file) != 0 || rename(temporary, pending) != 0) {
        (void)remove(temporary);
        set_feedback(app, true, "无法把 PCTL 恢复请求加入队列，启动标志没有改变。",
            "result=restore_request_queue_failed\npath=inbox/pending\naction=check_sd_and_retry");
        return false;
    }
    (void)fsdevCommitDevice("sdmc");
    return true;
}

static void complete_flag_operation(App *app, Operation operation)
{
    char message[320];
    PtcLabBootFlagResult result;
    if (operation == OPERATION_ENABLE)
        result = ptc_lab_boot_flags_enable(&FLAG_PATHS, message, sizeof(message));
    else
        result = ptc_lab_boot_flags_restore(&FLAG_PATHS, message, sizeof(message));
    set_feedback(app, result != PTC_LAB_FLAG_OK && result != PTC_LAB_FLAG_ALREADY_DONE, message, "");
    snprintf(app->ui.technical, sizeof(app->ui.technical),
        "boot_flag_result=%d\njournal=%s\nlab_flag=%s",
        (int)result, FLAG_PATHS.journal, FLAG_PATHS.lab_flag);
    refresh_status(app);
}

static void begin_restore(App *app, Operation operation)
{
    if (session_restore_proved()) {
        if (operation == OPERATION_RESTORE_FLAGS) complete_flag_operation(app, operation);
        else {
            set_feedback(app, false, "PCTL 原设置已经得到精确恢复证明，可以继续下一步。",
                "restore_verdict=exact_restore_proved");
            refresh_status(app);
        }
        return;
    }
    if (app->ui.session_status == PTC_LAB_SESSION_INVALID) {
        set_feedback(app, true, "session.json 无法可信读取，已停止恢复请求和启动切换。",
            "result=session_invalid\npath=" PLAYWISE_DEVICE_LAB_SD_ROOT "/lab/session.json");
        return;
    }
    if (!app->lab_service_running) {
        set_feedback(app, true, "实验后台没有运行，无法证明 PCTL 已恢复。请重启到实验后台后重试。",
            "result=pwtl_service_unavailable\nservice=" PLAYWISE_DEVICE_LAB_IPC_SERVICE);
        return;
    }
    if (!queue_session_restore(app)) return;
    app->waiting_operation = operation;
    app->wait_started = armGetSystemTick();
    app->ui.modal = PTC_LAB_NRO_MODAL_WORKING;
    snprintf(app->ui.message, sizeof(app->ui.message),
        "正在请求实验后台恢复，并等待原始 0x44 与计时器的精确回读证明...");
}

static void update_waiting(App *app)
{
    if (app->waiting_operation == OPERATION_NONE) return;
    if (session_restore_proved()) {
        Operation completed = app->waiting_operation;
        app->waiting_operation = OPERATION_NONE;
        app->ui.modal = PTC_LAB_NRO_MODAL_NONE;
        if (completed == OPERATION_RESTORE_FLAGS) complete_flag_operation(app, completed);
        else {
            set_feedback(app, false, "PCTL 原设置已精确恢复，可以继续恢复正常后台。",
                "restore_verdict=exact_restore_proved");
            refresh_status(app);
        }
        return;
    }
    if (armTicksToNs(armGetSystemTick() - app->wait_started) >= 10000000000ULL) {
        app->waiting_operation = OPERATION_NONE;
        app->ui.modal = PTC_LAB_NRO_MODAL_NONE;
        set_feedback(app, true, "等待恢复证明超时。启动标志没有改变，请打开浮窗选择“立即恢复”。",
            "result=restore_proof_timeout\ntimeout_ms=10000");
        snprintf(app->ui.technical, sizeof(app->ui.technical),
            "result=restore_proof_timeout\ntimeout_ms=10000\nrequest_id=%s", app->restore_request_id);
        refresh_status(app);
    }
}

static void activate_selected(App *app, bool *running)
{
    if (app->ui.selected == 2) { *running = false; return; }
    if (app->ui.selected == 1) {
        if (app->ui.report_available) app->ui.modal = PTC_LAB_NRO_MODAL_REPORT;
        return;
    }
    switch (app->ui.stage) {
    case PTC_LAB_NRO_PREPARE:
        app->confirm_operation = OPERATION_ENABLE;
        app->ui.modal = PTC_LAB_NRO_MODAL_CONFIRM;
        break;
    case PTC_LAB_NRO_RESTORE_NORMAL:
    case PTC_LAB_NRO_RECOVER_FLAGS:
        app->confirm_operation = OPERATION_RESTORE_FLAGS;
        app->ui.modal = PTC_LAB_NRO_MODAL_CONFIRM;
        break;
    case PTC_LAB_NRO_RESTORE_PCTL:
        begin_restore(app, OPERATION_RESTORE_PCTL);
        break;
    default:
        *running = false;
        break;
    }
}

static void fallback_console(void)
{
    PadState pad;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    printf("任我玩 Device Lab\n\n");
    printf("无法初始化中文图形界面。\n");
    printf("请确认系统共享中文字体和 FreeType 环境可用。\n\n");
    printf("错误代码: graphics_init_failed\n");
    printf("按 B 退出。\n");
    consoleUpdate(NULL);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_B) break;
        svcSleepThread(16000000LL);
    }
    consoleExit(NULL);
}

int main(int argc, char **argv)
{
    App app;
    PadState pad;
    bool running = true;
    char failed_path[320] = "";
    (void)argc;
    (void)argv;
    { const char *volatile anchor = PLAYWISE_EMBEDDED_MANIFEST; (void)anchor; }
    memset(&app, 0, sizeof(app));
    if (!ptc_lab_nro_graphics_init()) { fallback_console(); return 1; }
    hidInitializeTouchScreen();
    /* libnx's default NRO runtime keeps SM initialized and mounts sdmc before
       main. Mounting sdmc again fails because the device name already exists
       and previously made a healthy boot-flag state look like a conflict. */
    if (!ensure_directories(failed_path, sizeof(failed_path))) {
        set_feedback(&app, true, "无法准备 Device Lab 目录，所有操作均已禁用。", "");
        snprintf(app.ui.technical, sizeof(app.ui.technical), "result=mkdir_failed\npath=%s\nerrno=%d", failed_path, errno);
        app.ui.stage = PTC_LAB_NRO_CONFLICT;
    } else {
        refresh_status(&app);
    }
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (running && appletMainLoop()) {
        u64 down;
        u64 held;
        HidTouchScreenState touch;
        bool touch_active;
        padUpdate(&pad);
        down = padGetButtonsDown(&pad);
        held = padGetButtons(&pad);
        touch_active = hidGetTouchScreenStates(&touch, 1) && touch.count > 0;
        update_waiting(&app);
        if (app.ui.modal == PTC_LAB_NRO_MODAL_WORKING) {
            if (down & HidNpadButton_B) {
                app.waiting_operation = OPERATION_NONE;
                app.ui.modal = PTC_LAB_NRO_MODAL_NONE;
                set_feedback(&app, false, "已停止等待。恢复请求可能仍在后台处理，请稍后重新打开本页检查。",
                    "result=wait_cancelled\nrequest_remains_persisted=true");
            }
        } else if (app.ui.modal == PTC_LAB_NRO_MODAL_REPORT) {
            if (down & (HidNpadButton_A | HidNpadButton_B)) app.ui.modal = PTC_LAB_NRO_MODAL_NONE;
            if (touch_active && !app.touch_down) app.ui.modal = PTC_LAB_NRO_MODAL_NONE;
        } else if (app.ui.modal == PTC_LAB_NRO_MODAL_CONFIRM) {
            u64 combo = HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_A;
            if (down & HidNpadButton_B) {
                app.ui.modal = PTC_LAB_NRO_MODAL_NONE;
                app.confirm_started = 0;
                app.ui.confirm_progress = 0;
            } else if ((held & combo) == combo) {
                uint64_t elapsed;
                if (app.confirm_started == 0) app.confirm_started = armGetSystemTick();
                elapsed = armTicksToNs(armGetSystemTick() - app.confirm_started);
                app.ui.confirm_progress = elapsed >= 1000000000ULL ? 100 : (int)(elapsed / 10000000ULL);
                if (elapsed >= 1000000000ULL) {
                    Operation operation = app.confirm_operation;
                    app.ui.modal = PTC_LAB_NRO_MODAL_NONE;
                    app.confirm_started = 0;
                    app.ui.confirm_progress = 0;
                    if (operation == OPERATION_ENABLE) complete_flag_operation(&app, operation);
                    else begin_restore(&app, operation);
                }
            } else {
                app.confirm_started = 0;
                app.ui.confirm_progress = 0;
            }
        } else {
            if (down & HidNpadButton_B) running = false;
            if (down & HidNpadButton_Y) app.ui.details_visible = !app.ui.details_visible;
            if (down & (HidNpadButton_Up | HidNpadButton_StickLUp | HidNpadButton_StickRUp))
                app.ui.selected = app.ui.selected == 0 ? 2 : app.ui.selected - 1;
            if (down & (HidNpadButton_Down | HidNpadButton_StickLDown | HidNpadButton_StickRDown))
                app.ui.selected = (app.ui.selected + 1) % 3;
            if (down & HidNpadButton_A) activate_selected(&app, &running);
            if (touch_active && !app.touch_down) {
                int hit = ptc_lab_nro_hit_test((int)touch.touches[0].x, (int)touch.touches[0].y);
                if (hit >= 0) {
                    app.ui.selected = hit;
                    activate_selected(&app, &running);
                }
            }
        }
        app.touch_down = touch_active;
        ptc_lab_nro_graphics_draw(&app.ui);
        svcSleepThread(16000000LL);
    }
    ptc_lab_nro_graphics_exit();
    return 0;
}
