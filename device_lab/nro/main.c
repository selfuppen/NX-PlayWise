#include <switch.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../../common/version.h"
#include "release_manifest.h"

__attribute__((used)) static const char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

typedef struct {
    const char *label;
    const char *request_type;
    const char *payload;
} LabAction;

static const LabAction ACTIONS[] = {
    {"Raw block probe", "probe_raw_block", "{}"},
    {"Suspend probe", "probe_suspend", "{}"},
    {"Play-timer write probe", "probe_play_timer_write", "{}"},
    {"Apply 1-minute target", "probe_apply_today_limit", "{\"minutes\":1,\"start_timer\":true}"},
    {"Play-timer effect probe", "probe_play_timer_effect", "{\"wait_for_expiry\":false}"},
    {"Prepare device test", "prepare_device_test", "{}"},
};

static void ensure_directories(void)
{
    static const char *PATHS[] = {
        "sdmc:/switch/playwise-device-lab",
        "sdmc:/switch/playwise-device-lab/inbox",
        "sdmc:/switch/playwise-device-lab/inbox/pending",
        "sdmc:/switch/playwise-device-lab/results",
    };
    size_t i;
    for (i = 0; i < sizeof(PATHS) / sizeof(PATHS[0]); ++i) {
        if (mkdir(PATHS[i], 0777) != 0 && errno != EEXIST) break;
    }
}

static bool submit_action(const LabAction *action, char *request_id, size_t request_id_size)
{
    char temporary[256];
    char pending[256];
    FILE *file;
    long long created_at = (long long)time(NULL);
    snprintf(request_id, request_id_size, "lab-%016llx", (unsigned long long)randomGet64());
    snprintf(temporary, sizeof(temporary), PLAYWISE_DEVICE_LAB_SD_ROOT "/inbox/pending/%s.tmp", request_id);
    snprintf(pending, sizeof(pending), PLAYWISE_DEVICE_LAB_SD_ROOT "/inbox/pending/%s.json", request_id);
    file = fopen(temporary, "wb");
    if (!file) return false;
    if (fprintf(file, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"created_at\":%lld,\"payload\":%s}\n",
            request_id, action->request_type, created_at, action->payload) < 0) {
        (void)fclose(file);
        (void)remove(temporary);
        return false;
    }
    if (fclose(file) != 0) {
        (void)remove(temporary);
        return false;
    }
    if (rename(temporary, pending) != 0) {
        (void)remove(temporary);
        return false;
    }
    return true;
}

static void draw_menu(size_t selected, const char *message)
{
    size_t i;
    consoleClear();
    printf("\x1b[31;1mPLAYWISE DEVICE LAB - DANGEROUS / INTERNAL ONLY\x1b[0m\n");
    printf("Title: %s  IPC: %s\n", PLAYWISE_DEVICE_LAB_TITLE_ID, PLAYWISE_DEVICE_LAB_IPC_SERVICE);
    printf("SD root: %s\n\n", PLAYWISE_DEVICE_LAB_SD_ROOT);
    for (i = 0; i < sizeof(ACTIONS) / sizeof(ACTIONS[0]); ++i) {
        printf("%s %s\n", selected == i ? ">" : " ", ACTIONS[i].label);
    }
    printf("\nHold ZL+ZR and press A to submit. B exits.\n");
    printf("Every probe may write private PCTL state and must restore it.\n");
    if (message && message[0]) printf("\n%s\n", message);
    consoleUpdate(NULL);
}

int main(int argc, char **argv)
{
    PadState pad;
    size_t selected = 0;
    char message[192] = "No request submitted.";
    (void)argc;
    (void)argv;
    {
        const char *volatile manifest_anchor = PLAYWISE_EMBEDDED_MANIFEST;
        (void)manifest_anchor;
    }
    consoleInit(NULL);
    fsdevMountSdmc();
    ensure_directories();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    draw_menu(selected, message);
    while (appletMainLoop()) {
        u64 down;
        u64 held;
        padUpdate(&pad);
        down = padGetButtonsDown(&pad);
        held = padGetButtons(&pad);
        if (down & HidNpadButton_B) break;
        if (down & HidNpadButton_Up) {
            selected = selected == 0 ? (sizeof(ACTIONS) / sizeof(ACTIONS[0])) - 1 : selected - 1;
            message[0] = '\0';
        }
        if (down & HidNpadButton_Down) {
            selected = (selected + 1) % (sizeof(ACTIONS) / sizeof(ACTIONS[0]));
            message[0] = '\0';
        }
        if (down & HidNpadButton_A) {
            if ((held & HidNpadButton_ZL) && (held & HidNpadButton_ZR)) {
                char request_id[80];
                if (submit_action(&ACTIONS[selected], request_id, sizeof(request_id))) {
                    snprintf(message, sizeof(message), "Submitted %s. Result: results/%s.json", ACTIONS[selected].request_type, request_id);
                } else {
                    snprintf(message, sizeof(message), "Submit failed. Check the Device Lab SD root.");
                }
            } else {
                snprintf(message, sizeof(message), "Blocked: hold both ZL and ZR while pressing A.");
            }
        }
        draw_menu(selected, message);
    }
    fsdevUnmountDevice("sdmc");
    consoleExit(NULL);
    return 0;
}
