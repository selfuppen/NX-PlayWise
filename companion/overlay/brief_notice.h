#ifndef PTC_OVERLAY_BRIEF_NOTICE_H
#define PTC_OVERLAY_BRIEF_NOTICE_H

#include "../../platform/storage.h"

typedef struct {
    char seen[2][96];
    char title[128];
    char text[512];
    uint64_t expires_ms;
    uint64_t last_ms;
    bool active;
} PtcOverlayBriefNotice;

/* Read-only consumer. The original publisher retains file cleanup ownership. */
void ptc_overlay_brief_poll(PtcOverlayBriefNotice *notice, PtcStorage *storage,
    const char *root, const char *release, const char *boot, uint64_t now_ms,
    bool allowed, bool dismiss);

#endif
