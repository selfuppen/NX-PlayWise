// Included in the production GUI and host preview. All interactive geometry is
// published in page_layout_; input never recreates measured content positions.
    PtcOverlayPageLayout page_layout_{};
    float page_scroll_ = 0, page_scroll_target_ = 0;
    int page_identity_ = -1;
    bool page_changed_ = false;
    int flow_y_ = 0;
    int promoted_parent_action_ = -1;
    int parent_tab_ = 0;
    bool parent_unavailable_expanded_ = false;
    OverlayNavbarScheme navbar_scheme_ = OverlayNavbarScheme::Default;
    PtcRestrictionReason child_drawn_reason_ = PTC_REASON_NONE;

    int parent_action_tab(int action) const
    {
        const int slot = ptc_overlay_parent_action_slot(action);
        const int group = ptc_overlay_parent_action_group(slot);
        return (group >= 0 && group <= 2) ? group : 0;
    }

    int parent_visible_actions(int out_actions[PTC_OVERLAY_PARENT_ACTION_COUNT]) const
    {
        int count = 0;
        const int tab = parent_tab_ < 0 ? 0 : parent_tab_ > 2 ? 2 : parent_tab_;
        if (promoted_parent_action_ >= 0 && promoted_parent_action_ < PTC_OVERLAY_PARENT_ACTION_COUNT) {
            out_actions[count++] = promoted_parent_action_;
        }
        const int *order = ptc_overlay_parent_action_order();
        if (tab == 1) {
            int unavail[PTC_OVERLAY_PARENT_ACTION_COUNT];
            int unavail_count = 0;
            bool focused_in_unavail = false;
            for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot) {
                const int idx = order[slot];
                if (idx == promoted_parent_action_) continue;
                if (ptc_overlay_parent_action_group(slot) != 1) continue;
                if (parent_action_reason(static_cast<PtcOverlayParentAction>(idx)) == nullptr) {
                    out_actions[count++] = idx;
                } else {
                    unavail[unavail_count++] = idx;
                    if (parent_action_ == idx) focused_in_unavail = true;
                }
            }
            if (parent_unavailable_expanded_ || focused_in_unavail) {
                for (int i = 0; i < unavail_count; ++i) out_actions[count++] = unavail[i];
            }
            return count;
        }
        for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot) {
            const int idx = order[slot];
            if (idx == promoted_parent_action_) continue;
            if (ptc_overlay_parent_action_group(slot) != tab) continue;
            out_actions[count++] = idx;
        }
        return count;
    }

    int parent_first_action_for_tab(int tab) const
    {
        const int clamped = tab < 0 ? 0 : tab > 2 ? 2 : tab;
        const int *order = ptc_overlay_parent_action_order();
        int fallback = -1;
        for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot) {
            const int idx = order[slot];
            if (idx == promoted_parent_action_) continue;
            if (ptc_overlay_parent_action_group(slot) != clamped) continue;
            if (fallback < 0) fallback = idx;
            if (parent_action_reason(static_cast<PtcOverlayParentAction>(idx)) == nullptr) return idx;
        }
        if (fallback >= 0) return fallback;
        return promoted_parent_action_ >= 0 ? promoted_parent_action_ : order[0];
    }

    PtcRestrictionReason child_home_reason() const
    {
        int day, second;
        return child_snapshot_usable(displayed_summary_, day, second)
            ? ptc_overlay_detail_reason(&displayed_summary_) : PTC_REASON_NONE;
    }

    bool child_is_restricted() const
    {
        auto reason = child_home_reason();
        // Daily exhaustion keeps the regular keypad and daily-buffer layout.
        return reason != PTC_REASON_NONE && reason != PTC_REASON_DAILY;
    }

    int page_identity() const
    {
        if (language_confirm_active_) return 6;
        if (access_recovery_visible()) return 10 + static_cast<int>(parent_view_);
        if (success_visible_) return 2;
        if (preview_ready_) return 3;
        if (status_expanded_) return 4;
        const auto reason = child_home_reason();
        if (reason == PTC_REASON_BEDTIME) return 20;
        if (reason == PTC_REASON_EYE) return 21;
        if (reason == PTC_REASON_DOCK) return 22;
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
        const char *text, tsl::Color color, bool emphasized = false, int size = PTC_OVERLAY_FONT_BTN,
        bool focused = false, bool solid = false)
    {
        const auto &pal = current_palette();
        const bool danger = color.r == pal.danger.r && color.g == pal.danger.g && color.b == pal.danger.b;
        const bool warning = color.r == pal.warning.r && color.g == pal.warning.g && color.b == pal.warning.b;
        const auto tone = focused ? pal.accent : color;
        const auto soft = danger ? pal.danger_soft : warning ? pal.warning_soft : pal.accent_soft;
        const bool emp = emphasized || focused;
        draw_dashboard_surface(renderer, rect, solid ? pal.accent : emp ? soft : pal.card_raised,
            focused ? pal.accent : emp ? tone : pal.border_control, emp);
        if (focused) {
            draw_focus_ring(renderer, rect, solid ? pal.text_primary : pal.accent);
        }
        const auto lines = overlay_button_lines(renderer, text, rect.w - 24, rect.h - 8, size);
        const int step = (size * 6 + 4) / 5;
        const int top = rect.y + (rect.h - static_cast<int>(lines.size()) * step) / 2;
        renderer->enableScissoring(rect.x + 12, rect.y + 4, rect.w - 24, rect.h - 8);
        for (size_t i = 0; i < lines.size(); ++i)
            overlay_text(renderer, lines[i].c_str(), rect.x + 12, top + static_cast<int>(i) * step,
                rect.w - 24, size, solid ? pal.panel_bg : focused ? pal.accent : color, true);
        renderer->disableScissoring();
    }

    void begin_page(tsl::gfx::Renderer *renderer, int cx, int cy, int cw,
        const char *title, const PtcFlowPath &flow, bool refresh = false,
        int title_size = PTC_OVERLAY_FONT_TITLE, bool full_viewport = false)
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
        const int vp_bottom = full_viewport ? PTC_OVERLAY_FULL_VIEWPORT_BOTTOM : PTC_OVERLAY_VIEWPORT_BOTTOM;
        page_layout_.viewport = ptc_overlay_rect(cx, cy + PTC_OVERLAY_HEADER_H + 8, cw,
            vp_bottom - (PTC_OVERLAY_HEADER_H + 8));
        if (full_viewport) {
            page_layout_.secondary = {};
            page_layout_.primary = {};
        } else {
            page_layout_.secondary = ptc_overlay_rect(cx, cy + PTC_OVERLAY_RAIL_Y,
                154, PTC_OVERLAY_RAIL_H);
            page_layout_.primary = ptc_overlay_rect(cx + 166, cy + PTC_OVERLAY_RAIL_Y,
                cw - 166, PTC_OVERLAY_RAIL_H);
        }
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
        int label_size = PTC_OVERLAY_FONT_BODY, bool promoted = false)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int raw_w = vp.w - 12;
        const int content_w = raw_w - 28;
        const int icon_space = 30;
        const int text_w = content_w - icon_space;
        const int text_x = (vp.x + 4) + 14 + icon_space;

        /* Unfocused unavailable rule cards fold into a compact single-line row so
           active and promoted actions stay above the fold without losing focusability. */
        if (disabled && !highlighted) {
            const int compact_size = PTC_OVERLAY_FONT_SECONDARY;
            const int label_w = static_cast<int>(measure_localized(renderer, label, compact_size).first);
            const bool short_note = note != nullptr && note[0] != '\0' && std::strchr(note, '\n') == nullptr;
            const int note_w = short_note ? static_cast<int>(measure_localized(renderer, note, PTC_OVERLAY_FONT_CAPTION).first) : 0;
            const bool show_inline_note = short_note && (label_w + note_w + 12 <= text_w);
            const int label_h = overlay_text_height(renderer, label, show_inline_note ? (label_w + 2) : text_w, compact_size);
            const int height = std::max(static_cast<int>(PTC_OVERLAY_PARENT_CARD_COMPACT_H), label_h + 12);
            const auto raw = ptc_overlay_rect(vp.x + 4, flow_y_, raw_w, height);
            const int y = raw.y - static_cast<int>(page_scroll_);
            flow_y_ += height + 4;
            if (y + height <= vp.y || y >= vp.y + vp.h) return raw;

            draw_dashboard_surface(renderer, ptc_overlay_rect(raw.x, y, raw.w, raw.h),
                pal.card_bg, pal.border_decorative, false);
            renderer->drawRect(raw.x, y + 6, 2, height - 12, renderer->a(pal.text_disabled));
            draw_parent_action_icon(renderer, action, raw.x + 14, y + (height - 22) / 2, pal.text_disabled);
            overlay_text(renderer, label, text_x, y + (height - label_h) / 2,
                show_inline_note ? (label_w + 2) : text_w, compact_size, pal.text_disabled);
            if (show_inline_note) {
                const int note_h = (PTC_OVERLAY_FONT_CAPTION * 6 + 4) / 5;
                overlay_text(renderer, note, raw.x + raw.w - 14 - note_w, y + (height - note_h) / 2,
                    note_w + 2, PTC_OVERLAY_FONT_CAPTION, pal.text_disabled);
            }
            return raw;
        }

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

        const auto card_tone = promoted ? pal.danger : tone;
        const auto card_bg = promoted ? pal.danger_soft : highlighted ? dashboard_soft(tone) : pal.card_raised;
        const auto card_border = promoted ? pal.danger : highlighted ? tone : pal.border_control;
        draw_dashboard_surface(renderer, ptc_overlay_rect(raw.x, y, raw.w, raw.h),
            card_bg, card_border, highlighted || promoted);
        if (highlighted) {
            draw_focus_ring(renderer, ptc_overlay_rect(raw.x, y, raw.w, raw.h), card_tone);
        }
        renderer->drawRect(raw.x, y + 6, promoted ? 5 : highlighted ? 4 : disabled ? 2 : 3,
            height - 12, renderer->a(card_tone));

        const int content_top = y + (height - content_h) / 2;
        const int label_y = content_top + (inline_value ? (row0_h - label_h) / 2 : 0);

        // Icon aligns with the first line of the title row
        const int icon_y = label_y + (first_line_h - 22) / 2;
        draw_parent_action_icon(renderer, action, raw.x + 14, icon_y, disabled ? pal.text_disabled : card_tone);

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
        draw_code_stepper(renderer, page_layout_.viewport.x + 4,
            flow_y_ - static_cast<int>(page_scroll_), page_layout_.viewport.w - 8, 1);
        flow_y_ += 34;
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
        draw_code_stepper(renderer, page_layout_.viewport.x + 4,
            flow_y_ - static_cast<int>(page_scroll_), page_layout_.viewport.w - 8, 2);
        flow_y_ += 34;
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

    // Compact single-line (auto-wrapping when needed) status strip for the
    // Parent Actions page so more action cards stay visible above the fold.
    void draw_parent_state_strip(tsl::gfx::Renderer *renderer, const OverlayCurrentState &state, bool stale)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int card_x = vp.x + 4;
        const int card_w = vp.w - 8;
        const int inner_w = card_w - 28;
        char age[64] = {};
        const bool has_age = !stale;
        if (has_age) format_refresh_age(age, sizeof(age));
        const int title_w = static_cast<int>(measure_localized(renderer, state.text, PTC_OVERLAY_FONT_BODY).first);
        const int age_w = has_age ? static_cast<int>(measure_localized(renderer, age, PTC_OVERLAY_FONT_CAPTION).first) : 0;
        const bool inline_age = has_age && (title_w + age_w + 16 <= inner_w);
        const int title_h = overlay_text_height(renderer, state.text, inline_age ? (title_w + 2) : inner_w, PTC_OVERLAY_FONT_BODY);
        const int age_h = has_age ? overlay_text_height(renderer, age, inline_age ? (age_w + 2) : inner_w, PTC_OVERLAY_FONT_CAPTION) : 0;
        const int content_h = inline_age ? std::max(title_h, age_h) : (has_age ? (title_h + 4 + age_h) : title_h);
        const int height = std::max(46, content_h + 14);
        const int y = flow_y_ - static_cast<int>(page_scroll_);
        flow_y_ += height + 6;
        if (y + height <= vp.y || y >= vp.y + vp.h) return;

        draw_dashboard_surface(renderer, ptc_overlay_rect(card_x, y, card_w, height),
            state.normal ? tsl::Color{1, 2, 3, 15} : dashboard_soft(state.color),
            state.normal ? state.color : pal.border_decorative, false);
        renderer->drawRect(card_x, y + 6, state.normal ? 2 : 3, height - 12, renderer->a(state.color));
        const int top = y + (height - content_h) / 2;
        if (inline_age) {
            overlay_text(renderer, state.text, card_x + 14, top + (content_h - title_h) / 2,
                title_w + 2, PTC_OVERLAY_FONT_BODY, state.color);
            overlay_text(renderer, age, card_x + card_w - 14 - age_w, top + (content_h - age_h) / 2,
                age_w + 2, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
        } else {
            overlay_text(renderer, state.text, card_x + 14, top, inner_w, PTC_OVERLAY_FONT_BODY, state.color);
            if (has_age) {
                overlay_text(renderer, age, card_x + 14, top + title_h + 4, inner_w,
                    PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
            }
        }
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
        const bool full_vp = parent_view_ == ParentView::Actions;
        begin_page(renderer, cx, cy, cw, title, flow, parent_view_ == ParentView::Actions,
            PTC_OVERLAY_FONT_TITLE, full_vp);
        const bool stale = !has_status_snapshot_ || status_is_stale();
        const auto state = current_state_line(stale);
        if (parent_view_ == ParentView::Actions) {
            draw_parent_state_strip(renderer, state, stale);
        } else if (parent_view_ == ParentView::Pin && !state.normal && !pin_message_[0]) {
            draw_parent_state_strip(renderer, state, stale);
        }
        if (parent_view_ == ParentView::Pin) {
            std::memset(page_layout_.pin_keys, 0, sizeof(page_layout_.pin_keys));
            const auto &vp = page_layout_.viewport;
            const int card_x = vp.x + 4;
            const int card_w = vp.w - 8;
            const bool has_msg = pin_message_[0] != '\0';
            const int msg_h = has_msg ? overlay_text_height(renderer, pin_message_, card_w - 24, PTC_OVERLAY_FONT_CAPTION) : 0;

            // Compact Blind PIN status + circular masked dot well
            {
                const int pin_card_h = 84 + (has_msg ? (msg_h + 6) : 0);
                const int py = flow_y_ - static_cast<int>(page_scroll_);
                const auto card_bg = has_msg ? pal.danger_soft : pal.card_bg;
                const auto card_bd = has_msg ? pal.danger : pal.border_control;
                draw_round_rect_surface(renderer, card_x, py, card_w, pin_card_h, 6,
                    card_bg, card_bd, has_msg ? 2 : 1);

                char count_buf[48];
                std::snprintf(count_buf, sizeof(count_buf), ptc_ui_text(PTC_UI_T_OVERLAY_PIN_COUNT_BADGE),
                    static_cast<unsigned>(pin_length_));
                const int count_w = text_width(renderer, count_buf, PTC_OVERLAY_FONT_CAPTION) + 16;
                const int badge_x = card_x + card_w - 10 - count_w;
                draw_pill(renderer, badge_x, py + 7, count_w, 24, pal.accent_soft, pal.accent, 1);
                draw_localized_center(renderer, count_buf, badge_x, py + 7, count_w, 24,
                    PTC_OVERLAY_FONT_CAPTION, pal.accent);

                const char *blind_txt = ptc_ui_text(PTC_UI_T_OVERLAY_PIN_BLIND_BADGE);
                overlay_text(renderer, blind_txt, card_x + 12, py + 7,
                    std::max(40, badge_x - (card_x + 12) - 8), PTC_OVERLAY_FONT_CAPTION,
                    has_msg ? pal.danger : pal.text_secondary);

                // Recessed masked dot well
                const int well_x = card_x + 10;
                const int well_y = py + 37;
                const int well_w = card_w - 20;
                const int well_h = 36;
                draw_round_rect_surface(renderer, well_x, well_y, well_w, well_h, 5,
                    pal.panel_bg, pal.border_decorative, 1);

                const size_t dot_count = std::max<size_t>(6,
                    std::min<size_t>(static_cast<size_t>(PTC_AUTH_PIN_MAX_LEN),
                        pin_length_ + (pin_length_ < PTC_AUTH_PIN_MAX_LEN ? 1 : 0)));
                const int dot_d = 12;
                const int dot_gap = dot_count > 10 ? 8 : 12;
                const int total_dots_w = static_cast<int>(dot_count) * dot_d +
                    static_cast<int>(dot_count - 1) * dot_gap;
                int dx = well_x + (well_w - total_dots_w) / 2;
                const int dy = well_y + (well_h - dot_d) / 2;
                for (size_t i = 0; i < dot_count; ++i) {
                    const bool filled = i < pin_length_;
                    if (filled) {
                        draw_round_rect_surface(renderer, dx, dy, dot_d, dot_d, dot_d / 2,
                            pal.accent, pal.accent, 0);
                    } else {
                        draw_round_rect_surface(renderer, dx, dy, dot_d, dot_d, dot_d / 2,
                            pal.card_bg, pal.border_control, 2);
                    }
                    dx += dot_d + dot_gap;
                }

                if (has_msg) {
                    overlay_text(renderer, pin_message_, card_x + 12, well_y + well_h + 6,
                        card_w - 24, PTC_OVERLAY_FONT_CAPTION, pal.danger);
                }
                flow_y_ += pin_card_h + 6;
            }

            if (!parent_pin_standard_keypad_) {
                // Batch 5 aligned Joy-Con 8-way radial compass + independent X=0 / Y=9 keycaps.
                // Dynamically sized to guarantee scroll_max == 0 in all languages and error states.
                const int bottom_rows_h = 46 + 6 + 46 + 4;
                const int compass_h = std::clamp(vp.y + vp.h - flow_y_ - bottom_rows_h - 6, 188, 266);
                const int compass_raw_y = flow_y_;
                const int compass_y = compass_raw_y - static_cast<int>(page_scroll_);

                draw_round_rect_surface(renderer, card_x, compass_y, card_w, compass_h, 6,
                    pal.card_bg, pal.border_decorative, 1);
                renderer->drawRect(card_x + 8, compass_y + 1, card_w - 16, 2, renderer->a(pal.accent));

                const char *hint = ptc_ui_text(PTC_UI_T_OVERLAY_PIN_COMPASS_HINT);
                draw_localized_center(renderer, hint, card_x + 8, compass_y + 4, card_w - 16, 24,
                    PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);

                const int hub_cx = card_x + card_w / 2;
                const int hub_cy = compass_y + 30 + (compass_h - 34) / 2;
                const int node_w = 54;
                const int node_h = 34;
                const int rx = 142;
                const int ry = std::max(54, (compass_h - 34 - node_h - 10) / 2);
                const int dx_diag = 104;
                const int dy_diag = (ry * 72) / 100;

                // Order matches main.cpp touch wheel_digits[8] = {8, 1, 2, 7, 3, 6, 5, 4}
                const int off_x[8] = {-dx_diag, 0, dx_diag, -rx, rx, -dx_diag, 0, dx_diag};
                const int off_y[8] = {-dy_diag, -ry, -dy_diag, 0, 0, dy_diag, ry, dy_diag};
                const char *digit_lbl[8] = {"8", "1", "2", "7", "3", "6", "5", "4"};
                const bool is_cardinal[8] = {false, true, false, true, true, false, true, false};

                // Subtle outer octagonal compass ring connecting clockwise neighbors
                static const int ring_order[8] = {1, 2, 4, 7, 6, 5, 3, 0};
                for (int r_i = 0; r_i < 8; ++r_i) {
                    const int a = ring_order[r_i];
                    const int b = ring_order[(r_i + 1) % 8];
                    draw_line_2px(renderer, hub_cx + off_x[a], hub_cy + off_y[a],
                        hub_cx + off_x[b], hub_cy + off_y[b], pal.border_decorative);
                }

                // 8 radial spokes from Joy-Con left stick hub
                for (unsigned i = 0; i < 8; ++i) {
                    draw_line_2px(renderer, hub_cx, hub_cy,
                        hub_cx + off_x[i], hub_cy + off_y[i],
                        is_cardinal[i] ? pal.border_control : pal.border_decorative);
                }

                // Center Left-Stick hub badge
                const int hub_w = 88;
                const int hub_h = 42;
                draw_round_rect_surface(renderer, hub_cx - hub_w / 2, hub_cy - hub_h / 2,
                    hub_w, hub_h, 21, pal.accent_soft, pal.accent, 2);
                draw_localized_center(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_PIN_STICK_HUB),
                    hub_cx - hub_w / 2 + 4, hub_cy - hub_h / 2, hub_w - 8, hub_h,
                    PTC_OVERLAY_FONT_CAPTION, pal.accent);

                // 8 directional digit nodes
                for (unsigned i = 0; i < 8; ++i) {
                    const int nx = hub_cx + off_x[i] - node_w / 2;
                    const int ny_screen = hub_cy + off_y[i] - node_h / 2;
                    const int ny_raw = ny_screen + static_cast<int>(page_scroll_);
                    page_layout_.pin_keys[i] = ptc_overlay_rect(nx, ny_raw, node_w, node_h);
                    draw_round_rect_surface(renderer, nx, ny_screen, node_w, node_h, 10,
                        is_cardinal[i] ? pal.accent_soft : pal.card_raised,
                        is_cardinal[i] ? pal.accent : pal.border_control,
                        is_cardinal[i] ? 2 : 1);
                    draw_localized_center(renderer, digit_lbl[i], nx, ny_screen, node_w, node_h,
                        PTC_OVERLAY_FONT_BODY, is_cardinal[i] ? pal.accent : pal.text_primary);
                }

                flow_y_ += compass_h + 6;

                // Independent Joy-Con X=0 and Y=9 face-button keycap row
                const int face_h = 46;
                const int face_gap = 10;
                const int face_w = (card_w - face_gap) / 2;
                const auto r0 = ptc_overlay_rect(card_x, flow_y_, face_w, face_h);
                page_layout_.pin_keys[8] = r0;
                auto vr0 = r0;
                vr0.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, vr0, "\uE0E2  0", pal.accent, false, PTC_OVERLAY_FONT_BODY);

                const auto r9 = ptc_overlay_rect(card_x + face_w + face_gap, flow_y_, card_w - face_w - face_gap, face_h);
                page_layout_.pin_keys[9] = r9;
                auto vr9 = r9;
                vr9.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, vr9, "\uE0E3  9", pal.accent, false, PTC_OVERLAY_FONT_BODY);
                flow_y_ += face_h + 6;
            } else {
                flow_text(renderer, ptc_ui_text(PTC_UI_T_TOUCH_NUMERIC_KEYPAD), PTC_OVERLAY_FONT_CAPTION, pal.text_secondary, 4);
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

            // Dial pad keeps Toggle + ZL Backspace side by side. Standard numpad
            // already has X Backspace on row 4, so Toggle spans the full width.
            if (!parent_pin_standard_keypad_) {
                const int btn_h = 46;
                const int toggle_w = 216;
                const int bs_w = card_w - toggle_w - 10;
                const auto toggle_rect = ptc_overlay_rect(card_x, flow_y_, toggle_w, btn_h);
                page_layout_.pin_keys[10] = toggle_rect;
                auto visible_toggle = toggle_rect;
                visible_toggle.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, visible_toggle,
                    ptc_ui_text(PTC_UI_T_SWITCH_TO_NUMPAD),
                    pal.accent, false, PTC_OVERLAY_FONT_SECONDARY, false);

                page_layout_.backspace = ptc_overlay_rect(card_x + toggle_w + 10, flow_y_, bs_w, btn_h);
                auto visible_bs = page_layout_.backspace;
                visible_bs.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, visible_bs, ptc_ui_text(PTC_UI_T_ZL_BACKSPACE_2),
                    pal.warning, false, PTC_OVERLAY_FONT_SECONDARY, false);
                flow_y_ += btn_h + 4;
            } else {
                const int btn_h = 44;
                const auto toggle_rect = ptc_overlay_rect(card_x, flow_y_, card_w, btn_h);
                page_layout_.pin_keys[10] = toggle_rect;
                page_layout_.backspace = {};
                auto visible_toggle = toggle_rect;
                visible_toggle.y -= static_cast<int>(page_scroll_);
                draw_button(renderer, visible_toggle,
                    ptc_ui_text(PTC_UI_T_SWITCH_TO_DIAL_PAD),
                    pal.accent, true, PTC_OVERLAY_FONT_SECONDARY,
                    parent_pin_focus_ == 0);
                flow_y_ += btn_h + 4;
            }

            end_page(renderer);
            // B Back lives in the Tesla host footer (native x < 150 touch hitbox),
            // letting Verify PIN span the full footer rail cleanly.
            page_layout_.secondary = {};
            page_layout_.primary = ptc_overlay_rect(cx, cy + PTC_OVERLAY_RAIL_Y, cw, PTC_OVERLAY_RAIL_H);
            const char *verify_label = ptc_ui_text(PTC_UI_T_VERIFY_PIN);
            draw_button(renderer, page_layout_.primary, verify_label, pal.accent, true,
                fit_rail_button_size(renderer, verify_label, page_layout_.primary.w),
                parent_pin_standard_keypad_ && parent_pin_focus_ == 14);
            return;
        }
        if (parent_view_ == ParentView::Actions) {
            const auto &vp = page_layout_.viewport;
            const int active_tab = parent_tab_ < 0 ? 0 : parent_tab_ > 2 ? 2 : parent_tab_;
            const int *order = ptc_overlay_parent_action_order();

            auto render_action_card = [&](int index, bool promoted) {
                const auto action = static_cast<PtcOverlayParentAction>(index);
                const char *reason = parent_action_reason(action);
                const char *value = nullptr;
                const char *note = reason;
                char action_note[512];
                tsl::Color value_color = reason ? pal.text_disabled : pal.text_primary;
                if (index == PTC_OVERLAY_PARENT_ADD_MINUTES) {
                    char min_buf[64];
                    ptc_ui_format_minutes(daily_add_minutes_, min_buf, sizeof(min_buf));
                    std::snprintf(line, sizeof(line), "< +%s >", min_buf);
                    value = line;
                    value_color = reason ? pal.text_disabled : pal.success;
                    if (!reason && !stale && displayed_summary_.valid &&
                        displayed_summary_.remaining_available && displayed_summary_.unrestricted_today != 1) {
                        std::snprintf(action_note, sizeof(action_note),
                            ptc_ui_text(PTC_UI_T_OVERLAY_QUICK_ADD_PREVIEW),
                            displayed_summary_.remaining_minutes,
                            displayed_summary_.remaining_minutes + daily_add_minutes_);
                    } else {
                        std::snprintf(action_note, sizeof(action_note), "%s%s%s",
                            ptc_ui_text(PTC_UI_T_OVERLAY_ADJUST_MINUTES), reason ? "\n" : "", reason ? reason : "");
                    }
                    note = action_note;
                } else if (index == PTC_OVERLAY_PARENT_SKIP_EYE_CARE && !reason &&
                           displayed_summary_.eye_care_rest_remaining_seconds > 0) {
                    ptc_ui_format_minutes((displayed_summary_.eye_care_rest_remaining_seconds + 59) / 60, line, sizeof(line));
                    value = line;
                }
                const int start = flow_y_;
                const auto tone = reason ? pal.text_disabled :
                    (promoted || parent_action_requires_hold(action)) ? pal.danger : pal.accent;
                page_layout_.actions[index] = flow_card(renderer, ptc_ui_text(parent_action_label_id(index)), value, note,
                    PTC_OVERLAY_FONT_BTN, value_color,
                    tone, action, parent_action_ == index, reason != nullptr,
                    PTC_OVERLAY_FONT_BODY, promoted);
                if (parent_action_ == index && !reason && parent_action_requires_hold(action)) {
                    auto rect = page_layout_.actions[index];
                    rect.y = start - static_cast<int>(page_scroll_);
                    draw_hold(renderer, rect, pal.danger);
                }
            };

            // 1. Active restriction override stays pinned above the tab bar
            if (promoted_parent_action_ >= 0 && promoted_parent_action_ < PTC_OVERLAY_PARENT_ACTION_COUNT) {
                render_action_card(promoted_parent_action_, true);
            }

            // 2. Enhanced Switch-native L/R 3-segment folder tab bar
            int tab_avail[3] = {0, 0, 0};
            for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot) {
                const int idx = order[slot];
                if (idx == promoted_parent_action_) continue;
                const int g = ptc_overlay_parent_action_group(slot);
                if (g >= 0 && g <= 2 && parent_action_reason(static_cast<PtcOverlayParentAction>(idx)) == nullptr) {
                    tab_avail[g]++;
                }
            }
            const int tab_raw_y = flow_y_;
            draw_parent_tab_bar(renderer, vp.x + 4, tab_raw_y - static_cast<int>(page_scroll_),
                vp.w - 8, active_tab, tab_avail, page_layout_.parent_tabs);
            for (int t = 0; t < 3; ++t) {
                page_layout_.parent_tabs[t].y = tab_raw_y + 2;
            }
            flow_y_ += 54;

            // Connected subtle tab pane hint line
            const PtcUiTextId tab_hints[3] = {
                PTC_UI_T_OVERLAY_GROUP_TODAY_ALLOWANCE_HINT,
                PTC_UI_T_OVERLAY_GROUP_ACTIVE_RULES_HINT,
                PTC_UI_T_OVERLAY_GROUP_SYSTEM_RECOVERY_HINT,
            };
            if (active_tab < 2) {
                const int hint_y = flow_y_ - static_cast<int>(page_scroll_);
                const int hint_h = overlay_text(renderer, ptc_ui_text(tab_hints[active_tab]),
                    vp.x + 12, hint_y, vp.w - 24, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
                flow_y_ += hint_h + 6;
            }

            // 3. Render only the active tab's actions
            if (active_tab == 0) {
                for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot) {
                    const int idx = order[slot];
                    if (idx == promoted_parent_action_) continue;
                    if (ptc_overlay_parent_action_group(slot) != 0) continue;
                    render_action_card(idx, false);
                }
            } else if (active_tab == 1) {
                int avail_list[PTC_OVERLAY_PARENT_ACTION_COUNT];
                int unavail_list[PTC_OVERLAY_PARENT_ACTION_COUNT];
                int avail_count = 0, unavail_count = 0;
                bool focused_in_unavail = false;
                for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot) {
                    const int idx = order[slot];
                    if (idx == promoted_parent_action_) continue;
                    if (ptc_overlay_parent_action_group(slot) != 1) continue;
                    if (parent_action_reason(static_cast<PtcOverlayParentAction>(idx)) == nullptr) {
                        avail_list[avail_count++] = idx;
                    } else {
                        unavail_list[unavail_count++] = idx;
                        if (parent_action_ == idx) focused_in_unavail = true;
                    }
                }
                for (int i = 0; i < avail_count; ++i) {
                    render_action_card(avail_list[i], false);
                }
                if (unavail_count > 0) {
                    const bool expanded = parent_unavailable_expanded_ || focused_in_unavail;
                    const auto drawer_raw = ptc_overlay_rect(vp.x + 4, flow_y_, vp.w - 12, 40);
                    page_layout_.parent_drawer = drawer_raw;
                    const int dy = drawer_raw.y - static_cast<int>(page_scroll_);
                    if (dy + drawer_raw.h > vp.y && dy < vp.y + vp.h) {
                        draw_round_rect_surface(renderer, drawer_raw.x, dy, drawer_raw.w, drawer_raw.h, 5,
                            pal.card_bg, expanded ? pal.border_control : pal.border_decorative, 1);
                        char drawer_lbl[128];
                        std::snprintf(drawer_lbl, sizeof(drawer_lbl),
                            ptc_ui_text(expanded ? PTC_UI_T_OVERLAY_UNAVAILABLE_OPEN : PTC_UI_T_OVERLAY_UNAVAILABLE_FOLDED),
                            unavail_count);
                        draw_localized_center(renderer, drawer_lbl, drawer_raw.x + 8, dy,
                            drawer_raw.w - 16, drawer_raw.h, PTC_OVERLAY_FONT_CAPTION,
                            expanded ? pal.accent : pal.text_secondary);
                    }
                    flow_y_ += drawer_raw.h + 6;
                    if (expanded) {
                        for (int i = 0; i < unavail_count; ++i) {
                            render_action_card(unavail_list[i], false);
                        }
                    }
                }
            } else {
                // Recovery tab: isolated high-risk warning banner + restore snapshot action
                const int wx = vp.x + 4;
                const int ww = vp.w - 12;
                const char *wt = ptc_ui_text(PTC_UI_T_OVERLAY_RECOVERY_WARN_TITLE);
                const char *wb = ptc_ui_text(PTC_UI_T_OVERLAY_RECOVERY_WARN_BODY);
                const int wt_h = overlay_text_height(renderer, wt, ww - 28, PTC_OVERLAY_FONT_BODY);
                const int wb_h = overlay_text_height(renderer, wb, ww - 28, PTC_OVERLAY_FONT_CAPTION);
                const int wh = 16 + wt_h + 4 + wb_h;
                const int wy = flow_y_ - static_cast<int>(page_scroll_);
                if (wy + wh > vp.y && wy < vp.y + vp.h) {
                    draw_round_rect_surface(renderer, wx, wy, ww, wh, 6, pal.danger_soft, pal.danger, 2);
                    renderer->drawRect(wx, wy + 6, 4, wh - 12, renderer->a(pal.danger));
                    overlay_text(renderer, wt, wx + 14, wy + 8, ww - 24, PTC_OVERLAY_FONT_BODY, pal.danger);
                    overlay_text(renderer, wb, wx + 14, wy + 12 + wt_h, ww - 24, PTC_OVERLAY_FONT_CAPTION, pal.text_primary);
                }
                flow_y_ += wh + 8;

                for (int slot = 0; slot < PTC_OVERLAY_PARENT_ACTION_COUNT; ++slot) {
                    const int idx = order[slot];
                    if (idx == promoted_parent_action_) continue;
                    if (ptc_overlay_parent_action_group(slot) != 2) continue;
                    render_action_card(idx, false);
                }
            }

            end_page(renderer);
            if (page_changed_ && parent_action_ >= 0 && page_layout_.actions[parent_action_].h > 0) {
                page_scroll_target_ = static_cast<float>(
                    ptc_overlay_scroll_to_parent_action(&page_layout_, parent_action_, 0));
            }
            // Action key hints (B Back / A Execute / Hold A 1s / Unavailable) live
            // in the Tesla host footer; the full 575px viewport stays for cards.
            page_layout_.primary = {};
            page_layout_.secondary = {};
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
            PtcFlowPath{{ptc_ui_text(PTC_UI_T_NAV_HOME), ptc_ui_text(PTC_UI_T_NAV_STATUS_DETAILS)}, 2},
            true, PTC_OVERLAY_FONT_TITLE, /*full_viewport=*/true);
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
            if (control_disabled())
                flow_text(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_DISABLED_HELP),
                    PTC_OVERLAY_FONT_CAPTION, pal.warning, 6);

            const auto policies = child_policy_status(summary);
            const auto eye = child_eye_care_status(summary);
            // Compact 4-guard health overview pills (Quota / Bedtime / Eye / Dock)
            {
                const auto &vp = page_layout_.viewport;
                const int pill_gap = 6;
                const int row_x = vp.x + 4;
                const int row_w = vp.w - 8;
                const int pill_w = (row_w - pill_gap * 3) / 4;
                const int pill_h = 30;
                const int py = flow_y_ - static_cast<int>(page_scroll_);
                const auto quota_col = control_disabled() ? pal.warning
                    : summary.daily_restriction_active ? pal.danger
                    : (stale || (summary.remaining_available && summary.unrestricted_today != 1 && summary.remaining_minutes <= 10))
                        ? pal.warning
                    : summary.valid ? pal.success : pal.text_disabled;
                const auto bed_col = bedtime_policy_color(policies.bedtime);
                const auto eye_col = eye_care_restricted() ? pal.danger
                    : eye.warning ? pal.warning
                    : summary.eye_care_enabled ? pal.success : pal.text_secondary;
                const auto dock_col = policy_color(policies.dock);
                const PtcUiTextId guard_ids[4] = {
                    PTC_UI_T_OVERLAY_GUARD_QUOTA,
                    PTC_UI_T_OVERLAY_GUARD_BEDTIME,
                    PTC_UI_T_OVERLAY_GUARD_EYE,
                    PTC_UI_T_OVERLAY_GUARD_DOCK,
                };
                const tsl::Color guard_cols[4] = {quota_col, bed_col, eye_col, dock_col};
                if (py + pill_h > vp.y && py < vp.y + vp.h) {
                    for (int i = 0; i < 4; ++i) {
                        const int px = row_x + i * (pill_w + pill_gap);
                        const int pw = (i == 3) ? (row_w - i * (pill_w + pill_gap)) : pill_w;
                        const bool muted = same_color(guard_cols[i], pal.text_secondary) ||
                                           same_color(guard_cols[i], pal.text_disabled);
                        draw_round_rect_surface(renderer, px, py, pw, pill_h, 6,
                            muted ? pal.card_bg : dashboard_soft(guard_cols[i]),
                            muted ? pal.border_decorative : semantic_border(guard_cols[i]), 1);
                        renderer->drawRect(px + 8, py + (pill_h - 6) / 2, 6, 6, renderer->a(guard_cols[i]));
                        draw_localized_center(renderer, ptc_ui_text(guard_ids[i]),
                            px + 16, py, pw - 20, pill_h, PTC_OVERLAY_FONT_CAPTION,
                            muted ? pal.text_secondary : pal.text_primary);
                    }
                }
                flow_y_ += pill_h + 6;
            }

            char label[128], value[128], note[128];
            int day, second;
            ptc_overlay_format_child_quota_parts(&summary, label, sizeof(label), value, sizeof(value), note, sizeof(note));
            const bool fresh = child_status_fresh(summary, day, second);
            child_card(renderer, label, value, note, PTC_OVERLAY_FONT_DISPLAY,
                fresh ? pal.success : pal.warning, PTC_OVERLAY_FONT_BODY_LG, ChildCardKind::Time);
            // Quota usage progress meter bar right under the daily quota card
            if (summary.valid && summary.unrestricted_today != 1 &&
                summary.daily_rule_available && summary.daily_rule_minutes > 0) {
                int used_permille = -1;
                if (summary.played_minutes_available && summary.played_minutes >= 0) {
                    used_permille = std::clamp(summary.played_minutes * 1000 / summary.daily_rule_minutes, 0, 1000);
                } else if (summary.remaining_available) {
                    const int used = std::max(0, summary.daily_rule_minutes - summary.remaining_minutes);
                    used_permille = std::clamp(used * 1000 / summary.daily_rule_minutes, 0, 1000);
                }
                if (used_permille >= 0) {
                    const auto bar_col = (summary.daily_restriction_active || used_permille >= 1000) ? pal.danger
                        : used_permille >= 800 ? pal.warning : pal.success;
                    const auto &vp = page_layout_.viewport;
                    const int py = flow_y_ - static_cast<int>(page_scroll_) - 2;
                    if (py + 8 > vp.y && py < vp.y + vp.h) {
                        draw_progress_bar(renderer, vp.x + 10, py, vp.w - 20, 8, used_permille, bar_col);
                    }
                    flow_y_ += 12;
                }
            }
            ptc_overlay_format_child_restriction_detail(&summary, line, sizeof(line));
            if (line[0]) flow_text(renderer, line, PTC_OVERLAY_FONT_BODY_LG, pal.warning, 6);

            // 2. Why: the three rules, promoting currently restricting rule(s) to the top.
            flow_section(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_GROUP_RULES), pal.accent);
            struct DetailRuleItem {
                const char *label;
                const char *val;
                const char *sub;
                tsl::Color color;
                ChildCardKind kind;
                bool active;
            };
            const DetailRuleItem items[3] = {
                {ptc_ui_text(PTC_UI_T_OVERLAY_EYE_LABEL), eye.compact, eye.detail,
                    eye.warning ? pal.warning : summary.eye_care_enabled ? pal.success : pal.text_secondary,
                    ChildCardKind::Eye, eye_care_restricted()},
                {ptc_ui_text(PTC_UI_T_POLICY_BED_LABEL), policies.bedtime.detail, nullptr,
                    bedtime_policy_color(policies.bedtime),
                    ChildCardKind::Bedtime, policies.bedtime.state == PTC_POLICY_ACTIVE},
                {ptc_ui_text(PTC_UI_T_POLICY_DOCK_LABEL), policies.dock.detail, nullptr,
                    policy_color(policies.dock),
                    ChildCardKind::Dock, summary.dock_restriction_active},
            };
            for (int pass = 0; pass < 2; ++pass) {
                for (int i = 0; i < 3; ++i) {
                    if (items[i].active == (pass == 0)) {
                        child_rule(renderer, items[i].label, items[i].val, items[i].sub,
                            items[i].color, items[i].kind);
                    }
                }
            }

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
        // B Back lives in the Tesla host footer; the full 575px viewport fits
        // all three core rule cards on the first screen without scrolling.
        page_layout_.primary = {};
        page_layout_.secondary = {};
    }

    void draw_keypad(tsl::gfx::Renderer *renderer, int cx, int cy, int cw)
    {
        const auto &pal = current_palette();
        const auto &summary = displayed_summary_;
        char line[128], age[64], date[64];
        begin_page(renderer, cx, cy, cw, ptc_ui_text(PTC_UI_T_ENTER_THE_EXTRA_TIME_CODE),
            PtcFlowPath{{ptc_ui_text(PTC_UI_T_NAV_HOME), ptc_ui_text(PTC_UI_T_NAV_EXTRA_TIME_CODE)}, 2}, true);
        if (control_disabled())
            std::snprintf(line, sizeof(line), "%s", current_state_line(false).text);
        else if (ptc_overlay_remaining_refresh_pending(bridge_->waiting, active_request_kind_ == OverlayRequestKind::OfflineCode))
            std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_CONFIRMING));
        else if (summary.valid && summary.unrestricted_today == 1) std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else if (summary.valid && summary.remaining_available) ptc_ui_format_minutes(summary.remaining_minutes, line, sizeof(line));
        else std::snprintf(line, sizeof(line), "%s", ptc_ui_text(PTC_UI_T_UNAVAILABLE));
        format_refresh_age(age, sizeof(age));
        const bool default_scheme = navbar_scheme_ == OverlayNavbarScheme::Default;
        const int meta_y = default_scheme ? 208 : 216;
        const int date_y = default_scheme ? 236 : 244;
        if (default_scheme) {
            // 3-step micro stepper + compact context strip above PTC_OVERLAY_SLOT_Y (112)
            draw_code_stepper(renderer, cx + 4, cy + 49, cw - 8, 0);
            const int strip_y = cy + 79;
            const int strip_h = 29;
            draw_round_rect_surface(renderer, cx + 4, strip_y, cw - 8, strip_h, 4,
                pal.card_bg, control_disabled() ? pal.warning_border : pal.border_decorative, 1);
            const int left_w = static_cast<int>(measure_localized(renderer, line, PTC_OVERLAY_FONT_BODY).first);
            const int age_w = static_cast<int>(measure_localized(renderer, age, PTC_OVERLAY_FONT_CAPTION).first);
            const bool show_age = left_w + age_w + 36 <= cw - 8;
            const auto line_color = control_disabled() ? pal.warning : pal.success;
            if (left_w <= cw - 28) {
                const int base_y = strip_y + static_cast<int>((strip_h + PTC_OVERLAY_FONT_BODY * 0.72f) / 2.0f);
                draw_localized(renderer, line, false, cx + 14, base_y, PTC_OVERLAY_FONT_BODY, renderer->a(line_color));
            } else {
                overlay_text(renderer, line, cx + 14, strip_y + 1, cw - 28, PTC_OVERLAY_FONT_BODY, line_color);
            }
            if (show_age) {
                const int age_base_y = strip_y + static_cast<int>((strip_h + PTC_OVERLAY_FONT_CAPTION * 0.72f) / 2.0f);
                draw_localized(renderer, age, false, cx + cw - 14 - age_w, age_base_y,
                    PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.text_secondary));
            }
        } else {
            const int status_y = 100;
            draw_localized(renderer, line, false, cx + 14, cy + status_y, PTC_OVERLAY_FONT_BODY,
                renderer->a(control_disabled() ? pal.warning : pal.success));
            const int left_width = static_cast<int>(measure_localized(renderer, line, PTC_OVERLAY_FONT_BODY).first);
            if (left_width + static_cast<int>(measure_localized(renderer, age, PTC_OVERLAY_FONT_SECONDARY).first) + 40 <= cw)
                draw_localized_right(renderer, age, cx + cw - 14, cy + status_y, PTC_OVERLAY_FONT_SECONDARY, pal.text_secondary);
        }
        for (unsigned i = 0; i < PTC_OVERLAY_CODE_SYMBOLS; ++i) {
            const auto slot = ptc_overlay_code_slot_rect(cx, cy, cw, i);
            const bool cursor = input_->length < PTC_OVERLAY_CODE_SYMBOLS && i == input_->length;
            const bool entered = i < input_->length;
            draw_round_rect_surface(renderer, slot.x, slot.y, slot.w, slot.h, 6,
                cursor ? pal.accent_soft : entered ? pal.success_soft : pal.card_bg,
                cursor ? pal.accent : entered ? pal.success_border : pal.border_control,
                (cursor || entered) ? 2 : 1);
            if (entered) {
                const char symbol[] = {input_->symbols[i], '\0'};
                draw_localized_center(renderer, symbol, slot.x, slot.y, slot.w, slot.h,
                    PTC_OVERLAY_FONT_CODE, pal.success);
            } else if (cursor) {
                /* Draw cursor bar with drawRect to stay crisp and avoid extra glyph cache entries. */
                renderer->drawRect(slot.x + (slot.w - 20) / 2, slot.y + slot.h - 14, 20, 3, renderer->a(pal.accent));
            } else {
                renderer->drawRect(slot.x + (slot.w - 4) / 2, slot.y + (slot.h - 4) / 2, 4, 4, renderer->a(pal.text_disabled));
            }
        }
        {
            /* Center 4-4 grouping dash drawn via drawRect (zero glyph cache cost). */
            const auto slot3 = ptc_overlay_code_slot_rect(cx, cy, cw, 3);
            const auto slot4 = ptc_overlay_code_slot_rect(cx, cy, cw, 4);
            const int gap_w = slot4.x - (slot3.x + slot3.w);
            const int dash_w = std::max(6, std::min(10, gap_w - 4));
            renderer->drawRect(slot3.x + slot3.w + (gap_w - dash_w) / 2,
                slot3.y + (slot3.h - 3) / 2, dash_w, 3, renderer->a(pal.accent));
        }
        std::snprintf(line, sizeof(line), ptc_ui_text(PTC_UI_T_OVERLAY_INPUT_COUNT), input_->length);
        format_console_date(summary, date, sizeof(date));
        const int count_w = static_cast<int>(measure_localized(renderer, line, PTC_OVERLAY_FONT_CAPTION).first);
        const int date_w = static_cast<int>(measure_localized(renderer, date, PTC_OVERLAY_FONT_CAPTION).first);
        if (count_w + date_w + 16 <= cw - 28) {
            const int combined_y = (meta_y + date_y) / 2;
            draw_localized(renderer, line, false, cx + 14, cy + combined_y, PTC_OVERLAY_FONT_CAPTION,
                renderer->a(input_->length > 0 ? pal.text_primary : pal.text_secondary));
            draw_localized_right(renderer, date, cx + cw - 14, cy + combined_y, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
        } else {
            draw_localized(renderer, line, false, cx + 14, cy + meta_y, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.text_secondary));
            draw_localized(renderer, date, false, cx + 14, cy + date_y, PTC_OVERLAY_FONT_CAPTION, renderer->a(pal.text_secondary));
        }
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
        // Parent entry and submit share the unified bottom rail; B Cancel lives
        // in the Tesla host footer (native x < 150 touch-back hitbox).
        end_page(renderer);
        page_layout_.secondary = {};
        page_layout_.parent = ptc_overlay_keypad_nav_parent_rect(cx, cy, cw);
        page_layout_.primary = ptc_overlay_submit_rect(cx, cy, cw);
        const bool unavailable = has_status_snapshot_ && !status_is_stale() &&
            (summary.unrestricted_today == 1 || summary.eye_care_unlimited_capped || summary.dock_unlimited_capped);
        const bool ready = !control_disabled() && !unavailable && !bridge_->waiting && ptc_overlay_input_can_submit(input_);
        char remaining[64];
        std::snprintf(remaining, sizeof(remaining), ptc_ui_text(PTC_UI_T_OVERLAY_NEED_CODE),
            static_cast<unsigned>(PTC_OVERLAY_CODE_SYMBOLS - input_->length));
        const char *submit = control_disabled() ? current_state_line(false).text :
            unavailable ? ptc_ui_text(PTC_UI_T_OVERLAY_CODE_UNAVAILABLE) :
            bridge_->waiting ? ptc_ui_text(PTC_UI_T_BACKGROUND_PROCESSING) :
            ready ? ptc_ui_text(PTC_UI_T_OVERLAY_SUBMIT) : remaining;
        const char *parent_label = ptc_ui_text(PTC_UI_T_R_PARENT_AREA);
        draw_button(renderer, page_layout_.parent, parent_label, pal.accent, false,
            fit_rail_button_size(renderer, parent_label, page_layout_.parent.w), keypad_nav_focus_ == 12);
        draw_button(renderer, page_layout_.primary, submit, ready ? pal.accent : unavailable ? pal.warning : pal.text_secondary,
            ready, fit_rail_button_size(renderer, submit, page_layout_.primary.w), keypad_nav_focus_ == 13, ready);
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
        int width = 0;
        overlay_walk_lines(renderer, text, size, 100000,
            [&](const char *, size_t, int measured, int index) { if (index == 0) width = measured; });
        return width;
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

    int measure_child_rule_chain_height(tsl::gfx::Renderer *renderer, const ChildRuleRow *rows,
        int count, bool compact_inactive = false) const
    {
        const auto &vp = page_layout_.viewport;
        const int avail = (vp.w - 8) - 16 - 12;
        int total = 0;
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
            const int base_h = (compact_inactive && row.level > 0) ? 38 : 48;
            const int height = inline_value ? base_h : std::max(base_h, 12 + title_h + 2 + value_h + 6);
            total += height + ((compact_inactive && row.level > 0) ? 4 : 6);
        }
        return total;
    }

    // Independent guard rule cards (left semantic bar + clean border, no fake timeline connector).
    void draw_child_rule_chain(tsl::gfx::Renderer *renderer, const ChildRuleRow *rows,
        int count, bool compact_inactive = false)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int x = vp.x + 4, w = vp.w - 8;
        const int text_x = x + 16, avail = w - 16 - 12;
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
            const int base_h = (compact_inactive && row.level > 0) ? 38 : 48;
            const int height = inline_value ? base_h : std::max(base_h, 12 + title_h + 2 + value_h + 6);
            const int y = flow_y_ - static_cast<int>(page_scroll_);
            flow_y_ += height + ((compact_inactive && row.level > 0) ? 4 : 6);
            if (y + height <= vp.y || y >= vp.y + vp.h) continue;

            const auto card_bg = row.level == 0 ? dashboard_soft(row.color) : pal.card_bg;
            const auto card_bd = row.level == 0 ? semantic_border(row.color) : pal.border_decorative;
            const auto bar_col = row.level == 2 ? pal.text_disabled :
                row.level == 1 ? pal.text_secondary : row.color;
            draw_round_rect_surface(renderer, x, y, w, height, 6, card_bg, card_bd, 1);
            renderer->drawRect(x, y + 6, 4, height - 12, renderer->a(bar_col));

            const auto title_color = row.level == 2 ? pal.text_disabled : pal.text_primary;
            const auto value_color = row.level == 0 ? row.color :
                row.level == 1 ? pal.text_secondary : pal.text_disabled;
            if (inline_value) {
                overlay_text(renderer, row.label, text_x, y + (height - title_h) / 2, title_w + 2,
                    title_size, title_color);
                overlay_text(renderer, row.value, x + w - 12 - value_w, y + (height - value_h) / 2,
                    value_w + 2, value_size, value_color);
            } else {
                overlay_text(renderer, row.label, text_x, y + 7, avail, title_size, title_color);
                overlay_text(renderer, row.value, text_x, y + 9 + title_h, avail, value_size, value_color);
            }
        }
    }

    /* Structured action guidance for restricted child pages using 4-bit palette
       tokens only; adapts cleanly to available viewport budget so all 3 languages
       keep scroll_max == 0 without dead black voids. */
    void draw_restricted_guidance(tsl::gfx::Renderer *renderer, bool dock_restricted,
        bool confirmed, const PtcCompanionResultSummary &summary, int avail_h)
    {
        const auto &pal = current_palette();
        const auto &vp = page_layout_.viewport;
        const int x = vp.x + 4, w = vp.w - 8;
        int used_h = 0;

        auto draw_guidance_card = [&](const char *title, const char *badge, const char *sub,
            tsl::Color bg, tsl::Color border, tsl::Color accent, bool with_dock_icon, int budget) -> int {
            const int icon_w = with_dock_icon ? 30 : 0;
            const int inner_w = w - 28 - icon_w;
            const int title_w = text_width(renderer, title, PTC_OVERLAY_FONT_SECONDARY);
            const int badge_w = (badge && *badge) ? text_width(renderer, badge, PTC_OVERLAY_FONT_CAPTION) : 0;
            const bool inline_badge = (badge && *badge) && (title_w + badge_w + 12 <= inner_w);
            const int title_h = overlay_text_height(renderer, title, inline_badge ? (title_w + 2) : inner_w, PTC_OVERLAY_FONT_SECONDARY);
            const int sub_h = (sub && *sub) ? overlay_text_height(renderer, sub, inner_w, PTC_OVERLAY_FONT_CAPTION) : 0;
            const int card_h = 14 + title_h + (sub_h ? 4 + sub_h : 0);
            if (card_h + 6 > budget) return 0;

            const int y = flow_y_ - static_cast<int>(page_scroll_);
            draw_dashboard_surface(renderer, ptc_overlay_rect(x, y, w, card_h), bg, border, false);
            renderer->drawRect(x, y + 6, 3, card_h - 12, renderer->a(accent));
            if (with_dock_icon) {
                draw_child_icon(renderer, ChildCardKind::Dock, x + 14, y + (card_h - 22) / 2, accent);
            }
            const int tx = x + 14 + icon_w;
            overlay_text(renderer, title, tx, y + 7, inline_badge ? (title_w + 2) : inner_w,
                PTC_OVERLAY_FONT_SECONDARY, pal.text_primary);
            if (inline_badge) {
                const int bh = (PTC_OVERLAY_FONT_CAPTION * 6 + 4) / 5;
                overlay_text(renderer, badge, x + w - 14 - badge_w, y + 7 + (title_h - bh) / 2,
                    badge_w + 2, PTC_OVERLAY_FONT_CAPTION, accent);
            }
            if (sub_h) {
                overlay_text(renderer, sub, tx, y + 7 + title_h + 4, inner_w,
                    PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
            }
            flow_y_ += card_h + 6;
            return card_h + 6;
        };

        if (dock_restricted) {
            char tv_sub[128];
            if (confirmed && summary.unrestricted_today == 1) {
                std::snprintf(tv_sub, sizeof(tv_sub), "%s", ptc_ui_text(PTC_UI_T_OVERLAY_TV_UNLIMITED_TODAY));
            } else if (confirmed && summary.remaining_available && summary.remaining_minutes > 0) {
                std::snprintf(tv_sub, sizeof(tv_sub), ptc_ui_text(PTC_UI_T_OVERLAY_TV_REMAINING_D_MIN),
                    summary.remaining_minutes);
            } else {
                std::snprintf(tv_sub, sizeof(tv_sub), "%s", ptc_ui_text(PTC_UI_T_OVERLAY_TV_CONTINUE_NOTE));
            }
            if (avail_h >= 146) {
                draw_child_section(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_HOW_TO_CONTINUE));
                used_h += 32;
            }
            used_h += draw_guidance_card(
                ptc_ui_text(PTC_UI_T_OVERLAY_DOCK_SELF_HELP_TITLE),
                ptc_ui_text(PTC_UI_T_OVERLAY_NO_PIN_NEEDED),
                tv_sub, pal.success_soft, pal.success_border, pal.success, true, avail_h - used_h);
            const int parent_used = draw_guidance_card(
                ptc_ui_text(PTC_UI_T_OVERLAY_PARENT_UNLOCK_SHORT),
                nullptr, nullptr, pal.accent_soft, pal.border_control, pal.accent, false, avail_h - used_h);
            used_h += parent_used;
            if (used_h == 0) {
                flow_text(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_ASK_PARENT), PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
            }
            return;
        }

        if (avail_h >= 106) {
            draw_child_section(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_HOW_TO_CONTINUE));
            used_h += 32;
        }
        const int card_used = draw_guidance_card(
            ptc_ui_text(PTC_UI_T_OVERLAY_PARENT_UNLOCK_SHORT),
            nullptr,
            ptc_ui_text(PTC_UI_T_OVERLAY_ASK_PARENT),
            pal.accent_soft, pal.border_control, pal.accent, false, avail_h - used_h);
        if (card_used == 0) {
            flow_text(renderer, ptc_ui_text(PTC_UI_T_OVERLAY_ASK_PARENT), PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
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
        const auto reason = child_home_reason();
        const bool bedtime = reason == PTC_REASON_BEDTIME;
        const bool eye_restricted = reason == PTC_REASON_EYE;
        const bool dock_restricted = reason == PTC_REASON_DOCK;
        const bool restricted = bedtime || eye_restricted || dock_restricted;
        int day, second;
        const bool confirmed = child_status_fresh(summary, day, second) && !bridge_->waiting;
        const auto eye = child_eye_care_status(summary);
        const auto policies = child_policy_status(summary);

        // Banner content ----------------------------------------------------
        const char *head = ""; tsl::Color tone = pal.success;
        const char *num = nullptr; int num_size = PTC_OVERLAY_FONT_HERO; tsl::Color num_color = pal.text_primary;
        const char *num_label = nullptr;
        const char *sub = nullptr; const char *note = nullptr;
        if (control_disabled()) {
            const auto state = current_state_line(false);
            head = state.text;
            tone = state.color;
            note = ptc_ui_text(PTC_UI_T_OVERLAY_DISABLED_HELP);
        } else if (bedtime) {
            head = ptc_ui_text(PTC_UI_T_OVERLAY_BEDTIME_CHILD_TITLE); tone = pal.night;
            sub = policies.bedtime.state == PTC_POLICY_ACTIVE ? policies.bedtime.detail :
                ptc_ui_text(PTC_UI_T_STATUS_PENDING_REFRESH);
            if (policies.bedtime.state == PTC_POLICY_ACTIVE || policies.bedtime.state == PTC_POLICY_WAITING) {
                std::snprintf(window, sizeof(window), ptc_ui_text(PTC_UI_T_OVERLAY_BEDTIME_CHILD_WINDOW),
                    summary.bedtime_start_minute / 60, summary.bedtime_start_minute % 60,
                    summary.bedtime_end_minute / 60, summary.bedtime_end_minute % 60);
                note = window;
            }
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
            const bool playable = confirmed && summary.ok &&
                (summary.restricted_now == 0 || (summary.temporary_unlocked_available && summary.temporary_unlocked));
            if (confirming) {
                head = ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY); tone = pal.warning;
                std::snprintf(number, sizeof(number), "%s", ptc_ui_text(PTC_UI_T_CONFIRMING));
                num = number; num_color = pal.warning;
            } else if (reason == PTC_REASON_DAILY && confirmed) {
                head = ptc_ui_text(PTC_UI_T_OVERLAY_QUOTA_DONE_TITLE); tone = pal.danger;
                sub = ptc_ui_text(PTC_UI_T_OVERLAY_QUOTA_DONE_VALUE);
            } else if (playable && summary.unrestricted_today == 1) {
                head = ptc_ui_text(PTC_UI_T_OVERLAY_NOW_CAN_PLAY); tone = stale ? pal.warning : pal.success;
                std::snprintf(number, sizeof(number), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
                num = number; num_label = ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY);
            } else if (playable && summary.remaining_available) {
                const bool low = summary.remaining_minutes <= 10;
                head = ptc_ui_text(summary.remaining_minutes == 0 ? PTC_UI_T_PLAYTIME_TODAY :
                    low ? PTC_UI_T_OVERLAY_NOW_LOW : PTC_UI_T_OVERLAY_NOW_CAN_PLAY);
                tone = stale || low ? pal.warning : pal.success;
                ptc_ui_format_minutes(summary.remaining_minutes, number, sizeof(number));
                num = number; num_label = ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY);
            } else {
                head = ptc_ui_text(PTC_UI_T_PLAYTIME_TODAY); tone = pal.text_secondary;
                std::snprintf(number, sizeof(number), "%s", ptc_ui_text(summary.valid && !playable ?
                    PTC_UI_T_STATUS_PENDING_REFRESH : PTC_UI_T_UNAVAILABLE));
                num = number; num_size = PTC_OVERLAY_FONT_BODY; num_color = pal.text_disabled;
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

        // Compute 4-state hero progress bar permille (0..1000, or -1 when not applicable)
        int hero_progress = -1;
        if (!control_disabled()) {
            if (eye_restricted) {
                if (eye.rest_seconds >= 0 && summary.eye_care_rest_minutes > 0) {
                    const int total_sec = summary.eye_care_rest_minutes * 60;
                    hero_progress = std::clamp(eye.rest_seconds * 1000 / total_sec, 0, 1000);
                }
            } else if (dock_restricted) {
                hero_progress = 1000;
            } else if (bedtime) {
                if (policies.bedtime.state == PTC_POLICY_ACTIVE) hero_progress = 1000;
            } else if (confirmed && summary.unrestricted_today != 1 && summary.remaining_available) {
                if (summary.daily_rule_available && summary.daily_rule_minutes > 0) {
                    const int denom = std::max(summary.daily_rule_minutes, summary.remaining_minutes);
                    hero_progress = std::clamp(summary.remaining_minutes * 1000 / denom, 0, 1000);
                } else if (summary.played_minutes_available && summary.played_minutes >= 0 &&
                           (summary.played_minutes + summary.remaining_minutes) > 0) {
                    hero_progress = std::clamp(summary.remaining_minutes * 1000 /
                        (summary.played_minutes + summary.remaining_minutes), 0, 1000);
                }
            }
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
            if (hero_progress >= 0) {
                if (draw) draw_progress_bar(renderer, x, y + 3, inner_w, 7, hero_progress, tone);
                y += 14;
            }
            if (sub) {
                if (draw) overlay_text(renderer, sub, x, y, inner_w, PTC_OVERLAY_FONT_BODY, pal.text_primary);
                y += overlay_text_height(renderer, sub, inner_w, PTC_OVERLAY_FONT_BODY) + 4;
            }
            if (note && *note) {
                if (draw) overlay_text(renderer, note, x, y, inner_w, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
                y += overlay_text_height(renderer, note, inner_w, PTC_OVERLAY_FONT_CAPTION);
            }
            return y + 10;
        };
        const int banner_bottom = walk(false);
        const auto banner = ptc_overlay_rect(cx, banner_y, cw, banner_bottom - banner_y);
        draw_round_rect_surface(renderer, banner.x, banner.y, banner.w, banner.h, 6,
            dashboard_soft(tone), semantic_border(tone), 2);
        renderer->drawRect(banner.x + 8, banner.y + 2, banner.w - 16, 3, renderer->a(tone));

        // Header row: brand left; language, parent area and refresh right.
        // Every control is measured and the brand yields the space they need,
        // so a third entry cannot push text outside the sidebar.
        const bool refresh_busy = bridge_->waiting;
        const char *refresh_text = ptc_ui_text(refresh_busy ? PTC_UI_T_REFRESHING : PTC_UI_T_Y_REFRESH);
        const char *parent_text = ptc_ui_text(PTC_UI_T_R_PARENT_AREA);
        const char *language_text = ptc_ui_text(PTC_UI_T_OVERLAY_LANG_BUTTON);
        const int refresh_w = std::max(text_width(renderer, ptc_ui_text(PTC_UI_T_Y_REFRESH), PTC_OVERLAY_FONT_SECONDARY),
            text_width(renderer, ptc_ui_text(PTC_UI_T_REFRESHING), PTC_OVERLAY_FONT_SECONDARY)) + 24;
        const int parent_w = text_width(renderer, parent_text, PTC_OVERLAY_FONT_SECONDARY) + 24;
        const int language_w = text_width(renderer, language_text, PTC_OVERLAY_FONT_SECONDARY) + 24;
        page_layout_.refresh = ptc_overlay_rect(cx + cw - 8 - refresh_w, cy, refresh_w, 40);
        page_layout_.parent = restricted ? ptc_overlay_rect(cx, cy + PTC_OVERLAY_RAIL_Y, cw, PTC_OVERLAY_RAIL_H) :
            ptc_overlay_rect(page_layout_.refresh.x - 6 - parent_w, cy, parent_w, 40);
        const int language_right = restricted ? page_layout_.refresh.x : page_layout_.parent.x;
        page_layout_.language = ptc_overlay_rect(language_right - 6 - language_w, cy, language_w, 40);
        const int brand_x = cx + pad_x, brand_w = page_layout_.language.x - brand_x - 8;
        const char *brand = ptc_ui_text(PTC_UI_T_PLAYWISE);
        // Native system fonts can be wider than the host preview font. Keep
        // the localized product name on one line instead of wrapping the
        // longer slogan beneath the controls.
        if (text_width(renderer, brand, PTC_OVERLAY_FONT_CAPTION) > brand_w)
            brand = ptc_ui_text(PTC_UI_T_NAV_HOME);
        const char *dot = std::strstr(brand, "\xC2\xB7");
        const int brand_top = cy + (40 - (PTC_OVERLAY_FONT_CAPTION * 6 + 4) / 5) / 2;
        if (dot && text_width(renderer, brand, PTC_OVERLAY_FONT_CAPTION) <= brand_w) {
            const std::string head_part(brand, dot);
            const int head_w = text_width(renderer, head_part.c_str(), PTC_OVERLAY_FONT_CAPTION);
            overlay_text(renderer, head_part.c_str(), brand_x, brand_top, brand_w, PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
            overlay_text(renderer, dot, brand_x + head_w, brand_top, std::max(0, brand_w - head_w),
                PTC_OVERLAY_FONT_CAPTION, pal.accent);
        } else if (text_width(renderer, brand, PTC_OVERLAY_FONT_CAPTION) <= brand_w) {
            overlay_text(renderer, brand, brand_x, brand_top, brand_w,
                PTC_OVERLAY_FONT_CAPTION, pal.text_secondary);
        }
        const bool highlight = needs_refresh() && !refresh_busy;
        draw_button(renderer, page_layout_.language, language_text, pal.text_secondary, false,
            PTC_OVERLAY_FONT_SECONDARY, child_overview_focus_ == 5);
        draw_button(renderer, page_layout_.parent, parent_text, pal.accent, restricted,
            PTC_OVERLAY_FONT_SECONDARY, child_overview_focus_ == (restricted ? 0 : 2));
        draw_button(renderer, page_layout_.refresh, refresh_text,
            refresh_busy ? pal.text_secondary : highlight ? pal.warning : pal.accent,
            highlight || refresh_busy, PTC_OVERLAY_FONT_SECONDARY, child_overview_focus_ == (restricted ? 3 : 4));
        walk(true);
        if (confirmed && (!bedtime || policies.bedtime.state == PTC_POLICY_ACTIVE) &&
            (!eye_restricted || eye.rest_seconds > 0))
            child_drawn_reason_ = reason;

        // Scrolling area: ask-a-parent hint, rule chain and the status row.
        const int button_h = PTC_OVERLAY_RAIL_H;
        const int bottom = cy + PTC_OVERLAY_RAIL_Y - 8;
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
            if (!control_disabled() && ((bedtime && i == 1) ||
                (eye_restricted && i == 0) || (dock_restricted && i == 2))) continue;
            rows[count++] = all_rows[i];
        }
        if (restricted) {
            const int rules_h = measure_child_rule_chain_height(renderer, rows, count, true);
            const int reserved_below = 32 + rules_h + 52;
            const int avail_h = page_layout_.viewport.h - reserved_below;
            draw_restricted_guidance(renderer, dock_restricted, confirmed, summary, avail_h);
        }
        draw_child_section(renderer, ptc_ui_text(restricted ? PTC_UI_T_OVERLAY_SECTION_OTHER_RULES :
            PTC_UI_T_OVERLAY_SECTION_RULES));
        draw_child_rule_chain(renderer, rows, count, restricted);
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
        const char *redeem_btn = ptc_ui_text(PTC_UI_T_OVERLAY_GRANT_CODE_BUTTON);
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

        end_page(renderer);
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
    }

    OverlayFooterSpec current_footer_spec() const
    {
        const auto &pal = current_palette();
        OverlayFooterSpec spec{};
        spec.left_color = pal.text_primary;
        spec.action_color = pal.text_primary;

        if (language_confirm_active_) {
            spec.left_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_CANCEL);
            spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_CONFIRM);
            spec.action_color = pal.accent;
            return spec;
        }
        if (access_recovery_visible()) {
            if (parent_view_ == ParentView::Pin) {
                spec.left_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_BACK);
                if (!parent_pin_standard_keypad_ || parent_pin_focus_ == 14) {
                    spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_VERIFY);
                    spec.action_color = pal.accent;
                } else {
                    spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_INPUT);
                }
                return spec;
            }
            if (parent_view_ == ParentView::Actions) {
                spec.left_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_BACK);
                if (parent_action_ < 0) {
                    spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_REFRESH);
                    spec.action_color = pal.accent;
                    return spec;
                }
                const auto selected = static_cast<PtcOverlayParentAction>(parent_action_);
                if (parent_action_reason(selected) != nullptr) {
                    spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_UNAVAILABLE);
                    spec.action_color = pal.warning;
                } else if (parent_action_requires_hold(selected)) {
                    spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_HOLD_A);
                    spec.action_color = pal.danger;
                } else if (selected == PTC_OVERLAY_PARENT_CLEAR_BEDTIME_SKIP ||
                           selected == PTC_OVERLAY_PARENT_WAIVE_DOCK) {
                    spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_CONTINUE);
                    spec.action_color = pal.accent;
                } else {
                    spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_EXECUTE);
                    spec.action_color = pal.accent;
                }
                return spec;
            }
            if (parent_view_ == ParentView::Confirm) {
                spec.left_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_CANCEL);
                const auto action = static_cast<PtcOverlayParentAction>(parent_action_);
                const bool immediate = parent_action_ != PTC_OVERLAY_PARENT_WAIVE_DOCK &&
                    displayed_summary_.bedtime_active && displayed_summary_.bedtime_skipped;
                if (parent_action_reason(action) != nullptr) {
                    spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_UNAVAILABLE);
                    spec.action_color = pal.warning;
                } else if (immediate) {
                    spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_HOLD_A);
                    spec.action_color = pal.danger;
                } else {
                    spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_CONFIRM);
                    spec.action_color = pal.accent;
                }
                return spec;
            }
            spec.left_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_BACK);
            spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_OK);
            return spec;
        }
        if (success_visible_) {
            spec.left_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_BACK);
            spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_OK);
            return spec;
        }
        if (preview_ready_) {
            spec.left_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_CANCEL);
            const auto level = ptc_overlay_preview_visual_level(
                preview_summary_.remaining_after_available,
                preview_summary_.remaining_after_minutes,
                preview_summary_.preview_capped,
                preview_summary_.converts_unlimited_to_limited);
            if (level == PTC_OVERLAY_PREVIEW_DANGER) {
                spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_HOLD_A);
                spec.action_color = pal.danger;
            } else {
                spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_CONFIRM);
                spec.action_color = pal.accent;
            }
            return spec;
        }
        if (status_expanded_) {
            spec.left_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_BACK);
            if (error_ || !has_status_snapshot_ || status_details_focus_ == 2) {
                spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_REFRESH);
                spec.action_color = pal.accent;
            } else {
                spec.action_text = ptc_ui_text(details_tech_expanded_ ?
                    PTC_UI_T_OVERLAY_NAV_COLLAPSE : PTC_UI_T_OVERLAY_NAV_EXPAND);
                spec.action_color = pal.accent;
            }
            return spec;
        }
        if (is_keypad_mode() && !child_is_restricted()) {
            spec.left_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_CANCEL);
            const auto &summary = displayed_summary_;
            const bool unavailable = has_status_snapshot_ && !status_is_stale() &&
                (summary.unrestricted_today == 1 || summary.eye_care_unlimited_capped || summary.dock_unlimited_capped);
            const bool ready = !control_disabled() && !unavailable && !bridge_->waiting &&
                input_ != nullptr && ptc_overlay_input_can_submit(input_);
            if (ready) {
                spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_SUBMIT);
                spec.action_color = pal.accent;
            } else if (keypad_nav_focus_ == 12) {
                spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_OK);
                spec.action_color = pal.accent;
            } else {
                spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_INPUT);
            }
            return spec;
        }
        spec.left_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_CLOSE);
        spec.action_text = ptc_ui_text(PTC_UI_T_OVERLAY_NAV_OK);
        return spec;
    }

    void draw_overlay(tsl::gfx::Renderer *renderer, s32 cx, s32 cy, s32 cw, s32 ch)
    {
        (void)ch;
        child_drawn_reason_ = PTC_REASON_NONE;
        prepare_page();
        // Child overview and details share the watcher; never interrupt code entry.
        if (!language_confirm_active_ && !access_recovery_visible() && !success_visible_ &&
            !preview_ready_ && !is_keypad_mode())
            observe_child_countdown();
        if (language_confirm_active_) draw_language_confirm(renderer, cx, cy, cw);
        else if (access_recovery_visible()) draw_parent_page(renderer, cx, cy, cw);
        else if (success_visible_) draw_code_success(renderer, cx, cy, cw);
        else if (preview_ready_) draw_code_preview(renderer, cx, cy, cw);
        else if (status_expanded_) draw_status_details(renderer, cx, cy, cw);
        else if (is_keypad_mode() && !child_is_restricted()) draw_keypad(renderer, cx, cy, cw);
        else draw_child_overview(renderer, cx, cy, cw);
    }
