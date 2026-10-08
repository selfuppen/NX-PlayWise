#include "ui_render_internal.h"

static void draw_config_backup(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const PtcUiTextId GROUPS[] = {
        PTC_UI_T_CONFIG_BACKUP_GROUP_WEEK, PTC_UI_T_CONFIG_BACKUP_GROUP_SCHEDULED,
        PTC_UI_T_CONFIG_BACKUP_GROUP_TODAY, PTC_UI_T_CONFIG_BACKUP_GROUP_BUFFER,
        PTC_UI_T_CONFIG_BACKUP_GROUP_HOLIDAY, PTC_UI_T_CONFIG_BACKUP_GROUP_BEDTIME,
        PTC_UI_T_CONFIG_BACKUP_GROUP_EYE, PTC_UI_T_CONFIG_BACKUP_GROUP_DOCK,
        PTC_UI_T_CONFIG_BACKUP_GROUP_PREFS, PTC_UI_T_CONFIG_BACKUP_GROUP_PIN, PTC_UI_T_CONFIG_BACKUP_GROUP_PAIRING
    };
    fill_rect(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, UI_PAGE);
    draw_text(pixels, stride, 54, 58, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_TITLE), 28, UI_INK);
    draw_text(pixels, stride, 54, 98, model->config_metadata, 16, UI_ACCENT);

    /* 分类 1：基础额度与调整 */
    draw_text(pixels, stride, 54, 158, ptc_ui_text(PTC_UI_T_QUOTA_RULES), 16, UI_INK);

    /* 分类 2：管控策略与日历 */
    draw_text(pixels, stride, 54, 282, ptc_ui_text(PTC_UI_T_SYSTEM_CONTROL), 16, UI_INK);

    /* 分类 3：账户与安全凭据 */
    draw_text(pixels, stride, 54, 454, ptc_ui_text(PTC_UI_T_SAFETY_MANAGEMENT), 16, UI_INK);

    for (int i = 0; i < 16; ++i) {
        UiRect rect = to_uirect(ptc_ui_config_backup_field_rect(i));
        bool disabled = model->waiting || (i < 11 && (!model->config_backup_ready ||
            (i == 2 && !model->config_today_available) || (i == 9 && !model->config_pin_available))) ||
            (i == 14 && (!model->config_backup_ready || !model->config_groups));
        const char *label = i < 11 ? ptc_ui_text(GROUPS[i]) :
            ptc_ui_text(i == 11 ? PTC_UI_T_CONFIG_BACKUP_ALL : i == 12 ? PTC_UI_T_CONFIG_BACKUP_NONE :
                i == 13 ? PTC_UI_T_CONFIG_BACKUP_CREATE : i == 14 ? PTC_UI_T_CONFIG_BACKUP_IMPORT : PTC_UI_T_B_BACK);
        draw_plan_card(pixels, stride, rect, model->overlay_selection == i);
        if (i < 11) {
            UiRect box = {rect.x + 12, rect.y + 11, 20, 20};
            fill_round_rect(pixels, stride, box, 4, (model->config_groups & (1u << i)) ? UI_ACCENT : UI_RAISED);
            draw_rect_outline(pixels, stride, box, 4, 1, disabled ? UI_DISABLED : UI_ACCENT);
            if (model->config_groups & (1u << i)) {
                draw_line(pixels, stride, box.x + 4, box.y + 10, box.x + 8, box.y + 14, 2, UI_ON_ACCENT);
                draw_line(pixels, stride, box.x + 8, box.y + 14, box.x + 16, box.y + 5, 2, UI_ON_ACCENT);
            }
            char fitted[128]; fit_text(fitted, sizeof(fitted), label, 16, rect.width - 56);
            draw_text(pixels, stride, rect.x + 44, rect.y + 27, fitted, 16, disabled ? UI_DISABLED : UI_INK);
        } else if (i == 15) {
            draw_button_label(pixels, stride, rect, label, 16, UI_MUTED);
        } else {
            draw_text_center(pixels, stride, rect, label, 17, disabled ? UI_DISABLED : UI_ACCENT);
        }
    }
    UiRect info = {824, 144, 402, 498};
    draw_plan_card(pixels, stride, info, false);
    draw_wrapped_text(pixels, stride, 842, 174, ptc_ui_text(PTC_UI_T_CONFIG_BACKUP_NOTE), 16, 366, 26, 5, UI_WARNING);
    draw_wrapped_text(pixels, stride, 842, 338, model->config_preview, 16, 366, 26, 8, UI_INK);
    draw_text(pixels, stride, 842, 570, "backups/config-backup.json", 15, UI_MUTED);
    draw_text(pixels, stride, 842, 610, ptc_ui_text(PTC_UI_T_Y_REFRESH), 16, UI_MUTED);
    char fitted[256]; fit_text(fitted, sizeof(fitted), model->message, 14, 910);
    draw_text(pixels, stride, 300, 692, fitted, 14, UI_MUTED);
}

void draw_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    if (model->overlay == PTC_UI_OVERLAY_CONFIG_BACKUP) { draw_config_backup(pixels, stride, model); return; }
    if (draw_account_overlay_surface(pixels, stride, model) ||
        draw_plan_overlay_surface(pixels, stride, model) ||
        draw_support_overlay_surface(pixels, stride, model)) return;

    switch (model->overlay) {
    case PTC_UI_OVERLAY_MINUTES:
        draw_minutes_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_WEEKLY:
        draw_weekly_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_CONFIRM:
        draw_confirm_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_NUMPAD:
        draw_numpad_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_PIN:
        draw_pin_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_MINUTE_EDITOR:
        draw_minute_editor_overlay(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_NOTICE_DETAILS:
        draw_notice_details_dialog(pixels, stride, model);
        break;
    case PTC_UI_OVERLAY_NONE:
    default:
        break;
    }
}
