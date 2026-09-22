#include "ui_render_internal.h"

/* 24 小时可视化时间轴使用的家庭常用就寝窗口。 */
static const struct {
    const char *name;
    uint16_t start;
    uint16_t end;
} PTC_BEDTIME_PRESETS[] = {
    {"早睡型", 21 * 60, 7 * 60},
    {"标准型", 21 * 60 + 30, 7 * 60},
    {"均衡型", 22 * 60, 7 * 60},
    {"周末型", 22 * 60 + 30, 7 * 60 + 30},
    {"宽松型", 23 * 60, 8 * 60}
};

static void draw_scheduled_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcScheduledOverride *draft = &model->draft_scheduled_override;
    uint32_t duration = draft->end_day_index >= draft->start_day_index
        ? (uint32_t)draft->end_day_index - draft->start_day_index + 1u : 1u;
    uint16_t s_year = 0, e_year = 0;
    uint8_t s_month = 0, s_day = 0, e_month = 0, e_day = 0;
    bool s_ok = ptc_date_from_day_index(draft->start_day_index, &s_year, &s_month, &s_day);
    bool e_ok = ptc_date_from_day_index(draft->end_day_index, &e_year, &e_month, &e_day);
    static const char *LABELS[] = {"计划状态", "开始日期", "持续天数", "每天额度"};
    char values[4][128];
    char banner_text[192];
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 640);

    snprintf(values[0], sizeof(values[0]), "%s",
             draft->enabled ? "开启（计划生效中）" : "关闭（暂不生效）");
    if (s_ok) {
        snprintf(values[1], sizeof(values[1]), "%04u-%02u-%02u%s", s_year, s_month, s_day,
                 model->status_loaded && draft->start_day_index == model->day_index ? "（今天）" : "");
    } else {
        snprintf(values[1], sizeof(values[1]), "暂不可用");
    }
    snprintf(values[2], sizeof(values[2]), "%u 天（最长 366 天）", (unsigned int)duration);
    if (draft->rule.mode == PTC_RULE_MODE_UNLIMITED) {
        snprintf(values[3], sizeof(values[3]), "不限时");
    } else {
        snprintf(values[3], sizeof(values[3]), "%u 分钟（%u小时%u分）",
                 (unsigned int)draft->rule.minutes,
                 (unsigned int)draft->rule.minutes / 60,
                 (unsigned int)draft->rule.minutes % 60);
    }

    for (int index = 0; index < 4; ++index) {
        UiRect row = to_uirect(ptc_ui_scheduled_field_rect(index));
        draw_plan_card(pixels, stride, row, model->overlay_selection == index);
        draw_text(pixels, stride, row.x + 18, row.y + 38, LABELS[index], 16, UI_MUTED);

        if (index == 0) {
            draw_text(pixels, stride, row.x + 130, row.y + 38, values[0], 20,
                       draft->enabled ? UI_SUCCESS : UI_MUTED);
            UiRect toggle_rect = {row.x + row.width - 78, row.y + (row.height - 30) / 2, 60, 30};
            draw_toggle_switch(pixels, stride, toggle_rect, draft->enabled,
                               model->overlay_selection == 0, model->disable_flag_present, NULL, NULL);
        } else if (index == 1 || index == 2) {
            draw_text(pixels, stride, row.x + 130, row.y + 38, values[index], 20, UI_RGB(UI_BLENDED(text_primary)));
            UiRect edit_chip = {row.x + row.width - 86, row.y + (row.height - 28) / 2, 70, 28};
            fill_round_rect(pixels, stride, edit_chip, 6, UI_RGB(UI_BLENDED(surface)));
            draw_rect_outline(pixels, stride, edit_chip, 6, 1, UI_BORDER);
            draw_text_center(pixels, stride, edit_chip, "A 编辑", 13, UI_MUTED);
        } else {
            draw_text(pixels, stride, row.x + 130, row.y + 38, values[index], 20,
                       draft->rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT);
            UiRect edit_chip = {row.x + row.width - 86, row.y + (row.height - 28) / 2, 70, 28};
            fill_round_rect(pixels, stride, edit_chip, 6, UI_RGB(UI_BLENDED(surface)));
            draw_rect_outline(pixels, stride, edit_chip, 6, 1, UI_BORDER);
            draw_text_center(pixels, stride, edit_chip, "A 编辑", 13, UI_MUTED);
        }
    }

    /* 右侧预估面板 */
    draw_plan_impact(pixels, stride, model, PTC_UI_PLAN_SCHEDULED, ptc_ui_scheduled_dirty(model),
                     (UiRect){dialog.x + 626, dialog.y + 116, 460, 306});

    /* 日期区间视觉横幅 (Date Range Visual Banner) */
    UiRect banner = {dialog.x + 34, dialog.y + 424, dialog.width - 68, 44};
    fill_round_rect(pixels, stride, banner, 10, UI_RGB(UI_BLENDED(surface)));
    draw_rect_outline(pixels, stride, banner, 10, 1, UI_RGB(UI_BLENDED(border_control)));
    if (s_ok && e_ok) {
        bool today_in_range = model->status_loaded &&
            model->day_index >= draft->start_day_index &&
            model->day_index <= draft->end_day_index;
        const char *range_status = draft->enabled
            ? (today_in_range ? " | 今天在区间内（生效中）" :
               (model->day_index < draft->start_day_index ? " | 计划将在未来生效" : " | 计划已到期"))
            : " | 计划总开关未开启";
        snprintf(banner_text, sizeof(banner_text),
                 "计划区间：%04u-%02u-%02u 至 %04u-%02u-%02u（含当天共 %u 天）%s",
                 s_year, s_month, s_day, e_year, e_month, e_day, (unsigned int)duration, range_status);
        draw_text(pixels, stride, banner.x + 16, banner.y + 27, banner_text, 15,
                  draft->enabled && today_in_range ? UI_SUCCESS : UI_RGB(UI_BLENDED(text_primary)));
    } else {
        draw_text(pixels, stride, banner.x + 16, banner.y + 27,
                  "计划区间计算中，请先设定有效起止日期", 15, UI_MUTED);
    }

    /* 规则优先级说明 */
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 482,
              "规则链优先级：今日额度调整 > 临时额度计划 > 国家节假日 > 周计划（就寝时间独立并行）",
              14, UI_RGB(UI_BLENDED(text_secondary)));

    /* 动态上下文按键引导栏 (Context-Aware Action Guide Bar) */
    UiRect guide_bar = {dialog.x + 34, dialog.y + 508, dialog.width - 68, 30};
    fill_round_rect(pixels, stride, guide_bar, 6, UI_RGB(UI_BLENDED(surface_raised)));
    draw_rect_outline(pixels, stride, guide_bar, 6, 1, UI_BORDER);

    char guide_text[160];
    if (model->overlay_selection == 0) {
        snprintf(guide_text, sizeof(guide_text), "操作：按 A 或点按切换计划开启/停用状态");
    } else if (model->overlay_selection == 1) {
        snprintf(guide_text, sizeof(guide_text), "操作：左右键 ±1 天  •  ZL / ZR ±7 天  •  A 键盘输入开始日期");
    } else if (model->overlay_selection == 2) {
        snprintf(guide_text, sizeof(guide_text), "操作：左右键 ±1 天  •  ZL / ZR ±7 天  •  A 键盘输入天数 (1~366)");
    } else if (model->overlay_selection == 3) {
        snprintf(guide_text, sizeof(guide_text), "操作：X 切换不限时/限时模式  •  A 键盘设定每日分钟数");
    } else {
        snprintf(guide_text, sizeof(guide_text), "操作：上下键选择字段  •  + 保存草稿  •  B 返回");
    }
    draw_text(pixels, stride, guide_bar.x + 12, guide_bar.y + 20, guide_text, 13, UI_ACCENT);

    if (strcmp(model->result_status, "error") == 0) {
        draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 538,
                          "上次操作未完成，草稿仍保留。可重试，或放弃草稿后到支持与恢复检查。",
                          16, dialog.width - 68, 22, 2, UI_RGB(UI_BLENDED(danger)));
    }

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回", UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       model->waiting ? "正在保存..." : (ptc_ui_scheduled_dirty(model) ? "+  保存草稿" : "已保存"),
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_scheduled_leave(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel copy = *model;
    snprintf(copy.overlay_title, sizeof(copy.overlay_title), "放弃临时额度计划草稿？");
    snprintf(copy.overlay_body, sizeof(copy.overlay_body), "尚未保存的修改会丢失，已保存的计划不变。");
    draw_dialog_shell(pixels, stride, &copy, &dialog, 720, 300);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  继续编辑",
                       UI_RAISED, UI_ACCENT, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  放弃草稿",
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_autonomy_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    static const uint16_t OPTIONS[] = {0, 5, 10, 15};
    static const char *SUBTITLES[] = {"不开放缓冲", "快速存盘", "收尾推荐", "充裕退出"};
    int index;
    char label[48];
    PtcUiModel shell_model = *model;
    shell_model.overlay_body[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 880, 480);

    /* 详细用途与机制说明 */
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 92,
        "用途说明：在额度耗尽时允许孩子自主启用缓冲用于存盘收尾，避免强退丢失进度。",
        14, UI_INK);
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 118,
        "每天限领一次，仅限限时日；领取失败不扣除资格。",
        14, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 144,
        "推荐设置：建议 5 到 15 分钟；既能从容存盘，又保持健康作息规律。",
        14, UI_ACCENT);

    for (index = 0; index < 4; ++index) {
        UiRect option = to_uirect(ptc_ui_autonomy_option_rect(index));
        bool selected = model->draft_autonomy_policy.daily_buffer_minutes == OPTIONS[index];
        if (OPTIONS[index] > 0u) {
            snprintf(label, sizeof(label), "%u 分钟", (unsigned int)OPTIONS[index]);
        } else {
            snprintf(label, sizeof(label), "关闭");
        }
        fill_round_rect(pixels, stride, option, 12, selected ? UI_SUCCESS_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 12, selected ? 2 : 1, selected ? UI_SUCCESS : UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){option.x, option.y + 12, option.width, 32},
                         label, 20, selected ? UI_SUCCESS : UI_INK);
        draw_text_center(pixels, stride, (UiRect){option.x, option.y + 46, option.width, 24},
                         SUBTITLES[index], 13, selected ? UI_SUCCESS : UI_MUTED);
    }

    /* 底部保障与生效提示卡片 */
    UiRect tip_box = {dialog.x + 48, dialog.y + 296, dialog.width - 96, 76};
    fill_round_rect(pixels, stride, tip_box, 8, UI_PAGE);
    draw_rect_outline(pixels, stride, tip_box, 8, 1, UI_BORDER);
    draw_text(pixels, stride, tip_box.x + 16, tip_box.y + 28,
        "缓冲只增加今日额度，不改变就寝窗口；", 14, UI_INK);
    draw_text(pixels, stride, tip_box.x + 16, tip_box.y + 54,
        "就寝时间到点仍按计划限制。", 14, UI_WARNING);

    draw_overlay_actions(pixels, stride, model, "+  保存缓冲设置");
}

