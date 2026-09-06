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


FTP_INSTALL_PY = ROOT / "tools" / "install_package_via_ftp.py"
FTP_INSTALL_PS1 = ROOT / "tools" / "install_package_via_ftp.ps1"


def create_fake_lab_package(root: Path) -> Path:
    pkg = root / "fake_lab_package"
    app = pkg / "switch" / "playwise-device-lab"
    app.mkdir(parents=True, exist_ok=True)
    (app / "build.json").write_text('{"profile":"device-lab"}', encoding="utf-8")
    overlay = pkg / "switch" / ".overlays" / "playwise-device-lab.ovl"
    sysmodule = pkg / "atmosphere" / "contents" / "4200000000BD23F0" / "exefs.nsp"
    overlay.parent.mkdir(parents=True, exist_ok=True)
    sysmodule.parent.mkdir(parents=True, exist_ok=True)
    overlay.write_bytes(b"dummy_lab_overlay")
    sysmodule.write_bytes(b"dummy_lab_sysmodule")
    return pkg


def test_ftp_install_preview() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-test-ftp-install-") as tmp_dir:
        tmp_path = Path(tmp_dir)
        std_pkg = create_fake_mtp_package(tmp_path)
        lab_pkg = create_fake_lab_package(tmp_path)

        # 1. Default preview with standard package
        res_default = subprocess.run(
            [sys.executable, str(FTP_INSTALL_PY), "--source", str(std_pkg)],
            capture_output=True,
            text=True,
        )
        require(res_default.returncode == 0, f"FTP default preview failed: {res_default.stderr}")
        require("Target FTP:     ftp://192.168.8.178:5000/" in res_default.stdout, "Default FTP URL missing")
        require("Package type:   standard" in res_default.stdout, "Default standard package type missing")
        require("[-] /switch/playwise" in res_default.stdout, "Standard app clean path missing")
        require("[-] /atmosphere/contents/4200000000BD2300" in res_default.stdout, "Standard sysmodule clean path missing")
        require("Dry-run preview only" in res_default.stdout, "Dry-run notice missing")

        # 2. Lab package preview
        res_lab = subprocess.run(
            [sys.executable, str(FTP_INSTALL_PY), "--source", str(lab_pkg), "--lab"],
            capture_output=True,
            text=True,
        )
        require(res_lab.returncode == 0, f"FTP lab preview failed: {res_lab.stderr}")
        require("Package type:   lab" in res_lab.stdout, "Lab package type missing")
        require("[-] /switch/playwise-device-lab" in res_lab.stdout, "Lab clean path missing")

        # 3. Custom FTP URL preview
        res_custom_url = subprocess.run(
            [sys.executable, str(FTP_INSTALL_PY), "--source", str(std_pkg), "--url", "ftp://10.0.0.99:2121/custom"],
            capture_output=True,
            text=True,
        )
        require(res_custom_url.returncode == 0, f"FTP custom URL preview failed: {res_custom_url.stderr}")
        require("Target FTP:     ftp://10.0.0.99:2121/custom" in res_custom_url.stdout, "Custom URL not reflected")

        # 4. PowerShell wrapper preview on Windows
        if sys.platform == "win32":
            res_ps = subprocess.run(
                [
                    "powershell",
                    "-ExecutionPolicy",
                    "Bypass",
                    "-File",
                    str(FTP_INSTALL_PS1),
                    "-SourceFolder",
                    str(std_pkg),
                    "-FtpUrl",
                    "ftp://192.168.8.178:5000/",
                ],
                capture_output=True,
                text=True,
            )
            require(res_ps.returncode == 0, f"FTP PS1 preview failed: {res_ps.stderr}")
            require("PlayWise Switch FTP Full Clean Installer" in res_ps.stdout, "PS1 title missing")
            require("Target FTP:     ftp://192.168.8.178:5000/" in res_ps.stdout, "PS1 FTP target missing")


def test_ftp_install_safety_contract() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-test-ftp-safety-") as tmp_dir:
        tmp_path = Path(tmp_dir)
        std_pkg = create_fake_mtp_package(tmp_path)

        # Inject forbidden credentials.json
        cred_file = std_pkg / "switch" / "playwise" / "credentials.json"
        cred_file.write_text('{"secret":"leak"}', encoding="utf-8")

        res_unsafe = subprocess.run(
            [sys.executable, str(FTP_INSTALL_PY), "--source", str(std_pkg)],
            capture_output=True,
            text=True,
        )
        require(res_unsafe.returncode != 0, "Package with credentials.json must be rejected")
        require("credentials.json must not be installed" in res_unsafe.stderr, "Error message must mention credentials.json")

    # Safety checks in script contents
    py_code = FTP_INSTALL_PY.read_text(encoding="utf-8")
    require("assert_standard_package" in py_code, "FTP installer must assert standard package safety")
    require("remove_path" in py_code, "FTP installer must include robust removal method")
    require("DEFAULT_FTP_URL = \"ftp://192.168.8.178:5000/\"" in py_code, "FTP installer must default to specified switch URL")
    require("Dry-run preview only" in py_code, "FTP installer must require explicit apply")

    ps_code = FTP_INSTALL_PS1.read_text(encoding="utf-8")
    require("[string]$FtpUrl = \"ftp://192.168.8.178:5000/\"" in ps_code, "PS1 must default to specified switch URL")
    require("[switch]$Apply" in ps_code, "PS1 must support -Apply switch")
    require("[switch]$Lab" in ps_code, "PS1 must support -Lab switch")
    require("[switch]$Both" in ps_code, "PS1 must support -Both switch")


def main() -> int:
    test_install_script_preview()
    test_install_script_confirmation_uses_drive_letter_only()
    test_dbi_mtp_install_script_preview()
    test_dbi_mtp_install_script_safety_contract()
    test_device_lab_preparation_safety_contract()
    test_ftp_install_preview()
    test_ftp_install_safety_contract()
    print("Install script tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
