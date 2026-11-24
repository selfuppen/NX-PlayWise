#include "ui_render_internal.h"
#include "../../common/update/update.h"

static const UiAction TODAY_ACTIONS[] = {
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_SET_TODAY_LIMIT), PTC_UI_TEXT_REFERENCE(PTC_UI_T_CONTROLLED_BY_RULES), UI_ACCENT, UI_ACTION_ICON_CLOCK, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_QUICK_GRANT), "", UI_SUCCESS, UI_ACTION_ICON_ADD_TIME, UI_ACTION_VISUAL_QUICK_ADD},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_NO_LIMIT_TODAY), PTC_UI_TEXT_REFERENCE(PTC_UI_T_TODAY_ONLY_BEDTIME_STAYS_ON), UI_SUCCESS, UI_ACTION_ICON_INFINITY, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_CLEAR_TODAY_LIMIT), PTC_UI_TEXT_REFERENCE(PTC_UI_T_RESET_TO_REGULAR_PLAN), UI_MUTED, UI_ACTION_ICON_CLEAR_OVERRIDE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_SKIP_BEDTIME), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_WARNING, UI_ACTION_ICON_MOON, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_AUTONOMY_BUFFER_2), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_MUTED, UI_ACTION_ICON_BUFFER, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_EYE_CARE_SKIP), PTC_UI_TEXT_REFERENCE(PTC_UI_T_EYE_CARE_SKIP_NOT_RESTING), UI_WARNING, UI_ACTION_ICON_CLOCK, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_DOCK_WAIVE), PTC_UI_TEXT_REFERENCE(PTC_UI_T_DOCK_ALLOWANCE), UI_SUCCESS, UI_ACTION_ICON_CONSOLE, UI_ACTION_VISUAL_NONE},
};

static const UiAction PLAN_ACTIONS[] = {
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_SPECIFIED_DATE_QUOTA), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_ACCENT, UI_ACTION_ICON_CALENDAR_RANGE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_HOLIDAY_QUOTA), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_SUCCESS, UI_ACTION_ICON_HOLIDAY, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_WEEKLY_QUOTA), PTC_UI_TEXT_REFERENCE(PTC_UI_T_ACTIVE_2), UI_ACCENT, UI_ACTION_ICON_WEEKLY, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_BEDTIME), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_WARNING, UI_ACTION_ICON_MOON, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_EYE_CARE), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_WARNING, UI_ACTION_ICON_CLOCK, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_DOCK_TITLE), "", UI_ACCENT, UI_ACTION_ICON_CONSOLE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_AUTONOMY_BUFFER_2), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_CLOSED), UI_SUCCESS, UI_ACTION_ICON_BUFFER, UI_ACTION_VISUAL_NONE},
};

static const UiAction GRANT_ACTIONS[] = {
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_HOST_GENERATION), PTC_UI_TEXT_REFERENCE(PTC_UI_T_NATIVE_SIGNATURE_8_BIT_CODE), UI_SUCCESS, UI_ACTION_ICON_CONSOLE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_MOBILE_PC_GENERATOR), PTC_UI_TEXT_REFERENCE(PTC_UI_T_AVAILABLE_OFFLINE_ACROSS_DEVICES), UI_ACCENT, UI_ACTION_ICON_DEVICE, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_GRANT_SETTINGS), PTC_UI_TEXT_REFERENCE(PTC_UI_T_DEVICE_AND_KEY_CONFIGURED), UI_MUTED, UI_ACTION_ICON_SLIDERS, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_USAGE_HISTORY), PTC_UI_TEXT_REFERENCE(PTC_UI_T_LAST_100), UI_MUTED, UI_ACTION_ICON_HISTORY, UI_ACTION_VISUAL_NONE},
};

static const UiAction SETTINGS_ACTIONS[] = {
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_CHANGE_PIN), PTC_UI_TEXT_REFERENCE(PTC_UI_T_IS_CURRENTLY_ENABLED), UI_ACCENT, UI_ACTION_ICON_KEY, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_ALERT_PREFERENCES), "", UI_SUCCESS, UI_ACTION_ICON_NOTIFICATION, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_PARENT_SHORTCUT), PTC_UI_TEXT_REFERENCE(PTC_UI_T_CURRENT_MINUS_2), UI_ACCENT, UI_ACTION_ICON_CONTROLLER, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_FAMILY_ACTIVITIES), PTC_UI_TEXT_REFERENCE(PTC_UI_T_LAST_200), UI_MUTED, UI_ACTION_ICON_ACTIVITY, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_HOMEBREW_ACCESS), PTC_UI_TEXT_REFERENCE(PTC_UI_T_NOT_ENABLED), UI_DANGER, UI_ACTION_ICON_HOMEBREW, UI_ACTION_VISUAL_NONE},
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_APPEARANCE_THEME), "", UI_ACCENT, UI_ACTION_ICON_THEME, UI_ACTION_VISUAL_NONE},
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
    {PTC_UI_TEXT_REFERENCE(PTC_UI_T_CONFIG_BACKUP_TITLE), PTC_UI_TEXT_REFERENCE(PTC_UI_T_CONFIG_BACKUP_CARD_NOTE), UI_ACCENT, UI_ACTION_ICON_EXPORT, UI_ACTION_VISUAL_NONE},
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
    PtcUiOverview overview;
    PtcPolicyStatus policies;
    ui_render_overview(model, &overview);
    ptc_ui_render_policy_status(model, &policies);
    draw_plan_card(pixels, stride, box, false);
    uint32_t tone = overview.available ? UI_SUCCESS : overview.blocked ? UI_DANGER : UI_WARNING;
    UiRect verdict = {box.x + 18, box.y + 18, box.width - 36, 66};
    fill_round_rect(pixels, stride, verdict, 10, overview.available ? UI_SUCCESS_SOFT : overview.blocked ? UI_DANGER_SOFT : UI_WARNING_SOFT);
    char fitted[256];
    fit_text(fitted, sizeof(fitted), overview.title, 25, verdict.width - 24);
    draw_text_bold(pixels, stride, verdict.x + 12, verdict.y + 29, fitted, 25, tone);
    fit_text(fitted, sizeof(fitted), overview.hint, 13, verdict.width - 24);
    draw_text(pixels, stride, verdict.x + 12, verdict.y + 52, fitted, 13, tone);
    UiRect quota = {box.x + 18, box.y + 96, box.width - 36, 102};
    fill_round_rect(pixels, stride, quota, 12, UI_HERO);
    draw_text(pixels, stride, quota.x + 16, quota.y + 25, ptc_ui_text(PTC_UI_T_OVERVIEW_BALANCE), 16, UI_HERO_SECONDARY);
    fit_text(fitted, sizeof(fitted), overview.balance, 38, quota.width - 32);
    draw_text_bold(pixels, stride, quota.x + 16, quota.y + 69, fitted, 38, UI_ON_HERO);
    if (overview.fresh && model->played_minutes_available && model->played_minutes >= 0)
        snprintf(fitted, sizeof(fitted), ptc_ui_text(model->usage_present ? PTC_UI_T_CONSOLE_USAGE_D_MIN : PTC_UI_T_PLAYED_D_MIN), model->played_minutes);
    else snprintf(fitted, sizeof(fitted), "%s", ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED));
    char used[128], source[96];
    fit_text(used, sizeof(used), fitted, 12, quota.width / 2 - 16);
    fit_text(source, sizeof(source), overview.fresh ? ui_rule_source_label(model->rule_source) : ptc_ui_text(PTC_UI_T_RULE_TO_CONFIRM), 12, quota.width / 2 - 16);
    draw_text(pixels, stride, quota.x + 16, quota.y + 90, used, 12, UI_HERO_SECONDARY);
    draw_text(pixels, stride, quota.x + quota.width - 16 - measure_text(source, 12), quota.y + 90, source, 12, UI_HERO_SECONDARY);
    char eye[160];
    ptc_ui_format_eye_care_cycle(model, ptc_ui_render_now(), eye, sizeof(eye));
    for (int i = 0; i < 3; ++i) {
        UiRect row = to_uirect(ptc_ui_today_status_rect(8 + i));
        const PtcPolicyDisplay *policy = i == 1 ? &policies.bedtime : &policies.dock;
        const char *label = ptc_ui_text(i == 0 ? PTC_UI_T_EYE_CARE : i == 1 ? PTC_UI_T_BEDTIME_SCHEDULE : PTC_UI_T_DOCK_TITLE);
        const char *detail = i == 0 ? eye : policy->detail;
        const char *badge = ptc_ui_text(i == 0 ? !overview.fresh ? PTC_UI_T_TO_BE_CONFIRMED : !model->eye_care_policy.enabled ? PTC_UI_T_DISABLED_2 : PTC_UI_T_ENABLED : policy->badge);
        PtcPolicyDisplayState state = i == 0 ? !overview.fresh ? PTC_POLICY_UNKNOWN : !model->eye_care_policy.enabled ? PTC_POLICY_OFF :
            strcmp(model->eye_care_phase, "resting") == 0 ? PTC_POLICY_WAITING : PTC_POLICY_AVAILABLE : policy->state;
        draw_home_policy_row(pixels, stride, row, label, detail, badge, state);
        if (model->selected_index == 8 + i && !model->parent_footer_focused && !model->header_language_focused)
            draw_focus_ring(pixels, stride, row, 10);
        if (i == 2) {
            fit_text(fitted, sizeof(fitted), policies.dock_rule, 11, row.width - 24);
            draw_text(pixels, stride, row.x + 12, row.y + 61, fitted, 11, UI_MUTED);
        }
    }
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_STATUS_OPEN_SETTINGS), 12, box.width - 36);
    draw_text(pixels, stride, box.x + 18, box.y + box.height - 18, fitted, 12, UI_ACCENT);
}

