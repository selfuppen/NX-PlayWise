#ifndef PLAYWISE_DEVICE_LAB_NRO_UI_GRAPHICS_H
#define PLAYWISE_DEVICE_LAB_NRO_UI_GRAPHICS_H

#include <stdbool.h>

#include "../ui_model.h"

typedef enum {
    PTC_LAB_NRO_MODAL_NONE = 0,
    PTC_LAB_NRO_MODAL_CONFIRM = 1,
    PTC_LAB_NRO_MODAL_REPORT = 2,
    PTC_LAB_NRO_MODAL_WORKING = 3
} PtcLabNroModal;

typedef struct {
    PtcLabNroStage stage;
    PtcLabSessionLoadStatus session_status;
    PtcLabSessionView session;
    int selected;
    bool report_available;
    bool draft_available;
    bool details_visible;
    bool message_is_error;
    PtcLabNroModal modal;
    int confirm_progress;
    char message[320];
    char technical[384];
    char report_path[320];
} PtcLabNroUi;

bool ptc_lab_nro_graphics_init(void);
void ptc_lab_nro_graphics_exit(void);
void ptc_lab_nro_graphics_draw(const PtcLabNroUi *ui);

#endif
