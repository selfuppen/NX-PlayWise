#include "hot_reload.h"
#include "../ui_language.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../../common/crypto/sha256.h"
#include "../../common/protocol/ipc_protocol.h"
#include "../../common/version.h"
#include "../../third_party/cjson/cJSON.h"
#include "release_manifest.h"

#define BOOT_FLAG_PATH "sdmc:/atmosphere/contents/4200000000BD2300/flags/boot2.flag"
#define BOOT_FLAG_BACKUP_PATH BOOT_FLAG_PATH ".playwise-hot-reload-backup"
#define JOURNAL_PATH PLAYWISE_RELEASE_SD_ROOT "/handover/reload.json"
#define JOURNAL_TEMP_PATH JOURNAL_PATH ".tmp"
#define INTENT_PATH PLAYWISE_RELEASE_SD_ROOT "/handover/intent.json"
#define READY_PATH PLAYWISE_RELEASE_SD_ROOT "/handover/ready.json"
#define RUNTIME_READY_PATH PLAYWISE_RELEASE_SD_ROOT "/handover/runtime-ready.json"
#define RECOVERY_PATH PLAYWISE_RELEASE_SD_ROOT "/recovery/active/meta.json"
#define ARTIFACTS_PATH PLAYWISE_RELEASE_SD_ROOT "/package-artifacts.json"
#define HANDOFF_TIMEOUT_NS UINT64_C(10000000000)
#define EXIT_TIMEOUT_NS UINT64_C(10000000000)
#define READY_TIMEOUT_NS UINT64_C(30000000000)
#define RELEASE_PROGRAM_ID UINT64_C(0x4200000000BD2300)

static const PtcHotReloadPaths PATHS = {
    BOOT_FLAG_PATH,
    BOOT_FLAG_BACKUP_PATH,
    JOURNAL_PATH,
};

typedef struct {
    const char *package_name;
    const char *sd_path;
} ArtifactPath;

static const ArtifactPath ARTIFACT_PATHS[] = {
    {"switch/playwise/pctc.nro", PLAYWISE_RELEASE_SD_ROOT "/pctc.nro"},
    {"switch/.overlays/playwise.ovl", "sdmc:/switch/.overlays/playwise.ovl"},
    {"atmosphere/contents/4200000000BD2300/exefs.nsp",
     "sdmc:/atmosphere/contents/4200000000BD2300/exefs.nsp"},
};

static bool exists(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0;
}

static bool empty_file(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0 && info.st_size == 0;
}

static bool read_text(const char *path, char *out, size_t out_size)
{
    FILE *file;
    size_t got;
    if (!path || !out || out_size < 2U || !(file = fopen(path, "rb"))) return false;
    got = fread(out, 1, out_size - 1U, file);
    if (fclose(file) != 0 || got == 0 || got >= out_size - 1U) return false;
    out[got] = '\0';
    return true;
}

static bool write_atomic(const char *path, const char *text)
{
    char temporary[384];
    FILE *file;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) <= 0 || exists(temporary)) return false;
    file = fopen(temporary, "wb");
    if (!file || fwrite(text, 1, strlen(text), file) != strlen(text)) {
        if (file) (void)fclose(file);
        (void)remove(temporary);
        return false;
    }
    if (fclose(file) != 0) { (void)remove(temporary); return false; }
    (void)fsdevCommitDevice("sdmc");
    if (exists(path) && remove(path) != 0) return false;
    (void)fsdevCommitDevice("sdmc");
    if (rename(temporary, path) != 0) return false;
    (void)fsdevCommitDevice("sdmc");
    return true;
}

static bool hash_file(const char *path, uint64_t *size_out, char hex[65])
{
    uint8_t buffer[16384];
    uint8_t digest[PTC_SHA256_DIGEST_SIZE];
    PtcSha256Ctx sha;
    FILE *file = fopen(path, "rb");
    uint64_t total = 0;
    size_t got;
    unsigned int index;
    if (!file) return false;
    ptc_sha256_init(&sha);
    while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        ptc_sha256_update(&sha, buffer, got);
        total += got;
    }
    if (ferror(file)) {
        (void)fclose(file);
        return false;
    }
    if (fclose(file) != 0) return false;
    ptc_sha256_final(&sha, digest);
    for (index = 0; index < sizeof(digest); ++index) snprintf(hex + index * 2U, 3, "%02x", digest[index]);
    hex[64] = '\0';
    *size_out = total;
    return true;
}

