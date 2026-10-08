#include "ui_state.h"
#include "ui_layout.h"
#include "../ui_language.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../common/protocol/error_code.h"
#include "../../common/rules/holiday_calendar.h"
#include "../../common/time/ptc_time.h"

int ptc_ui_preview_remaining_minutes(const PtcUiModel *model)
{
    int remaining;
    if (!model) {
        return 0;
    }

    if (model->operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        if (model->remaining_available && model->remaining_minutes >= 0) {
            return model->remaining_minutes + (int)model->draft_minutes;
        }
        return -1;
    }

    if (!model->played_minutes_available || model->played_minutes < 0) {
        return -1;
    }
    remaining = (int)model->draft_minutes;
    if (model->played_minutes_available && model->played_minutes >= 0) {
        remaining = (int)model->draft_minutes - model->played_minutes;
        if (remaining < 0) {
            remaining = 0;
        }
    }
    return remaining;
}

void ptc_ui_mark_status_updated(PtcUiModel *model, int64_t now)
{
    if (!model) {
        return;
    }
    model->status_updated_at = now > 0 ? now : 0;
}

int64_t ptc_ui_status_age_seconds(const PtcUiModel *model, int64_t now)
{
    if (!model || !model->status_loaded || model->status_updated_at <= 0) {
        return -1;
    }
    if (now < model->status_updated_at) return -1;
    if (now == model->status_updated_at) {
        return 0;
    }
    return now - model->status_updated_at;
}

bool ptc_ui_status_is_fresh(const PtcUiModel *model, int64_t now)
{
    int64_t age = ptc_ui_status_age_seconds(model, now);
    return model && age >= 0 && age <= 120 && model->error_code == 0 &&
        strcmp(model->result_status, "error") != 0;
}

void ptc_ui_format_eye_care_cycle(const PtcUiModel *model, int64_t now,
    char *out, size_t out_size)
{
    int64_t age;
    PtcUiTextId id;
    PtcUiTextArg arg;
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!ptc_ui_status_is_fresh(model, now)) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_REFRESH));
        return;
    }
    if (!model->eye_care_policy.enabled || strcmp(model->eye_care_phase, "off") == 0) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_OFF));
        return;
    }
    if (strcmp(model->eye_care_phase, "paused") == 0) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_PAUSED));
        return;
    }
    if (strcmp(model->eye_care_phase, "playing") == 0) {
        unsigned remaining = model->eye_care_used_minutes >= model->eye_care_policy.play_minutes
            ? 0u : (unsigned)(model->eye_care_policy.play_minutes - model->eye_care_used_minutes);
        if (remaining == 0) {
            snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_REFRESH));
            return;
        }
        id = PTC_UI_T_EYE_CARE_PLAY_COUNTDOWN_NAMED;
        arg = PTC_UI_TEXT_NUMBER("minutes", remaining);
        (void)ptc_ui_text_format(id, out, out_size, &arg, 1);
        return;
    }
    if (strcmp(model->eye_care_phase, "resting") == 0) {
        int64_t remaining;
        age = ptc_ui_status_age_seconds(model, now);
        remaining = (int64_t)model->eye_care_rest_remaining_seconds - age;
        if (remaining <= 0 || model->eye_care_break_id == 0) {
            snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_REFRESH));
            return;
        }
        id = PTC_UI_T_EYE_CARE_REST_COUNTDOWN_NAMED;
        char seconds[3];
        snprintf(seconds, sizeof(seconds), "%02u", (unsigned)(remaining % 60));
        PtcUiTextArg args[] = {
            PTC_UI_TEXT_NUMBER("minutes", remaining / 60),
            PTC_UI_TEXT_STRING("seconds", seconds)
        };
        (void)ptc_ui_text_format(id, out, out_size, args, 2);
        return;
    }
    snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_UNKNOWN));
}

