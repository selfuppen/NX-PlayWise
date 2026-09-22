#include "ui_render_internal.h"

static const UiAction TODAY_ACTIONS[] = {
    {"今日额度", "当前由规则计划决定", UI_ACCENT, UI_ACTION_ICON_CLOCK, UI_ACTION_VISUAL_NONE},
    {"快速加时", "", UI_SUCCESS, UI_ACTION_ICON_ADD_TIME, UI_ACTION_VISUAL_QUICK_ADD},
    {"今日不限时", "今天不设时间上限", UI_SUCCESS, UI_ACTION_ICON_INFINITY, UI_ACTION_VISUAL_NONE},
    {"清除今日调整", "恢复下级额度规则", UI_MUTED, UI_ACTION_ICON_RESTORE, UI_ACTION_VISUAL_NONE},
    {"跳过本次就寝", "当前关闭", UI_WARNING, UI_ACTION_ICON_MOON, UI_ACTION_VISUAL_NONE},
    {"自主缓冲", "当前关闭", UI_MUTED, UI_ACTION_ICON_BUFFER, UI_ACTION_VISUAL_NONE},
};

static const UiAction PLAN_ACTIONS[] = {
    {"临时额度", "当前关闭", UI_ACCENT, UI_ACTION_ICON_CALENDAR_RANGE, UI_ACTION_VISUAL_NONE},
    {"节假日额度", "当前关闭", UI_SUCCESS, UI_ACTION_ICON_HOLIDAY, UI_ACTION_VISUAL_NONE},
    {"每周额度", "当前生效", UI_ACCENT, UI_ACTION_ICON_WEEKLY, UI_ACTION_VISUAL_NONE},
    {"就寝时间", "当前关闭", UI_WARNING, UI_ACTION_ICON_MOON, UI_ACTION_VISUAL_NONE},
    {"自主缓冲", "当前关闭", UI_SUCCESS, UI_ACTION_ICON_BUFFER, UI_ACTION_VISUAL_NONE},
};

static const UiAction GRANT_ACTIONS[] = {
    {"主机生成", "本机签名 / 8 位码", UI_SUCCESS, UI_ACTION_ICON_CONSOLE, UI_ACTION_VISUAL_NONE},
    {"手机 / 电脑生成", "跨设备离线可用", UI_ACCENT, UI_ACTION_ICON_DEVICE, UI_ACTION_VISUAL_NONE},
    {"生成设置", "设备与密钥已配置", UI_MUTED, UI_ACTION_ICON_SLIDERS, UI_ACTION_VISUAL_NONE},
    {"使用记录", "最近 100 条", UI_MUTED, UI_ACTION_ICON_HISTORY, UI_ACTION_VISUAL_NONE},
};

static const UiAction SETTINGS_ACTIONS[] = {
    {"外观主题", "", UI_ACCENT, UI_ACTION_ICON_THEME, UI_ACTION_VISUAL_THEME},
    {"修改 PIN", "当前已启用", UI_ACCENT, UI_ACTION_ICON_KEY, UI_ACTION_VISUAL_NONE},
    {"家长区快捷键", "当前：Minus", UI_ACCENT, UI_ACTION_ICON_CONTROLLER, UI_ACTION_VISUAL_NONE},
    {"自制程序高级入口", "未开启", UI_DANGER, UI_ACTION_ICON_HOMEBREW, UI_ACTION_VISUAL_NONE},
    {"家庭活动", "最近 200 条", UI_MUTED, UI_ACTION_ICON_ACTIVITY, UI_ACTION_VISUAL_NONE},
};

const UiAction GRANT_MANAGER_ACTIONS[] = {
    {"管理加时码设备名", "查看、输入或随机生成设备名", UI_ACCENT, UI_ACTION_ICON_DEVICE, UI_ACTION_VISUAL_NONE},
    {"管理加时码密钥", "查看、输入或随机生成签名密钥", UI_DANGER, UI_ACTION_ICON_KEY, UI_ACTION_VISUAL_NONE},
    {"导出手机/电脑配置", "导出供手机或电脑使用的配置文件", UI_SUCCESS, UI_ACTION_ICON_EXPORT, UI_ACTION_VISUAL_NONE},
    {"编辑二维码跳转地址", "修改扫码后打开的网页地址", UI_ACCENT, UI_ACTION_ICON_DEVICE, UI_ACTION_VISUAL_NONE},
    {"恢复二维码跳转默认地址", "恢复项目提供的默认网页地址", UI_MUTED, UI_ACTION_ICON_RESTORE, UI_ACTION_VISUAL_NONE},
};

static const UiAction SUPPORT_ACTIONS[] = {
    {"接管系统控制", "安全预检后启用额度管理", UI_ACCENT, UI_ACTION_ICON_SHIELD, UI_ACTION_VISUAL_NONE},
    {"重试修复", "重新检查安全前置条件", UI_SUCCESS, UI_ACTION_ICON_REPAIR, UI_ACTION_VISUAL_NONE},
    {"紧急停用", "停止新的控制写入", UI_DANGER, UI_ACTION_ICON_STOP, UI_ACTION_VISUAL_NONE},
    {"恢复安装前状态", "恢复原始设置并停用", UI_DANGER, UI_ACTION_ICON_RESTORE, UI_ACTION_VISUAL_NONE},
    {"导出诊断", "不含密钥、PIN 或离线码", UI_MUTED, UI_ACTION_ICON_EXPORT, UI_ACTION_VISUAL_NONE},
    {"软件信息", "版本、项目仓库和家长网页", UI_ACCENT, UI_ACTION_ICON_INFO, UI_ACTION_VISUAL_NONE},
};

static const UiAction RESUME_CONTROL_ACTION = {
    "解除停用并重新接管", "安全预检后恢复后台控制", UI_SUCCESS,
    UI_ACTION_ICON_REPAIR, UI_ACTION_VISUAL_NONE
};

