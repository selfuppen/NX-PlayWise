#include "ui_render_internal.h"

/* 24 小时可视化时间轴使用的家庭常用就寝窗口。 */
static const struct {
    PtcUiTextId name;
    uint16_t start;
    uint16_t end;
} PTC_BEDTIME_PRESETS[] = {
    {PTC_UI_T_EARLY_TO_BED_TYPE, 21 * 60, 7 * 60},
    {PTC_UI_T_STANDARD_TYPE, 21 * 60 + 30, 7 * 60},
    {PTC_UI_T_BALANCED_TYPE, 22 * 60, 7 * 60},
    {PTC_UI_T_WEEKEND_TYPE, 22 * 60 + 30, 7 * 60 + 30},
    {PTC_UI_T_LOOSE_TYPE, 23 * 60, 8 * 60}
};

static void draw_scheduled_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcScheduledOverride *draft = &model->draft_scheduled_override;
    uint32_t duration = draft->end_day_index >= draft->start_day_index
        ? (uint32_t)draft->end_day_index - draft->start_day_index + 1u : 1u;
    uint16_t s_year = 0, e_year = 0;
    uint8_t s_month = 0, s_day = 0, e_month = 0, e_day = 0;
    bool s_ok = ptc_date_from_day_index(draft->start_day_index, &s_year, &s_month, &s_day);
    bool e_ok = ptc_date_from_day_index(draft->end_day_index, &e_year, &e_month, &e_day);
    const char *LABELS[] = {ptc_ui_text(PTC_UI_T_PLAN_STATUS), ptc_ui_text(PTC_UI_T_START_DATE), ptc_ui_text(PTC_UI_T_NUMBER_OF_DAYS_TO_LAST), ptc_ui_text(PTC_UI_T_TOTAL_DAILY_QUOTA)};
    char values[4][128];
    char banner_text[192];
    bool save_danger = ptc_ui_scheduled_dirty(model) &&
        ptc_ui_plan_save_requires_hold(model, PTC_UI_PLAN_SCHEDULED, ptc_ui_render_now());
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 640);

    snprintf(values[0], sizeof(values[0]), "%s",
             draft->enabled ? ptc_ui_text(PTC_UI_T_IS_ENABLED_PLAN_IS_IN_EFFECT) : ptc_ui_text(PTC_UI_T_CLOSED_NOT_EFFECTIVE_YET));
    if (s_ok) {
        snprintf(values[1], sizeof(values[1]), "%04u-%02u-%02u%s", s_year, s_month, s_day,
                 model->status_loaded && draft->start_day_index == model->day_index ? ptc_ui_text(PTC_UI_T_TODAY_2) : "");
    } else {
        snprintf(values[1], sizeof(values[1]), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    }
    snprintf(values[2], sizeof(values[2]), ptc_ui_text(PTC_UI_T_U_DAYS_UP_TO_366), (unsigned int)duration);
    if (draft->rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(values[3], sizeof(values[3]), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    } else {
        snprintf(values[3], sizeof(values[3]), ptc_ui_text(PTC_UI_T_U_MIN_U_HR_U_MIN),
                 (unsigned int)draft->rule.minutes,
                 (unsigned int)draft->rule.minutes / 60,
                 (unsigned int)draft->rule.minutes % 60);
    }

    int max_label_w = 0;
    for (int i = 0; i < 4; ++i) {
        int lw = measure_text(LABELS[i], 16);
        if (lw > max_label_w) max_label_w = lw;
    }

    for (int index = 0; index < 4; ++index) {
        UiRect row = to_uirect(ptc_ui_scheduled_field_rect(index));
        draw_plan_card(pixels, stride, row, model->overlay_selection == index);
        draw_text(pixels, stride, row.x + 18, row.y + 38, LABELS[index], 16, UI_MUTED);

        int val_x = row.x + 18 + max_label_w + 16;
        if (val_x < row.x + 130) val_x = row.x + 130;

        if (index == 0) {
            draw_text(pixels, stride, val_x, row.y + 38, values[0], 20,
                       draft->enabled ? UI_SUCCESS : UI_MUTED);
            UiRect toggle_rect = {row.x + row.width - 78, row.y + (row.height - 30) / 2, 60, 30};
            draw_toggle_switch(pixels, stride, toggle_rect, draft->enabled,
                                model->overlay_selection == 0, model->disable_flag_present, NULL, NULL);
        } else if (index == 1 || index == 2) {
            draw_text(pixels, stride, val_x, row.y + 38, values[index], 20, UI_RGB(UI_BLENDED(text_primary)));
            UiRect edit_chip = {row.x + row.width - 86, row.y + (row.height - 28) / 2, 70, 28};
            fill_round_rect(pixels, stride, edit_chip, 6, UI_RGB(UI_BLENDED(surface)));
            draw_rect_outline(pixels, stride, edit_chip, 6, 1, UI_BORDER);
            draw_text_center(pixels, stride, edit_chip, ptc_ui_text(PTC_UI_T_A_EDITOR), 13, UI_MUTED);
        } else {
            draw_text(pixels, stride, val_x, row.y + 38, values[index], 20,
                       draft->rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT);
            UiRect edit_chip = {row.x + row.width - 86, row.y + (row.height - 28) / 2, 70, 28};
            fill_round_rect(pixels, stride, edit_chip, 6, UI_RGB(UI_BLENDED(surface)));
            draw_rect_outline(pixels, stride, edit_chip, 6, 1, UI_BORDER);
            draw_text_center(pixels, stride, edit_chip, ptc_ui_text(PTC_UI_T_A_EDITOR), 13, UI_MUTED);
        }
    }

    /* 右侧预估面板 */
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_SCHEDULED, ptc_ui_scheduled_dirty(model),
                     (UiRect){dialog.x + 626, dialog.y + 116, 460, 306});

    /* 日期区间视觉横幅 (Date Range Visual Banner) */
    UiRect banner = {dialog.x + 34, dialog.y + 424, dialog.width - 68, 44};
    fill_round_rect(pixels, stride, banner, 10, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, banner, 10, 1, UI_RGB(UI_BLENDED(border_control)));
    if (s_ok && e_ok) {
        bool today_in_range = model->status_loaded &&
            model->day_index >= draft->start_day_index &&
            model->day_index <= draft->end_day_index;
        const char *range_status = draft->enabled
            ? (today_in_range ? ptc_ui_text(PTC_UI_T_WITHIN_THE_RANGE_TODAY_IN_EFFECT) :
               (model->day_index < draft->start_day_index ? ptc_ui_text(PTC_UI_T_PLAN_WILL_BE_EFFECTIVE_IN_THE_FUTURE) : ptc_ui_text(PTC_UI_T_PLAN_HAS_EXPIRED)))
            : ptc_ui_text(PTC_UI_T_THE_MASTER_PLAN_SWITCH_IS_NOT_TURNED);
        snprintf(banner_text, sizeof(banner_text),
                 ptc_ui_text(PTC_UI_T_RANGE_04U_02U_02U_TO_04U_02U),
                 s_year, s_month, s_day, e_year, e_month, e_day, (unsigned int)duration, range_status);
        draw_text(pixels, stride, banner.x + 16, banner.y + 27, banner_text, 15,
                  draft->enabled && today_in_range ? UI_SUCCESS : UI_RGB(UI_BLENDED(text_primary)));
    } else {
        draw_text(pixels, stride, banner.x + 16, banner.y + 27,
                  ptc_ui_text(PTC_UI_T_THE_PLANNING_INTERVAL_IS_BEING_CALCULATED_PLEASE), 15, UI_MUTED);
    }

    /* 规则优先级说明 */
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 482,
              ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_IS_IN_SEQUENCE_TODAY),
              14, UI_RGB(UI_BLENDED(text_secondary)));

    /* 动态上下文按键引导栏 (Context-Aware Action Guide Bar) */
    UiRect guide_bar = {dialog.x + 34, dialog.y + 508, dialog.width - 68, 30};
    fill_round_rect(pixels, stride, guide_bar, 6, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, guide_bar, 6, 1, UI_BORDER);

    char guide_text[160];
    if (model->overlay_selection == 0) {
        snprintf(guide_text, sizeof(guide_text), ptc_ui_text(PTC_UI_T_ACTION_PRESS_A_OR_TAP_TO_SWITCH));
    } else if (model->overlay_selection == 1) {
        snprintf(guide_text, sizeof(guide_text), ptc_ui_text(PTC_UI_T_OPERATION_LEFT_AND_RIGHT_KEYS_1_DAY));
    } else if (model->overlay_selection == 2) {
        snprintf(guide_text, sizeof(guide_text), ptc_ui_text(PTC_UI_T_OPERATION_LEFT_AND_RIGHT_KEYS_1_DAY_2));
    } else if (model->overlay_selection == 3) {
        snprintf(guide_text, sizeof(guide_text), ptc_ui_text(PTC_UI_T_OPERATION_X_SWITCH_BETWEEN_UNLIMITED_LIMITED_TIME));
    } else {
        snprintf(guide_text, sizeof(guide_text), ptc_ui_text(PTC_UI_T_OPERATION_USE_UP_AND_DOWN_KEYS_TO));
    }
    draw_text(pixels, stride, guide_bar.x + 12, guide_bar.y + 20, guide_text, 13, UI_ACCENT);

    if (strcmp(model->result_status, "error") == 0) {
        draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 538,
                          ptc_ui_text(PTC_UI_T_THE_LAST_OPERATION_WAS_NOT_COMPLETED_AND),
                          16, dialog.width - 68, 22, 2, UI_RGB(UI_BLENDED(danger)));
    }

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK), UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       model->waiting ? ptc_ui_text(PTC_UI_T_SAVING) : (ptc_ui_scheduled_dirty(model)
                           ? (save_danger ? ptc_ui_text(PTC_UI_T_SAVE_MAY_RESTRICT_USE_IMMEDIATELY) : ptc_ui_text(PTC_UI_T_SAVE_DRAFT)) : ptc_ui_text(PTC_UI_T_SAVED)),
                       save_danger ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_scheduled_leave(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel copy = *model;
    snprintf(copy.overlay_title, sizeof(copy.overlay_title), ptc_ui_text(PTC_UI_T_GIVE_UP_THE_DRAFT_QUOTA_FOR_A));
    snprintf(copy.overlay_body, sizeof(copy.overlay_body), ptc_ui_text(PTC_UI_T_UNSAVED_MODIFICATIONS_WILL_BE_LOST_AND_SAVED));
    draw_dialog_shell(pixels, stride, &copy, &dialog, 720, 300);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_CONTINUE_EDITING),
                       UI_RAISED, UI_ACCENT, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_ABANDON_DRAFT),
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_autonomy_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    static const uint16_t OPTIONS[] = {0, 5, 10, 15};
    const char *SUBTITLES[] = {ptc_ui_text(PTC_UI_T_DOES_NOT_OPEN_BUFFERING), ptc_ui_text(PTC_UI_T_QUICK_SAVE), ptc_ui_text(PTC_UI_T_CLOSING_RECOMMENDATIONS), ptc_ui_text(PTC_UI_T_ABUNDANT_EXIT)};
    int index;
    char label[48];
    PtcUiModel shell_model = *model;
    shell_model.overlay_body[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 880, 480);

    /* 详细用途与机制说明 */
    char fitted_buf[256];
    fit_text(fitted_buf, sizeof(fitted_buf),
             ptc_ui_text(PTC_UI_T_PURPOSE_WHEN_THE_QUOTA_IS_EXHAUSTED_CHILDREN),
             14, dialog.width - 96);
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 92, fitted_buf, 14, UI_INK);

    fit_text(fitted_buf, sizeof(fitted_buf),
             ptc_ui_text(PTC_UI_T_CAN_ONLY_BE_COLLECTED_ONCE_A_DAY),
             14, dialog.width - 96);
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 118, fitted_buf, 14, UI_MUTED);

    fit_text(fitted_buf, sizeof(fitted_buf),
             ptc_ui_text(PTC_UI_T_RECOMMENDED_SETTINGS_5_TO_15_MINUTES_ARE),
             14, dialog.width - 96);
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 144, fitted_buf, 14, UI_ACCENT);

    for (index = 0; index < 4; ++index) {
        UiRect option = to_uirect(ptc_ui_autonomy_option_rect(index));
        bool selected = model->draft_autonomy_policy.daily_buffer_minutes == OPTIONS[index];
        if (OPTIONS[index] > 0u) {
            snprintf(label, sizeof(label), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned int)OPTIONS[index]);
        } else {
            snprintf(label, sizeof(label), ptc_ui_text(PTC_UI_T_OFF));
        }
        fill_round_rect(pixels, stride, option, 12, selected ? UI_SUCCESS_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 12, selected ? 2 : 1, selected ? UI_SUCCESS : UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){option.x, option.y + 12, option.width, 32},
                         label, 20, selected ? UI_SUCCESS : UI_INK);
        draw_text_center(pixels, stride, (UiRect){option.x, option.y + 46, option.width, 24},
                         SUBTITLES[index], 13, selected ? UI_SUCCESS : UI_MUTED);
    }

    /* 底部保障与生效提示卡片 */
    UiRect tip_box = {dialog.x + 48, dialog.y + 296, dialog.width - 96, 76};
    fill_round_rect(pixels, stride, tip_box, 8, UI_PAGE);
    draw_rect_outline(pixels, stride, tip_box, 8, 1, UI_BORDER);
    fit_text(fitted_buf, sizeof(fitted_buf),
             ptc_ui_text(PTC_UI_T_THE_BUFFER_ONLY_INCREASES_TODAY_S_QUOTA),
             14, tip_box.width - 32);
    draw_text(pixels, stride, tip_box.x + 16, tip_box.y + 28, fitted_buf, 14, UI_INK);
    fit_text(fitted_buf, sizeof(fitted_buf),
             ptc_ui_text(PTC_UI_T_BEDTIME_TIME_REMAINS_AS_SCHEDULED),
             14, tip_box.width - 32);
    draw_text(pixels, stride, tip_box.x + 16, tip_box.y + 54, fitted_buf, 14, UI_WARNING);

    draw_overlay_actions(pixels, stride, model, ptc_ui_text(PTC_UI_T_SAVE_BUFFER_SETTINGS));
}

