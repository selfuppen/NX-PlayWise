#include "ui_render_internal.h"

static const char *rule_source_label(const char *source);

static const UiAction TODAY_ACTIONS[] = {
    {"今日额度", "当前由规则计划决定", UI_ACCENT, UI_ACTION_ICON_CLOCK, UI_ACTION_VISUAL_NONE},
    {"快速加时", "", UI_SUCCESS, UI_ACTION_ICON_ADD_TIME, UI_ACTION_VISUAL_QUICK_ADD},
    {"今日不限时", "今天不设时间上限", UI_SUCCESS, UI_ACTION_ICON_INFINITY, UI_ACTION_VISUAL_NONE},
    {"清除今日调整", "恢复下级额度规则", UI_MUTED, UI_ACTION_ICON_RESTORE, UI_ACTION_VISUAL_NONE},
    {"就寝窗口", "当前关闭", UI_WARNING, UI_ACTION_ICON_MOON, UI_ACTION_VISUAL_NONE},
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
void home_button(uint32_t *pixels, uint32_t stride, PtcUiRect target,
    const char *label, bool primary, bool selected, bool disabled)
{
    UiRect box = to_uirect(target);
    uint32_t fill = disabled ? UI_RAISED : (primary ? UI_ACCENT : UI_ACCENT_SOFT);
    fill_round_rect(pixels, stride, box, 12, fill);
    if (selected) draw_focus_ring(pixels, stride, box, 12);
    draw_button_label(pixels, stride, box, label, target.h <= 48 ? 18 : 22,
        disabled ? UI_DISABLED : (primary ? UI_ON_ACCENT : UI_ACCENT));
}

static float child_remaining_fraction(const PtcUiModel *model, bool *available)
{
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    *available = false;
    if (!fresh) return 0.0f;
    if (model->bedtime_active && !model->bedtime_skipped) {
        *available = true;
        return 0.0f;
    }
    if (model->unrestricted_today == 1) {
        *available = true;
        return 1.0f;
    }
    if (model->remaining_available && model->played_minutes_available &&
        model->remaining_minutes >= 0 && model->played_minutes >= 0 &&
        model->remaining_minutes + model->played_minutes > 0) {
        *available = true;
        return (float)model->displayed_remaining_minutes /
               (float)(model->remaining_minutes + model->played_minutes);
    }
    if (model->remaining_available && model->remaining_minutes > 0) {
        int shown = model->displayed_remaining_minutes > 120 ? 120 : model->displayed_remaining_minutes;
        if (shown < 0) shown = 0;
        *available = true;
        return (float)shown / 120.0f;
    }
    return 0.0f;
}

static void draw_child_calendar_icon(uint32_t *pixels, uint32_t stride, int cx, int cy, uint32_t color)
{
    draw_rect_outline(pixels, stride, (UiRect){cx - 9, cy - 8, 18, 16}, 3, 2, color);
    draw_line(pixels, stride, cx - 9, cy - 2, cx + 9, cy - 2, 2, color);
    draw_line(pixels, stride, cx - 4, cy - 10, cx - 4, cy - 7, 2, color);
    draw_line(pixels, stride, cx + 4, cy - 10, cx + 4, cy - 7, 2, color);
}

static void draw_child_day_card(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                                UiRect card, const char *title, int forecast_index)
{
    char value[48];
    char source[64];
    char fitted[64];
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    bool available = fresh && model->forecast_available && forecast_index >= 0 && forecast_index < 7 &&
        model->forecast[forecast_index].day_index == model->day_index + forecast_index;
    fill_round_rect(pixels, stride, card, 12, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, card, 12, 1, UI_RGB(UI_BLENDED(border_control)));
    draw_child_calendar_icon(pixels, stride, card.x + 20, card.y + 22, UI_ACCENT);
    draw_text(pixels, stride, card.x + 38, card.y + 28, title, 16, UI_MUTED);
    if (available && forecast_index == 0 && model->unrestricted_today == 1) {
        snprintf(value, sizeof(value), "不限时");
        snprintf(source, sizeof(source), "%s", rule_source_label(model->rule_source));
    } else if (available) {
        const PtcResultForecastDay *day = &model->forecast[forecast_index];
        if (day->mode == PTC_RULE_MODE_UNLIMITED) snprintf(value, sizeof(value), "不限时");
        else snprintf(value, sizeof(value), "%u 分钟", (unsigned int)day->minutes);
        snprintf(source, sizeof(source), "%s", rule_source_label(day->rule_source));
    } else {
        snprintf(value, sizeof(value), "%s", fresh ? "暂不可用" : "待确认");
        snprintf(source, sizeof(source), "%s", fresh ? "尚未取得安排" : "状态需要刷新");
    }
    draw_text(pixels, stride, card.x + 16, card.y + 68, value, 25,
              available ? UI_INK : UI_MUTED);
    fit_text(fitted, sizeof(fitted), source, 13, card.width - 32);
    draw_text(pixels, stride, card.x + 16, card.y + 98, fitted, 13,
              available ? UI_ACCENT : UI_MUTED);
}

static void draw_child_budget_bar(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, UiRect box)
{
    char left[64];
    char right[64];
    UiRect slot = {box.x + 28, box.y + 384, box.width - 56, 10};
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    fill_round_rect(pixels, stride, slot, 5, UI_GAUGE_SLOT);
    draw_rect_outline(pixels, stride, slot, 5, 1, UI_GAUGE_SLOT_BORDER);
    if (bedtime_enforcing) {
        snprintf(left, sizeof(left), "就寝限制生效中");
        snprintf(right, sizeof(right), "今天暂停游玩");
    } else if (!fresh) {
        snprintf(left, sizeof(left), "今天的用时待确认");
        snprintf(right, sizeof(right), "刷新后显示");
    } else if (model->unrestricted_today == 1) {
        fill_round_rect(pixels, stride, slot, 5, UI_SUCCESS);
        snprintf(left, sizeof(left), "今天不限时");
        snprintf(right, sizeof(right), "合理安排休息");
    } else if (model->remaining_available && model->played_minutes_available &&
               model->remaining_minutes >= 0 && model->played_minutes >= 0 &&
               model->remaining_minutes + model->played_minutes > 0) {
        int total = model->remaining_minutes + model->played_minutes;
        int fill_w = (int)((int64_t)slot.width * model->displayed_remaining_minutes / total);
        uint32_t color = model->remaining_minutes <= 10 ? UI_DANGER :
            (model->remaining_minutes <= 30 ? UI_WARNING : UI_SUCCESS);
        if (fill_w < 4 && model->remaining_minutes > 0) fill_w = 4;
        if (fill_w > slot.width) fill_w = slot.width;
        if (fill_w > 0) fill_round_rect(pixels, stride, (UiRect){slot.x, slot.y, fill_w, slot.height}, 5, color);
        snprintf(left, sizeof(left), "已玩约 %d 分钟", model->played_minutes);
        snprintf(right, sizeof(right), "还剩 %d 分钟", model->remaining_minutes);
    } else {
        snprintf(left, sizeof(left), "今天的用时暂不可用");
        snprintf(right, sizeof(right), "稍后再刷新");
    }
    draw_text(pixels, stride, slot.x, slot.y - 14, "今天的时间", 15, UI_MUTED);
    draw_text(pixels, stride, slot.x, slot.y + 34, left, 14,
              bedtime_enforcing ? UI_DANGER : UI_MUTED);
    draw_text(pixels, stride, slot.x + slot.width - measure_text(right, 14), slot.y + 34, right, 14,
              bedtime_enforcing ? UI_DANGER : UI_MUTED);
}

static void draw_child_task_summary(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect box = to_uirect(ptc_ui_home_summary_rect(false));
    UiRect today_card = {box.x + 28, box.y + 218, 250, 122};
    UiRect tomorrow_card = {box.x + 290, box.y + 218, 262, 122};
    char remaining[64];
    char today[64];
    char line[160];
    char age[64];
    bool ring_available;
    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;
    float fraction = child_remaining_fraction(model, &ring_available);
    uint32_t ring_color = UI_SUCCESS;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    if (bedtime_enforcing || (model->remaining_available && model->remaining_minutes <= 10)) ring_color = UI_DANGER;
    else if (model->remaining_available && model->remaining_minutes <= 30) ring_color = UI_WARNING;

    ptc_ui_format_home_remaining(model, ptc_ui_render_now(), remaining, sizeof(remaining));
    ptc_ui_format_today_mode(model, today, sizeof(today));
    draw_card_shadow(pixels, stride, box, 16);
    fill_round_rect(pixels, stride, box, 16, UI_RGB(UI_BLENDED(hero)));
    draw_text(pixels, stride, box.x + 28, box.y + 42,
              bedtime_enforcing ? "今天还可玩（就寝立断）" : "今天还可玩",
              22, UI_RGB(UI_BLENDED(hero_secondary)));
    {
        char *unit = strstr(remaining, " 分钟");
        if (unit) {
            *unit = '\0';
            int number_width = measure_text(remaining, 72);
            draw_text_bold(pixels, stride, box.x + 28, box.y + 132, remaining, 72,
                           UI_RGB(UI_BLENDED(on_hero)));
            draw_text(pixels, stride, box.x + 40 + number_width, box.y + 130, "分钟", 22,
                      UI_RGB(UI_BLENDED(hero_secondary)));
        } else {
            draw_wrapped_text(pixels, stride, box.x + 28, box.y + 120, remaining, 38,
                              300, 44, 2, UI_RGB(UI_BLENDED(on_hero)));
        }
    }
    draw_ring_progress(pixels, stride, box.x + box.width - 78, box.y + 92, 48, 9,
                       ring_available ? fraction : 0.0f,
                       UI_RGB(ui_mix_rgb(UI_BLENDED(hero), 0xFFFFFF, 20)), ring_color);
    draw_circle_outline(pixels, stride, box.x + box.width - 78, box.y + 92, 17, 2,
                        UI_RGB(UI_BLENDED(hero_secondary)));
    draw_line(pixels, stride, box.x + box.width - 78, box.y + 92,
              box.x + box.width - 78, box.y + 82, 2, UI_RGB(UI_BLENDED(hero_secondary)));
    draw_line(pixels, stride, box.x + box.width - 78, box.y + 92,
              box.x + box.width - 70, box.y + 96, 2, UI_RGB(UI_BLENDED(hero_secondary)));
    if (bedtime_enforcing) {
        snprintf(line, sizeof(line), "就寝时间生效中  /  %s",
                 model->status_loaded ? rule_source_label(model->rule_source) : "待确认规则");
    } else {
        snprintf(line, sizeof(line), "今日%s  /  %s", today,
                 model->status_loaded ? rule_source_label(model->rule_source) : "待确认规则");
    }
    draw_text(pixels, stride, box.x + 28, box.y + 176, line, 17,
              UI_RGB(UI_BLENDED(hero_secondary)));

    fill_round_rect(pixels, stride, (UiRect){box.x + 12, box.y + 200, box.width - 24, box.height - 212},
                    16, UI_SURFACE);
    draw_child_day_card(pixels, stride, model, today_card, "今天安排", 0);
    draw_child_day_card(pixels, stride, model, tomorrow_card, "明天安排", 1);
    draw_child_budget_bar(pixels, stride, model, box);
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, box.x + 28, box.y + box.height - 24, age, 13, UI_MUTED);
}

static void draw_home_summary(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, bool parent)
{
    if (!parent) {
        draw_child_task_summary(pixels, stride, model);
        return;
    }
    UiRect box = to_uirect(ptc_ui_home_summary_rect(parent));
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
            model->status_loaded ? rule_source_label(model->rule_source) : "待确认规则");
    } else {
        snprintf(line, sizeof(line), "今日%s  /  %s", today,
            model->status_loaded ? rule_source_label(model->rule_source) : "待确认规则");
    }
    draw_text(pixels, stride, x, box.y + 176, line, 18, UI_RGB(UI_BLENDED(hero_secondary)));
    /* Supporting information sits on a separate surface, below the hero. */
    UiRect lower = {box.x + 12, box.y + 200, box.width - 24, box.height - 212};
    fill_round_rect(pixels, stride, lower, 16, UI_SURFACE);
    if (parent) {
        ptc_ui_format_home_total(model, line, sizeof(line));
        draw_text(pixels, stride, x, box.y + 236, line, 22, UI_INK);
        if (model->played_minutes_available && model->played_minutes >= 0)
            snprintf(line, sizeof(line), "额度消耗估算  约 %d 分钟", model->played_minutes);
        else snprintf(line, sizeof(line), "额度消耗估算  暂不可用");
        draw_text(pixels, stride, x, box.y + 272, line, 18, UI_MUTED);
    } else {
        draw_text(pixels, stride, x, box.y + 246, "明天的安排", 20, UI_MUTED);
        if (model->forecast_available) {
            const PtcResultForecastDay *day = &model->forecast[1];
            if (day->mode == PTC_RULE_MODE_UNLIMITED) snprintf(line, sizeof(line), "不限时");
            else snprintf(line, sizeof(line), "%u 分钟", (unsigned int)day->minutes);
            draw_text(pixels, stride, x, box.y + 292, line, 30, UI_INK);
            draw_text(pixels, stride, x + 200, box.y + 292,
                rule_source_label(day->rule_source), 18, UI_MUTED);
        } else draw_text(pixels, stride, x, box.y + 292, "安排暂不可用", 24, UI_MUTED);
    }
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, x, box.y + box.height - 26, age, 16, UI_MUTED);
}

