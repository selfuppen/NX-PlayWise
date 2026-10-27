#include "ui_state.h"
#include "ui_layout.h"
#include "ui_layout_internal.h"
#include "../ui_language.h"

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

static int dialog_button_top(PtcUiRect dialog)
{
    return dialog.y + dialog.h - PTC_UI_DIALOG_BTN_H - 24;
}

PtcUiRect ptc_ui_child_submit_rect(void)
{
    PtcUiRect rect = {964, 120, 268, 112};
    return rect;
}

PtcUiRect ptc_ui_child_refresh_rect(void)
{
    PtcUiRect rect = {1044, 660, 188, 48};
    return rect;
}

PtcUiRect ptc_ui_child_buffer_rect(void)
{
    PtcUiRect rect = {964, 246, 268, 106};
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

PtcUiRect ptc_ui_setup_language_rect(int index)
{
    if (index < 0 || index > 3) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){94 + index * 274, 240, 262, 44};
}

PtcUiRect ptc_ui_setup_primary_rect(void) { return (PtcUiRect){1036, 664, 190, 44}; }
PtcUiRect ptc_ui_setup_back_rect(void) { return (PtcUiRect){54, 664, 170, 44}; }
PtcUiRect ptc_ui_setup_skip_rect(void) { return (PtcUiRect){236, 664, 280, 44}; }
PtcUiRect ptc_ui_setup_pin_rect(void) { return (PtcUiRect){94, 320, 510, 54}; }
PtcUiRect ptc_ui_setup_more_rect(void) { return (PtcUiRect){0, 0, 0, 0}; }
PtcUiRect ptc_ui_setup_shortcut_rect(void) { return (PtcUiRect){676, 334, 510, 44}; }
PtcUiRect ptc_ui_setup_time_help_rect(void) { return (PtcUiRect){94, 536, 1092, 76}; }
PtcUiRect ptc_ui_setup_pctl_help_rect(void) { return (PtcUiRect){914, 452, 272, 38}; }
PtcUiRect ptc_ui_setup_theme_rect(int index)
{
    if (index < 0 || index > 2) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){676 + index * 174, 258, 162, 40};
}

PtcUiRect ptc_ui_setup_eye_care_rect(void)
{
    return (PtcUiRect){676, 414, 510, 48};
}

PtcUiRect ptc_ui_setup_bedtime_rect(void)
{
    return (PtcUiRect){676, 516, 510, 48};
}

PtcUiRect ptc_ui_notice_rect(void)
{
    return (PtcUiRect){806, 664, 420, 44};
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

PtcUiRect ptc_ui_time_status_bar_rect(void)
{
    return (PtcUiRect){754, 18, 472, 66};
}

PtcUiRect ptc_ui_header_language_rect(void)
{
    return (PtcUiRect){632, 28, 110, 36};
}

PtcUiRect ptc_ui_time_status_bar_for_model(const PtcUiModel *model)
{
    PtcUiRect rect = ptc_ui_time_status_bar_rect();
    if (!model) return rect;
    bool policy = model->bedtime_policy.enabled || model->bedtime_status_enabled ||
        model->dock_policy.force_docked || model->dock_policy.undocked_limit_enabled;
    rect.h += (model->eye_care_policy.enabled ? 12 : 0) + (policy ? 16 : 0);
    /* 保持状态胶囊与顶部通栏底分界线(Y=104)之间至少留出 10px 舒适呼吸空间 (即底边缘不超过 Y=94) */
    if (rect.y + rect.h > 94) {
        rect.y = 94 - rect.h;
        if (rect.y < 6) rect.y = 6;
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
    PtcUiRect rect = {54 + index * 174, 120, 158, 38};
    return rect;
}

PtcUiRect ptc_ui_parent_card_rect(int index)
{
    int column = index % 2;
    int row = index / 2;
    PtcUiRect rect = {54 + column * 385, 176 + row * 136, 365, 120};
    return rect;
}

PtcUiRect ptc_ui_settings_card_rect(int index)
{
    int column = index % 2;
    int row = index / 2;
    PtcUiRect rect = {54 + column * 385, 176 + row * 108, 365, 96};
    return rect;
}

/* Pairing QR page shares the standard dialog button row so the drawn button,
   the touch target and the tests always agree. */
PtcUiRect ptc_ui_qr_export_rect(void)
{
    return ptc_ui_confirm_rect(PTC_UI_OVERLAY_QR);
}

/* Two side-by-side solution cards: index 0 = online scan, index 1 = offline file. */
PtcUiRect ptc_ui_qr_panel_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_QR);
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 34 + index * 556, dialog.y + 148, 536, 462};
}

