#include <switch.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../companion/auth.h"
#include "../../companion/file_protocol.h"
#include "../../platform/switch/fs_storage.h"

#define APP_ROOT "sdmc:/switch/play-time-control"
#define RESULT_TEXT_SIZE 4096
#define REQUEST_TIMEOUT_MS 60000
#define LOOP_SLEEP_NS 100000000LL
#define LOOP_SLEEP_MS 100
#define HIDDEN_HOLD_TICKS 20
#define HIDDEN_LEFT_SHOULDER_MASK (HidNpadButton_L | HidNpadButton_ZL)
#define HIDDEN_RIGHT_SHOULDER_MASK (HidNpadButton_R | HidNpadButton_ZR)

typedef enum {
    UI_VIEW_CHILD = 0,
    UI_VIEW_PARENT = 1,
    UI_VIEW_DANGER = 2
} UiView;

typedef struct {
    PtcCompanionFileClient client;
    PtcCompanionAuth auth;
    char active_request_id[PTC_COMPANION_REQUEST_ID_SIZE];
    char last_result[RESULT_TEXT_SIZE];
    char message[192];
    int elapsed_ms;
    int hidden_ticks;
    int parent_index;
    int danger_index;
    bool waiting;
    bool parent_unlocked;
    bool child_x_pending;
    UiView view;
} UiState;

typedef PtcCompanionStatus (*SubmitNoArgFn)(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);

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

static void set_auth_message(UiState *ui, const char *prefix, PtcAuthStatus status)
{
    snprintf(ui->message, sizeof(ui->message), "%s: %s", prefix, ptc_auth_status_name(status));
}

static bool hidden_parent_combo_held(u64 buttons)
{
    return (buttons & HidNpadButton_X) &&
           (buttons & HIDDEN_LEFT_SHOULDER_MASK) &&
           (buttons & HIDDEN_RIGHT_SHOULDER_MASK);
}

static bool hidden_parent_combo_attempted(u64 buttons)
{
    return (buttons & HidNpadButton_X) &&
           (buttons & (HIDDEN_LEFT_SHOULDER_MASK | HIDDEN_RIGHT_SHOULDER_MASK));
}

static bool switch_random(uint8_t *out, size_t out_size, void *ctx)
{
    size_t i;
    (void)ctx;
    for (i = 0; i < out_size; ++i) {
        out[i] = (uint8_t)(rand() & 0xff);
    }
    return true;
}

static bool keyboard_text(const char *header, const char *guide, char *out, size_t out_size)
{
    SwkbdConfig keyboard;
    Result rc;
    if (!out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    rc = swkbdCreate(&keyboard, 0);
    if (R_FAILED(rc)) {
        return false;
    }
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, header);
    swkbdConfigSetGuideText(&keyboard, guide);
    rc = swkbdShow(&keyboard, out, out_size);
    swkbdClose(&keyboard);
    return R_SUCCEEDED(rc) && out[0] != '\0';
}

static bool keyboard_u16(const char *header, const char *guide, uint16_t *out)
{
    char text[32];
    char *end = NULL;
    long value;
    if (!keyboard_text(header, guide, text, sizeof(text))) {
        return false;
    }
    value = strtol(text, &end, 10);
    if (!end || *end != '\0' || value < 0 || value > 65535) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

static bool confirm_yes(const char *header)
{
    char text[16];
    return keyboard_text(header, "Type YES to confirm", text, sizeof(text)) && strcmp(text, "YES") == 0;
}

static void begin_wait(UiState *ui, const char *message)
{
    ui->waiting = true;
    ui->elapsed_ms = 0;
    ui->last_result[0] = '\0';
    snprintf(ui->message, sizeof(ui->message), "%s", message);
}

static void submit_noarg(UiState *ui, SubmitNoArgFn submit, const char *ok_message, const char *fail_prefix)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = submit(&ui->client, ui->active_request_id, time(NULL));
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, ok_message);
        return;
    }
    ui->waiting = false;
    set_message(ui, fail_prefix, status);
}

static void submit_status(UiState *ui)
{
    submit_noarg(ui, ptc_companion_submit_status, "Status request submitted.", "Status submit failed");
}

