#ifndef PLAYWISE_DEVICE_LAB_UI_MODEL_H
#define PLAYWISE_DEVICE_LAB_UI_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_flags.h"

typedef enum {
    PTC_LAB_SESSION_MISSING = 0,
    PTC_LAB_SESSION_VALID = 1,
    PTC_LAB_SESSION_INVALID = 2
} PtcLabSessionLoadStatus;

typedef struct {
    char run_id[48];
    char mode[32];
    char state[32];
    char active_phase[32];
    char restore_verdict[32];
    char last_verdict[48];
    int next_phase;
    int required_phases;
    int64_t deadline;
    bool restored;
    bool baseline_all_zero;
} PtcLabSessionView;

typedef enum {
    PTC_LAB_NRO_PREPARE = 0,
    PTC_LAB_NRO_REBOOT_TO_LAB = 1,
    PTC_LAB_NRO_START_OVERLAY = 2,
    PTC_LAB_NRO_RESUME_OVERLAY = 3,
    PTC_LAB_NRO_RESTORE_PCTL = 4,
    PTC_LAB_NRO_RESTORE_NORMAL = 5,
    PTC_LAB_NRO_RECOVER_FLAGS = 6,
    PTC_LAB_NRO_CONFLICT = 7,
    PTC_LAB_NRO_SESSION_INVALID = 8,
    PTC_LAB_NRO_REBOOT_TO_NORMAL = 9
} PtcLabNroStage;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} PtcLabUiRect;

bool ptc_lab_session_parse(const char *text, PtcLabSessionView *view);
bool ptc_lab_json_string(const char *text, const char *key, char *out, size_t out_size);
bool ptc_lab_result_error(const char *text, int *error_code, char *reason, size_t reason_size);
PtcLabNroStage ptc_lab_nro_stage(const PtcLabBootStatus *boot, bool lab_service_running,
    PtcLabSessionLoadStatus session_status, const PtcLabSessionView *session);

const char *ptc_lab_nro_stage_title_zh(PtcLabNroStage stage);
const char *ptc_lab_nro_stage_body_zh(PtcLabNroStage stage);
const char *ptc_lab_nro_stage_action_zh(PtcLabNroStage stage);
const char *ptc_lab_session_state_zh(const char *state);
const char *ptc_lab_phase_title_zh(int phase);
const char *ptc_lab_phase_instruction_zh(int phase);
const char *ptc_lab_verdict_zh(const char *verdict);
const char *ptc_lab_mode_zh(const char *mode);
const char *ptc_lab_transport_error_zh(int status);

PtcLabUiRect ptc_lab_nro_action_rect(int index);
bool ptc_lab_ui_rect_contains(PtcLabUiRect rect, int x, int y);
int ptc_lab_nro_hit_test(int x, int y);

#endif
