#include "ui_state.h"
#include "ui_layout.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../common/protocol/error_code.h"
#include "../../common/rules/holiday_calendar.h"
#include "../../common/time/ptc_time.h"
#include "../../third_party/cjson/cJSON.h"

int ptc_ui_migrate_setup_step(int step, int wizard_version)
{
    if (step <= 0) return 0;
    if (wizard_version >= 5) return step <= PTC_UI_SETUP_CONFIRM ? step : PTC_UI_SETUP_PREPARE;
    return PTC_UI_SETUP_PREPARE;
}

void ptc_ui_open_setup_pctl_help(PtcUiModel *model)
{
    if (!model || model->view != PTC_UI_SETUP ||
        model->setup_step != PTC_UI_SETUP_PREPARE || model->overlay != PTC_UI_OVERLAY_NONE) return;
    model->overlay = PTC_UI_OVERLAY_SETUP_PCTL_HELP;
    snprintf(model->overlay_title, sizeof(model->overlay_title), "%s",
        ptc_ui_text(PTC_UI_T_SETUP_PCTL_HELP_TITLE));
    model->overlay_body[0] = '\0';
}

void ptc_ui_setup_sync(PtcUiModel *model)
{
    if (!model) return;
    /* Older completed installs have no independent completion marker. Only a
       confirmed active backend can migrate their zero progress to completed. */
    if (!model->setup_completion_known && (model->setup_step == 0 || model->setup_legacy_completed_candidate) &&
        strcmp(model->setup_phase, "active") == 0) {
        model->setup_wizard_completed = true;
        model->setup_completion_known = true;
        model->setup_step = 0;
        if (model->view == PTC_UI_SETUP) model->view = PTC_UI_CHILD;
    }
    if (model->setup_wizard_completed) return;
    if (model->setup_step == 0) model->setup_step = PTC_UI_SETUP_PREPARE;
    if (model->view != PTC_UI_PARENT) model->view = PTC_UI_SETUP;
}

void ptc_ui_setup_record_issue(PtcUiModel *model, PtcUiSetupIssueKind kind, int code)
{
    if (!model || kind < 0 || kind >= PTC_UI_SETUP_ISSUE_COUNT) return;
    model->setup_issue_mask |= 1U << kind;
    model->setup_issue_steps[kind] = model->setup_step;
    model->setup_issue_codes[kind] = code;
}

bool ptc_ui_setup_reference_matches(const PtcUiModel *model)
{
    return model && model->environment_available && model->environment_atmosphere &&
        strcmp(model->environment_hos, "22.5.0") == 0 &&
        strcmp(model->environment_model, "mariko-oled") == 0 &&
        strcmp(model->environment_atmosphere_version, "1.11.2") == 0;
}

bool ptc_ui_setup_has_issues(const PtcUiModel *model)
{
    return !model || model->setup_issue_mask || !model->status_loaded ||
        !ptc_ui_setup_reference_matches(model) || !model->setup_pin_ready ||
        !model->restriction_enabled_available || !model->restriction_enabled ||
        (model->temporary_unlocked_available && model->temporary_unlocked) ||
        model->disable_flag_present || strcmp(model->setup_phase, "failed") == 0 ||
        strcmp(model->setup_phase, "protection") == 0;
}

bool ptc_ui_parent_read_only_action(const PtcUiModel *model, int index)
{
    return model && model->parent_page == PTC_UI_PARENT_SUPPORT &&
        (index == 4 || index == 5 || index == 6 + model->recent_event_count);
}

