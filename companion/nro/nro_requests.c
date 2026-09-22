#include "nro_app_internal.h"

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

void activate_parent_status(UiState *ui)
{
    if (ptc_ui_parent_status_alert_visible(&ui->model)) {
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
