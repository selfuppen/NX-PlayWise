#!/usr/bin/env python3
"""Host regression for tools/probe_report.py.

Builds a probe directory with a dummy frame and one scenario, renders the
review bundle, and pins: screenshots appear by relative path (and base64 when
inlined), machine numbers and the sha/metadata are present, the "待你判定" block
and the no-verdict banner are present, and the page NEVER asserts a verdict
(no "已确认" / "通过"). No real device.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import probe_report  # noqa: E402

FAILURES = 0


def check(condition: bool, label: str) -> None:
    global FAILURES
    if not condition:
        FAILURES += 1
        print(f"FAIL: {label}", file=sys.stderr)


def _scenario() -> dict:
    return {
        "name": "restriction_quick",
        "title": "限制生效",
        "status": "captured",
        "note": "关浮窗后 15s 窗口盲抓",
        "screenshots": [
            {"path": "restriction_quick/00-baseline.jpg", "checkpoint": "baseline",
             "captured_at": "2026-10-11T00:00:00Z", "sha256": "abcdef0123456789" * 4},
            {"path": "restriction_quick/01.jpg", "checkpoint": "sampling-01",
             "captured_at": "2026-10-11T00:00:05Z", "sha256": "0f1e2d3c4b5a6978" * 4},
        ],
        "machine": {"lab/phase-5.json": {"1454": {"nanoseconds": 0}, "1455": {"value": True}}},
        "pending_questions": ["限时提示是否出现？", "游戏是否暂停/挂起？"],
    }


def test_render_relative_and_metadata() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        probe_dir = Path(tmp)
        (probe_dir / "restriction_quick").mkdir(parents=True)
        (probe_dir / "restriction_quick" / "00-baseline.jpg").write_bytes(b"\xff\xd8\xff\xd9")
        (probe_dir / "restriction_quick" / "01.jpg").write_bytes(b"\xff\xd8\xff\xd9")
        out = probe_report.render_report_html(probe_dir, [_scenario()])
        check(out.is_file() and out.name == "review.html", "writes review.html")
        text = out.read_text(encoding="utf-8")
        check('src="restriction_quick/01.jpg"' in text, "screenshot referenced by relative path")
        check("abcdef012345" in text, "sha prefix shown in caption")
        check("1454" in text and "nanoseconds" in text, "machine numbers rendered")
        check("待你判定" in text, "judgement block present")
        check(probe_report.BANNER in text, "no-verdict banner present")
        check("已确认" not in text, "page never claims a confirmed observation")
        check("通过" not in text, "page never claims a pass verdict")


def test_inline_embeds_base64() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        probe_dir = Path(tmp)
        (probe_dir / "restriction_quick").mkdir(parents=True)
        (probe_dir / "restriction_quick" / "00-baseline.jpg").write_bytes(b"\xff\xd8\xff\xd9")
        (probe_dir / "restriction_quick" / "01.jpg").write_bytes(b"\xff\xd8\xff\xd9")
        out = probe_report.render_report_html(probe_dir, [_scenario()], inline=True)
        text = out.read_text(encoding="utf-8")
        check("data:image/jpeg;base64," in text, "inline mode embeds base64 image data")


def main() -> int:
    test_render_relative_and_metadata()
    test_inline_embeds_base64()
    if FAILURES:
        print(f"{FAILURES} probe_report test(s) failed", file=sys.stderr)
        return 1
    print("PASS: probe_report review bundle renders evidence without a verdict")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
