#include "ui_state.h"
#include "ui_layout.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../common/protocol/error_code.h"
#include "../../common/rules/holiday_calendar.h"
#include "../../common/time/ptc_time.h"

void ptc_ui_open_support_guide(PtcUiModel *model)
{
    if (!model) return;
    model->selected_index = 6 + model->recent_event_count;
    model->parent_footer_focused = false;
    model->overlay = PTC_UI_OVERLAY_SUPPORT_GUIDE;
    model->support_guide_page = 0;
    snprintf(model->overlay_title, sizeof(model->overlay_title), "%s", ptc_ui_text(PTC_UI_T_SUPPORT_GUIDE));
    model->overlay_body[0] = '\0';
}

void ptc_ui_support_guide_change_page(PtcUiModel *model, int direction)
{
    if (!model || model->overlay != PTC_UI_OVERLAY_SUPPORT_GUIDE) return;
    model->support_guide_page = (model->support_guide_page + (direction > 0 ? 1 : 3)) % 4;
}

void ptc_ui_format_custom_shortcut_hint(
    const char *shortcut_label,
    char *out,
    size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, ptc_ui_text(PTC_UI_T_HOLD_ABOUT_400_MS_S_TO_OPEN),
             shortcut_label && shortcut_label[0] ? shortcut_label : ptc_ui_text(PTC_UI_T_CUSTOM_COMBINATION));
}

int ptc_ui_weekday_for_display_slot(int slot)
{
    static const int ORDER[] = {1, 2, 3, 4, 5, 6, 0};
    return slot >= 0 && slot < 7 ? ORDER[slot] : 0;
}

int ptc_ui_parent_action_count(PtcUiParentPage page)
{
    switch (page) {
    case PTC_UI_PARENT_PLAN:
        return 7;
    case PTC_UI_PARENT_GRANT:
        return 4;
    case PTC_UI_PARENT_SETTINGS:
        return 8;
    case PTC_UI_PARENT_SUPPORT:
        return 6;
    case PTC_UI_PARENT_TODAY:
        return 8;
    default:
        return 5;
    }
}

bool ptc_ui_eye_care_dirty(const PtcUiModel *model)
{
    if (!model) return false;
    return model->draft_eye_care_policy.enabled != model->eye_care_policy.enabled ||
           model->draft_eye_care_policy.play_minutes != model->eye_care_policy.play_minutes ||
           model->draft_eye_care_policy.rest_minutes != model->eye_care_policy.rest_minutes;
}

const char *ptc_ui_eye_care_plan_badge_label(const PtcUiModel *model)
{
    return ptc_ui_text(model && model->eye_care_policy.enabled
        ? PTC_UI_T_ENABLED : PTC_UI_T_DISABLED_2);
}

const char *ptc_ui_settings_status_label(const PtcUiModel *model)
{
    if (!model) return NULL;
    if (model->disable_flag_present || model->recovery_active ||
        strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) {
        return ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED);
    }
    if (model->setup_phase[0] && strcmp(model->setup_phase, "active") != 0) return ptc_ui_text(PTC_UI_T_TO_BE_COMPLETED);
    return NULL;
}

PtcUiActionState ptc_ui_settings_support_state(const PtcUiModel *model)
{
    return ptc_ui_settings_status_label(model) ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_AVAILABLE;
}

void ptc_ui_change_parent_page(PtcUiModel *model, int direction)
{
    int page;
    if (!model || direction == 0) {
        return;
    }
    page = (int)model->parent_page + (direction > 0 ? 1 : -1);
    if (page < 0) {
        page = PTC_UI_PARENT_PAGE_COUNT - 1;
    } else if (page >= PTC_UI_PARENT_PAGE_COUNT) {
        page = 0;
    }
    model->parent_page = (PtcUiParentPage)page;
    model->today_settings_origin = false;
    if (model->parent_page == PTC_UI_PARENT_PLAN) model->plan_page = PTC_UI_PLAN_PAGE_ROOT;
    if (!model->parent_footer_focused) model->selected_index = 0;
}

