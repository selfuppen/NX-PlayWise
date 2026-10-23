#include "ui_render_internal.h"

void draw_subpage_shell(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                        const char *page_name, const char *subtitle,
                        const char *back_label, const char *path)
{
    fill_rect_packed(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, pack_rgb(UI_BLENDED(page_bg)));
    ui_background_draw_frame(pixels, stride);
    draw_header(pixels, stride, page_name, subtitle);
    draw_time_status_bar(pixels, stride, model);

    draw_subpage_nav(pixels, stride, back_label, path);

    draw_footer_button(pixels, stride, ptc_ui_settings_page_back_rect(), back_label);
    if (model->message[0]) {
        char fitted[256];
        fit_text(fitted, sizeof(fitted), model->message, 14, 380);
        draw_text(pixels, stride, 640, 692, fitted, 14, UI_WARNING_TEXT);
    }
}

void draw_settings_page_shell(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                              const char *page_name, const char *subtitle)
{
    char path[128];
    PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("name", page_name)};
    (void)ptc_ui_text_format(PTC_UI_T_SECURITY_PREFS_S, path, sizeof(path), args, 1);
    draw_subpage_shell(pixels, stride, model, page_name, subtitle,
                       ptc_ui_text(PTC_UI_T_B_BACK_TO_SETTINGS), path);
}