/* Fixed white QR canvas (12px quiet zone included) inside the left card. */
PtcUiRect ptc_ui_qr_code_rect(void)
{
    PtcUiRect panel = ptc_ui_qr_panel_rect(0);
    return (PtcUiRect){panel.x + 122, panel.y + 52, 292, 292};
}

PtcUiRect ptc_ui_qr_address_rect(void)
{
    PtcUiRect panel = ptc_ui_qr_panel_rect(0);
    return (PtcUiRect){panel.x + 22, panel.y + 350, panel.w - 44, 60};
}

PtcUiRect ptc_ui_qr_warning_rect(void)
{
    PtcUiRect panel = ptc_ui_qr_panel_rect(0);
    return (PtcUiRect){panel.x + 22, panel.y + 414, panel.w - 44, 40};
}

/* Safety & preferences secondary pages: full-page shell with the same footer
   anchors as the time-plan sub-pages. */
PtcUiRect ptc_ui_settings_page_back_rect(void)
{
    return (PtcUiRect){54, 664, 230, 44};
}

PtcUiRect ptc_ui_settings_page_primary_rect(void)
{
    return (PtcUiRect){1026, 664, 200, 44};
}

PtcUiRect ptc_ui_settings_page_footer_rect(int index)
{
    if (index == 0) return ptc_ui_settings_page_back_rect();
    if (index == 1) return (PtcUiRect){300, 664, 150, 44};
    if (index == 2) return (PtcUiRect){466, 664, 150, 44};
    if (index == 3) return ptc_ui_settings_page_primary_rect();
    return (PtcUiRect){0, 664, 0, 44};
}

void ptc_ui_format_home_remaining(const PtcUiModel *model, int64_t now, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!model || !model->status_loaded) snprintf(out, out_size, ptc_ui_text(PTC_UI_T_AWAITING_REFRESH));
    else if (!ptc_ui_status_is_fresh(model, now))
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
    else if (model->bedtime_active && !model->bedtime_skipped)
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE));
    else if (model->eye_care_policy.enabled && strcmp(model->eye_care_phase, "resting") == 0)
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING));
    else if (model->dock_restriction_active)
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_DOCK_BLOCKED));
    else if (model->blocked_today == 1 || model->restricted_now == 1)
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_PLAY_BLOCKED));
    else ptc_ui_format_quota_remaining(model, out, out_size);
}

bool ptc_ui_home_remaining_minutes(const PtcUiModel *model, int64_t now, int *minutes)
{
    if (!model || !minutes || !model->status_loaded || !ptc_ui_status_is_fresh(model, now) ||
        (model->bedtime_active && !model->bedtime_skipped) ||
        (model->eye_care_policy.enabled && strcmp(model->eye_care_phase, "resting") == 0) ||
        model->dock_restriction_active ||
        model->blocked_today == 1 || model->restricted_now == 1 ||
        model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped) ||
        !model->remaining_available ||
        model->remaining_minutes < 0)
        return false;
    *minutes = model->remaining_minutes;
    return true;
}

void ptc_ui_format_home_total_value(const PtcUiModel *model, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    /* Only the backend's current-day forecast supplies the displayed total;
       do not reconstruct it from remaining time or consumption estimates. */
    if (!model || !model->status_loaded) snprintf(out, out_size, ptc_ui_text(PTC_UI_T_PENDING));
    else if (model->unrestricted_today == 1 || (model->eye_care_unlimited_capped || model->dock_unlimited_capped))
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
    else if (model->forecast_available && model->forecast[0].day_index == model->day_index &&
             model->forecast[0].mode == PTC_RULE_MODE_LIMIT)
        snprintf(out, out_size, ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)model->forecast[0].minutes);
    else snprintf(out, out_size, ptc_ui_text(PTC_UI_T_UNAVAILABLE));
}