void ptc_ui_move_parent_selection(PtcUiModel *model, int horizontal, int vertical)
{
    int count;
    int index;
    int row;
    int row_count;
    int column;
    int target;
    if (!model) {
        return;
    }
    if (model->parent_footer_focused) {
        if (!ptc_ui_parent_status_alert_visible(model)) model->parent_footer_selection = 0;
        if (horizontal < 0 && model->parent_footer_selection > 0) {
            --model->parent_footer_selection;
        } else if (horizontal > 0 && model->parent_footer_selection < 1 &&
                   ptc_ui_parent_status_alert_visible(model)) {
            ++model->parent_footer_selection;
        }
        if (vertical < 0) {
            model->parent_footer_focused = false;
            model->selected_index = model->parent_content_selection;
        }
        return;
    }
    count = model->parent_page == PTC_UI_PARENT_PLAN &&
            model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY
        ? 8 : ptc_ui_parent_action_count(model->parent_page);
    if (count <= 0) {
        model->selected_index = 0;
        return;
    }
    index = model->selected_index;
    if (index < 0 ||
        (model->parent_page == PTC_UI_PARENT_SUPPORT
            ? index > count + model->recent_event_count
            : (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_ROOT && model->forecast_available
                ? index > 13
                : (model->parent_page == PTC_UI_PARENT_TODAY ? index > 11 :
                   (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_ROOT ? index >= 7 : index >= count))))) {
        index = 0;
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
        static const int left[8]  = {0, 1, 1, 3, 3, 4, 5, 5};
        static const int right[8] = {0, 2, 6, 4, 5, 6, 6, 7};
        static const int up[8]    = {0, 0, 0, 1, 1, 2, 2, 6};
        static const int down[8]  = {1, 3, 5, 3, 4, 5, 7, 7};
        int previous = index;
        if (horizontal < 0) index = left[index];
        else if (horizontal > 0) index = right[index];
        else if (vertical < 0) index = up[index];
        else if (vertical > 0) index = down[index];
        model->selected_index = index;
        if (index == 1 || index == 2) model->holiday_last_rule = index - 1;
        if (vertical > 0 && previous == down[previous]) {
            model->parent_content_selection = previous;
            model->parent_footer_focused = true;
            model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
        }
        return;
    }
    if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
        int event_count = model->recent_event_count;
        int max_index = 6 + event_count;
        if (index > max_index) index = 0;
        if (index >= 6) {
            if (vertical < 0) index = index == 6 ? 4 : index - 1;
            else if (vertical > 0 && index < max_index) ++index;
            else if (vertical > 0) {
                model->parent_content_selection = index;
                model->parent_footer_focused = true;
                model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
            }
        } else {
            if (horizontal < 0 && index % 2 == 1) --index;
            else if (horizontal > 0 && index % 2 == 0) ++index;
            else if (vertical < 0 && index >= 2) index -= 2;
            else if (vertical > 0 && index < 4) index += 2;
            else if (vertical > 0) index = 6;
        }
        model->selected_index = index;
        return;
    }
    if (model->parent_page == PTC_UI_PARENT_TODAY) {
        if (index >= 8) {
            if (horizontal > 0) index = (index == 11) ? 0 : (index == 8 ? 2 : (index == 9 ? 4 : 5));
            else if (vertical < 0) {
                if (index == 8) index = 11;
                else if (index > 8 && index <= 10) --index;
            }
            else if (vertical > 0) {
                if (index == 11) index = 8;
                else if (index < 10) ++index;
                else {
                    model->parent_content_selection = index;
                    model->parent_footer_focused = true;
                    model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
                }
            }
            model->selected_index = index;
            return;
        }
        if (vertical < 0) {
            if (index == 5) {
                index = 4;
            } else if (index == 7) {
                index = 6;
            } else if (index == 4) {
                index = 2;
            } else if (index == 6) {
                index = 3;
            } else if (index == 2) {
                index = 0;
            } else if (index == 3) {
                index = 1;
            }
        } else if (vertical > 0) {
            if (index == 0) {
                index = 2;
            } else if (index == 1) {
                index = 3;
            } else if (index == 2) {
                index = 4;
            } else if (index == 3) {
                index = 6;
            } else if (index == 4) {
                index = 5;
            } else if (index == 6) {
                index = 7;
            } else if (index == 5 || index == 7) {
                model->parent_content_selection = index;
                model->parent_footer_focused = true;
                model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
            }
        } else if (horizontal < 0) {
            if (index == 0) index = 11;
            else if (index == 2) index = 8;
            else if (index == 4) index = 9;
            else if (index == 5) index = 10;
            else if (index == 1) index = 0;
            else if (index == 3) index = 2;
            else if (index == 6) index = 4;
            else if (index == 7) index = 5;
        } else if (horizontal > 0) {
            if (index == 0) index = 1;
            else if (index == 2) index = 3;
            else if (index == 4) index = 6;
            else if (index == 5) index = 7;
        }
        model->selected_index = index;
        return;
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_ROOT) {
        int previous = index;
        PtcUiRect current = index < 7 ? ptc_ui_plan_card_rect(index) : ptc_ui_forecast_day_row_rect(index - 7);
        int best = index, distance = INT_MAX;
        /* Cross-column moves use the closest vertical center, including forecast rows. */
        for (int candidate = 0; candidate < (model->forecast_available ? 14 : 7); ++candidate) {
            PtcUiRect rect = candidate < 7 ? ptc_ui_plan_card_rect(candidate) : ptc_ui_forecast_day_row_rect(candidate - 7);
            int dx = (rect.x + rect.w / 2) - (current.x + current.w / 2);
            int dy = (rect.y + rect.h / 2) - (current.y + current.h / 2);
            bool eligible = horizontal ? (horizontal > 0 ? dx > 0 : dx < 0)
                : (dx == 0 && (vertical > 0 ? dy > 0 : dy < 0));
            int score = horizontal ? abs(dx) + abs(dy) * 4 : abs(dy);
            if (eligible && score < distance) { best = candidate; distance = score; }
        }
        if (vertical > 0 && best == index) {
            model->parent_content_selection = previous;
            model->parent_footer_focused = true;
            model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
        }
        model->selected_index = best;
        return;
    }
    column = index % 2;
    if (horizontal < 0 && column > 0) {
        --index;
    } else if (horizontal > 0 && column == 0 && index + 1 < count) {
        ++index;
    }
    {
        int previous_row = index / 2;
        if (vertical != 0) {
        row = index / 2;
        column = index % 2;
        row_count = (count + 1) / 2;
        row = (row + (vertical > 0 ? 1 : row_count - 1)) % row_count;
        target = row * 2 + column;
        if (target >= count) {
            target = row * 2;
        }
            index = target;
        }
        model->selected_index = index;
        if (vertical > 0 && previous_row == row_count - 1) {
            model->parent_content_selection = index;
            model->parent_footer_focused = true;
            model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
        }
    }
}