static void draw_child_action_icon(uint32_t *pixels, uint32_t stride, PtcUiRect target,
                                   int kind, bool primary, bool disabled)
{
    int cx = target.x + 34;
    int cy = target.y + target.h / 2;
    uint32_t color = disabled ? UI_DISABLED : (primary ? UI_ON_ACCENT : UI_ACCENT);
    uint32_t background = disabled ? UI_PAGE : (primary ? UI_RGB(ui_mix_rgb(UI_BLENDED(accent), 0xFFFFFF, 28)) : UI_SURFACE);
    fill_round_rect(pixels, stride, (UiRect){cx - 18, cy - 18, 36, 36}, 12, background);
    if (kind == 0) {
        draw_rect_outline(pixels, stride, (UiRect){cx - 10, cy - 8, 20, 16}, 4, 2, color);
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 4; ++column)
                fill_round_rect(pixels, stride, (UiRect){cx - 7 + column * 5, cy - 5 + row * 7, 3, 3}, 1, color);
    } else if (kind == 1) {
        draw_circle_outline(pixels, stride, cx - 2, cy - 1, 9, 2, color);
        draw_line(pixels, stride, cx - 7, cy + 7, cx + 8, cy - 8, 2, color);
        draw_line(pixels, stride, cx - 2, cy + 2, cx - 8, cy - 2, 2, color);
    } else {
        draw_child_calendar_icon(pixels, stride, cx, cy, color);
    }
}

static void draw_child_status_card(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect card = {684, 492, 516, 108};
    const char *runtime = ptc_ui_runtime_notice_summary(model);
    bool feedback = ptc_ui_operation_feedback_visible(model);
    bool error = strcmp(model->result_status, "error") == 0;
    bool alert = runtime[0] || feedback || ptc_ui_home_notice_expanded(model);
    uint32_t accent = (error || model->disable_flag_present) ? UI_DANGER :
        (model->waiting ? UI_WARNING : (alert ? UI_WARNING : UI_SUCCESS));
    uint32_t background = (error || model->disable_flag_present) ? UI_DANGER_SOFT :
        (model->waiting ? UI_WARNING_SOFT : UI_RGB(UI_BLENDED(surface_raised)));
    char age[64];
    const char *title = alert ? (runtime[0] ? runtime : (error ? "操作未完成" :
        (model->waiting ? "正在同步" : "状态提示"))) : "状态自动同步";
    const char *detail = "今天的安排会在后台保持更新";
    if (alert) {
        if (model->message[0] && strcmp(title, model->message) != 0) detail = model->message;
        else if (model->feedback_detail[0]) detail = model->feedback_detail;
        else if (model->waiting) detail = "请稍候，完成后即可继续操作";
        else detail = "需要帮助时，请家长查看支持与恢复";
    }
    fill_round_rect(pixels, stride, card, 12, background);
    draw_rect_outline(pixels, stride, card, 12, 1, alert ? accent : UI_BORDER);
    draw_status_symbol(pixels, stride, card.x + 18, card.y + 20, accent,
                       error ? 3 : (model->waiting ? 2 : 1));
    draw_wrapped_text(pixels, stride, card.x + 48, card.y + 28, title, 15,
                      card.width - 70, 19, 2, accent);
    draw_wrapped_text(pixels, stride, card.x + 48, card.y + 63, detail, 13,
                      card.width - 70, 18, 2, UI_MUTED);
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, card.x + card.width - 18 - measure_text(age, 12),
              card.y + card.height - 10, age, 12, UI_MUTED);
}

void draw_child(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char buffer[128], hint[160], fitted_hint[160];
    bool disabled = model->disable_flag_present || model->waiting;
    draw_header(pixels, stride, "自律即自由", "合理安排时间，做自己时间的主人");
    draw_time_status_bar(pixels, stride, model);
    draw_home_summary(pixels, stride, model, false);
    draw_card_shadow(pixels, stride, (UiRect){652, 120, 580, 496}, 16);
    fill_round_rect(pixels, stride, (UiRect){652, 120, 580, 496}, 16, UI_RGB(UI_BLENDED(surface)));
    draw_text(pixels, stride, 684, 164, "今天可以做什么？", 28, UI_RGB(UI_BLENDED(text_primary)));
    draw_text(pixels, stride, 684, 195, "需要更多时间时，从这里开始", 18, UI_RGB(UI_BLENDED(text_secondary)));
    home_button(pixels, stride, ptc_ui_child_submit_rect(),
        model->disable_flag_present ? "兑换暂不可用" : "A  输入加时码", true, false, disabled);
    draw_child_action_icon(pixels, stride, ptc_ui_child_submit_rect(), 0, true, disabled);
    if (model->daily_buffer_available)
        snprintf(buffer, sizeof(buffer), "X  领取自主缓冲  +%u 分钟", (unsigned int)model->daily_buffer_minutes);
    else snprintf(buffer, sizeof(buffer), "%s", model->daily_buffer_claimed ? "今日已使用缓冲" :
        (model->daily_buffer_minutes == 0 ? "今日自主缓冲未开启" : "自主缓冲仅可在限时日领取"));
    home_button(pixels, stride, ptc_ui_child_buffer_rect(), buffer, false, false,
        disabled || !model->daily_buffer_available);
    draw_child_action_icon(pixels, stride, ptc_ui_child_buffer_rect(), 1, false,
                           disabled || !model->daily_buffer_available);
    home_button(pixels, stride, ptc_ui_home_details_rect(false), "+  使用详情", false, false, model->waiting);
    draw_child_action_icon(pixels, stride, ptc_ui_home_details_rect(false), 2, false, model->waiting);
    draw_child_status_card(pixels, stride, model);
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_footer_rect(0)), "A  输入加时码", 18, disabled ? UI_DISABLED : UI_MUTED);
    if (model->show_parent_shortcut_hint && model->custom_shortcut_enabled)
        ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label, hint, sizeof(hint));
    else snprintf(hint, sizeof(hint), "状态会在后台自动同步");
    fit_text(fitted_hint, sizeof(fitted_hint), hint, 18, ptc_ui_child_footer_rect(1).w - 24);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_child_footer_rect(1)), fitted_hint, 18, UI_RGB(UI_BLENDED(text_secondary)));
    draw_footer_button(pixels, stride, ptc_ui_child_footer_rect(2), "B  退出");
    draw_button_label(pixels, stride, to_uirect(ptc_ui_child_refresh_rect()), "Y  刷新", 18, model->waiting ? UI_DISABLED : UI_MUTED);
}

void draw_setup(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {54, 120, 1172, 500};
    const char *phase = model->setup_phase[0] ? model->setup_phase : "pending";
    int64_t grace_remaining = ptc_ui_setup_grace_remaining(model, ptc_ui_render_now());
    char title[64];
    char phase_line[192];
    char countdown_line[80];
    char fitted[220];
    int step = model->setup_step > 0 ? model->setup_step : PTC_UI_SETUP_SHORTCUT;
    snprintf(title, sizeof(title), "首次设置  |  %d/5", step);
    draw_header(pixels, stride, grace_remaining >= 0 ? "正在同步" : title,
        grace_remaining >= 0 ? "系统设置正在同步，完成后继续选择进入的区域" : "按步骤完成 任我玩 的家长设置");
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    for (int i = 0; i < 5; ++i)
        fill_round_rect(pixels, stride, (UiRect){104 + i * 212, 167, 188, 5}, 2,
            i < step ? UI_ACCENT : UI_RAISED);
    if (grace_remaining >= 0) {
        draw_text(pixels, stride, 204, 190, "环境检查已通过", 31, UI_SUCCESS);
        snprintf(phase_line, sizeof(phase_line), "当前状态：正在同步    安装前快照：%s",
                 model->setup_snapshot_available ? "已保存" : "不可用");
        draw_text(pixels, stride, 204, 248, phase_line, 22, UI_MUTED);
        if (grace_remaining > 0) {
            snprintf(countdown_line, sizeof(countdown_line), "系统设置同步中（约 %lld 秒）...", (long long)grace_remaining);
        } else {
            snprintf(countdown_line, sizeof(countdown_line), "同步完成，正在启用额度管理...");
        }
        draw_text(pixels, stride, 204, 310, countdown_line, 34, UI_ACCENT);
        draw_text(pixels, stride, 204, 356, "无需操作；同步完成后会进入第 5 步选择区域。", 22, UI_INK);
    } else {
        draw_text(pixels, stride, 104, 154, "1 快捷键", 16, step == PTC_UI_SETUP_SHORTCUT ? UI_ACCENT : UI_MUTED);
        draw_text(pixels, stride, 286, 154, "2 PIN", 16, step == PTC_UI_SETUP_PIN ? UI_ACCENT : UI_MUTED);
        draw_text(pixels, stride, 452, 154, "3 外观主题", 16, step == PTC_UI_SETUP_THEME ? UI_ACCENT : UI_MUTED);
        draw_text(pixels, stride, 708, 154, "4 启用管理", 16, step == PTC_UI_SETUP_TAKEOVER ? UI_ACCENT : UI_MUTED);
        draw_text(pixels, stride, 912, 154, "5 进入区域", 16, step == PTC_UI_SETUP_ZONE ? UI_ACCENT : UI_MUTED);
        if (step == PTC_UI_SETUP_SHORTCUT) {
            UiRect compact_fixed = {204, 184, 872, 54};
            fill_round_rect(pixels, stride, compact_fixed, 12, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, compact_fixed, 12, 2, UI_ACCENT);
            draw_text(pixels, stride, 232, 218, "固定入口 Minus：松开即可进入，无需长按", 20, UI_ACCENT);
            draw_text(pixels, stride, 204, 264, "自定义组合需长按约 400ms；按 A 加入草稿，按 + 确认后生效", 18, UI_MUTED);
            for (int index = 0; index < PTC_UI_SHORTCUT_PRESET_COUNT; ++index) {
                UiRect card = to_uirect(ptc_ui_setup_shortcut_card_rect(index));
                bool selected = index == model->setup_shortcut_index;
                fill_round_rect(pixels, stride, card, 16, selected ? UI_ACCENT_SOFT : UI_RAISED);
                draw_rect_outline(pixels, stride, card, 16, selected ? 2 : 1, selected ? UI_ACCENT : UI_CONTROL);
                draw_text_center(pixels, stride, card,
                                 ptc_ui_shortcut_common_label(index), 16,
                                 selected ? UI_ACCENT : UI_INK);
            }
            draw_text(pixels, stride, 204, 554,
                      model->shortcut_draft_enabled ? "待确认自定义组合（需长按）：" : "待确认状态：仅保留 Minus（松开进入）",
                      17, UI_MUTED);
            if (model->shortcut_draft_enabled) {
                fit_text(fitted, sizeof(fitted), model->shortcut_draft_label, 18, 250);
                draw_text(pixels, stride, 420, 554, fitted, 18, UI_ACCENT);
            }
        } else if (step == PTC_UI_SETUP_PIN) {
            draw_text(pixels, stride, 204, 220, "任我玩 PIN 已设置", 30, UI_INK);
            draw_text(pixels, stride, 204, 262, "全新安装默认 PIN：110", 24, UI_WARNING);
            draw_text(pixels, stride, 204, 294, "默认值属于弱保护；可继续使用，也可现在修改为 1到64 位数字。", 20, UI_MUTED);
            draw_dialog_button(pixels, stride, ptc_ui_setup_pin_rect(), "X / 点击  修改 PIN",
                               UI_ACCENT, UI_ON_ACCENT, false);
            draw_text(pixels, stride, 204, 420, "此 PIN 用于进入家长区，与 Nintendo 官方家长控制 PIN 不同。", 19, UI_MUTED);
        } else if (step == PTC_UI_SETUP_TAKEOVER) {
            bool resuming_restored_setup = model->disable_flag_present && strcmp(phase, "restored") == 0;
            bool reconfirming_environment = ptc_ui_runtime_fingerprint_reconfirmation_needed(model);
            bool takeover_complete = ptc_ui_setup_takeover_complete(model);
            draw_text(pixels, stride, 204, 218,
                      takeover_complete ? "系统控制接管已完成" :
                      (reconfirming_environment ? "系统环境已变化" :
                       (resuming_restored_setup ? "解除停用并重新接管" : "确认接管系统控制")),
                      30, takeover_complete ? UI_SUCCESS : UI_INK);
            snprintf(phase_line, sizeof(phase_line), "当前状态：%s    安装前快照：%s",
                     takeover_complete ? (strcmp(phase, "active") == 0 ? "正常运行" : "正在同步") :
                     (strcmp(phase, "protection") == 0 ? "保护模式" :
                     (strcmp(phase, "failed") == 0 ? "检查失败" :
                      (resuming_restored_setup ? "已恢复并停用" : "等待家长确认"))),
                     model->setup_snapshot_available ? "已保存" : "待保存");
            draw_text(pixels, stride, 204, 266, phase_line, 21, UI_MUTED);
            draw_text(pixels, stride, 204, 324,
                      takeover_complete
                          ? "游玩时间管理已启用，可以继续选择进入的区域。"
                          : reconfirming_environment
                           ? "系统版本或运行环境与上次确认时不同，需要家长重新确认兼容性。"
                          : resuming_restored_setup
                           ? "确认后先检查主机；通过后恢复游玩时间管理。"
                           : "确认后先检查主机是否支持，再启用游玩时间管理。",
                      21, UI_INK);
            draw_text(pixels, stride, 204, 360,
                      takeover_complete
                          ? "按 A 或点击继续，进入第 5 步选择区域。"
                          : reconfirming_environment
                           ? "检查通过后保留现有计划，恢复游玩时间管理。"
                           : resuming_restored_setup
                            ? "保留现有计划；需要撤销时可到“支持与恢复”操作。"
                            : "首次接管会原样保留今天的总额度和剩余时间，不会先临时解限。",
                      21, UI_INK);
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(),
                               takeover_complete ? "A / 点击  继续到第 5 步" :
                               (reconfirming_environment ? "A / 点击  重新检测并接管" :
                                (resuming_restored_setup ? "A / 点击  解除停用并重新接管" : "A / 点击  确认接管")),
                               takeover_complete ? UI_SUCCESS : UI_ACCENT,
                               UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_THEME) {
            static const char *LABELS[] = {"跟随系统", "浅色", "暗色"};
            static const char *DETAILS[] = {"随 Switch 设置", "经典浅色外观", "柔和的深色背景"};
            draw_text(pixels, stride, 204, 220, "选择外观主题", 30, UI_INK);
            draw_text(pixels, stride, 204, 252, "默认跟随系统；只改变主机应用外观，不影响计时和后台控制。", 18, UI_MUTED);
            for (int index = 0; index < 3; ++index) {
                UiRect option = to_uirect(ptc_ui_setup_theme_rect(index));
                bool selected = index == model->setup_theme_index;
                fill_round_rect(pixels, stride, option, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
                draw_rect_outline(pixels, stride, option, 12, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
                draw_text_center(pixels, stride, (UiRect){option.x, option.y + 22, option.width, 36},
                                 LABELS[index], 23, UI_INK);
                draw_text_center(pixels, stride, (UiRect){option.x, option.y + 70, option.width, 28},
                                 DETAILS[index], 16, UI_MUTED);
            }
            draw_text(pixels, stride, 204, 448, "左右选择  |  A / + 保存并继续", 18, UI_ACCENT);
        } else {
            draw_text(pixels, stride, 204, 214, "初始化完成，选择进入区域", 30, UI_INK);
            draw_text(pixels, stride, 204, 254, "之后可在两个区域之间切换；进入家长区会受 PIN 保护。", 21, UI_MUTED);
            for (int index = 0; index < 2; ++index) {
                UiRect card = to_uirect(ptc_ui_setup_zone_rect(index));
                bool selected = index == model->setup_zone_index;
                fill_round_rect(pixels, stride, card, 16, selected ? UI_ACCENT_SOFT : UI_RAISED);
                draw_rect_outline(pixels, stride, card, 16, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 26, card.width, 34},
                                 index == 0 ? "孩子区" : "家长区", 27,
                                 selected ? UI_ACCENT : UI_INK);
                if (index == 0) {
                    char shortcut_hint[160];
                    char fitted_shortcut_hint[160];
                    ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label,
                                                       shortcut_hint, sizeof(shortcut_hint));
                    fit_text(fitted_shortcut_hint, sizeof(fitted_shortcut_hint), shortcut_hint, 17, card.width - 36);
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 86, card.width - 36, 26},
                                     model->show_parent_shortcut_hint && model->custom_shortcut_enabled
                                        ? fitted_shortcut_hint : "家长区快捷提示未显示", 17, UI_MUTED);
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     "家长区需要输入 任我玩 PIN", 17, UI_MUTED);
                } else {
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 86, card.width - 36, 26},
                                     "固定 Minus：松开进入，无需长按", 18, UI_MUTED);
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     model->custom_shortcut_enabled ? "自定义组合：长按约 400ms" : "未启用自定义组合", 17, UI_MUTED);
                }
            }
        }
        if (model->message[0] && step != PTC_UI_SETUP_SHORTCUT) {
            fit_text(fitted, sizeof(fitted), model->message, 18, 1160);
            draw_text(pixels, stride, 64, 530, fitted, 18, UI_MUTED);
        }
        if (model->feedback_detail[0]) {
            fit_text(fitted, sizeof(fitted), model->feedback_detail, 17, 1160);
            draw_text(pixels, stride, 64, 552, fitted, 17, UI_DANGER);
        }
    }
    if (grace_remaining < 0) {
        draw_dialog_button(pixels, stride, ptc_ui_setup_back_rect(), "B  返回上一步",
                           UI_RAISED, UI_INK, true);
        if (step == PTC_UI_SETUP_SHORTCUT) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), "+  确认快捷键并继续",
                               UI_ACCENT, UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_ZONE) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(),
                               model->setup_zone_index == 1 ? "A  确认进入家长区" : "A  确认进入孩子区",
                               UI_ACCENT, UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_PIN) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), "A  继续使用当前 PIN",
                               UI_ACCENT, UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_THEME) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), "A  保存主题并继续",
                               UI_ACCENT, UI_ON_ACCENT, false);
        }
    }
}

