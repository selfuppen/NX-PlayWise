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


def test_remote_build_runs_inside_container() -> None:
    command = devkit.remote_build_command(
        devkit.REMOTE_CONTAINER,
        devkit.REMOTE_HOST_PATH,
        devkit.REMOTE_PATH,
        pull=True,
        targets=[["make", "test-host"]],
    )

    assert_true(
        command.startswith("git -C /home/ygq/nintendo/switch-play-time-control-local fetch origin master"),
        "host git fetch",
    )
    assert_true("git -C /home/ygq/nintendo/switch-play-time-control-local merge --ff-only FETCH_HEAD" in command, "host merge")
    assert_true("docker exec devkitpro-ssh-v1 sh -lc " in command, "docker exec")
    assert_true("make test-host" in command, "build target")


def test_remote_artifact_path_rejects_parent_traversal() -> None:
    try:
        devkit.remote_artifact_path(devkit.REMOTE_PATH, "../secret.zip")
    except devkit.VerificationError:
        return
    raise AssertionError("parent traversal should be rejected")


def main() -> int:
    test_default_package_download_dir()
    test_prepare_package_download_dir_clears_contents()
    test_remote_build_runs_inside_container()
    test_remote_artifact_path_rejects_parent_traversal()
    print("Devkit build helper tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