static void draw_quick_add_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    static const char *LABELS[] = {"+15 分钟", "+30 分钟", "+60 分钟", "自定义"};
    draw_dialog_shell(pixels, stride, model, &dialog, 760, 420);
    for (int index = 0; index < 4; ++index) {
        UiRect option = to_uirect(ptc_ui_quick_add_option_rect(index));
        bool selected = model->overlay_selection == index;
        fill_round_rect(pixels, stride, option, 12, selected ? UI_SUCCESS_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 12, selected ? 2 : 1,
            selected ? UI_SUCCESS : UI_BORDER);
        draw_text_center(pixels, stride, option, LABELS[index], 20,
            selected ? UI_SUCCESS : UI_INK);
    }
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 284,
        "选择后还会显示确认页；自定义范围为 1 到 120 分钟。", 16, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  取消",
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  继续",
        UI_SUCCESS, UI_ON_ACCENT, false);
}

static void draw_bedtime_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcBedtimePolicy *draft = &model->draft_bedtime_policy;
    char values[4][96];
    draw_dialog_shell(pixels, stride, model, &dialog, 860, 540);
    snprintf(values[0], sizeof(values[0]), "就寝计划：%s", draft->enabled ? "开启" : "关闭");
    snprintf(values[1], sizeof(values[1]), "每天开始：%02u:%02u",
        (unsigned int)(draft->week[0].start_minute / 60),
        (unsigned int)(draft->week[0].start_minute % 60));
    snprintf(values[2], sizeof(values[2]), "次日结束：%02u:%02u",
        (unsigned int)(draft->week[0].end_minute / 60),
        (unsigned int)(draft->week[0].end_minute % 60));
    snprintf(values[3], sizeof(values[3]), "%s",
        model->bedtime_active && !model->bedtime_skipped
            ? "生效中：仅可从 Overlay 或任天堂原生弹窗恢复"
            : "限制生效后，PlayWise NRO、HOME 与系统设置均不可访问");
    for (int index = 0; index < 4; ++index) {
        UiRect row = {dialog.x + 42, dialog.y + 118 + index * 70, dialog.width - 84, 56};
        draw_plan_card(pixels, stride, row, model->overlay_selection == index);
        draw_text(pixels, stride, row.x + 18, row.y + 35, values[index], 20,
            index == 3 && model->bedtime_active && !model->bedtime_skipped ? UI_WARNING : UI_INK);
    }
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 402,
        "方向键选择；A 打开小时/分钟快速编辑器；+ 保存并立即生效。", 16, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 434,
        model->bedtime_official_setting_confirmed
            ? (model->bedtime_overlay_verified ? "环境已确认，Overlay 已验证。" : "环境已确认；继续即接受 Overlay 尚未验证的风险。")
            : "首次启用须确认任天堂家长控制已开启“暂停软件”；按 + 确认后再按 + 保存。",
        14, model->bedtime_official_setting_confirmed ? UI_MUTED : UI_WARNING);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "+  保存并生效",
        UI_WARNING, UI_ON_ACCENT, false);
}

