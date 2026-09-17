#include "nro_app_internal.h"

void open_offline_code_input(UiState *ui)
{
    if (ui->recovering_redemption) {
        show_pending_redemption(ui);
        return;
    }
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "紧急停用已开启，当前不能兑换加时码；状态和恢复仍可使用。");
        return;
    }
    ptc_ui_numpad_open(
        &ui->model, PTC_UI_NUMPAD_OFFLINE_CODE, PTC_UI_OVERLAY_NONE,
        "输入加时码", "输入家长给你的 8 位码，确认前会先显示加时预览。", 8, 0, 0, 0);
}

void begin_wait(UiState *ui, const char *type, const char *message)
{
    ui->request_view = ui->model.view;
    ui->waiting = true;
    ui->elapsed_ms = 0;
    ui->last_result[0] = '\0';
    ui->model.result_status[0] = '\0';
    ui->model.feedback_detail[0] = '\0';
    set_command_name(ui, type);
    sync_transport_label(ui);
    snprintf(ui->model.message, sizeof(ui->model.message), "%s", message);
}

void submit_transport_empty(UiState *ui, const char *type, const char *ok_message, const char *fail_prefix)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_empty(&ui->transport, ui->active_request_id, time(NULL), type);
    set_command_name(ui, type);
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, type, ok_message);
        return;
    }
    ui->waiting = false;
    set_message(ui, fail_prefix, status);
}

void submit_status(UiState *ui)
{
    PtcCompanionStatus status;
    if (!ui || ui->waiting) {
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_status(&ui->transport, ui->active_request_id, time(NULL));
    set_command_name(ui, "status");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) begin_wait(ui, "status", "正在刷新今天的状态...");
    else set_message(ui, "刷新失败", status);
}

static bool parent_status_needs_support(const PtcUiModel *model)
{
    return strcmp(model->setup_phase, "protection") == 0 ||
        strcmp(model->setup_phase, "failed") == 0 || model->recovery_active ||
        model->disable_flag_present ||
        (model->temporary_unlocked_available && model->temporary_unlocked);
}

void activate_parent_status(UiState *ui)
{
    if (parent_status_needs_support(&ui->model)) {
        ui->model.parent_page = PTC_UI_PARENT_SUPPORT;
        ui->model.parent_footer_focused = false;
        ui->model.selected_index = ptc_ui_support_recommended_action(&ui->model);
        if (ui->model.selected_index < 0) ui->model.selected_index = 4;
    } else {
        refresh_disable_flag(ui);
        submit_status(ui);
    }
}

void enter_child_area(UiState *ui)
{
    if (!ui) {
        return;
    }
    ui->model.view = PTC_UI_CHILD;
    submit_status(ui);
}

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

static void apply_pending_redemption_preview(UiState *ui, const PtcPendingRedemption *pending)
{
    ui->model.code_grant_minutes = pending->grant_minutes;
    ui->model.code_before_remaining_available = pending->before_remaining_available;
    ui->model.code_before_remaining_minutes = pending->before_remaining_minutes;
    ui->model.code_before_unlimited = pending->before_unlimited;
    ui->model.code_preview_after_available = pending->after_remaining_available;
    ui->model.code_preview_after_minutes = pending->after_remaining_minutes;
    ui->model.code_effective_add_minutes = pending->effective_add_minutes;
    ui->model.code_preview_capped = pending->capped;
    ui->model.code_preview_converts_unlimited = pending->converts_unlimited_to_limited;
}

void show_pending_redemption(UiState *ui)
{
    apply_pending_redemption_preview(ui, &ui->pending_redemption);
    ui->model.view = PTC_UI_CHILD;
    ui->model.overlay = PTC_UI_OVERLAY_CODE_RESULT;
    ui->model.code_result_pending = true;
    ui->model.code_result_failed = false;
    snprintf(ui->model.message, sizeof(ui->model.message),
             "已恢复上次确认的加时请求，结果确认中；请勿重复输入这枚加时码。");
    snprintf(ui->model.result_status, sizeof(ui->model.result_status), "pending");
    snprintf(ui->active_request_id, sizeof(ui->active_request_id), "%s", ui->pending_redemption.request_id);
    set_command_name(ui, "offline_code");
}

