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
    const char *LABELS[] = {
        ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM),
        ptc_ui_text(PTC_UI_T_LIGHT),
        ptc_ui_text(PTC_UI_T_DARK)
    };
    const char *DETAILS[] = {
        ptc_ui_text(PTC_UI_T_FOLLOWS_SWITCH_SETTINGS),
        ptc_ui_text(PTC_UI_T_REFRESHING_LIGHT_BACKGROUND),
        ptc_ui_text(PTC_UI_T_SOFT_DARK_BACKGROUND)
    };
    UiRect dialog;
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 1160, 600);

    for (index = 0; index < 3; ++index) {
        UiRect option = to_uirect(ptc_ui_theme_option_rect(index));
        bool selected = index == model->overlay_selection;
        bool is_current = (int)g_theme.preference == index;

        // Card container shadow and background
        draw_round_rect_shadow(pixels, stride, option, 16, 12, selected ? 45 : 20, 3);
        fill_round_rect(pixels, stride, option, 16, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 16, selected ? 3 : 1, selected ? UI_ACCENT : UI_BORDER);

        // Header Title
        draw_text(pixels, stride, option.x + 20, option.y + 36, LABELS[index], 24, UI_INK);

        // Current theme badge if active
        if (is_current) {
            UiRect cur_badge = {option.x + option.width - 92, option.y + 16, 76, 26};
            fill_round_rect(pixels, stride, cur_badge, 13, UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, cur_badge, 13, 1, UI_SUCCESS_BORDER);
            draw_text_center(pixels, stride, (UiRect){cur_badge.x, cur_badge.y + 2, cur_badge.width, cur_badge.height},
                             ptc_ui_text(PTC_UI_T_ADJUST_BADGE_ACTIVE), 14, UI_SUCCESS);
        }

        // Subtitle description (measured or wrapped)
        draw_wrapped_text(pixels, stride, option.x + 20, option.y + 68, DETAILS[index],
                          15, option.width - 40, 22, 2, UI_MUTED);

        // Realistic UI Miniature Preview Screen inside the card
        UiRect preview = {option.x + 18, option.y + 116, option.width - 36, 204};
        draw_rect_outline(pixels, stride, preview, 12, 1, selected ? UI_ACCENT : UI_BORDER);

        if (index == PTC_UI_THEME_LIGHT) {
            // Light Theme Preview
            fill_round_rect(pixels, stride, preview, 12, UI_RGB(0xF0F3FA));
            // Top mini navbar
            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 10, preview.width - 20, 28}, 6, UI_RGB(0xFFFFFF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16, preview.y + 18, 12, 12}, 3, UI_RGB(0x245BC4));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 36, preview.y + 22, 64, 4}, 2, UI_RGB(0x172640));
            // Content cards
            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 46, preview.width - 20, 48}, 8, UI_RGB(0xFFFFFF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 20, preview.y + 58, 80, 8}, 4, UI_RGB(0x245BC4));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 20, preview.y + 74, 120, 5}, 2, UI_RGB(0x53627A));
            // Secondary cards
            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 102, (preview.width - 26) / 2, 88}, 8, UI_RGB(0xFFFFFF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16 + (preview.width - 26) / 2, preview.y + 102, (preview.width - 26) / 2, 88}, 8, UI_RGB(0xE8EDF7));
            // Accent button
            fill_round_rect(pixels, stride, (UiRect){preview.x + 18, preview.y + 154, (preview.width - 42) / 2, 24}, 6, UI_RGB(0x245BC4));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 24 + (preview.width - 26) / 2, preview.y + 154, (preview.width - 42) / 2, 24}, 6, UI_RGB(0x16734E));
        } else if (index == PTC_UI_THEME_DARK) {
            // Dark Theme Preview
            fill_round_rect(pixels, stride, preview, 12, UI_RGB(0x0B0F19));
            // Top mini navbar
            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 10, preview.width - 20, 28}, 6, UI_RGB(0x162032));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16, preview.y + 18, 12, 12}, 3, UI_RGB(0x91B9FF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 36, preview.y + 22, 64, 4}, 2, UI_RGB(0xF3F6FD));
            // Content cards
            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 46, preview.width - 20, 48}, 8, UI_RGB(0x162032));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 20, preview.y + 58, 80, 8}, 4, UI_RGB(0x91B9FF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 20, preview.y + 74, 120, 5}, 2, UI_RGB(0xB6C4D9));
            // Secondary cards
            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 102, (preview.width - 26) / 2, 88}, 8, UI_RGB(0x162032));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16 + (preview.width - 26) / 2, preview.y + 102, (preview.width - 26) / 2, 88}, 8, UI_RGB(0x1E2C44));
            // Accent button
            fill_round_rect(pixels, stride, (UiRect){preview.x + 18, preview.y + 154, (preview.width - 42) / 2, 24}, 6, UI_RGB(0x91B9FF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 24 + (preview.width - 26) / 2, preview.y + 154, (preview.width - 42) / 2, 24}, 6, UI_RGB(0x5CCB8A));
        } else {
            // Split System Theme Preview (Left Light, Right Dark)
            int half_w = preview.width / 2;
            // Left half light
            fill_round_rect(pixels, stride, (UiRect){preview.x, preview.y, half_w, preview.height}, 12, UI_RGB(0xF0F3FA));
            // Right half dark
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w, preview.y, preview.width - half_w, preview.height}, 12, UI_RGB(0x0B0F19));
            // Seam line
            fill_rect_packed(pixels, stride, (UiRect){preview.x + half_w, preview.y, 1, preview.height}, UI_RGB(0x79879D));
            // Left mini card
            fill_round_rect(pixels, stride, (UiRect){preview.x + 8, preview.y + 16, half_w - 16, 70}, 6, UI_RGB(0xFFFFFF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 26, 48, 6}, 3, UI_RGB(0x245BC4));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 40, half_w - 28, 4}, 2, UI_RGB(0x53627A));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 60, half_w - 28, 16}, 4, UI_RGB(0x245BC4));
            // Right mini card
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 8, preview.y + 16, half_w - 16, 70}, 6, UI_RGB(0x162032));
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 14, preview.y + 26, 48, 6}, 3, UI_RGB(0x91B9FF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 14, preview.y + 40, half_w - 28, 4}, 2, UI_RGB(0xB6C4D9));
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 14, preview.y + 60, half_w - 28, 16}, 4, UI_RGB(0x91B9FF));
            // Bottom banner explaining auto sync
            fill_round_rect(pixels, stride, (UiRect){preview.x + 12, preview.y + 114, preview.width - 24, 72}, 8, UI_ACCENT_SOFT);
            draw_text_center(pixels, stride, (UiRect){preview.x + 12, preview.y + 128, preview.width - 24, 24},
                             ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM), 16, UI_ACCENT);
            draw_text_center(pixels, stride, (UiRect){preview.x + 12, preview.y + 154, preview.width - 24, 20},
                             "Auto Day / Night Switch", 13, UI_MUTED);
        }

        // Selection pill / button at bottom of card
        UiRect select_btn = {option.x + 24, option.y + option.height - 52, option.width - 48, 38};
        fill_round_rect(pixels, stride, select_btn, 19, selected ? UI_ACCENT : UI_SURFACE);
        draw_rect_outline(pixels, stride, select_btn, 19, 1, selected ? UI_FOCUS : UI_CONTROL);
        draw_text_center(pixels, stride, (UiRect){select_btn.x, select_btn.y + 6, select_btn.width, select_btn.height},
                         selected ? ptc_ui_text(PTC_UI_T_A_CONFIRM_SAVE) : (is_current ? ptc_ui_text(PTC_UI_T_CURRENT) : ptc_ui_text(PTC_UI_T_A_CONFIRM)),
                         16, selected ? UI_ON_ACCENT : UI_INK);
    }

    // Bottom guidance footer
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 550,
              !g_theme.system_theme_available && model->overlay_selection == PTC_UI_THEME_SYSTEM
                  ? ptc_ui_text(PTC_UI_T_THE_SYSTEM_THEME_IS_TEMPORARILY_UNAVAILABLE_AND)
                  : ptc_ui_text(PTC_UI_T_D_PAD_SELECT_A_APPLY_SAVE_B),
              18, !g_theme.system_theme_available ? UI_WARNING : UI_MUTED);
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


/* ================== 今日调度详情重构辅助图元 ================== */

static void support_fill_circle_mini(uint32_t *pixels, uint32_t stride, float cx, float cy, float radius, uint32_t color)
{
    if (radius <= 0.0f) return;
    uint32_t resolved = resolve_color(color);
    int ir = (int)(radius + 1.5f);
    int icx = (int)(cx + 0.5f);
    int icy = (int)(cy + 0.5f);
    for (int dy = -ir; dy <= ir; ++dy) {
        for (int dx = -ir; dx <= ir; ++dx) {
            float px = (float)(icx + dx) + 0.5f;
            float py = (float)(icy + dy) + 0.5f;
            float dist = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
            float delta = radius + 0.5f - dist;
            if (delta > 0.0f) {
                float cov = delta > 1.0f ? 1.0f : delta;
                blend_pixel(pixels, stride, icx + dx, icy + dy, resolved, (uint8_t)(cov * 255.0f + 0.5f));
            }
        }
    }
}

static void support_draw_crescent_moon(uint32_t *pixels, uint32_t stride, int cx, int cy, float r_out, float r_in,
                                       float offset_x, float offset_y, uint32_t color)
{
    uint32_t resolved = resolve_color(color);
    int ir = (int)(r_out + 2.0f);
    for (int dy = -ir; dy <= ir; ++dy) {
        for (int dx = -ir; dx <= ir; ++dx) {
            float dist_out = sqrtf((float)(dx * dx + dy * dy));
            float delta_out = r_out + 0.5f - dist_out;
            if (delta_out <= 0.0f) continue;
            float in_dx = (float)dx - offset_x;
            float in_dy = (float)dy - offset_y;
            float dist_in = sqrtf(in_dx * in_dx + in_dy * in_dy);
            float delta_in = dist_in - (r_in - 0.5f);
            if (delta_in <= 0.0f) continue;
            float cov = delta_out < delta_in ? delta_out : delta_in;
            if (cov > 1.0f) cov = 1.0f;
            blend_pixel(pixels, stride, cx + dx, cy + dy, resolved, (uint8_t)(cov * 255.0f + 0.5f));
        }
    }
}

static void support_draw_checkmark(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t bg_color, uint32_t fg_color)
{
    support_fill_circle_mini(pixels, stride, (float)cx, (float)cy, (float)radius, bg_color);
    draw_line(pixels, stride, cx - radius * 4 / 9, cy, cx - radius / 7, cy + radius * 3 / 8, 2, fg_color);
    draw_line(pixels, stride, cx - radius / 7, cy + radius * 3 / 8, cx + radius * 5 / 9, cy - radius * 3 / 8, 2, fg_color);
}

static void support_draw_clock(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t color)
{
    draw_circle_outline(pixels, stride, cx, cy, radius, 2, color);
    support_fill_circle_mini(pixels, stride, (float)cx, (float)cy, 1.8f, color);
    draw_line(pixels, stride, cx, cy, cx, cy - radius * 6 / 10, 2, color);
    draw_line(pixels, stride, cx, cy, cx + radius * 5 / 10, cy, 2, color);
}

