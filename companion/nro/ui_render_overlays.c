#include "ui_render_internal.h"

static void masked_value(const char *value, bool revealed, char *out, size_t out_size)
{
    size_t length;
    if (revealed) {
        snprintf(out, out_size, "%s", value && value[0] ? value : "--");
        return;
    }
    length = value ? strlen(value) : 0U;
    snprintf(out, out_size, "%s", length ? "************************" : "--");
}

static void draw_credential_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    UiRect current_box;
    UiRect input_box = to_uirect(ptc_ui_credential_input_rect());
    char current[96];
    char next[96];
    bool valid;
    bool dirty;
    draw_dialog_shell(pixels, stride, model, &dialog, 900, 500);
    current_box = (UiRect){dialog.x + 42, dialog.y + 132, 600, 56};
    masked_value(model->credential_current, model->credential_kind == 1 || model->credential_revealed, current, sizeof(current));
    masked_value(model->credential_new, model->credential_kind == 1 || model->credential_new_revealed, next, sizeof(next));
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 122, "当前值", 17, UI_MUTED);
    fill_round_rect(pixels, stride, current_box, 16, UI_PAGE);
    if (model->credential_kind == 2 && model->credential_revealed) {
        if (strlen(current) > 32U) {
            char first[33];
            memcpy(first, current, 32U);
            first[32] = '\0';
            draw_text(pixels, stride, current_box.x + 16, current_box.y + 23, first, 14, UI_INK);
            draw_text(pixels, stride, current_box.x + 16, current_box.y + 44, current + 32, 14, UI_INK);
        } else {
            draw_text(pixels, stride, current_box.x + 16, current_box.y + 36, current, 14, UI_INK);
        }
    } else {
        draw_text(pixels, stride, current_box.x + 16, current_box.y + 36, current, 18, UI_INK);
    }
    if (model->credential_kind == 2) {
        draw_candidate_button(pixels, stride, ptc_ui_credential_reveal_rect(),
                              model->credential_revealed ? "ZR  隐藏当前密钥" : "ZR  显示当前密钥",
                              UI_PAGE, UI_ACCENT,
                              model->overlay_selection == PTC_UI_CREDENTIAL_REVEAL, false);
    }
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 215, "新值", 17, UI_MUTED);
    fill_round_rect(pixels, stride, input_box, 12, UI_ACCENT_SOFT);
    draw_rect_outline(pixels, stride, input_box, 12, model->overlay_selection == PTC_UI_CREDENTIAL_INPUT ? 3 : 1, model->overlay_selection == PTC_UI_CREDENTIAL_INPUT ? UI_ACCENT : UI_CONTROL);
    if (model->overlay_selection == PTC_UI_CREDENTIAL_INPUT) {
        draw_text(pixels, stride, input_box.x + input_box.width - 72, input_box.y + 20, "A / X", 14, UI_ACCENT);
    }
    if (model->credential_kind == 2 && model->credential_new_revealed) {
        if (strlen(next) > 32U) {
            char first[33];
            memcpy(first, next, 32U);
            first[32] = '\0';
            draw_text(pixels, stride, input_box.x + 16, input_box.y + 25, first, 14, UI_INK);
            draw_text(pixels, stride, input_box.x + 16, input_box.y + 48, next + 32, 14, UI_INK);
        } else {
            draw_text(pixels, stride, input_box.x + 16, input_box.y + 40, next, 14, UI_INK);
        }
    } else {
        draw_text(pixels, stride, input_box.x + 16, input_box.y + 40, next, 18, UI_INK);
    }
    draw_candidate_button(pixels, stride, ptc_ui_credential_random_rect(), "Y  随机生成",
                          UI_PAGE, UI_ACCENT,
                          model->overlay_selection == PTC_UI_CREDENTIAL_RANDOM, false);
    if (model->credential_kind == 2) {
        draw_candidate_button(pixels, stride, ptc_ui_credential_demo_rect(),
                              model->demo_secret_enabled ? "R  退出演示并换新密钥" : "R  使用公共演示密钥",
                              model->demo_secret_enabled ? UI_PAGE : UI_DANGER_SOFT,
                              model->demo_secret_enabled ? UI_ACCENT : UI_DANGER,
                              model->overlay_selection == PTC_UI_CREDENTIAL_DEMO, false);
        draw_text(pixels, stride, dialog.x + 332, dialog.y + 350,
                  "建议使用随机生成；手工密钥至少 32 个字符。", 17, UI_MUTED);
    }
    valid = model->credential_kind == 1
        ? ptc_device_id_valid(model->credential_new)
        : ptc_grant_secret_valid(model->credential_new);
    dirty = strcmp(model->credential_current, model->credential_new) != 0;
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                          dirty ? "+  保存" : "+  没有修改",
                          UI_ACCENT, UI_ON_ACCENT,
                          model->overlay_selection == PTC_UI_CREDENTIAL_SAVE, !valid || !dirty);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 406,
              "方向键选择  |  A 确定  |  X 手工输入  |  + 保存", 16, UI_MUTED);
}

