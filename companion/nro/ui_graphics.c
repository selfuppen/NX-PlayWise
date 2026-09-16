#include "ui_render_internal.h"

bool ptc_ui_graphics_init(void)
{
    PlFontData font_data;
    Result result;
    memset(&g_ui, 0, sizeof(g_ui));
    ui_glyph_cache_clear();
    g_font_pixel_size = -1;
    result = plInitialize(PlServiceType_User);
    if (R_FAILED(result)) {
        return false;
    }
    g_ui.pl_ready = true;
    result = plGetSharedFontByType(&font_data, PlSharedFontType_ChineseSimplified);
    if (R_FAILED(result) || FT_Init_FreeType(&g_ui.library) != 0) {
        ptc_ui_graphics_exit();
        return false;
    }
    if (FT_New_Memory_Face(
            g_ui.library,
            (const FT_Byte *)font_data.address,
            (FT_Long)font_data.size,
            0,
            &g_ui.face) != 0) {
        ptc_ui_graphics_exit();
        return false;
    }
    g_ui.font_ready = true;
    result = framebufferCreate(
        &g_ui.framebuffer,
        nwindowGetDefault(),
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        PIXEL_FORMAT_RGBA_8888,
        2);
    if (R_FAILED(result)) {
        ptc_ui_graphics_exit();
        return false;
    }
    g_ui.framebuffer_ready = true;
    result = framebufferMakeLinear(&g_ui.framebuffer);
    if (R_FAILED(result)) {
        ptc_ui_graphics_exit();
        return false;
    }
    return true;
}

void ptc_ui_graphics_exit(void)
{
    ui_glyph_cache_clear();
    g_font_pixel_size = -1;
    free(g_background_cache);
    g_background_cache = NULL;
    g_background_palette = NULL;
    if (g_ui.framebuffer_ready) {
        framebufferClose(&g_ui.framebuffer);
    }
    if (g_ui.font_ready) {
        FT_Done_Face(g_ui.face);
    }
    if (g_ui.library) {
        FT_Done_FreeType(g_ui.library);
    }
    if (g_ui.pl_ready) {
        plExit();
    }
    memset(&g_ui, 0, sizeof(g_ui));
}

void ptc_ui_graphics_draw(const PtcUiModel *model, const PtcUiThemeView *theme)
{
    uint32_t stride_bytes = 0;
    uint32_t *pixels;
    uint32_t stride;
    if (!model || !theme || !theme->palette || !g_ui.framebuffer_ready) {
        return;
    }
    g_palette = theme->palette;
    g_theme = *theme;
#ifndef PTC_UI_PREVIEW_ANIM_CLOCK_MS
    /* palette 指针变化即主题切换：快照旧值开启 320ms 取色过渡。 */
    if (g_palette != g_palette_prev) {
        if (g_palette_prev != NULL && !g_theme_blending) {
            g_palette_from = *g_palette_prev;
            g_theme_blending = true;
            g_theme_blend_started_ms = ptc_ui_anim_now_ms();
        }
        g_palette_prev = g_palette;
    }
#endif
    pixels = (uint32_t *)framebufferBegin(&g_ui.framebuffer, &stride_bytes);
    if (!pixels) {
        return;
    }
    stride = stride_bytes / sizeof(uint32_t);
    if (!g_background_cache || g_background_palette != g_palette ||
        g_background_docked != is_docked_mode()
#ifndef PTC_UI_PREVIEW_ANIM_CLOCK_MS
        || g_theme_blending
#endif
    ) {
        if (!g_background_cache) {
            g_background_cache = (uint32_t *)malloc((size_t)SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
        }
        if (g_background_cache) {
            ui_background_rebuild();
        }
    }
    if (g_background_cache) {
        if (stride == SCREEN_WIDTH) {
            memcpy(pixels, g_background_cache, (size_t)SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
        } else {
            int row;
            for (row = 0; row < SCREEN_HEIGHT; ++row) {
                memcpy(pixels + (size_t)row * stride,
                       g_background_cache + (size_t)row * SCREEN_WIDTH,
                       SCREEN_WIDTH * sizeof(uint32_t));
            }
        }
    } else {
        fill_rect_packed(pixels, stride, (UiRect){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, pack_rgb(UI_BLENDED(page_bg)));
    }
    if (model->view == PTC_UI_PARENT) {
        draw_parent(pixels, stride, model);
    } else if (model->view == PTC_UI_SETUP) {
        draw_setup(pixels, stride, model);
    } else if (model->view == PTC_UI_ERROR) {
        draw_error(pixels, stride, model);
    } else {
        draw_child(pixels, stride, model);
    }
    if (model->rendered_overlay != PTC_UI_OVERLAY_NONE) {
        /* 关闭动画期间渲染上一个弹层：输入已切回下层页面，仅视觉保留数帧。 */
        PtcUiModel visual = *model;
        visual.overlay = model->rendered_overlay;
        if (model->overlay == PTC_UI_OVERLAY_NONE) {
            int64_t closing_elapsed = ptc_ui_anim_now_ms() - model->closing_started_ms;
            int closed_frames = (int)(closing_elapsed / PTC_UI_OVERLAY_CLOSE_FRAME_MS);
            if (closed_frames < 0) closed_frames = 0;
            if (closed_frames > PTC_UI_OVERLAY_CLOSE_FRAMES) closed_frames = PTC_UI_OVERLAY_CLOSE_FRAMES;
            visual.overlay_open_frames = PTC_UI_OVERLAY_OPEN_FRAMES -
                closed_frames * PTC_UI_OVERLAY_OPEN_FRAMES / PTC_UI_OVERLAY_CLOSE_FRAMES;
        }
        draw_overlay(pixels, stride, &visual);
    }
#ifdef PLAYWISE_EDEN
    /* Permanent badge: no screenshot from the simulated build may be mistaken
       for real-device PCTL evidence. */
    fill_rect(pixels, stride, (UiRect){1080, 12, 184, 28}, UI_DANGER);
    draw_text(pixels, stride, 1094, 18, "EDEN TEST", 16, UI_ON_ACCENT);
#endif
    framebufferEnd(&g_ui.framebuffer);
}
