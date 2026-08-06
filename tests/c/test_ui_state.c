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

    model.parent_page = PTC_UI_PARENT_TODAY;
    model.selected_index = 0;
    ptc_ui_move_parent_selection(&model, 1, 0);
    check_int(model.selected_index, 1, "move right");
    ptc_ui_move_parent_selection(&model, 0, -1);
    check_int(model.selected_index, 5, "six-card grid wraps up in the same column");
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 1, "six-card grid wraps down in the same column");

    model.parent_page = PTC_UI_PARENT_SAFETY;
    model.selected_index = 5;
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 6, "seven-card grid uses final card");
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 0, "seven-card grid wraps down");

    model.parent_page = PTC_UI_PARENT_PLAN;
    model.selected_index = 3;
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 4, "five-card grid uses final card");
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 0, "five-card grid wraps down");
}

static void test_editors(void)
{
    PtcUiModel model;
    uint16_t parsed = 0;
    memset(&model, 0, sizeof(model));
    check_int(ptc_ui_adjust_minutes(5, -15, 5, 120), 5, "minutes minimum");
    check_int(ptc_ui_adjust_minutes(115, 15, 5, 120), 120, "minutes maximum");
    check_int(ptc_ui_adjust_minute_of_day(0, -15), 1425, "clock wraps backward");
    check_int(ptc_ui_adjust_minute_of_day(1430, 15), 5, "clock wraps forward");
    check_int(ptc_ui_next_rule_mode(PTC_RULE_MODE_LIMIT), PTC_RULE_MODE_UNLIMITED, "rule mode unlimited");
    check_int(ptc_ui_next_rule_mode(PTC_RULE_MODE_UNLIMITED), PTC_RULE_MODE_BLOCKED, "rule mode blocked");
    check_int(ptc_ui_next_rule_mode(PTC_RULE_MODE_BLOCKED), PTC_RULE_MODE_LIMIT, "rule mode wraps");
    check_int(ptc_ui_shift_limit_action(PTC_LIMIT_ACTION_REMIND, -1), PTC_LIMIT_ACTION_SUSPEND, "limit action wraps");
    check_true(ptc_ui_parse_minutes("1", 1, 1440, &parsed) && parsed == 1, "exact minute minimum parses");
    check_true(ptc_ui_parse_minutes("1440", 1, 1440, &parsed) && parsed == 1440, "exact minute maximum parses");
    check_true(!ptc_ui_parse_minutes("0", 1, 1440, &parsed), "exact minute below range rejected");
    check_true(!ptc_ui_parse_minutes("15m", 1, 120, &parsed), "exact minute non-digit rejected");

    model.played_minutes_available = true;
    model.played_minutes = 45;
    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    model.operation = PTC_UI_OPERATION_EMERGENCY_DISABLE;
    check_int(
        ptc_ui_take_confirmed_operation(&model),
        PTC_UI_OPERATION_EMERGENCY_DISABLE,
        "danger operation requires confirmation overlay");
    check_int(model.operation, PTC_UI_OPERATION_NONE, "confirmed operation is consumed once");
    check_int(ptc_ui_take_confirmed_operation(&model), PTC_UI_OPERATION_NONE, "confirmation cannot be reused");

    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    model.operation = PTC_UI_OPERATION_DISABLE_TODAY_LIMIT;
    check_int(
        ptc_ui_take_confirmed_operation(&model),
        PTC_UI_OPERATION_DISABLE_TODAY_LIMIT,
        "release current restriction requires confirmation");

    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    model.operation = PTC_UI_OPERATION_RESUME_CONTROL;
    check_int(
        ptc_ui_take_confirmed_operation(&model),
        PTC_UI_OPERATION_RESUME_CONTROL,
        "resume control requires confirmation");
}

