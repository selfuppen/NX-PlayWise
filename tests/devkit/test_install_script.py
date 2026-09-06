#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
INSTALL_SCRIPT = ROOT / "tools" / "install_package_to_sd.ps1"
MTP_INSTALL_SCRIPT = ROOT / "tools" / "install_package_via_dbi_mtp.ps1"
PREPARE_LAB_SCRIPT = ROOT / "tools" / "prepare_device_lab_sd.ps1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def create_fake_package(root: Path) -> Path:
    pkg = root / "fake_package"
    app = pkg / "switch" / "playwise"
    defaults = app / "defaults"
    defaults.mkdir(parents=True, exist_ok=True)
    for name in ("config.json", "auth.json", "rules.json", "state.json", "compatibility.json", "setup.json"):
        (defaults / name).write_text('{"version":1}', encoding="utf-8")
    (app / "build.json").write_text('{"profile":"release"}', encoding="utf-8")
    (app / "pctc.nro").write_bytes(b"dummy_nro")
    return pkg


def create_fake_mtp_package(root: Path) -> Path:
    pkg = create_fake_package(root)
    app = pkg / "switch" / "playwise"
    (app / "build.json").write_text(
        '{"profile":"release","release_id":"playwise-test"}', encoding="utf-8"
    )
    overlay = pkg / "switch" / ".overlays" / "playwise.ovl"
    sysmodule = pkg / "atmosphere" / "contents" / "4200000000BD2300" / "exefs.nsp"
    boot_flag = sysmodule.parent / "flags" / "boot2.flag"
    overlay.parent.mkdir(parents=True, exist_ok=True)
    boot_flag.parent.mkdir(parents=True, exist_ok=True)
    overlay.write_bytes(b"dummy_overlay")
    sysmodule.write_bytes(b"dummy_sysmodule")
    boot_flag.write_bytes(b"")

    artifacts = {}
    for relative, path in (
        ("switch/playwise/pctc.nro", app / "pctc.nro"),
        ("switch/.overlays/playwise.ovl", overlay),
        ("atmosphere/contents/4200000000BD2300/exefs.nsp", sysmodule),
    ):
        content = path.read_bytes()
        artifacts[relative] = {"size": len(content), "sha256": hashlib.sha256(content).hexdigest()}
    (app / "package-artifacts.json").write_text(
        json.dumps({"schema_version": 1, "release_id": "playwise-test", "artifacts": artifacts}),
        encoding="utf-8",
    )
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
        require("package assets (merge/replace)" in res_inc.stdout, "Incremental package merge text missing")
        require("credentials, PIN, rules and runtime data are preserved" in res_inc.stdout, "Incremental mode text missing")

        # 2. Clean mode preview
        cmd_clean = cmd_inc + ["-Clean"]
        res_clean = subprocess.run(cmd_clean, capture_output=True, text=True)
        require(res_clean.returncode == 0, f"Clean preview failed: {res_clean.stderr}")
        require("Install mode:   Full clean install" in res_clean.stdout, "Clean mode title missing")
        require("switch\\playwise (full clean install)" in res_clean.stdout, "Clean mode summary missing")

        # 3. Full mode preview
        cmd_full = cmd_inc + ["-Full"]
        res_full = subprocess.run(cmd_full, capture_output=True, text=True)
        require(res_full.returncode == 0, f"Full preview failed: {res_full.stderr}")
        require("Install mode:   Full clean install" in res_full.stdout, "Full mode title missing")


def test_install_script_confirmation_uses_drive_letter_only() -> None:
    script = INSTALL_SCRIPT.read_text(encoding="utf-8")
    require(
        'Read-Host "Type $destinationDriveLetter to confirm' in script,
        "confirmation prompt must request only the drive letter",
    )
    require(
        'backups\\album_restriction' not in script,
        "clean/full installs must not special-case legacy album restriction backups",
    )
    require(
        'if ($confirmation -cne $destinationDriveLetter)' in script,
        "confirmation must compare against only the drive letter",
    )


def test_dbi_mtp_install_script_preview() -> None:
    if sys.platform != "win32":
        print("Skipping DBI MTP PowerShell script test on non-Windows platform")
        return

    with tempfile.TemporaryDirectory(prefix="ptc-test-mtp-install-script-") as tmp_dir:
        pkg = create_fake_mtp_package(Path(tmp_dir))
        result = subprocess.run(
            [
                "powershell",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(MTP_INSTALL_SCRIPT),
                "-SourceFolder",
                str(pkg),
            ],
            capture_output=True,
            text=True,
        )
        require(result.returncode == 0, f"DBI MTP preview failed: {result.stderr}")
        require("Destination:    DBI MTP / SD Card" in result.stdout, "DBI raw SD target missing")
        require("Preview only. No device was accessed" in result.stdout, "offline preview contract missing")
        require("preserves existing config and data" in result.stdout, "preservation warning missing")

        wrong_target = subprocess.run(
            [
                "powershell",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(MTP_INSTALL_SCRIPT),
                "-SourceFolder",
                str(pkg),
                "-StorageName",
                "MicroSD install",
            ],
            capture_output=True,
            text=True,
        )
        require(wrong_target.returncode != 0, "DBI installer endpoint must be rejected")
        require("installer endpoint, not the raw SD root" in wrong_target.stderr,
                "DBI installer endpoint failure must explain the safe target")

        credentials = pkg / "switch" / "playwise" / "credentials.json"
        credentials.write_text('{"grant_secret":"must-not-ship"}', encoding="utf-8")
        unsafe_package = subprocess.run(
            [
                "powershell",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(MTP_INSTALL_SCRIPT),
                "-SourceFolder",
                str(pkg),
            ],
            capture_output=True,
            text=True,
        )
        require(unsafe_package.returncode != 0, "package containing credentials must be rejected")
        require("credentials.json must not be installed" in unsafe_package.stderr,
                "credential rejection must identify the unsafe seed")


def test_dbi_mtp_install_script_safety_contract() -> None:
    script = MTP_INSTALL_SCRIPT.read_text(encoding="utf-8")
    require("New-Object -ComObject Shell.Application" in script, "Windows Shell MTP transport missing")
    require("$shell.NameSpace(17)" in script, "This PC portable-device discovery missing")
    require("StorageName '$StorageName' is an installer endpoint" in script,
            "DBI NSP/XCI installer endpoint must be rejected")
    require("Test-MtpFileHash" in script and "Get-Sha256" in script,
            "MTP copies must be verified by reading files back")
    require("Remove-Item -LiteralPath $verifyDirectory" in script,
            "temporary verification files must be cleaned up")


def test_device_lab_preparation_safety_contract() -> None:
    script = PREPARE_LAB_SCRIPT.read_text(encoding="utf-8")
    require("[switch]$WipeAll" in script and "explicit -WipeAll" in script,
            "qualification preparation must require an explicit destructive mode")
    require("Refusing to prepare the Windows system drive" in script,
            "qualification preparation must reject the system drive")
    require("backup.json" in script and "device-lab-backups" in script,
            "all-clean preparation must create a recoverable host backup")
    require("-Both -CleanAll -Apply" in script,
            "qualification preparation must delegate to the verified dual installer")
    require("standard boot2.flag is missing or non-empty" in script,
            "post-install boot flag state must be verified")


def main() -> int:
    test_install_script_preview()
    test_install_script_confirmation_uses_drive_letter_only()
    test_dbi_mtp_install_script_preview()
    test_dbi_mtp_install_script_safety_contract()
    test_device_lab_preparation_safety_contract()
    print("Install script tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
