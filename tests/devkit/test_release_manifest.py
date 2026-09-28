#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import sys
import tempfile
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import generate_release_manifest as manifest  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_libnx_package_identity() -> None:
    with mock.patch.object(manifest, "command_output", return_value="libnx 4.12.0-1"):
        require(manifest.libnx_identity() == "libnx 4.12.0-1", "pacman identity must be retained")


def test_libnx_pkgconfig_fallback() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-libnx-") as tmp_dir:
        pc = Path(tmp_dir) / "libnx" / "lib" / "pkgconfig" / "libnx.pc"
        pc.parent.mkdir(parents=True)
        pc.write_text("Name: libnx\nVersion: 4.12.0\n", encoding="utf-8")
        with mock.patch.dict(os.environ, {"DEVKITPRO": tmp_dir}, clear=False), \
                mock.patch.object(manifest, "command_output", return_value="unknown"):
            require(manifest.libnx_identity() == "libnx 4.12.0", "pkg-config version must be a fallback")


def test_libnx_devkitpro_package_database_fallback() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-libnx-db-") as tmp_dir:
        desc = Path(tmp_dir) / "pacman" / "var" / "lib" / "pacman" / "local" / "libnx-4.12.0-1" / "desc"
        desc.parent.mkdir(parents=True)
        desc.write_text("%NAME%\nlibnx\n\n%VERSION%\n4.12.0-1\n", encoding="utf-8")
        with mock.patch.dict(os.environ, {"DEVKITPRO": tmp_dir}, clear=False), \
                mock.patch.object(manifest, "command_output", return_value="unknown"):
            require(manifest.libnx_identity() == "libnx 4.12.0-1",
                    "devkitPro package database must identify stripped container installs")


def test_candidate_defaults_to_pending() -> None:
    with mock.patch.object(manifest, "git_commit", return_value="a" * 40), \
            mock.patch.object(manifest, "git_tracked_dirty", return_value=False), \
            mock.patch.object(manifest, "toolchain_identity", return_value="gcc 15.2.0"), \
            mock.patch.object(manifest, "libnx_identity", return_value="libnx 4.12.0-1"), \
            mock.patch.object(manifest, "libtesla_commit", return_value="b" * 40), \
            mock.patch.dict(os.environ, {"PLAYWISE_BUILD_IMAGE": "devkitpro:v1"}, clear=True):
        data = manifest.make_manifest("release")
    require(data["qualification"]["status"] == "pending", "candidate must not claim qualification")
    require(data["verified_environment"]["result"] == "pending", "baseline must await current evidence")
    require(data["build"]["libnx"] == "libnx 4.12.0-1", "libnx package identity must be recorded")
    require(data["build"]["container_image"] == "devkitpro:v1", "Docker image tag must be recorded")
    require(data["build"]["source_dirty"] is False, "tracked dirty state must be recorded")


def test_manual_device_verification_is_explicit_and_scoped() -> None:
    verification = {
        "PLAYWISE_MANUAL_DEVICE_VERIFIED": "1",
        "PLAYWISE_VERIFIED_MODEL": "Nintendo Switch OLED",
        "PLAYWISE_VERIFIED_HOS": "22.5.0",
        "PLAYWISE_VERIFIED_ATMOSPHERE": "1.11.2",
    }
    with mock.patch.dict(os.environ, verification, clear=True):
        release = manifest.make_manifest("release")
        lab = manifest.make_manifest("device-lab")
        eden = manifest.make_manifest("eden-test")
    require(release["qualification"]["status"] == "manual_verified", "release must record manual verification")
    require(release["qualification"]["artifact_binding"] == "embedded-manifest",
            "manual declaration must identify its package binding")
    require(release["qualification"]["method"] == "manual" and
            release["qualification"]["scope"] == "main_features", "manual verification scope must be explicit")
    require(release["verified_environment"] == {
        "model": "Nintendo Switch OLED", "hos": "22.5.0", "atmosphere": "1.11.2",
        "result": "manual_verified",
    }, "release must record the stated device environment")
    require(lab["qualification"]["status"] == "pending" and
            eden["qualification"]["status"] == "pending", "manual status must not leak into test profiles")
    with mock.patch.dict(os.environ, {"PLAYWISE_MANUAL_DEVICE_VERIFIED": "1"}, clear=True):
        try:
            manifest.make_manifest("release")
        except ValueError as exc:
            require("requires model" in str(exc), "missing manual environment must fail clearly")
        else:
            raise AssertionError("manual verification without environment must fail")


def test_tracked_dirty_uses_content_diff() -> None:
    clean = manifest.subprocess.CompletedProcess(args=[], returncode=0)
    dirty = manifest.subprocess.CompletedProcess(args=[], returncode=1)
    with mock.patch.object(manifest.subprocess, "run", return_value=clean):
        require(manifest.git_tracked_dirty() is False, "content-identical tracked files must remain clean")
    with mock.patch.object(manifest.subprocess, "run", return_value=dirty):
        require(manifest.git_tracked_dirty() is True, "tracked content changes must mark the candidate dirty")


def main() -> int:
    test_libnx_package_identity()
    test_libnx_pkgconfig_fallback()
    test_libnx_devkitpro_package_database_fallback()
    test_candidate_defaults_to_pending()
    test_manual_device_verification_is_explicit_and_scoped()
    test_tracked_dirty_uses_content_diff()
    print("Release manifest tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
