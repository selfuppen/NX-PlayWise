// Included in the production GUI and host preview. All interactive geometry is
// published in page_layout_; input never recreates measured content positions.
    PtcOverlayPageLayout page_layout_{};
    float page_scroll_ = 0, page_scroll_target_ = 0;
    int page_identity_ = -1;
    bool page_changed_ = false;
    int flow_y_ = 0;
    OverlayNavbarScheme navbar_scheme_ = OverlayNavbarScheme::Default;

    bool child_is_restricted() const
    {
        int day, second;
        return child_snapshot_usable(displayed_summary_, day, second) &&
            (bedtime_restricted() || eye_care_restricted() || displayed_summary_.dock_restriction_active);
    }

    int page_identity() const
    {
        if (language_confirm_active_) return 6;
        if (access_recovery_visible()) return 10 + static_cast<int>(parent_view_);
        if (success_visible_) return 2;
        if (preview_ready_) return 3;
        if (status_expanded_) return 4;
        if (child_is_restricted()) return bedtime_restricted() ? 20 : eye_care_restricted() ? 21 : 22;
        if (is_keypad_mode()) return 5;
        return 0;
    }

    void reset_page_scroll()
    {
        page_identity_ = -1;
        page_scroll_ = page_scroll_target_ = 0;
    }

    void prepare_page()
    {
        const int identity = page_identity();
        page_changed_ = identity != page_identity_;
        if (page_changed_) {
            page_scroll_ = page_scroll_target_ = 0;
            page_identity_ = identity;
        }
        page_layout_ = {};
    }

    void draw_focus_ring(tsl::gfx::Renderer *renderer, PtcOverlayRect rect, tsl::Color color)
    {
        if (rect.w <= 4 || rect.h <= 4) return;
        renderer->drawRect(rect.x + 1, rect.y + 1, rect.w - 2, 2, renderer->a(color));
        renderer->drawRect(rect.x + 1, rect.y + rect.h - 3, rect.w - 2, 2, renderer->a(color));
        renderer->drawRect(rect.x + 1, rect.y + 1, 2, rect.h - 2, renderer->a(color));
        renderer->drawRect(rect.x + rect.w - 3, rect.y + 1, 2, rect.h - 2, renderer->a(color));
    }

    void draw_button(tsl::gfx::Renderer *renderer, PtcOverlayRect rect,
        const char *text, tsl::Color color, bool emphasized = false, int size = PTC_OVERLAY_FONT_BTN, bool focused = false)
    {
        const auto &pal = current_palette();
        const bool danger = color.r == pal.danger.r && color.g == pal.danger.g && color.b == pal.danger.b;
        const bool warning = color.r == pal.warning.r && color.g == pal.warning.g && color.b == pal.warning.b;
        const auto tone = focused ? pal.accent : color;
        const auto soft = danger ? pal.danger_soft : warning ? pal.warning_soft : pal.accent_soft;
        const bool emp = emphasized || focused;
        draw_dashboard_surface(renderer, rect, emp ? soft : pal.card_raised,
            focused ? pal.accent : emp ? tone : pal.border_control, emp);
        if (focused) {
            draw_focus_ring(renderer, rect, pal.accent);
        }
        const int height = overlay_text_height(renderer, text, rect.w - 24, size);
        overlay_text(renderer, text, rect.x + 12, rect.y + (rect.h - height) / 2,
            rect.w - 24, size, focused ? pal.accent : emp ? color : color, true);
    }

    void begin_page(tsl::gfx::Renderer *renderer, int cx, int cy, int cw,
        const char *title, const PtcFlowPath &flow, bool refresh = false,
        int title_size = PTC_OVERLAY_FONT_TITLE)
    {
        (void)title_size; // 方案 A: the path bar carries page identity; kept for scheme B experiments.
        const auto &pal = current_palette();
        if (navbar_scheme_ != OverlayNavbarScheme::Default) {
            const char *category = ptc_ui_text(PTC_UI_T_PLAYWISE);
            const char *breadcrumb = "\u4efb\u6211\u73a9 › \u4eca\u65e5\u6982\u89c8";
            const char *display_title = title;
            int step_index = 0, total_steps = 1;
            const char *step_names[4] = {"\u4eca\u65e5\u6982\u89c8", "", "", ""};
            tsl::Color cat_color = pal.accent;
            tsl::Color cat_soft = pal.accent_soft;

            const bool is_child_overview = !access_recovery_visible() && !preview_ready_ && !success_visible_ && !status_expanded_ && !is_keypad_mode();
            const bool parent_in_header = is_child_overview;

            if (access_recovery_visible()) {
                category = "\u5bb6\u957f\u533a";
                cat_color = pal.warning;
                cat_soft = pal.warning_soft;
                if (parent_view_ == ParentView::Pin) {
                    breadcrumb = "\u9996\u9875 › \u5bb6\u957f\u533a › PIN\u9a8c\u8bc1";
                    display_title = "\u5bb6\u957f\u8eab\u4efd\u9a8c\u8bc1";
                    step_index = 0; total_steps = 3;
                    step_names[0] = "1.\u9a8c\u8bc1PIN"; step_names[1] = "2.\u7ba1\u7406\u83dc\u5355"; step_names[2] = "3.\u6267\u884c\u786e\u8ba4";
                } else if (parent_view_ == ParentView::Actions) {
                    breadcrumb = "\u9996\u9875 › \u5bb6\u957f\u533a › \u7ba1\u7406\u83dc\u5355";
                    display_title = "\u5feb\u6377\u7ba1\u7406\u4e0e\u63a7\u5236";
                    step_index = 1; total_steps = 3;
                    step_names[0] = "1.\u9a8c\u8bc1PIN"; step_names[1] = "2.\u7ba1\u7406\u83dc\u5355"; step_names[2] = "3.\u6267\u884c\u786e\u8ba4";
                } else if (parent_view_ == ParentView::Confirm) {
                    breadcrumb = "\u9996\u9875 › \u5bb6\u957f\u533a › \u64cd\u4f5c\u786e\u8ba4";
                    display_title = "\u64cd\u4f5c\u786e\u8ba4";
                    step_index = 2; total_steps = 3;
                    step_names[0] = "1.\u9a8c\u8bc1PIN"; step_names[1] = "2.\u7ba1\u7406\u83dc\u5355"; step_names[2] = "3.\u6267\u884c\u786e\u8ba4";
                } else {
                    breadcrumb = "\u9996\u9875 › \u5bb6\u957f\u533a › \u6267\u884c\u7ed3\u679c";
                    display_title = "\u64cd\u4f5c\u7ed3\u679c";
                    step_index = 2; total_steps = 3;
                    step_names[0] = "1.\u9a8c\u8bc1PIN"; step_names[1] = "2.\u7ba1\u7406\u83dc\u5355"; step_names[2] = "3.\u6267\u884c\u786e\u8ba4";
                }
            } else if (preview_ready_) {
                category = "\u52a0\u65f6\u7801";
                cat_color = pal.success;
                cat_soft = pal.success_soft;
                breadcrumb = "\u9996\u9875 › \u5151\u6362\u52a0\u65f6 › \u9884\u89c8\u786e\u8ba4";
                display_title = "\u786e\u8ba4\u589e\u52a0\u6e38\u73a9\u65f6\u95f4";
                step_index = 1; total_steps = 3;
                step_names[0] = "1.\u8f93\u5165\u7801"; step_names[1] = "2.\u9884\u89c8\u6548\u679c"; step_names[2] = "3.\u5b8c\u6210";
            } else if (success_visible_) {
                category = "\u52a0\u65f6\u7801";
                cat_color = pal.success;
                cat_soft = pal.success_soft;
                breadcrumb = "\u9996\u9875 › \u5151\u6362\u52a0\u65f6 › \u5151\u6362\u7ed3\u679c";
                display_title = "\u5151\u6362\u7ed3\u679c";
                step_index = 2; total_steps = 3;
                step_names[0] = "1.\u8f93\u5165\u7801"; step_names[1] = "2.\u9884\u89c8\u6548\u679c"; step_names[2] = "3.\u5b8c\u6210";
            } else if (status_expanded_) {
                category = "\u8be6\u60c5";
                cat_color = pal.text_secondary;
                cat_soft = pal.card_raised;
                breadcrumb = "\u9996\u9875 › \u4eca\u65e5\u6982\u89c8 › \u89c4\u5219\u4e0e\u72b6\u6001\u8be6\u60c5";
                display_title = "\u6838\u5fc3\u72b6\u6001\u4e0e\u63a7\u5236\u89c4\u5219";
                step_index = 1; total_steps = 2;
                step_names[0] = "\u4eca\u65e5\u6982\u89c8"; step_names[1] = "\u72b6\u6001\u4e0e\u89c4\u5219\u8be6\u60c5";
            } else if (is_keypad_mode()) {
                category = "\u52a0\u65f6\u7801";
                cat_color = pal.success;
                cat_soft = pal.success_soft;
                breadcrumb = "\u9996\u9875 › \u5151\u6362\u52a0\u65f6 › \u8f93\u5165\u7801";
                display_title = "\u8f93\u5165 8 \u4f4d\u52a0\u65f6\u7801";
                step_index = 0; total_steps = 3;
                step_names[0] = "1.\u8f93\u5165\u7801"; step_names[1] = "2.\u9884\u89c8\u6548\u679c"; step_names[2] = "3.\u5b8c\u6210";
            } else {
                const bool restricted = child_is_restricted();
                category = "\u6982\u89c8";
                cat_color = pal.accent;
                cat_soft = pal.accent_soft;
                breadcrumb = restricted ? "\u4efb\u6211\u73a9 › \u4f11\u606f\u63d0\u9192" : "\u4efb\u6211\u73a9 › \u4eca\u65e5\u6982\u89c8";
                display_title = ptc_ui_text(PTC_UI_T_PLAYWISE);
                step_index = 0; total_steps = 2;
                step_names[0] = "\u4eca\u65e5\u6982\u89c8"; step_names[1] = "\u72b6\u6001\u4e0e\u89c4\u5219\u8be6\u60c5";
            }

            const bool restricted = child_is_restricted();
            const bool parent_focused = !status_expanded_ && !is_keypad_mode() &&
                parent_view_ == ParentView::Child &&
                child_overview_focus_ == (restricted ? 0 : 2);
            const bool refresh_focused = (parent_view_ == ParentView::Actions && parent_action_ == -1) ||
                (status_expanded_ && status_details_focus_ == 2) ||
                (!status_expanded_ && !is_keypad_mode() && parent_view_ == ParentView::Child &&
                 child_overview_focus_ == (restricted ? 3 : 4));

            if (navbar_scheme_ == OverlayNavbarScheme::Breadcrumb) {
                renderer->drawString(breadcrumb, false, cx + 6, cy + 16, 20, renderer->a(pal.text_secondary));
                int title_w = parent_in_header ? cw - 224 : (refresh ? cw - 124 : cw - 16);
                overlay_text(renderer, display_title, cx + 6, cy + 24, title_w, 30, pal.text_primary);

                if (parent_in_header) {
                    page_layout_.parent = ptc_overlay_rect(cx + cw - 216, cy + 4, 102, 50);
                    draw_button(renderer, page_layout_.parent, ptc_ui_text(PTC_UI_T_R_PARENT_AREA),
                        pal.accent, restricted, PTC_OVERLAY_FONT_SECONDARY, parent_focused);
                }
                if (refresh) {
                    page_layout_.refresh = ptc_overlay_rect(cx + cw - 108, cy + 4, 108, 50);
                    draw_button(renderer, page_layout_.refresh, ptc_ui_text(PTC_UI_T_Y_REFRESH),
                        pal.accent, false, PTC_OVERLAY_FONT_SECONDARY, refresh_focused);
                }

                renderer->drawRect(cx + 4, cy + 62, cw - 8, 1, renderer->a(pal.border_decorative));
                renderer->drawRect(cx + 4, cy + 62, 36, 2, renderer->a(cat_color));

                page_layout_.viewport = ptc_overlay_rect(cx, cy + 70, cw, 544 - 70);
            } else if (navbar_scheme_ == OverlayNavbarScheme::Capsule) {
                auto badge_size = measure_localized(renderer, category, 20);
                int badge_w = static_cast<int>(badge_size.first) + 18;
                int badge_h = 32;
                int badge_x = cx + 6;
                int badge_y = cy + 10;

                draw_round_rect_surface(renderer, badge_x, badge_y, badge_w, badge_h, 4,
                    cat_soft, cat_color, 1);
                draw_localized_center(renderer, category, badge_x, badge_y, badge_w, badge_h, 20, cat_color);

                int title_x = badge_x + badge_w + 10;
                int title_w = parent_in_header ? (cx + cw - 220 - title_x) : (refresh ? (cx + cw - 116 - title_x) : (cw - title_x));
                overlay_text(renderer, display_title, title_x, cy + 8, title_w, 28, pal.text_primary);

                if (parent_in_header) {
                    page_layout_.parent = ptc_overlay_rect(cx + cw - 216, cy + 2, 102, 48);
                    draw_button(renderer, page_layout_.parent, ptc_ui_text(PTC_UI_T_R_PARENT_AREA),
                        pal.accent, restricted, PTC_OVERLAY_FONT_SECONDARY, parent_focused);
                }
                if (refresh) {
                    page_layout_.refresh = ptc_overlay_rect(cx + cw - 108, cy + 2, 108, 48);
                    draw_button(renderer, page_layout_.refresh, ptc_ui_text(PTC_UI_T_Y_REFRESH),
                        pal.accent, false, PTC_OVERLAY_FONT_SECONDARY, refresh_focused);
                }

                renderer->drawRect(cx + 4, cy + 54, cw - 8, 1, renderer->a(pal.border_decorative));
                renderer->drawRect(cx + 4, cy + 54, 36, 2, renderer->a(cat_color));

                page_layout_.viewport = ptc_overlay_rect(cx, cy + 62, cw, 544 - 62);
            } else if (navbar_scheme_ == OverlayNavbarScheme::Stepper) {
                int title_w = parent_in_header ? cw - 224 : (refresh ? cw - 124 : cw - 16);
                overlay_text(renderer, display_title, cx + 6, cy + 2, title_w, 28, pal.text_primary);

                if (parent_in_header) {
                    page_layout_.parent = ptc_overlay_rect(cx + cw - 216, cy + 2, 102, 42);
                    draw_button(renderer, page_layout_.parent, ptc_ui_text(PTC_UI_T_R_PARENT_AREA),
                        pal.accent, restricted, PTC_OVERLAY_FONT_CAPTION, parent_focused);
                }
                if (refresh) {
                    page_layout_.refresh = ptc_overlay_rect(cx + cw - 108, cy + 2, 108, 42);
                    draw_button(renderer, page_layout_.refresh, ptc_ui_text(PTC_UI_T_Y_REFRESH),
                        pal.accent, false, PTC_OVERLAY_FONT_CAPTION, refresh_focused);
                }

                int step_y = cy + 46;
                int step_x = cx + 6;
                int step_w = (cw - 12) / total_steps;
                for (int i = 0; i < total_steps; ++i) {
                    bool active = (i == step_index);
                    bool done = (i < step_index);
                    tsl::Color step_col = active ? pal.accent : done ? pal.success : pal.text_disabled;

                    int dot_x = step_x + i * step_w + 4;
                    int dot_y = step_y + 4;
                    if (active) {
                        renderer->drawRect(dot_x - 1, dot_y - 1, 8, 8, renderer->a(pal.accent_soft));
                        renderer->drawRect(dot_x, dot_y, 6, 6, renderer->a(pal.accent));
                    } else if (done) {
                        renderer->drawRect(dot_x, dot_y, 6, 6, renderer->a(pal.success));
                    } else {
                        renderer->drawRect(dot_x, dot_y, 6, 6, renderer->a(pal.border_control));
                    }

                    char step_buf[64];
                    std::snprintf(step_buf, sizeof(step_buf), "%s", step_names[i]);
                    renderer->drawString(step_buf, false, dot_x + 10, step_y + 12, 18, renderer->a(step_col));

                    if (i < total_steps - 1) {
                        int line_start = dot_x + 10 + static_cast<int>(measure_localized(renderer, step_buf, 18).first) + 6;
                        int line_end = step_x + (i + 1) * step_w - 4;
                        if (line_end > line_start) {
                            renderer->drawRect(line_start, dot_y + 2, line_end - line_start, 1,
                                renderer->a(done ? pal.success : pal.border_decorative));
                        }
                    }
                }

                renderer->drawRect(cx + 4, cy + 70, cw - 8, 1, renderer->a(pal.border_decorative));
                renderer->drawRect(cx + 4, cy + 70, 36, 2, renderer->a(cat_color));

                page_layout_.viewport = ptc_overlay_rect(cx, cy + 76, cw, 544 - 76);
            }

            page_layout_.secondary = ptc_overlay_rect(cx, cy + 554, 154, 76);
            page_layout_.primary = ptc_overlay_rect(cx + 166, cy + 554, cw - 166, 76);
            flow_y_ = page_layout_.viewport.y;
            renderer->enableScissoring(cx, page_layout_.viewport.y, cw, page_layout_.viewport.h);
            return;
        }
        // 方案 A：所有非儿童根页面共用顶部单行路径栏，页头不再画 34px 标题。
        const int path_width = refresh ? cw - 156 : cw - 32;
        draw_flow_bar(renderer, cx + PTC_OVERLAY_PATH_X, cy + PTC_OVERLAY_PATH_Y,
            path_width, flow);
        renderer->drawRect(cx + 4, cy + PTC_OVERLAY_HEADER_H + 2, cw - 8, 1,
            renderer->a(pal.border_decorative));
        renderer->drawRect(cx + 4, cy + PTC_OVERLAY_HEADER_H + 2, 32, 2,
            renderer->a(pal.accent));
        if (refresh) {
            page_layout_.refresh = ptc_overlay_rect(cx + cw - 140, cy + 2, 136, 40);
            const bool highlight = needs_refresh() && !bridge_->waiting;
            const bool restricted = child_is_restricted();
            const bool refresh_focused = (parent_view_ == ParentView::Actions && parent_action_ == -1) ||
                (status_expanded_ && status_details_focus_ == 2) ||
                (!status_expanded_ && !is_keypad_mode() && parent_view_ == ParentView::Child &&
                 child_overview_focus_ == (restricted ? 3 : 4));
            draw_button(renderer, page_layout_.refresh,
                ptc_ui_text(bridge_->waiting ? PTC_UI_T_REFRESHING : PTC_UI_T_Y_REFRESH),
                bridge_->waiting ? pal.text_secondary : highlight ? pal.warning : pal.accent,
                highlight || bridge_->waiting, PTC_OVERLAY_FONT_CAPTION, refresh_focused);
        }
        page_layout_.viewport = ptc_overlay_rect(cx, cy + PTC_OVERLAY_HEADER_H + 8, cw,
            PTC_OVERLAY_VIEWPORT_BOTTOM - (PTC_OVERLAY_HEADER_H + 8));
        page_layout_.secondary = ptc_overlay_rect(cx, cy + PTC_OVERLAY_RAIL_Y,
            154, PTC_OVERLAY_RAIL_H);
        page_layout_.primary = ptc_overlay_rect(cx + 166, cy + PTC_OVERLAY_RAIL_Y,
            cw - 166, PTC_OVERLAY_RAIL_H);
        flow_y_ = page_layout_.viewport.y;
        renderer->enableScissoring(cx, page_layout_.viewport.y, cw, page_layout_.viewport.h);
    }

    int flow_text(tsl::gfx::Renderer *renderer, const char *text, int size,
        tsl::Color color, int gap = 12)
    {
        const auto &vp = page_layout_.viewport;
        const int height = overlay_text(renderer, text, vp.x + 16,
            flow_y_ - static_cast<int>(page_scroll_), vp.w - 36, size, color);
        flow_y_ += height + ((parent_view_ == ParentView::Child || parent_view_ == ParentView::Pin) ? std::min(gap, 6) : gap);
        return height;
    }

    // Section header shared by the scrolling pages: colour tick, measured label
    // and a hairline that starts after the text instead of running under it.
    void flow_section(tsl::gfx::Renderer *renderer, const char *label,
        tsl::Color color, int gap_before = 14)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int line_h = (PTC_OVERLAY_FONT_CAPTION * 6 + 4) / 5;
        flow_y_ += gap_before;
        const int y = flow_y_ - static_cast<int>(page_scroll_);
        const int label_w = text_width(renderer, label, PTC_OVERLAY_FONT_CAPTION);
        renderer->drawRect(vp.x + 4, y + 4, 4, PTC_OVERLAY_FONT_CAPTION - 4, renderer->a(color));
        overlay_text(renderer, label, vp.x + 16, y, vp.w - 24, PTC_OVERLAY_FONT_CAPTION, color);
        const int rule_x = vp.x + 16 + label_w + 12;
        if (rule_x < vp.x + vp.w - 8)
            renderer->drawRect(rule_x, y + line_h / 2, vp.x + vp.w - 8 - rule_x, 1,
                renderer->a(pal.border_decorative));
        flow_y_ += line_h + 6;
    }

    PtcOverlayRect flow_card(tsl::gfx::Renderer *renderer, const char *label,
        const char *value, const char *note, int value_size, tsl::Color color,
        tsl::Color tone, PtcOverlayParentAction action, bool highlighted = false, bool disabled = false,
        int label_size = PTC_OVERLAY_FONT_BODY)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int raw_w = vp.w - 12;
        const int content_w = raw_w - 28;
        const int icon_space = 30;
        const int text_w = content_w - icon_space;
        const int text_x = (vp.x + 4) + 14 + icon_space;

        const int first_line_h = (label_size * 6 + 4) / 5;
        const int label_w = static_cast<int>(measure_localized(renderer, label, label_size).first);
        const int label_h = overlay_text_height(renderer, label, text_w, label_size);

        const bool has_value = (value != nullptr && value[0] != '\0');
        const int value_w = has_value ? static_cast<int>(measure_localized(renderer, value, value_size).first) : 0;
        const int value_h = has_value ? overlay_text_height(renderer, value, text_w, value_size) : 0;
        const bool inline_value = has_value && (label_w + value_w + 16 <= text_w);

        const int row0_h = inline_value ? std::max(label_h, value_h) : (has_value ? (label_h + 4 + value_h) : label_h);

        const bool has_note = (note != nullptr && note[0] != '\0');
        const int note_h = has_note ? overlay_text_height(renderer, note, text_w, PTC_OVERLAY_FONT_SECONDARY) : 0;

        const int content_h = row0_h + (has_note ? (4 + note_h) : 0);
        const int height = std::max(static_cast<int>(PTC_OVERLAY_PARENT_CARD_MIN_H), content_h + 16);

        const auto raw = ptc_overlay_rect(vp.x + 4, flow_y_, raw_w, height);
        const int y = raw.y - static_cast<int>(page_scroll_);
        flow_y_ += height + 6;

        // Still measure hidden cards for scrolling, but avoid rasterizing their
        // glyphs into the bounded Tesla cache when the entire card is clipped.
        if (y + height <= vp.y || y >= vp.y + vp.h) return raw;

        draw_dashboard_surface(renderer, ptc_overlay_rect(raw.x, y, raw.w, raw.h),
            highlighted ? dashboard_soft(tone) : tsl::Color{1, 2, 3, 15},
            highlighted ? tone : pal.border_decorative, highlighted);
        renderer->drawRect(raw.x, y + 6, disabled ? 2 : 3, height - 12, renderer->a(tone));

        const int content_top = y + (height - content_h) / 2;
        const int label_y = content_top + (inline_value ? (row0_h - label_h) / 2 : 0);

        // Icon aligns with the first line of the title row
        const int icon_y = label_y + (first_line_h - 22) / 2;
        draw_parent_action_icon(renderer, action, raw.x + 14, icon_y, disabled ? pal.text_disabled : tone);

        overlay_text(renderer, label, text_x, label_y, inline_value ? (label_w + 1) : text_w, label_size,
            disabled ? pal.text_disabled : pal.text_primary);

        if (has_value) {
            if (inline_value) {
                const int val_x = raw.x + raw.w - 14 - value_w;
                const int val_y = content_top + (row0_h - value_h) / 2;
                overlay_text(renderer, value, val_x, val_y, value_w + 1, value_size, color);
            } else {
                const int val_y = label_y + label_h + 4;
                overlay_text(renderer, value, text_x, val_y, text_w, value_size, color);
            }
        }

        if (has_note) {
            const int note_y = content_top + row0_h + 4;
            overlay_text(renderer, note, text_x, note_y, text_w, PTC_OVERLAY_FONT_SECONDARY,
                disabled ? pal.text_disabled : pal.text_secondary);
        }

        return raw;
    }

    // Child pages use measured rows instead of the parent's 104px minimum.
    // Stack only when the two measured texts cannot share a line safely.
    PtcOverlayRect child_card(tsl::gfx::Renderer *renderer, const char *label,
        const char *value, const char *note, int value_size, tsl::Color color,
        int label_size = PTC_OVERLAY_FONT_BODY_LG, ChildCardKind kind = ChildCardKind::Plain, bool focused = false)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int width = vp.w - 36;
        const int label_w = static_cast<int>(measure_localized(renderer, label, label_size).first);
        const int value_w = static_cast<int>(measure_localized(renderer, value, value_size).first);
        const bool inline_value = label_w + value_w + 12 <= width;
        const bool rule = kind == ChildCardKind::Eye || kind == ChildCardKind::Bedtime || kind == ChildCardKind::Dock;
        // Decorative icons must never force an extra line or reduce note width.
        const int icon_space = rule && inline_value && label_w + value_w + 12 + 30 <= width ? 30 : 0;
        const int label_h = overlay_text_height(renderer, label, width, label_size);
        const int value_h = overlay_text_height(renderer, value, width, value_size);
        const int note_h = note && *note ? overlay_text_height(renderer, note, width, PTC_OVERLAY_FONT_SECONDARY) : 0;
        const int content_h = inline_value ? std::max(label_h, value_h) : label_h + value_h + 2;
        const int height = 16 + content_h + (note_h ? note_h + 2 : 0);
        const auto raw = ptc_overlay_rect(vp.x + 4, flow_y_, vp.w - 8, height);
        const int y = raw.y - static_cast<int>(page_scroll_);
        flow_y_ += height + 6;
        if (y + height <= vp.y || y >= vp.y + vp.h) return raw;
        const auto surface = focused ? pal.accent_soft : kind == ChildCardKind::Time ? tsl::Color{1, 2, 3, 15} : pal.card_bg;
        draw_dashboard_surface(renderer, ptc_overlay_rect(raw.x, y, raw.w, raw.h), surface,
            focused ? pal.accent : pal.border_decorative, focused);
        if (focused) {
            draw_focus_ring(renderer, ptc_overlay_rect(raw.x, y, raw.w, raw.h), pal.accent);
        }
        if (kind == ChildCardKind::Time) {
            // A bounded pattern in the bottom padding leaves all text unobscured.
            for (int i = 0; i < 7; ++i)
                renderer->drawRect(raw.x + raw.w - 66 + i * 8, y + height - 5, 3, 2, renderer->a(pal.border_decorative));
        }
        renderer->drawRect(raw.x, y + 8, 3, height - 16,
            renderer->a(rule ? color : kind == ChildCardKind::Time ? pal.accent : pal.border_decorative));
        if (icon_space) draw_child_icon(renderer, kind, raw.x + 14, y + 8 + (content_h - 22) / 2, pal.accent);
        if (inline_value) {
            const int top = y + 8;
            overlay_text(renderer, label, raw.x + 14 + icon_space, top + (content_h - label_h) / 2,
                label_w + 1, label_size, pal.text_primary);
            overlay_text(renderer, value, raw.x + raw.w - 14 - value_w,
                top + (content_h - value_h) / 2, value_w + 1, value_size, color);
        } else {
            overlay_text(renderer, label, raw.x + 14, y + 8, width, label_size, pal.text_primary);
            overlay_text(renderer, value, raw.x + 14, y + 10 + label_h, width, value_size, color);
        }
        if (note_h) overlay_text(renderer, note, raw.x + 14, y + 10 + content_h,
            width, PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary);
        return raw;
    }

    PtcOverlayRect child_rule(tsl::gfx::Renderer *renderer, const char *label,
        const char *value, const char *note, tsl::Color color, ChildCardKind kind)
    {
        return child_card(renderer, label, value, note, PTC_OVERLAY_FONT_BODY,
            color, PTC_OVERLAY_FONT_BODY_LG, kind, false);
    }

    void end_page(tsl::gfx::Renderer *renderer)
    {
        renderer->disableScissoring();
        const auto &vp = page_layout_.viewport;
        page_layout_.scroll_max = ptc_overlay_scroll_max(flow_y_ - vp.y, vp.h);
        page_scroll_target_ = std::clamp(page_scroll_target_, 0.0f, static_cast<float>(page_layout_.scroll_max));
        page_scroll_ = std::clamp(page_scroll_, 0.0f, static_cast<float>(page_layout_.scroll_max));
        if (page_layout_.scroll_max > 0) {
            const auto &pal = current_palette();
            const int h = std::max(24, vp.h * vp.h / (vp.h + page_layout_.scroll_max));
            const int y = vp.y + (vp.h - h) * static_cast<int>(page_scroll_) / page_layout_.scroll_max;
            renderer->drawRect(vp.x + vp.w - 3, vp.y, 3, vp.h, renderer->a(pal.border_decorative));
            renderer->drawRect(vp.x + vp.w - 3, y, 3, h, renderer->a(pal.accent));
        }
    }

    void draw_hold(tsl::gfx::Renderer *renderer, PtcOverlayRect rect, tsl::Color color)
    {
        const auto &pal = current_palette();
        const int progress = confirm_hold_.fired ? 0 : ptc_overlay_hold_progress(&confirm_hold_, 1000);
        renderer->drawRect(rect.x + 8, rect.y + rect.h - 7, rect.w - 16, 3, renderer->a(pal.card_bg));
        if (progress) renderer->drawRect(rect.x + 8, rect.y + rect.h - 7,
            (rect.w - 16) * progress / 1000, 3, renderer->a(color));
    }

    void draw_code_preview(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        char line[256];
        begin_page(renderer, cx, cy, cw, ptc_ui_text(preview_changed_ ?
            PTC_UI_T_STATUS_CHANGED_PLEASE_CONFIRM_AGAIN : PTC_UI_T_CONFIRM_PLAYTIME_GRANT),
            PtcFlowPath{{ptc_ui_text(PTC_UI_T_NAV_HOME), ptc_ui_text(PTC_UI_T_NAV_EXTRA_TIME_CODE),
                ptc_ui_text(PTC_UI_T_NAV_CONFIRM)}, 3});
        ptc_ui_format_actual_added(preview_summary_.grant_minutes, line, sizeof(line));
        dashboard_panel(renderer, {{line, PTC_OVERLAY_FONT_DISPLAY, pal.accent},
            {ptc_ui_text(PTC_UI_T_OVERLAY_CODE_ONCE), PTC_OVERLAY_FONT_BODY_LG, pal.text_secondary}}, pal.accent);
        if (preview_summary_.converts_unlimited_to_limited) std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else if (preview_summary_.remaining_available) ptc_ui_format_minutes(preview_summary_.remaining_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        child_card(renderer, ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY), line, nullptr,
            PTC_OVERLAY_FONT_DISPLAY, pal.success, PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
        if (preview_summary_.remaining_after_available) ptc_ui_format_minutes(preview_summary_.remaining_after_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        const auto level = ptc_overlay_preview_visual_level(preview_summary_.remaining_after_available,
            preview_summary_.remaining_after_minutes, preview_summary_.preview_capped, preview_summary_.converts_unlimited_to_limited);
        const bool dangerous = level == PTC_OVERLAY_PREVIEW_DANGER;
        child_card(renderer, ptc_ui_text(PTC_UI_T_ESTIMATED_TIME_AFTER_GRANT), line, nullptr,
            PTC_OVERLAY_FONT_DISPLAY, dangerous ? pal.danger : pal.success, PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
        const char *warning = ptc_ui_text(PTC_UI_T_OVERLAY_CODE_NOT_USED);
        if (preview_summary_.converts_unlimited_to_limited) warning = ptc_ui_text(PTC_UI_T_WARNING_WILL_CHANGE_FROM_UNLIMITED_TO_LIMITED);
        else if (!preview_summary_.remaining_after_available) warning = ptc_ui_text(PTC_UI_T_WARNING_CANNOT_CONFIRM_PLAYTIME_AFTER_GRANT_RIGHT);
        else if (!preview_summary_.remaining_after_minutes) warning = ptc_ui_text(PTC_UI_T_WARNING_ESTIMATED_NO_PLAYTIME_REMAINING_AFTER_GRANT);
        else if (preview_summary_.preview_capped) {
            ptc_ui_format_daily_cap_applied(preview_summary_.effective_add_minutes, line, sizeof(line));
            warning = line;
        }
        flow_text(renderer, warning, PTC_OVERLAY_FONT_BODY_LG,
            dangerous ? pal.danger : level == PTC_OVERLAY_PREVIEW_WARNING ? pal.warning : pal.text_secondary);
        end_page(renderer);
        const char *cancel_label = ptc_ui_text(PTC_UI_T_B_CANCEL_2);
        const char *confirm_label = ptc_ui_text(dangerous ? PTC_UI_T_HOLD_A_TO_CONFIRM : PTC_UI_T_A_CONFIRM_2);
        draw_button(renderer, page_layout_.secondary, cancel_label, pal.text_primary, false,
            fit_rail_button_size(renderer, cancel_label, page_layout_.secondary.w), preview_focus_ == 0);
        draw_button(renderer, page_layout_.primary, confirm_label,
            dangerous ? pal.danger : pal.accent, true,
            fit_rail_button_size(renderer, confirm_label, page_layout_.primary.w), preview_focus_ == 1);
        if (dangerous) draw_hold(renderer, page_layout_.primary, pal.danger);
    }

    void draw_code_success(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        const auto accent = result_failed_ ? pal.danger : result_pending_ ? pal.warning : pal.success;
        char line[256];
        begin_page(renderer, cx, cy, cw, ptc_ui_text(result_pending_ ? PTC_UI_T_CONFIRMING_GRANT_RESULT :
            result_failed_ ? PTC_UI_T_GRANT_FAILED : PTC_UI_T_GRANT_SUCCESSFUL),
            PtcFlowPath{{ptc_ui_text(PTC_UI_T_NAV_HOME), ptc_ui_text(PTC_UI_T_NAV_EXTRA_TIME_CODE),
                ptc_ui_text(PTC_UI_T_NAV_RESULT)}, 3}, false, PTC_OVERLAY_FONT_TITLE_LG);
        std::snprintf(line, sizeof(line), ptc_ui_text(result_pending_ ? PTC_UI_T_ESTIMATED_D_MIN :
            result_failed_ ? PTC_UI_T_ORIGINALLY_PLANNED_D_MIN : PTC_UI_T_ADDED_D_MIN), preview_summary_.grant_minutes);
        dashboard_panel(renderer, {{line, PTC_OVERLAY_FONT_DISPLAY, accent},
            {ptc_ui_text(result_pending_ ? PTC_UI_T_VERIFYING_RESULT_PLEASE_DO_NOT_RE_ENTER :
            result_failed_ ? PTC_UI_T_VERIFICATION_FAILED_CODE_NOT_CONSUMED_CAN_RETRY : PTC_UI_T_THIS_GRANT_CODE_HAS_ALREADY_BEEN_USED),
            PTC_OVERLAY_FONT_BODY_LG, pal.text_secondary}}, accent);
        if (redemption_before_.converts_unlimited_to_limited) std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else if (redemption_before_.remaining_available) ptc_ui_format_minutes(redemption_before_.remaining_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        child_card(renderer, ptc_ui_text(PTC_UI_T_BEFORE_GRANT), line, nullptr,
            PTC_OVERLAY_FONT_DISPLAY, pal.text_primary, PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
        const auto &after = result_pending_ ? preview_summary_ : displayed_summary_;
        if (result_pending_ ? after.remaining_after_available : after.remaining_available)
            ptc_ui_format_minutes(result_pending_ ? after.remaining_after_minutes : after.remaining_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        child_card(renderer, ptc_ui_text(result_pending_ ? PTC_UI_T_PREVIEW_AFTER_GRANT : PTC_UI_T_AFTER_GRANT),
            line, nullptr, PTC_OVERLAY_FONT_DISPLAY, accent, PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
        if (result_failed_) flow_text(renderer, ptc_overlay_bridge_error_message_zh(bridge_), PTC_OVERLAY_FONT_BODY_LG, pal.danger);
        flow_text(renderer, ptc_ui_text(result_pending_ ? PTC_UI_T_YOU_MAY_CLOSE_STATUS_WILL_RESUME_NEXT :
            result_failed_ ? PTC_UI_T_FAILURE_RESULT_CONFIRMED : PTC_UI_T_RESULTS_CONFIRMED_AND_SAVED), PTC_OVERLAY_FONT_BODY, accent);
        end_page(renderer);
        page_layout_.primary = ptc_overlay_rect(cx, cy + PTC_OVERLAY_RAIL_Y, cw, PTC_OVERLAY_RAIL_H);
        page_layout_.secondary = {};
        const char *return_label = ptc_ui_text(PTC_UI_T_A_B_RETURN_TO_BLANK_INPUT_PAGE);
        draw_button(renderer, page_layout_.primary, return_label, pal.accent, true,
            fit_rail_button_size(renderer, return_label, page_layout_.primary.w), true);
    }

    void draw_parent_page(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        char line[384];
        const char *title = ptc_ui_text(parent_view_ == ParentView::Pin ? PTC_UI_T_PARENT_PIN_VERIFICATION :
            parent_view_ == ParentView::Actions ? PTC_UI_T_PARENT_ZONE :
            parent_view_ == ParentView::Confirm ? PTC_UI_T_CONFIRM_RESTORING_LIMIT : PTC_UI_T_OPERATION_RESULT);
        if (parent_view_ == ParentView::Confirm && parent_action_ == PTC_OVERLAY_PARENT_WAIVE_DOCK) title = ptc_ui_text(PTC_UI_T_DOCK_WAIVE);
        PtcFlowPath flow{{ptc_ui_text(PTC_UI_T_NAV_HOME), ptc_ui_text(PTC_UI_T_PARENT_ZONE), nullptr}, 2};
        if (parent_view_ == ParentView::Pin) {
            flow.nodes[2] = ptc_ui_text(PTC_UI_T_NAV_VERIFY);
            flow.count = 3;
        } else if (parent_view_ == ParentView::Confirm) {
            flow.nodes[2] = ptc_ui_text(PTC_UI_T_NAV_CONFIRM);
            flow.count = 3;
        } else if (parent_view_ == ParentView::Result) {
            flow.nodes[2] = ptc_ui_text(PTC_UI_T_NAV_RESULT);
            flow.count = 3;
        }
        begin_page(renderer, cx, cy, cw, title, flow, parent_view_ == ParentView::Actions);
        const bool stale = !has_status_snapshot_ || status_is_stale();
        const auto state = current_state_line(stale);
        // PIN must stay one screen: fall back to 32px only when the 44px state wraps.
        const int state_size = (parent_view_ == ParentView::Pin &&
            overlay_text_height(renderer, state.text, cw - 36, PTC_OVERLAY_FONT_DISPLAY) >
                (PTC_OVERLAY_FONT_DISPLAY * 6 + 4) / 5)
            ? PTC_OVERLAY_FONT_BODY_LG : PTC_OVERLAY_FONT_DISPLAY;
        // Confirm and result own their hero panel; repeating the state banner
        // above them only competes with the decision the parent is making.
        const bool shows_state_panel = parent_view_ == ParentView::Pin || parent_view_ == ParentView::Actions;
        if (shows_state_panel && parent_view_ == ParentView::Actions && !stale) {
            char age[64];
            format_refresh_age(age, sizeof(age));
            dashboard_panel(renderer, {{state.text, state_size, state.color},
                {age, PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary}}, state.color, state.normal);
        } else if (shows_state_panel) {
            dashboard_panel(renderer, {{state.text, state_size, state.color}}, state.color, state.normal);
        }
        if (parent_view_ == ParentView::Pin) {
            std::memset(page_layout_.pin_keys, 0, sizeof(page_layout_.pin_keys));
            char masked[PTC_AUTH_PIN_MAX_LEN + 1];
            (void)ptc_overlay_pin_mask(pin_length_, masked, sizeof(masked));
            std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_U_DIGITS_ENTERED_S), static_cast<unsigned>(pin_length_), masked);
            dashboard_panel(renderer, {{line, PTC_OVERLAY_FONT_BODY_LG, pal.text_primary}}, pal.accent);
            if (pin_message_[0]) flow_text(renderer, pin_message_, PTC_OVERLAY_FONT_BODY, pal.danger);

            if (!parent_pin_standard_keypad_) {
                flow_text(renderer, ptc_ui_text(PTC_UI_T_STICK_8_DIRECTIONS_FOR_1_8_D), PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary, 6);
                const PtcUiTextId directions[] = {PTC_UI_T_8_UP_LEFT, PTC_UI_T_1_UP, PTC_UI_T_2_UP_RIGHT,
                    PTC_UI_T_7_LEFT, PTC_UI_T_3_RIGHT, PTC_UI_T_6_DOWN_LEFT, PTC_UI_T_5_DOWN, PTC_UI_T_4_DOWN_RIGHT};
                for (unsigned i = 0; i < 8; ++i) {
                    const auto r = ptc_overlay_pin_wheel_rect(cx, flow_y_, i);
                    page_layout_.pin_keys[i] = r;
                    auto vr = r;
                    vr.y -= static_cast<int>(page_scroll_);
                    draw_button(renderer, vr, ptc_ui_text(directions[i]), pal.text_primary, false, PTC_OVERLAY_FONT_SECONDARY);
                }
                const auto r0 = ptc_overlay_pin_center_0_rect(cx, flow_y_);
                page_layout_.pin_keys[8] = r0;
                auto vr0 = r0;
                vr0.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, vr0, "\uE0E2 0", pal.text_primary, false, PTC_OVERLAY_FONT_SECONDARY);

                const auto r9 = ptc_overlay_pin_center_9_rect(cx, flow_y_);
                page_layout_.pin_keys[9] = r9;
                auto vr9 = r9;
                vr9.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, vr9, "\uE0E3 9", pal.text_primary, false, PTC_OVERLAY_FONT_SECONDARY);

                flow_y_ += 220;
            } else {
                flow_text(renderer, ptc_ui_text(PTC_UI_T_TOUCH_NUMERIC_KEYPAD), PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary, 6);
                for (int d = 0; d < 10; ++d) {
                    const auto r = ptc_overlay_pin_numpad_key_rect(cx, flow_y_, d);
                    page_layout_.pin_keys[d] = r;
                    auto vr = r;
                    vr.y -= static_cast<int>(page_scroll_);
                    char digit_str[4];
                    std::snprintf(digit_str, sizeof(digit_str), "%d", d);
                    const bool digit_focused = parent_pin_standard_keypad_ && parent_pin_focus_ == (d == 0 ? 11 : d);
                    draw_button(renderer, vr, digit_str, pal.text_primary, false, PTC_OVERLAY_FONT_KEYPAD, digit_focused);
                }
                const auto r_bs = ptc_overlay_rect(cx + 14, flow_y_ + 3 * 66, 124, 58);
                page_layout_.pin_keys[12] = r_bs;
                auto vr_bs = r_bs;
                vr_bs.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, vr_bs, ptc_ui_text(PTC_UI_T_X_BACKSPACE), pal.warning, false, PTC_OVERLAY_FONT_SECONDARY,
                    parent_pin_standard_keypad_ && parent_pin_focus_ == 10);

                const auto r_clr = ptc_overlay_pin_numpad_clear_rect(cx, flow_y_);
                page_layout_.pin_keys[11] = r_clr;
                auto vr_clr = r_clr;
                vr_clr.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, vr_clr, ptc_ui_text(PTC_UI_T_OVERLAY_CLEAR_PIN), pal.danger, false, PTC_OVERLAY_FONT_SECONDARY,
                    parent_pin_standard_keypad_ && parent_pin_focus_ == 12);

                flow_y_ += 260;
            }

            // Bottom action row: Toggle button on the left, Backspace button on the right
            const int btn_h = 48;
            const int toggle_w = 214;
            const int bs_w = cw - 12 - toggle_w - 10;
            const auto toggle_rect = ptc_overlay_rect(cx + 4, flow_y_, toggle_w, btn_h);
            page_layout_.pin_keys[10] = toggle_rect;
            auto visible_toggle = toggle_rect;
            visible_toggle.y -= static_cast<int>(page_scroll_);
            draw_button(renderer, visible_toggle,
                ptc_ui_text(parent_pin_standard_keypad_ ? PTC_UI_T_SWITCH_TO_DIAL_PAD : PTC_UI_T_SWITCH_TO_NUMPAD),
                pal.accent, parent_pin_standard_keypad_, PTC_OVERLAY_FONT_SECONDARY,
                parent_pin_standard_keypad_ && parent_pin_focus_ == 0);

            page_layout_.backspace = ptc_overlay_rect(cx + 4 + toggle_w + 10, flow_y_, bs_w, btn_h);
            auto visible_bs = page_layout_.backspace;
            visible_bs.y -= static_cast<int>(page_scroll_);
            draw_button(renderer, visible_bs, ptc_ui_text(PTC_UI_T_ZL_BACKSPACE_2), pal.warning, false, PTC_OVERLAY_FONT_SECONDARY,
                parent_pin_standard_keypad_ && parent_pin_focus_ == 15);
            flow_y_ += btn_h + 4;

            end_page(renderer);
            const char *back_label = ptc_ui_text(PTC_UI_T_B_BACK_2);
            const char *verify_label = ptc_ui_text(PTC_UI_T_VERIFY_PIN);
            draw_button(renderer, page_layout_.secondary, back_label, pal.text_primary, false,
                fit_rail_button_size(renderer, back_label, page_layout_.secondary.w),
                parent_pin_standard_keypad_ && parent_pin_focus_ == 13);
            draw_button(renderer, page_layout_.primary, verify_label, pal.accent, true,
                fit_rail_button_size(renderer, verify_label, page_layout_.primary.w),
                parent_pin_standard_keypad_ && parent_pin_focus_ == 14);
            return;
        }
        if (parent_view_ == ParentView::Actions) {
            // Groups answer "what am I trying to change today?" and stay in the
            // same order as ptc_overlay_parent_action_order() so focus never jumps.
            const PtcUiTextId groups[] = {PTC_UI_T_OVERLAY_GROUP_TODAY_ALLOWANCE,
                PTC_UI_T_OVERLAY_GROUP_ACTIVE_RULES, PTC_UI_T_OVERLAY_GROUP_SYSTEM_RECOVERY};
            const PtcUiTextId hints[] = {PTC_UI_T_OVERLAY_GROUP_TODAY_ALLOWANCE_HINT,
                PTC_UI_T_OVERLAY_GROUP_ACTIVE_RULES_HINT, PTC_UI_T_OVERLAY_GROUP_SYSTEM_RECOVERY_HINT};
            const int *order = ptc_overlay_parent_action_order();
            for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot) {
                const int group = ptc_overlay_parent_action_group(slot);
                const int index = order[slot];
                if (group >= 0 && (slot == 0 || ptc_overlay_parent_action_group(slot - 1) != group)) {
                    const auto group_color = group == 2 ? pal.danger : group == 1 ? pal.warning : pal.accent;
                    if (slot != 0) {
                        flow_y_ += 4;
                        renderer->drawRect(page_layout_.viewport.x + 4,
                            flow_y_ - static_cast<int>(page_scroll_), page_layout_.viewport.w - 12, 1,
                            renderer->a(pal.border_decorative));
                        flow_y_ += 8;
                    }
                    const int label_y = flow_y_ - static_cast<int>(page_scroll_);
                    renderer->drawRect(page_layout_.viewport.x + 4, label_y + 4, 4,
                        PTC_OVERLAY_FONT_CAPTION - 4, renderer->a(group_color));
                    overlay_text(renderer, ptc_ui_text(groups[group]), page_layout_.viewport.x + 16,
                        label_y, page_layout_.viewport.w - 24, PTC_OVERLAY_FONT_CAPTION, group_color);
                    flow_y_ += overlay_text_height(renderer, ptc_ui_text(groups[group]),
                        page_layout_.viewport.w - 24, PTC_OVERLAY_FONT_CAPTION);
                    // The hint is context only; it never becomes a hit target.
                    overlay_text(renderer, ptc_ui_text(hints[group]), page_layout_.viewport.x + 16,
                        flow_y_ - static_cast<int>(page_scroll_), page_layout_.viewport.w - 24,
                        PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
                    flow_y_ += overlay_text_height(renderer, ptc_ui_text(hints[group]),
                        page_layout_.viewport.w - 24, PTC_OVERLAY_FONT_CAPTION) + 6;
                }
                const auto action = static_cast<PtcOverlayParentAction>(index);
                const char *reason = parent_action_reason(action);
                const char *value = nullptr;
                const char *note = reason;
                char action_note[512];
                if (index == PTC_OVERLAY_PARENT_ADD_MINUTES) {
                    ptc_ui_format_minutes(daily_add_minutes_, line, sizeof(line));
                    value = line;
                    std::snprintf(action_note, sizeof(action_note), "%s%s%s",
                        ptc_ui_text(PTC_UI_T_OVERLAY_ADJUST_MINUTES), reason ? "\n" : "", reason ? reason : "");
                    note = action_note;
                } else if (index == PTC_OVERLAY_PARENT_SKIP_EYE_CARE && !reason && displayed_summary_.eye_care_rest_remaining_seconds > 0) {
                    ptc_ui_format_minutes((displayed_summary_.eye_care_rest_remaining_seconds + 59) / 60, line, sizeof(line));
                    value = line;
                }
                const int start = flow_y_;
                const auto tone = reason ? pal.text_disabled : parent_action_requires_hold(action) ? pal.danger : pal.accent;
                page_layout_.actions[index] = flow_card(renderer, ptc_ui_text(parent_action_label_id(index)), value, note,
                    PTC_OVERLAY_FONT_BTN, reason ? pal.text_disabled : pal.text_primary,
                    tone, action, parent_action_ == index, reason != nullptr);
                if (parent_action_ == index && !reason && parent_action_requires_hold(action)) {
                    auto rect = page_layout_.actions[index];
                    rect.y = start - static_cast<int>(page_scroll_);
                    draw_hold(renderer, rect, pal.danger);
                }
            }
            end_page(renderer);
            if (page_changed_) page_scroll_target_ = static_cast<float>(ptc_overlay_scroll_to_rect(
                page_layout_.viewport, page_layout_.actions[parent_action_], 0, page_layout_.scroll_max));
            page_layout_.primary = ptc_overlay_rect(cx, cy + PTC_OVERLAY_RAIL_Y, cw, PTC_OVERLAY_RAIL_H);
            page_layout_.secondary = {};
            const auto selected = static_cast<PtcOverlayParentAction>(parent_action_);
            const char *footer = ptc_ui_text(parent_action_reason(selected) ? PTC_UI_T_ACTION_CURRENTLY_UNAVAILABLE_B_BACK :
                parent_action_requires_hold(selected) ? PTC_UI_T_HOLD_A_1S_Y_REFRESH_B_BACK : PTC_UI_T_A_EXECUTE_Y_REFRESH_B_BACK);
            draw_button(renderer, page_layout_.primary, footer, parent_action_reason(selected) ? pal.warning : pal.accent,
                false, fit_rail_button_size(renderer, footer, page_layout_.primary.w));
            return;
        }
        if (parent_view_ == ParentView::Confirm) {
            const bool waive = parent_action_ == PTC_OVERLAY_PARENT_WAIVE_DOCK;
            const bool immediate = !waive && displayed_summary_.bedtime_active && displayed_summary_.bedtime_skipped;
            const auto action = static_cast<PtcOverlayParentAction>(parent_action_);
            // Confirm answers three questions in order: what will change, what
            // still applies, and (only when relevant) why it is risky. The
            // current-state banner stays on the actions page; repeating it here
            // can contradict the warning (e.g. bedtime already skipped).
            dashboard_panel(renderer, {{ptc_ui_text(parent_action_label_id(parent_action_)),
                PTC_OVERLAY_FONT_TITLE, immediate ? pal.danger : pal.accent},
                {ptc_ui_text(waive ? PTC_UI_T_DOCK_WAIVE_BODY : immediate ?
                    PTC_UI_T_CURRENTLY_DURING_BEDTIME_CONFIRMING_WILL_RESTRICT_USE : PTC_UI_T_SKIP_WILL_BE_CLEARED_BEDTIME_RESTRICTIONS_WILL),
                PTC_OVERLAY_FONT_BODY_LG, immediate ? pal.danger : pal.text_primary}},
                immediate ? pal.danger : pal.accent);
            const char *reason = parent_action_reason(action);
            if (reason) flow_text(renderer, reason, PTC_OVERLAY_FONT_BODY, pal.warning);
            end_page(renderer);
            const char *cancel_label = ptc_ui_text(PTC_UI_T_B_CANCEL_2);
            const char *confirm_label = ptc_ui_text(immediate ? PTC_UI_T_HOLD_A_1S_TO_RESTORE :
                waive ? PTC_UI_T_A_CONFIRM : PTC_UI_T_A_CONFIRM_RESTORE);
            draw_button(renderer, page_layout_.secondary, cancel_label, pal.text_primary, false,
                fit_rail_button_size(renderer, cancel_label, page_layout_.secondary.w),
                confirm_focus_ == 0);
            draw_button(renderer, page_layout_.primary, confirm_label,
                reason ? pal.text_disabled : immediate ? pal.danger : pal.accent, true,
                fit_rail_button_size(renderer, confirm_label, page_layout_.primary.w),
                confirm_focus_ == 1);
            if (immediate) draw_hold(renderer, page_layout_.primary, pal.danger);
            return;
        }
        // Result page: one verdict first, then what changed, then the current
        // state. Only the verdict keeps the hero treatment; everything the
        // parent does not need to act on drops to body/caption weight.
        const auto result_color = bridge_->waiting ? pal.warning : parent_action_succeeded_ ? pal.success : pal.danger;
        const char *verdict = ptc_ui_text(bridge_->waiting ? PTC_UI_T_PROCESSING_REQUEST : parent_action_succeeded_ ?
            PTC_UI_T_OPERATION_COMPLETED : PTC_UI_T_OPERATION_INCOMPLETE);
        dashboard_panel(renderer, {{verdict, PTC_OVERLAY_FONT_TITLE_LG, result_color},
            {request_label(), PTC_OVERLAY_FONT_BODY, pal.text_secondary}}, result_color);
        if (!bridge_->waiting && parent_action_succeeded_) {
            if (last_request_kind_ == OverlayRequestKind::ClaimDailyBuffer) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_SELF_BUFFER_CLAIMED_ADDED_D_MIN), displayed_summary_.daily_buffer_minutes);
                flow_text(renderer, line, PTC_OVERLAY_FONT_BODY_LG, pal.success);
            } else if (last_request_kind_ == OverlayRequestKind::SkipEyeCare)
                flow_text(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_SKIP_CONFIRM_BODY), PTC_OVERLAY_FONT_BODY, pal.text_secondary);
            flow_text(renderer, ptc_ui_text(!displayed_summary_.access_recovery_required ? PTC_UI_T_PCTL_RELOADED_RESTRICTIONS_CLEARED :
                bedtime_restricted() ? PTC_UI_T_STILL_RESTRICTED_BY_BEDTIME : displayed_summary_.daily_restriction_active ?
                PTC_UI_T_STILL_RESTRICTED_BY_DAILY_LIMIT : PTC_UI_T_PLEASE_CHECK_CURRENT_RESTRICTION_STATUS),
                PTC_OVERLAY_FONT_BODY, displayed_summary_.access_recovery_required ? pal.warning : pal.success);
        } else if (!bridge_->waiting) {
            flow_text(renderer, ptc_overlay_bridge_error_message_zh(bridge_), PTC_OVERLAY_FONT_BODY_LG, pal.danger);
            if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_ERROR_CODE_D), bridge_->summary.error_code);
                flow_text(renderer, line, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
            }
            flow_text(renderer, ptc_ui_text(PTC_UI_T_WHEN_BACKGROUND_IS_UNAVAILABLE_PLEASE_RECOVER_VIA), PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
        }
        end_page(renderer);
        const char *reverify_label = ptc_ui_text(PTC_UI_T_Y_RE_VERIFY);
        const char *back_label = ptc_ui_text(PTC_UI_T_B_BACK_2);
        if (last_request_kind_ != OverlayRequestKind::ClaimDailyBuffer)
            draw_button(renderer, page_layout_.secondary, reverify_label, pal.accent, false,
                fit_rail_button_size(renderer, reverify_label, page_layout_.secondary.w), result_focus_ == 1);
        else page_layout_.secondary = {};
        draw_button(renderer, page_layout_.primary, back_label, pal.accent, true,
            fit_rail_button_size(renderer, back_label, page_layout_.primary.w), result_focus_ == 0);
    }

    void draw_status_details(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        const auto &summary = displayed_summary_;
        char line[384];
        begin_page(renderer, cx, cy, cw, ptc_ui_text(PTC_UI_T_NAV_STATUS_DETAILS),
            PtcFlowPath{{ptc_ui_text(PTC_UI_T_NAV_HOME), ptc_ui_text(PTC_UI_T_NAV_STATUS_DETAILS)}, 2}, true);
        if (error_) {
            dashboard_panel(renderer, {
                {request_label(), PTC_OVERLAY_FONT_BODY_LG, pal.text_primary},
                {ptc_overlay_bridge_error_message_zh(bridge_), PTC_OVERLAY_FONT_BODY_LG, pal.danger}
            }, pal.danger);
            if (bridge_->summary.valid && bridge_->summary.error_code > 0) {
                std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_ERROR_CODE_D), bridge_->summary.error_code);
                flow_text(renderer, line, PTC_OVERLAY_FONT_BODY, pal.danger, 6);
            }
            flow_text(renderer, ptc_ui_text(last_request_kind_ == OverlayRequestKind::OfflineCode ||
                last_request_kind_ == OverlayRequestKind::PreviewOfflineCode ? PTC_UI_T_PLEASE_RE_ENTER_Y_RETURNS_TO_INPUT : PTC_UI_T_PRESS_Y_TO_TRY_AGAIN),
                PTC_OVERLAY_FONT_BODY, pal.danger, 10);
        } else if (has_status_snapshot_) {
            // 1. What is true right now: one verdict, then the allowance it comes from.
            const bool stale = !has_status_snapshot_ || status_is_stale();
            const auto state = current_state_line(stale);
            char age[64];
            format_refresh_age(age, sizeof(age));
            dashboard_panel(renderer, {{state.text, PTC_OVERLAY_FONT_DISPLAY, state.color},
                {age, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary}}, state.color, state.normal);

            char label[128], value[128], note[128];
            int day, second;
            ptc_overlay_format_child_quota_parts(&summary, label, sizeof(label), value, sizeof(value), note, sizeof(note));
            const bool fresh = child_status_fresh(summary, day, second);
            child_card(renderer, label, value, note, PTC_OVERLAY_FONT_DISPLAY,
                fresh ? pal.success : pal.warning, PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
            ptc_overlay_format_child_restriction_detail(&summary, line, sizeof(line));
            if (line[0]) flow_text(renderer, line, PTC_OVERLAY_FONT_BODY_LG, pal.warning, 6);

            // 2. Why: the three rules, each with its own state.
            flow_section(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_GROUP_RULES), pal.accent);
            const auto policies = child_policy_status(summary);
            const auto eye = child_eye_care_status(summary);
            child_rule(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_EYE_LABEL), eye.compact, eye.detail,
                eye.warning ? pal.warning : summary.eye_care_enabled ? pal.success : pal.text_secondary, ChildCardKind::Eye);
            child_rule(renderer, ptc_ui_text(PTC_UI_T_POLICY_BED_LABEL), policies.bedtime.detail, nullptr,
                bedtime_policy_color(policies.bedtime), ChildCardKind::Bedtime);
            child_rule(renderer, ptc_ui_text(PTC_UI_T_POLICY_DOCK_LABEL), policies.dock.detail, nullptr,
                policy_color(policies.dock), ChildCardKind::Dock);

            // 3. Background numbers and transport detail stay collapsed by default.
            flow_section(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_GROUP_DIAGNOSTICS), pal.text_secondary);
            const auto toggle_rect = ptc_overlay_rect(page_layout_.viewport.x + 4, flow_y_, page_layout_.viewport.w - 8, 52);
            auto visible_toggle = toggle_rect;
            visible_toggle.y -= static_cast<int>(page_scroll_);
            draw_button(renderer, visible_toggle,
                ptc_ui_text(details_tech_expanded_ ? PTC_UI_T_COLLAPSE_SECONDARY_DETAILS : PTC_UI_T_EXPAND_SECONDARY_DETAILS),
                pal.accent, details_tech_expanded_, PTC_OVERLAY_FONT_SECONDARY, status_details_focus_ == 1);
            page_layout_.status = toggle_rect;
            flow_y_ += 60;

            if (details_tech_expanded_) {
                if (summary.usage_present) {
                    const bool used = summary.valid && summary.played_minutes_available && summary.played_minutes >= 0;
                    if (used) std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_CONSOLE_USAGE_D_MIN), summary.played_minutes);
                    else std::snprintf(line, sizeof(line), "%s: %s", ptc_ui_text(PTC_UI_T_CONSOLE_USAGE), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
                    flow_text(renderer, line, PTC_OVERLAY_FONT_BODY, used ? pal.text_primary : pal.warning, 4);
                    // The backend timestamp belongs to the counter, not this UI refresh.
                    const std::time_t captured = static_cast<std::time_t>(summary.usage_captured_at);
                    const std::tm *utc = summary.usage_captured_at > 0 ? std::gmtime(&captured) : nullptr;
                    char timestamp[64];
                    if (utc && static_cast<int64_t>(captured) == summary.usage_captured_at &&
                        std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S UTC", utc))
                        std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_OVERLAY_USAGE_CAPTURE), timestamp);
                    else std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_OVERLAY_USAGE_CAPTURE), ptc_ui_text(PTC_UI_T_UNAVAILABLE));
                    flow_text(renderer, line, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary, 4);
                }
                flow_text(renderer, ptc_overlay_rule_source_label(summary.rule_source), PTC_OVERLAY_FONT_BODY, pal.text_secondary, 4);
                ptc_overlay_format_child_buffer_status(&summary, line, sizeof(line));
                flow_text(renderer, line, PTC_OVERLAY_FONT_BODY, pal.text_secondary, 4);
                flow_text(renderer, ptc_ui_text(PTC_UI_T_EYE_CARE_CYCLE_FOOTNOTE), PTC_OVERLAY_FONT_CAPTION, pal.text_secondary, 4);
                if (summary.dock_available) flow_text(renderer, ptc_ui_text(PTC_UI_T_DOCK_RULE_EXPLANATION), PTC_OVERLAY_FONT_CAPTION, pal.text_secondary, 4);
                flow_text(renderer, ptc_overlay_bridge_transport_label(bridge_), PTC_OVERLAY_FONT_CAPTION, pal.text_secondary, 4);
                if (bridge_->waiting) flow_text(renderer, transport_stage(bridge_), PTC_OVERLAY_FONT_BODY, pal.warning, 4);
            }
        } else {
            flow_text(renderer, ptc_ui_text(PTC_UI_T_THE_STATUS_HAS_NOT_BEEN_OBTAINED_YET), PTC_OVERLAY_FONT_BODY_LG, pal.text_secondary);
        }
        end_page(renderer);
        page_layout_.primary = ptc_overlay_rect(cx, cy + PTC_OVERLAY_RAIL_Y, cw, PTC_OVERLAY_RAIL_H);
        page_layout_.secondary = {};
        const char *back_label = ptc_ui_text(PTC_UI_T_B_BACK_2);
        draw_button(renderer, page_layout_.primary, back_label, pal.accent, true,
            fit_rail_button_size(renderer, back_label, page_layout_.primary.w),
            status_details_focus_ == 0);
    }

    void draw_keypad(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        const auto &summary = displayed_summary_;
        char line[128], age[64], date[64];
        begin_page(renderer, cx, cy, cw, ptc_ui_text(PTC_UI_T_ENTER_THE_EXTRA_TIME_CODE),
            PtcFlowPath{{ptc_ui_text(PTC_UI_T_NAV_HOME), ptc_ui_text(PTC_UI_T_NAV_EXTRA_TIME_CODE)}, 2}, true);
        if (ptc_overlay_remaining_refresh_pending(bridge_->waiting, active_request_kind_ == OverlayRequestKind::OfflineCode))
            std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_CONFIRMING));
        else if (summary.valid && summary.unrestricted_today == 1) std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else if (summary.valid && summary.remaining_available) ptc_ui_format_minutes(summary.remaining_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        format_refresh_age(age, sizeof(age));
        const bool default_scheme = navbar_scheme_ == OverlayNavbarScheme::Default;
        // The whole block is clipped to the scrolling viewport, whose top edge is
        // cy + PTC_OVERLAY_HEADER_H + 8 (absolute Y=64). A 28px line draws its ink
        // from baseline-16, so a baseline at cy+76 lost the glyph tops; cy+84 keeps
        // the row whole and still leaves 19px above the code slots (absolute 116).
        // Count and console date then fill the 180..244 band between the slots and
        // the keypad instead of running into the slot outlines.
        const int status_y = default_scheme ? 84 : 100;
        const int meta_y = default_scheme ? 192 : 208;
        const int date_y = default_scheme ? 218 : 234;
        draw_localized(renderer, line, false, cx + 14, cy + status_y, PTC_OVERLAY_FONT_BODY, renderer->a(pal.success));
        const int left_width = static_cast<int>(measure_localized(renderer, line, PTC_OVERLAY_FONT_BODY).first);
        if (left_width + static_cast<int>(measure_localized(renderer, age, PTC_OVERLAY_FONT_SECONDARY).first) + 40 <= cw)
            draw_localized_right(renderer, age, cx + cw - 14, cy + status_y, PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary);
        const int total = PTC_OVERLAY_CODE_SYMBOLS * PTC_OVERLAY_SLOT_W + (PTC_OVERLAY_CODE_SYMBOLS - 1) * PTC_OVERLAY_SLOT_GAP;
        for (unsigned i = 0; i < PTC_OVERLAY_CODE_SYMBOLS; ++i) {
            const int x = cx + (cw - total) / 2 + static_cast<int>(i) * (PTC_OVERLAY_SLOT_W + PTC_OVERLAY_SLOT_GAP);
            const bool cursor = input_->length < PTC_OVERLAY_CODE_SYMBOLS && i == input_->length;
            const bool entered = i < input_->length;
            draw_dashboard_surface(renderer, ptc_overlay_rect(x, cy + PTC_OVERLAY_SLOT_Y, PTC_OVERLAY_SLOT_W, PTC_OVERLAY_SLOT_H),
                cursor ? pal.accent_soft : pal.card_raised, cursor ? pal.accent : pal.border_control, cursor);
            const char symbol[] = {entered ? input_->symbols[i] : cursor ? '_' : '|', '\0'};
            draw_localized_center(renderer, symbol, x, cy + PTC_OVERLAY_SLOT_Y, PTC_OVERLAY_SLOT_W, PTC_OVERLAY_SLOT_H,
                PTC_OVERLAY_FONT_CODE, entered ? pal.text_primary : cursor ? pal.accent : pal.text_disabled);
        }
        std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_OVERLAY_INPUT_COUNT), input_->length);
        draw_localized(renderer, line, false, cx + 14, cy + meta_y, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.text_secondary));
        format_console_date(summary, date, sizeof(date));
        draw_localized(renderer, date, false, cx + 14, cy + date_y, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.text_secondary));
        const char *charset = ptc_overlay_input_charset();
        for (unsigned i = 0; i < PTC_OVERLAY_KEY_COUNT; ++i) {
            const char symbol[] = {charset[i], '\0'};
            const auto rect = ptc_overlay_key_rect(cx, cy, i);
            const bool key_focused = keypad_nav_focus_ == -1 && i == input_->cursor;
            draw_dashboard_surface(renderer, rect, key_focused ? pal.accent_soft : pal.card_raised,
                key_focused ? pal.accent : pal.border_control, key_focused);
            if (key_focused) {
                draw_focus_ring(renderer, rect, pal.accent);
            }
            draw_localized_center(renderer, symbol, rect.x, rect.y, rect.w, rect.h, PTC_OVERLAY_FONT_KEYPAD,
                key_focused ? pal.accent : pal.text_primary);
        }
        draw_button(renderer, ptc_overlay_backspace_rect(cx, cy), ptc_ui_text(PTC_UI_T_X_BACKSPACE), pal.warning, false, PTC_OVERLAY_FONT_SECONDARY, keypad_nav_focus_ == 10);
        draw_button(renderer, ptc_overlay_clear_rect(cx, cy), ptc_ui_text(PTC_UI_T_OVERLAY_CLEAR), pal.danger, false, PTC_OVERLAY_FONT_SECONDARY, keypad_nav_focus_ == 11);
        // Submit and navigation live outside the scrolled viewport; release the
        // clip so the rail row is not cut at the viewport bottom.
        end_page(renderer);
        page_layout_.primary = ptc_overlay_submit_rect(cx, cy, cw);
        const bool unavailable = has_status_snapshot_ && !status_is_stale() &&
            (summary.unrestricted_today == 1 || summary.eye_care_unlimited_capped || summary.dock_unlimited_capped);
        const bool ready = !unavailable && !bridge_->waiting && ptc_overlay_input_can_submit(input_);
        const char *submit = ptc_ui_text(unavailable ? PTC_UI_T_OVERLAY_CODE_UNAVAILABLE :
            bridge_->waiting ? PTC_UI_T_BACKGROUND_PROCESSING : ready ? PTC_UI_T_OVERLAY_SUBMIT : PTC_UI_T_OVERLAY_NEED_CODE);
        draw_button(renderer, page_layout_.primary, submit, ready ? pal.accent : unavailable ? pal.warning : pal.text_secondary,
            ready, PTC_OVERLAY_FONT_SECONDARY, keypad_nav_focus_ == 13);
        page_layout_.secondary = ptc_overlay_keypad_nav_back_rect(cx, cy);
        page_layout_.parent = ptc_overlay_keypad_nav_parent_rect(cx, cy, cw);
        draw_button(renderer, page_layout_.secondary, ptc_ui_text(PTC_UI_T_B_CANCEL_2), pal.text_primary, false, PTC_OVERLAY_FONT_BTN, keypad_nav_focus_ == 12);
        draw_button(renderer, page_layout_.parent, ptc_ui_text(PTC_UI_T_R_PARENT_AREA), pal.accent, true, PTC_OVERLAY_FONT_BTN, keypad_nav_focus_ == 14);
    }

    // ---- Child overview: status banner + rule chain ------------------------
    // The banner owns the current state (headline 44px, value 56px); rules below
    // are graded active / standby / idle so idle rules never compete for focus.
    struct ChildRuleRow {
        const char *label;
        const char *value;
        tsl::Color color = {0, 0, 0, 0};
        int level; // 0 active, 1 standby, 2 idle
    };

    static int text_width(tsl::gfx::Renderer *renderer, const char *text, int size)
    {
        const auto lines = overlay_wrap(renderer, text, size, 100000);
        return lines.empty() ? 0 : static_cast<int>(measure_localized(renderer, lines[0].c_str(), size).first);
    }

    // Largest size that keeps a button label on one line; never below 24px.
    int fit_button_size(tsl::gfx::Renderer *renderer, const char *text, int width)
    {
        const int sizes[] = {PTC_OVERLAY_FONT_BODY, 26, PTC_OVERLAY_FONT_SECONDARY};
        for (const int size : sizes)
            if (overlay_text_height(renderer, text, width - 24, size) <= (size * 6 + 4) / 5) return size;
        return PTC_OVERLAY_FONT_SECONDARY;
    }

    int child_rule_level(bool enabled, tsl::Color color) const
    {
        if (!enabled) return 2;
        return same_color(color, current_palette().text_secondary) ? 1 : 0;
    }

    void draw_child_section(tsl::gfx::Renderer *renderer, const char *label)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int y = flow_y_ - static_cast<int>(page_scroll_);
        const int width = text_width(renderer, label, PTC_OVERLAY_FONT_CAPTION);
        overlay_text(renderer, label, vp.x + 8, y, width + 2, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
        renderer->drawRect(vp.x + 8 + width + 12, y + 14, vp.w - width - 24, 1,
            renderer->a(pal.border_decorative));
        flow_y_ += 32;
    }

    void draw_child_rule_chain(tsl::gfx::Renderer *renderer, const ChildRuleRow *rows, int count)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int x = vp.x + 4, w = vp.w - 8;
        const int text_x = x + 44, avail = w - 44 - 12;
        int previous_center = 0;
        for (int i = 0; i < count; ++i) {
            const auto &row = rows[i];
            const int title_size = row.level == 2 ? PTC_OVERLAY_FONT_BODY - 2 : PTC_OVERLAY_FONT_BODY;
            const int value_size = row.level == 0 ? PTC_OVERLAY_FONT_BODY - 2 :
                row.level == 1 ? PTC_OVERLAY_FONT_SECONDARY : PTC_OVERLAY_FONT_CAPTION;
            const int title_w = text_width(renderer, row.label, title_size);
            const int value_w = text_width(renderer, row.value, value_size);
            const bool inline_value = title_w + value_w + 12 <= avail;
            const int title_h = overlay_text_height(renderer, row.label, avail, title_size);
            const int value_h = overlay_text_height(renderer, row.value, avail, value_size);
            const int height = inline_value ? 54 : std::max(54, 14 + title_h + 2 + value_h + 6);
            const int y = flow_y_ - static_cast<int>(page_scroll_);
            flow_y_ += height + 4;

            const auto soft = dashboard_soft(row.color);
            if (row.level == 0) draw_round_rect_surface(renderer, x, y, w, height, 4, soft, soft, 0);
            const int dot_x = x + 18;
            const int dot_y = y + (inline_value ? height / 2 : 27);
            // Chain rail runs between dots; dots never sit on top of it.
            if (i > 0 && dot_y - 17 > previous_center + 17)
                renderer->drawRect(dot_x - 1, previous_center + 17, 2, dot_y - 17 - (previous_center + 17),
                    renderer->a(pal.border_control));
            previous_center = dot_y;
            if (row.level == 0) {
                auto halo = row.color;
                halo.a = 5;
                draw_round_rect_surface(renderer, dot_x - 15, dot_y - 15, 30, 30, 15, halo, halo, 0);
                draw_round_rect_surface(renderer, dot_x - 10, dot_y - 10, 20, 20, 10, row.color, row.color, 0);
            } else if (row.level == 1) {
                draw_round_rect_surface(renderer, dot_x - 8, dot_y - 8, 16, 16, 8,
                    pal.text_secondary, pal.text_secondary, 0);
                draw_round_rect_surface(renderer, dot_x - 5, dot_y - 5, 10, 10, 5, pal.card_bg, pal.card_bg, 0);
            } else {
                draw_round_rect_surface(renderer, dot_x - 5, dot_y - 5, 10, 10, 5,
                    pal.text_disabled, pal.text_disabled, 0);
            }

            const auto title_color = row.level == 2 ? pal.text_disabled : pal.text_primary;
            const auto value_color = row.level == 0 ? row.color :
                row.level == 1 ? pal.text_secondary : pal.text_disabled;
            if (inline_value) {
                overlay_text(renderer, row.label, text_x, y + (height - title_h) / 2, title_w + 2,
                    title_size, title_color);
                overlay_text(renderer, row.value, x + w - 12 - value_w, y + (height - value_h) / 2,
                    value_w + 2, value_size, value_color);
            } else {
                overlay_text(renderer, row.label, text_x, y + 8, avail, title_size, title_color);
                overlay_text(renderer, row.value, text_x, y + 10 + title_h, avail, value_size, value_color);
            }
        }
    }

    // Bottom-anchored when the page fits; otherwise follows the flow and scrolls.
    PtcOverlayRect draw_child_status_row(tsl::gfx::Renderer *renderer, bool focused)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int height = 48;
        const int raw_y = std::max(flow_y_ + 4, vp.y + vp.h - height);
        const auto raw = ptc_overlay_rect(vp.x + 4, raw_y, vp.w - 8, height);
        flow_y_ = raw_y + height;
        const auto rect = ptc_overlay_rect(raw.x, raw.y - static_cast<int>(page_scroll_), raw.w, raw.h);
        draw_dashboard_surface(renderer, rect, focused ? pal.accent_soft : pal.card_bg,
            focused ? pal.accent : pal.border_decorative, focused);
        if (focused) draw_focus_ring(renderer, rect, pal.accent);
        const char *action = error_ ? ptc_ui_text(PTC_UI_T_OVERLAY_FAILED_DETAILS) :
            bridge_->waiting ? ptc_ui_text(PTC_UI_T_BACKGROUND_PROCESSING) : ptc_ui_text(PTC_UI_T_OVERLAY_OPEN_DETAILS);
        const int action_w = text_width(renderer, action, PTC_OVERLAY_FONT_SECONDARY);
        const int line_h = (PTC_OVERLAY_FONT_SECONDARY * 6 + 4) / 5;
        overlay_text(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_STATUS), rect.x + 14, rect.y + (rect.h - line_h) / 2,
            rect.w - action_w - 40, PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary);
        overlay_text(renderer, action, rect.x + rect.w - 14 - action_w, rect.y + (rect.h - line_h) / 2,
            action_w + 2, PTC_OVERLAY_FONT_SECONDARY, error_ ? pal.danger : pal.accent);
        return raw;
    }

    void draw_child_overview(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        auto summary = displayed_summary_;
        char line[384], number[48], window[160];
        const bool restricted = child_is_restricted();
        const bool bedtime = restricted && bedtime_restricted();
        const bool eye_restricted = restricted && !bedtime && eye_care_restricted();
        const bool dock_restricted = restricted && !bedtime && !eye_restricted;
        const auto eye = child_eye_care_status(summary);
        const auto policies = child_policy_status(summary);

        // Banner content ----------------------------------------------------
        const char *head = ""; tsl::Color tone = pal.success;
        const char *num = nullptr; int num_size = PTC_OVERLAY_FONT_HERO; tsl::Color num_color = pal.text_primary;
        const char *num_label = nullptr;
        const char *sub = nullptr; const char *note = nullptr;
        if (bedtime) {
            head = ptc_ui_text(PTC_UI_T_OVERLAY_BEDTIME_CHILD_TITLE); tone = pal.night;
            sub = ptc_ui_text(PTC_UI_T_OVERLAY_BEDTIME_CHILD_HERO);
            std::snprintf(window, sizeof(window), ptc_ui_text(PTC_UI_T_OVERLAY_BEDTIME_CHILD_WINDOW),
                summary.bedtime_start_minute / 60, summary.bedtime_start_minute % 60,
                summary.bedtime_end_minute / 60, summary.bedtime_end_minute % 60);
            note = window;
        } else if (eye_restricted) {
            head = ptc_ui_text(PTC_UI_T_OVERLAY_EYE_CHILD_TITLE); tone = pal.warning;
            if (eye.rest_seconds >= 0) {
                std::snprintf(number, sizeof(number), "%02d:%02d", eye.rest_seconds / 60, eye.rest_seconds % 60);
                num = number; num_color = pal.warning;
            } else {
                num = eye.compact; num_size = PTC_OVERLAY_FONT_BODY; num_color = pal.warning;
            }
            sub = ptc_ui_text(PTC_UI_T_OVERLAY_EYE_CHILD_NOTE);
            note = eye.detail;
        } else if (dock_restricted) {
            head = ptc_ui_text(summary.force_docked ? PTC_UI_T_OVERLAY_DOCK_CHILD_FORCE_TITLE :
                PTC_UI_T_OVERLAY_DOCK_CHILD_TITLE);
            tone = pal.danger;
            sub = ptc_ui_text(PTC_UI_T_OVERLAY_DOCK_CHILD_HERO);
            note = ptc_ui_text(PTC_UI_T_OVERLAY_DOCK_SUMMARY);
        } else {
            const bool confirming = ptc_overlay_remaining_refresh_pending(bridge_->waiting,
                active_request_kind_ == OverlayRequestKind::OfflineCode);
            const bool stale = status_is_stale();
            if (confirming) {
                head = ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY); tone = pal.warning;
                std::snprintf(number, sizeof(number), "%s", ptc_ui_text(PTC_UI_T_CONFIRMING));
                num = number; num_color = pal.warning;
            } else if (summary.valid && summary.unrestricted_today == 1) {
                head = ptc_ui_text(PTC_UI_T_OVERLAY_NOW_CAN_PLAY); tone = stale ? pal.warning : pal.success;
                std::snprintf(number, sizeof(number), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
                num = number; num_label = ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY);
            } else if (summary.valid && summary.remaining_available && summary.remaining_minutes == 0) {
                head = ptc_ui_text(PTC_UI_T_OVERLAY_QUOTA_DONE_TITLE); tone = pal.danger;
                sub = ptc_ui_text(PTC_UI_T_OVERLAY_QUOTA_DONE_VALUE);
            } else if (summary.valid && summary.remaining_available) {
                const bool low = summary.remaining_minutes <= 10;
                head = ptc_ui_text(low ? PTC_UI_T_OVERLAY_NOW_LOW : PTC_UI_T_OVERLAY_NOW_CAN_PLAY);
                tone = stale || low ? pal.warning : pal.success;
                ptc_ui_format_minutes(summary.remaining_minutes, number, sizeof(number));
                num = number; num_label = ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY);
            } else {
                head = ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY); tone = pal.text_secondary;
                std::snprintf(number, sizeof(number), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
                num = number; num_color = pal.text_disabled;
            }
            // Keep the quota and usage together; proxy-timer detail stays in the status page.
            char label[128], value[128], quota_note[128];
            ptc_overlay_format_child_quota_parts(&summary, label, sizeof(label), value, sizeof(value),
                quota_note, sizeof(quota_note));
            const bool used = summary.valid && summary.played_minutes_available && summary.played_minutes >= 0;
            PtcUiTextArg args[] = {PTC_UI_TEXT_STRING("quota", value),
                PTC_UI_TEXT_NUMBER("minutes", summary.played_minutes)};
            if (used && !summary.remaining_available && !summary.daily_rule_available &&
                summary.unrestricted_today != 1 && !summary.eye_care_unlimited_capped)
                std::snprintf(line, sizeof(line), "%s %s", label, value);
            else if (summary.usage_present) {
                char daily_quota[96], console_usage[96];
                ptc_ui_text_format(PTC_UI_T_OVERLAY_QUOTA, daily_quota, sizeof(daily_quota), args, 1);
                if (used) {
                    std::snprintf(console_usage, sizeof(console_usage), ptc_ui_text(PTC_UI_T_CONSOLE_USAGE_D_MIN),
                        summary.played_minutes);
                } else {
                    std::snprintf(console_usage, sizeof(console_usage), "%s %s",
                        ptc_ui_text(PTC_UI_T_CONSOLE_USAGE), ptc_ui_text(PTC_UI_T_POLICY_PENDING));
                }
                std::snprintf(line, sizeof(line), "%s / %s", daily_quota, console_usage);
            }
            else ptc_ui_text_format(used ? PTC_UI_T_OVERLAY_QUOTA_USAGE :
                PTC_UI_T_OVERLAY_QUOTA, line, sizeof(line), args, used ? 2 : 1);
            note = line;
        }

        // One walk both measures and draws, so the banner always fits its text.
        const int pad_x = 14, banner_y = cy - 6, inner_w = cw - 2 * pad_x;
        auto walk = [&](bool draw) {
            int y = cy + 46;
            const int x = cx + pad_x;
            const int head_h = overlay_text_height(renderer, head, inner_w, PTC_OVERLAY_FONT_TITLE_LG);
            if (draw) overlay_text(renderer, head, x, y, inner_w, PTC_OVERLAY_FONT_TITLE_LG, tone);
            y += head_h + 2;
            if (num) {
                const int num_h = overlay_text_height(renderer, num, inner_w, num_size);
                const int num_w = text_width(renderer, num, num_size);
                const int label_w = num_label ? text_width(renderer, num_label, PTC_OVERLAY_FONT_CAPTION) : 0;
                const bool beside = num_label && num_w + label_w + 16 <= inner_w;
                if (draw) {
                    overlay_text(renderer, num, x, y, inner_w, num_size, num_color);
                    if (beside) overlay_text(renderer, num_label, x + inner_w - label_w,
                        y + num_size - PTC_OVERLAY_FONT_CAPTION, label_w + 2, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
                }
                y += num_h;
                if (num_label && !beside) {
                    if (draw) overlay_text(renderer, num_label, x, y, inner_w, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
                    y += overlay_text_height(renderer, num_label, inner_w, PTC_OVERLAY_FONT_CAPTION);
                }
            }
            if (sub) {
                if (draw) overlay_text(renderer, sub, x, y, inner_w, PTC_OVERLAY_FONT_BODY, pal.text_primary);
                y += overlay_text_height(renderer, sub, inner_w, PTC_OVERLAY_FONT_BODY) + 4;
            }
            if (note && *note) {
                if (draw) overlay_text(renderer, note, x, y, inner_w, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
                y += overlay_text_height(renderer, note, inner_w, PTC_OVERLAY_FONT_CAPTION);
            }
            return y + 12;
        };
        const int banner_bottom = walk(false);
        const auto banner = ptc_overlay_rect(cx, banner_y, cw, banner_bottom - banner_y);
        draw_dashboard_surface(renderer, banner, dashboard_soft(tone), pal.border_decorative);
        renderer->drawRect(banner.x + 8, banner.y + 1, banner.w - 16, 3, renderer->a(tone));

        // Header row: brand on the left, parent area and refresh on the right.
        const bool refresh_busy = bridge_->waiting;
        const char *refresh_text = ptc_ui_text(refresh_busy ? PTC_UI_T_REFRESHING : PTC_UI_T_Y_REFRESH);
        const char *parent_text = ptc_ui_text(PTC_UI_T_R_PARENT_AREA);
        const int refresh_w = std::max(text_width(renderer, ptc_ui_text(PTC_UI_T_Y_REFRESH), PTC_OVERLAY_FONT_SECONDARY),
            text_width(renderer, ptc_ui_text(PTC_UI_T_REFRESHING), PTC_OVERLAY_FONT_SECONDARY)) + 24;
        const int parent_w = text_width(renderer, parent_text, PTC_OVERLAY_FONT_SECONDARY) + 24;
        page_layout_.refresh = ptc_overlay_rect(cx + cw - 8 - refresh_w, cy, refresh_w, 40);
        page_layout_.parent = ptc_overlay_rect(page_layout_.refresh.x - 6 - parent_w, cy, parent_w, 40);
        const int brand_x = cx + pad_x, brand_w = page_layout_.parent.x - brand_x - 8;
        const char *brand = ptc_ui_text(PTC_UI_T_PLAYWISE);
        const char *dot = std::strstr(brand, "\xC2\xB7");
        const int brand_top = cy + (40 - (PTC_OVERLAY_FONT_CAPTION * 6 + 4) / 5) / 2;
        if (dot) {
            const std::string head_part(brand, dot);
            const int head_w = text_width(renderer, head_part.c_str(), PTC_OVERLAY_FONT_CAPTION);
            overlay_text(renderer, head_part.c_str(), brand_x, brand_top, brand_w, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
            overlay_text(renderer, dot, brand_x + head_w, brand_top, std::max(0, brand_w - head_w),
                PTC_OVERLAY_FONT_CAPTION, pal.accent);
        } else overlay_text(renderer, brand, brand_x, brand_top, brand_w, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
        const bool highlight = needs_refresh() && !refresh_busy;
        draw_button(renderer, page_layout_.parent, parent_text, pal.accent, restricted,
            PTC_OVERLAY_FONT_SECONDARY, child_overview_focus_ == (restricted ? 0 : 2));
        draw_button(renderer, page_layout_.refresh, refresh_text,
            refresh_busy ? pal.text_secondary : highlight ? pal.warning : pal.accent,
            highlight || refresh_busy, PTC_OVERLAY_FONT_SECONDARY, child_overview_focus_ == (restricted ? 3 : 4));
        walk(true);

        // Scrolling area: ask-a-parent hint, rule chain and the status row.
        const int button_h = PTC_OVERLAY_RAIL_H;
        const int bottom = restricted ? cy + PTC_OVERLAY_RAIL_BOTTOM : cy + PTC_OVERLAY_RAIL_Y - 8;
        page_layout_.viewport = ptc_overlay_rect(cx, banner_bottom + 8, cw, bottom - (banner_bottom + 8));
        flow_y_ = page_layout_.viewport.y;
        renderer->enableScissoring(cx, page_layout_.viewport.y, cw, page_layout_.viewport.h);

        char eye_text[160], bed_text[160], dock_text[160];
        std::snprintf(eye_text, sizeof(eye_text), "%s", eye.compact);
        std::snprintf(bed_text, sizeof(bed_text), "%s", policies.bedtime.compact);
        std::snprintf(dock_text, sizeof(dock_text), "%s", policies.dock.compact);
        const auto eye_color = eye.warning ? pal.warning : summary.eye_care_enabled ? pal.success : pal.text_secondary;
        const auto bed_color = bedtime_policy_color(policies.bedtime);
        const auto dock_color = policy_color(policies.dock);
        const bool bed_on = policies.bedtime.state != PTC_POLICY_OFF && policies.bedtime.state != PTC_POLICY_NONE_TODAY;
        const bool dock_on = policies.dock.state != PTC_POLICY_OFF && policies.dock.state != PTC_POLICY_NONE_TODAY;
        const ChildRuleRow all_rows[3] = {
            {ptc_ui_text(PTC_UI_T_OVERLAY_EYE_LABEL), eye_text, eye_color,
                child_rule_level(summary.eye_care_enabled, eye_color)},
            {ptc_ui_text(PTC_UI_T_POLICY_BED_LABEL), bed_text, bed_color, child_rule_level(bed_on, bed_color)},
            {ptc_ui_text(policies.mode == PTC_UI_T_STATUS_TO_CONFIRM ? PTC_UI_T_POLICY_DOCK_LABEL : policies.mode),
                dock_text, dock_color, child_rule_level(dock_on, dock_color)}};
        ChildRuleRow rows[3] = {};
        int count = 0;
        for (int i = 0; i < 3; ++i) {
            // The rule that caused the restriction is already the banner.
            if ((bedtime && i == 1) || (eye_restricted && i == 0) || (dock_restricted && i == 2)) continue;
            rows[count++] = all_rows[i];
        }
        if (restricted) {
            flow_text(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_ASK_PARENT), PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
            flow_y_ += 4;
        }
        draw_child_section(renderer, ptc_ui_text(restricted ? PTC_UI_T_OVERLAY_SECTION_OTHER_RULES :
            PTC_UI_T_OVERLAY_SECTION_RULES));
        draw_child_rule_chain(renderer, rows, count);
        page_layout_.status = draw_child_status_row(renderer, child_overview_focus_ == (restricted ? 2 : 3));
        end_page(renderer);

        page_layout_.primary = {};
        page_layout_.secondary = {};
        page_layout_.redeem = {};
        page_layout_.buffer = {};
        if (restricted) return;

        // Buffer and extra-time code share one bottom row; B/A are on the Tesla footer.
        const bool buffer_ready = summary.valid && summary.daily_buffer_available && !bridge_->waiting;
        const int button_y = cy + PTC_OVERLAY_RAIL_Y;
        const int buffer_w = 200;
        page_layout_.buffer = ptc_overlay_rect(cx, button_y, buffer_w, button_h);
        page_layout_.redeem = ptc_overlay_rect(cx + buffer_w + 8, button_y, cw - buffer_w - 8, button_h);
        char redeem_btn[96];
        std::snprintf(redeem_btn, sizeof(redeem_btn), "\uE0E0  %s", ptc_ui_text(PTC_UI_T_ENTER_THE_EXTRA_TIME_CODE));
        const char *buffer_text = ptc_ui_text(buffer_ready ? PTC_UI_T_L_RECEIVE_AUTONOMOUS_BUFFER :
            summary.daily_buffer_claimed ? PTC_UI_T_BUFFER_RECEIVED_TODAY : PTC_UI_T_THE_BUFFER_IS_TEMPORARILY_UNAVAILABLE);
        draw_button(renderer, page_layout_.buffer, buffer_text,
            buffer_ready ? pal.accent : pal.text_secondary, buffer_ready,
            fit_button_size(renderer, buffer_text, buffer_w), child_overview_focus_ == 1);
        draw_button(renderer, page_layout_.redeem, redeem_btn, pal.accent, true,
            fit_button_size(renderer, redeem_btn, page_layout_.redeem.w), child_overview_focus_ == 0);
    }

    void draw_language_confirm(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        begin_page(renderer, cx, cy, cw, ptc_ui_text(PTC_UI_T_OVERLAY_LANG_CONFIRM_TITLE),
            PtcFlowPath{{ptc_ui_text(PTC_UI_T_NAV_HOME), ptc_ui_text(PTC_UI_T_UI_LANGUAGE),
                         ptc_ui_text(PTC_UI_T_NAV_CONFIRM)}, 3},
            false, PTC_OVERLAY_FONT_TITLE_LG);

        flow_text(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_LANG_CONFIRM_DESC),
            PTC_OVERLAY_FONT_CAPTION, pal.text_secondary, 8);
        flow_y_ += 4;

        struct LangOptionInfo {
            PtcOverlayLanguageSetting setting;
            PtcUiTextId title;
            PtcUiTextId subtitle;
        };
        static const LangOptionInfo OPTIONS[4] = {
            {PtcOverlayLanguageSetting::Auto, PTC_UI_T_OVERLAY_LANG_OPT_FOLLOW, PTC_UI_T_OVERLAY_LANG_OPT_FOLLOW_SUB},
            {PtcOverlayLanguageSetting::Simplified, PTC_UI_T_OVERLAY_LANG_OPT_HANS, PTC_UI_T_OVERLAY_LANG_OPT_HANS_SUB},
            {PtcOverlayLanguageSetting::Traditional, PTC_UI_T_OVERLAY_LANG_OPT_HANT, PTC_UI_T_OVERLAY_LANG_OPT_HANT_SUB},
            {PtcOverlayLanguageSetting::English, PTC_UI_T_OVERLAY_LANG_OPT_EN, PTC_UI_T_OVERLAY_LANG_OPT_EN_SUB},
        };

        const auto &vp = page_layout_.viewport;
        const int card_w = vp.w - 12;
        const int card_h = 68;

        for (int i = 0; i < 4; ++i) {
            const auto raw = ptc_overlay_rect(vp.x + 4, flow_y_, card_w, card_h);
            const int y = raw.y - static_cast<int>(page_scroll_);
            flow_y_ += card_h + 8;
            page_layout_.lang_options[i] = raw;

            if (y + card_h <= vp.y || y >= vp.y + vp.h) continue;

            const bool selected = (language_draft_selection_ == OPTIONS[i].setting);
            const bool focused = (language_confirm_focus_ == i);
            const bool is_current = (s_overlay_lang_setting == OPTIONS[i].setting);

            const auto bg_col = selected ? pal.accent_soft : pal.card_bg;
            const auto border_col = focused ? pal.accent : selected ? pal.accent : pal.border_decorative;

            draw_dashboard_surface(renderer, ptc_overlay_rect(raw.x, y, raw.w, raw.h),
                bg_col, border_col, selected || focused);
            renderer->drawRect(raw.x, y + 8, selected ? 4 : 2, card_h - 16,
                renderer->a(selected ? pal.accent : pal.border_control));

            if (focused) {
                draw_focus_ring(renderer, ptc_overlay_rect(raw.x, y, raw.w, raw.h), pal.accent);
            }

            const char *title_text = ptc_ui_text(OPTIONS[i].title);
            const char *sub_text = ptc_ui_text(OPTIONS[i].subtitle);

            overlay_text(renderer, title_text, raw.x + 18, y + 10, raw.w - 120,
                PTC_OVERLAY_FONT_BODY, selected ? pal.accent : pal.text_primary);
            overlay_text(renderer, sub_text, raw.x + 18, y + 36, raw.w - 36,
                PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);

            if (is_current) {
                const char *badge = ptc_ui_text(PTC_UI_T_OVERLAY_LANG_CURRENT_BADGE);
                const int badge_w = text_width(renderer, badge, PTC_OVERLAY_FONT_CAPTION);
                const int badge_x = raw.x + raw.w - badge_w - 16;
                overlay_text(renderer, badge, badge_x, y + 12, badge_w + 4,
                    PTC_OVERLAY_FONT_CAPTION, pal.success);
            }
        }

        page_layout_.secondary = ptc_overlay_rect(cx, cy + PTC_OVERLAY_RAIL_Y, 154, PTC_OVERLAY_RAIL_H);
        page_layout_.primary = ptc_overlay_rect(cx + 166, cy + PTC_OVERLAY_RAIL_Y, cw - 166, PTC_OVERLAY_RAIL_H);

        const char *cancel_text = ptc_ui_text(PTC_UI_T_B_CANCEL_2);
        const char *confirm_text = ptc_ui_text(PTC_UI_T_OVERLAY_LANG_CONFIRM_BTN);

        draw_button(renderer, page_layout_.secondary, cancel_text,
            pal.text_secondary, false, fit_rail_button_size(renderer, cancel_text, 154),
            language_confirm_focus_ == 4);
        draw_button(renderer, page_layout_.primary, confirm_text,
            pal.accent, true, fit_rail_button_size(renderer, confirm_text, cw - 166),
            language_confirm_focus_ == 5);

        end_page(renderer);
    }

    void draw_overlay(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw, s32 ch)
    {
        (void)ch;
        prepare_page();
        if (language_confirm_active_) draw_language_confirm(renderer, cx, cy, cw);
        else if (access_recovery_visible()) draw_parent_page(renderer, cx, cy, cw);
        else if (success_visible_) draw_code_success(renderer, cx, cy, cw);
        else if (preview_ready_) draw_code_preview(renderer, cx, cy, cw);
        else if (status_expanded_) draw_status_details(renderer, cx, cy, cw);
        else if (is_keypad_mode() && !child_is_restricted()) draw_keypad(renderer, cx, cy, cw);
        else draw_child_overview(renderer, cx, cy, cw);
    }
