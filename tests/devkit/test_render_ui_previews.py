#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.build import render_ui_previews as preview
from tools.build.convert_ui_previews import convert_all


def test_selection_and_cache() -> None:
    assert len(preview.selected_files("docs-zh")) == len(preview.PREVIEW_FILES)
    assert len(preview.selected_files("docs")) == 2 * len(preview.PREVIEW_FILES)
    assert preview.selected_files("all") is None
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        binary, font = root / "renderer", root / "font"
        binary.write_bytes(b"binary-v1")
        font.write_bytes(b"font-v1")
        directory = root / "previews"
        calls = []

        def run(command: list[str], *, env: dict[str, str], check: bool) -> None:
            assert check
            calls.append((command, env))
            assert env["PLAYWISE_PREVIEW_WORKERS"] == "1"
            for name in env["PLAYWISE_PREVIEW_FILES"].splitlines():
                path = Path(name)
                if "overlay" in path.parts:
                    continue
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"P6\n1 1\n255\n\x01\x02\x03")

        def render(scope: str = "docs-zh", **kwargs: bool) -> int:
            return preview.render("nro", scope, directory, renderer=binary, font=font, jobs=1, **kwargs)

        with mock.patch.object(preview.subprocess, "run", side_effect=run):
            expected = sum(not name.startswith("overlay/") for name in preview.PREVIEW_FILES)
            assert render() == expected and len(calls) == 1
            first = json.loads((directory / ".nro-render.json").read_text(encoding="utf-8"))
            assert render() == expected and len(calls) == 1
            assert json.loads((directory / ".nro-render.json").read_text(encoding="utf-8")) == first
            image = directory / "child/child-light.ppm"
            image.unlink()
            render()
            assert len(calls) == 2 and image.is_file()
            image.write_bytes(b"broken")
            render()
            assert len(calls) == 3
            binary.write_bytes(b"binary-v2")
            render()
            assert len(calls) == 4
            font.write_bytes(b"font-v2")
            render()
            assert len(calls) == 5
            render(force=True)
            assert len(calls) == 6
            render("docs")
            assert len(calls) == 7 and (directory / "en/child/child-light.ppm").is_file()
            render("docs")
            assert len(calls) == 7

        cache = directory / ".nro-render.json"
        binary.write_bytes(b"broken renderer")
        with mock.patch.object(preview.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "renderer")):
            try:
                render()
            except subprocess.CalledProcessError:
                pass
            else:
                raise AssertionError("render failure must propagate")
        assert not cache.exists(), "failed render must invalidate prior success"
        with mock.patch.object(preview.subprocess, "run"):
            try:
                preview.render("overlay", "docs-zh", directory, renderer=binary, font=font)
            except ValueError:
                pass
            else:
                raise AssertionError("missing selected output must reject success")
        assert not (directory / ".overlay-render.json").exists()


def test_selected_conversion() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        selected = root / "selected.ppm"
        selected.write_bytes(b"P6\n1 1\n255\n\x01\x02\x03")
        (root / "old-unused.ppm").write_bytes(b"invalid legacy output")
        assert convert_all(root, jobs=1, sources=[selected]) == (1, 0, 1)
        assert convert_all(root, jobs=1, sources=[selected]) == (0, 1, 1)
        assert not (root / "old-unused.png").exists()


if __name__ == "__main__":
    test_selection_and_cache()
    test_selected_conversion()
    print("PASS: preview selection, render cache invalidation/failure and selected conversion")
