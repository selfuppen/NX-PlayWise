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
    char fitted[160];
    bool remaining_fresh = !model->code_result_failed && ptc_ui_status_is_fresh(model, ptc_ui_render_now());
    format_duration(model->code_actual_add_minutes, actual_value, sizeof(actual_value));
    if (model->code_result_pending) {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "%s", ptc_ui_text(PTC_UI_T_CONFIRMING_GRANT_RESULT));
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "%s", ptc_ui_text(PTC_UI_T_THE_LAST_CONFIRMED_REDEMPTION_REQUEST_HAS_BEEN));
    } else if (model->code_result_failed) {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "%s", ptc_ui_text(PTC_UI_T_GRANT_FAILED));
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "%s", ptc_ui_code_failure_guidance(model->error_code));
    } else {
        snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "%s", ptc_ui_text(PTC_UI_T_GRANT_SUCCESSFUL));
        snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body),
                 "%s", ptc_ui_text(PTC_UI_T_THIS_GRANT_CODE_HAS_BEEN_USED_AND));
    }
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 860, 476);

    /* Top-right semantic status badge */
    {
        const char *badge_txt = model->code_result_pending ? ptc_ui_text(PTC_UI_T_REDEEM_PENDING_BADGE)
            : (model->code_result_failed ? ptc_ui_text(PTC_UI_T_REDEEM_NOT_CONSUMED_BADGE)
                                         : ptc_ui_text(PTC_UI_T_REDEEM_SUCCESS_BADGE));
        uint32_t bbg = model->code_result_pending ? UI_WARNING_SOFT
            : (model->code_result_failed ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
        uint32_t bbd = model->code_result_pending ? UI_WARNING_BORDER
            : (model->code_result_failed ? UI_DANGER_BORDER : UI_SUCCESS_BORDER);
        uint32_t bfg = model->code_result_pending ? UI_WARNING_TEXT
            : (model->code_result_failed ? UI_DANGER : UI_SUCCESS);
        int bw = measure_text(badge_txt, 13) + 24;
        if (bw > 240) bw = 240;
        UiRect bpill = {dialog.x + dialog.width - 34 - bw, dialog.y + 26, bw, 28};
        fill_round_rect(pixels, stride, bpill, 14, bbg);
        draw_rect_outline(pixels, stride, bpill, 14, 1, bbd);
        fit_text(fitted, sizeof(fitted), badge_txt, 13, bpill.width - 14);
        draw_text_center(pixels, stride, bpill, fitted, 13, bfg);
    }

    draw_redeem_step_bar(pixels, stride,
                         (UiRect){dialog.x + 54, dialog.y + 72, dialog.width - 108, 30}, 2);

    /* Concise body guidance line */
    draw_wrapped_text(pixels, stride, dialog.x + 54, dialog.y + 124,
                      shell_model.overlay_body, 15, dialog.width - 108, 20, 2, UI_MUTED);

    if (model->code_before_unlimited) snprintf(before_value, sizeof(before_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    else format_duration(model->code_before_remaining_available ? model->code_before_remaining_minutes : -1,
                         before_value, sizeof(before_value));
    if (model->code_result_pending) {
        format_duration(model->code_preview_after_available ? model->code_preview_after_minutes : -1,
                        after_value, sizeof(after_value));
    } else if (!remaining_fresh) snprintf(after_value, sizeof(after_value), "%s", ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
    else if (model->unrestricted_today == 1) snprintf(after_value, sizeof(after_value), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
    else format_duration(model->remaining_available ? model->remaining_minutes : -1,
                         after_value, sizeof(after_value));

    int card_w = (dialog.width - 108 - 24) / 2;
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 158, card_w, 96},
                         model->code_result_pending || model->code_result_failed ? ptc_ui_text(PTC_UI_T_BEFORE_GRANT) : ptc_ui_text(PTC_UI_T_ACTUAL_INCREASE),
                         model->code_result_pending || model->code_result_failed ? before_value :
                         (model->code_actual_add_available ? actual_value : ptc_ui_text(PTC_UI_T_UNAVAILABLE)),
                         model->code_result_failed ? UI_INK : (model->code_result_pending ? UI_INK : UI_SUCCESS));
    draw_time_state_card(pixels, stride, (UiRect){dialog.x + 54 + card_w + 24, dialog.y + 158, card_w, 96},
                         model->code_result_pending ? ptc_ui_text(PTC_UI_T_PREVIEW_AFTER_GRANT) : (model->code_result_failed ? ptc_ui_text(PTC_UI_T_LAST_REMAINING_READING) : ptc_ui_text(PTC_UI_T_REMAINING_AFTER_REDEMPTION)), after_value,
                         time_state_accent(model->code_result_pending ? model->code_preview_after_available :
                                           (remaining_fresh && (model->unrestricted_today == 1 || model->remaining_available)),
                                           model->code_result_pending ? false : model->unrestricted_today == 1,
                                           model->code_result_pending ? model->code_preview_after_minutes : model->remaining_minutes));

    {
        UiRect status_strip = {dialog.x + 54, dialog.y + 268, dialog.width - 108, 60};
        uint32_t sbg = model->code_result_pending ? UI_WARNING_SOFT
            : (model->code_result_failed ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
        uint32_t sbd = model->code_result_pending ? UI_WARNING_BORDER
            : (model->code_result_failed ? UI_DANGER_BORDER : UI_SUCCESS_BORDER);
        uint32_t sfg = model->code_result_pending ? UI_WARNING_TEXT
            : (model->code_result_failed ? UI_DANGER : UI_SUCCESS);
        fill_round_rect(pixels, stride, status_strip, 12, sbg);
        draw_rect_outline(pixels, stride, status_strip, 12, 1, sbd);
        const char *msg = model->code_result_pending
            ? ptc_ui_text(PTC_UI_T_CAN_BE_CLOSED_DURING_RESULT_CONFIRMATION_IT)
            : (model->code_result_failed
                ? ptc_ui_text(PTC_UI_T_NO_CODES_WERE_CONSUMED_THIS_TIME_USED)
                : (model->code_actual_add_available && model->code_actual_add_minutes < model->code_grant_minutes
                    ? ptc_ui_text(PTC_UI_T_HAS_REACHED_THE_DAILY_LIMIT_AND_THE)
                    : (model->code_actual_add_available
                        ? ptc_ui_text(PTC_UI_T_THE_EXCHANGE_RESULT_HAS_BEEN_CONFIRMED_AND)
                        : ptc_ui_text(PTC_UI_T_SUCCESS_HAS_BEEN_CONFIRMED_GRANT_DETAILS_CANNOT))));
        fit_text(fitted, sizeof(fitted), msg, 16, status_strip.width - 24);
        draw_text_center(pixels, stride, status_strip, fitted, 16, sfg);
    }

    if (!model->code_result_pending) {
        char age[80];
        format_status_age(model, age, sizeof(age));
        draw_text_center(pixels, stride, (UiRect){dialog.x + 54, dialog.y + 344, dialog.width - 108, 24}, age, 15, status_age_color(model));
    }

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
    char fitted[160];
    bool cooldown = model->auth_cooldown_seconds > 0;
    snprintf(shell_model.overlay_title, sizeof(shell_model.overlay_title), "%s",
             model->auth_error_title[0] ? model->auth_error_title : ptc_ui_text(PTC_UI_T_PIN_VERIFICATION_FAILED));
    snprintf(shell_model.overlay_body, sizeof(shell_model.overlay_body), "%s",
             model->auth_error_message[0] ? model->auth_error_message : ptc_ui_text(PTC_UI_T_PIN_IS_INCORRECT_PLEASE_TRY_AGAIN));
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 820, 400);

    /* Top-right status badge */
    {
        const char *btxt = ptc_ui_text(cooldown ? PTC_UI_T_PIN_COOLDOWN_BADGE : PTC_UI_T_PIN_ERROR_BADGE);
        int bw = measure_text(btxt, 13) + 24;
        if (bw > 220) bw = 220;
        UiRect bpill = {dialog.x + dialog.width - 34 - bw, dialog.y + 26, bw, 28};
        fill_round_rect(pixels, stride, bpill, 14, UI_DANGER_SOFT);
        draw_rect_outline(pixels, stride, bpill, 14, 1, UI_DANGER_BORDER);
        fit_text(fitted, sizeof(fitted), btxt, 13, bpill.width - 14);
        draw_text_center(pixels, stride, bpill, fitted, 13, UI_DANGER);
    }

    UiRect alert_box = {dialog.x + 36, dialog.y + 148, dialog.width - 72, 114};
    fill_round_rect(pixels, stride, alert_box, 14, UI_DANGER_SOFT);
    draw_rect_outline(pixels, stride, alert_box, 14, 1, UI_DANGER_BORDER);
    {
        int text_w = cooldown ? (alert_box.width - 156) : (alert_box.width - 36);
        fit_text(fitted, sizeof(fitted),
                 cooldown ? ptc_ui_text(PTC_UI_T_TOO_MANY_ERRORS_YOU_CAN_TRY_AGAIN)
                          : ptc_ui_text(PTC_UI_T_ERROR_PIN_WILL_NOT_BE_RETAINED_INPUT),
                 17, text_w);
        draw_text_bold(pixels, stride, alert_box.x + 18, alert_box.y + 38, fitted, 17, UI_DANGER);
        draw_wrapped_text(pixels, stride, alert_box.x + 18, alert_box.y + 66,
                          ptc_ui_text(model->auth_error_parent_entry
                              ? PTC_UI_T_PARENT_PIN_SUPPORT_READ_ONLY
                              : PTC_UI_T_PLEASE_GO_TO_SUPPORT_AND_RECOVERY_TO),
                          14, text_w, 20, 2, UI_MUTED);
        if (cooldown) {
            char sec_text[32];
            UiRect cd_box = {alert_box.x + alert_box.width - 124, alert_box.y + 20, 106, 74};
            snprintf(sec_text, sizeof(sec_text), ptc_ui_text(PTC_UI_T_PIN_COOLDOWN_SEC_FMT), model->auth_cooldown_seconds);
            fill_round_rect(pixels, stride, cd_box, 12, UI_SURFACE);
            draw_rect_outline(pixels, stride, cd_box, 12, 2, UI_DANGER);
            draw_text_center(pixels, stride, cd_box, sec_text, 28, UI_DANGER);
        }
    }

    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_PIN_SECURITY_RULE_LINE), 13, dialog.width - 72);
    draw_text_center(pixels, stride, (UiRect){dialog.x + 36, dialog.y + 276, dialog.width - 72, 24},
                     fitted, 13, UI_MUTED);

    if (cooldown) {
        snprintf(retry_label, sizeof(retry_label), ptc_ui_text(PTC_UI_T_WAIT_D_SECONDS), model->auth_cooldown_seconds);
    } else {
        snprintf(retry_label, sizeof(retry_label), "%s", ptc_ui_text(PTC_UI_T_A_RE_ENTER));
    }
    draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay), ptc_ui_text(model->auth_error_parent_entry ? PTC_UI_T_B_BACK : PTC_UI_T_B_CANCEL),
                       UI_RAISED, UI_INK, true);
    draw_dialog_button(pixels, stride, ptc_ui_auth_error_guide_rect(), ptc_ui_text(model->auth_error_parent_entry ? PTC_UI_T_X_SUPPORT_RECOVERY : PTC_UI_T_X_SUPPORT_GUIDE),
                       UI_WARNING_SOFT, UI_WARNING_TEXT, false);
    draw_rect_outline(pixels, stride, to_uirect(ptc_ui_auth_error_guide_rect()), 12, 1, UI_WARNING_BORDER);
    draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay), retry_label,
                       cooldown ? UI_BORDER : UI_ACCENT,
                       cooldown ? UI_MUTED : UI_ON_ACCENT, false);
}

