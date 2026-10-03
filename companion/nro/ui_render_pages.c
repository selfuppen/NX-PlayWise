#include "ui_render_internal.h"

static const UiAction TODAY_ACTIONS[] = {
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_SET_TODAY_LIMIT), PTC_UI_TEXT_REFERENCE(PTC_UI_T_CONTROLLED_BY_RULES), UI_ACCENT, UI_ACTION_ICON_CLOCK, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_QUICK_GRANT), "", UI_SUCCESS, UI_ACTION_ICON_ADD_TIME, UI_ACTION_VISUAL_QUICK_ADD},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_NO_LIMIT_TODAY), PTC_UI_TEXT_REFERENCE(PTC_UI_T_TODAY_ONLY_BEDTIME_STAYS_ON), UI_SUCCESS, UI_ACTION_ICON_INFINITY, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_CLEAR_TODAY_LIMIT), PTC_UI_TEXT_REFERENCE(PTC_UI_T_RESET_TO_REGULAR_PLAN), UI_MUTED, UI_ACTION_ICON_CLEAR_OVERRIDE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_SKIP_BEDTIME), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_WARNING, UI_ACTION_ICON_MOON, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_AUTONOMY_BUFFER_2), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_MUTED, UI_ACTION_ICON_BUFFER, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_EYE_CARE_SKIP), PTC_UI_TEXT_REFERENCE(PTC_UI_T_EYE_CARE_SKIP_NOT_RESTING), UI_WARNING, UI_ACTION_ICON_CLOCK, UI_ACTION_VISUAL_NONE},
};

static const UiAction PLAN_ACTIONS[] = {
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_SPECIFIED_DATE_QUOTA), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_ACCENT, UI_ACTION_ICON_CALENDAR_RANGE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_HOLIDAY_QUOTA), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_SUCCESS, UI_ACTION_ICON_HOLIDAY, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_WEEKLY_QUOTA), PTC_UI_TEXT_REFERENCE(PTC_UI_T_ACTIVE_2), UI_ACCENT, UI_ACTION_ICON_WEEKLY, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_BEDTIME), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_WARNING, UI_ACTION_ICON_MOON, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_AUTONOMY_BUFFER_2), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_SUCCESS, UI_ACTION_ICON_BUFFER, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_EYE_CARE), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_WARNING, UI_ACTION_ICON_CLOCK, UI_ACTION_VISUAL_NONE},
};

static const UiAction GRANT_ACTIONS[] = {
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_HOST_GENERATION), PTC_UI_TEXT_REFERENCE(PTC_UI_T_NATIVE_SIGNATURE_8_BIT_CODE), UI_SUCCESS, UI_ACTION_ICON_CONSOLE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_MOBILE_PC_GENERATOR), PTC_UI_TEXT_REFERENCE(PTC_UI_T_AVAILABLE_OFFLINE_ACROSS_DEVICES), UI_ACCENT, UI_ACTION_ICON_DEVICE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_GRANT_SETTINGS), PTC_UI_TEXT_REFERENCE(PTC_UI_T_DEVICE_AND_KEY_CONFIGURED), UI_MUTED, UI_ACTION_ICON_SLIDERS, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_USAGE_HISTORY), PTC_UI_TEXT_REFERENCE(PTC_UI_T_LAST_100), UI_MUTED, UI_ACTION_ICON_HISTORY, UI_ACTION_VISUAL_NONE},
};

static const UiAction SETTINGS_ACTIONS[] = {
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_APPEARANCE_THEME), "", UI_ACCENT, UI_ACTION_ICON_THEME, UI_ACTION_VISUAL_THEME},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_CHANGE_PIN), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_ENABLED), UI_ACCENT, UI_ACTION_ICON_KEY, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_PARENT_SHORTCUT), PTC_UI_TEXT_REFERENCE(PTC_UI_T_CURRENT_MINUS_2), UI_ACCENT, UI_ACTION_ICON_CONTROLLER, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_HOMEBREW_ACCESS), PTC_UI_TEXT_REFERENCE(PTC_UI_T_NOT_ENABLED), UI_DANGER, UI_ACTION_ICON_HOMEBREW, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_FAMILY_ACTIVITIES), PTC_UI_TEXT_REFERENCE(PTC_UI_T_LAST_200), UI_MUTED, UI_ACTION_ICON_ACTIVITY, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_SOUND_EFFECTS), "", UI_SUCCESS, UI_ACTION_ICON_AUDIO, UI_ACTION_VISUAL_AUDIO},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_UI_LANGUAGE), PTC_UI_TEXT_REFERENCE(PTC_UI_T_FOLLOW_SYSTEM), UI_ACCENT, UI_ACTION_ICON_THEME, UI_ACTION_VISUAL_NONE},
};

const UiAction GRANT_MANAGER_ACTIONS[] = {
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_MANAGES_THE_TIME_CODE_DEVICE_NAME), PTC_UI_TEXT_REFERENCE(PTC_UI_T_VIEW_ENTER_OR_RANDOMLY_GENERATE_DEVICE_NAMES), UI_ACCENT, UI_ACTION_ICON_DEVICE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_MANAGE_GRANT_CODE_KEYS), PTC_UI_TEXT_REFERENCE(PTC_UI_T_VIEW_ENTER_OR_RANDOMLY_GENERATE_A_SIGNING), UI_DANGER, UI_ACTION_ICON_KEY, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_EXPORT_MOBILE_PHONE_COMPUTER_CONFIGURATION), PTC_UI_TEXT_REFERENCE(PTC_UI_T_EXPORT_CONFIGURATION_FILES_FOR_USE_ON_MOBILE), UI_SUCCESS, UI_ACTION_ICON_EXPORT, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_EDIT_QR_CODE_JUMP_ADDRESS), PTC_UI_TEXT_REFERENCE(PTC_UI_T_MODIFY_THE_WEB_PAGE_ADDRESS_OPENED_AFTER), UI_ACCENT, UI_ACTION_ICON_DEVICE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_RESTORE_THE_DEFAULT_ADDRESS_OF_QR_CODE), PTC_UI_TEXT_REFERENCE(PTC_UI_T_RESTORES_THE_DEFAULT_WEB_PAGE_ADDRESS_PROVIDED), UI_MUTED, UI_ACTION_ICON_RESTORE, UI_ACTION_VISUAL_NONE},
};

static const UiAction SUPPORT_ACTIONS[] = {
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_ENABLE_QUOTA_CONTROL), PTC_UI_TEXT_REFERENCE(PTC_UI_T_ENABLED_AFTER_SECURITY_CHECK), UI_ACCENT, UI_ACTION_ICON_SHIELD, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_RETRY_REPAIR), PTC_UI_TEXT_REFERENCE(PTC_UI_T_RECHECK_IF_IT_IS_SAFE_TO_ENABLE), UI_SUCCESS, UI_ACTION_ICON_REPAIR, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_EMERGENCY_DEACTIVATION), PTC_UI_TEXT_REFERENCE(PTC_UI_T_STOP_NEW_CONTROL_WRITES), UI_DANGER, UI_ACTION_ICON_STOP, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_RESTORE_ORIGINAL_STATE), PTC_UI_TEXT_REFERENCE(PTC_UI_T_RESTORE_ORIGINAL_SETTINGS_AND_DEACTIVATE), UI_DANGER, UI_ACTION_ICON_RESTORE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_EXPORT_DIAGNOSTICS), PTC_UI_TEXT_REFERENCE(PTC_UI_T_NO_KEY_PIN_OR_OFFLINE_CODE), UI_MUTED, UI_ACTION_ICON_EXPORT, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_SOFTWARE_INFO), PTC_UI_TEXT_REFERENCE(PTC_UI_T_VERSION_PROJECT_REPOSITORY_AND_PARENT_PAGE), UI_ACCENT, UI_ACTION_ICON_INFO, UI_ACTION_VISUAL_NONE},
};

static const UiAction RESUME_CONTROL_ACTION = {
    PTC_UI_TEXT_REFERENCE(PTC_UI_T_RE_ENABLE_CONTROLS), PTC_UI_TEXT_REFERENCE(PTC_UI_T_RESTORE_QUOTA_MANAGEMENT_AFTER_SECURITY_CHECK), UI_SUCCESS,
    UI_ACTION_ICON_REPAIR, UI_ACTION_VISUAL_NONE
};

