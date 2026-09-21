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

/*
 * Shared control geometry. These rects are the single source of truth for both
 * the renderer (ui_graphics.c) and touch hit-testing (ptc_ui_hit_test), so a
 * tap always lands on exactly the control that was drawn.
 */

#define PTC_UI_SCREEN_W 1280
#define PTC_UI_SCREEN_H 720
#define PTC_UI_DIALOG_BTN_W 210
#define PTC_UI_DIALOG_BTN_H 52

PtcUiRect ptc_ui_child_submit_rect(void)
{
    PtcUiRect rect = {684, 216, 516, 92};
    return rect;
}

PtcUiRect ptc_ui_child_refresh_rect(void)
{
    PtcUiRect rect = {1044, 660, 188, 48};
    return rect;
}

PtcUiRect ptc_ui_child_buffer_rect(void)
{
    PtcUiRect rect = {684, 324, 516, 72};
    return rect;
}

PtcUiRect ptc_ui_child_footer_rect(int index)
{
    static const int widths[] = {250, 500, 180};
    static const int xs[] = {54, 322, 840};
    PtcUiRect rect = {0, 660, 0, 48};
    if (index >= 0 && index < 3) {
        rect.x = xs[index];
        rect.w = widths[index];
    }
    return rect;
}

PtcUiRect ptc_ui_error_retry_rect(void)
{
    PtcUiRect rect = {294, 478, 320, 76};
    return rect;
}

PtcUiRect ptc_ui_error_back_rect(void)
{
    PtcUiRect rect = {666, 478, 320, 76};
    return rect;
}

PtcUiRect ptc_ui_setup_shortcut_card_rect(int index)
{
    int column = index / 7;
    int row = index % 7;
    PtcUiRect rect = {204 + column * 438, 278 + row * 34, 410, 29};
    if (index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

PtcUiRect ptc_ui_setup_primary_rect(void)
{
    PtcUiRect rect = {896, 570, 330, 62};
    return rect;
}

PtcUiRect ptc_ui_setup_back_rect(void)
{
    PtcUiRect rect = {54, 570, 230, 62};
    return rect;
}

PtcUiRect ptc_ui_setup_pin_rect(void)
{
    PtcUiRect rect = {204, 300, 520, 78};
    return rect;
}

PtcUiRect ptc_ui_setup_theme_rect(int index)
{
    PtcUiRect rect = {204 + index * 292, 270, 268, 132};
    if (index < 0 || index >= 3) return (PtcUiRect){0, 0, 0, 0};
    return rect;
}

PtcUiRect ptc_ui_notice_rect(void)
{
    return (PtcUiRect){806, 620, 420, 36};
}

PtcUiRect ptc_ui_notice_details_rect(void)
{
    PtcUiRect notice = ptc_ui_notice_rect();
    return (PtcUiRect){notice.x + notice.w - 82, notice.y + (notice.h - 28) / 2, 70, 28};
}

PtcUiRect ptc_ui_notice_status_icon_rect(int y)
{
    PtcUiRect notice = ptc_ui_notice_rect();
    (void)y;
    return (PtcUiRect){notice.x + 14, notice.y + (notice.h - 20) / 2, 20, 20};
}

PtcUiRect ptc_ui_notice_command_text_rect(int y, int height)
{
    /* The support-only command follows up to two lines of user feedback. */
    return (PtcUiRect){108, y + height - 28, 1094, 22};
}

PtcUiRect ptc_ui_setup_zone_rect(int index)
{
    PtcUiRect rect = {204 + index * 448, 286, 400, 190};
    if (index < 0 || index > 1) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

PtcUiRect ptc_ui_parent_footer_rect(int index)
{
    static const int widths[] = {130, 130, 170, 130, 564};
    static const int xs[] = {54, 196, 338, 520, 662};
    PtcUiRect rect = {0, 664, 0, 44};
    if (index >= 0 && index < 5) {
        rect.x = xs[index];
        rect.w = widths[index];
    }
    return rect;
}

PtcUiRect ptc_ui_parent_subpage_footer_rect(int index)
{
    if (index == 0) return (PtcUiRect){54, 664, 170, 44};
    if (index == 1) return (PtcUiRect){236, 664, 130, 44};
    return (PtcUiRect){0, 664, 0, 44};
}

PtcUiRect ptc_ui_parent_tab_rect(int index)
{
    PtcUiRect rect = {54 + index * 174, 108, 158, 48};
    return rect;
}

PtcUiRect ptc_ui_parent_card_rect(int index)
{
    if (index == 6) return (PtcUiRect){842, 408, 384, 94};
    int column = index % 2;
    int row = index / 2;
    PtcUiRect rect = {54 + column * 385, 176 + row * 136, 365, 120};
    return rect;
}

void ptc_ui_format_home_remaining(const PtcUiModel *model, int64_t now, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || !model->status_loaded) snprintf(out, out_size, "等待刷新");
    else if (!ptc_ui_status_is_fresh(model, now))
        snprintf(out, out_size, "状态待确认");
    else if (model->bedtime_active && !model->bedtime_skipped)
        snprintf(out, out_size, "就寝限制中");
    else if (model->blocked_today == 1 || model->restricted_now == 1)
        snprintf(out, out_size, "禁止游玩");
    else ptc_ui_format_quota_remaining(model, out, out_size);
}

void ptc_ui_format_home_total_value(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    /* Only the backend's current-day forecast supplies the displayed total;
       do not reconstruct it from remaining time or consumption estimates. */
    if (!model || !model->status_loaded) snprintf(out, out_size, "待刷新");
    else if (model->unrestricted_today == 1) snprintf(out, out_size, "不限时");
    else if (model->forecast_available && model->forecast[0].day_index == model->day_index &&
             model->forecast[0].mode == PTC_RULE_MODE_LIMIT)
        snprintf(out, out_size, "%u 分钟", (unsigned int)model->forecast[0].minutes);
    else snprintf(out, out_size, "暂不可用");
}

void ptc_ui_format_home_total(const PtcUiModel *model, char *out, size_t out_size)
{
    char value[64];
    if (!out || out_size == 0) return;
    ptc_ui_format_home_total_value(model, value, sizeof(value));
    snprintf(out, out_size, "今日总额度  %s", value);
}

PtcUiRect ptc_ui_home_summary_rect(bool parent)
{
    return parent ? (PtcUiRect){48, 176, 488, 452} : (PtcUiRect){48, 120, 580, 496};
}

PtcUiRect ptc_ui_today_card_rect(int index)
{
    if (index < 0 || index >= 6) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){560 + (index % 2) * 348, 176 + (index / 2) * 136, 324, 120};
}

PtcUiRect ptc_ui_plan_card_rect(int index)
{
    if (index >= 0 && index < 3) return (PtcUiRect){54, 212 + index * 136, 365, 120};
    if (index == 3) return (PtcUiRect){439, 212, 365, 120};
    if (index == 4) return (PtcUiRect){439, 348, 365, 120};
    return (PtcUiRect){0, 0, 0, 0};
}

PtcUiRect ptc_ui_bedtime_section_rect(int index)
{
    if (index < 0 || index >= 3) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){54 + index * 250, 172, 230, 44};
}

