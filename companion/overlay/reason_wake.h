#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PTC_OVERLAY_REASON_AUDIT_MS UINT64_C(1000)
#define PTC_OVERLAY_REASON_SCAN_MS UINT64_C(2000)

typedef struct {
    uint64_t signal_ms;
    bool signaled;
} PtcOverlayReasonWake;

static inline void ptc_overlay_reason_wake(PtcOverlayReasonWake *wake, uint64_t ms)
{
    wake->signal_ms = ms;
    wake->signaled = true;
}

/* The normal poll gates run first. Audit even when an event handle looks
   healthy: a lost wake or disconnected peer must still allow an SD read. */
static inline bool ptc_overlay_reason_scan(const PtcOverlayReasonWake *wake, bool subscribed,
    uint64_t ms, uint64_t *next_audit_ms)
{
    bool signaled = wake->signaled && ms >= wake->signal_ms && ms - wake->signal_ms < PTC_OVERLAY_REASON_SCAN_MS;
    if (subscribed && !signaled && ms < *next_audit_ms && *next_audit_ms - ms <= PTC_OVERLAY_REASON_AUDIT_MS)
        return false;
    *next_audit_ms = ms <= UINT64_MAX - PTC_OVERLAY_REASON_AUDIT_MS ? ms + PTC_OVERLAY_REASON_AUDIT_MS : UINT64_MAX;
    return true;
}