const char *ptc_ui_today_action_unavailable_reason(const PtcUiModel *model,
    int index, int64_t now)
{
    if (index == 7) {
        if (!ptc_ui_status_is_fresh(model, now) || !model->dock_available)
            return ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM);
        if (model->dock_waived_today) return ptc_ui_text(PTC_UI_T_DOCK_WAIVED);
        return model->dock_policy.force_docked || model->dock_policy.undocked_limit_enabled
            ? NULL : ptc_ui_text(PTC_UI_T_DOCK_OFF);
    }
    if (index == 6) {
        int64_t age;
        if (!ptc_ui_status_is_fresh(model, now))
            return ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_REFRESH);
        if (!model->eye_care_policy.enabled)
            return ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_OFF);
        if (strcmp(model->eye_care_phase, "resting") != 0)
            return ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_NOT_RESTING);
        age = ptc_ui_status_age_seconds(model, now);
        if (model->eye_care_break_id == 0 ||
            (!model->disable_flag_present && (int64_t)model->eye_care_rest_remaining_seconds <= age))
            return ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_REFRESH);
        if ((model->bedtime_active && !model->bedtime_skipped) ||
            model->daily_restriction_active)
            return ptc_ui_text(PTC_UI_T_EYE_CARE_LIMIT_PRIORITY);
        return NULL;
    }
    if (!ptc_ui_status_is_fresh(model, now)) return NULL;
    if ((index == 1 || index == 2) && model->dock_restriction_active)
        return ptc_ui_text(PTC_UI_T_DOCK_CONNECT);
    if (index == 1 && model->unrestricted_today == 1)
        return ptc_ui_text(PTC_UI_T_NO_TIME_LIMIT_TODAY_NO_GRANT_REQUIRED);
    if (index != 4) return NULL;
    if (!model->bedtime_policy.enabled)
        return ptc_ui_text(PTC_UI_T_BEDTIME_SCHEDULE_IS_TURNED_OFF_NO_SKIP);
    if (model->bedtime_active) {
        if (model->bedtime_skipped) return ptc_ui_text(PTC_UI_T_THIS_BEDTIME_HAS_BEEN_SKIPPED);
        return model->bedtime_window_instance_id != 0 ? NULL :
            ptc_ui_text(PTC_UI_T_THERE_ARE_CURRENTLY_NO_SKIPPABLE_BEDTIME_PERIODS);
    }
    if (model->bedtime_next_available && model->bedtime_next_window_instance_id != 0) {
        if (model->bedtime_skipped_window_available &&
            model->bedtime_next_window_instance_id == model->bedtime_skipped_window_instance_id)
            return ptc_ui_text(PTC_UI_T_THIS_BEDTIME_HAS_BEEN_SKIPPED);
        return NULL;
    }
    if (ptc_ui_bedtime_skip_matches_policy(model, &model->bedtime_policy))
        return ptc_ui_text(PTC_UI_T_THIS_BEDTIME_HAS_BEEN_SKIPPED);
    return ptc_ui_text(PTC_UI_T_THERE_ARE_CURRENTLY_NO_SKIPPABLE_BEDTIME_PERIODS);
}

void ptc_ui_quota_recheck_snapshot(const PtcUiModel *model,
    PtcUiQuotaRecheckSnapshot *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!model) return;
    out->operation = model->operation;
    out->hold_required = model->confirm_hold_required;
    out->remaining_minutes = model->remaining_minutes;
    out->played_minutes = model->played_minutes;
    out->unrestricted_today = model->unrestricted_today;
    out->remaining_available = model->remaining_available;
    out->played_available = model->played_minutes_available;
    out->day_index = model->day_index;
    out->today_override_present = model->today_override_present;
    out->today_override_rule = model->today_override_rule;
    snprintf(out->rule_source, sizeof(out->rule_source), "%s", model->rule_source);
}

PtcUiQuotaRecheckDecision ptc_ui_quota_recheck_decide(
    const PtcUiQuotaRecheckSnapshot *before, const PtcUiModel *after,
    bool refresh_succeeded, bool manual, int64_t now, bool *hold_required)
{
    bool changed;
    bool hold;
    if (!before || !after || !refresh_succeeded ||
        !ptc_ui_status_is_fresh(after, now) ||
        after->operation != before->operation) return PTC_UI_QUOTA_RECHECK_BLOCK;
    changed = before->remaining_minutes != after->remaining_minutes ||
        before->played_minutes != after->played_minutes ||
        before->unrestricted_today != after->unrestricted_today ||
        before->remaining_available != after->remaining_available ||
        before->played_available != after->played_minutes_available ||
        before->day_index != after->day_index ||
        before->today_override_present != after->today_override_present ||
        before->today_override_rule.mode != after->today_override_rule.mode ||
        before->today_override_rule.minutes != after->today_override_rule.minutes ||
        strcmp(before->rule_source, after->rule_source) != 0;
    hold = after->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT
        ? ptc_ui_today_limit_requires_hold(after, after->draft_minutes) :
        (after->operation == PTC_UI_OPERATION_RESTORE_TODAY_POLICY &&
         ptc_ui_rule_after_today_restore(after).rule.mode == PTC_RULE_MODE_LIMIT &&
         (!after->played_minutes_available ||
          ptc_ui_rule_after_today_restore(after).rule.minutes <= after->played_minutes));
    if (hold_required) *hold_required = hold;
    return changed || hold != before->hold_required || manual
        ? PTC_UI_QUOTA_RECHECK_CONFIRM_AGAIN : PTC_UI_QUOTA_RECHECK_SUBMIT;
}

