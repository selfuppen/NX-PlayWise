#include "ui_render_internal.h"

static void setup_button(uint32_t *pixels, uint32_t stride, PtcUiRect rect,
    const char *label, bool selected, bool primary)
{
    UiRect r = to_uirect(rect);
    uint32_t fill = primary ? UI_ACCENT : (selected ? UI_ACCENT_SOFT : UI_SURFACE);
    uint32_t border = selected ? UI_ACCENT : (primary ? UI_ACCENT : UI_BORDER);
    uint32_t text_color = primary ? UI_ON_ACCENT : (selected ? UI_ACCENT : UI_INK);
    if (!selected) draw_card_shadow(pixels, stride, r, 10);
    fill_round_rect(pixels, stride, r, 10, fill);
    draw_rect_outline(pixels, stride, r, 10, selected ? 2 : 1, border);
    if (selected) draw_focus_ring(pixels, stride, r, 10);
    draw_button_label(pixels, stride, r, label, 16, text_color);
}

static void setup_draw_surface_card(uint32_t *pixels, uint32_t stride, UiRect r, int radius)
{
    draw_card_shadow(pixels, stride, r, radius);
    fill_round_rect(pixels, stride, r, radius, UI_SURFACE);
    draw_rect_outline(pixels, stride, r, radius, 1, UI_BORDER);
}

static int setup_draw_pill(uint32_t *pixels, uint32_t stride, int x, int y, int h,
    const char *label, int font_size, uint32_t bg, uint32_t border, uint32_t fg)
{
    int tw = measure_text(label, font_size);
    int w = tw + 22;
    if (w < h) w = h;
    UiRect r = {x, y, w, h};
    fill_round_rect(pixels, stride, r, h / 2, bg);
    draw_rect_outline(pixels, stride, r, h / 2, 1, border);
    draw_text_center(pixels, stride, r, label, font_size, fg);
    return w;
}

static int setup_draw_pill_right(uint32_t *pixels, uint32_t stride, int right_x, int y, int h,
    const char *label, int font_size, uint32_t bg, uint32_t border, uint32_t fg)
{
    int tw = measure_text(label, font_size);
    int w = tw + 22;
    if (w < h) w = h;
    int x = right_x - w;
    (void)setup_draw_pill(pixels, stride, x, y, h, label, font_size, bg, border, fg);
    return x;
}

static void setup_draw_check_mark(uint32_t *pixels, uint32_t stride, int cx, int cy, uint32_t color)
{
    draw_line(pixels, stride, cx - 5, cy, cx - 1, cy + 4, 2, color);
    draw_line(pixels, stride, cx - 1, cy + 4, cx + 6, cy - 4, 2, color);
}

static void setup_draw_header_badge(uint32_t *pixels, uint32_t stride, const char *label)
{
    (void)setup_draw_pill_right(pixels, stride, 1226, 34, 34, label, 14,
        UI_ACCENT_SOFT, UI_ACCENT, UI_ACCENT);
}

static void setup_draw_stepper(uint32_t *pixels, uint32_t stride, int step)
{
    const char *labels[3] = {
        ptc_ui_text(PTC_UI_T_SETUP_PREPARE),
        ptc_ui_text(PTC_UI_T_SETUP_PARENT),
        ptc_ui_text(PTC_UI_T_SETUP_CONFIRM)
    };
    UiRect bar = {54, 116, 1172, 48};
    setup_draw_surface_card(pixels, stride, bar, 14);

    int text_w[3];
    int item_w[3];
    for (int i = 0; i < 3; ++i) {
        text_w[i] = measure_text(labels[i], 15);
        if (text_w[i] > 270) text_w[i] = 270;
        item_w[i] = 26 + 10 + text_w[i];
    }

    int x0 = bar.x + 24;
    int x2 = bar.x + bar.width - 24 - item_w[2];
    int x1 = bar.x + (bar.width - item_w[1]) / 2;
    if (x1 < x0 + item_w[0] + 44) x1 = x0 + item_w[0] + 44;
    if (x1 + item_w[1] + 44 > x2) x1 = x2 - 44 - item_w[1];
    int xs[3] = {x0, x1, x2};
    int cy = bar.y + bar.height / 2;

    for (int i = 0; i < 2; ++i) {
        int line_x0 = xs[i] + item_w[i] + 16;
        int line_x1 = xs[i + 1] - 16;
        if (line_x1 > line_x0 + 8) {
            fill_round_rect(pixels, stride, (UiRect){line_x0, cy - 1, line_x1 - line_x0, 3}, 1,
                (i + 1 < step) ? UI_SUCCESS : UI_BORDER);
        }
    }

    for (int i = 0; i < 3; ++i) {
        int n = i + 1;
        bool done = n < step;
        bool active = n == step;
        UiRect dot = {xs[i], cy - 13, 26, 26};
        fill_round_rect(pixels, stride, dot, 13,
            done ? UI_SUCCESS : (active ? UI_ACCENT : UI_RAISED));
        if (done) {
            setup_draw_check_mark(pixels, stride, dot.x + 13, dot.y + 13, UI_ON_ACCENT);
        } else {
            char num[4];
            snprintf(num, sizeof(num), "%d", n);
            draw_text_center(pixels, stride, dot, num, 13,
                active ? UI_ON_ACCENT : UI_MUTED);
        }
        char fitted[96];
        fit_text(fitted, sizeof(fitted), labels[i], 15, text_w[i] + 4);
        uint32_t col = active ? UI_ACCENT : (done ? UI_INK : UI_MUTED);
        if (active) draw_text_bold(pixels, stride, xs[i] + 36, cy + 5, fitted, 15, col);
        else draw_text(pixels, stride, xs[i] + 36, cy + 5, fitted, 15, col);
    }
}

static int setup_line_height(int size)
{
    return size * 3 / 2;
}

static void setup_draw_theme_swatch(uint32_t *pixels, uint32_t stride, int cx, int cy, int mode)
{
    int r = 6;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r) {
                uint32_t col;
                if (mode == 0) col = (dx + dy <= 0) ? UI_RGB(0xFCFCFD) : UI_RGB(0x222222);
                else if (mode == 1) col = UI_RGB(0xFCFCFD);
                else col = UI_RGB(0x222222);
                blend_pixel(pixels, stride, cx + dx, cy + dy, resolve_color(col), 255);
            }
        }
    }
    draw_circle_outline(pixels, stride, cx, cy, r, 1, UI_CONTROL);
}

static void setup_draw_footer_cta(uint32_t *pixels, uint32_t stride, PtcUiRect rect,
    const char *label, bool primary, bool selected)
{
    UiRect r = to_uirect(rect);
    uint32_t fill = primary ? UI_ACCENT : (selected ? UI_ACCENT_SOFT : UI_SURFACE);
    uint32_t fg = primary ? UI_ON_ACCENT : (selected ? UI_ACCENT : UI_INK);
    if (!selected) draw_card_shadow(pixels, stride, r, 12);
    fill_round_rect(pixels, stride, r, 12, fill);
    if (selected) {
        draw_focus_ring(pixels, stride, r, 12);
    } else {
        draw_rect_outline(pixels, stride, r, 12, 1, primary ? UI_ACCENT : UI_BORDER);
    }
    int size = 16;
    while (size > 13 && measure_text(label, size) > r.width - 28) --size;
    draw_button_label(pixels, stride, r, label, size, fg);
}

static const char *setup_review_emoji(int item)
{
    switch (item) {
    case PTC_UI_SETUP_OPTION_LANGUAGE: return "🌐";
    case PTC_UI_SETUP_OPTION_THEME: return "🎨";
    case PTC_UI_SETUP_OPTION_SHORTCUT: return "🎮";
    case PTC_UI_SETUP_OPTION_NOTIFICATIONS: return "🔔";
    case PTC_UI_SETUP_OPTION_EYE_CARE: return "🌿";
    case PTC_UI_SETUP_OPTION_BEDTIME: return "🌙";
    default: return "✨";
    }
}

static PtcUiTextId setup_review_category(int item)
{
    switch (item) {
    case PTC_UI_SETUP_OPTION_LANGUAGE:
    case PTC_UI_SETUP_OPTION_THEME: return PTC_UI_T_SETUP_REVIEW_CAT_DISPLAY;
    case PTC_UI_SETUP_OPTION_SHORTCUT: return PTC_UI_T_SETUP_REVIEW_CAT_ACCESS;
    case PTC_UI_SETUP_OPTION_NOTIFICATIONS: return PTC_UI_T_SETUP_REVIEW_CAT_ALERTS;
    case PTC_UI_SETUP_OPTION_EYE_CARE: return PTC_UI_T_SETUP_REVIEW_CAT_HEALTH;
    case PTC_UI_SETUP_OPTION_BEDTIME: return PTC_UI_T_SETUP_REVIEW_CAT_SLEEP;
    default: return PTC_UI_T_SETUP_REVIEW_CAT_DISPLAY;
    }
}

