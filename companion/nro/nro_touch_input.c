#include "nro_app_internal.h"

/* A tap resolves to the renderer's control geometry, then drives the same
   shared action as its button shortcut so pointer and pad stay in lock-step. */
void handle_touch(UiState *ui, int x, int y)
{
    PtcUiHit hit = ptc_ui_hit_test(&ui->model, x, y);
    if (ui->waiting && ui->model.view == PTC_UI_PARENT &&
        ui->model.overlay == PTC_UI_OVERLAY_NONE) {
        snprintf(ui->model.message, sizeof(ui->model.message), "请等待当前操作完成后再修改设置。");
        return;
    }
    switch (hit.kind) {
    case PTC_UI_HIT_CHILD_SUBMIT_CODE:
        if (ui->waiting) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), "请等待当前操作完成后再提交加时码。");
        } else {
            ptc_audio_play(PTC_SE_CONFIRM);
            open_offline_code_input(ui);
        }
        break;
    case PTC_UI_HIT_CHILD_REFRESH:
        ptc_audio_play(PTC_SE_CONFIRM);
        submit_status(ui);
        break;
    case PTC_UI_HIT_CHILD_BUFFER:
        if (!ui->waiting && ui->model.daily_buffer_available) {
            ptc_audio_play(PTC_SE_CLAIM_BUFFER);
            submit_transport_empty(ui, "claim_daily_buffer",
                "正在领取今日自主缓冲...", "领取今日自主缓冲失败");
        } else {
            ptc_audio_play(PTC_SE_ERROR);
        }
        break;
    case PTC_UI_HIT_CHILD_PARENT:
        ptc_audio_play(PTC_SE_CONFIRM);
        enter_parent_area(ui);
        break;
    case PTC_UI_HIT_CHILD_EXIT:
        ptc_audio_play(PTC_SE_CANCEL);
        ui->exit_requested = true;
        break;
    case PTC_UI_HIT_ERROR_RETRY:
        ptc_audio_play(PTC_SE_CONFIRM);
        retry_error(ui);
        break;
    case PTC_UI_HIT_ERROR_BACK:
        ptc_audio_play(PTC_SE_CANCEL);
        enter_child_area(ui);
        break;
    case PTC_UI_HIT_SETUP_SHORTCUT_CARD:
        ptc_audio_play(PTC_SE_FOCUS);
        select_setup_shortcut(ui, hit.index);
        break;
    case PTC_UI_HIT_SETUP_PRIMARY:
        ptc_audio_play(PTC_SE_CONFIRM);
        setup_primary(ui);
        break;
    case PTC_UI_HIT_SETUP_BACK:
        ptc_audio_play(PTC_SE_CANCEL);
        setup_previous(ui);
        break;
    case PTC_UI_HIT_SETUP_PIN:
        ptc_audio_play(PTC_SE_CONFIRM);
        setup_pin(ui);
        break;
    case PTC_UI_HIT_SETUP_CHILD_ZONE:
        ptc_audio_play(PTC_SE_FOCUS);
        ui->model.setup_zone_index = 0;
        break;
    case PTC_UI_HIT_SETUP_PARENT_ZONE:
        ptc_audio_play(PTC_SE_FOCUS);
        ui->model.setup_zone_index = 1;
        break;
    case PTC_UI_HIT_PARENT_PREV_PAGE:
        ptc_audio_play(PTC_SE_TAB);
        request_parent_navigation(ui,
            (ui->model.parent_page + PTC_UI_PARENT_PAGE_COUNT - 1) % PTC_UI_PARENT_PAGE_COUNT, false);
        break;
    case PTC_UI_HIT_PARENT_NEXT_PAGE:
        ptc_audio_play(PTC_SE_TAB);
        request_parent_navigation(ui, (ui->model.parent_page + 1) % PTC_UI_PARENT_PAGE_COUNT, false);
        break;
    case PTC_UI_HIT_PARENT_REFRESH:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.parent_footer_focused = true;
        ui->model.parent_footer_selection = 0;
        refresh_disable_flag(ui);
        submit_status(ui);
        break;
    case PTC_UI_HIT_PARENT_STATUS:
        if (!ptc_ui_parent_status_alert_visible(&ui->model)) break;
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.parent_footer_focused = true;
        ui->model.parent_footer_selection = 1;
        activate_parent_status(ui);
        break;
    case PTC_UI_HIT_PARENT_BACK:
        ptc_audio_play(PTC_SE_CANCEL);
        request_parent_navigation(ui, -1, true);
        break;
    case PTC_UI_HIT_PARENT_TAB:
        ptc_audio_play(PTC_SE_TAB);
        request_parent_navigation(ui, hit.index, false);
        break;
    case PTC_UI_HIT_PARENT_CARD:
        if (ui->waiting) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), "请等待当前操作完成后再执行其他设置。");
        } else {
            ptc_audio_play(PTC_SE_CONFIRM);
            ui->model.selected_index = hit.index;
            if (!(ui->model.parent_page == PTC_UI_PARENT_PLAN &&
                  ui->model.plan_page == PTC_UI_PLAN_PAGE_HOLIDAY) || hit.index >= 3) {
                handle_parent_action(ui);
            }
        }
        break;
    case PTC_UI_HIT_HOLIDAY_CALENDAR:
        ptc_audio_play(PTC_SE_POPUP);
        ui->model.selected_index = 6;
        handle_parent_action(ui);
        break;
    case PTC_UI_HIT_HOLIDAY_PAGE_ACTION:
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_SUPPORT_EVENT:
        if (hit.index >= 0 && hit.index < ui->model.recent_event_count) {
            ptc_audio_play(PTC_SE_POPUP);
            ui->model.selected_index = 6 + (ui->model.recent_event_count - 1 - hit.index);
            ui->model.overlay = PTC_UI_OVERLAY_SUPPORT_EVENT;
            ui->model.overlay_selection = hit.index;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "最近事件详情");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                     "家长区已通过 PIN 验证；这里显示完整诊断字段，但不会显示 PIN、密钥或可复用授权材料。");
        }
        break;
    case PTC_UI_HIT_FORECAST_DAY:
        if (hit.index >= 0 && hit.index < 7 && ui->model.forecast_available) {
            ptc_audio_play(PTC_SE_POPUP);
            ui->model.selected_index = 5 + hit.index;
            ui->model.forecast_detail_day_offset = hit.index;
            ui->model.overlay = PTC_UI_OVERLAY_DAY_DECISION;
            ui->model.overlay_selection = hit.index;
        }
        break;
    case PTC_UI_HIT_HOME_DETAILS:
        ptc_audio_play(PTC_SE_POPUP);
        ptc_ui_open_home_details(&ui->model);
        break;
    case PTC_UI_HIT_OVERLAY_CANCEL:
        ptc_audio_play(PTC_SE_CANCEL);
        if (ui->model.overlay == PTC_UI_OVERLAY_HOME_DETAILS ||
            ui->model.overlay == PTC_UI_OVERLAY_NOTICE_DETAILS ||
            ui->model.overlay == PTC_UI_OVERLAY_DAY_DECISION ||
            ui->model.overlay == PTC_UI_OVERLAY_PARENT_EXPORT_RESULT) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_REDEMPTION_HISTORY) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_AUTH_ERROR) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL ||
            ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL ||
            ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
                ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_BACK;
            }
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
            close_code_result(ui);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM &&
                   (ui->model.operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
                    ui->model.operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
                    ui->model.operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY)) {
            ui->model.overlay_selection = 0;
            handle_overlay_input(ui, HidNpadButton_A);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE ||
                   ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM &&
                   (ui->model.operation == PTC_UI_OPERATION_SAVE_BEDTIME ||
                    ui->model.operation == PTC_UI_OPERATION_SAVE_WEEKLY)) {
            handle_overlay_input(ui, HidNpadButton_B);
        } else {
            ptc_ui_cancel_overlay(&ui->model);
            snprintf(ui->model.message, sizeof(ui->model.message), "已取消修改。");
        }
        break;
    case PTC_UI_HIT_OVERLAY_CONFIRM:
        if (ui->model.overlay == PTC_UI_OVERLAY_REDEMPTION_HISTORY ||
            ui->model.overlay == PTC_UI_OVERLAY_ACTIVITY_HISTORY) {
            handle_overlay_input(ui, HidNpadButton_X);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CODE_RESULT) {
            close_code_result(ui);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE ||
            ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE) {
            handle_overlay_input(ui, HidNpadButton_A);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_SUPPORT_EVENT) {
            handle_overlay_input(ui, HidNpadButton_A);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_CALENDAR) {
            handle_overlay_input(ui, HidNpadButton_A);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM &&
            (ui->model.operation == PTC_UI_OPERATION_ENABLE_ALBUM_RESTRICTION ||
             ui->model.operation == PTC_UI_OPERATION_RESTORE_ALBUM_ENTRY ||
             ui->model.operation == PTC_UI_OPERATION_FORCE_RESTORE_ALBUM_ENTRY)) {
            ui->model.overlay_selection = 1;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL) {
            ui->model.overlay_selection = PTC_UI_CREDENTIAL_SAVE;
        } else if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            ui->model.overlay_selection = 1;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CONFIRM && ui->model.confirm_hold_required) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "为避免误操作，请长按手柄 A 或持续按住触摸确认按钮。");
        } else {
            handle_overlay_input(ui, ptc_ui_overlay_primary_uses_plus(ui->model.overlay)
                ? HidNpadButton_Plus : HidNpadButton_A);
        }
        break;
    case PTC_UI_HIT_OVERLAY_DISCARD:
        ptc_audio_play(PTC_SE_CANCEL);
        if (ui->model.overlay == PTC_UI_OVERLAY_HOLIDAY_LEAVE ||
            ui->model.overlay == PTC_UI_OVERLAY_BEDTIME_LEAVE) {
            handle_overlay_input(ui, HidNpadButton_X);
            break;
        }
        if (ui->model.overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
            ui->model.overlay_selection = 0;
        }
        handle_overlay_input(ui, HidNpadButton_X);
        break;
    case PTC_UI_HIT_HISTORY_PREV:
        ptc_audio_play(PTC_SE_TAB);
        handle_overlay_input(ui, HidNpadButton_Left);
        break;
    case PTC_UI_HIT_HISTORY_NEXT:
        ptc_audio_play(PTC_SE_TAB);
        handle_overlay_input(ui, HidNpadButton_Right);
        break;
    case PTC_UI_HIT_SCHEDULED_FIELD:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_AUTONOMY_OPTION:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.overlay_selection = hit.index;
        ui->model.draft_autonomy_policy.daily_buffer_minutes = (uint16_t)(hit.index * 5);
        break;
    case PTC_UI_HIT_QUICK_ADD_OPTION:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_BEDTIME_SECTION:
        ptc_audio_play(PTC_SE_TAB);
        ui->model.bedtime_master_focused = false;
        select_bedtime_section(ui, hit.index);
        break;
    case PTC_UI_HIT_BEDTIME_MANAGE:
        ptc_audio_play(PTC_SE_CONFIRM);
        if (hit.index == 0) handle_today_action_ready(ui, PTC_UI_OPERATION_SKIP_BEDTIME);
        else request_clear_bedtime_skip(ui);
        break;
    case PTC_UI_HIT_BEDTIME_MASTER_SWITCH:
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.bedtime_master_focused = true;
        ui->model.bedtime_section_focused = false;
        if (!ui->model.disable_flag_present) {
            ui->model.draft_bedtime_policy.enabled = !ui->model.draft_bedtime_policy.enabled;
            update_bedtime_dirty(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "就寝计划总开关草稿已%s；保存后生效。",
                     ui->model.draft_bedtime_policy.enabled ? "开启" : "关闭");
        }
        break;
    case PTC_UI_HIT_BEDTIME_FIELD:
        ui->model.bedtime_master_focused = false;
        ui->model.bedtime_section_focused = false;
        ui->model.selected_index = hit.index;
        if (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
            ui->model.plan_page == PTC_UI_PLAN_PAGE_BEDTIME) {
            if (ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY && hit.index < 7) {
                int day = ptc_ui_weekday_for_display_slot(hit.index);
                ui->model.bedtime_editor_day = day;
                ptc_audio_play(PTC_SE_POPUP);
                open_bedtime_window_editor(ui, day);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY &&
                       (hit.index == 7 || hit.index == 8)) {
                ptc_audio_play(PTC_SE_POPUP);
                ui->model.overlay = PTC_UI_OVERLAY_BEDTIME_BULK;
                ui->model.overlay_selection = hit.index - 7;
                snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "复制每周就寝窗口");
                snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
                    "把最后编辑日期的完整开关和时间复制到所选日期组。");
            } else if ((ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY && hit.index == 9) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index == 3) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 4)) {
                ptc_audio_play(PTC_SE_CANCEL);
                discard_bedtime_draft(ui);
            } else if ((ui->model.bedtime_section == PTC_UI_BEDTIME_WEEKLY && hit.index == 10) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index == 4) ||
                       (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 5)) {
                ptc_audio_play(PTC_SE_CONFIRM);
                save_bedtime_from_page(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index == 0) {
                ptc_audio_play(PTC_SE_TOGGLE);
                ui->model.draft_bedtime_policy.calendar_enabled =
                    !ui->model.draft_bedtime_policy.calendar_enabled;
                update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_CALENDAR && hit.index <= 2) {
                ptc_audio_play(PTC_SE_POPUP);
                open_bedtime_special_editor(ui, hit.index - 1);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 0) {
                ptc_audio_play(PTC_SE_TOGGLE);
                ui->model.draft_bedtime_policy.scheduled_override.present =
                    !ui->model.draft_bedtime_policy.scheduled_override.present;
                update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 1) {
                ptc_audio_play(PTC_SE_POPUP);
                if (edit_date_range_start(ui,
                        &ui->model.draft_bedtime_policy.scheduled_override.start_day_index,
                        &ui->model.draft_bedtime_policy.scheduled_override.end_day_index))
                    update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 2) {
                ptc_audio_play(PTC_SE_POPUP);
                if (edit_date_range_span(ui,
                        ui->model.draft_bedtime_policy.scheduled_override.start_day_index,
                        &ui->model.draft_bedtime_policy.scheduled_override.end_day_index))
                    update_bedtime_dirty(ui);
            } else if (ui->model.bedtime_section == PTC_UI_BEDTIME_SCHEDULED && hit.index == 3) {
                ptc_audio_play(PTC_SE_POPUP);
                open_bedtime_special_editor(ui, 2);
            }
        }
        break;
    case PTC_UI_HIT_BEDTIME_OVERLAY_FIELD:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_BEDTIME_PRESET:
        ptc_audio_play(PTC_SE_STEP);
        apply_bedtime_preset(ui, hit.index);
        break;
    case PTC_UI_HIT_MINUTES_INC:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_DEC:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -5, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_INC_LARGE:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, 15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_DEC_LARGE:
        ptc_audio_play(PTC_SE_STEP);
        ui->model.draft_minutes = ptc_ui_adjust_minutes(ui->model.draft_minutes, -15, ui->model.minimum_minutes, ui->model.maximum_minutes);
        break;
    case PTC_UI_HIT_MINUTES_VALUE:
        ptc_audio_play(PTC_SE_POPUP);
        edit_overlay_minutes(ui);
        break;
    case PTC_UI_HIT_WEEKLY_DAY:
        ptc_audio_play(PTC_SE_FOCUS);
        ui->model.editor_index = hit.index;
        for (int slot = 0; slot < 7; ++slot) {
            if (ptc_ui_weekday_for_display_slot(slot) == hit.index) {
                ui->model.weekly_grid_slot = slot;
                ui->model.weekly_last_day_slot = slot;
                break;
            }
        }
        ui->model.selected_index = 0;
        break;
    case PTC_UI_HIT_WEEKLY_BULK:
        ui->model.selected_index = 2;
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，批量操作暂不可用。");
        } else {
            ptc_audio_play(PTC_SE_POPUP);
            ui->model.overlay = PTC_UI_OVERLAY_WEEKLY_BULK;
            ui->model.overlay_selection = 0;
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "批量快捷操作");
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "把最后选中日期的完整草稿规则复制到一组日期。");
        }
        break;
    case PTC_UI_HIT_WEEKLY_BULK_TARGET:
        ptc_audio_play(PTC_SE_FOCUS);
        ui->model.overlay_selection = hit.index;
        break;
    case PTC_UI_HIT_ALBUM_ACTION:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_ALBUM_REFRESH:
        ptc_audio_play(PTC_SE_CONFIRM);
        handle_overlay_input(ui, HidNpadButton_Y);
        break;
    case PTC_UI_HIT_WEEKLY_MODE:
        if (hit.index >= 0 && hit.index < 7) {
            ui->model.editor_index = hit.index;
            for (int slot = 0; slot < 7; ++slot) {
                if (ptc_ui_weekday_for_display_slot(slot) == hit.index) {
                    ui->model.weekly_grid_slot = slot;
                    ui->model.weekly_last_day_slot = slot;
                    break;
                }
            }
        }
        ui->model.selected_index = 0;
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.draft_week[ui->model.editor_index].mode =
            ptc_ui_next_rule_mode(ui->model.draft_week[ui->model.editor_index].mode);
        update_weekly_dirty(ui);
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            snprintf(ui->model.message, sizeof(ui->model.message), "已恢复此前的每日限额：%u 分钟。",
                     (unsigned int)ui->model.draft_week[ui->model.editor_index].minutes);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_UP:
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 15, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DOWN:
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -15, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_DEC:
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, -5, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_INC:
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_STEP);
            ui->model.draft_week[ui->model.editor_index].minutes =
                ptc_ui_adjust_minutes(ui->model.draft_week[ui->model.editor_index].minutes, 5, 1, 1440);
            update_weekly_dirty(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_MIN_INPUT:
        if (hit.index >= 0 && hit.index < 7) {
            ui->model.editor_index = hit.index;
            for (int slot = 0; slot < 7; ++slot) {
                if (ptc_ui_weekday_for_display_slot(slot) == hit.index) {
                    ui->model.weekly_grid_slot = slot;
                    ui->model.weekly_last_day_slot = slot;
                    break;
                }
            }
        }
        ui->model.selected_index = 0;
        if (weekly_editing_blocked(ui)) {
            ptc_audio_play(PTC_SE_ERROR);
            break;
        }
        if (ui->model.draft_week[ui->model.editor_index].mode == PTC_RULE_MODE_LIMIT) {
            ptc_audio_play(PTC_SE_POPUP);
            edit_weekly_minutes(ui);
        } else {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "当前为不限时模式，请先切换为限时模式。");
        }
        break;
    case PTC_UI_HIT_HOLIDAY_ENABLE:
        ui->model.selected_index = 0;
        if (ui->model.disable_flag_present) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，规则暂时只读。");
        } else {
            ptc_audio_play(PTC_SE_TOGGLE);
            ui->model.draft_holiday_enabled = !ui->model.draft_holiday_enabled;
            update_holiday_dirty(ui);
        }
        break;
    case PTC_UI_HIT_HOLIDAY_MODE:
        ui->model.selected_index = hit.index + 1;
        ui->model.holiday_last_rule = hit.index;
        if (ui->model.disable_flag_present) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，规则暂时只读。");
        } else if (hit.index == 0) {
            ptc_audio_play(PTC_SE_TOGGLE);
            ui->model.draft_holiday_rule.mode = ptc_ui_next_rule_mode(ui->model.draft_holiday_rule.mode);
            update_holiday_dirty(ui);
        } else if (hit.index == 1) {
            ptc_audio_play(PTC_SE_TOGGLE);
            ui->model.draft_makeup_workday_rule.mode = ptc_ui_next_rule_mode(ui->model.draft_makeup_workday_rule.mode);
            update_holiday_dirty(ui);
        }
        break;
    case PTC_UI_HIT_HOLIDAY_MINUTES:
        ui->model.selected_index = hit.index + 1;
        ui->model.holiday_last_rule = hit.index;
        if (ui->model.disable_flag_present) {
            ptc_audio_play(PTC_SE_ERROR);
            snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，规则暂时只读。");
        } else {
            ptc_audio_play(PTC_SE_POPUP);
            handle_parent_action(ui);
        }
        break;
    case PTC_UI_HIT_WEEKLY_SAVE:
        ui->model.selected_index = 4;
        ptc_audio_play(PTC_SE_CONFIRM);
        save_weekly_from_page(ui);
        break;
    case PTC_UI_HIT_WEEKLY_DISCARD:
        ui->model.selected_index = 3;
        ptc_audio_play(PTC_SE_CANCEL);
        if (ui->model.weekly_dirty) {
            memcpy(ui->model.draft_week, ui->model.current_week, sizeof(ui->model.draft_week));
            ui->model.weekly_dirty = false;
            snprintf(ui->model.message, sizeof(ui->model.message), "已放弃未保存的周计划修改。");
        } else {
            snprintf(ui->model.message, sizeof(ui->model.message), "周计划没有修改。");
        }
        break;
    case PTC_UI_HIT_CREDENTIAL_INPUT:
        ptc_audio_play(PTC_SE_KEYSTROKE);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
        handle_overlay_input(ui, HidNpadButton_X);
        break;
    case PTC_UI_HIT_CREDENTIAL_RANDOM:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_RANDOM;
        handle_overlay_input(ui, HidNpadButton_Y);
        break;
    case PTC_UI_HIT_CREDENTIAL_REVEAL:
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_REVEAL;
        handle_overlay_input(ui, HidNpadButton_ZR);
        break;
    case PTC_UI_HIT_CREDENTIAL_DEMO:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = PTC_UI_CREDENTIAL_DEMO;
        handle_overlay_input(ui, HidNpadButton_R);
        break;
    case PTC_UI_HIT_GRANT_MANAGER_CARD:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_GRANT_GENERATE:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
        generate_local_grant_code(ui);
        break;
    case PTC_UI_HIT_SHORTCUT_OPTION:
        ptc_audio_play(PTC_SE_FOCUS);
        select_setup_shortcut(ui, hit.index);
        break;
    case PTC_UI_HIT_SHORTCUT_DISABLE:
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.shortcut_draft_enabled = false;
        break;
    case PTC_UI_HIT_SHORTCUT_HINT:
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.shortcut_draft_show_hint = !ui->model.shortcut_draft_show_hint;
        break;
    case PTC_UI_HIT_THEME_OPTION:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_LANGUAGE_OPTION:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.overlay_selection = hit.index;
        handle_overlay_input(ui, HidNpadButton_A);
        break;
    case PTC_UI_HIT_QR_EXPORT:
        ptc_audio_play(PTC_SE_CONFIRM);
        export_parent_import(ui);
        break;
    case PTC_UI_HIT_SETUP_THEME_OPTION:
        ptc_audio_play(PTC_SE_CONFIRM);
        ui->model.setup_theme_index = hit.index;
        setup_primary(ui);
        break;
    case PTC_UI_HIT_GRANT_ADJUST:
        ptc_audio_play(PTC_SE_POPUP);
        ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_ADJUST_FIRST;
        edit_grant_minutes(ui);
        break;
    case PTC_UI_HIT_NUMPAD_KEY:
        ptc_audio_play(PTC_SE_KEYSTROKE);
        ui->model.numpad_cursor = hit.index;
        ptc_ui_numpad_activate(&ui->model);
        break;
    case PTC_UI_HIT_NUMPAD_QUICK:
        if (hit.index >= 0 && hit.index < 2) {
            static const int DELTAS[] = {-15, 15};
            ptc_audio_play(PTC_SE_STEP);
            ptc_ui_numpad_adjust(&ui->model, DELTAS[hit.index]);
        }
        break;
    case PTC_UI_HIT_DURATION_FIELD:
        ptc_audio_play(PTC_SE_FOCUS);
        ptc_ui_duration_select_field(&ui->model, (PtcUiDurationField)hit.index);
        break;
    case PTC_UI_HIT_TODAY_MODE:
        ptc_audio_play(PTC_SE_TOGGLE);
        ui->model.today_limit_unlimited_draft = hit.index == 1;
        break;
    case PTC_UI_HIT_QUOTA_REFRESH:
        ptc_audio_play(PTC_SE_CONFIRM);
        start_quota_recheck(ui, true);
        break;
    case PTC_UI_HIT_TODAY_LIMIT_REFRESH:
        ptc_audio_play(PTC_SE_CONFIRM);
        refresh_today_limit_editor(ui, false);
        break;
    case PTC_UI_HIT_NOTICE_DETAILS:
        ptc_audio_play(PTC_SE_POPUP);
        ptc_ui_open_notice_details(&ui->model);
        break;
    case PTC_UI_HIT_NONE:
    default:
        break;
    }
}