void poll_pending_redemption(UiState *ui)
{
    PtcCompanionStatus status;
    if (!ui || !ui->recovering_redemption) return;
    status = ptc_companion_read_result(
        &ui->client, ui->pending_redemption.request_id, 0, -1,
        ui->last_result, sizeof(ui->last_result));
    if (status != PTC_COMPANION_OK) {
        if (status == PTC_COMPANION_RESULT_INVALID || status == PTC_COMPANION_RESULT_MISMATCH) {
            snprintf(ui->model.message, sizeof(ui->model.message),
                     "兑换结果正在确认，已读取到的结果尚不能安全核对；请勿重复输入这枚加时码。");
        }
        return;
    }
    if (!ptc_ui_apply_result_json(&ui->model, ui->last_result) ||
        strcmp(ui->model.result_type, "offline_code") != 0) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "兑换结果正在确认，后台返回内容尚不能安全核对；请勿重复输入这枚加时码。");
        return;
    }
    ui->recovering_redemption = false;
    apply_pending_redemption_preview(ui, &ui->pending_redemption);
    ui->model.view = PTC_UI_CHILD;
    ui->model.overlay = PTC_UI_OVERLAY_CODE_RESULT;
    ui->model.code_result_pending = false;
    ui->model.code_result_failed = strcmp(ui->model.result_status, "ok") != 0;
    (void)load_redemption_history(ui);
    ptc_ui_match_redemption_result(&ui->model);
    ptc_ui_mark_status_updated(&ui->model, ui->model.code_completed_at);
    if (ui->model.code_result_failed) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "%s", ptc_ui_code_failure_guidance(ui->model.error_code));
    } else {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "已恢复并确认上次兑换成功；这枚加时码已经使用，不能再次使用。");
    }
}

bool restore_pending_redemption(UiState *ui)
{
    PtcCompanionStatus status;
    bool found = false;
    status = ptc_companion_pending_redemption_load(&ui->client, &ui->pending_redemption, &found);
    if (status != PTC_COMPANION_OK) {
        ui->model.view = PTC_UI_ERROR;
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "上次加时的恢复信息无法读取。为避免重复兑换，请暂勿再次输入该码。");
        return found;
    }
    if (!found) return false;
    if (!ptc_companion_pending_redemption_has_submission(&ui->client, &ui->pending_redemption)) {
        (void)ptc_companion_pending_redemption_clear(&ui->client);
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "上次确认在提交前中断，加时码未消费；请重新输入。");
        return true;
    }
    ui->recovering_redemption = true;
    show_pending_redemption(ui);
    poll_pending_redemption(ui);
    return true;
}

void submit_preview_offline_code(UiState *ui, const char *code)
{
    PtcCompanionStatus status;
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_preview_offline_code(
        &ui->transport, ui->active_request_id, time(NULL), code);
    set_command_name(ui, "preview_offline_code");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "preview_offline_code", "正在验证加时码并计算生效预览...");
        return;
    }
    ui->waiting = false;
    set_message(ui, "加时码预览失败", status);
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
    open_confirm_overlay(ui, PTC_UI_OPERATION_REDEEM_OFFLINE_CODE,
                         refreshed ? "状态已变化，请再次确认" : "确认兑换加时码", body);
    ui->model.confirm_hold_required = !ui->model.code_preview_after_available ||
        ui->model.code_preview_after_minutes == 0 ||
        ui->model.code_preview_converts_unlimited;
}

void submit_minutes(UiState *ui, PtcUiOperation operation, uint16_t minutes)
{
    PtcCompanionStatus status;
    const char *type;
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用已开启，额度修改不可用。");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    if (operation == PTC_UI_OPERATION_SET_TODAY_LIMIT) {
        type = "set_today_limit";
        status = ptc_companion_transport_submit_set_today_limit(&ui->transport, ui->active_request_id, time(NULL), minutes);
    } else if (operation == PTC_UI_OPERATION_ADD_TODAY_MINUTES) {
        type = "add_today_minutes";
        status = ptc_companion_transport_submit_add_today_minutes(&ui->transport, ui->active_request_id, time(NULL), minutes);
    } else {
        ui->waiting = false;
        snprintf(ui->model.message, sizeof(ui->model.message), "不支持的额度操作。");
        return;
    }
    set_command_name(ui, type);
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, type, "设置已提交，正在等待后台确认...");
    } else {
        ui->waiting = false;
        set_message(ui, "设置提交失败", status);
    }
}

void submit_weekly(UiState *ui)
{
    PtcCompanionStatus status;
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用已开启，周计划暂不能保存。");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_weekly_template(
        &ui->transport,
        ui->active_request_id,
        time(NULL),
        ui->model.draft_week);
    set_command_name(ui, "set_weekly_template");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "set_weekly_template", "正在保存周计划...");
    } else {
        ui->waiting = false;
        set_message(ui, "每周计划提交失败", status);
    }
}

