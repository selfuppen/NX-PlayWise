#ifndef PTC_UI_STATUS_TRACE_H
#define PTC_UI_STATUS_TRACE_H

#include "../platform/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PTC_UI_STATUS_OTHER = 0,
    PTC_UI_STATUS_BUTTON_Y = 1,
    PTC_UI_STATUS_RESUME = 2,
    PTC_UI_STATUS_FOCUS = 4,
    PTC_UI_STATUS_RESUME_AND_FOCUS = 6,
    PTC_UI_STATUS_OVERLAY_READY = 8,
    PTC_UI_STATUS_TOUCH = 16,
} PtcUiStatusOrigin;

typedef struct {
    PtcStorage *storage;
    char config_path[256];
    char runtime_path[256];
    char output_path[320];
    char release_id[128];
    char backend_boot_id[17];
    uint8_t config_hash[32];
    uint64_t started_ns;
    uint64_t last_ns;
    uint64_t expires_ns;
    unsigned count;
    unsigned max_records;
    bool active;
} PtcUiStatusTrace;

/* Optional, same-thread observation. No payload, PIN, key mask or control writes. */
void ptc_ui_status_trace_init(PtcUiStatusTrace *trace, PtcStorage *storage,
    const char *root, const char *commit, const char *release_id,
    const char *client, uint64_t now_ns);
void ptc_ui_status_trace_record(PtcUiStatusTrace *trace, const char *request_id,
    PtcUiStatusOrigin origin, uint64_t now_ns, int focus_state, int transport_status);
void ptc_ui_status_trace_close(PtcUiStatusTrace *trace, uint64_t now_ns);

#ifdef __cplusplus
}
#endif
#endif
