#include "ui_render_internal.h"

void draw_dialog_shell(
    uint32_t *pixels,
    uint32_t stride,
    const PtcUiModel *model,
    UiRect *dialog,
    int width,
    int height)
{
    bool numeric = model->overlay == PTC_UI_OVERLAY_NUMPAD || model->overlay == PTC_UI_OVERLAY_MINUTE_EDITOR;
    bool pin = model->overlay == PTC_UI_OVERLAY_PIN;
    bool custom_header = model->overlay == PTC_UI_OVERLAY_NOTICE_DETAILS ||
                         model->overlay == PTC_UI_OVERLAY_DAY_DECISION;
    const char *title = custom_header ? "" : (numeric ? model->numpad_title : (pin ? model->pin_title : model->overlay_title));
    const char *description = custom_header ? "" : (numeric ? model->numpad_guide : (pin ? model->pin_guide : model->overlay_body));
    *dialog = to_uirect(ptc_ui_dialog_rect(width, height));
    float open_progress = ui_ease_out((float)model->overlay_open_frames / (float)PTC_UI_OVERLAY_OPEN_FRAMES);
    int y_offset = (int)(((float)PTC_UI_OVERLAY_OPEN_FRAMES - open_progress * (float)PTC_UI_OVERLAY_OPEN_FRAMES) * 2.0f + 0.5f);
    dialog->y += y_offset;

    uint32_t raw_scrim = UI_BLENDED(scrim);
    uint32_t a = (raw_scrim >> 24) & 0xff;
    a = (uint32_t)(a * open_progress + 0.5f);
    uint32_t fade_scrim = (a << 24) | (raw_scrim & 0xffffff);

    fill_rect_packed(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, fade_scrim);
    draw_round_rect_shadow(pixels, stride, *dialog, 16, 16, 76, 5);
    fill_round_rect_gradient(pixels, stride, *dialog, 16, UI_SURFACE,
                             UI_RGB(ui_darken(UI_BLENDED(surface), 4)));
    draw_rect_outline(pixels, stride, *dialog, 16, 1, UI_BORDER);
    fill_round_rect(pixels, stride, (UiRect){dialog->x + 34, dialog->y + 15, 36, 5}, 2, UI_CORAL);
    if (title && title[0]) {
        draw_text(pixels, stride, dialog->x + 34, dialog->y + 54, title, 29, UI_INK);
    }
    if (description && description[0]) {
        draw_wrapped_text(pixels, stride, dialog->x + 34, dialog->y + 88, description,
                          18, dialog->width - 68, 26, 6, UI_MUTED);
    }
}
