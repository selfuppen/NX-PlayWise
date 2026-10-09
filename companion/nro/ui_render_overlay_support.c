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
                                    const PtcUiTodayDecision *decision, bool bedtime_enforcing, bool compact)
{
    const PtcUiTextId titles[] = {PTC_UI_T_TODAY_S_ADJUSTMENT, PTC_UI_T_SPECIFIED_DATE_QUOTA,
        PTC_UI_T_HOLIDAY, PTC_UI_T_WEEKLY_PLAN};
    const PtcUiDecisionStep *steps[] = {
        &decision->today_override,
        &decision->scheduled_override,
        &decision->holiday,
        &decision->weekly
    };
    int count = 4;
    int node_w = total_w < 900 ? (total_w - 36) / 4 : 216;
    int node_h = compact ? 128 : 136;
    int gap = (total_w - node_w * count) / (count - 1);
    bool hit_found = false;

    char pipeline_title[192];
    fit_text(pipeline_title, sizeof(pipeline_title), ptc_ui_text(PTC_UI_T_THE_DAILY_QUOTA_IS_DETERMINED_IN_THE),
        14, total_w - (bedtime_enforcing ? 300 : 0));
    draw_text(pixels, stride, x, y + 6, pipeline_title, 14, UI_INK);
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
        char node_title[128];
        snprintf(node_title, sizeof(node_title), "%02d %s", i + 1, ptc_ui_text(titles[i]));
        fit_text(node_title, sizeof(node_title), node_title, 14, card.width - 28);
        draw_text(pixels, stride, card.x + 14, card.y + 20, node_title, 14,
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
        } else if (step->state == PTC_UI_DECISION_UNKNOWN) {
            desc = ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM);
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

static const PtcUiTextId DETAIL_CONDITION_TITLES[] = {
    PTC_UI_T_TOTAL_DAILY_ALLOWANCE, PTC_UI_T_BEDTIME_SCHEDULE,
    PTC_UI_T_DETAIL_REST, PTC_UI_T_DOCK_TITLE
};

static void detail_text(uint32_t *pixels, uint32_t stride, UiRect r,
                        const char *text, int size, uint32_t color, int lines)
{
    draw_wrapped_text(pixels, stride, r.x, r.y, text, size, r.width, size + 4, lines, color);
}

static void detail_panel(uint32_t *pixels, uint32_t stride, UiRect r)
{
    fill_round_rect(pixels, stride, r, 12, UI_SURFACE);
    draw_rect_outline(pixels, stride, r, 12, 1, UI_BORDER);
}

static uint32_t detail_condition_color(PtcUiCondition condition)
{
    return condition == PTC_UI_CONDITION_BLOCK ? UI_DANGER :
        condition == PTC_UI_CONDITION_UNKNOWN ? UI_WARNING :
        condition == PTC_UI_CONDITION_PASS ? UI_SUCCESS : UI_MUTED;
}

static const char *detail_condition_label(PtcUiCondition condition)
{
    return ptc_ui_text(condition == PTC_UI_CONDITION_BLOCK ? PTC_UI_T_DETAIL_BLOCKED :
        condition == PTC_UI_CONDITION_UNKNOWN ? PTC_UI_T_STATUS_TO_CONFIRM :
        condition == PTC_UI_CONDITION_PASS ? PTC_UI_T_DETAIL_PASS :
        condition == PTC_UI_CONDITION_WAIVED ? PTC_UI_T_DETAIL_WAIVED : PTC_UI_T_DETAIL_OFF);
}

static void detail_reasons(unsigned reasons, char *out, size_t size)
{
    static const unsigned bits[] = {PTC_UI_STOP_DAILY, PTC_UI_STOP_BEDTIME,
        PTC_UI_STOP_EYE, PTC_UI_STOP_DOCK, PTC_UI_STOP_DAY_END};
    static const PtcUiTextId labels[] = {PTC_UI_T_DETAIL_STOP_DAILY, PTC_UI_T_DETAIL_STOP_BEDTIME,
        PTC_UI_T_DETAIL_REST, PTC_UI_T_DETAIL_STOP_DOCK, PTC_UI_T_DETAIL_DAY_END};
    out[0] = '\0';
    for (int i = 0; i < 5; ++i) if (reasons & bits[i]) {
        size_t used = strlen(out);
        if (used < size) snprintf(out + used, size - used, "%s%s", used ? " / " : "", ptc_ui_text(labels[i]));
    }
}

static void detail_balance(bool known, bool unlimited, int minutes, char *out, size_t size)
{
    if (!known) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
    else if (unlimited) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    else snprintf(out, size, ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)minutes);
}