static const UiAction RECONFIRM_ENVIRONMENT_ACTION = {
    PTC_UI_TEXT_REFERENCE(PTC_UI_T_RECHECK_AND_ENABLE), PTC_UI_TEXT_REFERENCE(PTC_UI_T_ENVIRONMENT_CHANGES_RESUME_QUOTA_MANAGEMENT_AFTER_CONFIRMING), UI_WARNING,
    UI_ACTION_ICON_REPAIR, UI_ACTION_VISUAL_NONE
};
static void draw_parent_home_summary(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect box = to_uirect(ptc_ui_home_summary_rect(true));
    char remaining[64], today[64], line[192], age[64];
    int x = box.x + 28;
    ptc_ui_format_home_remaining(model, ptc_ui_render_now(), remaining, sizeof(remaining));
    ptc_ui_format_today_mode(model, today, sizeof(today));
    draw_card_shadow(pixels, stride, box, 16);
    fill_round_rect(pixels, stride, box, 16, UI_RGB(UI_BLENDED(hero)));
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    /* 剩余告急或就寝生效时描边随呼吸相位脉冲。 */
    if ((model->remaining_available && model->unrestricted_today != 1 &&
         model->remaining_minutes >= 0 && model->remaining_minutes <= 10) ||
        bedtime_enforcing) {
        int phase = get_breathing_phase();
        draw_rect_outline(pixels, stride, box, 16, 2,
                          UI_RGB(ui_mix_rgb(UI_BLENDED(danger), 0xFF9A8A, phase * 4)));
    }
    draw_text(pixels, stride, x, box.y + 42,
              bedtime_enforcing ? ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY_BEDTIME_ACTIVE) : ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY),
              22, UI_RGB(UI_BLENDED(hero_secondary)));
    /* 环形额度表：弧长由缓动后的剩余分钟驱动，颜色沿用今日额度健康色；
     * 数据不可用时只画弱化轨道环。 */
    {
        float fraction = 0.0f;
        uint32_t ring_fill = UI_SUCCESS;
        bool has_fraction = false;
        if (bedtime_enforcing) {
            fraction = 0.0f;
            ring_fill = UI_DANGER;
            has_fraction = true;
        } else if (model->unrestricted_today == 1) {
            fraction = 1.0f;
            ring_fill = UI_SUCCESS;
            has_fraction = true;
        } else if (model->remaining_available && model->played_minutes_available &&
                   model->remaining_minutes >= 0 && model->played_minutes >= 0 &&
                   model->remaining_minutes + model->played_minutes > 0) {
            int total = model->remaining_minutes + model->played_minutes;
            /* 与下方额度条同语义：弧长表示剩余占比，随消耗缩小。 */
            fraction = (float)model->displayed_remaining_minutes / (float)total;
            has_fraction = true;
        } else if (model->remaining_available && model->remaining_minutes > 0) {
            int shown = model->remaining_minutes > 120 ? 120 : model->remaining_minutes;
            fraction = (float)shown / 120.0f;
            has_fraction = true;
        }
        if (has_fraction && model->unrestricted_today != 1 && !bedtime_enforcing) {
            if (model->remaining_available && model->remaining_minutes <= 10) ring_fill = UI_DANGER;
            else if (model->remaining_available && model->remaining_minutes <= 30) ring_fill = UI_WARNING;
            else ring_fill = UI_SUCCESS;
        }
        if (fraction < 0) fraction = 0;
        if (fraction > 1) fraction = 1;
        draw_ring_progress(pixels, stride, box.x + box.width - 46, box.y + 40, 16, 5, fraction,
                           UI_RGB(ui_mix_rgb(UI_BLENDED(hero), 0xFFFFFF, 20)), ring_fill);
    }
    /* Keep the numeric presentation independent from localized wording. */
    int minutes;
    if (ptc_ui_home_remaining_minutes(model, ptc_ui_render_now(), &minutes)) {
        snprintf(remaining, sizeof(remaining), "%d", minutes);
        int num_w = measure_text(remaining, 80);
        draw_text_bold(pixels, stride, x, box.y + 133, remaining, 80, UI_RGB(UI_BLENDED(on_hero)));
        int unit_x = x + num_w + 12;
        const char *unit_label = ptc_ui_text(PTC_UI_T_MIN);
        draw_text(pixels, stride, unit_x, box.y + 130, unit_label, 24, UI_RGB(UI_BLENDED(hero_secondary)));
        if (minutes >= 60) {
            char duration_str[64];
            if (minutes % 60 == 0)
                snprintf(duration_str, sizeof(duration_str), ptc_ui_text(PTC_UI_T_D_HR_2), minutes / 60);
            else
                snprintf(duration_str, sizeof(duration_str), ptc_ui_text(PTC_UI_T_D_HR_D_MIN_2), minutes / 60, minutes % 60);
            int dur_x = unit_x + measure_text(unit_label, 24) + 12;
            draw_text(pixels, stride, dur_x, box.y + 130, duration_str, 20, UI_RGB(UI_BLENDED(hero_secondary)));
        }
    } else {
        draw_wrapped_text(pixels, stride, x, box.y + 124, remaining, 40, box.width - 56, 48, 2, UI_RGB(UI_BLENDED(on_hero)));
    }
    /* 今日额度胶囊进度槽 (Time Progress Gauge) */
    UiRect gauge_bg = {box.x + 28, box.y + 150, box.width - 56, 8};
    fill_round_rect(pixels, stride, gauge_bg, 4, UI_GAUGE_SLOT);
    draw_rect_outline(pixels, stride, gauge_bg, 4, 1, UI_GAUGE_SLOT_BORDER);

    uint32_t health_color = UI_SUCCESS;
    int remaining_mins = model->remaining_available ? model->remaining_minutes : -1;
    if (bedtime_enforcing) {
        health_color = UI_DANGER;
    } else if (remaining_mins >= 0) {
        if (remaining_mins <= 10) health_color = UI_DANGER;
        else if (remaining_mins <= 30) health_color = UI_WARNING;
        else health_color = UI_SUCCESS;
    }

    if (bedtime_enforcing) {
        /* 就寝生效时进度槽为空并呈红框 */
    } else if (model->unrestricted_today == 1) {
        fill_round_rect(pixels, stride, gauge_bg, 4, UI_SUCCESS);
    } else if (model->remaining_available && model->played_minutes_available &&
               (model->remaining_minutes + model->played_minutes > 0)) {
        int total_mins = model->remaining_minutes + model->played_minutes;
        /* 填充宽度用缓动后的剩余分钟，随数字一起滚动。 */
        int remain_w = (int)((int64_t)gauge_bg.width * model->displayed_remaining_minutes / total_mins);
        if (remain_w < 6 && model->remaining_minutes > 0) remain_w = 6;
        if (remain_w > gauge_bg.width) remain_w = gauge_bg.width;
        if (remain_w > 0) {
            fill_round_rect(pixels, stride, (UiRect){gauge_bg.x, gauge_bg.y, remain_w, gauge_bg.height}, 4, health_color);
        }
    } else if (model->remaining_available && model->remaining_minutes > 0) {
        int displayed_shown = model->displayed_remaining_minutes > 120 ? 120 :
                              (model->displayed_remaining_minutes < 0 ? 0 : model->displayed_remaining_minutes);
        int fill_w = (int)((int64_t)gauge_bg.width * displayed_shown / 120);
        if (fill_w < 6) fill_w = 6;
        fill_round_rect(pixels, stride, (UiRect){gauge_bg.x, gauge_bg.y, fill_w, gauge_bg.height}, 4, health_color);
    }

    if (bedtime_enforcing) {
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE_PLAY_RESTRICTED_S),
            model->status_loaded ? ui_rule_source_label(model->rule_source) : (ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM)));
    } else {
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_TODAY_S_S), today,
            model->status_loaded ? ui_rule_source_label(model->rule_source) : (ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM)));
    }
    draw_text(pixels, stride, x, box.y + 176, line, 18, UI_RGB(UI_BLENDED(hero_secondary)));
    /* Supporting information sits on a separate surface, below the hero. */
    UiRect lower = {box.x + 12, box.y + 200, box.width - 24, box.height - 212};
    fill_round_rect(pixels, stride, lower, 16, UI_SURFACE);
    ptc_ui_format_home_total(model, line, sizeof(line));
    draw_text(pixels, stride, x, box.y + 236, line, 22, UI_INK);
    if (model->played_minutes_available && model->played_minutes >= 0)
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_USED_QUOTA_ESTIMATE_D_MIN), model->played_minutes);
    else snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_USED_QUOTA_ESTIMATE_UNAVAILABLE));
    draw_text(pixels, stride, x, box.y + 272, line, 18, UI_MUTED);
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, x, box.y + box.height - 26, age, 16, UI_MUTED);
}

static const UiAction *actions_for_page(PtcUiParentPage page, int *count)
{
    if (page == PTC_UI_PARENT_PLAN) {
        *count = (int)(sizeof(PLAN_ACTIONS) / sizeof(PLAN_ACTIONS[0]));
        return PLAN_ACTIONS;
    }
    if (page == PTC_UI_PARENT_GRANT) {
        *count = (int)(sizeof(GRANT_ACTIONS) / sizeof(GRANT_ACTIONS[0]));
        return GRANT_ACTIONS;
    }
    if (page == PTC_UI_PARENT_SETTINGS) {
        *count = (int)(sizeof(SETTINGS_ACTIONS) / sizeof(SETTINGS_ACTIONS[0]));
        return SETTINGS_ACTIONS;
    }
    if (page == PTC_UI_PARENT_SUPPORT) {
        *count = (int)(sizeof(SUPPORT_ACTIONS) / sizeof(SUPPORT_ACTIONS[0]));
        return SUPPORT_ACTIONS;
    }
    *count = (int)(sizeof(TODAY_ACTIONS) / sizeof(TODAY_ACTIONS[0]));
    return TODAY_ACTIONS;
}

static void draw_tabs(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *LABELS[] = {ptc_ui_text(PTC_UI_T_TODAY_SCHEDULE), ptc_ui_text(PTC_UI_T_TIME_PLANS), ptc_ui_text(PTC_UI_T_OFFLINE_GRANTS), ptc_ui_text(PTC_UI_T_SECURITY_PREFS), ptc_ui_text(PTC_UI_T_SUPPORT_RECOVERY)};
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page != PTC_UI_PLAN_PAGE_ROOT) {
        const char *name = model->plan_page == PTC_UI_PLAN_PAGE_WEEKLY ? (ptc_ui_text(PTC_UI_T_WEEKLY_PLAN)) :
            (model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY ? (ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAYS)) : (ptc_ui_text(PTC_UI_T_BEDTIME_SCHEDULE)));
        home_button(pixels, stride, ptc_ui_advanced_back_rect(), ptc_ui_text(PTC_UI_T_B_BACK_TO_PLANS), false, false, false);
        {
            char path[96];
            snprintf(path, sizeof(path), ptc_ui_text(PTC_UI_T_TIME_PLANS_S), name);
            draw_text(pixels, stride, 278, 140, path, 22, UI_MUTED);
        }
        return;
    }

    int index;
    for (index = 0; index < PTC_UI_PARENT_PAGE_COUNT; ++index) {
        UiRect tab = to_uirect(ptc_ui_parent_tab_rect(index));
        fill_round_rect(pixels, stride, tab, 12, UI_RAISED);
    }
    /* 选中指示胶囊从上一页位置滑向当前页，页签文字保持不动。 */
    {
        UiRect from = to_uirect(ptc_ui_parent_tab_rect((int)model->last_parent_page));
        UiRect to = to_uirect(ptc_ui_parent_tab_rect((int)model->parent_page));
        float slide_t = 1.0f;
        if (model->last_parent_page != model->parent_page) {
            slide_t = ui_ease_out((float)(ptc_ui_anim_now_ms() - model->page_switched_at_ms) /
                                  (float)PTC_UI_PAGE_SWITCH_TOTAL_MS);
        }
        UiRect pill = {
            from.x + (int)((to.x - from.x) * slide_t),
            from.y,
            from.width + (int)((to.width - from.width) * slide_t),
            from.height};
        fill_round_rect(pixels, stride, pill, 12, UI_ACCENT);
    }
    for (index = 0; index < PTC_UI_PARENT_PAGE_COUNT; ++index) {
        UiRect tab = to_uirect(ptc_ui_parent_tab_rect(index));
        bool active = index == (int)model->parent_page;
        draw_text_center(pixels, stride, tab, LABELS[index], 18, active ? UI_ON_ACCENT : UI_INK);
    }
    draw_text(pixels, stride, 1038, 140, ptc_ui_text(PTC_UI_T_L_R_SWITCH), 19, UI_MUTED);
}