static void draw_eye_care_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char value[64];
    int row;
    PtcUiModel shell_model = *model;
    shell_model.overlay_body[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 880, 480);
    for (row = 0; row < 3; ++row) {
        UiRect rect = {dialog.x + 48, dialog.y + 120 + row * 74, dialog.width - 96, 64};
        bool focused = model->overlay_selection == row;
        fill_round_rect(pixels, stride, rect, 10, focused ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, rect, 10, focused ? 2 : 1, focused ? UI_ACCENT : UI_BORDER);
        draw_text(pixels, stride, rect.x + 16, rect.y + 38,
            row == 0 ? ptc_ui_text(PTC_UI_T_EYE_CARE) :
            (row == 1 ? ptc_ui_text(PTC_UI_T_EYE_CARE_PLAY) : ptc_ui_text(PTC_UI_T_EYE_CARE_REST)),
            18, UI_INK);
        if (row == 0) snprintf(value, sizeof(value), "%s",
            model->draft_eye_care_policy.enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF));
        else snprintf(value, sizeof(value), "%u %s",
            (unsigned)(row == 1 ? model->draft_eye_care_policy.play_minutes :
                model->draft_eye_care_policy.rest_minutes), ptc_ui_text(PTC_UI_T_MIN));
        if (row == 0) draw_text(pixels, stride, rect.x + 480, rect.y + 38, value, 18, UI_ACCENT);
        else {
            static const char *steps[] = {"-10", "-1", "+1", "+10"};
            int first = row == 1 ? 1 : 5;
            draw_text_center(pixels, stride,
                (UiRect){rect.x + 464, rect.y + 14, 96, 36}, value, 18, UI_ACCENT);
            for (int step = 0; step < 4; ++step)
                draw_candidate_button(pixels, stride,
                    ptc_ui_eye_care_field_rect(first + step), steps[step],
                    UI_PAGE, UI_INK, focused, false);
        }
    }
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 370,
        ptc_ui_text(PTC_UI_T_EYE_CARE_GUIDE), 14, UI_MUTED);
    if (model->draft_eye_care_policy.enabled && !model->bedtime_official_setting_confirmed)
        draw_text(pixels, stride, dialog.x + 48, dialog.y + 397,
            ptc_ui_text(PTC_UI_T_EYE_CARE_ENABLE_CONFIRM), 12, UI_WARNING);
    draw_overlay_actions(pixels, stride, model, ptc_ui_text(PTC_UI_T_EYE_CARE_SAVE));
}

