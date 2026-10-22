#include "ui_render_internal.h"
#include "../../common/rules/rules.h"

static void draw_shortcut_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char status[192];
    char shortcut_hint[160];
    int index;
    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_PARENT_SHORTCUT),
                             ptc_ui_text(PTC_UI_T_PARENT_AREA_SHORTCUT_KEY_MANAGEMENT));
    for (index = 0; index < PTC_UI_SHORTCUT_PRESET_COUNT; ++index) {
        UiRect option = to_uirect(ptc_ui_shortcut_option_rect(index));
        bool selected = index == model->setup_shortcut_index;
        bool chosen = model->shortcut_draft_enabled && selected;
        fill_round_rect(pixels, stride, option, 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 10, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
        draw_text(pixels, stride, option.x + 16, option.y + 28,
                  ptc_ui_shortcut_common_label(index), 17,
                  selected ? UI_ACCENT : UI_INK);
        if (chosen) {
            const char *save_text = ptc_ui_text(PTC_UI_T_PENDING_SAVE);
            int tw = measure_text(save_text, 14);
            draw_text(pixels, stride, option.x + option.width - tw - 16, option.y + 28,
                      save_text, 14, UI_SUCCESS);
        }
    }
    ptc_ui_format_custom_shortcut_hint(model->shortcut_draft_label, shortcut_hint, sizeof(shortcut_hint));
    snprintf(status, sizeof(status), "%s",
             model->shortcut_draft_enabled ? shortcut_hint : ptc_ui_text(PTC_UI_T_THE_CUSTOM_ENTRANCE_HAS_BEEN_CLOSED_FIXED));
    draw_wrapped_text(pixels, stride, 54, 544, status, 14, 1172, 18, 1,
                      model->shortcut_draft_enabled ? UI_SUCCESS : UI_DANGER);

    home_button(pixels, stride, ptc_ui_shortcut_disable_rect(),
                ptc_ui_text(PTC_UI_T_ZL_CLOSE_CUSTOM_SHORTCUT_KEYS), false, false, false);
    home_button(pixels, stride, ptc_ui_shortcut_hint_rect(),
                model->shortcut_draft_show_hint ? ptc_ui_text(PTC_UI_T_Y_CHILD_AREA_PROMPT_DISPLAY) : ptc_ui_text(PTC_UI_T_Y_CHILD_AREA_PROMPT_HIDE),
                false, false, false);
    home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                ptc_ui_text(PTC_UI_T_CONFIRM_TO_SAVE), true, false, model->waiting);
}

static void draw_software_info_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect details;
    char value[128];
    const char *status = ptc_ui_text(PTC_UI_T_STATUS_UNKNOWN);
    uint32_t status_color = UI_MUTED;
    bool pending = model->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING && !model->parent_support_only;
    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_SOFTWARE_INFO),
                             ptc_ui_text(PTC_UI_T_VERSION_PROJECT_REPOSITORY_AND_PARENT_PAGE));
    details = (UiRect){54, 176, 1172, 468};
    draw_plan_card(pixels, stride, details, false);
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

    int val_offset = 34 + max_lbl_w + 28;
    if (val_offset < 220) val_offset = 220;
    int val_max_w = details.width - val_offset - 34;

    draw_text(pixels, stride, details.x + 34, details.y + 56, lbl_host, 18, UI_MUTED);
    snprintf(value, sizeof(value), "%.24s  (%.88s)", model->software_version,
             model->app_release_id[0] ? model->app_release_id : ptc_ui_text(PTC_UI_T_UNKNOWN_IDENTITY));
    fit_text(value, sizeof(value), value, 18, val_max_w);
    draw_text(pixels, stride, details.x + val_offset, details.y + 56, value, 18, UI_ACCENT);

    draw_text(pixels, stride, details.x + 34, details.y + 118, lbl_bg, 18, UI_MUTED);
    fit_text(value, sizeof(value), model->backend_release_id[0] ? model->backend_release_id : ptc_ui_text(PTC_UI_T_UNABLE_TO_TRUSTWORTHY_CONFIRMATION), 18,
             val_max_w);
    draw_text(pixels, stride, details.x + val_offset, details.y + 118, value, 18, UI_INK);

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
    draw_text(pixels, stride, details.x + 34, details.y + 182, lbl_status, 18, UI_MUTED);
    draw_text(pixels, stride, details.x + val_offset, details.y + 182, status, 20, status_color);
    draw_wrapped_text(pixels, stride, details.x + val_offset, details.y + 212,
        model->hot_reload_detail, 15, val_max_w, 20, 2, UI_MUTED);

    draw_text(pixels, stride, details.x + 34, details.y + 300, lbl_repo, 18, UI_MUTED);
    fit_text(value, sizeof(value), model->repository_url, 17, val_max_w);
    draw_text(pixels, stride, details.x + val_offset, details.y + 300, value, 17, UI_ACCENT);

    draw_text(pixels, stride, details.x + 34, details.y + 362, lbl_parent, 18, UI_MUTED);
    fit_text(value, sizeof(value), model->pwa_url, 17, val_max_w);
    draw_text(pixels, stride, details.x + val_offset, details.y + 362, value, 17, UI_SUCCESS);

    if (pending) {
        home_button(pixels, stride, ptc_ui_settings_page_footer_rect(1), ptc_ui_text(PTC_UI_T_B_NOT_YET),
                    false, false, false);
        home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                    ptc_ui_text(model->hot_reload_capture_restart
                        ? PTC_UI_T_CAPTURE_RESTART_BUTTON : PTC_UI_T_A_LOAD_NEW_VERSION),
                    true, false, false);
    } else {
        home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                    ptc_ui_text(PTC_UI_T_A_CLOSE), true, false, false);
    }
}

static void draw_album_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *state = model->album_restriction_state == 0 ? ptc_ui_text(PTC_UI_T_NOT_ENABLED) :
                        model->album_restriction_state == 1 ? ptc_ui_text(PTC_UI_T_ENABLED) :
                        model->album_restriction_state == 2 ? ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED_2) :
                        model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? ptc_ui_text(PTC_UI_T_EXTERNAL_CONFIGURED) : ptc_ui_text(PTC_UI_T_STATUS_UNKNOWN);
    const char *status_lbl = ptc_ui_text(PTC_UI_T_CURRENT_STATUS);
    int status_lbl_w;
    int state_x;
    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_HOMEBREW_ACCESS),
                             ptc_ui_text(PTC_UI_T_THIS_FUNCTION_ONLY_CHANGES_THE_HBMENU_STARTUP));

    /* 状态条：当前状态 + Y 重新检测 */
    {
        UiRect bar = {54, 176, 1172, 72};
        fill_round_rect(pixels, stride, bar, 12, UI_PAGE);
        draw_rect_outline(pixels, stride, bar, 12, 1, UI_BORDER);
        status_lbl_w = measure_text(status_lbl, 17);
        state_x = bar.x + 22 + status_lbl_w + 14;
        draw_text(pixels, stride, bar.x + 22, bar.y + 44, status_lbl, 17, UI_MUTED);
        draw_text(pixels, stride, state_x, bar.y + 44, state, 19,
                  model->album_restriction_state == 1 ? UI_SUCCESS :
                  model->album_restriction_state == 2 ? UI_WARNING :
                  model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? UI_ACCENT : UI_MUTED);
        home_button(pixels, stride, ptc_ui_album_refresh_rect(), ptc_ui_text(PTC_UI_T_Y_RETEST),
                    false, false, false);
    }

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
        draw_text(pixels, stride, card.x + 24, card.y + 44, card_title, title_size, enabled ? UI_INK : UI_DISABLED);
        const char *card_desc = index == 0
            ? (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL
                ? ptc_ui_text(PTC_UI_T_THE_CURRENT_DISK_CONFIGURATION_ALREADY_PROVIDES_THE)
                : ptc_ui_text(PTC_UI_T_FIRST_MAKE_A_COMPLETE_BACKUP_OF_RELEVANT))
            : ptc_ui_text(PTC_UI_T_RESTORE_THE_ORIGINAL_CONFIGURATION_ACCORDING_TO_THE);
        draw_wrapped_text(pixels, stride, card.x + 24, card.y + 82, card_desc,
                          15, card.width - 48, 22, 4, enabled ? UI_MUTED : UI_DISABLED);

        /* 底部行动胶囊：与护眼页卡片内按钮同一语义 */
        {
            UiRect pill = {card.x + 24, card.y + card.height - 62, card.width - 48, 40};
            const char *pill_label = enabled ? ptc_ui_text(PTC_UI_T_A_CONTINUE)
                                             : ptc_ui_text(PTC_UI_T_CURRENT_STATUS_IS_NOT_AVAILABLE);
            fill_round_rect(pixels, stride, pill, 20, enabled ? (selected ? UI_ACCENT : UI_ACCENT_SOFT) : UI_RAISED);
            draw_rect_outline(pixels, stride, pill, 20, 1, enabled ? UI_ACCENT : UI_BORDER);
            draw_text_center(pixels, stride, pill, pill_label, 16,
                             enabled ? (selected ? UI_ON_ACCENT : UI_ACCENT) : UI_DISABLED);
        }
    }

    if (model->album_restriction_detail[0]) {
        draw_wrapped_text(pixels, stride, 54, 600, model->album_restriction_detail, 14, 1172, 20, 2, UI_MUTED);
    }
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
    int index;
    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_APPEARANCE_THEME),
                             !g_theme.system_theme_available
                                 ? ptc_ui_text(PTC_UI_T_THE_SYSTEM_THEME_IS_TEMPORARILY_UNAVAILABLE_AND)
                                 : ptc_ui_text(PTC_UI_T_D_PAD_SELECT_A_APPLY_SAVE_B));

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
        draw_wrapped_text(pixels, stride, option.x + 20, option.y + 62, DETAILS[index],
                          15, option.width - 40, 22, 2, UI_MUTED);

        // Realistic UI Miniature Preview Screen inside the card
        UiRect preview = {option.x + 18, option.y + 90, option.width - 36, 204};
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
        UiRect select_btn = {option.x + 24, option.y + option.height - 74, option.width - 48, 38};
        fill_round_rect(pixels, stride, select_btn, 19, selected ? UI_ACCENT : UI_SURFACE);
        draw_rect_outline(pixels, stride, select_btn, 19, 1, selected ? UI_FOCUS : UI_CONTROL);
        draw_text_center(pixels, stride, (UiRect){select_btn.x, select_btn.y + 6, select_btn.width, select_btn.height},
                         selected ? ptc_ui_text(PTC_UI_T_A_CONFIRM_SAVE) : (is_current ? ptc_ui_text(PTC_UI_T_CURRENT) : ptc_ui_text(PTC_UI_T_A_CONFIRM)),
                         16, selected ? UI_ON_ACCENT : UI_INK);
    }
    home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                ptc_ui_text(PTC_UI_T_A_CONFIRM_SAVE), true, false, model->waiting);
}

