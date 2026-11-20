"""Release-time navigation refresh checks using normal Eden input and receipts."""
from __future__ import annotations

from pathlib import Path
import time

from tools.automation.eden_main_flow import MainFlow
from tools.automation.eden_runner import decode, digest, read, require, save

REFRESH_SCENARIOS = ("setup", "navigation_refresh", "refresh_reopen")
STAGES = ("baseline", "cached", "manual", "idle_begin", "idle_end", "expired")


def validate_stages(stages: dict[str, dict], receipts: dict[str, dict]) -> None:
    require(set(stages) == set(STAGES), "refresh stage set differs")
    first = stages["baseline"]
    previous = None
    for name in STAGES:
        row = stages[name]
        require(type(row["host_monotonic_ns"]) is int and row["host_monotonic_ns"] > 0 and
                type(row["sequence"]) is int and row["sequence"] > 0 and
                isinstance(row["instance"], str) and bool(row["instance"]) and
                isinstance(row["result_ids"], list) and len(set(row["result_ids"])) == len(row["result_ids"]),
                "invalid refresh clock or result inventory")
        require(row["instance"] == first["instance"] and row["rules_sha256"] == first["rules_sha256"] and
                row["config_sha256"] == first["config_sha256"] and row["ledger_sha256"] == first["ledger_sha256"],
                "navigation changed business data or instance")
        require(row["ui"]["view"] == 1 and row["ui"]["overlay"] == 0 and row["ui"]["waiting"] is False and
                row["ui"]["restricted_now"] == 1 and row["ui"]["today_minutes"] == 1 and
                row["ui"]["parent_page"] == (1 if name.startswith("idle_") else 0),
                "refresh checkpoint is not the expected visible page")
        if previous:
            require(row["host_monotonic_ns"] > previous["host_monotonic_ns"] and
                    row["sequence"] > previous["sequence"] and
                    set(previous["result_ids"]) <= set(row["result_ids"]), "refresh evidence regressed")
        require(all(key in receipts and receipts[key]["request_id"] == key for key in row["result_ids"]),
                "refresh inventory lacks an original receipt")
        previous = row
    require(first["result_ids"] == stages["cached"]["result_ids"], "cached navigation submitted a request")
    require(stages["manual"]["result_ids"] == stages["idle_begin"]["result_ids"] ==
            stages["idle_end"]["result_ids"], "idle plan page submitted a request")
    require(stages["idle_end"]["host_monotonic_ns"] - stages["idle_begin"]["host_monotonic_ns"] >=
            122_000_000_000, "stale interval is too short")
    times = []
    for before, after in (("cached", "manual"), ("idle_end", "expired")):
        added = set(stages[after]["result_ids"]) - set(stages[before]["result_ids"])
        require(len(added) == 1, "refresh must have exactly one new receipt")
        rows = [receipts[key] for key in added]
        require(all(row.get("type") == "status" and row.get("status") == "ok" and
                    type(row.get("completed_at")) is int for row in rows), "refresh receipt is invalid")
        times.append(max(row["completed_at"] for row in rows))
    require(times[1] - times[0] >= 121, "guest receipt clocks do not prove expiry")


def verify_refresh(output: Path) -> None:
    stages = {name: read(output / f"refresh-evidence/{name}.json") for name in STAGES}
    receipts = {path.stem: read(path) for path in (output / "refresh-evidence/results").glob("*.json")}
    validate_stages(stages, receipts)
    originals = {}
    for name, row in stages.items():
        observed = read(output / f"refresh-evidence/{name}-ui.json")
        require(all(row[key] == observed[key] for key in ("instance", "sequence", "ui")),
                "refresh stage differs from original observation")
        originals[(observed["instance"], observed["sequence"])] = observed
        for field in ("rules", "config", "ledger"):
            path = output / f"refresh-evidence/{name}-{field}.raw"
            require((digest(path) if path.exists() else None) == row[f"{field}_sha256"],
                    "navigation business snapshot differs")
    found = set()
    streams = list((output / "ui-streams").glob("*.jsonl")) + [output / "app-final/support/automation-ui.jsonl"]
    for path in streams:
        with path.open("rb") as stream:
            for line in stream:
                require(len(line) <= 65536 and line.endswith(b"\n"), "incomplete refresh UI stream")
                observed = decode(line)
                key = (observed["instance"], observed["sequence"])
                if key in originals:
                    require(observed == originals[key], "refresh observation differs from raw stream")
                    found.add(key)
    require(found == set(originals), "refresh observation missing from raw stream")
    before = read(output / "checkpoints/refresh-before-reopen.json")
    after = read(output / "checkpoints/refresh_reopen.json")
    require(before["instance"] != after["instance"] and after["ui"]["view"] == 0 and
            after["ui"]["today_minutes"] == 120 and after["ui"]["restricted_now"] == 0,
            "refresh reopen does not prove usable persisted settings")
    rules = read(output / "app-final/rules.json")
    require(rules["today_override_present"] is True and rules["today_override_mode"] == "limit" and
            rules["today_override_minutes"] == 120, "refresh final rules differ")
    writes = []
    for path in (output / "receipts").glob("*.json"):
        row = read(path)
        require(row.get("request_id") == path.stem, "archived refresh receipt ID differs")
        writes.append(row)
    writes = [row for row in writes if row.get("type") == "set_today_limit"]
    require(len(writes) == 2 and len({r["request_id"] for r in writes}) == 2 and
            all(row["status"] == "ok" for row in writes), "restriction/restore receipts missing or duplicated")
    require(read(output / "assertions/refresh_reopen.json")["rules_sha256"] == digest(output / "app-final/rules.json"),
            "reopen changed persisted rules")


