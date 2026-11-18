#ifndef PTC_OVERLAY_BRIEF_NOTICE_H
#define PTC_OVERLAY_BRIEF_NOTICE_H

#include "../../platform/storage.h"

typedef struct {
    char seen[2][96];
    char title[128];
    char text[512];
    uint64_t expires_ms;
    uint64_t last_ms;
    uint64_t next_test_read_ms;
    uint64_t earliest_issued_ms;
    bool suppressed;
    bool active;
} PtcOverlayBriefNotice;

/* Call on suppression transitions even without storage. Returns true when the
   displayed notice changes. The publisher owns cleanup. */
bool ptc_overlay_brief_poll(PtcOverlayBriefNotice *notice, PtcStorage *storage,
    const char *root, const char *release, const char *boot, uint64_t now_ms,
    bool allowed, bool dismiss);

#endif