void draw_error(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {214, 148, 852, 444};
    char fitted[192];
    char execution[150];
    draw_header(pixels, stride, "操作未完成", "请查看错误信息后重试或返回");
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    fill_round_rect(pixels, stride, (UiRect){254, 194, 64, 64}, 16, UI_DANGER);
    draw_text_center(pixels, stride, (UiRect){254, 194, 64, 64}, "!", 34, UI_ON_ACCENT);
    draw_text(pixels, stride, 342, 214, "加时码处理失败", 28, UI_INK);
    snprintf(
        execution,
        sizeof(execution),
        "命令：%s    %s",
        model->command_name[0] ? model->command_name : "未开始",
        model->transport_label[0] ? model->transport_label : "传输：未开始");
    fit_text(fitted, sizeof(fitted), execution, 18, 756);
    draw_text(pixels, stride, 254, 286, fitted, 18, UI_MUTED);
    fit_text(fitted, sizeof(fitted), model->message, 23, 756);
    draw_text(pixels, stride, 254, 342, fitted, 23, UI_MUTED);
    if (model->feedback_detail[0]) {
        fit_text(fitted, sizeof(fitted), model->feedback_detail, 17, 756);
        draw_text(pixels, stride, 254, 390, fitted, 17, UI_DANGER);
    }

    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_retry_rect()), 12, UI_ACCENT);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_retry_rect()),
                    model->error_code == 306 ? "A  重新检测" : "A  重新输入", 25, UI_ON_ACCENT);
    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 12, UI_RAISED);
    draw_rect_outline(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 12, 1, UI_CONTROL);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_back_rect()), "B  返回主页", 25, UI_INK);
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

static void draw_card_action_icon(uint32_t *pixels, uint32_t stride, int cx, int cy,
                                  UiActionIcon icon, uint32_t color)
{
    switch (icon) {
    case UI_ACTION_ICON_CLOCK:
        draw_circle_outline(pixels, stride, cx, cy, 10, 2, color);
        draw_line(pixels, stride, cx, cy, cx, cy - 5, 2, color);
        draw_line(pixels, stride, cx, cy, cx + 4, cy + 1, 2, color);
        break;
    case UI_ACTION_ICON_ADD_TIME:
        draw_circle_outline(pixels, stride, cx, cy, 10, 2, color);
        draw_line(pixels, stride, cx - 5, cy, cx + 5, cy, 2, color);
        draw_line(pixels, stride, cx, cy - 5, cx, cy + 5, 2, color);
        break;
    case UI_ACTION_ICON_INFINITY:
        draw_circle_outline(pixels, stride, cx - 5, cy, 6, 2, color);
        draw_circle_outline(pixels, stride, cx + 5, cy, 6, 2, color);
        break;
    case UI_ACTION_ICON_RESTORE:
        draw_line(pixels, stride, cx - 8, cy - 5, cx - 3, cy - 9, 2, color);
        draw_line(pixels, stride, cx - 8, cy - 5, cx - 2, cy - 2, 2, color);
        draw_line(pixels, stride, cx - 7, cy - 5, cx + 4, cy - 5, 2, color);
        draw_line(pixels, stride, cx + 4, cy - 5, cx + 9, cy, 2, color);
        draw_line(pixels, stride, cx + 9, cy, cx + 4, cy + 7, 2, color);
        draw_line(pixels, stride, cx + 4, cy + 7, cx - 5, cy + 7, 2, color);
        break;
    case UI_ACTION_ICON_MOON:
        draw_line(pixels, stride, cx - 1, cy - 10, cx - 7, cy - 5, 2, color);
        draw_line(pixels, stride, cx - 7, cy - 5, cx - 7, cy + 3, 2, color);
        draw_line(pixels, stride, cx - 7, cy + 3, cx - 2, cy + 8, 2, color);
        draw_line(pixels, stride, cx - 2, cy + 8, cx + 6, cy + 7, 2, color);
        draw_line(pixels, stride, cx + 6, cy + 7, cx + 10, cy + 3, 2, color);
        draw_line(pixels, stride, cx + 10, cy + 3, cx + 3, cy + 3, 2, color);
        draw_line(pixels, stride, cx + 3, cy + 3, cx - 1, cy - 10, 2, color);
        break;
    case UI_ACTION_ICON_BUFFER:
        draw_circle_outline(pixels, stride, cx, cy, 9, 2, color);
        draw_line(pixels, stride, cx, cy - 5, cx, cy + 5, 2, color);
        draw_line(pixels, stride, cx - 5, cy, cx + 5, cy, 2, color);
        draw_line(pixels, stride, cx - 12, cy, cx - 9, cy, 2, color);
        draw_line(pixels, stride, cx + 9, cy, cx + 12, cy, 2, color);
        break;
    case UI_ACTION_ICON_CALENDAR_RANGE:
    case UI_ACTION_ICON_HOLIDAY:
    case UI_ACTION_ICON_WEEKLY:
        draw_rect_outline(pixels, stride, (UiRect){cx - 9, cy - 8, 18, 16}, 3, 2, color);
        draw_line(pixels, stride, cx - 9, cy - 2, cx + 9, cy - 2, 2, color);
        draw_line(pixels, stride, cx - 4, cy - 10, cx - 4, cy - 7, 2, color);
        draw_line(pixels, stride, cx + 4, cy - 10, cx + 4, cy - 7, 2, color);
        if (icon == UI_ACTION_ICON_CALENDAR_RANGE) {
            draw_line(pixels, stride, cx - 5, cy + 3, cx + 5, cy + 3, 2, color);
            draw_line(pixels, stride, cx - 5, cy + 3, cx - 2, cy + 1, 2, color);
            draw_line(pixels, stride, cx + 5, cy + 3, cx + 2, cy + 5, 2, color);
        } else if (icon == UI_ACTION_ICON_HOLIDAY) {
            draw_line(pixels, stride, cx, cy, cx, cy + 6, 2, color);
            draw_line(pixels, stride, cx - 5, cy + 3, cx + 5, cy + 3, 2, color);
        } else {
            fill_rect(pixels, stride, (UiRect){cx - 5, cy + 1, 3, 3}, color);
            fill_rect(pixels, stride, (UiRect){cx - 1, cy + 1, 3, 3}, color);
            fill_rect(pixels, stride, (UiRect){cx + 3, cy + 1, 3, 3}, color);
        }
        break;
    case UI_ACTION_ICON_KEY:
        draw_circle_outline(pixels, stride, cx - 4, cy - 4, 6, 2, color);
        draw_line(pixels, stride, cx, cy, cx + 7, cy + 7, 2, color);
        draw_line(pixels, stride, cx + 4, cy + 4, cx + 7, cy + 1, 2, color);
        break;
    case UI_ACTION_ICON_DEVICE:
        draw_rect_outline(pixels, stride, (UiRect){cx - 8, cy - 9, 16, 18}, 3, 2, color);
        draw_line(pixels, stride, cx - 3, cy + 4, cx + 3, cy + 4, 2, color);
        fill_rect(pixels, stride, (UiRect){cx - 1, cy - 6, 2, 2}, color);
        break;
    case UI_ACTION_ICON_CONSOLE:
        draw_rect_outline(pixels, stride, (UiRect){cx - 11, cy - 7, 22, 14}, 5, 2, color);
        draw_line(pixels, stride, cx - 7, cy, cx - 1, cy, 2, color);
        draw_line(pixels, stride, cx - 4, cy - 3, cx - 4, cy + 3, 2, color);
        fill_rect(pixels, stride, (UiRect){cx + 4, cy - 3, 3, 3}, color);
        fill_rect(pixels, stride, (UiRect){cx + 7, cy + 1, 3, 3}, color);
        break;
    case UI_ACTION_ICON_SLIDERS:
        draw_line(pixels, stride, cx - 10, cy - 6, cx + 10, cy - 6, 2, color);
        draw_line(pixels, stride, cx - 10, cy, cx + 10, cy, 2, color);
        draw_line(pixels, stride, cx - 10, cy + 6, cx + 10, cy + 6, 2, color);
        draw_circle_outline(pixels, stride, cx - 4, cy - 6, 2, 2, color);
        draw_circle_outline(pixels, stride, cx + 5, cy, 2, 2, color);
        draw_circle_outline(pixels, stride, cx - 1, cy + 6, 2, 2, color);
        break;
    case UI_ACTION_ICON_HISTORY:
        draw_circle_outline(pixels, stride, cx, cy, 9, 2, color);
        draw_line(pixels, stride, cx, cy, cx, cy - 5, 2, color);
        draw_line(pixels, stride, cx, cy, cx + 5, cy, 2, color);
        draw_line(pixels, stride, cx - 12, cy - 6, cx - 8, cy - 9, 2, color);
        break;
    case UI_ACTION_ICON_THEME:
        draw_circle_outline(pixels, stride, cx, cy, 9, 2, color);
        fill_rect(pixels, stride, (UiRect){cx - 4, cy - 4, 3, 3}, color);
        fill_rect(pixels, stride, (UiRect){cx + 1, cy - 3, 3, 3}, color);
        fill_rect(pixels, stride, (UiRect){cx - 2, cy + 2, 3, 3}, color);
        break;
    case UI_ACTION_ICON_CONTROLLER:
        draw_rect_outline(pixels, stride, (UiRect){cx - 10, cy - 6, 20, 13}, 4, 2, color);
        draw_line(pixels, stride, cx - 5, cy, cx - 1, cy, 2, color);
        draw_line(pixels, stride, cx - 3, cy - 2, cx - 3, cy + 2, 2, color);
        fill_rect(pixels, stride, (UiRect){cx + 3, cy - 2, 2, 2}, color);
        fill_rect(pixels, stride, (UiRect){cx + 5, cy + 1, 2, 2}, color);
        break;
    case UI_ACTION_ICON_HOMEBREW:
        draw_rect_outline(pixels, stride, (UiRect){cx - 10, cy - 9, 20, 18}, 3, 2, color);
        fill_rect(pixels, stride, (UiRect){cx - 6, cy - 5, 4, 4}, color);
        fill_rect(pixels, stride, (UiRect){cx + 2, cy - 5, 4, 4}, color);
        fill_rect(pixels, stride, (UiRect){cx - 6, cy + 2, 4, 4}, color);
        fill_rect(pixels, stride, (UiRect){cx + 2, cy + 2, 4, 4}, color);
        break;
    case UI_ACTION_ICON_ACTIVITY:
        draw_line(pixels, stride, cx - 10, cy + 8, cx + 10, cy + 8, 2, color);
        fill_rect(pixels, stride, (UiRect){cx - 8, cy + 1, 4, 7}, color);
        fill_rect(pixels, stride, (UiRect){cx - 2, cy - 5, 4, 13}, color);
        fill_rect(pixels, stride, (UiRect){cx + 4, cy - 1, 4, 9}, color);
        break;
    case UI_ACTION_ICON_SHIELD:
        draw_line(pixels, stride, cx - 8, cy - 8, cx + 8, cy - 8, 2, color);
        draw_line(pixels, stride, cx - 8, cy - 8, cx - 8, cy, 2, color);
        draw_line(pixels, stride, cx - 8, cy, cx, cy + 9, 2, color);
        draw_line(pixels, stride, cx + 8, cy - 8, cx + 8, cy, 2, color);
        draw_line(pixels, stride, cx + 8, cy, cx, cy + 9, 2, color);
        draw_line(pixels, stride, cx - 3, cy, cx - 1, cy + 3, 2, color);
        draw_line(pixels, stride, cx - 1, cy + 3, cx + 4, cy - 3, 2, color);
        break;
    case UI_ACTION_ICON_REPAIR:
        draw_circle_outline(pixels, stride, cx - 5, cy - 5, 5, 2, color);
        draw_line(pixels, stride, cx - 1, cy - 1, cx + 8, cy + 8, 3, color);
        draw_line(pixels, stride, cx + 5, cy + 8, cx + 8, cy + 5, 2, color);
        break;
    case UI_ACTION_ICON_STOP:
        draw_rect_outline(pixels, stride, (UiRect){cx - 9, cy - 9, 18, 18}, 5, 2, color);
        draw_line(pixels, stride, cx - 5, cy - 5, cx + 5, cy + 5, 3, color);
        break;
    case UI_ACTION_ICON_EXPORT:
        draw_rect_outline(pixels, stride, (UiRect){cx - 9, cy - 3, 18, 12}, 3, 2, color);
        draw_line(pixels, stride, cx, cy - 10, cx, cy + 3, 2, color);
        draw_line(pixels, stride, cx - 5, cy - 5, cx, cy - 10, 2, color);
        draw_line(pixels, stride, cx + 5, cy - 5, cx, cy - 10, 2, color);
        break;
    case UI_ACTION_ICON_INFO:
        draw_circle_outline(pixels, stride, cx, cy, 10, 2, color);
        fill_rect(pixels, stride, (UiRect){cx - 1, cy - 5, 3, 3}, color);
        fill_rect(pixels, stride, (UiRect){cx - 1, cy, 3, 7}, color);
        break;
    default:
        draw_circle_outline(pixels, stride, cx, cy, 7, 2, color);
        break;
    }
}