bool ptc_ui_setup_diagnostic_json(const PtcUiModel *model, char *out, size_t size)
{
    static const char *const categories[] = {
        "language_save", "pin_setup", "theme_save", "shortcut_save",
        "progress_save", "status_read", "activation", "install_defaults"
    };
    cJSON *root;
    cJSON *issues;
    char *text;
    bool ok;
    if (!model || !out || size == 0) return false;
    root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddBoolToObject(root, "wizard_completed", model->setup_wizard_completed);
    cJSON_AddNumberToObject(root, "step", model->setup_step);
    cJSON_AddBoolToObject(root, "activation_pending", model->setup_activation_pending);
    cJSON_AddBoolToObject(root, "status_loaded", model->status_loaded);
    cJSON_AddBoolToObject(root, "environment_available", model->environment_available);
    cJSON_AddBoolToObject(root, "reference_environment_matches", ptc_ui_setup_reference_matches(model));
    cJSON_AddNumberToObject(root, "parental_control_enabled", model->restriction_enabled_available
        ? (model->restriction_enabled ? 1 : 0) : -1);
    cJSON_AddBoolToObject(root, "pin_ready", model->setup_pin_ready);
    cJSON_AddStringToObject(root, "clock_sync", "not_detected");
    issues = cJSON_AddArrayToObject(root, "issues");
    for (int i = 0; issues && i < PTC_UI_SETUP_ISSUE_COUNT; ++i) {
        if (model->setup_issue_mask & (1U << i)) {
            cJSON *issue = cJSON_CreateObject();
            if (!issue) continue;
            cJSON_AddStringToObject(issue, "category", categories[i]);
            cJSON_AddNumberToObject(issue, "step", model->setup_issue_steps[i]);
            cJSON_AddNumberToObject(issue, "code", model->setup_issue_codes[i]);
            cJSON_AddItemToArray(issues, issue);
        }
    }
    text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = text && strlen(text) < size;
    if (ok) snprintf(out, size, "%s", text);
    free(text);
    return ok;
}

PtcRuleMode ptc_ui_next_rule_mode(PtcRuleMode mode)
{
    switch (mode) {
    case PTC_RULE_MODE_LIMIT:
        return PTC_RULE_MODE_UNLIMITED;
    case PTC_RULE_MODE_UNLIMITED:
    default:
        return PTC_RULE_MODE_LIMIT;
    }
}

bool ptc_ui_day_rule_effectively_changed(PtcDayRule before, PtcDayRule after)
{
    if (before.mode != after.mode) {
        return true;
    }
    return before.mode == PTC_RULE_MODE_LIMIT && before.minutes != after.minutes;
}

bool ptc_ui_weekly_today_changed(const PtcUiModel *model)
{
    uint8_t weekday;
    if (!model) {
        return false;
    }
    weekday = ptc_weekday_from_day_index(model->day_index);
    return ptc_ui_day_rule_effectively_changed(
        model->current_week[weekday], model->draft_week[weekday]);
}

bool ptc_ui_limit_minutes_would_restrict(const PtcUiModel *model, uint16_t minutes)
{
    return model && model->played_minutes_available && model->played_minutes >= 0 &&
        minutes <= (uint16_t)model->played_minutes;
}

bool ptc_ui_today_limit_requires_hold(const PtcUiModel *model, uint16_t minutes)
{
    return model && (!model->played_minutes_available || model->played_minutes < 0 ||
        ptc_ui_limit_minutes_would_restrict(model, minutes));
}

void ptc_ui_format_today_limit_confirmation(
    const PtcUiModel *model,
    char *risk,
    size_t risk_size,
    char *recovery,
    size_t recovery_size)
{
    if (risk && risk_size > 0) {
        if (!model || !model->played_minutes_available || model->played_minutes < 0) {
            snprintf(risk, risk_size,
                     ptc_ui_text(PTC_UI_T_RISK_UNABLE_TO_OBTAIN_QUOTA_CONSUMPTION_ESTIMATE));
        } else if (ptc_ui_limit_minutes_would_restrict(model, model->draft_minutes)) {
            snprintf(risk, risk_size,
                     ptc_ui_text(PTC_UI_T_RISK_THE_NEW_QUOTA_IS_NOT_HIGHER));
        } else if (model->unrestricted_today == 1) {
            snprintf(risk, risk_size,
                     ptc_ui_text(PTC_UI_T_TIP_TODAY_WILL_BE_CHANGED_FROM_NO));
        } else {
            snprintf(risk, risk_size, ptc_ui_text(PTC_UI_T_TIP_PLEASE_CONFIRM_TODAY_S_REAL_TIME));
        }
    }
    if (recovery && recovery_size > 0) {
        snprintf(recovery, recovery_size,
                 ptc_ui_text(PTC_UI_T_TO_LIFT_CHOOSE_NO_LIMIT_TODAY_QUICK_2));
    }
}

bool ptc_ui_day_rule_would_restrict(const PtcUiModel *model, PtcDayRule rule)
{
    return rule.mode == PTC_RULE_MODE_LIMIT && ptc_ui_limit_minutes_would_restrict(model, rule.minutes);
}