static void draw_today_small_action(uint32_t *pixels, uint32_t stride, UiRect box,
    const UiAction *action, bool selected, bool disabled)
{
    draw_plan_card(pixels, stride, box, selected);
    char title[160];
    fit_text(title, sizeof(title), ptc_ui_text_resolve(action->title), 18, box.width - 28);
    draw_text(pixels, stride, box.x + 14, box.y + 47, title, 18, disabled ? UI_DISABLED : UI_INK);
    draw_wrapped_text(pixels, stride, box.x + 14, box.y + 66, ptc_ui_text_resolve(action->subtitle), 12,
        box.width - 28, 16, 2, disabled ? UI_DISABLED : UI_MUTED);
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

/* Shared secondary-page navigation row: back pill on the left, breadcrumb on
   the right. Time-plan sub-pages and safety-preference pages use the same row
   so "back one level" never moves between sections. */
void draw_subpage_nav(uint32_t *pixels, uint32_t stride, const char *back_label, const char *path)
{
    home_button(pixels, stride, ptc_ui_advanced_back_rect(), back_label, false, false, false);
    draw_text(pixels, stride, 278, 146, path, 22, UI_MUTED);
}

static void draw_tabs(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *LABELS[] = {ptc_ui_text(PTC_UI_T_TODAY_SCHEDULE), ptc_ui_text(PTC_UI_T_TIME_PLANS), ptc_ui_text(PTC_UI_T_OFFLINE_GRANTS), ptc_ui_text(PTC_UI_T_SECURITY_PREFS), ptc_ui_text(PTC_UI_T_SUPPORT_RECOVERY)};
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page != PTC_UI_PLAN_PAGE_ROOT) {
        const char *name = model->plan_page == PTC_UI_PLAN_PAGE_WEEKLY ? (ptc_ui_text(PTC_UI_T_WEEKLY_PLAN)) :
            (model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY ? (ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAYS)) :
            (model->plan_page == PTC_UI_PLAN_PAGE_BEDTIME ? (ptc_ui_text(PTC_UI_T_BEDTIME_SCHEDULE)) :
            (model->plan_page == PTC_UI_PLAN_PAGE_DOCK ? ptc_ui_text(PTC_UI_T_DOCK_TITLE) : ptc_ui_text(PTC_UI_T_EYE_CARE))));
        {
            char path[96];
            if (model->today_settings_origin) {
                PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("name", name)};
                (void)ptc_ui_text_format(PTC_UI_T_TODAY_SETTINGS_PATH_NAMED, path, sizeof(path), args, 1);
            } else snprintf(path, sizeof(path), ptc_ui_text(PTC_UI_T_TIME_PLANS_S), name);
            draw_subpage_nav(pixels, stride,
                ptc_ui_text(model->today_settings_origin ? PTC_UI_T_BACK_TO_TODAY : PTC_UI_T_B_BACK_TO_PLANS), path);
        }
        return;
    }

    int index;
    /* 贯通一体式分段轨道槽容器 */
    UiRect track = to_uirect(ptc_ui_parent_tab_track_rect());
    fill_round_rect(pixels, stride, track, 14, UI_SURFACE);
    draw_rect_outline(pixels, stride, track, 14, 1, UI_BORDER);

    /* 首尾嵌入式 L / R 实体肩键帽 */
    UiRect l_key = to_uirect(ptc_ui_parent_shoulder_key_rect(0));
    UiRect r_key = to_uirect(ptc_ui_parent_shoulder_key_rect(1));
    draw_shoulder_key_glyph(pixels, stride, l_key.x, l_key.y, l_key.width, l_key.height, "L", false);
    draw_shoulder_key_glyph(pixels, stride, r_key.x, r_key.y, r_key.width, r_key.height, "R", false);

    for (index = 0; index < PTC_UI_PARENT_PAGE_COUNT; ++index) {
        UiRect tab = to_uirect(ptc_ui_parent_tab_rect(index));
        fill_round_rect(pixels, stride, tab, 10, UI_RAISED);
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
        fill_round_rect(pixels, stride, pill, 10, UI_ACCENT);
    }
    for (index = 0; index < PTC_UI_PARENT_PAGE_COUNT; ++index) {
        UiRect tab = to_uirect(ptc_ui_parent_tab_rect(index));
        bool active = index == (int)model->parent_page;
        const char *status = index == PTC_UI_PARENT_SUPPORT ? ptc_ui_settings_status_label(model) : NULL;
        if (status) {
            /* Share the tab vertically: an overlaid corner badge obscures
               the title, and fixed badge widths truncate longer translations. */
            UiRect title = {tab.x + 8, tab.y + 3, tab.width - 16, 16};
            int width = measure_text(status, 11) + 16;
            if (width > tab.width - 16) width = tab.width - 16;
            UiRect badge = {tab.x + (tab.width - width) / 2, tab.y + tab.height - 17, width, 15};
            uint32_t color = strcmp(status, ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED)) == 0 ? UI_DANGER :
                (strcmp(status, ptc_ui_text(PTC_UI_T_UPDATE_NEW_VERSION_BADGE)) == 0 ? UI_ACCENT : UI_WARNING);
            draw_text_center(pixels, stride, title, LABELS[index], 15, active ? UI_ON_ACCENT : UI_INK);
            fill_round_rect(pixels, stride, badge, 5, color);
            draw_text_center(pixels, stride, badge, status, 11, UI_ON_ACCENT);
        } else {
            draw_text_center(pixels, stride, tab, LABELS[index], 17, active ? UI_ON_ACCENT : UI_INK);
        }
    }
}

static void draw_safety_status(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 452};
    char troubleshoot[160];
    char fitted[160];
    char age[80];
    draw_plan_card(pixels, stride, panel, false);

    int recommended = ptc_ui_support_recommended_action(model);
    bool is_fault = model->disable_flag_present || model->recovery_active ||
        strcmp(model->setup_phase, "failed") == 0 || strcmp(model->setup_phase, "protection") == 0;
    bool is_warn = !is_fault && (recommended >= 0 || model->waiting || model->apply_pending_confirmation ||
        (model->setup_phase[0] && strcmp(model->setup_phase, "active") != 0));

    /* 1. Structured System Health Banner */
    {
        UiRect banner = {panel.x + 18, panel.y + 16, panel.width - 36, 118};
        uint32_t b_bg = is_fault ? UI_DANGER_SOFT : (is_warn ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
        uint32_t b_bd = is_fault ? UI_DANGER_BORDER : (is_warn ? UI_WARNING_BORDER : UI_SUCCESS_BORDER);
        uint32_t b_fg = is_fault ? UI_DANGER : (is_warn ? UI_WARNING_TEXT : UI_SUCCESS);
        uint32_t b_dot = is_fault ? UI_DANGER : (is_warn ? UI_WARNING : UI_SUCCESS);
        const char *badge_txt = ptc_ui_text(is_fault ? PTC_UI_T_SUPPORT_HEALTH_FAULT_BADGE
            : (is_warn ? PTC_UI_T_SUPPORT_HEALTH_WARN_BADGE : PTC_UI_T_SUPPORT_HEALTH_OK_BADGE));
        int bw = measure_text(badge_txt, 12) + 18;
        if (bw > 130) bw = 130;
        UiRect bpill = {banner.x + banner.width - 12 - bw, banner.y + 10, bw, 22};

        fill_round_rect(pixels, stride, banner, 12, b_bg);
        draw_rect_outline(pixels, stride, banner, 12, 1, b_bd);
        fill_round_rect(pixels, stride, (UiRect){banner.x + 12, banner.y + 16, 10, 10}, 5, b_dot);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SUPPORT_HEALTH_TITLE), 15,
                 bpill.x - (banner.x + 28) - 8);
        draw_text_bold(pixels, stride, banner.x + 28, banner.y + 26, fitted, 15, b_fg);

        fill_round_rect(pixels, stride, bpill, 11, UI_SURFACE);
        draw_rect_outline(pixels, stride, bpill, 11, 1, b_bd);
        fit_text(fitted, sizeof(fitted), badge_txt, 12, bpill.width - 10);
        draw_text_center(pixels, stride, bpill, fitted, 12, b_fg);

        draw_wrapped_text(pixels, stride, banner.x + 12, banner.y + 52, ptc_ui_support_problem(model),
                          14, banner.width - 24, 18, 2, UI_INK);
        const char *next = recommended == 0 ? (model->disable_flag_present ? ptc_ui_text(PTC_UI_T_RECOMMENDATION_UNDEACTIVATE_AND_RE_ENABLE) : ptc_ui_text(PTC_UI_T_RECOMMENDATION_RECHECK_AND_ENABLE)) :
                           recommended == 1 ? ptc_ui_text(PTC_UI_T_SUGGESTION_SELECT_RETRY_REPAIR) :
                           recommended == 4 ? ptc_ui_text(PTC_UI_T_SUGGESTION_EXPORT_DIAGNOSTIC_PACKAGE_AND_KEEP_PROBLEM) :
                           (model->waiting || model->apply_pending_confirmation ? ptc_ui_text(PTC_UI_T_PLEASE_WAIT_FOR_THE_RESULT_AND_THEN) : ptc_ui_text(PTC_UI_T_NO_NEED_TO_RESTORE_THE_OPERATION_YOU));
        draw_wrapped_text(pixels, stride, banner.x + 12, banner.y + 92, next, 12,
                          banner.width - 24, 16, 2, b_fg);
    }

    /* 2. Environment & Freshness Summary Card */
    {
        UiRect env_box = {panel.x + 18, panel.y + 142, panel.width - 36, 56};
        fill_round_rect(pixels, stride, env_box, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, env_box, 10, 1, UI_BORDER);
        if (model->environment_available)
            snprintf(troubleshoot, sizeof(troubleshoot), "HOS %s  |  %s", model->environment_hos, model->environment_model);
        else snprintf(troubleshoot, sizeof(troubleshoot), "%s", ptc_ui_text(PTC_UI_T_ENVIRONMENT_DETAILS_ARE_TEMPORARILY_UNAVAILABLE_DIAGNOSTICS_CA));
        fit_text(fitted, sizeof(fitted), troubleshoot, 13, env_box.width - 24);
        draw_text(pixels, stride, env_box.x + 12, env_box.y + 23, fitted, 13, UI_INK);
        format_status_age(model, age, sizeof(age));
        fit_text(fitted, sizeof(fitted), age, 12, env_box.width - 24);
        draw_text(pixels, stride, env_box.x + 12, env_box.y + 44, fitted, 12, status_age_color(model));
    }

    /* 3. Structured Recent Events Card */
    {
        UiRect ev_card = {panel.x + 18, panel.y + 206, panel.width - 36, 186};
        char badge_buf[48];
        uint32_t eb_bg, eb_bd, eb_fg;
        fill_round_rect(pixels, stride, ev_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, ev_card, 12, 1, UI_BORDER);

        if (model->recent_event_count > 0) {
            snprintf(badge_buf, sizeof(badge_buf), ptc_ui_text(PTC_UI_T_SUPPORT_EVENTS_COUNT_BADGE), model->recent_event_count);
            eb_bg = UI_ACCENT_SOFT; eb_bd = UI_ACCENT; eb_fg = UI_ACCENT;
        } else if (model->recent_events_available) {
            snprintf(badge_buf, sizeof(badge_buf), "%s", ptc_ui_text(PTC_UI_T_SUPPORT_EVENTS_CLEAR_BADGE));
            eb_bg = UI_SUCCESS_SOFT; eb_bd = UI_SUCCESS_BORDER; eb_fg = UI_SUCCESS;
        } else {
            snprintf(badge_buf, sizeof(badge_buf), "%s", ptc_ui_text(PTC_UI_T_SUPPORT_EVENTS_UNREAD_BADGE));
            eb_bg = UI_PAGE; eb_bd = UI_BORDER; eb_fg = UI_MUTED;
        }
        int ebw = measure_text(badge_buf, 11) + 16;
        if (ebw > 128) ebw = 128;
        UiRect ebpill = {ev_card.x + ev_card.width - 12 - ebw, ev_card.y + 10, ebw, 22};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_RECENT_EVENTS), 15,
                 ebpill.x - (ev_card.x + 12) - 8);
        draw_text_bold(pixels, stride, ev_card.x + 12, ev_card.y + 26, fitted, 15, UI_INK);
        fill_round_rect(pixels, stride, ebpill, 11, eb_bg);
        draw_rect_outline(pixels, stride, ebpill, 11, 1, eb_bd);
        fit_text(fitted, sizeof(fitted), badge_buf, 11, ebpill.width - 8);
        draw_text_center(pixels, stride, ebpill, fitted, 11, eb_fg);

        if (model->recent_event_count > 0) {
            for (int event_index = 0; event_index < model->recent_event_count; ++event_index) {
                char latest[192];
                char event_time[48];
                int source_index = model->recent_event_count - 1 - event_index;
                format_event_time(model->recent_event_timestamps[source_index], false, event_time, sizeof(event_time));
                snprintf(latest, sizeof(latest), "%s  |  %s", model->recent_events[source_index], event_time);
                UiRect ev_rect = to_uirect(ptc_ui_support_event_rect(event_index));
                UiRect row_box = {ev_rect.x + 8, ev_rect.y, ev_rect.width - 16, ev_rect.height};
                bool row_focused = event_index + 6 == model->selected_index && !model->parent_footer_focused;
                fill_round_rect(pixels, stride, row_box, 8, row_focused ? UI_ACCENT_SOFT : UI_SURFACE);
                if (row_focused) draw_focus_ring(pixels, stride, row_box, 8);
                else draw_rect_outline(pixels, stride, row_box, 8, 1, UI_BORDER);
                fit_text(latest, sizeof(latest), latest, 13, row_box.width - 20);
                draw_text(pixels, stride, row_box.x + 10, row_box.y + 24,
                          latest, 13, row_focused ? UI_ACCENT : UI_INK);
            }
        } else {
            /* Keep unread events calm when system is healthy rather than showing alarming red. */
            const char *no_events_msg = model->recent_events_available
                ? ptc_ui_text(PTC_UI_T_THERE_ARE_NO_RECENT_EVENTS_REQUIRING_ATTENTION)
                : ptc_ui_text(PTC_UI_T_TEMPORARILY_UNABLE_TO_READ_RECENT_EVENTS_PLEASE);
            draw_wrapped_text(pixels, stride, ev_card.x + 14, ev_card.y + 72,
                              no_events_msg, 13, ev_card.width - 28, 20, 3,
                              (model->recent_events_available || !is_fault) ? UI_MUTED : UI_WARNING_TEXT);
        }
    }

    UiRect guide = to_uirect(ptc_ui_support_guide_rect());
    bool guide_focused = model->selected_index == 6 + model->recent_event_count && !model->parent_footer_focused;
    fill_round_rect(pixels, stride, guide, 10, guide_focused ? UI_ACCENT : UI_ACCENT_SOFT);
    if (guide_focused) draw_focus_ring(pixels, stride, guide, 10);
    else draw_rect_outline(pixels, stride, guide, 10, 1, UI_ACCENT);
    draw_text_center(pixels, stride, guide, ptc_ui_text(PTC_UI_T_SUPPORT_GUIDE), 15,
                     guide_focused ? UI_ON_ACCENT : UI_ACCENT);
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
    UiRect quota_group = {480, 176, 764, 230};
    UiRect other_group = {480, 416, 764, 212};
    fill_round_rect(pixels, stride, quota_group, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, quota_group, 16, 1, UI_BORDER);
    const char *title1 = ptc_ui_text(PTC_UI_T_TODAY_S_LIMIT_ADJUSTMENT_TODAY_ONLY);
    const char *hint1 = ptc_ui_text(PTC_UI_T_ORIGINAL_PLAN_RESUMES_TOMORROW);
    char group_title1[160], group_hint1[160];
    fit_text(group_hint1, sizeof(group_hint1), hint1, 13, 400);
    int hint1_w = measure_text(group_hint1, 13);
    fit_text(group_title1, sizeof(group_title1), title1, 16, 712 - hint1_w - 14);
    draw_text(pixels, stride, 506, 195, group_title1, 16, UI_ACCENT);
    draw_text(pixels, stride, 1218 - hint1_w, 195, group_hint1, 13, UI_MUTED);
    fill_round_rect(pixels, stride, other_group, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, other_group, 16, 1, UI_BORDER);
    const char *title2 = ptc_ui_text(PTC_UI_T_BEDTIME_AUTONOMY_BUFFER);
    const char *hint2 = ptc_ui_text(PTC_UI_T_BEDTIME_OPERATES_INDEPENDENTLY);
    char group_title2[160], group_hint2[160];
    fit_text(group_hint2, sizeof(group_hint2), hint2, 13, 400);
    int hint2_w = measure_text(group_hint2, 13);
    fit_text(group_title2, sizeof(group_title2), title2, 16, 712 - hint2_w - 14);
    draw_text(pixels, stride, 506, 435, group_title2, 16, UI_WARNING_TEXT);
    draw_text(pixels, stride, 1218 - hint2_w, 435, group_hint2, 13, UI_MUTED);
    for (int index = 0; index < 8; ++index) {
        UiRect box = to_uirect(ptc_ui_today_card_rect(index));
        bool focused = !model->parent_footer_focused && !model->header_language_focused && model->selected_index == index;
        bool clear_unavailable = index == 3 && fresh &&
                                 !model->today_override_present;
        const char *unavailable = ptc_ui_today_action_unavailable_reason(model, index, now);
        bool eye_needs_refresh = index == 6 && unavailable &&
            strcmp(unavailable, ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_REFRESH)) == 0;
        bool disabled = (model->disable_flag_present && index != 6 && index != 7) || model->waiting ||
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
                else snprintf(dynamic, sizeof(dynamic), "%s", ptc_ui_text(PTC_UI_T_ENABLED_WAITING_FOR_WINDOW));
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
            if (unavailable && (!model->eye_care_policy.enabled ||
                (strcmp(model->eye_care_phase, "resting") != 0 &&
                 strcmp(model->eye_care_phase, "playing") != 0))) subtitle = unavailable;
            else {
                ptc_ui_format_eye_care_cycle(model, now, dynamic, sizeof(dynamic));
                subtitle = dynamic;
            }
        }
        if (index == 7) {
            if (unavailable) subtitle = unavailable;
            else if (model->dock_waived_today) subtitle = ptc_ui_text(PTC_UI_T_DOCK_CARD_WAIVED);
            else if (model->dock_policy.force_docked) subtitle = ptc_ui_text(PTC_UI_T_DOCK_CARD_FORCE);
            else if (model->dock_policy.undocked_limit_enabled) {
                snprintf(dynamic, sizeof(dynamic), ptc_ui_text(PTC_UI_T_DOCK_CARD_LIMIT),
                         model->dock_policy.undocked_daily_minutes, model->undocked_used_minutes);
                subtitle = dynamic;
            } else {
                subtitle = ptc_ui_text(PTC_UI_T_DOCK_CARD_OFF);
            }
        }
        action.title = title;
        action.subtitle = subtitle;
        char quick_preview[48] = "";
        int quick_badge_w = 0;
        if (index == 1 && !disabled && !unavailable && fresh) {
            int est = ptc_ui_preview_added_remaining_minutes(model, 30);
            if (est >= 0) {
                snprintf(quick_preview, sizeof(quick_preview), ptc_ui_text(PTC_UI_T_QUICK_GRANT_PREVIEW_BADGE), est);
                quick_badge_w = measure_text(quick_preview, 11) + 16;
                if (quick_badge_w > 136) quick_badge_w = 136;
            }
        }
        if (index == 0 || index == 2 || index == 3) draw_today_small_action(pixels, stride, box, &action, focused, disabled);
        else
        draw_action_card(pixels, stride, box, &action, focused,
                         disabled ? PTC_UI_ACTION_DISABLED : PTC_UI_ACTION_AVAILABLE,
                         index == 1 ? (quick_badge_w > 0 ? quick_badge_w + 10 : 0) :
                         ((index == 0 || index >= 2) ? (is_en ? 76 : 82) : 0), (UiRect){0});
        if (index == 1 && quick_preview[0]) {
            UiRect qbadge = {box.x + box.width - quick_badge_w - 10, box.y + 10, quick_badge_w, 22};
            fill_round_rect(pixels, stride, qbadge, 6, UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, qbadge, 6, 1, UI_SUCCESS_BORDER);
            char fitted_q[48];
            fit_text(fitted_q, sizeof(fitted_q), quick_preview, 11, qbadge.width - 10);
            draw_text_center(pixels, stride, qbadge, fitted_q, 11, UI_SUCCESS);
        }
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
            uint32_t t_bd = badge_color == UI_SUCCESS ? UI_SUCCESS_BORDER :
                            (badge_color == UI_DANGER ? UI_DANGER_BORDER :
                             (badge_color == UI_WARNING ? UI_WARNING_BORDER : UI_BORDER));
            draw_rect_outline(pixels, stride, tbadge, 6, 1, t_bd);
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
                 index == 6 ? (!model->eye_care_policy.enabled ? ptc_ui_text(PTC_UI_T_DISABLED) :
                               (strcmp(model->eye_care_phase, "resting") == 0 ? ptc_ui_text(PTC_UI_T_EYE_CARE_BADGE_RESTING) :
                                ptc_ui_text(PTC_UI_T_EYE_CARE_BADGE_IDLE))) :
                 (model->dock_waived_today ? ptc_ui_text(PTC_UI_T_SKIPPED) :
                  (model->dock_restriction_active ? ptc_ui_text(PTC_UI_T_RESTRICTED) :
                   (model->dock_policy.force_docked ? ptc_ui_text(PTC_UI_T_DOCK_FORCE) :
                    (model->dock_policy.undocked_limit_enabled ? ptc_ui_text(PTC_UI_T_DOCK_BADGE_QUOTA) :
                     ptc_ui_text(PTC_UI_T_DISABLED))))));
            uint32_t color = (!fresh || eye_needs_refresh) ? UI_WARNING :
                ((index == 4 && model->bedtime_active && !model->bedtime_skipped) ||
                 (index == 6 && strcmp(model->eye_care_phase, "resting") == 0) ||
                 (index == 7 && model->dock_restriction_active && !model->dock_waived_today) ? UI_DANGER :
                 (strcmp(badge, ptc_ui_text(PTC_UI_T_ENABLED_2)) == 0 || strcmp(badge, ptc_ui_text(PTC_UI_T_SKIPPED)) == 0 ||
                  strcmp(badge, ptc_ui_text(PTC_UI_T_CLAIMED)) == 0 || strcmp(badge, "Enabled") == 0 ||
                  strcmp(badge, "Skipped") == 0 || strcmp(badge, "Claimed") == 0 ? UI_SUCCESS : UI_MUTED));
            int b_width = measure_text(badge, 12) + 16;
            if (b_width < 72) b_width = 72;
            UiRect tbadge = {box.x + box.width - b_width - 10, box.y + 10, b_width, 22};
            uint32_t b_bd = color == UI_SUCCESS ? UI_SUCCESS_BORDER :
                            (color == UI_DANGER ? UI_DANGER_BORDER :
                             (color == UI_WARNING ? UI_WARNING_BORDER : UI_BORDER));
            fill_round_rect(pixels, stride, tbadge, 6, color == UI_DANGER ? UI_DANGER_SOFT :
                            (color == UI_SUCCESS ? UI_SUCCESS_SOFT :
                             (color == UI_WARNING ? UI_WARNING_SOFT : UI_PAGE)));
            draw_rect_outline(pixels, stride, tbadge, 6, 1, b_bd);
            draw_text_center(pixels, stride, tbadge, badge, 12, color);
        }
    }
}