static bool verify_artifacts(char *detail, size_t detail_size)
{
    char text[8192];
    cJSON *root;
    const cJSON *release;
    const cJSON *artifacts;
    size_t index;
    if (!read_text(ARTIFACTS_PATH, text, sizeof(text)) || !(root = cJSON_Parse(text))) {
        snprintf(detail, detail_size, ptc_ui_text(PTC_UI_T_PACKAGE_ARTIFACTS_JSON_IS_MISSING_OR_CANNOT));
        return false;
    }
    release = cJSON_GetObjectItemCaseSensitive(root, "release_id");
    artifacts = cJSON_GetObjectItemCaseSensitive(root, "artifacts");
    if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "schema_version")) ||
        cJSON_GetObjectItemCaseSensitive(root, "schema_version")->valueint != 1 ||
        !cJSON_IsString(release) || strcmp(release->valuestring, PLAYWISE_BUILD_RELEASE_ID) != 0 ||
        !cJSON_IsObject(artifacts) || cJSON_GetArraySize(artifacts) != 3) {
        cJSON_Delete(root);
        snprintf(detail, detail_size, ptc_ui_text(PTC_UI_T_THE_IDENTITY_OF_THE_INSTALLATION_PACKAGE_IS));
        return false;
    }
    for (index = 0; index < sizeof(ARTIFACT_PATHS) / sizeof(ARTIFACT_PATHS[0]); ++index) {
        const cJSON *entry = cJSON_GetObjectItemCaseSensitive(artifacts, ARTIFACT_PATHS[index].package_name);
        const cJSON *expected_size = entry ? cJSON_GetObjectItemCaseSensitive(entry, "size") : NULL;
        const cJSON *expected_hash = entry ? cJSON_GetObjectItemCaseSensitive(entry, "sha256") : NULL;
        uint64_t actual_size;
        char actual_hash[65];
        if (!cJSON_IsObject(entry) || !cJSON_IsNumber(expected_size) || !cJSON_IsString(expected_hash) ||
            expected_size->valuedouble < 0 || expected_size->valuedouble > 9007199254740991.0 ||
            (double)(uint64_t)expected_size->valuedouble != expected_size->valuedouble ||
            !hash_file(ARTIFACT_PATHS[index].sd_path, &actual_size, actual_hash) ||
            actual_size != (uint64_t)expected_size->valuedouble || strcmp(actual_hash, expected_hash->valuestring) != 0) {
            snprintf(detail, detail_size, ptc_ui_text(PTC_UI_T_COMPONENT_VERIFICATION_FAILED_S), ARTIFACT_PATHS[index].package_name);
            cJSON_Delete(root);
            return false;
        }
    }
    cJSON_Delete(root);
    return true;
}

static bool current_identity(u64 pid, PtcHotReloadIdentity *identity)
{
    char text[1280];
    return read_text(RUNTIME_READY_PATH, text, sizeof(text)) &&
        ptc_hot_reload_parse_identity(text, identity) && identity->pid == pid &&
        strcmp(identity->profile, "release") == 0 &&
        identity->ipc_version == PTC_IPC_INTERFACE_VERSION;
}

static void close_pm(PtcHotReloadController *controller)
{
    if (controller->pm_initialized) pmshellExit();
    controller->pm_initialized = false;
}

static void clear_handoff(void)
{
    (void)remove(INTENT_PATH);
    (void)remove(READY_PATH);
    (void)fsdevCommitDevice("sdmc");
}

static void fail(PtcHotReloadController *controller, const char *detail)
{
    bool restore_failed = false;
    bool cleanup_failed = false;
    if (controller->boot_disabled) {
        PtcHotReloadJournal recovered;
        if (ptc_hot_reload_read_journal(&PATHS, &recovered) &&
            ptc_hot_reload_restore_boot(&PATHS, &recovered) == PTC_HOT_RELOAD_FLAG_OK) {
            controller->boot_disabled = false;
            controller->journal = recovered;
        } else {
            restore_failed = true;
        }
    }
    clear_handoff();
    if (!restore_failed && !ptc_hot_reload_finish_journal(&PATHS)) cleanup_failed = true;
    close_pm(controller);
    controller->phase = PTC_HOT_RELOAD_PHASE_FAILED;
    controller->status = (restore_failed || cleanup_failed)
        ? PTC_HOT_RELOAD_RECOVERY_REQUIRED : PTC_HOT_RELOAD_UNAVAILABLE;
    snprintf(controller->detail, sizeof(controller->detail), "%s",
        restore_failed ? ptc_ui_text(PTC_UI_T_LAUNCH_FLAG_RECOVERY_FAILED_DO_NOT_CLOSE) :
        (cleanup_failed ? ptc_ui_text(PTC_UI_T_THE_CLEANUP_AFTER_LOADING_THE_NEW_VERSION) : detail));
}

