#include "input_model.h"

#include <stdio.h>
#include <string.h>

static const char CHARSET[] = "0123456789";
static const unsigned int DIRECTION_BUTTONS =
    PTC_OVERLAY_BUTTON_UP |
    PTC_OVERLAY_BUTTON_DOWN |
    PTC_OVERLAY_BUTTON_LEFT |
    PTC_OVERLAY_BUTTON_RIGHT;

const char *ptc_overlay_input_charset(void)
{
    return CHARSET;
}

bool ptc_overlay_request_action_enabled(bool waiting)
{
    return !waiting;
}

void ptc_overlay_input_init(PtcOverlayInput *input)
{
    if (!input) {
        return;
    }
    memset(input, 0, sizeof(*input));
    input->cursor = 1;
}

static void move_cursor(PtcOverlayInput *input, int dx, int dy)
{
    static const int GRID[PTC_OVERLAY_KEY_ROWS][PTC_OVERLAY_KEY_COLUMNS] = {
        {1, 2, 3},
        {4, 5, 6},
        {7, 8, 9},
        {-1, 0, -1},
    };
    int row = input->cursor == 0 ? 3 : ((int)input->cursor - 1) / PTC_OVERLAY_KEY_COLUMNS;
    int col = input->cursor == 0 ? 1 : ((int)input->cursor - 1) % PTC_OVERLAY_KEY_COLUMNS;
    int attempts;
    for (attempts = 0; attempts < PTC_OVERLAY_KEY_ROWS * PTC_OVERLAY_KEY_COLUMNS; ++attempts) {
        col = (col + dx + PTC_OVERLAY_KEY_COLUMNS) % PTC_OVERLAY_KEY_COLUMNS;
        row = (row + dy + PTC_OVERLAY_KEY_ROWS) % PTC_OVERLAY_KEY_ROWS;
        if (GRID[row][col] >= 0) {
            input->cursor = (unsigned int)GRID[row][col];
            return;
        }
    }
}

static bool is_single_direction(unsigned int buttons)
{
    return buttons != 0u && (buttons & (buttons - 1u)) == 0u;
}

static void reset_repeat(PtcOverlayInput *input)
{
    input->repeat_direction = 0u;
    input->repeat_elapsed_ms = 0;
    input->repeat_started = false;
}

static void move_in_direction(PtcOverlayInput *input, unsigned int direction)
{
    switch (direction) {
    case PTC_OVERLAY_BUTTON_UP:
        move_cursor(input, 0, -1);
        break;
    case PTC_OVERLAY_BUTTON_DOWN:
        move_cursor(input, 0, 1);
        break;
    case PTC_OVERLAY_BUTTON_LEFT:
        move_cursor(input, -1, 0);
        break;
    case PTC_OVERLAY_BUTTON_RIGHT:
        move_cursor(input, 1, 0);
        break;
    default:
        break;
    }
}

static bool handle_direction_repeat(
    PtcOverlayInput *input,
    unsigned int buttons_down,
    unsigned int buttons_held,
    int elapsed_ms)
{
    unsigned int down = buttons_down & DIRECTION_BUTTONS;
    unsigned int held = buttons_held & DIRECTION_BUTTONS;

    if (down == 0u && held == 0u) {
        reset_repeat(input);
        return false;
    }

    /* Ambiguous diagonals/opposing directions are consumed without moving. */
    if ((down != 0u && !is_single_direction(down)) ||
        (held != 0u && !is_single_direction(held)) ||
        (down != 0u && held != 0u && down != held)) {
        reset_repeat(input);
        return true;
    }

    if (down != 0u) {
        move_in_direction(input, down);
        input->repeat_direction = down;
        input->repeat_elapsed_ms = 0;
        input->repeat_started = false;
        return true;
    }

    if (input->repeat_direction != held) {
        input->repeat_direction = held;
        input->repeat_elapsed_ms = 0;
        input->repeat_started = false;
        return true;
    }

    if (elapsed_ms > 0) {
        input->repeat_elapsed_ms += elapsed_ms;
    }
    if (!input->repeat_started) {
        if (input->repeat_elapsed_ms < PTC_OVERLAY_REPEAT_DELAY_MS) {
            return true;
        }
        input->repeat_elapsed_ms -= PTC_OVERLAY_REPEAT_DELAY_MS;
        input->repeat_started = true;
        move_in_direction(input, held);
    }
    while (input->repeat_elapsed_ms >= PTC_OVERLAY_REPEAT_INTERVAL_MS) {
        input->repeat_elapsed_ms -= PTC_OVERLAY_REPEAT_INTERVAL_MS;
        move_in_direction(input, held);
    }
    return true;
}

bool ptc_overlay_input_handle(
    PtcOverlayInput *input,
    unsigned int buttons_down,
    unsigned int buttons_held,
    int elapsed_ms)
{
    if (!input || input->timed_out) {
        return false;
    }
    if (handle_direction_repeat(input, buttons_down, buttons_held, elapsed_ms)) {
        return true;
    }
    if (buttons_down & PTC_OVERLAY_BUTTON_X) {
        if (input->length > 0) {
            --input->length;
            input->symbols[input->length] = '\0';
        }
        return true;
    }
    if (buttons_down & PTC_OVERLAY_BUTTON_Y) {
        input->length = 0;
        input->symbols[0] = '\0';
        return true;
    }
    if (buttons_down & PTC_OVERLAY_BUTTON_A) {
        if (input->length < PTC_OVERLAY_CODE_SYMBOLS) {
            input->symbols[input->length++] = CHARSET[input->cursor];
            input->symbols[input->length] = '\0';
        }
        return true;
    }
    return (buttons_down & (PTC_OVERLAY_BUTTON_B | PTC_OVERLAY_BUTTON_PLUS)) != 0;
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
