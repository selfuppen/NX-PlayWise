#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Display-only throttle. Authentication must keep reading its own live clock. */
typedef struct {
    uint64_t last_ns;
    bool sampled;
    char text[16];
} PtcOverlayDisplayClock;

static inline bool ptc_overlay_display_clock_due(PtcOverlayDisplayClock *clock, uint64_t now_ns)
{
    if (clock->sampled && now_ns >= clock->last_ns && now_ns - clock->last_ns < UINT64_C(1000000000))
        return false;
    clock->sampled = true;
    clock->last_ns = now_ns;
    return true;
}