void ptc_ui_format_home_total(const PtcUiModel *model, char *out, size_t out_size)
{
    char value[64];
    if (!out || out_size == 0) return;
    ptc_ui_format_home_total_value(model, value, sizeof(value));
    snprintf(out, out_size, ptc_ui_text(PTC_UI_T_DAILY_QUOTA_S), value);
}

PtcUiRect ptc_ui_home_summary_rect(bool parent)
{
    return parent ? (PtcUiRect){48, 176, 488, 452} : (PtcUiRect){48, 120, 896, 496};
}

PtcUiRect ptc_ui_today_card_rect(int index)
{
    if (index < 0 || index >= 8) return (PtcUiRect){0, 0, 0, 0};
    if (index == 5) return (PtcUiRect){560, 536, 324, 82};
    if (index == 7) return (PtcUiRect){908, 536, 324, 82};
    if (index == 4) return (PtcUiRect){560, 444, 324, 82};
    if (index == 6) return (PtcUiRect){908, 444, 324, 82};
    return (PtcUiRect){560 + (index % 2) * 348,
                       204 + (index / 2) * 100, 324, 90};
}

PtcUiRect ptc_ui_plan_card_rect(int index)
{
    if (index >= 0 && index < 3) return (PtcUiRect){54, 212 + index * 136, 365, 120};
    if (index >= 3 && index <= 6) return (PtcUiRect){439, 212 + (index - 3) * 106, 365, 90};
    return (PtcUiRect){0, 0, 0, 0};
}

PtcUiRect ptc_ui_forecast_day_row_rect(int index)
{
    if (index < 0 || index >= 7) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){834, 210 + index * 51, 382, 44};
}

PtcUiRect ptc_ui_bedtime_section_rect(int index)
{
    if (index < 0 || index >= 3) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){54 + index * 240, 230, 224, 38};
}

PtcUiRect ptc_ui_bedtime_field_rect(int section, int index)
{
    if (section == PTC_UI_BEDTIME_WEEKLY) {
        if (index >= 0 && index < 7) return (PtcUiRect){54 + index * 108, 276, 96, 186};
        if (index >= 7 && index <= 10) return (PtcUiRect){54 + (index - 7) * 188, 470, 176, 50};
    } else if (section == PTC_UI_BEDTIME_CALENDAR) {
        if (index == 0) return (PtcUiRect){54, 276, 752, 68};
        if (index == 1 || index == 2) return (PtcUiRect){54 + (index - 1) * 376, 352, 364, 110};
        if (index == 3 || index == 4) return (PtcUiRect){430 + (index - 3) * 188, 470, 176, 50};
    } else if (section == PTC_UI_BEDTIME_SCHEDULED) {
        if (index >= 0 && index < 4) return (PtcUiRect){54, 276 + index * 60, 752, 52};
        if (index == 4 || index == 5) return (PtcUiRect){430 + (index - 4) * 188, 526, 176, 50};
    }
    return (PtcUiRect){0, 0, 0, 0};
}

PtcUiRect ptc_ui_bedtime_master_switch_rect(void)
{
    return (PtcUiRect){54, 168, 1172, 54};
}

PtcUiRect ptc_ui_eye_care_page_master_rect(void)
{
    return (PtcUiRect){54, 168, 1172, 58};
}

PtcUiRect ptc_ui_eye_care_play_preset_rect(int index)
{
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){320 + index * 70, 258, 62, 34};
}

PtcUiRect ptc_ui_eye_care_play_dec_rect(void)
{
    return (PtcUiRect){790, 258, 36, 34};
}

PtcUiRect ptc_ui_eye_care_play_value_rect(void)
{
    return (PtcUiRect){832, 258, 130, 34};
}