static void submit_offline_code(UiState *ui)
{
    char code[80];
    PtcCompanionStatus status;

    if (!keyboard_text("Offline code", "XXXX-XXXX-XXXX-XXXX", code, sizeof(code))) {
        snprintf(ui->message, sizeof(ui->message), "Offline code cancelled.");
        return;
    }

    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_submit_offline_code(&ui->client, ui->active_request_id, time(NULL), code);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "Offline code request submitted.");
        return;
    }
    ui->waiting = false;
    set_message(ui, "Offline code submit failed", status);
}

static void submit_minutes_request(UiState *ui, bool add)
{
    uint16_t minutes;
    PtcCompanionStatus status;
    if (!keyboard_u16(add ? "Add minutes" : "Today limit", "Minutes", &minutes)) {
        snprintf(ui->message, sizeof(ui->message), "Minutes input cancelled or invalid.");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = add
        ? ptc_companion_submit_add_today_minutes(&ui->client, ui->active_request_id, time(NULL), minutes)
        : ptc_companion_submit_set_today_limit(&ui->client, ui->active_request_id, time(NULL), minutes);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, add ? "Add-minutes request submitted." : "Today-limit request submitted.");
        return;
    }
    ui->waiting = false;
    set_message(ui, "Minutes request failed", status);
}

static void submit_unlock_start(UiState *ui)
{
    uint16_t minutes;
    PtcCompanionStatus status;
    if (!keyboard_u16("Parent unlock", "Duration minutes", &minutes) || minutes == 0) {
        snprintf(ui->message, sizeof(ui->message), "Unlock duration cancelled or invalid.");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_submit_parent_unlock_start(&ui->client, ui->active_request_id, time(NULL), minutes);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "Parent unlock request submitted.");
        return;
    }
    ui->waiting = false;
    set_message(ui, "Unlock request failed", status);
}

static void submit_bedtime(UiState *ui)
{
    PtcBedtimeRule bedtime;
    uint16_t enabled;
    PtcCompanionStatus status;
    if (!keyboard_u16("Bedtime enabled", "0 or 1", &enabled) ||
        !keyboard_u16("Bedtime start", "Minute of day 0-1439", &bedtime.start_min) ||
        !keyboard_u16("Bedtime end", "Minute of day 0-1439", &bedtime.end_min) ||
        enabled > 1 || bedtime.start_min >= 1440 || bedtime.end_min >= 1440) {
        snprintf(ui->message, sizeof(ui->message), "Bedtime input cancelled or invalid.");
        return;
    }
    bedtime.enabled = enabled != 0;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_submit_set_bedtime(&ui->client, ui->active_request_id, time(NULL), &bedtime);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "Bedtime request submitted.");
        return;
    }
    ui->waiting = false;
    set_message(ui, "Bedtime request failed", status);
}

static bool parse_rule_mode(const char *text, PtcRuleMode *mode)
{
    if (strcmp(text, "limit") == 0) {
        *mode = PTC_RULE_MODE_LIMIT;
        return true;
    }
    if (strcmp(text, "unlimited") == 0) {
        *mode = PTC_RULE_MODE_UNLIMITED;
        return true;
    }
    if (strcmp(text, "blocked") == 0) {
        *mode = PTC_RULE_MODE_BLOCKED;
        return true;
    }
    return false;
}

static void submit_weekly_template(UiState *ui)
{
    static const char *DAY_NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    PtcDayRule week[7];
    PtcCompanionStatus status;
    unsigned int i;
    for (i = 0; i < 7; ++i) {
        char mode_text[24];
        char header[48];
        snprintf(header, sizeof(header), "%s mode", DAY_NAMES[i]);
        if (!keyboard_text(header, "limit/unlimited/blocked", mode_text, sizeof(mode_text)) ||
            !parse_rule_mode(mode_text, &week[i].mode)) {
            snprintf(ui->message, sizeof(ui->message), "Weekly template cancelled or invalid.");
            return;
        }
        week[i].minutes = 0;
        if (week[i].mode == PTC_RULE_MODE_LIMIT &&
            !keyboard_u16("Limit minutes", "Minutes", &week[i].minutes)) {
            snprintf(ui->message, sizeof(ui->message), "Weekly minutes cancelled or invalid.");
            return;
        }
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_submit_set_weekly_template(&ui->client, ui->active_request_id, time(NULL), week);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "Weekly template request submitted.");
        return;
    }
    ui->waiting = false;
    set_message(ui, "Weekly request failed", status);
}

