#include <switch.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../companion/file_protocol.h"
#include "../../platform/switch/fs_storage.h"

#define APP_ROOT "sdmc:/switch/play-time-control"
#define RESULT_TEXT_SIZE 4096
#define REQUEST_TIMEOUT_MS 60000
#define LOOP_SLEEP_NS 100000000LL
#define LOOP_SLEEP_MS 100

typedef struct {
    PtcCompanionFileClient client;
    char active_request_id[PTC_COMPANION_REQUEST_ID_SIZE];
    char last_result[RESULT_TEXT_SIZE];
    char message[160];
    int elapsed_ms;
    bool waiting;
} UiState;

static int64_t unix_ms_now(void)
{
    return (int64_t)time(NULL) * 1000;
}

static void make_next_request_id(char *out, size_t out_size)
{
    static uint16_t counter = 0;
    uint16_t random16 = (uint16_t)((rand() ^ counter++) & 0xffff);
    if (ptc_companion_make_request_id(out, out_size, unix_ms_now() + (counter % 1000), random16) != PTC_COMPANION_OK) {
        snprintf(out, out_size, "0-%04x", random16);
    }
}

static void set_message(UiState *ui, const char *prefix, PtcCompanionStatus status)
{
    snprintf(ui->message, sizeof(ui->message), "%s: %s", prefix, ptc_companion_status_name(status));
}

static void submit_status(UiState *ui)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_submit_status(&ui->client, ui->active_request_id, time(NULL));
    if (status == PTC_COMPANION_OK) {
        ui->waiting = true;
        ui->elapsed_ms = 0;
        ui->last_result[0] = '\0';
        snprintf(ui->message, sizeof(ui->message), "Status request submitted.");
        return;
    }
    ui->waiting = false;
    set_message(ui, "Status submit failed", status);
}

static void submit_offline_code(UiState *ui)
{
    char code[80];
    SwkbdConfig keyboard;
    Result rc;
    PtcCompanionStatus status;

    code[0] = '\0';
    rc = swkbdCreate(&keyboard, 0);
    if (R_FAILED(rc)) {
        snprintf(ui->message, sizeof(ui->message), "Keyboard open failed: 0x%x", rc);
        return;
    }
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, "Offline code");
    swkbdConfigSetGuideText(&keyboard, "XXXXX-XXXXX-XXXXX-XXXXX");
    rc = swkbdShow(&keyboard, code, sizeof(code));
    swkbdClose(&keyboard);

    if (R_FAILED(rc) || code[0] == '\0') {
        snprintf(ui->message, sizeof(ui->message), "Offline code cancelled.");
        return;
    }

    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_submit_offline_code(&ui->client, ui->active_request_id, time(NULL), code);
    if (status == PTC_COMPANION_OK) {
        ui->waiting = true;
        ui->elapsed_ms = 0;
        ui->last_result[0] = '\0';
        snprintf(ui->message, sizeof(ui->message), "Offline code request submitted.");
        return;
    }
    ui->waiting = false;
    set_message(ui, "Offline code submit failed", status);
}

static void poll_result(UiState *ui, bool force)
{
    PtcCompanionStatus status;
    if (!ui->waiting && !force) {
        return;
    }
    if (ui->active_request_id[0] == '\0') {
        return;
    }
    if (ui->waiting) {
        ui->elapsed_ms += LOOP_SLEEP_MS;
    }
    status = ptc_companion_read_result(
        &ui->client,
        ui->active_request_id,
        ui->elapsed_ms,
        REQUEST_TIMEOUT_MS,
        ui->last_result,
        sizeof(ui->last_result));
    if (status == PTC_COMPANION_PENDING) {
        snprintf(ui->message, sizeof(ui->message), "Waiting for backend result...");
        return;
    }
    ui->waiting = false;
    set_message(ui, "Result", status);
}

static void draw(const UiState *ui)
{
    consoleClear();
    printf("Play Time Control\n");
    printf("=================\n\n");
    printf("Child main interface\n");
    printf("App root: %s\n\n", APP_ROOT);
    printf("A  Submit status request\n");
    printf("X  Enter offline code\n");
    printf("Y  Poll result now\n");
    printf("B/+ Exit\n\n");
    printf("Current request: %s\n", ui->active_request_id[0] ? ui->active_request_id : "(none)");
    printf("State: %s\n", ui->waiting ? "waiting" : "idle");
    printf("Message: %s\n\n", ui->message);
    if (ui->last_result[0] != '\0') {
        printf("Last result:\n%s\n", ui->last_result);
    }
    consoleUpdate(NULL);
}

int main(int argc, char **argv)
{
    PtcFsStorage fs;
    UiState ui;
    PadState pad;
    bool running = true;
    (void)argc;
    (void)argv;

    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    srand((unsigned int)time(NULL));

    memset(&ui, 0, sizeof(ui));
    snprintf(ui.message, sizeof(ui.message), "Ready.");
    ptc_fs_storage_init(&fs);
    ptc_companion_file_client_init(&ui.client, APP_ROOT, ptc_fs_storage_as_storage(&fs));

    while (appletMainLoop() && running) {
        u64 down;
        padUpdate(&pad);
        down = padGetButtonsDown(&pad);
        if (down & (HidNpadButton_Plus | HidNpadButton_B)) {
            running = false;
        } else if (down & HidNpadButton_A) {
            submit_status(&ui);
        } else if (down & HidNpadButton_X) {
            submit_offline_code(&ui);
        } else if (down & HidNpadButton_Y) {
            poll_result(&ui, true);
        }
        poll_result(&ui, false);
        draw(&ui);
        svcSleepThread(LOOP_SLEEP_NS);
    }

    consoleExit(NULL);
    return 0;
}
