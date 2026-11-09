#pragma once

/* Optional local heap snapshots, not kernel resource counts or qualification. */
#if defined(__SWITCH__) || defined(PTC_OVERLAY_RESOURCE_STATS)
#include <malloc.h>
#include <stdio.h>
#include <stdint.h>

static inline void ptc_overlay_resource_snapshot(const char *phase, uint64_t mono_ns,
                                                 int connected, int waiting)
{
    static unsigned samples;
    static uint64_t instance_ns;
    if (!samples) instance_ns = mono_ns;
    if (samples >= 64) return;
    ++samples; /* Bound attempts as well as successful records; never retry UI work. */
    struct mallinfo memory = mallinfo();
    FILE *file = fopen("sdmc:/switch/playwise/support/overlay-resources.jsonl", "a");
    if (!file) return;
    if (fseek(file, 0, SEEK_END) != 0 || ftell(file) < 0 || ftell(file) >= 262144) { fclose(file); return; }
    fprintf(file, "{\"schema_version\":1,\"release_id\":\"%s\",\"instance_mono_ns\":\"%llu\",\"phase\":\"%s\",\"mono_ns\":\"%llu\","
            "\"heap_used_bytes\":%llu,\"heap_free_bytes\":%llu,"
            "\"ipc_connected\":%d,\"request_waiting\":%d}\n",
            PLAYWISE_BUILD_RELEASE_ID, (unsigned long long)instance_ns, phase, (unsigned long long)mono_ns, (unsigned long long)memory.uordblks,
            (unsigned long long)memory.fordblks, connected, waiting);
    fclose(file);
}
#endif
#ifndef PTC_OVERLAY_RESOURCE_STATS
#define ptc_overlay_resource_snapshot(phase, mono_ns, connected, waiting) ((void)0)
#endif
