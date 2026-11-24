#!/usr/bin/env python3
"""Verify Markdown relative link, heading anchor, and index coverage across the repo."""
from __future__ import annotations

import os
from pathlib import Path
import re
import tempfile
import urllib.parse


ROOT = Path(__file__).resolve().parents[2]
SKIP_DIRS = {".git", "build", "__pycache__", "node_modules", "vendor", "device-reports", ".venv", "venv"}
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
FENCED_RE = re.compile(r"```.*?```", re.S)
HEADING_RE = re.compile(r"^(#{1,6})\s+(.+?)\s*#*$")

# Unversioned local device/build artifacts referenced inside historical records
# are intentionally excluded from git and therefore allowed to be absent.
HISTORICAL_PREFIXES = (
    "docs/开发/验收记录/",
    "docs/调研/历史调查/",
)
UNVERSIONED_ARTIFACT_ROOTS = ("device-reports/", "build/")

# Root entry indexes and legacy help-URL redirect stubs do not require incoming links.
ORPHAN_EXEMPT = {
    "docs/README.md",
    "docs/README_en.md",
    "docs/自制节假日日历格式.md",
    "docs/CUSTOM_HOLIDAY_CALENDAR_FORMAT.md",
}


def github_heading_slug(heading: str) -> str:
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", heading.strip().lower()).replace("`", "")
    kept = [
        ch for ch in text
        if ch.isalnum() or ch in (" ", "-", "_") or "\u4e00" <= ch <= "\u9fff"
    ]
    return "".join(kept).replace(" ", "-")


def extract_heading_slugs(md_path: Path) -> set[str]:
    slugs: set[str] = set()
    counts: dict[str, int] = {}
    in_code = False
    for line in md_path.read_text(encoding="utf-8").splitlines():
        if line.strip().startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            continue
        match = HEADING_RE.match(line)
        if not match:
            continue
        base = github_heading_slug(match.group(2))
        if not base:
            continue
        seen = counts.get(base, 0)
        counts[base] = seen + 1
        slugs.add(base if seen == 0 else f"{base}-{seen}")
    return slugs


def collect_markdown_files(repo_root: Path) -> list[Path]:
    files = list(repo_root.glob("*.md"))
    for sub in ("docs", "tools"):
        base = repo_root / sub
        if not base.exists():
            continue
        for dp, dns, fns in os.walk(base):
            dns[:] = sorted(d for d in dns if d not in SKIP_DIRS)
            for fn in sorted(fns):
                if fn.endswith(".md"):
                    files.append(Path(dp) / fn)
    return sorted(set(files))


def check_docs_integrity(repo_root: Path) -> tuple[list[str], list[str], list[str]]:
    md_files = collect_markdown_files(repo_root)
    incoming: dict[str, set[str]] = {
        p.relative_to(repo_root).as_posix(): set() for p in md_files
    }
    slug_cache: dict[Path, set[str]] = {}
    broken_paths: list[str] = []
    broken_anchors: list[str] = []

    for md in md_files:
        rel_src = md.relative_to(repo_root).as_posix()
        is_historical = rel_src.startswith(HISTORICAL_PREFIXES)
        text = FENCED_RE.sub("", md.read_text(encoding="utf-8"))
        for match in LINK_RE.finditer(text):
            raw = match.group(1).strip()
            if not raw or raw.startswith(("http://", "https://", "mailto:", "file://")):
                continue
            target = raw.split()[0]
            if "#" in target:
                path_part, anchor_part = target.split("#", 1)
            else:
                path_part, anchor_part = target, ""

            if not path_part:
                resolved = md.resolve()
            else:
                decoded = urllib.parse.unquote(path_part)
                resolved = (md.parent / decoded).resolve()
                if not resolved.exists():
                    try:
                        rel_missing = resolved.relative_to(repo_root.resolve()).as_posix()
                    except ValueError:
                        rel_missing = ""
                    if is_historical and rel_missing.startswith(UNVERSIONED_ARTIFACT_ROOTS):
                        continue
                    broken_paths.append(f"{rel_src} -> {raw}")
                    continue
                try:
                    rel_dst = resolved.relative_to(repo_root.resolve()).as_posix()
                except ValueError:
                    rel_dst = ""
                if rel_dst in incoming and rel_dst != rel_src:
                    incoming[rel_dst].add(rel_src)

            if anchor_part and not is_historical and resolved.suffix.lower() == ".md":
                if resolved not in slug_cache:
                    slug_cache[resolved] = extract_heading_slugs(resolved)
                anchor_slug = urllib.parse.unquote(anchor_part).lower()
                if anchor_slug not in slug_cache[resolved]:
                    broken_anchors.append(f"{rel_src} -> {raw}")

    orphans = [
        path
        for path, sources in sorted(incoming.items())
        if path.startswith("docs/")
        and path not in ORPHAN_EXEMPT
        and not sources
    ]
    return broken_paths, broken_anchors, orphans


def test_repo_docs_integrity() -> None:
    broken_paths, broken_anchors, orphans = check_docs_integrity(ROOT)
    assert not broken_paths, "Broken markdown links found:\n  " + "\n  ".join(broken_paths)
    assert not broken_anchors, "Broken markdown anchors found:\n  " + "\n  ".join(broken_anchors)
    assert not orphans, "Unindexed orphan docs found:\n  " + "\n  ".join(orphans)


def test_detects_broken_links_anchors_and_orphans() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-docs-check-") as tmp:
        root = Path(tmp)
        (root / "docs" / "开发" / "验收记录").mkdir(parents=True)
        (root / "docs" / "README.md").write_text(
            "[ok](开发/guide.md#usage) [bad-anchor](开发/guide.md#missing) "
            "[bad-file](开发/missing.md) [old](开发/验收记录/old.md)\n",
            encoding="utf-8",
        )
        (root / "docs" / "开发" / "guide.md").write_text("# Guide\n\n## Usage\n", encoding="utf-8")
        (root / "docs" / "开发" / "orphan.md").write_text("# Orphan\n", encoding="utf-8")
        # Historical unversioned device-reports reference should be allowed.
        (root / "docs" / "开发" / "验收记录" / "old.md").write_text(
            "[raw](../../../device-reports/run1/audit.json)\n",
            encoding="utf-8",
        )
        broken_paths, broken_anchors, orphans = check_docs_integrity(root)
        assert broken_paths == ["docs/README.md -> 开发/missing.md"], f"unexpected broken: {broken_paths}"
        assert broken_anchors == ["docs/README.md -> 开发/guide.md#missing"], f"unexpected anchors: {broken_anchors}"
        assert orphans == ["docs/开发/orphan.md"], f"unexpected orphans: {orphans}"


def main() -> int:
    test_repo_docs_integrity()
    test_detects_broken_links_anchors_and_orphans()
    print("PASS: docs integrity")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