static void draw_bedtime_timeline_strip(
    uint32_t *pixels, uint32_t stride,
    UiRect bar, uint16_t start_m, uint16_t end_m, bool enabled)
{
    fill_round_rect(pixels, stride, bar, 6, UI_PAGE);
    draw_rect_outline(pixels, stride, bar, 6, 1, UI_BORDER);

    if (!enabled) {
        draw_text_center(pixels, stride, bar, "本日就寝限制未开启（全天夜间不设强制立断）", 13, UI_MUTED);
    } else {
        int x_morn_end = bar.x + (int)((float)end_m / 1440.0f * (float)bar.width + 0.5f);
        int x_eve_start = bar.x + (int)((float)start_m / 1440.0f * (float)bar.width + 0.5f);
        if (x_morn_end < bar.x) x_morn_end = bar.x;
        if (x_morn_end > bar.x + bar.width) x_morn_end = bar.x + bar.width;
        if (x_eve_start < bar.x) x_eve_start = bar.x;
        if (x_eve_start > bar.x + bar.width) x_eve_start = bar.x + bar.width;

        /* 清晨就寝区间 [bar.x, x_morn_end] */
        if (x_morn_end > bar.x) {
            UiRect morn_rect = {bar.x, bar.y, x_morn_end - bar.x, bar.height};
            fill_round_rect(pixels, stride, morn_rect, 6, UI_RGB(0x403264));
            if (morn_rect.width > 44) {
                draw_text_center(pixels, stride, morn_rect, "清晨就寝", 11, UI_RGB(0xdfd8f5));
            }
        }

        /* 晚间就寝区间 [x_eve_start, bar.x + bar.width] */
        if (x_eve_start < bar.x + bar.width) {
            UiRect eve_rect = {x_eve_start, bar.y, bar.x + bar.width - x_eve_start, bar.height};
            fill_round_rect(pixels, stride, eve_rect, 6, UI_RGB(0x403264));
            if (eve_rect.width > 44) {
                draw_text_center(pixels, stride, eve_rect, "夜间就寝", 11, UI_RGB(0xdfd8f5));
            }
        }

        /* 白昼允许游玩区间 [x_morn_end, x_eve_start] */
        if (x_eve_start > x_morn_end) {
            UiRect day_rect = {x_morn_end, bar.y, x_eve_start - x_morn_end, bar.height};
            draw_text_center(pixels, stride, day_rect, "白昼允许游玩时段", 12, UI_RGB(UI_BLENDED(text_primary)));
        }

        /* 立断与解禁指示线 */
        if (x_eve_start >= bar.x && x_eve_start <= bar.x + bar.width) {
            fill_rect(pixels, stride, (UiRect){x_eve_start - 1, bar.y, 2, bar.height}, UI_DANGER);
        }
        if (x_morn_end >= bar.x && x_morn_end <= bar.x + bar.width) {
            fill_rect(pixels, stride, (UiRect){x_morn_end - 1, bar.y, 2, bar.height}, UI_SUCCESS);
        }
    }
}