static bool ack_ready(const PtcHotReloadController *controller)
{
    char text[1024];
    cJSON *root;
    const cJSON *transaction;
    const cJSON *status;
    const cJSON *handover_version;
    bool valid;
    if (!controller || !read_text(READY_PATH, text, sizeof(text)) || !(root = cJSON_Parse(text))) return false;
    transaction = cJSON_GetObjectItemCaseSensitive(root, "transaction_id");
    status = cJSON_GetObjectItemCaseSensitive(root, "status");
    handover_version = cJSON_GetObjectItemCaseSensitive(root, "handover_version");
    valid = cJSON_IsString(transaction) && cJSON_IsString(status) &&
        strcmp(transaction->valuestring, controller->journal.transaction_id) == 0 &&
        strcmp(status->valuestring, "ready") == 0;
    if (valid && handover_version) {
        const cJSON *source_pid = cJSON_GetObjectItemCaseSensitive(root, "source_pid");
        const cJSON *profile = cJSON_GetObjectItemCaseSensitive(root, "profile");
        const cJSON *release = cJSON_GetObjectItemCaseSensitive(root, "release_id");
        valid = cJSON_IsNumber(handover_version) && handover_version->valueint == 1 &&
            cJSON_IsNumber(source_pid) && (uint64_t)source_pid->valuedouble == controller->journal.source_pid &&
            cJSON_IsString(profile) && strcmp(profile->valuestring, "release") == 0 &&
            cJSON_IsString(release) &&
            strcmp(release->valuestring, controller->journal.source_release_id) == 0;
    }
    cJSON_Delete(root);
    return valid;
}

static bool process_absent(void)
{
    u64 pid;
    Result rc = pmshellGetProcessId(&pid, RELEASE_PROGRAM_ID);
    return R_FAILED(rc) && R_MODULE(rc) == 15 && R_DESCRIPTION(rc) == 1;
}

static bool launch_target(PtcHotReloadController *controller)
{
    NcmProgramLocation location;
    Result rc;
    memset(&location, 0, sizeof(location));
    location.program_id = RELEASE_PROGRAM_ID;
    location.storageID = NcmStorageId_None;
    (void)remove(RUNTIME_READY_PATH);
    (void)fsdevCommitDevice("sdmc");
    ++controller->launch_attempts;
    rc = pmshellLaunchProgram(PmLaunchFlag_None, &location, &controller->journal.target_pid);
    if (R_FAILED(rc)) return false;
    snprintf(controller->journal.phase, sizeof(controller->journal.phase), "target_launched");
    if (!ptc_hot_reload_write_journal(&PATHS, &controller->journal)) return false;
    controller->deadline_tick = armGetSystemTick();
    controller->phase = PTC_HOT_RELOAD_PHASE_WAIT_TARGET_READY;
    return true;
}

void ptc_hot_reload_init(PtcHotReloadController *controller)
{
    if (!controller) return;
    memset(controller, 0, sizeof(*controller));
    controller->status = PTC_HOT_RELOAD_UNKNOWN;
}

