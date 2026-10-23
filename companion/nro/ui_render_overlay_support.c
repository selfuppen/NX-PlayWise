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
    const char *shell_hint = !g_theme.system_theme_available
        ? ptc_ui_text(PTC_UI_T_THE_SYSTEM_THEME_IS_TEMPORARILY_UNAVAILABLE_AND)
        : ptc_ui_text(PTC_UI_T_THEME_SHELL_HINT);

    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_APPEARANCE_THEME), shell_hint);

    // --- Row 0: Mode Selection (3 Cards) ---
    for (int index = 0; index < 3; ++index) {
        UiRect option = to_uirect(ptc_ui_theme_option_rect(index));
        bool selected = (index == model->overlay_selection);
        bool is_current = ((int)g_theme.preference == index);

        fill_round_rect(pixels, stride, option, 14, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 14, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);

        draw_text(pixels, stride, option.x + 18, option.y + 26, LABELS[index], 20, UI_INK);

        if (is_current) {
            UiRect cur_badge = {option.x + option.width - 78, option.y + 12, 62, 22};
            fill_round_rect(pixels, stride, cur_badge, 11, UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, cur_badge, 11, 1, UI_SUCCESS_BORDER);
            draw_text_center(pixels, stride, cur_badge,
                             ptc_ui_text(PTC_UI_T_ADJUST_BADGE_ACTIVE), 12, UI_SUCCESS);
        }

        draw_wrapped_text(pixels, stride, option.x + 18, option.y + 50, DETAILS[index],
                          13, option.width - 36, 18, 1, UI_MUTED);

        // Preview box inside Mode Card
        UiRect preview = {option.x + 16, option.y + 68, option.width - 32, 108};
        draw_rect_outline(pixels, stride, preview, 10, 1, selected ? UI_ACCENT : UI_BORDER);

        if (index == PTC_UI_THEME_LIGHT) {
            fill_round_rect(pixels, stride, preview, 10, UI_RGB(0xF0F3FA));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 10, preview.width - 20, 20}, 5, UI_RGB(0xFFFFFF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16, preview.y + 15, 10, 10}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 32, preview.y + 18, 48, 4}, 2, UI_RGB(0x172640));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 36, preview.width - 20, 32}, 6, UI_RGB(0xFFFFFF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 18, preview.y + 44, 56, 5}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 18, preview.y + 54, 88, 4}, 2, UI_RGB(0x53627A));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 74, (preview.width - 26) / 2, 24}, 5, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16 + (preview.width - 26) / 2, preview.y + 74, (preview.width - 26) / 2, 24}, 5, UI_RGB(0xE8EDF7));
        } else if (index == PTC_UI_THEME_DARK) {
            fill_round_rect(pixels, stride, preview, 10, UI_RGB(0x111122));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 10, preview.width - 20, 20}, 5, UI_RGB(0x1A2333));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16, preview.y + 15, 10, 10}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 32, preview.y + 18, 48, 4}, 2, UI_RGB(0xEEEEFF));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 36, preview.width - 20, 32}, 6, UI_RGB(0x1A2333));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 18, preview.y + 44, 56, 5}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 18, preview.y + 54, 88, 4}, 2, UI_RGB(0xAABBCC));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 74, (preview.width - 26) / 2, 24}, 5, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16 + (preview.width - 26) / 2, preview.y + 74, (preview.width - 26) / 2, 24}, 5, UI_RGB(0x223344));
        } else {
            int half_w = preview.width / 2;
            fill_round_rect(pixels, stride, (UiRect){preview.x, preview.y, half_w, preview.height}, 10, UI_RGB(0xF0F3FA));
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w, preview.y, preview.width - half_w, preview.height}, 10, UI_RGB(0x111122));
            fill_rect_packed(pixels, stride, (UiRect){preview.x + half_w, preview.y, 1, preview.height}, UI_RGB(0x6E7D94));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 8, preview.y + 10, half_w - 14, 20}, 5, UI_RGB(0xFFFFFF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 15, 8, 8}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 26, preview.y + 18, 32, 4}, 2, UI_RGB(0x172640));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 8, preview.y + 36, half_w - 14, 32}, 6, UI_RGB(0xFFFFFF));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 44, 40, 5}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 54, half_w - 28, 4}, 2, UI_RGB(0x53627A));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 8, preview.y + 74, half_w - 14, 24}, 5, UI_ACCENT);

            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 6, preview.y + 10, half_w - 14, 20}, 5, UI_RGB(0x1A2333));
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 12, preview.y + 15, 8, 8}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 24, preview.y + 18, 32, 4}, 2, UI_RGB(0xEEEEFF));

            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 6, preview.y + 36, half_w - 14, 32}, 6, UI_RGB(0x1A2333));
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 12, preview.y + 44, 40, 5}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 12, preview.y + 54, half_w - 28, 4}, 2, UI_RGB(0xAABBCC));

            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 6, preview.y + 74, half_w - 14, 24}, 5, UI_RGB(0x223344));
        }

        // Action button at bottom of mode card
        UiRect select_btn = {option.x + 16, option.y + option.height - 38, option.width - 32, 28};
        fill_round_rect(pixels, stride, select_btn, 14, selected ? UI_ACCENT : UI_SURFACE);
        draw_rect_outline(pixels, stride, select_btn, 14, 1, selected ? UI_FOCUS : UI_CONTROL);
        const char *btn_label = selected
            ? ptc_ui_text(PTC_UI_T_A_CONFIRM_SAVE)
            : (is_current ? ptc_ui_text(PTC_UI_T_THEME_STYLE_IN_USE) : ptc_ui_text(PTC_UI_T_THEME_STYLE_SELECT));
        draw_text_center(pixels, stride, select_btn, btn_label, 13, selected ? UI_ON_ACCENT : UI_INK);
    }

    // --- Row 1: Theme Style Header ---
    const char *style_title = ptc_ui_text(PTC_UI_T_THEME_STYLE_TITLE);
    const char *style_sub = ptc_ui_text(PTC_UI_T_THEME_STYLE_SUBTITLE);
    int title_w = measure_text(style_title, 17);
    draw_text(pixels, stride, 56, 432, style_title, 17, UI_INK);
    draw_text(pixels, stride, 56 + title_w + 18, 432, style_sub, 13, UI_MUTED);

    // --- Row 1: Theme Style Cards (4 Cards) ---
    bool is_en = (ptc_ui_language_get_resolved() == PTC_UI_LANGUAGE_ENGLISH);
    for (int i = 0; i < PTC_UI_THEME_STYLE_COUNT; ++i) {
        UiRect s_rect = to_uirect(ptc_ui_theme_style_rect(i));
        bool selected = (model->overlay_selection == 3 + i);
        bool is_current = (ptc_ui_theme_get_style() == (PtcUiThemeStyle)i);
        const PtcUiPalette *spal = ptc_ui_theme_style_palette(g_theme.resolved, (PtcUiThemeStyle)i);

        fill_round_rect(pixels, stride, s_rect, 14, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, s_rect, 14, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);

        // Color Swatches Row
        int sw_x = s_rect.x + 16;
        int sw_y = s_rect.y + 16;
        fill_round_rect(pixels, stride, (UiRect){sw_x, sw_y, 28, 16}, 4, UI_RGB(spal->accent));
        fill_round_rect(pixels, stride, (UiRect){sw_x + 34, sw_y, 22, 16}, 4, UI_RGB(spal->focus));
        fill_round_rect(pixels, stride, (UiRect){sw_x + 62, sw_y, 22, 16}, 4, UI_RGB(spal->accent_soft));
        draw_rect_outline(pixels, stride, (UiRect){sw_x + 62, sw_y, 22, 16}, 4, 1, UI_RGB(spal->accent));
        fill_round_rect(pixels, stride, (UiRect){sw_x + 90, sw_y, 22, 16}, 4, UI_RGB(spal->night));

        // Active Badge if currently active style
        if (is_current) {
            UiRect cur_badge = {s_rect.x + s_rect.width - 66, s_rect.y + 14, 52, 20};
            fill_round_rect(pixels, stride, cur_badge, 10, UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, cur_badge, 10, 1, UI_SUCCESS_BORDER);
            draw_text_center(pixels, stride, cur_badge,
                             ptc_ui_text(PTC_UI_T_ADJUST_BADGE_ACTIVE), 11, UI_SUCCESS);
        }

        // Style Title
        draw_text(pixels, stride, s_rect.x + 16, s_rect.y + 58, ptc_ui_theme_style_label((PtcUiThemeStyle)i), 18, UI_INK);

        // Style Keyword (Chinese only) & Description
        if (!is_en) {
            draw_text(pixels, stride, s_rect.x + 16, s_rect.y + 80, ptc_ui_theme_style_name((PtcUiThemeStyle)i), 12, UI_ACCENT);
            draw_wrapped_text(pixels, stride, s_rect.x + 16, s_rect.y + 100, ptc_ui_theme_style_desc((PtcUiThemeStyle)i),
                              12, s_rect.width - 32, 16, 2, UI_MUTED);
        } else {
            draw_wrapped_text(pixels, stride, s_rect.x + 16, s_rect.y + 86, ptc_ui_theme_style_desc((PtcUiThemeStyle)i),
                              12, s_rect.width - 32, 17, 2, UI_MUTED);
        }

        // Action Pill at Bottom
        UiRect action_btn = {s_rect.x + 16, s_rect.y + s_rect.height - 38, s_rect.width - 32, 28};
        fill_round_rect(pixels, stride, action_btn, 14, selected ? UI_ACCENT : UI_SURFACE);
        draw_rect_outline(pixels, stride, action_btn, 14, 1, selected ? UI_FOCUS : UI_CONTROL);
        draw_text_center(pixels, stride, action_btn,
                         selected ? ptc_ui_text(PTC_UI_T_THEME_STYLE_APPLY)
                                  : (is_current ? ptc_ui_text(PTC_UI_T_THEME_STYLE_IN_USE) : ptc_ui_text(PTC_UI_T_THEME_STYLE_SELECT)),
                         12, selected ? UI_ON_ACCENT : UI_INK);
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
