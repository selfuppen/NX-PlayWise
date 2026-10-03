#include "ui_render_internal.h"

static void draw_shortcut_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char status[192];
    char shortcut_hint[160];
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 650);
    for (index = 0; index < PTC_UI_SHORTCUT_PRESET_COUNT; ++index) {
        UiRect option = to_uirect(ptc_ui_shortcut_option_rect(index));
        bool selected = index == model->setup_shortcut_index;
        bool chosen = model->shortcut_draft_enabled && selected;
        fill_round_rect(pixels, stride, option, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 12, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
        draw_text(pixels, stride, option.x + 14, option.y + 23,
                  ptc_ui_shortcut_common_label(index), 16,
                  selected ? UI_ACCENT : UI_INK);
        if (chosen) {
            const char *save_text = ptc_ui_text(PTC_UI_T_PENDING_SAVE);
            int tw = measure_text(save_text, 15);
            draw_text(pixels, stride, option.x + option.width - tw - 16, option.y + 23,
                      save_text, 15, UI_SUCCESS);
        }
    }
    draw_dialog_button(pixels, stride, ptc_ui_shortcut_disable_rect(),
                       ptc_ui_text(PTC_UI_T_ZL_CLOSE_CUSTOM_SHORTCUT_KEYS), UI_DANGER_SOFT, UI_DANGER, true);
    draw_dialog_button(pixels, stride, ptc_ui_shortcut_hint_rect(),
                       model->shortcut_draft_show_hint ? ptc_ui_text(PTC_UI_T_Y_CHILD_AREA_PROMPT_DISPLAY) : ptc_ui_text(PTC_UI_T_Y_CHILD_AREA_PROMPT_HIDE),
                       UI_PAGE, UI_INK, true);
    ptc_ui_format_custom_shortcut_hint(model->shortcut_draft_label, shortcut_hint, sizeof(shortcut_hint));
    snprintf(status, sizeof(status), "%s",
             model->shortcut_draft_enabled ? shortcut_hint : ptc_ui_text(PTC_UI_T_THE_CUSTOM_ENTRANCE_HAS_BEEN_CLOSED_FIXED));
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 516, status, 18,
              model->shortcut_draft_enabled ? UI_SUCCESS : UI_DANGER);
    draw_overlay_actions(pixels, stride, model, ptc_ui_text(PTC_UI_T_CONFIRM_TO_SAVE));
}

static void draw_software_info_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect details;
    char value[128];
    const char *status = ptc_ui_text(PTC_UI_T_STATUS_UNKNOWN);
    uint32_t status_color = UI_MUTED;
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 560);
    details = (UiRect){dialog.x + 34, dialog.y + 106, dialog.width - 68, 326};
    fill_round_rect(pixels, stride, details, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, details, 16, 1, UI_BORDER);
    const char *lbl_host = ptc_ui_text(PTC_UI_T_HOST_APPLICATION);
    const char *lbl_bg = ptc_ui_text(PTC_UI_T_CURRENT_BACKGROUND);
    const char *lbl_status = ptc_ui_text(PTC_UI_T_LOADING_STATUS);
    const char *lbl_repo = ptc_ui_text(PTC_UI_T_PROJECT_WAREHOUSE);
    const char *lbl_parent = ptc_ui_text(PTC_UI_T_PARENT_PAGE);

    int max_lbl_w = measure_text(lbl_host, 18);
    int w = measure_text(lbl_bg, 18); if (w > max_lbl_w) max_lbl_w = w;
    w = measure_text(lbl_status, 18); if (w > max_lbl_w) max_lbl_w = w;
    w = measure_text(lbl_repo, 18); if (w > max_lbl_w) max_lbl_w = w;
    w = measure_text(lbl_parent, 18); if (w > max_lbl_w) max_lbl_w = w;

    int val_offset = 24 + max_lbl_w + 16;
    if (val_offset < 180) val_offset = 180;
    int val_max_w = details.width - val_offset - 20;

    draw_text(pixels, stride, details.x + 24, details.y + 38, lbl_host, 18, UI_MUTED);
    snprintf(value, sizeof(value), "%.24s  (%.88s)", model->software_version,
             model->app_release_id[0] ? model->app_release_id : ptc_ui_text(PTC_UI_T_UNKNOWN_IDENTITY));
    fit_text(value, sizeof(value), value, 18, val_max_w);
    draw_text(pixels, stride, details.x + val_offset, details.y + 38, value, 18, UI_ACCENT);
    draw_text(pixels, stride, details.x + 24, details.y + 82, lbl_bg, 18, UI_MUTED);
    fit_text(value, sizeof(value), model->backend_release_id[0] ? model->backend_release_id : ptc_ui_text(PTC_UI_T_UNABLE_TO_TRUSTWORTHY_CONFIRMATION), 18,
             val_max_w);
    draw_text(pixels, stride, details.x + val_offset, details.y + 82, value, 18, UI_INK);
    if (model->hot_reload_status == PTC_UI_HOT_RELOAD_CURRENT ||
        model->hot_reload_status == PTC_UI_HOT_RELOAD_SUCCESS) {
        status = ptc_ui_text(PTC_UI_T_LOADED); status_color = UI_SUCCESS;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING) {
        status = ptc_ui_text(PTC_UI_T_TO_BE_LOADED); status_color = UI_WARNING;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_INCOMPLETE) {
        status = ptc_ui_text(PTC_UI_T_INCOMPLETE_INSTALLATION); status_color = UI_DANGER;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_RECOVERY_REQUIRED) {
        status = ptc_ui_text(PTC_UI_T_REQUIRES_RECOVERY); status_color = UI_DANGER;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_RUNNING) {
        status = ptc_ui_text(PTC_UI_T_IS_LOADING); status_color = UI_WARNING;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_UNAVAILABLE) {
        status = ptc_ui_text(PTC_UI_T_THE_NEW_VERSION_CANNOT_BE_LOADED_YET); status_color = UI_WARNING;
    }
    draw_text(pixels, stride, details.x + 24, details.y + 126, lbl_status, 18, UI_MUTED);
    draw_text(pixels, stride, details.x + val_offset, details.y + 126, status, 20, status_color);
    draw_wrapped_text(pixels, stride, details.x + val_offset, details.y + 154,
        model->hot_reload_detail, 15, val_max_w, 20, 2, UI_MUTED);
    draw_text(pixels, stride, details.x + 24, details.y + 216, lbl_repo, 18, UI_MUTED);
    fit_text(value, sizeof(value), model->repository_url, 17, val_max_w);
    draw_text(pixels, stride, details.x + val_offset, details.y + 216, value, 17, UI_ACCENT);
    draw_text(pixels, stride, details.x + 24, details.y + 266, lbl_parent, 18, UI_MUTED);
    fit_text(value, sizeof(value), model->pwa_url, 17, val_max_w);
    draw_text(pixels, stride, details.x + val_offset, details.y + 266, value, 17, UI_SUCCESS);
    if (model->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING) {
        draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_NOT_YET),
                           UI_RAISED, UI_INK, false);
        draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_LOAD_NEW_VERSION),
                           UI_ACCENT, UI_ON_ACCENT, false);
    } else {
        draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_CLOSE),
                           UI_ACCENT, UI_ON_ACCENT, false);
    }
}