static void submit_limit_action(UiState *ui)
{
    char action_text[24];
    PtcLimitAction action;
    PtcCompanionStatus status;
    if (!keyboard_text("Limit action", "remind/raw_block/suspend", action_text, sizeof(action_text))) {
        snprintf(ui->message, sizeof(ui->message), "Limit action cancelled.");
        return;
    }
    if (strcmp(action_text, "remind") == 0) {
        action = PTC_LIMIT_ACTION_REMIND;
    } else if (strcmp(action_text, "raw_block") == 0) {
        action = PTC_LIMIT_ACTION_RAW_BLOCK;
    } else if (strcmp(action_text, "suspend") == 0) {
        action = PTC_LIMIT_ACTION_SUSPEND;
    } else {
        snprintf(ui->message, sizeof(ui->message), "Limit action invalid.");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_submit_set_limit_action(&ui->client, ui->active_request_id, time(NULL), action);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "Limit action request submitted.");
        return;
    }
    ui->waiting = false;
    set_message(ui, "Limit action failed", status);
}

static void submit_danger_request(UiState *ui, SubmitNoArgFn submit, const char *confirm, const char *ok_message)
{
    if (!confirm_yes(confirm)) {
        snprintf(ui->message, sizeof(ui->message), "Danger action cancelled.");
        return;
    }
    submit_noarg(ui, submit, ok_message, "Danger request failed");
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
    if (status == PTC_COMPANION_OK) {
        char summary[RESULT_TEXT_SIZE];
        if (ptc_companion_format_result_summary(ui->last_result, summary, sizeof(summary)) == PTC_COMPANION_OK) {
            snprintf(ui->last_result, sizeof(ui->last_result), "%s", summary);
        }
    }
    set_message(ui, "Result", status);
}

static void enter_parent_area(UiState *ui)
{
    char pin[PTC_AUTH_PIN_MAX_LEN + 1];
    char pin_confirm[PTC_AUTH_PIN_MAX_LEN + 1];
    PtcAuthStatus state = ptc_companion_auth_state(&ui->auth);
    if (state == PTC_AUTH_EMPTY) {
        if (!keyboard_text("Set parent PIN", "1-32 chars", pin, sizeof(pin)) ||
            !keyboard_text("Confirm PIN", "Repeat PIN", pin_confirm, sizeof(pin_confirm)) ||
            strcmp(pin, pin_confirm) != 0) {
            snprintf(ui->message, sizeof(ui->message), "PIN setup cancelled or mismatch.");
            return;
        }
        state = ptc_companion_auth_set_pin(&ui->auth, pin, time(NULL), switch_random, NULL);
        if (state != PTC_AUTH_OK) {
            set_auth_message(ui, "PIN setup failed", state);
            return;
        }
    } else if (state != PTC_AUTH_OK) {
        set_auth_message(ui, "Auth unavailable", state);
        return;
    }

    if (!keyboard_text("Parent PIN", "Enter PIN", pin, sizeof(pin))) {
        snprintf(ui->message, sizeof(ui->message), "Parent entry cancelled.");
        return;
    }
    state = ptc_companion_auth_verify_pin(&ui->auth, pin);
    if (state != PTC_AUTH_OK) {
        set_auth_message(ui, "PIN rejected", state);
        return;
    }
    ui->parent_unlocked = true;
    ui->view = UI_VIEW_PARENT;
    snprintf(ui->message, sizeof(ui->message), "Parent area unlocked.");
}