static void draw_code_result_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell_model = *model;
    char before_value[48];
    char after_value[48];
    char actual_value[48];
    bool remaining_fresh = !model->code_result_failed && ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    format_duration(model->code_actual_add_minutes, actual_value, sizeof(actual_value));
    if (model->code_result_pending) {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "加时结果确认中");
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "已恢复上次确认的兑换请求，正在读取最终结果；请勿重复输入这枚加时码。");
    } else if (model->code_result_failed) {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "兑换未成功");
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "%s", ptc_ui_code_failure_guidance(model->error_code));
    } else {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "加时成功");
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "该加时码已经使用，不能再次使用。");
    }
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 760, 420);
    if (model->code_before_unlimited) snprintf(before_value, sizeof(before_value), "不限时");
    else format_duration(model->code_before_remaining_available ? model->code_before_remaining_minutes : -1,
                         before_value, sizeof(before_value));
    if (model->code_result_pending) {
        format_duration(model->code_preview_after_available ? model->code_preview_after_minutes : -1,
                        after_value, sizeof(after_value));
    } else if (!remaining_fresh) snprintf(after_value, sizeof(after_value), "状态待确认");
    else if (model->unrestricted_today == 1) snprintf(after_value, sizeof(after_value), "不限时");
    else format_duration(model->remaining_available ? model->remaining_minutes : -1,
                         after_value, sizeof(after_value));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 142, 300, 92},
                         model->code_result_pending || model->code_result_failed ? "兑换前" : "实际增加",
                         model->code_result_pending || model->code_result_failed ? before_value :
                         (model->code_actual_add_available ? actual_value : "暂不可用"),
                         UI_RGB(UI_BLENDED(text_primary)));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 406, dialog.y + 142, 300, 92},
                         model->code_result_pending ? "预览兑换后" : (model->code_result_failed ? "上次剩余读数" : "兑换后剩余"), after_value,
                         time_state_accent(model->code_result_pending ? model->code_preview_after_available :
                                           (remaining_fresh && (model->unrestricted_today == 1 || model->remaining_available)),
                                           model->code_result_pending ? false : model->unrestricted_today == 1,
                                           model->code_result_pending ? model->code_preview_after_minutes : model->remaining_minutes));
    if (!model->code_result_pending) {
        char age[80];
        format_status_age(model, age, sizeof(age));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 312, 652, 24}, age, 18, status_age_color(model));
    }
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54}, 16, model->code_result_pending ? UI_RGB(UI_BLENDED(surface_raised)) :
                    (model->code_result_failed ? UI_DANGER_SOFT : UI_SUCCESS_SOFT));
    draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 252, 652, 54},
                     model->code_result_pending ? "结果确认期间可关闭；下次打开会继续确认" :
                     (model->code_result_failed ? "本次未消费代码；已使用或过期的代码仍不可用" : (model->code_actual_add_available && model->code_actual_add_minutes < model->code_grant_minutes
                         ? "已到每日上限，实际增加少于代码时长" :
                         (model->code_actual_add_available ? "兑换结果已确认并保存" : "成功已确认；加时明细暂不可核对，请查看使用记录"))),
                     19, model->code_result_pending ? UI_RGB(UI_BLENDED(text_secondary)) :
                     (model->code_result_failed ? UI_DANGER : UI_SUCCESS));
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回孩子区",
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), model->code_result_pending ? "A  关闭" : "A  完成",
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_auth_error_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell_model = *model;
    char retry_label[64];
    snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "%s",
             model->auth_error_title[0] ? model->auth_error_title : "PIN 验证未通过");
    snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body), "%s",
             model->auth_error_message[0] ? model->auth_error_message : "PIN 不正确，请重试。");
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 720, 340);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 44, dialog.y + 142, dialog.width - 88, 72}, 16, UI_DANGER_SOFT);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 58, dialog.y + 142, dialog.width - 116, 72},
                     model->auth_cooldown_seconds > 0
                        ? "错误次数过多，倒计时结束后才能重试"
                        : "错误 PIN 不会保留；重新输入时输入框为空",
                     18, UI_DANGER);
    if (model->auth_cooldown_seconds > 0) {
        snprintf(retry_label, sizeof(retry_label), "请等待 %d 秒", model->auth_cooldown_seconds);
    } else {
        snprintf(retry_label, sizeof(retry_label), "A  重新输入");
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  取消",
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), retry_label,
                       model->auth_cooldown_seconds > 0 ? UI_BORDER : UI_ACCENT,
                       model->auth_cooldown_seconds > 0 ? UI_MUTED : UI_ON_ACCENT, false);
}

static void draw_grant_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char fitted[192];
    int index;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 650);
    for (index = 0; index < PTC_UI_GRANT_MANAGER_COUNT; ++index) {
        draw_action_card(
            pixels,
            stride,
            to_uirect(ptc_ui_grant_manager_card_rect(index)),
            &GRANT_MANAGER_ACTIONS[index],
            model->overlay_selection == index,
            PTC_UI_ACTION_AVAILABLE,
            0);
    }
    fit_text(fitted, sizeof(fitted), model->message, 16, dialog.width - 330);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 506,
              fitted[0] ? fitted : "方向键选择  |  A 确定  |  B 返回加时码", 16,
              fitted[0] ? UI_SUCCESS : UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
}

static void draw_redemption_history_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char page_text[64];
    int pages = ptc_ui_redemption_history_page_count(model);
    int first = model->redemption_history_page * 6;
    int visible = model->redemption_history_count - first;
    int row;
    if (visible > 6) visible = 6;
    if (visible < 0) visible = 0;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 650);
    snprintf(page_text, sizeof(page_text), "最新优先  |  第 %d/%d 页  |  共 %d 条",
             model->redemption_history_page + 1, pages, model->redemption_history_count);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 104, page_text, 16, UI_MUTED);
    if (!model->redemption_history_available) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
                         "暂时无法读取使用记录；可重试，或验证 PIN 后清空损坏记录。",
                         19, UI_DANGER);
    } else if (model->redemption_history_count == 0) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
                         "暂无成功使用记录；升级前的兑换不会回填。",
                         19, UI_MUTED);
    } else {
        if (visible > 0) {
            int line_x = dialog.x + 48;
            int start_y = dialog.y + 132 + 27;
            int end_y = dialog.y + 132 + (visible - 1) * 62 + 27;
            fill_rect(pixels, stride, (UiRect){line_x - 1, start_y, 2, end_y - start_y}, UI_BORDER);
        }
        for (row = 0; row < visible; ++row) {
            int source = model->redemption_history_count - 1 - first - row;
            const PtcRedemptionHistoryRecord *record = &model->redemption_history[source];
            UiRect item = {dialog.x + 68, dialog.y + 132 + row * 62, dialog.width - 102, 54};
            char time_text[48];
            char allowance[160];
            char remaining[80];
            format_event_time(record->redeemed_at, true, time_text, sizeof(time_text));
            if (record->remaining_after_available) {
                char value[48];
                format_duration((int)record->remaining_after_minutes, value, sizeof(value));
                snprintf(remaining, sizeof(remaining), "兑换后 %s", value);
            } else {
                snprintf(remaining, sizeof(remaining), "兑换后暂不可用");
            }
            snprintf(allowance, sizeof(allowance), "代码 %u 分钟  |  实际计入 %u 分钟%s",
                     (unsigned int)record->grant_minutes,
                     (unsigned int)record->effective_add_minutes,
                     record->effective_add_minutes < record->grant_minutes ? "（已到每日上限）" : "");
            fill_round_rect(pixels, stride, item, 16, UI_RAISED);
            draw_rect_outline(pixels, stride, item, 16, 1, UI_BORDER);
            draw_text(pixels, stride, item.x + 16, item.y + 20, time_text, 15, UI_INK);
            draw_text(pixels, stride, item.x + 250, item.y + 20, allowance, 15, UI_ACCENT);
            draw_text(pixels, stride, item.x + 720, item.y + 20, remaining, 15, UI_INK);
            draw_text(pixels, stride, item.x + item.width - 84, item.y + 20,
                      record->token_version == 2u ? "v2 成功" : "v1 成功", 14, UI_SUCCESS);

            int cy = item.y + 27;
            int cx = dialog.x + 48;
            fill_round_rect(pixels, stride, (UiRect){cx - 4, cy - 4, 8, 8}, 4, UI_SUCCESS);
            fill_round_rect(pixels, stride, (UiRect){cx - 2, cy - 2, 4, 4}, 2, UI_SURFACE);
        }
    }
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_prev_rect(), "L / 左  上一页",
                       UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_next_rect(), "R / 右  下一页",
                       UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "X  清空全部",
                       UI_DANGER_SOFT, UI_DANGER, false);
}