static void draw_action_visual(uint32_t *pixels, uint32_t stride, UiRect area,
                               const UiAction *action, bool disabled)
{
    uint32_t ink = disabled ? UI_DISABLED : action->accent;
    if (action->visual == UI_ACTION_VISUAL_QUICK_ADD) {
        static const char *LABELS[] = {"+15", "+30", "+60", "自定义"};
        int gap = 4;
        int width = (area.width - gap * 3) / 4;
        for (int index = 0; index < 4; ++index) {
            UiRect chip = {area.x + index * (width + gap), area.y, width, area.height};
            fill_round_rect(pixels, stride, chip, 6, disabled ? UI_PAGE : UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, chip, 6, 1, disabled ? UI_DISABLED : UI_SUCCESS);
            draw_text_center(pixels, stride, chip, LABELS[index], index == 3 ? 11 : 13, ink);
        }
    } else if (action->visual == UI_ACTION_VISUAL_THEME) {
        static const char *LABELS[] = {"系统", "浅色", "暗色"};
        const char *selected = action->subtitle ? action->subtitle : "";
        int gap = 5;
        int width = (area.width - gap * 2) / 3;
        for (int index = 0; index < 3; ++index) {
            bool active = strstr(selected, LABELS[index]) != NULL;
            UiRect chip = {area.x + index * (width + gap), area.y, width, area.height};
            fill_round_rect(pixels, stride, chip, 6, active ? UI_ACCENT_SOFT : UI_RAISED);
            draw_rect_outline(pixels, stride, chip, 6, active ? 2 : 1,
                              disabled ? UI_DISABLED : (active ? UI_ACCENT : UI_BORDER));
            draw_text_center(pixels, stride, chip, LABELS[index], 12,
                             disabled ? UI_DISABLED : (active ? UI_ACCENT : UI_MUTED));
        }
    }
}

