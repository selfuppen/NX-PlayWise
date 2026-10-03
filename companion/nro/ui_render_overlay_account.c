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
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 122, ptc_ui_text(PTC_UI_T_CURRENT_VALUE), 17, UI_MUTED);
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
                              model->credential_revealed ? ptc_ui_text(PTC_UI_T_ZR_HIDE_CURRENT_KEY) : ptc_ui_text(PTC_UI_T_ZR_DISPLAYS_THE_CURRENT_KEY),
                              UI_PAGE, UI_ACCENT,
                              model->overlay_selection == PTC_UI_CREDENTIAL_REVEAL, false);
    }
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 215, ptc_ui_text(PTC_UI_T_NEW_VALUE), 17, UI_MUTED);
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
    draw_candidate_button(pixels, stride, ptc_ui_credential_random_rect(), ptc_ui_text(PTC_UI_T_Y_RANDOMLY_GENERATED),
                          UI_PAGE, UI_ACCENT,
                          model->overlay_selection == PTC_UI_CREDENTIAL_RANDOM, false);
    if (model->credential_kind == 2) {
        draw_candidate_button(pixels, stride, ptc_ui_credential_demo_rect(),
                              model->demo_secret_enabled ? ptc_ui_text(PTC_UI_T_R_EXIT_THE_DEMO_AND_CHANGE_THE) : ptc_ui_text(PTC_UI_T_R_USE_PUBLIC_DEMO_KEY),
                              model->demo_secret_enabled ? UI_PAGE : UI_DANGER_SOFT,
                              model->demo_secret_enabled ? UI_ACCENT : UI_DANGER,
                              model->overlay_selection == PTC_UI_CREDENTIAL_DEMO, false);
        draw_text(pixels, stride, dialog.x + 332, dialog.y + 350,
                  ptc_ui_text(PTC_UI_T_IT_IS_RECOMMENDED_TO_USE_RANDOM_GENERATION), 17, UI_MUTED);
    }
    valid = model->credential_kind == 1
        ? ptc_device_id_valid(model->credential_new)
        : ptc_grant_secret_valid(model->credential_new);
    dirty = strcmp(model->credential_current, model->credential_new) != 0;
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                          dirty ? ptc_ui_text(PTC_UI_T_SAVE) : ptc_ui_text(PTC_UI_T_NO_MODIFICATION),
                          UI_ACCENT, UI_ON_ACCENT,
                          model->overlay_selection == PTC_UI_CREDENTIAL_SAVE, !valid || !dirty);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
                       UI_RAISED, UI_INK, true);
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 406,
              ptc_ui_text(PTC_UI_T_DIRECTION_KEY_SELECTION_A_OK_X_MANUAL), 16, UI_MUTED);
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
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), ptc_ui_text(PTC_UI_T_CONFIRMING_GRANT_RESULT));
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 ptc_ui_text(PTC_UI_T_THE_LAST_CONFIRMED_REDEMPTION_REQUEST_HAS_BEEN));
    } else if (model->code_result_failed) {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), ptc_ui_text(PTC_UI_T_GRANT_FAILED));
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "%s", ptc_ui_code_failure_guidance(model->error_code));
    } else {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), ptc_ui_text(PTC_UI_T_GRANT_SUCCESSFUL));
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 ptc_ui_text(PTC_UI_T_THIS_GRANT_CODE_HAS_BEEN_USED_AND));
    }
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 760, 420);
    if (model->code_before_unlimited) snprintf(before_value, sizeof(before_value), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    else format_duration(model->code_before_remaining_available ? model->code_before_remaining_minutes : -1,
                         before_value, sizeof(before_value));
    if (model->code_result_pending) {
        format_duration(model->code_preview_after_available ? model->code_preview_after_minutes : -1,
                        after_value, sizeof(after_value));
    } else if (!remaining_fresh) snprintf(after_value, sizeof(after_value), ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
    else if (model->unrestricted_today == 1) snprintf(after_value, sizeof(after_value), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    else format_duration(model->remaining_available ? model->remaining_minutes : -1,
                         after_value, sizeof(after_value));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 142, 300, 92},
                         model->code_result_pending || model->code_result_failed ? ptc_ui_text(PTC_UI_T_BEFORE_GRANT) : ptc_ui_text(PTC_UI_T_ACTUAL_INCREASE),
                         model->code_result_pending || model->code_result_failed ? before_value :
                         (model->code_actual_add_available ? actual_value : ptc_ui_text(PTC_UI_T_UNAVAILABLE)),
                         UI_RGB(UI_BLENDED(text_primary)));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 406, dialog.y + 142, 300, 92},
                         model->code_result_pending ? ptc_ui_text(PTC_UI_T_PREVIEW_AFTER_GRANT) : (model->code_result_failed ? ptc_ui_text(PTC_UI_T_LAST_REMAINING_READING) : ptc_ui_text(PTC_UI_T_REMAINING_AFTER_REDEMPTION)), after_value,
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
                     model->code_result_pending ? ptc_ui_text(PTC_UI_T_CAN_BE_CLOSED_DURING_RESULT_CONFIRMATION_IT) :
                     (model->code_result_failed ? ptc_ui_text(PTC_UI_T_NO_CODES_WERE_CONSUMED_THIS_TIME_USED) : (model->code_actual_add_available && model->code_actual_add_minutes < model->code_grant_minutes
                         ? ptc_ui_text(PTC_UI_T_HAS_REACHED_THE_DAILY_LIMIT_AND_THE) :
                         (model->code_actual_add_available ? ptc_ui_text(PTC_UI_T_THE_EXCHANGE_RESULT_HAS_BEEN_CONFIRMED_AND) : ptc_ui_text(PTC_UI_T_SUCCESS_HAS_BEEN_CONFIRMED_GRANT_DETAILS_CANNOT)))),
                     19, model->code_result_pending ? UI_RGB(UI_BLENDED(text_secondary)) :
                     (model->code_result_failed ? UI_DANGER : UI_SUCCESS));
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_RETURN_TO_CHILD_AREA),
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), model->code_result_pending ? ptc_ui_text(PTC_UI_T_A_CLOSE) : ptc_ui_text(PTC_UI_T_A_COMPLETE),
                       UI_ACCENT, UI_ON_ACCENT, false);
}