static PtcUiTextId setup_review_desc(int item)
{
    switch (item) {
    case PTC_UI_SETUP_OPTION_LANGUAGE: return PTC_UI_T_SETUP_REVIEW_DESC_LANG;
    case PTC_UI_SETUP_OPTION_THEME: return PTC_UI_T_SETUP_REVIEW_DESC_THEME;
    case PTC_UI_SETUP_OPTION_SHORTCUT: return PTC_UI_T_SETUP_REVIEW_DESC_SHORTCUT;
    case PTC_UI_SETUP_OPTION_NOTIFICATIONS: return PTC_UI_T_SETUP_REVIEW_DESC_NOTIF;
    case PTC_UI_SETUP_OPTION_EYE_CARE: return PTC_UI_T_SETUP_REVIEW_DESC_EYE;
    case PTC_UI_SETUP_OPTION_BEDTIME: return PTC_UI_T_SETUP_REVIEW_DESC_BEDTIME;
    default: return PTC_UI_T_SETUP_REVIEW_BODY;
    }
}

static void setup_review_tone(int item, uint32_t *soft, uint32_t *border, uint32_t *accent)
{
    switch (item) {
    case PTC_UI_SETUP_OPTION_NOTIFICATIONS:
        *soft = UI_WARNING_SOFT; *border = UI_WARNING_BORDER; *accent = UI_WARNING_TEXT;
        break;
    case PTC_UI_SETUP_OPTION_EYE_CARE:
        *soft = UI_SUCCESS_SOFT; *border = UI_SUCCESS_BORDER; *accent = UI_SUCCESS;
        break;
    case PTC_UI_SETUP_OPTION_BEDTIME:
        *soft = UI_NIGHT_SOFT; *border = UI_NIGHT_BORDER; *accent = UI_NIGHT;
        break;
    default:
        *soft = UI_ACCENT_SOFT; *border = UI_ACCENT; *accent = UI_ACCENT;
        break;
    }
}

static void setup_review_format_current(const PtcUiModel *model, int item, char *out, size_t out_size)
{
    switch (item) {
    case PTC_UI_SETUP_OPTION_LANGUAGE:
        if (model->language_preference == PTC_UI_LANGUAGE_SIMPLIFIED)
            snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HANS));
        else if (model->language_preference == PTC_UI_LANGUAGE_TRADITIONAL)
            snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HANT));
        else if (model->language_preference == PTC_UI_LANGUAGE_ENGLISH)
            snprintf(out, out_size, "English");
        else
            snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM));
        break;
    case PTC_UI_SETUP_OPTION_THEME:
        snprintf(out, out_size, "%s / %s",
            ptc_ui_theme_preference_label(g_theme.preference),
            ptc_ui_theme_style_label(g_theme.style));
        break;
    case PTC_UI_SETUP_OPTION_SHORTCUT:
        snprintf(out, out_size, "%s",
            model->custom_shortcut_label[0] ? model->custom_shortcut_label : "(-)");
        break;
    case PTC_UI_SETUP_OPTION_NOTIFICATIONS:
        snprintf(out, out_size, "%s",
            ptc_ui_text(model->reason_notifications_enabled ? PTC_UI_T_ENABLED : PTC_UI_T_DISABLED_2));
        break;
    case PTC_UI_SETUP_OPTION_EYE_CARE: {
        bool eye_on = model->eye_care_policy.enabled || model->draft_eye_care_policy.enabled;
        snprintf(out, out_size, "%s",
            ptc_ui_text(eye_on ? PTC_UI_T_ENABLED : PTC_UI_T_DISABLED));
        break;
    }
    case PTC_UI_SETUP_OPTION_BEDTIME: {
        bool bed_on = model->bedtime_status_enabled || model->bedtime_policy.enabled ||
            model->draft_bedtime_policy.enabled;
        snprintf(out, out_size, "%s",
            ptc_ui_text(bed_on ? PTC_UI_T_ENABLED : PTC_UI_T_DISABLED));
        break;
    }
    default:
        snprintf(out, out_size, "%s", ptc_ui_text(PTC_UI_T_DISABLED));
        break;
    }
}