static const char *detail_pending_reason(PtcUiEstimatePending reason)
{
    static const PtcUiTextId ids[] = {
        PTC_UI_T_DETAIL_NO_PREDICTION, PTC_UI_T_DETAIL_ESTIMATE_STALE,
        PTC_UI_T_DETAIL_ESTIMATE_DATE, PTC_UI_T_DETAIL_ESTIMATE_REFRESH,
        PTC_UI_T_DETAIL_ESTIMATE_RECOVERY, PTC_UI_T_DETAIL_ESTIMATE_READBACK,
        PTC_UI_T_DETAIL_ESTIMATE_RUNTIME
    };
    return ptc_ui_text(ids[reason]);
}

static void detail_buffer(const PtcUiModel *model, const PtcUiTodayProjection *p,
                          const PtcUiTodayDecision *decision, char *out, size_t size)
{
    if (p->fresh && model->daily_buffer_minutes && !model->daily_buffer_available && !model->daily_buffer_claimed) {
        char minutes[64];
        detail_balance(true, false, model->daily_buffer_minutes, minutes, sizeof(minutes));
        snprintf(out, size, "%s / %s", decision->autonomy, minutes);
    } else snprintf(out, size, "%s", decision->autonomy);
}

static void detail_time(int seconds, char *out, size_t size)
{
    if (seconds % 60)
        snprintf(out, size, "%02u:%02u:%02u", (unsigned)(seconds / 3600),
            (unsigned)((seconds / 60) % 60), (unsigned)(seconds % 60));
    else snprintf(out, size, "%02u:%02u", (unsigned)(seconds / 3600), (unsigned)((seconds / 60) % 60));
}

static bool detail_available(const PtcUiModel *model, const PtcUiTodayProjection *p)
{
    if (!p->fresh || p->active_reasons || model->disable_flag_present || model->recovery_active ||
        model->apply_pending_confirmation || model->temporary_unlocked ||
        strcmp(model->setup_phase, "active") != 0 || model->restricted_now == 1 || model->blocked_today == 1)
        return false;
    for (int i = 0; i < 4; ++i) if (p->conditions[i] == PTC_UI_CONDITION_UNKNOWN) return false;
    return true;
}

static void detail_current(const PtcUiModel *model, const PtcUiTodayProjection *p, char *out, size_t size)
{
    if (p->active_reasons) detail_reasons(p->active_reasons, out, size);
    else if (detail_available(model, p)) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_DETAIL_AVAILABLE));
    else if (model->disable_flag_present) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_CONTROL_IS_DISABLED));
    else if (model->recovery_active) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_RECOVERING));
    else if (model->apply_pending_confirmation) snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_WAITING_FOR_EFFECT));
    else snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_DETAIL_PENDING));
}