static int bedtime_matching_preset(const PtcBedtimeWindow *window)
{
    if (!window) return -1;
    for (int preset = 0; preset < 5; ++preset) {
        if (window->start_minute == PTC_BEDTIME_PRESETS[preset].start &&
            window->end_minute == PTC_BEDTIME_PRESETS[preset].end)
            return preset;
    }
    return -1;
}

static void draw_bedtime_editor_fields(
    uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
    const PtcBedtimeWindow *window, const char *mode_label, uint32_t mode_color,
    bool custom, bool show_toggle)
{
    UiRect mode = to_uirect(ptc_ui_bedtime_overlay_field_rect(model->overlay, 0));
    draw_plan_card(pixels, stride, mode, model->overlay_selection == 0);
    draw_text(pixels, stride, mode.x + 18, mode.y + 36,
        show_toggle ? "本日就寝限制" : "规则模式", 18, UI_INK);
    draw_text(pixels, stride, mode.x + 170, mode.y + 36, mode_label, 18, mode_color);
    if (show_toggle) {
        UiRect toggle = {mode.x + mode.width - 76, mode.y + 15, 58, 28};
        draw_toggle_switch(pixels, stride, toggle, window->enabled,
            model->overlay_selection == 0, false, NULL, NULL);
        draw_text(pixels, stride, mode.x + mode.width - 174, mode.y + 36,
            "A / X 切换", 13, UI_MUTED);
    } else {
        draw_text(pixels, stride, mode.x + mode.width - 188, mode.y + 36,
            "A / X 切换模式", 13, UI_MUTED);
    }

    for (int field = 1; field < 3; ++field) {
        UiRect row = to_uirect(ptc_ui_bedtime_overlay_field_rect(model->overlay, field));
        unsigned int minute = field == 1 ? window->start_minute : window->end_minute;
        char value[32];
        draw_plan_card(pixels, stride, row, model->overlay_selection == field);
        draw_text(pixels, stride, row.x + 18, row.y + 24,
            field == 1 ? "就寝开始，到点立断" : "次日结束，恢复使用",
            13, custom ? UI_MUTED : UI_DISABLED);
        snprintf(value, sizeof(value), "%02u:%02u", minute / 60, minute % 60);
        draw_text(pixels, stride, row.x + 18, row.y + 52, value, 22,
            custom ? UI_ACCENT : UI_DISABLED);
        draw_text(pixels, stride, row.x + row.width - 108, row.y + 50,
            custom ? "A  精调" : "不可编辑", 13, custom ? UI_MUTED : UI_DISABLED);
    }
}

