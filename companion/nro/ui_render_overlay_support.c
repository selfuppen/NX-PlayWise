#include "ui_render_internal.h"
#include "../../common/rules/rules.h"
#include "../../common/update/update.h"
#include "../../common/version.h"

static PtcUiTextId update_status_key(int status)
{
    static const PtcUiTextId keys[] = {
        PTC_UI_T_UPDATE_IDLE, PTC_UI_T_UPDATE_CHECKING, PTC_UI_T_UPDATE_CURRENT, PTC_UI_T_UPDATE_AVAILABLE,
        PTC_UI_T_UPDATE_LOCAL_NEWER, PTC_UI_T_UPDATE_CHANNEL_EXCLUDED, PTC_UI_T_UPDATE_DOWNLOADING,
        PTC_UI_T_UPDATE_VERIFYING, PTC_UI_T_UPDATE_BACKUP_PENDING, PTC_UI_T_UPDATE_INSTALLING,
        PTC_UI_T_UPDATE_AWAITING_REOPEN, PTC_UI_T_UPDATE_COMPLETED, PTC_UI_T_UPDATE_FAILED,
        PTC_UI_T_UPDATE_CANCELED, PTC_UI_T_UPDATE_RECOVERY_REQUIRED, PTC_UI_T_UPDATE_MANUAL_REQUIRED,
        PTC_UI_T_UPDATE_BACKEND_PENDING, PTC_UI_T_UPDATE_RECONFIRM, PTC_UI_T_UPDATE_BACKUP_UNKNOWN
    };
    return status >= 0 && (size_t)status < sizeof(keys) / sizeof(keys[0]) ? keys[status] : PTC_UI_T_UPDATE_FAILED;
}

static void draw_update_step_arrow(uint32_t *pixels, uint32_t stride, int x_start, int x_end, int cy, uint32_t color)
{
    draw_line(pixels, stride, x_start, cy, x_end, cy, 2, color);
    draw_line(pixels, stride, x_end - 5, cy - 4, x_end, cy, 2, color);
    draw_line(pixels, stride, x_end - 5, cy + 4, x_end, cy, 2, color);
}

typedef struct {
    char date[40];
    char status[64];
    char release_id[112];
    char short_ver[64];
    char build_hash[32];
    bool verified;
} BackupLabelInfo;

static void parse_backup_label(const char *raw, BackupLabelInfo *out)
{
    memset(out, 0, sizeof(*out));
    out->verified = true;
    if (!raw || !raw[0]) {
        snprintf(out->date, sizeof(out->date), "-");
        snprintf(out->short_ver, sizeof(out->short_ver), "-");
        snprintf(out->release_id, sizeof(out->release_id), "-");
        return;
    }
    const char *nl = strchr(raw, '\n');
    size_t line1_len = nl ? (size_t)(nl - raw) : strlen(raw);
    char line1[128];
    if (line1_len >= sizeof(line1)) line1_len = sizeof(line1) - 1;
    memcpy(line1, raw, line1_len);
    line1[line1_len] = '\0';

    const char *sep = strstr(line1, " / ");
    if (sep) {
        size_t dlen = (size_t)(sep - line1);
        if (dlen >= sizeof(out->date)) dlen = sizeof(out->date) - 1;
        memcpy(out->date, line1, dlen);
        out->date[dlen] = '\0';
        snprintf(out->status, sizeof(out->status), "%.63s", sep + 3);
    } else {
        snprintf(out->date, sizeof(out->date), "%.39s", line1);
    }

    const char *failed_lbl = ptc_ui_text(PTC_UI_T_UPDATE_FAILED);
    if (out->status[0] && failed_lbl && strcmp(out->status, failed_lbl) == 0) {
        out->verified = false;
    }

    const char *rel = (nl && nl[1]) ? (nl + 1) : "";
    snprintf(out->release_id, sizeof(out->release_id), "%.111s", rel[0] ? rel : "-");

    /* Derive compact version string and optional build hash from release_id */
    const char *vstart = rel;
    if (strncmp(vstart, "playwise-", 9) == 0) vstart += 9;
    const char *plus = strchr(vstart, '+');
    if (plus) {
        size_t vlen = (size_t)(plus - vstart);
        if (vlen >= sizeof(out->short_ver) - 2) vlen = sizeof(out->short_ver) - 2;
        if (vlen > 0 && vstart[0] != 'v' && vstart[0] != 'V') {
            out->short_ver[0] = 'v';
            memcpy(out->short_ver + 1, vstart, vlen);
            out->short_ver[vlen + 1] = '\0';
        } else {
            memcpy(out->short_ver, vstart, vlen);
            out->short_ver[vlen] = '\0';
        }
        snprintf(out->build_hash, sizeof(out->build_hash), "build %.15s", plus);
    } else if (vstart[0]) {
        if (vstart[0] != 'v' && vstart[0] != 'V')
            snprintf(out->short_ver, sizeof(out->short_ver), "v%.60s", vstart);
        else
            snprintf(out->short_ver, sizeof(out->short_ver), "%.62s", vstart);
    } else {
        snprintf(out->short_ver, sizeof(out->short_ver), "-");
    }
}