PtcUiRect ptc_ui_bedtime_field_rect(int section, int index)
{
    if (section == PTC_UI_BEDTIME_WEEKLY) {
        if (index >= 0 && index < 7) return (PtcUiRect){54 + index * 108, 230, 96, 192};
        if (index >= 7 && index <= 10) return (PtcUiRect){54 + (index - 7) * 188, 436, 176, 52};
    } else if (section == PTC_UI_BEDTIME_CALENDAR) {
        if (index == 0) return (PtcUiRect){54, 234, 752, 76};
        if (index == 1 || index == 2) return (PtcUiRect){54 + (index - 1) * 376, 326, 364, 126};
        if (index == 3 || index == 4) return (PtcUiRect){430 + (index - 3) * 188, 468, 176, 56};
    } else if (section == PTC_UI_BEDTIME_SCHEDULED) {
        if (index >= 0 && index < 4) return (PtcUiRect){54, 234 + index * 68, 752, 58};
        if (index == 4 || index == 5) return (PtcUiRect){430 + (index - 4) * 188, 520, 176, 56};
    }
    return (PtcUiRect){0, 0, 0, 0};
}

PtcUiRect ptc_ui_bedtime_master_switch_rect(void)
{
    return (PtcUiRect){838, 172, 388, 82};
}

PtcUiRect ptc_ui_bedtime_overlay_field_rect(PtcUiOverlay overlay, int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(overlay);
    if ((overlay != PTC_UI_OVERLAY_BEDTIME_WINDOW &&
         overlay != PTC_UI_OVERLAY_BEDTIME_SPECIAL) || index < 0 || index >= 3)
        return (PtcUiRect){0, 0, 0, 0};
    if (index == 0) return (PtcUiRect){dialog.x + 44, dialog.y + 104, dialog.w - 88, 58};
    return (PtcUiRect){dialog.x + 44 + (index - 1) * ((dialog.w - 100) / 2 + 12),
        dialog.y + 176, (dialog.w - 100) / 2, 66};
}

