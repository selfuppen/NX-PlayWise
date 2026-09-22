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
        if (chosen) draw_text(pixels, stride, option.x + option.width - 74, option.y + 23,
                              "待保存", 15, UI_SUCCESS);
    }
    draw_dialog_button(pixels, stride, ptc_ui_shortcut_disable_rect(),
                       "ZL  关闭自定义快捷键", UI_DANGER_SOFT, UI_DANGER, true);
    draw_dialog_button(pixels, stride, ptc_ui_shortcut_hint_rect(),
                       model->shortcut_draft_show_hint ? "Y  孩子区提示：显示" : "Y  孩子区提示：隐藏",
                       UI_PAGE, UI_INK, true);
    ptc_ui_format_custom_shortcut_hint(model->shortcut_draft_label, shortcut_hint, sizeof(shortcut_hint));
    snprintf(status, sizeof(status), "%s",
             model->shortcut_draft_enabled ? shortcut_hint : "自定义入口已关闭；固定 Minus 松开即可进入，无需长按");
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 516, status, 18,
              model->shortcut_draft_enabled ? UI_SUCCESS : UI_DANGER);
    draw_overlay_actions(pixels, stride, model, "+  确认保存");
}

static void draw_software_info_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect details;
    char value[128];
    const char *status = "状态未知";
    uint32_t status_color = UI_MUTED;
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 560);
    details = (UiRect){dialog.x + 34, dialog.y + 106, dialog.width - 68, 326};
    fill_round_rect(pixels, stride, details, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, details, 16, 1, UI_BORDER);
    draw_text(pixels, stride, details.x + 24, details.y + 38, "主机应用", 18, UI_MUTED);
    snprintf(value, sizeof(value), "%.24s  (%.88s)", model->software_version,
             model->app_release_id[0] ? model->app_release_id : "身份未知");
    fit_text(value, sizeof(value), value, 18, details.width - 220);
    draw_text(pixels, stride, details.x + 180, details.y + 38, value, 18, UI_ACCENT);
    draw_text(pixels, stride, details.x + 24, details.y + 82, "当前后台", 18, UI_MUTED);
    fit_text(value, sizeof(value), model->backend_release_id[0] ? model->backend_release_id : "无法可信确认", 18,
             details.width - 220);
    draw_text(pixels, stride, details.x + 180, details.y + 82, value, 18, UI_INK);
    if (model->hot_reload_status == PTC_UI_HOT_RELOAD_CURRENT ||
        model->hot_reload_status == PTC_UI_HOT_RELOAD_SUCCESS) {
        status = "已加载"; status_color = UI_SUCCESS;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING) {
        status = "待加载"; status_color = UI_WARNING;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_INCOMPLETE) {
        status = "安装不完整"; status_color = UI_DANGER;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_RECOVERY_REQUIRED) {
        status = "需要恢复"; status_color = UI_DANGER;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_RUNNING) {
        status = "正在加载"; status_color = UI_WARNING;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_UNAVAILABLE) {
        status = "热加载不可用"; status_color = UI_WARNING;
    }
    draw_text(pixels, stride, details.x + 24, details.y + 126, "加载状态", 18, UI_MUTED);
    draw_text(pixels, stride, details.x + 180, details.y + 126, status, 20, status_color);
    draw_wrapped_text(pixels, stride, details.x + 180, details.y + 154,
        model->hot_reload_detail, 15, details.width - 210, 20, 2, UI_MUTED);
    draw_text(pixels, stride, details.x + 24, details.y + 216, "项目仓库", 18, UI_MUTED);
    draw_text(pixels, stride, details.x + 180, details.y + 216, model->repository_url, 17, UI_ACCENT);
    draw_text(pixels, stride, details.x + 24, details.y + 266, "家长网页", 18, UI_MUTED);
    draw_text(pixels, stride, details.x + 180, details.y + 266, model->pwa_url, 17, UI_SUCCESS);
    if (model->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING) {
        draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  暂不",
                           UI_RAISED, UI_INK, false);
        draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  加载新版",
                           UI_ACCENT, UI_ON_ACCENT, false);
    } else {
        draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  关闭",
                           UI_ACCENT, UI_ON_ACCENT, false);
    }
}