static void draw_online_update(uint32_t *pixels, uint32_t stride, const PtcUiModel *m)
{
    char text[512];
    if (m->overlay == PTC_UI_OVERLAY_ONLINE_UPDATE && m->update_notes_open) {
        char fitted[256];
        draw_subpage_shell(pixels, stride, m,
                           ptc_ui_text(PTC_UI_T_UPDATE_NOTES_PAGE_TITLE),
                           ptc_ui_text(PTC_UI_T_UPDATE_TRUST),
                           ptc_ui_text(PTC_UI_T_UPDATE_BACK_TO_UPDATE),
                           ptc_ui_text(PTC_UI_T_UPDATE_NOTES_PATH));

        /* Left version metadata panel (x=54..394, w=340, y=174..638) */
        UiRect meta = {54, 174, 340, 464};
        draw_plan_card(pixels, stride, meta, false);
        {
            bool is_avail = (m->update_status == PTC_UPDATE_AVAILABLE);
            bool is_err = (m->update_status == PTC_UPDATE_FAILED || m->update_status == PTC_UPDATE_RECOVERY_REQUIRED ||
                           m->update_status == PTC_UPDATE_MANUAL_REQUIRED);
            uint32_t p_bg = is_err ? UI_DANGER_SOFT : (is_avail ? UI_SUCCESS_SOFT : UI_ACCENT_SOFT);
            uint32_t p_bd = is_err ? UI_DANGER_BORDER : (is_avail ? UI_SUCCESS_BORDER : UI_ACCENT);
            uint32_t p_fg = is_err ? UI_DANGER : (is_avail ? UI_SUCCESS : UI_ACCENT);

            const char *st_txt = ptc_ui_text(update_status_key(m->update_status));
            int pw = measure_text(st_txt, 12) + 22;
            if (pw > meta.width - 40) pw = meta.width - 40;
            UiRect st_pill = {meta.x + 20, meta.y + 20, pw, 26};
            fill_round_rect(pixels, stride, st_pill, 13, p_bg);
            draw_rect_outline(pixels, stride, st_pill, 13, 1, p_bd);
            fit_text(fitted, sizeof(fitted), st_txt, 12, st_pill.width - 12);
            draw_text_center(pixels, stride, st_pill, fitted, 12, p_fg);

            char ver_disp[80];
            if (m->update_version[0]) {
                if (m->update_version[0] == 'v' || m->update_version[0] == 'V')
                    snprintf(ver_disp, sizeof(ver_disp), "%s", m->update_version);
                else
                    snprintf(ver_disp, sizeof(ver_disp), "v%s", m->update_version);
            } else {
                snprintf(ver_disp, sizeof(ver_disp), "v%s", PLAYWISE_VERSION);
            }
            fit_text(fitted, sizeof(fitted), ver_disp, 28, meta.width - 40);
            draw_text_bold(pixels, stride, meta.x + 20, meta.y + 84, fitted, 28, UI_INK);

            const char *chan_txt = ptc_ui_text(m->update_stable_only
                ? PTC_UI_T_UPDATE_STABLE : PTC_UI_T_UPDATE_FOLLOW);
            fit_text(fitted, sizeof(fitted), chan_txt, 13, meta.width - 40);
            draw_text(pixels, stride, meta.x + 20, meta.y + 108, fitted, 13, UI_MUTED);

            /* 4-row metadata key-value subcard */
            UiRect kv = {meta.x + 20, meta.y + 124, meta.width - 40, 152};
            fill_round_rect(pixels, stride, kv, 10, UI_PAGE);
            draw_rect_outline(pixels, stride, kv, 10, 1, UI_BORDER);
            const PtcUiTextId kv_labels[4] = {
                PTC_UI_T_UPDATE_NOTES_FIELD_NOW,
                PTC_UI_T_UPDATE_NOTES_FIELD_SIZE,
                PTC_UI_T_UPDATE_NOTES_FIELD_SOURCE,
                PTC_UI_T_UPDATE_NOTES_FIELD_VERIFY
            };
            char kv_vals[4][64];
            snprintf(kv_vals[0], sizeof(kv_vals[0]), "v%s", PLAYWISE_VERSION);
            if (m->update_size_kib) snprintf(kv_vals[1], sizeof(kv_vals[1]), "%u KiB", m->update_size_kib);
            else snprintf(kv_vals[1], sizeof(kv_vals[1]), "-");
            snprintf(kv_vals[2], sizeof(kv_vals[2]), "%s",
                     m->update_source == 2 ? "GitHub" : (m->update_source == 1 ? "Gitee" : "Gitee / GitHub"));
            snprintf(kv_vals[3], sizeof(kv_vals[3]), "SHA-256");

            for (int r = 0; r < 4; ++r) {
                int ry = kv.y + 28 + r * 35;
                if (r > 0) draw_line(pixels, stride, kv.x + 14, ry - 23, kv.x + kv.width - 14, ry - 23, 1, UI_BORDER);
                const char *lbl = ptc_ui_text(kv_labels[r]);
                draw_text(pixels, stride, kv.x + 14, ry, lbl, 13, UI_MUTED);
                fit_text(fitted, sizeof(fitted), kv_vals[r], 14, kv.width - 120);
                int vw = measure_text(fitted, 14);
                draw_text_bold(pixels, stride, kv.x + kv.width - 14 - vw, ry, fitted, 14,
                               r == 3 ? UI_SUCCESS : UI_INK);
            }

            /* Side assurance note */
            UiRect note_card = {meta.x + 20, meta.y + 292, meta.width - 40, 152};
            fill_round_rect(pixels, stride, note_card, 10, UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, note_card, 10, 1, UI_BORDER);
            draw_wrapped_text(pixels, stride, note_card.x + 12, note_card.y + 28,
                              ptc_ui_text(PTC_UI_T_UPDATE_NOTES_SIDE_NOTE),
                              13, note_card.width - 24, 21, 6, UI_INK);
        }

        /* Right comfortable reading surface (x=410..1226, w=816, y=174..638) */
        const int reader_w = 760;
        const int lines_per_page = 11;
        int lines = count_wrapped_lines(m->update_notes, 18, reader_w, 2048);
        int pages = (lines + lines_per_page - 1) / lines_per_page;
        if (pages < 1) pages = 1;
        int page = (m->update_notes_page % pages + pages) % pages;

        UiRect reader = {410, 174, 816, 464};
        draw_plan_card(pixels, stride, reader, false);

        /* Reader top header bar */
        draw_text_bold(pixels, stride, reader.x + 24, reader.y + 34,
                       ptc_ui_text(PTC_UI_T_UPDATE_NOTES_READER_TITLE), 18, UI_INK);
        {
            int dots = pages < 8 ? pages : 8;
            int active_dot = pages < 8 ? page : (page * (dots - 1) / (pages > 1 ? pages - 1 : 1));
            int dot_w = 24, dot_gap = 6;
            int total_dots_w = dots * dot_w + (dots - 1) * dot_gap;
            int dx = reader.x + reader.width - 24 - total_dots_w;
            for (int d = 0; d < dots; ++d) {
                fill_round_rect(pixels, stride, (UiRect){dx + d * (dot_w + dot_gap), reader.y + 24, dot_w, 6},
                                3, d == active_dot ? UI_ACCENT : UI_BORDER);
            }
        }
        draw_line(pixels, stride, reader.x + 24, reader.y + 52, reader.x + reader.width - 24, reader.y + 52, 1, UI_BORDER);

        /* Reader main text body */
        draw_wrapped_text_from_line(pixels, stride, reader.x + 28, reader.y + 84,
                                    m->update_notes, 18, reader_w, 28, lines_per_page, page * lines_per_page, UI_INK);

        /* Reader bottom pagination footer */
        draw_line(pixels, stride, reader.x + 24, reader.y + reader.height - 48,
                  reader.x + reader.width - 24, reader.y + reader.height - 48, 1, UI_BORDER);
        draw_text(pixels, stride, reader.x + 24, reader.y + reader.height - 18,
                  ptc_ui_text(PTC_UI_T_UPDATE_NOTES_NAV_HINT), 13, UI_MUTED);
        snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_NOTES_PAGE_FMT), page + 1, pages);
        int pg_w = measure_text(text, 14);
        draw_text_bold(pixels, stride, reader.x + reader.width - 24 - pg_w,
                       reader.y + reader.height - 18, text, 14, UI_ACCENT);

        home_button(pixels, stride, ptc_ui_settings_page_footer_rect(2), ptc_ui_text(PTC_UI_T_GUIDE_PREV), false, false, pages <= 1);
        home_button(pixels, stride, ptc_ui_settings_page_primary_rect(), ptc_ui_text(PTC_UI_T_GUIDE_NEXT), false, false, pages <= 1);
        return;
    }

    if (m->overlay == PTC_UI_OVERLAY_UPDATE_BACKUPS) {
        char fitted[256];
        draw_subpage_shell(pixels, stride, m,
                           ptc_ui_text(PTC_UI_T_UPDATE_BACKUPS),
                           ptc_ui_text(PTC_UI_T_UPDATE_BACKUPS_SUB),
                           ptc_ui_text(PTC_UI_T_UPDATE_BACK_TO_UPDATE),
                           ptc_ui_text(PTC_UI_T_UPDATE_BACKUPS_PATH));

        if (!m->update_backup_count) {
            UiRect empty_card = {54, 174, 1172, 464};
            draw_plan_card(pixels, stride, empty_card, false);
            UiRect icon_box = {empty_card.x + (empty_card.width - 68) / 2, empty_card.y + 116, 68, 68};
            fill_round_rect(pixels, stride, icon_box, 18, UI_RAISED);
            draw_rect_outline(pixels, stride, icon_box, 18, 1, UI_BORDER);
            draw_text_center(pixels, stride, icon_box, "0", 28, UI_MUTED);

            const char *no_title = ptc_ui_text(PTC_UI_T_UPDATE_NO_BACKUPS);
            int tw = measure_text(no_title, 22);
            draw_text_bold(pixels, stride, empty_card.x + (empty_card.width - tw) / 2,
                           empty_card.y + 226, no_title, 22, UI_INK);
            draw_wrapped_text_centered(pixels, stride, empty_card.x + empty_card.width / 2,
                                       empty_card.y + 266, ptc_ui_text(PTC_UI_T_UPDATE_NO_BACKUPS_DESC),
                                       15, 620, 24, 3, UI_MUTED);
        } else {
            /* Left column: compact SD archive list (x=54..640, w=586) */
            snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_BACKUPS_LIST_HDR), m->update_backup_count);
            draw_text_bold(pixels, stride, 58, 194, text, 14, UI_MUTED);

            char cur_pill_txt[64];
            snprintf(cur_pill_txt, sizeof(cur_pill_txt), ptc_ui_text(PTC_UI_T_UPDATE_CURRENT_VER_PILL), PLAYWISE_VERSION);
            int cpw = measure_text(cur_pill_txt, 12) + 20;
            if (cpw > 180) cpw = 180;
            UiRect cur_pill = {640 - cpw, 174, cpw, 24};
            fill_round_rect(pixels, stride, cur_pill, 12, UI_RAISED);
            draw_rect_outline(pixels, stride, cur_pill, 12, 1, UI_BORDER);
            fit_text(fitted, sizeof(fitted), cur_pill_txt, 12, cur_pill.width - 10);
            draw_text_center(pixels, stride, cur_pill, fitted, 12, UI_MUTED);

            int sel_idx = m->update_backup_index + m->overlay_selection;
            if (sel_idx < 0 || sel_idx >= m->update_backup_count) sel_idx = m->update_backup_index;
            if (sel_idx < 0 || sel_idx >= m->update_backup_count) sel_idx = 0;

            for (int i = 0; i < 4 && m->update_backup_index + i < m->update_backup_count; ++i) {
                int abs_i = m->update_backup_index + i;
                PtcUiRect pr = ptc_ui_update_backup_rect(i);
                UiRect r = {pr.x, pr.y, pr.w, pr.h};
                bool focused = (abs_i == sel_idx);
                BackupLabelInfo info;
                parse_backup_label(m->update_backup_labels[abs_i], &info);

                draw_plan_card(pixels, stride, r, focused);

                /* Row 1: Version + optional latest badge + right verification pill */
                const char *st_txt = ptc_ui_text(info.verified
                    ? PTC_UI_T_UPDATE_BACKUP_COMP_SHA : PTC_UI_T_UPDATE_BACKUP_INCOMPLETE_BADGE);
                int sw = measure_text(st_txt, 12) + 18;
                if (sw > 148) sw = 148;
                UiRect st_pill = {r.x + r.width - 16 - sw, r.y + 16, sw, 24};
                fill_round_rect(pixels, stride, st_pill, 12, info.verified ? UI_SUCCESS_SOFT : UI_DANGER_SOFT);
                draw_rect_outline(pixels, stride, st_pill, 12, 1, info.verified ? UI_SUCCESS_BORDER : UI_DANGER_BORDER);
                fit_text(fitted, sizeof(fitted), st_txt, 12, st_pill.width - 10);
                draw_text_center(pixels, stride, st_pill, fitted, 12, info.verified ? UI_SUCCESS : UI_DANGER);

                int vx = r.x + 18;
                int latest_w = 0;
                const char *latest_txt = ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_LATEST_BADGE);
                if (abs_i == 0) latest_w = measure_text(latest_txt, 11) + 16;
                int max_ver_w = st_pill.x - vx - (latest_w ? latest_w + 18 : 12);
                if (max_ver_w < 80) max_ver_w = 80;
                fit_text(fitted, sizeof(fitted), info.short_ver, 18, max_ver_w);
                draw_text_bold(pixels, stride, vx, r.y + 35, fitted, 18, focused ? UI_ACCENT : UI_INK);
                if (abs_i == 0) {
                    int vw = measure_text(fitted, 18);
                    UiRect lpill = {vx + vw + 10, r.y + 17, latest_w, 22};
                    if (lpill.x + lpill.width < st_pill.x - 6) {
                        fill_round_rect(pixels, stride, lpill, 11, UI_WARNING_SOFT);
                        draw_rect_outline(pixels, stride, lpill, 11, 1, UI_WARNING_BORDER);
                        draw_text_center(pixels, stride, lpill, latest_txt, 11, UI_WARNING_TEXT);
                    }
                }

                /* Row 2: Timestamp on left, build hash or release suffix on right */
                char time_row[96];
                snprintf(time_row, sizeof(time_row), ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_TIME_FMT), info.date);
                int bh_w = info.build_hash[0] ? measure_text(info.build_hash, 12) : 0;
                if (bh_w > 0) {
                    draw_text(pixels, stride, r.x + r.width - 16 - bh_w, r.y + 72, info.build_hash, 12, UI_MUTED);
                }
                fit_text(fitted, sizeof(fitted), time_row, 13, r.width - 36 - (bh_w ? bh_w + 12 : 0));
                draw_text(pixels, stride, vx, r.y + 72, fitted, 13, UI_MUTED);
            }

            /* Right column: Selected archive verification & action panel (x=656..1226, w=570, y=174..638) */
            UiRect detail = {656, 174, 570, 464};
            draw_plan_card(pixels, stride, detail, false);
            {
                BackupLabelInfo sel;
                parse_backup_label(m->update_backup_labels[sel_idx], &sel);

                const char *ready_txt = ptc_ui_text(sel.verified
                    ? PTC_UI_T_UPDATE_BACKUP_READY_BADGE : PTC_UI_T_UPDATE_BACKUP_INCOMPLETE_BADGE);
                int rw = measure_text(ready_txt, 12) + 20;
                if (rw > 180) rw = 180;
                UiRect rpill = {detail.x + detail.width - 20 - rw, detail.y + 18, rw, 26};
                fill_round_rect(pixels, stride, rpill, 13, sel.verified ? UI_SUCCESS_SOFT : UI_DANGER_SOFT);
                draw_rect_outline(pixels, stride, rpill, 13, 1, sel.verified ? UI_SUCCESS_BORDER : UI_DANGER_BORDER);
                fit_text(fitted, sizeof(fitted), ready_txt, 12, rpill.width - 10);
                draw_text_center(pixels, stride, rpill, fitted, 12, sel.verified ? UI_SUCCESS : UI_DANGER);

                fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_DETAIL_TITLE),
                         17, rpill.x - (detail.x + 20) - 10);
                draw_text_bold(pixels, stride, detail.x + 20, detail.y + 37, fitted, 17, UI_INK);

                /* Version comparison subcard */
                UiRect vdiff = {detail.x + 20, detail.y + 54, detail.width - 40, 78};
                fill_round_rect(pixels, stride, vdiff, 10, UI_PAGE);
                draw_rect_outline(pixels, stride, vdiff, 10, 1, UI_BORDER);
                draw_text(pixels, stride, vdiff.x + 16, vdiff.y + 26,
                          ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_VER_NOW), 12, UI_MUTED);
                char cur_v[48];
                snprintf(cur_v, sizeof(cur_v), "v%s", PLAYWISE_VERSION);
                fit_text(fitted, sizeof(fitted), cur_v, 19, 210);
                draw_text_bold(pixels, stride, vdiff.x + 16, vdiff.y + 58, fitted, 19, UI_INK);

                int mid_x = vdiff.x + vdiff.width / 2;
                draw_update_step_arrow(pixels, stride, mid_x - 16, mid_x + 16, vdiff.y + 42, UI_ACCENT);

                const char *arch_lbl = ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_VER_ARCHIVED);
                int alw = measure_text(arch_lbl, 12);
                draw_text(pixels, stride, vdiff.x + vdiff.width - 16 - alw, vdiff.y + 26, arch_lbl, 12, UI_MUTED);
                fit_text(fitted, sizeof(fitted), sel.short_ver, 19, 220);
                int svw = measure_text(fitted, 19);
                draw_text_bold(pixels, stride, vdiff.x + vdiff.width - 16 - svw, vdiff.y + 58, fitted, 19, UI_ACCENT);

                /* Source release ID subcard */
                UiRect src_box = {detail.x + 20, detail.y + 144, detail.width - 40, 66};
                fill_round_rect(pixels, stride, src_box, 10, UI_PAGE);
                draw_rect_outline(pixels, stride, src_box, 10, 1, UI_BORDER);
                draw_text(pixels, stride, src_box.x + 16, src_box.y + 24,
                          ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_SOURCE_ID_LBL), 12, UI_MUTED);
                int dw = measure_text(sel.date, 12);
                draw_text(pixels, stride, src_box.x + src_box.width - 16 - dw, src_box.y + 24, sel.date, 12, UI_MUTED);
                fit_text(fitted, sizeof(fitted), sel.release_id, 15, src_box.width - 32);
                draw_text_bold(pixels, stride, src_box.x + 16, src_box.y + 52, fitted, 15, UI_INK);

                /* Component status chips */
                const char *c1 = ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_COMP_CONFIG);
                int c1w = measure_text(c1, 13) + 24;
                UiRect chip1 = {detail.x + 20, detail.y + 222, c1w, 28};
                fill_round_rect(pixels, stride, chip1, 14, sel.verified ? UI_SUCCESS_SOFT : UI_RAISED);
                draw_rect_outline(pixels, stride, chip1, 14, 1, sel.verified ? UI_SUCCESS_BORDER : UI_BORDER);
                draw_text_center(pixels, stride, chip1, c1, 13, sel.verified ? UI_SUCCESS : UI_MUTED);

                const char *c2 = ptc_ui_text(sel.verified
                    ? PTC_UI_T_UPDATE_BACKUP_COMP_SHA : PTC_UI_T_UPDATE_BACKUP_INCOMPLETE_BADGE);
                int c2w = measure_text(c2, 13) + 24;
                UiRect chip2 = {chip1.x + chip1.width + 10, detail.y + 222, c2w, 28};
                fill_round_rect(pixels, stride, chip2, 14, sel.verified ? UI_SUCCESS_SOFT : UI_DANGER_SOFT);
                draw_rect_outline(pixels, stride, chip2, 14, 1, sel.verified ? UI_SUCCESS_BORDER : UI_DANGER_BORDER);
                draw_text_center(pixels, stride, chip2, c2, 13, sel.verified ? UI_SUCCESS : UI_DANGER);

                /* Safety callout box */
                UiRect safe_box = {detail.x + 20, detail.y + 264, detail.width - 40, 114};
                bool err_box = m->update_cleanup_failed || !sel.verified;
                fill_round_rect(pixels, stride, safe_box, 10, err_box ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
                draw_rect_outline(pixels, stride, safe_box, 10, 1, err_box ? UI_WARNING_BORDER : UI_SUCCESS_BORDER);
                draw_text_bold(pixels, stride, safe_box.x + 16, safe_box.y + 26,
                               ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_SAFE_TITLE), 14,
                               err_box ? UI_WARNING_TEXT : UI_SUCCESS);
                const char *safe_msg = ptc_ui_text(m->update_cleanup_failed
                    ? PTC_UI_T_UPDATE_ERROR_CONFLICT
                    : (sel.verified ? PTC_UI_T_UPDATE_BACKUP_SAFE_BODY : PTC_UI_T_UPDATE_BACKUP_FAILED_BODY));
                draw_wrapped_text(pixels, stride, safe_box.x + 16, safe_box.y + 52,
                                  safe_msg, 14, safe_box.width - 32, 20, 3,
                                  m->update_cleanup_failed ? UI_DANGER : UI_INK);

                /* Action divider + Restore / Clean buttons (shared layout rects) */
                draw_line(pixels, stride, detail.x + 20, detail.y + 390,
                          detail.x + detail.width - 20, detail.y + 390, 1, UI_BORDER);
                home_button(pixels, stride, ptc_ui_update_backup_action_rect(0),
                            ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_RESTORE_BTN),
                            true, false, !sel.verified || m->parent_support_only);
                home_button(pixels, stride, ptc_ui_update_backup_action_rect(1),
                            ptc_ui_text(PTC_UI_T_UPDATE_BACKUP_CLEAN_BTN),
                            false, false, m->parent_support_only);
            }
        }

        bool has_any = m->update_backup_count > 0;
        home_button(pixels, stride, ptc_ui_settings_page_footer_rect(1),
                    ptc_ui_text(PTC_UI_T_UPDATE_DELETE), false, false, !has_any || m->parent_support_only);
        home_button(pixels, stride, ptc_ui_settings_page_footer_rect(2),
                    ptc_ui_text(PTC_UI_T_GUIDE_PREV), false, false, m->update_backup_index == 0);
        home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                    ptc_ui_text(PTC_UI_T_GUIDE_NEXT), false, false, m->update_backup_index + 4 >= m->update_backup_count);
        return;
    }

    draw_settings_page_shell(pixels, stride, m, ptc_ui_text(PTC_UI_T_UPDATE_TITLE), ptc_ui_text(PTC_UI_T_UPDATE_TRUST));

    /* Tier 1: Version info (Scheme A chips & banner) + Release notes / progress summary box */
    draw_plan_card(pixels, stride, (UiRect){54, 168, 1172, 206}, false);
    {
        bool is_ok = m->update_status == PTC_UPDATE_COMPLETED || m->update_status == PTC_UPDATE_CURRENT ||
                     m->update_status == PTC_UPDATE_AWAITING_REOPEN;
        bool is_err = m->update_status == PTC_UPDATE_FAILED || m->update_status == PTC_UPDATE_RECOVERY_REQUIRED ||
                      m->update_status == PTC_UPDATE_MANUAL_REQUIRED;
        bool is_warn = m->update_status == PTC_UPDATE_BACKEND_PENDING || m->update_status == PTC_UPDATE_RECONFIRM ||
                       m->update_status == PTC_UPDATE_BACKUP_UNKNOWN || m->update_status == PTC_UPDATE_CANCELED;
        uint32_t st_color = is_err ? UI_DANGER : (is_ok ? UI_SUCCESS : (is_warn ? UI_WARNING_TEXT : UI_ACCENT));
        uint32_t st_dot = is_err ? UI_DANGER : (is_ok ? UI_SUCCESS : (is_warn ? UI_WARNING : UI_ACCENT));
        uint32_t st_bg = is_err ? UI_DANGER_SOFT : (is_ok ? UI_SUCCESS_SOFT : (is_warn ? UI_WARNING_SOFT : UI_ACCENT_SOFT));
        uint32_t st_bd = is_err ? UI_DANGER_BORDER : (is_ok ? UI_SUCCESS_BORDER : (is_warn ? UI_WARNING_BORDER : UI_ACCENT));

        UiRect banner = {76, 184, 656, 44};
        fill_round_rect(pixels, stride, banner, 10, st_bg);
        draw_rect_outline(pixels, stride, banner, 10, 1, st_bd);
        fill_round_rect(pixels, stride, (UiRect){banner.x + 14, banner.y + 17, 10, 10}, 5, st_dot);

        int size_w = 0;
        char size_buf[128] = "";
        if (m->update_size_kib) {
            snprintf(size_buf, sizeof(size_buf), "%u KiB / %s", m->update_size_kib,
                m->update_source == 2 ? "GitHub" : (m->update_source == 1 ? "Gitee" : "Gitee / GitHub"));
            fit_text(size_buf, sizeof(size_buf), size_buf, 12, 200);
            size_w = measure_text(size_buf, 12);
            draw_text(pixels, stride, banner.x + banner.width - 14 - size_w, banner.y + 27, size_buf, 12, UI_MUTED);
        }
        int status_max_w = banner.width - 42 - (size_w ? size_w + 16 : 0);
        fit_text(text, sizeof(text), ptc_ui_text(update_status_key(m->update_status)), 18, status_max_w);
        draw_text_bold(pixels, stride, banner.x + 32, banner.y + 28, text, 18, st_color);

        /* 3-column version metric chips (Scheme A) */
        const PtcUiTextId chip_labels[3] = {
            PTC_UI_T_UPDATE_CURRENT_APP_LABEL,
            PTC_UI_T_UPDATE_INSTALLED_LABEL,
            PTC_UI_T_UPDATE_ONLINE_LABEL
        };
        const char *chip_vals[3] = {
            PLAYWISE_VERSION,
            m->update_installed_version[0] ? m->update_installed_version : "?",
            m->update_version[0] ? m->update_version : "?"
        };
        bool avail = m->update_status == PTC_UPDATE_AVAILABLE && m->update_version[0];
        for (int c = 0; c < 3; ++c) {
            UiRect chip = {76 + c * 223, 240, 210, 74};
            bool hl = (c == 2 && avail);
            fill_round_rect(pixels, stride, chip, 10, hl ? UI_ACCENT_SOFT : UI_PAGE);
            draw_rect_outline(pixels, stride, chip, 10, 1, hl ? UI_ACCENT : UI_BORDER);
            fit_text(text, sizeof(text), ptc_ui_text(chip_labels[c]), 12, chip.width - 24);
            draw_text(pixels, stride, chip.x + 12, chip.y + 25, text, 12, hl ? UI_ACCENT : UI_MUTED);
            fit_text(text, sizeof(text), chip_vals[c], 16, chip.width - 24);
            draw_text_bold(pixels, stride, chip.x + 12, chip.y + 54, text, 16, hl ? UI_ACCENT : UI_INK);
        }

        snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_BACKEND),
            m->backend_release_id[0] ? m->backend_release_id : "?");
        fit_text(text, sizeof(text), text, 14, 656);
        draw_text(pixels, stride, 76, 344, text, 14, UI_MUTED);
    }

    /* Right embedded summary / progress box (Scheme A) */
    {
        UiRect notes_box = {750, 184, 454, 174};
        PtcUiRect notes_btn = ptc_ui_update_notes_rect();
        fill_round_rect(pixels, stride, notes_box, 12, UI_PAGE);
        draw_rect_outline(pixels, stride, notes_box, 12, 1, UI_BORDER);

        const char *box_title = ptc_ui_text(m->update_busy ? PTC_UI_T_UPDATE_PROGRESS_TITLE : PTC_UI_T_UPDATE_NOTES_SUMMARY_TITLE);
        fit_text(text, sizeof(text), box_title, 14, notes_btn.x - (notes_box.x + 16) - 10);
        draw_text_bold(pixels, stride, notes_box.x + 16, notes_box.y + 29, text, 14, UI_INK);
        home_button(pixels, stride, notes_btn, ptc_ui_text(PTC_UI_T_UPDATE_NOTES), false, false,
            m->update_busy || !m->update_notes[0]);

        if (m->update_busy) {
            if (m->update_size_kib > 0) {
                unsigned pct = m->update_received_kib * 100u / m->update_size_kib;
                if (pct > 100u) pct = 100u;
                snprintf(text, sizeof(text), "%u / %u KiB", m->update_received_kib, m->update_size_kib);
                draw_text(pixels, stride, notes_box.x + 16, notes_box.y + 68, text, 15, UI_INK);
                char pct_buf[16];
                snprintf(pct_buf, sizeof(pct_buf), "%u%%", pct);
                int pw = measure_text(pct_buf, 15);
                draw_text_bold(pixels, stride, notes_box.x + notes_box.width - 16 - pw, notes_box.y + 68, pct_buf, 15, UI_ACCENT);
                UiRect track = {notes_box.x + 16, notes_box.y + 82, notes_box.width - 32, 10};
                fill_round_rect(pixels, stride, track, 5, UI_RAISED);
                int fill_w = (int)((unsigned)track.width * pct / 100u);
                if (fill_w > 0) {
                    if (fill_w < 8) fill_w = 8;
                    if (fill_w > track.width) fill_w = track.width;
                    fill_round_rect(pixels, stride, (UiRect){track.x, track.y, fill_w, track.height}, 5, UI_ACCENT);
                }
            } else {
                snprintf(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_TRANSFER), m->update_received_kib);
                draw_text(pixels, stride, notes_box.x + 16, notes_box.y + 72, text, 16, UI_INK);
            }
            draw_wrapped_text(pixels, stride, notes_box.x + 16, notes_box.y + 118,
                ptc_ui_text(PTC_UI_T_UPDATE_TRUST), 12, notes_box.width - 32, 18, 2, UI_MUTED);
        } else {
            const PtcUiTextId errors[] = {PTC_UI_T_UPDATE_IDLE, PTC_UI_T_UPDATE_ERROR_NETWORK, PTC_UI_T_UPDATE_MANUAL_REQUIRED,
                PTC_UI_T_UPDATE_ERROR_PACKAGE, PTC_UI_T_UPDATE_ERROR_STORAGE, PTC_UI_T_UPDATE_ERROR_BACKUP, PTC_UI_T_UPDATE_ERROR_CONFLICT};
            bool has_err = m->update_error > 0 && (size_t)m->update_error < sizeof(errors) / sizeof(errors[0]);
            const char *notes = has_err ? ptc_ui_text(errors[m->update_error]) :
                (m->update_notes[0] ? m->update_notes : ptc_ui_text(PTC_UI_T_UPDATE_TRUST));
            draw_wrapped_text(pixels, stride, notes_box.x + 16, notes_box.y + 62,
                notes, 14, notes_box.width - 32, 22, 4, has_err ? UI_DANGER : UI_MUTED);
        }
    }

    /* Tier 2: 4-step update pipeline (Scheme B) */
    draw_plan_card(pixels, stride, (UiRect){54, 386, 1172, 120}, false);
    fit_text(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_STEPS_TITLE), 13, 1132);
    draw_text_bold(pixels, stride, 74, 410, text, 13, UI_MUTED);
    {
        static const int step_ids[4] = {0, 1, 7, 5};
        static const PtcUiTextId step_subs[4] = {
            PTC_UI_T_UPDATE_STEP1_SUB, PTC_UI_T_UPDATE_STEP2_SUB,
            PTC_UI_T_UPDATE_STEP3_SUB, PTC_UI_T_UPDATE_STEP4_SUB
        };
        const PtcUiTextId step_titles[4] = {
            m->update_status == PTC_UPDATE_BACKUP_UNKNOWN ? PTC_UI_T_UPDATE_WAIT_RECEIPT : PTC_UI_T_UPDATE_CHECK,
            PTC_UI_T_UPDATE_DOWNLOAD,
            PTC_UI_T_UPDATE_EXIT,
            m->update_status == PTC_UPDATE_RECONFIRM ? PTC_UI_T_UPDATE_SUPPORT : PTC_UI_T_UPDATE_LOAD
        };
        for (int s = 0; s < 4; ++s) {
            int id = step_ids[s];
            UiRect box = to_uirect(ptc_ui_update_action_rect(id));
            bool selected = (m->overlay_selection == id);
            bool enabled = ptc_ui_update_action_enabled(m, id);
            bool primary = enabled && (
                (id == 1 && m->update_status == PTC_UPDATE_AVAILABLE) ||
                (id == 7 && m->update_status == PTC_UPDATE_AWAITING_REOPEN) ||
                (id == 5 && (m->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING || m->update_status == PTC_UPDATE_RECONFIRM)) ||
                (id == 0 && (m->update_status == PTC_UPDATE_IDLE || m->update_status == PTC_UPDATE_CURRENT ||
                             m->update_status == PTC_UPDATE_BACKUP_UNKNOWN)));
            uint32_t bg = !enabled ? UI_RAISED : (primary ? UI_ACCENT : (selected ? UI_ACCENT_SOFT : UI_SURFACE));
            uint32_t fg = !enabled ? UI_DISABLED : (primary ? UI_ON_ACCENT : (selected ? UI_ACCENT : UI_INK));
            uint32_t sub_fg = !enabled ? UI_DISABLED : (primary ? UI_ON_ACCENT : UI_MUTED);

            if (enabled && !selected) draw_card_shadow(pixels, stride, box, 12);
            fill_round_rect(pixels, stride, box, 12, bg);
            if (selected && enabled) draw_focus_ring(pixels, stride, box, 12);
            else if (selected) draw_rect_outline(pixels, stride, box, 12, 2, UI_ACCENT);
            else draw_rect_outline(pixels, stride, box, 12, 1, !enabled ? UI_BORDER : (primary ? UI_ACCENT : UI_BORDER));

            char step_label[128];
            snprintf(step_label, sizeof(step_label), "%d. %s", s + 1, ptc_ui_text(step_titles[s]));
            int tsz = 16;
            while (tsz > 13 && measure_text(step_label, tsz) > box.width - 20) --tsz;
            fit_text(text, sizeof(text), step_label, tsz, box.width - 20);
            draw_text_center(pixels, stride, (UiRect){box.x + 10, box.y + 10, box.width - 20, 26}, text, tsz, fg);

            fit_text(text, sizeof(text), ptc_ui_text(step_subs[s]), 12, box.width - 20);
            draw_text_center(pixels, stride, (UiRect){box.x + 10, box.y + 38, box.width - 20, 22}, text, 12, sub_fg);

            if (s < 3) {
                draw_update_step_arrow(pixels, stride, box.x + box.width + 6, box.x + box.width + 22,
                    box.y + box.height / 2, UI_MUTED);
            }
        }
    }

    /* Tier 3: 3 preference and maintenance cards (Actions 2, 3, 4) */
    for (int i = 2; i <= 4; ++i) {
        UiRect box = to_uirect(ptc_ui_update_action_rect(i));
        bool selected = (m->overlay_selection == i);
        bool enabled = ptc_ui_update_action_enabled(m, i);
        if (enabled && !selected) draw_card_shadow(pixels, stride, box, 14);
        fill_round_rect(pixels, stride, box, 14, !enabled ? UI_RAISED : (selected ? UI_ACCENT_SOFT : UI_SURFACE));
        if (selected && enabled) draw_focus_ring(pixels, stride, box, 14);
        else if (selected) draw_rect_outline(pixels, stride, box, 14, 2, UI_ACCENT);
        else draw_rect_outline(pixels, stride, box, 14, 1, UI_BORDER);

        if (i == 2) {
            UiRect sw_rect = {box.x + box.width - 60, box.y + 20, 42, 22};
            const char *st_txt = ptc_ui_text(m->update_check_on_startup ? PTC_UI_T_ON : PTC_UI_T_MUTED);
            int st_w = measure_text(st_txt, 13);
            int right_edge = sw_rect.x - st_w - 10;
            fit_text(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_AUTO_CHECK_LABEL), 16, right_edge - (box.x + 20) - 8);
            draw_text_bold(pixels, stride, box.x + 20, box.y + 37, text, 16,
                !enabled ? UI_DISABLED : (selected ? UI_ACCENT : UI_INK));
            draw_text(pixels, stride, right_edge, box.y + 36, st_txt, 13,
                !enabled ? UI_DISABLED : (m->update_check_on_startup ? UI_SUCCESS : UI_MUTED));
            draw_toggle_switch(pixels, stride, sw_rect, m->update_check_on_startup, selected, !enabled, NULL, NULL);
            draw_wrapped_text(pixels, stride, box.x + 20, box.y + 78,
                ptc_ui_text(PTC_UI_T_UPDATE_AUTO_CHECK_DESC), 13, box.width - 40, 19, 2,
                enabled ? UI_MUTED : UI_DISABLED);
        } else if (i == 3) {
            const char *ch_name = ptc_ui_text(m->update_stable_only ? PTC_UI_T_UPDATE_CHANNEL_STABLE_ONLY : PTC_UI_T_UPDATE_CHANNEL_WITH_BETA);
            int ch_w = measure_text(ch_name, 13) + 20;
            if (ch_w > 168) ch_w = 168;
            UiRect pill_rect = {box.x + box.width - ch_w - 18, box.y + 19, ch_w, 26};
            fit_text(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_CHANNEL_LABEL), 16, pill_rect.x - (box.x + 20) - 10);
            draw_text_bold(pixels, stride, box.x + 20, box.y + 37, text, 16,
                !enabled ? UI_DISABLED : (selected ? UI_ACCENT : UI_INK));
            fill_round_rect(pixels, stride, pill_rect, 6, m->update_stable_only ? UI_PAGE : UI_ACCENT_SOFT);
            draw_rect_outline(pixels, stride, pill_rect, 6, 1, m->update_stable_only ? UI_BORDER : UI_ACCENT);
            fit_text(text, sizeof(text), ch_name, 13, pill_rect.width - 12);
            draw_text_center(pixels, stride, pill_rect, text, 13,
                !enabled ? UI_DISABLED : (m->update_stable_only ? UI_MUTED : UI_ACCENT));
            draw_wrapped_text(pixels, stride, box.x + 20, box.y + 78,
                ptc_ui_text(PTC_UI_T_UPDATE_CHANNEL_DESC), 13, box.width - 40, 19, 2,
                enabled ? UI_MUTED : UI_DISABLED);
        } else {
            const char *badge_text = ptc_ui_text(PTC_UI_T_UPDATE_VIEW_BACKUPS_BADGE);
            int bw = measure_text(badge_text, 13) + 18;
            if (bw > 168) bw = 168;
            UiRect b_rect = {box.x + box.width - bw - 18, box.y + 19, bw, 26};
            fit_text(text, sizeof(text), ptc_ui_text(PTC_UI_T_UPDATE_BACKUPS), 16, b_rect.x - (box.x + 20) - 10);
            draw_text_bold(pixels, stride, box.x + 20, box.y + 37, text, 16,
                !enabled ? UI_DISABLED : (selected ? UI_ACCENT : UI_INK));
            fill_round_rect(pixels, stride, b_rect, 6, selected && enabled ? UI_ACCENT : UI_PAGE);
            draw_rect_outline(pixels, stride, b_rect, 6, 1, selected && enabled ? UI_ACCENT : UI_BORDER);
            fit_text(text, sizeof(text), badge_text, 13, b_rect.width - 10);
            draw_text_center(pixels, stride, b_rect, text, 13,
                !enabled ? UI_DISABLED : (selected ? UI_ON_ACCENT : UI_MUTED));
            draw_wrapped_text(pixels, stride, box.x + 20, box.y + 78,
                ptc_ui_text(PTC_UI_T_UPDATE_BACKUPS_DESC), 13, box.width - 40, 19, 2,
                enabled ? UI_MUTED : UI_DISABLED);
        }
    }

    /* Footer right: Action 6 (Cancel download / Recover interrupted install) */
    {
        PtcUiRect rect = ptc_ui_update_action_rect(6);
        UiRect box = to_uirect(rect);
        bool selected = (m->overlay_selection == 6);
        bool enabled = ptc_ui_update_action_enabled(m, 6);
        const char *label = ptc_ui_text(m->update_status == PTC_UPDATE_RECOVERY_REQUIRED ?
            PTC_UI_T_UPDATE_RECOVER : PTC_UI_T_UPDATE_CANCEL);
        int sz = 16;
        while (sz > 12 && measure_text(label, sz) > box.width - 20) --sz;
        fit_text(text, sizeof(text), label, sz, box.width - 20);
        fill_round_rect(pixels, stride, box, 12, enabled ? UI_DANGER_SOFT : UI_RAISED);
        if (selected && enabled) draw_focus_ring(pixels, stride, box, 12);
        else if (selected) draw_rect_outline(pixels, stride, box, 12, 2, UI_ACCENT);
        else draw_rect_outline(pixels, stride, box, 12, enabled ? 2 : 1, enabled ? UI_DANGER : UI_BORDER);
        draw_text_center(pixels, stride, box, text, sz, enabled ? UI_DANGER : UI_DISABLED);
    }
}

