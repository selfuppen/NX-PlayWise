#include "ui_render_internal.h"
#include "../../common/rules/rules.h"
#include "../../common/update/update.h"
#include "../../common/version.h"

static PtcUiTextId update_status_key(int status)
{
    static const PtcUiTextId keys[] = {
        PTC_UI_T_UPDATE_IDLE, PTC_UI_T_UPDATE_CHECKING, PTC_UI_T_UPDATE_CURRENT, PTC_UI_T_UPDATE_AVAILABLE,
        PTC_UI_T_UPDATE_LOCAL_NEWER, PTC_UI_T_UPDATE_CHANNEL_EXCLUDED, PTC_UI_T_UPDATE_DOWNLOADING,
        PTC_UI_T_UPDATE_VERIFYING, PTC_UI_T_UPDATE_BACKUP_PENDING, PTC_UI_T_UPDATE_INSTALLING,
        PTC_UI_T_UPDATE_AWAITING_REOPEN, PTC_UI_T_UPDATE_COMPLETED, PTC_UI_T_UPDATE_FAILED,
        PTC_UI_T_UPDATE_CANCELED, PTC_UI_T_UPDATE_RECOVERY_REQUIRED, PTC_UI_T_UPDATE_MANUAL_REQUIRED,
        PTC_UI_T_UPDATE_BACKEND_PENDING, PTC_UI_T_UPDATE_RECONFIRM, PTC_UI_T_UPDATE_BACKUP_UNKNOWN
    };
    return status >= 0 && (size_t)status < sizeof(keys) / sizeof(keys[0]) ? keys[status] : PTC_UI_T_UPDATE_FAILED;
}

