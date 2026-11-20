#ifndef PTC_NRO_LOOP_CLOCK_H
#define PTC_NRO_LOOP_CLOCK_H

#include <stdint.h>
#include <limits.h>

/* Count time spent in I/O, rendering and sleep. A regressed clock starts a new
 * interval; it must not produce a huge unsigned delta or replay missed polls. */
static inline int ptc_loop_elapsed_ms(int64_t *previous, int64_t now)
{
    int64_t before = *previous;
    *previous = now;
    if (before < 0 || now < before) return 0;
    uint64_t delta = (uint64_t)now - (uint64_t)before;
    return delta > INT_MAX ? INT_MAX : (int)delta;
}

static inline int ptc_loop_add_ms(int elapsed, int delta)
{
    return delta > INT_MAX - elapsed ? INT_MAX : elapsed + delta;
}

#endif