static void draw_bedtime_editor_timeline(
    uint32_t *pixels, uint32_t stride, const PtcUiModel *model,
    const PtcBedtimeWindow *window, bool custom, const char *neutral_text)
{
    UiRect bar = to_uirect(ptc_ui_bedtime_timeline_rect(model->overlay));
    int matched = custom ? bedtime_matching_preset(window) : -1;
    int tick_y = bar.y + bar.height + 16;
    char summary[112];

    if (custom) {
        draw_bedtime_timeline_strip(pixels, stride, bar,
            window->start_minute, window->end_minute, window->enabled);
        draw_text(pixels, stride, bar.x, tick_y, "00:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width / 4 - 14, tick_y, "06:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width / 2 - 14, tick_y, "12:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width * 3 / 4 - 14, tick_y, "18:00", 10, UI_MUTED);
        draw_text(pixels, stride, bar.x + bar.width - 32, tick_y, "24:00", 10, UI_MUTED);
        if (window->enabled) {
            int duration = (int)(window->end_minute + 1440 - window->start_minute) % 1440;
            snprintf(summary, sizeof(summary), "%s，夜间管控 %u 小时 %u 分",
                matched >= 0 ? PTC_BEDTIME_PRESETS[matched].name : "自定义时段",
                (unsigned int)(duration / 60), (unsigned int)(duration % 60));
        } else {
            snprintf(summary, sizeof(summary), "本日关闭，夜间不触发强制立断");
        }
        draw_text_center(pixels, stride,
            (UiRect){bar.x + 150, tick_y - 14, bar.width - 300, 28},
            summary, 12, window->enabled ? UI_ACCENT : UI_MUTED);
    } else {
        fill_round_rect(pixels, stride, bar, 6, UI_PAGE);
        draw_rect_outline(pixels, stride, bar, 6, 1, UI_BORDER);
        draw_text_center(pixels, stride, bar, neutral_text, 13, UI_MUTED);
    }

    for (int preset = 0; preset < 5; ++preset) {
        UiRect chip = to_uirect(ptc_ui_bedtime_preset_rect(model->overlay, preset));
        bool selected = custom && matched == preset;
        char times[32];
        fill_round_rect(pixels, stride, chip, 9,
            selected ? UI_ACCENT_SOFT : (custom ? UI_RAISED : UI_PAGE));
        draw_rect_outline(pixels, stride, chip, 9, selected ? 2 : 1,
            selected ? UI_ACCENT : UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){chip.x, chip.y + 3, chip.width, 18},
            PTC_BEDTIME_PRESETS[preset].name, 12,
            custom ? (selected ? UI_ACCENT : UI_INK) : UI_DISABLED);
        snprintf(times, sizeof(times), "%02u:%02u 至 %02u:%02u",
            PTC_BEDTIME_PRESETS[preset].start / 60, PTC_BEDTIME_PRESETS[preset].start % 60,
            PTC_BEDTIME_PRESETS[preset].end / 60, PTC_BEDTIME_PRESETS[preset].end % 60);
        draw_text_center(pixels, stride, (UiRect){chip.x, chip.y + 21, chip.width, 18},
            times, 10, custom ? UI_MUTED : UI_DISABLED);
    }
}