static void draw_auth_error_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell_model = *model;
    char retry_label[64];
    snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "%s",
             model->auth_error_title[0] ? model->auth_error_title : ptc_ui_text(PTC_UI_T_PIN_VERIFICATION_FAILED));
    snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body), "%s",
             model->auth_error_message[0] ? model->auth_error_message : ptc_ui_text(PTC_UI_T_PIN_IS_INCORRECT_PLEASE_TRY_AGAIN));
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 720, 340);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 44, dialog.y + 142, dialog.width - 88, 72}, 16, UI_DANGER_SOFT);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 58, dialog.y + 142, dialog.width - 116, 72},
                     model->auth_cooldown_seconds > 0
                        ? ptc_ui_text(PTC_UI_T_TOO_MANY_ERRORS_YOU_CAN_TRY_AGAIN)
                        : ptc_ui_text(PTC_UI_T_ERROR_PIN_WILL_NOT_BE_RETAINED_INPUT),
                     18, UI_DANGER);
    if (model->auth_cooldown_seconds > 0) {
        snprintf(retry_label, sizeof(retry_label), ptc_ui_text(PTC_UI_T_WAIT_D_SECONDS), model->auth_cooldown_seconds);
    } else {
        snprintf(retry_label, sizeof(retry_label), ptc_ui_text(PTC_UI_T_A_RE_ENTER));
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_CANCEL),
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
              fitted[0] ? fitted : ptc_ui_text(PTC_UI_T_DIRECTION_KEY_SELECTION_A_CONFIRM_B_RETURN), 16,
              fitted[0] ? UI_SUCCESS : UI_MUTED);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
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
    snprintf(page_text, sizeof(page_text), ptc_ui_text(PTC_UI_T_NEWEST_FIRST_PAGE_D_D_D_ENTRIES),
             model->redemption_history_page + 1, pages, model->redemption_history_count);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 104, page_text, 16, UI_MUTED);
    if (!model->redemption_history_available) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
                         ptc_ui_text(PTC_UI_T_THE_USAGE_RECORD_CANNOT_BE_READ_TEMPORARILY),
                         19, UI_DANGER);
    } else if (model->redemption_history_count == 0) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
                         ptc_ui_text(PTC_UI_T_THERE_IS_NO_RECORD_OF_SUCCESSFUL_USE),
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
                snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_AFTER_REDEMPTION_S), value);
            } else {
                snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_TEMPORARILY_UNAVAILABLE_AFTER_REDEMPTION));
            }
            snprintf(allowance, sizeof(allowance), ptc_ui_text(PTC_UI_T_CODE_U_MIN_CREDITED_U_MIN_S),
                     (unsigned int)record->grant_minutes,
                     (unsigned int)record->effective_add_minutes,
                     record->effective_add_minutes < record->grant_minutes ? ptc_ui_text(PTC_UI_T_DAILY_LIMIT_REACHED) : "");
            fill_round_rect(pixels, stride, item, 16, UI_RAISED);
            draw_rect_outline(pixels, stride, item, 16, 1, UI_BORDER);
            draw_text(pixels, stride, item.x + 16, item.y + 20, time_text, 15, UI_INK);
            draw_text(pixels, stride, item.x + 250, item.y + 20, allowance, 15, UI_ACCENT);
            draw_text(pixels, stride, item.x + 720, item.y + 20, remaining, 15, UI_INK);
            draw_text(pixels, stride, item.x + item.width - 84, item.y + 20,
                      record->token_version == 2u ? ptc_ui_text(PTC_UI_T_V2_SUCCESSFUL) : ptc_ui_text(PTC_UI_T_V1_SUCCESSFUL), 14, UI_SUCCESS);

            int cy = item.y + 27;
            int cx = dialog.x + 48;
            fill_round_rect(pixels, stride, (UiRect){cx - 4, cy - 4, 8, 8}, 4, UI_SUCCESS);
            fill_round_rect(pixels, stride, (UiRect){cx - 2, cy - 2, 4, 4}, 2, UI_SURFACE);
        }
    }
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_prev_rect(), ptc_ui_text(PTC_UI_T_L_LEFT_PREVIOUS_PAGE),
                       UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_next_rect(), ptc_ui_text(PTC_UI_T_R_RIGHT_NEXT_PAGE),
                       UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_X_CLEAR_ALL),
                       UI_DANGER_SOFT, UI_DANGER, false);
}