static void draw_grant_help(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 452};
    draw_plan_card(pixels, stride, panel, false);

    int sel = model ? model->selected_index : 0;
    if (sel < 0 || sel > 3) sel = 0;

    const char *tag = ptc_ui_text(PTC_UI_T_OFFLINE_GRANTS);
    const char *title = ptc_ui_text(PTC_UI_T_HOST_GENERATION);
    const char *status_text = ptc_ui_text(PTC_UI_T_NATIVE_SIGNATURE_8_BIT_CODE);
    uint32_t status_color = UI_ACCENT;
    const char *desc1 = "";
    const char *desc2 = "";
    const char *desc3 = "";
    const char *action_hint = ptc_ui_text(PTC_UI_T_GRANT_INSPECTOR_OPEN);

    switch (sel) {
    case 0: /* 主机生成 */
        tag = ptc_ui_text(PTC_UI_T_OFFLINE_GRANTS);
        title = ptc_ui_text(PTC_UI_T_GENERATE_CODE_ON_CONSOLE);
        status_text = ptc_ui_text(PTC_UI_T_NATIVE_SIGNATURE_8_BIT_CODE);
        status_color = UI_SUCCESS;
        desc1 = ptc_ui_text(PTC_UI_T_PARENTS_CAN_DIRECTLY_SELECT_THE_SHORTCUT_TIME);
        desc2 = ptc_ui_text(PTC_UI_T_THE_EXTRA_TIME_CODE_INCLUDES_ANTI_REPLAY);
        desc3 = ptc_ui_text(PTC_UI_T_PIN_WILL_BE_VERIFIED_AGAIN_BEFORE_GENERATION);
        break;
    case 1: /* 手机 / 电脑生成 */
        tag = ptc_ui_text(PTC_UI_T_PHONE_PC_PAIRING);
        title = ptc_ui_text(PTC_UI_T_MOBILE_PC_GENERATOR);
        status_text = ptc_ui_text(PTC_UI_T_AVAILABLE_OFFLINE_ACROSS_DEVICES);
        status_color = UI_ACCENT;
        desc1 = ptc_ui_text(PTC_UI_T_RECOMMENDED_SOLUTION_ONE_SCAN_THE_QR_CODE);
        desc2 = ptc_ui_text(PTC_UI_T_NO_NETWORK_REQUIRED_FOR_DAILY_GENERATION_NO);
        desc3 = ptc_ui_text(PTC_UI_T_THE_QR_CODE_CONTAINS_A_TIME_CODE);
        break;
    case 2: /* 生成设置 */
        tag = ptc_ui_text(PTC_UI_T_GRANT_SECRET_MANAGEMENT);
        title = ptc_ui_text(PTC_UI_T_GRANT_SETTINGS);
        status_text = model && model->demo_secret_enabled
            ? ptc_ui_text(PTC_UI_T_DEMO) : ptc_ui_text(PTC_UI_T_DEVICE_AND_KEY_CONFIGURED);
        status_color = model && model->demo_secret_enabled ? UI_WARNING : UI_SUCCESS;
        desc1 = ptc_ui_text(PTC_UI_T_VIEW_ENTER_OR_RANDOMLY_GENERATE_DEVICE_NAMES);
        desc2 = ptc_ui_text(PTC_UI_T_IT_IS_RECOMMENDED_TO_USE_RANDOM_GENERATION);
        desc3 = ptc_ui_text(PTC_UI_T_FILE_CONTAINS_A_TIME_CODED_KEY_AND);
        break;
    case 3: /* 使用记录 */
    default: {
        tag = ptc_ui_text(PTC_UI_T_SECURITY_AUDIT);
        title = ptc_ui_text(PTC_UI_T_EXTRA_TIME_CODE_USAGE_RECORD);
        bool available = model && model->redemption_history_available &&
            ptc_ui_status_is_fresh(model, ptc_ui_render_now());
        if (available) {
            status_text = ptc_ui_text(PTC_UI_T_LAST_100);
            status_color = UI_ACCENT;
        } else {
            status_text = ptc_ui_text(PTC_UI_T_TO_BE_CONFIRMED);
            status_color = UI_MUTED;
        }
        desc1 = ptc_ui_text(PTC_UI_T_ONLY_DISPLAYS_THE_LAST_100_SUCCESSFULLY_REDEEMED);
        desc2 = ptc_ui_text(PTC_UI_T_THE_EXTRA_TIME_CODE_INCLUDES_ANTI_REPLAY);
        desc3 = ptc_ui_text(PTC_UI_T_NOTE_THE_EXTRA_TIME_CODE_IS_ONLY);
        break;
    }
    }

    char fitted_title[128];
    fit_text(fitted_title, sizeof(fitted_title), title, 22, panel.width - 48);
    draw_text(pixels, stride, panel.x + 24, panel.y + 72, fitted_title, 22, UI_RGB(UI_BLENDED(text_primary)));

    char fitted_badge[96], fitted_tag[96];
    int tag_w = measure_text(tag, 14);
    int max_badge_w = panel.width - 48 - tag_w - 28;
    if (max_badge_w < 120) max_badge_w = 120;
    if (max_badge_w > panel.width - 140) max_badge_w = panel.width - 140;
    fit_text(fitted_badge, sizeof(fitted_badge), status_text, 12, max_badge_w);
    int badge_width = measure_text(fitted_badge, 12) + 16;
    UiRect badge = {panel.x + panel.width - 24 - badge_width, panel.y + 24, badge_width, 26};
    fit_text(fitted_tag, sizeof(fitted_tag), tag, 14, badge.x - panel.x - 36);
    fill_round_rect(pixels, stride, badge, 6,
        status_color == UI_SUCCESS ? UI_SUCCESS_SOFT :
        (status_color == UI_WARNING ? UI_WARNING_SOFT :
        (status_color == UI_ACCENT ? UI_ACCENT_SOFT : UI_PAGE)));
    draw_rect_outline(pixels, stride, badge, 6, 1,
        status_color == UI_SUCCESS ? UI_SUCCESS_BORDER :
        (status_color == UI_WARNING ? UI_WARNING_BORDER : status_color));
    draw_text_center(pixels, stride, badge, fitted_badge, 12, status_color);
    draw_text(pixels, stride, panel.x + 24, panel.y + 42, fitted_tag, 14, UI_ACCENT);

    UiRect div = {panel.x + 24, panel.y + 92, panel.width - 48, 1};
    fill_round_rect(pixels, stride, div, 0, UI_BORDER);

    const char *descs[3] = {desc1, desc2, desc3};
    int desc_y = panel.y + 122;
    for (int i = 0; i < 3; ++i) {
        const char *text = descs[i];
        if (!text || !*text) continue;
        /* Strip leading UTF-8 bullet ("• ") so draw_wrapped_text never orphans the bullet on its own line. */
        if ((unsigned char)text[0] == 0xE2 && (unsigned char)text[1] == 0x80 && (unsigned char)text[2] == 0xA2) {
            text += 3;
            while (*text == ' ') ++text;
        }
        fill_round_rect(pixels, stride, (UiRect){panel.x + 24, desc_y - 8, 5, 5}, 2, UI_ACCENT);
        desc_y = draw_wrapped_text(pixels, stride, panel.x + 36, desc_y, text, 14,
                                   panel.width - 60, 18, 3, UI_RGB(UI_BLENDED(text_secondary))) + 6;
    }

    UiRect hint_box = {panel.x + 24, panel.y + 300, panel.width - 48, 44};
    fill_round_rect(pixels, stride, hint_box, 10, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, hint_box, 10, 1, UI_ACCENT);
    draw_button_label(pixels, stride, hint_box, action_hint, 15, UI_ACCENT);

    draw_wrapped_text(pixels, stride, panel.x + 24, panel.y + panel.height - 76,
                      ptc_ui_text(PTC_UI_T_NOTE_THE_EXTRA_TIME_CODE_IS_ONLY), 12,
                      panel.width - 48, 16, 4, UI_MUTED);
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
              ptc_ui_text(PTC_UI_T_GITHUB_ADDRESS_HTTPS_GITHUB_COM_SELFUPPEN_NX), 15, accent);
}

