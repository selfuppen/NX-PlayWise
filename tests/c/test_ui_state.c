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

static bool rects_overlap(PtcUiRect left, PtcUiRect right)
{
    return left.x < right.x + right.w && left.x + left.w > right.x &&
           left.y < right.y + right.h && left.y + left.h > right.y;
}

static void test_release_navigation(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_TODAY), 5, "today actions");
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_PLAN), 0, "weekly plan is edited directly");
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_SECURITY), 5, "security actions");
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
    check_int(ptc_ui_weekday_for_display_slot(0), 1, "weekly display starts on Monday");
    check_int(ptc_ui_weekday_for_display_slot(5), 6, "Saturday is the first weekend slot");
    check_int(ptc_ui_weekday_for_display_slot(6), 0, "weekly display ends on Sunday");
    check_int(PTC_UI_SHORTCUT_PRESET_COUNT, 14, "all common shortcut combinations are listed");
    check_true(strstr(ptc_ui_shortcut_common_label(5), "Plus") != NULL, "plus shortcut preset is visible");
    check_true(strstr(ptc_ui_shortcut_common_label(13), "Minus") != NULL, "minus shortcut preset is visible");
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

    ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_WEEKLY_MINUTES, PTC_UI_OVERLAY_NONE,
        "修改周一的额度", "输入 1 到 1440 分钟", 4, 1, 1440, 60);
    ptc_ui_numpad_adjust(&model, 15);
    check_true(strcmp(model.numpad_text, "75") == 0, "weekly quick increase updates input");
    ptc_ui_numpad_adjust(&model, -100);
    check_true(strcmp(model.numpad_text, "1") == 0, "weekly quick adjustment clamps to minimum");

    ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_OFFLINE_CODE, PTC_UI_OVERLAY_NONE,
        "输入加时码", "8 位数字", 8, 0, 0, 0);
    snprintf(model.numpad_text, sizeof(model.numpad_text), "1051468");
    check_true(!ptc_ui_numpad_validate(&model, NULL), "short code rejected");
    snprintf(model.numpad_text, sizeof(model.numpad_text), "10514680");
    check_true(ptc_ui_numpad_validate(&model, NULL), "eight-digit code accepted");
}

static void test_time_previews(void)
{
    PtcUiModel model;
    bool capped = false;
    memset(&model, 0, sizeof(model));
    model.operation = PTC_UI_OPERATION_SET_TODAY_LIMIT;
    model.draft_minutes = 60;
    model.played_minutes_available = true;
    model.played_minutes = 20;
    check_int(ptc_ui_preview_remaining_minutes(&model), 40, "set-limit preview subtracts played time");
    model.played_minutes_available = false;
    check_int(ptc_ui_preview_remaining_minutes(&model), -1, "unavailable played time is not guessed");
    model.played_minutes_available = true;
    model.played_minutes = 60;
    check_true(ptc_ui_limit_minutes_would_restrict(&model, 60), "equal played and limit requires immediate restriction warning");
    model.operation = PTC_UI_OPERATION_ADD_TODAY_MINUTES;
    model.remaining_available = true;
    model.remaining_minutes = 25;
    model.draft_minutes = 15;
    check_int(ptc_ui_preview_remaining_minutes(&model), 40, "add preview stacks on remaining time");
    model.remaining_available = false;
    check_int(ptc_ui_preview_remaining_minutes(&model), -1, "unlimited add preview is unavailable");

    memset(&model, 0, sizeof(model));
    model.status_loaded = true;
    model.played_minutes_available = true;
    model.played_minutes = 20;
    model.remaining_available = true;
    model.remaining_minutes = 40;
    check_int(ptc_ui_grant_estimate_remaining(&model, 30, &capped), 70,
              "local grant estimate adds to current remaining");
    check_true(!capped, "ordinary local grant estimate is not capped");
    model.played_minutes = 1430;
    model.remaining_minutes = 10;
    check_int(ptc_ui_grant_estimate_remaining(&model, 30, &capped), 10,
              "local grant estimate respects the daily maximum");
    check_true(capped, "daily maximum truncation is reported");
    model.unrestricted_today = 1;
    check_int(ptc_ui_grant_estimate_remaining(&model, 30, &capped), 30,
              "unlimited day becomes a grant-sized limited remainder");
    model.grant_status_refresh_failed = true;
    check_int(ptc_ui_grant_estimate_remaining(&model, 30, &capped), -1,
              "failed refresh is never presented as a live estimate");
}