static void draw_quick_add_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const char *LABELS[] = {ptc_ui_text(PTC_UI_T_15_MINUTES), ptc_ui_text(PTC_UI_T_30_MINUTES), ptc_ui_text(PTC_UI_T_60_MINUTES), ptc_ui_text(PTC_UI_T_CUSTOM)};
    draw_dialog_shell(pixels, stride, model, &dialog, 760, 420);
    for (int index = 0; index < 4; ++index) {
        UiRect option = to_uirect(ptc_ui_quick_add_option_rect(index));
        bool selected = model->overlay_selection == index;
        fill_round_rect(pixels, stride, option, 12, selected ? UI_SUCCESS_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 12, selected ? 2 : 1,
            selected ? UI_SUCCESS : UI_BORDER);
        draw_text_center(pixels, stride, option, LABELS[index], 20,
            selected ? UI_SUCCESS : UI_INK);
    }
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 284,
        ptc_ui_text(PTC_UI_T_ALSO_DISPLAYS_A_CONFIRMATION_PAGE_AFTER_SELECTION), 16, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_CANCEL),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_CONTINUE),
        UI_SUCCESS, UI_ON_ACCENT, false);
}

static void draw_bedtime_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    char values[4][96];
    draw_dialog_shell(pixels, stride, model, &dialog, 860, 540);
    snprintf(values[0], sizeof(values[0]), ptc_ui_text(PTC_UI_T_BEDTIME_PLAN_S), draft->enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF));
    snprintf(values[1], sizeof(values[1]), ptc_ui_text(PTC_UI_T_STARTS_DAILY_02U_02U),
        (unsigned int)(draft->week[0].start_minute / 60),
        (unsigned int)(draft->week[0].start_minute % 60));
    snprintf(values[2], sizeof(values[2]), ptc_ui_text(PTC_UI_T_ENDS_NEXT_DAY_02U_02U),
        (unsigned int)(draft->week[0].end_minute / 60),
        (unsigned int)(draft->week[0].end_minute % 60));
    snprintf(values[3], sizeof(values[3]), "%s",
        model->bedtime_active && !model->bedtime_skipped
            ? ptc_ui_text(PTC_UI_T_IN_EFFECT_CAN_ONLY_BE_RESTORED_FROM)
            : ptc_ui_text(PTC_UI_T_AFTER_THE_RESTRICTION_TAKES_EFFECT_PLAYWISE_NRO));
    for (int index = 0; index < 4; ++index) {
        UiRect row = {dialog.x + 42, dialog.y + 118 + index * 70, dialog.width - 84, 56};
        draw_plan_card(pixels, stride, row, model->overlay_selection == index);
        draw_text(pixels, stride, row.x + 18, row.y + 35, values[index], 20,
            index == 3 && model->bedtime_active && !model->bedtime_skipped ? UI_WARNING : UI_INK);
    }
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 402,
        ptc_ui_text(PTC_UI_T_DIRECTION_KEY_SELECTION_A_OPENS_THE_HOUR), 16, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 434,
        model->bedtime_official_setting_confirmed
            ? (model->bedtime_overlay_verified ? ptc_ui_text(PTC_UI_T_ENVIRONMENT_CONFIRMED_OVERLAY_VERIFIED) : ptc_ui_text(PTC_UI_T_THE_ENVIRONMENT_HAS_BEEN_CONFIRMED_BY_CONTINUING))
            : ptc_ui_text(PTC_UI_T_BEFORE_ENABLING_CONFIRM_SUSPEND_SOFTWARE_IN_NINTENDO),
        14, model->bedtime_official_setting_confirmed ? UI_MUTED : UI_WARNING);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_SAVE_AND_TAKE_EFFECT),
        UI_WARNING, UI_ON_ACCENT, false);
}

