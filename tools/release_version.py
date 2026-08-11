#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

from playwise_version import ROOT, read_playwise_version, validate_playwise_version, write_playwise_version


VERSION_PATHS = ("common/version.h", "common/version.mk")


def run_git(*args: str, root: Path = ROOT, capture: bool = False) -> str:
    result = subprocess.run(
        ["git", *args], cwd=root, check=True, text=True,
        stdout=subprocess.PIPE if capture else None,
    )
    return result.stdout.strip() if capture else ""


def git_has_changes(*paths: str, root: Path, cached: bool = False) -> bool:
    command = ["git", "diff", "--quiet", "--exit-code"]
    if cached:
        command.append("--cached")
    command.extend(("--", *paths))
    result = subprocess.run(command, cwd=root)
    if result.returncode not in (0, 1):
        raise subprocess.CalledProcessError(result.returncode, command)
    return result.returncode == 1


def worktree_entries(root: Path) -> list[tuple[str, str]]:
    output = subprocess.run(
        ["git", "status", "--porcelain=v1", "-z"],
        cwd=root,
        check=True,
        capture_output=True,
    ).stdout
    records = output.decode("utf-8", errors="surrogateescape").split("\0")
    entries: list[tuple[str, str]] = []
    index = 0
    while index < len(records) and records[index]:
        record = records[index]
        status = record[:2]
        entries.append((status, record[3:]))
        index += 2 if "R" in status or "C" in status else 1
    return entries


def refresh_verification_outputs(root: Path) -> None:
    for status, path in worktree_entries(root):
        if status == "??":
            continue
        if not git_has_changes(path, root=root) and not git_has_changes(path, root=root, cached=True):
            run_git("add", "--", path, root=root)

    unexpected = [path for _, path in worktree_entries(root) if path not in VERSION_PATHS]
    if unexpected:
        raise RuntimeError(
            "verification changed files outside the version sources: " + ", ".join(unexpected)
        )


def require_clean_worktree(root: Path) -> None:
    if run_git("status", "--porcelain", root=root, capture=True):
        raise RuntimeError("working tree is not clean; commit or stash existing changes first")


def require_missing_tag(tag: str, root: Path) -> None:
    result = subprocess.run(
        ["git", "rev-parse", "--verify", "--quiet", f"refs/tags/{tag}"],
        cwd=root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if result.returncode == 0:
        raise RuntimeError(f"tag already exists: {tag}")


def next_alpha_version(version: str) -> str:
    version = validate_playwise_version(version)
    stable_version = version.split("-", 1)[0]
    major, minor, patch = (int(part) for part in stable_version.split("."))
    return f"{major}.{minor}.{patch + 1}-alpha"


def release(version: str, *, root: Path = ROOT, verify: bool = True) -> str:
    version = validate_playwise_version(version)
    tag = f"v{version}"
    require_clean_worktree(root)
    require_missing_tag(tag, root)
    if read_playwise_version(root) != version:
        write_playwise_version(version, root)

    if verify:
        subprocess.run([sys.executable, "tools/package_remote.py"], cwd=root, check=True)

    refresh_verification_outputs(root)

    run_git("add", *VERSION_PATHS, root=root)
    if git_has_changes(*VERSION_PATHS, root=root, cached=True):
        run_git("commit", "-m", f"chore(release): publish {version}", root=root)
    run_git("tag", "-a", tag, "-m", f"PlayWise {version}", root=root)

    alpha_version = next_alpha_version(version)
    write_playwise_version(alpha_version, root)
    run_git("add", *VERSION_PATHS, root=root)
    run_git("commit", "-m", f"chore(release): start {alpha_version}", root=root)
    return tag


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Update the PlayWise version, verify, commit, and create an annotated tag.")
    parser.add_argument("version", help="Release version without the v prefix, for example 1.0.0")
    parser.add_argument(
        "--no-verify", action="store_true",
        help="Skip the authoritative remote package verification (intended only for isolated script tests).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    tag = release(args.version, verify=not args.no_verify)
    print(f"released {tag}; development version is now {read_playwise_version()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