PtcUiRect ptc_ui_bedtime_timeline_rect(PtcUiOverlay overlay)
{
    PtcUiRect dialog = ptc_ui_dialog_for(overlay);
    if (overlay != PTC_UI_OVERLAY_BEDTIME_WINDOW &&
        overlay != PTC_UI_OVERLAY_BEDTIME_SPECIAL)
        return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 44, dialog.y + 266, dialog.w - 88, 32};
}

PtcUiRect ptc_ui_bedtime_preset_rect(PtcUiOverlay overlay, int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(overlay);
    int gap = 8;
    int width;
    if ((overlay != PTC_UI_OVERLAY_BEDTIME_WINDOW &&
         overlay != PTC_UI_OVERLAY_BEDTIME_SPECIAL) || index < 0 || index >= 5)
        return (PtcUiRect){0, 0, 0, 0};
    width = (dialog.w - 88 - gap * 4) / 5;
    return (PtcUiRect){dialog.x + 44 + index * (width + gap), dialog.y + 352, width, 42};
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

    if (model->bedtime_section_focused) {
        if (horizontal != 0) {
            int next = (int)model->bedtime_section + (horizontal > 0 ? 1 : -1);
            if (next >= PTC_UI_BEDTIME_WEEKLY && next <= PTC_UI_BEDTIME_SCHEDULED) {
                model->bedtime_section = (PtcUiBedtimeSection)next;
                model->selected_index = 0;
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

PtcUiRect ptc_ui_home_details_rect(bool parent)
{
    return parent ? (PtcUiRect){84, 490, 416, 48} : (PtcUiRect){684, 412, 516, 64};
}

PtcUiRect ptc_ui_home_details_tab_rect(int index)
{
    (void)index;
    return (PtcUiRect){0, 0, 0, 0};
}

PtcUiOperation ptc_ui_today_operation(int index)
{
    /* UI order is independent of the operation enum and request dispatch. */
    static const PtcUiOperation actions[] = {
        PTC_UI_OPERATION_SET_TODAY_LIMIT, PTC_UI_OPERATION_ADD_TODAY_MINUTES,
        PTC_UI_OPERATION_DISABLE_TODAY_LIMIT, PTC_UI_OPERATION_RESTORE_TODAY_POLICY,
        PTC_UI_OPERATION_SKIP_BEDTIME, PTC_UI_OPERATION_NONE
    };
    return index >= 0 && index < 6 ? actions[index] : PTC_UI_OPERATION_NONE;
}

bool ptc_ui_open_home_details(PtcUiModel *model)
{
    if (!model || model->waiting || model->overlay != PTC_UI_OVERLAY_NONE ||
        (model->view != PTC_UI_CHILD &&
         !(model->view == PTC_UI_PARENT && model->parent_page == PTC_UI_PARENT_TODAY))) return false;
    /* Keep the underlying focus and execution message intact on open/close. */
    model->overlay = PTC_UI_OVERLAY_HOME_DETAILS;
    model->home_details_page = 0;
    snprintf(model->overlay_title, sizeof(model->overlay_title), "%s",
        model->view == PTC_UI_CHILD ? "使用详情" : "今日调度详情");
    model->overlay_body[0] = '\0';
    return true;
}

bool ptc_ui_open_notice_details(PtcUiModel *model)
{
    PtcUiNoticeProjection notice;
    if (!model || model->overlay != PTC_UI_OVERLAY_NONE) return false;
    ptc_ui_project_notice(model, &notice);
    if (!notice.visible || !notice.has_details) return false;
    model->overlay = PTC_UI_OVERLAY_NOTICE_DETAILS;
    return true;
}

bool ptc_ui_home_notice_expanded(const PtcUiModel *model)
{
    return model && (strcmp(model->result_status, "error") == 0 ||
        model->disable_flag_present || model->recovery_active ||
        model->apply_pending_confirmation || model->restricted_now == 1 ||
        (model->remaining_available && model->remaining_minutes == 0 && model->unrestricted_today != 1) ||
        (model->temporary_unlocked_available && model->temporary_unlocked) ||
        (model->restriction_enabled_available && !model->restriction_enabled) ||
        strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0);
}

PtcUiRect ptc_ui_advanced_back_rect(void)
{
    return (PtcUiRect){54, 108, 192, 48};
}

PtcUiRect ptc_ui_support_card_rect(int index)
{
    int column;
    int row;
    if (index < 0 || index >= 6) return (PtcUiRect){0, 0, 0, 0};
    column = index % 2;
    row = index / 2;
    return (PtcUiRect){54 + column * 385, 176 + row * 136, 365, 120};
}

PtcUiRect ptc_ui_holiday_card_rect(int index)
{
    switch (index) {
    case 0: return (PtcUiRect){54, 176, 760, 84};
    case 1: return (PtcUiRect){54, 276, 368, 224};
    case 2: return (PtcUiRect){446, 276, 368, 224};
    case 3: return (PtcUiRect){54, 516, 240, 66};
    case 4: return (PtcUiRect){310, 516, 240, 66};
    case 5: return (PtcUiRect){566, 516, 248, 66};
    default: return (PtcUiRect){0, 0, 0, 0};
    }
}

PtcUiRect ptc_ui_holiday_calendar_rect(void)
{
    return (PtcUiRect){862, 532, 336, 50};
}

PtcUiRect ptc_ui_holiday_page_action_rect(int index)
{
    PtcUiRect rect = {280 + index * 250, 576, 230, 50};
    if (index < 0 || index >= 3) return (PtcUiRect){0, 0, 0, 0};
    return rect;
}

PtcUiRect ptc_ui_holiday_enable_rect(void)
{
    return (PtcUiRect){722, 200, 76, 36};
}

PtcUiRect ptc_ui_holiday_mode_rect(int index)
{
    PtcUiRect card = ptc_ui_holiday_card_rect(index + 1);
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){card.x + card.w - 92, card.y + 14, 80, 36};
}

PtcUiRect ptc_ui_holiday_minutes_rect(int index)
{
    PtcUiRect card = ptc_ui_holiday_card_rect(index + 1);
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){card.x + 12, card.y + 80, card.w - 24, 130};
}

uint16_t ptc_ui_today_limit_start_value(const PtcUiModel *model, uint16_t fallback)
{
    int played;
    if (model && model->played_minutes_available && model->played_minutes >= 0) {
        played = model->played_minutes;
        if (played < 1) played = 1;
        if (played > 1440) played = 1440;
        if (fallback >= 1 && fallback <= 1440 && fallback > played) return fallback;
        return (uint16_t)played;
    }
    if (fallback >= 1 && fallback <= 1440) return fallback;
    return 60;
}

PtcUiRect ptc_ui_support_event_rect(int index)
{
    if (index < 0 || index >= 3) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){868, 478 + index * 42, 332, 36};
}

PtcUiRect ptc_ui_dialog_rect(int width, int height)
{
    PtcUiRect rect = {(PTC_UI_SCREEN_W - width) / 2, (PTC_UI_SCREEN_H - height) / 2 - 10, width, height};
    if (rect.y < 10) rect.y = 10;
    return rect;
}

static void dialog_dims(PtcUiOverlay overlay, int *width, int *height)
{
    switch (overlay) {
    case PTC_UI_OVERLAY_NOTICE_DETAILS:
        *width = 780;
        *height = 420;
        break;
    case PTC_UI_OVERLAY_HOME_DETAILS:
        *width = 1120;
        *height = 640;
        break;
    case PTC_UI_OVERLAY_MINUTES:
        *width = 720;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_WEEKLY:
        *width = 1172;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_NUMPAD:
        *width = 620;
        *height = 700;
        break;
    case PTC_UI_OVERLAY_CREDENTIAL:
        *width = 900;
        *height = 500;
        break;
    case PTC_UI_OVERLAY_GRANT_MANAGER:
        *width = 1120;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_REDEMPTION_HISTORY:
        *width = 1120;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_ACTIVITY_HISTORY:
        *width = 1120;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_SCHEDULED:
        *width = 1120;
        *height = 640;
        break;
    case PTC_UI_OVERLAY_AUTONOMY:
        *width = 880;
        *height = 480;
        break;
    case PTC_UI_OVERLAY_BEDTIME:
        *width = 860;
        *height = 540;
        break;
    case PTC_UI_OVERLAY_QR:
        *width = 1120;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
        *width = 1120;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_GRANT_LOCAL:
        *width = 920;
        *height = 650;
        break;
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
        *width = 860;
        *height = 350;
        break;
    case PTC_UI_OVERLAY_CREDENTIAL_LEAVE:
    case PTC_UI_OVERLAY_SCHEDULED_LEAVE:
        *width = 720;
        *height = 300;
        break;
    case PTC_UI_OVERLAY_CODE_RESULT:
        *width = 760;
        *height = 420;
        break;
    case PTC_UI_OVERLAY_AUTH_ERROR:
        *width = 720;
        *height = 340;
        break;
    case PTC_UI_OVERLAY_SOFTWARE_INFO:
        *width = 960;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_HOLIDAY_CALENDAR:
        *width = 1040;
        *height = 600;
        break;
    case PTC_UI_OVERLAY_MINUTE_EDITOR:
        *width = 920;
        *height = 620;
        break;
    case PTC_UI_OVERLAY_HOLIDAY_LEAVE:
        *width = 720;
        *height = 320;
        break;
    case PTC_UI_OVERLAY_SUPPORT_EVENT:
        *width = 960;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_WEEKLY_BULK:
        *width = 1040;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_ALBUM_MANAGER:
        *width = 980;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_THEME:
        *width = 820;
        *height = 360;
        break;
    case PTC_UI_OVERLAY_BEDTIME_WINDOW:
    case PTC_UI_OVERLAY_BEDTIME_SPECIAL:
        *width = 960;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_PIN:
        *width = 1040;
        *height = 620;
        break;
    case PTC_UI_OVERLAY_CONFIRM:
    default:
        *width = 760;
        *height = 420;
        break;
    }
}

PtcUiRect ptc_ui_dialog_for(PtcUiOverlay overlay)
{
    int width = 0;
    int height = 0;
    dialog_dims(overlay, &width, &height);
    return ptc_ui_dialog_rect(width, height);
}

PtcUiRect ptc_ui_redemption_history_prev_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_REDEMPTION_HISTORY);
    return (PtcUiRect){dialog.x + 24, dialog.y + dialog.h - PTC_UI_DIALOG_BTN_H - 24,
        160, PTC_UI_DIALOG_BTN_H};
}