static const char *activity_label(const char *action)
{
    if (strcmp(action, "today_limit") == 0) return ptc_ui_text(PTC_UI_T_MODIFY_TODAY_S_TOTAL_QUOTA);
    if (strcmp(action, "today_add") == 0) return ptc_ui_text(PTC_UI_T_TEMPORARY_EXTRA_TIME_FOR_PARENTS);
    if (strcmp(action, "today_unlimited") == 0) return ptc_ui_text(PTC_UI_T_CHANGED_TO_UNLIMITED_TIME_TODAY);
    if (strcmp(action, "today_restore") == 0) return ptc_ui_text(PTC_UI_T_RESTORE_TODAY_S_PLAN);
    if (strcmp(action, "weekly_update") == 0) return ptc_ui_text(PTC_UI_T_MODIFY_WEEKLY_PLAN);
    if (strcmp(action, "holiday_update") == 0) return ptc_ui_text(PTC_UI_T_MODIFY_HOLIDAY_RULES);
    if (strcmp(action, "scheduled_update") == 0) return ptc_ui_text(PTC_UI_T_MODIFY_THE_QUOTA_ON_THE_SPECIFIED_DATE);
    if (strcmp(action, "autonomy_update") == 0) return ptc_ui_text(PTC_UI_T_MODIFY_AUTONOMOUS_BUFFERING);
    if (strcmp(action, "offline_grant") == 0) return ptc_ui_text(PTC_UI_T_REDEEM_EXTRA_TIME_CODE);
    if (strcmp(action, "daily_buffer") == 0) return ptc_ui_text(PTC_UI_T_RECEIVE_INDEPENDENT_BUFFERING);
    if (strcmp(action, "protection") == 0) return ptc_ui_text(PTC_UI_T_PROTECTION_EVENT);
    return ptc_ui_text(PTC_UI_T_ACTIVITY_RECORD);
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
    snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_NEWEST_FIRST_PAGE_D_D_D_ENTRIES_2),
        model->activity_history_page + 1, pages, model->activity_history_count);
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 104, line, 15, UI_MUTED);
    if (!model->activity_history_available) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
            ptc_ui_text(PTC_UI_T_FAMILY_ACTIVITY_LOGGING_IS_TEMPORARILY_UNAVAILABLE_CONTROL), 19, UI_DANGER);
    } else if (model->activity_history_count == 0) {
        draw_text_center(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 230, dialog.width - 68, 44},
            ptc_ui_text(PTC_UI_T_THERE_IS_NO_RECORD_OF_FAMILY_ACTIVITIES), 19, UI_MUTED);
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
            snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_S_S_PLANNED_U_MIN_ACTUAL_U),
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
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_prev_rect(), ptc_ui_text(PTC_UI_T_L_LEFT_PREVIOUS_PAGE),
        UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_redemption_history_next_rect(), ptc_ui_text(PTC_UI_T_R_RIGHT_NEXT_PAGE),
        UI_PAGE, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
        UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_X_CLEAR_ALL),
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
    snprintf(shell.overlay_body, sizeof(shell.overlay_body), ptc_ui_text(PTC_UI_T_CHOOSE_THE_DURATION_GENERATE_IT_AND_TELL));
    draw_dialog_shell(pixels, stride, &shell, &dialog, 920, 650);
    if (reliable && model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    else format_duration(reliable && model->remaining_available ? model->remaining_minutes : -1, remaining, sizeof(remaining));
    format_duration(expected, estimate, sizeof(estimate));
    format_status_age(model, freshness, sizeof(freshness));
    if (model->grant_status_refresh_failed) snprintf(freshness, sizeof(freshness), ptc_ui_text(PTC_UI_T_REFRESH_FAILED_REFRESH_AND_TRY_AGAIN_AFTER));
    snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_S_LEFT_TODAY_S), remaining, freshness);
    if (model->grant_notice[0]) snprintf(line, sizeof(line), "%s", model->grant_notice);
    draw_text(pixels, stride, dialog.x + 42, dialog.y + 126, line, 18, UI_RGB(UI_BLENDED(text_secondary)));

    fill_round_rect(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 144, 852, 192}, 16, UI_RGB(UI_BLENDED(surface_raised)));
    draw_text(pixels, stride, dialog.x + 54, dialog.y + 176, ptc_ui_text(PTC_UI_T_THE_NEXT_GRANT_DURATION), 20, UI_RGB(UI_BLENDED(text_secondary)));
    char played[48];
    format_duration(reliable && model->played_minutes_available ? model->played_minutes : -1, played, sizeof(played));
    snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_ESTIMATED_USED_S), played);
    draw_text(pixels, stride, dialog.x + 420, dialog.y + 176, line, 18, UI_RGB(UI_BLENDED(text_secondary)));
    snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_MIN_A_ENTER), (unsigned)model->grant_minutes);
    draw_candidate_button(pixels, stride, ptc_ui_grant_adjust_rect(0), line,
        UI_ACCENT_SOFT, UI_ACCENT,
        model->overlay_selection == PTC_UI_GRANT_LOCAL_ADJUST_FIRST, false);
    snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_EST_AFTER_REDEMPTION_S_S), estimate, capped ? ptc_ui_text(PTC_UI_T_DAILY_LIMIT_REACHED) : "");
    draw_text(pixels, stride, dialog.x + 420, dialog.y + 238, line, 20, UI_RGB(UI_BLENDED(text_secondary)));
    draw_text(pixels, stride, dialog.x + 54, dialog.y + 320, ptc_ui_text(PTC_UI_T_ADJUSTING_THIS_WILL_NOT_CHANGE_THE_DURATION), 18, UI_RGB(UI_BLENDED(text_secondary)));

    fill_round_rect(pixels, stride, (UiRect){dialog.x + 34, dialog.y + 350, 852, 170}, 16, UI_RGB(UI_BLENDED(surface_raised)));
    draw_text(pixels, stride, dialog.x + 54, dialog.y + 382, ptc_ui_text(PTC_UI_T_CODE_GENERATED), 20, UI_RGB(UI_BLENDED(text_secondary)));
    if (model->grant_has_code) {
        ptc_ui_format_code(model->grant_code, code, sizeof(code));
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 432, code, 42, UI_RGB(UI_BLENDED(text_primary)));
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_CODE_DURATION_U_MIN), (unsigned)model->grant_issued_minutes);
        draw_text(pixels, stride, dialog.x + 420, dialog.y + 426, line, 25, UI_RGB(UI_BLENDED(text_primary)));
        format_duration(model->grant_estimate_available ? model->grant_estimate_minutes : -1, estimate, sizeof(estimate));
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_EST_REMAINING_WHEN_GENERATED_S_S), estimate, model->grant_estimate_capped ? ptc_ui_text(PTC_UI_T_DAILY_LIMIT_REACHED) : "");
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 466, line, 18, UI_RGB(UI_BLENDED(text_secondary)));
        if (ptc_date_from_day_index(model->grant_day_index, &year, &month, &day))
            snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_VALID_ON_U_02U_02U_ONE_USE), (unsigned)year, (unsigned)month, (unsigned)day);
        else snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_THE_ISSUANCE_DATE_NEEDS_TO_BE_CONFIRMED));
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 499, line, 18, UI_RGB(UI_BLENDED(text_secondary)));
    } else {
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 430, ptc_ui_text(PTC_UI_T_AFTER_SELECTING_THE_DURATION_PRESS_TO_GENERATE), 28, UI_RGB(UI_BLENDED(text_primary)));
        draw_text(pixels, stride, dialog.x + 54, dialog.y + 471, ptc_ui_text(PTC_UI_T_PIN_WILL_BE_VERIFIED_AGAIN_BEFORE_GENERATION), 18, UI_RGB(UI_BLENDED(text_secondary)));
    }
    draw_candidate_button(pixels, stride, ptc_ui_grant_generate_rect(),
        model->grant_has_code ? ptc_ui_text(PTC_UI_T_GENERATE_ANOTHER_ONE) : ptc_ui_text(PTC_UI_T_GENERATE_EXTRA_TIME_CODE), UI_ACCENT, UI_ON_ACCENT,
        model->overlay_selection == PTC_UI_GRANT_LOCAL_GENERATE, model->waiting);
    draw_candidate_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
        UI_RGB(UI_BLENDED(surface_raised)), UI_RGB(UI_BLENDED(text_primary)), model->overlay_selection == PTC_UI_GRANT_LOCAL_BACK, false);
    draw_text(pixels, stride, dialog.x + 280, dialog.y + 615, ptc_ui_text(PTC_UI_T_DIRECTION_KEY_SELECTION_A_EDIT_CODE_DURATION), 18, UI_RGB(UI_BLENDED(text_secondary)));
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
    draw_text(pixels, stride, dialog.x + 34, dialog.y + 142, ptc_ui_text(PTC_UI_T_RECOMMENDED_SOLUTION_ONE_SCAN_THE_QR_CODE), 23, UI_SUCCESS);
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
              ptc_ui_text(PTC_UI_T_SCAN_THE_CODE_ONLY_WHEN_THE_WEB), 14, UI_MUTED);
    next_y = draw_wrapped_text(pixels, stride, dialog.x + 34, dialog.y + 552,
                               model->pairing_base_url, 13, 400, 19, 3, UI_ACCENT);
    draw_text(pixels, stride, dialog.x + 34, next_y + 4,
              ptc_ui_text(PTC_UI_T_THE_QR_CODE_CONTAINS_A_TIME_CODE), 14, UI_DANGER);

    draw_text(pixels, stride, dialog.x + 470, dialog.y + 142, ptc_ui_text(PTC_UI_T_ALTERNATE_PLAN_TWO_SINGLE_FILE_OFFLINE_VERSION), 23, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 178,
              ptc_ui_text(PTC_UI_T_1_UNZIP_THE_COMPLETE_DELIVERY_PACKAGE_AND), 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 208,
              ptc_ui_text(PTC_UI_T_2_PRESS_A_TO_EXPORT_THE_CONFIGURATION), 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 488, dialog.y + 234,
              PLAYWISE_SD_ROOT "/parent-import.json", 15, UI_ACCENT);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 266,
              ptc_ui_text(PTC_UI_T_3_TRANSFER_HTML_AND_CONFIGURATION_FILES_TO), 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 298,
              ptc_ui_text(PTC_UI_T_4_OPEN_THE_HTML_IN_A_BROWSER), 15, UI_INK);
    draw_text(pixels, stride, dialog.x + 470, dialog.y + 330,
              ptc_ui_text(PTC_UI_T_5_SELECT_PARENT_IMPORT_JSON_THEN_IMPORT), 15, UI_INK);
    next_y = dialog.y + 362;
    draw_text(pixels, stride, dialog.x + 470, next_y + 6,
              ptc_ui_text(PTC_UI_T_NO_NETWORK_REQUIRED_FOR_DAILY_GENERATION_NO), 14, UI_SUCCESS);
    draw_text(pixels, stride, dialog.x + 470, next_y + 32,
              ptc_ui_text(PTC_UI_T_PLEASE_SAVE_THE_FILE_ON_YOUR_MOBILE), 14, UI_MUTED);
    draw_text(pixels, stride, dialog.x + 470, next_y + 58,
              ptc_ui_text(PTC_UI_T_DO_NOT_USE_THE_BUILT_IN_PREVIEWER), 14, UI_MUTED);
    fill_round_rect(pixels, stride, (UiRect){dialog.x + 470, next_y + 74, 610, 48}, 16, UI_DANGER_SOFT);
    draw_text(pixels, stride, dialog.x + 486, next_y + 104,
              ptc_ui_text(PTC_UI_T_THE_CONFIGURATION_FILE_CONTAINS_A_TIME_CODED), 15, UI_DANGER);

    UiRect export_button = to_uirect(ptc_ui_qr_export_rect());
    fill_round_rect(pixels, stride, export_button, 12, UI_ACCENT);
    draw_text_center(pixels, stride, export_button, ptc_ui_text(PTC_UI_T_A_EXPORT_CONFIGURATION_FILE), 18, UI_ON_ACCENT);

    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(PTC_UI_T_B_BACK),
                       UI_RAISED, UI_INK, true);
}

