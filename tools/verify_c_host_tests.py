#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = ROOT / "Makefile"
HOST_BUILD_DIR = ROOT / "build" / "host"
HOST_TEST = HOST_BUILD_DIR / ("test_host_core_auto.exe" if sys.platform == "win32" else "test_host_core_auto")
PASS_TEXT = "C host core tests passed"


class VerificationError(AssertionError):
    pass


class CommandError(RuntimeError):
    def __init__(self, command: list[str], result: subprocess.CompletedProcess[str]):
        self.command = command
        self.result = result
        super().__init__(f"command failed with exit code {result.returncode}: {format_command(command)}")


def format_command(command: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def run(command: list[str], *, cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            command,
            cwd=cwd or ROOT,
            text=True,
            capture_output=True,
        )
    except FileNotFoundError as exc:
        raise VerificationError(f"executable not found: {command[0]}") from exc
    if check and result.returncode != 0:
        raise CommandError(command, result)
    return result


def makefile_variables() -> dict[str, str]:
    variables: dict[str, str] = {}
    current = ""
    for raw_line in MAKEFILE.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].rstrip()
        if not line:
            continue
        if line.endswith("\\"):
            current += line[:-1] + " "
            continue
        line = current + line
        current = ""
        if "?=" in line:
            name, value = line.split("?=", 1)
        elif ":=" in line:
            name, value = line.split(":=", 1)
        else:
            continue
        variables[name.strip()] = value.strip()
    return variables


def split_words(value: str) -> list[str]:
    return shlex.split(value, posix=True)


def host_sources(variables: dict[str, str]) -> list[str]:
    source_vars = ["COMMON_SRCS", "PLATFORM_HOST_SRCS", "ORCH_SRCS", "TEST_SRCS"]
    sources: list[str] = []
    for name in source_vars:
        value = variables.get(name)
        if value is None:
            raise VerificationError(f"Makefile missing {name}")
        sources.extend(split_words(value))
    for source in sources:
        if not (ROOT / source).is_file():
            raise VerificationError(f"Makefile source does not exist: {source}")
    return sources


def host_cflags(variables: dict[str, str]) -> list[str]:
    return split_words(os.environ.get("HOST_CFLAGS") or variables.get("HOST_CFLAGS", "-std=c99 -Wall -Wextra -Werror -I."))


def host_cc(variables: dict[str, str]) -> str:
    return os.environ.get("HOST_CC") or variables.get("HOST_CC", "gcc")


def compile_local() -> None:
    variables = makefile_variables()
    HOST_BUILD_DIR.mkdir(parents=True, exist_ok=True)
    cc = host_cc(variables)
    command = [
        cc,
        *host_cflags(variables),
        "-o",
        str(HOST_TEST),
        *host_sources(variables),
    ]
    try:
        run(command)
    except VerificationError as exc:
        if str(exc) == f"executable not found: {cc}":
            raise VerificationError(
                f"C compiler not found: {cc}. Set HOST_CC to an available compiler, "
                "or run the default SSH verification after pushing master."
            ) from exc
        raise


def run_local_test() -> None:
    result = run([str(HOST_TEST)])
    if PASS_TEXT not in result.stdout:
        raise VerificationError(f"missing success marker in C host output: {PASS_TEXT!r}")


def run_remote_test() -> None:
    remote_command = (
        "cd /ws/switch-play-time-control-local "
        "&& git pull --ff-only origin master "
        "&& make test-host"
    )
    result = run(["ssh", "249-nintendo-switch-dev", remote_command])
    if PASS_TEXT not in result.stdout:
        raise VerificationError(f"missing success marker in remote C host output: {PASS_TEXT!r}")


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
    parser = argparse.ArgumentParser(description="Run and verify the C host core test.")
    parser.add_argument(
        "--remote",
        action="store_true",
        help="Run the remote devkitPro host target via SSH. This is the default.",
    )
    parser.add_argument(
        "--local",
        action="store_true",
        help="Compile and run locally for debugging when a C compiler is available.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.remote and args.local:
        print("--remote and --local cannot be used together")
        return 2
    steps = (
        [
            ("compile C host core", compile_local),
            ("run C host core", run_local_test),
        ]
        if args.local
        else [("remote C host core", run_remote_test)]
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
    print(f"summary: {passed}/{total} C host verification steps passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