static void draw_language_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *LABELS[] = {
        ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM),
        ptc_ui_text(PTC_UI_T_SIMPLIFIED_CHINESE),
        ptc_ui_text(PTC_UI_T_TRADITIONAL_CHINESE),
        "English"
    };
    const char *DETAILS[] = {
        ptc_ui_text(PTC_UI_T_USE_SWITCH_SYSTEM_LANGUAGE),
        ptc_ui_text(PTC_UI_T_ALWAYS_SHOW_SIMPLIFIED_CHINESE),
        ptc_ui_text(PTC_UI_T_ALWAYS_SHOW_TRADITIONAL_CHINESE),
        "Always use English"
    };
    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_UI_LANGUAGE),
                             ptc_ui_text(PTC_UI_T_D_PAD_SELECT_A_APPLY_SAVE_B));

    for (int index = 0; index < 4; ++index) {
        UiRect option = to_uirect(ptc_ui_language_option_rect(index));
        bool selected = (index == model->overlay_selection);
        bool is_current = (index == (int)model->language_preference);

        fill_round_rect(pixels, stride, option, 14, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 14, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);

        /* Realistic UI miniature preview screen on the left of each option card */
        UiRect preview = {option.x + 16, option.y + 16, 178, 178};
        fill_round_rect(pixels, stride, preview, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, preview, 10, 1, selected ? UI_ACCENT : UI_BORDER);

        if (index == 0) {
            /* Follow system: Switch OS bar + dual language badges */
            fill_round_rect(pixels, stride, (UiRect){preview.x + 8, preview.y + 10, preview.width - 16, 26}, 6, UI_SURFACE);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 16, 6, 14}, 2, UI_RGB(0x2E72D2));
            fill_round_rect(pixels, stride, (UiRect){preview.x + preview.width - 20, preview.y + 16, 6, 14}, 2, UI_RGB(0xE64A38));
            draw_text_center(pixels, stride, (UiRect){preview.x + 8, preview.y + 14, preview.width - 16, 18}, "Switch OS", 12, UI_INK);

            fill_round_rect(pixels, stride, (UiRect){preview.x + 16, preview.y + 46, preview.width - 32, 66}, 8, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, (UiRect){preview.x + 16, preview.y + 46, preview.width - 32, 66}, 8, 1, UI_ACCENT);
            draw_text_center(pixels, stride, (UiRect){preview.x + 16, preview.y + 54, preview.width - 32, 28}, "AUTO", 20, UI_ACCENT);
            draw_text_center(pixels, stride, (UiRect){preview.x + 16, preview.y + 84, preview.width - 32, 18}, "Auto Sync", 12, UI_MUTED);

            fill_round_rect(pixels, stride, (UiRect){preview.x + 16, preview.y + 122, preview.width - 32, 38}, 6, UI_SURFACE);
            draw_text_center(pixels, stride, (UiRect){preview.x + 16, preview.y + 132, preview.width - 32, 18}, ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM), 13, UI_INK);
        } else if (index == 1) {
            /* Simplified Chinese miniature UI */
            fill_round_rect(pixels, stride, (UiRect){preview.x + 8, preview.y + 10, preview.width - 16, 26}, 6, UI_SURFACE);
            draw_text_center(pixels, stride, (UiRect){preview.x + 8, preview.y + 14, preview.width - 16, 18}, "PlayWise", 12, UI_ACCENT);

            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 44, preview.width - 28, 76}, 8, UI_SURFACE);
            draw_rect_outline(pixels, stride, (UiRect){preview.x + 14, preview.y + 44, preview.width - 28, 76}, 8, 1, UI_BORDER);
            draw_text_center(pixels, stride, (UiRect){preview.x + 18, preview.y + 50, 42, 42}, "CN", 22, UI_ACCENT);
            draw_text(pixels, stride, preview.x + 64, preview.y + 70,
                      ptc_ui_text_in_language(PTC_UI_T_CHILD_ZONE, PTC_UI_LANGUAGE_SIMPLIFIED), 12, UI_INK);
            draw_text(pixels, stride, preview.x + 64, preview.y + 96, "120 min", 14, UI_SUCCESS);

            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 130, preview.width - 28, 32}, 6, UI_ACCENT);
            draw_text_center(pixels, stride, (UiRect){preview.x + 14, preview.y + 136, preview.width - 28, 20},
                             ptc_ui_text_in_language(PTC_UI_T_SIMPLIFIED_CHINESE, PTC_UI_LANGUAGE_SIMPLIFIED), 13, UI_ON_ACCENT);
        } else if (index == 2) {
            /* Traditional Chinese miniature UI */
            fill_round_rect(pixels, stride, (UiRect){preview.x + 8, preview.y + 10, preview.width - 16, 26}, 6, UI_SURFACE);
            draw_text_center(pixels, stride, (UiRect){preview.x + 8, preview.y + 14, preview.width - 16, 18}, "PlayWise", 12, UI_ACCENT);

            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 44, preview.width - 28, 76}, 8, UI_SURFACE);
            draw_rect_outline(pixels, stride, (UiRect){preview.x + 14, preview.y + 44, preview.width - 28, 76}, 8, 1, UI_BORDER);
            draw_text_center(pixels, stride, (UiRect){preview.x + 18, preview.y + 50, 42, 42}, "HK", 22, UI_ACCENT);
            draw_text(pixels, stride, preview.x + 64, preview.y + 70,
                      ptc_ui_text_in_language(PTC_UI_T_CHILD_ZONE, PTC_UI_LANGUAGE_TRADITIONAL), 12, UI_INK);
            draw_text(pixels, stride, preview.x + 64, preview.y + 96, "120 min", 14, UI_SUCCESS);

            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 130, preview.width - 28, 32}, 6, UI_ACCENT);
            draw_text_center(pixels, stride, (UiRect){preview.x + 14, preview.y + 136, preview.width - 28, 20},
                             ptc_ui_text_in_language(PTC_UI_T_TRADITIONAL_CHINESE, PTC_UI_LANGUAGE_TRADITIONAL), 13, UI_ON_ACCENT);
        } else {
            /* English miniature UI */
            fill_round_rect(pixels, stride, (UiRect){preview.x + 8, preview.y + 10, preview.width - 16, 26}, 6, UI_SURFACE);
            draw_text_center(pixels, stride, (UiRect){preview.x + 8, preview.y + 14, preview.width - 16, 18}, "PlayWise", 12, UI_ACCENT);

            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 44, preview.width - 28, 76}, 8, UI_SURFACE);
            draw_rect_outline(pixels, stride, (UiRect){preview.x + 14, preview.y + 44, preview.width - 28, 76}, 8, 1, UI_BORDER);
            draw_text_center(pixels, stride, (UiRect){preview.x + 18, preview.y + 50, 42, 42}, "EN", 22, UI_ACCENT);
            draw_text(pixels, stride, preview.x + 64, preview.y + 70, "Child Zone", 12, UI_INK);
            draw_text(pixels, stride, preview.x + 64, preview.y + 96, "120 min", 14, UI_SUCCESS);

            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 130, preview.width - 28, 32}, 6, UI_ACCENT);
            draw_text_center(pixels, stride, (UiRect){preview.x + 14, preview.y + 136, preview.width - 28, 20}, "English", 13, UI_ON_ACCENT);
        }

        /* Information & action area on the right side of the card */
        draw_text(pixels, stride, option.x + 214, option.y + 44, LABELS[index], 22,
                  selected ? UI_ACCENT : UI_INK);
        draw_wrapped_text(pixels, stride, option.x + 214, option.y + 80, DETAILS[index], 14,
                          option.width - 232, 20, 2, UI_MUTED);

        /* Selection pill button */
        UiRect select_btn = {option.x + 214, option.y + option.height - 54, 180, 36};
        fill_round_rect(pixels, stride, select_btn, 18, selected ? UI_ACCENT : UI_SURFACE);
        draw_rect_outline(pixels, stride, select_btn, 18, 1, selected ? UI_FOCUS : UI_CONTROL);
        draw_text_center(pixels, stride, (UiRect){select_btn.x, select_btn.y + 6, select_btn.width, select_btn.height},
                         selected ? ptc_ui_text(PTC_UI_T_A_CONFIRM_SAVE) : (is_current ? ptc_ui_text(PTC_UI_T_CURRENT) : ptc_ui_text(PTC_UI_T_A_CONFIRM)),
                         15, selected ? UI_ON_ACCENT : UI_INK);
    }

    home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                ptc_ui_text(PTC_UI_T_A_CONFIRM_SAVE), true, false, model->waiting);
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
    PTC_UI_T_DETAIL_REST, PTC_UI_T_DETAIL_RULES_GATE_DOCK_TITLE
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