bool ptc_ui_open_today_settings(PtcUiModel *model, int index)
{
    if (!model || model->view != PTC_UI_PARENT || model->parent_page != PTC_UI_PARENT_TODAY ||
        model->parent_support_only || model->waiting || index < 8 || index > 10) return false;
    model->today_settings_origin = true;
    model->today_settings_selection = index;
    model->parent_page = PTC_UI_PARENT_PLAN;
    model->parent_footer_focused = false;
    model->selected_index = 0;
    if (index == 8) {
        if (!model->eye_care_dirty) model->draft_eye_care_policy = model->eye_care_policy;
        model->eye_care_field_focus = 0;
        model->plan_page = PTC_UI_PLAN_PAGE_EYE_CARE;
    } else if (index == 9) {
        if (!model->bedtime_dirty) model->draft_bedtime_policy = model->bedtime_policy;
        model->bedtime_switch_pending = false;
        model->bedtime_section = PTC_UI_BEDTIME_WEEKLY;
        model->bedtime_section_focused = false;
        model->bedtime_master_focused = false;
        model->plan_page = PTC_UI_PLAN_PAGE_BEDTIME;
    } else {
        if (!model->dock_dirty) model->draft_dock_policy = model->dock_policy;
        model->dock_field_focus = 0;
        model->plan_page = PTC_UI_PLAN_PAGE_DOCK;
    }
    return true;
}