void ptc_hot_reload_recover_startup(PtcHotReloadController *controller)
{
    PtcHotReloadJournal journal;
    u64 pid;
    Result rc;
    if (!controller) return;
    if (!exists(JOURNAL_PATH) && !exists(JOURNAL_TEMP_PATH) && !exists(BOOT_FLAG_BACKUP_PATH)) return;
    if (!ptc_hot_reload_read_journal(&PATHS, &journal) ||
        ptc_hot_reload_restore_boot(&PATHS, &journal) != PTC_HOT_RELOAD_FLAG_OK) {
        controller->status = PTC_HOT_RELOAD_RECOVERY_REQUIRED;
        controller->phase = PTC_HOT_RELOAD_PHASE_FAILED;
        snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_THERE_IS_A_CONFLICT_IN_LOADING_THE));
        return;
    }
    clear_handoff();
    rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        controller->status = PTC_HOT_RELOAD_UNAVAILABLE;
        controller->phase = PTC_HOT_RELOAD_PHASE_FAILED;
        snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_STARTUP_FLAG_RESTORED_BUT_PM_SHELL_IS));
        return;
    }
    rc = pmshellGetProcessId(&pid, RELEASE_PROGRAM_ID);
    if (R_FAILED(rc) && R_MODULE(rc) == 15 && R_DESCRIPTION(rc) == 1) {
        NcmProgramLocation location;
        memset(&location, 0, sizeof(location));
        location.program_id = RELEASE_PROGRAM_ID;
        location.storageID = NcmStorageId_None;
        rc = pmshellLaunchProgram(PmLaunchFlag_None, &location, &pid);
    }
    pmshellExit();
    if (R_SUCCEEDED(rc)) {
        (void)ptc_hot_reload_finish_journal(&PATHS);
    } else {
        controller->status = PTC_HOT_RELOAD_UNAVAILABLE;
        controller->phase = PTC_HOT_RELOAD_PHASE_FAILED;
        snprintf(controller->detail, sizeof(controller->detail),
            ptc_ui_text(PTC_UI_T_THE_STARTUP_FLAG_HAS_BEEN_RESTORED_BUT));
    }
}

void ptc_hot_reload_inspect(PtcHotReloadController *controller)
{
    PtcHotReloadIdentity identity;
    u64 pid;
    Result rc;
    if (!controller || controller->phase > PTC_HOT_RELOAD_PHASE_IDLE) return;
    controller->status = PTC_HOT_RELOAD_UNKNOWN;
    controller->detail[0] = '\0';
    rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        controller->status = PTC_HOT_RELOAD_UNAVAILABLE;
        snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_PM_SHELL_IS_NOT_AVAILABLE_YOU_NEED));
        return;
    }
    rc = pmshellGetProcessId(&pid, RELEASE_PROGRAM_ID);
    if (R_FAILED(rc) || !current_identity(pid, &identity)) {
        pmshellExit();
        controller->status = PTC_HOT_RELOAD_UNAVAILABLE;
        snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_UNABLE_TO_RELIABLY_CONFIRM_THE_CURRENT_BACKGROUND));
        return;
    }
    pmshellExit();
    controller->journal.source_pid = pid;
    snprintf(controller->journal.source_release_id, sizeof(controller->journal.source_release_id), "%s", identity.release_id);
    snprintf(controller->journal.target_release_id, sizeof(controller->journal.target_release_id), "%s", PLAYWISE_BUILD_RELEASE_ID);
    snprintf(controller->source_boot_id, sizeof(controller->source_boot_id), "%s", identity.boot_id);
    if (exists(JOURNAL_PATH) || exists(JOURNAL_TEMP_PATH) || exists(BOOT_FLAG_BACKUP_PATH) ||
        exists(INTENT_PATH) || exists(READY_PATH)) {
        controller->status = PTC_HOT_RELOAD_RECOVERY_REQUIRED;
        snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_CONFLICTING_UPGRADE_RECORDS_DETECTED_EXISTING_FILES_UNCHANGED));
    } else if (!empty_file(BOOT_FLAG_PATH)) {
        controller->status = PTC_HOT_RELOAD_UNAVAILABLE;
        snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_THE_STANDARD_BOOT2_FLAG_IS_MISSING_OR));
    } else if (strcmp(identity.release_id, PLAYWISE_BUILD_RELEASE_ID) == 0) {
        controller->status = PTC_HOT_RELOAD_CURRENT;
        snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_THE_CURRENT_INSTALLED_VERSION_HAS_BEEN_LOADED));
    } else if (!verify_artifacts(controller->detail, sizeof(controller->detail))) {
        controller->status = PTC_HOT_RELOAD_INCOMPLETE;
    } else {
        controller->status = PTC_HOT_RELOAD_PENDING;
        snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_BACKEND_REMAINS_S), identity.release_id);
    }
}