bool ptc_ui_parent_status_alert_visible(const PtcUiModel *model)
{
    if (!model) return false;
    return strcmp(model->setup_phase, "protection") == 0 ||
        strcmp(model->setup_phase, "failed") == 0 || model->recovery_active ||
        model->disable_flag_present ||
        (model->temporary_unlocked_available && model->temporary_unlocked);
}

bool ptc_ui_operation_feedback_visible(const PtcUiModel *model)
{
    if (!model) return false;
    if (model->waiting || strcmp(model->result_status, "error") == 0 ||
        model->feedback_detail[0]) return true;
    return strcmp(model->result_status, "ok") == 0 && model->message[0] &&
        model->command_name[0] &&
        strcmp(model->command_name, ptc_ui_text(PTC_UI_T_REFRESH_STATUS)) != 0 &&
        strcmp(model->command_name, ptc_ui_text(PTC_UI_T_NOT_STARTED)) != 0;
}

const char *ptc_ui_runtime_notice_summary(const PtcUiModel *model)
{
    if (!model) return "";
    if (model->disable_flag_present) return ptc_ui_text(PTC_UI_T_CONTROL_HAS_BEEN_DEACTIVATED_PLEASE_CONTACT_SUPPORT);
    if (model->recovery_active) return ptc_ui_text(PTC_UI_T_SETTINGS_ARE_BEING_RESTORED_PLEASE_WAIT_FOR);
    if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0)
        return ptc_ui_text(PTC_UI_T_NEEDS_PARENTAL_PROCESSING_PLEASE_ENTER_SUPPORT_AND);
    if (model->restriction_enabled_available && !model->restriction_enabled)
        return ptc_ui_text(PTC_UI_T_NINTENDO_PARENTAL_CONTROLS_ARE_NOT_ENABLED_PLEASE);
    if (model->temporary_unlocked_available && model->temporary_unlocked)
        return ptc_ui_text(PTC_UI_T_THERE_IS_NO_TIMER_DURING_THE_TEMPORARY);
    if (model->apply_pending_confirmation) return ptc_ui_text(PTC_UI_T_THE_SETTING_IS_WAITING_FOR_CONFIRMATION_TO);
    if (model->eye_care_policy.enabled && strcmp(model->eye_care_phase, "resting") == 0)
        return ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING);
    if (model->dock_restriction_active) return ptc_ui_text(PTC_UI_T_DOCK_BLOCKED);
    if (model->restricted_now == 1) return ptc_ui_text(PTC_UI_T_HAS_ENTERED_THE_TIME_LIMIT_YOU_CAN);
    if (model->remaining_available && model->remaining_minutes == 0 && model->unrestricted_today != 1)
        return ptc_ui_text(PTC_UI_T_THE_QUOTA_HAS_BEEN_USED_UP_RESTRICTIONS);
    return "";
}

