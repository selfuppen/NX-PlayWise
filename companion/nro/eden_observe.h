#ifndef PTC_EDEN_OBSERVE_H
#define PTC_EDEN_OBSERVE_H

#ifdef PLAYWISE_EDEN
#include <stdio.h>
#include <string.h>
#include "ui_model.h"
#include "ui_layout.h"
#include "../../common/protocol/request_type.h"

static inline bool ptc_eden_plan_request_recordable(PtcRequestType type)
{
    return type == PTC_REQUEST_IMPORT_HOLIDAY_CALENDAR ||
        type == PTC_REQUEST_ACTIVATE_HOLIDAY_CALENDAR || type == PTC_REQUEST_SET_BEDTIME_POLICY;
}

typedef struct {
    uint64_t sequence;
    bool active;
} PtcEdenInputObservation;

void ptc_eden_trace_stage(unsigned stage, int detail);

static inline bool ptc_eden_format_input(char *out, size_t size,
    const PtcEdenInputObservation *input, uint64_t instance, uint64_t tick)
{
    if (!out || !size) return false;
    out[0] = '\0';
    if (!input) return false;
    int n = snprintf(out, size,
        "{\"schema_version\":1,\"input_stream_version\":1,\"input_sampling_version\":2,"
        "\"instance\":\"%llx\",\"sequence\":%llu,\"tick\":\"%llu\","
        "\"input_sequence\":%llu,\"input_active\":%s}",
        (unsigned long long)instance, (unsigned long long)input->sequence,
        (unsigned long long)tick, (unsigned long long)input->sequence, input->active ? "true" : "false");
    if (n < 0 || (size_t)n >= size) { out[0] = '\0'; return false; }
    return true;
}

enum {
    PTC_EDEN_TRACE_INPUT = 1, PTC_EDEN_TRACE_DISPATCH_DONE,
    PTC_EDEN_TRACE_TICK_BEGIN, PTC_EDEN_TRACE_TICK_END,
    PTC_EDEN_TRACE_POLL_BEGIN, PTC_EDEN_TRACE_POLL_END,
    PTC_EDEN_TRACE_DRAW_BEGIN, PTC_EDEN_TRACE_FRAMEBUFFER_READY,
    PTC_EDEN_TRACE_CONTENT_DONE, PTC_EDEN_TRACE_PRESENT_DONE,
    PTC_EDEN_TRACE_OBSERVE_BEGIN, PTC_EDEN_TRACE_OBSERVE_END,
    PTC_EDEN_TRACE_REQUEST_BEGIN, PTC_EDEN_TRACE_REQUEST_END,
    PTC_EDEN_TRACE_FORMAT_FAILED, PTC_EDEN_TRACE_PRESENT_BEGIN
};

static inline bool ptc_eden_format_progress(char *out, size_t size,
    const PtcEdenInputObservation *input, unsigned stage, int detail,
    uint64_t instance, uint64_t sequence, uint64_t tick)
{
    if (!out || !size) return false;
    out[0] = '\0';
    if (!input || stage < PTC_EDEN_TRACE_INPUT || stage > PTC_EDEN_TRACE_PRESENT_BEGIN) return false;
    int n = snprintf(out, size,
        "{\"schema_version\":1,\"instance\":\"%llx\",\"sequence\":%llu,\"tick\":\"%llu\","
        "\"stage\":%u,\"detail\":%d,\"input_sequence\":%llu,\"input_active\":%s}",
        (unsigned long long)instance, (unsigned long long)sequence, (unsigned long long)tick,
        stage, detail, (unsigned long long)input->sequence, input->active ? "true" : "false");
    if (n < 0 || (size_t)n >= size) { out[0] = '\0'; return false; }
    return true;
}

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