static void draw_shortcut_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    char status[192];
    char shortcut_hint[160];
    char fitted[160];
    int index;
    int active_prefix = !model->shortcut_draft_enabled ? 0 : (model->setup_shortcut_index < 7 ? 1 : 2);

    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_PARENT_SHORTCUT),
                             ptc_ui_text(PTC_UI_T_PARENT_AREA_SHORTCUT_KEY_MANAGEMENT));

    /* Left Workbench: Step 1 Prefix Mode + Step 2 Combination Matrix */
    UiRect left_card = {54, 172, 738, 466};
    draw_plan_card(pixels, stride, left_card, false);
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SHORTCUT_STEP_PREFIX), 14, left_card.width - 36);
    draw_text_bold(pixels, stride, left_card.x + 16, left_card.y + 26, fitted, 14, UI_MUTED);

    const char *prefix_labels[3] = {
        ptc_ui_text(PTC_UI_T_SHORTCUT_PREFIX_OFF),
        "L + R",
        "ZL + ZR"
    };
    for (int p = 0; p < 3; ++p) {
        UiRect pr = to_uirect(ptc_ui_shortcut_prefix_rect(p));
        bool active = (p == active_prefix);
        uint32_t pbg = active ? (p == 0 ? UI_DANGER_SOFT : UI_ACCENT) : UI_RAISED;
        uint32_t pbd = active ? (p == 0 ? UI_DANGER : UI_ACCENT) : UI_BORDER;
        uint32_t pfg = active ? (p == 0 ? UI_DANGER : UI_ON_ACCENT) : UI_INK;
        fill_round_rect(pixels, stride, pr, 10, pbg);
        draw_rect_outline(pixels, stride, pr, 10, active ? 2 : 1, pbd);
        fit_text(fitted, sizeof(fitted), prefix_labels[p], 16, pr.width - 16);
        draw_text_center(pixels, stride, pr, fitted, 16, pfg);
    }

    draw_line(pixels, stride, left_card.x + 16, left_card.y + 98,
              left_card.x + left_card.width - 16, left_card.y + 98, 1, UI_BORDER);
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SHORTCUT_STEP_COMBO), 14, left_card.width - 36);
    draw_text_bold(pixels, stride, left_card.x + 16, left_card.y + 122, fitted, 14, UI_MUTED);

    for (index = 0; index < PTC_UI_SHORTCUT_PRESET_COUNT; ++index) {
        UiRect option = to_uirect(ptc_ui_shortcut_option_rect(index));
        bool selected = (index == model->setup_shortcut_index);
        bool chosen = model->shortcut_draft_enabled && selected;
        bool same_group = model->shortcut_draft_enabled &&
            ((active_prefix == 1 && index < 7) || (active_prefix == 2 && index >= 7));
        fill_round_rect(pixels, stride, option, 9,
                        chosen ? UI_ACCENT_SOFT : (selected ? UI_PAGE : UI_RAISED));
        draw_rect_outline(pixels, stride, option, 9, chosen ? 2 : 1,
                          chosen ? UI_ACCENT : (selected ? UI_CONTROL : UI_BORDER));
        int badge_w = 0;
        if (chosen) {
            const char *save_text = ptc_ui_text(PTC_UI_T_PENDING_SAVE);
            badge_w = measure_text(save_text, 12) + 14;
            UiRect bp = {option.x + option.width - badge_w - 10, option.y + 7, badge_w, 24};
            fill_round_rect(pixels, stride, bp, 6, UI_SUCCESS_SOFT);
            draw_text_center(pixels, stride, bp, save_text, 12, UI_SUCCESS);
        }
        fit_text(fitted, sizeof(fitted), ptc_ui_shortcut_common_label(index), 15,
                 option.width - 28 - (badge_w ? badge_w + 8 : 0));
        draw_text(pixels, stride, option.x + 14, option.y + 25, fitted, 15,
                  chosen ? UI_ACCENT : (same_group ? UI_INK : UI_MUTED));
    }

    /* Right Column: Live Preview + Saved vs Draft Diff + Child Hint Card + Fixed Entry Note */
    {
        UiRect prev = {810, 172, 416, 222};
        draw_plan_card(pixels, stride, prev, false);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SHORTCUT_PREVIEW_TITLE), 15, prev.width - 36);
        draw_text_bold(pixels, stride, prev.x + 18, prev.y + 30, fitted, 15, UI_INK);

        UiRect combo_box = {prev.x + 18, prev.y + 46, prev.width - 36, 62};
        const char *active_label = model->shortcut_draft_enabled
            ? (model->shortcut_draft_label[0] ? model->shortcut_draft_label : ptc_ui_shortcut_common_label(model->setup_shortcut_index))
            : ptc_ui_text(PTC_UI_T_SHORTCUT_PREFIX_OFF);
        fill_round_rect(pixels, stride, combo_box, 12,
                        model->shortcut_draft_enabled ? UI_ACCENT_SOFT : UI_DANGER_SOFT);
        draw_rect_outline(pixels, stride, combo_box, 12, 2,
                          model->shortcut_draft_enabled ? UI_ACCENT : UI_DANGER_BORDER);
        fit_text(fitted, sizeof(fitted), active_label, 22, combo_box.width - 24);
        draw_text_center(pixels, stride, combo_box, fitted, 22,
                         model->shortcut_draft_enabled ? UI_ACCENT : UI_DANGER);

        ptc_ui_format_custom_shortcut_hint(active_label, shortcut_hint, sizeof(shortcut_hint));
        snprintf(status, sizeof(status), "%s",
                 model->shortcut_draft_enabled ? shortcut_hint : ptc_ui_text(PTC_UI_T_THE_CUSTOM_ENTRANCE_HAS_BEEN_CLOSED_FIXED));
        draw_wrapped_text(pixels, stride, prev.x + 18, prev.y + 130, status, 13, prev.width - 36, 18, 2,
                          model->shortcut_draft_enabled ? UI_INK : UI_MUTED);

        /* Saved vs Draft comparison */
        const char *saved_str = (model->custom_shortcut_enabled && model->custom_shortcut_label[0])
            ? model->custom_shortcut_label : ptc_ui_text(PTC_UI_T_SHORTCUT_PREFIX_OFF);
        snprintf(status, sizeof(status), ptc_ui_text(PTC_UI_T_SHORTCUT_SAVED_TAG), saved_str);
        fit_text(fitted, sizeof(fitted), status, 12, prev.width - 36);
        draw_text(pixels, stride, prev.x + 18, prev.y + 182, fitted, 12, UI_MUTED);

        snprintf(status, sizeof(status), ptc_ui_text(PTC_UI_T_SHORTCUT_DRAFT_TAG), active_label);
        fit_text(fitted, sizeof(fitted), status, 12, prev.width - 36);
        bool dirty = (model->shortcut_draft_enabled != model->custom_shortcut_enabled) ||
                     (model->shortcut_draft_enabled && strcmp(active_label, saved_str) != 0);
        draw_text(pixels, stride, prev.x + 18, prev.y + 204, fitted, 12,
                  dirty ? UI_WARNING_TEXT : UI_SUCCESS);
    }

    /* Child-area shortcut hint toggle card */
    {
        UiRect hint_card = to_uirect(ptc_ui_shortcut_hint_rect());
        draw_plan_card(pixels, stride, hint_card, false);
        UiRect sw = {hint_card.x + hint_card.width - 62, hint_card.y + 20, 44, 24};
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SHORTCUT_HINT_CARD_TITLE), 16,
                 sw.x - (hint_card.x + 18) - 10);
        draw_text_bold(pixels, stride, hint_card.x + 18, hint_card.y + 38, fitted, 16, UI_INK);
        draw_toggle_switch(pixels, stride, sw, model->shortcut_draft_show_hint, false, false, NULL, NULL);
        draw_wrapped_text(pixels, stride, hint_card.x + 18, hint_card.y + 68,
                          model->shortcut_draft_show_hint
                              ? ptc_ui_text(PTC_UI_T_Y_CHILD_AREA_PROMPT_DISPLAY)
                              : ptc_ui_text(PTC_UI_T_Y_CHILD_AREA_PROMPT_HIDE),
                          13, hint_card.width - 36, 18, 2, UI_ACCENT);
    }

    /* Fixed entry guarantee note */
    {
        UiRect fixed_card = {810, 516, 416, 122};
        fill_round_rect(pixels, stride, fixed_card, 14, UI_PAGE);
        draw_rect_outline(pixels, stride, fixed_card, 14, 1, UI_BORDER);
        fill_round_rect(pixels, stride, (UiRect){fixed_card.x + 14, fixed_card.y + 18, 4, fixed_card.height - 36}, 2, UI_ACCENT);
        draw_wrapped_text(pixels, stride, fixed_card.x + 28, fixed_card.y + 34,
                          ptc_ui_text(PTC_UI_T_SHORTCUT_FIXED_NOTE), 14,
                          fixed_card.width - 44, 22, 4, UI_MUTED);
    }

    home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                ptc_ui_text(PTC_UI_T_CONFIRM_TO_SAVE), true, false, model->waiting);
}

static void draw_software_info_qr(uint32_t *pixels, uint32_t stride, UiRect canvas, const char *url)
{
    static char cached_url[512] = "";
    static uint8_t cached_qr[qrcodegen_BUFFER_LEN_MAX];
    static bool cached_ok = false;

    if (url && url[0] && strcmp(cached_url, url) != 0) {
        static uint8_t temp_buf[qrcodegen_BUFFER_LEN_MAX];
        snprintf(cached_url, sizeof(cached_url), "%.511s", url);
        cached_ok = qrcodegen_encodeText(
            url, temp_buf, cached_qr,
            qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
            qrcodegen_Mask_AUTO, true);
    }

    /* Keep pure #000000 on #FFFFFF polarity across light and dark themes. */
    fill_round_rect(pixels, stride, canvas, 6, pack_rgb(0xFFFFFF));
    draw_rect_outline(pixels, stride, canvas, 6, 1, UI_BORDER);
    if (url && url[0] && cached_ok) {
        int qr_size = qrcodegen_getSize(cached_qr);
        if (qr_size > 0) {
            int quiet = 4;
            int scale = canvas.width / (qr_size + quiet * 2);
            if (scale < 2) scale = 2;
            int total = (qr_size + quiet * 2) * scale;
            int ox = canvas.x + (canvas.width - total) / 2;
            int oy = canvas.y + (canvas.height - total) / 2;
            for (int y = 0; y < qr_size; ++y) {
                for (int x = 0; x < qr_size; ++x) {
                    if (qrcodegen_getModule(cached_qr, x, y)) {
                        fill_rect_packed(pixels, stride,
                                         (UiRect){ox + (x + quiet) * scale, oy + (y + quiet) * scale, scale, scale},
                                         pack_rgb(0x000000));
                    }
                }
            }
        }
    }
}

static void draw_software_info_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    /* Scheme A: Left version & update workbench + Right switchable QR scan card. */
    char buf[256];
    char fitted[256];
    const char *status = ptc_ui_text(PTC_UI_T_STATUS_UNKNOWN);
    uint32_t status_color = UI_MUTED;
    uint32_t status_soft = UI_RAISED;
    uint32_t status_border = UI_BORDER;
    bool pending = model->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING && !model->parent_support_only;
    bool update_available = (model->update_status == PTC_UPDATE_AVAILABLE && model->update_version[0]);

    if (model->hot_reload_status == PTC_UI_HOT_RELOAD_CURRENT ||
        model->hot_reload_status == PTC_UI_HOT_RELOAD_SUCCESS) {
        status = ptc_ui_text(PTC_UI_T_LOADED);
        status_color = UI_SUCCESS;
        status_soft = UI_SUCCESS_SOFT;
        status_border = UI_SUCCESS_BORDER;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_PENDING) {
        status = ptc_ui_text(PTC_UI_T_TO_BE_LOADED);
        status_color = UI_WARNING_TEXT;
        status_soft = UI_WARNING_SOFT;
        status_border = UI_WARNING_BORDER;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_INCOMPLETE) {
        status = ptc_ui_text(PTC_UI_T_INCOMPLETE_INSTALLATION);
        status_color = UI_DANGER;
        status_soft = UI_DANGER_SOFT;
        status_border = UI_DANGER_BORDER;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_RECOVERY_REQUIRED) {
        status = ptc_ui_text(PTC_UI_T_REQUIRES_RECOVERY);
        status_color = UI_DANGER;
        status_soft = UI_DANGER_SOFT;
        status_border = UI_DANGER_BORDER;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_RUNNING) {
        status = ptc_ui_text(PTC_UI_T_IS_LOADING);
        status_color = UI_WARNING_TEXT;
        status_soft = UI_WARNING_SOFT;
        status_border = UI_WARNING_BORDER;
    } else if (model->hot_reload_status == PTC_UI_HOT_RELOAD_UNAVAILABLE) {
        status = ptc_ui_text(PTC_UI_T_THE_NEW_VERSION_CANNOT_BE_LOADED_YET);
        status_color = UI_WARNING_TEXT;
        status_soft = UI_WARNING_SOFT;
        status_border = UI_WARNING_BORDER;
    }

    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_SOFTWARE_INFO),
                             ptc_ui_text(PTC_UI_T_SW_INFO_SUBTITLE));

    /* --- Left top card: Version hero + 3 component identity chips --- */
    UiRect top_card = {54, 168, 730, 232};
    draw_plan_card(pixels, stride, top_card, false);

    snprintf(buf, sizeof(buf), "PlayWise  %s", model->software_version[0] ? model->software_version : "-");
    if (update_available) {
        snprintf(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SW_INFO_NEW_VERSION), model->update_version);
    } else {
        snprintf(fitted, sizeof(fitted), "%s", ptc_ui_text(PTC_UI_T_SW_INFO_UP_TO_DATE));
    }
    int pill_w = measure_text(fitted, 13) + 26;
    if (pill_w > 240) pill_w = 240;
    UiRect ver_pill = {top_card.x + top_card.width - 22 - pill_w, top_card.y + 18, pill_w, 30};
    fill_round_rect(pixels, stride, ver_pill, 15, update_available ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
    draw_rect_outline(pixels, stride, ver_pill, 15, 1, update_available ? UI_WARNING_BORDER : UI_SUCCESS_BORDER);
    {
        char pill_fit[128];
        fit_text(pill_fit, sizeof(pill_fit), fitted, 13, ver_pill.width - 14);
        draw_text_center(pixels, stride, ver_pill, pill_fit, 13,
                         update_available ? UI_WARNING_TEXT : UI_SUCCESS);
    }
    fit_text(fitted, sizeof(fitted), buf, 26, ver_pill.x - (top_card.x + 24) - 14);
    draw_text_bold(pixels, stride, top_card.x + 24, top_card.y + 42, fitted, 26, UI_INK);

    /* 3 Component chips: Host App, Background Service, Update Channel */
    int chip_gap = 12;
    int chip_w = (top_card.width - 48 - chip_gap * 2) / 3;
    const char *chip_labels[3] = {
        ptc_ui_text(PTC_UI_T_HOST_APPLICATION),
        ptc_ui_text(PTC_UI_T_CURRENT_BACKGROUND),
        ptc_ui_text(PTC_UI_T_SW_INFO_CHANNEL_LABEL)
    };
    const char *chip_values[3] = {
        model->app_release_id[0] ? model->app_release_id : ptc_ui_text(PTC_UI_T_UNKNOWN_IDENTITY),
        model->backend_release_id[0] ? model->backend_release_id : ptc_ui_text(PTC_UI_T_UNABLE_TO_TRUSTWORTHY_CONFIRMATION),
        ptc_ui_text(model->update_stable_only ? PTC_UI_T_SW_INFO_CHANNEL_STABLE : PTC_UI_T_SW_INFO_CHANNEL_BETA)
    };
    uint32_t chip_colors[3] = {UI_ACCENT, status_color, UI_INK};
    for (int i = 0; i < 3; ++i) {
        UiRect chip = {top_card.x + 24 + i * (chip_w + chip_gap), top_card.y + 68, chip_w, 84};
        fill_round_rect(pixels, stride, chip, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, chip, 10, 1, UI_BORDER);
        fill_round_rect(pixels, stride, (UiRect){chip.x + 12, chip.y + 16, 8, 8}, 4, chip_colors[i]);
        fit_text(fitted, sizeof(fitted), chip_labels[i], 12, chip.width - 36);
        draw_text(pixels, stride, chip.x + 26, chip.y + 24, fitted, 12, UI_MUTED);
        draw_wrapped_text(pixels, stride, chip.x + 12, chip.y + 48,
                          chip_values[i], 14, chip.width - 24, 18, 2, chip_colors[i]);
    }

    /* Integrity note at bottom of top card */
    draw_wrapped_text(pixels, stride, top_card.x + 24, top_card.y + 174,
                      ptc_ui_text(PTC_UI_T_SW_INFO_INTEGRITY_NOTE), 13,
                      top_card.width - 48, 18, 2, UI_MUTED);

    /* --- Left middle card: Prominent online update action banner --- */
    UiRect banner = to_uirect(ptc_ui_software_info_update_banner_rect());
    fill_round_rect(pixels, stride, banner, 14, update_available ? UI_ACCENT_SOFT : UI_RAISED);
    draw_rect_outline(pixels, stride, banner, 14, update_available ? 2 : 1,
                      update_available ? UI_ACCENT : UI_BORDER);
    fill_round_rect(pixels, stride, (UiRect){banner.x + 12, banner.y + 18, 5, banner.height - 36},
                    3, update_available ? UI_ACCENT : UI_SUCCESS);

    const char *btn_lbl = ptc_ui_text(update_available ? PTC_UI_T_UPDATE_NOW_ENTRY : PTC_UI_T_UPDATE_ENTRY);
    int cta_w = measure_text(btn_lbl, 15) + 34;
    if (cta_w < 146) cta_w = 146;
    if (cta_w > 220) cta_w = 220;
    UiRect cta = {banner.x + banner.width - 22 - cta_w, banner.y + (banner.height - 42) / 2, cta_w, 42};
    fill_round_rect(pixels, stride, cta, 10, update_available ? UI_ACCENT : UI_SURFACE);
    draw_rect_outline(pixels, stride, cta, 10, 1, update_available ? UI_ACCENT : UI_CONTROL);
    fit_text(fitted, sizeof(fitted), btn_lbl, 15, cta.width - 16);
    draw_text_center(pixels, stride, cta, fitted, 15, update_available ? UI_ON_ACCENT : UI_INK);

    int banner_text_w = cta.x - (banner.x + 30) - 16;
    if (update_available) {
        snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_SW_INFO_BANNER_AVAIL), model->update_version);
    } else {
        snprintf(buf, sizeof(buf), "%s", ptc_ui_text(PTC_UI_T_SW_INFO_BANNER_IDLE));
    }
    fit_text(fitted, sizeof(fitted), buf, 20, banner_text_w);
    draw_text_bold(pixels, stride, banner.x + 30, banner.y + 42, fitted, 20,
                   update_available ? UI_ACCENT : UI_INK);
    draw_wrapped_text(pixels, stride, banner.x + 30, banner.y + 68,
                      ptc_ui_text(PTC_UI_T_SW_INFO_BANNER_DESC), 14,
                      banner_text_w, 19, 2, UI_MUTED);

    /* --- Left bottom card: Hot-reload & runtime status strip --- */
    UiRect reload_card = {54, 532, 730, 108};
    draw_plan_card(pixels, stride, reload_card, false);
    draw_text_bold(pixels, stride, reload_card.x + 24, reload_card.y + 34,
                   ptc_ui_text(PTC_UI_T_LOADING_STATUS), 16, UI_INK);
    int st_w = measure_text(status, 13) + 24;
    if (st_w > 260) st_w = 260;
    UiRect st_pill = {reload_card.x + reload_card.width - 24 - st_w, reload_card.y + 16, st_w, 28};
    fill_round_rect(pixels, stride, st_pill, 14, status_soft);
    draw_rect_outline(pixels, stride, st_pill, 14, 1, status_border);
    fit_text(fitted, sizeof(fitted), status, 13, st_pill.width - 12);
    draw_text_center(pixels, stride, st_pill, fitted, 13, status_color);
    draw_wrapped_text(pixels, stride, reload_card.x + 24, reload_card.y + 62,
                      model->hot_reload_detail[0] ? model->hot_reload_detail : ptc_ui_text(PTC_UI_T_THE_CURRENT_INSTALLED_VERSION_HAS_BEEN_LOADED),
                      14, reload_card.width - 48, 20, 2, UI_MUTED);

    /* --- Right column: Switchable QR scan card --- */
    UiRect qr_card = {802, 168, 424, 472};
    draw_plan_card(pixels, stride, qr_card, false);
    int qr_tab = model->software_info_qr_tab ? 1 : 0;

    draw_text_bold(pixels, stride, qr_card.x + 24, qr_card.y + 36,
                   ptc_ui_text(PTC_UI_T_SW_INFO_QR_TITLE), 18, UI_INK);
    const char *sw_hint = ptc_ui_text(PTC_UI_T_SW_INFO_QR_SWITCH_HINT);
    int sw_w = measure_text(sw_hint, 12);
    draw_text(pixels, stride, qr_card.x + qr_card.width - 24 - sw_w, qr_card.y + 35, sw_hint, 12, UI_MUTED);

    /* Segmented 2-tab switch */
    UiRect tab_track = {826, 224, 376, 36};
    fill_round_rect(pixels, stride, tab_track, 10, UI_PAGE);
    draw_rect_outline(pixels, stride, tab_track, 10, 1, UI_BORDER);
    for (int t = 0; t < 2; ++t) {
        UiRect tab = to_uirect(ptc_ui_software_info_qr_tab_rect(t));
        bool active = (qr_tab == t);
        if (active) {
            fill_round_rect(pixels, stride, (UiRect){tab.x + 2, tab.y + 2, tab.width - 4, tab.height - 4},
                            8, UI_ACCENT);
        }
        fit_text(fitted, sizeof(fitted),
                 ptc_ui_text(t == 0 ? PTC_UI_T_SW_INFO_QR_TAB_PWA : PTC_UI_T_SW_INFO_QR_TAB_REPO),
                 14, tab.width - 12);
        draw_text_center(pixels, stride, tab, fitted, 14, active ? UI_ON_ACCENT : UI_MUTED);
    }

    int repo_source = model->software_info_repo_source ? 1 : 0;
    const char *repo_url = repo_source
        ? (model->gitee_repository_url[0] ? model->gitee_repository_url : "https://gitee.com/selfuppen/nx-playwise")
        : (model->repository_url[0] ? model->repository_url : "https://github.com/selfuppen/NX-PlayWise");
    const char *active_url = (qr_tab == 0) ? model->pwa_url : repo_url;
    if (qr_tab == 0) {
        UiRect qr_canvas = {qr_card.x + (qr_card.width - 216) / 2, 276, 216, 216};
        draw_software_info_qr(pixels, stride, qr_canvas, active_url);
    } else {
        UiRect qr_canvas = {qr_card.x + (qr_card.width - 188) / 2, 268, 188, 188};
        draw_software_info_qr(pixels, stride, qr_canvas, active_url);

        UiRect src_track = {826, 466, 250, 30};
        fill_round_rect(pixels, stride, src_track, 8, UI_PAGE);
        draw_rect_outline(pixels, stride, src_track, 8, 1, UI_BORDER);
        static const char *SRC_LABELS[2] = {"GitHub", "Gitee"};
        for (int s = 0; s < 2; ++s) {
            UiRect btn = to_uirect(ptc_ui_software_info_repo_source_rect(s));
            bool active = (repo_source == s);
            if (active) {
                fill_round_rect(pixels, stride, (UiRect){btn.x + 2, btn.y + 2, btn.width - 4, btn.height - 4},
                                6, UI_ACCENT);
            }
            draw_text_center(pixels, stride, btn, SRC_LABELS[s], 13, active ? UI_ON_ACCENT : UI_MUTED);
        }
        const char *repo_hint = ptc_ui_text(PTC_UI_T_SW_INFO_REPO_SWITCH_HINT);
        int repo_hint_w = measure_text(repo_hint, 12);
        draw_text(pixels, stride, qr_card.x + qr_card.width - 24 - repo_hint_w, 486, repo_hint, 12, UI_MUTED);
    }

    draw_wrapped_text_centered(pixels, stride, qr_card.x + qr_card.width / 2, qr_tab == 0 ? 514 : 518,
                               ptc_ui_text(qr_tab == 0 ? PTC_UI_T_SW_INFO_QR_PWA_DESC : PTC_UI_T_SW_INFO_QR_REPO_DESC),
                               14, qr_card.width - 40, 19, 2, UI_INK);

    UiRect url_box = {qr_card.x + 22, 558, qr_card.width - 44, 62};
    fill_round_rect(pixels, stride, url_box, 10, UI_PAGE);
    draw_rect_outline(pixels, stride, url_box, 10, 1, UI_BORDER);
    draw_wrapped_text(pixels, stride, url_box.x + 12, url_box.y + 22,
                      active_url[0] ? active_url : "-", 13, url_box.width - 24, 18, 2,
                      qr_tab == 0 ? UI_SUCCESS : UI_ACCENT);

    /* Footer actions */
    home_button(pixels, stride, ptc_ui_settings_page_footer_rect(2),
                ptc_ui_text(update_available ? PTC_UI_T_UPDATE_NOW_ENTRY : PTC_UI_T_UPDATE_ENTRY),
                update_available, false, false);
    if (pending) {
        home_button(pixels, stride, ptc_ui_settings_page_footer_rect(1), ptc_ui_text(PTC_UI_T_B_NOT_YET),
                    false, false, false);
        home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                    ptc_ui_text(model->hot_reload_capture_restart
                        ? PTC_UI_T_CAPTURE_RESTART_BUTTON : PTC_UI_T_A_LOAD_NEW_VERSION),
                    true, false, false);
    } else {
        home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                    ptc_ui_text(PTC_UI_T_A_CLOSE), true, false, false);
    }
}