static void draw_grant_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char path[128];
    int index;
    PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("name", ptc_ui_text(PTC_UI_T_GRANT_SETTINGS))};
    (void)ptc_ui_text_format(PTC_UI_T_OFFLINE_GRANTS_S, path, sizeof(path), args, 1);
    draw_subpage_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_GRANT_SETTINGS),
                       ptc_ui_text(PTC_UI_T_MANAGES_THE_DEVICE_INFORMATION_EXPORT_CONFIGURATION_AND),
                       ptc_ui_text(PTC_UI_T_B_BACK_TO_OFFLINE_GRANTS), path);

    for (index = 0; index < PTC_UI_GRANT_MANAGER_COUNT; ++index) {
        draw_action_card(
            pixels,
            stride,
            to_uirect(ptc_ui_grant_manager_card_rect(index)),
            &GRANT_MANAGER_ACTIONS[index],
            model->overlay_selection == index,
            PTC_UI_ACTION_AVAILABLE,
            0, (UiRect){0});
    }
}

static void draw_history_stat_card(
    uint32_t *pixels, uint32_t stride, UiRect rect,
    const char *label, const char *value, uint32_t accent)
{
    char fitted[128];
    draw_plan_card(pixels, stride, rect, false);
    fill_round_rect(pixels, stride, (UiRect){rect.x + 12, rect.y + 14, 4, rect.height - 28}, 2, accent);
    fit_text(fitted, sizeof(fitted), label, 12, rect.width - 36);
    draw_text(pixels, stride, rect.x + 24, rect.y + 23, fitted, 12, UI_MUTED);
    fit_text(fitted, sizeof(fitted), value, 17, rect.width - 36);
    draw_text_bold(pixels, stride, rect.x + 24, rect.y + 49, fitted, 17, accent);
}

