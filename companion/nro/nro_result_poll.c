#include "nro_app_internal.h"

static void submit_offline_code(UiState *ui, const char *code)
{
    PtcCompanionStatus status;
    PtcPendingRedemption pending;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    memset(&pending, 0, sizeof(pending));
    snprintf(pending.request_id, sizeof(pending.request_id), "%s", ui->active_request_id);
    pending.confirmed_at = (int64_t)time(NULL);
    pending.grant_minutes = ui->model.code_grant_minutes;
    pending.before_remaining_available = ui->model.code_before_remaining_available;
    pending.before_remaining_minutes = ui->model.code_before_remaining_minutes;
    pending.before_unlimited = ui->model.code_before_unlimited;
    pending.after_remaining_available = ui->model.code_preview_after_available;
    pending.after_remaining_minutes = ui->model.code_preview_after_minutes;
    pending.effective_add_minutes = ui->model.code_effective_add_minutes;
    pending.capped = ui->model.code_preview_capped;
    pending.converts_unlimited_to_limited = ui->model.code_preview_converts_unlimited;
    status = ptc_companion_pending_redemption_save(&ui->client, &pending);
    if (status != PTC_COMPANION_OK) {
        ui->waiting = false;
        ui->model.pending_code[0] = '\0';
        set_message(ui, "无法保存兑换恢复信息；加时码未提交，仍可使用", status);
        return;
    }
    status = ptc_companion_transport_submit_offline_code(&ui->transport, ui->active_request_id, time(NULL), code);
    set_command_name(ui, "offline_code");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        pending.submitted = true;
        (void)ptc_companion_pending_redemption_save(&ui->client, &pending);
        ui->pending_redemption = pending;
        begin_wait(ui, "offline_code", "加时码已提交，正在等待后台确认...");
        return;
    }
    (void)ptc_companion_pending_redemption_clear(&ui->client);
    ui->model.pending_code[0] = '\0';
    ui->waiting = false;
    set_message(ui, "加时码提交失败；该码未消费，仍可使用", status);
}

static bool code_error_stays_in_input(int error_code)
{
    return error_code >= PTC_ERR_BAD_CODE && error_code <= PTC_ERR_CODE_COOLDOWN;
}

static void open_code_preview_confirm(UiState *ui, bool refreshed)
{
    const char *body = refreshed
        ? "实时状态发生了重要变化，已重新计算预览。\n确认后才会生效并消费这枚加时码。"
        : "请核对当前状态和兑换后的预计结果。\n确认前不会消费这枚加时码。";
    bool requires_hold = !ui->model.code_preview_after_available ||
        ui->model.code_preview_after_minutes == 0 ||
        ui->model.code_preview_converts_unlimited;
    if (requires_hold)
        open_danger_confirm_overlay(ui, PTC_UI_OPERATION_REDEEM_OFFLINE_CODE,
                                    refreshed ? "状态已变化，请再次确认" : "确认兑换加时码", body);
    else
        open_confirm_overlay(ui, PTC_UI_OPERATION_REDEEM_OFFLINE_CODE,
                             refreshed ? "状态已变化，请再次确认" : "确认兑换加时码", body);
}

static void sync_setup_wizard(UiState *ui)
{
    bool active;
    if (!ui) {
        return;
    }
    active = strcmp(ui->model.setup_phase, "active") == 0;
    if (!active) {
        if (strcmp(ui->model.setup_phase, "restored") == 0) {
            if (ui->model.setup_step != PTC_UI_SETUP_TAKEOVER) {
                (void)save_setup_step(ui, PTC_UI_SETUP_TAKEOVER);
            }
        } else if (ui->model.setup_step == 0) {
            (void)save_setup_step(ui, PTC_UI_SETUP_SHORTCUT);
        }
        ui->model.view = PTC_UI_SETUP;
    } else if (ui->model.setup_step > 0) {
        ui->model.view = PTC_UI_SETUP;
    } else if (ui->model.view == PTC_UI_SETUP) {
        ui->model.view = PTC_UI_CHILD;
    }
}