static void draw_settings_badge(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *label = ptc_ui_settings_status_label(model);
    uint32_t color = label && strcmp(label, ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED)) == 0 ? UI_DANGER : UI_WARNING;
    UiRect badge;
    if (!label) return;
    badge = (UiRect){54 + PTC_UI_PARENT_SUPPORT * 174 + 96, 113, 56, 24};
    fill_round_rect(pixels, stride, badge, 6, color);
    draw_text_center(pixels, stride, badge, label, 11, UI_ON_ACCENT);
}

static void draw_safety_status(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 452};
    char troubleshoot[128];
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    int recommended = ptc_ui_support_recommended_action(model);
    draw_text(pixels, stride, panel.x + 26, panel.y + 36, ptc_ui_text(PTC_UI_T_CURRENT_ISSUE), 23, UI_INK);
    draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 70, ptc_ui_support_problem(model),
                      18, panel.width - 52, 25, 2, UI_RGB(UI_BLENDED(text_primary)));
    const char *next = recommended == 0 ? (model->disable_flag_present ? ptc_ui_text(PTC_UI_T_RECOMMENDATION_UNDEACTIVATE_AND_RE_ENABLE) : ptc_ui_text(PTC_UI_T_RECOMMENDATION_RECHECK_AND_ENABLE)) :
                       recommended == 1 ? ptc_ui_text(PTC_UI_T_SUGGESTION_SELECT_RETRY_REPAIR) :
                       recommended == 4 ? ptc_ui_text(PTC_UI_T_SUGGESTION_EXPORT_DIAGNOSTIC_PACKAGE_AND_KEEP_PROBLEM) :
                       (model->waiting || model->apply_pending_confirmation ? ptc_ui_text(PTC_UI_T_PLEASE_WAIT_FOR_THE_RESULT_AND_THEN) : ptc_ui_text(PTC_UI_T_NO_NEED_TO_RESTORE_THE_OPERATION_YOU));
    draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 130, next, 16,
                      panel.width - 52, 23, 2, UI_RGB(UI_BLENDED(accent)));
    char age[80];
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, panel.x + 26, panel.y + 188, age, 15, status_age_color(model));
    if (model->environment_available)
        snprintf(troubleshoot, sizeof(troubleshoot), "HOS %s  |  %s", model->environment_hos, model->environment_model);
    else snprintf(troubleshoot, sizeof(troubleshoot), ptc_ui_text(PTC_UI_T_ENVIRONMENT_DETAILS_ARE_TEMPORARILY_UNAVAILABLE_DIAGNOSTICS_CA));
    fit_text(troubleshoot, sizeof(troubleshoot), troubleshoot, 15, panel.width - 52);
    draw_text(pixels, stride, panel.x + 26, panel.y + 214, troubleshoot, 15, UI_RGB(UI_BLENDED(text_secondary)));
    draw_text(pixels, stride, panel.x + 26, panel.y + 252, ptc_ui_text(PTC_UI_T_RECENT_EVENTS), 18, UI_MUTED);
    if (model->recent_event_count > 0) {
        for (int event_index = 0; event_index < model->recent_event_count; ++event_index) {
            char latest[192];
            char event_time[48];
            int source_index = model->recent_event_count - 1 - event_index;
            format_event_time(model->recent_event_timestamps[source_index], false, event_time, sizeof(event_time));
            snprintf(latest, sizeof(latest), "%s  |  %s", model->recent_events[source_index], event_time);
            fit_text(latest, sizeof(latest), latest, 14, panel.width - 52);
            UiRect ev_rect = to_uirect(ptc_ui_support_event_rect(event_index));
            if (event_index + 6 == model->selected_index && !model->parent_footer_focused)
                draw_rect_outline(pixels, stride, ev_rect, 8, 2, UI_RGB(UI_BLENDED(focus)));
            draw_text(pixels, stride, panel.x + 26, ev_rect.y + 24,
                      latest, 14, event_index + 6 == model->selected_index ? UI_ACCENT : UI_MUTED);
        }
    } else {
        const char *no_events_msg = model->recent_events_available
            ? ptc_ui_text(PTC_UI_T_THERE_ARE_NO_RECENT_EVENTS_REQUIRING_ATTENTION)
            : ptc_ui_text(PTC_UI_T_TEMPORARILY_UNABLE_TO_READ_RECENT_EVENTS_PLEASE);
        draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 288,
                          no_events_msg, 14, panel.width - 52, 20, 2,
                          model->recent_events_available ? UI_MUTED : UI_DANGER);
    }
}