PtcUiRect ptc_ui_eye_care_play_inc_rect(void)
{
    return (PtcUiRect){968, 258, 36, 34};
}

PtcUiRect ptc_ui_eye_care_rest_preset_rect(int index)
{
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){320 + index * 70, 332, 62, 34};
}

PtcUiRect ptc_ui_eye_care_rest_dec_rect(void)
{
    return (PtcUiRect){790, 332, 36, 34};
}

PtcUiRect ptc_ui_eye_care_rest_value_rect(void)
{
    return (PtcUiRect){832, 332, 130, 34};
}

PtcUiRect ptc_ui_eye_care_rest_inc_rect(void)
{
    return (PtcUiRect){968, 332, 36, 34};
}

PtcUiRect ptc_ui_eye_care_page_save_rect(void)
{
    return (PtcUiRect){1026, 646, 200, 44};
}

PtcUiRect ptc_ui_eye_care_page_skip_rect(void)
{
    return (PtcUiRect){398, 664, 200, 44};
}

PtcUiRect ptc_ui_bedtime_manage_rect(int index)
{
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){784 + index * 224, 230, 218, 38};
}

PtcUiRect ptc_ui_bedtime_overlay_field_rect(PtcUiOverlay overlay, int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(overlay);
    if ((overlay != PTC_UI_OVERLAY_BEDTIME_WINDOW &&
         overlay != PTC_UI_OVERLAY_BEDTIME_SPECIAL) || index < 0 || index >= 3)
        return (PtcUiRect){0, 0, 0, 0};
    /* Reserve two measured header lines, including the longer English guide. */
    if (index == 0) return (PtcUiRect){dialog.x + 44, dialog.y + 130, dialog.w - 88, 58};
    return (PtcUiRect){dialog.x + 44 + (index - 1) * ((dialog.w - 100) / 2 + 12),
        dialog.y + 202, (dialog.w - 100) / 2, 66};
}

PtcUiRect ptc_ui_bedtime_timeline_rect(PtcUiOverlay overlay)
{
    PtcUiRect dialog = ptc_ui_dialog_for(overlay);
    if (overlay != PTC_UI_OVERLAY_BEDTIME_WINDOW &&
        overlay != PTC_UI_OVERLAY_BEDTIME_SPECIAL)
        return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){dialog.x + 44, dialog.y + 292, dialog.w - 88, 32};
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
    return (PtcUiRect){dialog.x + 44 + index * (width + gap), dialog.y + 378, width, 42};
}

PtcUiRect ptc_ui_home_details_rect(bool parent)
{
    return parent ? (PtcUiRect){368, 186, 152, 32} : (PtcUiRect){964, 366, 268, 106};
}

PtcUiRect ptc_ui_home_rules_rect(void)
{
    PtcUiRect dt_rect = ptc_ui_home_details_rect(true);
    return (PtcUiRect){dt_rect.x - 128, dt_rect.y, 120, dt_rect.h};
}

PtcUiRect ptc_ui_today_quota_card_rect(void)
{
    PtcUiRect box = ptc_ui_home_summary_rect(true);
    return (PtcUiRect){box.x + 22, box.y + 154, box.w - 44, 52};
}

PtcUiRect ptc_ui_plan_today_rules_rect(void)
{
    return (PtcUiRect){1074, 184, 140, 24};
}

PtcUiRect ptc_ui_day_decision_refresh_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_DAY_DECISION);
    return (PtcUiRect){dialog.x + dialog.w - 28 - PTC_UI_DIALOG_BTN_W - 16 - 160, dialog_button_top(dialog), 160, PTC_UI_DIALOG_BTN_H};
}

PtcUiRect ptc_ui_day_decision_prev_rect(void)
{
    return (PtcUiRect){624, 66, 44, 30};
}

PtcUiRect ptc_ui_day_decision_next_rect(void)
{
    return (PtcUiRect){852, 66, 44, 30};
}

PtcUiRect ptc_ui_home_details_body_rect(void) { return (PtcUiRect){40, 72, 1200, 552}; }

