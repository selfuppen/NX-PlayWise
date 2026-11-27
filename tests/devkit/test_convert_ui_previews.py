#!/usr/bin/env python3
"""Tests for convert_ui_previews.py incremental and parallel capabilities."""
from __future__ import annotations

import os
from pathlib import Path
import struct
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(ROOT))

import tools.build.convert_ui_previews as convert_ui_previews  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def make_ppm(width: int = 4, height: int = 4, color: tuple[int, int, int] = (255, 0, 0)) -> bytes:
    header = f"P6\n{width} {height}\n255\n".encode("ascii")
    pixel = bytes(color)
    return header + (pixel * (width * height))


def test_convert_one_valid() -> None:
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        ppm_file = tmp_path / "sample.ppm"
        ppm_file.write_bytes(make_ppm(4, 4, (10, 20, 30)))

        convert_ui_previews.convert_one(str(ppm_file))

        png_file = tmp_path / "sample.png"
        require(png_file.is_file(), "PNG file must be created")
        content = png_file.read_bytes()
        require(content.startswith(b"\x89PNG\r\n\x1a\n"), "PNG header magic must match")
        require(content.endswith(b"IEND\xaeB`\x82"), "PNG must terminate with IEND chunk")


def test_convert_one_invalid() -> None:
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        bad_ppm = tmp_path / "bad.ppm"
        bad_ppm.write_bytes(b"P5\n4 4\n255\n" + b"\0" * 16)
        try:
            convert_ui_previews.convert_one(str(bad_ppm))
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid magic must raise ValueError")


def test_convert_all_incremental() -> None:
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        sub_dir = tmp_path / "module"
        sub_dir.mkdir()

        ppm1 = sub_dir / "img1.ppm"
        ppm2 = sub_dir / "img2.ppm"
        ppm3 = sub_dir / "img3.ppm"

        ppm1.write_bytes(make_ppm(2, 2, (1, 1, 1)))
        ppm2.write_bytes(make_ppm(2, 2, (2, 2, 2)))
        ppm3.write_bytes(make_ppm(2, 2, (3, 3, 3)))

        # 1. Initial conversion: all 3 should be converted
        converted, skipped, total = convert_ui_previews.convert_all(tmp_path, jobs=1)
        require(converted == 3, f"Expected 3 converted, got {converted}")
        require(skipped == 0, f"Expected 0 skipped, got {skipped}")
        require(total == 3, f"Expected total 3, got {total}")

        # 2. Second pass: all 3 PNGs exist and are up to date -> 0 converted, 3 skipped
        converted, skipped, total = convert_ui_previews.convert_all(tmp_path, jobs=1)
        require(converted == 0, f"Expected 0 converted on unchanged pass, got {converted}")
        require(skipped == 3, f"Expected 3 skipped on unchanged pass, got {skipped}")
        require(total == 3, f"Expected total 3, got {total}")

        # 3. Modify 1 PPM: update content and explicitly touch mtime forward
        time.sleep(0.05)
        new_mtime = time.time() + 2.0
        ppm2.write_bytes(make_ppm(2, 2, (99, 99, 99)))
        os.utime(ppm2, (new_mtime, new_mtime))

        converted, skipped, total = convert_ui_previews.convert_all(tmp_path, jobs=1)
        require(converted == 1, f"Expected 1 converted, got {converted}")
        require(skipped == 2, f"Expected 2 skipped, got {skipped}")
        require(total == 3, f"Expected total 3, got {total}")

        # 4. Force flag: all 3 should be re-converted regardless of mtime
        converted, skipped, total = convert_ui_previews.convert_all(tmp_path, jobs=1, force=True)
        require(converted == 3, f"Expected 3 converted with force=True, got {converted}")
        require(skipped == 0, f"Expected 0 skipped with force=True, got {skipped}")


def test_convert_all_parallel() -> None:
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        for i in range(8):
            ppm = tmp_path / f"pic_{i}.ppm"
            ppm.write_bytes(make_ppm(8, 8, (i * 10, i * 20, i * 30)))

        converted, skipped, total = convert_ui_previews.convert_all(tmp_path, jobs=2)
        require(converted == 8, f"Expected 8 converted, got {converted}")
        require(total == 8, f"Expected 8 total, got {total}")
        for i in range(8):
            png = tmp_path / f"pic_{i}.png"
            require(png.is_file(), f"pic_{i}.png must exist")


def test_parse_args() -> None:
    args = convert_ui_previews.parse_args(["--jobs", "4", "--force"])
    require(args.jobs == 4, "jobs should be 4")
    require(args.force is True, "force should be True")
    require(args.remove_source is False, "default remove_source should be False")

    args_rm = convert_ui_previews.parse_args(["--remove-source"])
    require(args_rm.remove_source is True, "parse_args must accept --remove-source")

    args_default = convert_ui_previews.parse_args([])
    require(args_default.jobs is None, "default jobs should be None")
    require(args_default.force is False, "default force should be False")


def test_convert_remove_source() -> None:
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        ppm1 = tmp_path / "a.ppm"
        ppm2 = tmp_path / "b.ppm"
        ppm1.write_bytes(make_ppm(2, 2, (11, 22, 33)))
        ppm2.write_bytes(make_ppm(2, 2, (44, 55, 66)))

        converted, skipped, total = convert_ui_previews.convert_all(
            tmp_path, jobs=2, remove_source=True
        )
        require((converted, skipped, total) == (2, 0, 2), f"unexpected result: {(converted, skipped, total)}")
        require(not ppm1.exists() and not ppm2.exists(), "source PPMs must be removed after conversion")
        require((tmp_path / "a.png").is_file() and (tmp_path / "b.png").is_file(), "PNGs must exist")

        # Second pass with no remaining PPMs must count existing PNGs as cached
        converted, skipped, total = convert_ui_previews.convert_all(
            tmp_path, jobs=1, remove_source=True
        )
        require((converted, skipped, total) == (0, 2, 2), f"expected (0, 2, 2), got {(converted, skipped, total)}")

        # Missing both PPM and PNG for explicit source must fail
        try:
            convert_ui_previews.convert_all(
                tmp_path, jobs=1, sources=[tmp_path / "missing.ppm"], remove_source=True
            )
        except FileNotFoundError:
            pass
        else:
            raise AssertionError("missing source PPM without cached PNG must fail")


def main() -> None:
    test_convert_one_valid()
    test_convert_one_invalid()
    test_convert_all_incremental()
    test_convert_all_parallel()
    test_parse_args()
    test_convert_remove_source()
    print("PASS: test_convert_ui_previews")


if __name__ == "__main__":
    main()