static void draw_today_status(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char adjustment_badge[32];
    char adjustment_detail[128];
    int64_t now = ptc_ui_render_now();
    bool fresh = ptc_ui_status_is_fresh(model, now);
    bool bedtime_skip_matches = fresh && ptc_ui_bedtime_skip_matches_policy(
        model, &model->bedtime_policy) &&
        (model->bedtime_active
            ? model->bedtime_window_instance_id == model->bedtime_skipped_window_instance_id
            : (!model->bedtime_next_available ||
               model->bedtime_next_window_instance_id == model->bedtime_skipped_window_instance_id));
    ptc_ui_format_today_adjustment_status(model, now, adjustment_badge, sizeof(adjustment_badge),
                                          adjustment_detail, sizeof(adjustment_detail));
    draw_parent_home_summary(pixels, stride, model);
    bool is_en = (ptc_ui_language_get_resolved() == PTC_UI_LANGUAGE_ENGLISH);
    UiRect quota_group = {548, 176, 696, 264};
    UiRect other_group = {548, 446, 696, 182};
    fill_round_rect(pixels, stride, quota_group, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, quota_group, 16, 1, UI_BORDER);
    const char *title1 = ptc_ui_text(PTC_UI_T_TODAY_S_LIMIT_ADJUSTMENT_TODAY_ONLY);
    const char *hint1 = ptc_ui_text(PTC_UI_T_ORIGINAL_PLAN_RESUMES_TOMORROW);
    draw_text(pixels, stride, 574, 195, title1, 16, UI_ACCENT);
    int title1_w = measure_text(title1, 16);
    draw_text(pixels, stride, 574 + title1_w + 14, 195, hint1, 13, UI_MUTED);
    fill_round_rect(pixels, stride, other_group, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, other_group, 16, 1, UI_BORDER);
    const char *title2 = ptc_ui_text(PTC_UI_T_BEDTIME_AUTONOMY_BUFFER);
    const char *hint2 = ptc_ui_text(PTC_UI_T_BEDTIME_OPERATES_INDEPENDENTLY);
    draw_text(pixels, stride, 574, 466, title2, 16, UI_WARNING);
    int title2_w = measure_text(title2, 16);
    draw_text(pixels, stride, 574 + title2_w + 14, 466, hint2, 13, UI_MUTED);
    for (int index = 0; index < 7; ++index) {
        UiRect box = to_uirect(ptc_ui_today_card_rect(index));
        bool focused = !model->parent_footer_focused && model->selected_index == index;
        bool clear_unavailable = index == 3 && fresh &&
                                 !model->today_override_present;
        const char *unavailable = ptc_ui_today_action_unavailable_reason(model, index, now);
        bool eye_needs_refresh = index == 6 && unavailable &&
            strcmp(unavailable, ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_REFRESH)) == 0;
        bool disabled = model->disable_flag_present || model->waiting ||
                        clear_unavailable || unavailable != NULL;
        const char *title = TODAY_ACTIONS[index].title;
        const char *subtitle = TODAY_ACTIONS[index].subtitle;
        char dynamic[128];
        UiAction action = TODAY_ACTIONS[index];
        if (index == 0) {
            subtitle = adjustment_detail;
        } else if (index == 1 && unavailable) {
            subtitle = unavailable;
            action.visual = UI_ACTION_VISUAL_NONE;
        } else if (index == 3 && clear_unavailable) {
            subtitle = model->today_override_cleared_in_session
                ? (ptc_ui_text(PTC_UI_T_CLEARED_IN_SESSION_LOWER_RULE_APPLIES))
                : (ptc_ui_text(PTC_UI_T_NO_TODAY_LIMIT_SET));
        } else if (index == 4) {
            if (unavailable) {
                subtitle = unavailable;
            } else if (!fresh) {
                subtitle = ptc_ui_text(PTC_UI_T_REFRESH_TO_SHOW_NEXT_WINDOW);
            } else if (model->bedtime_active) {
                if (model->bedtime_skipped) {
                    snprintf(dynamic, sizeof(dynamic), ptc_ui_text(PTC_UI_T_02U_02U_TO_NEXT_DAY_02U_02U),
                        (unsigned int)(model->bedtime_start_minute / 60),
                        (unsigned int)(model->bedtime_start_minute % 60),
                        (unsigned int)(model->bedtime_end_minute / 60),
                        (unsigned int)(model->bedtime_end_minute % 60));
                } else {
                    snprintf(dynamic, sizeof(dynamic), ptc_ui_text(PTC_UI_T_02U_02U_TO_NEXT_DAY_02U_02U_2),
                        (unsigned int)(model->bedtime_start_minute / 60),
                        (unsigned int)(model->bedtime_start_minute % 60),
                        (unsigned int)(model->bedtime_end_minute / 60),
                        (unsigned int)(model->bedtime_end_minute % 60));
                }
                subtitle = dynamic;
            } else if (bedtime_skip_matches) {
                snprintf(dynamic, sizeof(dynamic), ptc_ui_text(PTC_UI_T_02U_02U_TO_NEXT_DAY_02U_02U),
                    (unsigned int)(model->bedtime_skipped_start_minute / 60),
                    (unsigned int)(model->bedtime_skipped_start_minute % 60),
                    (unsigned int)(model->bedtime_skipped_end_minute / 60),
                    (unsigned int)(model->bedtime_skipped_end_minute % 60));
                subtitle = dynamic;
            } else if (model->bedtime_next_available) {
                uint16_t year;
                uint8_t month, day;
                if (ptc_date_from_day_index(model->bedtime_next_start_day_index, &year, &month, &day))
                    snprintf(dynamic, sizeof(dynamic), ptc_ui_text(PTC_UI_T_02U_02U_02U_02U_TO_02U_02U), month, day,
                        (unsigned int)(model->bedtime_next_start_minute / 60),
                        (unsigned int)(model->bedtime_next_start_minute % 60),
                        (unsigned int)(model->bedtime_next_end_minute / 60),
                        (unsigned int)(model->bedtime_next_end_minute % 60));
                else snprintf(dynamic, sizeof(dynamic), ptc_ui_text(PTC_UI_T_ENABLED_WAITING_FOR_WINDOW));
                subtitle = dynamic;
            } else {
                subtitle = model->bedtime_policy.enabled ? (ptc_ui_text(PTC_UI_T_NO_UPCOMING_WINDOW_TO_SKIP)) : (ptc_ui_text(PTC_UI_T_CURRENTLY_OFF));
            }
        } else if (index == 5) {
            if (!fresh) subtitle = ptc_ui_text(PTC_UI_T_PRESS_Y_TO_REFRESH_STATUS);
            else if (model->daily_buffer_minutes == 0) subtitle = ptc_ui_text(PTC_UI_T_CURRENTLY_OFF_CONFIGURE_IN_PLANS);
            else if (model->daily_buffer_claimed) subtitle = ptc_ui_text(PTC_UI_T_CLAIMED_TODAY_RESUMES_TOMORROW);
            else if (model->daily_buffer_available) {
                snprintf(dynamic, sizeof(dynamic), ptc_ui_text(PTC_UI_T_AVAILABLE_TODAY_U_MIN), (unsigned int)model->daily_buffer_minutes);
                subtitle = dynamic;
            } else subtitle = ptc_ui_text(PTC_UI_T_NOT_AVAILABLE_TODAY);
        } else if (index == 6) {
            if (unavailable) subtitle = unavailable;
            else {
                ptc_ui_format_eye_care_cycle(model, now, dynamic, sizeof(dynamic));
                subtitle = dynamic;
            }
        }
        action.title = title;
        action.subtitle = subtitle;
        draw_action_card(pixels, stride, box, &action, focused,
                         disabled ? PTC_UI_ACTION_DISABLED : PTC_UI_ACTION_AVAILABLE,
                         (index == 0 || index >= 2) ? (is_en ? 76 : 82) : 0);
        if (index == 1 && unavailable)
            draw_text(pixels, stride, box.x + 78, box.y + 88,
                      ptc_ui_text(PTC_UI_T_TO_RESTORE_LIMIT_USE_SET_TODAY_LIMIT), 12, UI_DISABLED);
        if (index == 0) {
            int t_width = measure_text(adjustment_badge, 12) + 16;
            if (t_width < 68) t_width = 68;
            UiRect tbadge = {box.x + box.width - t_width - 10, box.y + 10, t_width, 22};
            uint32_t badge_color = (strcmp(adjustment_badge, ptc_ui_text(PTC_UI_T_ADJUST_BADGE_ACTIVE)) == 0 ||
                                    strcmp(adjustment_badge, ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED)) == 0 ||
                                    strcmp(adjustment_badge, "Active") == 0 ||
                                    strcmp(adjustment_badge, "Unlimited") == 0) ? UI_SUCCESS :
                (strcmp(adjustment_badge, ptc_ui_text(PTC_UI_T_BEDTIME_ACTIVE)) == 0 ||
                 strcmp(adjustment_badge, "Bedtime") == 0 ? UI_WARNING :
                (strcmp(adjustment_badge, ptc_ui_text(PTC_UI_T_CONTROL_DEACTIVATION)) == 0 || strcmp(adjustment_badge, ptc_ui_text(PTC_UI_T_ADJUST_BADGE_RECOVERING)) == 0 ||
                 strcmp(adjustment_badge, "Disabled") == 0 ? UI_DANGER :
                 (strcmp(adjustment_badge, ptc_ui_text(PTC_UI_T_WAITING_FOR_EFFECT)) == 0 || strcmp(adjustment_badge, ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED)) == 0 ||
                  strcmp(adjustment_badge, "Pending") == 0 ? UI_WARNING : UI_MUTED)));
            fill_round_rect(pixels, stride, tbadge, 6,
                            badge_color == UI_SUCCESS ? UI_SUCCESS_SOFT :
                            (badge_color == UI_DANGER ? UI_DANGER_SOFT :
                             (badge_color == UI_WARNING ? UI_WARNING_SOFT : UI_PAGE)));
            draw_rect_outline(pixels, stride, tbadge, 6, 1, badge_color);
            draw_text_center(pixels, stride, tbadge, adjustment_badge, 12, badge_color);
        } else if (index >= 2) {
            const char *badge = (!fresh || eye_needs_refresh) ? (ptc_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING)) :
                (index == 2 ? (model->today_override_present &&
                                model->today_override_rule.mode == PTC_RULE_MODE_UNLIMITED ? (ptc_ui_text(PTC_UI_T_ENABLED_2)) : (ptc_ui_text(PTC_UI_T_DISABLED))) :
                 index == 3 ? (model->today_override_cleared_in_session &&
                               !model->today_override_present ? (ptc_ui_text(PTC_UI_T_ADJUST_BADGE_CLEARED)) : (ptc_ui_text(PTC_UI_T_ACTIVE))) :
                 index == 4 ? (model->bedtime_active && !model->bedtime_skipped ? (ptc_ui_text(PTC_UI_T_RESTRICTED)) :
                               (bedtime_skip_matches ? (ptc_ui_text(PTC_UI_T_SKIPPED)) : (ptc_ui_text(PTC_UI_T_NOT_SKIPPED)))) :
                 index == 5 ? (model->daily_buffer_claimed ? (ptc_ui_text(PTC_UI_T_CLAIMED)) :
                               (model->daily_buffer_available ? (ptc_ui_text(PTC_UI_T_AVAILABLE)) : (ptc_ui_text(PTC_UI_T_UNCLAIMED)))) :
                 (!model->eye_care_policy.enabled ? ptc_ui_text(PTC_UI_T_DISABLED) :
                  (strcmp(model->eye_care_phase, "resting") == 0 ? ptc_ui_text(PTC_UI_T_EYE_CARE_BADGE_RESTING) :
                   ptc_ui_text(PTC_UI_T_EYE_CARE_BADGE_IDLE))));
            uint32_t color = (!fresh || eye_needs_refresh) ? UI_WARNING :
                ((index == 4 && model->bedtime_active && !model->bedtime_skipped) ||
                 (index == 6 && strcmp(model->eye_care_phase, "resting") == 0) ? UI_DANGER :
                 (strcmp(badge, ptc_ui_text(PTC_UI_T_ENABLED_2)) == 0 || strcmp(badge, ptc_ui_text(PTC_UI_T_SKIPPED)) == 0 ||
                  strcmp(badge, ptc_ui_text(PTC_UI_T_CLAIMED)) == 0 || strcmp(badge, "Enabled") == 0 ||
                  strcmp(badge, "Skipped") == 0 || strcmp(badge, "Claimed") == 0 ? UI_SUCCESS : UI_MUTED));
            int b_width = measure_text(badge, 12) + 16;
            if (b_width < 72) b_width = 72;
            UiRect tbadge = {box.x + box.width - b_width - 10, box.y + 10, b_width, 22};
            fill_round_rect(pixels, stride, tbadge, 6, color == UI_DANGER ? UI_DANGER_SOFT :
                            (color == UI_SUCCESS ? UI_SUCCESS_SOFT : UI_PAGE));
            draw_rect_outline(pixels, stride, tbadge, 6, 1, color);
            draw_text_center(pixels, stride, tbadge, badge, 12, color);
        }
    }
    home_button(pixels, stride, ptc_ui_home_details_rect(true), ptc_ui_text(PTC_UI_T_VIEW_DETAILS), false, false, model->waiting);
}