static void draw_bedtime_timeline_strip(
    uint32_t *pixels, uint32_t stride,
    UiRect bar, uint16_t start_m, uint16_t end_m, bool enabled)
{
    fill_round_rect(pixels, stride, bar, 6, UI_PAGE);
    draw_rect_outline(pixels, stride, bar, 6, 1, UI_BORDER);

    if (!enabled) {
        draw_text_center(pixels, stride, bar, ptc_ui_text(PTC_UI_T_TODAY_S_BEDTIME_RESTRICTION_IS_NOT_ENABLED), 13, UI_MUTED);
    } else {
        int x_morn_end = bar.x + (int)((float)end_m / 1440.0f * (float)bar.width + 0.5f);
        int x_eve_start = bar.x + (int)((float)start_m / 1440.0f * (float)bar.width + 0.5f);
        if (x_morn_end < bar.x) x_morn_end = bar.x;
        if (x_morn_end > bar.x + bar.width) x_morn_end = bar.x + bar.width;
        if (x_eve_start < bar.x) x_eve_start = bar.x;
        if (x_eve_start > bar.x + bar.width) x_eve_start = bar.x + bar.width;

        /* 清晨就寝区间 [bar.x, x_morn_end] */
        if (x_morn_end > bar.x) {
            UiRect morn_rect = {bar.x, bar.y, x_morn_end - bar.x, bar.height};
            fill_round_rect(pixels, stride, morn_rect, 6, UI_RGB(0x403264));
            if (morn_rect.width > 44) {
                draw_text_center(pixels, stride, morn_rect, ptc_ui_text(PTC_UI_T_GO_TO_BED_EARLY_IN_THE_MORNING), 11, UI_RGB(0xdfd8f5));
            }
        }

        /* 晚间就寝区间 [x_eve_start, bar.x + bar.width] */
        if (x_eve_start < bar.x + bar.width) {
            UiRect eve_rect = {x_eve_start, bar.y, bar.x + bar.width - x_eve_start, bar.height};
            fill_round_rect(pixels, stride, eve_rect, 6, UI_RGB(0x403264));
            if (eve_rect.width > 44) {
                draw_text_center(pixels, stride, eve_rect, ptc_ui_text(PTC_UI_T_NIGHTTIME_BEDTIME), 11, UI_RGB(0xdfd8f5));
            }
        }

        /* 白昼允许游玩区间 [x_morn_end, x_eve_start] */
        if (x_eve_start > x_morn_end) {
            UiRect day_rect = {x_morn_end, bar.y, x_eve_start - x_morn_end, bar.height};
            draw_text_center(pixels, stride, day_rect, ptc_ui_text(PTC_UI_T_ALLOWED_PLAY_TIME_DURING_THE_DAY), 12, UI_RGB(UI_BLENDED(text_primary)));
        }

        /* 立断与解禁指示线 */
        if (x_eve_start >= bar.x && x_eve_start <= bar.x + bar.width) {
            fill_rect(pixels, stride, (UiRect){x_eve_start - 1, bar.y, 2, bar.height}, UI_DANGER);
        }
        if (x_morn_end >= bar.x && x_morn_end <= bar.x + bar.width) {
            fill_rect(pixels, stride, (UiRect){x_morn_end - 1, bar.y, 2, bar.height}, UI_SUCCESS);
        }
    }
}

static int bedtime_matching_preset(const PtcBedtimeWindow *window)
{
    if (!window) return -1;
    for (int preset = 0; preset < 5; ++preset) {
        if (window->start_minute == PTC_BEDTIME_PRESETS[preset].start &&
            window->end_minute == PTC_BEDTIME_PRESETS[preset].end)
            return preset;
    }
    return -1;
}

static void draw_bedtime_editor_fields(
    uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
    const PtcBedtimeWindow *window, const char *mode_label, uint32_t mode_color,
    bool custom, bool show_toggle)
{
    UiRect mode = to_uirect(ptc_ui_bedtime_overlay_field_rect(model->overlay, 0));
    draw_plan_card(pixels, stride, mode, model->overlay_selection == 0);
    draw_text(pixels, stride, mode.x + 18, mode.y + 36,
        show_toggle ? ptc_ui_text(PTC_UI_T_TODAY_S_BEDTIME_LIMIT) : ptc_ui_text(PTC_UI_T_RULE_PATTERN), 18, UI_INK);
    draw_text(pixels, stride, mode.x + 170, mode.y + 36, mode_label, 18, mode_color);
    if (show_toggle) {
        UiRect toggle = {mode.x + mode.width - 76, mode.y + 15, 58, 28};
        draw_toggle_switch(pixels, stride, toggle, window->enabled,
            model->overlay_selection == 0, false, NULL, NULL);
        draw_text(pixels, stride, mode.x + mode.width - 174, mode.y + 36,
            ptc_ui_text(PTC_UI_T_A_X_SWITCH), 13, UI_MUTED);
    } else {
        draw_text(pixels, stride, mode.x + mode.width - 188, mode.y + 36,
            ptc_ui_text(PTC_UI_T_A_X_SWITCH_MODE), 13, UI_MUTED);
    }

    for (int field = 1; field < 3; ++field) {
        UiRect row = to_uirect(ptc_ui_bedtime_overlay_field_rect(model->overlay, field));
        unsigned int minute = field == 1 ? window->start_minute : window->end_minute;
        char value[32];
        draw_plan_card(pixels, stride, row, model->overlay_selection == field);
        draw_text(pixels, stride, row.x + 18, row.y + 24,
            field == 1 ? ptc_ui_text(PTC_UI_T_BEGINS_TO_GO_TO_BED_USE_IS) : ptc_ui_text(PTC_UI_T_ENDS_THE_NEXT_DAY_AND_RESUMES_USE),
            13, custom ? UI_MUTED : UI_DISABLED);
        snprintf(value, sizeof(value), "%02u:%02u", minute / 60, minute % 60);
        draw_text(pixels, stride, row.x + 18, row.y + 52, value, 22,
            custom ? UI_ACCENT : UI_DISABLED);
        draw_text(pixels, stride, row.x + row.width - 108, row.y + 50,
            custom ? ptc_ui_text(PTC_UI_T_A_FINE_ADJUSTMENT) : ptc_ui_text(PTC_UI_T_NOT_EDITABLE), 13, custom ? UI_MUTED : UI_DISABLED);
    }
}

static void draw_bedtime_editor_timeline(
    uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
    const PtcBedtimeWindow *window, bool custom, const char *neutral_text)
{
    UiRect bar = to_uirect(ptc_ui_bedtime_timeline_rect(model->overlay));
    int matched = custom ? bedtime_matching_preset(window) : -1;
    int tick_y = bar.y + bar.height + 16;
    char summary[112];

    if (custom) {
        draw_bedtime_timeline_strip(pixels, stride, bar,
            window->start_minute, window->end_minute, window->enabled);
        draw_text(pixels, stride, bar.x, tick_y, "00:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width / 4 - 14, tick_y, "06:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width / 2 - 14, tick_y, "12:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width * 3 / 4 - 14, tick_y, "18:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width - 32, tick_y, "24:00", 10, UI_MUTED);
        if (window->enabled) {
            int duration = (int)(window->end_minute + 1440 - window->start_minute) % 1440;
            snprintf(summary, sizeof(summary), ptc_ui_text(PTC_UI_T_S_OVERNIGHT_CONTROL_U_HR_U_MIN),
                matched >= 0 ? ptc_ui_text(PTC_BEDTIME_PRESETS[matched].name) : ptc_ui_text(PTC_UI_T_CUSTOM_TIME_PERIOD),
                (unsigned int)(duration / 60), (unsigned int)(duration % 60));
        } else {
            snprintf(summary, sizeof(summary), ptc_ui_text(PTC_UI_T_CLOSED_TODAY_USE_WILL_NOT_BE_RESTRICTED));
        }
        draw_text_center(pixels, stride,
            (UiRect){bar.x + 150, tick_y - 14, bar.width - 300, 28},
            summary, 12, window->enabled ? UI_ACCENT : UI_MUTED);
    } else {
        fill_round_rect(pixels, stride, bar, 6, UI_PAGE);
        draw_rect_outline(pixels, stride, bar, 6, 1, UI_BORDER);
        draw_text_center(pixels, stride, bar, neutral_text, 13, UI_MUTED);
    }

    for (int preset = 0; preset < 5; ++preset) {
        UiRect chip = to_uirect(ptc_ui_bedtime_preset_rect(model->overlay, preset));
        bool selected = custom && matched == preset;
        char times[32];
        fill_round_rect(pixels, stride, chip, 9,
            selected ? UI_ACCENT_SOFT : (custom ? UI_RAISED : UI_PAGE));
        draw_rect_outline(pixels, stride, chip, 9, selected ? 2 : 1,
            selected ? UI_ACCENT : UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){chip.x, chip.y + 3, chip.width, 18},
            ptc_ui_text(PTC_BEDTIME_PRESETS[preset].name), 12,
            custom ? (selected ? UI_ACCENT : UI_INK) : UI_DISABLED);
        snprintf(times, sizeof(times), ptc_ui_text(PTC_UI_T_02U_02U_TO_02U_02U),
            PTC_BEDTIME_PRESETS[preset].start / 60, PTC_BEDTIME_PRESETS[preset].start % 60,
            PTC_BEDTIME_PRESETS[preset].end / 60, PTC_BEDTIME_PRESETS[preset].end % 60);
        draw_text_center(pixels, stride, (UiRect){chip.x, chip.y + 21, chip.width, 18},
            times, 10, custom ? UI_MUTED : UI_DISABLED);
    }
}