static void draw_parent_header(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *LABELS[] = {
        ptc_ui_text(PTC_UI_T_TODAY_SCHEDULE),
        ptc_ui_text(PTC_UI_T_TIME_PLANS),
        ptc_ui_text(PTC_UI_T_OFFLINE_GRANTS),
        ptc_ui_text(PTC_UI_T_SECURITY_PREFS),
        ptc_ui_text(PTC_UI_T_SUPPORT_RECOVERY)
    };
    const char *title = ptc_ui_text(PTC_UI_T_PARENT_CONSOLE);
    const char *sub_raw =
        model->parent_page == PTC_UI_PARENT_SUPPORT ? ptc_ui_text(PTC_UI_T_COMPATIBILITY_STATUS_DIAGNOSTICS_AND_SAFE_RECOVERY) :
         (model->parent_page == PTC_UI_PARENT_PLAN ? ptc_ui_text(PTC_UI_T_CREDIT_RULES_AND_BEDTIME_PLAN) :
         (model->parent_page == PTC_UI_PARENT_TODAY ? ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_ADJUSTMENT_AND_SINGLE_MEASURES) :
         (model->parent_page == PTC_UI_PARENT_GRANT ? ptc_ui_text(PTC_UI_T_OFFLINE_GRANT_SUBTITLE) :
          ptc_ui_text(PTC_UI_T_LOCAL_RULES_AND_DEVICE_SECURITY_SETTINGS))));

    char subtitle_buf[256];
    const char *subtitle = sub_raw;

    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page != PTC_UI_PLAN_PAGE_ROOT) {
        if (model->plan_page == PTC_UI_PLAN_PAGE_WEEKLY) title = ptc_ui_text(PTC_UI_T_WEEKLY_PLAN);
        else if (model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) title = ptc_ui_text(PTC_UI_T_NATIONAL_HOLIDAYS);
        else if (model->plan_page == PTC_UI_PLAN_PAGE_BEDTIME) title = ptc_ui_text(PTC_UI_T_BEDTIME);
        else if (model->plan_page == PTC_UI_PLAN_PAGE_DOCK) title = ptc_ui_text(PTC_UI_T_DOCK_TITLE);
        else if (model->plan_page == PTC_UI_PLAN_PAGE_EYE_CARE) title = ptc_ui_text(PTC_UI_T_EYE_CARE);
    } else {
        int page_idx = (model->parent_page >= 0 && model->parent_page < PTC_UI_PARENT_PAGE_COUNT)
            ? (int)model->parent_page : 0;
        snprintf(subtitle_buf, sizeof(subtitle_buf), "%s / %s", LABELS[page_idx], sub_raw);
        subtitle = subtitle_buf;
    }

    if (model->parent_page == PTC_UI_PARENT_TODAY) {
        draw_header_limited(pixels, stride, title, subtitle, ptc_ui_header_language_rect().x - 124 - 16);
    } else {
        draw_header(pixels, stride, title, subtitle);
    }
    draw_time_status_bar(pixels, stride, model);
    draw_tabs(pixels, stride, model);
    if (model->parent_page == PTC_UI_PARENT_TODAY) {
        draw_language_switcher_badge(pixels, stride, to_uirect(ptc_ui_header_language_rect()),
                                     model->header_language_focused);
    }
}

static UiRect parent_action_badge(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                       UiRect card, int index, bool paint)
{
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
        if (paint && fresh && is_active_rule && !(index == model->selected_index && !model->parent_footer_focused)) {
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
            index == 4 ? ptc_ui_eye_care_plan_badge_label(model) :
            index == 5 ? (model->dock_restriction_active ? ptc_ui_text(PTC_UI_T_RESTRICTED) :
                model->dock_waived_today ? ptc_ui_text(PTC_UI_T_SKIPPED) :
                (model->dock_policy.force_docked || model->dock_policy.undocked_limit_enabled) ? ptc_ui_text(PTC_UI_T_ENABLED) : ptc_ui_text(PTC_UI_T_DISABLED_2)) :
            (model->autonomy_policy.daily_buffer_minutes == 0 ? ptc_ui_text(PTC_UI_T_DISABLED_2) :
             (model->daily_buffer_claimed ? ptc_ui_text(PTC_UI_T_RECEIVED_TODAY) :
              (model->daily_buffer_available ? ptc_ui_text(PTC_UI_T_AVAILABLE_TODAY) : ptc_ui_text(PTC_UI_T_ENABLED))));
        int p_width = measure_text(badge_label, 12) + 16;
        if (p_width < 76) p_width = 76;
        UiRect pbadge = {card.x + card.width - p_width - 12, card.y + 8, p_width, 22};
        if (!paint) return pbadge;
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
        uint32_t p_bd = badge_color == UI_DANGER ? UI_DANGER_BORDER :
                        (badge_color == UI_SUCCESS ? UI_SUCCESS_BORDER :
                         (badge_color == UI_WARNING ? UI_WARNING_BORDER : UI_BORDER));
        draw_rect_outline(pixels, stride, pbadge, 6, 1, p_bd);
        draw_text_center(pixels, stride, pbadge, badge_label, 12, badge_color);
    } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 4) {
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
        if (!paint) return badge;
        uint32_t st_bg = state_color == UI_SUCCESS ? UI_SUCCESS_SOFT :
                         (state_color == UI_WARNING ? UI_WARNING_SOFT :
                          (state_color == UI_DANGER ? UI_DANGER_SOFT :
                           (state_color == UI_ACCENT ? UI_ACCENT_SOFT : UI_PAGE)));
        uint32_t st_bd = state_color == UI_SUCCESS ? UI_SUCCESS_BORDER :
                         (state_color == UI_WARNING ? UI_WARNING_BORDER :
                          (state_color == UI_DANGER ? UI_DANGER_BORDER :
                           (state_color == UI_ACCENT ? UI_ACCENT : UI_BORDER)));
        fill_round_rect(pixels, stride, badge, 6, st_bg);
        draw_rect_outline(pixels, stride, badge, 6, 1, st_bd);
        draw_text_center(pixels, stride, badge, state_label, 12, state_color);
    } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 0) {
        const char *state_label = ptc_ui_text(PTC_UI_T_ENABLED);
        int pw = measure_text(state_label, 12) + 16;
        if (pw < 62) pw = 62;
        UiRect badge = {card.x + card.width - pw - 14, card.y + 8, pw, 24};
        if (!paint) return badge;
        fill_round_rect(pixels, stride, badge, 6, UI_SUCCESS_SOFT);
        draw_rect_outline(pixels, stride, badge, 6, 1, UI_SUCCESS_BORDER);
        draw_text_center(pixels, stride, badge, state_label, 12, UI_SUCCESS);
    } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 5) {
        const char *style_label = ptc_ui_theme_style_label(ptc_ui_theme_get_style());
        int sw = measure_text(style_label, 12) + 16;
        if (sw < 56) sw = 56;
        UiRect badge = {card.x + card.width - sw - 14, card.y + 8, sw, 24};
        if (!paint) return badge;
        fill_round_rect(pixels, stride, badge, 6, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, badge, 6, 1, UI_ACCENT);
        draw_text_center(pixels, stride, badge, style_label, 12, UI_ACCENT);
    } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 1) {
        bool enabled = model->reason_notifications_enabled;
        const char *state_label = enabled ? ptc_ui_text(PTC_UI_T_ENABLED) : ptc_ui_text(PTC_UI_T_DISABLED_2);
        uint32_t state_color = enabled ? UI_SUCCESS : UI_MUTED;
        int au_width = measure_text(state_label, 12) + 16;
        if (au_width < 62) au_width = 62;
        UiRect badge = {card.x + card.width - au_width - 14, card.y + 8, au_width, 24};
        if (!paint) return badge;
        uint32_t au_bg = enabled ? UI_SUCCESS_SOFT : UI_PAGE;
        uint32_t au_bd = enabled ? UI_SUCCESS_BORDER : UI_BORDER;
        fill_round_rect(pixels, stride, badge, 6, au_bg);
        draw_rect_outline(pixels, stride, badge, 6, 1, au_bd);
        draw_text_center(pixels, stride, badge, state_label, 12, state_color);
    } else if (model->parent_page == PTC_UI_PARENT_SUPPORT && index == 5 &&
               model->update_status == PTC_UPDATE_AVAILABLE && model->update_version[0]) {
        const char *state_label = ptc_ui_text(PTC_UI_T_UPDATE_NEW_VERSION_BADGE);
        uint32_t state_color = UI_ACCENT;
        int st_width = measure_text(state_label, 12) + 16;
        if (st_width < 72) st_width = 72;
        UiRect badge = {card.x + card.width - st_width - 14, card.y + 8, st_width, 24};
        if (!paint) return badge;
        fill_round_rect(pixels, stride, badge, 6, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, badge, 6, 1, UI_ACCENT);
        draw_text_center(pixels, stride, badge, state_label, 12, state_color);
        return badge;
    }
    return (UiRect){0};
}

