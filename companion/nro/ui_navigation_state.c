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
        return 6;
    case PTC_UI_PARENT_GRANT:
        return 4;
    case PTC_UI_PARENT_SETTINGS:
        return 7;
    case PTC_UI_PARENT_SUPPORT:
        return 6;
    case PTC_UI_PARENT_TODAY:
        return 7;
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
                ? index >= count + 7
                : index >= count))) {
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
        if (vertical < 0) {
            if (index == 5) {
                index = (model->parent_content_selection == 6) ? 6 : 4;
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
            } else if (index == 4 || index == 6) {
                index = 5;
            } else if (index == 5) {
                model->parent_content_selection = 5;
                model->parent_footer_focused = true;
                model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
            }
        } else if (horizontal < 0) {
            if (index == 1) index = 0;
            else if (index == 3) index = 2;
            else if (index == 6) index = 4;
        } else if (horizontal > 0) {
            if (index == 0) index = 1;
            else if (index == 2) index = 3;
            else if (index == 4) index = 6;
        }
        model->selected_index = index;
        return;
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_ROOT) {
        static const int left_target[6] = {0, 1, 2, 0, 1, 2};
        static const int right_target[6] = {3, 4, 5, 3, 4, 5};
        int previous = index;
        if (index >= 6 && index <= 12) {
            if (horizontal < 0) {
                index = index <= 7 ? 3 : (index <= 9 ? 4 : 5);
            } else if (vertical < 0) {
                if (index > 6) --index;
            } else if (vertical > 0) {
                if (index < 12) ++index;
                else {
                    model->parent_content_selection = previous;
                    model->parent_footer_focused = true;
                    model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
                }
            }
        } else {
            if (horizontal < 0 && index >= 3) {
                index = left_target[index];
            } else if (horizontal > 0) {
                if (index < 3) {
                    index = right_target[index];
                } else if (model->forecast_available) {
                    index = index == 3 ? 6 : (index == 4 ? 8 : 10);
                }
            } else if (vertical < 0) {
                if (index == 1 || index == 2 || index == 4 || index == 5) --index;
            } else if (vertical > 0) {
                if (index == 0 || index == 1 || index == 3 || index == 4) ++index;
                else {
                    model->parent_content_selection = previous;
                    model->parent_footer_focused = true;
                    model->parent_footer_selection = ptc_ui_parent_status_alert_visible(model) ? 1 : 0;
                }
            }
        }
        model->selected_index = index;
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
