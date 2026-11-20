#include <assert.h>
#include <stdio.h>
#include "../../companion/nro/loop_clock.h"

int main(void)
{
    int64_t previous = 0;
    int elapsed = 0, polls = 0;
    /* Twenty 5ms iterations must be one 100ms poll, not four polls. */
    for (int64_t now = 5; now <= 100; now += 5) {
        elapsed = ptc_loop_add_ms(elapsed, ptc_loop_elapsed_ms(&previous, now));
        if (elapsed >= 100) { ++polls; elapsed = 0; }
    }
    assert(polls == 1);
    assert(ptc_loop_elapsed_ms(&previous, 810) == 710);
    assert(ptc_loop_elapsed_ms(&previous, 810) == 0);
    assert(ptc_loop_elapsed_ms(&previous, 700) == 0);
    assert(ptc_loop_elapsed_ms(&previous, 720) == 20);
    assert(ptc_loop_elapsed_ms(&previous, INT64_MAX) == INT_MAX);
    assert(ptc_loop_add_ms(100, INT_MAX) == INT_MAX);
    puts("PASS: real elapsed clock, fast/slow loops, rollback and saturation");
    return 0;
}
