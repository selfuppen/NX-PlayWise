#!/usr/bin/env python3
"""Bounded PCTL mainline supplement through existing release requests only."""
from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
import ftplib
import json
import math
import time
import uuid

from tools.legacy.pctl_capture import collect, normalized_sample
from tools.legacy.pctl_usage_evidence import transition
from tools.legacy.usage_probe import Device, RELEASE, dump
from tools.legacy.usage_probe_autostart import optional
from tools.automation.sys_botbase_client import SysBotBaseClient


class Mainline:
    def __init__(self, d: Device, out: Path) -> None:
        self.d, self.out = d, out
        self.install = json.loads((out / "capture-install.json").read_text(encoding="utf-8"))
        journal = out / "mainline-run.json"
        previous = json.loads(journal.read_text(encoding="utf-8")) if journal.exists() else {}
        self.operations: list[dict] = previous.get("operations", [])
        self.phases: list[dict] = previous.get("phases", [])
        self.operation_start = len(self.operations)

    def checkpoint(self) -> None:
        dump(self.out / "mainline-run.json", {"operations": self.operations, "phases": self.phases})

    def request(self, kind: str, payload: dict | None = None) -> dict:
        rid = uuid.uuid4().hex
        q = {"version": 1, "request_id": rid, "type": kind, "created_at": int(time.time()), "payload": payload or {}}
        op = {"request": q, "host_sent_unix_s": int(time.time()), "outcome": "pending"}
        self.operations.append(op)
        self.checkpoint()
        self.d.put_json(RELEASE + "/inbox/pending/" + rid + ".json", q)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            data = optional(self.d, RELEASE + "/results/" + rid + ".json")
            if data is not None:
                r = json.loads(data)
                if r.get("request_id") != rid or r.get("type") != kind:
                    raise RuntimeError("release result identity mismatch")
                op.update(outcome="acknowledged", result=r)
                self.checkpoint()
                if r.get("status") != "ok":
                    raise RuntimeError("release refused " + kind + ": " + json.dumps(r.get("error"), ensure_ascii=False))
                return r
            time.sleep(.5)
        op["outcome"] = "unknown_no_replay"
        self.checkpoint()
        raise TimeoutError("original request outcome unknown; never resend: " + rid)

    def sample(self, after_tick: int | None = None) -> dict:
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            collection = collect(self.d, self.out, self.install)
            rows = json.loads((self.out / "capture-samples.json").read_text(encoding="utf-8"))
            rows = [s for s in rows if s["boot_id"] == collection["current_runtime_boot_id"]]
            rows.sort(key=lambda s: int(s["tick_ns"]))
            if rows:
                s = normalized_sample(rows[-1])
                if s.get("sample_error") != 0 or s.get("spent_result") != 0:
                    raise RuntimeError("direct PCTL read failed")
                if after_tick is None or int(s["tick_ns"]) > after_tick:
                    return s
            time.sleep(2)
        raise TimeoutError("no fresh source-bound PCTL tap sample")

    def picture(self, name: str) -> None:
        try:
            path = self.out / "screenshots" / (name + ".jpg")
            with SysBotBaseClient(self.d.host, timeout=5) as c:
                c.capture_screenshot(path)
            dump(self.out / (name + "-picture.json"), {"path": "screenshots/" + path.name, "review": "pending_final_human_review", "no_input_injected": True})
        except (OSError, EOFError, RuntimeError, ValueError) as exc:
            dump(self.out / (name + "-picture.json"), {"error": str(exc), "review": "capture_unavailable", "non_blocking": True})

    def phase(self, name: str, seconds: int) -> dict:
        a = self.sample()
        p = {"name": name, "before": a, "requested_seconds": seconds, "status": "running"}
        self.phases.append(p)
        self.checkpoint()
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            time.sleep(min(5, max(0, until-time.monotonic())))
        # A periodic log can lag host time by one sampling interval. Require
        # the device endpoint itself to span the requested observation window.
        b = self.sample(after_tick=int(a["tick_ns"])+seconds*10**9-1)
        p.update(after=b, status="captured", comparison=transition(a, b))
        self.checkpoint()
        print(json.dumps({"phase": name, "comparison": p["comparison"]}, ensure_ascii=False), flush=True)
        return p

    def action(self, kind: str, payload: dict | None = None) -> dict:
        before = self.sample()
        result = self.request(kind, payload)
        after = self.sample(after_tick=int(before["tick_ns"]))
        record = {"name": kind, "request_id": result["request_id"], "before": before, "after": after,
                  "status": "captured", "comparison": transition(before, after), "release_state": result.get("state")}
        self.phases.append(record)
        self.checkpoint()
        print(json.dumps({"action": kind, "spent_s": int(after["spent_ns"])/1e9,
                          "minutes": after.get("configured_minutes"), "restricted": after.get("restricted")}, ensure_ascii=False), flush=True)
        return record

    def grant_chain(self) -> None:
        rules_bytes = self.d.read(RELEASE + "/rules.json")
        rules = json.loads(rules_bytes)
        (self.out / "rules-chain-preimage.json").write_bytes(rules_bytes)
        if not (rules.get("today_override_present") and rules.get("today_override_mode") == "unlimited" and rules.get("today_override_minutes") == 0):
            raise RuntimeError("this supplement requires the observed original unlimited-today preimage")
        state = self.request("status")["state"]
        bedtime = state["bedtime"]
        skipped_by_us = False
        quota_changed = False
        try:
            if bedtime.get("active") and not bedtime.get("skipped"):
                self.action("skip_bedtime", {"window_instance_id": bedtime["window_instance_id"]})
                skipped_by_us = True
            spent = int(self.sample()["spent_ns"])/1e9
            base = math.ceil(spent/60) - 180
            if base < 1 or base + 240 > 1440:
                raise RuntimeError("current usage cannot support the planned two-grant crossing within valid daily quotas")
            self.action("set_today_limit", {"minutes": base})
            quota_changed = True
            self.picture("chain-base-restricted")
            self.phase("chain-initial-restricted", 15)
            self.action("add_today_minutes", {"minutes": 120})
            self.phase("chain-first-grant-still-restricted", 15)
            second = self.action("add_today_minutes", {"minutes": 120})
            if second["after"].get("restricted"):
                raise RuntimeError("second grant did not clear the measured restriction")
            self.picture("chain-second-grant-clear")
            self.phase("chain-after-second-grant", 60)
        finally:
            # Queue results are recorded before restoration; an ambiguous mutation
            # leaves a recovery record rather than assuming it committed.
            owned = self.operations[self.operation_start:]
            unknown = [o for o in owned if o["outcome"] in ("pending", "unknown_no_replay") and o["request"]["type"] != "status"]
            if unknown:
                dump(self.out / "scenario-cleanup-pending.json", {"reason": "ambiguous original request", "ids": [o["request"]["request_id"] for o in unknown]})
            else:
                committed = {o["request"]["type"] for o in owned if o.get("result", {}).get("status") == "ok"}
                quota_changed = "set_today_limit" in committed
                skipped_by_us = "skip_bedtime" in committed
                if quota_changed:
                    self.request("disable_today_limit")
                if skipped_by_us:
                    self.request("clear_bedtime_skip", {"window_instance_id": bedtime["window_instance_id"]})
                after = self.d.read(RELEASE + "/rules.json")
                dump(self.out / "scenario-restoration.json", {"rules_semantically_equal": json.loads(after) == rules,
                     "bedtime_skip_cleared": skipped_by_us, "status": self.request("status")})
                self.checkpoint()


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--host", default="192.168.0.106")
    a = p.parse_args()
    Mainline(Device(a.host), a.out).grant_chain()
