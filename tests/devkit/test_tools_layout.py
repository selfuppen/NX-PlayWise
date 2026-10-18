#!/usr/bin/env python3
from __future__ import annotations

import ast
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
DAILY = {
    "test.py", "package_remote.py", "grant_code.py", "ptc_frontend_server.py",
    "protocol_probe.py", "release_version.py",
}


def test_catalog_and_layout() -> None:
    catalog = json.loads((TOOLS / "catalog.json").read_text(encoding="utf-8"))
    paths = [item["path"] for item in catalog["scripts"]]
    actual = {p.relative_to(TOOLS).as_posix() for p in TOOLS.rglob("*")
              if p.suffix in (".py", ".ps1") and "__pycache__" not in p.parts}
    assert catalog["schema_version"] == 2
    assert len(paths) == len(set(paths)) == catalog["script_count"]
    assert set(paths) == actual, (set(paths) - actual, actual - set(paths))
    assert {p for p in paths if "/" not in p} == DAILY
    directories = {item["path"] for item in catalog["directories"]}
    for item in catalog["scripts"]:
        path = Path(item["path"])
        assert not path.is_absolute() and ".." not in path.parts
        if "/" in item["path"]:
            assert path.parts[0] in directories and path.parts[0] == item["category"]
        else:
            assert item["category"] == "daily"
        if "legacy_path" in item:
            assert not (TOOLS / item["legacy_path"]).exists()
    readme = (TOOLS / "README.md").read_text(encoding="utf-8")
    assert all(f"]({p})" in readme for p in paths)


def test_cli_help_outside_checkout() -> None:
    # Help parses before any device action. An unrelated cwd catches lost root
    # discovery and imports that only succeed from an IDE or repository shell.
    with tempfile.TemporaryDirectory(prefix="playwise-cli-layout-") as directory:
        for script in sorted(TOOLS.rglob("*.py")):
            tree = ast.parse(script.read_text(encoding="utf-8"))
            has_parser = any(isinstance(node, ast.Import)
                             and any(alias.name == "argparse" for alias in node.names)
                             for node in ast.walk(tree))
            if not has_parser:
                continue
            result = subprocess.run([sys.executable, "-X", "utf8", str(script), "--help"],
                                    cwd=directory, capture_output=True, text=True,
                                    encoding="utf-8", timeout=30)
            assert result.returncode == 0, (script, result.stdout, result.stderr)
            assert "usage:" in result.stdout.lower(), script


def test_archived_imports_success_and_failure() -> None:
    runner = TOOLS / "compat/run_archived.py"
    with tempfile.TemporaryDirectory(prefix="playwise-archived-layout-") as directory:
        script = Path(directory) / "verify.py"
        script.write_text(
            "import sys\n"
            "import verify_device_qualification as old\n"
            "from tools.qualification import verify_device_qualification as current\n"
            "from package_remote import PackageError\n"
            "from tools.package_remote import PackageError as CurrentError\n"
            "assert old is current and PackageError is CurrentError\n"
            "assert sys.argv[1:] == ['expected']\n"
            "print('archive imports passed')\n", encoding="utf-8")
        before = hashlib.sha256(script.read_bytes()).hexdigest()
        result = subprocess.run([sys.executable, str(runner), str(script), "expected"],
                                cwd=directory, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert "archive imports passed" in result.stdout
        assert hashlib.sha256(script.read_bytes()).hexdigest() == before
        script.write_text("import missing_playwise_archived_module\n", encoding="utf-8")
        before = hashlib.sha256(script.read_bytes()).hexdigest()
        result = subprocess.run([sys.executable, str(runner), str(script)],
                                cwd=directory, capture_output=True, text=True)
        assert result.returncode != 0 and "ModuleNotFoundError" in result.stderr
        assert hashlib.sha256(script.read_bytes()).hexdigest() == before


def main() -> int:
    test_catalog_and_layout()
    test_cli_help_outside_checkout()
    test_archived_imports_success_and_failure()
    print("PASS: tools catalog, CLI paths and archived imports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
