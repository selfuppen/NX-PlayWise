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
    snprintf(title, sizeof(title), ptc_ui_text(PTC_UI_T_SETUP_STEP_D_5), step);
    draw_header(pixels, stride, grace_remaining >= 0 ? (ptc_ui_text(PTC_UI_T_SYNCING)) : title,
        grace_remaining >= 0
            ? (ptc_ui_text(PTC_UI_T_SYNCING_SYSTEM_SETTINGS_PLEASE_WAIT_TO_CHOOSE))
            : (ptc_ui_text(PTC_UI_T_COMPLETE_PLAYWISE_PARENTAL_SETUP_STEP_BY_STEP)));
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    for (int i = 0; i < 5; ++i)
        fill_round_rect(pixels, stride, (UiRect){104 + i * 212, 167, 188, 5}, 2,
            i < step ? UI_ACCENT : UI_RAISED);
    if (grace_remaining >= 0) {
        draw_text(pixels, stride, 204, 190, ptc_ui_text(PTC_UI_T_ENVIRONMENT_CHECK_PASSED), 31, UI_SUCCESS);
        snprintf(phase_line, sizeof(phase_line), ptc_ui_text(PTC_UI_T_CURRENT_SYNCING_PRE_INSTALL_S),
                 model->setup_snapshot_available ? ptc_ui_text(PTC_UI_T_SAVED) : ptc_ui_text(PTC_UI_T_UNAVAILABLE_2));
        if (grace_remaining > 0)
            snprintf(countdown_line, sizeof(countdown_line), ptc_ui_text(PTC_UI_T_SYNCING_SYSTEM_SETTINGS_ABOUT_LLD_SECONDS), (long long)grace_remaining);
        else
            snprintf(countdown_line, sizeof(countdown_line), "%s", ptc_ui_text(PTC_UI_T_MESSAGE_4));
        draw_text(pixels, stride, 204, 248, phase_line, 22, UI_MUTED);
        draw_text(pixels, stride, 204, 310, countdown_line, 34, UI_ACCENT);
        draw_text(pixels, stride, 204, 356, ptc_ui_text(PTC_UI_T_NO_ACTION_NEEDED_ADVANCES_TO_STEP_5), 22, UI_INK);
    } else {
        const char *const step_labels[5] = {ptc_ui_text(PTC_UI_T_1_SHORTCUT), ptc_ui_text(PTC_UI_T_2_PIN), ptc_ui_text(PTC_UI_T_3_THEME), ptc_ui_text(PTC_UI_T_4_ENABLE_CONTROLS), ptc_ui_text(PTC_UI_T_5_SELECT_ZONE)};
        for (int i = 0; i < 5; ++i) {
            UiRect step_rect = {104 + i * 212, 142, 188, 20};
            draw_text_center(pixels, stride, step_rect, step_labels[i], 16,
                             step == i + 1 ? UI_ACCENT : UI_MUTED);
        }

        if (step == PTC_UI_SETUP_SHORTCUT) {
            UiRect compact_fixed = {204, 184, 872, 54};
            fill_round_rect(pixels, stride, compact_fixed, 12, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, compact_fixed, 12, 2, UI_ACCENT);
            draw_text(pixels, stride, 232, 218, ptc_ui_text(PTC_UI_T_FIXED_MINUS_RELEASE_TO_ENTER_WITHOUT_HOLDING), 20, UI_ACCENT);
            draw_text(pixels, stride, 204, 264, ptc_ui_text(PTC_UI_T_CUSTOM_COMBO_REQUIRES_400MS_HOLD_A_TO), 18, UI_MUTED);
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
                      model->shortcut_draft_enabled ? ptc_ui_text(PTC_UI_T_PENDING_CUSTOM_COMBO_HOLD_REQUIRED) : ptc_ui_text(PTC_UI_T_PENDING_MINUS_ONLY_RELEASE_TO_ENTER),
                      17, UI_MUTED);
            if (model->shortcut_draft_enabled) {
                fit_text(fitted, sizeof(fitted), model->shortcut_draft_label, 18, 250);
                draw_text(pixels, stride, 420, 554, fitted, 18, UI_ACCENT);
            }
        } else if (step == PTC_UI_SETUP_PIN) {
            draw_text(pixels, stride, 204, 220, ptc_ui_text(PTC_UI_T_PLAYWISE_PIN_CONFIGURED), 30, UI_INK);
            draw_text(pixels, stride, 204, 262, ptc_ui_text(PTC_UI_T_FRESH_INSTALL_DEFAULT_PIN_110), 24, UI_WARNING);
            draw_text(pixels, stride, 204, 294, ptc_ui_text(PTC_UI_T_DEFAULT_IS_WEAK_PROTECTION_KEEP_IT_OR), 20, UI_MUTED);
            draw_dialog_button(pixels, stride, ptc_ui_setup_pin_rect(), ptc_ui_text(PTC_UI_T_X_CLICK_CHANGE_PIN),
                               UI_ACCENT, UI_ON_ACCENT, false);
            draw_text(pixels, stride, 204, 420, ptc_ui_text(PTC_UI_T_USED_FOR_PARENT_ZONE_SEPARATE_FROM_NINTENDO), 19, UI_MUTED);
        } else if (step == PTC_UI_SETUP_TAKEOVER) {
            bool resuming_restored_setup = model->disable_flag_present && strcmp(phase, "restored") == 0;
            bool reconfirming_environment = ptc_ui_runtime_fingerprint_reconfirmation_needed(model);
            bool takeover_complete = ptc_ui_setup_takeover_complete(model);
            draw_text(pixels, stride, 204, 218,
                      takeover_complete ? ptc_ui_text(PTC_UI_T_PLAYTIME_CONTROLS_ACTIVE) :
                      (reconfirming_environment ? ptc_ui_text(PTC_UI_T_SYSTEM_ENVIRONMENT_CHANGED) :
                       (resuming_restored_setup ? ptc_ui_text(PTC_UI_T_RE_ENABLE_CONTROLS) : ptc_ui_text(PTC_UI_T_CONFIRM_ENABLE_CONTROLS))),
                      30, takeover_complete ? UI_SUCCESS : UI_INK);
            snprintf(phase_line, sizeof(phase_line), ptc_ui_text(PTC_UI_T_CURRENT_S_PRE_INSTALL_S),
                     takeover_complete ? (strcmp(phase, "active") == 0 ? ptc_ui_text(PTC_UI_T_RUNNING_NORMALLY) : ptc_ui_text(PTC_UI_T_SYNCHRONIZING)) :
                     (strcmp(phase, "protection") == 0 ? ptc_ui_text(PTC_UI_T_PROTECTION_MODE) :
                     (strcmp(phase, "failed") == 0 ? ptc_ui_text(PTC_UI_T_CHECK_FAILED) :
                      (resuming_restored_setup ? ptc_ui_text(PTC_UI_T_RESTORED_DISABLED) : ptc_ui_text(PTC_UI_T_AWAITING_PARENTAL_CONFIRMATION)))),
                     model->setup_snapshot_available ? ptc_ui_text(PTC_UI_T_SAVED) : ptc_ui_text(PTC_UI_T_PENDING_SAVE));

            draw_text(pixels, stride, 204, 266, phase_line, 21, UI_MUTED);
            draw_text(pixels, stride, 204, 324,
                      takeover_complete
                          ? ptc_ui_text(PTC_UI_T_PLAYTIME_CONTROLS_ACTIVE_PROCEED_TO_SELECT_ZONE)
                          : reconfirming_environment
                           ? ptc_ui_text(PTC_UI_T_SYSTEM_VERSION_OR_ENVIRONMENT_CHANGED_RECONFIRMATION_REQUIRED)
                          : resuming_restored_setup
                           ? ptc_ui_text(PTC_UI_T_CHECKS_CONSOLE_FIRST_RESUMES_PLAYTIME_MANAGEMENT_ONCE)
                           : ptc_ui_text(PTC_UI_T_CHECKS_CONSOLE_SUPPORT_FIRST_THEN_ENABLES_PLAYTIME),
                      21, UI_INK);
            draw_text(pixels, stride, 204, 360,
                      takeover_complete
                          ? ptc_ui_text(PTC_UI_T_PRESS_A_OR_CLICK_TO_CONTINUE_TO)
                          : reconfirming_environment
                           ? ptc_ui_text(PTC_UI_T_KEEPS_EXISTING_PLAN_AND_RESUMES_MANAGEMENT_UPON)
                           : resuming_restored_setup
                            ? ptc_ui_text(PTC_UI_T_RETAINS_CURRENT_PLAN_MANAGE_OR_REVOKE_IN)
                            : ptc_ui_text(PTC_UI_T_FIRST_ACTIVATION_PRESERVES_TODAY_TOTAL_AND_REMAINING),
                      21, UI_INK);
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(),
                               takeover_complete ? ptc_ui_text(PTC_UI_T_A_CLICK_TO_CONTINUE_TO_STEP_5) :
                               (reconfirming_environment ? ptc_ui_text(PTC_UI_T_A_CLICK_RE_CHECK_ENABLE) :
                                (resuming_restored_setup ? ptc_ui_text(PTC_UI_T_A_CLICK_RE_ENABLE) : ptc_ui_text(PTC_UI_T_A_CLICK_CONFIRM))),
                               takeover_complete ? UI_SUCCESS : UI_ACCENT,
                               UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_THEME) {
            const char *LABELS[] = {ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM), ptc_ui_text(PTC_UI_T_LIGHT), ptc_ui_text(PTC_UI_T_DARK)};
            const char *DETAILS[] = {ptc_ui_text(PTC_UI_T_FOLLOWS_SWITCH_SETTINGS), ptc_ui_text(PTC_UI_T_CLASSIC_LIGHT_LOOK), ptc_ui_text(PTC_UI_T_SOFT_DARK_BACKGROUND)};
            draw_text(pixels, stride, 204, 220, ptc_ui_text(PTC_UI_T_SELECT_APPEARANCE_THEME), 30, UI_INK);
            draw_text(pixels, stride, 204, 252, ptc_ui_text(PTC_UI_T_FOLLOWS_SYSTEM_BY_DEFAULT_AFFECTS_CONSOLE_APP), 18, UI_MUTED);
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
            draw_text(pixels, stride, 204, 448, ptc_ui_text(PTC_UI_T_LEFT_RIGHT_TO_SELECT_A_SAVE_NEXT), 18, UI_ACCENT);
        } else {
            draw_text(pixels, stride, 204, 214, ptc_ui_text(PTC_UI_T_SETUP_COMPLETE_SELECT_DESTINATION_ZONE), 30, UI_INK);
            draw_text(pixels, stride, 204, 254, ptc_ui_text(PTC_UI_T_SWITCH_BETWEEN_ZONES_ANYTIME_PARENT_ZONE_REQUIRES), 21, UI_MUTED);
            for (int index = 0; index < 2; ++index) {
                UiRect card = to_uirect(ptc_ui_setup_zone_rect(index));
                bool selected = index == model->setup_zone_index;
                fill_round_rect(pixels, stride, card, 16, selected ? UI_ACCENT_SOFT : UI_RAISED);
                draw_rect_outline(pixels, stride, card, 16, selected ? 3 : 1, selected ? UI_ACCENT : UI_CONTROL);
                draw_text_center(pixels, stride, (UiRect){card.x, card.y + 26, card.width, 34},
                                 index == 0 ? ptc_ui_text(PTC_UI_T_CHILD_ZONE) : ptc_ui_text(PTC_UI_T_PARENT_ZONE), 27,
                                 selected ? UI_ACCENT : UI_INK);
                if (index == 0) {
                    char shortcut_hint[160];
                    char fitted_shortcut_hint[160];
                    ptc_ui_format_custom_shortcut_hint(model->custom_shortcut_label,
                                                       shortcut_hint, sizeof(shortcut_hint));
                    fit_text(fitted_shortcut_hint, sizeof(fitted_shortcut_hint), shortcut_hint, 17, card.width - 36);
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 86, card.width - 36, 26},
                                     model->show_parent_shortcut_hint && model->custom_shortcut_enabled
                                        ? fitted_shortcut_hint : ptc_ui_text(PTC_UI_T_PARENT_SHORTCUT_HINT_HIDDEN), 17, UI_MUTED);
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     ptc_ui_text(PTC_UI_T_PARENT_ZONE_REQUIRES_PLAYWISE_PIN), 17, UI_MUTED);
                } else {
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 86, card.width - 36, 26},
                                     ptc_ui_text(PTC_UI_T_FIXED_MINUS_RELEASE_TO_ENTER), 18, UI_MUTED);
                    draw_text_center(pixels, stride, (UiRect){card.x + 18, card.y + 122, card.width - 36, 25},
                                     model->custom_shortcut_enabled ? ptc_ui_text(PTC_UI_T_CUSTOM_COMBO_HOLD_400MS) : ptc_ui_text(PTC_UI_T_CUSTOM_COMBO_DISABLED), 17, UI_MUTED);
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
        draw_dialog_button(pixels, stride, ptc_ui_setup_back_rect(), ptc_ui_text(PTC_UI_T_B_PREVIOUS_STEP),
                           UI_RAISED, UI_INK, true);
        if (step == PTC_UI_SETUP_SHORTCUT) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), ptc_ui_text(PTC_UI_T_CONFIRM_NEXT),
                               UI_ACCENT, UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_ZONE) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(),
                               model->setup_zone_index == 1 ? ptc_ui_text(PTC_UI_T_A_ENTER_PARENT_ZONE) : ptc_ui_text(PTC_UI_T_A_ENTER_CHILD_ZONE),
                               UI_ACCENT, UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_PIN) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), ptc_ui_text(PTC_UI_T_A_KEEP_CURRENT_PIN),
                               UI_ACCENT, UI_ON_ACCENT, false);
        } else if (step == PTC_UI_SETUP_THEME) {
            draw_dialog_button(pixels, stride, ptc_ui_setup_primary_rect(), ptc_ui_text(PTC_UI_T_A_SAVE_THEME_NEXT),
                               UI_ACCENT, UI_ON_ACCENT, false);
        }
    }
}