void draw_action_card(uint32_t *pixels, uint32_t stride, UiRect rect,
    const UiAction *action, bool selected, PtcUiActionState state, int reserved_right)
{
    bool disabled = state == PTC_UI_ACTION_DISABLED;
    bool recommended = state == PTC_UI_ACTION_RECOMMENDED;
    bool compact = rect.height < 90;
    int badge_size = compact ? 34 : 44;
    int title_size = compact ? 20 : 22;
    int sub_size = compact ? 15 : 17;
    int text_x = rect.x + 18 + badge_size + 16;
    int content_width = rect.width - (text_x - rect.x) - (recommended ? 64 : 16) - reserved_right;
    if (content_width < 100) content_width = 100;

    uint32_t background = disabled ? UI_RAISED : (selected ? UI_ACCENT_SOFT : UI_SURFACE);
    draw_card_shadow(pixels, stride, rect, 16);
    fill_round_rect(pixels, stride, rect, 16, background);
    if (selected) {
        draw_focus_ring(pixels, stride, rect, 16);
    } else {
        draw_rect_outline(pixels, stride, rect, 16, 1, UI_BORDER);
    }

    bool has_sub = action->subtitle && action->subtitle[0];
    bool has_visual = action->visual != UI_ACTION_VISUAL_NONE;
    int text_block_h = title_size + ((has_sub || has_visual) ? (compact ? 31 : 36) : 0);
    int text_top = rect.y + (rect.height - text_block_h) / 2;
    if (text_top < rect.y + 12) text_top = rect.y + 12;
    int icon_cx = rect.x + 18 + badge_size / 2;
    int icon_cy = text_top + text_block_h / 2;
    UiRect badge_rect = {icon_cx - badge_size / 2, icon_cy - badge_size / 2, badge_size, badge_size};
    uint32_t badge_bg = disabled ? UI_PAGE :
        (action->accent == UI_SUCCESS ? UI_SUCCESS_SOFT :
        (action->accent == UI_DANGER ? UI_DANGER_SOFT :
        (action->accent == UI_WARNING ? UI_WARNING_SOFT : UI_ACCENT_SOFT)));
    fill_round_rect(pixels, stride, badge_rect, 12, badge_bg);
    draw_rect_outline(pixels, stride, badge_rect, 12, 1, UI_BORDER);
    draw_card_action_icon(pixels, stride, icon_cx, icon_cy, action->icon,
                          disabled ? UI_DISABLED : action->accent);

    char fitted[128];
    fit_text(fitted, sizeof(fitted), action->title, title_size, content_width);
    draw_text(pixels, stride, text_x, text_top + title_size, fitted, title_size,
              disabled ? UI_DISABLED : UI_INK);

    if (has_visual) {
        draw_action_visual(pixels, stride,
            (UiRect){text_x, text_top + title_size + 8, content_width, compact ? 24 : 28},
            action, disabled);
    } else if (has_sub) {
        fit_text(fitted, sizeof(fitted), action->subtitle, sub_size, content_width);
        draw_text(pixels, stride, text_x, text_top + title_size + sub_size + 7, fitted, sub_size,
                  disabled ? UI_DISABLED : UI_MUTED);
    }

    if (recommended && !disabled) {
        fill_round_rect(pixels, stride, (UiRect){rect.x + rect.width - 66, rect.y + 8, 56, 24}, 6, UI_SUCCESS);
        draw_text_center(pixels, stride, (UiRect){rect.x + rect.width - 66, rect.y + 8, 56, 24}, "建议", 16, UI_ON_ACCENT);
    }
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
    draw_home_summary(pixels, stride, model, true);
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
                snprintf(dynamic, sizeof(dynamic), "%02u:%02u 至次日 %02u:%02u，%s",
                    (unsigned int)(model->bedtime_start_minute / 60),
                    (unsigned int)(model->bedtime_start_minute % 60),
                    (unsigned int)(model->bedtime_end_minute / 60),
                    (unsigned int)(model->bedtime_end_minute % 60),
                    model->bedtime_skipped ? "已跳过" : "生效中");
                subtitle = dynamic;
            } else if (model->bedtime_skipped_window_available) {
                snprintf(dynamic, sizeof(dynamic), "%02u:%02u 至次日 %02u:%02u，已跳过",
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
                (strcmp(adjustment_badge, "控制停用") == 0 || strcmp(adjustment_badge, "恢复中") == 0
                    ? UI_DANGER :
                 (strcmp(adjustment_badge, "等待生效") == 0 || strcmp(adjustment_badge, "待确认") == 0
                    ? UI_WARNING : UI_MUTED));
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

void format_duration(int minutes, char *out, size_t out_size)
{
    if (minutes < 0) {
        snprintf(out, out_size, "暂不可用");
        return;
    }
    if (minutes < 60) {
        snprintf(out, out_size, "%d 分钟", minutes);
    } else if (minutes % 60 == 0) {
        snprintf(out, out_size, "%d 小时", minutes / 60);
    } else {
        snprintf(out, out_size, "%d 小时 %d 分钟", minutes / 60, minutes % 60);
    }
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

uint32_t time_state_accent(bool available, bool unlimited, int minutes)
{
    if (!available) return UI_WARNING;
    if (unlimited) return UI_SUCCESS;
    if (minutes <= 0) return UI_DANGER;
    if (minutes <= 15) return UI_WARNING;
    return UI_SUCCESS;
}

void draw_time_state_card(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    const char *label,
    const char *value,
    uint32_t accent)
{
    int label_size = 18;
    int value_size = 23;
    while (label_size > 16 && measure_text(label, label_size) > rect.width - 16) --label_size;
    while (value_size > 17 && measure_text(value, value_size) > rect.width - 16) --value_size;
    fill_round_rect(pixels, stride, rect, 16, UI_RGB(UI_BLENDED(surface_raised)));
    draw_text_center(pixels, stride, (UiRect){rect.x + 8, rect.y + 10, rect.width - 16, 26}, label, label_size, UI_MUTED);
    draw_text_center(pixels, stride, (UiRect){rect.x + 8, rect.y + 38, rect.width - 16, 38}, value, value_size, accent);
}

void format_status_age(const PtcUiModel *model, char *out, size_t out_size)
{
    ptc_ui_format_status_age(model, ptc_ui_render_now(), out, out_size);
}

uint32_t status_age_color(const PtcUiModel *model)
{
    if (model->waiting) return UI_RGB(UI_BLENDED(warning));
    return UI_RGB(ptc_ui_status_is_fresh(model, ptc_ui_render_now())
        ? UI_BLENDED(text_secondary) : UI_BLENDED(warning));
}

static const char *rule_source_label(const char *source)
{
    if (!source || !source[0]) return "尚未刷新";
    if (strcmp(source, "today_override") == 0) return "今日额度调整";
    if (strcmp(source, "scheduled_override") == 0) return "临时额度计划";
    if (strcmp(source, "statutory_holiday") == 0) return "国家法定休假日";
    if (strcmp(source, "makeup_workday") == 0) return "国家调休工作日";
    return "周计划";
}

void draw_plan_card(uint32_t *pixels, uint32_t stride, UiRect card, bool focused)
{
    draw_card_shadow(pixels, stride, card, 16);
    fill_round_rect(pixels, stride, card, 16, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, card, 16, focused ? 3 : 1, UI_RGB(focused ? UI_BLENDED(focus) : UI_BLENDED(border_control)));
}

void draw_plan_impact(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                      PtcUiPlanKind kind, bool dirty, UiRect panel)
{
    char impact[256], age[80], title[64], left_value[40], right_value[48];
    char left_note[64], right_note[64], source_text[80];
    PtcUiPlanImpactProjection projection;
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    int played = fresh && model->played_minutes_available ? model->played_minutes : -1;
    bool requires_hold = dirty && ptc_ui_plan_save_requires_hold(model, kind, ptc_ui_render_now());
    bool error = strcmp(model->result_status, "error") == 0;
    const char *state_message = NULL;
    uint32_t state_color = UI_MUTED;
    uint32_t state_background = UI_RGB(UI_BLENDED(surface_raised));
    int changed = 0;
    ptc_ui_project_plan_impact(model, kind, dirty, ptc_ui_render_now(), &projection);

    if (kind == PTC_UI_PLAN_WEEKLY) {
        for (int d = 0; d < 7; ++d)
            if (ptc_ui_day_rule_effectively_changed(model->current_week[d], model->draft_week[d])) ++changed;
    } else if (kind == PTC_UI_PLAN_HOLIDAY) {
        if (model->holiday_enabled != model->draft_holiday_enabled) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->holiday_rule, model->draft_holiday_rule)) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->makeup_workday_rule, model->draft_makeup_workday_rule)) ++changed;
    }

    draw_plan_card(pixels, stride, panel, false);
    if (dirty) {
        if (changed > 0)
            snprintf(title, sizeof(title), kind == PTC_UI_PLAN_WEEKLY ? "修改草稿 | 已调整 %d 天" : "修改草稿 | 已调整 %d 项", changed);
        else snprintf(title, sizeof(title), "修改草稿 | 待保存");
    } else snprintf(title, sizeof(title), "计划已保存 | 正常生效");
    draw_text(pixels, stride, panel.x + 20, panel.y + 34, title, 19,
              dirty ? (requires_hold ? UI_DANGER : UI_WARNING) : UI_SUCCESS);
    const char *sub = model->disable_flag_present ? "控制已停用，计划只读" :
        (model->waiting ? "正在保存中，请稍候..." :
         (model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR ? "按 + 完成输入，再保存计划" :
          (requires_hold ? (fresh ? "保存后今日额度将耗尽；长按保存" : "今日状态待确认；长按保存") :
           (dirty ? "按 + 保存后应用到主机" : "已与主机策略同步"))));
    draw_text(pixels, stride, panel.x + 20, panel.y + 58, sub, 13,
              requires_hold ? UI_DANGER : UI_RGB(UI_BLENDED(text_secondary)));

    UiRect result = {panel.x + 20, panel.y + 74, panel.width - 40, 96};
    UiRect left = {result.x + 10, result.y + 8, 138, result.height - 16};
    UiRect right = {result.x + result.width - 148, result.y + 8, 138, result.height - 16};
    fill_round_rect(pixels, stride, result, 12, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, result, 12, 1, UI_RGB(UI_BLENDED(border_control)));

    if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
        draw_status_symbol(pixels, stride, result.x + 24, result.y + 48, UI_MUTED, 1);
        draw_text(pixels, stride, result.x + 44, result.y + 30, "今天额度不变", 16, UI_INK);
        draw_wrapped_text(pixels, stride, result.x + 44, result.y + 54, impact,
                          12, result.width - 58, 17, 2, UI_MUTED);
    } else {
        bool current_state = projection.state == PTC_UI_PLAN_IMPACT_CURRENT;
        PtcDayRule left_rule = current_state ? projection.after.rule : projection.before.rule;
        PtcDayRule right_rule = projection.after.rule;
        draw_text_center(pixels, stride, (UiRect){left.x, left.y, left.width, 22},
                         current_state ? "今日额度" : "当前额度", 13, UI_MUTED);
        if (left_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(left_value, sizeof(left_value), "不限时");
        else snprintf(left_value, sizeof(left_value), "%u 分钟", (unsigned int)left_rule.minutes);
        snprintf(left_note, sizeof(left_note), "%s",
                 ptc_ui_effective_rule_label(current_state ? projection.after.source : projection.before.source));
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 24, left.width, 30}, left_value, 19,
                         left_rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT);
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 55, left.width, 20}, left_note, 11, UI_MUTED);

        if (!current_state) {
            int arrow_x = result.x + result.width / 2;
            int arrow_y = result.y + 40;
            draw_line(pixels, stride, arrow_x - 13, arrow_y, arrow_x + 10, arrow_y, 2, UI_MUTED);
            draw_line(pixels, stride, arrow_x + 5, arrow_y - 5, arrow_x + 10, arrow_y, 2, UI_MUTED);
            draw_line(pixels, stride, arrow_x + 5, arrow_y + 5, arrow_x + 10, arrow_y, 2, UI_MUTED);
        }

        if (current_state) {
            draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 22}, "今天还可玩", 13, UI_MUTED);
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "不限时");
            else if (projection.remaining_available) snprintf(right_value, sizeof(right_value), "%d 分钟", projection.remaining_minutes);
            else snprintf(right_value, sizeof(right_value), "待确认");
            if (played >= 0) snprintf(right_note, sizeof(right_note), "已用约 %d 分钟", played);
            else snprintf(right_note, sizeof(right_note), "刷新后显示");
        } else {
            draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 22}, "保存后额度", 13, UI_MUTED);
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "不限时");
            else snprintf(right_value, sizeof(right_value), "%u 分钟", (unsigned int)right_rule.minutes);
            if (right_rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_note, sizeof(right_note), "预计还可玩：不限时");
            else if (projection.remaining_available) snprintf(right_note, sizeof(right_note), "预计还可玩 %d 分钟", projection.remaining_minutes);
            else snprintf(right_note, sizeof(right_note), "剩余待刷新确认");
        }
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 24, right.width, 30}, right_value, 19,
                         right_rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS :
                         (projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER :
                          (projection.state == PTC_UI_PLAN_IMPACT_UNKNOWN ? UI_MUTED : UI_SUCCESS)));
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 55, right.width, 20}, right_note, 11,
                         projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_MUTED);
    }

    snprintf(source_text, sizeof(source_text), "%s  %s", dirty ? "保存后来源" : "今天来源",
             ptc_ui_effective_rule_label(projection.after.source));
    {
        int source_width = measure_text(source_text, 12) + 24;
        if (source_width > panel.width - 40) source_width = panel.width - 40;
        UiRect source = {panel.x + 20, panel.y + 184, source_width, 26};
        fill_round_rect(pixels, stride, source, 13, UI_ACCENT_SOFT);
        draw_text_center(pixels, stride, source, source_text, 12, UI_ACCENT);
    }

    if (error) {
        state_message = "保存失败，草稿已保留；检查后可重新保存。";
        state_color = UI_DANGER; state_background = UI_DANGER_SOFT;
    } else if (model->disable_flag_present) {
        state_message = "紧急停用中，计划保持只读。";
        state_color = UI_DANGER; state_background = UI_DANGER_SOFT;
    } else if (model->waiting) {
        state_message = "正在保存草稿，请稍候。";
        state_color = UI_WARNING; state_background = UI_WARNING_SOFT;
    } else if (model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
        state_message = "先完成额度输入，再返回保存计划。";
        state_color = UI_WARNING; state_background = UI_WARNING_SOFT;
    } else if (!fresh && projection.quota_changes_today) {
        state_message = requires_hold ? "状态待确认；保存时会再次核对今天的影响，请长按保存。" :
            "状态待确认，保存时会再次核对今天的影响。";
        state_color = UI_WARNING; state_background = UI_WARNING_SOFT;
    } else if (requires_hold) {
        state_message = "保存后今天的时间将立即用尽，请长按保存。";
        state_color = UI_DANGER; state_background = UI_DANGER_SOFT;
    }

    UiRect conclusion = {panel.x + 20, panel.y + 222, panel.width - 40, 58};
    if (state_message) {
        fill_round_rect(pixels, stride, conclusion, 10, state_background);
        draw_wrapped_text(pixels, stride, conclusion.x + 12, conclusion.y + 24, state_message,
                          13, conclusion.width - 24, 18, 2, state_color);
    } else if (projection.state == PTC_UI_PLAN_IMPACT_CURRENT) {
        draw_text(pixels, stride, conclusion.x, conclusion.y + 16, "当前状态", 13, UI_MUTED);
        draw_text(pixels, stride, conclusion.x, conclusion.y + 39,
                  "计划已保存，没有等待应用的额度草稿。", 13, UI_RGB(UI_BLENDED(text_secondary)));
    } else if (projection.state != PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        draw_text(pixels, stride, conclusion.x, conclusion.y + 16, "保存后的今天", 13, UI_MUTED);
        ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
        draw_wrapped_text(pixels, stride, conclusion.x, conclusion.y + 39, impact,
                          13, conclusion.width, 18, 2, UI_RGB(UI_BLENDED(text_secondary)));
    }

    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, panel.x + 20, panel.y + panel.height - 20,
              age, 13, status_age_color(model));
}

void draw_plan_impact_compact(uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
                              PtcUiPlanKind kind, UiRect panel)
{
    char title[64], impact[256], left_value[40], right_value[48], source_text[80];
    PtcUiPlanImpactProjection projection;
    int changed = 0;
    ptc_ui_project_plan_impact(model, kind, true, ptc_ui_render_now(), &projection);

    if (kind == PTC_UI_PLAN_WEEKLY) {
        for (int day = 0; day < 7; ++day) {
            if (ptc_ui_day_rule_effectively_changed(model->current_week[day], model->draft_week[day])) ++changed;
        }
        if (changed > 0) snprintf(title, sizeof(title), "修改草稿 | 已调整 %d 天", changed);
        else snprintf(title, sizeof(title), "修改草稿 | 待保存");
    } else {
        if (model->holiday_enabled != model->draft_holiday_enabled) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->holiday_rule, model->draft_holiday_rule)) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->makeup_workday_rule, model->draft_makeup_workday_rule)) ++changed;
        if (changed > 0) snprintf(title, sizeof(title), "修改草稿 | 已调整 %d 项", changed);
        else snprintf(title, sizeof(title), "修改草稿 | 待保存");
    }

    draw_plan_card(pixels, stride, panel, false);
    draw_text(pixels, stride, panel.x + 20, panel.y + 32, title, 18, UI_WARNING);

    UiRect result = {panel.x + 20, panel.y + 50, panel.width - 40, 104};
    UiRect left = {result.x + 8, result.y + 8, 132, result.height - 16};
    UiRect right = {result.x + result.width - 140, result.y + 8, 132, result.height - 16};
    fill_round_rect(pixels, stride, result, 12, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, result, 12, 1, UI_RGB(UI_BLENDED(border_control)));
    if (projection.state == PTC_UI_PLAN_IMPACT_NO_TODAY_CHANGE) {
        ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
        draw_status_symbol(pixels, stride, result.x + 24, result.y + 52, UI_MUTED, 1);
        draw_text(pixels, stride, result.x + 44, result.y + 34, "今天额度不变", 15, UI_INK);
        draw_wrapped_text(pixels, stride, result.x + 44, result.y + 58, impact,
                          12, result.width - 56, 17, 2, UI_MUTED);
    } else {
        draw_text_center(pixels, stride, (UiRect){left.x, left.y, left.width, 22}, "当前额度", 12, UI_MUTED);
        if (projection.before.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(left_value, sizeof(left_value), "不限时");
        else snprintf(left_value, sizeof(left_value), "%u 分钟", (unsigned int)projection.before.rule.minutes);
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 24, left.width, 34}, left_value, 19,
                         projection.before.rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT);
        draw_text_center(pixels, stride, (UiRect){left.x, left.y + 61, left.width, 18},
                         ptc_ui_effective_rule_label(projection.before.source), 11, UI_MUTED);

        int arrow_x = result.x + result.width / 2;
        int arrow_y = result.y + 42;
        draw_line(pixels, stride, arrow_x - 11, arrow_y, arrow_x + 9, arrow_y, 2, UI_MUTED);
        draw_line(pixels, stride, arrow_x + 4, arrow_y - 5, arrow_x + 9, arrow_y, 2, UI_MUTED);
        draw_line(pixels, stride, arrow_x + 4, arrow_y + 5, arrow_x + 9, arrow_y, 2, UI_MUTED);

        draw_text_center(pixels, stride, (UiRect){right.x, right.y, right.width, 22}, "保存后额度", 12, UI_MUTED);
        if (projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "不限时");
        else snprintf(right_value, sizeof(right_value), "%u 分钟", (unsigned int)projection.after.rule.minutes);
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 24, right.width, 34}, right_value, 19,
                         projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS :
                         (projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_ACCENT));
        if (projection.after.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(right_value, sizeof(right_value), "预计还可玩：不限时");
        else if (projection.remaining_available) snprintf(right_value, sizeof(right_value), "预计还可玩 %d 分钟", projection.remaining_minutes);
        else snprintf(right_value, sizeof(right_value), "剩余待刷新确认");
        draw_text_center(pixels, stride, (UiRect){right.x, right.y + 61, right.width, 18}, right_value, 11,
                         projection.state == PTC_UI_PLAN_IMPACT_EXHAUSTED ? UI_DANGER : UI_MUTED);
    }

    snprintf(source_text, sizeof(source_text), "保存后来源  %s", ptc_ui_effective_rule_label(projection.after.source));
    {
        int source_width = measure_text(source_text, 12) + 24;
        if (source_width > panel.width - 40) source_width = panel.width - 40;
        UiRect source = {panel.x + 20, panel.y + 174, source_width, 26};
        fill_round_rect(pixels, stride, source, 13, UI_ACCENT_SOFT);
        draw_text_center(pixels, stride, source, source_text, 12, UI_ACCENT);
    }
}

