#!/usr/bin/env python3
"""Stage/source-check an internal PCTL tap; collect/restore without UI automation."""
from __future__ import annotations

import argparse
import ftplib
import hashlib
import json
import struct
from pathlib import Path
import time
import uuid
import zipfile

import package_pctl_capture as pkg
from usage_probe import Device, FLAG, RELEASE, dump
from usage_probe_autostart import optional

REMOTE = "/switch/playwise-pctl-capture"
EXEFS = "/atmosphere/contents/4200000000BD2300/exefs.nsp"
BACKUP = EXEFS + ".pctl-capture-original"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normalized_sample(raw: dict) -> dict:
    s = dict(raw)
    s.update(configured_available=False, configured_minutes=0, settings_mode="unknown")
    if raw.get("settings_result") != 0:
        return s
    try:
        words = struct.unpack("<34H", bytes.fromhex(raw["settings_hex"]))
        weekday = (int(raw["day_index"]) + 3) % 7
    except (KeyError, ValueError, TypeError, struct.error):
        return s
    # Same repository 0x44 contract, no inference from absent libnx wrappers.
    for day in range(7):
        flag, enabled, minutes = words[7+day*4:10+day*4]
        if flag not in (0, 0x600) or enabled not in (0, 0x100) or (minutes > 1440 and minutes != 65535):
            return s
    flag, enabled, minutes = words[7+weekday*4:10+weekday*4]
    s["weekday"] = weekday
    if flag == 0x600 and enabled == 0x100 and minutes <= 1440:
        s.update(configured_available=True, configured_minutes=minutes, settings_mode="limited" if minutes else "blocked")
    elif flag == enabled == minutes == 0:
        s["settings_mode"] = "unconfigured"
    elif flag == enabled == 0 and minutes == 65535:
        s["settings_mode"] = "unlimited"
    return s


def stage(d: Device, out: Path, package: Path) -> dict:
    identity = pkg.verify(package)
    original = d.read(EXEFS)
    if identity.get("installed_nsp_sha256") != sha(original):
        raise RuntimeError("current installed NSP differs from the build control preimage")
    if identity.get("baseline_loaded_segments_match") is not True or identity.get("baseline_npdm_match") is not True:
        raise RuntimeError("uninstrumented baseline/permissions must match actual installed backend")
    if (out / "capture-install.json").exists():
        raise RuntimeError("existing capture journal; collect or restore it")
    out.mkdir(parents=True, exist_ok=True)
    (out / "original-exefs.nsp").write_bytes(original)
    (out / "original-build.json").write_bytes(d.read(RELEASE + "/build.json"))
    with zipfile.ZipFile(package) as z:
        for name in z.namelist():
            data = z.read(name)
            d.write("/" + name, data)
            if sha(d.read("/" + name)) != sha(data):
                raise RuntimeError("capture staging readback mismatch")
    j = {"version": 1, "phase": "staged_inactive", "run_id": uuid.uuid4().hex,
         "source_hash": identity["source_hash"], "original_sha256": sha(original),
         "capture_sha256": identity["artifact"]["sha256"], "identity": identity,
         "runtime_before": d.json(RELEASE + "/handover/runtime-ready.json")}
    dump(out / "capture-install.json", j)
    return j


def activate(d: Device, out: Path, j: dict) -> dict:
    if j["phase"] != "staged_inactive":
        raise RuntimeError("activation requires an inactive staging transaction")
    if d.read(FLAG) != b"":
        raise RuntimeError("standard boot flag is not the expected existing empty file")
    for path in (BACKUP, REMOTE + "/capture.json"):
        if optional(d, path) is not None:
            raise RuntimeError("unresolved prior capture state: " + path)
    original = d.read(EXEFS)
    if sha(original) != j["original_sha256"]:
        raise RuntimeError("installed backend changed after staging")
    data = d.read(REMOTE + "/staged-exefs.nsp")
    if sha(data) != j["capture_sha256"] or pkg.nsp_member(data, "main.npdm") != pkg.nsp_member(original, "main.npdm"):
        raise RuntimeError("staged capture image/permissions changed")
    runtime = d.json(RELEASE + "/handover/runtime-ready.json")
    if runtime["release_id"] != j["identity"]["baseline_manifest"]["release_id"]:
        raise RuntimeError("running baseline identity differs")
    j["phase"] = "install_pending"
    dump(out / "capture-install.json", j)
    d.write(BACKUP, original)
    if sha(d.read(BACKUP)) != j["original_sha256"]:
        raise RuntimeError("original backend backup readback failed")
    config = {"version": 1, "run_id": j["run_id"], "source_hash": j["source_hash"], "duration_s": 3600}
    try:
        d.put_json(REMOTE + "/capture.json", config)
        if d.json(REMOTE + "/capture.json") != config:
            raise RuntimeError("capture config readback failed")
        d.write(EXEFS, data)
        if sha(d.read(EXEFS)) != j["capture_sha256"]:
            raise RuntimeError("installed capture readback failed")
    except Exception:
        # Both preimages remain on disk even if a network loss prevents rollback.
        j["phase"] = "install_outcome_requires_inspection"
        dump(out / "capture-install.json", j)
        raise
    j["phase"] = "awaiting_normal_reboot"
    j["runtime_after_disk_install"] = d.json(RELEASE + "/handover/runtime-ready.json")
    dump(out / "capture-install.json", j)
    return j