PtcUiRect ptc_ui_redemption_history_next_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_REDEMPTION_HISTORY);
    return (PtcUiRect){dialog.x + 200, dialog.y + dialog.h - PTC_UI_DIALOG_BTN_H - 24,
        160, PTC_UI_DIALOG_BTN_H};
}

PtcUiRect ptc_ui_scheduled_field_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_SCHEDULED);
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 34, dialog.y + 116 + index * 76, 560, 64};
}

PtcUiRect ptc_ui_autonomy_option_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_AUTONOMY);
    const int gap = 12;
    const int width = (dialog.w - 96 - gap * 3) / 4;
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 48 + index * (width + gap), dialog.y + 176, width, 96};
}

PtcUiRect ptc_ui_quick_add_option_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_QUICK_ADD);
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 48 + index * 164, dialog.y + 166, 148, 92};
}

PtcUiRect ptc_ui_minutes_value_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 174, dialog.y + 140, 372, 96};
    return rect;
}

PtcUiRect ptc_ui_minutes_dec_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 36, dialog.y + 140, 112, 96};
    return rect;
}

PtcUiRect ptc_ui_minutes_inc_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 572, dialog.y + 140, 112, 96};
    return rect;
}

PtcUiRect ptc_ui_minutes_inc_large_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 378, dialog.y + 260, 156, 42};
    return rect;
}