void submit_holiday_policy(UiState *ui)
{
    PtcCompanionStatus status;
    if (ui->model.disable_flag_present) {
        snprintf(ui->model.message, sizeof(ui->model.message), "紧急停用中，国家节假日设置暂时只读。");
        return;
    }
    if (!ui->model.holiday_dirty) {
        snprintf(ui->model.message, sizeof(ui->model.message), "国家节假日设置没有修改。");
        return;
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    status = ptc_companion_transport_submit_set_holiday_policy(&ui->transport, ui->active_request_id, time(NULL),
        ui->model.draft_holiday_enabled, ui->model.draft_holiday_rule, ui->model.draft_makeup_workday_rule);
    set_command_name(ui, "set_holiday_policy");
    sync_transport_label(ui);
    if (status == PTC_COMPANION_OK) {
        begin_wait(ui, "set_holiday_policy", "正在保存国家节假日设置...");
    } else {
        set_message(ui, "国家节假日设置提交失败", status);
    }
}

static PtcRuleMode parse_rule_mode(const char *mode)
{
    if (mode && strcmp(mode, "unlimited") == 0) {
        return PTC_RULE_MODE_UNLIMITED;
    }
    return PTC_RULE_MODE_LIMIT;
}

void edit_scheduled_minutes(UiState *ui)
{
    PtcScheduledOverride *draft;
    if (!ui) return;
    draft = &ui->model.draft_scheduled_override;
    if (draft->rule.mode != PTC_RULE_MODE_LIMIT) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "当前为不限时；按 X 切换为限时后可编辑额度。");
        return;
    }
    ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_SCHEDULED_MINUTES,
        PTC_UI_OVERLAY_SCHEDULED, "设置临时额度计划",
        "分别输入小时和分钟，总计 1 到 1440 分钟", 4, 1, 1440,
        draft->rule.minutes);
}

void edit_grant_minutes(UiState *ui)
{
    if (!ui) return;
    ptc_ui_numpad_open(&ui->model, PTC_UI_NUMPAD_GRANT_MINUTES,
        PTC_UI_OVERLAY_GRANT_LOCAL, "设置代码时长",
        "合法面额：1-4、5-120 每 5 分钟，以及 150/180/210/240", 4, 1,
        ui->model.grant_max_minutes, ui->model.grant_minutes);
}

static PtcBedtimeOverrideMode parse_bedtime_override_mode(const char *mode)
{
    if (mode && strcmp(mode, "disabled") == 0) return PTC_BEDTIME_OVERRIDE_DISABLED;
    if (mode && strcmp(mode, "custom") == 0) return PTC_BEDTIME_OVERRIDE_CUSTOM;
    return PTC_BEDTIME_OVERRIDE_INHERIT;
}

static const char *rule_json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : NULL;
}