static bool detail_runtime_available(const PtcUiModel *model)
{
    return !model->disable_flag_present && !model->recovery_active &&
        !model->apply_pending_confirmation && !model->temporary_unlocked &&
        strcmp(model->setup_phase, "active") == 0 && model->restricted_now != 1 && model->blocked_today != 1;
}

static bool detail_available(const PtcUiModel *model, const PtcUiTodayProjection *p)
{
    if (!p->fresh || p->active_reasons || !detail_runtime_available(model)) return false;
    for (int i = 0; i < 4; ++i) if (p->conditions[i] == PTC_UI_CONDITION_UNKNOWN) return false;
    return true;
}

/* Keep labels in two measured lanes; a dense day may omit earlier labels,
 * while the rail still preserves every segment and the final stop. */
static void detail_timeline_label(uint32_t *pixels, uint32_t stride, UiRect rail,
                                  int anchor, int second, const char *label,
                                  uint32_t bg, uint32_t border, uint32_t fg,
                                  int lane, int *right, bool *omitted, bool leaders)
{
    char time_text[16], fitted[128];
    detail_time(second, time_text, sizeof(time_text));
    int width = measure_text(time_text, 10);
    int label_width = measure_text(label, 10);
    if (label_width > width) width = label_width;
    width += 16;
    if (width > rail.width / 3) width = rail.width / 3;
    int x = anchor - width / 2;
    if (x + width > *right) x = *right - width;
    if (x < rail.x + 18) x = rail.x + 18;
    if (x + width > *right) {
        *omitted = true;
        return;
    }
    UiRect bubble = {x, rail.y - 68 + lane * 34, width, 30};
    if (leaders) {
        draw_line(pixels, stride, x + width / 2, bubble.y + bubble.height,
                  anchor, rail.y + rail.height / 2, 1, border);
        *right = x - 6;
        return;
    }
    fill_round_rect(pixels, stride, bubble, 5, bg);
    draw_rect_outline(pixels, stride, bubble, 5, 1, border);
    draw_text_center(pixels, stride, (UiRect){x, bubble.y + 2, width, 14}, time_text, 10, fg);
    fit_text(fitted, sizeof(fitted), label, 10, width - 12);
    draw_text_center(pixels, stride, (UiRect){x, bubble.y + 16, width, 14}, fitted, 10, fg);
    *right = x - 6;
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

static void support_draw_info(uint32_t *pixels, uint32_t stride, int cx, int cy, int radius, uint32_t color)
{
    draw_circle_outline(pixels, stride, cx, cy, radius, 2, color);
    support_fill_circle_mini(pixels, stride, (float)cx, (float)(cy - radius * 4 / 10), 1.3f, color);
    draw_line(pixels, stride, cx, cy - radius / 10, cx, cy + radius * 5 / 10, 2, color);
}


static void support_draw_arrow_down(uint32_t *pixels, uint32_t stride, int tip_x, int tip_y, int width, int height, uint32_t color)
{
    uint32_t resolved = resolve_color(color);
    for (int dy = 0; dy <= height; ++dy) {
        int y = tip_y - dy;
        if (y < 0 || y >= SCREEN_HEIGHT) continue;
        int half_w = (width * dy + height / 2) / (height * 2);
        int x1 = tip_x - half_w;
        int x2 = tip_x + half_w;
        if (x1 < 0) x1 = 0;
        if (x2 >= SCREEN_WIDTH) x2 = SCREEN_WIDTH - 1;
        for (int x = x1; x <= x2; ++x) {
            pixels[y * stride + x] = resolved;
        }
    }
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
                            const PtcUiTodayProjection *p, const PtcUiTodayDecision *decision,
                            const PtcUiHomeDetailsStatus *status)
{
    char text[384], value[128], fitted[128];
    bool is_avail = detail_available(model, p);
    bool is_dark = (g_theme.resolved == PTC_UI_RESOLVED_DARK);
    bool is_docked = (strcmp(model->operation_mode, "docked") == 0);

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
        fit_text(value, sizeof(value), value, 20, c0.width - 84);
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
        char plan_tag[192];
        if (status->fresh) snprintf(plan_tag, sizeof(plan_tag), "%s / %s",
            ptc_ui_effective_rule_label(decision->effective.source), status->quota);
        else snprintf(plan_tag, sizeof(plan_tag), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
        const int tag_left = c1.x + 44 + measure_text(ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE), 12) + 8;
        fit_text(plan_tag, sizeof(plan_tag), plan_tag, 11, c1.x + c1.width - 24 - tag_left);
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
        int fill_w = status->usage_progress_percent * pbar.width / 100;
        if (fill_w > 0) fill_round_rect(pixels, stride, (UiRect){pbar.x, pbar.y, fill_w, pbar.height}, 4, UI_ACCENT);

        char used_str[128], buf_str[160];
        fit_text(buf_str, sizeof(buf_str), status->buffer, 11, pbar.width / 2);
        int buf_w = measure_text(buf_str, 11);
        fit_text(used_str, sizeof(used_str), status->usage, 11, pbar.width - buf_w - 12);
        draw_text(pixels, stride, c1.x + 44, c1.y + 75, used_str, 11, UI_MUTED);
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

        const char *mode_badge = !status->fresh ? ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM) :
            ptc_ui_text(is_docked ? PTC_UI_T_DOCK_TV_BADGE : PTC_UI_T_DOCK_HANDHELD_BADGE);
        uint32_t badge_col = !status->fresh ? UI_WARNING : is_docked ? UI_SUCCESS : UI_ACCENT;
        uint32_t badge_bg = !status->fresh ? UI_WARNING_SOFT : is_docked ? UI_SUCCESS_SOFT : UI_ACCENT_SOFT;
        int mb_w = measure_text(mode_badge, 10) + 14;
        UiRect mb_rect = {c2.x + c2.width - mb_w - 16, c2.y + 14, mb_w, 20};
        fill_round_rect(pixels, stride, mb_rect, 4, badge_bg);
        draw_rect_outline(pixels, stride, mb_rect, 4, 1, badge_col);
        draw_text_center(pixels, stride, mb_rect, mode_badge, 10, badge_col);

        /* 核心态势标语 */
        const char *headline = status->policies.dock.compact;
        fit_text(fitted, sizeof(fitted), headline, 13, c2.width - 40);
        draw_text_bold(pixels, stride, c2.x + 20, c2.y + 48, fitted, 13, UI_INK);

        /* 引导与健康提示行 */
        const char *guide = status->policies.dock.detail;
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
        fit_text(text, sizeof(text), status->policies.bedtime.detail, 12, tip_rect.width - 40);
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
            UiRect rail = {prediction.x + 24, prediction.y + 122, prediction.width - 48, 18};
            fill_round_rect(pixels, stride, rail, 4, UI_RAISED);
            int range = p->stop_second - p->now_second;
            if (range <= 0) range = 1;
            for (int i = 0; i < p->segment_count; ++i) {
                const PtcUiUsageSegment *seg = &p->segments[i];
                int x = rail.x + (seg->start_second - p->now_second) * rail.width / range;
                int end = rail.x + (seg->end_second - p->now_second) * rail.width / range;
                fill_rect(pixels, stride, (UiRect){x, rail.y, end - x, rail.height},
                          seg->resting ? UI_WARNING : UI_ACCENT);
            }
            char t_stop[16];
            detail_time(p->stop_second, t_stop, sizeof(t_stop));
            /* Paint all leaders first so none can cut through another label. */
            for (int pass = 0; pass < 2; ++pass) {
                int right[2] = {rail.x + rail.width, rail.x + rail.width};
                bool omitted = false;
                int lane = 0;
                char stop_reason[128];
                detail_reasons(p->stop_reasons, stop_reason, sizeof(stop_reason));
                detail_timeline_label(pixels, stride, rail, rail.x + rail.width, p->stop_second,
                                      stop_reason, UI_RAISED, UI_MUTED, UI_INK,
                                      lane, &right[lane], &omitted, pass == 0);
                for (int i = p->segment_count - 1; i >= 0; --i) {
                    const PtcUiUsageSegment *seg = &p->segments[i];
                    if (!seg->resting) continue;
                    int x = rail.x + (seg->start_second - p->now_second) * rail.width / range;
                    int end = rail.x + (seg->end_second - p->now_second) * rail.width / range;
                    if (seg->end_second < p->stop_second) {
                        lane = 1 - lane;
                        detail_timeline_label(pixels, stride, rail, end, seg->end_second,
                                              ptc_ui_text(PTC_UI_T_DETAIL_SIM_RESUME_LABEL),
                                              UI_ACCENT_SOFT, UI_ACCENT, UI_ACCENT,
                                              lane, &right[lane], &omitted, pass == 0);
                    }
                    lane = 1 - lane;
                    char rest[64];
                    snprintf(rest, sizeof(rest), ptc_ui_text(PTC_UI_T_DETAIL_SIM_REST_FMT),
                             (unsigned)((seg->end_second - seg->start_second) / 60));
                    detail_timeline_label(pixels, stride, rail, x, seg->start_second,
                                          rest, UI_WARNING_SOFT, UI_WARNING_BORDER, UI_WARNING_TEXT,
                                          lane, &right[lane], &omitted, pass == 0);
                }
                if (pass == 1 && omitted) draw_text(pixels, stride, rail.x, rail.y - 40, "...", 12, UI_MUTED);
            }

            /* 刻度文字 */
            detail_time(p->now_second, text, sizeof(text));
            draw_text(pixels, stride, rail.x, rail.y + rail.height + 15, text, 10, UI_MUTED);
            draw_text(pixels, stride, rail.x + rail.width - measure_text(t_stop, 10), rail.y + rail.height + 15, t_stop, 10, UI_MUTED);
        }

        /* 底部说明栏 */
        UiRect tip2 = {prediction.x + 24, prediction.y + 156, prediction.width - 48, 42};
        fill_round_rect(pixels, stride, tip2, 6, UI_ACCENT_SOFT);
        support_draw_info(pixels, stride, tip2.x + 14, tip2.y + 16, 7, UI_ACCENT);
        if (!p->simulation_available) snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
        else if (p->total_unlimited) snprintf(text, sizeof(text), "%s: %s",
            ptc_ui_text(PTC_UI_T_TOTAL_DAILY_ALLOWANCE), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else if (p->total_at_stop >= 0) snprintf(text, sizeof(text),
            ptc_ui_text(PTC_UI_T_DETAIL_SIM_FOOTER_FMT), (unsigned)p->total_at_stop);
        else snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
        fit_text(text, sizeof(text), text, 12, tip2.width - 40);
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

        /* Reuse the current status projection: stale/off values must not
           become live counters merely because the cards have a new layout. */
        const PtcUiTextId titles[] = {PTC_UI_T_DETAIL_HEALTH_EYE,
            PTC_UI_T_DETAIL_HEALTH_BEDTIME, PTC_UI_T_DETAIL_HEALTH_DOCK};
        const PtcUiTextId badges[] = {status->eye_badge,
            status->policies.bedtime.badge, status->policies.dock.badge};
        const PtcPolicyDisplayState states[] = {status->eye_state,
            status->policies.bedtime.state, status->policies.dock.state};
        const char *details[] = {status->eye, status->policies.bedtime.detail,
            status->policies.dock.compact};
        for (int i = 0; i < 3; ++i) {
            UiRect card = {c_health.x + 14, c_health.y + 36 + i * 74, c_health.width - 28, 66};
            uint32_t fg = states[i] == PTC_POLICY_UNKNOWN ? UI_WARNING : policy_display_color(states[i]);
            uint32_t bg = fg == UI_WARNING ? UI_WARNING_SOFT :
                fg == UI_DANGER ? UI_DANGER_SOFT : fg == UI_SUCCESS ? UI_SUCCESS_SOFT : UI_RAISED;
            fill_round_rect(pixels, stride, card, 6, UI_PAGE);
            draw_rect_outline(pixels, stride, card, 6, 1, UI_BORDER);
            if (i == 0) support_draw_eye(pixels, stride, card.x + 18, card.y + 18, 7, fg);
            else if (i == 1) support_draw_crescent_moon(pixels, stride, card.x + 18, card.y + 18, 6.0f, 5.0f, 2.0f, -1.0f, fg);
            else support_draw_controller(pixels, stride, card.x + 18, card.y + 18, 7, fg);
            const char *badge = ptc_ui_text(badges[i]);
            int badge_w = measure_text(badge, 10) + 12;
            UiRect pill = {card.x + card.width - badge_w - 12, card.y + 8, badge_w, 18};
            fit_text(fitted, sizeof(fitted), ptc_ui_text(titles[i]), 13, pill.x - card.x - 42);
            draw_text_bold(pixels, stride, card.x + 34, card.y + 20, fitted, 13, UI_INK);
            fill_round_rect(pixels, stride, pill, 4, bg);
            draw_text_center(pixels, stride, pill, badge, 10, fg);
            detail_text(pixels, stride, (UiRect){card.x + 34, card.y + 39, card.width - 48, 26},
                details[i], 11, fg, 2);
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
        if (status->fresh) snprintf(hit_buf, sizeof(hit_buf), "%s / %s",
            ptc_ui_effective_rule_label(decision->effective.source), status->quota);
        else snprintf(hit_buf, sizeof(hit_buf), "%s", ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM));
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
    char text[512], value[64], fitted_sub[256];
    UiRect sec1 = {x, y, full_w, full_h};
    detail_panel(pixels, stride, sec1);

    /* 标题行: 第二步：实时准入与四大健康关卡 */
    bool any_block = (conditions[0] == PTC_UI_CONDITION_BLOCK || conditions[1] == PTC_UI_CONDITION_BLOCK ||
                      conditions[2] == PTC_UI_CONDITION_BLOCK || conditions[3] == PTC_UI_CONDITION_BLOCK);
    bool any_unknown = is_today && !detail_runtime_available(model);
    for (int i = 0; i < 4; ++i)
        if (conditions[i] == PTC_UI_CONDITION_UNKNOWN) any_unknown = true;
    uint32_t badge_color = any_block ? UI_DANGER : any_unknown ? UI_WARNING : UI_SUCCESS;
    support_fill_circle_mini(pixels, stride, (float)(sec1.x + 24), (float)(sec1.y + 18), 9.0f, badge_color);
    draw_text_center(pixels, stride, (UiRect){sec1.x + 15, sec1.y + 9, 18, 18}, "2", 12, UI_ON_ACCENT);
    const char *sec1_title = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC1_TITLE);
    int t1_w = measure_text(sec1_title, 14);
    draw_text_bold(pixels, stride, sec1.x + 40, sec1.y + 22, sec1_title, 14, UI_INK);

    const char *sec1_sub = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC1_SUB);
    int sub1_x = sec1.x + 40 + t1_w + 14;
    int max_sub1_w = (sec1.x + sec1.width - 20) - sub1_x;
    if (max_sub1_w > 60) {
        fit_text(fitted_sub, sizeof(fitted_sub), sec1_sub, 12, max_sub1_w);
        draw_text(pixels, stride, sub1_x, sec1.y + 22, fitted_sub, 12, UI_MUTED);
    }

    /* 独立守护互不干扰交互行 (可点击弹出规则说明) */
    UiRect tip_rect = to_uirect(ptc_ui_rules_independent_tip_rect(model));
    fill_round_rect(pixels, stride, tip_rect, 6, UI_PAGE);
    draw_rect_outline(pixels, stride, tip_rect, 6, 1, UI_BORDER);
    draw_text(pixels, stride, tip_rect.x + 10, tip_rect.y + 18, "🛡️", 14, UI_INK);

    /* 右侧查看说明药丸按钮 */
    const char *btn_txt = ptc_ui_text(PTC_UI_T_RULES_EXPLAIN_BTN);
    int btn_w = measure_text(btn_txt, 11) + 20;
    UiRect exp_btn = {tip_rect.x + tip_rect.width - btn_w - 6, tip_rect.y + 3, btn_w, 20};
    fill_round_rect(pixels, stride, exp_btn, 4, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, exp_btn, 4, 1, UI_ACCENT);
    draw_text_center(pixels, stride, exp_btn, btn_txt, 11, UI_ACCENT);

    /* 左侧提示文案 */
    int tip_text_x = tip_rect.x + 30;
    int max_tip_w = exp_btn.x - tip_text_x - 10;
    char fitted_tip[256];
    fit_text(fitted_tip, sizeof(fitted_tip), ptc_ui_text(PTC_UI_T_DETAIL_RULES_TIP_INDEPENDENT), 11, max_tip_w);
    draw_text(pixels, stride, tip_text_x, tip_rect.y + 18, fitted_tip, 11, UI_MUTED);

    /* 4 张健康关卡卡片 (高度压缩至 76px) */
    int card_y1 = tip_rect.y + tip_rect.height + 6;
    int gap1 = (full_w > 1100) ? 20 : 12;
    int card_w1 = (full_w - 32 - 3 * gap1) / 4;
    int card_h1 = 76;

    for (int i = 0; i < 4; ++i) {
        int cx = sec1.x + 16 + i * (card_w1 + gap1);
        UiRect c = {cx, card_y1, card_w1, card_h1};
        PtcUiCondition cond = conditions[i];
        bool is_pass = (cond == PTC_UI_CONDITION_PASS || cond == PTC_UI_CONDITION_WAIVED);
        bool is_block = (cond == PTC_UI_CONDITION_BLOCK);

        if (is_block) {
            fill_round_rect(pixels, stride, c, 8, UI_DANGER_SOFT);
            draw_rect_outline(pixels, stride, c, 8, 2, UI_DANGER);
        } else {
            fill_round_rect(pixels, stride, c, 8, UI_PAGE);
            draw_rect_outline(pixels, stride, c, 8, 1, UI_BORDER);
        }

        /* 4 个关卡的专属领域色 */
        uint32_t domain_color = (i == 0) ? UI_ACCENT : ((i == 1) ? UI_HERO : ((i == 2) ? UI_SUCCESS : UI_WARNING));

        /* 左侧领域竖色条 */
        UiRect accent_bar = {c.x + 4, c.y + 8, 4, c.height - 16};
        fill_round_rect(pixels, stride, accent_bar, 2, is_block ? UI_DANGER : domain_color);

        /* 顶部行：Emoji + 关卡名称 + 右上角紧凑状态胶囊 */
        const char *gate_emojis[] = {"⏰", "🌙", "🌿", "📺"};
        draw_text(pixels, stride, c.x + 12, c.y + 20, gate_emojis[i], 14, UI_INK);
        draw_text_bold(pixels, stride, c.x + 32, c.y + 19, ptc_ui_text(DETAIL_CONDITION_TITLES[i]), 13, UI_INK);

        /* 右上角紧凑状态胶囊 */
        const char *cond_label = detail_condition_label(cond);
        int pill_w = measure_text(cond_label, 11) + (is_pass ? 26 : 14);
        int pill_h = 18;
        UiRect pass_pill = {c.x + c.width - pill_w - 8, c.y + 6, pill_w, pill_h};
        uint32_t p_bg = is_block ? UI_DANGER : (is_pass ? UI_SUCCESS_SOFT : UI_RAISED);
        uint32_t p_bd = is_block ? UI_DANGER : (is_pass ? UI_SUCCESS : UI_BORDER);
        uint32_t p_fg = is_block ? UI_ON_ACCENT : (is_pass ? UI_SUCCESS : UI_MUTED);

        fill_round_rect(pixels, stride, pass_pill, 4, p_bg);
        if (!is_block) draw_rect_outline(pixels, stride, pass_pill, 4, 1, p_bd);
        if (is_pass) {
            support_draw_checkmark(pixels, stride, pass_pill.x + 9, pass_pill.y + pill_h / 2, 4, UI_SUCCESS, UI_ON_ACCENT);
        }
        draw_text_center(pixels, stride, pass_pill, cond_label, 11, p_fg);

        /* 中部核心指标 */
        int metric_y = c.y + 42;
        /* 底部副文 */
        int sub_y = c.y + 62;

        if (cond == PTC_UI_CONDITION_UNKNOWN || (i > 0 &&
            (cond == PTC_UI_CONDITION_OFF || cond == PTC_UI_CONDITION_WAIVED))) {
            fit_text(text, sizeof(text), detail_condition_label(cond), 13, c.width - 28);
            draw_text_bold(pixels, stride, c.x + 14, metric_y, text, 13,
                cond == PTC_UI_CONDITION_UNKNOWN ? UI_WARNING : UI_MUTED);
        } else if (i == 0) {
            if (is_today) {
                detail_balance(total_known, total_unlimited, remaining_minutes, value, sizeof(value));
                char left_msg[64];
                snprintf(left_msg, sizeof(left_msg), ptc_ui_text(PTC_UI_T_DETAIL_LEFT_LABEL), value);
                draw_text_bold(pixels, stride, c.x + 14, metric_y, left_msg, 14, is_block ? UI_DANGER : domain_color);
                if (total_known && !total_unlimited && model->played_minutes_available && played_minutes >= 0)
                    snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_USED_TOTAL_SUB),
                        (unsigned)played_minutes, (unsigned)target_minutes);
                else snprintf(text, sizeof(text), "%s", ptc_ui_effective_rule_label(decision->effective.source));
                draw_text(pixels, stride, c.x + 14, sub_y, text, 10, UI_MUTED);
            } else {
                detail_balance(total_known, total_unlimited, target_minutes, value, sizeof(value));
                draw_text_bold(pixels, stride, c.x + 14, metric_y, value, 14, is_block ? UI_DANGER : domain_color);
                draw_text(pixels, stride, c.x + 14, sub_y, ptc_ui_effective_rule_label(decision->effective.source), 10, UI_MUTED);
            }
        } else if (i == 1) {
            unsigned b_st = model->bedtime_policy.week[target_weekday].start_minute;
            unsigned b_en = model->bedtime_policy.week[target_weekday].end_minute;
            char bt_msg[64];
            snprintf(bt_msg, sizeof(bt_msg), ptc_ui_text(PTC_UI_T_DETAIL_RULE_BEDTIME_START), b_st / 60, b_st % 60);
            draw_text_bold(pixels, stride, c.x + 14, metric_y, bt_msg, 14, is_block ? UI_DANGER : domain_color);
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_BEDTIME_NEXT), b_en / 60, b_en % 60);
            draw_text(pixels, stride, c.x + 14, sub_y, text, 10, UI_MUTED);
        } else if (i == 2) {
            char eye_msg[64];
            snprintf(eye_msg, sizeof(eye_msg), ptc_ui_text(PTC_UI_T_DETAIL_RULE_EYE_PLAYED),
                     (unsigned)(is_today ? model->eye_care_used_minutes : 0), (unsigned)model->eye_care_policy.play_minutes);
            draw_text_bold(pixels, stride, c.x + 14, metric_y, eye_msg, 14, is_block ? UI_DANGER : domain_color);
            unsigned left_p = is_today && model->eye_care_policy.play_minutes > model->eye_care_used_minutes ?
                (unsigned)(model->eye_care_policy.play_minutes - model->eye_care_used_minutes) : (unsigned)model->eye_care_policy.play_minutes;
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_EYE_LEFT), left_p, (unsigned)model->eye_care_policy.rest_minutes);
            draw_text(pixels, stride, c.x + 14, sub_y, text, 10, UI_MUTED);
        } else {
            const char *dmode = is_today ?
                ptc_ui_text(strcmp(model->operation_mode, "docked") == 0 ? PTC_UI_T_DETAIL_TAG_DOCKED : PTC_UI_T_DETAIL_TAG_HANDHELD) :
                ptc_ui_text(model->dock_policy.force_docked ? PTC_UI_T_DETAIL_TAG_DOCKED : PTC_UI_T_DETAIL_TAG_HANDHELD);
            draw_text_bold(pixels, stride, c.x + 14, metric_y, dmode, 14, is_block ? UI_DANGER : domain_color);
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_DETAIL_RULE_DOCK_REMAIN),
                     (unsigned)(is_today ? model->undocked_remaining_minutes : model->dock_policy.undocked_daily_minutes));
            draw_text(pixels, stride, c.x + 14, sub_y, text, 10, UI_MUTED);
        }
    }

    int mid_x = sec1.x + sec1.width / 2;
    int gate_w = (full_w > 1000) ? 960 : (full_w - 40);
    int gate_h = (full_h >= 240) ? 36 : 30;
    int gate_y = sec1.y + full_h - gate_h - 10;
    UiRect gate = {mid_x - gate_w / 2, gate_y, gate_w, gate_h};

    int by1 = card_y1 + card_h1;
    int bus_y = by1 + (gate_y - by1) * 38 / 100;
    int lx1 = sec1.x + 16 + card_w1 / 2;
    int lx2 = sec1.x + 16 + 3 * (card_w1 + gap1) + card_w1 / 2;

    /* 绘制各卡片向下的动态引线与道闸 (垂直长度达 36px 以上) */
    for (int i = 0; i < 4; ++i) {
        int cx = sec1.x + 16 + i * (card_w1 + gap1);
        int bx = cx + card_w1 / 2;
        PtcUiCondition cond = conditions[i];
        bool is_pass = (cond == PTC_UI_CONDITION_PASS || cond == PTC_UI_CONDITION_WAIVED);
        bool is_block = (cond == PTC_UI_CONDITION_BLOCK);

        if (is_block) {
            /* 红色阻断引线 */
            draw_line(pixels, stride, bx, by1, bx, bus_y, 2, UI_DANGER);
            int sym_y = (by1 + bus_y) / 2;
            support_fill_circle_mini(pixels, stride, (float)bx, (float)sym_y, 5.0f, UI_DANGER);
            draw_line(pixels, stride, bx - 2, sym_y - 2, bx + 2, sym_y + 2, 2, UI_ON_ACCENT);
            draw_line(pixels, stride, bx - 2, sym_y + 2, bx + 2, sym_y - 2, 2, UI_ON_ACCENT);
        } else if (is_pass) {
            /* 绿色放行引线 */
            draw_line(pixels, stride, bx, by1, bx, bus_y, 2, UI_SUCCESS);
            int sym_y = (by1 + bus_y) / 2;
            support_fill_circle_mini(pixels, stride, (float)bx, (float)sym_y, 3.5f, UI_SUCCESS);
        } else {
            /* 未开启 / 离线淡灰色弱化线 */
            draw_line(pixels, stride, bx, by1, bx, bus_y, 1, UI_BORDER);
        }
    }

    /* 汇聚横线及连接节点 */
    uint32_t bus_color = any_block ? UI_DANGER : any_unknown ? UI_WARNING : UI_SUCCESS;
    draw_line(pixels, stride, lx1, bus_y, lx2, bus_y, 2, bus_color);
    for (int i = 0; i < 4; ++i) {
        int bx = sec1.x + 16 + i * (card_w1 + gap1) + card_w1 / 2;
        support_fill_circle_mini(pixels, stride, (float)bx, (float)bus_y, 3.0f, bus_color);
    }

    /* 粗壮导向干线与实心大三角向下箭头强力注入总闸门 (通道长度达 50px 以上) */
    int shaft_end_y = gate.y - 14;
    draw_line(pixels, stride, mid_x, bus_y, mid_x, shaft_end_y, 3, bus_color);
    support_draw_arrow_down(pixels, stride, mid_x, gate.y, 22, 14, bus_color);

    /* 居中横条 (总闸门) */
    fill_round_rect(pixels, stride, gate, 8, any_block ? UI_DANGER_SOFT : any_unknown ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
    draw_rect_outline(pixels, stride, gate, 8, 2, bus_color);
    if (!any_block && any_unknown) {
        draw_text_center(pixels, stride, gate, ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM), 13, UI_WARNING);
    } else if (!any_block) {
        const char *pass_txt = ptc_ui_text(PTC_UI_T_DETAIL_RULES_GATE_PASS);
        support_draw_checkmark(pixels, stride, gate.x + 24, gate.y + gate_h / 2, 7, UI_SUCCESS, UI_ON_ACCENT);
        draw_text_center(pixels, stride, gate, pass_txt, 13, UI_INK);
        const char *note = ptc_ui_text(PTC_UI_T_DETAIL_RULES_CONVERGE_SUB);
        int note_w = measure_text(note, 10);
        int pass_w = measure_text(pass_txt, 13);
        int note_x = gate.x + gate.width - note_w - 18;
        if (note_x > gate.x + gate.width / 2 + pass_w / 2 + 16) {
            draw_text(pixels, stride, note_x, gate.y + gate_h / 2 + 4, note, 10, UI_MUTED);
        }
    } else {
        const char *block_txt = ptc_ui_text(PTC_UI_T_DETAIL_RULES_GATE_BLOCKED_TITLE);
        int bx = gate.x + 24;
        int by = gate.y + gate_h / 2;
        support_fill_circle_mini(pixels, stride, (float)bx, (float)by, 7.0f, UI_DANGER);
        draw_line(pixels, stride, bx - 3, by - 3, bx + 3, by + 3, 2, UI_ON_ACCENT);
        draw_line(pixels, stride, bx - 3, by + 3, bx + 3, by - 3, 2, UI_ON_ACCENT);
        draw_text_center(pixels, stride, gate, block_txt, 13, UI_DANGER);
    }
}