static void draw_grant_help(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 452};
    (void)model;
    fill_round_rect(pixels, stride, panel, 16, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    draw_text(pixels, stride, 868, 222, ptc_ui_text(PTC_UI_T_GRANT_CODE_GUIDE), 24, UI_RGB(UI_BLENDED(text_primary)));
    char fitted_grant_sub[128];
    fit_text(fitted_grant_sub, sizeof(fitted_grant_sub),
             ptc_ui_text(PTC_UI_T_NO_HOST_NETWORKING_REQUIRED_OFFLINE_SECURE_SIGNATURE),
             14, panel.width - 52);
    draw_text(pixels, stride, 868, 258, fitted_grant_sub, 14, UI_MUTED);

    /* 3 个步骤卡片 */
    const char *STEP_TITLES[] = {ptc_ui_text(PTC_UI_T_1_SELECT_EXTRA_TIME), ptc_ui_text(PTC_UI_T_2_VERIFY_PIN_GENERATE), ptc_ui_text(PTC_UI_T_3_CHILD_ENTERS_8_DIGIT_CODE)};
    const char *STEP_HINTS[] = {ptc_ui_text(PTC_UI_T_SUPPORTS_15_30_60_MINUTES_OR_CUSTOM), ptc_ui_text(PTC_UI_T_THE_SIGNATURE_KEY_IS_CALCULATED_LOCALLY_EFFECTIVELY), ptc_ui_text(PTC_UI_T_CHILDREN_ENTER_8_DIGIT_PURE_NUMBERS_ON)};
    for (int s = 0; s < 3; ++s) {
        UiRect step_box = {864, 280 + s * 72, panel.width - 44, 62};
        fill_round_rect(pixels, stride, step_box, 10, UI_RAISED);
        draw_rect_outline(pixels, stride, step_box, 10, 1, UI_BORDER);
        draw_text(pixels, stride, step_box.x + 14, step_box.y + 24, STEP_TITLES[s], 17, UI_RGB(UI_BLENDED(text_primary)));
        char fitted_hint[128];
        fit_text(fitted_hint, sizeof(fitted_hint), STEP_HINTS[s], 13, step_box.width - 28);
        draw_text(pixels, stride, step_box.x + 14, step_box.y + 48, fitted_hint, 13, UI_MUTED);
    }
    draw_wrapped_text(pixels, stride, 868, 524, ptc_ui_text(PTC_UI_T_NOTE_THE_EXTRA_TIME_CODE_IS_ONLY), 14, 332, 20, 3, UI_RGB(UI_BLENDED(text_secondary)));
}

static void draw_diagnostic_notice(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect rect = {54, 522, 1172, 128};
    uint32_t accent = model->diagnostic_status == PTC_UI_DIAGNOSTIC_SUCCESS
        ? UI_SUCCESS
        : (model->diagnostic_status == PTC_UI_DIAGNOSTIC_ERROR
            ? UI_DANGER : UI_WARNING);
    char line[320];
    fill_round_rect(pixels, stride, rect, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, rect, 16, 1, UI_BORDER);
    fill_round_rect(pixels, stride, (UiRect){rect.x + 12, rect.y + 20, 4, rect.height - 40}, 2, accent);
    if (model->diagnostic_status == PTC_UI_DIAGNOSTIC_EXPORTING) {
        draw_text(pixels, stride, rect.x + 24, rect.y + 36, ptc_ui_text(PTC_UI_T_EXPORTING_DIAGNOSTIC_PACKAGE), 21, accent);
        draw_text(pixels, stride, rect.x + 24, rect.y + 76,
                  ptc_ui_text(PTC_UI_T_THE_DIAGNOSTIC_PACKAGE_EXCLUDES_KEYS_PINS_OFFLINE), 17, UI_MUTED);
        return;
    }
    if (model->diagnostic_status == PTC_UI_DIAGNOSTIC_ERROR) {
        draw_text(pixels, stride, rect.x + 24, rect.y + 36, ptc_ui_text(PTC_UI_T_DIAGNOSTIC_PACKAGE_EXPORT_FAILED), 21, accent);
        draw_text(pixels, stride, rect.x + 24, rect.y + 76,
                  ptc_ui_text(PTC_UI_T_PLEASE_CONFIRM_THAT_THE_SD_CARD_IS), 17, UI_MUTED);
        return;
    }
    snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_DIAGNOSTIC_BUNDLE_EXPORTED_S), model->diagnostic_path);
    draw_text(pixels, stride, rect.x + 24, rect.y + 32, line, 17, UI_INK);
    draw_text(pixels, stride, rect.x + 24, rect.y + 66,
              ptc_ui_text(PTC_UI_T_IF_YOU_ENCOUNTER_PROBLEMS_PLEASE_ATTACH_THIS), 17, UI_INK);
    draw_text(pixels, stride, rect.x + 24, rect.y + 100,
              ptc_ui_text(PTC_UI_T_GITHUB_ADDRESS_HTTPS_GITHUB_COM_SELFUPPEN_NX), 17, accent);
}