static void draw_setup_review(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    int item = ptc_ui_setup_next_option(model);
    if (item < 0) return;

    int pending_indices[PTC_UI_SETUP_OPTION_COUNT];
    int pending_count = 0;
    for (int i = 0; i < PTC_UI_SETUP_OPTION_COUNT; ++i) {
        if (model->setup_option_versions[i] < ptc_ui_setup_option_revision(i)) {
            pending_indices[pending_count++] = i;
        }
    }

    draw_header(pixels, stride, ptc_ui_text(PTC_UI_T_SETUP_REVIEW_TITLE),
        ptc_ui_text(PTC_UI_T_SETUP_REVIEW_INTRO));
    char rem_badge[64];
    snprintf(rem_badge, sizeof(rem_badge), ptc_ui_text(PTC_UI_T_SETUP_REVIEW_REMAINING), pending_count);
    setup_draw_header_badge(pixels, stride, rem_badge);

    /* Top Queue Strip (Y=116..164) */
    UiRect queue_bar = {54, 116, 1172, 48};
    setup_draw_surface_card(pixels, stride, queue_bar, 14);
    const char *q_lbl = ptc_ui_text(PTC_UI_T_SETUP_REVIEW_QUEUE_LABEL);
    draw_text(pixels, stride, queue_bar.x + 20, queue_bar.y + 29, q_lbl, 13, UI_MUTED);
    int qx = queue_bar.x + 20 + measure_text(q_lbl, 13) + 14;
    const char *once_hint = ptc_ui_text(PTC_UI_T_SETUP_REVIEW_ONCE_HINT);
    int once_w = measure_text(once_hint, 12);
    int queue_right_limit = queue_bar.x + queue_bar.width - 20 - once_w - 18;
    draw_text(pixels, stride, queue_bar.x + queue_bar.width - 20 - once_w, queue_bar.y + 28,
        once_hint, 12, UI_MUTED);

    int drawn_chips = 0;
    for (int p = 0; p < pending_count; ++p) {
        int qi = pending_indices[p];
        bool cur = (qi == item);
        char chip_text[96];
        snprintf(chip_text, sizeof(chip_text), "%s %s", setup_review_emoji(qi),
            ptc_ui_text(ptc_ui_setup_option_title(qi)));
        int cw = measure_text(chip_text, 13) + 22;
        if (qx + cw > queue_right_limit) {
            int hidden = pending_count - drawn_chips;
            if (hidden > 0 && qx + 48 <= queue_right_limit) {
                char more_buf[16];
                snprintf(more_buf, sizeof(more_buf), "+%d", hidden);
                (void)setup_draw_pill(pixels, stride, qx, queue_bar.y + 10, 28, more_buf, 12,
                    UI_RAISED, UI_BORDER, UI_MUTED);
            }
            break;
        }
        (void)setup_draw_pill(pixels, stride, qx, queue_bar.y + 10, 28, chip_text, 13,
            cur ? UI_ACCENT_SOFT : UI_RAISED, cur ? UI_ACCENT : UI_BORDER,
            cur ? UI_ACCENT : UI_MUTED);
        qx += cw + 8;
        ++drawn_chips;
    }

    /* Main Spotlight Card (Y=180..442, H=262) */
    uint32_t tone_soft, tone_border, tone_accent;
    setup_review_tone(item, &tone_soft, &tone_border, &tone_accent);
    UiRect card = {54, 180, 1172, 262};
    setup_draw_surface_card(pixels, stride, card, 16);

    /* Left Identity Panel (W=408) */
    UiRect left_pane = {card.x + 1, card.y + 1, 408, card.height - 2};
    fill_round_rect(pixels, stride, left_pane, 15, tone_soft);
    /* Square off the right half of left_pane so it meets the divider cleanly */
    fill_rect(pixels, stride, (UiRect){left_pane.x + left_pane.width - 16, left_pane.y, 16, left_pane.height}, tone_soft);
    fill_rect(pixels, stride, (UiRect){left_pane.x + left_pane.width, card.y + 1, 1, card.height - 2}, UI_BORDER);

    UiRect icon_box = {card.x + 26, card.y + 24, 56, 56};
    fill_round_rect(pixels, stride, icon_box, 14, UI_SURFACE);
    draw_rect_outline(pixels, stride, icon_box, 14, 1, tone_border);
    draw_text_center(pixels, stride, icon_box, setup_review_emoji(item), 28, tone_accent);

    (void)setup_draw_pill_right(pixels, stride, left_pane.x + left_pane.width - 24,
        card.y + 36, 28, ptc_ui_text(setup_review_category(item)), 13,
        UI_SURFACE, tone_border, tone_accent);

    draw_wrapped_text(pixels, stride, card.x + 26, card.y + 124,
        ptc_ui_text(ptc_ui_setup_option_title(item)), 23, 356, 30, 2, UI_INK);

    /* Current Value Box */
    UiRect cur_box = {card.x + 26, card.y + 186, 356, 52};
    fill_round_rect(pixels, stride, cur_box, 10, UI_SURFACE);
    draw_rect_outline(pixels, stride, cur_box, 10, 1, UI_BORDER);
    const char *cur_lbl = ptc_ui_text(PTC_UI_T_SETUP_REVIEW_CURRENT_LABEL);
    int cur_lbl_w = measure_text(cur_lbl, 13);
    draw_text(pixels, stride, cur_box.x + 14, cur_box.y + 31, cur_lbl, 13, UI_MUTED);
    char cur_val[96], cur_fitted[96];
    setup_review_format_current(model, item, cur_val, sizeof(cur_val));
    int val_max_w = cur_box.width - cur_lbl_w - 40;
    if (val_max_w < 80) val_max_w = 80;
    fit_text(cur_fitted, sizeof(cur_fitted), cur_val, 14, val_max_w);
    int val_w = measure_text(cur_fitted, 14);
    draw_text(pixels, stride, cur_box.x + cur_box.width - 14 - val_w, cur_box.y + 31,
        cur_fitted, 14, UI_INK);

    /* Right Explanation Panel (X=488..1198, W=710) */
    int rx = card.x + 436;
    int rw = card.width - 464;
    draw_text(pixels, stride, rx, card.y + 42,
        ptc_ui_text(PTC_UI_T_SETUP_REVIEW_PURPOSE_LABEL), 13, UI_ACCENT);
    draw_wrapped_text(pixels, stride, rx, card.y + 78,
        ptc_ui_text(setup_review_desc(item)), 17, rw, 27, 3, UI_INK);

    UiRect guide_box = {rx, card.y + 174, rw, 64};
    fill_round_rect(pixels, stride, guide_box, 10, UI_RAISED);
    draw_rect_outline(pixels, stride, guide_box, 10, 1, UI_BORDER);
    draw_wrapped_text(pixels, stride, guide_box.x + 16, guide_box.y + 24,
        ptc_ui_text(PTC_UI_T_SETUP_REVIEW_BODY), 13, guide_box.width - 32, 20, 2, UI_MUTED);

    /* Primary & Secondary Large Action Cards (Actions 0 and 1, Y=462..548) */
    const PtcUiTextId primary_titles[2] = {
        PTC_UI_T_SETUP_REVIEW_CONFIGURE,
        PTC_UI_T_SETUP_REVIEW_KEEP
    };
    const PtcUiTextId primary_descs[2] = {
        PTC_UI_T_SETUP_REVIEW_CONFIG_DESC,
        PTC_UI_T_SETUP_REVIEW_KEEP_DESC
    };
    for (int i = 0; i < 2; ++i) {
        UiRect r = to_uirect(ptc_ui_setup_review_rect(i));
        bool sel = (model->setup_review_selection == i);
        bool is_primary = (i == 0);
        uint32_t fill = is_primary ? UI_ACCENT : (sel ? UI_ACCENT_SOFT : UI_SURFACE);
        uint32_t title_col = is_primary ? UI_ON_ACCENT : (sel ? UI_ACCENT : UI_INK);
        uint32_t desc_col = is_primary ? UI_ON_ACCENT : UI_MUTED;
        if (!sel) draw_card_shadow(pixels, stride, r, 14);
        fill_round_rect(pixels, stride, r, 14, fill);
        if (sel) {
            draw_focus_ring(pixels, stride, r, 14);
        } else {
            draw_rect_outline(pixels, stride, r, 14, 1, is_primary ? UI_ACCENT : UI_BORDER);
        }
        char fitted_t[128], fitted_d[160];
        int text_w = r.width - 48;
        fit_text(fitted_t, sizeof(fitted_t), ptc_ui_text(primary_titles[i]), 18, text_w);
        fit_text(fitted_d, sizeof(fitted_d), ptc_ui_text(primary_descs[i]), 13, text_w);
        draw_text(pixels, stride, r.x + 24, r.y + 38, fitted_t, 18, title_col);
        draw_text(pixels, stride, r.x + 24, r.y + 66, fitted_d, 13, desc_col);
    }

    /* Non-blocking Save Failure Banner strictly outside all 4 buttons (Y=566..606) */
    if (model->message[0]) {
        UiRect warn_bar = {54, 566, 1172, 42};
        fill_round_rect(pixels, stride, warn_bar, 10, UI_WARNING_SOFT);
        draw_rect_outline(pixels, stride, warn_bar, 10, 1, UI_WARNING_BORDER);
        char fitted_msg[256];
        fit_text(fitted_msg, sizeof(fitted_msg), model->message, 14, warn_bar.width - 40);
        draw_text(pixels, stride, warn_bar.x + 20, warn_bar.y + 26, fitted_msg, 14, UI_WARNING_TEXT);
    }

    /* Footer Actions 2 & 3 (Y=664..708) */
    setup_draw_footer_cta(pixels, stride, ptc_ui_setup_review_rect(2),
        ptc_ui_text(PTC_UI_T_SETUP_REVIEW_SKIP), false, model->setup_review_selection == 2);
    setup_draw_footer_cta(pixels, stride, ptc_ui_setup_review_rect(3),
        ptc_ui_text(PTC_UI_T_SETUP_REVIEW_SKIP_ALL), false, model->setup_review_selection == 3);

    const char *f_hint = ptc_ui_text(PTC_UI_T_SETUP_REVIEW_FOOTER_HINT);
    PtcUiRect r3 = ptc_ui_setup_review_rect(3);
    int hint_max_w = 1226 - (r3.x + r3.w + 24);
    if (hint_max_w > 120) {
        char fitted_hint[128];
        fit_text(fitted_hint, sizeof(fitted_hint), f_hint, 13, hint_max_w);
        int fw = measure_text(fitted_hint, 13);
        draw_text(pixels, stride, 1226 - fw, 691, fitted_hint, 13, UI_MUTED);
    }
}

