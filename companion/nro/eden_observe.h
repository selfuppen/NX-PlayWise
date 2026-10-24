#ifndef PTC_EDEN_OBSERVE_H
#define PTC_EDEN_OBSERVE_H

#ifdef PLAYWISE_EDEN
#include <stdio.h>
#include <string.h>
#include "ui_model.h"
#include "ui_layout.h"

typedef struct {
    uint64_t sequence;
    bool active;
} PtcEdenInputObservation;

static inline void ptc_eden_sample_input(PtcEdenInputObservation *input, bool active)
{
    if (!input) return;
    if (input->sequence < UINT64_MAX) ++input->sequence;
    input->active = active;
}

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
        "\"plan_page\":%d,\"overlay_selection\":%d,\"editor_index\":%d,"
        "\"weekly_dirty\":%s,\"confirm_hold_required\":%s,\"config_groups\":%u,"
        "\"config_backup_ready\":%s,\"daily_buffer_available\":%s,\"daily_buffer_claimed\":%s,"
        "\"daily_restriction_active\":%s,\"dock_restriction_active\":%s,"
        "\"bedtime_active\":%s,\"bedtime_skipped\":%s,\"eye_resting\":%s,"
        "\"restricted_now\":%d,\"unrestricted_today\":%d,\"today_minutes\":%u,"
        "\"targets\":{\"width\":1280,\"height\":720",
        (int)m->view, (int)m->parent_page, (int)m->overlay, m->setup_step,
        strcmp(m->setup_phase, "active") == 0 ? "true" : "false",
        m->waiting ? "true" : "false", m->status_loaded ? "true" : "false",
        m->error_code, m->selected_index, (unsigned)strlen(m->pin_text),
        (unsigned)strlen(m->numpad_text), (int)m->numpad_purpose,
        m->code_result_failed ? "true" : "false", m->code_result_pending ? "true" : "false",
        ((m->overlay != PTC_UI_OVERLAY_NONE && m->overlay_open_frames < PTC_UI_OVERLAY_OPEN_FRAMES) ||
         m->closing_overlay != PTC_UI_OVERLAY_NONE) ? "true" : "false",
        (int)m->plan_page, m->overlay_selection, m->editor_index,
        m->weekly_dirty ? "true" : "false", m->confirm_hold_required ? "true" : "false",
        (unsigned)m->config_groups, m->config_backup_ready ? "true" : "false",
        m->daily_buffer_available ? "true" : "false", m->daily_buffer_claimed ? "true" : "false",
        m->daily_restriction_active ? "true" : "false", m->dock_restriction_active ? "true" : "false",
        m->bedtime_active ? "true" : "false", m->bedtime_skipped ? "true" : "false",
        strcmp(m->eye_care_phase, "resting") == 0 ? "true" : "false",
        m->restricted_now, m->unrestricted_today, (unsigned)m->forecast[0].minutes);
    if (n < 0 || (size_t)n >= size) { out[0] = '\0'; return false; }
    used = (size_t)n;
    if (!ptc_eden_target(out, size, &used, "setup_primary", ptc_ui_setup_primary_rect()) ||
        !ptc_eden_target(out, size, &used, "child_code", ptc_ui_child_submit_rect()) ||
        !ptc_eden_target(out, size, &used, "pin_confirm", ptc_ui_pin_confirm_rect())) goto fail;
    if (!ptc_eden_target(out, size, &used, "child_buffer", ptc_ui_child_buffer_rect())) goto fail;
    /* Only publish geometry for the current page. All centers come from the
     * production layout; the observer cannot submit or authorize an action. */
    for (int i = 0; i < 5; ++i) {
        char key[24];
        snprintf(key, sizeof(key), "tab_%d", i);
        if (!ptc_eden_target(out, size, &used, key, ptc_ui_parent_tab_rect(i))) goto fail;
    }
    for (int i = 0; i < 8; ++i) {
        char key[24];
        PtcUiRect rect = m->parent_page == PTC_UI_PARENT_TODAY ? ptc_ui_today_card_rect(i) :
            (m->parent_page == PTC_UI_PARENT_PLAN ? ptc_ui_plan_card_rect(i) : ptc_ui_settings_card_rect(i));
        snprintf(key, sizeof(key), "card_%d", i);
        if (!ptc_eden_target(out, size, &used, key, rect)) goto fail;
    }
    for (int i = 0; i < 7; ++i) {
        char key[24];
        snprintf(key, sizeof(key), "week_%d", i);
        if (!ptc_eden_target(out, size, &used, key, ptc_ui_weekly_day_minutes_rect(i))) goto fail;
    }
    for (int i = 0; i < 18; ++i) {
        char key[24];
        snprintf(key, sizeof(key), "backup_%d", i);
        if (!ptc_eden_target(out, size, &used, key, ptc_ui_config_backup_field_rect(i))) goto fail;
    }
    for (int i = 0; i < 11; ++i) {
        char key[24];
        snprintf(key, sizeof(key), "dock_%d", i);
        if (!ptc_eden_target(out, size, &used, key, ptc_ui_dock_field_rect(i))) goto fail;
    }
    for (int i = 0; i < 2; ++i) {
        char key[24];
        snprintf(key, sizeof(key), "duration_%d", i);
        if (!ptc_eden_target(out, size, &used, key, ptc_ui_minute_editor_field_rect((PtcUiDurationField)i))) goto fail;
    }
    if (!ptc_eden_target(out, size, &used, "eye_master", ptc_ui_eye_care_page_master_rect()) ||
        !ptc_eden_target(out, size, &used, "bedtime_master", ptc_ui_bedtime_master_switch_rect())) goto fail;
    for (int digit = 0; digit <= 9; ++digit) {
        char key[16];
        snprintf(key, sizeof(key), "pin_%d", digit);
        if (!ptc_eden_target(out, size, &used, key, ptc_ui_pin_key_rect(digit))) goto fail;
        snprintf(key, sizeof(key), "code_%d", digit);
        if (!ptc_eden_target(out, size, &used, key,
                m->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR
                    ? ptc_ui_minute_editor_key_rect(digit == 0 ? 10 : digit - 1)
                    : ptc_ui_numpad_key_rect(digit == 0 ? 10 : digit - 1))) goto fail;
    }
    if (size - used < 3) goto fail;
    memcpy(out + used, "}}", 3);
    return true;
fail:
    out[0] = '\0';
    return false;
}

void ptc_eden_observe_ui(const PtcUiModel *model);
void ptc_eden_observe_input(bool active);
#endif
#endif
