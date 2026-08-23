#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import zipfile


TITLE_ID = "4200000000BD23F0"
APP_ROOT = Path("switch") / "playwise-device-lab"


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def copy_required(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(f"missing Device Lab artifact: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def create_package(out: Path, manifest_path: Path, sysmodule_exefs: Path, nro: Path, overlay: Path) -> None:
    if out.exists():
        shutil.rmtree(out)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("profile") != "device-lab":
        raise ValueError("Device Lab package requires profile=device-lab")
    for relative in (
        "inbox/pending",
        "inbox/processing",
        "inbox/done",
        "results",
        "logs/undated",
        "ledger",
        "backups",
        "recovery/active",
        "flags",
        "lab",
        "reports",
    ):
        (out / APP_ROOT / relative).mkdir(parents=True, exist_ok=True)
    write_json(out / APP_ROOT / "build.json", manifest)
    write_json(
        out / APP_ROOT / "config.json",
        {"version": 1, "device_id": "device-lab", "max_add_minutes": 1440, "default_request_timeout_ms": 300000},
    )
    write_json(
        out / APP_ROOT / "rules.json",
        {
            "version": 1,
            "week": [{"mode": "unlimited", "minutes": 0} for _ in range(7)],
            "today_override_present": False,
            "today_override_day_index": 0,
            "today_override_mode": "unlimited",
            "today_override_minutes": 0,
            "scheduled_override_enabled": False,
            "scheduled_override_start_day_index": 0,
            "scheduled_override_end_day_index": 0,
            "scheduled_override_mode": "limit",
            "scheduled_override_minutes": 60,
            "daily_buffer_minutes": 0,
        },
    )
    write_json(
        out / APP_ROOT / "setup.json",
        {
            "version": 1,
            "phase": "active",
            "compatibility_status": "lab_override",
            "restriction_cleared": False,
            "snapshot_available": False,
            "activate_after": 0,
            "last_error": "",
        },
    )
    copy_required(nro, out / APP_ROOT / "playwise-device-lab.nro")
    copy_required(overlay, out / "switch" / ".overlays" / "playwise-device-lab.ovl")
    copy_required(sysmodule_exefs, out / "atmosphere" / "contents" / TITLE_ID / "exefs.nsp")
    warning = out / "DEVICE-LAB.txt"
    warning.write_text(
        "任我玩 DEVICE LAB - 内部取证工具 / 危险操作\n"
        "此配置不是公开发行版，并且有意不包含 boot2.flag。\n"
        "请从 Device Lab NRO 启用实验后台，完成取证和精确恢复后再切回正常后台。\n",
        encoding="utf-8",
        newline="\n",
    )


def write_zip(root: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as package:
        for path in sorted(root.rglob("*")):
            if path.is_file():
                package.write(path, path.relative_to(root).as_posix())


def main() -> int:
    parser = argparse.ArgumentParser(description="Create the isolated internal PlayWise Device Lab package.")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--zip", dest="zip_path", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--sysmodule-exefs", type=Path, required=True)
    parser.add_argument("--nro", type=Path, required=True)
    parser.add_argument("--overlay", type=Path, required=True)
    args = parser.parse_args()
    create_package(args.out, args.manifest, args.sysmodule_exefs, args.nro, args.overlay)
    write_zip(args.out, args.zip_path)
    print(args.zip_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