PtcUiRect ptc_ui_minutes_dec_large_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTES);
    PtcUiRect rect = {dialog.x + 186, dialog.y + 260, 156, 42};
    return rect;
}

PtcUiRect ptc_ui_weekly_day_rect(int index)
{
    PtcUiRect rect = {54 + index * 108, 208, 98, 280};
    if (index < 0 || index >= 7) rect = (PtcUiRect){0, 0, 0, 0};
    return rect;
}

PtcUiRect ptc_ui_weekly_day_header_rect(int index)
{
    PtcUiRect card = ptc_ui_weekly_day_rect(index);
    return (PtcUiRect){card.x, card.y, card.w, 42};
}

PtcUiRect ptc_ui_weekly_day_mode_rect(int index)
{
    PtcUiRect card = ptc_ui_weekly_day_rect(index);
    return (PtcUiRect){card.x + 4, card.y + 44, card.w - 8, 46};
}

PtcUiRect ptc_ui_weekly_day_minutes_rect(int index)
{
    PtcUiRect card = ptc_ui_weekly_day_rect(index);
    PtcUiRect rect = {card.x + 4, card.y + 90, card.w - 8, 186};
    return rect;
}

PtcUiRect ptc_ui_weekly_bulk_rect(void)
{
    return (PtcUiRect){242, 516, 176, 66};
}