void ptc_ui_project_notice(const PtcUiModel *model, PtcUiNoticeProjection *out)
{
    const char *runtime;
    bool error;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->level = PTC_UI_NOTICE_SUCCESS;
    if (!model || model->view != PTC_UI_PARENT) return;

    runtime = ptc_ui_runtime_notice_summary(model);
    error = strcmp(model->result_status, "error") == 0;
    out->visible = runtime[0] || ptc_ui_operation_feedback_visible(model) ||
        ptc_ui_home_notice_expanded(model);
    if (!out->visible) return;

    if (runtime[0]) snprintf(out->summary, sizeof(out->summary), "%s", runtime);
    else if (model->message[0]) snprintf(out->summary, sizeof(out->summary), "%s", model->message);
    else if (error) snprintf(out->summary, sizeof(out->summary), ptc_ui_text(PTC_UI_T_OPERATION_INCOMPLETE));
    else if (model->waiting) snprintf(out->summary, sizeof(out->summary), ptc_ui_text(PTC_UI_T_SYNCHRONIZING_PLEASE_WAIT));
    else snprintf(out->summary, sizeof(out->summary), ptc_ui_text(PTC_UI_T_STATUS_UPDATED));

    if (error || model->disable_flag_present || model->restricted_now == 1 ||
        (model->remaining_available && model->remaining_minutes == 0 && model->unrestricted_today != 1) ||
        strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) {
        out->level = PTC_UI_NOTICE_DANGER;
    } else if (model->waiting || runtime[0]) {
        out->level = PTC_UI_NOTICE_WARNING;
    }

    if (model->feedback_detail[0]) {
        snprintf(out->details, sizeof(out->details), "%s", model->feedback_detail);
    } else if (error) {
        snprintf(out->details, sizeof(out->details),
                 ptc_ui_text(PTC_UI_T_Y));
    } else if (model->disable_flag_present) {
        snprintf(out->details, sizeof(out->details),
                 ptc_ui_text(PTC_UI_T_STOPPED_UPDATING_THE_QUOTA_SETTING_PLEASE_GO));
    } else if (model->recovery_active) {
        snprintf(out->details, sizeof(out->details),
                 ptc_ui_text(PTC_UI_T_THE_BACKGROUND_IS_RESTORING_PREVIOUS_SETTINGS_PLEASE));
    } else if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) {
        snprintf(out->details, sizeof(out->details),
                 ptc_ui_text(PTC_UI_T_PLEASE_GO_TO_SUPPORT_AND_RECOVERY_TO));
    } else if (model->restriction_enabled_available && !model->restriction_enabled) {
        snprintf(out->details, sizeof(out->details),
                 ptc_ui_text(PTC_UI_T_PLEASE_CHECK_WHETHER_NINTENDO_SYSTEM_PARENTAL_CONTROLS));
    } else if (model->temporary_unlocked_available && model->temporary_unlocked) {
        snprintf(out->details, sizeof(out->details),
                 ptc_ui_text(PTC_UI_T_TODAY_S_TIME_WILL_NOT_BE_ACCUMULATED));
    } else if (model->apply_pending_confirmation) {
        snprintf(out->details, sizeof(out->details),
                 ptc_ui_text(PTC_UI_T_THE_BACKGROUND_IS_CONFIRMING_WHETHER_THE_SETTINGS));
    } else if (model->eye_care_policy.enabled && strcmp(model->eye_care_phase, "resting") == 0) {
        snprintf(out->details, sizeof(out->details), "%s",
                 ptc_ui_text(PTC_UI_T_EYE_CARE_INDEPENDENT_RULE));
    } else if (model->dock_restriction_active) {
        snprintf(out->details, sizeof(out->details), "%s", ptc_ui_text(PTC_UI_T_DOCK_CONNECT));
    } else if (model->restricted_now == 1 ||
               (model->remaining_available && model->remaining_minutes == 0 && model->unrestricted_today != 1)) {
        snprintf(out->details, sizeof(out->details),
                 ptc_ui_text(PTC_UI_T_MESSAGE_7));
    }
    out->has_details = out->details[0] != '\0';
}