/* 孩子区使用详情顶部的导读条。它不是控件，只是把阅读顺序和“三条规则各自独立”
 * 固定成一条可被预览与布局测试核对的矩形，避免绘制端再各写一套坐标。 */
PtcUiRect ptc_ui_home_details_guide_rect(void) { return (PtcUiRect){40, 72, 1200, 32}; }

PtcUiRect ptc_ui_rules_independent_tip_rect(const PtcUiModel *model)
{
    if (model && model->overlay == PTC_UI_OVERLAY_DAY_DECISION)
        return (PtcUiRect){124, 354, 1032, 26};
    return (PtcUiRect){56, 358, 1168, 26};
}

PtcUiRect ptc_ui_rules_dialog_rect(void)
{
    return (PtcUiRect){140, 80, 1000, 560};
}

PtcUiRect ptc_ui_rules_dialog_close_rect(void)
{
    return (PtcUiRect){140 + (1000 - 220) / 2, 80 + 560 - 58, 220, 44};
}

PtcUiRect ptc_ui_home_details_action_rect(const PtcUiModel *model, int index)
{
    if (!model) return (PtcUiRect){0, 0, 0, 0};
    if (index == 1) return (PtcUiRect){276, 628, 170, 44};
    if (index == 2) return (PtcUiRect){40, 628, 220, 44};
    if (index == 0) {
        if (model->home_details_page == 0) {
            /* 孩子区多看一条导读条，正文整体下移 32px；按钮同时收薄 16px 以保持
             * 与家长区相同的底边（616）。绘制与命中都走这里，避免两侧各写一份坐标。 */
            if (model->view == PTC_UI_CHILD) return (PtcUiRect){796, 514, 444, 102};
            return (PtcUiRect){796, 498, 444, 118};
        }
        if (model->home_details_page == 1) return ptc_ui_rules_independent_tip_rect(model);
    }
    return (PtcUiRect){0, 0, 0, 0};
}

PtcUiRect ptc_ui_home_details_data_rect(const PtcUiModel *model, int index)
{
    (void)model;
    (void)index;
    return (PtcUiRect){0, 0, 0, 0};
}

PtcUiRect ptc_ui_advanced_back_rect(void)
{
    return (PtcUiRect){54, 120, 192, 38};
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

PtcUiRect ptc_ui_holiday_manage_rect(void)
{
    return (PtcUiRect){862, 590, 336, 48};
}

PtcUiRect ptc_ui_calendar_manager_tab_rect(int index)
{
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){110 + index * 250, 113, 236, 42};
}

PtcUiRect ptc_ui_calendar_manager_row_rect(int index)
{
    if (index < 0 || index >= 6) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){110, 176 + index * 68, 520, 60};
}

PtcUiRect ptc_ui_calendar_manager_nav_rect(int index)
{
    if (index < 0 || index > 5) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){110 + index * 178, 596, 170, 48};
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

PtcUiRect ptc_ui_support_guide_rect(void) { return (PtcUiRect){868, 602, 332, 26}; }

PtcUiRect ptc_ui_support_guide_nav_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_SUPPORT_GUIDE);
    return (PtcUiRect){dialog.x + 34 + index * 250, dialog.y + dialog.h - 72, 226, 48};
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
    case PTC_UI_OVERLAY_SUPPORT_GUIDE:
        *width = 1100;
        *height = 610;
        break;
    case PTC_UI_OVERLAY_SETUP_PCTL_HELP:
        *width = 1060;
        *height = 570;
        break;
    case PTC_UI_OVERLAY_NOTICE_DETAILS:
        *width = 780;
        *height = 420;
        break;
    case PTC_UI_OVERLAY_HOME_DETAILS:
        *width = 1240;
        *height = 684;
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
    case PTC_UI_OVERLAY_DAY_DECISION:
    case PTC_UI_OVERLAY_CALENDAR_MANAGER:
        *width = 1120;
        *height = 640;
        break;
    case PTC_UI_OVERLAY_CALENDAR_FORMAT:
        *width = 1000;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_AUTONOMY:
    case PTC_UI_OVERLAY_EYE_CARE:
        *width = 880;
        *height = 480;
        break;
    case PTC_UI_OVERLAY_BEDTIME:
        *width = 860;
        *height = 540;
        break;
    case PTC_UI_OVERLAY_QR:
        *width = 1160;
        *height = 700;
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
        *width = 760;
        *height = 360;
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
        *width = 1160;
        *height = 600;
        break;
    case PTC_UI_OVERLAY_LANGUAGE:
        *width = 820;
        *height = 360;
        break;
    case PTC_UI_OVERLAY_PARENT_EXPORT_RESULT:
        *width = 800;
        *height = 420;
        break;
    case PTC_UI_OVERLAY_BEDTIME_WINDOW:
    case PTC_UI_OVERLAY_BEDTIME_SPECIAL:
        *width = 960;
        *height = 560;
        break;
    case PTC_UI_OVERLAY_DOCK_RULES:
        *width = 1000;
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

