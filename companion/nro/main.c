#include <switch.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../companion/auth.h"
#include "../../companion/file_protocol.h"
#include "../../companion/self_check.h"
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
    UI_VIEW_PARENT = 1
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
    bool waiting;
    bool parent_unlocked;
    bool child_x_pending;
    bool self_check_after_result;
    bool quick_device_test;
    PtcSelfCheckProfile self_check_after_profile;
    UiView view;
} UiState;

typedef PtcCompanionStatus (*SubmitNoArgFn)(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);

static void run_self_check_profile(UiState *ui, PtcSelfCheckProfile profile, bool prompt_request_id, const char *prefix_text);
static PtcCompanionStatus submit_effect_fast(PtcCompanionFileClient *client, const char *request_id, int64_t created_at);

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
    ui->self_check_after_result = false;
    ui->quick_device_test = false;
    snprintf(ui->message, sizeof(ui->message), "%s", message);
}

static void arm_self_check_after_result(UiState *ui, PtcSelfCheckProfile profile)
{
    ui->self_check_after_result = true;
    ui->self_check_after_profile = profile;
}

static void append_text_truncated(char *out, size_t out_size, const char *text)
{
    size_t used;
    size_t available;
    size_t text_len;
    size_t copy_len;
    if (!out || out_size == 0 || !text) {
        return;
    }
    used = strlen(out);
    if (used >= out_size - 1) {
        out[out_size - 1] = '\0';
        return;
    }
    available = out_size - used - 1;
    text_len = strlen(text);
    copy_len = text_len < available ? text_len : available;
    memcpy(out + used, text, copy_len);
    out[used + copy_len] = '\0';
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
        summary[0] = '\0';
        if (ptc_companion_format_result_summary(ui->last_result, summary, sizeof(summary)) == PTC_COMPANION_OK) {
            snprintf(ui->last_result, sizeof(ui->last_result), "%s", summary);
        } else {
            snprintf(summary, sizeof(summary), "%s", ui->last_result);
        }
        if (ui->self_check_after_result) {
            ui->self_check_after_result = false;
            run_self_check_profile(ui, ui->self_check_after_profile, false, summary);
            return;
        }
    }
    ui->self_check_after_result = false;
    if (ui->quick_device_test) {
        PtcCompanionStatus disable_status = ptc_companion_set_disable_flag(&ui->client, true);
        ui->quick_device_test = false;
        snprintf(
            ui->message,
            sizeof(ui->message),
            "Quick device test FAIL; control disabled: %s",
            ptc_companion_status_name(disable_status));
        return;
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

static void run_self_check_profile(UiState *ui, PtcSelfCheckProfile profile, bool prompt_request_id, const char *prefix_text)
{
    char request_id[PTC_COMPANION_REQUEST_ID_SIZE];
    PtcSelfCheckResult result;
    char report[RESULT_TEXT_SIZE];
    char *out = prefix_text ? report : ui->last_result;
    size_t out_size = prefix_text ? sizeof(report) : sizeof(ui->last_result);

    snprintf(request_id, sizeof(request_id), "%s", ui->active_request_id);
    if (prompt_request_id && profile != PTC_SELF_CHECK_ENFORCE_SNAPSHOT) {
        if (!keyboard_text("Self-check request", "request_id", request_id, sizeof(request_id))) {
            snprintf(ui->message, sizeof(ui->message), "Self-check cancelled.");
            return;
        }
        snprintf(ui->active_request_id, sizeof(ui->active_request_id), "%s", request_id);
    }

    if (profile != PTC_SELF_CHECK_ENFORCE_SNAPSHOT && request_id[0] == '\0') {
        snprintf(ui->message, sizeof(ui->message), "Self-check needs a request id.");
        return;
    }

    result = ptc_self_check_run(
        ui->client.storage,
        APP_ROOT,
        profile == PTC_SELF_CHECK_ENFORCE_SNAPSHOT ? "" : request_id,
        profile,
        NULL,
        out,
        out_size);
    if (prefix_text) {
        snprintf(ui->last_result, sizeof(ui->last_result), "%s", prefix_text);
        append_text_truncated(ui->last_result, sizeof(ui->last_result), "\nSelf-check report\n");
        append_text_truncated(ui->last_result, sizeof(ui->last_result), report);
    }
    if (ui->quick_device_test) {
        ui->quick_device_test = false;
        if (result.status == PTC_SELF_CHECK_PASS) {
            snprintf(ui->message, sizeof(ui->message), "Quick device test PASS");
        } else {
            PtcCompanionStatus disable_status = ptc_companion_set_disable_flag(&ui->client, true);
            snprintf(
                ui->message,
                sizeof(ui->message),
                "Quick device test FAIL; control disabled: %s",
                ptc_companion_status_name(disable_status));
        }
        return;
    }
    snprintf(
        ui->message,
        sizeof(ui->message),
        "Self-check %s: %s",
        ptc_self_check_profile_name(profile),
        ptc_self_check_status_name(result.status));
}

static void handle_parent_action(UiState *ui)
{
    PtcCompanionStatus status;
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
        if (!confirm_yes("Quick device test")) {
            snprintf(ui->message, sizeof(ui->message), "Quick device test cancelled.");
            break;
        }
        submit_noarg(ui, submit_effect_fast, "Quick device test running...", "Quick device test submit failed");
        if (ui->waiting) {
            ui->quick_device_test = true;
            arm_self_check_after_result(ui, PTC_SELF_CHECK_PLAY_TIMER_EFFECT_PROBE);
        } else {
            status = ptc_companion_set_disable_flag(&ui->client, true);
            snprintf(
                ui->message,
                sizeof(ui->message),
                "Quick device test FAIL; control disabled: %s",
                ptc_companion_status_name(status));
        }
        break;
    case 12:
        if (!confirm_yes("Emergency disable")) {
            snprintf(ui->message, sizeof(ui->message), "Emergency disable cancelled.");
            break;
        }
        status = ptc_companion_set_disable_flag(&ui->client, true);
        set_message(ui, "Emergency disable", status);
        break;
    case 13:
        if (!confirm_yes("Resume control")) {
            snprintf(ui->message, sizeof(ui->message), "Resume control cancelled.");
            break;
        }
        status = ptc_companion_set_disable_flag(&ui->client, false);
        set_message(ui, "Resume control", status);
        break;
    default:
        ui->parent_index = 0;
        break;
    }
}

static PtcCompanionStatus submit_effect_fast(PtcCompanionFileClient *client, const char *request_id, int64_t created_at)
{
    return ptc_companion_submit_probe_play_timer_effect(client, request_id, created_at, false);
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
        "Quick device test",
        "Emergency disable",
        "Resume control",
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


static void draw(const UiState *ui)
{
    consoleClear();
    if (ui->view == UI_VIEW_PARENT) {
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
                ui.parent_index = ui.parent_index <= 0 ? 13 : ui.parent_index - 1;
            } else if (down & HidNpadButton_Down) {
                ui.parent_index = ui.parent_index >= 13 ? 0 : ui.parent_index + 1;
            } else if (down & HidNpadButton_A) {
                handle_parent_action(&ui);
            }
        }

        poll_result(&ui, false);
        draw(&ui);
        svcSleepThread(LOOP_SLEEP_NS);
    }

    consoleExit(NULL);
    return 0;
}