static void support_draw_controller(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t color)
{
    int hw = radius * 8 / 10;
    int hh = radius * 5 / 10;
    fill_round_rect(pixels, stride, (UiRect){cx - hw, cy - hh, hw * 2, hh * 2}, 4, color);
    fill_round_rect(pixels, stride, (UiRect){cx - hw + 2, cy - hh + 2, hw * 2 - 4, hh * 2 - 4}, 3, UI_SURFACE);
    draw_line(pixels, stride, cx - hw / 2 - 2, cy, cx - hw / 2 + 2, cy, 1, color);
    draw_line(pixels, stride, cx - hw / 2, cy - 2, cx - hw / 2, cy + 2, 1, color);
    support_fill_circle_mini(pixels, stride, (float)(cx + hw / 2 - 2), (float)cy, 1.1f, color);
    support_fill_circle_mini(pixels, stride, (float)(cx + hw / 2 + 2), (float)cy, 1.1f, color);
    support_fill_circle_mini(pixels, stride, (float)(cx + hw / 2), (float)(cy - 2), 1.1f, color);
    support_fill_circle_mini(pixels, stride, (float)(cx + hw / 2), (float)(cy + 2), 1.1f, color);
}

static void support_draw_tv(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t color)
{
    int hw = radius * 9 / 10;
    int hh = radius * 6 / 10;
    fill_round_rect(pixels, stride, (UiRect){cx - hw, cy - hh - 2, hw * 2, hh * 2}, 3, color);
    fill_round_rect(pixels, stride, (UiRect){cx - hw + 2, cy - hh, hw * 2 - 4, hh * 2 - 4}, 2, UI_SURFACE);
    draw_line(pixels, stride, cx, cy + hh - 2, cx, cy + hh + 2, 2, color);
    draw_line(pixels, stride, cx - hw / 2, cy + hh + 2, cx + hw / 2, cy + hh + 2, 2, color);
}

static void support_draw_eye(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t color)
{
    int rx = radius * 9 / 10;
    int ry = radius * 5 / 10;
    draw_line(pixels, stride, cx - rx, cy, cx - rx / 2, cy - ry, 2, color);
    draw_line(pixels, stride, cx - rx / 2, cy - ry, cx + rx / 2, cy - ry, 2, color);
    draw_line(pixels, stride, cx + rx / 2, cy - ry, cx + rx, cy, 2, color);
    draw_line(pixels, stride, cx - rx, cy, cx - rx / 2, cy + ry, 2, color);
    draw_line(pixels, stride, cx - rx / 2, cy + ry, cx + rx / 2, cy + ry, 2, color);
    draw_line(pixels, stride, cx + rx / 2, cy + ry, cx + rx, cy, 2, color);
    support_fill_circle_mini(pixels, stride, (float)cx, (float)cy, (float)(ry * 6 / 10), color);
}

static void support_draw_calendar(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t color)
{
    int w = radius * 8 / 10;
    int h = radius * 8 / 10;
    draw_rect_outline(pixels, stride, (UiRect){cx - w, cy - h + 2, w * 2, h * 2 - 2}, 3, 2, color);
    draw_line(pixels, stride, cx - w, cy - h / 3, cx + w, cy - h / 3, 2, color);
    draw_line(pixels, stride, cx - w / 2, cy - h, cx - w / 2, cy - h + 4, 2, color);
    draw_line(pixels, stride, cx + w / 2, cy - h, cx + w / 2, cy - h + 4, 2, color);
}

static void support_draw_gift(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t color)
{
    int w = radius * 8 / 10;
    int h = radius * 8 / 10;
    draw_rect_outline(pixels, stride, (UiRect){cx - w, cy - h + 4, w * 2, h * 2 - 4}, 2, 2, color);
    draw_line(pixels, stride, cx, cy - h + 4, cx, cy + h, 2, color);
    draw_line(pixels, stride, cx - w, cy, cx + w, cy, 2, color);
    draw_circle_outline(pixels, stride, cx - 3, cy - h + 3, 3, 1, color);
    draw_circle_outline(pixels, stride, cx + 3, cy - h + 3, 3, 1, color);
}

static void support_draw_info(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t color)
{
    draw_circle_outline(pixels, stride, cx, cy, radius, 2, color);
    support_fill_circle_mini(pixels, stride, (float)cx, (float)(cy - radius * 4 / 10), 1.3f, color);
    draw_line(pixels, stride, cx, cy - radius / 10, cx, cy + radius * 5 / 10, 2, color);
}


static void support_draw_chevron_right(uint32_t *pixels, uint32_t stride, int cx, int cy, int size, uint32_t color)
{
    draw_line(pixels, stride, cx - size / 2, cy - size, cx + size / 2, cy, 2, color);
    draw_line(pixels, stride, cx + size / 2, cy, cx - size / 2, cy + size, 2, color);
}

static void support_draw_play(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t bg_color, uint32_t fg_color)
{
    support_fill_circle_mini(pixels, stride, (float)cx, (float)cy, (float)radius, bg_color);
    int pw = radius * 5 / 10;
    int ph = radius * 5 / 10;
    draw_line(pixels, stride, cx - pw / 2, cy - ph, cx - pw / 2, cy + ph, 2, fg_color);
    draw_line(pixels, stride, cx - pw / 2, cy - ph, cx + pw, cy, 2, fg_color);
    draw_line(pixels, stride, cx - pw / 2, cy + ph, cx + pw, cy, 2, fg_color);
}

static void support_draw_heart(uint32_t *pixels, uint32_t stride, int cx, int cy, int size, uint32_t color)
{
    float r = (float)size * 0.38f;
    float d = r * 0.85f;
    support_fill_circle_mini(pixels, stride, (float)cx - d, (float)cy - d * 0.5f, r, color);
    support_fill_circle_mini(pixels, stride, (float)cx + d, (float)cy - d * 0.5f, r, color);
    int top_y = cy - (int)(d * 0.5f);
    int bot_y = cy + size;
    for (int y = top_y; y <= bot_y; ++y) {
        float t = (float)(y - top_y) / (float)(bot_y - top_y);
        float half_w = (1.0f - t) * (d + r);
        if (half_w > 0.0f) {
            draw_line(pixels, stride, (int)((float)cx - half_w), y, (int)((float)cx + half_w), y, 1, color);
        }
    }
}