bool ptc_hot_reload_begin(PtcHotReloadController *controller)
{
    char intent[512];
    PtcHotReloadIdentity identity;
    u64 pid;
    Result rc;
    if (!controller || controller->status != PTC_HOT_RELOAD_PENDING) return false;
    (void)mkdir(PLAYWISE_RELEASE_SD_ROOT "/handover", 0777);
    if (exists(RECOVERY_PATH)) {
        snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_THE_BACKGROUND_RECOVERY_HAS_NOT_BEEN_COMPLETED));
        return false;
    }
    if (exists(JOURNAL_PATH) || exists(JOURNAL_TEMP_PATH) || exists(BOOT_FLAG_BACKUP_PATH) ||
        exists(INTENT_PATH) || exists(READY_PATH)) {
        controller->status = PTC_HOT_RELOAD_RECOVERY_REQUIRED;
        snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_CONFLICTING_UPGRADE_RECORDS_DETECTED_EXISTING_FILES_UNCHANGED));
        return false;
    }
    if (!empty_file(BOOT_FLAG_PATH)) {
        controller->status = PTC_HOT_RELOAD_UNAVAILABLE;
        snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_THE_STANDARD_BOOT2_FLAG_IS_MISSING_OR_2));
        return false;
    }
    if (!verify_artifacts(controller->detail, sizeof(controller->detail))) {
        controller->status = PTC_HOT_RELOAD_INCOMPLETE;
        return false;
    }
    rc = pmshellInitialize();
    if (R_FAILED(rc)) { fail(controller, ptc_ui_text(PTC_UI_T_PM_SHELL_IS_UNAVAILABLE_THE_OLD_BACKGROUND)); return false; }
    controller->pm_initialized = true;
    rc = pmshellGetProcessId(&pid, RELEASE_PROGRAM_ID);
    if (R_FAILED(rc) || pid != controller->journal.source_pid || !current_identity(pid, &identity) ||
        strcmp(identity.release_id, controller->journal.source_release_id) != 0) {
        fail(controller, ptc_ui_text(PTC_UI_T_THE_BACKGROUND_HAS_CHANGED_AND_THE_NEW));
        return false;
    }
    snprintf(controller->journal.transaction_id, sizeof(controller->journal.transaction_id), "%016llx%016llx",
        (unsigned long long)randomGet64(), (unsigned long long)randomGet64());
    snprintf(controller->journal.phase, sizeof(controller->journal.phase), "prepared");
    if (!ptc_hot_reload_write_journal(&PATHS, &controller->journal)) {
        fail(controller, ptc_ui_text(PTC_UI_T_UNABLE_TO_SAVE_THE_RECORD_OF_LOADING));
        return false;
    }
    snprintf(intent, sizeof(intent),
        "{\"version\":1,\"transaction_id\":\"%s\",\"action\":\"quiesce\",\"created_at\":%lld}\n",
        controller->journal.transaction_id, (long long)time(NULL));
    if (!write_atomic(INTENT_PATH, intent)) {
        fail(controller, ptc_ui_text(PTC_UI_T_UNABLE_TO_WRITE_BACKGROUND_HANDOVER_REQUEST_OLD));
        return false;
    }
    controller->deadline_tick = armGetSystemTick();
    controller->phase = PTC_HOT_RELOAD_PHASE_WAIT_ACK;
    controller->status = PTC_HOT_RELOAD_RUNNING;
    snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_1_4_WAITING_FOR_THE_OLD_BACKEND));
    return true;
}

void ptc_hot_reload_confirm_ipc_closed(PtcHotReloadController *controller)
{
    if (!controller || controller->phase != PTC_HOT_RELOAD_PHASE_NEED_IPC_CLOSE) return;
    if (ptc_hot_reload_disable_boot(&PATHS, &controller->journal) != PTC_HOT_RELOAD_FLAG_OK) {
        fail(controller, ptc_ui_text(PTC_UI_T_UNABLE_TO_SAFELY_STAGE_STARTUP_FLAGS_OLD));
        return;
    }
    controller->boot_disabled = true;
    controller->deadline_tick = armGetSystemTick();
    controller->phase = PTC_HOT_RELOAD_PHASE_WAIT_SOURCE_EXIT;
    snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_2_4_THE_OLD_BACKGROUND_IS_CLEANING));
}

