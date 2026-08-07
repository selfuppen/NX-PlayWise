#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
INSTALL_SCRIPT = ROOT / "tools" / "install_package_to_sd.ps1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def create_fake_package(root: Path) -> Path:
    pkg = root / "fake_package"
    app = pkg / "switch" / "playwise"
    app.mkdir(parents=True, exist_ok=True)
    (app / "config.json").write_text('{"version":1}', encoding="utf-8")
    (app / "setup.json").write_text('{"version":1}', encoding="utf-8")
    (app / "build.json").write_text('{"profile":"release"}', encoding="utf-8")
    (app / "pctc.nro").write_bytes(b"dummy_nro")
    install = pkg / "playwise-install"
    install.mkdir(parents=True, exist_ok=True)
    (install / "release-manifest.json").write_text('{"profile":"release"}', encoding="utf-8")
    return pkg


def test_install_script_preview() -> None:
    if sys.platform != "win32":
        print("Skipping PowerShell script test on non-Windows platform")
        return

    with tempfile.TemporaryDirectory(prefix="ptc-test-install-script-") as tmp_dir:
        pkg = create_fake_package(Path(tmp_dir))

        # 1. Incremental mode preview
        cmd_inc = [
            "powershell",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(INSTALL_SCRIPT),
            "-SourceFolder",
            str(pkg),
            "-Drive",
            ROOT.drive[0],
        ]
        res_inc = subprocess.run(cmd_inc, capture_output=True, text=True)
        require(res_inc.returncode == 0, f"Incremental preview failed: {res_inc.stderr}")
        require("Install mode:   Incremental update" in res_inc.stdout, "Incremental mode title missing")
        require("credentials, PIN, rules and runtime data are preserved" in res_inc.stdout, "Incremental mode text missing")

        # 2. Clean mode preview
        cmd_clean = cmd_inc + ["-Clean"]
        res_clean = subprocess.run(cmd_clean, capture_output=True, text=True)
        require(res_clean.returncode == 0, f"Clean preview failed: {res_clean.stderr}")
        require("Install mode:   Full clean install" in res_clean.stdout, "Clean mode title missing")
        require("switch\\playwise (full clean install)" in res_clean.stdout, "Clean mode text missing")

        # 3. Full mode preview
        cmd_full = cmd_inc + ["-Full"]
        res_full = subprocess.run(cmd_full, capture_output=True, text=True)
        require(res_full.returncode == 0, f"Full preview failed: {res_full.stderr}")
        require("Install mode:   Full clean install" in res_full.stdout, "Full mode title missing")


def main() -> int:
    test_install_script_preview()
    print("Install script tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