static void draw_weekly_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    int slot;
    char detail[64];
    char freshness[64];
    uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
    format_status_age(model, freshness, sizeof(freshness));
    draw_rect_outline(pixels, stride, (UiRect){54, 178, 26, 24}, 4, 2, UI_ACCENT);
    draw_line(pixels, stride, 54, 186, 80, 186, 2, UI_ACCENT);
    draw_line(pixels, stride, 61, 174, 61, 181, 3, UI_ACCENT);
    draw_line(pixels, stride, 73, 174, 73, 181, 3, UI_ACCENT);
    draw_text(pixels, stride, 92, 198, "周一到周日基础规划  |  点按模式或额度区直接修改", 17, UI_MUTED);
    for (slot = 0; slot < 7; ++slot) {
        int day = ptc_ui_weekday_for_display_slot(slot);
        bool selected = slot == model->weekly_grid_slot && model->selected_index == 0;
        bool today = model->status_loaded && day == weekday;
        UiRect card = to_uirect(ptc_ui_weekly_day_rect(slot));
        UiRect header = to_uirect(ptc_ui_weekly_day_header_rect(slot));
        UiRect mode = to_uirect(ptc_ui_weekly_day_mode_rect(slot));
        UiRect minutes = to_uirect(ptc_ui_weekly_day_minutes_rect(slot));
        bool limited = model->draft_week[day].mode == PTC_RULE_MODE_LIMIT;
        draw_plan_card(pixels, stride, card, selected && !model->parent_footer_focused);
        draw_text_center(pixels, stride, header, DAYS[day], 19, UI_INK);
        fill_round_rect(pixels, stride, (UiRect){mode.x + 6, mode.y + 6, mode.width - 12, 32}, 14,
                        model->disable_flag_present ? UI_BORDER :
                        (limited ? UI_ACCENT : UI_SUCCESS));
        draw_text_center(pixels, stride, (UiRect){mode.x + 6, mode.y + 6, mode.width - 12, 32},
                         limited ? "限时" : "不限时", 15,
                         model->disable_flag_present ? UI_DISABLED : UI_ON_ACCENT);
        UiRect today_rect = {card.x, minutes.y + 8, card.width, 22};
        draw_text_center(pixels, stride, today_rect,
                         today ? "● 今天" : " ", 13, today ? UI_SUCCESS : UI_MUTED);
        if (limited) {
            snprintf(detail, sizeof(detail), "%u", (unsigned int)model->draft_week[day].minutes);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 36, minutes.width, 46}, detail, 30,
                             model->disable_flag_present ? UI_DISABLED : UI_ACCENT);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 84, minutes.width, 24}, "分钟", 14, UI_MUTED);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 124, minutes.width, 22}, "A / 点按修改", 12,
                             model->disable_flag_present ? UI_DISABLED : UI_MUTED);
        } else {
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 50, minutes.width, 36}, "不限时间", 18,
                             model->disable_flag_present ? UI_DISABLED : UI_SUCCESS);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 94, minutes.width, 24}, "全天无限制", 13, UI_MUTED);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 124, minutes.width, 22}, "点按看提示", 11, UI_DISABLED);
        }
        /* 每日容量柱状直方图 (Weekly Capacity Histogram Bar) */
        int bar_w = card.width - 24;
        int bar_x = card.x + 12;
        int bar_y = card.y + card.height - 14;
        UiRect bar_bg = {bar_x, bar_y, bar_w, 5};
        fill_round_rect(pixels, stride, bar_bg, 2, UI_BORDER);
        if (limited) {
            int fill_w = (int)((int64_t)bar_w * (model->draft_week[day].minutes > 180 ? 180 : model->draft_week[day].minutes) / 180);
            if (fill_w < 4 && model->draft_week[day].minutes > 0) fill_w = 4;
            uint32_t bar_col = model->disable_flag_present ? UI_DISABLED :
                (model->draft_week[day].minutes <= 30 ? UI_WARNING : UI_ACCENT);
            if (fill_w > 0) {
                fill_round_rect(pixels, stride, (UiRect){bar_x, bar_y, fill_w, 5}, 2, bar_col);
            }
        } else {
            fill_round_rect(pixels, stride, bar_bg, 2, model->disable_flag_present ? UI_DISABLED : UI_SUCCESS);
        }
    }
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_WEEKLY, model->weekly_dirty,
                     (UiRect){838, 176, 388, 452});
    draw_candidate_button(pixels, stride, ptc_ui_weekly_page_mode_rect(), "X  切换模式",
                           UI_PAGE, UI_ACCENT, model->selected_index == 1,
                           model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_bulk_rect(), "批量设置",
                          UI_PAGE, UI_ACCENT, model->selected_index == 2,
                          model->disable_flag_present);
    draw_candidate_button(pixels, stride, ptc_ui_weekly_discard_rect(), "ZL  放弃",
                           UI_PAGE, UI_INK, model->selected_index == 3,
                           !model->weekly_dirty);
    bool weekly_hold = model->weekly_dirty && ptc_ui_plan_save_requires_hold(model, PTC_UI_PLAN_WEEKLY, ptc_ui_render_now());
    draw_candidate_button(pixels, stride, ptc_ui_weekly_save_rect(),
                           model->disable_flag_present ? "只读" : (model->waiting ? "保存中" : (model->weekly_dirty ? (weekly_hold ? "长按 + 保存" : "+  保存草稿") : "已保存")),
                           weekly_hold ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT, model->selected_index == 4,
                           !model->weekly_dirty || model->disable_flag_present || model->waiting);
}

void draw_toggle_switch(
    uint32_t *pixels,
    uint32_t stride,
    UiRect rect,
    bool is_on,
    bool selected,
    bool disabled,
    const char *on_label,
    const char *off_label)
{
    int radius = rect.height / 2;
    uint32_t bg_color = disabled ? UI_BORDER :
                       (is_on ? UI_SUCCESS : UI_BORDER);
    uint32_t knob_color = disabled ? UI_RAISED : UI_SURFACE;
    int knob_size = rect.height - 6;
    int off_x = rect.x + 3;
    int on_x = rect.x + rect.width - 3 - knob_size;
    /* 滑块按切换时刻缓动；首帧或预览直接落位（单调用点，矩形即身份）。 */
    static UiRect memo_rect = {0, 0, 0, 0};
    static bool memo_is_on = false;
    static int64_t memo_changed_ms = 0;
    float slide_t = 1.0f;
    int knob_x;
    if (memo_rect.x != rect.x || memo_rect.y != rect.y || memo_rect.width != rect.width) {
        memo_rect = rect;
        memo_is_on = is_on;
        memo_changed_ms = 0;
    } else if (memo_is_on != is_on) {
        memo_is_on = is_on;
        memo_changed_ms = ptc_ui_anim_now_ms();
    }
    if (memo_changed_ms != 0) {
        slide_t = ui_ease_out((float)(ptc_ui_anim_now_ms() - memo_changed_ms) / 120.0f);
        if (slide_t >= 1.0f) memo_changed_ms = 0;
    }
    knob_x = off_x + (int)((float)(on_x - off_x) * (is_on ? slide_t : 1.0f - slide_t));
    int knob_y = rect.y + 3;

    if (selected && !disabled) {
        fill_round_rect(pixels, stride, (UiRect){rect.x - 3, rect.y - 3, rect.width + 6, rect.height + 6},
                        radius + 3, UI_ACCENT);
    }
    fill_round_rect(pixels, stride, rect, radius, bg_color);
    fill_round_rect(pixels, stride, (UiRect){knob_x, knob_y, knob_size, knob_size}, knob_size / 2, knob_color);

    if (on_label && off_label) {
        const char *label = is_on ? on_label : off_label;
        uint32_t text_color = is_on ? UI_ON_ACCENT : UI_MUTED;
        int label_x = is_on ? (rect.x + 12) : (rect.x + knob_size + 8);
        draw_text(pixels, stride, label_x, rect.y + rect.height / 2 + 5, label, 15, text_color);
    }
}

static void draw_holiday_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {838, 176, 388, 340};
    UiRect top_card = to_uirect(ptc_ui_holiday_card_rect(0));
    const char *titles[] = {"法定休假", "调休工作日"};
    const char *descriptions[] = {"主要法定节假日的休假日期", "节假日调休产生的补班日期"};
    char line[160];
    char minutes_str[64];
    bool disabled = model->disable_flag_present;
    bool top_selected = model->selected_index == 0;
    fill_round_rect(pixels, stride, top_card, 16, disabled ? UI_PAGE : (top_selected ? UI_ACCENT_SOFT : UI_SURFACE));
    draw_rect_outline(pixels, stride, top_card, 16, top_selected ? 3 : 1, top_selected ? UI_ACCENT : UI_BORDER);
    draw_text(pixels, stride, top_card.x + 20, top_card.y + 32, "国家节假日规则", 22, UI_INK);
    draw_text(pixels, stride, top_card.x + 20, top_card.y + 60, "开启后自动应用法定休假与调休工作日规则", 14, UI_MUTED);
    {
        const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
        snprintf(line, sizeof(line), "内置日历：%u  |  v%u", (unsigned int)info->last_year, (unsigned int)info->version);
        draw_text(pixels, stride, top_card.x + 498, top_card.y + 34, line, 14,
                  model->calendar_update_warning ? UI_DANGER : UI_SUCCESS);
    }
    draw_toggle_switch(pixels, stride, to_uirect(ptc_ui_holiday_enable_rect()), model->draft_holiday_enabled,
                       top_selected, disabled, NULL, NULL);

    for (int index = 0; index < 2; ++index) {
        PtcDayRule rule = index == 0 ? model->draft_holiday_rule : model->draft_makeup_workday_rule;
        UiRect card = to_uirect(ptc_ui_holiday_card_rect(index + 1));
        UiRect mode = to_uirect(ptc_ui_holiday_mode_rect(index));
        UiRect minutes = to_uirect(ptc_ui_holiday_minutes_rect(index));
        bool selected = model->selected_index == index + 1;
        bool limited = rule.mode == PTC_RULE_MODE_LIMIT;
        draw_plan_card(pixels, stride, card, selected && !model->parent_footer_focused);
        draw_text(pixels, stride, card.x + 18, card.y + 34, titles[index], 21, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, card.x + 18, card.y + 62, descriptions[index], 13, UI_MUTED);
        fill_round_rect(pixels, stride, mode, 18,
                        disabled ? UI_BORDER : (limited ? UI_ACCENT : UI_SUCCESS));
        draw_text_center(pixels, stride, mode, limited ? "限时" : "不限时", 14,
                         disabled ? UI_DISABLED : UI_ON_ACCENT);
        fill_round_rect(pixels, stride, minutes, 12, disabled || !limited ? UI_RAISED : UI_RAISED);
        if (limited) {
            snprintf(minutes_str, sizeof(minutes_str), "%u 分钟（%u小时%u分）", (unsigned int)rule.minutes,
                     (unsigned int)rule.minutes / 60, (unsigned int)rule.minutes % 60);
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 46, minutes_str, 22,
                      disabled ? UI_DISABLED : UI_RGB(UI_BLENDED(accent)));
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 88, "A / 点按修改额度", 14,
                      disabled ? UI_DISABLED : UI_MUTED);
        } else {
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 46, "不限时间", 22, UI_DISABLED);
            draw_text(pixels, stride, minutes.x + 16, minutes.y + 88, "点按后提示先切换为限时", 14, UI_DISABLED);
        }
    }
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(3), "X  切换模式",
                           UI_PAGE, UI_ACCENT, model->selected_index == 3, disabled);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(4), "ZL  放弃",
                           UI_PAGE, UI_INK, model->selected_index == 4,
                           !model->holiday_dirty);
    bool holiday_hold = model->holiday_dirty && ptc_ui_plan_save_requires_hold(model, PTC_UI_PLAN_HOLIDAY, ptc_ui_render_now());
    draw_candidate_button(pixels, stride, ptc_ui_holiday_card_rect(5),
                           disabled ? "紧急停用中，设置只读" : (model->waiting ? "正在保存..." :
                           (model->holiday_dirty ? (holiday_hold ? "长按 + 保存" : "+  保存草稿") : "已保存")),
                           holiday_hold ? UI_DANGER : UI_ACCENT, UI_ON_ACCENT, model->selected_index == 5,
                           disabled || model->waiting || !model->holiday_dirty);
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_HOLIDAY, model->holiday_dirty, panel);
    draw_candidate_button(pixels, stride, ptc_ui_holiday_calendar_rect(), "查看节假日安排",
                           UI_ACCENT_SOFT, UI_ACCENT, model->selected_index == 6, false);
}

const char *bedtime_override_label(PtcBedtimeOverrideMode mode)
{
    if (mode == PTC_BEDTIME_OVERRIDE_DISABLED) return "关闭";
    if (mode == PTC_BEDTIME_OVERRIDE_CUSTOM) return "自定义";
    return "继承";
}

static void draw_bedtime_window_value(char *out, size_t out_size, const PtcBedtimeWindow *window)
{
    if (!window->enabled) snprintf(out, out_size, "关闭");
    else snprintf(out, out_size, "%02u:%02u - 次日 %02u:%02u",
        (unsigned int)(window->start_minute / 60), (unsigned int)(window->start_minute % 60),
        (unsigned int)(window->end_minute / 60), (unsigned int)(window->end_minute % 60));
}

