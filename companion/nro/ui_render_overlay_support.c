#include "ui_render_internal.h"
#include "../../common/rules/rules.h"

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

static void draw_home_timeline_view(uint32_t *pixels, uint32_t stride,
                                    const PtcUiModel *model, UiRect dialog)
{
    PtcUiTodayDecision decision;
    char total[64], remaining[64], effective[96], line[256];
    int64_t raw_now = ptc_ui_render_now();
    uint16_t minute_of_day = ptc_ui_render_minute_of_day(raw_now);
    bool fresh = ptc_ui_status_is_fresh(model, raw_now);
    int played = fresh && model->played_minutes_available ? model->played_minutes : 0;
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    bool eye_resting = model->eye_care_policy.enabled &&
        strcmp(model->eye_care_phase, "resting") == 0;

    ptc_ui_build_today_decision(model, PTC_UI_PLAN_SAVED, raw_now, &decision);
    ptc_ui_format_home_total_value(model, total, sizeof(total));
    ptc_ui_format_home_remaining(model, raw_now, remaining, sizeof(remaining));
    if (eye_resting) {
        int64_t seconds = (int64_t)model->eye_care_rest_remaining_seconds -
            ptc_ui_status_age_seconds(model, raw_now);
        if (fresh && seconds > 0 && model->eye_care_break_id != 0)
            snprintf(remaining, sizeof(remaining), "%lld:%02lld",
                     (long long)(seconds / 60), (long long)(seconds % 60));
        else snprintf(remaining, sizeof(remaining), "%s",
                      ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_REFRESH));
    }

    if (fresh) {
        snprintf(effective, sizeof(effective), "%s  |  %s",
                 ptc_ui_effective_rule_label(decision.effective.source), total);
    } else {
        snprintf(effective, sizeof(effective), "%s", ptc_ui_text(PTC_UI_T_STATUS_AWAITING_CONFIRMATION_NOT_AVAILABLE_YET));
        snprintf(total, sizeof(total), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    }

    int top_y = dialog.y + 68;
    int full_w = 1064;
    int x_left = dialog.x + 28;

    /* 1. Hero Zone (1064 x 82) */
    UiRect hero = {x_left, top_y, full_w, 82};
    fill_round_rect_gradient(pixels, stride, hero, 12, UI_ACCENT_SOFT,
                             UI_RGB(ui_darken(UI_BLENDED(accent_soft), 5)));
    draw_rect_outline(pixels, stride, hero, 12, 1, UI_ACCENT);

    draw_text(pixels, stride, hero.x + 20, hero.y + 20,
               eye_resting ? ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING) :
               ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY), 13, UI_MUTED);
    char fitted_remaining[64];
    fit_text(fitted_remaining, sizeof(fitted_remaining), remaining, 30, 170);
    draw_text(pixels, stride, hero.x + 20, hero.y + 62, fitted_remaining, 30,
              fresh ? UI_ACCENT : UI_MUTED);

    draw_text(pixels, stride, hero.x + 210, hero.y + 20, ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE), 13, UI_MUTED);
    draw_text(pixels, stride, hero.x + 210, hero.y + 56, total, 19, UI_INK);

    draw_text(pixels, stride, hero.x + 360, hero.y + 20, ptc_ui_text(PTC_UI_T_USED_QUOTA_EST), 13, UI_MUTED);
    if (fresh && model->played_minutes_available && !eye_resting)
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_D_MIN_2), played);
    else snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    draw_text(pixels, stride, hero.x + 360, hero.y + 56, line, 19, UI_INK);

    /* 额度进度条 */
    int bar_pw = 150;
    UiRect pbar_rect = {hero.x + 480, hero.y + 40, bar_pw, 12};
    fill_round_rect(pixels, stride, pbar_rect, 6, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, pbar_rect, 6, 1, UI_BORDER);
    if (decision.effective.rule.mode == PTC_RULE_MODE_LIMIT && decision.effective.rule.minutes > 0) {
        int fill_w = (played * bar_pw) / decision.effective.rule.minutes;
        if (fill_w > bar_pw) fill_w = bar_pw;
        if (fill_w < 0) fill_w = 0;
        if (fill_w > 0) {
            UiRect fill_rect = {pbar_rect.x, pbar_rect.y, fill_w, pbar_rect.height};
            fill_round_rect(pixels, stride, fill_rect, 6, (played > decision.effective.rule.minutes) ? UI_DANGER : UI_SUCCESS);
        }
        int pct = (played * 100) / decision.effective.rule.minutes;
        snprintf(line, sizeof(line), "%d%%", pct);
        draw_text(pixels, stride, pbar_rect.x + bar_pw + 10, pbar_rect.y + 11, line, 12, UI_MUTED);
    } else {
        draw_text(pixels, stride, pbar_rect.x + 10, pbar_rect.y + 11, ptc_ui_text(PTC_UI_T_TIMELINE_UNLIMITED_MODE), 11, UI_MUTED);
    }

    /* 右侧主生效规则徽章卡片 */
    UiRect active_badge = {hero.x + 710, hero.y + 12, hero.width - 724, 58};
    uint32_t badge_bg = !fresh ? UI_WARNING_SOFT :
        (bedtime_enforcing || eye_resting ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
    uint32_t badge_border = !fresh ? UI_WARNING :
        (bedtime_enforcing || eye_resting ? UI_DANGER : UI_SUCCESS);
    fill_round_rect(pixels, stride, active_badge, 8, badge_bg);
    draw_rect_outline(pixels, stride, active_badge, 8, 1, badge_border);
    const char *badge_title = !fresh ? ptc_ui_text(PTC_UI_T_RULES_PENDING_CONFIRMATION) :
        (bedtime_enforcing ? ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_UNDER_BEDTIME_LIMIT) :
         (eye_resting ? ptc_ui_text(PTC_UI_T_EYE_CARE_RESTING) : ptc_ui_text(PTC_UI_T_RULES_ADOPTED_TODAY)));
    draw_text(pixels, stride, active_badge.x + 12, active_badge.y + 18, badge_title, 12, badge_border);
    draw_text(pixels, stride, active_badge.x + 12, active_badge.y + 36, effective, 13, UI_INK);
    const char *final_desc = bedtime_enforcing
        ? ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_HAS_BEEN_DETERMINED_CURRENT) :
        (eye_resting ? ptc_ui_text(PTC_UI_T_EYE_CARE_GUIDE) : decision.final_reason);
    draw_wrapped_text(pixels, stride, active_badge.x + 12, active_badge.y + 50, final_desc,
                      11, active_badge.width - 24, 14, 1, UI_MUTED);

    /* 2. 24 小时全景作息时间轴 (1064 x 168) */
    UiRect tl_card = {x_left, top_y + 92, full_w, 168};
    draw_card_shadow(pixels, stride, tl_card, 16);
    fill_round_rect(pixels, stride, tl_card, 12, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, tl_card, 12, 1, UI_RGB(UI_BLENDED(border_control)));

    draw_text(pixels, stride, tl_card.x + 18, tl_card.y + 22,
              ptc_ui_text(PTC_UI_T_TIMELINE_TITLE), 16, UI_INK);
    draw_text(pixels, stride, tl_card.x + tl_card.width - 240, tl_card.y + 22,
              ptc_ui_text(PTC_UI_T_TIMELINE_SUBTITLE), 12, UI_MUTED);

    /* 时间轴主体轨道 */
    int track_x = tl_card.x + 36;
    int track_w = tl_card.width - 72;
    int track_y = tl_card.y + 60;
    int track_h = 24;
    UiRect track = {track_x, track_y, track_w, track_h};
    fill_round_rect(pixels, stride, track, 6, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, track, 6, 1, UI_BORDER);

    /* A. 就寝封锁带映射 (Bedtime Zone) */
    if (model->bedtime_policy.enabled) {
        uint16_t b_start = 0, b_end = 0;
        bool b_active = false;
        PtcRules eval_rules;
        memset(&eval_rules, 0, sizeof(eval_rules));
        eval_rules.bedtime = model->bedtime_policy;
        PtcEffectiveBedtime eff_today = ptc_bedtime_resolve_start_day(
            &eval_rules, model->day_index, ptc_weekday_from_day_index(model->day_index));
        if (eff_today.window.enabled) {
            b_start = eff_today.window.start_minute;
            b_end = eff_today.window.end_minute;
            b_active = true;
        }

        if (b_active) {
            if (b_start > b_end) {
                int bx1 = track_x + (b_start * track_w) / 1440;
                int bw1 = track_x + track_w - bx1;
                UiRect b_rect1 = {bx1, track_y, bw1, track_h};
                fill_round_rect(pixels, stride, b_rect1, 4, UI_DANGER_SOFT);
                draw_rect_outline(pixels, stride, b_rect1, 4, 1, UI_DANGER);

                int bw2 = (b_end * track_w) / 1440;
                UiRect b_rect2 = {track_x, track_y, bw2, track_h};
                fill_round_rect(pixels, stride, b_rect2, 4, UI_DANGER_SOFT);
                draw_rect_outline(pixels, stride, b_rect2, 4, 1, UI_DANGER);
            } else {
                int bx = track_x + (b_start * track_w) / 1440;
                int bw = ((b_end - b_start) * track_w) / 1440;
                UiRect b_rect = {bx, track_y, bw, track_h};
                fill_round_rect(pixels, stride, b_rect, 4, UI_DANGER_SOFT);
                draw_rect_outline(pixels, stride, b_rect, 4, 1, UI_DANGER);
            }
        }
    }

    /* B. 过去已用时间色块 */
    if (played > 0) {
        int play_w = (played * track_w) / 1440;
        if (play_w < 4) play_w = 4;
        int cur_x = track_x + (minute_of_day * track_w) / 1440;
        int start_x = cur_x - play_w;
        if (start_x < track_x) start_x = track_x;
        int actual_w = cur_x - start_x;
        if (actual_w > 0) {
            UiRect played_rect = {start_x, track_y + 2, actual_w, track_h - 4};
            fill_round_rect(pixels, stride, played_rect, 4, UI_ACCENT);
        }
    }

    /* C. 当前时刻游标 (Now Cursor) */
    int cursor_x = track_x + (minute_of_day * track_w) / 1440;
    if (cursor_x < track_x) cursor_x = track_x;
    if (cursor_x > track_x + track_w) cursor_x = track_x + track_w;

    draw_line(pixels, stride, cursor_x, track_y - 12, cursor_x, track_y + track_h + 10, 2, UI_ACCENT);
    char now_str[32];
    snprintf(now_str, sizeof(now_str), "v %02u:%02u", minute_of_day / 60, minute_of_day % 60);
    draw_text_center(pixels, stride, (UiRect){cursor_x - 36, track_y - 22, 72, 16}, now_str, 12, UI_ACCENT);

    /* D. 下一次护眼预计节点 */
    if (model->eye_care_policy.enabled && model->draft_eye_care_policy.play_minutes > 0) {
        int next_eye_m = minute_of_day + (model->draft_eye_care_policy.play_minutes - model->eye_care_used_minutes);
        if (next_eye_m < 1440) {
            int eye_x = track_x + (next_eye_m * track_w) / 1440;
            draw_line(pixels, stride, eye_x, track_y + 4, eye_x, track_y + track_h - 4, 3, UI_WARNING);
            char eye_tip[32];
            snprintf(eye_tip, sizeof(eye_tip), "* %02u:%02u", next_eye_m / 60, next_eye_m % 60);
            draw_text_center(pixels, stride, (UiRect){eye_x - 45, track_y + track_h + 14, 90, 16}, eye_tip, 11, UI_WARNING);
        }
    }

    /* E. 刻度线与时刻标注 */
    static const int TICKS[] = {0, 240, 480, 720, 960, 1200, 1440};
    for (int t = 0; t < 7; ++t) {
        int tick_x = track_x + (TICKS[t] * track_w) / 1440;
        if (tick_x > track_x + track_w) tick_x = track_x + track_w;
        draw_line(pixels, stride, tick_x, track_y + track_h, tick_x, track_y + track_h + 4, 1, UI_MUTED);
        char tick_lbl[16];
        snprintf(tick_lbl, sizeof(tick_lbl), "%02u:00", TICKS[t] / 60);
        draw_text_center(pixels, stride, (UiRect){tick_x - 24, track_y + track_h + 4, 48, 14}, tick_lbl, 10, UI_MUTED);
    }

    /* F. 底部图例说明栏 */
    int leg_y = tl_card.y + tl_card.height - 24;
    int lx = tl_card.x + 24;
    fill_round_rect(pixels, stride, (UiRect){lx, leg_y + 2, 10, 10}, 2, UI_ACCENT);
    draw_text(pixels, stride, lx + 16, leg_y + 11, ptc_ui_text(PTC_UI_T_TIMELINE_LEGEND_USED), 11, UI_MUTED);
    lx += 110;

    fill_round_rect(pixels, stride, (UiRect){lx, leg_y + 2, 10, 10}, 2, UI_DANGER_SOFT);
    draw_rect_outline(pixels, stride, (UiRect){lx, leg_y + 2, 10, 10}, 2, 1, UI_DANGER);
    draw_text(pixels, stride, lx + 16, leg_y + 11, ptc_ui_text(PTC_UI_T_TIMELINE_LEGEND_BEDTIME), 11, UI_MUTED);
    lx += 110;

    draw_text(pixels, stride, lx, leg_y + 11, "v", 11, UI_ACCENT);
    draw_text(pixels, stride, lx + 14, leg_y + 11, ptc_ui_text(PTC_UI_T_TIMELINE_LEGEND_NOW), 11, UI_MUTED);
    lx += 100;

    draw_text(pixels, stride, lx, leg_y + 11, "*", 11, UI_WARNING);
    draw_text(pixels, stride, lx + 14, leg_y + 11, ptc_ui_text(PTC_UI_T_TIMELINE_LEGEND_EYE), 11, UI_MUTED);

    /* 3. Bento Core Grid: 4 张核心卡片 (1064 x 172) */
    int grid_y = top_y + 268;
    int card_w = (full_w - 36) / 4;
    int card_h = 172;

    /* 卡片 1: 护眼健康管控 */
    UiRect c1 = {x_left, grid_y, card_w, card_h};
    fill_round_rect(pixels, stride, c1, 12, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, c1, 12, 1, UI_RGB(UI_BLENDED(border_control)));
    draw_text(pixels, stride, c1.x + 14, c1.y + 22, ptc_ui_text(PTC_UI_T_BENTO_EYE_TITLE), 15, UI_INK);
    {
        bool resting = (strcmp(model->eye_care_phase, "resting") == 0);
        bool enabled = model->eye_care_policy.enabled;
        UiRect pill = {c1.x + c1.width - 82, c1.y + 8, 70, 20};
        uint32_t pbg = resting ? UI_DANGER_SOFT : (enabled ? UI_SUCCESS_SOFT : UI_RAISED);
        uint32_t pfg = resting ? UI_DANGER : (enabled ? UI_SUCCESS : UI_MUTED);
        fill_round_rect(pixels, stride, pill, 4, pbg);
        draw_text_center(pixels, stride, pill,
                         resting ? ptc_ui_text(PTC_UI_T_EYE_CARE_BADGE_RESTING) :
                         (enabled ? ptc_ui_text(PTC_UI_T_ACTIVE) : ptc_ui_text(PTC_UI_T_OFF)), 11, pfg);

        char c1_buf[160];
        snprintf(c1_buf, sizeof(c1_buf), ptc_ui_text(PTC_UI_T_BENTO_CYCLE_FMT),
                 model->draft_eye_care_policy.play_minutes, model->draft_eye_care_policy.rest_minutes);
        draw_text(pixels, stride, c1.x + 14, c1.y + 54, c1_buf, 13, UI_INK);

        ptc_ui_format_eye_care_cycle(model, raw_now, c1_buf, sizeof(c1_buf));
        draw_text(pixels, stride, c1.x + 14, c1.y + 82, c1_buf, 12, pfg);

        draw_wrapped_text(pixels, stride, c1.x + 14, c1.y + 116,
                          ptc_ui_text(PTC_UI_T_BENTO_EYE_FOOTNOTE),
                          11, c1.width - 24, 15, 3, UI_MUTED);
    }

    /* 卡片 2: 晚间就寝时段 */
    UiRect c2 = {x_left + card_w + 12, grid_y, card_w, card_h};
    fill_round_rect(pixels, stride, c2, 12, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, c2, 12, 1, UI_RGB(UI_BLENDED(border_control)));
    draw_text(pixels, stride, c2.x + 14, c2.y + 22, ptc_ui_text(PTC_UI_T_BENTO_BEDTIME_TITLE), 15, UI_INK);
    {
        bool b_en = model->bedtime_policy.enabled;
        UiRect pill = {c2.x + c2.width - 82, c2.y + 8, 70, 20};
        uint32_t pbg = bedtime_enforcing ? UI_DANGER_SOFT : (model->bedtime_skipped ? UI_ACCENT_SOFT : (b_en ? UI_SUCCESS_SOFT : UI_RAISED));
        uint32_t pfg = bedtime_enforcing ? UI_DANGER : (model->bedtime_skipped ? UI_ACCENT : (b_en ? UI_SUCCESS : UI_MUTED));
        fill_round_rect(pixels, stride, pill, 4, pbg);
        draw_text_center(pixels, stride, pill,
                         bedtime_enforcing ? ptc_ui_text(PTC_UI_T_RESTRICTED_USE) :
                         (model->bedtime_skipped ? ptc_ui_text(PTC_UI_T_BEDTIME_STATUS_SKIPPED) :
                         (b_en ? ptc_ui_text(PTC_UI_T_ACTIVE) : ptc_ui_text(PTC_UI_T_OFF))), 11, pfg);

        char c2_buf[160];
        snprintf(c2_buf, sizeof(c2_buf), "%s", decision.bedtime);
        draw_text(pixels, stride, c2.x + 14, c2.y + 54, c2_buf, 13, UI_INK);

        if (bedtime_enforcing) {
            snprintf(c2_buf, sizeof(c2_buf), "%s", ptc_ui_text(PTC_UI_T_BENTO_BEDTIME_ENFORCING));
        } else {
            snprintf(c2_buf, sizeof(c2_buf), "%s", ptc_ui_text(PTC_UI_T_BENTO_BEDTIME_NORMAL));
        }
        draw_text(pixels, stride, c2.x + 14, c2.y + 82, c2_buf, 12, pfg);

        draw_wrapped_text(pixels, stride, c2.x + 14, c2.y + 116,
                          ptc_ui_text(PTC_UI_T_BENTO_BEDTIME_FOOTNOTE),
                          11, c2.width - 24, 15, 3, UI_MUTED);
    }

    /* 卡片 3: 临时自主与缓冲 */
    UiRect c3 = {x_left + (card_w + 12) * 2, grid_y, card_w, card_h};
    fill_round_rect(pixels, stride, c3, 12, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, c3, 12, 1, UI_RGB(UI_BLENDED(border_control)));
    draw_text(pixels, stride, c3.x + 14, c3.y + 22, ptc_ui_text(PTC_UI_T_BENTO_BUFFER_TITLE), 15, UI_INK);
    {
        char c3_buf[160];
        snprintf(c3_buf, sizeof(c3_buf), ptc_ui_text(PTC_UI_T_BENTO_BUFFER_VAL_FMT), decision.autonomy);
        draw_text(pixels, stride, c3.x + 14, c3.y + 54, c3_buf, 13, UI_INK);

        if (model->grant_minutes > 0) {
            snprintf(c3_buf, sizeof(c3_buf), "%s", ptc_ui_text(PTC_UI_T_BENTO_TOKEN_ACTIVE));
        } else {
            snprintf(c3_buf, sizeof(c3_buf), "%s", ptc_ui_text(PTC_UI_T_BENTO_TOKEN_NONE));
        }
        draw_text(pixels, stride, c3.x + 14, c3.y + 82, c3_buf, 12, model->grant_minutes > 0 ? UI_SUCCESS : UI_MUTED);

        draw_wrapped_text(pixels, stride, c3.x + 14, c3.y + 116,
                          ptc_ui_text(PTC_UI_T_BENTO_BUFFER_FOOTNOTE),
                          11, c3.width - 24, 15, 3, UI_MUTED);
    }

    /* 卡片 4: 规则决策来源 */
    UiRect c4 = {x_left + (card_w + 12) * 3, grid_y, card_w, card_h};
    fill_round_rect(pixels, stride, c4, 12, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, c4, 12, 1, UI_RGB(UI_BLENDED(border_control)));
    draw_text(pixels, stride, c4.x + 14, c4.y + 22, ptc_ui_text(PTC_UI_T_BENTO_DECISION_TITLE), 15, UI_INK);
    {
        char c4_buf[160];
        snprintf(c4_buf, sizeof(c4_buf), ptc_ui_text(PTC_UI_T_BENTO_DECISION_SRC_FMT), ptc_ui_effective_rule_label(decision.effective.source));
        draw_text(pixels, stride, c4.x + 14, c4.y + 54, c4_buf, 13, UI_INK);

        snprintf(c4_buf, sizeof(c4_buf), ptc_ui_text(PTC_UI_T_BENTO_DECISION_QUOTA_FMT), total);
        draw_text(pixels, stride, c4.x + 14, c4.y + 82, c4_buf, 12, UI_ACCENT);

        draw_wrapped_text(pixels, stride, c4.x + 14, c4.y + 116,
                          ptc_ui_text(PTC_UI_T_BENTO_DECISION_FOOTNOTE),
                          11, c4.width - 24, 15, 3, UI_MUTED);
    }
}

