#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "companion/overlay/display_clock.h"
#include "companion/overlay/hidden_poll.h"
#include "companion/ui_language.h"
#include "sysmodule/idle_wait.h"

int main(void)
{
    assert(ptc_sysmodule_idle_wait_ms(5000, false) == 1000);
    assert(ptc_sysmodule_idle_wait_ms(5000, true) == 250);
    assert(ptc_sysmodule_idle_wait_ms(100, false) == 100);
    assert(ptc_sysmodule_idle_wait_ms(100, true) == 100);
    /* Hidden overlay UI thread: five seconds while reason notifications are off,
       250ms cooperation while on, never again after five write failures. */
    assert(ptc_overlay_hidden_poll_ns(false, 0) == UINT64_C(5000000000));
    assert(ptc_overlay_hidden_poll_ns(true, 0) == UINT64_C(250000000));
    assert(ptc_overlay_hidden_poll_ns(true, PTC_OVERLAY_HIDDEN_FAILURE_LIMIT - 1) == UINT64_C(250000000));
    assert(ptc_overlay_hidden_poll_ns(true, PTC_OVERLAY_HIDDEN_FAILURE_LIMIT) == UINT64_MAX);
    assert(ptc_overlay_hidden_poll_ns(false, PTC_OVERLAY_HIDDEN_FAILURE_LIMIT) == UINT64_MAX);
    assert(PTC_OVERLAY_HIDDEN_CONFIG_MS == 5000);
    assert(ptc_overlay_hidden_runtime_next_ms(1000, true) == 11000);
    assert(ptc_overlay_hidden_runtime_next_ms(1000, false) == 2000);
    assert(ptc_overlay_hidden_runtime_next_ms(UINT64_MAX - 5, true) == UINT64_MAX);
    PtcOverlayDisplayClock clock = {0};
    assert(ptc_overlay_display_clock_due(&clock, 0));
    for (uint64_t t = 1; t < 1000000000; t += 16666666)
        assert(!ptc_overlay_display_clock_due(&clock, t));
    assert(ptc_overlay_display_clock_due(&clock, 1000000000));
    assert(ptc_overlay_display_clock_due(&clock, 1)); /* monotonic reset */
    clock.sampled = false; /* reopening forces a new read, including after failure */
    assert(ptc_overlay_display_clock_due(&clock, 2));
#define EXPECTED(id, text) ((id) == PTC_UI_T_EDEN_SIMULATE_TV || (id) == PTC_UI_T_EDEN_SIMULATE_NON_TV ? "" : (text))
#define PTC_OVERLAY_TEXT(id, name, hans, hant, en) \
    assert(strcmp(ptc_ui_text_in_language(id, PTC_UI_LANGUAGE_SIMPLIFIED), EXPECTED(id, hans)) == 0); \
    assert(strcmp(ptc_ui_text_in_language(id, PTC_UI_LANGUAGE_TRADITIONAL), EXPECTED(id, hant)) == 0); \
    assert(strcmp(ptc_ui_text_in_language(id, PTC_UI_LANGUAGE_ENGLISH), EXPECTED(id, en)) == 0); \
    assert(strcmp(ptc_ui_text_resolve(name), EXPECTED(id, hans)) == 0);
#include "companion/overlay/ui_text_catalog.inc"
#undef PTC_OVERLAY_TEXT
    assert(strcmp(ptc_ui_text((PtcUiTextId)-1), "[missing text]") == 0);
    assert(strcmp(ptc_ui_text((PtcUiTextId)PTC_UI_TEXT_COUNT), "[missing text]") == 0);
    assert(strcmp(ptc_ui_text_resolve("PTC_UI_T_UNKNOWN_KEY"), "[missing text]") == 0);
    assert(strcmp(ptc_ui_text_resolve("literal"), "literal") == 0);
    puts("Overlay clock throttle and all compact catalog translations passed");
    return 0;
}