PtcUiRect ptc_ui_numpad_display_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_NUMPAD);
    PtcUiRect rect = {dialog.x + 70, dialog.y + 140, dialog.w - 140, 70};
    return rect;
}

PtcUiRect ptc_ui_code_slot_rect(int index)
{
    PtcUiRect display = ptc_ui_numpad_display_rect();
    const int slot_width = 48;
    const int gap = 6;
    const int group_gap = 18;
    const int total_width = slot_width * 8 + gap * 6 + group_gap;
    int x;
    if (index < 0 || index >= 8) return (PtcUiRect){0, 0, 0, 0};
    x = display.x + (display.w - total_width) / 2 + index * (slot_width + gap);
    if (index >= 4) x += group_gap - gap;
    return (PtcUiRect){x, display.y + 9, slot_width, display.h - 18};
}

PtcUiRect ptc_ui_numpad_key_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_NUMPAD);
    int row = index / 3;
    int column = index % 3;
    PtcUiRect rect = {dialog.x + 116 + column * 134, dialog.y + 324 + row * 54, 120, 46};
    if (index < 0 || index >= 12) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

PtcUiRect ptc_ui_numpad_quick_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_NUMPAD);
    PtcUiRect rect = {dialog.x + 32 + index * 140, dialog.y + 274, 124, 40};
    if (index < 0 || index >= 4) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

PtcUiRect ptc_ui_pin_dialog_rect(void)
{
    return ptc_ui_dialog_for(PTC_UI_OVERLAY_PIN);
}

PtcUiRect ptc_ui_pin_key_rect(int digit)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_PIN);
    int row;
    int column;
    PtcUiRect rect;
    if (digit < 0 || digit > 9) return (PtcUiRect){0, 0, 0, 0};
    row = digit == 0 ? 3 : (digit - 1) / 3;
    column = digit == 0 ? 1 : (digit - 1) % 3;
    rect = (PtcUiRect){dialog.x + 590 + column * 112, dialog.y + 220 + row * 58, 100, 52};
    return rect;
}

PtcUiRect ptc_ui_pin_backspace_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_PIN);
    return (PtcUiRect){dialog.x + 590, dialog.y + 458, 210, 48};
}

PtcUiRect ptc_ui_pin_confirm_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_PIN);
    return (PtcUiRect){dialog.x + 814, dialog.y + 458, 190, 48};
}

PtcUiRect ptc_ui_pin_cancel_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_PIN);
    return (PtcUiRect){dialog.x + 590, dialog.y + 516, 190, 48};
}

PtcUiRect ptc_ui_pin_keyboard_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_PIN);
    return (PtcUiRect){dialog.x + 794, dialog.y + 516, 210, 48};
}

static int dialog_button_top(PtcUiRect dialog)
{
    return dialog.y + dialog.h - PTC_UI_DIALOG_BTN_H - 24;
}

PtcUiRect ptc_ui_confirm_rect(PtcUiOverlay overlay)
{
    if (overlay == PTC_UI_OVERLAY_HOME_DETAILS || overlay == PTC_UI_OVERLAY_NOTICE_DETAILS)
        return (PtcUiRect){0, 0, 0, 0};
    PtcUiRect dialog = ptc_ui_dialog_for(overlay);
    PtcUiRect rect = {dialog.x + dialog.w - 24 - PTC_UI_DIALOG_BTN_W, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    return rect;
}

PtcUiRect ptc_ui_cancel_rect(PtcUiOverlay overlay)
{
    PtcUiRect dialog = ptc_ui_dialog_for(overlay);
    if (overlay == PTC_UI_OVERLAY_GRANT_LOCAL)
        return (PtcUiRect){dialog.x + 42, dialog.y + 588, 210, 44};
    if (overlay == PTC_UI_OVERLAY_HOME_DETAILS)
        return (PtcUiRect){dialog.x + dialog.w - 28 - PTC_UI_DIALOG_BTN_W, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    if (overlay == PTC_UI_OVERLAY_NOTICE_DETAILS)
        return (PtcUiRect){dialog.x + (dialog.w - PTC_UI_DIALOG_BTN_W) / 2, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    PtcUiRect rect = {dialog.x + dialog.w - 24 - PTC_UI_DIALOG_BTN_W * 2 - 16, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    return rect;
}

PtcUiRect ptc_ui_discard_rect(PtcUiOverlay overlay)
{
    PtcUiRect dialog = ptc_ui_dialog_for(overlay);
    PtcUiRect rect = {dialog.x + 24, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    return rect;
}

PtcUiRect ptc_ui_weekly_save_rect(void)
{
    PtcUiRect rect = {618, 516, 196, 66};
    return rect;
}

PtcUiRect ptc_ui_weekly_discard_rect(void)
{
    PtcUiRect rect = {430, 516, 176, 66};
    return rect;
}

PtcUiRect ptc_ui_credential_input_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_CREDENTIAL);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 224, 600, 64};
    return rect;
}

PtcUiRect ptc_ui_credential_random_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_CREDENTIAL);
    PtcUiRect rect = {dialog.x + 662, dialog.y + 224, 196, 64};
    return rect;
}

