#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
REMOTE_ALIAS = "renqi-nintendo-switch-dev"
REMOTE_CONTAINER = "devkitpro-ssh-v1"
REMOTE_HOST_PATH = "/home/ygq/nintendo/switch-play-time-control-local"
REMOTE_PATH = "/ws/switch-play-time-control-local"
DEFAULT_OUTPUT = Path(r"D:\switch\play-time-controll\download") if os.name == "nt" else ROOT / "build" / "downloads" / "packages"
APP_CONFIG = "switch/play-time-control/config.json"
CONTENT_ROOT = "atmosphere/contents/4200000000BD2300"
PACKAGE_EXPECTATIONS = {
    "safe-nro": ("observe", False),
    "disabled-boot2": ("disabled", True),
    "observe-boot2": ("observe", True),
    "grant-boot2": ("grant", True),
    "enforce-boot2": ("enforce", True),
}


class PackageError(RuntimeError):
    pass


def package_prefix(path: Path) -> str:
    for prefix in PACKAGE_EXPECTATIONS:
        if path.name.startswith(f"{prefix}-") and path.suffix == ".zip":
            return prefix
    raise PackageError(f"unexpected package name: {path.name}")


def safe_zip_members(package: zipfile.ZipFile) -> list[str]:
    names = package.namelist()
    for name in names:
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts:
            raise PackageError(f"unsafe zip entry: {name}")
    return names


def bundle_member_prefix(member: tarfile.TarInfo) -> str:
    name = PurePosixPath(member.name)
    if len(name.parts) != 1 or name.name != member.name or not member.isfile():
        raise PackageError(f"unsafe remote bundle entry: {member.name}")
    return package_prefix(Path(member.name))


def verify_package_zip(path: Path, prefix: str | None = None) -> None:
    expected_prefix = prefix or package_prefix(path)
    expected_mode, expect_boot2 = PACKAGE_EXPECTATIONS[expected_prefix]
    with zipfile.ZipFile(path) as package:
        names = safe_zip_members(package)
        if APP_CONFIG not in names:
            raise PackageError(f"{path.name}: missing {APP_CONFIG}")
        config = json.loads(package.read(APP_CONFIG).decode("utf-8"))
    if config.get("control_mode") != expected_mode:
        raise PackageError(f"{path.name}: expected control_mode={expected_mode}")
    boot2 = f"{CONTENT_ROOT}/flags/boot2.flag"
    exefs = f"{CONTENT_ROOT}/exefs.nsp"
    nro = "switch/play-time-control/pctc.nro"
    if (boot2 in names) != expect_boot2:
        raise PackageError(f"{path.name}: unexpected boot2.flag state")
    if expect_boot2 and exefs not in names:
        raise PackageError(f"{path.name}: missing exefs.nsp")
    if nro not in names:
        raise PackageError(f"{path.name}: missing pctc.nro")


def latest_packages(package_dir: Path) -> dict[str, Path]:
    selected: dict[str, Path] = {}
    for prefix in PACKAGE_EXPECTATIONS:
        matches = sorted(package_dir.glob(f"{prefix}-*.zip"), key=lambda path: path.stat().st_mtime)
        if not matches:
            raise PackageError(f"missing remote package: {prefix}-*.zip")
        selected[prefix] = matches[-1]
    return selected


def emit_bundle() -> int:
    packages = latest_packages(ROOT / "build" / "packages")
    for prefix, path in packages.items():
        verify_package_zip(path, prefix)
    with tarfile.open(fileobj=sys.stdout.buffer, mode="w|") as bundle:
        for prefix in PACKAGE_EXPECTATIONS:
            path = packages[prefix]
            bundle.add(path, arcname=path.name, recursive=False)
    return 0


def remote_command() -> str:
    env = (
        "-e DEVKITPRO=/opt/devkitpro "
        "-e DEVKITARM=/opt/devkitpro/devkitARM "
        "-e DEVKITA64=/opt/devkitpro/devkitA64 "
        "-e PATH=/opt/devkitpro/devkitA64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
    )
    container_script = (
        f"cd {shlex.quote(REMOTE_PATH)} "
        "&& make test packages 1>&2 "
        "&& python3 tools/package_remote.py --emit-bundle"
    )
    return " && ".join(
        [
            f"git -C {shlex.quote(REMOTE_HOST_PATH)} fetch origin master 1>&2",
            f"git -C {shlex.quote(REMOTE_HOST_PATH)} merge --ff-only FETCH_HEAD 1>&2",
            f"docker exec {env} {shlex.quote(REMOTE_CONTAINER)} sh -lc {shlex.quote(container_script)}",
        ]
    )


def receive_bundle(staging: Path) -> None:
    process = subprocess.Popen(
        ["ssh", REMOTE_ALIAS, remote_command()],
        cwd=ROOT,
        stdin=None,
        stdout=subprocess.PIPE,
    )
    assert process.stdout is not None
    seen: set[str] = set()
    try:
        with tarfile.open(fileobj=process.stdout, mode="r|") as bundle:
            for member in bundle:
                prefix = bundle_member_prefix(member)
                if prefix in seen:
                    raise PackageError(f"duplicate remote package: {prefix}")
                source = bundle.extractfile(member)
                if source is None:
                    raise PackageError(f"cannot read remote package: {member.name}")
                with (staging / member.name).open("wb") as destination:
                    shutil.copyfileobj(source, destination)
                seen.add(prefix)
    except Exception:
        process.kill()
        process.wait()
        raise
    return_code = process.wait()
    if return_code != 0:
        raise PackageError(f"remote package command failed with exit code {return_code}")
    missing = set(PACKAGE_EXPECTATIONS) - seen
    if missing:
        raise PackageError(f"remote bundle missing: {', '.join(sorted(missing))}")


def extract_package(path: Path) -> Path:
    prefix = package_prefix(path)
    destination = path.with_suffix("")
    destination.mkdir()
    with zipfile.ZipFile(path) as package:
        safe_zip_members(package)
        package.extractall(destination)
    verify_package_zip(path, prefix)
    return destination


def replace_output(staging: Path, output: Path) -> None:
    output = output.resolve()
    parent = output.parent
    previous = parent / f".{output.name}.previous"
    if previous.exists():
        if previous.is_dir():
            shutil.rmtree(previous)
        else:
            previous.unlink()
    if output.exists():
        output.rename(previous)
    try:
        staging.rename(output)
    except Exception:
        if previous.exists() and not output.exists():
            previous.rename(output)
        raise
    if previous.exists():
        if previous.is_dir():
            shutil.rmtree(previous)
        else:
            previous.unlink()


def download_packages(output: Path) -> None:
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{output.name}.staging-", dir=output.parent))
    try:
        receive_bundle(staging)
        for path in sorted(staging.glob("*.zip")):
            verify_package_zip(path)
            extract_package(path)
        replace_output(staging, output)
    except Exception:
        if staging.exists():
            shutil.rmtree(staging)
        raise


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Test, build, verify, and download all Switch packages remotely.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help=f"Download directory. Default: {DEFAULT_OUTPUT}")
    parser.add_argument("--emit-bundle", action="store_true", help=argparse.SUPPRESS)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.emit_bundle:
        return emit_bundle()
    try:
        download_packages(args.output)
    except (OSError, PackageError, subprocess.SubprocessError, tarfile.TarError, zipfile.BadZipFile) as exc:
        print(f"FAIL: remote packages: {exc}")
        return 1
    print(f"PASS: remote packages -> {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