static void draw_today_plan(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                            const PtcUiTodayProjection *p, const PtcUiTodayDecision *decision)
{
    char text[384], value[128], buf[128], fitted[128];
    bool is_avail = detail_available(model, p);
    bool is_dark = (g_theme.resolved == PTC_UI_RESOLVED_DARK);
    bool is_docked = (strcmp(model->operation_mode, "docked") == 0);
    bool dock_enabled = (model->dock_policy.undocked_limit_enabled || model->dock_policy.force_docked);
    bool eye_resting = model->eye_care_policy.enabled &&
                       strcmp(model->eye_care_phase, "resting") == 0;

    /* 1. 顶部 3 项核心感知卡片 (y = 72, h = 92) - 职责清晰、全屏唯一无冗余 */
    /* 1.1 卡片 0: 当前准入判定 (x = 40, w = 368) */
    {
        UiRect c0 = {40, 72, 368, 92};
        uint32_t c0_bg = is_avail ? UI_SUCCESS_SOFT : (p->active_reasons ? UI_DANGER_SOFT : UI_WARNING_SOFT);
        uint32_t c0_bd = is_avail ? UI_SUCCESS_BORDER : (p->active_reasons ? UI_DANGER_BORDER : UI_WARNING_BORDER);
        uint32_t c0_fg = is_avail ? (is_dark ? UI_RGB(0x49AA19) : UI_RGB(0x52C41A)) : (p->active_reasons ? UI_DANGER : UI_WARNING_TEXT);
        fill_round_rect(pixels, stride, c0, 10, c0_bg);
        draw_rect_outline(pixels, stride, c0, 10, 1, c0_bd);
        support_draw_checkmark(pixels, stride, c0.x + 36, c0.y + 46, 17, c0_fg, UI_ON_ACCENT);
        detail_current(model, p, value, sizeof(value));
        draw_text_bold(pixels, stride, c0.x + 68, c0.y + 36, value, 20, UI_INK);
        draw_text(pixels, stride, c0.x + 68, c0.y + 60,
                  is_avail ? ptc_ui_text(PTC_UI_T_DETAIL_ALL_LIMITS_CLEAR) : value, 12, UI_MUTED);
    }

    /* 1.2 卡片 1: 今日总额度走势与规则来源 (x = 428, w = 432) */
    {
        UiRect c1 = {428, 72, 432, 92};
        fill_round_rect(pixels, stride, c1, 10, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, c1, 10, 1, UI_BORDER);
        support_draw_clock(pixels, stride, c1.x + 24, c1.y + 26, 11, UI_ACCENT);
        draw_text(pixels, stride, c1.x + 44, c1.y + 24, ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE), 12, UI_MUTED);

        /* 右上角规则来源标签胶囊 (全屏统领总额度来源，消除卡片D的重复画图) */
        char plan_tag[96];
        if (p->total_unlimited) {
            snprintf(plan_tag, sizeof(plan_tag), "%s",
                     ptc_ui_effective_rule_label(decision->effective.source));
        } else {
            snprintf(plan_tag, sizeof(plan_tag), "%s / %u min",
                     ptc_ui_effective_rule_label(decision->effective.source), (unsigned)model->forecast[0].minutes);
        }
        int tag_w = measure_text(plan_tag, 11) + 16;
        UiRect tag_pill = {c1.x + c1.width - tag_w - 16, c1.y + 14, tag_w, 20};
        fill_round_rect(pixels, stride, tag_pill, 4, UI_PAGE);
        draw_rect_outline(pixels, stride, tag_pill, 4, 1, UI_BORDER);
        draw_text_center(pixels, stride, tag_pill, plan_tag, 11, UI_ACCENT);

        detail_balance(p->total_known, p->total_unlimited, p->total_remaining, value, sizeof(value));
        char rem_str[64];
        snprintf(rem_str, sizeof(rem_str), ptc_ui_text(PTC_UI_T_DETAIL_LEFT_LABEL), value);
        draw_text_bold(pixels, stride, c1.x + 44, c1.y + 46, rem_str, 18, UI_ACCENT);

        /* 进度条 */
        UiRect pbar = {c1.x + 44, c1.y + 55, c1.width - 64, 8};
        fill_round_rect(pixels, stride, pbar, 4, UI_RAISED);
        int fill_w = 0;
        if (model->forecast[0].minutes > 0)
            fill_w = (int)((int64_t)model->played_minutes * pbar.width / model->forecast[0].minutes);
        if (fill_w > pbar.width) fill_w = pbar.width;
        if (fill_w > 0) fill_round_rect(pixels, stride, (UiRect){pbar.x, pbar.y, fill_w, pbar.height}, 4, UI_ACCENT);

        char used_str[64], buf_str[64];
        snprintf(used_str, sizeof(used_str),
            ptc_ui_text(model->usage_present ? PTC_UI_T_CONSOLE_USAGE_U_MIN : PTC_UI_T_DETAIL_USED_LABEL), (unsigned)model->played_minutes);
        draw_text(pixels, stride, c1.x + 44, c1.y + 75, used_str, 11, UI_MUTED);
        snprintf(buf_str, sizeof(buf_str), ptc_ui_text(PTC_UI_T_DETAIL_BUFFER_CLAIMABLE_FMT), (unsigned)model->daily_buffer_minutes);
        int buf_w = measure_text(buf_str, 11);
        draw_text(pixels, stride, c1.x + c1.width - 20 - buf_w, c1.y + 75, buf_str, 11, UI_INK);
    }

    /* 1.3 卡片 2: 设备环境与游玩模式 (x = 880, w = 360) - 消除与健康管理的掌机/护眼数字重叠 */
    {
        UiRect c2 = {880, 72, 360, 92};
        fill_round_rect(pixels, stride, c2, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, c2, 10, 1, UI_BORDER);

        if (is_docked) {
            support_draw_tv(pixels, stride, c2.x + 22, c2.y + 25, 8, UI_SUCCESS);
        } else {
            support_draw_controller(pixels, stride, c2.x + 22, c2.y + 25, 8, UI_ACCENT);
        }
        draw_text(pixels, stride, c2.x + 38, c2.y + 25, ptc_ui_text(PTC_UI_T_DOCK_CURRENT_SCREEN), 12, UI_MUTED);

        const char *mode_badge = is_docked
            ? ptc_ui_text(PTC_UI_T_DOCK_TV_BADGE)
            : ptc_ui_text(PTC_UI_T_DOCK_HANDHELD_BADGE);
        uint32_t badge_col = is_docked ? UI_SUCCESS : UI_ACCENT;
        uint32_t badge_bg = is_docked ? UI_SUCCESS_SOFT : UI_ACCENT_SOFT;
        int mb_w = measure_text(mode_badge, 10) + 14;
        UiRect mb_rect = {c2.x + c2.width - mb_w - 16, c2.y + 14, mb_w, 20};
        fill_round_rect(pixels, stride, mb_rect, 4, badge_bg);
        draw_rect_outline(pixels, stride, mb_rect, 4, 1, badge_col);
        draw_text_center(pixels, stride, mb_rect, mode_badge, 10, badge_col);

        /* 核心态势标语 */
        const char *headline = is_docked
            ? ptc_ui_text(PTC_UI_T_DOCK_TV_ACTIVE_NOTE)
            : ptc_ui_text(PTC_UI_T_DOCK_HANDHELD_ACTIVE_NOTE);
        fit_text(fitted, sizeof(fitted), headline, 13, c2.width - 40);
        draw_text_bold(pixels, stride, c2.x + 20, c2.y + 48, fitted, 13, UI_INK);

        /* 引导与健康提示行 */
        const char *guide = is_docked
            ? ptc_ui_text(PTC_UI_T_DETAIL_RULES_TIP_TV_DOCK)
            : ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_NOTE_BAR);
        fit_text(fitted, sizeof(fitted), guide, 11, c2.width - 40);
        draw_text(pixels, stride, c2.x + 20, c2.y + 70, fitted, 11, is_docked ? UI_SUCCESS : UI_MUTED);
    }

    /* 2. 左侧主栏 (x = 40, w = 736) - 全天作息计划与未来推演 */
    /* 2.1 卡片 A: 今日作息 (y = 180, h = 208) */
    {
        UiRect sleep = {40, 180, 736, 208};
        detail_panel(pixels, stride, sleep);
        support_draw_calendar(pixels, stride, sleep.x + 24, sleep.y + 24, 10, UI_ACCENT);
        const char *sleep_title = ptc_ui_text(PTC_UI_T_DETAIL_SLEEP);
        const int sleep_sub_x = sleep.x + 40 + measure_text(sleep_title, 16) + 12;
        char sleep_sub[128];
        fit_text(sleep_sub, sizeof(sleep_sub), ptc_ui_text(PTC_UI_T_DETAIL_SCHEDULE_SUB), 12,
                 sleep.x + sleep.width - 212 - sleep_sub_x);
        draw_text_bold(pixels, stride, sleep.x + 40, sleep.y + 30, sleep_title, 16, UI_INK);
        draw_text(pixels, stride, sleep_sub_x, sleep.y + 30, sleep_sub, 12, UI_MUTED);

        /* 右上图例 */
        uint32_t bedtime_col = is_dark ? UI_RGB(0x384E78) : UI_RGB(0x2C3E60);
        uint32_t bedtime_txt = UI_ON_ACCENT;
        fill_round_rect(pixels, stride, (UiRect){sleep.x + sleep.width - 200, sleep.y + 18, 14, 14}, 3, bedtime_col);
        draw_text(pixels, stride, sleep.x + sleep.width - 180, sleep.y + 30, ptc_ui_text(PTC_UI_T_DETAIL_LEGEND_BEDTIME), 11, UI_MUTED);
        support_fill_circle_mini(pixels, stride, (float)(sleep.x + sleep.width - 80), (float)(sleep.y + 25), 4.5f, UI_ACCENT);
        draw_text(pixels, stride, sleep.x + sleep.width - 70, sleep.y + 30, ptc_ui_text(PTC_UI_T_DETAIL_LEGEND_NOW), 11, UI_MUTED);

        /* 24 小时轨道 */
        UiRect track = {sleep.x + 24, sleep.y + 60, sleep.width - 48, 24};
        fill_round_rect(pixels, stride, track, 6, UI_RAISED);

        for (int i = 0; i < p->sleep_count; ++i) {
            const PtcUiSleepInterval *b = &p->sleep[i];
            int sx = track.x + b->start_second * track.width / 86400;
            int ex = track.x + b->end_second * track.width / 86400;
            int seg_w = ex - sx;
            if (seg_w > 0) {
                uint32_t seg_col = b->skipped ? UI_SUCCESS_SOFT : bedtime_col;
                uint32_t seg_txt = b->skipped ? UI_SUCCESS : bedtime_txt;
                int r = seg_w >= 8 ? 4 : seg_w / 2;
                fill_round_rect(pixels, stride, (UiRect){sx, track.y, seg_w, track.height}, r, seg_col);
                char from[16], to[16], slot[48];
                detail_time(b->start_second, from, sizeof(from));
                detail_time(b->end_second, to, sizeof(to));
                snprintf(slot, sizeof(slot), "%s - %s", from, to);
                draw_text_center(pixels, stride, (UiRect){sx, track.y + 4, seg_w, 16}, slot, 10, seg_txt);
            }
        }

        /* 刻度时间点 */
        const char *ticks[] = {"00:00", "06:00", "09:00", "12:00", "15:00", "18:00", "21:30", "24:00"};
        const int tick_secs[] = {0, 6 * 3600, 9 * 3600, 12 * 3600, 15 * 3600, 18 * 3600, 21 * 3600 + 1800, 24 * 3600};
        for (int i = 0; i < 8; ++i) {
            int tx = track.x + tick_secs[i] * track.width / 86400;
            draw_line(pixels, stride, tx, track.y + track.height, tx, track.y + track.height + 4, 1, UI_MUTED);
            int tw = measure_text(ticks[i], 10);
            int tx_pos = tx - tw / 2;
            if (tx_pos < track.x) tx_pos = track.x;
            if (tx_pos + tw > track.x + track.width) tx_pos = track.x + track.width - tw;
            draw_text(pixels, stride, tx_pos, track.y + track.height + 15, ticks[i], 10, UI_MUTED);
        }

        /* 当前时刻指针与气泡 */
        int nx = track.x + p->now_second * track.width / 86400;
        fill_round_rect(pixels, stride, (UiRect){nx - 22, track.y - 20, 44, 18}, 4, UI_ACCENT);
        draw_line(pixels, stride, nx, track.y - 2, nx, track.y + track.height, 2, UI_ACCENT);
        support_fill_circle_mini(pixels, stride, (float)nx, (float)(track.y + track.height / 2), 3.5f, UI_ON_ACCENT);
        detail_time(p->now_second, text, sizeof(text));
        draw_text_center(pixels, stride, (UiRect){nx - 22, track.y - 19, 44, 16}, text, 10, UI_ON_ACCENT);

        /* 底部信息提示栏 */
        UiRect tip_rect = {sleep.x + 24, sleep.y + 154, sleep.width - 48, 36};
        fill_round_rect(pixels, stride, tip_rect, 6, UI_ACCENT_SOFT);
        support_draw_info(pixels, stride, tip_rect.x + 14, tip_rect.y + 18, 7, UI_ACCENT);
        unsigned bed_start = model->bedtime_policy.week[ptc_weekday_from_day_index(model->day_index)].start_minute;
        unsigned bed_end = model->bedtime_policy.week[ptc_weekday_from_day_index(model->day_index)].end_minute;
        snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_BEDTIME_NOTICE_FMT),
                 bed_start / 60, bed_start % 60, bed_end / 60, bed_end % 60);
        draw_text(pixels, stride, tip_rect.x + 28, tip_rect.y + 22, text, 12, UI_INK);
    }

    /* 2.2 卡片 B: 按当前模式持续使用时预计 (y = 404, h = 212) */
    {
        UiRect prediction = {40, 404, 736, 212};
        detail_panel(pixels, stride, prediction);
        support_draw_play(pixels, stride, prediction.x + 24, prediction.y + 24, 10, UI_ACCENT, UI_ON_ACCENT);
        draw_text_bold(pixels, stride, prediction.x + 40, prediction.y + 30, ptc_ui_text(PTC_UI_T_DETAIL_PREDICTION), 16, UI_INK);
        draw_text(pixels, stride, prediction.x + 40, prediction.y + 50, ptc_ui_text(PTC_UI_T_DETAIL_SIM_SUB), 12, UI_MUTED);

        if (!p->simulation_available) {
            detail_text(pixels, stride, (UiRect){prediction.x + 24, prediction.y + 78, prediction.width - 48, 48},
                        detail_pending_reason(p->pending_reason), 15, UI_WARNING, 2);
        } else if (!p->segment_count) {
            detail_reasons(p->stop_reasons, text, sizeof(text));
            detail_text(pixels, stride, (UiRect){prediction.x + 24, prediction.y + 78, prediction.width - 48, 48},
                        text, 15, UI_DANGER, 2);
        } else {
            UiRect rail = {prediction.x + 24, prediction.y + 102, prediction.width - 48, 18};
            fill_round_rect(pixels, stride, rail, 4, UI_RAISED);
            int range = p->stop_second - p->now_second;
            if (range <= 0) range = 1;

            for (int i = 0; i < p->segment_count; ++i) {
                const PtcUiUsageSegment *seg = &p->segments[i];
                int x = rail.x + (seg->start_second - p->now_second) * rail.width / range;
                int end = rail.x + (seg->end_second - p->now_second) * rail.width / range;
                uint32_t seg_color = seg->resting ? UI_WARNING : UI_ACCENT;
                fill_rect(pixels, stride, (UiRect){x, rail.y, end - x, rail.height}, seg_color);

                /* 气泡标签 */
                if (seg->resting) {
                    char t1[16];
                    detail_time(seg->start_second, t1, sizeof(t1));
                    int bx = x;
                    UiRect bubble = {bx - 36, rail.y - 34, 72, 30};
                    uint32_t b_bg = UI_WARNING_SOFT;
                    uint32_t b_bd = UI_WARNING_BORDER;
                    uint32_t b_fg = UI_WARNING_TEXT;
                    fill_round_rect(pixels, stride, bubble, 5, b_bg);
                    draw_rect_outline(pixels, stride, bubble, 5, 1, b_bd);
                    draw_text_center(pixels, stride, (UiRect){bubble.x, bubble.y + 2, bubble.width, 14}, t1, 10, b_fg);
                    char r_text[32];
                    snprintf(r_text, sizeof(r_text), ptc_ui_text(PTC_UI_T_DETAIL_SIM_REST_FMT), (unsigned)((seg->end_second - seg->start_second) / 60));
                    draw_text_center(pixels, stride, (UiRect){bubble.x, bubble.y + 16, bubble.width, 14}, r_text, 10, b_fg);
                    support_fill_circle_mini(pixels, stride, (float)x, (float)(rail.y + rail.height / 2), 4.5f, b_fg);
                    support_fill_circle_mini(pixels, stride, (float)x, (float)(rail.y + rail.height / 2), 2.0f, UI_PAGE);

                    /* 恢复节点 */
                    char t2[16];
                    detail_time(seg->end_second, t2, sizeof(t2));
                    UiRect b_resume = {end - 28, rail.y - 34, 56, 30};
                    fill_round_rect(pixels, stride, b_resume, 5, UI_ACCENT_SOFT);
                    draw_rect_outline(pixels, stride, b_resume, 5, 1, UI_ACCENT);
                    draw_text_center(pixels, stride, (UiRect){b_resume.x, b_resume.y + 2, b_resume.width, 14}, t2, 10, UI_ACCENT);
                    draw_text_center(pixels, stride, (UiRect){b_resume.x, b_resume.y + 16, b_resume.width, 14},
                                     ptc_ui_text(PTC_UI_T_DETAIL_SIM_RESUME_LABEL), 10, UI_ACCENT);
                    support_fill_circle_mini(pixels, stride, (float)end, (float)(rail.y + rail.height / 2), 4.5f, UI_ACCENT);
                    support_fill_circle_mini(pixels, stride, (float)end, (float)(rail.y + rail.height / 2), 2.0f, UI_PAGE);
                }
            }

            /* 终止节点气泡 */
            char t_stop[16];
            detail_time(p->stop_second, t_stop, sizeof(t_stop));
            UiRect b_stop = {rail.x + rail.width - 56, rail.y - 34, 76, 30};
            fill_round_rect(pixels, stride, b_stop, 5, UI_RAISED);
            draw_rect_outline(pixels, stride, b_stop, 5, 1, UI_MUTED);
            draw_text_center(pixels, stride, (UiRect){b_stop.x, b_stop.y + 2, b_stop.width, 14}, t_stop, 10, UI_INK);
            draw_text_center(pixels, stride, (UiRect){b_stop.x, b_stop.y + 16, b_stop.width, 14},
                             ptc_ui_text(PTC_UI_T_DETAIL_SIM_UNDOCKED_EXHAUSTED), 9, UI_MUTED);

            /* 刻度文字 */
            detail_time(p->now_second, text, sizeof(text));
            draw_text(pixels, stride, rail.x, rail.y + rail.height + 15, text, 10, UI_MUTED);
            draw_text(pixels, stride, rail.x + rail.width - measure_text(t_stop, 10), rail.y + rail.height + 15, t_stop, 10, UI_MUTED);
        }

        /* 底部说明栏 */
        UiRect tip2 = {prediction.x + 24, prediction.y + 156, prediction.width - 48, 42};
        fill_round_rect(pixels, stride, tip2, 6, UI_ACCENT_SOFT);
        support_draw_info(pixels, stride, tip2.x + 14, tip2.y + 16, 7, UI_ACCENT);
        unsigned total_left = p->total_at_stop >= 0 ? (unsigned)p->total_at_stop : 0;
        snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_SIM_FOOTER_FMT), total_left);
        draw_text(pixels, stride, tip2.x + 28, tip2.y + 18, text, 12, UI_INK);
        draw_text(pixels, stride, tip2.x + 28, tip2.y + 35, ptc_ui_text(PTC_UI_T_DETAIL_ESTIMATE), 10, UI_MUTED);
    }

    /* 3. 右侧侧栏 (x = 796, w = 444, 占 38%) - 三大守护策略执行细则 + 规则决策总览 */
    /* 3.1 卡片 C: 健康与安全守护策略看板 (y = 180, h = 302) */
    {
        UiRect c_health = {796, 180, 444, 302};
        detail_panel(pixels, stride, c_health);
        support_draw_heart(pixels, stride, c_health.x + 20, c_health.y + 20, 6, UI_ACCENT);
        draw_text_bold(pixels, stride, c_health.x + 36, c_health.y + 25, ptc_ui_text(PTC_UI_T_DETAIL_HEALTH), 15, UI_INK);
        draw_text(pixels, stride, c_health.x + c_health.width - measure_text(ptc_ui_text(PTC_UI_T_DETAIL_CONDITIONS), 11) - 18,
                  c_health.y + 25, ptc_ui_text(PTC_UI_T_DETAIL_CONDITIONS), 11, UI_MUTED);

        /* 3 项守护条目微卡片 (每项高 66px，y = 216, 290, 364) */
        /* 条目 0: 视力保护 */
        {
            UiRect sub0 = {c_health.x + 14, c_health.y + 36, c_health.width - 28, 66};
            fill_round_rect(pixels, stride, sub0, 6, UI_PAGE);
            draw_rect_outline(pixels, stride, sub0, 6, 1, UI_BORDER);
            uint32_t eye_col = !model->eye_care_policy.enabled ? UI_MUTED : (eye_resting ? UI_DANGER : UI_SUCCESS);
            support_draw_eye(pixels, stride, sub0.x + 18, sub0.y + 18, 7, eye_col);
            draw_text_bold(pixels, stride, sub0.x + 34, sub0.y + 20, ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_EYE), 13, UI_INK);

            const char *badge = !model->eye_care_policy.enabled ? ptc_ui_text(PTC_UI_T_DISABLED_2) :
                (eye_resting ? ptc_ui_text(PTC_UI_T_EYE_CARE_BADGE_RESTING) : ptc_ui_text(PTC_UI_T_DETAIL_TAG_ACTIVE));
            uint32_t badge_fg = eye_col;
            uint32_t badge_bg = !model->eye_care_policy.enabled ? UI_RAISED : (eye_resting ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
            int bw = measure_text(badge, 10) + 12;
            UiRect pill = {sub0.x + sub0.width - bw - 12, sub0.y + 10, bw, 18};
            fill_round_rect(pixels, stride, pill, 4, badge_bg);
            draw_text_center(pixels, stride, pill, badge, 10, badge_fg);

            if (!model->eye_care_policy.enabled) {
                fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_OFF), 11, sub0.width - 48);
                draw_text(pixels, stride, sub0.x + 34, sub0.y + 38, fitted, 11, UI_MUTED);
                fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_IS_CURRENTLY_CLOSED_RULES_CAN_BE_PRESET), 11, sub0.width - 48);
                draw_text(pixels, stride, sub0.x + 34, sub0.y + 54, fitted, 11, UI_MUTED);
            } else {
                snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_EYE_SUB),
                         (unsigned)model->eye_care_used_minutes,
                         (unsigned)model->eye_care_policy.play_minutes,
                         (unsigned)model->eye_care_policy.rest_minutes);
                fit_text(fitted, sizeof(fitted), buf, 11, sub0.width - 48);
                draw_text(pixels, stride, sub0.x + 34, sub0.y + 38, fitted, 11, UI_INK);

                unsigned left_c = p->continuous_seconds > 0 ? (unsigned)(p->continuous_seconds / 60) : 0;
                snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_DETAIL_EYE_CARE_NEED_REST_FMT), left_c);
                fit_text(fitted, sizeof(fitted), buf, 11, sub0.width - 48);
                draw_text(pixels, stride, sub0.x + 34, sub0.y + 54, fitted, 11, eye_resting ? UI_DANGER : UI_MUTED);
            }
        }

        /* 条目 1: 就寝守护 */
        {
            UiRect sub1 = {c_health.x + 14, c_health.y + 110, c_health.width - 28, 66};
            fill_round_rect(pixels, stride, sub1, 6, UI_PAGE);
            draw_rect_outline(pixels, stride, sub1, 6, 1, UI_BORDER);
            uint32_t bed_col = !model->bedtime_policy.enabled ? UI_MUTED :
                (model->bedtime_skipped ? UI_SUCCESS : (model->bedtime_active ? UI_DANGER : UI_ACCENT));
            support_draw_crescent_moon(pixels, stride, sub1.x + 18, sub1.y + 18, 6.0f, 5.0f, 2.0f, -1.0f, bed_col);
            draw_text_bold(pixels, stride, sub1.x + 34, sub1.y + 20, ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_BEDTIME), 13, UI_INK);

            const char *badge = !model->bedtime_policy.enabled ? ptc_ui_text(PTC_UI_T_DISABLED_2) :
                (model->bedtime_skipped ? ptc_ui_text(PTC_UI_T_DETAIL_SKIPPED) :
                 (model->bedtime_active ? ptc_ui_text(PTC_UI_T_DETAIL_TAG_ACTIVE) : ptc_ui_text(PTC_UI_T_DETAIL_TAG_NOT_STARTED)));
            uint32_t badge_fg = bed_col;
            uint32_t badge_bg = !model->bedtime_policy.enabled ? UI_RAISED :
                (model->bedtime_skipped ? UI_SUCCESS_SOFT : (model->bedtime_active ? UI_DANGER_SOFT : UI_RAISED));
            int bw = measure_text(badge, 10) + 12;
            UiRect pill = {sub1.x + sub1.width - bw - 12, sub1.y + 10, bw, 18};
            fill_round_rect(pixels, stride, pill, 4, badge_bg);
            draw_text_center(pixels, stride, pill, badge, 10, badge_fg);

            if (!model->bedtime_policy.enabled) {
                fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_IS_CURRENTLY_CLOSED_RULES_CAN_BE_PRESET), 11, sub1.width - 48);
                draw_text(pixels, stride, sub1.x + 34, sub1.y + 38, fitted, 11, UI_MUTED);
                fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_CLOSED_TODAY_USE_WILL_NOT_BE_RESTRICTED), 11, sub1.width - 48);
                draw_text(pixels, stride, sub1.x + 34, sub1.y + 54, fitted, 11, UI_MUTED);
            } else {
                unsigned b_st = model->bedtime_policy.week[ptc_weekday_from_day_index(model->day_index)].start_minute;
                unsigned b_en = model->bedtime_policy.week[ptc_weekday_from_day_index(model->day_index)].end_minute;
                snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_BEDTIME_SUB),
                         b_st / 60, b_st % 60, b_en / 60, b_en % 60);
                fit_text(fitted, sizeof(fitted), buf, 11, sub1.width - 48);
                draw_text(pixels, stride, sub1.x + 34, sub1.y + 38, fitted, 11, UI_INK);

                const char *bed_note = model->bedtime_skipped ? ptc_ui_text(PTC_UI_T_FOR_EXCEPTIONS_SELECT_SKIP_TONIGHT_BEDTIME_IN) :
                    (model->bedtime_active ? ptc_ui_text(PTC_UI_T_TODAY_S_QUOTA_HAS_BEEN_DETERMINED_CURRENT) :
                     ptc_ui_text(PTC_UI_T_BEDTIME_OPERATES_INDEPENDENTLY));
                fit_text(fitted, sizeof(fitted), bed_note, 11, sub1.width - 48);
                draw_text(pixels, stride, sub1.x + 34, sub1.y + 54, fitted, 11, model->bedtime_active ? UI_DANGER : UI_MUTED);
            }
        }

        /* 条目 2: 掌机模式限额 */
        {
            UiRect sub2 = {c_health.x + 14, c_health.y + 184, c_health.width - 28, 66};
            fill_round_rect(pixels, stride, sub2, 6, UI_PAGE);
            draw_rect_outline(pixels, stride, sub2, 6, 1, UI_BORDER);
            uint32_t dock_col = is_docked ? UI_SUCCESS : (!dock_enabled ? UI_MUTED : (p->undocked_remaining == 0 ? UI_DANGER : UI_ACCENT));
            support_draw_controller(pixels, stride, sub2.x + 18, sub2.y + 18, 7, dock_col);
            draw_text_bold(pixels, stride, sub2.x + 34, sub2.y + 20, ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_DOCK), 13, UI_INK);

            const char *badge = is_docked ? ptc_ui_text(PTC_UI_T_DETAIL_TAG_DOCKED) :
                (!dock_enabled ? ptc_ui_text(PTC_UI_T_DETAIL_OFF) :
                 (p->undocked_remaining == 0 ? ptc_ui_text(PTC_UI_T_DETAIL_SIM_UNDOCKED_EXHAUSTED) : ptc_ui_text(PTC_UI_T_DETAIL_TAG_HANDHELD)));
            uint32_t badge_fg = dock_col;
            uint32_t badge_bg = is_docked ? UI_SUCCESS_SOFT : (!dock_enabled ? UI_RAISED : (p->undocked_remaining == 0 ? UI_DANGER_SOFT : UI_ACCENT_SOFT));
            int bw = measure_text(badge, 10) + 12;
            UiRect pill = {sub2.x + sub2.width - bw - 12, sub2.y + 10, bw, 18};
            fill_round_rect(pixels, stride, pill, 4, badge_bg);
            draw_text_center(pixels, stride, pill, badge, 10, badge_fg);

            if (!dock_enabled) {
                fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_DOCK_OFF), 11, sub2.width - 48);
                draw_text(pixels, stride, sub2.x + 34, sub2.y + 38, fitted, 11, UI_MUTED);
                fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_NOTE_BAR), 11, sub2.width - 48);
                draw_text(pixels, stride, sub2.x + 34, sub2.y + 54, fitted, 11, UI_MUTED);
            } else {
                snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_DOCK_SUB),
                         (unsigned)model->undocked_used_minutes,
                         (unsigned)model->dock_policy.undocked_daily_minutes,
                         (unsigned)model->undocked_remaining_minutes);
                fit_text(fitted, sizeof(fitted), buf, 11, sub2.width - 48);
                draw_text(pixels, stride, sub2.x + 34, sub2.y + 38, fitted, 11, UI_INK);

                const char *dock_note = is_docked ? ptc_ui_text(PTC_UI_T_DOCK_TV_ACTIVE_NOTE) :
                    ptc_ui_text(PTC_UI_T_DETAIL_RULES_TIP_TV_DOCK);
                fit_text(fitted, sizeof(fitted), dock_note, 11, sub2.width - 48);
                draw_text(pixels, stride, sub2.x + 34, sub2.y + 54, fitted, 11, UI_MUTED);
            }
        }

        /* 底部规则说明提示栏 */
        UiRect tip3 = {c_health.x + 14, c_health.y + 258, c_health.width - 28, 30};
        fill_round_rect(pixels, stride, tip3, 5, UI_ACCENT_SOFT);
        support_draw_info(pixels, stride, tip3.x + 14, tip3.y + 15, 6, UI_ACCENT);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_NOTE_BAR), 11, tip3.width - 34);
        draw_text(pixels, stride, tip3.x + 26, tip3.y + 19, fitted, 11, UI_INK);
    }

    /* 3.2 大卡片: 查看决策与规则 (Action 0) (y = 498, h = 118) - 彻底移除卡片D的自我克隆进度条 */
    {
        PtcUiRect btn_target = ptc_ui_home_details_action_rect(model, 0);
        UiRect btn_r = to_uirect(btn_target);
        bool focused = (model->home_details_focus == 0);
        fill_round_rect(pixels, stride, btn_r, 12, UI_ACCENT);
        if (focused) {
            draw_focus_ring(pixels, stride, btn_r, 12);
            draw_rect_outline(pixels, stride, btn_r, 12, 2, UI_ON_ACCENT);
        }

        /* Ⓧ 徽章：手柄直达快捷键提示与高对比选中态 */
        uint32_t badge_bg = focused ? UI_ON_ACCENT : UI_SURFACE;
        uint32_t badge_fg = UI_ACCENT;
        support_fill_circle_mini(pixels, stride, (float)(btn_r.x + 36), (float)(btn_r.y + btn_r.height / 2), 16.0f, badge_bg);
        draw_text_center(pixels, stride, (UiRect){btn_r.x + 22, btn_r.y + btn_r.height / 2 - 12, 28, 24}, "X", 17, badge_fg);

        /* 主标题 */
        draw_text_bold(pixels, stride, btn_r.x + 68, btn_r.y + 32, ptc_ui_text(PTC_UI_T_DETAIL_VIEW_RULES_BTN), 18, UI_ON_ACCENT);

        /* 副标题：当前规则溯源判定 */
        char hit_buf[128];
        snprintf(hit_buf, sizeof(hit_buf), "%s / %u min",
                 ptc_ui_effective_rule_label(decision->effective.source),
                 (unsigned)model->forecast[0].minutes);
        fit_text(fitted, sizeof(fitted), hit_buf, 12, btn_r.width - 68 - 36);
        draw_text(pixels, stride, btn_r.x + 68, btn_r.y + 60, fitted, 12, UI_ON_ACCENT);

        /* 底部流水线与规则优先级说明 */
        char prio_buf[192];
        snprintf(prio_buf, sizeof(prio_buf), "%s", ptc_ui_text(PTC_UI_T_DETAIL_RULES_PRIORITY_NOTE));
        fit_text(fitted, sizeof(fitted), prio_buf, 10, btn_r.width - 68 - 36);
        draw_text(pixels, stride, btn_r.x + 68, btn_r.y + 82, fitted, 10, UI_ON_ACCENT);

        /* 向右箭头 */
        support_draw_chevron_right(pixels, stride, btn_r.x + btn_r.width - 24, btn_r.y + btn_r.height / 2, 7, UI_ON_ACCENT);
    }
}