static void detail_condition_info(const PtcUiModel *model, const PtcUiTodayProjection *p,
                                 const PtcUiTodayDecision *decision, int index, char *out, size_t size)
{
    if (!p->fresh) { snprintf(out, size, "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM)); return; }
    if (index == 0) detail_balance(p->total_known, p->total_unlimited, p->total_remaining, out, size);
    else if (index == 1) snprintf(out, size, "%s", decision->bedtime);
    else if (index == 2) {
        char cycle[128];
        ptc_ui_format_eye_care_cycle(model, ptc_ui_render_now(), cycle, sizeof(cycle));
        if (strcmp(model->eye_care_phase, "playing") == 0)
            snprintf(out, size, ptc_ui_text(PTC_UI_T_DETAIL_EYE_POLICY), (unsigned)model->eye_care_used_minutes,
                (unsigned)model->eye_care_policy.play_minutes, (unsigned)model->eye_care_policy.rest_minutes);
        else snprintf(out, size, "%s", cycle);
    } else {
        char left[64];
        detail_balance(p->undocked_known, p->undocked_unlimited, p->undocked_remaining, left, sizeof(left));
        snprintf(out, size, "%s / %s", ptc_ui_text(strcmp(model->operation_mode, "docked") == 0 ?
            PTC_UI_T_DOCK_TV : strcmp(model->operation_mode, "undocked") == 0 ? PTC_UI_T_DOCK_HANDHELD :
            PTC_UI_T_STATUS_TO_CONFIRM), left);
    }
}

static void draw_today_plan(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                            const PtcUiTodayProjection *p, const PtcUiTodayDecision *decision)
{
    char text[384], value[128];
    const PtcUiTextId titles[] = {PTC_UI_T_DETAIL_STATE, PTC_UI_T_DETAIL_TOTAL,
        PTC_UI_T_DETAIL_DOCK, PTC_UI_T_DETAIL_CONTINUOUS};
    for (int i = 0; i < 4; ++i) {
        UiRect card = {108 + i * 269, 108, 257, 90};
        detail_panel(pixels, stride, card);
        const char *title = ptc_ui_text(titles[i]);
        detail_text(pixels, stride, (UiRect){card.x + 14, 130, 229, 20}, title, 14, UI_MUTED, 1);
        if (i == 0) detail_current(model, p, value, sizeof(value));
        else if (i == 1) detail_balance(p->total_known, p->total_unlimited, p->total_remaining, value, sizeof(value));
        else if (i == 2) detail_balance(p->undocked_known, p->undocked_unlimited, p->undocked_remaining, value, sizeof(value));
        else if (p->continuous_seconds < 0) snprintf(value, sizeof(value), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
        else snprintf(value, sizeof(value), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)(p->continuous_seconds / 60));
        detail_text(pixels, stride, (UiRect){card.x + 14, 158, 229, 42}, value, i == 0 ? 12 : 22,
            i == 0 ? (p->active_reasons ? UI_DANGER : detail_available(model, p) ? UI_SUCCESS : UI_WARNING) : UI_ACCENT,
            i == 0 ? 3 : 1);
        if (i == 1 && p->total_known && !p->total_unlimited && model->played_minutes_available) {
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_USED),
                (unsigned)model->played_minutes, (unsigned)model->forecast[0].minutes);
            detail_text(pixels, stride, (UiRect){card.x + 14, 185, 229, 18}, text, 11, UI_MUTED, 1);
        }
        if (i == 2 && p->undocked_known && !p->undocked_unlimited && model->undocked_usage_available) {
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_USED_DOCK), (unsigned)model->undocked_used_minutes,
                (unsigned)(model->dock_policy.force_docked ? 0 : model->dock_policy.undocked_daily_minutes));
            detail_text(pixels, stride, (UiRect){card.x + 14, 185, 229, 18}, text, 11, UI_MUTED, 1);
        }
    }
    UiRect sleep = {108, 212, 704, 140};
    detail_panel(pixels, stride, sleep);
    draw_text(pixels, stride, 126, 236, ptc_ui_text(PTC_UI_T_DETAIL_SLEEP), 17, UI_INK);
    UiRect track = {132, 278, 656, 20};
    fill_round_rect(pixels, stride, track, 5, UI_RAISED);
    for (int i = 0; i < p->sleep_count; ++i) {
        const PtcUiSleepInterval *b = &p->sleep[i];
        int x = track.x + b->start_second * track.width / 86400;
        int end = track.x + b->end_second * track.width / 86400;
        fill_rect(pixels, stride, (UiRect){x, track.y, end - x, track.height}, b->skipped ? UI_SUCCESS_SOFT : UI_DANGER_SOFT);
        char from[16], to[16];
        detail_time(b->start_second, from, sizeof(from)); detail_time(b->end_second, to, sizeof(to));
        snprintf(text, sizeof(text), "%s - %s%s", from, to, b->skipped ? ptc_ui_text(PTC_UI_T_DETAIL_SKIPPED) : "");
        draw_text(pixels, stride, 132 + i * 330, 338, text, 13, b->skipped ? UI_SUCCESS : UI_MUTED);
    }
    if (!p->sleep_count) draw_text(pixels, stride, 132, 338, ptc_ui_text(PTC_UI_T_DETAIL_SLEEP_NONE), 13, UI_MUTED);
    int nx = track.x + p->now_second * track.width / 86400;
    draw_line(pixels, stride, nx, 267, nx, 303, 2, UI_ACCENT);
    detail_time(p->now_second, text, sizeof(text));
    int clock_width = measure_text(text, 13);
    int tx = nx - clock_width / 2;
    if (tx < track.x) tx = track.x;
    if (tx > track.x + track.width - clock_width) tx = track.x + track.width - clock_width;
    draw_text(pixels, stride, tx, 261, text, 13, UI_ACCENT);
    for (int i = 0; i <= 6; ++i) {
        snprintf(text, sizeof(text), "%02d:00", i * 4);
        int x = track.x + i * track.width / 6 - 18;
        if (i == 0) x = track.x;
        if (i == 6) x -= 18;
        draw_text(pixels, stride, x, 317, text, 11, UI_MUTED);
    }

    UiRect prediction = {108, 364, 704, 204};
    detail_panel(pixels, stride, prediction);
    draw_text(pixels, stride, 126, 389, ptc_ui_text(PTC_UI_T_DETAIL_PREDICTION), 16, UI_INK);
    const char *mode = ptc_ui_text(!p->fresh ? PTC_UI_T_STATUS_TO_CONFIRM :
        strcmp(model->operation_mode, "docked") == 0 ? PTC_UI_T_DOCK_TV : PTC_UI_T_DOCK_HANDHELD);
    detail_text(pixels, stride, (UiRect){126, 412, 668, 20}, mode, 13, UI_MUTED, 1);
    fill_rect(pixels, stride, (UiRect){546, 402, 10, 10}, UI_ACCENT);
    draw_text(pixels, stride, 562, 412, ptc_ui_text(PTC_UI_T_DETAIL_PLAY), 12, UI_MUTED);
    fill_rect(pixels, stride, (UiRect){654, 402, 10, 10}, UI_WARNING);
    draw_text(pixels, stride, 670, 412, ptc_ui_text(PTC_UI_T_DETAIL_REST), 12, UI_MUTED);
    if (!p->simulation_available) {
        detail_text(pixels, stride, (UiRect){126, 450, 668, 60}, detail_pending_reason(p->pending_reason), 15, UI_WARNING, 2);
    } else if (!p->segment_count) {
        detail_reasons(p->stop_reasons, text, sizeof(text));
        detail_text(pixels, stride, (UiRect){126, 450, 668, 60}, text, 15, UI_DANGER, 2);
    } else {
        UiRect rail = {132, 460, 656, 18};
        int range = p->stop_second - p->now_second;
        if (range <= 0) range = 1;
        for (int i = 0; i < p->segment_count; ++i) {
            const PtcUiUsageSegment *seg = &p->segments[i];
            int x = rail.x + (seg->start_second - p->now_second) * rail.width / range;
            int end = rail.x + (seg->end_second - p->now_second) * rail.width / range;
            fill_rect(pixels, stride, (UiRect){x, rail.y, end - x, rail.height}, seg->resting ? UI_WARNING : UI_ACCENT);
        }
        detail_time(p->now_second, text, sizeof(text));
        draw_text(pixels, stride, rail.x, 448, text, 12, UI_MUTED);
        detail_time(p->stop_second, text, sizeof(text));
        draw_text(pixels, stride, rail.x + rail.width - measure_text(text, 12), 448, text, 12, UI_DANGER);
        /* A readable event row preserves the first break and resume even for dense cycles. */
        for (int i = 0; i < p->segment_count; ++i) if (p->segments[i].resting) {
            char from[16], to[16];
            detail_time(p->segments[i].start_second, from, sizeof(from));
            detail_time(p->segments[i].end_second, to, sizeof(to));
            if (i + 1 < p->segment_count && !p->segments[i + 1].resting)
                snprintf(text, sizeof(text), "%s %s / %s %s", from, ptc_ui_text(PTC_UI_T_DETAIL_REST),
                    to, ptc_ui_text(PTC_UI_T_DETAIL_RESUME));
            else snprintf(text, sizeof(text), "%s %s", from, ptc_ui_text(PTC_UI_T_DETAIL_REST));
            detail_text(pixels, stride, (UiRect){132, 498, 656, 20}, text, 13, UI_WARNING, 1);
            break;
        }
        detail_reasons(p->stop_reasons, text, sizeof(text));
        detail_text(pixels, stride, (UiRect){132, 522, 656, 20}, text, 13, UI_DANGER, 1);
    }
    if (p->total_at_stop >= 0) {
        snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_AT_STOP), (unsigned)p->total_at_stop);
        detail_text(pixels, stride, (UiRect){132, 549, 656, 20}, text, 14, UI_INK, 1);
    }

    detail_panel(pixels, stride, (UiRect){824, 212, 348, 128});
    draw_text(pixels, stride, 840, 236, ptc_ui_text(PTC_UI_T_DETAIL_SOURCE), 17, UI_INK);
    detail_balance(model->forecast_available && model->forecast[0].day_index == model->day_index,
        p->total_unlimited, model->forecast[0].minutes, value, sizeof(value));
    snprintf(text, sizeof(text), "%s / %s", p->fresh ? ptc_ui_effective_rule_label(decision->effective.source) :
        ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM), p->fresh ? value : ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
    detail_text(pixels, stride, (UiRect){840, 260, 316, 22}, text, 14, UI_ACCENT, 1);
    detail_buffer(model, p, decision, text, sizeof(text));
    detail_text(pixels, stride, (UiRect){840, 282, 316, 22}, text, 12, UI_MUTED, 1);
    if (p->fresh && !model->daily_buffer_claimed && model->daily_buffer_minutes > 0)
        detail_text(pixels, stride, (UiRect){840, 296, 316, 18}, ptc_ui_text(PTC_UI_T_DETAIL_BUFFER_NOTE), 10, UI_MUTED, 1);
    detail_text(pixels, stride, (UiRect){840, 315, 316, 32}, ptc_ui_text(PTC_UI_T_DETAIL_RELATION), 11, UI_INK, 2);
    detail_panel(pixels, stride, (UiRect){824, 352, 348, 140});
    draw_text(pixels, stride, 840, 376, ptc_ui_text(PTC_UI_T_DETAIL_HEALTH), 17, UI_INK);
    for (int i = 1; i < 4; ++i) {
        detail_condition_info(model, p, decision, i, value, sizeof(value));
        snprintf(text, sizeof(text), "%s / %s", ptc_ui_text(DETAIL_CONDITION_TITLES[i]), detail_condition_label(p->conditions[i]));
        detail_text(pixels, stride, (UiRect){840, 396 + (i - 1) * 32, 316, 18}, text, 13, detail_condition_color(p->conditions[i]), 1);
        detail_text(pixels, stride, (UiRect){840, 412 + (i - 1) * 32, 316, 18}, value, 11, UI_MUTED, 1);
    }
    home_button(pixels, stride, ptc_ui_home_details_action_rect(model, 0), ptc_ui_text(PTC_UI_T_DETAIL_OPEN),
        model->home_details_focus == 0, false, false);
}