static void draw_album_flow_diagram(uint32_t *pixels, uint32_t stride, UiRect box, bool protected_mode)
{
    char fitted[64];
    fill_round_rect(pixels, stride, box, 12, UI_PAGE);
    draw_rect_outline(pixels, stride, box, 12, 1, UI_BORDER);
    if (protected_mode) {
        /* Row 1: Album -> Normal Album (blocked from hbmenu) */
        UiRect a1 = {box.x + 18, box.y + 18, 118, 36};
        UiRect a2 = {box.x + 184, box.y + 18, 152, 36};
        fill_round_rect(pixels, stride, a1, 8, UI_SURFACE);
        draw_rect_outline(pixels, stride, a1, 8, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ALBUM_DIAG_ALBUM), 14, a1.width - 12);
        draw_text_center(pixels, stride, a1, fitted, 14, UI_INK);
        draw_transition_arrow(pixels, stride, box.x + 160, a1.y + 18, UI_MUTED);
        fill_round_rect(pixels, stride, a2, 8, UI_RAISED);
        draw_rect_outline(pixels, stride, a2, 8, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ALBUM_DIAG_NORMAL), 13, a2.width - 12);
        draw_text_center(pixels, stride, a2, fitted, 13, UI_MUTED);

        /* Row 2: Controllers + Hold X+A -> hbmenu */
        UiRect b1 = {box.x + 18, box.y + 68, 118, 38};
        UiRect b2 = {box.x + 148, box.y + 68, 128, 38};
        UiRect b3 = {box.x + 324, box.y + 68, 140, 38};
        fill_round_rect(pixels, stride, b1, 8, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, b1, 8, 1, UI_ACCENT);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ALBUM_DIAG_CTRL), 14, b1.width - 10);
        draw_text_center(pixels, stride, b1, fitted, 14, UI_ACCENT);
        fill_round_rect(pixels, stride, b2, 8, UI_ACCENT);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ALBUM_DIAG_HOLD_XA), 13, b2.width - 10);
        draw_text_center(pixels, stride, b2, fitted, 13, UI_ON_ACCENT);
        draw_transition_arrow(pixels, stride, box.x + 300, b1.y + 19, UI_SUCCESS);
        fill_round_rect(pixels, stride, b3, 8, UI_SUCCESS_SOFT);
        draw_rect_outline(pixels, stride, b3, 8, 1, UI_SUCCESS_BORDER);
        draw_text_center(pixels, stride, b3, "hbmenu", 15, UI_SUCCESS);
    } else {
        /* Centered original path: Album -> hbmenu */
        UiRect r1 = {box.x + 76, box.y + 40, 146, 44};
        UiRect r2 = {box.x + 286, box.y + 40, 166, 44};
        fill_round_rect(pixels, stride, r1, 10, UI_SURFACE);
        draw_rect_outline(pixels, stride, r1, 10, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_ALBUM_DIAG_ALBUM), 16, r1.width - 12);
        draw_text_center(pixels, stride, r1, fitted, 16, UI_INK);
        draw_transition_arrow(pixels, stride, box.x + 254, r1.y + 22, UI_ACCENT);
        fill_round_rect(pixels, stride, r2, 10, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, r2, 10, 1, UI_ACCENT);
        draw_text_center(pixels, stride, r2, "hbmenu", 16, UI_ACCENT);
    }
}

static void draw_album_manager_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *state = model->album_restriction_state == 0 ? ptc_ui_text(PTC_UI_T_NOT_ENABLED) :
                        model->album_restriction_state == 1 ? ptc_ui_text(PTC_UI_T_ENABLED) :
                        model->album_restriction_state == 2 ? ptc_ui_text(PTC_UI_T_NEEDS_TO_BE_PROCESSED_2) :
                        model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? ptc_ui_text(PTC_UI_T_EXTERNAL_CONFIGURED) : ptc_ui_text(PTC_UI_T_STATUS_UNKNOWN);
    const char *status_lbl = ptc_ui_text(PTC_UI_T_CURRENT_STATUS);
    char fitted[160];
    int status_lbl_w;
    int state_x;
    uint32_t st_color = model->album_restriction_state == 1 ? UI_SUCCESS :
                        model->album_restriction_state == 2 ? UI_WARNING_TEXT :
                        model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? UI_ACCENT : UI_MUTED;
    uint32_t st_bg = model->album_restriction_state == 1 ? UI_SUCCESS_SOFT :
                     model->album_restriction_state == 2 ? UI_WARNING_SOFT :
                     model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? UI_ACCENT_SOFT : UI_PAGE;
    uint32_t st_bd = model->album_restriction_state == 1 ? UI_SUCCESS_BORDER :
                     model->album_restriction_state == 2 ? UI_WARNING_BORDER :
                     model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL ? UI_ACCENT : UI_BORDER;

    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_HOMEBREW_ACCESS),
                             ptc_ui_text(PTC_UI_T_THIS_FUNCTION_ONLY_CHANGES_THE_HBMENU_STARTUP));

    /* Top Semantic Status Banner + Y Retest */
    {
        UiRect bar = {54, 174, 1172, 68};
        fill_round_rect(pixels, stride, bar, 14, st_bg);
        draw_rect_outline(pixels, stride, bar, 14, 1, st_bd);
        fill_round_rect(pixels, stride, (UiRect){bar.x + 18, bar.y + 28, 12, 12}, 6,
                        model->album_restriction_state == 2 ? UI_WARNING : st_color);
        status_lbl_w = measure_text(status_lbl, 16);
        draw_text(pixels, stride, bar.x + 38, bar.y + 40, status_lbl, 16, UI_MUTED);
        state_x = bar.x + 38 + status_lbl_w + 14;
        int sw = measure_text(state, 16) + 24;
        if (sw > 260) sw = 260;
        UiRect sp = {state_x, bar.y + 18, sw, 32};
        fill_round_rect(pixels, stride, sp, 16, UI_SURFACE);
        draw_rect_outline(pixels, stride, sp, 16, 1, st_bd);
        fit_text(fitted, sizeof(fitted), state, 16, sp.width - 14);
        draw_text_center(pixels, stride, sp, fitted, 16, st_color);
        home_button(pixels, stride, ptc_ui_album_refresh_rect(), ptc_ui_text(PTC_UI_T_Y_RETEST),
                    false, false, false);
    }

    for (int index = 0; index < 2; ++index) {
        UiRect card = to_uirect(ptc_ui_album_action_rect(index));
        bool selected = model->overlay_selection == index;
        bool enabled = index == 0 ? model->album_restriction_state == 0 :
                       (model->album_restriction_state == 1 ||
                        (model->album_restriction_state == 2 && model->album_backup_valid));
        fill_round_rect(pixels, stride, card, 16, enabled && selected ? UI_ACCENT_SOFT :
                        (enabled ? UI_SURFACE : UI_PAGE));
        draw_rect_outline(pixels, stride, card, 16, enabled && selected ? 3 : 1, enabled && selected ? UI_ACCENT : UI_BORDER);

        const char *tag = ptc_ui_text(index == 0 ? PTC_UI_T_ALBUM_DIAG_PROTECTED_TAG : PTC_UI_T_ALBUM_DIAG_RESTORE_TAG);
        int tw = measure_text(tag, 12) + 18;
        if (tw > 180) tw = 180;
        UiRect tpill = {card.x + card.width - 24 - tw, card.y + 18, tw, 24};
        fill_round_rect(pixels, stride, tpill, 12, index == 0 ? UI_SUCCESS_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, tpill, 12, 1, index == 0 ? UI_SUCCESS_BORDER : UI_BORDER);
        fit_text(fitted, sizeof(fitted), tag, 12, tpill.width - 10);
        draw_text_center(pixels, stride, tpill, fitted, 12, index == 0 ? UI_SUCCESS : UI_MUTED);

        const char *card_title = index == 0 ? (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL
                                                ? ptc_ui_text(PTC_UI_T_NO_NEED_TO_REPEAT_CONFIGURATION) : ptc_ui_text(PTC_UI_T_CONFIGURE_ADVANCED_ENTRY_OF_HOMEBREW_MENU)) :
                                (model->album_restriction_state == 2 && model->album_backup_valid ? ptc_ui_text(PTC_UI_T_FORCE_RECOVERY_OF_TRUSTED_BACKUPS) : ptc_ui_text(PTC_UI_T_RESTORE_THE_ORIGINAL_STARTUP_MODE));
        int title_max_w = tpill.x - (card.x + 24) - 10;
        fit_text(fitted, sizeof(fitted), card_title, 19, title_max_w);
        draw_text_bold(pixels, stride, card.x + 24, card.y + 38, fitted, 19, enabled ? UI_INK : UI_DISABLED);

        draw_album_flow_diagram(pixels, stride, (UiRect){card.x + 24, card.y + 56, card.width - 48, 122}, index == 0);

        const char *card_desc = index == 0
            ? (model->album_restriction_state == PTC_ALBUM_RESTRICTION_EXTERNAL
                ? ptc_ui_text(PTC_UI_T_THE_CURRENT_DISK_CONFIGURATION_ALREADY_PROVIDES_THE)
                : ptc_ui_text(PTC_UI_T_FIRST_MAKE_A_COMPLETE_BACKUP_OF_RELEVANT))
            : ptc_ui_text(PTC_UI_T_RESTORE_THE_ORIGINAL_CONFIGURATION_ACCORDING_TO_THE);
        draw_wrapped_text(pixels, stride, card.x + 24, card.y + 204, card_desc,
                          14, card.width - 48, 20, 3, enabled ? UI_MUTED : UI_DISABLED);

        {
            UiRect pill = {card.x + 24, card.y + card.height - 56, card.width - 48, 40};
            const char *pill_label = enabled ? ptc_ui_text(PTC_UI_T_A_CONTINUE)
                                             : ptc_ui_text(PTC_UI_T_CURRENT_STATUS_IS_NOT_AVAILABLE);
            fill_round_rect(pixels, stride, pill, 20, enabled ? (selected ? UI_ACCENT : UI_ACCENT_SOFT) : UI_RAISED);
            draw_rect_outline(pixels, stride, pill, 20, 1, enabled ? UI_ACCENT : UI_BORDER);
            draw_text_center(pixels, stride, pill, pill_label, 16,
                             enabled ? (selected ? UI_ON_ACCENT : UI_ACCENT) : UI_DISABLED);
        }
    }

    {
        const char *bottom_note = model->album_restriction_detail[0]
            ? model->album_restriction_detail : ptc_ui_text(PTC_UI_T_ALBUM_UNINSTALL_NOTE);
        draw_wrapped_text(pixels, stride, 54, 612, bottom_note, 14, 1172, 19, 2, UI_MUTED);
    }
}

/* 单选指示：未选为控件描边圆环，选中为强调色圆环 + 实心圆点。 */
static void draw_radio_marker(uint32_t *pixels, uint32_t stride, int cx, int cy,
                              bool selected, uint32_t accent)
{
    draw_circle_outline(pixels, stride, cx, cy, 9, selected ? 3 : 2, selected ? accent : UI_CONTROL);
    if (selected) draw_circle_outline(pixels, stride, cx, cy, 4, 4, accent);
}