static void draw_rules_conditions_gate(uint32_t *pixels, uint32_t stride,
                                      const PtcUiModel *model,
                                      const PtcUiTodayDecision *decision,
                                      int x, int y, int full_w, int full_h,
                                      int target_minutes, int played_minutes, int remaining_minutes,
                                      uint8_t target_weekday, bool is_today,
                                      bool total_known, bool total_unlimited,
                                      const PtcUiCondition *conditions)
{
    char text[512], value[64];
    UiRect sec1 = {x, y, full_w, full_h};
    detail_panel(pixels, stride, sec1);

    /* 标题行: 综合把关：现在能不能开机玩 */
    support_fill_circle_mini(pixels, stride, (float)(sec1.x + 22), (float)(sec1.y + 16), 8.5f, UI_ACCENT);
    draw_text_center(pixels, stride, (UiRect){sec1.x + 13, sec1.y + 8, 18, 16}, "2", 11, UI_ON_ACCENT);
    const char *sec1_title = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC1_TITLE);
    int t1_w = measure_text(sec1_title, 14);
    draw_text(pixels, stride, sec1.x + 38, sec1.y + 20, sec1_title, 14, UI_INK);

    const char *sec1_sub = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC1_SUB);
    int sub1_x = sec1.x + 38 + t1_w + 14;
    int max_sub1_w = (sec1.x + sec1.width - 16) - sub1_x;
    if (max_sub1_w > 80) {
        char fitted_sub[256];
        fit_text(fitted_sub, sizeof(fitted_sub), sec1_sub, 12, max_sub1_w);
        draw_text(pixels, stride, sub1_x, sec1.y + 20, fitted_sub, 12, UI_MUTED);
    }

    int card_y1 = sec1.y + 30;
    int gap1 = (full_w > 1100) ? 26 : 16;
    int card_w1 = (full_w - 32 - 3 * gap1) / 4;
    int card_h1 = (full_h >= 210) ? 102 : 94;

    for (int i = 0; i < 4; ++i) {
        int cx = sec1.x + 16 + i * (card_w1 + gap1);
        UiRect c = {cx, card_y1, card_w1, card_h1};
        fill_round_rect(pixels, stride, c, 8, UI_PAGE);
        draw_rect_outline(pixels, stride, c, 8, 1, UI_BORDER);

        int icon_y = c.y + 16;
        if (i == 0) support_draw_clock(pixels, stride, c.x + 16, icon_y, 7, UI_ACCENT);
        else if (i == 1) support_draw_crescent_moon(pixels, stride, c.x + 16, icon_y, 6.0f, 5.0f, 1.8f, -0.8f, UI_ACCENT);
        else if (i == 2) support_draw_eye(pixels, stride, c.x + 16, icon_y, 7, UI_ACCENT);
        else support_draw_controller(pixels, stride, c.x + 16, icon_y, 7, UI_ACCENT);

        draw_text(pixels, stride, c.x + 28, c.y + 20, ptc_ui_text(DETAIL_CONDITION_TITLES[i]), 12, UI_INK);

        /* 卡片 0 右上角增加额度解析提示标签 */
        if (i == 0) {
            const char *hint = ptc_ui_text(PTC_UI_T_DETAIL_RULES_QUOTA_PARSE_HINT);
            int hint_w = measure_text(hint, 10);
            draw_text(pixels, stride, c.x + c.width - hint_w - 10, c.y + 20, hint, 10, UI_ACCENT);
        }

        /* 主指标与次要副文 */
        if (i == 0) {
            if (is_today) {
                detail_balance(total_known, total_unlimited, remaining_minutes, value, sizeof(value));
                char left_msg[64];
                snprintf(left_msg, sizeof(left_msg), ptc_ui_text(PTC_UI_T_DETAIL_LEFT_LABEL), value);
                draw_text(pixels, stride, c.x + 12, c.y + 38, left_msg, 13, UI_ACCENT);
                snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_USED_TOTAL_SUB),
                         (unsigned)played_minutes, (unsigned)target_minutes);
                draw_text(pixels, stride, c.x + 12, c.y + 54, text, 10, UI_MUTED);
            } else {
                detail_balance(total_known, total_unlimited, target_minutes, value, sizeof(value));
                draw_text(pixels, stride, c.x + 12, c.y + 38, value, 13, UI_ACCENT);
                draw_text(pixels, stride, c.x + 12, c.y + 54, ptc_ui_effective_rule_label(decision->effective.source), 10, UI_MUTED);
            }
        } else if (i == 1) {
            unsigned b_st = model->bedtime_policy.week[target_weekday].start_minute;
            unsigned b_en = model->bedtime_policy.week[target_weekday].end_minute;
            char bt_msg[64];
            snprintf(bt_msg, sizeof(bt_msg), ptc_ui_text(PTC_UI_T_DETAIL_RULE_BEDTIME_START), b_st / 60, b_st % 60);
            draw_text(pixels, stride, c.x + 12, c.y + 38, bt_msg, 13, UI_ACCENT);
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_BEDTIME_NEXT), b_en / 60, b_en % 60);
            draw_text(pixels, stride, c.x + 12, c.y + 54, text, 10, UI_MUTED);
        } else if (i == 2) {
            char eye_msg[64];
            snprintf(eye_msg, sizeof(eye_msg), ptc_ui_text(PTC_UI_T_DETAIL_RULE_EYE_PLAYED),
                     (unsigned)(is_today ? model->eye_care_used_minutes : 0), (unsigned)model->eye_care_policy.play_minutes);
            draw_text(pixels, stride, c.x + 12, c.y + 38, eye_msg, 13, UI_ACCENT);
            unsigned left_p = is_today && model->eye_care_policy.play_minutes > model->eye_care_used_minutes ?
                (unsigned)(model->eye_care_policy.play_minutes - model->eye_care_used_minutes) : (unsigned)model->eye_care_policy.play_minutes;
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_EYE_LEFT), left_p, (unsigned)model->eye_care_policy.rest_minutes);
            draw_text(pixels, stride, c.x + 12, c.y + 54, text, 10, UI_MUTED);
        } else {
            const char *dmode = is_today ?
                ptc_ui_text(strcmp(model->operation_mode, "docked") == 0 ? PTC_UI_T_DETAIL_TAG_DOCKED : PTC_UI_T_DETAIL_TAG_HANDHELD) :
                ptc_ui_text(model->dock_policy.force_docked ? PTC_UI_T_DETAIL_TAG_DOCKED : PTC_UI_T_DETAIL_TAG_HANDHELD);
            draw_text(pixels, stride, c.x + 12, c.y + 38, dmode, 13, UI_ACCENT);
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_DOCK_REMAIN),
                     (unsigned)(is_today ? model->undocked_remaining_minutes : model->dock_policy.undocked_daily_minutes));
            draw_text(pixels, stride, c.x + 12, c.y + 54, text, 10, UI_MUTED);
        }

        /* 胶囊状态 */
        UiRect pass_pill = {c.x + 10, c.y + card_h1 - 26, c.width - 20, 20};
        PtcUiCondition cond = conditions[i];
        bool is_pass = (cond == PTC_UI_CONDITION_PASS || cond == PTC_UI_CONDITION_WAIVED);
        bool is_block = (cond == PTC_UI_CONDITION_BLOCK);
        uint32_t p_bg = is_block ? UI_DANGER_SOFT : (is_pass ? UI_SUCCESS_SOFT : UI_RAISED);
        uint32_t p_bd = is_block ? UI_DANGER : (is_pass ? UI_SUCCESS : UI_BORDER);
        uint32_t p_fg = is_block ? UI_DANGER : (is_pass ? UI_SUCCESS : UI_MUTED);

        fill_round_rect(pixels, stride, pass_pill, 4, p_bg);
        draw_rect_outline(pixels, stride, pass_pill, 4, 1, p_bd);
        if (is_pass) {
            support_draw_checkmark(pixels, stride, pass_pill.x + 10, pass_pill.y + 10, 4, UI_SUCCESS, UI_ON_ACCENT);
        }
        draw_text_center(pixels, stride, pass_pill, detail_condition_label(cond), 11, p_fg);
    }

    bool any_block = (conditions[0] == PTC_UI_CONDITION_BLOCK || conditions[1] == PTC_UI_CONDITION_BLOCK ||
                      conditions[2] == PTC_UI_CONDITION_BLOCK || conditions[3] == PTC_UI_CONDITION_BLOCK);

    int mid_x = sec1.x + sec1.width / 2;
    int gate_w = (full_w > 1000) ? 860 : 640;
    int gate_h = (full_h >= 210) ? 28 : 24;
    int gate_y = sec1.y + full_h - ((full_h >= 210) ? 58 : 44);
    UiRect gate = {mid_x - gate_w / 2, gate_y, gate_w, gate_h};

    int bus_y = (card_y1 + card_h1 + gate_y) / 2;
    int lx1 = sec1.x + 16 + card_w1 / 2;
    int lx2 = sec1.x + 16 + 3 * (card_w1 + gap1) + card_w1 / 2;

    /* 绘制各卡片向下的动态引线与道闸 */
    for (int i = 0; i < 4; ++i) {
        int cx = sec1.x + 16 + i * (card_w1 + gap1);
        int bx = cx + card_w1 / 2;
        int by1 = card_y1 + card_h1;
        PtcUiCondition cond = conditions[i];
        bool is_pass = (cond == PTC_UI_CONDITION_PASS || cond == PTC_UI_CONDITION_WAIVED);
        bool is_block = (cond == PTC_UI_CONDITION_BLOCK);

        if (is_block) {
            /* 红色阻断引线 */
            draw_line(pixels, stride, bx, by1, bx, bus_y, 2, UI_DANGER);
            /* 醒目的道闸断路阻断符号 (红色小圆徽章内嵌白色 X) */
            int sym_y = (by1 + bus_y) / 2;
            support_fill_circle_mini(pixels, stride, (float)bx, (float)sym_y, 6.0f, UI_DANGER);
            draw_line(pixels, stride, bx - 3, sym_y - 3, bx + 3, sym_y + 3, 2, UI_ON_ACCENT);
            draw_line(pixels, stride, bx - 3, sym_y + 3, bx + 3, sym_y - 3, 2, UI_ON_ACCENT);
        } else if (is_pass) {
            /* 绿色放行引线 */
            draw_line(pixels, stride, bx, by1, bx, bus_y, 2, UI_SUCCESS);
            int sym_y = (by1 + bus_y) / 2;
            support_fill_circle_mini(pixels, stride, (float)bx, (float)sym_y, 4.0f, UI_SUCCESS);
        } else {
            /* 未开启 / 离线淡灰色弱化线 */
            draw_line(pixels, stride, bx, by1, bx, bus_y, 1, UI_BORDER);
        }
    }

    /* 汇聚横线 (根据是否有阻断决定颜色) */
    uint32_t bus_color = any_block ? UI_DANGER : UI_SUCCESS;
    draw_line(pixels, stride, lx1, bus_y, lx2, bus_y, 2, bus_color);

    /* 从总线中央注入总闸门的纵向箭头 */
    draw_line(pixels, stride, mid_x, bus_y, mid_x, gate.y, 2, bus_color);
    draw_line(pixels, stride, mid_x - 4, gate.y - 4, mid_x, gate.y, 2, bus_color);
    draw_line(pixels, stride, mid_x + 4, gate.y - 4, mid_x, gate.y, 2, bus_color);

    /* 居中横条 (总闸门) */
    fill_round_rect(pixels, stride, gate, 6, any_block ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
    draw_rect_outline(pixels, stride, gate, 6, 1, any_block ? UI_DANGER : UI_SUCCESS);
    if (!any_block) {
        support_draw_checkmark(pixels, stride, gate.x + 18, gate.y + gate_h / 2, 5, UI_SUCCESS, UI_ON_ACCENT);
        draw_text_center(pixels, stride, gate, ptc_ui_text(PTC_UI_T_DETAIL_RULES_GATE_PASS), 12, UI_INK);
    } else {
        draw_text_center(pixels, stride, gate, ptc_ui_text(PTC_UI_T_DETAIL_BLOCKED), 12, UI_DANGER);
    }

    /* 底部说明 */
    draw_text_center(pixels, stride, (UiRect){sec1.x, gate_y + gate_h + 6, sec1.width, 16},
                     ptc_ui_text(PTC_UI_T_DETAIL_RULES_CONVERGE_SUB), 11, UI_MUTED);
}