static void draw_album_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const char *state = model->album_restriction_state == 0 ? "未开启" :
                        model->album_restriction_state == 1 ? "已开启" :
                        model->album_restriction_state == 2 ? "需要处理" :
                        model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? "外部已配置" : "状态未知";
    draw_dialog_shell(pixels, stride, model, &dialog, 980, 560);
    draw_text(pixels, stride, dialog.x + 38, dialog.y + 126, "当前状态", 17, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 136, dialog.y + 126, state, 19,
              model->album_restriction_state == 1 ? UI_SUCCESS :
              model->album_restriction_state == 2 ? UI_WARNING :
              model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? UI_ACCENT : UI_MUTED);
    draw_candidate_button(pixels, stride, ptc_ui_album_refresh_rect(), "Y  重新检测",
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
        draw_text(pixels, stride, card.x + 24, card.y + 42,
                  index == 0 ? (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL
                                  ? "无需重复配置" : "配置自制程序菜单高级入口") :
                  (model->album_restriction_state == 2 && model->album_backup_valid ? "强制恢复可信备份" : "恢复原来的启动方式"),
                  21, enabled ? UI_INK : UI_DISABLED);
        draw_wrapped_text(pixels, stride, card.x + 24, card.y + 84,
                          index == 0
                            ? (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL
                                ? "当前磁盘配置已经提供相同入口。PlayWise 从未修改它，因此不会伪造安装前备份或声称可以恢复。"
                                : "先完整备份相关配置；重启后，在桌面‘手柄设置’图标上按住 X，再按 A，进入自制程序菜单（hbmenu）。")
                            : "按可信备份恢复原配置。卸载或删除 PlayWise 数据前必须完成恢复；外部修改不会被静默覆盖。",
                          16, card.width - 48, 25, 5, enabled ? UI_MUTED : UI_DISABLED);
        draw_text(pixels, stride, card.x + 24, card.y + 194,
                  enabled ? "A  继续" : "当前状态不可用", 16,
                  enabled ? UI_ACCENT : UI_DISABLED);
    }
    if (model->album_restriction_detail[0]) {
        draw_wrapped_text(pixels, stride, dialog.x + 38, dialog.y + 432,
                          model->album_restriction_detail, 14, dialog.width - 76, 20, 2, UI_MUTED);
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
}

