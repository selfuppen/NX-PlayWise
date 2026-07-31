#include <stdio.h>
#include <string.h>

#include "../../companion/nro/ui_graphics.h"

static int failures = 0;

static void check_int(int actual, int expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL %s: expected %d, got %d\n", label, expected, actual);
        ++failures;
    }
}

static void check_true(bool value, const char *label)
{
    if (!value) {
        fprintf(stderr, "FAIL %s\n", label);
        ++failures;
    }
}

static void test_navigation(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    model.parent_page = PTC_UI_PARENT_TODAY;
    ptc_ui_change_parent_page(&model, 1);
    check_int(model.parent_page, PTC_UI_PARENT_PLAN, "next parent page");
    ptc_ui_change_parent_page(&model, -1);
    check_int(model.parent_page, PTC_UI_PARENT_TODAY, "previous parent page");
    ptc_ui_change_parent_page(&model, -1);
    check_int(model.parent_page, PTC_UI_PARENT_SAFETY, "parent page wraps");

    model.selected_index = 0;
    ptc_ui_move_parent_selection(&model, 1, 0);
    check_int(model.selected_index, 1, "move right");
    ptc_ui_move_parent_selection(&model, 0, -1);
    check_int(model.selected_index, 4, "odd grid wraps up to final card");
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 0, "odd grid wraps down from final card");

    model.parent_page = PTC_UI_PARENT_PLAN;
    model.selected_index = 3;
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 4, "five-card grid uses final card");
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 0, "five-card grid wraps down");
}

static void test_editors(void)
{
    check_int(ptc_ui_adjust_minutes(5, -15, 5, 120), 5, "minutes minimum");
    check_int(ptc_ui_adjust_minutes(115, 15, 5, 120), 120, "minutes maximum");
    check_int(ptc_ui_adjust_minute_of_day(0, -15), 1425, "clock wraps backward");
    check_int(ptc_ui_adjust_minute_of_day(1430, 15), 5, "clock wraps forward");
    check_int(ptc_ui_next_rule_mode(PTC_RULE_MODE_LIMIT), PTC_RULE_MODE_UNLIMITED, "rule mode unlimited");
    check_int(ptc_ui_next_rule_mode(PTC_RULE_MODE_UNLIMITED), PTC_RULE_MODE_BLOCKED, "rule mode blocked");
    check_int(ptc_ui_next_rule_mode(PTC_RULE_MODE_BLOCKED), PTC_RULE_MODE_LIMIT, "rule mode wraps");
    check_int(ptc_ui_shift_limit_action(PTC_LIMIT_ACTION_REMIND, -1), PTC_LIMIT_ACTION_SUSPEND, "limit action wraps");
}

static void test_overlay_confirmation(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    model.operation = PTC_UI_OPERATION_QUICK_TEST;
    check_true(ptc_ui_cancel_overlay(&model), "confirmation can be cancelled");
    check_int(model.overlay, PTC_UI_OVERLAY_NONE, "cancel closes overlay");
    check_int(ptc_ui_take_confirmed_operation(&model), PTC_UI_OPERATION_NONE, "cancel does not confirm operation");

    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    model.operation = PTC_UI_OPERATION_EMERGENCY_DISABLE;
    check_int(
        ptc_ui_take_confirmed_operation(&model),
        PTC_UI_OPERATION_EMERGENCY_DISABLE,
        "danger operation requires confirmation overlay");
    check_int(model.operation, PTC_UI_OPERATION_NONE, "confirmed operation is consumed once");
    check_int(ptc_ui_take_confirmed_operation(&model), PTC_UI_OPERATION_NONE, "confirmation cannot be reused");
}

static void test_result_mapping(void)
{
    PtcUiModel model;
    const char *success =
        "{\"version\":1,\"request_id\":\"1-a\",\"type\":\"status\",\"status\":\"ok\","
        "\"mode\":\"observe\",\"dry_run\":true,\"state\":{\"limited_today\":1,\"blocked_today\":0,"
        "\"unrestricted_today\":0,\"remaining_available\":true,\"remaining_minutes\":42,"
        "\"play_timer_enabled\":1,\"restricted_now\":0,\"bedtime_active\":false,"
        "\"parent_unlock_active\":true},\"capabilities\":{\"play_timer_write_verified\":true,"
        "\"play_timer_effect_verified\":true,\"raw_block_verified\":false,\"suspend_verified\":false}}";
    const char *failure =
        "{\"type\":\"offline_code\",\"status\":\"error\",\"mode\":\"grant\","
        "\"dry_run\":false,\"error\":{\"message\":\"授权码签名不匹配\"}}";
    memset(&model, 0, sizeof(model));
    check_true(ptc_ui_apply_result_json(&model, success), "success result parses");
    check_true(model.status_loaded, "result status loaded");
    check_int(model.remaining_minutes, 42, "remaining minutes mapped");
    check_true(model.parent_unlock_active, "unlock state mapped");
    check_true(model.play_timer_effect_verified, "capability mapped");
    check_true(strcmp(model.mode, "观察") == 0, "mode localized");
    check_true(ptc_ui_apply_result_json(&model, failure), "error result parses");
    check_true(strcmp(model.result_status, "error") == 0, "error status mapped");
    check_true(strstr(model.message, "签名") != NULL, "error message preserved");
    check_int(model.remaining_minutes, 42, "error without state preserves dashboard");
    check_true(ptc_ui_apply_result_json(&model, "{\"type\":\"status\",\"status\":\"ok\",\"mode\":\"future\"}"), "unknown mode result parses");
    check_true(strcmp(model.mode, "未知模式") == 0, "unknown mode stays localized");
    check_true(!ptc_ui_apply_result_json(&model, "{"), "bad result rejected");
    check_true(!ptc_ui_apply_result_json(&model, "[]"), "non-object result rejected");
    check_true(!ptc_ui_apply_result_json(&model, "{}"), "missing status rejected");
}

int main(void)
{
    test_navigation();
    test_editors();
    test_overlay_confirmation();
    test_result_mapping();
    if (failures != 0) {
        return 1;
    }
    puts("PTC UI state tests passed");
    return 0;
}