void draw_error(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {214, 148, 852, 444};
    char fitted[192];
    char execution[150];
    draw_header(pixels, stride, ptc_ui_text(PTC_UI_T_OPERATION_INCOMPLETE), ptc_ui_text(PTC_UI_T_PLEASE_CHECK_THE_ERROR_MESSAGE_AND_TRY));
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    fill_round_rect(pixels, stride, (UiRect){254, 194, 64, 64}, 16, UI_DANGER);
    draw_text_center(pixels, stride, (UiRect){254, 194, 64, 64}, "!", 34, UI_ON_ACCENT);
    draw_text(pixels, stride, 342, 214, ptc_ui_text(PTC_UI_T_GRANT_CODE_PROCESSING_FAILED), 28, UI_INK);
    snprintf(
        execution,
        sizeof(execution),
        ptc_ui_text(PTC_UI_T_COMMAND_S_S),
        model->command_name[0] ? model->command_name : (ptc_ui_text(PTC_UI_T_NOT_STARTED)),
        model->transport_label[0] ? model->transport_label : (ptc_ui_text(PTC_UI_T_TRANSPORT_NOT_STARTED)));

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
                    model->error_code == 306 ? ptc_ui_text(PTC_UI_T_A_RETEST) : ptc_ui_text(PTC_UI_T_A_RE_ENTER), 25, UI_ON_ACCENT);
    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 12, UI_RAISED);
    draw_rect_outline(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 12, 1, UI_CONTROL);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_back_rect()), ptc_ui_text(PTC_UI_T_B_RETURN_TO_HOMEPAGE), 25, UI_INK);
}
