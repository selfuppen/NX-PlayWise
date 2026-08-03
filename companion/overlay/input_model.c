#include "input_model.h"

#include <stdio.h>
#include <string.h>

static const char CHARSET[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

const char *ptc_overlay_input_charset(void)
{
    return CHARSET;
}

void ptc_overlay_input_init(PtcOverlayInput *input)
{
    if (!input) {
        return;
    }
    memset(input, 0, sizeof(*input));
}

static void move_cursor(PtcOverlayInput *input, int dx, int dy)
{
    int row = (int)(input->cursor / 8u);
    int col = (int)(input->cursor % 8u);
    col = (col + dx + 8) % 8;
    row = (row + dy + 4) % 4;
    input->cursor = (unsigned int)(row * 8 + col);
}

bool ptc_overlay_input_handle(PtcOverlayInput *input, unsigned int buttons)
{
    if (!input || input->timed_out) {
        return false;
    }
    if (buttons & PTC_OVERLAY_BUTTON_UP) {
        move_cursor(input, 0, -1);
        return true;
    }
    if (buttons & PTC_OVERLAY_BUTTON_DOWN) {
        move_cursor(input, 0, 1);
        return true;
    }
    if (buttons & PTC_OVERLAY_BUTTON_LEFT) {
        move_cursor(input, -1, 0);
        return true;
    }
    if (buttons & PTC_OVERLAY_BUTTON_RIGHT) {
        move_cursor(input, 1, 0);
        return true;
    }
    if (buttons & PTC_OVERLAY_BUTTON_X) {
        if (input->length > 0) {
            --input->length;
            input->symbols[input->length] = '\0';
        }
        return true;
    }
    if (buttons & PTC_OVERLAY_BUTTON_Y) {
        input->length = 0;
        input->symbols[0] = '\0';
        return true;
    }
    if (buttons & PTC_OVERLAY_BUTTON_A) {
        if (input->length < PTC_OVERLAY_CODE_SYMBOLS) {
            input->symbols[input->length++] = CHARSET[input->cursor];
            input->symbols[input->length] = '\0';
        }
        return true;
    }
    return (buttons & (PTC_OVERLAY_BUTTON_B | PTC_OVERLAY_BUTTON_PLUS)) != 0;
}

void ptc_overlay_input_tick(PtcOverlayInput *input, int elapsed_ms, int timeout_ms)
{
    if (!input || input->timed_out || elapsed_ms <= 0 || timeout_ms < 0) {
        return;
    }
    input->elapsed_ms += elapsed_ms;
    if (input->elapsed_ms >= timeout_ms) {
        input->timed_out = true;
    }
}

bool ptc_overlay_input_format(const PtcOverlayInput *input, char *out, size_t out_size)
{
    unsigned int i;
    unsigned int used = 0;
    if (!input || !out || out_size == 0) {
        return false;
    }
    for (i = 0; i < input->length; ++i) {
        if (i > 0 && i % 4u == 0) {
            if (used + 1u >= out_size) return false;
            out[used++] = '-';
        }
        if (used + 1u >= out_size) return false;
        out[used++] = input->symbols[i];
    }
    if (used >= out_size) return false;
    out[used] = '\0';
    return true;
}

bool ptc_overlay_input_can_submit(const PtcOverlayInput *input)
{
    return input && !input->timed_out && input->length == PTC_OVERLAY_CODE_SYMBOLS;
}
