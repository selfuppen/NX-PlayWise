#ifndef PTC_COMPANION_UI_GRAPHICS_H
#define PTC_COMPANION_UI_GRAPHICS_H

#include "ui_model.h"
#include "ui_theme.h"

bool ptc_ui_graphics_init(void);
void ptc_ui_graphics_exit(void);
void ptc_ui_graphics_draw(const PtcUiModel *model, const PtcUiThemeView *theme);

/* 单调动画时钟（毫秒）。预览构建通过 PTC_UI_PREVIEW_ANIM_CLOCK_MS 固定，
 * 保证宿主渲染逐像素可复现。 */
int64_t ptc_ui_anim_now_ms(void);

#endif