static int rule_json_int(const cJSON *object, const char *name, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static uint16_t clamp_rule_minutes(int value)
{
    if (value < 15) {
        return 15;
    }
    if (value > 1440) {
        return 1440;
    }
    return (uint16_t)value;
}

void load_rule_drafts(UiState *ui)
{
    PtcRules rules;
    char text[RESULT_TEXT_SIZE];
    cJSON *root;
    const cJSON *week;
    const cJSON *version;
    const cJSON *override_present;
    const cJSON *override_mode;
    const cJSON *override_minutes;
    const cJSON *override_day;
    unsigned int index;
    ptc_rules_default(&rules);
    memcpy(ui->model.draft_week, rules.week, sizeof(rules.week));
    memcpy(ui->model.current_week, rules.week, sizeof(rules.week));
    ui->model.today_override_present = false;
    ui->model.holiday_enabled = rules.holiday_enabled;
    ui->model.draft_holiday_enabled = rules.holiday_enabled;
    ui->model.holiday_rule = rules.holiday_rule;
    ui->model.draft_holiday_rule = rules.holiday_rule;
    ui->model.makeup_workday_rule = rules.makeup_workday_rule;
    ui->model.draft_makeup_workday_rule = rules.makeup_workday_rule;
    ui->model.scheduled_override = rules.scheduled_override;
    ui->model.draft_scheduled_override = rules.scheduled_override;
    ui->model.autonomy_policy = rules.autonomy_policy;
    ui->model.draft_autonomy_policy = rules.autonomy_policy;
    ui->model.bedtime_policy = rules.bedtime;
    ui->model.draft_bedtime_policy = rules.bedtime;
    if (!ui->client.storage->vtable->read_text(ui->client.storage, RULES_PATH, text, sizeof(text))) {
        return;
    }
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || (version->valueint != 1 && version->valueint != 2)) {
        cJSON_Delete(root);
        return;
    }
    week = cJSON_GetObjectItemCaseSensitive(root, "week");
    if (cJSON_IsArray(week) && cJSON_GetArraySize(week) == 7) {
        for (index = 0; index < 7; ++index) {
            const cJSON *day = cJSON_GetArrayItem(week, (int)index);
            rules.week[index].mode = parse_rule_mode(rule_json_string(day, "mode"));
            rules.week[index].minutes = clamp_rule_minutes(rule_json_int(day, "minutes", rules.week[index].minutes));
        }
    }
    override_present = cJSON_GetObjectItemCaseSensitive(root, "today_override_present");
    override_day = cJSON_GetObjectItemCaseSensitive(root, "today_override_day_index");
    override_mode = cJSON_GetObjectItemCaseSensitive(root, "today_override_mode");
    override_minutes = cJSON_GetObjectItemCaseSensitive(root, "today_override_minutes");
    if (cJSON_IsTrue(override_present) && cJSON_IsNumber(override_day) &&
        override_day->valueint == (int)ui->model.day_index) {
        ui->model.today_override_present = true;
        ui->model.today_override_rule.mode = parse_rule_mode(cJSON_GetStringValue(override_mode));
        ui->model.today_override_rule.minutes = clamp_rule_minutes(
            cJSON_IsNumber(override_minutes) ? override_minutes->valueint : 60);
    }
    rules.holiday_enabled = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "holiday_enabled"));
    rules.holiday_rule.mode = parse_rule_mode(rule_json_string(root, "holiday_mode"));
    rules.holiday_rule.minutes = clamp_rule_minutes(rule_json_int(root, "holiday_minutes", rules.holiday_rule.minutes));
    rules.makeup_workday_rule.mode = parse_rule_mode(rule_json_string(root, "makeup_workday_mode"));
    rules.makeup_workday_rule.minutes = clamp_rule_minutes(
        rule_json_int(root, "makeup_workday_minutes", rules.makeup_workday_rule.minutes));
    rules.scheduled_override.enabled = cJSON_IsTrue(
        cJSON_GetObjectItemCaseSensitive(root, "scheduled_override_enabled"));
    rules.scheduled_override.start_day_index = (uint16_t)rule_json_int(
        root, "scheduled_override_start_day_index", 0);
    rules.scheduled_override.end_day_index = (uint16_t)rule_json_int(
        root, "scheduled_override_end_day_index", 0);
    rules.scheduled_override.rule.mode = parse_rule_mode(
        rule_json_string(root, "scheduled_override_mode"));
    rules.scheduled_override.rule.minutes = clamp_rule_minutes(rule_json_int(
        root, "scheduled_override_minutes", rules.scheduled_override.rule.minutes));
    if (!ptc_scheduled_override_is_valid(&rules.scheduled_override)) {
        rules.scheduled_override.enabled = false;
    }
    rules.autonomy_policy.daily_buffer_minutes = (uint16_t)rule_json_int(
        root, "daily_buffer_minutes", 0);
    if (!ptc_autonomy_policy_is_valid(&rules.autonomy_policy)) {
        rules.autonomy_policy.daily_buffer_minutes = 0;
    }
    if (version->valueint >= 2) {
        const cJSON *bedtime_week = cJSON_GetObjectItemCaseSensitive(root, "bedtime_week");
        const cJSON *item;
        rules.bedtime.enabled = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "bedtime_enabled"));
        if (cJSON_IsArray(bedtime_week) && cJSON_GetArraySize(bedtime_week) == 7) {
            for (index = 0; index < 7; ++index) {
                const cJSON *day = cJSON_GetArrayItem(bedtime_week, (int)index);
                rules.bedtime.week[index].enabled = cJSON_IsTrue(
                    cJSON_GetObjectItemCaseSensitive(day, "enabled"));
                rules.bedtime.week[index].start_minute = (uint16_t)rule_json_int(
                    day, "start_minute", rules.bedtime.week[index].start_minute);
                rules.bedtime.week[index].end_minute = (uint16_t)rule_json_int(
                    day, "end_minute", rules.bedtime.week[index].end_minute);
            }
        }
        rules.bedtime.calendar_enabled = cJSON_IsTrue(
            cJSON_GetObjectItemCaseSensitive(root, "bedtime_calendar_enabled"));
        rules.bedtime.holiday_rule.mode = parse_bedtime_override_mode(
            rule_json_string(root, "bedtime_holiday_mode"));
        rules.bedtime.holiday_rule.window.enabled = cJSON_IsTrue(
            cJSON_GetObjectItemCaseSensitive(root, "bedtime_holiday_enabled"));
        rules.bedtime.holiday_rule.window.start_minute = (uint16_t)rule_json_int(
            root, "bedtime_holiday_start_minute", rules.bedtime.holiday_rule.window.start_minute);
        rules.bedtime.holiday_rule.window.end_minute = (uint16_t)rule_json_int(
            root, "bedtime_holiday_end_minute", rules.bedtime.holiday_rule.window.end_minute);
        rules.bedtime.makeup_workday_rule.mode = parse_bedtime_override_mode(
            rule_json_string(root, "bedtime_makeup_mode"));
        rules.bedtime.makeup_workday_rule.window.enabled = cJSON_IsTrue(
            cJSON_GetObjectItemCaseSensitive(root, "bedtime_makeup_enabled"));
        rules.bedtime.makeup_workday_rule.window.start_minute = (uint16_t)rule_json_int(
            root, "bedtime_makeup_start_minute", rules.bedtime.makeup_workday_rule.window.start_minute);
        rules.bedtime.makeup_workday_rule.window.end_minute = (uint16_t)rule_json_int(
            root, "bedtime_makeup_end_minute", rules.bedtime.makeup_workday_rule.window.end_minute);
        rules.bedtime.scheduled_override.present = cJSON_IsTrue(
            cJSON_GetObjectItemCaseSensitive(root, "bedtime_scheduled_present"));
        rules.bedtime.scheduled_override.start_day_index = (uint16_t)rule_json_int(
            root, "bedtime_scheduled_start_day_index", rules.bedtime.scheduled_override.start_day_index);
        rules.bedtime.scheduled_override.end_day_index = (uint16_t)rule_json_int(
            root, "bedtime_scheduled_end_day_index", rules.bedtime.scheduled_override.end_day_index);
        rules.bedtime.scheduled_override.rule.mode = parse_bedtime_override_mode(
            rule_json_string(root, "bedtime_scheduled_mode"));
        rules.bedtime.scheduled_override.rule.window.enabled = cJSON_IsTrue(
            cJSON_GetObjectItemCaseSensitive(root, "bedtime_scheduled_enabled"));
        rules.bedtime.scheduled_override.rule.window.start_minute = (uint16_t)rule_json_int(
            root, "bedtime_scheduled_start_minute", rules.bedtime.scheduled_override.rule.window.start_minute);
        rules.bedtime.scheduled_override.rule.window.end_minute = (uint16_t)rule_json_int(
            root, "bedtime_scheduled_end_minute", rules.bedtime.scheduled_override.rule.window.end_minute);
        rules.bedtime.confirmation_version = (uint16_t)rule_json_int(
            root, "bedtime_confirmation_version", rules.bedtime.confirmation_version);
        item = cJSON_GetObjectItemCaseSensitive(root, "bedtime_official_setting_confirmed_at");
        if (cJSON_IsNumber(item)) rules.bedtime.official_setting_confirmed_at = (int64_t)item->valuedouble;
        item = cJSON_GetObjectItemCaseSensitive(root, "bedtime_confirmed_environment");
        if (cJSON_IsString(item) && item->valuestring) {
            snprintf(rules.bedtime.confirmed_environment, sizeof(rules.bedtime.confirmed_environment),
                "%s", item->valuestring);
        }
        rules.bedtime.unverified_overlay_risk_accepted = cJSON_IsTrue(
            cJSON_GetObjectItemCaseSensitive(root, "bedtime_overlay_risk_accepted"));
        if (!ptc_bedtime_policy_is_valid(&rules.bedtime)) {
            PtcRules defaults;
            ptc_rules_default(&defaults);
            rules.bedtime = defaults.bedtime;
        }
    }
    memcpy(ui->model.draft_week, rules.week, sizeof(rules.week));
    memcpy(ui->model.current_week, rules.week, sizeof(rules.week));
    ui->model.weekly_dirty = false;
    ui->model.holiday_enabled = rules.holiday_enabled;
    ui->model.draft_holiday_enabled = rules.holiday_enabled;
    ui->model.holiday_rule = rules.holiday_rule;
    ui->model.draft_holiday_rule = rules.holiday_rule;
    ui->model.makeup_workday_rule = rules.makeup_workday_rule;
    ui->model.draft_makeup_workday_rule = rules.makeup_workday_rule;
    ui->model.scheduled_override = rules.scheduled_override;
    ui->model.draft_scheduled_override = rules.scheduled_override;
    ui->model.autonomy_policy = rules.autonomy_policy;
    ui->model.draft_autonomy_policy = rules.autonomy_policy;
    ui->model.bedtime_policy = rules.bedtime;
    ui->model.draft_bedtime_policy = rules.bedtime;
    ui->model.holiday_dirty = false;
    cJSON_Delete(root);
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