static void draw_bedtime_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *SECTIONS[] = {"每周", "节假日", "指定日期就寝"};
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    char line[128];
    for (int i = 0; i < 3; ++i) {
        UiRect rect = to_uirect(ptc_ui_bedtime_section_rect(i));
        bool selected = model->bedtime_section == (PtcUiBedtimeSection)i;
        fill_round_rect(pixels, stride, rect, 12, selected ? UI_ACCENT : UI_RAISED);
        draw_text_center(pixels, stride, rect, SECTIONS[i], 18, selected ? UI_ON_ACCENT : UI_INK);
        if (selected && model->bedtime_section_focused) draw_focus_ring(pixels, stride, rect, 12);
    }

    bool bedtime_enforcing = model->bedtime_active && !model->bedtime_skipped;

    /* 独立醒目的就寝时间总开关卡片 (Bedtime Master Switch Card) */
    UiRect master_card = to_uirect(ptc_ui_bedtime_master_switch_rect());
    draw_card_shadow(pixels, stride, master_card, 16);
    fill_round_rect(pixels, stride, master_card, 16, UI_RGB(UI_BLENDED(surface)));
    if (bedtime_enforcing) {
        int phase = get_breathing_phase();
        draw_rect_outline(pixels, stride, master_card, 16, 2,
                          UI_RGB(ui_mix_rgb(UI_BLENDED(danger), 0xFF9A8A, phase * 4)));
    } else {
        draw_rect_outline(pixels, stride, master_card, 16, 1, UI_RGB(UI_BLENDED(border_control)));
    }
    draw_text(pixels, stride, master_card.x + 18, master_card.y + 28, "就寝时间总开关", 20, UI_INK);
    if (bedtime_enforcing) {
        draw_text(pixels, stride, master_card.x + 18, master_card.y + 54,
                  "生效中 (夜间立断限制执行中)", 13, UI_DANGER);
        UiRect active_pill = {master_card.x + 175, master_card.y + 12, 96, 20};
        fill_round_rect(pixels, stride, active_pill, 6, UI_DANGER_SOFT);
        draw_text_center(pixels, stride, active_pill, "● 立断生效中", 12, UI_DANGER);
    } else {
        draw_text(pixels, stride, master_card.x + 18, master_card.y + 54,
                  draft->enabled ? "开启 (到点自动执行就寝限制)" : "关闭 (停用全部就寝限制)", 13,
                  draft->enabled ? UI_SUCCESS : UI_MUTED);
    }
    UiRect toggle_rect = {master_card.x + master_card.width - 76, master_card.y + (master_card.height - 30) / 2, 60, 30};
    draw_toggle_switch(pixels, stride, toggle_rect, draft->enabled, false, model->disable_flag_present, NULL, NULL);
    draw_text(pixels, stride, master_card.x + master_card.width - 92, master_card.y + master_card.height - 8,
              "- / 点按切换", 11, UI_MUTED);

    /* 页面状态、预测与风险统一在一张卡片中，避免与全局状态和底部反馈重复。 */
    {
        UiRect eval_card = {838, 266, 388, 364};
        uint32_t state_color = strcmp(model->result_status, "error") == 0 ? UI_DANGER :
            (model->waiting || model->bedtime_dirty ? UI_WARNING :
             (bedtime_enforcing ? UI_DANGER : UI_SUCCESS));
        uint32_t state_bg = state_color == UI_DANGER ? UI_DANGER_SOFT :
            (state_color == UI_WARNING ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
        const char *state_text = strcmp(model->result_status, "error") == 0 ? "保存失败，草稿仍保留" :
            (model->waiting ? "正在保存并等待后台确认" :
             (model->bedtime_dirty ? "草稿尚未保存" :
              (bedtime_enforcing ? "就寝限制正在生效" : "计划已保存")));
        fill_round_rect(pixels, stride, eval_card, 12, UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, eval_card, 12, 1, UI_RGB(UI_BLENDED(border_control)));
        draw_text(pixels, stride, eval_card.x + 16, eval_card.y + 28,
                  "计划状态与今晚预测", 17, UI_RGB(UI_BLENDED(text_primary)));
        UiRect state_pill = {eval_card.x + 16, eval_card.y + 42, eval_card.width - 32, 34};
        fill_round_rect(pixels, stride, state_pill, 9, state_bg);
        draw_rect_outline(pixels, stride, state_pill, 9, 1, state_color);
        draw_text_center(pixels, stride, state_pill, state_text, 14, state_color);

        time_t raw_now = time(NULL);
        struct tm *tm_now = localtime(&raw_now);
        uint16_t minute_of_day = tm_now ? (uint16_t)(tm_now->tm_hour * 60 + tm_now->tm_min) : 0;
        PtcRules eval_rules;
        memset(&eval_rules, 0, sizeof(eval_rules));
        eval_rules.bedtime = *draft;
        PtcBedtimeEvaluation eval = ptc_bedtime_evaluate(
            &eval_rules, model->day_index, ptc_weekday_from_day_index(model->day_index), minute_of_day);

        char forecast_line1[96];
        char forecast_line2[96];
        uint32_t f1_color = UI_RGB(UI_BLENDED(text_primary));

        if (!draft->enabled) {
            snprintf(forecast_line1, sizeof(forecast_line1), "当前预测：总开关关闭，夜间不设就寝限制");
            f1_color = UI_MUTED;
        } else if (eval.active) {
            snprintf(forecast_line1, sizeof(forecast_line1), "! 立即生效：当前处于就寝时段，保存后将立断！");
            f1_color = UI_DANGER;
        } else {
            PtcEffectiveBedtime eff_today = ptc_bedtime_resolve_start_day(
                &eval_rules, model->day_index, ptc_weekday_from_day_index(model->day_index));
            if (eff_today.window.enabled) {
                int diff_m = (int)eff_today.window.start_minute - (int)minute_of_day;
                if (diff_m < 0) diff_m += 1440;
                snprintf(forecast_line1, sizeof(forecast_line1), "今晚 %02u:%02u 开始就寝（距现在约 %u 小时 %u 分）",
                         (unsigned int)(eff_today.window.start_minute / 60),
                         (unsigned int)(eff_today.window.start_minute % 60),
                         (unsigned int)(diff_m / 60), (unsigned int)(diff_m % 60));
                f1_color = UI_SUCCESS;
            } else {
                snprintf(forecast_line1, sizeof(forecast_line1), "今晚安排：本日就寝窗口已关闭，不限制");
                f1_color = UI_MUTED;
            }
        }
        draw_wrapped_text(pixels, stride, eval_card.x + 16, eval_card.y + 104,
                          forecast_line1, 13, eval_card.width - 32, 19, 2, f1_color);

        int enabled_count = 0;
        int total_span_m = 0;
        for (int d = 0; d < 7; ++d) {
            if (draft->week[d].enabled) {
                enabled_count++;
                total_span_m += (int)(draft->week[d].end_minute + 1440 - draft->week[d].start_minute) % 1440;
            }
        }
        snprintf(forecast_line2, sizeof(forecast_line2), "每周统计：开启 %d/7 天  |  周总就寝管控 %u 小时",
                 enabled_count, (unsigned int)(total_span_m / 60));
        draw_text(pixels, stride, eval_card.x + 16, eval_card.y + 150,
                  forecast_line2, 13, UI_MUTED);

        const char *cal_str = draft->calendar_enabled ? "节假日日历开启" : "节假日跟随周计划";
        const char *sch_str = draft->scheduled_override.present ? "指定日期生效中" : "指定日期未设置";
        char special_line[128];
        snprintf(special_line, sizeof(special_line), "特殊规则：%s  |  %s", cal_str, sch_str);
        draw_wrapped_text(pixels, stride, eval_card.x + 16, eval_card.y + 178,
                          special_line, 12, eval_card.width - 32, 18, 2, UI_MUTED);

        UiRect risk = {eval_card.x + 16, eval_card.y + 218, eval_card.width - 32, 64};
        fill_round_rect(pixels, stride, risk, 8,
            model->bedtime_official_setting_confirmed && model->bedtime_overlay_verified
                ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
        draw_wrapped_text(pixels, stride, risk.x + 12, risk.y + 22,
            model->bedtime_official_setting_confirmed
                ? (model->bedtime_overlay_verified
                    ? "官方暂停设置已确认，Overlay 已验证。"
                    : "官方暂停设置已确认；保存即接受 Overlay 尚未验证的恢复风险。")
                : "首次启用前请确认 Nintendo 家长控制已开启“时间到了暂停软件”，并接受 Overlay 恢复风险。",
            12, risk.width - 24, 18, 2,
            model->bedtime_official_setting_confirmed && model->bedtime_overlay_verified
                ? UI_SUCCESS : UI_WARNING);

        draw_text(pixels, stride, eval_card.x + 16,
                  eval_card.y + eval_card.height - 18,
                  "L/R 切换区段，- 总开关，+ 保存，ZL 放弃",
                  12, UI_ACCENT);
    }

    if (model->bedtime_section == PTC_UI_BEDTIME_WEEKLY) {
        int today_weekday = ptc_weekday_from_day_index(model->day_index);
        for (int slot = 0; slot < 7; ++slot) {
            int day = ptc_ui_weekday_for_display_slot(slot);
            UiRect card = to_uirect(ptc_ui_bedtime_field_rect(PTC_UI_BEDTIME_WEEKLY, slot));
            char value[64];
            int diff = day - today_weekday;
            uint16_t slot_day_idx = (uint16_t)(model->day_index + diff);
            uint16_t c_y; uint8_t c_m, c_d;
            bool has_date = ptc_date_from_day_index(slot_day_idx, &c_y, &c_m, &c_d);
            bool is_today = (day == today_weekday);
            bool is_active_day = (bedtime_enforcing && is_today);

            draw_plan_card(pixels, stride, card,
                model->selected_index == slot && !model->bedtime_section_focused && !model->parent_footer_focused);

            if (is_active_day) {
                draw_rect_outline(pixels, stride, card, 12, 2, UI_DANGER);
            } else if (is_today) {
                draw_rect_outline(pixels, stride, card, 12, 1, UI_ACCENT);
            }

            if (is_today) {
                UiRect today_pill = {card.x + (card.width - 38) / 2, card.y + 6, 38, 16};
                fill_round_rect(pixels, stride, today_pill, 4, is_active_day ? UI_DANGER_SOFT : UI_ACCENT_SOFT);
                draw_text_center(pixels, stride, today_pill, is_active_day ? "立断" : "今日", 11,
                                 is_active_day ? UI_DANGER : UI_ACCENT);
            }

            draw_text_center(pixels, stride, (UiRect){card.x, card.y + (is_today ? 23 : 13), card.width, 20},
                             DAYS[day], 16, is_today ? UI_ACCENT : UI_INK);

            if (has_date) {
                char date_str[16];
                snprintf(date_str, sizeof(date_str), "%02u/%02u", c_m, c_d);
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + (is_today ? 42 : 33), card.width, 18},
                                 date_str, 12, is_today ? UI_ACCENT : UI_MUTED);
            }

            draw_bedtime_window_value(value, sizeof(value), &draft->week[day]);
            if (draft->week[day].enabled) {
                snprintf(line, sizeof(line), "%02u:%02u",
                    (unsigned int)(draft->week[day].start_minute / 60),
                    (unsigned int)(draft->week[day].start_minute % 60));
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 64, card.width, 22}, line, 18,
                                 draft->enabled ? (is_active_day ? UI_DANGER : UI_ACCENT) : UI_MUTED);
                snprintf(line, sizeof(line), "到 %02u:%02u",
                    (unsigned int)(draft->week[day].end_minute / 60),
                    (unsigned int)(draft->week[day].end_minute % 60));
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 88, card.width, 18}, line, 13, UI_MUTED);

                /* 开启状态微标 */
                UiRect pill = {card.x + (card.width - 48) / 2, card.y + 114, 48, 20};
                fill_round_rect(pixels, stride, pill, 6,
                                is_active_day ? UI_DANGER_SOFT : (draft->enabled ? UI_ACCENT_SOFT : UI_RAISED));
                draw_text_center(pixels, stride, pill,
                                 is_active_day ? "生效中" : (draft->enabled ? "开启" : "暂停"), 12,
                                 is_active_day ? UI_DANGER : (draft->enabled ? UI_ACCENT : UI_MUTED));
            } else {
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 74, card.width, 24}, "关闭", 18, UI_MUTED);
                UiRect pill = {card.x + (card.width - 48) / 2, card.y + 114, 48, 20};
                fill_round_rect(pixels, stride, pill, 6, UI_RAISED);
                draw_text_center(pixels, stride, pill, "关闭", 12, UI_MUTED);
            }
            draw_text_center(pixels, stride, (UiRect){card.x, card.y + 148, card.width, 18}, "A/X 设置", 11, UI_MUTED);
        }
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 7), "复制到工作日",
            UI_PAGE, UI_ACCENT, model->selected_index == 7 && !model->bedtime_section_focused, model->disable_flag_present);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 8), "复制到周末",
            UI_PAGE, UI_ACCENT, model->selected_index == 8 && !model->bedtime_section_focused, model->disable_flag_present);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 9), "ZL  放弃",
            UI_PAGE, UI_INK, model->selected_index == 9 && !model->bedtime_section_focused, !model->bedtime_dirty);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(0, 10), "+  保存",
            UI_ACCENT, UI_ON_ACCENT, model->selected_index == 10 && !model->bedtime_section_focused,
            !model->bedtime_dirty || model->disable_flag_present || model->waiting);

        /* 左下角就寝规则说明面板（与右侧预测卡片底部平齐对齐至 y=630） */
        UiRect bedtime_guide = {54, 500, 752, 130};
        fill_round_rect(pixels, stride, bedtime_guide, 12, UI_RGB(UI_BLENDED(surface)));
        draw_rect_outline(pixels, stride, bedtime_guide, 12, 1, UI_RGB(UI_BLENDED(border_control)));
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 26,
                  "就寝时间并行规则与执行机制", 16, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 54,
                  "• 到点立断：进入就寝窗口后立即强制限制，不受今日剩余游玩额度影响", 14, UI_MUTED);
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 80,
                  "• 单次跳过：若今晚有特殊需要，可在今日调度页面选择“跳过今晚就寝”放行一次", 14, UI_MUTED);
        draw_text(pixels, stride, bedtime_guide.x + 18, bedtime_guide.y + 106,
                  "• 跨日计算：就寝窗口支持跨日（如 22:00 至次日 07:00），到结束时间自动恢复", 14, UI_MUTED);
    } else if (model->bedtime_section == PTC_UI_BEDTIME_CALENDAR) {
        UiRect master = to_uirect(ptc_ui_bedtime_field_rect(1, 0));
        draw_plan_card(pixels, stride, master, model->selected_index == 0 && !model->bedtime_section_focused);
        draw_text(pixels, stride, master.x + 20, master.y + 40, "国家节假日日历", 20, UI_INK);
        draw_text(pixels, stride, master.x + 190, master.y + 40,
            draft->calendar_enabled ? "开启" : "关闭", 18,
            draft->calendar_enabled ? UI_SUCCESS : UI_MUTED);
        UiRect cal_toggle = {master.x + master.width - 76, master.y + (master.height - 28) / 2, 58, 28};
        draw_toggle_switch(pixels, stride, cal_toggle, draft->calendar_enabled,
                           model->selected_index == 0 && !model->bedtime_section_focused, false, NULL, NULL);
        draw_text(pixels, stride, master.x + master.width - 165, master.y + 39, "A/X 切换", 13, UI_MUTED);

        for (int i = 0; i < 2; ++i) {
            const PtcBedtimeSpecialRule *rule = i == 0 ? &draft->holiday_rule : &draft->makeup_workday_rule;
            UiRect card = to_uirect(ptc_ui_bedtime_field_rect(1, i + 1));
            char value[64];
            draw_plan_card(pixels, stride, card, model->selected_index == i + 1 && !model->bedtime_section_focused);
            draw_text(pixels, stride, card.x + 18, card.y + 30,
                i == 0 ? "法定休假" : "调休工作日", 19, UI_INK);
            draw_bedtime_window_value(value, sizeof(value), &rule->window);
            snprintf(line, sizeof(line), "%s  |  %s", bedtime_override_label(rule->mode),
                rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM ? value : "使用对应模式");
            draw_text(pixels, stride, card.x + 18, card.y + 72, line, 15, UI_MUTED);
        }
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(1, 3), "ZL  放弃",
            UI_PAGE, UI_INK, model->selected_index == 3 && !model->bedtime_section_focused, !model->bedtime_dirty);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(1, 4), "+  保存",
            UI_ACCENT, UI_ON_ACCENT, model->selected_index == 4 && !model->bedtime_section_focused,
            !model->bedtime_dirty || model->disable_flag_present || model->waiting);
    } else {
        const PtcBedtimeScheduledOverride *scheduled = &draft->scheduled_override;
        uint32_t duration = scheduled->end_day_index >= scheduled->start_day_index
            ? (uint32_t)scheduled->end_day_index - scheduled->start_day_index + 1u : 1u;
        const char *labels[] = {"指定日期就寝", "开始日期", "持续天数", "就寝规则"};
        char values[4][96];
        snprintf(values[0], sizeof(values[0]), "%s", scheduled->present ? "开启" : "关闭");
        {
            uint16_t year;
            uint8_t month, day;
            if (ptc_date_from_day_index(scheduled->start_day_index, &year, &month, &day))
                snprintf(values[1], sizeof(values[1]), "%04u-%02u-%02u", year, month, day);
            else snprintf(values[1], sizeof(values[1]), "等待有效日期");
        }
        snprintf(values[2], sizeof(values[2]), "%u 天", (unsigned int)duration);
        if (scheduled->rule.mode == PTC_BEDTIME_OVERRIDE_CUSTOM) {
            char value[64];
            draw_bedtime_window_value(value, sizeof(value), &scheduled->rule.window);
            snprintf(values[3], sizeof(values[3]), "%s  %s",
                bedtime_override_label(scheduled->rule.mode), value);
        } else snprintf(values[3], sizeof(values[3]), "%s", bedtime_override_label(scheduled->rule.mode));
        for (int i = 0; i < 4; ++i) {
            UiRect row = to_uirect(ptc_ui_bedtime_field_rect(2, i));
            draw_plan_card(pixels, stride, row, model->selected_index == i && !model->bedtime_section_focused);
            if (i == 0) {
                draw_text(pixels, stride, row.x + 18, row.y + 31, labels[0], 17, UI_MUTED);
                draw_text(pixels, stride, row.x + 150, row.y + 31,
                          scheduled->present ? "开启" : "关闭", 18,
                          scheduled->present ? UI_SUCCESS : UI_MUTED);
                UiRect sched_toggle = {row.x + row.width - 76, row.y + (row.height - 28) / 2, 58, 28};
                draw_toggle_switch(pixels, stride, sched_toggle, scheduled->present,
                                   model->selected_index == 0 && !model->bedtime_section_focused, false, NULL, NULL);
                draw_text(pixels, stride, row.x + row.width - 165, row.y + 31, "A/X 切换", 13, UI_MUTED);
            } else {
                draw_text(pixels, stride, row.x + 18, row.y + 31, labels[i], 17, UI_MUTED);
                draw_text(pixels, stride, row.x + 240, row.y + 31, values[i], 18, UI_INK);
                if (i == 1 || i == 2)
                    draw_text(pixels, stride, row.x + 484, row.y + 31,
                              "A 输入 | ZL/ZR ±7 天", 11, UI_MUTED);
            }
        }
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(2, 4), "ZL  放弃",
            UI_PAGE, UI_INK, model->selected_index == 4 && !model->bedtime_section_focused, !model->bedtime_dirty);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(2, 5), "+  保存",
            UI_ACCENT, UI_ON_ACCENT, model->selected_index == 5 && !model->bedtime_section_focused,
            !model->bedtime_dirty || model->disable_flag_present || model->waiting);
    }
}