PtcUiRect ptc_ui_eye_care_field_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_EYE_CARE);
    if (index == 0) return (PtcUiRect){dialog.x + 48, dialog.y + 120, dialog.w - 96, 64};
    if (index >= 1 && index <= 8) {
        static const int offsets[] = {300, 382, 564, 646};
        int row = index <= 4 ? 0 : 1;
        return (PtcUiRect){dialog.x + 48 + offsets[(index - 1) % 4],
            dialog.y + 194 + row * 74, 72, 64};
    }
    return (PtcUiRect){0, 0, 0, 0};
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

/* Safety & preferences rows that open a full-page secondary view instead of a
   modal dialog. Their confirm/cancel anchors live in the page footer, so the
   existing overlay-button hit tests keep working without a second geometry. */
static bool settings_page_overlay(PtcUiOverlay overlay)
{
    return overlay == PTC_UI_OVERLAY_THEME || overlay == PTC_UI_OVERLAY_LANGUAGE ||
           overlay == PTC_UI_OVERLAY_SHORTCUT_MANAGER || overlay == PTC_UI_OVERLAY_ALBUM_MANAGER ||
           overlay == PTC_UI_OVERLAY_ACTIVITY_HISTORY || overlay == PTC_UI_OVERLAY_SOFTWARE_INFO ||
           overlay == PTC_UI_OVERLAY_GRANT_MANAGER;
}