static void draw_parent_action_card(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                     const UiAction *actions, int index)
{
    UiRect card = to_uirect(model->parent_page == PTC_UI_PARENT_SUPPORT
        ? ptc_ui_support_card_rect(index) : (model->parent_page == PTC_UI_PARENT_PLAN
            ? ptc_ui_plan_card_rect(index) : (model->parent_page == PTC_UI_PARENT_SETTINGS
                ? ptc_ui_settings_card_rect(index) : ptc_ui_parent_card_rect(index))));
    PtcUiActionState astate = PTC_UI_ACTION_AVAILABLE;
    if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
        if (!ptc_ui_safety_action_visible(model, index)) return;
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
    if (model->parent_page == PTC_UI_PARENT_SUPPORT && index == 5 &&
        model->update_status == PTC_UPDATE_AVAILABLE && model->update_version[0]) {
        static char update_subtitle[96];
        dynamic_action = *action;
        snprintf(update_subtitle, sizeof(update_subtitle), ptc_ui_text(PTC_UI_T_UPDATE_AVAILABLE_TARGET), model->update_version);
        dynamic_action.subtitle = update_subtitle;
        dynamic_action.accent = UI_ACCENT;
        action = &dynamic_action;
        astate = PTC_UI_ACTION_RECOMMENDED;
    }
    if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 4) {
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
    } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 5) {
        static char dock_detail[192];
        dynamic_action = *action;
        ptc_ui_format_dock_usage(model, ptc_ui_render_now(), dock_detail, sizeof(dock_detail));
        dynamic_action.subtitle = dock_detail;
        action = &dynamic_action;
    } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 6) {
        static char autonomy_detail[64];
        dynamic_action = *action;
        if (model->autonomy_policy.daily_buffer_minutes > 0u) {
            snprintf(autonomy_detail, sizeof(autonomy_detail), ptc_ui_text(PTC_UI_T_CURRENTLY_U_MIN_DAY),
                (unsigned int)model->autonomy_policy.daily_buffer_minutes);
        } else {
            snprintf(autonomy_detail, sizeof(autonomy_detail), "%s", ptc_ui_text(PTC_UI_T_CURRENTLY_OFF));
        }
        dynamic_action.subtitle = autonomy_detail;
        action = &dynamic_action;
    }
    if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 1) {
        dynamic_action = *action;
        dynamic_action.subtitle = model->reason_notifications_enabled
            ? ptc_ui_text(PTC_UI_T_REASON_NOTICE_EXTERNAL_ON)
            : ptc_ui_text(PTC_UI_T_IS_CURRENTLY_CLOSED);
        dynamic_action.accent = model->reason_notifications_enabled ? UI_SUCCESS : UI_MUTED;
        action = &dynamic_action;
    } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 2) {
        static char shortcut_detail[112];
        dynamic_action = *action;
        if (model->custom_shortcut_enabled && model->custom_shortcut_label[0]) {
            snprintf(shortcut_detail, sizeof(shortcut_detail), ptc_ui_text(PTC_UI_T_CURRENT_S),
                     model->custom_shortcut_label);
        } else {
            snprintf(shortcut_detail, sizeof(shortcut_detail), "%s", ptc_ui_text(PTC_UI_T_CURRENT_MINUS));
        }
        dynamic_action.subtitle = shortcut_detail;
        action = &dynamic_action;
    } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 5) {
        static char theme_subtitle[96];
        dynamic_action = *action;
        snprintf(theme_subtitle, sizeof(theme_subtitle), "%s / %s",
                 ptc_ui_theme_preference_label(g_theme.preference),
                 ptc_ui_theme_style_label(ptc_ui_theme_get_style()));
        dynamic_action.subtitle = theme_subtitle;
        action = &dynamic_action;
    }
    if (model->parent_page == PTC_UI_PARENT_PLAN &&
        !ptc_ui_status_is_fresh(model, ptc_ui_render_now())) {
        dynamic_action = *action;
        dynamic_action.subtitle = ptc_ui_text(PTC_UI_T_PRESS_Y_TO_REFRESH_TO_CONFIRM_THE);
        action = &dynamic_action;
    }
    UiRect status_badge = parent_action_badge(pixels, stride, model, card, index, false);
    draw_action_card(pixels, stride, card, action, index == model->selected_index && !model->parent_footer_focused, astate,
                     0, status_badge);
    parent_action_badge(pixels, stride, model, card, index, true);
}

static void draw_settings_grouped_card(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                       const UiAction *actions, int index)
{
    UiRect card = to_uirect(ptc_ui_settings_card_rect(index));
    bool selected = (index == model->selected_index && !model->parent_footer_focused);
    const UiAction *action = &actions[index];
    const char *title = ptc_ui_text_resolve(action->title);
    const char *subtitle = ptc_ui_text_resolve(action->subtitle);
    const char *badge_label = "";
    uint32_t badge_color = UI_MUTED;
    uint32_t badge_bg = UI_PAGE;
    uint32_t badge_bd = UI_BORDER;
    uint32_t icon_accent = action->accent;
    char dynamic_sub[128];
    char fitted[128];

    if (index == 0) {
        badge_label = ptc_ui_text(PTC_UI_T_ENABLED);
        badge_color = UI_SUCCESS;
        badge_bg = UI_SUCCESS_SOFT;
        badge_bd = UI_SUCCESS_BORDER;
    } else if (index == 1) {
        bool en = model->reason_notifications_enabled;
        subtitle = en ? ptc_ui_text(PTC_UI_T_REASON_NOTICE_EXTERNAL_ON)
                      : ptc_ui_text(PTC_UI_T_IS_CURRENTLY_CLOSED);
        icon_accent = en ? UI_SUCCESS : UI_MUTED;
        badge_label = en ? ptc_ui_text(PTC_UI_T_ENABLED) : ptc_ui_text(PTC_UI_T_DISABLED_2);
        badge_color = en ? UI_SUCCESS : UI_MUTED;
        badge_bg = en ? UI_SUCCESS_SOFT : UI_PAGE;
        badge_bd = en ? UI_SUCCESS_BORDER : UI_BORDER;
    } else if (index == 2) {
        bool custom = (model->custom_shortcut_enabled && model->custom_shortcut_label[0]);
        if (custom) {
            snprintf(dynamic_sub, sizeof(dynamic_sub), ptc_ui_text(PTC_UI_T_CURRENT_S),
                     model->custom_shortcut_label);
        } else {
            snprintf(dynamic_sub, sizeof(dynamic_sub), "%s", ptc_ui_text(PTC_UI_T_CURRENT_MINUS));
        }
        subtitle = dynamic_sub;
        badge_label = ptc_ui_text(custom ? PTC_UI_T_SETTINGS_BADGE_CUSTOM_COMBO
                                         : PTC_UI_T_SETTINGS_BADGE_DEFAULT_COMBO);
        badge_color = UI_ACCENT;
        badge_bg = UI_ACCENT_SOFT;
        badge_bd = UI_ACCENT;
    } else if (index == 3) {
        badge_label = ptc_ui_text(PTC_UI_T_UP_TO_200_ITEMS);
        badge_color = UI_MUTED;
        badge_bg = UI_PAGE;
        badge_bd = UI_BORDER;
    } else if (index == 4) {
        if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_OFF) {
            subtitle = ptc_ui_text(PTC_UI_T_IS_CURRENTLY_NOT_ENABLED);
            badge_label = ptc_ui_text(PTC_UI_T_NOT_ENABLED);
            badge_color = UI_MUTED;
            badge_bg = UI_PAGE;
            badge_bd = UI_BORDER;
        } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED) {
            subtitle = ptc_ui_text(PTC_UI_T_IS_CURRENTLY_ON_HOLD_DOWN_X_AND);
            badge_label = ptc_ui_text(PTC_UI_T_ENABLED);
            badge_color = UI_SUCCESS;
            badge_bg = UI_SUCCESS_SOFT;
            badge_bd = UI_SUCCESS_BORDER;
            icon_accent = UI_SUCCESS;
        } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_ANOMALY) {
            subtitle = ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED_PLEASE_VIEW_DETAILS);
            badge_label = ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED_2);
            badge_color = UI_WARNING_TEXT;
            badge_bg = UI_WARNING_SOFT;
            badge_bd = UI_WARNING_BORDER;
            icon_accent = UI_WARNING;
        } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
            subtitle = ptc_ui_text(PTC_UI_T_EXTERNAL_CONFIGURATION_ENTRY_AVAILABLE);
            badge_label = ptc_ui_text(PTC_UI_T_EXTERNAL_CONFIGURATION);
            badge_color = UI_ACCENT;
            badge_bg = UI_ACCENT_SOFT;
            badge_bd = UI_ACCENT;
            icon_accent = UI_ACCENT;
        } else {
            subtitle = ptc_ui_text(PTC_UI_T_STATUS_IS_UNKNOWN_PLEASE_CHECK_AGAIN);
            badge_label = ptc_ui_text(PTC_UI_T_STATUS_UNKNOWN);
            badge_color = UI_DANGER;
            badge_bg = UI_DANGER_SOFT;
            badge_bd = UI_DANGER_BORDER;
            icon_accent = UI_DANGER;
        }
    } else if (index == 5) {
        snprintf(dynamic_sub, sizeof(dynamic_sub), "%s / %s",
                 ptc_ui_theme_preference_label(g_theme.preference),
                 ptc_ui_theme_style_label(ptc_ui_theme_get_style()));
        subtitle = dynamic_sub;
        badge_label = ptc_ui_theme_style_label(ptc_ui_theme_get_style());
        badge_color = UI_ACCENT;
        badge_bg = UI_ACCENT_SOFT;
        badge_bd = UI_ACCENT;
    }

    if (!selected) draw_card_shadow(pixels, stride, card, 14);
    fill_round_rect(pixels, stride, card, 14, selected ? UI_ACCENT_SOFT : UI_SURFACE);
    if (selected) draw_focus_ring(pixels, stride, card, 14);
    else draw_rect_outline(pixels, stride, card, 14, 1, UI_BORDER);

    /* Top row: 38x38 icon badge on left, status pill on right */
    UiRect icon_box = {card.x + 14, card.y + 14, 38, 38};
    uint32_t icon_bg = selected ? UI_ACCENT :
        (icon_accent == UI_SUCCESS ? UI_SUCCESS_SOFT :
        (icon_accent == UI_DANGER ? UI_DANGER_SOFT :
        (icon_accent == UI_WARNING ? UI_WARNING_SOFT :
        (icon_accent == UI_MUTED ? UI_RAISED : UI_ACCENT_SOFT))));
    fill_round_rect(pixels, stride, icon_box, 10, icon_bg);
    draw_card_action_icon(pixels, stride, icon_box.x + 19, icon_box.y + 19,
                          action->icon, selected ? UI_ON_ACCENT : icon_accent);

    if (badge_label[0]) {
        int max_bw = card.width - 74;
        fit_text(fitted, sizeof(fitted), badge_label, 12, max_bw - 14);
        int bw = measure_text(fitted, 12) + 16;
        if (bw < 52) bw = 52;
        if (bw > max_bw) bw = max_bw;
        UiRect pill = {card.x + card.width - 14 - bw, card.y + 20, bw, 24};
        fill_round_rect(pixels, stride, pill, 12, badge_bg);
        draw_rect_outline(pixels, stride, pill, 12, 1, badge_bd);
        draw_text_center(pixels, stride, pill, fitted, 12, badge_color);
    }

    /* Title + 2-line wrapped subtitle */
    int title_size = 18;
    while (title_size > 14 && measure_text(title, title_size) > card.width - 28) title_size--;
    fit_text(fitted, sizeof(fitted), title, title_size, card.width - 28);
    draw_text_bold(pixels, stride, card.x + 14, card.y + 84, fitted, title_size,
                   selected ? UI_ACCENT : UI_INK);
    if (subtitle && subtitle[0]) {
        draw_wrapped_text(pixels, stride, card.x + 14, card.y + 108, subtitle,
                          13, card.width - 28, 17, 2, UI_MUTED);
    }
}

