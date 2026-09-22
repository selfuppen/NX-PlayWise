#include "ui_render_internal.h"

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