static void draw_rules_priority_pipeline(uint32_t *pixels, uint32_t stride,
                                        const PtcUiModel *model,
                                        const PtcUiTodayDecision *decision,
                                        int x, int y, int full_w, int full_h,
                                        int target_minutes, int played_minutes, int remaining_minutes,
                                        bool is_today, bool total_known, bool total_unlimited)
{
    (void)model;
    char text[512], value[64];
    UiRect sec2 = {x, y, full_w, full_h};
    detail_panel(pixels, stride, sec2);

    /* 标题行: 先算时间：今天总共能玩多久 */
    support_fill_circle_mini(pixels, stride, (float)(sec2.x + 22), (float)(sec2.y + 16), 8.5f, UI_ACCENT);
    draw_text_center(pixels, stride, (UiRect){sec2.x + 13, sec2.y + 8, 18, 16}, "1", 11, UI_ON_ACCENT);
    const char *sec2_title = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC2_TITLE);
    int t2_w = measure_text(sec2_title, 14);
    draw_text(pixels, stride, sec2.x + 38, sec2.y + 20, sec2_title, 14, UI_INK);

    const char *sec2_sub = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC2_SUB);
    int sub2_x = sec2.x + 38 + t2_w + 14;
    int max_sub2_w = (sec2.x + sec2.width - 16) - sub2_x;
    if (max_sub2_w > 80) {
        char fitted_sub[256];
        fit_text(fitted_sub, sizeof(fitted_sub), sec2_sub, 12, max_sub2_w);
        draw_text(pixels, stride, sub2_x, sec2.y + 20, fitted_sub, 12, UI_MUTED);
    }

    /* 4 张优先级规则候选卡片 */
    const PtcUiDecisionStep *steps[] = {
        &decision->today_override, &decision->scheduled_override,
        &decision->holiday, &decision->weekly
    };
    const PtcUiTextId card_titles[] = {
        PTC_UI_T_TODAY_S_ADJUSTMENT,
        PTC_UI_T_SPECIFIED_DATE_QUOTA,
        PTC_UI_T_HOLIDAY,
        PTC_UI_T_WEEKLY_PLAN
    };
    const PtcUiTextId card_descs[] = {
        PTC_UI_T_DETAIL_RULES_TEMP_DESC,
        PTC_UI_T_DETAIL_RULES_SPEC_DESC,
        PTC_UI_T_DETAIL_RULES_HOLIDAY_DESC,
        PTC_UI_T_DETAIL_RULES_WEEKLY_PLAN_DESC
    };

    int selected_idx = -1;
    for (int i = 0; i < 4; ++i) {
        if (steps[i]->state == PTC_UI_DECISION_SELECTED) {
            selected_idx = i;
            break;
        }
    }

    int card_y2 = sec2.y + 30;
    int gap2 = (full_w > 1100) ? 26 : 16;
    int card_w2 = (full_w - 32 - 3 * gap2) / 4;
    int card_h2 = (full_h >= 200) ? 96 : 88;

    for (int i = 0; i < 4; ++i) {
        int cx = sec2.x + 16 + i * (card_w2 + gap2);
        UiRect c = {cx, card_y2, card_w2, card_h2};
        bool is_selected = (i == selected_idx);
        bool is_overridden = (selected_idx >= 0 && i > selected_idx);

        if (is_selected) {
            fill_round_rect(pixels, stride, c, 8, UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, c, 8, 2, UI_SUCCESS);
        } else {
            fill_round_rect(pixels, stride, c, 8, UI_PAGE);
            draw_rect_outline(pixels, stride, c, 8, 1, UI_BORDER);
        }

        if (i == 2) support_draw_gift(pixels, stride, c.x + 16, c.y + 16, 7, is_selected ? UI_SUCCESS : UI_MUTED);
        else support_draw_calendar(pixels, stride, c.x + 16, c.y + 16, 7, is_selected ? UI_SUCCESS : UI_MUTED);

        /* 标题 */
        char prio_title[64];
        snprintf(prio_title, sizeof(prio_title), "[%d] %s", i + 1, ptc_ui_text(card_titles[i]));
        draw_text(pixels, stride, c.x + 28, c.y + 20, prio_title, 12, is_overridden ? UI_MUTED : UI_INK);

        /* 状态胶囊 */
        if (is_selected) {
            UiRect pill = {c.x + 12, c.y + 36, c.width - 24, 23};
            fill_round_rect(pixels, stride, pill, 4, UI_SUCCESS);
            char val[32];
            format_decision_rule(steps[i], val, sizeof(val));
            char ptxt[48];
            snprintf(ptxt, sizeof(ptxt), ptc_ui_text(PTC_UI_T_DETAIL_EFFECTIVE_TAG), val);
            draw_text_center(pixels, stride, pill, ptxt, 11, UI_ON_ACCENT);
        } else {
            const char *st_lbl;
            if (i == 0 && !is_today) {
                st_lbl = ptc_ui_text(PTC_UI_T_REASON_TODAY_ONLY);
            } else {
                st_lbl = ptc_ui_decision_state_label(steps[i]->state);
            }
            UiRect pill = {c.x + 12, c.y + 36, c.width - 24, 23};
            fill_round_rect(pixels, stride, pill, 4, UI_RAISED);
            draw_rect_outline(pixels, stride, pill, 4, 1, UI_BORDER);
            draw_text_center(pixels, stride, pill, st_lbl, 11, UI_MUTED);
        }

        /* 底部说明 (基线距卡片底部合理留白，避免与上方状态胶囊挤压) */
        int desc_baseline = c.y + card_h2 - ((full_h >= 200) ? 14 : 11);
        draw_text(pixels, stride, c.x + 12, desc_baseline, ptc_ui_text(card_descs[i]), 10, UI_MUTED);

        /* 串行优先级流向箭头与阻断屏障 (节点间横向连接) */
        if (i < 3) {
            int x_start = c.x + c.width + 3;
            int x_end = c.x + c.width + gap2 - 3;
            int ay = c.y + 47; /* 精确对齐状态胶囊水平中心线 */
            int ax_mid = (x_start + x_end) / 2;

            if (selected_idx >= 0 && i == selected_idx) {
                /* 关键设计：命中规则处切断后续流向，绘制鲜明的阻断截断屏障 */
                draw_line(pixels, stride, x_start, ay, ax_mid - 2, ay, 2, UI_MUTED);
                /* 阻断双竖线屏障 (红/灰) */
                draw_line(pixels, stride, ax_mid - 2, ay - 8, ax_mid - 2, ay + 8, 2, UI_DANGER);
                draw_line(pixels, stride, ax_mid + 2, ay - 8, ax_mid + 2, ay + 8, 2, UI_MUTED);
            } else if (selected_idx < 0 || i < selected_idx) {
                /* 前序规则未命中，顺畅流向下一级优先级 */
                draw_line(pixels, stride, x_start, ay, x_end, ay, 2, UI_MUTED);
                draw_line(pixels, stride, x_end - 4, ay - 4, x_end, ay, 2, UI_MUTED);
                draw_line(pixels, stride, x_end - 4, ay + 4, x_end, ay, 2, UI_MUTED);
            } else {
                /* 已被高优先级覆盖的后续卡片，弱化浅线表示未激活 */
                draw_line(pixels, stride, x_start, ay, x_end, ay, 1, UI_BORDER);
            }
        }
    }

    /* 居中额度结果判定横条 (宽度延伸以覆盖全部候选卡片的投影点) */
    int mid_x = sec2.x + sec2.width / 2;
    int res_w = (full_w > 1000) ? 1040 : 920;
    int res_h = (full_h >= 200) ? 28 : 24;
    int res_y = sec2.y + full_h - ((full_h >= 200) ? 50 : 42);
    UiRect r_bar = {mid_x - res_w / 2, res_y, res_w, res_h};
    fill_round_rect(pixels, stride, r_bar, 6, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, r_bar, 6, 1, UI_ACCENT);

    detail_balance(total_known, total_unlimited, target_minutes, value, sizeof(value));
    char used_left[64];
    if (is_today) {
        snprintf(used_left, sizeof(used_left), ptc_ui_text(PTC_UI_T_DETAIL_RULES_USED_LEFT_FMT),
                 (unsigned)played_minutes, (unsigned)remaining_minutes);
        snprintf(text, sizeof(text), "%s: %s (%s / %s)",
                 ptc_ui_text(PTC_UI_T_DETAIL_RULES_CONFIRM_TOTAL), value,
                 ptc_ui_effective_rule_label(decision->effective.source), used_left);
    } else {
        snprintf(text, sizeof(text), "%s: %s (%s)",
                 ptc_ui_text(PTC_UI_T_DETAIL_RULES_CONFIRM_TOTAL), value,
                 ptc_ui_effective_rule_label(decision->effective.source));
    }
    draw_text_center(pixels, stride, r_bar, text, 12, UI_ACCENT);

    /* 纵向导通线条：仅从生效卡片引出醒目纵线注入 r_bar */
    if (selected_idx >= 0 && selected_idx < 4) {
        int sel_x = sec2.x + 16 + selected_idx * (card_w2 + gap2) + card_w2 / 2;
        int by1 = card_y2 + card_h2;
        int by2 = r_bar.y;
        /* 粗线条导通 */
        draw_line(pixels, stride, sel_x, by1, sel_x, by2, 2, UI_SUCCESS);
        /* 向下箭头落入 r_bar */
        draw_line(pixels, stride, sel_x - 5, by2 - 5, sel_x, by2, 2, UI_SUCCESS);
        draw_line(pixels, stride, sel_x + 5, by2 - 5, sel_x, by2, 2, UI_SUCCESS);
        /* 中间绘制圆形勾选徽章 */
        int sym_y = (by1 + by2) / 2;
        support_fill_circle_mini(pixels, stride, (float)sel_x, (float)sym_y, 6.5f, UI_SUCCESS);
        support_draw_checkmark(pixels, stride, sel_x, sym_y, 4, UI_SUCCESS, UI_ON_ACCENT);
    }

    /* 未选中的其他卡片绘制极淡的未连通短截线 */
    for (int i = 0; i < 4; ++i) {
        if (i == selected_idx) continue;
        int bx = sec2.x + 16 + i * (card_w2 + gap2) + card_w2 / 2;
        int by1 = card_y2 + card_h2;
        draw_line(pixels, stride, bx, by1, bx, by1 + 6, 1, UI_BORDER);
        draw_line(pixels, stride, bx - 3, by1 + 6, bx + 3, by1 + 6, 1, UI_BORDER);
    }

    /* 底部说明 */
    draw_text_center(pixels, stride, (UiRect){sec2.x, res_y + res_h + 6, sec2.width, 16},
                     ptc_ui_text(PTC_UI_T_DETAIL_RULES_PRIORITY_NOTE), 11, UI_MUTED);
}

