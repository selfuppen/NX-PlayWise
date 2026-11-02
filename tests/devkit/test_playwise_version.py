#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(ROOT))

from tools.build.playwise_version import (  # noqa: E402
    read_playwise_version,
    validate_playwise_version,
    write_playwise_version,
    device_lab_nacp_version,
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def write_version_pair(root: Path, header_version: str, make_version: str) -> None:
    common = root / "common"
    common.mkdir()
    (common / "version.h").write_text(
        f'#define PLAYWISE_VERSION "{header_version}"\n', encoding="utf-8"
    )
    (common / "version.mk").write_text(
        f"PLAYWISE_VERSION := {make_version}\n", encoding="utf-8"
    )


def test_matching_versions() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-version-") as tmp_dir:
        root = Path(tmp_dir)
        write_version_pair(root, "1.2.3-test", "1.2.3-test")
        require(read_playwise_version(root) == "1.2.3-test", "matching version pair must load")


def test_mismatch_fails() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-version-") as tmp_dir:
        root = Path(tmp_dir)
        write_version_pair(root, "1.2.3-test", "1.2.4-test")
        try:
            read_playwise_version(root)
        except ValueError as exc:
            require("version mismatch" in str(exc), "mismatch must explain the failure")
        else:
            raise AssertionError("mismatched version pair must fail")


def test_write_version_pair() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-version-") as tmp_dir:
        root = Path(tmp_dir)
        write_version_pair(root, "0.1.4", "0.1.4")
        write_playwise_version("1.0.0", root)
        require(read_playwise_version(root) == "1.0.0", "writer must update both version sources")


def test_invalid_version_fails() -> None:
    for version in ("v1.0.0", "1.0", "01.0.0", "1.0.0 bad"):
        try:
            validate_playwise_version(version)
        except ValueError:
            continue
        raise AssertionError(f"invalid version must fail: {version}")


def main() -> int:
    require(bool(read_playwise_version(ROOT)), "repository version pair must load")
    test_matching_versions()
    test_mismatch_fails()
    test_write_version_pair()
    test_invalid_version_fails()
    require(device_lab_nacp_version("3.1.2") == "3.1.2-lab", "short Lab labels keep their original format")
    require(device_lab_nacp_version("3.1.2-beta.1") == "3.1.2-beta.1-L", "prereleases retain full version with a compact Lab suffix")
    require(len(device_lab_nacp_version("3.1.2-beta.12")) <= 15, "Lab labels reserve the NACP terminator")
    try:
        device_lab_nacp_version("3.1.2-beta.1234")
    except ValueError as exc:
        require("15 bytes" in str(exc), "overlong labels must fail explicitly")
    else:
        raise AssertionError("Lab metadata must never silently truncate a version")
    print("PlayWise version source tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