static void draw_bedtime_window_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcBedtimeWindow *window = &model->draft_bedtime_policy.week[model->bedtime_editor_day];
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 560);
    draw_bedtime_editor_fields(pixels, stride, model, window,
        window->enabled ? ptc_ui_text(PTC_UI_T_ON) : ptc_ui_text(PTC_UI_T_OFF), window->enabled ? UI_SUCCESS : UI_MUTED,
        true, true);
    draw_bedtime_editor_timeline(pixels, stride, model, window, true, "");
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 424,
        ptc_ui_text(PTC_UI_T_DIRECTION_KEY_SELECTION_FIELD_A_FINE_ADJUSTMENT), 13, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 450,
        ptc_ui_text(PTC_UI_T_BEDTIME_PERIOD_MUST_SPAN_MIDNIGHT_USE_IS), 13, UI_WARNING);

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_COMPLETE),
        UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_bedtime_special_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcBedtimeSpecialRule *rule = model->bedtime_special_kind == 0
        ? &model->draft_bedtime_policy.holiday_rule
        : (model->bedtime_special_kind == 1
            ? &model->draft_bedtime_policy.makeup_workday_rule
            : &model->draft_bedtime_policy.scheduled_override.rule);
    bool custom = rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM;
    uint32_t mode_color = custom ? UI_ACCENT :
        (rule->mode == PTC_BEDTIME_OVERRIDE_DISABLED ? UI_MUTED : UI_SUCCESS);
    const char *neutral = rule->mode == PTC_BEDTIME_OVERRIDE_DISABLED
        ? ptc_ui_text(PTC_UI_T_THERE_IS_NO_SLEEPING_LIMIT_ON_THAT)
        : ptc_ui_text(PTC_UI_T_FOLLOW_THE_WEEKLY_SCHEDULE_USE_THE_BEDTIME);
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 560);
    draw_bedtime_editor_fields(pixels, stride, model, &rule->window,
        bedtime_override_label(rule->mode), mode_color, custom, false);
    draw_bedtime_editor_timeline(pixels, stride, model, &rule->window,
        custom, neutral);
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 424,
        custom
            ? ptc_ui_text(PTC_UI_T_DIRECTION_KEY_SELECTION_FIELD_A_FINE_ADJUSTMENT_2)
            : ptc_ui_text(PTC_UI_T_A_X_CHANGES_MODE_SELECT_CUSTOM_WINDOW),
        13, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 450,
        custom ? ptc_ui_text(PTC_UI_T_THE_CUSTOM_WINDOW_MUST_SPAN_MIDNIGHT_THE)
               : ptc_ui_text(PTC_UI_T_FOLLOW_AND_FREE_MODES_WILL_NOT_USE),
        13, custom ? UI_WARNING : UI_MUTED);

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_COMPLETE),
        UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_bedtime_bulk_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const char *LABELS[] = {ptc_ui_text(PTC_UI_T_MONDAY_TO_FRIDAY), ptc_ui_text(PTC_UI_T_SATURDAY_SUNDAY)};
    draw_dialog_shell(pixels, stride, model, &dialog, 700, 370);
    for (int i = 0; i < 2; ++i) {
        UiRect option = {dialog.x + 46 + i * 304, dialog.y + 138, 284, 82};
        draw_candidate_button(pixels, stride, (PtcUiRect){option.x, option.y, option.width, option.height},
            LABELS[i], UI_PAGE, UI_ACCENT, model->overlay_selection == i, false);
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_CANCEL),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_COPY),
        UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_bedtime_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 780, 390);
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 164,
        ptc_ui_text(PTC_UI_T_THE_DRAFT_REMAINS_UNTIL_SAVED_OR_EXPLICITLY), 17, UI_MUTED);
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay),
        model->bedtime_switch_pending ? ptc_ui_text(PTC_UI_T_X_GIVE_UP_AND_SWITCH) : ptc_ui_text(PTC_UI_T_X_GIVE_UP_AND_LEAVE),
        UI_DANGER_SOFT, UI_DANGER, model->overlay_selection == 1, false);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_CONTINUE_EDITING),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
        model->disable_flag_present ? ptc_ui_text(PTC_UI_T_CANNOT_BE_SAVED_DURING_EMERGENCY_DEACTIVATION) :
            (model->bedtime_switch_pending ? ptc_ui_text(PTC_UI_T_A_SAVE_AND_SWITCH) : ptc_ui_text(PTC_UI_T_A_SAVE_AND_EXIT)),
        UI_ACCENT, UI_ON_ACCENT, model->disable_flag_present);
}

static void draw_weekly_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    bool refreshing = strcmp(model->overlay_title, ptc_ui_text(PTC_UI_T_REFRESH_WEEKLY_PLAN)) == 0;
    bool disabled = model->disable_flag_present;
    draw_dialog_shell(pixels, stride, model, &dialog, 860, 350);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 150, dialog.width - 80, 34},
                     ptc_ui_text(PTC_UI_T_THERE_ARE_UNSAVED_MODIFICATIONS_TO_THE_WEEKLY), 22, UI_WARNING);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 190, dialog.width - 80, 24},
                     ptc_ui_text(PTC_UI_T_SELECT_LEFT_AND_RIGHT_A_CONFIRM_YOU), 17, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_discard_rect(model->overlay),
                       refreshing ? ptc_ui_text(PTC_UI_T_X_GIVE_UP_AND_REFRESH) : ptc_ui_text(PTC_UI_T_X_GIVE_UP_AND_LEAVE),
                       UI_DANGER_SOFT, UI_DANGER, true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), disabled ? ptc_ui_text(PTC_UI_T_B_BACK) : ptc_ui_text(PTC_UI_T_B_CONTINUE_EDITING),
                        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       disabled
                         ? (refreshing ? ptc_ui_text(PTC_UI_T_KEEP_DRAFT_AND_REFRESH) : ptc_ui_text(PTC_UI_T_KEEP_DRAFT_AND_LEAVE))
                         : (refreshing ? ptc_ui_text(PTC_UI_T_SAVE_AND_REFRESH) : ptc_ui_text(PTC_UI_T_SAVE_AND_EXIT)),
                       UI_ACCENT, UI_ON_ACCENT, false);
    if (model->weekly_leave_selection == 0) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_discard_rect(model->overlay)), 12, 3, UI_DANGER);
    } else if (model->weekly_leave_selection == 1) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_cancel_rect(model->overlay)), 12, 3, UI_ACCENT);
    } else {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_confirm_rect(model->overlay)), 12, 3, UI_SURFACE);
    }
}