PtcUiRect ptc_ui_credential_reveal_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_CREDENTIAL);
    PtcUiRect rect = {dialog.x + 662, dialog.y + 132, 196, 56};
    return rect;
}

PtcUiRect ptc_ui_credential_demo_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_CREDENTIAL);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 318, 270, 52};
    return rect;
}

PtcUiRect ptc_ui_grant_manager_card_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_GRANT_MANAGER);
    int column = index % 2;
    int row = index / 2;
    PtcUiRect rect = {dialog.x + 34 + column * 530, dialog.y + 126 + row * 116, 510, 100};
    if (index < 0 || index >= PTC_UI_GRANT_MANAGER_COUNT) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

PtcUiRect ptc_ui_grant_generate_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_GRANT_LOCAL);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 530, dialog.w - 84, 46};
    return rect;
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

PtcUiRect ptc_ui_minute_editor_key_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTE_EDITOR);
    int row = index / 3;
    int column = index % 3;
    PtcUiRect rect = {dialog.x + 34 + column * 138, dialog.y + 214 + row * 66, 126, 54};
    if (index < 0 || index >= 12) return (PtcUiRect){0, 0, 0, 0};
    return rect;
}

PtcUiRect ptc_ui_minute_editor_quick_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTE_EDITOR);
    PtcUiRect rect = {dialog.x + 34 + index * 226, dialog.y + 154, 208, 48};
    if (index < 0 || index >= 2) return (PtcUiRect){0, 0, 0, 0};
    return rect;
}

PtcUiRect ptc_ui_minute_editor_field_rect(PtcUiDurationField field)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTE_EDITOR);
    if (field == PTC_UI_DURATION_HOURS) return (PtcUiRect){dialog.x + 536, dialog.y + 146, 166, 68};
    if (field == PTC_UI_DURATION_MINUTES) return (PtcUiRect){dialog.x + 720, dialog.y + 146, 166, 68};
    return (PtcUiRect){0, 0, 0, 0};
}

PtcUiRect ptc_ui_minute_editor_summary_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTE_EDITOR);
    return (PtcUiRect){dialog.x + 536, dialog.y + 264, 350, 254};
}

bool ptc_ui_apply_weekly_bulk(PtcUiModel *model, bool weekend)
{
    PtcDayRule source;
    PtcDayRule before[7];
    int slot;
    if (!model || model->disable_flag_present) return false;
    slot = model->weekly_last_day_slot;
    if (slot < 0 || slot >= 7) slot = 0;
    source = model->draft_week[ptc_ui_weekday_for_display_slot(slot)];
    memcpy(before, model->draft_week, sizeof(before));
    if (weekend) {
        model->draft_week[6] = source;
        model->draft_week[0] = source;
    } else {
        for (int day = 1; day <= 5; ++day) model->draft_week[day] = source;
    }
    model->weekly_dirty = memcmp(model->draft_week, model->current_week, sizeof(model->draft_week)) != 0;
    return memcmp(before, model->draft_week, sizeof(before)) != 0;
}