static void draw_redemption_history_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char path[128];
    char buf[128];
    char fitted[160];
    int pages = ptc_ui_redemption_history_page_count(model);
    int first = model->redemption_history_page * 6;
    int visible = model->redemption_history_count - first;
    int row;
    unsigned today_count = 0, today_added = 0;
    char latest_time[48] = "--";
    if (visible > 6) visible = 6;
    if (visible < 0) visible = 0;

    for (int i = 0; i < model->redemption_history_count; ++i) {
        const PtcRedemptionHistoryRecord *r = &model->redemption_history[i];
        if (r->day_index == model->day_index) {
            ++today_count;
            today_added += r->effective_add_minutes;
        }
    }
    if (model->redemption_history_count > 0) {
        format_event_time(model->redemption_history[model->redemption_history_count - 1].redeemed_at,
                          true, latest_time, sizeof(latest_time));
    }

    PtcUiTextArg pargs[] = {PTC_UI_TEXT_STRING("name", ptc_ui_text(PTC_UI_T_EXTRA_TIME_CODE_USAGE_RECORD))};
    (void)ptc_ui_text_format(PTC_UI_T_OFFLINE_GRANTS_S, path, sizeof(path), pargs, 1);
    draw_subpage_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_EXTRA_TIME_CODE_USAGE_RECORD),
                       ptc_ui_text(PTC_UI_T_ONLY_DISPLAYS_THE_LAST_100_SUCCESSFULLY_REDEEMED),
                       ptc_ui_text(PTC_UI_T_B_BACK_TO_OFFLINE_GRANTS), path);

    /* Top 3 Summary Cards */
    {
        int sw = (1172 - 32) / 3;
        snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_HISTORY_COUNT_FMT),
                 model->redemption_history_count, PTC_REDEMPTION_HISTORY_MAX_RECORDS);
        draw_history_stat_card(pixels, stride, (UiRect){54, 172, sw, 64},
                               ptc_ui_text(PTC_UI_T_HISTORY_STAT_TOTAL), buf, UI_ACCENT);
        snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_HISTORY_TODAY_FMT), today_count, today_added);
        draw_history_stat_card(pixels, stride, (UiRect){54 + sw + 16, 172, sw, 64},
                               ptc_ui_text(PTC_UI_T_HISTORY_STAT_TODAY_REDEEM), buf, UI_SUCCESS);
        draw_history_stat_card(pixels, stride, (UiRect){54 + (sw + 16) * 2, 172, sw, 64},
                               ptc_ui_text(PTC_UI_T_HISTORY_STAT_LATEST), latest_time, UI_INK);
    }

    /* Page indicator strip */
    snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_NEWEST_FIRST_PAGE_D_D_D_ENTRIES),
             model->redemption_history_page + 1, pages, model->redemption_history_count);
    fit_text(fitted, sizeof(fitted), buf, 13, 1172);
    draw_text(pixels, stride, 54, 256, fitted, 13, UI_MUTED);

    if (!model->redemption_history_available) {
        draw_text_center(pixels, stride, (UiRect){54, 340, 1172, 44},
                         ptc_ui_text(PTC_UI_T_THE_USAGE_RECORD_CANNOT_BE_READ_TEMPORARILY),
                         19, UI_DANGER);
    } else if (model->redemption_history_count == 0) {
        draw_text_center(pixels, stride, (UiRect){54, 340, 1172, 44},
                         ptc_ui_text(PTC_UI_T_THERE_IS_NO_RECORD_OF_SUCCESSFUL_USE),
                         19, UI_MUTED);
    } else {
        for (row = 0; row < visible; ++row) {
            int source = model->redemption_history_count - 1 - first - row;
            const PtcRedemptionHistoryRecord *record = &model->redemption_history[source];
            UiRect item = {54, 270 + row * 60, 1172, 52};
            char time_text[48];
            char allowance[160];
            char remaining[96];
            const char *ver_txt = record->token_version == 2u
                ? ptc_ui_text(PTC_UI_T_V2_SUCCESSFUL) : ptc_ui_text(PTC_UI_T_V1_SUCCESSFUL);
            format_event_time(record->redeemed_at, true, time_text, sizeof(time_text));
            if (record->remaining_after_available) {
                char value[48];
                format_duration((int)record->remaining_after_minutes, value, sizeof(value));
                snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_AFTER_REDEMPTION_S), value);
            } else {
                snprintf(remaining, sizeof(remaining), "%s", ptc_ui_text(PTC_UI_T_TEMPORARILY_UNAVAILABLE_AFTER_REDEMPTION));
            }
            snprintf(allowance, sizeof(allowance), ptc_ui_text(PTC_UI_T_CODE_U_MIN_CREDITED_U_MIN_S),
                     (unsigned int)record->grant_minutes,
                     (unsigned int)record->effective_add_minutes,
                     record->effective_add_minutes < record->grant_minutes ? ptc_ui_text(PTC_UI_T_DAILY_LIMIT_REACHED) : "");

            fill_round_rect(pixels, stride, item, 12, UI_SURFACE);
            draw_rect_outline(pixels, stride, item, 12, 1, UI_BORDER);

            /* Left side: green dot + version badge + timestamp (all dynamically measured) */
            fill_round_rect(pixels, stride, (UiRect){item.x + 14, item.y + 21, 10, 10}, 5, UI_SUCCESS);
            int vw = measure_text(ver_txt, 12) + 18;
            if (vw > 120) vw = 120;
            UiRect vpill = {item.x + 34, item.y + 13, vw, 26};
            fill_round_rect(pixels, stride, vpill, 8, UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, vpill, 8, 1, UI_SUCCESS_BORDER);
            fit_text(fitted, sizeof(fitted), ver_txt, 12, vpill.width - 10);
            draw_text_center(pixels, stride, vpill, fitted, 12, UI_SUCCESS);

            int tx = vpill.x + vpill.width + 14;
            int tw = measure_text(time_text, 14);
            draw_text(pixels, stride, tx, item.y + 31, time_text, 14, UI_MUTED);

            /* Right side: measured edge-to-edge right-aligned remaining + allowance */
            fit_text(fitted, sizeof(fitted), remaining, 14, 320);
            int rw = measure_text(fitted, 14);
            int rx = item.x + item.width - 16 - rw;
            draw_text(pixels, stride, rx, item.y + 31, fitted, 14,
                      record->remaining_after_available ? UI_INK : UI_MUTED);

            int ax = tx + tw + 20;
            int a_max = rx - ax - 18;
            if (a_max > 60) {
                fit_text(fitted, sizeof(fitted), allowance, 15, a_max);
                draw_text_bold(pixels, stride, ax, item.y + 31, fitted, 15, UI_ACCENT);
            }
        }
    }

    home_button(pixels, stride, ptc_ui_redemption_history_prev_rect(),
                ptc_ui_text(PTC_UI_T_L_LEFT_PREVIOUS_PAGE), false, false, model->redemption_history_page <= 0);
    home_button(pixels, stride, ptc_ui_redemption_history_next_rect(),
                ptc_ui_text(PTC_UI_T_R_RIGHT_NEXT_PAGE), false, false, model->redemption_history_page + 1 >= pages);
    home_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                ptc_ui_text(PTC_UI_T_X_CLEAR_ALL), false, false, model->redemption_history_count == 0);
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
    static const PtcUiTextId FILTER_LABELS[4] = {
        PTC_UI_T_HISTORY_FILTER_ALL,
        PTC_UI_T_HISTORY_FILTER_GRANT,
        PTC_UI_T_HISTORY_FILTER_RULES,
        PTC_UI_T_HISTORY_FILTER_PROTECT
    };
    int filter = (model->activity_history_filter >= 0 && model->activity_history_filter < 4)
        ? model->activity_history_filter : 0;
    int filtered_total = ptc_ui_activity_filtered_count(model);
    int pages = ptc_ui_activity_history_page_count(model);
    int page = model->activity_history_page;
    if (page >= pages) page = pages - 1;
    if (page < 0) page = 0;
    int first = page * 8;
    char line[192];
    char fitted[192];

    unsigned grant_count = 0, grant_mins = 0, rule_count = 0, protect_count = 0;
    for (int i = 0; i < model->activity_history_count; ++i) {
        const PtcActivityHistoryRecord *r = &model->activity_history[i];
        if (ptc_ui_activity_matches_filter(r, 1)) {
            ++grant_count;
            grant_mins += r->effective_minutes;
        } else if (ptc_ui_activity_matches_filter(r, 3)) {
            ++protect_count;
        } else {
            ++rule_count;
        }
    }

    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_FAMILY_ACTIVITY_RECORD),
                             ptc_ui_text(PTC_UI_T_RETAINS_UP_TO_200_RULES_GRANT_AUTONOMOUS));

    /* Top 3 Summary Cards */
    {
        int sw = (1172 - 32) / 3;
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_HISTORY_COUNT_FMT),
                 model->activity_history_count, PTC_ACTIVITY_HISTORY_MAX_RECORDS);
        draw_history_stat_card(pixels, stride, (UiRect){54, 172, sw, 64},
                               ptc_ui_text(PTC_UI_T_HISTORY_STAT_TOTAL), line, UI_ACCENT);
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_HISTORY_TODAY_FMT), grant_count, grant_mins);
        draw_history_stat_card(pixels, stride, (UiRect){54 + sw + 16, 172, sw, 64},
                               ptc_ui_text(PTC_UI_T_HISTORY_STAT_TODAY_GRANT), line, UI_SUCCESS);
        snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_HISTORY_RULES_FMT), rule_count, protect_count);
        draw_history_stat_card(pixels, stride, (UiRect){54 + (sw + 16) * 2, 172, sw, 64},
                               ptc_ui_text(PTC_UI_T_HISTORY_STAT_RULES_PROTECT), line,
                               protect_count > 0 ? UI_DANGER : UI_INK);
    }

    /* Category Filter Pills + Right-aligned Page & Filter Hint */
    for (int f = 0; f < 4; ++f) {
        UiRect fr = to_uirect(ptc_ui_activity_filter_rect(f));
        bool active = (f == filter);
        fill_round_rect(pixels, stride, fr, 17, active ? UI_ACCENT : UI_SURFACE);
        draw_rect_outline(pixels, stride, fr, 17, 1, active ? UI_ACCENT : UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(FILTER_LABELS[f]), 13, fr.width - 14);
        draw_text_center(pixels, stride, fr, fitted, 13, active ? UI_ON_ACCENT : UI_INK);
    }
    snprintf(line, sizeof(line), "%s  |  ", ptc_ui_text(PTC_UI_T_HISTORY_FILTER_HINT));
    size_t pref_len = strlen(line);
    snprintf(line + pref_len, sizeof(line) - pref_len,
             ptc_ui_text(PTC_UI_T_NEWEST_FIRST_PAGE_D_D_D_ENTRIES_2),
             page + 1, pages, filtered_total);
    fit_text(fitted, sizeof(fitted), line, 13, 574);
    draw_text(pixels, stride, 1218 - measure_text(fitted, 13), 268, fitted, 13, UI_MUTED);

    if (!model->activity_history_available) {
        draw_text_center(pixels, stride, (UiRect){54, 360, 1172, 44},
            ptc_ui_text(PTC_UI_T_FAMILY_ACTIVITY_LOGGING_IS_TEMPORARILY_UNAVAILABLE_CONTROL), 19, UI_DANGER);
    } else if (model->activity_history_count == 0) {
        draw_text_center(pixels, stride, (UiRect){54, 360, 1172, 44},
            ptc_ui_text(PTC_UI_T_THERE_IS_NO_RECORD_OF_FAMILY_ACTIVITIES), 19, UI_MUTED);
    } else if (filtered_total == 0) {
        draw_text_center(pixels, stride, (UiRect){54, 360, 1172, 44},
            ptc_ui_text(PTC_UI_T_HISTORY_FILTER_EMPTY), 18, UI_MUTED);
    } else {
        int matched = 0;
        int drawn = 0;
        for (int i = model->activity_history_count - 1; i >= 0 && drawn < 8; --i) {
            const PtcActivityHistoryRecord *record = &model->activity_history[i];
            if (!ptc_ui_activity_matches_filter(record, filter)) continue;
            if (matched++ < first) continue;

            UiRect item = {54, 292 + drawn * 44, 1172, 40};
            char time_text[48];
            char mins_text[80];
            bool is_protect = (strcmp(record->action, "protection") == 0);
            bool is_buffer = (strcmp(record->action, "daily_buffer") == 0);
            bool is_grant = (strcmp(record->action, "offline_grant") == 0 || strcmp(record->action, "today_add") == 0);
            const char *badge_txt = ptc_ui_text(is_protect ? PTC_UI_T_HISTORY_BADGE_PROTECT
                : (is_buffer ? PTC_UI_T_HISTORY_BADGE_BUFFER
                   : (is_grant ? PTC_UI_T_HISTORY_BADGE_GRANT : PTC_UI_T_HISTORY_BADGE_RULE)));
            uint32_t b_bg = is_protect ? UI_DANGER_SOFT : ((is_grant || is_buffer) ? UI_SUCCESS_SOFT : UI_ACCENT_SOFT);
            uint32_t b_bd = is_protect ? UI_DANGER_BORDER : ((is_grant || is_buffer) ? UI_SUCCESS_BORDER : UI_ACCENT);
            uint32_t b_fg = is_protect ? UI_DANGER : ((is_grant || is_buffer) ? UI_SUCCESS : UI_ACCENT);

            fill_round_rect(pixels, stride, item, 10, UI_SURFACE);
            draw_rect_outline(pixels, stride, item, 10, 1, UI_BORDER);

            int bw = measure_text(badge_txt, 12) + 16;
            if (bw > 92) bw = 92;
            UiRect bpill = {item.x + 12, item.y + 8, bw, 24};
            fill_round_rect(pixels, stride, bpill, 7, b_bg);
            draw_rect_outline(pixels, stride, bpill, 7, 1, b_bd);
            fit_text(fitted, sizeof(fitted), badge_txt, 12, bpill.width - 8);
            draw_text_center(pixels, stride, bpill, fitted, 12, b_fg);

            /* Edge-to-edge dynamic right alignment: timestamp on far right, minutes pill left of it */
            format_event_time(record->occurred_at, true, time_text, sizeof(time_text));
            int tw = measure_text(time_text, 13);
            int tx = item.x + item.width - 14 - tw;
            draw_text(pixels, stride, tx, item.y + 25, time_text, 13, UI_MUTED);

            snprintf(mins_text, sizeof(mins_text), ptc_ui_text(PTC_UI_T_HISTORY_MINUTES_FMT),
                     (unsigned int)record->minutes, (unsigned int)record->effective_minutes);
            fit_text(fitted, sizeof(fitted), mins_text, 13, 280);
            int mw = measure_text(fitted, 13);
            int mx = tx - 20 - mw;
            draw_text(pixels, stride, mx, item.y + 25, fitted, 13, is_protect ? UI_DANGER : UI_ACCENT);

            int lx = bpill.x + bpill.width + 14;
            int lmax = mx - lx - 16;
            if (lmax > 60) {
                fit_text(fitted, sizeof(fitted), activity_label(record->action), 15, lmax);
                draw_text_bold(pixels, stride, lx, item.y + 26, fitted, 15, is_protect ? UI_DANGER : UI_INK);
            }
            ++drawn;
        }
    }

    home_button(pixels, stride, ptc_ui_settings_page_footer_rect(1),
                ptc_ui_text(PTC_UI_T_L_LEFT_PREVIOUS_PAGE), false, false, page <= 0);
    home_button(pixels, stride, ptc_ui_settings_page_footer_rect(2),
                ptc_ui_text(PTC_UI_T_R_RIGHT_NEXT_PAGE), false, false, page + 1 >= pages);
    home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                ptc_ui_text(PTC_UI_T_X_CLEAR_ALL), false, false, model->activity_history_count == 0);
}