static void handle_parent_action(UiState *ui)
{
    switch (ui->parent_index) {
    case 0:
        submit_status(ui);
        break;
    case 1:
        submit_minutes_request(ui, false);
        break;
    case 2:
        submit_minutes_request(ui, true);
        break;
    case 3:
        submit_noarg(ui, ptc_companion_submit_disable_today_limit, "Disable today request submitted.", "Disable today failed");
        break;
    case 4:
        submit_noarg(ui, ptc_companion_submit_block_today, "Block today request submitted.", "Block today failed");
        break;
    case 5:
        submit_noarg(ui, ptc_companion_submit_restore_today_policy, "Restore policy request submitted.", "Restore policy failed");
        break;
    case 6:
        submit_weekly_template(ui);
        break;
    case 7:
        submit_bedtime(ui);
        break;
    case 8:
        submit_limit_action(ui);
        break;
    case 9:
        submit_unlock_start(ui);
        break;
    case 10:
        submit_noarg(ui, ptc_companion_submit_parent_unlock_end, "Unlock end request submitted.", "Unlock end failed");
        break;
    case 11:
        ui->view = UI_VIEW_DANGER;
        break;
    default:
        ui->parent_index = 0;
        break;
    }
}

static void handle_danger_action(UiState *ui)
{
    PtcCompanionStatus status;
    switch (ui->danger_index) {
    case 0:
        submit_danger_request(ui, ptc_companion_submit_probe_play_timer_write, "Probe play write", "Play write probe submitted.");
        break;
    case 1:
        submit_danger_request(ui, ptc_companion_submit_probe_raw_block, "Probe raw block", "Raw block probe submitted.");
        break;
    case 2:
        submit_danger_request(ui, ptc_companion_submit_probe_suspend, "Probe suspend", "Suspend probe submitted.");
        break;
    case 3:
        if (!confirm_yes("Create disable.flag")) {
            snprintf(ui->message, sizeof(ui->message), "Disable flag cancelled.");
            break;
        }
        status = ptc_companion_set_disable_flag(&ui->client, true);
        set_message(ui, "Create disable.flag", status);
        break;
    case 4:
        if (!confirm_yes("Remove disable.flag")) {
            snprintf(ui->message, sizeof(ui->message), "Disable flag removal cancelled.");
            break;
        }
        status = ptc_companion_set_disable_flag(&ui->client, false);
        set_message(ui, "Remove disable.flag", status);
        break;
    default:
        ui->danger_index = 0;
        break;
    }
}

static void draw_child(const UiState *ui)
{
    printf("Play Time Control\n");
    printf("=================\n\n");
    printf("Child main interface\n");
    printf("App root: %s\n\n", APP_ROOT);
    printf("A  Submit status request\n");
    printf("X  Enter offline code\n");
    printf("Y  Poll result now\n");
    printf("Minus  Parent area\n");
    printf("Hold L/R or ZL/ZR + X  Parent area\n");
    printf("B/+ Exit\n\n");
    printf("Current request: %s\n", ui->active_request_id[0] ? ui->active_request_id : "(none)");
    printf("State: %s\n", ui->waiting ? "waiting" : "idle");
    printf("Message: %s\n\n", ui->message);
}

static void draw_parent(const UiState *ui)
{
    static const char *ACTIONS[] = {
        "Status",
        "Set today limit",
        "Add today minutes",
        "Disable today limit",
        "Block today",
        "Restore today policy",
        "Set weekly template",
        "Set bedtime",
        "Set limit action",
        "Parent unlock start",
        "Parent unlock end",
        "Verification / recovery",
    };
    int count = (int)(sizeof(ACTIONS) / sizeof(ACTIONS[0]));
    int i;
    printf("Parent area\n");
    printf("===========\n\n");
    printf("Up/Down Select  A Submit  X Child  Y Poll  B Child\n\n");
    for (i = 0; i < count; ++i) {
        printf("%c %s\n", i == ui->parent_index ? '>' : ' ', ACTIONS[i]);
    }
    printf("\nCurrent request: %s\n", ui->active_request_id[0] ? ui->active_request_id : "(none)");
    printf("State: %s\n", ui->waiting ? "waiting" : "idle");
    printf("Message: %s\n\n", ui->message);
}

