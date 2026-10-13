#include <assert.h>
#include <stdio.h>
#include "usage_probe/boot_policy.h"

int main(void)
{
    const char *denied[] = {"begin", "resume", "limit", "unlimited", "unconfigured",
        "start", "stop", "clock", "clock_local", "restore", "finish", "reboot", "unknown"};
    for (unsigned i = 0; i < sizeof(denied)/sizeof(denied[0]); ++i) {
        assert(!probe_boot_operation_allowed(true, denied[i]));
        assert(probe_boot_operation_allowed(false, denied[i]));
    }
    const char *allowed[] = {"sample", "export", "collect", "exit", "reboot_readonly", "handoff_standard"};
    for (unsigned i = 0; i < sizeof(allowed)/sizeof(allowed[0]); ++i)
        assert(probe_boot_operation_allowed(true, allowed[i]));
    puts("PASS: usage probe read-only boot policy");
    return 0;
}