static void draw_parent_export_result_overlay(
    uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 800, 420);
    if (model->parent_export_succeeded) {
        draw_text(pixels, stride, dialog.x + 42, dialog.y + 134,
            ptc_ui_text(PTC_UI_T_THE_CONFIGURATION_FILE_HAS_BEEN_WRITTEN_TO), 20, UI_SUCCESS);
        draw_text(pixels, stride, dialog.x + 42, dialog.y + 176,
            PLAYWISE_SD_ROOT "/parent-import.json", 19, UI_ACCENT);
        draw_text(pixels, stride, dialog.x + 42, dialog.y + 220,
            ptc_ui_text(PTC_UI_T_COPY_FROM_THE_SWITCH_PLAYWISE_DIRECTORY_OF), 17, UI_INK);
        draw_text(pixels, stride, dialog.x + 42, dialog.y + 254,
            ptc_ui_text(PTC_UI_T_SELECT_IMPORT_CONFIG_ON_THE_OFFLINE_PARENT), 17, UI_INK);
        draw_text(pixels, stride, dialog.x + 42, dialog.y + 294,
            ptc_ui_text(PTC_UI_T_FILE_CONTAINS_A_TIME_CODED_KEY_AND), 15, UI_DANGER);
    } else {
        draw_text(pixels, stride, dialog.x + 42, dialog.y + 150,
            ptc_ui_text(PTC_UI_T_THE_CONFIGURATION_FILE_IS_NOT_EXPORTED), 22, UI_DANGER);
        draw_wrapped_text(pixels, stride, dialog.x + 42, dialog.y + 204,
            model->message, 18, dialog.width - 84, 28, 3, UI_INK);
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay),
        ptc_ui_text(PTC_UI_T_A_B_RETURN), UI_RAISED, UI_INK, true);
}

static void draw_credential_leave_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    draw_dialog_shell(pixels, stride, model, &dialog, 720, 300);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 132, dialog.width - 72, 30},
                     ptc_ui_text(PTC_UI_T_SELECT_LEFT_AND_RIGHT_A_CONFIRM_B), 17, UI_MUTED);
    draw_candidate_button(pixels, stride, ptc_ui_discard_rect(model->overlay), ptc_ui_text(PTC_UI_T_MESSAGE),
                          UI_DANGER_SOFT, UI_DANGER,
                          model->overlay_selection == 0, false);
    draw_candidate_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), ptc_ui_text(PTC_UI_T_A_CONTINUE_EDITING),
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
    case PTC_UI_OVERLAY_PARENT_EXPORT_RESULT:
        draw_parent_export_result_overlay(pixels, stride, model);
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