static const UiAction RECONFIRM_ENVIRONMENT_ACTION = {
    "重新检测并接管", "环境变化，确认兼容后恢复控制", UI_WARNING,
    UI_ACTION_ICON_REPAIR, UI_ACTION_VISUAL_NONE
};
static void draw_parent_home_summary(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect box = to_uirect(ptc_ui_home_summary_rect(true));
    char remaining[64], today[64], line[192], age[64];
    int x = box.x + 28;
    ptc_ui_format_home_remaining(model, ptc_ui_render_now(), remaining, sizeof(remaining));
    ptc_ui_format_today_mode(model, today, sizeof(today));
    draw_card_shadow(pixels, stride, box, 16);
    fill_round_rect(pixels, stride, box, 16, UI_RGB(UI_BLENDED(hero)));
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    /* 剩余告急或就寝生效时描边随呼吸相位脉冲。 */
    if ((model->remaining_available && model->unrestricted_today != 1 &&
         model->remaining_minutes >= 0 && model->remaining_minutes <= 10) ||
        bedtime_enforcing) {
        int phase = get_breathing_phase();
        draw_rect_outline(pixels, stride, box, 16, 2,
                          UI_RGB(ui_mix_rgb(UI_BLENDED(danger), 0xFF9A8A, phase * 4)));
    }
    draw_text(pixels, stride, x, box.y + 42,
              bedtime_enforcing ? "今天还可玩（就寝立断）" : "今天还可玩",
              22, UI_RGB(UI_BLENDED(hero_secondary)));
    /* 环形额度表：弧长由缓动后的剩余分钟驱动，颜色沿用今日额度健康色；
     * 数据不可用时只画弱化轨道环。 */
    {
        float fraction = 0.0f;
        uint32_t ring_fill = UI_SUCCESS;
        bool has_fraction = false;
        if (bedtime_enforcing) {
            fraction = 0.0f;
            ring_fill = UI_DANGER;
            has_fraction = true;
        } else if (model->unrestricted_today == 1) {
            fraction = 1.0f;
            ring_fill = UI_SUCCESS;
            has_fraction = true;
        } else if (model->remaining_available && model->played_minutes_available &&
                   model->remaining_minutes >= 0 && model->played_minutes >= 0 &&
                   model->remaining_minutes + model->played_minutes > 0) {
            int total = model->remaining_minutes + model->played_minutes;
            /* 与下方额度条同语义：弧长表示剩余占比，随消耗缩小。 */
            fraction = (float)model->displayed_remaining_minutes / (float)total;
            has_fraction = true;
        } else if (model->remaining_available && model->remaining_minutes > 0) {
            int shown = model->remaining_minutes > 120 ? 120 : model->remaining_minutes;
            fraction = (float)shown / 120.0f;
            has_fraction = true;
        }
        if (has_fraction && model->unrestricted_today != 1 && !bedtime_enforcing) {
            if (model->remaining_available && model->remaining_minutes <= 10) ring_fill = UI_DANGER;
            else if (model->remaining_available && model->remaining_minutes <= 30) ring_fill = UI_WARNING;
            else ring_fill = UI_SUCCESS;
        }
        if (fraction < 0) fraction = 0;
        if (fraction > 1) fraction = 1;
        draw_ring_progress(pixels, stride, box.x + box.width - 46, box.y + 40, 16, 5, fraction,
                           UI_RGB(ui_mix_rgb(UI_BLENDED(hero), 0xFFFFFF, 20)), ring_fill);
    }
    /* Split only the existing numeric formatter output. Unknown/stale states
     * retain their words and are never converted to a numeric zero. */
    char *unit = strstr(remaining, " 分钟");
    if (unit) {
        *unit = '\0';
        int minutes = atoi(remaining);
        int num_w = measure_text(remaining, 80);
        draw_text_bold(pixels, stride, x, box.y + 133, remaining, 80, UI_RGB(UI_BLENDED(on_hero)));
        int unit_x = x + num_w + 12;
        draw_text(pixels, stride, unit_x, box.y + 130, "分钟", 24, UI_RGB(UI_BLENDED(hero_secondary)));
        if (minutes >= 60) {
            char duration_str[64];
            if (minutes % 60 == 0) {
                snprintf(duration_str, sizeof(duration_str), "（%d 小时）", minutes / 60);
            } else {
                snprintf(duration_str, sizeof(duration_str), "（%d 小时 %d 分钟）", minutes / 60, minutes % 60);
            }
            int dur_x = unit_x + measure_text("分钟", 24) + 12;
            draw_text(pixels, stride, dur_x, box.y + 130, duration_str, 20, UI_RGB(UI_BLENDED(hero_secondary)));
        }
    } else {
        draw_wrapped_text(pixels, stride, x, box.y + 124, remaining, 40, box.width - 56, 48, 2, UI_RGB(UI_BLENDED(on_hero)));
    }
    /* 今日额度胶囊进度槽 (Time Progress Gauge) */
    UiRect gauge_bg = {box.x + 28, box.y + 150, box.width - 56, 8};
    fill_round_rect(pixels, stride, gauge_bg, 4, UI_GAUGE_SLOT);
    draw_rect_outline(pixels, stride, gauge_bg, 4, 1, UI_GAUGE_SLOT_BORDER);

    uint32_t health_color = UI_SUCCESS;
    int remaining_mins = model->remaining_available ? model->remaining_minutes : -1;
    if (bedtime_enforcing) {
        health_color = UI_DANGER;
    } else if (remaining_mins >= 0) {
        if (remaining_mins <= 10) health_color = UI_DANGER;
        else if (remaining_mins <= 30) health_color = UI_WARNING;
        else health_color = UI_SUCCESS;
    }

    if (bedtime_enforcing) {
        /* 就寝生效时进度槽为空并呈红框 */
    } else if (model->unrestricted_today == 1) {
        fill_round_rect(pixels, stride, gauge_bg, 4, UI_SUCCESS);
    } else if (model->remaining_available && model->played_minutes_available &&
               (model->remaining_minutes + model->played_minutes > 0)) {
        int total_mins = model->remaining_minutes + model->played_minutes;
        /* 填充宽度用缓动后的剩余分钟，随数字一起滚动。 */
        int remain_w = (int)((int64_t)gauge_bg.width * model->displayed_remaining_minutes / total_mins);
        if (remain_w < 6 && model->remaining_minutes > 0) remain_w = 6;
        if (remain_w > gauge_bg.width) remain_w = gauge_bg.width;
        if (remain_w > 0) {
            fill_round_rect(pixels, stride, (UiRect){gauge_bg.x, gauge_bg.y, remain_w, gauge_bg.height}, 4, health_color);
        }
    } else if (model->remaining_available && model->remaining_minutes > 0) {
        int displayed_shown = model->displayed_remaining_minutes > 120 ? 120 :
                              (model->displayed_remaining_minutes < 0 ? 0 : model->displayed_remaining_minutes);
        int fill_w = (int)((int64_t)gauge_bg.width * displayed_shown / 120);
        if (fill_w < 6) fill_w = 6;
        fill_round_rect(pixels, stride, (UiRect){gauge_bg.x, gauge_bg.y, fill_w, gauge_bg.height}, 4, health_color);
    }

    if (bedtime_enforcing) {
        snprintf(line, sizeof(line), "就寝时间生效中（立断暂停游戏）  /  %s",
            model->status_loaded ? ui_rule_source_label(model->rule_source) : "待确认规则");
    } else {
        snprintf(line, sizeof(line), "今日%s  /  %s", today,
            model->status_loaded ? ui_rule_source_label(model->rule_source) : "待确认规则");
    }
    draw_text(pixels, stride, x, box.y + 176, line, 18, UI_RGB(UI_BLENDED(hero_secondary)));
    /* Supporting information sits on a separate surface, below the hero. */
    UiRect lower = {box.x + 12, box.y + 200, box.width - 24, box.height - 212};
    fill_round_rect(pixels, stride, lower, 16, UI_SURFACE);
    ptc_ui_format_home_total(model, line, sizeof(line));
    draw_text(pixels, stride, x, box.y + 236, line, 22, UI_INK);
    if (model->played_minutes_available && model->played_minutes >= 0)
        snprintf(line, sizeof(line), "额度消耗估算  约 %d 分钟", model->played_minutes);
    else snprintf(line, sizeof(line), "额度消耗估算  暂不可用");
    draw_text(pixels, stride, x, box.y + 272, line, 18, UI_MUTED);
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, x, box.y + box.height - 26, age, 16, UI_MUTED);
}

