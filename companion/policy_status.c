#include "policy_status.h"
#include "../common/time/ptc_time.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

bool ptc_policy_local_clock(long long now, int *day_index, int *second_of_day)
{
    time_t value = (time_t)now;
    struct tm *local = localtime(&value);
    uint16_t day;
    if (!local || !day_index || !second_of_day ||
        !ptc_day_index_from_date((uint16_t)(local->tm_year + 1900),
            (uint8_t)(local->tm_mon + 1), (uint8_t)local->tm_mday, &day)) return false;
    *day_index = day;
    *second_of_day = local->tm_hour * 3600 + local->tm_min * 60 + local->tm_sec;
    return true;
}

bool ptc_policy_snapshot_clock(const PtcCompanionResultSummary *s,
    uint64_t elapsed, int *day, int *second)
{
    if (!day || !second) return false;
    *day = *second = -1;
    if (!s || !s->valid || !s->local_clock_available || s->completed_at <= 0 ||
        s->day_index < 0 || s->day_index > 65535 || elapsed >= 86400 ||
        s->local_second_of_day < 0 || s->local_second_of_day >= 86400 ||
        (uint64_t)s->local_second_of_day + elapsed >= 86400) return false;
    *day = s->day_index;
    *second = s->local_second_of_day + (int)elapsed;
    return true;
}

static void message(PtcPolicyDisplay *out, PtcPolicyDisplayState state,
    PtcUiTextId badge, PtcUiTextId text)
{
    out->state = state;
    out->badge = badge;
    out->minutes = out->time_minute = -1;
    snprintf(out->compact, sizeof(out->compact), "%s", ptc_ui_text(text));
    snprintf(out->detail, sizeof(out->detail), "%s", ptc_ui_text(text));
}

static bool valid_window(int day, int start, int end, unsigned long long id)
{
    return day >= 0 && day < 65535 && start < 1440 && end >= 0 && start > end &&
        id == (((unsigned long long)day << 16) | (unsigned)start);
}

static void bedtime_project(const PtcCompanionResultSummary *s, int day,
    int second, PtcPolicyDisplay *out)
{
    int start_day, start, end, target;
    unsigned long long id;
    bool active = s->bedtime_active;
    if (!s->bedtime_available) return;
    if (!s->bedtime_enabled) {
        message(out, PTC_POLICY_OFF, PTC_UI_T_DISABLED_2, PTC_UI_T_CURRENTLY_OFF);
        return;
    }
    start_day = active ? s->bedtime_start_day_index : s->bedtime_next_start_day_index;
    start = active ? s->bedtime_start_minute : s->bedtime_next_start_minute;
    end = active ? s->bedtime_end_minute : s->bedtime_next_end_minute;
    id = active ? s->bedtime_window_instance_id : s->bedtime_next_window_instance_id;
    /* Skip belongs to an instance, not an arbitrary future window. */
    bool skipped_today = s->bedtime_skipped_window_available &&
        valid_window(s->bedtime_skipped_start_day_index, s->bedtime_skipped_start_minute,
            s->bedtime_skipped_end_minute, s->bedtime_skipped_window_instance_id) &&
        (s->bedtime_skipped_start_day_index == day ||
         (s->bedtime_skipped_start_day_index == day - 1 &&
          second < s->bedtime_skipped_end_minute * 60));
    if (!active && (!s->bedtime_next_available || start_day > day)) {
        message(out, skipped_today ? PTC_POLICY_SKIPPED : PTC_POLICY_NONE_TODAY,
            skipped_today ? PTC_UI_T_SKIPPED : PTC_UI_T_ENABLED,
            skipped_today ? PTC_UI_T_POLICY_BED_SKIPPED : PTC_UI_T_POLICY_BED_NONE);
        return;
    }
    if (!valid_window(start_day, start, end, id) ||
        (active ? (start_day != day && start_day != day - 1) : start_day != day)) return;
    if ((active && s->bedtime_skipped) ||
        (s->bedtime_skipped_window_available && s->bedtime_skipped_window_instance_id == id)) {
        message(out, PTC_POLICY_SKIPPED, PTC_UI_T_SKIPPED, PTC_UI_T_POLICY_BED_SKIPPED);
        return;
    }
    /* Bedtime windows always end on the next civil day (start > end). */
    target = active ? (start_day + 1 - day) * 86400 + end * 60 : start * 60;
    if (target <= second || (active && start_day == day && second < start * 60)) {
        message(out, PTC_POLICY_WAITING, PTC_UI_T_TO_BE_CONFIRMED, PTC_UI_T_POLICY_BOUNDARY);
        return;
    }
    out->state = active ? PTC_POLICY_ACTIVE : PTC_POLICY_PENDING;
    out->badge = active ? PTC_UI_T_RESTRICTED : PTC_UI_T_ENABLED;
    out->minutes = (target - second + 59) / 60;
    out->time_minute = active ? end : start;
    char clock[24], duration[64];
    snprintf(clock, sizeof(clock), "%s%02d:%02d", active && start_day == day ?
        ptc_ui_text(PTC_UI_T_POLICY_NEXT_DAY) : "", out->time_minute / 60, out->time_minute % 60);
    ptc_ui_format_hours_minutes(out->minutes / 60, out->minutes % 60, duration, sizeof(duration));
    PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("time", clock), PTC_UI_TEXT_STRING("duration", duration)};
    ptc_ui_text_format(active ? PTC_UI_T_POLICY_BED_END : PTC_UI_T_POLICY_BED_START,
        out->detail, sizeof(out->detail), args, 2);
    PtcUiTextArg short_args[] = {PTC_UI_TEXT_NUMBER("hours", out->minutes / 60),
        PTC_UI_TEXT_NUMBER("minutes", out->minutes % 60)};
    ptc_ui_text_format(out->minutes < 60 ? PTC_UI_T_POLICY_DURATION_MIN : PTC_UI_T_POLICY_DURATION_HM,
        duration, sizeof(duration), short_args, 2);
    ptc_ui_text_format(active ? PTC_UI_T_POLICY_BED_END_SHORT : PTC_UI_T_POLICY_BED_START_SHORT,
        out->compact, sizeof(out->compact), args, 2);
}