static void draw_setup_step1(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char text[256];

    /* Card 1: Interface Language (Y=178..306, H=128) */
    UiRect lang_card = {54, 178, 1172, 128};
    setup_draw_surface_card(pixels, stride, lang_card, 16);
    const char *lang_title = ptc_ui_text(PTC_UI_T_SETUP_CARD_LANG);
    draw_text_bold(pixels, stride, 74, 212, lang_title, 18, UI_INK);
    int hint_x = 74 + measure_text(lang_title, 18) + 22;
    const char *hint_zh = ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HINT_ZH);
    const char *hint_en = ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HINT_EN);
    char fitted_hint[256];
    fit_text(fitted_hint, sizeof(fitted_hint), hint_zh, 13, 360);
    draw_text(pixels, stride, hint_x, 212, fitted_hint, 13, UI_MUTED);
    hint_x += measure_text(fitted_hint, 13) + 24;
    if (hint_x < 1040) {
        fit_text(fitted_hint, sizeof(fitted_hint), hint_en, 13, 1206 - hint_x);
        draw_text(pixels, stride, hint_x, 212, fitted_hint, 13, UI_MUTED);
    }
    for (int i = 0; i < 4; ++i) {
        PtcUiRect r = ptc_ui_setup_language_rect(i);
        bool chosen = model->language_preference == (PtcUiLanguagePreference)i;
        bool focused = (model->setup_focus == 1) && chosen;
        UiRect ur = to_uirect(r);
        fill_round_rect(pixels, stride, ur, 10, chosen ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, ur, 10, (focused || chosen) ? 2 : 1,
            chosen ? UI_ACCENT : UI_BORDER);
        if (focused) draw_focus_ring(pixels, stride, ur, 10);
        const char *lbl = i == 0 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_SYSTEM) :
            i == 1 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HANS) :
            i == 2 ? ptc_ui_text(PTC_UI_T_SETUP_LANGUAGE_HANT) : "English";
        draw_text_center(pixels, stride, ur, lbl, 15, chosen ? UI_ACCENT : UI_INK);
    }

    /* Bottom Left Card: Pre-flight Checks (Y=322..632, W=684) */
    UiRect pre_card = {54, 322, 684, 310};
    setup_draw_surface_card(pixels, stride, pre_card, 16);
    bool pctl_ok = model->restriction_enabled_available && model->restriction_enabled;
    int pre_badge_x = setup_draw_pill_right(pixels, stride, pre_card.x + pre_card.width - 20,
        pre_card.y + 18, 28,
        ptc_ui_text(pctl_ok ? PTC_UI_T_SETUP_PREFLIGHT_READY : PTC_UI_T_SETUP_PREFLIGHT_CHECK),
        13, pctl_ok ? UI_SUCCESS_SOFT : UI_WARNING_SOFT,
        pctl_ok ? UI_SUCCESS_BORDER : UI_WARNING_BORDER,
        pctl_ok ? UI_SUCCESS : UI_WARNING_TEXT);
    char fitted_pre_title[128];
    fit_text(fitted_pre_title, sizeof(fitted_pre_title),
        ptc_ui_text(PTC_UI_T_SETUP_CARD_PREFLIGHT), 18, pre_badge_x - (pre_card.x + 20) - 12);
    draw_text_bold(pixels, stride, pre_card.x + 20, pre_card.y + 38, fitted_pre_title, 18, UI_INK);

    /* Sub-card 1: Nintendo Parental Controls (Y=384..492, H=108) */
    UiRect pctl_box = {74, 384, 644, 108};
    fill_round_rect(pixels, stride, pctl_box, 12, UI_RAISED);
    draw_rect_outline(pixels, stride, pctl_box, 12, 1, UI_BORDER);
    const char *pctl_str = ptc_ui_text(!model->restriction_enabled_available
        ? PTC_UI_T_SETUP_PCTL_UNKNOWN : model->restriction_enabled
        ? PTC_UI_T_SETUP_PCTL_ON : PTC_UI_T_SETUP_PCTL_OFF);
    int pctl_text_w = 482 - 90 - 16;
    int pctl_lines = count_wrapped_lines(pctl_str, 15, pctl_text_w, 2);
    draw_wrapped_text(pixels, stride, 90, 412, pctl_str, 15, pctl_text_w, 21, 2,
        pctl_ok ? UI_SUCCESS : UI_WARNING_TEXT);
    draw_wrapped_text(pixels, stride, 90, 412 + pctl_lines * 21 + 4,
        ptc_ui_text(PTC_UI_T_SETUP_PAUSE_OVERLAY), 13, pctl_text_w, 18, 2, UI_MUTED);
    setup_button(pixels, stride, ptc_ui_setup_pctl_help_rect(),
        ptc_ui_text(PTC_UI_T_SETUP_PCTL_HELP_BUTTON), model->setup_focus == 2, false);

    /* Sub-card 2: Console Clock & Time Sync (Y=508..612, H=104) */
    UiRect time_box = to_uirect(ptc_ui_setup_time_help_rect());
    fill_round_rect(pixels, stride, time_box, 12, model->setup_focus == 3 ? UI_ACCENT_SOFT : UI_RAISED);
    draw_rect_outline(pixels, stride, time_box, 12, model->setup_focus == 3 ? 2 : 1,
        model->setup_focus == 3 ? UI_ACCENT : UI_BORDER);
    if (model->setup_focus == 3) draw_focus_ring(pixels, stride, time_box, 12);

#ifdef PTC_UI_PREVIEW_WALL_TIME
    uint16_t year, minute = ptc_ui_render_minute_of_day(ptc_ui_render_now());
    uint8_t month, day;
    if (ptc_date_from_day_index(model->day_index, &year, &month, &day))
        snprintf(text, sizeof(text), "%u-%02u-%02u %02u:%02u", year, month, day, minute / 60, minute % 60);
    else snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
#else
    time_t now = (time_t)ptc_ui_render_now();
    struct tm *local = now > 0 ? localtime(&now) : NULL;
    if (local) strftime(text, sizeof(text), "%Y-%m-%d %H:%M", local);
    else snprintf(text, sizeof(text), "%s", ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
#endif
    char clock_lbl[96];
    snprintf(clock_lbl, sizeof(clock_lbl), "⏰ %s", ptc_ui_text(PTC_UI_T_SETUP_TIME_HELP));
    draw_text(pixels, stride, time_box.x + 16, time_box.y + 34, clock_lbl, 13, UI_MUTED);
    char fitted_clock[64];
    fit_text(fitted_clock, sizeof(fitted_clock), text, 21, 212);
    draw_text_bold(pixels, stride, time_box.x + 16, time_box.y + 72, fitted_clock, 21, UI_ACCENT);

    fill_rect(pixels, stride, (UiRect){time_box.x + 238, time_box.y + 18, 1, time_box.height - 36}, UI_BORDER);
    int guide_x = time_box.x + 254;
    int guide_w = time_box.x + time_box.width - guide_x - 16;
    int hint_lines = count_wrapped_lines(ptc_ui_text(PTC_UI_T_SETUP_TIME_HINT), 13, guide_w, 2);
    draw_wrapped_text(pixels, stride, guide_x, time_box.y + 30,
        ptc_ui_text(PTC_UI_T_SETUP_TIME_HINT), 13, guide_w, 19, 2, UI_INK);
    draw_wrapped_text(pixels, stride, guide_x, time_box.y + 30 + hint_lines * 19 + 4,
        ptc_ui_text(PTC_UI_T_SETUP_TIME_INSTRUCTIONS), 12, guide_w, 17, 2, UI_MUTED);

    /* Bottom Right Card: System Environment (Y=322..632, W=472) */
    UiRect env_card = {754, 322, 472, 310};
    setup_draw_surface_card(pixels, stride, env_card, 16);
    bool env_match = ptc_ui_setup_reference_matches(model);
    int env_badge_x = setup_draw_pill_right(pixels, stride, env_card.x + env_card.width - 20,
        env_card.y + 18, 28,
        ptc_ui_text(env_match ? PTC_UI_T_SETUP_ENV_MATCH_BADGE : PTC_UI_T_SETUP_ENV_DIFF_BADGE),
        13, env_match ? UI_SUCCESS_SOFT : UI_WARNING_SOFT,
        env_match ? UI_SUCCESS_BORDER : UI_WARNING_BORDER,
        env_match ? UI_SUCCESS : UI_WARNING_TEXT);
    char fitted_env_title[128];
    fit_text(fitted_env_title, sizeof(fitted_env_title),
        ptc_ui_text(PTC_UI_T_SETUP_CARD_SYSTEM_ENV), 18, env_badge_x - (env_card.x + 20) - 10);
    draw_text_bold(pixels, stride, env_card.x + 20, env_card.y + 38, fitted_env_title, 18, UI_INK);

    /* 3 Spec Tiles (Y=384..454) */
    const char *spec_labels[3] = {
        ptc_ui_text(PTC_UI_T_SETUP_ENV_HOS_LABEL),
        "Atmosphere",
        ptc_ui_text(PTC_UI_T_SETUP_ENV_MODEL_LABEL)
    };
    const char *spec_vals[3] = {
        (model->environment_available && model->environment_hos[0])
            ? model->environment_hos : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
        (model->environment_available && model->environment_atmosphere_version[0])
            ? model->environment_atmosphere_version : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
        (model->environment_available && model->environment_model[0])
            ? (strcmp(model->environment_model, "mariko-oled") == 0 ? "OLED" : model->environment_model)
            : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED)
    };
    for (int i = 0; i < 3; ++i) {
        UiRect tile = {env_card.x + 20 + i * 148, 384, 136, 70};
        fill_round_rect(pixels, stride, tile, 10, UI_RAISED);
        draw_rect_outline(pixels, stride, tile, 10, 1, UI_BORDER);
        char fl[48], fv[48];
        fit_text(fl, sizeof(fl), spec_labels[i], 12, tile.width - 20);
        fit_text(fv, sizeof(fv), spec_vals[i], 16, tile.width - 20);
        draw_text(pixels, stride, tile.x + 12, tile.y + 24, fl, 12, UI_MUTED);
        draw_text_bold(pixels, stride, tile.x + 12, tile.y + 52, fv, 16, UI_INK);
    }

    /* Reference environment + complete separated advisory (Y=474..548) */
    const char *reference = ptc_ui_text(PTC_UI_T_SETUP_REFERENCE);
    const char *compatibility = ptc_ui_text(env_match
        ? PTC_UI_T_SETUP_REFERENCE_MATCH : PTC_UI_T_SETUP_REFERENCE_UNKNOWN);
    int ref_x = env_card.x + 20;
    int ref_w = env_card.width - 40;
    int ref_size = 13;
    while (ref_size > 11 && measure_text(reference, ref_size) > ref_w) --ref_size;
    int comp_size = 13;
    while (comp_size > 11 && measure_text(compatibility, comp_size) > ref_w) --comp_size;
    draw_text(pixels, stride, ref_x, 486, reference, ref_size, UI_MUTED);
    draw_text(pixels, stride, ref_x, 486 + setup_line_height(ref_size) + 8,
        compatibility, comp_size, env_match ? UI_SUCCESS : UI_WARNING_TEXT);

    /* Calm Reassurance Strip (Y=560..612) */
    UiRect info_bar = {env_card.x + 20, 558, env_card.width - 40, 54};
    fill_round_rect(pixels, stride, info_bar, 10, UI_ACCENT_SOFT);
    draw_text(pixels, stride, info_bar.x + 12, info_bar.y + 25, "ℹ️", 15, UI_ACCENT);
    draw_wrapped_text(pixels, stride, info_bar.x + 34, info_bar.y + 22,
        ptc_ui_text(PTC_UI_T_SETUP_ENV_REASSURE), 12, info_bar.width - 46, 17, 2, UI_ACCENT);
}