static const char *activity_label(const char *action)
{
    if (strcmp(action, "today_limit") == 0) return "修改今日总额度";
    if (strcmp(action, "today_add") == 0) return "家长临时加时";
    if (strcmp(action, "today_unlimited") == 0) return "今日改为不限时";
    if (strcmp(action, "today_restore") == 0) return "恢复今日计划";
    if (strcmp(action, "weekly_update") == 0) return "修改周计划";
    if (strcmp(action, "holiday_update") == 0) return "修改节假日规则";
    if (strcmp(action, "scheduled_update") == 0) return "修改临时额度计划";
    if (strcmp(action, "autonomy_update") == 0) return "修改自主缓冲";
    if (strcmp(action, "offline_grant") == 0) return "兑换加时码";
    if (strcmp(action, "daily_buffer") == 0) return "领取自主缓冲";
    if (strcmp(action, "protection") == 0) return "保护事件";
    return "活动记录";
}

static void draw_activity_history_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    int pages = ptc_ui_activity_history_page_count(model);
    int first = model->activity_history_page * 8;
    int visible = model->activity_history_count - first;
    int row;
    char line[160];
    if (visible > 8) visible = 8;
    if (visible < 0) visible = 0;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 650);
    snprintf(line, sizeof(line), "最新优先  |  第 %d/%d 页  |  共 %d 条  |  不含 PIN、密钥、完整代码或 nonce",
        model->activity_history_page + 1, pages, model->activity_history_count);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 104, line, 15, UI_MUTED);
    if (!model->activity_history_available) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
            "家庭活动记录暂不可用；控制功能不会因此中断。", 19, UI_DANGER);
    } else if (model->activity_history_count == 0) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
            "暂无家庭活动记录。", 19, UI_MUTED);
    } else {
        if (visible > 0) {
            int line_x = dialog.x + 48;
            int start_y = dialog.y + 130 + 19;
            int end_y = dialog.y + 130 + (visible - 1) * 45 + 19;
            fill_rect(pixels, stride, (UiRect){line_x - 1, start_y, 2, end_y - start_y}, UI_BORDER);
        }
        for (row = 0; row < visible; ++row) {
            int source = model->activity_history_count - 1 - first - row;
            const PtcActivityHistoryRecord *record = &model->activity_history[source];
            UiRect item = {dialog.x + 68, dialog.y + 130 + row * 45, dialog.width - 102, 38};
            char time_text[48];
            format_event_time(record->occurred_at, true, time_text, sizeof(time_text));
            snprintf(line, sizeof(line), "%s  |  %s  |  计划 %u 分钟，实际 %u 分钟",
                time_text, activity_label(record->action), (unsigned int)record->minutes,
                (unsigned int)record->effective_minutes);
            fill_round_rect(pixels, stride, item, 6, UI_RAISED);
            draw_text(pixels, stride, item.x + 14, item.y + 24, line, 14,
                strcmp(record->action, "protection") == 0 ? UI_DANGER : UI_INK);

            int cy = item.y + 19;
            int cx = dialog.x + 48;
            uint32_t dot_color = strcmp(record->action, "protection") == 0 ? UI_DANGER : UI_ACCENT;
            fill_round_rect(pixels, stride, (UiRect){cx - 4, cy - 4, 8, 8}, 4, dot_color);
            fill_round_rect(pixels, stride, (UiRect){cx - 2, cy - 2, 4, 4}, 2, UI_SURFACE);
        }
    }
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_prev_rect(), "L / 左  上一页",
        UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_next_rect(), "R / 右  下一页",
        UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "X  清空全部",
        UI_DANGER_SOFT, UI_DANGER, false);
}

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
            draw_text(pixels, stride, row.x + row.width - 165, row.y + 38, "A/点按切换", 13, UI_MUTED);
        } else if (index == 1 || index == 2) {
            draw_text(pixels, stride, row.x + 130, row.y + 38, values[index], 20, UI_RGB(UI_BLENDED(text_primary)));
            draw_text(pixels, stride, row.x + 320, row.y + 38, "左右 ±1天 | ZL/ZR ±7天", 13, UI_ACCENT);
            draw_text(pixels, stride, row.x + row.width - 92, row.y + 38, "A 键盘输入", 12, UI_MUTED);
        } else {
            draw_text(pixels, stride, row.x + 130, row.y + 38, values[index], 20,
                       draft->rule.mode == PTC_RULE_MODE_UNLIMITED ? UI_SUCCESS : UI_ACCENT);
            draw_text(pixels, stride, row.x + row.width - 180, row.y + 38, "X 模式 | A 键盘输入", 13, UI_MUTED);
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

    draw_text(pixels, stride, dialog.x + 34, dialog.y + 486,
              "说明：临时额度计划仅改每日额度，就寝时间独立并行。优先级：今日额度调整 > 临时额度计划 > 国家节假日 > 周计划。",
              14, UI_RGB(UI_BLENDED(text_secondary)));
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 512,
              "操作：方向键上下选择  |  左右键 ±1 天  |  ZL / ZR 键 ±7 天  |  A 键快速输入  |  X 键切换限时模式",
              14, UI_MUTED);

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
    int index;
    char label[48];
    draw_dialog_shell(pixels, stride, model, &dialog, 760, 420);
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
        draw_text_center(pixels, stride, option, label, 21,
            selected ? UI_SUCCESS : UI_INK);
    }
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 284,
        "孩子每天仅可领取一次，只在限时日可用；失败不会消耗领取资格。",
        16, UI_MUTED);
    draw_overlay_actions(pixels, stride, model, "+  保存缓冲设置");
}