static void draw_theme_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    const char *LABELS[] = {
        ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM),
        ptc_ui_text(PTC_UI_T_LIGHT),
        ptc_ui_text(PTC_UI_T_DARK)
    };
    const char *DETAILS[] = {
        ptc_ui_text(PTC_UI_T_FOLLOWS_SWITCH_SETTINGS),
        ptc_ui_text(PTC_UI_T_REFRESHING_LIGHT_BACKGROUND),
        ptc_ui_text(PTC_UI_T_SOFT_DARK_BACKGROUND)
    };
    const char *shell_hint = !g_theme.system_theme_available
        ? ptc_ui_text(PTC_UI_T_THE_SYSTEM_THEME_IS_TEMPORARILY_UNAVAILABLE_AND)
        : ptc_ui_text(PTC_UI_T_THEME_SHELL_HINT);
    /* 页面显示草稿选择；未按 A 前不写入 config.json。没有活动草稿时
     * （如宿主预览）回退到当前已保存外观。 */
    const PtcUiThemePreference draft_preference = model->theme_draft_valid
        ? model->theme_draft_preference : g_theme.preference;
    const PtcUiThemeStyle draft_style = model->theme_draft_valid
        ? model->theme_draft_style : g_theme.style;
    const PtcUiThemePreference saved_preference = model->theme_draft_valid
        ? model->theme_saved_preference : g_theme.preference;
    const PtcUiThemeStyle saved_style = model->theme_draft_valid
        ? model->theme_saved_style : g_theme.style;
    const int draft_mode = (int)draft_preference;
    const bool dirty = draft_preference != saved_preference || draft_style != saved_style;
    const PtcUiResolvedTheme preview_resolved = draft_preference == PTC_UI_THEME_LIGHT
        ? PTC_UI_RESOLVED_LIGHT
        : (draft_preference == PTC_UI_THEME_DARK ? PTC_UI_RESOLVED_DARK : g_theme.resolved);
    const PtcUiPalette *draft_pal = ptc_ui_theme_style_palette(preview_resolved, draft_style);
    const PtcUiPalette *dark_pal = ptc_ui_theme_style_palette(PTC_UI_RESOLVED_DARK, draft_style);
    char fitted[192];

    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_APPEARANCE_THEME), shell_hint);

    /* Top-right status pill on breadcrumb row: saved state vs unsaved draft diff */
    {
        char path[128];
        char summary[192];
        PtcUiTextArg pargs[] = {PTC_UI_TEXT_STRING("name", ptc_ui_text(PTC_UI_T_APPEARANCE_THEME))};
        (void)ptc_ui_text_format(PTC_UI_T_SECURITY_PREFS_S, path, sizeof(path), pargs, 1);
        int min_x = 278 + measure_text(path, 22) + 20;
        int avail_w = 1226 - min_x;
        if (avail_w > 160) {
            if (dirty) {
                PtcUiTextArg dargs[] = {
                    PTC_UI_TEXT_STRING("saved_mode", ptc_ui_theme_preference_label(saved_preference)),
                    PTC_UI_TEXT_STRING("saved_style", ptc_ui_theme_style_label(saved_style)),
                    PTC_UI_TEXT_STRING("draft_mode", ptc_ui_theme_preference_label(draft_preference)),
                    PTC_UI_TEXT_STRING("draft_style", ptc_ui_theme_style_label(draft_style))
                };
                (void)ptc_ui_text_format(PTC_UI_T_THEME_DRAFT_DIFF, summary, sizeof(summary), dargs, 4);
            } else {
                PtcUiTextArg sargs[] = {
                    PTC_UI_TEXT_STRING("mode", ptc_ui_theme_preference_label(saved_preference)),
                    PTC_UI_TEXT_STRING("style", ptc_ui_theme_style_label(saved_style))
                };
                (void)ptc_ui_text_format(PTC_UI_T_THEME_SAVED_SUMMARY, summary, sizeof(summary), sargs, 2);
            }
            fit_text(fitted, sizeof(fitted), summary, 13, avail_w - 24);
            int pw = measure_text(fitted, 13) + 24;
            if (pw > avail_w) pw = avail_w;
            UiRect pill = {1226 - pw, 124, pw, 30};
            fill_round_rect(pixels, stride, pill, 15, dirty ? UI_WARNING_SOFT : UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, pill, 15, 1, dirty ? UI_WARNING_BORDER : UI_SUCCESS_BORDER);
            draw_text_center(pixels, stride, pill, fitted, 13, dirty ? UI_WARNING_TEXT : UI_SUCCESS);
        }
    }

    /* Left Section 1: Mode Selection (3 radio cards, X=54..662, Y=202..298) */
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THEME_SECTION_MODE), 14, 608);
    draw_text_bold(pixels, stride, 56, 192, fitted, 14, UI_MUTED);
    for (int index = 0; index < 3; ++index) {
        UiRect option = to_uirect(ptc_ui_theme_option_rect(index));
        bool focused = (index == model->overlay_selection);
        bool selected = (index == draft_mode);

        if (focused) {
            fill_round_rect(pixels, stride, option, 12, UI_ACCENT_SOFT);
            draw_focus_ring(pixels, stride, option, 12);
        } else {
            draw_plan_card(pixels, stride, option, false);
            if (selected) draw_rect_outline(pixels, stride, option, 12, 2, UI_ACCENT);
        }

        draw_radio_marker(pixels, stride, option.x + 22, option.y + 26, selected, UI_ACCENT);
        fit_text(fitted, sizeof(fitted), LABELS[index], 16, option.width - 50);
        draw_text_bold(pixels, stride, option.x + 38, option.y + 32, fitted, 16,
                       selected || focused ? UI_ACCENT : UI_INK);
        draw_wrapped_text(pixels, stride, option.x + 14, option.y + 58, DETAILS[index],
                          12, option.width - 28, 17, 2, UI_MUTED);
    }

    /* Left Section 2: 2x2 Theme Style Cards (X=54..662, Y=344..636) */
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THEME_SECTION_STYLE), 14, 608);
    draw_text_bold(pixels, stride, 56, 332, fitted, 14, UI_MUTED);
    for (int i = 0; i < PTC_UI_THEME_STYLE_COUNT; ++i) {
        UiRect s_rect = to_uirect(ptc_ui_theme_style_rect(i));
        bool focused = (model->overlay_selection == 3 + i);
        bool selected = (i == (int)draft_style);
        const PtcUiPalette *spal = ptc_ui_theme_style_palette(preview_resolved, (PtcUiThemeStyle)i);
        const uint32_t card_accent = UI_RGB(spal->accent);

        if (focused) {
            fill_round_rect(pixels, stride, s_rect, 14, UI_RGB(spal->accent_soft));
            draw_focus_ring(pixels, stride, s_rect, 14);
            draw_rect_outline(pixels, stride, s_rect, 14, 2, card_accent);
        } else {
            draw_plan_card(pixels, stride, s_rect, false);
            if (selected) draw_rect_outline(pixels, stride, s_rect, 14, 2, card_accent);
        }

        draw_radio_marker(pixels, stride, s_rect.x + 22, s_rect.y + 26, selected, card_accent);

        /* Right-aligned semantic swatch trio: accent + success + night */
        int sw_x = s_rect.x + s_rect.width - 70;
        int sw_y = s_rect.y + 18;
        fill_round_rect(pixels, stride, (UiRect){sw_x, sw_y, 20, 15}, 4, UI_RGB(spal->accent));
        fill_round_rect(pixels, stride, (UiRect){sw_x + 24, sw_y, 15, 15}, 4, UI_RGB(spal->success));
        fill_round_rect(pixels, stride, (UiRect){sw_x + 43, sw_y, 15, 15}, 4, UI_RGB(spal->night));

        fit_text(fitted, sizeof(fitted), ptc_ui_theme_style_label((PtcUiThemeStyle)i), 18, sw_x - (s_rect.x + 40) - 8);
        draw_text_bold(pixels, stride, s_rect.x + 40, s_rect.y + 32, fitted, 18, UI_INK);

        fit_text(fitted, sizeof(fitted), ptc_ui_theme_style_keyword((PtcUiThemeStyle)i), 13, s_rect.width - 32);
        draw_text_bold(pixels, stride, s_rect.x + 16, s_rect.y + 66, fitted, 13, card_accent);
        draw_wrapped_text(pixels, stride, s_rect.x + 16, s_rect.y + 92, ptc_ui_theme_style_desc((PtcUiThemeStyle)i),
                          12, s_rect.width - 32, 18, 2, UI_MUTED);
    }

    /* Right Column: Live Contrast Preview Window (X=680..1226, Y=172..636) */
    {
        UiRect shell = {680, 172, 546, 464};
        draw_plan_card(pixels, stride, shell, false);

        const char *mode_badge = ptc_ui_text(preview_resolved == PTC_UI_RESOLVED_DARK
            ? PTC_UI_T_THEME_PREVIEW_BADGE_DARK : PTC_UI_T_THEME_PREVIEW_BADGE_LIGHT);
        int mbw = measure_text(mode_badge, 12) + 20;
        if (mbw > 190) mbw = 190;
        UiRect mb = {shell.x + shell.width - 18 - mbw, shell.y + 14, mbw, 26};
        fill_round_rect(pixels, stride, mb, 13, UI_RGB(draft_pal->accent_soft));
        draw_rect_outline(pixels, stride, mb, 13, 1, UI_RGB(draft_pal->accent));
        fit_text(fitted, sizeof(fitted), mode_badge, 12, mb.width - 12);
        draw_text_center(pixels, stride, mb, fitted, 12, UI_RGB(draft_pal->accent));

        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THEME_PREVIEW_TITLE), 16, mb.x - (shell.x + 18) - 10);
        draw_text_bold(pixels, stride, shell.x + 18, shell.y + 33, fitted, 16, UI_INK);

        /* Inner Live Mini-Screen rendered strictly with draft_pal */
        UiRect canvas = {shell.x + 16, shell.y + 50, shell.width - 32, shell.height - 66};
        fill_round_rect(pixels, stride, canvas, 12, UI_RGB(draft_pal->page_bg));
        draw_rect_outline(pixels, stride, canvas, 12, 1, UI_RGB(draft_pal->border_decorative));

        /* Mini header inside live preview */
        fill_round_rect(pixels, stride, (UiRect){canvas.x + 14, canvas.y + 12, 20, 20}, 6, UI_RGB(draft_pal->accent));
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THEME_PREVIEW_HEADER), 13, canvas.width - 54);
        draw_text_bold(pixels, stride, canvas.x + 42, canvas.y + 27, fitted, 13, UI_RGB(draft_pal->text_primary));
        fill_rect(pixels, stride, (UiRect){canvas.x + 14, canvas.y + 42, canvas.width - 28, 1},
                  UI_RGB(draft_pal->border_decorative));

        /* Mini Child Hero Card */
        UiRect hero = {canvas.x + 14, canvas.y + 52, canvas.width - 28, 164};
        fill_round_rect(pixels, stride, hero, 10, UI_RGB(draft_pal->surface));
        draw_rect_outline(pixels, stride, hero, 10, 1, UI_RGB(draft_pal->border_decorative));

        const char *sample_min = ptc_ui_text(PTC_UI_T_LANG_PV_SAMPLE_MIN);
        int smw = measure_text(sample_min, 22);
        draw_text_bold(pixels, stride, hero.x + hero.width - 14 - smw, hero.y + 32, sample_min, 22,
                       UI_RGB(draft_pal->success));
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THEME_PREVIEW_PLAYABLE), 13,
                 hero.width - 36 - smw);
        draw_text_bold(pixels, stride, hero.x + 14, hero.y + 29, fitted, 13, UI_RGB(draft_pal->text_secondary));

        /* Quota progress bar */
        UiRect bar = {hero.x + 14, hero.y + 46, hero.width - 28, 10};
        fill_round_rect(pixels, stride, bar, 5, UI_RGB(draft_pal->surface_raised));
        fill_round_rect(pixels, stride, (UiRect){bar.x, bar.y, (bar.width * 58) / 100, bar.height}, 5,
                        UI_RGB(draft_pal->success));

        /* 2x2 Real Semantic Status Pills for instant contrast check */
        const PtcUiTextId pill_ids[4] = {
            PTC_UI_T_THEME_PREVIEW_PILL_OK,
            PTC_UI_T_THEME_PREVIEW_PILL_WARN,
            PTC_UI_T_THEME_PREVIEW_PILL_DANGER,
            PTC_UI_T_THEME_PREVIEW_PILL_NIGHT
        };
        const uint32_t pill_bgs[4] = {
            UI_RGB(draft_pal->success_soft), UI_RGB(draft_pal->warning_soft),
            UI_RGB(draft_pal->danger_soft), UI_RGB(draft_pal->night_soft)
        };
        const uint32_t pill_bds[4] = {
            UI_RGB(draft_pal->success_border), UI_RGB(draft_pal->warning_border),
            UI_RGB(draft_pal->danger_border), UI_RGB(draft_pal->night_border)
        };
        const uint32_t pill_fgs[4] = {
            UI_RGB(draft_pal->success), UI_RGB(draft_pal->warning_text),
            UI_RGB(draft_pal->danger), UI_RGB(draft_pal->night)
        };
        int pw = (hero.width - 38) / 2;
        for (int k = 0; k < 4; ++k) {
            int col = k % 2;
            int row = k / 2;
            UiRect pr = {hero.x + 14 + col * (pw + 10), hero.y + 72 + row * 40, pw, 30};
            fill_round_rect(pixels, stride, pr, 15, pill_bgs[k]);
            draw_rect_outline(pixels, stride, pr, 15, 1, pill_bds[k]);
            fit_text(fitted, sizeof(fitted), ptc_ui_text(pill_ids[k]), 12, pr.width - 16);
            draw_text_center(pixels, stride, pr, fitted, 12, pill_fgs[k]);
        }

        /* Bottom split: Left Focus Card Sample + Right 448px Dark Overlay Preview */
        int bot_y = canvas.y + 228;
        int bot_h = canvas.y + canvas.height - bot_y - 14;
        UiRect focus_box = {canvas.x + 14, bot_y, 252, bot_h};
        fill_round_rect(pixels, stride, focus_box, 10, UI_RGB(draft_pal->accent_soft));
        draw_rect_outline(pixels, stride, focus_box, 10, 2, UI_RGB(draft_pal->accent));
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THEME_PREVIEW_FOCUS_CARD), 14, focus_box.width - 24);
        draw_text_bold(pixels, stride, focus_box.x + 12, focus_box.y + 28, fitted, 14, UI_RGB(draft_pal->accent));
        draw_wrapped_text(pixels, stride, focus_box.x + 12, focus_box.y + 52,
                          ptc_ui_text(PTC_UI_T_THEME_PREVIEW_FOCUS_SUB), 12, focus_box.width - 24, 17, 2,
                          UI_RGB(draft_pal->text_secondary));
        UiRect cta = {focus_box.x + 12, focus_box.y + focus_box.height - 46, focus_box.width - 24, 34};
        fill_round_rect(pixels, stride, cta, 8, UI_RGB(draft_pal->accent));
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THEME_PREVIEW_PRIMARY_BTN), 13, cta.width - 16);
        draw_text_center(pixels, stride, cta, fitted, 13, UI_RGB(draft_pal->on_accent));

        UiRect ovl_box = {focus_box.x + focus_box.width + 12, bot_y,
                          canvas.x + canvas.width - 14 - (focus_box.x + focus_box.width + 12), bot_h};
        fill_round_rect(pixels, stride, ovl_box, 10, UI_RGB(0x0A0D12));
        draw_rect_outline(pixels, stride, ovl_box, 10, 1, UI_RGB(dark_pal->border_decorative));
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THEME_PREVIEW_OVERLAY_TITLE), 12, ovl_box.width - 20);
        draw_text_bold(pixels, stride, ovl_box.x + 10, ovl_box.y + 24, fitted, 12, UI_RGB(dark_pal->text_secondary));

        UiRect ovl_banner = {ovl_box.x + 10, ovl_box.y + 38, ovl_box.width - 20, 44};
        fill_round_rect(pixels, stride, ovl_banner, 7, UI_RGB(dark_pal->success_soft));
        draw_rect_outline(pixels, stride, ovl_banner, 7, 1, UI_RGB(dark_pal->success_border));
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_THEME_PREVIEW_OVERLAY_OK), 13, ovl_banner.width - 12);
        draw_text_center(pixels, stride, ovl_banner, fitted, 13, UI_RGB(dark_pal->success));

        fill_round_rect(pixels, stride, (UiRect){ovl_box.x + 12, ovl_box.y + 102, 8, 8}, 4, UI_RGB(dark_pal->accent));
        draw_wrapped_text(pixels, stride, ovl_box.x + 26, ovl_box.y + 110,
                          ptc_ui_text(PTC_UI_T_THEME_PREVIEW_OVERLAY_RULE), 11, ovl_box.width - 36, 16, 2,
                          UI_RGB(dark_pal->text_primary));
    }

    home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                ptc_ui_text(PTC_UI_T_A_CONFIRM_SAVE), true, false, model->waiting);
}

static void draw_language_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    /* Scheme B: 4 full-width vertical language cards side-by-side. */
    static const char *CODES[4] = {"AUTO", "ZH-CN", "ZH-TW", "EN"};
    const char *LABELS[4] = {
        ptc_ui_text(PTC_UI_T_FOLLOW_SYSTEM),
        ptc_ui_text_in_language(PTC_UI_T_SIMPLIFIED_CHINESE, PTC_UI_LANGUAGE_SIMPLIFIED),
        ptc_ui_text_in_language(PTC_UI_T_TRADITIONAL_CHINESE, PTC_UI_LANGUAGE_TRADITIONAL),
        "English"
    };
    const char *DETAILS[4] = {
        ptc_ui_text(PTC_UI_T_USE_SWITCH_SYSTEM_LANGUAGE),
        ptc_ui_text(PTC_UI_T_ALWAYS_SHOW_SIMPLIFIED_CHINESE),
        ptc_ui_text(PTC_UI_T_ALWAYS_SHOW_TRADITIONAL_CHINESE),
        ptc_ui_text(PTC_UI_T_LANG_ALWAYS_ENGLISH)
    };
    PtcUiLanguagePreference resolved_now = ptc_ui_language_get_resolved();
    char fitted[128];

    draw_settings_page_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_UI_LANGUAGE),
                             ptc_ui_text(PTC_UI_T_LANG_SUBTITLE_B));

    for (int index = 0; index < 4; ++index) {
        UiRect option = to_uirect(ptc_ui_language_option_rect(index));
        bool focused = (index == model->overlay_selection);
        bool is_current = (index == (int)model->language_preference);
        PtcUiLanguagePreference sample_lang = (index == 0) ? resolved_now : (PtcUiLanguagePreference)index;

        if (focused) {
            UiRect halo = {option.x - 3, option.y - 3, option.width + 6, option.height + 6};
            draw_rect_outline(pixels, stride, halo, 17, 1, UI_FOCUS);
        }
        fill_round_rect(pixels, stride, option, 14, focused ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, option, 14, focused ? 3 : 1,
                          focused ? UI_ACCENT : (is_current ? UI_SUCCESS_BORDER : UI_BORDER));

        /* Top row: language code chip + active badge if currently saved */
        UiRect code_pill = {option.x + 16, option.y + 16, 68, 24};
        fill_round_rect(pixels, stride, code_pill, 12, focused ? UI_ACCENT : UI_PAGE);
        draw_rect_outline(pixels, stride, code_pill, 12, 1, focused ? UI_ACCENT : UI_BORDER);
        draw_text_center(pixels, stride, code_pill, CODES[index], 12, focused ? UI_ON_ACCENT : UI_MUTED);

        if (is_current) {
            const char *act = ptc_ui_text(PTC_UI_T_LANG_BADGE_ACTIVE);
            int aw = measure_text(act, 12) + 18;
            if (aw > option.width - 112) aw = option.width - 112;
            UiRect act_pill = {option.x + option.width - 16 - aw, option.y + 16, aw, 24};
            fill_round_rect(pixels, stride, act_pill, 12, UI_SUCCESS_SOFT);
            draw_rect_outline(pixels, stride, act_pill, 12, 1, UI_SUCCESS_BORDER);
            fit_text(fitted, sizeof(fitted), act, 12, act_pill.width - 10);
            draw_text_center(pixels, stride, act_pill, fitted, 12, UI_SUCCESS);
        }

        /* Native language title + short description */
        fit_text(fitted, sizeof(fitted), LABELS[index], 22, option.width - 32);
        draw_text_bold(pixels, stride, option.x + 16, option.y + 72, fitted, 22,
                       focused ? UI_ACCENT : UI_INK);
        draw_wrapped_text(pixels, stride, option.x + 16, option.y + 98, DETAILS[index],
                          13, option.width - 32, 18, 2, UI_MUTED);

        /* Embedded native-language UI miniature preview */
        UiRect pv = {option.x + 16, option.y + 148, option.width - 32, 232};
        fill_round_rect(pixels, stride, pv, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, pv, 10, 1, focused ? UI_ACCENT : UI_BORDER);

        /* Mini header bar */
        UiRect pv_hdr = {pv.x + 10, pv.y + 10, pv.width - 20, 28};
        fill_round_rect(pixels, stride, pv_hdr, 6, UI_SURFACE);
        if (index == 0) {
            fill_round_rect(pixels, stride, (UiRect){pv_hdr.x + 8, pv_hdr.y + 7, 5, 14}, 2, UI_RGB(0x2E72D2));
            fill_round_rect(pixels, stride, (UiRect){pv_hdr.x + pv_hdr.width - 13, pv_hdr.y + 7, 5, 14}, 2, UI_RGB(0xE64A38));
            draw_text_center(pixels, stride, pv_hdr, "Switch OS", 12, UI_INK);
        } else {
            draw_text_bold(pixels, stride, pv_hdr.x + 10, pv_hdr.y + 19, "PlayWise", 12, UI_ACCENT);
            const char *zone = ptc_ui_text_in_language(PTC_UI_T_CHILD_ZONE, sample_lang);
            fit_text(fitted, sizeof(fitted), zone, 11, pv_hdr.width - 88);
            int zw = measure_text(fitted, 11);
            draw_text(pixels, stride, pv_hdr.x + pv_hdr.width - 10 - zw, pv_hdr.y + 19, fitted, 11, UI_MUTED);
        }

        /* Mini hero status card in target language */
        UiRect pv_hero = {pv.x + 10, pv.y + 48, pv.width - 20, 116};
        fill_round_rect(pixels, stride, pv_hero, 8, UI_SURFACE);
        draw_rect_outline(pixels, stride, pv_hero, 8, 1, UI_BORDER);

        const char *ready_txt = ptc_ui_text_in_language(PTC_UI_T_LANG_PV_READY, sample_lang);
        fit_text(fitted, sizeof(fitted), ready_txt, 11, pv_hero.width - 24);
        int rw = measure_text(fitted, 11) + 16;
        UiRect rpill = {pv_hero.x + 12, pv_hero.y + 12, rw, 22};
        fill_round_rect(pixels, stride, rpill, 11, UI_SUCCESS_SOFT);
        draw_text_center(pixels, stride, rpill, fitted, 11, UI_SUCCESS);

        fit_text(fitted, sizeof(fitted), ptc_ui_text_in_language(PTC_UI_T_LANG_PV_PLAYABLE_NOW, sample_lang),
                 12, pv_hero.width - 24);
        draw_text(pixels, stride, pv_hero.x + 12, pv_hero.y + 58, fitted, 12, UI_MUTED);

        fit_text(fitted, sizeof(fitted), ptc_ui_text_in_language(PTC_UI_T_LANG_PV_SAMPLE_MIN, sample_lang),
                 22, pv_hero.width - 24);
        draw_text_bold(pixels, stride, pv_hero.x + 12, pv_hero.y + 94, fitted, 22, UI_SUCCESS);

        /* Mini dual action chips */
        int btn_gap = 8;
        int btn_w = (pv.width - 20 - btn_gap) / 2;
        UiRect b0 = {pv.x + 10, pv.y + 176, btn_w, 42};
        UiRect b1 = {pv.x + 10 + btn_w + btn_gap, pv.y + 176, btn_w, 42};
        fill_round_rect(pixels, stride, b0, 8, UI_ACCENT);
        fit_text(fitted, sizeof(fitted), ptc_ui_text_in_language(PTC_UI_T_LANG_PV_REDEEM, sample_lang),
                 12, b0.width - 10);
        draw_text_center(pixels, stride, b0, fitted, 12, UI_ON_ACCENT);

        fill_round_rect(pixels, stride, b1, 8, UI_RAISED);
        draw_rect_outline(pixels, stride, b1, 8, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text_in_language(PTC_UI_T_LANG_PV_PARENT, sample_lang),
                 12, b1.width - 10);
        draw_text_center(pixels, stride, b1, fitted, 12, UI_INK);

        /* Bottom pure status badge (replaces duplicated A-confirm button inside card) */
        UiRect badge = {option.x + 16, option.y + option.height - 54, option.width - 32, 36};
        uint32_t badge_bg = is_current ? UI_SUCCESS_SOFT : (focused ? UI_ACCENT_SOFT : UI_PAGE);
        uint32_t badge_bd = is_current ? UI_SUCCESS_BORDER : (focused ? UI_ACCENT : UI_BORDER);
        uint32_t badge_fg = is_current ? UI_SUCCESS : (focused ? UI_ACCENT : UI_MUTED);
        PtcUiTextId badge_key = is_current ? PTC_UI_T_LANG_STATUS_IN_USE :
                                (focused ? PTC_UI_T_LANG_STATUS_CONFIRM : PTC_UI_T_LANG_STATUS_PREVIEW);
        fill_round_rect(pixels, stride, badge, 10, badge_bg);
        draw_rect_outline(pixels, stride, badge, 10, 1, badge_bd);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(badge_key), 14, badge.width - 16);
        draw_text_center(pixels, stride, badge, fitted, 14, badge_fg);
    }

    if (!model->message[0]) {
        const char *hint = ptc_ui_text(PTC_UI_T_LANG_FOOTER_HINT);
        int hw = measure_text(hint, 14);
        int center_x = (284 + 1026) / 2;
        draw_text(pixels, stride, center_x - hw / 2, 692, hint, 14, UI_MUTED);
    }

    home_button(pixels, stride, ptc_ui_settings_page_primary_rect(),
                ptc_ui_text(PTC_UI_T_A_CONFIRM_SAVE), true, false, model->waiting);
}