static void draw_rules_priority_pipeline(uint32_t *pixels, uint32_t stride,
                                        const PtcUiModel *model,
                                        const PtcUiTodayDecision *decision,
                                        int x, int y, int full_w, int full_h,
                                        int target_minutes, int played_minutes, int remaining_minutes,
                                        bool is_today, bool total_known, bool total_unlimited)
{
    (void)model;
    char text[512], value[64], val[64], fitted_sub[256];
    UiRect sec2 = {x, y, full_w, full_h};
    detail_panel(pixels, stride, sec2);

    /* 标题行: 第一步：今日可用额度从何而来 */
    support_fill_circle_mini(pixels, stride, (float)(sec2.x + 24), (float)(sec2.y + 18), 9.0f, UI_ACCENT);
    draw_text_center(pixels, stride, (UiRect){sec2.x + 15, sec2.y + 9, 18, 18}, "1", 12, UI_ON_ACCENT);
    const char *sec2_title = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC2_TITLE);
    int t2_w = measure_text(sec2_title, 14);
    draw_text_bold(pixels, stride, sec2.x + 40, sec2.y + 22, sec2_title, 14, UI_INK);

    const char *sec2_sub = ptc_ui_text(PTC_UI_T_DETAIL_RULES_SEC2_SUB);
    int sub2_x = sec2.x + 40 + t2_w + 14;
    int max_sub2_w = (sec2.x + sec2.width - 20) - sub2_x;
    if (max_sub2_w > 60) {
        fit_text(fitted_sub, sizeof(fitted_sub), sec2_sub, 12, max_sub2_w);
        draw_text(pixels, stride, sub2_x, sec2.y + 22, fitted_sub, 12, UI_MUTED);
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

    int card_y2 = sec2.y + 28;
    int gap2 = (full_w > 1100) ? 20 : 12;
    int card_w2 = (full_w - 32 - 3 * gap2) / 4;
    int card_h2 = (full_h >= 180) ? 84 : 72;

    for (int i = 0; i < 4; ++i) {
        int cx = sec2.x + 16 + i * (card_w2 + gap2);
        UiRect c = {cx, card_y2, card_w2, card_h2};
        bool is_selected = (i == selected_idx);
        bool is_overridden = (selected_idx >= 0 && i > selected_idx);

        uint32_t domain = (i == 0) ? UI_CORAL : ((i == 1) ? UI_ACCENT : ((i == 2) ? UI_WARNING : UI_HERO));

        if (is_selected) {
            fill_round_rect(pixels, stride, c, 8, UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, c, 8, 2, UI_SUCCESS);
        } else if (is_overridden) {
            fill_round_rect(pixels, stride, c, 8, UI_RAISED);
            draw_rect_outline(pixels, stride, c, 8, 1, UI_BORDER);
        } else {
            fill_round_rect(pixels, stride, c, 8, UI_PAGE);
            draw_rect_outline(pixels, stride, c, 8, 1, UI_BORDER);
        }

        /* 顶部行：Emoji + P1~P4芯片 + 规则名称 + 右侧状态标签 */
        int top_y = c.y + 12;
        const char *prio_emojis[] = {"📅", "📅", "🎁", "🕒"};
        draw_text(pixels, stride, c.x + 12, top_y + 9, prio_emojis[i], 14, UI_INK);

        const char *prio_tags[] = {"P1", "P2", "P3", "P4"};
        char tag_buf[8];
        snprintf(tag_buf, sizeof(tag_buf), "%s", prio_tags[i]);
        int tag_w = measure_text(tag_buf, 11) + 8;
        UiRect tag_chip = {c.x + 32, top_y - 4, tag_w, 16};
        fill_round_rect(pixels, stride, tag_chip, 3, is_selected ? UI_SUCCESS : (is_overridden ? UI_RAISED : UI_ACCENT_SOFT));
        draw_text_center(pixels, stride, tag_chip, tag_buf, 11,
                         is_selected ? UI_ON_ACCENT : (is_overridden ? UI_DISABLED : domain));

        int name_x = tag_chip.x + tag_w + 6;
        int max_name_w = c.x + c.width - name_x - (is_selected ? 64 : 10);
        char fitted_title[64];
        fit_text(fitted_title, sizeof(fitted_title), ptc_ui_text(card_titles[i]), 12, max_name_w);
        draw_text_bold(pixels, stride, name_x, top_y + 8, fitted_title, 12, is_overridden ? UI_DISABLED : UI_INK);

        /* 右上角高亮标签：Hero 命中卡突出显示 */
        if (is_selected) {
            const char *act_lbl = ptc_ui_text(PTC_UI_T_DETAIL_RULES_ACTIVE_BADGE);
            int ab_w = measure_text(act_lbl, 10) + 12;
            UiRect act_pill = {c.x + c.width - ab_w - 8, top_y - 4, ab_w, 16};
            fill_round_rect(pixels, stride, act_pill, 4, UI_SUCCESS);
            draw_text_center(pixels, stride, act_pill, act_lbl, 10, UI_ON_ACCENT);
        }

        /* 中部核心指标/状态 */
        int mid_y = c.y + 40;
        if (is_selected) {
            format_decision_rule(steps[i], val, sizeof(val));
            char bold_val[64];
            snprintf(bold_val, sizeof(bold_val), "%s", val);
            draw_text_bold(pixels, stride, c.x + 14, mid_y + 8, bold_val, 16, UI_SUCCESS);
        } else if (is_overridden) {
            if (steps[i]->rule.minutes > 0 || steps[i]->rule.mode == PTC_RULE_MODE_UNLIMITED) {
                format_decision_rule(steps[i], val, sizeof(val));
                char ov_val[128];
                snprintf(ov_val, sizeof(ov_val), "%s (%s)", val, ptc_ui_text(PTC_UI_T_DETAIL_RULES_OVERRIDDEN_BADGE));
                draw_text(pixels, stride, c.x + 14, mid_y + 6, ov_val, 12, UI_DISABLED);
            } else {
                draw_text(pixels, stride, c.x + 14, mid_y + 6, ptc_ui_text(PTC_UI_T_DETAIL_RULES_OVERRIDDEN_BADGE), 12, UI_DISABLED);
            }
        } else {
            const char *st_lbl;
            if (i == 0 && !is_today) st_lbl = ptc_ui_text(PTC_UI_T_REASON_TODAY_ONLY);
            else st_lbl = ptc_ui_text(PTC_UI_T_DETAIL_RULES_NOT_SET_BADGE);
            draw_text(pixels, stride, c.x + 14, mid_y + 6, st_lbl, 12, UI_MUTED);
        }

        /* 底部解释说明行 */
        int desc_y = c.y + card_h2 - 10;
        char fitted_desc[128];
        fit_text(fitted_desc, sizeof(fitted_desc), ptc_ui_text(card_descs[i]), 10, c.width - 24);
        draw_text(pixels, stride, c.x + 14, desc_y, fitted_desc, 10,
                  is_selected ? domain : (is_overridden ? UI_DISABLED : UI_MUTED));

        /* 串行优先级流向箭头与阻断屏障 (节点间横向连接) */
        if (i < 3) {
            int x_start = c.x + c.width + 2;
            int x_end = c.x + c.width + gap2 - 2;
            int ay = c.y + card_h2 / 2;
            int ax_mid = (x_start + x_end) / 2;

            if (selected_idx >= 0 && i == selected_idx) {
                /* 关键设计：命中规则处切断后续流向，绘制阻断屏障 */
                draw_line(pixels, stride, x_start, ay, ax_mid - 2, ay, 2, UI_SUCCESS);
                draw_line(pixels, stride, ax_mid - 2, ay - 10, ax_mid - 2, ay + 10, 2, UI_DANGER);
                draw_line(pixels, stride, ax_mid + 2, ay - 10, ax_mid + 2, ay + 10, 2, UI_BORDER);
            } else if (selected_idx < 0 || i < selected_idx) {
                /* 前序规则未命中，顺畅流向下一级优先级 */
                draw_line(pixels, stride, x_start, ay, x_end, ay, 2, UI_ACCENT);
                draw_line(pixels, stride, x_end - 4, ay - 4, x_end, ay, 2, UI_ACCENT);
                draw_line(pixels, stride, x_end - 4, ay + 4, x_end, ay, 2, UI_ACCENT);
            } else {
                /* 已被高优先级覆盖的后续卡片，弱化浅线表示未激活 */
                draw_line(pixels, stride, x_start, ay, x_end, ay, 1, UI_BORDER);
            }
        }
    }

    /* 居中额度结果判定横条 */
    int mid_x = sec2.x + sec2.width / 2;
    int res_w = (full_w > 1000) ? 1040 : full_w - 40;
    int res_h = (full_h >= 180) ? 34 : 30;
    int res_y = sec2.y + full_h - res_h - 6;
    UiRect r_bar = {mid_x - res_w / 2, res_y, res_w, res_h};
    fill_round_rect(pixels, stride, r_bar, 8, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, r_bar, 8, 1, UI_ACCENT);

    detail_balance(total_known, total_unlimited, target_minutes, value, sizeof(value));
    char used_left[64];
    if (is_today && total_known && !total_unlimited && model->played_minutes_available && played_minutes >= 0) {
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
    /* 结果条左侧时钟 Emoji，文字整体居中 */
    draw_text(pixels, stride, r_bar.x + 16, r_bar.y + r_bar.height / 2 + 5, "⏰", 14, UI_INK);
    draw_text_center(pixels, stride, r_bar, text, 12, UI_ACCENT);

    /* 纵向导通线条：从生效卡片引出醒目纵线与实心箭头注入 r_bar */
    if (selected_idx >= 0 && selected_idx < 4) {
        int sel_x = sec2.x + 16 + selected_idx * (card_w2 + gap2) + card_w2 / 2;
        int by1 = card_y2 + card_h2;
        int by2 = r_bar.y;
        draw_line(pixels, stride, sel_x, by1, sel_x, by2 - 6, 2, UI_SUCCESS);
        support_fill_circle_mini(pixels, stride, (float)sel_x, (float)(by1 + 2), 2.5f, UI_SUCCESS);
        support_draw_arrow_down(pixels, stride, sel_x, by2, 10, 6, UI_SUCCESS);
    }
}

static void draw_today_rules(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                             const PtcUiTodayProjection *p, const PtcUiTodayDecision *decision)
{
    PtcUiRect body = ptc_ui_home_details_body_rect();
    ui_set_vertical_clip(body.y, body.y + body.h);

    /* Use the same current-state projection as the overview. A future break
     * must never turn a blocked or stale result into permission to play. */
    {
        bool available = detail_available(model, p);
        bool blocked = p->fresh && p->active_reasons != 0;
        uint32_t fg = available ? UI_SUCCESS : blocked ? UI_DANGER : UI_WARNING;
        uint32_t bg = available ? UI_SUCCESS_SOFT : blocked ? UI_DANGER_SOFT : UI_WARNING_SOFT;
        UiRect banner = {40, 72, 1200, 34};
        char current[256], fitted[256];
        detail_current(model, p, current, sizeof(current));
        fit_text(fitted, sizeof(fitted), current, 13, banner.width - 64);
        fill_round_rect(pixels, stride, banner, 8, bg);
        draw_rect_outline(pixels, stride, banner, 8, 1, fg);
        support_draw_checkmark(pixels, stride, banner.x + 22, banner.y + 17, 9, fg, bg);
        draw_text_bold(pixels, stride, banner.x + 40, banner.y + 22, fitted, 13, UI_INK);
    }

    /* 2. 区块 ① 先算总量：今天一共能玩多久？ (y = 110, h = 152) */
    draw_rules_priority_pipeline(pixels, stride, model, decision, 40, 110, 1200, 152,
                                 (int)model->forecast[0].minutes, (int)model->played_minutes,
                                 p->total_remaining, true, p->total_known, p->total_unlimited);

    /* 3. 中间因果流向提示：从步骤①输出注入步骤②门禁 (y = 262 ~ 330, 间距扩大至 68px，垂直箭头舒展无遮盖) */
    {
        int mid_x = 40 + 1200 / 2;
        int flow_y1 = 262;
        int flow_y2 = 330;
        const char *flow_txt = ptc_ui_text(PTC_UI_T_DETAIL_RULES_FLOW_INTO_GATE);
        int flow_w = measure_text(flow_txt, 11);
        int pill_w = flow_w + 24;
        int pill_h = 20;
        int pill_y = 272;
        UiRect flow_pill = {mid_x - pill_w / 2, pill_y, pill_w, pill_h};

        /* 上方导流线 */
        draw_line(pixels, stride, mid_x, flow_y1, mid_x, pill_y, 2, UI_ACCENT);

        /* 流程流向胶囊 */
        fill_round_rect(pixels, stride, flow_pill, 6, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, flow_pill, 6, 1, UI_ACCENT);
        draw_text_center(pixels, stride, flow_pill, flow_txt, 11, UI_ACCENT);

        /* 下方导流长轴与实心向下大三角箭头直达步骤②顶部 (38px 舒展流道，完全不被文字条框遮盖) */
        int shaft_start_y = pill_y + pill_h;
        int arrow_tip_y = flow_y2;
        int arrow_h = 14;
        draw_line(pixels, stride, mid_x, shaft_start_y, mid_x, arrow_tip_y - arrow_h, 3, UI_ACCENT);
        support_draw_arrow_down(pixels, stride, mid_x, arrow_tip_y, 20, arrow_h, UI_ACCENT);
    }

    /* 4. 区块 ② 再看能不能玩：现在能开机吗？ (y = 330, h = 284) */
    draw_rules_conditions_gate(pixels, stride, model, decision, 40, 330, 1200, 284,
                               (int)model->forecast[0].minutes, (int)model->played_minutes,
                               p->total_remaining, ptc_weekday_from_day_index(model->day_index),
                               true, p->total_known, p->total_unlimited, p->conditions);

    ui_set_vertical_clip(0, SCREEN_HEIGHT);
}

static void draw_rules_help_dialog(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell = *model;
    snprintf(shell.overlay_title, sizeof(shell.overlay_title), "%s", ptc_ui_text(PTC_UI_T_RULES_EXPLAIN_TITLE));
    shell.overlay_body[0] = '\0';
    shell.overlay_open_frames = PTC_UI_OVERLAY_OPEN_FRAMES;
    draw_dialog_shell(pixels, stride, &shell, &dialog, 1000, 560);

    const PtcUiTextId titles[] = {
        PTC_UI_T_RULES_EXPLAIN_CARD1_TITLE,
        PTC_UI_T_RULES_EXPLAIN_CARD2_TITLE,
        PTC_UI_T_RULES_EXPLAIN_CARD3_TITLE,
        PTC_UI_T_RULES_EXPLAIN_CARD4_TITLE
    };
    const PtcUiTextId items[] = {
        PTC_UI_T_RULES_EXPLAIN_CARD1_BODY,
        PTC_UI_T_RULES_EXPLAIN_CARD2_BODY,
        PTC_UI_T_RULES_EXPLAIN_CARD3_BODY,
        PTC_UI_T_RULES_EXPLAIN_CARD4_BODY
    };
    const char *emojis[] = {"🛡️", "📅", "⚖️", "🌿"};
    uint32_t accent_colors[] = {UI_CORAL, UI_ACCENT, UI_SUCCESS, UI_HERO};

    int card_w = (dialog.width - 68 - 18) / 2;
    int card_h = 160;
    for (int i = 0; i < 4; ++i) {
        int col = i % 2;
        int row = i / 2;
        int cx = dialog.x + 34 + col * (card_w + 18);
        int cy = dialog.y + 104 + row * (card_h + 16);
        UiRect c = {cx, cy, card_w, card_h};
        fill_round_rect(pixels, stride, c, 12, UI_PAGE);
        draw_rect_outline(pixels, stride, c, 12, 1, UI_BORDER);

        /* 左侧领域色条 */
        fill_round_rect(pixels, stride, (UiRect){c.x + 4, c.y + 12, 4, c.height - 24}, 2, accent_colors[i]);

        /* 顶部标题与 Emoji */
        draw_text(pixels, stride, c.x + 16, c.y + 30, emojis[i], 16, UI_INK);
        draw_text_bold(pixels, stride, c.x + 42, c.y + 30, ptc_ui_text(titles[i]), 15, UI_INK);
        draw_line(pixels, stride, c.x + 16, c.y + 44, c.x + c.width - 16, c.y + 44, 1, UI_BORDER);

        /* 详细解释 */
        draw_wrapped_text(pixels, stride, c.x + 16, c.y + 54,
                          ptc_ui_text(items[i]), 13, c.width - 32, 20, 5, UI_INK);
    }

    /* 底部返回/关闭按钮 */
    home_button(pixels, stride, ptc_ui_rules_dialog_close_rect(), ptc_ui_text(PTC_UI_T_B_BACK), true, false, false);
}

static void draw_home_details(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiTodayProjection projection;
    PtcUiTodayDecision decision;
    PtcUiHomeDetailsStatus status;
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
    ptc_ui_project_home_details_status(model, now, day, second, &status);
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
        snprintf(sub, sizeof(sub), "%s", ptc_ui_text(PTC_UI_T_DETAIL_PLANNING));
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

    if (model->home_details_page == 0) draw_today_plan(pixels, stride, model, &projection, &decision, &status);
    else draw_today_rules(pixels, stride, model, &projection, &decision);

    /* 底部按钮 */
    const char *back_label = ptc_ui_text(
        model->home_details_page == 1
            ? (model->home_details_entry_page == 1
                ? (model->parent_page == PTC_UI_PARENT_PLAN ? PTC_UI_T_B_BACK_TO_PLANS : PTC_UI_T_DETAIL_BACK_TODAY)
                : PTC_UI_T_DETAIL_BACK)
            : (model->view == PTC_UI_CHILD ? PTC_UI_T_DETAIL_BACK_CHILD : PTC_UI_T_DETAIL_BACK_TODAY));
    home_button(pixels, stride, ptc_ui_home_details_action_rect(model, 2), back_label,
        true, model->home_details_focus == 2, false);
    home_button(pixels, stride, ptc_ui_home_details_action_rect(model, 1), ptc_ui_text(PTC_UI_T_Y_REFRESH),
        false, model->home_details_focus == 1, model->waiting);

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

    if (model->rules_dialog_open) {
        draw_rules_help_dialog(pixels, stride, model);
    }
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

    /* 2. 顶部全局提示横幅 (y = 98, h = 24) */
    UiRect banner = {108, 98, 1064, 24};
    fill_round_rect(pixels, stride, banner, 6, fresh ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
    draw_rect_outline(pixels, stride, banner, 6, 1, fresh ? UI_SUCCESS : UI_WARNING);
    if (fresh) {
        support_draw_checkmark(pixels, stride, banner.x + 20, banner.y + 12, 5, UI_SUCCESS, UI_ON_ACCENT);
        draw_text_bold(pixels, stride, banner.x + 34, banner.y + 16,
                       ptc_ui_text(is_today ? PTC_UI_T_DETAIL_RULES_GLOBAL_BANNER : PTC_UI_T_DETAIL_RULES_FORECAST_BANNER),
                       12, UI_INK);
    } else {
        support_draw_info(pixels, stride, banner.x + 20, banner.y + 12, 5, UI_WARNING);
        draw_text_bold(pixels, stride, banner.x + 34, banner.y + 16,
                       ptc_ui_text(PTC_UI_T_RULES_PENDING_CONFIRMATION), 12, UI_WARNING);
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

    /* 4. 区块 ① 先算时间：今天总共能玩多久 (y = 122, h = 150) */
    draw_rules_priority_pipeline(pixels, stride, model, &decision, 108, 122, 1064, 150,
                                 target_minutes, played_minutes, remaining_minutes,
                                 is_today, total_known, total_unlimited);

    /* 5. 中间因果流向提示：从步骤①输出注入步骤②门禁 (y = 272 ~ 330) */
    {
        int mid_x = 108 + 1064 / 2;
        int flow_y1 = 272;
        int flow_y2 = 330;
        const char *flow_txt = ptc_ui_text(PTC_UI_T_DETAIL_RULES_FLOW_INTO_GATE);
        int flow_w = measure_text(flow_txt, 11);
        int pill_w = flow_w + 24;
        int pill_h = 20;
        int pill_y = 280;
        UiRect flow_pill = {mid_x - pill_w / 2, pill_y, pill_w, pill_h};

        /* 上方导流线 */
        draw_line(pixels, stride, mid_x, flow_y1, mid_x, pill_y, 2, UI_ACCENT);

        /* 流程流向胶囊 */
        fill_round_rect(pixels, stride, flow_pill, 6, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, flow_pill, 6, 1, UI_ACCENT);
        draw_text_center(pixels, stride, flow_pill, flow_txt, 11, UI_ACCENT);

        /* 下方导流长轴与实心向下大三角箭头直达步骤②顶部 (30px 舒展流道，完全不被文字条框遮盖) */
        int shaft_start_y = pill_y + pill_h;
        int arrow_tip_y = flow_y2;
        int arrow_h = 14;
        draw_line(pixels, stride, mid_x, shaft_start_y, mid_x, arrow_tip_y - arrow_h, 3, UI_ACCENT);
        support_draw_arrow_down(pixels, stride, mid_x, arrow_tip_y, 20, arrow_h, UI_ACCENT);
    }

    /* 6. 区块 ② 综合把关：现在能不能开机玩 (y = 330, h = 244) */
    draw_rules_conditions_gate(pixels, stride, model, &decision, 108, 330, 1064, 244,
                               target_minutes, played_minutes, remaining_minutes,
                               weekday, is_today, total_known, total_unlimited, conditions);

    /* 7. 底部说明卡片: 日历属性解读 (y = 578, h = 32) */
    UiRect cal_card = {108, 578, 1064, 32};
    fill_round_rect(pixels, stride, cal_card, 6, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, cal_card, 6, 1, UI_BORDER);
    draw_text(pixels, stride, cal_card.x + 14, cal_card.y + 21, "📅", 14, UI_INK);
    draw_text(pixels, stride, cal_card.x + 36, cal_card.y + 20, ptc_ui_text(PTC_UI_T_CALENDAR_EXPLAIN_TITLE), 11, UI_INK);
    draw_line(pixels, stride, cal_card.x + 130, cal_card.y + 5, cal_card.x + 130, cal_card.y + 27, 1, UI_BORDER);
    snprintf(cal_desc, sizeof(cal_desc), ptc_ui_text(PTC_UI_T_CALENDAR_EXPLAIN_DESC), date_buf, type_label);
    draw_text(pixels, stride, cal_card.x + 144, cal_card.y + 20, cal_desc, 11, UI_INK);

    /* 8. 底部导航与按钮 (y = 618) */
    draw_text(pixels, stride, 114, 630, ptc_ui_text(PTC_UI_T_DAY_DECISION_SWITCH_HINT), 11, UI_MUTED);

    home_button(pixels, stride, ptc_ui_day_decision_refresh_rect(), ptc_ui_text(PTC_UI_T_Y_REFRESH),
                false, false, model->waiting);
    home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_B_RETURN),
                false, true, false);

    if (model->rules_dialog_open) {
        draw_rules_help_dialog(pixels, stride, model);
    }
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
