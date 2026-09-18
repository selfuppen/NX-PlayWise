#include "ui_render_internal.h"

static const char *rule_source_label(const char *source);

static const UiAction TODAY_ACTIONS[] = {
    {"设置今日总额度", "指定今天全天可玩的分钟数", UI_ACCENT},
    {"快速加时", "+15、+30、+60 分钟或自定义", UI_SUCCESS},
    {"今日不限时", "今天不设时间上限", UI_SUCCESS},
    {"清除今日额度调整", "只清除今日临时额度，恢复下级规则", UI_MUTED},
    {"当前 / 下次就寝", "查看并跳过最近一次就寝窗口", UI_WARNING},
    {"自主缓冲领取状态", "只读显示今天是否仍可领取", UI_MUTED},
};

static const UiAction PLAN_ACTIONS[] = {
    {"临时额度计划", "指定日期范围的每天额度，最多 366 天", UI_ACCENT},
    {"国家节假日", "法定休假和调休工作日额度", UI_SUCCESS},
    {"每周计划", "设置周一到周日的基础额度", UI_ACCENT},
    {"就寝时间", "独立设置每周、节假日和指定日期就寝", UI_WARNING},
    {"自主缓冲", "孩子每天可自主领取的小额时间", UI_SUCCESS},
};

static const UiAction GRANT_ACTIONS[] = {
    {"立即生成加时码", "选时长、生成、告诉孩子", UI_SUCCESS},
    {"手机/电脑生成加时码", "优先使用完整交付包中的离线网页", UI_ACCENT},
    {"加时码生成管理", "管理配对信息与导出配置", UI_MUTED},
    {"加时码使用记录", "查看最近 100 条成功兑换", UI_MUTED},
};

static const UiAction SETTINGS_ACTIONS[] = {
    {"外观主题", "跟随系统、浅色或暗色", UI_ACCENT},
    {"修改任我玩PIN", "验证当前 PIN 后设置新 PIN", UI_ACCENT},
    {"家长区快捷键管理", "选择组合并管理孩子区提示", UI_ACCENT},
    {"自制程序菜单\n高级入口", "改变 hbmenu 启动方式，不提供防篡改保护", UI_DANGER},
    {"家庭活动记录", "规则、加时和保护事件，最多 200 条", UI_MUTED},
};

const UiAction GRANT_MANAGER_ACTIONS[] = {
    {"管理加时码设备名", "查看、输入或随机生成设备名", UI_ACCENT},
    {"管理加时码密钥", "查看、输入或随机生成签名密钥", UI_DANGER},
    {"导出手机/电脑配置", "导出供手机或电脑使用的配置文件", UI_SUCCESS},
    {"编辑二维码跳转地址", "修改扫码后打开的网页地址", UI_ACCENT},
    {"恢复二维码跳转默认地址", "恢复项目提供的默认网页地址", UI_MUTED},
};

static const UiAction SUPPORT_ACTIONS[] = {
    {"确认接管系统控制", "预检、保存快照后启用额度管理", UI_ACCENT},
    {"重试修复", "重新检查并恢复安全前置条件", UI_SUCCESS},
    {"紧急停用控制", "停止新的控制写入，仍可诊断与恢复", UI_DANGER},
    {"恢复安装前状态", "恢复原始设置并停用 任我玩", UI_DANGER},
    {"导出诊断包", "导出诊断，不含密钥、PIN 或离线码", UI_MUTED},
    {"软件信息", "查看版本、项目仓库和家长网页", UI_ACCENT},
};

static const UiAction RESUME_CONTROL_ACTION = {
    "解除停用并重新接管", "安全预检通过后恢复后台控制", UI_SUCCESS
};

static const UiAction RECONFIRM_ENVIRONMENT_ACTION = {
    "重新检测并接管", "系统环境变化，确认兼容后恢复控制", UI_WARNING
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

const char *home_runtime_notice(const PtcUiModel *model)
{
    if (model->disable_flag_present) return "控制已停用，请家长到支持与恢复处理";
    if (model->recovery_active) return "正在恢复设置，请等待恢复完成";
    if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0)
        return "需要家长处理，请进入支持与恢复";
    if (model->restriction_enabled_available && !model->restriction_enabled)
        return "Nintendo 家长控制未启用，请家长检查系统设置";
    if (model->temporary_unlocked_available && model->temporary_unlocked)
        return "临时解除期间不计时，进入睡眠后恢复今日限制";
    if (model->apply_pending_confirmation) return "设置等待确认生效，请稍候";
    if (model->restricted_now == 1) return "已进入时间限制，可兑换加时码或请家长调整额度";
    if (model->remaining_available && model->remaining_minutes == 0 && model->unrestricted_today != 1)
        return "额度已用完，限制可能即将生效，可兑换加时码";
    return "";
}

static void draw_home_notice(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    bool expanded = ptc_ui_home_notice_expanded(model) || model->waiting || measure_text(model->message, 20) > 1100;
    bool error = strcmp(model->result_status, "error") == 0;
    const char *runtime = home_runtime_notice(model);
    const char *title = runtime[0] ? runtime : (error ? "操作未完成" : (model->waiting ? "正在同步" : ""));
    UiRect box = {48, 520, 1184, 128};
    uint32_t accent = error || model->disable_flag_present ? UI_DANGER : UI_WARNING;
    fill_round_rect(pixels, stride, box, 16, expanded ? (error || model->disable_flag_present ? UI_DANGER_SOFT : UI_WARNING_SOFT) : UI_SURFACE);
    if (model->waiting) {
        draw_rect_outline(pixels, stride, box, 16, 1, UI_WARNING);
    } else {
        draw_rect_outline(pixels, stride, box, 16, 1, UI_BORDER);
    }
    int icon_x = 68;
    int icon_y = 535;
    int text_x = 100;
    int baseline = 548;

    if (title[0]) {
        draw_status_symbol(pixels, stride, icon_x, icon_y, accent, error ? 3 : (model->waiting ? 2 : 1));
        draw_text(pixels, stride, text_x, baseline, title, 20, accent);
        int y = baseline + 26;
        y = draw_wrapped_text(pixels, stride, text_x, y, model->message, 18, 1100, 23, 2, UI_INK);
        if (model->feedback_detail[0])
            draw_wrapped_text(pixels, stride, text_x, y, model->feedback_detail, 18, 1100, 23,
                (644 - y) / 23 + 1, UI_MUTED);
    } else {
        draw_status_symbol(pixels, stride, icon_x, icon_y, UI_SUCCESS, 1);
        const char *msg = model->message[0] ? model->message : "状态会在后台自动同步";
        uint32_t text_col = model->message[0] ? UI_INK : UI_MUTED;
        draw_text(pixels, stride, text_x, baseline, msg, 20, text_col);
        if (model->feedback_detail[0])
            draw_wrapped_text(pixels, stride, text_x, baseline + 26, model->feedback_detail, 18, 1100, 23,
                (644 - (baseline + 26)) / 23 + 1, UI_MUTED);
    }
}

