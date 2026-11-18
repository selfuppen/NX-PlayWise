#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "companion/overlay/display_clock.h"
#include "companion/overlay/hidden_poll.h"
#include "companion/overlay/hidden_timing.h"
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
    PtcOverlayHiddenTiming timing;
    ptc_overlay_hidden_timing_init(&timing);
    assert(timing.clock_order_valid && !timing.ready_write_ok);
    ptc_overlay_hidden_timing_poll(&timing, 1000, true);
    ptc_overlay_hidden_timing_ready_begin(&timing, 1000, 2500, 1040);
    ptc_overlay_hidden_timing_ready_end(&timing, 1080, true);
    assert(timing.ready_issued_ms == 1000 && timing.ready_write_started_ms == 1040);
    assert(timing.ready_write_completed_ms == 1080 && timing.ready_write_ok);
    ptc_overlay_hidden_timing_read_begin(&timing, &timing.config_read, 1100);
    ptc_overlay_hidden_timing_read_end(&timing, &timing.config_read, 1963, true);
    ptc_overlay_hidden_timing_read_begin(&timing, &timing.runtime_read, 1970);
    ptc_overlay_hidden_timing_read_end(&timing, &timing.runtime_read, 1980, true);
    ptc_overlay_hidden_timing_read_begin(&timing, &timing.request_read, 1980);
    ptc_overlay_hidden_timing_read_end(&timing, &timing.request_read, 1988, false);
    assert(timing.config_read.max_elapsed_ms == 863 && timing.runtime_read.max_elapsed_ms == 10);
    assert(timing.request_read.attempted && !timing.request_read.read_ok && timing.request_read.max_elapsed_ms == 8);
    ptc_overlay_hidden_timing_poll(&timing, 2800, false);
    assert(timing.previous_poll_started_ms == 1000 && timing.max_poll_gap_ms == 1800);
    assert(!timing.neutral && timing.non_neutral_polls == 1);
    assert(!timing.config_read.attempted && timing.config_read.started_ms == 0 && timing.config_read.completed_ms == 0);
    assert(timing.config_read.max_elapsed_ms == 863 && timing.request_read.max_elapsed_ms == 8);
    ptc_overlay_hidden_timing_ready_begin(&timing, 2800, 4300, 2810);
    ptc_overlay_hidden_timing_ready_end(&timing, 4500, false);
    assert(timing.previous_ready_issued_ms == 1000 && timing.previous_ready_write_completed_ms == 1080);
    assert(timing.previous_ready_write_ok && !timing.ready_write_ok);
    assert(timing.clock_order_valid); /* completion after expiry is retained as measured */
    char hidden_timing_json[1536];
    assert(ptc_overlay_hidden_timing_format(&timing, hidden_timing_json, sizeof(hidden_timing_json)));
    assert(strstr(hidden_timing_json, "\"ready_write_completed_ms\":\"4500\""));
    assert(strstr(hidden_timing_json, "\"ready_write_ok\":false"));
    assert(strstr(hidden_timing_json, "\"previous_ready_write_ok\":true"));
    assert(strstr(hidden_timing_json, "\"reads\":{\"config\":{\"started_ms\":\"0\""));
    assert(strstr(hidden_timing_json, "\"max_elapsed_ms\":\"863\",\"attempted\":false,\"read_ok\":false"));
    assert(!ptc_overlay_hidden_timing_format(&timing, hidden_timing_json, 8));
    assert(!ptc_overlay_hidden_timing_format(&timing, hidden_timing_json, 0));
    ptc_overlay_hidden_timing_poll(&timing, 2700, true);
    assert(!timing.clock_order_valid && timing.max_poll_gap_ms == 1800);
    ptc_overlay_hidden_timing_init(&timing);
    ptc_overlay_hidden_timing_ready_begin(&timing, 100, 99, 90);
    ptc_overlay_hidden_timing_ready_end(&timing, 89, true);
    assert(!timing.clock_order_valid);
    ptc_overlay_hidden_timing_init(&timing);
    ptc_overlay_hidden_timing_poll(&timing, 100, true);
    ptc_overlay_hidden_timing_read_begin(&timing, &timing.request_read, 110);
    ptc_overlay_hidden_timing_read_end(&timing, &timing.request_read, 120, true);
    ptc_overlay_hidden_timing_poll(&timing, 115, true);
    assert(!timing.clock_order_valid); /* reset between the previous read end and next poll */
    ptc_overlay_hidden_timing_init(&timing);
    ptc_overlay_hidden_timing_read_end(&timing, &timing.request_read, 1, false);
    assert(!timing.clock_order_valid && timing.request_read.max_elapsed_ms == 0);
    ptc_overlay_hidden_timing_init(&timing);
    ptc_overlay_hidden_timing_poll(&timing, 100, true);
    ptc_overlay_hidden_timing_read_begin(&timing, &timing.config_read, 99);
    ptc_overlay_hidden_timing_read_end(&timing, &timing.config_read, 98, false);
    assert(!timing.clock_order_valid && timing.config_read.max_elapsed_ms == 0);
    ptc_overlay_hidden_timing_init(&timing);
    ptc_overlay_hidden_timing_poll(&timing, 1, true);
    ptc_overlay_hidden_timing_poll(&timing, UINT64_MAX, true);
    timing.non_neutral_polls = UINT64_MAX;
    ptc_overlay_hidden_timing_poll(&timing, UINT64_MAX, false);
    assert(timing.non_neutral_polls == UINT64_MAX && timing.max_poll_gap_ms == UINT64_MAX - 1);
    ptc_overlay_hidden_timing_ready_begin(&timing, UINT64_MAX - 1, UINT64_MAX, UINT64_MAX - 1);
    ptc_overlay_hidden_timing_ready_end(&timing, UINT64_MAX, true);
    PtcOverlayHiddenReadTiming *read_timings[] = {&timing.config_read, &timing.runtime_read, &timing.request_read};
    for (unsigned i = 0; i < 3; ++i) {
        ptc_overlay_hidden_timing_read_begin(&timing, read_timings[i], UINT64_MAX);
        ptc_overlay_hidden_timing_read_end(&timing, read_timings[i], UINT64_MAX, true);
        read_timings[i]->max_elapsed_ms = UINT64_MAX;
    }
    ptc_overlay_hidden_timing_ready_begin(&timing, UINT64_MAX - 1, UINT64_MAX, UINT64_MAX - 1);
    ptc_overlay_hidden_timing_ready_end(&timing, UINT64_MAX, true);
    assert(ptc_overlay_hidden_timing_format(&timing, hidden_timing_json, sizeof(hidden_timing_json)));
    assert(strstr(hidden_timing_json, "18446744073709551615"));
    char read_timing_json[256];
    assert(ptc_overlay_hidden_read_timing_format(&timing.config_read, read_timing_json, sizeof(read_timing_json)));
    assert(!ptc_overlay_hidden_read_timing_format(&timing.config_read, read_timing_json, 8));
    ptc_overlay_hidden_timing_init(&timing);
    assert(timing.clock_order_valid && timing.poll_started_ms == 0 && timing.previous_ready_issued_ms == 0);
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
