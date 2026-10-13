#!/usr/bin/env python3
"""Headless, read-only usage capture. No sys-botbase calls or hbmenu automation."""
from __future__ import annotations

import argparse
import ftplib
import hashlib
import json
from pathlib import Path
import time
import uuid

import package_usage_probe as pkg
from usage_probe import Device, Experiment, REMOTE, RELEASE, FLAG, FLAG_BACKUP, PROBE_FLAG, checked, deploy, dump, handoff, restore_flag, report, compare

CONFIG = REMOTE + "/boot-observe.json"


def optional(d: Device, path: str) -> bytes | None:
    try:
        return d.read(path)
    except ftplib.error_perm as exc:
        if not str(exc).startswith("550"):
            raise
        return None


def require_ready(ready: dict, latest: dict, plan: dict) -> None:
    if (ready.get("source_hash") != plan["source_hash"] or not ready.get("boot_plan_valid")
            or ready.get("boot_plan_id") != plan["plan_id"] or not ready.get("read_only_boot")
            or latest.get("boot_id") != ready.get("boot_id") or not latest.get("read_only_boot")
            or latest.get("writing_enabled") is not False or not latest.get("exclusive_owner")):
        raise RuntimeError("read-only boot identity, policy or owner proof failed")


def prepare(d: Device, out: Path, package: Path) -> dict:
    if (out / "autostart.json").exists():
        raise RuntimeError("existing autostart journal; resume or restore that transaction")
    manifest = pkg.verify(package)
    for path in (CONFIG, PROBE_FLAG, FLAG_BACKUP):
        if optional(d, path) is not None:
            raise RuntimeError("unresolved prior state: " + path)
    flag = optional(d, FLAG)
    if flag != b"":
        raise RuntimeError("expected existing empty standard boot flag")
    before_bytes = optional(d, REMOTE + "/latest.json")
    before = json.loads(before_bytes) if before_bytes else {}
    time.sleep(3)
    later_bytes = optional(d, REMOTE + "/latest.json")
    if before_bytes and later_bytes:
        later = json.loads(later_bytes)
        if later.get("boot_id") != before.get("boot_id") or int(later["tick_ns"]) > int(before["tick_ns"]):
            raise RuntimeError("existing probe is active; do not replace its running artifact")
    plan = {"version": 1, "plan_id": uuid.uuid4().hex, "source_hash": manifest["source_hash"],
            "phase": "preparing", "before_probe_session": before.get("boot_id"), "observe_timeout_s": 1200,
            "orchestrator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    dump(out / "autostart.json", plan)
    for name in ("build.json", "baseline.json", "ready.json", "command.json"):
        data = optional(d, REMOTE + "/" + name)
        if data is not None:
            (out / ("before-" + name)).write_bytes(data)
    pending = optional(d, REMOTE + "/command.json")
    if pending is not None:
        with d.ftp() as f:
            f.rename(REMOTE + "/command.json", REMOTE + "/command-autostart-quarantine-" + plan["plan_id"] + ".json")
    # Save the installed binaries before replacement; no previous evidence is overwritten.
    for name in manifest["artifacts"]:
        old = optional(d, "/" + name)
        if old is not None:
            path = out / "installed-preimage" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(old)
    dump(out / "deployed.json", deploy(d, package))
    config = {k: plan[k] for k in ("version", "plan_id", "source_hash", "observe_timeout_s")}
    d.put_json(CONFIG, config)
    if d.json(CONFIG) != config:
        raise RuntimeError("boot-observe config readback mismatch")
    try:
        handoff(d, out, wait_probe=False)
        d.write(PROBE_FLAG, b"")
        if d.read(PROBE_FLAG) != b"" or d.read(FLAG_BACKUP) != b"" or optional(d, FLAG) is not None:
            raise RuntimeError("committed boot flags do not match transaction")
    except Exception:
        if optional(d, PROBE_FLAG) == b"":
            with d.ftp() as f:
                f.delete(PROBE_FLAG)
        restore_flag(d, out)
        raise
    plan["phase"] = "awaiting_normal_reboot"
    dump(out / "autostart.json", plan)
    return plan


def wait_ready(d: Device, out: Path, plan: dict, seconds: int = 90, old_session: str | None = None) -> dict:
    deadline = time.monotonic() + seconds
    disconnected = False
    while time.monotonic() < deadline:
        try:
            ready = d.json(REMOTE + "/ready.json")
            if ready.get("boot_id") == (old_session or plan.get("before_probe_session")):
                time.sleep(2)
                continue
            a = d.json(REMOTE + "/latest.json")
            # ready.json precedes the first sample; an old latest during boot
            # is a publication race, not proof that the new policy failed.
            if a.get("boot_id") == (old_session or plan.get("before_probe_session")):
                time.sleep(2)
                continue
            require_ready(ready, a, plan)
            time.sleep(2)
            b = d.json(REMOTE + "/latest.json")
            require_ready(ready, b, plan)
            if int(b["tick_ns"]) > int(a["tick_ns"]):
                dump(out / ("restart-verification.json" if old_session else "startup-verification.json"),
                     {"ready": ready, "latest": b, "observed_connection_loss": disconnected,
                      "note": "boot_id is a process session; it alone is not system reboot proof"})
                return ready
        except (OSError, EOFError, ftplib.Error):
            disconnected = True
        time.sleep(2)
    raise TimeoutError("fresh read-only autostart not observed; no UI fallback attempted")


def phase(e: Experiment, name: str, seconds: int) -> None:
    e.log(f"无输入采集 {name}, {seconds}s")
    p = {"name": name, "requested_context": "normal console use; no remote input or capture",
         "observation": "not_visually_observed", "screenshots": [], "before": checked(e.d.command("sample"))["after"]}
    e.phases.append(p)
    e.checkpoint()
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        time.sleep(min(10, max(0, until-time.monotonic())))
        e.checkpoint()
    p["after"] = checked(e.d.command("sample"))["after"]
    p.update(status="captured", comparison=compare(p["before"], p["after"]))
    e.checkpoint()


def export_roundtrip(e: Experiment) -> dict:
    exported = e.action("export")
    try:
        with e.d.ftp() as f:
            before_size = f.size(REMOTE + "/samples.jsonl")
        e._download()
        with e.d.ftp() as f:
            after_size = f.size(REMOTE + "/samples.jsonl")
        if before_size != after_size:
            raise RuntimeError("sample log changed across acknowledged export barrier")
    finally:
        collected = e.action("collect")
    phase(e, "readonly-after-export", 15)
    with e.d.ftp() as f:
        resumed_size = f.size(REMOTE + "/samples.jsonl")
    if resumed_size is None or after_size is None or resumed_size <= after_size:
        raise RuntimeError("sample log did not resume after collect")
    return {"export_acknowledged": True, "collect_acknowledged": True,
            "frozen_size_before": before_size, "frozen_size_after": after_size,
            "resumed_size": resumed_size,
            "export_window_counter": compare(exported["after"], collected["before"]),
            "writing_enabled_after_collect": collected["after"]["writing_enabled"]}


def restore(d: Device, out: Path, plan: dict) -> dict:
    # Do not launch standard while our autostart flag remains enabled.
    flag = optional(d, PROBE_FLAG)
    if flag is not None:
        if flag != b"":
            raise RuntimeError("probe boot flag changed externally")
        with d.ftp() as f:
            f.delete(PROBE_FLAG)
    restore_flag(d, out)
    result = None
    try:
        ready = d.json(REMOTE + "/ready.json")
        latest = d.json(REMOTE + "/latest.json")
        require_ready(ready, latest, plan)
        result = d.command("handoff_standard", plan["plan_id"])
        dump(out / "standard-handoff-result.json", result)
    except (OSError, EOFError, ftplib.Error, RuntimeError) as exc:
        dump(out / "standard-handoff-pending.json", {"error": str(exc)})
    # Retain marker preimage after the live process has accepted handoff.
    config = optional(d, CONFIG)
    if config is not None:
        parsed = json.loads(config)
        if parsed.get("plan_id") != plan["plan_id"]:
            raise RuntimeError("observe config belongs to another transaction")
        with d.ftp() as f:
            f.rename(CONFIG, CONFIG + ".completed-" + plan["plan_id"])
    verified = False
    if optional(d, FLAG) == b"":
        time.sleep(6)
        rid = str(int(time.time()*1000)) + "-" + uuid.uuid4().hex[:8]
        q = {"version": 1, "request_id": rid, "type": "status", "created_at": int(time.time()), "payload": {}}
        d.put_json(RELEASE + "/inbox/pending/" + rid + ".json", q)
        for _ in range(30):
            try:
                response = d.json(RELEASE + "/results/" + rid + ".json")
                dump(out / "standard-restored-status.json", response)
                verified = response.get("status") == "ok"
                break
            except ftplib.error_perm:
                time.sleep(.5)
    plan.update(phase="restored" if verified else "flags_restored_manual_reboot_needed", standard_status_verified=verified)
    dump(out / "autostart.json", plan)
    return plan


def run(d: Device, out: Path, plan: dict, reboot: bool) -> None:
    try:
        ready = wait_ready(d, out, plan)
    except Exception:
        restore(d, out, plan)
        raise
    dump(out / "identity.json", {"probe": ready, "manifest": d.json(REMOTE + "/build.json")})
    e = Experiment(d, out)
    try:
        denial = d.command("begin")
        dump(out / "read-only-denial.json", denial)
        if (denial.get("ok") is not False or denial.get("reason") != "read_only_boot"
                or denial["before"]["settings_hex"] != denial["after"]["settings_hex"]
                or denial["after"].get("writing_enabled") is not False):
            raise RuntimeError("read-only write rejection test failed")
        phase(e, "autostart-baseline", 90)
        dump(out / "export-verification.json", export_roundtrip(e))
        if reboot:
            before = checked(d.command("sample"))["after"]
            e.action("export")
            e._download()
            ack = e.action("reboot_readonly", plan["plan_id"])
            if ack.get("ack_kind") != "pre_reboot_ack":
                raise RuntimeError("wrong reboot acknowledgement semantics")
            new = wait_ready(d, out, plan, old_session=ready["boot_id"])
            after = checked(d.command("sample"))["after"]
            verification = json.loads((out / "restart-verification.json").read_text(encoding="utf-8"))
            p = {"name": "readonly-software-reboot", "status": "captured", "before": before, "after": after,
                 "observation": "not_visually_observed", "screenshots": [], "new_process_session": new["boot_id"],
                 "observed_connection_loss": verification["observed_connection_loss"],
                 "reboot_evidence": "explicit BPC request, new autostart session and connection observation; random session alone insufficient"}
            e.phases.append(p)
            e.checkpoint()
            phase(e, "readonly-after-reboot", 60)
    finally:
        try:
            e.download(live=True)
        finally:
            try:
                restore(d, out, plan)
            except Exception as exc:
                dump(out / "cleanup-pending.json", {"error": str(exc),
                     "standard_flag": FLAG, "original_flag_backup": FLAG_BACKUP,
                     "probe_flag": PROBE_FLAG, "plan_id": plan["plan_id"]})
                raise
            finally:
                e.checkpoint()
                report(out)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=("prepare", "run", "restore"))
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--host", default="192.168.0.106")
    p.add_argument("--package", type=Path, default=pkg.ZIP)
    p.add_argument("--without-reboot", action="store_true")
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    d = Device(args.host)
    try:
        if args.action == "prepare":
            plan = prepare(d, args.out, args.package)
            print(json.dumps(plan, ensure_ascii=False), flush=True)
        else:
            plan = json.loads((args.out / "autostart.json").read_text(encoding="utf-8"))
            run(d, args.out, plan, not args.without_reboot) if args.action == "run" else restore(d, args.out, plan)
    except Exception as exc:
        dump(args.out / "autostart-error.json", {"type": type(exc).__name__, "error": str(exc)})
        raise


if __name__ == "__main__":
    main()