static void draw_home_summary(uint32_t *pixels, uint32_t stride, const PtcUiModel *model, bool parent)
{
    UiRect box = to_uirect(ptc_ui_home_summary_rect(parent));
    char remaining[64], today[64], line[192], age[64];
    int x = box.x + 28;
    ptc_ui_format_home_remaining(model, ptc_ui_render_now(), remaining, sizeof(remaining));
    ptc_ui_format_today_mode(model, today, sizeof(today));
    draw_card_shadow(pixels, stride, box, 16);
    fill_round_rect(pixels, stride, box, 16, UI_RGB(UI_BLENDED(hero)));
    /* 剩余告急时描边随呼吸相位脉冲。 */
    if (model->remaining_available && model->unrestricted_today != 1 &&
        model->remaining_minutes >= 0 && model->remaining_minutes <= 10) {
        int phase = get_breathing_phase();
        draw_rect_outline(pixels, stride, box, 16, 2,
                          UI_RGB(ui_mix_rgb(UI_BLENDED(danger), 0xFF9A8A, phase * 4)));
    }
    draw_text(pixels, stride, x, box.y + 42, "今天还可玩", 22, UI_RGB(UI_BLENDED(hero_secondary)));
    /* 环形额度表：弧长由缓动后的剩余分钟驱动，颜色沿用今日额度健康色；
     * 数据不可用时只画弱化轨道环。 */
    {
        float fraction = 0.0f;
        uint32_t ring_fill = UI_SUCCESS;
        bool has_fraction = false;
        if (model->unrestricted_today == 1) {
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
        if (has_fraction && model->unrestricted_today != 1) {
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
    if (remaining_mins >= 0) {
        if (remaining_mins <= 10) health_color = UI_DANGER;
        else if (remaining_mins <= 30) health_color = UI_WARNING;
        else health_color = UI_SUCCESS;
    }

    if (model->unrestricted_today == 1) {
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

    snprintf(line, sizeof(line), "今日%s  /  %s", today,
        model->status_loaded ? rule_source_label(model->rule_source) : "待确认规则");
    draw_text(pixels, stride, x, box.y + 176, line, 18, UI_RGB(UI_BLENDED(hero_secondary)));
    /* Supporting information sits on a separate surface, below the hero. */
    UiRect lower = {box.x + 12, box.y + 200, box.width - 24, box.height - 212};
    fill_round_rect(pixels, stride, lower, 16, UI_SURFACE);
    if (parent) {
        ptc_ui_format_home_total(model, line, sizeof(line));
        draw_text(pixels, stride, x, box.y + 228, line, 20, UI_INK);
        if (model->played_minutes_available && model->played_minutes >= 0)
            snprintf(line, sizeof(line), "额度消耗估算  约 %d 分钟", model->played_minutes);
        else snprintf(line, sizeof(line), "额度消耗估算  暂不可用");
        draw_text(pixels, stride, x, box.y + 259, line, 18, UI_MUTED);
    } else {
        draw_text(pixels, stride, x, box.y + 232, "明天的安排", 18, UI_MUTED);
        if (model->forecast_available) {
            const PtcResultForecastDay *day = &model->forecast[1];
            if (day->mode == PTC_RULE_MODE_UNLIMITED) snprintf(line, sizeof(line), "不限时");
            else snprintf(line, sizeof(line), "%u 分钟", (unsigned int)day->minutes);
            draw_text(pixels, stride, x, box.y + 270, line, 28, UI_INK);
            draw_text(pixels, stride, x + 200, box.y + 270,
                rule_source_label(day->rule_source), 18, UI_MUTED);
        } else draw_text(pixels, stride, x, box.y + 270, "安排暂不可用", 24, UI_MUTED);
    }
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, x, box.y + box.height - 27, age, 18, UI_MUTED);
}

void draw_child(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char buffer[128], hint[160], fitted_hint[160];
    bool disabled = model->disable_flag_present || model->waiting;
    draw_header(pixels, stride, "今天的约定", "合理安排时间，完成今天的约定");
    draw_time_status_bar(pixels, stride, model);
    draw_home_summary(pixels, stride, model, false);
    draw_card_shadow(pixels, stride, (UiRect){704, 120, 528, 384}, 16);
    fill_round_rect(pixels, stride, (UiRect){704, 120, 528, 384}, 16, UI_RGB(UI_BLENDED(surface)));
    draw_text(pixels, stride, 736, 164, "需要多一点时间？", 28, UI_RGB(UI_BLENDED(text_primary)));
    draw_text(pixels, stride, 736, 195, "输入家长给你的 8 位数字", 18, UI_RGB(UI_BLENDED(text_secondary)));
    home_button(pixels, stride, ptc_ui_child_submit_rect(),
        model->disable_flag_present ? "兑换暂不可用" : "A  输入加时码", true, false, disabled);
    if (model->daily_buffer_available)
        snprintf(buffer, sizeof(buffer), "X  领取自主缓冲  +%u 分钟", (unsigned int)model->daily_buffer_minutes);
    else snprintf(buffer, sizeof(buffer), "%s", model->daily_buffer_claimed ? "今日已使用缓冲" :
        (model->daily_buffer_minutes == 0 ? "今日自主缓冲未开启" : "自主缓冲仅可在限时日领取"));
    home_button(pixels, stride, ptc_ui_child_buffer_rect(), buffer, false, false,
        disabled || !model->daily_buffer_available);
    home_button(pixels, stride, ptc_ui_home_details_rect(false), "+  使用详情", false, false, model->waiting);
    draw_home_notice(pixels, stride, model);
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

static void draw_card_action_icon(uint32_t *pixels, uint32_t stride, int cx, int cy, const char *title, uint32_t color, bool disabled)
{
    (void)disabled;
    if (!title) return;

    /* 1. 时钟/时间类：包含 "额度", "加时", "时间", "缓冲" */
    if (strstr(title, "额度") || strstr(title, "加时") || strstr(title, "时间") || strstr(title, "缓冲")) {
        draw_circle_outline(pixels, stride, cx, cy, 10, 2, color);
        draw_line(pixels, stride, cx, cy, cx, cy - 5, 2, color);
        draw_line(pixels, stride, cx, cy, cx + 4, cy + 1, 2, color);
        return;
    }

    /* 2. 日历/休假类：包含 "日历", "节假日", "休假", "调休", "计划" */
    if (strstr(title, "日历") || strstr(title, "节假日") || strstr(title, "休假") ||
        strstr(title, "调休") || strstr(title, "计划")) {
        draw_rect_outline(pixels, stride, (UiRect){cx - 9, cy - 8, 18, 16}, 3, 2, color);
        draw_line(pixels, stride, cx - 9, cy - 2, cx + 9, cy - 2, 2, color);
        draw_line(pixels, stride, cx - 4, cy - 10, cx - 4, cy - 7, 2, color);
        draw_line(pixels, stride, cx + 4, cy - 10, cx + 4, cy - 7, 2, color);
        return;
    }

    /* 3. 钥匙/安全类：包含 "PIN", "密钥" */
    if (strstr(title, "PIN") || strstr(title, "密钥")) {
        draw_circle_outline(pixels, stride, cx - 4, cy - 4, 6, 2, color);
        draw_line(pixels, stride, cx, cy, cx + 7, cy + 7, 2, color);
        draw_line(pixels, stride, cx + 4, cy + 4, cx + 7, cy + 1, 2, color);
        return;
    }

    /* 4. 设备/二维码类：包含 "二维码", "手机", "电脑", "设备" */
    if (strstr(title, "二维码") || strstr(title, "手机") || strstr(title, "电脑") || strstr(title, "设备")) {
        draw_rect_outline(pixels, stride, (UiRect){cx - 8, cy - 9, 16, 18}, 3, 2, color);
        draw_line(pixels, stride, cx - 3, cy + 4, cx + 3, cy + 4, 2, color);
        fill_rect(pixels, stride, (UiRect){cx - 1, cy - 6, 2, 2}, color);
        return;
    }

    /* 5. 外观主题：包含 "主题", "外观" */
    if (strstr(title, "主题") || strstr(title, "外观")) {
        draw_circle_outline(pixels, stride, cx, cy, 9, 2, color);
        fill_rect(pixels, stride, (UiRect){cx - 4, cy - 4, 3, 3}, color);
        fill_rect(pixels, stride, (UiRect){cx + 1, cy - 3, 3, 3}, color);
        fill_rect(pixels, stride, (UiRect){cx - 2, cy + 2, 3, 3}, color);
        return;
    }

    /* 6. 快捷键/程序入口：包含 "快捷键", "自制程序" */
    if (strstr(title, "快捷键") || strstr(title, "自制程序")) {
        draw_rect_outline(pixels, stride, (UiRect){cx - 10, cy - 6, 20, 13}, 4, 2, color);
        draw_line(pixels, stride, cx - 5, cy, cx - 1, cy, 2, color);
        draw_line(pixels, stride, cx - 3, cy - 2, cx - 3, cy + 2, 2, color);
        fill_rect(pixels, stride, (UiRect){cx + 3, cy - 2, 2, 2}, color);
        fill_rect(pixels, stride, (UiRect){cx + 5, cy + 1, 2, 2}, color);
        return;
    }

    /* 7. 安全/恢复/接管：包含 "接管", "重试", "停用", "恢复", "诊断", "支持" */
    if (strstr(title, "接管") || strstr(title, "重试") || strstr(title, "停用") ||
        strstr(title, "恢复") || strstr(title, "诊断") || strstr(title, "支持")) {
        draw_line(pixels, stride, cx - 8, cy - 8, cx + 8, cy - 8, 2, color);
        draw_line(pixels, stride, cx - 8, cy - 8, cx - 8, cy, 2, color);
        draw_line(pixels, stride, cx - 8, cy, cx, cy + 9, 2, color);
        draw_line(pixels, stride, cx + 8, cy - 8, cx + 8, cy, 2, color);
        draw_line(pixels, stride, cx + 8, cy, cx, cy + 9, 2, color);
        draw_line(pixels, stride, cx - 3, cy, cx - 1, cy + 3, 2, color);
        draw_line(pixels, stride, cx - 1, cy + 3, cx + 4, cy - 3, 2, color);
        return;
    }

    /* 8. 默认：齿轮/设置/记录 */
    draw_circle_outline(pixels, stride, cx, cy, 7, 2, color);
    draw_line(pixels, stride, cx, cy - 9, cx, cy - 7, 2, color);
    draw_line(pixels, stride, cx, cy + 7, cx, cy + 9, 2, color);
    draw_line(pixels, stride, cx - 9, cy, cx - 7, cy, 2, color);
    draw_line(pixels, stride, cx + 7, cy, cx + 9, cy, 2, color);
}

void draw_action_card(uint32_t *pixels, uint32_t stride, UiRect rect,
    const UiAction *action, bool selected, PtcUiActionState state, int reserved_right)
{
    bool disabled = state == PTC_UI_ACTION_DISABLED;
    bool recommended = state == PTC_UI_ACTION_RECOMMENDED;
    bool compact = rect.height < 90;
    int title_size = 22;
    int content_width = rect.width - 76;
    int title_width = content_width - reserved_right - (recommended ? 64 : 0);
    uint32_t background = disabled ? UI_RAISED : (selected ? UI_ACCENT_SOFT : UI_SURFACE);
    draw_card_shadow(pixels, stride, rect, 16);
    fill_round_rect(pixels, stride, rect, 16, background);
    if (selected) {
        draw_focus_ring(pixels, stride, rect, 16);
    } else {
        draw_rect_outline(pixels, stride, rect, 16, 1, UI_BORDER);
    }
    /* 左侧精致微图标徽章 */
    int icon_cx = rect.x + 32;
    int icon_cy = rect.y + rect.height / 2;
    UiRect badge_rect = {icon_cx - 17, icon_cy - 17, 34, 34};
    uint32_t badge_bg = disabled ? UI_PAGE :
        (action->accent == UI_SUCCESS ? UI_SUCCESS_SOFT :
        (action->accent == UI_DANGER ? UI_DANGER_SOFT :
        (action->accent == UI_WARNING ? UI_WARNING_SOFT : UI_ACCENT_SOFT)));
    fill_round_rect(pixels, stride, badge_rect, 10, badge_bg);
    draw_rect_outline(pixels, stride, badge_rect, 10, 1, UI_BORDER);
    draw_card_action_icon(pixels, stride, icon_cx, icon_cy, action->title,
        disabled ? UI_DISABLED : action->accent, disabled);

    int title_lines = measure_text(action->title, title_size) > title_width ? 2 : 1;
    int baseline = draw_wrapped_text(pixels, stride, rect.x + 58, rect.y + (compact ? 28 : 32),
        action->title, title_size, title_width, 25, title_lines, disabled ? UI_DISABLED : UI_INK);
    draw_wrapped_text(pixels, stride, rect.x + 58, baseline + 3, action->subtitle, 18,
        content_width, 22, (rect.y + rect.height - baseline - 4) / 22 + 1,
        disabled ? UI_DISABLED : UI_MUTED);
    if (recommended && !disabled) {
        fill_round_rect(pixels, stride, (UiRect){rect.x + rect.width - 66, rect.y + 8, 56, 24}, 6, UI_SUCCESS);
        draw_text_center(pixels, stride, (UiRect){rect.x + rect.width - 66, rect.y + 8, 56, 24}, "建议", 16, UI_ON_ACCENT);
    }
}

static void draw_safety_status(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {842, 176, 384, 324};
    char troubleshoot[128];
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    int recommended = ptc_ui_support_recommended_action(model);
    draw_text(pixels, stride, panel.x + 26, panel.y + 36, "当前问题", 23, UI_INK);
    draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 70, ptc_ui_support_problem(model),
                      19, panel.width - 52, 26, 2, UI_RGB(UI_BLENDED(text_primary)));
    const char *next = recommended == 0 ? (model->disable_flag_present ? "建议：解除停用并重新接管" : "建议：重新检测并接管") :
                       recommended == 1 ? "建议：选择重试修复" :
                       recommended == 4 ? "建议：导出诊断包，保留问题记录" :
                       (model->waiting || model->apply_pending_confirmation ? "请等待结果，再刷新状态" : "无需恢复操作，可按 B 返回设置");
    draw_wrapped_text(pixels, stride, panel.x + 26, panel.y + 132, next, 17,
                      panel.width - 52, 24, 2, UI_RGB(UI_BLENDED(accent)));
    char age[80];
    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, panel.x + 26, panel.y + 192, age, 15, status_age_color(model));
    if (model->environment_available)
        snprintf(troubleshoot, sizeof(troubleshoot), "HOS %s  |  %s", model->environment_hos, model->environment_model);
    else snprintf(troubleshoot, sizeof(troubleshoot), "环境详情暂不可用，可导出诊断");
    fit_text(troubleshoot, sizeof(troubleshoot), troubleshoot, 15, panel.width - 52);
    draw_text(pixels, stride, panel.x + 26, panel.y + 218, troubleshoot, 15, UI_RGB(UI_BLENDED(text_secondary)));
    draw_text(pixels, stride, panel.x + 26, panel.y + 246, "最近事件", 17, UI_MUTED);
    if (model->recent_event_count > 0) {
        for (int event_index = 0; event_index < model->recent_event_count; ++event_index) {
            char latest[192];
            char event_time[48];
            int source_index = model->recent_event_count - 1 - event_index;
            format_event_time(model->recent_event_timestamps[source_index], false, event_time, sizeof(event_time));
            snprintf(latest, sizeof(latest), "%s  |  %s", model->recent_events[source_index], event_time);
            fit_text(latest, sizeof(latest), latest, 13, panel.width - 52);
            if (event_index + 6 == model->selected_index && !model->parent_footer_focused)
                draw_rect_outline(pixels, stride, to_uirect(ptc_ui_support_event_rect(event_index)), 6, 2, UI_RGB(UI_BLENDED(focus)));
            draw_text(pixels, stride, panel.x + 26, panel.y + 270 + event_index * 22,
                      latest, 13, event_index + 6 == model->selected_index ? UI_ACCENT : UI_MUTED);
        }
    } else {
        draw_text(pixels, stride, panel.x + 26, panel.y + 274,
                  model->recent_events_available ? "最近没有需要注意的事件" : "暂时无法读取最近事件，可刷新后重试",
                  14, model->recent_events_available ? UI_MUTED : UI_DANGER);
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
                uint16_t year;
                uint8_t month, day;
                if (ptc_date_from_day_index(model->bedtime_start_day_index, &year, &month, &day))
                    snprintf(dynamic, sizeof(dynamic), "%04u-%02u-%02u %02u:%02u 到次日 %02u:%02u%s",
                        year, month, day,
                        (unsigned int)(model->bedtime_start_minute / 60),
                        (unsigned int)(model->bedtime_start_minute % 60),
                        (unsigned int)(model->bedtime_end_minute / 60),
                        (unsigned int)(model->bedtime_end_minute % 60),
                        model->bedtime_skipped ? "，已跳过" : "，生效中");
                else snprintf(dynamic, sizeof(dynamic), "%02u:%02u 到次日 %02u:%02u%s",
                    (unsigned int)(model->bedtime_start_minute / 60),
                    (unsigned int)(model->bedtime_start_minute % 60),
                    (unsigned int)(model->bedtime_end_minute / 60),
                    (unsigned int)(model->bedtime_end_minute % 60),
                    model->bedtime_skipped ? "，已跳过" : "，生效中");
                subtitle = dynamic;
            } else if (model->bedtime_skipped_window_available) {
                uint16_t year;
                uint8_t month, day;
                if (ptc_date_from_day_index(model->bedtime_skipped_start_day_index, &year, &month, &day))
                    snprintf(dynamic, sizeof(dynamic), "%04u-%02u-%02u %02u:%02u 到次日 %02u:%02u，已跳过",
                        year, month, day,
                        (unsigned int)(model->bedtime_skipped_start_minute / 60),
                        (unsigned int)(model->bedtime_skipped_start_minute % 60),
                        (unsigned int)(model->bedtime_skipped_end_minute / 60),
                        (unsigned int)(model->bedtime_skipped_end_minute % 60));
                else snprintf(dynamic, sizeof(dynamic), "%02u:%02u 到次日 %02u:%02u，已跳过",
                    (unsigned int)(model->bedtime_skipped_start_minute / 60),
                    (unsigned int)(model->bedtime_skipped_start_minute % 60),
                    (unsigned int)(model->bedtime_skipped_end_minute / 60),
                    (unsigned int)(model->bedtime_skipped_end_minute % 60));
                subtitle = dynamic;
            } else if (model->bedtime_next_available) {
                uint16_t year;
                uint8_t month, day;
                if (ptc_date_from_day_index(model->bedtime_next_start_day_index, &year, &month, &day))
                    snprintf(dynamic, sizeof(dynamic), "%04u-%02u-%02u %02u:%02u 到次日 %02u:%02u", year, month, day,
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
    UiRect panel = {842, 176, 384, 274};
    (void)model;
    fill_round_rect(pixels, stride, panel, 16, UI_RGB(UI_BLENDED(surface)));
    draw_text(pixels, stride, 866, 216, "把加时码告诉孩子", 24, UI_RGB(UI_BLENDED(text_primary)));
    draw_text(pixels, stride, 866, 258, "1  选择要增加的时长", 20, UI_RGB(UI_BLENDED(text_primary)));
    draw_text(pixels, stride, 866, 294, "2  验证 PIN，生成代码", 20, UI_RGB(UI_BLENDED(text_primary)));
    draw_text(pixels, stride, 866, 330, "3  孩子输入代码，确认加时", 20, UI_RGB(UI_BLENDED(text_primary)));
    draw_wrapped_text(pixels, stride, 866, 373, "当天有效，成功兑换后仅可使用一次。无需联网。", 18, 336, 25, 2, UI_RGB(UI_BLENDED(text_secondary)));
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
    char impact[256];
    char age[80];
    char title[64];
    PtcEffectiveRule before = ptc_ui_plan_rule(model, PTC_UI_PLAN_SAVED);
    PtcEffectiveRule after = ptc_ui_plan_rule(model, kind);
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    int played = fresh && model->played_minutes_available ? model->played_minutes : -1;
    bool unlimited = after.rule.mode == PTC_RULE_MODE_UNLIMITED;
    int remaining = unlimited ? -999 : (played >= 0 ? (int)after.rule.minutes - played : -1);
    bool requires_hold = dirty && ptc_ui_plan_save_requires_hold(model, kind, ptc_ui_render_now());
    int changed = 0;
    if (remaining < 0 && !unlimited && played >= 0) remaining = 0;

    if (kind == PTC_UI_PLAN_WEEKLY) {
        for (int d = 0; d < 7; ++d) {
            if (ptc_ui_day_rule_effectively_changed(model->current_week[d], model->draft_week[d])) ++changed;
        }
    } else if (kind == PTC_UI_PLAN_HOLIDAY) {
        if (model->holiday_enabled != model->draft_holiday_enabled) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->holiday_rule, model->draft_holiday_rule)) ++changed;
        if (ptc_ui_day_rule_effectively_changed(model->makeup_workday_rule, model->draft_makeup_workday_rule)) ++changed;
    }

    draw_plan_card(pixels, stride, panel, false);
    if (dirty) {
        if (changed > 0) {
            snprintf(title, sizeof(title), kind == PTC_UI_PLAN_WEEKLY ? "修改草稿 (已调整 %d 天)" : "修改草稿 (已调整 %d 项)", changed);
        } else {
            snprintf(title, sizeof(title), "修改草稿 (待保存)");
        }
    } else {
        snprintf(title, sizeof(title), "计划已保存 (正常生效)");
    }
    draw_text(pixels, stride, panel.x + 20, panel.y + 34, title, 19,
              dirty ? (requires_hold ? UI_DANGER : UI_WARNING) : UI_SUCCESS);
    const char *sub = model->disable_flag_present ? "控制已停用，计划只读" :
        (model->waiting ? "正在保存中，请稍候..." :
         (model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR ? "按 + 完成输入，再保存计划" :
          (requires_hold ? "⚠️ 保存后今日额度将耗尽；长按保存" :
           (dirty ? "按 + 保存草稿以应用到主机" : "已与主机策略同步并持续生效"))));
    draw_text(pixels, stride, panel.x + 20, panel.y + 58, sub, 13,
              requires_hold ? UI_DANGER : UI_RGB(UI_BLENDED(text_secondary)));

    int box_gap = 10;
    int box_w = (panel.width - 40 - box_gap) / 2;
    int box_h = 66;
    UiRect box1 = {panel.x + 20, panel.y + 72, box_w, box_h};
    UiRect box2 = {panel.x + 20 + box_w + box_gap, panel.y + 72, box_w, box_h};

    fill_round_rect(pixels, stride, box1, 10, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, box1, 10, 1, UI_RGB(UI_BLENDED(border_control)));
    draw_text(pixels, stride, box1.x + 12, box1.y + 20, "今日规划额度", 13, UI_MUTED);
    if (unlimited) {
        draw_text(pixels, stride, box1.x + 12, box1.y + 44, "不限时", 18, UI_SUCCESS);
    } else {
        char quota_str[32];
        snprintf(quota_str, sizeof(quota_str), "%u 分钟", (unsigned int)after.rule.minutes);
        draw_text(pixels, stride, box1.x + 12, box1.y + 44, quota_str, 18, UI_ACCENT);
    }
    if (dirty && ptc_ui_day_rule_effectively_changed(before.rule, after.rule)) {
        char orig_str[32];
        if (before.rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(orig_str, sizeof(orig_str), "原: 不限时");
        else snprintf(orig_str, sizeof(orig_str), "原: %u分", (unsigned int)before.rule.minutes);
        draw_text(pixels, stride, box1.x + 12, box1.y + 60, orig_str, 11, UI_MUTED);
    } else if (before.source != after.source) {
        char src_str[48];
        snprintf(src_str, sizeof(src_str), "规则变为: %s", ptc_ui_effective_rule_label(after.source));
        draw_text(pixels, stride, box1.x + 12, box1.y + 60, src_str, 11, UI_MUTED);
    } else {
        char src_str[48];
        snprintf(src_str, sizeof(src_str), "今天不变 (来源: %s)", ptc_ui_effective_rule_label(after.source));
        draw_text(pixels, stride, box1.x + 12, box1.y + 60, src_str, 11, UI_MUTED);
    }

    fill_round_rect(pixels, stride, box2, 10, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, box2, 10, 1, UI_RGB(UI_BLENDED(border_control)));
    draw_text(pixels, stride, box2.x + 12, box2.y + 20, "预计剩余时间", 13, UI_MUTED);
    if (unlimited) {
        draw_text(pixels, stride, box2.x + 12, box2.y + 44, "不限时", 18, UI_SUCCESS);
        draw_text(pixels, stride, box2.x + 12, box2.y + 60, "无时间限制", 11, UI_MUTED);
    } else if (played >= 0) {
        char rem_str[32];
        if (remaining > 0) {
            snprintf(rem_str, sizeof(rem_str), "%d 分钟", remaining);
            draw_text(pixels, stride, box2.x + 12, box2.y + 44, rem_str, 18,
                      remaining > 15 ? UI_SUCCESS : UI_WARNING);
        } else {
            draw_text(pixels, stride, box2.x + 12, box2.y + 44, "0 分钟 (立断)", 16, UI_DANGER);
        }
        char played_str[64];
        snprintf(played_str, sizeof(played_str), "额度已耗约 %d 分钟", played);
        draw_text(pixels, stride, box2.x + 12, box2.y + 60, played_str, 11, UI_MUTED);
    } else {
        draw_text(pixels, stride, box2.x + 12, box2.y + 44, "暂不可用", 18, UI_MUTED);
        draw_text(pixels, stride, box2.x + 12, box2.y + 60, "刷新状态后计算", 11, UI_MUTED);
    }

    int text_y = panel.y + 152;
    if (requires_hold) {
        fill_round_rect(pixels, stride, (UiRect){panel.x + 20, text_y, panel.width - 40, 22}, 5, UI_DANGER_SOFT);
        draw_text(pixels, stride, panel.x + 28, text_y + 16, "保存后今日时间将立即用尽，请长按保存", 12, UI_DANGER);
        text_y += 28;
    }

    draw_text(pixels, stride, panel.x + 20, text_y + 4, "今日影响与决策参考", 14, UI_RGB(UI_BLENDED(text_primary)));
    ptc_ui_format_plan_impact(model, kind, ptc_ui_render_now(), impact, sizeof(impact));
    draw_wrapped_text(pixels, stride, panel.x + 20, text_y + 24, impact,
                      13, panel.width - 40, 18, requires_hold ? 2 : 3, UI_RGB(UI_BLENDED(text_secondary)));

    format_status_age(model, age, sizeof(age));
    draw_text(pixels, stride, panel.x + 20, panel.y + panel.height - 18,
              age, 13, status_age_color(model));
}

static void draw_weekly_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    int slot;
    char detail[64];
    char freshness[64];
    uint8_t weekday = ptc_weekday_from_day_index(model->day_index);
    format_status_age(model, freshness, sizeof(freshness));
    draw_rect_outline(pixels, stride, (UiRect){54, 180, 26, 24}, 4, 2, UI_ACCENT);
    draw_line(pixels, stride, 54, 188, 80, 188, 2, UI_ACCENT);
    draw_line(pixels, stride, 61, 176, 61, 183, 3, UI_ACCENT);
    draw_line(pixels, stride, 73, 176, 73, 183, 3, UI_ACCENT);
    draw_text(pixels, stride, 92, 202, "周一到周日  |  点按模式或额度区直接修改", 18, UI_MUTED);
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
        fill_round_rect(pixels, stride, (UiRect){mode.x + 6, mode.y + 4, mode.width - 12, 28}, 14,
                        model->disable_flag_present ? UI_BORDER :
                        (limited ? UI_ACCENT : UI_SUCCESS));
        draw_text_center(pixels, stride, (UiRect){mode.x + 6, mode.y + 4, mode.width - 12, 28},
                         limited ? "限时" : "不限时", 15,
                         model->disable_flag_present ? UI_DISABLED : UI_ON_ACCENT);
        int pill_bottom = mode.y + 4 + 28;
        int minutes_top = minutes.y + 16;
        UiRect today_rect = {card.x, pill_bottom, card.width, minutes_top - pill_bottom};
        draw_text_center(pixels, stride, today_rect,
                         today ? "今天" : " ", 15, today ? UI_SUCCESS : UI_MUTED);
        if (limited) {
            snprintf(detail, sizeof(detail), "%u", (unsigned int)model->draft_week[day].minutes);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 16, minutes.width, 40}, detail, 28,
                             model->disable_flag_present ? UI_DISABLED : UI_ACCENT);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 55, minutes.width, 24}, "分钟", 14, UI_MUTED);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 79, minutes.width, 20}, "A / 点按", 12,
                             model->disable_flag_present ? UI_DISABLED : UI_MUTED);
        } else {
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 29, minutes.width, 36}, "不限时间", 18,
                             model->disable_flag_present ? UI_DISABLED : UI_SUCCESS);
            draw_text_center(pixels, stride, (UiRect){minutes.x, minutes.y + 72, minutes.width, 20}, "点按看提示", 11, UI_DISABLED);
        }
        /* 每日容量柱状直方图 (Weekly Capacity Histogram Bar) */
        int bar_w = card.width - 24;
        int bar_x = card.x + 12;
        int bar_y = card.y + card.height - 11;
        UiRect bar_bg = {bar_x, bar_y, bar_w, 4};
        fill_round_rect(pixels, stride, bar_bg, 2, UI_BORDER);
        if (limited) {
            int fill_w = (int)((int64_t)bar_w * (model->draft_week[day].minutes > 180 ? 180 : model->draft_week[day].minutes) / 180);
            if (fill_w < 4 && model->draft_week[day].minutes > 0) fill_w = 4;
            uint32_t bar_col = model->disable_flag_present ? UI_DISABLED :
                (model->draft_week[day].minutes <= 30 ? UI_WARNING : UI_ACCENT);
            if (fill_w > 0) {
                fill_round_rect(pixels, stride, (UiRect){bar_x, bar_y, fill_w, 4}, 2, bar_col);
            }
        } else {
            fill_round_rect(pixels, stride, bar_bg, 2, model->disable_flag_present ? UI_DISABLED : UI_SUCCESS);
        }
    }
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_WEEKLY, model->weekly_dirty,
                     (UiRect){838, 176, 388, 324});
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
    draw_notice(pixels, stride, model, 522, 128);
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
    UiRect panel = {838, 176, 384, 324};
    UiRect top_card = to_uirect(ptc_ui_holiday_card_rect(0));
    const char *titles[] = {"法定休假", "调休工作日"};
    const char *descriptions[] = {"主要法定节假日的休假日期", "节假日调休产生的补班日期"};
    char line[160];
    char minutes_str[64];
    bool disabled = model->disable_flag_present;
    bool top_selected = model->selected_index == 0;
    fill_round_rect(pixels, stride, top_card, 16, disabled ? UI_PAGE : (top_selected ? UI_ACCENT_SOFT : UI_SURFACE));
    draw_rect_outline(pixels, stride, top_card, 16, top_selected ? 3 : 1, top_selected ? UI_ACCENT : UI_BORDER);
    draw_text(pixels, stride, top_card.x + 18, top_card.y + 28, "国家节假日规则", 20, UI_INK);
    draw_text(pixels, stride, top_card.x + 18, top_card.y + 54, "开启后自动应用法定休假与调休工作日规则", 14, UI_MUTED);
    {
        const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
        snprintf(line, sizeof(line), "内置日历：%u  |  v%u", (unsigned int)info->last_year, (unsigned int)info->version);
        draw_text(pixels, stride, top_card.x + 498, top_card.y + 31, line, 14,
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
        draw_text(pixels, stride, card.x + 16, card.y + 30, titles[index], 19, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, card.x + 16, card.y + 57, descriptions[index], 12, UI_MUTED);
        fill_round_rect(pixels, stride, mode, 18,
                        disabled ? UI_BORDER : (limited ? UI_ACCENT : UI_SUCCESS));
        draw_text_center(pixels, stride, mode, limited ? "限时" : "不限时", 14,
                         disabled ? UI_DISABLED : UI_ON_ACCENT);
        fill_round_rect(pixels, stride, minutes, 12, disabled || !limited ? UI_RAISED : UI_RAISED);
        if (limited) {
            snprintf(minutes_str, sizeof(minutes_str), "%u 分钟（%u小时%u分）", (unsigned int)rule.minutes,
                     (unsigned int)rule.minutes / 60, (unsigned int)rule.minutes % 60);
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 32, minutes_str, 21,
                      disabled ? UI_DISABLED : UI_RGB(UI_BLENDED(accent)));
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 61, "A / 点按修改额度", 13,
                      disabled ? UI_DISABLED : UI_MUTED);
        } else {
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 32, "不限时间", 21, UI_DISABLED);
            draw_text(pixels, stride, minutes.x + 14, minutes.y + 61, "点按后提示先切换为限时", 13, UI_DISABLED);
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
    draw_notice(pixels, stride, model, 522, 128);
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

    /* 独立醒目的就寝时间总开关卡片 (Bedtime Master Switch Card) */
    UiRect master_card = to_uirect(ptc_ui_bedtime_master_switch_rect());
    draw_card_shadow(pixels, stride, master_card, 16);
    fill_round_rect(pixels, stride, master_card, 16, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, master_card, 16, 1, UI_RGB(UI_BLENDED(border_control)));
    draw_text(pixels, stride, master_card.x + 18, master_card.y + 28, "就寝时间总开关", 20, UI_INK);
    draw_text(pixels, stride, master_card.x + 18, master_card.y + 54,
              draft->enabled ? "开启 (就寝限制生效中)" : "关闭 (停用全部就寝限制)", 13,
              draft->enabled ? UI_SUCCESS : UI_MUTED);
    UiRect toggle_rect = {master_card.x + master_card.width - 76, master_card.y + (master_card.height - 30) / 2, 60, 30};
    draw_toggle_switch(pixels, stride, toggle_rect, draft->enabled, false, model->disable_flag_present, NULL, NULL);
    draw_text(pixels, stride, master_card.x + master_card.width - 92, master_card.y + master_card.height - 8,
              "Y / 点按切换", 11, UI_MUTED);

    /* 状态与保存提示胶囊 */
    UiRect status_pill = {838, 258, 388, 34};
    fill_round_rect(pixels, stride, status_pill, 10, model->bedtime_dirty ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
    draw_rect_outline(pixels, stride, status_pill, 10, 1, model->bedtime_dirty ? UI_WARNING : UI_SUCCESS);
    draw_text_center(pixels, stride, status_pill,
                     model->bedtime_dirty ? "草稿尚未保存（按 + 保存生效）" : "✓ 计划已保存", 15,
                     model->bedtime_dirty ? UI_WARNING : UI_SUCCESS);

    draw_wrapped_text(pixels, stride, 838, 304,
        "就寝时间与每日额度并行生效。到点强制限制，不受额度多寡影响；指定日期就寝只改起止时间。",
        14, 388, 20, 3, UI_MUTED);

    draw_text(pixels, stride, 838, 372, "操作指南", 16, UI_INK);
    draw_text(pixels, stride, 838, 398, "方向键选择  |  L / R 切换区段", 14, UI_MUTED);
    draw_text(pixels, stride, 838, 420, "A 打开编辑  |  X 快速开关当前日", 14, UI_MUTED);
    draw_text(pixels, stride, 838, 442, "Y 键 / 点按右上角 切换总开关", 14, UI_ACCENT);
    draw_text(pixels, stride, 838, 464, "+ 保存生效  |  ZL 放弃修改", 14, UI_MUTED);

    draw_wrapped_text(pixels, stride, 838, 492,
        model->bedtime_official_setting_confirmed
            ? (model->bedtime_overlay_verified
                ? "官方暂停设置已确认，Overlay 已验证。"
                : "官方暂停设置已确认；保存即接受 Overlay 尚未验证的恢复风险。")
            : "首次启用前，请确认 Nintendo 家长控制的“时间到了暂停软件”已开启；保存即确认并接受 Overlay 恢复风险。",
        13, 388, 19, 2,
        model->bedtime_official_setting_confirmed && model->bedtime_overlay_verified
            ? UI_SUCCESS : UI_WARNING);

    if (model->bedtime_section == PTC_UI_BEDTIME_WEEKLY) {
        for (int slot = 0; slot < 7; ++slot) {
            int day = ptc_ui_weekday_for_display_slot(slot);
            UiRect card = to_uirect(ptc_ui_bedtime_field_rect(PTC_UI_BEDTIME_WEEKLY, slot));
            char value[64];
            draw_plan_card(pixels, stride, card,
                model->selected_index == slot && !model->bedtime_section_focused && !model->parent_footer_focused);
            draw_text_center(pixels, stride, (UiRect){card.x, card.y + 12, card.width, 26}, DAYS[day], 17, UI_INK);
            draw_bedtime_window_value(value, sizeof(value), &draft->week[day]);
            if (draft->week[day].enabled) {
                snprintf(line, sizeof(line), "%02u:%02u",
                    (unsigned int)(draft->week[day].start_minute / 60),
                    (unsigned int)(draft->week[day].start_minute % 60));
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 48, card.width, 26}, line, 18,
                                 draft->enabled ? UI_ACCENT : UI_MUTED);
                snprintf(line, sizeof(line), "到 %02u:%02u",
                    (unsigned int)(draft->week[day].end_minute / 60),
                    (unsigned int)(draft->week[day].end_minute % 60));
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 74, card.width, 22}, line, 13, UI_MUTED);

                /* 开启状态微标 */
                UiRect pill = {card.x + (card.width - 48) / 2, card.y + 104, 48, 20};
                fill_round_rect(pixels, stride, pill, 6, draft->enabled ? UI_ACCENT_SOFT : UI_RAISED);
                draw_text_center(pixels, stride, pill, draft->enabled ? "开启" : "暂停", 12,
                                 draft->enabled ? UI_ACCENT : UI_MUTED);
            } else {
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 64, card.width, 28}, "关闭", 18, UI_MUTED);
                UiRect pill = {card.x + (card.width - 48) / 2, card.y + 104, 48, 20};
                fill_round_rect(pixels, stride, pill, 6, UI_RAISED);
                draw_text_center(pixels, stride, pill, "关闭", 12, UI_MUTED);
            }
            draw_text_center(pixels, stride, (UiRect){card.x, card.y + 138, card.width, 22}, "A 编辑 | X 切换", 11, UI_MUTED);
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
    } else if (model->bedtime_section == PTC_UI_BEDTIME_CALENDAR) {
        UiRect master = to_uirect(ptc_ui_bedtime_field_rect(1, 0));
        draw_plan_card(pixels, stride, master, model->selected_index == 0 && !model->bedtime_section_focused);
        draw_text(pixels, stride, master.x + 20, master.y + 40, "国家节假日日历", 20, UI_INK);
        draw_text(pixels, stride, master.x + 490, master.y + 40,
            draft->calendar_enabled ? "开启" : "关闭", 18,
            draft->calendar_enabled ? UI_SUCCESS : UI_MUTED);
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
            draw_text(pixels, stride, row.x + 18, row.y + 31, labels[i], 17, UI_MUTED);
            draw_text(pixels, stride, row.x + 240, row.y + 31, values[i], 18, UI_INK);
            if (i == 1 || i == 2)
                draw_text(pixels, stride, row.x + 484, row.y + 31,
                          "A 输入 | ZL/ZR ±7 天", 11, UI_MUTED);
        }
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(2, 4), "ZL  放弃",
            UI_PAGE, UI_INK, model->selected_index == 4 && !model->bedtime_section_focused, !model->bedtime_dirty);
        draw_candidate_button(pixels, stride, ptc_ui_bedtime_field_rect(2, 5), "+  保存",
            UI_ACCENT, UI_ON_ACCENT, model->selected_index == 5 && !model->bedtime_section_focused,
            !model->bedtime_dirty || model->disable_flag_present || model->waiting);
    }
    draw_notice(pixels, stride, model, 554, 96);
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
    UiRect panel = {824, 164, 402, 332};
    int index;
    fill_round_rect(pixels, stride, panel, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    draw_text(pixels, stride, panel.x + 16, panel.y + 24, "未来 7 天计划与就寝预测", 17, UI_INK);

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
        UiRect row = {panel.x + 10, panel.y + 34 + index * 37, panel.width - 20, 34};
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
        draw_text(pixels, stride, row.x + 8, row.y + 22, date_label, 13,
                  is_today ? UI_ACCENT : UI_INK);

        /* 2. 额度与微进度条 */
        int quota_x = row.x + 94;
        if (day->mode == PTC_RULE_MODE_UNLIMITED) {
            draw_text(pixels, stride, quota_x, row.y + 16, "不限时", 13, UI_SUCCESS);
            fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 22, 64, 4}, 2, UI_SUCCESS);
        } else {
            char q_str[24];
            snprintf(q_str, sizeof(q_str), "%u 分钟", (unsigned int)day->minutes);
            draw_text(pixels, stride, quota_x, row.y + 16, q_str, 13, UI_INK);
            int bar_w = 64;
            int fill_w = (int)((int64_t)bar_w * (day->minutes > 180 ? 180 : day->minutes) / 180);
            if (fill_w < 3 && day->minutes > 0) fill_w = 3;
            uint32_t bar_col = day->minutes <= 30 ? UI_WARNING : UI_ACCENT;
            fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 22, bar_w, 4}, 2, UI_BORDER);
            if (fill_w > 0) {
                fill_round_rect(pixels, stride, (UiRect){quota_x, row.y + 22, fill_w, 4}, 2, bar_col);
            }
        }

        /* 3. 规则来源胶囊徽标 */
        UiRect badge = {row.x + 172, row.y + 7, 56, 20};
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
        int bt_x = row.x + 236;
        if (!model->bedtime_policy.enabled || !bedtime.window.enabled) {
            draw_text(pixels, stride, bt_x + 12, row.y + 22, "无就寝", 12, UI_MUTED);
        } else {
            char bt_buf[48];
            snprintf(bt_buf, sizeof(bt_buf), "🌙 %02u:%02u %s",
                     (unsigned int)(bedtime.window.start_minute / 60),
                     (unsigned int)(bedtime.window.start_minute % 60),
                     bedtime_source_short(bedtime.source));
            draw_text(pixels, stride, bt_x, row.y + 22, bt_buf, 12, UI_MUTED);
        }
    }

    draw_text(pixels, stride, panel.x + 14, panel.y + 316,
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
            UiRect quota_zone = {42, 164, 388, 332};
            UiRect parallel_zone = {428, 164, 388, 332};
            fill_round_rect(pixels, stride, quota_zone, 16, UI_RAISED);
            draw_rect_outline(pixels, stride, quota_zone, 16, 1, UI_BORDER);
            UiRect qbadge = {54, 170, 76, 20};
            fill_round_rect(pixels, stride, qbadge, 6, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, qbadge, 6, 1, UI_ACCENT);
            draw_text_center(pixels, stride, qbadge, "额度规则", 12, UI_ACCENT);
            draw_text(pixels, stride, 138, 185, "优先级自上而下逐级生效", 13, UI_MUTED);

            fill_round_rect(pixels, stride, parallel_zone, 16, UI_RAISED);
            draw_rect_outline(pixels, stride, parallel_zone, 16, 1, UI_BORDER);
            UiRect pbadge = {440, 170, 76, 20};
            fill_round_rect(pixels, stride, pbadge, 6, UI_WARNING_SOFT);
            draw_rect_outline(pixels, stride, pbadge, 6, 1, UI_WARNING);
            draw_text_center(pixels, stride, pbadge, "并行补充", 12, UI_WARNING);
            draw_text(pixels, stride, 524, 185, "独立于额度规则链生效", 13, UI_MUTED);

            /* 并行与补充下方对称说明卡片 */
            UiRect info_card = {439, 396, 365, 78};
            fill_round_rect(pixels, stride, info_card, 16, UI_PAGE);
            draw_rect_outline(pixels, stride, info_card, 16, 1, UI_BORDER);
            draw_text(pixels, stride, 455, 418, "规则独立生效机制", 14, UI_INK);
            draw_text(pixels, stride, 455, 440, "就寝时间：到点强制限制，不受额度多寡影响", 12, UI_MUTED);
            draw_text(pixels, stride, 455, 460, "自主缓冲：仅限时日额度耗尽前由孩子申请", 12, UI_MUTED);
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
                const char *detail = "重新检查后才能修改";
                dynamic_action = *action;
                if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_OFF) {
                    detail = "自制程序菜单高级入口未配置";
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_CONFIGURED) {
                    detail = "需按住 X，再按 A 进入自制程序菜单";
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_ANOMALY) {
                    detail = "检测到 PlayWise 事务异常，请查看详情";
                } else if (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL) {
                    detail = "入口已可用，但不是由 PlayWise 配置";
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
            }
            int reserved_right = (model->parent_page == PTC_UI_PARENT_SETTINGS && index == 3) ? 100 :
                                 (model->parent_page == PTC_UI_PARENT_PLAN ? 80 : 0);
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
            /* 胶囊 1: 优先于节假日规则 (位于卡片 0 底部 270 与卡片 1 顶部 294 之间，无重叠) */
            UiRect pill0 = {54 + 20, 273, 152, 18};
            fill_round_rect(pixels, stride, pill0, 9, UI_PAGE);
            draw_rect_outline(pixels, stride, pill0, 9, 1, UI_BORDER);
            draw_text_center(pixels, stride, pill0, "▼ 优先于节假日规则", 11, UI_ACCENT);

            /* 胶囊 2: 优先于每周常规计划 (位于卡片 1 底部 372 与卡片 2 顶部 396 之间，无重叠) */
            UiRect pill1 = {54 + 20, 375, 152, 18};
            fill_round_rect(pixels, stride, pill1, 9, UI_PAGE);
            draw_rect_outline(pixels, stride, pill1, 9, 1, UI_BORDER);
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
    if (model->parent_page == PTC_UI_PARENT_SUPPORT &&
        model->diagnostic_status != PTC_UI_DIAGNOSTIC_IDLE) {
        draw_diagnostic_notice(pixels, stride, model);
    } else if (model->parent_page == PTC_UI_PARENT_TODAY) {
        draw_home_notice(pixels, stride, model);
    } else if (!plan_subpage && model->parent_page != PTC_UI_PARENT_PLAN) {
        draw_notice(pixels, stride, model, 522, 128);
    }
    if (model->parent_page == PTC_UI_PARENT_SETTINGS) {
        UiRect help = {842, 176, 384, 324};
        draw_plan_card(pixels, stride, help, false);
        draw_text(pixels, stride, 866, 216, "系统安全与个人偏好", 24, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, 866, 266, "时间规则已集中到时间计划", 17, UI_RGB(UI_BLENDED(text_secondary)));
        draw_text(pixels, stride, 866, 310, "这里管理外观、PIN、快捷键", 17, UI_RGB(UI_BLENDED(text_secondary)));
        draw_text(pixels, stride, 866, 344, "自制程序入口与家庭活动记录", 17, UI_RGB(UI_BLENDED(text_secondary)));
        draw_text(pixels, stride, 866, 408, "设备异常请直接打开支持与恢复", 17, UI_WARNING);
    }
    draw_settings_badge(pixels, stride, model);
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(0),
                       plan_subpage ? "" : "L  上一页");
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(1),
                       plan_subpage ? "" : "R  下一页");
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(2),
                       plan_subpage ? "B  返回计划" : "B  返回孩子页");
    draw_footer_button(pixels, stride, ptc_ui_parent_footer_rect(3), "Y  刷新");
    if (model->parent_footer_focused && model->parent_footer_selection == 0) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_parent_footer_rect(3)), 12, 3, UI_ACCENT);
    }
    draw_parent_status_footer(pixels, stride, model);
}
