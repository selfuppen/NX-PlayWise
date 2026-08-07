#include <stdio.h>
#include <string.h>

#include "../../companion/nro/ui_graphics.h"

static int failures;

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

static void check_hit(PtcUiHit hit, PtcUiHitKind kind, int index, const char *label)
{
    if (hit.kind != kind || hit.index != index) {
        fprintf(stderr, "FAIL %s: expected (%d,%d), got (%d,%d)\n",
            label, (int)kind, index, (int)hit.kind, hit.index);
        ++failures;
    }
}

static PtcUiHit hit_center(const PtcUiModel *model, PtcUiRect rect)
{
    return ptc_ui_hit_test(model, rect.x + rect.w / 2, rect.y + rect.h / 2);
}

static void test_release_navigation(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_TODAY), 5, "today actions");
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_PLAN), 1, "weekly plan actions");
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_SECURITY), 4, "security actions");
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_SUPPORT), 5, "support actions");

    model.parent_page = PTC_UI_PARENT_TODAY;
    ptc_ui_change_parent_page(&model, -1);
    check_int(model.parent_page, PTC_UI_PARENT_SUPPORT, "page wraps to support");
    ptc_ui_change_parent_page(&model, 1);
    check_int(model.parent_page, PTC_UI_PARENT_TODAY, "page wraps to today");

    model.parent_page = PTC_UI_PARENT_TODAY;
    model.selected_index = 0;
    ptc_ui_move_parent_selection(&model, 1, 0);
    check_int(model.selected_index, 1, "selection moves right");
    ptc_ui_move_parent_selection(&model, 0, -1);
    check_int(model.selected_index, 4, "five-card selection wraps upward");

    check_int(ptc_ui_next_rule_mode(PTC_RULE_MODE_LIMIT), PTC_RULE_MODE_UNLIMITED, "limit toggles to unlimited");
    check_int(ptc_ui_next_rule_mode(PTC_RULE_MODE_UNLIMITED), PTC_RULE_MODE_LIMIT, "unlimited toggles to limit");
}

static void test_numeric_input(void)
{
    PtcUiModel model;
    uint16_t value = 0;
    memset(&model, 0, sizeof(model));
    ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_MINUTES, PTC_UI_OVERLAY_MINUTES,
        "输入额度", "1 到 1440 分钟", 4, 1, 1440, 60);
    snprintf(model.numpad_text, sizeof(model.numpad_text), "1440");
    check_true(ptc_ui_numpad_validate(&model, &value) && value == 1440, "daily maximum accepted");
    snprintf(model.numpad_text, sizeof(model.numpad_text), "0");
    check_true(!ptc_ui_numpad_validate(&model, &value), "zero-minute limit rejected");

    ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_OFFLINE_CODE, PTC_UI_OVERLAY_NONE,
        "输入加时码", "8 位数字", 8, 0, 0, 0);
    snprintf(model.numpad_text, sizeof(model.numpad_text), "1051468");
    check_true(!ptc_ui_numpad_validate(&model, NULL), "short code rejected");
    snprintf(model.numpad_text, sizeof(model.numpad_text), "10514680");
    check_true(ptc_ui_numpad_validate(&model, NULL), "eight-digit code accepted");
}

static void test_release_hit_targets(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    model.view = PTC_UI_CHILD;
    check_hit(hit_center(&model, ptc_ui_child_submit_rect()), PTC_UI_HIT_CHILD_SUBMIT_CODE, 0, "child code button");
    check_hit(hit_center(&model, ptc_ui_child_refresh_rect()), PTC_UI_HIT_CHILD_REFRESH, 0, "child refresh button");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(0)), PTC_UI_HIT_NONE, 0, "parent controls hidden from child");

    model.view = PTC_UI_PARENT;
    model.parent_page = PTC_UI_PARENT_SUPPORT;
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(0)), PTC_UI_HIT_PARENT_CARD, 0, "support environment card");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(4)), PTC_UI_HIT_PARENT_CARD, 4, "support diagnostics card");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(5)), PTC_UI_HIT_NONE, 0, "removed probe card is absent");

    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    model.operation = PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT;
    check_hit(hit_center(&model, ptc_ui_confirm_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CONFIRM, 0, "recovery confirmation");
    check_int(ptc_ui_take_confirmed_operation(&model), PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT,
        "recovery action confirmed once");
    check_int(ptc_ui_take_confirmed_operation(&model), PTC_UI_OPERATION_NONE, "confirmation cannot be reused");
}

static void test_user_state_mapping(void)
{
    PtcUiModel model;
    const char *pending =
        "{\"version\":1,\"request_id\":\"setup\",\"type\":\"complete_setup\",\"status\":\"ok\","
        "\"state\":{\"day_index\":1,\"limited_today\":0,\"blocked_today\":0,\"unrestricted_today\":1,"
        "\"remaining_available\":false,\"remaining_minutes\":-1,\"played_minutes_available\":true,"
        "\"played_minutes\":20,\"play_timer_enabled\":1,\"restricted_now\":0},"
        "\"setup\":{\"phase\":\"released\",\"restriction_cleared\":true,\"snapshot_available\":true,"
        "\"activate_after\":105,\"compatibility_status\":\"verified\"},\"completed_at\":100}";
    const char *active =
        "{\"version\":1,\"request_id\":\"status\",\"type\":\"status\",\"status\":\"ok\","
        "\"state\":{\"day_index\":1,\"limited_today\":1,\"blocked_today\":0,\"unrestricted_today\":0,"
        "\"remaining_available\":true,\"remaining_minutes\":40,\"played_minutes_available\":true,"
        "\"played_minutes\":20,\"play_timer_enabled\":1,\"restricted_now\":0},"
        "\"setup\":{\"phase\":\"active\",\"restriction_cleared\":true,\"snapshot_available\":true,"
        "\"activate_after\":0,\"compatibility_status\":\"verified\"},\"completed_at\":106}";
    const char *protection =
        "{\"version\":1,\"request_id\":\"status-error\",\"type\":\"status\",\"status\":\"error\","
        "\"error\":{\"code\":312,\"reason\":\"protection_mode\",\"message\":\"已进入保护模式\"},"
        "\"state\":{\"day_index\":1,\"limited_today\":-1,\"blocked_today\":-1,\"unrestricted_today\":-1,"
        "\"remaining_available\":false,\"remaining_minutes\":-1,\"played_minutes_available\":false,"
        "\"played_minutes\":-1,\"play_timer_enabled\":-1,\"restricted_now\":-1},\"completed_at\":107}";

    memset(&model, 0, sizeof(model));
    check_true(ptc_ui_apply_result_json(&model, pending), "syncing result parses");
    check_int(model.view, PTC_UI_SETUP, "syncing result opens setup status");
    check_true(strcmp(model.setup_phase, "released") == 0, "syncing phase mapped");
    check_int((int)ptc_ui_setup_grace_remaining(&model, 100), 5, "sync countdown mapped");
    check_true(ptc_ui_apply_result_json(&model, active), "active result parses");
    check_int(model.view, PTC_UI_CHILD, "active result opens child home");
    check_true(strcmp(model.mode, "额度管理") == 0, "release product language is stable");
    check_int(model.remaining_minutes, 40, "remaining minutes mapped");
    check_true(ptc_ui_apply_result_json(&model, protection), "protection result parses");
    check_true(strstr(model.message, "保护模式") != NULL, "protection guidance is visible");
}

int main(void)
{
    test_release_navigation();
    test_numeric_input();
    test_release_hit_targets();
    test_user_state_mapping();
    if (failures) return 1;
    puts("PTC release UI state tests passed");
    return 0;
}
