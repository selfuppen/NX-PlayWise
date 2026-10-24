#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def lum(hex_val: int) -> float:
    r = ((hex_val >> 16) & 0xFF) / 255.0
    g = ((hex_val >> 8) & 0xFF) / 255.0
    b = (hex_val & 0xFF) / 255.0

    def chan(c: float) -> float:
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4

    return 0.2126 * chan(r) + 0.7152 * chan(g) + 0.0722 * chan(b)


def contrast_ratio(c1: int, c2: int) -> float:
    l1, l2 = lum(c1), lum(c2)
    lighter = max(l1, l2)
    darker = min(l1, l2)
    return (lighter + 0.05) / (darker + 0.05)


def parse_palette(c_code: str, name: str) -> dict[str, int]:
    pattern = rf"static\s+const\s+PtcUiPalette\s+{name}\s*=\s*\{{(.*?)\}};"
    match = re.search(pattern, c_code, re.DOTALL)
    assert match, f"Could not find palette {name}"
    body = match.group(1)
    result = {}
    for line in body.splitlines():
        line = line.strip()
        m = re.match(r"\.([a-zA-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+),?", line)
        if m:
            key = m.group(1)
            val_str = m.group(2)
            result[key] = int(val_str, 16) if val_str.startswith("0x") else int(val_str)
    return result


def parse_overlay_palette(hpp_code: str, name: str) -> dict[str, int]:
    pattern = rf"constexpr\s+OverlayPalette\s+{name}\s*=\s*\{{(.*?)\}};"
    match = re.search(pattern, hpp_code, re.DOTALL)
    assert match, f"Could not find {name} in render_types.hpp"
    body = match.group(1)
    overlay_colors = {}
    for line in body.splitlines():
        line = line.strip()
        m = re.match(
            r"\.([a-zA-Z0-9_]+)\s*=\s*\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\},?",
            line,
        )
        if m:
            field = m.group(1)
            r = int(m.group(2), 16) if m.group(2).startswith("0x") else int(m.group(2))
            g = int(m.group(3), 16) if m.group(3).startswith("0x") else int(m.group(3))
            b = int(m.group(4), 16) if m.group(4).startswith("0x") else int(m.group(4))
            overlay_colors[field] = (r * 17) << 16 | (g * 17) << 8 | (b * 17)
    return overlay_colors


