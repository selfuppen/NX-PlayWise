#include "ui_state.h"
#include "ui_layout.h"
#include "ui_layout_internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../file_protocol.h"
#include "../../common/protocol/error_code.h"
#include "../../common/time/ptc_time.h"
#include "../../third_party/cjson/cJSON.h"

static bool rect_contains_row_cell(PtcUiRect rect, int cell_height, int x, int y)
{
    if (cell_height > rect.h) rect.h = cell_height;
    return ptc_ui_rect_contains(rect, x, y);
}

static bool rect_contains_with_padding(PtcUiRect rect, int padding, int x, int y)
{
    rect.x -= padding;
    rect.y -= padding;
    rect.w += padding * 2;
    rect.h += padding * 2;
    return ptc_ui_rect_contains(rect, x, y);
}

static PtcUiHit make_hit(PtcUiHitKind kind, int index)
{
    PtcUiHit hit;
    hit.kind = kind;
    hit.index = index;
    return hit;
}

static PtcUiHit hit_test_overlay(const PtcUiModel *model, int x, int y)
{
    int i;
    if (model->waiting && model->overlay == PTC_UI_OVERLAY_SCHEDULED)
        return make_hit(PTC_UI_HIT_NONE, 0);
    if (model->overlay == PTC_UI_OVERLAY_HOLIDAY_CALENDAR) {
        for (i = 0; i < 3; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_holiday_page_action_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_HOLIDAY_PAGE_ACTION, i);
            }
        }
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->overlay == PTC_UI_OVERLAY_HOME_DETAILS || model->overlay == PTC_UI_OVERLAY_NOTICE_DETAILS || model->overlay == PTC_UI_OVERLAY_DAY_DECISION) {
        return ptc_ui_rect_contains(ptc_ui_cancel_rect(model->overlay), x, y)
            ? make_hit(PTC_UI_HIT_OVERLAY_CANCEL, 0) : make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->overlay == PTC_UI_OVERLAY_SOFTWARE_INFO) {
        if (ptc_ui_rect_contains(ptc_ui_confirm_rect(model->overlay), x, y))
            return make_hit(PTC_UI_HIT_OVERLAY_CONFIRM, 0);
        if (model->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING &&
            ptc_ui_rect_contains(ptc_ui_cancel_rect(model->overlay), x, y))
            return make_hit(PTC_UI_HIT_OVERLAY_CANCEL, 0);
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->overlay != PTC_UI_OVERLAY_ALBUM_MANAGER &&
        ptc_ui_rect_contains(ptc_ui_confirm_rect(model->overlay), x, y)) {
        return make_hit(PTC_UI_HIT_OVERLAY_CONFIRM, 0);
    }
    if (model->overlay != PTC_UI_OVERLAY_CREDENTIAL_LEAVE &&
        ptc_ui_rect_contains(ptc_ui_cancel_rect(model->overlay), x, y)) {
        return make_hit(PTC_UI_HIT_OVERLAY_CANCEL, 0);
    }
    if ((model->overlay == PTC_UI_OVERLAY_WEEKLY_LEAVE ||
         model->overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE ||
         model->overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE ||
         model->overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) &&
        ptc_ui_rect_contains(ptc_ui_discard_rect(model->overlay), x, y)) {
        return make_hit(PTC_UI_HIT_OVERLAY_DISCARD, 0);
    }
    switch (model->overlay) {
    case PTC_UI_OVERLAY_REDEMPTION_HISTORY:
    case PTC_UI_OVERLAY_ACTIVITY_HISTORY:
        if (ptc_ui_rect_contains(ptc_ui_redemption_history_prev_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_HISTORY_PREV, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_redemption_history_next_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_HISTORY_NEXT, 0);
        }
        break;
    case PTC_UI_OVERLAY_BEDTIME_WINDOW:
    case PTC_UI_OVERLAY_BEDTIME_SPECIAL: {
        for (i = 0; i < 3; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_bedtime_overlay_field_rect(model->overlay, i), x, y))
                return make_hit(PTC_UI_HIT_BEDTIME_OVERLAY_FIELD, i);
        }
        bool presets_enabled = model->overlay == PTC_UI_OVERLAY_BEDTIME_WINDOW;
        if (model->overlay == PTC_UI_OVERLAY_BEDTIME_SPECIAL) {
            const PtcBedtimeSpecialRule *rule = model->bedtime_special_kind == 0
                ? &model->draft_bedtime_policy.holiday_rule
                : (model->bedtime_special_kind == 1
                    ? &model->draft_bedtime_policy.makeup_workday_rule
                    : &model->draft_bedtime_policy.scheduled_override.rule);
            presets_enabled = rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM;
        }
        if (presets_enabled) {
            for (i = 0; i < 5; ++i) {
                if (ptc_ui_rect_contains(ptc_ui_bedtime_preset_rect(model->overlay, i), x, y))
                    return make_hit(PTC_UI_HIT_BEDTIME_PRESET, i);
            }
        }
        break;
    }
    case PTC_UI_OVERLAY_BEDTIME: {
        PtcUiRect dialog = ptc_ui_dialog_for(model->overlay);
        for (i = 0; i < 4; ++i) {
            PtcUiRect row = {dialog.x + 42, dialog.y + 118 + i * 70, dialog.w - 84, 56};
            if (ptc_ui_rect_contains(row, x, y)) return make_hit(PTC_UI_HIT_BEDTIME_OVERLAY_FIELD, i);
        }
        break;
    }
    case PTC_UI_OVERLAY_QUICK_ADD:
        for (i = 0; i < 4; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_quick_add_option_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_QUICK_ADD_OPTION, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_SCHEDULED:
        for (i = 0; i < 4; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_scheduled_field_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_SCHEDULED_FIELD, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_AUTONOMY:
        for (i = 0; i < 4; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_autonomy_option_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_AUTONOMY_OPTION, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_MINUTE_EDITOR:
        for (i = 0; i < 2; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_minute_editor_field_rect((PtcUiDurationField)i), x, y)) {
                return make_hit(PTC_UI_HIT_DURATION_FIELD, i);
            }
        }
        for (i = 0; i < 2; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_minute_editor_quick_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_NUMPAD_QUICK, i);
            }
        }
        for (i = 0; i < 12; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_minute_editor_key_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_NUMPAD_KEY, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_PIN:
        for (i = 0; i < 10; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_pin_key_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_PIN_KEY, i);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_pin_backspace_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_PIN_BACKSPACE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_pin_confirm_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_PIN_CONFIRM, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_pin_cancel_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_PIN_CANCEL, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_pin_keyboard_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_PIN_KEYBOARD, 0);
        }
        break;
    case PTC_UI_OVERLAY_WEEKLY_BULK:
        for (i = 0; i < 2; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_weekly_bulk_target_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_BULK_TARGET, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_ALBUM_MANAGER:
        if (ptc_ui_rect_contains(ptc_ui_album_refresh_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_ALBUM_REFRESH, 0);
        }
        for (i = 0; i < 2; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_album_action_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_ALBUM_ACTION, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_MINUTES:
        if (ptc_ui_rect_contains(ptc_ui_minutes_value_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_MINUTES_VALUE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_minutes_dec_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_MINUTES_DEC, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_minutes_inc_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_MINUTES_INC, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_minutes_dec_large_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_MINUTES_DEC_LARGE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_minutes_inc_large_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_MINUTES_INC_LARGE, 0);
        }
        break;
    case PTC_UI_OVERLAY_WEEKLY:
        for (i = 0; i < 7; ++i) {
            int weekday = ptc_ui_weekday_for_display_slot(i);
            if (model->draft_week[weekday].mode == PTC_RULE_MODE_LIMIT &&
                ptc_ui_rect_contains(ptc_ui_weekly_day_minutes_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_MIN_INPUT, weekday);
            }
            if (ptc_ui_rect_contains(ptc_ui_weekly_day_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_DAY, weekday);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_mode_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MODE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_min_up_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MIN_UP, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_min_down_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MIN_DOWN, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_min_dec_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MIN_DEC, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_min_inc_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MIN_INC, 0);
        }
        if (model->draft_week[model->editor_index].mode == PTC_RULE_MODE_LIMIT &&
            ptc_ui_rect_contains(ptc_ui_weekly_min_input_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_WEEKLY_MIN_INPUT, model->editor_index);
        }
        break;
    case PTC_UI_OVERLAY_NUMPAD:
        if (model->numpad_purpose == PTC_UI_NUMPAD_MINUTES ||
            model->numpad_purpose == PTC_UI_NUMPAD_WEEKLY_MINUTES ||
            model->numpad_purpose == PTC_UI_NUMPAD_HOLIDAY_MINUTES ||
            model->numpad_purpose == PTC_UI_NUMPAD_MAKEUP_MINUTES) {
            for (i = 0; i < 4; ++i) {
                if (ptc_ui_rect_contains(ptc_ui_numpad_quick_rect(i), x, y)) {
                    return make_hit(PTC_UI_HIT_NUMPAD_QUICK, i);
                }
            }
        }
        for (i = 0; i < 12; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_numpad_key_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_NUMPAD_KEY, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_CREDENTIAL:
        if (ptc_ui_rect_contains(ptc_ui_credential_input_rect(), x, y)) return make_hit(PTC_UI_HIT_CREDENTIAL_INPUT, 0);
        if (ptc_ui_rect_contains(ptc_ui_credential_random_rect(), x, y)) return make_hit(PTC_UI_HIT_CREDENTIAL_RANDOM, 0);
        if (model->credential_kind == 2 && ptc_ui_rect_contains(ptc_ui_credential_reveal_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CREDENTIAL_REVEAL, 0);
        }
        if (model->credential_kind == 2 && ptc_ui_rect_contains(ptc_ui_credential_demo_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CREDENTIAL_DEMO, 0);
        }
        break;
    case PTC_UI_OVERLAY_GRANT_MANAGER:
        for (i = 0; i < PTC_UI_GRANT_MANAGER_COUNT; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_grant_manager_card_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_GRANT_MANAGER_CARD, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_GRANT_LOCAL:
        for (i = 0; i < 1; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_grant_adjust_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_GRANT_ADJUST, i);
            }
        }
        if (!model->waiting && ptc_ui_rect_contains(ptc_ui_grant_generate_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_GRANT_GENERATE, 0);
        }
        break;
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
        for (i = 0; i < PTC_UI_SHORTCUT_PRESET_COUNT; ++i) {
            if (rect_contains_row_cell(ptc_ui_shortcut_option_rect(i), 40, x, y)) {
                return make_hit(PTC_UI_HIT_SHORTCUT_OPTION, i);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_shortcut_disable_rect(), x, y)) return make_hit(PTC_UI_HIT_SHORTCUT_DISABLE, 0);
        if (ptc_ui_rect_contains(ptc_ui_shortcut_hint_rect(), x, y)) return make_hit(PTC_UI_HIT_SHORTCUT_HINT, 0);
        break;
    case PTC_UI_OVERLAY_THEME:
        for (i = 0; i < 3; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_theme_option_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_THEME_OPTION, i);
            }
        }
        break;
    case PTC_UI_OVERLAY_QR:
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
    case PTC_UI_OVERLAY_CREDENTIAL_LEAVE:
    case PTC_UI_OVERLAY_CODE_RESULT:
    case PTC_UI_OVERLAY_AUTH_ERROR:
    case PTC_UI_OVERLAY_SOFTWARE_INFO:
    case PTC_UI_OVERLAY_HOLIDAY_LEAVE:
    case PTC_UI_OVERLAY_SUPPORT_EVENT:
        break;
    case PTC_UI_OVERLAY_CONFIRM:
    case PTC_UI_OVERLAY_NONE:
    default:
        break;
    }
    /* Overlay is modal: taps on empty space do nothing, never fall through. */
    return make_hit(PTC_UI_HIT_NONE, 0);
}

