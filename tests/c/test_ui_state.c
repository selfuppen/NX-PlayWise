#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../../companion/nro/ui_graphics.h"
#include "../../common/time/ptc_time.h"

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

static void test_parent_status_summary(void)
{
    PtcUiModel model;
    char summary[160];
    memset(&model, 0, sizeof(model));

    ptc_ui_format_parent_status_summary(&model, 1000, summary, sizeof(summary));
    check_true(summary[0] != '\0' && strstr(summary, "尚无可靠读数") != NULL,
               "parent footer shows a placeholder before the first status result");

    model.status_loaded = true;
    model.status_updated_at = 1000;
    model.remaining_available = true;
    model.remaining_minutes = 42;
    ptc_ui_format_parent_status_summary(&model, 1010, summary, sizeof(summary));
    check_true(strstr(summary, "控制正常") != NULL && strstr(summary, "42 分钟") != NULL,
               "parent footer shows the current remaining time");

    model.remaining_minutes = 0;
    ptc_ui_format_parent_status_summary(&model, 1010, summary, sizeof(summary));
    check_true(strstr(summary, "已到限制") != NULL,
               "parent footer shows the exhausted-time state");

    snprintf(model.setup_phase, sizeof(model.setup_phase), "protection");
    ptc_ui_format_parent_status_summary(&model, 1010, summary, sizeof(summary));
    check_true(strstr(summary, "保护模式") != NULL,
               "parent footer prioritizes protection state guidance");

    memset(&model, 0, sizeof(model));
    model.today_override_present = true;
    ptc_ui_format_holiday_priority_summary(&model, summary, sizeof(summary));
    check_true(strstr(summary, "今日临时设置覆盖") != NULL,
               "holiday priority summary identifies the highest-priority override");
    model.today_override_present = false;
    model.holiday_enabled = false;
    ptc_ui_format_holiday_priority_summary(&model, summary, sizeof(summary));
    check_true(strstr(summary, "预设未开启") != NULL && strstr(summary, "回退周计划") != NULL,
               "holiday priority summary explains the disabled fallback");
    model.holiday_enabled = true;
    model.calendar_covered = false;
    ptc_ui_format_holiday_priority_summary(&model, summary, sizeof(summary));
    check_true(strstr(summary, "日历未覆盖") != NULL,
               "holiday priority summary explains uncovered years");
    model.calendar_covered = true;
    snprintf(model.rule_source, sizeof(model.rule_source), "statutory_holiday");
    ptc_ui_format_holiday_priority_summary(&model, summary, sizeof(summary));
    check_true(strstr(summary, "法定休假日命中") != NULL,
               "holiday priority summary identifies an active statutory holiday");
    snprintf(model.rule_source, sizeof(model.rule_source), "week");
    ptc_ui_format_holiday_priority_summary(&model, summary, sizeof(summary));
    check_true(strstr(summary, "普通日期") != NULL,
               "holiday priority summary identifies ordinary-date fallback");
}

static void test_release_navigation(void)
{
    PtcUiModel model;
    char shortcut_hint[160];
    memset(&model, 0, sizeof(model));
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_TODAY), 5, "today actions");
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_PLAN), 0, "weekly plan is edited directly");
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_HOLIDAY), 7, "holiday policy exposes rules, actions and calendar entry");
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_GRANT), 3, "grant page only exposes grant actions");
    check_int(ptc_ui_parent_action_count(PTC_UI_PARENT_SETTINGS), 5, "settings page exposes preferences, security and support");

    model.parent_page = PTC_UI_PARENT_TODAY;
    ptc_ui_change_parent_page(&model, -1);
    check_int(model.parent_page, PTC_UI_PARENT_SETTINGS, "page wraps to settings");
    check_int(model.settings_page, PTC_UI_SETTINGS_ROOT, "top-level navigation opens settings root");
    ptc_ui_change_parent_page(&model, 1);
    check_int(model.parent_page, PTC_UI_PARENT_TODAY, "page wraps to today");

    snprintf(model.setup_phase, sizeof(model.setup_phase), "active");
    check_true(ptc_ui_settings_status_label(&model) == NULL &&
               ptc_ui_settings_support_state(&model) == PTC_UI_ACTION_AVAILABLE,
               "healthy settings do not show a persistent badge");
    model.disable_flag_present = true;
    check_true(strcmp(ptc_ui_settings_status_label(&model), "需处理") == 0 &&
               ptc_ui_settings_support_state(&model) == PTC_UI_ACTION_RECOMMENDED,
               "disabled control recommends support with attention badge");
    model.disable_flag_present = false;
    snprintf(model.setup_phase, sizeof(model.setup_phase), "pending");
    check_true(strcmp(ptc_ui_settings_status_label(&model), "待完成") == 0,
               "unfinished takeover shows completion badge");

    model.parent_page = PTC_UI_PARENT_TODAY;
    model.selected_index = 0;
    ptc_ui_move_parent_selection(&model, 1, 0);
    check_int(model.selected_index, 1, "selection moves right");
    ptc_ui_move_parent_selection(&model, 0, -1);
    check_int(model.selected_index, 4, "five-card selection wraps upward");

    model.parent_page = PTC_UI_PARENT_HOLIDAY;
    model.selected_index = 0;
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 1, "holiday header moves down to statutory rule");
    ptc_ui_move_parent_selection(&model, 1, 0);
    check_int(model.selected_index, 2, "holiday navigation moves across rule cards");
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 6, "holiday right rule reaches calendar entry");

    model.parent_page = PTC_UI_PARENT_SETTINGS;
    model.settings_page = PTC_UI_SETTINGS_SUPPORT;
    model.recent_event_count = 3;
    model.selected_index = 4;
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 6, "support actions move down to newest recent event");
    ptc_ui_move_parent_selection(&model, 0, 1);
    check_int(model.selected_index, 7, "support recent events move vertically without overlap");
    ptc_ui_move_parent_selection(&model, 0, -1);
    check_int(model.selected_index, 6, "support recent event navigation returns upward");

    check_int(ptc_ui_next_rule_mode(PTC_RULE_MODE_LIMIT), PTC_RULE_MODE_UNLIMITED, "limit toggles to unlimited");
    check_int(ptc_ui_next_rule_mode(PTC_RULE_MODE_UNLIMITED), PTC_RULE_MODE_LIMIT, "unlimited toggles to limit");
    check_int(ptc_ui_weekday_for_display_slot(0), 1, "weekly display starts on Monday");
    check_int(ptc_ui_weekday_for_display_slot(5), 6, "Saturday is the first weekend slot");
    check_int(ptc_ui_weekday_for_display_slot(6), 0, "weekly display ends on Sunday");
    check_int(PTC_UI_SHORTCUT_PRESET_COUNT, 14, "all common shortcut combinations are listed");
    check_true(strcmp(ptc_ui_shortcut_common_label(5), "L + R + Plus(+)") == 0,
               "plus shortcut preset does not end in an ambiguous separator");
    check_true(strcmp(ptc_ui_shortcut_common_label(13), "ZL + ZR + Minus(-)") == 0,
               "minus shortcut preset uses an unambiguous button name");
    ptc_ui_format_custom_shortcut_hint(ptc_ui_shortcut_common_label(5), shortcut_hint, sizeof(shortcut_hint));
    check_true(strstr(shortcut_hint, "长按约 400ms") != NULL &&
               strstr(shortcut_hint, "L + R + Plus(+)") != NULL &&
               strstr(shortcut_hint, "进入家长区") != NULL,
               "custom shortcut hint explains the hold duration without changing the label");
    ptc_ui_format_custom_shortcut_hint(ptc_ui_shortcut_common_label(13), shortcut_hint, sizeof(shortcut_hint));
    check_true(strstr(shortcut_hint, "ZL + ZR + Minus(-)") != NULL,
               "minus shortcut hint remains unambiguous");
}