static bool holiday_is_past_or_today(
    const PtcHolidayArrangement *entry,
    uint16_t cur_year, uint8_t cur_month, uint8_t cur_day,
    bool *is_past_out, bool *is_today_out)
{
    *is_past_out = false;
    *is_today_out = false;
    if (!entry) return false;
    uint8_t last_month = entry->end_month;
    uint8_t last_day = entry->end_day;
    /* Calendar fixture fields have a fixed Chinese data format, independent of UI language. */
    if (entry->makeup_workdays && strcmp(entry->makeup_workdays, "无") != 0) {
        const char *p = entry->makeup_workdays;
        while (*p) {
            if (*p >= '0' && *p <= '9') {
                unsigned int m = 0, d = 0;
                if (sscanf(p, "%u月%u日", &m, &d) == 2) {
                    if (m > last_month || (m == last_month && d > last_day)) {
                        last_month = (uint8_t)m;
                        last_day = (uint8_t)d;
                    }
                }
                const char *next = strstr(p, "日");
                if (next) {
                    p = next + strlen("日");
                    continue;
                }
            }
            p++;
        }
    }
    if (cur_year > entry->year ||
        (cur_year == entry->year && (cur_month > last_month || (cur_month == last_month && cur_day > last_day)))) {
        *is_past_out = true;
        return true;
    }
    if (cur_year == entry->year) {
        bool in_holiday = (cur_month > entry->start_month || (cur_month == entry->start_month && cur_day >= entry->start_day)) &&
                          (cur_month < entry->end_month || (cur_month == entry->end_month && cur_day <= entry->end_day));
        if (in_holiday) {
            *is_today_out = true;
        } else if (entry->makeup_workdays && strcmp(entry->makeup_workdays, "无") != 0) {
            char today_str[32];
            snprintf(today_str, sizeof(today_str), "%u月%u日", cur_month, cur_day);
            if (strstr(entry->makeup_workdays, today_str)) {
                *is_today_out = true;
            }
        }
    }
    return true;
}

static void draw_holiday_calendar_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
    size_t count = ptc_holiday_calendar_arrangement_count(info->last_year);
    const int per_page = 4;
    int pages = (int)((count + per_page - 1) / per_page);
    int page = model->holiday_calendar_page;
    char line[192];
    uint16_t cur_year = 0;
    uint8_t cur_month = 0, cur_day = 0;
    bool has_date = model->status_loaded && ptc_date_from_day_index(model->day_index, &cur_year, &cur_month, &cur_day);
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 600);
    if (model->holiday_dirty) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + dialog.width - 330, dialog.y + 24, 290, 34}, 16, UI_WARNING_SOFT);
        draw_text_center(pixels, stride, (UiRect){dialog.x + dialog.width - 330, dialog.y + 24, 290, 34},
                         ptc_ui_text(PTC_UI_T_PREVIEW_UNSAVED_SETTINGS), 16, UI_WARNING);
    }
    if (!model->calendar_builtin && !model->holiday_calendar_data) {
        draw_text_center(pixels, stride,
            (UiRect){dialog.x + 40, dialog.y + 240, dialog.width - 80, 60},
            ptc_ui_text(PTC_UI_T_CALENDAR_INVALID_FILE), 20, UI_WARNING);
        draw_candidate_button(pixels, stride, ptc_ui_holiday_page_action_rect(2),
            ptc_ui_text(PTC_UI_T_A_B_CLOSE), UI_ACCENT, UI_ON_ACCENT, true, false);
        return;
    }
    if (model->holiday_calendar_data) {
        const PtcImportedCalendarYear *imported = model->holiday_calendar_data;
        size_t total = imported->group_count + imported->workday_count;
        uint16_t first = 0;
        pages = total ? (int)((total + 3u) / 4u) : 1;
        if (page < 0 || page >= pages) page = 0;
        (void)ptc_day_index_from_date(imported->year, 1, 1, &first);
        for (int row = 0; row < 4; ++row) {
            size_t index = (size_t)(page * 4 + row);
            UiRect card = {dialog.x + 34, dialog.y + 146 + row * 82, dialog.width - 68, 70};
            const char *name;
            if (index >= total) break;
            if (index < imported->group_count) {
                const PtcCalendarHolidayGroup *group = &imported->groups[index];
                uint16_t y;
                uint8_t sm = 0, sd = 0, em = 0, ed = 0;
                (void)ptc_date_from_day_index(group->first_day_index, &y, &sm, &sd);
                (void)ptc_date_from_day_index(group->last_day_index, &y, &em, &ed);
                name = group->name;
                snprintf(line, sizeof(line), "%04u-%02u-%02u - %04u-%02u-%02u",
                    imported->year, sm, sd, imported->year, em, ed);
            } else {
                size_t ordinal = index - imported->group_count, seen = 0;
                uint16_t y = 0;
                uint8_t m = 0, d = 0;
                name = ptc_ui_text(PTC_UI_T_MAKEUP_WORKDAY);
                for (uint16_t offset = 0; offset < 366; ++offset) {
                    if (imported->days[offset] != PTC_CALENDAR_DAY_MAKEUP_WORKDAY) continue;
                    if (seen++ == ordinal) {
                        (void)ptc_date_from_day_index((uint16_t)(first + offset), &y, &m, &d);
                        break;
                    }
                }
                snprintf(line, sizeof(line), "%04u-%02u-%02u", y, m, d);
            }
            fill_round_rect(pixels, stride, card, 16, UI_RAISED);
            draw_rect_outline(pixels, stride, card, 16, 1, UI_BORDER);
            draw_text(pixels, stride, card.x + 20, card.y + 27, name, 21, UI_INK);
            draw_text(pixels, stride, card.x + 20, card.y + 53, line, 16, UI_MUTED);
        }
        if (!total)
            draw_text_center(pixels, stride,
                (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 60},
                ptc_ui_text(PTC_UI_T_CALENDAR_EMPTY), 19, UI_MUTED);
        snprintf(line, sizeof(line), "%u  |  %s  |  %d / %d",
            imported->year, ptc_ui_text(PTC_UI_T_CALENDAR_USER_IMPORT), page + 1, pages);
        draw_text_center(pixels, stride,
            (UiRect){dialog.x + 30, dialog.y + 480, dialog.width - 60, 30},
            line, 16, UI_MUTED);
        for (int index = 0; index < 3; ++index) {
            bool disabled = (index == 0 && page == 0 && !model->holiday_calendar_has_previous_year) ||
                (index == 1 && page + 1 >= pages && !model->holiday_calendar_has_next_year);
            const char *label = index == 0 ? ptc_ui_text(PTC_UI_T_L_PREVIOUS_PAGE) :
                index == 1 ? ptc_ui_text(PTC_UI_T_R_NEXT_PAGE) :
                ptc_ui_text(PTC_UI_T_A_B_CLOSE);
            draw_candidate_button(pixels, stride, ptc_ui_holiday_page_action_rect(index), label,
                index == 2 ? UI_ACCENT : UI_PAGE,
                index == 2 ? UI_ON_ACCENT : UI_ACCENT,
                model->overlay_selection == index, disabled);
        }
        return;
    }
    if (page < 0 || page >= pages) page = 0;
    for (int row = 0; row < per_page; ++row) {
        size_t index = (size_t)(page * per_page + row);
        const PtcHolidayArrangement *entry = ptc_holiday_calendar_arrangement(info->last_year, index);
        UiRect card = {dialog.x + 34, dialog.y + 146 + row * 82, dialog.width - 68, 70};
        if (!entry) break;
        bool is_past = false, is_today = false;
        if (has_date) {
            holiday_is_past_or_today(entry, cur_year, cur_month, cur_day, &is_past, &is_today);
        }
        uint32_t bg_color = is_today ? UI_ACCENT_SOFT : (is_past ? UI_SURFACE : UI_RAISED);
        uint32_t border_color = is_today ? UI_ACCENT : UI_BORDER;
        uint32_t title_color = is_today ? UI_ACCENT : (is_past ? UI_MUTED : UI_INK);
        uint32_t line_color = is_today ? UI_INK : (is_past ? UI_DISABLED : UI_MUTED);
        fill_round_rect(pixels, stride, card, 16, bg_color);
        draw_rect_outline(pixels, stride, card, 16, is_today ? 2 : 1, border_color);
        draw_text(pixels, stride, card.x + 20, card.y + 28, entry->display_name, 21, title_color);
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_HOLIDAY_U_U_U_U_MAKEUP_WORKDAY),
                 entry->start_month, entry->start_day, entry->end_month, entry->end_day, entry->makeup_workdays);
        draw_text(pixels, stride, card.x + 150, card.y + 28, line, 18, line_color);
        UiRect badge = {card.x + card.width - 96, card.y + 21, 76, 28};
        if (is_today) {
            fill_round_rect(pixels, stride, badge, 6, UI_ACCENT);
            draw_text_center(pixels, stride, badge, ptc_ui_text(PTC_UI_T_IN_PROGRESS), 14, UI_ON_ACCENT);
        } else if (is_past) {
            fill_round_rect(pixels, stride, badge, 6, UI_PAGE);
            draw_text_center(pixels, stride, badge, ptc_ui_text(PTC_UI_T_ENDED), 14, UI_MUTED);
        }
    }
    snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_V_U_PUBLISHED_S_WWW_GOV),
             info->last_year, info->version, info->published_date, page + 1, pages);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 30, dialog.y + 480, dialog.width - 60, 30}, line, 16, UI_MUTED);
    for (int index = 0; index < 3; ++index) {
        bool disabled = (index == 0 && page == 0) || (index == 1 && page + 1 >= pages);
        const char *label = index == 0 ? ptc_ui_text(PTC_UI_T_L_PREVIOUS_PAGE) : (index == 1 ? ptc_ui_text(PTC_UI_T_R_NEXT_PAGE) : ptc_ui_text(PTC_UI_T_A_B_CLOSE));
        draw_candidate_button(pixels, stride, ptc_ui_holiday_page_action_rect(index), label,
                              index == 2 ? UI_ACCENT : UI_PAGE,
                              index == 2 ? UI_ON_ACCENT : UI_ACCENT,
                              model->overlay_selection == index, disabled);
    }
}