static void draw_parent_settings_quick_strip(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect strip = to_uirect(ptc_ui_settings_language_rect());
    bool selected = (model->selected_index == 6 && !model->parent_footer_focused);
    char lang_buf[64], ver_buf[80], fitted[96];
    if (!selected) draw_card_shadow(pixels, stride, strip, 14);
    fill_round_rect(pixels, stride, strip, 14, selected ? UI_ACCENT_SOFT : UI_SURFACE);
    if (selected) draw_focus_ring(pixels, stride, strip, 14);
    else draw_rect_outline(pixels, stride, strip, 14, 1, UI_BORDER);

    UiRect icon_box = {strip.x + 12, strip.y + 12, 36, 36};
    fill_round_rect(pixels, stride, icon_box, 10, selected ? UI_ACCENT : UI_ACCENT_SOFT);
    draw_globe_icon(pixels, stride, icon_box.x + 18, icon_box.y + 18, 9,
                    selected ? UI_ON_ACCENT : UI_ACCENT);

    const char *title = ptc_ui_text(PTC_UI_T_UI_LANGUAGE);
    draw_text_bold(pixels, stride, strip.x + 58, strip.y + 36, title, 16,
                   selected ? UI_ACCENT : UI_INK);
    int cur_x = strip.x + 58 + measure_text(title, 16) + 14;

    const char *hint = ptc_ui_text(PTC_UI_T_SETTINGS_LANG_STRIP_HINT);
    int hint_w = measure_text(hint, 13);
    int hint_x = strip.x + strip.width - 16 - hint_w;
    draw_text_bold(pixels, stride, hint_x, strip.y + 35, hint, 13, UI_ACCENT);

    /* Language status pill */
    const char *lang_label = ptc_ui_localize(ptc_ui_language_preference_label(model->language_preference),
                                             lang_buf, sizeof(lang_buf));
    fit_text(fitted, sizeof(fitted), lang_label, 12, 160);
    int lw = measure_text(fitted, 12) + 20;
    if (cur_x + lw + 8 < hint_x) {
        UiRect lpill = {cur_x, strip.y + 17, lw, 26};
        fill_round_rect(pixels, stride, lpill, 13, selected ? UI_SURFACE : UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, lpill, 13, 1, UI_ACCENT);
        draw_text_center(pixels, stride, lpill, fitted, 12, UI_ACCENT);
        cur_x += lw + 10;
    }

    /* Version / update pill */
    bool upd = (model->update_status == PTC_UPDATE_AVAILABLE && model->update_version[0]);
    const char *ver_str = model->software_version[0] ? model->software_version : PLAYWISE_VERSION;
    if (upd) {
        snprintf(ver_buf, sizeof(ver_buf), "v%.16s -> v%.16s", ver_str, model->update_version);
    } else {
        snprintf(ver_buf, sizeof(ver_buf), "v%.20s", ver_str);
    }
    int avail_w = hint_x - cur_x - 14;
    if (avail_w > 64) {
        fit_text(fitted, sizeof(fitted), ver_buf, 12, avail_w - 18);
        int vw = measure_text(fitted, 12) + 20;
        if (vw > avail_w) vw = avail_w;
        UiRect vpill = {cur_x, strip.y + 17, vw, 26};
        fill_round_rect(pixels, stride, vpill, 13, upd ? UI_ACCENT_SOFT : UI_PAGE);
        draw_rect_outline(pixels, stride, vpill, 13, 1, upd ? UI_ACCENT : UI_BORDER);
        draw_text_center(pixels, stride, vpill, fitted, 12, upd ? UI_ACCENT : UI_MUTED);
    }
}