bool ptc_ui_return_today_settings(PtcUiModel *model)
{
    if (!model || !model->today_settings_origin) return false;
    model->today_settings_origin = false;
    model->parent_page = PTC_UI_PARENT_TODAY;
    model->plan_page = PTC_UI_PLAN_PAGE_ROOT;
    model->selected_index = model->today_settings_selection;
    model->parent_footer_focused = false;
    return true;
}

bool ptc_ui_dock_dirty(const PtcUiModel *model)
{
    return model && (model->dock_policy.force_docked != model->draft_dock_policy.force_docked ||
        model->dock_policy.undocked_limit_enabled != model->draft_dock_policy.undocked_limit_enabled ||
        model->dock_policy.undocked_daily_minutes != model->draft_dock_policy.undocked_daily_minutes);
}

void ptc_ui_project_dock_policy(const PtcUiModel *model, const PtcDockPolicy *policy,
    int64_t now, PtcUiDockPreview *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!model || !policy || !model->dock_available || model->waiting || model->quota_refresh_failed ||
        !ptc_ui_status_is_fresh(model, now)) return;
    out->usage_known = model->undocked_usage_available && model->undocked_used_minutes >= 0;
    if (model->dock_waived_today) out->state = PTC_UI_DOCK_PREVIEW_WAIVED;
    else if (!policy->force_docked && !policy->undocked_limit_enabled) out->state = PTC_UI_DOCK_PREVIEW_OFF;
    else {
        out->remaining_known = policy->force_docked || policy->undocked_daily_minutes == 0 || out->usage_known;
        out->remaining_minutes = policy->force_docked ? 0 :
            (int)policy->undocked_daily_minutes - model->undocked_used_minutes;
        if (out->remaining_minutes < 0) out->remaining_minutes = 0;
        if (policy->force_docked || policy->undocked_daily_minutes == 0)
            out->state = PTC_UI_DOCK_PREVIEW_REQUIRED;
        else if (!out->usage_known) out->state = PTC_UI_DOCK_PREVIEW_USAGE_UNKNOWN;
        else out->state = out->remaining_minutes > 0 ? PTC_UI_DOCK_PREVIEW_ALLOWED : PTC_UI_DOCK_PREVIEW_REQUIRED;
    }
}

bool ptc_ui_dock_save_requires_hold(const PtcUiModel *model, int64_t now)
{
    const PtcDockPolicy *draft;
    if (!model) return true;
    draft = &model->draft_dock_policy;
    if (!(draft->force_docked || draft->undocked_limit_enabled) || model->dock_waived_today) return false;
    if (!ptc_ui_status_is_fresh(model, now) || !model->dock_available) return true;
    if (strcmp(model->operation_mode, "docked") == 0) return false;
    return draft->force_docked || strcmp(model->operation_mode, "undocked") != 0 ||
        (draft->undocked_limit_enabled && (!model->undocked_usage_available ||
         model->undocked_used_minutes >= draft->undocked_daily_minutes));
}

