#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[1]
REMOTE_ALIAS = "249-nintendo-switch-dev"
REMOTE_PATH = "/ws/switch-play-time-control-local"
DEVKITPRO = "/opt/devkitpro"
BUILD_TARGETS = [
    ["make"],
    ["make", "companion-nro"],
    ["make", "sysmodule-nsp"],
    [
        "make",
        "package-safe",
        "package-observe",
        "package-safe-nro",
        "package-disabled-boot2",
        "package-observe-boot2",
    ],
]
APP_DIR = Path("switch") / "play-time-control"
CONTENT_DIR = Path("atmosphere") / "contents" / "4200000000BD2300"
VERIFY_MARKER = "devkitPro build artifacts verified"
PACKAGE_ZIP_EXPECTATIONS = {
    "safe-nro": {"expect_boot2": False, "expect_exefs": False, "expect_nro": True},
    "disabled-boot2": {"expect_boot2": True, "expect_exefs": True, "expect_nro": False},
    "observe-boot2": {"expect_boot2": True, "expect_exefs": True, "expect_nro": False},
}
SAFE_NRO_ZIP_PREFIX = "safe-nro"
DEFAULT_DOWNLOAD = ROOT / "build" / "downloads" / "safe-nro.zip"
DEFAULT_PACKAGE_DOWNLOAD_DIR = Path(r"D:\switch\play-time-controll") if os.name == "nt" else ROOT / "build" / "downloads" / "packages"
DEFAULT_EDEN_SDMC = Path.home() / "AppData" / "Roaming" / "eden" / "sdmc"
REMOTE_ARTIFACT_VERIFIER = r'''
from pathlib import Path
import json
import zipfile

APP_DIR = Path("switch") / "play-time-control"
CONTENT_DIR = Path("atmosphere") / "contents" / "4200000000BD2300"
VERIFY_MARKER = "devkitPro build artifacts verified"
PACKAGE_ZIP_EXPECTATIONS = {
    "safe-nro": {"expect_boot2": False, "expect_exefs": False, "expect_nro": True},
    "disabled-boot2": {"expect_boot2": True, "expect_exefs": True, "expect_nro": False},
    "observe-boot2": {"expect_boot2": True, "expect_exefs": True, "expect_nro": False},
}


def fail(message):
    raise AssertionError(message)


def require_file(path, label):
    if not path.is_file():
        fail(f"{label}: missing file {path}")


def require_dir(path, label):
    if not path.is_dir():
        fail(f"{label}: missing directory {path}")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def verify_zip(path, *, expect_boot2, expect_exefs, expect_nro):
    require_file(path, f"{path.name} zip")
    with zipfile.ZipFile(path) as package:
        names = set(package.namelist())
    if "switch/play-time-control/config.json" not in names:
        fail(f"{path}: missing config entry")
    if not all(name.startswith(("switch/", "atmosphere/")) for name in names):
        fail(f"{path}: unexpected top-level entry")
    checks = {
        "boot2.flag": ("atmosphere/contents/4200000000BD2300/flags/boot2.flag" in names, expect_boot2),
        "exefs.nsp": ("atmosphere/contents/4200000000BD2300/exefs.nsp" in names, expect_exefs),
        "pctc.nro": ("switch/play-time-control/pctc.nro" in names, expect_nro),
    }
    for label, (actual, expected) in checks.items():
        if actual != expected:
            fail(f"{path}: {label} expectation failed")


def has_package_timestamp(path, prefix):
    stem = path.stem
    timestamp = stem[len(prefix) + 1:]
    return len(timestamp) == 15 and timestamp[8] == "-" and timestamp[:8].isdigit() and timestamp[9:].isdigit()


def latest_timestamped_zip(packages, prefix):
    candidates = sorted(
        (path for path in packages.glob(f"{prefix}-*.zip") if has_package_timestamp(path, prefix)),
        key=lambda path: path.name,
    )
    if not candidates:
        fail(f"missing timestamped zip for {prefix}")
    return candidates[-1]


def verify_package(package_root, *, expected_mode, expect_boot2, expect_exefs, expect_nro):
    app = package_root / APP_DIR
    require_dir(app, f"{package_root.name} app root")
    for relative in [
        "config.json",
        "auth.json",
        "rules.json",
        "state.json",
        "capabilities.json",
        "inbox/pending",
        "inbox/processing",
        "inbox/done",
        "results",
        "logs",
        "ledger",
        "backups",
        "flags",
    ]:
        path = app / relative
        if path.suffix == ".json":
            require_file(path, f"{package_root.name} {relative}")
        else:
            require_dir(path, f"{package_root.name} {relative}")
    config = read_json(app / "config.json")
    if config.get("control_mode") != expected_mode:
        fail(f"{package_root.name}: unexpected control_mode")
    checks = {
        "boot2.flag": ((package_root / CONTENT_DIR / "flags" / "boot2.flag").is_file(), expect_boot2),
        "exefs.nsp": ((package_root / CONTENT_DIR / "exefs.nsp").is_file(), expect_exefs),
        "pctc.nro": ((app / "pctc.nro").is_file(), expect_nro),
    }
    for label, (actual, expected) in checks.items():
        if actual != expected:
            fail(f"{package_root.name}: {label} expectation failed")


root = Path(".")
require_file(root / "build" / "switch" / "pctc.nro", "Companion NRO")
require_file(root / "build" / "switch" / "exefs.nsp", "sysmodule NSP")
packages = root / "build" / "packages"
verify_package(packages / "safe", expected_mode="observe", expect_boot2=False, expect_exefs=False, expect_nro=False)
verify_package(packages / "observe", expected_mode="observe", expect_boot2=False, expect_exefs=False, expect_nro=False)
verify_package(packages / "safe-nro", expected_mode="observe", expect_boot2=False, expect_exefs=False, expect_nro=True)
verify_package(packages / "disabled-boot2", expected_mode="disabled", expect_boot2=True, expect_exefs=True, expect_nro=False)
verify_package(packages / "observe-boot2", expected_mode="observe", expect_boot2=True, expect_exefs=True, expect_nro=False)
for prefix, expectations in PACKAGE_ZIP_EXPECTATIONS.items():
    verify_zip(latest_timestamped_zip(packages, prefix), **expectations)
print(VERIFY_MARKER)
'''.strip()


