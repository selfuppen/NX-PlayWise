#ifndef PTC_PCTL_USAGE_TAP_H
#define PTC_PCTL_USAGE_TAP_H
#include <stdbool.h>

/* Internal capture build only; no product counter/UI or command changes. */
#ifdef PLAYWISE_PCTL_CAPTURE
void pctl_usage_tap_init(const char *boot_id);
void pctl_usage_tap_tick(const char *stage, long long unix_s, int day_index, bool force);
#endif
#endif