static void draw_bedtime_window_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcBedtimeWindow *window = &model->draft_bedtime_policy.week[model->bedtime_editor_day];
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 560);
    draw_bedtime_editor_fields(pixels, stride, model, window,
        window->enabled ? "开启" : "关闭", window->enabled ? UI_SUCCESS : UI_MUTED,
        true, true);
    draw_bedtime_editor_timeline(pixels, stride, model, window, true, "");
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 424,
        "方向键选择字段，A 精调或切换，X 开关，Y 轮换预设，也可直接触摸预设", 13, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 450,
        "就寝窗口必须跨越午夜；到达开始时间会立即按计划限制。", 13, UI_WARNING);

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "+  完成",
        UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_bedtime_special_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcBedtimeSpecialRule *rule = model->bedtime_special_kind == 0
        ? &model->draft_bedtime_policy.holiday_rule
        : (model->bedtime_special_kind == 1
            ? &model->draft_bedtime_policy.makeup_workday_rule
            : &model->draft_bedtime_policy.scheduled_override.rule);
    bool custom = rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM;
    uint32_t mode_color = custom ? UI_ACCENT :
        (rule->mode == PTC_BEDTIME_OVERRIDE_DISABLED ? UI_MUTED : UI_SUCCESS);
    const char *neutral = rule->mode == PTC_BEDTIME_OVERRIDE_DISABLED
        ? "特殊免控：当天夜间不设置就寝限制，仅按全天额度管控"
        : "跟随周计划：自动继承对应星期的就寝时间窗口";
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 560);
    draw_bedtime_editor_fields(pixels, stride, model, &rule->window,
        bedtime_override_label(rule->mode), mode_color, custom, false);
    draw_bedtime_editor_timeline(pixels, stride, model, &rule->window,
        custom, neutral);
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 424,
        custom
            ? "方向键选择字段，A 精调或切换，X 切换模式，Y 轮换预设，可直接触摸预设"
            : "A / X 切换模式；选择“自定义窗口”后可编辑时间并使用预设。",
        13, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 44, dialog.y + 450,
        custom ? "自定义窗口必须跨越午夜；到达开始时间会立即按计划限制。"
               : "跟随与免控模式不会使用下方时间字段或预设。",
        13, custom ? UI_WARNING : UI_MUTED);

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "+  完成",
        UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_bedtime_bulk_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    static const char *LABELS[] = {"周一至周五", "周六、周日"};
    draw_dialog_shell(pixels, stride, model, &dialog, 700, 370);
    for (int i = 0; i < 2; ++i) {
        UiRect option = {dialog.x + 46 + i * 304, dialog.y + 138, 284, 82};
        draw_candidate_button(pixels, stride, (PtcUiRect){option.x, option.y, option.width, option.height},
            LABELS[i], UI_PAGE, UI_ACCENT, model->overlay_selection == i, false);
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  取消",
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  复制",
        UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_bedtime_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 780, 390);
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 164,
        "草稿会一直保留到保存或明确放弃；保存失败时仍留在编辑页。", 17, UI_MUTED);
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay), "X  放弃并离开",
        UI_DANGER_SOFT, UI_DANGER, model->overlay_selection == 1, false);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  继续编辑",
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
        model->disable_flag_present ? "紧急停用中不可保存" : "A  保存并离开",
        UI_ACCENT, UI_ON_ACCENT, model->disable_flag_present);
}

static void draw_weekly_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    bool refreshing = strcmp(model->overlay_title, "刷新周计划？") == 0;
    bool disabled = model->disable_flag_present;
    draw_dialog_shell(pixels, stride, model, &dialog, 860, 350);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 150, dialog.width - 80, 34},
                     "周计划还有未保存的修改", 22, UI_WARNING);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 40, dialog.y + 190, dialog.width - 80, 24},
                     "左右选择  |  A 确认  |  也可直接使用按钮快捷键", 17, UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_discard_rect(model->overlay),
                       refreshing ? "X  放弃并刷新" : "X  放弃并离开",
                       UI_DANGER_SOFT, UI_DANGER, true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), disabled ? "B  返回" : "B  继续编辑",
                        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       disabled
                         ? (refreshing ? "+  保留草稿并刷新" : "+  保留草稿并离开")
                         : (refreshing ? "+  保存并刷新" : "+  保存并离开"),
                       UI_ACCENT, UI_ON_ACCENT, false);
    if (model->weekly_leave_selection == 0) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_discard_rect(model->overlay)), 12, 3, UI_DANGER);
    } else if (model->weekly_leave_selection == 1) {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_cancel_rect(model->overlay)), 12, 3, UI_ACCENT);
    } else {
        draw_rect_outline(pixels, stride, to_uirect(ptc_ui_confirm_rect(model->overlay)), 12, 3, UI_SURFACE);
    }
}