static void draw_theme_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *LABELS[] = {"跟随系统", "浅色", "暗色"};
    static const char *DETAILS[] = {"随 Switch 设置", "清爽的浅色背景", "柔和的深色背景"};
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
                  ? "系统主题暂不可用，将安全回退为浅色。" : "方向键选择  |  A 立即应用并保存  |  B 取消",
              16, !g_theme.system_theme_available ? UI_WARNING : UI_MUTED);
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
    snprintf(row, sizeof(row), "事件：%s", model->recent_event_names[index][0] ? model->recent_event_names[index] : "未知");
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y, row, 18, dialog.width - 84, 28, 2, UI_INK);
    snprintf(row, sizeof(row), "操作类型：%s", model->recent_event_types[index][0] ? model->recent_event_types[index] : "未记录");
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2, UI_MUTED);
    snprintf(row, sizeof(row), "结果：%s", model->recent_event_errors[index][0] ? model->recent_event_errors[index] : "成功");
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2,
                          model->recent_event_errors[index][0] ? UI_DANGER : UI_SUCCESS);
    snprintf(row, sizeof(row), "时间：%s", time_text);
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 17, dialog.width - 84, 26, 2, UI_MUTED);
    snprintf(row, sizeof(row), "请求 ID：%s", model->recent_event_request_ids[index][0] ? model->recent_event_request_ids[index] : "未记录");
    y = draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 16, dialog.width - 84, 24, 3, UI_MUTED);
    snprintf(row, sizeof(row), "内部详情：%s", model->recent_event_details[index][0] ? model->recent_event_details[index] : "无");
    draw_wrapped_text(pixels, stride, dialog.x + 42, y + 8, row, 16, dialog.width - 84, 24, 3, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A / B  关闭",
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void format_decision_rule(const PtcUiDecisionStep *step, char *out, size_t out_size)
{
    if (!step || step->state == PTC_UI_DECISION_UNKNOWN ||
        step->state == PTC_UI_DECISION_NOT_CONFIGURED || step->state == PTC_UI_DECISION_NOT_MATCHED ||
        step->state == PTC_UI_DECISION_DISABLED || step->state == PTC_UI_DECISION_CALENDAR_UNCOVERED) {
        snprintf(out, out_size, "%s", step ? ptc_ui_decision_state_label(step->state) : "状态待确认");
    } else if (step->rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(out, out_size, "不限时");
    } else {
        snprintf(out, out_size, "%u 分钟", (unsigned int)step->rule.minutes);
    }
}

static void draw_waterfall_pipeline(uint32_t *pixels, uint32_t stride, int x, int y, int total_w,
                                    const PtcUiTodayDecision *decision, bool bedtime_enforcing)
{
    const char *titles[] = {"01 今日调整", "02 计划特例", "03 假日调休", "04 基础周计划"};
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

    draw_text(pixels, stride, x, y + 6, "额度决策流水线（优先级自高向低，命中即阻断后续）", 14, UI_INK);
    if (bedtime_enforcing) {
        UiRect pill = {x + total_w - 290, y, 290, 24};
        fill_round_rect(pixels, stride, pill, 6, UI_DANGER_SOFT);
        draw_rect_outline(pixels, stride, pill, 6, 1, UI_DANGER);
        draw_text_center(pixels, stride, pill, "🌙 就寝限制生效中 / 最终强制锁定", 12, UI_DANGER);
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
                draw_text_center(pixels, stride, badge, "🎯 额度命中 (🌙就寝锁定)", 11, UI_ON_ACCENT);
            } else {
                fill_round_rect(pixels, stride, badge, 4, UI_SUCCESS);
                draw_text_center(pixels, stride, badge, "🎯 当前生效命中", 11, UI_ON_ACCENT);
            }
        } else if (is_overridden) {
            fill_round_rect(pixels, stride, badge, 4, UI_WARNING_SOFT);
            draw_text_center(pixels, stride, badge, "🛡️ 已被上级覆盖", 11, UI_WARNING);
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
            desc = bedtime_enforcing ? "额度已生效 / 🌙就寝立断中" : "在此命中 / 阻断后续规则";
        } else if (is_overridden) {
            desc = "规则已配置，但上级优先";
        } else if (step->state == PTC_UI_DECISION_DISABLED) {
            desc = "未开启或未配置";
        } else if (step->state == PTC_UI_DECISION_CALENDAR_UNCOVERED) {
            desc = "校准日历未覆盖";
        } else {
            desc = (i == 0 ? "未配置调整 / 向下穿透" :
                   (i == 2 ? "非假日调休 / 向下穿透" : "未命中特例 / 向下穿透"));
        }
        draw_line(pixels, stride, card.x + 12, card.y + 98, card.x + card.width - 12, card.y + 98, 1, UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){card.x + 6, card.y + 104, card.width - 12, 24},
                         desc, 11, is_selected ? (bedtime_enforcing ? UI_DANGER : UI_SUCCESS) : UI_MUTED);

        /* 5. Connector line between node i and node i+1 */
        if (i < count - 1) {
            int line_start_x = card.x + card.width;
            int line_end_x = line_start_x + gap;
            int line_y = cards_y + node_h / 2;

            if (is_selected) {
                /* Short-circuit stop mark: ─┤ 阻断 */
                draw_line(pixels, stride, line_start_x, line_y, line_start_x + gap / 2, line_y, 2, UI_MUTED);
                draw_line(pixels, stride, line_start_x + gap / 2, line_y - 14, line_start_x + gap / 2, line_y + 14, 3, UI_DANGER);
                draw_text_center(pixels, stride, (UiRect){line_start_x, line_y - 28, gap, 16}, "阻断", 11, UI_DANGER);
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
    const char *runtime = !model->status_loaded ? "等待刷新" :
        model->disable_flag_present ? "控制已停用" : model->recovery_active ? "正在恢复" :
        model->apply_pending_confirmation ? "等待生效" : !fresh ? "状态待确认" :
        strcmp(model->setup_phase, "active") == 0 ? "正常运行" : "需家长确认";

    ptc_ui_build_today_decision(model, PTC_UI_PLAN_SAVED, ptc_ui_render_now(), &decision);
    ptc_ui_format_home_total_value(model, total, sizeof(total));
    ptc_ui_format_home_remaining(model, ptc_ui_render_now(), remaining, sizeof(remaining));
    ptc_ui_format_timer_status(model, timer, sizeof(timer));
    ptc_ui_format_today_mode(model, today, sizeof(today));

    if (fresh) {
        snprintf(effective, sizeof(effective), "%s  |  %s",
                 ptc_ui_effective_rule_label(decision.effective.source), total);
    } else {
        snprintf(effective, sizeof(effective), "状态待确认  |  暂不可用");
        snprintf(total, sizeof(total), "暂不可用");
        snprintf(today, sizeof(today), "状态待确认");
        snprintf(timer, sizeof(timer), "状态待确认");
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
    draw_text(pixels, stride, hero.x + 20, hero.y + 22, "今天还可玩", 13, UI_MUTED);
    draw_text(pixels, stride, hero.x + 20, hero.y + 64, remaining, 32, fresh ? UI_ACCENT : UI_MUTED);

    draw_text(pixels, stride, hero.x + 210, hero.y + 22, "今日总额度", 13, UI_MUTED);
    draw_text(pixels, stride, hero.x + 210, hero.y + 56, total, 20, UI_INK);

    draw_text(pixels, stride, hero.x + 370, hero.y + 22, "额度已耗(估算)", 13, UI_MUTED);
    if (fresh && played >= 0) snprintf(line, sizeof(line), "约 %d 分钟", played);
    else snprintf(line, sizeof(line), "暂不可用");
    draw_text(pixels, stride, hero.x + 370, hero.y + 56, line, 20, UI_INK);

    /* 生效规则指示徽章框 */
    UiRect active_badge = {hero.x + 540, hero.y + 14, hero.width - 556, 52};
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    uint32_t badge_bg = !fresh ? UI_WARNING_SOFT : (bedtime_enforcing ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
    uint32_t badge_border = !fresh ? UI_WARNING : (bedtime_enforcing ? UI_DANGER : UI_SUCCESS);
    fill_round_rect(pixels, stride, active_badge, 8, badge_bg);
    draw_rect_outline(pixels, stride, active_badge, 8, 1, badge_border);
    const char *badge_title = !fresh ? "规则状态待确认" :
        (bedtime_enforcing ? "🎯 基础额度生效 (🌙就寝限制中)" : "🎯 当前生效规则");
    draw_text(pixels, stride, active_badge.x + 14, active_badge.y + 20,
              badge_title, 12, badge_border);
    draw_text(pixels, stride, active_badge.x + (bedtime_enforcing ? 200 : 120), active_badge.y + 20, effective, 14, UI_INK);
    const char *final_desc = bedtime_enforcing
        ? "基础额度已就绪；当前处于就寝窗口，强制立断锁定" : decision.final_reason;
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
              "🌙 并行就寝限制", 13, bedtime_enforcing ? UI_DANGER : UI_WARNING);
    if (bedtime_enforcing) {
        UiRect enforcing_pill = {bedtime.x + bedtime.width - 112, bedtime.y + 8, 98, 24};
        fill_round_rect(pixels, stride, enforcing_pill, 6, UI_DANGER);
        draw_text_center(pixels, stride, enforcing_pill, "立断生效中", 12, UI_ON_ACCENT);
    } else {
        char bedtime_status[128];
        fit_text(bedtime_status, sizeof(bedtime_status), decision.bedtime, 14, bedtime.width - 158);
        draw_text(pixels, stride, bedtime.x + 130, bedtime.y + 22, bedtime_status, 14, UI_INK);
    }
    draw_text(pixels, stride, bedtime.x + 14, bedtime.y + 52,
              bedtime_enforcing ? "当前处于就寝窗口，独立于今日额度直接强制锁定机器。"
                                : "就寝限制独立于时长并行生效；到点后无论剩余额度直接锁定机器。", 11, UI_MUTED);

    UiRect autonomy = {x_left, bottom_y + 84, col_w, 76};
    fill_round_rect(pixels, stride, autonomy, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, autonomy, 10, 1, UI_BORDER);
    draw_text(pixels, stride, autonomy.x + 14, autonomy.y + 22, "🎁 自主缓冲额度", 13, UI_MUTED);
    draw_text(pixels, stride, autonomy.x + 130, autonomy.y + 22, decision.autonomy, 14, UI_INK);
    draw_text(pixels, stride, autonomy.x + 14, autonomy.y + 52,
              "由孩子在额度即将耗尽时自主申请，按预设条件追加缓冲时间。", 11, UI_MUTED);

    draw_text(pixels, stride, x_left, bottom_y + 180,
              "ℹ️ 决策流水线决定今日额度；就寝独立并行，自主缓冲按条件追加。", 11, UI_MUTED);

    /* 右下栏：系统运行、审计与健康 */
    UiRect sys_card = {x_right, bottom_y, col_w, 76};
    fill_round_rect(pixels, stride, sys_card, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, sys_card, 10, 1, UI_BORDER);

    draw_text(pixels, stride, sys_card.x + 14, sys_card.y + 20, "今日模式", 11, UI_MUTED);
    draw_text(pixels, stride, sys_card.x + 14, sys_card.y + 44, today, 14, UI_INK);

    draw_text(pixels, stride, sys_card.x + 130, sys_card.y + 20, "系统计时器", 11, UI_MUTED);
    draw_text(pixels, stride, sys_card.x + 130, sys_card.y + 44, timer, 14, fresh ? UI_INK : UI_WARNING);

    draw_text(pixels, stride, sys_card.x + 250, sys_card.y + 20, "PlayWise 守护", 11, UI_MUTED);
    draw_text(pixels, stride, sys_card.x + 250, sys_card.y + 44, runtime, 14,
              notice[0] || !fresh ? UI_WARNING : UI_SUCCESS);

    draw_text(pixels, stride, sys_card.x + 380, sys_card.y + 20, "近 7 天消耗", 11, UI_MUTED);
    if (model->usage_summary_available && model->usage_known_days_7 > 0)
        snprintf(line, sizeof(line), "%u 分钟", model->usage_consumed_minutes_7);
    else snprintf(line, sizeof(line), "暂不可用");
    draw_text(pixels, stride, sys_card.x + 380, sys_card.y + 44, line, 14, UI_INK);

    if (parent) {
        UiRect audit = {x_right, bottom_y + 84, col_w, 76};
        fill_round_rect(pixels, stride, audit, 10, UI_RAISED);
        draw_rect_outline(pixels, stride, audit, 10, 1, UI_BORDER);
        draw_text(pixels, stride, audit.x + 14, audit.y + 18, "最近指令执行审计", 12, UI_INK);
        snprintf(line, sizeof(line), "%s / %s", model->command_name, model->transport_label);
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
        draw_text(pixels, stride, child_tip.x + 14, child_tip.y + 20, "🌿 健康用机提醒", 12, UI_MUTED);
        draw_text(pixels, stride, child_tip.x + 14, child_tip.y + 44,
                  "合理规划游玩与休息时间，保护视力；就寝时间请准时休息。", 13, UI_INK);
        if (model->usage_summary_available && model->usage_known_days_30 > 0)
            snprintf(line, sizeof(line), "近 30 天已记录消耗: %u 分钟", model->usage_consumed_minutes_30);
        else snprintf(line, sizeof(line), "合理安排时间，享受精彩游戏。");
        draw_text(pixels, stride, child_tip.x + 14, child_tip.y + 64, line, 11, UI_MUTED);
    }

    draw_text(pixels, stride, x_right, bottom_y + 180,
              "本机额度消耗包含 HOME 亮屏使用；游戏明细暂不可用，缺失日期不计入。", 11, UI_MUTED);
}

static void draw_home_details(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char age[64];
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 640);
    format_status_age(model, age, sizeof(age));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 736, dialog.y + 26, 352, 30}, age, 17, status_age_color(model));
    draw_home_decision_details(pixels, stride, model, dialog);
    home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "A / B  返回", false, true, false);
}