static void draw_quick_add_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    static const char *LABELS[] = {"+15 分钟", "+30 分钟", "+60 分钟", "自定义"};
    draw_dialog_shell(pixels, stride, model, &dialog, 760, 420);
    for (int index = 0; index < 4; ++index) {
        UiRect option = to_uirect(ptc_ui_autonomy_option_rect(index));
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

static void draw_bedtime_window_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    const PtcBedtimeWindow *window = &model->draft_bedtime_policy.week[model->bedtime_editor_day];
    char values[3][96];
    draw_dialog_shell(pixels, stride, model, &dialog, 760, 460);
    snprintf(values[0], sizeof(values[0]), "本日窗口：%s", window->enabled ? "开启" : "关闭");
    snprintf(values[1], sizeof(values[1]), "开始时间：%02u:%02u",
        (unsigned int)(window->start_minute / 60), (unsigned int)(window->start_minute % 60));
    snprintf(values[2], sizeof(values[2]), "次日结束：%02u:%02u",
        (unsigned int)(window->end_minute / 60), (unsigned int)(window->end_minute % 60));
    for (int i = 0; i < 3; ++i) {
        UiRect row = {dialog.x + 48, dialog.y + 112 + i * 66, dialog.width - 96, 52};
        draw_plan_card(pixels, stride, row, model->overlay_selection == i);
        if (i == 0) {
            draw_text(pixels, stride, row.x + 18, row.y + 32, "本日就寝窗口", 19, UI_INK);
            draw_text(pixels, stride, row.x + 150, row.y + 32, window->enabled ? "开启" : "关闭", 19,
                      window->enabled ? UI_SUCCESS : UI_MUTED);
            UiRect toggle_rect = {row.x + row.width - 76, row.y + (row.height - 28) / 2, 58, 28};
            draw_toggle_switch(pixels, stride, toggle_rect, window->enabled, model->overlay_selection == 0, false, NULL, NULL);
            draw_text(pixels, stride, row.x + row.width - 156, row.y + 32, "A/X 切换", 13, UI_MUTED);
        } else {
            draw_text(pixels, stride, row.x + 18, row.y + 32, values[i], 19,
                      window->enabled ? UI_INK : UI_MUTED);
            draw_text(pixels, stride, row.x + row.width - 156, row.y + 32,
                      window->enabled ? "A 精调时间" : "（已关闭）", 13, UI_MUTED);
        }
    }
    if (window->enabled) {
        int dur_m = (int)(window->end_minute + 1440 - window->start_minute) % 1440;
        char dur_str[96];
        snprintf(dur_str, sizeof(dur_str), "夜间跨度：%02u:%02u 至 次日 %02u:%02u（跨越午夜 %u 小时 %u 分）",
                 (unsigned int)(window->start_minute / 60), (unsigned int)(window->start_minute % 60),
                 (unsigned int)(window->end_minute / 60), (unsigned int)(window->end_minute % 60),
                 (unsigned int)(dur_m / 60), (unsigned int)(dur_m % 60));
        draw_text(pixels, stride, dialog.x + 48, dialog.y + 324, dur_str, 15, UI_ACCENT);
    } else {
        draw_text(pixels, stride, dialog.x + 48, dialog.y + 324, "本日就寝窗口已关闭，当天不触发就寝限制。", 15, UI_MUTED);
    }
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 352,
        "方向键选择  |  A / 点按打开时间编辑器；窗口必须跨越午夜  |  + 完成", 14, UI_MUTED);
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
    char values[3][96];
    draw_dialog_shell(pixels, stride, model, &dialog, 760, 460);
    snprintf(values[0], sizeof(values[0]), "规则模式：%s", bedtime_override_label(rule->mode));
    snprintf(values[1], sizeof(values[1]), "开始时间：%02u:%02u",
        (unsigned int)(rule->window.start_minute / 60), (unsigned int)(rule->window.start_minute % 60));
    snprintf(values[2], sizeof(values[2]), "次日结束：%02u:%02u",
        (unsigned int)(rule->window.end_minute / 60), (unsigned int)(rule->window.end_minute % 60));
    for (int i = 0; i < 3; ++i) {
        UiRect row = {dialog.x + 48, dialog.y + 112 + i * 66, dialog.width - 96, 52};
        draw_plan_card(pixels, stride, row, model->overlay_selection == i);
        if (i == 0) {
            draw_text(pixels, stride, row.x + 18, row.y + 32, "规则模式", 19, UI_INK);
            uint32_t mode_color = rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM ? UI_ACCENT :
                (rule->mode == PTC_BEDTIME_OVERRIDE_DISABLED ? UI_MUTED : UI_SUCCESS);
            draw_text(pixels, stride, row.x + 130, row.y + 32, bedtime_override_label(rule->mode), 19, mode_color);
            draw_text(pixels, stride, row.x + row.width - 180, row.y + 32, "A/X 切换模式", 13, UI_MUTED);
        } else {
            draw_text(pixels, stride, row.x + 18, row.y + 32, values[i], 19,
                rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM ? UI_INK : UI_MUTED);
            draw_text(pixels, stride, row.x + row.width - 156, row.y + 32,
                      rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM ? "A 精调时间" : "跟随模式", 13, UI_MUTED);
        }
    }
    if (rule->mode == PTC_BEDTIME_OVERRIDE_CUSTOM) {
        int dur_m = (int)(rule->window.end_minute + 1440 - rule->window.start_minute) % 1440;
        char dur_str[96];
        snprintf(dur_str, sizeof(dur_str), "自定义跨度：%02u:%02u 至 次日 %02u:%02u（跨夜 %u 小时 %u 分）",
                 (unsigned int)(rule->window.start_minute / 60), (unsigned int)(rule->window.start_minute % 60),
                 (unsigned int)(rule->window.end_minute / 60), (unsigned int)(rule->window.end_minute % 60),
                 (unsigned int)(dur_m / 60), (unsigned int)(dur_m % 60));
        draw_text(pixels, stride, dialog.x + 48, dialog.y + 324, dur_str, 15, UI_ACCENT);
    } else if (rule->mode == PTC_BEDTIME_OVERRIDE_DISABLED) {
        draw_text(pixels, stride, dialog.x + 48, dialog.y + 324, "节假日不设就寝限制，仅按游玩额度控制。", 15, UI_MUTED);
    } else {
        draw_text(pixels, stride, dialog.x + 48, dialog.y + 324, "继承对应自然星期的每周就寝时间窗口设置。", 15, UI_SUCCESS);
    }
    draw_text(pixels, stride, dialog.x + 48, dialog.y + 352,
        "方向键选择  |  A / 点按打开快速时间编辑器；窗口必须跨越午夜  |  + 完成", 14, UI_MUTED);
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