static void test_shortcut_hold_and_setup_migration(void)
{
    PtcUiShortcutHoldState hold = {0};
    PtcUiConfirmHoldState confirm_hold = {0};
    uint64_t lr_right = UINT64_C(0x10) | UINT64_C(0x40) | UINT64_C(0x4000);
    check_true(ptc_ui_shortcut_mask_held(lr_right, lr_right), "L+R+right chord matches all configured buttons");
    check_true(!ptc_ui_shortcut_mask_held(lr_right, lr_right & ~UINT64_C(0x4000)),
               "incomplete L+R+right chord does not match");
    check_true(!ptc_ui_shortcut_hold_update(&hold, true, 4), "first shortcut sample waits");
    check_true(!ptc_ui_shortcut_hold_update(&hold, true, 4), "second shortcut sample waits");
    check_true(!ptc_ui_shortcut_hold_update(&hold, true, 4), "third shortcut sample waits");
    check_true(ptc_ui_shortcut_hold_update(&hold, true, 4), "fourth 100ms sample triggers shortcut");
    check_true(!ptc_ui_shortcut_hold_update(&hold, true, 4), "held shortcut triggers only once");
    check_true(!ptc_ui_shortcut_hold_update(&hold, false, 4), "release resets shortcut latch");
    check_true(!ptc_ui_shortcut_hold_update(&hold, true, 4) &&
               !ptc_ui_shortcut_hold_update(&hold, true, 4) &&
               !ptc_ui_shortcut_hold_update(&hold, false, 4),
               "early release never triggers shortcut");
    check_true(!ptc_ui_shortcut_hold_update(&hold, true, 4) &&
               !ptc_ui_shortcut_hold_update(&hold, true, 4) &&
               !ptc_ui_shortcut_hold_update(&hold, true, 4) &&
               ptc_ui_shortcut_hold_update(&hold, true, 4),
               "shortcut can trigger again after complete release");
    check_true(!ptc_ui_confirm_hold_update(&confirm_hold, true, 3), "confirm hold starts without completing");
    check_int(ptc_ui_confirm_hold_progress(&confirm_hold, 3), 333, "confirm hold exposes deterministic progress");
    check_true(!ptc_ui_confirm_hold_update(&confirm_hold, true, 3), "confirm hold waits for threshold");
    check_true(ptc_ui_confirm_hold_update(&confirm_hold, true, 3), "confirm hold completes at threshold");
    check_true(!ptc_ui_confirm_hold_update(&confirm_hold, true, 3), "completed hold submits only once");
    check_true(!ptc_ui_confirm_hold_update(&confirm_hold, false, 3), "release resets confirm hold");
    check_int(ptc_ui_confirm_hold_progress(&confirm_hold, 3), 0, "released confirm hold clears progress");

    check_int(ptc_ui_migrate_setup_step(0, 3), 0, "completed older wizard remains complete");
    check_int(ptc_ui_migrate_setup_step(3, 3), PTC_UI_SETUP_SHORTCUT, "unfinished older wizard restarts safely");
    check_int(ptc_ui_migrate_setup_step(3, 4), PTC_UI_SETUP_THEME, "v4 theme step is stable");
    check_int(ptc_ui_migrate_setup_step(5, 4), PTC_UI_SETUP_ZONE, "v4 zone step is stable");
}