void ptc_ui_format_dock_usage(const PtcUiModel *model, int64_t now, char *out, size_t out_size)
{
    char usage[192];
    const char *mode;
    if (!out || !out_size) return;
    if (!model || !model->dock_available || !ptc_ui_status_is_fresh(model, now))
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
    else {
        mode = strcmp(model->operation_mode, "docked") == 0 ? ptc_ui_text(PTC_UI_T_DOCK_TV) :
            strcmp(model->operation_mode, "undocked") == 0 ? ptc_ui_text(PTC_UI_T_DOCK_HANDHELD) :
            ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM);
        PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("used", model->undocked_used_minutes),
            PTC_UI_TEXT_NUMBER("remaining", model->undocked_remaining_minutes)};
        if (!(model->dock_policy.force_docked || model->dock_policy.undocked_limit_enabled))
            snprintf(usage, sizeof(usage), "%s", ptc_ui_text(PTC_UI_T_DOCK_OFF));
        else if (!model->undocked_usage_available)
            snprintf(usage, sizeof(usage), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        else (void)ptc_ui_text_format(model->dock_policy.undocked_limit_enabled ?
            PTC_UI_T_DOCK_USAGE_NAMED : PTC_UI_T_DOCK_USAGE_ONLY_NAMED, usage, sizeof(usage), args, 2);
        snprintf(out, out_size, "%s / %s", mode, usage);
    }
}

void ptc_ui_format_dock_preview(const PtcUiModel *model, int64_t now, char *out, size_t out_size)
{
    if (!out || !out_size) return;
    if (!model || !model->dock_available || !ptc_ui_status_is_fresh(model, now)) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
        return;
    }
    const PtcDockPolicy *draft = &model->draft_dock_policy;
    if (model->dock_waived_today) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_DOCK_WAIVED));
    } else if (draft->force_docked) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_DOCK_FORCE_HINT));
    } else if (!draft->undocked_limit_enabled) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_DOCK_OFF));
    } else if (!model->undocked_usage_available) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_DOCK_PREVIEW_UNKNOWN));
    } else {
        int remaining = (int)draft->undocked_daily_minutes - model->undocked_used_minutes;
        PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("remaining", remaining > 0 ? remaining : 0)};
        (void)ptc_ui_text_format(PTC_UI_T_DOCK_PREVIEW_NAMED, out, out_size, args, 1);
    }
}

