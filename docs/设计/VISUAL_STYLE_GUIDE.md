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


### 3.3 Classic 3-Section Console Shell Framing Contract
Across the Nintendo Switch 1280x720 global canvas, all main pages (Child Home, Parent Today) and second-level subpages (Time Plans, Settings, Offline Grants, Support & Recovery) uniformly follow the classic 3-section console shell framing:
1. **Top Header Bar (Y=0–104)**:
   * Anchored with a subtle elevated surface tint (blending 43% surface in Light mode, 47% surface in Dark mode), bounding the app icon, title, subtitle, and top-right time status capsule.
   * A full-width 1px hairline divider rule (border_decorative) spans across Y=104, establishing an architectural boundary.
   * **Status Capsule Breathing Contract**: Base position is Y=18, height 66px (vertical center Y=51 aligns harmoniously with the left app icon Y=49). When active policy rows (such as TV mode or bedtime restrictions) expand capsule height to 82px, the bottom edge is strictly clamped to Y<=94, ensuring at least 10px of clear breathing clearance to the divider line at Y=104, eliminating crowding.
2. **Central Main Stage (Y=105–651)**:
   * **Top Stage Buffer**: Navigation tabs and subpage back buttons start at Y=120 (height 38px, span Y=120~158), creating an airy 16px vertical breathing space from the top divider line (Y=104) and a balanced 14~18px space before content cards below (Y=172~176). Child mode summary and setup panels also align with Y=120, ensuring consistent rhythm across all views.
   * Feature cards (Y=172–628) are framed cleanly within this bounded stage with balanced breathing margins.
3. **Bottom Footer Bar (Y=652–720)**:
   * A full-width 1px hairline divider rule (border_decorative) spans across Y=652.
   * Y=653–719 provides a grounded dock bar surface tint (blending 70% surface_raised in Light mode, 55% surface_raised in Dark mode), with button prompts (Y=664, height 44px) perfectly vertically centered (12px top and bottom padding).
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

![Today: plan and status composite control preview](../images/usage-en/parent/today-planning-light.png)

### 4.4 Two-Stage Decision & Rule Matrix and Natural Guidance
The view moves beyond rigid and cramped layouts, adopting conversational, parent-friendly wording and an expansive, breathing visual architecture:

* **Top Verdict Banner**: Height 34px, utilizing soft semantic backgrounds (`UI_SUCCESS_SOFT` / `UI_WARNING_SOFT`) and full-color vector Emojis (✨ normal / 🌿 eye-care first) to provide an immediate plain-language verdict on whether gaming is currently permitted. An adaptive badge on the right highlights the next key milestone (e.g., bedtime at 21:00 or impending eye-care break).
* **Stage 1: Where Today's Quota Comes From (Pipeline)**:
  * **Direct Subtitle**: "Checked left to right — the first matching rule sets the daily quota."
  * **Four Rule Pipeline Cards**: "Today's Adjustment (P1)", "Specific Date (P2)", "Holiday (P3)", and "Weekly Plan (P4)". Each card features domain full-color vector Emojis (📅 / 📅 / 🎁 / 🕒) and high-contrast priority tags.
  * **Hero Active Rule Card**: The matching rule card stands out prominently with a light-green wash, 2px vivid green border, bold 16px quota digits, and an "Active Rule" badge; downstream overridden cards are cleanly dimmed with subtle borders.
  * **Cascade Arrows & Solid Downward Injection Arrow**: Horizontal directional arrows link the pipeline stages, cutting off downstream flow at the matching rule; the active card drops an indicator line and a solid triangle downward arrow (`support_draw_arrow_down`) into a full-width summary bar (with a ⏰ Emoji) confirming total approved minutes, rule source, played time, and remaining time.
