#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import zipfile


APP_DIR = Path("switch") / "playwise"
TEMPLATE_DIR = Path("playwise-install") / "templates"
ATMOSPHERE_CONTENT_DIR = Path("atmosphere") / "contents" / "4200000000BD2300"
INSTALL_SCRIPT = Path(__file__).with_name("install_package_to_sd.ps1")


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def copy_file(src: Path, dst: Path) -> None:
    if not src.is_file():
        raise FileNotFoundError(f"missing input file: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def write_zip(root: Path, zip_path: Path) -> None:
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as package:
        for directory in sorted(path for path in root.rglob("*") if path.is_dir()):
            package.writestr(directory.relative_to(root).as_posix() + "/", "")
        for file_path in sorted(path for path in root.rglob("*") if path.is_file()):
            package.write(file_path, file_path.relative_to(root).as_posix())


def require_file(parser: argparse.ArgumentParser, option: str, path: Path | None) -> Path | None:
    if path is None:
        return None
    if not path.is_file():
        parser.error(f"{option} file not found: {path}. Replace the example path with a real build artifact, or omit {option}.")
    return path


def create_package(
    out: Path,
    *,
    include_boot2: bool,
    device_id: str,
    grant_secret: str,
    max_add_minutes: int,
    nro: Path | None,
    sysmodule_exefs: Path | None,
    toolbox: Path | None,
    overlay: Path | None = None,
) -> None:
    if out.exists():
        shutil.rmtree(out)
    app = out / APP_DIR
    templates = out / TEMPLATE_DIR
    for directory in [
        app / "inbox" / "pending",
        app / "inbox" / "processing",
        app / "inbox" / "done",
        app / "results",
        app / "logs",
        app / "ledger",
        app / "backups",
        app / "flags",
    ]:
        directory.mkdir(parents=True, exist_ok=True)

    write_json(
        templates / "config.json",
        {
            "version": 1,
            "device_id": device_id,
            "grant_secret": grant_secret,
            "max_add_minutes": max_add_minutes,
            "control_mode": "enforce",
            "allow_unlimited_to_limited": True,
            "default_request_timeout_ms": 60000,
        },
    )
    write_json(templates / "auth.json", {"version": 1, "pin_hash": "", "pin_salt": "", "hash": "hmac-sha256", "updated_at": 0})
    write_json(
        templates / "rules.json",
        {
            "version": 1,
            "week": [
                {"mode": "unlimited", "minutes": 120},
                {"mode": "limit", "minutes": 60},
                {"mode": "limit", "minutes": 60},
                {"mode": "limit", "minutes": 60},
                {"mode": "limit", "minutes": 60},
                {"mode": "limit", "minutes": 60},
                {"mode": "unlimited", "minutes": 120},
            ],
            "today_override_present": False,
            "today_override_day_index": 0,
            "today_override_mode": "limit",
            "today_override_minutes": 60,
            "bedtime_enabled": False,
            "bedtime_start_min": 1260,
            "bedtime_end_min": 480,
            "limit_action": "remind",
        },
    )
    write_json(
        templates / "state.json",
        {
            "version": 1,
            "parent_unlock_until": 0,
            "last_enforced_day_index": 0,
            "last_enforced_mode": 0,
            "last_enforced_minutes": 0,
            "last_enforced_bedtime_active": False,
            "updated_at": 0,
        },
    )
    write_json(
        templates / "capabilities.json",
        {
            "version": 1,
            "raw_block_verified": False,
            "raw_block_backend": "pctl-s-rawblock-v1",
            "suspend_verified": False,
            "suspend_backend": "pctl-s-suspend-v1",
            "verified_at": {"raw_block": 0, "suspend": 0},
        },
    )
    write_json(
        templates / "setup.json",
        {
            "version": 1,
            "phase": "pending",
            "restriction_cleared": False,
            "snapshot_available": False,
            "activate_after": 0,
            "last_error": "",
        },
    )

    # Keep the installable seed beside the Companion binary.  The separate
    # template directory is retained as the immutable source-of-truth for
    # tooling, while the installer consumes these seed files without ever
    # replacing runtime data on the SD card.
    for template_file in sorted(templates.glob("*.json")):
        copy_file(template_file, app / template_file.name)

    copy_file(INSTALL_SCRIPT, out / "install-playwise.ps1")

    if nro is not None:
        copy_file(nro, app / nro.name)

    if overlay is not None:
        copy_file(overlay, out / "switch" / ".overlays" / overlay.name)

    if sysmodule_exefs is not None:
        copy_file(sysmodule_exefs, out / ATMOSPHERE_CONTENT_DIR / "exefs.nsp")

    if toolbox is not None:
        copy_file(toolbox, out / ATMOSPHERE_CONTENT_DIR / "toolbox.json")

    if include_boot2 and sysmodule_exefs is not None:
        boot2 = out / ATMOSPHERE_CONTENT_DIR / "flags" / "boot2.flag"
        boot2.parent.mkdir(parents=True, exist_ok=True)
        boot2.write_text("", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Create the PlayWise SDMC package.")
    parser.add_argument("--out", required=True)
    parser.add_argument("--device-id", default="kid-switch")
    parser.add_argument("--grant-secret", default="replace-with-long-random-secret")
    parser.add_argument("--max-add-minutes", type=int, default=120)
    parser.add_argument("--zip", dest="zip_path", help="Write a zip whose top-level entries are switch/ and optional atmosphere/.")
    parser.add_argument("--nro", type=Path, help="Optional companion NRO copied under switch/playwise/.")
    parser.add_argument("--sysmodule-exefs", type=Path, help="Optional sysmodule exefs.nsp copied under atmosphere/contents.")
    parser.add_argument("--overlay", type=Path, help="Optional Tesla overlay copied under switch/.overlays.")
    parser.add_argument("--toolbox", type=Path, help="Optional Atmosphere toolbox.json copied beside exefs.nsp.")
    parser.add_argument("--boot2", action="store_true", help="Include boot2.flag; requires --sysmodule-exefs.")
    args = parser.parse_args()

    if args.max_add_minutes <= 0:
        parser.error("--max-add-minutes must be positive")
    args.nro = require_file(parser, "--nro", args.nro)
    args.sysmodule_exefs = require_file(parser, "--sysmodule-exefs", args.sysmodule_exefs)
    args.overlay = require_file(parser, "--overlay", args.overlay)
    args.toolbox = require_file(parser, "--toolbox", args.toolbox)
    if args.boot2 and args.sysmodule_exefs is None:
        parser.error("--boot2 requires --sysmodule-exefs so the package cannot enable an empty boot2 entry")

    out = Path(args.out)
    create_package(
        out,
        include_boot2=args.boot2,
        device_id=args.device_id,
        grant_secret=args.grant_secret,
        max_add_minutes=args.max_add_minutes,
        nro=args.nro,
        sysmodule_exefs=args.sysmodule_exefs,
        overlay=args.overlay,
        toolbox=args.toolbox,
    )
    if args.zip_path is not None:
        write_zip(out, Path(args.zip_path))
        print(Path(args.zip_path))
    print(Path(args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
