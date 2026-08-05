#ifndef PTC_OVERLAY_INPUT_MODEL_H
#define PTC_OVERLAY_INPUT_MODEL_H

#include <stdbool.h>
#include <stddef.h>

#define PTC_OVERLAY_CODE_SYMBOLS 8
#define PTC_OVERLAY_KEY_COUNT 10
#define PTC_OVERLAY_KEY_COLUMNS 3
#define PTC_OVERLAY_KEY_ROWS 4
#define PTC_OVERLAY_REQUEST_TIMEOUT_MS 60000
#define PTC_OVERLAY_REPEAT_DELAY_MS 350
#define PTC_OVERLAY_REPEAT_INTERVAL_MS 90

typedef enum {
    PTC_OVERLAY_BUTTON_UP = 1u << 0,
    PTC_OVERLAY_BUTTON_DOWN = 1u << 1,
    PTC_OVERLAY_BUTTON_LEFT = 1u << 2,
    PTC_OVERLAY_BUTTON_RIGHT = 1u << 3,
    PTC_OVERLAY_BUTTON_A = 1u << 4,
    PTC_OVERLAY_BUTTON_B = 1u << 5,
    PTC_OVERLAY_BUTTON_X = 1u << 6,
    PTC_OVERLAY_BUTTON_Y = 1u << 7,
    PTC_OVERLAY_BUTTON_PLUS = 1u << 8,
    PTC_OVERLAY_BUTTON_MINUS = 1u << 9,
    PTC_OVERLAY_BUTTON_L = 1u << 10,
    PTC_OVERLAY_BUTTON_R = 1u << 11
} PtcOverlayButton;

typedef struct {
    char symbols[PTC_OVERLAY_CODE_SYMBOLS + 1];
    unsigned int length;
    unsigned int cursor;
    unsigned int repeat_direction;
    int repeat_elapsed_ms;
    int elapsed_ms;
    bool repeat_started;
    bool timed_out;
} PtcOverlayInput;

void ptc_overlay_input_init(PtcOverlayInput *input);
bool ptc_overlay_input_handle(
    PtcOverlayInput *input,
    unsigned int buttons_down,
    unsigned int buttons_held,
    int elapsed_ms);
void ptc_overlay_input_tick(PtcOverlayInput *input, int elapsed_ms, int timeout_ms);
bool ptc_overlay_input_format(const PtcOverlayInput *input, char *out, size_t out_size);
bool ptc_overlay_input_can_submit(const PtcOverlayInput *input);
const char *ptc_overlay_input_charset(void);

#endif
