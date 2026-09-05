/* Deterministic host screenshots of the real renderer, not a reimplemented UI. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
static time_t preview_time(time_t *out) { if (out) *out = 1000; return 1000; }
#define time preview_time
/* Pin the animation clock so breathing/fade phases render identically on every run.
 * The value must be a multiple of 1280 to keep the focus-ring breathing at phase 0. */
#define PTC_UI_PREVIEW_ANIM_CLOCK_MS 1024000
#include "../../companion/nro/ui_graphics.c"
#undef time

uint32_t preview_pixels[1280 * 720];

static int check_primitives(void)
{
    const uint32_t background = pack_rgb(0x182838), ink = pack_rgb(0xe0c0a0);
    UiRect rect = {20, 20, 96, 64};
    int failed = 0, mixed = 0;
    g_palette = ptc_ui_theme_palette(PTC_UI_RESOLVED_LIGHT);
    fill_rect_packed(preview_pixels, 1280, (UiRect){0, 0, 1280, 720}, background);
    fill_round_rect(preview_pixels, 1280, rect, 16, UI_RGB(0xe0c0a0));
    for (int y = 0; y < rect.height; ++y) {
        for (int x = 0; x < rect.width; ++x) {
            uint32_t pixel = preview_pixels[(rect.y + y) * 1280 + rect.x + x];
            if (pixel != preview_pixels[(rect.y + y) * 1280 + rect.x + rect.width - 1 - x] ||
                pixel != preview_pixels[(rect.y + rect.height - 1 - y) * 1280 + rect.x + x]) ++failed;
            if (pixel != background && pixel != ink) ++mixed;
        }
    }
    if (!mixed || preview_pixels[20 * 1280 + 20] != background || preview_pixels[40 * 1280 + 40] != ink) ++failed;
    /* A stroke must not erase text or other content inside its inner contour. */
    preview_pixels[40 * 1280 + 40] = pack_rgb(0xff00ff);
    draw_rect_outline(preview_pixels, 1280, rect, 16, 3, UI_RGB(0xabcdef));
    if (preview_pixels[40 * 1280 + 40] != pack_rgb(0xff00ff) || preview_pixels[20 * 1280 + 20] != background) ++failed;
    if (preview_pixels[21 * 1280 + 60] != pack_rgb(0xabcdef) || preview_pixels[40 * 1280 + 21] != pack_rgb(0xabcdef)) ++failed;
    draw_focus_ring(preview_pixels, 1280, rect, 16);
    if (preview_pixels[40 * 1280 + 19] != background ||
        preview_pixels[40 * 1280 + 17] != resolve_color(UI_FOCUS) ||
        preview_pixels[40 * 1280 + 40] != pack_rgb(0xff00ff)) ++failed;
    /* Requested radii remain distinct even on large cards. */
    fill_round_rect(preview_pixels, 1280, (UiRect){200, 20, 96, 64}, 8, UI_RGB(0xe0c0a0));
    if (preview_pixels[22 * 1280 + 202] == preview_pixels[22 * 1280 + 22]) ++failed;
    fill_rect_packed(preview_pixels, 1280, (UiRect){0, 0, 1280, 720}, background);
    draw_circle_outline(preview_pixels, 1280, 100, 100, 16, 3, UI_RGB(0xe0c0a0));
    mixed = 0;
    for (int y = -17; y <= 17; ++y) {
        for (int x = -17; x <= 17; ++x) {
            uint32_t pixel = preview_pixels[(100 + y) * 1280 + 100 + x];
            if (pixel != preview_pixels[(100 - y) * 1280 + 100 - x]) ++failed;
            if (pixel != ink && pixel != background) ++mixed;
        }
    }
    if (!mixed || preview_pixels[100 * 1280 + 100] != background) ++failed;
    /* Non-default stride and off-screen shapes must preserve all guard pixels. */
    uint32_t stride = 1284;
    uint32_t *guarded = malloc(stride * 722 * sizeof(*guarded));
    if (!guarded) return 1;
    for (unsigned int i = 0; i < stride * 722; ++i) guarded[i] = background;
    uint32_t *canvas = guarded + stride + 2;
    fill_round_rect(canvas, stride, (UiRect){-18, -15, 80, 60}, 16, UI_RGB(0xffffff));
    draw_rect_outline(canvas, stride, (UiRect){1250, 680, 90, 90}, 16, 3, UI_RGB(0xffffff));
    draw_circle_outline(canvas, stride, 0, 719, 20, 3, UI_RGB(0xffffff));
    fill_round_rect(canvas, stride, (UiRect){1279, 0, 1, 1}, 16, UI_RGB(0xffffff));
    draw_rect_outline(canvas, stride, (UiRect){1279, 0, 1, 1}, 16, 3, UI_RGB(0xffffff));
    fill_round_rect(canvas, stride, (UiRect){0, 0, 0, 0}, 16, UI_RGB(0xffffff));
    for (unsigned int y = 0; y < 722; ++y)
        for (unsigned int x = 0; x < stride; ++x)
            if ((y == 0 || y == 721 || x < 2 || x >= 1282) && guarded[y * stride + x] != background) ++failed;
    free(guarded);
    printf("%s: UI primitive coverage, symmetry, strokes and clipping\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}

static int save_preview(const char *directory, const char *name, const PtcUiModel *model, bool dark)
{
    char path[512];
    FILE *file;
    unsigned char *rgb;
    PtcUiThemeView theme = ptc_ui_theme_make_view(dark ? PTC_UI_THEME_DARK : PTC_UI_THEME_LIGHT, PTC_UI_SYSTEM_THEME_UNAVAILABLE);
    /* Previews skip update_animations, so render the tweened values at rest. */
    PtcUiModel settled = *model;
    settled.displayed_remaining_minutes = settled.remaining_minutes;
    settled.displayed_grant_minutes = settled.grant_minutes;
    settled.last_parent_page = settled.parent_page;
    ptc_ui_graphics_draw(&settled, &theme);
    draw_text(preview_pixels, 1280, 820, 22, "HOST PREVIEW / SAMPLE DATA", 16, UI_RGB(g_palette->text_secondary));
    snprintf(path, sizeof(path), "%s/%s-%s.ppm", directory, name, dark ? "dark" : "light");
    file = fopen(path, "wb");
    if (!file) return 1;
    rgb = malloc(1280 * 720 * 3);
    if (!rgb) { fclose(file); return 1; }
    fprintf(file, "P6\n1280 720\n255\n");
    for (int y = 0; y < 720; ++y) {
        for (int x = 0; x < 1280; ++x) {
            uint32_t pixel = preview_pixels[y * 1280 + x];
            int offset = (y * 1280 + x) * 3;
            rgb[offset] = pixel & 255;
            rgb[offset + 1] = (pixel >> 8) & 255;
            rgb[offset + 2] = (pixel >> 16) & 255;
        }
    }
    /* One frame write avoids hundreds of small writes across a Docker bind mount. */
    bool written = fwrite(rgb, 1, 1280 * 720 * 3, file) == 1280 * 720 * 3;
    free(rgb);
    if (!written) { fclose(file); return 1; }
    return fclose(file) != 0;
}

static int render_visual_matrix(const char *directory, const PtcUiModel *baseline)
{
    static const char *titles[] = {
        "", "调整今日额度", "编辑周计划", "确认本次修改", "输入加时码", "加时密钥管理",
        "加时码生成管理", "手机和电脑配对", "保留周计划草稿？", "家长区快捷键", "本机生成加时码",
        "保留密钥更改？", "兑换结果", "验证未通过", "软件信息", "节假日安排", "保留节假日草稿？",
        "支持事件详情", "批量设置", "自制程序菜单高级入口", "调整时长", "外观主题", "输入家长 PIN",
        "加时码使用记录", "临时日期计划", "今日自主缓冲", "家庭活动记录", "今日详情", "保留日期计划草稿？"
    };
    int failed = 0;
    for (int dark = 0; dark < 2; ++dark) {
        for (int overlay = PTC_UI_OVERLAY_MINUTES; overlay <= PTC_UI_OVERLAY_SCHEDULED_LEAVE; ++overlay) {
            PtcUiModel model = *baseline;
            char name[48];
            PtcRules rules;
            uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
            ptc_rules_default(&rules);
            memcpy(model.current_week, rules.week, sizeof(model.current_week));
            memcpy(model.draft_week, rules.week, sizeof(model.draft_week));
            model.view = PTC_UI_PARENT;
            model.overlay = (PtcUiOverlay)overlay;
            model.minimum_minutes = 1; model.maximum_minutes = 1440; model.draft_minutes = 120;
            model.grant_minutes = 30; model.grant_max_minutes = 240;
            model.numpad_purpose = PTC_UI_NUMPAD_OFFLINE_CODE;
            model.numpad_max_digits = 8;
            model.daily_buffer_minutes = 10;
            model.draft_autonomy_policy.daily_buffer_minutes = 10;
            model.draft_scheduled_override = (PtcScheduledOverride){true, 2380, 2386, {PTC_RULE_MODE_LIMIT, 120}};
            model.redemption_history_available = model.activity_history_available = true;
            snprintf(model.overlay_title, sizeof(model.overlay_title), "%s", titles[overlay]);
            snprintf(model.numpad_title, sizeof(model.numpad_title), "输入加时码");
            snprintf(model.numpad_text, sizeof(model.numpad_text), "1234");
            snprintf(model.pin_title, sizeof(model.pin_title), "输入家长 PIN");
            snprintf(model.pin_guide, sizeof(model.pin_guide), "验证后继续当前操作");
            snprintf(model.pin_text, sizeof(model.pin_text), "1234");
            snprintf(model.auth_error_title, sizeof(model.auth_error_title), "PIN 不正确");
            snprintf(model.auth_error_message, sizeof(model.auth_error_message), "请检查后重新输入");
            snprintf(model.pairing_base_url, sizeof(model.pairing_base_url), "https://example.invalid/playwise");
            if (!qrcodegen_encodeText(model.pairing_base_url, temp, model.qr_code,
                    qrcodegen_Ecc_LOW, 1, 20, qrcodegen_Mask_AUTO, true)) return 1;
            snprintf(name, sizeof(name), "matrix-overlay-%02d", overlay);
            failed |= save_preview(directory, name, &model, dark != 0);
        }
        for (int parent = 0; parent < 2; ++parent) {
            for (int state = 0; state < 6; ++state) {
                PtcUiModel model = *baseline;
                char name[48];
                model.view = parent ? PTC_UI_PARENT : PTC_UI_CHILD;
                if (state == 0) {
                    model.status_loaded = model.remaining_available = model.played_minutes_available = model.forecast_available = false;
                    snprintf(model.message, sizeof(model.message), "等待读取主机状态");
                }
                if (state == 1) model.remaining_minutes = 0;
                if (state == 2) { model.unrestricted_today = 1; model.limited_today = 0; }
                if (state == 3) model.status_updated_at = 879;
                if (state == 4) { model.waiting = true; snprintf(model.message, sizeof(model.message), "正在同步，请稍候"); }
                if (state == 5) { model.remaining_minutes = 1440; model.played_minutes = 0; model.forecast[0].minutes = 1440; }
                snprintf(name, sizeof(name), "matrix-%s-state-%d", parent ? "parent" : "child", state);
                failed |= save_preview(directory, name, &model, dark != 0);
                ptc_ui_open_home_details(&model);
                if (model.overlay == PTC_UI_OVERLAY_HOME_DETAILS) {
                    snprintf(name, sizeof(name), "details-%s-state-%d", parent ? "parent" : "child", state);
                    failed |= save_preview(directory, name, &model, dark != 0);
                }
            }
        }
        for (int state = 0; state < 6; ++state) {
            PtcUiModel details = *baseline;
            char name[64];
            details.view = PTC_UI_PARENT;
            details.parent_page = PTC_UI_PARENT_TODAY;
            if (state == 0) details.usage_summary_available = details.played_minutes_available = false;
            if (state == 1) details.disable_flag_present = true;
            if (state == 2) details.apply_pending_confirmation = true;
            if (state == 3) {
                details.usage_known_days_7 = details.usage_known_days_30 = 0;
                details.usage_consumed_minutes_7 = details.usage_consumed_minutes_30 = 0;
            }
            if (state == 4) snprintf(details.setup_phase, sizeof(details.setup_phase), "protection");
            if (state == 5) {
                details.disable_flag_present = true;
                snprintf(details.result_status, sizeof(details.result_status), "error");
                snprintf(details.message, sizeof(details.message), "设置未完成，请等待状态同步后重试。当前设置尚未确认生效，请家长到支持与恢复查看详细原因。请勿重复提交，请先确认系统当前状态。");
                snprintf(details.feedback_detail, sizeof(details.feedback_detail), "请勿重复提交；状态刷新和诊断仍然可用。若需要继续使用，请家长确认当前限制和剩余额度，检查后台是否正在恢复。");
            }
            ptc_ui_open_home_details(&details);
            snprintf(name, sizeof(name), "details-special-%d", state);
            failed |= save_preview(directory, name, &details, dark != 0);
        }
        PtcUiModel model = *baseline;
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_SETTINGS;
        model.settings_page = PTC_UI_SETTINGS_ADVANCED;
        failed |= save_preview(directory, "settings-advanced", &model, dark != 0);
        model.view = PTC_UI_ERROR;
        model.error_code = 306;
        snprintf(model.message, sizeof(model.message), "主机环境已变化，请家长重新检测");
        failed |= save_preview(directory, "error-page", &model, dark != 0);
    }
    return failed;
}

int main(int argc, char **argv)
{
    FILE *font;
    long length;
    unsigned char *bytes;
    PtcUiModel model;
    int failed = 0;
    if (argc == 2 && strcmp(argv[1], "--check-primitives") == 0) return check_primitives();
    if (argc != 3 || !(font = fopen(argv[1], "rb"))) return 1;
    fseek(font, 0, SEEK_END); length = ftell(font); rewind(font);
    if (length <= 0 || !(bytes = malloc((size_t)length))) { fclose(font); return 1; }
    if (fread(bytes, 1, length, font) != (size_t)length) { fclose(font); free(bytes); return 1; }
    fclose(font);
    if (FT_New_Memory_Face(NULL, bytes, length, 0, &g_ui.face)) { free(bytes); return 1; }
    g_ui.font_ready = g_ui.framebuffer_ready = true;
    memset(&model, 0, sizeof(model));
    model.view = PTC_UI_CHILD;
    model.status_loaded = model.remaining_available = model.played_minutes_available = true;
    model.status_updated_at = 998;
    model.remaining_minutes = 51;
    model.played_minutes = 69;
    model.limited_today = model.play_timer_enabled = 1;
    model.restriction_enabled_available = model.restriction_enabled = true;
    model.day_index = 2380;
    model.forecast_available = true;
    model.forecast[0] = (PtcResultForecastDay){.day_index=2380, .mode=PTC_RULE_MODE_LIMIT, .minutes=120, .rule_source="weekly"};
    model.forecast[1] = (PtcResultForecastDay){.day_index=2381, .mode=PTC_RULE_MODE_LIMIT, .minutes=90, .rule_source="weekly"};
    model.daily_buffer_available = true;
    model.daily_buffer_minutes = 10;
    model.usage_summary_available = true;
    model.usage_known_days_7 = 5; model.usage_consumed_minutes_7 = 350;
    model.usage_known_days_30 = 20; model.usage_consumed_minutes_30 = 1420;
    snprintf(model.setup_phase, sizeof(model.setup_phase), "active");
    snprintf(model.rule_source, sizeof(model.rule_source), "weekly");
    snprintf(model.command_name, sizeof(model.command_name), "刷新状态");
    snprintf(model.transport_label, sizeof(model.transport_label), "传输：IPC");
    snprintf(model.message, sizeof(model.message), "今天的状态已更新");
    snprintf(model.result_status, sizeof(model.result_status), "ok");
    const PtcUiModel baseline = model;
    for (int dark = 0; dark <= 1; ++dark) {
        model = baseline;
        model.view = PTC_UI_CHILD;
        failed |= save_preview(argv[2], "child", &model, dark);
        ptc_ui_open_home_details(&model);
        failed |= save_preview(argv[2], "child-details", &model, dark);
        ptc_ui_cancel_overlay(&model);
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_TODAY;
        failed |= save_preview(argv[2], "parent", &model, dark);
        ptc_ui_open_home_details(&model);
        failed |= save_preview(argv[2], "parent-details", &model, dark);
        ptc_ui_cancel_overlay(&model);
        model.parent_page = PTC_UI_PARENT_GRANT;
        failed |= save_preview(argv[2], "grant-entry", &model, dark);
        model.overlay = PTC_UI_OVERLAY_GRANT_LOCAL;
        model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
        model.grant_minutes = 20;
        model.grant_has_code = false;
        snprintf(model.overlay_title, sizeof(model.overlay_title), "本机生成 8 位加时码");
        failed |= save_preview(argv[2], "grant-empty", &model, dark);
        model.grant_has_code = true;
        model.grant_issued_minutes = 20;
        model.grant_day_index = 2380;
        model.grant_estimate_available = true;
        model.grant_estimate_minutes = 71;
        model.grant_minutes = 240;
        snprintf(model.grant_code, sizeof(model.grant_code), "12345678");
        failed |= save_preview(argv[2], "grant-issued", &model, dark);
        model.played_minutes = 1430;
        model.remaining_minutes = 5;
        model.grant_estimate_minutes = 10;
        model.grant_estimate_capped = true;
        failed |= save_preview(argv[2], "grant-capped", &model, dark);
        model.status_updated_at = 879;
        failed |= save_preview(argv[2], "grant-stale", &model, dark);
        model.status_updated_at = 998;
        model.played_minutes = 69;
        model.remaining_minutes = 51;
        model.grant_estimate_minutes = 71;
        model.grant_estimate_capped = false;
        model.grant_status_refresh_failed = true;
        snprintf(model.grant_notice, sizeof(model.grant_notice), "保存失败，请返回检查 SD 卡空间后重试；旧码未撤销。");
        failed |= save_preview(argv[2], "grant-error", &model, dark);
        model.grant_status_refresh_failed = false;
        model.grant_notice[0] = '\0';
        ptc_ui_cancel_overlay(&model);
        model.view = PTC_UI_CHILD;
        ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_OFFLINE_CODE, PTC_UI_OVERLAY_NONE,
            "输入加时码", "输入家长给你的 8 位码，确认前会先显示加时预览。", 8, 0, 0, 0);
        snprintf(model.numpad_text, sizeof(model.numpad_text), "12345678");
        failed |= save_preview(argv[2], "redeem-input", &model, dark);
        ptc_ui_cancel_overlay(&model);
        model.confirm_hold_required = false;
        model.operation = PTC_UI_OPERATION_REDEEM_OFFLINE_CODE;
        model.overlay = PTC_UI_OVERLAY_CONFIRM;
        model.code_grant_minutes = 30;
        model.code_preview_after_available = true;
        model.code_preview_after_minutes = 56;
        model.code_effective_add_minutes = 5;
        model.code_preview_capped = true;
        snprintf(model.overlay_title, sizeof(model.overlay_title), "确认兑换加时码");
        failed |= save_preview(argv[2], "redeem-confirm", &model, dark);
        model.confirm_hold_required = true;
        model.code_preview_after_minutes = 0;
        failed |= save_preview(argv[2], "redeem-confirm-hold", &model, dark);
        model.code_preview_after_minutes = 56;
        model.overlay = PTC_UI_OVERLAY_CODE_RESULT;
        model.code_actual_add_available = true;
        model.code_actual_add_minutes = 5;
        model.remaining_minutes = 56;
        failed |= save_preview(argv[2], "redeem-success-capped", &model, dark);
        model.code_actual_add_minutes = 30;
        failed |= save_preview(argv[2], "redeem-success", &model, dark);
        model.status_updated_at = 880;
        failed |= save_preview(argv[2], "redeem-age-120", &model, dark);
        model.status_updated_at = 879;
        failed |= save_preview(argv[2], "redeem-old-result", &model, dark);
        model.status_updated_at = 998;
        model.code_actual_add_available = false;
        failed |= save_preview(argv[2], "redeem-missing-record", &model, dark);
        model.code_result_pending = true;
        failed |= save_preview(argv[2], "redeem-pending", &model, dark);
        model.code_result_pending = false;
        model.code_result_failed = true;
        snprintf(model.result_status, sizeof(model.result_status), "error");
        failed |= save_preview(argv[2], "redeem-failed", &model, dark);
        model.error_code = 206;
        failed |= save_preview(argv[2], "redeem-used", &model, dark);
        model.error_code = 205;
        failed |= save_preview(argv[2], "redeem-wrong-date", &model, dark);
        model.error_code = 501;
        failed |= save_preview(argv[2], "redeem-storage-error", &model, dark);
        model.error_code = 0;
        model.code_result_failed = false;
        snprintf(model.result_status, sizeof(model.result_status), "ok");
        model.remaining_minutes = 51;
        ptc_ui_cancel_overlay(&model);
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_TODAY;
        model.operation = PTC_UI_OPERATION_SET_TODAY_LIMIT;
        ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_MINUTES, PTC_UI_OVERLAY_NONE,
            "设置今日总额度", "全天总额度包含今日额度消耗；右侧显示调整前后的可玩时间。", 4, 1, 1440, 1440);
        failed |= save_preview(argv[2], "quota-editor", &model, dark);
        ptc_ui_cancel_overlay(&model);
        model.overlay = PTC_UI_OVERLAY_CONFIRM;
        model.confirm_hold_required = true;
        snprintf(model.overlay_title, sizeof(model.overlay_title), "设置后可能立即限制");
        snprintf(model.overlay_body, sizeof(model.overlay_body), "全天总额度包含今日额度消耗。新额度可能已经耗尽，保存后可能立即进入时间限制。可通过临时加时、今日不限时或兑换加时码解除。");
        failed |= save_preview(argv[2], "confirmation", &model, dark);
        ptc_ui_cancel_overlay(&model);
    }
    for (int dark = 0; dark <= 1; ++dark) {
        PtcRules rules;
        ptc_rules_default(&rules);
        model = baseline;
        model.view = PTC_UI_PARENT;
        memcpy(model.current_week, rules.week, sizeof(rules.week));
        memcpy(model.draft_week, rules.week, sizeof(rules.week));
        model.parent_page = PTC_UI_PARENT_PLAN;
        for (int slot = 0; slot < 7; ++slot)
            if (ptc_ui_weekday_for_display_slot(slot) == ptc_weekday_from_day_index(model.day_index)) model.weekly_grid_slot = slot;
        failed |= save_preview(argv[2], "plan-saved", &model, dark);
        model.draft_week[ptc_weekday_from_day_index(model.day_index)].minutes = 90;
        model.weekly_dirty = true;
        failed |= save_preview(argv[2], "plan-draft-today", &model, dark);
        model.scheduled_override = (PtcScheduledOverride){true, 2380, 2386, {PTC_RULE_MODE_LIMIT, 120}};
        snprintf(model.rule_source, sizeof(model.rule_source), "scheduled_override");
        model.selected_index = 4;
        failed |= save_preview(argv[2], "plan-covered", &model, dark);
        model.editor_index = ptc_weekday_from_day_index(model.day_index);
        ptc_ui_numpad_open(&model, PTC_UI_NUMPAD_WEEKLY_MINUTES, PTC_UI_OVERLAY_NONE,
            "调整周计划额度", "完成输入后更新草稿，保存计划后才会应用。", 4, 1, 1440, 90);
        failed |= save_preview(argv[2], "plan-minute-editor", &model, dark);
        ptc_ui_cancel_overlay(&model);
        snprintf(model.result_status, sizeof(model.result_status), "error");
        snprintf(model.message, sizeof(model.message), "计划保存失败，修改仍保留，请检查后重试。");
        model.error_code = 501;
        failed |= save_preview(argv[2], "plan-failed", &model, dark);
        model.error_code = 0;
        snprintf(model.result_status, sizeof(model.result_status), "ok");
        snprintf(model.message, sizeof(model.message), "已读取保存的计划");
        model.parent_page = PTC_UI_PARENT_HOLIDAY;
        model.holiday_rule = model.draft_holiday_rule = rules.holiday_rule;
        model.makeup_workday_rule = model.draft_makeup_workday_rule = rules.makeup_workday_rule;
        model.draft_holiday_enabled = true;
        model.holiday_dirty = true;
        model.selected_index = 1;
        failed |= save_preview(argv[2], "holiday-draft", &model, dark);
        model.disable_flag_present = true;
        failed |= save_preview(argv[2], "holiday-disabled", &model, dark);
        model.disable_flag_present = false;
        model.parent_page = PTC_UI_PARENT_SETTINGS;
        model.settings_page = PTC_UI_SETTINGS_ADVANCED;
        model.draft_scheduled_override = model.scheduled_override;
        model.draft_scheduled_override.rule.minutes = 90;
        model.overlay = PTC_UI_OVERLAY_SCHEDULED;
        model.overlay_selection = 1;
        snprintf(model.overlay_title, sizeof(model.overlay_title), "临时日期计划");
        snprintf(model.overlay_body, sizeof(model.overlay_body), "安排一段时间的每日额度，保存后应用；一次保留一个日期区间。");
        failed |= save_preview(argv[2], "scheduled-draft", &model, dark);
        model.draft_scheduled_override.end_day_index = 2745;
        model.draft_scheduled_override.rule.mode = PTC_RULE_MODE_UNLIMITED;
        failed |= save_preview(argv[2], "scheduled-long", &model, dark);
        ptc_ui_cancel_overlay(&model);
        failed |= save_preview(argv[2], "scheduled-leave", &model, dark);
        ptc_ui_cancel_overlay(&model);
        model.error_code = 501;
        snprintf(model.result_status, sizeof(model.result_status), "error");
        failed |= save_preview(argv[2], "scheduled-failed", &model, dark);
        model = baseline;
        model.view = PTC_UI_PARENT;
        model.parent_page = PTC_UI_PARENT_SETTINGS;
        model.selected_index = 3;
        failed |= save_preview(argv[2], "settings-root", &model, dark);
        model.settings_page = PTC_UI_SETTINGS_SUPPORT;
        model.selected_index = 4;
        failed |= save_preview(argv[2], "support-healthy", &model, dark);
        model.disable_flag_present = true;
        model.selected_index = 0;
        failed |= save_preview(argv[2], "support-disabled", &model, dark);
        snprintf(model.setup_phase, sizeof(model.setup_phase), "failed");
        model.selected_index = 1;
        model.recent_events_available = true;
        model.recent_event_count = 3;
        for (int i = 0; i < 3; ++i) {
            snprintf(model.recent_events[i], sizeof(model.recent_events[i]), "规则保存未完成，请查看详情");
            model.recent_event_timestamps[i] = 998;
        }
        failed |= save_preview(argv[2], "support-failed", &model, dark);
        model.waiting = true;
        failed |= save_preview(argv[2], "support-waiting", &model, dark);
        model = baseline;
        model.view = PTC_UI_SETUP;
        snprintf(model.setup_phase, sizeof(model.setup_phase), "pending");
        model.message[0] = '\0';
        for (int step = 1; step <= PTC_UI_SETUP_ZONE; ++step) {
            char name[32];
            model.setup_step = step;
            snprintf(name, sizeof(name), "setup-step-%d", step);
            failed |= save_preview(argv[2], name, &model, dark);
        }
    }
    model = baseline;
    model.view = PTC_UI_CHILD;
    model.remaining_minutes = 0;
    model.temporary_unlocked_available = model.temporary_unlocked = true;
    snprintf(model.result_status, sizeof(model.result_status), "error");
    snprintf(model.message, sizeof(model.message), "设置未完成，请等待状态同步后重试。当前设置尚未确认生效，请家长到支持与恢复查看详细原因。");
    snprintf(model.feedback_detail, sizeof(model.feedback_detail), "请勿重复提交；状态刷新和诊断仍然可用。若需要继续使用，请家长确认当前限制和剩余额度。");
    failed |= save_preview(argv[2], "child-error", &model, false);
    failed |= save_preview(argv[2], "child-error", &model, true);
    failed |= render_visual_matrix(argv[2], &baseline);
    FT_Done_Face(g_ui.face);
    free(bytes);
    return failed;
}