static void draw_grant_local_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char line[256], remaining[48], estimate[48], code[16], freshness[80];
    bool capped = false;
    bool reliable = ptc_ui_status_is_fresh(model, ptc_ui_render_now()) && !model->grant_status_refresh_failed;
    int expected = reliable ? ptc_ui_grant_estimate_remaining(model, model->grant_minutes, &capped) : -1;
    uint16_t year; uint8_t month, day;
    PtcUiModel shell = *model;
    snprintf(shell.overlay_body, sizeof(shell.overlay_body), "选时长、生成，再把代码告诉孩子。实际增加以兑换结果为准。");
    draw_dialog_shell(pixels, stride, &shell, &dialog, 920, 650);
    if (reliable && model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), "不限时");
    else format_duration(reliable && model->remaining_available ? model->remaining_minutes : -1, remaining, sizeof(remaining));
    format_duration(expected, estimate, sizeof(estimate));
    format_status_age(model, freshness, sizeof(freshness));
    if (model->grant_status_refresh_failed) snprintf(freshness, sizeof(freshness), "刷新失败，返回后刷新再试");
    snprintf(line, sizeof(line), "今天还可玩 %s  |  %s", remaining, freshness);
    if (model->grant_notice[0]) snprintf(line, sizeof(line), "%s", model->grant_notice);
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 126, line, 18, UI_RGB(UI_BLENDED(text_secondary)));

    fill_round_rect(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 144, 852, 192}, 16, UI_RGB(UI_BLENDED(surface_raised)));
    draw_text(pixels, stride, dialog.x + 54, dialog.y + 176, "下次加时时长", 20, UI_RGB(UI_BLENDED(text_secondary)));
    char played[48];
    format_duration(reliable && model->played_minutes_available ? model->played_minutes : -1, played, sizeof(played));
    snprintf(line, sizeof(line), "额度已耗（估算）%s", played);
    draw_text(pixels, stride, dialog.x + 420, dialog.y + 176, line, 18, UI_RGB(UI_BLENDED(text_secondary)));
    snprintf(line, sizeof(line), "%u 分钟  |  A 输入", (unsigned)model->grant_minutes);
    draw_candidate_button(pixels, stride, ptc_ui_grant_adjust_rect(0), line,
        UI_ACCENT_SOFT, UI_ACCENT,
        model->overlay_selection == PTC_UI_GRANT_LOCAL_ADJUST_FIRST, false);
    snprintf(line, sizeof(line), "兑换后预计 %s%s", estimate, capped ? "（已到每日上限）" : "");
    draw_text(pixels, stride, dialog.x + 420, dialog.y + 238, line, 20, UI_RGB(UI_BLENDED(text_secondary)));
    draw_text(pixels, stride, dialog.x + 54, dialog.y + 320, "调整这里不会改变已生成代码的时长，也不会撤销旧码。", 18, UI_RGB(UI_BLENDED(text_secondary)));

    fill_round_rect(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 350, 852, 170}, 16, UI_RGB(UI_BLENDED(surface_raised)));
    draw_text(pixels, stride, dialog.x + 54, dialog.y + 382, "已生成代码", 20, UI_RGB(UI_BLENDED(text_secondary)));
    if (model->grant_has_code) {
        ptc_ui_format_code(model->grant_code, code, sizeof(code));
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 432, code, 42, UI_RGB(UI_BLENDED(text_primary)));
        snprintf(line, sizeof(line), "代码时长 %u 分钟", (unsigned)model->grant_issued_minutes);
        draw_text(pixels, stride, dialog.x + 420, dialog.y + 426, line, 25, UI_RGB(UI_BLENDED(text_primary)));
        format_duration(model->grant_estimate_available ? model->grant_estimate_minutes : -1, estimate, sizeof(estimate));
        snprintf(line, sizeof(line), "生成时预计剩余 %s%s", estimate, model->grant_estimate_capped ? "（已到每日上限）" : "");
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 466, line, 18, UI_RGB(UI_BLENDED(text_secondary)));
        if (ptc_date_from_day_index(model->grant_day_index, &year, &month, &day))
            snprintf(line, sizeof(line), "%u-%02u-%02u 有效，成功兑换后仅可使用一次", (unsigned)year, (unsigned)month, (unsigned)day);
        else snprintf(line, sizeof(line), "签发日期待确认，请返回后刷新状态");
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 499, line, 18, UI_RGB(UI_BLENDED(text_secondary)));
    } else {
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 430, "选好时长后，按 + 生成", 28, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 471, "生成前会再次验证 PIN；同日已签发的其他代码仍可能可用。", 18, UI_RGB(UI_BLENDED(text_secondary)));
    }
    draw_candidate_button(pixels, stride, ptc_ui_grant_generate_rect(),
        model->grant_has_code ? "+  再生成一个" : "+  生成加时码", UI_ACCENT, UI_ON_ACCENT,
        model->overlay_selection == PTC_UI_GRANT_LOCAL_GENERATE, model->waiting);
    draw_candidate_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
        UI_RGB(UI_BLENDED(surface_raised)), UI_RGB(UI_BLENDED(text_primary)), model->overlay_selection == PTC_UI_GRANT_LOCAL_BACK, false);
    draw_text(pixels, stride, dialog.x + 280, dialog.y + 615, "方向键选择  |  A 编辑代码时长  |  + 生成", 18, UI_RGB(UI_BLENDED(text_secondary)));
}

static void draw_qr_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    int size = qrcodegen_getSize(model->qr_code);
    int scale = size > 0 ? 350 / (size + 8) : 1;
    int total;
    int origin_x;
    int origin_y;
    int next_y;
    int x;
    int y;
    if (scale < 2) scale = 2;
    total = (size + 8) * scale;
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 650);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 142, "推荐方案一：联网扫码", 23, UI_SUCCESS);
    origin_x = dialog.x + 34;
    origin_y = dialog.y + 164;
    /* QR polarity is functional and intentionally bypasses the active theme. */
    fill_rect_packed(pixels, stride, (UiRect){origin_x, origin_y, total, total}, pack_rgb(0xFFFFFF));
    for (y = 0; y < size; ++y) {
        for (x = 0; x < size; ++x) {
            if (qrcodegen_getModule(model->qr_code, x, y)) {
                fill_rect_packed(pixels, stride,
                                 (UiRect){origin_x + (x + 4) * scale, origin_y + (y + 4) * scale, scale, scale},
                                 pack_rgb(0x000000));
            }
        }
    }

    draw_text(pixels, stride, dialog.x + 34, dialog.y + 530,
              "仅在网页可访问时扫码；地址：", 14, UI_MUTED);
    next_y = draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 552,
                               model->pairing_base_url, 13, 400, 19, 3, UI_ACCENT);
    draw_text(pixels, stride, dialog.x + 34, next_y + 4,
              "二维码包含加时码密钥，请仅由家长使用。", 14, UI_DANGER);

    draw_text(pixels, stride, dialog.x + 470, dialog.y + 142, "备用方案二：单文件离线版", 23, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 178,
              "1. 解压完整交付包，取得 playwise-offline.html", 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 208,
              "2. 返回“加时码生成管理”，导出手机/电脑配置", 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 488, dialog.y + 234,
              PLAYWISE_SD_ROOT "/parent-import.json", 15, UI_ACCENT);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 266,
              "3. 将 HTML 和配置文件传到可信的手机或电脑", 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 298,
              "4. 用系统浏览器打开 HTML，再点击“导入配置文件”", 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 330,
              "5. 选择 parent-import.json，再点击“导入此设备”", 15, UI_INK);
    next_y = dialog.y + 362;
    draw_text(pixels, stride, dialog.x + 470, next_y + 6,
              "日常生成无需网络，也无需安装应用或本地服务器。", 14, UI_SUCCESS);
    draw_text(pixels, stride, dialog.x + 470, next_y + 32,
              "手机请先保存文件，再交给系统浏览器打开；", 14, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 470, next_y + 58,
              "不要使用聊天软件或网盘的内置预览器。", 14, UI_MUTED);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 470, next_y + 74, 610, 48}, 16, UI_DANGER_SOFT);
    draw_text(pixels, stride, dialog.x + 486, next_y + 104,
              "配置文件包含加时码密钥，请勿发送给他人。", 15, UI_DANGER);

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "B  返回",
                       UI_RAISED, UI_INK, true);
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

static void draw_credential_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 720, 300);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 132, dialog.width - 72, 30},
                     "左右选择  |  A 确定  |  B 继续编辑", 17, UI_MUTED);
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay), "X  放弃修改",
                          UI_DANGER_SOFT, UI_DANGER,
                          model->overlay_selection == 0, false);
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), "A  继续编辑",
                          UI_PAGE, UI_ACCENT,
                          model->overlay_selection == 1, false);
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