static void draw_online_update(uint32_t *pixels, uint32_t stride, const PtcUiModel *m)
{
    char text[512];
    draw_settings_page_shell(pixels, stride, m, ptc_ui_text(PTC_UI_T_UPDATE_TITLE), ptc_ui_text(PTC_UI_T_UPDATE_TRUST));
    if (m->overlay == PTC_UI_OVERLAY_ONLINE_UPDATE && m->update_notes_open) {
        home_button(pixels, stride, ptc_ui_settings_page_back_rect(), ptc_ui_text(PTC_UI_T_B_BACK), false, false, false);
        const int lines_per_page = 14;
        int lines = count_wrapped_lines(m->update_notes, 20, 1124, 2048);
        int pages = (lines + lines_per_page - 1) / lines_per_page;
        if (pages < 1) pages = 1;
        int page = (m->update_notes_page % pages + pages) % pages;
        draw_plan_card(pixels, stride, (UiRect){54, 166, 1172, 438}, false);
        draw_wrapped_text_from_line(pixels, stride, 78, 198, m->update_notes, 20, 1124,
            28, lines_per_page, page * lines_per_page, UI_INK);
        snprintf(text, sizeof(text), "%d / %d", page + 1, pages);
        draw_text(pixels, stride, 78, 638, text, 18, UI_MUTED);
        home_button(pixels, stride, ptc_ui_settings_page_footer_rect(2), ptc_ui_text(PTC_UI_T_GUIDE_PREV), false, false, false);
        home_button(pixels, stride, ptc_ui_settings_page_primary_rect(), ptc_ui_text(PTC_UI_T_GUIDE_NEXT), false, false, false);
        return;
    }
    if (m->overlay == PTC_UI_OVERLAY_UPDATE_BACKUPS) {
        home_button(pixels, stride, ptc_ui_settings_page_back_rect(), ptc_ui_text(PTC_UI_T_B_BACK), false, false, false);
        if (!m->update_backup_count) draw_text(pixels, stride, 72, 180, ptc_ui_text(PTC_UI_T_UPDATE_NO_BACKUPS), 22, UI_MUTED);
        for (int i = 0; i < 4 && m->update_backup_index + i < m->update_backup_count; ++i) {
            PtcUiRect rect = ptc_ui_update_backup_rect(i);
            draw_plan_card(pixels, stride, (UiRect){rect.x, rect.y, rect.w, rect.h}, m->overlay_selection == i);
            draw_wrapped_text(pixels, stride, rect.x + 24, rect.y + 28,
                m->update_backup_labels[m->update_backup_index + i], 20, rect.w - 48, 26, 2, UI_INK);
        }
        draw_wrapped_text(pixels, stride, 72, 620, ptc_ui_text(m->update_cleanup_failed ? PTC_UI_T_UPDATE_ERROR_CONFLICT : PTC_UI_T_UPDATE_BACKUP_HINT), 17, 1120, 21, 2, UI_MUTED);
        bool selected = m->update_backup_count > 0;
        home_button(pixels, stride, ptc_ui_settings_page_footer_rect(1), ptc_ui_text(PTC_UI_T_UPDATE_DELETE), false, false, !selected || m->parent_support_only);
        home_button(pixels, stride, ptc_ui_settings_page_footer_rect(2), ptc_ui_text(PTC_UI_T_GUIDE_PREV), false, false, m->update_backup_index == 0);
        home_button(pixels, stride, ptc_ui_settings_page_primary_rect(), ptc_ui_text(PTC_UI_T_GUIDE_NEXT), false, false, m->update_backup_index + 4 >= m->update_backup_count);
        return;
    }
    draw_plan_card(pixels, stride, (UiRect){54, 166, 1172, 246}, false);
    snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_VERSIONS), PLAYWISE_VERSION,
        m->update_installed_version[0] ? m->update_installed_version : "?", m->update_version[0] ? m->update_version : "?");
    draw_wrapped_text(pixels, stride, 78, 190, text, 18, 1124, 23, 2, UI_INK);
    snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_BACKEND), m->backend_release_id[0] ? m->backend_release_id : "?");
    fit_text(text, sizeof(text), text, 15, 1124);
    draw_text(pixels, stride, 78, 238, text, 15, UI_MUTED);
    draw_wrapped_text(pixels, stride, 78, 270, ptc_ui_text(update_status_key(m->update_status)), 22, 1124, 28, 2,
        m->update_status == PTC_UPDATE_COMPLETED ? UI_SUCCESS : (m->update_status == PTC_UPDATE_FAILED || m->update_status == PTC_UPDATE_RECOVERY_REQUIRED ? UI_DANGER : UI_ACCENT));
    if (m->update_size_kib) {
        snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_SIZE), m->update_size_kib, m->update_source == 2 ? "GitHub" : (m->update_source == 1 ? "Gitee" : "Gitee / GitHub"));
        fit_text(text, sizeof(text), text, 16, 1124);
        draw_text(pixels, stride, 78, 322, text, 16, UI_MUTED);
    }
    if (m->update_busy) {
        snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_TRANSFER), m->update_received_kib);
        draw_text(pixels, stride, 78, 350, text, 18, UI_MUTED);
    } else {
        const PtcUiTextId errors[] = {PTC_UI_T_UPDATE_IDLE, PTC_UI_T_UPDATE_ERROR_NETWORK, PTC_UI_T_UPDATE_MANUAL_REQUIRED,
            PTC_UI_T_UPDATE_ERROR_PACKAGE, PTC_UI_T_UPDATE_ERROR_STORAGE, PTC_UI_T_UPDATE_ERROR_BACKUP, PTC_UI_T_UPDATE_ERROR_CONFLICT};
        const char *notes = m->update_error > 0 && (size_t)m->update_error < sizeof(errors) / sizeof(errors[0]) ? ptc_ui_text(errors[m->update_error]) : m->update_notes;
        draw_wrapped_text(pixels, stride, 78, 348, notes, 17, 1124, 22, 2, UI_MUTED);
    }
    home_button(pixels, stride, ptc_ui_update_notes_rect(), ptc_ui_text(PTC_UI_T_UPDATE_NOTES), false, false,
        m->update_busy || !m->update_notes[0]);
    const PtcUiTextId actions[] = {m->update_status == PTC_UPDATE_BACKUP_UNKNOWN ? PTC_UI_T_UPDATE_WAIT_RECEIPT : PTC_UI_T_UPDATE_CHECK, PTC_UI_T_UPDATE_DOWNLOAD,
        m->update_check_on_startup ? PTC_UI_T_UPDATE_AUTO_ON : PTC_UI_T_UPDATE_AUTO_OFF,
        m->update_stable_only ? PTC_UI_T_UPDATE_STABLE : PTC_UI_T_UPDATE_FOLLOW,
        PTC_UI_T_UPDATE_BACKUPS, m->update_status == PTC_UPDATE_RECONFIRM ? PTC_UI_T_UPDATE_SUPPORT : PTC_UI_T_UPDATE_LOAD,
        m->update_status == PTC_UPDATE_RECOVERY_REQUIRED ? PTC_UI_T_UPDATE_RECOVER : PTC_UI_T_UPDATE_CANCEL, PTC_UI_T_UPDATE_EXIT};
    for (int i = 0; i < 8; ++i) {
        PtcUiRect rect = ptc_ui_update_action_rect(i);
        bool selected = (m->overlay_selection == i);
        bool enabled = ptc_ui_update_action_enabled(m, i);
        if (i == 2) {
            fill_round_rect(pixels, stride, to_uirect(rect), 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
            draw_rect_outline(pixels, stride, to_uirect(rect), 10, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
            draw_text(pixels, stride, rect.x + 20, rect.y + 27, ptc_ui_text(PTC_UI_T_UPDATE_AUTO_CHECK_LABEL), 16,
                      selected ? UI_ACCENT : (enabled ? UI_INK : UI_MUTED));
            UiRect sw_rect = {rect.x + rect.w - 58, rect.y + (rect.h - 22) / 2, 42, 22};
            draw_toggle_switch(pixels, stride, sw_rect, m->update_check_on_startup, selected, !enabled, NULL, NULL);
            const char *st_txt = ptc_ui_text(m->update_check_on_startup ? PTC_UI_T_ON : PTC_UI_T_MUTED);
            int st_w = measure_text(st_txt, 14);
            draw_text(pixels, stride, sw_rect.x - st_w - 10, rect.y + 27, st_txt, 14,
                      m->update_check_on_startup ? UI_SUCCESS : UI_MUTED);
        } else if (i == 3) {
            fill_round_rect(pixels, stride, to_uirect(rect), 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
            draw_rect_outline(pixels, stride, to_uirect(rect), 10, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
            draw_text(pixels, stride, rect.x + 20, rect.y + 27, ptc_ui_text(PTC_UI_T_UPDATE_CHANNEL_LABEL), 16,
                      selected ? UI_ACCENT : (enabled ? UI_INK : UI_MUTED));
            const char *ch_name = ptc_ui_text(m->update_stable_only ? PTC_UI_T_UPDATE_CHANNEL_STABLE_ONLY : PTC_UI_T_UPDATE_CHANNEL_WITH_BETA);
            int ch_w = measure_text(ch_name, 13) + 20;
            UiRect pill_rect = {rect.x + rect.w - ch_w - 16, rect.y + (rect.h - 24) / 2, ch_w, 24};
            uint32_t pill_bg = m->update_stable_only ? UI_PAGE : UI_ACCENT_SOFT;
            uint32_t pill_bd = m->update_stable_only ? UI_BORDER : UI_ACCENT;
            uint32_t pill_color = m->update_stable_only ? UI_MUTED : UI_ACCENT;
            fill_round_rect(pixels, stride, pill_rect, 5, pill_bg);
            draw_rect_outline(pixels, stride, pill_rect, 5, 1, pill_bd);
            draw_text_center(pixels, stride, pill_rect, ch_name, 13, pill_color);
        } else if (i == 4) {
            fill_round_rect(pixels, stride, to_uirect(rect), 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
            draw_rect_outline(pixels, stride, to_uirect(rect), 10, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
            draw_text(pixels, stride, rect.x + 20, rect.y + 27, ptc_ui_text(PTC_UI_T_UPDATE_BACKUPS), 16,
                      selected ? UI_ACCENT : (enabled ? UI_INK : UI_MUTED));
            const char *badge_text = ptc_ui_text(PTC_UI_T_UPDATE_VIEW_BACKUPS_BADGE);
            int bw = measure_text(badge_text, 13) + 16;
            UiRect b_rect = {rect.x + rect.w - bw - 16, rect.y + (rect.h - 24) / 2, bw, 24};
            fill_round_rect(pixels, stride, b_rect, 5, selected ? UI_ACCENT : UI_PAGE);
            draw_rect_outline(pixels, stride, b_rect, 5, 1, selected ? UI_ACCENT : UI_BORDER);
            draw_text_center(pixels, stride, b_rect, badge_text, 13, selected ? UI_ON_ACCENT : UI_MUTED);
        } else {
            fit_text(text, sizeof(text), ptc_ui_text(actions[i]), 18, rect.w - 32);
            home_button(pixels, stride, rect, text, i == 1, selected, !enabled);
        }
    }
}

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
    char value[256];
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
    bool update_available = (model->update_status == PTC_UPDATE_AVAILABLE && model->update_version[0]);
    if (update_available) {
        snprintf(value, sizeof(value), "%.24s  (%.88s)  ->  v%.24s", model->software_version,
                 model->app_release_id[0] ? model->app_release_id : ptc_ui_text(PTC_UI_T_UNKNOWN_IDENTITY),
                 model->update_version);
    } else {
        snprintf(value, sizeof(value), "%.24s  (%.88s)", model->software_version,
                 model->app_release_id[0] ? model->app_release_id : ptc_ui_text(PTC_UI_T_UNKNOWN_IDENTITY));
    }
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

    home_button(pixels, stride, ptc_ui_settings_page_footer_rect(2),
                ptc_ui_text(update_available ? PTC_UI_T_UPDATE_NOW_ENTRY : PTC_UI_T_UPDATE_ENTRY),
                update_available, false, false);
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

/* 单选指示：未选为控件描边圆环，选中为强调色圆环 + 实心圆点。 */
static void draw_radio_marker(uint32_t *pixels, uint32_t stride, int cx, int cy,
                              bool selected, uint32_t accent)
{
    draw_circle_outline(pixels, stride, cx, cy, 9, selected ? 3 : 2, selected ? accent : UI_CONTROL);
    if (selected) draw_circle_outline(pixels, stride, cx, cy, 4, 4, accent);
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
    /* 页面显示草稿选择；未按 A 前不写入 config.json。没有活动草稿时
     * （如宿主预览）回退到当前已保存外观。 */
    const PtcUiThemePreference draft_preference = model->theme_draft_valid
        ? model->theme_draft_preference : g_theme.preference;
    const PtcUiThemeStyle draft_style = model->theme_draft_valid
        ? model->theme_draft_style : g_theme.style;
    const PtcUiThemePreference saved_preference = model->theme_draft_valid
        ? model->theme_saved_preference : g_theme.preference;
    const PtcUiThemeStyle saved_style = model->theme_draft_valid
        ? model->theme_saved_style : g_theme.style;
    const int draft_mode = (int)draft_preference;
    const bool dirty = draft_preference != saved_preference || draft_style != saved_style;
    /* 模式预览使用草稿风格，选中风格后即时反映到上方三张模式卡。 */
    const PtcUiPalette *light_pal = ptc_ui_theme_style_palette(PTC_UI_RESOLVED_LIGHT, draft_style);
    const PtcUiPalette *dark_pal = ptc_ui_theme_style_palette(PTC_UI_RESOLVED_DARK, draft_style);

    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_APPEARANCE_THEME), shell_hint);

    // --- Row 0: Mode Selection (3 radio cards) ---
    for (int index = 0; index < 3; ++index) {
        UiRect option = to_uirect(ptc_ui_theme_option_rect(index));
        bool focused = (index == model->overlay_selection);
        bool selected = (index == draft_mode);

        if (focused) {
            UiRect halo = {option.x - 3, option.y - 3, option.width + 6, option.height + 6};
            draw_rect_outline(pixels, stride, halo, 17, 1, UI_FOCUS);
        }
        fill_round_rect(pixels, stride, option, 14, focused ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 14, focused ? 4 : 1, focused ? UI_ACCENT : UI_BORDER);

        draw_radio_marker(pixels, stride, option.x + 26, option.y + 24, selected, UI_ACCENT);
        draw_text(pixels, stride, option.x + 44, option.y + 31, LABELS[index], 20, UI_INK);

        draw_wrapped_text(pixels, stride, option.x + 18, option.y + 54, DETAILS[index],
                          13, option.width - 36, 18, 1, UI_MUTED);

        // Preview box inside Mode Card
        UiRect preview = {option.x + 16, option.y + 72, option.width - 32, 120};
        draw_rect_outline(pixels, stride, preview, 10, 1, selected ? UI_ACCENT : UI_BORDER);

        if (index == PTC_UI_THEME_LIGHT) {
            fill_round_rect(pixels, stride, preview, 10, UI_RGB(light_pal->page_bg));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 10, preview.width - 20, 20}, 5, UI_RGB(light_pal->surface));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16, preview.y + 15, 10, 10}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 32, preview.y + 18, 48, 4}, 2, UI_RGB(light_pal->text_primary));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 38, preview.width - 20, 34}, 6, UI_RGB(light_pal->surface));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 18, preview.y + 46, 56, 5}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 18, preview.y + 58, 88, 4}, 2, UI_RGB(light_pal->text_secondary));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 80, (preview.width - 26) / 2, 26}, 5, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16 + (preview.width - 26) / 2, preview.y + 80, (preview.width - 26) / 2, 26}, 5, UI_RGB(light_pal->surface_raised));
        } else if (index == PTC_UI_THEME_DARK) {
            fill_round_rect(pixels, stride, preview, 10, UI_RGB(dark_pal->page_bg));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 10, preview.width - 20, 20}, 5, UI_RGB(dark_pal->surface));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16, preview.y + 15, 10, 10}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 32, preview.y + 18, 48, 4}, 2, UI_RGB(dark_pal->text_primary));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 38, preview.width - 20, 34}, 6, UI_RGB(dark_pal->surface));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 18, preview.y + 46, 56, 5}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 18, preview.y + 58, 88, 4}, 2, UI_RGB(dark_pal->text_secondary));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 10, preview.y + 80, (preview.width - 26) / 2, 26}, 5, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 16 + (preview.width - 26) / 2, preview.y + 80, (preview.width - 26) / 2, 26}, 5, UI_RGB(dark_pal->surface_raised));
        } else {
            int half_w = preview.width / 2;
            fill_round_rect(pixels, stride, (UiRect){preview.x, preview.y, half_w, preview.height}, 10, UI_RGB(light_pal->page_bg));
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w, preview.y, preview.width - half_w, preview.height}, 10, UI_RGB(dark_pal->page_bg));
            fill_rect_packed(pixels, stride, (UiRect){preview.x + half_w, preview.y, 1, preview.height}, UI_RGB(light_pal->border_control));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 8, preview.y + 10, half_w - 14, 20}, 5, UI_RGB(light_pal->surface));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 15, 8, 8}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 26, preview.y + 18, 32, 4}, 2, UI_RGB(light_pal->text_primary));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 8, preview.y + 38, half_w - 14, 34}, 6, UI_RGB(light_pal->surface));
            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 46, 40, 5}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + 14, preview.y + 58, half_w - 28, 4}, 2, UI_RGB(light_pal->text_secondary));

            fill_round_rect(pixels, stride, (UiRect){preview.x + 8, preview.y + 80, half_w - 14, 26}, 5, UI_ACCENT);

            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 6, preview.y + 10, half_w - 14, 20}, 5, UI_RGB(dark_pal->surface));
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 12, preview.y + 15, 8, 8}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 24, preview.y + 18, 32, 4}, 2, UI_RGB(dark_pal->text_primary));

            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 6, preview.y + 38, half_w - 14, 34}, 6, UI_RGB(dark_pal->surface));
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 12, preview.y + 46, 40, 5}, 2, UI_ACCENT);
            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 12, preview.y + 58, half_w - 28, 4}, 2, UI_RGB(dark_pal->text_secondary));

            fill_round_rect(pixels, stride, (UiRect){preview.x + half_w + 6, preview.y + 80, half_w - 14, 26}, 5, UI_RGB(dark_pal->surface_raised));
        }
    }

    // --- Row 1: Theme Style Header ---
    const char *style_title = ptc_ui_text(PTC_UI_T_THEME_STYLE_TITLE);
    const char *style_sub = ptc_ui_text(PTC_UI_T_THEME_STYLE_SUBTITLE);
    int title_w = measure_text(style_title, 17);
    draw_text(pixels, stride, 56, 424, style_title, 17, UI_INK);
    draw_text(pixels, stride, 56 + title_w + 18, 424, style_sub, 13, UI_MUTED);

    // --- Row 1: Theme Style Cards (4 radio cards) ---
    for (int i = 0; i < PTC_UI_THEME_STYLE_COUNT; ++i) {
        UiRect s_rect = to_uirect(ptc_ui_theme_style_rect(i));
        bool focused = (model->overlay_selection == 3 + i);
        bool selected = (i == (int)draft_style);
        const PtcUiPalette *spal = ptc_ui_theme_style_palette(g_theme.resolved, (PtcUiThemeStyle)i);
        const uint32_t card_accent = focused ? UI_RGB(spal->accent) : UI_ACCENT;
        const uint32_t card_focus = focused ? UI_RGB(spal->focus) : UI_FOCUS;

        if (focused) {
            UiRect halo = {s_rect.x - 3, s_rect.y - 3, s_rect.width + 6, s_rect.height + 6};
            draw_rect_outline(pixels, stride, halo, 17, 1, card_focus);
        }
        fill_round_rect(pixels, stride, s_rect, 14, focused ? UI_RGB(spal->accent_soft) : UI_RAISED);
        draw_rect_outline(pixels, stride, s_rect, 14, focused ? 4 : 1, focused ? card_accent : UI_BORDER);

        draw_radio_marker(pixels, stride, s_rect.x + 26, s_rect.y + 24, selected, card_accent);

        // Color Swatches Row
        int sw_x = s_rect.x + 44;
        int sw_y = s_rect.y + 16;
        fill_round_rect(pixels, stride, (UiRect){sw_x, sw_y, 28, 16}, 4, UI_RGB(spal->accent));
        fill_round_rect(pixels, stride, (UiRect){sw_x + 34, sw_y, 22, 16}, 4, UI_RGB(spal->focus));
        fill_round_rect(pixels, stride, (UiRect){sw_x + 62, sw_y, 22, 16}, 4, UI_RGB(spal->accent_soft));
        draw_rect_outline(pixels, stride, (UiRect){sw_x + 62, sw_y, 22, 16}, 4, 1, UI_RGB(spal->accent));
        fill_round_rect(pixels, stride, (UiRect){sw_x + 90, sw_y, 22, 16}, 4, UI_RGB(spal->night));

        // Style Title
        draw_text(pixels, stride, s_rect.x + 16, s_rect.y + 66, ptc_ui_theme_style_label((PtcUiThemeStyle)i), 18, UI_INK);

        // Style Keyword & Description
        draw_text(pixels, stride, s_rect.x + 16, s_rect.y + 90, ptc_ui_theme_style_keyword((PtcUiThemeStyle)i), 12, UI_ACCENT);
        draw_wrapped_text(pixels, stride, s_rect.x + 16, s_rect.y + 114, ptc_ui_theme_style_desc((PtcUiThemeStyle)i),
                          12, s_rect.width - 32, 16, 3, UI_MUTED);
    }

    // --- Selection summary: saved pair vs pending draft ---
    /* 13px 基线 644 落在风格卡下沿（626）与底栏分割线（652）之间的留白带。 */
    {
        char summary[192];
        char fitted[192];
        int x = 56;
        int max_width = ptc_ui_settings_page_primary_rect().x - 24 - x;
        PtcUiTextArg args[] = {
            PTC_UI_TEXT_STRING("mode", ptc_ui_theme_preference_label(saved_preference)),
            PTC_UI_TEXT_STRING("style", ptc_ui_theme_style_label(saved_style))
        };
        (void)ptc_ui_text_format(PTC_UI_T_THEME_SAVED_SUMMARY, summary, sizeof(summary), args, 2);
        fit_text(fitted, sizeof(fitted), summary, 13, max_width);
        draw_text(pixels, stride, x, 644, fitted, 13, UI_MUTED);
        if (dirty) {
            PtcUiTextArg draft_args[] = {
                PTC_UI_TEXT_STRING("mode", ptc_ui_theme_preference_label(draft_preference)),
                PTC_UI_TEXT_STRING("style", ptc_ui_theme_style_label(draft_style))
            };
            x += measure_text(fitted, 13) + 24;
            max_width = ptc_ui_settings_page_primary_rect().x - 24 - x;
            (void)ptc_ui_text_format(PTC_UI_T_THEME_PENDING_SUMMARY, summary, sizeof(summary), draft_args, 2);
            fit_text(fitted, sizeof(fitted), summary, 13, max_width);
            draw_text(pixels, stride, x, 644, fitted, 13, UI_WARNING_TEXT);
        }
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

/* Work in the reference screenshot's 1280x720 space. Vendored libtesla uses
   448x720: the left panel occupies exactly 35% of a 16:9 screen, full height.
   The dark device display stays dark inside either NRO theme. */
static UiRect notification_scene_rect(UiRect screen, int x, int y, int w, int h)
{
    return (UiRect){screen.x + x * screen.width / 1280, screen.y + y * screen.height / 720,
        w * screen.width / 1280, h * screen.height / 720};
}

static void notification_shade(uint32_t *pixels, uint32_t stride, UiRect rect, uint32_t color, uint8_t alpha)
{
    uint32_t packed = resolve_color(color);
    for (int y = rect.y; y < rect.y + rect.height; ++y)
        for (int x = rect.x; x < rect.x + rect.width; ++x)
            blend_pixel(pixels, stride, x, y, packed, alpha);
}

static void draw_notification_example(uint32_t *pixels, uint32_t stride, UiRect screen, bool detail)
{
    const PtcUiPalette *dark = ptc_ui_theme_palette(PTC_UI_RESOLVED_DARK);
    uint32_t ink = UI_RGB(dark->text_primary), muted = UI_RGB(dark->text_secondary);
    uint32_t base = UI_RGB(dark->page_bg), raised = UI_RGB(dark->surface_raised);
    uint32_t accent = UI_RGB(dark->accent), warning = UI_RGB(dark->warning);
    fill_round_rect(pixels, stride, screen, 10, raised);
    /* Muted HOME tiles remain visible through the native full-screen mask. */
    const uint32_t tiles[] = {UI_ACCENT_SOFT, UI_SUCCESS_SOFT, UI_WARNING_SOFT, UI_DANGER_SOFT, UI_NIGHT_SOFT};
    for (int i = 0; i < 5; ++i) {
        UiRect tile = notification_scene_rect(screen, 120 + i * 224, 210, 204, 240);
        fill_round_rect(pixels, stride, tile, 6, tiles[i]);
        draw_circle_outline(pixels, stride, tile.x + tile.width / 2, tile.y + tile.height / 2,
            tile.width / 4, 4, UI_BORDER);
    }
    notification_shade(pixels, stride, screen, base, 222);
    UiRect clock = notification_scene_rect(screen, 588, 100, 104, 104);
    int cx = clock.x + clock.width / 2, cy = clock.y + clock.height / 2;
    draw_circle_outline(pixels, stride, cx, cy, clock.width / 2, 3, ink);
    draw_line(pixels, stride, cx, cy, cx, cy - clock.height / 3, 3, ink);
    draw_line(pixels, stride, cx, cy, cx + clock.width / 4, cy, 3, ink);
    draw_line(pixels, stride, clock.x + 3, clock.y + 3, clock.x + 13, clock.y - 3, 3, ink);
    draw_line(pixels, stride, clock.x + clock.width - 13, clock.y - 3,
        clock.x + clock.width - 3, clock.y + 3, 3, ink);
    UiRect native_text = notification_scene_rect(screen, 460, 280, 360, 110);
    draw_wrapped_text_centered(pixels, stride, screen.x + screen.width / 2, native_text.y + 15,
        ptc_ui_text(PTC_UI_T_NOTICE_NATIVE_LIMIT), 14, native_text.width, 20, 2, ink);
    UiRect sleep = notification_scene_rect(screen, 280, 450, 720, 82);
    UiRect settings = notification_scene_rect(screen, 280, 550, 720, 60);
    draw_rect_outline(pixels, stride, sleep, 3, 2, accent);
    draw_text_center(pixels, stride, sleep, ptc_ui_text(PTC_UI_T_NOTICE_NATIVE_SLEEP), 14, ink);
    draw_rect_outline(pixels, stride, settings, 3, 1, muted);
    draw_text_center(pixels, stride, settings, ptc_ui_text(PTC_UI_T_NOTICE_NATIVE_SETTINGS), 12, ink);
    UiRect native_footer = notification_scene_rect(screen, 30, 647, 1220, 1);
    fill_rect(pixels, stride, native_footer, muted);

    if (detail) {
        UiRect panel = notification_scene_rect(screen, 0, 0, 448, 720);
        notification_shade(pixels, stride, panel, base, 238); /* Production panel alpha: 0xE/0xF. */
        draw_text(pixels, stride, panel.x + 10, panel.y + 23, ptc_ui_text(PTC_UI_T_NOTIFY_TITLE), 13, accent);
        UiRect reason = notification_scene_rect(screen, 18, 68, 412, 230);
        fill_round_rect(pixels, stride, reason, 4, UI_RGB(dark->warning_soft));
        fill_rect(pixels, stride, (UiRect){reason.x, reason.y + 5, 2, reason.height - 10}, warning);
        draw_wrapped_text(pixels, stride, reason.x + 10, reason.y + 25,
            ptc_ui_text(PTC_UI_T_NOTIFY_EYE), 14, reason.width - 20, 20, 2, warning);
        draw_text(pixels, stride, reason.x + 10, reason.y + 86, "05:00", 34, warning);
        draw_wrapped_text(pixels, stride, panel.x + 10, panel.y + 170,
            ptc_ui_text(PTC_UI_T_NOTICE_OVERLAY_HELP), 11, panel.width - 20, 16, 2, ink);
        for (int i = 0; i < 3; ++i) {
            UiRect row = notification_scene_rect(screen, 18, 430 + i * 58, 412, 48);
            fill_round_rect(pixels, stride, row, 3, raised);
            const PtcUiTextId labels[] = {PTC_UI_T_EYE_CARE, PTC_UI_T_BEDTIME, PTC_UI_T_DOCK_TV};
            const char *value = i == 0 ? "05:00" : ptc_ui_text(PTC_UI_T_CURRENTLY_OFF);
            int value_width = measure_text(value, 10);
            char fitted[128];
            fit_text(fitted, sizeof(fitted), ptc_ui_text(labels[i]), 10, row.width - value_width - 24);
            draw_text(pixels, stride, row.x + 8, row.y + 16, fitted, 10, muted);
            draw_text(pixels, stride, row.x + row.width - value_width - 8, row.y + 16, value, 10,
                i == 0 ? warning : muted);
        }
        UiRect footer = notification_scene_rect(screen, 18, 647, 412, 1);
        fill_rect(pixels, stride, footer, muted);
        draw_text(pixels, stride, panel.x + 10, panel.y + panel.height - 12,
            ptc_ui_text(PTC_UI_T_B_BACK), 11, ink);
    } else {
        /* Match the observed receiver's upper-left notice; themes can vary. */
        UiRect notice = notification_scene_rect(screen, 24, 28, 448, 180);
        fill_round_rect(pixels, stride, notice, 5, UI_RGB(dark->surface));
        draw_rect_outline(pixels, stride, notice, 5, 1, UI_RGB(dark->border_control));
        fill_rect(pixels, stride, (UiRect){notice.x + 8, notice.y + 10, 3, notice.height - 20}, warning);
        draw_text(pixels, stride, notice.x + 20, notice.y + 21, ptc_ui_text(PTC_UI_T_NOTIFY_TITLE), 12, accent);
        draw_wrapped_text(pixels, stride, notice.x + 20, notice.y + 43,
            ptc_ui_text(PTC_UI_T_NOTIFY_EYE), 12, notice.width - 32, 17, 2, ink);
        draw_text(pixels, stride, notice.x + 20, notice.y + notice.height - 12, "05:00", 13, warning);
    }
    draw_rect_outline(pixels, stride, screen, 10, 1, UI_CONTROL);
}

static void draw_notification_step(uint32_t *pixels, uint32_t stride, int x, int y, int step, bool selected)
{
    UiRect badge = {x, y, 22, 22};
    fill_round_rect(pixels, stride, badge, 11, selected ? UI_ACCENT : UI_ACCENT_SOFT);
    draw_text_center(pixels, stride, badge, step == 1 ? "1" : "2", 14,
        selected ? UI_ON_ACCENT : UI_ACCENT);
}

static void draw_notification_verification(uint32_t *pixels, uint32_t stride, const PtcUiModel *m)
{
    UiRect group = to_uirect(ptc_ui_notification_verification_rect());
    fill_round_rect(pixels, stride, group, 12, UI_PAGE);
    draw_rect_outline(pixels, stride, group, 12, 1, UI_BORDER);
    const char *title = ptc_ui_text(PTC_UI_T_NOTICE_VERIFY_TITLE);
    draw_text(pixels, stride, group.x + 16, group.y + 21, title, 15, UI_INK);
    int hint_x = group.x + 28 + measure_text(title, 15);
    draw_text(pixels, stride, hint_x, group.y + 21, ptc_ui_text(PTC_UI_T_NOTICE_VERIFY_HINT), 13, UI_MUTED);
    for (int i = PTC_UI_NOTIFICATION_PROBE; i < PTC_UI_NOTIFICATION_OPTION_COUNT; ++i) {
        UiRect button = to_uirect(ptc_ui_notification_option_rect(i));
        bool selected = m->overlay_selection == i;
        fill_round_rect(pixels, stride, button, 8, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, button, 8, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
        draw_notification_step(pixels, stride, button.x + 12, button.y + 11, i, selected);
        draw_text(pixels, stride, button.x + 46, button.y + 20,
            ptc_ui_text(i == PTC_UI_NOTIFICATION_PROBE ? PTC_UI_T_NOTICE_PROBE : PTC_UI_T_NOTICE_TEST), 17, UI_ACCENT);
        draw_text(pixels, stride, button.x + 46, button.y + 37,
            ptc_ui_text(i == PTC_UI_NOTIFICATION_PROBE ? PTC_UI_T_NOTICE_PROBE_NOTE : PTC_UI_T_NOTICE_TEST_NOTE), 12, UI_MUTED);
    }
    /* This connector describes a recommended order, not a prerequisite gate. */
    UiRect first = to_uirect(ptc_ui_notification_option_rect(PTC_UI_NOTIFICATION_PROBE));
    UiRect second = to_uirect(ptc_ui_notification_option_rect(PTC_UI_NOTIFICATION_TEST));
    int x = (first.x + first.width + second.x) / 2, y = first.y + first.height / 2;
    draw_line(pixels, stride, x - 8, y, x + 7, y, 2, UI_MUTED);
    draw_line(pixels, stride, x + 2, y - 5, x + 7, y, 2, UI_MUTED);
    draw_line(pixels, stride, x + 2, y + 5, x + 7, y, 2, UI_MUTED);
}

static void draw_notification_preferences(uint32_t *pixels, uint32_t stride, const PtcUiModel *m)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, m, &dialog, 1180, 680);
    char navigation[160];
    int navigation_width = dialog.width - 92 - measure_text(m->overlay_title, 29);
    fit_text(navigation, sizeof(navigation), ptc_ui_text(PTC_UI_T_NOTICE_NAV_HINT), 14, navigation_width);
    draw_text(pixels, stride, dialog.x + dialog.width - 34 - measure_text(navigation, 14),
        dialog.y + 54, navigation, 14, UI_MUTED);
    UiRect row = to_uirect(ptc_ui_notification_option_rect(PTC_UI_NOTIFICATION_TOGGLE));
    bool enabled = m->reason_notifications_enabled;
    bool selected = m->overlay_selection == PTC_UI_NOTIFICATION_TOGGLE;
    fill_round_rect(pixels, stride, row, 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
    if (selected) draw_focus_ring(pixels, stride, row, 10);
    const char *status = ptc_ui_text(enabled ? PTC_UI_T_ENABLED : PTC_UI_T_DISABLED_2);
    UiRect toggle = {row.x + row.width - 66, row.y + 17, 46, 24};
    int status_x = toggle.x - 12 - measure_text(status, 18);
    char title[192];
    fit_text(title, sizeof(title), ptc_ui_text(PTC_UI_T_REASON_NOTIFICATIONS), 20, status_x - row.x - 40);
    draw_text(pixels, stride, row.x + 20, row.y + 36, title, 20, UI_INK);
    draw_text(pixels, stride, status_x, row.y + 35, status, 18, enabled ? UI_SUCCESS : UI_MUTED);
    draw_toggle_switch(pixels, stride, toggle, enabled, selected, false, NULL, NULL);

    bool detail = m->notification_preview == 0;
    UiRect preview_group = to_uirect(ptc_ui_notification_preview_group_rect());
    draw_plan_card(pixels, stride, preview_group, false);
    draw_text(pixels, stride, preview_group.x + 16, preview_group.y + 21,
        ptc_ui_text(PTC_UI_T_NOTICE_SELECTOR_TITLE), 16, UI_INK);
    draw_text(pixels, stride, preview_group.x + 16, preview_group.y + 38,
        ptc_ui_text(PTC_UI_T_NOTICE_PREVIEW_LABEL), 11, UI_MUTED);
    UiRect selector = to_uirect(ptc_ui_notification_preview_tab_rect(0));
    selector.width *= 2;
    fill_round_rect(pixels, stride, selector, 8, UI_PAGE);
    draw_rect_outline(pixels, stride, selector, 8, 1, UI_BORDER);
    for (int i = 0; i < 2; ++i) {
        UiRect tab = to_uirect(ptc_ui_notification_preview_tab_rect(i));
        bool current = detail ? i == 0 : i == 1;
        if (current) fill_round_rect(pixels, stride,
            (UiRect){tab.x + 2, tab.y + 2, tab.width - 4, tab.height - 4}, 6, UI_ACCENT_SOFT);
        draw_text_center(pixels, stride, tab,
            ptc_ui_text(i ? PTC_UI_T_NOTICE_PREVIEW_SHORT : PTC_UI_T_NOTICE_PREVIEW_DETAILS),
            16, current ? UI_ACCENT : UI_MUTED);
    }
    draw_notification_example(pixels, stride, to_uirect(ptc_ui_notification_preview_rect()), detail);
    /* The guide follows the example; live checks and prior action results do
       not. Step focus highlights the corresponding read-only destination. */
    UiRect guidance = to_uirect(ptc_ui_notification_info_rect(0));
    UiRect environment = to_uirect(ptc_ui_notification_info_rect(1));
    UiRect feedback = to_uirect(ptc_ui_notification_info_rect(2));
    const int text_x = guidance.x + 16;
    const int text_width = guidance.width - 32;
    bool probing = m->overlay_selection == PTC_UI_NOTIFICATION_PROBE;
    bool testing = m->overlay_selection == PTC_UI_NOTIFICATION_TEST;
    draw_plan_card(pixels, stride, environment, false);
    if (probing) draw_rect_outline(pixels, stride, environment, 12, 2, UI_ACCENT);
    draw_notification_step(pixels, stride, text_x, environment.y + 9, 1, probing);
    draw_text(pixels, stride, text_x + 32, environment.y + 25,
        ptc_ui_text(PTC_UI_T_NOTICE_ENVIRONMENT_TITLE), 17, UI_INK);
    draw_wrapped_text(pixels, stride, text_x, environment.y + 48, ptc_ui_text(
        m->reason_external_notifications == 1 ? PTC_UI_T_REASON_NOTICE_EXTERNAL_ON :
        m->reason_external_notifications == 0 ? PTC_UI_T_REASON_NOTICE_EXTERNAL_OFF : PTC_UI_T_REASON_NOTICE_EXTERNAL_UNKNOWN),
        16, text_width, 20, 2, UI_WARNING);
    draw_wrapped_text(pixels, stride, text_x, environment.y + 92, ptc_ui_text(
        !m->reason_overlay_installed ? PTC_UI_T_NOTICE_OVERLAY_MISSING :
        m->reason_detail_ready ? PTC_UI_T_NOTICE_DETAIL_READY : PTC_UI_T_NOTICE_DETAIL_UNREADY),
        16, text_width, 20, 2, m->reason_detail_ready ? UI_ACCENT : UI_WARNING);

    draw_plan_card(pixels, stride, guidance, false);
    fill_rect(pixels, stride, (UiRect){guidance.x, guidance.y + 12, 3, guidance.height - 24}, UI_ACCENT);
    draw_text(pixels, stride, text_x, guidance.y + 25, ptc_ui_text(PTC_UI_T_NOTICE_USAGE_TITLE), 17, UI_INK);
    const char *mode = ptc_ui_text(detail ? PTC_UI_T_NOTICE_DETAIL_NAME : PTC_UI_T_NOTICE_BRIEF_NAME);
    int mode_width = measure_text(mode, 14) + 20;
    UiRect mode_badge = {guidance.x + guidance.width - mode_width - 16, guidance.y + 9, mode_width, 24};
    fill_round_rect(pixels, stride, mode_badge, 6, UI_ACCENT_SOFT);
    draw_text_center(pixels, stride, mode_badge, mode, 14, UI_ACCENT);
    draw_wrapped_text(pixels, stride, text_x, guidance.y + 49,
        ptc_ui_text(detail ? PTC_UI_T_NOTICE_DETAIL_GUIDE : PTC_UI_T_NOTICE_BRIEF_GUIDE),
        16, text_width, 20, 2, UI_INK);
    draw_wrapped_text(pixels, stride, text_x, guidance.y + 93,
        ptc_ui_text(detail ? PTC_UI_T_NOTICE_DETAIL_BOUNDARY : PTC_UI_T_NOTICE_BRIEF_BOUNDARY),
        15, text_width, 20, 2, UI_MUTED);
    char shortcut[192];
    snprintf(shortcut, sizeof(shortcut), "%s%s", m->reason_overlay_shortcut,
        ptc_ui_text(m->reason_overlay_shortcut[0] ? PTC_UI_T_NOTIFY_SHORTCUT : PTC_UI_T_NOTIFY_OPEN));
    draw_wrapped_text(pixels, stride, text_x, guidance.y + 137, shortcut, 16, text_width, 20, 1, UI_ACCENT);

    /* A small connector binds the selected example to its explanation. */
    int link_x = guidance.x - 14, link_y = guidance.y + 24;
    draw_line(pixels, stride, link_x - 6, link_y, link_x + 5, link_y, 2, UI_ACCENT);
    draw_line(pixels, stride, link_x, link_y - 5, link_x + 5, link_y, 2, UI_ACCENT);
    draw_line(pixels, stride, link_x, link_y + 5, link_x + 5, link_y, 2, UI_ACCENT);

    draw_plan_card(pixels, stride, feedback, false);
    if (probing || testing) draw_rect_outline(pixels, stride, feedback, 12, 2, UI_ACCENT);
    draw_text(pixels, stride, text_x, feedback.y + 23, ptc_ui_text(PTC_UI_T_NOTICE_RESULT_TITLE), 16, UI_INK);
    draw_wrapped_text(pixels, stride, text_x, feedback.y + 44,
        m->reason_test_message[0] ? m->reason_test_message : ptc_ui_text(PTC_UI_T_NOTICE_RESULT_IDLE),
        15, text_width, 20, 2, m->reason_test_message[0] ? UI_ACCENT : UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(m->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
        UI_RAISED, UI_INK, false);
    draw_notification_verification(pixels, stride, m);
}

bool draw_support_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
    case PTC_UI_OVERLAY_NOTIFICATIONS:
        draw_notification_preferences(pixels, stride, model);
        return true;
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
    case PTC_UI_OVERLAY_ONLINE_UPDATE:
    case PTC_UI_OVERLAY_UPDATE_BACKUPS:
        draw_online_update(pixels, stride, model);
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