void ptc_ui_move_bedtime_focus(PtcUiModel *model, int horizontal, int vertical)
{
    PtcUiRect current;
    int field_count;
    int current_x;
    int current_y;
    int best_index = -1;
    int best_score = INT_MAX;

    if (!model || (horizontal == 0 && vertical == 0)) return;
    if (model->bedtime_section < PTC_UI_BEDTIME_WEEKLY ||
        model->bedtime_section > PTC_UI_BEDTIME_SCHEDULED) {
        model->bedtime_section = PTC_UI_BEDTIME_WEEKLY;
    }
    field_count = model->bedtime_section == PTC_UI_BEDTIME_WEEKLY ? 11 :
        (model->bedtime_section == PTC_UI_BEDTIME_CALENDAR ? 5 : 6);

    if (model->bedtime_master_focused) {
        if (horizontal < 0 || vertical > 0) {
            model->bedtime_master_focused = false;
            model->bedtime_section_focused = true;
        }
        return;
    }

    if (model->bedtime_section_focused) {
        if (vertical < 0) {
            model->bedtime_section_focused = false;
            model->bedtime_master_focused = true;
            return;
        }
        if (horizontal != 0) {
            int next = (int)model->bedtime_section + (horizontal > 0 ? 1 : -1);
            if (next >= PTC_UI_BEDTIME_WEEKLY && next <= PTC_UI_BEDTIME_SCHEDULED) {
                model->bedtime_section = (PtcUiBedtimeSection)next;
                model->selected_index = 0;
            } else if (horizontal > 0) {
                model->bedtime_section_focused = false;
                model->bedtime_master_focused = true;
            }
        } else if (vertical > 0) {
            PtcUiRect tab = ptc_ui_bedtime_section_rect(model->bedtime_section);
            int tab_x = tab.x + tab.w / 2;
            for (int index = 0; index < field_count; ++index) {
                PtcUiRect field = ptc_ui_bedtime_field_rect(model->bedtime_section, index);
                int dx = abs(field.x + field.w / 2 - tab_x);
                int dy = field.y - (tab.y + tab.h);
                if (dy < 0) continue;
                if (dy * 4 + dx < best_score) {
                    best_score = dy * 4 + dx;
                    best_index = index;
                }
            }
            if (best_index >= 0) {
                model->bedtime_section_focused = false;
                model->selected_index = best_index;
            }
        }
        return;
    }

    if (model->selected_index < 0 || model->selected_index >= field_count) model->selected_index = 0;
    current = ptc_ui_bedtime_field_rect(model->bedtime_section, model->selected_index);
    current_x = current.x + current.w / 2;
    current_y = current.y + current.h / 2;

    if (horizontal != 0) {
        for (int index = 0; index < field_count; ++index) {
            PtcUiRect candidate;
            int candidate_x;
            int overlap;
            int distance;
            if (index == model->selected_index) continue;
            candidate = ptc_ui_bedtime_field_rect(model->bedtime_section, index);
            candidate_x = candidate.x + candidate.w / 2;
            if ((horizontal < 0 && candidate_x >= current_x) ||
                (horizontal > 0 && candidate_x <= current_x)) continue;
            overlap = (current.y + current.h < candidate.y + candidate.h
                ? current.y + current.h : candidate.y + candidate.h) -
                (current.y > candidate.y ? current.y : candidate.y);
            if (overlap <= 0) continue;
            distance = abs(candidate_x - current_x);
            if (distance < best_score) {
                best_score = distance;
                best_index = index;
            }
        }
    } else {
        for (int index = 0; index < field_count; ++index) {
            PtcUiRect candidate;
            int candidate_x;
            int candidate_y;
            int dy;
            int score;
            if (index == model->selected_index) continue;
            candidate = ptc_ui_bedtime_field_rect(model->bedtime_section, index);
            candidate_x = candidate.x + candidate.w / 2;
            candidate_y = candidate.y + candidate.h / 2;
            if ((vertical < 0 && candidate_y >= current_y) ||
                (vertical > 0 && candidate_y <= current_y)) continue;
            dy = abs(candidate_y - current_y);
            score = dy * 4 + abs(candidate_x - current_x);
            if (score < best_score) {
                best_score = score;
                best_index = index;
            }
        }
        if (vertical < 0) {
            PtcUiRect tab = ptc_ui_bedtime_section_rect(model->bedtime_section);
            int tab_y = tab.y + tab.h / 2;
            int score = (current_y - tab_y) * 4 + abs(current_x - (tab.x + tab.w / 2));
            if (tab_y < current_y && score < best_score) best_index = -2;
        }
    }

    if (best_index >= 0) model->selected_index = best_index;
    else if (best_index == -2) model->bedtime_section_focused = true;
}

void ptc_ui_home_details_scroll(PtcUiModel *model, int pixels)
{
    (void)model;
    (void)pixels;
}

void ptc_ui_home_details_move(PtcUiModel *model, int direction)
{
    if (!model) return;
    if (model->home_details_page == 1) {
        model->home_details_focus = (model->home_details_focus == 2) ? 1 : 2;
        return;
    }
    int count = 3;
    int position = (model->home_details_focus + (direction < 0 ? -1 : 1) + count) % count;
    model->home_details_focus = position;
}

void ptc_ui_home_details_activate(PtcUiModel *model, int index)
{
    if (!model || model->overlay != PTC_UI_OVERLAY_HOME_DETAILS) return;
    if (index > 2) return;
    model->home_details_focus = index;
    if (index == 0 && model->home_details_page == 0) {
        model->home_details_page = 1;
        model->home_details_focus = 2;
    } else if (index == 0 && model->home_details_page == 1) {
        model->rules_dialog_open = true;
    }
}

