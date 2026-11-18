#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t started_ms;
    uint64_t completed_ms;
    uint64_t max_elapsed_ms;
    bool attempted;
    bool read_ok;
} PtcOverlayHiddenReadTiming;

typedef struct {
    uint64_t poll_started_ms;
    uint64_t previous_poll_started_ms;
    uint64_t max_poll_gap_ms;
    uint64_t non_neutral_polls;
    uint64_t ready_issued_ms;
    uint64_t ready_expires_ms;
    uint64_t ready_write_started_ms;
    uint64_t ready_write_completed_ms;
    uint64_t previous_ready_issued_ms;
    uint64_t previous_ready_write_completed_ms;
    bool neutral;
    bool ready_write_ok;
    bool previous_ready_write_ok;
    bool clock_order_valid;
    PtcOverlayHiddenReadTiming config_read;
    PtcOverlayHiddenReadTiming runtime_read;
    PtcOverlayHiddenReadTiming request_read;
} PtcOverlayHiddenTiming;

static inline void ptc_overlay_hidden_timing_init(PtcOverlayHiddenTiming *trace)
{
    memset(trace, 0, sizeof(*trace));
    trace->clock_order_valid = true;
}

static inline void ptc_overlay_hidden_timing_poll(PtcOverlayHiddenTiming *trace,
    uint64_t started_ms, bool neutral)
{
    trace->previous_poll_started_ms = trace->poll_started_ms;
    if (started_ms < trace->poll_started_ms || started_ms < trace->ready_write_completed_ms ||
        started_ms < trace->config_read.completed_ms || started_ms < trace->runtime_read.completed_ms ||
        started_ms < trace->request_read.completed_ms) {
        trace->clock_order_valid = false;
    } else if (trace->poll_started_ms) {
        uint64_t gap = started_ms - trace->poll_started_ms;
        if (gap > trace->max_poll_gap_ms) trace->max_poll_gap_ms = gap;
    }
    trace->poll_started_ms = started_ms;
    trace->neutral = neutral;
    if (!neutral && trace->non_neutral_polls != UINT64_MAX) ++trace->non_neutral_polls;
    PtcOverlayHiddenReadTiming *reads[] = {&trace->config_read, &trace->runtime_read, &trace->request_read};
    for (unsigned i = 0; i < 3; ++i) {
        reads[i]->started_ms = 0;
        reads[i]->completed_ms = 0;
        reads[i]->attempted = false;
        reads[i]->read_ok = false;
    }
}

static inline void ptc_overlay_hidden_timing_read_begin(PtcOverlayHiddenTiming *trace,
    PtcOverlayHiddenReadTiming *read, uint64_t started_ms)
{
    read->started_ms = started_ms;
    read->completed_ms = 0;
    read->attempted = true;
    read->read_ok = false;
    if (started_ms < trace->poll_started_ms) trace->clock_order_valid = false;
}

static inline void ptc_overlay_hidden_timing_read_end(PtcOverlayHiddenTiming *trace,
    PtcOverlayHiddenReadTiming *read, uint64_t completed_ms, bool read_ok)
{
    read->completed_ms = completed_ms;
    read->read_ok = read_ok;
    if (!read->attempted || completed_ms < read->started_ms) {
        trace->clock_order_valid = false;
    } else {
        uint64_t elapsed = completed_ms - read->started_ms;
        if (elapsed > read->max_elapsed_ms) read->max_elapsed_ms = elapsed;
    }
}

static inline void ptc_overlay_hidden_timing_ready_begin(PtcOverlayHiddenTiming *trace,
    uint64_t issued_ms, uint64_t expires_ms, uint64_t write_started_ms)
{
    trace->previous_ready_issued_ms = trace->ready_issued_ms;
    trace->previous_ready_write_completed_ms = trace->ready_write_completed_ms;
    trace->previous_ready_write_ok = trace->ready_write_ok;
    trace->ready_issued_ms = issued_ms;
    trace->ready_expires_ms = expires_ms;
    trace->ready_write_started_ms = write_started_ms;
    trace->ready_write_completed_ms = 0;
    trace->ready_write_ok = false;
    if (write_started_ms < issued_ms || expires_ms <= issued_ms) trace->clock_order_valid = false;
}

static inline void ptc_overlay_hidden_timing_ready_end(PtcOverlayHiddenTiming *trace,
    uint64_t completed_ms, bool write_ok)
{
    trace->ready_write_completed_ms = completed_ms;
    trace->ready_write_ok = write_ok;
    if (completed_ms < trace->ready_write_started_ms) trace->clock_order_valid = false;
}

/* Snapshot only. Callers retain their existing state-change write budget. */
static inline bool ptc_overlay_hidden_read_timing_format(const PtcOverlayHiddenReadTiming *read,
    char *out, size_t capacity)
{
    int length = snprintf(out, capacity,
        "{\"started_ms\":\"%llu\",\"completed_ms\":\"%llu\",\"max_elapsed_ms\":\"%llu\","
        "\"attempted\":%s,\"read_ok\":%s}",
        (unsigned long long)read->started_ms, (unsigned long long)read->completed_ms,
        (unsigned long long)read->max_elapsed_ms, read->attempted ? "true" : "false",
        read->read_ok ? "true" : "false");
    return length > 0 && (size_t)length < capacity;
}

static inline bool ptc_overlay_hidden_timing_format(const PtcOverlayHiddenTiming *trace,
    char *out, size_t capacity)
{
    char config[256], runtime[256], request[256];
    if (!ptc_overlay_hidden_read_timing_format(&trace->config_read, config, sizeof(config)) ||
        !ptc_overlay_hidden_read_timing_format(&trace->runtime_read, runtime, sizeof(runtime)) ||
        !ptc_overlay_hidden_read_timing_format(&trace->request_read, request, sizeof(request))) return false;
    int length = snprintf(out, capacity,
        "{\"version\":1,\"poll_started_ms\":\"%llu\",\"previous_poll_started_ms\":\"%llu\","
        "\"max_poll_gap_ms\":\"%llu\",\"non_neutral_polls\":\"%llu\",\"neutral\":%s,"
        "\"ready_issued_ms\":\"%llu\",\"ready_expires_ms\":\"%llu\","
        "\"ready_write_started_ms\":\"%llu\",\"ready_write_completed_ms\":\"%llu\",\"ready_write_ok\":%s,"
        "\"previous_ready_issued_ms\":\"%llu\",\"previous_ready_write_completed_ms\":\"%llu\","
        "\"previous_ready_write_ok\":%s,\"clock_order_valid\":%s,"
        "\"reads\":{\"config\":%s,\"runtime\":%s,\"request\":%s}}",
        (unsigned long long)trace->poll_started_ms, (unsigned long long)trace->previous_poll_started_ms,
        (unsigned long long)trace->max_poll_gap_ms, (unsigned long long)trace->non_neutral_polls,
        trace->neutral ? "true" : "false", (unsigned long long)trace->ready_issued_ms,
        (unsigned long long)trace->ready_expires_ms, (unsigned long long)trace->ready_write_started_ms,
        (unsigned long long)trace->ready_write_completed_ms, trace->ready_write_ok ? "true" : "false",
        (unsigned long long)trace->previous_ready_issued_ms,
        (unsigned long long)trace->previous_ready_write_completed_ms,
        trace->previous_ready_write_ok ? "true" : "false", trace->clock_order_valid ? "true" : "false",
        config, runtime, request);
    return length > 0 && (size_t)length < capacity;
}