static void test_rule_result_guidance(void)
{
    PtcUiModel model;
    char message[192];
    char detail[192];
    PtcEffectiveRule restored;
    memset(&model, 0, sizeof(model));
    model.day_index = 1;
    model.played_minutes_available = true;
    model.played_minutes = 20;
    for (int day = 0; day < 7; ++day) {
        model.current_week[day].mode = PTC_RULE_MODE_LIMIT;
        model.current_week[day].minutes = 60;
        model.draft_week[day] = model.current_week[day];
    }
    model.draft_week[ptc_weekday_from_day_index(model.day_index)].minutes = 90;
    snprintf(model.rule_source, sizeof(model.rule_source), "today_override");
    ptc_ui_format_weekly_save_result(&model, message, sizeof(message), detail, sizeof(detail));
    check_true(strstr(message, "当前不变") != NULL && strstr(detail, "恢复周计划生效后") != NULL &&
               strstr(detail, "90") != NULL && strstr(detail, "20") != NULL && strstr(detail, "70") != NULL,
               "weekly override result states conclusion and calculation basis");
    snprintf(model.rule_source, sizeof(model.rule_source), "weekly");
    ptc_ui_format_weekly_save_result(&model, message, sizeof(message), detail, sizeof(detail));
    check_true(strstr(message, "影响今天") != NULL && strstr(detail, "额度 90") != NULL,
               "weekly result states immediate effect without an override");
    model.draft_week[ptc_weekday_from_day_index(model.day_index)] =
        model.current_week[ptc_weekday_from_day_index(model.day_index)];
    model.draft_week[3].minutes = 120;
    ptc_ui_format_weekly_save_result(&model, message, sizeof(message), detail, sizeof(detail));
    check_true(strstr(message, "其他日期") != NULL && strstr(message, "今天不受影响") != NULL,
               "weekly result identifies edits to other dates");

    model.today_override_present = true;
    model.today_override_rule.mode = PTC_RULE_MODE_LIMIT;
    model.today_override_rule.minutes = 45;
    model.holiday_enabled = true;
    model.holiday_rule.mode = PTC_RULE_MODE_LIMIT;
    model.holiday_rule.minutes = 120;
    model.calendar_covered = true;
    restored = ptc_ui_rule_after_today_restore(&model);
    check_int(restored.source, PTC_RULE_SOURCE_WEEKLY, "ordinary restore falls back to weekly plan");
    ptc_ui_format_restore_today_basis(&model, detail, sizeof(detail));
    check_true(strstr(detail, "当前临时设置") != NULL && strstr(detail, "额度 45") != NULL &&
               strstr(detail, "周计划") != NULL && strstr(detail, "额度 60") != NULL,
               "restore confirmation exposes current and restored calculation basis");

    model.draft_holiday_enabled = false;
    model.today_override_present = false;
    snprintf(model.rule_source, sizeof(model.rule_source), "weekly");
    ptc_ui_format_holiday_save_result(&model, message, sizeof(message), detail, sizeof(detail));
    check_true(strstr(message, "未启用") != NULL && strstr(message, "不受影响") != NULL &&
               strstr(detail, "总开关") != NULL && strstr(detail, "额度") == NULL && strstr(detail, "分钟") == NULL,
               "disabled holiday save gives a direct conclusion without calculation detail");
    model.draft_holiday_enabled = true;
    model.calendar_covered = true;
    model.draft_holiday_rule = model.holiday_rule;
    snprintf(model.rule_source, sizeof(model.rule_source), "statutory_holiday");
    ptc_ui_format_holiday_save_result(&model, message, sizeof(message), detail, sizeof(detail));
    check_true(strstr(message, "影响今天") != NULL && strstr(detail, "国家法定休假日") != NULL &&
               strstr(detail, "额度 120") != NULL,
               "active holiday save gives source and calculation basis");

    model.today_override_present = true;
    model.today_override_rule.mode = PTC_RULE_MODE_LIMIT;
    model.today_override_rule.minutes = 45;
    snprintf(model.rule_source, sizeof(model.rule_source), "today_override");
    ptc_ui_format_holiday_save_result(&model, message, sizeof(message), detail, sizeof(detail));
    check_true(strstr(message, "当前不变") != NULL && strstr(detail, "临时设置优先") != NULL &&
               strstr(detail, "额度") == NULL && strstr(detail, "分钟") == NULL,
               "holiday override result omits unchanged calculation detail");
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
        "设置周一的周计划额度", "输入 1 到 1440 分钟", 4, 1, 1440, 60);
    ptc_ui_numpad_adjust(&model, 15);
    check_true(strcmp(model.numpad_text, "75") == 0, "weekly quick increase updates input");
    ptc_ui_numpad_adjust(&model, -100);
    check_true(strcmp(model.numpad_text, "1") == 0, "weekly quick adjustment clamps to minimum");

    ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_HOLIDAY_MINUTES, PTC_UI_OVERLAY_NONE,
        "设置法定休假日额度", "输入 1 到 1440 分钟", 4, 1, 1440, 120);
    check_int(model.overlay, PTC_UI_OVERLAY_MINUTE_EDITOR,
              "statutory holiday quota uses the compact minute editor");
    check_true(model.numpad_replace_on_input && strcmp(model.numpad_text, "120") == 0,
               "holiday editor preselects the complete current value");
    ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_MAKEUP_MINUTES, PTC_UI_OVERLAY_NONE,
        "设置调休工作日额度", "输入 1 到 1440 分钟", 4, 1, 1440, 60);
    check_int(model.overlay, PTC_UI_OVERLAY_MINUTE_EDITOR,
              "makeup workday quota uses the compact minute editor");

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
    PtcDayRule before;
    PtcDayRule after;
    uint8_t weekday;
    bool capped = false;
    memset(&model, 0, sizeof(model));
    model.operation = PTC_UI_OPERATION_SET_TODAY_LIMIT;
    model.draft_minutes = 60;
    model.played_minutes_available = true;
    model.remaining_available = true;
    model.played_minutes = 20;
    model.remaining_minutes = 40;
    check_int(ptc_ui_today_limit_start_value(&model, 180), 180,
              "today-limit editor starts from the effective total quota");
    check_int(ptc_ui_today_limit_start_value(&model, 0), 60,
              "missing rule rebuilds the total quota from played and remaining");
    model.played_minutes = 10;
    model.remaining_minutes = 0;
    check_int(ptc_ui_today_limit_start_value(&model, 0), 10,
              "rebuilt exhausted quota keeps the played total rather than inventing 60 minutes");
    model.played_minutes = 1400;
    model.remaining_minutes = 100;
    check_int(ptc_ui_today_limit_start_value(&model, 0), 1440,
              "rebuilt total quota is clamped to the daily maximum");
    model.played_minutes_available = false;
    check_int(ptc_ui_today_limit_start_value(&model, 180), 180,
              "unavailable played time preserves the effective quota fallback");
    check_int(ptc_ui_today_limit_start_value(&model, 0), 60,
              "invalid unavailable-state fallback uses the safe default");
    model.played_minutes_available = true;
    model.played_minutes = 20;
    check_int(ptc_ui_preview_remaining_minutes(&model), 40, "set-limit preview subtracts played time");
    model.played_minutes_available = false;
    check_int(ptc_ui_preview_remaining_minutes(&model), -1, "unavailable played time is not guessed");
    model.played_minutes_available = true;
    model.played_minutes = 60;
    check_true(ptc_ui_limit_minutes_would_restrict(&model, 60), "equal played and limit requires immediate restriction warning");
    check_true(ptc_ui_limit_minutes_would_restrict(&model, 30),
               "a quota below played time remains selectable and requires the restriction warning");
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

    before.mode = PTC_RULE_MODE_LIMIT;
    before.minutes = 60;
    after = before;
    check_true(!ptc_ui_day_rule_effectively_changed(before, after),
               "unchanged limit does not affect today's weekly quota");
    after.minutes = 90;
    check_true(ptc_ui_day_rule_effectively_changed(before, after),
               "changed limited minutes affect today's weekly quota");
    before.mode = PTC_RULE_MODE_UNLIMITED;
    before.minutes = 60;
    after.mode = PTC_RULE_MODE_UNLIMITED;
    after.minutes = 120;
    check_true(!ptc_ui_day_rule_effectively_changed(before, after),
               "retained minutes do not matter while both rules are unlimited");
    after.mode = PTC_RULE_MODE_LIMIT;
    check_true(ptc_ui_day_rule_effectively_changed(before, after),
               "weekly mode changes affect today's quota");

    memset(&model, 0, sizeof(model));
    weekday = ptc_weekday_from_day_index(model.day_index);
    model.current_week[weekday].mode = PTC_RULE_MODE_LIMIT;
    model.current_week[weekday].minutes = 60;
    model.draft_week[weekday] = model.current_week[weekday];
    check_true(!ptc_ui_weekly_today_changed(&model),
               "today comparison stays hidden before a real draft change");
    model.draft_week[(weekday + 1) % 7].mode = PTC_RULE_MODE_UNLIMITED;
    check_true(!ptc_ui_weekly_today_changed(&model),
               "another weekday change does not show today's comparison");
    model.draft_week[weekday].minutes = 75;
    check_true(ptc_ui_weekly_today_changed(&model),
               "today comparison appears for a changed current weekday limit");

    memset(&model, 0, sizeof(model));
    for (int day = 0; day < 7; ++day) {
        model.current_week[day].mode = PTC_RULE_MODE_LIMIT;
        model.current_week[day].minutes = 60;
        model.draft_week[day] = model.current_week[day];
    }
    model.weekly_last_day_slot = 0;
    model.draft_week[1].minutes = 90;
    {
        PtcUiWeeklyBulkStats stats;
        ptc_ui_weekly_bulk_stats(&model, false, &stats);
        check_int(stats.target_count, 5, "bulk preview counts all workday targets");
        check_int(stats.changed_count, 4, "bulk preview counts rules that source will overwrite");
        check_int(stats.unchanged_count, 1, "bulk preview counts matching source rules");
        check_int(stats.rule_group_count, 2, "bulk preview groups existing values before applying");
        check_int(model.draft_week[2].minutes, 60, "bulk preview does not mutate drafts");
    }
    check_true(ptc_ui_apply_weekly_bulk(&model, false), "bulk apply changes workday drafts");
    for (int day = 1; day <= 5; ++day) {
        check_int(model.draft_week[day].minutes, 90, "bulk apply copies source to every workday");
    }
    check_int(model.draft_week[0].minutes, 60, "workday bulk leaves Sunday unchanged");
    check_int(model.draft_week[6].minutes, 60, "workday bulk leaves Saturday unchanged");
    model.draft_week[1].mode = PTC_RULE_MODE_UNLIMITED;
    check_true(ptc_ui_apply_weekly_bulk(&model, true), "bulk apply changes weekend drafts");
    check_int(model.draft_week[0].mode, PTC_RULE_MODE_UNLIMITED, "weekend bulk copies source to Sunday");
    check_int(model.draft_week[6].mode, PTC_RULE_MODE_UNLIMITED, "weekend bulk copies source to Saturday");
    model.disable_flag_present = true;
    check_true(!ptc_ui_apply_weekly_bulk(&model, false), "emergency stop rejects bulk draft changes");
}