static void draw_home_metrics_view(uint32_t *pixels, uint32_t stride,
                                   const PtcUiModel *model, UiRect dialog)
{
    PtcUiTodayDecision decision;
    char total[64], timer[64], today[64], line[256];
    int64_t raw_now = ptc_ui_render_now();
    bool fresh = ptc_ui_status_is_fresh(model, raw_now);
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    const char *notice = ptc_ui_runtime_notice_summary(model);
    const char *runtime = !model->status_loaded ? ptc_ui_text(PTC_UI_T_WAITING_FOR_REFRESH) :
        model->disable_flag_present ? ptc_ui_text(PTC_UI_T_CONTROL_IS_DISABLED) : model->recovery_active ? ptc_ui_text(PTC_UI_T_RECOVERING) :
        model->apply_pending_confirmation ? ptc_ui_text(PTC_UI_T_WAITING_FOR_EFFECT) : !fresh ? ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED) :
        strcmp(model->setup_phase, "active") == 0 ? ptc_ui_text(PTC_UI_T_RUNNING_NORMALLY) : ptc_ui_text(PTC_UI_T_REQUIRES_PARENT_CONFIRMATION);

    ptc_ui_build_today_decision(model, PTC_UI_PLAN_SAVED, raw_now, &decision);
    ptc_ui_format_home_total_value(model, total, sizeof(total));
    ptc_ui_format_timer_status(model, timer, sizeof(timer));
    ptc_ui_format_today_mode(model, today, sizeof(today));

    int top_y = dialog.y + 68;
    int full_w = 1064;
    int col_w = 520;
    int x_left = dialog.x + 28;
    int x_right = dialog.x + 572;

    /* 1. 额度决策流水线 (1064 x 164) */
    draw_waterfall_pipeline(pixels, stride, x_left, top_y, full_w, &decision, bedtime_enforcing);

    /* 2. 底部双栏 (520 + 520) */
    int bottom_y = top_y + 172;

    /* 左下栏: 近 7 日用量趋势与健康分析 */
    UiRect trend_card = {x_left, bottom_y, col_w, 270};
    draw_card_shadow(pixels, stride, trend_card, 16);
    fill_round_rect(pixels, stride, trend_card, 12, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, trend_card, 12, 1, UI_RGB(UI_BLENDED(border_control)));

    draw_text(pixels, stride, trend_card.x + 18, trend_card.y + 22,
              ptc_ui_text(PTC_UI_T_WEEKLY_TREND_TITLE), 16, UI_INK);

    /* 绘制 7 日柱状图 */
    static const PtcUiTextId DAYS_KEYS[] = {
        PTC_UI_T_DAY_MON, PTC_UI_T_DAY_TUE, PTC_UI_T_DAY_WED,
        PTC_UI_T_DAY_THU, PTC_UI_T_DAY_FRI, PTC_UI_T_DAY_SAT, PTC_UI_T_DAY_SUN
    };
    int bar_area_x = trend_card.x + 24;
    int bar_area_y = trend_card.y + 46;
    int bar_area_w = trend_card.width - 48;
    int bar_area_h = 110;

    int col_step = bar_area_w / 7;
    int single_bar_w = 32;
    uint16_t avg_min = model->usage_summary_available && model->usage_known_days_7 > 0
        ? (uint16_t)(model->usage_consumed_minutes_7 / model->usage_known_days_7) : 45;
    if (avg_min == 0) avg_min = 45;

    for (int d = 0; d < 7; ++d) {
        int bx = bar_area_x + d * col_step + (col_step - single_bar_w) / 2;
        int day_m = (int)model->draft_week[d].minutes;
        if (model->draft_week[d].mode == PTC_RULE_MODE_UNLIMITED) day_m = 120;
        else if (model->draft_week[d].mode == PTC_RULE_MODE_LIMIT && model->draft_week[d].minutes == 0) day_m = 0;
        if (day_m > 180) day_m = 180;
        int bh = (day_m * (bar_area_h - 26)) / 180;
        if (bh < 4) bh = 4;
        int by = bar_area_y + bar_area_h - bh - 20;

        UiRect bar = {bx, by, single_bar_w, bh};
        bool is_today_col = (ptc_weekday_from_day_index(model->day_index) == d);
        fill_round_rect(pixels, stride, bar, 4, is_today_col ? UI_ACCENT : UI_RAISED);
        if (is_today_col) draw_rect_outline(pixels, stride, bar, 4, 1, UI_ACCENT);

        snprintf(line, sizeof(line), "%dm", day_m);
        draw_text_center(pixels, stride, (UiRect){bx - 8, by - 16, single_bar_w + 16, 14}, line, 10, is_today_col ? UI_ACCENT : UI_MUTED);

        const char *dname = ptc_ui_text(DAYS_KEYS[d]);
        draw_text_center(pixels, stride, (UiRect){bx, bar_area_y + bar_area_h - 14, single_bar_w, 14}, dname, 11, is_today_col ? UI_ACCENT : UI_INK);
    }
    draw_line(pixels, stride, bar_area_x, bar_area_y + bar_area_h - 18, bar_area_x + bar_area_w, bar_area_y + bar_area_h - 18, 1, UI_BORDER);

    int stat_y = trend_card.y + 172;
    draw_text(pixels, stride, trend_card.x + 18, stat_y + 16,
              ptc_ui_text(PTC_UI_T_WEEKLY_TOTAL_CONSUMED), 13, UI_MUTED);
    if (model->usage_summary_available && model->usage_known_days_7 > 0)
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_MIN), model->usage_consumed_minutes_7);
    else snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
    draw_text(pixels, stride, trend_card.x + 110, stat_y + 16, line, 14, UI_INK);

    draw_text(pixels, stride, trend_card.x + 260, stat_y + 16,
              ptc_ui_text(PTC_UI_T_WEEKLY_DAILY_AVG), 13, UI_MUTED);
    snprintf(line, sizeof(line), "%u %s", avg_min, ptc_ui_text(PTC_UI_T_MIN));
    draw_text(pixels, stride, trend_card.x + 346, stat_y + 16, line, 14, UI_INK);

    draw_text(pixels, stride, trend_card.x + 18, stat_y + 46,
              ptc_ui_text(PTC_UI_T_WEEKLY_HEALTH_ASSESSMENT), 12, UI_SUCCESS);
    draw_text(pixels, stride, trend_card.x + 18, stat_y + 70,
              ptc_ui_text(PTC_UI_T_WEEKLY_FOOTNOTE), 11, UI_MUTED);

    /* 右下栏: 系统级守护与 IPC 运行体检 */
    UiRect sys_card = {x_right, bottom_y, col_w, 270};
    draw_card_shadow(pixels, stride, sys_card, 16);
    fill_round_rect(pixels, stride, sys_card, 12, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, sys_card, 12, 1, UI_RGB(UI_BLENDED(border_control)));

    draw_text(pixels, stride, sys_card.x + 18, sys_card.y + 22,
              ptc_ui_text(PTC_UI_T_SYS_HEALTH_TITLE), 16, UI_INK);

    int sy = sys_card.y + 42;
    int sh = 46;

    /* (1) 官方家长控制服务 */
    UiRect row1 = {sys_card.x + 14, sy, sys_card.width - 28, sh};
    fill_round_rect(pixels, stride, row1, 8, UI_RGB(UI_BLENDED(surface_raised)));
    draw_text(pixels, stride, row1.x + 12, row1.y + 18, ptc_ui_text(PTC_UI_T_SYS_PCTL_TITLE), 12, UI_MUTED);
    draw_text(pixels, stride, row1.x + 12, row1.y + 36, timer, 13, fresh ? UI_INK : UI_WARNING);
    UiRect p1 = {row1.x + row1.width - 82, row1.y + 13, 70, 20};
    fill_round_rect(pixels, stride, p1, 4, fresh ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
    draw_text_center(pixels, stride, p1, fresh ? ptc_ui_text(PTC_UI_T_SYS_PCTL_SYNCED) : ptc_ui_text(PTC_UI_T_SYS_PCTL_SYNCING), 11, fresh ? UI_SUCCESS : UI_WARNING);

    /* (2) PlayWise 守护进程 */
    UiRect row2 = {sys_card.x + 14, sy + sh + 8, sys_card.width - 28, sh};
    fill_round_rect(pixels, stride, row2, 8, UI_RGB(UI_BLENDED(surface_raised)));
    draw_text(pixels, stride, row2.x + 12, row2.y + 18, ptc_ui_text(PTC_UI_T_SYS_SYSMODULE_TITLE), 12, UI_MUTED);
    draw_text(pixels, stride, row2.x + 12, row2.y + 36, runtime, 13, notice[0] || !fresh ? UI_WARNING : UI_SUCCESS);
    UiRect p2 = {row2.x + row2.width - 82, row2.y + 13, 70, 20};
    fill_round_rect(pixels, stride, p2, 4, (notice[0] || !fresh) ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
    draw_text_center(pixels, stride, p2, (notice[0] || !fresh) ? ptc_ui_text(PTC_UI_T_SYS_WARN) : ptc_ui_text(PTC_UI_T_SYS_ACTIVE), 11, (notice[0] || !fresh) ? UI_WARNING : UI_SUCCESS);

    /* (3) 应急禁用标志与安全状态 */
    UiRect row3 = {sys_card.x + 14, sy + (sh + 8) * 2, sys_card.width - 28, sh};
    fill_round_rect(pixels, stride, row3, 8, UI_RGB(UI_BLENDED(surface_raised)));
    draw_text(pixels, stride, row3.x + 12, row3.y + 18, ptc_ui_text(PTC_UI_T_SYS_DISABLE_FLAG_TITLE), 12, UI_MUTED);
    draw_text(pixels, stride, row3.x + 12, row3.y + 36, model->disable_flag_present ? ptc_ui_text(PTC_UI_T_SYS_FLAG_ACTIVE) : ptc_ui_text(PTC_UI_T_SYS_FLAG_INACTIVE), 13, model->disable_flag_present ? UI_DANGER : UI_INK);
    UiRect p3 = {row3.x + row3.width - 82, row3.y + 13, 70, 20};
    fill_round_rect(pixels, stride, p3, 4, model->disable_flag_present ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
    draw_text_center(pixels, stride, p3, model->disable_flag_present ? ptc_ui_text(PTC_UI_T_SYS_FLAG_BADGE_DISABLED) : ptc_ui_text(PTC_UI_T_SYS_FLAG_BADGE_SECURE), 11, model->disable_flag_present ? UI_DANGER : UI_SUCCESS);

    /* (4) 最近操作审计 */
    UiRect row4 = {sys_card.x + 14, sy + (sh + 8) * 3, sys_card.width - 28, sh};
    fill_round_rect(pixels, stride, row4, 8, UI_RGB(UI_BLENDED(surface_raised)));
    draw_text(pixels, stride, row4.x + 12, row4.y + 18, ptc_ui_text(PTC_UI_T_SYS_AUDIT_TITLE), 12, UI_MUTED);
    char cmd_buf[96], tr_buf[96];
    snprintf(line, sizeof(line), "%s / %s",
             ptc_ui_localize(model->command_name, cmd_buf, sizeof(cmd_buf)),
             ptc_ui_localize(model->transport_label, tr_buf, sizeof(tr_buf)));
    draw_text(pixels, stride, row4.x + 12, row4.y + 36, line, 12, UI_INK);
    UiRect p4 = {row4.x + row4.width - 82, row4.y + 13, 70, 20};
    bool is_err = (strcmp(model->result_status, "error") == 0);
    fill_round_rect(pixels, stride, p4, 4, is_err ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
    draw_text_center(pixels, stride, p4, is_err ? ptc_ui_text(PTC_UI_T_SYS_AUDIT_FAIL) : ptc_ui_text(PTC_UI_T_SYS_AUDIT_PASS), 11, is_err ? UI_DANGER : UI_SUCCESS);
}

static void draw_home_details(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char age[64];
    PtcUiModel shell_model = *model;
    shell_model.overlay_title[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 1120, 640);
    char fitted_title[128];
    fit_text(fitted_title, sizeof(fitted_title), model->overlay_title, 21, 250);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 54, fitted_title, 21, UI_INK);
    format_status_age(model, age, sizeof(age));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 880, dialog.y + 26, 200, 30}, age, 15, status_age_color(model));

    /* 顶部 Tab 导航栏 */
    for (int t = 0; t < 2; ++t) {
        UiRect tab = to_uirect(ptc_ui_home_details_tab_rect(t));
        bool active = (model->home_details_page == t);
        fill_round_rect(pixels, stride, tab, 8, active ? UI_ACCENT : UI_RAISED);
        if (active) draw_rect_outline(pixels, stride, tab, 8, 1, UI_ACCENT);
        else draw_rect_outline(pixels, stride, tab, 8, 1, UI_BORDER);
        const char *tab_text = (t == 0)
            ? ptc_ui_text(PTC_UI_T_HOME_DETAILS_TAB_TIMELINE)
            : ptc_ui_text(PTC_UI_T_HOME_DETAILS_TAB_METRICS);
        draw_text_center(pixels, stride, tab, tab_text, 14, active ? UI_ON_ACCENT : UI_MUTED);
    }

    if (model->home_details_page == 0) {
        draw_home_timeline_view(pixels, stride, model, dialog);
    } else {
        draw_home_metrics_view(pixels, stride, model, dialog);
    }
    if (model->dock_available) {
        char dock_line[256];
        bool is_tv = strcmp(model->operation_mode, "docked") == 0;
        bool is_undocked = strcmp(model->operation_mode, "undocked") == 0;
        const char *mode_str = is_tv ? ptc_ui_text(PTC_UI_T_DOCK_TV) :
            (is_undocked ? ptc_ui_text(PTC_UI_T_DOCK_HANDHELD) : ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));

        if (model->dock_waived_today) {
            snprintf(dock_line, sizeof(dock_line), "%s / %s", mode_str, ptc_ui_text(PTC_UI_T_DOCK_CARD_WAIVED));
        } else if (model->dock_restriction_active) {
            snprintf(dock_line, sizeof(dock_line), "%s / %s", mode_str, ptc_ui_text(PTC_UI_T_DOCK_BLOCKED_BANNER));
        } else if (!(model->dock_policy.force_docked || model->dock_policy.undocked_limit_enabled)) {
            snprintf(dock_line, sizeof(dock_line), "%s / %s", mode_str, ptc_ui_text(PTC_UI_T_DOCK_CARD_OFF));
        } else if (model->dock_policy.force_docked) {
            snprintf(dock_line, sizeof(dock_line), "%s / %s", mode_str, ptc_ui_text(PTC_UI_T_DOCK_CARD_FORCE));
        } else {
            ptc_ui_format_dock_usage(model, ptc_ui_render_now(), dock_line, sizeof(dock_line));
        }
        draw_wrapped_text(pixels, stride, dialog.x + 28, dialog.y + 530,
            dock_line, 14, 1064, 18, 1, UI_ACCENT);
        if (ptc_ui_status_is_fresh(model, ptc_ui_render_now()) &&
            (model->dock_restriction_active || model->dock_waived_today))
            draw_wrapped_text(pixels, stride, dialog.x + 28, dialog.y + 554,
                ptc_ui_text(model->dock_waived_today ? PTC_UI_T_DOCK_WAIVED : PTC_UI_T_DOCK_CONNECT),
                12, 1064, 18, 1, model->dock_waived_today ? UI_SUCCESS : UI_DANGER);
    }
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
    PtcCalendarDayType day_type = ptc_holiday_calendar_classify_in(model->calendar, target_day_index, &covered);
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
    UiRect bedtime = {x_left, bottom_y, col_w, 180};
    fill_round_rect(pixels, stride, bedtime, 10, UI_WARNING_SOFT);
    draw_rect_outline(pixels, stride, bedtime, 10, 1, UI_WARNING);
    draw_text(pixels, stride, bedtime.x + 16, bedtime.y + 26, ptc_ui_text(PTC_UI_T_BEDTIME_SCHEDULE), 15, UI_WARNING);
    draw_text(pixels, stride, bedtime.x + 16, bedtime.y + 62, decision.bedtime, 17, UI_INK);
    draw_wrapped_text(pixels, stride, bedtime.x + 16, bedtime.y + 98,
              ptc_ui_text(PTC_UI_T_AFTER_BEDTIME_USAGE_WILL_BE_RESTRICTED_EVEN_2), 12, bedtime.width - 32, 22, 3, UI_MUTED);

    /* 右下栏：健康管控与裁决逻辑说明 */
    UiRect rule_info = {x_left + col_w + 24, bottom_y, col_w, 180};
    fill_round_rect(pixels, stride, rule_info, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, rule_info, 10, 1, UI_BORDER);
    draw_text(pixels, stride, rule_info.x + 16, rule_info.y + 26, ptc_ui_text(PTC_UI_T_HEALTH_AND_DECISION_RULES), 15, UI_INK);

    char eye_txt[128];
    if (model->eye_care_policy.enabled) {
        snprintf(eye_txt, sizeof(eye_txt), ptc_ui_text(PTC_UI_T_EYE_CARE_POLICY_FORMAT),
                 (unsigned)model->eye_care_policy.play_session_minutes,
                 (unsigned)model->eye_care_policy.rest_duration_minutes);
    } else {
        snprintf(eye_txt, sizeof(eye_txt), "%s", ptc_ui_text(PTC_UI_T_EYE_CARE_POLICY_OFF));
    }
    draw_text(pixels, stride, rule_info.x + 16, rule_info.y + 56, eye_txt, 13, model->eye_care_policy.enabled ? UI_INK : UI_MUTED);

    char dock_txt[128];
    if (model->dock_policy.force_docked) {
        snprintf(dock_txt, sizeof(dock_txt), "%s", ptc_ui_text(PTC_UI_T_DOCK_POLICY_FORCE_FORMAT));
    } else if (model->dock_policy.undocked_limit_enabled) {
        snprintf(dock_txt, sizeof(dock_txt), ptc_ui_text(PTC_UI_T_DOCK_POLICY_LIMIT_FORMAT),
                 (unsigned)model->dock_policy.undocked_daily_minutes);
    } else {
        snprintf(dock_txt, sizeof(dock_txt), "%s", ptc_ui_text(PTC_UI_T_DOCK_POLICY_OFF));
    }
    draw_text(pixels, stride, rule_info.x + 16, rule_info.y + 82, dock_txt, 13, (model->dock_policy.force_docked || model->dock_policy.undocked_limit_enabled) ? UI_INK : UI_MUTED);

    draw_text(pixels, stride, rule_info.x + 16, rule_info.y + 114, ptc_ui_text(PTC_UI_T_QUOTA_DECISION_PRIORITY), 12, UI_MUTED);
    draw_text(pixels, stride, rule_info.x + 16, rule_info.y + 138, ptc_ui_text(PTC_UI_T_QUOTA_DECISION_CHAIN), 13, UI_ACCENT);
    draw_text(pixels, stride, rule_info.x + 16, rule_info.y + 162, ptc_ui_text(PTC_UI_T_FROM_LEFT_TO_RIGHT_THE_FIRST_APPLICABLE), 11, UI_MUTED);

    home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_B_RETURN), false, true, false);
}