PtcUiRect ptc_ui_confirm_rect(PtcUiOverlay overlay)
{
    PtcUiRect dialog = ptc_ui_dialog_for(overlay);
    if (settings_page_overlay(overlay)) {
        /* The album, activity and grant manager pages have no single "confirm" action; their
           A key acts on the focused row instead. */
        if (overlay == PTC_UI_OVERLAY_ALBUM_MANAGER || overlay == PTC_UI_OVERLAY_ACTIVITY_HISTORY ||
            overlay == PTC_UI_OVERLAY_GRANT_MANAGER)
            return (PtcUiRect){0, 0, 0, 0};
        return ptc_ui_settings_page_primary_rect();
    }
    if (overlay == PTC_UI_OVERLAY_HOME_DETAILS || overlay == PTC_UI_OVERLAY_NOTICE_DETAILS ||
        overlay == PTC_UI_OVERLAY_SETUP_PCTL_HELP || overlay == PTC_UI_OVERLAY_SUPPORT_GUIDE)
        return (PtcUiRect){0, 0, 0, 0};
    if (overlay == PTC_UI_OVERLAY_AUTH_ERROR)
        return (PtcUiRect){dialog.x + dialog.w - 36 - 210, dialog_button_top(dialog), 210, PTC_UI_DIALOG_BTN_H};
    PtcUiRect rect = {dialog.x + dialog.w - 24 - PTC_UI_DIALOG_BTN_W, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    return rect;
}

PtcUiRect ptc_ui_cancel_rect(PtcUiOverlay overlay)
{
    PtcUiRect dialog = ptc_ui_dialog_for(overlay);
    if (settings_page_overlay(overlay)) return ptc_ui_settings_page_back_rect();
    if (overlay == PTC_UI_OVERLAY_AUTH_ERROR)
        return (PtcUiRect){dialog.x + 36, dialog_button_top(dialog), 210, PTC_UI_DIALOG_BTN_H};
    if (overlay == PTC_UI_OVERLAY_GRANT_LOCAL)
        return (PtcUiRect){dialog.x + 42, dialog.y + 588, 210, 44};
    if (overlay == PTC_UI_OVERLAY_HOME_DETAILS || overlay == PTC_UI_OVERLAY_DAY_DECISION)
        return (PtcUiRect){dialog.x + dialog.w - 28 - PTC_UI_DIALOG_BTN_W, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    if (overlay == PTC_UI_OVERLAY_NOTICE_DETAILS || overlay == PTC_UI_OVERLAY_CALENDAR_FORMAT ||
        overlay == PTC_UI_OVERLAY_SETUP_PCTL_HELP || overlay == PTC_UI_OVERLAY_DOCK_RULES)
        return (PtcUiRect){dialog.x + (dialog.w - PTC_UI_DIALOG_BTN_W) / 2, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    PtcUiRect rect = {dialog.x + dialog.w - 24 - PTC_UI_DIALOG_BTN_W * 2 - 16, dialog_button_top(dialog), PTC_UI_DIALOG_BTN_W, PTC_UI_DIALOG_BTN_H};
    return rect;
}

PtcUiRect ptc_ui_auth_error_guide_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_AUTH_ERROR);
    return (PtcUiRect){dialog.x + (dialog.w - 210) / 2, dialog_button_top(dialog), 210, PTC_UI_DIALOG_BTN_H};
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
    int column;
    int row;
    PtcUiRect rect;
    if (index < 0 || index >= PTC_UI_GRANT_MANAGER_COUNT) return (PtcUiRect){0, 0, 0, 0};
    column = index % 2;
    row = index / 2;
    rect = (PtcUiRect){54 + column * 598, 186 + row * 136, 574, 116};
    return rect;
}

PtcUiRect ptc_ui_grant_generate_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_GRANT_LOCAL);
    PtcUiRect rect = {dialog.x + 42, dialog.y + 530, dialog.w - 84, 46};
    return rect;
}

PtcUiRect ptc_ui_minute_editor_key_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTE_EDITOR);
    int row = index / 3;
    int column = index % 3;
    PtcUiRect rect = {dialog.x + 36 + column * 116, dialog.y + 280 + row * 52, 106, 44};
    if (index < 0 || index >= 12) return (PtcUiRect){0, 0, 0, 0};
    return rect;
}

PtcUiRect ptc_ui_minute_editor_quick_rect(int index)
{
    (void)index;
    return (PtcUiRect){0, 0, 0, 0};
}

PtcUiRect ptc_ui_minute_editor_field_rect(PtcUiDurationField field)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTE_EDITOR);
    if (field == PTC_UI_DURATION_HOURS) return (PtcUiRect){dialog.x + 36, dialog.y + 166, 162, 56};
    if (field == PTC_UI_DURATION_MINUTES) return (PtcUiRect){dialog.x + 212, dialog.y + 166, 162, 56};
    return (PtcUiRect){0, 0, 0, 0};
}

PtcUiRect ptc_ui_minute_editor_summary_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTE_EDITOR);
    return (PtcUiRect){dialog.x + 414, dialog.y + 150, 470, 350};
}

PtcUiRect ptc_ui_today_mode_rect(int index)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_MINUTE_EDITOR);
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    int w = 848;
    return (PtcUiRect){dialog.x + 36 + 4 + index * ((w - 8) / 2), dialog.y + 76 + 4, (w - 8) / 2, 36};
}

PtcUiRect ptc_ui_today_limit_refresh_rect(void)
{
    PtcUiRect summary = ptc_ui_minute_editor_summary_rect();
    return (PtcUiRect){summary.x, summary.y + 196, summary.w, 44};
}

PtcUiRect ptc_ui_quota_refresh_rect(void)
{
    PtcUiRect dialog = ptc_ui_dialog_for(PTC_UI_OVERLAY_CONFIRM);
    return (PtcUiRect){dialog.x + 54, dialog.y + 310, dialog.w - 108, 28};
}