static void test_candidate_navigation(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    model.editor_index = 0;
    model.selected_index = 0;
    ptc_ui_move_weekly_focus(&model, -1, 0);
    check_int(model.editor_index, 6, "weekly dates wrap at the start of the week");
    ptc_ui_move_weekly_focus(&model, 0, 1);
    check_int(model.selected_index, 1, "weekly focus moves from date to mode");
    ptc_ui_move_weekly_focus(&model, -1, 0);
    check_int(model.selected_index, 1, "weekly button focus stops at the left edge");
    ptc_ui_move_weekly_focus(&model, 1, 0);
    ptc_ui_move_weekly_focus(&model, 1, 0);
    ptc_ui_move_weekly_focus(&model, 1, 0);
    check_int(model.selected_index, 3, "weekly button focus stops at save");

    model.overlay = PTC_UI_OVERLAY_CREDENTIAL;
    model.credential_kind = 1;
    model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
    ptc_ui_move_overlay_selection(&model, 0, 1);
    check_int(model.overlay_selection, PTC_UI_CREDENTIAL_RANDOM,
              "device credential moves to random generation");
    ptc_ui_move_overlay_selection(&model, 0, 1);
    check_int(model.overlay_selection, PTC_UI_CREDENTIAL_SAVE,
              "device credential skips secret-only actions");

    model.overlay = PTC_UI_OVERLAY_GRANT_SETUP;
    model.grant_more_expanded = false;
    model.overlay_selection = PTC_UI_GRANT_SETUP_MORE;
    ptc_ui_move_overlay_selection(&model, 0, 1);
    check_int(model.overlay_selection, PTC_UI_GRANT_SETUP_MORE,
              "collapsed grant setup stops at more settings");
    model.grant_more_expanded = true;
    ptc_ui_move_overlay_selection(&model, 0, 1);
    check_int(model.overlay_selection, PTC_UI_GRANT_SETUP_EXPORT,
              "expanded grant setup exposes export");

    model.overlay = PTC_UI_OVERLAY_GRANT_LOCAL;
    model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
    ptc_ui_move_overlay_selection(&model, 0, -1);
    check_int(model.overlay_selection, PTC_UI_GRANT_LOCAL_ADJUST_FIRST,
              "local generator moves from generate to adjustment row");
    ptc_ui_move_overlay_selection(&model, 0, 1);
    ptc_ui_move_overlay_selection(&model, 0, 1);
    check_int(model.overlay_selection, PTC_UI_GRANT_LOCAL_BACK,
              "local generator can focus the return button");

    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    model.confirm_return_overlay = PTC_UI_OVERLAY_CREDENTIAL;
    snprintf(model.confirm_return_title, sizeof(model.confirm_return_title), "管理设备名");
    snprintf(model.confirm_return_body, sizeof(model.confirm_return_body), "草稿说明");
    check_true(ptc_ui_cancel_overlay(&model), "credential confirmation can be cancelled");
    check_int(model.overlay, PTC_UI_OVERLAY_CREDENTIAL,
              "cancelled credential confirmation returns to the draft");
    check_true(strcmp(model.overlay_title, "管理设备名") == 0 &&
               strcmp(model.overlay_body, "草稿说明") == 0,
               "cancelled confirmation restores the editor copy");

    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    model.operation = PTC_UI_OPERATION_REDEEM_OFFLINE_CODE;
    snprintf(model.pending_code, sizeof(model.pending_code), "10514680");
    check_true(ptc_ui_cancel_overlay(&model), "code confirmation can be cancelled");
    check_true(model.pending_code[0] == '\0', "cancelled code confirmation forgets the full code");
}