static void draw_album_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const char *state = model->album_restriction_state == 0 ? ptc_ui_text(PTC_UI_T_NOT_ENABLED) :
                        model->album_restriction_state == 1 ? ptc_ui_text(PTC_UI_T_ENABLED) :
                        model->album_restriction_state == 2 ? ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED_2) :
                        model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? ptc_ui_text(PTC_UI_T_EXTERNAL_CONFIGURED) : ptc_ui_text(PTC_UI_T_STATUS_UNKNOWN);
    const char *status_lbl = ptc_ui_text(PTC_UI_T_CURRENT_STATUS);
    int status_lbl_w = measure_text(status_lbl, 17);
    int state_x = dialog.x + 38 + status_lbl_w + 12;
    draw_dialog_shell(pixels, stride, model, &dialog, 980, 560);
    draw_text(pixels, stride, dialog.x + 38, dialog.y + 126, status_lbl, 17, UI_MUTED);
    draw_text(pixels, stride, state_x, dialog.y + 126, state, 19,
              model->album_restriction_state == 1 ? UI_SUCCESS :
              model->album_restriction_state == 2 ? UI_WARNING :
              model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? UI_ACCENT : UI_MUTED);
    draw_candidate_button(pixels, stride, ptc_ui_album_refresh_rect(), ptc_ui_text(PTC_UI_T_Y_RETEST),
                          UI_PAGE, UI_ACCENT, false, false);
    for (int index = 0; index < 2; ++index) {
        UiRect card = to_uirect(ptc_ui_album_action_rect(index));
        bool selected = model->overlay_selection == index;
        bool enabled = index == 0 ? model->album_restriction_state == 0 :
                       (model->album_restriction_state == 1 ||
                        (model->album_restriction_state == 2 && model->album_backup_valid));
        fill_round_rect(pixels, stride, card, 16, enabled && selected ? UI_ACCENT_SOFT :
                        (enabled ? UI_SURFACE : UI_PAGE));
        draw_rect_outline(pixels, stride, card, 16, enabled && selected ? 3 : 1, enabled && selected ? UI_ACCENT : UI_BORDER);
        const char *card_title = index == 0 ? (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL
                                                ? ptc_ui_text(PTC_UI_T_NO_NEED_TO_REPEAT_CONFIGURATION) : ptc_ui_text(PTC_UI_T_CONFIGURE_ADVANCED_ENTRY_OF_HOMEBREW_MENU)) :
                                (model->album_restriction_state == 2 && model->album_backup_valid ? ptc_ui_text(PTC_UI_T_FORCE_RECOVERY_OF_TRUSTED_BACKUPS) : ptc_ui_text(PTC_UI_T_RESTORE_THE_ORIGINAL_STARTUP_MODE));
        int title_size = 21;
        while (title_size > 16 && measure_text(card_title, title_size) > card.width - 48) --title_size;
        draw_text(pixels, stride, card.x + 24, card.y + 40, card_title, title_size, enabled ? UI_INK : UI_DISABLED);
        const char *card_desc = index == 0
            ? (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL
                ? ptc_ui_text(PTC_UI_T_THE_CURRENT_DISK_CONFIGURATION_ALREADY_PROVIDES_THE)
                : ptc_ui_text(PTC_UI_T_FIRST_MAKE_A_COMPLETE_BACKUP_OF_RELEVANT))
            : ptc_ui_text(PTC_UI_T_RESTORE_THE_ORIGINAL_CONFIGURATION_ACCORDING_TO_THE);
        draw_wrapped_text(pixels, stride, card.x + 24, card.y + 76, card_desc,
                          14, card.width - 48, 20, 3, enabled ? UI_MUTED : UI_DISABLED);
        draw_text(pixels, stride, card.x + 24, card.y + 194,
                  enabled ? ptc_ui_text(PTC_UI_T_A_CONTINUE) : ptc_ui_text(PTC_UI_T_CURRENT_STATUS_IS_NOT_AVAILABLE), 16,
                  enabled ? UI_ACCENT : UI_DISABLED);
    }
    if (model->album_restriction_detail[0]) {
        draw_wrapped_text(pixels, stride, dialog.x + 38, dialog.y + 432,
                          model->album_restriction_detail, 14, dialog.width - 76, 20, 2, UI_MUTED);
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
                       UI_RAISED, UI_INK, true);
}