void ptc_ui_home_details_back(PtcUiModel *model)
{
    if (!model) return;
    if (model->rules_dialog_open) {
        model->rules_dialog_open = false;
        return;
    }
    if (model->home_details_page == 1) {
        if (model->home_details_entry_page == 1) {
            ptc_ui_cancel_overlay(model);
        } else {
            model->home_details_page = 0;
            model->home_details_focus = 0;
        }
    } else ptc_ui_cancel_overlay(model);
}

PtcUiOperation ptc_ui_today_operation(int index)
{
    /* UI order is independent of the operation enum and request dispatch. */
    static const PtcUiOperation actions[] = {
        PTC_UI_OPERATION_SET_TODAY_LIMIT, PTC_UI_OPERATION_ADD_TODAY_MINUTES,
        PTC_UI_OPERATION_DISABLE_TODAY_LIMIT, PTC_UI_OPERATION_RESTORE_TODAY_POLICY,
        PTC_UI_OPERATION_SKIP_BEDTIME, PTC_UI_OPERATION_NONE,
        PTC_UI_OPERATION_SKIP_EYE_CARE, PTC_UI_OPERATION_WAIVE_DOCK
    };
    return index >= 0 && index < 8 ? actions[index] : PTC_UI_OPERATION_NONE;
}

bool ptc_ui_open_home_details_page(PtcUiModel *model, int page)
{
    if (!model || model->waiting || model->overlay != PTC_UI_OVERLAY_NONE ||
        (model->view != PTC_UI_CHILD &&
         !(model->view == PTC_UI_PARENT &&
           (model->parent_page == PTC_UI_PARENT_TODAY ||
            (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_ROOT))))) return false;
    /* Keep the underlying focus and execution message intact on open/close. */
    model->overlay = PTC_UI_OVERLAY_HOME_DETAILS;
    model->home_details_page = (page == 1) ? 1 : 0;
    model->home_details_entry_page = (page == 1) ? 1 : 0;
    model->home_details_focus = (page == 1) ? 2 : 0;
    model->home_details_scroll = 0;
    model->home_details_data_expanded = false;
    snprintf(model->overlay_title, sizeof(model->overlay_title), "%s",
        model->view == PTC_UI_CHILD ? ptc_ui_text(PTC_UI_T_USAGE_DETAILS) : ptc_ui_text(PTC_UI_T_TODAY_S_SCHEDULE_DETAILS));
    model->overlay_body[0] = '\0';
    return true;
}

bool ptc_ui_open_home_details(PtcUiModel *model)
{
    return ptc_ui_open_home_details_page(model, 0);
}

bool ptc_ui_open_notice_details(PtcUiModel *model)
{
    PtcUiNoticeProjection notice;
    if (!model || model->overlay != PTC_UI_OVERLAY_NONE) return false;
    ptc_ui_project_notice(model, &notice);
    if (!notice.visible || !notice.has_details) return false;
    model->overlay = PTC_UI_OVERLAY_NOTICE_DETAILS;
    model->overlay_title[0] = '\0';
    model->overlay_body[0] = '\0';
    return true;
}

void ptc_ui_weekly_leave_move(PtcUiModel *model, int direction)
{
    if (!model || direction == 0) {
        return;
    }
    model->weekly_leave_selection =
        (model->weekly_leave_selection + (direction > 0 ? 1 : 2)) % 3;
}