/* Stable numeric source IDs; never copy a model's arbitrary string to JSON. */
static inline int ptc_eden_rule_source(const char *source)
{
    static const char *names[] = {"weekly", "today_override", "scheduled_override",
        "statutory_holiday", "makeup_workday"};
    if (source) for (int i = 0; i < 5; ++i) if (!strcmp(source, names[i])) return i;
    return -1;
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
        "\"disable_flag_present\":%s,\"recovery_active\":%s,\"diagnostic_status\":%d,",
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
        m->restricted_now, m->unrestricted_today, (unsigned)m->forecast[0].minutes,
        m->disable_flag_present ? "true" : "false", m->recovery_active ? "true" : "false",
        (int)m->diagnostic_status);
    if (n < 0 || (size_t)n >= size) { out[0] = '\0'; return false; }
    used = (size_t)n;
    /* Insert read-only plan state before geometry. Policy values contain no
     * authentication material and are also checked against saved receipts. */
    n = snprintf(out + used, size - used,
        "\"calendar_tab\":%d,\"calendar_selected\":%d,\"calendar_count\":%d,"
        "\"calendar_builtin\":%s,\"bedtime_section\":%d,\"bedtime_dirty\":%s,"
        "\"bedtime_editor_day\":%d,\"bedtime_special_kind\":%d,"
        "\"bedtime_draft_enabled\":%s,\"bedtime_draft_calendar\":%s,"
        "\"bedtime_draft_present\":%s,\"bedtime_draft_start_day\":%u,\"bedtime_draft_end_day\":%u,"
        "\"bedtime_windows\":[",
        m->calendar_manager_tab, m->calendar_manager_selected, m->calendar_manager_count,
        m->calendar_builtin ? "true" : "false", (int)m->bedtime_section, m->bedtime_dirty ? "true" : "false",
        m->bedtime_editor_day, m->bedtime_special_kind,
        m->draft_bedtime_policy.enabled ? "true" : "false", m->draft_bedtime_policy.calendar_enabled ? "true" : "false",
        m->draft_bedtime_policy.scheduled_override.present ? "true" : "false",
        (unsigned)m->draft_bedtime_policy.scheduled_override.start_day_index,
        (unsigned)m->draft_bedtime_policy.scheduled_override.end_day_index);
    if (n < 0 || (size_t)n >= size - used) goto fail;
    used += (size_t)n;
    for (int i = 0; i < 10; ++i) {
        const PtcBedtimeSpecialRule *special = i < 7 ? NULL :
            (i == 7 ? &m->draft_bedtime_policy.holiday_rule :
             (i == 8 ? &m->draft_bedtime_policy.makeup_workday_rule : &m->draft_bedtime_policy.scheduled_override.rule));
        const PtcBedtimeWindow *w = i < 7 ? &m->draft_bedtime_policy.week[i] : &special->window;
        n = snprintf(out + used, size - used, "%s[%d,%u,%u]", i ? "," : "",
            special ? (int)special->mode : (int)w->enabled, (unsigned)w->start_minute, (unsigned)w->end_minute);
        if (n < 0 || (size_t)n >= size - used) goto fail;
        used += (size_t)n;
    }
    n = snprintf(out + used, size - used, "],\"forecast\":[");
    if (n < 0 || (size_t)n >= size - used) goto fail;
    used += (size_t)n;
    for (unsigned i = 0; i < PTC_RESULT_FORECAST_DAYS; ++i) {
        const PtcResultForecastDay *f = &m->forecast[i];
        n = snprintf(out + used, size - used, "%s[%u,%d,%u,%d,%s]", i ? "," : "",
            (unsigned)f->day_index, f->mode, (unsigned)f->minutes,
            ptc_eden_rule_source(f->rule_source), f->calendar_covered ? "true" : "false");
        if (n < 0 || (size_t)n >= size - used) goto fail;
        used += (size_t)n;
    }
    n = snprintf(out + used, size - used, "],\"targets\":{\"width\":1280,\"height\":720");
    if (n < 0 || (size_t)n >= size - used) goto fail;
    used += (size_t)n;
    if (!ptc_eden_target(out, size, &used, "holiday_manage", ptc_ui_holiday_manage_rect())) goto fail;
    for (int i = 0; i < 11; ++i) {
        char key[24];
        snprintf(key, sizeof(key), "bedtime_field_%d", i);
        if (!ptc_eden_target(out, size, &used, key, ptc_ui_bedtime_field_rect(m->bedtime_section, i))) goto fail;
    }
    for (int i = 0; i < 5; ++i) {
        char key[24];
        snprintf(key, sizeof(key), "bedtime_preset_%d", i);
        if (!ptc_eden_target(out, size, &used, key, ptc_ui_bedtime_preset_rect(m->overlay, i))) goto fail;
    }
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
            (m->parent_page == PTC_UI_PARENT_PLAN ? ptc_ui_plan_card_rect(i) :
             (m->parent_page == PTC_UI_PARENT_SUPPORT ? ptc_ui_support_card_rect(i) : ptc_ui_settings_card_rect(i)));
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