static void dock_project(const PtcCompanionResultSummary *s, PtcPolicyStatus *out)
{
    PtcPolicyDisplay *dock = &out->dock;
    if (!s->dock_available || !s->dock_policy_available) return;
    bool tv = strcmp(s->operation_mode, "docked") == 0;
    bool handheld = strcmp(s->operation_mode, "undocked") == 0;
    out->mode = tv ? PTC_UI_T_DOCK_TV_BADGE : handheld ?
        PTC_UI_T_DOCK_HANDHELD_BADGE : PTC_UI_T_STATUS_TO_CONFIRM;
    if (!tv && !handheld) return;
    if (!s->force_docked && !s->undocked_limit_enabled) {
        message(dock, PTC_POLICY_OFF, PTC_UI_T_DISABLED_2, PTC_UI_T_DOCK_CARD_OFF);
    } else if (s->dock_waived_today) {
        message(dock, PTC_POLICY_WAIVED, PTC_UI_T_SKIPPED, PTC_UI_T_DOCK_CARD_WAIVED);
    } else if (s->force_docked || s->undocked_daily_minutes == 0) {
        message(dock, s->dock_restriction_active ? PTC_POLICY_ACTIVE : PTC_POLICY_TV_ONLY,
            s->dock_restriction_active ? PTC_UI_T_RESTRICTED : PTC_UI_T_DOCK_FORCE,
            PTC_UI_T_DOCK_CARD_FORCE);
        dock->minutes = 0;
    } else if (s->undocked_usage_available && s->undocked_used_minutes >= 0 &&
        s->undocked_remaining_minutes >= 0 && s->undocked_daily_minutes > 0 &&
        s->undocked_daily_minutes <= 1440 && s->undocked_used_minutes <= 1440 &&
        s->undocked_remaining_minutes <= s->undocked_daily_minutes) {
        int remaining = s->undocked_remaining_minutes;
        dock->state = s->dock_restriction_active ? PTC_POLICY_ACTIVE :
            remaining == 0 ? PTC_POLICY_EXHAUSTED : PTC_POLICY_AVAILABLE;
        dock->badge = s->dock_restriction_active ? PTC_UI_T_RESTRICTED : PTC_UI_T_DOCK_BADGE_QUOTA;
        dock->minutes = remaining;
        PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("minutes", remaining)};
        ptc_ui_text_format(PTC_UI_T_POLICY_DOCK_LEFT, dock->compact, sizeof(dock->compact), args, 1);
        ptc_ui_text_format(remaining ? PTC_UI_T_POLICY_DOCK_COUNTDOWN :
            tv ? PTC_UI_T_POLICY_DOCK_EMPTY_TV : PTC_UI_T_POLICY_DOCK_EMPTY,
            dock->detail, sizeof(dock->detail), args, 1);
    }
    PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("mode", ptc_ui_text(out->mode)),
        PTC_UI_TEXT_NUMBER("limit", s->force_docked ? 0 : s->undocked_daily_minutes),
        PTC_UI_TEXT_NUMBER("used", s->undocked_used_minutes)};
    if (dock->state == PTC_POLICY_AVAILABLE || dock->state == PTC_POLICY_EXHAUSTED ||
        (dock->state == PTC_POLICY_ACTIVE && !s->force_docked && s->undocked_daily_minutes > 0))
        ptc_ui_text_format(PTC_UI_T_POLICY_DOCK_RULE, out->dock_rule, sizeof(out->dock_rule), args, 3);
    else snprintf(out->dock_rule, sizeof(out->dock_rule), "%s", ptc_ui_text(out->mode));
}

void ptc_policy_status_project(const PtcCompanionResultSummary *s,
    bool fresh, int day, int second, PtcPolicyStatus *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    message(&out->bedtime, PTC_POLICY_UNKNOWN, PTC_UI_T_TO_BE_CONFIRMED, PTC_UI_T_POLICY_PENDING);
    message(&out->dock, PTC_POLICY_UNKNOWN, PTC_UI_T_TO_BE_CONFIRMED, PTC_UI_T_POLICY_PENDING);
    out->mode = PTC_UI_T_STATUS_TO_CONFIRM;
    if (!s || !s->valid || s->day_index != day || day < 0 || day > 65535 ||
        second < 0 || second >= 86400) return;
    /* A known window remains useful as a countdown after refresh expires.
       Its boundary still waits for the backend; usage counters require freshness. */
    if (fresh || s->bedtime_enabled) bedtime_project(s, day, second, &out->bedtime);
    if (fresh) dock_project(s, out);
}