static void draw_detail_metric(uint32_t *pixels, uint32_t stride, int x, int y,
                               const char *label, const char *value, uint32_t color)
{
    draw_text(pixels, stride, x, y, label, 17, UI_MUTED);
    draw_text(pixels, stride, x, y + 36, value, 28, color);
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
                                   const PtcUiTodayDecision *decision)
{
    const char *titles[] = {"今日调整", "临时计划", "节假日", "周常规"};
    const PtcUiDecisionStep *steps[] = {
        &decision->today_override,
        &decision->scheduled_override,
        &decision->holiday,
        &decision->weekly
    };
    int node_w = 106;
    int node_h = 106;
    int count = 4;
    int gap = (total_w - node_w * count) / (count - 1);
    bool hit_found = false;

    draw_text(pixels, stride, x, y + 14, "额度决策流（优先级自高向低，遇命中阻断）", 13, UI_INK);
    int cards_y = y + 24;

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
            fill_round_rect(pixels, stride, card, 10, UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, card, 10, 2, UI_SUCCESS);
        } else if (is_overridden) {
            fill_round_rect(pixels, stride, card, 10, UI_RAISED);
            draw_rect_outline(pixels, stride, card, 10, 1, UI_BORDER);
        } else {
            fill_round_rect(pixels, stride, card, 10, UI_PAGE);
            draw_rect_outline(pixels, stride, card, 10, 1, UI_BORDER);
        }

        /* 1. Header title */
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 6, card.width, 18},
                         titles[i], 12, is_selected ? UI_SUCCESS : (is_overridden ? UI_INK : UI_MUTED));

        /* 2. Status Badge */
        UiRect badge = {card.x + 14, card.y + 26, card.width - 28, 20};
        if (is_selected) {
            fill_round_rect(pixels, stride, badge, 4, UI_SUCCESS);
            draw_text_center(pixels, stride, badge, "🎯 生效", 11, UI_ON_ACCENT);
        } else if (is_overridden) {
            fill_round_rect(pixels, stride, badge, 4, UI_WARNING_SOFT);
            draw_text_center(pixels, stride, badge, "🛡️ 已覆盖", 10, UI_WARNING);
        } else {
            fill_round_rect(pixels, stride, badge, 4, UI_BORDER);
            draw_text_center(pixels, stride, badge, ptc_ui_decision_state_label(step->state), 10, UI_MUTED);
        }

        /* 3. Rule Value */
        draw_text_center(pixels, stride, (UiRect){card.x, card.y + 50, card.width, 22},
                         rule_val, 14, is_selected ? UI_SUCCESS : (is_overridden ? UI_MUTED : UI_DISABLED));

        /* 4. Subtext explanation */
        const char *desc = is_selected ? "在此命中 / 阻断" :
            (is_overridden ? "已被上级覆盖" :
             (step->state == PTC_UI_DECISION_DISABLED ? "未开启" : "未匹配 / 穿透"));
        draw_text_center(pixels, stride, (UiRect){card.x + 4, card.y + 74, card.width - 8, 26},
                         desc, 10, is_selected ? UI_SUCCESS : UI_MUTED);

        /* 5. Connector line between node i and node i+1 */
        if (i < count - 1) {
            int line_start_x = card.x + card.width;
            int line_end_x = line_start_x + gap;
            int line_y = cards_y + node_h / 2;

            if (is_selected) {
                /* Short-circuit stop mark: ─┤ */
                draw_line(pixels, stride, line_start_x, line_y, line_start_x + gap / 2, line_y, 2, UI_MUTED);
                draw_line(pixels, stride, line_start_x + gap / 2, line_y - 10, line_start_x + gap / 2, line_y + 10, 3, UI_DANGER);
                draw_text(pixels, stride, line_start_x + 4, line_y - 18, "阻断", 10, UI_DANGER);
                hit_found = true;
            } else if (!hit_found) {
                /* Active flow arrow: ──> */
                draw_line(pixels, stride, line_start_x, line_y, line_end_x, line_y, 2, UI_ACCENT);
                draw_text(pixels, stride, line_start_x + gap / 2 - 4, line_y - 8, ">", 12, UI_ACCENT);
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
    const char *notice = home_runtime_notice(model);
    bool error = strcmp(model->result_status, "error") == 0;
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

    int top_y = dialog.y + 106;
    int col_w = 524;
    int x_left = dialog.x + 28;
    int x_right = dialog.x + 568;

    /* ======================== 左栏：额度判定与规则决策链 ======================== */
    /* 1. Hero Card (524 x 88) */
    UiRect hero = {x_left, top_y, col_w, 88};
    fill_round_rect_gradient(pixels, stride, hero, 12, UI_ACCENT_SOFT,
                             UI_RGB(ui_darken(UI_BLENDED(accent_soft), 5)));
    draw_text(pixels, stride, hero.x + 16, hero.y + 24, "今天还可玩", 13, UI_MUTED);
    draw_text(pixels, stride, hero.x + 16, hero.y + 68, remaining, 34, fresh ? UI_ACCENT : UI_MUTED);
    draw_text(pixels, stride, hero.x + 194, hero.y + 24, "今日总额度", 13, UI_MUTED);
    draw_text(pixels, stride, hero.x + 194, hero.y + 60, total, 20, UI_INK);
    draw_text(pixels, stride, hero.x + 350, hero.y + 24, "额度已耗(估算)", 13, UI_MUTED);
    if (fresh && played >= 0) snprintf(line, sizeof(line), "约 %d 分钟", played);
    else snprintf(line, sizeof(line), "暂不可用");
    draw_text(pixels, stride, hero.x + 350, hero.y + 60, line, 20, UI_INK);

    /* 2. 当前生效规则高亮卡片 (524 x 56) */
    UiRect active = {x_left, top_y + 96, col_w, 56};
    fill_round_rect(pixels, stride, active, 10, fresh ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
    draw_rect_outline(pixels, stride, active, 10, 2, fresh ? UI_SUCCESS : UI_WARNING);
    draw_text(pixels, stride, active.x + 16, active.y + 22,
              fresh ? "当前生效规则" : "规则状态待确认", 13, fresh ? UI_SUCCESS : UI_WARNING);
    draw_text(pixels, stride, active.x + 118, active.y + 22, effective, 15, UI_INK);
    draw_wrapped_text(pixels, stride, active.x + 16, active.y + 42, decision.final_reason,
                      12, active.width - 32, 16, 1, UI_MUTED);

    /* 3. 决策链流式优先级管道 (Option 3A) (524 x 134) */
    draw_waterfall_pipeline(pixels, stride, x_left, top_y + 158, col_w, &decision);

    /* 4. 并行就寝与自主缓冲 (524 x 54) */
    UiRect bedtime = {x_left, top_y + 300, 256, 54};
    UiRect autonomy = {x_left + 268, top_y + 300, 256, 54};
    fill_round_rect(pixels, stride, bedtime, 10, UI_WARNING_SOFT);
    draw_rect_outline(pixels, stride, bedtime, 10, 1, UI_WARNING);
    fill_round_rect(pixels, stride, autonomy, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, autonomy, 10, 1, UI_BORDER);
    draw_text(pixels, stride, bedtime.x + 12, bedtime.y + 20, "🌙 并行就寝限制", 12, UI_WARNING);
    draw_text(pixels, stride, bedtime.x + 12, bedtime.y + 42, decision.bedtime, 13, UI_INK);
    draw_text(pixels, stride, autonomy.x + 12, autonomy.y + 20, "🎁 自主缓冲额度", 12, UI_MUTED);
    draw_text(pixels, stride, autonomy.x + 12, autonomy.y + 42, decision.autonomy, 13, UI_INK);

    /* 5. 规则决策说明底注 */
    draw_text(pixels, stride, x_left, top_y + 372,
              "规则链决定今日额度；就寝独立并行，自主缓冲按条件追加", 11, UI_MUTED);

    /* ======================== 右栏：系统运行、历史趋势与诊断 ======================== */
    /* 1. 系统运行与计时器状态 (524 x 88) */
    UiRect sys_card = {x_right, top_y, col_w, 88};
    fill_round_rect(pixels, stride, sys_card, 12, UI_RAISED);
    draw_rect_outline(pixels, stride, sys_card, 12, 1, UI_BORDER);
    draw_text(pixels, stride, sys_card.x + 16, sys_card.y + 22, "今日规则模式", 12, UI_MUTED);
    draw_text(pixels, stride, sys_card.x + 16, sys_card.y + 48, today, 16, UI_INK);
    draw_text(pixels, stride, sys_card.x + 180, sys_card.y + 22, "系统计时器", 12, UI_MUTED);
    draw_text(pixels, stride, sys_card.x + 180, sys_card.y + 48, timer, 16, fresh ? UI_INK : UI_WARNING);
    draw_text(pixels, stride, sys_card.x + 350, sys_card.y + 22, "PlayWise 守护", 12, UI_MUTED);
    draw_text(pixels, stride, sys_card.x + 350, sys_card.y + 48, runtime, 16,
              notice[0] || !fresh ? UI_WARNING : UI_SUCCESS);
    if (notice[0]) {
        fill_round_rect(pixels, stride, (UiRect){sys_card.x + 10, sys_card.y + 60, sys_card.width - 20, 22}, 4, UI_WARNING_SOFT);
        draw_text(pixels, stride, sys_card.x + 16, sys_card.y + 76, notice, 11, UI_WARNING);
    }

    /* 2. 近 7 天与近 30 天消耗趋势 (524 x 86) */
    UiRect card7 = {x_right, top_y + 96, 256, 86};
    UiRect card30 = {x_right + 268, top_y + 96, 256, 86};
    fill_round_rect(pixels, stride, card7, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, card7, 10, 1, UI_BORDER);
    fill_round_rect(pixels, stride, card30, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, card30, 10, 1, UI_BORDER);

    draw_text(pixels, stride, card7.x + 14, card7.y + 22, "近 7 天额度消耗估算", 12, UI_MUTED);
    if (model->usage_summary_available && model->usage_known_days_7 > 0)
        snprintf(line, sizeof(line), "%u 分钟", model->usage_consumed_minutes_7);
    else snprintf(line, sizeof(line), "暂不可用");
    draw_text(pixels, stride, card7.x + 14, card7.y + 52, line, 22, UI_INK);
    snprintf(line, sizeof(line), "可靠记录 %u 天", model->usage_known_days_7);
    draw_text(pixels, stride, card7.x + 14, card7.y + 74, line, 11, UI_MUTED);

    draw_text(pixels, stride, card30.x + 14, card30.y + 22, "近 30 天额度消耗估算", 12, UI_MUTED);
    if (model->usage_summary_available && model->usage_known_days_30 > 0)
        snprintf(line, sizeof(line), "%u 分钟", model->usage_consumed_minutes_30);
    else snprintf(line, sizeof(line), "暂不可用");
    draw_text(pixels, stride, card30.x + 14, card30.y + 52, line, 22, UI_INK);
    snprintf(line, sizeof(line), "可靠记录 %u 天", model->usage_known_days_30);
    draw_text(pixels, stride, card30.x + 14, card30.y + 74, line, 11, UI_MUTED);

    /* 3. 家长指令执行审计与反馈 (524 x 146) */
    UiRect audit = {x_right, top_y + 190, col_w, 146};
    fill_round_rect(pixels, stride, audit, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, audit, 10, 1, UI_BORDER);
    draw_text(pixels, stride, audit.x + 14, audit.y + 22, "最近指令执行审计", 13, UI_INK);
    snprintf(line, sizeof(line), "%s / %s", model->command_name, model->transport_label);
    draw_text(pixels, stride, audit.x + 200, audit.y + 22, line, 12, UI_MUTED);
    draw_line(pixels, stride, audit.x + 14, audit.y + 34, audit.x + audit.width - 14, audit.y + 34, 1, UI_BORDER);
    draw_wrapped_text(pixels, stride, audit.x + 14, audit.y + 54, model->message,
                      13, audit.width - 28, 17, 2, error ? UI_DANGER : UI_INK);
    draw_wrapped_text(pixels, stride, audit.x + 14, audit.y + 96, model->feedback_detail,
                      11, audit.width - 28, 15, 2, error ? UI_DANGER : UI_MUTED);

    /* 4. 统计口径与系统说明 */
    draw_text(pixels, stride, x_right, top_y + 356,
              "本机额度消耗包含 HOME 亮屏使用；游戏明细暂不可用。", 11, UI_MUTED);
    draw_text(pixels, stride, x_right, top_y + 372,
              "缺失日期不计入累计；异常请在“支持与恢复”中处理。", 11, UI_MUTED);
}

static void draw_home_details(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    char line[256], timer[64], remaining[64], today[64], age[64], total[64];
    bool parent = model->view == PTC_UI_PARENT;
    bool error = strcmp(model->result_status, "error") == 0;
    const char *notice = home_runtime_notice(model);
    bool fresh = ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    draw_dialog_shell(pixels, stride, model, &dialog, 1120, 640);
    if (parent) {
        for (int index = 0; index < 2; ++index) {
            PtcUiRect tab = ptc_ui_home_details_tab_rect(index);
            bool selected = model->home_details_page == index;
            draw_candidate_button(pixels, stride, tab,
                index == 0 ? "今日全景看板" : "使用与状态",
                selected ? UI_ACCENT : UI_PAGE,
                selected ? UI_ON_ACCENT : UI_ACCENT, selected, false);
        }
        if (model->home_details_page == 0) {
            draw_home_decision_details(pixels, stride, model, dialog);
            home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "A / B  返回", false, true, false);
            return;
        }
    }
    int x = dialog.x + 32, y = dialog.y + (parent ? 108 : 80);
    UiRect hero = {x, y, 688, 200};
    UiRect status = {x + 704, y, 352, 200};
    ptc_ui_format_timer_status(model, timer, sizeof(timer));
    ptc_ui_format_home_remaining(model, ptc_ui_render_now(), remaining, sizeof(remaining));
    ptc_ui_format_home_total_value(model, total, sizeof(total));
    ptc_ui_format_today_mode(model, today, sizeof(today));
    format_status_age(model, age, sizeof(age));
    if (!fresh) {
        snprintf(total, sizeof(total), "暂不可用");
        snprintf(today, sizeof(today), "状态待确认");
        snprintf(timer, sizeof(timer), "状态待确认");
    }
    draw_text_center(pixels, stride, (UiRect){dialog.x + 736, dialog.y + 26, 352, 30}, age, 17, status_age_color(model));

    fill_round_rect_gradient(pixels, stride, hero, 16, UI_ACCENT_SOFT,
                             UI_RGB(ui_darken(UI_BLENDED(accent_soft), 5)));
    draw_text(pixels, stride, x + 24, y + 32, "今天还可玩", 20, UI_MUTED);
    draw_text(pixels, stride, x + 24, y + 100, remaining, 56, fresh ? UI_ACCENT : UI_MUTED);
    draw_detail_metric(pixels, stride, x + 24, y + 142, "今日总额度", total, UI_INK);
    if (fresh && model->played_minutes_available && model->played_minutes >= 0)
        snprintf(line, sizeof(line), "约 %d 分钟", model->played_minutes);
    else snprintf(line, sizeof(line), "暂不可用");
    draw_detail_metric(pixels, stride, x + 360, y + 142, "今日额度消耗估算", line, UI_INK);

    fill_round_rect(pixels, stride, status, 16, UI_RAISED);
    const char *runtime = !model->status_loaded ? "等待刷新" :
        model->disable_flag_present ? "控制已停用" : model->recovery_active ? "正在恢复" :
        model->apply_pending_confirmation ? "等待生效" : !fresh ? "状态待确认" :
        strcmp(model->setup_phase, "active") == 0 ? "正常运行" : "需家长确认";
    const char *labels[] = {"今日规则", "系统计时器", "PlayWise"};
    const char *values[] = {today, timer, runtime};
    for (int i = 0; i < 3; ++i) {
        draw_text(pixels, stride, status.x + 24, y + 28 + i * 60, labels[i], 16, UI_MUTED);
        fit_text(line, sizeof(line), values[i], 22, status.width - 48);
        draw_text(pixels, stride, status.x + 24, y + 54 + i * 60, line, 22,
            i == 2 && (notice[0] || !fresh) ? UI_WARNING : UI_INK);
    }

    int history_y = y + 216;
    if (notice[0]) {
        fill_round_rect(pixels, stride, (UiRect){x, history_y, 1056, 44}, 6,
            model->disable_flag_present ? UI_DANGER_SOFT : UI_WARNING_SOFT);
        draw_wrapped_text(pixels, stride, x + 16, history_y + 19, notice, 17, 1024, 20, 2,
            model->disable_flag_present ? UI_DANGER : UI_WARNING);
        history_y += 52;
    }
    for (int i = 0; i < 2; ++i) {
        UiRect card = {x + i * 536, history_y, 520, 98};
        unsigned int days = i ? model->usage_known_days_30 : model->usage_known_days_7;
        unsigned int minutes = i ? model->usage_consumed_minutes_30 : model->usage_consumed_minutes_7;
        fill_round_rect(pixels, stride, card, 16, UI_RAISED);
        if (model->usage_summary_available && days > 0)
            snprintf(line, sizeof(line), "%u 分钟", minutes);
        else snprintf(line, sizeof(line), "暂不可用");
        draw_detail_metric(pixels, stride, card.x + 24, card.y + 28,
            i ? "近 30 天额度消耗估算" : "近 7 天额度消耗估算", line, UI_INK);
        if (model->usage_summary_available)
            snprintf(line, sizeof(line), "可靠记录 %u 天", days);
        else snprintf(line, sizeof(line), "可靠记录暂不可用");
        draw_text(pixels, stride, card.x + 24, card.y + 86, line, 15, UI_MUTED);
    }
    draw_text(pixels, stride, x, history_y + 122,
        "本机额度消耗包含 HOME 等亮屏使用；游戏明细暂不可用，缺失日期不计入估算", 16, UI_MUTED);
    if (parent) {
        fill_rect(pixels, stride, (UiRect){x, history_y + 140, 1056, 1}, UI_BORDER);
        snprintf(line, sizeof(line), "最近执行  %s    %s", model->command_name, model->transport_label);
        fit_text(line, sizeof(line), line, 16, 1056);
        draw_text(pixels, stride, x, history_y + 162, line, 16, UI_MUTED);
        draw_wrapped_text(pixels, stride, x, history_y + 187, model->message, 19, 1056, 24, 2,
            error ? UI_DANGER : UI_INK);
        /* The last two detail lines reserve the return button's entire column. */
        draw_wrapped_text(pixels, stride, x, history_y + 231, model->feedback_detail, 16, 600, 20, 2,
            error ? UI_DANGER : UI_MUTED);
    }
    home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), "A / B  返回", false, true, false);
}

