#ifndef PTC_EDEN_OBSERVE_H
#define PTC_EDEN_OBSERVE_H

#ifdef PLAYWISE_EDEN
#include <stdio.h>
#include <string.h>
#include "ui_model.h"
#include "ui_layout.h"

static inline bool ptc_eden_target(char *out, size_t size, size_t *used,
                                   const char *name, PtcUiRect rect)
{
    int n = snprintf(out + *used, size - *used, ",\"%s\":[%d,%d]",
        name, rect.x + rect.w / 2, rect.y + rect.h / 2);
    if (n < 0 || (size_t)n >= size - *used) return false;
    *used += (size_t)n;
    return true;
}

/* Test-only observation, never an action or authentication interface. Keep this
 * whitelist numeric: model strings can contain PINs, codes or pairing secrets. */
static inline bool ptc_eden_format_ui(const PtcUiModel *m, char *out, size_t size)
{
    int n;
    size_t used;
    if (!out || size == 0) return false;
    out[0] = '\0';
    if (!m) return false;
    n = snprintf(out, size,
        "{\"view\":%d,\"parent_page\":%d,\"overlay\":%d,"
        "\"setup_step\":%d,\"setup_active\":%s,\"waiting\":%s,"
        "\"status_loaded\":%s,\"error_code\":%d,\"selected_index\":%d,"
        "\"pin_length\":%u,\"numpad_length\":%u,\"numpad_purpose\":%d,"
        "\"code_result_failed\":%s,\"code_result_pending\":%s,\"overlay_animating\":%s,"
        "\"targets\":{\"width\":1280,\"height\":720",
        (int)m->view, (int)m->parent_page, (int)m->overlay, m->setup_step,
        strcmp(m->setup_phase, "active") == 0 ? "true" : "false",
        m->waiting ? "true" : "false", m->status_loaded ? "true" : "false",
        m->error_code, m->selected_index, (unsigned)strlen(m->pin_text),
        (unsigned)strlen(m->numpad_text), (int)m->numpad_purpose,
        m->code_result_failed ? "true" : "false", m->code_result_pending ? "true" : "false",
        ((m->overlay != PTC_UI_OVERLAY_NONE && m->overlay_open_frames < PTC_UI_OVERLAY_OPEN_FRAMES) ||
         m->closing_overlay != PTC_UI_OVERLAY_NONE) ? "true" : "false");
    if (n < 0 || (size_t)n >= size) { out[0] = '\0'; return false; }
    used = (size_t)n;
    if (!ptc_eden_target(out, size, &used, "setup_primary", ptc_ui_setup_primary_rect()) ||
        !ptc_eden_target(out, size, &used, "child_code", ptc_ui_child_submit_rect()) ||
        !ptc_eden_target(out, size, &used, "pin_confirm", ptc_ui_pin_confirm_rect())) goto fail;
    for (int digit = 0; digit <= 9; ++digit) {
        char key[16];
        snprintf(key, sizeof(key), "pin_%d", digit);
        if (!ptc_eden_target(out, size, &used, key, ptc_ui_pin_key_rect(digit))) goto fail;
        snprintf(key, sizeof(key), "code_%d", digit);
        if (!ptc_eden_target(out, size, &used, key,
                ptc_ui_numpad_key_rect(digit == 0 ? 10 : digit - 1))) goto fail;
    }
    if (size - used < 3) goto fail;
    memcpy(out + used, "}}", 3);
    return true;
fail:
    out[0] = '\0';
    return false;
}

void ptc_eden_observe_ui(const PtcUiModel *model);
#endif
#endif