void draw_parent(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *TITLES[] = {ptc_ui_text(PTC_UI_T_TODAY_SCHEDULE), ptc_ui_text(PTC_UI_T_TIME_PLANS), ptc_ui_text(PTC_UI_T_OFFLINE_GRANTS), ptc_ui_text(PTC_UI_T_SECURITY_PREFS), ptc_ui_text(PTC_UI_T_SUPPORT_RECOVERY)};
    const UiAction *actions;
    int action_count;
    int index;
    const bool plan_subpage = model->parent_page == PTC_UI_PARENT_PLAN &&
        model->plan_page != PTC_UI_PLAN_PAGE_ROOT;
    const char *title = TITLES[model->parent_page >= 0 && model->parent_page < PTC_UI_PARENT_PAGE_COUNT
        ? model->parent_page : 0];
    if (model->parent_page == PTC_UI_PARENT_PLAN) {
        if (model->plan_page == PTC_UI_PLAN_PAGE_WEEKLY) title = ptc_ui_text(PTC_UI_T_WEEKLY_PLAN);
        else if (model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) title = ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAYS);
        else if (model->plan_page == PTC_UI_PLAN_PAGE_BEDTIME) title = ptc_ui_text(PTC_UI_T_BEDTIME);
    }
    draw_header(pixels, stride, title,
        model->parent_page == PTC_UI_PARENT_SUPPORT ? ptc_ui_text(PTC_UI_T_COMPATIBILITY_STATUS_DIAGNOSTICS_AND_SAFE_RECOVERY) :
         (model->parent_page == PTC_UI_PARENT_PLAN ? ptc_ui_text(PTC_UI_T_CREDIT_RULES_AND_BEDTIME_PLAN) :
         (model->parent_page == PTC_UI_PARENT_TODAY ? ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_ADJUSTMENT_AND_SINGLE_MEASURES) :
          ptc_ui_text(PTC_UI_T_LOCAL_RULES_AND_DEVICE_SECURITY_SETTINGS))));
    draw_time_status_bar(pixels, stride, model);
    draw_tabs(pixels, stride, model);
    if (!plan_subpage && model->parent_page != PTC_UI_PARENT_TODAY) {
        actions = actions_for_page(model->parent_page, &action_count);
        if (model->parent_page == PTC_UI_PARENT_PLAN) {
            UiRect quota_zone = {42, 172, 388, 452};
            UiRect parallel_zone = {428, 172, 388, 452};
            fill_round_rect(pixels, stride, quota_zone, 16, UI_RAISED);
            draw_rect_outline(pixels, stride, quota_zone, 16, 1, UI_BORDER);
            const char *q_text = ptc_ui_text(PTC_UI_T_QUOTA_RULES);
            int qw = measure_text(q_text, 12) + 16;
            if (qw < 76) qw = 76;
            UiRect qbadge = {54, 178, qw, 22};
            fill_round_rect(pixels, stride, qbadge, 6, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, qbadge, 6, 1, UI_ACCENT);
            draw_text_center(pixels, stride, qbadge, q_text, 12, UI_ACCENT);
            draw_text(pixels, stride, 54 + qw + 8, 195, ptc_ui_text(PTC_UI_T_APPLIES_FROM_TOP_TO_BOTTOM), 13, UI_MUTED);

            fill_round_rect(pixels, stride, parallel_zone, 16, UI_RAISED);
            draw_rect_outline(pixels, stride, parallel_zone, 16, 1, UI_BORDER);
            const char *p_text = ptc_ui_text(PTC_UI_T_SEPARATE);
            int pw = measure_text(p_text, 12) + 16;
            if (pw < 76) pw = 76;
            UiRect pbadge = {440, 178, pw, 22};
            fill_round_rect(pixels, stride, pbadge, 6, UI_WARNING_SOFT);
            draw_rect_outline(pixels, stride, pbadge, 6, 1, UI_WARNING);
            draw_text_center(pixels, stride, pbadge, p_text, 12, UI_WARNING);
            draw_text(pixels, stride, 440 + pw + 8, 195, ptc_ui_text(PTC_UI_T_OPERATES_INDEPENDENTLY), 13, UI_MUTED);

        }
        if (model->parent_page == PTC_UI_PARENT_GRANT) {
            UiRect grant_banner = {54, 484, 750, 144};
            fill_round_rect(pixels, stride, grant_banner, 16, UI_SURFACE);
            draw_rect_outline(pixels, stride, grant_banner, 16, 1, UI_BORDER);
            fill_round_rect(pixels, stride, (UiRect){grant_banner.x + 16, grant_banner.y + 18, 4, grant_banner.height - 36}, 2, UI_ACCENT);
            draw_text(pixels, stride, grant_banner.x + 32, grant_banner.y + 36, ptc_ui_text(PTC_UI_T_OFFLINE_GRANT_CODE_USAGE_GUIDE), 18, UI_INK);
            char fitted_grant[256];
            fit_text(fitted_grant, sizeof(fitted_grant),
                     ptc_ui_text(PTC_UI_T_PARENTS_CAN_DIRECTLY_SELECT_THE_SHORTCUT_TIME), 14, grant_banner.width - 48);
            draw_text(pixels, stride, grant_banner.x + 32, grant_banner.y + 68, fitted_grant, 14, UI_RGB(UI_BLENDED(text_secondary)));
            fit_text(fitted_grant, sizeof(fitted_grant),
                     ptc_ui_text(PTC_UI_T_AFTER_REDEMPTION_IT_WILL_AUTOMATICALLY_BE_INCLUDED), 14, grant_banner.width - 48);
            draw_text(pixels, stride, grant_banner.x + 32, grant_banner.y + 96, fitted_grant, 14, UI_RGB(UI_BLENDED(text_secondary)));
            fit_text(fitted_grant, sizeof(fitted_grant),
                     ptc_ui_text(PTC_UI_T_THE_EXTRA_TIME_CODE_INCLUDES_ANTI_REPLAY), 14, grant_banner.width - 48);
            draw_text(pixels, stride, grant_banner.x + 32, grant_banner.y + 124, fitted_grant, 14, UI_MUTED);
        }
        for (index = 0; index < action_count; ++index) {
            UiRect card = to_uirect(model->parent_page == PTC_UI_PARENT_SUPPORT
                ? ptc_ui_support_card_rect(index) : (model->parent_page == PTC_UI_PARENT_PLAN
                    ? ptc_ui_plan_card_rect(index) : (model->parent_page == PTC_UI_PARENT_SETTINGS
                        ? ptc_ui_settings_card_rect(index) : ptc_ui_parent_card_rect(index))));
            PtcUiActionState astate = PTC_UI_ACTION_AVAILABLE;
            if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
                if (!ptc_ui_safety_action_visible(model, index)) continue;
                astate = ptc_ui_safety_action_available(model, index);
                if (astate != PTC_UI_ACTION_DISABLED)
                    astate = index == ptc_ui_support_recommended_action(model)
                        ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_AVAILABLE;
            }
            const UiAction *action = &actions[index];
            UiAction dynamic_action;
            if (model->parent_page == PTC_UI_PARENT_SUPPORT && model->disable_flag_present && index == 0) {
                action = ptc_ui_runtime_fingerprint_reconfirmation_needed(model)
                    ? &RECONFIRM_ENVIRONMENT_ACTION : &RESUME_CONTROL_ACTION;
            }
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 3) {
                const char *detail = ptc_ui_text(PTC_UI_T_STATUS_IS_UNKNOWN_PLEASE_CHECK_AGAIN);
                dynamic_action = *action;
                if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_OFF) {
                    detail = ptc_ui_text(PTC_UI_T_IS_CURRENTLY_NOT_ENABLED);
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED) {
                    detail = ptc_ui_text(PTC_UI_T_IS_CURRENTLY_ON_HOLD_DOWN_X_AND);
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_ANOMALY) {
                    detail = ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED_PLEASE_VIEW_DETAILS);
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
                    detail = ptc_ui_text(PTC_UI_T_EXTERNAL_CONFIGURATION_ENTRY_AVAILABLE);
                }
                /* Use the card's single subtitle row; a second row at the same y overlaps it. */
                dynamic_action.subtitle = detail;
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 0) {
                dynamic_action = *action;
                bool scheduled_active = (strcmp(model->rule_source, "scheduled_override") == 0);
                bool bedtime_enforcing = (model->bedtime_active && !model->bedtime_skipped);
                dynamic_action.subtitle = scheduled_active
                    ? (bedtime_enforcing ? ptc_ui_text(PTC_UI_T_THE_PLAN_IS_IN_EFFECT_WITH_CURRENT) : ptc_ui_text(PTC_UI_T_THIS_PLAN_IS_CURRENTLY_USED_TO_DETERMINE))
                    : (model->scheduled_override.enabled ? ptc_ui_text(PTC_UI_T_IS_ENABLED_TODAY_IS_NOT_WITHIN_THE) : ptc_ui_text(PTC_UI_T_IS_CURRENTLY_CLOSED));
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 1) {
                dynamic_action = *action;
                bool holiday_active = (strcmp(model->rule_source, "statutory_holiday") == 0 ||
                                       strcmp(model->rule_source, "makeup_workday") == 0);
                bool bedtime_enforcing = (model->bedtime_active && !model->bedtime_skipped);
                dynamic_action.subtitle = holiday_active
                    ? (bedtime_enforcing ? ptc_ui_text(PTC_UI_T_IS_IN_EFFECT_DURING_HOLIDAYS_CURRENTLY_UNDER) :
                       (strcmp(model->rule_source, "statutory_holiday") == 0
                        ? ptc_ui_text(PTC_UI_T_IS_CURRENTLY_IN_EFFECT_AND_IS_A) : ptc_ui_text(PTC_UI_T_CURRENTLY_IN_EFFECT_THE_NATIONAL_REST_DAYS)))
                    : (model->holiday_enabled ? ptc_ui_text(PTC_UI_T_IS_ENABLED_TODAY_IS_NOT_A_HOLIDAY) : ptc_ui_text(PTC_UI_T_IS_CURRENTLY_CLOSED_RULES_CAN_BE_PRESET));
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 2) {
                dynamic_action = *action;
                bool scheduled_active = (strcmp(model->rule_source, "scheduled_override") == 0);
                bool holiday_active = (strcmp(model->rule_source, "statutory_holiday") == 0 ||
                                       strcmp(model->rule_source, "makeup_workday") == 0);
                bool today_active = (strcmp(model->rule_source, "today_override") == 0);
                bool bedtime_enforcing = (model->bedtime_active && !model->bedtime_skipped);
                if (today_active) {
                    dynamic_action.subtitle = ptc_ui_text(PTC_UI_T_TODAY_S_ADJUSTMENT_WILL_BE_USED_FIRST);
                } else if (scheduled_active) {
                    dynamic_action.subtitle = ptc_ui_text(PTC_UI_T_THE_SPECIFIED_DATE_QUOTA_WILL_BE_USED);
                } else if (holiday_active) {
                    dynamic_action.subtitle = ptc_ui_text(PTC_UI_T_HOLIDAY_SETTINGS_WILL_BE_USED_FIRST_TODAY);
                } else {
                    dynamic_action.subtitle = bedtime_enforcing
                        ? ptc_ui_text(PTC_UI_T_WEEKLY_QUOTA_IS_IN_EFFECT_CURRENT_BEDTIME) : ptc_ui_text(PTC_UI_T_IS_CURRENTLY_IN_EFFECT_BASIC_QUOTA_FROM);
                }
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 3) {
                dynamic_action = *action;
                bool bedtime_enforcing = (model->bedtime_active && !model->bedtime_skipped);
                dynamic_action.subtitle = model->bedtime_policy.enabled
                    ? (bedtime_enforcing ? ptc_ui_text(PTC_UI_T_THE_CURRENT_RESTRICTION_IS_IN_EFFECT) : ptc_ui_text(PTC_UI_T_IS_CURRENTLY_OPEN_WAITING_FOR_THE_NEXT))
                    : ptc_ui_text(PTC_UI_T_IS_CURRENTLY_CLOSED);
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 4) {
                static char autonomy_detail[64];
                dynamic_action = *action;
                if (model->autonomy_policy.daily_buffer_minutes > 0u) {
                    snprintf(autonomy_detail, sizeof(autonomy_detail), ptc_ui_text(PTC_UI_T_CURRENTLY_U_MIN_DAY),
                        (unsigned int)model->autonomy_policy.daily_buffer_minutes);
                } else {
                    snprintf(autonomy_detail, sizeof(autonomy_detail), ptc_ui_text(PTC_UI_T_CURRENTLY_OFF));
                }
                dynamic_action.subtitle = autonomy_detail;
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 5) {
                static char eye_detail[80];
                dynamic_action = *action;
                if (model->eye_care_policy.enabled) {
                    snprintf(eye_detail, sizeof(eye_detail), "%u / %u %s",
                        (unsigned)model->eye_care_policy.play_minutes,
                        (unsigned)model->eye_care_policy.rest_minutes,
                        ptc_ui_text(PTC_UI_T_MIN));
                } else snprintf(eye_detail, sizeof(eye_detail), "%s", ptc_ui_text(PTC_UI_T_CURRENTLY_OFF));
                dynamic_action.subtitle = eye_detail;
                action = &dynamic_action;
            }
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 0) {
                dynamic_action = *action;
                dynamic_action.subtitle = ptc_ui_theme_preference_label(g_theme.preference);
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 2) {
                static char shortcut_detail[112];
                dynamic_action = *action;
                if (model->custom_shortcut_enabled && model->custom_shortcut_label[0]) {
                    snprintf(shortcut_detail, sizeof(shortcut_detail), ptc_ui_text(PTC_UI_T_CURRENT_S),
                             model->custom_shortcut_label);
                } else {
                    snprintf(shortcut_detail, sizeof(shortcut_detail), ptc_ui_text(PTC_UI_T_CURRENT_MINUS));
                }
                dynamic_action.subtitle = shortcut_detail;
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 5) {
                dynamic_action = *action;
                dynamic_action.subtitle = ptc_audio_is_enabled() ? (ptc_ui_text(PTC_UI_T_ON)) : (ptc_ui_text(PTC_UI_T_MUTED));
                dynamic_action.accent = ptc_audio_is_enabled() ? UI_SUCCESS : UI_MUTED;
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 6) {
                dynamic_action = *action;
                dynamic_action.subtitle = ptc_ui_language_preference_label(model->language_preference);
                action = &dynamic_action;
            }
            if (model->parent_page == PTC_UI_PARENT_PLAN &&
                !ptc_ui_status_is_fresh(model, ptc_ui_render_now())) {
                dynamic_action = *action;
                dynamic_action.subtitle = ptc_ui_text(PTC_UI_T_PRESS_Y_TO_REFRESH_TO_CONFIRM_THE);
                action = &dynamic_action;
            }
            /* Status badges live in the card's top-right corner.  The one-line
             * title/status block is vertically centered below that corner, so
             * it can use the full remaining width without being truncated. */
            int reserved_right = 0;
            draw_action_card(pixels, stride, card, action, index == model->selected_index && !model->parent_footer_focused, astate,
                             reserved_right);
            if (model->parent_page == PTC_UI_PARENT_PLAN) {
                bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
                bool scheduled_active = (strcmp(model->rule_source, "scheduled_override") == 0);
                bool holiday_active = (strcmp(model->rule_source, "statutory_holiday") == 0 ||
                                       strcmp(model->rule_source, "makeup_workday") == 0);
                bool today_active = (strcmp(model->rule_source, "today_override") == 0);
                bool weekly_active = !scheduled_active && !holiday_active && !today_active;
                bool bedtime_enforcing = (model->bedtime_active && !model->bedtime_skipped);
                bool is_active_rule = (index == 0 && scheduled_active) ||
                                      (index == 1 && holiday_active) ||
                                      (index == 2 && weekly_active) ||
                                      (index == 3 && bedtime_enforcing);

                /* 当前生效规则的高亮描边（非聚焦时呈现） */
                if (fresh && is_active_rule && !(index == model->selected_index && !model->parent_footer_focused)) {
                    uint32_t active_border = (index == 3 ? UI_DANGER : (index == 1 ? UI_SUCCESS : UI_ACCENT));
                    draw_rect_outline(pixels, stride, card, 16, 2, active_border);
                }

                const char *badge_label = !fresh ? ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED) :
                    index == 0 ? (scheduled_active ? ptc_ui_text(PTC_UI_T_ACTIVE_2) :
                                  (model->scheduled_override.enabled ? ptc_ui_text(PTC_UI_T_ENABLED) : ptc_ui_text(PTC_UI_T_DISABLED_2))) :
                    index == 1 ? (holiday_active ? ptc_ui_text(PTC_UI_T_ACTIVE_2) :
                                  (model->holiday_enabled ? ptc_ui_text(PTC_UI_T_ENABLED) : ptc_ui_text(PTC_UI_T_DISABLED_2))) :
                    index == 2 ? (weekly_active ? ptc_ui_text(PTC_UI_T_ACTIVE_2) : ptc_ui_text(PTC_UI_T_NOT_ACTIVE)) :
                    index == 3 ? (bedtime_enforcing ? ptc_ui_text(PTC_UI_T_RESTRICTED) :
                                  (ptc_ui_bedtime_skip_matches_policy(model, &model->bedtime_policy) ? ptc_ui_text(PTC_UI_T_SKIPPED) :
                                   (model->bedtime_policy.enabled ? ptc_ui_text(PTC_UI_T_ENABLED) : ptc_ui_text(PTC_UI_T_DISABLED_2)))) :
                    index == 4 ? (model->autonomy_policy.daily_buffer_minutes == 0 ? ptc_ui_text(PTC_UI_T_DISABLED_2) :
                                  (model->daily_buffer_claimed ? ptc_ui_text(PTC_UI_T_RECEIVED_TODAY) :
                                   (model->daily_buffer_available ? ptc_ui_text(PTC_UI_T_AVAILABLE_TODAY) : ptc_ui_text(PTC_UI_T_ENABLED)))) :
                    ptc_ui_eye_care_plan_badge_label(model);
                int p_width = measure_text(badge_label, 12) + 16;
                if (p_width < 76) p_width = 76;
                UiRect pbadge = {card.x + card.width - p_width - 12, card.y + 8, p_width, 22};
                uint32_t badge_color = !fresh ? UI_WARNING :
                    bedtime_enforcing && index == 3 ? UI_DANGER :
                    (strcmp(badge_label, ptc_ui_text(PTC_UI_T_ACTIVE_2)) == 0 ||
                     strcmp(badge_label, ptc_ui_text(PTC_UI_T_AVAILABLE_TODAY)) == 0 ||
                     strcmp(badge_label, ptc_ui_text(PTC_UI_T_SKIPPED)) == 0 ? UI_SUCCESS :
                     (strcmp(badge_label, ptc_ui_text(PTC_UI_T_DISABLED_2)) == 0 ||
                       strcmp(badge_label, ptc_ui_text(PTC_UI_T_NOT_ACTIVE)) == 0 ? UI_MUTED : UI_ACCENT));
                fill_round_rect(pixels, stride, pbadge, 6,
                                badge_color == UI_DANGER ? UI_DANGER_SOFT :
                                (badge_color == UI_SUCCESS ? UI_SUCCESS_SOFT :
                                 (badge_color == UI_WARNING ? UI_WARNING_SOFT : UI_PAGE)));
                draw_rect_outline(pixels, stride, pbadge, 6, 1, badge_color);
                draw_text_center(pixels, stride, pbadge, badge_label, 12, badge_color);
            } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 3) {
                const char *state_label = ptc_ui_text(PTC_UI_T_STATUS_UNKNOWN);
                uint32_t state_color = UI_DANGER;
                if (model->album_restriction_state == 0) {
                    state_label = ptc_ui_text(PTC_UI_T_NOT_ENABLED);
                    state_color = UI_MUTED;
                } else if (model->album_restriction_state == 1) {
                    state_label = ptc_ui_text(PTC_UI_T_ENABLED);
                    state_color = UI_SUCCESS;
                } else if (model->album_restriction_state == 2) {
                    state_label = ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED_2);
                    state_color = UI_WARNING;
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
                    state_label = ptc_ui_text(PTC_UI_T_EXTERNAL_CONFIGURATION);
                    state_color = UI_ACCENT;
                }
                int st_width = measure_text(state_label, 12) + 16;
                if (st_width < 72) st_width = 72;
                UiRect badge = {card.x + card.width - st_width - 14, card.y + 8, st_width, 24};
                fill_round_rect(pixels, stride, badge, 6, UI_PAGE);
                draw_text_center(pixels, stride, badge, state_label, 12, state_color);
            } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 5) {
                bool enabled = ptc_audio_is_enabled();
                const char *state_label = enabled ? ptc_ui_text(PTC_UI_T_ENABLED) : ptc_ui_text(PTC_UI_T_MUTED_2);
                uint32_t state_color = enabled ? UI_SUCCESS : UI_MUTED;
                int au_width = measure_text(state_label, 12) + 16;
                if (au_width < 62) au_width = 62;
                UiRect badge = {card.x + card.width - au_width - 14, card.y + 8, au_width, 24};
                fill_round_rect(pixels, stride, badge, 6, UI_PAGE);
                draw_text_center(pixels, stride, badge, state_label, 12, state_color);
            }
        }
        if (model->parent_page == PTC_UI_PARENT_PLAN) {
            /* 胶囊 1: 优先于节假日规则 (位于卡片 0 底部 316 与卡片 1 顶部 354 之间，y=325) */
            const char *t0 = ptc_ui_text(PTC_UI_T_OVERRIDES_HOLIDAYS);
            int w0 = measure_text(t0, 11) + 16;
            UiRect pill0 = {54 + 20, 325, w0, 20};
            fill_round_rect(pixels, stride, pill0, 10, UI_PAGE);
            draw_rect_outline(pixels, stride, pill0, 10, 1, UI_BORDER);
            draw_text_center(pixels, stride, pill0, t0, 11, UI_ACCENT);

            /* 胶囊 2: 优先于每周常规计划 (位于卡片 1 底部 456 与卡片 2 顶部 494 之间，y=465) */
            const char *t1 = ptc_ui_text(PTC_UI_T_OVERRIDES_WEEKLY_PLAN);
            int w1 = measure_text(t1, 11) + 16;
            UiRect pill1 = {54 + 20, 465, w1, 20};
            fill_round_rect(pixels, stride, pill1, 10, UI_PAGE);
            draw_rect_outline(pixels, stride, pill1, 10, 1, UI_BORDER);
            draw_text_center(pixels, stride, pill1, t1, 11, UI_SUCCESS);
        }
    }
    if (draw_parent_plan_surface(pixels, stride, model)) {
        /* Plan root and editor pages are rendered by ui_render_plan.c. */
    } else if (model->parent_page == PTC_UI_PARENT_TODAY) {
        draw_today_status(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_GRANT) {
        draw_grant_help(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
        draw_safety_status(pixels, stride, model);
    }
    if (model->parent_page == PTC_UI_PARENT_SETTINGS) {
        UiRect help = {842, 176, 384, 452};
        draw_plan_card(pixels, stride, help, false);

        int sel = model->selected_index;
        const char *tag = ptc_ui_text(PTC_UI_T_SYSTEM_PREFERENCES);
        const char *title = ptc_ui_text(PTC_UI_T_APPEARANCE_THEME);
        const char *status_text = ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM);
        uint32_t status_color = UI_ACCENT;
        const char *desc1 = "";
        const char *desc2 = "";
        const char *desc3 = "";
        const char *action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_MODIFY_SETTINGS);

        switch (sel) {
        case 0: /* 外观主题 */
            tag = ptc_ui_text(PTC_UI_T_SYSTEM_PREFERENCES);
            title = ptc_ui_text(PTC_UI_T_APPEARANCE_THEME);
            status_text = ptc_ui_theme_preference_label(g_theme.preference);
            status_color = UI_ACCENT;
            desc1 = ptc_ui_text(PTC_UI_T_PROVIDES_THREE_THEME_MODES_LIGHT_DARK_AND);
            desc2 = ptc_ui_text(PTC_UI_T_DARK_MODE_OPTIMIZES_OLED_SCREEN_POWER_SAVING);
            desc3 = ptc_ui_text(PTC_UI_T_SETTINGS_ARE_SAVED_INSTANTLY_AND_TAKE_EFFECT);
            action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_OPEN_THE_THEME_SWITCHING);
            break;
        case 1: /* 修改 PIN */
            tag = ptc_ui_text(PTC_UI_T_SAFETY_MANAGEMENT);
            title = ptc_ui_text(PTC_UI_T_PARENT_PIN);
            status_text = ptc_ui_text(PTC_UI_T_PROTECTION_ENABLED);
            status_color = UI_SUCCESS;
            desc1 = ptc_ui_text(PTC_UI_T_USED_TO_PROTECT_PARENTAL_ZONE_SETTINGS_HIGH);
            desc2 = ptc_ui_text(PTC_UI_T_SUPPORTS_4_8_DIGIT_PASSWORD_PLEASE_KEEP);
            desc3 = ptc_ui_text(PTC_UI_T_ENTERING_INCORRECTLY_THREE_TIMES_IN_A_ROW);
            action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_CHANGE_THE_MANAGEMENT_PASSWORD);
            break;
        case 2: /* 家长区快捷键 */
            tag = ptc_ui_text(PTC_UI_T_SYSTEM_CONTROL);
            title = ptc_ui_text(PTC_UI_T_QUICK_ENTRANCE_TO_PARENT_AREA);
            status_text = (model->custom_shortcut_enabled && model->custom_shortcut_label[0])
                ? model->custom_shortcut_label : ptc_ui_text(PTC_UI_T_DEFAULT_SHORTCUT_LABEL);
            status_color = UI_ACCENT;
            desc1 = ptc_ui_text(PTC_UI_T_LONG_PRESS_THIS_KEY_ON_ANY_INTERFACE);
            desc2 = ptc_ui_text(PTC_UI_T_SUPPORTS_CUSTOM_CONFIGURATION_OF_MINUS_CAPTURE_OR);
            desc3 = ptc_ui_text(PTC_UI_T_IT_IS_CONVENIENT_FOR_PARENTS_TO_QUICKLY);
            action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_CHANGE_SHORTCUT_KEY_BINDINGS);
            break;
        case 3: /* 自制程序高级入口 */
            tag = ptc_ui_text(PTC_UI_T_ADVANCED_SECURITY);
            title = ptc_ui_text(PTC_UI_T_HOMEBREW_ACCESS);
            if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_OFF) {
                status_text = ptc_ui_text(PTC_UI_T_IS_CURRENTLY_NOT_ENABLED);
                status_color = UI_MUTED;
            } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED) {
                status_text = ptc_ui_text(PTC_UI_T_IS_CURRENTLY_ON);
                status_color = UI_SUCCESS;
            } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_ANOMALY) {
                status_text = ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED_2);
                status_color = UI_WARNING;
            } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
                status_text = ptc_ui_text(PTC_UI_T_EXTERNAL_CONFIGURATION);
                status_color = UI_ACCENT;
            } else {
                status_text = ptc_ui_text(PTC_UI_T_STATUS_UNKNOWN);
                status_color = UI_DANGER;
            }
            desc1 = ptc_ui_text(PTC_UI_T_REDIRECT_PHOTO_ALBUM_ENTRY_TO_HOMEBREW_MENU);
            desc2 = ptc_ui_text(PTC_UI_T_ONCE_ENABLED_HOLD_X_THEN_PRESS_A);
            desc3 = ptc_ui_text(PTC_UI_T_PREVENT_CHILDREN_FROM_BYPASSING_PARENTAL_CONTROL_RESTRICTIONS);
            action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_VIEW_DETAILS_AND_CONFIGURATION);
            break;
        case 4: /* 家庭活动 */
            tag = ptc_ui_text(PTC_UI_T_SECURITY_AUDIT);
            title = ptc_ui_text(PTC_UI_T_FAMILY_ACTIVITY_RECORD);
            status_text = ptc_ui_text(PTC_UI_T_UP_TO_200_ITEMS);
            status_color = UI_MUTED;
            desc1 = ptc_ui_text(PTC_UI_T_RECORDS_CREDIT_ADJUSTMENT_OFFLINE_GRANT_BEDTIME_SKIP);
            desc2 = ptc_ui_text(PTC_UI_T_ONLY_SAVE_LOCAL_SECURITY_AUDIT_LOGS_AND);
            desc3 = ptc_ui_text(PTC_UI_T_SUPPORTS_HANDLE_L_R_FOR_QUICK_PAGE);
            action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_SEE_FULL_ACTIVITY_LOG);
            break;
        case 5: /* 按键与交互音效 */
            tag = ptc_ui_text(PTC_UI_T_SYSTEM_PREFERENCES);
            title = ptc_ui_text(PTC_UI_T_SOUND_EFFECTS);
            status_text = ptc_audio_is_enabled() ? ptc_ui_text(PTC_UI_T_ENABLED) : ptc_ui_text(PTC_UI_T_MUTED_2);
            status_color = ptc_audio_is_enabled() ? UI_SUCCESS : UI_MUTED;
            desc1 = ptc_ui_text(PTC_UI_T_PROVIDES_CRISP_BUTTONS_MICRO_GEAR_WHEEL_AND);
            desc2 = ptc_ui_text(PTC_UI_T_CONTAINS_LONG_PRESS_TO_CONFIRM_COMPLETION_DANGER);
            desc3 = ptc_ui_text(PTC_UI_T_MUTE_DOES_NOT_AFFECT_THE_SWITCH_SYSTEM);
            action_hint = ptc_audio_is_enabled() ? ptc_ui_text(PTC_UI_T_PRESS_A_TO_MUTE_SOUND_EFFECTS) : ptc_ui_text(PTC_UI_T_PRESS_A_TO_TURN_ON_SOUND_EFFECTS);
            break;
        case 6: /* 界面语言 */
            tag = ptc_ui_text(PTC_UI_T_SYSTEM_PREFERENCES);
            title = ptc_ui_text(PTC_UI_T_UI_LANGUAGE);
            status_text = ptc_ui_language_preference_label(model->language_preference);
            status_color = UI_ACCENT;
            desc1 = ptc_ui_text(PTC_UI_T_SET_THE_LANGUAGE_FOR_PLAYWISE_CONSOLE_MANAGEMENT);
            desc2 = ptc_ui_text(PTC_UI_T_SUPPORTS_SIMPLIFIED_CHINESE_TRADITIONAL_CHINESE_ENGLISH_AND);
            desc3 = ptc_ui_text(PTC_UI_T_PREFERENCES_ARE_PERSISTED_IN_THE_SD_CARD);
            action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_OPEN_THE_LANGUAGE_SELECTION);
            break;
        default:
            break;
        }

        /* Render Tag */
        draw_text(pixels, stride, help.x + 24, help.y + 36, tag, 14, UI_ACCENT);

        /* Render Title */
        draw_text(pixels, stride, help.x + 24, help.y + 72, title, 22, UI_RGB(UI_BLENDED(text_primary)));

        /* Render Status Badge */
        UiRect badge = {help.x + help.width - 130, help.y + 24, 106, 26};
        fill_round_rect(pixels, stride, badge, 6, UI_PAGE);
        draw_rect_outline(pixels, stride, badge, 6, 1, status_color);
        draw_text_center(pixels, stride, badge, status_text, 12, status_color);

        /* Divider line */
        UiRect div = {help.x + 24, help.y + 92, help.width - 48, 1};
        fill_round_rect(pixels, stride, div, 0, UI_BORDER);

        /* Descriptions */
        int desc_y = help.y + 128;
        if (desc1 && *desc1) {
            desc_y = draw_wrapped_text(pixels, stride, help.x + 24, desc_y, desc1, 14,
                                      help.width - 48, 16, 2, UI_RGB(UI_BLENDED(text_secondary))) + 6;
        }
        if (desc2 && *desc2) {
            desc_y = draw_wrapped_text(pixels, stride, help.x + 24, desc_y, desc2, 14,
                                      help.width - 48, 16, 2, UI_RGB(UI_BLENDED(text_secondary))) + 6;
        }
        if (desc3 && *desc3) {
            draw_wrapped_text(pixels, stride, help.x + 24, desc_y, desc3, 14,
                              help.width - 48, 16, 2, UI_RGB(UI_BLENDED(text_secondary)));
        }

        /* Action Hint Box */
        UiRect hint_box = {help.x + 24, help.y + 268, help.width - 48, 44};
        fill_round_rect(pixels, stride, hint_box, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, hint_box, 10, 1, UI_BORDER);
        draw_text_center(pixels, stride, hint_box, action_hint, 15, UI_INK);

        /* Bottom guidance */
        draw_wrapped_text(pixels, stride, help.x + 24, help.y + help.height - 68,
                          ptc_ui_text(PTC_UI_T_PRESS_B_WHILE_AWAY_TO_RETURN_TO), 13,
                          help.width - 48, 17, 2, UI_MUTED);
        draw_wrapped_text(pixels, stride, help.x + 24, help.y + help.height - 30,
                          ptc_ui_text(PTC_UI_T_PRESS_Y_TO_REFRESH_DEVICE_STATUS), 13,
                          help.width - 48, 17, 2, UI_MUTED);
    }
    draw_settings_badge(pixels, stride, model);
    if (model->parent_page == PTC_UI_PARENT_SUPPORT &&
        model->diagnostic_status != PTC_UI_DIAGNOSTIC_IDLE) {
        draw_diagnostic_notice(pixels, stride, model);
    } else if (ptc_ui_operation_feedback_visible(model) || !ptc_ui_parent_status_alert_visible(model)) {
        draw_notice(pixels, stride, model);
    }
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(0),
                       ptc_ui_text(PTC_UI_T_L_PREVIOUS_PAGE));
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(1),
                       ptc_ui_text(PTC_UI_T_R_NEXT_PAGE));
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(2),
                       plan_subpage ? ptc_ui_text(PTC_UI_T_B_BACK_TO_PLANS_2) : ptc_ui_text(PTC_UI_T_B_RETURN_TO_CHILD_PAGE));
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(3),
                       ptc_ui_text(PTC_UI_T_Y_REFRESH));
    if (model->parent_footer_focused && model->parent_footer_selection == 0) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_parent_footer_rect(3)),
            12, 3, UI_ACCENT);
    }
    if (!ptc_ui_operation_feedback_visible(model)) {
        draw_parent_status_footer(pixels, stride, model);
    }
}
