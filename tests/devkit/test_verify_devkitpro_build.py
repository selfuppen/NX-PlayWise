#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import verify_devkitpro_build as devkit  # noqa: E402


def assert_true(value, label: str) -> None:
    if not value:
        raise AssertionError(f"{label}: expected truthy value")


def assert_equal(actual, expected, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def test_default_package_download_dir() -> None:
    if sys.platform == "win32":
        assert_equal(str(devkit.DEFAULT_PACKAGE_DOWNLOAD_DIR), r"D:\switch\play-time-controll\download", "windows default")
    else:
        assert_equal(devkit.DEFAULT_PACKAGE_DOWNLOAD_DIR, devkit.ROOT / "build" / "downloads" / "packages", "non-windows default")


def test_prepare_package_download_dir_clears_contents() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-package-download-") as tmp_dir:
        destination = Path(tmp_dir) / "download"
        nested = destination / "old-package"
        nested.mkdir(parents=True)
        (nested / "old.txt").write_text("old", encoding="utf-8")
        (destination / "old.zip").write_text("old", encoding="utf-8")

        devkit.prepare_package_download_dir(destination)

        assert_true(destination.is_dir(), "download dir remains")
        assert_equal(list(destination.iterdir()), [], "download dir emptied")


def main() -> int:
    test_default_package_download_dir()
    test_prepare_package_download_dir_clears_contents()
    print("Devkit build helper tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