* **Stage Transition Linker (Pipeline Flow Bridge)**: A spacious 68px vertical clearway featuring a centered causal pill (`① Determine daily quota ↓ Pass to ② 4 Guardians`), paired with an emphasized conduit line and a bold 20x14px solid triangle downward arrow with 38px of unobstructed channel beneath the text pill, powerfully injecting the quota into Stage 2 access evaluation without arrow obstruction.
* **Stage 2: Real-time Access & 4 Health Guardians**:
  * **Direct Subtitle**: "All 4 gates must show green; any trigger means pause and rest."
  * **Embedded Independent Protection Action Row**: An interactive row at the top of the module presenting "🛡️ Independent Protection: the 4 gates evaluate conditions independently without interference", with a `[ Rule Guide ❯ ]` pill button. Clicking or pressing X opens a fullscreen translucent modal dialog detailing the 4 core principles: independent protection, quota pipeline priority, convergence gate, and health baseline rules.
  * **Four Compact Guardian Cards (Height 76px)**:
    * **Total Daily Allowance** (`UI_ACCENT` blue bar + ⏰ Emoji + 14px bold remaining time + secondary used summary).
    * **Bedtime Schedule** (`UI_HERO` navy bar + 🌙 Emoji + 14px restriction time + overnight schedule).
    * **Eye-care Break** (`UI_SUCCESS` green bar + 🌿 Emoji + 14px elapsed/cycle metrics + rest countdown).
    * **Screen & Dock Mode** (`UI_WARNING` orange bar + 📺 Emoji + 14px mode display + remaining time).
    * Each card embeds a compact status pill in the top-right corner (green "✓ Pass", red "✕ Blocked", or gray "Off"), saving 40px of vertical space to accommodate long, clear downstream convergence channels.
  * **Long Convergence Leads & Master Injector Arrow**: Downward leads from all four cards drop 38px into a horizontal decision bus with status solder nodes. The bus center deploys a 50px vertical main channel and a bold 22x14px solid triangle downward arrow directly injecting into the centered master gate bar. Clear states show "All 4 gates green: healthy schedule clear, ready to play!", while any blockage displays a danger warning.
* **Adaptive Multi-Context Scaling**: In tighter modal viewports such as Forecast Day Details, card heights smoothly compress, uniformly rendering the `① Determine daily quota ↓ Pass to ② 4 Guardians` bridge pill, solid downward arrow, and independent protection interactive row, with the calendar explanatory card featuring a 📅 Emoji, ensuring consistent visual elegance across all screen modes.

![Decision and rules priority matrix preview](../images/usage-en/parent/parent-details-rules-light.png)

![Decision and rules restriction active preview](../images/usage-en/parent/parent-details-rules-restricted-light.png)

### 4.5 Tesla Overlay Current Design & Dark Baseline Contract
* **Tesla Host Dark Background**: The Tesla menu operates over a translucent dark background (`0x000D` / RGBA(0, 0, 0, 0.85)). To guarantee seamless integration without ocular strain, **the Tesla Overlay strictly locks to the Dark palette (`PTC_UI_RESOLVED_DARK`)**, never inheriting or following the Switch system Light theme.
* **Tuned Overlay Surfaces**:
  * Child and parent cards, code slots, keypad keys and buttons share closed rounded outlines (radius r=4 for cards/buttons, full half-height for pills). CPU span calculations render smooth rounded fills and outer borders; both normal 1px and emphasized 2px borders remain continuous without jagged seams.
  * Card Surface: `#131B2A` with high `0xE6` opacity, effectively shielding background game graphics.
  * Decorative Border: `#233047` with subtle `0xB0` opacity, providing restrained, clean edges.
  * Status Pills: Fully aligned with dark semantic rules (e.g. deep warm sepia warning `#332211`, dark jade success `#193C32`).
* **Font, Glyph & Handheld Legibility Contract**:
  * Due to Tesla sysmodule memory constraints (~6MB heap allocation), overlays cannot bundle external TTF/OTF fonts.
  * **Overlay text must never use Unicode 4-byte Emojis or miscellaneous Dingbats**, preventing mojibake or missing-glyph boxes on hardware.
  * Controller prompts must strictly use Nintendo Switch system font glyphs (e.g., `\uE0E0` for A button, `\uE0E1` for B button, `\uE0E2` for X button, `\uE0E3` for Y button, `\uE0E4` for L button, `\uE0E5` for R button, `\uE0E6` for ZL button, `\uE0EC` for touch/click).
  * The rendering layer includes a built-in UTF-8 runtime sanitizer (`ptc_overlay_sanitize_utf8`) that automatically strips out-of-range symbols before dispatching strings to `libtesla`.
  * **Unified large typography**: Tokens live in `companion/overlay/layout.h`: time/countdown 56px; key values and result titles 44px; page titles 40px; action labels and confirmation/restriction explanations 32px; primary buttons 30px; body 28px; supporting text and disabled reasons 24px; minimum captions 22px. Code slot digits use a separate 40px token; keypad digits use 36px.
* **Measured layouts prioritize readability**:
  * Stay inside the 448×720 sidebar, with content starting at Y=40 and ending at or above Y=647. Headers, refresh and bottom actions remain fixed; content scrolls in a separate scissored viewport.
  * Wrap Chinese at UTF-8 codepoint boundaries and English at word boundaries; split oversized words, preserve explicit newlines and blank lines. Keep important explanations complete instead of shrinking fonts or adding ellipses.
  * Measure localized text to determine card heights. Parent cards retain stacked text; child cards prefer measured edge-aligned rows and stack only when the texts cannot share a line. Production and Host previews share rendering; input consumes the renderer's `PtcOverlayPageLayout` instead of recreating touch coordinates.