static void draw_setup_step2(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    /* Left Card: Parent PIN & Access (X=54..632, Y=178..632, W=578, H=454) */
    UiRect left_card = {54, 178, 578, 454};
    setup_draw_surface_card(pixels, stride, left_card, 16);
    bool pin_ok = model->setup_pin_ready;
    int pin_badge_x = setup_draw_pill_right(pixels, stride, left_card.x + left_card.width - 20,
        left_card.y + 18, 28,
        ptc_ui_text(pin_ok ? PTC_UI_T_SETUP_PIN_BADGE_CUSTOM : PTC_UI_T_SETUP_PIN_BADGE_DEFAULT),
        13, pin_ok ? UI_SUCCESS_SOFT : UI_WARNING_SOFT,
        pin_ok ? UI_SUCCESS_BORDER : UI_WARNING_BORDER,
        pin_ok ? UI_SUCCESS : UI_WARNING_TEXT);
    char fitted_ltitle[128];
    fit_text(fitted_ltitle, sizeof(fitted_ltitle),
        ptc_ui_text(PTC_UI_T_SETUP_CARD_PIN_ACCESS), 18, pin_badge_x - (left_card.x + 20) - 10);
    draw_text_bold(pixels, stride, left_card.x + 20, left_card.y + 38, fitted_ltitle, 18, UI_INK);
    draw_wrapped_text(pixels, stride, left_card.x + 20, left_card.y + 66,
        ptc_ui_text(PTC_UI_T_SETUP_PIN_DESC), 13, 538, 19, 2, UI_MUTED);

    /* PIN Status & Action Box (Y=274..360, H=86) */
    UiRect pin_rect = to_uirect(ptc_ui_setup_pin_rect());
    bool pin_focused = (model->setup_focus == 1);
    fill_round_rect(pixels, stride, pin_rect, 12,
        pin_focused ? UI_ACCENT_SOFT : (pin_ok ? UI_SUCCESS_SOFT : UI_WARNING_SOFT));
    if (pin_focused) {
        draw_focus_ring(pixels, stride, pin_rect, 12);
    } else {
        draw_rect_outline(pixels, stride, pin_rect, 12, 1,
            pin_ok ? UI_SUCCESS_BORDER : UI_WARNING_BORDER);
    }
    const char *pin_btn_lbl = ptc_ui_text(PTC_UI_T_X_CLICK_CHANGE_PIN);
    int pin_btn_w = measure_text(pin_btn_lbl, 14) + 30;
    if (pin_btn_w < 130) pin_btn_w = 130;
    if (pin_btn_w > 220) pin_btn_w = 220;
    UiRect pin_chip = {pin_rect.x + pin_rect.width - 14 - pin_btn_w, pin_rect.y + 23, pin_btn_w, 40};
    fill_round_rect(pixels, stride, pin_chip, 10, UI_SURFACE);
    draw_rect_outline(pixels, stride, pin_chip, 10, 1, pin_focused ? UI_ACCENT : UI_BORDER);
    draw_button_label(pixels, stride, pin_chip, pin_btn_lbl, 14, pin_focused ? UI_ACCENT : UI_INK);

    int pin_info_w = pin_chip.x - (pin_rect.x + 16) - 12;
    char fitted_pin_st[128];
    fit_text(fitted_pin_st, sizeof(fitted_pin_st),
        ptc_ui_text(pin_ok ? PTC_UI_T_SETUP_PIN_STATUS_CUSTOM : PTC_UI_T_SETUP_PIN_STATUS_DEFAULT),
        14, pin_info_w);
    draw_text_bold(pixels, stride, pin_rect.x + 16, pin_rect.y + 34, fitted_pin_st, 14,
        pin_ok ? UI_SUCCESS : UI_WARNING_TEXT);
    draw_wrapped_text(pixels, stride, pin_rect.x + 16, pin_rect.y + 56,
        ptc_ui_text(PTC_UI_T_SETUP_PIN_RULES_COMPACT), 12, pin_info_w, 16, 2, UI_MUTED);

    /* Divider */
    fill_rect(pixels, stride, (UiRect){74, 376, 538, 1}, UI_BORDER);

    /* Theme Preference (Y=390..464) */
    char theme_hdr[96];
    snprintf(theme_hdr, sizeof(theme_hdr), "🎨 %s", ptc_ui_text(PTC_UI_T_SETUP_THEME_LABEL));
    draw_text_bold(pixels, stride, 74, 404, theme_hdr, 14, UI_INK);
    for (int i = 0; i < 3; ++i) {
        bool selected = model->setup_theme_index == i;
        bool focused = model->setup_focus == 2 && selected;
        UiRect utr = to_uirect(ptc_ui_setup_theme_rect(i));
        fill_round_rect(pixels, stride, utr, 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, utr, 10, (focused || selected) ? 2 : 1,
            selected ? UI_ACCENT : UI_BORDER);
        if (focused) draw_focus_ring(pixels, stride, utr, 10);
        const char *tlbl = ptc_ui_theme_preference_label((PtcUiThemePreference)i);
        int tw = measure_text(tlbl, 15);
        int total_w = 14 + 8 + tw;
        int start_x = utr.x + (utr.width - total_w) / 2;
        if (start_x < utr.x + 10) start_x = utr.x + 10;
        setup_draw_theme_swatch(pixels, stride, start_x + 7, utr.y + utr.height / 2, i);
        draw_text(pixels, stride, start_x + 22, utr.y + utr.height / 2 + 5, tlbl, 15,
            selected ? UI_ACCENT : UI_INK);
    }

    /* Parent Shortcut (Y=484..568) - no duplicated label */
    char sc_hdr[96];
    snprintf(sc_hdr, sizeof(sc_hdr), "🎮 %s", ptc_ui_text(PTC_UI_T_SETUP_SHORTCUT_LABEL));
    draw_text_bold(pixels, stride, 74, 498, sc_hdr, 14, UI_INK);
    UiRect s_rect = to_uirect(ptc_ui_setup_shortcut_rect());
    bool s_focused = (model->setup_focus == 3);
    fill_round_rect(pixels, stride, s_rect, 10, s_focused ? UI_ACCENT_SOFT : UI_RAISED);
    draw_rect_outline(pixels, stride, s_rect, 10, s_focused ? 2 : 1,
        s_focused ? UI_ACCENT : UI_BORDER);
    if (s_focused) draw_focus_ring(pixels, stride, s_rect, 10);
    const char *sc_val = model->custom_shortcut_label[0] ? model->custom_shortcut_label : "(-)";
    int key_pill_w = setup_draw_pill(pixels, stride, s_rect.x + 14, s_rect.y + 11, 32,
        sc_val, 14, UI_SURFACE, UI_CONTROL, UI_INK);
    char sc_action[96];
    snprintf(sc_action, sizeof(sc_action), "%s >", ptc_ui_text(PTC_UI_T_SETUP_SHORTCUT_CHANGE));
    int act_w = measure_text(sc_action, 14);
    draw_text(pixels, stride, s_rect.x + s_rect.width - 16 - act_w, s_rect.y + 32,
        sc_action, 14, UI_ACCENT);
    int mid_avail = (s_rect.x + s_rect.width - 28 - act_w) - (s_rect.x + 26 + key_pill_w);
    if (mid_avail > 80) {
        char mid_hint[96];
        fit_text(mid_hint, sizeof(mid_hint), ptc_ui_text(PTC_UI_T_FIXED_MINUS_RELEASE_TO_ENTER), 12, mid_avail);
        draw_text(pixels, stride, s_rect.x + 26 + key_pill_w, s_rect.y + 31, mid_hint, 12, UI_MUTED);
    }

    /* Compact Bottom Note inside Left Card */
    UiRect sec_note = {74, 582, 538, 34};
    fill_round_rect(pixels, stride, sec_note, 8, UI_RAISED);
    char fitted_note[160];
    fit_text(fitted_note, sizeof(fitted_note), ptc_ui_text(PTC_UI_T_SETUP_DEFAULT_PREFS), 12, sec_note.width - 24);
    draw_text(pixels, stride, sec_note.x + 12, sec_note.y + 21, fitted_note, 12, UI_MUTED);

    /* Right Card: Protection & Alerts (X=648..1226, Y=178..632, W=578, H=454) */
    UiRect right_card = {648, 178, 578, 454};
    setup_draw_surface_card(pixels, stride, right_card, 16);
    draw_text_bold(pixels, stride, right_card.x + 20, right_card.y + 38,
        ptc_ui_text(PTC_UI_T_SETUP_CARD_GUARDS), 18, UI_INK);
    draw_wrapped_text(pixels, stride, right_card.x + 20, right_card.y + 66,
        ptc_ui_text(PTC_UI_T_SETUP_GUARDS_INTRO), 13, 538, 19, 2, UI_MUTED);

    /* 1. Eye Care Card (Y=282..380) */
    bool eye_on = model->setup_eye_care_enabled || model->draft_eye_care_policy.enabled;
    UiRect eye_rect = to_uirect(ptc_ui_setup_eye_care_rect());
    bool eye_focused = (model->setup_focus == 4);
    fill_round_rect(pixels, stride, eye_rect, 12,
        eye_focused ? UI_ACCENT_SOFT : (eye_on ? UI_SUCCESS_SOFT : UI_RAISED));
    if (eye_focused) draw_focus_ring(pixels, stride, eye_rect, 12);
    else draw_rect_outline(pixels, stride, eye_rect, 12, 1, eye_on ? UI_SUCCESS_BORDER : UI_BORDER);
    UiRect eye_ico = {eye_rect.x + 14, eye_rect.y + 26, 44, 44};
    fill_round_rect(pixels, stride, eye_ico, 10, UI_SURFACE);
    draw_rect_outline(pixels, stride, eye_ico, 10, 1, eye_on ? UI_SUCCESS_BORDER : UI_BORDER);
    draw_text_center(pixels, stride, eye_ico, "🌿", 22, UI_SUCCESS);
    UiRect eye_sw = {eye_rect.x + eye_rect.width - 66, eye_rect.y + (eye_rect.height - 26) / 2, 50, 26};
    draw_toggle_switch(pixels, stride, eye_sw, eye_on, false, false, NULL, NULL);
    int eye_tx = eye_ico.x + eye_ico.width + 14;
    int eye_tw = eye_sw.x - eye_tx - 14;
    const char *eye_lbl = ptc_ui_text(PTC_UI_T_SETUP_EYE_CARE_LABEL);
    draw_text_bold(pixels, stride, eye_tx, eye_rect.y + 34, eye_lbl, 16, UI_INK);
    int eye_px = eye_tx + measure_text(eye_lbl, 16) + 10;
    if (eye_sw.x - 14 - eye_px > 60) {
        char ep[96];
        fit_text(ep, sizeof(ep), ptc_ui_text(PTC_UI_T_SETUP_EYE_CARE_DEFAULT_PARAMS), 13, eye_sw.x - 14 - eye_px);
        draw_text(pixels, stride, eye_px, eye_rect.y + 34, ep, 13, eye_on ? UI_SUCCESS : UI_MUTED);
    }
    draw_wrapped_text(pixels, stride, eye_tx, eye_rect.y + 58,
        ptc_ui_text(PTC_UI_T_SETUP_EYE_CARE_HINT), 12, eye_tw, 17, 2, UI_MUTED);

    /* 2. Bedtime Card (Y=394..492) */
    bool bed_on = model->setup_bedtime_enabled || model->draft_bedtime_policy.enabled;
    UiRect bed_rect = to_uirect(ptc_ui_setup_bedtime_rect());
    bool bed_focused = (model->setup_focus == 5);
    fill_round_rect(pixels, stride, bed_rect, 12,
        bed_focused ? UI_ACCENT_SOFT : (bed_on ? UI_NIGHT_SOFT : UI_RAISED));
    if (bed_focused) draw_focus_ring(pixels, stride, bed_rect, 12);
    else draw_rect_outline(pixels, stride, bed_rect, 12, 1, bed_on ? UI_NIGHT_BORDER : UI_BORDER);
    UiRect bed_ico = {bed_rect.x + 14, bed_rect.y + 26, 44, 44};
    fill_round_rect(pixels, stride, bed_ico, 10, UI_SURFACE);
    draw_rect_outline(pixels, stride, bed_ico, 10, 1, bed_on ? UI_NIGHT_BORDER : UI_BORDER);
    draw_text_center(pixels, stride, bed_ico, "🌙", 22, UI_NIGHT);
    UiRect bed_sw = {bed_rect.x + bed_rect.width - 66, bed_rect.y + (bed_rect.height - 26) / 2, 50, 26};
    draw_toggle_switch(pixels, stride, bed_sw, bed_on, false, false, NULL, NULL);
    int bed_tx = bed_ico.x + bed_ico.width + 14;
    int bed_tw = bed_sw.x - bed_tx - 14;
    const char *bed_lbl = ptc_ui_text(PTC_UI_T_SETUP_BEDTIME_LABEL);
    draw_text_bold(pixels, stride, bed_tx, bed_rect.y + 34, bed_lbl, 16, UI_INK);
    int bed_px = bed_tx + measure_text(bed_lbl, 16) + 10;
    if (bed_sw.x - 14 - bed_px > 60) {
        char bp[96];
        fit_text(bp, sizeof(bp), ptc_ui_text(PTC_UI_T_SETUP_BEDTIME_DEFAULT_PARAMS), 13, bed_sw.x - 14 - bed_px);
        draw_text(pixels, stride, bed_px, bed_rect.y + 34, bp, 13, bed_on ? UI_NIGHT : UI_MUTED);
    }
    draw_wrapped_text(pixels, stride, bed_tx, bed_rect.y + 58,
        ptc_ui_text(PTC_UI_T_SETUP_BEDTIME_HINT), 12, bed_tw, 17, 2, UI_MUTED);

    /* 3. Restriction Notifications Card (Y=506..604) */
    UiRect notif_rect = to_uirect(ptc_ui_setup_notifications_rect());
    bool notif_focused = (model->setup_focus == 6);
    bool notif_on = model->reason_notifications_enabled;
    fill_round_rect(pixels, stride, notif_rect, 12,
        notif_focused ? UI_ACCENT_SOFT : UI_RAISED);
    if (notif_focused) draw_focus_ring(pixels, stride, notif_rect, 12);
    else draw_rect_outline(pixels, stride, notif_rect, 12, 1, UI_BORDER);
    UiRect notif_ico = {notif_rect.x + 14, notif_rect.y + 26, 44, 44};
    fill_round_rect(pixels, stride, notif_ico, 10, UI_SURFACE);
    draw_rect_outline(pixels, stride, notif_ico, 10, 1, notif_on ? UI_ACCENT : UI_BORDER);
    draw_text_center(pixels, stride, notif_ico, "🔔", 22, UI_WARNING);
    char notif_pill[48];
    snprintf(notif_pill, sizeof(notif_pill), "%s >",
        ptc_ui_text(notif_on ? PTC_UI_T_ENABLED : PTC_UI_T_DISABLED_2));
    int pill_left = setup_draw_pill_right(pixels, stride, notif_rect.x + notif_rect.width - 14,
        notif_rect.y + 35, 28, notif_pill, 13,
        notif_on ? UI_SUCCESS_SOFT : UI_SURFACE,
        notif_on ? UI_SUCCESS_BORDER : UI_BORDER,
        notif_on ? UI_SUCCESS : UI_ACCENT);
    int notif_tx = notif_ico.x + notif_ico.width + 14;
    int notif_tw = pill_left - notif_tx - 12;
    char fitted_nt[96];
    fit_text(fitted_nt, sizeof(fitted_nt), ptc_ui_text(PTC_UI_T_ALERT_PREFERENCES), 16, notif_tw);
    draw_text_bold(pixels, stride, notif_tx, notif_rect.y + 34, fitted_nt, 16, UI_INK);
    draw_wrapped_text(pixels, stride, notif_tx, notif_rect.y + 58,
        ptc_ui_text(PTC_UI_T_SETUP_NOTIF_HINT), 12, notif_tw, 17, 2, UI_MUTED);
}

