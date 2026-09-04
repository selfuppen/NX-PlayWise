/* Deterministic host screenshots of the real renderer, not a reimplemented UI. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
static time_t preview_time(time_t *out) { if (out) *out = 1000; return 1000; }
#define time preview_time
#include "../../companion/nro/ui_graphics.c"
#undef time

uint32_t preview_pixels[1280 * 720];

static int save_preview(const char *directory, const char *name, const PtcUiModel *model, bool dark)
{
    char path[512];
    FILE *file;
    PtcUiThemeView theme = ptc_ui_theme_make_view(dark ? PTC_UI_THEME_DARK : PTC_UI_THEME_LIGHT, PTC_UI_SYSTEM_THEME_UNAVAILABLE);
    ptc_ui_graphics_draw(model, &theme);
    draw_text(preview_pixels, 1280, 820, 22, "HOST PREVIEW / SAMPLE DATA", 16, UI_RGB(g_palette->text_secondary));
    snprintf(path, sizeof(path), "%s/%s-%s.ppm", directory, name, dark ? "dark" : "light");
    file = fopen(path, "wb");
    if (!file) return 1;
    fprintf(file, "P6\n1280 720\n255\n");
    for (int i = 0; i < 1280 * 720; ++i) {
        uint32_t pixel = preview_pixels[i];
        unsigned char rgb[] = {pixel & 255, (pixel >> 8) & 255, (pixel >> 16) & 255};
        if (fwrite(rgb, 1, 3, file) != 3) { fclose(file); return 1; }
    }
    return fclose(file) != 0;
}

int main(int argc, char **argv)
{
    FILE *font;
    long length;
    unsigned char *bytes;
    PtcUiModel model;
    int failed = 0;
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
    model.view = PTC_UI_CHILD;
    model.remaining_minutes = 0;
    model.temporary_unlocked_available = model.temporary_unlocked = true;
    snprintf(model.result_status, sizeof(model.result_status), "error");
    snprintf(model.message, sizeof(model.message), "设置未完成，请等待状态同步后重试。当前设置尚未确认生效，请家长到支持与恢复查看详细原因。");
    snprintf(model.feedback_detail, sizeof(model.feedback_detail), "请勿重复提交；状态刷新和诊断仍然可用。若需要继续使用，请家长确认当前限制和剩余额度。");
    failed |= save_preview(argv[2], "child-error", &model, false);
    failed |= save_preview(argv[2], "child-error", &model, true);
    FT_Done_Face(g_ui.face);
    free(bytes);
    return failed;
}