static void draw_today_rules(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                             const PtcUiTodayProjection *p, const PtcUiTodayDecision *decision)
{
    PtcUiRect body = ptc_ui_home_details_body_rect();
    ui_set_vertical_clip(body.y, body.y + body.h);

    /* 1. 顶部全局提示横幅 (y = 72, h = 26) */
    {
        UiRect banner = {40, 72, 1200, 26};
        fill_round_rect(pixels, stride, banner, 8, UI_SUCCESS_SOFT);
        draw_rect_outline(pixels, stride, banner, 8, 1, UI_SUCCESS);
        support_draw_checkmark(pixels, stride, banner.x + 22, banner.y + 13, 6, UI_SUCCESS, UI_ON_ACCENT);
        draw_text(pixels, stride, banner.x + 38, banner.y + 18,
                  ptc_ui_text(PTC_UI_T_DETAIL_RULES_GLOBAL_BANNER), 13, UI_INK);
    }

    /* 2. 区块 ① 先算时间：今天总共能玩多久 (y = 106, h = 214) */
    draw_rules_priority_pipeline(pixels, stride, model, decision, 40, 106, 1200, 214,
                                 (int)model->forecast[0].minutes, (int)model->played_minutes,
                                 p->total_remaining, true, p->total_known, p->total_unlimited);

    /* 3. 中间因果流向提示：从步骤①输出注入步骤②门禁 (y = 320 ~ 342) */
    {
        int mid_x = 40 + 1200 / 2;
        draw_line(pixels, stride, mid_x, 320, mid_x, 342, 2, UI_ACCENT);
        draw_line(pixels, stride, mid_x - 4, 338, mid_x, 342, 2, UI_ACCENT);
        draw_line(pixels, stride, mid_x + 4, 338, mid_x, 342, 2, UI_ACCENT);

        const char *flow_txt = ptc_ui_text(PTC_UI_T_DETAIL_RULES_FLOW_INTO_GATE);
        int flow_w = measure_text(flow_txt, 11);
        UiRect flow_pill = {mid_x - (flow_w + 20) / 2, 322, flow_w + 20, 18};
        fill_round_rect(pixels, stride, flow_pill, 5, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, flow_pill, 5, 1, UI_ACCENT);
        draw_text_center(pixels, stride, flow_pill, flow_txt, 11, UI_ACCENT);
    }

    /* 4. 区块 ② 综合把关：现在能不能开机玩 (y = 342, h = 232) */
    draw_rules_conditions_gate(pixels, stride, model, decision, 40, 342, 1200, 232,
                               (int)model->forecast[0].minutes, (int)model->played_minutes,
                               p->total_remaining, ptc_weekday_from_day_index(model->day_index),
                               true, p->total_known, p->total_unlimited, p->conditions);

    /* 5. 底部规则常识与豁免精要栏 (y = 582, h = 36) */
    {
        UiRect footer_bar = {40, 582, 1200, 36};
        fill_round_rect(pixels, stride, footer_bar, 6, UI_PAGE);
        draw_rect_outline(pixels, stride, footer_bar, 6, 1, UI_BORDER);

        int col_w = (footer_bar.width - 24) / 3;
        const PtcUiTextId tips[] = {
            PTC_UI_T_DETAIL_RULES_TIP_INDEPENDENT,
            PTC_UI_T_DETAIL_RULES_TIP_OVERTIME,
            PTC_UI_T_DETAIL_RULES_TIP_TV_DOCK
        };

        for (int i = 0; i < 3; ++i) {
            int col_x = footer_bar.x + 12 + i * col_w;
            if (i > 0) {
                draw_line(pixels, stride, col_x - 6, footer_bar.y + 6, col_x - 6, footer_bar.y + footer_bar.height - 6, 1, UI_BORDER);
            }
            char tip_buf[192];
            fit_text(tip_buf, sizeof(tip_buf), ptc_ui_text(tips[i]), 11, col_w - 14);
            draw_text(pixels, stride, col_x, footer_bar.y + 22, tip_buf, 11, UI_MUTED);
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
    draw_dialog_shell(pixels, stride, &shell, &dialog, 1240, 684);
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

    /* 顶部标题与面包屑 */
    const char *details_title = ptc_ui_text(model->home_details_page == 0
        ? PTC_UI_T_TODAY_S_SCHEDULE_DETAILS : PTC_UI_T_DETAIL_RULES);
    const int details_sub_x = 44 + measure_text(details_title, 24) + 18;
    const int details_sub_width = 980 - details_sub_x;
    char fitted_sub[128];
    draw_text_bold(pixels, stride, 44, 52, details_title, 24, UI_INK);
    if (model->home_details_page == 0) {
        char sub[128];
        snprintf(sub, sizeof(sub), "%s / %s", ptc_ui_text(PTC_UI_T_DETAIL_PLANNING), ptc_ui_text(PTC_UI_T_DETAIL_PREVIEW_SAMPLE));
        fit_text(fitted_sub, sizeof(fitted_sub), sub, 13, details_sub_width);
        draw_text(pixels, stride, details_sub_x, 52, fitted_sub, 13, UI_MUTED);
    } else {
        char sub[128];
        snprintf(sub, sizeof(sub), "%s / %s",
                 ptc_ui_text(PTC_UI_T_TODAY_S_SCHEDULE_DETAILS), ptc_ui_text(PTC_UI_T_DETAIL_RULES));
        fit_text(fitted_sub, sizeof(fitted_sub), sub, 12, details_sub_width);
        draw_text(pixels, stride, details_sub_x, 44, fitted_sub, 12, UI_MUTED);
        fit_text(fitted_sub, sizeof(fitted_sub), ptc_ui_text(PTC_UI_T_DETAIL_PREVIEW_SAMPLE), 11, details_sub_width);
        draw_text(pixels, stride, details_sub_x, 60, fitted_sub, 11, UI_MUTED);
    }

    format_status_age(model, age, sizeof(age));
    char age_label[128];
    snprintf(age_label, sizeof(age_label), ptc_ui_text(PTC_UI_T_DETAIL_UPDATED_AGE), age);
    draw_text_center(pixels, stride, (UiRect){dialog.x + dialog.width - 280, dialog.y + 24, 260, 36}, projection.fresh ? age_label :
        ptc_ui_text(PTC_UI_T_DETAIL_PENDING), 13, projection.fresh ? UI_MUTED : UI_WARNING);

    if (model->home_details_page == 0) draw_today_plan(pixels, stride, model, &projection, &decision);
    else draw_today_rules(pixels, stride, model, &projection, &decision);

    /* 底部按钮 */
    const char *back_label = ptc_ui_text(
        model->home_details_page == 1
            ? (model->home_details_entry_page == 1
                ? (model->parent_page == PTC_UI_PARENT_PLAN ? PTC_UI_T_B_BACK_TO_PLANS : PTC_UI_T_DETAIL_BACK_TODAY)
                : PTC_UI_T_DETAIL_BACK)
            : (model->view == PTC_UI_CHILD ? PTC_UI_T_DETAIL_BACK_CHILD : PTC_UI_T_DETAIL_BACK_TODAY));
    home_button(pixels, stride, ptc_ui_home_details_action_rect(model, 2), back_label,
        model->home_details_focus == 2, false, false);
    home_button(pixels, stride, ptc_ui_home_details_action_rect(model, 1), ptc_ui_text(PTC_UI_T_Y_REFRESH),
        model->home_details_focus == 1, false, model->waiting);

    /* 底部右侧文字 */
    char footer[384];
    if (model->usage_direct_present || model->usage_summary_available) {
        char direct[192], legacy[192];
        ptc_ui_format_usage_history(model, true, direct, sizeof(direct));
        ptc_ui_format_usage_history(model, false, legacy, sizeof(legacy));
        snprintf(footer, sizeof(footer), "%.191s\n%.191s", direct, legacy);
    } else snprintf(footer, sizeof(footer), "%s%s%s", model->home_details_page == 0 ? ptc_ui_text(PTC_UI_T_DETAIL_HEALTH_NOTE) : "",
        model->home_details_page == 0 ? "\n" : "", ptc_ui_text(PTC_UI_T_DETAIL_ESTIMATE));
    detail_text(pixels, stride, (UiRect){470, 630, 750, 42}, footer, 11, UI_MUTED, 2);
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
    int64_t now = ptc_ui_render_now();
    bool fresh = ptc_ui_status_is_fresh(model, now);
    char age_buf[64], date_buf[32], sub[128], date_sub[128], cal_desc[256];

    PtcUiModel shell = *model;
    shell.overlay_title[0] = shell.overlay_body[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell, &dialog, 1120, 640);

    ptc_ui_build_day_decision(model, PTC_UI_PLAN_SAVED, target_day_index, now, &decision);

    /* 1. 顶部标题与面包屑 */
    draw_text_bold(pixels, stride, 114, 82, ptc_ui_text(PTC_UI_T_DETAIL_RULES), 24, UI_INK);

    const char *day_prefix = is_today ? ptc_ui_text(PTC_UI_T_TODAY) :
        (offset == 1 ? ptc_ui_text(PTC_UI_T_TOMORROW) : "");
    if (day_prefix[0]) {
        snprintf(sub, sizeof(sub), "%s / %s %s", ptc_ui_text(PTC_UI_T_TIME_PLANS),
                 day_prefix, ptc_ui_weekday_label(weekday));
    } else {
        snprintf(sub, sizeof(sub), "%s / %s (D+%d)", ptc_ui_text(PTC_UI_T_TIME_PLANS),
                 ptc_ui_weekday_label(weekday), offset);
    }
    draw_text(pixels, stride, 240, 72, sub, 12, UI_MUTED);

    bool covered = false;
    PtcCalendarDayType day_type = ptc_holiday_calendar_classify_in(model->calendar, target_day_index, &covered);
    const char *type_label = (covered && day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY) ? ptc_ui_text(PTC_UI_T_STATUTORY_HOLIDAYS) :
        ((covered && day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY) ? ptc_ui_text(PTC_UI_T_ADJUSTED_WORKING_DAYS) :
         ((weekday == 0 || weekday == 6) ? ptc_ui_text(PTC_UI_T_ORDINARY_WEEKEND) : ptc_ui_text(PTC_UI_T_ORDINARY_WORKING_DAYS)));

    ptc_format_date(target_day_index, date_buf);
    snprintf(date_sub, sizeof(date_sub), "%s  |  %s", date_buf, type_label);
    draw_text(pixels, stride, 240, 88, date_sub, 11,
              (covered && day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY) ? UI_WARNING :
              ((covered && day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY) ? UI_ACCENT : UI_MUTED));

    /* 顶部 7 天分页胶囊: < D+%d / 7天预测 > */
    UiRect prev_rect = to_uirect(ptc_ui_day_decision_prev_rect());
    UiRect next_rect = to_uirect(ptc_ui_day_decision_next_rect());
    UiRect pag_pill = {prev_rect.x, prev_rect.y, next_rect.x + next_rect.width - prev_rect.x, prev_rect.height};
    fill_round_rect(pixels, stride, pag_pill, 6, UI_RAISED);
    draw_rect_outline(pixels, stride, pag_pill, 6, 1, UI_BORDER);
    draw_text_center(pixels, stride, prev_rect, "<", 14, offset > 0 ? UI_ACCENT : UI_MUTED);
    char pag_txt[64];
    snprintf(pag_txt, sizeof(pag_txt), ptc_ui_text(PTC_UI_T_DAY_DECISION_PAGINATION), offset);
    draw_text_center(pixels, stride, (UiRect){prev_rect.x + prev_rect.width, prev_rect.y,
                     next_rect.x - (prev_rect.x + prev_rect.width), prev_rect.height}, pag_txt, 11, UI_INK);
    draw_text_center(pixels, stride, next_rect, ">", 14, offset < (int)PTC_RESULT_FORECAST_DAYS - 1 ? UI_ACCENT : UI_MUTED);

    /* 状态新鲜度 */
    format_status_age(model, age_buf, sizeof(age_buf));
    char age_label[128];
    snprintf(age_label, sizeof(age_label), ptc_ui_text(PTC_UI_T_DETAIL_UPDATED_AGE), age_buf);
    draw_text_center(pixels, stride, (UiRect){906, 56, 260, 36}, fresh ? age_label :
        ptc_ui_text(PTC_UI_T_DETAIL_PENDING), 13, fresh ? UI_MUTED : UI_WARNING);

    /* 2. 顶部全局提示横幅 (y = 110, h = 28) */
    UiRect banner = {108, 110, 1064, 28};
    fill_round_rect(pixels, stride, banner, 8, fresh ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
    draw_rect_outline(pixels, stride, banner, 8, 1, fresh ? UI_SUCCESS : UI_WARNING);
    if (fresh) {
        support_draw_checkmark(pixels, stride, banner.x + 22, banner.y + 14, 7, UI_SUCCESS, UI_ON_ACCENT);
        draw_text_bold(pixels, stride, banner.x + 36, banner.y + 19,
                       ptc_ui_text(is_today ? PTC_UI_T_DETAIL_RULES_GLOBAL_BANNER : PTC_UI_T_DETAIL_RULES_FORECAST_BANNER),
                       13, UI_INK);
    } else {
        support_draw_info(pixels, stride, banner.x + 22, banner.y + 14, 7, UI_WARNING);
        draw_text_bold(pixels, stride, banner.x + 36, banner.y + 19,
                       ptc_ui_text(PTC_UI_T_RULES_PENDING_CONFIRMATION), 13, UI_WARNING);
    }

    /* 3. 准备额度参数与条件 */
    int target_minutes = decision.effective.rule.minutes;
    bool total_unlimited = (decision.effective.rule.mode == PTC_RULE_MODE_UNLIMITED);
    bool total_known = fresh;
    int played_minutes = is_today ? model->played_minutes : 0;
    int remaining_minutes = is_today ? model->remaining_minutes : target_minutes;

    PtcUiCondition conditions[4];
    if (is_today) {
        PtcUiTodayProjection p;
        int second = ptc_ui_render_minute_of_day(now) * 60;
        ptc_ui_project_today(model, now, model->day_index, second, &p);
        memcpy(conditions, p.conditions, sizeof(conditions));
    } else {
        conditions[0] = (!fresh) ? PTC_UI_CONDITION_UNKNOWN :
            (total_unlimited || target_minutes > 0 ? PTC_UI_CONDITION_PASS : PTC_UI_CONDITION_BLOCK);
        conditions[1] = !model->bedtime_policy.enabled ? PTC_UI_CONDITION_OFF : PTC_UI_CONDITION_PASS;
        conditions[2] = !model->eye_care_policy.enabled ? PTC_UI_CONDITION_OFF : PTC_UI_CONDITION_PASS;
        conditions[3] = (!model->dock_policy.undocked_limit_enabled && !model->dock_policy.force_docked)
            ? PTC_UI_CONDITION_OFF : PTC_UI_CONDITION_PASS;
    }

    /* 4. 区块 ① 先算时间：今天总共能玩多久 (y = 140, h = 184) */
    draw_rules_priority_pipeline(pixels, stride, model, &decision, 108, 140, 1064, 184,
                                 target_minutes, played_minutes, remaining_minutes,
                                 is_today, total_known, total_unlimited);

    /* 5. 中间因果流向提示：从步骤①输出注入步骤②门禁 (y = 325, h = 18) */
    {
        int mid_x = 108 + 1064 / 2;
        draw_line(pixels, stride, mid_x, 324, mid_x, 342, 2, UI_ACCENT);
        draw_line(pixels, stride, mid_x - 4, 338, mid_x, 342, 2, UI_ACCENT);
        draw_line(pixels, stride, mid_x + 4, 338, mid_x, 342, 2, UI_ACCENT);

        const char *flow_txt = ptc_ui_text(PTC_UI_T_DETAIL_RULES_FLOW_INTO_GATE);
        int flow_w = measure_text(flow_txt, 11);
        UiRect flow_pill = {mid_x - (flow_w + 20) / 2, 325, flow_w + 20, 18};
        fill_round_rect(pixels, stride, flow_pill, 5, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, flow_pill, 5, 1, UI_ACCENT);
        draw_text_center(pixels, stride, flow_pill, flow_txt, 11, UI_ACCENT);
    }

    /* 6. 区块 ② 综合把关：现在能不能开机玩 (y = 344, h = 186) */
    draw_rules_conditions_gate(pixels, stride, model, &decision, 108, 344, 1064, 186,
                               target_minutes, played_minutes, remaining_minutes,
                               weekday, is_today, total_known, total_unlimited, conditions);

    /* 7. 底部说明卡片: 日历属性解读 (y = 538, h = 34) */
    UiRect cal_card = {108, 538, 1064, 34};
    fill_round_rect(pixels, stride, cal_card, 6, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, cal_card, 6, 1, UI_BORDER);
    support_draw_calendar(pixels, stride, cal_card.x + 18, cal_card.y + 17, 7, UI_ACCENT);
    draw_text(pixels, stride, cal_card.x + 34, cal_card.y + 21, ptc_ui_text(PTC_UI_T_CALENDAR_EXPLAIN_TITLE), 11, UI_INK);
    draw_line(pixels, stride, cal_card.x + 130, cal_card.y + 6, cal_card.x + 130, cal_card.y + 28, 1, UI_BORDER);
    snprintf(cal_desc, sizeof(cal_desc), ptc_ui_text(PTC_UI_T_CALENDAR_EXPLAIN_DESC), date_buf, type_label);
    draw_text(pixels, stride, cal_card.x + 144, cal_card.y + 21, cal_desc, 11, UI_INK);

    /* 7. 底部导航与按钮 (y = 606) */
    draw_text(pixels, stride, 114, 622, ptc_ui_text(PTC_UI_T_DAY_DECISION_SWITCH_HINT), 11, UI_MUTED);

    home_button(pixels, stride, ptc_ui_day_decision_refresh_rect(), ptc_ui_text(PTC_UI_T_Y_REFRESH),
                false, false, model->waiting);
    home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_B_RETURN),
                false, true, false);
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