static double channel_luminance(unsigned int channel)
{
    double value = channel / 255.0;
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

static double contrast_ratio(uint32_t left, uint32_t right)
{
    double l1 = 0.2126 * channel_luminance((left >> 16) & 0xff) +
                0.7152 * channel_luminance((left >> 8) & 0xff) +
                0.0722 * channel_luminance(left & 0xff);
    double l2 = 0.2126 * channel_luminance((right >> 16) & 0xff) +
                0.7152 * channel_luminance((right >> 8) & 0xff) +
                0.0722 * channel_luminance(right & 0xff);
    if (l2 > l1) { double swap = l1; l1 = l2; l2 = swap; }
    return (l1 + 0.05) / (l2 + 0.05);
}

static void test_theme_resolution(void)
{
    PtcUiThemePreference preference = PTC_UI_THEME_DARK;
    PtcUiThemeView view;
    const PtcUiPalette *dark;
    check_true(ptc_ui_theme_parse_preference("system", &preference) && preference == PTC_UI_THEME_SYSTEM,
               "system theme preference parses");
    check_true(ptc_ui_theme_parse_preference("dark", &preference) && preference == PTC_UI_THEME_DARK,
               "dark theme preference parses");
    check_true(!ptc_ui_theme_parse_preference("auto", &preference), "invalid theme preference is rejected");
    view = ptc_ui_theme_make_view(PTC_UI_THEME_SYSTEM, PTC_UI_SYSTEM_THEME_UNAVAILABLE);
    check_int(view.resolved, PTC_UI_RESOLVED_LIGHT, "unavailable system theme falls back to light");
    check_true(!view.system_theme_available, "unavailable system theme is observable");
    view = ptc_ui_theme_make_view(PTC_UI_THEME_SYSTEM, PTC_UI_SYSTEM_THEME_DARK);
    check_int(view.resolved, PTC_UI_RESOLVED_DARK, "system dark theme resolves to dark");
    dark = view.palette;
    check_true(dark && dark->page_bg == 0x000000 && dark->surface == 0x14191E &&
               dark->accent == 0x6EA8FE && dark->focus == 0xA9C8FF,
               "OLED Hybrid palette is stable");
    check_true(contrast_ratio(dark->text_primary, dark->surface) >= 4.5 &&
               contrast_ratio(dark->text_secondary, dark->surface) >= 4.5 &&
               contrast_ratio(dark->border_control, dark->surface) >= 3.0 &&
               contrast_ratio(dark->focus, dark->surface) >= 3.0,
               "dark palette meets text and control contrast thresholds");
    {
        PtcUiModel model;
        PtcUiRect option;
        memset(&model, 0, sizeof(model));
        model.overlay = PTC_UI_OVERLAY_THEME;
        option = ptc_ui_theme_option_rect(2);
        check_hit(hit_center(&model, option), PTC_UI_HIT_THEME_OPTION, 2,
                  "dark theme option has a matching touch target");
    }
    {
        PtcUiRect icon = ptc_ui_notice_status_icon_rect(530);
        PtcUiRect command = ptc_ui_notice_command_text_rect(530, 100);
        check_true(!rects_overlap(icon, command), "compact notice icon does not overlap command text");
        check_int(icon.w, 20, "notice status icon is compact");
    }
}

static void test_candidate_navigation(void)
{
    PtcUiModel model;
    memset(&model, 0, sizeof(model));
    model.editor_index = 0;
    model.selected_index = 0;
    model.weekly_grid_slot = 0;
    model.weekly_last_day_slot = 6;
    ptc_ui_move_weekly_focus(&model, -1, 0);
    check_int(model.weekly_grid_slot, 0, "weekly grid stops at the left edge");
    ptc_ui_move_weekly_focus(&model, 1, 0);
    check_int(model.weekly_grid_slot, 1, "weekly grid moves to the next day");
    ptc_ui_move_weekly_focus(&model, 0, 1);
    check_int(model.selected_index, 1, "weekly focus moves from date to mode");
    ptc_ui_move_weekly_focus(&model, -1, 0);
    check_int(model.selected_index, 1, "weekly button focus stops at the left edge");
    ptc_ui_move_weekly_focus(&model, 1, 0);
    ptc_ui_move_weekly_focus(&model, 1, 0);
    ptc_ui_move_weekly_focus(&model, 1, 0);
    check_int(model.selected_index, 4, "weekly button focus stops at save");

    model.overlay = PTC_UI_OVERLAY_CREDENTIAL;
    model.credential_kind = 1;
    model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
    ptc_ui_move_overlay_selection(&model, 0, 1);
    check_int(model.overlay_selection, PTC_UI_CREDENTIAL_RANDOM,
              "device credential moves to random generation");
    ptc_ui_move_overlay_selection(&model, 0, 1);
    check_int(model.overlay_selection, PTC_UI_CREDENTIAL_SAVE,
              "device credential skips secret-only actions");

    model.overlay = PTC_UI_OVERLAY_GRANT_MANAGER;
    model.overlay_selection = PTC_UI_GRANT_MANAGER_DEVICE;
    ptc_ui_move_overlay_selection(&model, 1, 0);
    check_int(model.overlay_selection, PTC_UI_GRANT_MANAGER_SECRET,
              "grant manager moves across a card row");
    ptc_ui_move_overlay_selection(&model, 0, 1);
    check_int(model.overlay_selection, PTC_UI_GRANT_MANAGER_EDIT_URL,
              "grant manager moves down in the same column");
    ptc_ui_move_overlay_selection(&model, 0, 1);
    check_int(model.overlay_selection, PTC_UI_GRANT_MANAGER_RESET_URL,
              "grant manager falls back to the final card in an incomplete row");

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
    model.show_parent_shortcut_hint = true;
    model.custom_shortcut_enabled = true;
    check_hit(hit_center(&model, ptc_ui_child_footer_rect(1)), PTC_UI_HIT_CHILD_PARENT, 0,
              "visible child parent shortcut enters parent area");
    model.show_parent_shortcut_hint = false;
    check_hit(hit_center(&model, ptc_ui_child_footer_rect(1)), PTC_UI_HIT_NONE, 0,
              "hidden child parent shortcut is not a refresh target");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(0)), PTC_UI_HIT_NONE, 0, "parent controls hidden from child");

    model.view = PTC_UI_PARENT;
    model.parent_page = PTC_UI_PARENT_TODAY;
    check_hit(hit_center(&model, ptc_ui_parent_footer_rect(3)), PTC_UI_HIT_PARENT_REFRESH, 0,
              "parent global refresh occupies the fourth footer slot");
    check_hit(hit_center(&model, ptc_ui_parent_footer_rect(4)), PTC_UI_HIT_PARENT_STATUS, 0,
              "parent global status occupies the fifth footer slot");
    check_true(!rects_overlap(ptc_ui_parent_footer_rect(3), ptc_ui_parent_footer_rect(4)),
               "parent status does not overlap global refresh");
    check_int(ptc_ui_parent_footer_rect(0).w, 130, "parent previous-page footer is narrower");
    check_int(ptc_ui_parent_footer_rect(1).w, 130, "parent next-page footer is narrower");
    check_int(ptc_ui_parent_footer_rect(2).w, 170, "parent child-page footer preserves its longer label");
    check_int(ptc_ui_parent_footer_rect(3).w, 130, "parent refresh footer is narrower");
    check_int(ptc_ui_parent_footer_rect(4).x, 662, "parent status footer starts after the action buttons");
    check_int(ptc_ui_parent_footer_rect(4).w, 564, "parent status footer receives the freed width");
    check_int(ptc_ui_parent_footer_rect(4).x + ptc_ui_parent_footer_rect(4).w, 1226,
              "parent status footer aligns with the main content border");
    model.parent_page = PTC_UI_PARENT_PLAN;
    model.draft_week[1].mode = PTC_RULE_MODE_LIMIT;
    check_hit(hit_center(&model, ptc_ui_weekly_day_mode_rect(0)), PTC_UI_HIT_WEEKLY_MODE, 1,
              "weekly mode pill toggles Monday directly");
    check_hit(hit_center(&model, ptc_ui_weekly_day_minutes_rect(0)), PTC_UI_HIT_WEEKLY_MIN_INPUT, 1,
              "leftmost weekly day edits Monday without changing protocol order");
    model.draft_week[1].mode = PTC_RULE_MODE_UNLIMITED;
    check_hit(hit_center(&model, ptc_ui_weekly_day_minutes_rect(0)), PTC_UI_HIT_WEEKLY_MIN_INPUT, 1,
              "unlimited weekly quota remains semantic for an explanatory message");
    check_hit(hit_center(&model, ptc_ui_weekly_day_header_rect(0)), PTC_UI_HIT_WEEKLY_DAY, 1,
              "weekly header only selects the day");
    check_hit(hit_center(&model, ptc_ui_parent_footer_rect(4)), PTC_UI_HIT_PARENT_STATUS, 0,
              "weekly page shares the global status target");
    check_hit(hit_center(&model, ptc_ui_parent_footer_rect(2)), PTC_UI_HIT_PARENT_BACK, 0,
              "parent back action occupies the third footer slot");
    check_hit(hit_center(&model, ptc_ui_weekly_save_rect()), PTC_UI_HIT_WEEKLY_SAVE, 0,
              "weekly save is on the page");
    check_hit(hit_center(&model, ptc_ui_weekly_bulk_rect()), PTC_UI_HIT_WEEKLY_BULK, 0,
              "weekly bulk action occupies slot seven");
    for (int slot = 0; slot < 7; ++slot) {
        PtcUiRect card = ptc_ui_weekly_day_rect(slot);
        check_true(card.w == 96 && card.h == 200, "weekly day cards use the seven-column size");
        check_true(card.x == 54 + slot * 108 && card.y == 218,
                   "weekly day cards use the horizontal Monday-to-Sunday coordinates");
        check_true(ptc_ui_weekly_day_header_rect(slot).h == 42 &&
                   ptc_ui_weekly_day_mode_rect(slot).h == 50 &&
                   ptc_ui_weekly_day_minutes_rect(slot).h == 108,
                   "weekly subregions exactly partition the card height");
    }
    check_hit(hit_center(&model, ptc_ui_weekly_page_mode_rect()), PTC_UI_HIT_WEEKLY_MODE, 0,
              "weekly page mode switch uses page geometry");
    check_true(ptc_ui_weekly_page_mode_rect().y != ptc_ui_weekly_mode_rect().y,
               "weekly page mode geometry does not move the legacy editor overlay");
    {
        PtcUiRect notice = {54, 522, 1172, 128};
        check_true(!rects_overlap(ptc_ui_weekly_page_mode_rect(), notice),
                   "weekly mode does not overlap recent execution");
        check_true(!rects_overlap(ptc_ui_weekly_discard_rect(), notice),
                   "weekly discard does not overlap recent execution");
        check_true(!rects_overlap(ptc_ui_weekly_save_rect(), notice),
                   "weekly save does not overlap recent execution");
        check_true(!rects_overlap(ptc_ui_weekly_bulk_rect(), notice),
                   "weekly bulk card does not overlap recent execution");
        check_true(!rects_overlap(notice, ptc_ui_parent_footer_rect(3)),
                   "full recent execution card does not overlap parent footer");
    }
    model.disable_flag_present = true;
    model.draft_week[1].mode = PTC_RULE_MODE_LIMIT;
    check_hit(hit_center(&model, ptc_ui_weekly_day_minutes_rect(0)), PTC_UI_HIT_WEEKLY_MIN_INPUT, 1,
              "disabled weekly minutes remain semantic for read-only feedback");
    check_hit(hit_center(&model, ptc_ui_weekly_day_mode_rect(0)), PTC_UI_HIT_WEEKLY_MODE, 1,
              "disabled weekly mode remains semantic for read-only feedback");
    check_hit(hit_center(&model, ptc_ui_weekly_save_rect()), PTC_UI_HIT_WEEKLY_SAVE, 0,
              "disabled weekly save reaches submission guard feedback");
    check_hit(hit_center(&model, ptc_ui_weekly_discard_rect()), PTC_UI_HIT_WEEKLY_DISCARD, 0,
              "disabled weekly page still allows discarding a draft");
    model.disable_flag_present = false;
    model.parent_page = PTC_UI_PARENT_SETTINGS;
    model.settings_page = PTC_UI_SETTINGS_SUPPORT;
    model.recent_event_count = 3;
    snprintf(model.setup_phase, sizeof(model.setup_phase), "active");
    check_true(ptc_ui_safety_action_visible(&model, 0) && ptc_ui_safety_action_visible(&model, 1),
               "support setup and repair cards stay visible while active");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(0)), PTC_UI_HIT_NONE, 0,
              "active support takeover card is disabled");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(1)), PTC_UI_HIT_NONE, 0,
              "active support repair card is disabled");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(4)), PTC_UI_HIT_PARENT_CARD, 4, "support diagnostics card");
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(5)), PTC_UI_HIT_PARENT_CARD, 5, "support software information card");
    check_hit(hit_center(&model, ptc_ui_support_event_rect(0)), PTC_UI_HIT_SUPPORT_EVENT, 2,
              "newest support event is independently actionable");
    check_true(!rects_overlap(ptc_ui_support_event_rect(0), ptc_ui_support_event_rect(1)),
               "support event rows do not overlap");

    model.settings_page = PTC_UI_SETTINGS_ADVANCED;
    model.recent_event_count = 0;
    check_hit(hit_center(&model, ptc_ui_parent_card_rect(0)), PTC_UI_HIT_PARENT_CARD, 0,
              "advanced settings exposes the hbmenu entry card");
    check_hit(hit_center(&model, ptc_ui_parent_tab_rect(0)), PTC_UI_HIT_NONE, 0,
              "advanced settings hides top-level tab touch navigation");

    model.parent_page = PTC_UI_PARENT_HOLIDAY;
    check_hit(hit_center(&model, ptc_ui_holiday_enable_rect()), PTC_UI_HIT_HOLIDAY_ENABLE, 0, "holiday global switch target");
    check_hit(hit_center(&model, ptc_ui_holiday_mode_rect(0)), PTC_UI_HIT_HOLIDAY_MODE, 0, "holiday statutory mode target");
    check_hit(hit_center(&model, ptc_ui_holiday_minutes_rect(0)), PTC_UI_HIT_HOLIDAY_MINUTES, 0, "holiday statutory quota target");
    check_hit(hit_center(&model, ptc_ui_holiday_mode_rect(1)), PTC_UI_HIT_HOLIDAY_MODE, 1, "holiday makeup mode target");
    check_hit(hit_center(&model, ptc_ui_holiday_minutes_rect(1)), PTC_UI_HIT_HOLIDAY_MINUTES, 1, "holiday makeup quota target");
    check_hit(hit_center(&model, ptc_ui_holiday_card_rect(3)), PTC_UI_HIT_PARENT_CARD, 3, "holiday mode action");
    check_hit(hit_center(&model, ptc_ui_holiday_card_rect(4)), PTC_UI_HIT_PARENT_CARD, 4, "holiday discard action");
    check_hit(hit_center(&model, ptc_ui_holiday_card_rect(5)), PTC_UI_HIT_PARENT_CARD, 5, "holiday save action");
    check_hit(hit_center(&model, ptc_ui_holiday_calendar_rect()), PTC_UI_HIT_HOLIDAY_CALENDAR, 0,
              "holiday calendar viewer is in the status column");
    {
        PtcUiRect notice = {54, 522, 1172, 128};
        for (int index = 0; index < 6; ++index) {
            check_true(!rects_overlap(ptc_ui_holiday_card_rect(index), notice),
                       "holiday control does not overlap recent execution");
        }
        check_true(!rects_overlap(ptc_ui_holiday_card_rect(0), ptc_ui_holiday_card_rect(1)),
                   "holiday header does not overlap rule controls");
        check_true(!rects_overlap(ptc_ui_holiday_card_rect(1), ptc_ui_holiday_card_rect(2)),
                   "holiday rule cards sit side by side without overlap");
        check_true(ptc_ui_holiday_card_rect(3).w == 240 && ptc_ui_holiday_card_rect(4).w == 240 &&
                   ptc_ui_holiday_card_rect(5).w == 248,
                   "holiday bottom actions share the left control width");
        for (int index = 0; index < 6; ++index) {
            PtcUiRect card = ptc_ui_holiday_card_rect(index);
            check_true(card.x >= 54 && card.x + card.w <= 814,
                       "holiday controls stay in the 760px left column");
        }
    }

    model.overlay = PTC_UI_OVERLAY_WEEKLY_BULK;
    check_hit(hit_center(&model, ptc_ui_weekly_bulk_target_rect(0)), PTC_UI_HIT_WEEKLY_BULK_TARGET, 0,
              "weekly bulk workday target is touchable");
    check_hit(hit_center(&model, ptc_ui_weekly_bulk_target_rect(1)), PTC_UI_HIT_WEEKLY_BULK_TARGET, 1,
              "weekly bulk weekend target is touchable");
    check_hit(hit_center(&model, ptc_ui_confirm_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CONFIRM, 0,
              "weekly bulk has a separate apply action");
    check_true(!rects_overlap(ptc_ui_weekly_bulk_target_rect(0), ptc_ui_weekly_bulk_target_rect(1)),
               "weekly bulk target cards do not overlap");
    check_true(!rects_overlap(ptc_ui_weekly_bulk_target_rect(1), ptc_ui_cancel_rect(model.overlay)) &&
               !rects_overlap(ptc_ui_weekly_bulk_target_rect(1), ptc_ui_confirm_rect(model.overlay)),
               "weekly bulk content does not overlap its actions");
    model.overlay = PTC_UI_OVERLAY_ALBUM_MANAGER;
    check_hit(hit_center(&model, ptc_ui_album_action_rect(0)), PTC_UI_HIT_ALBUM_ACTION, 0,
              "album enable action is touchable inside the manager");
    check_hit(hit_center(&model, ptc_ui_album_action_rect(1)), PTC_UI_HIT_ALBUM_ACTION, 1,
              "album restore action is touchable inside the manager");
    check_hit(hit_center(&model, ptc_ui_album_refresh_rect()), PTC_UI_HIT_ALBUM_REFRESH, 0,
              "album manager refresh is touchable");

    model.overlay = PTC_UI_OVERLAY_NONE;
    model.view = PTC_UI_SETUP;
    model.setup_step = PTC_UI_SETUP_SHORTCUT;
    check_hit(hit_center(&model, ptc_ui_setup_shortcut_card_rect(0)), PTC_UI_HIT_SETUP_SHORTCUT_CARD, 0,
              "setup shortcut preset card");
    check_hit(ptc_ui_hit_test(&model, 900, 540), PTC_UI_HIT_NONE, 0,
              "hidden setup shortcut capture area is inert");
    check_true(!rects_overlap(ptc_ui_setup_shortcut_card_rect(0), ptc_ui_setup_shortcut_card_rect(1)),
               "setup shortcut presets do not overlap");
    check_true(!rects_overlap(ptc_ui_setup_shortcut_card_rect(6), ptc_ui_setup_shortcut_card_rect(13)),
               "two shortcut preset columns do not overlap");
    model.setup_step = PTC_UI_SETUP_PIN;
    check_hit(hit_center(&model, ptc_ui_setup_pin_rect()), PTC_UI_HIT_SETUP_PIN, 0, "setup PIN guide");
    model.setup_step = PTC_UI_SETUP_THEME;
    check_hit(hit_center(&model, ptc_ui_setup_theme_rect(2)), PTC_UI_HIT_SETUP_THEME_OPTION, 2,
              "setup dark theme option has a matching touch target");
    snprintf(model.setup_phase, sizeof(model.setup_phase), "pending");
    check_true(!ptc_ui_setup_takeover_complete(&model), "pending takeover still requires confirmation");
    snprintf(model.setup_phase, sizeof(model.setup_phase), "restored");
    check_true(!ptc_ui_setup_takeover_complete(&model), "restored takeover still requires safe preflight");
    snprintf(model.setup_phase, sizeof(model.setup_phase), "released");
    check_true(ptc_ui_setup_takeover_complete(&model), "released takeover advances without resubmitting");
    snprintf(model.setup_phase, sizeof(model.setup_phase), "active");
    check_true(ptc_ui_setup_takeover_complete(&model), "active takeover advances without error 308");
    model.disable_flag_present = true;
    check_true(!ptc_ui_setup_takeover_complete(&model), "disabled active takeover requires safe preflight");
    check_int(ptc_ui_safety_action_available(&model, 0), PTC_UI_ACTION_RECOMMENDED,
              "disabled active takeover action is recommended");
    model.disable_flag_present = false;
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

    model.overlay = PTC_UI_OVERLAY_SOFTWARE_INFO;
    check_hit(hit_center(&model, ptc_ui_confirm_rect(model.overlay)), PTC_UI_HIT_OVERLAY_CONFIRM, 0,
              "software information close button");
    check_hit(hit_center(&model, ptc_ui_cancel_rect(model.overlay)), PTC_UI_HIT_NONE, 0,
              "software information has no invisible secondary button");
    check_true(ptc_ui_cancel_overlay(&model), "software information closes with the shared modal path");
    check_int(model.overlay, PTC_UI_OVERLAY_NONE, "software information close returns to support");

    model.overlay = PTC_UI_OVERLAY_NUMPAD;
    model.numpad_purpose = PTC_UI_NUMPAD_WEEKLY_MINUTES;
    check_hit(hit_center(&model, ptc_ui_numpad_quick_rect(2)), PTC_UI_HIT_NUMPAD_QUICK, 2,
              "numpad quick increase button");
    model.overlay = PTC_UI_OVERLAY_MINUTE_EDITOR;
    check_hit(hit_center(&model, ptc_ui_minute_editor_key_rect(10)), PTC_UI_HIT_NUMPAD_KEY, 10,
              "compact minute editor zero key is touchable");
    check_hit(hit_center(&model, ptc_ui_minute_editor_quick_rect(0)), PTC_UI_HIT_NUMPAD_QUICK, 0,
              "compact minute editor quick adjustment is touchable");
    check_true(ptc_ui_minute_editor_key_rect(2).x + ptc_ui_minute_editor_key_rect(2).w < 716,
               "compact minute editor keypad stays left of the right-side information panel");
    check_true(ptc_ui_minute_editor_quick_rect(3).x + ptc_ui_minute_editor_quick_rect(3).w < 716,
               "compact minute editor quick actions stay left of the information panel");

    model.overlay = PTC_UI_OVERLAY_CREDENTIAL;
    model.credential_kind = 1;
    check_hit(hit_center(&model, ptc_ui_credential_random_rect()), PTC_UI_HIT_CREDENTIAL_RANDOM, 0,
              "credential random button");
    check_hit(hit_center(&model, ptc_ui_credential_reveal_rect()), PTC_UI_HIT_NONE, 0,
              "device-name editor has no invisible secret reveal target");
    model.overlay = PTC_UI_OVERLAY_GRANT_MANAGER;
    check_hit(hit_center(&model, ptc_ui_grant_manager_card_rect(PTC_UI_GRANT_MANAGER_DEVICE)),
              PTC_UI_HIT_GRANT_MANAGER_CARD, PTC_UI_GRANT_MANAGER_DEVICE,
              "grant manager exposes device-name management");
    check_hit(hit_center(&model, ptc_ui_grant_manager_card_rect(PTC_UI_GRANT_MANAGER_EXPORT)),
              PTC_UI_HIT_GRANT_MANAGER_CARD, PTC_UI_GRANT_MANAGER_EXPORT,
              "grant manager exposes parent config export");
    check_hit(hit_center(&model, ptc_ui_grant_manager_card_rect(PTC_UI_GRANT_MANAGER_RESET_URL)),
              PTC_UI_HIT_GRANT_MANAGER_CARD, PTC_UI_GRANT_MANAGER_RESET_URL,
              "grant manager exposes default URL restore");
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
        "\"played_minutes\":20,\"play_timer_enabled\":1,\"restricted_now\":0,"
        "\"rule_source\":\"statutory_holiday\",\"calendar_covered\":true,\"calendar_update_warning\":false},"
        "\"setup\":{\"phase\":\"released\",\"restriction_cleared\":true,\"snapshot_available\":true,"
        "\"activate_after\":105,\"compatibility_status\":\"verified\"},\"completed_at\":100}";
    const char *active =
        "{\"version\":1,\"request_id\":\"status\",\"type\":\"status\",\"status\":\"ok\","
        "\"state\":{\"day_index\":1,\"limited_today\":1,\"blocked_today\":0,\"unrestricted_today\":0,"
        "\"remaining_available\":true,\"remaining_minutes\":40,\"played_minutes_available\":true,"
        "\"played_minutes\":20,\"play_timer_enabled\":1,\"restricted_now\":0,"
        "\"rule_source\":\"statutory_holiday\",\"calendar_covered\":true,\"calendar_update_warning\":false},"
        "\"setup\":{\"phase\":\"active\",\"restriction_cleared\":true,\"snapshot_available\":true,"
        "\"activate_after\":0,\"compatibility_status\":\"verified\"},"
        "\"environment\":{\"available\":true,\"hos\":\"22.5.0\",\"model\":\"mariko-oled\",\"atmosphere\":true},"
        "\"recent_events\":[{\"ts\":4294967296,\"request_id\":\"weekly-1\",\"type\":\"set_weekly_template\","
        "\"event\":\"result_ok\",\"error\":\"\",\"detail\":\"rules\"}],\"completed_at\":106}";
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
    const char *weekly_saved =
        "{\"version\":1,\"request_id\":\"weekly-ok\",\"type\":\"set_weekly_template\",\"status\":\"ok\","
        "\"state\":{\"day_index\":1,\"limited_today\":1,\"blocked_today\":0,\"unrestricted_today\":0,"
        "\"remaining_available\":true,\"remaining_minutes\":40,\"played_minutes_available\":true,"
        "\"played_minutes\":20,\"play_timer_enabled\":1,\"restricted_now\":0},\"completed_at\":110}";
    const char *holiday_saved =
        "{\"version\":1,\"request_id\":\"holiday-ok\",\"type\":\"set_holiday_policy\",\"status\":\"ok\","
        "\"state\":{\"day_index\":1,\"limited_today\":1,\"blocked_today\":0,\"unrestricted_today\":0,"
        "\"remaining_available\":true,\"remaining_minutes\":40,\"played_minutes_available\":true,"
        "\"played_minutes\":20,\"play_timer_enabled\":1,\"restricted_now\":0,\"rule_source\":\"weekly\","
        "\"calendar_covered\":true},\"completed_at\":111}";

    memset(&model, 0, sizeof(model));
    check_true(ptc_ui_apply_result_json(&model, pending), "syncing result parses");
    check_int(model.view, PTC_UI_SETUP, "syncing result opens setup status");
    check_true(strcmp(model.setup_phase, "released") == 0, "syncing phase mapped");
    check_int((int)ptc_ui_setup_grace_remaining(&model, 100), 5, "sync countdown mapped");
    check_true(ptc_ui_apply_result_json(&model, active), "active result parses");
    check_int(model.view, PTC_UI_CHILD, "active result opens child home");
    check_true(strcmp(model.mode, "额度管理") == 0, "release product language is stable");
    check_int(model.remaining_minutes, 40, "remaining minutes mapped");
    check_true(strcmp(model.rule_source, "statutory_holiday") == 0 && model.calendar_covered,
        "effective holiday source and coverage map to the UI model");
    check_true(strcmp(model.compatibility_status, "verified") == 0,
        "compatibility status maps to the UI model");
    check_true(model.environment_available && strcmp(model.environment_hos, "22.5.0") == 0 &&
               strcmp(model.environment_model, "mariko-oled") == 0 && model.environment_atmosphere,
               "support environment fields map independently for multiline display");
    check_true(model.recent_events_available && model.recent_event_count == 1 &&
               strstr(model.recent_events[0], "操作已完成") != NULL &&
               strcmp(model.recent_event_types[0], "set_weekly_template") == 0 &&
               strcmp(model.recent_event_request_ids[0], "weekly-1") == 0 &&
               model.recent_event_timestamps[0] == INT64_C(4294967296),
               "recent event keeps readable summary and complete diagnostic detail");
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
    check_true(ptc_ui_apply_result_json(&model, weekly_saved), "weekly success result parses");
    check_true(strstr(model.message, "周计划已保存") != NULL && strstr(model.message, "后台") == NULL &&
               model.feedback_detail[0] != '\0',
               "weekly success message gives a parent-facing conclusion and basis");
    model.draft_holiday_enabled = false;
    check_true(ptc_ui_apply_result_json(&model, holiday_saved), "disabled holiday success result parses");
    check_true(strstr(model.message, "未启用") != NULL && strstr(model.message, "重新计算") == NULL,
               "disabled holiday save does not claim that today's rule changed");
    model.draft_holiday_enabled = true;
    check_true(ptc_ui_apply_result_json(&model, holiday_saved), "enabled holiday success result parses");
    check_true(strstr(model.message, "普通日期") != NULL && strstr(model.message, "不受影响") != NULL &&
               strstr(model.feedback_detail, "周计划") != NULL,
               "enabled ordinary-date holiday save gives the direct result and basis");
}

int main(void)
{
    test_theme_resolution();
    test_parent_status_summary();
    test_release_navigation();
    test_shortcut_hold_and_setup_migration();
    test_rule_result_guidance();
    test_numeric_input();
    test_time_previews();
    test_candidate_navigation();
    test_release_hit_targets();
    test_user_state_mapping();
    if (failures) return 1;
    puts("PTC release UI state tests passed");
    return 0;
}
