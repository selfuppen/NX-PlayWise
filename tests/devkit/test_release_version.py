#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from playwise_version import read_playwise_version  # noqa: E402
from release_version import release  # noqa: E402


def git(root: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=root, check=True, text=True, capture_output=True
    ).stdout.strip()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def make_repository(root: Path) -> None:
    common = root / "common"
    common.mkdir()
    (common / "version.h").write_text(
        '#ifndef PLAYWISE_VERSION_H\n#define PLAYWISE_VERSION_H\n#define PLAYWISE_VERSION "0.1.4"\n#endif\n',
        encoding="utf-8",
    )
    (common / "version.mk").write_text("PLAYWISE_VERSION := 0.1.4\n", encoding="utf-8")
    git(root, "init")
    git(root, "config", "user.name", "PlayWise Test")
    git(root, "config", "user.email", "playwise-test@example.invalid")
    git(root, "add", ".")
    git(root, "commit", "-m", "initial")


def test_release_creates_commit_and_annotated_tag() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-release-") as tmp_dir:
        root = Path(tmp_dir)
        make_repository(root)
        require(release("1.0.0", root=root, verify=False) == "v1.0.0", "release must return tag")
        require(read_playwise_version(root) == "1.0.0", "release must update the version pair")
        require(git(root, "tag", "--list", "v1.0.0") == "v1.0.0", "release tag must exist")
        require(git(root, "cat-file", "-t", "v1.0.0") == "tag", "release tag must be annotated")
        require(not git(root, "status", "--porcelain"), "release must leave a clean worktree")


def test_existing_tag_is_not_moved() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-release-") as tmp_dir:
        root = Path(tmp_dir)
        make_repository(root)
        git(root, "tag", "v1.0.0")
        original = git(root, "rev-parse", "v1.0.0")
        try:
            release("1.0.0", root=root, verify=False)
        except RuntimeError as exc:
            require("already exists" in str(exc), "tag collision must be explained")
        else:
            raise AssertionError("existing tag must reject the release")
        require(git(root, "rev-parse", "v1.0.0") == original, "existing tag must not move")
        require(read_playwise_version(root) == "0.1.4", "collision must not edit version sources")


def main() -> int:
    test_release_creates_commit_and_annotated_tag()
    test_existing_tag_is_not_moved()
    print("Release version script tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