bool ptc_ui_setup_takeover_complete(const PtcUiModel *model)
{
    return model &&
        (strcmp(model->setup_phase, "released") == 0 ||
         (strcmp(model->setup_phase, "active") == 0 && !model->disable_flag_present));
}

bool ptc_ui_runtime_fingerprint_reconfirmation_needed(const PtcUiModel *model)
{
    return model && model->disable_flag_present &&
        strcmp(model->setup_phase, "protection") == 0 &&
        strcmp(model->disable_reason, "runtime_fingerprint_changed") == 0;
}

int64_t ptc_ui_setup_grace_remaining(const PtcUiModel *model, int64_t now)
{
    if (!model || strcmp(model->setup_phase, "released") != 0 || model->setup_activate_after <= 0) {
        return -1;
    }
    if (now >= model->setup_activate_after) {
        return 0;
    }
    return model->setup_activate_after - now;
}

bool ptc_ui_cancel_overlay(PtcUiModel *model)
{
    bool clear_pending_code;
    if (!model || model->overlay == PTC_UI_OVERLAY_NONE) {
        return false;
    }
    if (model->overlay == PTC_UI_OVERLAY_CALENDAR_FORMAT) {
        model->overlay = PTC_UI_OVERLAY_CALENDAR_MANAGER;
        snprintf(model->overlay_title, sizeof(model->overlay_title), "%s",
            ptc_ui_text(PTC_UI_T_REGION_CALENDAR_MANAGER));
        model->overlay_body[0] = '\0';
        return true;
    }
    if (model->overlay == PTC_UI_OVERLAY_SCHEDULED && ptc_ui_scheduled_dirty(model)) {
        model->overlay = PTC_UI_OVERLAY_SCHEDULED_LEAVE;
        return true;
    }
    if (model->overlay == PTC_UI_OVERLAY_SCHEDULED_LEAVE) {
        model->overlay = PTC_UI_OVERLAY_SCHEDULED;
        return true;
    }
    clear_pending_code = model->operation == PTC_UI_OPERATION_REDEEM_OFFLINE_CODE ||
        model->overlay == PTC_UI_OVERLAY_CODE_RESULT;
    if (model->overlay == PTC_UI_OVERLAY_PIN) {
        ptc_ui_pin_finish(model);
    } else if (model->overlay == PTC_UI_OVERLAY_NUMPAD ||
        model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR) {
        ptc_ui_numpad_finish(model);
    } else if (model->overlay == PTC_UI_OVERLAY_CONFIRM &&
               model->confirm_return_overlay != PTC_UI_OVERLAY_NONE) {
        bool return_to_today_limit = model->confirm_return_overlay == PTC_UI_OVERLAY_MINUTE_EDITOR &&
            (model->operation == PTC_UI_OPERATION_SET_TODAY_LIMIT ||
             model->operation == PTC_UI_OPERATION_DISABLE_TODAY_LIMIT);
        model->overlay = model->confirm_return_overlay;
        model->confirm_return_overlay = PTC_UI_OVERLAY_NONE;
        snprintf(model->overlay_title, sizeof(model->overlay_title), "%s", model->confirm_return_title);
        snprintf(model->overlay_body, sizeof(model->overlay_body), "%s", model->confirm_return_body);
        model->confirm_return_title[0] = '\0';
        model->confirm_return_body[0] = '\0';
        model->operation = return_to_today_limit
            ? PTC_UI_OPERATION_SET_TODAY_LIMIT : PTC_UI_OPERATION_NONE;
        model->confirm_hold_required = false;
    } else if (model->overlay == PTC_UI_OVERLAY_CREDENTIAL_LEAVE) {
        model->overlay = PTC_UI_OVERLAY_CREDENTIAL;
        snprintf(model->overlay_title, sizeof(model->overlay_title), "%s",
                 model->credential_kind == 1 ? ptc_ui_text(PTC_UI_T_MANAGES_THE_TIME_CODE_DEVICE_NAME) : ptc_ui_text(PTC_UI_T_MANAGE_GRANT_CODE_KEYS));
        snprintf(model->overlay_body, sizeof(model->overlay_body), "%s",
                 model->credential_kind == 1
                    ? ptc_ui_text(PTC_UI_T_THE_CURRENT_VALUE_IS_READ_ONLY_A)
                    : ptc_ui_text(PTC_UI_T_THE_CURRENT_KEY_IS_BLOCKED_BY_DEFAULT));
    } else {
        model->overlay = PTC_UI_OVERLAY_NONE;
        model->confirm_return_overlay = PTC_UI_OVERLAY_NONE;
        model->confirm_return_title[0] = '\0';
        model->confirm_return_body[0] = '\0';
        model->overlay_title[0] = '\0';
        model->overlay_body[0] = '\0';
        model->operation = PTC_UI_OPERATION_NONE;
        model->confirm_hold_required = false;
    }
    if (clear_pending_code) model->pending_code[0] = '\0';
    return true;
}