static void draw_calendar_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char page_text[40];
    int pages = (model->calendar_manager_count + 5) / 6;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 640);
    for (int i = 0; i < 2; ++i) {
        char label[96];
        snprintf(label, sizeof(label), "%s %s", i == 0 ? "L" : "R",
            i == 0 ? ptc_ui_text(PTC_UI_T_SAVED_REGIONS) : ptc_ui_text(PTC_UI_T_IMPORT_FILES));
        draw_candidate_button(pixels, stride, ptc_ui_calendar_manager_tab_rect(i),
            label,
            model->calendar_manager_tab == i ? UI_ACCENT : UI_RAISED,
            model->calendar_manager_tab == i ? UI_ON_ACCENT : UI_INK,
            model->calendar_manager_tab == i, false);
    }
    draw_wrapped_text(pixels, stride, 650, 140,
        ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_BRIEF), 13, 520, 16, 2, UI_MUTED);
    for (int i = 0; i < 6; ++i) {
        int index = model->calendar_manager_page * 6 + i;
        UiRect rect = to_uirect(ptc_ui_calendar_manager_row_rect(i));
        bool selected = index == model->calendar_manager_selected;
        char fitted[128];
        if (index >= model->calendar_manager_count) break;
        fill_round_rect(pixels, stride, rect, 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, rect, 10, selected ? 2 : 1,
            selected ? UI_ACCENT : UI_BORDER);
        fit_text(fitted, sizeof(fitted), model->calendar_manager_rows[index].title,
            17, rect.width - 28);
        draw_text(pixels, stride, rect.x + 14, rect.y + 23,
            fitted, 17, UI_INK);
        draw_text(pixels, stride, rect.x + 14, rect.y + 47,
            model->calendar_manager_rows[index].detail, 13, UI_MUTED);
    }
    if (model->calendar_manager_count == 0)
        draw_text(pixels, stride, 130, 230, ptc_ui_text(PTC_UI_T_CALENDAR_EMPTY), 19, UI_MUTED);
    fill_round_rect(pixels, stride, (UiRect){650, 176, 520, 405}, 12, UI_RAISED);
    draw_rect_outline(pixels, stride, (UiRect){650, 176, 520, 405}, 12, 1, UI_BORDER);
    for (int i = 0; i < model->calendar_preview_count && i < 12; ++i) {
        char fitted[144];
        fit_text(fitted, sizeof(fitted), model->calendar_preview_lines[i],
            i == 0 ? 18 : 15, 480);
        draw_text(pixels, stride, 670, 206 + i * 30,
            fitted, i == 0 ? 18 : 15, i == 0 ? UI_INK : UI_MUTED);
    }
    snprintf(page_text, sizeof(page_text), "%d / %d", model->calendar_manager_page + 1,
        pages > 0 ? pages : 1);
    draw_text(pixels, stride, 110, 665, page_text, 14, UI_MUTED);
    draw_text(pixels, stride, 250, 665, ptc_ui_text(PTC_UI_T_CALENDAR_MANAGER_HINT), 14, UI_MUTED);
    for (int i = 0; i < 6; ++i) {
        char label[96];
        bool disabled = (i == 0 && model->calendar_manager_page == 0) ||
            (i == 1 && model->calendar_manager_page + 1 >= pages) ||
            (i == 2 && (!model->calendar_pending_file[0] && !model->calendar_pending_option_id[0])) ||
            (i == 2 && model->calendar_manager_tab == 0 && model->disable_flag_present) ||
            (model->waiting && i < 3) || (model->waiting && i == 4);
        const char *caption = i == 0 ? ptc_ui_text(PTC_UI_T_CALENDAR_PREVIOUS) :
            i == 1 ? ptc_ui_text(PTC_UI_T_CALENDAR_NEXT) :
            i == 3 ? ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_ACTION) :
            i == 4 ? ptc_ui_text(PTC_UI_T_Y_REFRESH) :
            i == 5 ? ptc_ui_text(PTC_UI_T_B_BACK) :
            model->calendar_manager_tab == 0 ? ptc_ui_text(PTC_UI_T_CALENDAR_APPLY_ACTION) :
                ptc_ui_text(PTC_UI_T_CALENDAR_IMPORT_ACTION);
        snprintf(label, sizeof(label), "%s%s", i == 0 ? "ZL " :
            i == 1 ? "ZR " : i == 2 ? "A " : i == 3 ? "X " : "", caption);
        draw_candidate_button(pixels, stride, ptc_ui_calendar_manager_nav_rect(i),
            label, i == 2 ? UI_ACCENT : UI_RAISED,
            i == 2 ? UI_ON_ACCENT : UI_INK, false, disabled);
    }
}