/* Pure CPU vector illustrations for the 3-in-1 Grant & Pairing mode selector (A2). */
static void draw_grant_mode_illustration(uint32_t *pixels, uint32_t stride, UiRect box, int mode, bool active)
{
    uint32_t frame = active ? UI_ACCENT : UI_CONTROL;
    uint32_t tint = active ? UI_ACCENT_SOFT : UI_PAGE;
    uint32_t ink = active ? UI_ACCENT : UI_MUTED;
    fill_round_rect(pixels, stride, box, 12, tint);
    draw_rect_outline(pixels, stride, box, 12, 1, active ? UI_ACCENT : UI_BORDER);
    if (mode == 0) {
        /* Switch QR -> Phone scan illustration */
        UiRect sw = {box.x + 10, box.y + 22, 34, 24};
        UiRect ph = {box.x + 56, box.y + 12, 22, 40};
        fill_round_rect(pixels, stride, sw, 4, UI_SURFACE);
        draw_rect_outline(pixels, stride, sw, 4, 2, frame);
        fill_rect(pixels, stride, (UiRect){sw.x + 11, sw.y + 6, 12, 12}, ink);
        draw_transition_arrow(pixels, stride, box.x + 50, box.y + 32, ink);
        fill_round_rect(pixels, stride, ph, 5, UI_SURFACE);
        draw_rect_outline(pixels, stride, ph, 5, 2, frame);
        fill_rect(pixels, stride, (UiRect){ph.x + 5, ph.y + 8, 12, 16}, tint);
    } else if (mode == 1) {
        /* SD Card / JSON -> Laptop offline file illustration */
        UiRect sd = {box.x + 12, box.y + 18, 22, 28};
        UiRect pc = {box.x + 48, box.y + 16, 32, 24};
        fill_round_rect(pixels, stride, sd, 4, UI_SURFACE);
        draw_rect_outline(pixels, stride, sd, 4, 2, frame);
        draw_line(pixels, stride, sd.x + 5, sd.y + 10, sd.x + 16, sd.y + 10, 2, ink);
        draw_line(pixels, stride, sd.x + 5, sd.y + 16, sd.x + 16, sd.y + 16, 2, ink);
        fill_round_rect(pixels, stride, pc, 4, UI_SURFACE);
        draw_rect_outline(pixels, stride, pc, 4, 2, frame);
        fill_round_rect(pixels, stride, (UiRect){pc.x - 4, pc.y + pc.height + 2, pc.width + 8, 5}, 2, frame);
    } else {
        /* Switch handheld console + 8-digit voucher card */
        UiRect sw = {box.x + 12, box.y + 16, 66, 34};
        fill_round_rect(pixels, stride, (UiRect){sw.x, sw.y, 12, sw.height}, 6, UI_ACCENT);
        fill_round_rect(pixels, stride, (UiRect){sw.x + sw.width - 12, sw.y, 12, sw.height}, 6, UI_CORAL);
        fill_round_rect(pixels, stride, (UiRect){sw.x + 12, sw.y, sw.width - 24, sw.height}, 3, UI_SURFACE);
        draw_rect_outline(pixels, stride, (UiRect){sw.x + 12, sw.y, sw.width - 24, sw.height}, 3, 2, frame);
        draw_line(pixels, stride, sw.x + 20, sw.y + 14, sw.x + 46, sw.y + 14, 2, ink);
        draw_line(pixels, stride, sw.x + 20, sw.y + 22, sw.x + 38, sw.y + 22, 2, ink);
    }
}

