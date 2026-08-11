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


def release(version: str, *, root: Path = ROOT, verify: bool = True) -> str:
    version = validate_playwise_version(version)
    tag = f"v{version}"
    require_clean_worktree(root)
    require_missing_tag(tag, root)
    write_playwise_version(version, root)

    if verify:
        subprocess.run([sys.executable, "tools/package_remote.py"], cwd=root, check=True)

    run_git("add", *VERSION_PATHS, root=root)
    run_git("commit", "-m", f"chore(release): publish {version}", root=root)
    run_git("tag", "-a", tag, "-m", f"PlayWise {version}", root=root)
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
    print(f"released {read_playwise_version()}: {tag}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