static void draw_theme_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *LABELS[] = {ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM), ptc_ui_text(PTC_UI_T_LIGHT), ptc_ui_text(PTC_UI_T_DARK)};
    const char *DETAILS[] = {ptc_ui_text(PTC_UI_T_FOLLOWS_SWITCH_SETTINGS), ptc_ui_text(PTC_UI_T_REFRESHING_LIGHT_BACKGROUND), ptc_ui_text(PTC_UI_T_SOFT_DARK_BACKGROUND)};
    UiRect dialog;
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 820, 360);
    for (index = 0; index < 3; ++index) {
        UiRect option = to_uirect(ptc_ui_theme_option_rect(index));
        bool selected = index == model->overlay_selection;
        fill_round_rect(pixels, stride, option, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 12, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
        draw_text_center(pixels, stride, (UiRect){option.x, option.y + 14, option.width, 34},
                         LABELS[index], 22, UI_INK);
        draw_text_center(pixels, stride, (UiRect){option.x, option.y + 52, option.width, 26},
                         DETAILS[index], 15, UI_MUTED);
    }
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 294,
              !g_theme.system_theme_available && model->overlay_selection == PTC_UI_THEME_SYSTEM
                  ? ptc_ui_text(PTC_UI_T_THE_SYSTEM_THEME_IS_TEMPORARILY_UNAVAILABLE_AND) : ptc_ui_text(PTC_UI_T_D_PAD_SELECT_A_APPLY_SAVE_B),
              16, !g_theme.system_theme_available ? UI_WARNING : UI_MUTED);
}

static void draw_language_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *LABELS[] = {ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM), ptc_ui_text(PTC_UI_T_SIMPLIFIED_CHINESE), ptc_ui_text(PTC_UI_T_TRADITIONAL_CHINESE), "English"};
    const char *DETAILS[] = {
        ptc_ui_text(PTC_UI_T_USE_SWITCH_SYSTEM_LANGUAGE), ptc_ui_text(PTC_UI_T_ALWAYS_SHOW_SIMPLIFIED_CHINESE), ptc_ui_text(PTC_UI_T_ALWAYS_SHOW_TRADITIONAL_CHINESE), "Always use English"
    };
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 820, 360);
    for (int index = 0; index < 4; ++index) {
        UiRect option = to_uirect(ptc_ui_language_option_rect(index));
        bool selected = index == model->overlay_selection;
        fill_round_rect(pixels, stride, option, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 12, selected ? 3 : 1,
            selected ? UI_ACCENT : UI_CONTROL);
        int label_size = 20;
        while (label_size > 14 && measure_text(LABELS[index], label_size) > option.width - 16) --label_size;
        draw_text_center(pixels, stride,
            (UiRect){option.x, option.y + 12, option.width, 30}, LABELS[index], label_size, UI_INK);

        int detail_size = 14;
        while (detail_size > 11 && measure_text(DETAILS[index], detail_size) > option.width - 16) --detail_size;
        draw_text_center(pixels, stride,
            (UiRect){option.x, option.y + 44, option.width, 24}, DETAILS[index], detail_size, UI_MUTED);
    }
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 306,
        ptc_ui_text(PTC_UI_T_D_PAD_SELECT_A_APPLY_SAVE_B), 16, UI_MUTED);
}

static void draw_support_event_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    int index = model->overlay_selection;
    char time_text[48];
    char row[224];
    int y;
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 560);
    if (index < 0 || index >= model->recent_event_count) index = 0;
    format_event_time(model->recent_event_timestamps[index], true, time_text, sizeof(time_text));
    y = dialog.y + 132;
    snprintf(row, sizeof(row), ptc_ui_text(PTC_UI_T_EVENT_S), model->recent_event_names[index][0] ? model->recent_event_names[index] : ptc_ui_text(PTC_UI_T_UNKNOWN));
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y, row, 18, dialog.width - 84, 28, 2, UI_INK);
    snprintf(row, sizeof(row), ptc_ui_text(PTC_UI_T_ACTION_TYPE_S), model->recent_event_types[index][0] ? model->recent_event_types[index] : ptc_ui_text(PTC_UI_T_NOT_DOCUMENTED));
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2, UI_MUTED);
    snprintf(row, sizeof(row), ptc_ui_text(PTC_UI_T_RESULT_S), model->recent_event_errors[index][0] ? model->recent_event_errors[index] : ptc_ui_text(PTC_UI_T_SUCCESS));
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2,
                          model->recent_event_errors[index][0] ? UI_DANGER : UI_SUCCESS);
    snprintf(row, sizeof(row), ptc_ui_text(PTC_UI_T_TIME_S), time_text);
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2, UI_MUTED);
    snprintf(row, sizeof(row), ptc_ui_text(PTC_UI_T_REQUEST_ID_S), model->recent_event_request_ids[index][0] ? model->recent_event_request_ids[index] : ptc_ui_text(PTC_UI_T_NOT_DOCUMENTED));
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 16, dialog.width - 84, 24, 3, UI_MUTED);
    snprintf(row, sizeof(row), ptc_ui_text(PTC_UI_T_INTERNAL_DETAILS_S), model->recent_event_details[index][0] ? model->recent_event_details[index] : ptc_ui_text(PTC_UI_T_NONE));
    draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 16, dialog.width - 84, 24, 3, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_B_CLOSE),
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void format_decision_rule(const PtcUiDecisionStep *step, char *out, size_t out_size)
{
    if (!step || step->state == PTC_UI_DECISION_UNKNOWN ||
        step->state == PTC_UI_DECISION_NOT_CONFIGURED || step->state == PTC_UI_DECISION_NOT_MATCHED ||
        step->state == PTC_UI_DECISION_DISABLED || step->state == PTC_UI_DECISION_CALENDAR_UNCOVERED) {
        snprintf(out, out_size, "%s", step ? ptc_ui_decision_state_label(step->state)
            : ptc_ui_text(PTC_UI_T_DECISION_UNKNOWN));
    } else if (step->rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_UNLIMITED));
    } else {
        PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("minutes", step->rule.minutes)};
        (void)ptc_ui_text_format(PTC_UI_T_SHORT_MINUTES, out, out_size, args, 1);
    }
}

