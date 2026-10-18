#!/usr/bin/env python3
"""Self-contained HTML review bundle for a capability-probe run (no deps).

Renders one page the operator can open once and judge: per scenario, the
captured screenshot filmstrip (objective metadata only -- index / captured_at /
sha), the device's own machine numbers as retained JSON, and a blank "待你判定"
block. It makes NO verdict: captions never say a restriction was seen; the page
is evidence for a human (or, if the operator chooses, an AI) to read at the end.

Pure Python string templating -- no Pillow/montage, matching the repo's
no-third-party-dependency rule for tools.
"""
from __future__ import annotations

import base64
import datetime
import html
import json
from pathlib import Path

BANNER = "本页仅客观采集留痕，未做任何判定。请逐屏核对后自行判定。"

STYLE = """
:root { color-scheme: light dark; }
body { font-family: system-ui, "Segoe UI", sans-serif; margin: 0; padding: 24px;
       line-height: 1.5; background: #11141a; color: #e6e8ec; }
h1 { font-size: 22px; margin: 0 0 4px; }
.banner { background: #3a2d12; border: 1px solid #a9833a; color: #f2d79a;
          padding: 10px 14px; border-radius: 8px; margin: 12px 0 24px; }
.scenario { border: 1px solid #2a2f3a; border-radius: 10px; padding: 16px;
            margin: 0 0 20px; background: #171b22; }
.scenario h2 { margin: 0 0 2px; font-size: 18px; }
.meta { color: #9aa3b2; font-size: 13px; margin: 0 0 12px; }
.films { display: flex; flex-wrap: wrap; gap: 10px; }
figure { margin: 0; width: 220px; }
figure img { width: 220px; height: auto; border: 1px solid #2a2f3a; border-radius: 6px;
             background: #000; }
figcaption { font-size: 11px; color: #9aa3b2; word-break: break-all; margin-top: 4px; }
details { margin-top: 12px; }
pre { white-space: pre-wrap; word-break: break-all; background: #0d0f14;
      border: 1px solid #2a2f3a; border-radius: 6px; padding: 10px; font-size: 12px; }
.judge { margin-top: 12px; padding: 10px 14px; border: 1px dashed #4a5061; border-radius: 8px; }
.judge ul { margin: 6px 0 0; padding-left: 20px; }
.tag { display: inline-block; font-size: 12px; padding: 1px 8px; border-radius: 10px;
       border: 1px solid #4a5061; color: #c2c8d2; }
"""

# <!-- APPEND-MARKER -->

def _esc(value) -> str:
    return html.escape("" if value is None else str(value))


def _img_src(probe_dir: Path, rel_path: str, inline: bool) -> str:
    if not inline:
        return _esc(rel_path)
    abs_path = probe_dir / rel_path
    try:
        data = abs_path.read_bytes()
    except OSError:
        return _esc(rel_path)
    return "data:image/jpeg;base64," + base64.b64encode(data).decode("ascii")


def _render_film(probe_dir: Path, shots, inline: bool) -> str:
    if not shots:
        return '<p class="meta">（本场景未采集截图）</p>'
    cells = []
    for shot in shots:
        rel = shot.get("path", "")
        sha = str(shot.get("sha256") or "")[:12]
        caption = f"{_esc(shot.get('checkpoint'))}<br>{_esc(shot.get('captured_at'))}<br>{_esc(sha)}"
        cells.append(
            f'<figure><img loading="lazy" src="{_img_src(probe_dir, rel, inline)}" '
            f'alt="{_esc(rel)}"><figcaption>{caption}</figcaption></figure>'
        )
    return '<div class="films">' + "".join(cells) + "</div>"


def _render_machine(machine) -> str:
    if not machine:
        return ""
    text = _esc(json.dumps(machine, ensure_ascii=False, indent=2, sort_keys=True))
    return f"<details><summary>设备机器数字（原样留存）</summary><pre>{text}</pre></details>"


def _render_judge(questions) -> str:
    items = "".join(f"<li>{_esc(q)}</li>" for q in (questions or []))
    if not items:
        items = "<li>逐屏核对本场景截图与机器数字，自行判定。</li>"
    return f'<div class="judge"><strong>待你判定</strong><ul>{items}</ul></div>'


def _render_scenario(probe_dir: Path, scenario: dict, inline: bool) -> str:
    name = _esc(scenario.get("title") or scenario.get("name"))
    status = _esc(scenario.get("status") or "captured")
    note = scenario.get("note") or ""
    note_html = f'<p class="meta">{_esc(note)}</p>' if note else ""
    return (
        '<section class="scenario">'
        f'<h2>{name} <span class="tag">{status}</span></h2>'
        f'{note_html}'
        f'{_render_film(probe_dir, scenario.get("screenshots") or [], inline)}'
        f'{_render_machine(scenario.get("machine"))}'
        f'{_render_judge(scenario.get("pending_questions"))}'
        '</section>'
    )


def render_report_html(probe_dir, scenarios, *, inline: bool = False,
                       generated_at: str | None = None, environment: dict | None = None) -> Path:
    """Write <probe_dir>/review.html from the scenario records and return it.

    ``scenarios`` is a list of dicts: name/title/status/note, screenshots
    (each {path, checkpoint, captured_at, sha256}; path is relative to
    probe_dir), machine (any JSON-able object kept verbatim), pending_questions.
    With ``inline`` the screenshots are base64-embedded so the page is a true
    single file; otherwise they are referenced by relative path.
    """
    probe_dir = Path(probe_dir)
    stamp = generated_at or datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    env_html = ""
    if environment:
        env_html = ("<details><summary>运行环境 / preflight</summary><pre>"
                    + _esc(json.dumps(environment, ensure_ascii=False, indent=2, sort_keys=True))
                    + "</pre></details>")
    body = "".join(_render_scenario(probe_dir, s, inline) for s in scenarios)
    doc = (
        "<!doctype html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>真机能力探测复核</title>"
        f"<style>{STYLE}</style></head><body>"
        "<h1>真机能力探测复核</h1>"
        f'<p class="meta">生成于 {_esc(stamp)}</p>'
        f'<div class="banner">{_esc(BANNER)}</div>'
        f"{env_html}{body}"
        "</body></html>\n"
    )
    out = probe_dir / "review.html"
    out.write_text(doc, encoding="utf-8", newline="\n")
    return out