PtcUiOperation ptc_ui_take_confirmed_operation(PtcUiModel *model)
{
    PtcUiOperation operation;
    if (!model || model->overlay != PTC_UI_OVERLAY_CONFIRM) {
        return PTC_UI_OPERATION_NONE;
    }
    operation = model->operation;
    model->overlay = PTC_UI_OVERLAY_NONE;
    model->confirm_return_overlay = PTC_UI_OVERLAY_NONE;
    model->confirm_return_title[0] = '\0';
    model->confirm_return_body[0] = '\0';
    model->overlay_title[0] = '\0';
    model->overlay_body[0] = '\0';
    model->operation = PTC_UI_OPERATION_NONE;
    model->confirm_hold_required = false;
    return operation;
}

void ptc_ui_set_execution(PtcUiModel *model, const char *command_name, const char *transport_label)
{
    char command_copy[sizeof(model->command_name)];
    char transport_copy[sizeof(model->transport_label)];
    if (!model) {
        return;
    }
    snprintf(
        command_copy,
        sizeof(command_copy),
        "%s",
        command_name && command_name[0] ? command_name : ptc_ui_text(PTC_UI_T_NOT_STARTED));
    snprintf(
        transport_copy,
        sizeof(transport_copy),
        "%s",
        transport_label && transport_label[0] ? transport_label : ptc_ui_text(PTC_UI_T_TRANSFER_NOT_STARTED));
    snprintf(
        model->command_name,
        sizeof(model->command_name),
        "%s",
        command_copy);
    snprintf(
        model->transport_label,
        sizeof(model->transport_label),
        "%s",
        transport_copy);
}

const char *ptc_ui_support_problem(const PtcUiModel *model)
{
    if (model->parent_support_only) return ptc_ui_text(PTC_UI_T_SETUP_READ_ONLY_SUPPORT);
    if (model->waiting || model->apply_pending_confirmation) return ptc_ui_text(PTC_UI_T_CONFIRMING_CURRENT_OPERATION);
    if (model->recovery_active) return ptc_ui_text(PTC_UI_T_THERE_ARE_UNFINISHED_RESTORES_THAT_NEED_TO);
    if (ptc_ui_runtime_fingerprint_reconfirmation_needed(model)) return ptc_ui_text(PTC_UI_T_THE_SYSTEM_ENVIRONMENT_HAS_CHANGED_AND_NEEDS);
    if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0)
        return ptc_ui_text(PTC_UI_T_SECURITY_CHECK_FAILED_CONTROL_NEEDS_TO_BE);
    if (model->disable_flag_present) return ptc_ui_text(PTC_UI_T_CONTROL_IS_DISABLED);
    if (!model->status_loaded) return ptc_ui_text(PTC_UI_T_STATUS_HAS_NOT_BEEN_READ_YET);
    if (model->error_code) return ptc_ui_text(PTC_UI_T_THE_LAST_OPERATION_WAS_NOT_COMPLETED);
    if (model->setup_wizard_completed && model->setup_issue_mask) return ptc_ui_text(PTC_UI_T_SETUP_RECORDED_PROBLEMS);
    if (strcmp(model->setup_phase, "active") != 0) return ptc_ui_text(model->setup_wizard_completed
        ? PTC_UI_T_SETUP_CONTROLS_INACTIVE : PTC_UI_T_FIRST_TIME_SETUP_NOT_COMPLETED_YET);
    return ptc_ui_text(PTC_UI_T_THERE_ARE_CURRENTLY_NO_PENDING_ISSUES);
}