typedef struct {
    const char *label;
    const char *badge;
    uint32_t badge_bg;
    uint32_t badge_border;
    uint32_t badge_fg;
    const char *value;
    uint32_t value_color;
    const char *sub;
} SetupSummaryTile;

static void draw_setup_step3(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char env_val[160], quota_val[160], mode_text[64], quota_text[64];
    snprintf(env_val, sizeof(env_val), "HOS %s | AMS %s",
        model->environment_available && model->environment_hos[0] ? model->environment_hos : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED),
        model->environment_available && model->environment_atmosphere_version[0] ? model->environment_atmosphere_version : ptc_ui_text(PTC_UI_T_STATUS_UNCONFIRMED));
    if (model->status_loaded) {
        ptc_ui_format_today_mode(model, mode_text, sizeof(mode_text));
        ptc_ui_format_quota_remaining(model, quota_text, sizeof(quota_text));
        snprintf(quota_val, sizeof(quota_val), "%s  %s", mode_text, quota_text);
    } else {
        snprintf(quota_val, sizeof(quota_val), "%s", ptc_ui_text(PTC_UI_T_SETUP_STATUS_UNKNOWN));
    }

    bool env_ok = ptc_ui_setup_reference_matches(model);
    bool pctl_ok = model->restriction_enabled_available && model->restriction_enabled;
    bool pin_ok = model->setup_pin_ready;
    bool eye_on = model->setup_eye_care_enabled || model->draft_eye_care_policy.enabled;
    bool bed_on = model->setup_bedtime_enabled || model->draft_bedtime_policy.enabled;

    SetupSummaryTile tiles[6] = {
        {
            ptc_ui_text(PTC_UI_T_SETUP_CARD_SYSTEM_ENV),
            ptc_ui_text(env_ok ? PTC_UI_T_SETUP_ENV_MATCH_BADGE : PTC_UI_T_SETUP_ENV_DIFF_BADGE),
            env_ok ? UI_SUCCESS_SOFT : UI_WARNING_SOFT,
            env_ok ? UI_SUCCESS_BORDER : UI_WARNING_BORDER,
            env_ok ? UI_SUCCESS : UI_WARNING_TEXT,
            env_val, UI_INK,
            ptc_ui_text(env_ok ? PTC_UI_T_SETUP_REFERENCE_MATCH : PTC_UI_T_SETUP_REFERENCE_UNKNOWN)
        },
        {
            ptc_ui_text(PTC_UI_T_SETUP_SUMMARY_TODAY_QUOTA),
            ptc_ui_text(model->status_loaded ? PTC_UI_T_SETUP_SUMMARY_KEPT : PTC_UI_T_TO_BE_CONFIRMED),
            model->status_loaded ? UI_ACCENT_SOFT : UI_WARNING_SOFT,
            model->status_loaded ? UI_ACCENT : UI_WARNING_BORDER,
            model->status_loaded ? UI_ACCENT : UI_WARNING_TEXT,
            quota_val, model->status_loaded ? UI_ACCENT : UI_WARNING_TEXT,
            ptc_ui_text(model->status_loaded ? PTC_UI_T_QUOTA_RULES : PTC_UI_T_THE_STATUS_NEEDS_TO_BE_CONFIRMED_PLEASE)
        },
        {
            ptc_ui_text(PTC_UI_T_SETUP_SUMMARY_PCTL),
            ptc_ui_text(pctl_ok ? PTC_UI_T_ENABLED : PTC_UI_T_SETUP_PREFLIGHT_CHECK),
            pctl_ok ? UI_SUCCESS_SOFT : UI_WARNING_SOFT,
            pctl_ok ? UI_SUCCESS_BORDER : UI_WARNING_BORDER,
            pctl_ok ? UI_SUCCESS : UI_WARNING_TEXT,
            ptc_ui_text(!model->restriction_enabled_available ? PTC_UI_T_SETUP_PCTL_UNKNOWN :
                model->restriction_enabled ? PTC_UI_T_SETUP_PCTL_ON : PTC_UI_T_SETUP_PCTL_OFF),
            pctl_ok ? UI_SUCCESS : UI_WARNING_TEXT,
            ptc_ui_text(PTC_UI_T_SETUP_PCTL_PATH)
        },
        {
            ptc_ui_text(PTC_UI_T_SETUP_SUMMARY_PIN),
            ptc_ui_text(pin_ok ? PTC_UI_T_SETUP_PIN_BADGE_CUSTOM : PTC_UI_T_SETUP_PIN_BADGE_DEFAULT),
            pin_ok ? UI_SUCCESS_SOFT : UI_WARNING_SOFT,
            pin_ok ? UI_SUCCESS_BORDER : UI_WARNING_BORDER,
            pin_ok ? UI_SUCCESS : UI_WARNING_TEXT,
            ptc_ui_text(pin_ok ? PTC_UI_T_SETUP_PIN_READY : PTC_UI_T_SETUP_PIN_UNCONFIRMED),
            pin_ok ? UI_SUCCESS : UI_WARNING_TEXT,
            ptc_ui_text(PTC_UI_T_SETUP_PIN_DESC)
        },
        {
            "🌿 ",
            ptc_ui_text(eye_on ? PTC_UI_T_ENABLED_2 : PTC_UI_T_DISABLED),
            eye_on ? UI_SUCCESS_SOFT : UI_SURFACE,
            eye_on ? UI_SUCCESS_BORDER : UI_BORDER,
            eye_on ? UI_SUCCESS : UI_MUTED,
            ptc_ui_text(eye_on ? PTC_UI_T_SETUP_EYE_CARE_DEFAULT_PARAMS : PTC_UI_T_SETUP_EYE_CARE_SUMMARY_OFF),
            eye_on ? UI_SUCCESS : UI_MUTED,
            ptc_ui_text(PTC_UI_T_SETUP_EYE_CARE_HINT)
        },
        {
            "🌙 ",
            ptc_ui_text(bed_on ? PTC_UI_T_ENABLED_2 : PTC_UI_T_DISABLED),
            bed_on ? UI_NIGHT_SOFT : UI_SURFACE,
            bed_on ? UI_NIGHT_BORDER : UI_BORDER,
            bed_on ? UI_NIGHT : UI_MUTED,
            ptc_ui_text(bed_on ? PTC_UI_T_SETUP_BEDTIME_DEFAULT_PARAMS : PTC_UI_T_SETUP_BEDTIME_SUMMARY_OFF),
            bed_on ? UI_NIGHT : UI_MUTED,
            ptc_ui_text(PTC_UI_T_SETUP_BEDTIME_HINT)
        }
    };
    char eye_tile_lbl[96], bed_tile_lbl[96];
    snprintf(eye_tile_lbl, sizeof(eye_tile_lbl), "🌿 %s", ptc_ui_text(PTC_UI_T_SETUP_EYE_CARE_LABEL));
    snprintf(bed_tile_lbl, sizeof(bed_tile_lbl), "🌙 %s", ptc_ui_text(PTC_UI_T_SETUP_BEDTIME_LABEL));
    tiles[4].label = eye_tile_lbl;
    tiles[5].label = bed_tile_lbl;

    /* Card 1: Configuration Summary (Y=178..452, H=274) */
    UiRect sum_card = {54, 178, 1172, 274};
    setup_draw_surface_card(pixels, stride, sum_card, 16);
    draw_text_bold(pixels, stride, 74, 212, ptc_ui_text(PTC_UI_T_SETUP_CARD_SUMMARY), 18, UI_INK);

    for (int i = 0; i < 6; ++i) {
        int col = i % 3;
        int row = i / 3;
        UiRect t = {74 + col * 382, 234 + row * 106, 368, 94};
        fill_round_rect(pixels, stride, t, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, t, 12, 1, UI_BORDER);
        int bx = setup_draw_pill_right(pixels, stride, t.x + t.width - 12, t.y + 10, 22,
            tiles[i].badge, 11, tiles[i].badge_bg, tiles[i].badge_border, tiles[i].badge_fg);
        char fl[96], fv[128], fs[160];
        fit_text(fl, sizeof(fl), tiles[i].label, 13, bx - (t.x + 14) - 8);
        draw_text(pixels, stride, t.x + 14, t.y + 26, fl, 13, UI_MUTED);
        fit_text(fv, sizeof(fv), tiles[i].value, 15, t.width - 28);
        draw_text_bold(pixels, stride, t.x + 14, t.y + 54, fv, 15, tiles[i].value_color);
        fit_text(fs, sizeof(fs), tiles[i].sub, 12, t.width - 28);
        draw_text(pixels, stride, t.x + 14, t.y + 78, fs, 12, UI_MUTED);
    }

    /* Card 2: Protection & Safety Pillars (Y=466..632, H=166) */
    UiRect note_card = {54, 466, 1172, 166};
    setup_draw_surface_card(pixels, stride, note_card, 16);

    const char *status_note;
    char status_buf[160];
    bool status_warning = false;
    if (ptc_ui_setup_has_issues(model)) {
        int count = 0;
        for (int i = 0; i < PTC_UI_SETUP_ISSUE_COUNT; ++i)
            if (model->setup_issue_mask & (1U << i)) ++count;
        unsigned int save_issues = (1U << PTC_UI_SETUP_ISSUE_LANGUAGE) | (1U << PTC_UI_SETUP_ISSUE_THEME) |
            (1U << PTC_UI_SETUP_ISSUE_SHORTCUT) | (1U << PTC_UI_SETUP_ISSUE_PROGRESS) | (1U << PTC_UI_SETUP_ISSUE_DEFAULTS);
        if (count && (model->setup_issue_mask & save_issues))
            snprintf(status_buf, sizeof(status_buf), "%s", ptc_ui_text(PTC_UI_T_SETUP_SAVE_PROBLEM));
        else if (count)
            snprintf(status_buf, sizeof(status_buf), ptc_ui_text(PTC_UI_T_SETUP_ISSUE_COUNT), count);
        else
            status_buf[0] = '\0';
        status_note = status_buf;
        status_warning = count > 0;
    } else {
        status_note = ptc_ui_text(model->setup_activation_pending
            ? PTC_UI_T_SETUP_ACTIVATING : PTC_UI_T_SETUP_CONTROLS_INACTIVE);
    }
    if (model->message[0]) {
        status_note = model->message;
        status_warning = true;
    }

    const char *takeover_title = ptc_ui_text(PTC_UI_T_SETUP_CARD_TAKEOVER);
    draw_text_bold(pixels, stride, 74, 500, takeover_title, 18, UI_INK);
    if (status_note[0]) {
        int st_max_w = 1206 - (74 + measure_text(takeover_title, 18) + 24);
        if (st_max_w > 120) {
            char fitted_st[192];
            fit_text(fitted_st, sizeof(fitted_st), status_note, 13, st_max_w);
            if (status_warning) {
                (void)setup_draw_pill_right(pixels, stride, 1206, 480, 26, fitted_st, 13,
                    UI_WARNING_SOFT, UI_WARNING_BORDER, UI_WARNING_TEXT);
            } else {
                int sw = measure_text(fitted_st, 13);
                draw_text(pixels, stride, 1206 - sw, 498, fitted_st, 13, UI_MUTED);
            }
        }
    }

    const PtcUiTextId p_titles[3] = {
        PTC_UI_T_SETUP_PILLAR_1_TITLE,
        PTC_UI_T_SETUP_PILLAR_2_TITLE,
        PTC_UI_T_SETUP_PILLAR_3_TITLE
    };
    const PtcUiTextId p_descs[3] = {
        PTC_UI_T_SETUP_PILLAR_1_DESC,
        PTC_UI_T_SETUP_PILLAR_2_DESC,
        PTC_UI_T_SETUP_PILLAR_3_DESC
    };
    for (int i = 0; i < 3; ++i) {
        UiRect pil = {74 + i * 382, 520, 368, 94};
        fill_round_rect(pixels, stride, pil, 10, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, pil, 10, 1, UI_BORDER);
        char ft[96];
        fit_text(ft, sizeof(ft), ptc_ui_text(p_titles[i]), 14, pil.width - 24);
        draw_text_bold(pixels, stride, pil.x + 12, pil.y + 28, ft, 14, UI_ACCENT);
        draw_wrapped_text(pixels, stride, pil.x + 12, pil.y + 52,
            ptc_ui_text(p_descs[i]), 12, pil.width - 24, 17, 2, UI_MUTED);
    }

    char skip_label[96];
    snprintf(skip_label, sizeof(skip_label), "X  %s", ptc_ui_text(PTC_UI_T_SETUP_SKIP));
    setup_draw_footer_cta(pixels, stride, ptc_ui_setup_skip_rect(), skip_label,
        false, model->setup_focus == 1);
}