static void test_numpad(void)
{
    PtcUiModel model;
    uint16_t value = 0;
    int i;
    memset(&model, 0, sizeof(model));
    model.operation = PTC_UI_OPERATION_SET_TODAY_LIMIT;
    ptc_ui_numpad_open(
        &model, PTC_UI_NUMPAD_MINUTES, PTC_UI_OVERLAY_MINUTES,
        "输入额度", "输入范围", 4, 1, 1440, 60);
    check_int(model.overlay, PTC_UI_OVERLAY_NUMPAD, "numpad opens as modal overlay");
    check_true(model.numpad_text[0] == '\0', "numpad starts empty");
    check_int(model.numpad_current, 60, "numpad preserves current value as guidance");

    ptc_ui_numpad_move(&model, -1, 0);
    check_int(model.numpad_cursor, 2, "numpad wraps left within row");
    ptc_ui_numpad_move(&model, 0, -1);
    check_int(model.numpad_cursor, 11, "numpad wraps up to bottom row");
    ptc_ui_numpad_activate(&model);
    check_true(model.numpad_text[0] == '\0', "numpad clear key activates without adding a digit");
    model.numpad_cursor = 0;
    ptc_ui_numpad_activate(&model);
    model.numpad_cursor = 1;
    ptc_ui_numpad_activate(&model);
    check_true(strcmp(model.numpad_text, "12") == 0, "numpad appends selected digits");
    ptc_ui_numpad_backspace(&model);
    check_true(strcmp(model.numpad_text, "1") == 0, "numpad backspace removes one digit");
    ptc_ui_numpad_clear(&model);
    check_true(model.numpad_text[0] == '\0', "numpad clear removes all digits");

    snprintf(model.numpad_text, sizeof(model.numpad_text), "1440");
    check_true(ptc_ui_numpad_validate(&model, &value) && value == 1440, "numpad accepts minute maximum");
    snprintf(model.numpad_text, sizeof(model.numpad_text), "0");
    check_true(!ptc_ui_numpad_validate(&model, &value), "numpad rejects minute below range");
    check_true(model.numpad_error[0] != '\0', "numpad keeps validation feedback visible");

    check_true(ptc_ui_cancel_overlay(&model), "nested numpad can be cancelled");
    check_int(model.overlay, PTC_UI_OVERLAY_MINUTES, "numpad cancel returns to prior editor");
    check_int(model.operation, PTC_UI_OPERATION_SET_TODAY_LIMIT, "numpad cancel preserves editor operation");

    ptc_ui_numpad_open(
        &model, PTC_UI_NUMPAD_BEDTIME, PTC_UI_OVERLAY_BEDTIME,
        "输入时间", "HHMM", 4, 0, 1439, 1260);
    snprintf(model.numpad_text, sizeof(model.numpad_text), "2130");
    check_true(ptc_ui_numpad_validate(&model, &value) && value == 1290, "numpad parses valid HHMM");
    snprintf(model.numpad_text, sizeof(model.numpad_text), "2460");
    check_true(!ptc_ui_numpad_validate(&model, &value), "numpad rejects invalid HHMM");
    snprintf(model.numpad_text, sizeof(model.numpad_text), "930");
    check_true(!ptc_ui_numpad_validate(&model, &value), "numpad requires four time digits");

    ptc_ui_numpad_open(
        &model, PTC_UI_NUMPAD_OFFLINE_CODE, PTC_UI_OVERLAY_NONE,
        "输入加时码", "8 位", 8, 0, 0, 0);
    snprintf(model.numpad_text, sizeof(model.numpad_text), "1234567");
    check_true(!ptc_ui_numpad_validate(&model, NULL), "numpad rejects short offline code");
    snprintf(model.numpad_text, sizeof(model.numpad_text), "12345678");
    check_true(ptc_ui_numpad_validate(&model, NULL), "numpad accepts eight-digit offline code");
    for (i = 0; i < 3; ++i) {
        model.numpad_cursor = i;
        ptc_ui_numpad_activate(&model);
    }
    check_true(strcmp(model.numpad_text, "12345678") == 0, "numpad never exceeds configured maximum length");
}

static void check_hit(PtcUiHit hit, PtcUiHitKind kind, int index, const char *label)
{
    if (hit.kind != kind || hit.index != index) {
        fprintf(stderr, "FAIL %s: expected (%d,%d), got (%d,%d)\n", label, (int)kind, index, (int)hit.kind, hit.index);
        ++failures;
    }
}