class RefreshFlow(MainFlow):
    def execute(self) -> None:
        self.start()
        for name in REFRESH_SCENARIOS:
            self.run_case(name, getattr(self, name))

    def refresh_snapshot(self, name: str) -> dict:
        observed = self.wait_ui(view=1, overlay=0, waiting=False)
        ids = sorted(self.ids())
        for request_id in ids:
            source = self.app / f"results/{request_id}.json"
            target = self.output / f"refresh-evidence/results/{request_id}.json"
            target.parent.mkdir(parents=True, exist_ok=True)
            raw = source.read_bytes()
            if target.exists():
                require(target.read_bytes() == raw, "original result changed during navigation")
            else:
                with target.open("xb") as stream:
                    stream.write(raw)
        row = {"host_monotonic_ns": time.monotonic_ns(), "result_ids": ids,
               **{key: observed[key] for key in ("instance", "sequence", "ui")}}
        for field, relative in (("rules", "rules.json"), ("config", "config.json"),
                                ("ledger", "ledger/used_nonces.jsonl")):
            source = self.app / relative
            target = self.output / f"refresh-evidence/{name}-{field}.raw"
            if source.exists():
                with target.open("xb") as stream:
                    stream.write(source.read_bytes())
                row[f"{field}_sha256"] = digest(target)
            else:
                require(field == "ledger", "required navigation business file missing")
                row[f"{field}_sha256"] = None
        save(self.output / f"refresh-evidence/{name}-ui.json", observed)
        save(self.output / f"refresh-evidence/{name}.json", row)
        return row

    def navigation_refresh(self) -> None:
        # A normal one-minute rule below the fixture's existing usage stops auto-refresh;
        # it makes navigation requests independently observable without test hooks.
        self.today(1)
        self.refresh(today_minutes=1, restricted_now=1)
        baseline = self.refresh_snapshot("baseline")
        self.capture("refresh-baseline")
        for _ in range(3):
            self.page(1)
            self.page(0)
        cached = self.refresh_snapshot("cached")
        require(cached["result_ids"] == baseline["result_ids"], "cached navigation submitted a request")
        self.capture("refresh-cached")
        self.refresh(today_minutes=1, restricted_now=1)
        self.refresh_snapshot("manual")
        self.capture("refresh-manual")
        self.page(1)
        idle = self.refresh_snapshot("idle_begin")
        until = time.monotonic() + 122
        while time.monotonic() < until:
            self.bounded()
            self.wait_ui(view=1, parent_page=1, overlay=0, waiting=False)
            time.sleep(1)
        idle_end = self.refresh_snapshot("idle_end")
        require(idle_end["result_ids"] == idle["result_ids"], "idle plan page submitted a request")
        before = set(idle["result_ids"])
        self.page(0)
        require(self.wait_receipt("status", before)["status"] == "ok", "expired navigation did not refresh")
        self.refresh_snapshot("expired")
        self.capture("refresh-expired")
        stages = {name: read(self.output / f"refresh-evidence/{name}.json") for name in STAGES}
        receipts = {path.stem: read(path) for path in (self.output / "refresh-evidence/results").glob("*.json")}
        validate_stages(stages, receipts)
        self.today(120)
        self.refresh(today_minutes=120, restricted_now=0)
        self.checked("navigation_refresh", ["three page round trips issue no requests or business writes",
                     "Y refresh obtains a new successful status", "expired navigation refreshes after 122 seconds",
                     "normal confirmation restores a usable limit without duplicate writes"])

    def refresh_reopen(self) -> None:
        self.capture("refresh-before-reopen")
        rules = digest(self.app / "rules.json")
        self.reopen()
        self.wait_ui(view=0, overlay=0, waiting=False, today_minutes=120, restricted_now=0)
        require(digest(self.app / "rules.json") == rules, "reopen changed the saved rules")
        self.checked("refresh_reopen", ["normal reopen loads the saved usable allowance"], rules_sha256=rules)
