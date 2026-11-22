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
        fit_text(fitted, sizeof(fitted), model->message, 14, 330);
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

    /* 1. 全屏重绘底色与标准三段式外框 + 左侧返回面包屑 */
    fill_rect_packed(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, pack_rgb(UI_BLENDED(page_bg)));
    ui_background_draw_frame(pixels, stride);
    draw_header(pixels, stride, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_TITLE), ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CARD_NOTE));
    draw_time_status_bar(pixels, stride, model);
    {
        char path[128];
        char fitted_path[128];
        PtcUiTextArg pargs[] = {PTC_UI_TEXT_STRING("name", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_TITLE))};
        (void)ptc_ui_text_format(PTC_UI_T_SECURITY_PREFS_S, path, sizeof(path), pargs, 1);
        home_button(pixels, stride, ptc_ui_advanced_back_rect(), ptc_ui_text(PTC_UI_T_B_BACK_TO_SETTINGS), false, false, false);
        fit_text(fitted_path, sizeof(fitted_path), path, 20, 540);
        draw_text(pixels, stride, 266, 146, fitted_path, 20, UI_MUTED);
    }

    /* 2. 右上分段切换轨道 (Tabs: 16 创建备份 / 17 从备份恢复, X=830..1230, Y=116, H=46) */
    {
        UiRect tab_track = {830, 116, 400, 46};
        fill_round_rect(pixels, stride, tab_track, 23, UI_SURFACE);
        draw_rect_outline(pixels, stride, tab_track, 23, 1, UI_BORDER);
    }
    for (int t = 0; t < 2; ++t) {
        int idx = 16 + t;
        UiRect rect = to_uirect(ptc_ui_config_backup_field_rect(idx));
        bool active = (model->config_backup_tab == t);
        bool focused = (model->overlay_selection == idx);
        fill_round_rect(pixels, stride, rect, 19, active ? UI_ACCENT : UI_SURFACE);
        if (focused) draw_focus_ring(pixels, stride, rect, 19);
        draw_text_center(pixels, stride, rect,
            ptc_ui_text(t == 0 ? PTC_UI_T_CONFIG_BACKUP_TAB_CREATE : PTC_UI_T_CONFIG_BACKUP_TAB_RESTORE),
            15, active ? UI_ON_ACCENT : UI_INK);
    }

    if (model->config_backup_tab == 0) {
        /* ======================== Tab 0: 创建备份 ======================== */
        char fitted[160];
        UiRect hero = {54, 172, 744, 360};
        draw_plan_card(pixels, stride, hero, false);
        const char *tag_txt = ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE);
        int tag_w = measure_text(tag_txt, 12) + 20;
        if (tag_w > 180) tag_w = 180;
        UiRect tag_rect = {hero.x + hero.width - 24 - tag_w, hero.y + 18, tag_w, 24};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE_HERO_TITLE), 21,
                 tag_rect.x - (hero.x + 24) - 12);
        draw_text_bold(pixels, stride, hero.x + 24, hero.y + 38, fitted, 21, UI_INK);

        fill_round_rect(pixels, stride, tag_rect, 12, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, tag_rect, 12, 1, UI_ACCENT);
        fit_text(fitted, sizeof(fitted), tag_txt, 12, tag_rect.width - 12);
        draw_text_center(pixels, stride, tag_rect, fitted, 12, UI_ACCENT);

        draw_wrapped_text(pixels, stride, hero.x + 24, hero.y + 72, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_CREATE_HERO_DESC), 15, hero.width - 48, 22, 3, UI_MUTED);

        /* 存储路径卡片 */
        UiRect path_box = {hero.x + 24, hero.y + 150, hero.width - 48, 46};
        fill_round_rect(pixels, stride, path_box, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, path_box, 10, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_TARGET_PATH), 14, path_box.width - 32);
        draw_text(pixels, stride, path_box.x + 16, path_box.y + 28, fitted, 14, UI_ACCENT);

        /* 3-Module Summary Cards (游玩规则 / 偏好与安全 / 防篡改校验) */
        {
            static const PtcUiTextId MOD_T[3] = {
                PTC_UI_T_CONFIG_BACKUP_MOD_RULES_TITLE,
                PTC_UI_T_CONFIG_BACKUP_MOD_PREFS_TITLE,
                PTC_UI_T_CONFIG_BACKUP_MOD_VERIFY_TITLE
            };
            static const PtcUiTextId MOD_D[3] = {
                PTC_UI_T_CONFIG_BACKUP_MOD_RULES_DESC,
                PTC_UI_T_CONFIG_BACKUP_MOD_PREFS_DESC,
                PTC_UI_T_CONFIG_BACKUP_MOD_VERIFY_DESC
            };
            const uint32_t MOD_C[3] = {UI_ACCENT, UI_WARNING_TEXT, UI_SUCCESS};
            int mw = (hero.width - 48 - 24) / 3;
            for (int m = 0; m < 3; ++m) {
                UiRect mc = {hero.x + 24 + m * (mw + 12), hero.y + 210, mw, 130};
                fill_round_rect(pixels, stride, mc, 10, UI_RGB(UI_BLENDED(surface_raised)));
                draw_rect_outline(pixels, stride, mc, 10, 1, UI_BORDER);
                fit_text(fitted, sizeof(fitted), ptc_ui_text(MOD_T[m]), 14, mc.width - 24);
                draw_text_bold(pixels, stride, mc.x + 12, mc.y + 26, fitted, 14, MOD_C[m]);
                draw_wrapped_text(pixels, stride, mc.x + 12, mc.y + 52, ptc_ui_text(MOD_D[m]),
                                  12, mc.width - 24, 18, 4, UI_MUTED);
            }
        }

        /* 创建备份操作按钮 (Field 13, Y=552, H=58, W=744) */
        UiRect btn_create = to_uirect(ptc_ui_config_backup_field_rect(13));
        bool create_focused = (model->overlay_selection == 13);
        bool create_disabled = model->waiting || model->recovery_active || model->hot_reload_status == PTC_UI_HOT_RELOAD_RUNNING;
        fill_round_rect(pixels, stride, btn_create, 12, create_disabled ? UI_RAISED : UI_ACCENT);
        if (create_focused) draw_focus_ring(pixels, stride, btn_create, 12);
        else draw_rect_outline(pixels, stride, btn_create, 12, 1, create_disabled ? UI_BORDER : UI_ACCENT);
        char create_label[160];
        snprintf(create_label, sizeof(create_label), "A  %s", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_BTN_CREATE));
        draw_button_label(pixels, stride, btn_create, create_label, 19, create_disabled ? UI_DISABLED : UI_ON_ACCENT);

        /* 右侧上方：SD 卡快照状态卡片 (group_rect(3), Y=172..444) */
        UiRect info = to_uirect(ptc_ui_config_backup_group_rect(3));
        draw_plan_card(pixels, stride, info, false);
        const char *st_txt = ptc_ui_text(model->config_backup_ready ? PTC_UI_T_CONFIG_BACKUP_SAVED : PTC_UI_T_STATUS_TO_CONFIRM);
        int st_w = measure_text(st_txt, 11) + 18;
        if (st_w > 160) st_w = 160;
        UiRect status_pill = {info.x + info.width - 18 - st_w, info.y + 18, st_w, 24};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_SD_STATUS_TITLE), 18,
                 status_pill.x - (info.x + 18) - 8);
        draw_text_bold(pixels, stride, info.x + 18, info.y + 36, fitted, 18, UI_INK);
        fill_round_rect(pixels, stride, status_pill, 12, model->config_backup_ready ? UI_SUCCESS_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, status_pill, 12, 1, model->config_backup_ready ? UI_SUCCESS_BORDER : UI_BORDER);
        fit_text(fitted, sizeof(fitted), st_txt, 11, status_pill.width - 10);
        draw_text_center(pixels, stride, status_pill, fitted, 11, model->config_backup_ready ? UI_SUCCESS : UI_MUTED);

        if (model->config_backup_ready) {
            UiRect meta_box = {info.x + 18, info.y + 56, info.width - 36, 76};
            fill_round_rect(pixels, stride, meta_box, 10, UI_RGB(UI_BLENDED(surface_raised)));
            draw_rect_outline(pixels, stride, meta_box, 10, 1, UI_BORDER);
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_EXISTING_NOTICE), 12, meta_box.width - 24);
            draw_text(pixels, stride, meta_box.x + 12, meta_box.y + 24, fitted, 12, UI_MUTED);
            draw_wrapped_text(pixels, stride, meta_box.x + 12, meta_box.y + 46, model->config_metadata, 14, meta_box.width - 24, 18, 2, UI_ACCENT);

            UiRect warn_box = {info.x + 18, info.y + 144, info.width - 36, 72};
            fill_round_rect(pixels, stride, warn_box, 10, UI_WARNING_SOFT);
            draw_rect_outline(pixels, stride, warn_box, 10, 1, UI_WARNING_BORDER);
            draw_wrapped_text(pixels, stride, warn_box.x + 12, warn_box.y + 24,
                              ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_OVERWRITE_NOTICE), 12, warn_box.width - 24, 18, 3, UI_INK);
        } else {
            draw_wrapped_text(pixels, stride, info.x + 18, info.y + 76, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NO_FILE_NOTICE), 14, info.width - 36, 20, 2, UI_INK);
            draw_wrapped_text(pixels, stride, info.x + 18, info.y + 126, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NO_FILE_DESC), 13, info.width - 36, 19, 3, UI_MUTED);
        }
        draw_text(pixels, stride, info.x + 18, info.y + info.height - 18, "backups/config-backup.json", 12, UI_MUTED);

        /* 右侧下方：独立红色敏感恢复区 (Y=456..616) */
        {
            UiRect danger_card = {824, 456, 402, 160};
            fill_round_rect(pixels, stride, danger_card, 14, UI_DANGER_SOFT);
            draw_rect_outline(pixels, stride, danger_card, 14, 1, UI_DANGER_BORDER);
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_DANGER_ZONE_TITLE), 15, danger_card.width - 32);
            draw_text_bold(pixels, stride, danger_card.x + 16, danger_card.y + 28, fitted, 15, UI_DANGER);
            draw_wrapped_text(pixels, stride, danger_card.x + 16, danger_card.y + 52,
                              ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_DANGER_ZONE_DESC), 12,
                              danger_card.width - 32, 17, 2, UI_INK);
            bool restore_enabled = model->setup_snapshot_available && !model->parent_support_only &&
                !model->waiting && !model->recovery_active && model->hot_reload_status != PTC_UI_HOT_RELOAD_RUNNING;
            char restore_label[160];
            snprintf(restore_label, sizeof(restore_label), "X  %s", ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_RESTORE_ORIG_BTN));
            draw_candidate_button(pixels, stride, ptc_ui_config_backup_field_rect(18), restore_label,
                UI_SURFACE, UI_DANGER, model->overlay_selection == 18, !restore_enabled);
        }
    } else {
        /* ======================== Tab 1: 从备份恢复 ======================== */
        char fitted[160];
        const PtcUiTextId titles[] = {PTC_UI_T_QUOTA_RULES, PTC_UI_T_CONFIG_SECTION_POLICIES, PTC_UI_T_CONFIG_SECTION_SECURITY};
        for (int section = 0; section < 3; ++section) {
            UiRect card = to_uirect(ptc_ui_config_backup_group_rect(section));
            draw_plan_card(pixels, stride, card, false);
            draw_text_bold(pixels, stride, 70, card.y + 26, ptc_ui_text(titles[section]), 15, UI_ACCENT);
        }

        /* 渲染复选框控件 (0..10) 与三枚快捷按钮 (19 仅规则 / 11 全选 / 12 清空) */
        for (int i = 0; i < 20; ++i) {
            if (i == 13 || i == 14 || i == 15 || i == 16 || i == 17 || i == 18) continue;
            UiRect rect = to_uirect(ptc_ui_config_backup_field_rect(i));
            bool disabled = model->waiting || (i < 11 && (!model->config_backup_ready ||
                (i == 2 && !model->config_today_available) || (i == 9 && !model->config_pin_available))) ||
                ((i == 11 || i == 19) && !model->config_backup_ready);
            bool focused = (model->overlay_selection == i);
            const char *label = i < 11 ? ptc_ui_text(GROUPS[i]) :
                ptc_ui_text(i == 19 ? PTC_UI_T_CONFIG_BACKUP_RULES_ONLY :
                            (i == 11 ? PTC_UI_T_CONFIG_BACKUP_ALL : PTC_UI_T_CONFIG_BACKUP_NONE));

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
                fit_text(fitted, sizeof(fitted), label, 15, rect.width - 56);
                draw_text(pixels, stride, rect.x + 44, rect.y + 26, fitted, 15, disabled ? UI_DISABLED : (focused ? UI_ACCENT : UI_INK));
            } else {
                fill_round_rect(pixels, stride, rect, 8, focused ? UI_ACCENT_SOFT : UI_SURFACE);
                draw_rect_outline(pixels, stride, rect, 8, 1, focused ? UI_ACCENT : UI_BORDER);
                if (focused) draw_focus_ring(pixels, stride, rect, 8);
                fit_text(fitted, sizeof(fitted), label, 14, rect.width - 16);
                draw_text_center(pixels, stride, rect, fitted, 14, disabled ? UI_DISABLED : (focused ? UI_ACCENT : UI_INK));
            }
        }

        /* 右侧恢复影响与前后对比卡片 */
        UiRect info = {824, 172, 402, 368};
        draw_plan_card(pixels, stride, info, false);

        unsigned int count = 0;
        for (int i = 0; i < 11; ++i) if (model->config_groups & (1u << i)) ++count;
        char selected[128];
        PtcUiTextArg args[] = {PTC_UI_TEXT_NUMBER("count", count)};
        (void)ptc_ui_text_format(PTC_UI_T_RESTORE_SELECTED_NAMED, selected, sizeof(selected), args, 1);
        int cw = measure_text(selected, 12) + 20;
        if (cw > 160) cw = 160;
        UiRect count_badge = {info.x + info.width - 18 - cw, info.y + 16, cw, 24};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_RESTORE_IMPACT), 18,
                 count_badge.x - (info.x + 18) - 8);
        draw_text_bold(pixels, stride, info.x + 18, info.y + 34, fitted, 18, UI_INK);
        fill_round_rect(pixels, stride, count_badge, 12, count > 0 ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, count_badge, 12, 1, count > 0 ? UI_ACCENT : UI_BORDER);
        fit_text(fitted, sizeof(fitted), selected, 12, count_badge.width - 10);
        draw_text_center(pixels, stride, count_badge, fitted, 12, count > 0 ? UI_ACCENT : UI_MUTED);

        /* Before -> After Impact Strip distinguishing Preferences vs Rules */
        {
            bool has_rules = (model->config_groups & 0xFFu) != 0;
            UiRect cmp_box = {info.x + 18, info.y + 52, info.width - 36, 52};
            if (count == 0 || !model->config_backup_ready) {
                fill_round_rect(pixels, stride, cmp_box, 10, UI_PAGE);
                draw_rect_outline(pixels, stride, cmp_box, 10, 1, UI_BORDER);
                fit_text(fitted, sizeof(fitted),
                         model->config_metadata[0] ? model->config_metadata : ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_SELECTION),
                         13, cmp_box.width - 20);
                draw_text_center(pixels, stride, cmp_box, fitted, 13, UI_MUTED);
            } else if (!has_rules) {
                fill_round_rect(pixels, stride, cmp_box, 10, UI_SUCCESS_SOFT);
                draw_rect_outline(pixels, stride, cmp_box, 10, 1, UI_SUCCESS_BORDER);
                draw_wrapped_text(pixels, stride, cmp_box.x + 12, cmp_box.y + 22,
                                  ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_PREFS_UNCHANGED), 12,
                                  cmp_box.width - 24, 16, 2, UI_SUCCESS);
            } else {
                char cur_rem[48], line[160];
                if (model->unrestricted_today == 1) snprintf(cur_rem, sizeof(cur_rem), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
                else format_duration(model->remaining_available ? model->remaining_minutes : -1, cur_rem, sizeof(cur_rem));
                snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_BEFORE_AFTER), cur_rem);
                fill_round_rect(pixels, stride, cmp_box, 10, UI_ACCENT_SOFT);
                draw_rect_outline(pixels, stride, cmp_box, 10, 1, UI_ACCENT);
                draw_wrapped_text(pixels, stride, cmp_box.x + 12, cmp_box.y + 22, line, 13,
                                  cmp_box.width - 24, 17, 2, UI_ACCENT);
            }
        }

        UiRect warn_box = {info.x + 18, info.y + 114, info.width - 36, 62};
        fill_round_rect(pixels, stride, warn_box, 10, UI_WARNING_SOFT);
        draw_rect_outline(pixels, stride, warn_box, 10, 1, UI_WARNING_BORDER);
        draw_wrapped_text(pixels, stride, warn_box.x + 12, warn_box.y + 22, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NOTE), 12, warn_box.width - 24, 17, 3, UI_INK);

        UiRect prev_box = {info.x + 18, info.y + 186, info.width - 36, 166};
        fill_round_rect(pixels, stride, prev_box, 10, UI_RGB(UI_BLENDED(surface_raised)));
        draw_rect_outline(pixels, stride, prev_box, 10, 1, UI_BORDER);
        draw_wrapped_text(pixels, stride, prev_box.x + 14, prev_box.y + 24, model->config_preview, 13, prev_box.width - 28, 19, 7, UI_INK);

        /* 导入操作按钮 (Field 14, Y=552, H=58, W=402) */
        UiRect btn_import = to_uirect(ptc_ui_config_backup_field_rect(14));
        bool import_focused = (model->overlay_selection == 14);
        bool import_disabled = model->waiting || !model->config_backup_ready || !model->config_groups;
        fill_round_rect(pixels, stride, btn_import, 12, import_disabled ? UI_RAISED : UI_ACCENT);
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
    {
        int64_t now = ptc_ui_render_now();
        draw_refresh_footer_button(pixels, stride, (PtcUiRect){236, 664, 130, 44},
            ptc_ui_text(PTC_UI_T_Y_REFRESH), ptc_ui_status_needs_refresh(model, now),
            ptc_ui_status_refresh_failed(model), false, model->waiting);
    }
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