PtcUiRect ptc_ui_shortcut_option_rect(int index)
{
    PtcUiRect rect = {54 + (index / 7) * 594, 176 + (index % 7) * 50, 578, 44};
    if (index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

PtcUiRect ptc_ui_shortcut_disable_rect(void)
{
    return (PtcUiRect){54, 560, 574, 44};
}

PtcUiRect ptc_ui_shortcut_hint_rect(void)
{
    return (PtcUiRect){652, 560, 574, 44};
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
    if (index < 0 || index > 1) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){54 + index * 594, 272, 578, 300};
}

PtcUiRect ptc_ui_album_refresh_rect(void)
{
    return (PtcUiRect){1000, 190, 210, 44};
}

PtcUiRect ptc_ui_theme_option_rect(int index)
{
    if (index < 0 || index >= 3) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){54 + index * 396, 178, 380, 226};
}

PtcUiRect ptc_ui_theme_style_rect(int index)
{
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){54 + index * 297, 452, 281, 174};
}

PtcUiRect ptc_ui_language_option_rect(int index)
{
    int col;
    int row;
    if (index < 0 || index >= 4) return (PtcUiRect){0, 0, 0, 0};
    col = index % 2;
    row = index / 2;
    return (PtcUiRect){54 + col * 598, 186 + row * 228, 574, 210};
}

PtcUiRect ptc_ui_dock_field_rect(int index)
{
    /* Eden simulation buttons sit between the rule card and the footer divider. */
    if (index == 7 || index == 8) return (PtcUiRect){54 + (index - 7) * 384, 602, 364, 40};
    /* "Rules explained" pill sits in the bottom-right corner of the rules card. */
    if (index == 5) return (PtcUiRect){1026, 548, 184, 34};
    if (index < 0 || index > 4) return (PtcUiRect){0,0,0,0};
    /* Three mode rows inside the left configuration card (Y=176..596). */
    if (index < 3) return (PtcUiRect){66, 276 + index * 88, 720, 82};
    /* Save (3) and waive-today (4) live in the fixed footer action row. */
    if (index == 4) return (PtcUiRect){810, 664, 200, 44};
    return (PtcUiRect){1026, 664, 200, 44};
}

PtcUiRect ptc_ui_today_status_rect(int index)
{
    PtcUiRect box = ptc_ui_home_summary_rect(true);
    if (index < 8 || index > 10) return (PtcUiRect){0, 0, 0, 0};
    return (PtcUiRect){box.x + 22, box.y + 212 + (index - 8) * 58, box.w - 44, index == 10 ? 70 : 52};
}

PtcUiRect ptc_ui_config_backup_field_rect(int index)
{
    if (index == 16) return (PtcUiRect){54, 120, 190, 38};
    if (index == 17) return (PtcUiRect){254, 120, 190, 38};
    if (index >= 0 && index < 4) return (PtcUiRect){70 + (index % 2) * 364, 210 + (index / 2) * 44, 348, 38};
    if (index >= 4 && index < 9) return (PtcUiRect){70 + ((index - 4) % 2) * 364, 354 + ((index - 4) / 2) * 44, 348, 38};
    if (index == 9 || index == 10) return (PtcUiRect){70 + (index - 9) * 364, 542, 348, 38};
    if (index == 11 || index == 12) return (PtcUiRect){54 + (index - 11) * 190, 602, 175, 38};
    if (index == 13) return (PtcUiRect){54, 552, 744, 58};
    if (index == 14) return (PtcUiRect){824, 552, 402, 58};
    if (index == 15) return (PtcUiRect){54, 664, 170, 44};
    return (PtcUiRect){0, 0, 0, 0};
}

PtcUiRect ptc_ui_config_backup_group_rect(int group)
{
    if (group == 0) return (PtcUiRect){54, 172, 744, 132};
    if (group == 1) return (PtcUiRect){54, 316, 744, 176};
    if (group == 2) return (PtcUiRect){54, 504, 744, 88};
    return (PtcUiRect){0, 0, 0, 0};
}