static void draw_danger(const UiState *ui)
{
    static const char *ACTIONS[] = {
        "probe_play_timer_write",
        "probe_raw_block",
        "probe_suspend",
        "create disable.flag",
        "remove disable.flag",
    };
    int count = (int)(sizeof(ACTIONS) / sizeof(ACTIONS[0]));
    int i;
    printf("Verification / recovery\n");
    printf("=======================\n\n");
    printf("A requires typing YES. These actions can affect staged true-device tests.\n");
    printf("Up/Down Select  A Run  B Back  Y Poll\n\n");
    for (i = 0; i < count; ++i) {
        printf("%c %s\n", i == ui->danger_index ? '>' : ' ', ACTIONS[i]);
    }
    printf("\nCurrent request: %s\n", ui->active_request_id[0] ? ui->active_request_id : "(none)");
    printf("State: %s\n", ui->waiting ? "waiting" : "idle");
    printf("Message: %s\n\n", ui->message);
}

static void draw(const UiState *ui)
{
    consoleClear();
    if (ui->view == UI_VIEW_DANGER) {
        draw_danger(ui);
    } else if (ui->view == UI_VIEW_PARENT) {
        draw_parent(ui);
    } else {
        draw_child(ui);
    }
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
    ptc_companion_auth_init(&ui.auth, APP_ROOT, ptc_fs_storage_as_storage(&fs));

    while (appletMainLoop() && running) {
        u64 down;
        u64 held;
        u64 up;
        bool parent_combo_held;
        padUpdate(&pad);
        down = padGetButtonsDown(&pad);
        held = padGetButtons(&pad);
        up = padGetButtonsUp(&pad);
        parent_combo_held = hidden_parent_combo_held(held);

        if (parent_combo_held) {
            ++ui.hidden_ticks;
            ui.child_x_pending = false;
            if (ui.hidden_ticks == HIDDEN_HOLD_TICKS) {
                enter_parent_area(&ui);
            }
        } else {
            ui.hidden_ticks = 0;
            if (hidden_parent_combo_attempted(held)) {
                ui.child_x_pending = false;
            }
        }

        if (down & HidNpadButton_Y) {
            poll_result(&ui, true);
        }

        if (ui.view == UI_VIEW_CHILD) {
            if (down & (HidNpadButton_Plus | HidNpadButton_B)) {
                running = false;
            } else if (down & HidNpadButton_A) {
                submit_status(&ui);
            } else if (down & HidNpadButton_Minus) {
                ui.child_x_pending = false;
                enter_parent_area(&ui);
            } else if ((down & HidNpadButton_X) && !parent_combo_held) {
                ui.child_x_pending = true;
            } else if ((up & HidNpadButton_X) && ui.child_x_pending) {
                ui.child_x_pending = false;
                submit_offline_code(&ui);
            }
        } else if (ui.view == UI_VIEW_PARENT) {
            ui.child_x_pending = false;
            if (down & (HidNpadButton_B | HidNpadButton_X)) {
                ui.view = UI_VIEW_CHILD;
            } else if (down & HidNpadButton_Up) {
                ui.parent_index = ui.parent_index <= 0 ? 11 : ui.parent_index - 1;
            } else if (down & HidNpadButton_Down) {
                ui.parent_index = ui.parent_index >= 11 ? 0 : ui.parent_index + 1;
            } else if (down & HidNpadButton_A) {
                handle_parent_action(&ui);
            }
        } else {
            ui.child_x_pending = false;
            if (down & HidNpadButton_B) {
                ui.view = UI_VIEW_PARENT;
            } else if (down & HidNpadButton_Up) {
                ui.danger_index = ui.danger_index <= 0 ? 4 : ui.danger_index - 1;
            } else if (down & HidNpadButton_Down) {
                ui.danger_index = ui.danger_index >= 4 ? 0 : ui.danger_index + 1;
            } else if (down & HidNpadButton_A) {
                handle_danger_action(&ui);
            }
        }

        poll_result(&ui, false);
        draw(&ui);
        svcSleepThread(LOOP_SLEEP_NS);
    }

    consoleExit(NULL);
    return 0;
}