def main() -> None:
    theme_c = (ROOT / "companion" / "nro" / "ui_theme.c").read_text(encoding="utf-8")
    overlay_hpp = (ROOT / "companion" / "overlay" / "source" / "render_types.hpp").read_text(encoding="utf-8")

    styles = [
        ("slate", "LIGHT_SLATE_PALETTE", "DARK_SLATE_PALETTE", "DARK_OVERLAY_SLATE_PALETTE"),
        ("indigo", "LIGHT_PALETTE", "DARK_PALETTE", "DARK_OVERLAY_PALETTE"),
        ("teal", "LIGHT_TEAL_PALETTE", "DARK_TEAL_PALETTE", "DARK_OVERLAY_TEAL_PALETTE"),
        ("amethyst", "LIGHT_AMETHYST_PALETTE", "DARK_AMETHYST_PALETTE", "DARK_OVERLAY_AMETHYST_PALETTE"),
    ]

    required_keys = [
        "page_bg", "surface", "surface_raised", "border_decorative", "border_control",
        "text_primary", "text_secondary", "text_disabled", "accent", "focus", "on_accent",
        "accent_soft", "hero", "on_hero", "hero_secondary", "coral", "success", "success_soft",
        "success_border", "warning", "warning_soft", "warning_border", "warning_text",
        "danger", "danger_soft", "danger_border", "night", "night_soft", "night_border"
    ]
    color_keys_to_check = [
        k for k in required_keys if not k.endswith("strength") and k != "scrim"
    ]

    field_map = {
        "card_bg": "surface",
        "card_raised": "surface_raised",
        "border_decorative": "border_decorative",
        "border_control": "border_control",
        "text_primary": "text_primary",
        "text_secondary": "text_secondary",
        "text_disabled": "text_disabled",
        "accent": "accent",
        "accent_soft": "accent_soft",
        "success": "success",
        "success_soft": "success_soft",
        "success_border": "success_border",
        "warning": "warning",
        "warning_soft": "warning_soft",
        "warning_border": "warning_border",
        "danger": "danger",
        "danger_soft": "danger_soft",
        "danger_border": "danger_border",
        "night": "night",
        "night_soft": "night_soft",
        "night_border": "night_border",
    }

    for style_name, light_name, dark_name, ov_name in styles:
        light = parse_palette(theme_c, light_name)
        dark = parse_palette(theme_c, dark_name)
        ov_colors = parse_overlay_palette(overlay_hpp, ov_name)

        # 1. 验证必须包含的关键字段
        for k in required_keys:
            assert k in light, f"Missing {k} in {light_name}"
            assert k in dark, f"Missing {k} in {dark_name}"

        # 2. 验证深色模式 4-bit 严格安全：每个颜色通道为 0x11 倍数
        for k in color_keys_to_check:
            val = dark[k]
            r = (val >> 16) & 0xFF
            g = (val >> 8) & 0xFF
            b = val & 0xFF
            assert r % 17 == 0 and g % 17 == 0 and b % 17 == 0, (
                f"{dark_name} field '{k}' = 0x{val:06X} is not 4-bit safe (r={r}, g={g}, b={b})"
            )

        # 3. 对比度核验
        assert contrast_ratio(dark["text_primary"], dark["surface"]) >= 7.0
        assert contrast_ratio(dark["text_secondary"], dark["surface"]) >= 4.5
        assert contrast_ratio(dark["border_control"], dark["surface"]) >= 3.0
        assert contrast_ratio(dark["accent"], dark["surface"]) >= 4.5
        assert contrast_ratio(dark["accent"], dark["accent_soft"]) >= 4.5
        assert contrast_ratio(dark["on_accent"], dark["accent"]) >= 4.5
        assert contrast_ratio(dark["success"], dark["success_soft"]) >= 4.5
        assert contrast_ratio(dark["warning_text"], dark["warning_soft"]) >= 4.5
        assert contrast_ratio(dark["danger"], dark["danger_soft"]) >= 4.5
        assert contrast_ratio(dark["night"], dark["night_soft"]) >= 4.5

        assert contrast_ratio(light["text_primary"], light["surface"]) >= 7.0
        assert contrast_ratio(light["text_secondary"], light["surface"]) >= 4.5
        assert contrast_ratio(light["border_control"], light["surface"]) >= 3.0
        assert contrast_ratio(light["accent"], light["surface"]) >= 4.5
        assert contrast_ratio(light["accent"], light["accent_soft"]) >= 4.5
        assert contrast_ratio(light["on_accent"], light["accent"]) >= 4.5
        assert contrast_ratio(light["success"], light["success_soft"]) >= 4.5
        assert contrast_ratio(light["warning_text"], light["warning_soft"]) >= 4.5
        assert contrast_ratio(light["danger"], light["danger_soft"]) >= 4.5
        assert contrast_ratio(light["night"], light["night_soft"]) >= 4.5

        # 4. 断言 Overlay 与 NRO 深色 100% 精确同值 (Delta = 0)
        for ov_k, nro_k in field_map.items():
            assert ov_k in ov_colors, f"Missing {ov_k} in {ov_name}"
            ov_val = ov_colors[ov_k]
            nro_val = dark[nro_k]
            assert ov_val == nro_val, (
                f"Color mismatch in style '{style_name}': Overlay {ov_name}.{ov_k} (0x{ov_val:06X}) vs NRO {dark_name}.{nro_k} (0x{nro_val:06X})"
            )

    print("PASS: ui palette contract verified successfully across all 4 theme styles (Graphite, Paper & Ink, Mist Teal, Dusk)")


if __name__ == "__main__":
    main()