static void draw_waterfall_pipeline(uint32_t *pixels, uint32_t stride, int x, int y, int total_w,
                                    const PtcUiTodayDecision *decision, bool bedtime_enforcing)
{
    const char *titles[] = {ptc_ui_text(PTC_UI_T_01_TODAY_S_ADJUSTMENT), ptc_ui_text(PTC_UI_T_02_PLAN_SPECIAL_EXCEPTIONS), ptc_ui_text(PTC_UI_T_03_HOLIDAY_EXCEPTIONS), ptc_ui_text(PTC_UI_T_04_BASIC_WEEKLY_SCHEDULE)};
    const PtcUiDecisionStep *steps[] = {
        &decision->today_override,
        &decision->scheduled_override,
        &decision->holiday,
        &decision->weekly
    };
    int count = 4;
    int node_w = 216;
    int node_h = 136;
    int gap = (total_w - node_w * count) / (count - 1);
    bool hit_found = false;

    draw_text(pixels, stride, x, y + 6, ptc_ui_text(PTC_UI_T_THE_DAILY_QUOTA_IS_DETERMINED_IN_THE), 14, UI_INK);
    if (bedtime_enforcing) {
        UiRect pill = {x + total_w - 290, y, 290, 24};
        fill_round_rect(pixels, stride, pill, 6, UI_DANGER_SOFT);
        draw_rect_outline(pixels, stride, pill, 6, 1, UI_DANGER);
        draw_text_center(pixels, stride, pill, ptc_ui_text(PTC_UI_T_BEDTIME_RESTRICTIONS_ARE_IN_EFFECT), 12, UI_DANGER);
    }
    int cards_y = y + 26;

    for (int i = 0; i < count; ++i) {
        int node_x = x + i * (node_w + gap);
        UiRect card = {node_x, cards_y, node_w, node_h};
        const PtcUiDecisionStep *step = steps[i];
        bool is_selected = (step->state == PTC_UI_DECISION_SELECTED);
        bool is_overridden = (step->state == PTC_UI_DECISION_OVERRIDDEN);
        char rule_val[32];
        format_decision_rule(step, rule_val, sizeof(rule_val));

        /* Card background & outline */
        if (is_selected) {
            if (bedtime_enforcing) {
                fill_round_rect(pixels, stride, card, 12, UI_RGB(ui_mix_rgb(UI_BLENDED(surface), UI_BLENDED(danger), 14)));
                draw_rect_outline(pixels, stride, card, 12, 2, UI_DANGER);
            } else {
                fill_round_rect(pixels, stride, card, 12, UI_SUCCESS_SOFT);
                draw_rect_outline(pixels, stride, card, 12, 2, UI_SUCCESS);
            }
        } else if (is_overridden) {
            fill_round_rect(pixels, stride, card, 12, UI_RAISED);
            draw_rect_outline(pixels, stride, card, 12, 1, UI_BORDER);
        } else {
            fill_round_rect(pixels, stride, card, 12, UI_PAGE);
            draw_rect_outline(pixels, stride, card, 12, 1, UI_BORDER);
        }

        /* 1. Header title */
        draw_text(pixels, stride, card.x + 14, card.y + 20, titles[i], 14,
                  is_selected ? (bedtime_enforcing ? UI_DANGER : UI_SUCCESS) : (is_overridden ? UI_INK : UI_MUTED));

        /* 2. Status Badge */
        UiRect badge = {card.x + 12, card.y + 30, card.width - 24, 22};
        if (is_selected) {
            if (bedtime_enforcing) {
                fill_round_rect(pixels, stride, badge, 4, UI_DANGER);
                draw_text_center(pixels, stride, badge, ptc_ui_text(PTC_UI_T_THIS_RULE_APPLIES_TO_THE_DAILY_QUOTA), 11, UI_ON_ACCENT);
            } else {
                fill_round_rect(pixels, stride, badge, 4, UI_SUCCESS);
                draw_text_center(pixels, stride, badge, ptc_ui_text(PTC_UI_T_THIS_RULE_APPLIES_TO_THE_DAILY_QUOTA), 11, UI_ON_ACCENT);
            }
        } else if (is_overridden) {
            fill_round_rect(pixels, stride, badge, 4, UI_WARNING_SOFT);
            draw_text_center(pixels, stride, badge, ptc_ui_text(PTC_UI_T_TAKES_PRECEDENCE_OVER_THE_PREVIOUS_RULES), 11, UI_WARNING);
        } else {
            fill_round_rect(pixels, stride, badge, 4, UI_BORDER);
            draw_text_center(pixels, stride, badge, ptc_ui_decision_state_label(step->state), 11, UI_MUTED);
        }

        /* 3. Rule Value */
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 60, card.width, 26},
                         rule_val, 18, is_selected ? (bedtime_enforcing ? UI_DANGER : UI_SUCCESS) : (is_overridden ? UI_MUTED : UI_DISABLED));

        /* 4. Subtext explanation */
        const char *desc;
        if (is_selected) {
            desc = bedtime_enforcing ? ptc_ui_text(PTC_UI_T_THE_QUOTA_HAS_BEEN_DETERMINED_SLEEPING_LIMIT) : ptc_ui_text(PTC_UI_T_THE_FOLLOWING_RULES_ARE_NO_LONGER_USED);
        } else if (is_overridden) {
            desc = ptc_ui_text(PTC_UI_T_IS_SET_BUT_THE_PREVIOUS_RULE_TAKES);
        } else if (step->state == PTC_UI_DECISION_DISABLED) {
            desc = ptc_ui_text(PTC_UI_T_NOT_ENABLED_OR_CONFIGURED);
        } else if (step->state == PTC_UI_DECISION_CALENDAR_UNCOVERED) {
            desc = ptc_ui_text(PTC_UI_T_THE_DATE_IS_OUTSIDE_THE_BUILT_IN);
        } else {
            desc = (i == 0 ? ptc_ui_text(PTC_UI_T_NO_SEPARATE_ADJUSTMENT_CONTINUE_TO_THE_NEXT) :
                   (i == 2 ? ptc_ui_text(PTC_UI_T_IT_IS_NOT_A_HOLIDAY_CONTINUE_TO) : ptc_ui_text(PTC_UI_T_NOT_APPLICABLE_TODAY_CONTINUE_TO_THE_NEXT)));
        }
        draw_line(pixels, stride, card.x + 12, card.y + 98, card.x + card.width - 12, card.y + 98, 1, UI_BORDER);
        draw_wrapped_text(pixels, stride, card.x + 12, card.y + 113, desc, 11,
                          card.width - 24, 12, 2,
                          is_selected ? (bedtime_enforcing ? UI_DANGER : UI_SUCCESS) : UI_MUTED);

        /* 5. Connector line between node i and node i+1 */
        if (i < count - 1) {
            int line_start_x = card.x + card.width;
            int line_end_x = line_start_x + gap;
            int line_y = cards_y + node_h / 2;

            if (is_selected) {
                /* Short-circuit stop mark: ─┤ 阻断 */
                draw_line(pixels, stride, line_start_x, line_y, line_start_x + gap / 2, line_y, 2, UI_MUTED);
                draw_line(pixels, stride, line_start_x + gap / 2, line_y - 14, line_start_x + gap / 2, line_y + 14, 3, UI_DANGER);
                draw_text_center(pixels, stride, (UiRect){line_start_x, line_y - 28, gap, 16}, ptc_ui_text(PTC_UI_T_ADOPTED), 11, UI_DANGER);
                hit_found = true;
            } else if (!hit_found) {
                /* Active flow arrow: ──> (geometric vector arrow, perfectly aligned to line_y) */
                int arrow_tip_x = line_end_x - 6;
                draw_line(pixels, stride, line_start_x, line_y, arrow_tip_x, line_y, 2, UI_ACCENT);
                draw_line(pixels, stride, arrow_tip_x - 6, line_y - 5, arrow_tip_x, line_y, 2, UI_ACCENT);
                draw_line(pixels, stride, arrow_tip_x - 6, line_y + 5, arrow_tip_x, line_y, 2, UI_ACCENT);
            } else {
                /* Inactive bypassed line: ┄┄ */
                draw_line(pixels, stride, line_start_x, line_y, line_end_x, line_y, 1, UI_BORDER);
            }
        }
    }
}

