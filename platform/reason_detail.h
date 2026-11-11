#ifndef PTC_REASON_DETAIL_H
#define PTC_REASON_DETAIL_H
#include "storage.h"
#include "../common/notification/reason_event.h"
#ifdef __cplusplus
extern "C" {
#endif

/* A short-lived offer to an already loaded, matching PlayWise UI. No loader IPC. */
typedef struct {
    char release[128], boot[17], owner[33];
    uint64_t ui_instance, sequence, issued_ms, expires_ms, instance;
    uint16_t day;
    PtcRestrictionReason reason;
} PtcReasonDetail;
bool ptc_reason_detail_read(PtcStorage *s, const char *root, const char *kind, PtcReasonDetail *out);
bool ptc_reason_detail_write(PtcStorage *s, const char *root, const char *kind, const PtcReasonDetail *value);
bool ptc_reason_detail_fresh(const PtcReasonDetail *value, const char *release, const char *boot, uint64_t ms);
bool ptc_reason_detail_same(const PtcReasonDetail *a, const PtcReasonDetail *b);
bool ptc_reason_detail_clear(PtcStorage *s, const char *root, const PtcReasonDetail *owned);
bool ptc_reason_detail_runtime(PtcStorage *s, const char *root, const char *release, char boot[17]);
#ifdef __cplusplus
}
#endif
#endif
