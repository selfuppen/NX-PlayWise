# Visual Style and Design Guide

[English](VISUAL_STYLE_GUIDE.md) | [简体中文](视觉风格与设计指南.md) | [Interactive Preview (HTML)](theme_preview.html) | [Design Index](../README_en.md#design)

This guide defines the visual design system, color palette, and layout principles for PlayWise on Nintendo Switch. It provides clear standards for Companion NRO pages, settings menus, overlays, and detail panels to ensure consistent aesthetics, high readability, and reliable rendering across handheld and docked modes. Open [`theme_preview.html`](theme_preview.html) in your browser for an interactive preview of the full light/dark palette and UI components.

---

## 1. Design Philosophy & Runtime Contracts

### 1.1 Hardware & Viewing Distances
* **Handheld Mode (720p)**: 6.2 / 7.0-inch display viewed at 30–40 cm. Requires crisp typography (body $\ge$ 12px, captions $\ge$ 9px) and clear vector primitives with distinct contrast.
* **Docked Mode (1080p)**: TV screen viewed at 2–3 m. Requires moderate color saturation, zero harsh full-screen white glare, and prominent status pills and remaining-time indicators visible from afar.

### 1.2 Software Rasterization & Geometric Micro-Aesthetics
* **Pure CPU Software Rendering**: PlayWise relies on CPU rasterization directly to the frame buffer (no GPU shaders, no hardware MSAA).
* **Lightweight Geometric Micro-Texture**:
  * Cards and panels use smooth rounded rectangles (4px / 8px / 12px / 16px).
  * Icons are drawn procedurally as compact vector micro-icons (clock, crescent moon, eye, gamepad, calendar, gift, trend lines, etc.) with 1–2px line widths for crispness and performance.
  * Status pills use subtle 4px rounded corners for a modern, breathy, flat card feel.

### 1.3 Dual-Theme System Contract
All pages must adapt seamlessly to both color modes, matching system or user preferences:
* **Light Mode**: Crisp, airy, using white cards over cool tinted blue-gray backgrounds.
* **Dark Mode**: Low-glare, high-contrast, using deep navy-slate backgrounds paired with vibrant accents for eye comfort in dark environments.

---

## 2. Color Palette & Semantic System

### 2.1 Base Surface & Layering (`companion/nro/ui_theme.c`)

| Semantic Role | Light Mode | Dark Mode | Usage Description |
| :--- | :--- | :--- | :--- |
| **Page Background (`page_bg`)** | `#F0F3FA` | `#0B0F19` | Global canvas backdrop, cool-tinted to elevate cards |
| **Base Surface (`surface`)** | `#FFFFFF` | `#162032` | Top-level cards, main informational panels |
| **Raised Surface (`surface_raised`)** | `#E8EDF7` | `#1E2C44` | Secondary containers, result summary cards |
| **Decorative Border (`border_decorative`)** | `#DBE1E9` | `#2A3B54` | Subtle card dividers and panel strokes (1px) |
| **Control Border (`border_control`)** | `#79879D` | `#7E90AA` | Interactive buttons, inactive input frames |
| **Primary Text (`text_primary`)** | `#172640` | `#F3F6FD` | Key headlines, major numbers, body emphasis |
| **Secondary Text (`text_secondary`)** | `#53627A` | `#B6C4D9` | Subtitles, labels, auxiliary metrics |
| **Disabled Text (`text_disabled`)** | `#9199A5` | `#818C98` | Disabled actions, placeholders, inactive rules |

---

### 2.2 Semantic Status Colors

PlayWise follows modern UI principles pairing **saturated foregrounds with ultra-soft pastel backgrounds**. Muddy or grayish intermediate tones (such as legacy `#865600`) are strictly avoided.

#### A. Active / Success / Sufficient (Success)
* **Context**: Quota active, grant confirmed, rules satisfied, schedule running.
* **Light Mode**:
  * Soft Background (`UI_SUCCESS_SOFT`): `#F6FFED` (subtle mint)
  * Border: `#B7EB8F` (soft bright green, 1–2px)
  * Solid / Text / Foreground (`UI_SUCCESS`): `#52C41A` (vivid emerald) or `#16734E`
* **Dark Mode**:
  * Soft Background: `#193C32` / `#1B382B` (deep jade)
  * Border: `#274916`
  * Solid / Text / Foreground: `#5CCB8A` / `#49AA19` (high-visibility bright green)

#### B. Warning / Rest / Pending (Warning)
* **Context**: Eye-care rest underway, quota running low, pending changes, bedtime approaching.
* **Light Mode**:
  * Soft Background (`UI_WARNING_SOFT`): `#FFF7E6` (warm soft apricot/cream)
  * Border: `#F59E0B` (warm amber border, 1–2px, reinforcing outer contour)
  * Solid / Text / Foreground (`UI_WARNING`): `#D95A00` (high-contrast deep warm amber orange, resolving washed-out readability issues), deep emphasis `#B24400`
* **Dark Mode**:
  * Soft Background: `#332211` / `#40351F` (deep warm sepia)
  * Border: `#874D00`
  * Solid / Text / Foreground: `#FFA940` / `#F2C14E` (bright golden orange)

#### C. Restriction / Danger / Exhausted (Danger)
* **Context**: Daily limit reached, bedtime lock active, quota exhausted, safety error.
* **Light Mode**:
  * Soft Background (`UI_DANGER_SOFT`): `#FFE9ED` / `#FFF1F0` (light rose)
  * Border: `#FFA39E` (rose pink)
  * Solid / Text / Foreground (`UI_DANGER`): `#B92F46` / `#F5222D` (vivid ruby crimson)
* **Dark Mode**:
  * Soft Background: `#462936` (deep roseate)
  * Border: `#78061A`
  * Solid / Text / Foreground: `#FF7A85` / `#FF4D4F` (fluorescent coral red)

#### D. Brand Accent & Information (Accent / Info)
* **Context**: Focus rings, decision sequence pills, keyframe dots, default active segments.
* **Light Mode**: Accent `#245BC4` / `#1890FF`, Soft background `#E4EDFF` / `#BAE7FF`.
* **Dark Mode**: Accent `#91B9FF` / `#69C0FF`, Soft background `#273E60` / `#113559`.

---

## 3. Spatial Typography & Dynamic Alignment Contracts

To prevent text collisions across variable font widths and multiple localizations (English, Simplified/Traditional Chinese), the following rules must be maintained:

### 3.1 Edge-to-Edge Card Layout
In list rows, health-care cards, and decision cells, content must strictly separate into a **left-anchored zone** and a **right-anchored zone**:
```
+---------------------------------------------------------------------------------+
| [Icon] Title Text [Adaptive Dynamic Pill]             Right Metric & Detail Text |
| (Left-anchored: icon_x -> title_x -> pill_x)           (Right-anchored: align-R) |
+---------------------------------------------------------------------------------+
```
1. **Left-Anchoring (Dynamic Following)**:
   * Pills follow dynamically immediately after the title text. Never use hardcoded absolute X offsets.
   * Formula:
     $$\text{pill\_x} = \text{title\_x} + \text{measure\_text}(\text{title}) + 8$$
2. **Right-Anchoring (Right Alignment)**:
   * Metric and descriptive text must first measure its text width, then subtract from the right inner padding.
   * Formula:
     $$\text{desc\_x} = \text{card\_x} + \text{card\_width} - \text{padding\_right} - \text{measure\_text}(\text{desc})$$
   * This contract guarantees zero overlapping between short 4-character CJK words and longer 25-character European text strings.

### 3.2 Spacing & Grid System
* **Global Margin**: Fixed at 108px left and right; main cards occupy `1064px` width (centered across 1280px Switch resolution: $108 + 1064 + 108 = 1280$).
* **Card Padding**: Standard panels use 16px or 20px padding; compact sub-cards use 12px.
* **Vertical Rhythm**:
  * Major section gap: 16px / 20px
  * Header to sub-card gap: 12px
  * Inline icon-to-text gap: 6px / 8px

---

## 4. Key Component Standards

### 4.1 Hero Stat Cards
* **Grid**: 4 equal columns (Daily Quota, Remaining Time, Continuous Session, Current State).
* **Hierarchy**:
  * Top label: 8–9px muted uppercase tag (`UI_MUTED`).
  * Big metric: 20–24px bold figure (`UI_ACCENT` or semantic color).
  * Bottom badge: 4px rounded pill or micro-icon conveying status.

### 4.2 24-Hour Activity Track
* **Track Slot**: Height 14–18px, corner radius 8px, background `UI_RAISED` or `0xE0E6F0`.
* **Segments**:
  * **Played Segment**: Brand accent (`UI_ACCENT`).
  * **Bedtime Lock Segment**: Dark slate / danger-soft diagonal strip with crescent moon icon.
  * **Remaining Playable**: Soft mint (`#B7EB8F`) or translucent accent.
* **Current Time Needle**: Vertical high-contrast line with subtle outer shadow and indicator tip.

### 4.3 Continuous Play Forecast Bar
* **Alternating Session & Rest Chunks**:
  * Play Session: Vivid blue (`#1890FF` / `#245BC4`), rounded rectangle.
  * Rest Period: Warm amber (`#FFF7E6` bg + `#FA8C16` border), displaying rest duration clearly.
  * Exhaustion Point: Pulsing micro-circle paired with an endpoint time balloon.

### 4.4 Decision & Rule Priority Matrix
* **Active Selected Rule**: 2px stroke in `UI_SUCCESS`, soft green background (`UI_SUCCESS_SOFT`), embedded "Active" pill.
* **Fallback Unhit Rules**: 1px border in `UI_BORDER`, canvas-blended background (`UI_PAGE`), muted text.
* **Priority Arrows**: 2px directional arrows between rule stages illustrating evaluation flow (Temporary $\to$ Specified Date $\to$ Holiday $\to$ Weekly Plan $\to$ Final Balance).

### 4.5 Tesla Overlay Architecture & Dark Baseline Contract
* **Tesla Host Dark Background**: The Tesla menu operates over a translucent dark background (`0x000D` / RGBA(0, 0, 0, 0.85)). To guarantee seamless integration without ocular strain, **the Tesla Overlay strictly locks to the Dark palette (`PTC_UI_RESOLVED_DARK`)**, never inheriting or following the Switch system Light theme.
* **Tuned Overlay Surfaces**:
  * Card Surface: `#131B2A` with high `0xE6` opacity, effectively shielding background game graphics.
  * Decorative Border: `#233047` with subtle `0xB0` opacity, providing restrained, clean edges.
  * Status Pills: Fully aligned with dark semantic rules (e.g. deep warm sepia warning `#332211`, dark jade success `#193C32`).
* **Font, Glyph & Handheld Legibility Contract**:
  * Due to Tesla sysmodule memory constraints (~6MB heap allocation), overlays cannot bundle external TTF/OTF fonts.
  * **Overlay text must never use Unicode 4-byte Emojis or miscellaneous Dingbats**, preventing mojibake or missing-glyph boxes on hardware.
  * Controller prompts must strictly use Nintendo Switch system font glyphs (e.g., `\uE0E0` for A button, `\uE0E1` for B button, `\uE0E2` for X button, `\uE0E3` for Y button, `\uE0E4` for L button, `\uE0E5` for R button, `\uE0E6` for ZL button, `\uE0EC` for touch/click).
  * The rendering layer includes a built-in UTF-8 runtime sanitizer (`ptc_overlay_sanitize_utf8`) that automatically strips out-of-range symbols before dispatching strings to `libtesla`.
  * **Unified Typography Scale Tokens**:
    To eliminate ad-hoc font size fragmentation, all font sizes are centralized in `companion/overlay/layout.h`:
    * `PTC_OVERLAY_FONT_HERO = 48`: Hero numeric values (glanceable playtime numerals `45 分钟`, eye-care countdown `02:00`);
    * `PTC_OVERLAY_FONT_DISPLAY = 32`: Prominent card values & key comparisons (grant code slots, before/after values);
    * `PTC_OVERLAY_FONT_KEYPAD = 30`: Numeric keypad & directional keycaps (3x4 digits, PIN directions);
    * `PTC_OVERLAY_FONT_TITLE_LG = 28`: Critical alert titles & primary outcomes (restriction cards, success titles);
    * `PTC_OVERLAY_FONT_TITLE = 26`: Module headers & brand titles (PlayWise brand, parent zone/PIN header, confirm titles);
    * `PTC_OVERLAY_FONT_BTN = 22`: Primary action buttons (50–56px high buttons: Confirm, Cancel, Redeem banner);
    * `PTC_OVERLAY_FONT_BODY_LG = 20`: High-emphasis body & card labels (policy card labels, action card titles);
    * `PTC_OVERLAY_FONT_BODY = 18`: Comfortable body text & secondary actions (policy status details, navigation items);
    * `PTC_OVERLAY_FONT_SECONDARY = 16`: Ancillary notes & status summaries (hints, card subtitles, warning reasons);
    * `PTC_OVERLAY_FONT_CAPTION = 15`: Compact captions & minimum legible floor (strictly $\ge 15\text{px}$ to avoid stroke loss).
* **Scheme A Dual-Mode Architecture (Smart Focus & Adaptive Collapsible Keypad)**:
  * **Top Title Reclaimed**: Reclaims the 90px header banner occupied by the default frame, placing a compact 34px brand & refresh bar at the top; content starts at $Y=40$ with total content height expanded to 607px.
  * **Mode 1: Overview Mode (Daily High-Frequency Default)**: Focuses on glanceable status: Hero card with 48px time readout, two rule cards (20px label + 18px detail), grant code trigger banner (22px title + 18px subtitle), autonomous buffer and parent zone buttons (52px high, 18–20px text). Pressing A, D-Pad, Plus, or touching the code trigger card smoothly unfolds Keypad Mode; pressing B closes the overlay.
  * **Mode 2: Keypad Mode (Comfortable Code Entry)**: Collapses policy cards into a summary bar, displaying a 3x4 spacious numeric keypad ($124\text{px} \times 48\text{px}$, 30px digits) and enlarged 8-slot input ($44\text{px} \times 54\text{px}$, 32px glyphs); pressing B or touching [B Cancel] smoothly returns to Overview Mode.
  * **Mode 3: Restricted Hero Mode (Scenario-Aware Alert)**: Adapts into a full-height alert hero card (28px title, 48px countdown, 18px explanation) when bedtime, eye care, or dock restrictions are active, with direct one-touch parent area access.
* **Parent Zone Natural Vertical Scrolling & Scissoring Contract**:
  * **Expanded Action Cards & Natural Overflow**: Action card heights are enlarged to 50px (58px step), titles to 20px (18px when accompanied by reasons), reasons/descriptions to 15px, and group headings to 18px. The 8 action cards across 3 groups span 588px total height, naturally overflowing the visible viewport height (484px).
  * **Viewport Scissoring Protection**: Renders within a scissored box ($[Y+72, H=484]$) via `enableScissoring` and `disableScissoring`, completely preventing clipping artifacts onto headers or footers.
  * **Smooth Auto-Scroll to Focus**: When navigating with D-pad or sticks, `parent_scroll_y_` smoothly interpolates to bring the focused action into view (`ptc_overlay_parent_scroll_for_action`); manual scrolling via the right analog stick is also supported.
  * **Virtual Touch Hit-Testing**: Touch clicks map virtual coordinates via $vty = ty + parent\_scroll\_y\_$ for pixel-accurate item selection and execution.
  * **Micro-Geometric Scrollbar Indicator**: A 3px semi-transparent scroll track and thumb dynamically render along the right viewport margin to signal scroll depth and progress.

---

## 5. Development Checklist for New Pages

The parent today page is named “今日安排” in both Chinese locales and `Today's Schedule` in English; detail titles and return prompts use the same name. Parent-entry PIN errors use the existing 760px by 360px dialog with theme-aware danger colors. Its three buttons are `B Back`, `X Support & Recovery`, and `A Re-enter`. During cooldown, retry shows remaining seconds and is disabled; back and support remain available. Measure and fit multilingual text, and share geometry between drawing and touch. The entry support button opens read-only Support & Recovery; other sensitive actions retain their troubleshooting guide button.

When authoring or updating Companion NRO pages or overlays:

- [ ] **Dual-Theme Compatible (NRO)**: All colors resolve through `palette->xxx`, `UI_xxx`, or dark/light ternaries; no hardcoded single-mode colors.
- [ ] **Overlay Dark Baseline**: Overlay strictly locks to the dark palette, with surfaces tuned to Tesla's translucent black background.
- [ ] **Overlay Zero-Emoji Contract**: Overlay strings contain no 4-byte emojis; controller prompts strictly use official Switch font glyphs (`\uE0E0`, etc.).
- [ ] **Contrast & Luminance**: Text passes legibility standards on handheld screens (Light mode primary text $\ge$ `#172640`, Dark mode primary text $\ge$ `#F3F6FD`).
- [ ] **Semantic Color Harmony**: Active/success uses fresh green (`#52C41A` / `#F6FFED`), rest/warning uses high-contrast warm amber (`#D95A00` / `#FFF7E6`), limits use crimson (`#B92F46` / `#FFE9ED`). Avoid muddy yellow/brown tones.
- [ ] **Dynamic Text Measurement**: Adjacent items and badges calculate positions dynamically using `measure_text(...)`.
- [ ] **Edge-to-Edge Right Alignment**: Right-hand descriptions and metrics align strictly via `card_x + card_w - padding - desc_w`.
- [ ] **Text Overflow Protection**: Long strings or localized variations use `fit_text(...)` with ellipses to prevent layout breaking.
- [ ] **Gamepad Focus Feedback**: Selected elements have distinct 2–3px highlight borders (`UI_ACCENT`) matching Nintendo Switch controller navigation.

## 6. Policy information in overviews

The child hero uses four compact 56px cards for quota, eye care, bedtime and TV, preserving the primary allowance and weekly forecast. Bedtime retains its clock and countdown. TV prioritizes the remaining usage and end-of-allowance guidance, followed by mode, allowance and estimated use. The parent's TV card is 70px high and retains existing touch and focus-return behavior.

The global status bar adapts to eye-care and policy rows, with its bottom no lower than y=104. Bedtime and TV summaries share two columns when both are enabled. The collapsed Overlay adds two policy rows below its bottom controls (content-relative y=510, height 46); expanded status puts those rows inside a 136px detail card. Input, submit, parent access and the Tesla footer must remain unobstructed. Boundary confirmation uses warning colors; active backend restrictions use danger colors. An empty non-TV balance on TV uses warning colors. Existing measured multilingual layouts, overflow handling, NRO themes and the Overlay dark baseline remain mandatory.