static void draw_home_decision_details(uint32_t *pixels, uint32_t stride,
                                       const PtcUiModel *model, UiRect dialog)
{
    PtcUiTodayDecision decision;
    char total[64], remaining[64], effective[96], timer[64], today[64], line[256];
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    const char *notice = ptc_ui_runtime_notice_summary(model);
    bool error = strcmp(model->result_status, "error") == 0;
    bool parent = model->view == PTC_UI_PARENT;
    int played = fresh && model->played_minutes_available ? model->played_minutes : -1;
    const char *runtime = !model->status_loaded ? ptc_ui_text(PTC_UI_T_WAITING_FOR_REFRESH) :
        model->disable_flag_present ? ptc_ui_text(PTC_UI_T_CONTROL_IS_DISABLED) : model->recovery_active ? ptc_ui_text(PTC_UI_T_RECOVERING) :
        model->apply_pending_confirmation ? ptc_ui_text(PTC_UI_T_WAITING_FOR_EFFECT) : !fresh ? ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED) :
        strcmp(model->setup_phase, "active") == 0 ? ptc_ui_text(PTC_UI_T_RUNNING_NORMALLY) : ptc_ui_text(PTC_UI_T_REQUIRES_PARENT_CONFIRMATION);

    ptc_ui_build_today_decision(model, PTC_UI_PLAN_SAVED, ptc_ui_render_now(), &decision);
    ptc_ui_format_home_total_value(model, total, sizeof(total));
    ptc_ui_format_home_remaining(model, ptc_ui_render_now(), remaining, sizeof(remaining));
    ptc_ui_format_timer_status(model, timer, sizeof(timer));
    ptc_ui_format_today_mode(model, today, sizeof(today));

    if (fresh) {
        snprintf(effective, sizeof(effective), "%s  |  %s",
                 ptc_ui_effective_rule_label(decision.effective.source), total);
    } else {
        snprintf(effective, sizeof(effective), ptc_ui_text(PTC_UI_T_STATUS_AWAITING_CONFIRMATION_NOT_AVAILABLE_YET));
        snprintf(total, sizeof(total), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        snprintf(today, sizeof(today), ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
        snprintf(timer, sizeof(timer), ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
    }

    int top_y = dialog.y + 68;
    int full_w = 1064;
    int col_w = 520;
    int x_left = dialog.x + 28;
    int x_right = dialog.x + 572;

    /* 1. Hero 看板卡片 (1064 x 80) */
    UiRect hero = {x_left, top_y, full_w, 80};
    fill_round_rect_gradient(pixels, stride, hero, 12, UI_ACCENT_SOFT,
                             UI_RGB(ui_darken(UI_BLENDED(accent_soft), 5)));
    draw_text(pixels, stride, hero.x + 20, hero.y + 22, ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY), 13, UI_MUTED);
    draw_text(pixels, stride, hero.x + 20, hero.y + 64, remaining, 32, fresh ? UI_ACCENT : UI_MUTED);

    draw_text(pixels, stride, hero.x + 210, hero.y + 22, ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE), 13, UI_MUTED);
    draw_text(pixels, stride, hero.x + 210, hero.y + 56, total, 20, UI_INK);

    bool is_en = (ptc_ui_language_get_resolved() == PTC_UI_LANGUAGE_ENGLISH);
    draw_text(pixels, stride, hero.x + 370, hero.y + 22, ptc_ui_text(PTC_UI_T_USED_QUOTA_EST), 13, UI_MUTED);
    if (fresh && played >= 0) snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_D_MIN_2), played);
    else snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    draw_text(pixels, stride, hero.x + 370, hero.y + 56, line, 20, UI_INK);

    /* 生效规则指示徽章框 */
    UiRect active_badge = {hero.x + 540, hero.y + 14, hero.width - 556, 52};
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    uint32_t badge_bg = !fresh ? UI_WARNING_SOFT : (bedtime_enforcing ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
    uint32_t badge_border = !fresh ? UI_WARNING : (bedtime_enforcing ? UI_DANGER : UI_SUCCESS);
    fill_round_rect(pixels, stride, active_badge, 8, badge_bg);
    draw_rect_outline(pixels, stride, active_badge, 8, 1, badge_border);
    const char *badge_title = !fresh ? ptc_ui_text(PTC_UI_T_RULES_PENDING_CONFIRMATION) :
        (bedtime_enforcing ? ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_UNDER_BEDTIME_LIMIT) : ptc_ui_text(PTC_UI_T_RULES_ADOPTED_TODAY));
    draw_text(pixels, stride, active_badge.x + 14, active_badge.y + 20,
              badge_title, 12, badge_border);
    int badge_title_w = measure_text(badge_title, 12);
    draw_text(pixels, stride, active_badge.x + 14 + badge_title_w + 14, active_badge.y + 20, effective, 14, UI_INK);
    const char *final_desc = bedtime_enforcing
        ? ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_HAS_BEEN_DETERMINED_CURRENT) : decision.final_reason;
    draw_wrapped_text(pixels, stride, active_badge.x + 14, active_badge.y + 40, final_desc,
                      11, active_badge.width - 28, 15, 1, UI_MUTED);

    /* 2. 额度决策流水线 (1064 x 164) */
    draw_waterfall_pipeline(pixels, stride, x_left, top_y + 88, full_w, &decision, bedtime_enforcing);

    /* 3. 底部双栏 (520 + 520) */
    int bottom_y = top_y + 258;

    /* 左下栏：并行就寝与自主缓冲 */
    UiRect bedtime = {x_left, bottom_y, col_w, 76};
    fill_round_rect(pixels, stride, bedtime, 10, bedtime_enforcing ? UI_DANGER_SOFT : UI_WARNING_SOFT);
    draw_rect_outline(pixels, stride, bedtime, 10, 1, bedtime_enforcing ? UI_DANGER : UI_WARNING);
        draw_text(pixels, stride, bedtime.x + 14, bedtime.y + 22,
              ptc_ui_text(PTC_UI_T_BEDTIME_LIMIT), 13, bedtime_enforcing ? UI_DANGER : UI_WARNING);
    if (bedtime_enforcing) {
        UiRect enforcing_pill = {bedtime.x + bedtime.width - 112, bedtime.y + 8, 98, 24};
        fill_round_rect(pixels, stride, enforcing_pill, 6, UI_DANGER);
        draw_text_center(pixels, stride, enforcing_pill, ptc_ui_text(PTC_UI_T_RESTRICTED_USE), 12, UI_ON_ACCENT);
    } else {
        char bedtime_status[128];
        fit_text(bedtime_status, sizeof(bedtime_status), decision.bedtime, 14, bedtime.width - (is_en ? 190 : 158));
        draw_text(pixels, stride, bedtime.x + (is_en ? 170 : 130), bedtime.y + 22, bedtime_status, 14, UI_INK);
    }
    draw_text(pixels, stride, bedtime.x + 14, bedtime.y + 52,
              bedtime_enforcing ? ptc_ui_text(PTC_UI_T_IT_IS_CURRENTLY_IN_THE_BEDTIME_PERIOD)
                                : ptc_ui_text(PTC_UI_T_AFTER_BEDTIME_USAGE_WILL_BE_RESTRICTED_EVEN), 11, UI_MUTED);

    UiRect autonomy = {x_left, bottom_y + 84, col_w, 76};
    fill_round_rect(pixels, stride, autonomy, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, autonomy, 10, 1, UI_BORDER);
    draw_text(pixels, stride, autonomy.x + 14, autonomy.y + 22, ptc_ui_text(PTC_UI_T_AUTONOMY_BUFFER), 13, UI_MUTED);
    draw_text(pixels, stride, autonomy.x + (is_en ? 170 : 130), autonomy.y + 22, decision.autonomy, 14, UI_INK);
    draw_wrapped_text(pixels, stride, autonomy.x + 14, autonomy.y + 52,
                      ptc_ui_text(PTC_UI_T_THE_CHILD_APPLIES_INDEPENDENTLY_WHEN_THE_QUOTA),
                      11, autonomy.width - 28, 12, 2, UI_MUTED);

    /* Eye care is parallel to quota selection, so explain it outside the waterfall. */
    UiRect eye_rule = {x_left, bottom_y + 166, col_w, 64};
    fill_round_rect(pixels, stride, eye_rule, 10, UI_WARNING_SOFT);
    draw_rect_outline(pixels, stride, eye_rule, 10, 1, UI_WARNING);
    draw_text(pixels, stride, eye_rule.x + 14, eye_rule.y + 18,
        ptc_ui_text(PTC_UI_T_EYE_CARE), 12, UI_WARNING);
    draw_wrapped_text(pixels, stride, eye_rule.x + 14, eye_rule.y + 36,
        ptc_ui_text(PTC_UI_T_EYE_CARE_INDEPENDENT_RULE), 10,
        eye_rule.width - 28, 13, 2, UI_MUTED);

    /* 右下栏：系统运行、审计与健康 */
    UiRect sys_card = {x_right, bottom_y, col_w, 76};
    fill_round_rect(pixels, stride, sys_card, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, sys_card, 10, 1, UI_BORDER);

    draw_text(pixels, stride, sys_card.x + 14, sys_card.y + 20, ptc_ui_text(PTC_UI_T_TODAY_MODE), 11, UI_MUTED);
    draw_text(pixels, stride, sys_card.x + 14, sys_card.y + 44, today, 14, UI_INK);

    draw_text(pixels, stride, sys_card.x + 130, sys_card.y + 20, ptc_ui_text(PTC_UI_T_SYSTEM_TIMER), 11, UI_MUTED);
    draw_text(pixels, stride, sys_card.x + 130, sys_card.y + 44, timer, 14, fresh ? UI_INK : UI_WARNING);

    draw_text(pixels, stride, sys_card.x + 250, sys_card.y + 20, ptc_ui_text(PTC_UI_T_PLAYWISE_GUARDIAN), 11, UI_MUTED);
    draw_text(pixels, stride, sys_card.x + 250, sys_card.y + 44, runtime, 14,
              notice[0] || !fresh ? UI_WARNING : UI_SUCCESS);

    draw_text(pixels, stride, sys_card.x + 380, sys_card.y + 20, ptc_ui_text(PTC_UI_T_CONSUMPTION_IN_THE_PAST_7_DAYS), 11, UI_MUTED);
    if (model->usage_summary_available && model->usage_known_days_7 > 0)
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_MIN), model->usage_consumed_minutes_7);
    else snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    draw_text(pixels, stride, sys_card.x + 380, sys_card.y + 44, line, 14, UI_INK);

    if (parent) {
        UiRect audit = {x_right, bottom_y + 84, col_w, 76};
        fill_round_rect(pixels, stride, audit, 10, UI_RAISED);
        draw_rect_outline(pixels, stride, audit, 10, 1, UI_BORDER);
        draw_text(pixels, stride, audit.x + 14, audit.y + 18, ptc_ui_text(PTC_UI_T_RECENT_OPERATIONS), 12, UI_INK);
        char cmd_buf[128], tr_buf[128];
        snprintf(line, sizeof(line), "%s / %s",
                 ptc_ui_localize(model->command_name, cmd_buf, sizeof(cmd_buf)),
                 ptc_ui_localize(model->transport_label, tr_buf, sizeof(tr_buf)));
        draw_text(pixels, stride, audit.x + 200, audit.y + 18, line, 11, UI_MUTED);
        draw_line(pixels, stride, audit.x + 14, audit.y + 26, audit.x + audit.width - 14, audit.y + 26, 1, UI_BORDER);
        draw_wrapped_text(pixels, stride, audit.x + 14, audit.y + 42, model->message,
                          12, audit.width - 28, 16, 1, error ? UI_DANGER : UI_INK);
        draw_wrapped_text(pixels, stride, audit.x + 14, audit.y + 60, model->feedback_detail,
                          10, audit.width - 28, 14, 1, error ? UI_DANGER : UI_MUTED);
    } else {
        UiRect child_tip = {x_right, bottom_y + 84, col_w, 76};
        fill_round_rect(pixels, stride, child_tip, 10, UI_RAISED);
        draw_rect_outline(pixels, stride, child_tip, 10, 1, UI_BORDER);
        draw_text(pixels, stride, child_tip.x + 14, child_tip.y + 20, ptc_ui_text(PTC_UI_T_HEALTHY_PHONE_REMINDER), 12, UI_MUTED);
        draw_text(pixels, stride, child_tip.x + 14, child_tip.y + 44,
                  ptc_ui_text(PTC_UI_T_MESSAGE_2), 13, UI_INK);
        if (model->usage_summary_available && model->usage_known_days_30 > 0)
            snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_RECORDED_USAGE_IN_LAST_30_DAYS_U), model->usage_consumed_minutes_30);
        else snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_MESSAGE_3));
        draw_text(pixels, stride, child_tip.x + 14, child_tip.y + 64, line, 11, UI_MUTED);
    }

    UiRect eye_status = {x_right, bottom_y + 166, col_w, 64};
    char eye_cycle[160];
    ptc_ui_format_eye_care_cycle(model, ptc_ui_render_now(), eye_cycle, sizeof(eye_cycle));
    fill_round_rect(pixels, stride, eye_status, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, eye_status, 10, 1, UI_BORDER);
    draw_text(pixels, stride, eye_status.x + 14, eye_status.y + 19,
        ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_LABEL), 12, UI_MUTED);
    draw_text(pixels, stride, eye_status.x + 14, eye_status.y + 46,
        eye_cycle, 17, fresh ? UI_INK : UI_WARNING);
}