static void draw_today_rules(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                             const PtcUiTodayProjection *p, const PtcUiTodayDecision *decision)
{
    char text[512];
    int offset = model->home_details_scroll;
    PtcUiRect body = ptc_ui_home_details_body_rect();
    ui_set_vertical_clip(body.y, body.y + body.h);
    detail_panel(pixels, stride, (UiRect){108, 108 - offset, 1064, 36});
    detail_current(model, p, text, sizeof(text));
    detail_text(pixels, stride, (UiRect){124, 132 - offset, 1032, 22}, text, 15,
        p->active_reasons ? UI_DANGER : detail_available(model, p) ? UI_SUCCESS : UI_WARNING, 1);
    draw_waterfall_pipeline(pixels, stride, 108, 160 - offset, 1064, decision, false, true);
    draw_text(pixels, stride, 108, 325 - offset, ptc_ui_text(PTC_UI_T_DETAIL_CONDITIONS), 15, UI_INK);
    for (int i = 0; i < 4; ++i) {
        UiRect card = {108 + i * 269, 338 - offset, 257, 80};
        detail_panel(pixels, stride, card);
        draw_text(pixels, stride, card.x + 14, card.y + 21, ptc_ui_text(DETAIL_CONDITION_TITLES[i]), 15, UI_INK);
        detail_condition_info(model, p, decision, i, text, sizeof(text));
        detail_text(pixels, stride, (UiRect){card.x + 14, card.y + 40, 229, 36}, text, 12, UI_INK, 2);
        draw_text(pixels, stride, card.x + 14, card.y + 73, detail_condition_label(p->conditions[i]), 12,
            detail_condition_color(p->conditions[i]));
        int x = card.x + card.width / 2;
        draw_line(pixels, stride, x, card.y + card.height, x, 425 - offset, 1, UI_BORDER);
    }
    draw_line(pixels, stride, 236, 425 - offset, 1043, 425 - offset, 1, UI_BORDER);
    detail_current(model, p, text, sizeof(text));
    draw_text_center(pixels, stride, (UiRect){108, 428 - offset, 1064, 22}, text, 13,
        p->active_reasons ? UI_DANGER : UI_SUCCESS);
    detail_text(pixels, stride, (UiRect){108, 459 - offset, 1064, 20}, ptc_ui_text(PTC_UI_T_DETAIL_RELATION), 13, UI_INK, 1);
    detail_text(pixels, stride, (UiRect){108, 479 - offset, 1064, 20}, ptc_ui_text(PTC_UI_T_DETAIL_RELATION_MORE), 12, UI_MUTED, 1);
    char buffer[256];
    detail_buffer(model, p, decision, buffer, sizeof(buffer));
    snprintf(text, sizeof(text), "%s%s%s", buffer,
        p->fresh && !model->daily_buffer_claimed && model->daily_buffer_minutes > 0 ? " / " : "",
        p->fresh && !model->daily_buffer_claimed && model->daily_buffer_minutes > 0 ? ptc_ui_text(PTC_UI_T_DETAIL_BUFFER_NOTE) : "");
    detail_text(pixels, stride, (UiRect){108, 501 - offset, 1064, 20}, text, 13, UI_ACCENT, 1);
    home_button(pixels, stride, ptc_ui_home_details_action_rect(model, 0), ptc_ui_text(PTC_UI_T_DETAIL_DATA),
        model->home_details_focus == 0, false, false);
    if (model->home_details_data_expanded) {
        PtcUiRect row = ptc_ui_home_details_data_rect(model, model->home_details_focus);
        if (row.h) {
            fill_round_rect(pixels, stride, to_uirect(row), 8, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, to_uirect(row), 8, 2, UI_ACCENT);
        }
        int y = 589 - offset;
        if (model->usage_summary_available && model->usage_known_days_7)
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_HISTORY7), (unsigned)model->usage_consumed_minutes_7,
                (unsigned)model->usage_known_days_7);
        else snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        draw_text(pixels, stride, 124, y, text, 15, UI_INK);
        if (model->usage_summary_available && model->usage_known_days_30)
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_HISTORY30), (unsigned)model->usage_consumed_minutes_30,
                (unsigned)model->usage_known_days_30);
        else snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        draw_text(pixels, stride, 124, y + 28, text, 15, UI_INK);
        detail_text(pixels, stride, (UiRect){124, y + 54, 1032, 24}, ptc_ui_text(PTC_UI_T_DETAIL_HISTORY_NOTE), 13, UI_MUTED, 1);
        ptc_ui_format_timer_status(model, text, sizeof(text));
        detail_text(pixels, stride, (UiRect){124, y + 82, 1032, 24}, p->fresh ? text : ptc_ui_text(PTC_UI_T_DETAIL_PENDING), 15, UI_INK, 1);
        const char *runtime = model->disable_flag_present ? ptc_ui_text(PTC_UI_T_CONTROL_IS_DISABLED) :
            model->recovery_active ? ptc_ui_text(PTC_UI_T_RECOVERING) :
            model->apply_pending_confirmation ? ptc_ui_text(PTC_UI_T_WAITING_FOR_EFFECT) :
            !p->fresh ? ptc_ui_text(PTC_UI_T_DETAIL_PENDING) : ptc_ui_text(PTC_UI_T_RUNNING_NORMALLY);
        draw_text(pixels, stride, 124, y + 111, runtime, 15, UI_MUTED);
        if (model->view == PTC_UI_PARENT) {
            char command[96], transport[96];
            snprintf(text, sizeof(text), "%s / %s / %s", ptc_ui_localize(model->command_name, command, sizeof(command)),
                ptc_ui_localize(model->transport_label, transport, sizeof(transport)), model->result_status);
            detail_text(pixels, stride, (UiRect){124, y + 146, 1032, 24}, text, 14, UI_INK, 1);
            detail_text(pixels, stride, (UiRect){124, y + 172, 1032, 50}, model->feedback_detail, 13, UI_MUTED, 2);
        }
    }
    ui_set_vertical_clip(0, SCREEN_HEIGHT);
}