static void draw_support_event_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    int index = model->overlay_selection;
    char time_text[64];
    char row[256];
    char fitted[256];
    draw_dialog_shell(pixels, stride, model, &dialog, 960, 580);

    bool has_event = model->recent_event_count > 0;
    if (index < 0 || index >= model->recent_event_count) index = 0;

    bool has_err = has_event && model->recent_event_errors[index][0] != '\0';
    bool is_sparse = has_event && !has_err &&
                     model->recent_event_types[index][0] == '\0' &&
                     model->recent_event_request_ids[index][0] == '\0';

    /* 1. Modal header */
    {
        const char *title = model->overlay_title[0]
            ? model->overlay_title : ptc_ui_text(PTC_UI_T_RECENT_EVENT_DETAILS);
        fit_text(fitted, sizeof(fitted), title, 24, dialog.width - 64);
        draw_text_bold(pixels, stride, dialog.x + 32, dialog.y + 52, fitted, 24, UI_INK);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SUPPORT_EVENT_SUBTITLE), 14, dialog.width - 64);
        draw_text(pixels, stride, dialog.x + 32, dialog.y + 78, fitted, 14, UI_MUTED);
    }

    /* 2. Semantic result verdict banner */
    {
        UiRect banner = {dialog.x + 32, dialog.y + 96, dialog.width - 64, 84};
        uint32_t bg = (!has_event || is_sparse) ? UI_RAISED : (has_err ? UI_DANGER_SOFT : UI_SUCCESS_SOFT);
        uint32_t bd = (!has_event || is_sparse) ? UI_BORDER : (has_err ? UI_DANGER_BORDER : UI_SUCCESS_BORDER);
        uint32_t fg = (!has_event || is_sparse) ? UI_MUTED : (has_err ? UI_DANGER : UI_SUCCESS);
        int sym_kind = !has_event ? 0 : (has_err ? 3 : 1);
        const char *badge_txt = ptc_ui_text(!has_event ? PTC_UI_T_SUPPORT_EVENT_EMPTY_BADGE
                                            : (has_err ? PTC_UI_T_SUPPORT_EVENT_ERR_BADGE
                                                       : PTC_UI_T_SUPPORT_EVENT_OK_BADGE));

        fill_round_rect(pixels, stride, banner, 12, bg);
        draw_rect_outline(pixels, stride, banner, 12, 2, bd);

        UiRect icon_tile = {banner.x + 16, banner.y + 20, 44, 44};
        fill_round_rect(pixels, stride, icon_tile, 10, UI_SURFACE);
        draw_rect_outline(pixels, stride, icon_tile, 10, 1, bd);
        draw_status_symbol(pixels, stride, icon_tile.x + 22, icon_tile.y + 22, fg, sym_kind);

        int pw = measure_text(badge_txt, 13) + 24;
        if (pw > 180) pw = 180;
        UiRect pill = {banner.x + banner.width - 16 - pw, banner.y + 28, pw, 28};
        fill_round_rect(pixels, stride, pill, 14, UI_SURFACE);
        draw_rect_outline(pixels, stride, pill, 14, 1, bd);
        fit_text(fitted, sizeof(fitted), badge_txt, 13, pill.width - 12);
        draw_text_center(pixels, stride, pill, fitted, 13, fg);

        int tx = banner.x + 74;
        int tw = pill.x - tx - 14;
        const char *ev_name = (has_event && model->recent_event_names[index][0])
            ? model->recent_event_names[index] : ptc_ui_text(PTC_UI_T_UNKNOWN);
        fit_text(fitted, sizeof(fitted), ev_name, 19, tw);
        draw_text_bold(pixels, stride, tx, banner.y + 36, fitted, 19, UI_INK);

        const char *res_val = has_err ? model->recent_event_errors[index]
            : (has_event ? ptc_ui_text(PTC_UI_T_SUCCESS) : ptc_ui_text(PTC_UI_T_NONE));
        snprintf(row, sizeof(row), ptc_ui_text(PTC_UI_T_RESULT_S), res_val);
        fit_text(fitted, sizeof(fitted), row, 15, tw);
        draw_text_bold(pixels, stride, tx, banner.y + 64, fitted, 15, fg);
    }

    /* 3. Two-column metadata cards + full-width Request ID card */
    if (has_event) {
        format_event_time(model->recent_event_timestamps[index], true, time_text, sizeof(time_text));
    } else {
        snprintf(time_text, sizeof(time_text), "%s", ptc_ui_text(PTC_UI_T_NOT_DOCUMENTED));
    }
    {
        int col_w = (dialog.width - 64 - 12) / 2;
        UiRect c_type = {dialog.x + 32, dialog.y + 194, col_w, 68};
        UiRect c_time = {c_type.x + col_w + 12, dialog.y + 194, col_w, 68};

        fill_round_rect(pixels, stride, c_type, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, c_type, 10, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SUPPORT_EVENT_FIELD_TYPE), 13, c_type.width - 28);
        draw_text(pixels, stride, c_type.x + 14, c_type.y + 24, fitted, 13, UI_MUTED);
        const char *ev_type = (has_event && model->recent_event_types[index][0])
            ? model->recent_event_types[index] : ptc_ui_text(PTC_UI_T_NOT_DOCUMENTED);
        fit_text(fitted, sizeof(fitted), ev_type, 16, c_type.width - 28);
        draw_text_bold(pixels, stride, c_type.x + 14, c_type.y + 51, fitted, 16, UI_INK);

        fill_round_rect(pixels, stride, c_time, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, c_time, 10, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SUPPORT_EVENT_FIELD_TIME), 13, c_time.width - 28);
        draw_text(pixels, stride, c_time.x + 14, c_time.y + 24, fitted, 13, UI_MUTED);
        fit_text(fitted, sizeof(fitted), time_text, 16, c_time.width - 28);
        draw_text_bold(pixels, stride, c_time.x + 14, c_time.y + 51, fitted, 16, UI_INK);

        UiRect c_req = {dialog.x + 32, dialog.y + 274, dialog.width - 64, 68};
        fill_round_rect(pixels, stride, c_req, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, c_req, 10, 1, UI_BORDER);

        const char *redacted = ptc_ui_text(PTC_UI_T_SUPPORT_EVENT_REDACTED_BADGE);
        int rw = measure_text(redacted, 12) + 22;
        if (rw > 260) rw = 260;
        UiRect rpill = {c_req.x + c_req.width - 14 - rw, c_req.y + 21, rw, 26};
        fill_round_rect(pixels, stride, rpill, 13, UI_SURFACE);
        draw_rect_outline(pixels, stride, rpill, 13, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), redacted, 12, rpill.width - 10);
        draw_text_center(pixels, stride, rpill, fitted, 12, UI_MUTED);

        int req_max_w = rpill.x - (c_req.x + 14) - 14;
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SUPPORT_EVENT_FIELD_REQ_ID), 13, req_max_w);
        draw_text(pixels, stride, c_req.x + 14, c_req.y + 24, fitted, 13, UI_MUTED);
        const char *req_id = (has_event && model->recent_event_request_ids[index][0])
            ? model->recent_event_request_ids[index] : ptc_ui_text(PTC_UI_T_NOT_DOCUMENTED);
        fit_text(fitted, sizeof(fitted), req_id, 16, req_max_w);
        draw_text_bold(pixels, stride, c_req.x + 14, c_req.y + 51, fitted, 16, UI_INK);
    }

    /* 4. Diagnostic detail box */
    {
        UiRect dbox = {dialog.x + 32, dialog.y + 354, dialog.width - 64, 134};
        fill_round_rect(pixels, stride, dbox, 10, UI_PAGE);
        draw_rect_outline(pixels, stride, dbox, 10, 1, UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SUPPORT_EVENT_FIELD_DETAILS), 13, dbox.width - 28);
        draw_text_bold(pixels, stride, dbox.x + 14, dbox.y + 25, fitted, 13, UI_MUTED);

        const char *details = (has_event && model->recent_event_details[index][0])
            ? model->recent_event_details[index] : ptc_ui_text(PTC_UI_T_NONE);
        draw_wrapped_text(pixels, stride, dbox.x + 14, dbox.y + 52, details, 15,
                          dbox.width - 28, 24, 4, UI_INK);
    }

    /* 5. Footer hint + primary close button */
    {
        PtcUiRect btn_rect = ptc_ui_confirm_rect(model->overlay);
        int hint_max = btn_rect.x - (dialog.x + 32) - 20;
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SUPPORT_EVENT_FOOTER_HINT), 14, hint_max);
        draw_text(pixels, stride, dialog.x + 32, btn_rect.y + 32, fitted, 14, UI_MUTED);
        draw_dialog_button(pixels, stride, btn_rect, ptc_ui_text(PTC_UI_T_A_B_CLOSE),
                           UI_ACCENT, UI_ON_ACCENT, false);
    }
}

static void draw_setup_pctl_help(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const PtcUiTextId steps[] = {
        PTC_UI_T_SETUP_PCTL_HELP_STEP_1, PTC_UI_T_SETUP_PCTL_HELP_STEP_2,
        PTC_UI_T_SETUP_PCTL_HELP_STEP_3, PTC_UI_T_SETUP_PCTL_HELP_STEP_4,
        PTC_UI_T_SETUP_PCTL_HELP_STEP_5, PTC_UI_T_SETUP_PCTL_HELP_STEP_6
    };
    static const PtcUiTextId descs[] = {
        PTC_UI_T_SETUP_PCTL_HELP_DESC_1, PTC_UI_T_SETUP_PCTL_HELP_DESC_2,
        PTC_UI_T_SETUP_PCTL_HELP_DESC_3, PTC_UI_T_SETUP_PCTL_HELP_DESC_4,
        PTC_UI_T_SETUP_PCTL_HELP_DESC_5, PTC_UI_T_SETUP_PCTL_HELP_DESC_6
    };
    static const PtcUiTextId mock_navs[] = {
        PTC_UI_T_SETUP_PCTL_MOCK_NAV_1, PTC_UI_T_SETUP_PCTL_MOCK_NAV_2,
        PTC_UI_T_SETUP_PCTL_MOCK_NAV_3, PTC_UI_T_SETUP_PCTL_MOCK_NAV_4,
        PTC_UI_T_SETUP_PCTL_MOCK_NAV_5
    };
    UiRect dialog;
    char fitted[256];
    char buf[96];
    PtcUiRect base_dialog = ptc_ui_dialog_rect(1080, 620);
    draw_dialog_shell(pixels, stride, model, &dialog, 1080, 620);
    int anim_dy = dialog.y - base_dialog.y;

    int active = model->overlay_selection;
    if (active < 0 || active >= 6) active = 0;

    /* 1. Header with interactive guide badge */
    {
        const char *badge = ptc_ui_text(PTC_UI_T_SETUP_PCTL_HELP_INTERACT_BADGE);
        int bw = measure_text(badge, 12) + 24;
        if (bw > 280) bw = 280;
        UiRect pill = {dialog.x + dialog.width - 32 - bw, dialog.y + 28, bw, 28};
        fill_round_rect(pixels, stride, pill, 14, UI_ACCENT_SOFT);
        draw_rect_outline(pixels, stride, pill, 14, 1, UI_ACCENT);
        fit_text(fitted, sizeof(fitted), badge, 12, pill.width - 12);
        draw_text_center(pixels, stride, pill, fitted, 12, UI_ACCENT);

        int title_max = pill.x - (dialog.x + 32) - 16;
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SETUP_PCTL_HELP_TITLE), 23, title_max);
        draw_text_bold(pixels, stride, dialog.x + 32, dialog.y + 50, fitted, 23, UI_INK);

        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SETUP_PCTL_HELP_SUBTITLE), 14, dialog.width - 64);
        draw_text(pixels, stride, dialog.x + 32, dialog.y + 80, fitted, 14, UI_MUTED);
    }

    /* 2. Left column: 6 interactive step cards */
    for (int i = 0; i < 6; ++i) {
        UiRect sr = to_uirect(ptc_ui_setup_pctl_step_rect(i));
        sr.y += anim_dy;
        bool is_sel = (i == active);

        fill_round_rect(pixels, stride, sr, 10, is_sel ? UI_ACCENT_SOFT : UI_PAGE);
        draw_rect_outline(pixels, stride, sr, 10, is_sel ? 2 : 1, is_sel ? UI_ACCENT : UI_BORDER);
        if (is_sel) draw_focus_ring(pixels, stride, sr, 10);

        UiRect num_box = {sr.x + 12, sr.y + 15, 32, 32};
        fill_round_rect(pixels, stride, num_box, 8, is_sel ? UI_ACCENT : UI_RAISED);
        draw_rect_outline(pixels, stride, num_box, 8, 1, is_sel ? UI_ACCENT : UI_BORDER);
        snprintf(buf, sizeof(buf), "%d", i + 1);
        draw_text_center(pixels, stride, num_box, buf, 15, is_sel ? UI_ON_ACCENT : UI_INK);

        fit_text(fitted, sizeof(fitted), ptc_ui_text(steps[i]), 15, sr.width - 68);
        draw_text_bold(pixels, stride, sr.x + 56, sr.y + 27, fitted, 15, is_sel ? UI_ACCENT : UI_INK);

        fit_text(fitted, sizeof(fitted), ptc_ui_text(descs[i]), 13, sr.width - 68);
        draw_text(pixels, stride, sr.x + 56, sr.y + 49, fitted, 13, UI_MUTED);
    }

    /* 3. Right column: Live Nintendo Switch System Settings mock preview */
    {
        UiRect mock = {dialog.x + 562, dialog.y + 104, 486, 330};
        fill_round_rect(pixels, stride, mock, 14, UI_RGB(0x141821));
        draw_rect_outline(pixels, stride, mock, 14, 2, UI_RGB(0x2E3648));

        /* Mock top bar */
        snprintf(buf, sizeof(buf), ptc_ui_text(PTC_UI_T_SETUP_PCTL_MOCK_STEP_FMT), active + 1);
        int sw = measure_text(buf, 12) + 20;
        if (sw > 140) sw = 140;
        UiRect sp = {mock.x + mock.width - 14 - sw, mock.y + 11, sw, 24};
        fill_round_rect(pixels, stride, sp, 12, UI_RGB(0x2563EB));
        fit_text(fitted, sizeof(fitted), buf, 12, sp.width - 10);
        draw_text_center(pixels, stride, sp, fitted, 12, UI_RGB(0xFFFFFF));

        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SETUP_PCTL_MOCK_HEADER), 13,
                 sp.x - (mock.x + 16) - 10);
        draw_text(pixels, stride, mock.x + 16, mock.y + 28, fitted, 13, UI_RGB(0x94A3B8));
        draw_line(pixels, stride, mock.x + 14, mock.y + 43, mock.x + mock.width - 14, mock.y + 43,
                  1, UI_RGB(0x2A3244));

        /* Mock left settings sidebar */
        UiRect sb = {mock.x + 14, mock.y + 54, 152, 262};
        fill_round_rect(pixels, stride, sb, 9, UI_RGB(0x1C2230));
        for (int j = 0; j < 5; ++j) {
            UiRect item = {sb.x + 8, sb.y + 10 + j * 49, sb.width - 16, 40};
            if (j == 2) {
                bool blue_hi = (active <= 1);
                fill_round_rect(pixels, stride, item, 7,
                                blue_hi ? UI_RGB(0x2563EB) : UI_RGB(0x452414));
                draw_rect_outline(pixels, stride, item, 7, 1,
                                  blue_hi ? UI_RGB(0x60A5FA) : UI_RGB(0xFB923C));
                fill_round_rect(pixels, stride, (UiRect){item.x + 8, item.y + 16, 8, 8}, 4,
                                blue_hi ? UI_RGB(0xFFFFFF) : UI_RGB(0xFB923C));
                fit_text(fitted, sizeof(fitted), ptc_ui_text(mock_navs[j]), 13, item.width - 26);
                draw_text_bold(pixels, stride, item.x + 22, item.y + 25, fitted, 13, UI_RGB(0xFFFFFF));
            } else {
                fit_text(fitted, sizeof(fitted), ptc_ui_text(mock_navs[j]), 13, item.width - 14);
                draw_text(pixels, stride, item.x + 8, item.y + 25, fitted, 13, UI_RGB(0x94A3B8));
            }
        }

        /* Mock right detail pane */
        UiRect pane = {mock.x + 176, mock.y + 54, 296, 262};
        fill_round_rect(pixels, stride, pane, 9, UI_RGB(0x1C2230));
        fit_text(fitted, sizeof(fitted), ptc_ui_text(steps[active]), 14, pane.width - 24);
        draw_text_bold(pixels, stride, pane.x + 12, pane.y + 26, fitted, 14, UI_RGB(0xFFFFFF));

        PtcUiTextId opts[3] = {
            PTC_UI_T_SETUP_PCTL_MOCK_OPT_1,
            PTC_UI_T_SETUP_PCTL_MOCK_OPT_2,
            active == 5 ? PTC_UI_T_SETUP_PCTL_MOCK_OPT_DONE : PTC_UI_T_SETUP_PCTL_MOCK_OPT_3
        };
        for (int k = 0; k < 3; ++k) {
            UiRect opt = {pane.x + 12, pane.y + 42 + k * 50, pane.width - 24, 40};
            bool hi = (k == 0 && active == 2) || (k == 1 && active == 3) || (k == 2 && active >= 4);
            uint32_t obg = UI_RGB(0x242C3D);
            uint32_t obd = UI_RGB(0x2E384D);
            if (hi) {
                obg = (k == 0) ? UI_RGB(0x2563EB) : (k == 1 ? UI_RGB(0xEA580C) : UI_RGB(0x16A34A));
                obd = (k == 0) ? UI_RGB(0x93C5FD) : (k == 1 ? UI_RGB(0xFDBA74) : UI_RGB(0x86EFAC));
            }
            fill_round_rect(pixels, stride, opt, 8, obg);
            draw_rect_outline(pixels, stride, opt, 8, hi ? 2 : 1, obd);
            fit_text(fitted, sizeof(fitted), ptc_ui_text(opts[k]), 13, opt.width - 20);
            if (hi) draw_text_bold(pixels, stride, opt.x + 10, opt.y + 25, fitted, 13, UI_RGB(0xFFFFFF));
            else draw_text(pixels, stride, opt.x + 10, opt.y + 25, fitted, 13, UI_RGB(0xCBD5E1));
        }

        draw_line(pixels, stride, pane.x + 12, pane.y + 198, pane.x + pane.width - 12, pane.y + 198,
                  1, UI_RGB(0x2B354A));
        draw_wrapped_text(pixels, stride, pane.x + 12, pane.y + 219, ptc_ui_text(descs[active]),
                          13, pane.width - 24, 19, 2, UI_RGB(0x93C5FD));

        /* Return-check card below mock */
        UiRect note = {dialog.x + 562, dialog.y + 446, 486, 70};
        fill_round_rect(pixels, stride, note, 10, UI_SUCCESS_SOFT);
        draw_rect_outline(pixels, stride, note, 10, 1, UI_SUCCESS_BORDER);
        draw_status_symbol(pixels, stride, note.x + 24, note.y + 35, UI_SUCCESS, 1);
        draw_wrapped_text(pixels, stride, note.x + 46, note.y + 29,
                          ptc_ui_text(PTC_UI_T_SETUP_PCTL_HELP_RETURN),
                          14, note.width - 60, 20, 2, UI_INK);
    }

    /* 4. Footer hint + primary return button */
    {
        PtcUiRect btn_rect = ptc_ui_cancel_rect(model->overlay);
        int hint_max = btn_rect.x - (dialog.x + 32) - 20;
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_SETUP_PCTL_NAV_HINT), 14, hint_max);
        draw_text(pixels, stride, dialog.x + 32, btn_rect.y + 32, fitted, 14, UI_MUTED);
        draw_dialog_button(pixels, stride, btn_rect,
                           ptc_ui_text(PTC_UI_T_A_B_RETURN), UI_ACCENT, UI_ON_ACCENT, false);
    }
}