static void draw_grant_hub_step_badge(uint32_t *pixels, uint32_t stride, UiRect badge, int number)
{
    char label[8];
    fill_round_rect(pixels, stride, badge, badge.width / 2, UI_ACCENT);
    snprintf(label, sizeof(label), "%d", number);
    draw_text_center(pixels, stride, badge, label, 14, UI_ON_ACCENT);
}

static void draw_grant_hub_page(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const PtcUiTextId MODE_TITLES[3] = {
        PTC_UI_T_GRANT_HUB_MODE_PHONE,
        PTC_UI_T_GRANT_HUB_MODE_PC,
        PTC_UI_T_GRANT_HUB_MODE_LOCAL
    };
    static const PtcUiTextId MODE_TAGS[3] = {
        PTC_UI_T_GRANT_HUB_MODE_PHONE_TAG,
        PTC_UI_T_GRANT_HUB_MODE_PC_TAG,
        PTC_UI_T_GRANT_HUB_MODE_LOCAL_TAG
    };
    static const PtcUiTextId MODE_DESCS[3] = {
        PTC_UI_T_GRANT_HUB_MODE_PHONE_DESC,
        PTC_UI_T_GRANT_HUB_MODE_PC_DESC,
        PTC_UI_T_GRANT_HUB_MODE_LOCAL_DESC
    };
    int active_mode = ptc_ui_grant_hub_active_mode(model);
    int64_t now = ptc_ui_render_now();
    char path[128], fitted[192], buf[192];
    PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("name", ptc_ui_text(PTC_UI_T_GRANT_HUB_TITLE))};
    (void)ptc_ui_text_format(PTC_UI_T_OFFLINE_GRANTS_S, path, sizeof(path), args, 1);
    draw_subpage_shell(pixels, stride, model,
                       ptc_ui_text(PTC_UI_T_GRANT_HUB_TITLE),
                       ptc_ui_text(PTC_UI_T_GRANT_HUB_SUBTITLE),
                       ptc_ui_text(PTC_UI_T_B_BACK_TO_OFFLINE_GRANTS), path);
    draw_refresh_footer_button(pixels, stride, ptc_ui_scheduled_refresh_rect(),
        ptc_ui_text(PTC_UI_T_Y_REFRESH), ptc_ui_status_needs_refresh(model, now),
        ptc_ui_status_refresh_failed(model), false, model->waiting);

    /* Footer hint right of refresh */
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_GRANT_HUB_SWITCH_HINT), 13, 460);
    draw_text(pixels, stride, 456, 692, fitted, 13, UI_MUTED);

    /* ================= Left Column: 3 Unified Radio Mode Cards ================= */
    for (int m = 0; m < 3; ++m) {
        UiRect card = to_uirect(ptc_ui_grant_hub_mode_rect(m));
        bool selected = (m == active_mode);
        draw_plan_card(pixels, stride, card, selected);

        /* Radio indicator circle */
        int rx = card.x + 24;
        int ry = card.y + 30;
        draw_circle_outline(pixels, stride, rx, ry, 10, 2, selected ? UI_ACCENT : UI_CONTROL);
        if (selected) {
            fill_round_rect(pixels, stride, (UiRect){rx - 5, ry - 5, 10, 10}, 5, UI_ACCENT);
        }

        /* Mode title + tag pill + vector illustration on right */
        UiRect illus = {card.x + card.width - 102, card.y + 42, 88, 66};
        draw_grant_mode_illustration(pixels, stride, illus, m, selected);

        int text_w = illus.x - (card.x + 44) - 12;
        fit_text(fitted, sizeof(fitted), ptc_ui_text(MODE_TITLES[m]), 18, text_w);
        draw_text_bold(pixels, stride, card.x + 44, card.y + 36, fitted, 18, selected ? UI_ACCENT : UI_INK);

        const char *tag = ptc_ui_text(MODE_TAGS[m]);
        int tw = measure_text(tag, 11) + 18;
        if (tw > text_w) tw = text_w;
        UiRect tag_pill = {card.x + 44, card.y + 46, tw, 22};
        fill_round_rect(pixels, stride, tag_pill, 11, selected ? UI_ACCENT : UI_RAISED);
        fit_text(fitted, sizeof(fitted), tag, 11, tag_pill.width - 10);
        draw_text_center(pixels, stride, tag_pill, fitted, 11, selected ? UI_ON_ACCENT : UI_MUTED);

        draw_wrapped_text(pixels, stride, card.x + 44, card.y + 88,
                          ptc_ui_text(MODE_DESCS[m]), 13, text_w, 19, 3, UI_MUTED);
    }

    /* ================= Right Column: Focused Active Mode Workbench ================= */
    UiRect right = to_uirect(ptc_ui_qr_panel_rect(1));
    draw_plan_card(pixels, stride, right, false);

    if (active_mode == 0) {
        /* Mode 0: Phone Pairing (Online QR Scan) */
        UiRect canvas = to_uirect(ptc_ui_qr_code_rect());
        UiRect address = to_uirect(ptc_ui_qr_address_rect());
        UiRect warning = to_uirect(ptc_ui_qr_warning_rect());
        int qr_size = qrcodegen_getSize(model->qr_code);

        const char *badge_txt = model->demo_secret_enabled
            ? ptc_ui_text(PTC_UI_T_GRANT_HUB_DEMO_BADGE)
            : ptc_ui_text(PTC_UI_T_GRANT_HUB_MODE_PHONE_TAG);
        int bw = measure_text(badge_txt, 12) + 22;
        if (bw > 200) bw = 200;
        UiRect bpill = {right.x + right.width - 24 - bw, right.y + 18, bw, 26};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_QR_TITLE_ONLINE), 21,
                 bpill.x - (right.x + 24) - 12);
        draw_text_bold(pixels, stride, right.x + 24, right.y + 38, fitted, 21, UI_INK);
        fill_round_rect(pixels, stride, bpill, 13, model->demo_secret_enabled ? UI_DANGER_SOFT : UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, bpill, 13, 1, model->demo_secret_enabled ? UI_DANGER_BORDER : UI_ACCENT);
        fit_text(fitted, sizeof(fitted), badge_txt, 12, bpill.width - 12);
        draw_text_center(pixels, stride, bpill, fitted, 12, model->demo_secret_enabled ? UI_DANGER : UI_ACCENT);

        /* QR polarity MUST stay pure #000000 on #FFFFFF in both light and dark themes. */
        fill_rect_packed(pixels, stride, canvas, pack_rgb(0xFFFFFF));
        draw_rect_outline(pixels, stride, canvas, 4, 1, UI_BORDER);
        if (qr_size > 0) {
            int scale = canvas.width / (qr_size + 8);
            if (scale < 2) scale = 2;
            int total = (qr_size + 8) * scale;
            int origin_x = canvas.x + (canvas.width - total) / 2;
            int origin_y = canvas.y + (canvas.height - total) / 2;
            for (int y = 0; y < qr_size; ++y) {
                for (int x = 0; x < qr_size; ++x) {
                    if (qrcodegen_getModule(model->qr_code, x, y)) {
                        fill_rect_packed(pixels, stride,
                                         (UiRect){origin_x + (x + 4) * scale, origin_y + (y + 4) * scale, scale, scale},
                                         pack_rgb(0x000000));
                    }
                }
            }
        } else {
            draw_wrapped_text_centered(pixels, stride, canvas.x + canvas.width / 2, canvas.y + canvas.height / 2 - 10,
                                       ptc_ui_text(PTC_UI_T_GRANT_HUB_QR_LOCKED_HINT), 14,
                                       canvas.width - 32, 20, 3, UI_MUTED);
        }

        /* Right-side numbered steps beside QR */
        {
            static const PtcUiTextId P_STEPS[3] = {
                PTC_UI_T_GRANT_HUB_PHONE_STEP_1,
                PTC_UI_T_GRANT_HUB_PHONE_STEP_2,
                PTC_UI_T_GRANT_HUB_PHONE_STEP_3
            };
            int sx = address.x;
            int sy = canvas.y + 18;
            for (int s = 0; s < 3; ++s) {
                sy = draw_wrapped_text(pixels, stride, sx, sy, ptc_ui_text(P_STEPS[s]),
                                       14, address.width, 19, 2, UI_INK);
                sy += 6;
            }
        }

        fill_round_rect(pixels, stride, address, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, address, 10, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SCAN_THE_CODE_ONLY_WHEN_THE_WEB), 12, address.width - 24);
        draw_text(pixels, stride, address.x + 12, address.y + 20, fitted, 12, UI_MUTED);
        draw_wrapped_text(pixels, stride, address.x + 12, address.y + 40,
                          model->pairing_base_url, 13, address.width - 24, 16, 2, UI_ACCENT);

        fill_round_rect(pixels, stride, warning, 10, UI_DANGER_SOFT);
        draw_rect_outline(pixels, stride, warning, 10, 1, UI_DANGER_BORDER);
        draw_wrapped_text(pixels, stride, warning.x + 12, warning.y + 22,
                          ptc_ui_text(PTC_UI_T_THE_QR_CODE_CONTAINS_A_TIME_CODE), 12,
                          warning.width - 24, 16, 2, UI_DANGER);

        draw_line(pixels, stride, right.x + 24, right.y + 392, right.x + right.width - 24, right.y + 392, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NO_NETWORK_REQUIRED_FOR_DAILY_GENERATION_NO), 13, 444);
        draw_text(pixels, stride, right.x + 24, right.y + 434, fitted, 13, UI_SUCCESS);
        draw_dialog_button(pixels, stride, ptc_ui_qr_export_rect(),
                           ptc_ui_text(PTC_UI_T_A_EXPORT_CONFIGURATION_FILE), UI_ACCENT, UI_ON_ACCENT, false);
    } else if (active_mode == 1) {
        /* Mode 1: PC / Phone Offline Single-File Flow */
        const char *tag = ptc_ui_text(PTC_UI_T_GRANT_HUB_MODE_PC_TAG);
        int bw = measure_text(tag, 12) + 22;
        if (bw > 180) bw = 180;
        UiRect bpill = {right.x + right.width - 24 - bw, right.y + 18, bw, 26};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_QR_TITLE_OFFLINE), 21,
                 bpill.x - (right.x + 24) - 12);
        draw_text_bold(pixels, stride, right.x + 24, right.y + 38, fitted, 21, UI_INK);
        fill_round_rect(pixels, stride, bpill, 13, UI_SUCCESS_SOFT);
        draw_rect_outline(pixels, stride, bpill, 13, 1, UI_SUCCESS_BORDER);
        fit_text(fitted, sizeof(fitted), tag, 12, bpill.width - 12);
        draw_text_center(pixels, stride, bpill, fitted, 12, UI_SUCCESS);

        /* Step 1 Card with embedded SD path chip */
        UiRect s1 = {right.x + 24, right.y + 62, right.width - 48, 86};
        fill_round_rect(pixels, stride, s1, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, s1, 12, 1, UI_BORDER);
        draw_grant_hub_step_badge(pixels, stride, (UiRect){s1.x + 14, s1.y + 14, 26, 26}, 1);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_GRANT_HUB_OFFLINE_STEP1_TITLE), 16, s1.width - 62);
        draw_text_bold(pixels, stride, s1.x + 50, s1.y + 32, fitted, 16, UI_INK);
        UiRect path_chip = {s1.x + 50, s1.y + 46, s1.width - 66, 28};
        fill_round_rect(pixels, stride, path_chip, 8, UI_PAGE);
        draw_rect_outline(pixels, stride, path_chip, 8, 1, UI_BORDER);
        draw_text(pixels, stride, path_chip.x + 12, path_chip.y + 19, PLAYWISE_SD_ROOT "/parent-import.json", 13, UI_ACCENT);

        /* Step 2 Card */
        UiRect s2 = {right.x + 24, right.y + 158, right.width - 48, 76};
        fill_round_rect(pixels, stride, s2, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, s2, 12, 1, UI_BORDER);
        draw_grant_hub_step_badge(pixels, stride, (UiRect){s2.x + 14, s2.y + 14, 26, 26}, 2);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_GRANT_HUB_OFFLINE_STEP2_TITLE), 16, s2.width - 62);
        draw_text_bold(pixels, stride, s2.x + 50, s2.y + 32, fitted, 16, UI_INK);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_GRANT_HUB_OFFLINE_STEP2_DESC), 13, s2.width - 62);
        draw_text(pixels, stride, s2.x + 50, s2.y + 58, fitted, 13, UI_MUTED);

        /* Step 3 Card */
        UiRect s3 = {right.x + 24, right.y + 244, right.width - 48, 76};
        fill_round_rect(pixels, stride, s3, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, s3, 12, 1, UI_BORDER);
        draw_grant_hub_step_badge(pixels, stride, (UiRect){s3.x + 14, s3.y + 14, 26, 26}, 3);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_GRANT_HUB_OFFLINE_STEP3_TITLE), 16, s3.width - 62);
        draw_text_bold(pixels, stride, s3.x + 50, s3.y + 32, fitted, 16, UI_INK);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_GRANT_HUB_OFFLINE_STEP3_DESC), 13, s3.width - 62);
        draw_text(pixels, stride, s3.x + 50, s3.y + 58, fitted, 13, UI_SUCCESS);

        /* Security Warning Strip */
        UiRect warn = {right.x + 24, right.y + 332, right.width - 48, 52};
        fill_round_rect(pixels, stride, warn, 10, UI_DANGER_SOFT);
        draw_rect_outline(pixels, stride, warn, 10, 1, UI_DANGER_BORDER);
        draw_wrapped_text(pixels, stride, warn.x + 14, warn.y + 22,
                          ptc_ui_text(PTC_UI_T_THE_CONFIGURATION_FILE_CONTAINS_A_TIME_CODED), 13,
                          warn.width - 28, 17, 2, UI_DANGER);

        draw_line(pixels, stride, right.x + 24, right.y + 394, right.x + right.width - 24, right.y + 394, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_PLEASE_SAVE_THE_FILE_ON_YOUR_MOBILE), 13, 444);
        draw_text(pixels, stride, right.x + 24, right.y + 434, fitted, 13, UI_MUTED);
        draw_dialog_button(pixels, stride, ptc_ui_qr_export_rect(),
                           ptc_ui_text(PTC_UI_T_A_EXPORT_CONFIGURATION_FILE), UI_ACCENT, UI_ON_ACCENT, false);
    } else {
        /* Mode 2: On-Console 8-Digit Grant Code Generation */
        char remaining[48], estimate[48], code[16], freshness[80], played[48];
        bool capped = false;
        bool reliable = ptc_ui_status_is_fresh(model, now) && !model->grant_status_refresh_failed;
        int expected = reliable ? ptc_ui_grant_estimate_remaining(model, model->grant_minutes, &capped) : -1;
        uint16_t year; uint8_t month, day;

        if (reliable && model->unrestricted_today == 1) snprintf(remaining, sizeof(remaining), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else format_duration(reliable && model->remaining_available ? model->remaining_minutes : -1, remaining, sizeof(remaining));
        format_duration(expected, estimate, sizeof(estimate));
        format_duration(reliable && model->played_minutes_available ? model->played_minutes : -1, played, sizeof(played));
        format_status_age(model, freshness, sizeof(freshness));
        if (model->grant_status_refresh_failed) snprintf(freshness, sizeof(freshness), "%s", ptc_ui_text(PTC_UI_T_REFRESH_FAILED_REFRESH_AND_TRY_AGAIN_AFTER));

        /* Top header + live today balance pill */
        snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_S_LEFT_TODAY_S), remaining, freshness);
        int bw = measure_text(buf, 12) + 22;
        if (bw > 340) bw = 340;
        UiRect status_pill = {right.x + right.width - 24 - bw, right.y + 18, bw, 26};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NATIVELY_GENERATES_8_BIT_PLUS_TIME_CODE), 20,
                 status_pill.x - (right.x + 24) - 12);
        draw_text_bold(pixels, stride, right.x + 24, right.y + 38, fitted, 20, UI_INK);
        fill_round_rect(pixels, stride, status_pill, 13,
                        model->grant_status_refresh_failed ? UI_DANGER_SOFT : (reliable ? UI_ACCENT_SOFT : UI_WARNING_SOFT));
        fit_text(fitted, sizeof(fitted), buf, 12, status_pill.width - 12);
        draw_text_center(pixels, stride, status_pill, fitted, 12,
                         model->grant_status_refresh_failed ? UI_DANGER : (reliable ? UI_ACCENT : UI_WARNING_TEXT));

        /* Upper Config Panel: 5 Quick Presets + Custom Duration + Live Estimate */
        UiRect cfg_box = {right.x + 24, right.y + 62, right.width - 48, 150};
        fill_round_rect(pixels, stride, cfg_box, 14, UI_RAISED);
        draw_rect_outline(pixels, stride, cfg_box, 14, 1, UI_BORDER);

        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THE_NEXT_GRANT_DURATION), 14, 382);
        draw_text_bold(pixels, stride, cfg_box.x + 14, cfg_box.y + 24, fitted, 14, UI_INK);

        for (int p = 0; p < 5; ++p) {
            uint16_t preset = ptc_ui_grant_preset_minutes(p);
            UiRect chip = to_uirect(ptc_ui_grant_preset_rect(p));
            bool active = (model->grant_minutes == preset);
            bool disabled = (model->grant_max_minutes > 0 && preset > model->grant_max_minutes);
            snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_U_MIN), (unsigned)preset);
            fill_round_rect(pixels, stride, chip, 9, active ? UI_ACCENT : UI_SURFACE);
            draw_rect_outline(pixels, stride, chip, 9, 1, active ? UI_ACCENT : UI_CONTROL);
            fit_text(fitted, sizeof(fitted), buf, 13, chip.width - 8);
            draw_text_center(pixels, stride, chip, fitted, 13,
                             disabled ? UI_DISABLED : (active ? UI_ON_ACCENT : UI_INK));
        }

        snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_U_MIN_A_ENTER), (unsigned)model->grant_minutes);
        draw_candidate_button(pixels, stride, ptc_ui_grant_adjust_rect(0), buf,
                              UI_ACCENT_SOFT, UI_ACCENT,
                              model->overlay_selection == PTC_UI_GRANT_LOCAL_ADJUST_FIRST, false);

        /* Right-side preview inside cfg_box */
        UiRect est_box = {right.x + 422, cfg_box.y + 14, right.x + right.width - 38 - (right.x + 422), cfg_box.height - 28};
        fill_round_rect(pixels, stride, est_box, 10, UI_SURFACE);
        draw_rect_outline(pixels, stride, est_box, 10, 1, UI_BORDER);
        snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_ESTIMATED_USED_S), played);
        fit_text(fitted, sizeof(fitted), buf, 13, est_box.width - 20);
        draw_text(pixels, stride, est_box.x + 12, est_box.y + 26, fitted, 13, UI_MUTED);
        snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_EST_AFTER_REDEMPTION_S_S), estimate,
                 capped ? ptc_ui_text(PTC_UI_T_DAILY_LIMIT_REACHED) : "");
        draw_wrapped_text(pixels, stride, est_box.x + 12, est_box.y + 52, buf,
                          15, est_box.width - 24, 20, 2, reliable ? UI_ACCENT : UI_WARNING_TEXT);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ADJUSTING_THIS_WILL_NOT_CHANGE_THE_DURATION), 11, est_box.width - 20);
        draw_text(pixels, stride, est_box.x + 12, est_box.y + est_box.height - 12, fitted, 11, UI_MUTED);

        /* Lower Voucher Ticket Card (High-Contrast Independent Issued Code) */
        UiRect ticket = {right.x + 24, right.y + 224, right.width - 48, 160};
        fill_round_rect(pixels, stride, ticket, 14, model->grant_has_code ? UI_ACCENT_SOFT : UI_PAGE);
        draw_rect_outline(pixels, stride, ticket, 14, model->grant_has_code ? 2 : 1,
                          model->grant_has_code ? UI_ACCENT : UI_BORDER);
        if (model->grant_has_code) {
            /* Ticket notch cutouts on left & right edges */
            fill_round_rect(pixels, stride, (UiRect){ticket.x - 8, ticket.y + ticket.height / 2 - 8, 16, 16}, 8, UI_SURFACE);
            fill_round_rect(pixels, stride, (UiRect){ticket.x + ticket.width - 8, ticket.y + ticket.height / 2 - 8, 16, 16}, 8, UI_SURFACE);

            ptc_ui_format_code(model->grant_code, code, sizeof(code));
            if (ptc_date_from_day_index(model->grant_day_index, &year, &month, &day))
                snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_VALID_ON_U_02U_02U_ONE_USE), (unsigned)year, (unsigned)month, (unsigned)day);
            else snprintf(buf, sizeof(buf), "%s", ptc_ui_text(PTC_UI_T_THE_ISSUANCE_DATE_NEEDS_TO_BE_CONFIRMED));
            int dw = measure_text(buf, 12) + 18;
            if (dw > 300) dw = 300;
            UiRect date_pill = {ticket.x + ticket.width - 18 - dw, ticket.y + 12, dw, 24};
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_GRANT_HUB_TICKET_ISSUED), 14,
                     date_pill.x - (ticket.x + 20) - 10);
            draw_text_bold(pixels, stride, ticket.x + 20, ticket.y + 30, fitted, 14, UI_ACCENT);
            fill_round_rect(pixels, stride, date_pill, 12, UI_SURFACE);
            fit_text(fitted, sizeof(fitted), buf, 12, date_pill.width - 10);
            draw_text_center(pixels, stride, date_pill, fitted, 12, UI_MUTED);

            /* Large 42px formatted code */
            draw_text_bold(pixels, stride, ticket.x + 22, ticket.y + 88, code, 42, UI_INK);

            snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_CODE_DURATION_U_MIN), (unsigned)model->grant_issued_minutes);
            draw_text_bold(pixels, stride, ticket.x + 340, ticket.y + 72, buf, 22, UI_ACCENT);
            format_duration(model->grant_estimate_available ? model->grant_estimate_minutes : -1, estimate, sizeof(estimate));
            snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_EST_REMAINING_WHEN_GENERATED_S_S), estimate,
                     model->grant_estimate_capped ? ptc_ui_text(PTC_UI_T_DAILY_LIMIT_REACHED) : "");
            fit_text(fitted, sizeof(fitted), buf, 13, ticket.width - 356);
            draw_text(pixels, stride, ticket.x + 340, ticket.y + 98, fitted, 13, UI_MUTED);

            if (model->grant_minutes != model->grant_issued_minutes) {
                UiRect diff_bar = {ticket.x + 20, ticket.y + 116, ticket.width - 40, 30};
                snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_GRANT_HUB_DRAFT_DIFF_FMT), (unsigned)model->grant_minutes);
                fill_round_rect(pixels, stride, diff_bar, 8, UI_WARNING_SOFT);
                draw_rect_outline(pixels, stride, diff_bar, 8, 1, UI_WARNING_BORDER);
                fit_text(fitted, sizeof(fitted), buf, 12, diff_bar.width - 16);
                draw_text_center(pixels, stride, diff_bar, fitted, 12, UI_WARNING_TEXT);
            }
        } else {
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_AFTER_SELECTING_THE_DURATION_PRESS_TO_GENERATE), 22, ticket.width - 40);
            draw_text_center(pixels, stride, (UiRect){ticket.x + 20, ticket.y + 42, ticket.width - 40, 32},
                             fitted, 22, UI_INK);
            draw_wrapped_text_centered(pixels, stride, ticket.x + ticket.width / 2, ticket.y + 96,
                                       ptc_ui_text(PTC_UI_T_PIN_WILL_BE_VERIFIED_AGAIN_BEFORE_GENERATION), 14,
                                       ticket.width - 60, 20, 2, UI_MUTED);
        }

        /* Bottom action row inside right card */
        if (model->grant_notice[0]) {
            fit_text(fitted, sizeof(fitted), model->grant_notice, 14, 420);
            draw_text(pixels, stride, right.x + 24, right.y + 434, fitted, 14, UI_DANGER);
        } else {
            fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_DIRECTION_KEY_SELECTION_A_EDIT_CODE_DURATION), 13, 420);
            draw_text(pixels, stride, right.x + 24, right.y + 434, fitted, 13, UI_MUTED);
        }
        draw_candidate_button(pixels, stride, ptc_ui_grant_generate_rect(),
                              model->grant_has_code ? ptc_ui_text(PTC_UI_T_GENERATE_ANOTHER_ONE)
                                                    : ptc_ui_text(PTC_UI_T_GENERATE_EXTRA_TIME_CODE),
                              UI_ACCENT, UI_ON_ACCENT,
                              model->overlay_selection == PTC_UI_GRANT_LOCAL_GENERATE, model->waiting);
    }
}

static void draw_grant_local_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    draw_grant_hub_page(pixels, stride, model);
}

static void draw_qr_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    draw_grant_hub_page(pixels, stride, model);
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
