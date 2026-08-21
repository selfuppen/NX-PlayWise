#include <switch.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../../common/version.h"
#include "../boot_flags.h"
#include "release_manifest.h"

__attribute__((used)) static const char PLAYWISE_EMBEDDED_MANIFEST[] = PLAYWISE_RELEASE_MANIFEST_JSON;

static const PtcLabBootFlagPaths FLAG_PATHS = {
    "sdmc:/atmosphere/contents/4200000000BD2300/flags/boot2.flag",
    "sdmc:/atmosphere/contents/4200000000BD2300/flags/boot2.flag.playwise-device-lab-backup",
    "sdmc:/atmosphere/contents/4200000000BD23F0/flags/boot2.flag",
    PLAYWISE_DEVICE_LAB_SD_ROOT "/lab/boot-switch.json"
};

static const char *const ACTIONS[] = {
    "Enable Lab",
    "Restore normal package",
    "View latest report"
};

static void ensure_directories(void)
{
    static const char *const PATHS[] = {
        PLAYWISE_DEVICE_LAB_SD_ROOT,
        PLAYWISE_DEVICE_LAB_SD_ROOT "/lab",
        PLAYWISE_DEVICE_LAB_SD_ROOT "/reports"
    };
    size_t i;
    for (i = 0; i < sizeof(PATHS) / sizeof(PATHS[0]); ++i) {
        if (mkdir(PATHS[i], 0777) != 0 && errno != EEXIST) break;
    }
}

static void latest_report(char *message, size_t size)
{
    DIR *directory = opendir(PLAYWISE_DEVICE_LAB_SD_ROOT "/reports");
    struct dirent *entry;
    char latest[192] = "";
    if (!directory) {
        snprintf(message, size, "No reports directory. Enable and run the Lab first.");
        return;
    }
    while ((entry = readdir(directory)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 5U && strcmp(entry->d_name + len - 5U, ".json") == 0 &&
            strcmp(entry->d_name, latest) > 0) snprintf(latest, sizeof(latest), "%s", entry->d_name);
    }
    closedir(directory);
    if (latest[0]) snprintf(message, size, "Send this file:\n%s/reports/%s", PLAYWISE_DEVICE_LAB_SD_ROOT, latest);
    else snprintf(message, size, "No report yet. Open the Device Lab Overlay after reboot.");
}

static bool session_restore_proved(void)
{
    char text[2048];
    FILE *file = fopen(PLAYWISE_DEVICE_LAB_SD_ROOT "/lab/session.json", "rb");
    size_t got;
    if (!file) return true;
    got = fread(text, 1, sizeof(text) - 1U, file);
    (void)fclose(file);
    text[got] = '\0';
    return strstr(text, "\"restored\":true") != NULL &&
        strstr(text, "\"restore_verdict\":\"exact_restore_proved\"") != NULL;
}

static bool request_session_restore(char *message, size_t size)
{
    char request_id[80];
    char temporary[320];
    char pending[320];
    FILE *file;
    unsigned int retry;
    if (session_restore_proved()) return true;
    snprintf(request_id, sizeof(request_id), "lab-restore-%016llx", (unsigned long long)randomGet64());
    snprintf(temporary, sizeof(temporary), PLAYWISE_DEVICE_LAB_SD_ROOT "/inbox/pending/%s.tmp", request_id);
    snprintf(pending, sizeof(pending), PLAYWISE_DEVICE_LAB_SD_ROOT "/inbox/pending/%s.json", request_id);
    file = fopen(temporary, "wb");
    if (!file || fprintf(file,
            "{\"version\":1,\"request_id\":\"%s\",\"type\":\"lab_session_restore\","
            "\"created_at\":%lld,\"payload\":{}}\n", request_id, (long long)time(NULL)) < 0) {
        if (file) (void)fclose(file);
        (void)remove(temporary);
        snprintf(message, size, "Could not request PCTL restore. Boot flags were not changed.");
        return false;
    }
    if (fclose(file) != 0 || rename(temporary, pending) != 0) {
        (void)remove(temporary);
        snprintf(message, size, "Could not queue PCTL restore. Boot flags were not changed.");
        return false;
    }
    for (retry = 0; retry < 40U; ++retry) {
        svcSleepThread(250000000LL);
        if (session_restore_proved()) return true;
    }
    snprintf(message, size, "PCTL restore not proved. Open the Lab Overlay and choose immediate restore.");
    return false;
}

static void draw(size_t selected, const char *message)
{
    size_t i;
    consoleClear();
    printf("\x1b[31;1mPLAYWISE DEVICE LAB - INTERNAL / DANGEROUS\x1b[0m\n\n");
    for (i = 0; i < sizeof(ACTIONS) / sizeof(ACTIONS[0]); ++i) {
        printf("%s %s\n", selected == i ? ">" : " ", ACTIONS[i]);
    }
    printf("\nA: run selected action   B: exit\n");
    printf("Only boot flags change; standard data and binaries stay untouched.\n");
    if (message && message[0]) printf("\n%s\n", message);
    consoleUpdate(NULL);
}

int main(int argc, char **argv)
{
    PadState pad;
    size_t selected = 0;
    char message[320] = "Choose Enable Lab before the first reboot.";
    (void)argc;
    (void)argv;
    { const char *volatile anchor = PLAYWISE_EMBEDDED_MANIFEST; (void)anchor; }
    consoleInit(NULL);
    fsdevMountSdmc();
    ensure_directories();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    draw(selected, message);
    while (appletMainLoop()) {
        u64 down;
        padUpdate(&pad);
        down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_B) break;
        if (down & HidNpadButton_Up) selected = selected == 0 ? 2 : selected - 1;
        if (down & HidNpadButton_Down) selected = (selected + 1) % 3;
        if (down & HidNpadButton_A) {
            if (selected == 0) (void)ptc_lab_boot_flags_enable(&FLAG_PATHS, message, sizeof(message));
            else if (selected == 1) {
                if (request_session_restore(message, sizeof(message)))
                    (void)ptc_lab_boot_flags_restore(&FLAG_PATHS, message, sizeof(message));
            }
            else latest_report(message, sizeof(message));
        }
        draw(selected, message);
    }
    fsdevUnmountDevice("sdmc");
    consoleExit(NULL);
    return 0;
}