static const char *bedtime_source_short(PtcBedtimeSource source)
{
    switch (source) {
    case PTC_BEDTIME_SOURCE_SCHEDULED_OVERRIDE: return "日期";
    case PTC_BEDTIME_SOURCE_STATUTORY_HOLIDAY: return "休假";
    case PTC_BEDTIME_SOURCE_MAKEUP_WORKDAY: return "调休";
    case PTC_BEDTIME_SOURCE_WEEKLY:
    default: return "每周";
    }
}

static void draw_time_plan_preview(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *WEEKDAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    UiRect panel = {824, 172, 402, 452};
    int index;
    fill_round_rect(pixels, stride, panel, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    draw_text(pixels, stride, panel.x + 16, panel.y + 26, "未来 7 天计划与就寝预测", 17, UI_INK);

    if (!model->forecast_available) {
        draw_text(pixels, stride, panel.x + 16, panel.y + 110, "刷新状态后显示预测数据", 15, UI_MUTED);
        return;
    }

    for (index = 0; index < (int)PTC_RESULT_FORECAST_DAYS; ++index) {
        const PtcResultForecastDay *day = &model->forecast[index];
        PtcEffectiveBedtime bedtime = ptc_bedtime_resolve_start_day(
            (const PtcRules *)&(PtcRules){.bedtime = model->bedtime_policy},
            day->day_index, ptc_weekday_from_day_index(day->day_index));
        uint8_t weekday = ptc_weekday_from_day_index(day->day_index);
        UiRect row = {panel.x + 10, panel.y + 38 + index * 51, panel.width - 20, 44};
        bool is_today = (index == 0);

        if (is_today) {
            fill_round_rect(pixels, stride, row, 8, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, row, 8, 1, UI_ACCENT);
        } else {
            fill_round_rect(pixels, stride, row, 8, UI_RGB(UI_BLENDED(surface)));
            draw_rect_outline(pixels, stride, row, 8, 1, UI_RGB(UI_BLENDED(border_control)));
        }

        /* 1. 日期与星期 */
        char date_label[32];
        if (is_today) {
            snprintf(date_label, sizeof(date_label), "今天 %s", WEEKDAYS[weekday]);
        } else if (index == 1) {
            snprintf(date_label, sizeof(date_label), "明天 %s", WEEKDAYS[weekday]);
        } else {
            snprintf(date_label, sizeof(date_label), "%s (D+%d)", WEEKDAYS[weekday], index);
        }
        draw_text(pixels, stride, row.x + 10, row.y + 27, date_label, 14,
                  is_today ? UI_ACCENT : UI_INK);

        /* 2. 额度与微进度条 */
        int quota_x = row.x + 100;
        if (day->mode == PTC_RULE_MODE_UNLIMITED) {
            draw_text(pixels, stride, quota_x, row.y + 20, "不限时", 14, UI_SUCCESS);
            fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 28, 64, 4}, 2, UI_SUCCESS);
        } else {
            char q_str[24];
            snprintf(q_str, sizeof(q_str), "%u 分钟", (unsigned int)day->minutes);
            draw_text(pixels, stride, quota_x, row.y + 20, q_str, 14, UI_INK);
            int bar_w = 64;
            int fill_w = (int)((int64_t)bar_w * (day->minutes > 180 ? 180 : day->minutes) / 180);
            if (fill_w < 3 && day->minutes > 0) fill_w = 3;
            uint32_t bar_col = day->minutes <= 30 ? UI_WARNING : UI_ACCENT;
            fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 28, bar_w, 4}, 2, UI_BORDER);
            if (fill_w > 0) {
                fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 28, fill_w, 4}, 2, bar_col);
            }
        }

        /* 3. 规则来源胶囊徽标 */
        UiRect badge = {row.x + 178, row.y + 11, 56, 22};
        const char *badge_label = "周常规";
        uint32_t badge_bg = UI_RAISED;
        uint32_t badge_color = UI_MUTED;
        if (day->rule_source && strcmp(day->rule_source, "statutory_holiday") == 0) {
            badge_label = "节假日";
            badge_bg = UI_WARNING_SOFT;
            badge_color = UI_WARNING;
        } else if (day->rule_source && strcmp(day->rule_source, "makeup_workday") == 0) {
            badge_label = "调休";
            badge_bg = UI_ACCENT_SOFT;
            badge_color = UI_ACCENT;
        } else if (day->rule_source && strcmp(day->rule_source, "scheduled_override") == 0) {
            badge_label = "临时";
            badge_bg = UI_SUCCESS_SOFT;
            badge_color = UI_SUCCESS;
        } else if (day->rule_source && strcmp(day->rule_source, "today_override") == 0) {
            badge_label = "今日调";
            badge_bg = UI_WARNING_SOFT;
            badge_color = UI_WARNING;
        }
        fill_round_rect(pixels, stride, badge, 4, badge_bg);
        draw_text_center(pixels, stride, badge, badge_label, 11, badge_color);

        /* 4. 就寝窗口 */
        int bt_x = row.x + 244;
        if (!model->bedtime_policy.enabled || !bedtime.window.enabled) {
            draw_text(pixels, stride, bt_x + 12, row.y + 27, "无就寝", 13, UI_MUTED);
        } else {
            char bt_buf[48];
            snprintf(bt_buf, sizeof(bt_buf), "🌙 %02u:%02u %s",
                     (unsigned int)(bedtime.window.start_minute / 60),
                     (unsigned int)(bedtime.window.start_minute % 60),
                     bedtime_source_short(bedtime.source));
            draw_text(pixels, stride, bt_x, row.y + 27, bt_buf, 13, UI_MUTED);
        }
    }

    draw_text(pixels, stride, panel.x + 14, panel.y + panel.height - 16,
              "规则优先级：今日调整 -> 临时计划 -> 节假日 -> 周常规", 11, UI_MUTED);
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
                dynamic_action.subtitle = scheduled_active
                    ? "当前生效中，覆盖每天可玩额度"
                    : (model->scheduled_override.enabled ? "已启用，今日未在计划日期范围内" : "当前关闭");
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 1) {
                dynamic_action = *action;
                bool holiday_active = (strcmp(model->rule_source, "statutory_holiday") == 0 ||
                                       strcmp(model->rule_source, "makeup_workday") == 0);
                dynamic_action.subtitle = holiday_active
                    ? (strcmp(model->rule_source, "statutory_holiday") == 0
                        ? "当前生效中，国家法定休假日" : "当前生效中，国家调休工作日")
                    : (model->holiday_enabled ? "已启用，今日非节假日" : "当前关闭，可预设规则");
                action = &dynamic_action;
            } else if (model->parent_page == PTC_UI_PARENT_PLAN && index == 2) {
                dynamic_action = *action;
                bool scheduled_active = (strcmp(model->rule_source, "scheduled_override") == 0);
                bool holiday_active = (strcmp(model->rule_source, "statutory_holiday") == 0 ||
                                       strcmp(model->rule_source, "makeup_workday") == 0);
                bool today_active = (strcmp(model->rule_source, "today_override") == 0);
                if (today_active) {
                    dynamic_action.subtitle = "今日已被临时调整覆盖";
                } else if (scheduled_active) {
                    dynamic_action.subtitle = "今日已被临时额度计划覆盖";
                } else if (holiday_active) {
                    dynamic_action.subtitle = "今日已被国家节假日规则覆盖";
                } else {
                    dynamic_action.subtitle = "当前生效中，周一到周日基础额度";
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
    if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_WEEKLY) {
        draw_weekly_page(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) {
        draw_holiday_page(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_PLAN && model->plan_page == PTC_UI_PLAN_PAGE_BEDTIME) {
        draw_bedtime_page(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_TODAY) {
        draw_today_status(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_GRANT) {
        draw_grant_help(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_SUPPORT) {
        draw_safety_status(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_PLAN) {
        draw_time_plan_preview(pixels, stride, model);
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