static void draw_parent_action_grid(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const UiAction *actions;
    int action_count;
    int index;
    actions = actions_for_page(model->parent_page, &action_count);
    if (model->parent_page == PTC_UI_PARENT_SETTINGS) {
        /* Scheme A: Two grouped sections (Security 3 + Preferences 3) + Quick Status Strip. */
        int sec_on = 2 + ((model->album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED ||
                           model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) ? 1 : 0);
        char count_text[64];
        snprintf(count_text, sizeof(count_text), ptc_ui_text(PTC_UI_T_SETTINGS_GROUP_SECURITY_COUNT), sec_on);
        draw_text_bold(pixels, stride, 54, 193, ptc_ui_text(PTC_UI_T_SETTINGS_GROUP_SECURITY), 14, UI_MUTED);
        draw_text_bold(pixels, stride, 822 - measure_text(count_text, 13), 193, count_text, 13, UI_SUCCESS);

        draw_text_bold(pixels, stride, 54, 389, ptc_ui_text(PTC_UI_T_SETTINGS_GROUP_PREFS), 14, UI_MUTED);
        const char *prefs_hint = ptc_ui_text(PTC_UI_T_SETTINGS_GROUP_PREFS_HINT);
        draw_text(pixels, stride, 822 - measure_text(prefs_hint, 12), 389, prefs_hint, 12, UI_MUTED);

        for (index = 0; index < action_count; ++index) {
            draw_settings_grouped_card(pixels, stride, model, actions, index);
        }
        draw_parent_settings_quick_strip(pixels, stride, model);
        return;
    }
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
        draw_rect_outline(pixels, stride, pbadge, 6, 1, UI_WARNING_BORDER);
        draw_text_center(pixels, stride, pbadge, p_text, 12, UI_WARNING_TEXT);
        draw_text(pixels, stride, 440 + pw + 8, 195, ptc_ui_text(PTC_UI_T_OPERATES_INDEPENDENTLY), 13, UI_MUTED);

    }
    if (model->parent_page == PTC_UI_PARENT_GRANT) {
        UiRect grant_banner = {54, 484, 750, 144};
        fill_round_rect(pixels, stride, grant_banner, 16, UI_SURFACE);
        draw_rect_outline(pixels, stride, grant_banner, 16, 1, UI_BORDER);
        fill_round_rect(pixels, stride, (UiRect){grant_banner.x + 16, grant_banner.y + 18, 4, grant_banner.height - 36}, 2, UI_ACCENT);
        draw_text(pixels, stride, grant_banner.x + 32, grant_banner.y + 36,
            ptc_ui_text(PTC_UI_T_USAGE_HISTORY), 18, UI_INK);
        char summary[160];
        bool available = model->redemption_history_available &&
            ptc_ui_status_is_fresh(model, ptc_ui_render_now());
        if (available) {
            unsigned count = 0, added = 0;
            for (int row = 0; row < model->redemption_history_count; ++row) {
                const PtcRedemptionHistoryRecord *record = &model->redemption_history[row];
                if (record->day_index != model->day_index) continue;
                ++count;
                added += record->effective_add_minutes;
            }
            snprintf(summary, sizeof(summary), ptc_ui_text(PTC_UI_T_GRANT_TODAY_REDEEMED), count, added);
        } else {
            snprintf(summary, sizeof(summary), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        }
        draw_wrapped_text(pixels, stride, grant_banner.x + 32, grant_banner.y + 76, summary,
            18, grant_banner.width - 64, 23, 2, available ? UI_INK : UI_MUTED);
        draw_wrapped_text(pixels, stride, grant_banner.x + 32, grant_banner.y + 119,
            ptc_ui_text(PTC_UI_T_ONLY_DISPLAYS_THE_LAST_100_SUCCESSFULLY_REDEEMED),
            13, grant_banner.width - 64, 16, 1, UI_MUTED);
    }
    for (index = 0; index < action_count; ++index) {
        draw_parent_action_card(pixels, stride, model, actions, index);
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

static void draw_settings_dynamic_preview(uint32_t *pixels, uint32_t stride,
                                          const PtcUiModel *model, UiRect box, int sel)
{
    char buf[128], fitted[128];
    fill_round_rect(pixels, stride, box, 12, UI_PAGE);
    draw_rect_outline(pixels, stride, box, 12, 1, UI_BORDER);

    if (sel == 0) {
        /* PIN dots + 5-attempt cooldown guarantee */
        int dot_start_x = box.x + 22;
        int dot_y = box.y + 34;
        for (int i = 0; i < 6; ++i) {
            UiRect dot = {dot_start_x + i * 26, dot_y - 7, 14, 14};
            fill_round_rect(pixels, stride, dot, 7, UI_ACCENT);
        }
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_PROTECTION_ENABLED), 12, 120);
        int pw = measure_text(fitted, 12) + 16;
        UiRect pill = {box.x + box.width - 14 - pw, dot_y - 12, pw, 24};
        fill_round_rect(pixels, stride, pill, 12, UI_SUCCESS_SOFT);
        draw_rect_outline(pixels, stride, pill, 12, 1, UI_SUCCESS_BORDER);
        draw_text_center(pixels, stride, pill, fitted, 12, UI_SUCCESS);
        draw_wrapped_text(pixels, stride, box.x + 18, box.y + 74,
                          ptc_ui_text(PTC_UI_T_SETTINGS_PREVIEW_PIN_NOTE), 13,
                          box.width - 36, 18, 2, UI_SUCCESS);
    } else if (sel == 1) {
        /* Notifications: Dual-channel live readiness */
        draw_text_bold(pixels, stride, box.x + 14, box.y + 26,
                       ptc_ui_text(PTC_UI_T_SETTINGS_PREVIEW_NOTICE_TITLE), 13, UI_INK);
        bool ext_on = (model->reason_external_notifications == 1);
        const char *ext_state = ptc_ui_text(
            model->reason_external_notifications == 1 ? PTC_UI_T_NOTICE_STATE_READY :
            model->reason_external_notifications == 0 ? PTC_UI_T_NOTICE_STATE_OFF : PTC_UI_T_NOTICE_STATE_UNKNOWN);
        const char *det_state = ptc_ui_text(
            !model->reason_overlay_installed ? PTC_UI_T_NOTICE_STATE_MISSING :
            model->reason_detail_ready ? PTC_UI_T_NOTICE_STATE_READY : PTC_UI_T_NOTICE_STATE_NEED_HIDE);
        int cw = (box.width - 36) / 2;
        UiRect c0 = {box.x + 14, box.y + 42, cw, 60};
        UiRect c1 = {box.x + 22 + cw, box.y + 42, cw, 60};
        fill_round_rect(pixels, stride, c0, 8, UI_SURFACE);
        draw_rect_outline(pixels, stride, c0, 8, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NOTICE_CHANNEL_BRIEF), 11, c0.width - 14);
        draw_text(pixels, stride, c0.x + 8, c0.y + 21, fitted, 11, UI_MUTED);
        fit_text(fitted, sizeof(fitted), ext_state, 13, c0.width - 14);
        draw_text_bold(pixels, stride, c0.x + 8, c0.y + 45, fitted, 13, ext_on ? UI_SUCCESS : UI_WARNING_TEXT);

        fill_round_rect(pixels, stride, c1, 8, UI_SURFACE);
        draw_rect_outline(pixels, stride, c1, 8, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NOTICE_CHANNEL_DETAIL), 11, c1.width - 14);
        draw_text(pixels, stride, c1.x + 8, c1.y + 21, fitted, 11, UI_MUTED);
        fit_text(fitted, sizeof(fitted), det_state, 13, c1.width - 14);
        draw_text_bold(pixels, stride, c1.x + 8, c1.y + 45, fitted, 13,
                       model->reason_detail_ready ? UI_SUCCESS : (!model->reason_overlay_installed ? UI_DANGER : UI_WARNING_TEXT));
    } else if (sel == 2) {
        /* Shortcut keycap preview */
        const char *combo = (model->custom_shortcut_enabled && model->custom_shortcut_label[0])
            ? model->custom_shortcut_label : "-";
        fit_text(fitted, sizeof(fitted), combo, 16, box.width - 56);
        int kw = measure_text(fitted, 16) + 28;
        if (kw < 56) kw = 56;
        UiRect keycap = {box.x + 16, box.y + 20, kw, 36};
        fill_round_rect(pixels, stride, keycap, 8, UI_SURFACE);
        draw_rect_outline(pixels, stride, keycap, 8, 2, UI_ACCENT);
        draw_text_center(pixels, stride, keycap, fitted, 16, UI_ACCENT);
        draw_wrapped_text(pixels, stride, box.x + 16, box.y + 78,
                          ptc_ui_text(PTC_UI_T_SETTINGS_PREVIEW_SHORTCUT_NOTE), 13,
                          box.width - 32, 18, 2, UI_MUTED);
    } else if (sel == 3) {
        /* Recent 2 family activity records preview */
        draw_text_bold(pixels, stride, box.x + 14, box.y + 24,
                       ptc_ui_text(PTC_UI_T_SETTINGS_PREVIEW_ACTIVITY_TITLE), 12, UI_MUTED);
        if (model->activity_history_available && model->activity_history_count > 0) {
            int shown = model->activity_history_count < 2 ? model->activity_history_count : 2;
            for (int r = 0; r < shown; ++r) {
                const PtcActivityHistoryRecord *rec = &model->activity_history[r];
                int ry = box.y + 34 + r * 38;
                UiRect row = {box.x + 12, ry, box.width - 24, 32};
                char time_str[32];
                fill_round_rect(pixels, stride, row, 8, UI_SURFACE);
                draw_rect_outline(pixels, stride, row, 8, 1, UI_BORDER);
                format_event_time(rec->occurred_at, false, time_str, sizeof(time_str));
                int tw = measure_text(time_str, 11);
                draw_text(pixels, stride, row.x + row.width - 10 - tw, row.y + 21, time_str, 11, UI_MUTED);
                fit_text(fitted, sizeof(fitted), activity_label(rec->action), 12, row.width - tw - 26);
                draw_text(pixels, stride, row.x + 10, row.y + 21, fitted, 12, UI_INK);
            }
        } else {
            draw_wrapped_text(pixels, stride, box.x + 14, box.y + 56,
                              ptc_ui_text(PTC_UI_T_THERE_IS_NO_RECORD_OF_FAMILY_ACTIVITIES), 13,
                              box.width - 28, 18, 2, UI_MUTED);
        }
    } else if (sel == 4) {
        /* Compact dual-path album bypass diagram */
        UiRect a1 = {box.x + 14, box.y + 18, 92, 32};
        UiRect a2 = {box.x + 146, box.y + 18, box.width - 160, 32};
        fill_round_rect(pixels, stride, a1, 8, UI_SURFACE);
        draw_rect_outline(pixels, stride, a1, 8, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ALBUM_DIAG_ALBUM), 12, a1.width - 10);
        draw_text_center(pixels, stride, a1, fitted, 12, UI_INK);
        draw_transition_arrow(pixels, stride, box.x + 126, a1.y + 16, UI_MUTED);
        fill_round_rect(pixels, stride, a2, 8, UI_RAISED);
        draw_rect_outline(pixels, stride, a2, 8, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ALBUM_DIAG_NORMAL), 12, a2.width - 10);
        draw_text_center(pixels, stride, a2, fitted, 12, UI_MUTED);

        UiRect b1 = {box.x + 14, box.y + 64, 92, 34};
        UiRect b2 = {box.x + 114, box.y + 64, 92, 34};
        UiRect b3 = {box.x + 240, box.y + 64, box.width - 254, 34};
        fill_round_rect(pixels, stride, b1, 8, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, b1, 8, 1, UI_ACCENT);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ALBUM_DIAG_CTRL), 11, b1.width - 8);
        draw_text_center(pixels, stride, b1, fitted, 11, UI_ACCENT);
        fill_round_rect(pixels, stride, b2, 8, UI_ACCENT);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ALBUM_DIAG_HOLD_XA), 11, b2.width - 8);
        draw_text_center(pixels, stride, b2, fitted, 11, UI_ON_ACCENT);
        draw_transition_arrow(pixels, stride, box.x + 222, b1.y + 17, UI_SUCCESS);
        fill_round_rect(pixels, stride, b3, 8, UI_SUCCESS_SOFT);
        draw_rect_outline(pixels, stride, b3, 8, 1, UI_SUCCESS_BORDER);
        draw_text_center(pixels, stride, b3, "hbmenu", 12, UI_SUCCESS);
    } else if (sel == 5) {
        /* 4 live theme style palette swatches */
        PtcUiThemeStyle active_style = ptc_ui_theme_get_style();
        draw_text_bold(pixels, stride, box.x + 14, box.y + 24,
                       ptc_ui_text(PTC_UI_T_SETTINGS_PREVIEW_THEME_TITLE), 12, UI_MUTED);
        snprintf(buf, sizeof(buf), "%s", ptc_ui_theme_preference_label(g_theme.preference));
        draw_text_bold(pixels, stride, box.x + box.width - 14 - measure_text(buf, 12),
                       box.y + 24, buf, 12, UI_ACCENT);
        int gap = 8;
        int sw = (box.width - 28 - gap * 3) / 4;
        for (int s = 0; s < PTC_UI_THEME_STYLE_COUNT; ++s) {
            const PtcUiPalette *spal = ptc_ui_theme_style_palette(g_theme.resolved, (PtcUiThemeStyle)s);
            UiRect tile = {box.x + 14 + s * (sw + gap), box.y + 36, sw, 68};
            bool is_on = ((PtcUiThemeStyle)s == active_style);
            fill_round_rect(pixels, stride, tile, 8, UI_SURFACE);
            draw_rect_outline(pixels, stride, tile, 8, is_on ? 2 : 1, is_on ? UI_ACCENT : UI_BORDER);
            UiRect swatch = {tile.x + 8, tile.y + 8, tile.width - 16, 26};
            fill_round_rect(pixels, stride, swatch, 5, UI_RGB(spal->accent));
            fit_text(fitted, sizeof(fitted), ptc_ui_theme_style_label((PtcUiThemeStyle)s), 11, tile.width - 10);
            draw_text_center(pixels, stride, (UiRect){tile.x, tile.y + 38, tile.width, 24},
                             fitted, 11, is_on ? UI_ACCENT : UI_INK);
        }
    } else if (sel == 6) {
        /* 4 UI language options overview */
        static const char *CODES[4] = {"AUTO", "ZH-CN", "ZH-TW", "EN"};
        const char *SHORT_NAMES[4] = {
            ptc_ui_text(PTC_UI_T_SYSTEM),
            ptc_ui_text_in_language(PTC_UI_T_SIMPLIFIED_CHINESE, PTC_UI_LANGUAGE_SIMPLIFIED),
            ptc_ui_text_in_language(PTC_UI_T_TRADITIONAL_CHINESE, PTC_UI_LANGUAGE_TRADITIONAL),
            "English"
        };
        draw_text_bold(pixels, stride, box.x + 14, box.y + 24,
                       ptc_ui_text(PTC_UI_T_SETTINGS_PREVIEW_LANG_TITLE), 12, UI_MUTED);
        const char *shortcut = "ZL / ZR";
        draw_text_bold(pixels, stride, box.x + box.width - 14 - measure_text(shortcut, 12),
                       box.y + 24, shortcut, 12, UI_ACCENT);
        int gap = 8;
        int sw = (box.width - 28 - gap * 3) / 4;
        for (int l = 0; l < 4; ++l) {
            UiRect tile = {box.x + 14 + l * (sw + gap), box.y + 36, sw, 68};
            bool is_on = ((int)model->language_preference == l);
            fill_round_rect(pixels, stride, tile, 8, is_on ? UI_ACCENT_SOFT : UI_SURFACE);
            draw_rect_outline(pixels, stride, tile, 8, is_on ? 2 : 1, is_on ? UI_ACCENT : UI_BORDER);
            UiRect chip = {tile.x + 6, tile.y + 10, tile.width - 12, 22};
            fill_round_rect(pixels, stride, chip, 6, is_on ? UI_ACCENT : UI_PAGE);
            draw_text_center(pixels, stride, chip, CODES[l], 10, is_on ? UI_ON_ACCENT : UI_MUTED);
            fit_text(fitted, sizeof(fitted), SHORT_NAMES[l], 11, tile.width - 10);
            draw_text_center(pixels, stride, (UiRect){tile.x, tile.y + 38, tile.width, 24},
                             fitted, 11, is_on ? UI_ACCENT : UI_INK);
        }
    }
}