void ptc_ui_move_weekly_focus(PtcUiModel *model, int horizontal, int vertical)
{
    int slot;
    if (!model) return;
    if (model->selected_index < 0 || model->selected_index > 4) model->selected_index = 0;
    if (model->selected_index != 0) {
        if (vertical < 0) {
            model->selected_index = 0;
        } else if (horizontal < 0 && model->selected_index > 1) {
            --model->selected_index;
        } else if (horizontal > 0 && model->selected_index < 4) {
            ++model->selected_index;
        }
        return;
    }
    slot = model->weekly_grid_slot;
    if (slot < 0 || slot > 6) slot = 0;
    if (horizontal < 0 && slot > 0) --slot;
    else if (horizontal > 0 && slot < 6) ++slot;
    else if (vertical > 0) {
        model->selected_index = 1;
        return;
    }
    model->weekly_grid_slot = slot;
    model->weekly_last_day_slot = slot;
    model->editor_index = ptc_ui_weekday_for_display_slot(slot);
}

static int credential_selection_step(const PtcUiModel *model, int current, int direction)
{
    static const int DEVICE_ITEMS[] = {
        PTC_UI_CREDENTIAL_INPUT, PTC_UI_CREDENTIAL_RANDOM, PTC_UI_CREDENTIAL_SAVE
    };
    static const int SECRET_ITEMS[] = {
        PTC_UI_CREDENTIAL_INPUT, PTC_UI_CREDENTIAL_RANDOM, PTC_UI_CREDENTIAL_REVEAL,
        PTC_UI_CREDENTIAL_DEMO, PTC_UI_CREDENTIAL_SAVE
    };
    const int *items = model->credential_kind == 2 ? SECRET_ITEMS : DEVICE_ITEMS;
    int count = model->credential_kind == 2 ? 5 : 3;
    int index;
    for (index = 0; index < count; ++index) {
        if (items[index] == current) break;
    }
    if (index >= count) index = 0;
    if (direction < 0 && index > 0) --index;
    if (direction > 0 && index + 1 < count) ++index;
    return items[index];
}

void ptc_ui_move_overlay_selection(PtcUiModel *model, int horizontal, int vertical)
{
    int direction;
    if (!model) return;
    direction = horizontal != 0 ? horizontal : vertical;
    if (direction == 0) return;
    if (model->overlay == PTC_UI_OVERLAY_CREDENTIAL) {
        model->overlay_selection = credential_selection_step(model, model->overlay_selection, direction);
    } else if (model->overlay == PTC_UI_OVERLAY_GRANT_MANAGER) {
        int index = model->overlay_selection;
        int column;
        int row;
        if (index < 0 || index >= PTC_UI_GRANT_MANAGER_COUNT) index = 0;
        column = index % 2;
        row = index / 2;
        if (horizontal < 0 && column > 0) --index;
        else if (horizontal > 0 && column == 0 && index + 1 < PTC_UI_GRANT_MANAGER_COUNT) ++index;
        if (vertical != 0) {
            row = index / 2;
            column = index % 2;
            row = (row + (vertical > 0 ? 1 : 2)) % 3;
            index = row * 2 + column;
            if (index >= PTC_UI_GRANT_MANAGER_COUNT) index = row * 2;
        }
        model->overlay_selection = index;
    } else if (model->overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
        int selection = model->overlay_selection;
        if (selection != PTC_UI_GRANT_LOCAL_ADJUST_FIRST &&
            selection != PTC_UI_GRANT_LOCAL_GENERATE &&
            selection != PTC_UI_GRANT_LOCAL_BACK) {
            selection = PTC_UI_GRANT_LOCAL_GENERATE;
        }
        if (selection == PTC_UI_GRANT_LOCAL_ADJUST_FIRST) {
            if (vertical > 0) selection = PTC_UI_GRANT_LOCAL_GENERATE;
        } else if (selection == PTC_UI_GRANT_LOCAL_GENERATE) {
            if (vertical < 0) selection = PTC_UI_GRANT_LOCAL_ADJUST_FIRST;
            else if (vertical > 0) selection = PTC_UI_GRANT_LOCAL_BACK;
        } else if (vertical < 0) {
            selection = PTC_UI_GRANT_LOCAL_GENERATE;
        }
        (void)horizontal;
        model->overlay_selection = selection;
    }
}