static void draw_forecast_day_details(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *WEEKDAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
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

    if (is_today) {
        snprintf(title_buf, sizeof(title_buf), "计算规则生效逻辑 - 今天 %s", WEEKDAYS[weekday]);
    } else if (offset == 1) {
        snprintf(title_buf, sizeof(title_buf), "计算规则生效逻辑 - 明天 %s", WEEKDAYS[weekday]);
    } else {
        snprintf(title_buf, sizeof(title_buf), "计算规则生效逻辑 - %s (D+%d)", WEEKDAYS[weekday], offset);
    }
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
        snprintf(total, sizeof(total), "不限时");
    } else {
        snprintf(total, sizeof(total), "%u 分钟", (unsigned int)decision.effective.rule.minutes);
    }

    draw_text(pixels, stride, hero.x + 20, hero.y + 22, "当天总额度", 13, UI_MUTED);
    draw_text(pixels, stride, hero.x + 20, hero.y + 60, total, 28, UI_INK);

    bool covered = false;
    PtcCalendarDayType day_type = ptc_holiday_calendar_classify(target_day_index, &covered);
    const char *type_label = (covered && day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY) ? "法定休假日" :
        ((covered && day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY) ? "调休工作日" :
         ((weekday == 0 || weekday == 6) ? "普通周末" : "普通工作日"));

    draw_text(pixels, stride, hero.x + 190, hero.y + 22, "日历属性", 13, UI_MUTED);
    draw_text(pixels, stride, hero.x + 190, hero.y + 58, type_label, 20,
              (covered && day_type == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY) ? UI_WARNING :
              ((covered && day_type == PTC_CALENDAR_DAY_MAKEUP_WORKDAY) ? UI_ACCENT : UI_INK));

    draw_text(pixels, stride, hero.x + 340, hero.y + 22, "所属日期", 13, UI_MUTED);
    ptc_format_date(target_day_index, date_buf);
    draw_text(pixels, stride, hero.x + 340, hero.y + 58, date_buf, 20, UI_INK);

    /* 生效规则指示徽章框 */
    UiRect active_badge = {hero.x + 500, hero.y + 14, hero.width - 516, 52};
    fill_round_rect(pixels, stride, active_badge, 8, fresh ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
    draw_rect_outline(pixels, stride, active_badge, 8, 1, fresh ? UI_SUCCESS : UI_WARNING);

    snprintf(eff_label, sizeof(eff_label), "%s  |  %s",
             ptc_ui_effective_rule_label(decision.effective.source), total);
    draw_text(pixels, stride, active_badge.x + 14, active_badge.y + 20,
              fresh ? "最终生效规则" : "规则状态待确认", 12, fresh ? UI_SUCCESS : UI_WARNING);
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
    draw_text(pixels, stride, bedtime.x + 14, bedtime.y + 24, "并行就寝预测", 14, UI_WARNING);
    draw_text(pixels, stride, bedtime.x + 14, bedtime.y + 52, decision.bedtime, 15, UI_INK);
    draw_text(pixels, stride, bedtime.x + 14, bedtime.y + 78,
              "就寝时间并行生效；到点后即使有剩余额度也会锁定机器。", 11, UI_MUTED);

    /* 右下栏：规则裁决链条说明 */
    UiRect rule_info = {x_left + col_w + 24, bottom_y, col_w, 94};
    fill_round_rect(pixels, stride, rule_info, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, rule_info, 10, 1, UI_BORDER);
    draw_text(pixels, stride, rule_info.x + 14, rule_info.y + 24, "规则优先级裁决机制", 14, UI_INK);
    draw_text(pixels, stride, rule_info.x + 14, rule_info.y + 52,
              "今日调整(最高) -> 临时特例 -> 假日调休 -> 周常规(兜底)", 12, UI_ACCENT);
    draw_text(pixels, stride, rule_info.x + 14, rule_info.y + 78,
              "自高向低顺序求值，命中有效规则后立即阻断后续判定。", 11, UI_MUTED);

    home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "A / B  返回", false, true, false);
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
    default:
        return false;
    }
}