* **Child overview, entry and explanations**:
  * **Dark dashboard surfaces**: Child pages use CPU pixel primitives for smooth rounded corners, thin top highlights and bounded geometric texture. Time cards use a dark blue-gray surface; rule cards carry a narrow state rail. Eye-care, bedtime and TV rule titles all use 32px, summaries 28px and supporting notes 24px. Blue eye, moon and TV icons are functional decoration, displayed only when measurement leaves room for the icon, title and summary on one line. Omit icons when space is limited; never shrink text or add height for decoration.
  * Available states use green. Unknown or boundary states, exhausted non-TV allowance while connected to TV, and bedtime within 15 minutes use bright orange. Active bedtime/TV restrictions and failures use coral red; eye-care rest uses orange and disabled rules use secondary gray. Restrictions and redemption outcomes use matching soft panels; pending outcomes use warning colors. Code slots and keypad keys use rounded corners, highlights and blue soft fills/borders for the insertion point, focused key and enabled submission. Parent pages share the rounded, highlighted surfaces while retaining their stacked layout and scrolling contract.
  * Add a measured eye-care row showing disabled, play minutes until rest, remaining rest time, paused or awaiting confirmation. Rest and confirmation use warning colors. Combine daily quota and estimated usage in the main time card's note. Status details show the full play/rest cycle and sleep timing explanation; the rest restriction keeps its 56px countdown and fixed Back/Parent buttons. At zero, wait for backend confirmation.
  * Common child pages prioritize a complete single-screen view while preserving primary large type. Use measured heights and 6px card gaps instead of the parent 104px minimum. Combine quota and usage, use compact rule summaries, and remove the duplicate redeem card. Redeem, buffer and parent shortcuts remain fixed. A / Plus or the entry button opens the keypad; B returns or closes.
  * Eight 44×64px code slots have 5px gaps and 40px digits. The 3×4 keypad uses 124×56px keys, a 64px row step and 36px digits. Count and console date occupy separate lines; digits, submit and navigation remain accessible on one screen.
  * Confirmation and outcome comparisons prefer measured inline values with reduced repetition and padding. Restriction pages retain large essential instructions, with Back and Parent fixed side by side. Common states fit on one screen in all three languages; full long rule explanations remain in status details, with scrolling reserved for details and oversized exceptional text. Minus opens independent status details from the overview or keypad; B / Minus returns.
* **Parent zone and scrolling contract**:
  * PIN entry, all eight actions, confirmation and result pages share dashboard surfaces and fixed button styling. Status, masked PIN count, confirmation explanation and result title use semantic soft panels. Action lists add group separators, optional functional icons and narrow color rails: blue for ordinary actions, coral for actions requiring hold confirmation, disabled gray for unavailable actions. Focus borders and fills follow these colors. Icons appear only when measurement leaves room on a single title line; they do not change card height. Pending, success and failure results use orange, green and coral respectively. Active TV restrictions show their corresponding status instead of normal availability.
  * Keep all eight actions and their three groups. Measured cards are at least 104px high, with 32px titles and 24px reasons. Quick-grant minutes and adjustment instructions occupy separate lines.
  * Focus scrolling uses actual card positions and shows the full selected card. Compute the scroll limit from measured content height. Right-stick scrolling is available outside PIN entry; explanatory pages also accept Up/Down.
  * Resolve taps on release. Movement over 10px cancels the tap and starts dragging. A drag or page transition cannot become a click; scrolled cards only receive touches inside the viewport.
  * PIN retains both sticks as digit input; touch dragging reads long instructions. Errors and cooldown appear near the entered PIN, and failed verification resets scrolling. Verify and Back remain fixed.
  * Page transitions reset scrolling; returning to parent actions follows the selected card. A 3px scrollbar indicates position. Existing authorization and one-second controller holds remain mandatory for dangerous actions.

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

The global status bar adapts to eye-care and policy rows, with its bottom no lower than y=104. Bedtime and TV summaries share two columns when both are enabled. The Overlay displays policies in individual large overview cards and complete scrollable status details. Input, submit, parent access and the Tesla footer must remain unobstructed. Boundary confirmation uses warning colors; active backend restrictions use danger colors. An empty non-TV balance on TV uses warning colors. Existing measured multilingual layouts, overflow handling, NRO themes and the Overlay dark baseline remain mandatory.