static const UiAction *actions_for_page(PtcUiParentPage page, int *count)
{
    if (page == PTC_UI_PARENT_PLAN) {
        *count = (int)(sizeof(PLAN_ACTIONS) / sizeof(PLAN_ACTIONS[0]));
        return PLAN_ACTIONS;
    }
    if (page == PTC_UI_PARENT_GRANT) {
        *count = (int)(sizeof(GRANT_ACTIONS) / sizeof(GRANT_ACTIONS[0]));
        return GRANT_ACTIONS;
    }
    if (page == PTC_UI_PARENT_SETTINGS) {
        *count = (int)(sizeof(SETTINGS_ACTIONS) / sizeof(SETTINGS_ACTIONS[0]));
        return SETTINGS_ACTIONS;
    }
    if (page == PTC_UI_PARENT_SUPPORT) {
        *count = (int)(sizeof(SUPPORT_ACTIONS) / sizeof(SUPPORT_ACTIONS[0]));
        return SUPPORT_ACTIONS;
    }
    *count = (int)(sizeof(TODAY_ACTIONS) / sizeof(TODAY_ACTIONS[0]));
    return TODAY_ACTIONS;
}

static void draw_tabs(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *LABELS[] = {"今日调度", "时间计划", "离线加时", "安全与偏好", "支持与恢复"};
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page != PTC_UI_PLAN_PAGE_ROOT) {
        const char *name = model->plan_page == PTC_UI_PLAN_PAGE_WEEKLY ? "每周计划" :
            (model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY ? "国家节假日" : "就寝时间");
        home_button(pixels, stride, ptc_ui_advanced_back_rect(), "B  返回时间计划", false, false, false);
        {
            char path[96];
            snprintf(path, sizeof(path), "时间计划 / %s", name);
            draw_text(pixels, stride, 278, 140, path, 22, UI_MUTED);
        }
        return;
    }
    int index;
    for (index = 0; index < PTC_UI_PARENT_PAGE_COUNT; ++index) {
        UiRect tab = to_uirect(ptc_ui_parent_tab_rect(index));
        fill_round_rect(pixels, stride, tab, 12, UI_RAISED);
    }
    /* 选中指示胶囊从上一页位置滑向当前页，页签文字保持不动。 */
    {
        UiRect from = to_uirect(ptc_ui_parent_tab_rect((int)model->last_parent_page));
        UiRect to = to_uirect(ptc_ui_parent_tab_rect((int)model->parent_page));
        float slide_t = 1.0f;
        if (model->last_parent_page != model->parent_page) {
            slide_t = ui_ease_out((float)(ptc_ui_anim_now_ms() - model->page_switched_at_ms) /
                                  (float)PTC_UI_PAGE_SWITCH_TOTAL_MS);
        }
        UiRect pill = {
            from.x + (int)((to.x - from.x) * slide_t),
            from.y,
            from.width + (int)((to.width - from.width) * slide_t),
            from.height};
        fill_round_rect(pixels, stride, pill, 12, UI_ACCENT);
    }
    for (index = 0; index < PTC_UI_PARENT_PAGE_COUNT; ++index) {
        UiRect tab = to_uirect(ptc_ui_parent_tab_rect(index));
        bool active = index == (int)model->parent_page;
        draw_text_center(pixels, stride, tab, LABELS[index], 18, active ? UI_ON_ACCENT : UI_INK);
    }
    draw_text(pixels, stride, 1038, 140, "L / R 切换", 19, UI_MUTED);
}

static void draw_settings_badge(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *label = ptc_ui_settings_status_label(model);
    uint32_t color = label && strcmp(label, "需处理") == 0 ? UI_DANGER : UI_WARNING;
    UiRect badge;
    if (!label) return;
    badge = (UiRect){54 + PTC_UI_PARENT_SUPPORT * 174 + 96, 113, 56, 24};
    fill_round_rect(pixels, stride, badge, 6, color);
    draw_text_center(pixels, stride, badge, label, 11, UI_ON_ACCENT);
}

