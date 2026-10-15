#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import sys
import subprocess
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
            mock.patch.object(manifest, "source_fingerprint", return_value="c" * 64), \
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
    require(data["usage_contract"] == "pctl1952_v1_proxy1440",
            "release declares the direct counter contract while retaining the proxy")


def test_manual_device_verification_is_explicit_and_scoped() -> None:
    verification = {
        "PLAYWISE_MANUAL_DEVICE_VERIFIED": "1",
        "PLAYWISE_VERIFIED_MODEL": "Nintendo Switch OLED",
        "PLAYWISE_VERIFIED_HOS": "22.5.0",
        "PLAYWISE_VERIFIED_ATMOSPHERE": "1.11.2",
    }
    with mock.patch.dict(os.environ, verification, clear=True), \
            mock.patch.object(manifest, "source_fingerprint", return_value="c" * 64):
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
    require("usage_contract" not in lab and "usage_contract" not in eden,
            "Lab raw and Eden simulation must not claim the release consumption contract")
    with mock.patch.dict(os.environ, {"PLAYWISE_MANUAL_DEVICE_VERIFIED": "1"}, clear=True), \
            mock.patch.object(manifest, "source_fingerprint", return_value="c" * 64):
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


def test_host_newline_policy_preserves_real_edits() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-git-normalization-") as tmp_dir:
        root = Path(tmp_dir)
        def run(args: list[str]) -> None:
            subprocess.run(["git", "-c", "core.autocrlf=false", "-c", "commit.gpgsign=false",
                            "-c", "core.hooksPath=" + str(root / "no-hooks"), *args],
                           cwd=root, check=True, capture_output=True)
        run(["init"])
        tracked = root / "sample.txt"
        tracked.write_bytes(b"value=1\n")
        run(["add", "sample.txt"])
        run(["-c", "user.name=fixture", "-c", "user.email=fixture@example.invalid", "commit", "-m", "baseline"])
        tracked.write_bytes(b"value=1\r\n")
        with mock.patch.object(manifest, "ROOT", root):
            with mock.patch.dict(os.environ, {"PLAYWISE_GIT_AUTOCRLF": "false"}):
                require(manifest.git_tracked_dirty(), "mismatched container policy exposes CRLF checkout difference")
            with mock.patch.dict(os.environ, {"PLAYWISE_GIT_AUTOCRLF": "true"}):
                require(not manifest.git_tracked_dirty(), "effective host normalization keeps identical source clean")
                tracked.write_bytes(b"value=2\r\n")
                require(manifest.git_tracked_dirty(), "normalization cannot hide a real source edit")
            with mock.patch.dict(os.environ, {"PLAYWISE_GIT_AUTOCRLF": "false; unsafe"}):
                try:
                    manifest.git_tracked_dirty()
                except ValueError:
                    pass
                else:
                    raise AssertionError("invalid Git policy must not reach a command")
        with mock.patch.object(manifest.subprocess, "run", side_effect=OSError("Git unavailable")):
            require(manifest.git_tracked_dirty(), "Git failure can never force a clean source identity")


def test_rebuild_identity_tracks_source_and_toolchain() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-build-identity-") as temporary:
        root = Path(temporary)
        subprocess.run(["git", "init"], cwd=root, check=True, capture_output=True)
        (root / ".gitignore").write_text("build/\n", encoding="utf-8")
        source = root / "main.c"
        source.write_text("int value = 1;\n", encoding="utf-8")
        subprocess.run(["git", "add", "."], cwd=root, check=True, capture_output=True)
        with mock.patch.object(manifest, "ROOT", root), \
                mock.patch.object(manifest, "git_commit", return_value="a" * 40), \
                mock.patch.object(manifest, "git_tracked_dirty", return_value=True), \
                mock.patch.object(manifest, "toolchain_identity", return_value="gcc fixture"), \
                mock.patch.object(manifest, "libnx_identity", return_value="libnx fixture"):
            first = manifest.make_manifest("release")
            require(first == manifest.make_manifest("release"), "unchanged input has deterministic build identity")
            require(first["release_id"] == manifest.make_manifest("eden-test")["release_id"],
                    "profiles share their source/toolchain generation")
            source.write_text("int value = 2;\n", encoding="utf-8")
            second = manifest.make_manifest("release")
            require(first["release_id"] != second["release_id"], "same-commit dirty rebuild must be reloadable")
            require(first["commit"] == second["commit"], "source commit remains truthful")
            new_source = root / "new.c"
            new_source.write_text("new input 1", encoding="utf-8")
            third = manifest.make_manifest("release")
            new_source.write_text("new input 2", encoding="utf-8")
            require(third["release_id"] != manifest.make_manifest("release")["release_id"],
                    "editing a new untracked source must not collide")
            unchanged = manifest.make_manifest("release")
            (root / "build").mkdir()
            (root / "build" / "output.bin").write_bytes(b"ignored generated output")
            require(unchanged == manifest.make_manifest("release"), "ignored build output cannot feed back into identity")
            with mock.patch.object(manifest, "libnx_identity", return_value="libnx changed"):
                require(unchanged["release_id"] != manifest.make_manifest("release")["release_id"],
                        "toolchain identity changes require a reload")


def main() -> int:
    test_libnx_package_identity()
    test_libnx_pkgconfig_fallback()
    test_libnx_devkitpro_package_database_fallback()
    test_candidate_defaults_to_pending()
    test_manual_device_verification_is_explicit_and_scoped()
    test_tracked_dirty_uses_content_diff()
    test_host_newline_policy_preserves_real_edits()
    test_rebuild_identity_tracks_source_and_toolchain()
    print("Release manifest tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
