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

bool draw_account_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
    case PTC_UI_OVERLAY_CREDENTIAL:
        draw_credential_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_GRANT_MANAGER:
        draw_grant_manager_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_REDEMPTION_HISTORY:
        draw_redemption_history_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_ACTIVITY_HISTORY:
        draw_activity_history_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_GRANT_LOCAL:
        draw_grant_local_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_QR:
        draw_qr_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_CREDENTIAL_LEAVE:
        draw_credential_leave_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_CODE_RESULT:
        draw_code_result_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_AUTH_ERROR:
        draw_auth_error_overlay(pixels, stride, model);
        return true;
    default:
        return false;
    }
}
