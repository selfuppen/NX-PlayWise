#ifndef PTC_COMPANION_AUTH_CLOCK_H
#define PTC_COMPANION_AUTH_CLOCK_H

#include "auth.h"

/* Read the same user-system Unix clock in NRO and Tesla, without a retained
 * service or a cached status timestamp. On failure no authentication state changes. */
bool ptc_companion_auth_read_clock(int64_t *unix_seconds);
PtcAuthStatus ptc_companion_auth_verify_pin_now(
    PtcCompanionAuth *auth, const char *pin, int64_t *retry_after_seconds);

#endif
