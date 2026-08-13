#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SWITCH_UI_ROOTS = [ROOT / "companion" / "nro", ROOT / "companion" / "overlay" / "source"]
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}
FORBIDDEN = {
    "−": "use ASCII '-'",
    "－": "use ASCII '-'",
    "＋": "use ASCII '+'",
    "–": "write the range with Chinese '到'",
    "—": "use ASCII '-' or Chinese punctuation",
    "→": "describe the transition in Chinese",
    "←": "describe the direction in Chinese",
    "↔": "describe the switch in Chinese",
    "…": "use ASCII '...'",
    "·": "use ASCII separators or Chinese punctuation",
}


def main() -> int:
    failures: list[str] = []
    for root in SWITCH_UI_ROOTS:
        for path in sorted(root.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                for glyph, guidance in FORBIDDEN.items():
                    if glyph in line:
                        failures.append(f"{path.relative_to(ROOT)}:{line_number}: {glyph!r}: {guidance}")
    if failures:
        raise AssertionError("Switch UI contains unverified glyphs:\n" + "\n".join(failures))
    print("PASS: Switch UI glyph gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