static bool holiday_is_past_or_today(
    const PtcHolidayArrangement *entry,
    uint16_t cur_year, uint8_t cur_month, uint8_t cur_day,
    bool *is_past_out, bool *is_today_out)
{
    *is_past_out = false;
    *is_today_out = false;
    if (!entry) return false;
    uint8_t last_month = entry->end_month;
    uint8_t last_day = entry->end_day;
    if (entry->makeup_workdays && strcmp(entry->makeup_workdays, "无") != 0) {
        const char *p = entry->makeup_workdays;
        while (*p) {
            if (*p >= '0' && *p <= '9') {
                unsigned int m = 0, d = 0;
                if (sscanf(p, "%u月%u日", &m, &d) == 2) {
                    if (m > last_month || (m == last_month && d > last_day)) {
                        last_month = (uint8_t)m;
                        last_day = (uint8_t)d;
                    }
                }
                const char *next = strstr(p, "日");
                if (next) {
                    p = next + strlen("日");
                    continue;
                }
            }
            p++;
        }
    }
    if (cur_year > entry->year ||
        (cur_year == entry->year && (cur_month > last_month || (cur_month == last_month && cur_day > last_day)))) {
        *is_past_out = true;
        return true;
    }
    if (cur_year == entry->year) {
        bool in_holiday = (cur_month > entry->start_month || (cur_month == entry->start_month && cur_day >= entry->start_day)) &&
                          (cur_month < entry->end_month || (cur_month == entry->end_month && cur_day <= entry->end_day));
        if (in_holiday) {
            *is_today_out = true;
        } else if (entry->makeup_workdays && strcmp(entry->makeup_workdays, "无") != 0) {
            char today_str[32];
            snprintf(today_str, sizeof(today_str), "%u月%u日", cur_month, cur_day);
            if (strstr(entry->makeup_workdays, today_str)) {
                *is_today_out = true;
            }
        }
    }
    return true;
}

static void draw_holiday_calendar_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcHolidayCalendarInfo *info = ptc_holiday_calendar_info();
    size_t count = ptc_holiday_calendar_arrangement_count(info->last_year);
    const int per_page = 4;
    int pages = (int)((count + per_page - 1) / per_page);
    int page = model->holiday_calendar_page;
    char line[192];
    uint16_t cur_year = 0;
    uint8_t cur_month = 0, cur_day = 0;
    bool has_date = model->status_loaded && ptc_date_from_day_index(model->day_index, &cur_year, &cur_month, &cur_day);
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 600);
    if (model->holiday_dirty) {
        fill_round_rect(pixels, stride, (UiRect){dialog.x + dialog.width - 330, dialog.y + 24, 290, 34}, 16, UI_WARNING_SOFT);
        draw_text_center(pixels, stride, (UiRect){dialog.x + dialog.width - 330, dialog.y + 24, 290, 34},
                         "预览尚未保存的设置", 16, UI_WARNING);
    }
    if (page < 0 || page >= pages) page = 0;
    for (int row = 0; row < per_page; ++row) {
        size_t index = (size_t)(page * per_page + row);
        const PtcHolidayArrangement *entry = ptc_holiday_calendar_arrangement(info->last_year, index);
        UiRect card = {dialog.x + 34, dialog.y + 146 + row * 82, dialog.width - 68, 70};
        if (!entry) break;
        bool is_past = false, is_today = false;
        if (has_date) {
            holiday_is_past_or_today(entry, cur_year, cur_month, cur_day, &is_past, &is_today);
        }
        uint32_t bg_color = is_today ? UI_ACCENT_SOFT : (is_past ? UI_SURFACE : UI_RAISED);
        uint32_t border_color = is_today ? UI_ACCENT : UI_BORDER;
        uint32_t title_color = is_today ? UI_ACCENT : (is_past ? UI_MUTED : UI_INK);
        uint32_t line_color = is_today ? UI_INK : (is_past ? UI_DISABLED : UI_MUTED);
        fill_round_rect(pixels, stride, card, 16, bg_color);
        draw_rect_outline(pixels, stride, card, 16, is_today ? 2 : 1, border_color);
        draw_text(pixels, stride, card.x + 20, card.y + 28, entry->display_name, 21, title_color);
        snprintf(line, sizeof(line), "放假：%u月%u日-%u月%u日    调休上班：%s",
                 entry->start_month, entry->start_day, entry->end_month, entry->end_day, entry->makeup_workdays);
        draw_text(pixels, stride, card.x + 150, card.y + 28, line, 18, line_color);
        UiRect badge = {card.x + card.width - 96, card.y + 21, 76, 28};
        if (is_today) {
            fill_round_rect(pixels, stride, badge, 6, UI_ACCENT);
            draw_text_center(pixels, stride, badge, "进行中", 14, UI_ON_ACCENT);
        } else if (is_past) {
            fill_round_rect(pixels, stride, badge, 6, UI_PAGE);
            draw_text_center(pixels, stride, badge, "已结束", 14, UI_MUTED);
        }
    }
    snprintf(line, sizeof(line), "%u 年  |  v%u  |  发布于 %s  |  来源：www.gov.cn    第 %d/%d 页",
             info->last_year, info->version, info->published_date, page + 1, pages);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 30, dialog.y + 480, dialog.width - 60, 30}, line, 16, UI_MUTED);
    for (int index = 0; index < 3; ++index) {
        bool disabled = (index == 0 && page == 0) || (index == 1 && page + 1 >= pages);
        const char *label = index == 0 ? "L  上一页" : (index == 1 ? "R  下一页" : "A / B  关闭");
        draw_candidate_button(pixels, stride, ptc_ui_holiday_page_action_rect(index), label,
                              index == 2 ? UI_ACCENT : UI_PAGE,
                              index == 2 ? UI_ON_ACCENT : UI_ACCENT,
                              model->overlay_selection == index, disabled);
    }
}