void draw_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
    case PTC_UI_OVERLAY_HOME_DETAILS:
        draw_home_details(pixels, stride, model);
        break;
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
    case PTC_UI_OVERLAY_CREDENTIAL:
        draw_credential_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_GRANT_MANAGER:
        draw_grant_manager_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_REDEMPTION_HISTORY:
        draw_redemption_history_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_ACTIVITY_HISTORY:
        draw_activity_history_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_SCHEDULED_LEAVE:
        draw_scheduled_leave(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_SCHEDULED:
        draw_scheduled_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_AUTONOMY:
        draw_autonomy_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_QUICK_ADD:
        draw_quick_add_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_BEDTIME:
        draw_bedtime_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_BEDTIME_WINDOW:
        draw_bedtime_window_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_BEDTIME_SPECIAL:
        draw_bedtime_special_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_BEDTIME_LEAVE:
        draw_bedtime_leave_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_BEDTIME_BULK:
        draw_bedtime_bulk_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
        draw_shortcut_manager_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_GRANT_LOCAL:
        draw_grant_local_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_QR:
        draw_qr_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_WEEKLY_LEAVE:
        draw_weekly_leave_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_CREDENTIAL_LEAVE:
        draw_credential_leave_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_CODE_RESULT:
        draw_code_result_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_AUTH_ERROR:
        draw_auth_error_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_SOFTWARE_INFO:
        draw_software_info_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_HOLIDAY_CALENDAR:
        draw_holiday_calendar_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_HOLIDAY_LEAVE:
        draw_holiday_leave_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_SUPPORT_EVENT:
        draw_support_event_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_WEEKLY_BULK:
        draw_weekly_bulk_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_ALBUM_MANAGER:
        draw_album_manager_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_THEME:
        draw_theme_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_NONE:
    default:
        break;
    }
}