static void draw_safety_status(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 452};
    char troubleshoot[128];
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    int recommended = ptc_ui_support_recommended_action(model);
    draw_text(pixels, stride, panel.x + 26, panel.y + 36, "当前问题", 23, UI_INK);
    draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 70, ptc_ui_support_problem(model),
                      18, panel.width - 52, 25, 2, UI_RGB(UI_BLENDED(text_primary)));
    const char *next = recommended == 0 ? (model->disable_flag_present ? "建议：解除停用并重新接管" : "建议：重新检测并接管") :
                       recommended == 1 ? "建议：选择重试修复" :
                       recommended == 4 ? "建议：导出诊断包，保留问题记录" :
                       (model->waiting || model->apply_pending_confirmation ? "请等待结果，再刷新状态" : "无需恢复操作，可按 B 返回设置");
    draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 130, next, 16,
                      panel.width - 52, 23, 2, UI_RGB(UI_BLENDED(accent)));
    char age[80];
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, panel.x + 26, panel.y + 188, age, 15, status_age_color(model));
    if (model->environment_available)
        snprintf(troubleshoot, sizeof(troubleshoot), "HOS %s  |  %s", model->environment_hos, model->environment_model);
    else snprintf(troubleshoot, sizeof(troubleshoot), "环境详情暂不可用，可导出诊断");
    fit_text(troubleshoot, sizeof(troubleshoot), troubleshoot, 15, panel.width - 52);
    draw_text(pixels, stride, panel.x + 26, panel.y + 214, troubleshoot, 15, UI_RGB(UI_BLENDED(text_secondary)));
    draw_text(pixels, stride, panel.x + 26, panel.y + 252, "最近事件", 18, UI_MUTED);
    if (model->recent_event_count > 0) {
        for (int event_index = 0; event_index < model->recent_event_count; ++event_index) {
            char latest[192];
            char event_time[48];
            int source_index = model->recent_event_count - 1 - event_index;
            format_event_time(model->recent_event_timestamps[source_index], false, event_time, sizeof(event_time));
            snprintf(latest, sizeof(latest), "%s  |  %s", model->recent_events[source_index], event_time);
            fit_text(latest, sizeof(latest), latest, 14, panel.width - 52);
            UiRect ev_rect = to_uirect(ptc_ui_support_event_rect(event_index));
            if (event_index + 6 == model->selected_index && !model->parent_footer_focused)
                draw_rect_outline(pixels, stride, ev_rect, 8, 2, UI_RGB(UI_BLENDED(focus)));
            draw_text(pixels, stride, panel.x + 26, ev_rect.y + 24,
                      latest, 14, event_index + 6 == model->selected_index ? UI_ACCENT : UI_MUTED);
        }
    } else {
        draw_text(pixels, stride, panel.x + 26, panel.y + 288,
                  model->recent_events_available ? "最近没有需要注意的事件" : "暂时无法读取最近事件，可刷新后重试",
                  15, model->recent_events_available ? UI_MUTED : UI_DANGER);
    }
}

static void draw_today_status(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char adjustment_badge[32];
    char adjustment_detail[128];
    int64_t now = ptc_ui_render_now();
    ptc_ui_format_today_adjustment_status(model, now, adjustment_badge, sizeof(adjustment_badge),
                                          adjustment_detail, sizeof(adjustment_detail));
    draw_parent_home_summary(pixels, stride, model);
    for (int index = 0; index < 6; ++index) {
        UiRect box = to_uirect(ptc_ui_today_card_rect(index));
        bool focused = !model->parent_footer_focused && model->selected_index == index;
        bool clear_unavailable = index == 3 && ptc_ui_status_is_fresh(model, now) &&
                                 !model->today_override_present;
        bool disabled = model->disable_flag_present || model->waiting || clear_unavailable;
        const char *title = TODAY_ACTIONS[index].title;
        const char *subtitle = TODAY_ACTIONS[index].subtitle;
        char dynamic[128];
        UiAction action = TODAY_ACTIONS[index];
        if (index == 0) {
            subtitle = adjustment_detail;
        } else if (index == 3 && clear_unavailable) {
            subtitle = model->today_override_cleared_in_session
                ? "本次会话已清除，当前使用下级规则" : "今天没有单独额度调整，无需清除";
        } else if (index == 4) {
            if (!model->status_loaded) {
                subtitle = "刷新后显示当前或下次窗口";
            } else if (model->bedtime_active) {
                if (model->bedtime_skipped) {
                    snprintf(dynamic, sizeof(dynamic), "%02u:%02u 至次日 %02u:%02u（已跳过）",
                        (unsigned int)(model->bedtime_start_minute / 60),
                        (unsigned int)(model->bedtime_start_minute % 60),
                        (unsigned int)(model->bedtime_end_minute / 60),
                        (unsigned int)(model->bedtime_end_minute % 60));
                } else {
                    snprintf(dynamic, sizeof(dynamic), "%02u:%02u 至次日 %02u:%02u",
                        (unsigned int)(model->bedtime_start_minute / 60),
                        (unsigned int)(model->bedtime_start_minute % 60),
                        (unsigned int)(model->bedtime_end_minute / 60),
                        (unsigned int)(model->bedtime_end_minute % 60));
                }
                subtitle = dynamic;
            } else if (model->bedtime_skipped_window_available) {
                snprintf(dynamic, sizeof(dynamic), "%02u:%02u 至次日 %02u:%02u（已跳过）",
                    (unsigned int)(model->bedtime_skipped_start_minute / 60),
                    (unsigned int)(model->bedtime_skipped_start_minute % 60),
                    (unsigned int)(model->bedtime_skipped_end_minute / 60),
                    (unsigned int)(model->bedtime_skipped_end_minute % 60));
                subtitle = dynamic;
            } else if (model->bedtime_next_available) {
                uint16_t year;
                uint8_t month, day;
                if (ptc_date_from_day_index(model->bedtime_next_start_day_index, &year, &month, &day))
                    snprintf(dynamic, sizeof(dynamic), "%02u-%02u %02u:%02u 至 %02u:%02u", month, day,
                        (unsigned int)(model->bedtime_next_start_minute / 60),
                        (unsigned int)(model->bedtime_next_start_minute % 60),
                        (unsigned int)(model->bedtime_next_end_minute / 60),
                        (unsigned int)(model->bedtime_next_end_minute % 60));
                else snprintf(dynamic, sizeof(dynamic), "已开启，等待下次窗口");
                subtitle = dynamic;
            } else {
                subtitle = model->bedtime_policy.enabled ? "没有可跳过的近期窗口" : "当前关闭";
            }
        } else if (index == 5) {
            if (model->daily_buffer_minutes == 0) subtitle = "当前关闭，可在时间计划中设置";
            else if (model->daily_buffer_claimed) subtitle = "今日已领取，明天恢复资格";
            else if (model->daily_buffer_available) {
                snprintf(dynamic, sizeof(dynamic), "今日可领取 %u 分钟", (unsigned int)model->daily_buffer_minutes);
                subtitle = dynamic;
            } else subtitle = "今天暂不可领取";
        }
        action.title = title;
        action.subtitle = subtitle;
        draw_action_card(pixels, stride, box, &action, focused,
                         disabled ? PTC_UI_ACTION_DISABLED : PTC_UI_ACTION_AVAILABLE,
                         (index == 0 || index == 4) ? 82 : 0);
        if (index == 0) {
            UiRect tbadge = {box.x + box.width - 86, box.y + 10, 74, 22};
            uint32_t badge_color = strcmp(adjustment_badge, "生效中") == 0 ? UI_SUCCESS :
                (strcmp(adjustment_badge, "就寝立断") == 0 ? UI_WARNING :
                (strcmp(adjustment_badge, "控制停用") == 0 || strcmp(adjustment_badge, "恢复中") == 0
                    ? UI_DANGER :
                 (strcmp(adjustment_badge, "等待生效") == 0 || strcmp(adjustment_badge, "待确认") == 0
                    ? UI_WARNING : UI_MUTED)));
            fill_round_rect(pixels, stride, tbadge, 6,
                            badge_color == UI_SUCCESS ? UI_SUCCESS_SOFT :
                            (badge_color == UI_DANGER ? UI_DANGER_SOFT :
                             (badge_color == UI_WARNING ? UI_WARNING_SOFT : UI_PAGE)));
            draw_rect_outline(pixels, stride, tbadge, 6, 1, badge_color);
            draw_text_center(pixels, stride, tbadge, adjustment_badge, 12, badge_color);
        } else if (index == 4 && model->bedtime_active && !model->bedtime_skipped) {
            UiRect tbadge = {box.x + box.width - 78, box.y + 10, 66, 20};
            fill_round_rect(pixels, stride, tbadge, 5, UI_DANGER);
            draw_text_center(pixels, stride, tbadge, "限制中", 12, UI_ON_ACCENT);
        }
    }
    home_button(pixels, stride, ptc_ui_home_details_rect(true), "+  查看详情", false, false, model->waiting);
}