static void draw_support_guide(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    static const PtcUiTextId step_labels[] = {
        PTC_UI_T_SUPPORT_GUIDE_STEP_1, PTC_UI_T_SUPPORT_GUIDE_STEP_2,
        PTC_UI_T_SUPPORT_GUIDE_STEP_3, PTC_UI_T_SUPPORT_GUIDE_STEP_4
    };
    static const PtcUiTextId titles[] = {PTC_UI_T_GUIDE_CHECKS, PTC_UI_T_GUIDE_LIMIT,
        PTC_UI_T_GUIDE_CODES, PTC_UI_T_GUIDE_BACKEND};
    static const PtcUiTextId bodies[] = {PTC_UI_T_GUIDE_CHECKS_BODY, PTC_UI_T_GUIDE_LIMIT_BODY,
        PTC_UI_T_GUIDE_CODES_BODY, PTC_UI_T_GUIDE_BACKEND_BODY};
    int page = model->support_guide_page >= 0 && model->support_guide_page < 4
        ? model->support_guide_page : 0;
    char heading[192];
    char fitted[160];

    draw_subpage_shell(pixels, stride, model, ptc_ui_text(PTC_UI_T_SUPPORT_GUIDE),
                       ptc_ui_text(PTC_UI_T_COMPATIBILITY_STATUS_DIAGNOSTICS_AND_SAFE_RECOVERY),
                       ptc_ui_text(PTC_UI_T_SUPPORT_GUIDE_BACK),
                       ptc_ui_text(PTC_UI_T_SUPPORT_GUIDE_PATH));

    /* 1. Top 4-Step Segmented Stepper */
    for (int i = 0; i < 4; ++i) {
        UiRect sr = to_uirect(ptc_ui_support_guide_step_rect(i));
        bool active = (i == page);
        fill_round_rect(pixels, stride, sr, 12, active ? UI_ACCENT : UI_SURFACE);
        draw_rect_outline(pixels, stride, sr, 12, active ? 2 : 1, active ? UI_ACCENT : UI_BORDER);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(step_labels[i]), 15, sr.width - 20);
        draw_text_center(pixels, stride, sr, fitted, 15, active ? UI_ON_ACCENT : UI_INK);
    }

    /* 2. Live Environment Check Strip */
    {
        UiRect strip = {54, 228, 1172, 54};
        bool known = model->restriction_enabled_available &&
            ptc_ui_status_is_fresh(model, ptc_ui_render_now());
        bool pctl_ok = known && model->restriction_enabled;
        const char *pctl_txt = ptc_ui_text(!known ? PTC_UI_T_SETUP_PCTL_UNKNOWN :
            model->restriction_enabled ? PTC_UI_T_SETUP_PCTL_ON : PTC_UI_T_SETUP_PCTL_OFF);
        const char *clock_txt = ptc_ui_text(PTC_UI_T_GUIDE_CLOCK_STATUS);
        draw_plan_card(pixels, stride, strip, false);

        int pw = measure_text(pctl_txt, 14) + 28;
        if (pw > 470) pw = 470;
        UiRect pctl_pill = {strip.x + 14, strip.y + 10, pw, 34};
        fill_round_rect(pixels, stride, pctl_pill, 10, pctl_ok ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
        draw_rect_outline(pixels, stride, pctl_pill, 10, 1, pctl_ok ? UI_SUCCESS_BORDER : UI_WARNING_BORDER);
        fit_text(fitted, sizeof(fitted), pctl_txt, 14, pctl_pill.width - 16);
        draw_text_center(pixels, stride, pctl_pill, fitted, 14, pctl_ok ? UI_SUCCESS : UI_WARNING_TEXT);

        int cx = pctl_pill.x + pctl_pill.width + 12;
        int max_cw = ptc_ui_support_guide_refresh_rect().x - cx - 14;
        int cw = measure_text(clock_txt, 14) + 28;
        if (cw > max_cw) cw = max_cw;
        if (cw > 80) {
            UiRect clock_pill = {cx, strip.y + 10, cw, 34};
            fill_round_rect(pixels, stride, clock_pill, 10, UI_WARNING_SOFT);
            draw_rect_outline(pixels, stride, clock_pill, 10, 1, UI_WARNING_BORDER);
            fit_text(fitted, sizeof(fitted), clock_txt, 14, clock_pill.width - 16);
            draw_text_center(pixels, stride, clock_pill, fitted, 14, UI_WARNING_TEXT);
        }

        home_button(pixels, stride, ptc_ui_support_guide_refresh_rect(),
                    ptc_ui_text(PTC_UI_T_SUPPORT_GUIDE_RETEST), false, false, model->waiting);
    }

    /* 3. Main Structured Guidance Card */
    {
        UiRect card = {54, 294, 1172, 344};
        draw_plan_card(pixels, stride, card, false);
        fill_round_rect(pixels, stride, (UiRect){card.x + 16, card.y + 22, 5, 30}, 2, UI_ACCENT);
        snprintf(heading, sizeof(heading), "%d/4  %s", page + 1, ptc_ui_text(titles[page]));
        fit_text(fitted, sizeof(fitted), heading, 22, card.width - 64);
        draw_text_bold(pixels, stride, card.x + 34, card.y + 45, fitted, 22, UI_ACCENT);
        draw_line(pixels, stride, card.x + 24, card.y + 64, card.x + card.width - 24, card.y + 64, 1, UI_BORDER);
        draw_wrapped_text(pixels, stride, card.x + 34, card.y + 96,
                          ptc_ui_text(bodies[page]), 18, card.width - 68, 28, 9, UI_INK);
    }

    home_button(pixels, stride, ptc_ui_support_guide_nav_rect(0),
                ptc_ui_text(PTC_UI_T_GUIDE_PREV), false, false, false);
    home_button(pixels, stride, ptc_ui_support_guide_nav_rect(1),
                ptc_ui_text(PTC_UI_T_GUIDE_NEXT), false, false, false);
    home_button(pixels, stride, ptc_ui_cancel_rect(model->overlay),
                ptc_ui_text(PTC_UI_T_A_B_RETURN), true, false, false);
}

/* Work in the reference screenshot's 1280x720 space. Vendored libtesla uses
   448x720: the left panel occupies exactly 35% of a 16:9 screen, full height.
   The dark device display stays dark inside either NRO theme. */
static UiRect notification_scene_rect(UiRect screen, int x, int y, int w, int h)
{
    return (UiRect){screen.x + x * screen.width / 1280, screen.y + y * screen.height / 720,
        w * screen.width / 1280, h * screen.height / 720};
}

static void notification_shade(uint32_t *pixels, uint32_t stride, UiRect rect, uint32_t color, uint8_t alpha)
{
    uint32_t packed = resolve_color(color);
    for (int y = rect.y; y < rect.y + rect.height; ++y)
        for (int x = rect.x; x < rect.x + rect.width; ++x)
            blend_pixel(pixels, stride, x, y, packed, alpha);
}

static void draw_notification_example(uint32_t *pixels, uint32_t stride, UiRect screen, bool detail)
{
    const PtcUiPalette *dark = ptc_ui_theme_palette(PTC_UI_RESOLVED_DARK);
    uint32_t ink = UI_RGB(dark->text_primary), muted = UI_RGB(dark->text_secondary);
    uint32_t base = UI_RGB(dark->page_bg), raised = UI_RGB(dark->surface_raised);
    uint32_t accent = UI_RGB(dark->accent), warning = UI_RGB(dark->warning);
    fill_round_rect(pixels, stride, screen, 10, raised);
    /* Muted HOME tiles remain visible through the native full-screen mask. */
    const uint32_t tiles[] = {UI_ACCENT_SOFT, UI_SUCCESS_SOFT, UI_WARNING_SOFT, UI_DANGER_SOFT, UI_NIGHT_SOFT};
    for (int i = 0; i < 5; ++i) {
        UiRect tile = notification_scene_rect(screen, 120 + i * 224, 210, 204, 240);
        fill_round_rect(pixels, stride, tile, 6, tiles[i]);
        draw_circle_outline(pixels, stride, tile.x + tile.width / 2, tile.y + tile.height / 2,
            tile.width / 4, 4, UI_BORDER);
    }
    notification_shade(pixels, stride, screen, base, 222);
    UiRect clock = notification_scene_rect(screen, 588, 100, 104, 104);
    int cx = clock.x + clock.width / 2, cy = clock.y + clock.height / 2;
    draw_circle_outline(pixels, stride, cx, cy, clock.width / 2, 3, ink);
    draw_line(pixels, stride, cx, cy, cx, cy - clock.height / 3, 3, ink);
    draw_line(pixels, stride, cx, cy, cx + clock.width / 4, cy, 3, ink);
    draw_line(pixels, stride, clock.x + 3, clock.y + 3, clock.x + 13, clock.y - 3, 3, ink);
    draw_line(pixels, stride, clock.x + clock.width - 13, clock.y - 3,
        clock.x + clock.width - 3, clock.y + 3, 3, ink);
    bool compact = screen.width < 460;
    UiRect native_text = notification_scene_rect(screen, 460, 280, 360, 110);
    draw_wrapped_text_centered(pixels, stride, screen.x + screen.width / 2,
        native_text.y + (compact ? 12 : 15),
        ptc_ui_text(PTC_UI_T_NOTICE_NATIVE_LIMIT), compact ? 11 : 14,
        native_text.width, compact ? 14 : 20, 2, ink);
    UiRect sleep = notification_scene_rect(screen, 280, 450, 720, 82);
    UiRect settings = notification_scene_rect(screen, 280, 550, 720, 60);
    draw_rect_outline(pixels, stride, sleep, 3, 2, accent);
    draw_text_center(pixels, stride, sleep, ptc_ui_text(PTC_UI_T_NOTICE_NATIVE_SLEEP), compact ? 11 : 14, ink);
    draw_rect_outline(pixels, stride, settings, 3, 1, muted);
    draw_text_center(pixels, stride, settings, ptc_ui_text(PTC_UI_T_NOTICE_NATIVE_SETTINGS), compact ? 10 : 12, ink);
    UiRect native_footer = notification_scene_rect(screen, 30, 647, 1220, 1);
    fill_rect(pixels, stride, native_footer, muted);

    if (detail) {
        UiRect panel = notification_scene_rect(screen, 0, 0, 448, 720);
        char fitted[128];
        notification_shade(pixels, stride, panel, base, 238); /* Production panel alpha: 0xE/0xF. */
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NOTIFY_TITLE), compact ? 10 : 13, panel.width - 14);
        draw_text(pixels, stride, panel.x + (compact ? 7 : 10), panel.y + (compact ? 15 : 23),
                  fitted, compact ? 10 : 13, accent);
        UiRect reason = notification_scene_rect(screen, 18, 68, 412, 230);
        fill_round_rect(pixels, stride, reason, 4, UI_RGB(dark->warning_soft));
        fill_rect(pixels, stride, (UiRect){reason.x, reason.y + 4, 2, reason.height - 8}, warning);
        draw_wrapped_text(pixels, stride, reason.x + (compact ? 7 : 10), reason.y + (compact ? 15 : 25),
            ptc_ui_text(PTC_UI_T_NOTIFY_EYE), compact ? 10 : 14,
            reason.width - (compact ? 12 : 20), compact ? 13 : 20, compact ? 1 : 2, warning);
        draw_text(pixels, stride, reason.x + (compact ? 7 : 10), reason.y + (compact ? 52 : 86),
                  "05:00", compact ? 22 : 34, warning);
        draw_wrapped_text(pixels, stride, panel.x + (compact ? 7 : 10), panel.y + (compact ? 106 : 170),
            ptc_ui_text(PTC_UI_T_NOTICE_OVERLAY_HELP), compact ? 9 : 11,
            panel.width - (compact ? 14 : 20), compact ? 12 : 16, 2, ink);
        for (int i = 0; i < 3; ++i) {
            UiRect row = notification_scene_rect(screen, 18, 430 + i * 58, 412, 48);
            fill_round_rect(pixels, stride, row, 3, raised);
            const PtcUiTextId labels[] = {PTC_UI_T_EYE_CARE, PTC_UI_T_BEDTIME, PTC_UI_T_DOCK_TV};
            const char *value = i == 0 ? "05:00" : ptc_ui_text(PTC_UI_T_CURRENTLY_OFF);
            int rsize = compact ? 9 : 10;
            int value_width = measure_text(value, rsize);
            fit_text(fitted, sizeof(fitted), ptc_ui_text(labels[i]), rsize, row.width - value_width - 14);
            draw_text(pixels, stride, row.x + (compact ? 5 : 8), row.y + (compact ? 11 : 16), fitted, rsize, muted);
            draw_text(pixels, stride, row.x + row.width - value_width - (compact ? 5 : 8),
                      row.y + (compact ? 11 : 16), value, rsize, i == 0 ? warning : muted);
        }
        UiRect footer = notification_scene_rect(screen, 18, 647, 412, 1);
        fill_rect(pixels, stride, footer, muted);
        draw_text(pixels, stride, panel.x + (compact ? 7 : 10), panel.y + panel.height - (compact ? 6 : 12),
            ptc_ui_text(PTC_UI_T_B_BACK), compact ? 9 : 11, ink);
    } else {
        /* Match the observed receiver's upper-left notice; themes can vary. */
        UiRect notice = notification_scene_rect(screen, 24, 28, 448, 180);
        char fitted[128];
        fill_round_rect(pixels, stride, notice, 5, UI_RGB(dark->surface));
        draw_rect_outline(pixels, stride, notice, 5, 1, UI_RGB(dark->border_control));
        fill_rect(pixels, stride, (UiRect){notice.x + (compact ? 6 : 8), notice.y + (compact ? 6 : 10),
                                           3, notice.height - (compact ? 12 : 20)}, warning);
        fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_NOTIFY_TITLE),
                 compact ? 10 : 12, notice.width - (compact ? 18 : 28));
        draw_text(pixels, stride, notice.x + (compact ? 14 : 20), notice.y + (compact ? 15 : 21),
                  fitted, compact ? 10 : 12, accent);
        draw_wrapped_text(pixels, stride, notice.x + (compact ? 14 : 20), notice.y + (compact ? 29 : 43),
            ptc_ui_text(PTC_UI_T_NOTIFY_EYE), compact ? 9 : 12,
            notice.width - (compact ? 20 : 32), compact ? 12 : 17, compact ? 1 : 2, ink);
        draw_text(pixels, stride, notice.x + (compact ? 14 : 20), notice.y + notice.height - (compact ? 6 : 12),
                  "05:00", compact ? 10 : 13, warning);
    }
    draw_rect_outline(pixels, stride, screen, 10, 1, UI_CONTROL);
}

static void draw_notification_step(uint32_t *pixels, uint32_t stride, int x, int y, int step, bool selected)
{
    UiRect badge = {x, y, 22, 22};
    fill_round_rect(pixels, stride, badge, 11, selected ? UI_ACCENT : UI_ACCENT_SOFT);
    draw_text_center(pixels, stride, badge, step == 1 ? "1" : "2", 14,
        selected ? UI_ON_ACCENT : UI_ACCENT);
}