void ptc_hot_reload_tick(PtcHotReloadController *controller)
{
    uint64_t elapsed;
    if (!controller) return;
    if (controller->phase == PTC_HOT_RELOAD_PHASE_WAIT_ACK) {
        if (ack_ready(controller)) {
            controller->phase = PTC_HOT_RELOAD_PHASE_NEED_IPC_CLOSE;
            snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_2_4_THE_BACKGROUND_HAS_BEEN_SILENT));
            return;
        }
        elapsed = armTicksToNs(armGetSystemTick() - controller->deadline_tick);
        if (elapsed >= HANDOFF_TIMEOUT_NS) fail(controller, ptc_ui_text(PTC_UI_T_THE_OLD_VERSION_CANNOT_DIRECTLY_LOAD_THE));
        return;
    }
    if (controller->phase == PTC_HOT_RELOAD_PHASE_WAIT_SOURCE_EXIT) {
        if (process_absent()) ++controller->absent_samples;
        else controller->absent_samples = 0;
        if (controller->absent_samples >= 3U) {
            if (ptc_hot_reload_restore_boot(&PATHS, &controller->journal) != PTC_HOT_RELOAD_FLAG_OK) {
                fail(controller, ptc_ui_text(PTC_UI_T_THE_OLD_BACKGROUND_HAS_EXITED_BUT_THE));
                return;
            }
            controller->boot_disabled = false;
            clear_handoff();
            if (!launch_target(controller) && process_absent() && controller->launch_attempts < 2U) {
                (void)launch_target(controller);
            }
            if (controller->phase != PTC_HOT_RELOAD_PHASE_WAIT_TARGET_READY)
                fail(controller, ptc_ui_text(PTC_UI_T_THE_NEW_VERSION_OF_THE_BACKGROUND_FAILED));
            else snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_3_4_THE_NEW_VERSION_OF_THE));
            return;
        }
        elapsed = armTicksToNs(armGetSystemTick() - controller->deadline_tick);
        if (elapsed >= EXIT_TIMEOUT_NS) fail(controller, ptc_ui_text(PTC_UI_T_UNABLE_TO_PROVE_THAT_THE_OLD_BACKGROUND));
        return;
    }
    if (controller->phase == PTC_HOT_RELOAD_PHASE_WAIT_TARGET_READY) {
        PtcHotReloadIdentity identity;
        u64 actual_pid;
        if (R_SUCCEEDED(pmshellGetProcessId(&actual_pid, RELEASE_PROGRAM_ID)) &&
            actual_pid == controller->journal.target_pid && current_identity(actual_pid, &identity) &&
            ptc_hot_reload_identity_matches(&identity, actual_pid, "release", PLAYWISE_BUILD_RELEASE_ID) &&
            strcmp(identity.boot_id, controller->source_boot_id) != 0) {
            if (!ptc_hot_reload_finish_journal(&PATHS)) {
                fail(controller, ptc_ui_text(PTC_UI_T_THE_NEW_VERSION_HAS_BEEN_STARTED_BUT));
                return;
            }
            close_pm(controller);
            controller->journal.source_pid = actual_pid;
            snprintf(controller->journal.source_release_id,
                sizeof(controller->journal.source_release_id), "%s", identity.release_id);
            snprintf(controller->source_boot_id, sizeof(controller->source_boot_id), "%s", identity.boot_id);
            controller->phase = PTC_HOT_RELOAD_PHASE_COMPLETE;
            controller->status = PTC_HOT_RELOAD_SUCCESS;
            snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_4_4_THE_NEW_VERSION_OF_THE));
            return;
        }
        elapsed = armTicksToNs(armGetSystemTick() - controller->deadline_tick);
        if (elapsed >= READY_TIMEOUT_NS) {
            if (process_absent() && controller->launch_attempts < 2U && launch_target(controller)) {
                snprintf(controller->detail, sizeof(controller->detail), ptc_ui_text(PTC_UI_T_3_4_THE_NEW_VERSION_OF_THE_2));
            } else {
                fail(controller, ptc_ui_text(PTC_UI_T_THE_NEW_VERSION_OF_BACKGROUND_IDENTITY_VERIFICATION));
            }
        }
    }
}

void ptc_hot_reload_exit(PtcHotReloadController *controller)
{
    bool restored;
    if (!controller) return;
    restored = !controller->boot_disabled;
    if (controller->boot_disabled) {
        PtcHotReloadJournal journal;
        if (ptc_hot_reload_read_journal(&PATHS, &journal) &&
            ptc_hot_reload_restore_boot(&PATHS, &journal) == PTC_HOT_RELOAD_FLAG_OK) {
            controller->boot_disabled = false;
            restored = true;
        }
    }
    if (controller->phase == PTC_HOT_RELOAD_PHASE_WAIT_ACK ||
        controller->phase == PTC_HOT_RELOAD_PHASE_NEED_IPC_CLOSE ||
        controller->phase == PTC_HOT_RELOAD_PHASE_WAIT_SOURCE_EXIT) {
        clear_handoff();
        if (restored && controller->phase != PTC_HOT_RELOAD_PHASE_WAIT_SOURCE_EXIT)
            (void)ptc_hot_reload_finish_journal(&PATHS);
    }
    close_pm(controller);
}