static void draw_grant_help(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 452};
    (void)model;
    fill_round_rect(pixels, stride, panel, 16, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    draw_text(pixels, stride, 868, 222, "加时码使用指南", 24, UI_RGB(UI_BLENDED(text_primary)));
    draw_text(pixels, stride, 868, 258, "无需主机联网，离线安全签名", 15, UI_MUTED);

    /* 3 个步骤卡片 */
    static const char *STEP_TITLES[] = {"1  选择要增加的时长", "2  验证 PIN 生成加时码", "3  孩子输入代码确认加时"};
    static const char *STEP_HINTS[] = {"支持 15/30/60 分钟或自定义时长", "签名密钥本地计算，有效防篡改", "孩子在掌机输入 8 位纯数字离线兑换"};
    for (int s = 0; s < 3; ++s) {
        UiRect step_box = {864, 280 + s * 72, panel.width - 44, 62};
        fill_round_rect(pixels, stride, step_box, 10, UI_RAISED);
        draw_rect_outline(pixels, stride, step_box, 10, 1, UI_BORDER);
        draw_text(pixels, stride, step_box.x + 14, step_box.y + 24, STEP_TITLES[s], 17, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, step_box.x + 14, step_box.y + 48, STEP_HINTS[s], 13, UI_MUTED);
    }
    draw_wrapped_text(pixels, stride, 868, 524, "说明：加时码仅当天有效，单次兑换后立即失效。家长可通过手机或电脑离线生成。", 14, 332, 20, 3, UI_RGB(UI_BLENDED(text_secondary)));
}

static void draw_diagnostic_notice(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect rect = {54, 522, 1172, 128};
    uint32_t accent = model->diagnostic_status == PTC_UI_DIAGNOSTIC_SUCCESS
        ? UI_SUCCESS
        : (model->diagnostic_status == PTC_UI_DIAGNOSTIC_ERROR
            ? UI_DANGER : UI_WARNING);
    char line[320];
    fill_round_rect(pixels, stride, rect, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, rect, 16, 1, UI_BORDER);
    fill_round_rect(pixels, stride, (UiRect){rect.x + 12, rect.y + 20, 4, rect.height - 40}, 2, accent);
    if (model->diagnostic_status == PTC_UI_DIAGNOSTIC_EXPORTING) {
        draw_text(pixels, stride, rect.x + 24, rect.y + 36, "正在导出诊断包...", 21, accent);
        draw_text(pixels, stride, rect.x + 24, rect.y + 76,
                  "诊断包会排除密钥、PIN、离线码和完整 nonce。", 17, UI_MUTED);
        return;
    }
    if (model->diagnostic_status == PTC_UI_DIAGNOSTIC_ERROR) {
        draw_text(pixels, stride, rect.x + 24, rect.y + 36, "诊断包导出失败。", 21, accent);
        draw_text(pixels, stride, rect.x + 24, rect.y + 76,
                  "请确认 SD 卡可写后重试。", 17, UI_MUTED);
        return;
    }
    snprintf(line, sizeof(line), "诊断包导出成功：%s", model->diagnostic_path);
    draw_text(pixels, stride, rect.x + 24, rect.y + 32, line, 17, UI_INK);
    draw_text(pixels, stride, rect.x + 24, rect.y + 66,
              "如遇到问题，提交 GitHub Issue 时请附上此文件。", 17, UI_INK);
    draw_text(pixels, stride, rect.x + 24, rect.y + 100,
              "GitHub 地址：https://github.com/selfuppen/NX-PlayWise/issues", 17, accent);
}