void ptc_ui_project_time_status(const PtcUiModel *model, int64_t now, PtcUiTimeProjection *out)
{
    time_t clock_value = (time_t)now;
    struct tm *local;
    int remaining;
    int total;
    int64_t age;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    snprintf(out->clock_text, sizeof(out->clock_text), "--:--");
    snprintf(out->date_text, sizeof(out->date_text), ptc_ui_text(PTC_UI_T_MESSAGE_6));
    snprintf(out->remaining_text, sizeof(out->remaining_text), ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
    snprintf(out->freshness_text, sizeof(out->freshness_text), ptc_ui_text(PTC_UI_T_AWAITING_REFRESH));
    out->state = PTC_UI_TIME_UNKNOWN;
    local = localtime(&clock_value);
    if (local) {
        int wday = (local->tm_wday >= 0 && local->tm_wday < 7) ? local->tm_wday : 0;
        snprintf(out->clock_text, sizeof(out->clock_text), "%02d:%02d",
                 local->tm_hour, local->tm_min);
        PtcUiTextArg date_args[] = {
            PTC_UI_TEXT_NUMBER("month", local->tm_mon + 1),
            PTC_UI_TEXT_NUMBER("day", local->tm_mday),
            PTC_UI_TEXT_STRING("weekday", ptc_ui_weekday_label((unsigned)wday))
        };
        (void)ptc_ui_text_format(PTC_UI_T_STATUS_DATE_NAMED, out->date_text,
                                 sizeof(out->date_text), date_args, 3);
    } else if (model && model->status_loaded && model->day_index > 0) {
        uint16_t y = 0;
        uint8_t m = 0, d = 0;
        if (ptc_date_from_day_index(model->day_index, &y, &m, &d)) {
            uint8_t w = ptc_weekday_from_day_index(model->day_index);
            PtcUiTextArg date_args[] = {
                PTC_UI_TEXT_NUMBER("month", m), PTC_UI_TEXT_NUMBER("day", d),
                PTC_UI_TEXT_STRING("weekday", ptc_ui_weekday_label(w % 7))
            };
            (void)ptc_ui_text_format(PTC_UI_T_STATUS_DATE_NAMED, out->date_text,
                                     sizeof(out->date_text), date_args, 3);
        }
    }
    if (!model) return;

    age = ptc_ui_status_age_seconds(model, now);
    if (model->waiting) {
        snprintf(out->freshness_text, sizeof(out->freshness_text), ptc_ui_text(PTC_UI_T_SYNCING));
        out->state = PTC_UI_TIME_WAITING;
    } else if (!model->status_loaded) {
        snprintf(out->remaining_text, sizeof(out->remaining_text), ptc_ui_text(PTC_UI_T_NO_STATUS_YET_PRESS_Y_TO_REFRESH));
        snprintf(out->freshness_text, sizeof(out->freshness_text),
                 model->error_code || strcmp(model->result_status, "error") == 0
                     ? (ptc_ui_text(PTC_UI_T_UNAVAILABLE)) : (ptc_ui_text(PTC_UI_T_AWAITING_REFRESH)));
    } else if (!ptc_ui_status_is_fresh(model, now)) {
        snprintf(out->freshness_text, sizeof(out->freshness_text),
                 model->error_code || strcmp(model->result_status, "error") == 0
                     ? (ptc_ui_text(PTC_UI_T_REFRESH_FAILED)) : (ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM)));
        if (age >= 0 && !(model->error_code || strcmp(model->result_status, "error") == 0)) {
            if (age < 3600) snprintf(out->freshness_text, sizeof(out->freshness_text),
                                     ptc_ui_text(PTC_UI_T_CONFIRMED_LLDM_AGO), (long long)(age / 60));
            else snprintf(out->freshness_text, sizeof(out->freshness_text),
                          ptc_ui_text(PTC_UI_T_CONFIRMED_LLDH_AGO), (long long)(age / 3600));
        }
    } else if (age <= 0) {
        snprintf(out->freshness_text, sizeof(out->freshness_text), ptc_ui_text(PTC_UI_T_JUST_UPDATED));
    } else {
        snprintf(out->freshness_text, sizeof(out->freshness_text),
                 ptc_ui_text(PTC_UI_T_UPDATED_LLDS_AGO),
                 (long long)age);
    }

    if (model->status_loaded && !ptc_ui_status_is_fresh(model, now)) {
        const char *last = model->bedtime_active && !model->bedtime_skipped ? (ptc_ui_text(PTC_UI_T_BEDTIME_2)) :
            (model->unrestricted_today == 1 ? (ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED)) :
             (model->limited_today == 1 ? (ptc_ui_text(PTC_UI_T_LIMITED)) : (ptc_ui_text(PTC_UI_T_UNKNOWN_2))));
        snprintf(out->remaining_text, sizeof(out->remaining_text),
                 ptc_ui_text(PTC_UI_T_LAST_CONFIRMED_S_PRESS_Y_TO_REFRESH), last);
    }
    if (ptc_ui_status_is_fresh(model, now)) {
        if (model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped)) {
            if (model->dock_policy.undocked_limit_enabled && !model->dock_waived_today &&
                strcmp(model->operation_mode, "undocked") == 0 && model->undocked_usage_available) {
                snprintf(out->remaining_text, sizeof(out->remaining_text),
                         ptc_ui_text(PTC_UI_T_AVAILABLE_TODAY_UNLIMITED_WITH_UNDOCKED), model->undocked_remaining_minutes);
            } else {
                snprintf(out->remaining_text, sizeof(out->remaining_text), ptc_ui_text(PTC_UI_T_AVAILABLE_TODAY_UNLIMITED));
            }
            out->progress_available = true;
            out->progress_per_mille = 1000;
            out->state = PTC_UI_TIME_UNLIMITED;
        } else if (model->remaining_available && model->remaining_minutes >= 0 &&
                   model->forecast_available && model->forecast[0].day_index == model->day_index &&
                   model->forecast[0].mode == PTC_RULE_MODE_LIMIT) {
            remaining = model->remaining_minutes;
            total = model->forecast[0].minutes;
            if (total > 0) {
                if (model->dock_policy.undocked_limit_enabled && !model->dock_waived_today &&
                    strcmp(model->operation_mode, "undocked") == 0 && model->undocked_usage_available &&
                    model->undocked_remaining_minutes < remaining) {
                    snprintf(out->remaining_text, sizeof(out->remaining_text),
                             ptc_ui_text(PTC_UI_T_AVAILABLE_TODAY_WITH_UNDOCKED), remaining, model->undocked_remaining_minutes);
                } else {
                    snprintf(out->remaining_text, sizeof(out->remaining_text),
                             ptc_ui_text(PTC_UI_T_AVAILABLE_TODAY_D_MIN), remaining);
                }
                out->progress_available = true;
                if (remaining >= total) out->progress_per_mille = 1000;
                else out->progress_per_mille = (uint16_t)(remaining * 1000 / total);
                if (remaining == 0) out->state = PTC_UI_TIME_EXHAUSTED;
                else if (remaining < 10 || (model->dock_policy.undocked_limit_enabled && !model->dock_waived_today &&
                         strcmp(model->operation_mode, "undocked") == 0 && model->undocked_usage_available && model->undocked_remaining_minutes <= 5))
                    out->state = PTC_UI_TIME_DANGER;
                else if (remaining < 30 || (model->dock_policy.undocked_limit_enabled && !model->dock_waived_today &&
                         strcmp(model->operation_mode, "undocked") == 0 && model->undocked_usage_available && model->undocked_remaining_minutes <= 15))
                    out->state = PTC_UI_TIME_REMINDER;
                else out->state = PTC_UI_TIME_NORMAL;
            }
        } else {
            snprintf(out->remaining_text, sizeof(out->remaining_text), ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
        }
    }

    if (ptc_ui_status_is_fresh(model, now) && model->eye_care_policy.enabled &&
        strcmp(model->eye_care_phase, "resting") == 0) {
        snprintf(out->remaining_text, sizeof(out->remaining_text), "%s",
                 ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING));
        out->progress_available = false;
        out->state = PTC_UI_TIME_DANGER;
    }
    if (ptc_ui_status_is_fresh(model, now) && model->dock_restriction_active &&
        !(model->bedtime_active && !model->bedtime_skipped) &&
        !(model->eye_care_policy.enabled && strcmp(model->eye_care_phase, "resting") == 0)) {
        snprintf(out->remaining_text, sizeof(out->remaining_text), "%s", ptc_ui_text(PTC_UI_T_DOCK_BLOCKED));
        out->progress_available = false;
        out->state = PTC_UI_TIME_DANGER;
    }

    /* Operational exceptions are badges over the last authoritative quota;
       they never manufacture a countdown or a zero value. */
    if (model->recovery_active) {
        out->state = PTC_UI_TIME_RECOVERY;
        snprintf(out->freshness_text, sizeof(out->freshness_text), ptc_ui_text(PTC_UI_T_ADJUST_BADGE_RECOVERING));
    } else if (strcmp(model->setup_phase, "protection") == 0 ||
               strcmp(model->setup_phase, "failed") == 0) {
        out->state = PTC_UI_TIME_PROTECTION;
        snprintf(out->freshness_text, sizeof(out->freshness_text), ptc_ui_text(PTC_UI_T_PROTECTION));
    } else if (model->disable_flag_present) {
        out->state = PTC_UI_TIME_DISABLED;
        snprintf(out->freshness_text, sizeof(out->freshness_text), ptc_ui_text(PTC_UI_T_DISABLED_3));
    } else if (model->temporary_unlocked_available && model->temporary_unlocked) {
        out->state = PTC_UI_TIME_TEMPORARY_UNLOCK;
        snprintf(out->freshness_text, sizeof(out->freshness_text), ptc_ui_text(PTC_UI_T_UNLOCKED));
    } else if (model->waiting) {
        out->state = PTC_UI_TIME_WAITING;
    }
}