static void draw_calendar_format_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    static const PtcUiTextId lines[] = {
        PTC_UI_T_CALENDAR_FORMAT_FILE, PTC_UI_T_CALENDAR_FORMAT_FIELDS,
        PTC_UI_T_CALENDAR_FORMAT_DATES, PTC_UI_T_CALENDAR_FORMAT_LIMITS,
        PTC_UI_T_CALENDAR_IMPORT_CONFIRM
    };
    draw_dialog_shell(pixels, stride, model, &dialog, 1000, 560);
    for (int i = 0; i < 5; ++i)
        draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 104 + i * 62,
            ptc_ui_text(lines[i]), 18, dialog.width - 68, 24, 2, UI_INK);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 434,
        ptc_ui_text(PTC_UI_T_CALENDAR_FORMAT_DOCUMENT), 16, UI_ACCENT);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay),
        ptc_ui_text(PTC_UI_T_B_BACK), UI_RAISED, UI_INK, true);
}

static void draw_weekly_bulk_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *DAYS[] = {ptc_ui_text(PTC_UI_T_SUNDAY), ptc_ui_text(PTC_UI_T_MONDAY), ptc_ui_text(PTC_UI_T_TUESDAY), ptc_ui_text(PTC_UI_T_WEDNESDAY), ptc_ui_text(PTC_UI_T_THURSDAY), ptc_ui_text(PTC_UI_T_FRIDAY), ptc_ui_text(PTC_UI_T_SATURDAY)};
    UiRect dialog;
    char source[96];
    char line[128];
    PtcUiWeeklyBulkStats stats;
    int slot = model->weekly_last_day_slot >= 0 && model->weekly_last_day_slot < 7 ? model->weekly_last_day_slot : 0;
    int day = ptc_ui_weekday_for_display_slot(slot);
    PtcDayRule rule = model->draft_week[day];
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 560);
    if (rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(source, sizeof(source), ptc_ui_text(PTC_UI_T_SOURCE_S_UNLIMITED), DAYS[day]);
    else snprintf(source, sizeof(source), ptc_ui_text(PTC_UI_T_SOURCE_S_U_MIN_LIMIT), DAYS[day], (unsigned int)rule.minutes);
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 116, source, 19, UI_INK);
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 148, ptc_ui_text(PTC_UI_T_1_SELECT_TARGET), 17, UI_MUTED);
    for (int index = 0; index < 2; ++index) {
        UiRect card = to_uirect(ptc_ui_weekly_bulk_target_rect(index));
        bool selected = model->overlay_selection == index;
        fill_round_rect(pixels, stride, card, 16, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, card, 16, selected ? 3 : 1, selected ? UI_ACCENT : UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 18, card.width, 34},
                         index == 0 ? ptc_ui_text(PTC_UI_T_WORKING_DAY) : ptc_ui_text(PTC_UI_T_WEEKEND), 22, UI_INK);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 54, card.width, 28},
                         index == 0 ? ptc_ui_text(PTC_UI_T_MONDAY_TO_FRIDAY) : ptc_ui_text(PTC_UI_T_SATURDAY_AND_SUNDAY), 17, UI_MUTED);
    }
    ptc_ui_weekly_bulk_stats(model, model->overlay_selection == 1, &stats);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 490, dialog.y + 148, 510, 270}, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, (UiRect){dialog.x + 490, dialog.y + 148, 510, 270}, 16, 1, UI_BORDER);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 180, ptc_ui_text(PTC_UI_T_2_IMPACT_ON_TODAY), 20, UI_INK);
    snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_TARGET_D_DAYS_CHANGE_D_SKIP_D),
             stats.target_count, stats.changed_count, stats.unchanged_count);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 218, line, 17, UI_ACCENT);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 256, ptc_ui_text(PTC_UI_T_TARGET_CURRENT_RULE), 16, UI_MUTED);
    for (int index = 0; index < stats.rule_group_count; ++index) {
        PtcDayRule group = stats.rule_groups[index].rule;
        if (group.mode == PTC_RULE_MODE_UNLIMITED) snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_UNLIMITED_D_DAYS), stats.rule_groups[index].count);
        else snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_MIN_LIMIT_D_DAYS), (unsigned int)group.minutes, stats.rule_groups[index].count);
        draw_text(pixels, stride, dialog.x + 536, dialog.y + 288 + index * 27, line, 16, UI_INK);
    }
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 424,
              ptc_ui_text(PTC_UI_T_AFTER_APPLICATION_ONLY_THE_DRAFT_IN_THIS), 16, UI_WARNING);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       stats.changed_count > 0 ? ptc_ui_text(PTC_UI_T_A_APPLY_TO_DRAFT) : ptc_ui_text(PTC_UI_T_A_NO_MODIFICATION_REQUIRED),
                       stats.changed_count > 0 ? UI_ACCENT : UI_DISABLED,
                       UI_ON_ACCENT, false);
}

static void draw_holiday_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 720, 320);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 148, dialog.width - 72, 28},
                     ptc_ui_text(PTC_UI_T_UNSAVED_HOLIDAY_SETTINGS_WILL_BE_LOST_AFTER), 19, UI_WARNING);
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay), ptc_ui_text(PTC_UI_T_X_DISCARD_CHANGES),
                          UI_DANGER_SOFT, UI_DANGER, model->holiday_leave_selection == 0, false);
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_CONTINUE_EDITING),
                          UI_PAGE, UI_ACCENT, model->holiday_leave_selection == 1, false);
}

bool draw_plan_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
    case PTC_UI_OVERLAY_SCHEDULED_LEAVE:
        draw_scheduled_leave(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SCHEDULED:
        draw_scheduled_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_AUTONOMY:
        draw_autonomy_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_EYE_CARE:
        draw_eye_care_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_QUICK_ADD:
        draw_quick_add_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME:
        draw_bedtime_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_WINDOW:
        draw_bedtime_window_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_SPECIAL:
        draw_bedtime_special_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_LEAVE:
        draw_bedtime_leave_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_BULK:
        draw_bedtime_bulk_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
        draw_weekly_leave_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_HOLIDAY_CALENDAR:
        draw_holiday_calendar_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_CALENDAR_MANAGER:
        draw_calendar_manager_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_CALENDAR_FORMAT:
        draw_calendar_format_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_HOLIDAY_LEAVE:
        draw_holiday_leave_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_WEEKLY_BULK:
        draw_weekly_bulk_overlay(pixels, stride, model);
        return true;
    default:
        return false;
    }
}
