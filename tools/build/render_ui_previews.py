#!/usr/bin/env python3
"""Render the documented Chinese scenes by default, reusing unchanged snapshots."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.build.convert_ui_previews import convert_all
from tools.build.sync_doc_previews import PREVIEW_FILES

ROOT = Path(__file__).resolve().parents[2]
SCOPES = ("docs-zh", "docs", "all")


def selected_files(scope: str) -> list[str] | None:
    if scope not in SCOPES:
        raise ValueError(f"unknown preview scope: {scope}")
    if scope == "all":
        return None
    prefixes = ("",) if scope == "docs-zh" else ("", "en/")
    return [prefix + str(Path(name).with_suffix(".ppm")).replace("\\", "/")
            for prefix in prefixes for name in PREVIEW_FILES]


def inventory(directory: Path, kind: str, selection: list[str] | None) -> dict[str, list[int]]:
    paths = (directory / name for name in selection) if selection is not None else directory.rglob("*.ppm")
    outputs = {}
    for path in paths:
        name = path.relative_to(directory).as_posix()
        if ("overlay" in Path(name).parts) != (kind == "overlay"):
            continue
        if path.is_file():
            stat = path.stat()
            if stat.st_size:
                outputs[name] = [stat.st_size, stat.st_mtime_ns]
    return outputs


def render(kind: str, scope: str, directory: Path, *, jobs: int = 8, force: bool = False,
           renderer: Path | None = None, font: Path | None = None) -> int:
    if jobs < 1:
        raise ValueError("jobs must be positive")
    renderer = renderer or ROOT / "build" / "host" / ("ui_preview" if kind == "nro" else "overlay_preview")
    font = font or ROOT / "third_party/fonts/noto-sans-sc/NotoSansSC-Regular.ttf"
    directory = directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    selection = selected_files(scope)
    # Binary content includes compiled source, flags and dependencies. Make still
    # validates those dependencies; only image generation is cached here.
    identity = {"schema": 1, "scope": scope, "selection": selection,
                "inputs": [hashlib.sha256(path.read_bytes()).hexdigest()
                           for path in (renderer, font, Path(__file__))]}
    cache = directory / f".{kind}-render.json"
    current = inventory(directory, kind, selection)
    try:
        previous = json.loads(cache.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        previous = None
    if not force and current and previous == {"identity": identity, "outputs": current}:
        print(f"PASS: {kind} previews: {len(current)} cached ({scope})")
        return len(current)
    cache.unlink(missing_ok=True)
    env = os.environ.copy()
    env.pop("PLAYWISE_PREVIEW_FILES", None)
    # Two independent Make recipes may render concurrently. Each renderer
    # forks at most three languages, within the requested shared budget.
    env["PLAYWISE_PREVIEW_WORKERS"] = str(max(1, jobs // 2))
    if selection is not None:
        env["PLAYWISE_PREVIEW_FILES"] = "\n".join((directory / name).as_posix() for name in selection)
    command = [str(renderer.resolve()), str(font.resolve()), directory.as_posix()]
    if kind == "nro" and scope == "all":
        command.append("--all-styles")
    subprocess.run(command, env=env, check=True)
    outputs = inventory(directory, kind, selection)
    expected = sum(("overlay" in Path(name).parts) == (kind == "overlay") for name in selection or [])
    if not outputs or (selection is not None and len(outputs) != expected):
        raise ValueError(f"{kind}: missing rendered scenes ({len(outputs)}/{expected})")
    payload = {"identity": identity, "outputs": outputs}
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=directory,
                                     suffix=".json", delete=False) as stream:
        temporary = Path(stream.name)
        json.dump(payload, stream, sort_keys=True)
    try:
        os.replace(temporary, cache)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"PASS: {kind} previews: {len(outputs)} rendered ({scope})")
    return len(outputs)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("nro", "overlay", "convert"))
    parser.add_argument("--scope", choices=SCOPES, default="docs-zh")
    parser.add_argument("--dir", type=Path, default=ROOT / "build/ui-previews")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.kind == "convert":
        selection = selected_files(args.scope)
        sources = None if selection is None else [args.dir / name for name in selection]
        converted, skipped, total = convert_all(args.dir, jobs=args.jobs, force=args.force, sources=sources)
        print(f"PASS: selected PNG previews: {converted} converted, {skipped} cached ({total} total)")
    else:
        render(args.kind, args.scope, args.dir, jobs=args.jobs, force=args.force)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