def collect(d: Device, out: Path, j: dict) -> dict:
    raw = out / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    samples, damaged = [], []
    with d.ftp() as f:
        names = f.nlst(REMOTE)
    for path in names:
        name = path.rsplit("/", 1)[-1]
        if not name.startswith(j["run_id"] + "-") or not name.endswith(".jsonl"):
            continue
        data = d.read(REMOTE + "/" + name)
        (raw / name).write_bytes(data)
        for number, line in enumerate(data.splitlines(), 1):
            try:
                s = json.loads(line)
            except ValueError:
                damaged.append({"file": name, "line": number})
                continue
            if s.get("source_hash") != j["source_hash"] or s.get("run_id") != j["run_id"] or s.get("tap_read_only") is not True:
                raise RuntimeError("raw capture identity mismatch")
            samples.append(s)
    runtime = d.json(RELEASE + "/handover/runtime-ready.json")
    dump(out / "runtime-current.json", runtime)
    samples.sort(key=lambda s: (int(s["unix_s"]), s["boot_id"], int(s["tick_ns"])))
    result = {"sample_count": len(samples), "sessions": sorted({s["boot_id"] for s in samples}),
              "current_runtime_boot_id": runtime["boot_id"],
              "damaged_rows": damaged, "files_sha256": {p.name: sha(p.read_bytes()) for p in raw.glob("*.jsonl")},
              "note": "active file download can include a partial last row; preserved raw bytes, not silently repaired"}
    dump(out / "capture-samples.json", samples)
    dump(out / "normalized-samples.json", [normalized_sample(s) for s in samples])
    dump(out / "collection.json", result)
    return result


def restore(d: Device, out: Path, j: dict) -> dict:
    original = (out / "original-exefs.nsp").read_bytes()
    if sha(original) != j["original_sha256"]:
        raise RuntimeError("local original preimage changed")
    config = optional(d, REMOTE + "/capture.json")
    if config is not None and json.loads(config).get("run_id") != j["run_id"]:
        raise RuntimeError("capture config belongs to another transaction")
    current = d.read(EXEFS)
    if sha(current) not in (j["original_sha256"], j["capture_sha256"]):
        raise RuntimeError("installed backend changed externally; do not overwrite")
    if sha(current) != j["original_sha256"]:
        d.write(EXEFS, original)
    if sha(d.read(EXEFS)) != j["original_sha256"]:
        raise RuntimeError("restored original image readback mismatch")
    if config is not None:
        with d.ftp() as f:
            f.rename(REMOTE + "/capture.json", REMOTE + "/capture-completed-" + j["run_id"] + ".json")
    j["phase"] = "original_restored_on_disk_tap_disabled"
    dump(out / "capture-install.json", j)
    return j


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=("stage", "activate", "collect", "restore"))
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--host", default="192.168.0.106")
    p.add_argument("--package", type=Path, default=pkg.ZIP)
    a = p.parse_args()
    d = Device(a.host)
    if a.action == "stage":
        j = stage(d, a.out, a.package)
    else:
        j = json.loads((a.out / "capture-install.json").read_text(encoding="utf-8"))
        j = {"activate": activate, "collect": collect, "restore": restore}[a.action](d, a.out, j)
    print(json.dumps({k: j[k] for k in ("phase", "run_id", "source_hash", "sample_count") if k in j}, ensure_ascii=False))


if __name__ == "__main__":
    main()
