#include "ui_render_internal.h"

static void draw_config_backup(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const PtcUiTextId GROUPS[] = {
        PTC_UI_T_CONFIG_BACKUP_GROUP_WEEK, PTC_UI_T_CONFIG_BACKUP_GROUP_SCHEDULED,
        PTC_UI_T_CONFIG_BACKUP_GROUP_TODAY, PTC_UI_T_CONFIG_BACKUP_GROUP_BUFFER,
        PTC_UI_T_CONFIG_BACKUP_GROUP_HOLIDAY, PTC_UI_T_CONFIG_BACKUP_GROUP_BEDTIME,
        PTC_UI_T_CONFIG_BACKUP_GROUP_EYE, PTC_UI_T_CONFIG_BACKUP_GROUP_DOCK,
        PTC_UI_T_CONFIG_BACKUP_GROUP_PREFS, PTC_UI_T_CONFIG_BACKUP_GROUP_PIN, PTC_UI_T_CONFIG_BACKUP_GROUP_PAIRING
    };
    fill_rect(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, UI_PAGE);
    draw_text(pixels, stride, 54, 58, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_TITLE), 28, UI_INK);

    /* Tabs: 16 (Create Backup) and 17 (Restore Backup) */
    for (int t = 0; t < 2; ++t) {
        int idx = 16 + t;
        UiRect rect = to_uirect(ptc_ui_config_backup_field_rect(idx));
        bool active = (model->config_backup_tab == t);
        bool focused = (model->overlay_selection == idx);
        fill_round_rect(pixels, stride, rect, 6, active ? UI_ACCENT : UI_SURFACE);
        if (focused) draw_rect_outline(pixels, stride, rect, 6, 2, UI_FOCUS);
        draw_text_center(pixels, stride, rect,
            ptc_ui_text(t == 0 ? PTC_UI_T_CONFIG_BACKUP_TAB_CREATE : PTC_UI_T_CONFIG_BACKUP_TAB_RESTORE),
            16, active ? UI_ON_ACCENT : UI_INK);
    }

    if (model->config_backup_tab == 0) {
        /* Tab 0: Create Backup */
        UiRect hero = {54, 150, 744, 380};
        draw_plan_card(pixels, stride, hero, false);
        draw_text(pixels, stride, 78, 188, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE_HERO_TITLE), 20, UI_ACCENT);
        draw_wrapped_text(pixels, stride, 78, 226, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE_HERO_DESC), 16, 696, 28, 4, UI_INK);

        UiRect path_box = {78, 346, 696, 42};
        fill_round_rect(pixels, stride, path_box, 4, UI_PAGE);
        draw_text(pixels, stride, 94, 372, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_TARGET_PATH), 15, UI_MUTED);

        draw_text(pixels, stride, 78, 424, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE_INCLUDES), 14, UI_MUTED);
        draw_text(pixels, stride, 78, 452, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE_TAMPER_DESC), 14, UI_MUTED);

        /* Create Button (13) */
        UiRect btn_create = to_uirect(ptc_ui_config_backup_field_rect(13));
        draw_plan_card(pixels, stride, btn_create, model->overlay_selection == 13);
        draw_text_center(pixels, stride, btn_create, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_BTN_CREATE), 18, UI_ACCENT);

        /* Back Button (15) */
        UiRect btn_back = to_uirect(ptc_ui_config_backup_field_rect(15));
        draw_plan_card(pixels, stride, btn_back, model->overlay_selection == 15);
        draw_button_label(pixels, stride, btn_back, ptc_ui_text(PTC_UI_T_B_BACK), 16, UI_MUTED);

        /* Right Side info card */
        UiRect info = {824, 144, 402, 498};
        draw_plan_card(pixels, stride, info, false);
        draw_text(pixels, stride, 842, 178, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_SD_STATUS_TITLE), 18, UI_INK);
        if (model->config_backup_ready) {
            draw_wrapped_text(pixels, stride, 842, 218, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_EXISTING_NOTICE), 15, 366, 24, 2, UI_INK);
            draw_wrapped_text(pixels, stride, 842, 256, model->config_metadata, 15, 366, 24, 3, UI_ACCENT);
            draw_wrapped_text(pixels, stride, 842, 330, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_OVERWRITE_NOTICE), 14, 366, 22, 3, UI_WARNING);
        } else {
            draw_wrapped_text(pixels, stride, 842, 218, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NO_FILE_NOTICE), 15, 366, 24, 3, UI_MUTED);
            draw_wrapped_text(pixels, stride, 842, 280, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NO_FILE_DESC), 14, 366, 22, 3, UI_INK);
        }
        draw_text(pixels, stride, 842, 570, "backups/config-backup.json", 15, UI_MUTED);
        draw_text(pixels, stride, 842, 610, ptc_ui_text(PTC_UI_T_Y_REFRESH), 16, UI_MUTED);
    } else {
        /* Tab 1: Restore Backup */
        draw_text(pixels, stride, 54, 150, model->config_metadata[0] ? model->config_metadata : ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_SELECTION), 15, UI_ACCENT);

        const PtcUiTextId titles[] = {PTC_UI_T_QUOTA_RULES, PTC_UI_T_CONFIG_SECTION_POLICIES, PTC_UI_T_CONFIG_SECTION_SECURITY};
        for (int section = 0; section < 3; ++section) {
            UiRect card = to_uirect(ptc_ui_config_backup_group_rect(section));
            draw_plan_card(pixels, stride, card, false);
            draw_text(pixels, stride, 70, card.y + 26, ptc_ui_text(titles[section]), 16, UI_ACCENT);
        }

        for (int i = 0; i < 16; ++i) {
            if (i == 13) continue;
            UiRect rect = to_uirect(ptc_ui_config_backup_field_rect(i));
            bool disabled = model->waiting || (i < 11 && (!model->config_backup_ready ||
                (i == 2 && !model->config_today_available) || (i == 9 && !model->config_pin_available))) ||
                (i == 14 && (!model->config_backup_ready || !model->config_groups));
            const char *label = i < 11 ? ptc_ui_text(GROUPS[i]) :
                ptc_ui_text(i == 11 ? PTC_UI_T_CONFIG_BACKUP_ALL : i == 12 ? PTC_UI_T_CONFIG_BACKUP_NONE :
                    i == 14 ? PTC_UI_T_CONFIG_BACKUP_IMPORT : PTC_UI_T_B_BACK);
            draw_plan_card(pixels, stride, rect, model->overlay_selection == i);
            if (i < 11) {
                UiRect box = {rect.x + 12, rect.y + 9, 20, 20};
                fill_round_rect(pixels, stride, box, 4, (model->config_groups & (1u << i)) ? UI_ACCENT : UI_RAISED);
                draw_rect_outline(pixels, stride, box, 4, 1, disabled ? UI_DISABLED : UI_ACCENT);
                if (model->config_groups & (1u << i)) {
                    draw_line(pixels, stride, box.x + 4, box.y + 10, box.x + 8, box.y + 14, 2, UI_ON_ACCENT);
                    draw_line(pixels, stride, box.x + 8, box.y + 14, box.x + 16, box.y + 5, 2, UI_ON_ACCENT);
                }
                char fitted[128]; fit_text(fitted, sizeof(fitted), label, 16, rect.width - 56);
                draw_text(pixels, stride, rect.x + 44, rect.y + 27, fitted, 16, disabled ? UI_DISABLED : UI_INK);
            } else if (i == 15) {
                draw_button_label(pixels, stride, rect, label, 16, UI_MUTED);
            } else {
                draw_text_center(pixels, stride, rect, label, 17, disabled ? UI_DISABLED : UI_ACCENT);
            }
        }
        UiRect info = {824, 144, 402, 390};
        draw_plan_card(pixels, stride, info, false);
        draw_text(pixels, stride, 842, 178, ptc_ui_text(PTC_UI_T_RESTORE_IMPACT), 19, UI_INK);
        unsigned int count = 0;
        for (int i = 0; i < 11; ++i) if (model->config_groups & (1u << i)) ++count;
        char selected[128];
        PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("count", count)};
        (void)ptc_ui_text_format(PTC_UI_T_RESTORE_SELECTED_NAMED, selected, sizeof(selected), args, 1);
        draw_text(pixels, stride, 842, 210, selected, 16, UI_ACCENT);
        draw_wrapped_text(pixels, stride, 842, 246, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NOTE), 14, 366, 21, 5, UI_WARNING);
        draw_wrapped_text(pixels, stride, 842, 366, model->config_preview, 14, 366, 21, 7, UI_INK);
        draw_text(pixels, stride, 842, 636, ptc_ui_text(PTC_UI_T_Y_REFRESH), 16, UI_MUTED);
    }
    char fitted[256]; fit_text(fitted, sizeof(fitted), model->message, 14, 910);
    draw_text(pixels, stride, 300, 692, fitted, 14, UI_MUTED);
}

void draw_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    if (model->overlay == PTC_UI_OVERLAY_CONFIG_BACKUP) { draw_config_backup(pixels, stride, model); return; }
    if (draw_account_overlay_surface(pixels, stride, model) ||
        draw_plan_overlay_surface(pixels, stride, model) ||
        draw_support_overlay_surface(pixels, stride, model)) return;

    switch (model->overlay) {
    case PTC_UI_OVERLAY_MINUTES:
        draw_minutes_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_WEEKLY:
        draw_weekly_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_CONFIRM:
        draw_confirm_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_NUMPAD:
        draw_numpad_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_PIN:
        draw_pin_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_MINUTE_EDITOR:
        draw_minute_editor_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_NOTICE_DETAILS:
        draw_notice_details_dialog(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_NONE:
    default:
        break;
    }
}