static void draw_notification_verification(uint32_t *pixels, uint32_t stride, const PtcUiModel *m)
{
    UiRect group = to_uirect(ptc_ui_notification_verification_rect());
    char fitted[192];
    draw_plan_card(pixels, stride, group, false);

    for (int i = PTC_UI_NOTIFICATION_PROBE; i < PTC_UI_NOTIFICATION_OPTION_COUNT; ++i) {
        UiRect button = to_uirect(ptc_ui_notification_option_rect(i));
        bool selected = (m->overlay_selection == i);
        fill_round_rect(pixels, stride, button, 10, selected ? UI_ACCENT_SOFT : UI_RAISED);
        draw_rect_outline(pixels, stride, button, 10, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
        if (selected) draw_focus_ring(pixels, stride, button, 10);
        draw_notification_step(pixels, stride, button.x + 12, button.y + 11, i, selected);
        fit_text(fitted, sizeof(fitted),
                 ptc_ui_text(i == PTC_UI_NOTIFICATION_PROBE ? PTC_UI_T_NOTICE_PROBE : PTC_UI_T_NOTICE_TEST),
                 15, button.width - 52);
        draw_text_bold(pixels, stride, button.x + 42, button.y + 28, fitted, 15,
                       selected ? UI_ACCENT : UI_INK);
    }

    /* Recommended sequence arrow between step 1 and step 2 */
    UiRect first = to_uirect(ptc_ui_notification_option_rect(PTC_UI_NOTIFICATION_PROBE));
    UiRect second = to_uirect(ptc_ui_notification_option_rect(PTC_UI_NOTIFICATION_TEST));
    int ax = (first.x + first.width + second.x) / 2;
    int ay = first.y + first.height / 2;
    draw_line(pixels, stride, ax - 8, ay, ax + 7, ay, 2, UI_MUTED);
    draw_line(pixels, stride, ax + 2, ay - 5, ax + 7, ay, 2, UI_MUTED);
    draw_line(pixels, stride, ax + 2, ay + 5, ax + 7, ay, 2, UI_MUTED);

    /* Right live receipt pill box */
    UiRect receipt = to_uirect(ptc_ui_notification_info_rect(2));
    bool active_step = (m->overlay_selection == PTC_UI_NOTIFICATION_PROBE ||
                        m->overlay_selection == PTC_UI_NOTIFICATION_TEST);
    fill_round_rect(pixels, stride, receipt, 10, UI_PAGE);
    draw_rect_outline(pixels, stride, receipt, 10, active_step ? 2 : 1,
                      active_step ? UI_ACCENT : UI_BORDER);
    {
        char line[512];
        const char *msg = m->reason_test_message[0]
            ? m->reason_test_message
            : ptc_ui_text(PTC_UI_T_NOTICE_RESULT_IDLE);
        snprintf(line, sizeof(line), "%.64s%.384s", ptc_ui_text(PTC_UI_T_NOTICE_RECEIPT_PREFIX), msg);
        draw_wrapped_text(pixels, stride, receipt.x + 14, receipt.y + 22, line, 14,
                          receipt.width - 28, 19, 2,
                          m->reason_test_message[0] ? UI_ACCENT : UI_MUTED);
    }
}

static void draw_notification_channel_chip(uint32_t *pixels, uint32_t stride,
                                           UiRect rect, const char *label, const char *state,
                                           uint32_t state_fg, uint32_t state_bg, uint32_t state_bd)
{
    char fitted[96];
    fill_round_rect(pixels, stride, rect, 10, state_bg);
    draw_rect_outline(pixels, stride, rect, 10, 1, state_bd);
    fill_round_rect(pixels, stride, (UiRect){rect.x + 10, rect.y + 15, 8, 8}, 4, state_fg);
    snprintf(fitted, sizeof(fitted), "%s: %s", label, state);
    fit_text(fitted, sizeof(fitted), fitted, 13, rect.width - 30);
    draw_text_bold(pixels, stride, rect.x + 24, rect.y + 24, fitted, 13, state_fg);
}

static void draw_notification_preferences(uint32_t *pixels, uint32_t stride, const PtcUiModel *m)
{
    /* Scheme B: Unified settings shell + Top master switch bar + Side-by-side dual mode comparison + Footer self-check. */
    char buf[224];
    char fitted[192];

    draw_settings_page_shell(pixels, stride, m, ptc_ui_text(PTC_UI_T_ALERT_PREFERENCES),
                             ptc_ui_text(PTC_UI_T_NOTICE_SUBTITLE_B));

    /* --- 1. Top master switch + dual-channel live readiness strip --- */
    UiRect row = to_uirect(ptc_ui_notification_option_rect(PTC_UI_NOTIFICATION_TOGGLE));
    bool enabled = m->reason_notifications_enabled;
    bool selected = (m->overlay_selection == PTC_UI_NOTIFICATION_TOGGLE);
    fill_round_rect(pixels, stride, row, 12, selected ? UI_ACCENT_SOFT : UI_RAISED);
    draw_rect_outline(pixels, stride, row, 12, selected ? 2 : 1, selected ? UI_ACCENT : UI_BORDER);
    if (selected) draw_focus_ring(pixels, stride, row, 12);

    UiRect toggle = {row.x + row.width - 68, row.y + 25, 46, 24};
    const char *status = ptc_ui_text(enabled ? PTC_UI_T_ENABLED : PTC_UI_T_DISABLED_2);
    int status_w = measure_text(status, 15);
    int status_x = toggle.x - 12 - status_w;
    draw_text_bold(pixels, stride, status_x, row.y + 42, status, 15, enabled ? UI_SUCCESS : UI_MUTED);
    draw_toggle_switch(pixels, stride, toggle, enabled, selected, false, NULL, NULL);

    /* Two compact live readiness chips to the left of the toggle */
    const char *brief_state = ptc_ui_text(
        m->reason_app_muted ? PTC_UI_T_NOTICE_STATE_MUTED :
        m->reason_external_notifications == 1 ? PTC_UI_T_NOTICE_STATE_READY :
        m->reason_external_notifications == 0 ? PTC_UI_T_NOTICE_STATE_OFF : PTC_UI_T_NOTICE_STATE_UNKNOWN);
    uint32_t brief_fg = m->reason_app_muted ? UI_WARNING_TEXT :
                        (m->reason_external_notifications == 1) ? UI_SUCCESS :
                        (m->reason_external_notifications == 0 ? UI_WARNING_TEXT : UI_MUTED);
    uint32_t brief_bg = m->reason_app_muted ? UI_WARNING_SOFT :
                        (m->reason_external_notifications == 1) ? UI_SUCCESS_SOFT :
                        (m->reason_external_notifications == 0 ? UI_WARNING_SOFT : UI_PAGE);
    uint32_t brief_bd = m->reason_app_muted ? UI_WARNING_BORDER :
                        (m->reason_external_notifications == 1) ? UI_SUCCESS_BORDER :
                        (m->reason_external_notifications == 0 ? UI_WARNING_BORDER : UI_BORDER);

    const char *detail_state = ptc_ui_text(
        !m->reason_overlay_installed ? PTC_UI_T_NOTICE_STATE_MISSING :
        m->reason_detail_ready ? PTC_UI_T_NOTICE_STATE_READY : PTC_UI_T_NOTICE_STATE_NEED_HIDE);
    uint32_t detail_fg = !m->reason_overlay_installed ? UI_DANGER :
                         (m->reason_detail_ready ? UI_SUCCESS : UI_WARNING_TEXT);
    uint32_t detail_bg = !m->reason_overlay_installed ? UI_DANGER_SOFT :
                         (m->reason_detail_ready ? UI_SUCCESS_SOFT : UI_WARNING_SOFT);
    uint32_t detail_bd = !m->reason_overlay_installed ? UI_DANGER_BORDER :
                         (m->reason_detail_ready ? UI_SUCCESS_BORDER : UI_WARNING_BORDER);

    int chip_w = 198;
    UiRect brief_chip = {status_x - 20 - chip_w, row.y + 18, chip_w, 38};
    UiRect detail_chip = {brief_chip.x - 10 - chip_w, row.y + 18, chip_w, 38};
    draw_notification_channel_chip(pixels, stride, detail_chip,
                                   ptc_ui_text(PTC_UI_T_NOTICE_CHANNEL_DETAIL), detail_state,
                                   detail_fg, detail_bg, detail_bd);
    draw_notification_channel_chip(pixels, stride, brief_chip,
                                   ptc_ui_text(PTC_UI_T_NOTICE_CHANNEL_BRIEF), brief_state,
                                   brief_fg, brief_bg, brief_bd);

    /* Left title + shortcut hint (+ detected overlay manager) inside top bar */
    int left_max_w = detail_chip.x - (row.x + 22) - 14;
    fit_text(fitted, sizeof(fitted), ptc_ui_text(PTC_UI_T_REASON_NOTIFICATIONS), 19, left_max_w);
    draw_text_bold(pixels, stride, row.x + 22, row.y + 34, fitted, 19, UI_INK);
    if (m->reason_manager_label[0]) {
        snprintf(buf, sizeof(buf), "%s%s | %s", m->reason_overlay_shortcut,
                 ptc_ui_text(m->reason_overlay_shortcut[0] ? PTC_UI_T_NOTIFY_SHORTCUT : PTC_UI_T_NOTIFY_OPEN),
                 m->reason_manager_label);
    } else {
        snprintf(buf, sizeof(buf), "%s%s", m->reason_overlay_shortcut,
                 ptc_ui_text(m->reason_overlay_shortcut[0] ? PTC_UI_T_NOTIFY_SHORTCUT : PTC_UI_T_NOTIFY_OPEN));
    }
    fit_text(fitted, sizeof(fitted), buf, 13, left_max_w);
    draw_text(pixels, stride, row.x + 22, row.y + 58, fitted, 13, UI_ACCENT);

    /* --- 2. Middle side-by-side dual-mode comparison cards (no L/R toggle required) --- */
    for (int i = 0; i < 2; ++i) {
        UiRect card = to_uirect(ptc_ui_notification_preview_card_rect(i));
        UiRect screen = to_uirect(ptc_ui_notification_preview_screen_rect(i));
        UiRect desc = to_uirect(ptc_ui_notification_info_rect(i));
        bool is_detail = (i == 0);
        bool emphasized = (m->notification_preview == i);

        draw_plan_card(pixels, stride, card, false);
        if (emphasized) {
            draw_rect_outline(pixels, stride, card, 14, 2, is_detail ? UI_ACCENT : UI_WARNING_BORDER);
        }

        UiRect pill;
        const char *pill_txt;
        uint32_t pill_bg, pill_bd, pill_fg;
        if (is_detail) {
            bool preload_active = m->reason_notifications_enabled &&
                m->reason_overlay_preload_enabled && m->reason_preload_state == 1;
            pill = to_uirect(ptc_ui_notification_preload_rect());
            pill_txt = ptc_ui_text(preload_active ? PTC_UI_T_NOTICE_PRELOAD_ON : PTC_UI_T_NOTICE_PRELOAD_OFF);
            pill_bg = preload_active ? UI_SUCCESS_SOFT :
                      (m->reason_preload_supported ? UI_ACCENT_SOFT : UI_PAGE);
            pill_bd = preload_active ? UI_SUCCESS_BORDER :
                      (m->reason_preload_supported ? UI_ACCENT : UI_BORDER);
            pill_fg = preload_active ? UI_SUCCESS :
                      (m->reason_preload_supported ? UI_ACCENT : UI_MUTED);
        } else {
            pill_txt = ptc_ui_text(PTC_UI_T_NOTICE_CARD2_PILL);
            int pw = measure_text(pill_txt, 12) + 20;
            if (pw > 210) pw = 210;
            pill = (UiRect){card.x + card.width - 16 - pw, card.y + 10, pw, 24};
            pill_bg = UI_WARNING_SOFT;
            pill_bd = UI_WARNING_BORDER;
            pill_fg = UI_WARNING_TEXT;
        }
        fill_round_rect(pixels, stride, pill, 12, pill_bg);
        draw_rect_outline(pixels, stride, pill, 12, 1, pill_bd);
        fit_text(fitted, sizeof(fitted), pill_txt, 12, pill.width - 10);
        draw_text_center(pixels, stride, pill, fitted, 12, pill_fg);

        const char *card_title = ptc_ui_text(is_detail ? PTC_UI_T_NOTICE_CARD1_TITLE : PTC_UI_T_NOTICE_CARD2_TITLE);
        fit_text(fitted, sizeof(fitted), card_title, 16, pill.x - (card.x + 16) - 10);
        draw_text_bold(pixels, stride, card.x + 16, card.y + 28, fitted, 16, UI_INK);

        draw_notification_example(pixels, stride, screen, is_detail);

        draw_wrapped_text(pixels, stride, desc.x, desc.y + 16,
                          ptc_ui_text(is_detail ? PTC_UI_T_NOTICE_CARD1_DESC : PTC_UI_T_NOTICE_CARD2_DESC),
                          13, desc.width, 18, 2, UI_MUTED);
    }

    /* --- 3. Bottom one-click self-check & test strip --- */
    draw_notification_verification(pixels, stride, m);

    /* Footer safety reassurance */
    if (!m->message[0]) {
        const char *safe = ptc_ui_text(PTC_UI_T_NOTICE_FOOTER_SAFE);
        int sw = measure_text(safe, 14);
        draw_text(pixels, stride, 1226 - sw, 692, safe, 14, UI_MUTED);
    }
}

static void draw_upgrade_flow_overlay(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    UiRect dialog;
    PtcUiModel shell_model = *model;
    shell_model.overlay_title[0] = '\0';
    shell_model.overlay_body[0] = '\0';
    draw_dialog_shell(pixels, stride, &shell_model, &dialog, 860, 476);

    PtcUiUpgradeFlowStage stage = model->upgrade_flow_stage;
    bool busy = ptc_ui_upgrade_flow_busy(model);
    bool is_done = stage == PTC_UI_UPGRADE_FLOW_DONE;
    bool is_failed = stage == PTC_UI_UPGRADE_FLOW_FAILED;
    bool is_reconfirm = stage == PTC_UI_UPGRADE_FLOW_RECONFIRM_READY;
    bool is_grace = stage == PTC_UI_UPGRADE_FLOW_GRACE_WAIT;
    int64_t grace_sec = is_grace ? ptc_ui_setup_grace_remaining(model, ptc_ui_render_now()) : -1;

    /* 1. Header: semantic icon + bold title + right status pill */
    const char *title = ptc_ui_text(
        stage == PTC_UI_UPGRADE_FLOW_HOT_RELOAD || stage == PTC_UI_UPGRADE_FLOW_REFRESHING
            ? PTC_UI_T_UPGRADE_FLOW_TITLE_LOAD :
        is_reconfirm ? PTC_UI_T_UPGRADE_FLOW_TITLE_RECONFIRM :
        stage == PTC_UI_UPGRADE_FLOW_ACTIVATING || is_grace ? PTC_UI_T_UPGRADE_FLOW_TITLE_ACTIVATING :
        is_done ? PTC_UI_T_UPGRADE_FLOW_TITLE_DONE : PTC_UI_T_UPGRADE_FLOW_TITLE_FAILED);

    char badge_buf[48] = "";
    uint32_t tone_fg = UI_ACCENT;
    uint32_t tone_bg = UI_ACCENT_SOFT;
    uint32_t tone_bd = UI_ACCENT;
    int symbol_kind = 2; /* warning/info dot */

    if (is_done) {
        snprintf(badge_buf, sizeof(badge_buf), "%s", ptc_ui_text(PTC_UI_T_SUPPORT_HEALTH_OK_BADGE));
        tone_fg = UI_SUCCESS; tone_bg = UI_SUCCESS_SOFT; tone_bd = UI_SUCCESS_BORDER; symbol_kind = 1;
    } else if (is_failed) {
        snprintf(badge_buf, sizeof(badge_buf), "%s", ptc_ui_text(PTC_UI_T_SUPPORT_EVENT_ERR_BADGE));
        tone_fg = UI_DANGER; tone_bg = UI_DANGER_SOFT; tone_bd = UI_DANGER_BORDER; symbol_kind = 3;
    } else if (is_reconfirm) {
        snprintf(badge_buf, sizeof(badge_buf), "%s", ptc_ui_text(PTC_UI_T_RECONFIRM_BADGE));
        tone_fg = UI_WARNING_TEXT; tone_bg = UI_WARNING_SOFT; tone_bd = UI_WARNING_BORDER; symbol_kind = 2;
    } else if (is_grace) {
        if (grace_sec > 0) snprintf(badge_buf, sizeof(badge_buf), "%lld s", (long long)grace_sec);
        else snprintf(badge_buf, sizeof(badge_buf), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING));
        tone_fg = UI_WARNING_TEXT; tone_bg = UI_WARNING_SOFT; tone_bd = UI_WARNING_BORDER; symbol_kind = 2;
    } else if (stage == PTC_UI_UPGRADE_FLOW_HOT_RELOAD) {
        int sub = model->upgrade_flow_substep;
        if (sub < 1) sub = 1;
        if (sub > 4) sub = 4;
        snprintf(badge_buf, sizeof(badge_buf), "%d / 4", sub);
        tone_fg = UI_ACCENT; tone_bg = UI_ACCENT_SOFT; tone_bd = UI_ACCENT; symbol_kind = 2;
    } else if (stage == PTC_UI_UPGRADE_FLOW_REFRESHING) {
        snprintf(badge_buf, sizeof(badge_buf), "4 / 4");
        tone_fg = UI_ACCENT; tone_bg = UI_ACCENT_SOFT; tone_bd = UI_ACCENT; symbol_kind = 2;
    } else {
        snprintf(badge_buf, sizeof(badge_buf), "%s", ptc_ui_text(PTC_UI_T_ADJUST_BADGE_PENDING));
        tone_fg = UI_ACCENT; tone_bg = UI_ACCENT_SOFT; tone_bd = UI_ACCENT; symbol_kind = 2;
    }

    draw_status_symbol(pixels, stride, dialog.x + 50, dialog.y + 52,
                       is_reconfirm || is_grace ? UI_WARNING : tone_fg, symbol_kind);
    char fitted_badge[48];
    fit_text(fitted_badge, sizeof(fitted_badge), badge_buf, 13, 160);
    int bw = measure_text(fitted_badge, 13) + 24;
    if (bw < 68) bw = 68;
    UiRect bpill = {dialog.x + dialog.width - 34 - bw, dialog.y + 38, bw, 28};
    fill_round_rect(pixels, stride, bpill, 14, tone_bg);
    draw_rect_outline(pixels, stride, bpill, 14, 1, tone_bd);
    draw_text_center(pixels, stride, bpill, fitted_badge, 13, tone_fg);

    char fitted_title[128];
    int title_max = bpill.x - (dialog.x + 74) - 12;
    if (title_max < 200) title_max = 200;
    fit_text(fitted_title, sizeof(fitted_title), title, 24, title_max);
    draw_text_bold(pixels, stride, dialog.x + 74, dialog.y + 60, fitted_title, 24, UI_INK);

    /* 2. Stepper bar (3 steps when started from hot reload, 2 steps from direct Support recheck) */
    {
        UiRect area = {dialog.x + 34, dialog.y + 92, dialog.width - 68, 38};
        int count = model->upgrade_flow_from_hot_reload ? 3 : 2;
        const PtcUiTextId keys3[3] = {
            PTC_UI_T_UPGRADE_FLOW_STEP_1,
            PTC_UI_T_UPGRADE_FLOW_STEP_2,
            PTC_UI_T_UPGRADE_FLOW_STEP_3
        };
        const PtcUiTextId keys2[2] = {
            PTC_UI_T_UPGRADE_FLOW_STEP_2,
            PTC_UI_T_UPGRADE_FLOW_STEP_3
        };
        int active_step = 0;
        if (model->upgrade_flow_from_hot_reload) {
            if (stage == PTC_UI_UPGRADE_FLOW_HOT_RELOAD || stage == PTC_UI_UPGRADE_FLOW_REFRESHING) active_step = 0;
            else if (stage == PTC_UI_UPGRADE_FLOW_RECONFIRM_READY || stage == PTC_UI_UPGRADE_FLOW_ACTIVATING) active_step = 1;
            else if (stage == PTC_UI_UPGRADE_FLOW_GRACE_WAIT) active_step = 2;
            else if (stage == PTC_UI_UPGRADE_FLOW_DONE) active_step = 3;
            else active_step = (model->hot_reload_status == PTC_UI_HOT_RELOAD_SUCCESS ||
                                model->hot_reload_status == PTC_UI_HOT_RELOAD_CURRENT) ? 1 : 0;
        } else {
            if (stage == PTC_UI_UPGRADE_FLOW_ACTIVATING || stage == PTC_UI_UPGRADE_FLOW_RECONFIRM_READY) active_step = 0;
            else if (stage == PTC_UI_UPGRADE_FLOW_GRACE_WAIT) active_step = 1;
            else if (stage == PTC_UI_UPGRADE_FLOW_DONE) active_step = 2;
            else active_step = 0;
        }
        const int arrow_w = 24;
        int pill_w = (area.width - arrow_w * (count - 1)) / count;
        for (int i = 0; i < count; ++i) {
            UiRect pill = {area.x + i * (pill_w + arrow_w), area.y, pill_w, area.height};
            bool step_done = i < active_step;
            bool step_curr = i == active_step;
            uint32_t pbg = step_done ? UI_SUCCESS_SOFT :
                (step_curr ? (is_failed ? UI_DANGER_SOFT : (is_reconfirm || is_grace ? UI_WARNING_SOFT : UI_ACCENT_SOFT)) : UI_RAISED);
            uint32_t pbd = step_done ? UI_SUCCESS_BORDER :
                (step_curr ? (is_failed ? UI_DANGER_BORDER : (is_reconfirm || is_grace ? UI_WARNING_BORDER : UI_ACCENT)) : UI_BORDER);
            uint32_t pfg = step_done ? UI_SUCCESS :
                (step_curr ? (is_failed ? UI_DANGER : (is_reconfirm || is_grace ? UI_WARNING_TEXT : UI_ACCENT)) : UI_MUTED);
            fill_round_rect(pixels, stride, pill, 10, pbg);
            draw_rect_outline(pixels, stride, pill, 10, step_curr ? 2 : 1, pbd);
            char fitted_step[96];
            fit_text(fitted_step, sizeof(fitted_step),
                     ptc_ui_text(count == 3 ? keys3[i] : keys2[i]), 14, pill.width - 18);
            draw_text_center(pixels, stride, pill, fitted_step, 14, pfg);
            if (i < count - 1) {
                draw_text_center(pixels, stride,
                                 (UiRect){pill.x + pill.width, area.y, arrow_w, area.height},
                                 ">", 14, step_done ? UI_SUCCESS : UI_MUTED);
            }
        }
    }

    /* 3. Main status & progress card */
    {
        UiRect card = {dialog.x + 34, dialog.y + 148, dialog.width - 68, 224};
        uint32_t cbg = is_done ? UI_SUCCESS_SOFT :
            (is_failed ? UI_DANGER_SOFT : (is_reconfirm || is_grace ? UI_WARNING_SOFT : UI_PAGE));
        uint32_t cbd = is_done ? UI_SUCCESS_BORDER :
            (is_failed ? UI_DANGER_BORDER : (is_reconfirm || is_grace ? UI_WARNING_BORDER : UI_BORDER));
        fill_round_rect(pixels, stride, card, 14, cbg);
        draw_rect_outline(pixels, stride, card, 14, 1, cbd);

        char primary_msg[256];
        const char *secondary_msg = "";
        if (stage == PTC_UI_UPGRADE_FLOW_HOT_RELOAD) {
            snprintf(primary_msg, sizeof(primary_msg), "%s",
                     model->hot_reload_detail[0] ? model->hot_reload_detail :
                     (model->upgrade_flow_detail[0] ? model->upgrade_flow_detail : ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_TITLE_LOAD)));
            secondary_msg = ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_WAIT_LOCK_HINT);
        } else if (stage == PTC_UI_UPGRADE_FLOW_REFRESHING) {
            snprintf(primary_msg, sizeof(primary_msg), "%s", ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_REFRESHING_STATUS));
            secondary_msg = ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_WAIT_LOCK_HINT);
        } else if (is_reconfirm) {
            snprintf(primary_msg, sizeof(primary_msg), "%s", ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_RECONFIRM_BODY));
            secondary_msg = ptc_ui_text(PTC_UI_T_RECONFIRM_NOTICE_DETAILS);
        } else if (stage == PTC_UI_UPGRADE_FLOW_ACTIVATING) {
            snprintf(primary_msg, sizeof(primary_msg), "%s",
                     ptc_ui_text(model->upgrade_flow_reconfirm_context
                         ? PTC_UI_T_RECONFIRM_SUBMITTING : PTC_UI_T_COMPLETING_FIRST_TIME_SETUP));
            secondary_msg = ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_WAIT_LOCK_HINT);
        } else if (is_grace) {
            if (grace_sec > 0) {
                snprintf(primary_msg, sizeof(primary_msg),
                         ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_GRACE_COUNTDOWN), (long long)grace_sec);
            } else {
                snprintf(primary_msg, sizeof(primary_msg), "%s", ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_GRACE_FINALIZING));
            }
            secondary_msg = ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_WAIT_LOCK_HINT);
        } else if (is_done) {
            snprintf(primary_msg, sizeof(primary_msg), "%s",
                     model->upgrade_flow_detail[0] ? model->upgrade_flow_detail : ptc_ui_text(PTC_UI_T_RECONFIRM_COMPLETED_MSG));
            secondary_msg = ptc_ui_text(PTC_UI_T_SETUP_CHECK_NOTICE);
        } else {
            snprintf(primary_msg, sizeof(primary_msg), "%s",
                     model->upgrade_flow_detail[0] ? model->upgrade_flow_detail :
                     (model->message[0] ? model->message : ptc_ui_text(PTC_UI_T_OPERATION_INCOMPLETE)));
            secondary_msg = ptc_ui_text(PTC_UI_T_PLEASE_GO_TO_SUPPORT_AND_RECOVERY_TO);
        }

        draw_wrapped_text(pixels, stride, card.x + 24, card.y + 42, primary_msg, 19,
                          card.width - 48, 28, 2, is_failed ? UI_DANGER : UI_INK);

        if (busy) {
            int pct = 20;
            if (stage == PTC_UI_UPGRADE_FLOW_HOT_RELOAD) {
                int sub = model->upgrade_flow_substep;
                pct = sub <= 1 ? 22 : (sub == 2 ? 46 : (sub == 3 ? 72 : 90));
            } else if (stage == PTC_UI_UPGRADE_FLOW_REFRESHING) {
                pct = 94;
            } else if (stage == PTC_UI_UPGRADE_FLOW_ACTIVATING) {
                pct = 38;
            } else if (is_grace) {
                if (grace_sec >= 5) pct = 54;
                else if (grace_sec > 0) pct = 54 + (int)(5 - grace_sec) * 9;
                else pct = 95;
            }
            UiRect track = {card.x + 24, card.y + 114, card.width - 48, 16};
            fill_round_rect(pixels, stride, track, 8, UI_GAUGE_SLOT);
            draw_rect_outline(pixels, stride, track, 8, 1, UI_GAUGE_SLOT_BORDER);
            int fill_w = track.width * pct / 100;
            if (fill_w < 16) fill_w = 16;
            if (fill_w > track.width) fill_w = track.width;
            fill_round_rect(pixels, stride, (UiRect){track.x, track.y, fill_w, track.height},
                            8, is_grace ? UI_WARNING : UI_ACCENT);
            draw_wrapped_text(pixels, stride, card.x + 24, card.y + 162, secondary_msg, 14,
                              card.width - 48, 20, 2, is_grace ? UI_WARNING_TEXT : UI_MUTED);
        } else {
            draw_wrapped_text(pixels, stride, card.x + 24, card.y + 122, secondary_msg, 15,
                              card.width - 48, 22, 3, is_done ? UI_SUCCESS : (is_reconfirm ? UI_WARNING_TEXT : UI_MUTED));
        }
    }

    /* 4. Bottom action area: locked badge while busy, clear buttons when actionable */
    if (busy) {
        UiRect lock_pill = {dialog.x + (dialog.width - 360) / 2, dialog.y + dialog.height - 82, 360, 52};
        fill_round_rect(pixels, stride, lock_pill, 12, UI_RAISED);
        draw_rect_outline(pixels, stride, lock_pill, 12, 1, UI_BORDER);
        char fitted_wait[96];
        fit_text(fitted_wait, sizeof(fitted_wait), ptc_ui_text(PTC_UI_T_THE_BACKGROUND_IS_BEING_PROCESSED_PLEASE_WAIT),
                 16, lock_pill.width - 28);
        draw_text_center(pixels, stride, lock_pill, fitted_wait, 16, UI_MUTED);
    } else if (is_reconfirm) {
        draw_dialog_button(pixels, stride, ptc_ui_cancel_rect(model->overlay),
                           ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_BTN_TO_SUPPORT), UI_RAISED, UI_INK, true);
        draw_dialog_button(pixels, stride, ptc_ui_confirm_rect(model->overlay),
                           ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_BTN_RECONFIRM), UI_ACCENT, UI_ON_ACCENT, false);
    } else if (is_done) {
        draw_dialog_button(pixels, stride, ptc_ui_upgrade_flow_close_rect(),
                           ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_BTN_DONE), UI_SUCCESS, UI_ON_ACCENT, false);
    } else {
        draw_dialog_button(pixels, stride, ptc_ui_upgrade_flow_close_rect(),
                           ptc_ui_text(PTC_UI_T_UPGRADE_FLOW_BTN_CLOSE_SUPPORT), UI_ACCENT, UI_ON_ACCENT, false);
    }
}

bool draw_support_overlay_surface(uint32_t *pixels, uint32_t stride, const PtcUiModel *model)
{
    switch (model->overlay) {
    case PTC_UI_OVERLAY_UPGRADE_FLOW:
        draw_upgrade_flow_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_NOTIFICATIONS:
        draw_notification_preferences(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SUPPORT_GUIDE:
        draw_support_guide(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SETUP_PCTL_HELP:
        draw_setup_pctl_help(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SHORTCUT_MANAGER:
        draw_shortcut_manager_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SOFTWARE_INFO:
        draw_software_info_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_ONLINE_UPDATE:
    case PTC_UI_OVERLAY_UPDATE_BACKUPS:
        draw_online_update(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_SUPPORT_EVENT:
        draw_support_event_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_ALBUM_MANAGER:
        draw_album_manager_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_THEME:
        draw_theme_overlay(pixels, stride, model);
        return true;
    case PTC_UI_OVERLAY_LANGUAGE:
        draw_language_overlay(pixels, stride, model);
        return true;
    default:
        return false;
    }
}