static void draw_home_details(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiTodayProjection projection;
    PtcUiTodayDecision decision;
    char age[64];
    PtcUiModel shell = *model;
    shell.overlay_title[0] = shell.overlay_body[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell, &dialog, 1120, 640);
    int64_t now = ptc_ui_render_now();
    uint16_t day = model->day_index;
    int second = ptc_ui_render_minute_of_day(now) * 60;
#ifdef PTC_UI_PREVIEW_ANIM_CLOCK_MS
    if (model->home_details_preview_second > 0) second = model->home_details_preview_second;
#endif
#ifndef PTC_UI_PREVIEW_WALL_TIME
    time_t clock = (time_t)now;
    struct tm *local = localtime(&clock);
    if (local) {
        second += local->tm_sec;
        if (!ptc_day_index_from_date(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, &day)) day = UINT16_MAX;
    } else day = UINT16_MAX;
#endif
    ptc_ui_project_today(model, now, day, second, &projection);
    ptc_ui_build_today_decision(model, PTC_UI_PLAN_SAVED, now, &decision);
    if (!projection.fresh) {
        decision.today_override.state = decision.scheduled_override.state =
            decision.holiday.state = decision.weekly.state = PTC_UI_DECISION_UNKNOWN;
        snprintf(decision.autonomy, sizeof(decision.autonomy), "%s", ptc_ui_text(PTC_UI_T_AUTONOMY_STATUS_PENDING));
    }
    draw_text(pixels, stride, 114, 82, ptc_ui_text(model->home_details_page == 0 ?
        PTC_UI_T_DETAIL_PLANNING : PTC_UI_T_DETAIL_RULES), 22, UI_INK);
    format_status_age(model, age, sizeof(age));
    draw_text_center(pixels, stride, (UiRect){906, 56, 260, 36}, projection.fresh ? age :
        ptc_ui_text(PTC_UI_T_DETAIL_PENDING), 14, projection.fresh ? UI_MUTED : UI_WARNING);
    if (model->home_details_page == 0) draw_today_plan(pixels, stride, model, &projection, &decision);
    else draw_today_rules(pixels, stride, model, &projection, &decision);
    detail_text(pixels, stride, (UiRect){108, 592, 1064, 20}, ptc_ui_text(model->home_details_page ?
        PTC_UI_T_DETAIL_HEALTH_NOTE : PTC_UI_T_DETAIL_RELATION_MORE), 12, UI_MUTED, 1);
    home_button(pixels, stride, ptc_ui_home_details_action_rect(model, 2), ptc_ui_text(model->home_details_page ?
        PTC_UI_T_DETAIL_BACK : model->view == PTC_UI_CHILD ? PTC_UI_T_DETAIL_BACK_CHILD : PTC_UI_T_DETAIL_BACK_TODAY),
        model->home_details_focus == 2, false, false);
    home_button(pixels, stride, ptc_ui_home_details_action_rect(model, 1), ptc_ui_text(PTC_UI_T_Y_REFRESH),
        model->home_details_focus == 1, false, model->waiting);
    char footer[384];
    snprintf(footer, sizeof(footer), "%s%s%s", model->home_details_page == 0 ? ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_NOTE) : "",
        model->home_details_page == 0 ? "\n" : "", ptc_ui_text(model->home_details_page == 1 &&
        model->home_details_data_expanded ? PTC_UI_T_DETAIL_SCROLL : PTC_UI_T_DETAIL_ESTIMATE));
    detail_text(pixels, stride, (UiRect){542, 619, 630, 38}, footer, 11, UI_MUTED, 2);
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
    int type_size = 20;
    while (type_size > 12 && measure_text(type_label, type_size) > 136) --type_size;
    draw_text(pixels, stride, hero.x + 190, hero.y + 58, type_label, type_size,
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
    const char *badge_title = ptc_ui_text(fresh ? PTC_UI_T_FINAL_EFFECTIVE_RULES : PTC_UI_T_RULES_PENDING_CONFIRMATION);
    int badge_value_x = active_badge.x + 26 + measure_text(badge_title, 12);
    draw_text(pixels, stride, active_badge.x + 14, active_badge.y + 20,
              badge_title, 12, fresh ? UI_SUCCESS : UI_WARNING);
    fit_text(eff_label, sizeof(eff_label), eff_label, 14, active_badge.x + active_badge.width - 14 - badge_value_x);
    draw_text(pixels, stride, badge_value_x, active_badge.y + 20, eff_label, 14, UI_INK);
    draw_wrapped_text(pixels, stride, active_badge.x + 14, active_badge.y + 40, decision.final_reason,
                      11, active_badge.width - 28, 15, 1, UI_MUTED);

    /* 2. 额度决策流水线 (1064 x 164) */
    draw_waterfall_pipeline(pixels, stride, x_left, top_y + 88, full_w, &decision,
                            is_today && model->bedtime_active && !model->bedtime_skipped, false);

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
                 (unsigned)model->eye_care_policy.play_minutes,
                 (unsigned)model->eye_care_policy.rest_minutes);
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

    draw_wrapped_text(pixels, stride, rule_info.x + 16, rule_info.y + 108,
        ptc_ui_text(PTC_UI_T_DETAIL_RELATION), 12, rule_info.width - 32, 16, 2, UI_INK);
    draw_wrapped_text(pixels, stride, rule_info.x + 16, rule_info.y + 145,
        ptc_ui_text(PTC_UI_T_DETAIL_RELATION_MORE), 11, rule_info.width - 32, 14, 2, UI_MUTED);
    draw_wrapped_text(pixels, stride, rule_info.x + 16, rule_info.y + 174,
        ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_NOTE), 11, rule_info.width - 32, 14, 1, UI_MUTED);

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