static void draw_config_backup(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const PtcUiTextId GROUPS[] = {
        PTC_UI_T_CONFIG_BACKUP_GROUP_WEEK, PTC_UI_T_CONFIG_BACKUP_GROUP_SCHEDULED,
        PTC_UI_T_CONFIG_BACKUP_GROUP_TODAY, PTC_UI_T_CONFIG_BACKUP_GROUP_BUFFER,
        PTC_UI_T_CONFIG_BACKUP_GROUP_HOLIDAY, PTC_UI_T_CONFIG_BACKUP_GROUP_BEDTIME,
        PTC_UI_T_CONFIG_BACKUP_GROUP_EYE, PTC_UI_T_CONFIG_BACKUP_GROUP_DOCK,
        PTC_UI_T_CONFIG_BACKUP_GROUP_PREFS, PTC_UI_T_CONFIG_BACKUP_GROUP_PIN, PTC_UI_T_CONFIG_BACKUP_GROUP_PAIRING
    };

    /* 1. 全屏重绘底色与全局三段式主机通栏外框 (Classic 3-Section Console Shell) */
    fill_rect_packed(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, pack_rgb(UI_BLENDED(page_bg)));
    ui_background_draw_frame(pixels, stride);
    draw_header(pixels, stride, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_TITLE), ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CARD_NOTE));
    draw_time_status_bar(pixels, stride, model);

    /* 2. 经典胶囊导航标签栏 (Tabs: 16 创建备份 / 17 从备份恢复, Y=120, H=38) */
    for (int t = 0; t < 2; ++t) {
        int idx = 16 + t;
        UiRect rect = to_uirect(ptc_ui_config_backup_field_rect(idx));
        bool active = (model->config_backup_tab == t);
        bool focused = (model->overlay_selection == idx);
        fill_round_rect(pixels, stride, rect, 19, active ? UI_ACCENT : UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, rect, 19, 1, active ? UI_ACCENT : UI_BORDER);
        if (focused) draw_focus_ring(pixels, stride, rect, 19);
        draw_text_center(pixels, stride, rect,
            ptc_ui_text(t == 0 ? PTC_UI_T_CONFIG_BACKUP_TAB_CREATE : PTC_UI_T_CONFIG_BACKUP_TAB_RESTORE),
            16, active ? UI_ON_ACCENT : UI_INK);
    }

    if (model->config_backup_tab == 0) {
        /* ======================== Tab 0: 创建备份 ======================== */
        /* 左侧快照说明英雄卡片 (Hero Card) */
        UiRect hero = {54, 172, 744, 360};
        draw_plan_card(pixels, stride, hero, false);
        draw_text(pixels, stride, hero.x + 24, hero.y + 36, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE_HERO_TITLE), 21, UI_INK);

        UiRect tag_rect = {hero.x + hero.width - 124, hero.y + 18, 100, 24};
        fill_round_rect(pixels, stride, tag_rect, 6, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, tag_rect, 6, 1, UI_ACCENT);
        draw_text_center(pixels, stride, tag_rect, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE), 12, UI_ACCENT);

        draw_wrapped_text(pixels, stride, hero.x + 24, hero.y + 70, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE_HERO_DESC), 15, hero.width - 48, 22, 3, UI_MUTED);

        /* 存储路径卡片 */
        UiRect path_box = {hero.x + 24, hero.y + 154, hero.width - 48, 48};
        fill_round_rect(pixels, stride, path_box, 8, UI_PAGE);
        draw_rect_outline(pixels, stride, path_box, 8, 1, UI_BORDER);
        draw_text(pixels, stride, path_box.x + 16, path_box.y + 30, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_TARGET_PATH), 14, UI_INK);

        /* 范围说明与安全特性卡片 */
        UiRect inc_card = {hero.x + 24, hero.y + 218, (hero.width - 60) / 2, 118};
        UiRect sec_card = {inc_card.x + inc_card.width + 12, hero.y + 218, (hero.width - 60) / 2, 118};
        fill_round_rect(pixels, stride, inc_card, 8, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, inc_card, 8, 1, UI_BORDER);
        draw_text(pixels, stride, inc_card.x + 14, inc_card.y + 26, ptc_ui_text(PTC_UI_T_QUOTA_RULES), 13, UI_ACCENT);
        draw_wrapped_text(pixels, stride, inc_card.x + 14, inc_card.y + 48, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE_INCLUDES), 12, inc_card.width - 28, 17, 3, UI_MUTED);

        fill_round_rect(pixels, stride, sec_card, 8, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, sec_card, 8, 1, UI_BORDER);
        draw_text(pixels, stride, sec_card.x + 14, sec_card.y + 26, ptc_ui_text(PTC_UI_T_SAFETY_MANAGEMENT), 13, UI_SUCCESS);
        draw_wrapped_text(pixels, stride, sec_card.x + 14, sec_card.y + 48, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE_TAMPER_DESC), 12, sec_card.width - 28, 17, 3, UI_MUTED);

        /* 创建备份操作按钮 (Field 13, Y=552, H=58, W=744) */
        UiRect btn_create = to_uirect(ptc_ui_config_backup_field_rect(13));
        bool create_focused = (model->overlay_selection == 13);
        bool create_disabled = model->waiting || model->recovery_active || model->hot_reload_status == PTC_UI_HOT_RELOAD_RUNNING;
        fill_round_rect(pixels, stride, btn_create, 12, create_disabled ? UI_RAISED : (create_focused ? UI_ACCENT : UI_ACCENT));
        if (create_focused) draw_focus_ring(pixels, stride, btn_create, 12);
        else draw_rect_outline(pixels, stride, btn_create, 12, 1, create_disabled ? UI_BORDER : UI_ACCENT);
        char create_label[160];
        snprintf(create_label, sizeof(create_label), "A  %s", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_BTN_CREATE));
        draw_button_label(pixels, stride, btn_create, create_label, 19, create_disabled ? UI_DISABLED : UI_ON_ACCENT);

        /* 右侧快照状态卡片 (Right Side Info Card, X=824, Y=172, W=402, H=438) */
        UiRect info = {824, 172, 402, 438};
        draw_plan_card(pixels, stride, info, false);
        draw_text(pixels, stride, info.x + 20, info.y + 34, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_SD_STATUS_TITLE), 19, UI_INK);
        if (model->config_backup_ready) {
            UiRect status_pill = {info.x + 20, info.y + 54, 180, 24};
            fill_round_rect(pixels, stride, status_pill, 6, UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, status_pill, 6, 1, UI_SUCCESS);
            draw_text_center(pixels, stride, status_pill, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_SAVED), 12, UI_SUCCESS);

            UiRect meta_box = {info.x + 20, info.y + 92, info.width - 40, 72};
            fill_round_rect(pixels, stride, meta_box, 8, UI_RGB(UI_BLENDED(surface_raised)));
            draw_rect_outline(pixels, stride, meta_box, 8, 1, UI_BORDER);
            draw_text(pixels, stride, meta_box.x + 12, meta_box.y + 24, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_EXISTING_NOTICE), 12, UI_MUTED);
            draw_wrapped_text(pixels, stride, meta_box.x + 12, meta_box.y + 48, model->config_metadata, 14, meta_box.width - 24, 18, 2, UI_ACCENT);

            UiRect warn_box = {info.x + 20, info.y + 180, info.width - 40, 84};
            fill_round_rect(pixels, stride, warn_box, 8, UI_WARNING_SOFT);
            draw_rect_outline(pixels, stride, warn_box, 8, 1, UI_WARNING_BORDER);
            draw_text(pixels, stride, warn_box.x + 12, warn_box.y + 24, ptc_ui_text(PTC_UI_T_SAFETY_MANAGEMENT), 13, UI_WARNING_TEXT);
            draw_wrapped_text(pixels, stride, warn_box.x + 12, warn_box.y + 46, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_OVERWRITE_NOTICE), 12, warn_box.width - 24, 17, 3, UI_INK);
        } else {
            UiRect status_pill = {info.x + 20, info.y + 54, 160, 24};
            fill_round_rect(pixels, stride, status_pill, 6, UI_RAISED);
            draw_rect_outline(pixels, stride, status_pill, 6, 1, UI_BORDER);
            draw_text_center(pixels, stride, status_pill, ptc_ui_text(PTC_UI_T_STATUS_TO_CONFIRM), 12, UI_MUTED);

            draw_wrapped_text(pixels, stride, info.x + 20, info.y + 106, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NO_FILE_NOTICE), 15, info.width - 40, 22, 2, UI_INK);
            draw_wrapped_text(pixels, stride, info.x + 20, info.y + 160, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NO_FILE_DESC), 13, info.width - 40, 20, 4, UI_MUTED);
        }
        UiRect hint_box = {info.x + 20, info.y + info.height - 76, info.width - 40, 56};
        fill_round_rect(pixels, stride, hint_box, 8, UI_PAGE);
        draw_rect_outline(pixels, stride, hint_box, 8, 1, UI_BORDER);
        draw_text(pixels, stride, hint_box.x + 12, hint_box.y + 24, "backups/config-backup.json", 12, UI_MUTED);
        draw_text(pixels, stride, hint_box.x + 12, hint_box.y + 44, ptc_ui_text(PTC_UI_T_Y_REFRESH), 12, UI_ACCENT);
    } else {
        /* ======================== Tab 1: 从备份恢复 ======================== */
        /* 备份标识放在胶囊标签右侧的同一行：Y=120..158 已被标签占用，原先
           画在 Y=154 会盖住按钮，因此改为与标签垂直居中对齐并右移让位。 */
        if (model->config_metadata[0]) {
            char meta[160];
            fit_text(meta, sizeof(meta), model->config_metadata, 14, 740);
            draw_text(pixels, stride, 470, 146, meta, 14, UI_MUTED);
        } else {
            draw_text(pixels, stride, 470, 146, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_SELECTION), 14, UI_MUTED);
        }

        const PtcUiTextId titles[] = {PTC_UI_T_QUOTA_RULES, PTC_UI_T_CONFIG_SECTION_POLICIES, PTC_UI_T_CONFIG_SECTION_SECURITY};
        for (int section = 0; section < 3; ++section) {
            UiRect card = to_uirect(ptc_ui_config_backup_group_rect(section));
            draw_plan_card(pixels, stride, card, false);
            draw_text(pixels, stride, 70, card.y + 26, ptc_ui_text(titles[section]), 16, UI_ACCENT);
        }

        /* 渲染复选框控件 (0..10) 与快捷按钮 (11, 12) */
        for (int i = 0; i < 16; ++i) {
            if (i == 13 || i == 14 || i == 15) continue;
            UiRect rect = to_uirect(ptc_ui_config_backup_field_rect(i));
            bool disabled = model->waiting || (i < 11 && (!model->config_backup_ready ||
                (i == 2 && !model->config_today_available) || (i == 9 && !model->config_pin_available)));
            bool focused = (model->overlay_selection == i);
            const char *label = i < 11 ? ptc_ui_text(GROUPS[i]) :
                ptc_ui_text(i == 11 ? PTC_UI_T_CONFIG_BACKUP_ALL : PTC_UI_T_CONFIG_BACKUP_NONE);

            if (i < 11) {
                bool checked = (model->config_groups & (1u << i)) != 0;
                fill_round_rect(pixels, stride, rect, 8, focused ? UI_ACCENT_SOFT : UI_RGB(UI_BLENDED(surface_raised)));
                draw_rect_outline(pixels, stride, rect, 8, 1, focused ? UI_ACCENT : UI_BORDER);
                if (focused) draw_focus_ring(pixels, stride, rect, 8);

                UiRect box = {rect.x + 12, rect.y + 9, 20, 20};
                fill_round_rect(pixels, stride, box, 4, disabled ? UI_RAISED : (checked ? UI_ACCENT : UI_SURFACE));
                draw_rect_outline(pixels, stride, box, 4, 1, disabled ? UI_BORDER : (checked ? UI_ACCENT : UI_BORDER));
                if (checked) {
                    draw_line(pixels, stride, box.x + 4, box.y + 10, box.x + 8, box.y + 14, 2, UI_ON_ACCENT);
                    draw_line(pixels, stride, box.x + 8, box.y + 14, box.x + 16, box.y + 5, 2, UI_ON_ACCENT);
                }
                char fitted[128];
                fit_text(fitted, sizeof(fitted), label, 15, rect.width - 56);
                draw_text(pixels, stride, rect.x + 44, rect.y + 26, fitted, 15, disabled ? UI_DISABLED : (focused ? UI_ACCENT : UI_INK));
            } else {
                /* 全选 (11) / 清空 (12) */
                fill_round_rect(pixels, stride, rect, 8, focused ? UI_ACCENT_SOFT : UI_SURFACE);
                draw_rect_outline(pixels, stride, rect, 8, 1, focused ? UI_ACCENT : UI_BORDER);
                if (focused) draw_focus_ring(pixels, stride, rect, 8);
                draw_text_center(pixels, stride, rect, label, 15, disabled ? UI_DISABLED : (focused ? UI_ACCENT : UI_INK));
            }
        }

        /* 右侧恢复影响与导入执行卡片 */
        UiRect info = {824, 172, 402, 368};
        draw_plan_card(pixels, stride, info, false);
        draw_text(pixels, stride, info.x + 20, info.y + 34, ptc_ui_text(PTC_UI_T_RESTORE_IMPACT), 19, UI_INK);

        unsigned int count = 0;
        for (int i = 0; i < 11; ++i) if (model->config_groups & (1u << i)) ++count;
        char selected[128];
        PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("count", count)};
        (void)ptc_ui_text_format(PTC_UI_T_RESTORE_SELECTED_NAMED, selected, sizeof(selected), args, 1);
        UiRect count_badge = {info.x + 20, info.y + 50, 140, 24};
        fill_round_rect(pixels, stride, count_badge, 6, count > 0 ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, count_badge, 6, 1, count > 0 ? UI_ACCENT : UI_BORDER);
        draw_text_center(pixels, stride, count_badge, selected, 12, count > 0 ? UI_ACCENT : UI_MUTED);

        UiRect warn_box = {info.x + 20, info.y + 86, info.width - 40, 68};
        fill_round_rect(pixels, stride, warn_box, 8, UI_WARNING_SOFT);
        draw_rect_outline(pixels, stride, warn_box, 8, 1, UI_WARNING_BORDER);
        draw_text(pixels, stride, warn_box.x + 10, warn_box.y + 22, ptc_ui_text(PTC_UI_T_SAFETY_MANAGEMENT), 12, UI_WARNING_TEXT);
        draw_wrapped_text(pixels, stride, warn_box.x + 10, warn_box.y + 40, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NOTE), 11, warn_box.width - 20, 15, 2, UI_INK);

        UiRect prev_box = {info.x + 20, info.y + 164, info.width - 40, 186};
        fill_round_rect(pixels, stride, prev_box, 8, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, prev_box, 8, 1, UI_BORDER);
        draw_wrapped_text(pixels, stride, prev_box.x + 14, prev_box.y + 24, model->config_preview, 13, prev_box.width - 28, 19, 7, UI_INK);

        /* 导入操作按钮 (Field 14, Y=552, H=58, W=402) */
        UiRect btn_import = to_uirect(ptc_ui_config_backup_field_rect(14));
        bool import_focused = (model->overlay_selection == 14);
        bool import_disabled = model->waiting || !model->config_backup_ready || !model->config_groups;
        fill_round_rect(pixels, stride, btn_import, 12, import_disabled ? UI_RAISED : (import_focused ? UI_ACCENT : UI_ACCENT));
        if (import_focused) draw_focus_ring(pixels, stride, btn_import, 12);
        else draw_rect_outline(pixels, stride, btn_import, 12, 1, import_disabled ? UI_BORDER : UI_ACCENT);
        char import_label[160];
        snprintf(import_label, sizeof(import_label), "A  %s", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_IMPORT));
        draw_button_label(pixels, stride, btn_import, import_label, 19, import_disabled ? UI_DISABLED : UI_ON_ACCENT);
    }

    /* 3. 规范化底部通栏 (Footer Bar, Y=652~720) */
    draw_footer_button(pixels, stride, ptc_ui_config_backup_field_rect(15), ptc_ui_text(PTC_UI_T_B_BACK));
    if (model->overlay_selection == 15) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_config_backup_field_rect(15)), 12, 3, UI_ACCENT);
    }
    draw_footer_button(pixels, stride, (PtcUiRect){236, 664, 130, 44}, ptc_ui_text(PTC_UI_T_Y_REFRESH));
    draw_footer_button(pixels, stride, (PtcUiRect){378, 664, 130, 44}, ptc_ui_text(PTC_UI_T_L_PREVIOUS_PAGE));
    draw_footer_button(pixels, stride, (PtcUiRect){516, 664, 130, 44}, ptc_ui_text(PTC_UI_T_R_NEXT_PAGE));

    if (model->message[0]) {
        char fitted[256];
        fit_text(fitted, sizeof(fitted), model->message, 14, 580);
        draw_text(pixels, stride, 660, 692, fitted, 14, UI_WARNING_TEXT);
    }
}

void draw_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    if (model->overlay == PTC_UI_OVERLAY_CONFIG_BACKUP) { draw_config_backup(pixels, stride, model); return; }
    if (model->confirm_return_overlay == PTC_UI_OVERLAY_CONFIG_BACKUP) {
        PtcUiModel base = *model;
        base.overlay = PTC_UI_OVERLAY_CONFIG_BACKUP;
        draw_config_backup(pixels, stride, &base);
    }
    if (draw_account_overlay_surface(pixels, stride, model) ||
        draw_plan_overlay_surface(pixels, stride, model) ||
        draw_details_overlay_surface(pixels, stride, model) ||
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