static PtcUiHit hit_center(const PtcUiModel *model, PtcUiRect rect)
{
    return ptc_ui_hit_test(model, rect.x + rect.w / 2, rect.y + rect.h / 2);
}

static void test_page_action_counts(void)
{
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_TODAY), 6, "today card count");
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_PLAN), 5, "plan card count");
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_SAFETY), 7, "safety card count includes setup recovery, probes and secret edit");
}

static void test_hit_test_child(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    model.view = PTC_UI_CHILD;
    check_hit(hit_center(&model, ptc_ui_child_submit_rect()), PTC_UI_HIT_CHILD_SUBMIT_CODE, 0, "child submit button");
    check_hit(hit_center(&model, ptc_ui_child_refresh_rect()), PTC_UI_HIT_CHILD_REFRESH, 0, "child refresh area");
    check_hit(hit_center(&model, ptc_ui_child_footer_rect(0)), PTC_UI_HIT_CHILD_SUBMIT_CODE, 0, "child footer submit");
    check_hit(hit_center(&model, ptc_ui_child_footer_rect(1)), PTC_UI_HIT_CHILD_REFRESH, 0, "child footer refresh");
    check_hit(hit_center(&model, ptc_ui_child_footer_rect(2)), PTC_UI_HIT_CHILD_EXIT, 0, "child footer exit");
    check_hit(ptc_ui_hit_test(&model, 8, 8), PTC_UI_HIT_NONE, 0, "child empty space");
    /* The parent area stays hidden behind the button combo; touch must not reach it. */
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(0)), PTC_UI_HIT_NONE, 0, "child view hides parent cards");
    check_hit(hit_center(&model, ptc_ui_parent_tab_rect(1)), PTC_UI_HIT_NONE, 0, "child view hides parent tabs");
}

static void test_hit_test_error(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    model.view = PTC_UI_ERROR;
    check_hit(hit_center(&model, ptc_ui_error_retry_rect()), PTC_UI_HIT_ERROR_RETRY, 0, "error retry button");
    check_hit(hit_center(&model, ptc_ui_error_back_rect()), PTC_UI_HIT_ERROR_BACK, 0, "error back button");
    check_hit(ptc_ui_hit_test(&model, 8, 8), PTC_UI_HIT_NONE, 0, "error empty space");
    check_hit(hit_center(&model, ptc_ui_child_submit_rect()), PTC_UI_HIT_NONE, 0, "error page blocks child actions");
}

static void test_hit_test_parent(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    model.view = PTC_UI_PARENT;
    model.parent_page = PTC_UI_PARENT_TODAY;
    check_hit(hit_center(&model, ptc_ui_parent_tab_rect(0)), PTC_UI_HIT_PARENT_TAB, 0, "first tab");
    check_hit(hit_center(&model, ptc_ui_parent_tab_rect(2)), PTC_UI_HIT_PARENT_TAB, 2, "last tab");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(0)), PTC_UI_HIT_PARENT_CARD, 0, "first card");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(5)), PTC_UI_HIT_PARENT_CARD, 5, "sixth card on today page");
    check_hit(hit_center(&model, ptc_ui_parent_footer_rect(0)), PTC_UI_HIT_PARENT_PREV_PAGE, 0, "parent previous page footer");
    check_hit(hit_center(&model, ptc_ui_parent_footer_rect(1)), PTC_UI_HIT_PARENT_NEXT_PAGE, 0, "parent next page footer");
    check_hit(hit_center(&model, ptc_ui_parent_footer_rect(2)), PTC_UI_HIT_PARENT_REFRESH, 0, "parent refresh footer");
    check_hit(hit_center(&model, ptc_ui_parent_footer_rect(3)), PTC_UI_HIT_PARENT_BACK, 0, "parent back footer");
    check_hit(ptc_ui_hit_test(&model, 8, 640), PTC_UI_HIT_NONE, 0, "parent empty space");

    model.parent_page = PTC_UI_PARENT_SAFETY;
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(5)), PTC_UI_HIT_PARENT_CARD, 5, "suspend probe card is reachable");

    model.parent_page = PTC_UI_PARENT_PLAN;
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(5)), PTC_UI_HIT_NONE, 0, "card beyond page count is inert");
}