void draw_setup(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    if (model->setup_review_active) {
        draw_setup_review(pixels, stride, model);
        return;
    }
    int step = model->setup_step > 0 ? model->setup_step : PTC_UI_SETUP_PREPARE;
    const char *labels[] = {
        ptc_ui_text(PTC_UI_T_SETUP_PREPARE),
        ptc_ui_text(PTC_UI_T_SETUP_PARENT),
        ptc_ui_text(PTC_UI_T_SETUP_CONFIRM)
    };
    char text[128];
    snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_SETUP_TITLE), step);
    draw_header(pixels, stride, text, labels[step - 1]);
    snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_SETUP_STEP_BADGE), step);
    setup_draw_header_badge(pixels, stride, text);
    setup_draw_stepper(pixels, stride, step);

    if (step == PTC_UI_SETUP_PREPARE) {
        draw_setup_step1(pixels, stride, model);
    } else if (step == PTC_UI_SETUP_PARENT) {
        draw_setup_step2(pixels, stride, model);
    } else {
        draw_setup_step3(pixels, stride, model);
    }

    if (model->message[0] && step != PTC_UI_SETUP_CONFIRM) {
        char fitted[256];
        fit_text(fitted, sizeof(fitted), model->message, 13, 1172);
        draw_text(pixels, stride, 54, 646, fitted, 13, UI_WARNING_TEXT);
    }

    draw_footer_button(pixels, stride, ptc_ui_setup_back_rect(), ptc_ui_text(PTC_UI_T_B_PREVIOUS_STEP));
    char primary_label[96];
    snprintf(primary_label, sizeof(primary_label), "A  %s", ptc_ui_text(
        step == PTC_UI_SETUP_PREPARE ? PTC_UI_T_SETUP_NEXT_PREPARE :
        step == PTC_UI_SETUP_PARENT ? PTC_UI_T_SETUP_NEXT : PTC_UI_T_SETUP_ACTIVATE));
    setup_draw_footer_cta(pixels, stride, ptc_ui_setup_primary_rect(), primary_label,
        true, model->setup_focus == 0);
}