static void draw_weekly_bulk_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const char *DAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    UiRect dialog;
    char source[96];
    char line[128];
    PtcUiWeeklyBulkStats stats;
    int slot = model->weekly_last_day_slot >= 0 && model->weekly_last_day_slot < 7 ? model->weekly_last_day_slot : 0;
    int day = ptc_ui_weekday_for_display_slot(slot);
    PtcDayRule rule = model->draft_week[day];
    draw_dialog_shell(pixels, stride, model, &dialog, 1040, 560);
    if (rule.mode == PTC_RULE_MODE_UNLIMITED) snprintf(source, sizeof(source), "来源：%s，不限时", DAYS[day]);
    else snprintf(source, sizeof(source), "来源：%s，限时 %u 分钟", DAYS[day], (unsigned int)rule.minutes);
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 116, source, 19, UI_INK);
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 148, "1. 选择目标", 17, UI_MUTED);
    for (int index = 0; index < 2; ++index) {
        UiRect card = to_uirect(ptc_ui_weekly_bulk_target_rect(index));
        bool selected = model->overlay_selection == index;
        fill_round_rect(pixels, stride, card, 16, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, card, 16, selected ? 3 : 1, selected ? UI_ACCENT : UI_BORDER);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 18, card.width, 34},
                         index == 0 ? "工作日" : "周末", 22, UI_INK);
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 54, card.width, 28},
                         index == 0 ? "周一至周五" : "周六与周日", 17, UI_MUTED);
    }
    ptc_ui_weekly_bulk_stats(model, model->overlay_selection == 1, &stats);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 490, dialog.y + 148, 510, 270}, 16, UI_RAISED);
    draw_rect_outline(pixels, stride, (UiRect){dialog.x + 490, dialog.y + 148, 510, 270}, 16, 1, UI_BORDER);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 180, "2. 覆盖预览", 20, UI_INK);
    snprintf(line, sizeof(line), "目标 %d 天；会改变 %d 天；相同跳过 %d 天",
             stats.target_count, stats.changed_count, stats.unchanged_count);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 218, line, 17, UI_ACCENT);
    draw_text(pixels, stride, dialog.x + 516, dialog.y + 256, "目标当前规则：", 16, UI_MUTED);
    for (int index = 0; index < stats.rule_group_count; ++index) {
        PtcDayRule group = stats.rule_groups[index].rule;
        if (group.mode == PTC_RULE_MODE_UNLIMITED) snprintf(line, sizeof(line), "不限时：%d 天", stats.rule_groups[index].count);
        else snprintf(line, sizeof(line), "限时 %u 分钟：%d 天", (unsigned int)group.minutes, stats.rule_groups[index].count);
        draw_text(pixels, stride, dialog.x + 536, dialog.y + 288 + index * 27, line, 16, UI_INK);
    }
    draw_text(pixels, stride, dialog.x + 40, dialog.y + 424,
              "应用后只修改本次运行中的草稿，仍需回到周计划页面保存。", 16, UI_WARNING);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                       stats.changed_count > 0 ? "A / +  应用到草稿" : "A / +  无需修改",
                       stats.changed_count > 0 ? UI_ACCENT : UI_DISABLED,
                       UI_ON_ACCENT, false);
}

static void draw_holiday_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 720, 320);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 148, dialog.width - 72, 28},
                     "离开后将丢失尚未保存的节假日设置", 19, UI_WARNING);
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay), "X  放弃更改",
                          UI_DANGER_SOFT, UI_DANGER, model->holiday_leave_selection == 0, false);
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  继续编辑",
                          UI_PAGE, UI_ACCENT, model->holiday_leave_selection == 1, false);
}

bool draw_plan_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
    case PTC_UI_OVERLAY_SCHEDULED_LEAVE:
        draw_scheduled_leave(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SCHEDULED:
        draw_scheduled_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_AUTONOMY:
        draw_autonomy_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_QUICK_ADD:
        draw_quick_add_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME:
        draw_bedtime_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_WINDOW:
        draw_bedtime_window_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_SPECIAL:
        draw_bedtime_special_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_LEAVE:
        draw_bedtime_leave_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_BEDTIME_BULK:
        draw_bedtime_bulk_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
        draw_weekly_leave_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_HOLIDAY_CALENDAR:
        draw_holiday_calendar_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_HOLIDAY_LEAVE:
        draw_holiday_leave_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_WEEKLY_BULK:
        draw_weekly_bulk_overlay(pixels, stride, model);
        return true;
    default:
        return false;
    }
}