void ptc_ui_weekly_bulk_stats(const PtcUiModel *model, bool weekend, PtcUiWeeklyBulkStats *stats)
{
    int slot;
    PtcDayRule source;
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    if (!model) return;
    slot = model->weekly_last_day_slot;
    if (slot < 0 || slot >= 7) slot = 0;
    source = model->draft_week[ptc_ui_weekday_for_display_slot(slot)];
    for (int day = 0; day < 7; ++day) {
        bool included = weekend ? (day == 0 || day == 6) : (day >= 1 && day <= 5);
        int group = -1;
        if (!included) continue;
        ++stats->target_count;
        if (ptc_ui_day_rule_effectively_changed(model->draft_week[day], source)) ++stats->changed_count;
        else ++stats->unchanged_count;
        for (int index = 0; index < stats->rule_group_count; ++index) {
            if (!ptc_ui_day_rule_effectively_changed(stats->rule_groups[index].rule, model->draft_week[day])) {
                group = index;
                break;
            }
        }
        if (group < 0 && stats->rule_group_count < 5) {
            group = stats->rule_group_count++;
            stats->rule_groups[group].rule = model->draft_week[day];
        }
        if (group >= 0) ++stats->rule_groups[group].count;
    }
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

int ptc_ui_grant_estimate_remaining(const PtcUiModel *model, uint16_t grant_minutes, bool *capped)
{
    int maximum_remaining;
    int estimate;
    if (capped) *capped = false;
    if (!model || model->grant_status_refresh_failed || !model->played_minutes_available ||
        model->played_minutes < 0) {
        return -1;
    }
    if (model->unrestricted_today == 1) return (int)grant_minutes;
    if (!model->remaining_available || model->remaining_minutes < 0) return -1;
    maximum_remaining = 1440 - model->played_minutes;
    if (maximum_remaining < 0) maximum_remaining = 0;
    estimate = model->remaining_minutes + (int)grant_minutes;
    if (estimate > maximum_remaining) {
        estimate = maximum_remaining;
        if (capped) *capped = true;
    }
    return estimate;
}

PtcUiRect ptc_ui_shortcut_option_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_SHORTCUT_MANAGER);
    int column = index / 7;
    int row = index % 7;
    PtcUiRect rect = {dialog.x + 36 + column * 524, dialog.y + 128 + row * 40, 500, 34};
    if (index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

PtcUiRect ptc_ui_shortcut_disable_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_SHORTCUT_MANAGER);
    PtcUiRect rect = {dialog.x + 120, dialog.y + 430, 400, 46};
    return rect;
}

PtcUiRect ptc_ui_shortcut_hint_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_SHORTCUT_MANAGER);
    PtcUiRect rect = {dialog.x + 600, dialog.y + 430, 400, 46};
    return rect;
}

PtcUiRect ptc_ui_grant_adjust_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_GRANT_LOCAL);
    PtcUiRect rect = {dialog.x + 54, dialog.y + 194, 330, 92};
    if (index != 0) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

/* Left-aligned editor helper buttons for the weekly overlay. */
PtcUiRect ptc_ui_weekly_mode_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 34, dialog_button_top(dialog), 150, PTC_UI_DIALOG_BTN_H};
    return rect;
}

PtcUiRect ptc_ui_weekly_min_down_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 426, dialog.y + 464, 140, 32};
    return rect;
}

PtcUiRect ptc_ui_weekly_min_up_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 426, dialog.y + 350, 140, 32};
    return rect;
}

PtcUiRect ptc_ui_weekly_min_input_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 406, dialog.y + 390, 180, 66};
    return rect;
}

PtcUiRect ptc_ui_weekly_min_dec_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 310, dialog.y + 390, 80, 66};
    return rect;
}

PtcUiRect ptc_ui_weekly_min_inc_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_WEEKLY);
    PtcUiRect rect = {dialog.x + 602, dialog.y + 390, 80, 66};
    return rect;
}

bool ptc_ui_rect_contains(PtcUiRect rect, int x, int y)
{
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}
PtcUiRect ptc_ui_weekly_page_mode_rect(void)
{
    PtcUiRect rect = {54, 516, 176, 66};
    return rect;
}

PtcUiRect ptc_ui_weekly_bulk_target_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_WEEKLY_BULK);
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 40, dialog.y + 184 + index * 102, 410, 88};
}

PtcUiRect ptc_ui_album_action_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_ALBUM_MANAGER);
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 38 + index * 450, dialog.y + 178, 420, 224};
}

PtcUiRect ptc_ui_album_refresh_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_ALBUM_MANAGER);
    return (PtcUiRect){dialog.x + dialog.w - 226, dialog.y + 92, 188, 46};
}

PtcUiRect ptc_ui_theme_option_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_THEME);
    if (index < 0 || index >= 3) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 40 + index * 250, dialog.y + 154, 230, 100};
}