void poll_result(UiState *ui, bool force)
{
    PtcCompanionStatus status;
    PtcDayRule saved_draft[7];
    bool preserve_weekly_draft;
    bool preserve_holiday_draft;
    bool saved_holiday_enabled;
    PtcDayRule saved_holiday_rule;
    PtcDayRule saved_makeup_rule;
    PtcScheduledOverride saved_scheduled_draft;
    bool preserve_scheduled_draft;
    PtcBedtimePolicy saved_bedtime_draft;
    bool preserve_bedtime_draft;
    if (!ui->waiting) {
        if (force) {
            submit_status(ui);
        }
        return;
    }
    if (ui->active_request_id[0] == '\0') {
        if (force) {
            submit_status(ui);
        }
        return;
    }
    if (ui->waiting) {
        ui->elapsed_ms += BACKGROUND_POLL_INTERVAL_MS;
    }
    status = ptc_companion_transport_poll(
        &ui->transport,
        BACKGROUND_POLL_INTERVAL_MS,
        REQUEST_TIMEOUT_MS,
        ui->last_result,
        sizeof(ui->last_result));
    sync_transport_label(ui);
    if (status == PTC_COMPANION_PENDING) {
        snprintf(ui->model.message, sizeof(ui->model.message), "后台正在处理，请稍候...");
        return;
    }
    ui->waiting = false;
    if (status == PTC_COMPANION_OK) {
        saved_scheduled_draft = ui->model.draft_scheduled_override;
        preserve_scheduled_draft = ptc_ui_scheduled_dirty(&ui->model);
        saved_bedtime_draft = ui->model.draft_bedtime_policy;
        preserve_bedtime_draft = ui->model.bedtime_dirty ||
            (ui->model.parent_page == PTC_UI_PARENT_PLAN &&
             ui->model.plan_page == PTC_UI_PLAN_PAGE_BEDTIME);
        preserve_weekly_draft = ui->model.weekly_dirty;
        if (preserve_weekly_draft) {
            memcpy(saved_draft, ui->model.draft_week, sizeof(saved_draft));
        }
        preserve_holiday_draft = ui->model.holiday_dirty;
        saved_holiday_enabled = ui->model.draft_holiday_enabled;
        saved_holiday_rule = ui->model.draft_holiday_rule;
        saved_makeup_rule = ui->model.draft_makeup_workday_rule;
        if (!ptc_ui_apply_result_json(&ui->model, ui->last_result)) {
            ui->pending_parent_page = -1;
            ui->pending_leave_parent = false;
            set_message(ui, "读取结果失败", PTC_COMPANION_RESULT_INVALID);
            if (ui->request_view == PTC_UI_CHILD) ui->model.view = PTC_UI_ERROR;
            return;
        }
        if (strcmp(ui->model.result_type, "status") == 0 &&
            ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
            ui->model.grant_status_refresh_failed = strcmp(ui->model.result_status, "ok") != 0;
        }
        if (strcmp(ui->model.result_type, "complete_setup") == 0 &&
            strcmp(ui->model.result_status, "ok") == 0) {
            ui->model.setup_zone_index = 1;
            (void)save_setup_step(ui, PTC_UI_SETUP_ZONE);
        }
        sync_setup_wizard(ui);
        load_rule_drafts(ui);
        if (strcmp(ui->model.result_status, "ok") == 0 &&
            strcmp(ui->model.result_type, "restore_today_policy") == 0) {
            ui->model.today_override_cleared_in_session = true;
        } else if (ui->model.today_override_present) {
            ui->model.today_override_cleared_in_session = false;
        }
        ptc_ui_reconcile_scheduled_result(&ui->model, &saved_scheduled_draft, preserve_scheduled_draft);
        if (preserve_bedtime_draft &&
            !(strcmp(ui->model.result_type, "set_bedtime_policy") == 0 &&
              strcmp(ui->model.result_status, "ok") == 0)) {
            ui->model.draft_bedtime_policy = saved_bedtime_draft;
            ui->model.bedtime_dirty = memcmp(&ui->model.draft_bedtime_policy,
                &ui->model.bedtime_policy, sizeof(PtcBedtimePolicy)) != 0;
        } else if (strcmp(ui->model.result_type, "set_bedtime_policy") == 0 &&
                   strcmp(ui->model.result_status, "ok") == 0) {
            ui->model.bedtime_dirty = false;
        }
        if (ui->model.status_loaded && strcmp(ui->model.result_status, "ok") == 0) {
            ptc_ui_mark_status_updated(&ui->model, (int64_t)time(NULL));
        }
        if (preserve_weekly_draft &&
            !(strcmp(ui->model.result_type, "set_weekly_template") == 0 &&
              strcmp(ui->model.result_status, "ok") == 0)) {
            memcpy(ui->model.draft_week, saved_draft, sizeof(saved_draft));
            update_weekly_dirty(ui);
        }
        if (preserve_holiday_draft &&
            !(strcmp(ui->model.result_type, "set_holiday_policy") == 0 &&
              strcmp(ui->model.result_status, "ok") == 0)) {
            ui->model.draft_holiday_enabled = saved_holiday_enabled;
            ui->model.draft_holiday_rule = saved_holiday_rule;
            ui->model.draft_makeup_workday_rule = saved_makeup_rule;
            update_holiday_dirty(ui);
        }
        refresh_disable_flag(ui);
        if (strcmp(ui->model.result_type, "clear_redemption_history") == 0) {
            bool cleared = strcmp(ui->model.result_status, "ok") == 0;
            open_redemption_history(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                     cleared ? "加时码使用记录已清空；防重复兑换账本保持不变。"
                             : "清空加时码使用记录失败，原记录已保留。");
        }
        if (strcmp(ui->model.result_type, "clear_activity_history") == 0) {
            bool cleared = strcmp(ui->model.result_status, "ok") == 0;
            open_activity_history(ui);
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                cleared ? "家庭活动记录已清空；规则和加时码防重复账本保持不变。"
                        : "清空家庭活动记录失败，原记录已保留。");
        }
        if (strcmp(ui->model.result_type, "preview_offline_code") == 0) {
            if (strcmp(ui->model.result_status, "ok") == 0) {
                bool after_zero = ui->model.code_preview_after_available &&
                    ui->model.code_preview_after_minutes == 0;
                bool material_change = ui->code_preview_recheck &&
                    (ui->code_previous_after_available != ui->model.code_preview_after_available ||
                     ui->code_previous_after_zero != after_zero ||
                     ui->code_previous_capped != ui->model.code_preview_capped ||
                     ui->code_previous_converts_unlimited != ui->model.code_preview_converts_unlimited);
                if (ui->code_preview_recheck && !material_change) {
                    ui->code_preview_recheck = false;
                    ui->model.code_before_remaining_available = ui->model.remaining_available;
                    ui->model.code_before_remaining_minutes = ui->model.remaining_minutes;
                    ui->model.code_before_unlimited = ui->model.unrestricted_today == 1;
                    submit_offline_code(ui, ui->model.pending_code);
                } else {
                    ui->code_preview_recheck = false;
                    open_code_preview_confirm(ui, material_change);
                }
            } else {
                ui->code_preview_recheck = false;
                ui->model.pending_code[0] = '\0';
                if (code_error_stays_in_input(ui->model.error_code)) {
                    char error[96];
                    snprintf(error, sizeof(error), "%.95s", ui->model.message);
                    open_offline_code_input(ui);
                    snprintf(ui->model.numpad_error, sizeof(ui->model.numpad_error), "%s", error);
                } else {
                    ui->model.view = PTC_UI_ERROR;
                }
            }
        } else if (strcmp(ui->model.result_type, "offline_code") == 0) {
            ui->model.pending_code[0] = '\0';
            ui->model.code_result_pending = false;
            ui->model.code_result_failed = strcmp(ui->model.result_status, "ok") != 0;
            (void)load_redemption_history(ui);
            ptc_ui_match_redemption_result(&ui->model);
            ptc_ui_mark_status_updated(&ui->model, ui->model.code_completed_at);
            if (strcmp(ui->model.result_status, "ok") == 0) {
                ui->model.overlay = PTC_UI_OVERLAY_CODE_RESULT;
                ui->model.operation = PTC_UI_OPERATION_NONE;
            } else if (code_error_stays_in_input(ui->model.error_code)) {
                char error[96];
                snprintf(error, sizeof(error), "%.95s", ui->model.message);
                open_offline_code_input(ui);
                snprintf(ui->model.numpad_error, sizeof(ui->model.numpad_error), "%s", error);
                (void)ptc_companion_pending_redemption_clear(&ui->client);
            } else {
                char original[192];
                snprintf(original, sizeof(original), "%s", ui->model.message);
                snprintf(ui->model.message, sizeof(ui->model.message),
                         "兑换未成功，加时码仍可使用。");
                snprintf(ui->model.feedback_detail, sizeof(ui->model.feedback_detail), "%s", original);
                ui->model.view = PTC_UI_ERROR;
                (void)ptc_companion_pending_redemption_clear(&ui->client);
            }
        }
        if (ui->pending_today_action >= 0 && strcmp(ui->model.result_type, "status") == 0) {
            int action = ui->pending_today_action;
            ui->pending_today_action = -1;
            if (strcmp(ui->model.result_status, "ok") == 0) {
                handle_today_action_ready(ui, action);
            } else {
                snprintf(ui->model.message, sizeof(ui->model.message),
                         "无法刷新当前状态，已取消本次时间调整。请重试。");
            }
        }
        if (ui->request_view == PTC_UI_CHILD && strcmp(ui->model.result_status, "error") == 0 &&
            strcmp(ui->model.result_type, "preview_offline_code") != 0 &&
            strcmp(ui->model.result_type, "offline_code") != 0) {
            ui->model.view = PTC_UI_ERROR;
        }
        if (ui->pending_parent_page >= 0 || ui->pending_leave_parent) {
            if (strcmp(ui->model.result_type, "set_weekly_template") == 0 &&
                strcmp(ui->model.result_status, "ok") == 0) {
                apply_pending_navigation(ui);
            } else if (strcmp(ui->model.result_type, "set_weekly_template") == 0) {
                ui->pending_parent_page = -1;
                ui->pending_leave_parent = false;
                snprintf(ui->model.message, sizeof(ui->model.message),
                         "周计划保存未完成，修改仍保留，请重试。");
            } else if (strcmp(ui->model.result_type, "set_bedtime_policy") == 0 &&
                       strcmp(ui->model.result_status, "ok") == 0) {
                ui->model.bedtime_dirty = false;
                apply_pending_navigation(ui);
            } else if (strcmp(ui->model.result_type, "set_bedtime_policy") == 0) {
                ui->pending_parent_page = -1;
                ui->pending_leave_parent = false;
                snprintf(ui->model.message, sizeof(ui->model.message),
                         "就寝时间保存未完成，修改仍保留，请重试。");
            }
        }
        if (strcmp(ui->model.result_type, "confirm_bedtime_requirements") == 0) {
            if (strcmp(ui->model.result_status, "ok") == 0 && ui->model.bedtime_dirty) {
                /* First enablement is one guarded save operation: after the
                 * environment acknowledgement is persisted, submit the
                 * unchanged full draft before honoring pending navigation. */
                submit_bedtime_policy(ui);
                if (!ui->waiting) {
                    ui->pending_parent_page = -1;
                    ui->pending_leave_parent = false;
                }
                return;
            }
            if (strcmp(ui->model.result_status, "ok") != 0) {
                ui->pending_parent_page = -1;
                ui->pending_leave_parent = false;
                snprintf(ui->model.message, sizeof(ui->model.message),
                    "就寝限制环境确认未完成，草稿仍保留，请检查后重试。");
            }
        }
        if (strcmp(ui->model.result_type, "skip_bedtime") == 0 &&
            strcmp(ui->model.result_status, "error") == 0 && ui->model.error_code == 318) {
            ui->model.pending_bedtime_skip_instance_id = 0;
            submit_status(ui);
            snprintf(ui->model.message, sizeof(ui->model.message),
                "就寝窗口已变化，正在刷新；不会自动跳过另一个窗口。");
        }
        return;
    }
    if (ui->pending_parent_page >= 0 || ui->pending_leave_parent) {
        ui->pending_parent_page = -1;
        ui->pending_leave_parent = false;
    }
    if (ui->model.overlay == PTC_UI_OVERLAY_GRANT_LOCAL) {
        ui->model.grant_status_refresh_failed = true;
    }
    if (ui->pending_redemption.request_id[0] != '\0' &&
        strcmp(ui->pending_redemption.request_id, ui->active_request_id) == 0) {
        ptc_companion_transport_cancel(&ui->transport);
        ui->recovering_redemption = true;
        show_pending_redemption(ui);
        return;
    }
    set_message(ui, "读取结果失败", status);
    if (ui->request_view == PTC_UI_CHILD) ui->model.view = PTC_UI_ERROR;
}