void ptc_ui_format_status_age(const PtcUiModel *model, int64_t now, char *out, size_t out_size)
{
    int64_t age = ptc_ui_status_age_seconds(model, now);
    if (!out || !out_size) return;
    if (!model || !model->status_loaded) snprintf(out, out_size, ptc_ui_text(PTC_UI_T_AWAITING_REFRESH));
    else if (model->waiting) snprintf(out, out_size, ptc_ui_text(PTC_UI_T_REFRESHING_STATUS));
    else if (!ptc_ui_status_is_fresh(model, now)) snprintf(out, out_size, ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM_PLEASE_REFRESH));
    else if (age == 0) snprintf(out, out_size, ptc_ui_text(PTC_UI_T_JUST_REFRESHED));
    else if (age < 60) snprintf(out, out_size, ptc_ui_text(PTC_UI_T_LAST_REFRESHED_LLDS_AGO), (long long)age);
    else snprintf(out, out_size, ptc_ui_text(PTC_UI_T_LAST_REFRESHED_LLDM_AGO), (long long)(age / 60));
}


void ptc_ui_format_code(const char *code, char *out, size_t out_size)
{
    char grouped[10] = "____ ____";
    size_t length = code ? strlen(code) : 0;
    if (!out || !out_size) return;
    for (size_t i = 0; i < 8 && i < length; ++i) grouped[i + (i >= 4)] = code[i];
    snprintf(out, out_size, "%s", grouped);
}