int ptc_ui_support_recommended_action(const PtcUiModel *model)
{
    if (model->waiting || model->apply_pending_confirmation) return -1;
    if (model->parent_support_only || (model->setup_wizard_completed && model->setup_issue_mask)) return 4;
    if (model->recovery_active) {
        return ptc_ui_safety_action_available(model, 1) != PTC_UI_ACTION_DISABLED ? 1 : 4;
    }
    if (ptc_ui_runtime_fingerprint_reconfirmation_needed(model)) return 0;
    if (strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0) return 1;
    if (model->disable_flag_present) return 0;
    if (model->error_code || !model->status_loaded) return 4;
    return ptc_ui_safety_action_available(model, 0) != PTC_UI_ACTION_DISABLED ? 0 : -1;
}

PtcUiActionState ptc_ui_safety_action_available(const PtcUiModel *model, int index)
{
    if (!model) {
        return PTC_UI_ACTION_DISABLED;
    }
    switch (index) {
    case 0:
        return ptc_ui_runtime_fingerprint_reconfirmation_needed(model) || model->disable_flag_present ||
            (strcmp(model->setup_phase, "active") != 0 && strcmp(model->setup_phase, "protection") != 0 &&
             strcmp(model->setup_phase, "failed") != 0)
            ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_DISABLED;
    case 1:
        return strcmp(model->setup_phase, "protection") == 0 || strcmp(model->setup_phase, "failed") == 0 ||
            strcmp(model->setup_phase, "pending") == 0 ? PTC_UI_ACTION_RECOMMENDED : PTC_UI_ACTION_DISABLED;
    case 2:
        return !model->disable_flag_present && strcmp(model->setup_phase, "protection") != 0 &&
            strcmp(model->setup_phase, "restored") != 0
            ? PTC_UI_ACTION_AVAILABLE : PTC_UI_ACTION_DISABLED;
    case 3:
        return model->setup_snapshot_available ? PTC_UI_ACTION_AVAILABLE : PTC_UI_ACTION_DISABLED;
    case 4:
        return PTC_UI_ACTION_AVAILABLE;
    case 5:
        return PTC_UI_ACTION_AVAILABLE;
    default:
        return PTC_UI_ACTION_DISABLED;
    }
}
bool ptc_ui_safety_action_visible(const PtcUiModel *model, int index)
{
    if (!model) return false;
    return index >= 0 && index < 6;
}

const char *ptc_ui_safety_action_hint(const PtcUiModel *model, int index)
{
    if (!model) {
        return "";
    }
    switch (index) {
    case 0:
        return strcmp(model->setup_phase, "active") == 0 ? ptc_ui_text(PTC_UI_T_QUOTA_MANAGEMENT_IS_ENABLED) : ptc_ui_text(PTC_UI_T_AFTER_PASSING_THE_CHECK_SAVE_THE_PRE);
    case 1:
        return strcmp(model->setup_phase, "active") == 0
            ? ptc_ui_text(PTC_UI_T_IS_CURRENTLY_FUNCTIONING_NORMALLY_AND_NO_REPAIR)
            : ptc_ui_text(PTC_UI_T_RECHECK_SYSTEM_COMPATIBILITY_PRE_INSTALLATION_SETUP_AND);
    case 2:
        return model->disable_flag_present
            ? ptc_ui_text(PTC_UI_T_NEW_CONTROL_WRITES_ARE_NOT_ALLOWED_UNTIL)
            : ptc_ui_text(PTC_UI_T_MESSAGE_5);
    case 3:
        return model->setup_snapshot_available ? ptc_ui_text(PTC_UI_T_RESTORE_PRE_INSTALLATION_SETTINGS) : ptc_ui_text(PTC_UI_T_THERE_ARE_NO_RESTOREABLE_PRE_INSTALLATION_SETTINGS);
    case 4:
        return ptc_ui_text(PTC_UI_T_AUTOMATICALLY_EXCLUDE_SECRETS_PINS_OFFLINE_CODES_AND);
    case 5:
        return ptc_ui_text(PTC_UI_T_VIEW_THE_PLAYWISE_VERSION_PROJECT_REPOSITORY_AND);
    default:
        return "";
    }
}