static void test_release_hit_targets(void)
{
    PtcUiModel model;
    PtcUiRect code_input;
    PtcUiRect refresh;
    memset(&model, 0, sizeof(model));
    model.view = PTC_UI_CHILD;
    code_input = ptc_ui_child_submit_rect();
    refresh = ptc_ui_child_refresh_rect();
    check_int(code_input.x, 86, "child code input aligns with reward card");
    check_int(code_input.w, 696, "child code input uses the prominent field width");
    check_int(refresh.y, 454, "child refresh stays below game-time summary");
    check_hit(hit_center(&model, ptc_ui_child_submit_rect()), PTC_UI_HIT_CHILD_SUBMIT_CODE, 0, "child code button");
    model.disable_flag_present = true;
    check_hit(hit_center(&model, ptc_ui_child_submit_rect()), PTC_UI_HIT_NONE, 0, "disabled child code button is not actionable");
    model.disable_flag_present = false;
    check_hit(hit_center(&model, ptc_ui_child_refresh_rect()), PTC_UI_HIT_CHILD_REFRESH, 0, "child refresh button");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(0)), PTC_UI_HIT_NONE, 0, "parent controls hidden from child");

    model.view = PTC_UI_PARENT;
    model.parent_page = PTC_UI_PARENT_TODAY;
    check_hit(hit_center(&model, ptc_ui_parent_refresh_rect()), PTC_UI_HIT_PARENT_REFRESH, 0,
              "parent refresh is prominent on the page");
    model.parent_page = PTC_UI_PARENT_PLAN;
    model.draft_week[1].mode = PTC_RULE_MODE_LIMIT;
    check_hit(hit_center(&model, ptc_ui_weekly_day_minutes_rect(0)), PTC_UI_HIT_WEEKLY_MIN_INPUT, 1,
              "leftmost weekly day edits Monday without changing protocol order");
    model.draft_week[1].mode = PTC_RULE_MODE_UNLIMITED;
    check_hit(hit_center(&model, ptc_ui_weekly_day_rect(0)), PTC_UI_HIT_WEEKLY_DAY, 1,
              "unlimited weekly day remains selectable for an explanatory message");
    check_hit(hit_center(&model, ptc_ui_parent_refresh_rect()), PTC_UI_HIT_PARENT_REFRESH, 0,
              "weekly refresh button is actionable");
    check_hit(hit_center(&model, ptc_ui_parent_footer_rect(2)), PTC_UI_HIT_PARENT_BACK, 0,
              "parent back action occupies the third footer slot");
    check_hit(hit_center(&model, ptc_ui_weekly_save_rect()), PTC_UI_HIT_WEEKLY_SAVE, 0,
              "weekly save is on the page");
    model.parent_page = PTC_UI_PARENT_SUPPORT;
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(0)), PTC_UI_HIT_PARENT_CARD, 0, "support environment card");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(4)), PTC_UI_HIT_PARENT_CARD, 4, "support diagnostics card");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(5)), PTC_UI_HIT_NONE, 0, "removed probe card is absent");

    model.view = PTC_UI_SETUP;
    model.setup_step = PTC_UI_SETUP_SHORTCUT;
    check_hit(hit_center(&model, ptc_ui_setup_shortcut_card_rect(0)), PTC_UI_HIT_SETUP_SHORTCUT_CARD, 0,
              "setup shortcut preset card");
    check_hit(hit_center(&model, ptc_ui_setup_shortcut_capture_rect()), PTC_UI_HIT_SETUP_SHORTCUT_CAPTURE, 0,
              "setup manual shortcut capture");
    check_true(!rects_overlap(ptc_ui_setup_shortcut_card_rect(0), ptc_ui_setup_shortcut_card_rect(1)),
               "setup shortcut presets do not overlap");
    check_true(!rects_overlap(ptc_ui_setup_shortcut_card_rect(1), ptc_ui_setup_shortcut_capture_rect()),
               "setup manual shortcut card does not overlap presets");
    check_true(!rects_overlap(ptc_ui_setup_shortcut_card_rect(6), ptc_ui_setup_shortcut_card_rect(13)),
               "two shortcut preset columns do not overlap");
    model.setup_step = PTC_UI_SETUP_PIN;
    check_hit(hit_center(&model, ptc_ui_setup_pin_rect()), PTC_UI_HIT_SETUP_PIN, 0, "setup PIN guide");
    snprintf(model.setup_phase, sizeof(model.setup_phase), "pending");
    check_true(!ptc_ui_setup_takeover_complete(&model), "pending takeover still requires confirmation");
    snprintf(model.setup_phase, sizeof(model.setup_phase), "restored");
    check_true(!ptc_ui_setup_takeover_complete(&model), "restored takeover still requires safe preflight");
    snprintf(model.setup_phase, sizeof(model.setup_phase), "released");
    check_true(ptc_ui_setup_takeover_complete(&model), "released takeover advances without resubmitting");
    snprintf(model.setup_phase, sizeof(model.setup_phase), "active");
    check_true(ptc_ui_setup_takeover_complete(&model), "active takeover advances without error 308");
    model.setup_step = PTC_UI_SETUP_ZONE;
    check_hit(hit_center(&model, ptc_ui_setup_zone_rect(0)), PTC_UI_HIT_SETUP_CHILD_ZONE, 0,
              "setup child zone choice");
    check_hit(hit_center(&model, ptc_ui_setup_zone_rect(1)), PTC_UI_HIT_SETUP_PARENT_ZONE, 1,
              "setup parent zone choice");
    check_hit(hit_center(&model, ptc_ui_setup_primary_rect()), PTC_UI_HIT_SETUP_PRIMARY, 0,
              "setup zone confirmation remains a separate action");

    model.overlay = PTC_UI_OVERLAY_CONFIRM;
    model.operation = PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT;
    check_hit(hit_center(&model, ptc_ui_confirm_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CONFIRM, 0, "recovery confirmation");
    check_int(ptc_ui_take_confirmed_operation(&model), PTC_UI_OPERATION_RESTORE_INSTALL_SNAPSHOT,
        "recovery action confirmed once");
    check_int(ptc_ui_take_confirmed_operation(&model), PTC_UI_OPERATION_NONE, "confirmation cannot be reused");

    model.overlay = PTC_UI_OVERLAY_NUMPAD;
    model.numpad_purpose = PTC_UI_NUMPAD_WEEKLY_MINUTES;
    check_hit(hit_center(&model, ptc_ui_numpad_quick_rect(2)), PTC_UI_HIT_NUMPAD_QUICK, 2,
              "numpad quick increase button");

    model.overlay = PTC_UI_OVERLAY_CREDENTIAL;
    model.credential_kind = 1;
    check_hit(hit_center(&model, ptc_ui_credential_random_rect()), PTC_UI_HIT_CREDENTIAL_RANDOM, 0,
              "credential random button");
    check_hit(hit_center(&model, ptc_ui_credential_reveal_rect()), PTC_UI_HIT_NONE, 0,
              "device-name editor has no invisible secret reveal target");
    model.overlay = PTC_UI_OVERLAY_GRANT_SETUP;
    check_hit(hit_center(&model, ptc_ui_grant_qr_rect()), PTC_UI_HIT_GRANT_QR, 0, "pairing QR button");
    check_hit(hit_center(&model, ptc_ui_grant_local_toggle_rect()), PTC_UI_HIT_GRANT_LOCAL_TOGGLE, 0,
              "local generator expansion button");
    check_hit(hit_center(&model, ptc_ui_grant_more_toggle_rect()), PTC_UI_HIT_GRANT_MORE_TOGGLE, 0,
              "grant more-settings expansion button");
    check_hit(hit_center(&model, ptc_ui_grant_export_rect()), PTC_UI_HIT_NONE, 0,
              "collapsed more settings hide export");
    model.grant_more_expanded = true;
    check_hit(hit_center(&model, ptc_ui_grant_export_rect()), PTC_UI_HIT_GRANT_EXPORT, 0, "parent config export button");
    check_hit(hit_center(&model, ptc_ui_grant_edit_url_rect()), PTC_UI_HIT_GRANT_EDIT_URL, 0, "pairing URL edit button");
    check_hit(hit_center(&model, ptc_ui_grant_reset_url_rect()), PTC_UI_HIT_GRANT_RESET_URL, 0, "pairing URL reset button");
    model.overlay = PTC_UI_OVERLAY_GRANT_LOCAL;
    check_hit(hit_center(&model, ptc_ui_grant_adjust_rect(3)), PTC_UI_HIT_GRANT_ADJUST, 3,
              "local generator exposes the plus-fifteen shortcut");
    check_hit(hit_center(&model, ptc_ui_grant_generate_rect()), PTC_UI_HIT_GRANT_GENERATE, 0,
              "local generator has a dedicated generate action");
    model.overlay = PTC_UI_OVERLAY_SHORTCUT_MANAGER;
    check_hit(hit_center(&model, ptc_ui_shortcut_option_rect(13)), PTC_UI_HIT_SHORTCUT_OPTION, 13,
              "shortcut manager exposes all common combinations");
    check_hit(hit_center(&model, ptc_ui_shortcut_hint_rect()), PTC_UI_HIT_SHORTCUT_HINT, 0,
              "shortcut manager exposes the child hint switch");
    model.overlay = PTC_UI_OVERLAY_WEEKLY_LEAVE;
    model.weekly_leave_selection = 1;
    ptc_ui_weekly_leave_move(&model, 1);
    check_int(model.weekly_leave_selection, 2, "weekly leave selection moves right to save");
    ptc_ui_weekly_leave_move(&model, 1);
    check_int(model.weekly_leave_selection, 0, "weekly leave selection wraps to discard");
    ptc_ui_weekly_leave_move(&model, -1);
    check_int(model.weekly_leave_selection, 2, "weekly leave selection moves left with wrapping");
    check_hit(hit_center(&model, ptc_ui_discard_rect(model.overlay)), PTC_UI_HIT_OVERLAY_DISCARD, 0,
              "weekly leave discard button");
    model.overlay = PTC_UI_OVERLAY_CREDENTIAL_LEAVE;
    check_hit(hit_center(&model, ptc_ui_discard_rect(model.overlay)), PTC_UI_HIT_OVERLAY_DISCARD, 0,
              "credential leave has a separate discard button");
    check_hit(hit_center(&model, ptc_ui_confirm_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CONFIRM, 0,
              "credential leave has a continue-editing button");
    check_hit(hit_center(&model, ptc_ui_cancel_rect(model.overlay)), PTC_UI_HIT_NONE, 0,
              "credential leave has no invisible cancel target");
    model.overlay = PTC_UI_OVERLAY_CODE_RESULT;
    check_hit(hit_center(&model, ptc_ui_confirm_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CONFIRM, 0,
              "code result has a completion target");
    check_hit(hit_center(&model, ptc_ui_cancel_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CANCEL, 0,
              "code result can return to the child area");
    model.overlay = PTC_UI_OVERLAY_AUTH_ERROR;
    check_true(!rects_overlap(ptc_ui_confirm_rect(model.overlay), ptc_ui_cancel_rect(model.overlay)),
               "PIN error retry and cancel targets do not overlap");
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
    const char *effect_not_observed =
        "{\"version\":1,\"request_id\":\"grant-error\",\"type\":\"offline_code\",\"status\":\"error\","
        "\"error\":{\"code\":306,\"reason\":\"pctl_effect_not_observed\","
        "\"message\":\"家长控制运行时未观察到生效\"},"
        "\"state\":{\"day_index\":1,\"limited_today\":-1,\"blocked_today\":-1,\"unrestricted_today\":-1,"
        "\"remaining_available\":false,\"remaining_minutes\":-1,\"played_minutes_available\":false,"
        "\"played_minutes\":-1,\"play_timer_enabled\":-1,\"restricted_now\":-1},\"completed_at\":108}";
    const char *preview =
        "{\"version\":1,\"request_id\":\"preview\",\"type\":\"preview_offline_code\",\"status\":\"ok\","
        "\"state\":{\"day_index\":1,\"limited_today\":1,\"blocked_today\":0,\"unrestricted_today\":0,"
        "\"remaining_available\":true,\"remaining_minutes\":20,\"played_minutes_available\":true,"
        "\"played_minutes\":40,\"play_timer_enabled\":1,\"restricted_now\":0},"
        "\"preview\":{\"grant_minutes\":30,\"remaining_after_available\":true,"
        "\"remaining_after_minutes\":50,\"effective_add_minutes\":30,\"capped\":false,"
        "\"converts_unlimited_to_limited\":false},\"completed_at\":109}";

    memset(&model, 0, sizeof(model));
    check_true(ptc_ui_apply_result_json(&model, pending), "syncing result parses");
    check_int(model.view, PTC_UI_SETUP, "syncing result opens setup status");
    check_true(strcmp(model.setup_phase, "released") == 0, "syncing phase mapped");
    check_int((int)ptc_ui_setup_grace_remaining(&model, 100), 5, "sync countdown mapped");
    check_true(ptc_ui_apply_result_json(&model, active), "active result parses");
    check_int(model.view, PTC_UI_CHILD, "active result opens child home");
    check_true(strcmp(model.mode, "额度管理") == 0, "release product language is stable");
    check_int(model.remaining_minutes, 40, "remaining minutes mapped");
    ptc_ui_mark_status_updated(&model, 1000);
    check_int((int)ptc_ui_status_age_seconds(&model, 1030), 30, "status age is measured from last refresh");
    check_int((int)ptc_ui_status_age_seconds(&model, 999), 0, "status age never goes negative");
    check_true(ptc_ui_apply_result_json(&model, protection), "protection result parses");
    check_true(strstr(model.message, "保护模式") != NULL, "protection guidance is visible");
    check_true(ptc_ui_apply_result_json(&model, effect_not_observed), "306 result parses");
    check_int(model.error_code, 306, "306 error code is retained");
    check_true(strstr(model.feedback_detail, "手动") != NULL &&
               strstr(model.feedback_detail, "重新检测") != NULL,
               "306 provides manual control guidance");
    check_true(ptc_ui_apply_result_json(&model, preview), "offline-code preview parses");
    check_int(model.code_grant_minutes, 30, "preview grant minutes mapped");
    check_true(model.code_preview_after_available && model.code_preview_after_minutes == 50,
               "preview post-redemption remainder mapped");
}

int main(void)
{
    test_release_navigation();
    test_numeric_input();
    test_time_previews();
    test_candidate_navigation();
    test_release_hit_targets();
    test_user_state_mapping();
    if (failures) return 1;
    puts("PTC release UI state tests passed");
    return 0;
}