const char *ptc_ui_code_failure_guidance(int error_code)
{
    switch (error_code) {
    case PTC_ERR_USED_TOKEN:
        return ptc_ui_text(PTC_UI_T_THIS_CODE_HAS_BEEN_USED_PLEASE_GENERATE);
    case PTC_ERR_DOCK_ACTIVE:
        return ptc_ui_text(PTC_UI_T_DOCK_CONNECT);
    case PTC_ERR_WRONG_DATE:
        return ptc_ui_text(PTC_UI_T_THE_CODE_DATE_IS_INCONSISTENT_WITH_THE);
    case PTC_ERR_BAD_CLOCK:
        return ptc_ui_text(PTC_UI_T_THE_HOST_DATE_CANNOT_BE_CONFIRMED_PARENTS);
    case PTC_ERR_BAD_CODE:
    case PTC_ERR_BAD_TOKEN_VERSION:
    case PTC_ERR_UNSUPPORTED_TOKEN_ACTION:
    case PTC_ERR_BAD_SIGNATURE:
        return ptc_ui_text(PTC_UI_T_THE_CODE_DOES_NOT_PASS_VERIFICATION_PLEASE);
    case PTC_ERR_MINUTES_EXCEED_LIMIT:
        return ptc_ui_text(PTC_UI_T_THE_CODE_DURATION_EXCEEDS_THE_ALLOWED_LIMIT);
    case PTC_ERR_CODE_COOLDOWN:
        return ptc_ui_text(PTC_UI_T_TOO_MANY_INPUT_ATTEMPTS_PLEASE_WAIT_AND);
    case PTC_ERR_STORAGE_READ_FAILED:
    case PTC_ERR_STORAGE_WRITE_FAILED:
        return ptc_ui_text(PTC_UI_T_READING_AND_WRITING_ARE_NOT_COMPLETED_PLEASE);
    default:
        return ptc_ui_text(PTC_UI_T_THE_EXCHANGE_IS_NOT_COMPLETED_PARENTS_PLEASE);
    }
}

void ptc_ui_format_today_mode(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || !model->status_loaded) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_AWAITING_REFRESH));
    } else if (model->eye_care_policy.enabled && strcmp(model->eye_care_phase, "resting") == 0) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING));
    } else if (model->blocked_today == 1) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_PLAY_BLOCKED));
    } else if (model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped)) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
    } else if (model->limited_today == 1) {
        snprintf(out, out_size,
                 model->remaining_available && model->remaining_minutes <= 0
                     ? (ptc_ui_text(PTC_UI_T_LIMITED_EXHAUSTED))
                     : (ptc_ui_text(PTC_UI_T_LIMITED)));
    } else {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_STATUS_UNKNOWN));
    }
}

void ptc_ui_format_quota_remaining(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || !model->status_loaded) {
        snprintf(out, out_size, "--");
    } else if (model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped)) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
    } else if (model->remaining_available && model->remaining_minutes >= 0) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_D_MIN), model->remaining_minutes);
    } else {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    }
}

void ptc_ui_format_timer_status(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || model->play_timer_enabled < 0) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_UNCONFIRMED));
    } else if (model->play_timer_enabled == 1) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_TIMING));
    } else if (model->unrestricted_today == 1) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_NO_TIMER));
    } else {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_NOT_TIMED));
    }
}

void ptc_ui_format_console_date(const PtcUiModel *model, char *out, size_t out_size)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    if (!out || out_size == 0) return;
    /* The offline code MAC binds the day index reported by status. Never fall back
       to the NRO local clock here: a guessed date would advertise a day the
       sysmodule does not accept. */
    if (!model || !model->status_loaded ||
        !ptc_date_from_day_index(model->day_index, &year, &month, &day)) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_CONSOLE_DATE_PENDING));
        return;
    }
    snprintf(out, out_size, ptc_ui_text(PTC_UI_T_CONSOLE_TODAY_04U_02U_02U),
             (unsigned int)year, (unsigned int)month, (unsigned int)day);
}

