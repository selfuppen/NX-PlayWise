#ifndef PLAYWISE_USAGE_PROBE_BOOT_POLICY_H
#define PLAYWISE_USAGE_PROBE_BOOT_POLICY_H

#include <stdbool.h>
#include <string.h>

/* This profile may observe, export, reboot explicitly, and return ownership.
 * PCTL/settings/clock mutation is forbidden even when an old queue says begin. */
static inline bool probe_boot_operation_allowed(bool read_only, const char *op)
{
    if (!read_only) return true;
    return strcmp(op, "sample") == 0 || strcmp(op, "export") == 0 ||
        strcmp(op, "collect") == 0 || strcmp(op, "exit") == 0 ||
        strcmp(op, "reboot_readonly") == 0 || strcmp(op, "handoff_standard") == 0;
}

#endif