void draw_parent(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *TITLES[] = {"今日调度", "时间计划", "离线加时", "安全与偏好", "支持与恢复"};
    const UiAction *actions;
    int action_count;
    int index;
    const bool plan_subpage = model->parent_page == PTC_UI_PARENT_PLAN &&
        model->plan_page != PTC_UI_PLAN_PAGE_ROOT;
    const char *title = TITLES[model->parent_page >= 0 && model->parent_page < PTC_UI_PARENT_PAGE_COUNT
        ? model->parent_page : 0];
    if (model->parent_page == PTC_UI_PARENT_PLAN) {
        if (model->plan_page == PTC_UI_PLAN_PAGE_WEEKLY) title = "每周计划";
        else if (model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) title = "国家节假日";
        else if (model->plan_page == PTC_UI_PLAN_PAGE_BEDTIME) title = "就寝时间";
    }
    draw_header(pixels, stride, title,
        model->parent_page == PTC_UI_PARENT_SUPPORT ? "兼容状态、诊断与安全恢复" :
        (model->parent_page == PTC_UI_PARENT_PLAN ? "额度规则与并行就寝计划" : "本地规则与设备安全设置"));
    draw_time_status_bar(pixels, stride, model);
    draw_tabs(pixels, stride, model);
    if (!plan_subpage && model->parent_page != PTC_UI_PARENT_TODAY) {
        actions = actions_for_page(model->parent_page, &action_count);
        if (model->parent_page == PTC_UI_PARENT_PLAN) {
            UiRect quota_zone = {42, 172, 388, 452};
            UiRect parallel_zone = {428, 172, 388, 452};
            fill_round_rect(pixels, stride, quota_zone, 16, UI_RAISED);
            draw_rect_outline(pixels, stride, quota_zone, 16, 1, UI_BORDER);
            UiRect qbadge = {54, 178, 76, 22};
            fill_round_rect(pixels, stride, qbadge, 6, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, qbadge, 6, 1, UI_ACCENT);
            draw_text_center(pixels, stride, qbadge, "额度规则", 12, UI_ACCENT);
            draw_text(pixels, stride, 138, 195, "优先级自上而下逐级生效", 13, UI_MUTED);

            fill_round_rect(pixels, stride, parallel_zone, 16, UI_RAISED);
            draw_rect_outline(pixels, stride, parallel_zone, 16, 1, UI_BORDER);
            UiRect pbadge = {440, 178, 76, 22};
            fill_round_rect(pixels, stride, pbadge, 6, UI_WARNING_SOFT);
            draw_rect_outline(pixels, stride, pbadge, 6, 1, UI_WARNING);
            draw_text_center(pixels, stride, pbadge, "并行补充", 12, UI_WARNING);
            draw_text(pixels, stride, 524, 195, "独立于额度规则链生效", 13, UI_MUTED);

            /* 并行与补充下方对称说明卡片 */
            UiRect info_card = {439, 484, 365, 132};
            fill_round_rect(pixels, stride, info_card, 16, UI_PAGE);
            draw_rect_outline(pixels, stride, info_card, 16, 1, UI_BORDER);
            draw_text(pixels, stride, 457, 508, "规则独立生效机制", 14, UI_INK);
            draw_text(pixels, stride, 457, 534, "• 就寝时间：到点限制，不受额度影响", 11, UI_MUTED);
            draw_text(pixels, stride, 457, 556, "• 自主缓冲：限时日耗尽前由孩子申请", 11, UI_MUTED);
            draw_text(pixels, stride, 457, 578, "• 优先顺序：临时计划 > 节假日 > 周计划", 11, UI_MUTED);
            draw_text(pixels, stride, 457, 600, "• 异常防护：离线运行，断网断电保护", 11, UI_MUTED);
        }
        if (model->parent_page == PTC_UI_PARENT_GRANT) {
            UiRect grant_banner = {54, 484, 750, 144};
            fill_round_rect(pixels, stride, grant_banner, 16, UI_SURFACE);
            draw_rect_outline(pixels, stride, grant_banner, 16, 1, UI_BORDER);
            fill_round_rect(pixels, stride, (UiRect){grant_banner.x + 16, grant_banner.y + 18, 4, grant_banner.height - 36}, 2, UI_ACCENT);
            draw_text(pixels, stride, grant_banner.x + 32, grant_banner.y + 36, "离线加时码使用指南", 18, UI_INK);
            draw_text(pixels, stride, grant_banner.x + 32, grant_banner.y + 68, "• 家长可直接在上方选择快捷时长或自定义分钟生成 8 位加时码", 15, UI_RGB(UI_BLENDED(text_secondary)));
            draw_text(pixels, stride, grant_banner.x + 32, grant_banner.y + 96, "• 兑换后自动计入今日额度；若主机处于离线或外出状态同样支持校验", 15, UI_RGB(UI_BLENDED(text_secondary)));
            draw_text(pixels, stride, grant_banner.x + 32, grant_banner.y + 124, "• 加时码包含防重放与天数校验，当日有效，不可跨日或重复兑换", 14, UI_MUTED);
        }
        for (index = 0; index < action_count; ++index) {
            UiRect card = to_uirect(model->parent_page == PTC_UI_PARENT_SUPPORT
                ? ptc_ui_support_card_rect(index) : (model->parent_page == PTC_UI_PARENT_PLAN
                    ? ptc_ui_plan_card_rect(index) : ptc_ui_parent_card_rect(index)));
            PtcUiActionState astate = PTC_UI_ACTION_AVAILABLE;
            if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
                if (!ptc_ui_safety_action_visible(model, index)) continue;
                astate = ptc_ui_safety_action_available(model, index);
                if (astate != PTC_UI_ACTION_DISABLED)
                    astate = index == ptc_ui_support_recommended_action(model)
                        ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_AVAILABLE;
            }
            const UiAction *action = &actions[index];
            UiAction dynamic_action;
            if (model->parent_page == PTC_UI_PARENT_SUPPORT && model->disable_flag_present && index == 0) {
                action = ptc_ui_runtime_fingerprint_reconfirmation_needed(model)
                    ? &RECONFIRM_ENVIRONMENT_ACTION : &RESUME_CONTROL_ACTION;
            }
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 3) {
                const char *detail = "状态未知，请重新检测";
                dynamic_action = *action;
                if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_OFF) {
                    detail = "当前未开启";
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED) {
                    detail = "当前已开启，按住 X 再按 A 进入";
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_ANOMALY) {
                    detail = "需要处理，请查看详情";
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
                    detail = "外部配置，入口可用";
                }
                /* Use the card's single subtitle row; a second row at the same y overlaps it. */
                dynamic_action.subtitle = detail;
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 0) {
                dynamic_action = *action;
                bool scheduled_active = (strcmp(model->rule_source, "scheduled_override") == 0);
                bool bedtime_enforcing = (model->bedtime_active && !model->bedtime_skipped);
                dynamic_action.subtitle = scheduled_active
                    ? (bedtime_enforcing ? "计划生效中（当前就寝限制中）" : "当前生效中，覆盖每天可玩额度")
                    : (model->scheduled_override.enabled ? "已启用，今日未在计划日期范围内" : "当前关闭");
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 1) {
                dynamic_action = *action;
                bool holiday_active = (strcmp(model->rule_source, "statutory_holiday") == 0 ||
                                       strcmp(model->rule_source, "makeup_workday") == 0);
                bool bedtime_enforcing = (model->bedtime_active && !model->bedtime_skipped);
                dynamic_action.subtitle = holiday_active
                    ? (bedtime_enforcing ? "节假日生效中（当前就寝限制中）" :
                       (strcmp(model->rule_source, "statutory_holiday") == 0
                        ? "当前生效中，国家法定休假日" : "当前生效中，国家调休工作日"))
                    : (model->holiday_enabled ? "已启用，今日非节假日" : "当前关闭，可预设规则");
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 2) {
                dynamic_action = *action;
                bool scheduled_active = (strcmp(model->rule_source, "scheduled_override") == 0);
                bool holiday_active = (strcmp(model->rule_source, "statutory_holiday") == 0 ||
                                       strcmp(model->rule_source, "makeup_workday") == 0);
                bool today_active = (strcmp(model->rule_source, "today_override") == 0);
                bool bedtime_enforcing = (model->bedtime_active && !model->bedtime_skipped);
                if (today_active) {
                    dynamic_action.subtitle = "今日已被临时调整覆盖";
                } else if (scheduled_active) {
                    dynamic_action.subtitle = "今日已被临时额度计划覆盖";
                } else if (holiday_active) {
                    dynamic_action.subtitle = "今日已被国家节假日规则覆盖";
                } else {
                    dynamic_action.subtitle = bedtime_enforcing
                        ? "周额度生效中（当前就寝限制中）" : "当前生效中，周一到周日基础额度";
                }
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 3) {
                dynamic_action = *action;
                bool bedtime_enforcing = (model->bedtime_active && !model->bedtime_skipped);
                dynamic_action.subtitle = model->bedtime_policy.enabled
                    ? (bedtime_enforcing ? "当前限制生效中" : "当前已开启，等待下次窗口")
                    : "当前关闭";
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 4) {
                static char autonomy_detail[64];
                dynamic_action = *action;
                if (model->autonomy_policy.daily_buffer_minutes > 0u) {
                    snprintf(autonomy_detail, sizeof(autonomy_detail), "当前每天 %u 分钟",
                        (unsigned int)model->autonomy_policy.daily_buffer_minutes);
                } else {
                    snprintf(autonomy_detail, sizeof(autonomy_detail), "当前关闭");
                }
                dynamic_action.subtitle = autonomy_detail;
                action = &dynamic_action;
            }
            if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 0) {
                dynamic_action = *action;
                dynamic_action.subtitle = ptc_ui_theme_preference_label(g_theme.preference);
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 2) {
                static char shortcut_detail[112];
                dynamic_action = *action;
                if (model->custom_shortcut_enabled && model->custom_shortcut_label[0]) {
                    snprintf(shortcut_detail, sizeof(shortcut_detail), "当前：%s",
                             model->custom_shortcut_label);
                } else {
                    snprintf(shortcut_detail, sizeof(shortcut_detail), "当前：Minus");
                }
                dynamic_action.subtitle = shortcut_detail;
                action = &dynamic_action;
            }
            /* Status badges live in the card's top-right corner.  The one-line
             * title/status block is vertically centered below that corner, so
             * it can use the full remaining width without being truncated. */
            int reserved_right = 0;
            draw_action_card(pixels, stride, card, action, index == model->selected_index && !model->parent_footer_focused, astate,
                             reserved_right);
            if (model->parent_page == PTC_UI_PARENT_PLAN) {
                bool scheduled_active = (strcmp(model->rule_source, "scheduled_override") == 0);
                bool holiday_active = (strcmp(model->rule_source, "statutory_holiday") == 0 ||
                                       strcmp(model->rule_source, "makeup_workday") == 0);
                bool today_active = (strcmp(model->rule_source, "today_override") == 0);
                bool weekly_active = !scheduled_active && !holiday_active && !today_active;
                bool bedtime_enforcing = (model->bedtime_active && !model->bedtime_skipped);
                bool is_active_rule = (index == 0 && scheduled_active) ||
                                      (index == 1 && holiday_active) ||
                                      (index == 2 && weekly_active) ||
                                      (index == 3 && bedtime_enforcing);

                /* 当前生效规则的高亮描边（非聚焦时呈现） */
                if (is_active_rule && !(index == model->selected_index && !model->parent_footer_focused)) {
                    uint32_t active_border = (index == 3 ? UI_DANGER : (index == 1 ? UI_SUCCESS : UI_ACCENT));
                    draw_rect_outline(pixels, stride, card, 16, 2, active_border);
                }

                UiRect pbadge = {card.x + card.width - 86, card.y + 8, 74, 22};
                if (index == 0) {
                    if (scheduled_active) {
                        fill_round_rect(pixels, stride, pbadge, 6, UI_ACCENT);
                        draw_text_center(pixels, stride, pbadge, "当前生效", 12, UI_ON_ACCENT);
                    } else {
                        fill_round_rect(pixels, stride, pbadge, 6, UI_ACCENT_SOFT);
                        draw_rect_outline(pixels, stride, pbadge, 6, 1, UI_ACCENT);
                        draw_text_center(pixels, stride, pbadge, "优先 1", 12, UI_ACCENT);
                    }
                } else if (index == 1) {
                    if (holiday_active) {
                        fill_round_rect(pixels, stride, pbadge, 6, UI_SUCCESS);
                        draw_text_center(pixels, stride, pbadge, "当前生效", 12, UI_ON_ACCENT);
                    } else {
                        fill_round_rect(pixels, stride, pbadge, 6, UI_SUCCESS_SOFT);
                        draw_rect_outline(pixels, stride, pbadge, 6, 1, UI_SUCCESS);
                        draw_text_center(pixels, stride, pbadge, "优先 2", 12, UI_SUCCESS);
                    }
                } else if (index == 2) {
                    if (weekly_active) {
                        fill_round_rect(pixels, stride, pbadge, 6, UI_ACCENT);
                        draw_text_center(pixels, stride, pbadge, "当前生效", 12, UI_ON_ACCENT);
                    } else {
                        fill_round_rect(pixels, stride, pbadge, 6, UI_PAGE);
                        draw_rect_outline(pixels, stride, pbadge, 6, 1, UI_BORDER);
                        draw_text_center(pixels, stride, pbadge, "基础规则", 12, UI_MUTED);
                    }
                } else if (index == 3) {
                    if (bedtime_enforcing) {
                        fill_round_rect(pixels, stride, pbadge, 6, UI_DANGER);
                        draw_text_center(pixels, stride, pbadge, "限制中", 12, UI_ON_ACCENT);
                    } else {
                        fill_round_rect(pixels, stride, pbadge, 6, UI_WARNING_SOFT);
                        draw_rect_outline(pixels, stride, pbadge, 6, 1, UI_WARNING);
                        draw_text_center(pixels, stride, pbadge, "独立并行", 12, UI_WARNING);
                    }
                } else if (index == 4) {
                    if (model->daily_buffer_claimed) {
                        fill_round_rect(pixels, stride, pbadge, 6, UI_PAGE);
                        draw_rect_outline(pixels, stride, pbadge, 6, 1, UI_BORDER);
                        draw_text_center(pixels, stride, pbadge, "今日已领", 12, UI_MUTED);
                    } else if (model->daily_buffer_available) {
                        fill_round_rect(pixels, stride, pbadge, 6, UI_SUCCESS_SOFT);
                        draw_rect_outline(pixels, stride, pbadge, 6, 1, UI_SUCCESS);
                        draw_text_center(pixels, stride, pbadge, "今日可领", 12, UI_SUCCESS);
                    } else {
                        fill_round_rect(pixels, stride, pbadge, 6, UI_PAGE);
                        draw_rect_outline(pixels, stride, pbadge, 6, 1, UI_BORDER);
                        draw_text_center(pixels, stride, pbadge, "限时追加", 12, UI_MUTED);
                    }
                }
            } else if (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 3) {
                const char *state_label = "状态未知";
                uint32_t state_color = UI_DANGER;
                if (model->album_restriction_state == 0) {
                    state_label = "未开启";
                    state_color = UI_MUTED;
                } else if (model->album_restriction_state == 1) {
                    state_label = "已开启";
                    state_color = UI_SUCCESS;
                } else if (model->album_restriction_state == 2) {
                    state_label = "需要处理";
                    state_color = UI_WARNING;
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
                    state_label = "外部配置";
                    state_color = UI_ACCENT;
                }
                UiRect badge = {card.x + card.width - 104, card.y + 10, 88, 28};
                fill_round_rect(pixels, stride, badge, 6, UI_PAGE);
                draw_text_center(pixels, stride, badge, state_label, 13, state_color);
            }
        }
        if (model->parent_page == PTC_UI_PARENT_PLAN) {
            /* 胶囊 1: 优先于节假日规则 (位于卡片 0 底部 316 与卡片 1 顶部 354 之间，y=325) */
            UiRect pill0 = {54 + 20, 325, 160, 20};
            fill_round_rect(pixels, stride, pill0, 10, UI_PAGE);
            draw_rect_outline(pixels, stride, pill0, 10, 1, UI_BORDER);
            draw_text_center(pixels, stride, pill0, "▼ 优先于节假日规则", 11, UI_ACCENT);

            /* 胶囊 2: 优先于每周常规计划 (位于卡片 1 底部 456 与卡片 2 顶部 494 之间，y=465) */
            UiRect pill1 = {54 + 20, 465, 160, 20};
            fill_round_rect(pixels, stride, pill1, 10, UI_PAGE);
            draw_rect_outline(pixels, stride, pill1, 10, 1, UI_BORDER);
            draw_text_center(pixels, stride, pill1, "▼ 优先于每周常规计划", 11, UI_SUCCESS);
        }
    }
    if (draw_parent_plan_surface(pixels, stride, model)) {
        /* Plan root and editor pages are rendered by ui_render_plan.c. */
    } else if (model->parent_page == PTC_UI_PARENT_TODAY) {
        draw_today_status(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_GRANT) {
        draw_grant_help(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
        draw_safety_status(pixels, stride, model);
    }
    if (model->parent_page == PTC_UI_PARENT_SETTINGS) {
        UiRect help = {842, 176, 384, 452};
        draw_plan_card(pixels, stride, help, false);
        draw_text(pixels, stride, 866, 216, "系统安全与个人偏好", 24, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, 866, 258, "时间规则已集中到时间计划", 16, UI_RGB(UI_BLENDED(text_secondary)));
        draw_text(pixels, stride, 866, 292, "在此统一管理外观、PIN、快捷键", 16, UI_RGB(UI_BLENDED(text_secondary)));
        draw_text(pixels, stride, 866, 326, "自制程序入口与家庭活动记录", 16, UI_RGB(UI_BLENDED(text_secondary)));

        UiRect tip_card = {862, 370, help.width - 40, 170};
        fill_round_rect(pixels, stride, tip_card, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, tip_card, 12, 1, UI_BORDER);
        draw_text(pixels, stride, tip_card.x + 16, tip_card.y + 26, "安全防护提示", 16, UI_INK);
        draw_text(pixels, stride, tip_card.x + 16, tip_card.y + 56, "• 修改 PIN 前需验证原 PIN 确保家长权限", 13, UI_MUTED);
        draw_text(pixels, stride, tip_card.x + 16, tip_card.y + 84, "• 自制程序高级入口仅供进阶玩家便利使用", 13, UI_MUTED);
        draw_text(pixels, stride, tip_card.x + 16, tip_card.y + 112, "• 遇到任何策略或主机异常可直接使用支持与恢复", 13, UI_WARNING);
        draw_text(pixels, stride, tip_card.x + 16, tip_card.y + 140, "• 离开时请按 B 返回孩子页以锁定家长控制", 13, UI_MUTED);

        draw_text(pixels, stride, 866, help.y + help.height - 20, "按 B 返回孩子页  |  Y 刷新设备状态", 13, UI_MUTED);
    }
    draw_settings_badge(pixels, stride, model);
    if (model->parent_page == PTC_UI_PARENT_SUPPORT &&
        model->diagnostic_status != PTC_UI_DIAGNOSTIC_IDLE) {
        draw_diagnostic_notice(pixels, stride, model);
    } else {
        draw_notice(pixels, stride, model);
    }
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(0),
                       plan_subpage ? "" : "L  上一页");
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(1),
                       plan_subpage ? "" : "R  下一页");
    draw_footer_button(pixels, stride,
                       plan_subpage ? ptc_ui_parent_subpage_footer_rect(0) : ptc_ui_parent_footer_rect(2),
                       plan_subpage ? "B  返回计划" : "B  返回孩子页");
    draw_footer_button(pixels, stride,
                       plan_subpage ? ptc_ui_parent_subpage_footer_rect(1) : ptc_ui_parent_footer_rect(3),
                       "Y  刷新");
    if (model->parent_footer_focused && model->parent_footer_selection == 0) {
        draw_rect_outline(pixels, stride, to_uirect(
            plan_subpage ? ptc_ui_parent_subpage_footer_rect(1) : ptc_ui_parent_footer_rect(3)),
            12, 3, UI_ACCENT);
    }
    draw_parent_status_footer(pixels, stride, model);
}