static void draw_home_details(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char age[64];
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 640);
    format_status_age(model, age, sizeof(age));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 736, dialog.y + 26, 352, 30}, age, 17, status_age_color(model));
    draw_home_decision_details(pixels, stride, model, dialog);
    home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_B_RETURN), false, true, false);
}

static void draw_forecast_day_details(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiTodayDecision decision;
    int offset = model->forecast_detail_day_offset;
    if (offset < 0 || offset >= (int)PTC_RESULT_FORECAST_DAYS) offset = 0;
    uint16_t target_day_index = model->day_index + offset;
    uint8_t weekday = ptc_weekday_from_day_index(target_day_index);
    bool is_today = (offset == 0);
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    char total[64], title_buf[96], age_buf[64], eff_label[96], date_buf[32];

    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 640);
    format_status_age(model, age_buf, sizeof(age_buf));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 736, dialog.y + 26, 352, 30},
                     age_buf, 17, status_age_color(model));

    ptc_ui_build_day_decision(model, PTC_UI_PLAN_SAVED, target_day_index, ptc_ui_render_now(), &decision);

    PtcUiTextId title_id = is_today ? PTC_UI_T_FORECAST_TITLE_TODAY :
        offset == 1 ? PTC_UI_T_FORECAST_TITLE_TOMORROW : PTC_UI_T_FORECAST_TITLE_DAY;
    PtcUiTextArg title_args[] = {
        PTC_UI_TEXT_STRING("weekday", ptc_ui_weekday_label(weekday)),
        PTC_UI_TEXT_NUMBER("offset", offset)
    };
    (void)ptc_ui_text_format(title_id, title_buf, sizeof(title_buf), title_args, 2);
    draw_text(pixels, stride, dialog.x + 36, dialog.y + 44, title_buf, 23, UI_INK);

    int top_y = dialog.y + 68;
    int full_w = 1064;
    int col_w = 520;
    int x_left = dialog.x + 28;

    /* 1. Hero 看板卡片 (1064 x 80) */
    UiRect hero = {x_left, top_y, full_w, 80};
    fill_round_rect_gradient(pixels, stride, hero, 12, UI_ACCENT_SOFT,
                             UI_RGB(ui_darken(UI_BLENDED(accent_soft), 5)));

    if (decision.effective.rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(total, sizeof(total), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    } else {
        PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("minutes", decision.effective.rule.minutes)};
        (void)ptc_ui_text_format(PTC_UI_T_SHORT_MINUTES, total, sizeof(total), args, 1);
    }

    draw_text(pixels, stride, hero.x + 20, hero.y + 22, ptc_ui_text(PTC_UI_T_TOTAL_QUOTA_FOR_THE_DAY), 13, UI_MUTED);
    draw_text(pixels, stride, hero.x + 20, hero.y + 60, total, 28, UI_INK);

    bool covered = false;
    PtcCalendarDayType day_type = ptc_holiday_calendar_classify(target_day_index, &covered);
    const char *type_label = (covered && day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY) ? ptc_ui_text(PTC_UI_T_STATUTORY_HOLIDAYS) :
        ((covered && day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY) ? ptc_ui_text(PTC_UI_T_ADJUSTED_WORKING_DAYS) :
         ((weekday == 0 || weekday == 6) ? ptc_ui_text(PTC_UI_T_ORDINARY_WEEKEND) : ptc_ui_text(PTC_UI_T_ORDINARY_WORKING_DAYS)));

    draw_text(pixels, stride, hero.x + 190, hero.y + 22, ptc_ui_text(PTC_UI_T_CALENDAR_ATTRIBUTES), 13, UI_MUTED);
    draw_text(pixels, stride, hero.x + 190, hero.y + 58, type_label, 20,
              (covered && day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY) ? UI_WARNING :
              ((covered && day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY) ? UI_ACCENT : UI_INK));

    draw_text(pixels, stride, hero.x + 340, hero.y + 22, ptc_ui_text(PTC_UI_T_DATE_OF_OWNERSHIP), 13, UI_MUTED);
    ptc_format_date(target_day_index, date_buf);
    draw_text(pixels, stride, hero.x + 340, hero.y + 58, date_buf, 20, UI_INK);

    /* 生效规则指示徽章框 */
    UiRect active_badge = {hero.x + 500, hero.y + 14, hero.width - 516, 52};
    fill_round_rect(pixels, stride, active_badge, 8, fresh ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
    draw_rect_outline(pixels, stride, active_badge, 8, 1, fresh ? UI_SUCCESS : UI_WARNING);

    snprintf(eff_label, sizeof(eff_label), "%s  |  %s",
             ptc_ui_effective_rule_label(decision.effective.source), total);
    draw_text(pixels, stride, active_badge.x + 14, active_badge.y + 20,
              fresh ? ptc_ui_text(PTC_UI_T_FINAL_EFFECTIVE_RULES) : ptc_ui_text(PTC_UI_T_RULES_PENDING_CONFIRMATION), 12, fresh ? UI_SUCCESS : UI_WARNING);
    draw_text(pixels, stride, active_badge.x + 120, active_badge.y + 20, eff_label, 14, UI_INK);
    draw_wrapped_text(pixels, stride, active_badge.x + 14, active_badge.y + 40, decision.final_reason,
                      11, active_badge.width - 28, 15, 1, UI_MUTED);

    /* 2. 额度决策流水线 (1064 x 164) */
    draw_waterfall_pipeline(pixels, stride, x_left, top_y + 88, full_w, &decision,
                            is_today && model->bedtime_active && !model->bedtime_skipped);

    /* 3. 底部双栏 (520 + 520) */
    int bottom_y = top_y + 258;

    /* 左下栏：就寝预测 */
    UiRect bedtime = {x_left, bottom_y, col_w, 94};
    fill_round_rect(pixels, stride, bedtime, 10, UI_WARNING_SOFT);
    draw_rect_outline(pixels, stride, bedtime, 10, 1, UI_WARNING);
    draw_text(pixels, stride, bedtime.x + 14, bedtime.y + 24, ptc_ui_text(PTC_UI_T_BEDTIME), 14, UI_WARNING);
    draw_text(pixels, stride, bedtime.x + 14, bedtime.y + 52, decision.bedtime, 15, UI_INK);
    draw_text(pixels, stride, bedtime.x + 14, bedtime.y + 78,
              ptc_ui_text(PTC_UI_T_AFTER_BEDTIME_USAGE_WILL_BE_RESTRICTED_EVEN_2), 11, UI_MUTED);

    /* 右下栏：规则裁决链条说明 */
    UiRect rule_info = {x_left + col_w + 24, bottom_y, col_w, 94};
    fill_round_rect(pixels, stride, rule_info, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, rule_info, 10, 1, UI_BORDER);
    draw_text(pixels, stride, rule_info.x + 14, rule_info.y + 24, ptc_ui_text(PTC_UI_T_HOW_IS_THE_DAILY_QUOTA_DETERMINED), 14, UI_INK);
    draw_text(pixels, stride, rule_info.x + 14, rule_info.y + 52,
              ptc_ui_text(PTC_UI_T_TODAY_S_ADJUSTMENT_SPECIFIED_DATE_QUOTA_HOLIDAYS), 12, UI_ACCENT);
    draw_text(pixels, stride, rule_info.x + 14, rule_info.y + 78,
              ptc_ui_text(PTC_UI_T_FROM_LEFT_TO_RIGHT_THE_FIRST_APPLICABLE), 11, UI_MUTED);

    home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_B_RETURN), false, true, false);
}

bool draw_support_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
    case PTC_UI_OVERLAY_HOME_DETAILS:
        draw_home_details(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_DAY_DECISION:
        draw_forecast_day_details(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
        draw_shortcut_manager_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SOFTWARE_INFO:
        draw_software_info_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SUPPORT_EVENT:
        draw_support_event_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_ALBUM_MANAGER:
        draw_album_manager_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_THEME:
        draw_theme_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_LANGUAGE:
        draw_language_overlay(pixels, stride, model);
        return true;
    default:
        return false;
    }
}