void ptc_ui_format_parent_status_summary(
    const PtcUiModel *model,
    int64_t now,
    char *out,
    size_t out_size)
{
    int64_t age;
    char remaining[64];
    char freshness[40];
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!model) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM_NO_RELIABLE_READING));
        return;
    }
    age = ptc_ui_status_age_seconds(model, now);
    if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_PROTECTION_MODE_ACTION_REQUIRED));
        return;
    }
    if (model->recovery_active) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_RECOVERY_INCOMPLETE_VIEW_DETAILS));
        return;
    }
    if (model->disable_flag_present) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_EMERGENCY_DISABLED_CONTROL_WRITING_STOPPED));
        return;
    }
    if (model->restriction_enabled_available && !model->restriction_enabled) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_NINTENDO_PARENTAL_CONTROLS_NOT_ENABLED));
        return;
    }
    if (model->temporary_unlocked_available && model->temporary_unlocked) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_SYSTEM_RESTRICTION_UNLOCKED_NO_TIMER_UNTIL_SLEEP));
        return;
    }
    if (model->apply_pending_confirmation) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_SETTINGS_AWAITING_CONFIRMATION));
        return;
    }
    if (model->waiting) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_CHECKING_CURRENT_STATUS));
        return;
    }
    if (!ptc_ui_status_is_fresh(model, now)) {
        if (age < 0) snprintf(out, out_size, ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM_NO_RELIABLE_READING));
        else if (age < 3600) snprintf(out, out_size, ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM_LAST_SUCCESS_LLDM_AGO), (long long)(age / 60));
        else if (age < 86400) snprintf(out, out_size, ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM_LAST_SUCCESS_LLDH_AGO), (long long)(age / 3600));
        else snprintf(out, out_size, ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM_LAST_SUCCESS_OVER_1));
        return;
    }
    if (model->eye_care_policy.enabled && strcmp(model->eye_care_phase, "resting") == 0) {
        ptc_ui_format_eye_care_cycle(model, now, out, out_size);
        return;
    }
    if (model->dock_restriction_active) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_DOCK_CONNECT));
        return;
    }
    if (model->restricted_now == 1 || model->blocked_today == 1 ||
        (model->remaining_available && model->remaining_minutes <= 0)) {
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_LIMIT_REACHED_TODAY_QUOTA_EXHAUSTED_JUST_SYNCED));
        return;
    }
    if (model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_AVAILABLE_TODAY_UNLIMITED));
    else if (model->remaining_available) snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_D_MIN_AVAILABLE_TODAY), model->remaining_minutes);
    else snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_AVAILABLE_TODAY_UNAVAILABLE));
    if (age <= 30) snprintf(freshness, sizeof(freshness), ptc_ui_text(PTC_UI_T_JUST_SYNCED));
    else if (age < 60) snprintf(freshness, sizeof(freshness), ptc_ui_text(PTC_UI_T_LLDS_AGO), (long long)age);
    else snprintf(freshness, sizeof(freshness), ptc_ui_text(PTC_UI_T_LLDM_AGO), (long long)(age / 60));
    snprintf(out, out_size, ptc_ui_text(PTC_UI_T_CONTROL_NORMAL_S_S), remaining, freshness);
}

void ptc_ui_format_holiday_priority_summary(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    PtcUiTextId text_id;
    if (!model) {
        text_id = PTC_UI_T_HOLIDAY_PRIORITY_PENDING;
    } else if (model->today_override_present) {
        text_id = PTC_UI_T_HOLIDAY_PRIORITY_TODAY;
    } else if (strcmp(model->rule_source, "scheduled_override") == 0) {
        text_id = PTC_UI_T_HOLIDAY_PRIORITY_SCHEDULED;
    } else if (!model->holiday_enabled) {
        text_id = PTC_UI_T_HOLIDAY_PRIORITY_DISABLED;
    } else if (!model->calendar_covered) {
        text_id = PTC_UI_T_HOLIDAY_PRIORITY_UNCOVERED;
    } else if (strcmp(model->rule_source, "statutory_holiday") == 0) {
        text_id = PTC_UI_T_HOLIDAY_PRIORITY_HOLIDAY;
    } else if (strcmp(model->rule_source, "makeup_workday") == 0) {
        text_id = PTC_UI_T_HOLIDAY_PRIORITY_MAKEUP;
    } else {
        text_id = PTC_UI_T_HOLIDAY_PRIORITY_WEEKLY;
    }
    snprintf(out, out_size, "%s", ptc_ui_text(text_id));
}
