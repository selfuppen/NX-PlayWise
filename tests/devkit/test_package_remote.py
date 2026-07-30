#!/usr/bin/env python3
from __future__ import annotations

import io
from pathlib import Path
import sys
import tarfile
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import package_remote  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def write_package(path: Path, mode: str, boot2: bool) -> None:
    with zipfile.ZipFile(path, "w") as package:
        package.writestr("switch/play-time-control/config.json", f'{{"control_mode":"{mode}"}}')
        package.writestr("switch/play-time-control/pctc.nro", b"nro")
        if boot2:
            package.writestr("atmosphere/contents/4200000000BD2300/exefs.nsp", b"nsp")
            package.writestr("atmosphere/contents/4200000000BD2300/flags/boot2.flag", b"")


def test_remote_command() -> None:
    command = package_remote.remote_command()
    require("fetch origin master" in command, "remote command must fetch master")
    require("merge --ff-only FETCH_HEAD" in command, "remote command must fast-forward")
    require("make test packages" in command, "remote command must test and package")
    require("--emit-bundle" in command, "remote command must stream one bundle")


def test_zip_verification() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-package-test-") as tmp_dir:
        root = Path(tmp_dir)
        for prefix, (mode, boot2) in package_remote.PACKAGE_EXPECTATIONS.items():
            path = root / f"{prefix}-20260730-120000.zip"
            write_package(path, mode, boot2)
            package_remote.verify_package_zip(path, prefix)


def test_unsafe_bundle_entry() -> None:
    payload = io.BytesIO()
    with tarfile.open(fileobj=payload, mode="w") as bundle:
        data = b"bad"
        member = tarfile.TarInfo("../bad.zip")
        member.size = len(data)
        bundle.addfile(member, io.BytesIO(data))
    payload.seek(0)
    with tarfile.open(fileobj=payload, mode="r|") as bundle:
        member = next(iter(bundle))
        try:
            package_remote.bundle_member_prefix(member)
        except package_remote.PackageError:
            return
    raise AssertionError("parent traversal must be rejected")


def test_replace_output() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-package-output-") as tmp_dir:
        root = Path(tmp_dir)
        output = root / "download"
        output.mkdir()
        (output / "old.txt").write_text("old", encoding="utf-8")
        staging = root / "staging"
        staging.mkdir()
        (staging / "new.txt").write_text("new", encoding="utf-8")
        package_remote.replace_output(staging, output)
        require((output / "new.txt").is_file(), "new output must replace old output")
        require(not (output / "old.txt").exists(), "old output must be removed after success")


def test_download_failure_preserves_output() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-package-failure-") as tmp_dir:
        output = Path(tmp_dir) / "download"
        output.mkdir()
        marker = output / "last-good.txt"
        marker.write_text("keep", encoding="utf-8")
        original_receive = package_remote.receive_bundle

        def fail_receive(staging: Path) -> None:
            raise package_remote.PackageError(f"simulated failure in {staging.name}")

        package_remote.receive_bundle = fail_receive
        try:
            try:
                package_remote.download_packages(output)
            except package_remote.PackageError:
                pass
            else:
                raise AssertionError("simulated download must fail")
        finally:
            package_remote.receive_bundle = original_receive
        require(marker.read_text(encoding="utf-8") == "keep", "failed download must preserve last good output")


def main() -> int:
    test_remote_command()
    test_zip_verification()
    test_unsafe_bundle_entry()
    test_replace_output()
    test_download_failure_preserves_output()
    print("Remote package helper tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