static void draw_setup_pctl_help(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const PtcUiTextId steps[] = {
        PTC_UI_T_SETUP_PCTL_HELP_STEP_1, PTC_UI_T_SETUP_PCTL_HELP_STEP_2,
        PTC_UI_T_SETUP_PCTL_HELP_STEP_3, PTC_UI_T_SETUP_PCTL_HELP_STEP_4,
        PTC_UI_T_SETUP_PCTL_HELP_STEP_5, PTC_UI_T_SETUP_PCTL_HELP_STEP_6
    };
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 1060, 570);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 91,
        ptc_ui_text(PTC_UI_T_SETUP_PCTL_HELP_INTRO), 18, UI_MUTED);
    for (int i = 0; i < 6; ++i) {
        char number[8];
        snprintf(number, sizeof(number), "%d", i + 1);
        UiRect badge = {dialog.x + 34, dialog.y + 115 + i * 46, 32, 32};
        fill_round_rect(pixels, stride, badge, 8, UI_ACCENT_SOFT);
        draw_text_center(pixels, stride, badge, number, 18, UI_ACCENT);
        draw_wrapped_text(pixels, stride, dialog.x + 82, dialog.y + 138 + i * 46,
            ptc_ui_text(steps[i]), 18, dialog.width - 116, 22, 2, UI_INK);
    }
    draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 432,
        ptc_ui_text(PTC_UI_T_SETUP_PCTL_HELP_RETURN), 17, dialog.width - 68, 24, 2, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay),
        ptc_ui_text(PTC_UI_T_A_B_RETURN), UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_support_guide(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const PtcUiTextId titles[] = {PTC_UI_T_GUIDE_CHECKS, PTC_UI_T_GUIDE_LIMIT,
        PTC_UI_T_GUIDE_CODES, PTC_UI_T_GUIDE_BACKEND};
    static const PtcUiTextId bodies[] = {PTC_UI_T_GUIDE_CHECKS_BODY, PTC_UI_T_GUIDE_LIMIT_BODY,
        PTC_UI_T_GUIDE_CODES_BODY, PTC_UI_T_GUIDE_BACKEND_BODY};
    UiRect dialog;
    int page = model->support_guide_page >= 0 && model->support_guide_page < 4
        ? model->support_guide_page : 0;
    char heading[192];
    draw_dialog_shell(pixels, stride, model, &dialog, 1100, 610);
    snprintf(heading, sizeof(heading), "%d/4  %s", page + 1, ptc_ui_text(titles[page]));
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 104, heading, 23, UI_ACCENT);
    bool known = model->restriction_enabled_available &&
        ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 145,
        ptc_ui_text(!known ? PTC_UI_T_SETUP_PCTL_UNKNOWN :
            model->restriction_enabled ? PTC_UI_T_SETUP_PCTL_ON : PTC_UI_T_SETUP_PCTL_OFF),
        17, known && model->restriction_enabled ? UI_SUCCESS : UI_WARNING);
    draw_text(pixels, stride, dialog.x + 520, dialog.y + 145,
        ptc_ui_text(PTC_UI_T_GUIDE_CLOCK_STATUS), 17, UI_WARNING);
    draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 193,
        ptc_ui_text(bodies[page]), 18, dialog.width - 68, 28, 12, UI_INK);
    draw_dialog_button(pixels, stride, ptc_ui_support_guide_nav_rect(0),
        ptc_ui_text(PTC_UI_T_GUIDE_PREV), UI_RAISED, UI_ACCENT, false);
    draw_dialog_button(pixels, stride, ptc_ui_support_guide_nav_rect(1),
        ptc_ui_text(PTC_UI_T_GUIDE_NEXT), UI_RAISED, UI_ACCENT, false);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay),
        ptc_ui_text(PTC_UI_T_A_B_RETURN), UI_ACCENT, UI_ON_ACCENT, false);
}

bool draw_support_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
    case PTC_UI_OVERLAY_SUPPORT_GUIDE:
        draw_support_guide(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SETUP_PCTL_HELP:
        draw_setup_pctl_help(pixels, stride, model);
        return true;
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