PtcUiHit ptc_ui_hit_test(const PtcUiModel *model, int x, int y)
{
    int i;
    int count;
    if (!model) {
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->overlay != PTC_UI_OVERLAY_NONE) {
        return hit_test_overlay(model, x, y);
    }
    {
        PtcUiNoticeProjection notice;
        ptc_ui_project_notice(model, &notice);
        if (notice.visible && notice.has_details &&
            ptc_ui_rect_contains(ptc_ui_notice_details_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_NOTICE_DETAILS, 0);
        }
    }
    if ((model->view == PTC_UI_CHILD ||
         (model->view == PTC_UI_PARENT && model->parent_page == PTC_UI_PARENT_TODAY)) &&
        ptc_ui_rect_contains(ptc_ui_home_details_rect(model->view == PTC_UI_PARENT), x, y)) {
        return model->waiting ? make_hit(PTC_UI_HIT_NONE, 0) : make_hit(PTC_UI_HIT_HOME_DETAILS, 0);
    }
    if (model->view == PTC_UI_CHILD) {
        if (!model->disable_flag_present && !model->waiting && ptc_ui_rect_contains(ptc_ui_child_submit_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_SUBMIT_CODE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_child_refresh_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_REFRESH, 0);
        }
        if (model->daily_buffer_available && !model->disable_flag_present && !model->waiting &&
            ptc_ui_rect_contains(ptc_ui_child_buffer_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_BUFFER, 0);
        }
        if (!model->disable_flag_present && !model->waiting && ptc_ui_rect_contains(ptc_ui_child_footer_rect(0), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_SUBMIT_CODE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_child_footer_rect(1), x, y)) {
            if (model->show_parent_shortcut_hint && model->custom_shortcut_enabled) {
                return make_hit(PTC_UI_HIT_CHILD_PARENT, 0);
            }
            return make_hit(PTC_UI_HIT_NONE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_child_footer_rect(2), x, y)) {
            return make_hit(PTC_UI_HIT_CHILD_EXIT, 0);
        }
        /* The parent area stays hidden; touch never exposes it. */
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->view == PTC_UI_SETUP) {
        int step = model->setup_step > 0 ? model->setup_step : PTC_UI_SETUP_SHORTCUT;
        if (ptc_ui_rect_contains(ptc_ui_setup_back_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_SETUP_BACK, 0);
        }
        if (step == PTC_UI_SETUP_SHORTCUT) {
            for (i = 0; i < PTC_UI_SHORTCUT_PRESET_COUNT; ++i) {
                if (rect_contains_row_cell(ptc_ui_setup_shortcut_card_rect(i), 34, x, y)) {
                    return make_hit(PTC_UI_HIT_SETUP_SHORTCUT_CARD, i);
                }
            }
        } else if (step == PTC_UI_SETUP_PIN &&
                   ptc_ui_rect_contains(ptc_ui_setup_pin_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_SETUP_PIN, 0);
        } else if (step == PTC_UI_SETUP_THEME) {
            for (i = 0; i < 3; ++i) {
                if (ptc_ui_rect_contains(ptc_ui_setup_theme_rect(i), x, y)) {
                    return make_hit(PTC_UI_HIT_SETUP_THEME_OPTION, i);
                }
            }
        } else if (step == PTC_UI_SETUP_ZONE) {
            if (ptc_ui_rect_contains(ptc_ui_setup_zone_rect(0), x, y)) {
                return make_hit(PTC_UI_HIT_SETUP_CHILD_ZONE, 0);
            }
            if (ptc_ui_rect_contains(ptc_ui_setup_zone_rect(1), x, y)) {
                return make_hit(PTC_UI_HIT_SETUP_PARENT_ZONE, 1);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_setup_primary_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_SETUP_PRIMARY, 0);
        }
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->view == PTC_UI_ERROR) {
        if (ptc_ui_rect_contains(ptc_ui_error_retry_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_ERROR_RETRY, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_error_back_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_ERROR_BACK, 0);
        }
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (!(model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page != PTC_UI_PLAN_PAGE_ROOT)) {
        for (i = 0; i < PTC_UI_PARENT_PAGE_COUNT; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_parent_tab_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_PARENT_TAB, i);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(0), x, y)) {
            return make_hit(PTC_UI_HIT_PARENT_PREV_PAGE, 0);
        }
        if (ptc_ui_rect_contains(ptc_ui_parent_footer_rect(1), x, y)) {
            return make_hit(PTC_UI_HIT_PARENT_NEXT_PAGE, 0);
        }
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page != PTC_UI_PLAN_PAGE_ROOT &&
        ptc_ui_rect_contains(ptc_ui_advanced_back_rect(), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_BACK, 0);
    }
    bool plan_subpage = model->parent_page == PTC_UI_PARENT_PLAN &&
        model->plan_page != PTC_UI_PLAN_PAGE_ROOT;
    if (ptc_ui_rect_contains(plan_subpage ? ptc_ui_parent_subpage_footer_rect(0) :
                             ptc_ui_parent_footer_rect(2), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_BACK, 0);
    }
    if (ptc_ui_rect_contains(plan_subpage ? ptc_ui_parent_subpage_footer_rect(1) :
                             ptc_ui_parent_footer_rect(3), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_REFRESH, 0);
    }
    if (ptc_ui_parent_status_alert_visible(model) &&
        !ptc_ui_operation_feedback_visible(model) &&
        ptc_ui_rect_contains(ptc_ui_parent_footer_rect(4), x, y)) {
        return make_hit(PTC_UI_HIT_PARENT_STATUS, 0);
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_WEEKLY) {
        for (i = 0; i < 7; ++i) {
            int weekday = ptc_ui_weekday_for_display_slot(i);
            if (ptc_ui_rect_contains(ptc_ui_weekly_day_mode_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_MODE, weekday);
            }
            if (ptc_ui_rect_contains(ptc_ui_weekly_day_minutes_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_MIN_INPUT, weekday);
            }
            if (ptc_ui_rect_contains(ptc_ui_weekly_day_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_WEEKLY_DAY, weekday);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_weekly_page_mode_rect(), x, y)) return make_hit(PTC_UI_HIT_WEEKLY_MODE, model->editor_index);
        if (ptc_ui_rect_contains(ptc_ui_weekly_save_rect(), x, y)) return make_hit(PTC_UI_HIT_WEEKLY_SAVE, 0);
        if (ptc_ui_rect_contains(ptc_ui_weekly_discard_rect(), x, y)) return make_hit(PTC_UI_HIT_WEEKLY_DISCARD, 0);
        if (ptc_ui_rect_contains(ptc_ui_weekly_bulk_rect(), x, y)) return make_hit(PTC_UI_HIT_WEEKLY_BULK, 0);
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
        if (rect_contains_with_padding(ptc_ui_holiday_enable_rect(), 4, x, y)) {
            return make_hit(PTC_UI_HIT_HOLIDAY_ENABLE, 0);
        }
        for (i = 0; i < 2; ++i) {
            if (rect_contains_with_padding(ptc_ui_holiday_mode_rect(i), 4, x, y)) {
                return make_hit(PTC_UI_HIT_HOLIDAY_MODE, i);
            }
            if (ptc_ui_rect_contains(ptc_ui_holiday_minutes_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_HOLIDAY_MINUTES, i);
            }
        }
        if (ptc_ui_rect_contains(ptc_ui_holiday_calendar_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_HOLIDAY_CALENDAR, 0);
        }
        for (i = 0; i < 6; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_holiday_card_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_PARENT_CARD, i);
            }
        }
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_BEDTIME) {
        if (ptc_ui_rect_contains(ptc_ui_bedtime_master_switch_rect(), x, y)) {
            return make_hit(PTC_UI_HIT_BEDTIME_MASTER_SWITCH, 0);
        }
        int fields = model->bedtime_section == PTC_UI_BEDTIME_WEEKLY ? 11 :
            (model->bedtime_section == PTC_UI_BEDTIME_CALENDAR ? 5 : 6);
        for (i = 0; i < 3; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_bedtime_section_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_BEDTIME_SECTION, i);
            }
        }
        for (i = 0; i < fields; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_bedtime_field_rect(model->bedtime_section, i), x, y)) {
                return make_hit(PTC_UI_HIT_BEDTIME_FIELD, i);
            }
        }
        return make_hit(PTC_UI_HIT_NONE, 0);
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_ROOT &&
        model->forecast_available) {
        for (i = 0; i < 7; ++i) {
            if (ptc_ui_rect_contains(ptc_ui_forecast_day_row_rect(i), x, y)) {
                return make_hit(PTC_UI_HIT_FORECAST_DAY, i);
            }
        }
    }
    if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
        for (i = 0; i < model->recent_event_count; ++i) {
            if (rect_contains_row_cell(ptc_ui_support_event_rect(i), 42, x, y)) {
                return make_hit(PTC_UI_HIT_SUPPORT_EVENT, model->recent_event_count - 1 - i);
            }
        }
    }
    count = ptc_ui_parent_action_count(model->parent_page);
    for (i = 0; i < count; ++i) {
        PtcUiRect card_rect = model->parent_page == PTC_UI_PARENT_SUPPORT
            ? ptc_ui_support_card_rect(i)
            : (model->parent_page == PTC_UI_PARENT_TODAY ? ptc_ui_today_card_rect(i) :
               (model->parent_page == PTC_UI_PARENT_PLAN ? ptc_ui_plan_card_rect(i) : ptc_ui_parent_card_rect(i)));
        if (model->parent_page == PTC_UI_PARENT_TODAY &&
            (model->disable_flag_present || model->waiting ||
             (i == 3 && ptc_ui_status_is_fresh(model, (int64_t)time(NULL)) &&
              !model->today_override_present))) continue;
        if ((model->parent_page != PTC_UI_PARENT_SUPPORT ||
             (ptc_ui_safety_action_visible(model, i) &&
              ptc_ui_safety_action_available(model, i) != PTC_UI_ACTION_DISABLED)) &&
            ptc_ui_rect_contains(card_rect, x, y)) {
            return make_hit(PTC_UI_HIT_PARENT_CARD, i);
        }
    }
    return make_hit(PTC_UI_HIT_NONE, 0);
}