static void test_hit_test_overlays(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    model.view = PTC_UI_PARENT;
    model.parent_page = PTC_UI_PARENT_TODAY;

    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    check_hit(hit_center(&model, ptc_ui_confirm_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CONFIRM, 0, "confirm button");
    check_hit(hit_center(&model, ptc_ui_cancel_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CANCEL, 0, "cancel button");
    /* Overlays are modal: a tap over a card underneath must not fall through. */
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(1)), PTC_UI_HIT_NONE, 0, "overlay blocks card underneath");

    model.overlay = PTC_UI_OVERLAY_MINUTES;
    check_hit(hit_center(&model, ptc_ui_minutes_dec_rect()), PTC_UI_HIT_MINUTES_DEC, 0, "minutes decrement");
    check_hit(hit_center(&model, ptc_ui_minutes_inc_rect()), PTC_UI_HIT_MINUTES_INC, 0, "minutes increment");
    check_hit(hit_center(&model, ptc_ui_minutes_dec_large_rect()), PTC_UI_HIT_MINUTES_DEC_LARGE, 0, "minutes large decrement");
    check_hit(hit_center(&model, ptc_ui_minutes_inc_large_rect()), PTC_UI_HIT_MINUTES_INC_LARGE, 0, "minutes large increment");
    check_hit(hit_center(&model, ptc_ui_minutes_value_rect()), PTC_UI_HIT_MINUTES_VALUE, 0, "minutes exact input");
    check_hit(hit_center(&model, ptc_ui_confirm_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CONFIRM, 0, "minutes confirm");

    model.overlay = PTC_UI_OVERLAY_WEEKLY;
    model.editor_index = 0;
    model.draft_week[0].mode = PTC_RULE_MODE_LIMIT;
    check_hit(hit_center(&model, ptc_ui_weekly_day_rect(0)), PTC_UI_HIT_WEEKLY_DAY, 0, "weekly first day");
    check_hit(hit_center(&model, ptc_ui_weekly_day_rect(6)), PTC_UI_HIT_WEEKLY_DAY, 6, "weekly last day");
    check_hit(hit_center(&model, ptc_ui_weekly_day_minutes_rect(0)), PTC_UI_HIT_WEEKLY_MIN_INPUT, 0, "weekly displayed minutes open input");
    check_hit(hit_center(&model, ptc_ui_weekly_mode_rect()), PTC_UI_HIT_WEEKLY_MODE, 0, "weekly mode toggle");
    check_hit(hit_center(&model, ptc_ui_weekly_min_up_rect()), PTC_UI_HIT_WEEKLY_MIN_UP, 0, "weekly minutes up");
    check_hit(hit_center(&model, ptc_ui_weekly_min_down_rect()), PTC_UI_HIT_WEEKLY_MIN_DOWN, 0, "weekly minutes down");
    check_hit(hit_center(&model, ptc_ui_weekly_min_dec_rect()), PTC_UI_HIT_WEEKLY_MIN_DEC, 0, "weekly minutes small decrement");
    check_hit(hit_center(&model, ptc_ui_weekly_min_inc_rect()), PTC_UI_HIT_WEEKLY_MIN_INC, 0, "weekly minutes small increment");
    check_hit(hit_center(&model, ptc_ui_weekly_min_input_rect()), PTC_UI_HIT_WEEKLY_MIN_INPUT, 0, "weekly exact minutes");

    model.overlay = PTC_UI_OVERLAY_BEDTIME;
    model.editor_index = 1;
    model.draft_bedtime.enabled = true;
    check_hit(hit_center(&model, ptc_ui_bedtime_field_rect(0)), PTC_UI_HIT_BEDTIME_FIELD, 0, "bedtime enable field");
    check_hit(hit_center(&model, ptc_ui_bedtime_field_rect(2)), PTC_UI_HIT_BEDTIME_FIELD, 2, "bedtime end field");
    check_hit(hit_center(&model, ptc_ui_bedtime_adj_up_rect(1)), PTC_UI_HIT_BEDTIME_ADJ_UP, 1, "bedtime start step up");
    check_hit(hit_center(&model, ptc_ui_bedtime_adj_down_rect(2)), PTC_UI_HIT_BEDTIME_ADJ_DOWN, 2, "bedtime end step down");

    model.overlay = PTC_UI_OVERLAY_LIMIT_ACTION;
    check_hit(hit_center(&model, ptc_ui_limit_option_rect(0)), PTC_UI_HIT_LIMIT_ACTION_OPTION, 0, "limit action remind");
    check_hit(hit_center(&model, ptc_ui_limit_option_rect(2)), PTC_UI_HIT_LIMIT_ACTION_OPTION, 2, "limit action suspend");

    model.overlay = PTC_UI_OVERLAY_NUMPAD;
    check_hit(hit_center(&model, ptc_ui_numpad_key_rect(0)), PTC_UI_HIT_NUMPAD_KEY, 0, "numpad first digit");
    check_hit(hit_center(&model, ptc_ui_numpad_key_rect(9)), PTC_UI_HIT_NUMPAD_KEY, 9, "numpad backspace key");
    check_hit(hit_center(&model, ptc_ui_numpad_key_rect(11)), PTC_UI_HIT_NUMPAD_KEY, 11, "numpad clear key");
    check_hit(hit_center(&model, ptc_ui_confirm_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CONFIRM, 0, "numpad confirm button");
    check_hit(hit_center(&model, ptc_ui_cancel_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CANCEL, 0, "numpad cancel button");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(0)), PTC_UI_HIT_NONE, 0, "numpad blocks parent card underneath");
}

static void test_probe_confirmation(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    model.operation = PTC_UI_OPERATION_PROBE_RAW_BLOCK;
    check_int(
        ptc_ui_take_confirmed_operation(&model),
        PTC_UI_OPERATION_PROBE_RAW_BLOCK,
        "raw block probe requires confirmation");
    check_int(ptc_ui_take_confirmed_operation(&model), PTC_UI_OPERATION_NONE, "raw block probe consumed once");

    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    model.operation = PTC_UI_OPERATION_PROBE_SUSPEND;
    check_true(ptc_ui_cancel_overlay(&model), "suspend probe can be cancelled");
    check_int(ptc_ui_take_confirmed_operation(&model), PTC_UI_OPERATION_NONE, "cancelled suspend probe does not run");
}

static void test_execution_state(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    ptc_ui_set_execution(&model, NULL, NULL);
    check_true(strcmp(model.command_name, "未开始") == 0, "execution state defaults command label");
    check_true(strcmp(model.transport_label, "传输：未开始") == 0, "execution state defaults transport label");
    ptc_ui_set_execution(&model, "刷新状态", "传输：IPC");
    check_true(strcmp(model.command_name, "刷新状态") == 0, "execution state records request command");
    check_true(strcmp(model.transport_label, "传输：IPC") == 0, "execution state records IPC route");
    ptc_ui_set_execution(&model, "紧急停用控制", "执行方式：本地 SD 标志文件");
    check_true(strcmp(model.command_name, "紧急停用控制") == 0, "local operation replaces request command");
    check_true(strcmp(model.transport_label, "执行方式：本地 SD 标志文件") == 0,
        "local operation replaces prior transport route");
}

static void test_setup_grace_countdown(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    snprintf(model.setup_phase, sizeof(model.setup_phase), "released");
    model.setup_activate_after = 105;
    check_int(ptc_ui_setup_grace_remaining(&model, 100), 5, "setup grace reports remaining seconds");
    check_int(ptc_ui_setup_grace_remaining(&model, 105), 0, "setup grace reaches zero at deadline");
    check_int(ptc_ui_setup_grace_remaining(&model, 115), 0, "setup grace stays at zero after deadline");
    snprintf(model.setup_phase, sizeof(model.setup_phase), "active");
    check_int(ptc_ui_setup_grace_remaining(&model, 100), -1, "active setup has no countdown");
    check_int(ptc_ui_setup_grace_remaining(NULL, 100), -1, "missing setup model has no countdown");
}

static void test_result_mapping(void)
{
    PtcUiModel model;
    const char *success =
        "{\"version\":1,\"request_id\":\"1-a\",\"type\":\"status\",\"status\":\"ok\","
        "\"mode\":\"enforce\",\"dry_run\":false,\"state\":{\"day_index\":1,\"limited_today\":1,\"blocked_today\":0,"
        "\"unrestricted_today\":0,\"remaining_available\":true,\"remaining_minutes\":42,"
        "\"played_minutes_available\":true,\"played_minutes\":18,"
        "\"play_timer_enabled\":1,\"restricted_now\":0,\"bedtime_active\":false,"
        "\"parent_unlock_active\":true},\"capabilities\":{\"raw_block_verified\":false,\"suspend_verified\":false},"
        "\"setup\":{\"phase\":\"released\",\"restriction_cleared\":true,\"snapshot_available\":true,"
        "\"activate_after\":0,\"last_error\":\"\"},\"completed_at\":1}";
    const char *failure =
        "{\"version\":1,\"request_id\":\"1-b\",\"type\":\"offline_code\",\"status\":\"error\",\"mode\":\"grant\","
        "\"dry_run\":false,\"error\":{\"code\":203,\"reason\":\"bad_signature\",\"message\":\"授权码签名不匹配\"},"
        "\"state\":{\"day_index\":1,\"limited_today\":-1,\"blocked_today\":-1,\"unrestricted_today\":-1,"
        "\"remaining_available\":false,\"remaining_minutes\":-1,\"play_timer_enabled\":-1,\"restricted_now\":-1,"
        "\"bedtime_active\":false,\"parent_unlock_active\":false},\"capabilities\":{\"raw_block_verified\":false,"
        "\"suspend_verified\":false},\"completed_at\":1}";
    const char *bedtime_grant =
        "{\"version\":1,\"request_id\":\"1-bt\",\"type\":\"set_bedtime\",\"status\":\"ok\",\"mode\":\"grant\","
        "\"dry_run\":false,\"state\":{\"day_index\":1,\"limited_today\":1,\"blocked_today\":0,\"unrestricted_today\":0,"
        "\"remaining_available\":true,\"remaining_minutes\":42,\"play_timer_enabled\":1,\"restricted_now\":0,"
        "\"bedtime_active\":false,\"parent_unlock_active\":false},\"capabilities\":{\"raw_block_verified\":true,"
        "\"suspend_verified\":false},\"completed_at\":1}";
    const char *unlock_end =
        "{\"version\":1,\"request_id\":\"1-ue\",\"type\":\"parent_unlock_end\",\"status\":\"ok\",\"mode\":\"grant\","
        "\"dry_run\":false,\"state\":{\"day_index\":1,\"limited_today\":1,\"blocked_today\":0,\"unrestricted_today\":0,"
        "\"remaining_available\":true,\"remaining_minutes\":42,\"play_timer_enabled\":1,\"restricted_now\":0,"
        "\"bedtime_active\":false,\"parent_unlock_active\":false},\"capabilities\":{\"raw_block_verified\":true,"
        "\"suspend_verified\":false},\"completed_at\":1}";
    const char *future =
        "{\"version\":1,\"request_id\":\"1-c\",\"type\":\"status\",\"status\":\"ok\",\"mode\":\"future\",\"dry_run\":true,"
        "\"state\":{\"day_index\":1,\"limited_today\":-1,\"blocked_today\":-1,\"unrestricted_today\":-1,\"remaining_available\":false,"
        "\"remaining_minutes\":-1,\"play_timer_enabled\":-1,\"restricted_now\":-1,\"bedtime_active\":false,\"parent_unlock_active\":false},"
        "\"capabilities\":{\"raw_block_verified\":false,\"suspend_verified\":false},\"completed_at\":1}";
    const char *complete_setup =
        "{\"version\":1,\"request_id\":\"1-setup\",\"type\":\"complete_setup\",\"status\":\"ok\","
        "\"mode\":\"enforce\",\"dry_run\":false,\"state\":{\"day_index\":1,\"limited_today\":0,"
        "\"blocked_today\":0,\"unrestricted_today\":1,\"remaining_available\":false,\"remaining_minutes\":-1,"
        "\"play_timer_enabled\":1,\"restricted_now\":0,\"bedtime_active\":false,\"parent_unlock_active\":false},"
        "\"capabilities\":{\"raw_block_verified\":false,\"suspend_verified\":false},"
        "\"setup\":{\"phase\":\"released\",\"restriction_cleared\":true,\"snapshot_available\":true,"
        "\"activate_after\":105,\"last_error\":\"\"},\"completed_at\":100}";
    const char *active_setup =
        "{\"version\":1,\"request_id\":\"1-active\",\"type\":\"status\",\"status\":\"ok\","
        "\"mode\":\"enforce\",\"dry_run\":false,\"state\":{\"day_index\":1,\"limited_today\":1,"
        "\"blocked_today\":0,\"unrestricted_today\":0,\"remaining_available\":true,\"remaining_minutes\":60,"
        "\"play_timer_enabled\":1,\"restricted_now\":0,\"bedtime_active\":false,\"parent_unlock_active\":false},"
        "\"capabilities\":{\"raw_block_verified\":false,\"suspend_verified\":false},"
        "\"setup\":{\"phase\":\"active\",\"restriction_cleared\":true,\"snapshot_available\":true,"
        "\"activate_after\":0,\"last_error\":\"\"},\"completed_at\":106}";
    memset(&model, 0, sizeof(model));
    check_true(ptc_ui_apply_result_json(&model, success), "success result parses");
    check_true(model.status_loaded, "result status loaded");
    check_int(model.remaining_minutes, 42, "remaining minutes mapped");
    check_int(model.day_index, 1, "day index mapped for today's weekly rule");
    check_int(model.played_minutes, 18, "played minutes mapped");
    check_true(model.played_minutes_available, "played minutes availability mapped");
    check_true(model.parent_unlock_active, "unlock state mapped");
    check_true(strcmp(model.mode, "强制执行") == 0, "mode localized");
    check_true(strcmp(model.setup_phase, "released") == 0, "setup phase mapped");
    check_true(model.setup_restriction_cleared, "setup release mapped");
    check_true(model.setup_snapshot_available, "setup snapshot mapped");
    check_int(model.view, PTC_UI_SETUP, "released setup opens onboarding view");
    check_true(ptc_ui_apply_result_json(&model, bedtime_grant), "grant bedtime result parses");
    check_true(strstr(model.message, "不会自动执行") != NULL, "grant bedtime result explains automatic enforcement boundary");
    check_int(model.played_minutes, 18, "management result preserves unavailable played minutes");
    check_true(model.played_minutes_available, "management result preserves played availability");
    check_true(model.parent_unlock_active, "unrelated management result preserves unlock state");
    check_true(ptc_ui_apply_result_json(&model, failure), "error result parses");
    check_true(strcmp(model.result_status, "error") == 0, "error status mapped");
    check_true(strstr(model.message, "签名") != NULL, "error message preserved");
    check_true(model.status_loaded, "error preserves loaded state");
    check_int(model.remaining_minutes, 42, "error preserves remaining minutes");
    check_int(model.played_minutes, 18, "error preserves played minutes");
    check_int(model.play_timer_enabled, 1, "error preserves timer state");
    check_true(model.parent_unlock_active, "error preserves unlock state");
    check_true(ptc_ui_apply_result_json(&model, unlock_end), "unlock end result parses");
    check_true(!model.parent_unlock_active, "unlock end clears unlock state");
    check_true(ptc_ui_apply_result_json(&model, future), "unknown mode result parses");
    check_true(model.feedback_detail[0] == '\0', "success clears prior feedback detail");
    check_true(strcmp(model.mode, "未知模式") == 0, "unknown mode stays localized");
    model.view = PTC_UI_PARENT;
    check_true(ptc_ui_apply_result_json(&model, complete_setup), "complete setup result parses");
    check_int(model.view, PTC_UI_SETUP, "complete setup opens automatic countdown page");
    check_int(model.setup_activate_after, 105, "complete setup maps activation deadline");
    check_true(ptc_ui_apply_result_json(&model, active_setup), "active setup result parses");
    check_int(model.view, PTC_UI_CHILD, "active setup automatically opens child page");
    check_true(strstr(model.message, "自动控制已启用") != NULL, "active setup shows completion message");
    check_true(!ptc_ui_apply_result_json(&model, "{"), "bad result rejected");
    check_true(!ptc_ui_apply_result_json(&model, "[]"), "non-object result rejected");
    check_true(!ptc_ui_apply_result_json(&model, "{}"), "missing status rejected");
}

static void test_bedtime_conflict_detection(void)
{
    PtcUiModel model;
    PtcUiBedtimeConflict conflict;
    memset(&model, 0, sizeof(model));

    model.draft_bedtime.enabled = true;
    model.draft_bedtime.start_min = 1260;
    model.draft_bedtime.end_min = 480;
    model.played_minutes_available = true;
    model.played_minutes = 1200;

    check_int(ptc_ui_minutes_to_bedtime(1200, &model.draft_bedtime), 60, "minutes to bedtime calculation");

    check_true(ptc_ui_check_bedtime_conflict(&model, 90, &conflict), "quota exceeding bedtime conflict detected");
    check_true(conflict.quota_exceeds_bedtime, "quota_exceeds_bedtime flag set");
    check_int(conflict.minutes_to_bedtime, 60, "conflict holds correct minutes to bedtime");

    memset(&conflict, 0, sizeof(conflict));
    model.current_limit_action_loaded = true;
    model.current_limit_action = PTC_LIMIT_ACTION_RAW_BLOCK;
    check_true(!ptc_ui_check_bedtime_conflict(&model, 30, &conflict), "no quota overflow when target fits bedtime");

    memset(&conflict, 0, sizeof(conflict));
    model.current_limit_action = PTC_LIMIT_ACTION_REMIND;
    check_true(ptc_ui_check_bedtime_conflict(&model, 30, &conflict), "remind action bedtime conflict detected");
    check_true(conflict.remind_bedtime_conflict, "remind_bedtime_conflict flag set");
}

static void test_preview_remaining_minutes(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));

    model.played_minutes_available = true;
    model.played_minutes = 45;

    // 1. Set today limit
    model.operation = PTC_UI_OPERATION_SET_TODAY_LIMIT;
    model.draft_minutes = 90;
    check_int(ptc_ui_preview_remaining_minutes(&model), 45, "preview remaining for set today limit (90 - 45)");

    // 2. Parent unlock
    model.operation = PTC_UI_OPERATION_PARENT_UNLOCK;
    model.draft_minutes = 30;
    check_int(ptc_ui_preview_remaining_minutes(&model), 30, "preview remaining for parent unlock ignores played minutes");

    // 3. Add today minutes
    model.operation = PTC_UI_OPERATION_ADD_TODAY_MINUTES;
    model.draft_minutes = 15;
    model.remaining_available = true;
    model.remaining_minutes = 20;
    check_int(ptc_ui_preview_remaining_minutes(&model), 35, "preview remaining for add today minutes (20 + 15)");
}

int main(void)
{
    test_navigation();
    test_page_action_counts();
    test_editors();
    test_numpad();
    test_probe_confirmation();
    test_execution_state();
    test_setup_grace_countdown();
    test_hit_test_child();
    test_hit_test_error();
    test_hit_test_parent();
    test_hit_test_overlays();
    test_result_mapping();
    test_bedtime_conflict_detection();
    test_preview_remaining_minutes();
    if (failures != 0) {
        return 1;
    }
    puts("PTC UI state tests passed");
    return 0;
}