class VerificationError(AssertionError):
    pass


class CommandError(RuntimeError):
    def __init__(self, command: list[str], result: subprocess.CompletedProcess[str]):
        self.command = command
        self.result = result
        super().__init__(f"command failed with exit code {result.returncode}: {format_command(command)}")


def format_command(command: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def run(command: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            command,
            cwd=cwd or ROOT,
            env=env,
            text=True,
            capture_output=True,
        )
    except FileNotFoundError as exc:
        raise VerificationError(f"executable not found: {command[0]}") from exc
    if result.returncode != 0:
        raise CommandError(command, result)
    return result


def assert_true(value: bool, label: str) -> None:
    if not value:
        raise VerificationError(label)


def require_file(path: Path, label: str) -> None:
    assert_true(path.is_file(), f"{label}: missing file {path}")


def require_dir(path: Path, label: str) -> None:
    assert_true(path.is_dir(), f"{label}: missing directory {path}")


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def zip_names(path: Path) -> set[str]:
    with zipfile.ZipFile(path) as package:
        return set(package.namelist())


def has_package_timestamp(path: Path, prefix: str) -> bool:
    stem = path.stem
    timestamp = stem[len(prefix) + 1 :]
    return len(timestamp) == 15 and timestamp[8] == "-" and timestamp[:8].isdigit() and timestamp[9:].isdigit()


def latest_timestamped_zip(packages: Path, prefix: str) -> Path:
    candidates = sorted(
        (path for path in packages.glob(f"{prefix}-*.zip") if has_package_timestamp(path, prefix)),
        key=lambda path: path.name,
    )
    if not candidates:
        raise VerificationError(f"missing timestamped zip for {prefix}")
    return candidates[-1]


def ensure_safe_zip_entry(target_root: Path, member_name: str) -> Path:
    target = (target_root / member_name).resolve()
    root = target_root.resolve()
    if os.path.commonpath([str(root), str(target)]) != str(root):
        raise VerificationError(f"unsafe zip entry: {member_name}")
    return target


def verify_zip(path: Path, *, expect_boot2: bool, expect_exefs: bool, expect_nro: bool) -> None:
    require_file(path, f"{path.name} zip")
    names = zip_names(path)
    assert_true("switch/play-time-control/config.json" in names, f"{path}: missing config entry")
    assert_true(all(name.startswith(("switch/", "atmosphere/")) for name in names), f"{path}: unexpected top-level entry")
    has_boot2 = "atmosphere/contents/4200000000BD2300/flags/boot2.flag" in names
    has_exefs = "atmosphere/contents/4200000000BD2300/exefs.nsp" in names
    has_nro = "switch/play-time-control/pctc.nro" in names
    assert_true(has_boot2 == expect_boot2, f"{path}: boot2.flag expectation failed")
    assert_true(has_exefs == expect_exefs, f"{path}: exefs.nsp expectation failed")
    assert_true(has_nro == expect_nro, f"{path}: pctc.nro expectation failed")


def verify_safe_nro_zip(path: Path) -> None:
    verify_zip(path, expect_boot2=False, expect_exefs=False, expect_nro=True)


def verify_package_zip_by_prefix(path: Path, prefix: str) -> None:
    expectations = PACKAGE_ZIP_EXPECTATIONS[prefix]
    verify_zip(path, **expectations)


def verify_package(
    package_root: Path,
    *,
    expected_mode: str,
    expect_boot2: bool,
    expect_exefs: bool,
    expect_nro: bool,
) -> None:
    app = package_root / APP_DIR
    require_dir(app, f"{package_root.name} app root")
    for relative in [
        "config.json",
        "auth.json",
        "rules.json",
        "state.json",
        "capabilities.json",
        "inbox/pending",
        "inbox/processing",
        "inbox/done",
        "results",
        "logs",
        "ledger",
        "backups",
        "flags",
    ]:
        path = app / relative
        if path.suffix == ".json":
            require_file(path, f"{package_root.name} {relative}")
        else:
            require_dir(path, f"{package_root.name} {relative}")

    config = read_json(app / "config.json")
    assert_true(config.get("control_mode") == expected_mode, f"{package_root.name}: unexpected control_mode")

    boot2 = package_root / CONTENT_DIR / "flags" / "boot2.flag"
    exefs = package_root / CONTENT_DIR / "exefs.nsp"
    nro = app / "pctc.nro"
    assert_true(boot2.is_file() == expect_boot2, f"{package_root.name}: boot2.flag expectation failed")
    assert_true(exefs.is_file() == expect_exefs, f"{package_root.name}: exefs.nsp expectation failed")
    assert_true(nro.is_file() == expect_nro, f"{package_root.name}: pctc.nro expectation failed")


def verify_artifacts(root: Path) -> None:
    require_file(root / "build" / "switch" / "pctc.nro", "Companion NRO")
    require_file(root / "build" / "switch" / "exefs.nsp", "sysmodule NSP")

    packages = root / "build" / "packages"
    verify_package(packages / "safe", expected_mode="observe", expect_boot2=False, expect_exefs=False, expect_nro=False)
    verify_package(packages / "observe", expected_mode="observe", expect_boot2=False, expect_exefs=False, expect_nro=False)
    verify_package(packages / "safe-nro", expected_mode="observe", expect_boot2=False, expect_exefs=False, expect_nro=True)
    verify_package(packages / "disabled-boot2", expected_mode="disabled", expect_boot2=True, expect_exefs=True, expect_nro=False)
    verify_package(packages / "observe-boot2", expected_mode="observe", expect_boot2=True, expect_exefs=True, expect_nro=False)

    for prefix in PACKAGE_ZIP_EXPECTATIONS:
        verify_package_zip_by_prefix(latest_timestamped_zip(packages, prefix), prefix)
    print(VERIFY_MARKER)


def devkit_env() -> dict[str, str]:
    env = os.environ.copy()
    env.setdefault("DEVKITPRO", DEVKITPRO)
    env.setdefault("DEVKITARM", str(Path(env["DEVKITPRO"]) / "devkitARM"))
    env.setdefault("DEVKITA64", str(Path(env["DEVKITPRO"]) / "devkitA64"))
    env["PATH"] = str(Path(env["DEVKITA64"]) / "bin") + os.pathsep + env.get("PATH", "")
    return env


def remote_shell_prefix(remote_path: str, *, pull: bool) -> str:
    parts = [
        f"export DEVKITPRO={shlex.quote(DEVKITPRO)}",
        "export DEVKITARM=$DEVKITPRO/devkitARM",
        "export DEVKITA64=$DEVKITPRO/devkitA64",
        "export PATH=$DEVKITA64/bin:$PATH",
        f"cd {shlex.quote(remote_path)}",
    ]
    if pull:
        parts.append("git pull --ff-only origin master")
    return " && ".join(parts)


def remote_build_command(remote_path: str, *, pull: bool) -> str:
    commands = [remote_shell_prefix(remote_path, pull=pull)]
    commands.extend(format_command(target) for target in BUILD_TARGETS)
    return " && ".join(commands)


def remote_verify_command(remote_path: str) -> str:
    encoded = base64.b64encode(REMOTE_ARTIFACT_VERIFIER.encode("utf-8")).decode("ascii")
    return (
        f"cd {shlex.quote(remote_path)} "
        "&& python3 -c "
        + shlex.quote(f"import base64; exec(base64.b64decode('{encoded}').decode('utf-8'))")
    )


def remote_package_manifest_command(remote_path: str) -> str:
    script = f"""
from pathlib import Path
import json

packages = Path("build") / "packages"
prefixes = {list(PACKAGE_ZIP_EXPECTATIONS)!r}
result = []
for prefix in prefixes:
    candidates = sorted(
        (
            path for path in packages.glob(f"{{prefix}}-*.zip")
            if len(path.stem[len(prefix) + 1:]) == 15
            and path.stem[len(prefix) + 9:len(prefix) + 10] == "-"
            and path.stem[len(prefix) + 1:len(prefix) + 9].isdigit()
            and path.stem[len(prefix) + 10:].isdigit()
        ),
        key=lambda path: path.name,
    )
    if not candidates:
        raise AssertionError(f"missing timestamped zip for {{prefix}}")
    result.append(candidates[-1].as_posix())
print(json.dumps(result))
""".strip()
    encoded = base64.b64encode(script.encode("utf-8")).decode("ascii")
    return (
        f"cd {shlex.quote(remote_path)} "
        "&& python3 -c "
        + shlex.quote(f"import base64; exec(base64.b64decode('{encoded}').decode('utf-8'))")
    )


def run_remote_build(alias: str, remote_path: str, *, pull: bool) -> None:
    run(["ssh", alias, remote_build_command(remote_path, pull=pull)])


def run_remote_verify(alias: str, remote_path: str) -> None:
    result = run(["ssh", alias, remote_verify_command(remote_path)])
    if VERIFY_MARKER not in result.stdout:
        raise VerificationError(f"missing verification marker: {VERIFY_MARKER!r}")


def list_remote_package_zips(alias: str, remote_path: str) -> list[str]:
    result = run(["ssh", alias, remote_package_manifest_command(remote_path)])
    try:
        paths = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise VerificationError("remote package manifest was not valid JSON") from exc
    if not isinstance(paths, list) or not all(isinstance(path, str) for path in paths):
        raise VerificationError("remote package manifest has unexpected shape")
    return paths


def download_remote_safe_nro(alias: str, remote_path: str, destination: Path) -> None:
    remote_paths = [
        path for path in list_remote_package_zips(alias, remote_path)
        if Path(path).name.startswith(f"{SAFE_NRO_ZIP_PREFIX}-")
    ]
    if not remote_paths:
        raise VerificationError("remote safe-nro timestamped zip was not found")
    destination.parent.mkdir(parents=True, exist_ok=True)
    remote_zip = f"{alias}:{remote_path}/{remote_paths[0]}"
    downloaded = destination.parent / Path(remote_paths[0]).name
    run(["scp", remote_zip, "."], cwd=destination.parent)
    if downloaded != destination:
        shutil.copy2(downloaded, destination)
    verify_safe_nro_zip(destination)


def package_prefix_for_zip(path: Path) -> str:
    for prefix in PACKAGE_ZIP_EXPECTATIONS:
        if path.name.startswith(f"{prefix}-") and path.suffix == ".zip":
            return prefix
    raise VerificationError(f"unexpected package zip name: {path.name}")


def extract_zip_to_named_dir(zip_path: Path) -> Path:
    extract_root = zip_path.with_suffix("")
    target = extract_root.resolve()
    parent = zip_path.parent.resolve()
    if os.path.commonpath([str(parent), str(target)]) != str(parent):
        raise VerificationError(f"unsafe extract directory: {extract_root}")
    if extract_root.exists():
        raise VerificationError(f"extract directory already exists: {extract_root}")
    extract_root.mkdir(parents=True)
    with zipfile.ZipFile(zip_path) as package:
        for member in package.infolist():
            ensure_safe_zip_entry(extract_root, member.filename)
        package.extractall(extract_root)
    return extract_root


def download_remote_package_zips(alias: str, remote_path: str, destination_dir: Path) -> None:
    remote_paths = list_remote_package_zips(alias, remote_path)
    destination_dir.mkdir(parents=True, exist_ok=True)
    for remote_rel in remote_paths:
        local_zip = destination_dir / Path(remote_rel).name
        remote_zip = f"{alias}:{remote_path}/{remote_rel}"
        run(["scp", remote_zip, "."], cwd=destination_dir)
        prefix = package_prefix_for_zip(local_zip)
        verify_package_zip_by_prefix(local_zip, prefix)
        extract_zip_to_named_dir(local_zip)


def copy_local_safe_nro(destination: Path) -> None:
    source = latest_timestamped_zip(ROOT / "build" / "packages", SAFE_NRO_ZIP_PREFIX)
    require_file(source, "local safe-nro timestamped zip")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    verify_safe_nro_zip(destination)


def install_safe_nro_zip(zip_path: Path, sdmc_root: Path) -> None:
    verify_safe_nro_zip(zip_path)
    sdmc_root.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as package:
        for member in package.infolist():
            ensure_safe_zip_entry(sdmc_root, member.filename)
        package.extractall(sdmc_root)

    require_file(sdmc_root / APP_DIR / "pctc.nro", "installed Companion NRO")
    boot2 = sdmc_root / CONTENT_DIR / "flags" / "boot2.flag"
    if boot2.exists():
        raise VerificationError(f"boot2.flag exists after safe-nro install: {boot2}")


def run_local_build() -> None:
    env = devkit_env()
    for target in BUILD_TARGETS:
        run(target, env=env)


def run_local_verify() -> None:
    verify_artifacts(ROOT)


def install_remote_safe_nro(alias: str, remote_path: str, download_path: Path, sdmc_root: Path) -> None:
    download_remote_safe_nro(alias, remote_path, download_path)
    install_safe_nro_zip(download_path, sdmc_root)


def install_local_safe_nro(download_path: Path, sdmc_root: Path) -> None:
    copy_local_safe_nro(download_path)
    install_safe_nro_zip(download_path, sdmc_root)


def run_step(name: str, fn) -> bool:
    print(f"[RUN ] {name}")
    try:
        fn()
    except CommandError as exc:
        print(f"[FAIL] {name}")
        print(f"command: {format_command(exc.command)}")
        if exc.result.stdout:
            print("stdout:")
            print(exc.result.stdout.rstrip())
        if exc.result.stderr:
            print("stderr:")
            print(exc.result.stderr.rstrip())
        return False
    except Exception as exc:
        print(f"[FAIL] {name}")
        print(str(exc))
        return False
    print(f"[PASS] {name}")
    return True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run and verify the devkitPro build targets.")
    parser.add_argument("--ssh", dest="remote", action="store_true", help="Run through SSH. This is the default.")
    parser.add_argument("--remote", dest="remote", action="store_true", help="Alias for --ssh.")
    parser.add_argument("--local", action="store_true", help="Run locally when devkitPro and make are available.")
    parser.add_argument("--verify-only", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--no-pull", action="store_true", help="Skip git pull in remote mode.")
    parser.add_argument(
        "--install-safe-nro",
        action="store_true",
        help="Download/copy the latest safe NRO zip and extract it to the emulator SD root after verification.",
    )
    parser.add_argument(
        "--sdmc-root",
        type=Path,
        default=DEFAULT_EDEN_SDMC,
        help=f"Emulator SD root used with --install-safe-nro. Default: {DEFAULT_EDEN_SDMC}",
    )
    parser.add_argument(
        "--safe-nro-zip",
        type=Path,
        default=DEFAULT_DOWNLOAD,
        help=f"Local safe NRO zip download/copy path. Default: {DEFAULT_DOWNLOAD}",
    )
    parser.add_argument(
        "--package-download-dir",
        type=Path,
        default=DEFAULT_PACKAGE_DOWNLOAD_DIR,
        help=f"Remote package zip download/extract directory. Default: {DEFAULT_PACKAGE_DOWNLOAD_DIR}",
    )
    parser.add_argument(
        "--skip-package-download",
        action="store_true",
        help="Skip downloading timestamped remote package zips after remote verification.",
    )
    parser.add_argument("--ssh-alias", default=REMOTE_ALIAS)
    parser.add_argument("--remote-path", default=REMOTE_PATH)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.remote and args.local:
        print("--remote and --local cannot be used together")
        return 2
    if args.verify_only:
        verify_artifacts(ROOT)
        return 0

    if args.local:
        steps = [
            ("local devkitPro build", run_local_build),
            ("local build artifacts", run_local_verify),
        ]
        if args.install_safe_nro:
            steps.append(
                (
                    "install safe-nro to emulator SD",
                    lambda: install_local_safe_nro(args.safe_nro_zip, args.sdmc_root),
                )
            )
    else:
        steps = [
            (
                "remote devkitPro build",
                lambda: run_remote_build(args.ssh_alias, args.remote_path, pull=not args.no_pull),
            ),
            ("remote build artifacts", lambda: run_remote_verify(args.ssh_alias, args.remote_path)),
        ]
        if not args.skip_package_download:
            steps.append(
                (
                    "download remote package zips",
                    lambda: download_remote_package_zips(
                        args.ssh_alias,
                        args.remote_path,
                        args.package_download_dir,
                    ),
                )
            )
        if args.install_safe_nro:
            steps.append(
                (
                    "install safe-nro to emulator SD",
                    lambda: install_remote_safe_nro(
                        args.ssh_alias,
                        args.remote_path,
                        args.safe_nro_zip,
                        args.sdmc_root,
                    ),
                )
            )

    passed = 0
    for index, (name, fn) in enumerate(steps):
        ok = run_step(name, fn)
        if ok:
            passed += 1
            continue
        for skipped_name, _ in steps[index + 1 :]:
            print(f"[SKIP] {skipped_name}")
        break
    total = len(steps)
    print(f"summary: {passed}/{total} devkitPro verification steps passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