static void draw_parent_settings_help(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect help = {840, 174, 386, 454};
    draw_plan_card(pixels, stride, help, false);

    int sel = model->selected_index;
    const char *tag = ptc_ui_text(PTC_UI_T_SYSTEM_PREFERENCES);
    const char *title = ptc_ui_text(PTC_UI_T_APPEARANCE_THEME);
    const char *status_text = ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM);
    uint32_t status_color = UI_ACCENT;
    uint32_t status_bg = UI_ACCENT_SOFT;
    uint32_t status_bd = UI_ACCENT;
    UiActionIcon icon = UI_ACTION_ICON_THEME;
    const char *desc1 = "";
    const char *desc2 = "";
    const char *action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_MODIFY_SETTINGS);
    char lang_status[64];

    switch (sel) {
    case 0:
        tag = ptc_ui_text(PTC_UI_T_SAFETY_MANAGEMENT);
        title = ptc_ui_text(PTC_UI_T_PARENT_PIN);
        status_text = ptc_ui_text(PTC_UI_T_PROTECTION_ENABLED);
        status_color = UI_SUCCESS;
        status_bg = UI_SUCCESS_SOFT;
        status_bd = UI_SUCCESS_BORDER;
        icon = UI_ACTION_ICON_KEY;
        desc1 = ptc_ui_text(PTC_UI_T_USED_TO_PROTECT_PARENTAL_ZONE_SETTINGS_HIGH);
        desc2 = ptc_ui_text(PTC_UI_T_SUPPORTS_1_64_DIGIT_PIN);
        action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_CHANGE_THE_MANAGEMENT_PASSWORD);
        break;
    case 1:
        tag = ptc_ui_text(PTC_UI_T_SYSTEM_PREFERENCES);
        title = ptc_ui_text(PTC_UI_T_ALERT_PREFERENCES);
        status_text = model->reason_notifications_enabled ? ptc_ui_text(PTC_UI_T_ENABLED) : ptc_ui_text(PTC_UI_T_DISABLED_2);
        status_color = model->reason_notifications_enabled ? UI_SUCCESS : UI_MUTED;
        status_bg = model->reason_notifications_enabled ? UI_SUCCESS_SOFT : UI_PAGE;
        status_bd = model->reason_notifications_enabled ? UI_SUCCESS_BORDER : UI_BORDER;
        icon = UI_ACTION_ICON_NOTIFICATION;
        desc1 = ptc_ui_text(PTC_UI_T_NOTICE_SUBTITLE_B);
        desc2 = ptc_ui_text(PTC_UI_T_REASON_NOTICE_EXPIRY);
        action_hint = ptc_ui_text(PTC_UI_T_REASON_NOTICE_TOGGLE);
        break;
    case 2:
        tag = ptc_ui_text(PTC_UI_T_SYSTEM_CONTROL);
        title = ptc_ui_text(PTC_UI_T_QUICK_ENTRANCE_TO_PARENT_AREA);
        status_text = (model->custom_shortcut_enabled && model->custom_shortcut_label[0])
            ? model->custom_shortcut_label : ptc_ui_text(PTC_UI_T_DEFAULT_SHORTCUT_LABEL);
        status_color = UI_ACCENT;
        status_bg = UI_ACCENT_SOFT;
        status_bd = UI_ACCENT;
        icon = UI_ACTION_ICON_CONTROLLER;
        desc1 = ptc_ui_text(PTC_UI_T_LONG_PRESS_THIS_KEY_ON_ANY_INTERFACE);
        desc2 = ptc_ui_text(PTC_UI_T_SUPPORTS_CUSTOM_CONFIGURATION_OF_MINUS_CAPTURE_OR);
        action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_CHANGE_SHORTCUT_KEY_BINDINGS);
        break;
    case 3:
        tag = ptc_ui_text(PTC_UI_T_SECURITY_AUDIT);
        title = ptc_ui_text(PTC_UI_T_FAMILY_ACTIVITY_RECORD);
        status_text = ptc_ui_text(PTC_UI_T_UP_TO_200_ITEMS);
        status_color = UI_MUTED;
        status_bg = UI_PAGE;
        status_bd = UI_BORDER;
        icon = UI_ACTION_ICON_ACTIVITY;
        desc1 = ptc_ui_text(PTC_UI_T_RECORDS_CREDIT_ADJUSTMENT_OFFLINE_GRANT_BEDTIME_SKIP);
        desc2 = ptc_ui_text(PTC_UI_T_ONLY_SAVE_LOCAL_SECURITY_AUDIT_LOGS_AND);
        action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_SEE_FULL_ACTIVITY_LOG);
        break;
    case 4:
        tag = ptc_ui_text(PTC_UI_T_ADVANCED_SECURITY);
        title = ptc_ui_text(PTC_UI_T_HOMEBREW_ACCESS);
        icon = UI_ACTION_ICON_HOMEBREW;
        if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_OFF) {
            status_text = ptc_ui_text(PTC_UI_T_IS_CURRENTLY_NOT_ENABLED);
            status_color = UI_MUTED;
            status_bg = UI_PAGE;
            status_bd = UI_BORDER;
        } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED) {
            status_text = ptc_ui_text(PTC_UI_T_IS_CURRENTLY_ON);
            status_color = UI_SUCCESS;
            status_bg = UI_SUCCESS_SOFT;
            status_bd = UI_SUCCESS_BORDER;
        } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_ANOMALY) {
            status_text = ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED_2);
            status_color = UI_WARNING_TEXT;
            status_bg = UI_WARNING_SOFT;
            status_bd = UI_WARNING_BORDER;
        } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
            status_text = ptc_ui_text(PTC_UI_T_EXTERNAL_CONFIGURATION);
            status_color = UI_ACCENT;
            status_bg = UI_ACCENT_SOFT;
            status_bd = UI_ACCENT;
        } else {
            status_text = ptc_ui_text(PTC_UI_T_STATUS_UNKNOWN);
            status_color = UI_DANGER;
            status_bg = UI_DANGER_SOFT;
            status_bd = UI_DANGER_BORDER;
        }
        desc1 = ptc_ui_text(PTC_UI_T_REDIRECT_PHOTO_ALBUM_ENTRY_TO_HOMEBREW_MENU);
        desc2 = ptc_ui_text(PTC_UI_T_ONCE_ENABLED_HOLD_X_THEN_PRESS_A);
        action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_VIEW_DETAILS_AND_CONFIGURATION);
        break;
    case 6:
        tag = ptc_ui_text(PTC_UI_T_SYSTEM_PREFERENCES);
        title = ptc_ui_text(PTC_UI_T_UI_LANGUAGE);
        status_text = ptc_ui_localize(ptc_ui_language_preference_label(model->language_preference),
                                      lang_status, sizeof(lang_status));
        status_color = UI_ACCENT;
        status_bg = UI_ACCENT_SOFT;
        status_bd = UI_ACCENT;
        icon = UI_ACTION_ICON_LANGUAGE;
        desc1 = ptc_ui_text(PTC_UI_T_SETTINGS_LANG_DESC1);
        desc2 = ptc_ui_text(PTC_UI_T_SETTINGS_LANG_DESC2);
        action_hint = ptc_ui_text(PTC_UI_T_SETTINGS_LANG_CTA);
        break;
    case 5:
    default:
        tag = ptc_ui_text(PTC_UI_T_SYSTEM_PREFERENCES);
        title = ptc_ui_text(PTC_UI_T_APPEARANCE_THEME);
        status_text = ptc_ui_theme_preference_label(g_theme.preference);
        status_color = UI_ACCENT;
        status_bg = UI_ACCENT_SOFT;
        status_bd = UI_ACCENT;
        icon = UI_ACTION_ICON_THEME;
        desc1 = ptc_ui_text(PTC_UI_T_PROVIDES_THREE_THEME_MODES_LIGHT_DARK_AND);
        desc2 = ptc_ui_text(PTC_UI_T_DARK_MODE_OPTIMIZES_OLED_SCREEN_POWER_SAVING);
        action_hint = ptc_ui_text(PTC_UI_T_PRESS_A_TO_OPEN_THE_THEME_SWITCHING);
        break;
    }

    /* Header tag + status pill */
    char fitted_badge[96], fitted_tag[96], fitted_title[128];
    fit_text(fitted_badge, sizeof(fitted_badge), status_text, 12, (help.width - 44) / 2);
    int badge_width = measure_text(fitted_badge, 12) + 18;
    UiRect badge = {help.x + help.width - 20 - badge_width, help.y + 18, badge_width, 26};
    fit_text(fitted_tag, sizeof(fitted_tag), tag, 13, badge.x - help.x - 32);
    fill_round_rect(pixels, stride, badge, 13, status_bg);
    draw_rect_outline(pixels, stride, badge, 13, 1, status_bd);
    draw_text_center(pixels, stride, badge, fitted_badge, 12, status_color);
    draw_text_bold(pixels, stride, help.x + 20, help.y + 36, fitted_tag, 13, UI_ACCENT);

    /* Icon badge + Title */
    UiRect icon_box = {help.x + 20, help.y + 54, 40, 40};
    fill_round_rect(pixels, stride, icon_box, 10, UI_ACCENT_SOFT);
    draw_card_action_icon(pixels, stride, icon_box.x + 20, icon_box.y + 20, icon, UI_ACCENT);
    fit_text(fitted_title, sizeof(fitted_title), title, 21, help.width - 92);
    draw_text_bold(pixels, stride, help.x + 72, help.y + 82, fitted_title, 21, UI_RGB(UI_BLENDED(text_primary)));

    /* Divider */
    fill_round_rect(pixels, stride, (UiRect){help.x + 20, help.y + 106, help.width - 40, 1}, 0, UI_BORDER);

    /* Description */
    int desc_y = help.y + 128;
    if (desc1 && *desc1) {
        desc_y = draw_wrapped_text(pixels, stride, help.x + 20, desc_y, desc1, 14,
                                   help.width - 40, 19, 2, UI_RGB(UI_BLENDED(text_secondary))) + 6;
    }
    if (desc2 && *desc2) {
        draw_wrapped_text(pixels, stride, help.x + 20, desc_y, desc2, 14,
                          help.width - 40, 19, 2, UI_RGB(UI_BLENDED(text_secondary)));
    }

    /* Dynamic visual preview module */
    UiRect preview_box = {help.x + 20, help.y + 220, help.width - 40, 118};
    draw_settings_dynamic_preview(pixels, stride, model, preview_box, sel);

    /* Primary CTA pill */
    UiRect hint_box = {help.x + 20, help.y + 354, help.width - 40, 44};
    fill_round_rect(pixels, stride, hint_box, 10, UI_ACCENT);
    draw_button_label(pixels, stride, hint_box, action_hint, 15, UI_ON_ACCENT);

    /* Bottom quiet lock hint */
    draw_wrapped_text_centered(pixels, stride, help.x + help.width / 2, help.y + 422,
                               ptc_ui_text(PTC_UI_T_PRESS_B_WHILE_AWAY_TO_RETURN_TO), 12,
                               help.width - 36, 16, 2, UI_MUTED);
}

static void draw_prominent_footer_action(
    uint32_t *pixels, uint32_t stride, PtcUiRect rect,
    const char *label, bool focused, bool disabled)
{
    UiRect box = to_uirect(rect);
    if (!disabled && !focused) {
        draw_card_shadow(pixels, stride, box, 12);
    }
    fill_round_rect(pixels, stride, box, 12,
        disabled ? UI_RAISED : (focused ? UI_ACCENT_SOFT : UI_RGB(UI_BLENDED(surface_raised))));
    if (focused && !disabled) {
        draw_focus_ring(pixels, stride, box, 12);
    } else {
        draw_rect_outline(pixels, stride, box, 12, 1,
            disabled ? UI_BORDER : UI_RGB(UI_BLENDED(border_control)));
    }
    uint32_t text_color = disabled ? UI_DISABLED : (focused ? UI_ACCENT : UI_INK);
    draw_button_label(pixels, stride, box, label, 16, text_color);
}

static void draw_parent_footer(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                bool plan_subpage)
{
    bool compact_left = plan_subpage || ptc_ui_parent_footer_feedback(model);
    if (!compact_left) {
        draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(0),
                           ptc_ui_text(PTC_UI_T_L_PREVIOUS_PAGE));
        draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(1),
                           ptc_ui_text(PTC_UI_T_R_NEXT_PAGE));
    }
    PtcUiRect back = compact_left ? ptc_ui_parent_subpage_footer_rect(0) : ptc_ui_parent_footer_rect(2);
    PtcUiRect refresh = compact_left ? ptc_ui_parent_subpage_footer_rect(1) : ptc_ui_parent_footer_rect(3);
    draw_footer_button(pixels, stride, back,
        plan_subpage ? ptc_ui_text(model->today_settings_origin ? PTC_UI_T_BACK_TO_TODAY : PTC_UI_T_B_BACK_TO_PLANS_2) :
        ptc_ui_text(PTC_UI_T_B_RETURN_TO_CHILD_PAGE));
    {
        int64_t now = ptc_ui_render_now();
        draw_refresh_footer_button(pixels, stride, refresh, ptc_ui_text(PTC_UI_T_Y_REFRESH),
            ptc_ui_status_needs_refresh(model, now), ptc_ui_status_refresh_failed(model),
            model->parent_footer_focused && model->parent_footer_selection == 0, model->waiting);
    }
    draw_parent_status_footer(pixels, stride, model);
    if (ptc_ui_parent_footer_shortcuts(model)) {
        const PtcUiRect rects[] = {ptc_ui_parent_footer_details_rect(), ptc_ui_parent_footer_rules_rect()};
        const PtcUiTextId labels[] = {PTC_UI_T_VIEW_DETAILS, PTC_UI_T_VIEW_RULES_FOOTER};
        for (int i = 0; i < 2; ++i) {
            bool focused = model->parent_footer_focused &&
                model->parent_footer_selection == PTC_UI_PARENT_FOOTER_DETAILS + i;
            const char *label = ptc_ui_text(labels[i]);
            if (focused || model->parent_page == PTC_UI_PARENT_TODAY)
                draw_prominent_footer_action(pixels, stride, rects[i], label, focused, model->waiting);
            else
                draw_button_label(pixels, stride, to_uirect(rects[i]), label, 15,
                    model->waiting ? UI_DISABLED : UI_MUTED);
        }
    }
}

void draw_parent(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const bool plan_subpage = model->parent_page == PTC_UI_PARENT_PLAN &&
        model->plan_page != PTC_UI_PLAN_PAGE_ROOT;
    PtcUiNoticeProjection notice;
    ptc_ui_project_notice(model, &notice);
    bool alert_visible = ptc_ui_parent_status_alert_visible(model);
    bool notice_showing = notice.visible &&
        (ptc_ui_operation_feedback_visible(model) || !alert_visible);

    draw_parent_header(pixels, stride, model);
    if (!plan_subpage && model->parent_page != PTC_UI_PARENT_TODAY) {
        draw_parent_action_grid(pixels, stride, model);
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
        draw_parent_settings_help(pixels, stride, model);
    }
    if (model->parent_page == PTC_UI_PARENT_SUPPORT &&
        model->diagnostic_status != PTC_UI_DIAGNOSTIC_IDLE) {
        draw_diagnostic_notice(pixels, stride, model);
    } else if (notice_showing) {
        draw_notice(pixels, stride, model);
    }
    draw_parent_footer(pixels, stride, model, plan_subpage);
}