void draw_error(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect panel = {214, 148, 852, 444};
    char fitted[192];
    char execution[150];
    draw_header(pixels, stride, ptc_ui_text(PTC_UI_T_OPERATION_INCOMPLETE), ptc_ui_text(PTC_UI_T_PLEASE_CHECK_THE_ERROR_MESSAGE_AND_TRY));
    fill_round_rect(pixels, stride, panel, 16, UI_SURFACE);
    draw_rect_outline(pixels, stride, panel, 16, 1, UI_BORDER);
    fill_round_rect(pixels, stride, (UiRect){254, 194, 64, 64}, 16, UI_DANGER);
    draw_text_center(pixels, stride, (UiRect){254, 194, 64, 64}, "!", 34, UI_ON_ACCENT);
    draw_text(pixels, stride, 342, 214, ptc_ui_text(PTC_UI_T_GRANT_CODE_PROCESSING_FAILED), 28, UI_INK);
    snprintf(
        execution,
        sizeof(execution),
        ptc_ui_text(PTC_UI_T_COMMAND_S_S),
        model->command_name[0] ? model->command_name : (ptc_ui_text(PTC_UI_T_NOT_STARTED)),
        model->transport_label[0] ? model->transport_label : (ptc_ui_text(PTC_UI_T_TRANSPORT_NOT_STARTED)));

    fit_text(fitted, sizeof(fitted), execution, 18, 756);
    draw_text(pixels, stride, 254, 286, fitted, 18, UI_MUTED);
    fit_text(fitted, sizeof(fitted), model->message, 23, 756);
    draw_text(pixels, stride, 254, 342, fitted, 23, UI_MUTED);
    if (model->feedback_detail[0]) {
        fit_text(fitted, sizeof(fitted), model->feedback_detail, 17, 756);
        draw_text(pixels, stride, 254, 390, fitted, 17, UI_DANGER);
    }

    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_retry_rect()), 12, UI_ACCENT);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_retry_rect()),
                    model->error_code == 306 ? ptc_ui_text(PTC_UI_T_A_RETEST) : ptc_ui_text(PTC_UI_T_A_RE_ENTER), 25, UI_ON_ACCENT);
    fill_round_rect(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 12, UI_RAISED);
    draw_rect_outline(pixels, stride, to_uirect(ptc_ui_error_back_rect()), 12, 1, UI_CONTROL);
    draw_text_center(pixels, stride, to_uirect(ptc_ui_error_back_rect()), ptc_ui_text(PTC_UI_T_B_RETURN_TO_HOMEPAGE), 25, UI_INK);
}
